/**
 * @file       xvfd_harness.h
 * @brief      变频器端到端测试/演示装配脚手架（头文件内联，仅 Test 域使用）。
 * @details    [2026-11-01 通道化重构] XVfd 设备层只认通道点；本脚手架把
 *             「品牌点规格表 → 虚拟地址段 → 通道点 → 通道组 → 设备绑定」
 *             这套装配流程固化成一个可复用小工具，供三套 VFD 测试与面板页
 *             共用（测试/演示专属，不进产品库）：
 *               1) 虚拟地址**按区 bump 分配**（HR/Coils/DI 各自独立计数）：
 *                  品牌地址只在段内做偏移，多站共总线不会撞虚拟地址；
 *               2) 规格表先按 (区, 地址) 排序再合并连续段——一次轮询帧读回
 *                  一整块寄存器（与真实 VFD 轮询形态一致）；
 *               3) 参数点用 AO 点承载（AO 的 ReadRaw 同样读 Hub 缓存，
 *                  写走 FC06），故一个点即可读可写。
 *             依赖方向：本脚手架可 include XModbus（测试域特权），XVfd 库本身
 *             不 include 任何 XModbus 头。
 */
#ifndef XVFD_HARNESS_H
#define XVFD_HARNESS_H

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XModbus_config.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

#include <stdlib.h>
#include "XModbusHub.h"
#include "XPrintf.h"
#include "XModbusMapping.h"
#include "XModbusClient.h"
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoAnalogPoint.h"
#include "XChannel/XModbusLink.h"
#include "XChannel/XChannelAssembly.h"
#include "XVfd/XVfdDevice.h"
#include "XVfd/XVfdBind.h"

/** @brief 单次装配可处理的规格条目上限。 */
#define XVFD_HARNESS_SPEC_CAP 64
/** @brief 单链路可挂从站数上限。 */
#define XVFD_HARNESS_STATION_CAP 8
/** @brief 单次装配可处理的通信组上限。 */
#define XVFD_HARNESS_GROUP_CAP 96
/** @brief 单次装配可处理的远端地址索引上限（点 + 参数去重）。 */
#define XVFD_HARNESS_REF_CAP 192
/** @brief 参数点占用的 role 段起点（语义 role 到 XVfdRole_Count 为止，参数从 128 起）。
 *  @note 角色上限 XIOCHANNELGROUP_ROLE_CAP=256，故参数可用 128 个槽位。 */
#define XVFD_HARNESS_PARAM_ROLE_BASE 128

/**
 * @brief 远端地址索引项（装配期去重：语义点与参数点同址时共用一个通道点）。
 */
typedef struct XvfdHarnessRef {
    uint16_t m_remote;               /**< 远端地址 */
    XIoPoint* m_point;               /**< 通道点（NULL=未建） */
} XvfdHarnessRef;

/**
 * @brief 单站的装配上下文（组段惰性建 + 同址点复用）。
 * @note 组段**按需**建：只有被语义点或已绑定参数用到的组才建映射段——本型号
 *       地址天然分散（P0~PE 各功能码组 + 特殊条目共 28 组），全量建段会让读帧
 *       数暴涨（读帧越多，总线占用越大、写应答越容易被读挤掉）。
 */
typedef struct XvfdHarnessStation {
    XIoChannelGroup* m_group;        /**< 本站独立通道组（role 在组内唯一） */
    XModbusLink* m_link;             /**< 本站独立链路（共用 Hub，但 role 表独立） */
    int m_devId;                     /**< 设备句柄（-1=未用） */
    int m_unitId;                    /**< 从站地址 */
    const XVfdBindTable* m_bind;     /**< 绑定表（借用；地址 + 组表 + 约束） */
    const XVfdCommGroup* m_groups;   /**< 通信组表（借用） */
    int m_groupCount;                /**< 组数 */
    uint16_t m_groupVbase[XVFD_HARNESS_GROUP_CAP]; /**< 已建组的虚拟起点 */
    uint8_t m_groupBuilt[XVFD_HARNESS_GROUP_CAP];  /**< 组是否已建段（0=未建） */
    XvfdHarnessRef m_refs[XVFD_HARNESS_REF_CAP];   /**< 远端地址 → 通道点 */
    int m_refCount;                  /**< 索引条目数 */
} XvfdHarnessStation;

/**
 * @brief 装配脚手架。
 */
