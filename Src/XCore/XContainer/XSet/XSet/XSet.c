#include "XSet.h"
#if XSet_ON
#include <string.h>
#include "XRedBlackTree.h"
#include "XAlgorithm.h"

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
static void XSet_deleteNodeData(void* key, XSet* this_set);

static bool VXSet_insert(XSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod);
static void VXSet_erase(XSet* this_set, const XSet_iterator* it, XSet_iterator* next);
static bool VXSet_remove(XSet* this_set, const void* key);
static bool VXSet_find(XSet* this_set, const void* key, XSet_iterator* it);
static XVector* VXSetBase_keys(const XSetBase* this_set);
static void VXSet_clear(XSet* this_set);
static void VXClass_copy(XSet* object, const XSet* src);
static void VXClass_move(XSet* object, XSet* src);
static void VXSet_deinit(XSet* this_set);

static void XSet_copy_data_to_node(XSet* set, void* source, XRBTreeNode* node)
{
    void* target = XBTreeNode_GetDataPtr(node);
    if (XContainerDataCopyMethod(set))
        XContainerDataCopyMethod(set)(target, source);
    else
        memcpy(target, source, XContainerTypeSize(set));
}

static void XSet_deinit_data(XSet* set, void* data)
{
    if (XContainerDataDeinitMethod(set))
        XContainerDataDeinitMethod(set)(data);
}

static void XSet_release_data_after_copy(XSet* set, void* data)
{
    if (XContainerDataCopyMethod(set)) {
        if (XContainerDataDeinitMethod(set))
            XContainerDataDeinitMethod(set)(data);
    }
    else if (XContainerDataDeinitMethod(set)) {
        memset(data, 0, XContainerTypeSize(set));
    }
}

XVtable* XSet_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XSET_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XSet");
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
    XCLASS_SHOW_SIZE_DEFAULT(XSet);
    return XVTABLE_DEFAULT;
}

static bool VXSetDetachIfNeeded(XSet* this_set)
{
    if (!XContainerIsCow(this_set)) return true;  // 非 COW 不需要分离

    XSharedData* sd = (XSharedData*)XContainerDataPtr(this_set);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t typeSize = XContainerTypeSize(this_set);
    XRBTreeNode* oldRoot = *XSet_root_ptr(this_set);
    if (oldRoot == NULL) return true;

    XVector* nodes = XVector_create_ex(XContainer_memory_type(this_set), sizeof(XRBTreeNode*), true);
    if (!nodes) return false;
    XBTree_TraversingToXVector(oldRoot, XBTreePreorder, nodes);

    XRBTreeNode* newRoot = NULL;
    for (size_t i = 0; i < XVector_size_base(nodes); i++)
    {
        XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
        void* oldData = XBTreeNode_GetDataPtr(oldNode);
        XRBTreeNode* newNode = XRBTree_create_ex(NULL, typeSize, XContainer_memory(this_set));
        if (!newNode)
        {
            /* 回滚：newRoot 上已挂入的深拷贝节点归该树所有，必须带数据析构
               回调销毁，否则这些节点失去唯一根引用全部泄漏（同下方 newShared
               失败出口与 VXSetDataDelete 的释放方式） */
            XTreeNode_deleteTree(newRoot, XTreeNode_delete, XSet_deleteNodeData, this_set, XContainer_memory(this_set));
            XClassDelete(nodes);
            return false;
        }
        void* newData = XBTreeNode_GetDataPtr(newNode);
        if (XContainerDataCopyMethod(this_set))
            XContainerDataCopyMethod(this_set)(newData, oldData);
        else
            memcpy(newData, oldData, typeSize);
        XRBTree_SetRed(newNode);
        memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->m_nodeCount);
        ((XTreeNode*)newNode)->m_parentNode = NULL;
        XRBTree_insertNode(&newRoot, XContainerCompare(this_set), XCompareRuleTwo_XSet, newNode, XContainer_memory(this_set));
    }
    XClassDelete(nodes);

    XSharedData* newShared = XSharedData_create_ex(NULL, sizeof(XRBTreeNode*), XContainer_memory(this_set));
    if (!newShared)
    {
        /* 释放新建树时必须带数据析构回调：节点负载经拷贝方法深拷贝出的
           持有资源不能随节点缓冲一起裸释放 */
        XTreeNode_deleteTree(newRoot, XTreeNode_delete, XSet_deleteNodeData, this_set, XContainer_memory(this_set));
        return false;
    }
    *(XRBTreeNode**)newShared->data = newRoot;
    XSharedData_release(sd, XContainer_memory(this_set));
    XContainerSetDataPtr(this_set, newShared);
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
        XTreeNode_deleteTree(root, XTreeNode_delete, XSet_deleteNodeData, this_set, XContainer_memory(this_set));
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
}

