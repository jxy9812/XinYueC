#include"XStack.h"
//虚函数表定义
XVtable* XStackVtable = NULL;

void XStack_class_init()
{
	if (XStackVtable)
		return;
	XStackVtable = XVtable_new();
	//继承的函数
	XVtable_append_vtable(XStackVtable, XVectorVtable);
	//追加函数
	//XVtable_append_array(XStackVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
}
