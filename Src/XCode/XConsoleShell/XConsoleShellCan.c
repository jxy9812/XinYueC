/**
 * @file XConsoleShellCan.c
 * @brief XConsoleShell CAN 固定槽位命令实现。
 * @details
 * 本模块只调用 XCan 公共 API 和 Shell 输出接口，不直接访问 SocketCAN、
 * Win32、RTOS 或任何控制器寄存器。控制器句柄由 Shell 固定数组拥有，
 * 因此命令执行期间不会为每条命令分配堆内存。
 */
#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_CAN_ON

#include "XConsoleShellCan.h"
#include "XUtf8StringView.h"
#include <stdio.h>
#include <string.h>

static bool xcc_parse_u32(const char* text, uint32_t* value)
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

static bool xcc_parse_timeout(const char* text, int32_t* timeout)
{
    uint32_t value;
    if (!timeout || !xcc_parse_u32(text, &value) ||
        value > XCONSOLE_SHELL_CAN_MAX_TIMEOUT_MS || value > INT32_MAX)
        return false;
    *timeout = (int32_t)value;
    return true;
}

static bool xcc_parse_channel(const char* controllerText,
                              const char* channelText,
                              XCanChannel* channel)
{
    if (!channel || !xcc_parse_u32(controllerText, &channel->m_controller) ||
        !xcc_parse_u32(channelText, &channel->m_channel)) return false;
    channel->m_name = NULL;
    return true;
}

static XConsoleShellCanSlot* xcc_find_slot(XConsoleShell* shell,
                                           const XCanChannel* channel)
{
    size_t i;
    if (!shell || !channel) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_CAN_SLOT_CAPACITY; ++i) {
        XConsoleShellCanSlot* slot = &shell->m_canSlots[i];
        if (slot->can && slot->controller == channel->m_controller &&
            slot->channel == channel->m_channel)
            return slot;
    }
    return NULL;
}

static XConsoleShellCanSlot* xcc_empty_slot(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return NULL;
    for (i = 0u; i < XCONSOLE_SHELL_CAN_SLOT_CAPACITY; ++i)
        if (!shell->m_canSlots[i].can) return &shell->m_canSlots[i];
    return NULL;
}

static bool xcc_authorized(XConsoleShell* shell,
                           const XConsoleShellSession* session,
                           const XCanChannel* channel,
                           XConsoleShellCanOperation operation,
                           bool dangerous)
{
    if (!shell || !session || !channel) return false;
    if (shell->m_canAuthorize)
        return shell->m_canAuthorize(shell->m_canAuthorizeUserData, session,
                                     channel, operation);
#if XCONSOLE_SHELL_CAN_REQUIRE_POLICY_ON
    if (dangerous) return false;
#else
    (void)dangerous;
#endif
    return true;
}

static XConsoleResult xcc_denied(XConsoleShell* shell)
{
    return XConsoleShell_writeUtf8(shell, "can: 策略拒绝\n")
               ? XConsoleResult_PermissionDenied : XConsoleResult_IoError;
}

static XConsoleResult xcc_error(XConsoleShell* shell, const char* operation,
                                const XCan* can)
{
    char line[160];
    XCanError error = can ? XCan_lastError(can) : XCanError_Unknown;
    int32_t nativeError = can ? XCan_nativeError(can) : 0;
    int written = snprintf(line, sizeof(line), "can: %s: %s (本机错误=%d)\n",
                           operation ? operation : "操作",
                           XCan_errorString(error), (int)nativeError);
    if (written < 0 || (size_t)written >= sizeof(line) ||
        !XConsoleShell_writeUtf8(shell, line))
        return XConsoleResult_IoError;
    if (error == XCanError_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (error == XCanError_Unsupported) return XConsoleResult_NotSupported;
    if (error == XCanError_PermissionDenied) return XConsoleResult_PermissionDenied;
    if (error == XCanError_Busy || error == XCanError_Underrun ||
        error == XCanError_Overflow)
        return XConsoleResult_ResourceLimit;
    if (error == XCanError_Timeout || error == XCanError_WouldBlock)
        return XConsoleResult_Ok;
    return XConsoleResult_Failed;
}

static XConsoleResult xcc_missing(XConsoleShell* shell, const char* operation)
{
    char line[96];
    int written = snprintf(line, sizeof(line), "can: %s: 通道未打开\n",
                           operation ? operation : "操作");
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_InvalidArgument : XConsoleResult_IoError;
}

static const char* xcc_state_text(XCanBusState state)
{
    switch (state) {
    case XCanBusState_Stopped: return "stopped";
    case XCanBusState_ErrorActive: return "error-active";
    case XCanBusState_ErrorWarning: return "error-warning";
    case XCanBusState_ErrorPassive: return "error-passive";
    case XCanBusState_BusOff: return "bus-off";
    case XCanBusState_Recovering: return "recovering";
    default: return "unknown";
    }
}

static const char* xcc_format_text(XCanFrameFormat format)
{
    if (format == XCanFrameFormat_FlexibleDataRate) return "fd";
    if (format == XCanFrameFormat_Auto) return "auto";
    return "classical";
}

static const char* xcc_mode_text(XCanMode mode)
{
    if (mode == XCanMode_Loopback) return "loopback";
    if (mode == XCanMode_ListenOnly) return "listen-only";
    if (mode == XCanMode_Restricted) return "restricted";
    return "normal";
}

static bool xcc_copy_name(char* destination, size_t capacity,
                          const char* source)
{
    size_t length;
    if (!destination || capacity == 0u) return false;
    destination[0] = '\0';
    if (!source) return true;
    length = strlen(source);
    if (length >= capacity) return false;
    memcpy(destination, source, length + 1u);
    return true;
}

static bool xcc_apply_open_options(XCanConfig* config,
                                   char* name, size_t nameCapacity,
                                   int argc, const char* const* argv, int start)
{
    int i = start;
    bool hasName = false;
    if (!config || !name || !argv || start < 0 || start > argc) return false;
    while (i < argc) {
        uint32_t value;
        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc || !xcc_copy_name(name, nameCapacity, argv[i + 1]))
                return false;
            hasName = true;
            i += 2;
        } else if (strcmp(argv[i], "--bitrate") == 0) {
            if (i + 1 >= argc || !xcc_parse_u32(argv[i + 1], &value) || value == 0u)
                return false;
            config->m_nominalTiming.m_bitrate = value;
            i += 2;
        } else if (strcmp(argv[i], "--sample-point") == 0) {
            if (i + 1 >= argc || !xcc_parse_u32(argv[i + 1], &value) || value > 1000u)
                return false;
            config->m_nominalTiming.m_samplePointPermille = (uint16_t)value;
            i += 2;
        } else if (strcmp(argv[i], "--data-bitrate") == 0) {
            if (i + 1 >= argc || !xcc_parse_u32(argv[i + 1], &value) || value == 0u)
                return false;
            config->m_dataTiming.m_bitrate = value;
            config->m_frameFormat = XCanFrameFormat_FlexibleDataRate;
            i += 2;
        } else if (strcmp(argv[i], "--fd") == 0) {
            config->m_frameFormat = XCanFrameFormat_FlexibleDataRate;
            ++i;
        } else if (strcmp(argv[i], "--auto") == 0) {
            config->m_frameFormat = XCanFrameFormat_Auto;
            ++i;
        } else if (strcmp(argv[i], "--loopback") == 0) {
            config->m_mode = XCanMode_Loopback;
            ++i;
        } else if (strcmp(argv[i], "--listen-only") == 0) {
            config->m_mode = XCanMode_ListenOnly;
            ++i;
        } else if (strcmp(argv[i], "--restricted") == 0) {
            config->m_mode = XCanMode_Restricted;
            ++i;
        } else if (strcmp(argv[i], "--auto-restart") == 0) {
            config->m_flags |= XCanConfigFlag_AutoRestart;
            ++i;
        } else if (strcmp(argv[i], "--one-shot") == 0) {
            config->m_flags |= XCanConfigFlag_OneShot;
            ++i;
        } else if (strcmp(argv[i], "--receive-own") == 0) {
            config->m_flags |= XCanConfigFlag_ReceiveOwn;
            ++i;
        } else if (strcmp(argv[i], "--timestamp") == 0) {
            config->m_flags |= XCanConfigFlag_Timestamp;
            ++i;
        } else if (strcmp(argv[i], "--error-frames") == 0) {
            config->m_flags |= XCanConfigFlag_ErrorFrames;
            ++i;
        } else if (strcmp(argv[i], "--non-iso-fd") == 0) {
            config->m_flags |= XCanConfigFlag_NonIsoCanFd;
            ++i;
        } else {
            return false;
        }
    }
    config->m_channel.m_name = hasName ? name : NULL;
    return true;
}

#if XCONSOLE_SHELL_CAN_LIST_ON
static int xcc_list(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count = 0u;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!shell || !session ||
        !XConsoleShell_writeUtf8(shell,
            " ctrl  chan name                         open start state\n"))
        return XConsoleResult_IoError;
    for (i = 0u; i < XCONSOLE_SHELL_CAN_SLOT_CAPACITY; ++i) {
        XConsoleShellCanSlot* slot = &shell->m_canSlots[i];
        XCanConfig config = XCAN_CONFIG_INIT;
        char line[160];
        int written;
        XCanBusState state = XCanBusState_Unknown;
        XCanChannel channel;
        channel.m_controller = slot->controller;
        channel.m_channel = slot->channel;
        channel.m_name = slot->name;
        if (!slot->can || !xcc_authorized(shell, session,
                &channel,
                XConsoleShellCanOperation_List, false))
            continue;
        if (XCan_getConfig(slot->can, &config)) {
            XCanStatus status;
            if (XCan_getStatus(slot->can, &status)) state = status.m_state;
        }
        written = snprintf(line, sizeof(line), "%5u %5u %-28s %-4s %-5s %s\n",
                           slot->controller, slot->channel,
                           slot->name[0] ? slot->name : "-",
                           XCan_isOpen(slot->can) ? "yes" : "no",
                           XCan_isStarted(slot->can) ? "yes" : "no",
                           xcc_state_text(state));
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
        ++count;
    }
    if (count == 0u && !XConsoleShell_writeUtf8(shell, "can: 没有打开的通道\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_CAN_OPEN_ON
static int xcc_open(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XCanConfig config = XCAN_CONFIG_INIT;
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcc_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Open, true)) return xcc_denied(shell);
    if (xcc_find_slot(shell, &channel)) {
        return XConsoleShell_writeUtf8(shell, "can: 打开: 通道已打开\n")
                   ? XConsoleResult_ResourceLimit : XConsoleResult_IoError;
    }
    slot = xcc_empty_slot(shell);
    if (!slot) {
        return XConsoleShell_writeUtf8(shell, "can: 打开: 槽位已满\n")
                   ? XConsoleResult_ResourceLimit : XConsoleResult_IoError;
    }
    if (!xcc_apply_open_options(&config, slot->name, sizeof(slot->name),
                                argc, argv, 2))
        return XConsoleResult_InvalidArgument;
    config.m_channel.m_controller = channel.m_controller;
    config.m_channel.m_channel = channel.m_channel;
    slot->can = XCan_create(&config);
    if (!slot->can) {
        slot->name[0] = '\0';
        return XConsoleShell_writeUtf8(shell, "can: 打开: 创建失败\n")
                   ? XConsoleResult_Failed : XConsoleResult_IoError;
    }
    if (!XCan_open(slot->can)) {
        XConsoleResult result = xcc_error(shell, "打开", slot->can);
        XCan_delete(slot->can);
        slot->can = NULL;
        slot->name[0] = '\0';
        return result;
    }
    slot->controller = channel.m_controller;
    slot->channel = channel.m_channel;
    return XConsoleShell_writeUtf8(shell, "can: 打开: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_CLOSE_ON
static int xcc_close(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcc_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "关闭");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Close, true)) return xcc_denied(shell);
    XCan_delete(slot->can);
    memset(slot, 0, sizeof(*slot));
    return XConsoleShell_writeUtf8(shell, "can: 关闭: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_START_ON
static int xcc_start(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcc_parse_channel(argv[0], argv[1], &channel)) return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "启动");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Start, true)) return xcc_denied(shell);
    if (!XCan_start(slot->can)) return xcc_error(shell, "启动", slot->can);
    return XConsoleShell_writeUtf8(shell, "can: 启动: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_STOP_ON
static int xcc_stop(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 ||
        !xcc_parse_channel(argv[0], argv[1], &channel)) return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "停止");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Stop, true)) return xcc_denied(shell);
    XCan_stop(slot->can);
    return XConsoleShell_writeUtf8(shell, "can: 停止: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_INFO_ON || XCONSOLE_SHELL_CAN_STATUS_ON
static XConsoleResult xcc_get_slot(XConsoleShell* shell,
                                   XConsoleShellSession* session,
                                   const char* operation, int argc,
                                   const char* const* argv,
                                   XConsoleShellCanOperation command,
                                   XConsoleShellCanSlot** result,
                                   XCanChannel* channel)
{
    if (!shell || !session || !result || !channel || argc != 2 ||
        !xcc_parse_channel(argv[0], argv[1], channel))
        return XConsoleResult_InvalidArgument;
    *result = xcc_find_slot(shell, channel);
    if (!*result) {
        (void)xcc_missing(shell, operation);
        return XConsoleResult_InvalidArgument;
    }
    if (!xcc_authorized(shell, session, channel, command, false))
        return xcc_denied(shell);
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_CAN_INFO_ON
static int xcc_info(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    XCanConfig config = XCAN_CONFIG_INIT;
    XCanStatus status;
    char line[400];
    int written;
    (void)userData;
    {
        XConsoleResult result = xcc_get_slot(
            shell, session, "查询配置", argc, argv,
            XConsoleShellCanOperation_Info, &slot, &channel);
        if (result != XConsoleResult_Ok) return result;
    }
    if (!XCan_getConfig(slot->can, &config)) return xcc_error(shell, "查询配置", slot->can);
    if (!XCan_getStatus(slot->can, &status)) {
        memset(&status, 0, sizeof(status));
        status.m_state = XCanBusState_Unknown;
        status.m_receiveErrorCount = UINT32_MAX;
        status.m_transmitErrorCount = UINT32_MAX;
        status.m_rxPending = UINT32_MAX;
        status.m_txPending = UINT32_MAX;
    }
    written = snprintf(line, sizeof(line),
        "controller=%u channel=%u name=%s open=%s started=%s\n"
        "format=%s mode=%s bitrate=%u data-bitrate=%u flags=0x%08x\n"
        "state=%s rx-errors=%u tx-errors=%u rx-pending=%u tx-pending=%u\n"
        "features=0x%08x error=%s native=%d\n",
        channel.m_controller, channel.m_channel, slot->name[0] ? slot->name : "-",
        XCan_isOpen(slot->can) ? "yes" : "no",
        XCan_isStarted(slot->can) ? "yes" : "no",
        xcc_format_text(config.m_frameFormat), xcc_mode_text(config.m_mode),
        config.m_nominalTiming.m_bitrate, config.m_dataTiming.m_bitrate,
        (unsigned)config.m_flags, xcc_state_text(status.m_state),
        status.m_receiveErrorCount, status.m_transmitErrorCount,
        status.m_rxPending, status.m_txPending,
        (unsigned)XCan_features(slot->can),
        XCan_errorString(XCan_lastError(slot->can)),
        (int)XCan_nativeError(slot->can));
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_STATUS_ON
static int xcc_status(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    XCanStatus status;
    char line[180];
    int written;
    (void)userData;
    {
        XConsoleResult result = xcc_get_slot(
            shell, session, "查询状态", argc, argv,
            XConsoleShellCanOperation_Status, &slot, &channel);
        if (result != XConsoleResult_Ok) return result;
    }
    if (!XCan_getStatus(slot->can, &status)) return xcc_error(shell, "查询状态", slot->can);
    written = snprintf(line, sizeof(line),
                       "can %u:%u state=%s rx-errors=%u tx-errors=%u "
                       "rx-pending=%u tx-pending=%u flags=0x%08x\n",
                       channel.m_controller, channel.m_channel,
                       xcc_state_text(status.m_state), status.m_receiveErrorCount,
                       status.m_transmitErrorCount, status.m_rxPending,
                       status.m_txPending, (unsigned)status.m_nativeFlags);
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_SEND_ON
static bool xcc_parse_frame(XCanFrame* frame, int argc, const char* const* argv,
                            int* timeout)
{
    int i = 3;
    uint32_t value;
    if (!frame || !argv || !timeout || argc < 3 ||
        !xcc_parse_u32(argv[2], &value)) return false;
    *frame = (XCanFrame)XCAN_FRAME_INIT;
    frame->m_id = value;
    frame->m_idFormat = XCanIdFormat_Standard;
    *timeout = 1000;
    while (i < argc) {
        if (strcmp(argv[i], "--extended") == 0) {
            frame->m_idFormat = XCanIdFormat_Extended;
            ++i;
        } else if (strcmp(argv[i], "--fd") == 0) {
            frame->m_flags |= XCanFrameFlag_FlexibleDataRate;
            ++i;
        } else if (strcmp(argv[i], "--remote") == 0) {
            frame->m_type = XCanFrameType_Remote;
            ++i;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc || !xcc_parse_timeout(argv[i + 1], timeout)) return false;
            i += 2;
        } else if (strcmp(argv[i], "--dlc") == 0) {
            if (i + 1 >= argc || !xcc_parse_u32(argv[i + 1], &value) || value > 15u)
                return false;
            frame->m_dlc = (uint8_t)value;
            i += 2;
        } else {
            if (!xcc_parse_u32(argv[i], &value) || value > 255u ||
                frame->m_length >= XCAN_FD_DATA_LENGTH)
                return false;
            frame->m_data[frame->m_length++] = (uint8_t)value;
            ++i;
        }
    }
    if (frame->m_idFormat == XCanIdFormat_Standard && frame->m_id > XCAN_STANDARD_ID_MAX)
        return false;
    if (frame->m_idFormat == XCanIdFormat_Extended && frame->m_id > XCAN_EXTENDED_ID_MAX)
        return false;
    if ((frame->m_flags & XCanFrameFlag_FlexibleDataRate) == 0u &&
        frame->m_length > XCAN_CLASSIC_DATA_LENGTH)
        return false;
    return true;
}
#endif

#if XCONSOLE_SHELL_CAN_RECEIVE_ON
static void xcc_print_frame(XConsoleShell* shell, const XCanFrame* frame)
{
    char line[240];
    size_t offset = 0u;
    int written;
    uint8_t i;
    if (!shell || !frame) return;
    written = snprintf(line, sizeof(line), "id=0x%08x format=%s type=%s len=%u data=",
                       frame->m_id, frame->m_idFormat == XCanIdFormat_Extended ? "extended" : "standard",
                       frame->m_type == XCanFrameType_Remote ? "remote" :
                       (frame->m_type == XCanFrameType_Error ? "error" : "data"),
                       frame->m_length);
    if (written < 0 || (size_t)written >= sizeof(line)) return;
    offset = (size_t)written;
    for (i = 0u; i < frame->m_length && offset + 4u < sizeof(line); ++i) {
        written = snprintf(line + offset, sizeof(line) - offset, "%02x%s",
                           frame->m_data[i], i + 1u == frame->m_length ? "" : " ");
        if (written < 0) return;
        offset += (size_t)written;
    }
    if (offset + 2u < sizeof(line)) {
        line[offset++] = '\n';
        line[offset] = '\0';
        (void)XConsoleShell_writeUtf8(shell, line);
    }
}
#endif

#if XCONSOLE_SHELL_CAN_SEND_ON
static int xcc_send(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    XCanFrame frame;
    int timeout;
    XCanIoResult result;
    (void)userData;
    if (!shell || !session || !xcc_parse_channel(argv[0], argv[1], &channel) ||
        !xcc_parse_frame(&frame, argc, argv, &timeout)) return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "发送");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Send, true)) return xcc_denied(shell);
    result = XCan_send(slot->can, &frame, timeout);
    if (result == XCanIoResult_Success)
        return XConsoleShell_writeUtf8(shell, "can: 发送: 成功\n")
                   ? XConsoleResult_Ok : XConsoleResult_IoError;
    if (result == XCanIoResult_Timeout || result == XCanIoResult_WouldBlock)
        return xcc_error(shell, "发送", slot->can);
    return xcc_error(shell, "发送", slot->can);
}
#endif

