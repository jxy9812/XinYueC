#include"Test.h"
#include"XVector_iterator.h"
#include<stdio.h>
#include<math.h>
void test()
{
	int x = 3, y = 4, z = 5;

	printf("%d\n", !(x + y) + z - 1 && y + z / 2);
}
int main(int argc, char* args[])
{
	//ListSortTest();
	//ListTest();
	//ListIterator();
	//VectorTest();
	//stackTest();
	//XStringTest();
	//XMapTest();
	//XMapAndXVectorFindTest();
	//XBinarySearchTest();
	//SortTest();
	//XMazeGeneratedTest();
	//XMazePathfinding();
	//queueTest();
	//XPriority_QueueTest();
	//XBinaryTreeObjectTest();	
	//XBalancedBinaryTreeTest();
	//XRedBlackTreeTest();
#ifdef _WIN32
	XHuffmanTreeTest();
#else
	XRedBlackTreeTest();
#endif // _Win32
	return 0;
}