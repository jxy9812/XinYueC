#include "XVector.h"
#if XVector_ON
#include "XSort.h"
#include "XVtable.h"
#include <string.h>
#include <stdlib.h>

#define VECTORNUM 20  // 初始数组大小
static inline void* XVector_data(XVector* vec);
// 前向声明
static bool VXVectorDetachIfNeeded(XVector * this_vector);
static void VXVectorDataDelete(void* data, XVector* this_vector);
static bool VXVectorEnlargeCapacity(XVector* this_vector);

// 虚函数实现声明
static void VXClass_copy(XVector* object, const XVector* src);
static void VXClass_move(XVector* object, XVector* src);
static bool VXVector_resize(XVector* this_vector, size_t size);
static bool VXVector_push_front(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod);
static bool VXVector_push_back(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod);
static bool VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod);
static bool VXVector_append_array(XVector* this_vector, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod);
static void VXVector_pop_front(XVector* this_vector);
static void VXVector_pop_back(XVector* this_vector);
static void VXVector_erase(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next);
static void VXVector_remove(XVector* this_vector, int64_t index, int64_t n);
static void VXVector_clear(XVector* this_vector);
static void VXVector_rcopy(XVector* this_One, const XVector* this_Two);
static void* VXVector_at(const XVector* this_vector, int64_t index);
static void* VXVector_front(const XVector* this_vector);
static void* VXVector_back(const XVector* this_vector);
static bool VXVector_find(const XVector* this_vector, const void* findVal, XVector_iterator* it);
static void VXVector_sort(XVector* this_vector, XSortOrder order);

// ========================
// 虚函数表初始化
// ========================
XVtable* XVector_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XVector))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXVector_resize,
        VXVector_push_front, VXVector_push_back,
        VXVector_insert_array, VXVector_append_array,
        VXVector_pop_front, VXVector_pop_back, VXVector_erase, VXVector_remove,
        VXVector_rcopy,
        VXVector_at, VXVector_front, VXVector_back, VXVector_find,
        VXVector_sort
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXVector_clear);
    //XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    //XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
#if SHOWCONTAINERSIZE
    printf("XVector size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// ========================
// 初始化
// ========================
void XVector_init(XVector* this_vector, size_t typeSize, bool useCow)
{
    if (ISNULL(this_vector, "") || ISNULL(typeSize, ""))
        return;
    XContainer_init(this_vector, typeSize, useCow);
    XClassSetVtable(this_vector, XVector);
}

// ========================
// 容量管理（内部使用）
// ========================
static bool VXVectorEnlargeCapacity(XVector* this_vector)
{
    // 首次分配
    if (XContainerCapacity(this_vector) == 0)
    {
        size_t bytes = XContainerTypeSize(this_vector) * VECTORNUM;
        if (XContainerIsCow(this_vector)) {
            XSharedData* shared = XSharedData_create(NULL, bytes);
            if (!shared) return false;
            XContainerSharedData(this_vector) = shared;
        }
        else {
            void* raw = XMalloc_System(bytes);
            if (!raw) return false;
            XContainerDataPtr(this_vector) = raw;
        }
        XContainerCapacity(this_vector) = VECTORNUM;
        return true;
    }
    // 扩容
    else if (XContainerCapacity(this_vector) == XContainerSize(this_vector))
    {
        size_t newCapacity;
        size_t oldCap = XContainerCapacity(this_vector);
        if (oldCap > 100)
            newCapacity = oldCap * 1.5;
        else
            newCapacity = oldCap * 2;

        size_t typeSize = XContainerTypeSize(this_vector);
        size_t bytes = newCapacity * typeSize;
        size_t oldSize = XContainerSize(this_vector);

        if (XContainerIsCow(this_vector)) {
            XSharedData* newShared = XSharedData_create(NULL, bytes);
            if (!newShared) return false;
            void* oldData = XContainerSharedDataPtr(this_vector);
            XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
            if (copy) {
                for (size_t i = 0; i < oldSize; i++) {
                    copy((char*)newShared->data + i * typeSize,
                        (char*)oldData + i * typeSize);
                }
            }
            else {
                memcpy(newShared->data, oldData, oldSize * typeSize);
            }
            XSharedData_release(XContainerSharedData(this_vector));
            XContainerSharedData(this_vector) = newShared;
        }
        else {
            void* newRaw = XRealloc_System(XContainerDataPtr(this_vector), bytes);
            if (!newRaw) return false;
            XContainerDataPtr(this_vector) = newRaw;
        }
        XContainerCapacity(this_vector) = newCapacity;
    }
    return true;
}

inline void* XVector_data(XVector* vec)
{
    return XContainerDataAddr(vec);
}

// ========================
// COW 分离核心函数
// ========================
static bool VXVectorDetachIfNeeded(XVector* this_vector)
{
    // 非 COW 模式永远不需要分离
    if (!XContainerIsCow(this_vector)) return true;

    XSharedData* sd = XContainerSharedData(this_vector);
    if (!sd || !XSharedData_isShared(sd))
        return true;

    size_t size = XContainerSize(this_vector);
    size_t capacity = XContainerCapacity(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);
    if (capacity == 0 || typeSize == 0) return true;

    size_t bytes = capacity * typeSize;
    XSharedData* newShared = XSharedData_create(NULL, bytes);
    if (!newShared) return false;

    void* oldData = sd->data;
    XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
    if (copy) {
        for (size_t i = 0; i < size; i++) {
            copy((char*)newShared->data + i * typeSize,
                (char*)oldData + i * typeSize);
        }
    }
    else {
        if (oldData && size > 0)
            memcpy(newShared->data, oldData, size * typeSize);
    }

    XSharedData_release(sd);
    XContainerSharedData(this_vector) = newShared;
    return true;
}

// ========================
// 数据删除回调（用于释放共享块时清理元素）
// ========================
static void VXVectorDataDelete(void* data, XVector* this_vector)
{
    if (XContainerDataDeinitMethod(this_vector) != NULL)
    {
        size_t size = XContainerSize(this_vector);
        size_t typeSize = XContainerTypeSize(this_vector);
        for (size_t i = 0; i < size; i++)
        {
            XContainerDataDeinitMethod(this_vector)(
                ((uint8_t*)data) + i * typeSize);
        }
    }
    // 注意：不在这里释放 data，XSharedData_release_with 会释放 XSharedData 整体内存
}

// ========================
// 拷贝 / 移动
// ========================
void VXClass_copy(XVector* object, const XVector* src)
{
    //// 如果目标还未初始化，先初始化
    //if (((XClass*)object)->m_vtable == NULL)
    //{
    //    XVector_init(object, XContainerTypeSize(src));
    //}
    //else if (XContainerSharedData(object))
    //{
    //    XSharedData_release_with(XContainerSharedData(object), (XCDataDeinitMethod)VXVectorDataDelete, object);
    //}

    //// 复制回调函数
    //XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    //XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    //XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    //// 共享源数据的 XSharedData（COW 机制）
    //XContainerSharedData(object) = XContainerSharedData(src);
    //if (XContainerSharedData(object))
    //{
    //    XSharedData_addRef(XContainerSharedData(object));
    //}

    //XContainerSize(object) = XContainerSize(src);
    //XContainerCapacity(object) = XContainerCapacity(src);
    //XContainerTypeSize(object) = XContainerTypeSize(src);
}

void VXClass_move(XVector* object, XVector* src)
{
    //if (((XClass*)object)->m_vtable == NULL)
    //{
    //    XVector_init(object, XContainerTypeSize(src));
    //}
    //else if (XContainerSharedData(object))
    //{
    //    XSharedData_release_with(XContainerSharedData(object), (XCDataDeinitMethod)VXVectorDataDelete, object);
    //}

    //// 转移所有权（浅拷贝指针）
    //memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XVector) - sizeof(XClass));

    //// 清空源对象
    //XContainerSharedData(src) = NULL;
    //XContainerCapacity(src) = 0;
    //XContainerSize(src) = 0;
}

// ========================
// 容器大小调整
// ========================
bool VXVector_resize(XVector* this_vector, size_t size)
{
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;

    size_t oldSize = XContainerSize(this_vector);
    size_t capacity = XContainerCapacity(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);

    if (size <= oldSize) {
        if (XContainerDataDeinitMethod(this_vector)) {
            for (size_t i = size; i < oldSize; i++) {
                XContainerDataDeinitMethod(this_vector)(
                    (char*)XVector_data(this_vector) + i * typeSize);
            }
        }
        XContainerSize(this_vector) = size;
        return true;
    }

    if (size > capacity) {
        size_t newCapacity = size;
        size_t bytes = newCapacity * typeSize;
        if (XContainerIsCow(this_vector)) {
            XSharedData* newShared = XSharedData_create(NULL, bytes);
            if (!newShared) return false;
            void* oldData = XVector_data(this_vector);
            XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
            if (copy) {
                for (size_t i = 0; i < oldSize; i++) {
                    copy((char*)newShared->data + i * typeSize,
                        (char*)oldData + i * typeSize);
                }
            }
            else {
                if (oldData && oldSize > 0)
                    memcpy(newShared->data, oldData, oldSize * typeSize);
            }
            XSharedData_release(XContainerSharedData(this_vector));
            XContainerSharedData(this_vector) = newShared;
        }
        else {
            void* newRaw = XRealloc_System(XContainerDataPtr(this_vector), bytes);
            if (!newRaw) return false;
            XContainerDataPtr(this_vector) = newRaw;
        }
        XContainerCapacity(this_vector) = newCapacity;
    }

    char* start = (char*)XVector_data(this_vector) + oldSize * typeSize;
    memset(start, 0, (size - oldSize) * typeSize);
    XContainerSize(this_vector) = size;
    return true;
}

// ========================
// 插入操作（均已添加分离检查）
// ========================
bool VXVector_push_front(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;
    if (XContainer_isEmpty_base(this_vector))
        return XClassGetVirtualFunc(this_vector, EXVector_Push_Back,
            bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, dataCreatMethod);
    else
        return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array,
            bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, 0, pvValue, 1, dataCreatMethod);
}

