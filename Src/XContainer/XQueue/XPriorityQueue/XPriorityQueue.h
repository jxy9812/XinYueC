#include"XDataStructConfig.h"
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
	XCompare m_compare;//比较准则
}XPriorityQueue;
//初始化类
XVtable* XPriorityQueue_class_init();
//初始化 队列
void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare);
//队列初始化函数
XPriorityQueue* XPriorityQueue_create(size_t typeSize, XCompare compare);
#define XPriorityQueue_Create(Type,compare) XPriorityQueue_create(sizeof(Type),compare)
//api
#define XPriorityQueue_Push_Base				XQueueBase_Push_Base
#define XPriorityQueue_push_base				XQueueBase_push_base
//出队
#define XPriorityQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XPriorityQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XPriorityQueue_Top_Base					XQueueBase_Top_Base
#define XPriorityQueue_top_base					XQueueBase_top_base
#define XPriorityQueue_isFull_base				XQueueBase_isFull_base
//释放内存
#define XPriorityQueue_delete_base				XQueueBase_delete_base
//清空，不是释放内存
#define XPriorityQueue_clear_base				XQueueBase_clear_base
//检测是否为空，空为真 O(1)
#define XPriorityQueue_isEmpty_base				XQueueBase_isEmpty_base
//返回元素的个数 O(1)
#define XPriorityQueue_getSize_base				XQueueBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XPriorityQueue_getCapacity_base			XQueueBase_getCapacity_base
//交换两个同类型向量的数据
#define XPriorityQueue_swap_base				XQueueBase_swap_base
//返回元素类型字节大小
#define XPriorityQueue_getTypeSize_base			XQueueBase_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif