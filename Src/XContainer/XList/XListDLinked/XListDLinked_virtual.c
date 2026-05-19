#include"XListDLinked.h"
#if XListDLinked_ON
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>

// 获取头节点指针的地址（统一 COW/非 COW 模式）
static inline XListDNode** XListDLinked_head_ptr(XListDLinked* list) {
    if (XContainerIsCow(list)) {
        return (XListDNode**)XContainerSharedDataPtr(list);
    }
    else {
        return (XListDNode**)&XContainerDataPtr(list);
    }
}

static bool VXListBase_push_front_node(XListDLinked* this_list, XListDNode* node);
static bool VXListBase_push_back_node(XListDLinked* this_list, XListDNode* node);
//插入
static XListDNode * VXList_push_front(XListDLinked * this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static XListDNode* VXList_push_back(XListDLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static void VXList_inserts(XListDLinked* this_list, XListDNode* curNode, void* pvData, size_t n, XCDataCreatMethod dataCreatMethod);
static bool VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod);
static size_t VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod);

//删除
static bool VXList_pop_front(XListDLinked* this_list);
static bool VXList_pop_back(XListDLinked* this_list);
static void VXList_erase(XListDLinked* this_list, const XListDLinked_iterator* it, XListDLinked_iterator* next);
static bool VXList_remove(XListDLinked* this_list, void* pvData);
static void VXList_clear(XListDLinked* this_list);
//遍历
static void* VXList_front(XListDLinked* this_list);
static void* VXList_back(XListDLinked* this_list);
static bool VXList_find(const XListDLinked* this_list, void* pvData, XListDLinked_iterator* it);
//COW分离与数据删除
static bool VXListDLinkedDetachIfNeeded(XListDLinked* this_list);
static void VXListDLinkedDataDelete(void* data, XListDLinked* this_list);
// 确保 XSharedData 存在（首次写入时延迟创建）
static bool ensureSharedData(XListDLinked* this_list);

//其他
static void VXList_sort(XListDLinked* this_list, XSortOrder order);
static void VXClass_copy(XListDLinked* object, const XListDLinked* src);
static void VXClass_move(XListDLinked* object, XListDLinked* src);
static void VXList_deinit(XListDLinked* this_list);

XVtable* XListDLinked_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XLISTDLINKED_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_XCLASS(XContainer);

    void* table[] = {
        //插入
        VXList_push_front,VXListBase_push_front_node,
        VXList_push_back,VXListBase_push_back_node,
        VXList_insert,
        VXList_insert_array,
        //删除
        VXList_pop_front,VXList_pop_back,VXList_erase,VXList_remove,
        //遍历
        VXList_front,VXList_back,VXList_find,
        //排序
        VXList_sort
    };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXList_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXList_clear);

