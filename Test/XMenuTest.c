#include "XDataStructTest.h"
#include "XMenuTest.h"
#include "XMenu.h"
#include "XCoreApplication.h"
#include "XString.h"
#include "XPrintf.h"
#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
#include "XMemory.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include"XContainerTest.h"
#include"XProtocolTest.h"
#include"XIOTest.h"
#include"XCodeTest.h"
#include"XTimerTest.h"
#include"XLibraryTest.h"
#include"XDeviceTest.h"
#include"XMemoryTest.h"
#include"XDataTest.h"
XMenu* XMenuTest_create()
{
	XMenu* root = XMenu_create("测试代码");
	XMenu_XLibraryTest(root);
	XMenu_XContainerTest(root);
	XMenu_XCodeTest(root);
	XMenu_XIOTest(root);
	XMenu_XDeviceTest(root);
	XMenu_XProtocolTest(root);
	XMenu_XTimerTest(root);
	XMenu_XMemoryTest(root);
	XMenu_XDataTest(root);
	return root;
}

typedef struct MenuData
{
	Action  action;
	void* data;
	XMenu** menu;
}MenuData;

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
struct XMenuTestRemoteSession
{
	XMenu* menu;
	int column;
	XVector* choices;
};
#endif

/*
 * @brief 按行读取菜单命令，避免 scanf 保留换行符导致下一层菜单误读输入。
 * @param command 输出缓冲区
 * @param size 缓冲区容量
 * @return 成功读取返回 true，输入结束返回 false
 */
static bool XMenuTest_readCommand(char* command, size_t size)
{
	int ch;
	if (!command || size < 2 || !fgets(command, (int)size, stdin))
		return false;
	if (!strchr(command, '\n') && !strchr(command, '\r'))
	{
		while ((ch = getchar()) != '\n' && ch != '\r' && ch != EOF)
			;
	}
	command[strcspn(command, "\r\n")] = '\0';
	return true;
}
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
		//XCoreApplication_instance()->m_quit = false;
		XAction_trigger(data->data);
		//if (!(XCoreApplication_instance()->m_quit))
			//*((int*)(&(menu->m_userData)))=XCoreApplication_exec();//有初始化XObject派生类需要调用事件循环
		XPrintf("\n结束---------------%s---------------\n", XAction_getText(data->data));
	}
}

/* 构建并显示一页菜单；menuRef 必须指向调用方长期有效的当前菜单指针。 */
static bool XMenuTest_renderPage(XMenu** menuRef, int column, XVector* choices)
{
	XMenu* menu;
	const XVector* actions;
	XVector* menus;
	MenuData data = { 0 };
	size_t menuSize;
	int i;
	if (!menuRef || !(menu = *menuRef) || !choices || column <= 0)
		return false;
	XVector_clear_base(choices);
	data.menu = menuRef;
	actions = XMenu_getActions(menu);
	menus = XMenu_getMenus(menu);
	if (!actions || !menus)
	{
		if (menus) XVector_delete_base(menus);
		return false;
	}
	XPrintf("\n---------------%s---------------\n", XMenu_getTitle(menu));
	if (XTreeNode_GetParent(menu))
	{
		XPrintf("%d 返回上级目录 -----返回\n", XContainerSize(choices));
		data.action = gotoParent;
		data.data = menu;
		XVector_push_back_1_base(choices, &data);
	}
	menuSize = XVector_size_base(menus);
	for (i = 0; i < (int)menuSize; ++i)
	{
		XMenu* child = XVector_At_Base(menus, i, XMenu*);
		XPrintf("%02d--菜单 %-30s\t", XContainerSize(choices),
				XMenu_getTitle(child));
		if ((i + 1) % column == 0 || (size_t)(i + 1) == menuSize)
			XPrintf("\n");
		data.action = gotoChild;
		data.data = child;
		XVector_push_back_1_base(choices, &data);
	}
	for (i = 0; i < (int)XVector_size_base(actions); ++i)
	{
		XAction* child = XVector_At_Base(actions, i, XAction*);
		XPrintf("%02d--项目 %-30s\t", XContainerSize(choices),
				XAction_getText(child));
		if (((size_t)i + 1u + menuSize) % (size_t)column == 0 ||
			((size_t)i + 1u + menuSize) == XVector_size_base(actions))
			XPrintf("\n");
		data.action = trigger;
		data.data = child;
		XVector_push_back_1_base(choices, &data);
	}
	XVector_delete_base(menus);
	XPrintf("---------------%s---------------\n", XMenu_getTitle(menu));
	XPrintf("请输入序号进行选择 0~%d,输入q退出\n", XContainerSize(choices) - 1);
	return true;
}

int XMenuTest_show(XMenu* menu, int column)
{
	XVector* v = XVector_Create(MenuData);
	char command[32] = {0};
	if (!v) return -1;
	while (true)
	{
		if (!XMenuTest_renderPage(&menu, column, v))
		{
			XVector_delete_base(v);
			return -1;
		}
		if (!XMenuTest_readCommand(command, sizeof(command)) ||
			strcmp(command, "q") == 0 || strcmp(command, "Q") == 0) {
			clearerr(stdin);
			XVector_delete_base(v);
			return 0;
		}
		if (command[0] == '\0')
			continue;
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

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
XMenuTestRemoteSession* XMenuTestRemoteSession_create(void)
{
	XMenuTestRemoteSession* session =
		(XMenuTestRemoteSession*)XCalloc_System(1, sizeof(*session));
	if (!session) return NULL;
	session->menu = XMenuTest_create();
	session->column = 1;
	session->choices = XVector_Create(MenuData);
	if (!session->menu || !session->choices)
	{
		XMenuTestRemoteSession_destroy(session);
		return NULL;
	}
	return session;
}

void XMenuTestRemoteSession_show(XMenuTestRemoteSession* session)
{
	if (!session) return;
	(void)XMenuTest_renderPage(&session->menu, session->column,
						   session->choices);
}

XMenuTestRemoteResult XMenuTestRemoteSession_processLine(
	XMenuTestRemoteSession* session, const char* line, size_t length)
{
	char command[32];
	char* end;
	long index;
	MenuData* data;
	if (!session || (!line && length) || length >= sizeof(command))
		return XMenuTestRemoteResult_Error;
	memcpy(command, line, length);
	command[length] = '\0';
	command[strcspn(command, "\r\n")] = '\0';
	if (strcmp(command, "q") == 0 || strcmp(command, "Q") == 0)
		return XMenuTestRemoteResult_Finished;
	if (command[0] == '\0')
	{
		XMenuTestRemoteSession_show(session);
		return XMenuTestRemoteResult_Active;
	}
	end = NULL;
	index = strtol(command, &end, 10);
	if (end == command || *end != '\0' || index < 0 ||
		index >= (long)XContainerSize(session->choices))
	{
		XPrintf("序号不合法请重新选择\n");
		return XMenuTestRemoteResult_Active;
	}
	data = XVector_at_base(session->choices, (int)index);
	if (!data || !data->action)
		return XMenuTestRemoteResult_Error;
	data->action(data);
	XMenuTestRemoteSession_show(session);
	return XMenuTestRemoteResult_Active;
}

void XMenuTestRemoteSession_destroy(XMenuTestRemoteSession* session)
{
	if (!session) return;
	if (session->choices) XVector_delete_base(session->choices);
	if (session->menu) XMenu_delete(session->menu);
	XFree_System(session);
}
#endif

int XMenuTest_run()
{
	XMenu* menu = XMenuTest_create();
	int code;
	if (!menu)
		return -1;
	code = XMenuTest_show(menu, 1);
	XMenu_delete(menu);
	return code;
}
