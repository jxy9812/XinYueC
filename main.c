#include"XDataStructTest.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<math.h>
#include"XCoreApplication.h"
#include <stdarg.h>
#include"XVarList.h"

int main(int argc, char* args[])
{
	int n = 8,n1=666;
	XVarList* list=XVarList_Create(XVar(int,n));
	XVarList_start(list);
	n = XVarList_arg(list,int);
	printf("%d\n",n);
	n = XVarList_arg(list, int);
	printf("%d\n", n);
	XVarList_delete(list);

	XCoreApplication* app = XCoreApplication_create(argc,args);
	// 设置应用描述
	XCoreApplication_setApplicationDescription(app, "示例命令行工具");

	// 添加普通选项
	XCoreApplication_addCommandLineOption(app, "c", "config", "配置文件路径", true, false, "config.ini");

	// 创建选项组
	XCommandLineOptionGroup* logGroup = XCommandLineOptionGroup_create("log", "日志选项", false);
	XCommandLineOption logLevelOpt = {
		.shortName = "l",
		.longName = "log-level",
		.description = "日志级别",
		.defaultValue = "info",
		.requiresValue = true,
		.isHidden = false
	};
	XCommandLineOptionGroup_addOption(logGroup, &logLevelOpt);
	XCoreApplication_addOptionGroup(app, logGroup);

	// 解析命令行
	XCoreApplication_parseCommandLine(app);

	// 检查帮助选项
	if (XCoreApplication_hasOption(app, "help")) {
		XCoreApplication_printHelpAndExit(app, NULL);
	}
	//XCoreApplication_setApplicationDescription
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
	//XTimerTimeWheelTest();
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