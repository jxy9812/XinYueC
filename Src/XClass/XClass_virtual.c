#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
#include<string.h>
static void VXClass_copy(XClass* object, const XClass* src);
static void VXClass_move(XClass* object, XClass* src);
static void VXClass_deinit(XClass* object);
XVtable* XClass_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	void* table[] = { VXClass_copy,VXClass_move,VXClass_deinit };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

#if SHOWCONTAINERSIZE
	printf("XClass size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}
void XClass_init(XClass* object)
{
	XClassGetVtable(object) = XClass_class_init();
}

void VXClass_copy(XClass* object, const XClass* src)
{
	memcpy(object,src,sizeof(XClass));
}

void VXClass_move(XClass* object, XClass* src)
{
	memcpy(object, src, sizeof(XClass));
}

void VXClass_deinit(XClass* object)
{
}