/**
 * @file       XGpio_posix.c
 * @brief      Linux GPIO character device 后端。
 * @details    本文件把 XGpio 的逻辑控制器和 line 编号映射到
 *             /dev/gpiochipN，并优先使用 Linux GPIO character device ABI v2。
 *             v2 支持配置、读写、边沿事件、偏置、开漏和去抖；当内核只提供
 *             已弃用的 ABI v1 时，后端退化为基本的输入输出读写。公共层不
 *             暴露任何 Linux 结构体，所有 ioctl、poll 和文件描述符操作都
 *             被限制在本平台驱动内。
 *
 *             Linux GPIO line 是独占资源。XGpio_open 成功后，直到
 *             XGpio_close 或 XGpio_delete 才释放 line。XGpioInitialLevel_Keep
 *             在 Linux 上通过一次临时输入请求读取当前电平，再以该电平请求
 *             输出；这能最大限度保持电平，但芯片在切换方向期间仍可能产生
 *             极短的硬件窗口，不能替代板级无毛刺切换电路。
 */

#include "XGpio.h"

#if defined(__linux__) && !(defined(XGPIO_TEST_BACKEND) && XGPIO_TEST_BACKEND)

#include "XMemory.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/gpio.h>

#define XGPIO_CONSUMER "XinYueC"
#define XGPIO_PATH_MAX 64u

/** @brief Linux GPIO 句柄的固定大小私有状态。 */
struct XGpio {
    XGpioConfig m_config;                 /**< 调用者最后成功设置的配置副本。 */
    int m_chipFd;                         /**< /dev/gpiochipN 文件描述符。 */
    int m_lineFd;                         /**< GPIO line 请求文件描述符。 */
    XGpioFeatures m_features;             /**< 当前后端可用的能力位。 */
    XGpioError m_lastError;               /**< 最近一次通用错误。 */
    int32_t m_nativeError;                /**< 最近一次 errno；成功时为 0。 */
    XGpioInterruptCallback m_callback;   /**< GPIO 边沿回调，借用。 */
    void* m_userData;                     /**< 回调用户数据，借用。 */
    XGpioLevel m_cachedLevel;             /**< 最近一次已知物理电平。 */
    uint8_t m_open;                       /**< 是否已占用硬件 line。 */
    uint8_t m_v2;                         /**< 当前请求使用 GPIO ABI v2。 */
    uint8_t m_interruptEnabled;           /**< 是否允许 processEvents 分发。 */
    uint8_t m_cachedValid;                /**< cachedLevel 是否有效。 */
};

static void xgpio_set_error(XGpio* gpio, XGpioError error, int nativeError)
{
    if (!gpio) return;
    gpio->m_lastError = error;
    gpio->m_nativeError = (int32_t)nativeError;
}

static void xgpio_success(XGpio* gpio)
{
    xgpio_set_error(gpio, XGpioError_None, 0);
}

static XGpioError xgpio_error_from_errno(int value)
{
    switch (value) {
    case 0: return XGpioError_None;
    case EINVAL: return XGpioError_InvalidArgument;
    case EBUSY: return XGpioError_Busy;
    case EACCES:
    case EPERM: return XGpioError_PermissionDenied;
    case ETIMEDOUT: return XGpioError_Timeout;
    case EINTR: return XGpioError_Interrupted;
    case ENOTTY:
    case EOPNOTSUPP:
    case ENOSYS: return XGpioError_Unsupported;
    case EBADF: return XGpioError_Closed;
    case ENODEV:
    case ENXIO:
    case ENOENT:
    case EIO: return XGpioError_Hardware;
    default: return XGpioError_Unknown;
    }
}

static bool xgpio_fail(XGpio* gpio, XGpioError error, int nativeError)
{
    xgpio_set_error(gpio, error, nativeError);
    return false;
}

