/** @file XTelnetClient.c
 * @brief Telnet 客户端实现：XTelnetClient 协议栈。
 */

#include "XTelnetClient.h"

#if XPROTOCOL_ON && XTELNET_ON && XTELNET_CLIENT_ON

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
    XTELNET_AYT = 246,
    XTELNET_OPT_ECHO = 1,
    XTELNET_OPT_SGA = 3
};

static bool xtelnet_client_write_all(XTelnetClient* self,
                                     const uint8_t* data, size_t size)
{
    size_t offset = 0;
    if (!self || !self->m_device || (!data && size)) return false;
    while (offset < size) {
        int64_t written = XIODevice_write_1(self->m_device,
                                            (const char*)data + offset,
                                            (int64_t)(size - offset));
        if (written <= 0 || (size_t)written > size - offset) return false;
        offset += (size_t)written;
    }
    return true;
}

static bool xtelnet_client_send_option(XTelnetClient* self,
                                       uint8_t command, uint8_t option)
{
    const uint8_t packet[3] = { XTELNET_IAC, command, option };
    return xtelnet_client_write_all(self, packet, sizeof(packet));
}

static bool xtelnet_client_emit_data(XTelnetClient* self,
                                     const uint8_t* data, size_t size)
{
    if (!self || (!data && size)) return false;
    if (size) XTelnetClient_dataReceived_signal(self, data, size);
    return true;
}

static int64_t xtelnet_client_write_raw(XTelnetClient* self,
                                        const uint8_t* data, size_t size)
{
    size_t begin = 0;
    size_t i;
    if (!self || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        if (data[i] != XTELNET_IAC) continue;
        if (i > begin && !xtelnet_client_write_all(self, data + begin, i - begin))
            return -1;
        {
            const uint8_t escaped[2] = { XTELNET_IAC, XTELNET_IAC };
            if (!xtelnet_client_write_all(self, escaped, sizeof(escaped))) return -1;
        }
        begin = i + 1u;
    }
    if (begin < size && !xtelnet_client_write_all(self, data + begin, size - begin))
        return -1;
    return (int64_t)size;
}

static void xtelnet_client_device_ready_read(XObject* receiver, XVarList* args)
{
    XTelnetClient* self = (XTelnetClient*)receiver;
    uint8_t buffer[1024];
    (void)args;
    if (!self || !self->m_device || self->m_closed) return;
    for (;;) {
        int64_t n = XIODevice_read_1(self->m_device, (char*)buffer,
                                     (int64_t)sizeof(buffer));
        if (n <= 0) break;
        if (XTelnetClient_feedData(self, buffer, (size_t)n) < XProtocolResult_Ok)
            break;
        if (self->m_closed) break;
    }
}

bool XTelnetClient_setDevice(XTelnetClient* self, XIODevice* device)
{
    if (!self || !device) return false;
    if (self->m_device == device) return true;
    XTelnetClient_stop(self);
    self->m_device = device;
    self->m_state = XTelnetClientState_Data;
    self->m_negotiation = 0;
    self->m_afterCarriageReturn = false;
    self->m_localEchoEnabled = true;
    self->m_closed = false;
    self->m_readyRead = XObject_connect_1((XObject*)device,
        XSignal(XIODevice_readyRead_signal), (XObject*)self,
        xtelnet_client_device_ready_read, XConnectionType_Direct);
    return self->m_readyRead != NULL;
}

bool XTelnetClient_start(XTelnetClient* self)
{
    if (!self || !self->m_device || self->m_closed) return false;
    if (!xtelnet_client_send_option(self, XTELNET_DO, XTELNET_OPT_ECHO) ||
        !xtelnet_client_send_option(self, XTELNET_WILL, XTELNET_OPT_SGA) ||
        !xtelnet_client_send_option(self, XTELNET_DO, XTELNET_OPT_SGA) ||
        !XTelnetClient_flush(self)) {
        self->m_closed = true;
        XTelnetClient_errorOccurred_signal(self, XProtocolResult_IoError);
        return false;
    }
    return true;
}

void XTelnetClient_stop(XTelnetClient* self)
{
    if (!self) return;
    if (self->m_readyRead) {
        XObject_disconnect_2(self->m_readyRead);
        self->m_readyRead = NULL;
    }
    if (!self->m_closed) {
        self->m_closed = true;
        XTelnetClient_closed_signal(self);
    }
}

