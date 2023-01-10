#ifndef STACK_H
#define STACK_H
#include<stdio.h>
#include<stdbool.h>
#define MAXNUM 4//初始数组大小
typedef struct stack
{
	void (*clear) (struct stack* st);//清空stack的队列，释放内存
	void(*push)(struct stack* st, void* x);//压栈，增加元素 O(1)
	void (*pop)(struct stack* st);//移除栈顶元素 O(1)
	int* (*top)(struct stack* st);// 取得栈顶元素（但不删除）O(1)
	bool (*empty)(struct stack* st);// 检测栈内是否为空，空为真 O(1)
	int (*size)(struct stack* st);//返回stack内元素的个数 O(1)
	int (*capacity)(struct stack* st);//返回当前stack所能容纳的最大元素值
	void (*copy)(struct stack* st1, const struct stack* st2);//将st2拷贝到st1
	void (*rcopy)(struct stack* st1, const struct stack* st2);//将st2逆序拷贝到st1
	void (*swap)(struct stack* st1, struct stack* st2);//交换两个栈
}stack;
void Stack_clear(stack* st);//清空stack的队列，释放内存
void Stack_Push(stack* st, const void* x);// 压栈，增加元素 O(1)
void Stack_Push_char(stack* st, const char x);
void Stack_Push_Char(stack* st, const char* x);
void Stack_Push_charArray(stack* st, const char* x);
void Stack_Push_int(stack* st, const int x);
void Stack_Push_Int(stack* st, const int* x);
void Stack_pop(stack* st);//移除栈顶元素 O(1)
void* Stack_top(stack* st);// 取得栈顶元素（但不删除）O(1)
char Stack_top_char(stack* st);
char* Stack_top_Char(stack* st);
char* Stack_top_charArray(stack* st);
int Stack_top_int(stack* st);
int* Stack_top_Int(stack* st);
bool Stack_empty(stack* st);//检测栈内是否为空，空为真 O(1)
int Stack_size(stack* st);//返回stack内元素的个数 O(1)
int Stack_Capacity(stack* st);//返回当前stack所能容纳的最大元素值
void Stack_Copy(stack* st1, const stack* st2);//将st2拷贝到st1
void Stack_Rcopy(stack* st1, const stack* st2);//将st2逆序拷贝到st1
void Stack_Swap(stack* st1, stack* st2);//交换两个栈
//创建一个stack容器并返回其指针
stack* NewStack(char* arr, ...);

#endif // !STACK_H


