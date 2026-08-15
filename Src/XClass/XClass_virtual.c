#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
#include<string.h>
static void VXClass_copy(XClass* object, const XClass* src);
static void VXClass_move(XClass* object, XClass* src);
static void VXClass_deinit(XClass* object);
XVtable* XClass_class_init()
{
	XVTABLE_INIT_DEFAULT(XClass)
	void* table[] = { VXClass_copy,VXClass_move,VXClass_deinit };
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	XCLASS_SHOW_SIZE_DEFAULT(XClass);
	return XVTABLE_DEFAULT;
}
void XClass_init(XClass* object)
{
	XClassGetVtable(object) = XClass_class_init();
	Set_Class_Memory(object, XCLASS_DEFAULT_MEMORY_TYPE);
	Set_Class_IsHeap(object, false);
	object->m_reserved = 0;
}

void VXClass_copy(XClass* object, const XClass* src)
{
	//memcpy(object,src,sizeof(XClass));
}

void VXClass_move(XClass* object, XClass* src)
{
	//memcpy(object, src, sizeof(XClass));
}

void VXClass_deinit(XClass* object)
{
}
