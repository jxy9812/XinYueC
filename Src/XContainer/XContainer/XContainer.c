#include"XContainer.h"
#if XContainer_ON
#include<string.h>
void XContainer_init(XContainer* Object, size_t typeSize, bool cow)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	memset(((XClass*)Object)+1,0,sizeof(XContainer)-sizeof(XClass));
	XClass_init(Object);
	XClassSetVtable(Object, XContainer);
	Object->m_typeSize = typeSize;
	Object->m_useCow = cow;
}
size_t XContainer_capacity_base(const  XContainer* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainer*);
	return XClassGetVirtualFunc(Object, EXContainer_Capacity, funcPtr)(Object);
}
size_t XContainer_typeSize_base(const XContainer* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainer*);
	return XClassGetVirtualFunc(Object, EXContainer_TypeSize, funcPtr)(Object);
}

void XContainer_swap_base(XContainer* ObjectOne,  XContainer* ObjectTwo)
{
	if (ISNULL(ObjectOne, "") || ISNULL(ObjectTwo, ""))
		return;
	typedef void(*funcPtr)(XContainer*, XContainer*);
	XClassGetVirtualFunc(ObjectOne, EXContainer_Swap, funcPtr)(ObjectOne, ObjectTwo);
}

void XContainer_clear_base(XContainer* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return ;
	typedef void(*funcPtr)(XContainer*);
	XClassGetVirtualFunc(Object, EXContainer_Clear, funcPtr)(Object);
}


#endif