#if SHOWCONTAINERSIZE
    printf("XListDLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}
#define CreatNode(this_list)    XMalloc_System(ALIGN_UP(sizeof(XListDNode)+XContainerTypeSize(this_list),sizeof(void*)))
//#define CreatNode(this_list)   (XMalloc_System(sizeof(XListDNode)+XContainerTypeSize(this_list)))

// 确保 XSharedData 存在（延迟创建）
static bool ensureSharedData(XListDLinked* this_list)
{
    if (XContainerIsCow(this_list)) {
        if (!XContainerSharedData(this_list)) {
            XSharedData* sd = XSharedData_create(NULL, sizeof(XListDNode*));
            if (!sd) return false;
            XContainerSharedData(this_list) = sd;
            *(XListDNode**)sd->data = NULL;
        }
    }
    else {
        // 非 COW 模式：无需分配共享块，只需确保头指针为 NULL
        if (!XContainerDataPtr(this_list)) {
            XContainerDataPtr(this_list) = NULL;
        }
    }
    return true;
}

// COW分离：如果数据被共享，创建独立副本（深拷贝双向链表节点）
static bool VXListDLinkedDetachIfNeeded(XListDLinked* this_list)
{
    if (!XContainerIsCow(this_list)) return true;  // 非 COW 不需要分离

    XSharedData* sd = XContainerSharedData(this_list);
    if (!sd || !XSharedData_isShared(sd)) return true;

    size_t typeSize = XContainerTypeSize(this_list);
    XListDNode* oldHead = *XListDLinked_head_ptr(this_list);

    XSharedData* newShared = XSharedData_create(NULL, sizeof(XListDNode*));
    if (!newShared) return false;

    XListDNode* newHead = NULL;
    XListDNode* newTail = NULL;

    if (oldHead) {
        XListDNode* oldNode = oldHead;
        do {
            XListDNode* newNode = XMalloc_System(ALIGN_UP(sizeof(XListDNode) + typeSize, sizeof(void*)));
            if (!newNode) {
                // 释放已创建的节点
                XListDNode* tmp = newHead;
                if (tmp) {
                    do {
                        XListDNode* next = tmp->next;
                        XFree_System(tmp);
                        tmp = next;
                    } while (tmp != newHead);
                }
                XSharedData_release(newShared);
                return false;
            }
            if (XContainerDataCopyMethod(this_list))
                XContainerDataCopyMethod(this_list)(XListDNode_DataPtr(newNode), XListDNode_DataPtr(oldNode));
            else
                memcpy(XListDNode_DataPtr(newNode), XListDNode_DataPtr(oldNode), typeSize);

            if (!newHead) {
                newHead = newNode;
                newTail = newNode;
                newNode->next = newNode;
                newNode->prev = newNode;
            }
            else {
                newNode->prev = newTail;
                newNode->next = newHead;
                newTail->next = newNode;
                newHead->prev = newNode;
                newTail = newNode;
            }
            oldNode = oldNode->next;
        } while (oldNode != oldHead);
    }

    *(XListDNode**)newShared->data = newHead;

    XSharedData_release(sd);
    XContainerSharedData(this_list) = newShared;
    return true;
}

// 删除双向链表数据（XSharedData释放回调）
static void VXListDLinkedDataDelete(void* data, XListDLinked* this_list)
{
    if (this_list == NULL) return;
    XListDNode* head = data ? *(XListDNode**)data : NULL;
    if (head)
    {
        XListDNode* node = head;
        do
        {
            XListDNode* next = node->next;
            if (XContainerDataDeinitMethod(this_list))
                XContainerDataDeinitMethod(this_list)(&(node->data));
            XFree_System(node);
            node = next;
        } while (node != head);
    }
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
    XContainerSharedData(this_list) = NULL;
}

void VXClass_copy(XListDLinked* object, const XListDLinked* src)
{
    // 如果目标未初始化，先初始化（模式与源相同）
    if (((XClass*)object)->m_vtable == NULL) {
        bool useCow = XContainerIsCow(src);
        XListDLinked_init(object, XContainerTypeSize(src), useCow);
    }
    else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXListDLinkedDataDelete, object);
        }
        else {
            XListDNode* head = (XListDNode*)XContainerDataPtr(object);
            if (head) {
                XListDNode* node = head;
                do {
                    XListDNode* next = node->next;
                    if (XContainerDataDeinitMethod(object))
                        XContainerDataDeinitMethod(object)(XListDNode_DataPtr(node));
                    XFree_System(node);
                    node = next;
                } while (node != head);
            }
            XContainerDataPtr(object) = NULL;
        }
    }

    // 复制所有成员（包括 m_tail 等，但双向链表没有 m_tail 成员，只有基类）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XListDLinked) - sizeof(XClass));

    // 根据源模式处理数据
    if (XContainerIsCow(src)) {
        // COW 模式：共享 XSharedData
        if (XContainerSharedData(object))
            XSharedData_addRef(XContainerSharedData(object));
    }
    else {
        // 非 COW 模式：深拷贝链表
        XListDNode* srcHead = (XListDNode*)XContainerDataPtr(src);
        if (srcHead) {
            XListDNode* newHead = NULL;
            XListDNode* newTail = NULL;
            XListDNode* srcNode = srcHead;
            size_t typeSize = XContainerTypeSize(src);
            do {
                XListDNode* newNode = XMalloc_System(ALIGN_UP(sizeof(XListDNode) + typeSize, sizeof(void*)));
                if (!newNode) {
                    // 回滚已创建的节点
                    if (newHead) {
                        XListDNode* node = newHead;
                        do {
                            XListDNode* next = node->next;
                            XFree_System(node);
                            node = next;
                        } while (node != newHead);
                    }
                    return;
                }
                if (XContainerDataCopyMethod(object))
                    XContainerDataCopyMethod(object)(XListDNode_DataPtr(newNode), XListDNode_DataPtr(srcNode));
                else
                    memcpy(XListDNode_DataPtr(newNode), XListDNode_DataPtr(srcNode), typeSize);

                if (!newHead) {
                    newHead = newNode;
                    newTail = newNode;
                    newNode->next = newNode;
                    newNode->prev = newNode;
                }
                else {
                    newNode->prev = newTail;
                    newNode->next = newHead;
                    newTail->next = newNode;
                    newHead->prev = newNode;
                    newTail = newNode;
                }
                srcNode = srcNode->next;
            } while (srcNode != srcHead);
            XContainerDataPtr(object) = newHead;
        }
        else {
            XContainerDataPtr(object) = NULL;
        }
        XContainerSize(object) = XContainerSize(src);
        XContainerCapacity(object) = XContainerCapacity(src);
    }
}

void VXClass_move(XListDLinked* object, XListDLinked* src)
{
    // 如果目标未初始化，先初始化（模式与源相同）
    if (((XClass*)object)->m_vtable == NULL) {
        bool useCow = XContainerIsCow(src);
        XListDLinked_init(object, XContainerTypeSize(src), useCow);
    }
    else {
        // 释放目标原有资源
        if (XContainerIsCow(object)) {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXListDLinkedDataDelete, object);
        }
        else {
            XListDNode* head = (XListDNode*)XContainerDataPtr(object);
            if (head) {
                XListDNode* node = head;
                do {
                    XListDNode* next = node->next;
                    if (XContainerDataDeinitMethod(object))
                        XContainerDataDeinitMethod(object)(XListDNode_DataPtr(node));
                    XFree_System(node);
                    node = next;
                } while (node != head);
            }
            XContainerDataPtr(object) = NULL;
        }
    }

    // 转移所有权
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XListDLinked) - sizeof(XClass));

    // 清空源对象
    if (XContainerIsCow(src)) {
        XContainerSharedData(src) = NULL;
    }
    else {
        XContainerDataPtr(src) = NULL;
    }
    XContainerSize(src) = 0;
    XContainerCapacity(src) = 0;
}

bool VXListBase_push_front_node(XListDLinked* this_list, XListDNode* node)
{
    if (!this_list || !node) return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return false;

    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    XListDNode* oldHead = *head_ptr;

    if (XListBase_isEmpty_base(this_list)) {
        *head_ptr = node;
        node->next = node;
        node->prev = node;
    }
    else {
        XListDNode* tail = oldHead->prev;
        node->next = oldHead;
        node->prev = tail;
        tail->next = node;
        oldHead->prev = node;
        *head_ptr = node;
    }

    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

bool VXListBase_push_back_node(XListDLinked* this_list, XListDNode* node)
{
    if (!this_list || !node) return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return false;

    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    XListDNode* oldHead = *head_ptr;

    if (XListBase_isEmpty_base(this_list)) {
        *head_ptr = node;
        node->next = node;
        node->prev = node;
    }
    else {
        XListDNode* tail = oldHead->prev;
        node->prev = tail;
        node->next = oldHead;
        tail->next = node;
        oldHead->prev = node;
        // 头节点不变
    }

    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

XListDNode* VXList_push_front(XListDLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XListDNode* newNode = CreatNode(this_list);
    if (!newNode) return NULL;

    if (dataCreatMethod) {
        memset(XListDNode_DataPtr(newNode), 0, XContainerTypeSize(this_list));
        dataCreatMethod(XListDNode_DataPtr(newNode), pvData);
    }
    else {
        memcpy(XListDNode_DataPtr(newNode), pvData, XContainerTypeSize(this_list));
    }

    if (VXListBase_push_front_node(this_list, newNode))
        return newNode;

    XFree_System(newNode);
    return NULL;
}

XListDNode* VXList_push_back(XListDLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XListDNode* newNode = CreatNode(this_list);
    if (!newNode) return NULL;

    if (dataCreatMethod) {
        memset(XListDNode_DataPtr(newNode), 0, XContainerTypeSize(this_list));
        dataCreatMethod(XListDNode_DataPtr(newNode), pvData);
    }
    else {
        memcpy(XListDNode_DataPtr(newNode), pvData, XContainerTypeSize(this_list));
    }

    if (VXListBase_push_back_node(this_list, newNode))
        return newNode;

    XFree_System(newNode);
    return NULL;
}

void VXList_inserts(XListDLinked* this_list, XListDNode* curNode, void* pvData, size_t n, XCDataCreatMethod dataCreatMethod)
{
    if (!curNode) return;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return;

    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    XListDNode* left = curNode->prev;
    size_t typeSize = XContainerTypeSize(this_list);

    for (size_t i = 0; i < n; ++i) {
        XListDNode* newNode = CreatNode(this_list);
        if (!newNode) return;

        if (dataCreatMethod) {
            memset(XListDNode_DataPtr(newNode), 0, typeSize);
            dataCreatMethod(XListDNode_DataPtr(newNode), pvData);
        }
        else {
            memcpy(XListDNode_DataPtr(newNode), pvData, typeSize);
        }

        newNode->prev = left;
        newNode->next = curNode;
        left->next = newNode;
        curNode->prev = newNode;
        left = newNode;

        if (curNode == *head_ptr) {
            *head_ptr = newNode;
        }

        ++XContainerSize(this_list);
        ++XContainerCapacity(this_list);
    }
}

bool VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (curNode == NULL)
    {
        printf("节点指针不能为空\n");
        return false;
    }
    VXList_inserts(this_list, curNode, pvData, 1, dataCreatMethod);
    return true;
}

size_t VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* array, size_t count, XCDataCreatMethod dataCreatMethod)
{
    size_t typeSize = XContainerTypeSize(this_list);
    const char* src = (const char*)array;
    for (size_t i = 0; i < count; ++i) {
        const void* elem = src + i * typeSize;
        if (curNode == NULL) {
            VXList_push_back(this_list, (void*)elem, dataCreatMethod);
        }
        else {
            VXList_inserts(this_list, curNode, (void*)elem, 1, dataCreatMethod);
        }
    }
    return count;
}
//根据节点指针删除
static void removeNode(XListDLinked* this_list, XListDNode* node)
{
    if (!node) return;
    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    XListDNode* head = *head_ptr;

    if (head == node && head->next == head) {
        // 只有一个节点
        *head_ptr = NULL;
    }
    else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        if (head == node) {
            *head_ptr = node->next;
        }
    }

    if (XContainerDataDeinitMethod(this_list))
        XContainerDataDeinitMethod(this_list)(XListDNode_DataPtr(node));
    XFree_System(node);

    --XContainerSize(this_list);
    --XContainerCapacity(this_list);
}

//头删
bool VXList_pop_front(XListDLinked* this_list)
{
    if (XListBase_isEmpty_base(this_list)) return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return false;
    XListDNode* head = *XListDLinked_head_ptr(this_list);
    removeNode(this_list, head);
    return true;
}

bool VXList_pop_back(XListDLinked* this_list)
{
    if (XListBase_isEmpty_base(this_list)) return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return false;
    XListDNode* head = *XListDLinked_head_ptr(this_list);
    if (!head) return false;
    removeNode(this_list, head->prev);
    return true;
}
//迭代器删除
void VXList_erase(XListDLinked* this_list, const XListDLinked_iterator* it, XListDLinked_iterator* next)
{
    if (XListBase_isEmpty_base(this_list) || !it->node) {
        if (next) *next = XListDLinked_end(this_list);
        return;
    }
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) {
        if (next) *next = XListDLinked_end(this_list);
        return;
    }

    XListDNode* node = (XListDNode*)it->node;
    XListDNode* nextNode = node->next;
    if (node == nextNode) nextNode = NULL; // 只有一个节点

    removeNode(this_list, node);
    if (next) next->node = nextNode;
}

bool VXList_remove(XListDLinked* this_list, void* pvData)
{
    XListDLinked_iterator it;
    if (!XListDLinked_find_base(this_list, pvData, &it)) return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list)) return false;
    removeNode(this_list, it.node);
    return true;
}

void VXList_clear(XListDLinked* this_list)
{
    if (XListBase_isEmpty_base(this_list)) return;

    // COW 模式且共享：直接丢弃共享块
    if (XContainerIsCow(this_list) && XContainerSharedData(this_list) && XSharedData_isShared(XContainerSharedData(this_list))) {
        XSharedData_release(XContainerSharedData(this_list));
        XContainerSharedData(this_list) = NULL;
        XContainerSize(this_list) = 0;
        XContainerCapacity(this_list) = 0;
        return;
    }

    // 其他情况：遍历删除节点
    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    XListDNode* head = *head_ptr;
    if (head) {
        XListDNode* node = head;
        do {
            XListDNode* next = node->next;
            if (XContainerDataDeinitMethod(this_list))
                XContainerDataDeinitMethod(this_list)(XListDNode_DataPtr(node));
            XFree_System(node);
            node = next;
        } while (node != head);
    }
    *head_ptr = NULL;
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}

void* VXList_front(XListDLinked* this_list)
{
    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    if (!head_ptr) return NULL;
    XListDNode* head = *head_ptr;
    return head ? XListDNode_DataPtr(head) : NULL;
}

void* VXList_back(XListDLinked* this_list)
{
    XListDNode** head_ptr = XListDLinked_head_ptr(this_list);
    if (!head_ptr) return NULL;
    XListDNode* head = *head_ptr;
    return head ? XListDNode_DataPtr(head->prev) : NULL;
}

