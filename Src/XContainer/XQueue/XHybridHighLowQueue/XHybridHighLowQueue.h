#include"XDataStructConfig.h"
#if !defined(XHYBRIDHIGHLOWQUEUE_H)&& XQueue_ON
#define XHYBRIDHIGHLOWQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XQueueBase.h"
#define XHYBRIDHIGHLOWQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)       //XQueue容器虚函数表大小
// 混合队列结构
typedef struct XHybridHighLowQueue
{
    XContainerObject m_parent;//
    XCompare m_comparePriority;//优先级数据比较大小方法
    size_t m_typeSizePriority;//优先级类型占用字节数
    //Priority
    //// 高频数据队列（原子环形队列，固定优先级）
    //XCircularQueueAtomic high_freq_queues[HIGH_FREQ_PRIORITY_COUNT];
    void* mapPriority;//映射优先级队列
    // 低频数据队列（优先队列，动态优先级）
    XPriorityQueue* low_freq_queue;
}XHybridHighLowQueue;
//初始化类
XVtable* XHybridHighLowQueue_class_init();
//队列初始化函数
void XHybridHighLowQueue_init(XHybridHighLowQueue* this_queue, size_t typeSize);
//队列创建函数
#define XHybridHighLowQueue_Create(Type) XHybridHighLowQueue_create(sizeof(Type))
XHybridHighLowQueue* XHybridHighLowQueue_create(size_t typeSize);
	//api
#define XHybridHighLowQueue_Push_Base				XQueueBase_Push_Base
#define XHybridHighLowQueue_push_base				XQueueBase_push_base
#define XHybridHighLowQueue_Push_Move_Base			XQueueBase_Push_Move_Base
#define XHybridHighLowQueue_push_move_base			XQueueBase_push_move_base
//出队
#define XHybridHighLowQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XHybridHighLowQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XHybridHighLowQueue_Top_Base					XQueueBase_Top_Base
#define XHybridHighLowQueue_top_base					XQueueBase_top_base
#define XHybridHighLowQueue_isFull_base				XQueueBase_isFull_base

#define XHybridHighLowQueue_copy_base				XQueueBase_copy_base	
#define XHybridHighLowQueue_move_base				XQueueBase_move_base	
#define XHybridHighLowQueue_deinit_base				XQueueBase_deinit_base	
#define XHybridHighLowQueue_delete_base				XQueueBase_delete_base	
#define XHybridHighLowQueue_clear_base				XQueueBase_clear_base	
#define XHybridHighLowQueue_isEmpty_base				XQueueBase_isEmpty_base	
#define XHybridHighLowQueue_size_base				XQueueBase_size_base	
#define XHybridHighLowQueue_capacity_base			XQueueBase_capacity_base
#define XHybridHighLowQueue_swap_base				XQueueBase_swap_base	
#define XHybridHighLowQueue_typeSize_base			XQueueBase_typeSize_base

#ifdef __cplusplus
}
#endif
#endif