bool VXVector_push_back(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;
    if (!VXVectorEnlargeCapacity(this_vector))
        return false;

    char* ptr = (char*)XVector_data(this_vector)
        + XContainerTypeSize(this_vector) * XContainerSize(this_vector);
    if (dataCreatMethod)
    {
        memset(ptr, 0, XContainerTypeSize(this_vector));
        dataCreatMethod(ptr, pvValue);
    }
    else
    {
        memcpy(ptr, pvValue, XContainerTypeSize(this_vector));
    }
    XContainerSize(this_vector)++;
    return true;
}

bool VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod)
{
    if (!this_vector || !begin || n == 0)
        return false;
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;

    size_t current_size = XContainerSize(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);
    if (index < 0 || index >(int64_t)current_size)
        return false;

    // 尾部追加优化
    if (current_size == 0 || index == (int64_t)current_size)
        return XClassGetVirtualFunc(this_vector, EXVector_append_Array,
            bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, dataCreatMethod);

    void* ptr = VXVector_at(this_vector, index);
    if (ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
    {
        int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
        void* temp = XMalloc_System(size);
        memcpy(temp, ptr, size);
        int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;

        for (size_t i = 0; i < n; i++)
        {
            if (!VXVectorEnlargeCapacity(this_vector))
            {
                memcpy(VXVector_at(this_vector, sizen), temp, size);
                XFree_System(temp);
                return false;
            }
            char* dest = (char*)XVector_data(this_vector) + typeSize * sizen;
            if (dataCreatMethod)
            {
                memset(dest, 0, typeSize);
                dataCreatMethod(dest, (char*)begin + i * typeSize);
            }
            else
            {
                memcpy(dest, (char*)begin + i * typeSize, typeSize);
            }
            sizen++;
            XContainerSize(this_vector)++;
        }
        memcpy(VXVector_at(this_vector, sizen), temp, size);
        XFree_System(temp);
    }
    return true;
}

bool VXVector_append_array(XVector* this_vector, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod)
{
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;

    size_t oldSize = XContainerSize(this_vector);
    if (oldSize + n > XContainerCapacity(this_vector))
    {
        if (!VXVector_resize(this_vector, oldSize + n))
            return false;
    }
    else
    {
        XContainerSize(this_vector) += n;
    }

    size_t typeSize = XContainerTypeSize(this_vector);
    char* dest = (char*)XVector_data(this_vector) + oldSize * typeSize;
    if (dataCreatMethod)
    {
        memset(dest, 0, n * typeSize);
        for (size_t i = 0; i < n; i++)
        {
            dataCreatMethod(dest + i * typeSize, (char*)begin + i * typeSize);
        }
    }
    else
    {
        memcpy(dest, begin, n * typeSize);
    }
    return true;
}

// ========================
// 删除操作（已添加分离检查）
// ========================
void VXVector_pop_front(XVector* this_vector)
{
    VXVector_remove(this_vector, 0, 1);
}

void VXVector_pop_back(XVector* this_vector)
{
    if (XContainer_isEmpty_base(this_vector))
        return;
    if (!VXVectorDetachIfNeeded(this_vector))
        return;
    if (XContainerDataDeinitMethod(this_vector) != NULL)
        XContainerDataDeinitMethod(this_vector)(XVector_back_base(this_vector));
    XContainerSize(this_vector)--;
}

void VXVector_erase(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next)
{
    if (XVector_isEmpty_base(this_vector) || it->data == NULL)
    {
        if (next)
            *next = XVector_end(this_vector);
        return;
    }
    if (!VXVectorDetachIfNeeded(this_vector))
    {
        if (next)
            *next = XVector_end(this_vector);
        return;
    }

    void* pvValue = XVector_iterator_data(it);
    void* front = XVector_front_base(this_vector);
    void* back = XVector_back_base(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);
    if (front <= pvValue && pvValue <= back && ((char*)pvValue - (char*)front) % typeSize == 0)
    {
        if (XContainerSize(this_vector) == 1)
        {
            XContainerSize(this_vector) = 0;
            if (next)
                *next = XVector_end(this_vector);
        }
        else
        {
            if (XContainerDataDeinitMethod(this_vector) != NULL)
                XContainerDataDeinitMethod(this_vector)(pvValue);
            XContainerSize(this_vector)--;
            if (pvValue == back && next != NULL)
                *next = XVector_end(this_vector);
            memcpy(pvValue, (char*)pvValue + typeSize,
                (size_t)((char*)back - (char*)pvValue));
            if (next)
                *next = *it;
        }
    }
    if (next)
        *next = XVector_end(this_vector);
}

void VXVector_remove(XVector* this_vector, int64_t index, int64_t n)
{
    if (XContainer_isEmpty_base(this_vector))
        return;
    if (!VXVectorDetachIfNeeded(this_vector))
        return;

    size_t size = XContainerSize(this_vector);
    if (index < 0 || index >= size)
        return;

    size_t typeSize = XContainerTypeSize(this_vector);
    char* data = XVector_data(this_vector);

    if (index + n > size || n < 0)
        n = size - index;

    if (XContainerDataDeinitMethod(this_vector))
    {
        for (int64_t i = 0; i < n; i++)
        {
            XContainerDataDeinitMethod(this_vector)(data + (i + index) * typeSize);
        }
    }

    for (size_t i = 0; i < size - index - n; i++)
    {
        memcpy(data + (i + index) * typeSize,
            data + (i + index + n) * typeSize,
            typeSize);
    }
    XContainerSize(this_vector) -= n;
}

void VXVector_clear(XVector* this_vector)
{
    if (XContainer_isEmpty_base(this_vector))
        return;

    // COW 模式且共享：直接丢弃共享块
    if (XContainerIsCow(this_vector)) {
        XSharedData* sd = XContainerSharedData(this_vector);
        if (sd && XSharedData_isShared(sd)) {
            XSharedData_release(sd);
            XContainerSharedData(this_vector) = NULL;
            XContainerCapacity(this_vector) = 0;
            XContainerSize(this_vector) = 0;
            return;
        }
    }

    // 其他情况：逐个析构元素
    if (XContainerDataDeinitMethod(this_vector)) {
        char* data = XVector_data(this_vector);
        size_t size = XContainerSize(this_vector);
        size_t typeSize = XContainerTypeSize(this_vector);
        for (size_t i = 0; i < size; i++) {
            XContainerDataDeinitMethod(this_vector)(data + i * typeSize);
        }
    }
    XContainerSize(this_vector) = 0;
}

// ========================
// 其他操作（逆序拷贝等）
// ========================
void VXVector_rcopy(XVector* this_One, const XVector* this_Two)
{
    if (((XClass*)this_One)->m_vtable == NULL) {
        // 目标未初始化，使用源的模式初始化
        bool useCow = XContainerIsCow((XVector*)this_Two);
        XVector_init(this_One, XContainerTypeSize(this_Two), useCow);
    }
    else {
        // 释放目标原有数据
        if (XContainerIsCow(this_One)) {
            XSharedData* sd = XContainerSharedData(this_One);
            if (sd) XSharedData_release_with(sd, (XCDataDeinitMethod)VXVectorDataDelete, this_One);
        }
        else {
            if (XContainerDataPtr(this_One))
                XFree_System(XContainerDataPtr(this_One));
        }
    }

    XContainerSetDataCopyMethod(this_One, XContainerDataCopyMethod(this_Two));
    XContainerSetDataMoveMethod(this_One, XContainerDataMoveMethod(this_Two));
    XContainerSetDataDeinitMethod(this_One, XContainerDataDeinitMethod(this_Two));

    size_t size = XContainerSize(this_Two);
    size_t typeSize = XContainerTypeSize(this_Two);
    if (size > 0) {
        if (XContainerIsCow(this_One)) {
            XSharedData* newShared = XSharedData_create(NULL, size * typeSize);
            if (!newShared) return;
            const char* src = (const char*)XVector_data(this_Two);
            char* dst = newShared->data;
            XCDataCopyMethod copy = XContainerDataCopyMethod(this_One);
            for (size_t i = 0; i < size; i++) {
                const char* srcElem = src + (size - 1 - i) * typeSize;
                if (copy)
                    copy(dst + i * typeSize, srcElem);
                else
                    memcpy(dst + i * typeSize, srcElem, typeSize);
            }
            XContainerSharedData(this_One) = newShared;
        }
        else {
            void* newRaw = XMalloc_System(size * typeSize);
            if (!newRaw) return;
            const char* src = (const char*)XVector_data(this_Two);
            char* dst = (char*)newRaw;
            XCDataCopyMethod copy = XContainerDataCopyMethod(this_One);
            for (size_t i = 0; i < size; i++) {
                const char* srcElem = src + (size - 1 - i) * typeSize;
                if (copy)
                    copy(dst + i * typeSize, srcElem);
                else
                    memcpy(dst + i * typeSize, srcElem, typeSize);
            }
            XContainerDataPtr(this_One) = newRaw;
        }
        XContainerCapacity(this_One) = size;
        XContainerSize(this_One) = size;
        XContainerTypeSize(this_One) = typeSize;
    }
}

// ========================
// 元素访问（只读，无需分离）
// ========================
void* VXVector_at(const XVector* this_vector, int64_t index)
{
    if (index < 0 || index + 1 > XContainerSize(this_vector))
        return NULL;
    return (void*)((char*)XVector_data(this_vector) +
        XContainerTypeSize(this_vector) * index);
}

void* VXVector_front(const XVector* this_vector)
{
    if (XContainer_isEmpty_base(this_vector))
        return NULL;
    return XVector_data(this_vector);
}

void* VXVector_back(const XVector* this_vector)
{
    if (XContainer_isEmpty_base(this_vector))
        return NULL;
    return VXVector_at(this_vector, XContainerSize(this_vector) - 1);
}

// ========================
// 查找（只读，无需分离）
// ========================
bool VXVector_find(const XVector* this_vector, const void* findVal, XVector_iterator* it)
{
    if (ISNULL(this_vector, "") || XVector_isEmpty_base(this_vector))
    {
        if (it)
            *it = XVector_end(this_vector);
        return false;
    }
    size_t size = XContainerSize(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);
    char* data = XVector_data(this_vector);
    for (size_t i = 0; i < size; i++)
    {
        void* elem = data + i * typeSize;
        if (XContainerCompare(this_vector))
        {
            if (XContainerCompare(this_vector)(elem, findVal) == XCompare_Equality)
            {
                if (it)
                    it->data = elem;
                return true;
            }
        }
        else if (memcmp(elem, findVal, typeSize) == 0)
        {
            if (it)
                it->data = elem;
            return true;
        }
    }
    if (it)
        *it = XVector_end(this_vector);
    return false;
}

// ========================
// 排序（修改操作，需要分离）
// ========================
void VXVector_sort(XVector* this_vector, XSortOrder order)
{
    if (XContainerSize(this_vector) <= 1)
        return;
    if (!VXVectorDetachIfNeeded(this_vector))
        return;
    XQuicPitSort_Stack(XVector_data(this_vector),
        XContainerSize(this_vector),
        XContainerTypeSize(this_vector),
        XContainerCompare(this_vector),
        order);
}

// ========================
// 公开的 API 函数（创建、包装等）
// ========================
XVector* XVector_create_ex(size_t typeSize, bool useCow)
{
    if (ISNULL(typeSize, ""))
        return NULL;
    XVector* this_vector = XMalloc_System(sizeof(XVector));
    XVector_init(this_vector, typeSize, useCow);
    Set_Class_MemoryFree(this_vector, XFree_System);
    return this_vector;
}

XVector* XVector_create_copy(const XVector* other)
{
    if (other == NULL)
        return NULL;
    XVector* v = XVector_create_ex((((XContainer*)(other))->m_typeSize), XContainerIsCow(other));
    XVector_copy_base(v, other);
    return v;
}

XVector* XVector_create_move(XVector* other)
{
    if (other == NULL)
        return NULL;
    XVector* v = XVector_create_ex((((XContainer*)(other))->m_typeSize), XContainerIsCow(other));
    XVector_move_base(v, other);
    return v;
}

// 以下 base 函数保持原样，但内部通过虚函数调用最终会进入上面已修复的实现
bool XVector_resize_base(XVector* this_vector, size_t size)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Resize, bool (*)(XVector*, size_t))(this_vector, size);
}

bool XVector_push_front_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Front,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_front_move_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Front,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_push_back_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Back,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_back_move_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Back,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_insert(XVector* this_vector, int64_t index, const void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XVector_insert_array_base(this_vector, index, pvValue, 1);
}

bool XVector_insert_move(XVector* this_vector, int64_t index, const void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XVector_insert_array_move_base(this_vector, index, pvValue, 1);
}

bool XVector_insert_array_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array,
        bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_insert_array_move_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array,
        bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataMoveMethod(this_vector));
}

bool XVector_append_array_base(XVector* this_vector, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_append_Array,
        bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_append_array_move_base(XVector* this_vector, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_append_Array,
        bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataMoveMethod(this_vector));
}

void XVector_pop_front_base(XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    typedef void (*funcPtr)(XVector*);
    XClassGetVirtualFunc(this_vector, EXVector_Pop_Front, funcPtr)(this_vector);
}

void XVector_pop_back_base(XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    typedef void (*funcPtr)(XVector*);
    XClassGetVirtualFunc(this_vector, EXVector_Pop_Back, funcPtr)(this_vector);
}

void XVector_erase_base(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    XClassGetVirtualFunc(this_vector, EXVector_Erase,
        void(*)(XVector*, const XVector_iterator*, XVector_iterator*))(this_vector, it, next);
}

void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    typedef void (*funcPtr)(XVector*, int64_t, int64_t);
    XClassGetVirtualFunc(this_vector, EXVector_Remove, funcPtr)(this_vector, index, n);
}

void XVector_rcopy_base(XVector* this_One, const XVector* this_Two)
{
    if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
        return;
    typedef void(*funcPtr)(XVector*, XVector*);
    XClassGetVirtualFunc(this_One, EXVector_Rcopy, funcPtr)(this_One, this_Two);
}

void* XVector_at_base(const XVector* this_vector, int64_t index)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return NULL;
    return XClassGetVirtualFunc(this_vector, EXVector_At, void* (*)(XVector*, int64_t))(this_vector, index);
}

void* XVector_front_base(const XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return NULL;
    return XClassGetVirtualFunc(this_vector, EXVector_Front, void* (*)(XVector*))(this_vector);
}

void* XVector_back_base(const XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return NULL;
    return XClassGetVirtualFunc(this_vector, EXVector_Back, void* (*)(XVector*))(this_vector);
}

bool XVector_find_base(const XVector* this_vector, const void* findVal, XVector_iterator* it)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), "") || ISNULL(findVal, ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Find,
        bool(*)(XVector*, const void*, XVector_iterator*))(this_vector, findVal, it);
}

