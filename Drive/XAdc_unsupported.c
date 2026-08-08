/**
 * @file XAdc_unsupported.c
 * @brief 未接入 ADC 驱动的平台存根。
 * @details
 * 存根只保存配置，不调用任何平台 API，也不伪造采样值。这样 Shell 和上层
 * 测试可以验证参数、生命周期和错误处理；产品接入 ADC 时应在 Drive 下提供
 * 同名实现，并通过 XADC_TEST_BACKEND 排除本文件，避免重复符号。
 */
#include "XAdc.h"

#if !(defined(XADC_TEST_BACKEND) && XADC_TEST_BACKEND)

#include "XMemory.h"

struct XAdc {
    XAdcConfig m_config;       /**< 配置副本。 */
    bool m_open;               /**< 存根永远无法打开硬件。 */
    XAdcError m_lastError;     /**< 最近一次错误。 */
    int32_t m_nativeError;     /**< 存根没有原生错误。 */
};

static bool xadc_valid_config(const XAdcConfig* config)
{
    return config && config->m_resolutionBits <= 32u && config->m_flags == 0u;
}

static bool xadc_fail(XAdc* adc, XAdcError error)
{
    if (adc) {
        adc->m_lastError = error;
        adc->m_nativeError = 0;
    }
    return false;
}

XAdc* XAdc_create(const XAdcConfig* config)
{
    XAdc* adc;
    if (!xadc_valid_config(config)) return NULL;
    adc = (XAdc*)XCalloc_System(1u, sizeof(*adc));
    if (!adc) return NULL;
    adc->m_config = *config;
    adc->m_lastError = XAdcError_None;
    return adc;
}

bool XAdc_open(XAdc* adc)
{
    if (!adc) return false;
    return xadc_fail(adc, XAdcError_Unsupported);
}

void XAdc_close(XAdc* adc)
{
    if (adc) adc->m_lastError = XAdcError_None;
}

void XAdc_delete(XAdc* adc)
{
    if (adc) {
        XAdc_close(adc);
        XFree_System(adc);
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
    ((XAdc*)adc)->m_lastError = XAdcError_None;
    return true;
}

bool XAdc_configure(XAdc* adc, const XAdcConfig* config)
{
    if (!adc || !xadc_valid_config(config))
        return xadc_fail(adc, XAdcError_InvalidArgument);
    adc->m_config = *config;
    adc->m_lastError = XAdcError_None;
    return true;
}

bool XAdc_readRaw(XAdc* adc, uint32_t* value, int32_t timeoutMs)
{
    (void)value;
    (void)timeoutMs;
    return xadc_fail(adc, XAdcError_Unsupported);
}

bool XAdc_readMillivolts(XAdc* adc, uint32_t* value, int32_t timeoutMs)
{
    (void)value;
    (void)timeoutMs;
    return xadc_fail(adc, XAdcError_Unsupported);
}

XAdcFeatures XAdc_features(const XAdc* adc)
{
    (void)adc;
    return XAdcFeature_None;
}

bool XAdc_hasFeature(const XAdc* adc, XAdcFeature feature)
{
    return adc && feature == XAdcFeature_None;
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
