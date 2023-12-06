#include"XStack.h"
//虚函数表定义
XVtable* XStackVtable = NULL;
// 压栈，增加元素 O(1)
static void VXStack_push(XStack* this_stack, void* LpValue);
//移除栈顶元素 O(1)
static void VXStack_pop(XStack* this_stack);
// 取得栈顶元素（但不删除）O(1)
static void* VXStack_top(XStack* this_stack);
void XStack_class_init()
{
	void* vtable[] = {
		VXStack_push,VXStack_pop,VXStack_top
	};
	XStackVtable = XVtable_new();
	//继承的函数
	XVtable_append_vtable(XStackVtable, XVectorVtable);
	//追加函数
	XVtable_append_array(XStackVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
}

void VXStack_push(XStack* this_stack, void* LpValue)
{
	XVector_push_back(this_stack, LpValue);
}

void VXStack_pop(XStack* this_stack)
{
	XVector_pop_back(this_stack);
}

void* VXStack_top(XStack* this_stack)
{
	return XVector_back(this_stack);
}
