// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// XSslSocket：TLS 客户端/服务端套接字，对齐 Qt 6.8 QSslSocket。
// 基于 XClass 框架，继承 XTcpSocket，通过虚函数表重载将 TCP 明文通道
// 替换为 TLS 加密通道。
//
// 设计要点：
//   - 继承：XSslSocket 继承 XTcpSocket，虚函数表在 XSslSocket_init 中由
//     XSslSocket_class_init() 设置，重载 readData / writeData /
//     connectToHost / disconnectFromHost / close /
//     waitForConnected / waitForReadyRead / waitForBytesWritten /
//     waitForDisconnected 等虚函数。
//   - 别名：父类同名 API 通过头文件宏别名暴露，本文件不写 wrapper 函数，
//     避免符号重复与 API 漂移；read_1 经 vtable -> xssl_v_readData
//     -> mbedtls_ssl_read -> BIO recv -> 父类 readData。
//   - BIO：xssl_bio_send/recv 通过 XClass_Parent(XAbstractSocket, ...)
//     直接调用父类 readData/writeData，绕过 TLS 层。

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
#include "XFile.h"
#include "XTypes.h"
#include <stdlib.h>

/* = 内部结构体 = */

struct XSslSocket {
    XTcpSocket           base;             /* 父类：TCP 明文通道 */

    XSslSession*         session;          /* TLS 会话（mbedTLS 后端），自持有 */
    XSslProtocol         protocol;
    XSslPeerVerifyMode   verifyMode;
    int                  peerVerifyDepth;
    XString*             peerVerifyName;   /* 期望的对端名称（SNI/主机名校验） */
    XSslCertificate*     localCert;        /* 外部引用，不持有 */
    XSslKey*             privateKey;       /* 外部引用，不持有 */
    XSslCertificate*     caCert;           /* 外部引用，不持有；MVP 仅支持单个 CA */

    XSslSocket_SslMode   mode;
    bool                 encrypted;        /* 握手是否完成 */
    bool                 handshakePending; /* 是否处于握手态（read/write 会驱动握手） */
    bool                 ignoreErrors;
    XVector*             localCertChain;   /* Qt 6.8 setLocalCertificateChain；自持有 */
    XVector*             peerCertChain;    /* Qt 6.8 peerCertificateChain 缓存；自持有 */
    XVector*             handshakeErrors;  /* Qt 6.8 sslHandshakeErrors；每个元素为 int 错误码 */
    struct XRingBuffer*  encRxBuf;
};


/* =============== XSslConfiguration 结构体（对齐 Qt 6.8 QSslConfiguration） =============== */

struct XSslConfiguration {
    XSslProtocol         protocol;
    XSslPeerVerifyMode   verifyMode;
    int                  peerVerifyDepth;
    XString*             peerVerifyName;   /* 自持有 */
    XSslCertificate*     localCert;        /* 外部引用，不持有 */
    XSslKey*             privateKey;       /* 外部引用，不持有 */
    XVector*             localCertChain;   /* 自持有 */
};

/* = 父类虚函数指针类型（供 XClass_Parent 使用） = */

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

/* 直接调用父类实现的语法糖 */
#define PARENT_IO(SLOT, FnType)    XClass_Parent(XAbstractSocket, SLOT, FnType)
#define PARENT_SOCK(SLOT, FnType)  XClass_Parent(XAbstractSocket, SLOT, FnType)

/* = BIO：mbedTLS 原始 socket I/O 回调（绕过 TLS 层） = */

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

/* = 会话懒创建 = */

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

/* 推进一次握手，返回 XSSL_S_* */
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

/* = 虚函数重载：用 TLS 替换 TCP 明文 = */

/* readData：解密后交给上层调用者；未加密模式直接透传父类 */
static int64_t xssl_v_readData(XIODevice* io, char* data, int64_t maxlen) {
    XSslSocket* self = (XSslSocket*)io;

    /* TLS 尚未启动：在 startClientEncryption 之前透传到 TCP 明文 */
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_ReadData, Fn_readData)(io, data, maxlen);
    }

    /* 加密模式：先驱动握手；未完成则返回 0 */
    if (!self->encrypted) {
        if (!self->handshakePending) {
            return 0; /* 会话已结束（close_notify/出错）：不再有明文 */
        }
        int r = xssl_pump_handshake(self);
        if (r != XSSL_S_OK) {
            return 0; /* XSSL_S_ERROR/CLOSED —— 错误已通过信号上报 */
        }
    }

    /* 解密：从 TLS 会话读取明文交给调用者 */
    int n = XSsl_sessionRead(self->session, (uint8_t*)data, (size_t)maxlen);
    if (n > 0) return (int64_t)n;
    if (n == 0) return 0;
    if (n == XSSL_S_CLOSED) return 0; /* 对端以 close_notify 关闭 */
    return 0; /* XSSL_S_ERROR -> 0：返回 0（非 -1），让 XIODevice_read_1 保留已缓冲明文；错误经 sslErrors/socketError 信号上报 */
}

/* writeData：加密后写入 socket；未加密模式直接透传父类 */
static int64_t xssl_v_writeData(XIODevice* io, const char* data, int64_t len) {
    XSslSocket* self = (XSslSocket*)io;

    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_WriteData, Fn_writeData)(io, data, len);
    }
    if (!self->encrypted) {
        if (!self->handshakePending) {
            return 0; /* 会话已结束（close_notify/出错）：不再写明文 */
        }
        int r = xssl_pump_handshake(self);
        if (r != XSSL_S_OK) return 0; /* 握手未完成：延后 */
    }
    int n = XSsl_sessionWrite(self->session, (const uint8_t*)data, (size_t)len);
    if (n > 0) {
        XSslSocket_encryptedBytesWritten_signal(self, (int64_t)n);
        return (int64_t)n;
    }
    if (n == 0) return 0;
    return -1;
}

/* connectToHost：客户端模式自动记录 SNI，其余透传父类 */
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

/* disconnectFromHost：先发 close_notify，再走父类断开 */
static void xssl_v_disconnectFromHost(XAbstractSocket* sock) {
    XSslSocket* self = (XSslSocket*)sock;
    if (self->session && self->encrypted) {
        XSsl_sessionShutdown(self->session);
        self->encrypted = false;
    }
    PARENT_SOCK(EXAbstractSocket_DisconnectFromHost, Fn_disconnectFromHost)(sock);
}

/* close：与 disconnect 类似，再走父类 XIODevice.close */
static void xssl_v_close(XIODevice* io) {
    XSslSocket* self = (XSslSocket*)io;
    if (self->session && self->encrypted) {
        XSsl_sessionShutdown(self->session);
        self->encrypted = false;
    }
    PARENT_IO(EXIODevice_Close, Fn_close)(io);
}

/* skipData：跳过密文数据（对齐 QSslSocket::skipData） */
static int64_t xssl_v_skipData(XIODevice* io, int64_t maxSize) {
    XSslSocket* self = (XSslSocket*)io;
    if (!self->encrypted || !self->session) {
        /* 未加密态直接调用父类 skipData */
        typedef int64_t (*Fn_skipData)(XIODevice*, int64_t);
        return XClass_Parent(XAbstractSocket, EXIODevice_SkipData, Fn_skipData)(io, maxSize);
    }
    /* 加密态：先排空已解密数据，若不足则从 TLS 读取并丢弃 */
    if (self->handshakePending && !self->encrypted) {
        (void)xssl_pump_handshake(self);
    }
    if (!self->encrypted) return 0;
    char buf[4096];
    int64_t skipped = 0;
    while (skipped < maxSize) {
        int64_t chunk = maxSize - skipped;
        if (chunk > (int64_t)sizeof(buf)) chunk = (int64_t)sizeof(buf);
        int64_t n = xssl_v_readData(io, buf, chunk);
        if (n <= 0) break;
        skipped += n;
    }
    return skipped;
}
/* waitForConnected：等待 TCP 完成，客户端模式下置握手待定标志；
   实际握手由 waitForEncrypted 或 read/write 触发 */
static bool xssl_v_waitForConnected(XAbstractSocket* sock, int msecs) {
    bool ok = PARENT_SOCK(EXAbstractSocket_WaitForConnected, Fn_waitForConnected)(sock, msecs);
    if (!ok) return false;
    XSslSocket* self = (XSslSocket*)sock;
    if (self->mode == XSslSocket_SslClientMode) self->handshakePending = true;
    return true;
}

/* waitForReadyRead：先完成握手，再等待明文可读 */
static bool xssl_v_waitForReadyRead(XIODevice* io, int msecs) {
    XSslSocket* self = (XSslSocket*)io;
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_IO(EXIODevice_WaitForReadyRead, Fn_waitFor)(io, msecs);
    }
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (msecs < 0 ? 0 : (uint64_t)msecs);

    /* 握手阶段：推进握手，再派发事件以推进 */
    while (!self->encrypted) {
        int r = xssl_pump_handshake(self);
        if (r == XSSL_S_OK) break;
        if (r == XSSL_S_ERROR || r == XSSL_S_CLOSED) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        if (((XAbstractSocket*)self)->state != XAbstractSocket_ConnectedState) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    /* 加密阶段：等待解密后的明文出现在 IODevice 读缓冲中 */
    while (XAbstractSocket_bytesAvailable_base((XAbstractSocket*)self) == 0) {
        if (((XAbstractSocket*)self)->state != XAbstractSocket_ConnectedState) return false;
        if (msecs >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    return true;
}

/* waitForBytesWritten：透传父类；密文已写出即完成 */
static bool xssl_v_waitForBytesWritten(XIODevice* io, int msecs) {
    return PARENT_IO(EXIODevice_WaitForBytesWritten, Fn_waitFor)(io, msecs);
}

/* waitForDisconnected：透传父类 */
static bool xssl_v_waitForDisconnected(XAbstractSocket* sock, int msecs) {
    return PARENT_SOCK(EXAbstractSocket_WaitForDisconnected, Fn_waitForDisconnected)(sock, msecs);
}

/* deinit：销毁 TLS 会话、证书链缓存，再走父类析构 */
static void xssl_v_deinit(XClass* obj) {
    XSslSocket* self = (XSslSocket*)obj;
    if (self->session)         { XSsl_sessionDestroy(self->session); self->session = NULL; }
    if (self->peerVerifyName)  { XString_delete_base(self->peerVerifyName); self->peerVerifyName = NULL; }
    if (self->peerCertChain)   { XVector_delete_base((XContainer*)self->peerCertChain);   self->peerCertChain = NULL; }
    if (self->localCertChain)  { XVector_delete_base((XContainer*)self->localCertChain);  self->localCertChain = NULL; }
    if (self->handshakeErrors) { XVector_delete_base((XContainer*)self->handshakeErrors); self->handshakeErrors = NULL; }
    if (self->encRxBuf)        { XRingBuffer_delete_base((XContainer*)self->encRxBuf);    self->encRxBuf = (struct XRingBuffer*)XRingBuffer_create(16384); }
    /* localCert/privateKey/caCert 为外部引用，本类不持有 */
    XClass_Deinit_Parent(XAbstractSocket, self);
}

/* =============== 解密排空 =============== */


/* resume：恢复被中断的操作（对齐 QSslSocket::resume） */
static void xssl_v_resume(XAbstractSocket* sock) {
    XSslSocket* self = (XSslSocket*)sock;
    if (!self) return;
    /* 如果握手被中断，尝试继续推进 */
    if (self->handshakePending && !self->encrypted) {
        (void)xssl_pump_handshake(self);
    }
    /* 调用父类 resume */
    XClass_Parent(XAbstractSocket, EXAbstractSocket_Resume, void(*)(XAbstractSocket*))(sock);
}
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
    for (;;) {
        int n = XSsl_sessionRead(self->session, buf, sizeof(buf));
        if (n > 0) { XRingBuffer_write(rb, (const char*)buf, (size_t)n); any = true; continue; }
        break;
    }
    if (any) XIODevice_readyRead_signal(io);
}

/* SOCK_ACT 事件接管：在密文进入 IODevice 读缓冲之前先解密到自有 encRxBuf，
   再排空成明文写入 IODevice 读缓冲 */
static bool xssl_v_event(XAbstractSocket* sock, XEvent* e) {
    XSslSocket* self = (XSslSocket*)sock;

    /* 未加密模式：透传父类 */
    if (self->mode == XSslSocket_UnencryptedMode) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    /* 非 SOCK_ACT 事件：透传父类（如 SOCK_CLOSE 等） */
    if (e->type != XEVENT_TYPE_SOCK_ACT) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    /* 存在代理握手上下文时，走父类兼容路径（不常见） */
    if (sock->proxyHandshakeCtx) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }
    XNetworkSocketPrivate* priv = (XNetworkSocketPrivate*)sock->d_ptr;
    if (!priv || !sock->base.m_d) {
        return PARENT_SOCK(EXObject_Event, Fn_event)(sock, e);
    }

    XEventSockAct* sa = (XEventSockAct*)e;
    /* 驱动底层 IOCP/select 状态机（父类也是先调这一步） */
    XNetwork_socketHandleEvent(priv, e);

    /* Read：把底层刚读到的密文塞进 encRxBuf；不写 IODevice 读缓冲 */
    if (sa->actType & XSocketAct_Read) {
        size_t bytesTransferred = XNetwork_socketReadFinishedBytes(priv);
        if (bytesTransferred > 0 && self->encRxBuf) {
            const char* readBuf = XNetwork_socketReadBuffer(priv);
            if (readBuf) {
                XRingBuffer_write(self->encRxBuf, readBuf, bytesTransferred);
            }
        }
        /* 解密：mbedTLS 从 encRxBuf 读取密文，明文写入 IODevice 读缓冲并 emit readyRead */
        xssl_drain_encrypted(self);
        /* 继续投递下一次异步读 */
        XNetwork_socketContinueRead(priv, sock->socketType == XAbstractSocket_UdpSocket);
    }

    /* Write：明文出站已由 xssl_v_writeData 走父类 writeData 完成；
       这里只处理底层写完成事件，emit bytesWritten 让上层继续写 */
    if (sa->actType & XSocketAct_Write) {
        size_t bytesWritten = XNetwork_socketWriteFinishedBytes(priv);
        if (bytesWritten > 0) {
            XIODevice_bytesWritten_signal((XIODevice*)sock, bytesWritten);
        }
        int wch = XIODevice_currentWriteChannel((XIODevice*)sock);
        struct XRingBuffer* wrb = XIODevicePrivate_getOrCreateWriteBuffer(sock->base.m_d, wch);
        XNetwork_socketContinueWrite(priv, wrb, sock->socketType == XAbstractSocket_UdpSocket);
    }

    /* Connect：底层完成连接时切换状态 */
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


/* =============== 信号实现 =============== */

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

//void XSslSocket_setSslConfiguration(XSslSocket* self, void* config) { (void)self; (void)config; }
//void* XSslSocket_sslConfiguration(const XSslSocket* self) { (void)self; return NULL; }

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

void XSslSocket_setPeerVerifyName(XSslSocket* self, const XString* name)
{
    if (!self) return;
        if (self->peerVerifyName) { XString_delete_base(self->peerVerifyName); self->peerVerifyName = NULL; }
    if (name) self->peerVerifyName = XString_create_copy(name);
}

XString* XSslSocket_peerVerifyName(const XSslSocket* self)
{
    if (!self || !self->peerVerifyName) return NULL;
    return self->peerVerifyName;
}

/* =============== 连接与加密 =============== */

void XSslSocket_connectToHostEncrypted(XSslSocket* self, const XString* hostName, uint16_t port)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    if (hostName && !self->peerVerifyName) {
        self->peerVerifyName = XString_create_copy(hostName);
    }
    const char* hostNameUtf8 = XString_toUtf8(hostName);
    XAbstractSocket_connectToHost_base((XAbstractSocket*)self, hostNameUtf8, port,
                                       XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
}

void XSslSocket_connectToHostEncrypted_2(XSslSocket* self,
    const XString* hostName, uint16_t port,
    XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol proto)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    const char* hostNameUtf8 = XString_toUtf8(hostName);
    XAbstractSocket_connectToHost_base((XAbstractSocket*)self, hostNameUtf8, port, mode, proto);
}


/* 对齐 QSslSocket::connectToHostEncrypted(host, port, sslPeerName, mode, protocol) */
void XSslSocket_connectToHostEncrypted_3(XSslSocket* self,
    const XString* hostName, uint16_t port,
    const XString* sslPeerName,
    XIODeviceBaseMode mode, XAbstractSocket_NetworkLayerProtocol proto)
{
    if (!self) return;
    self->mode = XSslSocket_SslClientMode;
    /* 设置 SNI 名称（独立于连接主机名） */
    if (sslPeerName) {
        if (self->peerVerifyName) {
            XString_delete_base(self->peerVerifyName);
        }
        self->peerVerifyName = XString_create_copy(sslPeerName);
    }
    const char* hostNameUtf8 = XString_toUtf8(hostName);
    XAbstractSocket_connectToHost_base((XAbstractSocket*)self, hostNameUtf8, port, mode, proto);
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

XString* XSslSocket_sessionProtocol(const XSslSocket* self)
{
    if (!self || !self->session) return XString_create_utf8("Unknown");
    return XString_create_utf8(XSsl_sessionProtocolString(self->session));
}

XString* XSslSocket_sessionCipher(const XSslSocket* self)
{
    if (!self || !self->session) return NULL;
    return XString_create_utf8(XSsl_sessionCipherName(self->session));
}

//int XSslSocket_sessionCipherBits(const XSslSocket* self)
//{
//    if (!self || !self->session) return 0;
//    return 0; /* 暂未实现 */
//}
//
//int XSslSocket_sessionCipherBits_base(const XSslSocket* self)
//{
//    return XSslSocket_sessionCipherBits(self);
//}

//int XSslSocket_sessionProtocolVersion(const XSslSocket* self)
//{
//    if (!self || !self->session) return 0;
//    return 0; /* 暂未实现 */
//}

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

void XSslSocket_setPrivateKey(XSslSocket* self, XSslKey* key)
{
    if (!self) return;
    self->privateKey = key;
}

XSslKey* XSslSocket_privateKey(const XSslSocket* self)
{
    return self ? self->privateKey : NULL;
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

//XVector* XSslSocket_sessionPeerCertificateChain(const XSslSocket* self)
//{
//    return XSslSocket_peerCertificateChain(self);
//}

XVector* XSslSocket_sslHandshakeErrors(const XSslSocket* self)
{
    return self ? self->handshakeErrors : NULL;
}

void XSslSocket_ignoreSslErrors_2(XSslSocket* self, const XVector* errors)
{
    /* MVP：记录忽略意图，errors 列表暂不逐一比对 */
    (void)errors;
    if (!self) return;
    self->ignoreErrors = true;
}

void XSslSocket_ignoreSslErrors(XSslSocket* self)
{
    if (!self) return;
    self->ignoreErrors = true;
}

void XSslSocket_continueInterruptedHandshake(XSslSocket* self)
{
    /* MVP：当前握手不在 sslErrors 处中断，直接继续推进 */
    if (!self) return;
    if (self->handshakePending && !self->encrypted) {
        (void)xssl_pump_handshake(self);
    }
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

//void XSslSocket_attachPlainSocket(XSslSocket* self, XTcpSocket* plain)
//{
//    if (!self || !plain) return;
//    XAbstractSocket_setSocketDescriptor_base((XAbstractSocket*)self,
//        XAbstractSocket_fd(plain),
//        XAbstractSocket_ConnectedState,
//        XIODevice_ReadWrite);
//}


/* =============== 数据查询与访问器 =============== */

int64_t XSslSocket_encryptedBytesAvailable(const XSslSocket* self)
{
    if (!self || !self->encRxBuf) return 0;
    return (int64_t)XRingBuffer_available(self->encRxBuf);
}

int64_t XSslSocket_encryptedBytesToWrite(const XSslSocket* self)
{
    /* 已进入 TLS 加密、但尚未写到底层 TCP 的字节数：
       同步 BIO 模型下密文已即时交给父类 writeData，
       故此处等于底层 TCP 写缓冲中的待发字节数。 */
    if (!self) return 0;
    return XIODevice_bytesToWrite_base((const XIODevice*)self);
}

XSslSession* XSslSocket_session(XSslSocket* self)
{
    return self ? self->session : NULL;
}
/* =============== XSslConfiguration API 实现 =============== */

XSslConfiguration* XSslConfiguration_create(void)
{
    XSslConfiguration* cfg = (XSslConfiguration*)XMalloc_System(sizeof(XSslConfiguration));
    if (!cfg) return NULL;
    memset(cfg, 0, sizeof(XSslConfiguration));
    cfg->protocol = XSSL_SecureProtocols;
    cfg->verifyMode = XSSL_AutoVerifyPeer;
    cfg->peerVerifyDepth = -1;
    return cfg;
}

void XSslConfiguration_delete(XSslConfiguration* config)
{
    if (!config) return;
    if (config->peerVerifyName) XString_delete_base(config->peerVerifyName);
    if (config->localCertChain) XVector_delete_base((XContainer*)config->localCertChain);
    XFree_System(config);
}

XSslConfiguration* XSslConfiguration_copy(const XSslConfiguration* other)
{
    if (!other) return NULL;
    XSslConfiguration* cfg = XSslConfiguration_create();
    if (!cfg) return NULL;
    cfg->protocol = other->protocol;
    cfg->verifyMode = other->verifyMode;
    cfg->peerVerifyDepth = other->peerVerifyDepth;
    cfg->localCert = other->localCert;
    cfg->privateKey = other->privateKey;
    if (other->peerVerifyName)
        cfg->peerVerifyName = XString_create_utf8(XString_toUtf8(other->peerVerifyName));
    if (other->localCertChain)
        cfg->localCertChain = XVector_create_copy(other->localCertChain);
    return cfg;
}

XSslProtocol XSslConfiguration_protocol(const XSslConfiguration* self)
{
    return self ? self->protocol : XSSL_UnknownProtocol;
}

void XSslConfiguration_setProtocol(XSslConfiguration* self, XSslProtocol protocol)
{
    if (self) self->protocol = protocol;
}

XSslPeerVerifyMode XSslConfiguration_peerVerifyMode(const XSslConfiguration* self)
{
    return self ? self->verifyMode : XSSL_AutoVerifyPeer;
}

void XSslConfiguration_setPeerVerifyMode(XSslConfiguration* self, XSslPeerVerifyMode mode)
{
    if (self) self->verifyMode = mode;
}

int XSslConfiguration_peerVerifyDepth(const XSslConfiguration* self)
{
    return self ? self->peerVerifyDepth : -1;
}

void XSslConfiguration_setPeerVerifyDepth(XSslConfiguration* self, int depth)
{
    if (self) self->peerVerifyDepth = depth;
}

XSslCertificate* XSslConfiguration_localCertificate(const XSslConfiguration* self)
{
    return self ? self->localCert : NULL;
}

void XSslConfiguration_setLocalCertificate(XSslConfiguration* self, XSslCertificate* cert)
{
    if (self) self->localCert = cert;
}

XVector* XSslConfiguration_localCertificateChain(const XSslConfiguration* self)
{
    return self ? self->localCertChain : NULL;
}

void XSslConfiguration_setLocalCertificateChain(XSslConfiguration* self, XVector* chain)
{
    if (self) {
        if (self->localCertChain) XVector_delete_base((XContainer*)self->localCertChain);
        self->localCertChain = chain;
    }
}

XSslKey* XSslConfiguration_privateKey(const XSslConfiguration* self)
{
    return self ? self->privateKey : NULL;
}

void XSslConfiguration_setPrivateKey(XSslConfiguration* self, XSslKey* key)
{
    if (self) self->privateKey = key;
}

/* =============== XSslSocket 缺失 API 实现 =============== */

void XSslSocket_resume(XSslSocket* self)
{
    if (!self) return;
    /* 通过虚函数表调用 resume，确保多态 */
    XClassGetVirtualFunc(self, EXAbstractSocket_Resume, void(*)(XAbstractSocket*))((XAbstractSocket*)self);
}

XSslConfiguration* XSslSocket_sslConfiguration(const XSslSocket* self)
{
    if (!self) return NULL;
    XSslConfiguration* cfg = XSslConfiguration_create();
    if (!cfg) return NULL;
    cfg->protocol = self->protocol;
    cfg->verifyMode = self->verifyMode;
    cfg->peerVerifyDepth = self->peerVerifyDepth;
    cfg->localCert = self->localCert;
    cfg->privateKey = self->privateKey;
    if (self->peerVerifyName)
        cfg->peerVerifyName = XString_create_utf8(XString_toUtf8(self->peerVerifyName));
    if (self->localCertChain)
        cfg->localCertChain = XVector_create_copy(self->localCertChain);
    return cfg;
}

void XSslSocket_setSslConfiguration(XSslSocket* self, const XSslConfiguration* config)
{
    if (!self || !config) return;
    self->protocol = config->protocol;
    self->verifyMode = config->verifyMode;
    self->peerVerifyDepth = config->peerVerifyDepth;
    self->localCert = config->localCert;
    self->privateKey = config->privateKey;
    if (config->peerVerifyName) {
        if (self->peerVerifyName) XString_delete_base(self->peerVerifyName);
        self->peerVerifyName = XString_create_utf8(XString_toUtf8(config->peerVerifyName));
    }
    if (config->localCertChain) {
        if (self->localCertChain) XVector_delete_base((XContainer*)self->localCertChain);
        self->localCertChain = XVector_create_copy(config->localCertChain);
    }
}

void XSslSocket_setLocalCertificate_2(XSslSocket* self, const XString* fileName, XSslEncodingFormat format)
{
    if (!self || !fileName) return;
    const char* path = XString_toUtf8(fileName);
    if (!path) return;
    XSslCertificate* cert = XSsl_certificateLoad(path, format);
    if (cert) {
        self->localCert = cert;
    }
}

void XSslSocket_setPrivateKey_2(XSslSocket* self, const XString* fileName,
    XSslKeyAlgorithm algo, XSslEncodingFormat fmt, const XByteArray* passPhrase)
{
    if (!self || !fileName) return;
    const char* path = XString_toUtf8(fileName);
    if (!path) return;
    const char* pass = passPhrase ? (const char*)XByteArray_constData(passPhrase) : NULL;
    XSslKey* key = XSsl_keyFromPem(path, strlen(path), algo, XSSL_PrivateKey, pass);
    if (key) {
        self->privateKey = key;
    }
}

XVector* XSslSocket_ocspResponses(const XSslSocket* self)
{
    (void)self;
    /* OCSP 装订响应：当前 mbedTLS 后端未实现，返回空列表 */
    return NULL;
}


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
    XSslAlertLevel level, XSslAlertType type, const XString* description)
{
    (void)level; (void)type; (void)description;
    XEmitSignal(self, XSslSocket_alertSent_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSslSocket_alertReceived_signal(XSslSocket* self,
    XSslAlertLevel level, XSslAlertType type, const XString* description)
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
XString* XSslSocket_sslLibraryVersionString(void) { return XString_create_utf8("mbedtls"); }
long XSslSocket_sslLibraryBuildVersionNumber(void) { return 0; }
XString* XSslSocket_sslLibraryBuildVersionString(void) { return XString_create_utf8("mbedtls"); }
XVector* XSslSocket_availableBackends(void) { return NULL; }
XString* XSslSocket_activeBackend(void) { return XString_create_utf8("mbedtls"); }
bool XSslSocket_setActiveBackend(const XString* backendName) { (void)backendName; return false; }
XVector* XSslSocket_supportedProtocols(const XString* backendName) { (void)backendName; return NULL; }
bool XSslSocket_isProtocolSupported(XSslProtocol protocol, const XString* backendName) { (void)protocol; (void)backendName; return false; }

XVector* XSslSocket_implementedClasses(const XString* backendName)
{
    (void)backendName;
    /* 当前后端实现：Socket、Certificate、Key、Session */
    XVector* vec = XVector_create(sizeof(int));
    if (!vec) return NULL;
    int classes[] = {
        XSSL_ImplementedClass_Socket,
        XSSL_ImplementedClass_Certificate,
        XSSL_ImplementedClass_Key,
        XSSL_ImplementedClass_Dtls
    };
    for (int i = 0; i < 4; i++)
        XVector_push_back_1_base(vec, &classes[i]);
    return vec;
}

bool XSslSocket_isClassImplemented(XSslImplementedClass cl, const XString* backendName)
{
    (void)backendName;
    return XSsl_isClassImplemented(cl);
}

XVector* XSslSocket_supportedFeatures(const XString* backendName)
{
    (void)backendName;
    XVector* vec = XVector_create(sizeof(int));
    if (!vec) return NULL;
    int features[] = {
        XSSL_SupportedFeature_ClientSideAlpn,
        XSSL_SupportedFeature_SessionTicket,
        XSSL_SupportedFeature_Ocsp
    };
    for (int i = 0; i < 3; i++)
        XVector_push_back_1_base(vec, &features[i]);
    return vec;
}

bool XSslSocket_isFeatureSupported(XSslSupportedFeature feat, const XString* backendName)
{
    (void)backendName;
    return XSsl_isFeatureSupported(feat);
}


XVtable* XSslSocket_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractSocket))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    /* 继承 XAbstractSocket 的虚函数表 */
    XVTABLE_INHERIT_XCLASS(XAbstractSocket);

    /* IO 层：读、写、关闭、等待可读、等待写完 —— 全部由 TLS 重载 */
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_ReadData,             xssl_v_readData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WriteData,            xssl_v_writeData);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_Close,                xssl_v_close);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForReadyRead,     xssl_v_waitForReadyRead);
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_WaitForBytesWritten,  xssl_v_waitForBytesWritten);

    /* Socket 层：连接、断开、等待连接、等待断开 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_ConnectToHost,      xssl_v_connectToHost);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_DisconnectFromHost, xssl_v_disconnectFromHost);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_WaitForConnected,   xssl_v_waitForConnected);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_WaitForDisconnected,xssl_v_waitForDisconnected);

    /* IO 层：skipData */
    XVTABLE_OVERLOAD_DEFAULT(EXIODevice_SkipData,           xssl_v_skipData);

    /* Socket 层：resume */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSocket_Resume,       xssl_v_resume);

    /* Class 层：事件、析构 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, xssl_v_event);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xssl_v_deinit);

    return XVTABLE_DEFAULT;
}