#if XCONSOLE_SHELL_CAN_RECEIVE_ON
static int xcc_receive(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    uint32_t count = 1u;
    int32_t timeout = 1000;
    int i = 2;
    uint32_t received = 0u;
    (void)userData;
    if (!shell || !session || argc < 2 ||
        !xcc_parse_channel(argv[0], argv[1], &channel)) return XConsoleResult_InvalidArgument;
    while (i < argc) {
        if (i + 1 >= argc) return XConsoleResult_InvalidArgument;
        if (strcmp(argv[i], "--count") == 0) {
            if (!xcc_parse_u32(argv[i + 1], &count) || count == 0u ||
                count > XCONSOLE_SHELL_CAN_MAX_RECEIVE_COUNT) return XConsoleResult_InvalidArgument;
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (!xcc_parse_timeout(argv[i + 1], &timeout)) return XConsoleResult_InvalidArgument;
        } else {
            return XConsoleResult_InvalidArgument;
        }
        i += 2;
    }
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "接收");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Receive, false)) return xcc_denied(shell);
    while (received < count) {
        XCanFrame frame = XCAN_FRAME_INIT;
        XCanIoResult result = XCan_receive(slot->can, &frame, timeout);
        if (result == XCanIoResult_Success) {
            xcc_print_frame(shell, &frame);
            ++received;
        } else if (result == XCanIoResult_Timeout || result == XCanIoResult_WouldBlock) {
            return received ? XConsoleResult_Ok : xcc_error(shell, "接收", slot->can);
        } else {
            return xcc_error(shell, "接收", slot->can);
        }
    }
    return XConsoleResult_Ok;
}
#endif

#if XCONSOLE_SHELL_CAN_RECOVER_ON
static int xcc_recover(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 || !xcc_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "恢复");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Recover, true)) return xcc_denied(shell);
    if (!XCan_recoverBus(slot->can)) return xcc_error(shell, "恢复", slot->can);
    return XConsoleShell_writeUtf8(shell, "can: 恢复: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}
#endif

#if XCONSOLE_SHELL_CAN_FILTER_ON
static bool xcc_filter_type(const char* text, XCanFilterFrameType* type)
{
    if (!text || !type) return false;
    if (strcmp(text, "all") == 0) *type = XCanFilterFrameType_All;
    else if (strcmp(text, "data") == 0) *type = XCanFilterFrameType_Data;
    else if (strcmp(text, "remote") == 0) *type = XCanFilterFrameType_Remote;
    else if (strcmp(text, "error") == 0) *type = XCanFilterFrameType_Error;
    else return false;
    return true;
}

static int xcc_filter_add(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    XCanFilter filter = XCAN_FILTER_INIT;
    uint32_t filterId = XCAN_INVALID_FILTER_ID;
    int i = 4;
    char line[96];
    int written;
    (void)userData;
    if (!shell || !session || argc < 4 ||
        !xcc_parse_channel(argv[0], argv[1], &channel) ||
        !xcc_parse_u32(argv[2], &filter.m_id) ||
        !xcc_parse_u32(argv[3], &filter.m_mask)) return XConsoleResult_InvalidArgument;
    while (i < argc) {
        if (strcmp(argv[i], "--extended") == 0) {
            filter.m_idFormat = XCanIdFormat_Extended;
            ++i;
        } else if (strcmp(argv[i], "--invert") == 0) {
            filter.m_flags |= XCanFilterFlag_Invert;
            ++i;
        } else if (strcmp(argv[i], "--type") == 0) {
            if (i + 1 >= argc || !xcc_filter_type(argv[i + 1], &filter.m_frameType))
                return XConsoleResult_InvalidArgument;
            i += 2;
        } else {
            return XConsoleResult_InvalidArgument;
        }
    }
    if (filter.m_idFormat == XCanIdFormat_Standard) {
        if (filter.m_id > XCAN_STANDARD_ID_MAX || filter.m_mask > XCAN_STANDARD_ID_MAX)
            return XConsoleResult_InvalidArgument;
    } else if (filter.m_id > XCAN_EXTENDED_ID_MAX || filter.m_mask > XCAN_EXTENDED_ID_MAX) {
        return XConsoleResult_InvalidArgument;
    }
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "添加过滤器");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Filter, true)) return xcc_denied(shell);
    if (!XCan_addFilter(slot->can, &filter, &filterId)) return xcc_error(shell, "添加过滤器", slot->can);
    written = snprintf(line, sizeof(line), "can: 过滤器添加: id=%u\n", filterId);
    return written > 0 && (size_t)written < sizeof(line) &&
                   XConsoleShell_writeUtf8(shell, line)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcc_filter_remove(XConsoleShell* shell, XConsoleShellSession* session,
                             int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    uint32_t filterId;
    (void)userData;
    if (!shell || !session || argc != 3 ||
        !xcc_parse_channel(argv[0], argv[1], &channel) ||
        !xcc_parse_u32(argv[2], &filterId)) return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "删除过滤器");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Filter, true)) return xcc_denied(shell);
    if (!XCan_removeFilter(slot->can, filterId)) return xcc_error(shell, "删除过滤器", slot->can);
    return XConsoleShell_writeUtf8(shell, "can: 过滤器移除: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcc_filter_clear(XConsoleShell* shell, XConsoleShellSession* session,
                            int argc, const char* const* argv, void* userData)
{
    XCanChannel channel;
    XConsoleShellCanSlot* slot;
    (void)userData;
    if (!shell || !session || argc != 2 || !xcc_parse_channel(argv[0], argv[1], &channel))
        return XConsoleResult_InvalidArgument;
    slot = xcc_find_slot(shell, &channel);
    if (!slot) return xcc_missing(shell, "清空过滤器");
    if (!xcc_authorized(shell, session, &channel,
                        XConsoleShellCanOperation_Filter, true)) return xcc_denied(shell);
    XCan_clearFilters(slot->can);
    return XConsoleShell_writeUtf8(shell, "can: 过滤器清空: 成功\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static const XConsoleCommand g_canFilterCommands[] = {
    { "add", NULL, "添加 CAN 接收过滤器", "can filter add <controller> <channel> <id> <mask> [options]", 4, -1,
      XConsoleCommandFlag_Dangerous, xcc_filter_add, NULL, 0, NULL },
    { "remove", NULL, "删除 CAN 接收过滤器", "can filter remove <controller> <channel> <filter-id>", 3, 3,
      XConsoleCommandFlag_Dangerous, xcc_filter_remove, NULL, 0, NULL },
    { "clear", NULL, "清除 CAN 接收过滤器", "can filter clear <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcc_filter_clear, NULL, 0, NULL }
};
#endif

bool XConsoleShell_setCanAuthorizeCallback(
    XConsoleShell* self, XConsoleShellCanAuthorizeFn authorize,
    void* userData)
{
    if (!self) return false;
    self->m_canAuthorize = authorize;
    self->m_canAuthorizeUserData = authorize ? userData : NULL;
    return true;
}

void XConsoleShellCan_deinit(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0u; i < XCONSOLE_SHELL_CAN_SLOT_CAPACITY; ++i) {
        if (shell->m_canSlots[i].can) XCan_delete(shell->m_canSlots[i].can);
        memset(&shell->m_canSlots[i], 0, sizeof(shell->m_canSlots[i]));
    }
    shell->m_canAuthorize = NULL;
    shell->m_canAuthorizeUserData = NULL;
}

#define XCS_CAN_HAS_COMMANDS (XCONSOLE_SHELL_CAN_LIST_ON || \
    XCONSOLE_SHELL_CAN_OPEN_ON || XCONSOLE_SHELL_CAN_CLOSE_ON || \
    XCONSOLE_SHELL_CAN_INFO_ON || XCONSOLE_SHELL_CAN_STATUS_ON || \
    XCONSOLE_SHELL_CAN_START_ON || XCONSOLE_SHELL_CAN_STOP_ON || \
    XCONSOLE_SHELL_CAN_SEND_ON || XCONSOLE_SHELL_CAN_RECEIVE_ON || \
    XCONSOLE_SHELL_CAN_RECOVER_ON || XCONSOLE_SHELL_CAN_FILTER_ON)

