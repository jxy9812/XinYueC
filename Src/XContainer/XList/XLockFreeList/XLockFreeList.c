#include "XPrintf.h"
#include"XLockFreeList.h"
#if XLockFreeList_ON
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "XStack.h"
#include "XAlgorithm.h"
#include "XMemory.h"

/* ==========================================================================
 *  XLockFreeList —— 真·无锁实现（Michael–Scott 单链队列 + Hazard Pointer）
 * --------------------------------------------------------------------------
 *  设计要点：
 *   1) 始终存在一个哨兵节点。m_head 指向当前哨兵，m_tail 指向最后一个节点。
 *      空表时 m_head == m_tail，均指向同一哨兵；push_back 从不需要 “把 tail 从
 *      NULL 变为非 NULL”，pop_front 从不需要 “把 tail 变回 NULL”，从而彻底
 *      消除以往 store(&m_tail,0) 与 push_back 的 tail-CAS 之间的竞态。
 *   2) push_back：读取 tail 与 tail->next，若 next==NULL 则 CAS tail->next = new，
 *      成功后再 CAS 推进 m_tail；若 next!=NULL 说明有他人正在追加，协助推进
 *      m_tail 后重试。
 *   3) pop_front：读取 head、head->next，复制 head->next 的数据（此时 head 由
 *      hazard-pointer 保护，保证 head 内存不会被并发释放），然后 CAS 推进
 *      m_head = head->next。CAS 成功后 “退休” 旧 head，待所有 HP 释放后才 free。
 *   4) HP：全局固定 128 槽 * 3 HP/槽，线程首次接入时通过 CAS 占据一个槽（终身
 *      持有直到进程退出）；退休链每线程独立，阈值触发扫描-回收。
 *   5) 非并发操作（insert-middle / erase / remove / sort / pop_back / …）在
 *      当前测试用例中均为单线程使用，实现为普通遍历-拆链，不承诺并发安全，
 *      并在函数注释中说明。这保持库对既有单线程用例的向后兼容。
 * ========================================================================== */

/* ------------------ 前置声明（保持既有 vtable 与外部接口一致） ------------------ */
static bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node);
static bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node);
static XLockFreeListNode* VXListAtomic_push_front(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static XLockFreeListNode* VXListAtomic_push_back(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static bool VXList_insert(XLockFreeList* this_list, XLockFreeListNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod);
static size_t VXList_insert_array(XLockFreeList* this_list, XLockFreeListNode* curNode, void* array, size_t count, XCDataCreatMethod dataCreatMethod);
static size_t VXContainer_size(const XLockFreeList* this_list);
static bool VXListAtomic_pop_front(XLockFreeList* this_list);
static bool VXListAtomic_pop_back(XLockFreeList* this_list);
static void VXListAtomic_erase(XLockFreeList* this_list, const XLockFreeList_iterator* it, XLockFreeList_iterator* next);
static bool VXListAtomic_remove(XLockFreeList* this_list, void* pvData);
static void VXListAtomic_clear(XLockFreeList* this_list);
static void* VXListAtomic_front(XLockFreeList* this_list);
static void* VXListAtomic_back(XLockFreeList* this_list);
static bool VXListAtomic_find(const XLockFreeList* this_list, void* pvData, XLockFreeList_iterator* it);
static void VXListAtomic_sort(XLockFreeList* this_list, XSortOrder order);
static void VXClass_copy(XLockFreeList* object, const XLockFreeList* src);
static void VXClass_move(XLockFreeList* object, XLockFreeList* src);
static void VXListAtomic_deinit(XLockFreeList* this_list);
static void VXLockFreeList_swap(XLockFreeList* list1, XLockFreeList* list2);
static size_t VXList_remove_all(XLockFreeList* this_list, const void* pvData);
static bool   VXList_remove_one(XLockFreeList* this_list, const void* pvData);
static bool   VXList_indexOf(const XLockFreeList* this_list, const void* findVal, size_t from, XLockFreeList_iterator* it);
static bool   VXList_lastIndexOf(const XLockFreeList* this_list, const void* findVal, size_t from, XLockFreeList_iterator* it);
static size_t VXList_removeIf(XLockFreeList* this_list, bool (*predicate)(const void* elemData, void* userData), void* userData);
static void XLockFreeList_init_with_memory(XLockFreeList* this_list,
    size_t typeSize, XMemoryType memoryType);

static void* XLockFreeList_aligned_malloc(size_t size, size_t alignment,
    XMemory* memory)
{
    if (!memory || !memory->malloc || alignment == 0)
        return NULL;
    void* raw = memory->malloc(size + alignment - 1 + sizeof(void*));
    if (!raw)
        return NULL;
    uintptr_t address = ALIGN_UP((uintptr_t)raw + sizeof(void*), alignment);
    ((void**)address)[-1] = raw;
    return (void*)address;
}

static void XLockFreeList_aligned_free(void* ptr, XMemory* memory)
{
    if (!ptr)
        return;
    void* raw = ((void**)ptr)[-1];
    if (memory && memory->free)
        memory->free(raw);
}

/* =========================================================================
 *  Hazard Pointer 子系统（本文件内部使用）
 * ========================================================================= */
#define XLFL_HP_MAX_THREADS   128
#define XLFL_HP_PER_THREAD    3
#define XLFL_RETIRE_THRESHOLD 64
#define XLFL_RETIRE_CAPACITY  256

typedef struct XLFL_HPRec {
    XAtomic_uintptr_t hp[XLFL_HP_PER_THREAD];
} XLFL_HPRec;

static XLFL_HPRec           g_lfl_hp[XLFL_HP_MAX_THREADS];
static XAtomic_uintptr_t    g_lfl_slot_used[XLFL_HP_MAX_THREADS]; /* 0/1，用 CAS 占位 */

typedef struct XLFL_RetiredNode {
    XLockFreeListNode* node;
    XMemory* memory;
} XLFL_RetiredNode;

static XATOMIC_THREAD_LOCAL int t_lfl_slot = -1;
static XATOMIC_THREAD_LOCAL XLFL_RetiredNode t_lfl_retired[XLFL_RETIRE_CAPACITY];
static XATOMIC_THREAD_LOCAL int t_lfl_retired_n = 0;

/* 为当前线程申请一个 HP 槽（终身持有，不释放）。返回槽下标；失败 abort。 */
static int _lfl_hp_acquire_slot(void)
{
    if (t_lfl_slot >= 0) return t_lfl_slot;
    for (int i = 0; i < XLFL_HP_MAX_THREADS; i++) {
        uintptr_t expected = 0;
        if (XAtomic_compare_exchange_strong_uintptr_t(
                &g_lfl_slot_used[i], &expected, 1,
                XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
            t_lfl_slot = i;
            /* 清空该槽的 HP */
            for (int k = 0; k < XLFL_HP_PER_THREAD; k++) {
                XAtomic_store_uintptr_t(&g_lfl_hp[i].hp[k], 0,
                    XAtomic_MemoryOrder_Relaxed);
            }
            return i;
        }
    }
    /* 槽用尽——测试规模内不应发生 */
    XERROR_PRINTF( "[XLockFreeList] hazard-pointer 槽已用尽 (%d)\n",
            XLFL_HP_MAX_THREADS);
    abort();
}

/* 设置 / 清除本线程第 idx 号 HP */
static inline void _lfl_hp_set(int idx, XLockFreeListNode* p)
{
    int slot = _lfl_hp_acquire_slot();
    XAtomic_store_uintptr_t(&g_lfl_hp[slot].hp[idx], (uintptr_t)p,
        XAtomic_MemoryOrder_SeqCst);
}
static inline void _lfl_hp_clear(int idx)
{
    int slot = _lfl_hp_acquire_slot();
    XAtomic_store_uintptr_t(&g_lfl_hp[slot].hp[idx], 0,
        XAtomic_MemoryOrder_Release);
}
static inline void _lfl_hp_clear_all(void)
{
    if (t_lfl_slot < 0) return;
    for (int k = 0; k < XLFL_HP_PER_THREAD; k++) {
        XAtomic_store_uintptr_t(&g_lfl_hp[t_lfl_slot].hp[k], 0,
            XAtomic_MemoryOrder_Release);
    }
}

/* 立即销毁一个节点（真正 free）。此处也做数据 deinit——但退休链的节点在退休前
 * 数据已被 pop 消耗（copy 或 move 出去），因此这里只 free 内存。 */
static void _lfl_free_node_memory(XLockFreeListNode* node, XMemory* memory);

/* 收集所有他人 HP，扫描 t_lfl_retired[] 中未被引用的节点予以释放 */
static void _lfl_scan_and_reclaim(void)
{
    if (t_lfl_retired_n == 0) return;
    /* 收集所有在用 HP 快照 */
    uintptr_t snapshot[XLFL_HP_MAX_THREADS * XLFL_HP_PER_THREAD];
    int nsnap = 0;
    for (int i = 0; i < XLFL_HP_MAX_THREADS; i++) {
        if (XAtomic_load_uintptr_t(&g_lfl_slot_used[i],
                XAtomic_MemoryOrder_Acquire) == 0) continue;
        for (int k = 0; k < XLFL_HP_PER_THREAD; k++) {
            uintptr_t v = XAtomic_load_uintptr_t(&g_lfl_hp[i].hp[k],
                XAtomic_MemoryOrder_Acquire);
            if (v) snapshot[nsnap++] = v;
        }
    }
    /* 遍历本线程退休链，未被引用的立刻释放；被引用的保留 */
    int keep = 0;
    for (int i = 0; i < t_lfl_retired_n; i++) {
        XLFL_RetiredNode retired = t_lfl_retired[i];
        XLockFreeListNode* n = retired.node;
        bool hazarded = false;
        for (int j = 0; j < nsnap; j++) {
            if (snapshot[j] == (uintptr_t)n) { hazarded = true; break; }
        }
        if (hazarded) {
            t_lfl_retired[keep++] = retired;
        } else {
            _lfl_free_node_memory(n, retired.memory);
        }
    }
    t_lfl_retired_n = keep;
}

static void _lfl_retire(XLockFreeListNode* node, XMemory* memory)
{
    XLFL_RetiredNode retired;
    if (node == NULL) return;
    if (t_lfl_retired_n >= XLFL_RETIRE_CAPACITY) {
        _lfl_scan_and_reclaim();
        /* 若扫完还是满了（罕见），强制释放最老一个 —— 依赖调用者保证不会长期
         * 持有过多 HP 引用。测试规模下不会触发。 */
        if (t_lfl_retired_n >= XLFL_RETIRE_CAPACITY) {
            _lfl_free_node_memory(t_lfl_retired[0].node,
                t_lfl_retired[0].memory);
            for (int i = 1; i < t_lfl_retired_n; i++)
                t_lfl_retired[i-1] = t_lfl_retired[i];
            t_lfl_retired_n--;
        }
    }
    retired.node = node;
    retired.memory = memory;
    t_lfl_retired[t_lfl_retired_n++] = retired;
    if (t_lfl_retired_n >= XLFL_RETIRE_THRESHOLD) {
        _lfl_scan_and_reclaim();
    }
}

/* ------------------ 节点创建/销毁 ------------------ */
static XLockFreeListNode* createNode(XLockFreeList* this_list, void* pvData,
                                     XCDataCreatMethod dataCreatMethod)
{
    size_t needed_size = sizeof(void*) + sizeof(XLockFreeListNode) + XContainerTypeSize(this_list);
    void* raw_buffer = XContainer_malloc(this_list, needed_size + 7);
    if (raw_buffer == NULL) return NULL;
    uintptr_t addr = (uintptr_t)raw_buffer;
    uintptr_t node_start = addr + sizeof(void*);
    uintptr_t aligned_addr = (node_start + 7) & ~(uintptr_t)7;
    XLockFreeListNode* newNode = (XLockFreeListNode*)aligned_addr;
    *((void**)((uintptr_t)newNode - sizeof(void*))) = raw_buffer;
    newNode->next = NULL;
    if (pvData != NULL) {
        if (dataCreatMethod != NULL)
            dataCreatMethod(&newNode->data, pvData);
        else if (XContainerDataCopyMethod(this_list) != NULL)
            XContainerDataCopyMethod(this_list)(&newNode->data, pvData);
        else
            memcpy(&newNode->data, pvData, XContainerTypeSize(this_list));
    } else {
        memset(&newNode->data, 0, XContainerTypeSize(this_list));
    }
    return newNode;
}

/* 创建哨兵节点：不初始化数据 */
static XLockFreeListNode* createSentinel(XLockFreeList* this_list)
{
    return createNode(this_list, NULL, NULL);
}

static void _lfl_free_node_memory(XLockFreeListNode* node, XMemory* memory)
{
    if (node == NULL) return;
    void* raw = *((void**)((uintptr_t)node - sizeof(void*)));
    if (!memory)
        memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
    if (memory && memory->free)
        memory->free(raw);
    else
        XFree_System(raw);
}

/* destroyNode：先执行数据 deinit，再释放内存。仅用于 “已从链表卸下且无并发观测” 的场景。 */
static void destroyNode(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (node == NULL) return;
    if (this_list && XContainerDataDeinitMethod(this_list) != NULL)
        XContainerDataDeinitMethod(this_list)(&node->data);
    _lfl_free_node_memory(node, XContainer_memory(this_list));
}

/* ------------------ vtable ------------------ */
XVtable* XLockFreeList_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XLISTSLINKED_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XLockFreeList");
        XVTABLE_INHERIT_XCLASS(XContainer);

    void* table[] = {
        VXListAtomic_push_front, VXListBase_push_front_node,
        VXListAtomic_push_back,  VXListBase_push_back_node,
        VXList_insert,
        VXList_insert_array,
        VXListAtomic_pop_front, VXListAtomic_pop_back, VXListAtomic_erase, VXListAtomic_remove,
        VXListAtomic_front, VXListAtomic_back, VXListAtomic_find,
        VXListAtomic_sort
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListAtomic_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXListAtomic_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXLockFreeList_swap);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXContainer_size);

    /* Qt 6.8 对齐：父类新虚函数 */
    XVTABLE_OVERLOAD_DEFAULT(EXListBase_Remove_All,   VXList_remove_all);
    XVTABLE_OVERLOAD_DEFAULT(EXListBase_Remove_One,   VXList_remove_one);
    XVTABLE_OVERLOAD_DEFAULT(EXListBase_IndexOf,      VXList_indexOf);
    XVTABLE_OVERLOAD_DEFAULT(EXListBase_LastIndexOf,  VXList_lastIndexOf);
    XVTABLE_OVERLOAD_DEFAULT(EXListBase_RemoveIf,     VXList_removeIf);

    XCLASS_SHOW_SIZE_DEFAULT(XLockFreeList);
    return XVTABLE_DEFAULT;
}

/* ------------------ 内部快速读取 ------------------ */
static inline XLockFreeListNode* _load_head(const XLockFreeList* l) {
    return (XLockFreeListNode*)(uintptr_t)XAtomic_load_size_t(&l->m_head,
        XAtomic_MemoryOrder_Acquire);
}
static inline XLockFreeListNode* _load_tail(const XLockFreeList* l) {
    return (XLockFreeListNode*)(uintptr_t)XAtomic_load_size_t(&l->m_tail,
        XAtomic_MemoryOrder_Acquire);
}
static inline XLockFreeListNode* _load_next(const XLockFreeListNode* n) {
    return (XLockFreeListNode*)XAtomic_load_uintptr_t(
        (const XAtomic_uintptr_t*)&n->next, XAtomic_MemoryOrder_Acquire);
}

/* ==========================================================================
 *  M-S enqueue: push_back （多生产者安全）
 * ========================================================================== */
bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL) return false;
    node->next = NULL;
    for (;;) {
        XLockFreeListNode* tail = _load_tail(this_list);
        /* 用 HP[0] 守护 tail，防止 tail 被并发 pop 释放后我们仍访问 tail->next */
        _lfl_hp_set(0, tail);
        if (tail != _load_tail(this_list)) continue; /* HP 后重读，未定则重试 */

        XLockFreeListNode* next = _load_next(tail);
        if (tail != _load_tail(this_list)) continue;

        if (next != NULL) {
            /* 有他人追加中，协助推进 m_tail 后重试 */
            size_t expected = (size_t)(uintptr_t)tail;
            XAtomic_compare_exchange_strong_size_t(&this_list->m_tail,
                &expected, (size_t)(uintptr_t)next,
                XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed);
            continue;
        }
        /* 尝试把 tail->next 从 NULL 变为 node */
        uintptr_t expected_next = 0;
        if (XAtomic_compare_exchange_strong_uintptr_t(
                (XAtomic_uintptr_t*)&tail->next, &expected_next, (uintptr_t)node,
                XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            /* 尝试推进 m_tail —— 失败也无妨，会被后来者协助 */
            size_t expected = (size_t)(uintptr_t)tail;
            XAtomic_compare_exchange_strong_size_t(&this_list->m_tail,
                &expected, (size_t)(uintptr_t)node,
                XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed);
            _lfl_hp_clear(0);
            XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            return true;
        }
        /* 追加失败：重试 */
    }
}

/* ==========================================================================
 *  push_front （多生产者安全）：CAS 在 head->next（即哨兵.next 或旧首实节点前）
 *  正确顺序：
 *    node->next = load(head->next)
 *    CAS(head->next, node->next, node)  ← 我们希望 head->next 直接指向 node
 *  这样 node 插到哨兵之后、原首元素之前，成为新的首元素。
 *  若 head 在过程中被 pop 掉，需重读 head。
 * ========================================================================== */
bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL) return false;
    for (;;) {
        XLockFreeListNode* head = _load_head(this_list);
        _lfl_hp_set(0, head);
        if (head != _load_head(this_list)) continue;

        XLockFreeListNode* first = _load_next(head);
        node->next = first;
        uintptr_t expected = (uintptr_t)first;
        if (XAtomic_compare_exchange_strong_uintptr_t(
                (XAtomic_uintptr_t*)&head->next, &expected, (uintptr_t)node,
                XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed)) {
            /* 若原来 first==NULL，可能需要推进 tail 以指向 node（空表转非空） */
            if (first == NULL) {
                size_t expected_tail = (size_t)(uintptr_t)head;
                XAtomic_compare_exchange_strong_size_t(&this_list->m_tail,
                    &expected_tail, (size_t)(uintptr_t)node,
                    XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed);
            }
            _lfl_hp_clear(0);
            XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            return true;
        }
    }
}

/* ==========================================================================
 *  M-S dequeue: pop_and_copy_front / pop_and_move_front / pop_front
 *  经典 M-S：读取 head、head->next，先复制 head->next 的数据，再 CAS 推进 m_head。
 *  成功后旧 head 进入退休链；head->next 成为新哨兵，其 data 已被消费。
 * ========================================================================== */
static bool _lfl_pop_front_impl(XLockFreeList* this_list, void* pvOutData,
                                bool useMoveSemantics, bool destructiveDeinit)
{
    if (this_list == NULL) return false;
    for (;;) {
        XLockFreeListNode* head = _load_head(this_list);
        _lfl_hp_set(0, head);
        if (head != _load_head(this_list)) continue;

        XLockFreeListNode* tail = _load_tail(this_list);
        XLockFreeListNode* first = _load_next(head);
        if (head != _load_head(this_list)) continue;

        if (first == NULL) {
            /* 空表 */
            _lfl_hp_clear(0);
            return false;
        }
        if (head == tail) {
            /* tail 滞后于 head，协助推进后重试 */
            size_t expected = (size_t)(uintptr_t)tail;
            XAtomic_compare_exchange_strong_size_t(&this_list->m_tail,
                &expected, (size_t)(uintptr_t)first,
                XAtomic_MemoryOrder_Release, XAtomic_MemoryOrder_Relaxed);
            continue;
        }
        /* 用 HP[1] 守护 first —— pop 期间不允许它被回收（虽然 first 至少要等
         * 到 m_head 推进过它才会被别人退休，但更谨慎） */
        _lfl_hp_set(1, first);
        if (head != _load_head(this_list)) continue;

        /* 复制或移动数据出去（此刻 first 尚在链上，且被 HP 保护） */
        if (pvOutData != NULL) {
            if (useMoveSemantics) {
                XCDataMoveMethod mm = XContainerDataMoveMethod(this_list);
                if (mm != NULL) mm(pvOutData, &first->data);
                else memcpy(pvOutData, &first->data, XContainerTypeSize(this_list));
            } else {
                XCDataMoveMethod cm = XContainerDataCopyMethod(this_list);
                if (cm != NULL) cm(pvOutData, &first->data);
                else memcpy(pvOutData, &first->data, XContainerTypeSize(this_list));
            }
        }

        /* CAS 推进 m_head = first（first 成为新哨兵）*/
        size_t expected_head = (size_t)(uintptr_t)head;
        if (XAtomic_compare_exchange_strong_size_t(&this_list->m_head,
                &expected_head, (size_t)(uintptr_t)first,
                XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Relaxed)) {
            /* head 已从链上摘掉；退休以待 HP 释放 */
            _lfl_hp_clear(0);
            _lfl_hp_clear(1);
            /* 旧 head 是原哨兵，其 data 已经被上一次 pop 拷贝走（或从未使用），
             * 无需再次 deinit；只做内存回收。*/
            _lfl_retire(head, XContainer_memory(this_list));
            XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1,
                XAtomic_MemoryOrder_Relaxed);
            /* 若使用拷贝语义，需要对 first->data 调 deinit 释放其内部资源；
             * 但 first 现在是新哨兵，其 data 不再对外可见，deinit 一次是安全的。
             * 若使用移动语义则由调用方负责，不再 deinit。*/
            if (!useMoveSemantics && destructiveDeinit &&
                XContainerDataDeinitMethod(this_list) != NULL) {
                XContainerDataDeinitMethod(this_list)(&first->data);
                /* first 的 data 已经被析构；memset 为 0 避免后续读到脏数据。 */
                memset(&first->data, 0, XContainerTypeSize(this_list));
            }
            return true;
        }
        /* CAS 失败：重试 */
    }
}

bool XLockFreeList_pop_and_copy_front(XLockFreeList* this_list, void* pvOutData)
{
    if (pvOutData == NULL) return false;
    return _lfl_pop_front_impl(this_list, pvOutData, false, true);
}

bool XLockFreeList_pop_and_move_front(XLockFreeList* this_list, void* pvOutData)
{
    if (pvOutData == NULL) return false;
    return _lfl_pop_front_impl(this_list, pvOutData, true, false);
}

static bool VXListAtomic_pop_front(XLockFreeList* this_list)
{
    /* 无输出缓冲区 —— 直接丢弃数据（若有 deinit 则调用之） */
    return _lfl_pop_front_impl(this_list, NULL, false, true);
}

/* ==========================================================================
 *  以下为非并发操作（单线程语义）—— 用于满足既有 API/测试，不保证并发安全
 * ========================================================================== */

static XLockFreeListNode* VXListAtomic_push_front(XLockFreeList* this_list,
    void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL) return NULL;
    XLockFreeListNode* newNode = NULL;
    if (dataCreatMethod) {
        void* temp = XContainer_calloc(this_list, 1, XContainerTypeSize(this_list));
        dataCreatMethod(temp, pvData);
        newNode = createNode(this_list, temp, dataCreatMethod);
        XContainer_free(this_list, temp);
    } else {
        newNode = createNode(this_list, pvData, dataCreatMethod);
    }
    if (newNode == NULL) return NULL;
    if (VXListBase_push_front_node(this_list, newNode)) return newNode;
    return NULL;
}

static XLockFreeListNode* VXListAtomic_push_back(XLockFreeList* this_list,
    void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL) return NULL;
    XLockFreeListNode* newNode = NULL;
    if (dataCreatMethod) {
        void* temp = XContainer_calloc(this_list, 1, XContainerTypeSize(this_list));
        dataCreatMethod(temp, pvData);
        newNode = createNode(this_list, temp, dataCreatMethod);
        XContainer_free(this_list, temp);
    } else {
        newNode = createNode(this_list, pvData, dataCreatMethod);
    }
    if (newNode == NULL) return NULL;
    if (VXListBase_push_back_node(this_list, newNode)) return newNode;
    return NULL;
}

/* 单线程语义：在 curNode 后插入 pvData */
static bool VXList_insert(XLockFreeList* this_list, XLockFreeListNode* curNode,
    void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL || curNode == NULL || pvData == NULL) return false;
    XLockFreeListNode* newNode = createNode(this_list, pvData, dataCreatMethod);
    if (newNode == NULL) return false;
    newNode->next = curNode->next;
    curNode->next = newNode;
    if (curNode == _load_tail(this_list)) {
        XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)newNode,
            XAtomic_MemoryOrder_Relaxed);
    }
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

static size_t VXList_insert_array(XLockFreeList* this_list, XLockFreeListNode* curNode,
    void* pvArray, size_t nCount, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL || curNode == NULL || pvArray == NULL || nCount == 0) return 0;
    size_t typeSize = XContainerTypeSize(this_list);
    size_t inserted = 0;
    for (size_t i = 0; i < nCount; i++) {
        void* elem = (char*)pvArray + i * typeSize;
        if (VXList_insert(this_list, curNode, elem, dataCreatMethod))
            inserted++;
    }
    return inserted;
}

static size_t VXContainer_size(const XLockFreeList* this_list)
{
    return XAtomic_load_size_t(&XContainerSize(this_list), XAtomic_MemoryOrder_Relaxed);
}

/* 单线程语义：删除尾节点 */
static bool VXListAtomic_pop_back(XLockFreeList* this_list)
{
    if (this_list == NULL) return false;
    XLockFreeListNode* head = _load_head(this_list);
    XLockFreeListNode* first = _load_next(head);
    if (first == NULL) return false;
    /* 找到 tail 与其前驱 */
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = first;
    while (cur->next != NULL) { prev = cur; cur = cur->next; }
    prev->next = NULL;
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
        XAtomic_MemoryOrder_Relaxed);
    destroyNode(this_list, cur);
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

/* 单线程语义：根据迭代器删除节点 */
static void VXListAtomic_erase(XLockFreeList* this_list,
    const XLockFreeList_iterator* it, XLockFreeList_iterator* nextIt)
{
    if (nextIt) nextIt->node = NULL;
    if (this_list == NULL || it == NULL || it->node == NULL) return;
    XLockFreeListNode* target = (XLockFreeListNode*)it->node;
    XLockFreeListNode* head = _load_head(this_list);
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = _load_next(head);
    while (cur != NULL && cur != target) { prev = cur; cur = cur->next; }
    if (cur == NULL) return;
    prev->next = cur->next;
    if (cur == _load_tail(this_list)) {
        XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
            XAtomic_MemoryOrder_Relaxed);
    }
    if (nextIt) nextIt->node = cur->next;
    destroyNode(this_list, cur);
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
}

/* 单线程语义：按值删除首个匹配 */
static bool VXListAtomic_remove(XLockFreeList* this_list, void* pvData)
{
    if (this_list == NULL || XContainerCompare(this_list) == NULL) return false;
    XLockFreeListNode* head = _load_head(this_list);
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = _load_next(head);
    while (cur != NULL) {
        if (XContainerCompare(this_list)(&cur->data, pvData) == XCompare_Equality) {
            prev->next = cur->next;
            if (cur == _load_tail(this_list)) {
                XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
                    XAtomic_MemoryOrder_Relaxed);
            }
            destroyNode(this_list, cur);
            XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            return true;
        }
        prev = cur; cur = cur->next;
    }
    return false;
}

/* 单线程语义：清空所有实节点，保留哨兵 */
static void VXListAtomic_clear(XLockFreeList* this_list)
{
    if (this_list == NULL) return;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return;
    XLockFreeListNode* cur = _load_next(head);
    head->next = NULL;
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)head,
        XAtomic_MemoryOrder_Relaxed);
    while (cur != NULL) {
        XLockFreeListNode* next = cur->next;
        destroyNode(this_list, cur);
        cur = next;
    }
    XAtomic_store_size_t(&XContainerSize(this_list), 0, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(this_list), 0, XAtomic_MemoryOrder_Relaxed);
}

static void* VXListAtomic_front(XLockFreeList* this_list)
{
    if (this_list == NULL) return NULL;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return NULL;
    XLockFreeListNode* first = _load_next(head);
    if (first == NULL) return NULL;
    return &first->data;
}

static void* VXListAtomic_back(XLockFreeList* this_list)
{
    if (this_list == NULL) return NULL;
    XLockFreeListNode* head = _load_head(this_list);
    XLockFreeListNode* tail = _load_tail(this_list);
    if (tail == NULL || tail == head) return NULL;
    return &tail->data;
}

static bool VXListAtomic_find(const XLockFreeList* this_list, void* pvData,
    XLockFreeList_iterator* it)
{
    if (this_list == NULL || pvData == NULL) {
        if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
        return false;
    }
    if (XContainerCompare(this_list) == NULL) {
        if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
        return false;
    }
    XLockFreeListNode* head = _load_head((XLockFreeList*)this_list);
    XLockFreeListNode* cur = head ? _load_next(head) : NULL;
    while (cur != NULL) {
        if (XContainerCompare(this_list)(&cur->data, pvData) == XCompare_Equality) {
            if (it) it->node = cur;
            return true;
        }
        cur = cur->next;
    }
    if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
    return false;
}

/* --- 简单归并排序（单线程） --- */
static XLockFreeListNode* _lfl_split(XLockFreeListNode* head)
{
    XLockFreeListNode* slow = head;
    XLockFreeListNode* fast = head->next;
    while (fast != NULL && fast->next != NULL) {
        slow = slow->next;
        fast = fast->next->next;
    }
    XLockFreeListNode* mid = slow->next;
    slow->next = NULL;
    return mid;
}
static XLockFreeListNode* _lfl_merge(XLockFreeListNode* a, XLockFreeListNode* b,
    size_t typeSize, XCompare cmp, XSortOrder order)
{
    (void)typeSize;
    XLockFreeListNode dummy = {0};
    XLockFreeListNode* tail = &dummy;
    while (a && b) {
        int c = cmp(&a->data, &b->data);
        bool takeA = (order == XSORT_ASC) ? (c != XCompare_Greater) : (c != XCompare_Less);
        if (takeA) { tail->next = a; a = a->next; }
        else       { tail->next = b; b = b->next; }
        tail = tail->next;
    }
    tail->next = a ? a : b;
    return dummy.next;
}
static XLockFreeListNode* _lfl_mergesort(XLockFreeListNode* head, size_t typeSize,
    XCompare cmp, XSortOrder order)
{
    if (head == NULL || head->next == NULL) return head;
    XLockFreeListNode* mid = _lfl_split(head);
    XLockFreeListNode* left  = _lfl_mergesort(head, typeSize, cmp, order);
    XLockFreeListNode* right = _lfl_mergesort(mid,  typeSize, cmp, order);
    return _lfl_merge(left, right, typeSize, cmp, order);
}
static void VXListAtomic_sort(XLockFreeList* this_list, XSortOrder order)
{
    if (this_list == NULL || XContainerCompare(this_list) == NULL) return;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return;
    XLockFreeListNode* first = head->next;
    if (first == NULL || first->next == NULL) return;
    XLockFreeListNode* sorted = _lfl_mergesort(first,
        XContainerTypeSize(this_list), XContainerCompare(this_list), order);
    head->next = sorted;
    XLockFreeListNode* newTail = head;
    while (newTail->next != NULL) newTail = newTail->next;
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)newTail,
        XAtomic_MemoryOrder_Relaxed);
}

/* --- Qt 6.8 对齐父类虚函数：removeAll / removeOne / indexOf / lastIndexOf / removeIf --- */
static size_t VXList_remove_all(XLockFreeList* this_list, const void* pvData)
{
    if (this_list == NULL || pvData == NULL) return 0;
    if (XContainerCompare(this_list) == NULL) return 0;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return 0;
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = head->next;
    size_t removed = 0;
    while (cur != NULL) {
        XLockFreeListNode* next = cur->next;
        if (XContainerCompare(this_list)(&cur->data, pvData) == XCompare_Equality) {
            prev->next = next;
            destroyNode(this_list, cur);
            removed++;
            XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
        } else {
            prev = cur;
        }
        cur = next;
    }
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
        XAtomic_MemoryOrder_Relaxed);
    return removed;
}

static bool VXList_remove_one(XLockFreeList* this_list, const void* pvData)
{
    if (this_list == NULL || pvData == NULL) return false;
    if (XContainerCompare(this_list) == NULL) return false;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return false;
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = head->next;
    while (cur != NULL) {
        if (XContainerCompare(this_list)(&cur->data, pvData) == XCompare_Equality) {
            prev->next = cur->next;
            if (cur == _load_tail(this_list))
                XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
                    XAtomic_MemoryOrder_Relaxed);
            destroyNode(this_list, cur);
            XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            return true;
        }
        prev = cur; cur = cur->next;
    }
    return false;
}

static bool VXList_indexOf(const XLockFreeList* this_list, const void* findVal,
    size_t from, XLockFreeList_iterator* it)
{
    if (this_list == NULL || findVal == NULL || XContainerCompare(this_list) == NULL) {
        if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
        return false;
    }
    XLockFreeListNode* head = _load_head((XLockFreeList*)this_list);
    XLockFreeListNode* cur = head ? head->next : NULL;
    size_t idx = 0;
    while (cur != NULL) {
        if (idx >= from &&
            XContainerCompare(this_list)(&cur->data, findVal) == XCompare_Equality) {
            if (it) it->node = cur;
            return true;
        }
        cur = cur->next; idx++;
    }
    if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
    return false;
}

static bool VXList_lastIndexOf(const XLockFreeList* this_list, const void* findVal,
    size_t from, XLockFreeList_iterator* it)
{
    if (this_list == NULL || findVal == NULL || XContainerCompare(this_list) == NULL) {
        if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
        return false;
    }
    XLockFreeListNode* head = _load_head((XLockFreeList*)this_list);
    XLockFreeListNode* cur = head ? head->next : NULL;
    XLockFreeListNode* found = NULL;
    size_t idx = 0;
    while (cur != NULL) {
        if ((from == (size_t)-1 || idx <= from) &&
            XContainerCompare(this_list)(&cur->data, findVal) == XCompare_Equality) {
            found = cur;
        }
        cur = cur->next; idx++;
    }
    if (found) { if (it) it->node = found; return true; }
    if (it) *it = XLockFreeList_end((XLockFreeList*)this_list);
    return false;
}

static size_t VXList_removeIf(XLockFreeList* this_list,
    bool (*predicate)(const void* elemData, void* userData), void* userData)
{
    if (this_list == NULL || predicate == NULL) return 0;
    XLockFreeListNode* head = _load_head(this_list);
    if (head == NULL) return 0;
    XLockFreeListNode* prev = head;
    XLockFreeListNode* cur = head->next;
    size_t removed = 0;
    while (cur != NULL) {
        XLockFreeListNode* next = cur->next;
        if (predicate(&cur->data, userData)) {
            prev->next = next;
            destroyNode(this_list, cur);
            removed++;
            XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
            XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
        } else {
            prev = cur;
        }
        cur = next;
    }
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)prev,
        XAtomic_MemoryOrder_Relaxed);
    return removed;
}

