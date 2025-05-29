#include"XDataStructTest.h"
#if DEMOTEST
#include"XStack.h"
void stackTest()
{
#if XStack_ON
	printf("XStack 测试\n");
	XStack* s = XStack_New(int);
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
	XStack_free_base(s);
	XStack* string = XStack_New(char[100]);
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
	XStack_free_base(string);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

#endif