/** @file XTelnetServer.c
 * @brief Telnet 服务器实现：XTelnetServer 协议栈。
 * @details
 * 移植自 XConsoleShell_Telnet.c，处理 IAC 转义、DO/DONT/WILL/WONT 协商、
 * 子协商跳过和 CR-NUL 规则。数据通过 XIODevice 输出，TCP 等平台设备均可接入，
 * 主机回调通过信号槽与 Shell 交互，并采用 XObject 生命周期管理。
 * 输入字节经 bytesReceived 信号交给宿主，宿主用 setter 回填查询结果。
 */

#include "XTelnetServer.h"

#if XPROTOCOL_ON && XTELNET_ON && XTELNET_SERVER_ON

#include "XMemory.h"
#include "XVarList.h"
#include <string.h>

enum {
    XTELNET_IAC = 255,
    XTELNET_DONT = 254,
    XTELNET_DO = 253,
    XTELNET_WONT = 252,
    XTELNET_WILL = 251,
    XTELNET_SB = 250,
    XTELNET_SE = 240,
    XTELNET_IP = 244,
    XTELNET_OPT_ECHO = 1
};

/* 将全部字节写入底层设备，循环直到写完或传输失败 */
static bool xtelnet_write_all(XTelnetServer* self,
                              const uint8_t* data, size_t size)
{
    size_t offset = 0;
    if (!self || (!data && size) || !self->m_device) return false;
    while (offset < size) {
        int64_t written = XIODevice_write_1(self->m_device,
                                            (const char*)(data + offset),
                                            (int64_t)(size - offset));
        if (written <= 0 || (size_t)written > size - offset) return false;
        offset += (size_t)written;
    }
    return true;
}

static bool xtelnet_reply(XTelnetServer* self, uint8_t command, uint8_t option)
{
    const uint8_t bytes[3] = { XTELNET_IAC, command, option };
    return xtelnet_write_all(self, bytes, sizeof(bytes));
}

/* 原始写入：转义 IAC（0xFF）避免与命令序列混淆 */
static int64_t xtelnet_write_raw(XTelnetServer* self,
                                 const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t begin = 0;
    size_t i;
    if (!self || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != XTELNET_IAC) continue;
        if (i > begin && !xtelnet_write_all(self, bytes + begin, i - begin)) return -1;
        {
            const uint8_t escapedIac[2] = { XTELNET_IAC, XTELNET_IAC };
            if (!xtelnet_write_all(self, escapedIac, sizeof(escapedIac))) return -1;
        }
        begin = i + 1u;
    }
    if (begin < size && !xtelnet_write_all(self, bytes + begin, size - begin)) return -1;
    return (int64_t)size;
}

/* Telnet 文本行以 CRLF 结束；Shell 输出统一使用 LF，发送前补 CR，
 * 否则客户端只换行不回车，各行会向右错位。 */
int64_t XTelnetServer_write(XTelnetServer* self, const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t begin = 0;
    size_t i;
    if (!self || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (byte == '\r' || byte == '\n') {
            if (i > begin &&
                xtelnet_write_raw(self, bytes + begin, i - begin) !=
                    (int64_t)(i - begin))
                return -1;
            if (byte == '\r' && i + 1 < size && bytes[i + 1] == '\n') ++i;
            {
                const uint8_t crlf[2] = { '\r', '\n' };
                if (!xtelnet_write_all(self, crlf, sizeof(crlf))) return -1;
            }
            begin = i + 1u;
        }
    }
    if (begin < size &&
        xtelnet_write_raw(self, bytes + begin, size - begin) !=
            (int64_t)(size - begin))
        return -1;
    return (int64_t)size;
}

/* Telnet 客户端接受 DONT ECHO 后关闭本地回显，由服务端负责回显普通输入。
 * 密码输入期间 Shell 通过 XTelnetServer_setInputEcho(false) 关闭回显，
 * 避免明文泄露。 */
static bool xtelnet_echo_byte(XTelnetServer* self, uint8_t byte)
{
    const uint8_t* text;
    size_t size;
    if (!self || !self->m_echoEnabled) return true;
    if (self->m_echoEscape) {
        if (byte >= '@' && byte <= '~') self->m_echoEscape = false;
        return true;
    }
    if (byte == 0x1b) {
        self->m_echoEscape = true;
        return true;
    }
    if (byte == '\r') {
        text = (const uint8_t*)"\r\n";
        size = 2;
        return xtelnet_write_raw(self, text, size) == (int64_t)size;
    }
    if (byte == '\n') {
        if (!self->m_echoPendingCr)
            return xtelnet_write_raw(self, (const uint8_t*)"\r\n", 2) == 2;
        self->m_echoPendingCr = false;
        return true;
    }
    self->m_echoPendingCr = false;
    if (byte == '\b' || byte == 0x7f) {
        text = (const uint8_t*)"\b \b";
        size = 3;
        return xtelnet_write_raw(self, text, size) == (int64_t)size;
    }
    if (byte == 0x03) {
        text = (const uint8_t*)"^C\r\n";
        size = 4;
        return xtelnet_write_raw(self, text, size) == (int64_t)size;
    }
    if (byte >= 0x20 || byte == '\t')
        return xtelnet_write_raw(self, &byte, 1) == 1;
    return true;
}

bool XTelnetServer_setInputEcho(XTelnetServer* self, bool enabled)
{
    if (!self) return false;
    self->m_echoEnabled = enabled;
    return true;
}

/* ==================== 查询辅助 + setter 回填 ==================== */

static bool xtelnet_query_is_running(XTelnetServer* self)
{
    if (!self) return false;
    self->m_isRunningResult = false;
    XTelnetServer_isRunningRequested_signal(self);
    return self->m_isRunningResult;
}

static bool xtelnet_query_close_requested(XTelnetServer* self)
{
    if (!self) return false;
    self->m_closeRequestedResult = false;
    XTelnetServer_closeRequested_signal(self);
    return self->m_closeRequestedResult;
}

static bool xtelnet_query_suppress_prompt(XTelnetServer* self)
{
    if (!self) return false;
    self->m_suppressPromptResult = false;
    XTelnetServer_suppressPromptRequested_signal(self);
    return self->m_suppressPromptResult;
}

static size_t xtelnet_query_user_name(XTelnetServer* self,
                                      char* buffer, size_t capacity)
{
    size_t n;
    if (!self || !buffer || !capacity) return 0;
    self->m_userNameResultLen = 0;
    self->m_userNameResult[0] = '\0';
    XTelnetServer_userNameRequested_signal(
        self, self->m_userNameResult, sizeof(self->m_userNameResult));
    n = self->m_userNameResultLen;
    if (n >= capacity) n = capacity - 1u;
    if (n) memcpy(buffer, self->m_userNameResult, n);
    buffer[n] = '\0';
    return n;
}

bool XTelnetServer_emitPrompt(XTelnetServer* self)
{
    char prompt[XTELNET_LOGIN_NAME_SIZE + 2u];
    char name[XTELNET_LOGIN_NAME_SIZE];
    size_t n = 0;
    if (!self) return false;
    n = xtelnet_query_user_name(self, name, sizeof(name));
    if (n == 0 || n >= sizeof(name)) {
        const char* def = XTELNET_DEFAULT_PROMPT_NAME;
        n = strlen(def);
        if (n >= sizeof(name)) n = sizeof(name) - 1u;
        memcpy(name, def, n);
    }
    if (n >= sizeof(prompt) - 2u) n = sizeof(prompt) - 2u;
    memcpy(prompt, name, n);
    prompt[n++] = '>';
    prompt[n++] = ' ';
    return XTelnetServer_write(self, (const uint8_t*)prompt, n) == (int64_t)n;
}

static void xtelnet_prompt_after_line(XTelnetServer* self)
{
    if (!self) return;
    if (xtelnet_query_close_requested(self)) return;
    if (xtelnet_query_suppress_prompt(self)) return;
    if (!xtelnet_query_is_running(self)) return;
    (void)XTelnetServer_emitPrompt(self);
}

static XProtocolResult xtelnet_feed_byte(XTelnetServer* self, uint8_t byte)
{
    if (!self) return XProtocolResult_InvalidArgument;
    self->m_bytesReceivedResult = XProtocolResult_Ok;
    XTelnetServer_bytesReceived_signal(self, &byte, 1);
    return (XProtocolResult)self->m_bytesReceivedResult;
}

static void xtelnet_device_ready_read(XObject* receiver, XVarList* args)
{
    XTelnetServer* self = (XTelnetServer*)receiver;
    uint8_t buf[1024];
    (void)args;
    if (!self || !self->m_device) return;
    for (;;) {
        int64_t n = XIODevice_read_1(self->m_device, (char*)buf, (int64_t)sizeof(buf));
        if (n <= 0) break;
        (void)XTelnetServer_feedData(self, buf, (size_t)n);
        if (self->m_closed) break;
    }
}

bool XTelnetServer_setDevice(XTelnetServer* self, XIODevice* device)
{
    if (!self || !device) return false;
    if (self->m_device == device) return true;
    XTelnetServer_stop(self);
    self->m_device = device;
    self->m_closed = false;
    self->m_state = XTelnetServerState_Data;
    self->m_negotiation = 0;
    self->m_afterCarriageReturn = false;
    self->m_echoEnabled = true;
    self->m_echoPendingCr = false;
    self->m_echoEscape = false;
    self->m_readyRead = XObject_connect_1((XObject*)device,
        XSignal(XIODevice_readyRead_signal), (XObject*)self,
        xtelnet_device_ready_read, XConnectionType_Direct);
    return true;
}

void XTelnetServer_setHostContext(XTelnetServer* self, void* context)
{
    if (self) self->m_hostContext = context;
}

bool XTelnetServer_start(XTelnetServer* self)
{
    if (!self || !self->m_device) return false;
    self->m_closed = false;
    return XTelnetServer_emitPrompt(self);
}

void XTelnetServer_stop(XTelnetServer* self)
{
    if (!self) return;
    if (self->m_readyRead) {
        XObject_disconnect_2(self->m_readyRead);
        self->m_readyRead = NULL;
    }
    if (!self->m_closed) {
        self->m_closed = true;
        XTelnetServer_closed_signal(self);
    }
}

bool XTelnetServer_flush(XTelnetServer* self)
{
    return self && self->m_device && XIODevice_flush(self->m_device);
}

bool XTelnetServer_isClosed(const XTelnetServer* self)
{
    return !self || self->m_closed;
}

XProtocolResult XTelnetServer_feedData(XTelnetServer* self,
                                       const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    XProtocolResult result = XProtocolResult_Ok;
    size_t i;
    if (!self || (!data && size)) return XProtocolResult_InvalidArgument;
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (self->m_state == XTelnetServerState_Subnegotiation) {
            if (byte == XTELNET_IAC) self->m_state = XTelnetServerState_SubnegotiationIac;
            continue;
        }
        if (self->m_state == XTelnetServerState_SubnegotiationIac) {
            self->m_state = byte == XTELNET_SE ? XTelnetServerState_Data :
                                                 XTelnetServerState_Subnegotiation;
            continue;
        }
        if (self->m_state == XTelnetServerState_Option) {
            if (self->m_negotiation == XTELNET_DO) {
                if (byte == XTELNET_OPT_ECHO) {
                    if (!xtelnet_reply(self, XTELNET_WILL, byte))
                        return XProtocolResult_IoError;
                } else if (!xtelnet_reply(self, XTELNET_WONT, byte)) {
                    return XProtocolResult_IoError;
                }
            }
            if (self->m_negotiation == XTELNET_WILL) {
                if (byte == XTELNET_OPT_ECHO) {
                    if (!xtelnet_reply(self, XTELNET_DONT, byte))
                        return XProtocolResult_IoError;
                } else if (!xtelnet_reply(self, XTELNET_DONT, byte)) {
                    return XProtocolResult_IoError;
                }
            }
            self->m_state = XTelnetServerState_Data;
            continue;
        }
        if (self->m_state == XTelnetServerState_Iac) {
            if (byte == XTELNET_IAC) {
                self->m_state = XTelnetServerState_Data;
                if (!xtelnet_echo_byte(self, byte)) return XProtocolResult_IoError;
                result = xtelnet_feed_byte(self, byte);
                if (result == XProtocolResult_IoError) {
                    XTelnetServer_errorOccurred_signal(self, (int)result);
                    return result;
                }
                continue;
            }
            if (byte == XTELNET_DO || byte == XTELNET_DONT ||
                byte == XTELNET_WILL || byte == XTELNET_WONT) {
                self->m_negotiation = byte;
                self->m_state = XTelnetServerState_Option;
                continue;
            }
            if (byte == XTELNET_SB) {
                self->m_state = XTelnetServerState_Subnegotiation;
                continue;
            }
            self->m_state = XTelnetServerState_Data;
            if (byte == XTELNET_IP) {
                if (!xtelnet_echo_byte(self, 0x03)) return XProtocolResult_IoError;
                result = xtelnet_feed_byte(self, 0x03);
                if (result == XProtocolResult_IoError) {
                    XTelnetServer_errorOccurred_signal(self, (int)result);
                    return result;
                }
            }
            continue;
        }
        if (byte == XTELNET_IAC) {
            self->m_state = XTelnetServerState_Iac;
            continue;
        }
        if (self->m_afterCarriageReturn && (byte == 0 || byte == '\n')) {
            self->m_afterCarriageReturn = false;
            continue;
        }
        self->m_afterCarriageReturn = byte == '\r';
        if (!xtelnet_echo_byte(self, byte)) return XProtocolResult_IoError;
        result = xtelnet_feed_byte(self, byte);
        if (result == XProtocolResult_IoError) {
            XTelnetServer_errorOccurred_signal(self, (int)result);
            return result;
        }
        if (byte == '\r' || byte == '\n')
            xtelnet_prompt_after_line(self);
    }
    return result;
}

/* ==================== 信号实现 ==================== */

void* XTelnetServer_bytesReceived_signal(XTelnetServer* self,
                                         const void* data, size_t size)
{
    XVarList* args = XVarList_Create(XVar(const void*, data), XVar(size_t, size));
    XEmitSignal(self, XTelnetServer_bytesReceived_signal, args,
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_isRunningRequested_signal(XTelnetServer* self)
{
    XEmitSignal(self, XTelnetServer_isRunningRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_closeRequested_signal(XTelnetServer* self)
{
    XEmitSignal(self, XTelnetServer_closeRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_suppressPromptRequested_signal(XTelnetServer* self)
{
    XEmitSignal(self, XTelnetServer_suppressPromptRequested_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_userNameRequested_signal(XTelnetServer* self,
                                             char* buffer, size_t capacity)
{
    XEmitSignal(self, XTelnetServer_userNameRequested_signal,
                XVarList_Create(XVar(char*, buffer), XVar(size_t, capacity)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_closed_signal(XTelnetServer* self)
{
    XEmitSignal(self, XTelnetServer_closed_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetServer_errorOccurred_signal(XTelnetServer* self, int error)
{
    XEmitSignal(self, XTelnetServer_errorOccurred_signal,
                XVarList_Create(XVar(int, error)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* ==================== setter 回填 ==================== */

void XTelnetServer_setBytesReceivedResult(XTelnetServer* self, int result)
{
    if (self) self->m_bytesReceivedResult = result;
}

void XTelnetServer_setIsRunningResult(XTelnetServer* self, bool result)
{
    if (self) self->m_isRunningResult = result;
}

void XTelnetServer_setCloseRequestedResult(XTelnetServer* self, bool result)
{
    if (self) self->m_closeRequestedResult = result;
}

void XTelnetServer_setSuppressPromptResult(XTelnetServer* self, bool result)
{
    if (self) self->m_suppressPromptResult = result;
}

void XTelnetServer_setUserNameResult(XTelnetServer* self,
                                     const char* name, size_t len)
{
    if (!self) return;
    self->m_userNameResultLen = 0;
    self->m_userNameResult[0] = '\0';
    if (!name || len == 0) return;
    if (len >= sizeof(self->m_userNameResult))
        len = sizeof(self->m_userNameResult) - 1u;
    memcpy(self->m_userNameResult, name, len);
    self->m_userNameResult[len] = '\0';
    self->m_userNameResultLen = len;
}

static void VXTelnetServer_deinit(XTelnetServer* self)
{
    if (!self) return;
    XTelnetServer_stop(self);
    XClass_Deinit_Parent(XObject, self);
}

XVtable* XTelnetServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTelnetServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTelnetServer_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTelnetServer);
    return XVTABLE_DEFAULT;
}

void XTelnetServer_init(XTelnetServer* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XTelnetServer);
    self->m_state = XTelnetServerState_Data;
    self->m_echoEnabled = true;
    self->m_bytesReceivedResult = XProtocolResult_Ok;
}

XTelnetServer* XTelnetServer_create_ex(XMemoryType memory)
{
    XTelnetServer* self = (XTelnetServer*)XMemory_malloc(sizeof(XTelnetServer), memory);
    if (!self) return NULL;
    XTelnetServer_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XPROTOCOL_ON && XTELNET_ON && XTELNET_SERVER_ON */
