#include"XStack.h"
#if XStack_ON
#include<string.h>
#include<stdlib.h>
//初始化函数
XStack* XStack_new(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_stack = XMemory_malloc(sizeof(XVector));
	XStack_init(this_stack, typeSize);
	return this_stack;
}

void XStack_init(XStack* this_stack, size_t typeSize)
{
	if (ISNULL(this_stack, "") || ISNULL(typeSize, ""))
		return;
	XVector_init(this_stack, typeSize);
	XClassGetVtable(this_stack)= XStack_class_init();
}
#endif