/**
 * @file       HsfFlowMeterProvider.c
 * @brief      电磁流量计页远端数据提供者实现（XLink 后端）。
 * @details    [数据路径] HsfLinkClient 收 Publish → hsfLcParseFlowMeter 进
 *             HsfLinkFlowMeter 快照 → 本 provider 的回调 → 转发页注册的
 *             notify（页面侧只认本表，后端换实现零改动）。
 *
 *             [新鲜度] 快照的 m_valid 由服务端给，但**断线后推送停止，
 *             m_valid 仍是最后一次的值**。故本 provider 叠加本地新鲜度：
 *             超过 HSF_LINK_FLOWMETER_STALE_MS 没收到推送即判无效——
 *             直读服务端 valid 会把旧值当实时数据显示（温湿度页踩过此坑）。
 *
 *             [每个侧一张表] 两张静态表按 side 取，互不干扰。
 */
#include "HsfFlowMeterProvider.h"

#include "HsfLinkClient.h"

#if XVFD_ON || XINDUSTRY_ON

#include <string.h>

/* ==================== 每侧状态 ==================== */

/**
 * @brief 一册（一侧）的 provider 状态。
 */
typedef struct HsfFmSideState {
    HsfFlowMeterNotifyFn m_pageCb;      /**< 页面通知回调（单页单实例）。 */
    void* m_pageUser;                   /**< 页面令牌。 */
    const struct HsfFlowMeterProvider* m_self; /**< 表指针回填。 */
    int m_side;                         /**< 侧号。 */
} HsfFmSideState;

static HsfFmSideState s_fm[HSF_LINK_SIDE_COUNT];   /**< 模块单例（零初始化）。 */

/* ==================== 通知桥 ==================== */

/**
 * @brief 遥测更新桥：HsfLinkClient 流量计回调 → 页面通知。
 * @param userData 本册状态（HsfFmSideState*）。
 */
static void hsfFmOnTelemetry(void* userData)
{
    HsfFmSideState* st = (HsfFmSideState*)userData;
    if (st && st->m_pageCb)
        st->m_pageCb(st->m_self, st->m_pageUser);
}

/* ==================== provider 槽实现 ==================== */

/**
 * @brief 取快照：把 HsfLinkFlowMeter 翻译成本表口径（叠加本地新鲜度）。
 * @param userData 本册状态。
 * @return 静态快照缓冲借用指针；side 非法返回 NULL。
 * @note  返回的是**每册的静态缓冲**，随下次调用覆盖——页面须在回调内消费。
 */
static const HsfFlowMeterSnapshot* hsfFmSnapshot(void* userData)
{
    static HsfFlowMeterSnapshot s_snap[HSF_LINK_SIDE_COUNT];
    HsfFmSideState* st = (HsfFmSideState*)userData;
    const HsfLinkFlowMeter* src;
    HsfFlowMeterSnapshot* dst;

    if (!st || st->m_side < 0 || st->m_side >= HSF_LINK_SIDE_COUNT)
        return NULL;
    dst = &s_snap[st->m_side];
    memset(dst, 0, sizeof(*dst));
    dst->m_flowUnit = 0xFF;      /* Unknown */
    dst->m_totalUnit = 0xFF;

    src = HsfLinkClient_flowMeter(st->m_side);
    if (!src) return dst;

    dst->m_flow = src->m_flow;
    dst->m_speed = src->m_speed;
    dst->m_forwardTotal = src->m_forwardTotal;
    dst->m_reverseTotal = src->m_reverseTotal;
    dst->m_flowUnit = src->m_flowUnit;
    dst->m_totalUnit = src->m_totalUnit;
    dst->m_alarms = src->m_alarms;
    /* 双重质量：服务端说有效 && 本地推送没断流。 */
    dst->m_valid = src->m_valid && HsfLinkClient_flowMeterFresh(st->m_side);
    return dst;
}

static bool hsfFmRequestSnapshot(void* userData)
{
    HsfFmSideState* st = (HsfFmSideState*)userData;
    if (!st) return false;
    return HsfLinkClient_flowMeterSnapshotRequest(st->m_side);
}

static bool hsfFmSubscribe(void* userData, HsfFlowMeterNotifyFn cb, void* page)
{
    HsfFmSideState* st = (HsfFmSideState*)userData;
    if (!st || !cb) return false;
    st->m_pageCb = cb;
    st->m_pageUser = page;
    return true;
}

static void hsfFmUnsubscribe(void* userData, void* page)
{
    HsfFmSideState* st = (HsfFmSideState*)userData;
    if (!st) return;
    if (st->m_pageUser == page) {
        st->m_pageCb = NULL;
        st->m_pageUser = NULL;
    }
}

/* ==================== provider 静态表 ==================== */

/* 可写静态表（懒接线回填 m_userData）；对外出口仍 const。 */
static struct HsfFlowMeterProvider kFmProvider[HSF_LINK_SIDE_COUNT] = {
    { NULL, hsfFmSnapshot, hsfFmRequestSnapshot, hsfFmSubscribe, hsfFmUnsubscribe },
    { NULL, hsfFmSnapshot, hsfFmRequestSnapshot, hsfFmSubscribe, hsfFmUnsubscribe }
};

const HsfFlowMeterProvider* HsfFlowMeterProvider_remote(int side)
{
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return NULL;
    if (!s_fm[side].m_self) {
        s_fm[side].m_self = &kFmProvider[side];
        s_fm[side].m_side = side;
        kFmProvider[side].m_userData = &s_fm[side];
        /* 回调槽桥接（与变频器 provider 同款：各自占独立槽位，互不覆盖）。
         * userData 传本册状态，回调里据此区分侧别。 */
        HsfLinkClient_setFlowMeterCallback(hsfFmOnTelemetry, &s_fm[side]);
    }
    return &kFmProvider[side];
}

#endif /* XVFD_ON || XINDUSTRY_ON */
