#include"XIOTest.h"
#include"XTcpSocket.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XByteArray.h"
#include"XCoreApplication.h"
#include"XSocketNotifier.h"
#include"XHostInfo.h"
void XSocketTest();
static void readData(XObject* sender, XVarList* args)
{
	XByteArray* data= XTcpSocket_readAll_2(sender);
	for_each_iterator(data, XByteArray,it)
	{
		putchar(XByteArray_iterator_data(&it));
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
	for_each_iterator(data, XByteArray, it)
	{
		putchar(XByteArray_iterator_data(&it));
	}
	XByteArray_delete_base(data);
	XPrintf_3("[百度] 连接关闭\n");
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
	XObject_connect_2(socket, XSignal(XIODevice_readyRead_signal), readDataBaidu);
	XAbstractSocket_connectToHost_base(socket, "www.baidu.com", 80, XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
	bool connected = XTcpSocket_waitForConnected_base(socket, 5000);
	if (connected) {
		XPrintf("[百度] TCP连接成功，发送HTTP GET请求...\n");
		const char* httpReq = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\nConnection: close\r\n\r\n";
		XTcpSocket_write_1(socket, httpReq, (int)strlen(httpReq));
		XPrintf("[百度] 已发送HTTP请求，等待响应...\n");
	} else {
		XPrintf("[百度] TCP连接超时(5s)\n");
	}
	XCoreApplication_exec();
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
void XMenu_XSocketTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XSocket(网络客户端)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XSocketTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "百度外网测试");
		XAction_setAction(action, XSocketTest_Baidu);
	}
}
