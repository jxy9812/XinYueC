#include "XNetworkTest.h"
#include "XTestMenu.h"
#include "XFtpTest.h"
#include "XHttpTest.h"
#include "XServerChanTest.h"

void XTestMenu_XNetworkTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("网络(Network)");
    XTestMenu_addMenu(root, menu);

    // 添加网络相关测试
    XTestMenu_XNetworkInterfaceTest(menu);
    XTestMenu_XNetworkAddressEntryTest(menu);
    XTestMenu_XNetworkProxyTest(menu);
    XFtpTest_registerAll(menu);
    XHttpTest_registerAll(menu);
    XServerChanTest_registerAll(menu);
}
