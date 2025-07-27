#include"XListSLinkedAtomic.h"
#if XListSLinkedAtomic_ON
#include <stdlib.h>
#include <string.h>
#include "XStack.h"
#include "XAlgorithm.h"
// 内部函数声明
static XListSNodeAtomic * VXListAtomic_push_front(XListSLinkedAtomic * this_list, void* pvData);
static XListSNodeAtomic* VXListAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData);
static bool VXList_insert(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, void* pvData);
static size_t VXList_insert_array(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, const void* array, size_t count);

static XListSNodeAtomic* VXListAtomic_push_front_move(XListSLinkedAtomic* this_list, void* pvData);
static XListSNodeAtomic* VXListAtomic_push_back_move(XListSLinkedAtomic* this_list, void* pvData);
static bool VXList_insert_move(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, void* pvData);
static size_t VXList_insert_array_move(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, const void* array, size_t count);

static bool VXListAtomic_pop_front(XListSLinkedAtomic* this_list);
static bool VXListAtomic_pop_back(XListSLinkedAtomic* this_list);
static void VXListAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node);
static bool VXListAtomic_remove(XListSLinkedAtomic* this_list, void* pvData);
static void VXListAtomic_clear(XListSLinkedAtomic* this_list);
static void* VXListAtomic_front(XListSLinkedAtomic* this_list);
static void* VXListAtomic_back(XListSLinkedAtomic* this_list);
static XListSNodeAtomic* VXListAtomic_find(const XListSLinkedAtomic* this_list, void* pvData);
static void VXListAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare);
static void VXClass_copy(XListSLinkedAtomic* object, const XListSLinkedAtomic* src);
static void VXClass_move(XListSLinkedAtomic* object, XListSLinkedAtomic* src);
static void VXListAtomic_deinit(XListSLinkedAtomic* this_list);
static void VXListSLinkedAtomic_swap(XListSLinkedAtomic* list1, XListSLinkedAtomic* list2);
// 创建新节点
static XListSNodeAtomic* createNode(XListSLinkedAtomic* this_list, void* pvData) {
    XListSNodeAtomic* newNode = (XListSNodeAtomic*)XMemory_malloc(
        sizeof(XListSNodeAtomic) + XContainerTypeSize(this_list));
    if (newNode == NULL) {
        perror("创建节点失败");
        return NULL;
    }
    memcpy(&newNode->data, pvData, XContainerTypeSize(this_list));
    newNode->next = NULL;
    return newNode;
}

// 类初始化
XVtable* XListSLinkedAtomic_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XLISTSLINKED_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

    void* table[] = {
        // 插入操作
        VXListAtomic_push_front,VXListAtomic_push_front_move,
        VXListAtomic_push_back,VXListAtomic_push_back_move,
        VXList_insert,VXList_insert_move,
        VXList_insert_array, VXList_insert_array_move,
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
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXListAtomic_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXListSLinkedAtomic_swap);
#if SHOWCONTAINERSIZE
    printf("XListSLinkedAtomic size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 头部插入（多生产者安全）
XListSNodeAtomic* VXListAtomic_push_front(XListSLinkedAtomic* this_list, void* pvData) 
{
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataCopyMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataCopyMethod(this_list)(temp, pvData);
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
    }
    if (newNode == NULL) 
        return NULL;

    XListSNodeAtomic* oldHead;
    do {
        oldHead = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
        newNode->next = oldHead;
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, newNode));

    // 如果链表原来是空的，更新尾指针
    if (oldHead == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, newNode);
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return newNode;
}

// 尾部插入（多生产者安全）
XListSNodeAtomic* VXListAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData)
{
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataCopyMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataCopyMethod(this_list)(temp, pvData);
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
    }
    if (newNode == NULL)
        return NULL;

    XListSNodeAtomic* tail;
    while (1) {
        tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);

        // 如果链表为空，尝试更新头指针
        if (tail == NULL) {
            if (XAtomic_compare_exchange_strong_ptr(
                &this_list->m_head, (void**)&tail, newNode)) {
                XAtomic_store_ptr(&this_list->m_tail, newNode);
                break;
            }
        }
        else {
            // 尝试将新节点链接到尾部
            XListSNodeAtomic* next = (XListSNodeAtomic*)XAtomic_load_ptr(&tail->next);
            if (next == NULL) {
                if (XAtomic_compare_exchange_strong_ptr(
                    &tail->next, (void**)&next, newNode)) {
                    // 链接成功后，尝试更新尾指针
                    XAtomic_compare_exchange_strong_ptr(
                        &this_list->m_tail, (void**)&tail, newNode);
                    break;
                }
            }
            else {
                // 尾指针已过时，帮助推进
                XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_tail, (void**)&tail, next);
            }
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return newNode;
}

