#include"XDataStructConfig.h"
#if !defined(XPriorityMapQueue_H)&& XQueue_ON
#define XPriorityMapQueue_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XQueueBase.h"
#define XPRIORITYMAPQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)       //XQueue容器虚函数表大小
// 优先级映射队列结构
typedef struct XPriorityMapQueue
{
    XContainerObject m_parent;//
    //XCompare m_comparePriority;//优先级数据比较大小方法
    //size_t m_typeSizePriority;//优先级类型占用字节数
    //Priority
    //// 高频数据队列（原子环形队列，固定优先级）
    //XCircularQueueAtomic high_freq_queues[HIGH_FREQ_PRIORITY_COUNT];
    void* mapPriority;//映射优先级队列
    // 低频数据队列（优先队列，动态优先级）
    XPriorityQueue* low_freq_queue;
}XPriorityMapQueue;
//初始化类
XVtable* XPriorityMapQueue_class_init();
//队列初始化函数
void XPriorityMapQueue_init(XPriorityMapQueue* this_queue, size_t prioritySize,XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize);
//队列创建函数
#define XPriorityMapQueue_Create(Type) XPriorityMapQueue_create(sizeof(Type))
XPriorityMapQueue* XPriorityMapQueue_create(size_t prioritySize, XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize);
void XPriorityMapQueue_addFifoQueue(XPriorityMapQueue* this_queue, void* priority, size_t queueSize);
void XPriorityMapQueue_removeFifoQueue(XPriorityMapQueue* this_queue, void* priority);
//api
#define XPriorityMapQueue_Push_Base				XQueueBase_Push_Base
#define XPriorityMapQueue_push_base				XQueueBase_push_base
#define XPriorityMapQueue_Push_Move_Base			XQueueBase_Push_Move_Base
#define XPriorityMapQueue_push_move_base			XQueueBase_push_move_base
//出队
#define XPriorityMapQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XPriorityMapQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XPriorityMapQueue_Top_Base					XQueueBase_Top_Base
#define XPriorityMapQueue_top_base					XQueueBase_top_base
#define XPriorityMapQueue_isFull_base				XQueueBase_isFull_base

#define XPriorityMapQueue_copy_base				XQueueBase_copy_base	
#define XPriorityMapQueue_move_base				XQueueBase_move_base	
#define XPriorityMapQueue_deinit_base				XQueueBase_deinit_base	
#define XPriorityMapQueue_delete_base				XQueueBase_delete_base	
#define XPriorityMapQueue_clear_base				XQueueBase_clear_base	
#define XPriorityMapQueue_isEmpty_base				XQueueBase_isEmpty_base	
#define XPriorityMapQueue_size_base				XQueueBase_size_base	
#define XPriorityMapQueue_capacity_base			XQueueBase_capacity_base
#define XPriorityMapQueue_swap_base				XQueueBase_swap_base	
#define XPriorityMapQueue_typeSize_base			XQueueBase_typeSize_base

#ifdef __cplusplus
}
#endif
#endif
