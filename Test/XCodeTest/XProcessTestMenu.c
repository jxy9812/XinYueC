/**
 * @file XProcessTestMenu.c
 * @brief 将 XProcess 全量测试接入现有代码测试菜单。
 */

#include "XCodeTest.h"
#include "XMenu.h"
#include "XAction.h"

static void XProcessTest_runAll_wrapper(XVariant* data)
{
    (void)data;
    XProcessTest_runAll();
}

void XMenu_XProcessTest(XMenu* root)
{
    XMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XMenu_create("XProcess(Qt6.8)");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "全量测试");
    if (action) XAction_setAction(action, XProcessTest_runAll_wrapper);
}
