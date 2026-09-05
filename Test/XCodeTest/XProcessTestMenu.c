/**
 * @file XProcessTestMenu.c
 * @brief 将 XProcess 全量测试接入现有代码测试菜单。
 */

#include "XCodeTest.h"
#include "XTestMenu.h"
#include "XAction.h"

static void XProcessTest_runAll_wrapper(XVariant* data)
{
    (void)data;
    XProcessTest_runAll();
}

void XTestMenu_XProcessTest(XTestMenu* root)
{
    XTestMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XTestMenu_create("XProcess(Qt6.8)");
    if (!menu) return;
    XTestMenu_addMenu(root, menu);
    action = XTestMenu_addAction(menu, "全量测试");
    if (action) XTestMenu_setActionFunction(action, XProcessTest_runAll_wrapper);
}
