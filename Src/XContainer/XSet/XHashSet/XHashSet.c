#include "XHashSet.h"
#if XHashSet_ON
#include "XAlgorithm.h"
#include "XVector.h"
#include "XRedBlackTree.h"
#include <string.h>

// 获取桶数组基地址（指向 XRBTreeNode* 数组的指针，即 XRBTreeNode**）
#define XHashSet_Buckets(set) \
    (XContainerIsCow(set) ? (XRBTreeNode**)XContainerSharedDataPtr(set) : (XRBTreeNode**)XContainerDataPtr(set))

static bool VXHashSetDetachIfNeeded(XHashSet* this_set);
static void VXHashSetDataDelete(void* data, XHashSet* this_set);
static bool XHashSet_resize(XHashSet* set, size_t new_capacity);

static bool VXSet_insert(XHashSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod);
static void VXSet_erase(XHashSet* this_set, const XHashSet_iterator* it, XHashSet_iterator* next);
static bool VXSet_remove(XHashSet* this_set, const void* key);
static bool VXSet_find(XHashSet* this_set, const void* key, XHashSet_iterator* it);
static void VXSet_clear(XHashSet* this_set);
static void VXClass_copy(XHashSet* object, const XHashSet* src);
static void VXClass_move(XHashSet* object, XHashSet* src);
static void VXSet_deinit(XHashSet* this_set);
static XVector* VXSetBase_keys(const XSetBase* this_set);

static void XSet_deleteNodeData(void* key, XHashSet* this_set)
{
    if (!key || !this_set) return;
    if (XContainerDataDeinitMethod(this_set))
        XContainerDataDeinitMethod(this_set)(key);
}

XVtable* XHashSet_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XHASHSET_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXSet_insert, VXSet_erase, VXSet_remove, VXSet_find, VXSetBase_keys
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXSet_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSet_deinit);
#if SHOWCONTAINERSIZE
    printf("XHashSet size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

static bool VXHashSetDetachIfNeeded(XHashSet* this_set)
{
    if (!XContainerIsCow(this_set)) return true;

    XSharedData* sd = XContainerSharedData(this_set);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t capacity = XContainerCapacity(this_set);
    size_t typeSize = XContainerTypeSize(this_set);
    XRBTreeNode** oldBuckets = XHashSet_Buckets(this_set);
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
                void* oldKey = XBTreeNode_GetDataPtr(oldNode);
                XRBTreeNode* newNode = XRBTree_create(NULL, typeSize);
                if (!newNode) {
                    XVector_delete_base(nodes);
                    XSharedData_release(newShared);
                    return false;
                }
                void* newKey = XBTreeNode_GetDataPtr(newNode);
                if (XContainerDataCopyMethod(this_set))
                    XContainerDataCopyMethod(this_set)(newKey, oldKey);
                else
                    memcpy(newKey, oldKey, typeSize);
                XRBTree_SetRed(newNode);
                memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
                ((XTreeNode*)newNode)->parentNode = NULL;
                XRBTree_insertNode(&newBuckets[i], XContainerCompare(this_set), XCompareRuleTwo_XSet, newNode);
            }
            XVector_delete_base(nodes);
        }
    }

    XSharedData_release(sd);
    XContainerSharedData(this_set) = newShared;
    return true;
}

static void VXHashSetDataDelete(void* data, XHashSet* this_set)
{
    if (!data || !this_set) return;
    XRBTreeNode** buckets = (XRBTreeNode**)data;
    size_t capacity = XContainerCapacity(this_set);
    for (size_t i = 0; i < capacity; i++) {
        if (buckets[i])
            XTree_delete(buckets[i], XSet_deleteNodeData, this_set);
    }
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
}

static bool XHashSet_resize(XHashSet* set, size_t new_capacity)
{
    size_t newSize = new_capacity * sizeof(XRBTreeNode*);
    XRBTreeNode** newBuckets = NULL;
    XSharedData* newShared = NULL;
    if (XContainerIsCow(set)) {
        newShared = XSharedData_create(NULL, newSize);
        if (!newShared) return false;
        newBuckets = (XRBTreeNode**)newShared->data;
    }
    else {
        newBuckets = (XRBTreeNode**)XMalloc_System(newSize);
        if (!newBuckets) return false;
    }
    memset(newBuckets, 0, newSize);

    XRBTreeNode** oldBuckets = XHashSet_Buckets(set);
    size_t oldCap = XContainerCapacity(set);

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
                void* key = XBTreeNode_GetDataPtr(node);
                size_t idx = set->m_hash(key, XContainerTypeSize(set)) % new_capacity;
                XRBTree_SetRed(node);
                memset(XTreeNode_GetNodes(node), 0, sizeof(XTreeNode*) * ((XTreeNode*)node)->nodeCount);
                ((XTreeNode*)node)->parentNode = NULL;
                XRBTree_insertNode(&newBuckets[idx], XContainerCompare(set), XCompareRuleTwo_XSet, node);
            }
            XVector_delete_base(nodes);
        }
    }

    if (XContainerIsCow(set)) {
        if (XContainerSharedData(set))
            XSharedData_release(XContainerSharedData(set));
        XContainerSharedData(set) = newShared;
    }
    else {
        if (XContainerDataPtr(set))
            XFree_System(XContainerDataPtr(set));
        XContainerDataPtr(set) = newBuckets;
    }
    XContainerCapacity(set) = new_capacity;
    return true;
}

bool VXSet_insert(XHashSet* this_set, const void* key, XCDataCreatMethod dataCreatMethod)
{
    if (!VXHashSetDetachIfNeeded(this_set)) return false;

    // 初始分配
    if (XContainerIsCow(this_set)) {
        if (!XContainerSharedData(this_set)) {
            size_t size = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            XSharedData* sd = XSharedData_create(NULL, size);
            if (!sd) return false;
            memset(sd->data, 0, size);
            XContainerSharedData(this_set) = sd;
            XContainerCapacity(this_set) = DEFAULT_CAPACITY;
        }
    }
    else {
        if (!XContainerDataPtr(this_set)) {
            size_t size = DEFAULT_CAPACITY * sizeof(XRBTreeNode*);
            void* buckets = XMalloc_System(size);
            if (!buckets) return false;
            memset(buckets, 0, size);
            XContainerDataPtr(this_set) = buckets;
            XContainerCapacity(this_set) = DEFAULT_CAPACITY;
        }
    }

    if ((double)XContainerSize(this_set) / XContainerCapacity(this_set) >= DEFAULT_LOAD_FACTOR) {
        size_t new_cap = XContainerCapacity(this_set) * 2;
        if (!XHashSet_resize(this_set, new_cap))
            return false;
    }

    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode** buckets = XHashSet_Buckets(this_set);
    if (!buckets) return false;
    XRBTreeNode** root_ptr = &buckets[index];

    XRBTreeNode* current = XRBTree_findNode(*root_ptr, XContainerCompare(this_set), XCompareRuleOne_XSet, key);
    if (current == NULL) {
        if (dataCreatMethod) {
            void* temp = XCalloc_System(1, XContainerTypeSize(this_set));
            dataCreatMethod(temp, key);
            XRBTree_insert(root_ptr, XContainerCompare(this_set), XCompareRuleTwo_XSet, temp, XContainerTypeSize(this_set));
            XFree_System(temp);
        }
        else {
            XRBTree_insert(root_ptr, XContainerCompare(this_set), XCompareRuleTwo_XSet, key, XContainerTypeSize(this_set));
        }
        ++XContainerSize(this_set);
    }
    return true;
}

void VXSet_erase(XHashSet* this_set, const XHashSet_iterator* it, XHashSet_iterator* next)
{
    if (XHashSet_iterator_isEnd(it)) {
        if (next) *next = XHashSet_end(this_set);
        return;
    }
    if (!VXHashSetDetachIfNeeded(this_set)) {
        if (next) *next = XHashSet_end(this_set);
        return;
    }

    XHashSet_iterator next_it = *it;
    XHashSet_iterator_add(this_set, &next_it);

    XRBTreeNode* current_node = (XRBTreeNode*)it->node;
    if (!current_node) {
        if (next) *next = next_it;
        return;
    }

    XRBTreeNode** buckets = XHashSet_Buckets(this_set);
    if (!buckets) {
        if (next) *next = XHashSet_end(this_set);
        return;
    }
    XRBTreeNode** root_ptr = &buckets[it->index];
    XRBTreeNode* removeNode = XRBTree_removeNode(root_ptr, current_node, XContainerTypeSize(this_set));
    if (removeNode) {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete(removeNode);
        --XContainerSize(this_set);
    }
    if (next) *next = next_it;
}

bool VXSet_remove(XHashSet* this_set, const void* key)
{
    if (XSetBase_isEmpty_base(this_set)) return false;
    if (!VXHashSetDetachIfNeeded(this_set)) return false;

    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode** buckets = XHashSet_Buckets(this_set);
    if (!buckets) return false;
    XRBTreeNode** root_ptr = &buckets[index];
    XRBTreeNode* removeNode = XRBTree_remove(root_ptr, XContainerCompare(this_set), XCompareRuleOne_XSet, key, XContainerTypeSize(this_set));
    if (removeNode) {
        XSet_deleteNodeData(XBTreeNode_GetDataPtr(removeNode), this_set);
        XRBTreeNode_delete(removeNode);
        --XContainerSize(this_set);
        return true;
    }
    return false;
}

bool VXSet_find(XHashSet* this_set, const void* key, XHashSet_iterator* it)
{
    if (XSetBase_isEmpty_base(this_set)) {
        if (it) *it = XHashSet_end(this_set);
        return false;
    }
    size_t index = this_set->m_hash(key, XContainerTypeSize(this_set)) % XContainerCapacity(this_set);
    XRBTreeNode** buckets = XHashSet_Buckets(this_set);
    if (!buckets) {
        if (it) *it = XHashSet_end(this_set);
        return false;
    }
    XRBTreeNode* node = XRBTree_findNode(buckets[index], XContainerCompare(this_set), XCompareRuleOne_XSet, key);
    if (node == NULL) {
        if (it) *it = XHashSet_end(this_set);
        return false;
    }
    if (it) {
        it->node = node;
        it->index = index;
    }
    return true;
}

void VXSet_clear(XHashSet* this_set)
{
    if (XHashSet_isEmpty_base(this_set)) return;
    if (XContainerIsCow(this_set) && XContainerSharedData(this_set) && XSharedData_isShared(XContainerSharedData(this_set))) {
        XSharedData_release(XContainerSharedData(this_set));
        XContainerSharedData(this_set) = NULL;
        XContainerCapacity(this_set) = 0;
        XContainerSize(this_set) = 0;
        return;
    }
    XRBTreeNode** buckets = XHashSet_Buckets(this_set);
    if (!buckets) return;
    size_t cap = XContainerCapacity(this_set);
    for (size_t i = 0; i < cap; i++) {
        if (buckets[i])
            XTree_delete(buckets[i], XSet_deleteNodeData, this_set);
    }
    if (XContainerIsCow(this_set)) {
        if (XContainerSharedData(this_set))
            memset(XContainerSharedDataPtr(this_set), 0, cap * sizeof(XRBTreeNode*));
    }
    else {
        if (XContainerDataPtr(this_set))
            memset(XContainerDataPtr(this_set), 0, cap * sizeof(XRBTreeNode*));
    }
    XContainerSize(this_set) = 0;
}

