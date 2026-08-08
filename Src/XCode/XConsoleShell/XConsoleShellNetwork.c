/**
 * @file XConsoleShellNetwork.c
 * @brief XConsoleShell 内建网络命令实现。
 * @details
 * 所有操作只使用 XNetwork、XNetworkInterface 和 XHostInfo 公共 API，不直接
 * 调用套接字、DNS、网卡枚举或计时平台接口。输出采用固定列宽和统一缩进，
 * 适合 UART、USB CDC、RTT 以及普通终端查看。命令本身是同步的，长时间 DNS
 * 或 ICMP 等待由底层网络后端的超时参数约束。
 */

#include "XConsoleShell_Protected.h"
#if !XCONSOLE_SHELL_ASYNC_ON
#include "XThread.h"
#endif

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_NETWORK_ON

#include "XConsoleShellNetwork.h"
#include "XNetwork.h"
#include "XNetworkInterface.h"
#include "XNetworkAddressEntry.h"
#include "XHostInfo.h"
#include "XHostAddress.h"
#include "XString.h"
#include "XVector.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool xcsn_write_line(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text) &&
           XConsoleShell_writeUtf8(shell, "\n");
}

static bool xcsn_append_flag(char* output, size_t capacity, const char* flag,
                             bool* first)
{
    size_t length;
    int written;
    if (!output || !flag || !first) return false;
    length = strlen(output);
    written = snprintf(output + length, capacity > length ? capacity - length : 0,
                       "%s%s", *first ? "" : ",", flag);
    if (written < 0 || (size_t)written >= (capacity > length ? capacity - length : 0))
        return false;
    *first = false;
    return true;
}

static bool xcsn_interface_flags(const XNetworkInterface* iface,
                                 char* output, size_t capacity)
{
    bool first = true;
    if (!output || capacity == 0) return false;
    output[0] = '\0';
    if (XNetworkInterface_isUp(iface) && !xcsn_append_flag(output, capacity, "up", &first))
        return false;
    if (XNetworkInterface_isRunning(iface) &&
        !xcsn_append_flag(output, capacity, "running", &first)) return false;
    if (XNetworkInterface_canBroadcast(iface) &&
        !xcsn_append_flag(output, capacity, "broadcast", &first)) return false;
    if (XNetworkInterface_isLoopBack(iface) &&
        !xcsn_append_flag(output, capacity, "loopback", &first)) return false;
    if (XNetworkInterface_isPointToPoint(iface) &&
        !xcsn_append_flag(output, capacity, "point-to-point", &first)) return false;
    if (XNetworkInterface_canMulticast(iface) &&
        !xcsn_append_flag(output, capacity, "multicast", &first)) return false;
    if (first && !xcsn_append_flag(output, capacity, "none", &first)) return false;
    return true;
}

static int xcsn_write_interface(XConsoleShell* shell, const XNetworkInterface* iface)
{
    const XString* name;
    const XString* hardware;
    XVector* entries;
    char flags[128];
    char line[256];
    size_t i;
    if (!shell || !iface || !XNetworkInterface_isValid(iface))
        return XConsoleResult_Failed;
    name = XNetworkInterface_name_const(iface);
    hardware = XNetworkInterface_hardwareAddress_const(iface);
    if (!name || !xcsn_interface_flags(iface, flags, sizeof(flags)))
        return XConsoleResult_Failed;
    if (snprintf(line, sizeof(line), "%-12s flags=%-38s mtu=%-5d index=%d",
                 XString_toUtf8(name), flags,
                 XNetworkInterface_maximumTransmissionUnit(iface),
                 XNetworkInterface_index(iface)) < 0 ||
        !xcsn_write_line(shell, line))
        return XConsoleResult_IoError;
    if (hardware && XString_size_base(hardware) > 0) {
        if (snprintf(line, sizeof(line), "    hwaddr %s", XString_toUtf8(hardware)) < 0 ||
            !xcsn_write_line(shell, line))
            return XConsoleResult_IoError;
    }
    entries = XNetworkInterface_addressEntries(iface);
    for (i = 0; entries && i < XVector_size_base(entries); ++i) {
        XNetworkAddressEntry* entry = (XNetworkAddressEntry*)XVector_at_base(entries, i);
        const XHostAddress* address = entry ? XNetworkAddressEntry_ip(entry) : NULL;
        XString* addressText = address ? XHostAddress_toString(address) : NULL;
        int prefix = entry ? XNetworkAddressEntry_prefixLength(entry) : -1;
        int written;
        if (!addressText) continue;
        if (prefix >= 0) {
            char addressWithPrefix[192];
            int addressWritten = snprintf(addressWithPrefix, sizeof(addressWithPrefix),
                                          "%s/%d", XString_toUtf8(addressText), prefix);
            written = addressWritten > 0 && (size_t)addressWritten < sizeof(addressWithPrefix)
                          ? snprintf(line, sizeof(line), "    inet  %s", addressWithPrefix)
                          : -1;
        } else {
            written = snprintf(line, sizeof(line), "    inet  %s",
                               XString_toUtf8(addressText));
        }
        XString_delete_base(addressText);
        if (written < 0 || (size_t)written >= sizeof(line) ||
            !xcsn_write_line(shell, line))
            return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

static int xcsn_ifconfig(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XVector* interfaces = NULL;
    XNetworkInterface* selected = NULL;
    const char* selectedName = NULL;
    int result = XConsoleResult_Ok;
    int i;
    (void)session;
    (void)userData;
    if (!shell) return XConsoleResult_InvalidArgument;
    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--all") == 0) continue;
        if (argv[i][0] == '-') return XConsoleResult_InvalidArgument;
        if (selectedName) return XConsoleResult_InvalidArgument;
        selectedName = argv[i];
    }
    if (selectedName) {
        XString* name = XString_create_utf8(selectedName);
        if (!name) return XConsoleResult_Failed;
        selected = XNetworkInterface_interfaceFromName(name);
        XString_delete_base(name);
        if (!selected) return XConsoleResult_Failed;
    }
    if (selected) {
        result = xcsn_write_interface(shell, selected);
        XNetworkInterface_delete_base(selected);
        return result;
    }
    interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) return XConsoleResult_Failed;
    for (i = 0; (size_t)i < XVector_size_base(interfaces); ++i) {
        XNetworkInterface* iface =
            (XNetworkInterface*)XVector_at_base(interfaces, i);
        result = xcsn_write_interface(shell, iface);
        if (result < 0) break;
    }
    XVector_delete_base(interfaces);
    return result;
}

static int xcsn_hostname(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XString* name;
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    name = XHostInfo_localHostName();
    if (!name) return XConsoleResult_Failed;
    if (!xcsn_write_line(shell, XString_toUtf8(name))) {
        XString_delete_base(name);
        return XConsoleResult_IoError;
    }
    XString_delete_base(name);
    return XConsoleResult_Ok;
}

static int xcsn_resolve(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    XHostInfo* info;
    const XVector* addresses;
    size_t i;
    char line[256];
    (void)session;
    (void)userData;
    if (!shell || argc != 1 || !argv[0] || !argv[0][0])
        return XConsoleResult_InvalidArgument;
    info = XHostInfo_fromName2(argv[0]);
    if (!info) return XConsoleResult_Failed;
    addresses = XHostInfo_addresses_const(info);
    if (!addresses) {
        const XString* error = XHostInfo_errorString(info);
        if (error) xcsn_write_line(shell, XString_toUtf8(error));
        XHostInfo_delete_base(info);
        return XConsoleResult_Failed;
    }
    for (i = 0; i < XVector_size_base(addresses); ++i) {
        const XHostAddress* address =
            (const XHostAddress*)XVector_at_base(addresses, i);
        XString* text = address ? XHostAddress_toString(address) : NULL;
        int written;
        if (!text) continue;
        written = snprintf(line, sizeof(line), "%-32s %s", argv[0], XString_toUtf8(text));
        XString_delete_base(text);
        if (written < 0 || (size_t)written >= sizeof(line) || !xcsn_write_line(shell, line)) {
            XHostInfo_delete_base(info);
            return XConsoleResult_IoError;
        }
    }
    XHostInfo_delete_base(info);
    return XConsoleResult_Ok;
}

static bool xcsn_parse_unsigned(const char* text, unsigned* value,
                                unsigned minimum, unsigned maximum)
{
    unsigned result = 0;
    size_t i;
    if (!text || !text[0] || !value) return false;
    for (i = 0; text[i]; ++i) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') return false;
        digit = (unsigned)(text[i] - '0');
        if (result > (maximum - digit) / 10u) return false;
        result = result * 10u + digit;
    }
    if (result < minimum || result > maximum) return false;
    *value = result;
    return true;
}

#if XCONSOLE_SHELL_ASYNC_ON
static bool xcsn_ping_send_one(XConsoleShell* shell);
static int xcsn_ping_write_summary(XConsoleShell* shell);
static void xcsn_ping_cleanup(XConsoleShell* shell);
#endif

static int xcsn_ping(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    unsigned count = 4;
    unsigned timeout = 1000;
    unsigned i;
    XHostInfo* info = NULL;
    const XVector* addresses;
    const XHostAddress* target = NULL;
    XString* targetText = NULL;
    (void)session;
    (void)userData;
    if (!shell || argc < 1 || argc > 5 || !argv[0] || !argv[0][0])
        return XConsoleResult_InvalidArgument;

#if XCONSOLE_SHELL_ASYNC_ON
    if (shell->m_ping.active) {
        xcsn_write_line(shell, "ping: 已有 ping 正在运行");
        return XConsoleResult_Failed;
    }
#endif
    if (!XNetwork_icmpEchoSupported()) {
        xcsn_write_line(shell, "ping: 当前网络后端未启用 ICMP Echo");
        return XConsoleResult_NotSupported;
    }
    for (i = 1; i < (unsigned)argc; ++i) {
        const char* value = NULL;
        if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0) && i + 1u < (unsigned)argc)
            value = argv[++i], (void)0;
        else if ((strcmp(argv[i], "-W") == 0 || strcmp(argv[i], "--timeout") == 0) && i + 1u < (unsigned)argc)
            value = argv[++i], (void)0;
        else
            return XConsoleResult_InvalidArgument;
        if (strcmp(argv[i - 1u], "-c") == 0 || strcmp(argv[i - 1u], "--count") == 0) {
            if (!xcsn_parse_unsigned(value, &count, 1u, 10u)) return XConsoleResult_InvalidArgument;
        } else if (!xcsn_parse_unsigned(value, &timeout, 1u, 10000u)) {
            return XConsoleResult_InvalidArgument;
        }
    }
    info = XHostInfo_fromName2(argv[0]);
    if (!info) {
        xcsn_write_line(shell, "ping: 主机名解析失败");
        return XConsoleResult_Failed;
    }
    addresses = XHostInfo_addresses_const(info);
    if (addresses) {
        for (i = 0; i < XVector_size_base(addresses); ++i) {
            const XHostAddress* candidate =
                (const XHostAddress*)XVector_at_base(addresses, i);
            if (candidate && XHostAddress_protocol(candidate) == XHostAddress_IPv4Protocol) {
                target = candidate;
                break;
            }
        }
    }
    if (!target) {
        xcsn_write_line(shell, "ping: 没有可用的 IPv4 地址");
        XHostInfo_delete_base(info);
        return XConsoleResult_Failed;
    }
    targetText = XHostAddress_toString(target);
    if (!targetText) {
        XHostInfo_delete_base(info);
        return XConsoleResult_IoError;
    }
    {
        char header[192];
        snprintf(header, sizeof(header), "\xe6\xad\xa3\xe5\x9c\xa8 Ping %s \xe5\x85\xb7\xe6\x9c\x89 32 \xe5\xad\x97\xe8\x8a\x82\xe7\x9a\x84\xe6\x95\xb0\xe6\x8d\xae:",
                 XString_toUtf8(targetText));
        if (!xcsn_write_line(shell, header)) {
            XString_delete_base(targetText);
            XHostInfo_delete_base(info);
            return XConsoleResult_IoError;
        }
    }
#if XCONSOLE_SHELL_ASYNC_ON
    memset(&shell->m_ping, 0, sizeof(shell->m_ping));
    shell->m_pingTimer = XTIMER_INVALID_ID;
    shell->m_ping.info = info;
    shell->m_ping.target = target;
    shell->m_ping.targetText = targetText;
    shell->m_ping.count = count;
    shell->m_ping.timeout = timeout;
    shell->m_ping.minimum = UINT_MAX;
    shell->m_ping.active = true;
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    shell->m_session.suppressPrompt = true;
#endif
    if (!xcsn_ping_send_one(shell)) {
        xcsn_ping_cleanup(shell);
        return XConsoleResult_IoError;
    }
    if (count > 1u) {
        shell->m_pingTimer = XObject_startTimer_ms((XObject*)shell, 1000u,
                                                   XTimerType_CoarseTimer);
        if (shell->m_pingTimer == XTIMER_INVALID_ID) {
            (void)xcsn_ping_write_summary(shell);
            {
                int r = shell->m_ping.received ? XConsoleResult_Ok : XConsoleResult_Failed;
                xcsn_ping_cleanup(shell);
                return r;
            }
        }
        return XConsoleResult_MoreOutput;
    }
    (void)xcsn_ping_write_summary(shell);
    {
        int r = shell->m_ping.received ? XConsoleResult_Ok : XConsoleResult_Failed;
        xcsn_ping_cleanup(shell);
        return r;
    }
#else
    {
        unsigned total = 0;
        unsigned received = 0;
        unsigned minimum = UINT_MAX;
        unsigned maximum = 0;
        uint64_t sum = 0;
        static const uint8_t payload[] = "XinYueC-XConsoleShell-ICMP";
        for (i = 0; i < count; ++i) {
            uint32_t elapsed = 0;
            bool ok = XNetwork_icmpEcho(target, 0x5859u, (uint16_t)(i + 1u), payload,
                                        sizeof(payload) - 1u, (int)timeout, &elapsed);
            char line[192];
            ++total;
            if (ok) {
                ++received;
                if (elapsed < minimum) minimum = elapsed;
                if (elapsed > maximum) maximum = elapsed;
                sum += elapsed;
                if (elapsed < 1u)
                    snprintf(line, sizeof(line), "\xe6\x9d\xa5\xe8\x87\xaa %s \xe7\x9a\x84\xe5\x9b\x9e\xe5\xa4\x8d: \xe5\xad\x97\xe8\x8a\x82=32 \xe6\x97\xb6\xe9\x97\xb4<1ms",
                             XString_toUtf8(targetText));
                else
                    snprintf(line, sizeof(line), "\xe6\x9d\xa5\xe8\x87\xaa %s \xe7\x9a\x84\xe5\x9b\x9e\xe5\xa4\x8d: \xe5\xad\x97\xe8\x8a\x82=32 \xe6\x97\xb6\xe9\x97\xb4=%ums",
                             XString_toUtf8(targetText), elapsed);
            } else {
                snprintf(line, sizeof(line), "\xe8\xaf\xb7\xe6\xb1\x82\xe8\xb6\x85\xe6\x97\xb6\xe3\x80\x82");
            }
            if (!xcsn_write_line(shell, line)) {
                XString_delete_base(targetText);
                XHostInfo_delete_base(info);
                return XConsoleResult_IoError;
            }
            if (i + 1u < count) XThread_msleep(1000);
        }
        if (!xcsn_write_line(shell, "")) {
            XString_delete_base(targetText);
            XHostInfo_delete_base(info);
            return XConsoleResult_IoError;
        }
        {
            char summary[256];
            unsigned lossPercent = total ? ((total - received) * 100u / total) : 100u;
            snprintf(summary, sizeof(summary), "%s \xe7\x9a\x84 Ping \xe7\xbb\x9f\xe8\xae\xa1\xe4\xbf\xa1\xe6\x81\xaf:",
                     XString_toUtf8(targetText));
            if (!xcsn_write_line(shell, summary)) {
                XString_delete_base(targetText);
                XHostInfo_delete_base(info);
                return XConsoleResult_IoError;
            }
            snprintf(summary, sizeof(summary), "    \xe6\x95\xb0\xe6\x8d\xae\xe5\x8c\x85: \xe5\xb7\xb2\xe5\x8f\x91\xe9\x80\x81 = %u\xef\xbc\x8c\xe5\xb7\xb2\xe6\x8e\xa5\xe6\x94\xb6 = %u\xef\xbc\x8c\xe4\xb8\xa2\xe5\xa4\xb1 = %u (%u%% \xe4\xb8\xa2\xe5\xa4\xb1)\xef\xbc\x8c",
                     total, received, total - received, lossPercent);
            if (!xcsn_write_line(shell, summary)) {
                XString_delete_base(targetText);
                XHostInfo_delete_base(info);
                return XConsoleResult_IoError;
            }
            if (received) {
                snprintf(summary, sizeof(summary), "\xe5\xbe\x80\xe8\xbf\x94\xe8\xa1\x8c\xe7\xa8\x8b\xe7\x9a\x84\xe4\xbc\xb0\xe8\xae\xa1\xe6\x97\xb6\xe9\x97\xb4(\xe4\xbb\xa5\xe6\xaf\xab\xe7\xa7\x92\xe4\xb8\xba\xe5\x8d\x95\xe4\xbd\x8d):");
                if (!xcsn_write_line(shell, summary)) {
                    XString_delete_base(targetText);
                    XHostInfo_delete_base(info);
                    return XConsoleResult_IoError;
                }
                snprintf(summary, sizeof(summary), "    \xe6\x9c\x80\xe7\x9f\xad = %ums\xef\xbc\x8c\xe6\x9c\x80\xe9\x95\xbf = %ums\xef\xbc\x8c\xe5\xb9\xb3\xe5\x9d\x87 = %ums",
                         minimum, maximum, (unsigned)(sum / received));
                if (!xcsn_write_line(shell, summary)) {
                    XString_delete_base(targetText);
                    XHostInfo_delete_base(info);
                    return XConsoleResult_IoError;
                }
            }
        }
    }
    XString_delete_base(targetText);
    XHostInfo_delete_base(info);
    return received ? XConsoleResult_Ok : XConsoleResult_Failed;
#endif
}

#if XCONSOLE_SHELL_ASYNC_ON
static bool xcsn_ping_send_one(XConsoleShell* shell)
{
    XConsoleShellPingState* state;
    uint32_t elapsed = 0;
    bool ok;
    char line[192];
    static const uint8_t payload[] = "XinYueC-XConsoleShell-ICMP";
    if (!shell || !shell->m_ping.active || !shell->m_ping.target)
        return false;
    state = &shell->m_ping;
    ok = XNetwork_icmpEcho(state->target, 0x5859u,
                           (uint16_t)(state->sent + 1u), payload,
                           sizeof(payload) - 1u, (int)state->timeout,
                           &elapsed);
    ++state->sent;
    if (ok) {
        ++state->received;
        if (elapsed < state->minimum) state->minimum = elapsed;
        if (elapsed > state->maximum) state->maximum = elapsed;
        state->sum += elapsed;
        if (elapsed < 1u)
            snprintf(line, sizeof(line), "\xe6\x9d\xa5\xe8\x87\xaa %s \xe7\x9a\x84\xe5\x9b\x9e\xe5\xa4\x8d: \xe5\xad\x97\xe8\x8a\x82=32 \xe6\x97\xb6\xe9\x97\xb4<1ms",
                     XString_toUtf8(state->targetText));
        else
            snprintf(line, sizeof(line), "\xe6\x9d\xa5\xe8\x87\xaa %s \xe7\x9a\x84\xe5\x9b\x9e\xe5\xa4\x8d: \xe5\xad\x97\xe8\x8a\x82=32 \xe6\x97\xb6\xe9\x97\xb4=%ums",
                     XString_toUtf8(state->targetText), elapsed);
    } else {
        snprintf(line, sizeof(line), "\xe8\xaf\xb7\xe6\xb1\x82\xe8\xb6\x85\xe6\x97\xb6\xe3\x80\x82");
    }
    return xcsn_write_line(shell, line);
}

static int xcsn_ping_write_summary(XConsoleShell* shell)
{
    XConsoleShellPingState* state;
    char summary[256];
    unsigned total;
    unsigned received;
    unsigned lossPercent;
    if (!shell || !shell->m_ping.active) return XConsoleResult_Failed;
    state = &shell->m_ping;
    total = state->sent;
    received = state->received;
    lossPercent = total ? ((total - received) * 100u / total) : 100u;
    if (!xcsn_write_line(shell, "")) return XConsoleResult_IoError;
    snprintf(summary, sizeof(summary), "%s \xe7\x9a\x84 Ping \xe7\xbb\x9f\xe8\xae\xa1\xe4\xbf\xa1\xe6\x81\xaf:",
             XString_toUtf8(state->targetText));
    if (!xcsn_write_line(shell, summary)) return XConsoleResult_IoError;
    snprintf(summary, sizeof(summary), "    \xe6\x95\xb0\xe6\x8d\xae\xe5\x8c\x85: \xe5\xb7\xb2\xe5\x8f\x91\xe9\x80\x81 = %u\xef\xbc\x8c\xe5\xb7\xb2\xe6\x8e\xa5\xe6\x94\xb6 = %u\xef\xbc\x8c\xe4\xb8\xa2\xe5\xa4\xb1 = %u (%u%% \xe4\xb8\xa2\xe5\xa4\xb1)\xef\xbc\x8c",
             total, received, total - received, lossPercent);
    if (!xcsn_write_line(shell, summary)) return XConsoleResult_IoError;
    if (received) {
        snprintf(summary, sizeof(summary), "\xe5\xbe\x80\xe8\xbf\x94\xe8\xa1\x8c\xe7\xa8\x8b\xe7\x9a\x84\xe4\xbc\xb0\xe8\xae\xa1\xe6\x97\xb6\xe9\x97\xb4(\xe4\xbb\xa5\xe6\xaf\xab\xe7\xa7\x92\xe4\xb8\xba\xe5\x8d\x95\xe4\xbd\x8d):");
        if (!xcsn_write_line(shell, summary)) return XConsoleResult_IoError;
        snprintf(summary, sizeof(summary), "    \xe6\x9c\x80\xe7\x9f\xad = %ums\xef\xbc\x8c\xe6\x9c\x80\xe9\x95\xbf = %ums\xef\xbc\x8c\xe5\xb9\xb3\xe5\x9d\x87 = %ums",
                 state->minimum, state->maximum, (unsigned)(state->sum / state->received));
        if (!xcsn_write_line(shell, summary)) return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

static void xcsn_ping_cleanup(XConsoleShell* shell)
{
    XConsoleShellPingState* state;
    if (!shell) return;
    state = &shell->m_ping;
    if (shell->m_pingTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)shell, shell->m_pingTimer);
        shell->m_pingTimer = XTIMER_INVALID_ID;
    }
    if (state->targetText) {
        XString_delete_base(state->targetText);
        state->targetText = NULL;
    }
    if (state->info) {
        XHostInfo_delete_base(state->info);
        state->info = NULL;
    }
    state->target = NULL;
    state->active = false;
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    shell->m_session.suppressPrompt = false;
#endif
    memset(state, 0, sizeof(*state));
}

void XConsoleShellNetwork_pingTick(XConsoleShell* shell)
{
    XConsoleShellPingState* state;
    if (!shell || !shell->m_ping.active) return;
    state = &shell->m_ping;
    if (state->sent >= state->count) {
        xcsn_ping_cleanup(shell);
        return;
    }
    if (!xcsn_ping_send_one(shell)) {
        xcsn_ping_cleanup(shell);
        return;
    }
    if (state->sent >= state->count) {
        (void)xcsn_ping_write_summary(shell);
        xcsn_ping_cleanup(shell);
        if (shell->m_io.prompt)
            shell->m_io.prompt(shell->m_io.userData, shell);
    }
}

void XConsoleShellNetwork_cancelPing(XConsoleShell* shell)
{
    xcsn_ping_cleanup(shell);
}
#else
void XConsoleShellNetwork_pingTick(XConsoleShell* shell)
{
    (void)shell;
}

void XConsoleShellNetwork_cancelPing(XConsoleShell* shell)
{
    (void)shell;
}
#endif

static const XConsoleCommand g_networkCommands[] = {
#if XCONSOLE_SHELL_NET_IFCONFIG_ON
    { "ifconfig", NULL, "列出网络接口和地址", "net ifconfig [-a] [interface]", 0, 2, 0, xcsn_ifconfig, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_NET_HOSTNAME_ON
    { "hostname", NULL, "输出本机主机名", "net hostname", 0, 0, 0, xcsn_hostname, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_NET_RESOLVE_ON
    { "resolve", "nslookup", "解析主机名地址", "net resolve <host>", 1, 1, 0, xcsn_resolve, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_NET_PING_ON
    { "ping", NULL, "发送 ICMP Echo 请求，默认 4 次", "net ping <host> [-c count] [-W timeout]", 1, 5, 0, xcsn_ping, NULL, 0, NULL },
#endif
    { NULL, NULL, NULL, NULL, 0, 0, 0, NULL, NULL, 0, NULL }
};

const XConsoleCommand XConsoleShellNetwork_command = {
    "net", NULL, "网络状态、地址解析和 ICMP 探测",
    "net <ifconfig|hostname|resolve|ping>", 1, -1, 0, NULL,
    g_networkCommands, sizeof(g_networkCommands) / sizeof(g_networkCommands[0]) - 1u, NULL
};

#if XCONSOLE_SHELL_NET_PING_ON
const XConsoleCommand XConsoleShellNetwork_ping_command = {
    "ping", NULL, "发送 ICMP Echo 请求，默认 4 次", "ping <host> [-c count] [-W timeout]", 1, 5, 0, xcsn_ping, NULL, 0, NULL
};
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_NETWORK_ON */
