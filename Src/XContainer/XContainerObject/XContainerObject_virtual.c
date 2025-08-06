#include"XContainerObject.h"
#if XContainerObject_ON
#include"XAlgorithm.h"
#include"XVtable.h"
#include<stdlib.h>
#include<string.h>
//声明
static void VXClass_copy(XContainerObject* object, const XContainerObject* src);
static void VXClass_move(XContainerObject* object, XContainerObject* src);
static void VXContainerObject_deinit(XContainerObject* Object);
static bool VXContainerObject_isEmpty(const XContainerObject* Object);
static size_t VXContainerObject_getSize(const XContainerObject* Object);
static size_t VXContainerObject_getCapacity(const  XContainerObject* Object);
static size_t VXContainerObject_getTypeSize(const XContainerObject* Object);
static void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
static void VXContainerObject_clear(XContainerObject* Object);
XVtable* XContainerObject_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XContainerObject))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {VXContainerObject_isEmpty,VXContainerObject_getSize,VXContainerObject_getCapacity,VXContainerObject_getTypeSize,VXContainerObject_swap,VXContainerObject_clear };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXContainerObject_deinit);
#if SHOWCONTAINERSIZE
	printf("XContainerObject size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
bool VXContainerObject_isEmpty(const XContainerObject* Object)
{
	return XContainerSize(Object) == 0;
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
		XSwap(ObjectOne, ObjectTwo,sizeof(XContainerObject));
		//XSwap(&ObjectOne->m_data, &ObjectTwo->m_data, sizeof(void*));
		//XSwap(&ObjectOne->m_capacity, &ObjectTwo->m_capacity, sizeof(size_t));
		//XSwap(&ObjectOne->m_size, &ObjectTwo->m_size, sizeof(size_t));
	}
}

void VXContainerObject_clear(XContainerObject* Object)
{
	Object->m_size = 0;
}

void VXClass_copy(XContainerObject* object, const XContainerObject* src)
{
	if (XContainerDataPtr(object))
		XMemory_free(XContainerDataPtr(object));
	memcpy(object,src,sizeof(XContainerObject));
	XContainerDataPtr(object) = XMemory_malloc(XContainerSize(object)* XContainerTypeSize(object));
	memcpy(XContainerDataPtr(object), XContainerDataPtr(src), XContainerSize(object) * XContainerTypeSize(object));
	XContainerCapacity(object) = XContainerSize(object);
}

void VXClass_move(XContainerObject* object, XContainerObject* src)
{
	if (XContainerDataPtr(object))
		XMemory_free(XContainerDataPtr(object));
	memcpy(object, src, sizeof(XContainerObject));
	XContainerDataPtr(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src)=0;
}

void VXContainerObject_deinit(XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	//printf("准备释放\n");
	XContainerObject_clear_base(Object);
	//XClassGetVtable(Object) = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = 0;
	if (Object->m_data);
	{
		XMemory_free(Object->m_data);
		Object->m_data = NULL;
	}
	//XMemory_free(Object);
}



#endif