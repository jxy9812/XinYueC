#include "XNetworkTest.h"
#include "XNetworkProxy.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XString.h"
#include "XHostAddress.h"
#include <stdio.h>

// ==================== XNetworkProxyQuery 测试 ====================

static void testProxyQuery_basic(void)
{
    XPrintf("\n========== XNetworkProxyQuery 基础测试 ==========\n");
    
    // 测试默认创建
    XNetworkProxyQuery* query = XNetworkProxyQuery_create();
    if (query) {
        XPrintf("创建成功\n");
        XPrintf("  查询类型: %d\n", XNetworkProxyQuery_queryType(query));
        XPrintf("  目标端口: %d\n", XNetworkProxyQuery_peerPort(query));
        XPrintf("  本地端口: %d\n", XNetworkProxyQuery_localPort(query));
        XNetworkProxyQuery_delete_base(query);
    }
    
    // 测试使用 URL 创建
    XString* url = XString_create_utf8("http://www.example.com:8080/path");
    query = XNetworkProxyQuery_create_2(url, XNetworkProxyQuery_UrlRequest);
    if (query) {
        XPrintf("\n使用 URL 创建:\n");
        XPrintf("  查询类型: %d\n", XNetworkProxyQuery_queryType(query));
        const XString* queryUrl = XNetworkProxyQuery_url_const(query);
        if (queryUrl) {
            XPrintf("  URL: ");
            XPrintf_2(queryUrl);
            XPrintf("\n");
        }
        XNetworkProxyQuery_delete_base(query);
    }
    XString_delete_base(url);
    
    // 测试使用主机名和端口创建
    XString* hostname = XString_create_utf8("www.google.com");
    XString* protocolTag = XString_create_utf8("http");
    query = XNetworkProxyQuery_create_3(hostname, 443, protocolTag, XNetworkProxyQuery_TcpSocket);
    if (query) {
        XPrintf("\n使用主机名和端口创建:\n");
        XPrintf("  查询类型: %d (TcpSocket)\n", XNetworkProxyQuery_queryType(query));
        XPrintf("  目标端口: %d\n", XNetworkProxyQuery_peerPort(query));
        const XString* host = XNetworkProxyQuery_peerHostName_const(query);
        if (host) {
            XPrintf("  主机名: ");
            XPrintf_2(host);
            XPrintf("\n");
        }
        const XString* tag = XNetworkProxyQuery_protocolTag_const(query);
        if (tag) {
            XPrintf("  协议标签: ");
            XPrintf_2(tag);
            XPrintf("\n");
        }
        XNetworkProxyQuery_delete_base(query);
    }
    XString_delete_base(hostname);
    XString_delete_base(protocolTag);
    
    // 测试服务器绑定创建
    query = XNetworkProxyQuery_create_4(8080, NULL, XNetworkProxyQuery_TcpServer);
    if (query) {
        XPrintf("\n服务器绑定创建:\n");
        XPrintf("  查询类型: %d (TcpServer)\n", XNetworkProxyQuery_queryType(query));
        XPrintf("  本地端口: %d\n", XNetworkProxyQuery_localPort(query));
        XNetworkProxyQuery_delete_base(query);
    }
}

static void testProxyQuery_setters(void)
{
    XPrintf("\n========== XNetworkProxyQuery Setter 测试 ==========\n");
    
    XNetworkProxyQuery query;
    XNetworkProxyQuery_init(&query);
    
    // 设置查询类型
    XNetworkProxyQuery_setQueryType(&query, XNetworkProxyQuery_UdpSocket);
    XPrintf("设置查询类型为 UdpSocket: %d\n", XNetworkProxyQuery_queryType(&query));
    
    // 设置目标端口
    XNetworkProxyQuery_setPeerPort(&query, 8080);
    XPrintf("设置目标端口: %d\n", XNetworkProxyQuery_peerPort(&query));
    
    // 设置本地端口
    XNetworkProxyQuery_setLocalPort(&query, 12345);
    XPrintf("设置本地端口: %d\n", XNetworkProxyQuery_localPort(&query));
    
    // 设置主机名
    XString* hostname = XString_create_utf8("test.example.com");
    XNetworkProxyQuery_setPeerHostName(&query, hostname);
    const XString* host = XNetworkProxyQuery_peerHostName_const(&query);
    if (host) {
        XPrintf("设置主机名: ");
        XPrintf_2(host);
        XPrintf("\n");
    }
    XString_delete_base(hostname);
    
    // 设置协议标签
    XString* tag = XString_create_utf8("https");
    XNetworkProxyQuery_setProtocolTag(&query, tag);
    const XString* protocolTag = XNetworkProxyQuery_protocolTag_const(&query);
    if (protocolTag) {
        XPrintf("设置协议标签: ");
        XPrintf_2(protocolTag);
        XPrintf("\n");
    }
    XString_delete_base(tag);
    
    // 设置 URL
    XString* url = XString_create_utf8("https://secure.example.com/api");
    XNetworkProxyQuery_setUrl(&query, url);
    const XString* queryUrl = XNetworkProxyQuery_url_const(&query);
    if (queryUrl) {
        XPrintf("设置 URL: ");
        XPrintf_2(queryUrl);
        XPrintf("\n");
    }
    XString_delete_base(url);
    
    XNetworkProxyQuery_deinit_base(&query);
}

static void testProxyQuery_copy(void)
{
    XPrintf("\n========== XNetworkProxyQuery 拷贝测试 ==========\n");
    
    XString* hostname = XString_create_utf8("www.test.com");
    XNetworkProxyQuery* original = XNetworkProxyQuery_create_3(hostname, 443, NULL, XNetworkProxyQuery_TcpSocket);
    XString_delete_base(hostname);
    
    if (original) {
        // 栈上拷贝
        XNetworkProxyQuery copied;
        XNetworkProxyQuery_init(&copied);
        XNetworkProxyQuery_copy_base(&copied, original);
        
        XPrintf("原始查询端口: %d\n", XNetworkProxyQuery_peerPort(original));
        XPrintf("拷贝查询端口: %d\n", XNetworkProxyQuery_peerPort(&copied));
        
        // 比较相等性
        if (XNetworkProxyQuery_equal(original, &copied)) {
            XPrintf("拷贝测试: PASSED\n");
        } else {
            XPrintf("拷贝测试: FAILED\n");
        }
        
        XNetworkProxyQuery_deinit_base(&copied);
        XNetworkProxyQuery_delete_base(original);
    }
}

// ==================== XNetworkProxy 测试 ====================

static void testProxy_basic(void)
{
    XPrintf("\n========== XNetworkProxy 基础测试 ==========\n");
    
    // 测试默认创建
    XNetworkProxy* proxy = XNetworkProxy_create();
    if (proxy) {
        XPrintf("创建成功\n");
        XPrintf("  代理类型: %d\n", XNetworkProxy_type(proxy));
        XPrintf("  端口: %d\n", XNetworkProxy_port(proxy));
        XPrintf("  是否缓存代理: %s\n", XNetworkProxy_isCachingProxy(proxy) ? "是" : "否");
        XPrintf("  是否透明代理: %s\n", XNetworkProxy_isTransparentProxy(proxy) ? "是" : "否");
        XNetworkProxy_delete_base(proxy);
    }
    
    // 测试带参数创建
    XString* host = XString_create_utf8("proxy.example.com");
    XString* user = XString_create_utf8("testuser");
    XString* pass = XString_create_utf8("testpass");
    
    proxy = XNetworkProxy_create_2(XNetworkProxy_Socks5Proxy, host, 1080, user, pass);
    if (proxy) {
        XPrintf("\n带参数创建:\n");
        XPrintf("  代理类型: %d (Socks5Proxy)\n", XNetworkProxy_type(proxy));
        XPrintf("  端口: %d\n", XNetworkProxy_port(proxy));
        
        const XString* proxyHost = XNetworkProxy_hostName_const(proxy);
        if (proxyHost) {
            XPrintf("  主机名: ");
            XPrintf_2(proxyHost);
            XPrintf("\n");
        }
        
        const XString* proxyUser = XNetworkProxy_user_const(proxy);
        if (proxyUser) {
            XPrintf("  用户名: ");
            XPrintf_2(proxyUser);
            XPrintf("\n");
        }
        
        const XString* proxyPass = XNetworkProxy_password_const(proxy);
        if (proxyPass) {
            XPrintf("  密码: ");
            XPrintf_2(proxyPass);
            XPrintf("\n");
        }
        
        XPrintf("  是否缓存代理: %s\n", XNetworkProxy_isCachingProxy(proxy) ? "是" : "否");
        XPrintf("  是否透明代理: %s\n", XNetworkProxy_isTransparentProxy(proxy) ? "是" : "否");
        
        XNetworkProxy_delete_base(proxy);
    }
    
    XString_delete_base(host);
    XString_delete_base(user);
    XString_delete_base(pass);
}

