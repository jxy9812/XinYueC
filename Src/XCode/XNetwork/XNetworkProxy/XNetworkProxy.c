// XNetworkProxy.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XNetworkProxy.h"
#include "XMemory.h"
#include "XString.h"
#include <stdlib.h>
#include <string.h>

// =============== 全局变量 ===============

static XNetworkProxy g_applicationProxy = {0};
static bool g_applicationProxyInitialized = false;
static XNetworkProxyFactory* g_applicationProxyFactory = NULL;
static bool g_useSystemConfiguration = false;

// =============== XNetworkProxyQuery 实现 ===============

void XNetworkProxyQuery_init(XNetworkProxyQuery* query) {
    if (!query) return;
    memset(query, 0, sizeof(XNetworkProxyQuery));
    query->queryType = XNetworkProxyQuery_TcpSocket;
    query->peerPort = -1;
    query->localPort = -1;
}

XNetworkProxyQuery* XNetworkProxyQuery_create(void) {
    XNetworkProxyQuery* query = XMalloc_System(sizeof(XNetworkProxyQuery));
    if (query) XNetworkProxyQuery_init(query);
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_withUrl(const char* url, XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query && url) {
        XNetworkProxyQuery_setUrl(query, url);
        query->queryType = queryType;
    }
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_withHostPort(const char* hostname, int port, 
                                                           const char* protocolTag, XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        XNetworkProxyQuery_setPeerHostName(query, hostname);
        XNetworkProxyQuery_setPeerPort(query, port);
        XNetworkProxyQuery_setProtocolTag(query, protocolTag);
        query->queryType = queryType;
    }
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_withBindPort(uint16_t bindPort, const char* protocolTag,
                                                           XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        XNetworkProxyQuery_setLocalPort(query, bindPort);
        XNetworkProxyQuery_setProtocolTag(query, protocolTag);
        query->queryType = queryType;
    }
    return query;
}

void XNetworkProxyQuery_deinit(XNetworkProxyQuery* query) {
    if (!query) return;
    if (query->peerHostName) XFree_System(query->peerHostName);
    if (query->protocolTag) XFree_System(query->protocolTag);
    if (query->url) XFree_System(query->url);
    memset(query, 0, sizeof(XNetworkProxyQuery));
}

void XNetworkProxyQuery_delete(XNetworkProxyQuery* query) {
    XNetworkProxyQuery_deinit(query);
    XFree_System(query);
}

XNetworkProxyQuery* XNetworkProxyQuery_copy(const XNetworkProxyQuery* other) {
    if (!other) return NULL;
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        query->queryType = other->queryType;
        XNetworkProxyQuery_setPeerHostName(query, other->peerHostName);
        query->peerPort = other->peerPort;
        query->localPort = other->localPort;
        XNetworkProxyQuery_setProtocolTag(query, other->protocolTag);
        XNetworkProxyQuery_setUrl(query, other->url);
    }
    return query;
}

XNetworkProxyQuery_QueryType XNetworkProxyQuery_queryType(const XNetworkProxyQuery* query) {
    return query ? query->queryType : XNetworkProxyQuery_TcpSocket;
}

void XNetworkProxyQuery_setQueryType(XNetworkProxyQuery* query, XNetworkProxyQuery_QueryType type) {
    if (query) query->queryType = type;
}

int XNetworkProxyQuery_peerPort(const XNetworkProxyQuery* query) {
    return query ? query->peerPort : -1;
}

void XNetworkProxyQuery_setPeerPort(XNetworkProxyQuery* query, int port) {
    if (query) query->peerPort = port;
}

const char* XNetworkProxyQuery_peerHostName(const XNetworkProxyQuery* query) {
    return query ? query->peerHostName : NULL;
}

void XNetworkProxyQuery_setPeerHostName(XNetworkProxyQuery* query, const char* hostname) {
    if (!query) return;
    if (query->peerHostName) XFree_System(query->peerHostName);
    query->peerHostName = hostname ? XStrdup(hostname) : NULL;
}

