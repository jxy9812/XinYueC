/**
 * @file       XConsoleShellSpi.c
 * @brief      SPI Shell 命令实现。
 * @details    命令层只调用 XSpi 公共 API，使用固定槽位和固定长度缓冲，
 *             不直接访问片选 GPIO、DMA、sysfs、Win32 或芯片寄存器。
 */
#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_SPI_ON

#include "XConsoleShellSpi.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

#define XCSS_SPI_HAS_COMMANDS (XCONSOLE_SHELL_SPI_LIST_ON || XCONSOLE_SHELL_SPI_OPEN_ON || \
                               XCONSOLE_SHELL_SPI_CLOSE_ON || XCONSOLE_SHELL_SPI_INFO_ON || \
                               XCONSOLE_SHELL_SPI_TRANSFER_ON)

static bool xcss_parse_u32(const char* text, uint32_t* value)
{
    XUtf8StringView view;
    uint64_t parsed;
    bool ok = false;
    if (!text || !value || !text[0]) return false;
    view = XUtf8StringView_create_cstr(text);
    parsed = XUtf8StringView_toULongLong(&view, &ok, 0);
    if (!ok || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool xcss_parse_hex(const char* text, uint8_t* data, size_t capacity,
                           size_t* length)
{
    size_t i;
    size_t size;
    if (!text || !data || !length) return false;
    size = strlen(text);
    if (size == 0u || (size & 1u) != 0u || size / 2u > capacity) return false;
    for (i = 0u; i < size / 2u; ++i) {
        unsigned value = 0u;
        int digitIndex;
        for (digitIndex = 0; digitIndex < 2; ++digitIndex) {
            char c = text[i * 2u + (size_t)digitIndex];
            unsigned digit;
            if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        data[i] = (uint8_t)value;
    }
    *length = size / 2u;
    return true;
}

static XSpiConfig* xcss_find_config(XConsoleShell* shell, uint32_t controller,
                                    uint32_t chipSelect, XSpiConfig* config,
                                    XConsoleShellSpiSlot** result)
{
    size_t i;
    if (!shell || !config || !result) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_SPI_SLOT_CAPACITY; ++i) {
        XConsoleShellSpiSlot* slot = &shell->m_spiSlots[i];
        XSpiConfig current = XSPI_CONFIG_INIT;
        if (slot->m_spi && XSpi_getConfig(slot->m_spi, &current) &&
            current.m_controller == controller && current.m_chipSelect == chipSelect) {
            *config = current;
            *result = slot;
            return config;
        }
    }
    return NULL;
}

static XConsoleShellSpiSlot* xcss_find_slot(XConsoleShell* shell,
                                            uint32_t controller,
                                            uint32_t chipSelect)
{
    XSpiConfig config;
    XConsoleShellSpiSlot* slot = NULL;
    return xcss_find_config(shell, controller, chipSelect, &config, &slot) ? slot : NULL;
}

static XConsoleShellSpiSlot* xcss_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_SPI_SLOT_CAPACITY; ++i)
        if (!shell->m_spiSlots[i].m_spi) return &shell->m_spiSlots[i];
    return NULL;
}

