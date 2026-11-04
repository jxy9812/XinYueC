
/**
 * @file       HsfVfdProviderRemote.c
 * @brief      变频器页远端数据提供者实现（XLink 后端）。
 * @details    [数据路径] HsfLinkClient 收 Publish → XVfdCodec_statusFromBson
 *             进快照 → 本 provider 的 vfd 回调 → 转发页注册的 notify（页面
 *             侧与本地后端完全同一路径，只换数据源）。[控制路径] 页面动作
 *             → 本表打包 XVfdControlRequest → HsfLinkClient_vfdControl
 *             （Request/Response，服务端角色门禁）。[能力/故障文本] 本地
 *             无设备对象：能力位按协议约定给（M512 支持面），故障文本直接
 *             查 XVfdM512 品牌表（客户端同链 Industry 库）。
 */
#include "HsfVfdProviderRemote.h"

#if XVFD_ON

#include "HsfLinkClient.h"
#include "XVfdCodec.h"
#include "XVfdM512.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 模块状态 ==================== */

/** @brief 远端 provider 单例状态。 */
typedef struct HsfVfdRemoteState {
    HsfVfdNotifyFn m_pageCb;        /**< 页面通知回调（单页单实例）。 */
    void* m_pageUser;               /**< 页面令牌。 */
    bool m_gateOpen;                /**< 本地控制闸（默认关，勾选开）。 */
    const struct HsfVfdProvider* m_self; /**< 表指针回填（通知回传用）。 */
} HsfVfdRemoteState;

static HsfVfdRemoteState s_vr;      /**< 模块单例（零初始化）。 */

/** @brief 远端能力位（协议约定：M512 支持面，页面按位裁剪控件）。 */
#define HSF_VR_CAPS  ((uint32_t)XVfdCap_Reverse | (uint32_t)XVfdCap_Jog | \
                      (uint32_t)XVfdCap_FreeStop | (uint32_t)XVfdCap_QuickStop)

/* ==================== 通知桥 ==================== */

/**
 * @brief 遥测更新桥：HsfLinkClient vfd 回调 → 页面通知。
 */
static void hsfVrOnVfdTelemetry(void* userData)
{
    (void)userData;
    if (s_vr.m_pageCb) s_vr.m_pageCb(s_vr.m_self, s_vr.m_pageUser);
}

/**
 * @brief 连接就绪桥：重连/首连成功后立即拉一拍快照（不等下一推送拍）。
 */
static void hsfVrOnReady(void* userData)
{
    (void)userData;
    if (HsfLinkClient_isReady())
        HsfLinkClient_vfdSnapshotRequest();
}

/* ==================== provider 槽实现 ==================== */

static const XVfdDevice* remoteDevice(void* userData)
{
    (void)userData;
    return NULL;    /* 远端后端无本地设备对象；页面走 snapshot/caps 槽。 */
}

static const XVfdStatus* remoteSnapshot(void* userData)
{
    (void)userData;
    return HsfLinkClient_vfdStatus();
}

static const char* remoteFaultText(void* userData, uint16_t code)
{
    (void)userData;
    return XVfdM512_faultText(code);
}

static uint32_t remoteCaps(void* userData)
{
    (void)userData;
    return HSF_VR_CAPS;
}

static bool remoteCommandError(void* userData, int* outSeq,
                               char* outErrUtf8, size_t cap)
{
    (void)userData;
    if (outSeq) *outSeq = HsfLinkClient_commandSeq();
    if (outErrUtf8 && cap > 0) {
        const char* err = HsfLinkClient_lastCommandError();
        size_t n = err ? strlen(err) : 0;
        if (n >= cap) n = cap - 1;
        memcpy(outErrUtf8, err, n);
        outErrUtf8[n] = '\0';
    }
    return HsfLinkClient_commandSeq() > 0;
}

static bool remoteControlEnabled(void* userData)
{
    (void)userData;
    return s_vr.m_gateOpen;
}

static bool remoteSetControlEnabled(void* userData, bool enabled)
{
    (void)userData;
    if (!HsfLinkClient_isReady()) return false;   /* 未登录不允许开闸。 */
    s_vr.m_gateOpen = enabled;
    return true;
}

/**
 * @brief 控制动作统一打包下发（方向/数值按动作装入请求）。
 */
static bool remoteSendControl(XVfdOperation op, bool reverse, double hz)
{
    XVfdControlRequest req;
    memset(&req, 0, sizeof(req));
    req.m_op = op;
    req.m_reverse = reverse;
    req.m_valueHz = hz;
    return HsfLinkClient_vfdControl(&req);
}

static bool remoteStart(void* userData, bool reverse)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_Start, reverse, 0.0);
}

static bool remoteStop(void* userData)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_Stop, false, 0.0);
}

static bool remoteFreeStop(void* userData)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_FreeStop, false, 0.0);
}

static bool remoteJog(void* userData, bool reverse)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_Jog, reverse, 0.0);
}

static bool remoteSetFrequency(void* userData, double hz)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_SetFrequency, false, hz);
}

static bool remoteResetFault(void* userData)
{
    (void)userData;
    return remoteSendControl(XVfdOperation_ResetFault, false, 0.0);
}

static bool remoteSubscribe(void* userData, HsfVfdNotifyFn cb, void* page)
{
    (void)userData;
    s_vr.m_pageCb = cb;
    s_vr.m_pageUser = page;
    return true;
}

static void remoteUnsubscribe(void* userData, void* page)
{
    (void)userData;
    if (s_vr.m_pageUser == page) {
        s_vr.m_pageCb = NULL;
        s_vr.m_pageUser = NULL;
    }
}

/* ==================== provider 静态表 ==================== */

/* 可写静态表（懒接线回填 m_userData/表指针；对外出口仍 const）。 */
static HsfVfdProvider kRemoteProvider = {
    /* m_userData */ NULL,
    remoteDevice,
    remoteFaultText,
    remoteControlEnabled,
    remoteSetControlEnabled,
    remoteStart,
    remoteStop,
    remoteFreeStop,
    remoteJog,
    remoteSetFrequency,
    remoteResetFault,
    remoteSubscribe,
    remoteUnsubscribe,
    /* 远端扩展槽：快照 + 能力 + 控制应答观测（页面优先消费）。 */
    remoteSnapshot,
    remoteCaps,
    remoteCommandError
};

const HsfVfdProvider* HsfVfdProvider_remote(void)
{
    if (!s_vr.m_self) {
        s_vr.m_self = &kRemoteProvider;
        /* HsfLinkClient 回调槽位接桥（各槽单订阅者：登录窗占 ready/fail，
         * 本 provider 占 vfd/command；ready 桥在登录窗之后注册会覆盖——
         * 登录窗用 fail/ready 判定首登，本桥只在就绪后拉快照，覆盖安全）。 */
        HsfLinkClient_setVfdCallback(hsfVrOnVfdTelemetry, NULL);
        HsfLinkClient_setReadyCallback(hsfVrOnReady, NULL);
    }
    return &kRemoteProvider;
}

#endif /* XVFD_ON */
