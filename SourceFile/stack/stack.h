#ifndef STACK_H
#define STACK_H
#include<stdio.h>
#include<stdbool.h>
#define MAXNUM 4//初始数组大小
typedef struct stack
{
	void (*clear) (struct stack* this_stack);//清空stack的队列，释放内存
	void(*push)(struct stack* this_stack, void* val);//压栈，增加元素 O(1)
	void (*pop)(struct stack* this_stack);//移除栈顶元素 O(1)
	int* (*top)(struct stack* this_stack);// 取得栈顶元素（但不删除）O(1)
	bool (*empty)(struct stack* this_stack);// 检测栈内是否为空，空为真 O(1)
	int (*size)(struct stack* this_stack);//返回stack内元素的个数 O(1)
	int (*capacity)(struct stack* this_stack);//返回当前stack所能容纳的最大元素值
	void (*copy)(struct stack* this_stackOne, const struct stack* this_stackTwo);//将st2拷贝到st1
	void (*rcopy)(struct stack* this_stackOne, const struct stack* this_stackTwo);//将st2逆序拷贝到st1
	void (*swap)(struct stack* this_stackOne, struct stack* this_stackTwo);//交换两个栈
	//释放
	void (*free)(struct stack* this_stack);//释放内存
}stack;
//清空stack的队列，释放内存
void Stack_clear(struct stack* this_stack);
// 压栈，增加元素 O(1)
void Stack_Push(struct stack* this_stack, const void* val);
void Stack_Push_char(struct stack* this_stack, const char val);
void Stack_Push_Char(struct stack* this_stack, const char* val);
void Stack_Push_charArray(struct stack* this_stack, const char* val);
void Stack_Push_int(struct stack* this_stack, const int val);
void Stack_Push_Int(struct stack* this_stack, const int* val);
//移除栈顶元素 O(1)
void Stack_pop(struct stack* this_stack);
// 取得栈顶元素（但不删除）O(1)
void* Stack_top(struct stack* this_stack);
char Stack_top_char(struct stack* this_stack);
char* Stack_top_Char(struct stack* this_stack);
char* Stack_top_charArray(struct stack* this_stack);
int Stack_top_int(struct stack* this_stack);
int* Stack_top_Int(struct stack* this_stack);
//检测栈内是否为空，空为真 O(1)
bool Stack_empty(struct stack* this_stack);
//返回stack内元素的个数 O(1)
int Stack_size(struct stack* this_stack);
//返回当前stack所能容纳的最大元素值
int Stack_Capacity(struct stack* this_stack);
//将this_stackTwo拷贝到this_stackOne
void Stack_Copy(struct stack* this_stackOne, const struct stack* this_stackTwo);
//将this_stackTwo逆序拷贝到this_stackOne
void Stack_Rcopy(struct stack* this_stackOne, const struct stack* this_stackTwo);
//交换两个栈
void Stack_Swap(struct stack* this_stackOne, struct stack* this_stackTwo);
//释放栈
void Stact_free(stack* this_stack);
//创建一个stack容器并返回其指针
stack* Stack_init(const char* arr, ...);
#endif // !STACK_H


