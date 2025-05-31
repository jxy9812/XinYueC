#include"XDataStructConfig.h"
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
#define XCircularQueueAtomic_New(Type,count) XCircularQueueAtomic_new(sizeof(Type),count)
XCircularQueueAtomic* XCircularQueueAtomic_new(size_t typeSize, size_t count);
//api
#define XCircularQueueAtomic_Push_Base					XCircularQueue_Push_Base
#define XCircularQueueAtomic_push_base					XCircularQueue_push_base
#define XCircularQueueAtomic_pop_base					XCircularQueue_pop_base
//接收数据并且出队	
#define XCircularQueueAtomic_receive_base				XCircularQueue_receive_base
// 返回队头元素
#define XCircularQueueAtomic_Top_Base					XCircularQueue_Top_Base
#define XCircularQueueAtomic_top_base					XCircularQueue_top_base
#define XCircularQueueAtomic_isFull_base				XCircularQueue_isFull_base
//释放内存
#define XCircularQueueAtomic_free_base					XCircularQueue_free_base
//清空，不是释放内存
#define XCircularQueueAtomic_clear_base					XCircularQueue_clear_base
//检测是否为空，空为真 O(1)
#define XCircularQueueAtomic_isEmpty_base				XCircularQueue_isEmpty_base
//返回元素的个数 O(1)
#define XCircularQueueAtomic_getSize_base				XCircularQueue_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XCircularQueueAtomic_getCapacity_base			XCircularQueue_getCapacity_base
//交换两个同类型向量的数据
#define XCircularQueueAtomic_swap_base					XCircularQueue_swap_base
//返回元素类型字节大小
#define XCircularQueueAtomic_getTypeSize_base			XCircularQueue_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif