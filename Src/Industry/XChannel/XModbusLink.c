/**
 * @file       XModbusLink.c
 * @brief      Modbus 链路连接实现（基址偏移密集表 + 单次订阅）。
 * @details    [2026-11-02 XChannel 重写]
 *             本文件内**没有任何地址查找循环**：Hub 信号带 (type, vStart, count)，
 *             本类按区取密集表、算下标、直接取 role。区间回填是顺序写数组
 *             （缓存友好的连续访问），不是查找。
 */
#include "XModbusLink.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

#include "XIoChannelGroup.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 区映射 ==================== */

int XModbusLink_areaOfType(int type, bool useHolding)
{
    switch (type) {
    case (int)XIoPoint_DigitalInput:  return 0;   /* DI  → DiscreteInputs */
    case (int)XIoPoint_DigitalOutput: return 1;   /* DO  → Coils           */
    case (int)XIoPoint_AnalogInput:   return useHolding ? 2 : 3; /* AI → HR/IR */
    case (int)XIoPoint_AnalogOutput:  return 2;   /* AO  → HoldingRegisters */
    default: return -1;
    }
}

XModbusRegisterType XModbusLink_hubAreaType(int area)
{
    switch (area) {
    case 0: return XModbusDiscreteInputs;
    case 1: return XModbusCoils;
    case 2: return XModbusHoldingRegisters;
    case 3: return XModbusInputRegisters;
    default: return XModbusHoldingRegisters;
    }
}

/* ==================== 密集表内部助手 ==================== */

/**
 * @brief 在区内登记一条 (addr → role) 映射。
 * @details 若地址落在现有表内直接写；否则扩容表以覆盖新区间（向左或向右扩展，
 *          旧内容平移到新位置）。扩容只发生在装配期（BindRole），不在数据路径上。
 * @param map 目标区表。
 * @param addr 虚拟地址。
 * @param role 语义槽号。
 * @return 成功 true；内存分配失败返回 false。
 */
static bool areaMapBind(XModbusLinkAreaMap* map, uint16_t addr, uint16_t role)
{
    if (!map) return false;

    if (map->m_roleByAddr && addr >= map->m_base &&
        (uint32_t)(addr - map->m_base) < map->m_span) {
        map->m_roleByAddr[addr - map->m_base] = role;
        return true;
    }

    /* 需要新建或扩容：算出覆盖旧区间与新区间的最小范围。 */
    {
        uint32_t newBase = map->m_roleByAddr ? map->m_base : addr;
        uint32_t newEnd  = map->m_roleByAddr ? (uint32_t)map->m_base + map->m_span
                                             : (uint32_t)addr;
        uint32_t end = (uint32_t)addr + 1u;
        uint32_t span;
        uint16_t* table;

        if (addr < newBase) newBase = addr;
        if (end > newEnd) newEnd = end;
        span = newEnd - newBase;
        if (span == 0) return false;

        table = (uint16_t*)XMemory_malloc(sizeof(uint16_t) * span,
                                          XCLASS_DEFAULT_MEMORY_TYPE);
        if (!table) return false;
        {
            uint32_t i;
            for (i = 0; i < span; ++i) table[i] = XMODBUSLINK_ROLE_NONE;
        }
        if (map->m_roleByAddr) {
            /* 旧内容平移到新位置（保持 addr - base 的对应关系）。 */
            memcpy(table + (map->m_base - newBase), map->m_roleByAddr,
                   sizeof(uint16_t) * map->m_span);
            XMemory_free(map->m_roleByAddr, XCLASS_DEFAULT_MEMORY_TYPE);
        }
        table[addr - newBase] = role;
        map->m_roleByAddr = table;
        map->m_base = (uint16_t)newBase;
        map->m_span = span;
        return true;
    }
}

/**
 * @brief 区内按地址查 role（O(1)：一次减法、一次判界、一次取址）。
 * @param map 区表；可为 NULL。
 * @param addr 虚拟地址。
 * @return role；未绑定返回 XMODBUSLINK_ROLE_NONE。
 */
static uint16_t areaMapFind(const XModbusLinkAreaMap* map, uint16_t addr)
{
    uint32_t off;
    if (!map || !map->m_roleByAddr) return XMODBUSLINK_ROLE_NONE;
    if (addr < map->m_base) return XMODBUSLINK_ROLE_NONE;
    off = (uint32_t)(addr - map->m_base);
    if (off >= map->m_span) return XMODBUSLINK_ROLE_NONE;
    return map->m_roleByAddr[off];
}

/* ==================== Hub 信号槽（各订阅一次） ==================== */

/**
 * @brief Hub 值变化槽：按区取表，连续区间顺序回填。
 * @param receiver 连接的接收者（即本链路）；**不是** Hub，禁止按 Hub 强转。
 * @param args 信号参数（int type, int virtualStart, int count）。
 * @return 无。
 * @note 旧实现是「每点各订阅一次，广播后各自比对地址丢弃」（O(N)）。
 *       现在是一条连接 + 区间顺序访问（O(count)，count 是本帧覆盖的地址数）。
 */
static void VXModbusLink_onHubValueChanged(XObject* receiver, XVarList* args)
{
    XModbusLink* self;
    XIoChannelGroup* group;
    int area;
    int vStart;
    int count;
    int typeInt;
    int i;

    if (!receiver || !args) return;
    self = (XModbusLink*)receiver;
    if (!self->m_hub) return;
    group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    if (!group) return;

    XVarList_args_3(args, int, type, int, start, int, cnt);
    typeInt = type;
    vStart = start;
    count = cnt;

    /* 区下表由 Hub 寄存器类型决定：DI=0 / DO=1 / HR=2 / IR=3。
     * Hub 回填时给的 type 就是寄存器区，直接映射到本类区下标。 */
    switch (typeInt) {
    case (int)XModbusDiscreteInputs:  area = 0; break;
    case (int)XModbusCoils:           area = 1; break;
    case (int)XModbusHoldingRegisters: area = 2; break;
    case (int)XModbusInputRegisters:  area = 3; break;
    default: return;
    }

    if (vStart < 0 || count <= 0) return;

    /* 顺序回填：连续区间的缓存友好遍历（不是查找）。 */
    for (i = 0; i < count; ++i) {
        uint32_t addr = (uint32_t)vStart + (uint32_t)i;
        uint16_t role;
        uint16_t raw = 0;
        if (addr > 0xFFFFu) break;
        role = areaMapFind(&self->m_maps[area], (uint16_t)addr);
        if (role == XMODBUSLINK_ROLE_NONE) continue;
        if (!XModbusHub_value(self->m_hub, (XModbusRegisterType)typeInt,
                              (uint16_t)addr, &raw)) continue;
        XIoChannelGroup_feedRole(group, role, (int32_t)raw);
    }
}

/**
 * @brief Hub 写完成槽：按区取表查 role，转发 writeDone。
 * @param receiver 连接的接收者（即本链路）。
 * @param args 信号参数（int type, int virtualStart, int count, bool ok）。
 * @return 无。
 */
static void VXModbusLink_onHubWriteCompleted(XObject* receiver, XVarList* args)
{
    XModbusLink* self;
    XIoChannelGroup* group;
    int area;
    int vStart;
    int count;
    int typeInt;
    int i;
    bool okv;

    if (!receiver || !args) return;
    self = (XModbusLink*)receiver;
    if (!self->m_hub) return;
    group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    if (!group) return;

    XVarList_args_4(args, int, type, int, start, int, cnt, bool, ok);
    typeInt = type;
    vStart = start;
    count = cnt;
    okv = ok;

    switch (typeInt) {
    case (int)XModbusDiscreteInputs:  area = 0; break;
    case (int)XModbusCoils:           area = 1; break;
    case (int)XModbusHoldingRegisters: area = 2; break;
    case (int)XModbusInputRegisters:  area = 3; break;
    default: return;
    }

    if (vStart < 0 || count <= 0) return;

    for (i = 0; i < count; ++i) {
        uint32_t addr = (uint32_t)vStart + (uint32_t)i;
        uint16_t role;
        if (addr > 0xFFFFu) break;
        role = areaMapFind(&self->m_maps[area], (uint16_t)addr);
        if (role == XMODBUSLINK_ROLE_NONE) continue;
        XIoChannelGroup_completeWrite(group, role, okv);
    }
}

/* ==================== 虚槽实现 ==================== */

/**
 * @brief BindRole：登记 role → (区, 虚拟地址)。
 * @param self 链路。
 * @param role 语义槽号。
 * @param nativeAddr 虚拟地址。
 * @param aux 区下标（0..3，由调用方用 XModbusLink_areaOfType 算出）。
 * @return 成功 true。
 */
static bool VXModbusLink_bindRole(XModbusLink* self, uint16_t role,
                                  uint32_t nativeAddr, uint32_t aux)
{
    int area;
    if (!self) return false;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    if (nativeAddr > 0xFFFFu) return false;
    area = (int)aux;
    if (area < 0 || area >= XMODBUSLINK_AREA_COUNT) return false;
    if (!areaMapBind(&self->m_maps[area], (uint16_t)nativeAddr, role)) return false;
    /* 同步维护反向表：上层读写靠它 O(1) 定位地址，无需遍历密集表。 */
    self->m_roleArea[role] = (uint16_t)area;
    self->m_roleAddr[role] = (uint16_t)nativeAddr;
    return true;
}

/**
 * @brief ReadRole：读 Hub 缓存（本函数不发起传输）。
 * @param self 链路。
 * @param role 语义槽号。
 * @param outRaw 输出原生值。
 * @return 成功 true。
 * @note O(1)：经反向表直接取 (区, 地址)，无遍历。
 */
static bool VXModbusLink_readRole(XModbusLink* self, uint16_t role, int32_t* outRaw)
{
    uint16_t raw = 0;
    int area;
    uint16_t addr;
    if (!self || !outRaw || !self->m_hub) return false;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    area = (int)self->m_roleArea[role];
    if (area >= XMODBUSLINK_AREA_COUNT) return false;   /* 未绑定。 */
    addr = self->m_roleAddr[role];
    if (!XModbusHub_value(self->m_hub, XModbusLink_hubAreaType(area), addr, &raw))
        return false;
    *outRaw = (int32_t)raw;
    return true;
}

/**
 * @brief WriteRole：写 Hub（DO→Coils、AO→HR）。
 * @param self 链路。
 * @param role 语义槽号。
 * @param raw 原生值。
 * @return 受理成功 true。
 * @note O(1)：经反向表直接取 (区, 地址)，无遍历。
 */
static bool VXModbusLink_writeRole(XModbusLink* self, uint16_t role, int32_t raw)
{
    int area;
    uint16_t addr;
    if (!self || !self->m_hub) return false;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    area = (int)self->m_roleArea[role];
    if (area >= XMODBUSLINK_AREA_COUNT) return false;   /* 未绑定。 */
    if (area != 1 && area != 2) return false;           /* 只有 DO/AO 可写。 */
    addr = self->m_roleAddr[role];
    return XModbusHub_writeValue(self->m_hub, XModbusLink_hubAreaType(area),
                                 addr, (uint16_t)((uint32_t)raw & 0xFFFFu));
}

/** @brief IsAsync：Modbus 要等应答，恒 true。 */
static bool VXModbusLink_isAsync(const XModbusLink* self)
{
    (void)self;
    return true;
}

/**
 * @brief Start：建立 Hub 信号订阅（各一条）。
 * @param self 链路。
 * @return 成功 true（Hub 为空返回 false）。
 * @note 幂等：已订阅则直接返回成功，避免重复连接。
 */
static bool VXModbusLink_start(XModbusLink* self)
{
    if (!self || !self->m_hub) return false;
    if (self->m_valueConn) return true;
    self->m_valueConn =
        XObject_connect_1((XObject*)self->m_hub,
                          XSignal(XModbusHub_valueChanged_signal),
                          (XObject*)self, VXModbusLink_onHubValueChanged,
                          XConnectionType_Direct | XConnectionType_RawReceiver);
    self->m_doneConn =
        XObject_connect_1((XObject*)self->m_hub,
                          XSignal(XModbusHub_writeCompleted_signal),
                          (XObject*)self, VXModbusLink_onHubWriteCompleted,
                          XConnectionType_Direct | XConnectionType_RawReceiver);
    if (!self->m_valueConn || !self->m_doneConn) {
        /* 部分失败：把已建立的撤回，保持一致状态。 */
        if (self->m_valueConn) {
            XObject_disconnect_1((XObject*)self->m_hub,
                                 XSignal(XModbusHub_valueChanged_signal),
                                 (XObject*)self, VXModbusLink_onHubValueChanged);
            self->m_valueConn = NULL;
        }
        if (self->m_doneConn) {
            XObject_disconnect_1((XObject*)self->m_hub,
                                 XSignal(XModbusHub_writeCompleted_signal),
                                 (XObject*)self, VXModbusLink_onHubWriteCompleted);
            self->m_doneConn = NULL;
        }
        return false;
    }
    return true;
}

/** @brief Stop：断开订阅（不释放 Hub）。 */
static void VXModbusLink_stop(XModbusLink* self)
{
    if (!self) return;
    if (self->m_valueConn && self->m_hub) {
        XObject_disconnect_1((XObject*)self->m_hub,
                             XSignal(XModbusHub_valueChanged_signal),
                             (XObject*)self, VXModbusLink_onHubValueChanged);
        self->m_valueConn = NULL;
    }
    if (self->m_doneConn && self->m_hub) {
        XObject_disconnect_1((XObject*)self->m_hub,
                             XSignal(XModbusHub_writeCompleted_signal),
                             (XObject*)self, VXModbusLink_onHubWriteCompleted);
        self->m_doneConn = NULL;
    }
}

/**
 * @brief 析构：先断订阅，再释放密集表，最后走父类链。
 * @param self 链路。
 * @return 无。
 */
static void VXModbusLink_deinit(XModbusLink* self)
{
    int i;
    if (!self) return;
    VXModbusLink_stop(self);
    for (i = 0; i < XMODBUSLINK_AREA_COUNT; ++i) {
        if (self->m_maps[i].m_roleByAddr) {
            XMemory_free(self->m_maps[i].m_roleByAddr, XCLASS_DEFAULT_MEMORY_TYPE);
            self->m_maps[i].m_roleByAddr = NULL;
        }
        self->m_maps[i].m_base = 0;
        self->m_maps[i].m_span = 0;
    }
    if (self->m_roleArea) {
        XMemory_free(self->m_roleArea, XCLASS_DEFAULT_MEMORY_TYPE);
        self->m_roleArea = NULL;
    }
    if (self->m_roleAddr) {
        XMemory_free(self->m_roleAddr, XCLASS_DEFAULT_MEMORY_TYPE);
        self->m_roleAddr = NULL;
    }
    self->m_hub = NULL;
    XClass_Deinit_Parent(XIoLink, (XIoLink*)self);
}

/* ==================== 类初始化 / 生命周期 ==================== */

XVtable* XModbusLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusLink)
    XVTABLE_INHERIT_XCLASS(XIoLink);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, VXModbusLink_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, VXModbusLink_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, VXModbusLink_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, VXModbusLink_isAsync);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Start, VXModbusLink_start);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Stop, VXModbusLink_stop);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusLink_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XModbusLink);
    return XVTABLE_DEFAULT;
}

void XModbusLink_init(XModbusLink* link, XModbusHub* hub)
{
    size_t n = (size_t)XIOCHANNELGROUP_ROLE_CAP;
    if (!link) return;
    XIoLink_init(&link->m_base);
    XClassGetVtable(link) = XModbusLink_class_init();
    memset(link->m_maps, 0, sizeof(link->m_maps));
    link->m_hub = hub;
    link->m_valueConn = NULL;
    link->m_doneConn = NULL;
    /* 反向表：role → (区, 地址)。用 0xFFFF 填「未绑定」（区 0xFFFF 非法）。 */
    link->m_roleArea = (uint16_t*)XMemory_malloc(sizeof(uint16_t) * n,
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
    link->m_roleAddr = (uint16_t*)XMemory_malloc(sizeof(uint16_t) * n,
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
    if (link->m_roleArea) {
        size_t i;
        for (i = 0; i < n; ++i) link->m_roleArea[i] = (uint16_t)0xFFFFu;
    }
    if (link->m_roleAddr) memset(link->m_roleAddr, 0, sizeof(uint16_t) * n);
}

XModbusLink* XModbusLink_create(XModbusHub* hub)
{
    XModbusLink* link;
    if (!hub) return NULL;
    link = (XModbusLink*)XMemory_malloc(sizeof(XModbusLink),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!link) return NULL;
    XModbusLink_init(link, hub);
    Set_Class_Memory(link, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(link, true);
    return link;
}

/* ==================== 查询 ==================== */

bool XModbusLink_queryBinding(const XModbusLink* link, uint16_t role,
                              int* outArea, uint16_t* outAddr)
{
    int area;
    if (!link || !link->m_roleArea) return false;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    area = (int)link->m_roleArea[role];
    if (area >= XMODBUSLINK_AREA_COUNT) return false;     /* 未绑定。 */
    if (outArea) *outArea = area;
    if (outAddr) *outAddr = link->m_roleAddr[role];
    return true;
}

int XModbusLink_boundCount(const XModbusLink* link)
{
    int a;
    int n = 0;
    if (!link) return 0;
    for (a = 0; a < XMODBUSLINK_AREA_COUNT; ++a) {
        const XModbusLinkAreaMap* map = &link->m_maps[a];
        uint32_t off;
        if (!map->m_roleByAddr) continue;
        for (off = 0; off < map->m_span; ++off) {
            if (map->m_roleByAddr[off] != XMODBUSLINK_ROLE_NONE) ++n;
        }
    }
    return n;
}

int XModbusLink_subscriptionCount(const XModbusLink* link)
{
    int n = 0;
    if (!link) return 0;
    if (link->m_valueConn) ++n;
    if (link->m_doneConn) ++n;
    return n;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */
