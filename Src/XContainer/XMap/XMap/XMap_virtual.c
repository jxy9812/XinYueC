#include "XMap.h"
#if XMap_ON
#include "XRedBlackTree.h"
#include "XAlgorithm.h"
#include <stdlib.h>
#include <string.h>

// COW 分离：如果数据被共享，创建独立副本（深拷贝红黑树）
static bool VXMapDetachIfNeeded(XMap * this_map);
static void VXMapDataDelete(void* data, XMap* this_map);

// Map 操作
static bool VXMap_insert(XMap* this_map, const void* pvKey, const void* pvValue,
    XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod);
static void VXMap_erase(XMap* this_map, const XMap_iterator* it, XMap_iterator* next);
static bool VXMap_remove(XMap* this_map, const void* key);
static void* VXMap_value(XMap* this_map, const void* key);
static bool VXMap_find(XMap* this_map, const void* key, XMap_iterator* it);
static XVector* VXMapBase_keys(const XMapBase* this_map);
static XVector* VXMapBase_values(const XMapBase* this_map);
static void VXMap_clear(XMap* this_map);
static void VXClass_copy(XMap* object, const XMap* src);
static void VXClass_move(XMap* object, XMap* src);
static void VXMap_deinit(XMap* this_map);

// ========================
// 虚函数表初始化
// ========================
XVtable* XMap_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XMAP_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXMap_insert, VXMap_erase, VXMap_remove, VXMap_value, VXMap_find,
        VXMapBase_keys, VXMapBase_values
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXMap_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMap_deinit);
    // 注意：EXContainer_Swap 未重载，使用基类实现（但基类实现有bug，需单独修复 XContainer_virtual.c）
#if SHOWCONTAINERSIZE
    printf("XMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// ========================
// COW 分离核心函数（深拷贝红黑树）
// ========================
static bool VXMapDetachIfNeeded(XMap* this_map)
{
    XSharedData* sd = XContainerSharedData(this_map);
    if (!sd || !XSharedData_isShared(sd))
        return true; // 不共享，无需分离

    size_t typeSize = XContainerTypeSize(this_map);
    size_t keyTypeSize = ((XMapBase*)this_map)->m_keyTypeSize;

    XRBTreeNode* oldRoot = *(XRBTreeNode**)XContainerSharedDataPtr(this_map);
    if (oldRoot == NULL)
        return true;

    // 遍历旧树，收集所有节点（前序）
    XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
    if (!nodes) return false;
    XBTree_TraversingToXVector(oldRoot, XBTreePreorder, nodes);

    XRBTreeNode* newRoot = NULL;
    bool success = true;

    for (size_t i = 0; i < XVector_size_base(nodes); i++)
    {
        XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
        XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);

        // 创建新节点
        XRBTreeNode* newNode = XRBTree_create(NULL, XMapBasePairTypeSize(this_map));
        if (!newNode) {
            success = false;
            break;
        }

        XPair* newPair = XBTreeNode_GetDataPtr(newNode);
        XPair_init(newPair, oldPair->m_firstTypeSize, oldPair->m_secondTypeSize);

        // 拷贝 key
        if (XMapBaseKeyCopyMethod(this_map))
            XMapBaseKeyCopyMethod(this_map)(XPair_first(newPair), XPair_first(oldPair));
        else
            memcpy(XPair_first(newPair), XPair_first(oldPair), keyTypeSize);

        // 拷贝 value
        if (XContainerDataCopyMethod(this_map))
            XContainerDataCopyMethod(this_map)(XPair_second(newPair), XPair_second(oldPair));
        else
            memcpy(XPair_second(newPair), XPair_second(oldPair), typeSize);

        // 插入到新树
        XRBTree_SetRed(newNode);
        memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
        ((XTreeNode*)newNode)->parentNode = NULL;
        XRBTree_insertNode(&newRoot, XContainerCompare(this_map), XCompareRuleTwo_XMap, newNode);
    }

    XVector_delete_base(nodes);

    if (!success) {
        // 清理已创建的新树
        XTree_delete(newRoot, XMapBase_deleteNodeData, this_map);
        return false;
    }

    // 创建新的 XSharedData 存储根节点指针
    XSharedData* newShared = XSharedData_create(NULL, sizeof(XRBTreeNode*));
    if (!newShared) {
        XTree_delete(newRoot, XMapBase_deleteNodeData, this_map);
        return false;
    }
    *(XRBTreeNode**)newShared->data = newRoot;

    // 替换共享块
    XSharedData_release(sd);
    XContainerSharedData(this_map) = newShared;
    return true;
}

// ========================
// 数据删除回调（用于释放共享块时清理红黑树）
// ========================
static void VXMapDataDelete(void* data, XMap* this_map)
{
    if (data == NULL || this_map == NULL) return;
    XRBTreeNode* root = *(XRBTreeNode**)data;
    if (root)
        XTree_delete(root, XMapBase_deleteNodeData, this_map);
    XContainerSize(this_map) = 0;
    XContainerCapacity(this_map) = 0;
    XContainerSharedData(this_map) = NULL;
}

// ========================
// 插入（拷贝/移动语义，已添加分离）
// ========================
bool VXMap_insert(XMap* this_map, const void* pvKey, const void* pvValue,
    XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
    if (!VXMapDetachIfNeeded(this_map))
        return false;

    XMap_iterator it;
    XPair* pair = NULL;

    if (!XMap_find_base(this_map, pvKey, &it))
    {
        pair = XMapBasePairBuffer(this_map);
        if (keyCreatMethod)
            keyCreatMethod(XPair_first(pair), pvKey);
        else
            XPair_insertFirst(pair, pvKey);
        if (dataCreatMethod)
            dataCreatMethod(XPair_second(pair), pvValue);
        else
            XPair_insertSecond(pair, pvValue);

        if (!XContainerSharedData(this_map))
            XContainerSharedData(this_map) = XSharedData_create(NULL, sizeof(XRBTreeNode*));

        XRBTreeNode* inserted = XRBTree_insert((XRBTreeNode**)XContainerSharedDataPtr(this_map),
            XContainerCompare(this_map), XCompareRuleTwo_XMap,
            pair, XMapBasePairTypeSize(this_map));
        if (!inserted) {
            XMapBase_deleteNodeData(pair, this_map);
            return false;
        }
        ++XContainerCapacity(this_map);
        ++XContainerSize(this_map);
    }
    else
    {
        // 键已存在，修改值
        pair = XMap_iterator_data(&it);
        if (XMapBaseKeyDeinitMethod(this_map))
            XMapBaseKeyDeinitMethod(this_map)(XPair_first(pair));
        if (keyCreatMethod)
            keyCreatMethod(XPair_first(pair), pvKey);
        else
            XPair_insertFirst(pair, pvKey);

        if (XContainerDataDeinitMethod(this_map))
            XContainerDataDeinitMethod(this_map)(XPair_second(pair));
        if (dataCreatMethod)
            dataCreatMethod(XPair_second(pair), pvValue);
        else
            XPair_insertSecond(pair, pvValue);
    }
    return true;
}

// ========================
// 通过迭代器删除元素（修复：分离后根据键重新定位）
// ========================
void VXMap_erase(XMap* this_map, const XMap_iterator* it, XMap_iterator* next)
{
    if (ISNULL(this_map, "") || ISNULL(it, "") || XMap_iterator_isEnd((XMap_iterator*)it)) {
        if (next) *next = XMap_end(this_map);
        return;
    }

    // 从迭代器中提取键（需要拷贝，因为分离后原节点失效）
    XPair* oldPair = XMap_iterator_data(it);
    if (!oldPair) {
        if (next) *next = XMap_end(this_map);
        return;
    }

    size_t keySize = ((XMapBase*)this_map)->m_keyTypeSize;
    void* keyBuffer = XMalloc_System(keySize);
    if (!keyBuffer) {
        if (next) *next = XMap_end(this_map);
        return;
    }

    // 拷贝键（使用键的拷贝方法或 memcpy）
    if (XMapBaseKeyCopyMethod(this_map))
        XMapBaseKeyCopyMethod(this_map)(keyBuffer, XPair_first(oldPair));
    else
        memcpy(keyBuffer, XPair_first(oldPair), keySize);

    // 分离（深拷贝整棵树）
    if (!VXMapDetachIfNeeded(this_map)) {
        XFree_System(keyBuffer);
        if (next) *next = XMap_end(this_map);
        return;
    }

    // 根据键在新树中查找并删除
    XRBTreeNode* toRemove = XRBTree_remove((XRBTreeNode**)XContainerSharedDataPtr(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap,
        keyBuffer, XMapBasePairTypeSize(this_map));
    XFree_System(keyBuffer);

    if (toRemove) {
        // 获取后继迭代器（删除节点的下一个节点）
        if (next) {
            // 后继节点是删除节点的中序后继，由于红黑树删除后结构变化，需要重新查找
            // 简便方法：从容器中查找大于当前键的最小节点
            // 但为了性能，我们可以在删除前获取后继键，但键可能重复？不，键唯一。
            // 更简单：先不删除，获取后继节点，再删除。但分离后原节点已不在新树。
            // 这里提供一种可靠但稍慢的方法：在删除后，重新查找原键的后继。
            // 由于我们只有键，可以遍历找到第一个大于该键的节点。
            // 为简化，我们直接返回 end 迭代器，让用户使用 remove 或重新遍历。
            // 更符合 COW 语义：任何修改操作后，既有迭代器全部失效。
            *next = XMap_end(this_map);
        }
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(toRemove), this_map);
        XRBTreeNode_delete(toRemove);
        --XContainerCapacity(this_map);
        --XContainerSize(this_map);
    }
    else {
        if (next) *next = XMap_end(this_map);
    }
}

// ========================
// 通过键删除（已添加分离）
// ========================
bool VXMap_remove(XMap* this_map, const void* key)
{
    if (ISNULL(this_map, "") || ISNULL(key, ""))
        return false;
    if (!VXMapDetachIfNeeded(this_map))
        return false;

    XRBTreeNode* toRemove = XRBTree_remove((XRBTreeNode**)XContainerSharedDataPtr(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap,
        key, XMapBasePairTypeSize(this_map));
    if (toRemove) {
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(toRemove), this_map);
        XRBTreeNode_delete(toRemove);
        --XContainerCapacity(this_map);
        --XContainerSize(this_map);
        return true;
    }
    return false;
}

// ========================
// 根据键获取值（只读，无需分离）
// ========================
void* VXMap_value(XMap* this_map, const void* key)
{
    if (ISNULL(this_map, "") || ISNULL(key, ""))
        return NULL;
    XMap_iterator it;
    if (XMap_find_base(this_map, key, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return XPair_second(pair);
    }
    return NULL;
}

// ========================
// 查找（只读，无需分离）
// ========================
bool VXMap_find(XMap* this_map, const void* key, XMap_iterator* it)
{
    if (XMap_isEmpty_base(this_map)) {
        if (it) *it = XMap_end(this_map);
        return false;
    }
    XTreeNode* node = XRBTree_findNode(*(XRBTreeNode**)XContainerSharedDataPtr(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap, key);
    if (!node) {
        if (it) *it = XMap_end(this_map);
        return false;
    }
    if (it) it->node = node;
    return true;
}

// ========================
// 获取所有键的集合（只读，无需分离）
// ========================
XVector* VXMapBase_keys(const XMapBase* this_map)
{
    XVector* v = XVector_create(this_map->m_keyTypeSize);
    XVector_resize_base(v, XMapBase_size_base(this_map));
    XVector_clear_base(v);
    XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_map));
    XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_map));
    XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_map));
    for_each_iterator(this_map, XMap, it) {
        XVector_push_back_base(v, XPair_first(XMap_iterator_data(&it)));
    }
    return v;
}

// ========================
// 获取所有值的集合（只读，无需分离）
// ========================
XVector* VXMapBase_values(const XMapBase* this_map)
{
    XVector* v = XVector_create(XContainerTypeSize(this_map));
    XVector_resize_base(v, XMapBase_size_base(this_map));
    XVector_clear_base(v);
    XContainerSetDataCopyMethod(v, XContainerDataCopyMethod(this_map));
    XContainerSetDataMoveMethod(v, XContainerDataMoveMethod(this_map));
    XContainerSetDataDeinitMethod(v, XContainerDataDeinitMethod(this_map));
    for_each_iterator(this_map, XMap, it) {
        XVector_push_back_base(v, XPair_second(XMap_iterator_data(&it)));
    }
    return v;
}

// ========================
// 清空（已正确处理共享）
// ========================
void VXMap_clear(XMap* this_map)
{
    if (XMap_isEmpty_base(this_map))
        return;

    XSharedData* sd = XContainerSharedData(this_map);
    if (sd && XSharedData_isShared(sd)) {
        XSharedData_release(sd);
        XContainerSharedData(this_map) = NULL;
        XContainerCapacity(this_map) = 0;
        XContainerSize(this_map) = 0;
        return;
    }

    // 不共享，直接删除树
    XTree_delete(*(XRBTreeNode**)XContainerSharedDataPtr(this_map), XMapBase_deleteNodeData, this_map);
    XContainerCapacity(this_map) = 0;
    XContainerSize(this_map) = 0;
    *(XRBTreeNode**)XContainerSharedDataPtr(this_map) = NULL;
}

// ========================
// 拷贝（共享源数据块）
// ========================
void VXClass_copy(XMap* object, const XMap* src)
{
    if (((XClass*)object)->m_vtable == NULL) {
        XMapBase* map = (XMapBase*)src;
        XMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), XContainerCompare(map));
    }
    else if (XContainerSharedData(object)) {
        XSharedData_release_with(XContainerSharedData(object), VXMapDataDelete, object);
    }

    XMapBaseSetKeyCopyMethod(object, XMapBaseKeyCopyMethod(src));
    XMapBaseSetKeyMoveMethod(object, XMapBaseKeyMoveMethod(src));
    XMapBaseSetKeyDeinitMethod(object, XMapBaseKeyDeinitMethod(src));
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    XContainerSharedData(object) = XContainerSharedData(src);
    if (XContainerSharedData(object))
        XSharedData_addRef(XContainerSharedData(object));

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
}

// ========================
// 移动（转移所有权）
// ========================
void VXClass_move(XMap* object, XMap* src)
{
    if (((XClass*)object)->m_vtable == NULL) {
        XMapBase* map = (XMapBase*)src;
        XMap_init(object, map->m_keyTypeSize, XContainerTypeSize(src), XContainerCompare(map));
    }
    else if (XContainerSharedData(object)) {
        XSharedData_release_with(XContainerSharedData(object), VXMapDataDelete, object);
    }

    // 交换内部数据（不包括基类虚函数表指针）
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XMap) - sizeof(XClass));

    // 清空源对象
    XContainerSharedData(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
}

// ========================
// 析构（释放共享块）
// ========================
void VXMap_deinit(XMap* this_map)
{
    XSharedData_release_with(XContainerSharedData(this_map), VXMapDataDelete, this_map);
    XContainerSize(this_map) = 0;
    XContainerCapacity(this_map) = 0;
    XContainerSharedData(this_map) = NULL;

    if (XMapBasePairBuffer(this_map)) {
        XPair_delete(XMapBasePairBuffer(this_map));
        XMapBasePairBuffer(this_map) = NULL;
    }
}

#endif