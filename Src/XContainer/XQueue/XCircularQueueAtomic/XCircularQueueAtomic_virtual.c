#include "XCircularQueueAtomic.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>

static bool VXCircularQueueAtomic_isEmpty(const XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_isFull(const XCircularQueueAtomic* this_queue);
static void VXCircularQueueAtomic_clear(XCircularQueueAtomic* this_queue);//清空
static size_t VXCircularQueueAtomic_getSize(const XCircularQueueAtomic* this_queue);
//插入到队列的队尾
static bool VXCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* pvValue);
static bool VXCircularQueueAtomic_push_move(XCircularQueueAtomic* this_queue, void* pvValue);
//出队
static void VXCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue);
// 返回队头元素
static void* VXCircularQueueAtomic_top(XCircularQueueAtomic* this_queue);
static bool VXCircularQueueAtomic_receive(XCircularQueueAtomic* this_queue, void* pvBuffer);

static void VXClass_copy(XCircularQueueAtomic* object, const XCircularQueueAtomic* src);
static void VXClass_move(XCircularQueueAtomic* object, XCircularQueueAtomic* src);
static void VXClass_deinit(XCircularQueueAtomic* this_queue);

XVtable* XCircularQueueAtomic_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCIRCULARQUEUEATOMIC_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        //继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
    void* table[] = { VXCircularQueueAtomic_push,VXCircularQueueAtomic_push_move,VXCircularQueueAtomic_pop,VXCircularQueueAtomic_top,VXCircularQueueAtomic_receive,VXCircularQueueAtomic_isFull };
    //追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_IsEmpty, VXCircularQueueAtomic_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXCircularQueueAtomic_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Size, VXCircularQueueAtomic_getSize);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit)
#if SHOWCONTAINERSIZE
    printf("XCircularQueueAtomic size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
    return XVTABLE_DEFAULT;
}

bool VXCircularQueueAtomic_isEmpty(const XCircularQueueAtomic* this_queue)
{
    if (this_queue == NULL)
        return true;
    return (XAtomic_load_size_t(&(this_queue->m_head)) == XAtomic_load_size_t(&(this_queue->m_tail)));//头指针等于尾指针时为空
}

bool VXCircularQueueAtomic_isFull(const XCircularQueueAtomic* this_queue)
{
    if (this_queue == NULL)
        return false;
    size_t head = XAtomic_load_size_t(&(this_queue->m_head));
    size_t tail = XAtomic_load_size_t(&(this_queue->m_tail));
    return ((tail + 1) % XContainerSize(this_queue) == head);//尾指针下一个位置等于头指针时为满
}

void VXCircularQueueAtomic_clear(XCircularQueueAtomic* this_queue)
{
    if (this_queue == NULL)
        return;

    void* temp = XMemory_malloc(XContainerTypeSize(this_queue));
    if (temp == NULL) return;

    while (!VXCircularQueueAtomic_isEmpty(this_queue))
    {
        VXCircularQueueAtomic_receive(this_queue, temp);
    }

    XMemory_free(temp);
}

size_t VXCircularQueueAtomic_getSize(const XCircularQueueAtomic* this_queue)
{
    if (this_queue == NULL)
        return 0;
    size_t head = XAtomic_load_size_t(&(this_queue->m_head));
    size_t tail = XAtomic_load_size_t(&(this_queue->m_tail));
    return (tail >= head) ? (tail - head) : (XContainerSize(this_queue) - head + tail);
}

bool VXCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* pvValue)
{
    size_t tail, next_tail;

    // 循环尝试直到成功或队列满
    while (1) {
        tail = XAtomic_load_size_t(&(this_queue->m_tail));
        next_tail = (tail + 1) % XContainerSize(this_queue);

        // 检查队列是否已满（可能被其他生产者填满）
        if (next_tail == XAtomic_load_size_t(&(this_queue->m_head)))
            return false; // 队列已满

        // 使用CAS操作尝试更新队尾索引
        if (XAtomic_compare_exchange_strong_size_t(
            &(this_queue->m_tail), &tail, next_tail)) {
            break; // 成功获得写入权限
        }
        // 否则，表示其他线程已更新m_tail，重试
    }

    // 安全写入数据
    if (XContainerDataCopyMethod(this_queue))
    {
        memset(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), 0, XContainerTypeSize(this_queue));
        XContainerDataCopyMethod(this_queue)(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), pvValue);
    }
    else
    {
        memcpy(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), pvValue, XContainerTypeSize(this_queue));
    }
    return true;
}

