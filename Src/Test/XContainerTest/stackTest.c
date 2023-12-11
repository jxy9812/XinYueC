#include"Test.h"
#include"XStack.h"
void stackTest()
{
	printf("XStack 测试\n");
	XStack* s = XStack_New(int);
	int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };

	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XStack_push(s, arr + i);
	}
	while (!XStack_empty(s))
	{
		printf("%d\n",XStack_Top(s,int));
		XStack_pop(s);
	}
	XStack_free(s);
	XStack* string = XStack_New(char[100]);
	char* strings[] = { "琦神","小白","皮皮","蛇蛇" };
	for (size_t i = 0; i < sizeof(strings) / sizeof(strings[0]); i++)
	{
		char* str = strings[i];
		XStack_push(string, str);
	}
	while (!XStack_empty(string))
	{
		printf("%s\n", XStack_top(string));
		XStack_pop(string);
	}
	XStack_free(string);
}