typedef struct XvfdHarness {
    XModbusHub* m_hub;              /**< 聚合设备（拥有映射） */
    XIoChannelGroup* m_group;       /**< 通道组（托管点） */
    XModbusLink* m_link;            /**< 通道链路（Hub 的唯一订阅者；组持有） */
    XModbusMapping* m_map;          /**< 映射（setMapping 后归 Hub 所有） */
    int m_devIds[XVFD_HARNESS_STATION_CAP]; /**< 已注入设备句柄 */
    int m_devCount;                 /**< 设备数 */
    bool m_identity;                /**< true=虚拟地址=品牌地址（单站直挂） */
    uint16_t m_bumpHr;              /**< HR 区虚拟地址分配游标 */
    uint16_t m_bumpCoil;            /**< Coils 区虚拟地址分配游标 */
    uint16_t m_bumpDi;              /**< DI 区虚拟地址分配游标 */
    int m_pointCount;               /**< 已建点数 */
    XvfdHarnessStation m_stations[XVFD_HARNESS_STATION_CAP]; /**< 各站装配上下文 */
} XvfdHarness;

/**
 * @brief 规格排序索引项。
 */
typedef struct XvfdHarnessSpec {
    uint8_t m_area;                 /**< XModbusRegisterType（按区排序） */
    uint16_t m_addr;                /**< 品牌原生地址 */
    int m_idx;                      /**< 原规格下标 */
} XvfdHarnessSpec;

/**
 * @brief XModbusRegisterType(1..4) → XModbusLink 区下标(0..3)。
 * @param regType XModbusRegisterType 值。
 * @return 区下标；未知返回 2（HR）。
 * @note [2026-11-02] 两套编号不同：寄存器类型从 1 起（DI=1/Coils=2/IR=3/HR=4），
 *       链路区下标从 0 起（DI=0/DO=1/AI=2/AO=3）。直接传会越界。
 */
static int xvfdHarnessLinkArea(int regType)
{
    switch (regType) {
    case (int)XModbusDiscreteInputs:  return 0;
    case (int)XModbusCoils:           return 1;
    case (int)XModbusHoldingRegisters: return 2;
    case (int)XModbusInputRegisters:  return 3;
    default: return 2;
    }
}

/**
 * @brief 点类型 → 寄存器区。
 * @param type XIoPointType。
 * @return 对应 XModbusRegisterType。
 */
static XModbusRegisterType xvfdHarnessAreaOf(uint8_t type)
{
    switch (type) {
    case (uint8_t)XIoPoint_DigitalInput: return XModbusDiscreteInputs;
    case (uint8_t)XIoPoint_DigitalOutput: return XModbusCoils;
    default: return XModbusHoldingRegisters;
    }
}

/**
 * @brief 按区分配虚拟地址（bump）。
 * @param h 脚手架。
 * @param area 寄存器区。
 * @param count 段长。
 * @return 段起始虚拟地址。
 */
static uint16_t xvfdHarnessBump(XvfdHarness* h, XModbusRegisterType area, uint16_t count)
{
    uint16_t base;
    switch (area) {
    case XModbusDiscreteInputs:
        base = h->m_bumpDi;
        h->m_bumpDi = (uint16_t)(h->m_bumpDi + count);
        break;
    case XModbusCoils:
        base = h->m_bumpCoil;
        h->m_bumpCoil = (uint16_t)(h->m_bumpCoil + count);
        break;
    default:
        base = h->m_bumpHr;
        h->m_bumpHr = (uint16_t)(h->m_bumpHr + count);
        break;
    }
    return base;
}

/**
 * @brief 规格排序比较（区升序，再地址升序）。
 * @param a 左项。
 * @param b 右项。
 * @return 比较结果。
 */
static int xvfdHarnessSpecCmp(const void* a, const void* b)
{
    const XvfdHarnessSpec* x = (const XvfdHarnessSpec*)a;
    const XvfdHarnessSpec* y = (const XvfdHarnessSpec*)b;
    if (x->m_area != y->m_area) return (int)x->m_area - (int)y->m_area;
    return (int)x->m_addr - (int)y->m_addr;
}

/**
 * @brief 按规格建一个通道点（组托管释放）。
 * @param h 脚手架。
 * @param spec 点规格（类型/点名）。
 * @param vaddr 虚拟地址。
 * @param area 区下标（XModbusLink_areaOfType 口径）。
 * @return 新点；失败返回 NULL。
 * @note [2026-11-02 通道层重写] 点退化为纯数据容器，链路登记改由
 *       XModbusLink_bindRole 完成，故这里只建点、不碰 Hub。
 */
/* 前向声明：装配函数在前、站上下文写在后（逼近使用点）。 */
static XvfdHarnessStation* xvfdHarnessStationOf(XvfdHarness* h, int devId, int unitId,
                                                const XVfdCommGroup* groups, int groupCount);

