#ifndef XMENU_H
#define XMENU_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XHierarchicalTree.h"
#include"XTypes.h"
#include"XAction.h"

/**
 * @brief 菜单动作绑定的测试函数类型（供 XTestMenu_setActionFunction 使用）。
 * @param data 动作绑定时通过 XAction_setData 保存的数据；可为 NULL，
 *             绑定方通常忽略该参数。
 */
typedef void (*XTestMenuActionFunc)(XVariant* data);
//菜单
typedef struct XTestMenu
{
	XHTreeNode m_class;
	void* m_userData;
}XTestMenu;
size_t XTestMenu_typeSize();
XTestMenu* XTestMenu_create(const char* title);
void XTestMenu_init(XTestMenu* menu, size_t treeNodeSize,const char* title);
void XTestMenu_setTitle(XTestMenu* menu, const char* title);
const char* XTestMenu_getTitle(XTestMenu* menu);

XAction* XTestMenu_addAction(XTestMenu* menu, const char* text);
/**
 * @brief 把测试函数绑定到菜单动作（对标连接 XAction triggered 信号）。
 * @details 绑定经 XTestMenu_addAction 创建的菜单动作；动作被触发（trigger）
 *          时通过其 triggered 信号运行绑定函数。绑定会占用动作的用户
 *          数据槽（XAction_data），因此被绑定的菜单动作不应再调用
 *          XAction_setData。
 * @param action 已加入菜单的动作对象；可为 NULL。
 * @param func 要运行的函数指针；可为 NULL 表示解绑。
 * @return 无返回值。
 */
void XTestMenu_setActionFunction(XAction* action, XTestMenuActionFunc func);
bool XTestMenu_removeAction(XTestMenu* menu, XAction* action);
const XVector* XTestMenu_getActions(XTestMenu* menu);

bool XTestMenu_addMenu(XTestMenu* menu, XTestMenu* newMenu);
bool XTestMenu_removeMenu(XTestMenu* menu);
XVector* XTestMenu_getMenus(XTestMenu* menu);
void XTestMenu_delete(XTestMenu* menu);
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
