#include"XQueue.h"
#if XQueue_ON
//虚函数表定义
XVtable* XQueueVtable = NULL;
#if VTABLEISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[20];//虚函数数据
#endif
void XQueue_class_init()
{
	if (XQueueVtable)
		return;
#if !VTABLEISSTACK
	XQueueVtable = XVtable_new();
#else
	XQueueVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XQueueVtable, XListVtable);
	//追加函数
	//XVtable_append_array(XQueueVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
#if SHOWCONTAINERSIZE
	printf("XQueue size:%d\n", XVtable_size(XQueueVtable));
#endif
}
#endif