static XIoPoint* xvfdHarnessMakePoint(XvfdHarness* h, const XVfdPointSpec* spec,
                                      uint16_t vaddr, int area)
{
    XIoPoint* pt;
    (void)h;
    if (!spec) return NULL;
    /* [2026-11-03 拆分] 按规格类型选点类：AI/AO 用 XIoAnalogPoint（承载标度），
     * DI/DO 用基类（瘦身无标度载荷）；对外统一以基类指针返回。 */
    if (spec->m_type == (uint8_t)XIoPoint_AnalogInput ||
        spec->m_type == (uint8_t)XIoPoint_AnalogOutput) {
        XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!apt) return NULL;
        pt = &apt->m_base;
    } else {
        pt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pt) return NULL;
    }
    XIoPoint_setRole(pt, spec->m_role);
    XIoPoint_setType(pt, (XIoPointType)spec->m_type);
    XIoPoint_setTag(pt, spec->m_tag);
    XIoPoint_setSigned(pt, spec->m_signed != 0);
    if (spec->m_type == (uint8_t)XIoPoint_AnalogInput) {
        XIoPoint_setUseHolding(pt, area == 2);   /* AI 落 HR 区（本工程口径）。 */
    }
    /* [2026-11-02 标度归属] 语义点不在通道层标度：XVfdDevice 的 DecodePoint 需要原生量
     * 自己做 double 换算（如转矩 69/10 = 6.9），而 XIoPoint_toEngineering 返回 int32_t 会截断成 6。
     * 故这里一律 div=1（不标度），标度仅用于非语义点（参数点等）。 */
    XIoAnalogPoint_setScale((XIoAnalogPoint*)pt, 1, 1, 0, 0);
    return pt;
}

/**
 * @brief 初始化脚手架（Hub + 映射 + 通道组）。
 * @param h 脚手架；不能为 NULL。
 * @return 成功返回 true。
 */
static bool xvfdHarnessInit(XvfdHarness* h)
{
    int i;
    if (!h) return false;
    h->m_hub = XModbusHub_create();
    h->m_map = XModbusMapping_create();
    h->m_group = XIoChannelGroup_create();
    h->m_link = XModbusLink_create(h->m_hub);
    if (h->m_group && h->m_link) {
        XIoChannelGroup_setLink(h->m_group, (XIoLink*)h->m_link);
    }
    h->m_devCount = 0;
    h->m_identity = false;
    h->m_bumpHr = 0;
    h->m_bumpCoil = 0;
    h->m_bumpDi = 0;
    h->m_pointCount = 0;
    for (i = 0; i < XVFD_HARNESS_STATION_CAP; ++i) {
        h->m_stations[i].m_devId = -1;   /* 站上下文槽位空闲标记 */
    }
    return (h->m_hub && h->m_map && h->m_group && h->m_link);
}

/**
 * @brief 注入一条总线上的从站（同一 client 可挂多站号）。
 * @param h 脚手架。
 * @param client Modbus 客户端（借用）。
 * @param unitId 从站地址。
 * @param name 设备名（借用）。
 * @return 设备句柄（>0）；失败返回 -1。
 */
/**
 * @brief 设为恒等映射（虚拟地址=品牌地址；单站直挂用）。
 * @param h 脚手架。
 * @return 无。
 * @note 多站共 Hub 时不可用（品牌地址会重叠）；多站走 bump 偏移方案。
 */
static void xvfdHarnessSetIdentity(XvfdHarness* h)
{
    if (h) h->m_identity = true;
}

static int xvfdHarnessAddStation(XvfdHarness* h, XModbusClient* client, int unitId,
                                 const char* name)
{
    int devId;
    if (!h || !client || h->m_devCount >= XVFD_HARNESS_STATION_CAP) return -1;
    devId = XModbusHub_addDevice_2(h->m_hub, client, name);
    if (devId <= 0) return -1;
    h->m_devIds[h->m_devCount++] = devId;
    (void)unitId;
    return devId;
}

/**
 * @brief 按品牌点规格表装配一个设备的全部语义槽点。
 * @param h 脚手架。
 * @param dev 设备（须已 attach 到本脚手架通道组）。
 * @param devId 设备句柄（addStation 返回）。
 * @param unitId 从站地址。
 * @param specs 品牌点规格表（静态 const）。
 * @param n 条目数。
 * @return 全部建成并绑定返回 true。
 */
