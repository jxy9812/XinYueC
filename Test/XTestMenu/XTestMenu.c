#include "XTestMenu.h"
#include "XMemory.h"
#include "XVector.h"
#include "XVariant.h"
#include <string.h>


typedef struct XTestMenuData
{
	char* title;//标题
	XVector* actions;//动作数组
}XTestMenuData;

static void XTestMenuData_init(XTestMenuData* data,const char* title);
static void XTestMenuData_setTitle(XTestMenuData* data, const char* title);
static void XTestMenuData_delete(XTestMenuData* data);
//数据删除方法
static void DataDeleteMethod(XTestMenuData* data, void* args);

/*
 * 菜单动作触发调度槽：XTestMenu_addAction 把每个动作的 triggered 信号连接
 * 到这里；动作被 trigger 时读取其绑定函数并调用。
 */
static void XTestMenu_actionDispatcher(XObject* sender, XVarList* args)
{
	XAction* action;
	XVariant* data;
	XTestMenuActionFunc func;

	(void)args;
	action = (XAction*)sender;
	if (action == NULL)
		return;
	data = XAction_data(action);
	func = NULL;
	if (data)
		func = (XTestMenuActionFunc)XVariant_Value(data, void*);
	if (func)
		func(NULL);
}
size_t XTestMenu_typeSize()
{
	return sizeof(XTestMenu) + sizeof(struct XTreeNode*) * 2;
}
XTestMenu* XTestMenu_create(const char* title)
{
	XHTreeNode* menu = XMalloc_System(XTestMenu_typeSize()+ sizeof(XTestMenuData));
	if (menu)
		XTestMenu_init(menu, XTestMenu_typeSize(),title);
	return menu;
}

void XTestMenu_init(XTestMenu* menu, size_t treeNodeSize, const char* title)
{
	if (menu == NULL)
		return;
	XTestMenuData data = { 0 };
	XTestMenuData_init(&data,title);
	XHTreeNode_init(menu, treeNodeSize, &data, sizeof(XTestMenuData));
	menu->m_userData = NULL;
}

void XTestMenu_setTitle(XTestMenu* menu, const char* title)
{
	if (menu == NULL)
		return;
	XTestMenuData* data=XTreeNode_getData(menu);
	XTestMenuData_setTitle(data,title);
}

const char* XTestMenu_getTitle(XTestMenu* menu)
{
	if (menu == NULL)
		return NULL;
	XTestMenuData* data = XTreeNode_getData(menu);
	return data->title;
}

XAction* XTestMenu_addAction(XTestMenu* menu, const char* text)
{
	if (menu == NULL)
		return NULL;
	XTestMenuData* data = XTreeNode_getData(menu);
	XAction* action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, text);
	if (action == NULL)
		return NULL;
	/* 连接 triggered 信号到内部调度，使菜单动作在 trigger 时运行
	 * XTestMenu_setActionFunction 绑定的函数。 */
	XObject_connect_2((XObject*)action, XSignal(XAction_triggered_signal),
		XTestMenu_actionDispatcher);
	XVector_push_back_1_base(data->actions,&action);
	return action;
}

void XTestMenu_setActionFunction(XAction* action, XTestMenuActionFunc func)
{
	XVariant* value;

	if (action == NULL)
		return;
	value = func ? XVariant_create_ptr((void*)func) : NULL;
	XAction_setData(action, value);
}

bool XTestMenu_removeAction(XTestMenu* menu, XAction* action)
{
	if (menu == NULL||action==NULL)
		return false;
	XTestMenuData* data = XTreeNode_getData(menu);
	int64_t index= XVector_indexOf(data->actions,&action,0);
	if (index == -1)
		return false;
	XVector_remove_base(data->actions, index,1);
	XAction_delete_base(action);
	return true;
}

const XVector* XTestMenu_getActions(XTestMenu* menu)
{
	if(menu==NULL)
		return NULL;
	XTestMenuData* data = XTreeNode_getData(menu);
	return data->actions;
}

bool XTestMenu_addMenu(XTestMenu* menu, XTestMenu* newMenu)
{
	if (menu == NULL||newMenu==NULL)
		return false;
	XTestMenuData* data = XTreeNode_getData(menu);
	if (XHTreeNode_addNode(menu, newMenu))
	{
		return true;
	}
	//XTestMenu_delete(newMenu);
	return false;
}

bool XTestMenu_removeMenu(XTestMenu* menu)
{
	if (menu == NULL)
		return false;
	XHTreeNode* parent = XHTreeNode_GetParent(menu);
	if(parent==NULL)
	{
		XTestMenu_delete(menu);
		return true;
	}
	return XHTreeNode_removeNode(menu, DataDeleteMethod, NULL,
		XMemory_method(XMEMORY_TYPE_SYSTEM));
}

XVector* XTestMenu_getMenus(XTestMenu* menu)
{
	if (menu == NULL)
		return NULL;
	XVector* v = XVector_Create(XTestMenu*);
	if (v == NULL)
		return NULL;
	XHTreeNode* child = XHTreeNode_GetFirstChild(menu);
	while (child)
	{
		XTestMenuData* data = XTreeNode_getData(child);
		XVector_push_back_1_base(v, &child);
		child = XHTreeNode_GetNextSibling(child);
	}
	return v;
}

void XTestMenu_delete(XTestMenu* menu)
{
	if (menu==NULL)
		return;
	XHTree_delete(menu, DataDeleteMethod,NULL,
		XMemory_method(XMEMORY_TYPE_SYSTEM));
}

void XTestMenuData_init(XTestMenuData* data, const char* title)
{
	if (data == NULL)
		return;
	char* str = NULL;
	if (title)
	{
		size_t len = strlen(title) + 1;
		if (len > 0)
		{
			str = XMalloc_System(len);
			memcpy(str, title, len);
		}
	}
	data->title = str;
	data->actions = XVector_create(sizeof(XAction*));
	XContainerSetCompare(data->actions, uintptr_t_compare);
}

void XTestMenuData_setTitle(XTestMenuData* data, const char* title)
{
	if (data == NULL)
		return;
	if (data->title)
		XFree_System(data->title);
	char* str = NULL;
	if (title)
	{
		size_t len = strlen(title) + 1;
		if (len > 0)
		{
			str = XMalloc_System(len);
			memcpy(str, title, len);
		}
	}
	data->title = str;
}

void XTestMenuData_delete(XTestMenuData* data)
{
	if (data == NULL)
		return;
	if (data->title)
		XFree_System(data->title);
	if(data->actions)
	{
		size_t i;
		for (i = 0; i < XVector_size_base(data->actions); ++i) {
			XAction* action = *(XAction**)XVector_at_base(data->actions, (int64_t)i);
			if (action) XAction_delete_base(action);
		}
		XVector_delete_base(data->actions);
	}
	/* XTestMenuData 存放在 XHTreeNode 的内嵌数据区，不是独立堆对象。 */
}

void DataDeleteMethod(XTestMenuData* data, void* args)
{
	XTestMenuData_delete(data);
}
