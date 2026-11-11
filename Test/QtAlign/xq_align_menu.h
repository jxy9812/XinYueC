/**
 * @file       xq_align_menu.h
 * @brief      Qt 6.8 对齐测试的 Test 菜单入口声明。
 * @details    菜单树由 Test/XTestMenuTest.c 统一挂载；动作函数调用
 *             xq_align_run(0, NULL) 跑全部自注册用例。
 * @author     XinYueC 团队
 */
#ifndef XQ_ALIGN_MENU_H
#define XQ_ALIGN_MENU_H

#include "XTestMenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 在 Test 菜单根节点下挂载「Qt6.8 对齐(Test/QtAlign)」子菜单。
 * @param root 菜单根节点（XTestMenuTest_create 创建）；可为 NULL。
 */
void XTestMenu_XQtAlignTest(XTestMenu* root);

#ifdef __cplusplus
}
#endif

#endif /* XQ_ALIGN_MENU_H */