int XNetworkProxyQuery_localPort(const XNetworkProxyQuery* query) {
    return query ? query->localPort : -1;
}

void XNetworkProxyQuery_setLocalPort(XNetworkProxyQuery* query, int port) {
    if (query) query->localPort = port;
}

const char* XNetworkProxyQuery_protocolTag(const XNetworkProxyQuery* query) {
    return query ? query->protocolTag : NULL;
}

void XNetworkProxyQuery_setProtocolTag(XNetworkProxyQuery* query, const char* tag) {
    if (!query) return;
    if (query->protocolTag) XFree_System(query->protocolTag);
    query->protocolTag = tag ? XStrdup(tag) : NULL;
}

const char* XNetworkProxyQuery_url(const XNetworkProxyQuery* query) {
    return query ? query->url : NULL;
}

void XNetworkProxyQuery_setUrl(XNetworkProxyQuery* query, const char* url) {
    if (!query) return;
    if (query->url) XFree_System(query->url);
    query->url = url ? XStrdup(url) : NULL;
}

bool XNetworkProxyQuery_equal(const XNetworkProxyQuery* a, const XNetworkProxyQuery* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->queryType != b->queryType) return false;
    if (a->peerPort != b->peerPort) return false;
    if (a->localPort != b->localPort) return false;
    
    if ((a->peerHostName == NULL) != (b->peerHostName == NULL)) return false;
    if (a->peerHostName && b->peerHostName && strcmp(a->peerHostName, b->peerHostName) != 0) return false;
    
    if ((a->protocolTag == NULL) != (b->protocolTag == NULL)) return false;
    if (a->protocolTag && b->protocolTag && strcmp(a->protocolTag, b->protocolTag) != 0) return false;
    
    if ((a->url == NULL) != (b->url == NULL)) return false;
    if (a->url && b->url && strcmp(a->url, b->url) != 0) return false;
    
    return true;
}

// =============== XNetworkProxy 实现 ===============

void XNetworkProxy_init(XNetworkProxy* proxy) {
    if (!proxy) return;
    memset(proxy, 0, sizeof(XNetworkProxy));
    proxy->type = XNetworkProxy_DefaultProxy;
    proxy->capabilities = XNetworkProxy_TunnelingCapability | XNetworkProxy_HostNameLookupCapability;
}

XNetworkProxy* XNetworkProxy_create(void) {
    XNetworkProxy* proxy = XMalloc_System(sizeof(XNetworkProxy));
    if (proxy) XNetworkProxy_init(proxy);
    return proxy;
}

XNetworkProxy* XNetworkProxy_create_withType(XNetworkProxy_ProxyType type, const char* hostName, 
                                              uint16_t port, const char* user, const char* password) {
    XNetworkProxy* proxy = XNetworkProxy_create();
    if (proxy) {
        XNetworkProxy_setType(proxy, type);
        XNetworkProxy_setHostName(proxy, hostName);
        XNetworkProxy_setPort(proxy, port);
        XNetworkProxy_setUser(proxy, user);
        XNetworkProxy_setPassword(proxy, password);
    }
    return proxy;
}

void XNetworkProxy_deinit(XNetworkProxy* proxy) {
    if (!proxy) return;
    if (proxy->hostName) XFree_System(proxy->hostName);
    if (proxy->user) XFree_System(proxy->user);
    if (proxy->password) XFree_System(proxy->password);
    memset(proxy, 0, sizeof(XNetworkProxy));
}

void XNetworkProxy_delete(XNetworkProxy* proxy) {
    XNetworkProxy_deinit(proxy);
    XFree_System(proxy);
}

XNetworkProxy* XNetworkProxy_copy(const XNetworkProxy* other) {
    if (!other) return NULL;
    return XNetworkProxy_create_withType(other->type, other->hostName, other->port, other->user, other->password);
}

XNetworkProxy_ProxyType XNetworkProxy_type(const XNetworkProxy* proxy) {
    return proxy ? proxy->type : XNetworkProxy_DefaultProxy;
}

void XNetworkProxy_setType(XNetworkProxy* proxy, XNetworkProxy_ProxyType type) {
    if (!proxy) return;
    proxy->type = type;

    // 根据代理类型设置默认能力
    switch (type) 
    {
        case XNetworkProxy_NoProxy:
            proxy->capabilities = 0;
            break;
        case XNetworkProxy_Socks5Proxy:
            proxy->capabilities = XNetworkProxy_TunnelingCapability | 
                                  XNetworkProxy_ListeningCapability |
                                  XNetworkProxy_UdpTunnelingCapability |
                                  XNetworkProxy_HostNameLookupCapability;
            break;
        case XNetworkProxy_HttpProxy:
            proxy->capabilities = XNetworkProxy_TunnelingCapability | 
                                  XNetworkProxy_CachingCapability |
                                  XNetworkProxy_HostNameLookupCapability;
            break;
        case XNetworkProxy_HttpCachingProxy:
            proxy->capabilities = XNetworkProxy_CachingCapability;
            break;
        case XNetworkProxy_FtpCachingProxy:
            proxy->capabilities = XNetworkProxy_CachingCapability;
            break;
        default:
                    break;
    }
}
XNetworkProxy_Capabilities XNetworkProxy_capabilities(const XNetworkProxy* proxy) {
    return proxy ? proxy->capabilities : 0;
}

void XNetworkProxy_setCapabilities(XNetworkProxy* proxy, XNetworkProxy_Capabilities capab) {
    if (proxy) proxy->capabilities = capab;
}

bool XNetworkProxy_isCachingProxy(const XNetworkProxy* proxy) {
    return proxy && (proxy->capabilities & XNetworkProxy_CachingCapability);
}

bool XNetworkProxy_isTransparentProxy(const XNetworkProxy* proxy) {
    return proxy && (proxy->capabilities & XNetworkProxy_TunnelingCapability);
}

const char* XNetworkProxy_user(const XNetworkProxy* proxy) {
    return proxy ? proxy->user : NULL;
}

void XNetworkProxy_setUser(XNetworkProxy* proxy, const char* userName) {
    if (!proxy) return;
    if (proxy->user) XFree_System(proxy->user);
    proxy->user = userName ? XStrdup(userName) : NULL;
}

const char* XNetworkProxy_password(const XNetworkProxy* proxy) {
    return proxy ? proxy->password : NULL;
}

void XNetworkProxy_setPassword(XNetworkProxy* proxy, const char* password) {
    if (!proxy) return;
    if (proxy->password) XFree_System(proxy->password);
    proxy->password = password ? XStrdup(password) : NULL;
}

const char* XNetworkProxy_hostName(const XNetworkProxy* proxy) {
    return proxy ? proxy->hostName : NULL;
}

void XNetworkProxy_setHostName(XNetworkProxy* proxy, const char* hostName) {
    if (!proxy) return;
    if (proxy->hostName) XFree_System(proxy->hostName);
    proxy->hostName = hostName ? XStrdup(hostName) : NULL;
}

uint16_t XNetworkProxy_port(const XNetworkProxy* proxy) {
    return proxy ? proxy->port : 0;
}

void XNetworkProxy_setPort(XNetworkProxy* proxy, uint16_t port) {
    if (proxy) proxy->port = port;
}

bool XNetworkProxy_equal(const XNetworkProxy* a, const XNetworkProxy* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    if (a->capabilities != b->capabilities) return false;
    if (a->port != b->port) return false;

    if ((a->hostName == NULL) != (b->hostName == NULL)) return false;
    if (a->hostName && b->hostName && strcmp(a->hostName, b->hostName) != 0) return false;

    if ((a->user == NULL) != (b->user == NULL)) return false;
    if (a->user && b->user && strcmp(a->user, b->user) != 0) return false;

    if ((a->password == NULL) != (b->password == NULL)) return false;
    if (a->password && b->password && strcmp(a->password, b->password) != 0) return false;

    return true;
}

