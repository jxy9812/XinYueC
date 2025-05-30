#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
static void VXClass_free(XClass* Object);

XVtable* XClassVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XCLASS_VTABLE_SIZE];//虚函数数据
#endif
static void XClass_class_init()
{
	if (XClassVtable)
		return;
	//虚函数表初始化
#if !VTABLE_ISSTACK
	XClassVtable = XVtable_new();
#else
	XClassVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, XCLASS_VTABLE_SIZE);
#endif
	void* table[] = { VXClass_free };
	XVtable_append_array(XClassVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XClassVtable));
#endif
}
void XClass_init(XClass* Object)
{
	XClass_class_init();
	XClassGetVtable(Object) = XClassVtable;
}
void VXClass_free(XClass* Object)
{
	XMemory_free(Object);
}