#include"XListDLinked.h"
#if XListDLinked_ON
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
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
    if (!XContainerSharedData(this_list))
    {
        XContainerSharedData(this_list) = XSharedData_create(NULL, sizeof(XListDNode*));
        if (!XContainerSharedData(this_list))
            return false;
        *(XListDNode**)XContainerSharedDataPtr(this_list) = NULL;
    }
    return true;
}

// COW分离：如果数据被共享，创建独立副本（深拷贝双向链表节点）
static bool VXListDLinkedDetachIfNeeded(XListDLinked* this_list)
{
    if (!XContainerSharedData(this_list) || !XSharedData_isShared(XContainerSharedData(this_list)))
        return true; // 不共享，无需分离

    size_t typeSize = XContainerTypeSize(this_list);
    XListDNode* oldHead = *(XListDNode**)XContainerSharedDataPtr(this_list);

    // 创建新的 XSharedData 存储头节点指针
    XSharedData* newShared = XSharedData_create(NULL, sizeof(XListDNode*));
    if (!newShared) return false;
    XListDNode* newHead = NULL;
    XListDNode* newTail = NULL;

    // 深拷贝整个双向链表
    XListDNode* oldNode = oldHead;
    if (oldNode)
    {
        do
        {
            XListDNode* newNode = XMalloc_System(ALIGN_UP(sizeof(XListDNode) + typeSize, sizeof(void*)));
            if (!newNode)
            {
                // 释放已创建的节点
                XListDNode* tmp = newHead;
                while (tmp)
                {
                    XListDNode* next = tmp->next;
                    XFree_System(tmp);
                    tmp = next;
                    if (tmp == newHead) break; // 循环链表
                }
                XSharedData_release(newShared);
                return false;
            }
            // 拷贝数据
            if (XContainerDataCopyMethod(this_list))
                XContainerDataCopyMethod(this_list)(XListDNode_DataPtr(newNode), XListDNode_DataPtr(oldNode));
            else
                memcpy(XListDNode_DataPtr(newNode), XListDNode_DataPtr(oldNode), typeSize);

            if (!newHead)
            {
                newHead = newNode;
                newTail = newNode;
                newNode->next = newNode;
                newNode->prev = newNode;
            }
            else
            {
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

    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(this_list));
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
    // 如果目标还未初始化，先初始化
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListDLinked_init(object, XContainerTypeSize(src));
    }
    else if (XContainerSharedData(object))// 释放目标原有数据
    {
        XSharedData_release_with(XContainerSharedData(object), VXListDLinkedDataDelete, object);
    }

    // 复制回调函数
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSharedData(object) = XContainerSharedData(src);
    if (XContainerSharedData(object))
    {
        XSharedData_addRef(XContainerSharedData(object));
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    XContainerTypeSize(object) = XContainerTypeSize(src);
}

void VXClass_move(XListDLinked* object, XListDLinked* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListDLinked_init(object, XContainerTypeSize(src));
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXListDLinkedDataDelete, object);
    }

    // 转移所有权（指针拷贝）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XListDLinked) - sizeof(XClass));

    // 清空源对象
    XContainerSharedData(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
}

bool VXListBase_push_front_node(XListDLinked* this_list, XListDNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return false;
    if (VXListBase_push_back_node(this_list, node))
    {
        *(XListDNode**)XContainerSharedDataPtr(this_list) = node;
        return true;
    }
    return false;
}

bool VXListBase_push_back_node(XListDLinked* this_list, XListDNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return false;
    if (XListBase_isEmpty_base(this_list))
    {
        *(XListDNode**)XContainerSharedDataPtr(this_list) = node;
        node->next = node;
        node->prev = node;
    }
    else
    {
        XListDNode* pfront = *(XListDNode**)XContainerSharedDataPtr(this_list);//原头节点
        XListDNode* pback = pfront->prev;//原尾节点
        node->next = pfront;
        node->prev = pback;
        pfront->prev = node;
        pback->next = node;
    }
    //更新记录数量
    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

XListDNode* VXList_push_front(XListDLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    /*if (ISNULL(this_list, ""))
        return NULL;*/
    //XListBase* list = this_list;
   // XListDNode* node = XListDLinked_push_back_base(this_list, pvData);
    XListDNode* NewNode = XClassGetVirtualFunc(this_list, EXListBase_Push_Back, XListBaseNode * (*)(XListBase*, void*, XCDataCreatMethod))(this_list, pvData, dataCreatMethod);
    if (NewNode)
    {
        *(XListDNode**)XContainerSharedDataPtr(this_list) = NewNode;
    }
    return NewNode;
}

XListDNode* VXList_push_back(XListDLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    /*if (ISNULL(this_list, ""))
        return NULL;*/
    //XListBase* list = this_list;
    XListDNode* newNode = CreatNode(this_list);//新节点
    if (newNode == NULL)
    {
        perror("开辟节点失败");
        return NULL;
    }
    if (dataCreatMethod)
    {
        memset(XListDNode_DataPtr(newNode), 0, XContainerTypeSize(this_list));
        dataCreatMethod(XListDNode_DataPtr(newNode), pvData);
    }
    else
    {
        memcpy(XListDNode_DataPtr(newNode), pvData, XContainerTypeSize(this_list));//拷贝数据
    }
    if (VXListBase_push_back_node(this_list, newNode))
        return newNode;
    return NULL;
}

void VXList_inserts(XListDLinked* this_list, XListDNode* curNode, void* pvData, size_t n, XCDataCreatMethod dataCreatMethod)
{
    //XListBase* list = this_list;
    //if (ISNULL(this_list, ""))
    //    return;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return;
    for (size_t i = 0; i < n; i++)
    {
        /*Node* pval = List_find(li, p);*/
        if (curNode != NULL)
        {
            XListDNode* left = curNode->prev;
            //XListDNode* right = left->next;

            XListDNode* newNode = CreatNode(this_list);//新节点
            if (newNode == NULL)
            {
                perror("开辟节点失败");
                return;
            }
           
            if (dataCreatMethod)
            {
                memset(XListDNode_DataPtr(newNode), 0, XContainerTypeSize(this_list));
                dataCreatMethod(XListDNode_DataPtr(newNode), pvData);
            }
            else
            {
                memcpy(XListDNode_DataPtr(newNode), pvData, XContainerTypeSize(this_list));//拷贝数据
            }

            newNode->prev = left;
            newNode->next = curNode;
            left->next = newNode;
            curNode->prev = newNode;

            if (curNode == *(XListDNode**)XContainerSharedDataPtr(this_list))
            {
                *(XListDNode**)XContainerSharedDataPtr(this_list) = newNode;
            }
            ++XContainerSize(this_list);
            ++XContainerCapacity(this_list);
        }
        else
        {
           //perror("插入的数找不到");
        }
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
    if (curNode == NULL)
    {//尾插
        for (size_t i = 0; i < count; i++)
        {
            XClassGetVirtualFunc(this_list, EXListBase_Push_Back, XListBaseNode * (*)(XListBase*, void*, XCDataCreatMethod))(this_list, ((char*)array) + i * XContainerTypeSize(this_list), dataCreatMethod);
            //XListBase_push_back_base(this_list, ((char*)array) + i * XContainerTypeSize(this_list));
        }
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            VXList_inserts(this_list, curNode, ((char*)array) + i * XContainerTypeSize(this_list), 1,dataCreatMethod);
        }
    }
    return count;
}
//根据节点指针删除
static bool removeNode(XListDLinked* this_list, XListDNode* node)
{
    if (ISNULL(node, "") || XContainer_isEmpty_base(this_list))
        return false;
    XListDNode* nextNode = node->next;//下一个节点
    XListDNode* prevNode = node->prev;//上一个节点
    if (node->data)
    {
        if (XContainerDataDeinitMethod(this_list) != NULL)
            XContainerDataDeinitMethod(this_list)(&(node->data));
    }
    XFree_System(node);//释放节点
    if (XContainerSize(this_list) == 1)
    {
        *(XListDNode**)XContainerSharedDataPtr(this_list) = NULL;
    }
    else
    {
        nextNode->prev = prevNode;
        prevNode->next = nextNode;
        if (*(XListDNode**)XContainerSharedDataPtr(this_list) == node)
            *(XListDNode**)XContainerSharedDataPtr(this_list) = nextNode;//重新设置头节点
    }
    --XContainerSize(this_list);
    --XContainerCapacity(this_list);
    return true;
}

//头删
bool VXList_pop_front(XListDLinked* this_list)
{
    if (XContainer_isEmpty_base(this_list))
        return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return false;
    removeNode(this_list, *(XListDNode**)XContainerSharedDataPtr(this_list));
    return true;
}
//尾删
bool VXList_pop_back(XListDLinked* this_list)
{
    if (XContainer_isEmpty_base(this_list))
        return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return false;
    removeNode(this_list, (*(XListDNode**)XContainerSharedDataPtr(this_list))->prev);
    return true;
}
//迭代器删除
void VXList_erase(XListDLinked* this_list, const XListDLinked_iterator* it, XListDLinked_iterator* next)
{
    if (XListBase_isEmpty_base(this_list)||it->node==NULL)//链表为空或者迭代器已经是end
    {
        if (next)
            *next = XListDLinked_end(this_list);
        return;
    }
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
    {
        if (next)
            *next = XListDLinked_end(this_list);
        return;
    }
    //先获取下一个迭代器
    XListDNode* head = *(XListDNode**)XContainerSharedDataPtr(this_list);
    XListDLinked_iterator* back = head->prev;
    if (it->node == back)//如果是最后一个元素则返回空表示遍历完成了
    {
        if (next)
            *next = XListDLinked_end(this_list);
    }
    else if (next)
    {
        next->node = ((XListDNode*)(it->node))->next;//指向下一个元素
    }
    //正式删除
    removeNode(this_list, it->node);
}

bool VXList_remove(XListDLinked* this_list, void* pvData)
{
    if (ISNULL(pvData, ""))
        return false;
    if (XContainer_isEmpty_base(this_list))
        return false;
    if (!ensureSharedData(this_list) || !VXListDLinkedDetachIfNeeded(this_list))
        return false;
    XListDLinked_iterator it;
    if (!XListDLinked_find_base(this_list, pvData, &it))
        return false;
    XListDNode* node = it.node;
    removeNode(this_list, node);
    return true;
}

void VXList_clear(XListDLinked* this_list)
{
    if (XContainer_isEmpty_base(this_list))
        return;
    // 如果数据被共享，减少引用并置空
    if (XContainerSharedData(this_list) && XSharedData_isShared(XContainerSharedData(this_list)))
    {
        XSharedData_release(XContainerSharedData(this_list));
        XContainerSharedData(this_list) = NULL;
        XContainerSize(this_list) = 0;
        XContainerCapacity(this_list) = 0;
        return;
    }
    XListDNode* head = *(XListDNode**)XContainerSharedDataPtr(this_list);
    if (head)
    {
        XListDNode* node = head;
        do
        {
            XListDNode* next = node->next;
            if (XContainerDataDeinitMethod(this_list) != NULL)
                XContainerDataDeinitMethod(this_list)(&(node->data));
            XFree_System(node);
            node = next;
        } while (node != head);
    }
    *(XListDNode**)XContainerSharedDataPtr(this_list) = NULL;
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}

void* VXList_front(XListDLinked* this_list)
{
    if (!XContainerSharedData(this_list))
        return NULL;
    XListDNode* head = *(XListDNode**)XContainerSharedDataPtr(this_list);
    if (head)
        return XListDNode_DataPtr(head);
    return NULL;
}

void* VXList_back(XListDLinked* this_list)
{
    if (!XContainerSharedData(this_list))
        return NULL;
    XListDNode* head = *(XListDNode**)XContainerSharedDataPtr(this_list);
    if (head)
        return XListDNode_DataPtr(head->prev);
    return NULL;
}

bool VXList_find(const XListDLinked* this_list, void* pvData, XListDLinked_iterator* it)
{
    if (XListBase_isEmpty_base(this_list))
    {
        if (it)
            *it = XListDLinked_end(this_list);
        return false;
    }
    XListDNode* node = *(XListDNode**)XContainerSharedDataPtr(this_list);//当前节点
    for_each_iterator(this_list, XListDLinked, forIt)
    {
        void* curr=XListDLinked_iterator_data(&forIt);
        if (XContainerCompare(this_list))
        {
            if (XContainerCompare(this_list)(curr, pvData)==XCompare_Equality)
            {
                if (it)
                    *it = forIt;
                return true;
            }
        }
        else if (memcmp(curr, pvData, XContainerTypeSize(this_list)) == 0)
        {
            if (it)
                *it = forIt;
            return true;
        }
    }
    if (it)
        *it = XListDLinked_end(this_list);
    return false;
}
//其他

void VXList_deinit(XListDLinked* this_list)
{
    if (XContainerSharedData(this_list))
    {
        XSharedData_release_with(XContainerSharedData(this_list), VXListDLinkedDataDelete, this_list);
    }
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
    XContainerSharedData(this_list) = NULL;
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
    XListDNode* ListHead = *(XListDNode**)XContainerSharedDataPtr(this_list);//链表第一个节点
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