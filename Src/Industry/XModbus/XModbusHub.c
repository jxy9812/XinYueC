/**
 * @file       XModbusHub.c
 * @brief      Modbus 聚合设备实现（多从站虚拟合一 + 每设备缓存层）。
 * @details    结构总览：
 *               - m_devices：XModbusHubDevice 值数组（XVector），设备句柄=id
 *                 字段线性查找（句柄不复用，删除即摘除槽位）。
 *               - 所有权走 XObject 父子机制（框架惯例，析构自动删 children）：
 *                 addDevice 把 client reparent 到 hub；每次发起请求把 Reply
 *                 parent 到 hub。设备槽内只挂借用指针与非 XObject 资源。
 *               - [2026-10-29 缓存层重构] 缓存坐标从「全局虚拟地址连续图」
 *                 换成「每设备缓存块」：块 = 同 (寄存器区, 从站) 下远端
 *                 连续的映射段合并（块内无洞）。上层 API 仍是虚拟地址：
 *                 m_view[4] 是按 virtualStart 有序的视图切片表（二分定位
 *                 → 块内偏移），读写都经它翻译。
 *               - 读：每设备一张读组表（同区同从站、远端间隔 ≤
 *                 setReadMergeGap 的相邻块并成一帧，按协议上限切分）；设备
 *                 周期定时器逐组单飞，应答按块交集回填，valueChanged 按
 *                 块内映射切片逐段发射（首次回填恒发一次，供上层播种）。
 *               - 写：受理即写块缓存（乐观回显，微秒级）+ 标脏区间，返回
 *                 true；设备空闲时把脏区间按 FC15/FC16 一帧下发（单帧按
 *                 协议上限切分），失败保留脏区间周期重发；attempts 达阈值
 *                 发一次 writeStalled 与一次 writeCompleted(ok=false)。
 *                 写合并窗口 setWriteCoalesceMs 内的多次写只留最终值。
 *               - 质量：块级 lastOkMs 支撑 valueAge/pointValid 点级新鲜度。
 *               - 设备状态：connect_2 跟踪（connect_2 第一参=真 sender=client，
 *                 经 parent 反查 hub 后按指针定设备），恢复 Connected 时
 *                 reconnectCount++ 并立即泵轮询/补写滞留写——底层断线重连
 *                 对上层透明。
 *             应答→设备归属：发起时记入 dev->inflight，完成槽按指针扫描匹配。
 */
#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#if XMODBUS_HUB_ON

#include "XModbusHub.h"
#include "XModbusClient_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XModbusPdu.h"      /* 异常帧判定（XModbusPdu_isException）。 */
#include "XMemory.h"
#include "XVector.h"
#include "XString.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>

/* ==================== 诊断开关（env 门控，正式诊断手段） ====================
 *
 * XMODBUS_HUB_TRACE=1
 *   打开 Hub 侧逐组/逐笔诊断：① 读组被选中（过了哪几道闸）② 客户端受理
 *   结果（Reply 指针/终态/错误码）③ 泵每拍的在途状态（inflight/时刻戳）。
 *   现场定位"某站读组发不出去 / 游标卡死"时，这三行是唯一能分辨
 *   「请求没出 Hub」「客户端拒收」「从站不应答」的分水岭。
 *   限频打印（每设备 8 条），不刷屏。
 * XMODBUS_VIRTUAL_TRACE=1
 *   打开虚拟从站侧的取数诊断（请求到了没有、模型绑上没有、读成功没有）。
 *   两个开关互相配合：Hub 侧说"发了"但虚拟侧没打 ⇒ 请求在客户端里被
 *   吞掉（未连接/拒收/丢弃路径）；两侧都打了 ⇒ 到了从站，是从站不应答。
 */
#define XMODBUS_HUB_TRACE_DEV_CAP 16

/**
 * @brief 诊断限频闸：按 **Hub 指针**分桶计数。
 * @details [为什么按 Hub 而不是 dev->id] 同一个进程里每个 Hub 的设备句柄
 *          都从 1 起算（dev->id=1 是常态）⇒ 只按 dev->id 计数时，先跑的
 *          Hub 会吃光额度，后面的 Hub **一行都打不出来**——现场"逐组 trace
 *          零输出"就是这么来的假象（那台设备其实一直在被闸门跳过）。
 * @param hub Hub 指针（NULL=不打印）。
 * @param limit 该 Hub 的打印条数上限。
 * @return true=本条应当打印（并已计入额度）。
 */
static bool hubTraceAllow(XModbusHub* hub, int limit)
{
    static XModbusHub* s_bucket[XMODBUS_HUB_TRACE_DEV_CAP];
    static int s_count[XMODBUS_HUB_TRACE_DEV_CAP];
    int b = 0;
    if (!hub || limit <= 0) return false;
    while (b < XMODBUS_HUB_TRACE_DEV_CAP && s_bucket[b] && s_bucket[b] != hub)
        ++b;
    if (b >= XMODBUS_HUB_TRACE_DEV_CAP) return false;
    if (!s_bucket[b]) {
        s_bucket[b] = hub;
        s_count[b] = 0;
    }
    if (s_count[b] >= limit) return false;
    ++s_count[b];
    return true;
}

/* ==================== 槽前置声明 ==================== */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args);
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args);
static void hubPumpDeviceEx(XModbusHub* hub, XModbusHubDevice* dev,
                            bool allowPoll);
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev);
static void hubPumpWriteQueue(XModbusHub* hub);
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev);

/* [同步终态收口嵌套深度 2026-11-09]
 *
 * [为什么从文件级全局改成 per-hub 字段] 原 `static int s_hubInSyncFinish`
 * 是**进程级**的：多 Hub 场景下 A 呼的收口若经 valueChanged 槽触发 B 呼的
 * 写，B 呼的同步收口会被误判成"嵌套"⇒ 那一帧被整包丢弃。
 *
 * [语义] 0=不在收口中；1=最外层同步收口；>=2=嵌套层。
 * 嵌套层**不再丢弃结果**（改为完整记账），但**不再泵**（深度上界）——
 * 见 hubSendPollGroup / hubSendDirtyRange 的就地收口段与收口尾部。 */
#define HUB_SYNC_FINISH_MAX_DEPTH 2

/* ==================== 虚函数前置声明（class_init 在文件前部引用） ==================== */
static void VXModbusHub_deinit(XModbusHub* hub);
static void VXModbusHub_timerEvent(XObject* obj, XTimerEvent* event);

/* ==================== 内部辅助 ==================== */

/** @brief 按句柄查设备槽下标；未命中返回 -1。 */
static int hubDeviceIndexById(const XModbusHub* hub, int deviceId)
{
    if (!hub || !hub->m_devices || deviceId < 0) return -1;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d && d->id == deviceId) return (int)i;
    }
    return -1;
}

static XModbusHubDevice* hubDeviceById(XModbusHub* hub, int deviceId)
{
    const int idx = hubDeviceIndexById(hub, deviceId);
    if (idx < 0) return NULL;
    return (XModbusHubDevice*)XVector_at_base(hub->m_devices, idx);
}

/** @brief 停某设备轮询定时器（kill + 复位；周期定时器泄漏纪律）。 */
static void hubStopDevicePollTimer(XModbusHub* hub, XModbusHubDevice* dev)
{
    if (!hub || !dev) return;
    if (dev->pollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, dev->pollTimer);
        dev->pollTimer = XTIMER_INVALID_ID;
    }
}

/* [2026-10-26 越界根修] 枚举含 Invalid=0，四类区=1..4——数组下标一律经
 * 本函数取稠密 0..3（直接拿枚举当下标对 HR=4 越界一字）。 */
static inline int hubTypeIndex(int type)
{
    if (type < XModbusDiscreteInputs || type > XModbusHoldingRegisters) return -1;
    return type - (int)XModbusDiscreteInputs;
}
/** @brief 稠密下标(0..3)转回枚举值。 */
static inline int hubEnumOfType(int denseIdx)
{
    return denseIdx + (int)XModbusDiscreteInputs;
}
/** @brief 位区判定（位区 1bit/点，寄存器区 2B/点）。 */
static inline bool hubTypeIsBit(uint16_t type)
{
    return type == (uint16_t)XModbusDiscreteInputs || type == (uint16_t)XModbusCoils;
}
/** @brief 单帧写点数上限（位区 FC15=1968，寄存器区 FC16=123）。 */
static inline uint16_t hubWriteLimitOfType(uint16_t type)
{
    return hubTypeIsBit(type) ? (uint16_t)XMODBUS_HUB_WRITE_MAX_BITS
                              : (uint16_t)XMODBUS_HUB_WRITE_MAX_REGISTERS;
}
/** @brief 单帧读点数上限（位区 2000，寄存器区 125）。 */
static inline uint16_t hubReadLimitOfType(uint16_t type)
{
    return hubTypeIsBit(type) ? (uint16_t)XMODBUS_HUB_READ_MAX_BITS
                              : (uint16_t)XMODBUS_HUB_READ_MAX_REGISTERS;
}
/**
 * @brief 查某从站的协议约束（设备内按 unitId 簿记；未配置返回 NULL）。
 * @note 约束作用域是**从站**而非链路：一条 RTU 串口下可挂多个从站，各从站
 *       能力互不相同（限 12 字 / 只支持 FC06 / 有功能码组边界…）。
 */
static const XModbusHubUnitLimits* hubUnitLimitsOf(const XModbusHubDevice* dev,
                                                    int unitId)
{
    size_t i;
    if (!dev || !dev->m_unitLimits) return NULL;
    for (i = 0; i < XVector_size_base(dev->m_unitLimits); ++i) {
        const XModbusHubUnitLimitEntry* e =
            (const XModbusHubUnitLimitEntry*)XVector_at_base(dev->m_unitLimits,
                                                              (int64_t)i);
        if (e && e->unitId == unitId) return &e->m_limits;
    }
    return NULL;
}
/**
 * @brief 从站级单帧读点数上限（叠加从站约束；位区不适用从站上限）。
 * @details 部分从站只允许单帧读 N 个寄存器（如某变频器限 12）。从站上限
 *          只收紧不放宽：取「从站上限」与「协议全局上限」的较小者。
 */
static inline uint16_t hubReadLimitForUnit(const XModbusHubDevice* dev, int unitId,
                                           uint16_t type)
{
    const uint16_t base = hubReadLimitOfType(type);
    const XModbusHubUnitLimits* lim;
    if (!dev || hubTypeIsBit(type)) return base;
    lim = hubUnitLimitsOf(dev, unitId);
    if (!lim || lim->m_maxReadRegisters == 0) return base;
    return (lim->m_maxReadRegisters < base) ? lim->m_maxReadRegisters : base;
}
/** @brief 该从站是否启用「映射段边界为读帧硬边界」。 */
static inline bool hubUnitHasRangeBarrier(const XModbusHubDevice* dev, int unitId)
{
    const XModbusHubUnitLimits* lim = hubUnitLimitsOf(dev, unitId);
    return (lim && lim->m_rangeBarrier) ? true : false;
}
/** @brief 该从站是否只允许单寄存器写（FC06）。 */
static inline bool hubUnitSingleWriteOnly(const XModbusHubDevice* dev, int unitId)
{
    const XModbusHubUnitLimits* lim = hubUnitLimitsOf(dev, unitId);
    return (lim && lim->m_singleWriteOnly) ? true : false;
}
/** @brief 该从站的发帧前总线静默期（毫秒；0=不设闸）。 */
static inline uint32_t hubUnitPreTxQuietMs(const XModbusHubDevice* dev, int unitId)
{
    const XModbusHubUnitLimits* lim = hubUnitLimitsOf(dev, unitId);
    return (lim && lim->m_preTxQuietMs != 0) ? lim->m_preTxQuietMs : 0u;
}

/* ==================== 从站分级轮询（每从站最小发送间隔） ==================== */

/**
 * @brief 查某从站最近一次读帧发出时刻。
 * @return 时刻（单调毫秒）；从未发过返回 0。
 */
static uint64_t hubUnitLastSentOf(const XModbusHubDevice* dev, int unitId)
{
    size_t i;
    if (!dev || !dev->m_unitLastSent) return 0;
    for (i = 0; i < XVector_size_base(dev->m_unitLastSent); ++i) {
        const XModbusHubUnitSent* e =
            (const XModbusHubUnitSent*)XVector_at_base(dev->m_unitLastSent,
                                                        (int64_t)i);
        if (e && e->unitId == unitId) return e->lastSentMs;
    }
    return 0;
}

/**
 * @brief 取某从站的「静默闸连续被挡起点」簿记条目（可写；不存在返回 NULL）。
 */
static XModbusHubUnitSent* hubUnitSentEntryOf(XModbusHubDevice* dev, int unitId)
{
    size_t i;
    if (!dev || unitId < 0 || !dev->m_unitLastSent) return NULL;
    for (i = 0; i < XVector_size_base(dev->m_unitLastSent); ++i) {
        XModbusHubUnitSent* e =
            (XModbusHubUnitSent*)XVector_at_base(dev->m_unitLastSent, (int64_t)i);
        if (e && e->unitId == unitId) return e;
    }
    return NULL;
}

/**
 * @brief 「发帧前静默闸」的饥饿逃逸判定。
 * @details [2026-11-10 框架级根修·活性保证]
 *          静默闸量的是「本串口上一笔事务收口至今」，而一条 485 上往往
 *          挂着多个从站 ⇒ **别的从站的流量会把静默窗口一直顶掉**。若配置
 *          值离谱（现场根因：配置侧把 m_preTxQuietMs 留成未初始化的栈
 *          垃圾 0xCCCCCCCC ≈ 34 亿毫秒），该从站的读组就**永远轮不到**
 *          ——现场表现"变频器整站哑掉、失败计数=0、游标卡死"，而总线上
 *          别的站一切正常。
 *          框架层的活性保证：某从站被静默闸**连续**挡住超过
 *          XMODBUS_HUB_QUIET_ESCAPE_MS ⇒ 放行一帧（限频告警），此后该
 *          从站恢复正常轮询。**任何从站都不会被静默闸永久饿死。**
 * @param dev 设备槽；unitId 从站号；nowMs 当前时刻。
 * @return true=本拍应当放行（逃逸生效）；false=静默闸照常拦截。
 */
static bool hubUnitQuietEscape(XModbusHubDevice* dev, int unitId, uint64_t nowMs)
{
    XModbusHubUnitSent* e = hubUnitSentEntryOf(dev, unitId);
    uint64_t blockedSince;
    if (!e) return false;
    if (e->quietBlockedSinceMs == 0) {
        e->quietBlockedSinceMs = nowMs;    /* 本次连续被挡的起点。 */
        return false;
    }
    if (nowMs < e->quietBlockedSinceMs) {  /* 时钟回退：重新起算。 */
        e->quietBlockedSinceMs = nowMs;
        return false;
    }
    blockedSince = nowMs - e->quietBlockedSinceMs;
    if (blockedSince < (uint64_t)XMODBUS_HUB_QUIET_ESCAPE_MS) return false;
    /* 逃逸：放行一帧，并清零起点（下一轮重新计时）。 */
    e->quietBlockedSinceMs = 0;
    {
        static int s_escape = 0;
        if (s_escape < 5) {
            ++s_escape;
            XPrintf("XModbusHub: 从站 %d 被发帧前静默闸连续挡住 %llu ms ——"
                    "逃逸放行一帧（静默期配置过大或总线上其它从站流量过密："
                    "静默闸按**串口级**收口时刻量，别站流量会把窗口顶掉）\n",
                    unitId, (unsigned long long)blockedSince);
        }
    }
    return true;
}

/**
 * @brief 记账某从站的读帧发出时刻（表惰性建；已存在则更新）。
 * @return 记账成功返回 true；分配失败返回 false（不影响轮询本身）。
 */
static bool hubUnitMarkSent(XModbusHubDevice* dev, int unitId, uint64_t nowMs)
{
    size_t i;
    XModbusHubUnitSent entry;
    if (!dev || unitId < 0) return false;
    if (!dev->m_unitLastSent) {
        dev->m_unitLastSent = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                sizeof(XModbusHubUnitSent), false);
        if (!dev->m_unitLastSent) return false;
    }
    for (i = 0; i < XVector_size_base(dev->m_unitLastSent); ++i) {
        XModbusHubUnitSent* e =
            (XModbusHubUnitSent*)XVector_at_base(dev->m_unitLastSent, (int64_t)i);
        if (e && e->unitId == unitId) {
            e->lastSentMs = nowMs;
            return true;
        }
    }
    entry.unitId = unitId;
    entry.lastSentMs = nowMs;
    entry.quietBlockedSinceMs = 0;   /* [静默闸饥饿逃逸] 新条目：未在挡。 */
    return XVector_push_back_1_base(dev->m_unitLastSent, &entry);
}

/**
 * @brief 该从站是否已到允许发送的时刻（分级轮询闸）。
 * @param dev 设备槽；unitId 从站号；nowMs 当前时刻。
 * @return true=可以发（未配置最小间隔 / 从未发过 / 已到期）。
 * @note [2026-11-04 分级轮询] 未配 m_minPollIntervalMs 的从站恒放行，
 *       行为与旧版完全一致（零回归）。慢从站（如温湿度 500ms）到期前
 *       被跳过，快从站因此不必陪它轮转整个周期。
 */
static bool hubUnitDue(const XModbusHubDevice* dev, int unitId, uint64_t nowMs)
{
    const XModbusHubUnitLimits* lim = hubUnitLimitsOf(dev, unitId);
    uint64_t last;
    if (!lim || lim->m_minPollIntervalMs == 0) return true;
    last = hubUnitLastSentOf(dev, unitId);
    if (last == 0) return true;
    if (nowMs < last) return true;          /* 时钟回退：放行，不卡死。 */
    return (nowMs - last) >= (uint64_t)lim->m_minPollIntervalMs;
}

/* ==================== 缓存块生命周期 ==================== */

/** @brief 释放一个缓存块（数据单元 + 切片表 + 脏区间表 + 结构体）。 */
static void hubBlockFree(XModbusHubCacheBlock* block)
{
    if (!block) return;
    if (block->data) XClassDelete(block->data);
    if (block->segs) XClassDelete(block->segs);
    if (block->dirty) XClassDelete(block->dirty);
    XFree_System(block);
}

/** @brief 释放块表（XVector<XModbusHubCacheBlock*>，元素本身由块持有）。 */
static void hubBlocksFreeVector(struct XVector* blocks)
{
    if (!blocks) return;
    const size_t n = XVector_size_base(blocks);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(blocks, (int64_t)i);
        hubBlockFree(b);
    }
    XClassDelete(blocks);
}

/**
 * @brief 建一个缓存块（数据按区初始化哨兵：位区 0、寄存器区 0xFFFF）。
 * @details 位区初值必须 0：setValue 对位单元只留 bit0，0xFFFF 会截断成 1
 *          ——「全 1 初值」曾伪装成已回填数据（2026-10-26 闭环实证）。
 */
