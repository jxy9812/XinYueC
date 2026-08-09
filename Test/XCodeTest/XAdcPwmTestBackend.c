/**
 * @file XAdcPwmTestBackend.c
 * @brief ADC/PWM 确定性测试后端。
 * @details
 * 仅在定义 XADC_TEST_BACKEND 或 XPWM_TEST_BACKEND 时分别编译对应实现。
 * 后端使用 XMemory，不依赖任何平台 API；ADC 返回固定的原始样本，PWM
 * 保存配置和运行状态，用于覆盖 Shell 生命周期、参数和权限路径。
 */
#include "XMemory.h"

#if defined(XADC_TEST_BACKEND) && XADC_TEST_BACKEND

#include "XAdc.h"

struct XAdc {
    XAdcConfig m_config;
    bool m_open;
    uint32_t m_sample;
    XAdcError m_lastError;
    int32_t m_nativeError;
};

static bool xadc_test_valid(const XAdcConfig* config)
{
    return config && config->m_resolutionBits <= 32u && config->m_flags == 0u;
}

static bool xadc_test_fail(XAdc* adc, XAdcError error)
{
    if (adc) adc->m_lastError = error;
    return false;
}

XAdc* XAdc_create(const XAdcConfig* config)
{
    XAdc* adc;
    if (!xadc_test_valid(config)) return NULL;
    adc = (XAdc*)XMemory_malloc(sizeof(*adc), XMEMORY_TYPE_SYSTEM);
    if (!adc) return NULL;
    adc->m_config = *config;
    adc->m_open = false;
    adc->m_sample = 1234u;
    adc->m_lastError = XAdcError_None;
    adc->m_nativeError = 0;
    return adc;
}

bool XAdc_open(XAdc* adc)
{
    if (!adc) return false;
    if (adc->m_open) return xadc_test_fail(adc, XAdcError_AlreadyOpen);
    adc->m_open = true;
    adc->m_lastError = XAdcError_None;
    return true;
}

void XAdc_close(XAdc* adc)
{
    if (adc) {
        adc->m_open = false;
        adc->m_lastError = XAdcError_None;
    }
}

void XAdc_delete(XAdc* adc)
{
    if (adc) {
        XAdc_close(adc);
        XMemory_free(adc, XMEMORY_TYPE_SYSTEM);
    }
}

bool XAdc_isOpen(const XAdc* adc)
{
    return adc ? adc->m_open : false;
}

bool XAdc_getConfig(const XAdc* adc, XAdcConfig* config)
{
    if (!adc || !config) return false;
    *config = adc->m_config;
    return true;
}

bool XAdc_configure(XAdc* adc, const XAdcConfig* config)
{
    if (!adc || !xadc_test_valid(config))
        return xadc_test_fail(adc, XAdcError_InvalidArgument);
    adc->m_config = *config;
    adc->m_lastError = XAdcError_None;
    return true;
}

bool XAdc_readRaw(XAdc* adc, uint32_t* value, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!adc || !value) return xadc_test_fail(adc, XAdcError_InvalidArgument);
    if (!adc->m_open) return xadc_test_fail(adc, XAdcError_NotOpen);
    *value = adc->m_sample;
    adc->m_lastError = XAdcError_None;
    return true;
}

bool XAdc_readMillivolts(XAdc* adc, uint32_t* value, int32_t timeoutMs)
{
    uint32_t raw;
    uint32_t maxValue;
    uint64_t mv;
    (void)timeoutMs;
    if (!value || !XAdc_readRaw(adc, &raw, timeoutMs)) return false;
    maxValue = adc->m_config.m_resolutionBits == 0u ||
                       adc->m_config.m_resolutionBits >= 32u
                   ? UINT32_MAX
                   : (UINT32_C(1) << adc->m_config.m_resolutionBits) - 1u;
    if (adc->m_config.m_referenceMv == 0u || maxValue == 0u) {
        *value = raw;
    } else {
        mv = (uint64_t)raw * adc->m_config.m_referenceMv;
        *value = (uint32_t)(mv / maxValue);
    }
    return true;
}

XAdcFeatures XAdc_features(const XAdc* adc)
{
    return adc ? (XAdcFeature_RawRead | XAdcFeature_MillivoltRead |
                  XAdcFeature_Resolution | XAdcFeature_Reference |
                  XAdcFeature_SampleTime | XAdcFeature_Oversample)
               : XAdcFeature_None;
}

bool XAdc_hasFeature(const XAdc* adc, XAdcFeature feature)
{
    return adc && (XAdc_features(adc) & (XAdcFeatures)feature) != 0u;
}

XAdcError XAdc_lastError(const XAdc* adc)
{
    return adc ? adc->m_lastError : XAdcError_InvalidArgument;
}

int32_t XAdc_nativeError(const XAdc* adc)
{
    return adc ? adc->m_nativeError : 0;
}

const char* XAdc_errorString(XAdcError error)
{
    switch (error) {
    case XAdcError_None: return "none";
    case XAdcError_InvalidArgument: return "invalid-argument";
    case XAdcError_NotOpen: return "not-open";
    case XAdcError_AlreadyOpen: return "already-open";
    case XAdcError_Unsupported: return "unsupported";
    case XAdcError_Busy: return "busy";
    case XAdcError_Timeout: return "timeout";
    case XAdcError_PermissionDenied: return "permission-denied";
    case XAdcError_Hardware: return "hardware";
    case XAdcError_Closed: return "closed";
    default: return "unknown";
    }
}

#endif /* XADC_TEST_BACKEND */

#if defined(XPWM_TEST_BACKEND) && XPWM_TEST_BACKEND

#include "XPwm.h"

struct XPwm {
    XPwmConfig m_config;
    bool m_open;
    bool m_running;
    XPwmError m_lastError;
    int32_t m_nativeError;
};

static bool xpwm_test_valid(const XPwmConfig* config)
{
    return config && config->m_frequencyHz > 0u &&
           config->m_dutyPermille <= 10000u &&
           config->m_polarity <= XPwmPolarity_Inverted && config->m_flags == 0u;
}

static bool xpwm_test_fail(XPwm* pwm, XPwmError error)
{
    if (pwm) pwm->m_lastError = error;
    return false;
}

XPwm* XPwm_create(const XPwmConfig* config)
{
    XPwm* pwm;
    if (!xpwm_test_valid(config)) return NULL;
    pwm = (XPwm*)XMemory_malloc(sizeof(*pwm), XMEMORY_TYPE_SYSTEM);
    if (!pwm) return NULL;
    pwm->m_config = *config;
    pwm->m_open = false;
    pwm->m_running = false;
    pwm->m_lastError = XPwmError_None;
    pwm->m_nativeError = 0;
    return pwm;
}

bool XPwm_open(XPwm* pwm)
{
    if (!pwm) return false;
    if (pwm->m_open) return xpwm_test_fail(pwm, XPwmError_AlreadyOpen);
    pwm->m_open = true;
    pwm->m_running = pwm->m_config.m_initialEnabled;
    pwm->m_lastError = XPwmError_None;
    return true;
}

void XPwm_close(XPwm* pwm)
{
    if (pwm) {
        pwm->m_running = false;
        pwm->m_open = false;
        pwm->m_lastError = XPwmError_None;
    }
}

void XPwm_delete(XPwm* pwm)
{
    if (pwm) {
        XPwm_close(pwm);
        XMemory_free(pwm, XMEMORY_TYPE_SYSTEM);
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
    return true;
}

bool XPwm_configure(XPwm* pwm, const XPwmConfig* config)
{
    if (!pwm || !xpwm_test_valid(config))
        return xpwm_test_fail(pwm, XPwmError_InvalidArgument);
    pwm->m_config = *config;
    if (pwm->m_open) pwm->m_running = config->m_initialEnabled;
    pwm->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_start(XPwm* pwm)
{
    if (!pwm) return false;
    if (!pwm->m_open) return xpwm_test_fail(pwm, XPwmError_NotOpen);
    if (pwm->m_running) return xpwm_test_fail(pwm, XPwmError_AlreadyRunning);
    pwm->m_running = true;
    pwm->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_stop(XPwm* pwm)
{
    if (!pwm) return false;
    if (!pwm->m_open) return xpwm_test_fail(pwm, XPwmError_NotOpen);
    if (!pwm->m_running) return xpwm_test_fail(pwm, XPwmError_NotRunning);
    pwm->m_running = false;
    pwm->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_isRunning(const XPwm* pwm)
{
    return pwm ? pwm->m_running : false;
}

bool XPwm_setFrequency(XPwm* pwm, uint32_t frequencyHz)
{
    if (!pwm || frequencyHz == 0u)
        return xpwm_test_fail(pwm, XPwmError_InvalidArgument);
    pwm->m_config.m_frequencyHz = frequencyHz;
    pwm->m_lastError = XPwmError_None;
    return true;
}

bool XPwm_setDuty(XPwm* pwm, uint16_t dutyPermille)
{
    if (!pwm || dutyPermille > 10000u)
        return xpwm_test_fail(pwm, XPwmError_InvalidArgument);
    pwm->m_config.m_dutyPermille = dutyPermille;
    pwm->m_lastError = XPwmError_None;
    return true;
}

XPwmFeatures XPwm_features(const XPwm* pwm)
{
    return pwm ? (XPwmFeature_StartStop | XPwmFeature_Frequency |
                  XPwmFeature_Duty | XPwmFeature_Polarity)
               : XPwmFeature_None;
}

bool XPwm_hasFeature(const XPwm* pwm, XPwmFeature feature)
{
    return pwm && (XPwm_features(pwm) & (XPwmFeatures)feature) != 0u;
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
