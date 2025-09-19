#include"CXinYueConfig.h"
#if !defined(XPRIORITYQUEUE_H)&& XPriorityQueue_ON
#define XPRIORITYQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XQueueBase.h"
#include"XVector.h"
#define XPRIORITYQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)       //XPriorityQueue容器虚函数表大小
//优先队列
typedef struct XPriorityQueue
{
	XVector m_vector;//基本数据
	XSortOrder m_order;//
}XPriorityQueue;
//初始化类
XVtable* XPriorityQueue_class_init();
//初始化 队列
void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare, XSortOrder order);
//队列初始化函数
XPriorityQueue* XPriorityQueue_create(size_t typeSize, XCompare compare, XSortOrder order);
#define XPriorityQueue_Create(Type,compare,order) XPriorityQueue_create(sizeof(Type),compare,order)
//api
#define XPriorityQueue_Push_Base				XQueueBase_Push_Base
#define XPriorityQueue_push_base				XQueueBase_push_base
#define XPriorityQueue_Push_Move_Base			XQueueBase_Push_Move_Base
#define XPriorityQueue_push_move_base			XQueueBase_push_move_base
//出队
#define XPriorityQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XPriorityQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XPriorityQueue_Top_Base					XQueueBase_Top_Base
#define XPriorityQueue_top_base					XQueueBase_top_base
#define XPriorityQueue_isFull_base				XQueueBase_isFull_base

#define XPriorityQueue_copy_base				XQueueBase_copy_base	
#define XPriorityQueue_move_base				XQueueBase_move_base	
#define XPriorityQueue_deinit_base				XQueueBase_deinit_base	
#define XPriorityQueue_delete_base				XQueueBase_delete_base	
#define XPriorityQueue_clear_base				XQueueBase_clear_base	
#define XPriorityQueue_isEmpty_base				XQueueBase_isEmpty_base	
#define XPriorityQueue_size_base				XQueueBase_size_base	
#define XPriorityQueue_capacity_base			XQueueBase_capacity_base
#define XPriorityQueue_swap_base				XQueueBase_swap_base	
#define XPriorityQueue_typeSize_base			XQueueBase_typeSize_base

#ifdef __cplusplus
}
#endif
#endif