static XModbusHubCacheBlock* hubBlockCreate(uint16_t type, int unitId,
                                            uint16_t remoteStart, uint16_t count)
{
    XModbusHubCacheBlock* b;
    const uint16_t initVal = hubTypeIsBit(type) ? 0u : 0xFFFFu;
    if (count == 0) return NULL;
    b = (XModbusHubCacheBlock*)XMalloc_System(sizeof(XModbusHubCacheBlock));
    if (!b) return NULL;
    memset(b, 0, sizeof(*b));
    b->type = type;
    b->unitId = unitId;
    b->remoteStart = remoteStart;
    b->count = count;
    b->data = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XModbusRegisterType)type, remoteStart, count);
    b->segs = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                sizeof(XModbusHubCacheSeg), false);
    /* dirty 惰性创建（NULL=无脏区间）：只读输入块（DI/IR 轮询块）一辈子
     * 不会标脏，白背一个空向量=每块多一次堆分配；首脏由 hubDirtyPushRaw
     * 按需分配（2026-10-31 内存优化）。 */
    b->dirty = NULL;
    if (!b->data || !b->segs) {
        hubBlockFree(b);
        return NULL;
    }
    for (uint16_t i = 0; i < count; ++i)
        XModbusDataUnit_setValue(b->data, i, initVal);
    return b;
}

/** @brief 取设备块表中某类型某从站下覆盖 remote 地址的块（借用；未命中 NULL）。 */
static XModbusHubCacheBlock* hubFindBlockAt(XModbusHubDevice* dev, uint16_t type,
                                            int unitId, uint32_t remote)
{
    if (!dev || !dev->blocks) return NULL;
    const size_t n = XVector_size_base(dev->blocks);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)i);
        if (!b || b->type != type || b->unitId != unitId) continue;
        if (remote < b->remoteStart ||
            remote >= (uint32_t)b->remoteStart + b->count) continue;
        return b;
    }
    return NULL;
}

/* ==================== 脏区间簿记 ==================== */

/** @brief 脏区间是否覆盖块内偏移 offset（轮询回填跳过未确认写点）。 */
static bool hubDirtyHasOffset(const XModbusHubCacheBlock* block, uint16_t offset)
{
    if (!block || !block->dirty) return false;
    const size_t n = XVector_size_base(block->dirty);
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDirtyRange* d =
            (const XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        if (d && offset >= d->offset &&
            (uint32_t)offset < (uint32_t)d->offset + d->count) return true;
    }
    return false;
}

/** @brief 按 offset 升序插入一条脏区间（不合并；内部/迁移用）。
 *  @note dirty 惰性创建点：首次标脏才分配向量；分配失败按 OOM 容错
 *        丢弃本条标脏（与既有 insert 失败同级，不回滚乐观回显）。 */
static void hubDirtyPushRaw(XModbusHubCacheBlock* block,
                            const XModbusHubDirtyRange* range)
{
    size_t i;
    size_t n;
    if (!block || !range || range->count == 0) return;
    if (!block->dirty) {
        block->dirty = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         sizeof(XModbusHubDirtyRange), false);
        if (!block->dirty) return;
    }
    n = XVector_size_base(block->dirty);
    for (i = 0; i < n; ++i) {
        const XModbusHubDirtyRange* d =
            (const XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        if (d && range->offset < d->offset) break;
    }
    XVector_insert_1_base(block->dirty, (int64_t)i, range, 1);
}

/**
 * @brief 标脏：与既有脏区间重叠或相邻即合并（合并窗口内多次写只留最终值）。
 * @note  并入既有区间视为「新意图」：attempts 与两个提示沿一并复位。
 */
static void hubMarkDirty(XModbusHubCacheBlock* block, uint16_t offset, uint16_t count)
{
    if (!block || count == 0) return;
    const uint32_t newEnd = (uint32_t)offset + count;
    size_t n = block->dirty ? XVector_size_base(block->dirty) : 0;
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDirtyRange* d =
            (XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)i);
        const uint32_t dEnd = (uint32_t)d->offset + d->count;
        if (d && (uint32_t)offset <= dEnd && (uint32_t)d->offset <= newEnd) {
            const uint32_t lo = ((uint32_t)d->offset < offset) ? d->offset : offset;
            const uint32_t hi = (dEnd > newEnd) ? dEnd : newEnd;
            d->offset = (uint16_t)lo;
            d->count = (uint16_t)(hi - lo);
            d->attempts = 0;
            d->stallNotified = false;
            d->failNotified = false;
            return;
        }
    }
    {
        XModbusHubDirtyRange r;
        memset(&r, 0, sizeof(r));
        r.offset = offset;
        r.count = count;
        hubDirtyPushRaw(block, &r);
    }
}

/** @brief 摘除/切分脏区间：从 [offset, offset+count) 收口（写成功）。 */
static void hubDirtySubtract(XModbusHubCacheBlock* block, uint16_t offset, uint16_t count)
{
    uint32_t newEnd;
    size_t i;
    if (!block || !block->dirty || count == 0) return;
    newEnd = (uint32_t)offset + count;
    for (i = XVector_size_base(block->dirty); i > 0; --i) {
        XModbusHubDirtyRange* d =
            (XModbusHubDirtyRange*)XVector_at_base(block->dirty, (int64_t)(i - 1));
        const uint32_t dStart = d->offset;
        const uint32_t dEnd = dStart + d->count;
        if (!d) continue;
        if (dEnd <= offset || dStart >= newEnd) continue;      /* 无交集。 */
        if (dStart >= offset && dEnd <= newEnd) {              /* 完全覆盖：删。 */
            XVector_remove_base(block->dirty, (int64_t)(i - 1), 1);
            continue;
        }
        if (dStart < offset && dEnd > newEnd) {                /* 中间挖空：切两段。 */
            XModbusHubDirtyRange right;
            right.offset = (uint16_t)newEnd;
            right.count = (uint16_t)(dEnd - newEnd);
            right.attempts = d->attempts;
            right.stallNotified = d->stallNotified;
            right.failNotified = d->failNotified;
            d->count = (uint16_t)(offset - dStart);
            hubDirtyPushRaw(block, &right);
            continue;
        }
        if (dStart < offset) {                                 /* 掐尾。 */
            d->count = (uint16_t)(offset - dStart);
        } else {                                              /* 掐头。 */
            d->offset = (uint16_t)newEnd;
            d->count = (uint16_t)(dEnd - newEnd);
        }
    }
}

/** @brief 设备是否还有待写脏区间。 */
static bool hubDeviceHasDirty(const XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    if (!dev || !dev->blocks) return false;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b && b->dirty && XVector_size_base(b->dirty) > 0) return true;
    }
    return false;
}

/** @brief 设备脏区间总数（pendingWriteCount 口径=合并后区间数）。 */
static int hubDeviceDirtyCount(const XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    int total = 0;
    if (!dev || !dev->blocks) return 0;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b && b->dirty) total += (int)XVector_size_base(b->dirty);
    }
    return total;
}

/* ==================== 重建：块 / 视图 / 读组 ==================== */

/** @brief 切片按 virtualStart 升序排序（插入排序；段数为十级）。 */
static void hubSortSegs(struct XVector* segs)
{
    size_t i;
    const size_t n = segs ? XVector_size_base(segs) : 0;
    for (i = 1; i < n; ++i) {
        XModbusHubCacheSeg key =
            *(XModbusHubCacheSeg*)XVector_at_base(segs, (int64_t)i);
        int64_t j = (int64_t)i - 1;
        while (j >= 0) {
            const XModbusHubCacheSeg* cur =
                (const XModbusHubCacheSeg*)XVector_at_base(segs, j);
            if (cur->virtualStart <= key.virtualStart) break;
            *(XModbusHubCacheSeg*)XVector_at_base(segs, j + 1) = *cur;
            --j;
        }
        *(XModbusHubCacheSeg*)XVector_at_base(segs, j + 1) = key;
    }
}

/** @brief 视图切片按 virtualStart 升序排序。 */
static void hubSortView(struct XVector* view)
{
    size_t i;
    const size_t n = view ? XVector_size_base(view) : 0;
    for (i = 1; i < n; ++i) {
        XModbusHubCacheSlice key =
            *(XModbusHubCacheSlice*)XVector_at_base(view, (int64_t)i);
        int64_t j = (int64_t)i - 1;
        while (j >= 0) {
            const XModbusHubCacheSlice* cur =
                (const XModbusHubCacheSlice*)XVector_at_base(view, j);
            if (cur->virtualStart <= key.virtualStart) break;
            *(XModbusHubCacheSlice*)XVector_at_base(view, j + 1) = *cur;
            --j;
        }
        *(XModbusHubCacheSlice*)XVector_at_base(view, j + 1) = key;
    }
}

/** @brief 二分定位覆盖 virtualAddr 的视图切片下标；未命中返回 -1。 */
static int hubViewFind(const struct XVector* view, uint16_t virtualAddr)
{
    size_t lo = 0;
    size_t hi = view ? XVector_size_base(view) : 0;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(view, (int64_t)mid);
        if (!s) return -1;
        if (virtualAddr < s->virtualStart) {
            hi = mid;
        } else if ((uint32_t)virtualAddr >= (uint32_t)s->virtualStart + s->count) {
            lo = mid + 1;
        } else {
            return (int)mid;
        }
    }
    return -1;
}

/**
 * @brief 把旧块缓存值/脏区间按远端交集迁入新块。
 * @details 映射刷新（addRange/removeRange/换 mapping）时值不能丢：新块逐点
 *          按 (区, 从站, 远端地址) 回查旧块；脏区间只在完整覆盖时迁入
 *          （部分覆盖的写已被裁剪，按新意图重写即可）。
 */
static void hubMigrateBlockState(const XModbusHubDevice* dev,
                                 XModbusHubCacheBlock* newBlock)
{
    size_t i;
    size_t n;
    if (!dev || !dev->blocks || !newBlock) return;
    /* 值：逐点回查旧块。 */
    for (uint16_t k = 0; k < newBlock->count; ++k) {
        const uint32_t remote = (uint32_t)newBlock->remoteStart + k;
        const XModbusHubCacheBlock* old =
            hubFindBlockAt((XModbusHubDevice*)dev, newBlock->type,
                           newBlock->unitId, remote);
        if (old) {
            XModbusDataUnit_setValue(newBlock->data, k,
                                     XModbusDataUnit_value(old->data,
                                                           remote - old->remoteStart));
            /* 质量时间戳同样要迁：[2026-10-29] 重建（startPolling/
             * stopPolling/setPollAreas/映射刷新都会触发）不迁 lastOkMs 会让
             * valueAge/pointValid 在每次启停后失真（恒 0 = 从未读回）。 */
            if (old->lastOkMs > newBlock->lastOkMs)
                newBlock->lastOkMs = old->lastOkMs;
            /* 心跳计时同迁：重建后若归 0，稳定块会被判成"从未发射"，
             * 心跳永远起不来（与 lastOkMs 同一类坑）。 */
            if (old->lastEmitMs > newBlock->lastEmitMs)
                newBlock->lastEmitMs = old->lastEmitMs;
            if (old->failCount > newBlock->failCount)
                newBlock->failCount = old->failCount;
        }
    }
    /* 脏区间：完整落在新块内的迁入。 */
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* old =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        size_t k;
        if (!old || old->type != newBlock->type || old->unitId != newBlock->unitId) continue;
        if (!old->dirty) continue; /* 惰性脏区间：旧块无脏向量即无脏可迁。 */
        for (k = 0; k < XVector_size_base(old->dirty); ++k) {
            const XModbusHubDirtyRange* d =
                (const XModbusHubDirtyRange*)XVector_at_base(old->dirty, (int64_t)k);
            const uint32_t start = (uint32_t)old->remoteStart + d->offset;
            if (start < newBlock->remoteStart) continue;
            if (start + d->count > (uint32_t)newBlock->remoteStart + newBlock->count) continue;
            {
                XModbusHubDirtyRange r = *d;
                r.offset = (uint16_t)(start - newBlock->remoteStart);
                hubDirtyPushRaw(newBlock, &r);
            }
        }
    }
}

/**
 * @brief 块物化时把映射段默认值播种进缓存（仅未初始化点）。
 * @details [2026-11-01 输出上线对齐] 缓存是输出意图的事实源，默认值只在
 *          「还没有任何值」时入场：寄存器区哨兵 0xFFFF（从未写过/未回填）
 *          替换为段默认值；位区哨兵 0 处按段默认值置位（默认 0 则保持）。
 *          调用时机在 hubMigrateBlockState 之后——迁移来的旧缓存值优先于
 *          默认值（重建不丢值契约）；位区旧 0 与未初始化同形，会被非零
 *          默认值覆盖（重建=重配置时刻按默认复位，可接受并在此声明）。
 */
static void hubSeedBlockDefaults(XModbusHubCacheBlock* block,
                                 const XModbusMapRange* ranges, int n)
{
    int i;
    if (!block || !ranges) return;
    for (i = 0; i < n; ++i) {
        const bool isBit = hubTypeIsBit(block->type);
        const uint16_t sentinel = isBit ? 0u : 0xFFFFu;
        const uint16_t def = isBit ? (ranges[i].defValue ? 1u : 0u)
                                   : ranges[i].defValue;
        const int64_t off =
            (int64_t)ranges[i].remoteStart - (int64_t)block->remoteStart;
        uint16_t k;
        if (off < 0 || (uint32_t)off + ranges[i].count > block->count) continue;
        if (def == sentinel) continue;      /* 默认值与哨兵同形：无需播种。 */
        for (k = 0; k < ranges[i].count; ++k) {
            if (XModbusDataUnit_value(block->data, (size_t)(off + k)) == sentinel)
                XModbusDataUnit_setValue(block->data, (size_t)(off + k), def);
        }
    }
}

/**
 * @brief 重建某设备的缓存块表（同区同从站远端连续的映射段合并成一块）。
 * @return true=成功（失败保留旧表）。
 */
static bool hubRebuildDeviceBlocks(XModbusHub* hub, XModbusHubDevice* dev)
{
    struct XVector* newBlocks;
    bool ok = true;
    if (!hub || !dev) return false;
    newBlocks = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  sizeof(XModbusHubCacheBlock*), false);
    if (!newBlocks) return false;
    if (hub->m_mapping) {
        for (int t = 0; t < 4 && ok; ++t) {
            const XModbusRegisterType enumType = (XModbusRegisterType)hubEnumOfType(t);
            const size_t total = XModbusMapping_rangeCount_const(hub->m_mapping, enumType);
            XModbusMapRange* arr;
            int got;
            int i;
            if (total == 0) continue;
            arr = (XModbusMapRange*)XMalloc_System(sizeof(XModbusMapRange) * total);
            if (!arr) { ok = false; break; }
            got = (int)XModbusMapping_segmentsForDevice_const(hub->m_mapping, dev->id,
                                                              enumType, arr, (int)total);
            if (got < 0) got = 0;
            /* 插入排序：从站升序、远端起点升序（块/读组有序性的前提）。 */
            for (i = 1; i < got; ++i) {
                const XModbusMapRange key = arr[i];
                int j = i - 1;
                while (j >= 0 && (arr[j].unitId > key.unitId ||
                       (arr[j].unitId == key.unitId && arr[j].remoteStart > key.remoteStart))) {
                    arr[j + 1] = arr[j];
                    --j;
                }
                arr[j + 1] = key;
            }
            i = 0;
            while (i < got && ok) {
                const int unit = arr[i].unitId;
                const uint32_t start = arr[i].remoteStart;
                uint32_t end = start + arr[i].count;
                int j = i + 1;
                /* [从站级约束] rangeBarrier：映射段边界为硬边界——远端连续的
                 * 相邻映射段也**不合并成同一块**，从块层就保住「功能码组」边界
                 * （否则组边界在读帧规划里不可见，见 hubRebuildGroups）。
                 * 约束按**从站**取：同一条 RTU 链路上的其它从站不受影响。 */
                if (!hubUnitHasRangeBarrier(dev, unit)) {
                    while (j < got && arr[j].unitId == unit &&
                           (uint32_t)arr[j].remoteStart == end) {
                        end += arr[j].count;
                        ++j;
                    }
                }
                {
                    XModbusHubCacheBlock* b = hubBlockCreate((uint16_t)enumType, unit,
                                                             (uint16_t)start,
                                                             (uint16_t)(end - start));
                    if (b)
                    if (!b) { ok = false; break; }
                    /* [只写段] 块属性承自映射段：合并块里**全部**段都是只写
                     * 才算只写块（混合块仍要轮询）。只写块不进读组 ⇒ 不轮询：
                     * 厂商从站的写区读了只会回错误帧，白赔一个超时窗口，
                     * 且抬高轮询失败计数触发退避，把写回确认饿死。 */
                    {
                        bool allWriteOnly = true;
                        for (int k = i; k < j; ++k) {
                            if (!(arr[k].flags & (uint8_t)XModbusMapRange_WriteOnly)) {
                                allWriteOnly = false;
                                break;
                            }
                        }
                        b->flags = allWriteOnly ? (uint8_t)XModbusMapRange_WriteOnly : 0;
                    }
                    for (int k = i; k < j; ++k) {
                        XModbusHubCacheSeg seg;
                        seg.virtualStart = arr[k].virtualStart;
                        seg.remoteStart = arr[k].remoteStart;
                        seg.count = arr[k].count;
                        XVector_push_back_1_base(b->segs, &seg);
                    }
                    hubSortSegs(b->segs);
                    hubMigrateBlockState(dev, b);
                    /* [输出上线对齐] 迁移后播种默认值：仅哨兵点入场，
                     * 首启缓存即默认值，重建时旧缓存值优先。 */
                    hubSeedBlockDefaults(b, &arr[i], j - i);
                    XVector_push_back_1_base(newBlocks, &b);
                }
                i = j;
            }
            XFree_System(arr);
        }
    }
    if (!ok) {
        hubBlocksFreeVector(newBlocks);
        return false;
    }
    if (dev->blocks) hubBlocksFreeVector(dev->blocks);
    dev->blocks = newBlocks;
    return true;
}

