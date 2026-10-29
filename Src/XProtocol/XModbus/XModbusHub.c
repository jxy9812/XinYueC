/**
 * @file       XModbusHub.c
 * @brief      Modbus 聚合设备实现（多从站虚拟合一）。
 * @details    结构总览：
 *               - m_devices：XModbusHubDevice 值数组（XVector），设备句柄=id
 *                 字段线性查找（句柄不复用，删除即摘除槽位）。
 *               - 所有权走 XObject 父子机制（框架惯例，析构自动删 children）：
 *                 addDevice 把 client reparent 到 hub；每次发起请求把 Reply
 *                 parent 到 hub。设备槽内只挂借用指针与非 XObject 资源。
 *               - m_cache[4]：统一缓存，attach/refresh 映射时按每类型虚拟
 *                 跨度建 XModbusDataUnit（位区自动走 XBitArray）。
 *               - 轮询：每设备一个 XObject 自带周期定时器（定时器使用规范
 *                 2026-10-26：不内嵌独立 XTimer）；拍内按计划单飞逐段
 *                 sendReadRequest，应答经映射回填缓存（变化检测后发
 *                 valueChanged，type 字段区分来源寄存器类别）。
 *                 [2026-10-28 单元退避] 每设备内嵌一张 XPoll/XPollScheduler
 *                 退避表（按 plan 内 unitId 去重簿记）：连续失败达阈值
 *                 静默该从站（跳过其段，把总线让给活从站），到期自动探活；
 *                 跃迁沿发 unitLost/unitAlive。
 *               - 写：乐观更新缓存 → 写意图入队 → 设备空闲即时下发（写优先
 *                 于轮询）；失败滞留队列由 m_retryTimer 周期重试，成功出队发
 *                 writeCompleted(ok=true)，永不终局失败（用户裁定语义）。
 *                 [2026-10-28] 写下发统一走 High 优先级（传输层插队）；
 *                 attempts 达阈值（setWriteStallNotice，默认 3）发一次
 *                 writeStalled 提示。
 *               - 设备状态：connect_2 跟踪（槽第一参=真 sender=client，经
 *                 parent 反查 hub 后按指针定设备），恢复 Connected 时
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
#include "XMemory.h"
#include "XVector.h"
#include "XString.h"
#include "XDateTime.h"
#include <stdio.h>
#include <string.h>

/* ==================== 槽前置声明 ==================== */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args);
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args);
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev);
static void hubPumpWriteQueue(XModbusHub* hub);
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev); /* [2026-10-28 单元退避] */

/* [A3 时序缺口兜底] 同步终态收口重入护栏：收口尾部续泵理论上可经防御性
 * 忙分支再次同步终态（可达路径均被连接态门槛挡住），护栏兜底防栈爆；
 * 重入层改为就地释放+清 inflight（语义良性：意图留队下拍重试，无滞留）。 */
static int s_hubInSyncFinish = 0;

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

/* [2026-10-26 越界根修] 同 Mapping：枚举含 Invalid=0，四类区=1..4，
 * 缓存 m_cache[4] 一律经本函数取稠密下标 0..3。 */
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

/**
 * @brief 重建某类型统一缓存：按映射该类型虚拟跨度 [minV, maxV)。
 * @details 已有缓存与新跨度求交集保留旧值（refreshMapping 语义）；
 *          未回填区初始化 0xFFFF 哨兵（读到哨兵=尚未轮询到）。
 */
