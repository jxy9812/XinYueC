#include"stack.h"
#include"stack_head.h"
#include<stdlib.h>
#include<string.h>
static bool isStackNULL(const struct stack* Object, const char* str)
{
	if (Object == NULL)
	{
		perror("%s成员函数调用的对象为NULL", str);
		return true;
	}
	return false;
}
void Stack_clear(stack* this_stack)
{
	STACK* stack=(STACK*)this_stack;
	if (isStackNULL(this_stack, "Stack_clear"))
		return;
	if (stack->object._data != NULL)
	{
		free(stack->object._data);
		stack->object._data = NULL;
		stack->object._capacity = 0;
		stack->object._size = 0;
	}

}
//判断是否为空
bool Stack_empty(stack* this_stack)//检测栈内是否为空，空为真 O(1)
{
	STACK* stack=(STACK*)this_stack;
	if (isStackNULL(this_stack, "Stack_empty"))
		return true;
	return ContainerObject_empty(&stack->object);
}
//当前最大能存储数据量
int Stack_size(stack* this_stack)//返回stack内元素的个数 O(1)
{
	STACK* stack=(STACK*)this_stack;
	if (isStackNULL(this_stack, "Stack_size"))
		return 0;
	return ContainerObject_size(&stack->object);
}
//返回当前stack所能容纳的最大元素值
int Stack_Capacity(stack* this_stack)
{
	STACK* stack=(STACK*)this_stack;
	if (isStackNULL(this_stack, "Stack_Capacity"))
		return 0;
	return ContainerObject_capacity(&stack->object);
}
//将st2拷贝到st1
void Stack_Copy(stack* this_stackOne, const stack* this_stackTwo)
{
	STACK* stack1=(STACK*)this_stackOne;
	STACK* stack2=(STACK*)this_stackTwo;
	if (isStackNULL(stack1, "Stack_Copy")|| isStackNULL(stack2, "Stack_Copy"))
		return ;
	free(stack1->object._data);
	stack1->object._data = malloc(stack2->object._size * stack2->object._type);
	memcpy(stack1->object._data, stack2->object._data, stack2->object._size * stack2->object._type);
	stack1->object._capacity = stack2->object._size;
	stack1->object._size = stack2->object._size;
	stack1->object._type = stack2->object._type;
}
//将st2逆序拷贝到st1
void Stack_Rcopy(stack* this_stackOne, const stack* this_stackTwo)
{
	STACK* stack1=(STACK*)this_stackOne;
	STACK* stack2=(STACK*)this_stackTwo;
	if (stack2->object._size == 0)
		return;
	if (isStackNULL(stack1, "Stack_Rcopy") || isStackNULL(stack2, "Stack_Rcopy"))
		return;
	free(stack1->object._data);
	stack1->object._data = malloc(stack2->object._size * stack2->object._type);
	stack1->object._capacity = stack2->object._size;
	stack1->object._size = stack2->object._size;
	stack1->object._type = stack2->object._type;
	for (char* pst2 = (char*)stack2->object._data + (stack2->object._size - 1) * stack2->object._type, *pst1 = stack1->object._data; pst2 >= stack2->object._data; pst2 -= stack2->object._type, pst1 += stack2->object._type)
	{
		memcpy(pst1, pst2, stack2->object._type);
	}
}
void Stack_Swap(stack* this_stackOne, stack* this_stackTwo)//交换两个栈
{
	STACK* stack1=(STACK*)this_stackOne;
	STACK* stack2=(STACK*)this_stackTwo;
	if (isStackNULL(stack1, "Stack_Swap") || isStackNULL(stack2, "Stack_Swap"))
		return;
	ContainerObject_swap(&stack1->object, &stack2->object);
}
void Stact_free(stack* this_stack)
{
	if (isStackNULL(this_stack, "Stact_free"))
		return ;
	Stack_clear(this_stack);
	free(this_stack);
}
void Stack_pop(stack* this_stack)//移除栈顶元素 O(1)
{
	if (isStackNULL(this_stack, "Stack_pop"))
		return ;
	STACK* stack=(STACK*)this_stack;
	if (stack->object._size > 0)
		stack->object._size--;
}