/** @brief 重建虚拟视图切片表与各类型虚拟跨度（cacheCount 口径）。 */
static void hubRebuildView(XModbusHub* hub)
{
    int t;
    if (!hub) return;
    for (t = 0; t < 4; ++t) {
        size_t i;
        size_t n;
        if (!hub->m_view[t]) {
            hub->m_view[t] = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               sizeof(XModbusHubCacheSlice), false);
        }
        if (!hub->m_view[t]) continue;
        XVector_clear_base(hub->m_view[t]);
        hub->m_spanStart[t] = 0;
        hub->m_spanCount[t] = 0;
        if (hub->m_mapping) {
            const XModbusRegisterType enumType = (XModbusRegisterType)hubEnumOfType(t);
            const size_t rn = XModbusMapping_rangeCount_const(hub->m_mapping, enumType);
            uint32_t minV = 0x10000u;
            uint32_t maxV = 0;
            for (i = 0; i < rn; ++i) {
                const XModbusMapRange* r =
                    XModbusMapping_rangeAt_const(hub->m_mapping, enumType, i);
                if (!r) continue;
                if (r->virtualStart < minV) minV = r->virtualStart;
                if ((uint32_t)r->virtualStart + r->count > maxV)
                    maxV = (uint32_t)r->virtualStart + r->count;
            }
            if (maxV > minV) {
                hub->m_spanStart[t] = (uint16_t)minV;
                hub->m_spanCount[t] = (uint16_t)(maxV - minV);
            }
        }
        /* 切片一律从块内的映射切片生成：视图与块坐标天然一致。 */
        if (hub->m_devices) {
            const size_t dn = XVector_size_base(hub->m_devices);
            for (i = 0; i < dn; ++i) {
                const XModbusHubDevice* dev =
                    (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
                size_t bi;
                if (!dev || !dev->blocks) continue;
                for (bi = 0; bi < XVector_size_base(dev->blocks); ++bi) {
                    const XModbusHubCacheBlock* b =
                        *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)bi);
                    size_t si;
                    if (!b || b->type != (uint16_t)hubEnumOfType(t)) continue;
                    for (si = 0; si < XVector_size_base(b->segs); ++si) {
                        const XModbusHubCacheSeg* seg =
                            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
                        XModbusHubCacheSlice s;
                        if (!seg) continue;
                        s.virtualStart = seg->virtualStart;
                        s.count = seg->count;
                        s.deviceId = dev->id;
                        s.unitId = b->unitId;
                        s.blockOffset = (uint16_t)(seg->remoteStart - b->remoteStart);
                        s.block = (XModbusHubCacheBlock*)b;
                        XVector_push_back_1_base(hub->m_view[t], &s);
                    }
                }
            }
            hubSortView(hub->m_view[t]);
        }
        /* 剩余 n 仅供调试观察。 */
        n = XVector_size_base(hub->m_view[t]);
        (void)n;
    }
}

/**
 * @brief 重建某设备的读组表（合并后的轮询帧计划）。
 * @details 块表已按 (区, 从站, 远端) 有序：同区同从站、远端间隔 ≤
 *          m_readMergeGap 的相邻块并组，组内总长受协议单帧上限约束；
 *          单块超限时拆成多组（同块多帧）。读组合并只影响读——写不跨洞。
 */
static void hubRebuildGroups(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t first = 0;
    size_t total;
    if (!hub || !dev) return;
    if (!dev->groups) {
        dev->groups = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        sizeof(XModbusHubReadGroup), false);
    }
    if (!dev->groups) return;
    XVector_clear_base(dev->groups);
    dev->groupCursor = 0;
    if (!dev->blocks) return;
    total = XVector_size_base(dev->blocks);
    while (first < total) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)first);
        XModbusHubReadGroup g;
        size_t next;
        if (!b) { ++first; continue; }
        /* [只写段不轮询] 块照建（写路径依赖它），但不进读组 ⇒ 不会被轮询。
         * 厂商从站的命令字/密码区读了只会回错误帧。 */
        if (b->flags & (uint8_t)XModbusMapRange_WriteOnly) { ++first; continue; }
        memset(&g, 0, sizeof(g));
        g.type = b->type;
        g.unitId = b->unitId;
        g.remoteStart = b->remoteStart;
        g.count = b->count;
        g.firstBlock = (uint32_t)first;
        g.blockCount = 1;
        /* 单块超单帧上限：同块拆多组（从站级上限优先于协议全局上限）。 */
        if (b->count > hubReadLimitForUnit(dev, g.unitId, b->type)) {
            uint32_t off = 0;
            while (off < b->count) {
                const uint32_t n = (uint32_t)hubReadLimitForUnit(dev, g.unitId, b->type);
                const uint32_t take = (b->count - off > n) ? n : (b->count - off);
                XModbusHubReadGroup part = g;
                part.remoteStart = (uint16_t)(b->remoteStart + off);
                part.count = (uint16_t)take;
                XVector_push_back_1_base(dev->groups, &part);
                off += take;
            }
            ++first;
            continue;
        }
        next = first + 1;
        while (next < total) {
            XModbusHubCacheBlock* nb =
                *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)next);
            uint32_t gEnd;
            uint32_t gap;
            uint32_t newCount;
            if (!nb || nb->type != g.type || nb->unitId != g.unitId) break;
            /* [从站级约束] rangeBarrier：块即映射段，块间不并组——保住
             * 「一帧不跨功能码组」的硬边界（其它从站仍按 gap 并组）。 */
            if (hubUnitHasRangeBarrier(dev, g.unitId)) break;
            gEnd = (uint32_t)g.remoteStart + g.count;
            if (nb->remoteStart < gEnd) break;              /* 重叠不该发生：保守断开。 */
            gap = (uint32_t)nb->remoteStart - gEnd;
            if (gap > (uint32_t)hub->m_readMergeGap) break;
            newCount = (uint32_t)nb->remoteStart + nb->count - g.remoteStart;
            if (newCount > hubReadLimitForUnit(dev, g.unitId, g.type)) break;
            g.count = (uint16_t)newCount;
            ++g.blockCount;
            ++next;
        }
        XVector_push_back_1_base(dev->groups, &g);
        first = next;
    }
}

/** @brief 重建全部：设备块表 → 虚拟视图 → 读组/退避表。 */
static void hubRebuildAll(XModbusHub* hub)
{
    size_t i;
    if (!hub) return;
    if (hub->m_devices) {
        for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) (void)hubRebuildDeviceBlocks(hub, d);
        }
    }
    hubRebuildView(hub);
    if (!hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        hubRebuildGroups(hub, d);
        hubRebuildUnitSched(hub, d);
    }
}

/* ==================== 信号辅助 ==================== */

/**
 * @brief 回填一帧读应答到读组覆盖的各块（按远端交集切分）。
 * @details 变化检测在写入前完成（逐切片比对旧值），发射在写入后——槽内
 *          XModbusHub_value/values 读到的必须是本帧新值（与写路径「乐观
 *          写入块缓存+变化发射」同款契约；先发后写会让上层值通知恒滞后
 *          一拍，2026-11-01 修正）。首次回填（块从未成功读过）**恒发**
 *          一次 valueChanged——否则首采为 0 的点永远收不到初值，违反
 *          「挂接后至少推送一次供播种」的通道契约。
 *          写未确认的点（脏区间内）不被轮询覆写。
 *          切片按批处理（检测→写入→发射）：块内切片互不重叠且铺满块
 *          区间（块=远端连续映射段合并），批间点集不相交，分批与单批
 *          语义一致；栈上标志数组容量见 HUB_FILL_SEG_CHUNK。
 */
#define HUB_FILL_SEG_CHUNK 16
/**
 * @brief 稳定量心跳的发射间隔（毫秒）：本块距上次发射超过它就强制重发。
 * @details [2026-11-05] 不能拿 m_pollIntervalDefaultMs 当基准：那是 Hub 的
 *          **缺省**轮询间隔（1000ms），而实际设备的轮询间隔由 startPolling
 *          显式给出（本服务端 20ms）。用缺省值会算出 4000ms 的心跳周期，
 *          远大于上层（XVfdDevice）5×发布周期=1000ms 的陈旧阈值——心跳还没
 *          来，掩码就被清空了，等于没修。
 *
 *          正确基准取**设备自己的 pollIntervalMs**；夹在 [100, 800] 内：
 *          下限防抖（信号条数不该随高频轮询线性放大），上限必须低于上层
 *          最紧的陈旧阈值（1000ms），留一拍余量。
 */
static uint64_t hubEmitHeartbeatMs(const XModbusHubDevice* dev)
{
    uint64_t base = dev ? (uint64_t)dev->pollIntervalMs : 0;
    if (base == 0) base = 100;
    base *= 4;
    if (base < 100) base = 100;
    if (base > 800) base = 800;
    return base;
}

/**
 * @brief 心跳计时基准：最近一次发射时刻；从未发射过则退回最近一次成功读回。
 * @details lastEmitMs 为 0（本块还没发过任何信号）时若直接判"不心跳"，
 *          稳定块会永远停在 0——必须退回 lastOkMs 让首次心跳能起算。
 */
static uint64_t hubEmitRefMs(const XModbusHubCacheBlock* b)
{
    if (!b) return 0;
    return (b->lastEmitMs != 0) ? b->lastEmitMs : b->lastOkMs;
}

static void hubFillGroupResult(XModbusHub* hub, XModbusHubDevice* dev,
                               const XModbusHubReadGroup* group,
                               const XModbusDataUnit* result)
{
    const uint64_t now = XDateTime_currentMSecsSinceEpoch();
    uint32_t bi;
    if (!hub || !dev || !group || !result || !dev->blocks) return;
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        XModbusHubCacheBlock* b;
        uint32_t lo, hi;
        bool firstFill;
        bool heartbeat;
        bool emitted = false;
        size_t si, base;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
        if (!b || b->type != group->type || b->unitId != group->unitId) continue;
        lo = ((uint32_t)b->remoteStart > group->remoteStart)
                 ? b->remoteStart : group->remoteStart;
        hi = ((uint32_t)b->remoteStart + b->count <
              (uint32_t)group->remoteStart + group->count)
                 ? (uint32_t)b->remoteStart + b->count
                 : (uint32_t)group->remoteStart + group->count;
        if (hi <= lo) continue;
        firstFill = (b->lastOkMs == 0);
        /* [2026-11-05 稳定量心跳] changedOnly 下，值长期不变的块只在
         * firstFill/changed 时发射，上层（设备层按周期判陈旧）就再也收不到
         * 刷新——实测变频器母线电压/运行状态字稳定 ⇒ 遥测掩码首拍后塌成 0 ⇒
         * 健康列表报"从站失联"，而 485 上该从站应答完全正常（49 帧/秒）。
         *
         * 判据：本块距上次发射已超过**轮询间隔的 4 倍**就无条件重发一次
         * （与 XVfdDevice 的陈旧阈值 5× 周期同量级但更早，留一拍余量）。
         * 只影响 changedOnly 路径的发射，值写入与脏区间语义不变；缓存
         * 完全相同的情况下多几条信号，代价可忽略（上限 = 1/4 轮询带宽的
         * 信号条数，且不产生任何总线帧）。 */
        heartbeat = (hub->m_changedOnlySignals && !firstFill &&
                     now > hubEmitRefMs(b) &&
                     (now - hubEmitRefMs(b)) > hubEmitHeartbeatMs(dev));

        for (base = 0; base < XVector_size_base(b->segs);
             base += HUB_FILL_SEG_CHUNK) {
            uint8_t fire[HUB_FILL_SEG_CHUNK];
            size_t chunkEnd = base + HUB_FILL_SEG_CHUNK;
            if (chunkEnd > XVector_size_base(b->segs))
                chunkEnd = XVector_size_base(b->segs);

            /* ① 变化检测（按块内映射切片；脏点跳过；写入前比对旧值）。 */
            for (si = base; si < chunkEnd; ++si) {
                const XModbusHubCacheSeg* seg =
                    (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
                uint32_t sStart, sEnd, cLo, cHi;
                bool changed = false;
                if (!seg) { fire[si - base] = 0; continue; }
                sStart = seg->remoteStart;
                sEnd = sStart + seg->count;
                cLo = (sStart > lo) ? sStart : lo;
                cHi = (sEnd < hi) ? sEnd : hi;
                if (cHi <= cLo) { fire[si - base] = 0; continue; }
                for (uint32_t r = cLo; r < cHi; ++r) {
                    const uint16_t off = (uint16_t)(r - b->remoteStart);
                    const uint16_t newVal =
                        XModbusDataUnit_value(result, (size_t)(r - group->remoteStart));
                    if (hubDirtyHasOffset(b, off)) continue;
                    if (newVal != XModbusDataUnit_value(b->data, off)) {
                        changed = true;
                        break;
                    }
                }
                fire[si - base] = (uint8_t)(changed || firstFill || heartbeat);
            }
            /* ② 写入本批切片覆盖的点（脏点保留写意图值）。 */
            for (si = base; si < chunkEnd; ++si) {
                const XModbusHubCacheSeg* seg =
                    (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
                uint32_t sStart, sEnd, cLo, cHi;
                if (!seg) continue;
                sStart = seg->remoteStart;
                sEnd = sStart + seg->count;
                cLo = (sStart > lo) ? sStart : lo;
                cHi = (sEnd < hi) ? sEnd : hi;
                if (cHi <= cLo) continue;
                for (uint32_t r = cLo; r < cHi; ++r) {
                    const uint16_t off = (uint16_t)(r - b->remoteStart);
                    const uint16_t newVal =
                        XModbusDataUnit_value(result, (size_t)(r - group->remoteStart));
                    if (hubDirtyHasOffset(b, off)) continue;
                    if (newVal != XModbusDataUnit_value(b->data, off))
                        XModbusDataUnit_setValue(b->data, off, newVal);
                }
            }
            /* ③ 发射（此刻缓存已是本帧新值）；changedOnly=false 时每次
             *    回填都发（与写路径同款契约，供上层周期刷新/播种）。 */
            for (si = base; si < chunkEnd; ++si) {
                const XModbusHubCacheSeg* seg =
                    (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
                uint32_t sStart, sEnd, cLo, cHi;
                if (!seg) continue;
                if (!fire[si - base] && hub->m_changedOnlySignals) continue;
                sStart = seg->remoteStart;
                sEnd = sStart + seg->count;
                cLo = (sStart > lo) ? sStart : lo;
                cHi = (sEnd < hi) ? sEnd : hi;
                if (cHi <= cLo) continue;
                XModbusHub_valueChanged_signal(
                    hub, (XModbusRegisterType)b->type,
                    (int)(seg->virtualStart + (cLo - sStart)), (int)(cHi - cLo));
                emitted = true;
            }
        }
        b->lastOkMs = now;
        /* 心跳判据的基准：只有真发过才更新，否则稳定块永远拿不到心跳。 */
        if (emitted) b->lastEmitMs = now;
        b->failCount = 0;
        b->refreshRequested = false;
    }
}

/**
 * @brief 脏区间块内偏移 → 统一空间虚拟起始地址（按块段表反查）。
 * @details writeStalled/writeCompleted 的契约参数是 virtualStart；失败沿
 *          发射点只有块内偏移，必须经段表翻译——远端≠虚拟的映射下直发
 *          远端地址会让上层把故障挂到错误的点位上。段表未命中属不变量
 *          破坏（脏区间恒在块内、块恒有段覆盖），退化为首段虚拟起点。
 */
static int hubDirtyVirtualStart(const XModbusHubCacheBlock* b,
                                const XModbusHubDirtyRange* d)
{
    const uint32_t r = (uint32_t)b->remoteStart + d->offset;
    size_t si;
    for (si = 0; si < XVector_size_base(b->segs); ++si) {
        const XModbusHubCacheSeg* seg =
            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, (int64_t)si);
        if (seg && r >= seg->remoteStart &&
            r < (uint32_t)seg->remoteStart + seg->count)
            return (int)(seg->virtualStart + (r - seg->remoteStart));
    }
    if (XVector_size_base(b->segs) > 0) {
        const XModbusHubCacheSeg* seg0 =
            (const XModbusHubCacheSeg*)XVector_at_base(b->segs, 0);
        if (seg0) return (int)seg0->virtualStart;
    }
    return 0;
}

/** @brief 按视图切片逐段发 writeCompleted（上层看到的粒度=映射段）。 */
static void hubEmitWriteDone(XModbusHub* hub, uint16_t type,
                             uint16_t virtualStart, uint16_t count, bool ok)
{
    const int t = hubTypeIndex((int)type);
    const uint32_t end = (uint32_t)virtualStart + count;
    bool emitted = false;
    size_t i;
    if (!hub || t < 0 || !hub->m_view[t] || count == 0) return;
    for (i = 0; i < XVector_size_base(hub->m_view[t]); ++i) {
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)i);
        uint32_t lo, hi;
        if (!s) continue;
        lo = ((uint32_t)s->virtualStart > virtualStart) ? s->virtualStart : virtualStart;
        hi = ((uint32_t)s->virtualStart + s->count < end)
                 ? (uint32_t)s->virtualStart + s->count : end;
        if (hi <= lo) continue;
        XModbusHub_writeCompleted_signal(hub, (XModbusRegisterType)type, (int)lo, (int)(hi - lo), ok);
        emitted = true;
    }
    if (!emitted)
        XModbusHub_writeCompleted_signal(hub, (XModbusRegisterType)type, (int)virtualStart,
                                         (int)count, ok);
}

/* ==================== 点位翻译 ==================== */

/** @brief 虚拟地址 → (设备槽下标, 块, 块内偏移)；命中返回 true。 */
static bool hubTranslateVirtual(const XModbusHub* hub, XModbusRegisterType type,
                                uint16_t virtualAddr, int* outDevIndex,
                                XModbusHubCacheBlock** outBlock,
                                uint16_t* outBlockOffset)
{
    const int t = hubTypeIndex((int)type);
    const int vi = (t < 0 || !hub->m_view[t]) ? -1 : hubViewFind(hub->m_view[t], virtualAddr);
    const XModbusHubCacheSlice* s;
    int di;
    if (vi < 0) return false;
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    di = hubDeviceIndexById(hub, s->deviceId);
    if (di < 0) return false;
    if (outDevIndex) *outDevIndex = di;
    if (outBlock) *outBlock = s->block;
    if (outBlockOffset) *outBlockOffset = (uint16_t)(s->blockOffset +
                                                     (virtualAddr - s->virtualStart));
    return true;
}

/* ==================== 发送（轮询/写） ==================== */

/**
 * @brief [2026-10-28 单元退避] 从站是否静默中（退避表查询）。
 * @return 静默返回 true；未登记返回 false（不拦截）。
 */
static bool hubUnitMuted(XModbusHubDevice* dev, int unitId, uint64_t nowMs)
{
    if (!dev || unitId < 0) return false;
    return XPollScheduler_isMutedKey(&dev->unitSched, unitId, nowMs);
}

/**
 * @brief [2026-10-28 单元退避] 块表重建后同步设备退避表：清表重建，按
 *        块内 unitId 去重登记簿记条目（阈值/时长沿用 Hub 配置）。
 */
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t i;
    size_t n;
    if (!hub || !dev) return;
    XPollScheduler_deinit(&dev->unitSched);
    XPollScheduler_init(&dev->unitSched);
    XPollScheduler_setBackoff(&dev->unitSched, hub->m_unitMaxFails, hub->m_unitMuteMs);
    if (!dev->blocks) return;
    n = XVector_size_base(dev->blocks);
    for (i = 0; i < n; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)i);
        if (b) (void)XPollScheduler_registerKey(&dev->unitSched, b->unitId, 0);
    }
}

