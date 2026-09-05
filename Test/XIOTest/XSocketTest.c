#include "XPrintf.h"
#include"XIOTest.h"
#include"XTcpSocket.h"
#include"XSslSocket.h"
#include"XMemory.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XByteArray.h"
#include"XCoreApplication.h"
#include"XSocketNotifier.h"
#include"XHostInfo.h"
#include"XThread.h"
void XSocketTest();
static bool g_baiduRequestFinished = false;
static void readData(XObject* sender, XVarList* args)
{
	XByteArray* data= XTcpSocket_readAll_2(sender);
	for_each_iterator(data, XByteArray,it)
	{
		XPrintf("%c", XByteArray_iterator_data(&it));
	}
	XByteArray_delete_base(data);
	//XPrintf_3(XContainerDataAddr(data));
}

static void XSocketNotifierSlot(XObject* sender, XVarList* args)
{
	XVarList_args_2(args, XSocketDescriptor ,socket, XSocketNotifierType ,type);
	XPrintf_3("套接字监视\n");
}
static void readDataBaidu(XObject* sender, XVarList* args)
{
	XByteArray* data = XTcpSocket_readAll_2(sender);
	if (data && XByteArray_size_base(data) > 0)
	{
		/* HTTP 响应可能分多次到达，按实际字节数输出，避免依赖字符串终止符。 */
		XPrintf("[百度] 收到HTTP响应数据:\n");
		XPrintf(XByteArray_data(data), 1, XByteArray_size_base(data), stdout);
		fflush(stdout);
	}
	XByteArray_delete_base(data);
}

static void onBaiduDisconnected(XObject* sender, XVarList* args)
{
	(void)sender; (void)args;
	g_baiduRequestFinished = true;
	XPrintf("[百度] 连接已断开\n");
}

