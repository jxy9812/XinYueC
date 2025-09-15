#include"XListSLinked.h"
#if XListSLinked_ON
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
//插入
static bool VXListBase_push_front_node(XListSLinked* this_list, XListSNode* node);
static bool VXListBase_push_back_node(XListSLinked* this_list, XListSNode* node);
static XListSNode * VXList_push_front(XListSLinked * this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static XListSNode* VXList_push_back(XListSLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod);
static bool VXList_insert(XListSLinked* this_list, XListSNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod);
static size_t VXList_insert_array(XListSLinked* this_list, XListSNode* curNode, const void* array, size_t count, XCDataCreatMethod dataCreatMethod);
//删除
static bool VXList_pop_front(XListSLinked* this_list);
static bool VXList_pop_back(XListSLinked* this_list);
static void VXList_erase(XListSLinked* this_list, const XListSLinked_iterator* it, XListSLinked_iterator* next);
static bool VXList_remove(XListSLinked* this_list, void* pvData);
static void VXList_clear(XListSLinked* this_list);
//遍历
static void* VXList_front(XListSLinked* this_list);
static void* VXList_back(XListSLinked* this_list);
static bool VXList_find(const XListSLinked* this_list, void* pvData, XListSLinked_iterator* it);
//其他
static void VXList_sort(XListSLinked* this_list, XSortOrder order);
static void VXClass_copy(XListSLinked* object, const XListSLinked* src);
static void VXClass_move(XListSLinked* object, XListSLinked* src);
static void VXList_deinit(XListSLinked* this_list);

XVtable* XListSLinked_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XLISTSLINKED_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

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
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXList_clear);

#if SHOWCONTAINERSIZE
    printf("XListSLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

#define CreatNode(this_list)   (XMemory_malloc(sizeof(XListSNode*)+XContainerTypeSize(this_list)))

bool VXListBase_push_front_node(XListSLinked* this_list, XListSNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    XListSNode* head = XContainerDataPtr(this_list);//获取头指针
    XContainerDataPtr(this_list) = node;//新节点成为新的头
    node->next = head;//修改指向下一个节点为原先的头
    if (XListBase_isEmpty_base(this_list))
        this_list->m_tail = node;
    //更新记录数量
    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

bool VXListBase_push_back_node(XListSLinked* this_list, XListSNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    if (this_list->m_tail)
        this_list->m_tail->next = node;//尾指针指向新节点
    node->next = NULL;//新节点指向NULL
    this_list->m_tail = node;//更新记录的尾节点
    if (XListBase_isEmpty_base(this_list))
        XContainerDataPtr(this_list) = node;
    //更新记录数量
    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

XListSNode* VXList_push_front(XListSLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XListBase* list = this_list;
    XListSNode* newNode = CreatNode(this_list);//新节点
    if (newNode == NULL)
    {
        perror("开辟节点失败");
        return NULL;
    }
    if (dataCreatMethod)
    {
        memset(XListSNode_DataPtr(newNode),0, XContainerTypeSize(this_list));
        dataCreatMethod(XListSNode_DataPtr(newNode), pvData);
    }
    else
    {
        memcpy(XListSNode_DataPtr(newNode), pvData, XContainerTypeSize(this_list));//拷贝数据
    }
    if(VXListBase_push_front_node(this_list,newNode))
        return newNode;
    return NULL;
}

XListSNode* VXList_push_back(XListSLinked* this_list, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    XListBase* list = this_list;
    XListSNode* NewNode = CreatNode(this_list);//新节点
    if (NewNode == NULL)
    {
        perror("开辟节点失败");
        return NULL;
    }
    if (dataCreatMethod)
    {
        memset(XListSNode_DataPtr(NewNode), 0, XContainerTypeSize(this_list));
        dataCreatMethod(XListSNode_DataPtr(NewNode), pvData);
    }
    else
    {
        memcpy(XListSNode_DataPtr(NewNode), pvData, XContainerTypeSize(this_list));//拷贝数据
    }
    if (VXListBase_push_back_node(this_list, NewNode))
        return NewNode;
    return NULL;
}

bool VXList_insert(XListSLinked* this_list, XListSNode* curNode, void* pvData, XCDataCreatMethod dataCreatMethod)
{
    if (curNode == NULL)
    {
        XListBase_push_back_base(this_list, pvData);
        return true;
    }
    //遍历节点
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = XContainerDataPtr(this_list);//当前节点
    while (node)
    {
        if (node == curNode)
            break;//找到节点后跳出循环
        //没找到看下一个
        prev = node;
        node = node->next;
    }
    if (node == NULL)
        return false;//没找到插入失败
    //创建一个新的节点
    XListSNode* NewNode = CreatNode(this_list);//新节点
    if (NewNode == NULL)
    {
        perror("开辟节点失败");
        return false;
    }
    if (dataCreatMethod)
    {
        memset(XListSNode_DataPtr(NewNode), 0, XContainerTypeSize(this_list));
        dataCreatMethod(XListSNode_DataPtr(NewNode), pvData);
    }
    else
    {
        memcpy(XListSNode_DataPtr(NewNode), pvData, XContainerTypeSize(this_list));//拷贝数据
    }
    NewNode->next = node;//链接节点
    //更新节点信息
    if (prev == NULL)
    {
        XContainerDataPtr(this_list) = NewNode;//更新头节点
    }
    else
    {
        prev->next = NewNode;//
    }
    //更新记录数量
    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return true;
}

size_t VXList_insert_array(XListSLinked* this_list, XListSNode* curNode, const void* array, size_t count, XCDataCreatMethod dataCreatMethod)
{
    //遍历节点
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = XContainerDataPtr(this_list);//当前节点
    if (curNode != NULL)
    {
        while (node)
        {
            if (node == curNode)
                break;//找到节点后跳出循环
            //没找到看下一个
            prev = node;
            node = node->next;
        }
        if (node == NULL)
            return 0;//没找到插入失败
    }

    //开始将数组数据构建成链表
    XListSNode* NewListHead = NULL;
    XListSNode* NewListNode = NULL;
    XListSNode* NewListTail = NULL;
    for (size_t i = 0; i < count; i++)
    {
        //创建一个新的节点
        NewListNode = CreatNode(this_list);//新节点
        if (NewListNode == NULL)
        {
            perror("开辟节点失败");
            // 释放已创建的节点
            while (NewListHead != NULL) {
                XListSNode* temp = NewListHead;
                NewListHead = NewListHead->next;
                if (XContainerDataDeinitMethod(this_list) != NULL) {
                    XContainerDataDeinitMethod(this_list)(&temp->data);
                }
                XMemory_free(temp);
            }
            return 0;
        }
        if (dataCreatMethod)
        {
            memset(XListSNode_DataPtr(NewListNode), 0, XContainerTypeSize(this_list));
            dataCreatMethod(XListSNode_DataPtr(NewListNode), ((char*)array) + i * XContainerTypeSize(this_list));
        }
        else
        {
            memcpy(XListSNode_DataPtr(NewListNode), ((char*)array) + i * XContainerTypeSize(this_list), XContainerTypeSize(this_list));//拷贝数据
        }
       
        if (NewListTail == NULL)
        {//第一个节点
            NewListTail = NewListNode;
            NewListHead = NewListNode;
        }
        else
        {
            NewListTail->next = NewListNode;
            NewListTail = NewListNode;
        }
    }
    if (XContainerDataPtr(this_list) == NULL)
    {//链表是空的情况
        XContainerDataPtr(this_list) = NewListHead;//更新头节点
        this_list->m_tail = NewListTail;//更新尾节点
        NewListTail->next = NULL;
        //更新数量
        XContainerSize(this_list) += count;
        XContainerCapacity(this_list) += count;
        return count;
    }

    //开始将两个链表合并起来
    if (curNode == NULL)
    {//要插入到链表尾部	
        this_list->m_tail->next = NewListHead;//链接头尾
        this_list->m_tail = NewListTail;//更新尾节点
        NewListTail->next = NULL;
    }
    else if (curNode == XContainerDataPtr(this_list))
    {//插入到链表头
        NewListTail->next = XContainerDataPtr(this_list);
        XContainerDataPtr(this_list) = NewListHead;
    }
    else
    {//插入到原先链表中间
        prev->next = NewListHead;
        NewListTail->next = node;
    }
    //更新数量
    XContainerSize(this_list) += count;
    XContainerCapacity(this_list) += count;
    return count;
}
//删除一个节点
static void removeNode(XListSLinked* this_list, XListSNode* prev, XListSNode* removeNode)
{
    if (prev == NULL)
    {//删除的是头节点
        XContainerDataPtr(this_list) = removeNode->next;
    }
    else
    {
        prev->next = removeNode->next;
    }
    if (removeNode->next == NULL)
    {//删除的是尾节点
        this_list->m_tail = prev;
        if (prev)
            prev->next = NULL;
    }
    if (XContainerDataDeinitMethod(this_list) != NULL)
        XContainerDataDeinitMethod(this_list)(&(removeNode->data));
    XMemory_free(removeNode);
    //更新数量
    --XContainerSize(this_list);
    --XContainerCapacity(this_list);
}

bool VXList_pop_front(XListSLinked* this_list)
{
    XListSNode* head = XContainerDataPtr(this_list);
    if (head == NULL)
        return false;//链表是空的
    removeNode(this_list,NULL, head);
    return true;
}

bool VXList_pop_back(XListSLinked* this_list)
{
    XListSNode* tail = this_list->m_tail;
    if (tail == NULL)
        return false;
    XListSNode* node = XContainerDataPtr(this_list);//当前节点
    while (node)
    {
        if (node->next == tail)
        {
            removeNode(this_list, node, tail);
            break;
        }
        node = node->next;
    }
    return true;
}


void VXList_erase(XListSLinked* this_list, const XListSLinked_iterator* it, XListSLinked_iterator* next)
{
    if (XListBase_isEmpty_base(this_list) || it->node == NULL)
    {
        if (next)
            *next = XListSLinked_end(this_list);
        return;
    }
    XListSNode* node = it->node;
    XListSNode* prev = NULL;//前一个节点
    XListSNode* curNode = XContainerDataPtr(this_list);//当前节点
    while (curNode)
    {
        if (curNode == node)
        {//找到要删除的节点了
            XListSNode* next = curNode->next;
            removeNode(this_list, prev, curNode);
            if (next)
                next->next = next;
            return ;
        }
        prev = curNode;
        curNode = curNode->next;
    }
    if (next)
        *next = XListSLinked_end(this_list);
}

bool VXList_remove(XListSLinked* this_list, void* pvData)
{
    if (XListBase_isEmpty_base(this_list))
        return false;
    if (XContainerCompare(this_list) == NULL)
        return false;
    XListSNode* prev = NULL;//前一个节点
    XListSNode* curNode = XContainerDataPtr(this_list);//当前节点
    while (curNode)
    {
        if (XContainerCompare(this_list)(&(curNode->data), pvData)==XCompare_Equality)
        {//找到了
            removeNode(this_list, prev, curNode);
            return true;
        }
        prev = curNode;
        curNode = curNode->next;
    }
    return false;
}

void VXList_clear(XListSLinked* this_list)
{
    if (XListBase_isEmpty_base(this_list))
        return;
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = XContainerDataPtr(this_list);//当前节点
    while (node)
    {
        prev = node;
        node = node->next;
        if (XContainerDataDeinitMethod(this_list) != NULL)
            XContainerDataDeinitMethod(this_list)(&(prev->data));
        XMemory_free(prev);
    }
    this_list->m_tail = NULL;
    XContainerDataPtr(this_list) = NULL;
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}

void* VXList_front(XListSLinked* this_list)
{
    if (XContainerDataPtr(this_list))
        return XListSNode_DataPtr(XContainerDataPtr(this_list));
    return NULL;
}

void* VXList_back(XListSLinked* this_list)
{
    if (this_list->m_tail)
        return &(this_list->m_tail->data);
    return NULL;
}

bool VXList_find(const XListSLinked* this_list, void* pvData, XListSLinked_iterator* it)
{
    if (XListBase_isEmpty_base(this_list))
    {
        if (it)
            *it = XListSLinked_end(this_list);
        return false;
    }
    XListSNode* node = XContainerDataPtr(this_list);//当前节点
    while (node)
    {
        if (XContainerCompare(this_list))
        {
            if (XContainerCompare(this_list)(XListSNode_DataPtr(node), pvData)==XCompare_Equality)
            {
                if (it)
                    it->node = node;
                return true;
            }
        }
        else if (memcmp(XListSNode_DataPtr(node), pvData, XContainerTypeSize(this_list)) == 0)
        {
            if (it)
                it->node = node;
            return true;
        }
        node = node->next;
    }
    if (it)
        *it = XListSLinked_end(this_list);
    return false;
}
// 找到链表尾部节点
static XListSNode* findTail(XListSNode* head) {
    if (head == NULL) return NULL;
    while (head->next != NULL)
        head = head->next;
    return head;
}
// 单向链表的一次快排（分区函数）
static XListSNode* List_OneSort(XListSNode* left, XListSNode* right, size_t typeSize, XCompare compare, XSortOrder order) 
{
    if (left == NULL || right == NULL || left == right)
        return left;

    void* pivot = XMemory_malloc(typeSize);
    if (pivot == NULL) return NULL;
    memcpy(pivot, &(left->data), typeSize);

    XListSNode* i = left;    // 分区点
    XListSNode* j = left->next;
    int32_t cmp;
    while (j != NULL) {
        //if (compare(&(j->data), pivot)) 
        cmp = compare(&(j->data), pivot);
        if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
        {
            i = i->next;
            // 交换i和j的数据

            void* temp = XMemory_malloc(typeSize);
            memcpy(temp, &(i->data), typeSize);
            memcpy(&(i->data), &(j->data), typeSize);
            memcpy(&(j->data), temp, typeSize);
            XMemory_free(temp);
        }
        if (j == right) break;  // 到达右边界
        j = j->next;
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
void VXList_sort(XListSLinked* this_list, XSortOrder order)
{
#if XStack_ON
    if (XListBase_isEmpty_base(this_list)|| XContainerCompare(this_list)==NULL)
        return;
    //printf("进入排序\n");
    XListSNode* head = XContainerDataPtr(this_list);
    XListSNode* tail = findTail(head);

    // 使用现有的XStack
    XStack* stack = XStack_Create(XListSNode*);
    if (stack == NULL)
        return;

    // 初始化栈
    if (head != NULL) {
        XStack_push_base(stack, &tail);
        XStack_push_base(stack, &head);
    }

    while (!XStack_isEmpty_base(stack)) {
        // 弹出区间
        XListSNode* h = *((XListSNode**)XStack_top_base(stack));
        XStack_pop_base(stack);
        XListSNode* t = *((XListSNode**)XStack_top_base(stack));
        XStack_pop_base(stack);

        if (h == NULL || t == NULL || h == t)
            continue;

        // 执行一次快排
        XListSNode* pivot = List_OneSort(h, t, XContainerTypeSize(this_list), XContainerCompare(this_list),order);

        // 处理左子区间
        if (h != pivot) {
            XListSNode* leftTail = findTail(h);
            if (leftTail != NULL && h != leftTail) {
                XStack_push_base(stack, &leftTail);
                XStack_push_base(stack, &h);
            }
        }

        // 处理右子区间
        if (pivot != NULL && pivot->next != NULL) {
            XListSNode* rightHead = pivot->next;
            XListSNode* rightTail = findTail(rightHead);
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

void VXClass_copy(XListSLinked* object, const XListSLinked* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListSLinked_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
    for_each_iterator(src, XListSLinked, it)
    {
        XListBase_push_back_base(object, XListSLinked_iterator_data(&it));
    }
}

void VXClass_move(XListSLinked* object, XListSLinked* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListSLinked_init(object, XContainerTypeSize(src));
    }
    else if (!XListBase_isEmpty_base(object))
    {
        XListBase_clear_base(object);
    }
    XSwap(object, src, sizeof(XListSLinked));
}

void VXList_deinit(XListSLinked* this_list)
{
    XListBase_clear_base(this_list);
    //XMemory_free(this_list);
}

#endif