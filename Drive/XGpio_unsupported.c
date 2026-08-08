/**
 * @file       XGpio_unsupported.c
 * @brief      尚未接入 GPIO 驱动的平台存根。
 * @details    当构建目标不是 Linux，且工程没有另外提供 XGpio 后端时，本文件
 *             为公共 GPIO API 提供可链接的安全实现。存根不调用任何平台 API，
 *             不伪造引脚状态，不申请外部资源；open、读写和中断操作明确返回
 *             XGpioError_Unsupported。create、getConfig、configure 等纯软件
 *             操作仍保持可用，使上层能够先完成配置，再由产品平台驱动替换
 *             本文件。产品接入 GPIO 时应在 Drive 对应目录提供同名实现，并
 *             通过平台宏排除本存根，不能与另一个后端同时链接。
 */

#include "XGpio.h"

#if !defined(__linux__) && !(defined(XGPIO_TEST_BACKEND) && XGPIO_TEST_BACKEND)

#include "XMemory.h"

/** @brief 未接入平台 GPIO 句柄的最小软件状态。 */
struct XGpio {
    XGpioConfig m_config;                 /**< 配置副本。 */
    XGpioError m_lastError;               /**< 最近一次通用错误。 */
    int32_t m_nativeError;                /**< 存根没有原生错误，始终为 0。 */
    XGpioInterruptCallback m_callback;   /**< 借用的回调地址。 */
    void* m_userData;                     /**< 借用的回调数据。 */
};

static void xgpio_unsupported_set_error(XGpio* gpio, XGpioError error)
{
    if (!gpio) return;
    gpio->m_lastError = error;
    gpio->m_nativeError = 0;
}

static bool xgpio_unsupported_valid_config(const XGpioConfig* config)
{
    if (!config) return false;
    if (config->m_direction > XGpioDirection_Output ||
        config->m_pull > XGpioPull_Down ||
        config->m_outputType > XGpioOutputType_OpenDrain ||
        config->m_initialLevel > XGpioInitialLevel_High ||
        config->m_activeLevel > XGpioActiveLevel_High ||
        config->m_interruptEdge > XGpioInterruptEdge_Both ||
        config->m_flags != 0u) return false;
    if (config->m_direction == XGpioDirection_Input &&
        (config->m_outputType != XGpioOutputType_PushPull ||
         config->m_initialLevel != XGpioInitialLevel_Keep)) return false;
    if (config->m_direction == XGpioDirection_Output &&
        (config->m_interruptEdge != XGpioInterruptEdge_Disabled ||
         config->m_debounceUs != 0u)) return false;
    return true;
}

/** @brief 创建软件配置句柄；不会访问硬件。 */
XGpio* XGpio_create(const XGpioConfig* config)
{
    XGpio* gpio;
    if (!xgpio_unsupported_valid_config(config)) return NULL;
    gpio = (XGpio*)XCalloc_System(1u, sizeof(*gpio));
    if (!gpio) return NULL;
    gpio->m_config = *config;
    gpio->m_lastError = XGpioError_None;
    return gpio;
}

/** @brief 存根不具备硬件打开能力。 */
bool XGpio_open(XGpio* gpio)
{
    if (!gpio) return false;
    xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 存根没有资源需要关闭，重复调用安全。 */
void XGpio_close(XGpio* gpio)
{
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_None);
}

/** @brief 释放软件配置句柄。 */
void XGpio_delete(XGpio* gpio)
{
    if (gpio) XFree_System(gpio);
}

/** @brief 存根永远没有打开的硬件 line。 */
bool XGpio_isOpen(const XGpio* gpio)
{
    return false;
}

/** @brief 返回保存的配置副本。 */
bool XGpio_getConfig(const XGpio* gpio, XGpioConfig* config)
{
    if (!gpio || !config) return false;
    *config = gpio->m_config;
    xgpio_unsupported_set_error((XGpio*)gpio, XGpioError_None);
    return true;
}

/** @brief 保存配置；没有硬件时不执行实际配置。 */
bool XGpio_configure(XGpio* gpio, const XGpioConfig* config)
{
    if (!gpio || !config) return false;
    if (!xgpio_unsupported_valid_config(config)) {
        xgpio_unsupported_set_error(gpio, XGpioError_InvalidArgument);
        return false;
    }
    gpio->m_config = *config;
    xgpio_unsupported_set_error(gpio, XGpioError_None);
    return true;
}

/** @brief 修改保存配置中的方向。 */
bool XGpio_setDirection(XGpio* gpio, XGpioDirection direction)
{
    XGpioConfig config;
    if (!gpio) return false;
    config = gpio->m_config;
    config.m_direction = direction;
    if (direction == XGpioDirection_Input) {
        config.m_outputType = XGpioOutputType_PushPull;
        config.m_initialLevel = XGpioInitialLevel_Keep;
    } else if (direction == XGpioDirection_Output) {
        config.m_interruptEdge = XGpioInterruptEdge_Disabled;
        config.m_debounceUs = 0u;
    }
    return XGpio_configure(gpio, &config);
}

