// XNetworkProxy.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// C 语言模拟 Qt6 QNetworkProxy，提供网络代理配置功能。

#ifndef XNETWORKPROXY_H
#define XNETWORKPROXY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
#include "XString.h"
#include "XHostAddress.h"
#include <stdint.h>
#include <stdbool.h>

// =============== 枚举定义 ===============

/**
 * @brief 代理查询类型。
 */
typedef enum XNetworkProxyQuery_QueryType {
    XNetworkProxyQuery_TcpSocket,      ///< TCP 客户端套接字
    XNetworkProxyQuery_UdpSocket,      ///< UDP 套接字
    XNetworkProxyQuery_SctpSocket,     ///< SCTP 套接字
    XNetworkProxyQuery_TcpServer = 100, ///< TCP 服务器
    XNetworkProxyQuery_UrlRequest,     ///< URL 请求
    XNetworkProxyQuery_SctpServer      ///< SCTP 服务器
} XNetworkProxyQuery_QueryType;

/**
 * @brief 代理类型。
 */
typedef enum XNetworkProxy_ProxyType {
    XNetworkProxy_DefaultProxy,       ///< 默认代理（使用应用级设置）
    XNetworkProxy_Socks5Proxy,        ///< SOCKS v5 代理
    XNetworkProxy_NoProxy,            ///< 不使用代理
    XNetworkProxy_HttpProxy,          ///< HTTP 透明代理
    XNetworkProxy_HttpCachingProxy,   ///< HTTP 缓存代理（仅支持 GET/POST）
    XNetworkProxy_FtpCachingProxy     ///< FTP 缓存代理
} XNetworkProxy_ProxyType;

/**
 * @brief 代理能力标志。
 */
typedef enum XNetworkProxy_Capability {
    XNetworkProxy_TunnelingCapability      = 0x0001, ///< 支持隧道（CONNECT）
    XNetworkProxy_ListeningCapability      = 0x0002, ///< 支持监听（BIND）
    XNetworkProxy_UdpTunnelingCapability   = 0x0004, ///< 支持 UDP 隧道
    XNetworkProxy_CachingCapability        = 0x0008, ///< 支持缓存
    XNetworkProxy_HostNameLookupCapability = 0x0010, ///< 支持远程 DNS 解析
    XNetworkProxy_SctpTunnelingCapability  = 0x0020, ///< 支持 SCTP 隧道
    XNetworkProxy_SctpListeningCapability  = 0x0040  ///< 支持 SCTP 监听
} XNetworkProxy_Capability;

typedef int XNetworkProxy_Capabilities; ///< 能力位掩码

// =============== XNetworkProxyQuery 结构体 ===============

/**
 * @brief 代理查询条件，用于查询应使用的代理配置。
 */
typedef struct XNetworkProxyQuery {
    XNetworkProxyQuery_QueryType queryType;  ///< 查询类型
    char* peerHostName;                       ///< 目标主机名
    int peerPort;                             ///< 目标端口
    int localPort;                            ///< 本地端口（用于服务器绑定）
    char* protocolTag;                        ///< 协议标签
    char* url;                                ///< URL（仅 UrlRequest 类型）
} XNetworkProxyQuery;

// =============== XNetworkProxyQuery API ===============

/**
 * @brief 初始化已分配的 XNetworkProxyQuery 结构体。
 * @param query 指向未初始化的 XNetworkProxyQuery 实例（必须非 NULL）
 */
void XNetworkProxyQuery_init(XNetworkProxyQuery* query);

/**
 * @brief 在堆上创建并初始化一个 XNetworkProxyQuery 实例。
 * @return 成功返回指向新分配实例的指针，失败返回 NULL
 */
XNetworkProxyQuery* XNetworkProxyQuery_create(void);

/**
 * @brief 使用 URL 创建代理查询。
 * @param url 请求的 URL
 * @param queryType 查询类型
 * @return 新创建的查询实例
 */
XNetworkProxyQuery* XNetworkProxyQuery_create_withUrl(const char* url, XNetworkProxyQuery_QueryType queryType);

/**
 * @brief 使用主机名和端口创建代理查询。
 * @param hostname 目标主机名
 * @param port 目标端口
 * @param protocolTag 协议标签（可为 NULL）
 * @param queryType 查询类型
 * @return 新创建的查询实例
 */
XNetworkProxyQuery* XNetworkProxyQuery_create_withHostPort(const char* hostname, int port, 
                                                           const char* protocolTag, XNetworkProxyQuery_QueryType queryType);