static bool hubEnsureCache(XModbusHub* hub, int typeIdx)
{
    const XModbusMapping* map = hub->m_mapping;
    const size_t n = XModbusMapping_rangeCount_const(
        map, (XModbusRegisterType)hubEnumOfType(typeIdx));
    if (n == 0) {
        if (hub->m_cache[typeIdx]) {
            XClassDelete(hub->m_cache[typeIdx]);
            hub->m_cache[typeIdx] = NULL;
        }
        hub->m_cacheStart[typeIdx] = 0;
        hub->m_cacheCount[typeIdx] = 0;
        return true;
    }
    uint32_t minV = 0xFFFFu + 1u;
    uint32_t maxV = 0;
    for (size_t i = 0; i < n; ++i) {
        const XModbusMapRange* r =
            XModbusMapping_rangeAt_const(map, (XModbusRegisterType)hubEnumOfType(typeIdx), i);
        if (!r) continue;
        if (r->virtualStart < minV) minV = r->virtualStart;
        if ((uint32_t)r->virtualStart + r->count > maxV)
            maxV = (uint32_t)r->virtualStart + r->count;
    }
    const uint16_t count = (uint16_t)(maxV - minV);
    if (hub->m_cache[typeIdx] &&
        hub->m_cacheStart[typeIdx] == minV && hub->m_cacheCount[typeIdx] == count)
        return true; /* 跨度未变：复用。 */

    /* 快照交集 → 重建 → 写回。 */
    const uint16_t oldStart = hub->m_cacheStart[typeIdx];
    const uint16_t oldCount = hub->m_cacheCount[typeIdx];
    uint16_t* snapshot = NULL;
    if (hub->m_cache[typeIdx] && oldCount > 0) {
        snapshot = (uint16_t*)XMalloc_System(sizeof(uint16_t) * oldCount);
        if (snapshot) {
            for (uint16_t i = 0; i < oldCount; ++i)
                snapshot[i] = XModbusDataUnit_value(hub->m_cache[typeIdx], i);
        }
    }
    if (hub->m_cache[typeIdx]) {
        XClassDelete(hub->m_cache[typeIdx]);
        hub->m_cache[typeIdx] = NULL;
    }
    hub->m_cache[typeIdx] = XModbusDataUnit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XModbusRegisterType)hubEnumOfType(typeIdx),
        0, count);
    if (!hub->m_cache[typeIdx]) {
        hub->m_cacheStart[typeIdx] = 0;
        hub->m_cacheCount[typeIdx] = 0;
        if (snapshot) XFree_System(snapshot);
        return false;
    }
    hub->m_cacheStart[typeIdx] = minV;
    hub->m_cacheCount[typeIdx] = count;
    /* [2026-10-26 哨兵口径] 位区（DI/Coils）初值必须 0：setValue 对位单元
     * 只留 bit0，0xFFFF 会截断成 1——「全 1 初值」曾伪装成已回填数据
     * （闭环测试 DI 位图全 1 假象实证）。寄存器区仍用 0xFFFF 哨兵。 */
    {
        /* 稠密下标域内比较（此前误与枚举常量比较=恒假，位区哨兵从未生效）。 */
        const int enumType = hubEnumOfType(typeIdx);
        const uint16_t initVal = (enumType == (int)XModbusDiscreteInputs ||
                                  enumType == (int)XModbusCoils) ? 0u : 0xFFFFu;
        for (uint16_t i = 0; i < count; ++i)
            XModbusDataUnit_setValue(hub->m_cache[typeIdx], i, initVal);
    }
    if (snapshot) {
        for (uint16_t i = 0; i < oldCount; ++i) {
            const uint32_t v = (uint32_t)oldStart + i;
            if (v >= minV && v < minV + count)
                XModbusDataUnit_setValue(hub->m_cache[typeIdx],
                                         (size_t)(v - minV), snapshot[i]);
        }
        XFree_System(snapshot);
    }
    return true;
}

/** @brief 重建全部缓存 + 各设备轮询计划（计划=该设备在轮询区掩码内的映射段）。 */
static void hubRebuildAll(XModbusHub* hub)
{
    for (int t = 0; t < 4; ++t) (void)hubEnsureCache(hub, t);
    if (!hub->m_devices || !hub->m_mapping) return;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        if (!d->plan) {
            d->plan = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        sizeof(XModbusMapSegment), false);
        }
        if (d->plan) XVector_clear_base(d->plan);
        if (!d->plan) continue;
        for (int t = 0; t < 4; ++t) {
            if (!(hub->m_pollAreas & (1u << hubEnumOfType(t)))) continue;
            XModbusMapRange ranges[64];
            const int got = XModbusMapping_segmentsForDevice_const(
                hub->m_mapping, d->id, (XModbusRegisterType)hubEnumOfType(t),
                ranges, 64);
            const int total = (got > 0) ? ((got > 64) ? 64 : got) : 0;
            for (int k = 0; k < total; ++k) {
                XModbusMapSegment seg;
                memset(&seg, 0, sizeof(seg));
                seg.type = (uint16_t)hubEnumOfType(t); /* 枚举值（发送/回填统一口径） */
                seg.range = ranges[k];
                seg.virtualStart = ranges[k].virtualStart;
                seg.remoteStart = ranges[k].remoteStart;
                seg.count = ranges[k].count;
                XVector_push_back_1_base(d->plan, &seg);
            }
        }
        d->planCursor = 0;
        hubRebuildUnitSched(hub, d);   /* [2026-10-28 单元退避] 健康表随计划重建。 */
    }
}

/** @brief 发射值变化信号。 */
static void hubEmitValueChanged(XModbusHub* hub, int typeIdx,
                                uint16_t vStart, uint16_t count)
{
    XModbusHub_valueChanged_signal(hub, typeIdx, (int)vStart, (int)count);
}

/**
 * @brief 把远端结果回填统一缓存（带变化检测）。
 * @return true=至少一个值变化；changedOnly=false 时恒 true（恒通知）。
 */
