/**
 * @file XNetworkProxyHandshake.h
 * @brief 代理握手协议实现（对齐Qt 6.8）
 * 
 * 本模块完整实现Qt 6.8 QNetworkProxy的所有代理协议：
 * 
 * ============================================================================
 * SOCKS5 (RFC 1928) 完整实现：
 * ============================================================================
 * - CONNECT 命令：TCP隧道连接
 * - BIND 命令：反向连接（FTP被动模式）
 * - UDP ASSOCIATE：UDP隧道
 * - 认证方法：
 *   - 无认证 (0x00)
 *   - GSSAPI (0x01)
 *   - 用户名/密码 (0x02, RFC 1929)
 * 
 * ============================================================================
 * HTTP CONNECT 完整实现：
 * ============================================================================
 * - HTTP/1.1 CONNECT 方法
 * - 认证方式：
 *   - Basic (RFC 7617)
 *   - Digest (RFC 7616)
 *   - NTLM (Windows)
 *   - Negotiate/SPNEGO (Kerberos)
 * 
 * ============================================================================
 * 系统代理获取：
 * ============================================================================
 * - Windows: WinHttpGetProxyForUrl / 注册表
 * - Linux: 环境变量 (http_proxy, https_proxy, no_proxy)
 * - macOS: CFProxySupport
 * 
 * ============================================================================
 * 使用方式：
 * ============================================================================
 * 1. 同步握手：
 *    XAbstractSocket_connectToHost() -> 自动检测代理 -> 执行握手
 * 
 * 2. 异步握手：
 *    XNetworkProxyHandshake_start() -> 监听socket事件 -> 
 *    XNetworkProxyHandshake_process() -> 完成回调
 */

#ifndef XNETWORKPROXYHANDSHAKE_H
#define XNETWORKPROXYHANDSHAKE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XNetworkProxy.h"
#include "XHostAddress.h"
#include <stdint.h>
#include <stdbool.h>

// =============== 前向声明 ===============

typedef struct XAbstractSocket XAbstractSocket;

// =============== SOCKS5 常量定义 (RFC 1928) ===============

/** SOCKS5 版本号 */
#define XSOCKS5_VERSION                     0x05

/** SOCKS5 认证方法 */
typedef enum XSocks5AuthMethod {
    XSocks5Auth_NoAuth = 0x00,              ///< 无需认证
    XSocks5Auth_GSSAPI = 0x01,              ///< GSSAPI
    XSocks5Auth_UsernamePassword = 0x02,    ///< 用户名/密码 (RFC 1929)
    XSocks5Auth_NoAcceptable = 0xFF         ///< 无可接受的认证方法
} XSocks5AuthMethod;

/** SOCKS5 命令 */
typedef enum XSocks5Command {
    XSocks5Cmd_Connect = 0x01,              ///< CONNECT: 建立TCP连接
    XSocks5Cmd_Bind = 0x02,                 ///< BIND: 绑定端口等待连接
    XSocks5Cmd_UdpAssociate = 0x03          ///< UDP ASSOCIATE: 建立UDP隧道
} XSocks5Command;

/** SOCKS5 地址类型 */
typedef enum XSocks5AddressType {
    XSocks5Atyp_IPv4 = 0x01,                ///< IPv4地址 (4字节)
    XSocks5Atyp_DomainName = 0x03,          ///< 域名 (1字节长度 + 域名)
    XSocks5Atyp_IPv6 = 0x04                 ///< IPv6地址 (16字节)
} XSocks5AddressType;

/** SOCKS5 回复码 */
typedef enum XSocks5ReplyCode {
    XSocks5Rep_Succeeded = 0x00,            ///< 成功
    XSocks5Rep_GeneralFailure = 0x01,       ///< 一般SOCKS服务器失败
    XSocks5Rep_ConnectionNotAllowed = 0x02, ///< 连接不被规则允许
    XSocks5Rep_NetworkUnreachable = 0x03,   ///< 网络不可达
    XSocks5Rep_HostUnreachable = 0x04,      ///< 主机不可达
    XSocks5Rep_ConnectionRefused = 0x05,    ///< 连接被拒绝
    XSocks5Rep_TTLExpired = 0x06,           ///< TTL过期
    XSocks5Rep_CommandNotSupported = 0x07,  ///< 命令不支持
    XSocks5Rep_AddressNotSupported = 0x08   ///< 地址类型不支持
} XSocks5ReplyCode;

// =============== HTTP 认证类型 ===============

/**
 * @brief HTTP代理认证类型
 */
typedef enum XHttpProxyAuthType {
    XHttpProxyAuth_None = 0,                ///< 无认证
    XHttpProxyAuth_Basic = 1,               ///< Basic认证
    XHttpProxyAuth_Digest = 2,              ///< Digest认证
    XHttpProxyAuth_NTLM = 3,                ///< NTLM认证 (Windows)
    XHttpProxyAuth_Negotiate = 4            ///< Negotiate/SPNEGO (Kerberos)
} XHttpProxyAuthType;

// =============== 代理握手状态 ===============

/**
 * @brief 代理握手状态机
 */
typedef enum XProxyHandshakeState {
    XProxyHandshakeState_None = 0,              ///< 未开始握手
    XProxyHandshakeState_ConnectingToProxy,     ///< 正在连接代理服务器
    
    /* SOCKS5 状态 */
    XProxyHandshakeState_Socks5_Greeting,       ///< 发送认证方法协商
    XProxyHandshakeState_Socks5_WaitGreeting,   ///< 等待认证方法响应
    XProxyHandshakeState_Socks5_Authenticating, ///< 发送用户名/密码
    XProxyHandshakeState_Socks5_WaitAuth,       ///< 等待认证结果
    XProxyHandshakeState_Socks5_GSSAPI,         ///< GSSAPI认证中
    XProxyHandshakeState_Socks5_Requesting,     ///< 发送CONNECT/BIND/UDP请求
    XProxyHandshakeState_Socks5_WaitReply,      ///< 等待请求响应
    XProxyHandshakeState_Socks5_BindWaiting,    ///< BIND: 等待第二次连接
    
    /* HTTP CONNECT 状态 */
    XProxyHandshakeState_Http_Connecting,       ///< 发送CONNECT请求
    XProxyHandshakeState_Http_WaitResponse,     ///< 等待HTTP响应
    XProxyHandshakeState_Http_Authenticating,   ///< 发送认证请求
    XProxyHandshakeState_Http_ChunkedBody,      ///< 读取分块响应体
    
    /* 最终状态 */
    XProxyHandshakeState_Completed,             ///< 握手完成
    XProxyHandshakeState_Failed                 ///< 握手失败
} XProxyHandshakeState;

// =============== 代理握手错误码 ===============

/**
 * @brief 代理握手错误码
 */
typedef enum XProxyHandshakeError {
    XProxyHandshakeError_None = 0,                  ///< 无错误
    XProxyHandshakeError_Timeout = 1,               ///< 操作超时
    XProxyHandshakeError_ConnectionRefused = 2,     ///< 代理连接被拒绝
    XProxyHandshakeError_ConnectionClosed = 3,      ///< 代理连接被关闭
    XProxyHandshakeError_ProxyNotFound = 4,         ///< 代理服务器未找到
    XProxyHandshakeError_InvalidResponse = 5,       ///< 无效的代理响应
    XProxyHandshakeError_AuthFailed = 6,            ///< 认证失败
    XProxyHandshakeError_AuthRequired = 7,          ///< 需要认证
    XProxyHandshakeError_MethodNotSupported = 8,    ///< 不支持的认证方法
    XProxyHandshakeError_Socks5GeneralFailure = 9,  ///< SOCKS5一般失败
    XProxyHandshakeError_Socks5NotAllowed = 10,     ///< SOCKS5连接不允许
    XProxyHandshakeError_Socks5NetworkUnreachable = 11, ///< SOCKS5网络不可达
    XProxyHandshakeError_Socks5HostUnreachable = 12,///< SOCKS5主机不可达
    XProxyHandshakeError_Socks5ConnectionRefused = 13, ///< SOCKS5连接被拒绝
    XProxyHandshakeError_Socks5TTLExpired = 14,     ///< SOCKS5 TTL过期
    XProxyHandshakeError_Socks5CommandUnsupported = 15, ///< SOCKS5命令不支持
    XProxyHandshakeError_Socks5AddressUnsupported = 16, ///< SOCKS5地址不支持
    XProxyHandshakeError_HttpError = 17,            ///< HTTP错误响应
    XProxyHandshakeError_Unknown = 99               ///< 未知错误
} XProxyHandshakeError;

// =============== SOCKS5 绑定结果 ===============

/**
 * @brief SOCKS5 BIND 操作结果
 */
typedef struct XSocks5BindResult {
    XHostAddress bindAddress;           ///< 代理绑定的地址
    uint16_t bindPort;                  ///< 代理绑定的端口
    bool hasSecondConnection;           ///< 是否有第二个连接（用于FTP）
} XSocks5BindResult;

// =============== UDP 隧道信息 ===============

/**
 * @brief SOCKS5 UDP ASSOCIATE 结果
 */
typedef struct XSocks5UdpAssociateResult {
    XHostAddress relayAddress;          ///< UDP中继地址
    uint16_t relayPort;                 ///< UDP中继端口
    int udpSocket;                      ///< UDP套接字描述符
} XSocks5UdpAssociateResult;

// =============== HTTP Digest 认证参数 ===============

/**
 * @brief HTTP Digest 认证参数
 */
typedef struct XHttpDigestParams {
    char* realm;                        ///< 认证域
    char* nonce;                        ///< 服务器nonce
    char* opaque;                       ///< 透明值
    char* algorithm;                    ///< 算法 (MD5, MD5-sess, SHA-256等)
    char* qop;                          ///< 质量保护 (auth, auth-int)
    char* cnonce;                       ///< 客户端nonce
    int nc;                             ///< nonce计数
} XHttpDigestParams;

// =============== NTLM 认证上下文 ===============

/**
 * @brief NTLM 认证上下文
 */
typedef struct XNtlmContext {
    uint8_t* type1Message;              ///< Type-1消息
    size_t type1Length;                 ///< Type-1消息长度
    uint8_t* type2Message;              ///< Type-2消息（服务器响应）
    size_t type2Length;                 ///< Type-2消息长度
    uint8_t* type3Message;              ///< Type-3消息
    size_t type3Length;                 ///< Type-3消息长度
    char* workstation;                  ///< 工作站名
    char* domain;                       ///< 域名
} XNtlmContext;

// =============== 代理握手上下文 ===============

/**
 * @brief 代理握手上下文，保存握手过程中的所有状态
 */
typedef struct XProxyHandshakeContext {
    /* 基本配置 */
    XProxyHandshakeState state;         ///< 当前状态
    XNetworkProxy* proxy;               ///< 代理配置（不拥有）
    char* targetHost;                   ///< 目标主机名
    uint16_t targetPort;                ///< 目标端口
    XSocks5Command socks5Command;       ///< SOCKS5命令类型
    
    /* SOCKS5 状态 */
    XSocks5AuthMethod socks5AuthMethod; ///< 协商的认证方法
    uint8_t socks5Buffer[520];          ///< SOCKS5缓冲区（最大地址+端口）
    size_t socks5BufferLen;             ///< 缓冲区数据长度
    size_t socks5BytesNeeded;           ///< 需要接收的字节数
    
    /* SOCKS5 BIND 状态 */
    XSocks5BindResult bindResult;       ///< BIND操作结果
    
    /* SOCKS5 UDP ASSOCIATE 状态 */
    XSocks5UdpAssociateResult udpResult;///< UDP ASSOCIATE结果
    
    /* HTTP 状态 */
    char* httpBuffer;                   ///< HTTP响应缓冲区
    size_t httpBufferSize;              ///< 缓冲区大小
    size_t httpBufferLen;               ///< 已接收数据长度
    int httpResponseCode;               ///< HTTP响应码
    char* httpAuthHeader;               ///< WWW-Authenticate头
    XHttpProxyAuthType httpAuthType;    ///< HTTP认证类型
    
    /* HTTP Digest 参数 */
    XHttpDigestParams digestParams;     ///< Digest认证参数
    
    /* NTLM 上下文 */
    XNtlmContext ntlmContext;           ///< NTLM认证上下文
    
    /* GSSAPI 上下文 */
    void* gssContext;                   ///< GSSAPI上下文（平台相关）
    
    /* 错误信息 */
    XProxyHandshakeError errorCode;     ///< 错误码
    char errorMessage[256];             ///< 错误描述
    
    /* 超时控制 */
    int64_t startTime;                  ///< 开始时间（毫秒）
    int timeoutMs;                      ///< 超时时间（毫秒）
} XProxyHandshakeContext;

