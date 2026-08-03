#include"XDataStructTest.h"
#include"XMenuTest.h"
#include<stdio.h>
#include<string.h>
#include<math.h>
#include"XCoreApplication.h"
#include"XCommandLineParser.h"
#include"XCommandLineOption.h"
#include <stdarg.h>
#include"XVarList.h"
#include <stdint.h>
#include <stdlib.h>
#include "XDeviceTest.h"
void XSocketTest();
int main(int argc, char* args[])
{
	//fprintf(stderr, "[DEBUG] main() entered\n"); fflush(stderr);
	setvbuf(stdout, NULL, _IONBF, 0);  /* 禁用输出缓冲，确保调试信息即时显示 */
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
	
	/* ==================== 命令行参数解析 ==================== */
	XCommandLineParser* cmdParser = XCommandLineParser_create();
	XCommandLineParser_setApplicationDescription(cmdParser, "XinYueC 测试平台");
	
	XCommandLineOption* optTest = XCommandLineOption_createFull("t", 
		"直接运行指定测试（菜单路径，如 2-9-1）", "test-path", NULL);
	XCommandLineOption_addName(optTest, "test");
	XCommandLineParser_addOption(cmdParser, optTest);
	/* 解析器复制选项内容后不接管传入对象，原始对象由调用方释放。 */
	XCommandLineOption_delete(optTest);
	
	XCommandLineOption* optList = XCommandLineOption_createFull("l",
		"列出所有测试菜单", NULL, NULL);
	XCommandLineOption_addName(optList, "list");
	XCommandLineParser_addOption(cmdParser, optList);
	XCommandLineOption_delete(optList);
	
	XCommandLineParser_addHelpOption(cmdParser);
	
	if (argc > 1) {
		/* 将命令行参数转换为 XStringList */
		XStringList* argsList = XStringList_create();
		for (int i = 0; i < argc; ++i) {
			XStringList_push_back_utf8(argsList, args[i]);
		}
		
		if (!XCommandLineParser_parse(cmdParser, argsList)) {
			fprintf(stderr, "错误: %s\n", XCommandLineParser_errorText(cmdParser));
			fprintf(stderr, "使用 --help 查看帮助信息。\n");
			XCommandLineParser_delete(cmdParser);
			XStringList_delete_base(argsList);
			XCoreApplication_delete_base(app);
			return 1;
		}
		
		if (XCommandLineParser_isSet(cmdParser, "help") || XCommandLineParser_isSet(cmdParser, "h")) {
			XString* help = XCommandLineParser_helpText(cmdParser);
			printf("%s\n", XString_toUtf8(help));
			XString_delete_base(help);
			XCommandLineParser_delete(cmdParser);
			XStringList_delete_base(argsList);
			XCoreApplication_delete_base(app);
			return 0;
		}
		if (XCommandLineParser_isSet(cmdParser, "list") || XCommandLineParser_isSet(cmdParser, "l")) {
			printf("可用测试菜单:\n");
			printf("  2-9-1  XCoreApplication Qt 对齐测试\n");
			printf("  2-10-1 XCommandLineParser Qt 对齐测试\n");
			printf("  esp8266-unit XATComm / ESP8266 模拟 API 回归测试\n");
			printf("  esp8266-auto ESP8266 全 API 串口与 TCP 联调测试\n");
			XCommandLineParser_delete(cmdParser);
			XStringList_delete_base(argsList);
			XCoreApplication_delete_base(app);
			return 0;
		}
		if (XCommandLineParser_isSet(cmdParser, "test") || XCommandLineParser_isSet(cmdParser, "t")) {
			const char* testPath = XCommandLineParser_value(cmdParser, "test");
			if (testPath && strcmp(testPath, "esp8266-auto") == 0) {
				const char* port = getenv("XESP8266_PORT");
				const char* ssid = getenv("XESP8266_SSID");
				const char* password = getenv("XESP8266_PASSWORD");
				int result = XESP8266WifiTest_runAutomated(port, ssid, password);
				XCommandLineParser_delete(cmdParser);
				XStringList_delete_base(argsList);
				XCoreApplication_delete_base(app);
				return result;
			}
			if (testPath && strcmp(testPath, "esp8266-unit") == 0) {
				int result = XESP8266WifiTest_runUnit();
				XCommandLineParser_delete(cmdParser);
				XStringList_delete_base(argsList);
				XCoreApplication_delete_base(app);
				return result;
			}
			printf("指定测试路径: %s\n", testPath ? testPath : "(无)");
		}
		XStringList_delete_base(argsList);
	}
	XCommandLineParser_delete(cmdParser);
	
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
	int result = XMenuTest_run();
	XCoreApplication_delete_base(app);
	return result;
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
