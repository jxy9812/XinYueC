#ifndef XCONTAINERTEST_H
#define XCONTAINERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataStructConfig.h"
#include"XClass.h"
#if DEMOTEST

	//链表迭代器测试
	void ListIterator();
	//链表内置快排测试
	void ListSortTest();
	//链表一般项测试
	void ListTest();
	//双链表交换测试
	void ListSwapTest();
	//栈测试
	void stackTest();
	//优先队列
	void XPriority_QueueTest();
	//循环队列测试
	void XCircularQueueTest();
	//队列测试
	void queueTest();
	//动态数组测试
	void XVectorTest();
	//字符串数组测试
	void XStringVectorTest();
	//字符串测试
	void XStringTest();
	//map映射测试
	void  XMapTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif