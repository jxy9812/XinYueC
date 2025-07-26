#include"XDataStructConfig.h"
#if !defined(XCIRCULARQUEUE_H)&& XCircularQueue_ON
#define XCIRCULARQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XQueueBase.h"
#include"XVector.h"
#define XCIRCULARQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)       //XCircularQueue容器虚函数表大小
//环形队列
typedef struct XCircularQueue
{
	XVector m_vector;//基本数据
	bool    m_autoExpansion;//自动扩容
	size_t  m_head;//队头索引
	size_t  m_tail;//队尾索引
}XCircularQueue;
//初始化类
XVtable* XCircularQueue_class_init();
//队列初始化函数
void XCircularQueue_init(XCircularQueue* this_queue,size_t typeSize, size_t count);
//队列创建函数
#define XCircularQueue_Create(Type,count) XCircularQueue_create(sizeof(Type),count)
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count);
//设置自动扩容
void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue,bool autoExpansion);
//api
#define XCircularQueue_Push_Base				XQueueBase_Push_Base
#define XCircularQueue_push_base				XQueueBase_push_base
#define XCircularQueue_Push_Move_Base			XQueueBase_Push_Move_Base
#define XCircularQueue_push_move_base			XQueueBase_push_move_base
//出队
#define XCircularQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XCircularQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XCircularQueue_Top_Base					XQueueBase_Top_Base
#define XCircularQueue_top_base					XQueueBase_top_base
#define XCircularQueue_isFull_base				XQueueBase_isFull_base

#define XCircularQueue_copy_base				XQueueBase_copy_base	
#define XCircularQueue_move_base				XQueueBase_move_base	
#define XCircularQueue_deinit_base				XQueueBase_deinit_base	
#define XCircularQueue_delete_base				XQueueBase_delete_base	
#define XCircularQueue_clear_base				XQueueBase_clear_base	
#define XCircularQueue_isEmpty_base				XQueueBase_isEmpty_base	
#define XCircularQueue_getSize_base				XQueueBase_getSize_base	
#define XCircularQueue_getCapacity_base			XQueueBase_getCapacity_base
#define XCircularQueue_swap_base				XQueueBase_swap_base	
#define XCircularQueue_getTypeSize_base			XQueueBase_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif