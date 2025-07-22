#ifndef XCONTAINERTEST_H
#define XCONTAINERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataStructConfig.h"
#include"XClass.h"
#if DEMOTEST
	//容器主菜单
	void XMenu_XContainerTest(XMenu* root);
	void XMenu_XListTest(XMenu* root);
	//双向循环链表菜单
	void XMenu_XListDLinkedTest(XMenu* root);
	//单向链表菜单
	void XMenu_XListSLinkedTest(XMenu* root);
	//数组
	void XMenu_XVectorTest(XMenu* root);
	//栈
	void XMenu_XStackTest(XMenu* root);
	//栈测试
	void stackTest();
	//优先队列
	void XPriority_QueueTest();
	//循环队列测试
	void XCircularQueueTest();
	void XCircularQueueAtomicTest();
	//队列测试
	void queueTest();

	//字符串数组测试
	void XStringListTest();
	//字符串测试
	void XStringTest();
	//map映射测试
	void  XMapTest();
	void XHashMapTest();
	
	void XListSLinkedAtomicTest();
	void XListSLinkedAtomicSwapTest();
	void XListSLinkedAtomicSortTest();
	void XListSLinkedAtomicIterator();
	void XHashSetTest();
	void XSetTest();
	void XVariantListTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif