#ifndef QUEUE_HEAD_H
#define QUEUE_HEAD_H
typedef struct QUEUE
{
	void (*clear) (struct QUEUE*);//清空queue的队列，释放内存
	void(*push)(struct QUEUE*, void*);//插入到队列的队尾
	void (*pop)(struct QUEUE*);//删除queue的队头元素
	void* (*front)(struct QUEUE*);// 返回队列的队头元素指针，但不删除该元素
	void* (*back)(struct QUEUE*);// 返回队列的队尾元素指针，但不删除该元素
	bool (*empty)(struct QUEUE*);// 当队列为空时返回true，否则返回false
	int (*size)(struct QUEUE*);//返回队列中元素的个数
	void* _data;//指向自定义数组类型
	int  _current;//当前元素个数
	int _size;//元素最大个数
	int _type;//类型占用字节数
}QUEUE;
#endif // !QUEUE_HEAD_H

