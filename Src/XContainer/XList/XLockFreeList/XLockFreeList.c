#include"XLockFreeList.h"
#if XLockFreeList_ON
#include <stdlib.h>
#include <string.h>
#include "XStack.h"
#include "XLockFreeListConfig.h"
#include"XAlgorithm.h"
static inline tagged_ptr_t pack_ptr(void* ptr, uint32_t version);
static inline void* unpack_ptr(tagged_ptr_t val);
static inline uint32_t unpack_version(tagged_ptr_t val);

// --- 64-bit Implementation ---
#if defined(__x86_64__) || defined(_M_X64)
static inline tagged_ptr_t pack_ptr(void* ptr, uint32_t version) {
    uintptr_t p = (uintptr_t)ptr;
    return (p & PTR_MASK) | ((uint64_t)version << PTR_BITS);
}
static inline void* unpack_ptr(tagged_ptr_t val) {
    return (void*)(val & PTR_MASK);
}
static inline uint32_t unpack_version(tagged_ptr_t val) {
    return (uint32_t)((val & VERSION_MASK) >> PTR_BITS);
}

// --- 32-bit Implementation ---
#else
static inline tagged_ptr_t pack_ptr(void* ptr, uint32_t version) {
    uintptr_t p = (uintptr_t)ptr;
    uint32_t version_low = version & VERSION_LOW_MASK;
    uint32_t version_high = (version >> PTR_ALIGN_BITS) & 1;
    return (p & PTR_MASK) | version_low | (version_high << VERSION_HIGH_SHIFT);
}
static inline void* unpack_ptr(size_t val)
{
    return (void*)(val & PTR_MASK);
}
static inline uint32_t unpack_version(tagged_ptr_t val) {
    uint32_t version_low = val & VERSION_LOW_MASK;
    uint32_t version_high = (val >> VERSION_HIGH_SHIFT) & 1;
    return version_low | (version_high << PTR_ALIGN_BITS);
}
#endif
// ==================== 跨平台 ABA 防护定义结束 ====================
// 内部函数声明
static bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node);
static bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node);
static XLockFreeListNode * VXListAtomic_push_front(XLockFreeList * this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
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

//#define CreatNode(this_list)    XMemory_malloc(ALIGN_UP(sizeof(XListSNode)+XContainerTypeSize(this_list),sizeof(void*)))

// 创建新节点
static XLockFreeListNode* createNode(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    // 总需求大小 = header + 节点
    size_t needed_size = sizeof(void*) + sizeof(XLockFreeListNode) + XContainerTypeSize(this_list);
    // 分配足够的内存，并加上最大填充（align-1）以确保能找到对齐位置
    void* raw_buffer = XMemory_malloc(needed_size + 7); // +7 是为了8字节对齐的安全边际
    if (raw_buffer == NULL) {
        return NULL;
    }

    // 在 raw_buffer + sizeof(void*) 之后找对齐地址
    uintptr_t candidate = (uintptr_t)raw_buffer + sizeof(void*);
    uintptr_t aligned_addr = (candidate + 7) & (~7ULL);

    XLockFreeListNode* newNode = (XLockFreeListNode*)aligned_addr;
    if (dataCreatMethod)
        dataCreatMethod(&newNode->data, pvData);
    else
        memcpy(&newNode->data, pvData, XContainerTypeSize(this_list));
    newNode->next = NULL;

    // 将原始指针保存在对齐地址之前的固定位置
    // 现在，aligned_addr - sizeof(void*) >= raw_buffer，因为 candidate >= raw_buffer+8
    // 且 aligned_addr >= candidate, 所以 aligned_addr - 8 >= raw_buffer.
    void** header = (void**)(aligned_addr - sizeof(void*));
    *header = raw_buffer;

    return newNode;
}
// 对应的辅助释放函数
static void destroyNode(XLockFreeListNode* node) {
    if (node == NULL) return;
    void** header = (void**)((char*)node - sizeof(void*));
    void* raw_buffer = *header;
    XMemory_free(raw_buffer);
}
// 类初始化
XVtable* XLockFreeList_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XLISTSLINKED_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XContainer_class_init());

    void* table[] = {
        // 插入操作
        VXListAtomic_push_front,VXListBase_push_front_node,
        VXListAtomic_push_back,VXListBase_push_back_node,
        VXList_insert,
        VXList_insert_array,
        // 删除操作
        VXListAtomic_pop_front, VXListAtomic_pop_back, VXListAtomic_erase, VXListAtomic_remove,
        // 遍历操作
        VXListAtomic_front, VXListAtomic_back, VXListAtomic_find,
        // 排序
        VXListAtomic_sort
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXListAtomic_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXListAtomic_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Swap, VXLockFreeList_swap);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXContainer_size);
#if SHOWCONTAINERSIZE
    printf("XLockFreeList size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;

    tagged_ptr_t expected, desired;
    XLockFreeListNode* oldHead;

    do {
        expected = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        oldHead = (XLockFreeListNode*)unpack_ptr(expected);
        node->next = oldHead;

        uint32_t new_version = (unpack_version(expected) + 1) & ((1U << VERSION_BITS) - 1);
        desired = pack_ptr(node, new_version);

    } while (!XAtomic_compare_exchange_strong_size_t(
        &this_list->m_head, &expected, desired,
        XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    // 只有在链表从空变非空时，才需要设置 tail
    if (oldHead == NULL) {
        tagged_ptr_t newTail = pack_ptr(node, unpack_version(expected)); // 或者直接用0
        XAtomic_store_size_t(&this_list->m_tail, newTail, XAtomic_MemoryOrder_Relaxed);
    }
    // 注意：不再有任何关于 tail_prev 的逻辑

    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    tagged_ptr_t tail_tagged;
    XLockFreeListNode* tail;

    while (1) {
        // --- 关键修改：使用 XAtomic_load_size_t ---
        tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
        tail = (XLockFreeListNode*)unpack_ptr(tail_tagged);

        if (tail == NULL) {
            tagged_ptr_t expected_head = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
            XLockFreeListNode* oldHead = (XLockFreeListNode*)unpack_ptr(expected_head);
            if (oldHead == NULL) {
                uint32_t new_version = (unpack_version(expected_head) + 1) & ((1U << VERSION_BITS) - 1);
                tagged_ptr_t desired_head = pack_ptr(node, new_version);
                if (XAtomic_compare_exchange_strong_size_t(
                    &this_list->m_head, &expected_head, desired_head, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    tagged_ptr_t newTail = pack_ptr(node, 0);
                    XAtomic_store_size_t(&this_list->m_tail, newTail, XAtomic_MemoryOrder_Relaxed);
                    // XAtomic_store_uintptr_t(&this_list->m_tail_prev, NULL, XAtomic_MemoryOrder_Relaxed); // 已移除
                    break;
                }
            }
        }
        else {
            XLockFreeListNode* next = (XLockFreeListNode*)XAtomic_load_uintptr_t(&tail->next, XAtomic_MemoryOrder_Relaxed);
            if (next == NULL) {
                if (XAtomic_compare_exchange_strong_uintptr_t(
                    &tail->next, (void**)&next, node, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    uint32_t new_version = (unpack_version(tail_tagged) + 1) & ((1U << VERSION_BITS) - 1);
                    tagged_ptr_t desired_tail = pack_ptr(node, new_version);
                    XAtomic_compare_exchange_strong_size_t(
                        &this_list->m_tail, &tail_tagged, desired_tail, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);

                    // XAtomic_store_uintptr_t(&this_list->m_tail_prev, tail, XAtomic_MemoryOrder_Relaxed); // 已移除
                    break;
                }
            }
            else {
                tagged_ptr_t new_tail_tagged = pack_ptr(next, 0);
                XAtomic_compare_exchange_strong_size_t(
                    &this_list->m_tail, &tail_tagged, new_tail_tagged, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
            }
        }
    }

    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

// 头部插入（多生产者安全）
XLockFreeListNode* VXListAtomic_push_front(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XLockFreeListNode* newNode = NULL;
    if (dataCreatMethod)
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        dataCreatMethod(temp, pvData);
        newNode = createNode(this_list, temp, dataCreatMethod);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData, dataCreatMethod);
    }
    if (newNode == NULL)
        return NULL;

    if (VXListBase_push_front_node(this_list, newNode))
        return newNode;
    return NULL;
}

// 尾部插入（多生产者安全）
XLockFreeListNode* VXListAtomic_push_back(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XLockFreeListNode* newNode = NULL;
    if (dataCreatMethod)
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        dataCreatMethod(temp, pvData);
        newNode = createNode(this_list, temp, dataCreatMethod);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData, dataCreatMethod);
    }
    if (newNode == NULL)
        return NULL;
    if (VXListBase_push_back_node(this_list, newNode))
        return newNode;
    return NULL;
}

bool VXList_insert(XLockFreeList* this_list, XLockFreeListNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL || curNode == NULL || pvData == NULL) return false;

    // 正确加载并解包 m_head
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* current = (XLockFreeListNode*)unpack_ptr(head_tagged);

    XLockFreeListNode* prev = NULL;

    // 查找指定节点的前一个节点
    while (current != NULL && current != curNode) {
        prev = current;
        current = (XLockFreeListNode*)XAtomic_load_uintptr_t(&current->next, XAtomic_MemoryOrder_Relaxed);
    }

    // 如果没有找到指定节点，直接返回
    if (current == NULL) return false;

    // 创建新节点
    XLockFreeListNode* newNode = createNode(this_list, pvData, dataCreatMethod);
    if (newNode == NULL) return false;

    // 如果要插入的位置是头节点
    if (prev == NULL) {
        tagged_ptr_t expected_head, desired_head;
        do {
            expected_head = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
            current = (XLockFreeListNode*)unpack_ptr(expected_head);
            newNode->next = current;
            uint32_t new_version = (unpack_version(expected_head) + 1) & ((1U << VERSION_BITS) - 1);
            desired_head = pack_ptr(newNode, new_version);
        } while (!XAtomic_compare_exchange_strong_size_t(
            &this_list->m_head, &expected_head, desired_head,
            XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
    }
    else {
        // 插入到非头部位置
        XLockFreeListNode* expected_next;
        do {
            expected_next = (XLockFreeListNode*)XAtomic_load_uintptr_t(&prev->next, XAtomic_MemoryOrder_Relaxed);
            newNode->next = expected_next;
        } while (!XAtomic_compare_exchange_strong_uintptr_t(
            &prev->next, (void**)&expected_next, newNode, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

size_t VXList_insert_array(XLockFreeList* this_list, XLockFreeListNode* curNode, void* pvArray, size_t nCount, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL || pvArray == NULL || nCount == 0) return 0;

    // 正确加载并解包 m_head
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* current = (XLockFreeListNode*)unpack_ptr(head_tagged);

    XLockFreeListNode* prev = NULL;

    // 查找指定节点的前一个节点
    if (curNode != NULL) {
        while (current != NULL && current != curNode) {
            prev = current;
            current = (XLockFreeListNode*)XAtomic_load_uintptr_t(&current->next, XAtomic_MemoryOrder_Relaxed);
        }
        // 如果没有找到指定节点，直接返回
        if (current == NULL) return 0;
    }

    // 创建新链表
    XLockFreeListNode* newListHead = NULL;
    XLockFreeListNode* newListTail = NULL;
    const char* pSrc = (const char*)pvArray;
    size_t insertedCount = 0;

    for (size_t i = 0; i < nCount; ++i) {
        XLockFreeListNode* newNode = createNode(this_list, (void*)(pSrc + i * XContainerTypeSize(this_list)), dataCreatMethod);
        if (newNode == NULL) break;

        if (newListHead == NULL) {
            newListHead = newNode;
        }
        else {
            newListTail->next = newNode;
        }
        newListTail = newNode;
        insertedCount++;
    }

    if (insertedCount == 0) return 0;

    // 插入新链表
    if (prev == NULL) {
        // 插入到链表头部
        tagged_ptr_t expected_head, desired_head;
        do {
            expected_head = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
            current = (XLockFreeListNode*)unpack_ptr(expected_head);
            newListTail->next = current;
            uint32_t new_version = (unpack_version(expected_head) + 1) & ((1U << VERSION_BITS) - 1);
            desired_head = pack_ptr(newListHead, new_version);
        } while (!XAtomic_compare_exchange_strong_size_t(
            &this_list->m_head, &expected_head, desired_head,
            XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

        // 如果原链表为空，需要更新tail
        if (current == NULL) {
            tagged_ptr_t new_tail = pack_ptr(newListTail, 0);
            XAtomic_store_size_t(&this_list->m_tail, new_tail, XAtomic_MemoryOrder_Relaxed);
        }
    }
    else {
        // 插入到非头部位置
        XLockFreeListNode* expected_next;
        do {
            expected_next = (XLockFreeListNode*)XAtomic_load_uintptr_t(&prev->next, XAtomic_MemoryOrder_Relaxed);
            newListTail->next = expected_next;
        } while (!XAtomic_compare_exchange_strong_uintptr_t(
            &prev->next, (void**)&expected_next, newListHead, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

        // 如果插入点是原tail，需要更新tail
        if (expected_next == NULL) {
            tagged_ptr_t tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
            XLockFreeListNode* current_tail = (XLockFreeListNode*)unpack_ptr(tail_tagged);
            if (current_tail == prev) {
                tagged_ptr_t new_tail = pack_ptr(newListTail, (unpack_version(tail_tagged) + 1) & ((1U << VERSION_BITS) - 1));
                XAtomic_store_size_t(&this_list->m_tail, new_tail, XAtomic_MemoryOrder_Relaxed);
            }
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), insertedCount, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), insertedCount, XAtomic_MemoryOrder_Relaxed);
    return insertedCount;
}
size_t VXContainer_size(const XLockFreeList* this_list)
{
    return XAtomic_load_size_t(&XContainerSize(this_list),XAtomic_MemoryOrder_Relaxed);
}
// 头部删除
bool VXListAtomic_pop_front(XLockFreeList* this_list)
{
    if (XLockFreeList_isEmpty_base(this_list)) return false;

    tagged_ptr_t expected, desired;
    XLockFreeListNode* oldHead;
    XLockFreeListNode* next;

    do {
        // --- 关键修改：使用 XAtomic_load_size_t ---
        expected = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        oldHead = (XLockFreeListNode*)unpack_ptr(expected);
        if (oldHead == NULL) return false;

        next = oldHead->next;
        uint32_t new_version = (unpack_version(expected) + 1) & ((1U << VERSION_BITS) - 1);
        desired = pack_ptr(next, new_version);

    } while (!XAtomic_compare_exchange_strong_size_t(
        &this_list->m_head, &expected, desired,
        XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    if (next == NULL) {
        XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
        // XAtomic_store_uintptr_t(&this_list->m_tail_prev, NULL, XAtomic_MemoryOrder_Relaxed); // 已移除
    }
    // else if (next->next == NULL) {
    //     XAtomic_store_uintptr_t(&this_list->m_tail_prev, NULL, XAtomic_MemoryOrder_Relaxed); // 已移除
    // }

    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&oldHead->data);
    }
    destroyNode(oldHead);

    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

// 尾部删除
bool VXListAtomic_pop_back(XLockFreeList* this_list) 
{
    //printf("\n--- 开始执行 pop_back (无 tail_prev 版本) ---\n");

    if (XLockFreeList_isEmpty_base(this_list)) {
        //printf("  -> 链表为空\n");
        return false;
    }

    // 1. 尝试处理只有一个元素的情况
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* head = (XLockFreeListNode*)unpack_ptr(head_tagged);

    if (head != NULL && head->next == NULL) {
        //printf("  -> 检测到只有一个元素\n");
        uint32_t new_version = (unpack_version(head_tagged) + 1) & ((1U << VERSION_BITS) - 1);
        tagged_ptr_t desired_head = pack_ptr(NULL, new_version);
        if (XAtomic_compare_exchange_strong_size_t(
            &this_list->m_head, &head_tagged, desired_head, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {

            // 同时清空 tail
            XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
            goto success_single;
        }
        else {
            //printf("  -> CAS 失败，可能被其他线程修改\n");
            return false;
        }
    }

    // 2. 链表有多个元素，需要遍历找到 tail 和 tail_prev
    XLockFreeListNode* tail_prev = NULL;
    XLockFreeListNode* tail = head;

    // 单线程下安全遍历
    while (tail->next != NULL) {
        tail_prev = tail;
        tail = tail->next;
    }
    //printf("  -> 遍历找到: tail_prev=%p, tail=%p\n", (void*)tail_prev, (void*)tail);

    // 3. 尝试将 tail_prev->next 设为 NULL
    XLockFreeListNode* expected_next = tail;
    if (!XAtomic_compare_exchange_strong_uintptr_t(
        &tail_prev->next, (void**)&expected_next, NULL, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
        //printf("  -> CAS 修改 tail_prev->next 失败\n");
        return false;
    }

    // 4. 更新全局的 m_tail 指针
    tagged_ptr_t current_tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
    uint32_t new_tail_version = (unpack_version(current_tail_tagged) + 1) & ((1U << VERSION_BITS) - 1);
    tagged_ptr_t new_tail = pack_ptr(tail_prev, new_tail_version);

    // 这里可以使用 store，因为我们已经通过 CAS 确保了我们是合法的弹出者
    XAtomic_store_size_t(&this_list->m_tail, new_tail, XAtomic_MemoryOrder_Relaxed);

    // 5. 清理资源
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&tail->data);
    }
    destroyNode(tail);

    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    //printf("  -> pop_back 成功\n");
    return true;

success_single:
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&head->data);
    }
    destroyNode(head);
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    //printf("  -> pop_back (single) 成功\n");
    return true;
}

// 删除指定节点
void VXListAtomic_erase(XLockFreeList* this_list, const XLockFreeList_iterator* it, XLockFreeList_iterator* nextIt) 
{
    if (XLockFreeList_isEmpty_base(this_list) || it == NULL) return;

    XLockFreeListNode* node = it->node;
    XLockFreeListNode* prev = NULL;
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_uintptr_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    while (current != NULL)
    {
        if (current == node) 
        {
            // 如果要删除的是头节点
            if (prev == NULL) 
            {
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_uintptr_t(
                    &this_list->m_head, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_uintptr_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_uintptr_t(&this_list->m_tail, next, XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    if (nextIt)
                        nextIt->node = next;
                }
            }
            else 
            {
                // 不是头节点
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_uintptr_t(
                    &prev->next, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed))
                {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_uintptr_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_uintptr_t(&this_list->m_tail, prev, XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    if (nextIt)
                        nextIt->node = next;
                }
            }
            return;
        }

        prev = current;
        current = current->next;
    }
}

// 删除指定数据的节点
bool VXListAtomic_remove(XLockFreeList* this_list, void* pvData) {
    if (XLockFreeList_isEmpty_base(this_list))
        return false;
    if (XContainerCompare(this_list) == NULL)
        return false;

    // --- 修正: 正确加载并解包 m_head ---
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* current = (XLockFreeListNode*)unpack_ptr(head_tagged);

    XLockFreeListNode* prev = NULL;

    while (current != NULL) {
        if (XContainerCompare(this_list)(&current->data, pvData) == XCompare_Equality) {
            // 如果要删除的是头节点
            if (prev == NULL) {
                XLockFreeListNode* next = current->next;
                // --- 修正: 使用 size_t 版本的 CAS ---
                tagged_ptr_t expected_head = head_tagged; // 使用之前加载的完整tagged值
                tagged_ptr_t desired_head = pack_ptr(next, unpack_version(expected_head) + 1);
                if (XAtomic_compare_exchange_strong_size_t(
                    &this_list->m_head, &expected_head, desired_head,
                    XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {

                    // 如果删除的是尾节点，更新尾指针
                    tagged_ptr_t tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
                    if (current == (XLockFreeListNode*)unpack_ptr(tail_tagged)) {
                        XAtomic_store_size_t(&this_list->m_tail, pack_ptr(next, unpack_version(tail_tagged) + 1), XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    destroyNode(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    return true;
                }
                else {
                    // CAS失败，链表已被其他线程修改，重新开始
                    return VXListAtomic_remove(this_list, pvData);
                }
            }
            else {
                // 不是头节点
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_uintptr_t(
                    &prev->next, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {

                    // 如果删除的是尾节点，更新尾指针
                    tagged_ptr_t tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
                    if (current == (XLockFreeListNode*)unpack_ptr(tail_tagged)) {
                        XAtomic_store_size_t(&this_list->m_tail, pack_ptr(prev, unpack_version(tail_tagged) + 1), XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    destroyNode(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    return true;
                }
                // 如果CAS失败，继续循环尝试下一个匹配项或重试
            }
        }

        prev = current;
        current = current->next;
    }
    return false;
}

// 清空链表
void VXListAtomic_clear(XLockFreeList* this_list) {
    tagged_ptr_t current_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* current = (XLockFreeListNode*)unpack_ptr(current_tagged);

    while (current != NULL) {
        XLockFreeListNode* next = current->next;
        if (XContainerDataDeinitMethod(this_list) != NULL) {
            XContainerDataDeinitMethod(this_list)(&current->data);
        }
        destroyNode(current);
        current = next;
    }

    XAtomic_store_size_t(&this_list->m_head, 0, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
    // XAtomic_store_uintptr_t(&this_list->m_tail_prev, NULL, XAtomic_MemoryOrder_Relaxed); // 已移除
    XAtomic_store_size_t(&XContainerSize(this_list), 0, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(this_list), 0, XAtomic_MemoryOrder_Relaxed);
}

// 获取链表头数据
void* VXListAtomic_front(XLockFreeList* this_list) {
    // --- 关键修改：正确加载并解包 head ---
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* head = (XLockFreeListNode*)unpack_ptr(head_tagged);
    if (head == NULL) return NULL;
    return &head->data;
}

// 获取链表尾数据
void* VXListAtomic_back(XLockFreeList* this_list) {
    // --- 关键修改：正确加载并解包 tail ---
    tagged_ptr_t tail_tagged = XAtomic_load_size_t(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* tail = (XLockFreeListNode*)unpack_ptr(tail_tagged);
    if (tail == NULL) return NULL;
    return &tail->data;
}

// 查找数据
bool VXListAtomic_find(const XLockFreeList* this_list, void* pvData, XLockFreeList_iterator* it) 
{
    if (this_list == NULL || pvData == NULL) {
        if (it)
            *it = XLockFreeList_end(this_list);
        return false;
    }

    if (XLockFreeList_isEmpty_base(this_list))
    {
        if (it)
            *it = XLockFreeList_end(this_list);
        return false;
    }

    // 正确加载并解包 m_head
    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* current = (XLockFreeListNode*)unpack_ptr(head_tagged);

    while (current != NULL)
    {
        if (XContainerCompare(this_list)(&current->data, pvData) == XCompare_Equality)
        {
            if (it)
                it->node= current;
            return true;
        }
        current = (XLockFreeListNode*)XAtomic_load_uintptr_t(&current->next, XAtomic_MemoryOrder_Relaxed);
    }

    if (it)
        *it = XLockFreeList_end(this_list);
    return false;
}

// 找到链表尾部节点
static XLockFreeListNode* findTail(XLockFreeListNode* head) {
    if (head == NULL) return NULL;
    while (XAtomic_load_uintptr_t(&head->next, XAtomic_MemoryOrder_Relaxed) != NULL)
        head = (XLockFreeListNode*)XAtomic_load_uintptr_t(&head->next, XAtomic_MemoryOrder_Relaxed);
    return head;
}

// 单向链表的一次快排（分区函数）
static XLockFreeListNode* List_OneSort(XLockFreeListNode* left, XLockFreeListNode* right, size_t typeSize, XCompare compare, XSortOrder order) 
{
    if (left == NULL || right == NULL || left == right)
        return left;

    void* pivot = XMemory_malloc(typeSize);
    if (pivot == NULL) return NULL;
    memcpy(pivot, &(left->data), typeSize);

    XLockFreeListNode* i = left;    // 分区点
    XLockFreeListNode* j = (XLockFreeListNode*)XAtomic_load_uintptr_t(&left->next, XAtomic_MemoryOrder_Relaxed);
    int32_t cmp;
    while (j != NULL) {
        //if (compare(&(j->data), pivot)) 
        cmp = compare(&(j->data), pivot);
        if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
        {
            i = (XLockFreeListNode*)XAtomic_load_uintptr_t(&i->next, XAtomic_MemoryOrder_Relaxed);
            // 交换i和j的数据
            void* temp = XMemory_malloc(typeSize);
            memcpy(temp, &(i->data), typeSize);
            memcpy(&(i->data), &(j->data), typeSize);
            memcpy(&(j->data), temp, typeSize);
            XMemory_free(temp);
        }
        if (j == right) break;  // 到达右边界
        j = (XLockFreeListNode*)XAtomic_load_uintptr_t(&j->next, XAtomic_MemoryOrder_Relaxed);
    }

    // 将pivot放到正确位置
    void* temp = XMemory_malloc(typeSize);
    memcpy(temp, &(i->data), typeSize);
    memcpy(&(i->data), &(left->data), typeSize);
    memcpy(&(left->data), temp, typeSize);
    XMemory_free(temp);
    XMemory_free(pivot);

    return i;  // 返回分区点
}

// 链表排序
void VXListAtomic_sort(XLockFreeList* this_list, XSortOrder order) 
{
#if XStack_ON
    if (XLockFreeList_isEmpty_base(this_list) || XContainerCompare(this_list) == NULL)
        return;

    tagged_ptr_t head_tagged = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* head = (XLockFreeListNode*)unpack_ptr(head_tagged);
    XLockFreeListNode* tail = findTail(head);

    // 使用现有的XStack
    XStack* stack = XStack_Create(XLockFreeListNode*);
    if (stack == NULL)
        return;

    // 初始化栈
    if (head != NULL) {
        XStack_push_base(stack, &tail);
        XStack_push_base(stack, &head);
    }

    while (!XStack_isEmpty_base(stack)) {
        // 弹出区间
        XLockFreeListNode* h = *((XLockFreeListNode**)XStack_top_base(stack));
        XStack_pop_base(stack);
        XLockFreeListNode* t = *((XLockFreeListNode**)XStack_top_base(stack));
        XStack_pop_base(stack);

        if (h == NULL || t == NULL || h == t)
            continue;

        // 执行一次快排
        XLockFreeListNode* pivot = List_OneSort(h, t, XContainerTypeSize(this_list), XContainerCompare(this_list), order);

        // 处理左子区间
        if (h != pivot) {
            XLockFreeListNode* leftTail = findTail(h);
            if (leftTail != NULL && h != leftTail) {
                XStack_push_base(stack, &leftTail);
                XStack_push_base(stack, &h);
            }
        }

        // 处理右子区间
        if (pivot != NULL && XAtomic_load_uintptr_t(&pivot->next, XAtomic_MemoryOrder_Relaxed) != NULL) {
            XLockFreeListNode* rightHead = (XLockFreeListNode*)XAtomic_load_uintptr_t(&pivot->next, XAtomic_MemoryOrder_Relaxed);
            XLockFreeListNode* rightTail = findTail(rightHead);
            if (rightTail != NULL && rightHead != rightTail) {
                XStack_push_base(stack, &rightTail);
                XStack_push_base(stack, &rightHead);
            }
        }
    }

    XStack_delete_base(stack);
#else
    IS_ON_DEBUG(XStack_ON);
#endif
}

void VXClass_copy(XLockFreeList* object, const XLockFreeList* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XLockFreeList_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
    for_each_iterator(src, XLockFreeList, it)
    {
        XListBase_push_back_base(object, XLockFreeList_iterator_data(&it));
    }
}

void VXClass_move(XLockFreeList* object, XLockFreeList* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XLockFreeList_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XLockFreeList) - sizeof(XClass));
}

// 释放链表
void VXListAtomic_deinit(XLockFreeList* this_list) {
    VXListAtomic_clear(this_list);
   //XMemory_free(this_list);
}

// 对外接口实现

XLockFreeList* XLockFreeList_create(size_t typeSize) {
    if (typeSize == 0) return NULL;
    XLockFreeList* this_list = (XLockFreeList*)XMemory_malloc(sizeof(XLockFreeList));
    if (this_list == NULL) return NULL;
    XLockFreeList_init(this_list, typeSize);
    Set_Class_MemoryFree(this_list, XFree);
    return this_list;
}

void XLockFreeList_init(XLockFreeList* this_list, size_t typeSize) {
    if (this_list == NULL || typeSize == 0) return;

    XListBase_init(this_list, typeSize);
    XClassGetVtable(this_list) = XLockFreeList_class_init();
    // --- 关键修改：初始化为 size_t 类型的 0 ---
    XAtomic_init(this_list->m_head, (size_t)0);
    XAtomic_init(this_list->m_tail, (size_t)0);
    //XAtomic_init(this_list->m_tail_prev, NULL);
}

bool XLockFreeList_pop_and_copy_front(XLockFreeList* this_list, void* pvOutData)
{
    if (this_list == NULL || pvOutData == NULL)
        return false;
    if (XLockFreeList_isEmpty_base(this_list))
        return false;

    tagged_ptr_t expected, desired;
    XLockFreeListNode* oldHead;
    XLockFreeListNode* next;

    do {
        expected = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        oldHead = (XLockFreeListNode*)unpack_ptr(expected);
        if (oldHead == NULL)
            return false;

        next = oldHead->next;
        uint32_t new_version = (unpack_version(expected) + 1) & ((1U << VERSION_BITS) - 1);
        desired = pack_ptr(next, new_version);

    } while (!XAtomic_compare_exchange_strong_size_t(
        &this_list->m_head, &expected, desired,
        XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    // 更新尾指针（如果链表变空）
    if (next == NULL) {
        XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
    }

    // --- 关键修改：在释放节点前拷贝数据 ---
    XCDataMoveMethod copyMethod = XContainerDataCopyMethod(this_list);
    if (copyMethod != NULL) {
        copyMethod(pvOutData, &oldHead->data);
    }
    else {
        // 如果没有移动方法，则使用拷贝
        memcpy(pvOutData, &oldHead->data, XContainerTypeSize(this_list));
    }

    // 清理资源
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&oldHead->data);
    }
    destroyNode(oldHead);

    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}
bool XLockFreeList_pop_and_move_front(XLockFreeList* this_list, void* pvOutData)
{
    if (this_list == NULL || pvOutData == NULL)
        return false;
    if (XLockFreeList_isEmpty_base(this_list))
        return false;

    tagged_ptr_t expected, desired;
    XLockFreeListNode* oldHead;
    XLockFreeListNode* next;

    do {
        expected = XAtomic_load_size_t(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        oldHead = (XLockFreeListNode*)unpack_ptr(expected);
        if (oldHead == NULL)
            return false;

        next = oldHead->next;
        uint32_t new_version = (unpack_version(expected) + 1) & ((1U << VERSION_BITS) - 1);
        desired = pack_ptr(next, new_version);

    } while (!XAtomic_compare_exchange_strong_size_t(
        &this_list->m_head, &expected, desired,
        XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    if (next == NULL) {
        XAtomic_store_size_t(&this_list->m_tail, 0, XAtomic_MemoryOrder_Relaxed);
    }

    // --- 关键修改：使用移动或拷贝语义处理数据 ---
    XCDataMoveMethod moveMethod = XContainerDataMoveMethod(this_list);
    if (moveMethod != NULL) {
        moveMethod(pvOutData, &oldHead->data);
    }
    else {
        // 如果没有移动方法，则使用拷贝
        memcpy(pvOutData, &oldHead->data, XContainerTypeSize(this_list));
    }

    // 注意：如果使用了移动语义，这里不应再调用析构函数
    //if (moveMethod == NULL && XContainerDataDeinitMethod(this_list) != NULL) {
    //    XContainerDataDeinitMethod(this_list)(&oldHead->data);
    //}
    destroyNode(oldHead);

    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}
void VXLockFreeList_swap(XLockFreeList* list1, XLockFreeList* list2)
{
    // 交换链表实现
    tagged_ptr_t tempHead = XAtomic_load_size_t(&list1->m_head, XAtomic_MemoryOrder_Relaxed);
    tagged_ptr_t tempTail = XAtomic_load_size_t(&list1->m_tail, XAtomic_MemoryOrder_Relaxed);
    // XLockFreeListNode* tempTailPrev = (XLockFreeListNode*)XAtomic_load_uintptr_t(&list1->m_tail_prev, XAtomic_MemoryOrder_Relaxed); // 已移除
    size_t tempSize = XAtomic_load_size_t(&XContainerSize(list1), XAtomic_MemoryOrder_Relaxed);
    size_t tempCapacity = XAtomic_load_size_t(&XContainerCapacity(list1), XAtomic_MemoryOrder_Relaxed);

    XAtomic_store_size_t(&list1->m_head, XAtomic_load_size_t(&list2->m_head, XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list1->m_tail, XAtomic_load_size_t(&list2->m_tail, XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    // XAtomic_store_uintptr_t(&list1->m_tail_prev, XAtomic_load_uintptr_t(&list2->m_tail_prev, XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed); // 已移除
    XAtomic_store_size_t(&XContainerSize(list1), XAtomic_load_size_t(&XContainerSize(list2), XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list1), XAtomic_load_size_t(&XContainerCapacity(list2), XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);

    XAtomic_store_size_t(&list2->m_head, tempHead, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&list2->m_tail, tempTail, XAtomic_MemoryOrder_Relaxed);
    // XAtomic_store_uintptr_t(&list2->m_tail_prev, tempTailPrev, XAtomic_MemoryOrder_Relaxed); // 已移除
    XAtomic_store_size_t(&XContainerSize(list2), tempSize, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list2), tempCapacity, XAtomic_MemoryOrder_Relaxed);
}

#endif // XLockFreeList_ON