// =============== 核心API ===============

/**
 * @brief 创建代理握手上下文
 * @param proxy 代理配置
 * @param targetHost 目标主机名
 * @param targetPort 目标端口
 * @return 握手上下文，失败返回NULL
 */
XProxyHandshakeContext* XNetworkProxyHandshake_createContext(
    XNetworkProxy* proxy,
    const char* targetHost,
    uint16_t targetPort
);

/**
 * @brief 创建SOCKS5 BIND握手上下文
 * @param proxy 代理配置
 * @param bindPort 本地绑定端口（0表示自动分配）
 * @return 握手上下文，失败返回NULL
 */
XProxyHandshakeContext* XNetworkProxyHandshake_createBindContext(
    XNetworkProxy* proxy,
    uint16_t bindPort
);

/**
 * @brief 创建SOCKS5 UDP ASSOCIATE握手上下文
 * @param proxy 代理配置
 * @return 握手上下文，失败返回NULL
 */
XProxyHandshakeContext* XNetworkProxyHandshake_createUdpContext(
    XNetworkProxy* proxy
);

/**
 * @brief 销毁代理握手上下文
 * @param ctx 握手上下文
 */
void XNetworkProxyHandshake_destroyContext(XProxyHandshakeContext* ctx);

/**
 * @brief 设置握手超时时间
 * @param ctx 握手上下文
 * @param timeoutMs 超时毫秒数，-1表示无限等待
 */
void XNetworkProxyHandshake_setTimeout(XProxyHandshakeContext* ctx, int timeoutMs);

/**
 * @brief 执行代理握手（同步阻塞）
 * @param sock 已连接到代理服务器的套接字
 * @param ctx 握手上下文
 * @return 成功返回true
 */
bool XNetworkProxyHandshake_perform(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
);

/**
 * @brief 开始异步代理握手
 * @param sock 已连接到代理服务器的套接字
 * @param ctx 握手上下文
 * @return 成功返回true
 */
bool XNetworkProxyHandshake_start(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
);

/**
 * @brief 处理异步握手数据
 * @param sock 套接字
 * @param ctx 握手上下文
 * @return 返回握手状态
 */
XProxyHandshakeState XNetworkProxyHandshake_process(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
);

/**
 * @brief 检查握手是否完成
 * @param ctx 握手上下文
 * @return 完成返回true
 */
bool XNetworkProxyHandshake_isCompleted(const XProxyHandshakeContext* ctx);

/**
 * @brief 检查握手是否失败
 * @param ctx 握手上下文
 * @return 失败返回true
 */
bool XNetworkProxyHandshake_isFailed(const XProxyHandshakeContext* ctx);

/**
 * @brief 获取握手错误码
 * @param ctx 握手上下文
 * @return 错误码
 */
XProxyHandshakeError XNetworkProxyHandshake_errorCode(const XProxyHandshakeContext* ctx);

/**
 * @brief 获取握手错误描述
 * @param ctx 握手上下文
 * @return 错误描述字符串
 */
const char* XNetworkProxyHandshake_errorMessage(const XProxyHandshakeContext* ctx);

/**
 * @brief 获取SOCKS5 BIND结果
 * @param ctx 握手上下文
 * @return BIND结果指针，失败返回NULL
 */
const XSocks5BindResult* XNetworkProxyHandshake_bindResult(const XProxyHandshakeContext* ctx);

