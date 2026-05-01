#include"XContainer.h"
#if XContainer_ON
#include"XAlgorithm.h"
#include"XVtable.h"
#include<stdlib.h>
#include<string.h>
//声明
static void VXClass_copy(XContainer* object, const XContainer* src);
static void VXClass_move(XContainer* object, XContainer* src);
static void VXContainer_deinit(XContainer* Object);
static bool VXContainer_isEmpty(const XContainer* Object);
static size_t VXContainer_size(const XContainer* Object);
static size_t VXContainer_capacity(const  XContainer* Object);
static size_t VXContainer_typeSize(const XContainer* Object);
static void VXContainer_swap(XContainer* ObjectOne, XContainer* ObjectTwo);
static void VXContainer_clear(XContainer* Object);
XVtable* XContainer_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XContainer))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {VXContainer_isEmpty,VXContainer_size,VXContainer_capacity,VXContainer_typeSize,VXContainer_swap,VXContainer_clear };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXContainer_deinit);
#if SHOWCONTAINERSIZE
	printf("XContainer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
bool VXContainer_isEmpty(const XContainer* Object)
{
	return XContainer_size_base(Object) == 0;
}


size_t VXContainer_size(const XContainer* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_size;
}

size_t VXContainer_capacity(const XContainer* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_capacity;
}
size_t VXContainer_typeSize(const XContainer* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->m_typeSize;
}

void VXContainer_swap(XContainer* ObjectOne, XContainer* ObjectTwo)
{
	bool one = ISNULL(ObjectOne, "");
	bool two = ISNULL(ObjectTwo, "");
	if (!(one || two))
	{
		XSwap((XClass*)ObjectOne+1, (XClass*)ObjectTwo+1,sizeof(XContainer)- sizeof(XClass));
	}
}

void VXContainer_clear(XContainer* Object)
{
	Object->m_size = 0;
}

void VXClass_copy(XContainer* object, const XContainer* src)
{
	if (XContainerDataPtr(object))
		XMemory_free(XContainerDataPtr(object));
	memcpy(object,src,sizeof(XContainer));
	XContainerDataPtr(object) = XMemory_malloc(XContainerSize(object)* XContainerTypeSize(object));
	memcpy(XContainerDataPtr(object), XContainerDataPtr(src), XContainerSize(object) * XContainerTypeSize(object));
	XContainerCapacity(object) = XContainerSize(object);
}

void VXClass_move(XContainer* object, XContainer* src)
{
	if (XContainerDataPtr(object))
		XMemory_free(XContainerDataPtr(object));
	memcpy(object, src, sizeof(XContainer));
	XContainerDataPtr(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src)=0;
}

void VXContainer_deinit(XContainer* Object)
{
	if (ISNULL(Object, ""))
		return ;
	//printf("准备释放\n");
	XContainer_clear_base(Object);
	//XClassGetVtable(Object) = NULL;
	Object->m_capacity = 0;
	Object->m_size = 0;
	Object->m_typeSize = 0;
	if (Object->m_data)
	{
		XMemory_free(Object->m_data);
		Object->m_data = NULL;
	}
	//XMemory_free(Object);
}



#endif