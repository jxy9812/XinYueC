#include "XHashSet.h"
#if XHashSet_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include "XRedBlackTree.h"
#include <string.h>

// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXHashSetDetachIfNeeded(XHashSet* this_set);
static void VXHashSetDataDelete(void* data, XHashSet* this_set);

// Set插入数据
static bool VXSet_insert(XHashSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod);
// Set删除数据
static void VXSet_erase(XHashSet* this_set, const XHashSet_iterator* it, XHashSet_iterator* next);
// Set移除数据
static bool VXSet_remove(XHashSet* this_set, const void* key);
// 查找数据，返回是否找到
static bool VXSet_find(XHashSet* this_set, const void* key, XHashSet_iterator* it);
// 清空Set，释放内存
static void VXSet_clear(XHashSet* this_set);
static void VXClass_copy(XHashSet* object, const XHashSet* src);
static void VXClass_move(XHashSet* object, XHashSet* src);
static void VXSet_deinit(XHashSet* this_set);
//static void VXSet_swap(XHashSet* this_setOne, XHashSet* this_setTwo);
static XVector* VXSetBase_keys(const XSetBase* this_set);

// 私有函数：扩容哈希表
static bool XHashSet_resize(XHashSet* set, size_t new_capacity);

static void XSet_deleteNodeData(void* key, XHashSet* this_set)
{
    if (!key || this_set)return;
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
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXSet_insert,VXSet_erase, VXSet_remove, VXSet_find,VXSetBase_keys
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXSet_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSet_deinit);
    //XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXSet_swap);
#if SHOWCONTAINERSIZE
    printf("XHashSet size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
    return XVTABLE_DEFAULT;
}
// COW分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXHashSetDetachIfNeeded(XHashSet* this_set)
{
    if (!XContainerSharedData(this_set) || !XSharedData_isShared(XContainerSharedData(this_set)))
        return true; // 不共享，无需分离
    size_t capacity = XContainerCapacity(this_set);
    size_t typeSize = XContainerTypeSize(this_set);
    // 分配新的桶数组
    size_t newSize = capacity * sizeof(XRBTreeNode*);
    XRBTreeNode** newData = XMalloc_System(newSize);
    if (!newData)
        return false;
    memset(newData, 0, newSize);
    XRBTreeNode** oldData = (XRBTreeNode**)XContainerDataPtr(this_set);
    // 深拷贝每个桶的红黑树
    for (size_t i = 0; i < capacity; i++)
    {
        XRBTreeNode* root = oldData[i];
        if (root != NULL)
        {
            // 遍历红黑树，拷贝节点到新树
            XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
            if (!nodes)
            {
                XFree_System(newData);
                return false;
            }
            XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
                        for (size_t j = 0; j < XVector_size_base(nodes); j++)
            {
                XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerDataPtr(nodes))[j];
                void* oldKey = XBTreeNode_GetDataPtr(oldNode);

                // 创建新节点
                XRBTreeNode* newNode = XRBTree_create(NULL, typeSize);
                if (!newNode)
                {
                    XVector_delete_base(nodes);
                    XFree_System(newData);
                    return false;
                }
                void* newKey = XBTreeNode_GetDataPtr(newNode);
                // 拷贝数据
                if (XContainerDataCopyMethod(this_set))
                    XContainerDataCopyMethod(this_set)(newKey, oldKey);
                else
                    memcpy(newKey, oldKey, typeSize);
                // 插入到新红黑树
                XRBTree_SetRed(newNode);
                memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
                ((XTreeNode*)newNode)->parentNode = NULL;
                XRBTree_insertNode(&newData[i], XContainerCompare(this_set), XCompareRuleTwo_XSet, newNode);
            }
            XVector_delete_base(nodes);
        }
    }
    // 创建新的 XSharedData
    XSharedData* newShared = XSharedData_create(newData);
    if (!newShared)
    {
        XFree_System(newData);
        return false;
    }
    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(this_set));
    XContainerSharedData(this_set) = newShared;
    return true;
}
// 删除HashSet数据
static void VXHashSetDataDelete(void* data, XHashSet* this_set)
{
    if (data == NULL || this_set == NULL)
        return;
    XRBTreeNode** buckets = (XRBTreeNode**)data;
    size_t capacity = XContainerCapacity(this_set);
    // 删除每个桶的红黑树
    for (size_t i = 0; i < capacity; i++)
    {
        if (buckets[i] != NULL)
        {
            XTree_delete(buckets[i], XSet_deleteNodeData, this_set);
        }
    }
    XFree_System(data);
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
    XContainerSharedData(this_set) = NULL;
}
// 私有函数：扩容哈希表
static bool XHashSet_resize(XHashSet* set, size_t new_capacity)
{
    size_t new_size = new_capacity * sizeof(XRBTreeNode*);
    XRBTreeNode** newData = XMalloc_System(new_size);
    memset(newData, 0, new_size);
    if (newData == NULL)
        return false;

    // 获取原数据
    XSharedData* oldSD = XContainerSharedData(set);
    XRBTreeNode** oldData = oldSD ? (XRBTreeNode**)oldSD->data : NULL;

    // 遍历原哈希表
    for (size_t i = 0; i < XContainerCapacity(set); i++)
    {
        XRBTreeNode* root = oldData ? oldData[i] : NULL;
        if (root != NULL)
        {
            // 遍历红黑树，将节点插入到新哈希表中
            XVector* nodes = XVector_create(sizeof(struct XTreeNode*));
            XVector_resize_base(nodes, XContainerSize(set));
            XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
            if (nodes != NULL)
            {
                for (size_t j = 0; j < XVector_size_base(nodes); j++)
                {
                    XRBTreeNode* node = ((XRBTreeNode**)XContainerDataPtr(nodes))[j];
                    void* key = XRBTree_getData(node);
                    size_t index = set->m_hash(key, XContainerTypeSize(set)) % new_capacity;

                    XRBTree_SetRed(node);
                    memset(XTreeNode_GetNodes(node), 0, sizeof(XTreeNode*) * ((XTreeNode*)node)->nodeCount);
                    ((XTreeNode*)node)->parentNode = NULL;
                    XRBTree_insertNode(&newData[index], XContainerCompare(set), XCompareRuleTwo_XSet, node);
                }
                XVector_delete_base(nodes);
            }
        }
    }

    // 释放原共享数据
    if (oldSD)
    {
        if (XSharedData_isShared(oldSD))
        {
            XSharedData_release(oldSD);
        }
        else
        {
            if (oldSD->data)
                XFree_System(oldSD->data);
            XSharedData_release(oldSD);
        }
    }

    // 创建新的 XSharedData
    XSharedData* newShared = XSharedData_create(newData);
    if (!newShared)
    {
        XFree_System(newData);
        return false;
    }
    XContainerSharedData(set) = newShared;
    XContainerCapacity(set) = new_capacity;
    return true;
}

bool VXSet_insert(XHashSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod)
{
    // COW分离
    if (!VXHashSetDetachIfNeeded(this_set))
        return false;
    if (!XContainerSharedData(this_set))
    {
        XContainerCapacity(this_set) = DEFAULT_CAPACITY;

        size_t size = sizeof(XRBTreeNode*) * XContainerCapacity(this_set);
        void* data = XMalloc_System(size);
        if (data == NULL)
        {
            XFree_System(this_set);
            return false;
        }
        memset(data, 0, size);

        // 创建 XSharedData 包装数据
        XSharedData* sd = XSharedData_create(data);
        if (sd == NULL)
        {
            XFree_System(data);
            XFree_System(this_set);
            return false;
        }
        XContainerSharedData(this_set) = sd;
    }
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

    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);

    XRBTreeNode* current = XRBTree_findNode(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, key);
    if (current == NULL)
    {//节点不存在
        if (dataCreatMethod)
        {
            void* temp = XCalloc_System(1, XContainerTypeSize(this_set));
            dataCreatMethod(temp, key);
            XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, XContainerCompare(this_set), XCompareRuleTwo_XSet, temp, XContainerTypeSize(this_set));
            XFree_System(temp);
        }
        else
        {
            XRBTree_insert(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, XContainerCompare(this_set), XCompareRuleTwo_XSet, key, XContainerTypeSize(this_set));
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

    // COW分离
    if (!VXHashSetDetachIfNeeded(this_set))
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


    // 从哈希表对应桶的红黑树中删除节点
    // 哈希表存储的是红黑树根节点数组，需传入对应桶的根节点地址
    XRBTreeNode* removeNode=XRBTree_removeNode(
        &((XRBTreeNode**)XContainerDataPtr(this_set))[it->index],  // 对应桶的红黑树根节点指针
        current_node,                                               // 要删除的节点
        XContainerTypeSize(this_set)                                     // 传递容器作为额外参数
    );
    if(removeNode)
    {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete(removeNode);
        // 更新容器大小（哈希表容量不随元素删除改变，仅更新元素数量）
        --XContainerSize(this_set);
    }

    // 设置下一个迭代器
    if (next != NULL)
        *next = next_it;
}

bool VXSet_remove(XHashSet* this_set, const void* key)
{
    if (XSetBase_isEmpty_base(this_set))
        return false;

    // COW分离
    if (!VXHashSetDetachIfNeeded(this_set))
        return false;
    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode* removeNode = XRBTree_remove(((XRBTreeNode**)XContainerDataPtr(this_set)) + index, ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, key, XContainerTypeSize(this_set));
    if (removeNode != NULL)
    {
        XSet_deleteNodeData(key,this_set);
        XRBTreeNode_delete(removeNode);
        --XContainerSize(this_set);
        return true;
    }
    return false;
}

bool VXSet_find(XHashSet* this_set, const void* key,XHashSet_iterator* it)
{
    if (XSetBase_isEmpty_base(this_set))
    {
        if (it)
            *it = XHashSet_end(this_set);
        return false;
    }
    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode* node = XRBTree_findNode(((XRBTreeNode**)XContainerDataPtr(this_set))[index], ((XContainer*)this_set)->m_compare, XCompareRuleOne_XSet, key);
    if (node == NULL)
    {
        if (it)
            *it = XHashSet_end(this_set);
        return false;
    }
    if (it)
        it->node = node;
    return true;
}

void VXSet_clear(XHashSet* this_set)
{
    if (XHashSet_isEmpty_base(this_set))
        return;
    // 如果数据被共享，减少引用并创建空数据
    if (XContainerSharedData(this_set) && XSharedData_isShared(XContainerSharedData(this_set)))
    {
        XSharedData_release(XContainerSharedData(this_set));
        XContainerSharedData(this_set) = NULL;
        XContainerCapacity(this_set) = 0;
        XContainerSize(this_set) = 0;
        return;
    }

    // 不共享，直接删除数据
    for (size_t i = 0; i < XContainerCapacity(this_set); i++)
    {
        XRBTreeNode* root = ((XRBTreeNode**)XContainerDataPtr(this_set))[i];
        XTree_delete(root, XSet_deleteNodeData, this_set);
    }
    memset(XContainerDataPtr(this_set), 0, sizeof(XRBTreeNode*) * XContainerCapacity(this_set));
    XContainerSize(this_set) = 0;
}

void VXClass_copy(XHashSet* object, const XHashSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XHashSet_init(object, XContainerTypeSize(src), src->m_hash, XContainerCompare(src));
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXHashSetDataDelete, object);
    }

    // 复制回调函数
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSharedData(object) = XContainerSharedData(src);
    if (XContainerSharedData(object))
    {
        XSharedData_addRef(XContainerSharedData(object));
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
}

void VXClass_move(XHashSet* object, XHashSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XHashSet_init(object, XContainerTypeSize(src), src->m_hash, XContainerCompare(src));
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXHashSetDataDelete, object);
    }

    // 转移所有权
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XHashSet) - sizeof(XClass));

    // 清空源对象的共享数据指针
    XContainerSharedData(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
}

void VXSet_deinit(XHashSet* this_set)
{
    if(XContainerSharedData(this_set))
    {
        XSharedData_release_with(XContainerSharedData(this_set), VXHashSetDataDelete, this_set);
        XContainerSize(this_set) = 0;
        XContainerCapacity(this_set) = 0;
        XContainerSharedData(this_set) = NULL;
    }
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

XHashSet* XHashSet_create(const size_t keyTypeSize, XHashFunc hash, XCompare compare)
{
    XHashSet* set = XMalloc_System(sizeof(XHashSet));
    XHashSet_init(set, keyTypeSize, hash,compare);
    Set_Class_MemoryFree(set, XFree_System);
    return set;
}

void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XCompare compare)
{
    if (this_set == NULL)
        return;
    XSetBase_init(&this_set->m_class, keyTypeSize, compare);
    XClassGetVtable(this_set) = XHashSet_class_init();
    this_set->m_hash = hash;
    XContainerSharedData(this_set) = NULL;
}

#endif