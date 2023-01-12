#ifndef CONTAINEROBJECT_H
#define CONTAINEROBJECT_H
#include<stdio.h>
#include<stdbool.h>
//容器基类
struct ContainerObject
{
	//判断函数
	const bool (*empty)(const struct ContainerObject*);// 检测容器是否为空，空为真 O(1)
	//大小函数
	const size_t(*size)(const struct ContainerObject*);//返回容器内元素的个数 O(1)
	const size_t(*capacity)(const struct ContainerObject*); //返回当前容器所能容纳的最大元素值
	//其他函数
	void (*swap)(struct ContainerObject*, struct ContainerObject*);//交换两个同类型容器的数据
	void* _data;//指向容器数据的指针
	size_t  _capacity;//当前容器能容纳的最大元素数量
	size_t _size;//当前容器内的元素个数
	size_t _type;//类型占用字节数
};
const bool ContainerObject_empty(const struct ContainerObject* Object);
const size_t ContainerObject_size(const struct ContainerObject* Object);
const size_t ContainerObject_capacity(const struct ContainerObject* Object);
void ContainerObject_swap(struct ContainerObject* ObjectOne, struct ContainerObject* ObjectTwo);
#endif // !ContainerObject_h
