#include "XHashSet.h"
#if XHashSet_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include "XRedBlackTree.h"
#include <string.h>

// Set插入数据
static bool VXSet_insert(XHashSet* this_set, const void* pvKey, XCDataCreatMethod dataCreatMethod);
// Set删除数据
static void VXSet_erase(XHashSet* this_set, const XHashSet_iterator* it, XHashSet_iterator* next);
// Set移除数据
static bool VXSet_remove(XHashSet* this_set, const void* pvKey);
// 查找数据，返回是否找到
static bool VXSet_find(XHashSet* this_set, const void* pvKey);
// 清空Set，释放内存
static void VXSet_clear(XHashSet* this_set);
static void VXClass_copy(XHashSet* object, const XHashSet* src);
static void VXClass_move(XHashSet* object, XHashSet* src);
static void VXSet_deinit(XHashSet* this_set);
static void VXSet_swap(XHashSet* this_setOne, XHashSet* this_setTwo);
static XVector* VXSetBase_keys(const XSetBase* this_set);

// 私有函数：扩容哈希表
static bool XHashSet_resize(XHashSet* set, size_t new_capacity);

static void XSet_freeNodeData(void* key, XHashSet* this_set)
{
    if (XContainerDataDeinitMethod(this_set) != NULL)
        XContainerDataDeinitMethod(this_set)(key);
}
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
        VXSet_insert,VXSet_erase, VXSet_remove, VXSet_find,VXSetBase_keys
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXSet_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSet_deinit);
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
                for (size_t j = 0; j < XVector_size_base(nodes); j++)
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

bool VXSet_insert(XHashSet* this_set, const void* pvKey, XCDataCreatMethod dataCreatMethod)
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
        if (dataCreatMethod)
        {
            void* temp = XMemory_calloc(1, XContainerTypeSize(this_set));
            dataCreatMethod(temp, pvKey);
            XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XSetBase*)this_set)->m_KeyLess, XCompareRuleTwo_XSet, temp, XContainerTypeSize(this_set));
            XMemory_free(temp);
        }
        else
        {
            XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XSetBase*)this_set)->m_KeyLess, XCompareRuleTwo_XSet, pvKey, XContainerTypeSize(this_set));
        }
        ++XContainerSize(this_set);
    }
}

void VXSet_erase(XHashSet* this_set, const XHashSet_iterator* it, XHashSet_iterator* next)
{
    // 检查参数有效性：容器为空、迭代器为空或迭代器已指向末尾
    if (XHashSet_iterator_isEnd(it))
    {
        if (next != NULL)
            *next = XHashSet_end(this_set);
        return;
    }

    // 预存下一个迭代器（删除当前节点前先获取，避免迭代器失效）
    XHashSet_iterator next_it = *it;
    XHashSet_iterator_add(this_set, &next_it);

    // 获取当前迭代器指向的红黑树节点
    XRBTreeNode* current_node = (XRBTreeNode*)it->node;
    if (!current_node)
    {
        if (next != NULL)
            *next = next_it;
        return;
    }

    // 获取当前节点存储的键值
    void* current_key = XBTreeNode_GetDataPtr(current_node);
    if (!current_key)
    {
        if (next != NULL)
            *next = next_it;
        return;
    }

    // 从哈希表对应桶的红黑树中删除节点
    // 哈希表存储的是红黑树根节点数组，需传入对应桶的根节点地址
    XRBTree_remove(
        &((XRBTreeNode**)XContainerDataPtr(this_set))[it->index],  // 对应桶的红黑树根节点指针
        ((XSetBase*)this_set)->m_KeyLess,                          // 键比较函数
        ((XSetBase*)this_set)->m_KeyEquality,                      // 键相等判断函数
        XCompareRuleOne_XSet,                                       // 比较规则
        current_key,                                               // 要删除的键值
        XSet_freeNodeData,                                         // 节点数据释放回调
        this_set                                                   // 传递容器作为额外参数
    );

    // 更新容器大小（哈希表容量不随元素删除改变，仅更新元素数量）
    --XContainerSize(this_set);

    // 设置下一个迭代器
    if (next != NULL)
        *next = next_it;
}

bool VXSet_remove(XHashSet* this_set, const void* pvKey)
{
    if (XSetBase_isEmpty_base(this_set))
        return false;
    size_t index = this_set->m_hash(pvKey, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode* node = XRBTree_findData(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey);
    if (node != NULL)
    {
        XRBTree_remove(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XSetBase*)this_set)->m_KeyLess, ((XSetBase*)this_set)->m_KeyEquality, XCompareRuleOne_XSet, pvKey, XSet_freeNodeData,this_set);
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

void VXSet_clear(XHashSet* this_set)
{
    if (XHashSet_isEmpty_base(this_set))
        return;
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        XRBTreeNode* root = ((XRBTreeNode**)XContainerDataPtr(this_set))[i];
        XTree_delete(root, XSet_freeNodeData, this_set);
    }
    memset(XContainerDataPtr(this_set), 0, sizeof(XRBTreeNode*) * XContainerCapacity(this_set));
    XContainerSize(this_set) = 0;
}

void VXClass_copy(XHashSet* object, const XHashSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XSetBase* set = src;
        XHashSet_init(object,XContainerTypeSize(src), src->m_hash, set->m_KeyEquality, set->m_KeyLess);
    }
    else if (!XHashSet_isEmpty_base(object))
    {
        XHashSet_clear_base(object);
    }
    for_each_iterator(src, XHashSet, it)
    {
        XHashSet_insert_base(object, XHashSet_iterator_data(&it));
    }
}

void VXClass_move(XHashSet* object, XHashSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XSetBase* set = src;
        XHashSet_init(object, XContainerTypeSize(src), src->m_hash, set->m_KeyEquality, set->m_KeyLess);
    }
    else if (!XHashSet_isEmpty_base(object))
    {
        XHashSet_clear_base(object);
    }
    XSwap(object, src, sizeof(XHashSet));
}

void VXSet_deinit(XHashSet* this_set)
{
    VXSet_clear(this_set);
    if(XContainerDataPtr(this_set))
    {
        XMemory_free(XContainerDataPtr(this_set));
        XContainerDataPtr(this_set) = NULL;
    }
    //XMemory_free(this_set);
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