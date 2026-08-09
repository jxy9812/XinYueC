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
    XCS_TELNET_IP = 244,
    XCS_TELNET_OPT_ECHO = 1
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

static int64_t xcs_telnet_write_raw(void* userData, const void* data, size_t size)
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

/* Telnet 文本行以 CRLF 结束；Shell 输出统一使用 LF，发送前补 CR，
 * 否则客户端只换行不回车，各行会向右错位。 */
static int64_t xcs_telnet_write(void* userData, const void* data, size_t size)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    const uint8_t* bytes = (const uint8_t*)data;
    size_t begin = 0;
    size_t i;
    if (!adapter || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (byte == '\r' || byte == '\n') {
            if (i > begin &&
                xcs_telnet_write_raw(adapter, bytes + begin, i - begin) !=
                    (int64_t)(i - begin))
                return -1;
            if (byte == '\r' && i + 1 < size && bytes[i + 1] == '\n') ++i;
            {
                const uint8_t crlf[2] = { '\r', '\n' };
                if (!xcs_telnet_write_all(adapter, crlf, sizeof(crlf))) return -1;
            }
            begin = i + 1u;
        }
    }
    if (begin < size &&
        xcs_telnet_write_raw(adapter, bytes + begin, size - begin) !=
            (int64_t)(size - begin))
        return -1;
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

/* Telnet 客户端接受 DONT ECHO 后关闭本地回显，由服务端负责回显普通输入。
 * 密码输入期间 Shell 通过 inputEcho(false) 关闭回显，避免明文泄露。 */
static bool xcs_telnet_echo_byte(XConsoleShellTelnetAdapter* adapter, uint8_t byte)
{
    const uint8_t* text;
    size_t size;
    if (!adapter || !adapter->echoEnabled) return true;
    if (adapter->echoEscape) {
        if (byte >= '@' && byte <= '~') adapter->echoEscape = false;
        return true;
    }
    if (byte == 0x1b) {
        adapter->echoEscape = true;
        return true;
    }
    if (byte == '\r') {
        text = (const uint8_t*)"\r\n";
        size = 2;
        return xcs_telnet_write_raw(adapter, text, size) == (int64_t)size;
    }
    if (byte == '\n') {
        if (!adapter->echoPendingCr)
            return xcs_telnet_write_raw(adapter, (const uint8_t*)"\r\n", 2) == 2;
        adapter->echoPendingCr = false;
        return true;
    }
    adapter->echoPendingCr = false;
    if (byte == '\b' || byte == 0x7f) {
        text = (const uint8_t*)"\b \b";
        size = 3;
        return xcs_telnet_write_raw(adapter, text, size) == (int64_t)size;
    }
    if (byte == 0x03) {
        text = (const uint8_t*)"^C\r\n";
        size = 4;
        return xcs_telnet_write_raw(adapter, text, size) == (int64_t)size;
    }
    if (byte >= 0x20 || byte == '\t')
        return xcs_telnet_write_raw(adapter, &byte, 1) == 1;
    return true;
}

static bool xcs_telnet_input_echo(void* userData, bool enabled)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    if (!adapter) return false;
    adapter->echoEnabled = enabled;
    return true;
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

bool XConsoleShellTelnetAdapter_emitPrompt(XConsoleShellTelnetAdapter* adapter)
{
    const char* user;
    char prompt[XCONSOLE_SHELL_LOGIN_NAME_SIZE + 2u];
    size_t len;
    if (!adapter) return false;
    user = (adapter->session && adapter->session->authenticated &&
            adapter->session->userName[0])
               ? adapter->session->userName
               : (const char*)XCONSOLE_SHELL_DEFAULT_PROMPT_NAME;
    len = strlen(user);
    if (len >= sizeof(prompt) - 2u)
        len = sizeof(prompt) - 2u;
    memcpy(prompt, user, len);
    prompt[len++] = '>';
    prompt[len++] = ' ';
    return xcs_telnet_write(adapter, (const uint8_t*)prompt, len) == (int64_t)len;
}

static void xcs_telnet_prompt(void* userData, XConsoleShell* shell)
{
    XConsoleShellTelnetAdapter* adapter = (XConsoleShellTelnetAdapter*)userData;
    (void)shell;
    if (adapter) (void)XConsoleShellTelnetAdapter_emitPrompt(adapter);
}

/* Telnet 会话由 XTcpServer 适配器直接投喂字节，不经过默认控制台的异步
 * completedLine 提示路径；因此在每个完整输入行结束后由 Telnet 适配器主动
 * 补发提示符，行为与 SSH 通道一致。 */
static void xcs_telnet_prompt_after_line(XConsoleShellTelnetAdapter* adapter,
                                         XConsoleShell* shell)
{
    if (!adapter || !shell || !adapter->session) return;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    if (adapter->session->m_closeRequested) return;
#endif
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    if (adapter->session->suppressPrompt) return;
#endif
    if (!XConsoleShell_isRunning(shell)) return;
    (void)XConsoleShellTelnetAdapter_emitPrompt(adapter);
}

void XConsoleShellTelnetAdapter_setSession(XConsoleShellTelnetAdapter* adapter,
                                           XConsoleShellSession* session)
{
    if (adapter) adapter->session = session;
}

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
    adapter->echoEnabled = true;
}

bool XConsoleShellTelnetAdapter_makeIo(XConsoleShellTelnetAdapter* adapter,
                                       XConsoleShellIo* io)
{
    if (!adapter || !io || !adapter->transport.write) return false;
    memset(io, 0, sizeof(*io));
    io->write = xcs_telnet_write;
    io->flush = xcs_telnet_flush;
    io->cancelled = xcs_telnet_cancelled;
    io->inputEcho = xcs_telnet_input_echo;
    io->prompt = xcs_telnet_prompt;
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
            if (adapter->negotiation == XCS_TELNET_DO) {
                if (byte == XCS_TELNET_OPT_ECHO) {
                    if (!xcs_telnet_reply(adapter, XCS_TELNET_WILL, byte))
                        return XConsoleResult_IoError;
                } else if (!xcs_telnet_reply(adapter, XCS_TELNET_WONT, byte)) {
                    return XConsoleResult_IoError;
                }
            }
            if (adapter->negotiation == XCS_TELNET_WILL) {
                if (byte == XCS_TELNET_OPT_ECHO) {
                    if (!xcs_telnet_reply(adapter, XCS_TELNET_DONT, byte))
                        return XConsoleResult_IoError;
                } else if (!xcs_telnet_reply(adapter, XCS_TELNET_DONT, byte)) {
                    return XConsoleResult_IoError;
                }
            }
            adapter->state = XConsoleShellTelnetState_Data;
            continue;
        }
        if (adapter->state == XConsoleShellTelnetState_Iac) {
            if (byte == XCS_TELNET_IAC) {
                adapter->state = XConsoleShellTelnetState_Data;
                if (!xcs_telnet_echo_byte(adapter, byte)) return XConsoleResult_IoError;
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
            if (byte == XCS_TELNET_IP) {
                if (!xcs_telnet_echo_byte(adapter, 0x03)) return XConsoleResult_IoError;
                result = xcs_telnet_feed_byte(shell, session, 0x03);
            }
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
        if (!xcs_telnet_echo_byte(adapter, byte)) return XConsoleResult_IoError;
        result = xcs_telnet_feed_byte(shell, session, byte);
        if (byte == '\r' || byte == '\n')
            xcs_telnet_prompt_after_line(adapter, shell);
    }
    return result;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_TELNET_PROTOCOL_ON */