static void testProxy_types(void)
{
    XPrintf("\n========== XNetworkProxy 类型测试 ==========\n");
    
    XNetworkProxy proxy;
    XNetworkProxy_init(&proxy);
    
    // 测试各种代理类型
    XNetworkProxy_ProxyType types[] = {
        XNetworkProxy_DefaultProxy,
        XNetworkProxy_Socks5Proxy,
        XNetworkProxy_NoProxy,
        XNetworkProxy_HttpProxy,
        XNetworkProxy_HttpCachingProxy,
        XNetworkProxy_FtpCachingProxy
    };
    
    const char* typeNames[] = {
        "DefaultProxy",
        "Socks5Proxy",
        "NoProxy",
        "HttpProxy",
        "HttpCachingProxy",
        "FtpCachingProxy"
    };
    
    for (int i = 0; i < 6; i++) {
        XNetworkProxy_setType(&proxy, types[i]);
        XPrintf("%s:\n", typeNames[i]);
        XPrintf("  类型值: %d\n", XNetworkProxy_type(&proxy));
        XPrintf("  能力: 0x%04x\n", XNetworkProxy_capabilities(&proxy));
        XPrintf("  缓存代理: %s\n", XNetworkProxy_isCachingProxy(&proxy) ? "是" : "否");
        XPrintf("  透明代理: %s\n", XNetworkProxy_isTransparentProxy(&proxy) ? "是" : "否");
    }
    
    XNetworkProxy_deinit_base(&proxy);
}

static void testProxy_capabilities(void)
{
    XPrintf("\n========== XNetworkProxy 能力测试 ==========\n");
    
    XNetworkProxy proxy;
    XNetworkProxy_init(&proxy);
    
    // 设置能力
    XNetworkProxy_Capabilities caps = XNetworkProxy_TunnelingCapability | 
                                       XNetworkProxy_HostNameLookupCapability;
    XNetworkProxy_setCapabilities(&proxy, caps);
    
    XPrintf("设置能力: Tunneling | HostNameLookup\n");
    XPrintf("  能力值: 0x%04x\n", XNetworkProxy_capabilities(&proxy));
    XPrintf("  透明代理: %s\n", XNetworkProxy_isTransparentProxy(&proxy) ? "是" : "否");
    
    // 添加更多能力
    caps |= XNetworkProxy_UdpTunnelingCapability;
    XNetworkProxy_setCapabilities(&proxy, caps);
    XPrintf("\n添加 UDP 隧道能力:\n");
    XPrintf("  能力值: 0x%04x\n", XNetworkProxy_capabilities(&proxy));
    
    XNetworkProxy_deinit_base(&proxy);
}

static void testProxy_setters(void)
{
    XPrintf("\n========== XNetworkProxy Setter 测试 ==========\n");
    
    XNetworkProxy proxy;
    XNetworkProxy_init(&proxy);
    
    // 设置类型
    XNetworkProxy_setType(&proxy, XNetworkProxy_HttpProxy);
    XPrintf("设置类型为 HttpProxy: %d\n", XNetworkProxy_type(&proxy));
    
    // 设置主机名
    XString* host = XString_create_utf8("192.168.1.100");
    XNetworkProxy_setHostName(&proxy, host);
    const XString* proxyHost = XNetworkProxy_hostName_const(&proxy);
    if (proxyHost) {
        XPrintf("设置主机名: ");
        XPrintf_2(proxyHost);
        XPrintf("\n");
    }
    XString_delete_base(host);
    
    // 设置端口
    XNetworkProxy_setPort(&proxy, 8080);
    XPrintf("设置端口: %d\n", XNetworkProxy_port(&proxy));
    
    // 设置用户名
    XString* user = XString_create_utf8("admin");
    XNetworkProxy_setUser(&proxy, user);
    const XString* proxyUser = XNetworkProxy_user_const(&proxy);
    if (proxyUser) {
        XPrintf("设置用户名: ");
        XPrintf_2(proxyUser);
        XPrintf("\n");
    }
    XString_delete_base(user);
    
    // 设置密码
    XString* pass = XString_create_utf8("secret123");
    XNetworkProxy_setPassword(&proxy, pass);
    const XString* proxyPass = XNetworkProxy_password_const(&proxy);
    if (proxyPass) {
        XPrintf("设置密码: ");
        XPrintf_2(proxyPass);
        XPrintf("\n");
    }
    XString_delete_base(pass);
    
    XNetworkProxy_deinit_base(&proxy);
}

static void testProxy_copy(void)
{
    XPrintf("\n========== XNetworkProxy 拷贝测试 ==========\n");
    
    XString* host = XString_create_utf8("proxy.test.com");
    XString* user = XString_create_utf8("testuser");
    XString* pass = XString_create_utf8("testpass");
    
    XNetworkProxy* original = XNetworkProxy_create_2(XNetworkProxy_Socks5Proxy, host, 1080, user, pass);
    XString_delete_base(host);
    XString_delete_base(user);
    XString_delete_base(pass);
    
    if (original) {
        // 使用拷贝函数
        XNetworkProxy* copied = XNetworkProxy_copy(original);
        if (copied) {
            XPrintf("原始代理端口: %d\n", XNetworkProxy_port(original));
            XPrintf("拷贝代理端口: %d\n", XNetworkProxy_port(copied));
            
            // 比较相等性
            if (XNetworkProxy_equal(original, copied)) {
                XPrintf("拷贝测试: PASSED\n");
            } else {
                XPrintf("拷贝测试: FAILED\n");
            }
            
            XNetworkProxy_delete_base(copied);
        }
        
        // 栈上拷贝
        XNetworkProxy stackCopy;
        XNetworkProxy_init(&stackCopy);
        XNetworkProxy_copy_base(&stackCopy, original);
        
        XPrintf("栈上拷贝端口: %d\n", XNetworkProxy_port(&stackCopy));
        
        XNetworkProxy_deinit_base(&stackCopy);
        XNetworkProxy_delete_base(original);
    }
}

static void testProxy_applicationProxy(void)
{
    XPrintf("\n========== 应用级代理测试 ==========\n");
    
    // 创建代理
    XString* host = XString_create_utf8("global-proxy.example.com");
    XNetworkProxy* appProxy = XNetworkProxy_create_2(XNetworkProxy_HttpProxy, host, 3128, NULL, NULL);
    XString_delete_base(host);
    
    if (appProxy) {
        // 设置应用级代理
        XNetworkProxy_setApplicationProxy(appProxy);
        XPrintf("设置应用级代理成功\n");
        
        // 获取应用级代理
        XNetworkProxy* retrieved = XNetworkProxy_applicationProxy();
        if (retrieved) {
            XPrintf("获取应用级代理:\n");
            XPrintf("  类型: %d\n", XNetworkProxy_type(retrieved));
            XPrintf("  端口: %d\n", XNetworkProxy_port(retrieved));
            
            const XString* proxyHost = XNetworkProxy_hostName_const(retrieved);
            if (proxyHost) {
                XPrintf("  主机名: ");
                XPrintf_2(proxyHost);
                XPrintf("\n");
            }
        }
        
        // 清除应用级代理
        XNetworkProxy_setApplicationProxy(NULL);
        XPrintf("清除应用级代理\n");
        
        XNetworkProxy_delete_base(appProxy);
    }
}

// ==================== XNetworkProxyFactory 测试 ====================

static XNetworkProxy* testQueryFunc(XNetworkProxyFactory* factory, const XNetworkProxyQuery* query)
{
    XPrintf("  自定义工厂查询被调用\n");
    XPrintf("  查询类型: %d\n", XNetworkProxyQuery_queryType(query));
    XPrintf("  目标端口: %d\n", XNetworkProxyQuery_peerPort(query));
    
    // 返回一个固定的代理配置
    XString* host = XString_create_utf8("custom-proxy.example.com");
    XNetworkProxy* proxy = XNetworkProxy_create_2(XNetworkProxy_HttpProxy, host, 8888, NULL, NULL);
    XString_delete_base(host);
    
    return proxy;
}

static void testProxyFactory_basic(void)
{
    XPrintf("\n========== XNetworkProxyFactory 基础测试 ==========\n");
    
    // 创建工厂
    XNetworkProxyFactory* factory = XNetworkProxyFactory_create(testQueryFunc);
    if (factory) {
        XPrintf("工厂创建成功\n");
        
        // 创建查询
        XString* hostname = XString_create_utf8("www.example.com");
        XNetworkProxyQuery* query = XNetworkProxyQuery_create_3(hostname, 80, NULL, XNetworkProxyQuery_TcpSocket);
        XString_delete_base(hostname);
        
        if (query) {
            // 使用工厂查询代理
            XNetworkProxy* proxy = XNetworkProxyFactory_queryProxy(factory, query);
            if (proxy) {
                XPrintf("查询结果:\n");
                XPrintf("  类型: %d\n", XNetworkProxy_type(proxy));
                XPrintf("  端口: %d\n", XNetworkProxy_port(proxy));
                
                const XString* proxyHost = XNetworkProxy_hostName_const(proxy);
                if (proxyHost) {
                    XPrintf("  主机名: ");
                    XPrintf_2(proxyHost);
                    XPrintf("\n");
                }
                
                XNetworkProxy_delete_base(proxy);
            }
            
            XNetworkProxyQuery_delete_base(query);
        }
        
        XNetworkProxyFactory_delete_base(factory);
    }
}

static void testProxyFactory_systemConfig(void)
{
    XPrintf("\n========== 系统代理配置测试 ==========\n");
    
    // 检查是否使用系统配置
    bool usingSystem = XNetworkProxyFactory_usesSystemConfiguration();
    XPrintf("当前是否使用系统代理配置: %s\n", usingSystem ? "是" : "否");
    
    // 启用系统代理配置
    XNetworkProxyFactory_setUseSystemConfiguration(true);
    XPrintf("启用系统代理配置\n");
    
    usingSystem = XNetworkProxyFactory_usesSystemConfiguration();
    XPrintf("当前是否使用系统代理配置: %s\n", usingSystem ? "是" : "否");
    
    // 创建查询并获取系统代理
    XString* hostname = XString_create_utf8("www.google.com");
    XNetworkProxyQuery* query = XNetworkProxyQuery_create_3(hostname, 443, NULL, XNetworkProxyQuery_TcpSocket);
    XString_delete_base(hostname);
    
    if (query) {
        XNetworkProxy* systemProxy = XNetworkProxyFactory_systemProxyForQuery(query);
        if (systemProxy) {
            XPrintf("系统代理配置:\n");
            XPrintf("  类型: %d\n", XNetworkProxy_type(systemProxy));
            XPrintf("  端口: %d\n", XNetworkProxy_port(systemProxy));
            
            const XString* proxyHost = XNetworkProxy_hostName_const(systemProxy);
            if (proxyHost) {
                XPrintf("  主机名: ");
                XPrintf_2(proxyHost);
                XPrintf("\n");
            }
            
            XNetworkProxy_delete_base(systemProxy);
        } else {
            XPrintf("无系统代理配置\n");
        }
        
        XNetworkProxyQuery_delete_base(query);
    }
    
    // 恢复默认
    XNetworkProxyFactory_setUseSystemConfiguration(false);
}

static void testProxyFactory_applicationFactory(void)
{
    XPrintf("\n========== 应用级代理工厂测试 ==========\n");
    
    // 创建并设置应用级工厂
    XNetworkProxyFactory* factory = XNetworkProxyFactory_create(testQueryFunc);
    if (factory) {
        XNetworkProxyFactory_setApplicationProxyFactory(factory);
        XPrintf("设置应用级代理工厂成功\n");
        
        // 使用 proxyForQuery 查询
        XString* hostname = XString_create_utf8("www.test.com");
        XNetworkProxyQuery* query = XNetworkProxyQuery_create_3(hostname, 8080, NULL, XNetworkProxyQuery_TcpSocket);
        XString_delete_base(hostname);
        
        if (query) {
            XNetworkProxy* proxy = XNetworkProxyFactory_proxyForQuery(query);
            if (proxy) {
                XPrintf("通过应用级工厂查询:\n");
                XPrintf("  类型: %d\n", XNetworkProxy_type(proxy));
                XPrintf("  端口: %d\n", XNetworkProxy_port(proxy));
                
                XNetworkProxy_delete_base(proxy);
            }
            
            XNetworkProxyQuery_delete_base(query);
        }
        
        // 清除应用级工厂
        XNetworkProxyFactory_setApplicationProxyFactory(NULL);
        XPrintf("清除应用级代理工厂\n");
        
        XNetworkProxyFactory_delete_base(factory);
    }
}

// ==================== 综合测试 ====================

static void XNetworkProxy_comprehensiveTest(void)
{
    XPrintf("\n========== XNetworkProxy 综合测试 ==========\n");
    
    // Query 测试
    testProxyQuery_basic();
    testProxyQuery_setters();
    testProxyQuery_copy();
    
    // Proxy 测试
    testProxy_basic();
    testProxy_types();
    testProxy_capabilities();
    testProxy_setters();
    testProxy_copy();
    testProxy_applicationProxy();
    
    // Factory 测试
    testProxyFactory_basic();
    testProxyFactory_systemConfig();
    testProxyFactory_applicationFactory();
    
    XPrintf("\n========== 测试完成 ==========\n");
}

// ==================== 菜单注册 ====================

void XMenu_XNetworkProxyTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XNetworkProxy(网络代理)");
    XMenu_addMenu(root, menu);
    
    {
        XAction* action = XMenu_addAction(menu, "综合测试");
        XAction_setAction(action, XNetworkProxy_comprehensiveTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "ProxyQuery基础测试");
        XAction_setAction(action, testProxyQuery_basic);
    }
    {
        XAction* action = XMenu_addAction(menu, "ProxyQuery Setter测试");
        XAction_setAction(action, testProxyQuery_setters);
    }
    {
        XAction* action = XMenu_addAction(menu, "Proxy基础测试");
        XAction_setAction(action, testProxy_basic);
    }
    {
        XAction* action = XMenu_addAction(menu, "Proxy类型测试");
        XAction_setAction(action, testProxy_types);
    }
    {
        XAction* action = XMenu_addAction(menu, "Proxy能力测试");
        XAction_setAction(action, testProxy_capabilities);
    }
    {
        XAction* action = XMenu_addAction(menu, "Proxy Setter测试");
        XAction_setAction(action, testProxy_setters);
    }
    {
        XAction* action = XMenu_addAction(menu, "Proxy拷贝测试");
        XAction_setAction(action, testProxy_copy);
    }
    {
        XAction* action = XMenu_addAction(menu, "应用级代理测试");
        XAction_setAction(action, testProxy_applicationProxy);
    }
    {
        XAction* action = XMenu_addAction(menu, "ProxyFactory基础测试");
        XAction_setAction(action, testProxyFactory_basic);
    }
    {
        XAction* action = XMenu_addAction(menu, "系统代理配置测试");
        XAction_setAction(action, testProxyFactory_systemConfig);
    }
    {
        XAction* action = XMenu_addAction(menu, "应用级工厂测试");
        XAction_setAction(action, testProxyFactory_applicationFactory);
    }
}