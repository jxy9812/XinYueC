#ifndef XPRIORITY_QUEUE_MACRO_H
#define XPRIORITY_QUEUE_MACRO_H
//优先队列初始化
#define XPriority_Queue_Init(Type,compare) XPriority_Queue_init(sizeof(Type),compare)
//优先队列插入数据
#define XPriority_Queue_Push(queue,value) XPriority_Queue_push(queue,&value)
//优先队列获取数据
#define XPriority_Queue_Top(queue,Type) (*(Type*)XPriority_Queue_top(queue))
#endif // !XPRIORITY_QUEUE_MACRO_H
