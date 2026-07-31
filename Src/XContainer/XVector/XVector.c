#include "XVector.h"
#if XVector_ON
#include "XSort.h"
#include "XVtable.h"
#include <string.h>
#include <stdlib.h>

#define VECTORNUM 20  // 初始数组大小
static inline void* VXVectorData(XVector* vec);
// 前向声明
static bool VXVectorDetachIfNeeded(XVector * this_vector);
static void VXVectorDataDelete(void* data, XVector* this_vector);
static bool VXVectorEnlargeCapacity(XVector* this_vector);

// 虚函数实现声明
static void VXClass_copy(XVector* object, const XVector* src);
static void VXClass_move(XVector* object, XVector* src);
static bool VXVector_resize(XVector* this_vector, size_t size);
static bool VXVector_resizeCore(XVector* this_vector, size_t size, bool zeroFill);
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
static bool VXVector_reserve(XVector* this_vector, size_t size);
static void VXVector_squeeze(XVector* this_vector);

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
        VXVector_insert_array, /*VXVector_append_array,*/
        VXVector_pop_front, VXVector_pop_back, VXVector_erase, VXVector_remove,
        VXVector_rcopy,
        VXVector_at, VXVector_front, VXVector_back, VXVector_find,
        VXVector_sort,
        VXVector_reserve, VXVector_squeeze
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXVector_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
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
            XContainerSetDataPtr(this_vector, shared);
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
            /* 扩容前已完成 COW 分离，此时旧块由当前容器独占，释放前必须析构元素。 */
            XSharedData_release_with((XSharedData*)XContainerDataPtr(this_vector),
                (XCDataDeinitMethod)VXVectorDataDelete, this_vector);
            XContainerSetDataPtr(this_vector, newShared);
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

inline void* VXVectorData(XVector* vec)
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

    XSharedData* sd = (XSharedData*)XContainerDataPtr(this_vector);
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
    XContainerSetDataPtr(this_vector, newShared);
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
    // 如果目标还未初始化，先初始化
    if (XClassIsVtableNull(object))
    {
        XVector_init(object, XContainerTypeSize(src), XContainerIsCow(src));
    }
    XClass_Parent(XContainer,EXClass_Copy,void(*)(XVector*, const XVector*))(object, src);
    //else if ((XSharedData*)XContainerDataPtr(object))
    //{
    //    XSharedData_release_with((XSharedData*)XContainerDataPtr(object), (XCDataDeinitMethod)VXVectorDataDelete, object);
    //}

    //// 复制回调函数
    //XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    //XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    //XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    //// 共享源数据的 XSharedData（COW 机制）
    //XContainerSetDataPtr(object, (XSharedData*)XContainerDataPtr(src));
    //if ((XSharedData*)XContainerDataPtr(object))
    //{
    //    XSharedData_addRef((XSharedData*)XContainerDataPtr(object));
    //}

    //XContainerSize(object) = XContainerSize(src);
    //XContainerCapacity(object) = XContainerCapacity(src);
    //XContainerTypeSize(object) = XContainerTypeSize(src);
}

void VXClass_move(XVector* object, XVector* src)
{
    if (XClassIsVtableNull(object))
    {
        XVector_init(object, XContainerTypeSize(src), XContainerIsCow(src));
    }
    XClass_Parent(XContainer, EXClass_Move, void(*)(XVector*, const XVector*))(object, src);
    //else if ((XSharedData*)XContainerDataPtr(object))
    //{
    //    XSharedData_release_with((XSharedData*)XContainerDataPtr(object), (XCDataDeinitMethod)VXVectorDataDelete, object);
    //}

    //// 转移所有权（浅拷贝指针）
    //memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XVector) - sizeof(XClass));

    //// 清空源对象
    //XContainerSetDataPtr(src, NULL);
    //XContainerCapacity(src) = 0;
    //XContainerSize(src) = 0;
}

// ========================
// 容器大小调整
// ========================
static bool VXVector_resizeCore(XVector* this_vector, size_t size, bool zeroFill)
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
                    (char*)VXVectorData(this_vector) + i * typeSize);
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
            void* oldData = VXVectorData(this_vector);
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
            /* resize 的深拷贝完成后，旧块中的非 POD 元素仍需逐个释放。 */
            XSharedData_release_with((XSharedData*)XContainerDataPtr(this_vector),
                (XCDataDeinitMethod)VXVectorDataDelete, this_vector);
            XContainerSetDataPtr(this_vector, newShared);
        }
        else {
            void* newRaw = XRealloc_System(XContainerDataPtr(this_vector), bytes);
            if (!newRaw) return false;
            XContainerDataPtr(this_vector) = newRaw;
        }
        XContainerCapacity(this_vector) = newCapacity;
    }

    // 仅普通resize对新增元素清零；resizeForOverwrite保留未初始化内存（对齐Qt语义）
    if (zeroFill) {
        char* start = (char*)VXVectorData(this_vector) + oldSize * typeSize;
        memset(start, 0, (size - oldSize) * typeSize);
    }
    XContainerSize(this_vector) = size;
    return true;
}

// 普通resize的虚函数入口：对新增元素清零（保持原语义，注册于虚函数表EXVector_Resize）
bool VXVector_resize(XVector* this_vector, size_t size)
{
    return VXVector_resizeCore(this_vector, size, true);
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

    char* ptr = (char*)VXVectorData(this_vector)
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
        return VXVector_append_array(this_vector, begin, n, dataCreatMethod);
        //return XClassGetVirtualFunc(this_vector, EXVector_append_Array,
           // bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, dataCreatMethod);

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
            char* dest = (char*)VXVectorData(this_vector) + typeSize * sizen;
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
    char* dest = (char*)VXVectorData(this_vector) + oldSize * typeSize;
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
    char* data = VXVectorData(this_vector);

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
        XSharedData* sd = (XSharedData*)XContainerDataPtr(this_vector);
        if (sd && XSharedData_isShared(sd)) {
            XSharedData_release(sd);
            XContainerSetDataPtr(this_vector, NULL);
            XContainerCapacity(this_vector) = 0;
            XContainerSize(this_vector) = 0;
            return;
        }
    }

    // 其他情况：逐个析构元素
    if (XContainerDataDeinitMethod(this_vector)) {
        char* data = VXVectorData(this_vector);
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
    if (XClassIsVtableNull(this_One)) {
        // 目标未初始化，使用源的模式初始化
        bool useCow = XContainerIsCow((XVector*)this_Two);
        XVector_init(this_One, XContainerTypeSize(this_Two), useCow);
    }
    else {
        // 释放目标原有数据
        if (XContainerIsCow(this_One)) {
            XSharedData* sd = (XSharedData*)XContainerDataPtr(this_One);
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
            const char* src = (const char*)VXVectorData(this_Two);
            char* dst = newShared->data;
            XCDataCopyMethod copy = XContainerDataCopyMethod(this_One);
            for (size_t i = 0; i < size; i++) {
                const char* srcElem = src + (size - 1 - i) * typeSize;
                if (copy)
                    copy(dst + i * typeSize, srcElem);
                else
                    memcpy(dst + i * typeSize, srcElem, typeSize);
            }
            XContainerSetDataPtr(this_One, newShared);
        }
        else {
            void* newRaw = XMalloc_System(size * typeSize);
            if (!newRaw) return;
            const char* src = (const char*)VXVectorData(this_Two);
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
    return (void*)((char*)VXVectorData(this_vector) +
        XContainerTypeSize(this_vector) * index);
}

void* VXVector_front(const XVector* this_vector)
{
    if (XContainer_isEmpty_base(this_vector))
        return NULL;
    return VXVectorData(this_vector);
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
    char* data = VXVectorData(this_vector);
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
    XQuicPitSort_Stack(VXVectorData(this_vector),
        XContainerSize(this_vector),
        XContainerTypeSize(this_vector),
        XContainerCompare(this_vector),
        order);
}

// ========================
// 容量管理（预留/压缩，对齐Qt reserve/squeeze）
// ========================
static bool VXVector_reserve(XVector* this_vector, size_t size)
{
    // 容量已足够则不操作（reserve不缩小容量，故无需分离）
    if (size <= XContainerCapacity(this_vector))
        return true;
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t oldSize = XContainerSize(this_vector);
    size_t bytes = size * typeSize;

    if (XContainerIsCow(this_vector)) {
        XSharedData* newShared = XSharedData_create(NULL, bytes);
        if (!newShared) return false;
        void* oldData = VXVectorData(this_vector);
        XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
        if (oldData && oldSize > 0) {
            if (copy) {
                for (size_t i = 0; i < oldSize; i++)
                    copy((char*)newShared->data + i * typeSize,
                        (char*)oldData + i * typeSize);
            }
            else {
                memcpy(newShared->data, oldData, oldSize * typeSize);
            }
        }
        XSharedData* old = (XSharedData*)XContainerDataPtr(this_vector);
        if (old)
            XSharedData_release_with(old, (XCDataDeinitMethod)VXVectorDataDelete,
                this_vector);
        XContainerSetDataPtr(this_vector, newShared);
    }
    else {
        void* newRaw = XRealloc_System(XContainerDataPtr(this_vector), bytes);
        if (!newRaw) return false;
        XContainerDataPtr(this_vector) = newRaw;
    }
    XContainerCapacity(this_vector) = size;
    return true;
}

static void VXVector_squeeze(XVector* this_vector)
{
    if (!VXVectorDetachIfNeeded(this_vector))
        return;

    size_t size = XContainerSize(this_vector);
    size_t capacity = XContainerCapacity(this_vector);
    // 容量已不大于元素数量，无需压缩
    if (size >= capacity)
        return;

    size_t typeSize = XContainerTypeSize(this_vector);

    // 元素为空：直接释放整个缓冲区
    if (size == 0) {
        if (XContainerIsCow(this_vector)) {
            XSharedData* old = (XSharedData*)XContainerDataPtr(this_vector);
            if (old) {
                XSharedData_release_with(old, (XCDataDeinitMethod)VXVectorDataDelete, this_vector);
                XContainerSetDataPtr(this_vector, NULL);
            }
        }
        else {
            if (XContainerDataPtr(this_vector)) {
                XFree_System(XContainerDataPtr(this_vector));
                XContainerDataPtr(this_vector) = NULL;
            }
        }
        XContainerCapacity(this_vector) = 0;
        return;
    }

    size_t bytes = size * typeSize;
    if (XContainerIsCow(this_vector)) {
        XSharedData* newShared = XSharedData_create(NULL, bytes);
        if (!newShared) return;
        void* oldData = VXVectorData(this_vector);
        XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
        if (copy) {
            for (size_t i = 0; i < size; i++)
                copy((char*)newShared->data + i * typeSize,
                    (char*)oldData + i * typeSize);
        }
        else {
            memcpy(newShared->data, oldData, size * typeSize);
        }
        XSharedData* old = (XSharedData*)XContainerDataPtr(this_vector);
        // 深拷贝时需析构旧元素再释放；POD（浅拷贝）时仅释放缓冲区，避免双重释放
        if (copy)
            XSharedData_release_with(old, (XCDataDeinitMethod)VXVectorDataDelete, this_vector);
        else
            XSharedData_release(old);
        XContainerSetDataPtr(this_vector, newShared);
    }
    else {
        void* newRaw = XRealloc_System(XContainerDataPtr(this_vector), bytes);
        if (!newRaw) return;  // 失败时保留原缓冲区
        XContainerDataPtr(this_vector) = newRaw;
    }
    XContainerCapacity(this_vector) = size;
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

bool XVector_reserve_base(XVector* this_vector, size_t size)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Reserve, bool (*)(XVector*, size_t))(this_vector, size);
}

void XVector_squeeze_base(XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    XClassGetVirtualFunc(this_vector, EXVector_Squeeze, void (*)(XVector*))(this_vector);
}


bool XVector_push_front_1_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Front,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_front_3(XVector* this_vector,const XVector* pvValue)
{
    if(!this_vector||!pvValue|| XContainerTypeSize(this_vector)!= XContainerTypeSize(pvValue))
        return false;
    return XVector_push_front_2(this_vector, XVector_front_base(pvValue), XContainerSize(pvValue));
}

bool XVector_push_front_move_1_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Front,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_push_front_move_3(XVector* this_vector, XVector* pvValue)
{
    if (!this_vector || !pvValue || XContainerTypeSize(this_vector) != XContainerTypeSize(pvValue))
        return false;
    if (XVector_insert_move_1_base(this_vector, 0, XVector_front_base(pvValue), XContainerSize(pvValue)))
    {
        XContainerSize(pvValue) = 0;
        return true;
    }
    return false;
}

bool XVector_push_back_1_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Back,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_back_2(XVector* this_vector, const void* begin, size_t n)
{
    if (!this_vector || !begin || n == 0)
        return false;
    return XVector_insert_1_base(this_vector, XContainerSize(this_vector), begin, n);
}

bool XVector_push_back_3(XVector* this_vector, const XVector* pvValue)
{
    if (!this_vector || !pvValue || XContainerTypeSize(this_vector) != XContainerTypeSize(pvValue))
        return false;
    return XVector_insert_1_base(this_vector, XContainerSize(this_vector), XVector_front_base(pvValue), XContainerSize(pvValue));
}

void* XVector_emplace_back(XVector* this_vector)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return NULL;
    size_t oldSize = XContainerSize(this_vector);
    if (!XVector_resize_base(this_vector, oldSize + 1))
        return NULL;
    return XVector_at_base(this_vector, (int64_t)oldSize);
}

bool XVector_push_back_move_1_base(XVector* this_vector, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Push_Back,
        bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_push_back_move_2(XVector* this_vector, void* begin, size_t n)
{
    if (!this_vector || !begin || n == 0)
        return false;
    return XVector_insert_move_1_base(this_vector, XContainerSize(this_vector), begin, n);
}

bool XVector_push_back_move_3(XVector* this_vector, XVector* pvValue)
{
    if (!this_vector || !pvValue || XContainerTypeSize(this_vector) != XContainerTypeSize(pvValue))
        return false;
    if (XVector_insert_move_1_base(this_vector, XContainerSize(this_vector), XVector_front_base(pvValue), XContainerSize(pvValue)))
    {
        XContainerSize(pvValue)=0;
        return true;
    }
    return false;
}

bool XVector_insert_3(XVector* this_vector, int64_t index, const XVector* pvValue)
{
    if (!this_vector || !pvValue || XContainerTypeSize(this_vector) != XContainerTypeSize(pvValue))
        return false;
    return XVector_insert_1_base(this_vector, index, XVector_front_base(pvValue), XContainerSize(pvValue));
}

bool XVector_insert_move_3(XVector* this_vector, int64_t index, XVector* pvValue)
{
    if (!this_vector || !pvValue || XContainerTypeSize(this_vector) != XContainerTypeSize(pvValue))
        return false;
    if (XVector_insert_move_1_base(this_vector, index, XVector_front_base(pvValue), XContainerSize(pvValue)))
    {
        XContainerSize(pvValue)=0;
        return true;
    }
    return false;
}

bool XVector_insert_1_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array,
        bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_insert_move_1_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
    if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array,
        bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataMoveMethod(this_vector));
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

int64_t XVector_indexOf(const XVector* this_vector, const void* value, int64_t from)
{
    if (ISNULL(this_vector, "XVector is NULL") ||
        ISNULL(value, "Value is NULL") ||
        from < 0 ||
        from >= (int64_t)XVector_size_base(this_vector))
        return -1;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XVector_size_base(this_vector);
    const char* data = (const char*)VXVectorData(this_vector);
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
    const char* data = (const char*)VXVectorData(this_vector);
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

    const char* srcData = (const char*)VXVectorData(this_vector);
    for (size_t i = 0; i < actualLen; ++i)
    {
        const void* elem = srcData + (pos + i) * typeSize;
        if (!XVector_push_back_1_base(result, elem))
        {
            XVector_delete_base(result);
            return NULL;
        }
    }
    return result;
}

void XVector_sort_base(XVector* this_vector, XSortOrder order)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return;
    XClassGetVirtualFunc(this_vector, EXVector_Sort, void (*)(XVector*, XSortOrder))(this_vector, order);
}

bool XVector_replace_1(XVector* this_vector, int64_t index, void* pvValue)
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

bool XVector_replace_2(XVector* this_vector, int64_t index,const XVector* pvValue)
{
    if(!this_vector||!pvValue)
        return false;
    XVector_remove_base(this_vector, index,1);
    return  XVector_insert_3(this_vector, index, pvValue);
}

bool XVector_replace_move_1(XVector* this_vector, int64_t index, void* pvValue)
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

bool XVector_replace_move_2(XVector* this_vector, int64_t index, XVector* pvValue)
{
    if (!this_vector || !pvValue)
        return false;
    XVector_remove_base(this_vector, index, 1);
    return  XVector_insert_move_3(this_vector, index, pvValue);
}

// ========================
// Qt兼容扩展API实现
// ========================
bool XVector_resize_2(XVector* this_vector, size_t size, void* pvValue)
{
    if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
        return false;
    size_t oldSize = XContainerSize(this_vector);
    // 复用虚函数resize完成容量调整/缩小析构/新增置0
    if (!XVector_resize_base(this_vector, size))
        return false;
    // 新增部分用pvValue填充（覆盖resize的置0）
    if (size > oldSize && pvValue != NULL) {
        size_t typeSize = XContainerTypeSize(this_vector);
        char* data = (char*)VXVectorData(this_vector);
        XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
        for (size_t i = oldSize; i < size; i++) {
            if (copy)
                copy(data + i * typeSize, pvValue);
            else
                memcpy(data + i * typeSize, pvValue, typeSize);
        }
    }
    return true;
}

bool XVector_fill(XVector* this_vector, void* pvValue, int64_t size)
{
    if (ISNULL(this_vector, "") || ISNULL(pvValue, ""))
        return false;
    if (!VXVectorDetachIfNeeded(this_vector))
        return false;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t oldSize = XContainerSize(this_vector);
    size_t targetSize = (size < 0) ? oldSize : (size_t)size;
    XCDataCopyMethod copy = XContainerDataCopyMethod(this_vector);
    XCDataDeinitMethod deinit = XContainerDataDeinitMethod(this_vector);

    // 容量不足时先扩容到targetSize（resize会处理分配与新增置0）
    if (targetSize > XContainerCapacity(this_vector)) {
        if (!XVector_resize_base(this_vector, targetSize))
            return false;
    }
    char* data = (char*)VXVectorData(this_vector);

    // 已存在的槽位：先析构旧值再赋新值
    size_t assignCount = (oldSize < targetSize) ? oldSize : targetSize;
    for (size_t i = 0; i < assignCount; i++) {
        if (deinit)
            deinit(data + i * typeSize);
        if (copy)
            copy(data + i * typeSize, pvValue);
        else
            memcpy(data + i * typeSize, pvValue, typeSize);
    }
    // 新增槽位：构造新元素
    for (size_t i = oldSize; i < targetSize; i++) {
        memset(data + i * typeSize, 0, typeSize);
        if (copy)
            copy(data + i * typeSize, pvValue);
        else
            memcpy(data + i * typeSize, pvValue, typeSize);
    }
    // 缩减时析构被裁剪的元素
    if (deinit) {
        for (size_t i = targetSize; i < oldSize; i++)
            deinit(data + i * typeSize);
    }
    XContainerSize(this_vector) = targetSize;
    return true;
}

size_t XVector_removeAll(XVector* this_vector, const void* value)
{
    if (ISNULL(this_vector, "") || ISNULL(value, ""))
        return 0;
    if (XContainer_isEmpty_base(this_vector))
        return 0;
    if (!VXVectorDetachIfNeeded(this_vector))
        return 0;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XContainerSize(this_vector);
    char* data = (char*)VXVectorData(this_vector);
    XCDataDeinitMethod deinit = XContainerDataDeinitMethod(this_vector);
    size_t removed = 0;
    size_t write = 0;

    for (size_t read = 0; read < size; read++) {
        void* elem = data + read * typeSize;
        bool match = XContainerCompare(this_vector)
            ? (XContainerCompare(this_vector)(elem, value) == XCompare_Equality)
            : (memcmp(elem, value, typeSize) == 0);
        if (match) {
            if (deinit)
                deinit(elem);
            removed++;
        }
        else {
            if (write != read)
                memcpy(data + write * typeSize, elem, typeSize);
            write++;
        }
    }
    XContainerSize(this_vector) = write;
    return removed;
}

bool XVector_removeOne(XVector* this_vector, const void* value)
{
    if (ISNULL(this_vector, "") || ISNULL(value, ""))
        return false;
    int64_t idx = XVector_indexOf(this_vector, value, 0);
    if (idx < 0)
        return false;
    XVector_remove_base(this_vector, idx, 1);
    return true;
}

size_t XVector_count_value(const XVector* this_vector, const void* value)
{
    if (ISNULL(this_vector, "") || ISNULL(value, ""))
        return 0;
    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XContainerSize(this_vector);
    const char* data = (const char*)VXVectorData(this_vector);
    size_t count = 0;
    for (size_t i = 0; i < size; i++) {
        const void* elem = data + i * typeSize;
        bool match = XContainerCompare(this_vector)
            ? (XContainerCompare(this_vector)(elem, value) == XCompare_Equality)
            : (memcmp(elem, value, typeSize) == 0);
        if (match)
            count++;
    }
    return count;
}

void XVector_move(XVector* this_vector, int64_t from, int64_t to)
{
    if (ISNULL(this_vector, ""))
        return;
    size_t size = XContainerSize(this_vector);
    if (from < 0 || (size_t)from >= size || to < 0 || (size_t)to >= size || from == to)
        return;
    if (!VXVectorDetachIfNeeded(this_vector))
        return;

    size_t typeSize = XContainerTypeSize(this_vector);
    char* data = (char*)VXVectorData(this_vector);
    void* temp = XMalloc_System(typeSize);
    if (!temp)
        return;
    // 暂存待移动元素（字节级搬迁，与项目内remove/shift的方式一致）
    memcpy(temp, data + from * typeSize, typeSize);
    if (from < to)
        memmove(data + from * typeSize, data + (from + 1) * typeSize,
            (size_t)(to - from) * typeSize);
    else
        memmove(data + (to + 1) * typeSize, data + to * typeSize,
            (size_t)(from - to) * typeSize);
    memcpy(data + to * typeSize, temp, typeSize);
    XFree_System(temp);
}

void XVector_swapItemsAt(XVector* this_vector, int64_t i, int64_t j)
{
    if (ISNULL(this_vector, ""))
        return;
    size_t size = XContainerSize(this_vector);
    if (i < 0 || (size_t)i >= size || j < 0 || (size_t)j >= size || i == j)
        return;
    if (!VXVectorDetachIfNeeded(this_vector))
        return;

    size_t typeSize = XContainerTypeSize(this_vector);
    uint8_t* a = (uint8_t*)VXVectorData(this_vector) + (size_t)i * typeSize;
    uint8_t* b = (uint8_t*)VXVectorData(this_vector) + (size_t)j * typeSize;
    // 逐字节交换，避免额外内存分配
    for (size_t k = 0; k < typeSize; k++) {
        uint8_t t = a[k];
        a[k] = b[k];
        b[k] = t;
    }
}

void* XVector_takeAt(XVector* this_vector, int64_t index)
{
    if (ISNULL(this_vector, ""))
        return NULL;
    size_t size = XContainerSize(this_vector);
    if (index < 0 || (size_t)index >= size)
        return NULL;
    if (!VXVectorDetachIfNeeded(this_vector))
        return NULL;

    size_t typeSize = XContainerTypeSize(this_vector);
    char* data = (char*)VXVectorData(this_vector);
    void* out = XMalloc_System(typeSize);
    if (!out)
        return NULL;
    // 取出元素（移动语义：优先用move方法转移资源，否则字节拷贝）
    XCDataMoveMethod move = XContainerDataMoveMethod(this_vector);
    if (move) {
        memset(out, 0, typeSize);
        move(out, data + index * typeSize);
    }
    else {
        memcpy(out, data + index * typeSize, typeSize);
    }
    // 后续元素前移（字节级搬迁，被取出元素不再析构）
    memmove(data + index * typeSize, data + (index + 1) * typeSize,
        (size - (size_t)index - 1) * typeSize);
    XContainerSize(this_vector)--;
    return out;
}

bool XVector_startsWith(const XVector* this_vector, const void* value)
{
    if (ISNULL(this_vector, "") || ISNULL(value, ""))
        return false;
    void* first = XVector_front_base(this_vector);
    if (!first)
        return false;
    size_t typeSize = XContainerTypeSize(this_vector);
    return XContainerCompare(this_vector)
        ? (XContainerCompare(this_vector)(first, value) == XCompare_Equality)
        : (memcmp(first, value, typeSize) == 0);
}

bool XVector_endsWith(const XVector* this_vector, const void* value)
{
    if (ISNULL(this_vector, "") || ISNULL(value, ""))
        return false;
    void* last = XVector_back_base(this_vector);
    if (!last)
        return false;
    size_t typeSize = XContainerTypeSize(this_vector);
    return XContainerCompare(this_vector)
        ? (XContainerCompare(this_vector)(last, value) == XCompare_Equality)
        : (memcmp(last, value, typeSize) == 0);
}


// ========================
// Qt兼容扩展API：数据访问 / 谓词删除 / 字典序比较
// ========================
void* XVector_data(XVector* this_vector)
{
    if (ISNULL(this_vector, ""))
        return NULL;
    // 对齐Qt data()：返回可写裸指针前先做COW分离，避免写入污染共享数据
    if (!VXVectorDetachIfNeeded(this_vector))
        return NULL;
    return XContainerDataAddr(this_vector);
}

const void* XVector_constData(const XVector* this_vector)
{
    if (ISNULL(this_vector, ""))
        return NULL;
    // 只读访问，不触发COW分离（与Qt constData()语义一致）
    return XContainerDataAddr(this_vector);
}

size_t XVector_removeIf(XVector* this_vector, XEquality pred, const void* userData)
{
    if (ISNULL(this_vector, "") || ISNULL(pred, ""))
        return 0;
    if (XContainer_isEmpty_base(this_vector))
        return 0;
    if (!VXVectorDetachIfNeeded(this_vector))
        return 0;

    size_t typeSize = XContainerTypeSize(this_vector);
    size_t size = XContainerSize(this_vector);
    char* data = (char*)VXVectorData(this_vector);
    XCDataDeinitMethod deinit = XContainerDataDeinitMethod(this_vector);
    size_t removed = 0;
    size_t write = 0;

    // 原地压缩：谓词返回true的元素被析构删除，其余前移（字节级搬迁，与removeAll一致）
    for (size_t read = 0; read < size; read++) {
        void* elem = data + read * typeSize;
        if (pred(elem, userData)) {
            if (deinit)
                deinit(elem);
            removed++;
        }
        else {
            if (write != read)
                memcpy(data + write * typeSize, elem, typeSize);
            write++;
        }
    }
    XContainerSize(this_vector) = write;
    return removed;
}

int32_t XVector_compare(const XVector* lhs, const XVector* rhs)
{
    if (ISNULL(lhs, "") || ISNULL(rhs, ""))
        return XCompare_Other;

    size_t lSize = XContainerSize(lhs);
    size_t rSize = XContainerSize(rhs);
    size_t typeSize = XContainerTypeSize(lhs);
    // 元素类型不一致视为不可比较（对齐Qt要求同类型语义），仅按数量给出关系
    if (typeSize != XContainerTypeSize(rhs)) {
        if (lSize == rSize) return XCompare_Equality;
        return (lSize < rSize) ? XCompare_Less : XCompare_Greater;
    }

    size_t minSize = (lSize < rSize) ? lSize : rSize;
    const char* lData = (const char*)VXVectorData(lhs);
    const char* rData = (const char*)VXVectorData(rhs);
    XCompare cmp = XContainerCompare(lhs);

    // 逐元素字典序比较（有比较函数用之，否则内存比较）
    for (size_t i = 0; i < minSize; i++) {
        const void* le = lData + i * typeSize;
        const void* re = rData + i * typeSize;
        int32_t r;
        if (cmp)
            r = cmp(le, re);
        else {
            int m = memcmp(le, re, typeSize);
            r = (m < 0) ? XCompare_Less : (m > 0) ? XCompare_Greater : XCompare_Equality;
        }
        if (r != XCompare_Equality)
            return r;
    }
    // 公共元素全相等时，按元素数量决断（对齐Qt operator<的字典序语义）
    if (lSize < rSize) return XCompare_Less;
    if (lSize > rSize) return XCompare_Greater;
    return XCompare_Equality;
}


// ========================
// Qt兼容扩展API：覆写式调整 / 共享检测 / 容量上限
// ========================
bool XVector_resizeForOverwrite(XVector* this_vector, size_t size)
{
    if (ISNULL(this_vector, ""))
        return false;
    // 对齐Qt resizeForOverwrite：扩容/缩容逻辑与resize一致，但新增元素不清零（保留未初始化内存）
    return VXVector_resizeCore(this_vector, size, false);
}

bool XVector_isSharedWith(const XVector* this_vector, const XVector* other)
{
    if (this_vector == NULL || other == NULL)
        return false;
    // 仅COW模式可能共享底层XSharedData块；非COW各自独立分配，不可能共享
    if (!XContainerIsCow(this_vector) || !XContainerIsCow(other))
        return false;
    XSharedData* a = (XSharedData*)XContainerDataPtr(this_vector);
    XSharedData* b = (XSharedData*)XContainerDataPtr(other);
    // 两个向量指向同一共享块即为共享（对齐Qt isSharedWith的指针相等语义）；均为空(NULL)不算共享
    return (a != NULL) && (a == b);
}

void XVector_detach(XVector* this_vector)
{
    if (ISNULL(this_vector, ""))
        return;
    // 对齐Qt detach()：强制COW分离，确保本向量独占数据（若已独占或非COW则为空操作）
    VXVectorDetachIfNeeded(this_vector);
}

bool XVector_isDetached(const XVector* this_vector)
{
    if (this_vector == NULL)
        return true;
    // 非COW永远独占；COW下引用计数为1即独占（对齐Qt isDetached = !isShared）
    if (!XContainerIsCow(this_vector))
        return true;
    XSharedData* sd = (XSharedData*)XContainerDataPtr(this_vector);
    if (!sd)
        return true;
    return !XSharedData_isShared(sd);
}

size_t XVector_maxSize(size_t typeSize)
{
    // 理论最大元素数：保证 capacity*typeSize 不溢出 size_t（对齐Qt Data::maxSize语义）
    if (typeSize == 0)
        return 0;
    return SIZE_MAX / typeSize;
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
    vsnprintf((char*)VXVectorData(vector), len + 1, format, args_copy);
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
