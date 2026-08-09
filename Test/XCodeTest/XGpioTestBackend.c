/**
 * @file       XGpioTestBackend.c
 * @brief      XGpio 的确定性测试后端。
 * @details    本文件只在定义 XGPIO_TEST_BACKEND 且其值非零时参与编译。
 *             后端使用 XMemory 分配 GPIO 句柄，不依赖 Linux、Windows、
 *             FreeRTOS 或其他平台 API，因此可以在单元测试和 Shell 测试
 *             中验证完整的 GPIO 公共接口。
 *
 *             模拟后端用句柄成员保存配置、物理电平、中断和错误状态。
 *             processEvents() 不等待真实硬件，而是在中断已经使能且回调
 *             已注册时同步产生一个事件。这一行为使测试能够稳定覆盖
 *             gpio irq enable、gpio irq wait 和回调分发路径，同时保持与
 *             XGpio.h 约定的返回值和生命周期语义一致。
 *
 *             本文件不是产品 GPIO 驱动。产品代码应在 Drive 目录中实现
 *             相同的 XGpio 公共函数，并根据真实硬件能力准确填写 features。
 */

#if defined(XGPIO_TEST_BACKEND) && XGPIO_TEST_BACKEND

#include "XGpio.h"
#include "XMemory.h"

#include <limits.h>

/**
 * @brief XGpio 测试句柄的完整定义。
 * @details XGpio 在公共头文件中是不透明类型，只有后端可以定义其成员。
 *          所有成员均为值类型或借用指针，删除句柄时不需要额外释放子对象。
 */
struct XGpio {
    XGpioConfig m_config;                 /**< 当前 GPIO 配置副本。 */
    bool m_open;                          /**< 硬件资源是否处于打开状态。 */
    XGpioLevel m_level;                   /**< 当前模拟的物理电平。 */
    XGpioInterruptCallback m_callback;   /**< 中断回调，借用，不负责释放。 */
    void* m_userData;                     /**< 回调用户数据，借用，不负责释放。 */
    bool m_interruptEnabled;              /**< 中断是否已经使能。 */
    XGpioError m_lastError;               /**< 最近一次通用错误。 */
    int32_t m_nativeError;                /**< 模拟后端原生错误码，始终为 0。 */
};

/**
 * @brief 检查枚举值是否属于公共接口定义的范围。
 * @param value 待检查的方向值。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validDirection(XGpioDirection value)
{
    return value == XGpioDirection_Input || value == XGpioDirection_Output;
}

/**
 * @brief 检查上下拉枚举值。
 * @param value 待检查的上下拉值。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validPull(XGpioPull value)
{
    return value == XGpioPull_None || value == XGpioPull_Up ||
           value == XGpioPull_Down;
}

/**
 * @brief 检查输出类型枚举值。
 * @param value 待检查的输出类型。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validOutputType(XGpioOutputType value)
{
    return value == XGpioOutputType_PushPull ||
           value == XGpioOutputType_OpenDrain;
}

/**
 * @brief 检查初始电平枚举值。
 * @param value 待检查的初始电平。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validInitialLevel(XGpioInitialLevel value)
{
    return value == XGpioInitialLevel_Keep ||
           value == XGpioInitialLevel_Low ||
           value == XGpioInitialLevel_High;
}

/**
 * @brief 检查有效电平枚举值。
 * @param value 待检查的有效电平。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validActiveLevel(XGpioActiveLevel value)
{
    return value == XGpioActiveLevel_Low || value == XGpioActiveLevel_High;
}

/**
 * @brief 检查中断边沿枚举值。
 * @param value 待检查的边沿值。
 * @return 值有效返回 true，否则返回 false。
 */
static bool xgpio_test_validInterruptEdge(XGpioInterruptEdge value)
{
    return value == XGpioInterruptEdge_Disabled ||
           value == XGpioInterruptEdge_Rising ||
           value == XGpioInterruptEdge_Falling ||
           value == XGpioInterruptEdge_Both;
}

