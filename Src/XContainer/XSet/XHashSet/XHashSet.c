#include "XHashSet.h"
#if XHashSet_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include "XRedBlackTree.h"
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
    //printf("进入扩容\n");
    size_t new_size = new_capacity * sizeof(XRBTreeNode*);
    XRBTreeNode** newData = XMemory_malloc(new_size);
    memset(newData, 0, new_size);
    if (newData == NULL)
        return false;

    // 遍历原哈希表
    for (size_t i = 0; i < XContainerCapacity(set); i++)
    {
        XRBTreeNode* root = ((XRBTreeNode**)XContainerDataPtr(set))[i];
        if (root != NULL)
        {
            // 遍历红黑树，将节点插入到新哈希表中
            XVector* nodes = XBTree_TraversingToXVector(root, XBTreeInorder);
            if (nodes != NULL)
            {
                for (size_t j = 0; j < XVector_getSize_base(nodes); j++)
                {
                    XRBTreeNode* node = ((XRBTreeNode**)XContainerDataPtr(nodes))[j];
                    void*key = XRBTree_getData(node);
                    size_t index = set->m_hash(key, XContainerTypeSize(set)) % new_capacity;

                    // 将节点插入到新哈希表的相应红黑树中
                    XRBTree_insert(&newData[index], ((XSetBase*)set)->m_KeyLess, XCompareRuleTwo_XSet, XRBTree_getData(node), XContainerTypeSize(set));
                }
                XVector_delete_base(nodes);
            }
            // 删除原红黑树
            XRBTree_delete(root, NULL, NULL);
        }
    }

    // 释放原哈希表数组
    XMemory_free(XContainerDataPtr(set));
    XContainerDataPtr(set) = newData;
    XContainerCapacity(set) = new_capacity;
    return true;
}

bool VXSet_insert(XHashSet* this_set, const void* pvKey)
{
    if ((double)XContainerSize(this_set) / XContainerCapacity(this_set) >= DEFAULT_LOAD_FACTOR)
    {
        //printf("XHashSet 扩容\n");
        size_t new_capacity = XContainerCapacity(this_set) * 2;
        if (!XHashSet_resize(this_set, new_capacity))
        {
            printf("XHashSet 扩容失败\n");
            return false;
        }
    }

    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);

    XRBTreeNode* current = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey);
    if (current == NULL)
    {//节点不存在
        XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XSetBase*)this_set)->m_KeyLess, XCompareRuleTwo_XSet, pvKey, XContainerTypeSize(this_set));
        ++XContainerSize(this_set);
    }
}

void VXSet_erase(XHashSet* this_set, const void* pvKey)
{
    XHashSet_remove_base(this_set, pvKey);
}

bool VXSet_remove(XHashSet* this_set, const void* pvKey)
{
    if (XSetBase_isEmpty_base(this_set))
        return false;
    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode* node = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey);
    if (node != NULL)
    {
        void* value = XBTreeNode_getData(node);
        if (XContainerDataDeleteMethod(this_set) != NULL)
            XContainerDataDeleteMethod(this_set)(value);
        XRBTree_erase(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey);
        --XContainerSize(this_set);
        return true;
    }
}

bool VXSet_find(XHashSet* this_set, const void* pvKey)
{
    if (XSetBase_isEmpty_base(this_set))
        return false;
    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode* node = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey);
    return node != NULL;
}
static void XSet_freeNodeData(void* key, XHashSet* this_set)
{
    if (XContainerDataDeleteMethod(this_set) != NULL)
        XContainerDataDeleteMethod(this_set)(key);
}
void VXSet_clear(XHashSet* this_set)
{
    if (XHashSet_isEmpty_base(this_set))
        return;
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        XRBTreeNode* root = ((XRBTreeNode**)XContainerDataPtr(this_set))[i];
        XBTree_delete(root, XSet_freeNodeData, this_set);
    }
    memset(XContainerDataPtr(this_set), 0, sizeof(XRBTreeNode*) * XContainerCapacity(this_set));
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
    size_t size = sizeof(XRBTreeNode*) * XContainerCapacity(this_set);
    XContainerDataPtr(this_set) = XMemory_malloc(size);
    if (XContainerDataPtr(this_set) == NULL)
    {
        XMemory_free(this_set);
        return;
    }
    memset(XContainerDataPtr(this_set), 0, size);
}

#endif