static bool xcss_authorized(XConsoleShell* shell,
                            const XConsoleShellSession* session,
                            const XSpiConfig* config,
                            XConsoleShellSpiOperation operation,
                            bool dangerous)
{
    if (!shell || !session || !config) return false;
    if (shell->m_spiAuthorize)
        return shell->m_spiAuthorize(shell->m_spiAuthorizeUserData, session,
                                     config, operation);
#if XCONSOLE_SHELL_SPI_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}

static XConsoleResult xcss_error(XConsoleShell* shell, const char* operation,
                                 const XSpi* spi)
{
    char line[160];
    XSpiError error = spi ? XSpi_lastError(spi) : XSpiError_Unknown;
    int written = snprintf(line, sizeof(line), "spi: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作",
                           XSpi_errorString(error),
                           spi ? (int)XSpi_nativeError(spi) : 0);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
    if (error == XSpiError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XSpiError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XSpiError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XSpiError_Busy) return XConsoleResult_ResourceLimit;
    return XConsoleResult_Failed;
}

static XConsoleResult xcss_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "spi: %s: 目标未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}

static XConsoleResult xcss_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "spi: 策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}

static bool xcss_apply_options(XSpiConfig* config, int argc,
                               const char* const* argv, int start)
{
    int i;
    if (!config || !argv || start < 0 || start > argc) return false;
    for (i = start; i < argc; ++i) {
        uint32_t value;
        if (strcmp(argv[i], "--speed") == 0 || strcmp(argv[i], "--mode") == 0 ||
            strcmp(argv[i], "--bits") == 0) {
            if (i + 1 >= argc || !xcss_parse_u32(argv[i + 1], &value)) return false;
            if (strcmp(argv[i], "--speed") == 0) {
                if (value == 0u) return false;
                config->m_frequencyHz = value;
            } else if (strcmp(argv[i], "--mode") == 0) {
                if (value > 3u) return false;
                config->m_mode = (uint8_t)value;
            } else {
                if (value != 8u && value != 16u) return false;
                config->m_bitsPerWord = (uint8_t)value;
            }
            ++i;
        } else if (strcmp(argv[i], "--lsb-first") == 0) {
            config->m_lsbFirst = true;
        } else {
            return false;
        }
    }
    return true;
}

#if XCONSOLE_SHELL_SPI_LIST_ON
static int xcss_list(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0u;
    (void)argc; (void)argv; (void)userData;
    if (!shell || !session ||
        !XConsoleShell_writeUtf8(shell, " ctrl  cs state  speed mode bits order\n"))
        return XConsoleResult_IoError;
    for (i = 0u; i < XCONSOLE_SHELL_SPI_SLOT_CAPACITY; ++i) {
        XSpiConfig config = XSPI_CONFIG_INIT;
        XConsoleShellSpiSlot* slot = &shell->m_spiSlots[i];
        char line[144];
        int written;
        if (!slot->m_spi || !XSpi_getConfig(slot->m_spi, &config) ||
            !xcss_authorized(shell, session, &config,
                             XConsoleShellSpiOperation_List, false)) continue;
        written = snprintf(line, sizeof(line), "%5u %3u %-6s %7u %4u %4u %s\n",
                           config.m_controller, config.m_chipSelect,
                           XSpi_isOpen(slot->m_spi) ? "open" : "close",
                           config.m_frequencyHz, config.m_mode,
                           config.m_bitsPerWord, config.m_lsbFirst ? "lsb" : "msb");
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0u && !XConsoleShell_writeUtf8(shell, "spi: 没有打开的从机\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_SPI_OPEN_ON
static int xcss_open(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XSpiConfig config = XSPI_CONFIG_INIT;
    XConsoleShellSpiSlot* slot;
    uint32_t controller;
    uint32_t chipSelect;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcss_parse_u32(argv[0], &controller) ||
        !xcss_parse_u32(argv[1], &chipSelect) ||
        !xcss_apply_options(&config, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    config.m_controller = controller;
    config.m_chipSelect = chipSelect;
    if (!xcss_authorized(shell, session, &config,
                         XConsoleShellSpiOperation_Open, true)) return xcss_denied(shell);
    if (xcss_find_slot(shell, controller, chipSelect))
        return XConsoleResult_ResourceLimit;
    slot = xcss_empty_slot(shell);
    if (!slot) return XConsoleResult_ResourceLimit;
    slot->m_spi = XSpi_create(&config);
    if (!slot->m_spi) return XConsoleResult_ResourceLimit;
    if (!XSpi_open(slot->m_spi)) {
        XConsoleResult result = xcss_error(shell, "打开", slot->m_spi);
        XSpi_delete(slot->m_spi);
        slot->m_spi = NULL;
        return result;
    }
    return XConsoleShell_writeUtf8(shell, "spi: 打开: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_SPI_CLOSE_ON
static int xcss_close(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XSpiConfig config = XSPI_CONFIG_INIT;
    XConsoleShellSpiSlot* slot;
    uint32_t controller;
    uint32_t chipSelect;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcss_parse_u32(argv[0], &controller) || !xcss_parse_u32(argv[1], &chipSelect))
        return XConsoleResult_InvalidArgument;
    slot = xcss_find_slot(shell, controller, chipSelect);
    if (!slot || !XSpi_getConfig(slot->m_spi, &config)) return xcss_missing(shell, "关闭");
    if (!xcss_authorized(shell, session, &config,
                         XConsoleShellSpiOperation_Close, true)) return xcss_denied(shell);
    XSpi_delete(slot->m_spi);
    slot->m_spi = NULL;
    return XConsoleShell_writeUtf8(shell, "spi: 关闭: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_SPI_INFO_ON
static int xcss_info(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XSpiConfig config = XSPI_CONFIG_INIT;
    XConsoleShellSpiSlot* slot;
    uint32_t controller;
    uint32_t chipSelect;
    char line[256];
    int written;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcss_parse_u32(argv[0], &controller) || !xcss_parse_u32(argv[1], &chipSelect))
        return XConsoleResult_InvalidArgument;
    slot = xcss_find_slot(shell, controller, chipSelect);
    if (!slot || !XSpi_getConfig(slot->m_spi, &config)) return xcss_missing(shell, "查询配置");
    if (!xcss_authorized(shell, session, &config,
                         XConsoleShellSpiOperation_Info, false)) return xcss_denied(shell);
    written = snprintf(line, sizeof(line),
        "controller=%u cs=%u open=%s speed=%u mode=%u bits=%u order=%s features=0x%08x error=%s\n",
        config.m_controller, config.m_chipSelect, XSpi_isOpen(slot->m_spi) ? "yes" : "no",
        config.m_frequencyHz, config.m_mode, config.m_bitsPerWord,
        config.m_lsbFirst ? "lsb" : "msb", (unsigned)XSpi_features(slot->m_spi),
        XSpi_errorString(XSpi_lastError(slot->m_spi)));
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_SPI_TRANSFER_ON
static int xcss_transfer(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XSpiConfig config = XSPI_CONFIG_INIT;
    XConsoleShellSpiSlot* slot;
    uint32_t controller;
    uint32_t chipSelect;
    uint8_t tx[XCONSOLE_SHELL_SPI_MAX_TRANSFER];
    uint8_t rx[XCONSOLE_SHELL_SPI_MAX_TRANSFER];
    size_t length;
    size_t i;
    char line[3u * XCONSOLE_SHELL_SPI_MAX_TRANSFER + 32u];
    int written;
    (void)userData;
    if (!shell || !session || argc != 3 ||
        !xcss_parse_u32(argv[0], &controller) || !xcss_parse_u32(argv[1], &chipSelect) ||
        !xcss_parse_hex(argv[2], tx, sizeof(tx), &length))
        return XConsoleResult_InvalidArgument;
    slot = xcss_find_slot(shell, controller, chipSelect);
    if (!slot || !XSpi_getConfig(slot->m_spi, &config)) return xcss_missing(shell, "传输");
    if (!xcss_authorized(shell, session, &config,
                         XConsoleShellSpiOperation_Transfer, true)) return xcss_denied(shell);
    if (!XSpi_transfer(slot->m_spi, tx, rx, length, 1000))
        return xcss_error(shell, "传输", slot->m_spi);
    written = snprintf(line, sizeof(line), "spi: 接收:");
    if (written < 0 || (size_t)written >= sizeof(line)) return XConsoleResult_IoError;
    for (i = 0u; i < length; ++i) {
        int count = snprintf(line + (size_t)written, sizeof(line) - (size_t)written,
                             " %02x", rx[i]);
        if (count < 0 || (size_t)count >= sizeof(line) - (size_t)written)
            return XConsoleResult_IoError;
        written += count;
    }
    if ((size_t)written + 2u > sizeof(line)) return XConsoleResult_IoError;
    line[written++] = '\n';
    line[written] = '\0';
    return XConsoleShell_writeUtf8(shell, line) ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

void XConsoleShellSpi_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0u; i < XCONSOLE_SHELL_SPI_SLOT_CAPACITY; ++i) {
        if (shell->m_spiSlots[i].m_spi) XSpi_delete(shell->m_spiSlots[i].m_spi);
        shell->m_spiSlots[i].m_spi = NULL;
    }
    shell->m_spiAuthorize = NULL;
    shell->m_spiAuthorizeUserData = NULL;
}

bool XConsoleShell_setSpiAuthorizeCallback(
    XConsoleShell* self, XConsoleShellSpiAuthorizeFn authorize, void* userData)
{
    if (!self) return false;
    self->m_spiAuthorize = authorize;
    self->m_spiAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

#if XCSS_SPI_HAS_COMMANDS
static const XConsoleCommand g_spiCommands[] = {
#if XCONSOLE_SHELL_SPI_LIST_ON
    { "list", NULL, "列出 SPI 目标", "spi list", 0, 0, XConsoleCommandFlag_None, xcss_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_SPI_OPEN_ON
    { "open", NULL, "打开 SPI 目标", "spi open <controller> <cs> [options]", 2, -1, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xcss_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_SPI_CLOSE_ON
    { "close", NULL, "关闭 SPI 目标", "spi close <controller> <cs>", 2, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xcss_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_SPI_INFO_ON
    { "info", NULL, "显示 SPI 配置", "spi info <controller> <cs>", 2, 2, XConsoleCommandFlag_None, xcss_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_SPI_TRANSFER_ON
    { "transfer", NULL, "执行 SPI 全双工事务", "spi transfer <controller> <cs> <hex>", 3, 3, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xcss_transfer, NULL, 0, NULL },
#endif
};
#else
static const XConsoleCommand g_spiCommands[1] = {{ NULL }};
#endif

const XConsoleCommand XConsoleShellSpi_command = {
    "spi", NULL, "SPI 总线诊断", "spi <list|open|close|info|transfer> ...", 0, -1,
    XConsoleCommandFlag_None, NULL, g_spiCommands,
    XCSS_SPI_HAS_COMMANDS ? sizeof(g_spiCommands) / sizeof(g_spiCommands[0]) : 0u, NULL
};

#endif /* Shell、命令、I/O 和 SPI 均启用 */
