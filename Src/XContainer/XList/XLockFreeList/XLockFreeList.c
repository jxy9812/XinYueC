#include"XLockFreeList.h"
#if XLockFreeList_ON
#include <stdlib.h>
#include <string.h>
#include "XStack.h"
#include"XAlgorithm.h"
// 内部函数声明
static bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node);
static bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node);
static XLockFreeListNode * VXListAtomic_push_front(XLockFreeList * this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static XLockFreeListNode* VXListAtomic_push_back(XLockFreeList* this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static bool VXList_insert(XLockFreeList* this_list, XLockFreeListNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod);
static size_t VXList_insert_array(XLockFreeList* this_list, XLockFreeListNode* curNode, void* array, size_t count, XCDataCreatMethod dataCreatMethod);

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
static XLockFreeListNode* createNode(XLockFreeList* this_list, void* pvData) {
    XLockFreeListNode* newNode = (XLockFreeListNode*)XMemory_malloc(ALIGN_UP(sizeof(XLockFreeListNode) + XContainerTypeSize(this_list), sizeof(void*)));
    if (newNode == NULL) {
        perror("创建节点失败");
        return NULL;
    }
    memcpy(&newNode->data, pvData, XContainerTypeSize(this_list));
    newNode->next = NULL;
    return newNode;
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
#if SHOWCONTAINERSIZE
    printf("XLockFreeList size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

bool VXListBase_push_front_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    XLockFreeListNode* oldHead;
    do {
        oldHead = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        node->next = oldHead;
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, node, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    // 如果链表原来是空的，更新尾指针
    if (oldHead == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, node, XAtomic_MemoryOrder_Relaxed);
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return false;
}

bool VXListBase_push_back_node(XLockFreeList* this_list, XLockFreeListNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    XLockFreeListNode* tail;
    while (1) {
        tail = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);

        // 如果链表为空，尝试更新头指针
        if (tail == NULL) {
            if (XAtomic_compare_exchange_strong_ptr(
                &this_list->m_head, (void**)&tail, node, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                XAtomic_store_ptr(&this_list->m_tail, node, XAtomic_MemoryOrder_Relaxed);
                break;
            }
        }
        else {
            // 尝试将新节点链接到尾部
            XLockFreeListNode* next = (XLockFreeListNode*)XAtomic_load_ptr(&tail->next, XAtomic_MemoryOrder_Relaxed);
            if (next == NULL) {
                if (XAtomic_compare_exchange_strong_ptr(
                    &tail->next, (void**)&next, node, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    // 链接成功后，尝试更新尾指针
                    XAtomic_compare_exchange_strong_ptr(
                        &this_list->m_tail, (void**)&tail, node, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
                    break;
                }
            }
            else {
                // 尾指针已过时，帮助推进
                XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_tail, (void**)&tail, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed);
            }
        }
    }

    // 更新记录数量
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
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
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
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
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

    XLockFreeListNode* prev = NULL;
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    // 查找指定节点的前一个节点
    while (current != NULL && current != curNode) {
        prev = current;
        current = (XLockFreeListNode*)XAtomic_load_ptr(&current->next, XAtomic_MemoryOrder_Relaxed);
    }

    // 如果没有找到指定节点，直接返回
    if (current == NULL) return false;

    // 创建新节点
    XLockFreeListNode* newNode = NULL;
    if (dataCreatMethod)
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        dataCreatMethod(temp, pvData);
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
    }
    if (newNode == NULL)
        return NULL;

    // 如果要插入的位置是头节点
    if (prev == NULL) {
        do {
            current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newNode, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
    }
    else {
        // 插入到指定节点前
        do {
            current = (XLockFreeListNode*)XAtomic_load_ptr(&prev->next, XAtomic_MemoryOrder_Relaxed);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newNode, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

size_t VXList_insert_array(XLockFreeList* this_list, XLockFreeListNode* curNode, void* array, size_t count, XCDataCreatMethod dataCreatMethod)
{
    if (this_list == NULL || array == NULL || count == 0) return 0;

    XLockFreeListNode* prev = NULL;
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    // 查找指定节点的前一个节点
    if (curNode != NULL) {
        while (current != NULL && current != curNode) {
            prev = current;
            current = (XLockFreeListNode*)XAtomic_load_ptr(&current->next, XAtomic_MemoryOrder_Relaxed);
        }
        // 如果没有找到指定节点，直接返回
        if (current == NULL) return 0;
    }

    // 创建新链表
    XLockFreeListNode* newListHead = NULL;
    XLockFreeListNode* newListTail = NULL;
    for (size_t i = 0; i < count; i++) 
    {
        XLockFreeListNode* newNode = NULL;
        if (dataCreatMethod)
        {
            void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
            dataCreatMethod(temp, (void*)((char*)array + i * XContainerTypeSize(this_list)));
            newNode = createNode(this_list, temp);
            XMemory_free(temp);
        }
        else
        {
            newNode = createNode(this_list, (void*)((char*)array + i * XContainerTypeSize(this_list)));
        }
        if (newNode == NULL) 
        {
            // 如果创建节点失败，释放已创建的节点
            while (newListHead != NULL) 
            {
                XLockFreeListNode* temp = newListHead;
                newListHead = newListHead->next;
                if (XContainerDataDeinitMethod(this_list) != NULL)
                {
                    XContainerDataDeinitMethod(this_list)(&temp->data);
                }
                XMemory_free(temp);
            }
            return 0;
        }
        if (newListHead == NULL) {
            newListHead = newNode;
            newListTail = newNode;
        }
        else {
            newListTail->next = newNode;
            newListTail = newNode;
        }
    }
    newListTail->next = NULL;

    // 插入新链表
    if (prev == NULL) {
        // 插入到链表头部
        do {
            current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newListHead, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail, XAtomic_MemoryOrder_Relaxed);
        }
    }
    else {
        // 插入到指定节点前
        do {
            current = (XLockFreeListNode*)XAtomic_load_ptr(&prev->next, XAtomic_MemoryOrder_Relaxed);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newListHead, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail, XAtomic_MemoryOrder_Relaxed);
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), count, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), count, XAtomic_MemoryOrder_Relaxed);
    return count;
}
// 头部删除
bool VXListAtomic_pop_front(XLockFreeList* this_list) {
    if (XLockFreeList_isEmpty_base(this_list)) return false;

    XLockFreeListNode* oldHead;
    XLockFreeListNode* next;

    do {
        oldHead = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
        if (oldHead == NULL) return false;  // 链表可能已变空

        next = (XLockFreeListNode*)XAtomic_load_ptr(&oldHead->next, XAtomic_MemoryOrder_Relaxed);

        // 尝试更新头指针
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    // 如果删除后链表为空，更新尾指针
    if (next == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, NULL, XAtomic_MemoryOrder_Relaxed);
    }

    // 释放节点内存
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&oldHead->data);
    }
    XMemory_free(oldHead);

    // 更新记录数量
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

// 尾部删除
bool VXListAtomic_pop_back(XLockFreeList* this_list) 
{
    if (XLockFreeList_isEmpty_base(this_list))
        return false;

    XLockFreeListNode* tail;
    XLockFreeListNode* prev;

    while (true)
    {
        tail = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
        if (tail == NULL) return false;  // 链表可能已变空

        prev = NULL;
        XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

        // 查找尾节点的前一个节点
        while (current != NULL && current->next != tail)
        {
            current = current->next;
            prev = current;
        }

        // 如果尾节点已被其他线程修改
        if (tail != (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed))
        {
            continue;
        }

        // 如果尾节点是头节点，说明链表即将变空
        if (prev == NULL) 
        {
            if (XAtomic_compare_exchange_strong_ptr(&this_list->m_head, (void**)&tail, NULL, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed))
            {
                XAtomic_store_ptr(&this_list->m_tail, NULL, XAtomic_MemoryOrder_Relaxed);
                break;
            }
        }
        else 
        {
            // 尝试将前一个节点的next设为NULL
            if (XAtomic_compare_exchange_strong_ptr(&(prev->next), (void**)&tail, NULL, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed))
            {
                XAtomic_store_ptr(&this_list->m_tail, prev, XAtomic_MemoryOrder_Relaxed);
                break;
            }
        }
    }

    // 释放节点内存
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&tail->data);
    }
    XMemory_free(tail);

    // 更新记录数量
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
    return true;
}

// 删除指定节点
void VXListAtomic_erase(XLockFreeList* this_list, const XLockFreeList_iterator* it, XLockFreeList_iterator* nextIt) 
{
    if (XLockFreeList_isEmpty_base(this_list) || it == NULL) return;

    XLockFreeListNode* node = it->node;
    XLockFreeListNode* prev = NULL;
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    while (current != NULL)
    {
        if (current == node) 
        {
            // 如果要删除的是头节点
            if (prev == NULL) 
            {
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_head, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_ptr(&this_list->m_tail, next, XAtomic_MemoryOrder_Relaxed);
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
                if (XAtomic_compare_exchange_strong_ptr(
                    &prev->next, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed))
                {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_ptr(&this_list->m_tail, prev, XAtomic_MemoryOrder_Relaxed);
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

    XLockFreeListNode* prev = NULL;
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    while (current != NULL) {
        if (XContainerCompare(this_list)(&current->data, pvData)==XCompare_Equality)
        {
            // 如果要删除的是头节点
            if (prev == NULL) {
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_head, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_ptr(&this_list->m_tail, next, XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                }
            }
            else {
                // 不是头节点
                XLockFreeListNode* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &prev->next, (void**)&current, next, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed)) {
                        XAtomic_store_ptr(&this_list->m_tail, prev, XAtomic_MemoryOrder_Relaxed);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1, XAtomic_MemoryOrder_Relaxed);
                }
            }
            return true;
        }

        prev = current;
        current = current->next;
    }
    return false;
}

// 清空链表
void VXListAtomic_clear(XLockFreeList* this_list) {
    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    while (current != NULL) {
        XLockFreeListNode* next = current->next;

        if (XContainerDataDeinitMethod(this_list) != NULL) {
            XContainerDataDeinitMethod(this_list)(&current->data);
        }
        XMemory_free(current);

        current = next;
    }

    XAtomic_store_ptr(&this_list->m_head, NULL, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_ptr(&this_list->m_tail, NULL, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerSize(this_list), 0, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(this_list), 0, XAtomic_MemoryOrder_Relaxed);
}

// 获取链表头数据
void* VXListAtomic_front(XLockFreeList* this_list) {
    XLockFreeListNode* head = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
    if (head == NULL) return NULL;
    return &head->data;
}

// 获取链表尾数据
void* VXListAtomic_back(XLockFreeList* this_list) {
    XLockFreeListNode* tail = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_tail, XAtomic_MemoryOrder_Relaxed);
    if (tail == NULL) return NULL;
    return &tail->data;
}

// 查找数据
bool VXListAtomic_find(const XLockFreeList* this_list, void* pvData, XLockFreeList_iterator* it) 
{
    if (XLockFreeList_isEmpty_base(this_list))
    {
        if (it)
            *it = XLockFreeList_end(this_list);
        return false;
    }

    XLockFreeListNode* current = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);

    while (current != NULL) 
    {
        if (XContainerCompare(this_list))
        {
            if (XContainerCompare(this_list)(XLockFreeListNode_DataPtr(current), pvData)==XCompare_Equality)
            {
                if (it)
                    it->node = current;
                return true;
            }
        }
        else if (memcmp(XLockFreeListNode_DataPtr(current), pvData,XContainerTypeSize(this_list))==0)
        {
            if (it)
                it->node = current;
            return true;
        }
        current = current->next;
    }
    if (it)
        *it = XLockFreeList_end(this_list);
    return false;
}

// 找到链表尾部节点
static XLockFreeListNode* findTail(XLockFreeListNode* head) {
    if (head == NULL) return NULL;
    while (XAtomic_load_ptr(&head->next, XAtomic_MemoryOrder_Relaxed) != NULL)
        head = (XLockFreeListNode*)XAtomic_load_ptr(&head->next, XAtomic_MemoryOrder_Relaxed);
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
    XLockFreeListNode* j = (XLockFreeListNode*)XAtomic_load_ptr(&left->next, XAtomic_MemoryOrder_Relaxed);
    int32_t cmp;
    while (j != NULL) {
        //if (compare(&(j->data), pivot)) 
        cmp = compare(&(j->data), pivot);
        if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
        {
            i = (XLockFreeListNode*)XAtomic_load_ptr(&i->next, XAtomic_MemoryOrder_Relaxed);
            // 交换i和j的数据
            void* temp = XMemory_malloc(typeSize);
            memcpy(temp, &(i->data), typeSize);
            memcpy(&(i->data), &(j->data), typeSize);
            memcpy(&(j->data), temp, typeSize);
            XMemory_free(temp);
        }
        if (j == right) break;  // 到达右边界
        j = (XLockFreeListNode*)XAtomic_load_ptr(&j->next, XAtomic_MemoryOrder_Relaxed);
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
void VXListAtomic_sort(XLockFreeList* this_list, XSortOrder order) {
#if XStack_ON
    if (XLockFreeList_isEmpty_base(this_list) || XContainerCompare(this_list) == NULL)
        return;

    XLockFreeListNode* head = (XLockFreeListNode*)XAtomic_load_ptr(&this_list->m_head, XAtomic_MemoryOrder_Relaxed);
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
        if (pivot != NULL && XAtomic_load_ptr(&pivot->next, XAtomic_MemoryOrder_Relaxed) != NULL) {
            XLockFreeListNode* rightHead = (XLockFreeListNode*)XAtomic_load_ptr(&pivot->next, XAtomic_MemoryOrder_Relaxed);
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

    XAtomic_init((this_list->m_head), NULL);
    XAtomic_init(this_list->m_tail, NULL);
}

void VXLockFreeList_swap(XLockFreeList* list1, XLockFreeList* list2)
{
    // 交换链表实现
    XLockFreeListNode* tempHead = (XLockFreeListNode*)XAtomic_load_ptr(&list1->m_head, XAtomic_MemoryOrder_Relaxed);
    XLockFreeListNode* tempTail = (XLockFreeListNode*)XAtomic_load_ptr(&list1->m_tail, XAtomic_MemoryOrder_Relaxed);
    size_t tempSize = XAtomic_load_size_t(&XContainerSize(list1), XAtomic_MemoryOrder_Relaxed);
    size_t tempCapacity = XAtomic_load_size_t(&XContainerCapacity(list1), XAtomic_MemoryOrder_Relaxed);

    XAtomic_store_ptr(&list1->m_head, XAtomic_load_ptr(&list2->m_head, XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_ptr(&list1->m_tail, XAtomic_load_ptr(&list2->m_tail, XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerSize(list1), XAtomic_load_size_t(&XContainerSize(list2), XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list1), XAtomic_load_size_t(&XContainerCapacity(list2), XAtomic_MemoryOrder_Relaxed), XAtomic_MemoryOrder_Relaxed);

    XAtomic_store_ptr(&list2->m_head, tempHead, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_ptr(&list2->m_tail, tempTail, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerSize(list2), tempSize, XAtomic_MemoryOrder_Relaxed);
    XAtomic_store_size_t(&XContainerCapacity(list2), tempCapacity, XAtomic_MemoryOrder_Relaxed);
}

#endif // XLockFreeList_ON