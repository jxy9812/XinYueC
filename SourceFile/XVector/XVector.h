#ifndef VECTOR_H
#define VECTOR_H
#include<stdio.h>
#include<stdbool.h>
typedef struct XVector
{
	//插入函数
	void(*push_back)(struct XVector* this_vector , void*);//尾插
	void (*insert_front)(struct XVector* this_vector, const void* p, const void* x);// 向量中指向元素p前增加一个元素x
	void (*insert_nfront)(struct XVector* this_vector, const void* p, const int n, const void* x);// 向量中指向元素p前增加n个相同的元素x
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
	void* (*find)(const struct XVector* this_vector, const void* val, bool(*fi)(const void* val1, const void* val2));//查找数据，返回找到的指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct XVector* this_vector);// 检测vector内是否为空，空为真 O(1)
	//大小函数
	int (*size)(const struct XVector* this_vector);//返回vector内元素的个数 O(1)
	int (*capacity)(const struct XVector* this_vector); //返回当前向量所能容纳的最大元素个数
	//其他函数
	void (*sort)(struct XVector* this_vector, int (*Sort)(void*, void*));//排序
	void (*swap)(struct XVector* vectorOne, struct XVector* vectorTwo);//交换两个同类型向量的数据
	//释放
	void (*free)(const struct XVector* this_vector);//释放内存
}XVector;
//清空vector的队列，释放内存v
void XVector_clear(XVector* this_vector);
// 向量尾部增加一个元素X
void XVector_Push_Back(XVector* this_vector, void* x);
// 向量中指向元素p前增加一个元素x
void XVector_insert_front(XVector* this_vector, const void* p, const void* x);
// 向量中指向元素p前增加n个相同的元素x
void XVector_insert_nfront(XVector* this_vector, const void* p, const int n, const void* x);
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void XVector_insert(XVector* this_vector, const void* p, const void* p1, const void* p2);
//删除向量中最后一个元素
void XVector_pop_back(XVector* this_vector);
//删除指针区间内的数据
void XVector_erase_p(XVector* this_vector, const void* p1, const void* p2);
//删除区间内的数据
void XVector_erase_int(XVector* this_vector, const int left, const int right);
// 返回元素的指针
void* XVector_at(const XVector* this_vector, int i);
//返回向量头指针，指向第一个元素
void* XVector_front(const  XVector* this_vector);
//返回向量尾指针，指向向量最后一个元素
void* XVector_back(const  XVector* this_vector);
//查找数据，返回找到的指针，没有返回NULL
void* XVector_find(const XVector* this_vector, const void* val, bool(*fi)(const void* val1, const void* val2));
//排序
void  XVector_sort(XVector* this_vector, int (*Sort)(void* x, void* y));
//释放内存
void XVector_free(const  XVector* this_vector);
//检测vector内是否为空，空为真 O(1)
bool XVector_empty(const  XVector* this_vector);
//返回vector内元素的个数 O(1)
int XVector_size(const  XVector* this_vector);
//返回当前向量所能容纳的最大元素个数
int  XVector_capacity(const  XVector* this_vector);
//交换两个同类型向量的数据
void XVector_swap(XVector* this_vectorOne, XVector* this_vectorTwo);
//开辟一个动态数组,初始化
XVector* XVector_init(const char* arr, ...);

#endif // !VECTOR_H





