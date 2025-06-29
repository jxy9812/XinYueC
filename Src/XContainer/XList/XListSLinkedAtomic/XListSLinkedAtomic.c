#include"XListSLinkedAtomic.h"
#if XListSLinkedAtomic_ON
#include <stdlib.h>
#include <string.h>

// 内部函数声明
static XListSNodeAtomic * VXListAtomic_push_front(XListSLinkedAtomic * this_list, void* pvData);
static XListSNodeAtomic* VXListAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData);
static void VXListAtomic_pop_front(XListSLinkedAtomic* this_list);
static void VXListAtomic_pop_back(XListSLinkedAtomic* this_list);
static void VXListAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node);
static void VXListAtomic_remove(XListSLinkedAtomic* this_list, void* pvData);
static void VXListAtomic_clear(XListSLinkedAtomic* this_list);
static void* VXListAtomic_front(XListSLinkedAtomic* this_list);
static void* VXListAtomic_back(XListSLinkedAtomic* this_list);
static XListSNodeAtomic* VXListAtomic_find(const XListSLinkedAtomic* this_list, void* pvData);
static void VXListAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare);
static void VXListAtomic_delete(XListSLinkedAtomic* this_list);

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
        VXListAtomic_push_front, VXListAtomic_push_back,
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
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXListAtomic_delete);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXListAtomic_clear);

#if SHOWCONTAINERSIZE
    printf("XListSLinkedAtomic size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// 头部插入（多生产者安全）
XListSNodeAtomic* VXListAtomic_push_front(XListSLinkedAtomic* this_list, void* pvData) {
    XListSNodeAtomic* newNode = createNode(this_list, pvData);
    if (newNode == NULL) return NULL;

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
XListSNodeAtomic* VXListAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData) {
    XListSNodeAtomic* newNode = createNode(this_list, pvData);
    if (newNode == NULL) return NULL;

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

// 头部删除
void VXListAtomic_pop_front(XListSLinkedAtomic* this_list) {
    if (XListSLinkedAtomic_isEmpty(this_list)) return;

    XListSNodeAtomic* oldHead;
    XListSNodeAtomic* next;

    do {
        oldHead = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);
        if (oldHead == NULL) return;  // 链表可能已变空

        next = (XListSNodeAtomic*)XAtomic_load_ptr(&oldHead->next);

        // 尝试更新头指针
    } while (!XAtomic_compare_exchange_strong_ptr(
        &this_list->m_head, (void**)&oldHead, next));

    // 如果删除后链表为空，更新尾指针
    if (next == NULL) {
        XAtomic_store_ptr(&this_list->m_tail, NULL);
    }

    // 释放节点内存
    if (XContainerDataDeleteMethod(this_list) != NULL) {
        XContainerDataDeleteMethod(this_list)(&oldHead->data);
    }
    XMemory_free(oldHead);

    // 更新记录数量
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
}

// 尾部删除
void VXListAtomic_pop_back(XListSLinkedAtomic* this_list) {
    if (XListSLinkedAtomic_isEmpty(this_list)) return;

    XListSNodeAtomic* tail;
    XListSNodeAtomic* prev;

    while (1) {
        tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);
        if (tail == NULL) return;  // 链表可能已变空

        prev = NULL;
        XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

        // 查找尾节点的前一个节点
        while (current != NULL && current->next != tail) {
            prev = current;
            current = current->next;
        }

        // 如果尾节点已被其他线程修改
        if (tail != (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail)) {
            continue;
        }

        // 如果尾节点是头节点，说明链表即将变空
        if (prev == NULL) {
            if (XAtomic_compare_exchange_strong_ptr(
                &this_list->m_head, (void**)&tail, NULL)) {
                XAtomic_store_ptr(&this_list->m_tail, NULL);
                break;
            }
        }
        else {
            // 尝试将前一个节点的next设为NULL
            if (XAtomic_compare_exchange_strong_ptr(
                &prev->next, (void**)&tail, NULL)) {
                XAtomic_store_ptr(&this_list->m_tail, prev);
                break;
            }
        }
    }

    // 释放节点内存
    if (XContainerDataDeleteMethod(this_list) != NULL) {
        XContainerDataDeleteMethod(this_list)(&tail->data);
    }
    XMemory_free(tail);

    // 更新记录数量
    XAtomic_fetch_sub_size_t(&XContainerSize(this_list), 1);
    XAtomic_fetch_sub_size_t(&XContainerCapacity(this_list), 1);
}

