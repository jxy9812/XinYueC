#include"XListSLinked.h"
#if XListSLinked_ON
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<string.h>
// 获取头节点指针的地址（统一 COW/非 COW 模式）
static inline XListSNode** XListSLinked_head_ptr(XListSLinked* list) {
    if (XContainerIsCow(list)) {
        return (XListSNode**)XContainerSharedDataPtr(list);
    }
    else {
        return (XListSNode**)&XContainerDataPtr(list);
    }
}
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
//COW分离与数据删除
static bool VXListSLinkedDetachIfNeeded(XListSLinked* this_list);
static void VXListSLinkedDataDelete(void* data, XListSLinked* this_list);
// 确保 XSharedData 存在（首次写入时延迟创建）
static bool ensureSharedData(XListSLinked* this_list);

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
    printf("XListSLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}
#define CreatNode(this_list)    XMalloc_System(ALIGN_UP(sizeof(XListSNode)+XContainerTypeSize(this_list),sizeof(void*)))
//#define CreatNode(this_list)   (XMalloc_System(sizeof(XListSNode)+XContainerTypeSize(this_list)))

// 确保 XSharedData 存在（延迟创建）
static bool ensureSharedData(XListSLinked* this_list)
{
    if (XContainerIsCow(this_list)) {
        if (!XContainerSharedData(this_list)) {
            XSharedData* sd = XSharedData_create(NULL, sizeof(XListSNode*));
            if (!sd) return false;
            XContainerSharedData(this_list) = sd;
            *(XListSNode**)sd->data = NULL;
        }
    }
    else {
        // 非 COW 模式：确保头指针初始为 NULL
        if (!XContainerDataPtr(this_list)) {
            XContainerDataPtr(this_list) = NULL;
        }
    }
    return true;
}

// COW分离：如果数据被共享，创建独立副本（深拷贝链表节点）
static bool VXListSLinkedDetachIfNeeded(XListSLinked* this_list)
{
    if (!XContainerIsCow(this_list)) return true;  // 非 COW 永远不需要分离

    if (!XContainerSharedData(this_list) || !XSharedData_isShared(XContainerSharedData(this_list)))
        return true; // 不共享，无需分离

    size_t typeSize = XContainerTypeSize(this_list);
    XListSNode* oldHead = *XListSLinked_head_ptr(this_list);

    // 创建新的 XSharedData 存储头节点指针
    XSharedData* newShared = XSharedData_create(NULL, sizeof(XListSNode*));
    if (!newShared) return false;
    XListSNode* newHead = NULL;
    XListSNode* newTail = NULL;

    // 深拷贝整个链表
    XListSNode* oldNode = oldHead;
    while (oldNode)
    {
        XListSNode* newNode = XMalloc_System(ALIGN_UP(sizeof(XListSNode) + typeSize, sizeof(void*)));
        if (!newNode)
        {
            // 释放已创建的节点
            XListSNode* tmp = newHead;
            while (tmp) { XListSNode* next = tmp->next; XFree_System(tmp); tmp = next; }
            XSharedData_release(newShared);
            return false;
        }
        // 拷贝数据
        if (XContainerDataCopyMethod(this_list))
            XContainerDataCopyMethod(this_list)(XListSNode_DataPtr(newNode), XListSNode_DataPtr(oldNode));
        else
            memcpy(XListSNode_DataPtr(newNode), XListSNode_DataPtr(oldNode), typeSize);
        newNode->next = NULL;

        if (!newHead) { newHead = newNode; newTail = newNode; }
        else { newTail->next = newNode; newTail = newNode; }
        oldNode = oldNode->next;
    }

    *(XListSNode**)newShared->data = newHead;

    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(this_list));
    XContainerSharedData(this_list) = newShared;
    this_list->m_tail = newTail;
    return true;
}

// 删除链表数据（XSharedData释放回调）
static void VXListSLinkedDataDelete(void* data, XListSLinked* this_list)
{
    if (this_list == NULL) return;
    XListSNode* head = data ? *(XListSNode**)data : NULL;
    while (head)
    {
        XListSNode* next = head->next;
        if (XContainerDataDeinitMethod(this_list))
            XContainerDataDeinitMethod(this_list)(&(head->data));
        XFree_System(head);
        head = next;
    }
    this_list->m_tail = NULL;
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
    //XContainerSharedData(this_list) = NULL;
}

bool VXListBase_push_front_node(XListSLinked* this_list, XListSNode* node)
{
    if (this_list == NULL || node == NULL)
        return false;
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    XListSNode* head = *XListSLinked_head_ptr(this_list);//获取头指针
    *XListSLinked_head_ptr(this_list) = node;//新节点成为新的头
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
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    if (this_list->m_tail)
        this_list->m_tail->next = node;//尾指针指向新节点
    node->next = NULL;//新节点指向NULL
    this_list->m_tail = node;//更新记录的尾节点
    if (XListBase_isEmpty_base(this_list))
        *XListSLinked_head_ptr(this_list) = node;
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
    else if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    //遍历节点
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = *XListSLinked_head_ptr(this_list);//当前节点
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
        *XListSLinked_head_ptr(this_list) = NewNode;//更新头节点
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
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return 0;
    //遍历节点
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = *XListSLinked_head_ptr(this_list);//当前节点
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
                XFree_System(temp);
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
    if (*XListSLinked_head_ptr(this_list) == NULL)
    {//链表是空的情况
        *XListSLinked_head_ptr(this_list) = NewListHead;//更新头节点
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
    else if (curNode == *XListSLinked_head_ptr(this_list))
    {//插入到链表头
        NewListTail->next = *XListSLinked_head_ptr(this_list);
        *XListSLinked_head_ptr(this_list) = NewListHead;
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
        *XListSLinked_head_ptr(this_list) = removeNode->next;
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
    XFree_System(removeNode);
    //更新数量
    --XContainerSize(this_list);
    --XContainerCapacity(this_list);
}

bool VXList_pop_front(XListSLinked* this_list)
{
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    XListSNode* head = *XListSLinked_head_ptr(this_list);
    if (head == NULL)
        return false;//链表是空的
    removeNode(this_list,NULL, head);
    return true;
}

bool VXList_pop_back(XListSLinked* this_list)
{
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    XListSNode* tail = this_list->m_tail;
    if (tail == NULL)
        return false;
    XListSNode* node = *XListSLinked_head_ptr(this_list);//当前节点
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
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
    {
        if (next)
            *next = XListSLinked_end(this_list);
        return;
    }
    XListSNode* node = it->node;
    XListSNode* prev = NULL;//前一个节点
    XListSNode* curNode = *XListSLinked_head_ptr(this_list);//当前节点
    while (curNode)
    {
        if (curNode == node)
        {//找到要删除的节点了
            XListSNode* nextNode = curNode->next;
            removeNode(this_list, prev, curNode);
            if (next)
                next->node = nextNode;
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
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return false;
    if (XContainerCompare(this_list) == NULL)
        return false;
    XListSNode* prev = NULL;//前一个节点
    XListSNode* curNode = *XListSLinked_head_ptr(this_list);//当前节点
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
    // 如果数据被共享，减少引用并置空
    if (XContainerSharedData(this_list) && XSharedData_isShared(XContainerSharedData(this_list)))
    {
        XSharedData_release(XContainerSharedData(this_list));
        XContainerSharedData(this_list) = NULL;
        this_list->m_tail = NULL;
        XContainerSize(this_list) = 0;
        XContainerCapacity(this_list) = 0;
        return;
    }
    XListSNode* prev = NULL;//前一个节点
    XListSNode* node = *XListSLinked_head_ptr(this_list);//当前节点
    while (node)
    {
        prev = node;
        node = node->next;
        if (XContainerDataDeinitMethod(this_list) != NULL)
            XContainerDataDeinitMethod(this_list)(&(prev->data));
        XFree_System(prev);
    }
    this_list->m_tail = NULL;
    *XListSLinked_head_ptr(this_list) = NULL;
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}

void* VXList_front(XListSLinked* this_list)
{
    if (*XListSLinked_head_ptr(this_list))
        return XListSNode_DataPtr(*XListSLinked_head_ptr(this_list));
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
    XListSNode* node = *XListSLinked_head_ptr(this_list);//当前节点
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

    void* pivot = XMalloc_System(typeSize);
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

            void* temp = XMalloc_System(typeSize);
            memcpy(temp, &(i->data), typeSize);
            memcpy(&(i->data), &(j->data), typeSize);
            memcpy(&(j->data), temp, typeSize);
            XFree_System(temp);
        }
        if (j == right) break;  // 到达右边界
        j = j->next;
    }

    // 将pivot放到正确位置
    void* temp = XMalloc_System(typeSize);
    memcpy(temp, &(i->data), typeSize);
    memcpy(&(i->data), &(left->data), typeSize);
    memcpy(&(left->data), temp, typeSize);
    XFree_System(temp);
    XFree_System(pivot);

    return i;  // 返回分区点
}
void VXList_sort(XListSLinked* this_list, XSortOrder order)
{
#if XStack_ON
    if (XListBase_isEmpty_base(this_list)|| XContainerCompare(this_list)==NULL)
        return;
    if (!ensureSharedData(this_list) || !VXListSLinkedDetachIfNeeded(this_list))
        return;
    //printf("进入排序\n");
    XListSNode* head = *XListSLinked_head_ptr(this_list);
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
    // 如果目标未初始化，先初始化（模式与源相同）
    if (((XClass*)object)->m_vtable == NULL)
    {
        XListSLinked_init(object, XContainerTypeSize(src), XContainerIsCow(src));
    }
    else
    {
        // 释放目标原有资源
        if (XContainerIsCow(object))
        {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXListSLinkedDataDelete, object);
        }
        else
        {
            XListSNode* head = (XListSNode*)XContainerDataPtr(object);
            while (head)
            {
                XListSNode* next = head->next;
                if (XContainerDataDeinitMethod(object))
                    XContainerDataDeinitMethod(object)(XListSNode_DataPtr(head));
                XFree_System(head);
                head = next;
            }
        }
    }

    // 复制所有成员（包括 m_tail、m_data 等）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XListSLinked) - sizeof(XClass));

    // 根据源模式调整引用计数或深拷贝
    if (XContainerIsCow(src))
    {
        if (XContainerSharedData(object))
            XSharedData_addRef(XContainerSharedData(object));
    }
    else
    {
        // 非 COW 模式：需要深拷贝链表数据（因为上面 memcpy 只是拷贝了指针）
        XListSNode* srcHead = (XListSNode*)XContainerDataPtr(src);
        XListSNode* newHead = NULL;
        XListSNode* newTail = NULL;
        XListSNode* srcNode = srcHead;
        size_t typeSize = XContainerTypeSize(src);

        while (srcNode)
        {
            XListSNode* newNode = XMalloc_System(ALIGN_UP(sizeof(XListSNode) + typeSize, sizeof(void*)));
            if (!newNode)
            {
                while (newHead)
                {
                    XListSNode* tmp = newHead;
                    newHead = newHead->next;
                    if (XContainerDataDeinitMethod(object))
                        XContainerDataDeinitMethod(object)(XListSNode_DataPtr(tmp));
                    XFree_System(tmp);
                }
                return;
            }
            if (XContainerDataCopyMethod(object))
                XContainerDataCopyMethod(object)(XListSNode_DataPtr(newNode), XListSNode_DataPtr(srcNode));
            else
                memcpy(XListSNode_DataPtr(newNode), XListSNode_DataPtr(srcNode), typeSize);
            newNode->next = NULL;
            if (!newHead)
                newHead = newTail = newNode;
            else
            {
                newTail->next = newNode;
                newTail = newNode;
            }
            srcNode = srcNode->next;
        }
        XContainerDataPtr(object) = newHead;
        object->m_tail = newTail;
        // 大小和容量已经在 memcpy 中复制，但那是源的值，仍然正确
    }
}

void VXClass_move(XListSLinked* object, XListSLinked* src)
{
    // 如果目标未初始化，先初始化（模式与源相同）
    if (((XClass*)object)->m_vtable == NULL)
    {
        bool useCow = XContainerIsCow(src);
        XListSLinked_init(object, XContainerTypeSize(src), useCow);
    }
    else
    {
        // 目标已初始化，释放其原有资源
        if (XContainerIsCow(object))
        {
            if (XContainerSharedData(object))
                XSharedData_release_with(XContainerSharedData(object), VXListSLinkedDataDelete, object);
        }
        else
        {
            XListSNode* head = (XListSNode*)XContainerDataPtr(object);
            while (head)
            {
                XListSNode* next = head->next;
                if (XContainerDataDeinitMethod(object))
                    XContainerDataDeinitMethod(object)(XListSNode_DataPtr(head));
                XFree_System(head);
                head = next;
            }
            XContainerDataPtr(object) = NULL;
            object->m_tail = NULL;
            XContainerSize(object) = 0;
            XContainerCapacity(object) = 0;
        }
    }

    // 转移所有权：直接将源的内存拷贝到目标（包括 m_data, m_tail, m_useCow 等）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XListSLinked) - sizeof(XClass));

    // 清空源对象资源（根据源模式）
    if (XContainerIsCow(src))
    {
        XContainerSharedData(src) = NULL;
    }
    else
    {
        XContainerDataPtr(src) = NULL;
        src->m_tail = NULL;
        XContainerSize(src) = 0;
        XContainerCapacity(src) = 0;
    }
}

void VXList_deinit(XListSLinked* this_list)
{
    if (XContainerIsCow(this_list)) {
        if (XContainerSharedData(this_list))
            XSharedData_release_with(XContainerSharedData(this_list), VXListSLinkedDataDelete, this_list);
    }
    else {
        XListSNode* head = (XListSNode*)XContainerDataPtr(this_list);
        while (head) {
            XListSNode* next = head->next;
            if (XContainerDataDeinitMethod(this_list))
                XContainerDataDeinitMethod(this_list)(XListSNode_DataPtr(head));
            XFree_System(head);
            head = next;
        }
        XContainerDataPtr(this_list) = NULL;
        this_list->m_tail = NULL;
    }
    XContainerSize(this_list) = 0;
    XContainerCapacity(this_list) = 0;
}

#endif