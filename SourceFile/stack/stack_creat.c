#include"stack.h"
#include"stack_head.h"
#include"algorithm.h"
#include<stdlib.h>
#include <stdarg.h> 
#include<string.h>
//初始化函数
stack* Stack_init(const char* arr, ...)
{
	STACK* this_stack = malloc(sizeof(STACK));
	char buf[20];
	strcpy(buf, arr);
	size_t len = strlen(buf);
	//去掉字符串空格
	Unblank(buf, middle | left | right);
	ContainerObject_init(&this_stack->object,0);
	if (strcmp(buf, "char") == 0)
	{
		this_stack->object._type = sizeof(char);
		this_stack->push = Stack_Push_char;//入栈
		this_stack->top = Stack_top_char;//取得栈顶元素（但不删除）O(1)
	}
	else if (strcmp(buf, "char*") == 0)
	{
		this_stack->object._type = sizeof(char*);
		this_stack->push = Stack_Push_Char;//入栈
		this_stack->top = Stack_top_Char;//取得栈顶元素（但不删除）O(1)
	}
	else if (strncmp(buf, "char[", 5) == 0 && buf[strlen(buf) - 1])
	{
		char num[20];
		size_t n = strlen(buf) - 6;
		strncpy(num, &buf[5], n);
		num[n] = '\0';
		this_stack->object._type = sizeof(char) * atoi(num);
		this_stack->push = Stack_Push_charArray;//入栈
		this_stack->top = Stack_top_charArray;//取得栈顶元素（但不删除）O(1)
		//printf("%d\n", st->_type);
	}
	else if (strcmp(buf, "int") == 0)
	{
		this_stack->object._type = sizeof(int);
		this_stack->push = Stack_Push_int;//入栈
		this_stack->top = Stack_top_int;//取得栈顶元素（但不删除）O(1)
	}
	else if (strcmp(buf, "int*") == 0)
	{
		this_stack->object._type = sizeof(int*);
		this_stack->push = Stack_Push_Int;//入栈
		this_stack->top = Stack_top_Int;//取得栈顶元素（但不删除）O(1)
	}
	//else if (strncmp(buf, "int[", 4) == 0 && buf[strlen(buf) - 1])
	//{
	//	char num[20];
	//	size_t n = strlen(buf) - 5;
	//	strncpy(num, &buf[4], n);
	//	num[n] = '\0';
	//	st->_type = sizeof(int) * TurnNumber(num);
	//	st->push = Stack_Push_charArray;//入栈
	//	st->top = Stack_top_charArray;//取得栈顶元素（但不删除）O(1)
	//	printf("%d\n", st->_type);
	//}
	else
	{
		va_list args;//接收可变参数，
		va_start(args, arr);
		size_t n = va_arg(args, size_t);//依次访问参数，需指定按照什么类型读取数据  
		if (n <= 0 || n > 1000)
		{
			perror("您的类型本程序无内置请输入类型的字符数量，将以void指针形式返回，请强转后解引用使用（上限1000字节）\n");
			exit(-1);
		}
		va_end(args);//参数使用结束  
		this_stack->object._type = n;
		this_stack->push = Stack_Push;//入栈
		this_stack->top = Stack_top;//取得栈顶元素（但不删除）O(1)
	}

	this_stack->clear = Stack_clear;//清空stack的队列，释放内存
	this_stack->pop = Stack_pop;//出栈
	this_stack->empty = Stack_empty;//检测栈内是否为空，空为真 O(1)
	this_stack->size = Stack_size;//返回stack内元素的个数 O(1)
	this_stack->capacity = Stack_Capacity;//返回当前stack所能容纳的最大元素值
	//st->_current = 0;
	this_stack->copy = Stack_Copy;
	this_stack->rcopy = Stack_Rcopy;
	this_stack->swap = Stack_Swap;
	this_stack->free = Stact_free;
	//开辟初始空间
	this_stack->object._data = malloc(this_stack->object._type * MAXNUM);
	if (this_stack->object._data == NULL)
	{
		perror("初始化sttor失败");
		exit(-1);
	}
	else
	{
		this_stack->object._capacity = MAXNUM;
	}
	return this_stack;
}