/** @brief 读组是否该发：区域在轮询掩码内，或有块被 requestRefresh 标脏。 */
static bool hubGroupReadable(const XModbusHub* hub, const XModbusHubDevice* dev,
                             const XModbusHubReadGroup* group)
{
    uint32_t bi;
    if (!hub || !dev || !group) return false;
    if (hub->m_pollAreas & (1u << group->type)) return true;
    if (!dev->blocks) return false;
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        const XModbusHubCacheBlock* b;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock* const*)XVector_at_base(dev->blocks, (int64_t)bi);
        if (b && b->refreshRequested) return true;
    }
    return false;
}

/**
 * @brief 发一个读组。请求单元为栈上临时对象——sendReadRequest 经
 *        createReply 深拷贝收编，返回后即可析构。
 * @return true=已上线（应答异步回 onReplyFinished）；false=未受理。
 */
static bool hubSendPollGroup(XModbusHub* hub, XModbusHubDevice* dev,
                             const XModbusHubReadGroup* group)
{
    XModbusDataUnit unit;
    XModbusReply* reply;
    uint32_t bi;
    if (!hub || !dev || !dev->client || !group || group->count == 0) return false;
    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)group->type,
                            group->remoteStart, group->count);
    /* [诊断 2026-11-10] 进入发送点（区分"没进来"与"进来后卡住"）。 */
    {
        const char* env = getenv("XMODBUS_HUB_TRACE");
        if (env && env[0] && env[0] != '0' && hubTraceAllow(hub, 8)) {
            XPrintf("[hub] dev=%d sendReadRequest -> unit=%d client=%p\n",
                    dev->id, (int)group->unitId, (void*)dev->client);
        }
    }
    reply = XModbusClient_sendReadRequest(dev->client, &unit, group->unitId,
                                          XModbusRequestPriority_Normal);
    XClassDeinit(&unit);
    /* [诊断 2026-11-10 正式诊断手段] 读组发出后的**受理结果**一行：
     * 请求由客户端受理了吗？受理的是哪一支（Reply 指针/终态/错误码）？
     * 现场"某站全哑"时，这一行是分水岭——
     *   · 没这行 ⇒ 请求压根没进客户端（Hub 侧的问题）；
     *   · 有这行且 finished=0 ⇒ 客户端收下了但**从站不应答**（真机
     *     M512 对 0xFA00×5 无应答）⇒ 必须在途超时收口，否则整站被拖死。
     * 用法：XMODBUS_HUB_TRACE=1（每设备限 8 条，避免刷屏）。 */
    {
        const char* env = getenv("XMODBUS_HUB_TRACE");
        if (env && env[0] && env[0] != '0' && hubTraceAllow(hub, 8)) {
            XPrintf("[hub] dev=%d unit=%d start=%u count=%u -> client=%p "
                    "reply=%p finished=%d err=%d\n",
                    dev->id, (int)group->unitId,
                    (unsigned)group->remoteStart, (unsigned)group->count,
                    (void*)dev->client, (void*)reply,
                    reply ? (XModbusReply_isFinished(reply) ? 1 : 0) : -1,
                    reply ? (int)XModbusReply_error(reply) : -1);
        }
    }
    if (!reply) {
        /* [诊断 2026-11-10] 组被选中却发不出去（客户端拒收）——限频打印，
         * 否则现场只看到"某站全哑"却查不到是谁拒收的。 */
        static int s_noReply = 0;
        if (s_noReply < 5) {
            ++s_noReply;
            XPrintf("XModbusHub: 读组发不出去（客户端拒收）unit=%d 起始=%u 数量=%u\n",
                    (int)group->unitId,
                    (unsigned)group->remoteStart, (unsigned)group->count);
        }
        return false;
    }
    /* 刷新标记在发送时清除（一次尝试即算响应请求；失败由退避/重试兜底）。 */
    for (bi = group->firstBlock; bi < group->firstBlock + group->blockCount; ++bi) {
        XModbusHubCacheBlock* b;
        if (bi >= XVector_size_base(dev->blocks)) break;
        b = *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
        if (b) b->refreshRequested = false;
    }

    XObject_setParent((XObject*)reply, (XObject*)hub);
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Poll;
    dev->inflightStartMs = XDateTime_currentMSecsSinceEpoch();
    dev->inflightPayload.group = *group;
    /* [A3 时序缺口兜底] reply 可能在 send 内部即同步终态（未连接/io 关闭/
     * 忙/空 PDU/发送失败等收口分支，setError 同步发射 finished）——此刻
     * finished 已发射给空监听集，connect_2 再挂也收不到。若不就地收口，
     * dev->inflight 永卡：状态变化槽不清 inflight、重连续泵被单飞闸挡死
     * →该设备轮询永久停摆。先登记再直调收口槽＝与异步收口完全等价的路径
     * （退避记账/回填/统计/续泵全不走样）。递归有界：首个同步写/发失败已
     * 把设备拉成 Unconnected，收口尾部续泵会被 hubPumpDevice 的连接态
     * 门槛挡住。 */
    if (XModbusReply_isFinished(reply)) {
        /* [嵌套不再丢帧 2026-11-09 框架级根修]
         *
         * 原实现：嵌套层（收口尾部续泵又发一帧、同步终态再次就地收口）
         * **只清 inflight + 释放 reply**，那一帧的结果整包消失——
         *   · 不回填缓存 ⇒ b->lastOkMs 不推进 ⇒ 点级新鲜度判陈旧 ⇒
         *     上层显示"无数据"；
         *   · 不记账 ⇒ pollOkCount/pollErrCount/从站退避簿记全缺
         *     ⇒ 死从站永远不升级静默、活从站被它占住单飞槽；
         *   · 不发沿 ⇒ unitLost/unitAlive/valueChanged 全缺。
         * 而外层那一帧往往是**写收口**，内层被丢的正是**读帧** ⇒ 现场
         * 表现为"某台从站的数据再也不刷新"，看门狗却判健康。
         *
         * 现在：嵌套层**完整收口**（回填+记账+沿），只是**不再泵**
         * （深度上界 HUB_SYNC_FINISH_MAX_DEPTH），杜绝栈递归与总线风暴。 */
        if (hub->m_syncFinishDepth < HUB_SYNC_FINISH_MAX_DEPTH) {
            ++hub->m_syncFinishDepth;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            --hub->m_syncFinishDepth;
        } else {
            /* 深度超限：只做必要的清理（不再回填，也不泵）。 */
            dev->inflight = NULL;
            dev->inflightKind = XModbusHubInflight_None;
            XObject_deleteLater((XObject*)reply);
        }
        return true;
    }
    XObject_connect_2((XObject*)reply,
                      XSignal(XModbusReply_finished_signal),
                      XModbusHub_onReplyFinished);
    return true;
}

/** @brief 发一条脏区间（FC15/FC16；单帧按协议上限切分）。 */
static bool hubSendDirtyRange(XModbusHub* hub, XModbusHubDevice* dev,
                              XModbusHubCacheBlock* block,
                              const XModbusHubDirtyRange* dirty)
{
    XModbusDataUnit unit;
    XModbusReply* reply;
    uint16_t limit;
    uint16_t n;
    uint16_t virtualStart = 0;
    size_t si;
    if (!hub || !dev || !dev->client || !block || !dirty || dirty->count == 0) return false;
    limit = hubWriteLimitOfType(block->type);
    n = (dirty->count > limit) ? limit : dirty->count;
    /* [从站级约束] singleWriteOnly：只支持 FC06 的从站拒绝 FC16/FC15。
     * 脏区间仍可因「相邻写合并」变成多字，但下发时逐字切（n=1）：首字
     * 成功后 hubDirtySubtract 只摘掉首字，剩余继续排队——每个虚拟地址
     * 仍各收一次 writeDone(ok)，通道层逐点落定语义不变。
     * 约束按**从站**取：同一条 RTU 链路上的其它从站仍可走 FC16。 */
    if (hubUnitSingleWriteOnly(dev, block->unitId) && n > 1) n = 1;

    /* 统一空间起点：块内远端 → 切片 → 虚拟地址。 */
    for (si = 0; si < XVector_size_base(block->segs); ++si) {
        const XModbusHubCacheSeg* seg =
            (const XModbusHubCacheSeg*)XVector_at_base(block->segs, (int64_t)si);
        const uint32_t r = (uint32_t)block->remoteStart + dirty->offset;
        if (!seg) continue;
        if (r >= seg->remoteStart && r < (uint32_t)seg->remoteStart + seg->count) {
            virtualStart = (uint16_t)(seg->virtualStart + (r - seg->remoteStart));
            break;
        }
    }

    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)block->type,
                            (uint16_t)(block->remoteStart + dirty->offset), n);
    for (uint16_t k = 0; k < n; ++k)
        XModbusDataUnit_setValue(&unit, k,
                                 XModbusDataUnit_value(block->data, dirty->offset + k));
    /* [2026-10-28 写优先级] 写走 High 队列在传输层插队（轮询读保持 Normal）；
     * 即时下发与滞留重发共用本入口，一处收口。 */
        reply = XModbusClient_sendWriteRequest(dev->client, &unit, block->unitId,
                                           XModbusRequestPriority_High);
        XClassDeinit(&unit);
    if (!reply) return false;

    XObject_setParent((XObject*)reply, (XObject*)hub);
    /* 单飞登记：快照意图（脏区间合并/重建不影响收口）。 */
    if (dev->inflightPayload.write.values) {
        XFree_System(dev->inflightPayload.write.values);
        dev->inflightPayload.write.values = NULL;
    }
    memset(&dev->inflightPayload.write, 0, sizeof(dev->inflightPayload.write));
    dev->inflightPayload.write.type = block->type;
    dev->inflightPayload.write.unitId = block->unitId;
    dev->inflightPayload.write.remoteStart = (uint16_t)(block->remoteStart + dirty->offset);
    dev->inflightPayload.write.count = n;
    dev->inflightPayload.write.virtualStart = virtualStart;
    dev->inflightPayload.write.virtualCount = n;
    dev->inflightPayload.write.attempts = dirty->attempts;
    dev->inflightPayload.write.values = (uint16_t*)XMalloc_System(sizeof(uint16_t) * n);
    if (dev->inflightPayload.write.values) {
        for (uint16_t k = 0; k < n; ++k)
            dev->inflightPayload.write.values[k] =
                XModbusDataUnit_value(block->data, dirty->offset + k);
    }
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Write;
    dev->inflightStartMs = XDateTime_currentMSecsSinceEpoch();
    /* [A3 时序缺口兜底] 同上：send 内部同步终态就地带 inflight 登记直调
     * 收口槽（脏区间留表 + 续泵等价），防 dev->inflight 永卡单飞。 */
    if (XModbusReply_isFinished(reply)) {
        /* [嵌套不再丢帧 2026-11-09 框架级根修]
         *
         * 原实现：嵌套层（收口尾部续泵又发一帧、同步终态再次就地收口）
         * **只清 inflight + 释放 reply**，那一帧的结果整包消失——
         *   · 不回填缓存 ⇒ b->lastOkMs 不推进 ⇒ 点级新鲜度判陈旧 ⇒
         *     上层显示"无数据"；
         *   · 不记账 ⇒ pollOkCount/pollErrCount/从站退避簿记全缺
         *     ⇒ 死从站永远不升级静默、活从站被它占住单飞槽；
         *   · 不发沿 ⇒ unitLost/unitAlive/valueChanged 全缺。
         * 而外层那一帧往往是**写收口**，内层被丢的正是**读帧** ⇒ 现场
         * 表现为"某台从站的数据再也不刷新"，看门狗却判健康。
         *
         * 现在：嵌套层**完整收口**（回填+记账+沿），只是**不再泵**
         * （深度上界 HUB_SYNC_FINISH_MAX_DEPTH），杜绝栈递归与总线风暴。 */
        if (hub->m_syncFinishDepth < HUB_SYNC_FINISH_MAX_DEPTH) {
            ++hub->m_syncFinishDepth;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            --hub->m_syncFinishDepth;
        } else {
            /* 深度超限：只做必要的清理（不再回填，也不泵）。 */
            dev->inflight = NULL;
            dev->inflightKind = XModbusHubInflight_None;
            XObject_deleteLater((XObject*)reply);
        }
        return true;
    }
    XObject_connect_2((XObject*)reply,
                      XSignal(XModbusReply_finished_signal),
                      XModbusHub_onReplyFinished);
    return true;
}

/**
 * @brief 取该设备首条脏区间并下发（一次一帧，单飞）。
 * @return true=已发（或同步终态已收口）；false=无可发。
 */
static bool hubSendNextWrite(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t i;
    if (!hub || !dev || !dev->blocks) return false;
    for (i = 0; i < XVector_size_base(dev->blocks); ++i) {
        XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)i);
        XModbusHubDirtyRange* d;
        if (!b || !b->dirty || XVector_size_base(b->dirty) == 0) continue;
        d = (XModbusHubDirtyRange*)XVector_at_base(b->dirty, 0);
        if (!d) continue;
                ++d->attempts;                                  /* 下发次数（失败重发累计）。 */
        if (hub->m_writeStallNoticeAt >= 1 &&
            d->attempts >= (uint32_t)hub->m_writeStallNoticeAt) {
            const int virtualStart = hubDirtyVirtualStart(b, d);
            if (!d->stallNotified) {
                d->stallNotified = true;
                XModbusHub_writeStalled_signal(hub, (XModbusRegisterType)b->type,
                                               virtualStart,
                                               (int)d->count, (int)d->attempts);
            }
            if (!d->failNotified) {
                d->failNotified = true;
                /* [2026-10-29 失败沿] 写失败对上层可见：通道层
                 * XDigitalOutput_writeDone(false) 的故障判据。仍继续重发。 */
                hubEmitWriteDone(hub, b->type, (uint16_t)virtualStart,
                                 d->count, false);
            }
        }
        return hubSendDirtyRange(hub, dev, b, d);
    }
    return false;
}

/**
 * @brief 该设备是否有陈旧块（块级 lastOkMs 太久没回填）。
 * @param staleMs 判定阈值（毫秒）。
 * @details [为什么按块] 设备级"最后成功应答"会被同一 Hub 上其它从站掩盖：
 *          485 一条线挂变频器+温湿度，变频器一成功，设备级计时器就一直
 *          新鲜 ⇒ 温湿度那台不刷新也看不出来（现场实证：温湿度恒 "--"，
 *          看门狗判健康、重启才恢复）。块级 lastOkMs 才是"这个点最后
 *          一次真正被回填"的时刻。
 */
static bool hubDeviceHasStaleBlock(const XModbusHubDevice* dev, uint64_t nowMs,
                                   uint64_t staleMs)
{
    size_t bn;
    size_t i;
    if (!dev || !dev->blocks) return false;
    bn = XVector_size_base((const XContainer*)dev->blocks);
    for (i = 0; i < bn; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(
                (const XContainer*)dev->blocks, (int64_t)i);
        if (!b) continue;
        if (b->flags & XModbusMapRange_WriteOnly) continue;  /* 只写块不轮询 */
        if (b->lastOkMs == 0) return true;                   /* 从未回填过 */
        if (nowMs > b->lastOkMs && nowMs - b->lastOkMs > staleMs) return true;
    }
    return false;
}

/**
 * @brief 轮询停滞诊断：打印设备状态、**陈旧的块**与读组表的跳过原因。
 * @details [为什么按块看] 设备级"最后成功应答"会把一台 Hub 上的其它从站
 *          掩盖掉：485 一条线上挂着变频器+温湿度两台，变频器一成功，设备
 *          级计时器就一直新鲜 ⇒ 温湿度那台从站不刷新也看不出来（现场实证：
 *          温湿度恒为"--"，看门狗判健康、重启才恢复）。块级 lastOkMs 才是
 *          "这个点最后一次真正被回填"的时刻 ⇒ 按块定位才对得上现场。
 *
 *          读组被三道闸门静默跳过时（从站静默 / 分级节拍未到期 / 区域不在
 *          轮询掩码），既不记失败也不打日志 ⇒ 现场只看到"没数据"，无从
 *          判断是"读失败"还是"压根没读"。这里把跳过原因逐组列出。
 */
static void hubDumpStallDiag(const XModbusHub* hub, XModbusHubDevice* dev,
                              uint64_t nowMs)
{
    size_t gn = 0;
    size_t i;
    size_t bn = 0;
    if (!hub || !dev) return;
    if (dev->groups)
        gn = XVector_size_base((const XContainer*)dev->groups);
    if (dev->blocks)
        bn = XVector_size_base((const XContainer*)dev->blocks);
    XPrintf("XModbusHub: 设备 %d 轮询停滞（成功=%u 失败=%u 组数=%u 游标=%u "
            "轮询区掩码=0x%X）\n",
            dev->id,
            (unsigned)dev->pollOkCount, (unsigned)dev->pollErrCount,
            (unsigned)gn, (unsigned)dev->groupCursor,
            (unsigned)hub->m_pollAreas);
    /* ---- 陈旧块（最后一次真正回填太久之前）---- */
    for (i = 0; i < bn; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(
                (const XContainer*)dev->blocks, (int64_t)i);
        uint64_t age;
        if (!b) continue;
        if (b->flags & XModbusMapRange_WriteOnly) continue;  /* 只写块不参与轮询 */
        age = (b->lastOkMs == 0) ? 0 : (nowMs - b->lastOkMs);
        if (b->lastOkMs != 0 && age <= 3000u) continue;      /* 还新鲜 */
        XPrintf("    陈旧块[%u] unit=%d 类型=%d 起始=%u 数量=%u "
                "最后回填=%s%llu ms 失败计数=%u\n",
                (unsigned)i, (int)b->unitId, (int)b->type,
                (unsigned)b->remoteStart, (unsigned)b->count,
                (b->lastOkMs == 0) ? "从未 " : "",
                (unsigned long long)age, (unsigned)b->failCount);
    }
    /* ---- 读组表与每组的跳过原因 ---- */
    for (i = 0; i < gn; ++i) {
        const XModbusHubReadGroup* g =
            (const XModbusHubReadGroup*)XVector_at_base(
                (const XContainer*)dev->groups, (int64_t)i);
        if (!g) continue;
        XPrintf("    组[%u] unit=%d 类型=%d 起始=%u 数量=%u %s%s%s\n",
                (unsigned)i, g->unitId, (int)g->type,
                (unsigned)g->remoteStart, (unsigned)g->count,
                hubUnitMuted(dev, g->unitId, nowMs) ? "[静默] " : "",
                hubUnitDue(dev, g->unitId, nowMs) ? "" : "[未到期] ",
                hubGroupReadable(hub, dev, g) ? "" : "[不在掩码]",
                /* [静默期闸 2026-11-10] 诊断补标：这一闸用**设备级**
                 * lastTxEndMs 记账，而另两闸是从站级 ⇒ 同总线上另一站每
                 * 500ms 一拍就能把设备级收口时刻刷掉，要求 200ms 静默的站
                 * 永远等不到窗口（现场：变频器整站哑掉、失败计数=0）。 */
                (hubUnitPreTxQuietMs(dev, g->unitId) != 0 &&
                 dev->lastTxEndMs != 0 && nowMs >= dev->lastTxEndMs &&
                 (nowMs - dev->lastTxEndMs) <
                     (uint64_t)hubUnitPreTxQuietMs(dev, g->unitId))
                    ? "[静默期闸] " : "");
    }
}

