#include "XNetworkTest.h"
#include "XMenu.h"

void XMenu_XNetworkTest(XMenu* root)
{
    XMenu* menu = XMenu_create("网络(Network)");
    XMenu_addMenu(root, menu);
    
    // 添加网络相关测试
    XMenu_XNetworkInterfaceTest(menu);
    XMenu_XNetworkAddressEntryTest(menu);
}