#include"XStack.h"
#if XStack_ON
//虚函数表定义
XVtable* XStackVtable = NULL;
#if VTABLE_ISSTACK
	//static XVtable vtable;//虚函数类
	//static void* vtable_data[25];//虚函数数据
#endif
void XStack_class_init()
{
	if (XStackVtable)
		return;
	XStackVtable = XVectorVtable;
#if !VTABLE_ISSTACK
	XStackVtable = XVtable_new();
#else
	//XStackVtable = &vtable;
	//XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	//XVtable_append_vtable(XStackVtable, XVectorVtable);
#if SHOWCONTAINERSIZE
	printf("XStack size:%d\n", XVtable_size(XStackVtable));
#endif
}
#endif