static bool xvfdHarnessMount(XvfdHarness* h, XVfdDevice* dev, int devId, int unitId,
                             const XVfdPointSpec* specs, int n, const XVfdBindTable* bind)
{
    XvfdHarnessSpec idx[XVFD_HARNESS_SPEC_CAP];
    XvfdHarnessStation* st;
    int i, j;
    if (!h || !dev || !specs || !bind || n <= 0 || n > XVFD_HARNESS_SPEC_CAP) return false;
    st = xvfdHarnessStationOf(h, devId, unitId, NULL, 0);
    if (!st || !st->m_group || !st->m_link) return false;
    st->m_bind = bind;
    /* [2026-11-02 分层收口] 点规格只存语义，地址从绑定表按 role 取。 */
    for (i = 0; i < n; ++i) {
        XVfdPointBinding pb;
        if (!XVfdBind_findPoint(bind, specs[i].m_role, &pb)) return false;
        idx[i].m_area = (uint8_t)xvfdHarnessAreaOf(specs[i].m_type);
        idx[i].m_addr = pb.m_address;
        idx[i].m_idx = i;
    }
    qsort(idx, (size_t)n, sizeof(idx[0]), xvfdHarnessSpecCmp);
    i = 0;
    while (i < n) {
        int runEnd = i;
        XModbusRegisterType area = (XModbusRegisterType)idx[i].m_area;
        uint16_t runStart = idx[i].m_addr;
        uint16_t count;
        uint16_t vbase;
        while ((runEnd + 1) < n && idx[runEnd + 1].m_area == idx[i].m_area &&
               ((uint32_t)idx[runEnd].m_addr + 1u) == (uint32_t)idx[runEnd + 1].m_addr) {
            ++runEnd;
        }
        count = (uint16_t)(idx[runEnd].m_addr - runStart + 1);
        vbase = h->m_identity ? runStart : xvfdHarnessBump(h, area, count);
        if (!XModbusMapping_addRange(h->m_map, area, vbase, count, devId, unitId,
                                     runStart, 0)) {
            return false;
        }
        for (j = i; j <= runEnd; ++j) {
            const XVfdPointSpec* sp = &specs[idx[j].m_idx];
            XVfdPointBinding pb;
            uint16_t vaddr;
            (void)XVfdBind_findPoint(bind, sp->m_role, &pb);
            vaddr = (uint16_t)(vbase + (pb.m_address - runStart));
            XIoPoint* pt = xvfdHarnessMakePoint(h, sp, vaddr,
                                                xvfdHarnessLinkArea((int)idx[j].m_area));
            if (!pt) return false;
            if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
                return false;
            }
            /* 链路登记：role → 虚拟地址（区由点类型决定）。 */
            if (!XIoLink_bindRole_base((XIoLink*)st->m_link, sp->m_role, vaddr,
                                       (uint32_t)xvfdHarnessLinkArea((int)idx[j].m_area))) {
                return false;
            }
            /* 设备绑定：把点挂到语义槽上（此时 div/signed/caps 一并内联进槽）。 */
            if (!XVfdDevice_bindPoint(dev, sp, pt)) return false;
            ++h->m_pointCount;
        }
        i = runEnd + 1;
    }
}

/**
 * @brief 装配一个参数点（AO 承载：读走 Hub 缓存、写走 FC06）。
 * @param h 脚手架。
 * @param dev 设备（须已 attach）。
 * @param devId 设备句柄。
 * @param unitId 从站地址。
 * @param desc 参数描述（取地址与名称）。
 * @param paramId 参数描述表下标。
 * @return 成功返回 true。
 */
static bool xvfdHarnessMountParam(XvfdHarness* h, XVfdDevice* dev, int devId, int unitId,
                                  const XVfdParamDesc* desc, uint16_t paramId,
                                  const XVfdBindTable* bind)
{
    uint16_t vbase;
    XIoPoint* pt;
    XvfdHarnessStation* st;
    if (!h || !dev || !desc) return false;
    st = xvfdHarnessStationOf(h, devId, unitId, NULL, 0);
    if (!st || !st->m_group || !st->m_link) return false;
    {
        XVfdParamBinding pb;
        if (!bind || !XVfdBind_findParam(bind, paramId, &pb)) return false;
        vbase = xvfdHarnessBump(h, XModbusHoldingRegisters, 1);
        if (!XModbusMapping_addRange(h->m_map, XModbusHoldingRegisters, vbase, 1,
                                     devId, unitId, pb.m_addr, 0)) return false;
    }
    pt = NULL;
    {
        /* 参数点用 AO 承载（可读可写），AO 须为 XIoAnalogPoint（承载标度）。 */
        XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!apt) return false;
        pt = &apt->m_base;
    }
    /* 参数点的 role 用「参数槽基址 + paramId」，避免与语义 role 冲突。
     * 参数 role 段起点由 XVFD_HARNESS_PARAM_ROLE_BASE 给定（见文件头常量）。 */
    XIoPoint_setRole(pt, (uint16_t)(XVFD_HARNESS_PARAM_ROLE_BASE + paramId));
    XIoPoint_setType(pt, XIoPoint_AnalogOutput);
    XIoPoint_setTag(pt, desc->m_name);
    XIoAnalogPoint_setScale((XIoAnalogPoint*)pt, 1, 1, 0, 0);
    if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
        XClassDelete((XClass*)pt);
        return false;
    }
    if (!XIoLink_bindRole_base((XIoLink*)st->m_link,
                               (uint16_t)(XVFD_HARNESS_PARAM_ROLE_BASE + paramId),
                               vbase, 2)) {
        return false;
    }
    if (!XVfdDevice_bindParamPoint(dev, paramId, pt)) return false;
    ++h->m_pointCount;
    return true;
}

/**
 * @brief 找通信组（含指定远端地址的组）。
 * @param groups 组表（静态 const）。
 * @param n 组数。
 * @param remote 远端地址。
 * @return 组下标；未命中返回 -1。
 */
static int xvfdHarnessFindGroup(const XVfdCommGroup* groups, int n, uint16_t remote)
{
    int i;
    if (!groups || n <= 0) return -1;
    for (i = 0; i < n; ++i) {
        const XVfdCommGroup* g = &groups[i];
        if (!g) continue;
        if (remote >= g->m_remoteStart &&
            (uint32_t)remote < (uint32_t)g->m_remoteStart + g->m_count) return i;
    }
    return -1;
}

/**
 * @brief 取（或建）某站的装配上下文。
 * @param h 脚手架。
 * @param devId 设备句柄。
 * @param unitId 从站地址。
 * @param groups 通信组表（借用）；可为 NULL（沿用已有）。
 * @param groupCount 组数。
 * @return 站上下文；槽位用尽返回 NULL。
 */

static XvfdHarnessStation* xvfdHarnessStationOf(XvfdHarness* h, int devId, int unitId,
                                                 const XVfdCommGroup* groups, int groupCount)
{
    int i;
    if (!h) return NULL;
    for (i = 0; i < XVFD_HARNESS_STATION_CAP; ++i) {
        XvfdHarnessStation* st = &h->m_stations[i];
        if (st->m_devId == devId && st->m_unitId == unitId) {
            if (groups) { st->m_groups = groups; st->m_groupCount = groupCount; }
            return st;
        }
    }
    for (i = 0; i < XVFD_HARNESS_STATION_CAP; ++i) {
        XvfdHarnessStation* st = &h->m_stations[i];
        if (st->m_devId != -1) continue;
        memset(st, 0, sizeof(*st));
        st->m_devId = devId;
        st->m_unitId = unitId;
        st->m_groups = groups;
        st->m_groupCount = groupCount;
        /* [2026-11-02 多站] 每站独立一个通道组 + 一个链路：
         * role 是组内下标，若多站共用一组则同名 role 必然冲突
         * （两台变频器都有 StatusWord 等同名语义槽）。链路可共用 Hub，
         * 但 role→地址反查表必须每站一份。 */
        st->m_group = XIoChannelGroup_create();
        st->m_link = XModbusLink_create(h->m_hub);
        if (st->m_group && st->m_link) {
            XIoChannelGroup_setLink(st->m_group, (XIoLink*)st->m_link);
        }
        return st;
    }
    return NULL;
}

/**
 * @brief 取某站的通道组（设备 attach 用；未建则建立）。
 * @param h 脚手架。
 * @param devId 设备句柄。
 * @param unitId 从站地址。
 * @return 该站的通道组；失败返回 NULL。
 * @note [2026-11-02 多站] 每站独立一个组：role 是组内下标，
 *       多站共用一组会因同名语义槽（StatusWord 等）直接冲突。
 */
static XIoChannelGroup* xvfdHarnessStationGroup(XvfdHarness* h, int devId, int unitId)
{
    XvfdHarnessStation* st = xvfdHarnessStationOf(h, devId, unitId, NULL, 0);
    return st ? st->m_group : NULL;
}


/**
 * @brief 惰性建组段（幂等）：按需为某个通信组建映射段。
 * @param h 脚手架。
 * @param st 站上下文。
 * @param gi 组下标。
 * @param outVbase 输出组的虚拟起点。
 * @return 成功返回 true。
 * @note 组段**按需**建：只有被语义点或已绑定参数用到的组才建映射段——本型号
 *       地址天然分散（P0~PE 各功能码组 + 特殊条目共 28 组），全量建段会让读帧
 *       数暴涨（读帧越多，总线占用越大、写应答越容易被读挤掉）。
 */