/**
 * @brief 记录句柄的最近一次错误。
 * @param gpio GPIO 测试句柄；不能为 NULL。
 * @param error 要记录的通用错误。
 * @return 始终返回 false，便于直接用于失败分支。
 */
static bool xgpio_test_fail(XGpio* gpio, XGpioError error)
{
    if (gpio)
        gpio->m_lastError = error;
    return false;
}

/**
 * @brief 检查并验证完整 GPIO 配置。
 * @param config 待验证配置；不能为 NULL。
 * @return 配置有效返回 true，否则返回 false。
 */
static bool xgpio_test_validConfig(const XGpioConfig* config)
{
    if (!config || config->m_flags != 0u ||
        !xgpio_test_validDirection(config->m_direction) ||
        !xgpio_test_validPull(config->m_pull) ||
        !xgpio_test_validOutputType(config->m_outputType) ||
        !xgpio_test_validInitialLevel(config->m_initialLevel) ||
        !xgpio_test_validActiveLevel(config->m_activeLevel) ||
        !xgpio_test_validInterruptEdge(config->m_interruptEdge)) {
        return false;
    }

    if (config->m_direction == XGpioDirection_Input &&
        (config->m_outputType != XGpioOutputType_PushPull ||
         config->m_initialLevel != XGpioInitialLevel_Keep)) {
        return false;
    }

    return true;
}

/**
 * @brief 根据配置的初始电平更新模拟物理电平。
 * @param gpio GPIO 测试句柄；不能为 NULL。
 * @param initialLevel 初始电平设置。
 */
static void xgpio_test_applyInitialLevel(XGpio* gpio,
                                         XGpioInitialLevel initialLevel)
{
    if (initialLevel == XGpioInitialLevel_Low)
        gpio->m_level = XGpioLevel_Low;
    else if (initialLevel == XGpioInitialLevel_High)
        gpio->m_level = XGpioLevel_High;
}

XGpio* XGpio_create(const XGpioConfig* config)
{
    XGpio* gpio;

    if (!xgpio_test_validConfig(config))
        return NULL;

    gpio = (XGpio*)XMemory_malloc(sizeof(XGpio), XMEMORY_TYPE_SYSTEM);
    if (!gpio)
        return NULL;

    gpio->m_config = *config;
    gpio->m_open = false;
    gpio->m_level = XGpioLevel_Low;
    gpio->m_callback = NULL;
    gpio->m_userData = NULL;
    gpio->m_interruptEnabled = false;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return gpio;
}

bool XGpio_open(XGpio* gpio)
{
    if (!gpio)
        return false;
    if (gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_AlreadyOpen);
    if (!xgpio_test_validConfig(&gpio->m_config))
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);

    gpio->m_open = true;
    gpio->m_interruptEnabled = false;
    xgpio_test_applyInitialLevel(gpio, gpio->m_config.m_initialLevel);
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

void XGpio_close(XGpio* gpio)
{
    if (!gpio)
        return;
    gpio->m_interruptEnabled = false;
    gpio->m_open = false;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
}

void XGpio_delete(XGpio* gpio)
{
    if (!gpio)
        return;
    XGpio_close(gpio);
    XMemory_free(gpio, XMEMORY_TYPE_SYSTEM);
}

bool XGpio_isOpen(const XGpio* gpio)
{
    return gpio ? gpio->m_open : false;
}

bool XGpio_getConfig(const XGpio* gpio, XGpioConfig* config)
{
    if (!gpio || !config)
        return false;
    *config = gpio->m_config;
    return true;
}

bool XGpio_configure(XGpio* gpio, const XGpioConfig* config)
{
    XGpioLevel oldLevel;

    if (!gpio || !config)
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);
    if (!xgpio_test_validConfig(config))
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);

    oldLevel = gpio->m_level;
    gpio->m_config = *config;
    if (gpio->m_open) {
        gpio->m_interruptEnabled = false;
        xgpio_test_applyInitialLevel(gpio, config->m_initialLevel);
        if (config->m_initialLevel == XGpioInitialLevel_Keep)
            gpio->m_level = oldLevel;
    }
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

bool XGpio_setDirection(XGpio* gpio, XGpioDirection direction)
{
    XGpioConfig config;

    if (!gpio || !xgpio_test_validDirection(direction))
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);
    if (!gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_NotOpen);

    config = gpio->m_config;
    config.m_direction = direction;
    config.m_initialLevel = XGpioInitialLevel_Keep;
    if (direction == XGpioDirection_Input)
        config.m_outputType = XGpioOutputType_PushPull;
    return XGpio_configure(gpio, &config);
}

XGpioDirection XGpio_direction(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_direction : XGpioDirection_Input;
}

bool XGpio_write(XGpio* gpio, XGpioLevel level)
{
    if (!gpio || (level != XGpioLevel_Low && level != XGpioLevel_High))
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);
    if (!gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_NotOpen);
    if (gpio->m_config.m_direction != XGpioDirection_Output)
        return xgpio_test_fail(gpio, XGpioError_Unsupported);

    gpio->m_level = level;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

bool XGpio_read(const XGpio* gpio, XGpioLevel* level)
{
    if (!gpio || !level)
        return false;
    if (!gpio->m_open)
        return false;
    *level = gpio->m_level;
    return true;
}

bool XGpio_toggle(XGpio* gpio)
{
    if (!gpio)
        return false;
    if (!gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_NotOpen);
    if (gpio->m_config.m_direction != XGpioDirection_Output)
        return xgpio_test_fail(gpio, XGpioError_Unsupported);
    gpio->m_level = gpio->m_level == XGpioLevel_Low ? XGpioLevel_High
                                                    : XGpioLevel_Low;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

bool XGpio_writeActive(XGpio* gpio, bool active)
{
    XGpioLevel level;

    if (!gpio)
        return false;
    level = active ? (gpio->m_config.m_activeLevel == XGpioActiveLevel_High
                          ? XGpioLevel_High
                          : XGpioLevel_Low)
                   : (gpio->m_config.m_activeLevel == XGpioActiveLevel_High
                          ? XGpioLevel_Low
                          : XGpioLevel_High);
    return XGpio_write(gpio, level);
}

bool XGpio_readActive(const XGpio* gpio, bool* active)
{
    XGpioLevel level;

    if (!gpio || !active || !XGpio_read(gpio, &level))
        return false;
    *active = gpio->m_config.m_activeLevel == XGpioActiveLevel_High
                  ? level == XGpioLevel_High
                  : level == XGpioLevel_Low;
    return true;
}

bool XGpio_setInterruptEdge(XGpio* gpio, XGpioInterruptEdge edge)
{
    if (!gpio || !xgpio_test_validInterruptEdge(edge))
        return xgpio_test_fail(gpio, XGpioError_InvalidArgument);
    if (!gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_NotOpen);
    gpio->m_config.m_interruptEdge = edge;
    if (edge == XGpioInterruptEdge_Disabled)
        gpio->m_interruptEnabled = false;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

XGpioInterruptEdge XGpio_interruptEdge(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_interruptEdge
                : XGpioInterruptEdge_Disabled;
}

bool XGpio_setInterruptCallback(XGpio* gpio,
                                XGpioInterruptCallback callback,
                                void* userData)
{
    if (!gpio)
        return false;
    gpio->m_callback = callback;
    gpio->m_userData = callback ? userData : NULL;
    if (!callback)
        gpio->m_interruptEnabled = false;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

bool XGpio_enableInterrupt(XGpio* gpio)
{
    if (!gpio)
        return false;
    if (!gpio->m_open)
        return xgpio_test_fail(gpio, XGpioError_NotOpen);
    if (gpio->m_config.m_interruptEdge == XGpioInterruptEdge_Disabled ||
        !gpio->m_callback) {
        return xgpio_test_fail(gpio, XGpioError_NotConfigured);
    }
    gpio->m_interruptEnabled = true;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return true;
}

void XGpio_disableInterrupt(XGpio* gpio)
{
    if (!gpio)
        return;
    gpio->m_interruptEnabled = false;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
}

bool XGpio_isInterruptEnabled(const XGpio* gpio)
{
    return gpio ? gpio->m_interruptEnabled : false;
}

XGpioProcessResult XGpio_processEvents(XGpio* gpio, int32_t timeoutMs)
{
    XGpioInterruptEvent event;
    XGpioInterruptEdge edge;

    (void)timeoutMs;
    if (!gpio || !gpio->m_open || !gpio->m_interruptEnabled ||
        !gpio->m_callback) {
        if (gpio)
            gpio->m_lastError = gpio->m_open ? XGpioError_NotConfigured
                                              : XGpioError_NotOpen;
        return XGpioProcessResult_Error;
    }

    edge = gpio->m_config.m_interruptEdge;
    if (edge == XGpioInterruptEdge_Both)
        edge = XGpioInterruptEdge_Rising;
    event.m_edge = edge;
    event.m_level = gpio->m_level;
    event.m_context = XGpioCallbackContext_Task;
    gpio->m_callback(gpio, &event, gpio->m_userData);
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    return XGpioProcessResult_Event;
}

XGpioFeatures XGpio_features(const XGpio* gpio)
{
    if (!gpio)
        return XGpioFeature_None;
    return (XGpioFeatures)(XGpioFeature_Input | XGpioFeature_Output |
                           XGpioFeature_PullUp | XGpioFeature_PullDown |
                           XGpioFeature_OpenDrain | XGpioFeature_Readback |
                           XGpioFeature_Toggle |
                           XGpioFeature_InterruptRising |
                           XGpioFeature_InterruptFalling |
                           XGpioFeature_InterruptBoth |
                           XGpioFeature_ActiveLevel |
                           XGpioFeature_ProcessEvents);
}

bool XGpio_hasFeature(const XGpio* gpio, XGpioFeature feature)
{
    if (!gpio || feature == XGpioFeature_None)
        return false;
    return (XGpio_features(gpio) & (XGpioFeatures)feature) ==
           (XGpioFeatures)feature;
}

XGpioNativeHandle XGpio_handle(const XGpio* gpio)
{
    if (!gpio || !gpio->m_open)
        return XGPIO_INVALID_NATIVE_HANDLE;
#if INTPTR_MAX > INT32_MAX
    return (XGpioNativeHandle)(((uint64_t)gpio->m_config.m_pin.m_controller
                                << 32) |
                               (uint64_t)gpio->m_config.m_pin.m_line);
#else
    return (XGpioNativeHandle)(gpio->m_config.m_pin.m_line & 0x7fffffffu);
#endif
}

XGpioError XGpio_lastError(const XGpio* gpio)
{
    return gpio ? gpio->m_lastError : XGpioError_InvalidArgument;
}

int32_t XGpio_nativeError(const XGpio* gpio)
{
    return gpio ? gpio->m_nativeError : 0;
}

void XGpio_clearError(XGpio* gpio)
{
    if (!gpio)
        return;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
}

const char* XGpio_errorString(XGpioError error)
{
    switch (error) {
    case XGpioError_None: return "None";
    case XGpioError_InvalidArgument: return "Invalid argument";
    case XGpioError_NotOpen: return "Not open";
    case XGpioError_AlreadyOpen: return "Already open";
    case XGpioError_NotConfigured: return "Not configured";
    case XGpioError_Unsupported: return "Unsupported";
    case XGpioError_Busy: return "Busy";
    case XGpioError_PermissionDenied: return "Permission denied";
    case XGpioError_Timeout: return "Timeout";
    case XGpioError_Interrupted: return "Interrupted";
    case XGpioError_Hardware: return "Hardware error";
    case XGpioError_Closed: return "Closed";
    case XGpioError_Unknown: return "Unknown";
    default: return "Unknown";
    }
}

#endif /* defined(XGPIO_TEST_BACKEND) && XGPIO_TEST_BACKEND */
