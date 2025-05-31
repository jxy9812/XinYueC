#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
static void VXClass_free(XClass* Object);
XVtable* XClass_class_init()
{
	static XVtable* XClassVtable = NULL;
	if (XClassVtable)
		return XClassVtable;
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_DEFINITION(XCLASS_VTABLE_SIZE)
	XVTABLE_STACK_INIT(XClassVtable)
#else
	XVTABLE_HEAP_INIT(XClassVtable)
#endif
	void* table[] = { VXClass_free };
	XVtable_append_array(XClassVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XClassVtable;
}
void XClass_init(XClass* Object)
{
	XClassGetVtable(Object) = XClass_class_init();
}
void VXClass_free(XClass* Object)
{
	XMemory_free(Object);
}