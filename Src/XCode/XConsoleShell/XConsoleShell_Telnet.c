/**
 * @file XConsoleShell_Telnet.c
 * @brief XConsoleShell Telnet 协商与文本过滤实现。
 */

#include "XConsoleShell_Telnet.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_TELNET_PROTOCOL_ON

#include <string.h>

enum {
    XCS_TELNET_IAC = 255,
    XCS_TELNET_DONT = 254,
    XCS_TELNET_DO = 253,
    XCS_TELNET_WONT = 252,
    XCS_TELNET_WILL = 251,
    XCS_TELNET_SB = 250,
    XCS_TELNET_SE = 240,
    XCS_TELNET_IP = 244
};

static bool xcs_telnet_write_all(XConsoleShellTelnetAdapter* adapter,
                                 const uint8_t* data, size_t size)
{
    size_t offset = 0;
    if (!adapter || (!data && size) || !adapter->transport.write) return false;
    while (offset < size) {
        int64_t written = adapter->transport.write(adapter->transport.userData,
                                                   data + offset, size - offset);
        if (written <= 0 || (size_t)written > size - offset) return false;
        offset += (size_t)written;
    }
    return true;
}

static bool xcs_telnet_reply(XConsoleShellTelnetAdapter* adapter,
                             uint8_t command, uint8_t option)
{
    const uint8_t bytes[3] = { XCS_TELNET_IAC, command, option };
    return xcs_telnet_write_all(adapter, bytes, sizeof(bytes));
}

static int64_t xcs_telnet_write(void* userData, const void* data, size_t size)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    const uint8_t* bytes = (const uint8_t*)data;
    size_t begin = 0;
    size_t i;
    if (!adapter || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != XCS_TELNET_IAC) continue;
        if (i > begin && !xcs_telnet_write_all(adapter, bytes + begin, i - begin)) return -1;
        {
            const uint8_t escapedIac[2] = { XCS_TELNET_IAC, XCS_TELNET_IAC };
            if (!xcs_telnet_write_all(adapter, escapedIac, sizeof(escapedIac))) return -1;
        }
        begin = i + 1u;
    }
    if (begin < size && !xcs_telnet_write_all(adapter, bytes + begin, size - begin)) return -1;
    return (int64_t)size;
}

static bool xcs_telnet_flush(void* userData)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    return adapter && (!adapter->transport.flush ||
                       adapter->transport.flush(adapter->transport.userData));
}

static bool xcs_telnet_cancelled(void* userData)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    return adapter && adapter->transport.cancelled &&
           adapter->transport.cancelled(adapter->transport.userData);
}

#if XCONSOLE_SHELL_LOG_ON
static void xcs_telnet_log(void* userData, const XConsoleShellSession* session,
                           const void* data, size_t size)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    if (adapter && adapter->transport.log)
        adapter->transport.log(adapter->transport.userData, session, data, size);
}
#endif

#if XCONSOLE_SHELL_AUDIT_ON
static void xcs_telnet_audit(void* userData, const XConsoleShellSession* session,
                             const XConsoleCommand* command, int result)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    if (adapter && adapter->transport.audit)
        adapter->transport.audit(adapter->transport.userData, session, command, result);
}
#endif

static XConsoleResult xcs_telnet_feed_byte(XConsoleShell* shell,
                                           XConsoleShellSession* session,
                                           uint8_t byte)
{
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    if (session) return XConsoleShell_feedByteForSession(shell, session, byte);
#else
    (void)session;
#endif
    return XConsoleShell_feedByte(shell, byte);
}

void XConsoleShellTelnetAdapter_init(XConsoleShellTelnetAdapter* adapter,
                                     const XConsoleShellIo* transport)
{
    if (!adapter) return;
    memset(adapter, 0, sizeof(*adapter));
    if (transport) adapter->transport = *transport;
    adapter->state = XConsoleShellTelnetState_Data;
}

bool XConsoleShellTelnetAdapter_makeIo(XConsoleShellTelnetAdapter* adapter,
                                       XConsoleShellIo* io)
{
    if (!adapter || !io || !adapter->transport.write) return false;
    memset(io, 0, sizeof(*io));
    io->write = xcs_telnet_write;
    io->flush = xcs_telnet_flush;
    io->cancelled = xcs_telnet_cancelled;
#if XCONSOLE_SHELL_LOG_ON
    io->log = xcs_telnet_log;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    io->audit = xcs_telnet_audit;
#endif
    io->userData = adapter;
    return true;
}

XConsoleResult XConsoleShellTelnetAdapter_feedData(XConsoleShellTelnetAdapter* adapter,
                                                   XConsoleShell* shell,
                                                   XConsoleShellSession* session,
                                                   const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    XConsoleResult result = XConsoleResult_Ok;
    size_t i;
    if (!adapter || !shell || (!data && size)) return XConsoleResult_InvalidArgument;
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (adapter->state == XConsoleShellTelnetState_Subnegotiation) {
            if (byte == XCS_TELNET_IAC) adapter->state = XConsoleShellTelnetState_SubnegotiationIac;
            continue;
        }
        if (adapter->state == XConsoleShellTelnetState_SubnegotiationIac) {
            adapter->state = byte == XCS_TELNET_SE ? XConsoleShellTelnetState_Data :
                                                       XConsoleShellTelnetState_Subnegotiation;
            continue;
        }
        if (adapter->state == XConsoleShellTelnetState_Option) {
            if (adapter->negotiation == XCS_TELNET_DO &&
                !xcs_telnet_reply(adapter, XCS_TELNET_WONT, byte))
                return XConsoleResult_IoError;
            if (adapter->negotiation == XCS_TELNET_WILL &&
                !xcs_telnet_reply(adapter, XCS_TELNET_DONT, byte))
                return XConsoleResult_IoError;
            adapter->state = XConsoleShellTelnetState_Data;
            continue;
        }
        if (adapter->state == XConsoleShellTelnetState_Iac) {
            if (byte == XCS_TELNET_IAC) {
                adapter->state = XConsoleShellTelnetState_Data;
                result = xcs_telnet_feed_byte(shell, session, byte);
                continue;
            }
            if (byte == XCS_TELNET_DO || byte == XCS_TELNET_DONT ||
                byte == XCS_TELNET_WILL || byte == XCS_TELNET_WONT) {
                adapter->negotiation = byte;
                adapter->state = XConsoleShellTelnetState_Option;
                continue;
            }
            if (byte == XCS_TELNET_SB) {
                adapter->state = XConsoleShellTelnetState_Subnegotiation;
                continue;
            }
            adapter->state = XConsoleShellTelnetState_Data;
            if (byte == XCS_TELNET_IP)
                result = xcs_telnet_feed_byte(shell, session, 0x03);
            continue;
        }
        if (byte == XCS_TELNET_IAC) {
            adapter->state = XConsoleShellTelnetState_Iac;
            continue;
        }
        if (adapter->afterCarriageReturn && (byte == 0 || byte == '\n')) {
            adapter->afterCarriageReturn = false;
            continue;
        }
        adapter->afterCarriageReturn = byte == '\r';
        result = xcs_telnet_feed_byte(shell, session, byte);
    }
    return result;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_TELNET_PROTOCOL_ON */
