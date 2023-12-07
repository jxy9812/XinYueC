#include"XQueue.h"
//虚函数表定义
XVtable* XQueueVtable = NULL;

void XQueue_class_init()
{
	/*void* vtable[] = {
		VXQueue_push,VXQueue_pop
	};*/
	XQueueVtable = XVtable_new();
	//继承的函数
	XVtable_append_vtable(XQueueVtable, XListVtable);
	//追加函数
	//XVtable_append_array(XQueueVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
}
