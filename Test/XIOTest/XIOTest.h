#ifndef XIOTEST_H
#define XIOTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST_ON
	void XTestMenu_XIOTest(XTestMenu* root);
	void XTestMenu_XSerialPortTest(XTestMenu* root);
	void XTestMenu_XSocketTest(XTestMenu* root);
	void XTestMenu_XSslTest(XTestMenu* root);
	void XTestMenu_XUdpSocketTest(XTestMenu* root);

	/* UDP 定向收发与源地址校验回归（命令行 --test udp-directed）：
	   针对 Linux io_uring 驱动 UDP 目的地址丢失 / connect 未落实 /
	   取源地址返回垃圾数据三处缺陷建立的红线测试。 */
	int  XUdpDirectedTest_run(void);
	void XUdpDirectedTest(void);
	void XTestMenu_XTcpSocketTest(XTestMenu* root);
	void XTestMenu_XTcpServerTest(XTestMenu* root);

	/* XTcpServer 命令行自动回归（--test xtcpserver）：进程内自跑全部用例，
	   返回 0 表示全通过。供 CI/联调脚本化验证。 */
	int  XTcpServerTest_run(void);

	/* 套接字「读挂起后不再收数据」专项回归（--test xsocket-stall）：
	   长空闲/高频/背压/双向/重连/串包六组，返回失败数（0=全通过）。 */
	int  XSocketStallTest_run(void);
	void XTestMenu_XHostInfoTest(XTestMenu* root);
	void XTestMenu_XDirTest(XTestMenu* root);
	void XTestMenu_XFileTest(XTestMenu* root);
	void XTestMenu_XFileInfoTest(XTestMenu* root);
	void XTestMenu_XSaveFileTest(XTestMenu* root);
	void XTestMenu_XFileDescriptorTest(XTestMenu* root);
	void XPWMDeviceTest();
#endif // DEMOTEST_ON

#ifdef __cplusplus
}
#endif	
#endif
