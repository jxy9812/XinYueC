#include"XDataStructTest.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<string.h>
#include<math.h>
#include"XCoreApplication.h"
#include <stdarg.h>
#include"XVarList.h"
#include <stdint.h>
void XSocketTest();
int main(int argc, char* args[])
{
	//fprintf(stderr, "[DEBUG] main() entered\n"); fflush(stderr);
	//setvbuf(stdout, NULL, _IONBF, 0);  /* 禁用输出缓冲，确保调试信息即时显示 */
	XPrintf("=== XinYueC 启动 ===\n");
	//XVectorTest();
	int n = 8,n1=666,sum=n+n1;
	char* str = "dadasdsad";
	XVarList* list=XVarList_Create(XVar(int,n), XVar(int, n1), XVar(char*, str));
	//XVarList_start(list);
	XVarList_args_3(list,int,a,int,b, char*,c);
	//printf("%d %d %s\n", a,b,c);
	
	XVarList_delete(list);

	XCoreApplication* app = XCoreApplication_create(argc,args);
	//XSocketTest();
	//XThreadTest();
	//XCoreApplication_setApplicationDescription
	//XAtomic_bool b;
	//XAtomic_init(b,false);
	//XAtomic_store_bool(&b,false);
	//printf("%d\n",XAtomic_load_bool(&b));
#if DEMOTEST
	
	//XStringVectorTest();
	//XStringTest();
	//XBase64Test();
	//return;
	//XTimerTimeWheelTest();
	//XListDLinkedIterator();
	//XHashMapTest();
	//XMapTest();
	//XLockFreeListTest();
	//XLockFreeListSwapTest();
	//XLockFreeListSortTest();
	//XLockFreeListIterator();
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
	//XStringListTest();
	//XStringTest();
	//XJsonArrayTest();
	//XJsonObjectTest();
	//return;
	//XStateMachineSignalTest();
	//XHistoryState_Test();
	return XMenuTest_run();
	//XStateMachineEventTest();
	return XCoreApplication_exec();
	//cJsonTest();
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