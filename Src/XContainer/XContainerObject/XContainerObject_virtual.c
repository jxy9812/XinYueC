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
XVtable* XContainerObjectVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XCONTAINEROBJECT_VTABLE_SIZE];//虚函数数据
#endif
void XContainerDefaultDerivedClassDataFreeMethod(void* args)
{
	XContainerObject* object = *((XContainerObject**)args);
	XContainerObject_free_base(object);
}
static void XContainerObject_class_init()
{
	if (XContainerObjectVtable)
		return;
	//虚函数表初始化
 #if !VTABLE_ISSTACK
	XContainerObjectVtable = XVtable_new();
#else
	XContainerObjectVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, XCONTAINEROBJECT_VTABLE_SIZE);
#endif
	//继承的函数
	XVtable_append_vtable(XContainerObjectVtable, XClassVtable);
	void* table[] = {VXContainerObject_isEmpty,VXContainerObject_getSize,VXContainerObject_getCapacity,VXContainerObject_getTypeSize,VXContainerObject_swap,VXContainerObject_clear };
	XVtable_append_array(XContainerObjectVtable,table,sizeof(table)/sizeof(table[0]));
	//重写的函数
	XVtable_At(XContainerObjectVtable, EXClass_Free) = VXContainerObject_free;
#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XContainerObjectVtable));
#endif
}
void XContainerObject_init(XContainerObject* Object, size_t typeSize)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	XClass_init(Object);
	XContainerObject_class_init();
	XClassGetVtable(Object) = XContainerObjectVtable;
	Object->m_data = NULL;
	Object->m_dataFreeMethod = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = typeSize;
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