static bool xvfdHarnessEnsureGroup(XvfdHarness* h, XvfdHarnessStation* st, int gi,
                                    uint16_t* outVbase)
{
    XModbusRegisterType area;
    uint16_t vbase;
    if (!h || !st || !st->m_groups || gi < 0 || gi >= st->m_groupCount) return false;
    if (gi >= XVFD_HARNESS_GROUP_CAP) return false;
    if (st->m_groupBuilt[gi]) { if (outVbase) *outVbase = st->m_groupVbase[gi]; return true; }
    area = xvfdHarnessAreaOf(st->m_groups[gi].m_area);
    vbase = h->m_identity ? st->m_groups[gi].m_remoteStart
                          : xvfdHarnessBump(h, area, st->m_groups[gi].m_count);
    /* 只写组：登记为只写段（写路径照常，但不进读组 ⇒ 不参与轮询）。
     * 厂商从站的命令字/密码区读了只会回错误帧。 */
    if (!XModbusMapping_addRangeEx(h->m_map, area, vbase, st->m_groups[gi].m_count,
                                   st->m_devId, st->m_unitId,
                                   st->m_groups[gi].m_remoteStart, 0,
                                   (uint8_t)st->m_groups[gi].m_flags)) {
        XPrintf("[xvfd-harness] addRange(group=%d remote=0x%04X) 失败\n",
                gi, st->m_groups[gi].m_remoteStart);
        return false;
    }
    st->m_groupVbase[gi] = vbase;
    st->m_groupBuilt[gi] = 1;
    if (outVbase) *outVbase = vbase;
    return true;
}

/**
 * @brief 查同址已建点（语义点与参数点复用）。
 * @param st 站上下文。
 * @param remote 远端地址。
 * @return 通道点；未建返回 NULL。
 */
static XIoPoint* xvfdHarnessFindRef(const XvfdHarnessStation* st, uint16_t remote)
{
    int i;
    if (!st) return NULL;
    for (i = 0; i < st->m_refCount; ++i)
        if (st->m_refs[i].m_remote == remote) return st->m_refs[i].m_point;
    return NULL;
}

/**
 * @brief 登记远端地址 → 通道点。
 * @param st 站上下文。
 * @param remote 远端地址。
 * @param pt 通道点。
 * @return 成功返回 true（索引满返回 false）。
 */
static bool xvfdHarnessSetRef(XvfdHarnessStation* st, uint16_t remote, XIoPoint* pt)
{
    if (!st) return false;
    if (st->m_refCount >= XVFD_HARNESS_REF_CAP) return false;
    st->m_refs[st->m_refCount].m_remote = remote;
    st->m_refs[st->m_refCount].m_point = pt;
    ++st->m_refCount;
    return true;
}

/**
 * @brief 按通信组装配一个品牌的语义点 + 从站约束（组段按需建）。
 * @param h 脚手架。
 * @param dev 设备（须已 attach 到本脚手架通道组）。
 * @param devId 设备句柄（addStation 返回）。
 * @param unitId 从站地址。
 * @param groups 品牌通信组表（静态 const）。
 * @param groupCount 组数。
 * @param specs 品牌点规格表（静态 const）。
 * @param specCount 点数。
 * @param protocol 品牌从站协议约束（静态 const）；可为 NULL（不登记约束）。
 * @return 全部建成并绑定返回 true；任一步失败返回 false。
 * @details 装配顺序：
 *          1) 语义点：按远端地址落在哪个组 → **惰性建该组的映射段**（每组一段，
 *             不是「按地址相邻」合并；组边界即从站的「功能码组」边界，读帧
 *             规划据此不跨组）→ 虚拟地址 = 组虚拟起点 + 组内偏移；
 *          2) 登记从站约束（单帧读上限 / 组边界硬约束 / 只允许单寄存器写）。
 * @note **参数点不在此装配**：参数是低频按需操作，全量映射会把读帧数撑爆
 *       （本型号 28 组 → 每圈 29 帧）。参数点用 xvfdHarnessMountParamBrand
 *       按需绑定，届时才惰性建其所属组段。
 * @note 约束作用域是**从站**：同一 RTU 链路上的其它从站各自独立登记。
 */