/** @brief 返回保存的方向，NULL 返回输入。 */
XGpioDirection XGpio_direction(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_direction : XGpioDirection_Input;
}

/** @brief 存根不伪造物理写入。 */
bool XGpio_write(XGpio* gpio, XGpioLevel level)
{
    (void)level;
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 存根不伪造物理读取。 */
bool XGpio_read(const XGpio* gpio, XGpioLevel* level)
{
    (void)level;
    if (gpio) xgpio_unsupported_set_error((XGpio*)gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 存根不执行翻转。 */
bool XGpio_toggle(XGpio* gpio)
{
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 存根不执行有效电平写入。 */
bool XGpio_writeActive(XGpio* gpio, bool active)
{
    (void)active;
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 存根不执行有效电平读取。 */
bool XGpio_readActive(const XGpio* gpio, bool* active)
{
    (void)active;
    if (gpio) xgpio_unsupported_set_error((XGpio*)gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 保存边沿配置，但不申请硬件中断。 */
bool XGpio_setInterruptEdge(XGpio* gpio, XGpioInterruptEdge edge)
{
    XGpioConfig config;
    if (!gpio || edge > XGpioInterruptEdge_Both) return false;
    config = gpio->m_config;
    config.m_interruptEdge = edge;
    return XGpio_configure(gpio, &config);
}

/** @brief 返回保存的中断边沿。 */
XGpioInterruptEdge XGpio_interruptEdge(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_interruptEdge : XGpioInterruptEdge_Disabled;
}

/** @brief 保存或清除回调地址，不会产生事件。 */
bool XGpio_setInterruptCallback(XGpio* gpio, XGpioInterruptCallback callback, void* userData)
{
    if (!gpio) return false;
    gpio->m_callback = callback;
    gpio->m_userData = callback ? userData : NULL;
    xgpio_unsupported_set_error(gpio, XGpioError_None);
    return true;
}

/** @brief 未接入平台不支持使能中断。 */
bool XGpio_enableInterrupt(XGpio* gpio)
{
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return false;
}

/** @brief 清除存根中的中断使能请求。 */
void XGpio_disableInterrupt(XGpio* gpio)
{
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_None);
}

/** @brief 存根永远没有已使能的中断。 */
bool XGpio_isInterruptEnabled(const XGpio* gpio)
{
    return false;
}

/** @brief 未接入平台不支持事件处理。 */
XGpioProcessResult XGpio_processEvents(XGpio* gpio, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_Unsupported);
    return XGpioProcessResult_Error;
}

/** @brief 存根报告没有 GPIO 硬件能力。 */
XGpioFeatures XGpio_features(const XGpio* gpio)
{
    (void)gpio;
    return XGpioFeature_None;
}

/** @brief 存根不支持任何 GPIO 能力。 */
bool XGpio_hasFeature(const XGpio* gpio, XGpioFeature feature)
{
    (void)gpio;
    (void)feature;
    return false;
}

/** @brief 存根没有原生 GPIO 句柄。 */
XGpioNativeHandle XGpio_handle(const XGpio* gpio)
{
    (void)gpio;
    return XGPIO_INVALID_NATIVE_HANDLE;
}

/** @brief 返回最近一次存根错误。 */
XGpioError XGpio_lastError(const XGpio* gpio)
{
    return gpio ? gpio->m_lastError : XGpioError_InvalidArgument;
}

/** @brief 存根没有原生错误码。 */
int32_t XGpio_nativeError(const XGpio* gpio)
{
    return gpio ? gpio->m_nativeError : 0;
}

/** @brief 清除存根错误状态。 */
void XGpio_clearError(XGpio* gpio)
{
    if (gpio) xgpio_unsupported_set_error(gpio, XGpioError_None);
}

/** @brief 返回通用 GPIO 错误描述。 */
const char* XGpio_errorString(XGpioError error)
{
    switch (error) {
    case XGpioError_None: return "None";
    case XGpioError_InvalidArgument: return "InvalidArgument";
    case XGpioError_NotOpen: return "NotOpen";
    case XGpioError_AlreadyOpen: return "AlreadyOpen";
    case XGpioError_NotConfigured: return "NotConfigured";
    case XGpioError_Unsupported: return "Unsupported";
    case XGpioError_Busy: return "Busy";
    case XGpioError_PermissionDenied: return "PermissionDenied";
    case XGpioError_Timeout: return "Timeout";
    case XGpioError_Interrupted: return "Interrupted";
    case XGpioError_Hardware: return "Hardware";
    case XGpioError_Closed: return "Closed";
    default: return "Unknown";
    }
}

#endif /* !defined(__linux__) && !XGPIO_TEST_BACKEND */
