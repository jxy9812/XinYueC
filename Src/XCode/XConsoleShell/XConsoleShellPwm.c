/**
 * @file XConsoleShellPwm.c
 * @brief XConsoleShell PWM 固定槽位命令实现。
 * @details
 * 所有输出控制均通过 XPwm 公共 API 完成，Shell 不调用定时器寄存器、
 * sysfs、Win32 或其他平台 API。固定槽位和产品授权回调使资源边界和危险
 * 输出操作在编译期及运行时都可控。
 */
#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_PWM_ON

#include "XConsoleShellPwm.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

static bool xcsp_parse_u32(const char* text, uint32_t* value)
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

static bool xcsp_parse_channel(const char* controllerText, const char* channelText,
                               XPwmChannel* channel)
{
    return channel && xcsp_parse_u32(controllerText, &channel->m_controller) &&
           xcsp_parse_u32(channelText, &channel->m_channel);
}

static XConsoleShellPwmSlot* xcsp_find_slot(
    XConsoleShell* shell, const XPwmChannel* channel)
{
    size_t i;
    if (!shell || !channel) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_PWM_SLOT_CAPACITY; ++i) {
        XConsoleShellPwmSlot* slot = &shell->m_pwmSlots[i];
        XPwmConfig config = XPWM_CONFIG_INIT;
        if (slot->pwm && XPwm_getConfig(slot->pwm, &config) &&
            config.m_channel.m_controller == channel->m_controller &&
            config.m_channel.m_channel == channel->m_channel)
            return slot;
    }
    return NULL;
}

static XConsoleShellPwmSlot* xcsp_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_PWM_SLOT_CAPACITY; ++i)
        if (!shell->m_pwmSlots[i].pwm) return &shell->m_pwmSlots[i];
    return NULL;
}

