/**
 * @file XConsoleShellTestMenu.c
 * @brief 将 XConsoleShell 测试接入现有代码测试菜单。
 */

#include "XCodeTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#include "XMenu.h"
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

void XMenu_XConsoleShellTest(XMenu* root)
{
    XMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XMenu_create("XConsoleShell");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "全量测试");
    if (action) XAction_setAction(action, XConsoleShellTest_runAll_wrapper);
    action = XMenu_addAction(menu, "TCP 后端端到端测试");
    if (action) XAction_setAction(action, XConsoleShellBackendTest_runAll_wrapper);
}

void XMenu_XConsoleShellBackendTest(XMenu* root)
{
    XMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XMenu_create("XConsoleShell TCP 后端");
    if (!menu) return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "loopback 全量测试");
    if (action) XAction_setAction(action, XConsoleShellBackendTest_runAll_wrapper);
}
