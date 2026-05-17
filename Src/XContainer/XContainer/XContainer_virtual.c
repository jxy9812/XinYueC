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
	XVTABLE_INHERIT_XCLASS(XClass);
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
    // 释放目标对象原有共享块
    if (object->m_data)
        XSharedData_release_with(object->m_data, NULL,NULL);
    
    // 拷贝所有字段（共享同一块 XSharedData）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XContainer) - sizeof(XClass));
    
    // 增加源数据块的引用计数
    if (object->m_data)
        XSharedData_addRef(object->m_data);
}

void VXClass_move(XContainer* object, XContainer* src)
{
    // 释放目标对象原有共享块
    if (object->m_data)
        XSharedData_release_with(object->m_data, NULL, NULL);
    
    // 转移所有权
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XContainer) - sizeof(XClass));
    
    // 清空源对象
    src->m_data = NULL;
    src->m_capacity = 0;
    src->m_size = 0;
}

void VXContainer_deinit(XContainer* object)
{
    if (ISNULL(object, ""))
        return;
    
    // 让子容器清理元素和数据（通过虚函数调用 clear）
    XContainer_clear_base(object);
    
    object->m_capacity = 0;
    object->m_size = 0;
    object->m_typeSize = 0;
    
        // 减少引用计数，最后一个引用时由 dataDeleter 释放 data
    if (object->m_data) {
        XSharedData_release_with(object->m_data, NULL, NULL);
        object->m_data = NULL;
    }
}



#endif