#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
static void VXClass_free(XClass* Object);
XVtable* XClass_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	void* table[] = { VXClass_free };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}
void XClass_init(XClass* Object)
{
	XClassGetVtable(Object) = XClass_class_init();
}
void VXClass_free(XClass* Object)
{
	XMemory_free(Object);
}