/**
 * @brief 在途读组的收口时限（毫秒）。
 * @details [2026-11-10 框架级根修] 取值顺序：
 *          ① Hub 显式配置（XModbusHub_setInflightTimeoutMs）；
 *          ② 客户端超时（XModbusClient_timeout）；
 *          ③ **缺省兜底** XMODBUS_HUB_DEFAULT_INFLIGHT_TIMEOUT_MS。
 *          缺省兜底是必需的：注入的虚拟客户端可能把超时配成 0，此时若只
 *          依赖客户端超时，在途标记就**永远没有收口点** ⇒ 单飞闸把整台
 *          设备堵死（现场：变频器整站离线、失败计数=0、游标卡住）。
 *          另加宽限余量（XMODBUS_HUB_INFLIGHT_TIMEOUT_GRACE_MS）：Hub 的
 *          收口必须晚于客户端自己的超时，否则 Hub 会删掉客户端仍持有的
 *          Reply ⇒ 悬垂指针。
 */
static uint64_t hubInflightTimeoutMs(const XModbusHub* hub,
                                      const XModbusHubDevice* dev)
{
    uint64_t ms = 0;
    if (hub && hub->m_inflightTimeoutMs > 0) ms = (uint64_t)hub->m_inflightTimeoutMs;
    if (ms == 0 && dev && dev->client)
        ms = (uint64_t)XModbusClient_timeout(dev->client);
    if (ms == 0) ms = (uint64_t)XMODBUS_HUB_DEFAULT_INFLIGHT_TIMEOUT_MS;
    return ms + (uint64_t)XMODBUS_HUB_INFLIGHT_TIMEOUT_GRACE_MS;
}

/**
 * @brief 在途事务超时收口：判失败 + 记账 + 释放标记 + 继续下一组。
 * @details [2026-11-10 框架级根修] 从站对某一组**不应答**时（真机 M512
 *          对 0xFA00×5 无应答；模拟器越界地址亦然），在途标记会一直挂着
 *          ⇒ 单飞闸（`if (dev->inflight) return;`）把该设备后续**所有**
 *          读组永久堵死：现场表现"变频器整站离线、失败计数=0、游标卡在 1、
 *          其余组从不再发"，而同一总线上的别的站一切正常。
 *          收口动作（与"收到失败应答"的路径同口径）：
 *            · 记失败计数（pollErrCount / 块级 failCount）；
 *            · 报从站退避（XPollScheduler_reportResultKey(false)）——
 *              真不答应的从站该退避，不该被无限重试打爆总线；
 *            · 清 inflight / inflightKind / inflightStartMs ⇒ 下一组可发；
 *            · 释放 Reply：**先断开 Hub 与该 Reply 的 finished 连接**，
 *              免得迟到的应答再进收口槽二次释放（双删）。
 */
static void hubCloseTimedOutInflight(XModbusHub* hub, XModbusHubDevice* dev,
                                      uint64_t nowMs, uint64_t timeoutMs)
{
    XModbusReply* reply;
    const uint8_t kind = dev->inflightKind;
    /* [联合体按类别读] inflightPayload 是「读组 / 写快照」的联合体 ⇒
     * 只能按 inflightKind 取对应成员（读组有 firstBlock，写快照没有）。 */
    const XModbusHubReadGroup group = (kind == XModbusHubInflight_Write)
                                          ? (XModbusHubReadGroup){0}
                                          : dev->inflightPayload.group;
    const int unitId = (kind == XModbusHubInflight_Write)
                           ? dev->inflightPayload.write.unitId
                           : group.unitId;
    const uint16_t remoteStart = (kind == XModbusHubInflight_Write)
                                     ? dev->inflightPayload.write.remoteStart
                                     : group.remoteStart;
    const uint16_t count = (kind == XModbusHubInflight_Write)
                               ? dev->inflightPayload.write.count
                               : group.count;
    if (!hub || !dev) return;
    reply = dev->inflight;
    {
        /* 限频打印（每设备 1s 一条）：否则现场只看到"某站全哑"。 */
        const bool show = (dev->lastErrLogMs == 0 ||
                           nowMs - dev->lastErrLogMs >= 1000u);
        if (show) {
            dev->lastErrLogMs = nowMs;
            XPrintf("XModbusHub: 设备 %d 在途超时收口（%s unit=%d 起始=%u "
                    "数量=%u，超过 %llu ms 无应答）——判失败并继续下一组\n",
                    dev->id, (kind == XModbusHubInflight_Write) ? "写" : "读",
                    unitId,
                    (unsigned)remoteStart,
                    (unsigned)count,
                    (unsigned long long)timeoutMs);
        }
    }
    /* ① 断链：迟到的应答不再进本 Hub 的收口槽（否则二次 deleteLater ⇒ 双删）。
     *    [receiver 必须传 NULL] Hub 侧是用 XObject_connect_2 挂的（**无接收
     *    者** ⇒ 连接记录的 receiver 字段是 NULL），而 XConnection_equal 的
     *    匹配规则是「receiver 非空才比对」⇒ 传 hub 会匹配不上、断链失败。 */
    if (reply) {
        (void)XObject_disconnect_1((XObject*)reply,
                                    XSignal(XModbusReply_finished_signal),
                                    NULL, XModbusHub_onReplyFinished);
        /* ①b 传输层摘手（**必须先于释放 Reply**）：RTU/TCP 传输层在等待
         *     应答期间都持有这个 Reply（RTU=m_currentReply、TCP=在途表），
         *     Hub 若直接释放它，迟到的应答/超时回调就会解引用已释放对象
         *     ⇒ UAF ⇒ 实测板端 SEGV（浊度站1 在站5 长帧恢复期内不应答
         *     ⇒ 收口释放 Reply ⇒ 串口超时再碰它 ⇒ 崩）。
         *     XModbusClient_abandonReply 只解除「传输层还持有它」，不碰
         *     Reply 本身（所有权仍归 Hub）。 */
        (void)XModbusClient_abandonReply(dev->client, reply);
    }
    /* ② 记账：按"失败应答"同口径（失败计数 + 从站退避 + 块级失败计数）。 */
    dev->lastTxOk = false;
    if (kind == XModbusHubInflight_Poll) {
        const bool wasLost = (unitId >= 0) &&
            XPollScheduler_isLostKey(&dev->unitSched, unitId);
        const bool enteredMute = (unitId >= 0) &&
            XPollScheduler_reportResultKey(&dev->unitSched, unitId, false, nowMs);
        ++dev->pollErrCount;
        if (group.firstBlock < XVector_size_base(dev->blocks)) {
            XModbusHubCacheBlock* b =
                *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks,
                                                         (int64_t)group.firstBlock);
            if (b) ++b->failCount;
        }
        if (enteredMute && !wasLost)
            XModbusHub_unitLost_signal(hub, dev->id, unitId);
    } else if (kind == XModbusHubInflight_Write) {
        /* 写超时：脏区间原样保留（写重发拍会重试），快照内存在此释放。 */
        if (dev->inflightPayload.write.values) {
            XFree_System(dev->inflightPayload.write.values);
            dev->inflightPayload.write.values = NULL;
        }
        memset(&dev->inflightPayload.write, 0, sizeof(dev->inflightPayload.write));
    }
    /* ③ 释放标记 ⇒ 单飞闸放行，下一拍继续下一个读组。 */
    dev->inflight = NULL;
    dev->inflightKind = XModbusHubInflight_None;
    dev->inflightStartMs = 0;
    if (reply) XObject_deleteLater((XObject*)reply);
    XModbusHub_deviceStatsChanged_signal(hub, dev->id);
}

/**
 * @brief 统一泵：设备空闲（单飞未占用且在线）时——先写后轮询（写延迟优先）。
 *        轮询拍/重连恢复/写入队/重发拍/应答收口共用此入口。
 * @param allowPoll false=**只续写、不发读**（应答收口尾部用）。
 *        [为什么收口路径不许发读 2026-11-09] 2026-10-26 那次"轮询风暴"根修
 *        的注释说"收口只补写、不续轮询"，但代码调的是写+轮询的统一泵 ⇒
 *        只要写分支被跳过（合并窗口未开/失败期让路），收口路径就会**再发
 *        一帧读**；异步客户端上这是时间轴上无界的读帧链（2ms/请求、tid
 *        数千的风暴形态），同步客户端上则是栈递归。现按参数分开。 */
static void hubPumpDeviceEx(XModbusHub* hub, XModbusHubDevice* dev,
                            bool allowPoll)
{
    if (!hub || !dev || !dev->client) return;
    /* [诊断 2026-11-10] 泵入口快照：这一拍为什么没发出去？把三个可能一次性
     * 摊开——① 在途标记还挂着（单飞闸）② 在途时刻戳（超时收口的判据）
     * ③ 设备在线态。现场"游标卡住"时，先看这一行再决定往哪查。 */
    {
        const char* env = getenv("XMODBUS_HUB_TRACE");
        if (env && env[0] && env[0] != '0' && hubTraceAllow(hub, 8)) {
            XPrintf("[hub] dev=%d pump state=%d inflight=%p kind=%u "
                    "startMs=%llu cursor=%u\n",
                    dev->id, (int)dev->state, (void*)dev->inflight,
                    (unsigned)dev->inflightKind,
                    (unsigned long long)dev->inflightStartMs,
                    (unsigned)dev->groupCursor);
        }
    }
    if (dev->inflight) {
        /* [在途超时收口 2026-11-10 框架级根修] 从站对某一组不应答时（真机
         * M512 对 0xFA00×5 无应答；模拟器越界地址亦然），在途标记会一直挂
         * 着 ⇒ 单飞闸把该设备后续**所有**读组永久堵死（现场：变频器整体
         * 离线、失败计数=0、游标卡在 1、其余组从不再发）。
         * 收口：超时即判本次事务失败、记失败计数、报从站退避、释放标记，
         * 让轮询继续下一组（**不能**让一个组的沉默拖垮整条总线）。 */
        const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        const uint64_t timeoutMs = hubInflightTimeoutMs(hub, dev);
        if (dev->inflightStartMs != 0 && nowMs >= dev->inflightStartMs &&
            (nowMs - dev->inflightStartMs) > timeoutMs) {
            hubCloseTimedOutInflight(hub, dev, nowMs, timeoutMs);
            /* 继续往下走：本拍再发一个组（不空等）。 */
        } else {
            return;                                     /* 单飞占用。 */
        }
    }
    if (dev->state != XModbusDevice_ConnectedState) return;      /* 离线安静跳过。 */

    /* [轮询停滞诊断 2026-11-09 框架级补可观测]
     *
     * 现象：设备报"在线"、Hub 记"成功"，但某一台从站的数据再也不刷新，
     * 且**没有任何失败日志**（因为那条读组根本没发出去）。三道跳过闸门
     * （从站静默 / 分级节拍未到期 / 区域不在轮询掩码）都是静默 return
     * ⇒ 现场只能看到"没数据"，定位不到是哪一组、为什么没发。
     *
     * 处置：**按块**判陈旧（块级 lastOkMs = 这个点最后一次真正被回填的
     * 时刻；设备级 lastSuccessMs 会被同一 Hub 上其它从站的成功掩盖），
     * 一旦有块陈旧超过 3s ⇒ 打印一次诊断（限频 5s），逐块逐组列出
     * 陈旧点与跳过原因。 */
    {
        const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        if (hubDeviceHasStaleBlock(dev, nowMs, 3000u)) {
            if (dev->lastStallDiagMs == 0 ||
                nowMs - dev->lastStallDiagMs >= 5000u) {
                dev->lastStallDiagMs = nowMs;
                hubDumpStallDiag(hub, dev, nowMs);
            }
        }
    }


    /* ① 写优先（带饥饿保护）：该设备有脏区间；合并窗口未到期时等窗口。
     *    [2026-11-01 失败交替] 从站失联后写恒超时重发会把单飞总线永久占住
     *    （脏区间失败不放弃是写语义），轮询组再也提交不出去——unitLost 的
     *    失败计数无从累计（现场实证：pollErr 冻结在 1，21s 零流动）。
     *    处方：最近一笔事务失败期间读/写交替让路（偶数拍轮询先走），
     *    恢复成功即回到写优先；健康总线行为零变化。 */
    if (hubDeviceHasDirty(dev)) {
        const bool windowOpen = (hub->m_writeCoalesceMs > 0 &&
                                 hub->m_flushTimer != XTIMER_INVALID_ID);
        const bool writeTurn =
            (dev->lastTxOk || (dev->txAlternateTurn & 1u) != 0);
        if (!windowOpen && writeTurn) {
            const bool sent = hubSendNextWrite(hub, dev);
            if (sent) {
                if (!dev->lastTxOk) ++dev->txAlternateTurn;  /* 失败期：本次让给了写。 */
                return;
            }
        }
    }
    /* ② 轮询：总闸开且读组非空；groupCursor 回卷。[2026-10-28 单元退避]
     *    静默从站的组跳过——RTU 共口多从站时把总线让给活从站（死从站
     *    不再占用单飞槽吃超时），静默到期自动恢复探活；全部静默则本拍
     *    让总线。 */
    /* [只续写模式 2026-11-09] 收口尾部调用本泵时 allowPoll=false：写
     * 意图照发，**不发读**（避免收口链上再叠一帧读 → 轮询风暴/栈递归）。 */
    if (!allowPoll) return;
    if (!hub->m_polling || !dev->groups) return;
    {
        const size_t gn = XVector_size_base(dev->groups);
        const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        if (gn == 0) return;
        if (dev->groupCursor >= gn) dev->groupCursor = 0;
        for (size_t scanned = 0; scanned < gn; ++scanned) {
            const size_t idx = (dev->groupCursor + scanned) % gn;
            const XModbusHubReadGroup* g =
                (const XModbusHubReadGroup*)XVector_at_base(dev->groups, (int64_t)idx);
            bool muted;
            bool due;
            bool quietBlocked;
            bool readable;
            uint32_t quietMs;
            if (!g) continue;
            muted = hubUnitMuted(dev, g->unitId, nowMs);
            /* [2026-11-04 分级轮询] 该从站还没到最小发送间隔 ⇒ 跳过，
             * 让快从站立刻拿到本拍（慢从站不再按整轮陪跑）。 */
            due = hubUnitDue(dev, g->unitId, nowMs);
            /* [2026-11-10 发帧前总线静默闸] 该从站要求「上一笔事务收口后
             * 总线静默 ≥ m_preTxQuietMs」才许发（ttyS4 站5→站1 实测恢复期，
             * 见 XModbusHubUnitLimits 字段说明）。不满足则跳过该组——
             * 总线让给同拍扫描到的其它从站，本组等下一个扫描节拍再试；
             * 时钟回退放行（与 hubUnitDue 同口径），不卡死。 */
            quietMs = hubUnitPreTxQuietMs(dev, g->unitId);
            quietBlocked = (quietMs != 0 && dev->lastTxEndMs != 0 &&
                            nowMs >= dev->lastTxEndMs &&
                            (nowMs - dev->lastTxEndMs) < (uint64_t)quietMs);
            readable = hubGroupReadable(hub, dev, g);
            /* [诊断 2026-11-10 正式诊断手段] 逐组闸门裁决：把四道闸的
             * 判定结果**逐组**打出来（不只打被选中的那一组）——现场"某站
             * 全哑"时，这一行直接给出"哪一道闸把这组掐了"，不用再猜。
             * 计数按**Hub 指针**分桶（不同 Hub 的 dev->id 都从 1 起算，
             * 只按 dev->id 计数会让先跑的 Hub 吃光额度、后面的 Hub 一行都
             * 打不出来——这正是"逐组 trace 零输出"的假象来源）。
             * 用法：XMODBUS_HUB_TRACE=1 */
            {
                const char* env = getenv("XMODBUS_HUB_TRACE");
                if (env && env[0] && env[0] != '0' && hubTraceAllow(hub, 24)) {
                    XPrintf("[hub] dev=%d idx=%zu unit=%d type=%u start=%u "
                            "count=%u muted=%d due=%d quiet=%u quietBlocked=%d "
                            "readable=%d -> %s\n",
                            dev->id, idx, (int)g->unitId, (unsigned)g->type,
                            (unsigned)g->remoteStart, (unsigned)g->count,
                            muted ? 1 : 0, due ? 1 : 0, (unsigned)quietMs,
                            quietBlocked ? 1 : 0, readable ? 1 : 0,
                            (muted || !due || quietBlocked || !readable)
                                ? "skip" : "send");
                }
            }
            if (muted) continue;
            if (!due) continue;
            if (quietBlocked) {
                /* [静默闸饥饿逃逸 2026-11-10] 连续被挡过久 ⇒ 放行一帧，
                 * 否则配置离谱（未初始化的栈垃圾）或总线上别的从站流量过密
                 * 时，本从站的读组会**永远**轮不到（现场：变频器整站哑掉、
                 * 失败计数=0、游标卡死）。 */
                if (!hubUnitQuietEscape(dev, g->unitId, nowMs)) continue;
            } else {
                /* 未被挡：清零连续被挡起点（下一轮重新计时）。 */
                {
                    XModbusHubUnitSent* se = hubUnitSentEntryOf(dev, g->unitId);
                    if (se) se->quietBlockedSinceMs = 0;
                }
            }
            if (!readable) continue;
            if (hubSendPollGroup(hub, dev, g)) {
                (void)hubUnitMarkSent(dev, g->unitId, nowMs);
                dev->groupCursor = (idx + 1) % gn;
                if (!dev->lastTxOk) ++dev->txAlternateTurn;  /* 失败期：本次让给了轮询。 */
                return;
            }
        }
    }
}

/** @brief 统一泵（写+轮询都许）。绝大多数调用点用它；
 *        只有"应答收口尾部"用 hubPumpDeviceEx(..., false) 走只续写。 */
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev)
{
    hubPumpDeviceEx(hub, dev, true);
}

/** @brief 写队列泵：逐设备尝试下发（空闲设备才发）。 */
static void hubPumpWriteQueue(XModbusHub* hub)
{
    size_t i;
    if (!hub || !hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        if (d->inflight) continue;
        if (d->state != XModbusDevice_ConnectedState) continue;
        if (hubDeviceHasDirty(d)) hubPumpDevice(hub, d);
    }
}