// =============== 应用级代理设置 ===============

void XNetworkProxy_setApplicationProxy(const XNetworkProxy* proxy) {
    if (!g_applicationProxyInitialized) {
        XNetworkProxy_init(&g_applicationProxy);
        g_applicationProxyInitialized = true;
    }

    if (proxy) {
        g_applicationProxy.type = proxy->type;
        g_applicationProxy.capabilities = proxy->capabilities;
        g_applicationProxy.port = proxy->port;

        if (g_applicationProxy.hostName) XFree_System(g_applicationProxy.hostName);
        g_applicationProxy.hostName = proxy->hostName ? XStrdup(proxy->hostName) : NULL;

        if (g_applicationProxy.user) XFree_System(g_applicationProxy.user);
        g_applicationProxy.user = proxy->user ? XStrdup(proxy->user) : NULL;

        if (g_applicationProxy.password) XFree_System(g_applicationProxy.password);
        g_applicationProxy.password = proxy->password ? XStrdup(proxy->password) : NULL;
    } else {
        XNetworkProxy_deinit(&g_applicationProxy);
        XNetworkProxy_init(&g_applicationProxy);
    }
}

XNetworkProxy* XNetworkProxy_applicationProxy(void) {
    if (!g_applicationProxyInitialized) {
        XNetworkProxy_init(&g_applicationProxy);
        g_applicationProxyInitialized = true;
    }
    return &g_applicationProxy;
}

// =============== XNetworkProxyFactory 实现 ===============

void XNetworkProxyFactory_init(XNetworkProxyFactory* factory, XNetworkProxyFactory_QueryFunc queryFunc) {
    if (!factory) return;
    factory->queryProxy = queryFunc;
}

XNetworkProxyFactory* XNetworkProxyFactory_create(XNetworkProxyFactory_QueryFunc queryFunc) {
    XNetworkProxyFactory* factory = XMalloc_System(sizeof(XNetworkProxyFactory));
    if (factory) XNetworkProxyFactory_init(factory, queryFunc);
    return factory;
}

void XNetworkProxyFactory_deinit(XNetworkProxyFactory* factory) {
    if (!factory) return;
    factory->queryProxy = NULL;
}

void XNetworkProxyFactory_delete(XNetworkProxyFactory* factory) {
    XNetworkProxyFactory_deinit(factory);
    XFree_System(factory);
}

XNetworkProxy* XNetworkProxyFactory_queryProxy(XNetworkProxyFactory* factory, const XNetworkProxyQuery* query) {
    if (!factory || !factory->queryProxy) return NULL;
    return factory->queryProxy(factory, query);
}

bool XNetworkProxyFactory_usesSystemConfiguration(void) {
    return g_useSystemConfiguration;
}

void XNetworkProxyFactory_setUseSystemConfiguration(bool enable) {
    g_useSystemConfiguration = enable;
}

void XNetworkProxyFactory_setApplicationProxyFactory(XNetworkProxyFactory* factory) {
    g_applicationProxyFactory = factory;
}

XNetworkProxy* XNetworkProxyFactory_proxyForQuery(const XNetworkProxyQuery* query) {
    // 如果设置了自定义工厂，使用它
    if (g_applicationProxyFactory && g_applicationProxyFactory->queryProxy) {
        return g_applicationProxyFactory->queryProxy(g_applicationProxyFactory, query);
    }

    // 如果使用系统配置，返回系统代理
    if (g_useSystemConfiguration) {
        return XNetworkProxyFactory_systemProxyForQuery(query);
    }

    // 默认返回应用级代理
    return XNetworkProxy_copy(XNetworkProxy_applicationProxy());
}

XNetworkProxy* XNetworkProxyFactory_systemProxyForQuery(const XNetworkProxyQuery* query) {
    // 简化实现：返回 NoProxy
    // 实际实现需要调用系统 API 获取代理设置
    (void)query;
    return XNetworkProxy_create_withType(XNetworkProxy_NoProxy, NULL, 0, NULL, NULL);
}