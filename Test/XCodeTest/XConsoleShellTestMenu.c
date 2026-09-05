/**
 * @file XConsoleShellTestMenu.c
 * @brief 将 XConsoleShell 测试接入现有代码测试菜单。
 */

#include "XCodeTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#include "XTestMenu.h"
#include "XAction.h"

static void XConsoleShellTest_runAll_wrapper(XVariant* data)
{
    (void)data;
    XConsoleShellTest_runAll();
}

static void XConsoleShellBackendTest_runAll_wrapper(XVariant* data)
{
    (void)data;
    XConsoleShellBackendTest_runAll();
}

void XTestMenu_XConsoleShellTest(XTestMenu* root)
{
    XTestMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XTestMenu_create("XConsoleShell");
    if (!menu) return;
    XTestMenu_addMenu(root, menu);
    action = XTestMenu_addAction(menu, "全量测试");
    if (action) XTestMenu_setActionFunction(action, XConsoleShellTest_runAll_wrapper);
    action = XTestMenu_addAction(menu, "TCP 后端端到端测试");
    if (action) XTestMenu_setActionFunction(action, XConsoleShellBackendTest_runAll_wrapper);
}

void XTestMenu_XConsoleShellBackendTest(XTestMenu* root)
{
    XTestMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XTestMenu_create("XConsoleShell TCP 后端");
    if (!menu) return;
    XTestMenu_addMenu(root, menu);
    action = XTestMenu_addAction(menu, "loopback 全量测试");
    if (action) XTestMenu_setActionFunction(action, XConsoleShellBackendTest_runAll_wrapper);
}
