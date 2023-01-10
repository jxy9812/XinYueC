#ifndef VECTOR_H
#define VECTOR_H
#include<stdio.h>
#include<stdbool.h>
typedef struct vector
{
	//插入函数
	void(*push_back)(struct vector* vec, void*);//尾插
	void (*insert_front)(struct vector*, const void* p, const void* x);// 向量中指向元素p前增加一个元素x
	void (*insert_nfront)(struct vector*, const void* p, const int n, const void* x);// 向量中指向元素p前增加n个相同的元素x
	void (*insert)(struct vector*, const void* p, const void* p1, const void* p2);// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
	//删除函数
	void (*pop_back)(struct vector*);//尾删
	void (*erase_p)(struct vector*, const void*, const void*);//删除指针区间内的数据
	void (*erase_int)(struct vector*, const int, const int);//删除区间内的数据
	void (*clear) (struct vector*);//清空vector的队列，释放内存
	//遍历函数
	void* (*at)(const struct vector*, int);// 返回第0-n个元素的指针
	void* (*front)(const struct vector*);// 返回向量头指针，指向第一个元素
	void* (*back)(const struct vector*);//返回向量尾指针，指向向量最后一个元素
	void* (*find)(const struct vector* vec, const void* val, bool(*fi)(const void* val1, const void* val2));//查找数据，返回找到的指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct vector*);// 检测vector内是否为空，空为真 O(1)
	//大小函数
	int (*size)(const struct vector*);//返回vector内元素的个数 O(1)
	int (*capacity)(const struct vector*); //返回当前向量所能容纳的最大元素个数
	//其他函数
	void (*sort)(struct vector*, int (*Sort)(void*, void*));//排序
	void (*swap)(struct vector*, struct vector*);//交换两个同类型向量的数据
}vector;
void Vector_clear(vector* vec);//清空vector的队列，释放内存
void Vector_Push_Back(vector* vec, void* x);// 向量尾部增加一个元素X
void Vector_insert_front(vector* vec, const void* p, const void* x);// 向量中指向元素p前增加一个元素x
void Vector_insert_nfront(vector* vec, const void* p, const int n, const void* x);// 向量中指向元素p前增加n个相同的元素x
void Vector_insert(vector* vec, const void* p, const void* p1, const void* p2);// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void Vector_pop_back(vector* vec);//删除向量中最后一个元素
void Vector_erase_p(vector* vec, const void* p1, const void* p2);//删除指针区间内的数据
void Vector_erase_int(vector* vec, const int left, const int right);//删除区间内的数据
void* Vector_at(const vector* vec, int i);// 返回元素的指针
void* Vector_front(const  vector* vec);//返回向量头指针，指向第一个元素
void* Vector_back(const  vector* vec);//返回向量尾指针，指向向量最后一个元素
void* Vector_find(const vector* vec, const void* val, bool(*fi)(const void* val1, const void* val2));//查找数据，返回找到的指针，没有返回NULL
bool Vector_empty(const  vector* vec);//检测vector内是否为空，空为真 O(1)
int Vector_size(const  vector* vec);//返回vector内元素的个数 O(1)
int  Vector_capacity(const  vector* vec);//返回当前向量所能容纳的最大元素个数

void  Vector_sort(vector* vec, int (*Sort)(void* x, void* y));//排序
void Vector_swap(vector* vec1, vector* vec2);//交换两个同类型向量的数据
//开辟一个动态数组
vector* NewVector(const char* arr, ...);

#endif // !VECTOR_H