void VXSet_clear(XSet* this_set)
{
    if (XSet_isEmpty_base(this_set)) return;

    if (XContainerIsCow(this_set) && (XSharedData*)XContainerDataPtr(this_set) && XSharedData_isShared((XSharedData*)XContainerDataPtr(this_set)))
    {
        XSharedData_release((XSharedData*)XContainerDataPtr(this_set),
            XContainer_memory(this_set));
        XContainerSetDataPtr(this_set, NULL);
        XContainerCapacity(this_set) = 0;
        XContainerSize(this_set) = 0;
        return;
    }

    // 非 COW 或未共享
    XRBTreeNode* root = XSet_root(this_set);
    if (root)
                XTreeNode_deleteTree(root, XTreeNode_delete, XSet_deleteNodeData, this_set, XContainer_memory(this_set));
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
    if (!object || !src) return;
    bool target_uninitialized = XClassIsVtableNull(object);
    if (target_uninitialized)
    {
        XSet_init(object, XContainerTypeSize(src), XContainerCompare(src), XContainerIsCow(src));
        Class_Memory(object) = Class_Memory(src);
    }
    else if (XContainerIsCow(src) &&
        XContainer_memory(object) != XContainer_memory(src)) {
        return;
    }
    else
    {
        if (XContainerIsCow(object))
        {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object),
                    VXSetDataDelete, object, XContainer_memory(object));
        }
        else
        {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
                XTreeNode_deleteTree(root, XTreeNode_delete, XSet_deleteNodeData, object, XContainer_memory(object));
            XContainerDataPtr(object) = NULL;
        }
    }

    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    /* 统一 COW 模式：新数据按源的模式写入，必须与目标的 m_useCow 一致，
       否则非 COW 目标接管 XSharedData 后会按裸树根释放（XTreeNode_deleteTree 野指针、
       引用计数永不归还），COW 目标写入裸树根后按共享块释放同样错乱。 */
    XContainerIsCow(object) = XContainerIsCow(src);

    if (XContainerIsCow(src))
    {
        XContainerSetDataPtr(object, (XSharedData*)XContainerDataPtr(src));
        if ((XSharedData*)XContainerDataPtr(object))
            XSharedData_addRef((XSharedData*)XContainerDataPtr(object));
    }
    else
    {
        // 非 COW：深拷贝红黑树（遍历 + 逐个新建节点插入，参照 XMap 的深拷贝）
        XRBTreeNode* srcRoot = (XRBTreeNode*)XContainerDataPtr(src);
        if (srcRoot)
        {
            size_t typeSize = XContainerTypeSize(src);
            XRBTreeNode* newRoot = NULL;
            XVector* nodes = XVector_create_ex(XContainer_memory_type(object), sizeof(XRBTreeNode*), true);
            if (!nodes) return;
            XBTree_TraversingToXVector(srcRoot, XBTreePreorder, nodes);
            for (size_t i = 0; i < XVector_size_base(nodes); i++)
            {
                XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
                void* oldData = XBTreeNode_GetDataPtr(oldNode);
                XRBTreeNode* newNode = XRBTree_create_ex(NULL, typeSize, XContainer_memory(object));
                if (!newNode)
                {
                    /* 回滚：newRoot 上已挂入的深拷贝节点归该树所有，必须带数据析构
                       回调销毁，否则这些节点失去唯一根引用全部泄漏 */
                    XClassDelete(nodes);
                    XTreeNode_deleteTree(newRoot, XTreeNode_delete, XSet_deleteNodeData, object, XContainer_memory(object));
                    return;
                }
                void* newData = XBTreeNode_GetDataPtr(newNode);
                if (XContainerDataCopyMethod(object))
                    XContainerDataCopyMethod(object)(newData, oldData);
                else
                    memcpy(newData, oldData, typeSize);
                XRBTree_SetRed(newNode);
                memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->m_nodeCount);
                ((XTreeNode*)newNode)->m_parentNode = NULL;
                XRBTree_insertNode(&newRoot, XContainerCompare(object), XCompareRuleTwo_XSet, newNode, XContainer_memory(object));
            }
            XClassDelete(nodes);
            XContainerDataPtr(object) = newRoot;
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
    if (!object || !src) return;
    XMemory* source_memory = Class_Memory(src);
    if (XClassIsVtableNull(object))
    {
        XSet_init(object, XContainerTypeSize(src), XContainerCompare(src), XContainerIsCow(src));
    }
    else
    {
        if (XContainerIsCow(object))
        {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object),
                    VXSetDataDelete, object, XContainer_memory(object));
        }
        else
        {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
                XTreeNode_deleteTree(root, XTreeNode_delete, XSet_deleteNodeData, object, XContainer_memory(object));
            XContainerDataPtr(object) = NULL;
        }
    }
   
    //memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XSet) - sizeof(XClass));

    if (XContainerIsCow(object))
        XContainerSetDataPtr(object, NULL);
    else
        XContainerDataPtr(object) = NULL;
    XContainerCapacity(object) = 0;
    XContainerSize(object) = 0;

    XMemory_swap((XClass*)object + 1, (XClass*)src + 1, sizeof(XSet) - sizeof(XClass));
    Class_Memory(object) = source_memory;
}

void VXSet_deinit(XSet* this_set)
{
    if (!this_set) return;
    if (XContainerIsCow(this_set))
    {
        if ((XSharedData*)XContainerDataPtr(this_set))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(this_set),
                VXSetDataDelete, this_set, XContainer_memory(this_set));
    }
    else
    {
        XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(this_set);
        if (root)
            XTreeNode_deleteTree(root, XTreeNode_delete, XSet_deleteNodeData, this_set, XContainer_memory(this_set));
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
            if (!(XSharedData*)XContainerDataPtr(this_set))
            {
                /* 分配失败必须提前返回：否则 root_ptr 得 NULL，节点不挂链却被
                   当作成功计数，每次插入泄漏一个节点及其负载 */
                XSharedData* sd = XSharedData_create_ex(NULL, sizeof(XRBTreeNode*), XContainer_memory(this_set));
                if (!sd) return false;
                XContainerSetDataPtr(this_set, sd);
            }
        }
        else
        {
            if (!XContainerDataPtr(this_set))
                XContainerDataPtr(this_set) = NULL;
        }

        XRBTreeNode** root_ptr = XSet_root_ptr(this_set);
        if (dataCreatMethod)
        {
            void* temp = XContainer_calloc(this_set, 1, XContainerTypeSize(this_set));
            if (!temp) return false;
            dataCreatMethod(temp, pvKey);
            /* 先以浅拷贝位入节点参与比较插入：XRBTree_insertNode 失败时库内直接
               XTreeNode_delete 裸释放节点、不调元素析构，若深拷贝已入节点，其
               持有资源将随节点一起丢失，故深拷贝推迟到插入成功后进行 */
            XRBTreeNode* insertNode = XRBTree_create_ex((const char*)temp,
                XContainerTypeSize(this_set), XContainer_memory(this_set));
            if (!insertNode) {
                XSet_deinit_data(this_set, temp);
                XContainer_free(this_set, temp);
                return false;
            }
            XRBTreeNode* inserted = XRBTree_insertNode(root_ptr, XContainerCompare(this_set),
                XCompareRuleTwo_XSet, insertNode, XContainer_memory(this_set));
            if (!inserted) {
                /* 失败节点已由库内释放且仅持浅拷贝位；temp 的持有资源在本处析构 */
                XSet_deinit_data(this_set, temp);
                XContainer_free(this_set, temp);
                return false;
            }
            if (XContainerDataCopyMethod(this_set))
            {
                /* 插入成功后再深拷贝：节点负载先清零，恢复拷贝方法要求的
                   “新建节点”初值（与创建节点时的 memset 0 等价） */
                memset(XBTreeNode_GetDataPtr(insertNode), 0, XContainerTypeSize(this_set));
                XSet_copy_data_to_node(this_set, temp, insertNode);
            }
            XSet_release_data_after_copy(this_set, temp);
            XContainer_free(this_set, temp);
        }
        else
        {
            XRBTreeNode* insertNode = XRBTree_create_ex((const char*)pvKey,
                XContainerTypeSize(this_set), XContainer_memory(this_set));
            if (!insertNode) return false;
            if (!XRBTree_insertNode(root_ptr, XContainerCompare(this_set),
                XCompareRuleTwo_XSet, insertNode, XContainer_memory(this_set)))
                return false;
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

    XRBTreeNode* removeNode = XRBTree_removeNode(XSet_root_ptr(this_set), current_node,
        XContainerTypeSize(this_set), XContainer_memory(this_set));
    if (removeNode)
    {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete((XTreeNode*)removeNode, XContainer_memory(this_set));
        --XContainerCapacity(this_set);
        --XContainerSize(this_set);
    }
    if (next) *next = next_it;
}

bool VXSet_remove(XSet* this_set, const void* pvKey)
{
    if (XSet_isEmpty_base(this_set)) return false;
    if (!VXSetDetachIfNeeded(this_set)) return false;

    XRBTreeNode* removeNode = XRBTree_remove(XSet_root_ptr(this_set), XContainerCompare(this_set),
        XCompareRuleOne_XSet, pvKey, XContainerTypeSize(this_set), XContainer_memory(this_set));
    if (removeNode)
    {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete((XTreeNode*)removeNode, XContainer_memory(this_set));
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
    XVector* v = XVector_create_ex(XContainer_memory_type(this_set), XContainerTypeSize(this_set), true);
    if (!v) return NULL;
    for_each_iterator(this_set, XSet, it)
    {
        XVector_push_back_1_base(v, XSet_iterator_data(&it));
    }
    return v;
}

XSet* XSet_create_ex(XMemoryType memory, const size_t keyTypeSize, XCompare compare, bool useCow)
{
    if (keyTypeSize == 0 || compare == NULL) return NULL;
    XSet* this_set = (XSet*)XMemory_malloc(sizeof(XSet), memory);
    if (!this_set) return NULL;
    XSet_init(this_set, keyTypeSize, compare, useCow);
    Set_Class_Memory(this_set, memory); Set_Class_IsHeap(this_set, true);
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
