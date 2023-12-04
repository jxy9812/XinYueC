#include"Test.h"
#include"XVector_iterator.h"
#include<stdio.h>
#include<math.h>
int main(int argc, char* args[])
{
	//ListSortTest();
	ListTest();
	//ListIterator();
	//ListSwapTest();
	//VectorTest();
	//stackTest();
	//queueTest();
	//XPriority_QueueTest();
	//XStringTest();
	//XMapTest();
	//XMapAndXVectorFindTest();
	//XBinarySearchTest();
	//SortTest();
	//XMazeGeneratedTest();
	//XMazePathfinding();
	//XBinaryTreeObjectTest();	
	//XBalancedBinaryTreeTest();
	//XRedBlackTreeTest();
#ifdef _WIN32
	//XHuffmanTreeTest();
#else
	XRedBlackTreeTest();
#endif // _Win32
	return 0;
}