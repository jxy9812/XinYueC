#include"XDataStructConfig.h"
#if !defined(XCIRCULARQUEUE_H)&& XCircularQueue_ON
#define XCIRCULARQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
//XCircularQueue虚函数表
extern XVtable* XCircularQueueVtable;
#define XCIRCULARQUEUE_VTABLE_SIZE (XCONTAINEROBJECT_VTABLE_SIZE+5)       //XCircularQueue容器虚函数表大小
//XCircularQueue虚函数表枚举
enum XCircularQueueEnum
{
	EXCircularQueue_Push = XCONTAINEROBJECT_VTABLE_SIZE,
	EXCircularQueue_Pop,
	EXCircularQueue_Top,
	EXCircularQueue_Receive,
	EXCircularQueue_IsFull
};
//环形队列
typedef struct XCircularQueue
{
	XVector m_vector;//基本数据
	bool    m_autoExpansion;//自动扩容
	size_t  m_head;//队头索引
	size_t  m_tail;//队尾索引
}XCircularQueue;
//初始化类
void XCircularQueue_class_init();
//队列初始化函数
void XCircularQueue_init(XCircularQueue* this_queue,size_t typeSize, size_t count);
//队列创建函数
#define XCircularQueue_New(Type,count) XCircularQueue_new(sizeof(Type),count)
XCircularQueue* XCircularQueue_new(size_t typeSize, size_t count);
//设置自动扩容
void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue,bool autoExpansion);
//插入到队列的队尾
#define XCircularQueue_Push(this_queue,type,value){type t=value;XCircularQueue_push(this_vector,&t);}
bool XCircularQueue_push(XCircularQueue* this_queue, void* pvData);
//出队
void XCircularQueue_pop(XCircularQueue* this_queue);
//接收数据并且出队
bool XCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer);
// 返回队头元素
#define XCircularQueue_Top(this_queue,Type) (*(Type*)XCircularQueue_top(this_queue))
void* XCircularQueue_top(XCircularQueue* this_queue);
bool XCircularQueue_isFull(XCircularQueue* this_queue);
#define XCircularQueue_free		XContainerObject_free
#define XCircularQueue_isEmpty	XContainerObject_isEmpty
#define XCircularQueue_size		XContainerObject_size
#define XCircularQueue_clear	XContainerObject_clear
#ifdef __cplusplus
}
#endif
#endif