#include"XStack.h"
#if XStack_ON
#include<string.h>
#include<stdlib.h>
//初始化函数
XStack* XStack_create(size_t typeSize)
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
XVtable* XStack_class_init()
{
	XVTABLE_CREAT_DEFAULT
		XVTABLE_DEFAULT = XVector_class_init();
	//继承的函数
	//XVtable_append_vtable(XStackVtable, XVectorVtable);
#if SHOWCONTAINERSIZE
	printf("XStack size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
#endif