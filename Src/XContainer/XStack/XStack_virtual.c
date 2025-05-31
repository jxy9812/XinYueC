#include"XStack.h"
#if XStack_ON
#if VTABLE_ISSTACK
	//static XVtable vtable;//虚函数类
	//static void* vtable_data[25];//虚函数数据
#endif
XVtable* XStack_class_init()
{
	XVtable* XStackVtable = NULL;
	if (XStackVtable)
		return XStackVtable;
	XStackVtable = XVector_class_init();
	//继承的函数
	//XVtable_append_vtable(XStackVtable, XVectorVtable);
#if SHOWCONTAINERSIZE
	printf("XStack size:%d\n", XVtable_size(XStackVtable));
#endif
	return XStackVtable;
}
#endif