void VXClass_copy(XHashSet* object, const XHashSet* src)
{
    if (((XClass*)object)->m_vtable == NULL) {
        XHashSet_init(object, XContainerTypeSize(src), src->m_hash, XContainerCompare(src), XContainerIsCow(src));
    }
    else {
        if (XContainerIsCow(object)) {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXHashSetDataDelete, object);
        }
        else {
            XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(object);
            if (buckets) {
                size_t cap = XContainerCapacity(object);
                for (size_t i = 0; i < cap; i++) {
                    if (buckets[i])
                        XTree_delete(buckets[i], XSet_deleteNodeData, object);
                }
                XFree_System(buckets);
            }
            XContainerDataPtr(object) = NULL;
        }
    }

    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    if (XContainerIsCow(src)) {
        XContainerSharedData(object) = XContainerSharedData(src);
        if (XContainerSharedData(object))
            XSharedData_addRef(XContainerSharedData(object));
    }
    else {
        XRBTreeNode** srcBuckets = (XRBTreeNode**)XContainerDataPtr(src);
        size_t cap = XContainerCapacity(src);
        size_t typeSize = XContainerTypeSize(src);
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
                    void* oldKey = XBTreeNode_GetDataPtr(oldNode);
                    XRBTreeNode* newNode = XRBTree_create(NULL, typeSize);
                    if (!newNode) {
                        XVector_delete_base(nodes);
                        XFree_System(newBuckets);
                        return;
                    }
                    void* newKey = XBTreeNode_GetDataPtr(newNode);
                    if (XContainerDataCopyMethod(object))
                        XContainerDataCopyMethod(object)(newKey, oldKey);
                    else
                        memcpy(newKey, oldKey, typeSize);
                    XRBTree_SetRed(newNode);
                    memset(XTreeNode_GetNodes(newNode), 0, sizeof(XTreeNode*) * ((XTreeNode*)newNode)->nodeCount);
                    ((XTreeNode*)newNode)->parentNode = NULL;
                    XRBTree_insertNode(&newBuckets[i], XContainerCompare(object), XCompareRuleTwo_XSet, newNode);
                }
                XVector_delete_base(nodes);
            }
        }
        XContainerDataPtr(object) = newBuckets;
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = cap;
    }
}

void VXClass_move(XHashSet* object, XHashSet* src)
{
    // 如果目标未初始化，先初始化（模式与源相同）
    if (((XClass*)object)->m_vtable == NULL) {
        XHashSet_init(object, XContainerTypeSize(src), src->m_hash, XContainerCompare(src), XContainerIsCow(src));
    }
    else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXHashSetDataDelete, object);
        }
        else {
            XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(object);
            if (buckets) {
                size_t cap = XContainerCapacity(object);
                for (size_t i = 0; i < cap; i++) {
                    if (buckets[i])
                        XTree_delete(buckets[i], XSet_deleteNodeData, object);
                }
                XFree_System(buckets);
            }
            XContainerDataPtr(object) = NULL;
        }
        // 清空目标的大小和容量，准备交换
        XContainerSize(object) = 0;
        XContainerCapacity(object) = 0;
    }

    // 交换目标与源的所有成员（跳过 XClass 部分，包括 m_data/m_useCow/m_capacity/m_size/m_hash 等）
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XHashSet) - sizeof(XClass));
}

void VXSet_deinit(XHashSet* this_set)
{
    if (XContainerIsCow(this_set)) {
        if (XContainerSharedData(this_set))
            XSharedData_release_with(XContainerSharedData(this_set), VXHashSetDataDelete, this_set);
    }
    else {
        XRBTreeNode** buckets = (XRBTreeNode**)XContainerDataPtr(this_set);
        if (buckets) {
            size_t cap = XContainerCapacity(this_set);
            for (size_t i = 0; i < cap; i++) {
                if (buckets[i])
                    XTree_delete(buckets[i], XSet_deleteNodeData, this_set);
            }
            XFree_System(buckets);
        }
        XContainerDataPtr(this_set) = NULL;
    }
    XContainerSize(this_set) = 0;
    XContainerCapacity(this_set) = 0;
}

XVector* VXSetBase_keys(const XSetBase* this_set)
{
    XVector* v = XVector_create(XContainerTypeSize(this_set));
    for_each_iterator(this_set, XHashSet, it) {
        XVector_push_back_base(v, XHashSet_iterator_data(&it));
    }
    return v;
}

XHashSet* XHashSet_create_ex(const size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow)
{
    if (keyTypeSize == 0 || hash == NULL || compare == NULL) return NULL;
    XHashSet* set = XMalloc_System(sizeof(XHashSet));
    if (!set) return NULL;
    XHashSet_init(set, keyTypeSize, hash, compare, useCow);
    Set_Class_MemoryFree(set, XFree_System);
    return set;
}

void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow)
{
    if (!this_set) return;
    XSetBase_init(&this_set->m_class, keyTypeSize, compare, useCow);
    XClassSetVtable(this_set, XHashSet);
    this_set->m_hash = hash;
    XContainerDataPtr(this_set) = NULL;  // 初始化为空，首次插入时会分配
}

#endif