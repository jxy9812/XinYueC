
/**
 * @file       HsfServerIoVfd.c
 * @brief      服务端变频器站实现（品牌装配逻辑自 HsfServerIo.c 平移）。
 * @details    装配顺序（对齐 Test/XProtocolTest/xfdv_m512_rtu_test.c 真机范本）：
 *               1) 通信组惰性建段（映射表由编排层传入，本模块只 addRange）；
 *               2) 按点规格建通道点并登记进组；
 *               3) 品牌绑定表把 role → (区, 虚拟地址) 绑到链路；
 *               4) XVfdDevice_bindPoint 把语义规格与点配对；
 *               5) 品牌从站约束（单帧读上限/组边界/只 FC06）翻译成
 *                  XModbusHubUnitLimits 交给 Hub。
 *             [虚拟地址] 恒等映射：虚拟地址 = 远端地址。每路总线各有一个
 *             Hub，本路变频器独占该 Hub 的品牌地址段（0x1000/0x2xxx/0x3xxx/
 *             0x7xxx/0x8xxx/0xA0xx/0xF0xx~0xFFxx），同总线的其它从站用
 *             独立 vbase 避开。
 */
#include "HsfServerIoVfd.h"

#include "HsfServerBus.h"
#include "HsfServerConfig.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusLink.h"
#include "XIoLink.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "XVfdDevice.h"
#include "XVfdBind.h"
#include "XVfdM512.h"
#include "XPrintf.h"
#include <string.h>

/** @brief 通信组表容量上限（与 XVfdM512 的组数对齐并留余量）。 */
#define HSF_VFD_GROUP_CAP   48

/* ==================== 内部辅助 ==================== */

/**
 * @brief 在通信组表里找覆盖某远端地址的组。
 * @return 组下标；未覆盖返回 -1。
 */
