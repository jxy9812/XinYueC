#include"CXinYueConfig.h"
#if !defined(XCIRCULARQUEUEATOMIC_H)&& XCircularQueueAtomic_ON
#define XCIRCULARQUEUEATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XCircularQueue.h"
#include"XAtomic.h"
#define XCIRCULARQUEUEATOMIC_VTABLE_SIZE (XCIRCULARQUEUE_VTABLE_SIZE)       //XCircularQueueAtomic容器虚函数表大小
//环形队列
typedef struct XCircularQueueAtomic
{
	XVector m_vector;//基本数据
	XAtomic_size_t m_head;//队头索引
	XAtomic_size_t  m_tail;//队尾索引
}XCircularQueueAtomic;
//初始化类
XVtable* XCircularQueueAtomic_class_init();
//队列初始化函数
void XCircularQueueAtomic_init(XCircularQueueAtomic* this_queue,size_t typeSize, size_t count);
//队列创建函数
#define XCircularQueueAtomic_Create(Type,count) XCircularQueueAtomic_create(sizeof(Type),count)
XCircularQueueAtomic* XCircularQueueAtomic_create(size_t typeSize, size_t count);
//api
#define XCircularQueueAtomic_Push_Base					XCircularQueue_Push_Base
#define XCircularQueueAtomic_push_base					XCircularQueue_push_base
#define XCircularQueueAtomic_Push_Move_Base					XQueueBase_Push_Move_Base
#define XCircularQueueAtomic_push_move_base					XQueueBase_push_move_base

#define XCircularQueueAtomic_pop_base					XCircularQueue_pop_base
//接收数据并且出队	
#define XCircularQueueAtomic_receive_base				XCircularQueue_receive_base
// 返回队头元素
#define XCircularQueueAtomic_Top_Base					XCircularQueue_Top_Base
#define XCircularQueueAtomic_top_base					XCircularQueue_top_base
#define XCircularQueueAtomic_isFull_base				XCircularQueue_isFull_base

#define XCircularQueueAtomic_copy_base					XQueueBase_copy_base	
#define XCircularQueueAtomic_move_base					XQueueBase_move_base	
#define XCircularQueueAtomic_deinit_base				XQueueBase_deinit_base	
#define XCircularQueueAtomic_delete_base				XQueueBase_delete_base	
#define XCircularQueueAtomic_clear_base					XQueueBase_clear_base	
#define XCircularQueueAtomic_isEmpty_base				XQueueBase_isEmpty_base	
#define XCircularQueueAtomic_size_base				XQueueBase_size_base	
#define XCircularQueueAtomic_capacity_base			XQueueBase_capacity_base
#define XCircularQueueAtomic_swap_base					XQueueBase_swap_base	
#define XCircularQueueAtomic_typeSize_base			XQueueBase_typeSize_base
#ifdef __cplusplus
}
#endif
#endif