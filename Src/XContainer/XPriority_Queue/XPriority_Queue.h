#ifndef XPRIORITY_QUEUE
#define XPRIORITY_QUEUE
#include"XContainerObject.h"
#include"XFunctionCallback.h"
#include"XPriority_Queue_macro.h"
//优先队列
typedef struct XPriority_Queue
{
	XContainerObject object;//基本数据
	XCompare compare;//比较准则
}XPriority_Queue;

//插入到队列的队尾
void XPriority_Queue_push(XPriority_Queue* this_queue, void* LpValue);
//出队
void XPriority_Queue_pop(XPriority_Queue* this_queue);
// 返回优先队列堆顶元素
void* XPriority_Queue_top(XPriority_Queue* this_queue);
//当队列为空时返回true，否则返回false
bool XPriority_Queue_empty(XPriority_Queue* this_queue);
//返回队列中元素的个数
size_t XPriority_Queue_size(XPriority_Queue* this_queue);
//清空队列，释放内存
void XPriority_Queue_clear(XPriority_Queue* this_queue);
//释放队列
void XPriority_Queue_free(XPriority_Queue* this_queue);
//队列初始化函数
XPriority_Queue* XPriority_Queue_init(size_t TypeSize,XCompare compare);
#endif