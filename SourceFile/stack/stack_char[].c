#include"stack.h"
#include"stack_head.h"
#include<string.h>
//char[]数组字符串型入栈
void Stack_Push_charArray(STACK* st, const char* x)
{
	strcpy(StacketEnlargeCapacity(st), x);
}
//char[]数组字符串型取元素
char* Stack_top_charArray(STACK* st)
{
	return (char*)Stack_top(st);
}