#include"XDataStructTest.h"
#include"XVector_iterator.h"
#include"XClass.h"
#include<stdio.h>
#include<math.h>
#include"XAtomic.h"
int main(int argc, char* args[])
{
	XAtomic_bool b;
	XAtomic_init(b,false);
	//XAtomic_store_bool(&b,false);
	printf("%d\n",XAtomic_load_bool(&b));
#if DEMOTEST
	//XTimerWheelTest();
	//XListDLinkedIterator();
	//XHashMapTest();
	//XMapTest();
	XListDLinkedTest();
	//XListDLinkedSortTest();
	XListDLinkedIterator();
	XListDLinkedSwapTest();
	/*XListSLinkedTest();
	XListSLinkedSwapTest();
	XListSLinkedIterator();
	XListSLinkedSortTest();*/
	//XPriority_QueueTest();
	//XPWMDeviceTest();
	//XModbusTest();
	//XCylinderTest();	
	//XCircularQueueAtomicTest();
	//XSerialPortTest();
	//stackTest();
	//XStringTest();
	//XVectorTest();
	return;
	XStringVectorTest();
	cJsonTest();
	cJsonXContainerTest();
	//return;
	XListDLinkedTest();
	XListDLinkedSortTest();
	XListDLinkedIterator();
	XListDLinkedSwapTest();
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