#if XCS_CAN_HAS_COMMANDS
static const XConsoleCommand g_canCommands[] = {
#if XCONSOLE_SHELL_CAN_LIST_ON
    { "list", NULL, "列出已打开 CAN", "can list", 0, 0,
      XConsoleCommandFlag_None, xcc_list, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_OPEN_ON
    { "open", NULL, "打开 CAN 控制器", "can open <controller> <channel> [options]", 2, -1,
      XConsoleCommandFlag_Dangerous, xcc_open, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_CLOSE_ON
    { "close", NULL, "关闭 CAN 控制器", "can close <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcc_close, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_INFO_ON
    { "info", NULL, "显示 CAN 配置和能力", "can info <controller> <channel>", 2, 2,
      XConsoleCommandFlag_None, xcc_info, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_STATUS_ON
    { "status", NULL, "显示 CAN 总线状态", "can status <controller> <channel>", 2, 2,
      XConsoleCommandFlag_None, xcc_status, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_START_ON
    { "start", NULL, "启动 CAN 控制器", "can start <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcc_start, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_STOP_ON
    { "stop", NULL, "停止 CAN 控制器", "can stop <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcc_stop, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_SEND_ON
    { "send", NULL, "发送 CAN 帧", "can send <controller> <channel> <id> [data...] [options]", 3, -1,
      XConsoleCommandFlag_Dangerous, xcc_send, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_RECEIVE_ON
    { "receive", NULL, "接收 CAN 帧", "can receive <controller> <channel> [--count N] [--timeout MS]", 2, -1,
      XConsoleCommandFlag_None, xcc_receive, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_RECOVER_ON
    { "recover", NULL, "恢复 Bus Off", "can recover <controller> <channel>", 2, 2,
      XConsoleCommandFlag_Dangerous, xcc_recover, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_CAN_FILTER_ON
    { "filter", NULL, "管理 CAN 接收过滤器", "can filter <add|remove|clear>", 0, -1,
      XConsoleCommandFlag_Dangerous, NULL, g_canFilterCommands,
      sizeof(g_canFilterCommands) / sizeof(g_canFilterCommands[0]), NULL },
#endif
};
#else
static const XConsoleCommand g_canCommands[1] = { { NULL } };
#endif

const XConsoleCommand XConsoleShellCan_command = {
    "can", NULL, "CAN 控制器配置、收发和状态诊断", "can <subcommand>",
    0, -1, XConsoleCommandFlag_None, NULL, g_canCommands,
    XCS_CAN_HAS_COMMANDS ? sizeof(g_canCommands) / sizeof(g_canCommands[0]) : 0u,
    NULL
};

#endif /* Shell、命令、I/O 和 CAN 均启用 */
