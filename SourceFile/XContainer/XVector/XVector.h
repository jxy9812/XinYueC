#ifndef VECTOR_H
#define VECTOR_H
#include<stdio.h>
#include<stdbool.h>
#include"XVector_func.h"
#include"XVectorTwo_func.h"
#include"XVector_Iterator/XVector_iterator.h"
#include"XVector_Iterator/XVector_reverse_iterator.h"
typedef struct XVector
{
	//插入函数
	void(*push_back)(struct XVector* this_vector , void*);//尾插
	void (*insert_front)(struct XVector* this_vector, const void* p, const void*LValue);// 向量中指向元素p前增加一个元素x
	void (*insert_nfront)(struct XVector* this_vector, const void* p, const int n, const void*LValue);// 向量中指向元素p前增加n个相同的元素x
	void (*insert)(struct XVector* this_vector, const void* p, const void* p1, const void* p2);// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
	//删除函数
	void (*pop_back)(struct XVector* this_vector);//尾删
	void (*erase_p)(struct XVector* this_vector, const void*, const void*);//删除指针区间内的数据
	void (*erase_int)(struct XVector* this_vector, const int, const int);//删除区间内的数据
	void (*clear) (struct XVector* this_vector);//清空vector的队列，释放内存
	//遍历函数
	void* (*at)(const struct XVector* this_vector, int);// 返回第0-n个元素的指针
	void* (*front)(const struct XVector* this_vector);// 返回向量头指针，指向第一个元素
	void* (*back)(const struct XVector* this_vector);//返回向量尾指针，指向向量最后一个元素
	void* (*find)(const struct XVector* this_vector, const void* val, XEquality equality);//查找数据，返回找到的指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct XVector* this_vector);// 检测vector内是否为空，空为真 O(1)
	//大小函数
	int (*size)(const struct XVector* this_vector);//返回vector内元素的个数 O(1)
	int (*capacity)(const struct XVector* this_vector); //返回当前向量所能容纳的最大元素个数
	//其他函数
	void (*sort)(struct XVector* this_vector, XCompare compare);//排序
	void (*swap)(struct XVector* vectorOne, struct XVector* vectorTwo);//交换两个同类型向量的数据
	//释放
	void (*free)(const struct XVector* this_vector);//释放内存
}XVector;

#endif // !VECTOR_H





