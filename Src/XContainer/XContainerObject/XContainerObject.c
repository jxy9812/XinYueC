#include"XContainerObject.h"
#if XContainerObject_ON
void XContainerObject_init(XContainerObject* Object, size_t typeSize)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_class_init();
	XClassGetVtable(Object)= XContainerObjectVtable;
	Object->m_data = NULL;
	Object->m_dataFreeMethod = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = typeSize;
}

void XContainerObject_free_base(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_parent.m_vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	XClassGetVirtualFunc(Object, EXContainerObject_Free, funcPtr)(Object);
}

bool XContainerObject_isEmpty_base(const XContainerObject* Object)
{
	if (ISNULL(Object, "")|| ISNULL(Object->m_parent.m_vtable, ""))
		return true;
	typedef bool (*funcPtr)(const XContainerObject* );
	//void* p = XClassGetVirtualFunc(Object, XContainerObject_Empty, funcPtr);
	return XClassGetVirtualFunc(Object, EXContainerObject_IsEmpty,funcPtr)(Object);
}

size_t XContainerObject_getSize_base(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_parent.m_vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return XClassGetVirtualFunc(Object, EXContainerObject_Size, funcPtr)(Object);
}

size_t XContainerObject_getCapacity_base(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_parent.m_vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return XClassGetVirtualFunc(Object, EXContainerObject_Capacity, funcPtr)(Object);
}
size_t XContainerObject_getTypeSize_base(const XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_parent.m_vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return XClassGetVirtualFunc(Object, EXContainerObject_TypeSize, funcPtr)(Object);
}

void XContainerObject_swap_base(XContainerObject* ObjectOne,  XContainerObject* ObjectTwo)
{
	if (ISNULL(ObjectOne, "") || ISNULL(ObjectTwo, ""))
		return;
	typedef void(*funcPtr)(XContainerObject*, XContainerObject*);
	XClassGetVirtualFunc(ObjectOne, EXContainerObject_Swap, funcPtr)(ObjectOne, ObjectTwo);
}

void XContainerObject_clear_base(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_parent.m_vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	XClassGetVirtualFunc(Object, EXContainerObject_Clear, funcPtr)(Object);
}


#endif


