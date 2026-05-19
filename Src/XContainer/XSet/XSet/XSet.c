#include "XSet.h"
#if XSet_ON
#include "XRedBlackTree.h"
#include "XAlgorithm.h"
#include <stdlib.h>
#include <string.h>

// 获取根节点指针的地址（统一 COW/非 COW 模式）
static inline XRBTreeNode** XSet_root_ptr(XSet* set) {
    if (XContainerIsCow(set)) {
        return (XRBTreeNode**)XContainerSharedDataPtr(set);
    }
    else {
        return (XRBTreeNode**)&XContainerDataPtr(set);
    }
}

// 辅助：获取根节点（用于只读）
static inline XRBTreeNode* XSet_root(XSet* set) {
    XRBTreeNode** ptr = XSet_root_ptr(set);
    return ptr ? *ptr : NULL;
}

// COW分离
static bool VXSetDetachIfNeeded(XSet* this_set);
static void VXSetDataDelete(void* data, XSet* this_set);

static bool VXSet_insert(XSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod);
static void VXSet_erase(XSet* this_set, const XSet_iterator* it, XSet_iterator* next);
static bool VXSet_remove(XSet* this_set, const void* key);
static bool VXSet_find(XSet* this_set, const void* key, XSet_iterator* it);
static XVector* VXSetBase_keys(const XSetBase* this_set);
static void VXSet_clear(XSet* this_set);
static void VXClass_copy(XSet* object, const XSet* src);
static void VXClass_move(XSet* object, XSet* src);
static void VXSet_deinit(XSet* this_set);

XVtable* XSet_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSET_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXSet_insert, VXSet_erase, VXSet_remove, VXSet_find,
        VXSetBase_keys
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXSet_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSet_deinit);
#if SHOWCONTAINERSIZE
    printf("XSet size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

static bool VXSetDetachIfNeeded(XSet* this_set)
{
    if (!XContainerIsCow(this_set)) return true;  // 非 COW 不需要分离

    XSharedData* sd = XContainerSharedData(this_set);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t typeSize = XContainerTypeSize(this_set);
    XRBTreeNode* oldRoot = *XSet_root_ptr(this_set);
    if (oldRoot == NULL) return true;

    XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
    if (!nodes) return false;
    XBTree_TraversingToXVector(oldRoot, XBTreePreorder, nodes);

    XRBTreeNode* newRoot = NULL;
    for (size_t i = 0; i < XVector_size_base(nodes); i++)
    {
        XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
        void* oldData = XBTreeNode_GetDataPtr(oldNode);
        XRBTreeNode* newNode = XRBTree_create(NULL, typeSize);
        if (!newNode)
        {
            XVector_delete_base(nodes);
            return false;
        }
        void* newData = XBTreeNode_GetDataPtr(newNode);
        if (XContainerDataCopyMethod(this_set))
            XContainerDataCopyMethod(this_set)(newData, oldData);
        else
            memcpy(newData, oldData, typeSize);
        XRBTree_SetRed(newNode);
        memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
        ((XTreeNode*)newNode)->parentNode = NULL;
        XRBTree_insertNode(&newRoot, XContainerCompare(this_set), XCompareRuleTwo_XSet, newNode);
    }
    XVector_delete_base(nodes);

    XSharedData* newShared = XSharedData_create(NULL, sizeof(XRBTreeNode*));
    if (!newShared)
    {
        XTree_delete(newRoot, NULL, NULL);
        return false;
    }
    *(XRBTreeNode**)newShared->data = newRoot;
    XSharedData_release(sd);
    XContainerSharedData(this_set) = newShared;
    return true;
}

static void XSet_deleteNodeData(void* key, XSet* this_set)
{
    if (XContainerDataDeinitMethod(this_set))
        XContainerDataDeinitMethod(this_set)(key);
}

static void VXSetDataDelete(void* data, XSet* this_set)
{
    if (data == NULL || this_set == NULL) return;
    XRBTreeNode* root = *(XRBTreeNode**)data;
    if (root)
        XTree_delete(root, XSet_deleteNodeData, this_set);
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
}

void VXSet_clear(XSet* this_set)
{
    if (XSet_isEmpty_base(this_set)) return;

    if (XContainerIsCow(this_set) && XContainerSharedData(this_set) && XSharedData_isShared(XContainerSharedData(this_set)))
    {
        XSharedData_release(XContainerSharedData(this_set));
        XContainerSharedData(this_set) = NULL;
        XContainerCapacity(this_set) = 0;
        XContainerSize(this_set) = 0;
        return;
    }

    // 非 COW 或未共享
    XRBTreeNode* root = XSet_root(this_set);
    if (root)
        XTree_delete(root, XSet_deleteNodeData, this_set);
    // 清空根指针
    if (XContainerIsCow(this_set))
        *(XRBTreeNode**)XContainerSharedDataPtr(this_set) = NULL;
    else
        XContainerDataPtr(this_set) = NULL;
    XContainerCapacity(this_set) = 0;
    XContainerSize(this_set) = 0;
}

void VXClass_copy(XSet* object, const XSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XSet_init(object, XContainerTypeSize(src), XContainerCompare(src), XContainerIsCow(src));
    }
    else
    {
        if (XContainerIsCow(object))
        {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXSetDataDelete, object);
        }
        else
        {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
                XTree_delete(root, XSet_deleteNodeData, object);
            XContainerDataPtr(object) = NULL;
        }
    }

    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    if (XContainerIsCow(src))
    {
        XContainerSharedData(object) = XContainerSharedData(src);
        if (XContainerSharedData(object))
            XSharedData_addRef(XContainerSharedData(object));
    }
    else
    {
        // 非 COW：深拷贝红黑树（简化为遍历插入，实际可优化）
        XRBTreeNode* srcRoot = (XRBTreeNode*)XContainerDataPtr(src);
        if (srcRoot)
        {
            // 深拷贝：使用遍历 + 逐个插入（为了简单，可复用 VXSetDetachIfNeeded 的拷贝逻辑）
            // 这里通过创建一个临时容器来拷贝，但更高效的方式是直接复制节点。
            // 为保持简洁，建议实现一个专用的红黑树拷贝函数。
            // 此处暂不实现，如有需要可参照 XMap 的深拷贝。
        }
        else
        {
            XContainerDataPtr(object) = NULL;
        }
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
}

void VXClass_move(XSet* object, XSet* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XSet_init(object, XContainerTypeSize(src), XContainerCompare(src), XContainerIsCow(src));
    }
    else
    {
        if (XContainerIsCow(object))
        {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXSetDataDelete, object);
        }
        else
        {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
                XTree_delete(root, XSet_deleteNodeData, object);
            XContainerDataPtr(object) = NULL;
        }
    }
   
    //memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XSet) - sizeof(XClass));

    if (XContainerIsCow(object))
        XContainerSharedData(object) = NULL;
    else
        XContainerDataPtr(object) = NULL;
    XContainerCapacity(object) = 0;
    XContainerSize(object) = 0;

    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XSet) - sizeof(XClass));
}

void VXSet_deinit(XSet* this_set)
{
    if (XContainerIsCow(this_set))
    {
        if (XContainerSharedData(this_set))
            XSharedData_release_with(XContainerSharedData(this_set), VXSetDataDelete, this_set);
    }
    else
    {
        XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(this_set);
        if (root)
            XTree_delete(root, XSet_deleteNodeData, this_set);
        XContainerDataPtr(this_set) = NULL;
    }
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
}

bool VXSet_insert(XSet* this_set, const void* pvKey, XCDataCreatMethod dataCreatMethod)
{
    if (!VXSetDetachIfNeeded(this_set)) return false;

    if (!XSetBase_contains(this_set, pvKey))
    {
        if (XContainerIsCow(this_set))
        {
            if (!XContainerSharedData(this_set))
                XContainerSharedData(this_set) = XSharedData_create(NULL, sizeof(XRBTreeNode*));
        }
        else
        {
            if (!XContainerDataPtr(this_set))
                XContainerDataPtr(this_set) = NULL;
        }

        XRBTreeNode** root_ptr = XSet_root_ptr(this_set);
        if (dataCreatMethod)
        {
            void* temp = XCalloc_System(1, XContainerTypeSize(this_set));
            dataCreatMethod(temp, pvKey);
            XRBTree_insert(root_ptr, XContainerCompare(this_set), XCompareRuleTwo_XSet, temp, XContainerTypeSize(this_set));
            XFree_System(temp);
        }
        else
        {
            XRBTree_insert(root_ptr, XContainerCompare(this_set), XCompareRuleTwo_XSet, pvKey, XContainerTypeSize(this_set));
        }
        ++XContainerCapacity(this_set);
        ++XContainerSize(this_set);
    }
    return true;
}

void VXSet_erase(XSet* this_set, const XSet_iterator* it, XSet_iterator* next)
{
    if (XSet_iterator_isEnd(it))
    {
        if (next) *next = XSet_end(this_set);
        return;
    }
    if (!VXSetDetachIfNeeded(this_set))
    {
        if (next) *next = XSet_end(this_set);
        return;
    }

    XSet_iterator next_it = *it;
    XSet_iterator_add(this_set, &next_it);

    XRBTreeNode* current_node = (XRBTreeNode*)it->node;
    if (!current_node)
    {
        if (next) *next = next_it;
        return;
    }

    XRBTreeNode* removeNode = XRBTree_removeNode(XSet_root_ptr(this_set), current_node, XContainerTypeSize(this_set));
    if (removeNode)
    {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete(removeNode);
        --XContainerCapacity(this_set);
        --XContainerSize(this_set);
    }
    if (next) *next = next_it;
}

bool VXSet_remove(XSet* this_set, const void* pvKey)
{
    if (XSet_isEmpty_base(this_set)) return false;
    if (!VXSetDetachIfNeeded(this_set)) return false;

    XRBTreeNode* removeNode = XRBTree_remove(XSet_root_ptr(this_set), XContainerCompare(this_set), XCompareRuleOne_XSet, pvKey, XContainerTypeSize(this_set));
    if (removeNode)
    {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete(removeNode);
        --XContainerCapacity(this_set);
        --XContainerSize(this_set);
        return true;
    }
    return false;
}

bool VXSet_find(XSet* this_set, const void* key, XSet_iterator* it)
{
    if (XSet_isEmpty_base(this_set))
    {
        if (it) *it = XSet_end(this_set);
        return false;
    }
    XRBTreeNode* node = XRBTree_findNode(*XSet_root_ptr(this_set), XContainerCompare(this_set), XCompareRuleOne_XSet, key);
    if (node == NULL)
    {
        if (it) *it = XSet_end(this_set);
        return false;
    }
    if (it) it->node = node;
    return true;
}

XVector* VXSetBase_keys(const XSetBase* this_set)
{
    XVector* v = XVector_create(XContainerTypeSize(this_set));
    for_each_iterator(this_set, XSet, it)
    {
        XVector_push_back_base(v, XSet_iterator_data(&it));
    }
    return v;
}

XSet* XSet_create_ex(const size_t keyTypeSize, XCompare compare, bool useCow)
{
    if (keyTypeSize == 0 || compare == NULL) return NULL;
    XSet* this_set = (XSet*)XMalloc_System(sizeof(XSet));
    if (!this_set) return NULL;
    XSet_init(this_set, keyTypeSize, compare, useCow);
    Set_Class_MemoryFree(this_set, XFree_System);
    return this_set;
}

void XSet_init(XSet* this_set, const size_t keyTypeSize, XCompare compare, bool useCow)
{
    if (ISNULL(this_set, "")) return;
    if (keyTypeSize == 0 || compare == NULL) return;
    XSetBase_init(&this_set->m_class, keyTypeSize, compare, useCow);
    XClassSetVtable(this_set, XSet);
}

#endif