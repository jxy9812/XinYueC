#ifndef XSTACK_H
#define XSTACK_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
//XStack虚函数表
extern XVtable* XStackVtable;
//XStack虚函数表枚举
enum XStackEnum
{
	EXStack_Push = EXVector_Push_Back,
	EXStack_Pop= EXVector_Pop_Back,
	EXStack_Top= EXVector_Back,
};
typedef struct XStack
{
	XVector vector;
}XStack;
//初始化类
void XStack_class_init();
//创建一个stack容器并返回其指针
XStack* XStack_new(size_t typeSize);
#define XStack_New(Type) XStack_new(sizeof(Type))
void XStack_init(XStack* this_stack, size_t typeSize);
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


#ifdef __cplusplus
}
#endif
#endif// !STACK_H


