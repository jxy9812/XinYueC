#include "XHashSet.h"
#if XHashSet_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include <string.h>

// Set插入数据
static bool VXSet_insert(XHashSet* this_set, const void* pvKey);
// Set删除数据
static void VXSet_erase(XHashSet* this_set, const void* pvKey);
// Set移除数据
static bool VXSet_remove(XHashSet* this_set, const void* pvKey);
// 查找数据，返回是否找到
static bool VXSet_find(XHashSet* this_set, const void* pvKey);
// 清空Set，释放内存
static void VXSet_clear(XHashSet* this_set);
// 释放内存
static void VXSet_delete(XHashSet* this_set);
static void VXSet_swap(XHashSet* this_setOne, XHashSet* this_setTwo);
static XVector* VXSetBase_keys(const XSetBase* this_set);

// 私有函数：扩容哈希表
static bool XHashSet_resize(XHashSet* set, size_t new_capacity);

XVtable* XHashSet_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XHASHSET_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
    void* table[] = {
        VXSet_insert, VXSet_erase, VXSet_remove, VXSet_find,VXSetBase_keys
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXSet_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXSet_delete);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXSet_swap);
#if SHOWCONTAINERSIZE
    printf("XHashSet size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
    return XVTABLE_DEFAULT;
}

// 私有函数：扩容哈希表
static bool XHashSet_resize(XHashSet* set, size_t new_capacity)
{
    size_t new_size = new_capacity * sizeof(XHashSetNode*);
    XHashSetNode** newData = XMemory_malloc(new_size);
    memset(newData, 0, new_size);
    if (newData == NULL)
        return false;

    for (size_t i = 0; i < XContainerCapacity(set); i++)
    {
        XHashSetNode* current = ((XHashSetNode**)XContainerDataPtr(set))[i];
        while (current)
        {
            XHashSetNode* next = current->next;
            size_t index = set->m_hash(current->key, XContainerTypeSize(set)) % new_capacity;
            XHashSetNode* new_current = newData[index];
            if (new_current != NULL)
            {
                while (new_current->next)
                {
                    new_current = new_current->next;
                }
                new_current->next = current;
            }
            else
            {
                newData[index] = current;
            }
            current->next = NULL;
            current = next;
        }
    }

    XMemory_free(XContainerDataPtr(set));
    XContainerDataPtr(set) = newData;
    XContainerCapacity(set) = new_capacity;
    return true;
}

bool VXSet_insert(XHashSet* this_set, const void* pvKey)
{
    if ((double)XContainerSize(this_set) / XContainerCapacity(this_set) >= DEFAULT_LOAD_FACTOR)
    {
        size_t new_capacity = XContainerCapacity(this_set) * 2;
        if (!XHashSet_resize(this_set, new_capacity))
        {
            printf("XHashSet 扩容失败\n");
            return false;
        }
    }

    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XHashSetNode* current = ((XHashSetNode**)XContainerDataPtr(this_set))[index];

    while (current)
    {
        if (this_set->m_parent.m_KeyEquality(current->key, pvKey))
        {
            return; // 元素已存在
        }
        current = current->next;
    }

    XHashSetNode* new_node = (XHashSetNode*)XMemory_malloc(sizeof(XHashSetNode));
    if (new_node == NULL)
        return false;

    new_node->key = XMemory_malloc(XContainerTypeSize(this_set));
    memcpy(new_node->key, pvKey, XContainerTypeSize(this_set));
    new_node->next = ((XHashSetNode**)XContainerDataPtr(this_set))[index];
    ((XHashSetNode**)XContainerDataPtr(this_set))[index] = new_node;
    ++XContainerSize(this_set);
    return true;
}

void VXSet_erase(XHashSet* this_set, const void* pvKey)
{
    if (XSetBase_isEmpty_base(this_set))
        return;
    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XHashSetNode* current = ((XHashSetNode**)XContainerDataPtr(this_set))[index];
    XHashSetNode* prev = NULL;

    while (current)
    {
        if (this_set->m_parent.m_KeyEquality(current->key, pvKey))
        {
            if (prev)
            {
                prev->next = current->next;
            }
            else
            {
                ((XHashSetNode**)XContainerDataPtr(this_set))[index] = current->next;
            }
            XMemory_free(current->key);
            XMemory_free(current);
            --XContainerSize(this_set);
            return; // 释放成功
        }

        prev = current;
        current = current->next;
    }
}

bool VXSet_remove(XHashSet* this_set, const void* pvKey)
{
    VXSet_erase(this_set, pvKey);
    return true;
}

bool VXSet_find(XHashSet* this_set, const void* pvKey)
{
    if (XSetBase_isEmpty_base(this_set))
        return false;
    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XHashSetNode* current = ((XHashSetNode**)XContainerDataPtr(this_set))[index];

    while (current)
    {
        if (this_set->m_parent.m_KeyEquality(current->key, pvKey))
        {
            return true;
        }
        current = current->next;
    }
    return false;
}

void VXSet_clear(XHashSet* this_set)
{
    XHashSetNode* deleteNode = NULL;
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        XHashSetNode* current = ((XHashSetNode**)XContainerDataPtr(this_set))[i];

        while (current)
        {
            deleteNode = current;
            current = current->next;
            XMemory_free(deleteNode->key);
            XMemory_free(deleteNode);
        }
    }
    memset(XContainerDataPtr(this_set), 0, sizeof(XHashSetNode*) * XContainerCapacity(this_set));
    XContainerSize(this_set) = 0;
}

void VXSet_delete(XHashSet* this_set)
{
    VXSet_clear(this_set);
    void* data = XContainerDataPtr(this_set);
    if (data)
        XMemory_free(data);
    XMemory_free(this_set);
}

void VXSet_swap(XHashSet* this_setOne, XHashSet* this_setTwo)
{
    // 调用父类交换一部分
    XVtableGetFunc(XContainerObject_class_init(), EXContainerObject_Swap, void(*)(XHashSet*, XHashSet*))(this_setOne, this_setTwo);
    XSwap(&(this_setOne->m_parent.m_KeyEquality), &(this_setTwo->m_parent.m_KeyEquality), sizeof(XEquality));
    XSwap(&XContainerTypeSize(this_setOne), & XContainerTypeSize(this_setTwo), sizeof(size_t));
    XSwap(&(this_setOne->m_hash), &(this_setTwo->m_hash), sizeof(XHashFunc));
}

XVector* VXSetBase_keys(const XSetBase* this_set)
{
    XVector* v = XVector_create(XContainerTypeSize(this_set));
    for_each_iterator(this_set, XHashSet, it)
    {
        XVector_push_back_base(v, XHashSet_iterator_data(&it));
    }
    return v;
}

XHashSet* XHashSet_create(const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
    XHashSet* set = XMemory_malloc(sizeof(XHashSet));
    XHashSet_init(set, keyTypeSize, hash, KeyEquality,KeyLess);
    return set;
}

void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
    if (this_set == NULL)
        return;
    XSetBase_init(&this_set->m_parent, keyTypeSize, KeyEquality,KeyLess);
    XClassGetVtable(this_set) = XHashSet_class_init();
    this_set->m_hash = hash;
    XContainerCapacity(this_set) = DEFAULT_CAPACITY;
    size_t size = sizeof(XHashSetNode*) * XContainerCapacity(this_set);
    XContainerDataPtr(this_set) = XMemory_malloc(size);
    if (XContainerDataPtr(this_set) == NULL)
        XMemory_free(this_set);
    memset(XContainerDataPtr(this_set), 0, size);
}

#endif