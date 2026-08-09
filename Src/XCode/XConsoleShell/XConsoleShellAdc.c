/**
 * @file XConsoleShellAdc.c
 * @brief XConsoleShell ADC 固定槽位命令实现。
 * @details
 * 本模块不包含平台头文件，不创建动态命令或动态容器。所有硬件访问均经由
 * XAdc 公共接口完成；命令参数和输出使用固定大小的栈缓冲，适合嵌入式。
 */
#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ADC_ON

#include "XConsoleShellAdc.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

static bool xcsa_parse_u32(const char* text, uint32_t* value)
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

static bool xcsa_parse_i32(const char* text, int32_t* value)
{
    uint32_t parsed;
    if (!xcsa_parse_u32(text, &parsed) || parsed > INT32_MAX) return false;
    *value = (int32_t)parsed;
    return true;
}

static bool xcsa_parse_channel(const char* controllerText, const char* channelText,
                               XAdcChannel* channel)
{
    return channel && xcsa_parse_u32(controllerText, &channel->m_controller) &&
           xcsa_parse_u32(channelText, &channel->m_channel);
}

static XConsoleShellAdcSlot* xcsa_find_slot(
    XConsoleShell* shell, const XAdcChannel* channel)
{
    size_t i;
    if (!shell || !channel) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_ADC_SLOT_CAPACITY; ++i) {
        XConsoleShellAdcSlot* slot = &shell->m_adcSlots[i];
        XAdcConfig config = XADC_CONFIG_INIT;
        if (slot->adc && XAdc_getConfig(slot->adc, &config) &&
            config.m_channel.m_controller == channel->m_controller &&
            config.m_channel.m_channel == channel->m_channel)
            return slot;
    }
    return NULL;
}

static XConsoleShellAdcSlot* xcsa_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_ADC_SLOT_CAPACITY; ++i)
        if (!shell->m_adcSlots[i].adc) return &shell->m_adcSlots[i];
    return NULL;
}

static bool xcsa_authorized(XConsoleShell* shell,
                            const XConsoleShellSession* session,
                            const XAdcChannel* channel,
                            XConsoleShellAdcOperation operation,
                            bool dangerous)
{
    if (!shell || !session || !channel) return false;
    if (shell->m_adcAuthorize)
        return shell->m_adcAuthorize(shell->m_adcAuthorizeUserData, session,
                                     channel, operation);
#if XCONSOLE_SHELL_ADC_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}

static XConsoleResult xcsa_error(XConsoleShell* shell, const char* operation,
                                 const XAdc* adc)
{
    char line[160];
    XAdcError error = adc ? XAdc_lastError(adc) : XAdcError_Unknown;
    int32_t nativeError = adc ? XAdc_nativeError(adc) : 0;
    int written = snprintf(line, sizeof(line), "adc: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作",
                           XAdc_errorString(error), (int)nativeError);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    if (error == XAdcError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XAdcError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XAdcError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XAdcError_Busy) return XConsoleResult_ResourceLimit;
    if (error == XAdcError_Timeout) return XConsoleResult_Ok;
    return XConsoleResult_Failed;
}

static XConsoleResult xcsa_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "adc: %s: 通道未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}

static XConsoleResult xcsa_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "adc: 通道策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}

static bool xcsa_apply_options(XAdcConfig* config, int argc,
                               const char* const* argv, int start)
{
    int i = start;
    bool changed = false;
    if (!config || !argv || start < 0 || start > argc) return false;
    while (i < argc) {
        uint32_t value;
        if (i + 1 >= argc || !xcsa_parse_u32(argv[i + 1], &value)) return false;
        if (strcmp(argv[i], "--resolution") == 0) {
            if (value > UINT8_MAX) return false;
            config->m_resolutionBits = (uint8_t)value;
        } else if (strcmp(argv[i], "--reference") == 0) {
            config->m_referenceMv = value;
        } else if (strcmp(argv[i], "--sample-us") == 0) {
            config->m_sampleTimeUs = value;
        } else if (strcmp(argv[i], "--oversample") == 0) {
            if (value > UINT16_MAX) return false;
            config->m_oversample = (uint16_t)value;
        } else {
            return false;
        }
        changed = true;
        i += 2;
    }
    return changed || start == argc;
}

#if XCONSOLE_SHELL_ADC_LIST_ON
static int xcsa_list(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!shell || !session) return XConsoleResult_InvalidArgument;
    if (!XConsoleShell_writeUtf8(shell,
            " ctrl  chan state  resolution reference sample-us oversample\n"))
        return XConsoleResult_IoError;
    for (i = 0; i < XCONSOLE_SHELL_ADC_SLOT_CAPACITY; ++i) {
        XConsoleShellAdcSlot* slot = &shell->m_adcSlots[i];
        XAdcConfig config = XADC_CONFIG_INIT;
        char line[160];
        int written;
        if (!slot->adc || !XAdc_getConfig(slot->adc, &config) ||
            !xcsa_authorized(shell, session, &config.m_channel,
                             XConsoleShellAdcOperation_List, false))
            continue;
        written = snprintf(line, sizeof(line),
                           "%5u %5u %-6s %10u %9u %9u %10u\n",
                           config.m_channel.m_controller,
                           config.m_channel.m_channel,
                           XAdc_isOpen(slot->adc) ? "open" : "close",
                           (unsigned)config.m_resolutionBits,
                           (unsigned)config.m_referenceMv,
                           (unsigned)config.m_sampleTimeUs,
                           (unsigned)config.m_oversample);
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !XConsoleShell_writeUtf8(shell, line))
            return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0 && !XConsoleShell_writeUtf8(shell, "adc: 没有打开的通道\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_ADC_OPEN_ON
static int xcsa_open(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XAdcConfig config = XADC_CONFIG_INIT;
    XConsoleShellAdcSlot* slot;
    XAdc* adc;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcsa_parse_channel(argv[0], argv[1], &config.m_channel) ||
        !xcsa_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    if (!xcsa_authorized(shell, session, &config.m_channel,
                         XConsoleShellAdcOperation_Open, true))
        return xcsa_denied(shell);
    if (xcsa_find_slot(shell, &config.m_channel)) {
        if (!XConsoleShell_writeUtf8(shell, "adc: 通道已打开\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    slot = xcsa_empty_slot(shell);
    if (!slot) {
        if (!XConsoleShell_writeUtf8(shell, "adc: 槽位已满\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_ResourceLimit;
    }
    adc = XAdc_create(&config);
    if (!adc) return XConsoleResult_ResourceLimit;
    if (!XAdc_open(adc)) {
        XConsoleResult result = xcsa_error(shell, "打开", adc);
        XAdc_delete(adc);
        return result;
    }
    slot->adc = adc;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_ADC_CLOSE_ON
static int xcsa_close(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XAdcChannel channel;
    XConsoleShellAdcSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcsa_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcsa_find_slot(shell, &channel);
    if (!slot) return xcsa_missing(shell, "关闭");
    if (!xcsa_authorized(shell, session, &channel,
                         XConsoleShellAdcOperation_Close, true))
        return xcsa_denied(shell);
    XAdc_delete(slot->adc);
    slot->adc = NULL;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_ADC_INFO_ON
static int xcsa_info(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XAdcChannel channel;
    XConsoleShellAdcSlot* slot;
    XAdcConfig config = XADC_CONFIG_INIT;
    char line[256];
    int written;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcsa_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcsa_find_slot(shell, &channel);
    if (!slot) return xcsa_missing(shell, "查询配置");
    if (!xcsa_authorized(shell, session, &channel,
                         XConsoleShellAdcOperation_Info, false))
        return xcsa_denied(shell);
    if (!XAdc_getConfig(slot->adc, &config)) return xcsa_error(shell, "查询配置", slot->adc);
    written = snprintf(line, sizeof(line),
                       "adc %u/%u state=%s resolution=%u reference-mv=%u sample-us=%u oversample=%u features=0x%08x\n",
                       config.m_channel.m_controller, config.m_channel.m_channel,
                       XAdc_isOpen(slot->adc) ? "open" : "close",
                       (unsigned)config.m_resolutionBits,
                       (unsigned)config.m_referenceMv,
                       (unsigned)config.m_sampleTimeUs,
                       (unsigned)config.m_oversample,
                       (unsigned)XAdc_features(slot->adc));
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_ADC_READ_ON
static int xcsa_read(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XAdcChannel channel;
    XConsoleShellAdcSlot* slot;
    bool millivolts = false;
    int32_t timeoutMs = 0;
    int i;
    uint32_t value;
    char line[128];
    int written;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcsa_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--mv") == 0) millivolts = true;
        else if (strcmp(argv[i], "--raw") == 0) millivolts = false;
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            if (!xcsa_parse_i32(argv[++i], &timeoutMs) ||
                timeoutMs > XCONSOLE_SHELL_ADC_MAX_WAIT_MS) return XConsoleResult_InvalidArgument;
        } else return XConsoleResult_InvalidArgument;
    }
    slot = xcsa_find_slot(shell, &channel);
    if (!slot) return xcsa_missing(shell, "读取");
    if (!xcsa_authorized(shell, session, &channel,
                         XConsoleShellAdcOperation_Read, false))
        return xcsa_denied(shell);
    if (millivolts) {
        if (!XAdc_readMillivolts(slot->adc, &value, timeoutMs))
            return xcsa_error(shell, "读取", slot->adc);
        written = snprintf(line, sizeof(line), "adc %u/%u %u mV\n",
                           channel.m_controller, channel.m_channel,
                           (unsigned)value);
    } else {
        if (!XAdc_readRaw(slot->adc, &value, timeoutMs))
            return xcsa_error(shell, "读取", slot->adc);
        written = snprintf(line, sizeof(line), "adc %u/%u raw=%u\n",
                           channel.m_controller, channel.m_channel,
                           (unsigned)value);
    }
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_ADC_CONFIGURE_ON
static int xcsa_configure(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XAdcChannel channel;
    XConsoleShellAdcSlot* slot;
    XAdcConfig config = XADC_CONFIG_INIT;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcsa_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcsa_find_slot(shell, &channel);
    if (!slot) return xcsa_missing(shell, "配置");
    if (!XAdc_getConfig(slot->adc, &config) ||
        !xcsa_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    if (!xcsa_authorized(shell, session, &channel,
                         XConsoleShellAdcOperation_Configure, true))
        return xcsa_denied(shell);
    return XAdc_configure(slot->adc, &config)
               ? XConsoleResult_Ok : xcsa_error(shell, "配置", slot->adc);
}
#endif

void XConsoleShellAdc_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0; i < XCONSOLE_SHELL_ADC_SLOT_CAPACITY; ++i) {
        if (shell->m_adcSlots[i].adc) {
            XAdc_delete(shell->m_adcSlots[i].adc);
            shell->m_adcSlots[i].adc = NULL;
        }
    }
    shell->m_adcAuthorize = NULL;
    shell->m_adcAuthorizeUserData = NULL;
}

bool XConsoleShell_setAdcAuthorizeCallback(
    XConsoleShell* self, XConsoleShellAdcAuthorizeFn authorize,
    void* userData)
{
    if (!self) return false;
    self->m_adcAuthorize = authorize;
    self->m_adcAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

#define XCSA_HAS_COMMANDS (XCONSOLE_SHELL_ADC_LIST_ON || XCONSOLE_SHELL_ADC_OPEN_ON || \
                           XCONSOLE_SHELL_ADC_CLOSE_ON || XCONSOLE_SHELL_ADC_INFO_ON || \
                           XCONSOLE_SHELL_ADC_READ_ON || XCONSOLE_SHELL_ADC_CONFIGURE_ON)

#if XCSA_HAS_COMMANDS
static const XConsoleCommand g_adcCommands[] = {
#if XCONSOLE_SHELL_ADC_LIST_ON
    { "list", NULL, "列出已打开 ADC", "adc list", 0, 0, 0, xcsa_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_ADC_OPEN_ON
    { "open", NULL, "打开 ADC 通道", "adc open <controller> <channel> [options]", 2, -1,
      XConsoleCommandFlag_Dangerous, xcsa_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_ADC_CLOSE_ON
    { "close", NULL, "关闭 ADC 通道", "adc close <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcsa_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_ADC_INFO_ON
    { "info", NULL, "查看 ADC 配置", "adc info <controller> <channel>", 2, 2, 0, xcsa_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_ADC_READ_ON
    { "read", NULL, "读取 ADC 样本", "adc read <controller> <channel> [--mv|--raw] [--timeout ms]", 2, -1, 0, xcsa_read, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_ADC_CONFIGURE_ON
    { "configure", NULL, "重新配置 ADC", "adc configure <controller> <channel> [options]", 2, -1,
      XConsoleCommandFlag_Dangerous, xcsa_configure, NULL, 0, NULL },
#endif
};
#else
static const XConsoleCommand g_adcCommands[1] = {{ NULL }};
#endif

const XConsoleCommand XConsoleShellAdc_command = {
    "adc", NULL, "ADC 模拟采样命令", "adc <subcommand>", 0, -1, 0, NULL,
    g_adcCommands, XCSA_HAS_COMMANDS ? sizeof(g_adcCommands) / sizeof(g_adcCommands[0]) : 0u,
    NULL
};

#endif /* Shell、命令、I/O 和 ADC 均启用 */
