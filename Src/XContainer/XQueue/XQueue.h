#include"XDataStructConfig.h"
#if !defined(XQUEUE_H)&& XQueue_ON
#define XQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XList.h"
#define XQUEUE_VTABLE_SIZE (XLIST_VTABLE_SIZE)       //XQueue容器虚函数表大小
//XQueue虚函数表枚举
enum XQueueEnum
{
	EXQueue_Push = EXListBase_Push_Back,
	EXQueue_Pop= EXListBase_Pop_Front,
	EXQueue_Top = EXListBase_Front,
};
typedef struct XQueue
{
	XList m_list;
}XQueue;
//初始化类
XVtable* XQueue_class_init();
//queue容器初始化函数
XQueue* XQueue_new(size_t typeSize);
#define XQueue_New(Type) XQueue_new(sizeof(Type))
void XQueue_init(XQueue* this_queue, size_t typeSize);
//插入到队列的队尾
void XQueue_push_base(XQueue* this_queue, void* LpValue);
#define XQueue_Push_Base(this_queue,type,value) {type t=value;XQueue_push_base(this_queue,&t);}
//删除queue的队头元素
void XQueue_pop_base(XQueue* this_queue);
// 返回队列的队头元素指针，但不删除该元素
void* XQueue_front_base(XQueue* this_queue);
#define XQueue_Front_Base(queue,Type) (*(Type*)XQueue_front_base(queue))
// 返回队列的队尾元素指针，但不删除该元素
void* XQueue_back_base(XQueue* this_queue);
#define XQueue_Back_Base(queue,Type) (*(Type*)XQueue_back_base(queue))
// 取得队头元素（但不删除）O(1)
void* XQueue_top_base(XQueue* this_queue);
#define XQueue_Top_Base(queue,type) (*((type*)XQueue_top_base(queue)))
//释放内存
#define XQueue_free_base   XContainerObject_free_base
//清空，不是释放内存
#define XQueue_clear_base  XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XQueue_isEmpty_base			XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XQueue_getSize_base			XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XQueue_getCapacity_base		XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XQueue_swap_base			XContainerObject_swap_base
//返回元素类型字节大小
#define XQueue_getTypeSize_base		XContainerObject_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif
