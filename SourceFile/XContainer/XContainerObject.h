#ifndef CONTAINEROBJECT_H
#define CONTAINEROBJECT_H
#include<stdio.h>
#include<stdbool.h>
//容器基类
typedef struct XContainerObject
{
	//判断函数
	const bool (*empty)(const struct XContainerObject*);// 检测容器是否为空，空为真 O(1)
	//大小函数
	const size_t(*size)(const struct XContainerObject*);//返回容器内元素的个数 O(1)
	const size_t(*capacity)(const struct XContainerObject*); //返回当前容器所能容纳的最大元素值
	//其他函数
	void (*swap)(struct XContainerObject*, struct XContainerObject*);//交换两个同类型容器的数据
	void* _data;//指向容器数据的指针
	size_t  _capacity;//当前容器能容纳的最大元素数量
	size_t _size;//当前容器内的元素个数
	size_t _type;//类型占用字节数
}XContainerObject;
bool isObjectNULL(const struct XContainerObject* Object, const char* str);
const bool XContainerObject_empty(const struct XContainerObject* Object);
const size_t XContainerObject_size(const struct XContainerObject* Object);
const size_t XContainerObject_capacity(const struct XContainerObject* Object);
void XContainerObject_swap(struct XContainerObject* ObjectOne, struct XContainerObject* ObjectTwo);
void XContainerObject_init( struct XContainerObject* Object,size_t type);
#endif // !ContainerObject_h