static bool hubFillFromResult(XModbusHub* hub, int enumType,
                              uint16_t vStart, const XModbusDataUnit* result)
{
    const int typeIdx = hubTypeIndex(enumType);
    if (typeIdx < 0 || !hub->m_cache[typeIdx] || !result) return false;
    const size_t got = XModbusDataUnit_valueCount(result);
    const uint32_t cacheEnd = (uint32_t)hub->m_cacheStart[typeIdx] +
                              hub->m_cacheCount[typeIdx];

    bool changed = false;
    for (size_t i = 0; i < got; ++i) {
        const uint32_t v = (uint32_t)vStart + i;
        if (v < hub->m_cacheStart[typeIdx] || v >= cacheEnd) continue;
        const size_t idx = (size_t)(v - hub->m_cacheStart[typeIdx]);
        const uint16_t newVal = XModbusDataUnit_value(result, i);
        const uint16_t oldVal = XModbusDataUnit_value(hub->m_cache[typeIdx], idx);
        if (newVal != oldVal) {
            XModbusDataUnit_setValue(hub->m_cache[typeIdx], idx, newVal);
            changed = true;
        }
    }

    if (!hub->m_changedOnlySignals || changed)
        hubEmitValueChanged(hub, hubEnumOfType(typeIdx), vStart, (uint16_t)got);
    return changed;
}

/** @brief 找该设备队列首条写意图（先到先服务）。 */
static XModbusHubWriteIntent* hubNextWriteForDevice(XModbusHub* hub, XModbusHubDevice* dev)
{
    if (!hub->m_writeQueue) return NULL;
    const size_t qn = XVector_size_base(hub->m_writeQueue);
    for (size_t i = 0; i < qn; ++i) {
        XModbusHubWriteIntent* it =
            (XModbusHubWriteIntent*)XVector_at_base(hub->m_writeQueue, (int64_t)i);
        if (it && it->deviceId == dev->id) return it;
    }
    return NULL;
}

/** @brief 摘除队列中与快照匹配的首条意图（写成功收口用）。 */
static bool hubRemoveWriteIntent(XModbusHub* hub, const XModbusHubWriteIntent* snap)
{
    if (!hub->m_writeQueue) return false;
    const size_t qn = XVector_size_base(hub->m_writeQueue);
    for (size_t i = 0; i < qn; ++i) {
        const XModbusHubWriteIntent* it =
            (const XModbusHubWriteIntent*)XVector_at_base(hub->m_writeQueue, (int64_t)i);
        if (it && it->deviceId == snap->deviceId && it->type == snap->type &&
            it->virtualStart == snap->virtualStart && it->count == snap->count) {
            XVector_remove_base(hub->m_writeQueue, (int64_t)i, 1);
            return true;
        }
    }
    return false;
}

/* ==================== 发送（轮询/写） ==================== */

/**
 * @brief [2026-10-28 单元退避] 计划段对应从站是否静默中（退避表查询）。
 * @return 静默返回 true；段越界/未登记返回 false（不拦截）。
 */
static bool hubSegmentMuted(XModbusHubDevice* dev, size_t segIndex, uint64_t nowMs)
{
    const XModbusMapSegment* seg;
    if (!dev || !dev->plan || segIndex >= XVector_size_base(dev->plan)) return false;
    seg = (const XModbusMapSegment*)XVector_at_base(dev->plan, (int64_t)segIndex);
    if (!seg) return false;
    return XPollScheduler_isMutedKey(&dev->unitSched, seg->range.unitId, nowMs);
}

/**
 * @brief [2026-10-28 单元退避] 计划重建后同步设备退避表：清表重建，按计划
 *        内 unitId 去重登记簿记条目（阈值/时长沿用 Hub 配置）。
 */
static void hubRebuildUnitSched(XModbusHub* hub, XModbusHubDevice* dev)
{
    size_t i;
    size_t planLen;
    if (!hub || !dev) return;
    XPollScheduler_deinit(&dev->unitSched);
    XPollScheduler_init(&dev->unitSched);
    XPollScheduler_setBackoff(&dev->unitSched, hub->m_unitMaxFails, hub->m_unitMuteMs);
    if (!dev->plan) return;
    planLen = XVector_size_base(dev->plan);
    for (i = 0; i < planLen; ++i) {
        const XModbusMapSegment* seg =
            (const XModbusMapSegment*)XVector_at_base(dev->plan, (int64_t)i);
        if (seg) (void)XPollScheduler_registerKey(&dev->unitSched, seg->range.unitId, 0);
    }
}

/**
 * @brief 发一段轮询读。请求单元为栈上临时对象——sendReadRequest 经
 *        createReply 深拷贝收编，返回后即可析构。
 * @return true=已上线（应答异步回 onReplyFinished）；false=未受理。
 */
static bool hubSendPollSegment(XModbusHub* hub, XModbusHubDevice* dev, size_t segIndex)
{
    if (!dev->plan || !dev->client) return false;
    const XModbusMapSegment* seg =
        (const XModbusMapSegment*)XVector_at_base(dev->plan, (int64_t)segIndex);
    if (!seg || seg->count == 0) return false;

    XModbusDataUnit unit;
    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)seg->type,
                            seg->remoteStart, seg->count);
    XModbusReply* reply = XModbusClient_sendReadRequest(
        dev->client, &unit, seg->range.unitId, XModbusRequestPriority_Normal);
    XClassDeinit(&unit);
    if (!reply) return false;

    XObject_setParent((XObject*)reply, (XObject*)hub);
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Poll;
    dev->inflightSegIndex = segIndex;
    /* [A3 时序缺口兜底] reply 可能在 send 内部即同步终态（未连接/io 关闭/
     * 忙/空 PDU/发送失败等收口分支，setError 同步发射 finished）——此刻
     * finished 已发射给空监听集，connect_2 再挂也收不到。若不就地收口，
     * dev->inflight 永卡：状态变化槽不清 inflight、重连续泵被单飞闸挡死
     * →该设备轮询永久停摆。先登记再直调收口槽＝与异步收口完全等价的路径
     * （退避记账/回填/统计/续泵全不走样）。递归有界：首个同步写/发失败已
     * 把设备拉成 Unconnected，收口尾部续泵会被 hubPumpDevice 的连接态
     * 门槛挡住。 */
    if (XModbusReply_isFinished(reply)) {
        if (!s_hubInSyncFinish) {
            s_hubInSyncFinish = 1;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            s_hubInSyncFinish = 0;
        } else {
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

/** @brief 发一条写意图。 @return true=已上线。 */
static bool hubSendWriteIntent(XModbusHub* hub, XModbusHubDevice* dev,
                               const XModbusHubWriteIntent* intent)
{
    if (!dev || !dev->client || !intent || intent->count == 0) return false;
    XModbusDataUnit unit;
    XModbusDataUnit_init_ex(&unit, (XModbusRegisterType)intent->type,
                            intent->remoteStart, intent->count);
    for (uint16_t k = 0; k < intent->count; ++k)
        XModbusDataUnit_setValue(&unit, k, intent->values[k]);
    /* [2026-10-28 写优先级] 写走 High 队列在传输层插队（轮询读保持 Normal）；
     * 即时下发与滞留重发共用本入口，一处收口。 */
    XModbusReply* reply = XModbusClient_sendWriteRequest(
        dev->client, &unit, intent->unitId, XModbusRequestPriority_High);
    XClassDeinit(&unit);
    if (!reply) return false;

    XObject_setParent((XObject*)reply, (XObject*)hub);
    /* 单飞登记：快照意图（队列重排/摘除不影响完成路径）。 */
    dev->inflight = reply;
    dev->inflightKind = XModbusHubInflight_Write;
    dev->inflightWrite = *intent;
    /* [A3 时序缺口兜底] 同 hubSendPollSegment：send 内部同步终态就地带
     * inflight 登记直调收口槽（写意图按成败出队/留队 + 续泵等价），防
     * dev->inflight 永卡单飞。 */
    if (XModbusReply_isFinished(reply)) {
        if (!s_hubInSyncFinish) {
            s_hubInSyncFinish = 1;
            XModbusHub_onReplyFinished((XObject*)reply, NULL);
            s_hubInSyncFinish = 0;
        } else {
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
 * @brief 统一泵：设备空闲（单飞未占用且在线）时——先写后轮询（写延迟优先）。
 *        轮询拍/重连恢复/写入队/重发拍/应答收口共用此入口。
 */
static void hubPumpDevice(XModbusHub* hub, XModbusHubDevice* dev)
{
    if (!hub || !dev || !dev->client) return;
    if (dev->inflight) return;                                   /* 单飞占用。 */
    if (dev->state != XModbusDevice_ConnectedState) return;      /* 离线安静跳过。 */

    /* ① 写优先：该设备有待发写意图。 */
    XModbusHubWriteIntent* it = hubNextWriteForDevice(hub, dev);
    if (it) {
        ++it->attempts;
        /* [2026-10-28 写滞留可见性] attempts 首次达阈值发一次提示
         * （stallNotified 护栏保证每意图至多一次；重发语义不变）。 */
        if (!it->stallNotified && hub->m_writeStallNoticeAt >= 1 &&
            it->attempts >= (uint32_t)hub->m_writeStallNoticeAt) {
            it->stallNotified = true;
            XModbusHub_writeStalled_signal(hub, (int)it->type,
                                           (int)it->virtualStart,
                                           (int)it->count, (int)it->attempts);
        }
        hubSendWriteIntent(hub, dev, it);
        return;
    }
    /* ② 轮询：总闸开且计划非空；planCursor 回卷。[2026-10-28 单元退避]
     *    静默从站的段跳过——RTU 共口多从站时把总线让给活从站（死从站
     *    不再占用单飞槽吃超时），静默到期自动恢复探活；全部从站静默则
     *    本拍让总线。 */
    if (!hub->m_polling || !dev->plan) return;
    {
        const size_t planLen = XVector_size_base(dev->plan);
        const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        size_t scanned = 0;
        if (planLen == 0) return;
        if (dev->planCursor >= planLen) dev->planCursor = 0;
        while (scanned < planLen && hubSegmentMuted(dev, dev->planCursor, nowMs)) {
            dev->planCursor = (dev->planCursor + 1) % planLen;
            ++scanned;
        }
        if (scanned >= planLen) return;                          /* 全静默：本拍不发。 */
    }
    if (hubSendPollSegment(hub, dev, dev->planCursor))
        dev->planCursor++;                                       /* 在途；完成槽再续泵。 */
}

/** @brief 写队列泵：逐设备尝试下发（空闲设备才发）。 */
static void hubPumpWriteQueue(XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d) continue;
        if (d->inflight) continue;
        if (d->state != XModbusDevice_ConnectedState) continue;
        if (hubNextWriteForDevice(hub, d)) hubPumpDevice(hub, d);
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
    if (hub->m_devices) {
        const size_t n = XVector_size_base(hub->m_devices);
        for (size_t i = 0; i < n; ++i) {
            XModbusHubDevice* d =
                (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
            if (!d) continue;
            hubStopDevicePollTimer(hub, d);
            d->inflight = NULL;          /* Reply 是 hub 子对象，父析构统一收。 */
            d->client = NULL;            /* client 是 hub 子对象，父析构统一收。 */
            if (d->name) {
                XClassDelete(d->name);
                d->name = NULL;
            }
            if (d->plan) {
                XClassDelete(d->plan);
                d->plan = NULL;
            }
            XPollScheduler_deinit(&d->unitSched); /* [2026-10-28 单元退避] 纯数据，清零即可。 */
        }
        XClassDelete(hub->m_devices);
        hub->m_devices = NULL;
    }
    if (hub->m_writeQueue) {
        XClassDelete(hub->m_writeQueue);
        hub->m_writeQueue = NULL;
    }
    if (hub->m_mapping) {
        XClassDelete(hub->m_mapping);
        hub->m_mapping = NULL;
    }
    for (int t = 0; t < 4; ++t) {
        if (hub->m_cache[t]) {
            XClassDelete(hub->m_cache[t]);
            hub->m_cache[t] = NULL;
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
        hub->m_cache[t] = NULL;
        hub->m_cacheStart[t] = 0;
        hub->m_cacheCount[t] = 0;
    }
    hub->m_pollIntervalDefaultMs = 1000;
    /* 默认只轮询输入区（DI|IR）——效率最大化：输出区以写回显为准，
     * 不花流量读回。setPollAreas 可放宽。 */
    hub->m_pollAreas = (1u << XModbusDiscreteInputs) | (1u << XModbusInputRegisters);
    hub->m_polling = false;
    hub->m_writeQueue = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          sizeof(XModbusHubWriteIntent), false);
    hub->m_retryTimer = XTIMER_INVALID_ID;
    hub->m_writeRetryIntervalMs = 1000;
    hub->m_changedOnlySignals = true;
    /* [2026-10-28 单元退避/写滞留可见性] 缺省对齐 XPollScheduler 缺省值。 */
    hub->m_unitMaxFails = XPOLL_SCHED_DEFAULT_MAX_FAILS;
    hub->m_unitMuteMs = XPOLL_SCHED_DEFAULT_MUTE_MS;
    hub->m_writeStallNoticeAt = 3;
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

int XModbusHub_addDevice(XModbusHub* hub, XModbusClient* client, const char* name)
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
    dev.plan = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                 sizeof(XModbusMapSegment), false);
    if (name && name[0]) dev.name = XString_create_utf8(name);

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
        if (dev.plan) XClassDelete(dev.plan);
        return -1;
    }
    hubRebuildAll(hub);
    return dev.id;
}

bool XModbusHub_removeDevice(XModbusHub* hub, int deviceId)
{
    XModbusHubDevice* dev = hubDeviceById(hub, deviceId);
    if (!dev) return false;
    hubStopDevicePollTimer(hub, dev);
    /* 滞留写意图作废：writeCompleted(ok=false) 告知调用方。 */
    if (hub->m_writeQueue) {
        const size_t qn = XVector_size_base(hub->m_writeQueue);
        for (size_t i = qn; i > 0; --i) {
            XModbusHubWriteIntent* it =
                (XModbusHubWriteIntent*)XVector_at_base(hub->m_writeQueue, (int64_t)(i - 1));
            if (it && it->deviceId == deviceId) {
                XModbusHub_writeCompleted_signal(hub, (int)it->type,
                                                 (int)it->virtualStart,
                                                 (int)it->count, false);
                XVector_remove_base(hub->m_writeQueue, (int64_t)(i - 1), 1);
            }
        }
    }
    if (dev->inflight) {
        XObject_deleteLater((XObject*)dev->inflight);
        dev->inflight = NULL;
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
    if (dev->plan) {
        XClassDelete(dev->plan);
        dev->plan = NULL;
    }
    XPollScheduler_deinit(&dev->unitSched); /* [2026-10-28 单元退避] 槽位摘除前清表。 */
    const int idx = hubDeviceIndexById(hub, deviceId);
    if (idx >= 0) XVector_remove_base(hub->m_devices, idx, 1);
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
    const size_t n = XVector_size_base(hub->m_devices);
    if (n == 0) return false;
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d || d->state != XModbusDevice_ConnectedState) return false;
    }
    return true;
}

bool XModbusHub_connectAll(XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return false;
    bool all = true;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d || !d->client) continue;
        if (!XModbusDevice_connectDevice((XModbusDevice*)d->client)) all = false;
    }
    return all;
}

void XModbusHub_disconnectAll(XModbusHub* hub)
{
    if (!hub || !hub->m_devices) return;
    XModbusHub_stopPolling(hub);
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d && d->client)
            XModbusDevice_disconnectDevice((XModbusDevice*)d->client);
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
        if (ms > 0)
            dev->pollTimer = XObject_startTimer_ms((XObject*)hub,
                                                   (int)ms, XTimerType_CoarseTimer);
    }
    return true;
}

void XModbusHub_setPollAreas(XModbusHub* hub, unsigned areaMask)
{
    if (!hub) return;
    if (hub->m_pollAreas == areaMask) return;
    hub->m_pollAreas = areaMask;
    hubRebuildAll(hub);
}

void XModbusHub_setUnitBackoff(XModbusHub* hub, uint32_t maxFails, uint32_t muteMs)
{
    if (!hub) return;
    /* 门槛与 XPollScheduler_setBackoff 一致：>=1 / >=1000 才生效。 */
    if (maxFails >= 1) hub->m_unitMaxFails = maxFails;
    if (muteMs >= 1000) hub->m_unitMuteMs = muteMs;
    /* 已建设备即时同步（退避表在下次计划重建时也会按新配置重建）。 */
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
    /* 缓存/计划兜底重建（映射可能在 attach 后被直接编辑过）。 */
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
            }
        }
    }
    /* 写重发定时器随总闸拉起（滞留写意图恢复补发）。 */
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
}

bool XModbusHub_isPolling(const XModbusHub* hub)
{
    return hub ? hub->m_polling : false;
}

/* ==================== 统一缓存读 ==================== */

bool XModbusHub_value(const XModbusHub* hub, XModbusRegisterType type,
                      uint16_t virtualAddr, uint16_t* outValue)
{
    if (!outValue) return false;
    *outValue = 0xFFFFu;
    const int t = hubTypeIndex((int)type);
    if (!hub || t < 0 || !hub->m_cache[t]) return false;
    const uint32_t cacheEnd = (uint32_t)hub->m_cacheStart[t] + hub->m_cacheCount[t];
    if (virtualAddr < hub->m_cacheStart[t] || (uint32_t)virtualAddr >= cacheEnd)
        return false;
    *outValue = XModbusDataUnit_value(hub->m_cache[t],
                                      (size_t)(virtualAddr - hub->m_cacheStart[t]));
    return true;
}

bool XModbusHub_values(const XModbusHub* hub, XModbusRegisterType type,
                       uint16_t virtualStart, uint16_t count, uint16_t* outValues)
{
    if (!outValues) return false;
    bool all = true;
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
    return hub->m_cacheCount[t];
}

uint64_t XModbusHub_lastUpdateMs(const XModbusHub* hub, XModbusRegisterType type)
{
    /* 类型级最后更新时刻：映射到该类型的全部设备中最近一次成功应答。 */
    (void)type;
    if (!hub || !hub->m_devices) return 0;
    uint64_t latest = 0;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        const XModbusHubDevice* d =
            (const XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (d && d->lastSuccessMs > latest) latest = d->lastSuccessMs;
    }
    return latest;
}

/* ==================== 输出写 ==================== */

