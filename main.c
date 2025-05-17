#include"XDataStructTest.h"
#include"XVector_iterator.h"
#include"XClass.h"
#include<stdio.h>
#include<math.h>
int main(int argc, char* args[])
{
	//XModbusTest();
	XCylinderTest();
	return;
	XStringVectorTest();
	
#if DEMOTEST
	cJsonTest();
	cJsonXContainerTest();
	//return;
	ListTest();
	ListSortTest();
	ListIterator();
	ListSwapTest();
	XVectorTest();
	
	stackTest();
	queueTest();
	XPriority_QueueTest();
	XStringTest();
	XRedBlackTreeTest();
	XMapTest();
	return;
	

	//XMapAndXVectorFindTest();
	//XBinarySearchTest();
	SortTest();
	//XMazeGeneratedTest();
	//XMazePathfinding();
	//XBinaryTreeObjectTest();	
	//XBalancedBinaryTreeTest();
	
#endif
#ifdef _WIN32
	//XHuffmanTreeTest();
#else
	//XRedBlackTreeTest();
#endif // _Win32
	return 0;
}