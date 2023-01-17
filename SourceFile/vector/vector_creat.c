#include"vector.h"
#include"vector_head.h"
#include"algorithm.h"
#include<stdlib.h>
#include<string.h>
#include <stdarg.h> 
//初始化函数
vector* Vector_init(const char* arr, ...)
{
	VECTOR* this_vector = malloc(sizeof(VECTOR));
	char buf[100];
	strcpy(buf, arr);
	size_t len = strlen(buf);
	//printf("去空格前：%s\n", buf);
	//去掉字符串空格
	Unblank(buf, middle | left | right);
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
		ContainerObject_init(&this_vector->object, n);
		//this_vector->object._type = n;
	}
	this_vector->clear = Vector_clear;//清空vector的队列，释放内存
	this_vector->push_back = Vector_Push_Back;//尾插
	this_vector->insert_front = Vector_insert_front;// 向量中指向元素p前增加一个元素x
	this_vector->insert_nfront = Vector_insert_nfront;// 向量中指向元素p前增加n个相同的元素x
	this_vector->insert = Vector_insert;// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
	this_vector->pop_back = Vector_pop_back;//尾删
	this_vector->erase_p = Vector_erase_p;//删除指针区间内的数据
	this_vector->erase_int = Vector_erase_int;//删除区间内的数据
	this_vector->at = Vector_at;//返回指定位置的指针
	this_vector->front = Vector_front;//返回向量头指针，指向第一个元素
	this_vector->back = Vector_back;//返回向量尾指针，指向向量最后一个元素
	this_vector->find = Vector_find;//查找数据，返回找到的指针，没有返回NULL
	this_vector->sort = Vector_sort;//排序
	this_vector->free = Vector_free;//释放内存
	this_vector->empty = Vector_empty;//检测vector内是否为空，空为真 O(1)
	this_vector->size = Vector_size;//返回vector内元素的个数 O(1)
	this_vector->capacity = Vector_capacity;//返回当前向量所能容纳的最大元素值
	this_vector->swap = Vector_swap;//交换两个同类型向量的数据
	
	//this_vector->_type = n;
	/*this_vector->_current = 0;
	this_vector->_size = 0;
	this_vector->_data = NULL;*/
	return this_vector;
}