/** @brief 写意图受理：入队 + 保重发定时器存活 + 即时泵。 */
static bool hubEnqueueWrite(XModbusHub* hub, const XModbusHubWriteIntent* intent)
{
    if (!hub->m_writeQueue) return false;
    if (!XVector_push_back_1_base(hub->m_writeQueue, (void*)intent)) return false;
    if (hub->m_retryTimer == XTIMER_INVALID_ID) {
        /* 轮询总闸未开也要保写重发：单独拉起重发定时器。 */
        hub->m_retryTimer = XObject_startTimer_ms((XObject*)hub,
                                                  (int)hub->m_writeRetryIntervalMs,
                                                  XTimerType_CoarseTimer);
    }
    XModbusHubDevice* dev = hubDeviceById(hub, intent->deviceId);
    if (dev) hubPumpDevice(hub, dev);
    return true;
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
    if (!hub || !values || count == 0 || count > XMODBUS_HUB_WRITE_MAX_VALUES)
        return false;
    if (type != XModbusCoils && type != XModbusHoldingRegisters) return false;
    if (!hub->m_mapping) return false;
    /* 写区间必须完整落在单条映射段内（v1 拒绝跨段写）。 */
    const XModbusMapRange* r =
        XModbusMapping_rangeAtVirtual_const(hub->m_mapping, type, virtualStart);
    if (!r) return false;
    if ((uint32_t)virtualStart + count >
        (uint32_t)r->virtualStart + r->count) return false;

    XModbusHubWriteIntent intent;
    memset(&intent, 0, sizeof(intent));
    intent.deviceId = r->deviceId;
    intent.unitId = r->unitId;
    intent.type = (uint16_t)type;
    intent.virtualStart = virtualStart;
    intent.remoteStart = (uint16_t)(r->remoteStart + (virtualStart - r->virtualStart));
    intent.count = count;
    memcpy(intent.values, values, sizeof(uint16_t) * count);

    /* 乐观更新本地缓存（写回显语义：远端确认前即反映写入意图）。 */
    const int t = hubTypeIndex((int)type);
    if (t < 0) return false;
    bool changed = false;
    if (hub->m_cache[t]) {
        for (uint16_t i = 0; i < count; ++i) {
            const uint32_t v = (uint32_t)virtualStart + i;
            const uint32_t cacheEnd = (uint32_t)hub->m_cacheStart[t] +
                                      hub->m_cacheCount[t];
            if (v < hub->m_cacheStart[t] || v >= cacheEnd) continue;
            const size_t idx = (size_t)(v - hub->m_cacheStart[t]);
            if (XModbusDataUnit_value(hub->m_cache[t], idx) != values[i]) {
                XModbusDataUnit_setValue(hub->m_cache[t], idx, values[i]);
                changed = true;
            }
        }
        if (!hub->m_changedOnlySignals || changed)
            hubEmitValueChanged(hub, hubEnumOfType(t), virtualStart, count);
    }
    return hubEnqueueWrite(hub, &intent);
}

int XModbusHub_pendingWriteCount(const XModbusHub* hub)
{
    if (!hub || !hub->m_writeQueue) return 0;
    return (int)XVector_size_base(hub->m_writeQueue);
}

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

void XModbusHub_setWriteStallNotice(XModbusHub* hub, int attempts)
{
    if (hub) hub->m_writeStallNoticeAt = attempts; /* <1=关闭提示；只影响提示不改重发。 */
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

/* ==================== 定时器（XObject 自带机制，规范 2026-10-26） ==================== */

static void VXModbusHub_timerEvent(XObject* obj, XTimerEvent* event)
{
    XModbusHub* hub = (XModbusHub*)obj;
    if (!hub || !event) goto parent_call;
    XEvent_accept(event);
    const XTimerId id = XTimerEvent_timerId(event);
    if (id == hub->m_retryTimer) {
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
parent_call:
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* ==================== 槽 ==================== */

/** @brief 设备状态变化（connect_2：第一参=真 sender=client）。 */
static void XModbusHub_onDeviceStateChanged(XObject* sender, XVarList* args)
{
    if (!sender || !args) return;
    XVarList_args_1(args, int, newStateInt);
    XModbusHub* hub = (XModbusHub*)XObject_parent(sender);
    if (!hub || !XClassGetVtable(hub) ||
        XClassGetVtable(hub) != XModbusHub_class_init()) return;
    if (!hub->m_devices) return;
    const size_t n = XVector_size_base(hub->m_devices);
    for (size_t i = 0; i < n; ++i) {
        XModbusHubDevice* d =
            (XModbusHubDevice*)XVector_at_base(hub->m_devices, (int64_t)i);
        if (!d || (XObject*)d->client != sender) continue;
        const XModbusDevice_State old = d->state;
        const XModbusDevice_State newState = (XModbusDevice_State)newStateInt;
        if (old == newState) return;
        d->state = newState;
        XModbusHub_deviceStateChanged_signal(hub, d->id, newStateInt);
        if (newState == XModbusDevice_ConnectedState) {
            ++d->reconnectCount;
            /* 恢复在线：立即续泵轮询计划 + 补写滞留写（不等下一拍）。 */
            hubPumpDevice(hub, d);
            hubPumpWriteQueue(hub);
            XModbusHub_deviceStatsChanged_signal(hub, d->id);
        }
        return;
    }
}

/**
 * @brief 应答完成（connect_2：第一参=真 sender=reply；经 parent 反查 hub，
 *        按 inflight 指针归属设备；单飞收口后续泵）。
 */
static void XModbusHub_onReplyFinished(XObject* sender, XVarList* args)
{
    (void)args;
    XModbusReply* reply = (XModbusReply*)sender;
    if (!reply) return;
    XModbusHub* hub = (XModbusHub*)XObject_parent((XObject*)reply);
    if (!hub || !XClassGetVtable(hub) ||
        XClassGetVtable(hub) != XModbusHub_class_init()) {
        XObject_deleteLater((XObject*)reply);
        return;
    }
    XModbusHubDevice* dev = NULL;
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

    const uint64_t now = XDateTime_currentMSecsSinceEpoch();
    const bool ok = (XModbusReply_error(reply) == XModbusDevice_NoError);

    if (dev->inflightKind == XModbusHubInflight_Poll) {
        /* [2026-10-28 单元退避] 段/从站定位提前到成败分支之前，结果先回报
         * 退避表（lost 跃迁沿发 unitLost/unitAlive），再做回填与计数。 */
        const XModbusMapSegment* seg = (dev->plan && dev->inflightSegIndex <
                                        XVector_size_base(dev->plan))
            ? (const XModbusMapSegment*)XVector_at_base(dev->plan,
                                                        (int64_t)dev->inflightSegIndex)
            : NULL;
        const int unitId = seg ? seg->range.unitId : -1;
        const bool wasLost = (unitId >= 0) &&
            XPollScheduler_isLostKey(&dev->unitSched, unitId);
        const bool enteredMute = (unitId >= 0) &&
            XPollScheduler_reportResultKey(&dev->unitSched, unitId, ok, now);
        if (ok) {
            const XModbusDataUnit* result = XModbusReply_result_const(reply);
            if (result && seg)
                hubFillFromResult(hub, (int)seg->type, seg->virtualStart, result);
            ++dev->pollOkCount;
            dev->lastSuccessMs = now;
            if (wasLost)
                XModbusHub_unitAlive_signal(hub, dev->id, unitId); /* 失联→恢复跃迁。 */
        } else {
            ++dev->pollErrCount; /* 不风暴：静默计数，下轮计划重试该段。 */
            if (enteredMute && !wasLost)
                XModbusHub_unitLost_signal(hub, dev->id, unitId); /* 首次进入静默。 */
        }
        /* 计划游标推进到在途段的下一段（在途发送时已 +1，此处保持）。 */
        XModbusHub_deviceStatsChanged_signal(hub, dev->id);
    } else if (dev->inflightKind == XModbusHubInflight_Write) {
        if (ok) {
            dev->lastSuccessMs = now;
            const XModbusHubWriteIntent snap = dev->inflightWrite;
            hubRemoveWriteIntent(hub, &snap);
            /* 成功即出队停发（用户裁定语义）；失败静默留队重发。 */
            XModbusHub_writeCompleted_signal(hub, (int)snap.type,
                                             (int)snap.virtualStart,
                                             (int)snap.count, true);
        }
        XModbusHub_deviceStatsChanged_signal(hub, dev->id);
    }

    dev->inflight = NULL;
    dev->inflightKind = XModbusHubInflight_None;
    XObject_deleteLater((XObject*)reply);
    /* [2026-10-26 节拍根修] 收口只补写、不续轮询：若在此续泵，计划轮跑完
     * 回卷后立刻又发下一轮（实测 2ms/请求、tid 数千的轮询风暴，回填在
     * 风暴中概率性失败）。轮询节奏一律交还设备周期定时器——pollInterval
     * 语义=段发送节拍。写意图仍即时续泵（写延迟优先）。 */
    if (hubNextWriteForDevice(hub, dev))
        hubPumpDevice(hub, dev);
}

/* ==================== 信号 ==================== */

void* XModbusHub_deviceStateChanged_signal(XModbusHub* hub, int deviceId, int state)
{
    XEmitSignal(hub, XModbusHub_deviceStateChanged_signal,
        XVarList_Create(XVar(int, deviceId), XVar(int, state)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_valueChanged_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count)
{
    XEmitSignal(hub, XModbusHub_valueChanged_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_writeCompleted_signal(XModbusHub* hub, int type,
                                       int virtualStart, int count, bool ok)
{
    XEmitSignal(hub, XModbusHub_writeCompleted_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count),
                        XVar(bool, ok)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

void* XModbusHub_deviceStatsChanged_signal(XModbusHub* hub, int deviceId)
{
    XEmitSignal(hub, XModbusHub_deviceStatsChanged_signal,
        XVarList_Create(XVar(int, deviceId)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
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

void* XModbusHub_writeStalled_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count, int attempts)
{
    XEmitSignal(hub, XModbusHub_writeStalled_signal,
        XVarList_Create(XVar(int, type), XVar(int, virtualStart), XVar(int, count),
                        XVar(int, attempts)), NULL,
        NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XMODBUS_HUB_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