bool XVector_contains(const XVector* this_vector, const void* value)
{
    return XVector_find_base(this_vector, value, NULL);
}

int64_t XVector_indexOf(const XVector* this_vector, const void* value, int64_t from)
{
    if (ISNULL(this_vector, "XVector is NULL") ||
        ISNULL(value, "Value is NULL") ||
        from < 0 ||
        from >= (int64_t)XVector_size_base(this_vector))
        return -1;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XVector_size_base(this_vector);
    const char* data = (const char*)XVector_data(this_vector);
    for (size_t i = from; i < size; ++i)
    {
        const void* elem = &data[i * typeSize];
        if (XContainerCompare(this_vector))
        {
            if (XContainerCompare(this_vector)(elem, value) == XCompare_Equality)
                return (int64_t)i;
        }
        else if (memcmp(elem, value, typeSize) == 0)
            return (int64_t)i;
    }
    return -1;
}

int64_t XVector_lastIndexOf(const XVector* this_vector, const void* value, int64_t from)
{
    if (ISNULL(this_vector, "XVector is NULL") || ISNULL(value, "Value is NULL"))
        return -1;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XVector_size_base(this_vector);
    if (size == 0) return -1;
    int64_t start = (from < 0) ? (int64_t)size - 1 : (from >= (int64_t)size) ? (int64_t)size - 1 : from;
    const char* data = (const char*)XVector_data(this_vector);
    for (int64_t i = start; i >= 0; --i)
    {
        const void* elem = &data[(size_t)i * typeSize];
        if (XContainerCompare(this_vector))
        {
            if (XContainerCompare(this_vector)(elem, value) == XCompare_Equality)
                return i;
        }
        else if (memcmp(elem, value, typeSize) == 0)
            return i;
    }
    return -1;
}

