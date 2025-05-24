#include"XDataStructConfig.h"
#if !defined(XCIRCULARQUEUEATOMIC_H)&& XCircularQueueAtomic_ON
#define XCIRCULARQUEUEATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
#include"XAtomic.h"
//XCircularQueueAtomic虚函数表
extern XVtable* XCircularQueueAtomicVtable;
#define XCIRCULARQUEUEATOMIC_VTABLE_SIZE (XVECTOR_VTABLE_SIZE+3)       //XCircularQueueAtomic容器虚函数表大小
//XCircularQueue虚函数表枚举
enum XCircularQueueAtomicEnum
{
	EXCircularQueueAtomic_Push = EXVector_Sort + 1,
	EXCircularQueueAtomic_Pop,
	EXCircularQueueAtomic_Top,
};
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
#define XCircularQueueAtomic_free		XVector_free
//插入到队列的队尾
#define XCircularQueueAtomic_Push(this_queue,type,value){type t=value;XCircularQueueAtomic_push(this_vector,&t);}
bool XCircularQueueAtomic_push(XCircularQueueAtomic* this_queue, void* LpValue);
//出队
void XCircularQueueAtomic_pop(XCircularQueueAtomic* this_queue);
// 返回优先队列堆顶元素
#define XCircularQueueAtomic_Top(this_queue,Type) (*(Type*)XCircularQueueAtomic_top(this_queue))
void* XCircularQueueAtomic_top(XCircularQueueAtomic* this_queue);
#define XCircularQueueAtomic_isEmpty	XVector_isEmpty
#define XCircularQueueAtomic_size		XVector_size
#define XCircularQueueAtomic_clear	XVector_clear
#ifdef __cplusplus
}
#endif
#endif