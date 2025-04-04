#ifndef XDATASTRUCTTEST_H
#define XDATASTRUCTTEST_H
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
	//排序算法测试
	void SortTest();
	//随机迷宫生成算法——深度优先算法
	void XMazeGeneratedTest();
	//深度寻路算法
	void XMazePathfinding();
	//二叉树基类测试
	void XBinaryTreeObjectTest();
	//平衡二叉树测试
	void XBalancedBinaryTreeTest();
	//红黑树测试
	void XRedBlackTreeTest();
	//XMap和XVector的查询性能测试;
	void XMapAndXVectorFindTest();
	//二分查找测试
	void XBinarySearchTest();
	//哈夫曼树测试
	void XHuffmanTreeTest();
	void cJsonTest();
	void cJsonXContainerTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif