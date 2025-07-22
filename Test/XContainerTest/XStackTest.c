#include"XDataStructTest.h"
#if DEMOTEST
#include"XStack.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
//栈测试 XCoreApplication_requestQuit();
static void XStackTest();
void XStackTest()
{
#if XStack_ON
	printf("XStack 测试\n");
	XStack* s = XStack_Create(int);
	int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XStack_push_base(s, arr + i);
	}
	while (!XStack_isEmpty_base(s))
	{
		printf("%d\n",XStack_Top_Base(s,int));
		XStack_pop_base(s);
	}
	XStack_delete_base(s);
	XStack* string = XStack_Create(char[100]);
	char* strings[] = { "琦神","小白","皮皮","蛇蛇" };
	for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++)
	{
		char* str = strings[i];
		XStack_push_base(string, str);
	}
	while (!XStack_isEmpty_base(string))
	{
		printf("%s\n", XStack_top_base(string));
		XStack_pop_base(string);
	}
	XStack_delete_base(string);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
	XCoreApplication_requestQuit();
}
void XMenu_XStackTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XStack(栈)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStackTest);
	}
}
#endif