/* ==================== 虚函数 ==================== */

static void VXModbusHub_deinit(XModbusHub* hub)
{
    if (!hub) return;
    hub->m_polling = false;
    /* 定时器全部 kill（周期定时器泄漏纪律）；client/reply 是 hub 的
     * XObject 子对象，由父类析构链统一删除，这里只清自身容器。 */
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XTIMER_INVALID_ID;
    }
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
    /* [2026-11-05 退出随机崩溃根修] 先把 children（client / reply）**主动**
     * 释放干净，再清自身容器，最后不走父类的 children 兜底。
     *
     * 原来的顺序是"清容器 → 父类链删 children"，其中删除 client 时：
     *   client 的析构会调 clearAllPendingRequests →
     *   XModbusReply_setState(Finished) → **同步**发 finished 信号 →
     *   Hub 的 onReplyFinished → deleteLater(reply)（或者直接释放 reply）。
     * 此时 Hub 已置 m_is_deleting_children = true，reply 的 VXObject_deinit
     * 因此**不把 reply 从 m_children 摘掉**；而父类的删除循环用的是进入
     * 循环时拍的快照，快照里仍有 reply ⇒ 对已释放内存再 XClassDelete 一次，
     * 读到 0xDDDDDDDD 填充即 AV。
     * 实测：hsf-io-flowmeter 退出随机崩 1/15~1/3，栈恒为
     *   VXObject_deinit → XClass_delete_base → XClass_deinit_base。
     *
     * 现在的顺序保证「先断设备关系，再按实际存活情况删对象」：
     *   ① 设备槽里的 inflight/client 指针全部清空（不再指向将删对象）；
     *   ② 逐个把 children 从 hub 上摘下来（setParent(NULL) 会同步摘链）；
     *   ③ 摘一个删一个 —— 删除期间 hub 的 m_children 已是空的，
     *      任何重入的 setParent/deleteLater 都不会制造悬垂条目。
     */
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d) continue;
            hubStopDevicePollTimer(hub, d);
            d->inflight = NULL;          /* Reply 是 hub 子对象，父析构统一收。 */
            d->client = NULL;            /* client 是 hub 子对象，父析构统一收。 */
            if (d->inflightPayload.write.values) {
                XFree_System(d->inflightPayload.write.values);
                d->inflightPayload.write.values = NULL;
            }
            if (d->name) {
                XClassDelete(d->name);
                d->name = NULL;
            }
            if (d->blocks) {
                hubBlocksFreeVector(d->blocks);
                d->blocks = NULL;
            }
            if (d->groups) {
                XClassDelete(d->groups);
                d->groups = NULL;
            }
            if (d->m_unitLimits) {
                XClassDelete(d->m_unitLimits);
                d->m_unitLimits = NULL;
            }
            if (d->m_unitLastSent) {
                XClassDelete(d->m_unitLastSent);
                d->m_unitLastSent = NULL;
            }
            XPollScheduler_deinit(&d->unitSched);
        }
        XClassDelete(hub->m_devices);
        hub->m_devices = NULL;
    }
    if (hub->m_mapping) {
        XClassDelete(hub->m_mapping);
        hub->m_mapping = NULL;
    }
    for (int t = 0; t < 4; ++t) {
        if (hub->m_view[t]) {
            XClassDelete(hub->m_view[t]);
            hub->m_view[t] = NULL;
        }
    }
    /* ---- ①②③ 有序释放 children（见函数头注释的根因说明） ---- */
    {
        const XVector* kids = XObject_children((XObject*)hub);
        int32_t kn = kids ? (int32_t)XVector_size_base(kids) : 0;
        while (kn > 0) {
            /* 每轮重取：摘链会改变表内容；kn 每次减一保证有界收敛。 */
            XObject* c = *(XObject* const*)XVector_at_base(kids, 0);
            if (!c) { kn--; break; }
            /* 先把自己从 hub 的 children 表里摘下来（同步摘链），
             * 再删 —— 删除期间的任何重入都看不到这个条目。 */
            XObject_setParent(c, NULL);
            if (Class_IsHeap(c))
                XClassDelete(c);
            else
                XClassDeinit(c);
            kids = XObject_children((XObject*)hub);
            kn = kids ? (int32_t)XVector_size_base(kids) : 0;
            /* 防御：若摘链失败导致数量不减，强制退出避免死循环。 */
            if (kn > 0 && *(XObject* const*)XVector_at_base(kids, 0) == c)
                break;
        }
    }
    XClass_Deinit_Parent(XObject, hub);
}

XVtable* XModbusHub_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusHub)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 无新增虚函数：仅重载既有槽（deinit/timerEvent），不追加函数表。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusHub_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusHub_timerEvent);
    XCLASS_SHOW_SIZE_DEFAULT(XModbusHub);
    return XVTABLE_DEFAULT;
}

void XModbusHub_init(XModbusHub* hub)
{
    if (!hub) return;
    XObject_init((XObject*)hub);
    XClassGetVtable(hub) = XModbusHub_class_init();

    hub->m_mapping = NULL;
    hub->m_devices = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       sizeof(XModbusHubDevice), false);
    hub->m_nextDeviceId = 1;
    for (int t = 0; t < 4; ++t) {
        hub->m_view[t] = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           sizeof(XModbusHubCacheSlice), false);
        hub->m_spanStart[t] = 0;
        hub->m_spanCount[t] = 0;
    }
    hub->m_pollIntervalDefaultMs = 1000;
    /* [2026-11-10 在途收口] 0=自动（客户端超时 → 缺省 1000ms + 宽限余量）。 */
    hub->m_inflightTimeoutMs = 0;
    /* 默认只轮询输入区（DI|IR）——效率最大化：输出区以写回显为准，
     * 不花流量读回。setPollAreas 可放宽。 */
    hub->m_pollAreas = (1u << XModbusDiscreteInputs) | (1u << XModbusInputRegisters);
    hub->m_polling = false;
    hub->m_retryTimer = XTIMER_INVALID_ID;
    hub->m_writeRetryIntervalMs = 1000;
    hub->m_changedOnlySignals = true;
    hub->m_outputSyncMode = (uint16_t)XModbusHub_OutputSync_Cache;   /* 缺省缓存回灌。 */
    /* [2026-10-28 单元退避/写滞留可见性] 缺省对齐 XPollScheduler 缺省值。 */
    hub->m_unitMaxFails = XPOLL_SCHED_DEFAULT_MAX_FAILS;
    hub->m_unitMuteMs = XPOLL_SCHED_DEFAULT_MUTE_MS;
    hub->m_writeStallNoticeAt = 3;
    /* [2026-10-29 缓存层] 读合并缺口默认 0（行为与旧版一致，现场可调 4~8）；
     * 写合并窗口默认 0（受理即下发，行为与旧版一致）。 */
    hub->m_readMergeGap = 0;
    hub->m_writeCoalesceMs = 0;
    hub->m_flushTimer = XTIMER_INVALID_ID;
    hub->m_lastWriteMs = 0;
    hub->m_syncFinishDepth = 0;
}

XModbusHub* XModbusHub_create_ex(XMemoryType memory)
{
    XModbusHub* hub = (XModbusHub*)XMemory_malloc(sizeof(XModbusHub), memory);
    if (!hub) return NULL;
    XModbusHub_init(hub);
    Set_Class_Memory(hub, memory); Set_Class_IsHeap(hub, true);
    return hub;
}

/* ==================== 设备管理 ==================== */

int XModbusHub_addDevice(XModbusHub* hub, XModbusClient* client, const XString* name)
{
    if (!hub || !client || !hub->m_devices) return -1;
    XModbusHubDevice dev;
    memset(&dev, 0, sizeof(dev));
    dev.client = client;
    dev.id = hub->m_nextDeviceId++;
    dev.state = XModbusDevice_state((XModbusDevice*)client);
    dev.pollIntervalMs = hub->m_pollIntervalDefaultMs;
    dev.pollTimer = XTIMER_INVALID_ID;
    dev.inflight = NULL;
    dev.inflightKind = XModbusHubInflight_None;
    dev.lastTxOk = true;      /* 健康起点：写优先。 */
    dev.lastTxEndMs = 0;      /* 从未收口：静默闸放行（见 XModbusHubDevice）。 */
    dev.txAlternateTurn = 0;
    dev.blocks = NULL;
    dev.groups = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   sizeof(XModbusHubReadGroup), false);
    dev.m_unitLimits = NULL;    /* 从站约束表惰性建（setUnitLimits 时建）。 */
    dev.m_unitLastSent = NULL;  /* 从站发送时刻表惰性建（首次发读组时建）。 */
    /* XString 主版本：深拷贝入参（调用方传入的串可随即释放）；_2 为
     * UTF-8 const char* 兼容重载（约束文档「字符串 API 主次顺序」）。 */
    if (name && XString_size_base(name) > 0) dev.name = XString_create_copy(name);

    /* 所有权：client reparent 到 hub（XObject 父子析构链统一收尾）。
     * 底层断线自动重连常开（聚合设备对底层透明是核心承诺）。 */
    XObject_setParent((XObject*)client, (XObject*)hub);
    XModbusClient_setAutoReconnect(client, true);
    /* stateChanged：connect_2 → 槽第一参=真 sender=client（约束文档
     * 「信号与槽规范」：connect_1 第一参是 receiver，误读即错位）。 */
    XObject_connect_2((XObject*)client,
                      XSignal(XModbusDevice_stateChanged_signal),
                      XModbusHub_onDeviceStateChanged);

    if (!XVector_push_back_1_base(hub->m_devices, &dev)) {
        if (dev.name) XClassDelete(dev.name);
        if (dev.groups) XClassDelete(dev.groups);
        return -1;
    }
    hubRebuildAll(hub);
    return dev.id;
}

int XModbusHub_addDevice_2(XModbusHub* hub, XModbusClient* client, const char* name)
{
    XString* owned = NULL;
    int id;
    if (name && name[0]) {
        owned = XString_create_utf8(name);
        if (!owned) return -1; /* 与主版本失败口径一致：不接管 client。 */
    }
    id = XModbusHub_addDevice(hub, client, owned);
    if (owned) XClassDelete((XClass*)owned);
    return id;
}

bool XModbusHub_removeDevice(XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    size_t bi;
    if (!dev) return false;
    hubStopDevicePollTimer(hub, dev);
    /* 滞留写意图作废：writeCompleted(ok=false) 告知调用方（逐脏区间）。 */
    if (dev->blocks) {
        for (bi = 0; bi < XVector_size_base(dev->blocks); ++bi) {
            XModbusHubCacheBlock* b =
                *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks, (int64_t)bi);
            size_t di;
            if (!b || !b->dirty) continue;
            for (di = 0; di < XVector_size_base(b->dirty); ++di) {
                const XModbusHubDirtyRange* d =
                    (const XModbusHubDirtyRange*)XVector_at_base(b->dirty, (int64_t)di);
                if (!d) continue;
                hubEmitWriteDone(hub, b->type,
                                 (uint16_t)hubDirtyVirtualStart(b, d),
                                 d->count, false);
            }
        }
    }
    if (dev->inflight) {
        XObject_deleteLater((XObject*)dev->inflight);
        dev->inflight = NULL;
        dev->inflightKind = XModbusHubInflight_None;
    }
    if (dev->inflightPayload.write.values) {
        XFree_System(dev->inflightPayload.write.values);
        dev->inflightPayload.write.values = NULL;
    }
    if (dev->client) {
        XModbusDevice_disconnectDevice((XModbusDevice*)dev->client);
        XObject_deleteLater((XObject*)dev->client);   /* 自动脱离 hub 子列表。 */
        dev->client = NULL;
    }
    if (dev->name) {
        XClassDelete(dev->name);
        dev->name = NULL;
    }
    if (dev->blocks) {
        hubBlocksFreeVector(dev->blocks);
        dev->blocks = NULL;
    }
    if (dev->groups) {
        XClassDelete(dev->groups);
        dev->groups = NULL;
    }
    if (dev->m_unitLimits) {
        XClassDelete(dev->m_unitLimits);
        dev->m_unitLimits = NULL;
    }
    if (dev->m_unitLastSent) {
        XClassDelete(dev->m_unitLastSent);
        dev->m_unitLastSent = NULL;
    }
    XPollScheduler_deinit(&dev->unitSched); /* 槽位摘除前清表。 */
    {
        const int idx = hubDeviceIndexById(hub, deviceId);
        if (idx >= 0) XVector_remove_base(hub->m_devices, idx, 1);
    }
    hubRebuildView(hub);                    /* 视图切片指向已释放块：必须重建。 */
    return true;
}

int XModbusHub_deviceCount(const XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return 0;
    return (int)XVector_size_base(hub->m_devices);
}

XModbusClient* XModbusHub_deviceClient(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->client : NULL;
}

const char* XModbusHub_deviceName_const(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return (dev && dev->name) ? XString_toUtf8(dev->name) : NULL;
}

XModbusDevice_State XModbusHub_deviceState(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->state : XModbusDevice_UnconnectedState;
}

bool XModbusHub_isOnline(const XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return false;
    {
        const size_t n = XVector_size_base(hub->m_devices);
        if (n == 0) return false;
        for (size_t i = 0; i < n; ++i) {
            const XModbusHubDevice* d =
                (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d || d->state != XModbusDevice_ConnectedState) return false;
        }
    }
    return true;
}

bool XModbusHub_connectAll(XModbusHub* hub)
{
    bool all = true;
    if (!hub || !hub->m_devices) return false;
    {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d || !d->client) continue;
            if (!XModbusDevice_connectDevice((XModbusDevice*)d->client)) all = false;
        }
    }
    return all;
}

void XModbusHub_disconnectAll(XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return;
    XModbusHub_stopPolling(hub);
    {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d && d->client)
                XModbusDevice_disconnectDevice((XModbusDevice*)d->client);
        }
    }
}

/* ==================== 映射 ==================== */

void XModbusHub_setMapping(XModbusHub* hub, XModbusMapping* ownedMapping)
{
    if (!hub) {
        if (ownedMapping) XClassDelete(ownedMapping);
        return;
    }
    if (hub->m_mapping) XClassDelete(hub->m_mapping);
    hub->m_mapping = ownedMapping;
    hubRebuildAll(hub);
}

XModbusMapping* XModbusHub_mapping(const XModbusHub* hub)
{
    return hub ? hub->m_mapping : NULL;
}

void XModbusHub_refreshMapping(XModbusHub* hub)
{
    if (!hub) return;
    hubRebuildAll(hub);
}

/* ==================== 轮询 ==================== */

void XModbusHub_setPollIntervalDefault(XModbusHub* hub, size_t ms)
{
    if (hub) hub->m_pollIntervalDefaultMs = ms;
}

bool XModbusHub_setPollInterval(XModbusHub* hub, int deviceId, size_t ms)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    if (!dev) return false;
    dev->pollIntervalMs = ms;
    if (hub->m_polling) {
        hubStopDevicePollTimer(hub, dev);
        if (ms > 0) {
            dev->pollTimer = XObject_startTimer_ms((XObject*)hub,
                                                   (int)ms, XTimerType_CoarseTimer);
            /* [注册失败必须可见 2026-11-09] XObject_startTimer_ms 注册失败
             * 时返回 XTIMER_INVALID_ID（见 XAbstractEventDispatcher 的
             * registerTimer：dispatcher 无 deviceCtx 就会失败）。原实现
             * **丢弃返回值** ⇒ 该设备的轮询定时器静默死亡：既不轮询、
             * 也不报错，看门狗盯的是"最后成功应答"（别的从站还在成功）
             * ⇒ 永远判健康 ⇒ 只有重启进程能恢复。 */
            if (dev->pollTimer == XTIMER_INVALID_ID) {
                XPrintf("XModbusHub: 设备 %d 轮询定时器注册失败（间隔 %u ms）"
                        "——该设备将停止轮询\n", dev->id, (unsigned)ms);
            }
        }
    }
    return true;
}

size_t XModbusHub_pollInterval(XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    if (!dev) return 0;
    return dev->pollIntervalMs;
}

void XModbusHub_setPollAreas(XModbusHub* hub, unsigned areaMask)
{
    if (!hub) return;
    if (hub->m_pollAreas == areaMask) return;
    hub->m_pollAreas = areaMask;
    hubRebuildAll(hub);
}

void XModbusHub_setInflightTimeoutMs(XModbusHub* hub, size_t ms)
{
    if (!hub) return;
    hub->m_inflightTimeoutMs = ms;
}

size_t XModbusHub_inflightTimeoutMs(const XModbusHub* hub)
{
    return hub ? hub->m_inflightTimeoutMs : 0u;
}

void XModbusHub_setReadMergeGap(XModbusHub* hub, uint16_t gap)
{
    size_t i;
    if (!hub) return;
    if (hub->m_readMergeGap == gap) return;
    hub->m_readMergeGap = gap;
    if (!hub->m_devices) return;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d) hubRebuildGroups(hub, d);
    }
}

/**
 * @brief 登记从站约束前的**入口体检**（2026-11-10 框架级根修）。
 * @details 调用方常按"只填自己关心的字段"的方式构造 XModbusHubUnitLimits
 *          （栈上局部变量，未 memset）⇒ 漏掉的字段是**栈垃圾**。现场实证：
 *          变频器站的约束只填了 4 个字段，m_preTxQuietMs 留成 0xCCCCCCCC
 *          （Debug 构建的未初始化填充）⇒ 静默闸要求"34 亿毫秒总线静默"
 *          ⇒ 该从站的读组**永远**轮不到（现场：变频器整站哑掉、失败计数=0、
 *          游标卡死、其余组从不再发）。
 *          框架层不能判断调用方"忘了填"还是"故意填大"，但可以**夹住离谱值**：
 *          静默期超过 XMODBUS_HUB_MAX_PRE_TX_QUIET_MS 一律夹到上限并告警
 *          （配合静默闸饥饿逃逸 ⇒ 该从站仍会被放行，不会被永久饿死）。
 */
static XModbusHubUnitLimits hubSanitizeUnitLimits(XModbusHubUnitLimits lim,
                                                   int unitId)
{
    if (lim.m_preTxQuietMs > (uint32_t)XMODBUS_HUB_MAX_PRE_TX_QUIET_MS) {
        XPrintf("XModbusHub: 从站 %d 的发帧前静默期 %u ms 超过上限 %u ms ——"
                "已夹到上限（多半是调用方漏填该字段留下的栈垃圾；"
                "不夹 ⇒ 该从站的读组永远轮不到）\n",
                unitId, (unsigned)lim.m_preTxQuietMs,
                (unsigned)XMODBUS_HUB_MAX_PRE_TX_QUIET_MS);
        lim.m_preTxQuietMs = (uint32_t)XMODBUS_HUB_MAX_PRE_TX_QUIET_MS;
    }
    return lim;
}

