#include"XDataStructTest.h"
#include"XVector_iterator.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<math.h>
#include"XCoreApplication.h"
int main(int argc, char* args[])
{
	XCoreApplication* app = XCoreApplication_create(argc,args);

	//XAtomic_bool b;
	//XAtomic_init(b,false);
	//XAtomic_store_bool(&b,false);
	//printf("%d\n",XAtomic_load_bool(&b));
#if DEMOTEST
	//XVectorTest();
	//XStringVectorTest();
	//XStringTest();
	//XBase64Test();
	//return;
	//XTimerWheelTest();
	//XListDLinkedIterator();
	//XHashMapTest();
	//XMapTest();
	//XListSLinkedAtomicTest();
	//XListSLinkedAtomicSwapTest();
	//XListSLinkedAtomicSortTest();
	//XListSLinkedAtomicIterator();
	//XListDLinkedTest();
	//
	//XListDLinkedSortTest();
	//XListDLinkedIterator();
	//XListDLinkedSwapTest();
	//XListSLinkedTest();
	//XListSLinkedSwapTest();
	//XListSLinkedIterator();
	//XListSLinkedSortTest();
	//XPriority_QueueTest();
	//XPWMDeviceTest();
	//XVectorTest();
	//TJCHMICommTest();
	//XDataFrameCommTest();
	//XSocketTest();
	//XHashSetTest();
	//XSetTest();
	//XBinaryTreeTest();
	//XRedBlackTreeTest();
	//XMapTest();
	//return;
	//XVariantListTest();
	XMenuTest_run();
	//XModbusTest();
	//XCylinderTest();	
	//XCircularQueueAtomicTest();
	//XSerialPortTest();
	//XStackTest();
	//XStringTest();
	//XVectorTest();
	//return;
	return XCoreApplication_exec();
	cJsonTest();
	cJsonXContainerTest();
	XRedBlackTreeTest();
	//XMapAndXVectorFindTest();
	//XBinarySearchTest();
	SortTest();
	//XMazeGeneratedTest();
	//XMazePathfinding();
	//XBalancedBinaryTreeTest();
	
#endif
#ifdef _WIN32
	//XHuffmanTreeTest();
#else
	//XRedBlackTreeTest();
#endif // _Win32
	return XCoreApplication_exec();
}