#ifndef XIOTEST_H
#define XIOTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XIOTest(XMenu* root);
	void XMenu_XSerialPortTest(XMenu* root);
	void XMenu_XSocketTest(XMenu* root);
	void XMenu_XSslTest(XMenu* root);
	void XMenu_XUdpSocketTest(XMenu* root);
	void XMenu_XTcpSocketTest(XMenu* root);
	void XMenu_XTcpServerTest(XMenu* root);
	void XMenu_XHostInfoTest(XMenu* root);
	void XMenu_XDirTest(XMenu* root);
	void XMenu_XFileTest(XMenu* root);
	void XMenu_XFileInfoTest(XMenu* root);
	void XMenu_XSaveFileTest(XMenu* root);
	void XMenu_XFileDescriptorTest(XMenu* root);
	void XPWMDeviceTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
