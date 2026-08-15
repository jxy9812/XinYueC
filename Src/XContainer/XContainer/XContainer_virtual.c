#include"XContainer.h"
#if XContainer_ON
#include"XAlgorithm.h"
#include"XVtable.h"
#include<stdlib.h>
#include<string.h>
//声明
static void VXClass_copy(XContainer* dst, const XContainer* src);
static void VXClass_move(XContainer* dst, XContainer* src);
static void VXContainer_deinit(XContainer* obj);
static bool VXContainer_isEmpty(const XContainer* Object);
static size_t VXContainer_size(const XContainer* Object);
static size_t VXContainer_capacity(const  XContainer* Object);
static size_t VXContainer_typeSize(const XContainer* Object);
static void VXContainer_swap(XContainer* a, XContainer* b);
static void VXContainer_clear(XContainer* Object);

static void VXContainer_destroy_raw_data(XContainer* object)
{
    if (!object || !object->m_data)
        return;
    if (object->m_dataDeinitMethod) {
        for (size_t i = 0; i < object->m_size; ++i)
            object->m_dataDeinitMethod((char*)object->m_data + i * object->m_typeSize);
    }
    XContainer_free(object, object->m_data);
    object->m_data = NULL;
    object->m_capacity = 0;
    object->m_size = 0;
}

static void VXContainer_destroy_shared_data(void* data, void* arg)
{
    XContainer* object = (XContainer*)arg;
    if (!object || !data || !object->m_dataDeinitMethod)
        return;
    for (size_t i = 0; i < object->m_size; ++i)
        object->m_dataDeinitMethod((char*)data + i * object->m_typeSize);
}
XVtable* XContainer_class_init()
{
	XVTABLE_INIT_DEFAULT(XContainer)
	//继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	void* table[] = {VXContainer_isEmpty,VXContainer_size,VXContainer_capacity,VXContainer_typeSize,VXContainer_swap,VXContainer_clear };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXContainer_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XContainer);
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

void VXContainer_swap(XContainer* a, XContainer* b)
{
    // 不同模式不能直接交换（因为 m_data 的类型不同，交换后会导致类型混乱）
    if (a->m_useCow != b->m_useCow) {
        // 这里可以打印错误或忽略，实际使用中应避免交换不同模式的容器
        return;
    }
    // 交换所有成员（跳过 XClass 部分，保留虚表指针）
    XSwap((XClass*)a + 1, (XClass*)b + 1, sizeof(XContainer) - sizeof(XClass));
}

void VXContainer_clear(XContainer* Object)
{
	Object->m_size = 0;
}

void VXClass_copy(XContainer* dst, const XContainer* src)
{
    if (!dst || !src) return;
    bool target_uninitialized = XClassIsVtableNull(dst);
    if (target_uninitialized) {
        XContainer_init(dst, XContainerTypeSize(src), XContainerIsCow(src));
        Class_Memory(dst) = Class_Memory(src);
    }
    else if (XContainerIsCow(src) &&
        XContainer_memory(dst) != XContainer_memory(src)) {
        /* COW 共享块不能跨内存池共享。 */
        return;
    }
    // 1. 释放目标原有资源
    if (XContainerIsCow(dst))
    {
        if (dst->m_data)
            XSharedData_release_with(dst->m_data, VXContainer_destroy_shared_data,
                dst, XContainer_memory(dst));
    }
    else {
        if (dst->m_data)
            VXContainer_destroy_raw_data(dst);
    }

    // 2. 拷贝公共字段（包括 m_useCow、大小、容量、回调等）
    memcpy((XClass*)dst + 1, (XClass*)src + 1, sizeof(XContainer) - sizeof(XClass));

    // 3. 根据源模式拷贝数据
    if (XContainerIsCow(dst)) {
        // COW 模式：共享 XSharedData，增加引用计数
        dst->m_data = src->m_data;
        if (dst->m_data)
            XSharedData_addRef(dst->m_data);
    }
    else {
        // 非 COW 模式：深拷贝原始数据
        if (src->m_data && src->m_size > 0) {
            size_t bytes = src->m_capacity * src->m_typeSize;
            dst->m_data = XContainer_malloc(dst, bytes);
            if (dst->m_data) {
                memcpy(dst->m_data, src->m_data, src->m_size * src->m_typeSize);
            }
        }
        else {
            dst->m_data = NULL;
        }
    }
}

void VXClass_move(XContainer* dst, XContainer* src)
{
    if (!dst || !src) return;
    XMemory* source_memory = Class_Memory(src);
    bool target_uninitialized = XClassIsVtableNull(dst);
    if (target_uninitialized) {
        XContainer_init(dst, XContainerTypeSize(src), XContainerIsCow(src));
    }
    else if (XContainerIsCow(dst)) {
        if (dst->m_data)
            XSharedData_release_with(dst->m_data, VXContainer_destroy_shared_data,
                dst, XContainer_memory(dst));
    }
    else {
        VXContainer_destroy_raw_data(dst);
    }

    // 先按目标原内存池释放成员，再接管源成员和源内存池。
    memcpy((XClass*)dst + 1, (XClass*)src + 1, sizeof(XContainer) - sizeof(XClass));
    Class_Memory(dst) = source_memory;

    // 3. 清空源对象（不再持有资源）
    src->m_data = NULL;
    src->m_capacity = 0;
    src->m_size = 0;
    // 注意：src 的 m_useCow 保留原值，但已无资源
}

void VXContainer_deinit(XContainer* obj)
{
    // 先让子类清理元素（通过虚函数调用 clear）
    XContainer_clear_base(obj);

    // 释放数据块
    if (obj->m_useCow) {
        if (obj->m_data)
            XSharedData_release_with(obj->m_data, NULL, NULL, XContainer_memory(obj));
    }
    else {
        if (obj->m_data)
            XContainer_free(obj, obj->m_data);
    }
    obj->m_data = NULL;
    obj->m_capacity = 0;
    obj->m_size = 0;
    obj->m_typeSize = 0;
}



#endif
