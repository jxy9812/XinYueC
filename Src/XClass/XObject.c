#include "XObject.h"
#include "XMemory.h"
XVtable* XObject_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	/*	void* table[] = { VXClass_free };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);*/

#if SHOWCONTAINERSIZE
	printf("XObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XObject* XObject_create()
{
	XObject* object = XMemory_malloc(sizeof(XObject));
	XObject_init(object);
	return object;
}

void XObject_init(XObject* object)
{
	if (object == NULL)
		return;
}
