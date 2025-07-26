#include"XDataStructConfig.h"
#if !defined(XSTACK_H)&& XStack_ON
#define XSTACK_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
//XStack虚函数表枚举
enum XStackEnum
{
	EXStack_Push_Copy = EXVector_Push_Back_Copy,
	EXStack_Push_Move = EXVector_Push_Back_Move,
	EXStack_Pop= EXVector_Pop_Back,
	EXStack_Top= EXVector_Back,
};
typedef struct XStack
{
	XVector vector;
}XStack;
//初始化类
XVtable* XStack_class_init();
//创建一个stack容器并返回其指针
XStack* XStack_create(size_t typeSize);
#define XStack_Create(Type) XStack_create(sizeof(Type))
void XStack_init(XStack* this_stack, size_t typeSize);
// 压栈，增加元素 O(1)
#define XStack_push_base			XVector_push_back_base
#define XStack_Push_Base			XVector_Push_Back_Base
#define XStack_push_move_base		XVector_push_back_move_base
//#define XStack_Push_Base			XVector_Push_Back_Base
//移除栈顶元素 O(1)
#define XStack_pop_base				XVector_pop_back_base
// 取得栈顶元素（但不删除）O(1)
#define XStack_top_base				XVector_back_base
#define XStack_Top_Base				 XVector_Back_Base
//将this_stackTwo拷贝到this_stackOne
#define XStack_copy_base			XVector_copy_base
//将this_stackTwo逆序拷贝到this_stackOne
#define XStack_rcopy_base			XVector_rcopy_base
#define XStack_copy_base			XVector_copy_base	
#define XStack_move_base			XVector_move_base	
#define XStack_deinit_base			XVector_deinit_base	
#define XStack_delete_base			XVector_delete_base	
#define XStack_clear_base			XVector_clear_base	
#define XStack_isEmpty_base			XVector_isEmpty_base	
#define XStack_getSize_base			XVector_getSize_base	
#define XStack_getCapacity_base		XVector_getCapacity_base
#define XStack_swap_base			XVector_swap_base	
#define XStack_getTypeSize_base		XVector_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif// !STACK_H


