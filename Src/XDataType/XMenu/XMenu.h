#ifndef XMENU_H
#define XMENU_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XHierarchicalTree.h"
#include"XTypes.h"
#include"XAction.h"
//菜单
typedef struct XMenu
{
	XHTreeNode m_parent;
}XMenu;

XMenu* XMenu_create(const char* title);
void XMenu_init(XMenu* menu,const char* title);
void XMenu_setTitle(XMenu* menu, const char* title);
const char* XMenu_getTitle(XMenu* menu);

XAction* XMenu_addAction(XMenu* menu, const char* text);
bool XMenu_removeAction(XMenu* menu, XAction* action);
const XVector* XMenu_getActions(XMenu* menu);

bool XMenu_addMenu(XMenu* menu, XMenu* newMenu);

XVector* XMenu_getMenus(XMenu* menu);
void XMenu_delete(XMenu* menu);
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
