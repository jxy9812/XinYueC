#include"XContainerObject.h"
#if XContainerObject_ON
#include"XAlgorithm.h"
#include"XVtable.h"
#include<stdlib.h>
//声明 
static void VXContainerObject_free(XContainerObject* Object);
static bool VXContainerObject_isEmpty(const XContainerObject* Object);
static size_t VXContainerObject_getSize(const XContainerObject* Object);
static size_t VXContainerObject_getCapacity(const  XContainerObject* Object);
static size_t VXContainerObject_getTypeSize(const XContainerObject* Object);
static void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
static void VXContainerObject_clear(XContainerObject* Object);
void XContainerDefaultDerivedClassDataFreeMethod(void* args)
{
	XContainerObject* object = *((XContainerObject**)args);
	XContainerObject_free_base(object);
}
XVtable* XContainerObject_class_init()
{
	static XVtable* XClassVtable = NULL;
	if (XClassVtable)
		return XClassVtable;
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT(XClassVtable, XCONTAINEROBJECT_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT(XClassVtable)
#endif
	//继承的函数
	XVtable_append_vtable(XClassVtable, XClass_class_init());
	void* table[] = {VXContainerObject_isEmpty,VXContainerObject_getSize,VXContainerObject_getCapacity,VXContainerObject_getTypeSize,VXContainerObject_swap,VXContainerObject_clear };
	XVtable_append_array(XClassVtable,table,sizeof(table)/sizeof(table[0]));
	//重写的函数
	XVtable_At(XClassVtable, EXClass_Free) = VXContainerObject_free;
#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XClassVtable));
#endif
	return XClassVtable;
}
bool VXContainerObject_isEmpty(const XContainerObject* Object)
{
	return VXContainerObject_getSize(Object) == 0;
}


size_t VXContainerObject_getSize(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_size;
}

size_t VXContainerObject_getCapacity(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_capacity;
}
size_t VXContainerObject_getTypeSize(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_typeSize;
}

void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo)
{
	bool one = ISNULL(ObjectOne, "");
	bool two = ISNULL(ObjectTwo, "");
	if (!(one || two))
	{
		XSwap(&ObjectOne->m_data, &ObjectTwo->m_data, sizeof(void*));
		XSwap(&ObjectOne->m_capacity, &ObjectTwo->m_capacity, sizeof(size_t));
		XSwap(&ObjectOne->m_size, &ObjectTwo->m_size, sizeof(size_t));
	}
}

void VXContainerObject_clear(XContainerObject* Object)
{
	Object->m_size = 0;
}

void VXContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	//printf("准备释放\n");
	XContainerObject_clear_base(Object);
	XClassGetVtable(Object) = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = 0;
	if (Object->m_data);
		XMemory_free(Object->m_data);
	XMemory_free(Object);
}



#endif