#include "XDataStructTest.h"
#include "XTestMenuTest.h"
#include "XTestMenu.h"
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
XTestMenu* XTestMenuTest_create()
{
	XTestMenu* root = XTestMenu_create("测试代码");
	XTestMenu_XLibraryTest(root);
	XTestMenu_XContainerTest(root);
	XTestMenu_XCodeTest(root);
	XTestMenu_XIOTest(root);
	XTestMenu_XDeviceTest(root);
	XTestMenu_XProtocolTest(root);
	XTestMenu_XTimerTest(root);
	XTestMenu_XMemoryTest(root);
	XTestMenu_XDataTest(root);
	return root;
}

typedef struct MenuData MenuData;
/** @brief 菜单条目处理器（返回上级、进入子菜单或触发动作）。 */
typedef void (*XTestMenuTestHandler)(MenuData* data);
struct MenuData
{
	XTestMenuTestHandler  action;
	void* data;
	XTestMenu** menu;
};

#if XCONSOLE_SHELL_REMOTE_OUTPUT_REDIRECT_ON
struct XTestMenuTestRemoteSession
{
	XTestMenu* menu;
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
static bool XTestMenuTest_readCommand(char* command, size_t size)
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
	XTestMenu* parent = XTreeNode_GetParent(data->data);
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
		XTestMenu* menu = *(data->menu);
		const XString* text = XAction_text_const((XAction*)data->data);
		XPrintf("\n开始---------------%s---------------\n",
			text ? XString_toUtf8(text) : "");
		//XCoreApplication_instance()->m_quit = false;
		XAction_trigger(data->data);
		//if (!(XCoreApplication_instance()->m_quit))
			//*((int*)(&(menu->m_userData)))=XCoreApplication_exec();//有初始化XObject派生类需要调用事件循环
		XPrintf("\n结束---------------%s---------------\n",
			text ? XString_toUtf8(text) : "");
	}
}

/* 构建并显示一页菜单；menuRef 必须指向调用方长期有效的当前菜单指针。 */
static bool XTestMenuTest_renderPage(XTestMenu** menuRef, int column, XVector* choices)
{
	XTestMenu* menu;
	const XVector* actions;
	XVector* menus;
	MenuData data = { 0 };
	size_t menuSize;
	int i;
	if (!menuRef || !(menu = *menuRef) || !choices || column <= 0)
		return false;
	XVector_clear_base(choices);
	data.menu = menuRef;
	actions = XTestMenu_getActions(menu);
	menus = XTestMenu_getMenus(menu);
	if (!actions || !menus)
	{
		if (menus) XVector_delete_base(menus);
		return false;
	}
	XPrintf("\n---------------%s---------------\n", XTestMenu_getTitle(menu));
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
		XTestMenu* child = XVector_At_Base(menus, i, XTestMenu*);
		XPrintf("%02d--菜单 %-30s\t", XContainerSize(choices),
				XTestMenu_getTitle(child));
		if ((i + 1) % column == 0 || (size_t)(i + 1) == menuSize)
			XPrintf("\n");
		data.action = gotoChild;
		data.data = child;
		XVector_push_back_1_base(choices, &data);
	}
	for (i = 0; i < (int)XVector_size_base(actions); ++i)
	{
		XAction* child = XVector_At_Base(actions, i, XAction*);
		const XString* text = XAction_text_const(child);
		XPrintf("%02d--项目 %-30s\t", XContainerSize(choices),
				text ? XString_toUtf8(text) : "");
		if (((size_t)i + 1u + menuSize) % (size_t)column == 0 ||
			((size_t)i + 1u + menuSize) == XVector_size_base(actions))
			XPrintf("\n");
		data.action = trigger;
		data.data = child;
		XVector_push_back_1_base(choices, &data);
	}
	XVector_delete_base(menus);
	XPrintf("---------------%s---------------\n", XTestMenu_getTitle(menu));
	XPrintf("请输入序号进行选择 0~%d,输入q退出\n", XContainerSize(choices) - 1);
	return true;
}

int XTestMenuTest_show(XTestMenu* menu, int column)
{
	XVector* v = XVector_Create(MenuData);
	char command[32] = {0};
	if (!v) return -1;
	while (true)
	{
		if (!XTestMenuTest_renderPage(&menu, column, v))
		{
			XVector_delete_base(v);
			return -1;
		}
		if (!XTestMenuTest_readCommand(command, sizeof(command)) ||
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
XTestMenuTestRemoteSession* XTestMenuTestRemoteSession_create(void)
{
	XTestMenuTestRemoteSession* session =
		(XTestMenuTestRemoteSession*)XCalloc_System(1, sizeof(*session));
	if (!session) return NULL;
	session->menu = XTestMenuTest_create();
	session->column = 1;
	session->choices = XVector_Create(MenuData);
	if (!session->menu || !session->choices)
	{
		XTestMenuTestRemoteSession_destroy(session);
		return NULL;
	}
	return session;
}

void XTestMenuTestRemoteSession_show(XTestMenuTestRemoteSession* session)
{
	if (!session) return;
	(void)XTestMenuTest_renderPage(&session->menu, session->column,
						   session->choices);
}

XTestMenuTestRemoteResult XTestMenuTestRemoteSession_processLine(
	XTestMenuTestRemoteSession* session, const char* line, size_t length)
{
	char command[32];
	char* end;
	long index;
	MenuData* data;
	if (!session || (!line && length) || length >= sizeof(command))
		return XTestMenuTestRemoteResult_Error;
	memcpy(command, line, length);
	command[length] = '\0';
	command[strcspn(command, "\r\n")] = '\0';
	if (strcmp(command, "q") == 0 || strcmp(command, "Q") == 0)
		return XTestMenuTestRemoteResult_Finished;
	if (command[0] == '\0')
	{
		XTestMenuTestRemoteSession_show(session);
		return XTestMenuTestRemoteResult_Active;
	}
	end = NULL;
	index = strtol(command, &end, 10);
	if (end == command || *end != '\0' || index < 0 ||
		index >= (long)XContainerSize(session->choices))
	{
		XPrintf("序号不合法请重新选择\n");
		return XTestMenuTestRemoteResult_Active;
	}
	data = XVector_at_base(session->choices, (int)index);
	if (!data || !data->action)
		return XTestMenuTestRemoteResult_Error;
	data->action(data);
	XTestMenuTestRemoteSession_show(session);
	return XTestMenuTestRemoteResult_Active;
}

void XTestMenuTestRemoteSession_destroy(XTestMenuTestRemoteSession* session)
{
	if (!session) return;
	if (session->choices) XVector_delete_base(session->choices);
	if (session->menu) XTestMenu_delete(session->menu);
	XFree_System(session);
}
#endif

int XTestMenuTest_run()
{
	XTestMenu* menu = XTestMenuTest_create();
	int code;
	if (!menu)
		return -1;
	code = XTestMenuTest_show(menu, 1);
	XTestMenu_delete(menu);
	return code;
}
