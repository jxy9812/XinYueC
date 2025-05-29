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
//插入到队列的队尾
void XPriorityQueue_push_base(XPriorityQueue* this_queue, void* LpValue);
//出队
void XPriorityQueue_pop_base(XPriorityQueue* this_queue);
// 返回优先队列堆顶元素
void* XPriorityQueue_top_base(XPriorityQueue* this_queue);
#define XPriorityQueue_Top_Base(queue,Type) (*(Type*)XPriorityQueue_top_base(queue))
//释放内存
#define XPriorityQueue_free_base   XContainerObject_free_base
//清空，不是释放内存
#define XPriorityQueue_clear_base  XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XPriorityQueue_isEmpty_base			XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XPriorityQueue_size_base			XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XPriorityQueue_capacity_base		XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XPriorityQueue_swap_base			XContainerObject_swap_base
//返回元素类型字节大小
#define XPriorityQueue_getTypeSize_base		XContainerObject_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif