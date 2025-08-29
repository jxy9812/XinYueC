#include "XMenu.h"
#include "XMemory.h"
#include "XEquality.h"
#include "XVector.h"
#include <string.h>


typedef struct XMenuData
{
	char* title;//标题
	XVector* actions;//动作数组
}XMenuData;

static void XMenuData_init(XMenuData* data,const char* title);
static void XMenuData_setTitle(XMenuData* data, const char* title);
static void XMenuData_delete(XMenuData* data);
//数据删除方法
static void DataDeleteMethod(XMenuData* data, void* args);
XMenu* XMenu_create(const char* title)
{
	XHTreeNode* menu = XMemory_malloc(sizeof(XMenu));
	if (menu)
		XMenu_init(menu, title);
	return menu;
}

void XMenu_init(XMenu* menu, const char* title)
{
	if (menu == NULL)
		return;
	XMenuData data = { 0 };
	XMenuData_init(&data,title);
	XHTreeNode_init(menu, &data, sizeof(XMenuData));
}

void XMenu_setTitle(XMenu* menu, const char* title)
{
	if (menu == NULL)
		return;
	XMenuData* data=XTreeNode_getData(menu);
	XMenuData_setTitle(data,title);
}

const char* XMenu_getTitle(XMenu* menu)
{
	if (menu == NULL)
		return NULL;
	XMenuData* data = XTreeNode_getData(menu);
	return data->title;
}

XAction* XMenu_addAction(XMenu* menu, const char* text)
{
	if (menu == NULL)
		return NULL;
	XMenuData* data = XTreeNode_getData(menu);
	XAction* action=XAction_create(text);
	XVector_push_back_base(data->actions,&action);
	return action;
}

bool XMenu_removeAction(XMenu* menu, XAction* action)
{
	if (menu == NULL||action==NULL)
		return false;
	XMenuData* data = XTreeNode_getData(menu);
	int64_t index= XVector_indexOf(data->actions,&action,0);
	if (index == -1)
		return false;
	XVector_remove_base(data->actions, index,1);
	XAction_delete(action);
	return true;
}

const XVector* XMenu_getActions(XMenu* menu)
{
	if(menu==NULL)
		return NULL;
	XMenuData* data = XTreeNode_getData(menu);
	return data->actions;
}

bool XMenu_addMenu(XMenu* menu, XMenu* newMenu)
{
	if (menu == NULL||newMenu==NULL)
		return false;
	XMenuData* data = XTreeNode_getData(menu);
	if (XHTreeNode_addNode(menu, newMenu))
	{
		return true;
	}
	//XMenu_delete(newMenu);
	return false;
}

bool XMenu_removeMenu(XMenu* menu)
{
	if (menu == NULL)
		return false;
	XHTreeNode* parent = XHTreeNode_GetParent(menu);
	if(parent==NULL)
	{
		XMenu_delete(menu);
		return true;
	}
	return XHTreeNode_removeNode(menu, DataDeleteMethod, NULL);
}

XVector* XMenu_getMenus(XMenu* menu)
{
	if (menu == NULL)
		return NULL;
	XVector* v = XVector_Create(XMenu*);
	if (v == NULL)
		return NULL;
	XHTreeNode* child = XHTreeNode_GetFirstChild(menu);
	while (child)
	{
		XMenuData* data = XTreeNode_getData(child);
		XVector_push_back_base(v, &child);
		child = XHTreeNode_GetNextSibling(child);
	}
	return v;
}

void XMenu_delete(XMenu* menu)
{
	if (menu==NULL)
		return;
	XHTree_delete(menu, DataDeleteMethod,NULL);
}

void XMenuData_init(XMenuData* data, const char* title)
{
	if (data == NULL)
		return;
	char* str = NULL;
	if (title)
	{
		size_t len = strlen(title) + 1;
		if (len > 0)
		{
			str = XMemory_malloc(len);
			memcpy(str, title, len);
		}
	}
	data->title = str;
	data->actions = XVector_create(sizeof(XAction*));
	data->actions->m_equality = XEquality_ptr;
}

void XMenuData_setTitle(XMenuData* data, const char* title)
{
	if (data == NULL)
		return;
	if (data->title)
		XMemory_free(data->title);
	char* str = NULL;
	if (title)
	{
		size_t len = strlen(title) + 1;
		if (len > 0)
		{
			str = XMemory_malloc(len);
			memcpy(str, title, len);
		}
	}
	data->title = str;
}

void XMenuData_delete(XMenuData* data)
{
	if (data == NULL)
		return;
	if (data->title)
		XMemory_free(data->title);
	if(data->actions)
	{
		for_each_iterator(data->actions, XVector, it)
		{
			XAction* action = (*(XAction**)XVector_iterator_data(&it));
			XAction_delete(action);
		}
		XVector_delete_base(data->actions);
	}
	XMemory_free(data);
}

void DataDeleteMethod(XMenuData* data, void* args)
{
	XMenuData_delete(data);
}
