#include"XContainerObject.h"
#if XContainerObject_ON
#include<string.h>
void XContainerObject_init(XContainerObject* Object, size_t typeSize)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	memset(((XClass*)Object)+1,0,sizeof(XContainerObject)-sizeof(XClass));
	XClass_init(Object);
	XClassGetVtable(Object) = XContainerObject_class_init();
	Object->m_typeSize = typeSize;
}
bool XContainerObject_isEmpty_base(const XContainerObject* Object)
{
	if (ISNULL(Object, "")|| ISNULL(XClassGetVtable(Object), ""))
		return true;
	typedef bool (*funcPtr)(const XContainerObject* );
	//void* p = XClassGetVirtualFunc(Object, XContainerObject_Empty, funcPtr);
	return XClassGetVirtualFunc(Object, EXContainerObject_IsEmpty,funcPtr)(Object);
}

size_t XContainerObject_size_base(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return XClassGetVirtualFunc(Object, EXContainerObject_Size, funcPtr)(Object);
}

size_t XContainerObject_capacity_base(const  XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return XClassGetVirtualFunc(Object, EXContainerObject_Capacity, funcPtr)(Object);
}
size_t XContainerObject_typeSize_base(const XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
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
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	XClassGetVirtualFunc(Object, EXContainerObject_Clear, funcPtr)(Object);
}


#endif


