#include"XDataStructConfig.h"
#if !defined(XSTACK_H)&& XStack_ON
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
// 压栈，增加元素 O(1)
#define XStack_push			XVector_push_back
#define XStack_Push			XVector_Push_Back
//移除栈顶元素 O(1)
#define XStack_pop			XVector_pop_back
// 取得栈顶元素（但不删除）O(1)
#define XStack_top          XVector_back
#define XStack_Top          XVector_Back
//将this_stackTwo拷贝到this_stackOne
#define XStack_copy			XVector_copy
//将this_stackTwo逆序拷贝到this_stackOne
#define XStack_rcopy		XVector_rcopy
#define XStack_free			XVector_free_base	
#define XStack_clear		XVector_clear_base	
#define XStack_isEmpty		XVector_isEmpty_base	
#define XStack_size			XVector_size_base	
#define XStack_capacity		XVector_capacity_base
#define XStack_swap			XVector_swap_base	
#define XStack_typeSize		XVector_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif// !STACK_H


