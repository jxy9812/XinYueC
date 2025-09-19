#ifndef XDATASTRUCTTEST_H
#define XDATASTRUCTTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
//#include"XContainerTest/XContainerTest.h"
#include"XContainerTest.h"
#include"XProtocolStackTest.h"
#include"XIOTest.h"
#include"XCodeTest.h"
#include"XTimerTest.h"
#include"XLibraryTest.h"
#if DEMOTEST
	//排序算法测试
	void SortTest();
	//随机迷宫生成算法——深度优先算法
	void XMazeGeneratedTest();
	//深度寻路算法
	void XMazePathfinding();
	//二叉树基类测试
	void XBinaryTreeTest();
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

#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif