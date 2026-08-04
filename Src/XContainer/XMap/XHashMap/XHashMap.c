#include "XHashMap.h"
#include "XVariantTypeOps.h"
#if XHashMap_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include "XString.h"
#include "XVariant.h"
#include "XRedBlackTree.h"
#include <string.h>

XVARIANT_TYPE_OPS_DEFINE(XHashMap, sizeof(XHashMap), XHashMap_copy_base,
	XHashMap_move_base, XHashMap_clear_base, XHashMap_deinit_base,
	NULL, "XHashMap<XString,XVariant>");

XVariant* XHashMap_toVariant(const XHashMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, sizeof(XHashMap), XVariantType_Hash);
	if (!var)
		return NULL;
	XHashMap_init((XHashMap*)XVariant_data(var), ((const XMapBase*)map)->m_keyTypeSize,
	              XContainerTypeSize(map), map->m_hash, XContainerCompare(map), XContainerIsCow(map));
	XHashMap_copy_base(XVariant_data(var), map);
	return var;
}

XVariant* XHashMap_toVariant_move(XHashMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, sizeof(XHashMap), XVariantType_Hash);
	if (!var)
		return NULL;
	XHashMap_init((XHashMap*)XVariant_data(var), ((const XMapBase*)map)->m_keyTypeSize,
	              XContainerTypeSize(map), map->m_hash, XContainerCompare(map), XContainerIsCow(map));
	XHashMap_move_base(XVariant_data(var), map);
	return var;
}

XVariant* XHashMap_toVariant_ref(XHashMap* map)
{
	XVariant* var;
	if (!map)
		return NULL;
	var = XVariant_create(NULL, 0, XVariantType_Hash);
	if (!var)
		return NULL;
	XVariant_setDataRef(var, map, sizeof(XHashMap), XVariantType_Hash);
	return var;
}

XHashMap* XHashMap_fromVariant(const XVariant* var)
{
	return XHashMap_create_copy(XHashMap_fromVariant_ref(var));
}

XHashMap* XHashMap_fromVariant_ref(const XVariant* var)
{
	return (XHashMap*)XVariant_toRef(var, XVariantType_Hash);
}

static bool XHashMap_prepareVariant(XVariant* var, const XHashMap* source)
{
	if (!var || !source)
		return false;
	if (var->m_type != XVariantType_Hash)
	{
		XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XHashMap));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XHashMap);
		XHashMap_init((XHashMap*)var->m_data, ((const XMapBase*)source)->m_keyTypeSize,
		              XContainerTypeSize(source), source->m_hash,
		              XContainerCompare(source), XContainerIsCow(source));
		var->m_type = XVariantType_Hash;
	}
	else if (!var->m_data || var->m_dataSize != sizeof(XHashMap))
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XHashMap));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XHashMap);
		XHashMap_init((XHashMap*)var->m_data, ((const XMapBase*)source)->m_keyTypeSize,
		              XContainerTypeSize(source), source->m_hash,
		              XContainerCompare(source), XContainerIsCow(source));
	}
	return true;
}

void XHashMap_setVariant(XVariant* var, const XHashMap* map)
{
	if (!XHashMap_prepareVariant(var, map))
		return;
	XHashMap_copy_base(XVariant_data(var), map);
}

void XHashMap_setVariant_move(XVariant* var, XHashMap* map)
{
	if (!XHashMap_prepareVariant(var, map))
		return;
	XHashMap_move_base(XVariant_data(var), map);
}

void XHashMap_setVariant_ref(XVariant* var, XHashMap* map)
{
	if (!var || !map)
		return;
	XVariant_setDataRef(var, map, sizeof(XHashMap), XVariantType_Hash);
}

// ======================== 桶数组访问辅助 ========================
// 获取桶数组基地址（XRBTreeNode** 类型）
static inline XRBTreeNode** XHashMap_buckets(XHashMap* map) {
    if (XContainerIsCow(map)) {
        return (XRBTreeNode**)XContainerSharedDataPtr(map);
    } else {
        return (XRBTreeNode**)XContainerDataPtr(map);
    }
}

// 获取桶数组基地址的指针（XRBTreeNode*** 类型，用于修改）
static inline XRBTreeNode*** XHashMap_buckets_ptr(XHashMap* map) {
    if (XContainerIsCow(map)) {
        return (XRBTreeNode***)XContainerSharedDataPtr(map);
    } else {
        return (XRBTreeNode***)&XContainerDataPtr(map);
    }
}

// ======================== 前向声明 ========================
static bool VXHashMapDetachIfNeeded(XHashMap* this_hash);
static void VXHashMapDataDelete(void* data, XHashMap* this_hash);
static bool XHashMap_resize(XHashMap* map, size_t new_capacity);

static bool VXMap_insert(XHashMap* this_hash, const void* pvKey, const void* pvValue,
                         XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod);
static void VXMap_erase(XHashMap* this_hash, const XHashMap_iterator* it, XHashMap_iterator* next);
static bool VXMap_remove(XHashMap* this_hash, const void* pvKey);
static void* VXMap_value(XHashMap* this_hash, const void* pvKey);
static bool VXMap_find(XHashMap* this_hash, const void* pvKey, XHashMap_iterator* it);
static XVector* VXMapBase_keys(const XMapBase* this_hash);
static XVector* VXMapBase_values(const XMapBase* this_hash);
static void VXMap_clear(XHashMap* this_hash);
static void VXClass_copy(XHashMap* object, const XHashMap* src);
static void VXClass_move(XHashMap* object, XHashMap* src);
static void VXMap_deinit(XHashMap* this_hash);

// ======================== 虚函数表初始化 ========================
XVtable* XHashMap_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XHASHMAP_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XHashMap");
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
	XCLASS_SHOW_SIZE_DEFAULT(XHashMap);
    return XVTABLE_DEFAULT;
}

// ======================== COW 分离 ========================
static bool VXHashMapDetachIfNeeded(XHashMap* this_hash)
{
    if (!XContainerIsCow(this_hash)) return true;

    XSharedData* sd = (XSharedData*)XContainerDataPtr(this_hash);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t capacity = XContainerCapacity(this_hash);
    size_t typeSize = XContainerTypeSize(this_hash);
    size_t keyTypeSize = ((XMapBase*)this_hash)->m_keyTypeSize;

    XRBTreeNode** oldBuckets = XHashMap_buckets(this_hash);
    if (!oldBuckets) return true;

    size_t newSize = capacity * sizeof(XRBTreeNode*);
    XSharedData* newShared = XSharedData_create(NULL, newSize);
    if (!newShared) return false;
    XRBTreeNode** newBuckets = (XRBTreeNode**)newShared->data;
    memset(newBuckets, 0, newSize);

    for (size_t i = 0; i < capacity; i++) {
        XRBTreeNode* root = oldBuckets[i];
        if (root) {
            XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
            if (!nodes) {
                XSharedData_release(newShared);
                return false;
            }
            XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
            for (size_t j = 0; j < XVector_size_base(nodes); j++) {
                XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[j];
                XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);
                XRBTreeNode* newNode = XRBTree_create(NULL, XMapBasePairTypeSize(this_hash));
                if (!newNode) {
                    XVector_delete_base(nodes);
                    XSharedData_release(newShared);
                    return false;
                }
                XPair* newPair = XBTreeNode_GetDataPtr(newNode);
                XPair_init(newPair, keyTypeSize, typeSize);
                if (XMapBaseKeyCopyMethod(this_hash))
                    XMapBaseKeyCopyMethod(this_hash)(XPair_first(newPair), XPair_first(oldPair));
                else
                    memcpy(XPair_first(newPair), XPair_first(oldPair), keyTypeSize);
                if (XContainerDataCopyMethod(this_hash))
                    XContainerDataCopyMethod(this_hash)(XPair_second(newPair), XPair_second(oldPair));
                else
                    memcpy(XPair_second(newPair), XPair_second(oldPair), typeSize);
                XRBTree_SetRed(newNode);
                memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
                ((XTreeNode*)newNode)->parentNode = NULL;
                XRBTree_insertNode(&newBuckets[i], XContainerCompare(this_hash), XCompareRuleTwo_XMap, newNode);
            }
            XVector_delete_base(nodes);
        }
    }

    XSharedData_release(sd);
    XContainerSetDataPtr(this_hash, newShared);
    return true;
}

static void VXHashMapDataDelete(void* data, XHashMap* this_hash)
{
    if (!this_hash) return;
    XRBTreeNode** buckets = (XRBTreeNode**)data;
    size_t capacity = XContainerCapacity(this_hash);
    for (size_t i = 0; i < capacity; i++) {
        if (buckets[i])
            XTree_delete(buckets[i], XMapBase_deleteNodeData, this_hash);
    }
    XContainerSize(this_hash) = 0;
    XContainerCapacity(this_hash) = 0;
    // 注意：不清理容器自身的指针
}

// ======================== 扩容 ========================
static bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
    size_t newSize = new_capacity * sizeof(XRBTreeNode*);
    XRBTreeNode** newBuckets = NULL;
    XSharedData* newShared = NULL;

    if (XContainerIsCow(map)) {
        newShared = XSharedData_create(NULL, newSize);
        if (!newShared) return false;
        newBuckets = (XRBTreeNode**)newShared->data;
    } else {
        newBuckets = (XRBTreeNode**)XMalloc_System(newSize);
        if (!newBuckets) return false;
    }
    memset(newBuckets, 0, newSize);

    XRBTreeNode** oldBuckets = XHashMap_buckets(map);
    size_t oldCap = XContainerCapacity(map);

    for (size_t i = 0; i < oldCap; i++) {
        XRBTreeNode* root = oldBuckets ? oldBuckets[i] : NULL;
        if (root) {
            XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
            if (!nodes) {
                if (newShared) XSharedData_release(newShared);
                else XFree_System(newBuckets);
                return false;
            }
            XBTree_TraversingToXVector(root, XBTreePreorder, nodes);
            for (size_t j = 0; j < XVector_size_base(nodes); j++) {
                XRBTreeNode* node = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[j];
                XPair* pair = XBTreeNode_GetDataPtr(node);
                size_t idx = map->m_hash(XPair_first(pair), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;
                XRBTree_SetRed(node);
                memset(XTreeNode_GetNodes(node), 0, sizeof(XTreeNode*) * ((XTreeNode*)node)->nodeCount);
                ((XTreeNode*)node)->parentNode = NULL;
                XRBTree_insertNode(&newBuckets[idx], XContainerCompare(map), XCompareRuleTwo_XMap, node);
            }
            XVector_delete_base(nodes);
        }
    }

    if (XContainerIsCow(map)) {
        if ((XSharedData*)XContainerDataPtr(map))
            XSharedData_release((XSharedData*)XContainerDataPtr(map));
        XContainerSetDataPtr(map, newShared);
    } else {
        if (XContainerDataPtr(map))
            XFree_System(XContainerDataPtr(map));
        XContainerDataPtr(map) = newBuckets;
    }
    XContainerCapacity(map) = new_capacity;
    return true;
}

// ======================== 插入 ========================
bool VXMap_insert(XHashMap* this_hash, const void* pvKey, const void* pvValue,
                  XCDataCreatMethod keyCreatMethod, XCDataCreatMethod dataCreatMethod)
{
    if (!VXHashMapDetachIfNeeded(this_hash)) return false;

    // 初始化桶数组（首次写入）
    if (XContainerIsCow(this_hash)) {
        if (!(XSharedData*)XContainerDataPtr(this_hash)) {
            size_t size = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            XSharedData* sd = XSharedData_create(NULL, size);
            if (!sd) return false;
            memset(sd->data, 0, size);
            XContainerSetDataPtr(this_hash, sd);
            XContainerCapacity(this_hash) = DEFAULT_CAPACITY;
        }
    } else {
        if (!XContainerDataPtr(this_hash)) {
            size_t size = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            void* buckets = XMalloc_System(size);
            if (!buckets) return false;
            memset(buckets, 0, size);
            XContainerDataPtr(this_hash) = buckets;
            XContainerCapacity(this_hash) = DEFAULT_CAPACITY;
        }
    }

    // 检查是否需要扩容
    if ((double)XContainerSize(this_hash) / XContainerCapacity(this_hash) >= DEFAULT_LOAD_FACTOR) {
        size_t new_cap = XContainerCapacity(this_hash) * 2;
        if (!XHashMap_resize(this_hash, new_cap))
            return false;
    }

    size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
    XRBTreeNode** root_ptr = &XHashMap_buckets(this_hash)[index];
    XHashMap_iterator it;
    XPair* pair = NULL;

    if (!XHashMap_find_base(this_hash, pvKey, &it)) {
        pair = XMapBasePairBuffer(this_hash);
        XPair_init(pair, ((XMapBase*)this_hash)->m_keyTypeSize, XContainerTypeSize(this_hash));
        if (keyCreatMethod)
            keyCreatMethod(XPair_first(pair), pvKey);
        else
            XPair_insertFirst(pair, pvKey);
        if (dataCreatMethod)
            dataCreatMethod(XPair_second(pair), pvValue);
        else
            XPair_insertSecond(pair, pvValue);

        XRBTreeNode* inserted = XRBTree_insert(root_ptr, XContainerCompare(this_hash),
                                               XCompareRuleTwo_XMap, pair, XMapBasePairTypeSize(this_hash));
        if (!inserted) {
            //XMapBase_deleteNodeData(pair, this_hash);
            return false;
        }
        ++XContainerSize(this_hash);
    } else {
        pair = XHashMap_iterator_data(&it);
        if (XMapBaseKeyDeinitMethod(this_hash))
            XMapBaseKeyDeinitMethod(this_hash)(XPair_first(pair));
        if (keyCreatMethod)
            keyCreatMethod(XPair_first(pair), pvKey);
        else
            XPair_insertFirst(pair, pvKey);
        if (XContainerDataDeinitMethod(this_hash))
            XContainerDataDeinitMethod(this_hash)(XPair_second(pair));
        if (dataCreatMethod)
            dataCreatMethod(XPair_second(pair), pvValue);
        else
            XPair_insertSecond(pair, pvValue);
    }
    return true;
}

// ======================== 删除迭代器 ========================
void VXMap_erase(XHashMap* this_hash, const XHashMap_iterator* it, XHashMap_iterator* next)
{
    if (XHashMap_iterator_isEnd(it)) {
        if (next) *next = XHashMap_end(this_hash);
        return;
    }
    if (!VXHashMapDetachIfNeeded(this_hash)) {
        if (next) *next = XHashMap_end(this_hash);
        return;
    }

    XHashMap_iterator next_it = *it;
    XHashMap_iterator_add(this_hash, &next_it);

    XRBTreeNode* current_node = (XRBTreeNode*)it->node;
    if (!current_node) {
        if (next) *next = next_it;
        return;
    }

    XRBTreeNode** root_ptr = &XHashMap_buckets(this_hash)[it->index];
    XRBTreeNode* removeNode = XRBTree_removeNode(root_ptr, current_node, XMapBasePairTypeSize(this_hash));
    if (removeNode) {
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_hash);
        XRBTreeNode_delete(removeNode);
        --XContainerSize(this_hash);
    }
    if (next) *next = next_it;
}

// ======================== 删除键 ========================
bool VXMap_remove(XHashMap* this_hash, const void* pvKey)
{
    if (XMapBase_isEmpty_base(this_hash)) return false;
    if (!VXHashMapDetachIfNeeded(this_hash)) return false;

    size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
    XRBTreeNode** root_ptr = &XHashMap_buckets(this_hash)[index];
    XRBTreeNode* removeNode = XRBTree_remove(root_ptr, XContainerCompare(this_hash),
                                             XCompareRuleOne_XMap, pvKey, XMapBasePairTypeSize(this_hash));
    if (removeNode) {
        XMapBase_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_hash);
        XRBTreeNode_delete(removeNode);
        --XContainerSize(this_hash);
        return true;
    }
    return false;
}

// ======================== 取值 ========================
void* VXMap_value(XHashMap* this_hash, const void* pvKey)
{
    if (XMapBase_isEmpty_base(this_hash)) return NULL;
    XHashMap_iterator it;
    if (XHashMap_find_base(this_hash, pvKey, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        return XPair_second(pair);
    }
    return NULL;
}

// ======================== 查找 ========================
bool VXMap_find(XHashMap* this_hash, const void* pvKey, XHashMap_iterator* it)
{
    if (XMapBase_isEmpty_base(this_hash)) {
        if (it) *it = XHashMap_end(this_hash);
        return false;
    }
    size_t index = this_hash->m_hash(pvKey, ((XMapBase*)this_hash)->m_keyTypeSize) % XContainerCapacity(this_hash);
    XRBTreeNode* node = XRBTree_findNode(XHashMap_buckets(this_hash)[index],
                                         XContainerCompare(this_hash), XCompareRuleOne_XMap, pvKey);
    if (!node) {
        if (it) *it = XHashMap_end(this_hash);
        return false;
    }
    if (it) {
        it->node = node;
        it->index = index;
    }
    return true;
}

// ======================== 键集合 ========================
XVector* VXMapBase_keys(const XMapBase* this_hash)
{
    XVector* v = XVector_create(this_hash->m_keyTypeSize);
    XContainerSetDataCopyMethod(v, XMapBaseKeyCopyMethod(this_hash));
    XContainerSetDataMoveMethod(v, XMapBaseKeyMoveMethod(this_hash));
    XContainerSetDataDeinitMethod(v, XMapBaseKeyDeinitMethod(this_hash));
    for_each_iterator(this_hash, XHashMap, it) {
        XVector_push_back_1_base(v, XPair_first(XHashMap_iterator_data(&it)));
    }
    return v;
}

XVector* VXMapBase_values(const XMapBase* this_hash)
{
    XVector* v = XVector_create(XContainerTypeSize(this_hash));
    XContainerSetDataCopyMethod(v, XContainerDataCopyMethod(this_hash));
    XContainerSetDataMoveMethod(v, XContainerDataMoveMethod(this_hash));
    XContainerSetDataDeinitMethod(v, XContainerDataDeinitMethod(this_hash));
    for_each_iterator(this_hash, XHashMap, it) {
        XVector_push_back_1_base(v, XPair_second(XHashMap_iterator_data(&it)));
    }
    return v;
}

// ======================== 清空 ========================
void VXMap_clear(XHashMap* this_hash)
{
    if (XHashMap_isEmpty_base(this_hash)) return;
    if (XContainerIsCow(this_hash) && (XSharedData*)XContainerDataPtr(this_hash) && XSharedData_isShared((XSharedData*)XContainerDataPtr(this_hash))) {
        XSharedData_release((XSharedData*)XContainerDataPtr(this_hash));
        XContainerSetDataPtr(this_hash, NULL);
        XContainerCapacity(this_hash) = 0;
        XContainerSize(this_hash) = 0;
        return;
    }
    XRBTreeNode** buckets = XHashMap_buckets(this_hash);
    size_t cap = XContainerCapacity(this_hash);
    for (size_t i = 0; i < cap; i++) {
        if (buckets[i])
            XTree_delete(buckets[i], XMapBase_deleteNodeData, this_hash);
    }
    if (XContainerIsCow(this_hash)) {
        if ((XSharedData*)XContainerDataPtr(this_hash))
            memset(XContainerSharedDataPtr(this_hash), 0, cap * sizeof(XRBTreeNode*));
    } else {
        if (XContainerDataPtr(this_hash))
            memset(XContainerDataPtr(this_hash), 0, cap * sizeof(XRBTreeNode*));
    }
    XContainerSize(this_hash) = 0;
    // 注意：不清空容量，保留以便重用
}

// ======================== 拷贝 ========================
void VXClass_copy(XHashMap* object, const XHashMap* src)
{
    if (XClassIsVtableNull(object)) {
        XHashMap_init(object, ((XMapBase*)src)->m_keyTypeSize, XContainerTypeSize(src),
                      src->m_hash, XContainerCompare(src), XContainerIsCow(src));
    } else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object), VXHashMapDataDelete, object);
        } else {
            XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(object);
            if (buckets) {
                size_t cap = XContainerCapacity(object);
                for (size_t i = 0; i < cap; i++) {
                    if (buckets[i])
                        XTree_delete(buckets[i], XMapBase_deleteNodeData, object);
                }
                XFree_System(buckets);
            }
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
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = XContainerCapacity(src);
    } else {
        // 非 COW：深拷贝桶数组和红黑树
        XRBTreeNode** srcBuckets = (XRBTreeNode**)XContainerDataPtr(src);
        size_t cap = XContainerCapacity(src);
        size_t typeSize = XContainerTypeSize(src);
        size_t keySize = ((XMapBase*)src)->m_keyTypeSize;
        XRBTreeNode** newBuckets = (XRBTreeNode**)XMalloc_System(cap * sizeof(XRBTreeNode*));
        if (!newBuckets) return;
        memset(newBuckets, 0, cap * sizeof(XRBTreeNode*));
        for (size_t i = 0; i < cap; i++) {
            if (srcBuckets[i]) {
                XVector* nodes = XVector_create(sizeof(XRBTreeNode*));
                if (!nodes) {
                    XFree_System(newBuckets);
                    return;
                }
                XBTree_TraversingToXVector(srcBuckets[i], XBTreePreorder, nodes);
                for (size_t j = 0; j < XVector_size_base(nodes); j++) {
                    XRBTreeNode* oldNode = ((XRBTreeNode**)XContainerSharedDataPtr(nodes))[j];
                    XPair* oldPair = XBTreeNode_GetDataPtr(oldNode);
                    XRBTreeNode* newNode = XRBTree_create(NULL, XMapBasePairTypeSize(object));
                    if (!newNode) {
                        XVector_delete_base(nodes);
                        XFree_System(newBuckets);
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
                    XRBTree_insertNode(&newBuckets[i], XContainerCompare(object), XCompareRuleTwo_XMap, newNode);
                }
                XVector_delete_base(nodes);
            }
        }
        XContainerDataPtr(object) = newBuckets;
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = cap;
    }
}

// ======================== 移动 ========================
void VXClass_move(XHashMap* object, XHashMap* src)
{
    if (XClassIsVtableNull(object)) {
        XHashMap_init(object, ((XMapBase*)src)->m_keyTypeSize, XContainerTypeSize(src),
            src->m_hash, XContainerCompare(src), XContainerIsCow(src));
    }
    else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if ((XSharedData*)XContainerDataPtr(object))
                XSharedData_release_with((XSharedData*)XContainerDataPtr(object), VXHashMapDataDelete, object);
        }
        else {
            XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(object);
            if (buckets) {
                size_t cap = XContainerCapacity(object);
                for (size_t i = 0; i < cap; i++) {
                    if (buckets[i])
                        XTree_delete(buckets[i], XMapBase_deleteNodeData, object);
                }
                XFree_System(buckets);
            }
            XContainerDataPtr(object) = NULL;
        }
        XContainerSize(object) = 0;
        XContainerCapacity(object) = 0;   // 关键：交换前目标已经清空
    }

    // 交换目标与源（包括 m_data、m_capacity、m_size、m_hash 等）
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XHashMap) - sizeof(XClass));
}

// ======================== 析构 ========================
void VXMap_deinit(XHashMap* this_hash)
{
    if (XContainerIsCow(this_hash)) {
        if ((XSharedData*)XContainerDataPtr(this_hash))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(this_hash), VXHashMapDataDelete, this_hash);
    } else {
        XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(this_hash);
        if (buckets) {
            size_t cap = XContainerCapacity(this_hash);
            for (size_t i = 0; i < cap; i++) {
                if (buckets[i])
                    XTree_delete(buckets[i], XMapBase_deleteNodeData, this_hash);
            }
            XFree_System(buckets);
        }
        XContainerDataPtr(this_hash) = NULL;
    }
    XContainerSize(this_hash) = 0;
    XContainerCapacity(this_hash) = 0;
    if (XMapBasePairBuffer(this_hash)) {
        XPair_delete(XMapBasePairBuffer(this_hash));
        XMapBasePairBuffer(this_hash) = NULL;
    }
}


XHashMap* XHashMap_create_ex(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow)
{
    if (keyTypeSize == 0 || valTypeSize == 0 || hash == NULL || compare == NULL) return NULL;
    XHashMap* map = XMalloc_System(sizeof(XHashMap));
    if (!map) return NULL;
    XHashMap_init(map, keyTypeSize, valTypeSize, hash, compare, useCow);
    Set_Class_MemoryFree(map, XFree_System);
    return map;
}
XHashMap* XHashMap_create_copy(const XHashMap* other)
{
    if (!other) return NULL;
    XHashMap* map = XHashMap_create_ex(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other),
        other->m_hash, XContainerCompare(other), XContainerIsCow(other));
    if (!map) return NULL;
    XHashMap_copy_base(map, other);
    return map;
}
XHashMap* XHashMap_create_move(XHashMap* other)
{
    if (!other) return NULL;
    XHashMap* map = XHashMap_create_ex(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other),
        other->m_hash, XContainerCompare(other), XContainerIsCow(other));
    if (!map) return NULL;
    XHashMap_move_base(map, other);
    return map;
}
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow)
{
    if (this_map == NULL)
        return;
    XMapBase_init(this_map, keyTypeSize, valTypeSize, compare, useCow);
    XClassSetVtable(this_map, XHashMap);
    //XClassGetVtable(this_map) = XHashMap_class_init();
    this_map->m_hash = hash;
    XContainerDataPtr(this_map) = NULL;
}

XVariantHashMap* XHashMap_create_XVariantHashMap()
{
    XHashMap* hash = XHashMap_Create(XString, XVariant, XString_compare);
    if (hash == NULL)
        return NULL;
    XMapBaseSetKeyCopyMethod(hash, XString_copy_base);
    XMapBaseSetKeyMoveMethod(hash, XString_move_base);
    XMapBaseSetKeyDeinitMethod(hash, XString_deinit_base);
    XContainerSetDataCopyMethod(hash, XVariant_copy_base);
    XContainerSetDataMoveMethod(hash, XVariant_move_base);
    XContainerSetDataDeinitMethod(hash, XVariant_deinit_base);
    return hash;
}

/* ============================== Qt 6.8 命名对齐: reserve/squeeze ============================== */

bool XHashMap_reserve_base(XHashMap* this_hash, size_t size)
{
    if (!this_hash) return false;
    size_t need = (size_t)((double)size / (double)DEFAULT_LOAD_FACTOR) + 1;
    if (need < DEFAULT_CAPACITY) need = DEFAULT_CAPACITY;
    size_t cur = XContainerCapacity(this_hash);
    if (need <= cur) return true;
    size_t pow2 = DEFAULT_CAPACITY;
    while (pow2 < need) pow2 <<= 1;
    if (cur == 0) {
        if (XContainerIsCow(this_hash)) {
            size_t szBytes = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            XSharedData* sd = XSharedData_create(NULL, szBytes);
            if (!sd) return false;
            memset(sd->data, 0, szBytes);
            XContainerSetDataPtr(this_hash, sd);
        } else {
            size_t szBytes = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            void* p2 = XMalloc_System(szBytes);
            if (!p2) return false;
            memset(p2, 0, szBytes);
            XContainerDataPtr(this_hash) = p2;
        }
        XContainerCapacity(this_hash) = DEFAULT_CAPACITY;
    }
    return XHashMap_resize(this_hash, pow2);
}

void XHashMap_squeeze_base(XHashMap* this_hash)
{
    if (!this_hash) return;
    size_t cur = XContainerCapacity(this_hash);
    if (cur <= DEFAULT_CAPACITY) return;
    size_t sz = XContainerSize(this_hash);
    size_t need = (size_t)((double)sz / (double)DEFAULT_LOAD_FACTOR) + 1;
    if (need < DEFAULT_CAPACITY) need = DEFAULT_CAPACITY;
    size_t pow2 = DEFAULT_CAPACITY;
    while (pow2 < need) pow2 <<= 1;
    if (pow2 >= cur) return;
    (void)XHashMap_resize(this_hash, pow2);
}

#endif