bool XModbusHub_setUnitLimits(XModbusHub* hub, int deviceId, int unitId,
                               const XModbusHubUnitLimits* limits)
{
    XModbusHubDevice* dev;
    size_t i;
    if (!hub) return false;
    dev = hubDeviceById(hub, deviceId);
    if (!dev) return false;
    if (!dev->m_unitLimits) {
        dev->m_unitLimits = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               sizeof(XModbusHubUnitLimitEntry), false);
        if (!dev->m_unitLimits) return false;
    }
    for (i = 0; i < XVector_size_base(dev->m_unitLimits); ++i) {
        XModbusHubUnitLimitEntry* e =
            (XModbusHubUnitLimitEntry*)XVector_at_base(dev->m_unitLimits, (int64_t)i);
        if (!e || e->unitId != unitId) continue;
        if (!limits) {
            XVector_remove_base(dev->m_unitLimits, (int64_t)i, 1);
        } else {
            e->m_limits = hubSanitizeUnitLimits(*limits, unitId);
        }
        hubRebuildAll(hub);
        return true;
    }
    if (!limits) return true;                    /* 本就未配置：清除即无操作。 */
    {
        XModbusHubUnitLimitEntry e;
        e.unitId = unitId;
        e.m_limits = hubSanitizeUnitLimits(*limits, unitId);
        if (!XVector_push_back_1_base(dev->m_unitLimits, &e)) return false;
    }
    /* 屏障约束会改块结构（映射段不再合并），必须整表重建；只重建读组
     * 会留下旧块结构，边界仍被吃掉。 */
    hubRebuildAll(hub);
    return true;
}

bool XModbusHub_getUnitLimits(const XModbusHub* hub, int deviceId, int unitId,
                               XModbusHubUnitLimits* outLimits)
{
    XModbusHubDevice* dev;
    const XModbusHubUnitLimits* lim;
    if (!hub || !outLimits) return false;
    dev = hubDeviceById((XModbusHub*)hub, deviceId);
    if (!dev) return false;
    lim = hubUnitLimitsOf(dev, unitId);
    if (!lim) return false;
    *outLimits = *lim;
    return true;
}

void XModbusHub_setUnitBackoff(XModbusHub* hub, uint32_t maxFails, uint32_t muteMs)
{
    if (!hub) return;
    /* 门槛与 XPollScheduler_setBackoff 一致：>=1 / >=1000 才生效。 */
    if (maxFails >= 1) hub->m_unitMaxFails = maxFails;
    if (muteMs >= 1000) hub->m_unitMuteMs = muteMs;
    /* 已建设备即时同步（退避表在下次块表重建时也会按新配置重建）。 */
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) XPollScheduler_setBackoff(&d->unitSched,
                                             hub->m_unitMaxFails, hub->m_unitMuteMs);
        }
    }
}

void XModbusHub_startPolling(XModbusHub* hub)
{
    if (!hub || hub->m_polling) return;
    hub->m_polling = true;
    /* 缓存/读组兜底重建（映射可能在 attach 后被直接编辑过）。 */
    hubRebuildAll(hub);
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d) continue;
            hubStopDevicePollTimer(hub, d);
            if (d->pollIntervalMs > 0) {
                d->pollTimer = XObject_startTimer_ms((XObject*)hub,
                                                     (int)d->pollIntervalMs,
                                                     XTimerType_CoarseTimer);
                /* [注册失败必须可见 2026-11-09] 见 setPollInterval 的
                 * 同名注：注册失败返回 XTIMER_INVALID_ID，原实现丢弃返回
                 * 值 ⇒ 该设备静默停止轮询且无任何日志。 */
                if (d->pollTimer == XTIMER_INVALID_ID) {
                    XPrintf("XModbusHub: 设备 %d 轮询定时器注册失败（间隔 %u ms）"
                            "——该设备将停止轮询\n", d->id,
                            (unsigned)d->pollIntervalMs);
                }
            }
        }
    }
    /* 写重发定时器随总闸拉起（滞留脏区间恢复补发）。 */
    if (hub->m_retryTimer == XTIMER_INVALID_ID) {
        hub->m_retryTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)hub->m_writeRetryIntervalMs,
                                                  XTimerType_CoarseTimer);
    }
    hubPumpWriteQueue(hub);
}

void XModbusHub_stopPolling(XModbusHub* hub)
{
    if (!hub) return;
    hub->m_polling = false;
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            hubStopDevicePollTimer(hub, d);
        }
    }
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XTIMER_INVALID_ID;
    }
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
}

bool XModbusHub_isPolling(const XModbusHub* hub)
{
    return hub ? hub->m_polling : false;
}

/* ==================== 统一缓存读 ==================== */

bool XModbusHub_value(const XModbusHub* hub, XModbusRegisterType type,
                      uint16_t virtualAddr, uint16_t* outValue)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    const XModbusHubCacheSlice* s;
    if (!outValue) return false;
    *outValue = 0xFFFFu;
    if (!hub || t < 0 || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualAddr);
    if (vi < 0) return false;
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    *outValue = XModbusDataUnit_value(s->block->data,
                                      (size_t)s->blockOffset +
                                      (virtualAddr - s->virtualStart));
    return true;
}

bool XModbusHub_values(const XModbusHub* hub, XModbusRegisterType type,
                       uint16_t virtualStart, uint16_t count, uint16_t* outValues)
{
    bool all = true;
    if (!outValues) return false;
    for (uint16_t i = 0; i < count; ++i) {
        if (!XModbusHub_value(hub, type, (uint16_t)(virtualStart + i), &outValues[i])) {
            outValues[i] = 0xFFFFu; /* 未映射洞哨兵 */
            all = false;
        }
    }
    return all;
}

uint16_t XModbusHub_cacheCount(const XModbusHub* hub, XModbusRegisterType type)
{
    const int t = hubTypeIndex((int)type);
    if (!hub || t < 0) return 0;
    return hub->m_spanCount[t];
}

uint64_t XModbusHub_lastUpdateMs(const XModbusHub* hub, XModbusRegisterType type)
{
    uint64_t latest = 0;
    size_t i;
    if (!hub || !hub->m_devices) return 0;
    for (i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        size_t bi;
        if (!d || !d->blocks) continue;
        for (bi = 0; bi < XVector_size_base(d->blocks); ++bi) {
            const XModbusHubCacheBlock* b =
                *(XModbusHubCacheBlock* const*)XVector_at_base(d->blocks, (int64_t)bi);
            if (!b || b->type != (uint16_t)type) continue;
            if (b->lastOkMs > latest) latest = b->lastOkMs;
        }
    }
    return latest;
}

/* ==================== 点级质量 ==================== */

bool XModbusHub_valueAge(const XModbusHub* hub, XModbusRegisterType type,
                         uint16_t virtualAddr, uint64_t* ageMs)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    const XModbusHubCacheSlice* s;
    if (!hub || t < 0 || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualAddr);
    if (vi < 0) {
        /* [诊断 2026-11-09] 视图里二分查不到该虚拟地址 ⇒ 上层读值失败、
         * 点级有效性判为假 ⇒ 界面"无数据"。此前这条路径**完全静默**，
         * 现场只能看到"没数据"，查不到是"地址没进视图"还是"视图被重建
         * 后没重建回来"。限频打印视图内容。 */
        {
            static uint64_t s_lastDiagMs = 0;
            const uint64_t now = XDateTime_currentMSecsSinceEpoch();
            if (now - s_lastDiagMs >= 1000u) {
                size_t vn = XVector_size_base((const XContainer*)hub->m_view[t]);
                size_t k;
                s_lastDiagMs = now;
                XPrintf("XModbusHub: 视图查不到地址（类型=%d 地址=%u）"
                        "——视图切片数=%u\n", (int)type, (unsigned)virtualAddr,
                        (unsigned)vn);
                for (k = 0; k < vn; ++k) {
                    const XModbusHubCacheSlice* s2 =
                        (const XModbusHubCacheSlice*)XVector_at_base(
                            (const XContainer*)hub->m_view[t], (int64_t)k);
                    if (!s2) continue;
                    XPrintf("    切片[%u] 虚拟起始=%u 数量=%u 块=%p\n",
                            (unsigned)k, (unsigned)s2->virtualStart,
                            (unsigned)s2->count, (void*)s2->block);
                }
            }
        }
        return false;
    }
    s = (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    if (ageMs) {
        if (s->block->lastOkMs == 0) {
            *ageMs = UINT64_MAX;                 /* 从未成功读回。 */
        } else {
            const uint64_t now = XDateTime_currentMSecsSinceEpoch();
            *ageMs = (now > s->block->lastOkMs) ? (now - s->block->lastOkMs) : 0;
        }
    }
    return true;
}

bool XModbusHub_pointValid(const XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint64_t maxAgeMs)
{
    uint64_t age = 0;
    if (!XModbusHub_valueAge(hub, type, virtualAddr, &age)) return false;
    return age != UINT64_MAX && age <= maxAgeMs;
}

bool XModbusHub_requestRefresh(XModbusHub* hub, XModbusRegisterType type,
                               uint16_t virtualStart, uint16_t count)
{
    const int t = hubTypeIndex((int)type);
    uint32_t end;
    bool hit = false;
    if (!hub || !hub->m_view || t < 0 || !hub->m_view[t] || count == 0) return false;
    end = (uint32_t)virtualStart + count;
    for (size_t i = 0; i < XVector_size_base(hub->m_view[t]); ++i) {
        const XModbusHubCacheSlice* s =
            (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)i);
        if (!s || !s->block) continue;
        if ((uint32_t)s->virtualStart >= end) break;
        if ((uint32_t)s->virtualStart + s->count <= virtualStart) continue;
        s->block->refreshRequested = true;
        hit = true;
    }
    if (!hit) return false;
    /* 立即泵一次（未连接/在途时由定时器与收口续泵接管）。 */
    if (hub->m_devices) {
        for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d) hubPumpDevice(hub, d);
        }
    }
    return true;
}

/* ==================== 输出写 ==================== */

void XModbusHub_setWriteRetryInterval(XModbusHub* hub, size_t ms)
{
    if (!hub || ms == 0) return;
    hub->m_writeRetryIntervalMs = ms;
    if (hub->m_retryTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_retryTimer);
        hub->m_retryTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)ms, XTimerType_CoarseTimer);
    }
}

void XModbusHub_setWriteCoalesceMs(XModbusHub* hub, size_t ms)
{
    if (hub) hub->m_writeCoalesceMs = ms;
}

void XModbusHub_setWriteStallNotice(XModbusHub* hub, int attempts)
{
    if (hub) hub->m_writeStallNoticeAt = attempts; /* <1=关闭提示；只影响提示不改重发。 */
}

void XModbusHub_flush(XModbusHub* hub)
{
    if (!hub) return;
    if (hub->m_flushTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)hub, hub->m_flushTimer);
        hub->m_flushTimer = XTIMER_INVALID_ID;
    }
    hubPumpWriteQueue(hub);
}

/** @brief 写受理后调度刷写：窗口 0=立即泵；否则首笔拉起窗口定时器。 */
static void hubScheduleFlush(XModbusHub* hub, int deviceId)
{
    if (hub->m_writeCoalesceMs == 0) {
        XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
        if (dev) hubPumpDevice(hub, dev);
        return;
    }
    if (hub->m_flushTimer == XTIMER_INVALID_ID) {
        hub->m_flushTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)hub->m_writeCoalesceMs,
                                                  XTimerType_CoarseTimer);
    }
}

bool XModbusHub_writeValue(XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint16_t value)
{
    return XModbusHub_writeValues(hub, type, virtualAddr, 1, &value);
}

bool XModbusHub_writeValues(XModbusHub* hub, XModbusRegisterType type,
                            uint16_t virtualStart, uint16_t count,
                            const uint16_t* values)
{
    const int t = hubTypeIndex((int)type);
    int vi;
    XModbusHubCacheSlice* s;
    XModbusHubCacheBlock* block;
    uint16_t blockOffset;
    uint32_t end;
    bool changed = false;
    if (!hub || !values || count == 0) return false;
    if (type != XModbusCoils && type != XModbusHoldingRegisters) return false;
    if (t < 0 || !hub->m_view || !hub->m_view[t]) return false;
    vi = hubViewFind(hub->m_view[t], virtualStart);
    if (vi < 0)
        return false;   /* 该区未在缓存视图内：写前必须先轮询一次。 */
    s = (XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)vi);
    if (!s || !s->block) return false;
    block = s->block;
    blockOffset = (uint16_t)(s->blockOffset + (virtualStart - s->virtualStart));
    /* 整段必须落在同一块内且无空洞（块=远端连续段=写安全边界）。 */
    end = (uint32_t)virtualStart + count;
    {
        uint32_t covered = (uint32_t)virtualStart;
        for (size_t k = (size_t)vi; k < XVector_size_base(hub->m_view[t]); ++k) {
            const XModbusHubCacheSlice* s2 =
                (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)k);
            if (!s2 || s2->block != block) return false;
            if ((uint32_t)s2->virtualStart + s2->count <= covered) continue;
            if ((uint32_t)s2->virtualStart > covered) return false;   /* 洞：拒绝。 */
            covered = (uint32_t)s2->virtualStart + s2->count;
            if (covered >= end) break;
        }
        if (covered < end) return false;
    }
    if ((uint32_t)blockOffset + count > block->count) return false;

    /* ① 乐观写入块缓存（内存写，微秒级受理）+ 变化发射。 */
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t v = hubTypeIsBit(block->type) ? (values[i] ? 1u : 0u) : values[i];
        if (XModbusDataUnit_value(block->data, blockOffset + i) != v) {
            XModbusDataUnit_setValue(block->data, blockOffset + i, v);
            changed = true;
        }
    }
    /* [2026-11-01 首推恒发] 与读路径 hubFillGroupResult 的 firstFill 恒发
     * 对称：changedOnly 下播种值恰与写值相同（无变化沿）会吞掉首笔发射，
     * 未轮询输出区（上线对齐写是唯一推送源）的挂接点将永收不到初值。 */
    if (!hub->m_changedOnlySignals || changed || !block->firstEmitDone) {
        const uint32_t lo = (uint32_t)block->remoteStart + blockOffset;
        const uint32_t hi = lo + count;
        for (size_t k = (size_t)vi; k < XVector_size_base(hub->m_view[t]); ++k) {
            const XModbusHubCacheSlice* s2 =
                (const XModbusHubCacheSlice*)XVector_at_base(hub->m_view[t], (int64_t)k);
            uint32_t sStart, sEnd, cLo, cHi;
            if (!s2 || s2->block != block) break;
            sStart = (uint32_t)block->remoteStart + s2->blockOffset;
            sEnd = sStart + s2->count;
            cLo = (sStart > lo) ? sStart : lo;
            cHi = (sEnd < hi) ? sEnd : hi;
            if (cHi <= cLo) continue;
            XModbusHub_valueChanged_signal(hub, (XModbusRegisterType)block->type,
                                           (int)(s2->virtualStart + (cLo - sStart)),
                                           (int)(cHi - cLo));
        }
        block->firstEmitDone = true;
    }
    /* ② 标脏（合并窗口内多次写只留最终值）+ 调度刷写。 */
    hubMarkDirty(block, blockOffset, count);
    hub->m_lastWriteMs = XDateTime_currentMSecsSinceEpoch();
    hubScheduleFlush(hub, s->deviceId);
    return true;
}

int XModbusHub_pendingWriteCount(const XModbusHub* hub)
{
    int total = 0;
    if (!hub || !hub->m_devices) return 0;
    for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        total += hubDeviceDirtyCount(d);
    }
    return total;
}

/* ==================== 统计 ==================== */

uint32_t XModbusHub_pollOkCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->pollOkCount : 0;
}

uint32_t XModbusHub_pollErrCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->pollErrCount : 0;
}

uint32_t XModbusHub_reconnectCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->reconnectCount : 0;
}

uint64_t XModbusHub_deviceLastSuccessMs(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    return dev ? dev->lastSuccessMs : 0;
}

bool XModbusHub_deviceWorstUnitAge(const XModbusHub* hub, int deviceId,
                                    int* outWorstUnitId, uint64_t* outAgeMs)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    size_t bn;
    size_t i;
    uint64_t worst = 0;
    int worstUnit = -1;
    bool got = false;
    const uint64_t now = XDateTime_currentMSecsSinceEpoch();

    if (outWorstUnitId) *outWorstUnitId = -1;
    if (outAgeMs) *outAgeMs = 0;
    if (!dev || !dev->blocks) return false;
    bn = XVector_size_base((const XContainer*)dev->blocks);
    for (i = 0; i < bn; ++i) {
        const XModbusHubCacheBlock* b =
            *(XModbusHubCacheBlock* const*)XVector_at_base(
                (const XContainer*)dev->blocks, (int64_t)i);
        uint64_t age;
        if (!b) continue;
        if (b->flags & XModbusMapRange_WriteOnly) continue;  /* 只写块不轮询 */
        age = (b->lastOkMs == 0) ? UINT64_MAX
                                 : ((now > b->lastOkMs) ? now - b->lastOkMs : 0);
        if (!got || age > worst) {
            worst = age;
            worstUnit = (int)b->unitId;
            got = true;
        }
    }
    if (!got) return false;
    if (outWorstUnitId) *outWorstUnitId = worstUnit;
    if (outAgeMs) *outAgeMs = worst;
    return true;
}

int XModbusHub_readGroupCount(const XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById((XModbusHub*)hub, deviceId);
    if (!dev || !dev->groups) return 0;
    return (int)XVector_size_base(dev->groups);
}

/* ==================== 定时器（XObject 自带机制，规范 2026-10-26） ==================== */

