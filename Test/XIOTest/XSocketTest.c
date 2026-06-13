#include"XIOTest.h"
#include"XTcpSocket.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XByteArray.h"
#include"XCoreApplication.h"
#include"XSocketNotifier.h"
#include"XHostInfo.h"
static void XSocketTest();
static void readData(XObject* sender, XVarList* args)
{
	XByteArray* data= XTcpSocket_readAll_2(sender);
	for_each_iterator(data, XByteArray,it)
	{
		putchar(XByteArray_iterator_data(&it));
	}
	XByteArray_delete_base(data);
	//XPrintf_utf8(XContainerDataAddr(data));
}

static void XSocketNotifierSlot(XObject* sender, XVarList* args)
{
	XVarList_args_2(args, XSocketDescriptor ,socket, XSocketNotifierType ,type);
	XPrintf_utf8("套接字监视\n");
}
void XSocketTest()
{
	//while (true)
	//{
	//	XHostInfo* info = XHostInfo_fromName2("wwww.baidu.com");
	//	const XVector* address = XHostInfo_addresses_const(info);
	//	for_each_iterator(address, XVector, it)
	//	{
	//		XHostAddress* addr = XVector_iterator_data(&it);
	//		XString* a = XHostAddress_toString(addr);
	//		XPrintf_string(a);
	//		putchar(0);
	//		XString_delete_base(a);
	//	}
	//	XHostInfo_delete_base(info);
	//	XCoreApplication_processEvents(XEventLoop_AllEvents);
	//}

	XSocket* socket = XTcpSocket_create();
	XObject_connect_2(socket,XSignal(XIODevice_readyRead_signal), readData);
	XAbstractSocket_connectToHost_base(socket, "192.168.1.117", 502, XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
	//XAbstractSocket_connectToHost_base(socket, "192.168.1.46", 6666, XIODevice_ReadWrite, XHostAddress_AnyIPProtocol);
	XTcpSocket_waitForConnected_base(socket, 3000);
	XSocketDescriptor s = XSocketDescriptor_fromIntptr(XTcpSocket_socketDescriptor_base(socket));
	XSocketNotifier* notifier = XSocketNotifier_createWithSocket(s,XSocketAct_Read);
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
}