#include"XDataStructConfig.h"
#if !defined(XCIRCULARQUEUE_H)&& XCircularQueue_ON
#define XCIRCULARQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
//XCircularQueue虚函数表
extern XVtable* XCircularQueueVtable;
#define XCIRCULARQUEUE_VTABLE_SIZE (XVECTOR_VTABLE_SIZE+3)       //XCircularQueue容器虚函数表大小
//XCircularQueue虚函数表枚举
enum XCircularQueueEnum
{
	EXCircularQueue_Push = EXVector_Sort + 1,
	EXCircularQueue_Pop,
	EXCircularQueue_Top,
};
//环形队列
typedef struct XCircularQueue
{
	XVector m_vector;//基本数据
	size_t  m_head;//队头索引
	size_t  m_tail;//队尾索引
}XCircularQueue;
//初始化类
void XCircularQueue_class_init();
//队列初始化函数
void XCircularQueue_init(XCircularQueue* this_queue,size_t typeSize, size_t count);
//队列初始化函数
XCircularQueue* XCircularQueue_new(size_t typeSize, size_t count);
#define XCircularQueue_free		XVector_free
//插入到队列的队尾
void XCircularQueue_push(XCircularQueue* this_queue, void* LpValue);
//出队
void XCircularQueue_pop(XCircularQueue* this_queue);
// 返回优先队列堆顶元素
void* XCircularQueue_top(XCircularQueue* this_queue);
#define XCircularQueue_isEmpty	XVector_isEmpty
#define XCircularQueue_size		XVector_size
#define XCircularQueue_clear	XVector_clear
#ifdef __cplusplus
}
#endif
#endif