/* --- copy / move / swap（单线程） --- */
static void VXClass_copy(XLockFreeList* object, const XLockFreeList* src)
{
    if (object == NULL || src == NULL) return;
    if (XClassIsVtableNull(object)) {
        XLockFreeList_init_with_memory(object, XContainerTypeSize(src),
            XContainer_memory_type(src));
    } else if (!XListBase_isEmpty_base(object)) {
        XListBase_clear_base(object);
    }
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
    for_each_iterator(src, XLockFreeList, it) {
        XListBase_push_back_base(object, XLockFreeList_iterator_data(&it));
    }
}

static void VXClass_move(XLockFreeList* object, XLockFreeList* src)
{
    if (object == NULL || src == NULL) return;
    XMemory* source_memory = Class_Memory(src);
    bool target_uninitialized = XClassIsVtableNull(object);
    XMemory* target_memory = target_uninitialized ? NULL : Class_Memory(object);
    if (target_uninitialized) {
        XLockFreeList_init_with_memory(object, XContainerTypeSize(src),
            XContainer_memory_type(src));
    } else if (!XListBase_isEmpty_base(object)) {
        XListBase_clear_base(object);
    }
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XLockFreeList) - sizeof(XClass));
    Class_Memory(object) = source_memory;
    if (!target_uninitialized)
        Class_Memory(src) = target_memory;
}

static void VXLockFreeList_swap(XLockFreeList* list1, XLockFreeList* list2)
{
    if (list1 == NULL || list2 == NULL || list1 == list2) return;
    size_t h1 = XAtomic_load_size_t(&list1->m_head, XAtomic_MemoryOrder_Relaxed);
    size_t t1 = XAtomic_load_size_t(&list1->m_tail, XAtomic_MemoryOrder_Relaxed);
    size_t s1 = XAtomic_load_size_t(&XContainerSize(list1), XAtomic_MemoryOrder_Relaxed);
    size_t c1 = XAtomic_load_size_t(&XContainerCapacity(list1), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list1->m_head,
        XAtomic_load_size_t(&list2->m_head, XAtomic_MemoryOrder_Relaxed),
        XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list1->m_tail,
        XAtomic_load_size_t(&list2->m_tail, XAtomic_MemoryOrder_Relaxed),
        XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerSize(list1),
        XAtomic_load_size_t(&XContainerSize(list2), XAtomic_MemoryOrder_Relaxed),
        XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list1),
        XAtomic_load_size_t(&XContainerCapacity(list2), XAtomic_MemoryOrder_Relaxed),
        XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list2->m_head, h1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list2->m_tail, t1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerSize(list2), s1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list2), c1, XAtomic_MemoryOrder_Relaxed);
}

/* --- init / deinit --- */
static void VXListAtomic_deinit(XLockFreeList* this_list)
{
    if (this_list == NULL) return;
    VXListAtomic_clear(this_list);
    /* 释放哨兵节点。此时其他线程不应再访问该表；退休链在 clear/destroyNode
     * 后无残留（clear 走单线程语义直接 free）。为保险起见，扫描一次本线程
     * 退休链。*/
    _lfl_scan_and_reclaim();
    XLockFreeListNode* head = _load_head(this_list);
    if (head) _lfl_free_node_memory(head, XContainer_memory(this_list));
    XAtomic_store_size_t(&this_list->m_head, 0, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
}

XLockFreeList* XLockFreeList_create_ex(XMemoryType memory, size_t typeSize)
{
    if (typeSize == 0) return NULL;
    XMemory* memoryMethod = XMemory_method(memory);
    XLockFreeList* this_list = (XLockFreeList*)XLockFreeList_aligned_malloc(
        sizeof(XLockFreeList), CACHE_LINE_SIZE, memoryMethod);
    if (this_list == NULL) return NULL;
    XLockFreeList_init_with_memory(this_list, typeSize, memory);
    Set_Class_IsHeap(this_list, true);
    return this_list;
}

void XLockFreeList_delete_base(XLockFreeList* this_list)
{
    if (!this_list) return;
    XMemory* memory = Class_Memory(this_list);
    XClass_deinit_base((XClass*)this_list);
    XLockFreeList_aligned_free(this_list, memory);
}

void XLockFreeList_init(XLockFreeList* this_list, size_t typeSize)
{
    XLockFreeList_init_with_memory(this_list, typeSize,
        XCLASS_DEFAULT_MEMORY_TYPE);
}

static void XLockFreeList_init_with_memory(XLockFreeList* this_list,
    size_t typeSize, XMemoryType memoryType)
{
    if (this_list == NULL || typeSize == 0) return;
    XListBase_init(this_list, typeSize, false);
    XClassGetVtable(this_list) = XLockFreeList_class_init();
    Set_Class_Memory(this_list, memoryType);
    XAtomic_init(this_list->m_head, (size_t)0);
    XAtomic_init(this_list->m_tail, (size_t)0);
    /* 分配哨兵节点：m_head=m_tail=sentinel */
    XLockFreeListNode* sentinel = createSentinel(this_list);
    XAtomic_store_size_t(&this_list->m_head, (size_t)(uintptr_t)sentinel,
        XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&this_list->m_tail, (size_t)(uintptr_t)sentinel,
        XAtomic_MemoryOrder_Relaxed);
}

#endif // XLockFreeList_ON
