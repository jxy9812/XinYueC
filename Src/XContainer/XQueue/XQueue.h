#include"XDataStructConfig.h"
#if !defined(XQUEUE_H)&& XQueue_ON
#define XQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XList.h"
//XQueue虚函数表
extern XVtable* XQueueVtable;
//XQueue虚函数表枚举
enum XQueueEnum
{
	EXQueue_Push = EXList_Push_Back,
	EXQueue_Pop= EXList_Pop_Front,
	EXQueue_Top = EXList_Front,
};
typedef struct XQueue
{
	XList list;
}XQueue;
//初始化类
void XQueue_class_init();
//queue容器初始化函数
XQueue* XQueue_new(size_t typeSize);
#define XQueue_New(Type) XQueue_new(sizeof(Type))
void XQueue_init(XQueue* this_queue, size_t typeSize);
//释放队列
void XQueue_free(XQueue* this_queue);
//清空queue的队列，释放内存
void XQueue_clear(XQueue* this_queue);
//插入到队列的队尾
void XQueue_push(XQueue* this_queue, void* LpValue);
//删除queue的队头元素
void XQueue_pop(XQueue* this_queue);
// 返回队列的队头元素指针，但不删除该元素
void* XQueue_front(XQueue* this_queue);
#define XQueue_Front(queue,Type) (*(Type*)XQueue_front(queue))
// 返回队列的队尾元素指针，但不删除该元素
void* XQueue_back(XQueue* this_queue);
#define XQueue_Back(queue,Type) (*(Type*)XQueue_back(queue))
// 取得队头元素（但不删除）O(1)
void* XQueue_top(XQueue* this_queue);
#define XQueue_Top(queue,type) (*((type*)XQueue_top(queue)))
//当队列为空时返回true，否则返回false
bool XQueue_empty(XQueue* this_queue);
//返回队列中元素的个数
size_t XQueue_size(XQueue* this_queue);

#ifdef __cplusplus
}
#endif
#endif
