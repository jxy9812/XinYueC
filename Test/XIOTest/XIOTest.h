#ifndef XIOTEST_H
#define XIOTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	void XTestMenu_XIOTest(XTestMenu* root);
	void XTestMenu_XSerialPortTest(XTestMenu* root);
	void XTestMenu_XSocketTest(XTestMenu* root);
	void XTestMenu_XSslTest(XTestMenu* root);
	void XTestMenu_XUdpSocketTest(XTestMenu* root);
	void XTestMenu_XTcpSocketTest(XTestMenu* root);
	void XTestMenu_XTcpServerTest(XTestMenu* root);
	void XTestMenu_XHostInfoTest(XTestMenu* root);
	void XTestMenu_XDirTest(XTestMenu* root);
	void XTestMenu_XFileTest(XTestMenu* root);
	void XTestMenu_XFileInfoTest(XTestMenu* root);
	void XTestMenu_XSaveFileTest(XTestMenu* root);
	void XTestMenu_XFileDescriptorTest(XTestMenu* root);
	void XPWMDeviceTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
