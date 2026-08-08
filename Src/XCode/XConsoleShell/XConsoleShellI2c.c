/**
 * @file       XConsoleShellI2c.c
 * @brief      I2C Shell 命令实现。
 * @details    Shell 仅调用 XI2c 公共接口，使用固定槽位和固定长度字节缓冲，
 *             不调用平台文件、ioctl 或设备驱动 API。写事务默认受产品策略
 *             回调保护，适合在现场诊断中开启只读功能而关闭硬件修改功能。
 */
#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_I2C_ON

#include "XConsoleShellI2c.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

#define XCS_I2C_HAS_COMMANDS (XCONSOLE_SHELL_I2C_LIST_ON || XCONSOLE_SHELL_I2C_OPEN_ON || \
                              XCONSOLE_SHELL_I2C_CLOSE_ON || XCONSOLE_SHELL_I2C_INFO_ON || \
                              XCONSOLE_SHELL_I2C_READ_ON || XCONSOLE_SHELL_I2C_WRITE_ON || \
                              XCONSOLE_SHELL_I2C_WRITEREAD_ON)

#if XCS_I2C_HAS_COMMANDS
static bool xci_parse_u32(const char* text, uint32_t* value)
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

static bool xci_parse_target(const char* controllerText, const char* addressText,
                             XI2cTarget* target)
{
    uint32_t controller;
    uint32_t address;
    if (!target || !xci_parse_u32(controllerText, &controller) ||
        !xci_parse_u32(addressText, &address) || address > 0x3ffu) return false;
    target->m_controller = controller;
    target->m_address = (uint16_t)address;
    target->m_addressMode = XI2cAddressMode_SevenBit;
    return true;
}

static bool xci_target_is_valid(const XI2cTarget* target)
{
    if (!target) return false;
    if (target->m_addressMode == XI2cAddressMode_SevenBit)
        return target->m_address <= 0x7fu;
    if (target->m_addressMode == XI2cAddressMode_TenBit)
        return target->m_address <= 0x3ffu;
    return false;
}

static bool xci_apply_optional_address_mode(XI2cTarget* target, int argc,
                                            const char* const* argv,
                                            int optionIndex)
{
    if (!target || !argv || optionIndex < 0 || argc < optionIndex ||
        argc > optionIndex + 1) return false;
    if (argc == optionIndex + 1) {
        if (strcmp(argv[optionIndex], "--ten-bit") != 0) return false;
        target->m_addressMode = XI2cAddressMode_TenBit;
    }
    return xci_target_is_valid(target);
}

static bool xci_parse_hex(const char* text, uint8_t* data, size_t capacity, size_t* length)
{
    size_t i;
    size_t size;
    if (!text || !data || !length) return false;
    size = strlen(text);
    if (size == 0u || (size & 1u) != 0u || size / 2u > capacity) return false;
    for (i = 0u; i < size / 2u; ++i) {
        unsigned value = 0u;
        int j;
        for (j = 0; j < 2; ++j) {
            char c = text[i * 2u + (size_t)j];
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

static XConsoleShellI2cSlot* xci_find_slot(XConsoleShell* shell, const XI2cTarget* target)
{
    size_t i;
    if (!shell || !target) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_I2C_SLOT_CAPACITY; ++i) {
        XI2cConfig config = XI2C_CONFIG_INIT;
        XConsoleShellI2cSlot* slot = &shell->m_i2cSlots[i];
        if (slot->m_bus && XI2c_getConfig(slot->m_bus, &config) &&
            config.m_target.m_controller == target->m_controller &&
            config.m_target.m_address == target->m_address &&
            config.m_target.m_addressMode == target->m_addressMode) return slot;
    }
    return NULL;
}
static XConsoleShellI2cSlot* xci_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_I2C_SLOT_CAPACITY; ++i)
        if (!shell->m_i2cSlots[i].m_bus) return &shell->m_i2cSlots[i];
    return NULL;
}
static bool xci_authorized(XConsoleShell* shell, const XConsoleShellSession* session,
                           const XI2cTarget* target, XConsoleShellI2cOperation operation,
                           bool dangerous)
{
    if (!shell || !session || !target) return false;
    if (shell->m_i2cAuthorize)
        return shell->m_i2cAuthorize(shell->m_i2cAuthorizeUserData, session,
                                     target, operation);
#if XCONSOLE_SHELL_I2C_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}
static XConsoleResult xci_write_error(XConsoleShell* shell, const char* operation,
                                      const XI2c* bus)
{
    char line[160];
    XI2cError error = bus ? XI2c_lastError(bus) : XI2cError_Unknown;
    int written = snprintf(line, sizeof(line), "i2c: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作", XI2c_errorString(error),
                           bus ? (int)XI2c_nativeError(bus) : 0);
    if (written < 0 || (size_t)written >= sizeof(line) || !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    if (error == XI2cError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XI2cError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XI2cError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XI2cError_Busy) return XConsoleResult_ResourceLimit;
    return XConsoleResult_Failed;
}
static XConsoleResult xci_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "i2c: 策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}
static XConsoleResult xci_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "i2c: %s: 目标未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) && XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}
#endif /* XCS_I2C_HAS_COMMANDS: helpers */

bool XConsoleShell_setI2cAuthorizeCallback(
    XConsoleShell* self, XConsoleShellI2cAuthorizeFn authorize, void* userData)
{
    if (!self) return false;
    self->m_i2cAuthorize = authorize;
    self->m_i2cAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

void XConsoleShellI2c_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0u; i < XCONSOLE_SHELL_I2C_SLOT_CAPACITY; ++i) {
        if (shell->m_i2cSlots[i].m_bus) {
            XI2c_delete(shell->m_i2cSlots[i].m_bus);
            shell->m_i2cSlots[i].m_bus = NULL;
        }
    }
    shell->m_i2cAuthorize = NULL;
    shell->m_i2cAuthorizeUserData = NULL;
}

#if XCS_I2C_HAS_COMMANDS
#if XCONSOLE_SHELL_I2C_LIST_ON
static int xci_list(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0u;
    (void)argc; (void)argv; (void)userData;
    if (!shell || !session || !XConsoleShell_writeUtf8(shell, " ctrl  addr mode  state  speed\n"))
        return XConsoleResult_InvalidArgument;
    for (i = 0u; i < XCONSOLE_SHELL_I2C_SLOT_CAPACITY; ++i) {
        XI2cConfig config = XI2C_CONFIG_INIT;
        char line[128];
        int written;
        XConsoleShellI2cSlot* slot = &shell->m_i2cSlots[i];
        if (!slot->m_bus || !XI2c_getConfig(slot->m_bus, &config) ||
            !xci_authorized(shell, session, &config.m_target,
                            XConsoleShellI2cOperation_List, false)) continue;
        written = snprintf(line, sizeof(line), "%5u 0x%03x %-4s %-6s %7u\n",
                           config.m_target.m_controller, config.m_target.m_address,
                           config.m_target.m_addressMode == XI2cAddressMode_TenBit ? "10b" : "7b",
                           XI2c_isOpen(slot->m_bus) ? "open" : "close", config.m_frequencyHz);
        if (written < 0 || (size_t)written >= sizeof(line) || !XConsoleShell_writeUtf8(shell, line))
            return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0u && !XConsoleShell_writeUtf8(shell, "i2c: 没有打开的从机\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_I2C_OPEN_ON
static int xci_open(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XI2cConfig config = XI2C_CONFIG_INIT;
    XConsoleShellI2cSlot* slot;
    int i;
    (void)userData;
    if (!shell || !session || argc < 2 || !xci_parse_target(argv[0], argv[1], &config.m_target))
        return XConsoleResult_InvalidArgument;
    for (i = 2; i < argc; ++i) {
        uint32_t value;
        if (strcmp(argv[i], "--speed") == 0) {
            if (i + 1 >= argc || !xci_parse_u32(argv[i + 1], &value) || value == 0u)
                return XConsoleResult_InvalidArgument;
            config.m_frequencyHz = value;
            ++i;
        } else if (strcmp(argv[i], "--ten-bit") == 0) {
            config.m_target.m_addressMode = XI2cAddressMode_TenBit;
        } else {
            return XConsoleResult_InvalidArgument;
        }
    }
    if (!xci_target_is_valid(&config.m_target))
        return XConsoleResult_InvalidArgument;
    if (!xci_authorized(shell, session, &config.m_target, XConsoleShellI2cOperation_Open, true)) return xci_denied(shell);
    if (xci_find_slot(shell, &config.m_target)) return XConsoleResult_ResourceLimit;
    slot = xci_empty_slot(shell);
    if (!slot) return XConsoleResult_ResourceLimit;
    slot->m_bus = XI2c_create(&config);
    if (!slot->m_bus) return XConsoleResult_Failed;
    if (!XI2c_open(slot->m_bus)) {
        XConsoleResult result = xci_write_error(shell, "打开", slot->m_bus);
        XI2c_delete(slot->m_bus); slot->m_bus = NULL; return result;
    }
    return XConsoleShell_writeUtf8(shell, "i2c: 打开: 成功\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_I2C_CLOSE_ON
static int xci_close(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XI2cTarget target;
    XConsoleShellI2cSlot* slot;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xci_parse_target(argv[0], argv[1], &target) ||
        !xci_apply_optional_address_mode(&target, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    slot = xci_find_slot(shell, &target);
    if (!slot) return xci_missing(shell, "关闭");
    if (!xci_authorized(shell, session, &target, XConsoleShellI2cOperation_Close, true)) return xci_denied(shell);
    XI2c_delete(slot->m_bus); slot->m_bus = NULL;
    return XConsoleShell_writeUtf8(shell, "i2c: 关闭: 成功\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_I2C_INFO_ON
static int xci_info(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XI2cTarget target; XI2cConfig config = XI2C_CONFIG_INIT; XConsoleShellI2cSlot* slot; char line[256]; int written;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xci_parse_target(argv[0], argv[1], &target) ||
        !xci_apply_optional_address_mode(&target, argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    slot = xci_find_slot(shell, &target); if (!slot) return xci_missing(shell, "查询配置");
    if (!XI2c_getConfig(slot->m_bus, &config)) return xci_write_error(shell, "查询配置", slot->m_bus);
    written = snprintf(line, sizeof(line), "ctrl=%u addr=0x%03x mode=%s state=%s speed=%u features=0x%08x error=%s\n",
                       config.m_target.m_controller, config.m_target.m_address,
                       config.m_target.m_addressMode == XI2cAddressMode_TenBit ? "10bit" : "7bit",
                       XI2c_isOpen(slot->m_bus) ? "open" : "closed", config.m_frequencyHz,
                       (unsigned)XI2c_features(slot->m_bus), XI2c_errorString(XI2c_lastError(slot->m_bus)));
    return written > 0 && (size_t)written < sizeof(line) && XConsoleShell_writeUtf8(shell, line) ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

static XConsoleResult xci_print_bytes(XConsoleShell* shell, const char* prefix, const uint8_t* data, size_t length)
{
    char line[3u * XCONSOLE_SHELL_I2C_MAX_TRANSFER + 64u]; size_t i; int written;
    written = snprintf(line, sizeof(line), "%s", prefix); if (written < 0 || (size_t)written >= sizeof(line)) return XConsoleResult_IoError;
    for (i = 0u; i < length; ++i) {
        int n = snprintf(line + (size_t)written, sizeof(line) - (size_t)written, "%s%02x", i ? " " : "", data[i]);
        if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
            return XConsoleResult_IoError;
        written += n;
    }
    if ((size_t)written + 1u >= sizeof(line)) return XConsoleResult_IoError;
    line[written++] = '\n'; line[written] = '\0';
    return XConsoleShell_writeUtf8(shell, line) ? XConsoleResult_Ok : XConsoleResult_IoError;
}

#if XCONSOLE_SHELL_I2C_READ_ON
static int xci_read(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XI2cTarget target; XConsoleShellI2cSlot* slot; uint32_t length; uint8_t data[XCONSOLE_SHELL_I2C_MAX_TRANSFER];
    (void)userData;
    if (!shell || !session || argc < 3 ||
        !xci_parse_target(argv[0], argv[1], &target) ||
        !xci_parse_u32(argv[2], &length) ||
        length > XCONSOLE_SHELL_I2C_MAX_TRANSFER ||
        !xci_apply_optional_address_mode(&target, argc, argv, 3))
        return XConsoleResult_InvalidArgument;
    slot = xci_find_slot(shell, &target); if (!slot) return xci_missing(shell, "读取");
    if (!xci_authorized(shell, session, &target, XConsoleShellI2cOperation_Read, false)) return xci_denied(shell);
    if (!XI2c_read(slot->m_bus, data, (size_t)length, 1000)) return xci_write_error(shell, "读取", slot->m_bus);
    return xci_print_bytes(shell, "i2c: 读取:", data, (size_t)length);
}
#endif

#if XCONSOLE_SHELL_I2C_WRITE_ON
static int xci_write(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XI2cTarget target; XConsoleShellI2cSlot* slot; uint8_t data[XCONSOLE_SHELL_I2C_MAX_TRANSFER]; size_t length;
    (void)userData;
    if (!shell || !session || argc < 3 ||
        !xci_parse_target(argv[0], argv[1], &target) ||
        !xci_parse_hex(argv[2], data, sizeof(data), &length) ||
        !xci_apply_optional_address_mode(&target, argc, argv, 3))
        return XConsoleResult_InvalidArgument;
    slot = xci_find_slot(shell, &target); if (!slot) return xci_missing(shell, "写入");
    if (!xci_authorized(shell, session, &target, XConsoleShellI2cOperation_Write, true)) return xci_denied(shell);
    if (!XI2c_write(slot->m_bus, data, length, 1000)) return xci_write_error(shell, "写入", slot->m_bus);
    return XConsoleShell_writeUtf8(shell, "i2c: 写入: 成功\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_I2C_WRITEREAD_ON
static int xci_writeread(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XI2cTarget target; XConsoleShellI2cSlot* slot; uint8_t writeData[XCONSOLE_SHELL_I2C_MAX_TRANSFER]; uint8_t readData[XCONSOLE_SHELL_I2C_MAX_TRANSFER]; size_t writeLength; uint32_t readLength;
    (void)userData;
    if (!shell || !session || argc < 4 ||
        !xci_parse_target(argv[0], argv[1], &target) ||
        !xci_parse_hex(argv[2], writeData, sizeof(writeData), &writeLength) ||
        !xci_parse_u32(argv[3], &readLength) ||
        readLength > XCONSOLE_SHELL_I2C_MAX_TRANSFER ||
        !xci_apply_optional_address_mode(&target, argc, argv, 4))
        return XConsoleResult_InvalidArgument;
    slot = xci_find_slot(shell, &target); if (!slot) return xci_missing(shell, "写后读");
    if (!xci_authorized(shell, session, &target, XConsoleShellI2cOperation_WriteRead, true)) return xci_denied(shell);
    if (!XI2c_writeRead(slot->m_bus, writeData, writeLength, readData, (size_t)readLength, 1000)) return xci_write_error(shell, "写后读", slot->m_bus);
    return xci_print_bytes(shell, "i2c: 读取:", readData, (size_t)readLength);
}
#endif
#endif /* XCS_I2C_HAS_COMMANDS: handlers */

#if XCS_I2C_HAS_COMMANDS
static const XConsoleCommand g_i2cCommands[] = {
#if XCONSOLE_SHELL_I2C_LIST_ON
    { "list", NULL, "列出 I2C 目标", "i2c list", 0, 0, XConsoleCommandFlag_None, xci_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_OPEN_ON
    { "open", NULL, "打开 I2C 目标", "i2c open <ctrl> <addr> [--speed hz] [--ten-bit]", 2, -1, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xci_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_CLOSE_ON
    { "close", NULL, "关闭 I2C 目标", "i2c close <ctrl> <addr> [--ten-bit]", 2, 3, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xci_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_INFO_ON
    { "info", NULL, "显示 I2C 配置", "i2c info <ctrl> <addr> [--ten-bit]", 2, 3, XConsoleCommandFlag_None, xci_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_READ_ON
    { "read", NULL, "读取 I2C 字节", "i2c read <ctrl> <addr> <length> [--ten-bit]", 3, 4, XConsoleCommandFlag_None, xci_read, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_WRITE_ON
    { "write", NULL, "写入 I2C 字节", "i2c write <ctrl> <addr> <hex> [--ten-bit]", 3, 4, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xci_write, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_I2C_WRITEREAD_ON
    { "writeread", NULL, "I2C 写后读事务", "i2c writeread <ctrl> <addr> <hex> <length> [--ten-bit]", 4, 5, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xci_writeread, NULL, 0, NULL },
#endif
};
#else
static const XConsoleCommand g_i2cCommands[1] = {{ NULL }};
#endif

const XConsoleCommand XConsoleShellI2c_command = {
    "i2c", NULL, "I2C 总线诊断", "i2c <list|open|close|info|read|write|writeread> ...", 0, -1,
    XConsoleCommandFlag_None, NULL, g_i2cCommands,
    XCS_I2C_HAS_COMMANDS ? sizeof(g_i2cCommands) / sizeof(g_i2cCommands[0]) : 0u, NULL
};

#endif
