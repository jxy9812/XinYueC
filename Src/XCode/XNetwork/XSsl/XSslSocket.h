// XSslSocket.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6.8 QSslSocket，继承自 XTcpSocket（内存布局兼容）。
//
// 设计原则（对齐 XTcpSocket 已有做法）：
//   1) 结构体首成员必须是 XTcpSocket base；本类可直接强转为 XTcpSocket* /
//      XAbstractSocket* / XIODevice* / XObject* 参与整个 IO/信号体系。
//   2) 父类已有的公开 API 全部用 #define 别名暴露，本文件 **不写** 任何
//      wrapper 函数，避免符号重复和 API 漂移。
//   3) TLS 语义通过 XClass 虚函数重载完成：
//          EXIODevice_ReadData / WriteData      —— 加/解密插入到 read/write
//          EXAbstractSocket_ConnectToHost       —— 连接后按需驱动握手
//          EXAbstractSocket_DisconnectFromHost  —— 先发 close_notify
//          EXIODevice_Close                     —— 优雅关闭
//          EXAbstractSocket_WaitForConnected / EXIODevice_WaitForReadyRead
//                                               —— 同步驱动握手 + 数据泵
//      用户仍旧调 XTcpSocket_read_1 / XIODevice_read_1，走到我们的 readData
//      虚函数即得到明文；write_1 则走 writeData 加密后送出。
//   4) SSL 独有 API（Qt QSslSocket 特有的槽、setter、query、signals）
//      在本文件里以 XSslSocket_* 显式给出，与 QSslSocket 6.8 一一对应。

#ifndef XSSLSOCKET_H
#define XSSLSOCKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XTcpSocket.h"
#include "XSsl_platform.h"
#include "XSsl_session.h"
#include "XString.h"
#include "XByteArray.h"

/* =============== 类元信息（虚函数表） ================= */

XCLASS_DEFINE_BEGING(XSslSocket)
XCLASS_DEFINE_EXTEND_END(XSslSocket, XAbstractSocket)

XVtable* XSslSocket_class_init(void);

/* =============== 枚举（对齐 QSslSocket 语义） ============ */

/**
 * @brief 对齐 QSslSocket::SslMode
 */
typedef enum XSslSocket_SslMode {
    XSslSocket_UnencryptedMode = 0, /**< 未加密，等价于普通 TCP */
    XSslSocket_SslClientMode   = 1, /**< 客户端 TLS 模式 */
    XSslSocket_SslServerMode   = 2  /**< 服务端 TLS 模式 */
} XSslSocket_SslMode;

/* =============== 结构体（前向，实现私有） =============== */

typedef struct XSslSocket XSslSocket;

/* =============== 前向声明（对齐 Qt 6.8 QSslConfiguration） =============== */
typedef struct XSslConfiguration XSslConfiguration;

/* =============== 生命周期 =============== */

/**
 * @brief 初始化已分配的 XSslSocket，行为等价于 QSslSocket 构造函数。
 *        内部依次调用 XTcpSocket_init 并挂上 XSslSocket 的 vtable。
 */
void XSslSocket_init(XSslSocket* self);

/**
 * @brief 在堆上创建并初始化一个 XSslSocket 实例。
 */
XSslSocket* XSslSocket_create(void);

/* 继承自 XObject 的生命周期 */
#define XSslSocket_deleteLater              XObject_deleteLater

/* =============== 继承自 XTcpSocket / XAbstractSocket / XIODevice 的 API
 *                （全部走 #define 别名，避免重复实现） =============== */

/* --- XIODevice --- */
#define XSslSocket_open_base                XIODevice_open_base
#define XSslSocket_close_base               XIODevice_close_base
#define XSslSocket_isOpen                   XIODevice_isOpen
#define XSslSocket_isReadable               XIODevice_isReadable
#define XSslSocket_isWritable               XIODevice_isWritable
#define XSslSocket_isSequential             XIODevice_isSequential
#define XSslSocket_atEnd_base               XIODevice_atEnd_base
#define XSslSocket_bytesAvailable_base      XIODevice_bytesAvailable_base
#define XSslSocket_bytesToWrite_base        XIODevice_bytesToWrite_base
#define XSslSocket_canReadLine_base         XIODevice_canReadLine_base
#define XSslSocket_read_1                   XIODevice_read_1     /* 明文（虚函数 readData 解密） */
#define XSslSocket_write_1                  XIODevice_write_1    /* 明文（虚函数 writeData 加密） */
#define XSslSocket_readAll_2                XIODevice_readAll_3
#define XSslSocket_readLine_1               XIODevice_readLine_1
#define XSslSocket_peek_1                   XIODevice_peek_1
#define XSslSocket_waitForReadyRead_base    XIODevice_waitForReadyRead_base
#define XSslSocket_waitForBytesWritten_base XIODevice_waitForBytesWritten_base

/* --- XAbstractSocket --- */
#define XSslSocket_connectToHost_base       XAbstractSocket_connectToHost_base
#define XSslSocket_disconnectFromHost_base  XAbstractSocket_disconnectFromHost_base
#define XSslSocket_waitForConnected_base    XAbstractSocket_waitForConnected_base
#define XSslSocket_waitForDisconnected_base XAbstractSocket_waitForDisconnected_base
#define XSslSocket_state                    XAbstractSocket_state
#define XSslSocket_error                    XAbstractSocket_error
#define XSslSocket_errorString              XAbstractSocket_errorString
#define XSslSocket_isValid                  XAbstractSocket_isValid
#define XSslSocket_localAddress             XAbstractSocket_localAddress
#define XSslSocket_localPort                XAbstractSocket_localPort
#define XSslSocket_peerAddress              XAbstractSocket_peerAddress
#define XSslSocket_peerPort                 XAbstractSocket_peerPort
#define XSslSocket_peerName                 XAbstractSocket_peerName
#define XSslSocket_socketDescriptor_base    XAbstractSocket_socketDescriptor_base
#define XSslSocket_setSocketOption_base     XAbstractSocket_setSocketOption_base
#define XSslSocket_socketOption_base        XAbstractSocket_socketOption_base
#define XSslSocket_readBufferSize           XAbstractSocket_readBufferSize
#define XSslSocket_setReadBufferSize_base   XAbstractSocket_setReadBufferSize_base
#define XSslSocket_flush                    XAbstractSocket_flush
#define XSslSocket_abort                    XAbstractSocket_abort
#define XSslSocket_fd                       XAbstractSocket_fd

/* --- 继承信号（连接/断开/错误/状态/读写） --- */
#define XSslSocket_hostFound_signal         XAbstractSocket_hostFound_signal
#define XSslSocket_connected_signal         XAbstractSocket_connected_signal
#define XSslSocket_disconnected_signal      XAbstractSocket_disconnected_signal
#define XSslSocket_stateChanged_signal      XAbstractSocket_stateChanged_signal
#define XSslSocket_errorOccurred_signal     XAbstractSocket_errorOccurred_signal
#define XSslSocket_readyRead_signal         XIODevice_readyRead_signal
#define XSslSocket_bytesWritten_signal      XIODevice_bytesWritten_signal
#define XSslSocket_aboutToClose_signal      XIODevice_aboutToClose_signal

/* =============== QSslSocket 新增 API（本类特有） =============== */

/**
 * @brief 对齐 QSslSocket::connectToHostEncrypted()（重载1）。
 *        执行 TCP 连接并在连接完成后启动客户端 TLS 握手。
 */
void XSslSocket_connectToHostEncrypted(XSslSocket* self,
                                       const XString* hostName,
                                       uint16_t port);

/**
 * @brief 对齐 QSslSocket::connectToHostEncrypted()（重载2，带 mode/protocol）。
 */
void XSslSocket_connectToHostEncrypted_2(XSslSocket* self,
                                       const XString* hostName,
                                         uint16_t port,
                                         XIODeviceBaseMode mode,
                                         XAbstractSocket_NetworkLayerProtocol proto);

/**
 * @brief 对齐 QSslSocket::connectToHostEncrypted()（重载3，带 sslPeerName）。
 *        指定 SNI 名称独立于连接主机名。
 */
void XSslSocket_connectToHostEncrypted_3(XSslSocket* self,
                                         const XString* hostName,
                                         uint16_t port,
                                         const XString* sslPeerName,
                                         XIODeviceBaseMode mode,
                                         XAbstractSocket_NetworkLayerProtocol proto);

/** 对齐 QSslSocket::startClientEncryption() —— 在已建立的明文连接上做客户端握手。 */
void XSslSocket_startClientEncryption(XSslSocket* self);

/** 对齐 QSslSocket::startServerEncryption() —— 服务端 accept 后在明文连接上做服务端握手。 */
void XSslSocket_startServerEncryption(XSslSocket* self);

/** 对齐 QSslSocket::ignoreSslErrors() —— 忽略此后的证书校验错误，握手照常继续。 */
void XSslSocket_ignoreSslErrors(XSslSocket* self);

/** 对齐 QSslSocket::resume() —— 恢复被中断的握手或代理认证。 */
void XSslSocket_resume(XSslSocket* self);

/* --- 配置 setter / getter（对齐 QSslSocket）--- */
void         XSslSocket_setProtocol(XSslSocket* self, XSslProtocol protocol);
XSslProtocol XSslSocket_protocol(const XSslSocket* self);

void                XSslSocket_setLocalCertificate(XSslSocket* self, XSslCertificate* cert);
XSslCertificate*    XSslSocket_localCertificate(const XSslSocket* self);

/** 对齐 QSslSocket::setLocalCertificate(fileName, format) —— 从文件加载证书。 */
void XSslSocket_setLocalCertificate_2(XSslSocket* self, const XString* fileName, XSslEncodingFormat format);

void      XSslSocket_setPrivateKey(XSslSocket* self, XSslKey* key);
XSslKey*  XSslSocket_privateKey(const XSslSocket* self);

/** 对齐 QSslSocket::setPrivateKey(fileName, algo, format, passPhrase) —— 从文件加载私钥。 */
void XSslSocket_setPrivateKey_2(XSslSocket* self, const XString* fileName,
                                XSslKeyAlgorithm algo, XSslEncodingFormat fmt,
                                const XByteArray* passPhrase);

/** 追加 CA 证书用于校验对端。可多次调用累积。 */
void XSslSocket_addCaCertificate(XSslSocket* self, XSslCertificate* ca);

void               XSslSocket_setPeerVerifyMode(XSslSocket* self, XSslPeerVerifyMode mode);
XSslPeerVerifyMode XSslSocket_peerVerifyMode(const XSslSocket* self);

void        XSslSocket_setPeerVerifyName(XSslSocket* self, const XString* name);
XString* XSslSocket_peerVerifyName(const XSslSocket* self);

void XSslSocket_setPeerVerifyDepth(XSslSocket* self, int depth);
int  XSslSocket_peerVerifyDepth(const XSslSocket* self);

/* --- 查询（对齐 QSslSocket）--- */
XSslSocket_SslMode XSslSocket_mode(const XSslSocket* self);
bool               XSslSocket_isEncrypted(const XSslSocket* self);
XString* XSslSocket_sessionProtocol(const XSslSocket* self); /* "TLSv1.2"/"TLSv1.3" */
XString* XSslSocket_sessionCipher(const XSslSocket* self);   /* cipher suite name */

/* --- 同步等待（QSslSocket 新增的加密等待） --- */

/** 对齐 QSslSocket::waitForEncrypted()。默认 30000ms（Qt 默认）。 */
bool XSslSocket_waitForEncrypted(XSslSocket* self, int msecs);

/* 其它 waitFor* 直接沿用父类：waitForConnected / waitForReadyRead /
 * waitForBytesWritten / waitForDisconnected —— 我们在 vtable 里已重载它们，
 * 会自动驱动 TLS 握手。所以用户仍旧调 XTcpSocket_waitForReadyRead_base 等。 */

/* --- 平台后端句柄（调试/进阶用途） --- */
XSslSession* XSslSocket_session(XSslSocket* self);

/* =============== 新增信号（对齐 QSslSocket 特有信号） =============== */

/** 对齐 QSslSocket::encrypted() —— 握手成功、进入加密态时发射。 */
void* XSslSocket_encrypted_signal(XSslSocket* self);

/** 对齐 QSslSocket::modeChanged(QSslSocket::SslMode) —— SslMode 改变时发射。 */
void* XSslSocket_modeChanged_signal(XSslSocket* self, XSslSocket_SslMode newMode);

/** 对齐 QSslSocket::sslErrors(const QList<QSslError>&) —— 传递证书链校验错误码。 */
void* XSslSocket_sslErrors_signal(XSslSocket* self, int errorCode);

/** 对齐 QSslSocket::peerVerifyError(const QSslError&) —— 单个证书错误（非致命）。 */
void* XSslSocket_peerVerifyError_signal(XSslSocket* self, int errorCode);

/** 对齐 QSslSocket::encryptedBytesWritten(qint64) —— 密文层已经写出的字节数。 */
void* XSslSocket_encryptedBytesWritten_signal(XSslSocket* self, int64_t bytes);


// =============== Qt 6.8 API 对齐（补齐） ===============

/** encryptedBytesAvailable —— 底层 TCP 已接收但尚未被 TLS 解密的字节数。 */
int64_t XSslSocket_encryptedBytesAvailable(const XSslSocket* self);
/** encryptedBytesToWrite —— 已进入 TLS 但尚未写到底层 TCP 的字节数。 */
int64_t XSslSocket_encryptedBytesToWrite(const XSslSocket* self);

/** 设置本地证书链（首元素等价于 setLocalCertificate）。self 拥有传入 XVector 的所有权。 */
void XSslSocket_setLocalCertificateChain(XSslSocket* self, XVector* chain);
/** 获取本地证书链（可能为 NULL）。 */
XVector* XSslSocket_localCertificateChain(const XSslSocket* self);

/** 握手完成后的对端证书；未连接返回 NULL。 */
XSslCertificate* XSslSocket_peerCertificate(const XSslSocket* self);
/** 对端证书链（叶证书在首）。 */
XVector*         XSslSocket_peerCertificateChain(const XSslSocket* self);