static bool xcsp_authorized(XConsoleShell* shell,
                            const XConsoleShellSession* session,
                            const XPwmChannel* channel,
                            XConsoleShellPwmOperation operation,
                            bool dangerous)
{
    if (!shell || !session || !channel) return false;
    if (shell->m_pwmAuthorize)
        return shell->m_pwmAuthorize(shell->m_pwmAuthorizeUserData, session,
                                     channel, operation);
#if XCONSOLE_SHELL_PWM_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}

static XConsoleResult xcsp_error(XConsoleShell* shell, const char* operation,
                                 const XPwm* pwm)
{
    char line[160];
    XPwmError error = pwm ? XPwm_lastError(pwm) : XPwmError_Unknown;
    int32_t nativeError = pwm ? XPwm_nativeError(pwm) : 0;
    int written = snprintf(line, sizeof(line), "pwm: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作",
                           XPwm_errorString(error), (int)nativeError);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    if (error == XPwmError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XPwmError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XPwmError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XPwmError_Busy) return XConsoleResult_ResourceLimit;
    return XConsoleResult_Failed;
}

static XConsoleResult xcsp_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "pwm: %s: 通道未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}

static XConsoleResult xcsp_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "pwm: 通道策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}

static bool xcsp_parse_polarity(const char* text, XPwmPolarity* polarity)
{
    if (!text || !polarity) return false;
    if (strcmp(text, "normal") == 0) *polarity = XPwmPolarity_Normal;
    else if (strcmp(text, "inverted") == 0) *polarity = XPwmPolarity_Inverted;
    else return false;
    return true;
}

static bool xcsp_apply_options(XPwmConfig* config, int argc,
                               const char* const* argv, int start)
{
    int i = start;
    bool changed = false;
    if (!config || !argv || start < 0 || start > argc) return false;
    while (i < argc) {
        uint32_t value;
        if (i + 1 >= argc) return false;
        if (strcmp(argv[i], "--frequency") == 0) {
            if (!xcsp_parse_u32(argv[i + 1], &value) || value == 0u) return false;
            config->m_frequencyHz = value;
        } else if (strcmp(argv[i], "--duty") == 0) {
            if (!xcsp_parse_u32(argv[i + 1], &value) || value > 10000u) return false;
            config->m_dutyPermille = (uint16_t)value;
        } else if (strcmp(argv[i], "--polarity") == 0) {
            if (!xcsp_parse_polarity(argv[i + 1], &config->m_polarity)) return false;
        } else if (strcmp(argv[i], "--enabled") == 0) {
            if (strcmp(argv[i + 1], "yes") == 0 || strcmp(argv[i + 1], "1") == 0)
                config->m_initialEnabled = true;
            else if (strcmp(argv[i + 1], "no") == 0 || strcmp(argv[i + 1], "0") == 0)
                config->m_initialEnabled = false;
            else return false;
        } else {
            return false;
        }
        changed = true;
        i += 2;
    }
    return changed || start == argc;
}

#if XCONSOLE_SHELL_PWM_LIST_ON
static int xcsp_list(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!shell || !session) return XConsoleResult_InvalidArgument;
    if (!XConsoleShell_writeUtf8(shell,
            " ctrl  chan state  running frequency duty(permille) polarity\n"))
        return XConsoleResult_IoError;
    for (i = 0; i < XCONSOLE_SHELL_PWM_SLOT_CAPACITY; ++i) {
        XConsoleShellPwmSlot* slot = &shell->m_pwmSlots[i];
        XPwmConfig config = XPWM_CONFIG_INIT;
        char line[180];
        int written;
        if (!slot->pwm || !XPwm_getConfig(slot->pwm, &config) ||
            !xcsp_authorized(shell, session, &config.m_channel,
                             XConsoleShellPwmOperation_List, false))
            continue;
        written = snprintf(line, sizeof(line), "%5u %5u %-6s %-7s %9u %13u %-9s\n",
                           config.m_channel.m_controller,
                           config.m_channel.m_channel,
                           XPwm_isOpen(slot->pwm) ? "open" : "close",
                           XPwm_isRunning(slot->pwm) ? "yes" : "no",
                           (unsigned)config.m_frequencyHz,
                           (unsigned)config.m_dutyPermille,
                           config.m_polarity == XPwmPolarity_Inverted ? "inverted" : "normal");
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !XConsoleShell_writeUtf8(shell, line))
            return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0 && !XConsoleShell_writeUtf8(shell, "pwm: 没有打开的通道\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_PWM_OPEN_ON
static int xcsp_open(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XPwmConfig config = XPWM_CONFIG_INIT;
    XConsoleShellPwmSlot* slot;
    XPwm* pwm;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcsp_parse_channel(argv[0], argv[1], &config.m_channel) ||
        !xcsp_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    if (!xcsp_authorized(shell, session, &config.m_channel,
                         XConsoleShellPwmOperation_Open, true))
        return xcsp_denied(shell);
    if (xcsp_find_slot(shell, &config.m_channel)) {
        if (!XConsoleShell_writeUtf8(shell, "pwm: 通道已打开\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    slot = xcsp_empty_slot(shell);
    if (!slot) {
        if (!XConsoleShell_writeUtf8(shell, "pwm: 槽位已满\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    pwm = XPwm_create(&config);
    if (!pwm) return XConsoleResult_ResourceLimit;
    if (!XPwm_open(pwm)) {
        XConsoleResult result = xcsp_error(shell, "打开", pwm);
        XPwm_delete(pwm);
        return result;
    }
    slot->pwm = pwm;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_PWM_CLOSE_ON
static int xcsp_close(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcsp_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcsp_find_slot(shell, &channel);
    if (!slot) return xcsp_missing(shell, "关闭");
    if (!xcsp_authorized(shell, session, &channel,
                         XConsoleShellPwmOperation_Close, true))
        return xcsp_denied(shell);
    XPwm_delete(slot->pwm);
    slot->pwm = NULL;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_PWM_INFO_ON
static int xcsp_info(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    XPwmConfig config = XPWM_CONFIG_INIT;
    char line[256];
    int written;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcsp_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcsp_find_slot(shell, &channel);
    if (!slot) return xcsp_missing(shell, "查询配置");
    if (!xcsp_authorized(shell, session, &channel,
                         XConsoleShellPwmOperation_Info, false))
        return xcsp_denied(shell);
    if (!XPwm_getConfig(slot->pwm, &config)) return xcsp_error(shell, "查询配置", slot->pwm);
    written = snprintf(line, sizeof(line),
                       "pwm %u/%u state=%s running=%s frequency=%u duty-permille=%u polarity=%s features=0x%08x\n",
                       config.m_channel.m_controller, config.m_channel.m_channel,
                       XPwm_isOpen(slot->pwm) ? "open" : "close",
                       XPwm_isRunning(slot->pwm) ? "yes" : "no",
                       (unsigned)config.m_frequencyHz,
                       (unsigned)config.m_dutyPermille,
                       config.m_polarity == XPwmPolarity_Inverted ? "inverted" : "normal",
                       (unsigned)XPwm_features(slot->pwm));
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

static int xcsp_parse_target(XConsoleShell* shell, XConsoleShellSession* session,
                             int argc, const char* const* argv,
                             XConsoleShellPwmSlot** slot,
                             XPwmChannel* channel,
                             XConsoleShellPwmOperation operation)
{
    if (!shell || !session || argc < 2 || !slot || !channel ||
        !xcsp_parse_channel(argv[0], argv[1], channel))
        return XConsoleResult_InvalidArgument;
    *slot = xcsp_find_slot(shell, channel);
    if (!*slot) return xcsp_missing(shell, "操作");
    if (!xcsp_authorized(shell, session, channel, operation, true))
        return xcsp_denied(shell);
    return XConsoleResult_Ok;
}

#if XCONSOLE_SHELL_PWM_CONFIGURE_ON
static int xcsp_configure(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XPwmConfig config = XPWM_CONFIG_INIT;
    XConsoleShellPwmSlot* slot;
    int result;
    (void)userData;
    result = xcsp_parse_target(shell, session, argc, argv, &slot, &channel,
                               XConsoleShellPwmOperation_Configure);
    if (result != XConsoleResult_Ok) return result;
    if (!XPwm_getConfig(slot->pwm, &config) ||
        !xcsp_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    return XPwm_configure(slot->pwm, &config)
               ? XConsoleResult_Ok : xcsp_error(shell, "配置", slot->pwm);
}
#endif

#if XCONSOLE_SHELL_PWM_START_ON
static int xcsp_start(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    int result;
    (void)userData;
    if (argc != 2) return XConsoleResult_InvalidArgument;
    result = xcsp_parse_target(shell, session, argc, argv, &slot, &channel,
                               XConsoleShellPwmOperation_Start);
    if (result != XConsoleResult_Ok) return result;
    return XPwm_start(slot->pwm) ? XConsoleResult_Ok : xcsp_error(shell, "启动", slot->pwm);
}
#endif

#if XCONSOLE_SHELL_PWM_STOP_ON
static int xcsp_stop(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    int result;
    (void)userData;
    if (argc != 2) return XConsoleResult_InvalidArgument;
    result = xcsp_parse_target(shell, session, argc, argv, &slot, &channel,
                               XConsoleShellPwmOperation_Stop);
    if (result != XConsoleResult_Ok) return result;
    return XPwm_stop(slot->pwm) ? XConsoleResult_Ok : xcsp_error(shell, "停止", slot->pwm);
}
#endif

#if XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
static int xcsp_set_frequency(XConsoleShell* shell, XConsoleShellSession* session,
                              int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    uint32_t value;
    int result;
    (void)userData;
    if (argc != 3 || !xcsp_parse_u32(argv[2], &value) || value == 0u)
        return XConsoleResult_InvalidArgument;
    result = xcsp_parse_target(shell, session, argc, argv, &slot, &channel,
                               XConsoleShellPwmOperation_SetFrequency);
    if (result != XConsoleResult_Ok) return result;
    return XPwm_setFrequency(slot->pwm, value)
               ? XConsoleResult_Ok : xcsp_error(shell, "设置频率", slot->pwm);
}
#endif

#if XCONSOLE_SHELL_PWM_SET_DUTY_ON
static int xcsp_set_duty(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XPwmChannel channel;
    XConsoleShellPwmSlot* slot;
    uint32_t value;
    int result;
    (void)userData;
    if (argc != 3 || !xcsp_parse_u32(argv[2], &value) || value > 10000u)
        return XConsoleResult_InvalidArgument;
    result = xcsp_parse_target(shell, session, argc, argv, &slot, &channel,
                               XConsoleShellPwmOperation_SetDuty);
    if (result != XConsoleResult_Ok) return result;
    return XPwm_setDuty(slot->pwm, (uint16_t)value)
               ? XConsoleResult_Ok : xcsp_error(shell, "设置占空比", slot->pwm);
}
#endif

void XConsoleShellPwm_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0; i < XCONSOLE_SHELL_PWM_SLOT_CAPACITY; ++i) {
        if (shell->m_pwmSlots[i].pwm) {
            XPwm_delete(shell->m_pwmSlots[i].pwm);
            shell->m_pwmSlots[i].pwm = NULL;
        }
    }
    shell->m_pwmAuthorize = NULL;
    shell->m_pwmAuthorizeUserData = NULL;
}

bool XConsoleShell_setPwmAuthorizeCallback(
    XConsoleShell* self, XConsoleShellPwmAuthorizeFn authorize,
    void* userData)
{
    if (!self) return false;
    self->m_pwmAuthorize = authorize;
    self->m_pwmAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

#define XCSP_HAS_COMMANDS (XCONSOLE_SHELL_PWM_LIST_ON || XCONSOLE_SHELL_PWM_OPEN_ON || \
                           XCONSOLE_SHELL_PWM_CLOSE_ON || XCONSOLE_SHELL_PWM_INFO_ON || \
                           XCONSOLE_SHELL_PWM_CONFIGURE_ON || XCONSOLE_SHELL_PWM_START_ON || \
                           XCONSOLE_SHELL_PWM_STOP_ON || XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON || \
                           XCONSOLE_SHELL_PWM_SET_DUTY_ON)

#if XCSP_HAS_COMMANDS
static const XConsoleCommand g_pwmCommands[] = {
#if XCONSOLE_SHELL_PWM_LIST_ON
    { "list", NULL, "列出已打开 PWM", "pwm list", 0, 0, 0, xcsp_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_OPEN_ON
    { "open", NULL, "打开 PWM 通道", "pwm open <controller> <channel> [options]", 2, -1,
      XConsoleCommandFlag_Dangerous, xcsp_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_CLOSE_ON
    { "close", NULL, "关闭 PWM 通道", "pwm close <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcsp_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_INFO_ON
    { "info", NULL, "查看 PWM 配置", "pwm info <controller> <channel>", 2, 2, 0, xcsp_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_CONFIGURE_ON
    { "configure", NULL, "重新配置 PWM", "pwm configure <controller> <channel> [options]", 2, -1,
      XConsoleCommandFlag_Dangerous, xcsp_configure, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_START_ON
    { "start", NULL, "启动 PWM 输出", "pwm start <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcsp_start, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_STOP_ON
    { "stop", NULL, "停止 PWM 输出", "pwm stop <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcsp_stop, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_SET_FREQUENCY_ON
    { "set-frequency", NULL, "设置 PWM 频率", "pwm set-frequency <controller> <channel> <hz>", 3, 3,
      XConsoleCommandFlag_Dangerous, xcsp_set_frequency, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_PWM_SET_DUTY_ON
    { "set-duty", NULL, "设置 PWM 占空比", "pwm set-duty <controller> <channel> <permille>", 3, 3,
      XConsoleCommandFlag_Dangerous, xcsp_set_duty, NULL, 0, NULL },
#endif
};
#else
static const XConsoleCommand g_pwmCommands[1] = {{ NULL }};
#endif

const XConsoleCommand XConsoleShellPwm_command = {
    "pwm", NULL, "PWM 输出命令", "pwm <subcommand>", 0, -1, 0, NULL,
    g_pwmCommands, XCSP_HAS_COMMANDS ? sizeof(g_pwmCommands) / sizeof(g_pwmCommands[0]) : 0u,
    NULL
};

#endif /* Shell、命令、I/O 和 PWM 均启用 */
