#include"XListDLinked.h"
#if XListDLinked_ON
#include"XStack.h"
#include<stdlib.h>
#include<string.h>
//插入
static XListDNode * VXList_push_front(XListDLinked * this_list, void* pvData);
static XListDNode* VXList_push_back(XListDLinked* this_list, void* pvData);
static bool VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* pvData);
static size_t VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* begin, size_t n);
//删除
static bool VXList_pop_front(XListDLinked* this_list);
static bool VXList_pop_back(XListDLinked* this_list);
static void VXList_erase(XListDLinked* this_list, const XListDLinked_iterator* it, XListDLinked_iterator* next);
static bool VXList_remove(XListDLinked* this_list, void* pvData);
static void VXList_clear(XListDLinked* this_list);
//遍历
static void* VXList_front(XListDLinked* this_list);
static void* VXList_back(XListDLinked* this_list);
static XListDNode* VXList_find(const XListDLinked* this_list, void* pvData);
//其他
static void VXList_sort(XListDLinked* this_list, XCompare compare);
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
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

    void* table[] = {
        //插入
        VXList_push_front,VXList_push_back,VXList_insert,VXList_insert_array,
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
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXList_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXList_clear);

#if SHOWCONTAINERSIZE
    printf("XListDLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

#define CreatNode(this_list)   (XMemory_malloc(sizeof(XListDNode*)*2+XContainerTypeSize(this_list)))

XListDNode* VXList_push_front(XListDLinked* this_list, void* pvData)
{
    if (ISNULL(this_list, ""))
        return NULL;
    XListBase* list = this_list;
    XListDNode* NewNode = XListDLinked_push_back_base(this_list, pvData);
    if (NewNode)
    {
        XContainerDataPtr(this_list) = NewNode;
    }
    return NewNode;
}

XListDNode* VXList_push_back(XListDLinked* this_list, void* pvData)
{
    /*if (ISNULL(this_list, ""))
        return NULL;*/
    XListBase* list = this_list;
    XListDNode* NewNode = CreatNode(this_list);//新节点
    if (NewNode == NULL)
    {
        perror("开辟节点失败");
        return NULL;
    }
    //NewNode->data = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
    memcpy(&(NewNode->data), pvData, XContainerTypeSize(this_list));//拷贝数据
    if (XListBase_isEmpty_base(this_list))
    {
        XContainerDataPtr(this_list) = NewNode;
        NewNode->next = NewNode;
        NewNode->prev = NewNode;
    }
    else
    {
        XListDNode* pfront = XContainerDataPtr(this_list);//原头节点
        XListDNode* pback = pfront->prev;//原尾节点
        NewNode->next = pfront;
        NewNode->prev = pback;
        pfront->prev = NewNode;
        pback->next = NewNode;
    }
    //更新记录数量
    ++XContainerSize(this_list);
    ++XContainerCapacity(this_list);
    return NewNode;
}

void VXList_inserts(XListDLinked* this_list, XListDNode* curNode, void* pvData, size_t n)
{
    XListBase* list = this_list;
    //if (ISNULL(this_list, ""))
    //    return;
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
            //newNode->data = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
            memcpy(&(newNode->data), pvData, XContainerTypeSize(this_list));//拷贝数据

            newNode->prev = left;
            newNode->next = curNode;
            left->next = newNode;
            curNode->prev = newNode;

            if (curNode == XContainerDataPtr(this_list))
            {
                XContainerDataPtr(this_list) = newNode;
            }
            ++XContainerSize(this_list);
            ++XContainerCapacity(this_list);
        }
        else
        {
            perror("插入的数找不到");
        }
    }
}

bool VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* pvData)
{
    /*if (ISNULL(this_list, ""))
        return;*/
    XListDLinked* list = this_list;
    if (curNode == NULL)
    {
        printf("节点指针不能为空\n");
        return false;
    }
    VXList_inserts(this_list, curNode, pvData, 1);
    return true;
}

size_t VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* array, size_t count)
{
    /*if (ISNULL(this_list, ""))
        return;*/
    XListBase* list = this_list;
    if (curNode == NULL)
    {//尾插
        for (size_t i = 0; i < count; i++)
        {
            XListBase_push_back_base(this_list, ((char*)array) + i * XContainerTypeSize(this_list));
        }
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            VXList_inserts(this_list, curNode, ((char*)array) + i * XContainerTypeSize(this_list), 1);
        }
    }
    return count;
}
//根据节点指针删除
static bool removeNode(XListDLinked* this_list, XListDNode* node)
{
    if (ISNULL(node, "") || XContainerObject_isEmpty_base(this_list))
        return false;
    XListDNode* nextNode = node->next;//下一个节点
    XListDNode* prevNode = node->prev;//上一个节点
    if (node->data)
    {
        if (XContainerDataDeinitMethod(this_list) != NULL)
            XContainerDataDeinitMethod(this_list)(&(node->data));
    }
    XMemory_free(node);//释放节点
    if (XContainerSize(this_list) == 1)
    {
        XContainerDataPtr(this_list) = NULL;
    }
    else
    {
        nextNode->prev = prevNode;
        prevNode->next = nextNode;
        if (XContainerDataPtr(this_list) == node)
            XContainerDataPtr(this_list) = nextNode;//重新设置头节点
    }
    --XContainerSize(this_list);
    --XContainerCapacity(this_list);
    return true;
}

