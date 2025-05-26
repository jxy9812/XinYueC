#include"XDataStructTest.h"
#include"XVector_iterator.h"
#include"XClass.h"
#include<stdio.h>
#include<math.h>
#include"XAtomic.h"
int main(int argc, char* args[])
{
	XAtomic_int32_t b;
	XAtomic_init(b,100);
	XAtomic_fetch_sub_int32(&b,1);
	printf("%d\n",XAtomic_load_int32(&b));
#if DEMOTEST
	//XPWMDeviceTest();
	//XModbusTest();
	//XCylinderTest();	
	//XCircularQueueAtomicTest();
	//XSerialPortTest();
	stackTest();
	return;
	XStringVectorTest();
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