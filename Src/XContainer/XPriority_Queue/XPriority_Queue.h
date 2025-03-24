#ifndef XPRIORITY_QUEUE
#define XPRIORITY_QUEUE
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
//XPriority_Queue虚函数表
extern XVtable* XPriority_QueueVtable;
//XPriority_Queue虚函数表枚举
enum XPriority_QueueEnum
{
	EXPriority_Queue_Push = EXVector_Sort + 1,
	EXPriority_Queue_Pop,
	EXPriority_Queue_Top,
};
//优先队列
typedef struct 
{
	XVector vector;//基本数据
	XCompare compare;//比较准则
}XPriority_Queue;
//初始化类
void XPriority_Queue_class_init();
//队列初始化函数
XPriority_Queue* XPriority_Queue_new(size_t typeSize, XCompare compare);
#define XPriority_Queue_New(Type,compare) XPriority_Queue_new(sizeof(Type),compare)
//初始化 队列
void XPriority_Queue_init(XPriority_Queue* this_queue, size_t typeSize, XCompare compare);
//释放队列
void XPriority_Queue_free(XPriority_Queue* this_queue);
//插入到队列的队尾
void XPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue);
//出队
void XPriority_Queue_pop(XPriority_Queue* this_queue);
// 返回优先队列堆顶元素
void* XPriority_Queue_top(XPriority_Queue* this_queue);
#define XPriority_Queue_Top(queue,Type) (*(Type*)XPriority_Queue_top(queue))
//当队列为空时返回true，否则返回false
bool XPriority_Queue_empty(XPriority_Queue* this_queue);
//返回队列中元素的个数
size_t XPriority_Queue_size(XPriority_Queue* this_queue);
//清空队列，释放内存
void XPriority_Queue_clear(XPriority_Queue* this_queue);

#ifdef __cplusplus
}
#endif
#endif