bool VXCircularQueueAtomic_push_move(XCircularQueueAtomic* this_queue, void* pvValue)
{
    size_t tail, next_tail;

    // 循环尝试直到成功或队列满
    while (1) {
        tail = XAtomic_load_size_t(&(this_queue->m_tail));
        next_tail = (tail + 1) % XContainerSize(this_queue);

        // 检查队列是否已满（可能被其他生产者填满）
        if (next_tail == XAtomic_load_size_t(&(this_queue->m_head)))
            return false; // 队列已满

        // 使用CAS操作尝试更新队尾索引
        if (XAtomic_compare_exchange_strong_size_t(
            &(this_queue->m_tail), &tail, next_tail)) {
            break; // 成功获得写入权限
        }
        // 否则，表示其他线程已更新m_tail，重试
    }

    // 安全写入数据
    if (XContainerDataMoveMethod(this_queue))
    {
        memset(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), 0, XContainerTypeSize(this_queue));
        XContainerDataMoveMethod(this_queue)(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), pvValue);
    }
    else
    {
        memcpy(((char*)XContainerDataPtr(this_queue)) + tail * XContainerTypeSize(this_queue), pvValue, XContainerTypeSize(this_queue));
    }
    return true;
}

void VXCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue)
{
    if (VXCircularQueueAtomic_isEmpty(this_queue))
        return;

    void* temp = XMemory_malloc(XContainerTypeSize(this_queue));
    if (temp == NULL) return;

    VXCircularQueueAtomic_receive(this_queue, temp);
    XMemory_free(temp);
}

void* VXCircularQueueAtomic_top(XCircularQueueAtomic* this_queue)
{
    if (VXCircularQueueAtomic_isEmpty(this_queue))
        return NULL;

    size_t head = XAtomic_load_size_t(&(this_queue->m_head));
    return ((char*)XContainerDataPtr(this_queue)) + (head * XContainerTypeSize(this_queue));
}

bool VXCircularQueueAtomic_receive(XCircularQueueAtomic* this_queue, void* pvBuffer)
{
    size_t head, next_head;

    // 循环尝试直到成功或队列为空
    while (1) {
        head = XAtomic_load_size_t(&(this_queue->m_head));

        // 检查队列是否为空（可能被其他消费者取空）
        if (head == XAtomic_load_size_t(&(this_queue->m_tail)))
            return false; // 队列为空

        next_head = (head + 1) % XContainerSize(this_queue);

        // 使用CAS操作尝试更新队头索引
        if (XAtomic_compare_exchange_strong_size_t(
            &(this_queue->m_head), &head, next_head)) {
            break; // 成功获得读取权限
        }
        // 否则，表示其他线程已更新m_head，重试
    }

    // 安全读取数据
    void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (head * XContainerTypeSize(this_queue));
    memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));

    // 如果有删除方法，则调用它
    if (XContainerDataDeinitMethod(this_queue) != NULL) {
        // 创建一个临时副本用于删除
        void* temp = XMemory_malloc(XContainerTypeSize(this_queue));
        if (temp != NULL) {
            memcpy(temp, pvTop, XContainerTypeSize(this_queue));
            XContainerDataDeinitMethod(this_queue)(temp);
            XMemory_free(temp);
        }
    }

    return true;
}
void VXClass_copy(XCircularQueueAtomic* object, const XCircularQueueAtomic* src)
{
    XVtableGetFunc(XVector_class_init(), EXClass_Copy, void(*)(XVector*, const XVector*))(object, src);
    object->m_head = src->m_head;
    object->m_tail = src->m_tail;
}
void VXClass_move(XCircularQueueAtomic* object, XCircularQueueAtomic* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XCircularQueueAtomic_init(object, XContainerTypeSize(src), XContainerCapacity(src)-1);
    }
    else if (!XCircularQueueAtomic_isEmpty_base(object))
    {
        XCircularQueueAtomic_clear_base(object);
    }
    XSwap(object, src, sizeof(XCircularQueueAtomic));
}
void VXClass_deinit(XCircularQueueAtomic* this_queue)
{
    XVtableGetFunc(XVector_class_init(), EXClass_Deinit, void(*)(XVector*))(this_queue);
    this_queue->m_head.value = 0;
    this_queue->m_tail.value = 0;
}
#endif