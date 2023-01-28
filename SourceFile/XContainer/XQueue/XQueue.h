#ifndef QUEUE_H
#define QUEUE_H
#include<stdio.h>
#include<stdbool.h>
#define ARRAY 0  //数组
#define LIST  1  //链表
#define PATT LIST//实现方式

typedef struct XQueue
{
	void (*clear) (struct XQueue*);//清空queue的队列，释放内存
	void(*push)(struct XQueue*, void*);//插入到队列的队尾
	void (*pop)(struct XQueue*);//删除queue的队头元素
	void* (*front)(struct XQueue*);// 返回队列的队头元素指针，但不删除该元素
	void* (*back)(struct XQueue*);// 返回队列的队尾元素指针，但不删除该元素
	bool (*empty)(struct XQueue*);// 当队列为空时返回true，否则返回false
	int (*size)(struct XQueue*);//返回队列中元素的个数
	//释放
	void (*free)(struct XQueue*);//释放内存
}XQueue;
//清空queue的队列，释放内存
void XQueue_clear(struct XQueue* this_queue);
//插入到队列的队尾
void XQueue_Push(struct XQueue* this_queue, void* val);
//删除queue的队头元素
void XQueue_pop(struct XQueue* this_queue);
// 返回队列的队头元素指针，但不删除该元素
void* XQueue_front(struct XQueue* this_queue);
// 返回队列的队尾元素指针，但不删除该元素
void* XQueue_back(struct XQueue* this_queue);
//当队列为空时返回true，否则返回false
bool XQueue_empty(struct XQueue* this_queue);
//返回队列中元素的个数
int XQueue_size(struct XQueue* this_queue);
//释放队列
void XQueue_free(struct XQueue* this_queue);
//queue容器初始化函数
XQueue* XQueue_init(int sizeType);
#endif 