static bool xvfdHarnessMountBrand(XvfdHarness* h, XVfdDevice* dev, int devId, int unitId,
                                   const XVfdCommGroup* groups, int groupCount,
                                   const XVfdPointSpec* specs, int specCount,
                                   const XVfdUnitProtocol* protocol,
                                   const XVfdBindTable* bind)
{
    XvfdHarnessStation* st;
    int i;
    if (!h || !dev || !groups || groupCount <= 0 ||
        groupCount > XVFD_HARNESS_GROUP_CAP) return false;
    if (!specs || specCount <= 0) return false;
    st = xvfdHarnessStationOf(h, devId, unitId, groups, groupCount);
    if (!st) return false;
    st->m_bind = bind;
    for (i = 0; i < specCount; ++i) {
        XVfdPointBinding pb;
        if (!XVfdBind_findPoint(bind, specs[i].m_role, &pb)) return false;
        {
        int gi = xvfdHarnessFindGroup(groups, groupCount, pb.m_address);
        uint16_t vbase = 0;
        uint16_t vaddr;
        XIoPoint* pt;
        if (gi < 0) {
            XPrintf("[xvfd-harness] 点 %s 的远端地址 0x%04X 不在任何通信组内\n",
                    specs[i].m_tag ? specs[i].m_tag : "?", pb.m_address);
            return false;
        }
        if (!xvfdHarnessEnsureGroup(h, st, gi, &vbase)) {
            return false;
        }
        vaddr = (uint16_t)(vbase + (pb.m_address - groups[gi].m_remoteStart));
        pt = xvfdHarnessFindRef(st, pb.m_address);
        if (!pt) {
            pt = xvfdHarnessMakePoint(h, &specs[i], vaddr,
                                      XModbusLink_areaOfType((int)specs[i].m_type,
                                                             true));
            if (!pt) return false;
            if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) return false;
            if (!XVfdDevice_bindPoint(dev, &specs[i], pt)) {
                /* 绑定失败时把点从组里摘掉并释放，避免 role 位被脏点占住。 */
                (void)XIoChannelGroup_removeRole(st->m_group, specs[i].m_role);
                return false;
            }
            /* 链路登记 role → 虚拟地址（区由点类型 + AI 区选决定）。 */
            if (!XIoLink_bindRole_base((XIoLink*)st->m_link, specs[i].m_role, vaddr,
                                       (uint32_t)XModbusLink_areaOfType(
                                           (int)specs[i].m_type, true))) {
                return false;
            }
            if (!xvfdHarnessSetRef(st, pb.m_address, pt)) return false;
            ++h->m_pointCount;
        }
        }
    }
    /* 从站协议约束（品牌层中立表达 → 传输层配置，逐字段翻译不靠布局巧合）。 */
    if (protocol) {
        XModbusHubUnitLimits lim;
        lim.m_maxReadRegisters = protocol->m_maxReadRegisters;
        lim.m_rangeBarrier = protocol->m_rangeBarrier;
        lim.m_singleWriteOnly = protocol->m_singleWriteOnly;
        if (!XModbusHub_setUnitLimits(h->m_hub, devId, unitId, &lim)) {
            XPrintf("[xvfd-harness] setUnitLimits(dev=%d unit=%d) 失败\n", devId, unitId);
            return false;
        }
    }
    return true;
}

/**
 * @brief 按需装配一个参数点（AO 承载：读走 Hub 缓存、写走 FC06）。
 * @param h 脚手架。
 * @param dev 设备（须已 attach）。
 * @param devId 设备句柄。
 * @param unitId 从站地址。
 * @param groups 品牌通信组表（静态 const）。
 * @param groupCount 组数。
 * @param desc 参数描述（取地址与名称）。
 * @param paramId 参数描述表下标。
 * @return 成功返回 true。
 * @details 参数地址若落在已建组内 → 复用该组的映射段与虚拟地址空间；否则
 *          **惰性建该组**（参数是低频按需操作，不预建全部参数组——那会让
 *          读帧数暴涨）。
 */
static bool xvfdHarnessMountParamBrand(XvfdHarness* h, XVfdDevice* dev, int devId,
                                       int unitId, const XVfdCommGroup* groups,
                                       int groupCount, const XVfdParamDesc* desc,
                                       uint16_t paramId)
{
    XvfdHarnessStation* st;
    int gi;
    uint16_t vbase = 0;
    uint16_t vaddr;
    uint16_t paramAddr = 0;
    XIoPoint* pt;
    if (!h || !dev || !desc || !groups) return false;
    st = xvfdHarnessStationOf(h, devId, unitId, groups, groupCount);
    if (!st) return false;
    {
        XVfdParamBinding pb;
        if (!XVfdBind_findParam(st->m_bind, paramId, &pb)) return false;
        gi = xvfdHarnessFindGroup(groups, groupCount, pb.m_addr);
        if (gi < 0) {
            XPrintf("[xvfd-harness] param %s addr 0x%04X not in any comm group",
                    desc->m_name ? desc->m_name : "?", pb.m_addr);
            return false;
        }
        paramAddr = pb.m_addr;
    }
    if (!xvfdHarnessEnsureGroup(h, st, gi, &vbase)) {
        return false;
    }
    vaddr = (uint16_t)(vbase + (paramAddr - groups[gi].m_remoteStart));
    pt = xvfdHarnessFindRef(st, paramAddr);
    if (!pt) {
        /* AO 参数点：必须为 XIoAnalogPoint（承载标度）。 */
        XIoAnalogPoint* np = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        uint16_t prole = (uint16_t)(XVFD_HARNESS_PARAM_ROLE_BASE + paramId);
        if (!np) return false;
        pt = &np->m_base;
        XIoPoint_setRole(pt, prole);
        XIoPoint_setType(pt, XIoPoint_AnalogOutput);
        XIoPoint_setTag(pt, desc->m_name);
        XIoAnalogPoint_setScale(np, 1, 1, 0, 0);
        if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
            XClassDelete((XClass*)pt);
            return false;
        }
        if (!XIoLink_bindRole_base((XIoLink*)st->m_link, prole, vaddr, 2)) {
            return false;
        }
        if (!xvfdHarnessSetRef(st, paramAddr, pt)) return false;
        ++h->m_pointCount;
    }
    return XVfdDevice_bindParamPoint(dev, paramId, pt);
}

