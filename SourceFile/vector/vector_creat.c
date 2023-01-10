#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
#include <stdarg.h> 
//初始化函数
vector* NewVector(const char* arr, ...)
{
	VECTOR* vec = malloc(sizeof(VECTOR));
	char buf[20];
	strcpy(buf, arr);
	size_t len = strlen(buf);
	//printf("去空格前：%s\n", buf);
	//去掉字符串空格
	for (size_t i = 0; i < len; i++)
	{
		if (buf[len - 1 - i] == ' ')
		{
			for (size_t j = 0; j < i + 1; j++)
			{
				buf[len - 1 - i + j] = buf[len - i + j];
			}
		}
	}
	//printf("去空格后：%s  长度：%d\n", buf,strlen(buf));
	/*if (strcmp(buf, "char") == 0)
	{
		vec->_type= sizeof(char);

	}
	else*/
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
		vec->_type = n;
	}
	vec->clear = Vector_clear;//清空vector的队列，释放内存
	vec->push_back = Vector_Push_Back;//尾插
	vec->insert_front = Vector_insert_front;// 向量中指向元素p前增加一个元素x
	vec->insert_nfront = Vector_insert_nfront;// 向量中指向元素p前增加n个相同的元素x
	vec->insert = Vector_insert;// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
	vec->pop_back = Vector_pop_back;//尾删
	vec->erase_p = Vector_erase_p;//删除指针区间内的数据
	vec->erase_int = Vector_erase_int;//删除区间内的数据
	vec->at = Vector_at;//返回指定位置的指针
	vec->front = Vector_front;//返回向量头指针，指向第一个元素
	vec->back = Vector_back;//返回向量尾指针，指向向量最后一个元素
	vec->find = Vector_find;//查找数据，返回找到的指针，没有返回NULL
	vec->empty = Vector_empty;//检测vector内是否为空，空为真 O(1)
	vec->size = Vector_size;//返回vector内元素的个数 O(1)
	vec->capacity = Vector_capacity;//返回当前向量所能容纳的最大元素值
	vec->sort = Vector_sort;//排序
	vec->swap = Vector_swap;//交换两个同类型向量的数据
	//vec->_type = n;
	vec->_current = 0;
	vec->_size = 0;
	vec->_date = NULL;
	return vec;
}