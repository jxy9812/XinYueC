#include"XStack.h"
//虚函数表定义
XVtable* XStackVtable = NULL;

void XStack_class_init()
{
	/*void* vtable[] = {
		VXStack_push,VXStack_pop,VXStack_top
	};*/
	XStackVtable = XVtable_new();
	//继承的函数
	XVtable_append_vtable(XStackVtable, XVectorVtable);
	//追加函数
	//XVtable_append_array(XStackVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
}