void XSocketTest_Baidu()
{
	XPrintf("=== XSocketTest 百度外网测试 ===\n");

	/* 解析百度域名，用 lwIP DNS */
	XHostInfo* info = XHostInfo_fromName2("www.baidu.com");
	if (info && XHostInfo_error(info) == XHostInfo_NoError) {
		const XVector* address = XHostInfo_addresses_const(info);
		if (address && XVector_count_base(address) > 0) {
			const XHostAddress* addr = (const XHostAddress*)XVector_at_base(address, 0);
			XString* ipStr = XHostAddress_toString(addr);
			XPrintf_2(ipStr);
			XPrintf("\n");
			XString_delete_base(ipStr);
		} else {
			XPrintf("[百度] DNS解析: 无IP地址\n");
		}
		XHostInfo_delete_base(info);
	} else {
		XPrintf("[百度] DNS解析失败\n");
		return;
	}

	/* 发起 HTTP GET 请求 */
	XSocket* socket = XTcpSocket_create();
	g_baiduRequestFinished = false;
	XObject_connect_2(socket, XSignal(XIODevice_readyRead_signal), readDataBaidu);
	XObject_connect_2(socket, XSignal(XAbstractSocket_disconnected_signal), onBaiduDisconnected);
	XAbstractSocket_connectToHost_base(socket, "www.baidu.com", 80, XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
	bool connected = XTcpSocket_waitForConnected_base(socket, 5000);
	if (connected) {
		XPrintf("[百度] TCP连接成功，发送HTTP GET请求...\n");
		const char* httpReq = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
		XTcpSocket_write_1(socket, httpReq, (int)strlen(httpReq));
		XPrintf("[百度] 已发送HTTP请求，等待响应...\n");
	} else {
		XPrintf("[百度] TCP连接超时(5s)\n");
		XClass_delete_base((XClass*)socket);
		return;
	}

	/*
	 * 测试可能从菜单动作或其他已经运行的事件循环中调用，不能再次
	 * 启动 XCoreApplication_exec()。使用 processEvents 等待响应，既能
	 * 驱动网络回调，也不会退出宿主应用的事件循环。
	 */
	XPrintf("[百度] 处理事件并等待HTTP响应...\n");
	for (int i = 0; i < 500 && !g_baiduRequestFinished; ++i)
	{
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		XThread_msleep(10);
	}
	if (!g_baiduRequestFinished)
	{
		XPrintf("[百度] 等待HTTP响应超时，主动断开连接\n");
		XTcpSocket_abort(socket);
	}
	XClass_delete_base((XClass*)socket);
}

/* ===================================================================
 * 百度 HTTPS 外网测试（XSslSocket，对齐 QSslSocket）
 * -------------------------------------------------------------------
 * 独立菜单入口：验证 mbedTLS 后端 + XSslSocket 继承 XTcpSocket 的
 * 明文 read/write 语义。发送 HTTPS GET，握手 -> 加密 IO -> 关闭。
 * =================================================================== */
void XSocketTest_BaiduHttps()
{
	XPrintf("=== XSocketTest 百度 HTTPS 外网测试 (XSslSocket) ===\n");
	XPrintf("[百度HTTPS] SSL 后端: %s\n", XSsl_backendName());

	/* 1) DNS 解析（走 lwIP / 平台解析） */
	XHostInfo* info = XHostInfo_fromName2("www.baidu.com");
	if (info && XHostInfo_error(info) == XHostInfo_NoError) {
		const XVector* address = XHostInfo_addresses_const(info);
		if (address && XVector_count_base(address) > 0) {
			const XHostAddress* addr = (const XHostAddress*)XVector_at_base(address, 0);
			XString* ipStr = XHostAddress_toString(addr);
			XPrintf("[百度HTTPS] DNS: ");
			XPrintf_2(ipStr);
			XPrintf("\n");
			XString_delete_base(ipStr);
		} else {
			XPrintf("[百度HTTPS] DNS 解析: 无 IP 地址\n");
		}
		XHostInfo_delete_base(info);
	} else {
		XPrintf("[百度HTTPS] DNS 解析失败\n");
		return;
	}

	/* 2) 创建 XSslSocket，客户端模式 */
	XSslSocket* ssl = XSslSocket_create();
	XSslSocket_setProtocol(ssl, XSSL_SecureProtocols);
	XSslSocket_setPeerVerifyMode(ssl, XSSL_VerifyNone); /* 无 CA，只验证握手成功 */
	XString* verifyName = XString_create_utf8("www.baidu.com");
	XSslSocket_setPeerVerifyName(ssl, verifyName);
	XString_delete_base(verifyName);

	/* 3) 发起 TCP + TLS 加密连接 */
	XPrintf("[百度HTTPS] 发起 TCP+TLS 加密连接 www.baidu.com:443 ...\n");
	XString* hostName = XString_create_utf8("www.baidu.com");
	XSslSocket_connectToHostEncrypted(ssl, hostName, 443);
	XString_delete_base(hostName);

	if (!XSslSocket_waitForConnected_base(ssl, 10000)) {
		XPrintf("[百度HTTPS] TCP 连接超时(10s)\n");
		XClass_delete_base((XClass*)ssl);
		return;
	}
	XPrintf("[百度HTTPS] TCP 连接成功，等待 TLS 握手...\n");

	if (!XSslSocket_waitForEncrypted(ssl, 15000)) {
		XPrintf("[百度HTTPS] TLS 握手失败/超时(15s)\n");
		XClass_delete_base((XClass*)ssl);
		return;
	}
	XString* proto = XSslSocket_sessionProtocol((const XSslSocket*)ssl);
	XString* cipher = XSslSocket_sessionCipher((const XSslSocket*)ssl);
	XPrintf("[百度HTTPS] TLS 握手成功: proto=%s cipher=%s\n",
		XString_toUtf8(proto),
		XString_toUtf8(cipher));
	XString_delete_base(proto);
	XString_delete_base(cipher);

	/* 4) 明文接口发送 HTTPS GET（内部自动加密） */
	/*const char* httpReq =
		"GET / HTTP/1.1\r\n"
		"Host: www.baidu.com\r\n"
		"User-Agent: XinYueC-XSslSocket-Test/1.0\r\n"
		"Connection: close\r\n"
		"\r\n";*/
	const char* httpReq = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
	int64_t wrote = XSslSocket_write_1(ssl, httpReq, (int64_t)strlen(httpReq));
	XPrintf("[百度HTTPS] 已发送 HTTPS GET 请求 (%lld bytes)，等待响应...\n", (long long)wrote);

	/* 5) 同步循环读取解密后的明文响应 */
	char buf[4096];
	int64_t total = 0;
	while (XSslSocket_waitForReadyRead_base(ssl, 10000)) {
		int64_t n = XSslSocket_read_1(ssl, buf, (int64_t)sizeof(buf) - 1);
		if (n <= 0) break;
		buf[n] = '\0';
		XPrintf(buf, 1, (size_t)n, stdout);
		total += n;
	}
	XPrintf("\n[百度HTTPS] HTTPS 响应接收完成，共 %lld 字节\n", (long long)total);

	XSslSocket_disconnectFromHost_base(ssl);
	XPrintf("[百度HTTPS] 连接已关闭\n");
	XClass_delete_base((XClass*)ssl);
}

void XSocketTest()
{
	XPrintf("=== XSocketTest 开始 ===\n");

	XSocket* socket = XTcpSocket_create();
	XObject_connect_2(socket,XSignal(XIODevice_readyRead_signal), readData);
	XAbstractSocket_connectToHost_base(socket, "192.168.1.117", 502, XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
	bool conn = XTcpSocket_waitForConnected_base(socket, 10000);
	XPrintf("[测试] TCP连接到192.168.1.117:502 %s\n", conn ? "成功" : "失败");
	XFd fd = XAbstractSocket_fd(socket);
	XSocketNotifier* notifier = XSocketNotifier_createWithSocket(fd, XSocketNotifier_Read);
	XObject_connect_2(notifier, XSignal(XSocketNotifier_activated_signal), XSocketNotifierSlot);
	XTcpSocket_write_1(socket,"hello",6);
	XCoreApplication_exec();
}
void XTestMenu_XSocketTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XSocket(网络客户端)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XSocketTest);
	}
	{
		XAction* action = XTestMenu_addAction(menu, "百度外网测试");
		XTestMenu_setActionFunction(action, XSocketTest_Baidu);
	}
	{
		XAction* action = XTestMenu_addAction(menu, "百度HTTPS测试");
		XTestMenu_setActionFunction(action, XSocketTest_BaiduHttps);
	}
}
