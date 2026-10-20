#include"XProtocolTest.h"
#include"XTestMenu.h"
#include"XAction.h"
void XTestMenu_XProtocolTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("协议栈");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XModbusTest(menu);
	XTestMenu_XMqttTest(menu);
	XTestMenu_XCanTest(menu);
#if XPROTOCOL_ON && XLINK_ON
	/* XLink 私有应用层协议（控制进程 <-> GUI 进程 消息互通）。 */
	XTestMenu_XLinkTest(menu);
#endif
#if XPROTOCOL_ON && XNTP_ON
	/* XNtp SNTP 客户端（应用内校时）。 */
	XTestMenu_XNtpTest(menu);
#endif
#if XPROTOCOL_ON && XDDNS_ON
	/* XDdns 动态域名解析客户端（厂商抽象 + 阿里云）。 */
	XTestMenu_XDdnsTest(menu);
#endif
#if XPROTOCOL_ON && XPLC_ON && XS7_ON
	XTestMenu_XS7Test(menu);
#endif
}