/**
 * @brief 提交映射并启动轮询（装配完成后调用一次）。
 * @param h 脚手架。
 * @param pollMs 每设备轮询周期（毫秒；0 按 50）。
 * @return 无。
 * @note 映射所有权移交 Hub；Hub 析构时一并释放。
 */
static void xvfdHarnessStart(XvfdHarness* h, unsigned pollMs)
{
    int i;
    if (!h || !h->m_hub || !h->m_map) return;
    XModbusHub_setMapping(h->m_hub, h->m_map);
    h->m_map = NULL; /* 所有权已移交 */
    /* 命令字同址连写（如 CiA402 序列 0x0006→0x0007→0x000F）不允许被
     * 写合并窗口折叠，关闭合并（受理即下发）。 */
    XModbusHub_setWriteCoalesceMs(h->m_hub, 0);
    /* Hub 缺省只轮询输入区（DI|IR）；VFD 全部用址在 HR，放开四区。 */
    XModbusHub_setPollAreas(h->m_hub,
                            (1u << XModbusDiscreteInputs) |
                            (1u << XModbusCoils) |
                            (1u << XModbusInputRegisters) |
                            (1u << XModbusHoldingRegisters));
    XModbusHub_setPollIntervalDefault(h->m_hub, pollMs ? pollMs : 50);
    for (i = 0; i < h->m_devCount; ++i) {
        if (!XModbusHub_setPollInterval(h->m_hub, h->m_devIds[i], pollMs ? pollMs : 50)) {
            XPrintf("[xvfd-harness] setPollInterval(dev=%d) 失败\n", h->m_devIds[i]);
        }
    }
    XModbusHub_setUnitBackoff(h->m_hub, 1000, 1000); /* 测试免退避干扰 */
    /* [2026-11-02] 每站链路必须 Start：它负责订阅 Hub 的
     * valueChanged / writeCompleted（点类已退化为纯数据，订阅由链路承担）。 */
    for (i = 0; i < XVFD_HARNESS_STATION_CAP; ++i) {
        if (h->m_stations[i].m_link) {
            if (!XIoLink_start_base((XIoLink*)h->m_stations[i].m_link)) {
                XPrintf("[xvfd-harness] 站 %d 链路启动失败\n", i);
            }
        }
    }
    XModbusHub_connectAll(h->m_hub);
    XModbusHub_startPolling(h->m_hub);
}

/**
 * @brief 拆除脚手架（先删组：autoDelete 点随之析构并退订 Hub，再收 Hub）。
 * @param h 脚手架。
 * @return 无。
 */
static void xvfdHarnessDestroy(XvfdHarness* h)
{
    int i;
    if (!h) return;
    /* [2026-11-02 多站] 每站独立的组与链路先回收（组持有 link 的借用指针，
     * 故先摘 link 再删组，最后删 link）。 */
    for (i = 0; i < XVFD_HARNESS_STATION_CAP; ++i) {
        XvfdHarnessStation* st = &h->m_stations[i];
        if (st->m_group) {
            XIoChannelGroup_setLink(st->m_group, NULL);
            XClassDelete((XClass*)st->m_group);
            st->m_group = NULL;
        }
        if (st->m_link) {
            XClassDelete((XClass*)st->m_link);
            st->m_link = NULL;
        }
    }
    if (h->m_group) {
        /* 组持有 link 的借用指针：先把组摘掉 link，再删组，再删 link。 */
        XIoChannelGroup_setLink(h->m_group, NULL);
        XClassDelete((XClass*)h->m_group);
        h->m_group = NULL;
    }
    if (h->m_link) {
        XClassDelete((XClass*)h->m_link);
        h->m_link = NULL;
    }
    if (h->m_hub) {
        XModbusHub_stopPolling(h->m_hub);
        XObject_deleteLater((XObject*)h->m_hub);
        h->m_hub = NULL;
    }
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */
#endif /* XVFD_HARNESS_H */
