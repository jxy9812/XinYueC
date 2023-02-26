#ifndef XPRIORITY_QUEUE
#define XPRIORITY_QUEUE
#include"XContainerObject.h"
#include"XFunctionCallback.h"
//优先队列
typedef struct XPriority_Queue
{
	XContainerObject object;//基本数据
	XCompare compare;//比较准则
}XPriority_Queue;

//插入到队列的队尾
void XPriority_Queue_push(struct XPriority_Queue* this_queue, void* val);
//出队
void XPriority_Queue_pop(struct XPriority_Queue* this_queue);
// 返回优先队列堆顶元素
void* XPriority_Queue_top(struct XPriority_Queue* this_queue);
//当队列为空时返回true，否则返回false
bool XPriority_Queue_empty(struct XPriority_Queue* this_queue);
//返回队列中元素的个数
int XPriority_Queue_size(struct XPriority_Queue* this_queue);
//清空队列，释放内存
void XPriority_Queue_clear(struct XPriority_Queue* this_queue);
//释放队列
void XPriority_Queue_free(struct XPriority_Queue* this_queue);
//队列初始化函数
XPriority_Queue* XPriority_Queue_init(size_t TypeSize,XCompare compare);
#endif