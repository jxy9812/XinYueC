#ifndef QUEUE_H
#define QUEUE_H
#include<stdio.h>
#include<stdbool.h>
#define ARRAY 0  //数组
#define LIST  1  //链表
#define PATT LIST//实现方式

typedef struct queue
{
	void (*clear) (struct queue*);//清空queue的队列，释放内存
	void(*push)(struct queue*, void*);//插入到队列的队尾
	void (*pop)(struct queue*);//删除queue的队头元素
	void* (*front)(struct queue*);// 返回队列的队头元素指针，但不删除该元素
	void* (*back)(struct queue*);// 返回队列的队尾元素指针，但不删除该元素
	bool (*empty)(struct queue*);// 当队列为空时返回true，否则返回false
	int (*size)(struct queue*);//返回队列中元素的个数
}queue;
void Queue_clear(struct queue* que);//清空queue的队列，释放内存
void Queue_Push(queue* que, void* x);//插入到队列的队尾
void Queue_pop(struct queue* que);//删除queue的队头元素
void* Queue_front(struct queue* que);// 返回队列的队头元素指针，但不删除该元素
void* Queue_back(struct queue* que);// 返回队列的队尾元素指针，但不删除该元素
bool Queue_empty(struct queue* que);//当队列为空时返回true，否则返回false
int Queue_size(struct queue* que);//返回队列中元素的个数
//queue容器初始化函数
queue* NewQueue(int sizeType);
#endif 

