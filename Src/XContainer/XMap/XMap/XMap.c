#include "XPrintf.h"
#include "XMap.h"
#include "XVariantTypeOps.h"
#if XMap_ON
#include "XString.h"
#include "XVariant.h"
#include "XRedBlackTree.h"
#include "XAlgorithm.h"
#include <stdlib.h>
#include <string.h>

XVARIANT_TYPE_OPS_DEFINE(XMap, sizeof(XMap), XClass_copy_base, XClass_move_base,
	XMap_clear_base, XMap_deinit_base, NULL, "XMap<XString,XVariant>");

XVariant* XMap_toVariant(const XMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, sizeof(XMap), XVariantType_Map);
	if (!var)
		return NULL;
	XMap_init((XMap*)XVariant_data(var), ((const XMapBase*)map)->m_keyTypeSize,
	          XContainerTypeSize(map), XContainerCompare(map), XContainerIsCow(map));
	XCopy(XVariant_data(var), map);
	return var;
}

XVariant* XMap_toVariant_move(XMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, sizeof(XMap), XVariantType_Map);
	if (!var)
		return NULL;
	XMap_init((XMap*)XVariant_data(var), ((const XMapBase*)map)->m_keyTypeSize,
	          XContainerTypeSize(map), XContainerCompare(map), XContainerIsCow(map));
	XMove(XVariant_data(var), map);
	return var;
}

XVariant* XMap_toVariant_ref(XMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, 0, XVariantType_Map);
	if (!var)
		return NULL;
	XVariant_setDataRef(var, map, sizeof(XMap), XVariantType_Map);
	return var;
}

XMap* XMap_fromVariant(const XVariant* var)
{
	return XMap_create_copy(XMap_fromVariant_ref(var));
}

XMap* XMap_fromVariant_ref(const XVariant* var)
{
	return (XMap*)XVariant_toRef(var, XVariantType_Map);
}

static bool XMap_prepareVariant(XVariant* var, const XMap* source)
{
	if (!var || !source)
		return false;
	if (var->m_type != XVariantType_Map)
	{
		XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XMap));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XMap);
		XMap_init((XMap*)var->m_data, ((const XMapBase*)source)->m_keyTypeSize,
		          XContainerTypeSize(source), XContainerCompare(source), XContainerIsCow(source));
		var->m_type = XVariantType_Map;
	}
	else if (!var->m_data || var->m_dataSize != sizeof(XMap))
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XMap));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XMap);
		XMap_init((XMap*)var->m_data, ((const XMapBase*)source)->m_keyTypeSize,
		          XContainerTypeSize(source), XContainerCompare(source), XContainerIsCow(source));
	}
	return true;
}

void XMap_setVariant(XVariant* var, const XMap* map)
{
	if (!XMap_prepareVariant(var, map))
		return;
	XCopy(XVariant_data(var), map);
}

void XMap_setVariant_move(XVariant* var, XMap* map)
{
	if (!XMap_prepareVariant(var, map))
		return;
	XMove(XVariant_data(var), map);
}

void XMap_setVariant_ref(XVariant* var, XMap* map)
{
	if (!var || !map)
		return;
	XVariant_setDataRef(var, map, sizeof(XMap), XVariantType_Map);
}

// 获取根节点指针的地址（统一 COW/非 COW）
static inline XRBTreeNode** XMap_root_ptr(XMap* map) {
    if (XContainerIsCow(map)) {
        return (XRBTreeNode**)XContainerSharedDataPtr(map);
    }
    else {
        return (XRBTreeNode**)&XContainerDataPtr(map);
    }
}

// 获取根节点（用于只读操作）
static inline XRBTreeNode* XMap_root(XMap* map) {
    XRBTreeNode** ptr = XMap_root_ptr(map);
    return ptr ? *ptr : NULL;
}

// COW 分离
static bool VXMapDetachIfNeeded(XMap* this_map);
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

static void XMap_deinit_pair(XMap* map, XPair* pair)
{
    if (!pair) return;
    if (XMapBaseKeyDeinitMethod(map))
        XMapBaseKeyDeinitMethod(map)(XPair_first(pair));
    if (XContainerDataDeinitMethod(map))
        XContainerDataDeinitMethod(map)(XPair_second(pair));
}

static void XMap_release_pair_after_copy(XMap* map, XPair* pair)
{
    if (!pair) return;
    if (XMapBaseKeyCopyMethod(map)) {
        if (XMapBaseKeyDeinitMethod(map))
            XMapBaseKeyDeinitMethod(map)(XPair_first(pair));
    }
    else if (XMapBaseKeyDeinitMethod(map)) {
        memset(XPair_first(pair), 0, ((XMapBase*)map)->m_keyTypeSize);
    }
    if (XContainerDataCopyMethod(map)) {
        if (XContainerDataDeinitMethod(map))
            XContainerDataDeinitMethod(map)(XPair_second(pair));
    }
    else if (XContainerDataDeinitMethod(map)) {
        memset(XPair_second(pair), 0, XContainerTypeSize(map));
    }
}

static void XMap_copy_pair_to_node(XMap* map, XPair* source, XRBTreeNode* node)
{
    XPair* target = XBTreeNode_GetDataPtr(node);
    XPair_init(target, source->m_firstTypeSize, source->m_secondTypeSize);
    if (XMapBaseKeyCopyMethod(map))
        XMapBaseKeyCopyMethod(map)(XPair_first(target), XPair_first(source));
    else
        memcpy(XPair_first(target), XPair_first(source), source->m_firstTypeSize);
    if (XContainerDataCopyMethod(map))
        XContainerDataCopyMethod(map)(XPair_second(target), XPair_second(source));
    else
        memcpy(XPair_second(target), XPair_second(source), source->m_secondTypeSize);
}

static XPair* XMap_pair_buffer(XMap* this_map)
{
    XPair* pair = XMapBasePairBuffer(this_map);
    if (!pair) {
        pair = (XPair*)XContainer_malloc(this_map,
            XMapBasePairTypeSize(this_map));
        if (pair) {
            XPair_init(pair, ((XMapBase*)this_map)->m_keyTypeSize,
                XContainerTypeSize(this_map));
            XMapBasePairBuffer(this_map) = pair;
        }
    }
    return pair;
}
static void VXMap_deinit(XMap* this_map);

XVtable* XMap_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XMAP_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMap");
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
    XCLASS_SHOW_SIZE_DEFAULT(XMap);
    return XVTABLE_DEFAULT;
}

// COW 分离核心函数
static bool VXMapDetachIfNeeded(XMap* this_map)
{
    if (!XContainerIsCow(this_map)) return true;  // 非 COW 不需要分离

    XSharedData* sd = (XSharedData*)XContainerDataPtr(this_map);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t typeSize = XContainerTypeSize(this_map);
    size_t keyTypeSize = ((XMapBase*)this_map)->m_keyTypeSize;
    XRBTreeNode* oldRoot = *XMap_root_ptr(this_map);
    if (oldRoot == NULL) return true;

    XVector* nodes = XVector_create_ex(XContainer_memory_type(this_map), sizeof(XRBTreeNode*), true);
    if (!nodes) return false;
    XBTree_TraversingToXVector(oldRoot, XBTreePreorder, nodes);

    XRBTreeNode* newRoot = NULL;
    bool success = true;

    for (size_t i = 0; i < XVector_size_base(nodes); i++) {
        XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
        XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);
        XRBTreeNode* newNode = XRBTree_create_ex(NULL, XMapBasePairTypeSize(this_map), XContainer_memory(this_map));
        if (!newNode) {
            success = false;
            break;
        }
        XPair* newPair = XBTreeNode_GetDataPtr(newNode);
        XPair_init(newPair, oldPair->m_firstTypeSize, oldPair->m_secondTypeSize);
        if (XMapBaseKeyCopyMethod(this_map))
            XMapBaseKeyCopyMethod(this_map)(XPair_first(newPair), XPair_first(oldPair));
        else
            memcpy(XPair_first(newPair), XPair_first(oldPair), keyTypeSize);
        if (XContainerDataCopyMethod(this_map))
            XContainerDataCopyMethod(this_map)(XPair_second(newPair), XPair_second(oldPair));
        else
            memcpy(XPair_second(newPair), XPair_second(oldPair), typeSize);
        XRBTree_SetRed(newNode);
        memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
        ((XTreeNode*)newNode)->parentNode = NULL;
        XRBTree_insertNode(&newRoot, XContainerCompare(this_map), XCompareRuleTwo_XMap, newNode, XContainer_memory(this_map));
    }
    XVector_delete_base(nodes);

    if (!success) {
        XTree_delete(newRoot, XMapBase_deleteNodeData, this_map, XContainer_memory(this_map));
        return false;
    }

    XSharedData* newShared = XSharedData_create_ex(NULL, sizeof(XRBTreeNode*), XContainer_memory(this_map));
    if (!newShared) {
        XTree_delete(newRoot, XMapBase_deleteNodeData, this_map, XContainer_memory(this_map));
        return false;
    }
    *(XRBTreeNode**)newShared->data = newRoot;
    XSharedData_release(sd, XContainer_memory(this_map));
    XContainerSetDataPtr(this_map, newShared);
    return true;
}

static void VXMapDataDelete(void* data, XMap* this_map)
{
    if (data == NULL || this_map == NULL) return;
    XRBTreeNode* root = *(XRBTreeNode**)data;
    if (root)
        XTree_delete(root, XMapBase_deleteNodeData, this_map, XContainer_memory(this_map));
    XContainerSize(this_map) = 0;
    XContainerCapacity(this_map) = 0;
    XContainerSetDataPtr(this_map, NULL);
}

bool VXMap_insert(XMap* this_map, const void* pvKey, const void* pvValue,
    XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
    if (!VXMapDetachIfNeeded(this_map)) return false;

    XMap_iterator it;
    XPair* pair = NULL;

    if (!XMap_find_base(this_map, pvKey, &it)) {
        pair = XMap_pair_buffer(this_map);
        if (!pair) return false;
        if (keyCreatMethod)
            keyCreatMethod(XPair_first(pair), pvKey);
        else
            XPair_insertFirst(pair, pvKey);
        if (dataCreatMethod)
            dataCreatMethod(XPair_second(pair), pvValue);
        else
            XPair_insertSecond(pair, pvValue);

        if (XContainerIsCow(this_map)) {
            if (!(XSharedData*)XContainerDataPtr(this_map)) {
                XContainerSetDataPtr(this_map, XSharedData_create_ex(NULL, sizeof(XRBTreeNode*), XContainer_memory(this_map)));
            }
        }
        else {
            // 非 COW 模式：确保 XContainerDataPtr 初始为 NULL
            if (!XContainerDataPtr(this_map)) {
                XContainerDataPtr(this_map) = NULL;
            }
        }

        XRBTreeNode* insertNode = XRBTree_create_ex(NULL,
            XMapBasePairTypeSize(this_map), XContainer_memory(this_map));
        if (!insertNode) {
            XMap_deinit_pair(this_map, pair);
            return false;
        }
        XMap_copy_pair_to_node(this_map, pair, insertNode);
        XRBTreeNode* inserted = XRBTree_insertNode(XMap_root_ptr(this_map),
            XContainerCompare(this_map), XCompareRuleTwo_XMap, insertNode,
            XContainer_memory(this_map));
        XMap_release_pair_after_copy(this_map, pair);
        if (!inserted) {
            return false;
        }
        ++XContainerCapacity(this_map);
        ++XContainerSize(this_map);
    }
    else {
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

void VXMap_erase(XMap* this_map, const XMap_iterator* it, XMap_iterator* next)
{
    if (ISNULL(this_map, "") || ISNULL(it, "") || XMap_iterator_isEnd((XMap_iterator*)it)) {
        if (next) *next = XMap_end(this_map);
        return;
    }
    // 提取键并分离（参照 XSet 的实现）
    XPair* oldPair = XMap_iterator_data(it);
    if (!oldPair) {
        if (next) *next = XMap_end(this_map);
        return;
    }
    size_t keySize = ((XMapBase*)this_map)->m_keyTypeSize;
    void* keyBuffer = XContainer_malloc(this_map, keySize);
    if (!keyBuffer) {
        if (next) *next = XMap_end(this_map);
        return;
    }
    if (XMapBaseKeyCopyMethod(this_map))
        XMapBaseKeyCopyMethod(this_map)(keyBuffer, XPair_first(oldPair));
    else
        memcpy(keyBuffer, XPair_first(oldPair), keySize);
    if (!VXMapDetachIfNeeded(this_map)) {
        XContainer_free(this_map, keyBuffer);
        if (next) *next = XMap_end(this_map);
        return;
    }
    XRBTreeNode* toRemove = XRBTree_remove(XMap_root_ptr(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap,
        keyBuffer, XMapBasePairTypeSize(this_map), XContainer_memory(this_map));
    XContainer_free(this_map, keyBuffer);
    if (toRemove) {
        if (next) *next = XMap_end(this_map);
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(toRemove), this_map);
        XRBTreeNode_delete((XTreeNode*)toRemove, XContainer_memory(this_map));
        --XContainerCapacity(this_map);
        --XContainerSize(this_map);
    }
    else {
        if (next) *next = XMap_end(this_map);
    }
}

bool VXMap_remove(XMap* this_map, const void* key)
{
    if (ISNULL(this_map, "") || ISNULL(key, "")) return false;
    if (!VXMapDetachIfNeeded(this_map)) return false;
    XRBTreeNode* toRemove = XRBTree_remove(XMap_root_ptr(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap,
        key, XMapBasePairTypeSize(this_map), XContainer_memory(this_map));
    if (toRemove) {
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(toRemove), this_map);
        XRBTreeNode_delete((XTreeNode*)toRemove, XContainer_memory(this_map));
        --XContainerCapacity(this_map);
        --XContainerSize(this_map);
        return true;
    }
    return false;
}

void* VXMap_value(XMap* this_map, const void* key)
{
    if (ISNULL(this_map, "") || ISNULL(key, "")) return NULL;
    XMap_iterator it;
    if (XMap_find_base(this_map, key, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return XPair_second(pair);
    }
    return NULL;
}

bool VXMap_find(XMap* this_map, const void* key, XMap_iterator* it)
{
    if (XMap_isEmpty_base(this_map)) {
        if (it) *it = XMap_end(this_map);
        return false;
    }
    XRBTreeNode* node = XRBTree_findNode(XMap_root(this_map),
        XContainerCompare(this_map), XCompareRuleOne_XMap, key);
    if (!node) {
        if (it) *it = XMap_end(this_map);
        return false;
    }
    if (it) it->node = node;
    return true;
}

XVector* VXMapBase_keys(const XMapBase* this_map)
{
    XVector* v = XVector_create_ex(XContainer_memory_type(this_map), this_map->m_keyTypeSize, true);
    XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_map));
    XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_map));
    XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_map));
    for_each_iterator(this_map, XMap, it) {
        XVector_push_back_1_base(v, XPair_first(XMap_iterator_data(&it)));
    }
    return v;
}

XVector* VXMapBase_values(const XMapBase* this_map)
{
    XVector* v = XVector_create_ex(XContainer_memory_type(this_map), XContainerTypeSize(this_map), true);
    XContainerSetDataCopyMethod(v, XContainerDataCopyMethod(this_map));
    XContainerSetDataMoveMethod(v, XContainerDataMoveMethod(this_map));
    XContainerSetDataDeinitMethod(v, XContainerDataDeinitMethod(this_map));
    for_each_iterator(this_map, XMap, it) {
        XVector_push_back_1_base(v, XPair_second(XMap_iterator_data(&it)));
    }
    return v;
}

void VXMap_clear(XMap* this_map)
{
    if (XMap_isEmpty_base(this_map)) return;
    if (XContainerIsCow(this_map) && (XSharedData*)XContainerDataPtr(this_map) && XSharedData_isShared((XSharedData*)XContainerDataPtr(this_map))) {
        XSharedData_release((XSharedData*)XContainerDataPtr(this_map),
            XContainer_memory(this_map));
        XContainerSetDataPtr(this_map, NULL);
        XContainerCapacity(this_map) = 0;
        XContainerSize(this_map) = 0;
        return;
    }
    XRBTreeNode* root = XMap_root(this_map);
    if (root)
    XTree_delete(root, XMapBase_deleteNodeData, this_map, XContainer_memory(this_map));
    if (XContainerIsCow(this_map))
        *XMap_root_ptr(this_map) = NULL;
    else
        XContainerDataPtr(this_map) = NULL;
    XContainerCapacity(this_map) = 0;
    XContainerSize(this_map) = 0;
}

void VXClass_copy(XMap* object, const XMap* src)
{
    bool target_uninitialized = XClassIsVtableNull(object);
    if (target_uninitialized) {
        XMap_init(object, ((XMapBase*)src)->m_keyTypeSize, XContainerTypeSize(src), XContainerCompare(src), XContainerIsCow(src));
        Class_Memory(object) = Class_Memory(src);
    }
    else if (XContainerIsCow(src) &&
        XContainer_memory(object) != XContainer_memory(src)) {
        return;
    }
    else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object),
                    VXMapDataDelete, object, XContainer_memory(object));
        }
        else {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
                XTree_delete(root, XMapBase_deleteNodeData, object, XContainer_memory(object));
            XContainerDataPtr(object) = NULL;
        }
    }

    XMapBaseSetKeyCopyMethod(object, XMapBaseKeyCopyMethod(src));
    XMapBaseSetKeyMoveMethod(object, XMapBaseKeyMoveMethod(src));
    XMapBaseSetKeyDeinitMethod(object, XMapBaseKeyDeinitMethod(src));
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    if (XContainerIsCow(src)) {
        XContainerSetDataPtr(object, (XSharedData*)XContainerDataPtr(src));
        if ((XSharedData*)XContainerDataPtr(object))
            XSharedData_addRef((XSharedData*)XContainerDataPtr(object));
        // 修复：复制大小和容量
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = XContainerCapacity(src);
    }
    else {
        // 非 COW：深拷贝红黑树
        XRBTreeNode* srcRoot = (XRBTreeNode*)XContainerDataPtr(src);
        if (srcRoot) {
            size_t typeSize = XContainerTypeSize(src);
            size_t keySize = ((XMapBase*)src)->m_keyTypeSize;
            XRBTreeNode* newRoot = NULL;
            XVector* nodes = XVector_create_ex(XContainer_memory_type(object), sizeof(XRBTreeNode*), true);
            if (!nodes) return;
            XBTree_TraversingToXVector(srcRoot, XBTreePreorder, nodes);
            for (size_t i = 0; i < XVector_size_base(nodes); i++) {
                XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[i];
                XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);
                XRBTreeNode* newNode = XRBTree_create_ex(NULL, XMapBasePairTypeSize(object), XContainer_memory(object));
                if (!newNode) {
                    XVector_delete_base(nodes);
                    XTree_delete(newRoot, XMapBase_deleteNodeData, object, XContainer_memory(object));
                    return;
                }
                XPair* newPair = XBTreeNode_GetDataPtr(newNode);
                XPair_init(newPair, keySize, typeSize);
                if (XMapBaseKeyCopyMethod(object))
                    XMapBaseKeyCopyMethod(object)(XPair_first(newPair), XPair_first(oldPair));
                else
                    memcpy(XPair_first(newPair), XPair_first(oldPair), keySize);
                if (XContainerDataCopyMethod(object))
                    XContainerDataCopyMethod(object)(XPair_second(newPair), XPair_second(oldPair));
                else
                    memcpy(XPair_second(newPair), XPair_second(oldPair), typeSize);
                XRBTree_SetRed(newNode);
                memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
                ((XTreeNode*)newNode)->parentNode = NULL;
                XRBTree_insertNode(&newRoot, XContainerCompare(object), XCompareRuleTwo_XMap, newNode, XContainer_memory(object));
            }
            XVector_delete_base(nodes);
            XContainerDataPtr(object) = newRoot;
        }
        else {
            XContainerDataPtr(object) = NULL;
        }
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = XContainerCapacity(src);
    }
}

void VXClass_move(XMap* object, XMap* src)
{
    XMemory* source_memory = Class_Memory(src);
    bool target_uninitialized = XClassIsVtableNull(object);
    XMemory* target_memory = target_uninitialized ? NULL : Class_Memory(object);
    // 1. 如果目标未初始化，先初始化（模式与源相同）
    if (target_uninitialized) {
        XMap_init(object, ((XMapBase*)src)->m_keyTypeSize, XContainerTypeSize(src),
            XContainerCompare(src), XContainerIsCow(src));
        // 注意：初始化后直接转移资源？不，下面会交换，所以初始化是必要的
    }
    else {
        // 2. 释放目标原有资源
        if (XContainerIsCow(object)) {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object),
                    VXMapDataDelete, object, XContainer_memory(object));
        }
        else {
            XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(object);
            if (root)
        XTree_delete(root, XMapBase_deleteNodeData, object, XContainer_memory(object));
            XContainerDataPtr(object) = NULL;
        }
        // 3. 清空目标的大小和容量（已经由释放函数完成？不，需要显式清零）
        XContainerSize(object) = 0;
        XContainerCapacity(object) = 0;
        // 数据指针已在上面置 NULL，无需再置
    }

    // 4. 交换目标与源的内存（跳过 XClass 部分，包括 m_useCow、m_data、size、capacity 等）
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XMap) - sizeof(XClass));
    Class_Memory(object) = source_memory;
    if (!target_uninitialized)
        Class_Memory(src) = target_memory;
}

void VXMap_deinit(XMap* this_map)
{
    if (XContainerIsCow(this_map)) {
        if ((XSharedData*)XContainerDataPtr(this_map))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(this_map),
                VXMapDataDelete, this_map, XContainer_memory(this_map));
    }
    else {
        XRBTreeNode* root = (XRBTreeNode*)XContainerDataPtr(this_map);
        if (root)
            XTree_delete(root, XMapBase_deleteNodeData, this_map, XContainer_memory(this_map));
        XContainerDataPtr(this_map) = NULL;
    }
    XContainerSize(this_map) = 0;
    XContainerCapacity(this_map) = 0;
    if (XMapBasePairBuffer(this_map)) {
        XContainer_free(this_map, XMapBasePairBuffer(this_map));
        XMapBasePairBuffer(this_map) = NULL;
    }
}


XMap* XMap_create_ex(XMemoryType memory, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare, bool useCow)
{
    if (keyTypeSize == 0 || valTypeSize == 0)
    {
        XPrintf("类型参数不能为0");
        return NULL;
    }
    if (compare == NULL)
    {
        XPrintf("compare比较函数NULL");
        return NULL;
    }
    XMap* this_map = (XMap*)XMemory_malloc(sizeof(XMap), memory);
    XMap_init(this_map, keyTypeSize, valTypeSize, compare, useCow);
    Set_Class_Memory(this_map, memory); Set_Class_IsHeap(this_map, true);
    return this_map;
}
XMap* XMap_create_copy(const XMap* other)
{
    if (other == NULL) return NULL;
    XMemoryType memory = XContainerIsCow(other) ? XContainer_memory_type(other) : XCLASS_DEFAULT_MEMORY_TYPE;
    XMap* map = XMap_create_ex(memory, ((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), XContainerCompare(other), XContainerIsCow(other));
    if (map == NULL) return NULL;
    XCopy(map, other);
    return map;
}
XMap* XMap_create_move(XMap* other)
{
    if (other == NULL) return NULL;
    XMap* map = XMap_create_ex(XContainer_memory_type(other), ((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), XContainerCompare(other), XContainerIsCow(other));
    if (map == NULL) return NULL;
    XMove(map, other);
    return map;
}
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare, bool useCow)
{
    if (ISNULL(this_map, ""))
        return;
    if (keyTypeSize == 0 || valTypeSize == 0)
    {
        XPrintf("类型参数不能为0");
        return;
    }
    if (compare == NULL)
    {
        XPrintf("compare比较函数NULL");
        return;
    }
    XMapBase_init(this_map, keyTypeSize, valTypeSize, compare, useCow);
    XClassSetVtable(this_map, XMap);
    //this_map->m_KeyLess = KeyLess;
}

void XMap_detach(XMap* this_map)
{
    if (ISNULL(this_map, ""))
        return;
    VXMapDetachIfNeeded(this_map);
}

bool XMap_isDetached(const XMap* this_map)
{
    XSharedData* shared;
    if (this_map == NULL || !XContainerIsCow(this_map))
        return true;
    shared = (XSharedData*)XContainerDataPtr(this_map);
    return shared == NULL || !XSharedData_isShared(shared);
}

XVariantMap* XMap_create_XVariantMap()
{
    XMap* map = XMap_Create(XString, XVariant, XString_compare);
    if (map == NULL)
        return NULL;
    /*XContainerSetDataCopyMethod(map, XMapBase_XVariantMapCopyMethod);
    XContainerSetDataMoveMethod(map, XMapBase_XVariantMapMoveMethod);
    XContainerSetDataDeinitMethod(map, XMapBase_XVariantMapDeinitMethod);*/

    XMapBaseSetKeyCopyMethod(map, XClass_copy_base);
    XMapBaseSetKeyMoveMethod(map, XClass_move_base);
    XMapBaseSetKeyDeinitMethod(map, XString_deinit_base);

    XContainerSetDataCopyMethod(map, XClass_copy_base);
    XContainerSetDataMoveMethod(map, XClass_move_base);
    XContainerSetDataDeinitMethod(map, XVariant_deinit_base);

    return map;
}
#endif
