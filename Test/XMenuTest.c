#include "XDataStructTest.h"
#include "XMenuTest.h"
#include "XMenu.h"
#include "XCoreApplication.h"
#include "XString.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
XMenu* XMenuTest_create()
{
	XMenu* root = XMenu_create("测试代码");
	XMenu_XLibraryTest(root);
	XMenu_XContainerTest(root);
	XMenu_XCodeTest(root);
	XMenu_XIOTest(root);
	XMenu_XProtocolStackTest(root);
	XMenu_XTimerTest(root);
	return root;
}

typedef struct MenuData
{
	Action  action;
	void* data;
	XMenu** menu;
}MenuData;
//跳转到上一级父菜单
static void gotoParent(MenuData* data)
{
	if (data->menu == NULL || data->data == NULL)
		return;
	XMenu* parent = XTreeNode_GetParent(data->data);
	*(data->menu) = parent;
}
//跳转到子菜单
static void gotoChild(MenuData* data)
{
	if (data->menu == NULL || data->data == NULL)
		return;
	*(data->menu) = data->data;
}
//触发动作
static void trigger(MenuData* data)
{
	if(data->data)
	{
		XMenu* menu = *(data->menu);
		XPrintf("\n开始---------------%s---------------\n", XAction_getText(data->data));
		XCoreApplication_global()->m_quit = false;
		XAction_trigger(data->data);
		if (!(XCoreApplication_global()->m_quit))
			*((int*)(&(menu->m_userData)))=XCoreApplication_exec();//有初始化XObject派生类需要调用事件循环
		XPrintf("\n结束---------------%s---------------\n", XAction_getText(data->data));
	}
}
void XMenuTest_show(XMenu* menu, int column)
{
	XMenu* parent =NULL;
	XVector* v=XVector_Create(MenuData);
	MenuData data = { 0 };
	data.menu = &menu;
	char command[10] = {0};
	while (true)
	{
		//int index = 0;
		XVector_clear_base(v);
		XVector* actions = XMenu_getActions(menu);
		XVector* menus = XMenu_getMenus(menu);
		XPrintf("\n---------------%s---------------\n", XMenu_getTitle(menu));
		//判断是否右父菜单
		parent = XTreeNode_GetParent(menu);
		if (parent)
		{
			XPrintf("%d 返回上级目录 -----返回\n",XContainerSize(v));
			data.action = gotoParent;
			data.data = menu;
			XVector_push_back_base(v,&data);
		}
		size_t menuSize = XVector_size_base(menus);
		for (int i = 0; i < menuSize; i++)
		{
			XMenu* child = XVector_At_Base(menus,i, XMenu*);
			XPrintf("%02d--菜单 %-30s\t", XContainerSize(v), XMenu_getTitle(child));
			if ((i + 1) % column == 0 || (i + 1) == menuSize)printf("\n");//换行
			data.action = gotoChild;
			data.data = child;
			XVector_push_back_base(v, &data);
		}
		for (int i = 0; i < XVector_size_base(actions); i++)
		{
			XAction* child = XVector_At_Base(actions, i, XAction*);
			XPrintf("%02d--项目 %-30s\t", XContainerSize(v), XAction_getText(child));
			if ((i + 1+ menuSize) % column == 0 || (i + 1+ menuSize) == XVector_size_base(actions))printf("\n");//换行
			data.action = trigger;
			data.data = child;
			XVector_push_back_base(v, &data);
		}
		XVector_delete_base(menus);
		XPrintf("---------------%s---------------\n", XMenu_getTitle(menu));
		XPrintf("请输入序号进行选择 0~%d,输入q退出\n", XContainerSize(v) - 1);
		scanf("%s",command);
		if (strcmp(command, "q") == 0)
			exit(0);
		int index=atoi(command);
		if (index < 0 || index >= XContainerSize(v))
		{
			XPrintf("序号不合法请重新选择\n");
			continue;
		}
		MenuData* pdata = XVector_at_base(v, index);
		pdata->action(pdata);
	}
}

int XMenuTest_run()
{
	XMenu* menu = XMenuTest_create();
	XMenuTest_show(menu, 1);
	int code = *((int*)(&(menu->m_userData)));
	XMenu_delete(menu);
	return code;
}
