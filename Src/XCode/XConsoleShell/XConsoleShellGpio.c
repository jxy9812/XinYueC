/**
 * @file XConsoleShellGpio.c
 * @brief XConsoleShell GPIO 固定槽位命令实现。
 * @details
 * 模块只调用 XGpio、XAtomic、XThread 和 Shell 公共 API。GPIO 句柄保存在
 * Shell 固定数组中，不使用动态容器；XGpio_create 的具体存储策略由平台后端
 * 决定。中断回调只原子增加计数，不在中断上下文执行输出或内存分配。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_GPIO_ON

#include "XConsoleShellGpio.h"
#include "XThread.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

static bool xcg_parse_u32(const char* text, uint32_t* value)
{
    XUtf8StringView view;
    uint64_t parsed;
    bool ok = false;
    if (!text || !value || text[0] == '\0') return false;
    view = XUtf8StringView_create_cstr(text);
    parsed = XUtf8StringView_toULongLong(&view, &ok, 0);
    if (!ok || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool xcg_parse_pin(const char* controllerText, const char* lineText,
                          XGpioPin* pin)
{
    return pin && xcg_parse_u32(controllerText, &pin->m_controller) &&
           xcg_parse_u32(lineText, &pin->m_line);
}

static const char* xcg_direction_text(XGpioDirection direction)
{
    return direction == XGpioDirection_Output ? "output" : "input";
}

static const char* xcg_pull_text(XGpioPull pull)
{
    if (pull == XGpioPull_Up) return "up";
    if (pull == XGpioPull_Down) return "down";
    return "none";
}

static const char* xcg_drive_text(XGpioOutputType type)
{
    return type == XGpioOutputType_OpenDrain ? "open-drain" : "push-pull";
}

static const char* xcg_active_text(XGpioActiveLevel level)
{
    return level == XGpioActiveLevel_Low ? "low" : "high";
}

static const char* xcg_edge_text(XGpioInterruptEdge edge)
{
    if (edge == XGpioInterruptEdge_Rising) return "rising";
    if (edge == XGpioInterruptEdge_Falling) return "falling";
    if (edge == XGpioInterruptEdge_Both) return "both";
    return "off";
}

static bool xcg_parse_direction(const char* text, XGpioDirection* direction)
{
    if (!text || !direction) return false;
    if (strcmp(text, "input") == 0) {
        *direction = XGpioDirection_Input;
        return true;
    }
    if (strcmp(text, "output") == 0) {
        *direction = XGpioDirection_Output;
        return true;
    }
    return false;
}

static bool xcg_parse_pull(const char* text, XGpioPull* pull)
{
    if (!text || !pull) return false;
    if (strcmp(text, "none") == 0) *pull = XGpioPull_None;
    else if (strcmp(text, "up") == 0) *pull = XGpioPull_Up;
    else if (strcmp(text, "down") == 0) *pull = XGpioPull_Down;
    else return false;
    return true;
}

static bool xcg_parse_drive(const char* text, XGpioOutputType* drive)
{
    if (!text || !drive) return false;
    if (strcmp(text, "push-pull") == 0) *drive = XGpioOutputType_PushPull;
    else if (strcmp(text, "open-drain") == 0) *drive = XGpioOutputType_OpenDrain;
    else return false;
    return true;
}

static bool xcg_parse_active(const char* text, XGpioActiveLevel* active)
{
    if (!text || !active) return false;
    if (strcmp(text, "low") == 0) *active = XGpioActiveLevel_Low;
    else if (strcmp(text, "high") == 0) *active = XGpioActiveLevel_High;
    else return false;
    return true;
}

static bool xcg_parse_initial(const char* text, XGpioInitialLevel* initial)
{
    if (!text || !initial) return false;
    if (strcmp(text, "keep") == 0) *initial = XGpioInitialLevel_Keep;
    else if (strcmp(text, "0") == 0 || strcmp(text, "low") == 0)
        *initial = XGpioInitialLevel_Low;
    else if (strcmp(text, "1") == 0 || strcmp(text, "high") == 0)
        *initial = XGpioInitialLevel_High;
    else return false;
    return true;
}

#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON
static bool xcg_parse_edge(const char* text, XGpioInterruptEdge* edge)
{
    if (!text || !edge) return false;
    if (strcmp(text, "off") == 0) *edge = XGpioInterruptEdge_Disabled;
    else if (strcmp(text, "rising") == 0) *edge = XGpioInterruptEdge_Rising;
    else if (strcmp(text, "falling") == 0) *edge = XGpioInterruptEdge_Falling;
    else if (strcmp(text, "both") == 0) *edge = XGpioInterruptEdge_Both;
    else return false;
    return true;
}
#endif

static bool xcg_apply_options(XGpioConfig* config, int argc,
                              const char* const* argv, int start)
{
    int i = start;
    bool changed = false;
    if (!config || !argv || start < 0 || start > argc) return false;
    while (i < argc) {
        if (i + 1 >= argc) return false;
        if (strcmp(argv[i], "--direction") == 0) {
            if (!xcg_parse_direction(argv[i + 1], &config->m_direction)) return false;
        } else if (strcmp(argv[i], "--pull") == 0) {
            if (!xcg_parse_pull(argv[i + 1], &config->m_pull)) return false;
        } else if (strcmp(argv[i], "--drive") == 0) {
            if (!xcg_parse_drive(argv[i + 1], &config->m_outputType)) return false;
        } else if (strcmp(argv[i], "--active") == 0) {
            if (!xcg_parse_active(argv[i + 1], &config->m_activeLevel)) return false;
        } else if (strcmp(argv[i], "--initial") == 0) {
            if (!xcg_parse_initial(argv[i + 1], &config->m_initialLevel)) return false;
        }
#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON
        else if (strcmp(argv[i], "--edge") == 0) {
            if (!xcg_parse_edge(argv[i + 1], &config->m_interruptEdge)) return false;
        }
#endif
        else if (strcmp(argv[i], "--debounce") == 0) {
            if (!xcg_parse_u32(argv[i + 1], &config->m_debounceUs)) return false;
        } else {
            return false;
        }
        changed = true;
        i += 2;
    }
    if (config->m_direction == XGpioDirection_Input &&
        config->m_initialLevel != XGpioInitialLevel_Keep)
        return false;
    if (config->m_direction == XGpioDirection_Input &&
        config->m_outputType != XGpioOutputType_PushPull)
        return false;
    return changed || start == argc;
}

static XConsoleShellGpioSlot* xcg_find_slot(
    XConsoleShell* shell, const XGpioPin* pin)
{
    size_t i;
    if (!shell || !pin) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_GPIO_SLOT_CAPACITY; ++i) {
        XGpioConfig config = XGPIO_CONFIG_INIT;
        XConsoleShellGpioSlot* slot = &shell->m_gpioSlots[i];
        if (slot->gpio && XGpio_getConfig(slot->gpio, &config) &&
            config.m_pin.m_controller == pin->m_controller &&
            config.m_pin.m_line == pin->m_line)
            return slot;
    }
    return NULL;
}

static XConsoleShellGpioSlot* xcg_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_GPIO_SLOT_CAPACITY; ++i)
        if (!shell->m_gpioSlots[i].gpio) return &shell->m_gpioSlots[i];
    return NULL;
}

static bool xcg_authorized(XConsoleShell* shell,
                           const XConsoleShellSession* session,
                           const XGpioPin* pin,
                           XConsoleShellGpioOperation operation,
                           bool dangerous)
{
    if (!shell || !session || !pin) return false;
    if (shell->m_gpioAuthorize)
        return shell->m_gpioAuthorize(shell->m_gpioAuthorizeUserData,
                                      session, pin, operation);
#if XCONSOLE_SHELL_GPIO_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}

static XConsoleResult xcg_gpio_error(XConsoleShell* shell, const char* operation,
                                     const XGpio* gpio)
{
    char line[160];
    XGpioError error = gpio ? XGpio_lastError(gpio) : XGpioError_Unknown;
    int32_t nativeError = gpio ? XGpio_nativeError(gpio) : 0;
    int written = snprintf(line, sizeof(line), "gpio: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作",
                           XGpio_errorString(error), (int)nativeError);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    if (error == XGpioError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XGpioError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XGpioError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XGpioError_Busy) return XConsoleResult_ResourceLimit;
    if (error == XGpioError_Timeout) return XConsoleResult_Ok;
    return XConsoleResult_Failed;
}

static XConsoleResult xcg_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "gpio: %s: 引脚未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}

static XConsoleResult xcg_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "gpio: 引脚策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}

#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON
static void xcg_interrupt_callback(XGpio* gpio,
                                   const XGpioInterruptEvent* event,
                                   void* userData)
{
    XConsoleShellGpioSlot* slot = (XConsoleShellGpioSlot*)userData;
    (void)gpio;
    (void)event;
    if (slot)
        XAtomic_fetch_add_uint32(&slot->eventCount, 1u,
                                 XAtomic_MemoryOrder_Relaxed);
}
#endif

bool XConsoleShell_setGpioAuthorizeCallback(
    XConsoleShell* self, XConsoleShellGpioAuthorizeFn authorize,
    void* userData)
{
    if (!self) return false;
    self->m_gpioAuthorize = authorize;
    self->m_gpioAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

void XConsoleShellGpio_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0; i < XCONSOLE_SHELL_GPIO_SLOT_CAPACITY; ++i) {
        XConsoleShellGpioSlot* slot = &shell->m_gpioSlots[i];
        if (slot->gpio) {
            XGpio_disableInterrupt(slot->gpio);
            XGpio_setInterruptCallback(slot->gpio, NULL, NULL);
            XGpio_delete(slot->gpio);
            slot->gpio = NULL;
        }
        XAtomic_init(slot->eventCount, 0u);
    }
    shell->m_gpioAuthorize = NULL;
    shell->m_gpioAuthorizeUserData = NULL;
}

#if XCONSOLE_SHELL_GPIO_LIST_ON
static int xcg_list(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!shell || !session) return XConsoleResult_InvalidArgument;
    if (!XConsoleShell_writeUtf8(
            shell, " ctrl  line state direction level active pull drive       irq\n"))
        return XConsoleResult_IoError;
    for (i = 0; i < XCONSOLE_SHELL_GPIO_SLOT_CAPACITY; ++i) {
        XConsoleShellGpioSlot* slot = &shell->m_gpioSlots[i];
        XGpioConfig config = XGPIO_CONFIG_INIT;
        XGpioLevel level = XGpioLevel_Low;
        bool active = false;
        bool hasLevel;
        bool hasActive;
        char line[160];
        int written;
        if (!slot->gpio || !XGpio_getConfig(slot->gpio, &config)) continue;
        if (!xcg_authorized(shell, session, &config.m_pin,
                            XConsoleShellGpioOperation_List, false))
            continue;
        hasLevel = XGpio_read(slot->gpio, &level);
        hasActive = hasLevel && XGpio_readActive(slot->gpio, &active);
        written = snprintf(
            line, sizeof(line), "%5u %5u %-5s %-9s %5s %6s %-4s %-11s %s\n",
            config.m_pin.m_controller, config.m_pin.m_line,
            XGpio_isOpen(slot->gpio) ? "open" : "close",
            xcg_direction_text(config.m_direction),
            hasLevel ? (level == XGpioLevel_High ? "1" : "0") : "-",
            hasActive ? (active ? "yes" : "no") : "-",
            xcg_pull_text(config.m_pull), xcg_drive_text(config.m_outputType),
            XGpio_isInterruptEnabled(slot->gpio) ? "on" : "off");
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !XConsoleShell_writeUtf8(shell, line))
            return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0 && !XConsoleShell_writeUtf8(shell, "gpio: 没有打开的引脚\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_GPIO_OPEN_ON
static int xcg_open(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XGpioConfig config = XGPIO_CONFIG_INIT;
    XConsoleShellGpioSlot* slot;
    (void)userData;
    if (!shell || !session || argc < 3 ||
        !xcg_parse_pin(argv[0], argv[1], &config.m_pin) ||
        !xcg_parse_direction(argv[2], &config.m_direction) ||
        !xcg_apply_options(&config, argc, argv, 3))
        return XConsoleResult_InvalidArgument;
    if (!xcg_authorized(shell, session, &config.m_pin,
                        XConsoleShellGpioOperation_Open, true))
        return xcg_denied(shell);
    if (xcg_find_slot(shell, &config.m_pin)) {
        if (!XConsoleShell_writeUtf8(shell, "gpio: 打开: 引脚已打开\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    slot = xcg_empty_slot(shell);
    if (!slot) {
        if (!XConsoleShell_writeUtf8(shell, "gpio: 打开: 槽位已满\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    slot->gpio = XGpio_create(&config);
    if (!slot->gpio) {
        if (!XConsoleShell_writeUtf8(shell, "gpio: 打开: 创建失败\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_Failed;
    }
    XAtomic_init(slot->eventCount, 0u);
    if (!XGpio_open(slot->gpio)) {
        XConsoleResult result = xcg_gpio_error(shell, "打开", slot->gpio);
        XGpio_delete(slot->gpio);
        slot->gpio = NULL;
        return result;
    }
    return XConsoleShell_writeUtf8(shell, "gpio: 打开: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_CLOSE_ON
static int xcg_close(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "关闭");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Close, true))
        return xcg_denied(shell);
    XGpio_disableInterrupt(slot->gpio);
    XGpio_setInterruptCallback(slot->gpio, NULL, NULL);
    XGpio_delete(slot->gpio);
    slot->gpio = NULL;
    XAtomic_store_uint32(&slot->eventCount, 0u, XAtomic_MemoryOrder_Relaxed);
    return XConsoleShell_writeUtf8(shell, "gpio: 关闭: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_INFO_ON
static int xcg_info(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XGpioConfig config = XGPIO_CONFIG_INIT;
    XConsoleShellGpioSlot* slot;
    char line[320];
    int written;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "查询配置");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Info, false))
        return xcg_denied(shell);
    if (!XGpio_getConfig(slot->gpio, &config))
        return xcg_gpio_error(shell, "查询配置", slot->gpio);
    written = snprintf(
        line, sizeof(line),
        "controller=%u line=%u open=%s direction=%s\n"
        "pull=%s drive=%s active=%s initial=%d debounce-us=%u\n"
        "irq=%s edge=%s events=%u features=0x%08x\n"
        "error=%s native=%d\n",
        config.m_pin.m_controller, config.m_pin.m_line,
        XGpio_isOpen(slot->gpio) ? "yes" : "no",
        xcg_direction_text(config.m_direction),
        xcg_pull_text(config.m_pull), xcg_drive_text(config.m_outputType),
        xcg_active_text(config.m_activeLevel), (int)config.m_initialLevel,
        config.m_debounceUs,
        XGpio_isInterruptEnabled(slot->gpio) ? "enabled" : "disabled",
        xcg_edge_text(XGpio_interruptEdge(slot->gpio)),
        XAtomic_load_uint32(&slot->eventCount, XAtomic_MemoryOrder_Relaxed),
        (unsigned)XGpio_features(slot->gpio),
        XGpio_errorString(XGpio_lastError(slot->gpio)),
        (int)XGpio_nativeError(slot->gpio));
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_GPIO_READ_ON
static int xcg_read(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    bool logical = argc == 3 && strcmp(argv[2], "--active") == 0;
    char line[96];
    int written;
    (void)userData;
    if (!shell || !session || (argc != 2 && !logical) ||
        !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "读取");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Read, false))
        return xcg_denied(shell);
    if (logical) {
        bool active;
        if (!XGpio_readActive(slot->gpio, &active))
            return xcg_gpio_error(shell, "读取", slot->gpio);
        written = snprintf(line, sizeof(line), "gpio %u:%u active=%s\n",
                           pin.m_controller, pin.m_line, active ? "yes" : "no");
    } else {
        XGpioLevel level;
        if (!XGpio_read(slot->gpio, &level))
            return xcg_gpio_error(shell, "读取", slot->gpio);
        written = snprintf(line, sizeof(line), "gpio %u:%u level=%u\n",
                           pin.m_controller, pin.m_line,
                           level == XGpioLevel_High ? 1u : 0u);
    }
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_WRITE_ON
static int xcg_write(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    bool logical = argc == 4 && strcmp(argv[3], "--active") == 0;
    bool value;
    bool ok;
    (void)userData;
    if (!shell || !session || (argc != 3 && !logical) ||
        !xcg_parse_pin(argv[0], argv[1], &pin) ||
        (strcmp(argv[2], "0") != 0 && strcmp(argv[2], "1") != 0))
        return XConsoleResult_InvalidArgument;
    value = strcmp(argv[2], "1") == 0;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "写入");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Write, true))
        return xcg_denied(shell);
    ok = logical ? XGpio_writeActive(slot->gpio, value)
                 : XGpio_write(slot->gpio,
                               value ? XGpioLevel_High : XGpioLevel_Low);
    if (!ok) return xcg_gpio_error(shell, "写入", slot->gpio);
    return XConsoleShell_writeUtf8(shell, "gpio: 写入: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_TOGGLE_ON
static int xcg_toggle(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "翻转");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Toggle, true))
        return xcg_denied(shell);
    if (!XGpio_toggle(slot->gpio))
        return xcg_gpio_error(shell, "翻转", slot->gpio);
    return XConsoleShell_writeUtf8(shell, "gpio: 翻转: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_CONFIGURE_ON
static int xcg_configure(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XGpioConfig config = XGPIO_CONFIG_INIT;
    XConsoleShellGpioSlot* slot;
    (void)userData;
    if (!shell || !session || argc < 4 ||
        !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "配置");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Configure, true))
        return xcg_denied(shell);
    if (!XGpio_getConfig(slot->gpio, &config))
        return xcg_gpio_error(shell, "配置", slot->gpio);
    if (!xcg_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    if (!XGpio_configure(slot->gpio, &config))
        return xcg_gpio_error(shell, "配置", slot->gpio);
    return XConsoleShell_writeUtf8(shell, "gpio: 配置: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON
static int xcg_irq_set(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XGpioInterruptEdge edge;
    XConsoleShellGpioSlot* slot;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin) ||
        !xcg_parse_edge(argv[2], &edge))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "设置中断");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Interrupt, true))
        return xcg_denied(shell);
    if (XGpio_isInterruptEnabled(slot->gpio)) {
        if (!XConsoleShell_writeUtf8(shell, "gpio: 中断设置: 请先禁用中断\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_Failed;
    }
    if (!XGpio_setInterruptEdge(slot->gpio, edge))
        return xcg_gpio_error(shell, "设置中断", slot->gpio);
    return XConsoleShell_writeUtf8(shell, "gpio: 中断设置: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcg_irq_enable(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "使能中断");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Interrupt, true))
        return xcg_denied(shell);
    if (!XGpio_setInterruptCallback(slot->gpio, xcg_interrupt_callback, slot) ||
        !XGpio_enableInterrupt(slot->gpio)) {
        XGpio_setInterruptCallback(slot->gpio, NULL, NULL);
        return xcg_gpio_error(shell, "使能中断", slot->gpio);
    }
    XAtomic_store_uint32(&slot->eventCount, 0u, XAtomic_MemoryOrder_Relaxed);
    return XConsoleShell_writeUtf8(shell, "gpio: 中断使能: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcg_irq_disable(XConsoleShell* shell, XConsoleShellSession* session,
                           int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "禁用中断");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Interrupt, true))
        return xcg_denied(shell);
    XGpio_disableInterrupt(slot->gpio);
    XGpio_setInterruptCallback(slot->gpio, NULL, NULL);
    return XConsoleShell_writeUtf8(shell, "gpio: 中断禁用: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcg_irq_status(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    char line[112];
    int written;
    (void)argc;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "查询中断状态");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Interrupt, false))
        return xcg_denied(shell);
    written = snprintf(
        line, sizeof(line), "gpio %u:%u irq=%s edge=%s events=%u\n",
        pin.m_controller, pin.m_line,
        XGpio_isInterruptEnabled(slot->gpio) ? "enabled" : "disabled",
        xcg_edge_text(XGpio_interruptEdge(slot->gpio)),
        XAtomic_load_uint32(&slot->eventCount, XAtomic_MemoryOrder_Relaxed));
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcg_irq_wait(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    XGpioPin pin;
    XConsoleShellGpioSlot* slot;
    uint32_t wanted = 1u;
    uint32_t timeout = 1000u;
    uint32_t remaining;
    uint32_t startCount;
    uint32_t currentCount;
    bool timedOut = false;
    int i = 2;
    char line[96];
    int written;
    (void)userData;
    if (!shell || !session || !xcg_parse_pin(argv[0], argv[1], &pin))
        return XConsoleResult_InvalidArgument;
    while (i < argc) {
        if (i + 1 >= argc) return XConsoleResult_InvalidArgument;
        if (strcmp(argv[i], "--count") == 0) {
            if (!xcg_parse_u32(argv[i + 1], &wanted) || wanted == 0 ||
                wanted > XCONSOLE_SHELL_GPIO_MAX_WAIT_COUNT)
                return XConsoleResult_InvalidArgument;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (!xcg_parse_u32(argv[i + 1], &timeout) ||
                timeout > XCONSOLE_SHELL_GPIO_MAX_WAIT_MS)
                return XConsoleResult_InvalidArgument;
        } else {
            return XConsoleResult_InvalidArgument;
        }
        i += 2;
    }
    slot = xcg_find_slot(shell, &pin);
    if (!slot) return xcg_missing(shell, "等待中断");
    if (!xcg_authorized(shell, session, &pin,
                        XConsoleShellGpioOperation_Interrupt, false))
        return xcg_denied(shell);
    if (!XGpio_isInterruptEnabled(slot->gpio)) {
        if (!XConsoleShell_writeUtf8(shell, "gpio: 中断等待: 中断已禁用\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_Failed;
    }
    startCount =
        XAtomic_load_uint32(&slot->eventCount, XAtomic_MemoryOrder_Relaxed);
    remaining = timeout;
    do {
        uint32_t slice = remaining;
        XGpioProcessResult processResult;
        if (XConsoleShell_isCancelled(shell)) return XConsoleResult_Cancelled;
        if (slice > XCONSOLE_SHELL_GPIO_EVENT_SLICE_MS)
            slice = XCONSOLE_SHELL_GPIO_EVENT_SLICE_MS;
        processResult = XGpio_processEvents(slot->gpio, (int32_t)slice);
        if (processResult == XGpioProcessResult_Error) {
            if (XGpio_lastError(slot->gpio) == XGpioError_Unsupported) {
                XGpio_clearError(slot->gpio);
                if (slice) XThread_msleep(slice);
            } else {
                return xcg_gpio_error(shell, "等待中断", slot->gpio);
            }
        }
        currentCount =
            XAtomic_load_uint32(&slot->eventCount, XAtomic_MemoryOrder_Relaxed);
        if (currentCount - startCount >= wanted) break;
        if (remaining <= slice) {
            timedOut = true;
            break;
        }
        remaining -= slice;
    } while (remaining > 0u);
    currentCount =
        XAtomic_load_uint32(&slot->eventCount, XAtomic_MemoryOrder_Relaxed);
    written = snprintf(line, sizeof(line), "gpio %u:%u events=%u timeout=%s\n",
                       pin.m_controller, pin.m_line,
                       currentCount - startCount, timedOut ? "yes" : "no");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static const XConsoleCommand g_gpioIrqCommands[] = {
    { "set", NULL, "设置 GPIO 中断边沿", "gpio irq set <controller> <line> <off|rising|falling|both>",
      3, 3, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_irq_set, NULL, 0, NULL },
    { "enable", NULL, "启用 GPIO 中断", "gpio irq enable <controller> <line>",
      2, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_irq_enable, NULL, 0, NULL },
    { "disable", NULL, "禁用 GPIO 中断", "gpio irq disable <controller> <line>",
      2, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_irq_disable, NULL, 0, NULL },
    { "status", NULL, "显示 GPIO 中断状态", "gpio irq status <controller> <line>",
      2, 2, XConsoleCommandFlag_None, xcg_irq_status, NULL, 0, NULL },
    { "wait", NULL, "等待 GPIO 中断事件", "gpio irq wait <controller> <line> [--count N] [--timeout MS]",
      2, -1, XConsoleCommandFlag_None, xcg_irq_wait, NULL, 0, NULL }
};
#endif

#define XCS_GPIO_HAS_COMMANDS \
    (XCONSOLE_SHELL_GPIO_LIST_ON || XCONSOLE_SHELL_GPIO_OPEN_ON || \
     XCONSOLE_SHELL_GPIO_CLOSE_ON || XCONSOLE_SHELL_GPIO_INFO_ON || \
     XCONSOLE_SHELL_GPIO_READ_ON || XCONSOLE_SHELL_GPIO_WRITE_ON || \
     XCONSOLE_SHELL_GPIO_TOGGLE_ON || XCONSOLE_SHELL_GPIO_CONFIGURE_ON || \
     XCONSOLE_SHELL_GPIO_INTERRUPT_ON)

#if XCS_GPIO_HAS_COMMANDS
static const XConsoleCommand g_gpioCommands[] = {
#if XCONSOLE_SHELL_GPIO_LIST_ON
    { "list", NULL, "列出已打开 GPIO", "gpio list", 0, 0,
      XConsoleCommandFlag_None, xcg_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_OPEN_ON
    { "open", NULL, "创建并打开 GPIO", "gpio open <controller> <line> <input|output> [options]",
      3, -1, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_CLOSE_ON
    { "close", NULL, "关闭并释放 GPIO", "gpio close <controller> <line>",
      2, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_INFO_ON
    { "info", NULL, "显示 GPIO 配置和能力", "gpio info <controller> <line>",
      2, 2, XConsoleCommandFlag_None, xcg_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_READ_ON
    { "read", NULL, "读取 GPIO 状态", "gpio read <controller> <line> [--active]",
      2, 3, XConsoleCommandFlag_None, xcg_read, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_WRITE_ON
    { "write", NULL, "写入 GPIO 状态", "gpio write <controller> <line> <0|1> [--active]",
      3, 4, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_write, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_TOGGLE_ON
    { "toggle", NULL, "翻转 GPIO 输出", "gpio toggle <controller> <line>",
      2, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_toggle, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_CONFIGURE_ON
    { "configure", NULL, "重新配置 GPIO", "gpio configure <controller> <line> <options>",
      4, -1, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
      xcg_configure, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_GPIO_INTERRUPT_ON
    { "irq", NULL, "GPIO 中断命令", "gpio irq <set|enable|disable|status|wait>",
      0, -1, XConsoleCommandFlag_None, NULL, g_gpioIrqCommands,
      sizeof(g_gpioIrqCommands) / sizeof(g_gpioIrqCommands[0]), NULL },
#endif
};
#else
static const XConsoleCommand g_gpioCommands[1] = { { NULL } };
#endif

const XConsoleCommand XConsoleShellGpio_command = {
    "gpio", NULL, "GPIO 配置、读写和中断诊断", "gpio <subcommand>",
    0, -1, XConsoleCommandFlag_None, NULL, g_gpioCommands,
    XCS_GPIO_HAS_COMMANDS ? sizeof(g_gpioCommands) / sizeof(g_gpioCommands[0]) : 0u,
    NULL
};

#endif /* Shell、命令、I/O 和 GPIO 均启用 */