static int vfdFindGroup(const XVfdCommGroup* groups, int count, uint16_t addr)
{
    int i;
    for (i = 0; i < count; ++i) {
        if (addr >= groups[i].m_remoteStart &&
            addr < (uint16_t)(groups[i].m_remoteStart + groups[i].m_count)) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 惰性登记一个通信组映射段（恒等映射：vbase=远端起始地址）。
 * @param st 目标站；g 通信组；gi 组下标。
 * @return 登记成功返回 true。
 */
static bool vfdEnsureGroup(HsfServerIoVfd* st, const XVfdCommGroup* g, int gi)
{
    XModbusHub* hub = HsfServerBus_hub(st->m_bus);
    XModbusRegisterType area;
    int devId = HsfServerBus_deviceId(st->m_bus);
    uint16_t base;

    if (gi < 0 || gi >= HSF_VFD_GROUP_CAP) return false;
    if (st->m_vbaseBuilt[gi]) return true;
    /* ⚠ 区换算必须按 XIoPoint 类型（g->m_area 存的是点类型，不是区号）：
     *   XIoPoint_DigitalInput=1 → DiscreteInputs、DigitalOutput=2 → Coils、
     *   AnalogInput/AnalogOutput → HoldingRegisters（本机 IR 不用于遥测）。
     *   原实现按 0/1/default 判定，把 DI 错成 Coils（2026-11-04 实锅）。 */
    switch (g->m_area) {
    case XIoPoint_DigitalInput:  area = XModbusDiscreteInputs; break;
    case XIoPoint_DigitalOutput: area = XModbusCoils; break;
    default:                     area = XModbusHoldingRegisters; break;
    }
    base = g->m_remoteStart;        /* 恒等映射：虚拟地址 = 远端地址。 */
    if (!XModbusMapping_addRangeEx(st->m_map, area, base, g->m_count,
                                   devId, (int)st->m_unitId,
                                   g->m_remoteStart, 0,
                                   (uint8_t)g->m_flags)) {
        XPrintf("HsfServerIoVfd: addRange(v=0x%04X remote=0x%04X count=%u "
                "area=%d) 失败\n",
                (unsigned)base, (unsigned)g->m_remoteStart, g->m_count, (int)area);
        return false;
    }
    XPrintf("HsfServerIoVfd: seg[%d] type=%u area=%d v=0x%04X remote=0x%04X "
            "count=%u flags=%u\n", gi, (unsigned)g->m_area, (int)area,
            (unsigned)base, (unsigned)g->m_remoteStart, g->m_count,
            (unsigned)g->m_flags);
    st->m_vbase[gi] = base;
    st->m_vbaseBuilt[gi] = 1;
    return true;
}

/**
 * @brief 设备层 emitError → 服务端日志。
 * @details [为什么必须留痕] 动作监督超时/写拒绝这类错误此前无人订阅，
 *          只进设备层 m_lastError——界面上就是"点了没反应"，现场无从
 *          查起。与登录拒绝留痕同口径：至少落一行日志（receiver=设备
 *          自身，打印指针区分 A/B 侧）。
 */
static void VHsfServerIoVfd_onDeviceError(XObject* receiver, XVarList* args)
{
    const char* msg = "";
    if (!args) return;
    XVarList_start(args);
    msg = XVarList_arg(args, const char*);
    XPrintf("HsfServerIoVfd: 设备错误（dev=%p）：%s\n",
            (void*)receiver, (msg && msg[0]) ? msg : "(空)");
}

/**
 * @brief 按品牌规格建语义点（AI/AO 用 XIoAnalogPoint，DI/DO 用基类）。
 * @return 新点（autoDelete 语义，交给组托管）；失败返回 NULL。
 * @note 语义点不在通道层标度（XVfdDevice 的 DecodePoint 需要原生量自己
 *       换算，int32 截断会丢精度），恒 div=1；参数点另行标度。
 */
static XIoPoint* vfdMakePoint(const XVfdPointSpec* spec, int area)
{
    XIoPoint* pt;
    if (!spec) return NULL;
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
    XIoAnalogPoint_setScale((XIoAnalogPoint*)pt, 1, 1, 0, 0);
    return pt;
}

/**
 * @brief 按品牌绑定表装配全部语义点 + 从站协议约束。
 * @return 全部成功返回 true。
 */
static bool vfdMountBrand(HsfServerIoVfd* st)
{
    const XVfdBindTable* bind = XVfdM512_modbusBinding();
    const XVfdCommGroup* groups;
    int groupCount;
    int specCount = 0;
    const XVfdPointSpec* specs = XVfdM512_pointSpecs(&specCount);
    int i;

    if (!bind || !bind->m_groups || bind->m_groupCount <= 0 ||
        !specs || specCount <= 0) {
        XPrintf("HsfServerIoVfd: M512 绑定表/点规格表为空\n");
        return false;
    }
    groups = bind->m_groups;
    groupCount = bind->m_groupCount;
    /* [记组数 2026-11-10] 快照陈旧阈值要按「轮转周期 = 组数 × 拍长」算：
     * Hub 每拍只发一个读组，组数就是一圈要发多少帧。 */
    st->m_groupCount = groupCount;
    for (i = 0; i < specCount; ++i) {
        XVfdPointBinding pb;
        int gi;
        uint16_t vaddr;
        XIoPoint* pt;
        int area;
        if (!XVfdBind_findPoint(bind, specs[i].m_role, &pb)) {
            XPrintf("HsfServerIoVfd: 点 %s 无绑定表条目\n",
                    specs[i].m_tag ? specs[i].m_tag : "?");
            return false;
        }
        gi = vfdFindGroup(groups, groupCount, pb.m_address);
        if (gi < 0) {
            XPrintf("HsfServerIoVfd: 点 %s 地址 0x%04X 不在任何通信组内\n",
                    specs[i].m_tag ? specs[i].m_tag : "?", pb.m_address);
            return false;
        }
        if (!vfdEnsureGroup(st, &groups[gi], gi)) return false;
        /* 虚拟地址 = 组虚拟起点 + 组内偏移（远端 0x1001 组 → vbase+0..）。 */
        vaddr = (uint16_t)(st->m_vbase[gi] +
                           (pb.m_address - groups[gi].m_remoteStart));
        area = (int)XModbusLink_areaOfType((int)specs[i].m_type, true);
        pt = vfdMakePoint(&specs[i], area);
        if (!pt) return false;
        if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
            XClassDelete((XClass*)pt);
            return false;
        }
        if (!XVfdDevice_bindPoint((XVfdDevice*)st->m_dev, &specs[i], pt)) {
            (void)XIoChannelGroup_removeRole(st->m_group, specs[i].m_role);
            XPrintf("HsfServerIoVfd: 绑定点 %s 失败\n",
                    specs[i].m_tag ? specs[i].m_tag : "?");
            return false;
        }
        if (!XIoLink_bindRole_base((XIoLink*)st->m_link, specs[i].m_role,
                                   vaddr, (uint32_t)area)) {
            XPrintf("HsfServerIoVfd: bindRole(role=%u v=0x%04X area=%d) 失败\n",
                    (unsigned)specs[i].m_role, (unsigned)vaddr, area);
            return false;
        }
        XPrintf("HsfServerIoVfd: bind role=%u tag=%s v=0x%04X area=%d\n",
                (unsigned)specs[i].m_role,
                specs[i].m_tag ? specs[i].m_tag : "?", (unsigned)vaddr, area);
    }
    /* 从站协议约束（单帧读上限/组边界/只允许单写/分级节拍——作用域=从站）。 */
    if (bind->m_protocol) {
        XModbusHubUnitLimits lim;
        XModbusHub* hub = HsfServerBus_hub(st->m_bus);
        /* [2026-11-10 根修：必须清零]
         * XModbusHubUnitLimits 里本函数没显式填的字段（本次是
         * m_preTxQuietMs）此前留着**栈垃圾**（Debug 构建 = 0xCCCCCCCC
         * ≈ 34 亿毫秒）⇒ Hub 的「发帧前静默闸」要求 34 亿毫秒总线静默
         * ⇒ 变频器站的读组**永远**轮不到：现场表现为"变频器整站离线、
         * 失败计数=0、游标卡在 1、其余组从不再发"，而同一总线上温湿度站
         * 一切正常。其它三站的装配（温湿度/流量计/浊度仪）都有 memset，
         * 只有变频器这一处漏了。
         * 框架侧另有兜底（setUnitLimits 夹住离谱值 + 静默闸饥饿逃逸），
         * 但根因在本处 ⇒ 本处必须清零。 */
        memset(&lim, 0, sizeof(lim));
        lim.m_maxReadRegisters = bind->m_protocol->m_maxReadRegisters;
        lim.m_rangeBarrier = bind->m_protocol->m_rangeBarrier;
        lim.m_singleWriteOnly = bind->m_protocol->m_singleWriteOnly;
        lim.m_minPollIntervalMs = HSF_VFD_MIN_POLL_MS;   /* 0=占满轮转。 */
        if (!XModbusHub_setUnitLimits(hub, HsfServerBus_deviceId(st->m_bus),
                                      (int)st->m_unitId, &lim)) {
            XPrintf("HsfServerIoVfd: setUnitLimits(变频器) 失败\n");
            return false;
        }
    }
    return true;
}

/* ==================== 生命周期 ==================== */

static void VHsfServerIoVfd_deinit(XObject* obj)
{
    HsfServerIoVfd* st = (HsfServerIoVfd*)obj;
    if (!st) return;
    HsfServerIoVfd_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* HsfServerIoVfd_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoVfd)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoVfd_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoVfd);
    return XVTABLE_DEFAULT;
}