/**
 * @brief 使用绑定端口创建代理查询（用于服务器）。
 * @param bindPort 本地绑定端口
 * @param protocolTag 协议标签（可为 NULL）
 * @param queryType 查询类型
 * @return 新创建的查询实例
 */
XNetworkProxyQuery* XNetworkProxyQuery_create_withBindPort(uint16_t bindPort, const char* protocolTag,
                                                           XNetworkProxyQuery_QueryType queryType);

/**
 * @brief 释放 XNetworkProxyQuery 结构体内部资源。
 * @param query 查询实例
 */
void XNetworkProxyQuery_deinit(XNetworkProxyQuery* query);

/**
 * @brief 删除并释放 XNetworkProxyQuery 实例。
 * @param query 查询实例
 */
void XNetworkProxyQuery_delete(XNetworkProxyQuery* query);

/**
 * @brief 深拷贝一个 XNetworkProxyQuery 实例。
 * @param other 源查询实例
 * @return 新创建的副本
 */
XNetworkProxyQuery* XNetworkProxyQuery_copy(const XNetworkProxyQuery* other);

/**
 * @brief 获取查询类型。
 * @param query 查询实例
 * @return 查询类型枚举值
 */
XNetworkProxyQuery_QueryType XNetworkProxyQuery_queryType(const XNetworkProxyQuery* query);

/**
 * @brief 设置查询类型。
 * @param query 查询实例
 * @param type 查询类型
 */
void XNetworkProxyQuery_setQueryType(XNetworkProxyQuery* query, XNetworkProxyQuery_QueryType type);

/**
 * @brief 获取目标端口。
 * @param query 查询实例
 * @return 目标端口号，无效时返回 -1
 */
int XNetworkProxyQuery_peerPort(const XNetworkProxyQuery* query);

/**
 * @brief 设置目标端口。
 * @param query 查询实例
 * @param port 端口号
 */
void XNetworkProxyQuery_setPeerPort(XNetworkProxyQuery* query, int port);

/**
 * @brief 获取目标主机名。
 * @param query 查询实例
 * @return 主机名字符串（可能为 NULL）
 */
const char* XNetworkProxyQuery_peerHostName(const XNetworkProxyQuery* query);

/**
 * @brief 设置目标主机名。
 * @param query 查询实例
 * @param hostname 主机名字符串
 */
void XNetworkProxyQuery_setPeerHostName(XNetworkProxyQuery* query, const char* hostname);

/**
 * @brief 获取本地端口。
 * @param query 查询实例
 * @return 本地端口号，无效时返回 -1
 */
int XNetworkProxyQuery_localPort(const XNetworkProxyQuery* query);

/**
 * @brief 设置本地端口。
 * @param query 查询实例
 * @param port 端口号
 */
void XNetworkProxyQuery_setLocalPort(XNetworkProxyQuery* query, int port);

/**
 * @brief 获取协议标签。
 * @param query 查询实例
 * @return 协议标签字符串（可能为 NULL）
 */
const char* XNetworkProxyQuery_protocolTag(const XNetworkProxyQuery* query);

/**
 * @brief 设置协议标签。
 * @param query 查询实例
 * @param tag 协议标签字符串
 */
void XNetworkProxyQuery_setProtocolTag(XNetworkProxyQuery* query, const char* tag);

/**
 * @brief 获取请求 URL。
 * @param query 查询实例
 * @return URL 字符串（可能为 NULL）
 */
const char* XNetworkProxyQuery_url(const XNetworkProxyQuery* query);

/**
 * @brief 设置请求 URL。
 * @param query 查询实例
 * @param url URL 字符串
 */
void XNetworkProxyQuery_setUrl(XNetworkProxyQuery* query, const char* url);

/**
 * @brief 比较两个查询是否相等。
 * @param a 查询实例 A
 * @param b 查询实例 B
 * @return 相等返回 true
 */
bool XNetworkProxyQuery_equal(const XNetworkProxyQuery* a, const XNetworkProxyQuery* b);

// =============== XNetworkProxy 结构体 ===============

/**
 * @brief 网络代理配置，包含代理类型、地址、端口、认证等信息。
 */
typedef struct XNetworkProxy {
    XNetworkProxy_ProxyType type;         ///< 代理类型
    XNetworkProxy_Capabilities capabilities; ///< 能力标志
    char* hostName;                        ///< 代理服务器主机名
    uint16_t port;                         ///< 代理服务器端口
    char* user;                            ///< 认证用户名
    char* password;                        ///< 认证密码
} XNetworkProxy;

// =============== XNetworkProxy API ===============

/**
 * @brief 初始化已分配的 XNetworkProxy 结构体。
 * @param proxy 指向未初始化的 XNetworkProxy 实例（必须非 NULL）
 */
void XNetworkProxy_init(XNetworkProxy* proxy);

/**
 * @brief 在堆上创建并初始化一个 XNetworkProxy 实例。
 * @return 成功返回指向新分配实例的指针，失败返回 NULL
 */
XNetworkProxy* XNetworkProxy_create(void);

/**
 * @brief 使用指定参数创建代理配置。
 * @param type 代理类型
 * @param hostName 代理服务器主机名
 * @param port 代理服务器端口
 * @param user 认证用户名（可为 NULL）
 * @param password 认证密码（可为 NULL）
 * @return 新创建的代理实例
 */
XNetworkProxy* XNetworkProxy_create_withType(XNetworkProxy_ProxyType type, const char* hostName, 
                                              uint16_t port, const char* user, const char* password);

/**
 * @brief 释放 XNetworkProxy 结构体内部资源。
 * @param proxy 代理实例
 */
void XNetworkProxy_deinit(XNetworkProxy* proxy);

/**
 * @brief 删除并释放 XNetworkProxy 实例。
 * @param proxy 代理实例
 */
void XNetworkProxy_delete(XNetworkProxy* proxy);

/**
 * @brief 深拷贝一个 XNetworkProxy 实例。
 * @param other 源代理实例
 * @return 新创建的副本
 */
XNetworkProxy* XNetworkProxy_copy(const XNetworkProxy* other);

/**
 * @brief 获取代理类型。
 * @param proxy 代理实例
 * @return 代理类型枚举值
 */
XNetworkProxy_ProxyType XNetworkProxy_type(const XNetworkProxy* proxy);

/**
 * @brief 设置代理类型。
 * @param proxy 代理实例
 * @param type 代理类型
 * @note 会自动设置该类型对应的能力标志
 */
void XNetworkProxy_setType(XNetworkProxy* proxy, XNetworkProxy_ProxyType type);

/**
 * @brief 获取代理能力标志。
 * @param proxy 代理实例
 * @return 能力位掩码
 */
XNetworkProxy_Capabilities XNetworkProxy_capabilities(const XNetworkProxy* proxy);

/**
 * @brief 设置代理能力标志。
 * @param proxy 代理实例
 * @param capab 能力位掩码
 */
void XNetworkProxy_setCapabilities(XNetworkProxy* proxy, XNetworkProxy_Capabilities capab);

/**
 * @brief 判断是否为缓存代理。
 * @param proxy 代理实例
 * @return 具有缓存能力返回 true
 */
bool XNetworkProxy_isCachingProxy(const XNetworkProxy* proxy);

/**
 * @brief 判断是否为透明代理。
 * @param proxy 代理实例
 * @return 具有隧道能力返回 true
 */
bool XNetworkProxy_isTransparentProxy(const XNetworkProxy* proxy);

/**
 * @brief 获取认证用户名。
 * @param proxy 代理实例
 * @return 用户名字符串（可能为 NULL）
 */
const char* XNetworkProxy_user(const XNetworkProxy* proxy);

/**
 * @brief 设置认证用户名。
 * @param proxy 代理实例
 * @param userName 用户名字符串
 */
void XNetworkProxy_setUser(XNetworkProxy* proxy, const char* userName);

/**
 * @brief 获取认证密码。
 * @param proxy 代理实例
 * @return 密码字符串（可能为 NULL）
 */
const char* XNetworkProxy_password(const XNetworkProxy* proxy);

/**
 * @brief 设置认证密码。
 * @param proxy 代理实例
 * @param password 密码字符串
 */
void XNetworkProxy_setPassword(XNetworkProxy* proxy, const char* password);

/**
 * @brief 获取代理服务器主机名。
 * @param proxy 代理实例
 * @return 主机名字符串（可能为 NULL）
 */
const char* XNetworkProxy_hostName(const XNetworkProxy* proxy);

/**
 * @brief 设置代理服务器主机名。
 * @param proxy 代理实例
 * @param hostName 主机名字符串
 */
void XNetworkProxy_setHostName(XNetworkProxy* proxy, const char* hostName);

