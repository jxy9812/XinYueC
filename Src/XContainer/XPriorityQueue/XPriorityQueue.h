#include"XDataStructConfig.h"
#if !defined(XPRIORITYQUEUE_H)&& XPriorityQueue_ON
#define XPRIORITYQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
//XPriorityQueue虚函数表
extern XVtable* XPriorityQueueVtable;
#define XPriorityQueue_VTABLE_SIZE (XVECTOR_VTABLE_SIZE+3)       //XVector容器虚函数表大小
//XPriorityQueue虚函数表枚举
enum XPriorityQueueEnum
{
	EXPriorityQueue_Push = EXVector_Sort + 1,
	EXPriorityQueue_Pop,
	EXPriorityQueue_Top,
};
//优先队列
typedef struct XPriorityQueue
{
	XVector m_vector;//基本数据
	XCompare m_compare;//比较准则
}XPriorityQueue;
//初始化类
void XPriorityQueue_class_init();
//初始化 队列
void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare);
//队列初始化函数
XPriorityQueue* XPriorityQueue_new(size_t typeSize, XCompare compare);
#define XPriorityQueue_New(Type,compare) XPriorityQueue_new(sizeof(Type),compare)
//释放队列
void XPriorityQueue_free(XPriorityQueue* this_queue);
//插入到队列的队尾
void XPriorityQueue_push(XPriorityQueue* this_queue, void* LpValue);
//出队
void XPriorityQueue_pop(XPriorityQueue* this_queue);
// 返回优先队列堆顶元素
void* XPriorityQueue_top(XPriorityQueue* this_queue);
#define XPriorityQueue_Top(queue,Type) (*(Type*)XPriorityQueue_top(queue))
//当队列为空时返回true，否则返回false
bool XPriorityQueue_isEmpty(XPriorityQueue* this_queue);
//返回队列中元素的个数
size_t XPriorityQueue_size(XPriorityQueue* this_queue);
//清空队列，释放内存
void XPriorityQueue_clear(XPriorityQueue* this_queue);

#ifdef __cplusplus
}
#endif
#endif