void HsfServerIoVfd_init(HsfServerIoVfd* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoVfd_class_init();
    st->m_unitId = -1;
    st->m_mounted = false;
}

bool HsfServerIoVfd_mount(HsfServerIoVfd* st, HsfServerBus* bus,
                          XModbusMapping* map, int unitId,
                          int actionTimeoutMs, int statusIntervalMs)
{
    if (!st || st->m_mounted) return false;
    if (!bus || !map) return false;
    if (!HsfServerBus_hub(bus) || HsfServerBus_deviceId(bus) < 0) {
        XPrintf("HsfServerIoVfd: 总线未就绪\n");
        return false;
    }
    st->m_bus = bus;
    st->m_map = map;
    st->m_unitId = unitId;

    st->m_group = XIoChannelGroup_create();
    st->m_link = XModbusLink_create(HsfServerBus_hub(bus));
    if (!st->m_group || !st->m_link ||
        !XIoChannelGroup_setLink(st->m_group, (XIoLink*)st->m_link)) {
        XPrintf("HsfServerIoVfd: 站组/链路装配失败\n");
        goto fail;
    }
    st->m_dev = XVfdM512_create();
    if (!st->m_dev) goto fail;
    /* 设备错误留痕（见槽函数说明；receiver=设备自身，随设备析构自断）。 */
    XObject_connect((XObject*)st->m_dev,
                      XSignal(XVfdDevice_errorOccurred_signal),
                      (XObject*)st->m_dev, VHsfServerIoVfd_onDeviceError,
                      XConnectionType_Auto);
    if (!XVfdDevice_attach((XVfdDevice*)st->m_dev, st->m_group)) {
        XPrintf("HsfServerIoVfd: 变频器设备挂组失败\n");
        goto fail;
    }
    if (!vfdMountBrand(st)) goto fail;
    /* 链路订阅：必须在 HsfServerBus_start **之前**建立。Hub 的
     * valueChanged 是「变化才发」，首拍之后稳定量不再发——订阅晚一拍就
     * 拿不到初值（2026-11-04 温湿度实锅；变频器因状态字每拍都变而侥幸）。 */
    if (!XIoLink_start_base((XIoLink*)st->m_link)) {
        XPrintf("HsfServerIoVfd: 链路启动失败\n");
        goto fail;
    }
    /* 设备参数：动作监督放宽（28 组轮转）、状态周期发布、开控制闸
     *（权限在 XLink 角色层管，服务端设备闸恒开）。 */
    XVfdDevice_setActionTimeout((XVfdDevice*)st->m_dev, actionTimeoutMs);
    XVfdDevice_setStatusInterval((XVfdDevice*)st->m_dev, statusIntervalMs);
    XVfdDevice_setControlEnabled((XVfdDevice*)st->m_dev, true);
    st->m_mounted = true;
    XPrintf("HsfServerIoVfd: 站 %d 装配完成\n", unitId);
    return true;

fail:
    HsfServerIoVfd_unmount(st);
    return false;
}

void HsfServerIoVfd_unmount(HsfServerIoVfd* st)
{
    if (!st) return;
    if (st->m_dev) {
        XVfdDevice_detach((XVfdDevice*)st->m_dev);
        XClassDelete((XClass*)st->m_dev);
        st->m_dev = NULL;
    }
    if (st->m_group) { XClassDelete((XClass*)st->m_group); st->m_group = NULL; }
    if (st->m_link)  { XClassDelete((XClass*)st->m_link);  st->m_link = NULL; }
    st->m_bus = NULL;
    st->m_map = NULL;
    st->m_unitId = -1;
    memset(st->m_vbaseBuilt, 0, sizeof(st->m_vbaseBuilt));
    memset(st->m_vbase, 0, sizeof(st->m_vbase));
    st->m_mounted = false;
}

XVfdM512* HsfServerIoVfd_device(const HsfServerIoVfd* st)
{
    return st ? st->m_dev : NULL;
}

bool HsfServerIoVfd_isMounted(const HsfServerIoVfd* st)
{
    return st && st->m_mounted;
}

/* ==================== 快照陈旧阈值（随轮转周期） ==================== */

uint32_t HsfServerIoVfd_applyStaleMs(HsfServerIoVfd* st, int pollMs)
{
    uint32_t roundMs;
    uint32_t ms;
    int tickMs = pollMs;
    if (!st || !st->m_mounted) return 0;
    /* [拍长取 Hub 的真实节拍 2026-11-10] 一圈 = 组数 × **Hub 的拍长**
     * （Hub 每拍只发一个读组）。Hub 的拍长 = 该总线上各从站周期里最紧
     * 的那档（不是变频器自己那档）——取错了会把阈值算大（链路真断了也
     * 半天不报）。查不到才退回调用方给的拍长。 */
    if (st->m_bus) {
        XModbusHub* hub = HsfServerBus_hub(st->m_bus);
        if (hub) {
            const size_t t = XModbusHub_pollInterval(hub,
                                            HsfServerBus_deviceId(st->m_bus));
            if (t > 0) tickMs = (int)t;
        }
    }
    /* 轮转周期 = 组数 × 拍长（Hub 每拍只发一个读组）。 */
    roundMs = (tickMs > 0 && st->m_groupCount > 0)
                  ? (uint32_t)tickMs * (uint32_t)st->m_groupCount : 0u;
    ms = HSF_VFD_STALE_MS;
    if (roundMs > 0) {
        const uint32_t byRound = roundMs * 2u;   /* 容一圈抖动 */
        if (byRound > ms) ms = byRound;
    }
    st->m_staleMs = ms;
    if (st->m_dev) XVfdDevice_setStaleMs((XVfdDevice*)st->m_dev, ms);
    XPrintf("HsfServerIoVfd: 陈旧阈值 %ums（组数=%d 拍长=%dms 轮转=%ums）\n",
            (unsigned)ms, st->m_groupCount, tickMs, (unsigned)roundMs);
    return ms;
}