/** 握手期间收集到的 TLS 错误（每个元素为 int 错误码）。 */
XVector* XSslSocket_sslHandshakeErrors(const XSslSocket* self);

/** 忽略指定的一组 SSL 错误（对齐 QSslSocket::ignoreSslErrors(QList<QSslError>&)）。 */
void XSslSocket_ignoreSslErrors_2(XSslSocket* self, const XVector* errors);
/** 恢复被 emit sslErrors 中断的握手（对齐 QSslSocket::continueInterruptedHandshake）。 */
void XSslSocket_continueInterruptedHandshake(XSslSocket* self);

/** 获取 OCSP 装订响应列表（对齐 QSslSocket::ocspResponses()）。 */
XVector* XSslSocket_ocspResponses(const XSslSocket* self);

/** 获取/设置 SSL 配置快照（对齐 QSslSocket::sslConfiguration / setSslConfiguration）。 */
XSslConfiguration* XSslSocket_sslConfiguration(const XSslSocket* self);
void XSslSocket_setSslConfiguration(XSslSocket* self, const XSslConfiguration* config);

// ---- 静态 / 全局查询（对齐 QSslSocket 静态成员）----
bool          XSslSocket_supportsSsl(void);
long          XSslSocket_sslLibraryVersionNumber(void);
XString* XSslSocket_sslLibraryVersionString(void);
long          XSslSocket_sslLibraryBuildVersionNumber(void);
XString* XSslSocket_sslLibraryBuildVersionString(void);
XVector*      XSslSocket_availableBackends(void);
XString* XSslSocket_activeBackend(void);
bool          XSslSocket_setActiveBackend(const XString* backendName);
XVector*      XSslSocket_supportedProtocols(const XString* backendName);
bool          XSslSocket_isProtocolSupported(XSslProtocol protocol, const XString* backendName);
XVector*      XSslSocket_implementedClasses(const XString* backendName);
bool          XSslSocket_isClassImplemented(XSslImplementedClass cl, const XString* backendName);
XVector*      XSslSocket_supportedFeatures(const XString* backendName);
bool          XSslSocket_isFeatureSupported(XSslSupportedFeature feat, const XString* backendName);

// ---- Qt6.8 新增信号 ----
void* XSslSocket_newSessionTicketReceived_signal(XSslSocket* self);
void* XSslSocket_alertSent_signal(XSslSocket* self, XSslAlertLevel level, XSslAlertType type, const XString* description);
void* XSslSocket_alertReceived_signal(XSslSocket* self, XSslAlertLevel level, XSslAlertType type, const XString* description);
void* XSslSocket_handshakeInterruptedOnError_signal(XSslSocket* self, int errorCode);
void* XSslSocket_preSharedKeyAuthenticationRequired_signal(XSslSocket* self, void* authenticator);

// ---- 对齐 XAbstractSocket 新增的按地址直连 ----
#define XSslSocket_connectToHostByAddress   XAbstractSocket_connectToHostByAddress
#define XSslSocket_proxyAuthenticationRequired_signal XAbstractSocket_proxyAuthenticationRequired_signal


// =============== XSslConfiguration API（对齐 Qt 6.8 QSslConfiguration） ===============

/** 创建并初始化一个 XSslConfiguration 实例。 */
XSslConfiguration* XSslConfiguration_create(void);
/** 销毁 XSslConfiguration 实例并释放持有的资源。 */
void XSslConfiguration_delete(XSslConfiguration* config);
/** 深拷贝一个 XSslConfiguration 实例。 */
XSslConfiguration* XSslConfiguration_copy(const XSslConfiguration* other);

XSslProtocol XSslConfiguration_protocol(const XSslConfiguration* self);
void XSslConfiguration_setProtocol(XSslConfiguration* self, XSslProtocol protocol);

XSslPeerVerifyMode XSslConfiguration_peerVerifyMode(const XSslConfiguration* self);
void XSslConfiguration_setPeerVerifyMode(XSslConfiguration* self, XSslPeerVerifyMode mode);

int XSslConfiguration_peerVerifyDepth(const XSslConfiguration* self);
void XSslConfiguration_setPeerVerifyDepth(XSslConfiguration* self, int depth);

XSslCertificate* XSslConfiguration_localCertificate(const XSslConfiguration* self);
void XSslConfiguration_setLocalCertificate(XSslConfiguration* self, XSslCertificate* cert);

XVector* XSslConfiguration_localCertificateChain(const XSslConfiguration* self);
void XSslConfiguration_setLocalCertificateChain(XSslConfiguration* self, XVector* chain);

XSslKey* XSslConfiguration_privateKey(const XSslConfiguration* self);
void XSslConfiguration_setPrivateKey(XSslConfiguration* self, XSslKey* key);


#ifdef __cplusplus
}
#endif

#endif /* XSSLSOCKET_H */