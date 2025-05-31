#include"XQueue.h"
#if XQueue_ON
XVtable* XQueue_class_init()
{
	static XVtable* XClassVtable = NULL;
	if (XClassVtable)
		return XClassVtable;
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT(XClassVtable, XQUEUE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT(XClassVtable)
#endif
	//继承的函数
	XVtable_append_vtable(XClassVtable, XList_class_init());
	//追加函数
	//XVtable_append_array(XClassVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
#if SHOWCONTAINERSIZE
	printf("XQueue size:%d\n", XVtable_size(XClassVtable));
#endif
	return XClassVtable;
}
#endif