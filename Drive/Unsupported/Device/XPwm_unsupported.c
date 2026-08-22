/**
 * @file XPwm_unsupported.c
 * @brief 未接入 PWM 驱动的平台存根。
 * @details
 * 存根仅验证并保存配置，不调用平台 API，不改变任何真实输出。产品后端应
 * 在 Drive 下提供同名 XPwm 函数，并使用 XPWM_TEST_BACKEND 排除本文件。
 */
#include "XPwm.h"

#if !(defined(XPWM_TEST_BACKEND) && XPWM_TEST_BACKEND)

#include "XMemory.h"

struct XPwm {
    XPwmConfig m_config;       /**< 配置副本。 */
    bool m_open;               /**< 硬件是否打开；存根始终为 false。 */
    bool m_running;            /**< 输出是否运行；存根始终为 false。 */
    XPwmError m_lastError;     /**< 最近一次错误。 */
    int32_t m_nativeError;     /**< 存根没有原生错误。 */
};

static bool xpwm_valid_config(const XPwmConfig* config)
{
    return config && config->m_dutyPermille <= 10000u &&
           config->m_polarity <= XPwmPolarity_Inverted && config->m_flags == 0u;
}

static bool xpwm_fail(XPwm* pwm, XPwmError error)
{
    if (pwm) {
        pwm->m_lastError = error;
        pwm->m_nativeError = 0;
    }
    return false;
}

XPwm* XPwm_create(const XPwmConfig* config)
{
    XPwm* pwm;
    if (!xpwm_valid_config(config)) return NULL;
    pwm = (XPwm*)XCalloc_System(1u, sizeof(*pwm));
    if (!pwm) return NULL;
    pwm->m_config = *config;
    pwm->m_lastError = XPwmError_None;
    return pwm;
}

bool XPwm_open(XPwm* pwm)
{
    if (!pwm) return false;
    return xpwm_fail(pwm, XPwmError_Unsupported);
}

void XPwm_close(XPwm* pwm)
{
    if (pwm) pwm->m_lastError = XPwmError_None;
}

void XPwm_delete(XPwm* pwm)
{
    if (pwm) {
        XPwm_close(pwm);
        XFree_System(pwm);
    }
}

bool XPwm_isOpen(const XPwm* pwm)
{
    return pwm ? pwm->m_open : false;
}

bool XPwm_getConfig(const XPwm* pwm, XPwmConfig* config)
{
    if (!pwm || !config) return false;
    *config = pwm->m_config;
    ((XPwm*)pwm)->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_configure(XPwm* pwm, const XPwmConfig* config)
{
    if (!pwm || !xpwm_valid_config(config)) {
        return xpwm_fail(pwm, XPwmError_InvalidArgument);
    }
    pwm->m_config = *config;
    pwm->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_start(XPwm* pwm)
{
    return xpwm_fail(pwm, XPwmError_Unsupported);
}

bool XPwm_stop(XPwm* pwm)
{
    return xpwm_fail(pwm, XPwmError_Unsupported);
}

bool XPwm_isRunning(const XPwm* pwm)
{
    return pwm ? pwm->m_running : false;
}

bool XPwm_setFrequency(XPwm* pwm, uint32_t frequencyHz)
{
    (void)frequencyHz;
    return xpwm_fail(pwm, XPwmError_Unsupported);
}

bool XPwm_setDuty(XPwm* pwm, uint16_t dutyPermille)
{
    (void)dutyPermille;
    return xpwm_fail(pwm, XPwmError_Unsupported);
}

XPwmFeatures XPwm_features(const XPwm* pwm)
{
    (void)pwm;
    return XPwmFeature_None;
}

bool XPwm_hasFeature(const XPwm* pwm, XPwmFeature feature)
{
    return pwm && feature == XPwmFeature_None;
}

XPwmError XPwm_lastError(const XPwm* pwm)
{
    return pwm ? pwm->m_lastError : XPwmError_InvalidArgument;
}

int32_t XPwm_nativeError(const XPwm* pwm)
{
    return pwm ? pwm->m_nativeError : 0;
}

const char* XPwm_errorString(XPwmError error)
{
    switch (error) {
    case XPwmError_None: return "none";
    case XPwmError_InvalidArgument: return "invalid-argument";
    case XPwmError_NotOpen: return "not-open";
    case XPwmError_AlreadyOpen: return "already-open";
    case XPwmError_NotRunning: return "not-running";
    case XPwmError_AlreadyRunning: return "already-running";
    case XPwmError_Unsupported: return "unsupported";
    case XPwmError_Busy: return "busy";
    case XPwmError_PermissionDenied: return "permission-denied";
    case XPwmError_Hardware: return "hardware";
    case XPwmError_Closed: return "closed";
    default: return "unknown";
    }
}

#endif /* XPWM_TEST_BACKEND */
