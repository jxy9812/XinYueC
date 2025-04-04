#include"XContainerObject.h"
#if XContainerObject_ON
void XContainerObject_init(XContainerObject* Object, size_t typeSize)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_class_init();
	ObjectVtable(Object)= XContainerObjectVtable;
	Object->m_data = NULL;
	Object->m_dataFreeMethod = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = typeSize;
}

void XContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_object.vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	ObjectVirtualFunc(Object, EXContainerObject_Free, funcPtr)(Object);
}

bool XContainerObject_empty(const XContainerObject* Object)
{
	if (ISNULL(Object, "")|| ISNULL(Object->m_object.vtable, ""))
		return true;
	typedef bool (*funcPtr)(const XContainerObject* );
	//void* p = ObjectVirtualFunc(Object, XContainerObject_Empty, funcPtr);
	return ObjectVirtualFunc(Object, EXContainerObject_Empty,funcPtr)(Object);
}

size_t XContainerObject_size(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_object.vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_Size, funcPtr)(Object);
}

size_t XContainerObject_capacity(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_object.vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_Capacity, funcPtr)(Object);
}
size_t XContainerObject_typeSize(const XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_object.vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_TypeSize, funcPtr)(Object);
}

void XContainerObject_swap(XContainerObject* ObjectOne,  XContainerObject* ObjectTwo)
{
	if (ISNULL(ObjectOne, "") || ISNULL(ObjectTwo, ""))
		return;
	typedef void(*funcPtr)(XContainerObject*, XContainerObject*);
	ObjectVirtualFunc(ObjectOne, EXContainerObject_Swap, funcPtr)(ObjectOne, ObjectTwo);
}

void XContainerObject_clear(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->m_object.vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	ObjectVirtualFunc(Object, EXContainerObject_Clear, funcPtr)(Object);
}


#endif