// 删除指定节点
void VXListAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node) {
    if (XListSLinkedAtomic_isEmpty(this_list) || node == NULL) return;

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

                    if (XContainerDataDeleteMethod(this_list) != NULL) {
                        XContainerDataDeleteMethod(this_list)(&current->data);
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

                    if (XContainerDataDeleteMethod(this_list) != NULL) {
                        XContainerDataDeleteMethod(this_list)(&current->data);
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
void VXListAtomic_remove(XListSLinkedAtomic* this_list, void* pvData) {
    if (XListSLinkedAtomic_isEmpty(this_list))
        return;
    if (((XListBase*)this_list)->m_equality == NULL)
        return;

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

                    if (XContainerDataDeleteMethod(this_list) != NULL) {
                        XContainerDataDeleteMethod(this_list)(&current->data);
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

                    if (XContainerDataDeleteMethod(this_list) != NULL) {
                        XContainerDataDeleteMethod(this_list)(&current->data);
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

// 清空链表
void VXListAtomic_clear(XListSLinkedAtomic* this_list) {
    XListSNodeAtomic* current = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_head);

    while (current != NULL) {
        XListSNodeAtomic* next = current->next;

        if (XContainerDataDeleteMethod(this_list) != NULL) {
            XContainerDataDeleteMethod(this_list)(&current->data);
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
    if (head == NULL || head->next == NULL) return NULL;
    return &head->next->data;
}

// 获取链表尾数据
void* VXListAtomic_back(XListSLinkedAtomic* this_list) {
    XListSNodeAtomic* tail = (XListSNodeAtomic*)XAtomic_load_ptr(&this_list->m_tail);
    if (tail == NULL) return NULL;
    return &tail->data;
}

// 查找数据
XListSNodeAtomic* VXListAtomic_find(const XListSLinkedAtomic* this_list, void* pvData) {
    if (XListSLinkedAtomic_isEmpty(this_list)) return NULL;
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

// 链表排序
void VXListAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare) {
    // 链表排序实现（可以使用归并排序等算法）
    // 此处省略具体实现，需要根据需求完成
}

// 释放链表
void VXListAtomic_delete(XListSLinkedAtomic* this_list) {
    VXListAtomic_clear(this_list);
    XMemory_free(this_list);
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

// 其他对外接口实现（直接调用内部函数）
XListSNodeAtomic* XListSLinkedAtomic_push_front(XListSLinkedAtomic* this_list, void* pvData) {
    return VXListAtomic_push_front(this_list, pvData);
}

XListSNodeAtomic* XListSLinkedAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData) {
    return VXListAtomic_push_back(this_list, pvData);
}

void XListSLinkedAtomic_pop_front(XListSLinkedAtomic* this_list) {
    VXListAtomic_pop_front(this_list);
}

void XListSLinkedAtomic_pop_back(XListSLinkedAtomic* this_list) {
    VXListAtomic_pop_back(this_list);
}

void XListSLinkedAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node) {
    VXListAtomic_erase(this_list, node);
}

void XListSLinkedAtomic_remove(XListSLinkedAtomic* this_list, void* pvData) {
    VXListAtomic_remove(this_list, pvData);
}

void* XListSLinkedAtomic_front(XListSLinkedAtomic* this_list) {
    return VXListAtomic_front(this_list);
}

void* XListSLinkedAtomic_back(XListSLinkedAtomic* this_list) {
    return VXListAtomic_back(this_list);
}

XListSNodeAtomic* XListSLinkedAtomic_find(const XListSLinkedAtomic* this_list, const void* findVal) {
    return VXListAtomic_find(this_list, (void*)findVal);
}

void XListSLinkedAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare) {
    VXListAtomic_sort(this_list, compare);
}

void XListSLinkedAtomic_delete(XListSLinkedAtomic* this_list) {
    VXListAtomic_delete(this_list);
}

void XListSLinkedAtomic_clear(XListSLinkedAtomic* this_list) {
    VXListAtomic_clear(this_list);
}

bool XListSLinkedAtomic_isEmpty(XListSLinkedAtomic* this_list) {
    return XAtomic_load_ptr(&this_list->m_head) == NULL;
}

size_t XListSLinkedAtomic_getSize(XListSLinkedAtomic* this_list) {
    return XAtomic_load_size_t(&XContainerSize(this_list));
}

size_t XListSLinkedAtomic_getCapacity(XListSLinkedAtomic* this_list) {
    return XAtomic_load_size_t(&XContainerCapacity(this_list));
}

void XListSLinkedAtomic_swap(XListSLinkedAtomic* list1, XListSLinkedAtomic* list2) {
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

size_t XListSLinkedAtomic_getTypeSize(XListSLinkedAtomic* this_list) {
    return XContainerTypeSize(this_list);
}

#endif // XListSLinkedAtomic_ON