bool VXList_insert(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, void* pvData)
{
    if (this_list == NULL || curNode == NULL || pvData == NULL) return false;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    // 查找指定节点的前一个节点
    while (current != NULL && current != curNode) {
        prev = current;
        current = (XListSNodeAtomic*)XAtomic_load_ptr(&current->next);
    }

    // 如果没有找到指定节点，直接返回
    if (current == NULL) return false;

    // 创建新节点
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataCopyMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataCopyMethod(this_list)(temp, pvData);
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
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newNode));
    }
    else {
        // 插入到指定节点前
        do {
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&prev->next);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newNode));
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return true;
}

size_t VXList_insert_array(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, const void* array, size_t count)
{
    if (this_list == NULL || array == NULL || count == 0) return 0;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    // 查找指定节点的前一个节点
    if (curNode != NULL) {
        while (current != NULL && current != curNode) {
            prev = current;
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&current->next);
        }
        // 如果没有找到指定节点，直接返回
        if (current == NULL) return 0;
    }

    // 创建新链表
    XListSNodeAtomic* newListHead = NULL;
    XListSNodeAtomic* newListTail = NULL;
    for (size_t i = 0; i < count; i++) 
    {
        XListSNodeAtomic* newNode = NULL;
        if (XContainerDataCopyMethod(this_list))
        {
            void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
            XContainerDataCopyMethod(this_list)(temp, (void*)((char*)array + i * XContainerTypeSize(this_list)));
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
                XListSNodeAtomic* temp = newListHead;
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
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newListHead));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail);
        }
    }
    else {
        // 插入到指定节点前
        do {
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&prev->next);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newListHead));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail);
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), count);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), count);
    return count;
}

XListSNodeAtomic* VXListAtomic_push_front_move(XListSLinkedAtomic* this_list, void* pvData)
{
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataMoveMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataMoveMethod(this_list)(temp, pvData);
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
    }
    if (newNode == NULL)
        return NULL;

    XListSNodeAtomic* oldHead;
    do {
        oldHead = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
        newNode->next = oldHead;
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, newNode));

    // 如果链表原来是空的，更新尾指针
    if (oldHead == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, newNode);
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return newNode;
}

XListSNodeAtomic* VXListAtomic_push_back_move(XListSLinkedAtomic* this_list, void* pvData)
{
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataMoveMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataMoveMethod(this_list)(temp, pvData);
        newNode = createNode(this_list, temp);
        XMemory_free(temp);
    }
    else
    {
        newNode = createNode(this_list, pvData);
    }
    if (newNode == NULL)
        return NULL;

    XListSNodeAtomic* tail;
    while (1) {
        tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);

        // 如果链表为空，尝试更新头指针
        if (tail == NULL) {
            if (XAtomic_compare_exchange_strong_ptr(
                &this_list->m_head, (void**)&tail, newNode)) {
                XAtomic_store_ptr(&this_list->m_tail, newNode);
                break;
            }
        }
        else {
            // 尝试将新节点链接到尾部
            XListSNodeAtomic* next = (XListSNodeAtomic*)XAtomic_load_ptr(&tail->next);
            if (next == NULL) {
                if (XAtomic_compare_exchange_strong_ptr(
                    &tail->next, (void**)&next, newNode)) {
                    // 链接成功后，尝试更新尾指针
                    XAtomic_compare_exchange_strong_ptr(
                        &this_list->m_tail, (void**)&tail, newNode);
                    break;
                }
            }
            else {
                // 尾指针已过时，帮助推进
                XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_tail, (void**)&tail, next);
            }
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return newNode;
}

bool VXList_insert_move(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, void* pvData)
{
    if (this_list == NULL || curNode == NULL || pvData == NULL) return false;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    // 查找指定节点的前一个节点
    while (current != NULL && current != curNode) {
        prev = current;
        current = (XListSNodeAtomic*)XAtomic_load_ptr(&current->next);
    }

    // 如果没有找到指定节点，直接返回
    if (current == NULL) return false;

    // 创建新节点
    XListSNodeAtomic* newNode = NULL;
    if (XContainerDataMoveMethod(this_list))
    {
        void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
        XContainerDataMoveMethod(this_list)(temp, pvData);
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
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newNode));
    }
    else {
        // 插入到指定节点前
        do {
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&prev->next);
            newNode->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newNode));
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), 1);
    return true;
}

size_t VXList_insert_array_move(XListSLinkedAtomic* this_list, XListSNodeAtomic* curNode, const void* array, size_t count)
{
    if (this_list == NULL || array == NULL || count == 0) return 0;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    // 查找指定节点的前一个节点
    if (curNode != NULL) {
        while (current != NULL && current != curNode) {
            prev = current;
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&current->next);
        }
        // 如果没有找到指定节点，直接返回
        if (current == NULL) return 0;
    }

    // 创建新链表
    XListSNodeAtomic* newListHead = NULL;
    XListSNodeAtomic* newListTail = NULL;
    for (size_t i = 0; i < count; i++)
    {
        XListSNodeAtomic* newNode = NULL;
        if (XContainerDataMoveMethod(this_list))
        {
            void* temp = XMemory_calloc(1, XContainerTypeSize(this_list));
            XContainerDataMoveMethod(this_list)(temp, (void*)((char*)array + i * XContainerTypeSize(this_list)));
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
                XListSNodeAtomic* temp = newListHead;
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
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &this_list->m_head, (void**)&current, newListHead));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail);
        }
    }
    else {
        // 插入到指定节点前
        do {
            current = (XListSNodeAtomic*)XAtomic_load_ptr(&prev->next);
            newListTail->next = current;
        } while (!XAtomic_compare_exchange_strong_ptr(
            &prev->next, (void**)&current, newListHead));
        if (current == NULL) {
            XAtomic_store_ptr(&this_list->m_tail, newListTail);
        }
    }

    // 更新记录数量
    XAtomic_fetch_add_size_t(&XContainerSize(this_list), count);
    XAtomic_fetch_add_size_t(&XContainerCapacity(this_list), count);
    return count;
}

// 头部删除
bool VXListAtomic_pop_front(XListSLinkedAtomic* this_list) {
    if (XListSLinkedAtomic_isEmpty_base(this_list)) return false;

    XListSNodeAtomic* oldHead;
    XListSNodeAtomic* next;

    do {
        oldHead = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
        if (oldHead == NULL) return false;  // 链表可能已变空

        next = (XListSNodeAtomic*)XAtomic_load_ptr(&oldHead->next);

        // 尝试更新头指针
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, next));

    // 如果删除后链表为空，更新尾指针
    if (next == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, NULL);
    }

    // 释放节点内存
    if (XContainerDataDeinitMethod(this_list) != NULL) {
        XContainerDataDeinitMethod(this_list)(&oldHead->data);
    }
    XMemory_free(oldHead);

    // 更新记录数量
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
    return true;
}

// 尾部删除
bool VXListAtomic_pop_back(XListSLinkedAtomic* this_list) 
{
    if (XListSLinkedAtomic_isEmpty_base(this_list))
        return false;

    XListSNodeAtomic* tail;
    XListSNodeAtomic* prev;

    while (true)
    {
        tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);
        if (tail == NULL) return false;  // 链表可能已变空

        prev = NULL;
        XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

        // 查找尾节点的前一个节点
        while (current != NULL && current->next != tail)
        {
            current = current->next;
            prev = current;
        }

        // 如果尾节点已被其他线程修改
        if (tail != (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) 
        {
            continue;
        }

        // 如果尾节点是头节点，说明链表即将变空
        if (prev == NULL) 
        {
            if (XAtomic_compare_exchange_strong_ptr(&this_list->m_head, (void**)&tail, NULL)) 
            {
                XAtomic_store_ptr(&this_list->m_tail, NULL);
                break;
            }
        }
        else 
        {
            // 尝试将前一个节点的next设为NULL
            if (XAtomic_compare_exchange_strong_ptr(&(prev->next), (void**)&tail, NULL))
            {
                XAtomic_store_ptr(&this_list->m_tail, prev);
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
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
    return true;
}

// 删除指定节点
void VXListAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node) {
    if (XListSLinkedAtomic_isEmpty_base(this_list) || node == NULL) return;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    while (current != NULL) {
        if (current == node) {
            // 如果要删除的是头节点
            if (prev == NULL) {
                XListSNodeAtomic* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_head, (void**)&current, next)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) {
                        XAtomic_store_ptr(&this_list->m_tail, next);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
                }
            }
            else {
                // 不是头节点
                XListSNodeAtomic* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &prev->next, (void**)&current, next)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) {
                        XAtomic_store_ptr(&this_list->m_tail, prev);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
                }
            }
            return;
        }

        prev = current;
        current = current->next;
    }
}

// 删除指定数据的节点
bool VXListAtomic_remove(XListSLinkedAtomic* this_list, void* pvData) {
    if (XListSLinkedAtomic_isEmpty_base(this_list))
        return false;
    if (((XListBase*)this_list)->m_equality == NULL)
        return false;

    XListSNodeAtomic* prev = NULL;
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    while (current != NULL) {
        if (((XListBase*)this_list)->m_equality(&current->data, pvData)) {
            // 如果要删除的是头节点
            if (prev == NULL) {
                XListSNodeAtomic* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &this_list->m_head, (void**)&current, next)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) {
                        XAtomic_store_ptr(&this_list->m_tail, next);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
                }
            }
            else {
                // 不是头节点
                XListSNodeAtomic* next = current->next;
                if (XAtomic_compare_exchange_strong_ptr(
                    &prev->next, (void**)&current, next)) {
                    // 如果删除的是尾节点，更新尾指针
                    if (current == (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) {
                        XAtomic_store_ptr(&this_list->m_tail, prev);
                    }

                    if (XContainerDataDeinitMethod(this_list) != NULL) {
                        XContainerDataDeinitMethod(this_list)(&current->data);
                    }
                    XMemory_free(current);
                    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
                    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
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
void VXListAtomic_clear(XListSLinkedAtomic* this_list) {
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    while (current != NULL) {
        XListSNodeAtomic* next = current->next;

        if (XContainerDataDeinitMethod(this_list) != NULL) {
            XContainerDataDeinitMethod(this_list)(&current->data);
        }
        XMemory_free(current);

        current = next;
    }

    XAtomic_store_ptr(&this_list->m_head, NULL);
    XAtomic_store_ptr(&this_list->m_tail, NULL);
    XAtomic_store_size_t(&XContainerSize(this_list), 0);
    XAtomic_store_size_t(&XContainerCapacity(this_list), 0);
}

// 获取链表头数据
void* VXListAtomic_front(XListSLinkedAtomic* this_list) {
    XListSNodeAtomic* head = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
    if (head == NULL) return NULL;
    return &head->data;
}

// 获取链表尾数据
void* VXListAtomic_back(XListSLinkedAtomic* this_list) {
    XListSNodeAtomic* tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);
    if (tail == NULL) return NULL;
    return &tail->data;
}

// 查找数据
XListSNodeAtomic* VXListAtomic_find(const XListSLinkedAtomic* this_list, void* pvData) {
    if (XListSLinkedAtomic_isEmpty_base(this_list)) return NULL;
    if (((XListBase*)this_list)->m_equality == NULL) return NULL;

    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    while (current != NULL) {
        if (((XListBase*)this_list)->m_equality(&current->data, pvData)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// 找到链表尾部节点
static XListSNodeAtomic* findTail(XListSNodeAtomic* head) {
    if (head == NULL) return NULL;
    while (XAtomic_load_ptr(&head->next) != NULL)
        head = (XListSNodeAtomic*)XAtomic_load_ptr(&head->next);
    return head;
}

// 单向链表的一次快排（分区函数）
static XListSNodeAtomic* List_OneSort(XListSNodeAtomic* left, XListSNodeAtomic* right, size_t typeSize, XCompare compare) {
    if (left == NULL || right == NULL || left == right)
        return left;

    void* pivot = XMemory_malloc(typeSize);
    if (pivot == NULL) return NULL;
    memcpy(pivot, &(left->data), typeSize);

    XListSNodeAtomic* i = left;    // 分区点
    XListSNodeAtomic* j = (XListSNodeAtomic*)XAtomic_load_ptr(&left->next);

    while (j != NULL) {
        if (compare(&(j->data), pivot)) {
            i = (XListSNodeAtomic*)XAtomic_load_ptr(&i->next);
            // 交换i和j的数据
            void* temp = XMemory_malloc(typeSize);
            memcpy(temp, &(i->data), typeSize);
            memcpy(&(i->data), &(j->data), typeSize);
            memcpy(&(j->data), temp, typeSize);
            XMemory_free(temp);
        }
        if (j == right) break;  // 到达右边界
        j = (XListSNodeAtomic*)XAtomic_load_ptr(&j->next);
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
void VXListAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare) {
#if XStack_ON
    if (XListSLinkedAtomic_isEmpty_base(this_list))
        return;

    XListSNodeAtomic* head = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
    XListSNodeAtomic* tail = findTail(head);

    // 使用现有的XStack
    XStack* stack = XStack_Create(XListSNodeAtomic*);
    if (stack == NULL)
        return;

    // 初始化栈
    if (head != NULL) {
        XStack_push_base(stack, &tail);
        XStack_push_base(stack, &head);
    }

    while (!XStack_isEmpty_base(stack)) {
        // 弹出区间
        XListSNodeAtomic* h = *((XListSNodeAtomic**)XStack_top_base(stack));
        XStack_pop_base(stack);
        XListSNodeAtomic* t = *((XListSNodeAtomic**)XStack_top_base(stack));
        XStack_pop_base(stack);

        if (h == NULL || t == NULL || h == t)
            continue;

        // 执行一次快排
        XListSNodeAtomic* pivot = List_OneSort(h, t, XContainerTypeSize(this_list), compare);

        // 处理左子区间
        if (h != pivot) {
            XListSNodeAtomic* leftTail = findTail(h);
            if (leftTail != NULL && h != leftTail) {
                XStack_push_base(stack, &leftTail);
                XStack_push_base(stack, &h);
            }
        }

        // 处理右子区间
        if (pivot != NULL && XAtomic_load_ptr(&pivot->next) != NULL) {
            XListSNodeAtomic* rightHead = (XListSNodeAtomic*)XAtomic_load_ptr(&pivot->next);
            XListSNodeAtomic* rightTail = findTail(rightHead);
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

void VXClass_copy(XListSLinkedAtomic* object, const XListSLinkedAtomic* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListSLinkedAtomic_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    for_each_iterator(src, XListSLinkedAtomic, it)
    {
        XListBase_push_back_base(object, XListSLinkedAtomic_iterator_data(&it));
    }
}

void VXClass_move(XListSLinkedAtomic* object, XListSLinkedAtomic* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListSLinkedAtomic_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    XSwap(object, src, sizeof(XListSLinkedAtomic));
}

// 释放链表
void VXListAtomic_deinit(XListSLinkedAtomic* this_list) {
    VXListAtomic_clear(this_list);
   //XMemory_free(this_list);
}

// 对外接口实现

XListSLinkedAtomic* XListSLinkedAtomic_create(size_t typeSize) {
    if (typeSize == 0) return NULL;
    XListSLinkedAtomic* this_list = (XListSLinkedAtomic*)XMemory_malloc(sizeof(XListSLinkedAtomic));
    if (this_list == NULL) return NULL;
    XListSLinkedAtomic_init(this_list, typeSize);
    return this_list;
}

void XListSLinkedAtomic_init(XListSLinkedAtomic* this_list, size_t typeSize) {
    if (this_list == NULL || typeSize == 0) return;

    XListBase_init(this_list, typeSize);
    XClassGetVtable(this_list) = XListSLinkedAtomic_class_init();

    XAtomic_init((this_list->m_head), NULL);
    XAtomic_init(this_list->m_tail, NULL);
}

void VXListSLinkedAtomic_swap(XListSLinkedAtomic* list1, XListSLinkedAtomic* list2)
{
    // 交换链表实现
    XListSNodeAtomic* tempHead = (XListSNodeAtomic*)XAtomic_load_ptr(&list1->m_head);
    XListSNodeAtomic* tempTail = (XListSNodeAtomic*)XAtomic_load_ptr(&list1->m_tail);
    size_t tempSize = XAtomic_load_size_t(&XContainerSize(list1));
    size_t tempCapacity = XAtomic_load_size_t(&XContainerCapacity(list1));

    XAtomic_store_ptr(&list1->m_head, XAtomic_load_ptr(&list2->m_head));
    XAtomic_store_ptr(&list1->m_tail, XAtomic_load_ptr(&list2->m_tail));
    XAtomic_store_size_t(&XContainerSize(list1), XAtomic_load_size_t(&XContainerSize(list2)));
    XAtomic_store_size_t(&XContainerCapacity(list1), XAtomic_load_size_t(&XContainerCapacity(list2)));

    XAtomic_store_ptr(&list2->m_head, tempHead);
    XAtomic_store_ptr(&list2->m_tail, tempTail);
    XAtomic_store_size_t(&XContainerSize(list2), tempSize);
    XAtomic_store_size_t(&XContainerCapacity(list2), tempCapacity);
}

#endif // XListSLinkedAtomic_ON