bool VXList_find(const XListDLinked* this_list, void* pvData, XListDLinked_iterator* it)
{
    if (XListBase_isEmpty_base(this_list)) {
        if (it) *it = XListDLinked_end((XListDLinked*)this_list);
        return false;
    }
    XListDNode** head_ptr = XListDLinked_head_ptr((XListDLinked*)this_list);
    if (!head_ptr) return false;
    XListDNode* head = *head_ptr;
    if (!head) return false;

    XListDNode* node = head;
    do {
        if (XContainerCompare(this_list)) {
            if (XContainerCompare(this_list)(XListDNode_DataPtr(node), pvData) == XCompare_Equality) {
                if (it) it->node = node;
                return true;
            }
        }
        else if (memcmp(XListDNode_DataPtr(node), pvData, XContainerTypeSize(this_list)) == 0) {
            if (it) it->node = node;
            return true;
        }
        node = node->next;
    } while (node != head);

    if (it) *it = XListDLinked_end((XListDLinked*)this_list);
    return false;
}
//其他

void VXList_deinit(XListDLinked* this_list)
{
    if (XContainerIsCow(this_list)) {
        if (XContainerSharedData(this_list))
            XSharedData_release_with(XContainerSharedData(this_list), VXListDLinkedDataDelete, this_list);
    }
    else {
        XListDNode* head = (XListDNode*)XContainerDataPtr(this_list);
        if (head) {
            XListDNode* node = head;
            do {
                XListDNode* next = node->next;
                if (XContainerDataDeinitMethod(this_list))
                    XContainerDataDeinitMethod(this_list)(XListDNode_DataPtr(node));
                XFree_System(node);
                node = next;
            } while (node != head);
        }
        XContainerDataPtr(this_list) = NULL;
    }
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}
//排序
//一次快排
static struct XListDNode* List_OneSort(XListDNode* ListHead, XListDNode* ListTail, const size_t type, XCompare compare, XSortOrder order)
{

    char* compareVal = XMalloc_System(type);
    if (compareVal == NULL)
        return NULL;
    memcpy(compareVal, &(ListHead->data), type);
    int32_t cmp;
    while (ListHead != ListTail)
    {
        while (ListHead != ListTail)//右边开始往左边找
        {
            //if (!Sort(&(ListTail->data), compareVal))
            cmp = compare(&(ListTail->data), compareVal);
            if (!((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
            {
                ListTail = ListTail->prev;
            }
            else
            {
                memcpy(&(ListHead->data), &(ListTail->data), type);
                break;
            }
        }
        while (ListHead != ListTail)//左边开始往右边找
        {
            //if (Sort(&(ListHead->data), compareVal))
            cmp = compare(&(ListHead->data), compareVal);
            if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
            {
                ListHead = ListHead->next;
            }
            else
            {
                memcpy(&(ListTail->data), &(ListHead->data), type);
                break;
            }
        }
    }
    memcpy(&(ListTail->data), compareVal, type);
    XFree_System(compareVal);
    //单次结束，分割节点
    return ListHead;

}

void VXList_sort(XListDLinked* this_list, XSortOrder order)
{
#if XStack_ON
    if (ISNULL(this_list, "") || XListBase_isEmpty_base(this_list) || XContainerCompare(this_list) == NULL)
        return;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return;
    XListBase* list = this_list;
    XListDNode* ListHead = *XListDLinked_head_ptr(this_list);
    XListDNode* ListTail = ListHead->prev;//链表最后一个节点
    XStack* stack = XStack_Create(XListDNode*);
    XStack_push_base(stack, &ListTail);
    XStack_push_base(stack, &ListHead);
    while (!XStack_isEmpty_base(stack))
    {
        //获取节点
        XListDNode* ListHead = *((struct XListDNode**)XStack_top_base(stack));
        XStack_pop_base(stack);
        XListDNode* ListTail = *((struct XListDNode**)XStack_top_base(stack));
        XStack_pop_base(stack);
        //单次排序
        XListDNode* ListMiddle = List_OneSort(ListHead, ListTail, list->m_class.m_typeSize, XContainerCompare(this_list),order);
        //判断左区间是否存在
        if (ListHead != ListMiddle && ListHead->next != ListMiddle)
        {
            XStack_push_base(stack, &ListMiddle->prev);
            XStack_push_base(stack, &ListHead);
        }
        //判断右区间是否存在
        if (ListTail != ListMiddle && ListMiddle->next != ListTail)
        {
            XStack_push_base(stack, &ListTail);
            XStack_push_base(stack, &ListMiddle->next);
        }
    }
    XStack_delete_base(stack);
#else
    IS_ON_DEBUG(XStack_ON);
#endif
}
#endif