#include"XStack.h"
//虚函数表定义
XVtable* XStackVtable = NULL;
#if VTABLEISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[25];//虚函数数据
#endif
void XStack_class_init()
{
	if (XStackVtable)
		return;
#if !VTABLEISSTACK
	XStackVtable = XVtable_new();
#else
	XStackVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XStackVtable, XVectorVtable);
	//追加函数
	//XVtable_append_array(XStackVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
#if SHOWCONTAINERSIZE
	printf("XStack size:%d\n", XVtable_size(XStackVtable));
#endif
}
