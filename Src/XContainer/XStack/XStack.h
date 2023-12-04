#ifndef XSTACK_H
#define XSTACK_H
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
typedef struct XStack
{
	XVector vector;
}XStack;
//创建一个stack容器并返回其指针
XStack* XStack_init(size_t TypeSize);
#define XStack_Init(Type) XStack_init(sizeof(Type))
//释放栈
void XStack_free(XStack* this_stack);
// 压栈，增加元素 O(1)
void XStack_push(XStack* this_stack,void* LpValue);
//移除栈顶元素 O(1)
void XStack_pop(XStack* this_stack);
// 取得栈顶元素（但不删除）O(1)
void* XStack_top(XStack* this_stack);
#define XStack_Top(stack,type) (*((type*)XStack_top(stack)))
//清空stack的队列，释放内存
void XStack_clear(XStack* this_stack);
//检测栈内是否为空，空为真 O(1)
bool XStack_empty(XStack* this_stack);
//返回stack内元素的个数 O(1)
int XStack_size(XStack* this_stack);
//返回当前stack所能容纳的最大元素值
int XStack_capacity(XStack* this_stack);
//交换两个栈
void XStack_swap(XStack* this_stackOne, XStack* this_stackTwo);

//将this_stackTwo拷贝到this_stackOne
void XStack_copy(XStack* this_stackOne, const XStack* this_stackTwo);
//将this_stackTwo逆序拷贝到this_stackOne
void XStack_rcopy(XStack* this_stackOne, const XStack* this_stackTwo);
//返回元素类型字节大小
size_t XStack_typeSize(XStack* this_stack);


#endif // !STACK_H


