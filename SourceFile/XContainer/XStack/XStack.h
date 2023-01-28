#ifndef STACK_H
#define STACK_H
#include<stdio.h>
#include<stdbool.h>
#define MAXNUM 4//初始数组大小
typedef struct XStack
{
	void (*clear) (struct XStack* this_stack);//清空stack的队列，释放内存
	void(*push)(struct XStack* this_stack, void* val);//压栈，增加元素 O(1)
	void (*pop)(struct XStack* this_stack);//移除栈顶元素 O(1)
	int* (*top)(struct XStack* this_stack);// 取得栈顶元素（但不删除）O(1)
	bool (*empty)(struct XStack* this_stack);// 检测栈内是否为空，空为真 O(1)
	int (*size)(struct XStack* this_stack);//返回stack内元素的个数 O(1)
	int (*capacity)(struct XStack* this_stack);//返回当前stack所能容纳的最大元素值
	void (*copy)(struct XStack* this_stackOne, const struct XStack* this_stackTwo);//将st2拷贝到st1
	void (*rcopy)(struct XStack* this_stackOne, const struct XStack* this_stackTwo);//将st2逆序拷贝到st1
	void (*swap)(struct XStack* this_stackOne, struct XStack* this_stackTwo);//交换两个栈
	//释放
	void (*free)(struct XStack* this_stack);//释放内存
}XStack;
//清空stack的队列，释放内存
void XStack_clear(struct XStack* this_stack);
// 压栈，增加元素 O(1)
void XStack_Push(struct XStack* this_stack, const void* val);
void XStack_Push_char(struct XStack* this_stack, const char val);
void XStack_Push_Char(struct XStack* this_stack, const char* val);
void XStack_Push_charArray(struct XStack* this_stack, const char* val);
void XStack_Push_int(struct XStack* this_stack, const int val);
void XStack_Push_Int(struct XStack* this_stack, const int* val);
//移除栈顶元素 O(1)
void XStack_pop(struct XStack* this_stack);
// 取得栈顶元素（但不删除）O(1)
void* XStack_top(struct XStack* this_stack);
char XStack_top_char(struct XStack* this_stack);
char* XStack_top_Char(struct XStack* this_stack);
char* XStack_top_charArray(struct XStack* this_stack);
int XStack_top_int(struct XStack* this_stack);
int* XStack_top_Int(struct XStack* this_stack);
//返回元素类型字节大小
size_t XStack_TypeSize(struct XStack* this_stack);
//检测栈内是否为空，空为真 O(1)
bool XStack_empty(struct XStack* this_stack);
//返回stack内元素的个数 O(1)
int XStack_size(struct XStack* this_stack);
//返回当前stack所能容纳的最大元素值
int XStack_Capacity(struct XStack* this_stack);
//将this_stackTwo拷贝到this_stackOne
void XStack_Copy(struct XStack* this_stackOne, const struct XStack* this_stackTwo);
//将this_stackTwo逆序拷贝到this_stackOne
void XStack_Rcopy(struct XStack* this_stackOne, const struct XStack* this_stackTwo);
//交换两个栈
void XStack_Swap(struct XStack* this_stackOne, struct XStack* this_stackTwo);
//释放栈
void XStack_free(XStack* this_stack);
//创建一个stack容器并返回其指针
XStack* XStack_init(const char* arr, ...);
#endif // !STACK_H