XVector* XVector_last(const XVector* this_vector, int64_t n)
{
    return XVector_mid(this_vector, XContainerSize(this_vector) - n, -1);
}

XVector* XVector_mid(const XVector* this_vector, int64_t pos, int64_t length)
{
    if (ISNULL(this_vector, "XVector is NULL"))
        return NULL;

    size_t totalSize = XVector_size_base(this_vector);
    size_t typeSize = XContainerTypeSize(this_vector);
    XVector* result = XVector_create(typeSize);
    XContainerSetDataCopyMethod(result, XContainerDataCopyMethod(this_vector));
    XContainerSetDataMoveMethod(result, XContainerDataMoveMethod(this_vector));
    XContainerSetDataDeinitMethod(result, XContainerDataDeinitMethod(this_vector));
    if (ISNULL(result, "Failed to create XVector"))
        return NULL;

    if (pos < 0 || (size_t)pos >= totalSize)
        return result;

    size_t remaining = totalSize - (size_t)pos;
    size_t actualLen = (length < 0) ? remaining : ((size_t)length >= remaining) ? remaining : (size_t)length;
    if (actualLen == 0)
        return result;

    const char* srcData = (const char*)XVector_data(this_vector);
    for (size_t i = 0; i < actualLen; ++i)
    {
        const void* elem = srcData + (pos + i) * typeSize;
        if (!XVector_push_back_base(result, elem))
        {
            XVector_delete_base(result);
            return NULL;
        }
    }
    return result;
}

XVector* XVector_first(const XVector* this_vector, int64_t n)
{
    return XVector_mid(this_vector, 0, n);
}

void XVector_sort_base(XVector* this_vector, XSortOrder order)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    XClassGetVirtualFunc(this_vector, EXVector_Sort, void (*)(XVector*, XSortOrder))(this_vector, order);
}

bool XVector_replace(XVector* this_vector, int64_t index, void* pvValue)
{
    if (this_vector == NULL || index < -1 || index >= XVector_count_base(this_vector) || pvValue == NULL)
        return false;
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;
    void* oldValue = XVector_at_base(this_vector, index);
    if (oldValue == NULL)
        return false;
    if (XContainerDataDeinitMethod(this_vector))
        XContainerDataDeinitMethod(this_vector)(oldValue);
    if (XContainerDataCopyMethod(this_vector))
        XContainerDataCopyMethod(this_vector)(oldValue, pvValue);
    else
        memcpy(oldValue, pvValue, XContainerTypeSize(this_vector));
    return true;
}

bool XVector_replace_move(XVector* this_vector, int64_t index, void* pvValue)
{
    if (this_vector == NULL || index < -1 || index >= XVector_count_base(this_vector) || pvValue == NULL)
        return false;
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;
    void* oldValue = XVector_at_base(this_vector, index);
    if (oldValue == NULL)
        return false;
    if (XContainerDataDeinitMethod(this_vector))
        XContainerDataDeinitMethod(this_vector)(oldValue);
    if (XContainerDataMoveMethod(this_vector))
        XContainerDataMoveMethod(this_vector)(oldValue, pvValue);
    else
        memcpy(oldValue, pvValue, XContainerTypeSize(this_vector));
    return true;
}

bool XVector_format_text_core(XVector* vector, bool appendNull, const char* format, va_list args)
{
    if (vector == NULL || format == NULL)
        return false;
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    if (len <= 0) return false;

    size_t newSize = len + 1;
    if (!XVector_resize_base(vector, newSize))
        return false;
    va_copy(args_copy, args);
    vsnprintf((char*)XVector_data(vector), len + 1, format, args_copy);
    va_end(args_copy);

    if (!appendNull && newSize > 0)
        XVector_pop_back_base(vector);
    return true;
}

bool XVector_append_text_fmt(XVector* this_vector, bool appendNull, const char* format, ...)
{
    if (this_vector == NULL || format == NULL)
        return false;
    va_list args;
    va_start(args, format);
    bool result = XVector_format_text_core(this_vector, appendNull, format, args);
    va_end(args);
    return result;
}

XVector* XVector_create_text_fmt(bool appendNull, const char* format, ...)
{
    XVector* data = XVector_Create(uint8_t);
    if (data == NULL)
        return NULL;
    va_list args;
    va_start(args, format);
    bool result = XVector_format_text_core(data, appendNull, format, args);
    va_end(args);
    if (!result)
    {
        XVector_delete_base(data);
        return NULL;
    }
    return data;
}

#endif