/**
 * @brief 获取UDP ASSOCIATE结果
 * @param ctx 握手上下文
 * @return UDP结果指针，失败返回NULL
 */
const XSocks5UdpAssociateResult* XNetworkProxyHandshake_udpResult(const XProxyHandshakeContext* ctx);

// =============== SOCKS5 协议API ===============

/**
 * @brief 构建SOCKS5认证方法协商请求
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param methods 支持的认证方法数组
 * @param methodCount 方法数量
 * @return 请求长度，失败返回-1
 */
int XSocks5_buildGreetingRequest(
    uint8_t* buffer,
    size_t bufferSize,
    const XSocks5AuthMethod* methods,
    size_t methodCount
);

/**
 * @brief 解析SOCKS5认证方法响应
 * @param response 响应数据
 * @param responseLen 响应长度
 * @param outMethod 输出选中的认证方法
 * @return 成功返回true
 */
bool XSocks5_parseGreetingResponse(
    const uint8_t* response,
    size_t responseLen,
    XSocks5AuthMethod* outMethod
);

/**
 * @brief 构建SOCKS5用户名/密码认证请求 (RFC 1929)
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param username 用户名
 * @param password 密码
 * @return 请求长度，失败返回-1
 */
int XSocks5_buildAuthRequest(
    uint8_t* buffer,
    size_t bufferSize,
    const char* username,
    const char* password
);

/**
 * @brief 解析SOCKS5认证响应
 * @param response 响应数据
 * @param responseLen 响应长度
 * @return 认证成功返回true
 */
bool XSocks5_parseAuthResponse(
    const uint8_t* response,
    size_t responseLen
);

/**
 * @brief 构建SOCKS5请求 (CONNECT/BIND/UDP ASSOCIATE)
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param command 命令类型
 * @param host 目标主机（可为NULL表示UDP ASSOCIATE）
 * @param port 目标端口
 * @return 请求长度，失败返回-1
 */
int XSocks5_buildRequest(
    uint8_t* buffer,
    size_t bufferSize,
    XSocks5Command command,
    const char* host,
    uint16_t port
);

/**
 * @brief 解析SOCKS5响应
 * @param response 响应数据
 * @param responseLen 响应长度
 * @param outReplyCode 输出回复码
 * @param outBindAddress 输出绑定地址（可为NULL）
 * @param outBindPort 输出绑定端口（可为NULL）
 * @return 成功返回true
 */
bool XSocks5_parseReply(
    const uint8_t* response,
    size_t responseLen,
    XSocks5ReplyCode* outReplyCode,
    XHostAddress* outBindAddress,
    uint16_t* outBindPort
);

/**
 * @brief 构建SOCKS5 UDP数据报
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param data 数据
 * @param dataLen 数据长度
 * @param fragNumber 分片号（0表示不分片）
 * @param atyp 地址类型
 * @param host 目标主机
 * @param port 目标端口
 * @return 数据报长度，失败返回-1
 */
int XSocks5_buildUdpDatagram(
    uint8_t* buffer,
    size_t bufferSize,
    const uint8_t* data,
    size_t dataLen,
    uint8_t fragNumber,
    XSocks5AddressType atyp,
    const char* host,
    uint16_t port
);

/**
 * @brief 解析SOCKS5 UDP数据报
 * @param buffer 输入缓冲区
 * @param bufferLen 缓冲区长度
 * @param outData 输出数据指针（指向buffer内部）
 * @param outDataLen 输出数据长度
 * @param outHost 输出源主机
 * @param outPort 输出源端口
 * @return 成功返回true
 */
bool XSocks5_parseUdpDatagram(
    const uint8_t* buffer,
    size_t bufferLen,
    const uint8_t** outData,
    size_t* outDataLen,
    char* outHost,
    uint16_t* outPort
);

// =============== HTTP CONNECT 协议API ===============

/**
 * @brief 构建HTTP CONNECT请求
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param host 目标主机
 * @param port 目标端口
 * @param proxy 代理配置（用于认证）
 * @param authType 认证类型（None表示初始请求）
 * @param authHeader 认证头值（可为NULL）
 * @return 请求长度，失败返回-1
 */
int XHttpProxy_buildConnectRequest(
    char* buffer,
    size_t bufferSize,
    const char* host,
    uint16_t port,
    const XNetworkProxy* proxy,
    XHttpProxyAuthType authType,
    const char* authHeader
);

/**
 * @brief 解析HTTP CONNECT响应
 * @param response 响应数据
 * @param responseLen 响应长度
 * @param outCode 输出HTTP状态码
 * @param outHeaders 输出响应头（可为NULL）
 * @param outAuthHeader 输出WWW-Authenticate头（可为NULL）
 * @return 成功返回true
 */
bool XHttpProxy_parseResponse(
    const char* response,
    size_t responseLen,
    int* outCode,
    char** outHeaders,
    char** outAuthHeader
);

/**
 * @brief 解析WWW-Authenticate头
 * @param authHeader WWW-Authenticate头值
 * @param outAuthType 输出认证类型
 * @param outParams 输出Digest参数（仅Digest类型）
 * @return 成功返回true
 */
bool XHttpProxy_parseAuthHeader(
    const char* authHeader,
    XHttpProxyAuthType* outAuthType,
    XHttpDigestParams* outParams
);

/**
 * @brief 构建HTTP Basic认证头
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param username 用户名
 * @param password 密码
 * @return 认证头值长度，失败返回-1
 */
int XHttpProxy_buildBasicAuth(
    char* buffer,
    size_t bufferSize,
    const char* username,
    const char* password
);

/**
 * @brief 构建HTTP Digest认证头
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param method HTTP方法
 * @param uri URI
 * @param username 用户名
 * @param password 密码
 * @param params Digest参数
 * @return 认证头值长度，失败返回-1
 */
int XHttpProxy_buildDigestAuth(
    char* buffer,
    size_t bufferSize,
    const char* method,
    const char* uri,
    const char* username,
    const char* password,
    const XHttpDigestParams* params
);

/**
 * @brief 构建NTLM Type-1消息
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param domain 域名（可为NULL）
 * @param workstation 工作站名（可为NULL）
 * @return 消息长度，失败返回-1
 */
int XHttpProxy_buildNtlmType1(
    uint8_t* buffer,
    size_t bufferSize,
    const char* domain,
    const char* workstation
);

/**
 * @brief 解析NTLM Type-2消息
 * @param buffer Type-2消息
 * @param bufferLen 消息长度
 * @param outContext 输出NTLM上下文
 * @return 成功返回true
 */
bool XHttpProxy_parseNtlmType2(
    const uint8_t* buffer,
    size_t bufferLen,
    XNtlmContext* outContext
);

/**
 * @brief 构建NTLM Type-3消息
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param username 用户名
 * @param password 密码
 * @param context NTLM上下文（包含Type-2数据）
 * @return 消息长度，失败返回-1
 */
int XHttpProxy_buildNtlmType3(
    uint8_t* buffer,
    size_t bufferSize,
    const char* username,
    const char* password,
    const XNtlmContext* context
);

/**
 * @brief 构建NTLM认证头
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param ntlmMessage NTLM消息
 * @param messageLen 消息长度
 * @return 认证头值长度，失败返回-1
 */
int XHttpProxy_buildNtlmAuthHeader(
    char* buffer,
    size_t bufferSize,
    const uint8_t* ntlmMessage,
    size_t messageLen
);

// =============== 系统代理获取API ===============

/**
 * @brief 获取系统代理配置
 * @param query 代理查询条件
 * @param outProxy 输出代理配置
 * @return 成功返回true
 */
bool XNetworkProxy_getSystemProxy(
    const XNetworkProxyQuery* query,
    XNetworkProxy* outProxy
);

/**
 * @brief 检查URL是否在代理例外列表中
 * @param url 要检查的URL
 * @param bypassList 代理例外列表（分号分隔）
 * @return 在例外列表中返回true
 */
bool XNetworkProxy_isBypassed(
    const char* url,
    const char* bypassList
);

#ifdef __cplusplus
}
#endif

#endif // XNETWORKPROXYHANDSHAKE_H