//头删
bool VXList_pop_front(XListDLinked* this_list)
{
    if (XContainerObject_isEmpty_base(this_list))
        return false;
    removeNode(this_list, XContainerDataPtr(this_list));
    return true;
}
//尾删
bool VXList_pop_back(XListDLinked* this_list)
{
    if (XContainerObject_isEmpty_base(this_list))
        return false;
    removeNode(this_list, XContainerData(this_list, XListDNode*)->prev);
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
    //先获取下一个迭代器
    XListDLinked_iterator* back = XContainerData(this_list, XListDNode*)->prev;
    if (it->node == back)//如果是最后一个元素则返回空表示遍历完成了
    {
        if (next)
            *next = XListDLinked_end(this_list);
    }
    else if (next)
    {
        next->node = (XListDNode*)(it->node)->next;//指向下一个元素
    }
    //正式删除
    removeNode(this_list, it->node);
}

bool VXList_remove(XListDLinked* this_list, void* pvData)
{
    if (ISNULL(pvData, ""))
        return false;
    XListDLinked* list = this_list;
    XListDNode* node = XListDLinked_find_base(this_list, pvData);
    if (node==NULL)
        return false;
    removeNode(this_list,node);
    return true;
}

void VXList_clear(XListDLinked* this_list)
{
    if (XContainerObject_isEmpty_base(this_list))
        return;
    XListBase* list = this_list;
    XListDNode* p = list->m_parent.m_data;
    XListDNode* pnext = p->next;
    for (size_t i = 0; i < list->m_parent.m_size; i++)
    {
        if (XContainerDataDeinitMethod(this_list) != NULL)
            XContainerDataDeinitMethod(this_list)(&(p->data));
        pnext = p->next;
        //XMemory_free(p->data);
        XMemory_free(p);
        p = pnext;
    }
    list->m_parent.m_size = 0;
    list->m_parent.m_capacity = 0;
    list->m_parent.m_data = NULL;
}

void* VXList_front(XListDLinked* this_list)
{
    if (XContainerDataPtr(this_list))
        return XListDNode_DataPtr(XContainerDataPtr(this_list));
    return NULL;
}

void* VXList_back(XListDLinked* this_list)
{
    if (XContainerDataPtr(this_list))
        return XListDNode_DataPtr(((XListDNode*)XContainerDataPtr(this_list))->prev);
    return NULL;
}

XListDNode* VXList_find(const XListDLinked* this_list, void* pvData)
{
    XListBase* list = this_list;
    if (ISNULL(list->m_equality, "") || ISNULL(pvData, ""))
        return NULL;
    for_each_iterator(list, XListDLinked, it)
    {
        if (list->m_equality(XListDLinked_iterator_data(&it), pvData))
            return it.node;
    }
    return NULL;
}
//其他

void VXList_deinit(XListDLinked* this_list)
{
    XListDLinked_clear_base(this_list);
    //XMemory_free(this_list);
}
//排序
//一次快排
static struct XListDNode* List_OneSort(XListDNode* ListHead, XListDNode* ListTail, const size_t type, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{

    char* compareVal = XMemory_malloc(type);
    if (compareVal == NULL)
        return NULL;
    memcpy(compareVal, &(ListHead->data), type);
    while (ListHead != ListTail)
    {
        while (ListHead != ListTail)//右边开始往左边找
        {
            if (!Sort(&(ListTail->data), compareVal))
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
            if (Sort(&(ListHead->data), compareVal))
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
    XMemory_free(compareVal);
    //单次结束，分割节点
    return ListHead;

}

void VXList_sort(XListDLinked* this_list, XCompare compare)
{
#if XStack_ON
    if (ISNULL(this_list, "") || XListBase_isEmpty_base(this_list))
        return;
    XListBase* list = this_list;
    XListDNode* ListHead = XContainerDataPtr(this_list);//链表第一个节点
    XListDNode* ListTail = XContainerData(this_list, XListDNode*)->prev;//链表最后一个节点
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
        XListDNode* ListMiddle = List_OneSort(ListHead, ListTail, list->m_parent.m_typeSize, compare);
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