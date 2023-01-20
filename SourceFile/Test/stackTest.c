#include"Test.h"
#include"XStack.h"
stackTest()
{
	XStack* sInt = XStack_init("int");
	sInt->push(sInt, 1);
	sInt->push(sInt, 100);
	sInt->push(sInt, 65);
	sInt->push(sInt, 77);
	while (!sInt->empty(sInt))
	{
		printf("%d\n", sInt->top(sInt));
		sInt->pop(sInt);
	}
	XStack* string = XStack_init("char[100]");
	string->push(string, "琦神");
	string->push(string, "小白");
	string->push(string, "皮皮");
	string->push(string, "蛇蛇");
	while (!string->empty(string))
	{
		printf("%s\n", string->top(string));
		string->pop(string);
	}
}