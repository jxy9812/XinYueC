#include"XDataStructConfig.h"
#if !defined(XCIRCULARQUEUEATOMIC_H)&& XCircularQueueAtomic_ON
#define XCIRCULARQUEUEATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XCircularQueue.h"
#include"XAtomic.h"
//XCircularQueueAtomic虚函数表
extern XVtable* XCircularQueueAtomicVtable;
#define XCIRCULARQUEUEATOMIC_VTABLE_SIZE (XCIRCULARQUEUE_VTABLE_SIZE)       //XCircularQueueAtomic容器虚函数表大小
//环形队列
typedef struct XCircularQueueAtomic
{
	XVector m_vector;//基本数据
	XAtomic_size_t m_head;//队头索引
	XAtomic_size_t  m_tail;//队尾索引
}XCircularQueueAtomic;
//初始化类
void XCircularQueueAtomic_class_init();
//队列初始化函数
void XCircularQueueAtomic_init(XCircularQueueAtomic* this_queue,size_t typeSize, size_t count);
//队列创建函数
#define XCircularQueueAtomic_New(Type,count) XCircularQueueAtomic_new(sizeof(Type),count)
XCircularQueueAtomic* XCircularQueueAtomic_new(size_t typeSize, size_t count);
#define XCircularQueueAtomic_free_base		XContainerObject_free_base
//插入到队列的队尾
#define XCircularQueueAtomic_Push_Base(this_queue,type,value){type t=value;XCircularQueueAtomic_push_base(this_vector,&t);}
bool XCircularQueueAtomic_push_base(XCircularQueueAtomic* this_queue, void* pvData);
//出队
void XCircularQueueAtomic_pop_base(XCircularQueueAtomic* this_queue);
//接收数据并且出队
bool XCircularQueueAtomic_receive_base(XCircularQueueAtomic* this_queue, void* pvBuffer);
// 返回队头元素
#define XCircularQueueAtomic_Top_Base(this_queue,Type) (*(Type*)XCircularQueueAtomic_top_base(this_queue))
void* XCircularQueueAtomic_top_base(XCircularQueueAtomic* this_queue);
bool XCircularQueueAtomic_isFull_base(XCircularQueueAtomic* this_queue);
#define XCircularQueueAtomic_isEmpty_base	XContainerObject_isEmpty_base
#define XCircularQueueAtomic_getSize_base		XContainerObject_getSize_base
#define XCircularQueueAtomic_clear_base		XContainerObject_clear_base
#ifdef __cplusplus
}
#endif
#endif