static bool xgpio_valid_config(const XGpioConfig* config, XGpioError* error)
{
    if (!config) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (config->m_direction != XGpioDirection_Input &&
        config->m_direction != XGpioDirection_Output) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (config->m_pull > XGpioPull_Down ||
        config->m_outputType > XGpioOutputType_OpenDrain ||
        config->m_initialLevel > XGpioInitialLevel_High ||
        config->m_activeLevel > XGpioActiveLevel_High ||
        config->m_interruptEdge > XGpioInterruptEdge_Both ||
        config->m_flags != 0u) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (config->m_direction == XGpioDirection_Input &&
        (config->m_outputType != XGpioOutputType_PushPull ||
         config->m_initialLevel != XGpioInitialLevel_Keep)) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (config->m_direction == XGpioDirection_Output &&
        (config->m_interruptEdge != XGpioInterruptEdge_Disabled ||
         config->m_debounceUs != 0u)) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (config->m_direction == XGpioDirection_Input &&
        config->m_outputType != XGpioOutputType_PushPull) {
        if (error) *error = XGpioError_InvalidArgument;
        return false;
    }
    if (error) *error = XGpioError_None;
    return true;
}

static bool xgpio_path(uint32_t controller, char* path, size_t pathSize)
{
    int length;
    if (!path || pathSize == 0u) return false;
    length = snprintf(path, pathSize, "/dev/gpiochip%u", controller);
    return length >= 0 && (size_t)length < pathSize;
}

#if defined(GPIO_V2_GET_LINE_IOCTL)
#define XGPIO_HAVE_V2 1
#else
#define XGPIO_HAVE_V2 0
#endif

#if XGPIO_HAVE_V2
static uint64_t xgpio_v2_flags(const XGpioConfig* config, bool includeEdges)
{
    uint64_t flags = config->m_direction == XGpioDirection_Input
                   ? GPIO_V2_LINE_FLAG_INPUT : GPIO_V2_LINE_FLAG_OUTPUT;
    if (config->m_direction == XGpioDirection_Output &&
        config->m_outputType == XGpioOutputType_OpenDrain)
        flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
    if (config->m_pull == XGpioPull_None)
        flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
    else if (config->m_pull == XGpioPull_Up)
        flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    else if (config->m_pull == XGpioPull_Down)
        flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    if (includeEdges && config->m_direction == XGpioDirection_Input) {
        if (config->m_interruptEdge == XGpioInterruptEdge_Rising ||
            config->m_interruptEdge == XGpioInterruptEdge_Both)
            flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
        if (config->m_interruptEdge == XGpioInterruptEdge_Falling ||
            config->m_interruptEdge == XGpioInterruptEdge_Both)
            flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
    }
    return flags;
}

static void xgpio_v2_config(const XGpioConfig* config,
                            XGpioLevel outputLevel,
                            bool includeEdges,
                            struct gpio_v2_line_config* result)
{
    memset(result, 0, sizeof(*result));
    result->flags = xgpio_v2_flags(config, includeEdges);
    if (config->m_direction == XGpioDirection_Output) {
        result->attrs[result->num_attrs].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        result->attrs[result->num_attrs].attr.values = outputLevel == XGpioLevel_High ? 1u : 0u;
        result->attrs[result->num_attrs].mask = 1u;
        ++result->num_attrs;
    }
    if (config->m_debounceUs != 0u) {
        result->attrs[result->num_attrs].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
        result->attrs[result->num_attrs].attr.debounce_period_us = config->m_debounceUs;
        result->attrs[result->num_attrs].mask = 1u;
        ++result->num_attrs;
    }
}

static bool xgpio_v2_request(int chipFd, const XGpioConfig* config,
                             XGpioLevel outputLevel, int* lineFd, int* errorValue)
{
    struct gpio_v2_line_request request;
    memset(&request, 0, sizeof(request));
    request.offsets[0] = config->m_pin.m_line;
    request.num_lines = 1u;
    request.event_buffer_size = 16u;
    memcpy(request.consumer, XGPIO_CONSUMER, sizeof(XGPIO_CONSUMER));
    xgpio_v2_config(config, outputLevel, false, &request.config);
    if (ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &request) < 0) {
        if (errorValue) *errorValue = errno;
        return false;
    }
    *lineFd = request.fd;
    return true;
}

static bool xgpio_v2_set_config(XGpio* gpio, bool includeEdges)
{
    struct gpio_v2_line_config config;
    xgpio_v2_config(&gpio->m_config, gpio->m_cachedLevel, includeEdges, &config);
    if (ioctl(gpio->m_lineFd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &config) < 0)
        return xgpio_fail(gpio, xgpio_error_from_errno(errno), errno);
    xgpio_success(gpio);
    return true;
}
#endif

static bool xgpio_v1_supported_config(const XGpioConfig* config)
{
    return config->m_pull == XGpioPull_None &&
           config->m_outputType == XGpioOutputType_PushPull &&
           config->m_debounceUs == 0u &&
           config->m_interruptEdge == XGpioInterruptEdge_Disabled;
}

static bool xgpio_v1_request(int chipFd, const XGpioConfig* config,
                             XGpioLevel outputLevel, int* lineFd, int* errorValue)
{
    struct gpiohandle_request request;
    uint32_t flags = config->m_direction == XGpioDirection_Input
                   ? GPIOHANDLE_REQUEST_INPUT : GPIOHANDLE_REQUEST_OUTPUT;
    memset(&request, 0, sizeof(request));
    request.lineoffsets[0] = config->m_pin.m_line;
    request.lines = 1u;
    request.flags = flags;
    request.default_values[0] = outputLevel == XGpioLevel_High ? 1u : 0u;
    memcpy(request.consumer_label, XGPIO_CONSUMER, sizeof(XGPIO_CONSUMER));
    if (ioctl(chipFd, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0) {
        if (errorValue) *errorValue = errno;
        return false;
    }
    *lineFd = request.fd;
    return true;
}

static bool xgpio_read_fd(bool v2, int lineFd, XGpioLevel* level, int* errorValue)
{
    if (v2) {
#if XGPIO_HAVE_V2
        struct gpio_v2_line_values values;
        memset(&values, 0, sizeof(values));
        values.mask = 1u;
        if (ioctl(lineFd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values) < 0) {
            if (errorValue) *errorValue = errno;
            return false;
        }
        *level = (values.bits & 1u) ? XGpioLevel_High : XGpioLevel_Low;
        return true;
#else
        (void)lineFd; (void)level; (void)errorValue;
        return false;
#endif
    } else {
        struct gpiohandle_data values;
        memset(&values, 0, sizeof(values));
        if (ioctl(lineFd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &values) < 0) {
            if (errorValue) *errorValue = errno;
            return false;
        }
        *level = values.values[0] ? XGpioLevel_High : XGpioLevel_Low;
        return true;
    }
}

static bool xgpio_write_fd(bool v2, int lineFd, XGpioLevel level, int* errorValue)
{
    if (v2) {
#if XGPIO_HAVE_V2
        struct gpio_v2_line_values values;
        memset(&values, 0, sizeof(values));
        values.bits = level == XGpioLevel_High ? 1u : 0u;
        values.mask = 1u;
        if (ioctl(lineFd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
            if (errorValue) *errorValue = errno;
            return false;
        }
        return true;
#else
        (void)lineFd; (void)level; (void)errorValue;
        return false;
#endif
    } else {
        struct gpiohandle_data values;
        memset(&values, 0, sizeof(values));
        values.values[0] = level == XGpioLevel_High ? 1u : 0u;
        if (ioctl(lineFd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &values) < 0) {
            if (errorValue) *errorValue = errno;
            return false;
        }
        return true;
    }
}

static bool xgpio_probe_level(int chipFd, const XGpioConfig* config,
                              bool useV2, XGpioLevel* level, int* errorValue)
{
    XGpioConfig input = *config;
    int lineFd = -1;
    bool ok;
    input.m_direction = XGpioDirection_Input;
    input.m_outputType = XGpioOutputType_PushPull;
    input.m_initialLevel = XGpioInitialLevel_Keep;
    input.m_interruptEdge = XGpioInterruptEdge_Disabled;
    input.m_debounceUs = 0u;
#if XGPIO_HAVE_V2
    if (useV2)
        ok = xgpio_v2_request(chipFd, &input, XGpioLevel_Low, &lineFd, errorValue);
    else
#else
    (void)useV2;
#endif
    if (!useV2 || !XGPIO_HAVE_V2)
        ok = xgpio_v1_request(chipFd, &input, XGpioLevel_Low, &lineFd, errorValue);
    if (!ok) return false;
    ok = xgpio_read_fd(useV2, lineFd, level, errorValue);
    close(lineFd);
    return ok;
}

static bool xgpio_close_fds(XGpio* gpio)
{
    bool ok = true;
    int saved = 0;
    if (gpio->m_lineFd >= 0) {
        if (close(gpio->m_lineFd) < 0) { ok = false; saved = errno; }
        gpio->m_lineFd = -1;
    }
    if (gpio->m_chipFd >= 0) {
        if (close(gpio->m_chipFd) < 0 && ok) { ok = false; saved = errno; }
        gpio->m_chipFd = -1;
    }
    gpio->m_open = 0u;
    gpio->m_v2 = 0u;
    gpio->m_interruptEnabled = 0u;
    if (!ok) xgpio_set_error(gpio, xgpio_error_from_errno(saved), saved);
    return ok;
}

static bool xgpio_open_config(XGpio* gpio, const XGpioConfig* config,
                              bool haveInitial, XGpioLevel initialLevel)
{
    char path[XGPIO_PATH_MAX];
    struct gpiochip_info chipInfo;
    int chipFd = -1;
    int lineFd = -1;
    int errorValue = 0;
    bool useV2 = false;
    XGpioLevel effectiveLevel = initialLevel;

    /* 显式初始电平优先于调用者为 Keep 回滚场景传入的临时值。 */
    if (config->m_initialLevel == XGpioInitialLevel_Low)
        effectiveLevel = XGpioLevel_Low;
    else if (config->m_initialLevel == XGpioInitialLevel_High)
        effectiveLevel = XGpioLevel_High;

    if (!xgpio_path(config->m_pin.m_controller, path, sizeof(path)))
        return xgpio_fail(gpio, XGpioError_InvalidArgument, 0);
    chipFd = open(path, O_RDWR | O_CLOEXEC);
    if (chipFd < 0) {
        /* 某些只读 GPIO 控制器拒绝 O_RDWR，输入操作允许退回只读打开。 */
        if (config->m_direction == XGpioDirection_Input)
            chipFd = open(path, O_RDONLY | O_CLOEXEC);
    }
    if (chipFd < 0)
        return xgpio_fail(gpio, xgpio_error_from_errno(errno), errno);
    memset(&chipInfo, 0, sizeof(chipInfo));
    if (ioctl(chipFd, GPIO_GET_CHIPINFO_IOCTL, &chipInfo) < 0) {
        errorValue = errno;
        close(chipFd);
        return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
    }
    if (config->m_pin.m_line >= chipInfo.lines) {
        close(chipFd);
        return xgpio_fail(gpio, XGpioError_InvalidArgument, 0);
    }
    if (config->m_direction == XGpioDirection_Output &&
        config->m_initialLevel == XGpioInitialLevel_Keep && !haveInitial) {
#if XGPIO_HAVE_V2
        if (!xgpio_probe_level(chipFd, config, true, &effectiveLevel, &errorValue)) {
            if (errorValue != ENOTTY) {
                close(chipFd);
                return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
            }
        } else {
            useV2 = true;
        }
#endif
        if (!useV2) {
            if (!xgpio_probe_level(chipFd, config, false, &effectiveLevel, &errorValue)) {
                close(chipFd);
                return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
            }
        }
        haveInitial = true;
    }
#if XGPIO_HAVE_V2
    if (!useV2) {
        if (xgpio_v2_request(chipFd, config,
                             haveInitial ? effectiveLevel : XGpioLevel_Low,
                             &lineFd, &errorValue))
            useV2 = true;
        else if (errorValue != ENOTTY) {
            close(chipFd);
            return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
        }
    } else {
        /* Keep 的临时输入请求只用于读取电平，仍需重新申请实际输出 line。 */
        if (!xgpio_v2_request(chipFd, config,
                              haveInitial ? effectiveLevel : XGpioLevel_Low,
                              &lineFd, &errorValue)) {
            close(chipFd);
            return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
        }
    }
#endif
    if (!useV2) {
        if (!xgpio_v1_supported_config(config)) {
            close(chipFd);
            return xgpio_fail(gpio, XGpioError_Unsupported, ENOTSUP);
        }
        if (!xgpio_v1_request(chipFd, config,
                              haveInitial ? effectiveLevel : XGpioLevel_Low,
                              &lineFd, &errorValue)) {
            close(chipFd);
            return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
        }
    }
    gpio->m_chipFd = chipFd;
    gpio->m_lineFd = lineFd;
    gpio->m_v2 = useV2 ? 1u : 0u;
    gpio->m_open = 1u;
    gpio->m_interruptEnabled = 0u;
    if (config->m_direction == XGpioDirection_Output) {
        gpio->m_cachedLevel = haveInitial ? effectiveLevel : XGpioLevel_Low;
        gpio->m_cachedValid = 1u;
    } else {
        gpio->m_cachedValid = 0u;
    }
    xgpio_success(gpio);
    return true;
}

/* -------------------------------------------------------------------------
 * XGpio 公共生命周期接口。
 * ------------------------------------------------------------------------- */

/** @brief 创建 Linux GPIO 软件句柄，不在此处打开设备。 */
XGpio* XGpio_create(const XGpioConfig* config)
{
    XGpioError error;
    XGpio* gpio;
    if (!xgpio_valid_config(config, &error)) return NULL;
    gpio = (XGpio*)XCalloc_System(1u, sizeof(*gpio));
    if (!gpio) return NULL;
    gpio->m_config = *config;
    gpio->m_chipFd = -1;
    gpio->m_lineFd = -1;
    gpio->m_lastError = XGpioError_None;
    gpio->m_nativeError = 0;
    gpio->m_cachedLevel = XGpioLevel_Low;
    gpio->m_features = XGpioFeature_Input | XGpioFeature_Output |
                       XGpioFeature_Readback | XGpioFeature_Toggle |
                       XGpioFeature_ActiveLevel;
#if XGPIO_HAVE_V2
    gpio->m_features |= XGpioFeature_PullUp | XGpioFeature_PullDown |
                        XGpioFeature_OpenDrain | XGpioFeature_InterruptRising |
                        XGpioFeature_InterruptFalling | XGpioFeature_InterruptBoth |
                        XGpioFeature_Debounce | XGpioFeature_ProcessEvents;
#endif
    return gpio;
}

/** @brief 打开 GPIO chip 和 line，并应用句柄配置。 */
bool XGpio_open(XGpio* gpio)
{
    XGpioError error;
    if (!gpio) return false;
    if (gpio->m_open) return xgpio_fail(gpio, XGpioError_AlreadyOpen, EBUSY);
    if (!xgpio_valid_config(&gpio->m_config, &error))
        return xgpio_fail(gpio, error, 0);
    return xgpio_open_config(gpio, &gpio->m_config, false, XGpioLevel_Low);
}

/** @brief 关闭 line 和 chip 文件描述符，保留软件句柄及配置。 */
void XGpio_close(XGpio* gpio)
{
    if (!gpio) return;
    if (!gpio->m_open) {
        xgpio_success(gpio);
        return;
    }
    if (xgpio_close_fds(gpio)) xgpio_success(gpio);
}

/** @brief 先关闭硬件资源，再释放 Linux GPIO 句柄。 */
void XGpio_delete(XGpio* gpio)
{
    if (!gpio) return;
    XGpio_close(gpio);
    XFree_System(gpio);
}

/** @brief 判断 GPIO line 是否处于已打开状态。 */
bool XGpio_isOpen(const XGpio* gpio)
{
    return gpio ? gpio->m_open != 0u : false;
}

/* -------------------------------------------------------------------------
 * 配置和读写接口。
 * ------------------------------------------------------------------------- */

/** @brief 返回配置副本。 */
bool XGpio_getConfig(const XGpio* gpio, XGpioConfig* config)
{
    if (!gpio || !config) return gpio ? xgpio_fail((XGpio*)gpio, XGpioError_InvalidArgument, 0) : false;
    *config = gpio->m_config;
    xgpio_success((XGpio*)gpio);
    return true;
}

/** @brief 关闭并重新申请 line，以应用新的完整配置。 */
bool XGpio_configure(XGpio* gpio, const XGpioConfig* config)
{
    XGpioConfig oldConfig;
    XGpioLevel oldLevel = XGpioLevel_Low;
    XGpioLevel newLevel = XGpioLevel_Low;
    bool oldWasOpen;
    bool oldLevelValid = false;
    bool newLevelValid = false;
    XGpioError error;
    int savedNative;
    XGpioError savedError;
    if (!gpio || !config) return false;
    if (!xgpio_valid_config(config, &error)) return xgpio_fail(gpio, error, 0);
    oldConfig = gpio->m_config;
    oldWasOpen = gpio->m_open != 0u;
    if (!oldWasOpen) {
        gpio->m_config = *config;
        xgpio_success(gpio);
        return true;
    }
    if (oldConfig.m_direction == XGpioDirection_Output && gpio->m_cachedValid) {
        oldLevel = gpio->m_cachedLevel;
        oldLevelValid = true;
    } else if (oldConfig.m_direction == XGpioDirection_Output) {
        int e = 0;
        oldLevelValid = xgpio_read_fd(gpio->m_v2 != 0u, gpio->m_lineFd, &oldLevel, &e);
    }
    if (config->m_direction == XGpioDirection_Output &&
        config->m_initialLevel == XGpioInitialLevel_Keep && oldLevelValid &&
        oldConfig.m_pin.m_controller == config->m_pin.m_controller &&
        oldConfig.m_pin.m_line == config->m_pin.m_line) {
        newLevel = oldLevel;
        newLevelValid = true;
    }
    if (xgpio_close_fds(gpio)) {
        gpio->m_config = *config;
        if (xgpio_open_config(gpio, config, newLevelValid, newLevel)) return true;
        savedError = gpio->m_lastError;
        savedNative = gpio->m_nativeError;
        gpio->m_config = oldConfig;
        if (xgpio_open_config(gpio, &oldConfig, oldLevelValid, oldLevel)) {
            xgpio_set_error(gpio, savedError, savedNative);
            return false;
        }
        return xgpio_fail(gpio, XGpioError_Hardware, savedNative);
    }
    return false;
}

/** @brief 修改配置中的方向并复用完整配置应用路径。 */
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

/** @brief 返回当前方向，NULL 使用输入作为安全默认值。 */
XGpioDirection XGpio_direction(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_direction : XGpioDirection_Input;
}

/** @brief 写入物理电平。 */
bool XGpio_write(XGpio* gpio, XGpioLevel level)
{
    int errorValue = 0;
    if (!gpio) return false;
    if (!gpio->m_open) return xgpio_fail(gpio, XGpioError_NotOpen, 0);
    if (gpio->m_config.m_direction != XGpioDirection_Output)
        return xgpio_fail(gpio, XGpioError_InvalidArgument, EINVAL);
    if (level != XGpioLevel_Low && level != XGpioLevel_High)
        return xgpio_fail(gpio, XGpioError_InvalidArgument, EINVAL);
    if (!xgpio_write_fd(gpio->m_v2 != 0u, gpio->m_lineFd, level, &errorValue))
        return xgpio_fail(gpio, xgpio_error_from_errno(errorValue), errorValue);
    gpio->m_cachedLevel = level;
    gpio->m_cachedValid = 1u;
    xgpio_success(gpio);
    return true;
}

/** @brief 读取物理电平。 */
bool XGpio_read(const XGpio* gpio, XGpioLevel* level)
{
    int errorValue = 0;
    if (!gpio || !level) return gpio ? xgpio_fail((XGpio*)gpio, XGpioError_InvalidArgument, EINVAL) : false;
    if (!gpio->m_open) return xgpio_fail((XGpio*)gpio, XGpioError_NotOpen, 0);
    if (!xgpio_read_fd(gpio->m_v2 != 0u, gpio->m_lineFd, level, &errorValue))
        return xgpio_fail((XGpio*)gpio, xgpio_error_from_errno(errorValue), errorValue);
    ((XGpio*)gpio)->m_cachedLevel = *level;
    ((XGpio*)gpio)->m_cachedValid = 1u;
    xgpio_success((XGpio*)gpio);
    return true;
}

/** @brief 使用读后写实现非原子翻转。 */
bool XGpio_toggle(XGpio* gpio)
{
    XGpioLevel level;
    if (!gpio || gpio->m_config.m_direction != XGpioDirection_Output)
        return gpio ? xgpio_fail(gpio, XGpioError_InvalidArgument, EINVAL) : false;
    if (!XGpio_read(gpio, &level)) return false;
    return XGpio_write(gpio, level == XGpioLevel_High ? XGpioLevel_Low : XGpioLevel_High);
}

/** @brief 按有效电平转换后写入物理电平。 */
bool XGpio_writeActive(XGpio* gpio, bool active)
{
    XGpioActiveLevel activeLevel;
    if (!gpio) return false;
    activeLevel = gpio->m_config.m_activeLevel;
    return XGpio_write(gpio, active ? (activeLevel == XGpioActiveLevel_High ? XGpioLevel_High : XGpioLevel_Low)
                                    : (activeLevel == XGpioActiveLevel_High ? XGpioLevel_Low : XGpioLevel_High));
}

/** @brief 读取物理电平并转换为有效状态。 */
bool XGpio_readActive(const XGpio* gpio, bool* active)
{
    XGpioLevel level;
    if (!gpio || !active) return gpio ? xgpio_fail((XGpio*)gpio, XGpioError_InvalidArgument, EINVAL) : false;
    if (!XGpio_read(gpio, &level)) return false;
    *active = gpio->m_config.m_activeLevel == XGpioActiveLevel_High
            ? level == XGpioLevel_High : level == XGpioLevel_Low;
    return true;
}

/* -------------------------------------------------------------------------
 * 中断、能力和错误接口。
 * ------------------------------------------------------------------------- */

/** @brief 保存边沿配置；已打开的 v2 句柄会立即更新硬件配置。 */
bool XGpio_setInterruptEdge(XGpio* gpio, XGpioInterruptEdge edge)
{
    XGpioConfig config;
    if (!gpio) return false;
    if (edge > XGpioInterruptEdge_Both)
        return xgpio_fail(gpio, XGpioError_InvalidArgument, EINVAL);
    if (gpio->m_open && !gpio->m_v2 && edge != XGpioInterruptEdge_Disabled)
        return xgpio_fail(gpio, XGpioError_Unsupported, ENOTSUP);
    config = gpio->m_config;
    config.m_interruptEdge = edge;
    return XGpio_configure(gpio, &config);
}

/** @brief 返回当前边沿配置。 */
XGpioInterruptEdge XGpio_interruptEdge(const XGpio* gpio)
{
    return gpio ? gpio->m_config.m_interruptEdge : XGpioInterruptEdge_Disabled;
}

/** @brief 保存或清除中断回调。 */
bool XGpio_setInterruptCallback(XGpio* gpio, XGpioInterruptCallback callback, void* userData)
{
    if (!gpio) return false;
    gpio->m_callback = callback;
    gpio->m_userData = callback ? userData : NULL;
    xgpio_success(gpio);
    return true;
}

/** @brief 使能 v2 line event；边沿配置和回调必须已准备好。 */
bool XGpio_enableInterrupt(XGpio* gpio)
{
    if (!gpio) return false;
    if (!gpio->m_open) return xgpio_fail(gpio, XGpioError_NotOpen, 0);
    if (!gpio->m_v2) return xgpio_fail(gpio, XGpioError_Unsupported, ENOTSUP);
    if (!gpio->m_callback || gpio->m_config.m_interruptEdge == XGpioInterruptEdge_Disabled)
        return xgpio_fail(gpio, XGpioError_InvalidArgument, EINVAL);
#if XGPIO_HAVE_V2
    if (!xgpio_v2_set_config(gpio, true)) return false;
    gpio->m_interruptEnabled = 1u;
    xgpio_success(gpio);
    return true;
#else
    return xgpio_fail(gpio, XGpioError_Unsupported, ENOTSUP);
#endif
}

/** @brief 禁止 v2 line event，并使回调不再被 processEvents 分发。 */
void XGpio_disableInterrupt(XGpio* gpio)
{
    if (!gpio) return;
    gpio->m_interruptEnabled = 0u;
#if XGPIO_HAVE_V2
    if (gpio->m_open && gpio->m_v2) {
        if (!xgpio_v2_set_config(gpio, false)) return;
    }
#endif
    xgpio_success(gpio);
}

/** @brief 返回中断分发是否已使能。 */
bool XGpio_isInterruptEnabled(const XGpio* gpio)
{
    return gpio ? gpio->m_interruptEnabled != 0u : false;
}

/** @brief 等待一个 v2 GPIO 边沿并在当前线程执行回调。 */
XGpioProcessResult XGpio_processEvents(XGpio* gpio, int32_t timeoutMs)
{
#if XGPIO_HAVE_V2
    struct pollfd descriptor;
    struct gpio_v2_line_event event;
    int pollResult;
    ssize_t readSize;
    XGpioInterruptEvent callbackEvent;
    if (!gpio) return XGpioProcessResult_Error;
    if (!gpio->m_open || !gpio->m_interruptEnabled || !gpio->m_callback)
        return (xgpio_fail(gpio, XGpioError_NotConfigured, EINVAL), XGpioProcessResult_Error);
    if (!gpio->m_v2) return (xgpio_fail(gpio, XGpioError_Unsupported, ENOTSUP), XGpioProcessResult_Error);
    descriptor.fd = gpio->m_lineFd;
    descriptor.events = POLLIN | POLLPRI;
    descriptor.revents = 0;
    do { pollResult = poll(&descriptor, 1u, timeoutMs < 0 ? -1 : timeoutMs); }
    while (pollResult < 0 && errno == EINTR);
    if (pollResult == 0) { xgpio_set_error(gpio, XGpioError_Timeout, 0); return XGpioProcessResult_Timeout; }
    if (pollResult < 0) {
        xgpio_set_error(gpio, xgpio_error_from_errno(errno), errno);
        return XGpioProcessResult_Error;
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        xgpio_set_error(gpio, XGpioError_Hardware, EIO);
        return XGpioProcessResult_Error;
    }
    do { readSize = read(gpio->m_lineFd, &event, sizeof(event)); }
    while (readSize < 0 && errno == EINTR);
    if (readSize != (ssize_t)sizeof(event)) {
        int errorValue = readSize < 0 ? errno : EIO;
        xgpio_set_error(gpio, xgpio_error_from_errno(errorValue), errorValue);
        return XGpioProcessResult_Error;
    }
    callbackEvent.m_edge = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE
                         ? XGpioInterruptEdge_Rising : XGpioInterruptEdge_Falling;
    callbackEvent.m_level = event.id == GPIO_V2_LINE_EVENT_RISING_EDGE
                          ? XGpioLevel_High : XGpioLevel_Low;
    callbackEvent.m_context = XGpioCallbackContext_Task;
    gpio->m_cachedLevel = callbackEvent.m_level;
    gpio->m_cachedValid = 1u;
    gpio->m_callback(gpio, &callbackEvent, gpio->m_userData);
    xgpio_success(gpio);
    return XGpioProcessResult_Event;
#else
    if (gpio) xgpio_set_error(gpio, XGpioError_Unsupported, ENOTSUP);
    (void)timeoutMs;
    return XGpioProcessResult_Error;
#endif
}

/** @brief 返回当前 Linux 后端声明的能力集合。 */
XGpioFeatures XGpio_features(const XGpio* gpio)
{
    XGpioFeatures features = XGpioFeature_Input | XGpioFeature_Output |
                              XGpioFeature_Readback | XGpioFeature_Toggle |
                              XGpioFeature_ActiveLevel;
#if XGPIO_HAVE_V2
    features |= XGpioFeature_PullUp | XGpioFeature_PullDown |
                XGpioFeature_OpenDrain | XGpioFeature_InterruptRising |
                XGpioFeature_InterruptFalling | XGpioFeature_InterruptBoth |
                XGpioFeature_Debounce | XGpioFeature_ProcessEvents;
#endif
    return gpio ? gpio->m_features : XGpioFeature_None;
}

/** @brief 判断后端是否声明支持指定单一能力位。 */
bool XGpio_hasFeature(const XGpio* gpio, XGpioFeature feature)
{
    return gpio && feature != XGpioFeature_None &&
           (XGpio_features(gpio) & (XGpioFeatures)feature) == (XGpioFeatures)feature;
}

/** @brief 返回当前 line 文件描述符作为原生句柄。 */
XGpioNativeHandle XGpio_handle(const XGpio* gpio)
{
    return gpio && gpio->m_open ? (XGpioNativeHandle)gpio->m_lineFd : XGPIO_INVALID_NATIVE_HANDLE;
}

/** @brief 返回最近一次通用错误。 */
XGpioError XGpio_lastError(const XGpio* gpio)
{
    return gpio ? gpio->m_lastError : XGpioError_InvalidArgument;
}

/** @brief 返回最近一次 Linux errno。 */
int32_t XGpio_nativeError(const XGpio* gpio)
{
    return gpio ? gpio->m_nativeError : 0;
}

/** @brief 清除通用错误及 errno。 */
void XGpio_clearError(XGpio* gpio)
{
    if (gpio) xgpio_success(gpio);
}

/** @brief 返回静态持有的通用错误描述。 */
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

#endif /* defined(__linux__) && !XGPIO_TEST_BACKEND */
