#ifndef  VECTOR_HEAD
#define VECTOR_HEAD
#include"XContainerObject.h"
#define VECTORNUM 4//初始数组大小
typedef struct XVECTOR
{
	//插入函数
	void(*push_back)(void*);//尾插
	void (*insert_front)(struct XVECTOR*, const void* p, const void* x);// 向量中指向元素p前增加一个元素x
	void (*insert_nfront)(struct XVECTOR*, const void* p, const int n, const void* x);// 向量中指向元素p前增加n个相同的元素x
	void (*insert)(struct XVECTOR*, const void* p, const void* p1, const void* p2);// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
	//删除函数
	void (*pop_back)(struct XVECTOR*);//尾删
	void (*erase_p)(struct XVECTOR*, const void*, const void*);//删除指针区间内的数据
	void (*erase_int)(struct XVECTOR*, const int, const int);//删除区间内的数据
	void (*clear) (struct XVECTOR*);//清空vector的队列，释放内存
	//遍历函数
	void* (*at)(const struct XVECTOR*, int);// 返回第0-n个元素的指针
	void* (*front)(const struct XVECTOR*);// 返回向量头指针，指向第一个元素
	void* (*back)(const struct XVECTOR*);//返回向量尾指针，指向向量最后一个元素
	void* (*find)(const struct XVECTOR* this_vector, const void* val, bool(*fi)(const void* val1, const void* val2));//查找数据，返回找到的指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct XVECTOR*);// 检测vector内是否为空，空为真 O(1)
	//大小函数
	int (*size)(const struct XVECTOR*);//返回vector内元素的个数 O(1)
	int (*capacity)(const struct XVECTOR*); //返回当前向量所能容纳的最大元素值
	//其他函数
	void (*sort)(struct XVECTOR*, int (*Sort)(void*, void*));//排序
	void (*swap)(struct XVECTOR*, struct XVECTOR*);//交换两个同类型向量的数据
	//释放
	void (*free)(const struct XVECTOR* this_vector);//释放内存
	struct XContainerObject object;
	//void* _data;//指向自定义数组类型
	//int  _current;//当前元素个数
	//int _size;//元素最大个数
	//int _type;//类型占用字节数
}XVECTOR;
//检测是否需要扩容
void VectorEnlargeCapacity(XVECTOR* this_vector);
#endif // ! VECTOR_HEAD


