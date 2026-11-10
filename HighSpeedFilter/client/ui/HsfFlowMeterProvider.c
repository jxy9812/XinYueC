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
 *             [侧别] 侧别是**服务端的事实**：服务端按装配位置回答并在帧里
 *             带 side；provider 走「活动侧」（HsfLinkClient_flowMeterActive）
 *             取数，页面不写死现场接线（早先每侧一张表 + 页面 FM_SIDE=1）。
 */
#include "HsfFlowMeterProvider.h"

#include "HsfLinkClient.h"

#if XVFD_ON || XINDUSTRY_ON

#include <string.h>

/* ==================== provider 状态 ==================== */

/**
 * @brief provider 状态（单例；不按侧分册）。
 */
typedef struct HsfFmState {
    HsfFlowMeterNotifyFn m_pageCb;      /**< 页面通知回调（单页单实例）。 */
    void* m_pageUser;                   /**< 页面令牌。 */
    const struct HsfFlowMeterProvider* m_self; /**< 表指针回填。 */
} HsfFmState;

static HsfFmState s_fm;   /**< 模块单例（零初始化）。 */

/**
 * @brief 遥测更新桥：HsfLinkClient 流量计回调 → 页面通知。
 * @param userData 本模块状态（HsfFmState*）。
 */
static void hsfFmOnTelemetry(void* userData)
{
    HsfFmState* st = (HsfFmState*)userData;
    if (st && st->m_pageCb)
        st->m_pageCb(st->m_self, st->m_pageUser);
}

/* ==================== provider 槽实现 ==================== */

/**
 * @brief 取快照：把活动侧的 HsfLinkFlowMeter 翻译成本表口径（叠加本地新鲜度）。
 * @param userData 本模块状态。
 * @return 静态快照缓冲借用指针。
 * @note  返回的是**静态缓冲**，随下次调用覆盖——页面须在回调内消费。
 */
static const HsfFlowMeterSnapshot* hsfFmSnapshot(void* userData)
{
    static HsfFlowMeterSnapshot s_snap;
    const HsfLinkFlowMeter* src;
    HsfFlowMeterSnapshot* dst;

    (void)userData;
    memset(&s_snap, 0, sizeof(s_snap));
    dst = &s_snap;
    dst->m_flowUnit = 0xFF;      /* Unknown */
    dst->m_totalUnit = 0xFF;

    /* 活动侧＝服务端报告的侧（客户端不猜现场接线）。 */
    src = HsfLinkClient_flowMeterActive();
    if (!src) return dst;

    dst->m_flow = src->m_flow;
    dst->m_speed = src->m_speed;
    dst->m_forwardTotal = src->m_forwardTotal;
    dst->m_reverseTotal = src->m_reverseTotal;
    dst->m_flowUnit = src->m_flowUnit;
    dst->m_totalUnit = src->m_totalUnit;
    dst->m_alarms = src->m_alarms;
    /* 双重质量：服务端说有效 && 本地推送没断流。 */
    dst->m_valid = src->m_valid && HsfLinkClient_flowMeterActiveFresh();
    return dst;
}

static bool hsfFmRequestSnapshot(void* userData)
{
    (void)userData;
    return HsfLinkClient_flowMeterSnapshotRequest();
}

static bool hsfFmSubscribe(void* userData, HsfFlowMeterNotifyFn cb, void* page)
{
    HsfFmState* st = (HsfFmState*)userData;
    if (!st || !cb) return false;
    st->m_pageCb = cb;
    st->m_pageUser = page;
    return true;
}

static void hsfFmUnsubscribe(void* userData, void* page)
{
    HsfFmState* st = (HsfFmState*)userData;
    if (!st) return;
    if (st->m_pageUser == page) {
        st->m_pageCb = NULL;
        st->m_pageUser = NULL;
    }
}

/* ==================== provider 静态表 ==================== */

/* 可写静态表（懒接线回填 m_userData）；对外出口仍 const。 */
static struct HsfFlowMeterProvider kFmProvider = {
    NULL, hsfFmSnapshot, hsfFmRequestSnapshot, hsfFmSubscribe, hsfFmUnsubscribe
};

const HsfFlowMeterProvider* HsfFlowMeterProvider_remote(void)
{
    if (!s_fm.m_self) {
        s_fm.m_self = &kFmProvider;
        kFmProvider.m_userData = &s_fm;
        /* 回调槽桥接（与变频器 provider 同款）：客户端流量计回调 → 页面通知。 */
        HsfLinkClient_setFlowMeterCallback(hsfFmOnTelemetry, &s_fm);
    }
    return &kFmProvider;
}

#endif /* XVFD_ON || XINDUSTRY_ON */