static void VXModbusHub_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusHub* hub = (XModbusHub*)obj;
    if (!hub || !event) goto parent_call;
    XEvent_accept(event);
    {
        const XTimerId id = XTimerEvent_timerId(event);
        if (id == hub->m_retryTimer) {
            hubPumpWriteQueue(hub);
            return;
        }
        if (id == hub->m_flushTimer) {
            /* 合并窗口到期：停表并统一刷写（写窗口语义=首笔起算一个窗口）。 */
            XObject_killTimer(obj, hub->m_flushTimer);
            hub->m_flushTimer = XTIMER_INVALID_ID;
            hubPumpWriteQueue(hub);
            return;
        }
        if (hub->m_devices) {
            const size_t n = XVector_size_base(hub->m_devices);
            for (size_t i = 0; i < n; ++i) {
                XModbusHubDevice* d =
                    (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
                if (d && d->pollTimer == id) {
                    hubPumpDevice(hub, d);
                    return;
                }
            }
        }
    }
parent_call:
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* ==================== 槽 ==================== */

/**
 * @brief 设备上线输出对齐：未轮询的输出区（Coils/HR）各段一帧统一下发。
 * @details [2026-11-01 输出上线对齐] 输出区不轮询时外部状态无人兜底：
 *          程序重启残留、从站掉电恢复后的随机态都会让映射与外部不一致。
 *          设备每次跃迁 Connected 即按映射段表取该设备的输出区间整段
 *          下发（同设备同从站远端连续段即一帧，FC15/FC16），取值来源由
 *          m_outputSyncMode 选择：
 *            - Cache（缺省）：缓存回灌——首启缓存已由段默认值播种
 *              （hubSeedBlockDefaults），下发即默认值清残留；重连回灌
 *              最后写入值，恢复意图状态。
 *            - Default：映射段默认值覆盖设备与缓存（现场强制复位）。
 *          区在轮询掩码内则跳过（轮询自会比对对齐）。缓存回灌模式下
 *          寄存器区哨兵 0xFFFF 防御性置 0（播种已覆盖，双保险不写垃圾）。
 */
static void hubPushUnpolledOutputs(XModbusHub* hub, XModbusHubDevice* dev)
{
    const bool useDefault =
        (hub->m_outputSyncMode == (uint16_t)XModbusHub_OutputSync_Default);
    int t;
    if (!hub || !dev || !hub->m_mapping) return;
    for (t = XModbusCoils; t <= XModbusHoldingRegisters; ++t) {
        const XModbusRegisterType type = (XModbusRegisterType)t;
        XModbusMapRange* ranges;
        int got, i;
        size_t total;
        if (hub->m_pollAreas & (1u << t)) continue;  /* 轮询区交轮询对齐。 */
        total = XModbusMapping_rangeCount_const(hub->m_mapping, type);
        if (total == 0) continue;
        ranges = (XModbusMapRange*)XMalloc_System(sizeof(XModbusMapRange) * total);
        if (!ranges) continue;
        got = XModbusMapping_segmentsForDevice_const(hub->m_mapping, dev->id,
                                                     type, ranges, (int)total);
        for (i = 0; i < ((got < 0) ? 0 : got); ++i) {
            uint16_t* vals;
            if (ranges[i].count == 0) continue;
            vals = (uint16_t*)XMalloc_System(sizeof(uint16_t) * ranges[i].count);
            if (!vals) break;
            if (useDefault) {
                /* 默认值覆盖：整段按段默认值（位区非 0 视为 1）。 */
                const uint16_t v = hubTypeIsBit((uint16_t)t)
                                       ? (ranges[i].defValue ? 1u : 0u)
                                       : ranges[i].defValue;
                for (uint16_t k = 0; k < ranges[i].count; ++k) vals[k] = v;
            } else {
                /* 缓存回灌：读缓存下发（缓存是输出意图的事实源）。 */
                if (!XModbusHub_values(hub, type, ranges[i].virtualStart,
                                       ranges[i].count, vals)) {
                    XFree_System(vals);
                    continue;
                }
                if (!hubTypeIsBit((uint16_t)t)) {
                    for (uint16_t k = 0; k < ranges[i].count; ++k)
                        if (vals[k] == 0xFFFFu) vals[k] = 0;
                }
            }
            XModbusHub_writeValues(hub, type, ranges[i].virtualStart,
                                   ranges[i].count, vals);
            XFree_System(vals);
        }
        XFree_System(ranges);
    }
}

/** @brief 设置上线输出对齐模式（缓存回灌=缺省 / 默认值覆盖）。 */
void XModbusHub_setOutputSyncMode(XModbusHub* hub, XModbusHub_OutputSyncMode mode)
{
    if (!hub) return;
    if (mode != XModbusHub_OutputSync_Cache && mode != XModbusHub_OutputSync_Default)
        return;
    hub->m_outputSyncMode = (uint16_t)mode;
}

XModbusHub_OutputSyncMode XModbusHub_outputSyncMode(const XModbusHub* hub)
{
    if (!hub) return XModbusHub_OutputSync_Cache;
    return (hub->m_outputSyncMode == (uint16_t)XModbusHub_OutputSync_Default)
               ? XModbusHub_OutputSync_Default
               : XModbusHub_OutputSync_Cache;
}

/** @brief 设备状态变化（connect_2：第一参=真 sender=client）。 */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args)
{
    if (!sender || !args) return;
    XVarList_args_1(args, int, newStateInt);
    {
        XModbusHub* hub = (XModbusHub*)XObject_parent(sender);
        if (!hub || !XClassGetVtable(hub) ||
            XClassGetVtable(hub) != XModbusHub_class_init()) return;
        if (!hub->m_devices) return;
        for (size_t i = 0; i < XVector_size_base(hub->m_devices); ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            const XModbusDevice_State newState = (XModbusDevice_State)newStateInt;
            if (!d || (XObject*)d->client != sender) continue;
            {
                const XModbusDevice_State old = d->state;
                if (old == newState) return;
                d->state = newState;
            }
            XModbusHub_deviceStateChanged_signal(hub, d->id, newStateInt);
            if (newState == XModbusDevice_ConnectedState) {
                ++d->reconnectCount;
                /* [输出上线对齐] 未轮询输出区先标脏（初装/最后确认值），
                 * 随下面的泵与写队列同周期一帧统一下发。 */
                hubPushUnpolledOutputs(hub, d);
                /* 恢复在线：立即续泵轮询 + 补写滞留脏区间（不等下一拍）。 */
                hubPumpDevice(hub, d);
                hubPumpWriteQueue(hub);
                XModbusHub_deviceStatsChanged_signal(hub, d->id);
            }
            return;
        }
    }
}

/**
 * @brief 应答完成（connect_2：第一参=真 sender=reply；经 parent 反查 hub，
 *        按 inflight 指针归属设备；单飞收口后续泵）。
 */
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args)
{
    XModbusReply* reply = (XModbusReply*)sender;
    XModbusHub* hub;
    XModbusHubDevice* dev = NULL;
    (void)args;
    if (!reply) return;
    hub = (XModbusHub*)XObject_parent((XObject*)reply);
        if (!hub || !XClassGetVtable(hub) ||
        XClassGetVtable(hub) != XModbusHub_class_init()) {
        XObject_deleteLater((XObject*)reply);
        return;
    }
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (d && d->inflight == reply) { dev = d; break; }
        }
    }
        if (!dev) {
        /* 非 Hub 在途登记的 Reply（如设备被移除前的遗留）：直接回收。 */
        XObject_deleteLater((XObject*)reply);
        return;
    }

    {
        const uint64_t now = XDateTime_currentMSecsSinceEpoch();
        bool ok = (XModbusReply_error(reply) == XModbusDevice_NoError);

        /* [异常帧＝失败 2026-11-09 框架级根修]
         *
         * Modbus 的"异常应答"是**失败**：功能码|0x80 + 1 字节异常码。
         * 但 XModbusVirtualClient 只把异常编进 PDU，`reply->m_error` 始终
         * 是 NoError（见 XModbusVirtualClient.c 的 virtualBuildException
         * 分支）⇒ 原口径把它当**成功**：
         *   · lastSuccessMs 照常推进 ⇒ 应用层看门狗判"健康"、永不触发；
         *   · 但 m_result 为 NULL ⇒ 不回填缓存、valueChanged 不发射
         *     ⇒ 上层按"值陈旧"判 ⇒ 界面"无数据"；
         *   · pollOkCount 虚增、pollErrCount 不动 ⇒ 统计上看不出异常。
         * 现场实证（C 模拟服务端）：浊度从站的读组一旦回异常帧，设备就永久
         * "在线但无数据"，看门狗全程无告警，只有重启服务端能恢复。
         *
         * 口径：异常帧一律按失败记账（pollErrCount++、从站退避表报失败、
         * lastSuccessMs 不推进），让上层与看门狗都能看见。 */
        /* [异常应答不牵连整站 2026-11-10 框架级根修]
         * 异常帧＝"这个地址不支持"（EXC02 等），是**组级**事实；把它按
         * 从站级失败回报退避表，会让**同一从站的所有组**一起被静默
         * ⇒ 现场表现"一台设备整体离线"，而真机只是其中一组不支持
         * （实测：M512 对 0xFA00×5 无应答、C 端却把整台变频器判离线）。
         * 处置：异常帧仍计 pollErrCount（可观测），但**不进从站退避**，
         * 该组下轮自然重试；其余组照常轮询。 */
        bool isExceptionReply = false;
        if (ok) {
            const XModbusResponse* raw = XModbusReply_rawResult_const(reply);
            if (raw && XModbusPdu_isException((const XModbusPdu*)&raw->m_base)) {
                ok = false;
                isExceptionReply = true;
                /* [可观测] 限频打印：哪一组的读组、远端地址、异常码。
                 * [为什么必须留这行] 异常帧此前既不计数也不打印，现场只能
                 * 看到"没数据"，查不到是哪一组的哪条命令被拒。 */
                if (dev->pollErrCount <= 3) {
                    XPrintf("XModbusHub: 设备 %d 收到异常应答（组 unit=%d 起始=%u "
                            "数量=%u 异常码=%d）——按失败记账\n",
                            dev->id, dev->inflightPayload.group.unitId,
                            (unsigned)dev->inflightPayload.group.remoteStart,
                            (unsigned)dev->inflightPayload.group.count,
                            (int)XModbusPdu_exceptionCode(
                                (const XModbusPdu*)&raw->m_base));
                }
            }
        }

        dev->lastTxOk = ok;   /* 饥饿交替判据：失败期读写交替让路（见泵①）。 */
        /* [发帧前静默闸的记账点] 收口（成败都算）＝「上一帧把总线驱动完」
         * 的时刻；从站级 m_preTxQuietMs 按它判恢复期（异步与同步终态两条
         * 收口路径都经过本函数，一处记账全覆盖）。 */
        dev->lastTxEndMs = now;

        if (dev->inflightKind == XModbusHubInflight_Poll) {
            /* [2026-10-28 单元退避] 组/从站定位提前到成败分支之前，结果先
             * 回报退避表（lost 跃迁沿发 unitLost/unitAlive）。 */
            const XModbusHubReadGroup group = dev->inflightPayload.group;
            const int unitId = group.unitId;
            const bool wasLost = (unitId >= 0) &&
                XPollScheduler_isLostKey(&dev->unitSched, unitId);
            const bool enteredMute = (unitId >= 0) && !isExceptionReply &&
                XPollScheduler_reportResultKey(&dev->unitSched, unitId, ok, now);
            if (ok) {
                const XModbusDataUnit* result = XModbusReply_result_const(reply);
                if (result)
                    hubFillGroupResult(hub, dev, &group, result);
                ++dev->pollOkCount;
                dev->lastSuccessMs = now;
                if (wasLost)
                    XModbusHub_unitAlive_signal(hub, dev->id, unitId);
            } else {
                ++dev->pollErrCount; /* 不风暴：静默计数，下轮计划重试该组。 */
                /* [失败可观测 2026-11-09] 失败此前**完全静默**（只加计数），
                 * 现场只表现为"某台从站的数据不再刷新"，查不到是哪一组、哪条
                 * 命令、什么错误码。限频打印（每设备每秒一条）——用时间限频而
                 * 不是失败次数：同一 Hub 上多个从站共用计数，先出错的从站会把
                 * 次数额度吃光，后面那个真正停摆的从站反而永远打不出来。 */
                if (dev->lastErrLogMs == 0 ||
                    now - dev->lastErrLogMs >= 1000u) {
                    /* [所有权 2026-11-09 修] errorString() 按契约返回**堆副本**
                     * （调用方 XClassDelete），此前在 printf 实参里调了两次、
                     * 副本全没人释放 ⇒ 每条失败日志漏 2×sizeof(XString)=192B
                     * （回归 §G 实测按轮线性上涨）。另外 `%s` 只认 char*：直接
                     * 传 XString* 打印的是结构体字节（现场日志"错误="后面是
                     * 乱码）。故：只取一次副本 → 取 UTF-8 借用视图（XString
                     * 内部缓存，随副本释放）→ 打完释放副本。 */
                    XString* errStr = XModbusReply_errorString(reply);
                    /* toUtf8 对空串返回 NULL（XString_isEmpty_base 早退），
                     * 空错误文本一律显示 "-"，不能落到 printf 的 "(null)"。 */
                    const char* errUtf8 = errStr ? XString_toUtf8(errStr) : NULL;
                    dev->lastErrLogMs = now;
                    XPrintf("XModbusHub: 设备 %d 读组失败（unit=%d 类型=%d 起始=%u "
                            "数量=%u 错误码=%d 错误=%s）\n",
                            dev->id, unitId, (int)group.type,
                            (unsigned)group.remoteStart, (unsigned)group.count,
                            (int)XModbusReply_error(reply),
                            errUtf8 ? errUtf8 : "-");
                    if (errStr) XClassDelete(errStr);
                }
                if (enteredMute && !wasLost)
                    XModbusHub_unitLost_signal(hub, dev->id, unitId);
                if (group.firstBlock < XVector_size_base(dev->blocks)) {
                    XModbusHubCacheBlock* b =
                        *(XModbusHubCacheBlock**)XVector_at_base(dev->blocks,
                                                                 (int64_t)group.firstBlock);
                    if (b) ++b->failCount;
                }
            }
            XModbusHub_deviceStatsChanged_signal(hub, dev->id);
        } else if (dev->inflightKind == XModbusHubInflight_Write) {
            const XModbusHubInflightWrite snap = dev->inflightPayload.write;
            XModbusHubCacheBlock* b = hubFindBlockAt(dev, snap.type, snap.unitId,
                                                     snap.remoteStart);
            if (ok) {
                dev->lastSuccessMs = now;
                if (b) {
                    /* 逐点比较：缓存值 != 已发快照 ⇒ 在途期间又被改过，重新标脏。 */
                    const uint16_t off = (uint16_t)(snap.remoteStart - b->remoteStart);
                    uint16_t i = 0;
                    hubDirtySubtract(b, off, snap.count);
                    while (i < snap.count) {
                        uint16_t j;
                        bool differs;
                        if (!snap.values) break;
                        differs = (XModbusDataUnit_value(b->data, off + i) != snap.values[i]);
                        if (!differs) { ++i; continue; }
                        j = i;
                        while (j < snap.count && snap.values &&
                               XModbusDataUnit_value(b->data, off + j) != snap.values[j])
                            ++j;
                        {
                            XModbusHubDirtyRange r;
                            memset(&r, 0, sizeof(r));
                            r.offset = (uint16_t)(off + i);
                            r.count = (uint16_t)(j - i);
                            hubDirtyPushRaw(b, &r);
                        }
                        i = j;
                    }
                }
                hubEmitWriteDone(hub, snap.type, snap.virtualStart,
                                 snap.virtualCount, true);
            } else if (b) {
                /* 失败：脏区间原样保留，重发定时器周期重试；提示沿由发送端
                 * 按 attempts 阈值发过一次，这里不重复。 */
            }
            dev->inflightPayload.write.values = NULL;   /* 快照内存随收口释放。 */
            if (snap.values) XFree_System(snap.values);
            memset(&dev->inflightPayload.write, 0, sizeof(dev->inflightPayload.write));
            XModbusHub_deviceStatsChanged_signal(hub, dev->id);
        }

        dev->inflight = NULL;
        dev->inflightKind = XModbusHubInflight_None;
        XObject_deleteLater((XObject*)reply);
        /* [2026-10-26 节拍根修] 收口只补写、不续轮询：若在此续泵，计划轮跑完
         * 回卷后立刻又发下一轮（实测 2ms/请求、tid 数千的轮询风暴，回填在
         * 风暴中概率性失败）。轮询节奏一律交还设备周期定时器——pollInterval
         * 语义=段发送节拍。写意图仍即时续泵（写延迟优先）。
         *
         * [2026-11-09 落实] 原代码调的是写+轮询的统一泵 ⇒ 注释说的"只补写"
         * 实际没做到（写分支被跳过时就会再发一帧读）。现在：
         *   · allowPoll=false ⇒ 收口路径**只发写**；
         *   · 深度上界：嵌套收口（depth>=2）不再泵 ⇒ 脏区间永不消失时
         *     不会 close→write→close→write 栈递归、也不会总线写风暴。
         *
         * [失败不在同拍重试 2026-11-09] 若上一笔**失败**（写被从站拒了）
         * 还在同拍立刻重发，同一个脏区间会被写两遍 ⇒「读写交替让路」的
         * 交替计数同拍加两次、奇偶不变 ⇒ **读永远轮不到**（单飞槽被写占
         * 满）⇒ 该从站的数据再也不刷新（现场 485 停摆的一种成因）。
         * 失败的重试交给退避定时器（写重发拍），同拍不再补发。 */
        if (hub->m_syncFinishDepth <= 1 && dev->lastTxOk &&
            hubDeviceHasDirty(dev))
            hubPumpDeviceEx(hub, dev, false);
    }
}

/* ==================== 信号 ==================== */

void* XModbusHub_deviceStateChanged_signal(XModbusHub* hub, int deviceId, int state)
{
    XEmitSignal(hub, XModbusHub_deviceStateChanged_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_valueChanged_signal(XModbusHub* hub, XModbusRegisterType type,
                                     int virtualStart, int count)
{
    int typeInt = (int)type; /* 槽侧提取口径保持 int（XVarList_arg(args, int)）。 */
    XEmitSignal(hub, XModbusHub_valueChanged_signal,
        XVarList_Create(XVar(int, typeInt), XVar(int, virtualStart), XVar(int, count)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_writeCompleted_signal(XModbusHub* hub, XModbusRegisterType type,
                                       int virtualStart, int count, bool ok)
{
    int typeInt = (int)type; /* 槽侧提取口径保持 int（XVarList_arg(args, int)）。 */
    XEmitSignal(hub, XModbusHub_writeCompleted_signal,
        XVarList_Create(XVar(int, typeInt), XVar(int, virtualStart), XVar(int, count),
                        XVar(bool, ok)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_deviceStatsChanged_signal(XModbusHub* hub, int deviceId)
{
    XEmitSignal(hub, XModbusHub_deviceStatsChanged_signal,
        XVarList_Create(XVar(int, deviceId)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_unitLost_signal(XModbusHub* hub, int deviceId, int unitId)
{
    XEmitSignal(hub, XModbusHub_unitLost_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, unitId)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_unitAlive_signal(XModbusHub* hub, int deviceId, int unitId)
{
    XEmitSignal(hub, XModbusHub_unitAlive_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, unitId)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_writeStalled_signal(XModbusHub* hub, XModbusRegisterType type,
                                     int virtualStart, int count, int attempts)
{
    int typeInt = (int)type; /* 槽侧提取口径保持 int（XVarList_arg(args, int)）。 */
    XEmitSignal(hub, XModbusHub_writeStalled_signal,
        XVarList_Create(XVar(int, typeInt), XVar(int, virtualStart), XVar(int, count),
                        XVar(int, attempts)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_HUB_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
