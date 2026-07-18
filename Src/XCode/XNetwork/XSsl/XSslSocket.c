﻿﻿﻿﻿// XSslSocket.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// XSslSocket: TLS client/server socket, aligned with Qt 6.8 QSslSocket.
// Based on XClass framework, inherits XTcpSocket, overrides via vtable
// to replace TCP plaintext channel with TLS encrypted channel.
//
// Design notes:
//   - Inheritance: XSslSocket inherits XTcpSocket, vtable is set in XSslSocket_init
//     by XSslSocket_class_init(), overriding readData / writeData /
//     connectToHost / disconnectFromHost / close /
//     waitForConnected / waitForReadyRead / waitForBytesWritten /
//     waitForDisconnected virtual functions.
//   - Alias: Parent class same-name APIs aliased via header macros
//     to XIODevice_read_1/write_1; read_1 goes vtable -> xssl_v_readData
//     -> mbedtls_ssl_read -> BIO recv -> parent readData.
//   - BIO：xssl_bio_send/recv 通过 XClass_Parent(XAbstractSocket, ...)
//     to call parent readData/writeData directly, bypassing TLS layer.

#include "XSslSocket.h"
#include "XMemory.h"
#include "XString.h"
#include "XSignalSlot.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XClass.h"
#include <string.h>
#include "XPrintf.h"
#include "XRingBuffer.h"
#include "XIODevicePrivate.h"
#include "XEvent.h"
#include "XEventType.h"
#include "XNetwork_platform.h"
#include "XTypes.h"
#include <stdlib.h>

/* = Internal Structure = */

struct XSslSocket {
    XTcpSocket           base;             /* Parent class: TCP plaintext channel */

    XSslSession*         session;          /* TLS session (mbedTLS backend), self-owned */
    XSslProtocol         protocol;
    XSslPeerVerifyMode   verifyMode;
    int                  peerVerifyDepth;
    XString*             peerVerifyName;   /* Expected peer name (SNI/hostname verification) */
    XSslCertificate*     localCert;        /* Reference, not owned */
    XSslKey*             privateKey;       /* Reference, not owned */
    XSslCertificate*     caCert;           /* Reference, not owned; MVP only supports single CA */

    XSslSocket_SslMode   mode;
    bool                 encrypted;        /* Whether handshake is complete */
    bool                 handshakePending; /* Whether in handshake (read/write will pump handshake) */
    bool                 ignoreErrors;
    XVector*             localCertChain;   /* Qt 6.8 setLocalCertificateChain; owns */
    XVector*             peerCertChain;    /* Qt 6.8 peerCertificateChain cache; owns */
    XVector*             handshakeErrors;  /* Qt 6.8 sslHandshakeErrors; each element is int error code */
    struct XRingBuffer*  encRxBuf;
};

/* = Parent Vtable Func Ptr Types (for XClass_Parent) = */

typedef int64_t (*Fn_readData) (XIODevice*, char*, int64_t);
typedef int64_t (*Fn_writeData)(XIODevice*, const char*, int64_t);
typedef void    (*Fn_connectToHost)(XAbstractSocket*, const char*, uint16_t,
                                    XIODeviceBaseMode,
                                    XAbstractSocket_NetworkLayerProtocol);
typedef void    (*Fn_disconnectFromHost)(XAbstractSocket*);
typedef void    (*Fn_close)(XIODevice*);
typedef bool    (*Fn_waitFor)(XIODevice*, int);
typedef bool    (*Fn_waitForConnected)(XAbstractSocket*, int);
typedef bool    (*Fn_waitForDisconnected)(XAbstractSocket*, int);
typedef void    (*Fn_deinit)(XClass*);
typedef bool    (*Fn_event) (XAbstractSocket*, XEvent*);

/* 鐩存帴璋冪敤鐖剁被瀹炵幇鐨勮娉曠硸 */
#define PARENT_IO(SLOT, FnType)    XClass_Parent(XAbstractSocket, SLOT, FnType)
#define PARENT_SOCK(SLOT, FnType)  XClass_Parent(XAbstractSocket, SLOT, FnType)

/* = BIO: mbedTLS callbacks for raw socket I/O (bypassing TLS) = */

static int xssl_bio_send(void* u, const uint8_t* buf, size_t len) {
    XSslSocket* self = (XSslSocket*)u;
    int64_t w = PARENT_IO(EXIODevice_WriteData, Fn_writeData)
                    ((XIODevice*)self, (const char*)buf, (int64_t)len);
    if (w > 0) return (int)w;
    if (w == 0) return XSSL_BIO_WANT_WRITE;
    return XSSL_BIO_ERROR;
}

static int xssl_bio_recv(void* u, uint8_t* buf, size_t len) {
    XSslSocket* self = (XSslSocket*)u;
    if (!self->encRxBuf) return XSSL_BIO_WANT_READ;
    size_t avail = XRingBuffer_available(self->encRxBuf);
    if (avail == 0) return XSSL_BIO_WANT_READ;
    size_t toRead = len < avail ? len : avail;
    size_t got = XRingBuffer_read(self->encRxBuf, buf, toRead);
    return got > 0 ? (int)got : XSSL_BIO_WANT_READ;
}

/* = Session Lazy Creation = */

static bool xssl_ensure_session(XSslSocket* self, bool isServer) {
    if (self->session) return true;
    self->session = XSsl_sessionCreate(self->protocol, isServer);
    if (!self->session) return false;
    XSsl_sessionSetBio(self->session, self, xssl_bio_send, xssl_bio_recv);
    XSsl_sessionSetPeerVerify(self->session, self->verifyMode);
    if (self->peerVerifyName) {
        const char* n = XString_toUtf8(self->peerVerifyName);
        XSsl_sessionSetHostname(self->session, n);
    }
    if (self->localCert && self->privateKey) {
        XSsl_sessionSetCertificate(self->session, self->localCert, self->privateKey);
    }
    if (self->caCert) {
        XSsl_sessionAddCaCertificate(self->session, self->caCert);
    }
    return true;
}

/* 鎺銊ュЗ涓鈧[0xac]娆鈩冨綑鎵嬶紝杩斿洖 XSSL_S_* */
static int xssl_pump_handshake(XSslSocket* self) {
    if (!self->handshakePending) return XSSL_S_OK;
    if (!xssl_ensure_session(self, self->mode == XSslSocket_SslServerMode))
        return XSSL_S_ERROR;
    int r = XSsl_sessionHandshake(self->session);
    if (r == XSSL_S_OK) {
        self->encrypted = true;
        self->handshakePending = false;
        XSslSocket_encrypted_signal(self);
    } else if (r == XSSL_S_ERROR || r == XSSL_S_CLOSED) {
        XAbstractSocket_setSocketError((XAbstractSocket*)self,
            XAbstractSocket_SslHandshakeFailedError,
            XSsl_sessionLastErrorString(self->session));
        XSslSocket_sslErrors_signal(self, (int)XSsl_sessionVerifyResult(self->session));
    }
    return r;
}
/* = Vtable Override: Replace TCP Plaintext w/ TLS = */

    /* readData: decrypt then return to caller; unencrypted -> bypass */
static int64_t xssl_v_readData(XIODevice* io, char* data, int64_t maxlen) {
    XSslSocket* self = (XSslSocket*)io;

    /* 鏈惎鐢?TLS 鏃讹紙姣斿灏氭湭 startClientEncryption锛夛紝鐩存帴璧扮埗绫?TCP 鏄庢枃璇?*/
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_ReadData, Fn_readData)(io, data, maxlen);
    }

    /* 鍔犲瘑妯鈥崇础锛氳嫢鎻鈩冨[0xa2]滅亸氭湭瀹屾垚锛屽厛鎺銊ㄧ箻鎻鈩冨[0xa2]滈敍涙湭瀹屾垚杩斿洖 0锛堣銆冪粈烘殏鏃舵棤鏁版嵁锛?*/
    if (!self->encrypted) {
        if (!self->handshakePending) {
            return 0; /* session ended (close_notify/error): no more direct plaintext */
        }
        int r = xssl_pump_handshake(self);
        if (r != XSSL_S_OK) {
            /* 鎻鈩冨[0xa2]滄稉垨鍑洪敊锛氳繑鍥?0 璁鈺[0x80]笂灞傜瓑寰呬簨浠跺惊鐜紝閿欒宸查鈧[0xac]氳繃淇鈥冲娇涓婃姤 */
            return 0;
        }
    }

    /* 鍔犲瘑鏁版嵁浠庡簳灞?socket 璇伙紝鐒跺悗缁?session 瑙ｅ瘑鍚庤繑鍥炴槑鏂?*/
    int n = XSsl_sessionRead(self->session, (uint8_t*)data, (size_t)maxlen);
    if (n > 0) return (int64_t)n;
    if (n == 0) return 0;
    if (n == XSSL_S_CLOSED) return 0;   /* Peer closed w/ close_notify */
    return 0;   /* XSSL_S_ERROR -> 0: return 0 (not -1) so XIODevice_read_1 keeps already-buffered plaintext; error reported via sslErrors/socketError signals */
}

    /* writeData: encrypt then write to socket; unencrypted -> bypass */
static int64_t xssl_v_writeData(XIODevice* io, const char* data, int64_t len) {
    XSslSocket* self = (XSslSocket*)io;

    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_WriteData, Fn_writeData)(io, data, len);
    }
    if (!self->encrypted) {
        if (!self->handshakePending) {
            return 0; /* session ended (close_notify/error): no more direct plaintext */
        }
        int r = xssl_pump_handshake(self);
        if (r != XSSL_S_OK) return 0; /* 鎻鈩冨[0xa2]滈張畬鎴愶細鏆傛椂鏃犳硶鍐欙紝璁鈺[0x80]笂灞傞噸璇?*/
    }
    int n = XSsl_sessionWrite(self->session, (const uint8_t*)data, (size_t)len);
    if (n > 0) {
        XSslSocket_encryptedBytesWritten_signal(self, (int64_t)n);
        return (int64_t)n;
    }
    if (n == 0) return 0;
    return -1;
}

    /* connectToHost: auto-record SNI in client mode, else bypass */
static void xssl_v_connectToHost(XAbstractSocket* sock,
                                 const char* hostName, uint16_t port,
                                 XIODeviceBaseMode mode,
                                 XAbstractSocket_NetworkLayerProtocol proto) {
    XSslSocket* self = (XSslSocket*)sock;
    if (self->mode == XSslSocket_SslClientMode && !self->peerVerifyName && hostName) {
        self->peerVerifyName = XString_create_utf8(hostName);
    }
    PARENT_SOCK(EXAbstractSocket_ConnectToHost, Fn_connectToHost)
        (sock, hostName, port, mode, proto);
}

    /* disconnectFromHost: send close_notify then parent disconnect */
static void xssl_v_disconnectFromHost(XAbstractSocket* sock) {
    XSslSocket* self = (XSslSocket*)sock;
    if (self->session && self->encrypted) {
        XSsl_sessionShutdown(self->session);
        self->encrypted = false;
    }
    PARENT_SOCK(EXAbstractSocket_DisconnectFromHost, Fn_disconnectFromHost)(sock);
}

    /* close: similar to disconnect, then parent XIODevice.close */
static void xssl_v_close(XIODevice* io) {
    XSslSocket* self = (XSslSocket*)io;
    if (self->session && self->encrypted) {
        XSsl_sessionShutdown(self->session);
        self->encrypted = false;
    }
    PARENT_IO(EXIODevice_Close, Fn_close)(io);
}

/* --- waitForConnected锛氱瓑寰呭簳灞?TCP 寤虹珛鍚庯紝鍦銊ヮ吂鎴风妯鈥崇础涓嬪皢 handshakePending
 *     缃綅锛岀湡姝ｇ殑鎻鈩冨[0xa2]滈悽卞悗缁?waitForEncrypted 鎴?read/write 瑙﹀彂銆?--- */
static bool xssl_v_waitForConnected(XAbstractSocket* sock, int msecs) {
    bool ok = PARENT_SOCK(EXAbstractSocket_WaitForConnected, Fn_waitForConnected)(sock, msecs);
    if (!ok) return false;
    XSslSocket* self = (XSslSocket*)sock;
    if (self->mode == XSslSocket_SslClientMode) self->handshakePending = true;
    return true;
}

    /* waitForReadyRead: complete handshake first, then wait for plaintext */
static bool xssl_v_waitForReadyRead(XIODevice* io, int msecs) {
    XSslSocket* self = (XSslSocket*)io;
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_WaitForReadyRead, Fn_waitFor)(io, msecs);
    }
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (msecs < 0 ? 0 : (uint64_t)msecs);

    /* handshake phase: pump handshake, then dispatch events for progress */
    while (!self->encrypted) {
        int r = xssl_pump_handshake(self);
        if (r == XSSL_S_OK) break;
        if (r == XSSL_S_ERROR || r == XSSL_S_CLOSED) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        if (((XAbstractSocket*)self)->state != XAbstractSocket_ConnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    /* encrypted phase: wait until decrypted plaintext appears in the IODevice ring buffer */
    while (XAbstractSocket_bytesAvailable_base((XAbstractSocket*)self) == 0) {
        if (((XAbstractSocket*)self)->state != XAbstractSocket_ConnectedState) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    return true;
}

    /* waitForBytesWritten: bypass to parent; encrypted written = done */
static bool xssl_v_waitForBytesWritten(XIODevice* io, int msecs) {
    return PARENT_IO(EXIODevice_WaitForBytesWritten, Fn_waitFor)(io, msecs);
}

    /* waitForDisconnected: bypass to parent */
static bool xssl_v_waitForDisconnected(XAbstractSocket* sock, int msecs) {
    return PARENT_SOCK(EXAbstractSocket_WaitForDisconnected, Fn_waitForDisconnected)(sock, msecs);
}

    /* deinit: destroy TLS session, cert chain caches, then parent deinit */
static void xssl_v_deinit(XClass* obj) {
    XSslSocket* self = (XSslSocket*)obj;
    if (self->session)         { XSsl_sessionDestroy(self->session); self->session = NULL; }
    if (self->peerVerifyName)  { XString_delete_base(self->peerVerifyName); self->peerVerifyName = NULL; }
    if (self->peerCertChain)   { XVector_delete_base((XContainer*)self->peerCertChain);   self->peerCertChain = NULL; }
    if (self->localCertChain)  { XVector_delete_base((XContainer*)self->localCertChain);  self->localCertChain = NULL; }
    if (self->handshakeErrors) { XVector_delete_base((XContainer*)self->handshakeErrors); self->handshakeErrors = NULL; }
    if (self->encRxBuf)        { XRingBuffer_delete_base((XContainer*)self->encRxBuf);    self->encRxBuf = (struct XRingBuffer*)XRingBuffer_create(16384); }
    /* localCert / privateKey / caCert 鏄閮銊ョ穿鐢紝鏈被涓嶆嫢鏈?*/
    XClass_Deinit_Parent(XAbstractSocket, self);
}

/* =============== vtable 初始化 =============== */

static void xssl_drain_encrypted(XSslSocket* self) {
    if (self->handshakePending && !self->encrypted) {
        (void)xssl_pump_handshake(self);
    }
    if (!self->encrypted || !self->session) return;
    XIODevice* io = (XIODevice*)self;
    if (!io->m_d) return;
    int ch = XIODevice_currentReadChannel(io);
    struct XRingBuffer* rb = XIODevicePrivate_getOrCreateReadBuffer(io->m_d, ch);
    if (!rb) return;
    uint8_t buf[4096];
    bool any = false;
    //XPrintf("[XSSL_TRACE] drain begin sessionRead loop\n");
    for (;;) {
        int n = XSsl_sessionRead(self->session, buf, sizeof(buf));
        //XPrintf("[XSSL_TRACE] drain sessionRead n=%d\n", n);
        if (n > 0) { XRingBuffer_write(rb, (const char*)buf, (size_t)n); any = true; continue; }
        break;
    }
    //XPrintf("[XSSL_TRACE] drain end any=%d\n", (int)any);
    if (any) XIODevice_readyRead_signal(io);
    //XPrintf("[XSSL_TRACE] drain return\n");
}

    /* SOCK_ACT: decrypt to encRxBuf before IODevice read buffer */
static bool xssl_v_event(XAbstractSocket* sock, XEvent* e) {
    XSslSocket* self = (XSslSocket*)e; (void)self;
    self = (XSslSocket*)sock;
    /* 鏈姞瀵嗘膩寮忥細鐩存帴閫忎紶鐖剁被 */
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    /* 闈?SOCK_ACT 浜嬩欢锛氶鈧[0xac]忎紶鐖剁被锛堝 SOCK_CLOSE銆佸叾瀹冿級 */
    if (e->type != XEVENT_TYPE_SOCK_ACT) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    /* 瀛樺湪浠ｇ悊鎻鈩冨[0xa2]滄稉婁笅鏂囨椂锛岃蛋鐖剁被鍏煎璺緞锛堜笉甯歌锛?*/
    if (sock->proxyHandshakeCtx) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)sock->d_ptr;
    if (!priv || !sock->base.m_d) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }

    XEventSockAct* sa = (XEventSockAct*)e;
    //XPrintf("[XSSL_TRACE] event actType=%d\n", sa->actType);
    /* 椹卞姩搴曞眰 IOCP/select 鐘舵鈧[0xac]佹満锛堢埗绫讳篃鏄厛璋冭繖涓鈧[0xac]姝銉[0xaf]級 */
    XNetwork_socketHandleEvent(priv, e);

    /* Read: encrypted data -> encRxBuf (not IODevice read buffer) */
    if (sa->actType & XSocketAct_Read) {
        size_t bytesTransferred = XNetwork_socketReadFinishedBytes(priv);
        //XPrintf("[XSSL_TRACE] Read event bytes=%zu\n", bytesTransferred);
        if (bytesTransferred > 0 && self->encRxBuf) {
            const char* readBuf = XNetwork_socketReadBuffer(priv);
            if (readBuf) {
                XRingBuffer_write(self->encRxBuf, readBuf, bytesTransferred);
            }
        }
    /* Read: encrypted data -> encRxBuf (not IODevice read buffer) */
        xssl_drain_encrypted(self);
        /* 缁褏画鎶曢鈧[0xac]掍笅涓鈧[0xac]娆鈥崇磽姝銉[0xa8] */
        XNetwork_socketContinueRead(priv, sock->socketType == XAbstractSocket_UdpSocket);
    }

    /* Write: via xssl_v_writeData -> parent writeData; handle write-complete event */
       杩欓噷鍙鐞嗗簳灞傚啓瀹屾垚浜嬩欢锛宔mit bytesWritten 璁鈺[0x80]笂灞傜户缁?*/
    if (sa->actType & XSocketAct_Write) {
        size_t bytesWritten = XNetwork_socketWriteFinishedBytes(priv);
        if (bytesWritten > 0) {
            XIODevice_bytesWritten_signal((XIODevice*)sock, bytesWritten);
        }
        int wch = XIODevice_currentWriteChannel((XIODevice*)sock);
        struct XRingBuffer* wrb = XIODevicePrivate_getOrCreateWriteBuffer(sock->base.m_d, wch);
        XNetwork_socketContinueWrite(priv, wrb, sock->socketType == XAbstractSocket_UdpSocket);
    }

    /* Connect锛氬簳灞傚畬鎴愯繛鎺銉︽[0xa4]傞崚囨崲鐘舵鈧[0xac]?*/
    if (sa->actType & XSocketAct_Connect) {
        if (XNetwork_socketIsConnected(priv)) {
            XAbstractSocket_setSocketState(sock, XAbstractSocket_ConnectedState);
            XNetwork_socketContinueRead(priv, sock->socketType == XAbstractSocket_UdpSocket);
        } else {
            XAbstractSocket_setSocketError(sock, XAbstractSocket_ConnectionRefusedError, "Connection failed");
            XAbstractSocket_setSocketState(sock, XAbstractSocket_UnconnectedState);
        }
    }
    return true;
}


/* =============== 淇鈥冲娇瀹炵幇 =============== */

void* XSslSocket_encrypted_signal(XSslSocket* self)
{
    XEmitSignal(self, XSslSocket_encrypted_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_sslErrors_signal(XSslSocket* self, int errorCode)
{
    XEmitSignal(self, XSslSocket_sslErrors_signal,
                XVarList_Create(XVar(int, errorCode)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_encryptedBytesWritten_signal(XSslSocket* self, int64_t bytes)
{
    XEmitSignal(self, XSslSocket_encryptedBytesWritten_signal,
                XVarList_Create(XVar(int64_t, bytes)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}


/* =============== 生命周期管理 =============== */

void XSslSocket_init(XSslSocket* self)
{
    if (!self) return;
    XTcpSocket_init((XTcpSocket*)self);
    XClassGetVtable(self) = XSslSocket_class_init();

    self->session = NULL;
    self->protocol = XSSL_SecureProtocols;
    self->verifyMode = XSSL_VerifyPeer;
    self->peerVerifyDepth = 0;
    self->peerVerifyName = NULL;
    self->localCert = NULL;
    self->privateKey = NULL;
    self->caCert = NULL;
    self->mode = XSslSocket_UnencryptedMode;
    self->encrypted = false;
    self->handshakePending = false;
    self->ignoreErrors = false;
    self->localCertChain = NULL;
    self->peerCertChain = NULL;
    self->handshakeErrors = NULL;
    self->encRxBuf = (struct XRingBuffer*)XRingBuffer_create(16384);
}

XSslSocket* XSslSocket_create(void)
{
    XSslSocket* self = XNew(XSslSocket);
    if (!self) return NULL;
    XSslSocket_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

/* =============== 配置函数 =============== */

void XSslSocket_setSslConfiguration(XSslSocket* self, void* config) { (void)config; }
void* XSslSocket_sslConfiguration(const XSslSocket* self) { (void)self; return NULL; }

void XSslSocket_setProtocol(XSslSocket* self, XSslProtocol protocol)
{
    if (!self) return;
    self->protocol = protocol;
}

XSslProtocol XSslSocket_protocol(const XSslSocket* self)
{
    return self ? self->protocol : XSSL_SecureProtocols;
}

void XSslSocket_addCaCertificate(XSslSocket* self, XSslCertificate* ca)
{
    if (!self) return;
    self->caCert = ca;
}

void XSslSocket_setPeerVerifyMode(XSslSocket* self, XSslPeerVerifyMode mode)
{
    if (!self) return;
    self->verifyMode = mode;
}

XSslPeerVerifyMode XSslSocket_peerVerifyMode(const XSslSocket* self)
{
    return self ? self->verifyMode : XSSL_VerifyPeer;
}

void XSslSocket_setPeerVerifyDepth(XSslSocket* self, int depth)
{
    if (!self) return;
    self->peerVerifyDepth = depth;
}

int XSslSocket_peerVerifyDepth(const XSslSocket* self)
{
    return self ? self->peerVerifyDepth : 0;
}

void XSslSocket_setPeerVerifyName(XSslSocket* self, const char* hostName)
{
    if (!self) return;
    if (self->peerVerifyName) { XString_delete_base(self->peerVerifyName); self->peerVerifyName = NULL; }
    if (hostName) self->peerVerifyName = XString_create_utf8(hostName);
}

const char* XSslSocket_peerVerifyName(const XSslSocket* self)
{
    if (!self || !self->peerVerifyName) return NULL;
    return XString_toUtf8(self->peerVerifyName);
}

/* =============== 连接与加密 =============== */

void XSslSocket_connectToHostEncrypted(XSslSocket* self, const char* hostName, uint16_t port)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    if (hostName && !self->peerVerifyName) {
        self->peerVerifyName = XString_create_utf8(hostName);
    }
    XAbstractSocket_connectToHost_base((XAbstractSocket*)self, hostName, port,
                                       XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
}

void XSslSocket_connectToHostEncrypted_2(XSslSocket* self,
    const char* hostName, uint16_t port,
    XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol proto)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    XAbstractSocket_connectToHost_base((XAbstractSocket*)self, hostName, port, mode, proto);
}

void XSslSocket_startClientEncryption(XSslSocket* self)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    self->handshakePending = true;
}

void XSslSocket_startServerEncryption(XSslSocket* self)
{
    if (!self) return;
    self->mode = XSslSocket_SslServerMode;
    self->handshakePending = true;
}

bool XSslSocket_waitForEncrypted(XSslSocket* self, int msecs)
{
    if (!self) return false;
    self->handshakePending = true;
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch()
                        + (msecs < 0 ? 30000 : (uint64_t)msecs);
    while (!self->encrypted) {
        if (!self->handshakePending) return false;
        int r = xssl_pump_handshake(self);
        if (r == XSSL_S_OK) return true;
        if (r == XSSL_S_ERROR || r == XSSL_S_CLOSED) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        if (((XAbstractSocket*)self)->state != XAbstractSocket_ConnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    return true;
}

/* =============== 会话查询 =============== */

const char* XSslSocket_sessionProtocol(const XSslSocket* self)
{
    if (!self || !self->session) return "Unknown";
    return XSsl_sessionProtocolString(self->session);
}

const char* XSslSocket_sessionCipher(const XSslSocket* self)
{
    if (!self || !self->session) return NULL;
    return XSsl_sessionCipherName(self->session);
}

int XSslSocket_sessionCipherBits(const XSslSocket* self)
{
    if (!self || !self->session) return 0;
    return 0; /* not implemented */
}

int XSslSocket_sessionCipherBits_base(const XSslSocket* self)
{
    return XSslSocket_sessionCipherBits(self);
}

int XSslSocket_sessionProtocolVersion(const XSslSocket* self)
{
    if (!self || !self->session) return 0;
    return 0; /* not implemented */
}

/* =============== 证书管理 =============== */

void XSslSocket_setLocalCertificate(XSslSocket* self, const XSslCertificate* cert)
{
    if (!self) return;
    self->localCert = (XSslCertificate*)cert;
}

XSslCertificate* XSslSocket_localCertificate(const XSslSocket* self)
{
    return self ? self->localCert : NULL;
}


void XSslSocket_setLocalCertificateChain(XSslSocket* self, XVector* chain)
{
    if (!self) return;
    if (self->localCertChain) {
        XVector_delete_base((XContainer*)self->localCertChain);
    }
    self->localCertChain = chain;
}

XVector* XSslSocket_localCertificateChain(const XSslSocket* self)
{
    return self ? self->localCertChain : NULL;
}

XSslCertificate* XSslSocket_peerCertificate(const XSslSocket* self)
{
    if (!self || !self->peerCertChain) return NULL;
    return (XSslCertificate*)XVector_front_base(self->peerCertChain);
}

XVector* XSslSocket_peerCertificateChain(const XSslSocket* self)
{
    return self ? self->peerCertChain : NULL;
}

XVector* XSslSocket_sessionPeerCertificateChain(const XSslSocket* self)
{
    return XSslSocket_peerCertificateChain(self);
}

XVector* XSslSocket_sslHandshakeErrors(const XSslSocket* self)
{
    return self ? self->handshakeErrors : NULL;
}

void XSslSocket_ignoreSslErrors_2(XSslSocket* self, const XVector* errors)
{

void XSslSocket_ignoreSslErrors(XSslSocket* self)
{
    if (!self) return;
    self->ignoreErrors = true;
}

    (void)self; (void)errors;
}

void XSslSocket_continueInterruptedHandshake(XSslSocket* self)
{
    (void)self;
}

/* =============== 模式查询 =============== */

XSslSocket_SslMode XSslSocket_mode(const XSslSocket* self)
{
    return self ? self->mode : XSslSocket_UnencryptedMode;
}

bool XSslSocket_isEncrypted(const XSslSocket* self)
{
    return self ? self->encrypted : false;
}

XTcpSocket* XSslSocket_plainSocket(XSslSocket* self)
{
    return (XTcpSocket*)self;
}
/* =============== 简略名包装（loopback 测试用）=============== */

void XSslSocket_setPrivateKey(XSslSocket* self, XSslKey* key)
{
    if (!self) return;
    self->privateKey = key;
}

void XSslSocket_attachPlainSocket(XSslSocket* self, XTcpSocket* plain)
{
    if (!self || !plain) return;
    XAbstractSocket_setSocketDescriptor_base((XAbstractSocket*)self,
        XAbstractSocket_fd(plain),
        XAbstractSocket_ConnectedState,
        XIODevice_ReadWrite);
}

/* =============== 简略名包装（loopback 测试用）=============== */

bool XSslSocket_waitForReadyRead(XSslSocket* self, int msecs)
{
    if (!self) return false;
    Fn_waitFor fn = XClassGetVirtualFunc(self, EXIODevice_WaitForReadyRead, Fn_waitFor);
    return fn((XIODevice*)self, msecs);
}

bool XSslSocket_waitForBytesWritten(XSslSocket* self, int msecs)
{
    if (!self) return false;
    Fn_waitFor fn = XClassGetVirtualFunc(self, EXIODevice_WaitForBytesWritten, Fn_waitFor);
    return fn((XIODevice*)self, msecs);
}

int64_t XSslSocket_write(XSslSocket* self, const char* data, int64_t len)
{
    if (!self) return -1;
    Fn_writeData fn = XClassGetVirtualFunc(self, EXIODevice_WriteData, Fn_writeData);
    return fn((XIODevice*)self, data, len);
}

int64_t XSslSocket_read(XSslSocket* self, char* data, int64_t maxlen)
{
    if (!self) return -1;
    Fn_readData fn = XClassGetVirtualFunc(self, EXIODevice_ReadData, Fn_readData);
    return fn((XIODevice*)self, data, maxlen);
}

void XSslSocket_disconnectFromHost(XSslSocket* self)
{
    if (!self) return;
    Fn_disconnectFromHost fn = XClassGetVirtualFunc(self, EXAbstractSocket_DisconnectFromHost, Fn_disconnectFromHost);
    fn((XAbstractSocket*)self);
}

void XSslSocket_abort(XSslSocket* self)
{
    if (!self) return;
    /* TLS cleanup: send close_notify if encrypted */
    if (self->session && self->encrypted) {
        XSsl_sessionShutdown(self->session);
        self->encrypted = false;
        self->handshakePending = false;
    }
    XAbstractSocket_abort((XAbstractSocket*)self);
}




/* =============== 数据查询与访问器 =============== */

int64_t XSslSocket_encryptedBytesAvailable(const XSslSocket* self)
{
    if (!self || !self->encRxBuf) return 0;
    return (int64_t)XRingBuffer_available(self->encRxBuf);
}

int64_t XSslSocket_encryptedBytesToWrite(const XSslSocket* self)
{
    (void)self;
    return 0;
}

XSslSession* XSslSocket_session(XSslSocket* self)
{
    return self ? self->session : NULL;
}

void* XSslSocket_private(XSslSocket* self)
{
    (void)self;
    return NULL;
}

/* =============== 额外信号实现 =============== */

void* XSslSocket_modeChanged_signal(XSslSocket* self, XSslSocket_SslMode newMode)
{
    XEmitSignal(self, XSslSocket_modeChanged_signal,
                XVarList_Create(XVar(int, (int)newMode)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_peerVerifyError_signal(XSslSocket* self, int errorCode)
{
    XEmitSignal(self, XSslSocket_peerVerifyError_signal,
                XVarList_Create(XVar(int, errorCode)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_newSessionTicketReceived_signal(XSslSocket* self)
{
    XEmitSignal(self, XSslSocket_newSessionTicketReceived_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_alertSent_signal(XSslSocket* self,
    XSslAlertLevel level, XSslAlertType type, const char* description)
{
    (void)level; (void)type; (void)description;
    XEmitSignal(self, XSslSocket_alertSent_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_alertReceived_signal(XSslSocket* self,
    XSslAlertLevel level, XSslAlertType type, const char* description)
{
    (void)level; (void)type; (void)description;
    XEmitSignal(self, XSslSocket_alertReceived_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_handshakeInterruptedOnError_signal(XSslSocket* self, int errorCode)
{
    XEmitSignal(self, XSslSocket_handshakeInterruptedOnError_signal,
                XVarList_Create(XVar(int, errorCode)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_preSharedKeyAuthenticationRequired_signal(
    XSslSocket* self, void* authenticator)
{
    (void)authenticator;
    XEmitSignal(self, XSslSocket_preSharedKeyAuthenticationRequired_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/* =============== 静态/全局查询 =============== */

bool XSslSocket_supportsSsl(void) { return true; }
long XSslSocket_sslLibraryVersionNumber(void) { return 0; }
const char* XSslSocket_sslLibraryVersionString(void) { return "mbedtls"; }
long XSslSocket_sslLibraryBuildVersionNumber(void) { return 0; }
const char* XSslSocket_sslLibraryBuildVersionString(void) { return "mbedtls"; }
XVector* XSslSocket_availableBackends(void) { return NULL; }
const char* XSslSocket_activeBackend(void) { return "mbedtls"; }
bool XSslSocket_setActiveBackend(const char* backendName) { (void)backendName; return false; }
XVector* XSslSocket_supportedProtocols(const char* backendName) { (void)backendName; return NULL; }
bool XSslSocket_isProtocolSupported(XSslProtocol protocol, const char* backendName) { (void)protocol; (void)backendName; return false; }

XVtable* XSslSocket_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractSocket))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    /* 缁褎壙 XTcpSocket 鐨?vtable锛堣繘鑰岀户鎵?XAbstractSocket / XIODevice / XObject锛?*/
    XVTABLE_INHERIT_XCLASS(XAbstractSocket);

    /* IO 灞傦細璇汇鈧[0xac]佸啓銆佸叧闂鈧[0xac]佺瓑寰呭彲璇汇鈧[0xac]佺瓑寰呭啓瀹?鈥斺鈧[0xac]?鍏銊╁劥瑕嗙洊涓?TLS 鐗堟湰 */
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData,             xssl_v_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData,            xssl_v_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close,                xssl_v_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead,     xssl_v_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten,  xssl_v_waitForBytesWritten);

    /* Socket 灞傦細杩炴帴銆佹柇寮鈧[0xac]銆佺瓑寰呰繛鎺銉ｂ偓佺瓑寰呮柇寮鈧[0xac] 鈥斺鈧[0xac]?鍔犲叆 TLS 鐢熷懡鍛銊︽埂 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_ConnectToHost,      xssl_v_connectToHost);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_DisconnectFromHost, xssl_v_disconnectFromHost);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_WaitForConnected,   xssl_v_waitForConnected);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_WaitForDisconnected,xssl_v_waitForDisconnected);

    /* Class 灞傦細鏋愭瀯 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, xssl_v_event);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xssl_v_deinit);

    return XVTABLE_DEFAULT;
}