/**
 * @brief 获取代理服务器端口。
 * @param proxy 代理实例
 * @return 端口号
 */
uint16_t XNetworkProxy_port(const XNetworkProxy* proxy);

/**
 * @brief 设置代理服务器端口。
 * @param proxy 代理实例
 * @param port 端口号
 */
void XNetworkProxy_setPort(XNetworkProxy* proxy, uint16_t port);

/**
 * @brief 比较两个代理配置是否相等。
 * @param a 代理实例 A
 * @param b 代理实例 B
 * @return 相等返回 true
 */
bool XNetworkProxy_equal(const XNetworkProxy* a, const XNetworkProxy* b);

// =============== 应用级代理设置 ===============

/**
 * @brief 设置应用程序级别的默认代理。
 * @param proxy 代理配置（传 NULL 则清除设置）
 */
void XNetworkProxy_setApplicationProxy(const XNetworkProxy* proxy);

/**
 * @brief 获取应用程序级别的默认代理。
 * @return 指向应用级代理的指针（不应被释放）
 */
XNetworkProxy* XNetworkProxy_applicationProxy(void);

// =============== XNetworkProxyFactory 结构体 ===============

/**
 * @brief 代理工厂基类，用于自定义代理选择逻辑。
 */
typedef struct XNetworkProxyFactory XNetworkProxyFactory;

/**
 * @brief 代理查询回调函数类型。
 * @param factory 代理工厂实例
 * @param query 查询条件
 * @return 返回代理列表（以 NULL 结尾的 XNetworkProxy 数组）
 */
typedef XNetworkProxy* (*XNetworkProxyFactory_QueryFunc)(XNetworkProxyFactory* factory, const XNetworkProxyQuery* query);

struct XNetworkProxyFactory {
    XNetworkProxyFactory_QueryFunc queryProxy; ///< 查询代理回调
};

// =============== XNetworkProxyFactory API ===============

/**
 * @brief 初始化已分配的 XNetworkProxyFactory 结构体。
 * @param factory 指向未初始化的工厂实例
 * @param queryFunc 查询代理的回调函数
 */
void XNetworkProxyFactory_init(XNetworkProxyFactory* factory, XNetworkProxyFactory_QueryFunc queryFunc);

/**
 * @brief 在堆上创建并初始化一个 XNetworkProxyFactory 实例。
 * @param queryFunc 查询代理的回调函数
 * @return 新创建的工厂实例
 */
XNetworkProxyFactory* XNetworkProxyFactory_create(XNetworkProxyFactory_QueryFunc queryFunc);

/**
 * @brief 释放 XNetworkProxyFactory 结构体内部资源。
 * @param factory 工厂实例
 */
void XNetworkProxyFactory_deinit(XNetworkProxyFactory* factory);

/**
 * @brief 删除并释放 XNetworkProxyFactory 实例。
 * @param factory 工厂实例
 */
void XNetworkProxyFactory_delete(XNetworkProxyFactory* factory);

/**
 * @brief 使用工厂查询代理配置。
 * @param factory 工厂实例
 * @param query 查询条件
 * @return 查询到的代理配置（需调用者释放）
 */
XNetworkProxy* XNetworkProxyFactory_queryProxy(XNetworkProxyFactory* factory, const XNetworkProxyQuery* query);

// =============== 静态方法 ===============

/**
 * @brief 检查是否使用系统代理配置。
 * @return 使用系统配置返回 true
 */
bool XNetworkProxyFactory_usesSystemConfiguration(void);

/**
 * @brief 设置是否使用系统代理配置。
 * @param enable true 启用系统代理配置
 */
void XNetworkProxyFactory_setUseSystemConfiguration(bool enable);

/**
 * @brief 设置应用程序级别的代理工厂。
 * @param factory 代理工厂实例（传 NULL 则清除）
 */
void XNetworkProxyFactory_setApplicationProxyFactory(XNetworkProxyFactory* factory);

/**
 * @brief 根据查询条件获取代理配置。
 * @param query 查询条件
 * @return 代理配置（需调用者释放）
 */
XNetworkProxy* XNetworkProxyFactory_proxyForQuery(const XNetworkProxyQuery* query);

/**
 * @brief 获取系统代理配置。
 * @param query 查询条件
 * @return 系统代理配置（需调用者释放）
 */
XNetworkProxy* XNetworkProxyFactory_systemProxyForQuery(const XNetworkProxyQuery* query);

#ifdef __cplusplus
}
#endif

#endif // XNETWORKPROXY_H