XProtocolResult XTelnetClient_feedData(XTelnetClient* self,
                                       const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t i;
    if (!self || (!data && size)) return XProtocolResult_InvalidArgument;
    if (self->m_closed) return XProtocolResult_Failed;
    for (i = 0; i < size; ++i) {
        uint8_t byte = bytes[i];
        if (self->m_state == XTelnetClientState_Subnegotiation) {
            if (byte == XTELNET_IAC)
                self->m_state = XTelnetClientState_SubnegotiationIac;
            continue;
        }
        if (self->m_state == XTelnetClientState_SubnegotiationIac) {
            self->m_state = byte == XTELNET_SE ? XTelnetClientState_Data :
                                                  XTelnetClientState_Subnegotiation;
            continue;
        }
        if (self->m_state == XTelnetClientState_Option) {
            uint8_t reply;
            if (self->m_negotiation == XTELNET_WILL) {
                if (byte == XTELNET_OPT_ECHO) {
                    self->m_localEchoEnabled = false;
                    reply = XTELNET_DO;
                } else if (byte == XTELNET_OPT_SGA) {
                    reply = XTELNET_DO;
                } else {
                    reply = XTELNET_DONT;
                }
            } else if (self->m_negotiation == XTELNET_WONT) {
                if (byte == XTELNET_OPT_ECHO) self->m_localEchoEnabled = true;
                reply = XTELNET_DONT;
            } else if (self->m_negotiation == XTELNET_DO) {
                reply = byte == XTELNET_OPT_SGA ? XTELNET_WILL : XTELNET_WONT;
            } else {
                reply = XTELNET_WONT;
            }
            if (!xtelnet_client_send_option(self, reply, byte)) {
                self->m_closed = true;
                XTelnetClient_errorOccurred_signal(self, XProtocolResult_IoError);
                return XProtocolResult_IoError;
            }
            self->m_state = XTelnetClientState_Data;
            continue;
        }
        if (self->m_state == XTelnetClientState_Iac) {
            if (byte == XTELNET_IAC) {
                self->m_state = XTelnetClientState_Data;
                if (!xtelnet_client_emit_data(self, &byte, 1)) return XProtocolResult_IoError;
            } else if (byte == XTELNET_DO || byte == XTELNET_DONT ||
                       byte == XTELNET_WILL || byte == XTELNET_WONT) {
                self->m_negotiation = byte;
                self->m_state = XTelnetClientState_Option;
            } else if (byte == XTELNET_SB) {
                self->m_state = XTelnetClientState_Subnegotiation;
            } else if (byte == XTELNET_IP) {
                const uint8_t interrupt = 0x03;
                self->m_state = XTelnetClientState_Data;
                if (!xtelnet_client_emit_data(self, &interrupt, 1)) return XProtocolResult_IoError;
            } else if (byte == XTELNET_AYT) {
                static const uint8_t answer[] = "[yes]\r\n";
                self->m_state = XTelnetClientState_Data;
                if (!xtelnet_client_write_raw(self, answer, sizeof(answer) - 1u))
                    return XProtocolResult_IoError;
            } else {
                self->m_state = XTelnetClientState_Data;
            }
            continue;
        }
        if (byte == XTELNET_IAC) {
            self->m_state = XTelnetClientState_Iac;
            continue;
        }
        if (self->m_afterCarriageReturn && (byte == 0 || byte == '\n')) {
            self->m_afterCarriageReturn = false;
            continue;
        }
        if (byte == '\r') {
            const uint8_t newline = '\n';
            self->m_afterCarriageReturn = true;
            if (!xtelnet_client_emit_data(self, &newline, 1)) return XProtocolResult_IoError;
            continue;
        }
        self->m_afterCarriageReturn = false;
        if (!xtelnet_client_emit_data(self, &byte, 1)) return XProtocolResult_IoError;
    }
    return XProtocolResult_Ok;
}

int64_t XTelnetClient_write(XTelnetClient* self, const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t begin = 0;
    size_t i;
    if (!self || self->m_closed || (!data && size)) return -1;
    for (i = 0; i < size; ++i) {
        if (bytes[i] != '\r' && bytes[i] != '\n') continue;
        if (i > begin && xtelnet_client_write_raw(self, bytes + begin, i - begin) < 0)
            return -1;
        if (bytes[i] == '\r' && i + 1u < size && bytes[i + 1u] == '\n') ++i;
        {
            const uint8_t crlf[2] = { '\r', '\n' };
            if (!xtelnet_client_write_all(self, crlf, sizeof(crlf))) return -1;
        }
        begin = i + 1u;
    }
    if (begin < size && xtelnet_client_write_raw(self, bytes + begin, size - begin) < 0)
        return -1;
    return (int64_t)size;
}

bool XTelnetClient_flush(XTelnetClient* self)
{
    return self && !self->m_closed && self->m_device &&
           XIODevice_flush(self->m_device);
}

bool XTelnetClient_isClosed(const XTelnetClient* self)
{
    return !self || self->m_closed;
}

bool XTelnetClient_localEchoEnabled(const XTelnetClient* self)
{
    return self && self->m_localEchoEnabled;
}

void* XTelnetClient_dataReceived_signal(XTelnetClient* self,
                                       const void* data, size_t size)
{
    XEmitSignal(self, XTelnetClient_dataReceived_signal,
                XVarList_Create(XVar(const void*, data), XVar(size_t, size)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XTelnetClient_closed_signal(XTelnetClient* self)
{
    XEmitSignal(self, XTelnetClient_closed_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XTelnetClient_errorOccurred_signal(XTelnetClient* self, int error)
{
    XEmitSignal(self, XTelnetClient_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

static void xtelnet_client_deinit(XTelnetClient* self)
{
    if (!self) return;
    XTelnetClient_stop(self);
    XClass_Deinit_Parent(XObject, self);
}

XVtable* XTelnetClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTelnetClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xtelnet_client_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTelnetClient);
    return XVTABLE_DEFAULT;
}

void XTelnetClient_init(XTelnetClient* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XTelnetClient);
    self->m_state = XTelnetClientState_Data;
    self->m_localEchoEnabled = true;
}

XTelnetClient* XTelnetClient_create_ex(XMemoryType memory)
{
    XTelnetClient* self = (XTelnetClient*)XMemory_malloc(sizeof(XTelnetClient), memory);
    if (!self) return NULL;
    XTelnetClient_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XPROTOCOL_ON && XTELNET_ON && XTELNET_CLIENT_ON */
