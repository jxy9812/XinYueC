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

// =============== XNetworkProxyQuery 虚函数实现 ===============

static void VXNetworkProxyQuery_deinit(XNetworkProxyQuery* query) {
    if (!query) return;
    if (query->peerHostName) XString_delete_base(query->peerHostName);
    if (query->protocolTag) XString_delete_base(query->protocolTag);
    if (query->url) XString_delete_base(query->url);
}

static void VXNetworkProxyQuery_copy(XNetworkProxyQuery* dest, const XNetworkProxyQuery* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxyQuery);
    dest->queryType = src->queryType;
    dest->peerPort = src->peerPort;
    dest->localPort = src->localPort;
    if (src->peerHostName) {
        dest->peerHostName = XString_create_copy(src->peerHostName);
    }
    if (src->protocolTag) {
        dest->protocolTag = XString_create_copy(src->protocolTag);
    }
    if (src->url) {
        dest->url = XString_create_copy(src->url);
    }
}

static void VXNetworkProxyQuery_move(XNetworkProxyQuery* dest, XNetworkProxyQuery* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxyQuery);
    dest->queryType = src->queryType;
    dest->peerPort = src->peerPort;
    dest->localPort = src->localPort;
    dest->peerHostName = src->peerHostName;
    dest->protocolTag = src->protocolTag;
    dest->url = src->url;
    src->peerHostName = NULL;
    src->protocolTag = NULL;
    src->url = NULL;
}

XVtable* XNetworkProxyQuery_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetworkProxyQuery))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承自 XClass
    XVTABLE_INHERIT_XCLASS(XClass);
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkProxyQuery_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkProxyQuery_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkProxyQuery_move);
#if SHOWCONTAINERSIZE
    printf("XNetworkProxyQuery vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== XNetworkProxyQuery 实现 ===============

void XNetworkProxyQuery_init(XNetworkProxyQuery* query) {
    if (!query) return;
    memset(((XClass*)query) + 1, 0, sizeof(XNetworkProxyQuery) - sizeof(XClass));
    XClass_init(&query->base);
    XClassGetVtable(query) = XNetworkProxyQuery_class_init();
    query->queryType = XNetworkProxyQuery_TcpSocket;
    query->peerPort = -1;
    query->localPort = -1;
}

XNetworkProxyQuery* XNetworkProxyQuery_create(void) {
    XNetworkProxyQuery* query = XNew(XNetworkProxyQuery);
    XNetworkProxyQuery_init(query);
    Set_Class_MemoryFree(query, XFree_System);
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_2(const XString* url, XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query && url) {
        XNetworkProxyQuery_setUrl(query, url);
        query->queryType = queryType;
    }
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_3(const XString* hostname, int port, 
                                                           const XString* protocolTag, XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        XNetworkProxyQuery_setPeerHostName(query, hostname);
        XNetworkProxyQuery_setPeerPort(query, port);
        XNetworkProxyQuery_setProtocolTag(query, protocolTag);
        query->queryType = queryType;
    }
    return query;
}

XNetworkProxyQuery* XNetworkProxyQuery_create_4(uint16_t bindPort, const XString* protocolTag,
                                                           XNetworkProxyQuery_QueryType queryType) {
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        XNetworkProxyQuery_setLocalPort(query, bindPort);
        XNetworkProxyQuery_setProtocolTag(query, protocolTag);
        query->queryType = queryType;
    }
    return query;
}

// deinit/delete/copy 通过宏 XNetworkProxyQuery_deinit_base 等调用基类函数

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

const XString* XNetworkProxyQuery_peerHostName_const(const XNetworkProxyQuery* query) {
    return query ? query->peerHostName : NULL;
}
XString* XNetworkProxyQuery_peerHostName(const XNetworkProxyQuery* query)
{
    return query ? XString_create_copy(query->peerHostName) : NULL;
}
void XNetworkProxyQuery_setPeerHostName(XNetworkProxyQuery* query, const XString* hostname) {
    if (!query) return;
    if (query->peerHostName) XString_delete_base(query->peerHostName);
    query->peerHostName = hostname ? XString_create_copy(hostname) : NULL;
}

int XNetworkProxyQuery_localPort(const XNetworkProxyQuery* query) {
    return query ? query->localPort : -1;
}

void XNetworkProxyQuery_setLocalPort(XNetworkProxyQuery* query, int port) {
    if (query) query->localPort = port;
}

const XString* XNetworkProxyQuery_protocolTag_const(const XNetworkProxyQuery* query) {
    return query ? query->protocolTag : NULL;
}

XString* XNetworkProxyQuery_protocolTag(const XNetworkProxyQuery* query)
{
    return query ? XString_create_copy(query->protocolTag) : NULL;
}

void XNetworkProxyQuery_setProtocolTag(XNetworkProxyQuery* query, const XString* tag) {
    if (!query) return;
    if (query->protocolTag) XString_delete_base(query->protocolTag);
    query->protocolTag = tag ? XString_create_copy(tag) : NULL;
}

const XString* XNetworkProxyQuery_url_const(const XNetworkProxyQuery* query) {
    return query ? query->url : NULL;
}

XString* XNetworkProxyQuery_url(const XNetworkProxyQuery* query)
{
    return query ? XString_create_copy(query->url) : NULL;
}

void XNetworkProxyQuery_setUrl(XNetworkProxyQuery* query, const XString* url) {
    if (!query) return;
    if (query->url) XString_delete_base(query->url);
    query->url = url ? XString_create_copy(url) : NULL;
}

bool XNetworkProxyQuery_equal(const XNetworkProxyQuery* a, const XNetworkProxyQuery* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->queryType != b->queryType) return false;
    if (a->peerPort != b->peerPort) return false;
    if (a->localPort != b->localPort) return false;
    
    if ((a->peerHostName == NULL) != (b->peerHostName == NULL)) return false;
    if (a->peerHostName && b->peerHostName && XString_compare(a->peerHostName, b->peerHostName) != 0) return false;
    
    if ((a->protocolTag == NULL) != (b->protocolTag == NULL)) return false;
    if (a->protocolTag && b->protocolTag && XString_compare(a->protocolTag, b->protocolTag) != 0) return false;
    
    if ((a->url == NULL) != (b->url == NULL)) return false;
    if (a->url && b->url && XString_compare(a->url, b->url) != 0) return false;
    
    return true;
}

// =============== XNetworkProxy 虚函数实现 ===============

static void VXNetworkProxy_deinit_base(XNetworkProxy* proxy) {
    if (!proxy) return;
    if (proxy->hostName) XString_delete_base(proxy->hostName);
    if (proxy->user) XString_delete_base(proxy->user);
    if (proxy->password) XString_delete_base(proxy->password);
}

static void VXNetworkProxy_copy(XNetworkProxy* dest, const XNetworkProxy* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxy);
    dest->type = src->type;
    dest->capabilities = src->capabilities;
    dest->port = src->port;
    if (src->hostName) dest->hostName = XString_create_copy(src->hostName);
    if (src->user) dest->user = XString_create_copy(src->user);
    if (src->password) dest->password = XString_create_copy(src->password);
}

static void VXNetworkProxy_move(XNetworkProxy* dest, XNetworkProxy* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxy);
    dest->type = src->type;
    dest->capabilities = src->capabilities;
    dest->port = src->port;
    dest->hostName = src->hostName;
    dest->user = src->user;
    dest->password = src->password;
    src->hostName = NULL;
    src->user = NULL;
    src->password = NULL;
}

XVtable* XNetworkProxy_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetworkProxy))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承自 XClass
    XVTABLE_INHERIT_XCLASS(XClass);
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkProxy_deinit_base);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkProxy_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkProxy_move);
#if SHOWCONTAINERSIZE
    printf("XNetworkProxy vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== XNetworkProxy 实现 ===============

void XNetworkProxy_init(XNetworkProxy* proxy) {
    if (!proxy) return;
    memset(((XClass*)proxy) + 1, 0, sizeof(XNetworkProxy) - sizeof(XClass));
    XClass_init(&proxy->base);
    XClassGetVtable(proxy) = XNetworkProxy_class_init();
    proxy->type = XNetworkProxy_DefaultProxy;
    proxy->capabilities = XNetworkProxy_TunnelingCapability | XNetworkProxy_HostNameLookupCapability;
}

XNetworkProxy* XNetworkProxy_create(void) {
    XNetworkProxy* proxy = XNew(XNetworkProxy);
    XNetworkProxy_init(proxy);
    Set_Class_MemoryFree(proxy, XFree_System);
    return proxy;
}

XNetworkProxy* XNetworkProxy_create_2(XNetworkProxy_ProxyType type, const XString* hostName, 
                                              uint16_t port, const XString* user, const XString* password) {
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

// deinit/delete/copy/move 通过宏 XNetworkProxy_deinit_base_base 等调用基类函数

XNetworkProxy* XNetworkProxy_copy(const XNetworkProxy* other) {
    if (!other) return NULL;
    XNetworkProxy* proxy = XNetworkProxy_create();
    if (proxy) {
        proxy->type = other->type;
        proxy->capabilities = other->capabilities;
        proxy->port = other->port;
        if (other->hostName) proxy->hostName = XString_create_copy(other->hostName);
        if (other->user) proxy->user = XString_create_copy(other->user);
        if (other->password) proxy->password = XString_create_copy(other->password);
    }
    return proxy;
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

const XString* XNetworkProxy_user_const(const XNetworkProxy* proxy) {
    return proxy ? proxy->user : NULL;
}

XString* XNetworkProxy_user(const XNetworkProxy* proxy)
{
    return proxy ? XString_create_copy(proxy->user) : NULL;
}

void XNetworkProxy_setUser(XNetworkProxy* proxy, const XString* userName) {
    if (!proxy) return;
    if (proxy->user) XString_delete_base(proxy->user);
    proxy->user = userName ? XString_create_copy(userName) : NULL;
}

const XString* XNetworkProxy_password_const(const XNetworkProxy* proxy) {
    return proxy ? proxy->password : NULL;
}

XString* XNetworkProxy_password(const XNetworkProxy* proxy)
{
    return proxy ? XString_create_copy(proxy->password) : NULL;
}

void XNetworkProxy_setPassword(XNetworkProxy* proxy, const XString* password) {
    if (!proxy) return;
    if (proxy->password) XString_delete_base(proxy->password);
    proxy->password = password ? XString_create_copy(password) : NULL;
}

const XString* XNetworkProxy_hostName_const(const XNetworkProxy* proxy) {
    return proxy ? proxy->hostName : NULL;
}

XString* XNetworkProxy_hostName(const XNetworkProxy* proxy)
{
    return proxy ? XString_create_copy(proxy->hostName) : NULL;
}

void XNetworkProxy_setHostName(XNetworkProxy* proxy, const XString* hostName) {
    if (!proxy) return;
    if (proxy->hostName) XString_delete_base(proxy->hostName);
    proxy->hostName = hostName ? XString_create_copy(hostName) : NULL;
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
    if (a->hostName && b->hostName && XString_compare(a->hostName, b->hostName) != 0) return false;

    if ((a->user == NULL) != (b->user == NULL)) return false;
    if (a->user && b->user && XString_compare(a->user, b->user) != 0) return false;

    if ((a->password == NULL) != (b->password == NULL)) return false;
    if (a->password && b->password && XString_compare(a->password, b->password) != 0) return false;

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

        if (g_applicationProxy.hostName) XString_delete_base(g_applicationProxy.hostName);
        g_applicationProxy.hostName = proxy->hostName ? XString_create_copy(proxy->hostName) : NULL;

        if (g_applicationProxy.user) XString_delete_base(g_applicationProxy.user);
        g_applicationProxy.user = proxy->user ? XString_create_copy(proxy->user) : NULL;

        if (g_applicationProxy.password) XString_delete_base(g_applicationProxy.password);
        g_applicationProxy.password = proxy->password ? XString_create_copy(proxy->password) : NULL;
    } else {
        XNetworkProxy_deinit_base(&g_applicationProxy);
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

// =============== XNetworkProxyFactory 虚函数实现 ===============

static void VXNetworkProxyFactory_deinit(XNetworkProxyFactory* factory) {
    if (!factory) return;
    factory->queryProxy = NULL;
}

static void VXNetworkProxyFactory_copy(XNetworkProxyFactory* dest, const XNetworkProxyFactory* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxyFactory);
    dest->queryProxy = src->queryProxy;
}

static void VXNetworkProxyFactory_move(XNetworkProxyFactory* dest, XNetworkProxyFactory* src) {
    if (!dest || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(dest, XNetworkProxyFactory);
    dest->queryProxy = src->queryProxy;
    src->queryProxy = NULL;
}

XVtable* XNetworkProxyFactory_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetworkProxyFactory))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承自 XClass
    XVTABLE_INHERIT_XCLASS(XClass);
    // 重载虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkProxyFactory_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkProxyFactory_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkProxyFactory_move);
#if SHOWCONTAINERSIZE
    printf("XNetworkProxyFactory vtable size: %d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== XNetworkProxyFactory 实现 ===============

void XNetworkProxyFactory_init(XNetworkProxyFactory* factory, XNetworkProxyFactory_QueryFunc queryFunc) {
    if (!factory) return;
    memset(((XClass*)factory) + 1, 0, sizeof(XNetworkProxyFactory) - sizeof(XClass));
    XClass_init(&factory->base);
    XClassGetVtable(factory) = XNetworkProxyFactory_class_init();
    factory->queryProxy = queryFunc;
}

XNetworkProxyFactory* XNetworkProxyFactory_create(XNetworkProxyFactory_QueryFunc queryFunc) {
    XNetworkProxyFactory* factory = XNew(XNetworkProxyFactory);
    XNetworkProxyFactory_init(factory, queryFunc);
    Set_Class_MemoryFree(factory, XFree_System);
    return factory;
}

// deinit/delete 通过宏 XNetworkProxyFactory_deinit_base 等调用基类函数

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
    XNetworkProxy* proxy = XNetworkProxy_create();
    if (proxy) {
        XNetworkProxy_setType(proxy, XNetworkProxy_NoProxy);
    }
    return proxy;
}