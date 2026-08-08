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

static int xcsn_ping(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    unsigned count = 1;
    unsigned timeout = 1000;
    unsigned i;
    unsigned received = 0;
    unsigned total = 0;
    unsigned minimum = UINT_MAX;
    unsigned maximum = 0;
    uint64_t sum = 0;
    XHostInfo* info = NULL;
    const XVector* addresses;
    const XHostAddress* target = NULL;
    XString* targetText = NULL;
    static const uint8_t payload[] = "XinYueC-XConsoleShell-ICMP";
    (void)session;
    (void)userData;
    if (!shell || argc < 1 || argc > 5 || !argv[0] || !argv[0][0])
        return XConsoleResult_InvalidArgument;
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
    if (!targetText || !xcsn_write_line(shell, XString_toUtf8(targetText))) {
        if (targetText) XString_delete_base(targetText);
        XHostInfo_delete_base(info);
        return XConsoleResult_IoError;
    }
    XString_delete_base(targetText);
    for (i = 0; i < count; ++i) {
        uint32_t elapsed = 0;
        bool ok = XNetwork_icmpEcho(target, 0x5859u, (uint16_t)(i + 1u), payload,
                                    sizeof(payload) - 1u, (int)timeout, &elapsed);
        char line[128];
        ++total;
        if (ok) {
            ++received;
            if (elapsed < minimum) minimum = elapsed;
            if (elapsed > maximum) maximum = elapsed;
            sum += elapsed;
            snprintf(line, sizeof(line), "reply seq=%u time=%u ms", i + 1u, elapsed);
        } else {
            snprintf(line, sizeof(line), "timeout seq=%u", i + 1u);
        }
        if (!xcsn_write_line(shell, line)) {
            XHostInfo_delete_base(info);
            return XConsoleResult_IoError;
        }
    }
    {
        char summary[160];
        snprintf(summary, sizeof(summary), "%u packets transmitted, %u received, %u%% loss",
                 total, received, total ? ((total - received) * 100u / total) : 100u);
        if (!xcsn_write_line(shell, summary)) {
            XHostInfo_delete_base(info);
            return XConsoleResult_IoError;
        }
        if (received) {
            snprintf(summary, sizeof(summary), "rtt min/avg/max = %u/%.1f/%u ms",
                     minimum, (double)sum / received, maximum);
            if (!xcsn_write_line(shell, summary)) {
                XHostInfo_delete_base(info);
                return XConsoleResult_IoError;
            }
        }
    }
    XHostInfo_delete_base(info);
    return received ? XConsoleResult_Ok : XConsoleResult_Failed;
}


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
    { "ping", NULL, "发送 ICMP Echo 请求", "net ping <host> [-c count] [-W timeout]", 1, 5, 0, xcsn_ping, NULL, 0, NULL },
#endif
    { NULL, NULL, NULL, NULL, 0, 0, 0, NULL, NULL, 0, NULL }
};

const XConsoleCommand XConsoleShellNetwork_command = {
    "net", NULL, "网络状态、地址解析和 ICMP 探测",
    "net <ifconfig|hostname|resolve|ping>", 1, -1, 0, NULL,
    g_networkCommands, sizeof(g_networkCommands) / sizeof(g_networkCommands[0]) - 1u, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_NETWORK_ON */
