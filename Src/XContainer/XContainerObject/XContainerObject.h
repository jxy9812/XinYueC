#ifndef XCONTAINEROBJECT_H
#define XCONTAINEROBJECT_H
#include<stdio.h>
//#include<stdlib.h>
#include<stdbool.h>
#include"XClass.h"
//#define DEBUG_ON 1

//XContainerObject虚函数表

extern XVtable* XContainerObjectVtable;
//XContainerObject虚函数表枚举
enum XContainerObjectVtableEnum
{
	EXContainerObject_Free,
	EXContainerObject_Empty,
	EXContainerObject_Size,
	EXContainerObject_Capacity,
	EXContainerObject_TypeSize,
	EXContainerObject_Swap,
	EXContainerObject_Clear,
};
//容器基类
typedef struct XContainerObject
{
	XClassObject object;
	//void** vtable;//虚函数表
	void* _data;//指向容器数据的指针
	size_t  _capacity;//当前容器能容纳的最大元素数量
	size_t _size;//当前容器内的元素个数
	size_t _typeSize;//类型占用字节数
}XContainerObject;

#ifdef DEBUG_ON
#if DEBUG_ON &&defined _DEBUG
#define PRINT(fmt,...) printf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define PRINT(fmt,...)
#endif
#else
#if defined _DEBUG
#define PRINT(fmt,...) printf("[FILE:%s][FUNC:%s][LINE:%d]\n->"fmt"\n",__FILE__,__FUNCTION__,__LINE__,__VA_ARGS__)
#else
#define PRINT(fmt,...)
#endif
#endif // !DEBUG_ON

#define ContainerDataPtr(Object) ((XContainerObject*)Object)->_data//当前数据指针
#define ContainerData(Object,Type) (*(Type*)ContainerDataPtr(Object))//当前数据
#define ContainerCapacity(Object) (((XContainerObject*)Object)->_capacity)//当前容器能容纳的最大元素数量
#define ContainerSize(Object) (((XContainerObject*)Object)->_size)//当前容器内的元素个数
#define ContainerTypeSize(Object) (((XContainerObject*)Object)->_typeSize)//类型占用字节数

void XContainerObject_class_init(); 
void XContainerObject_init(XContainerObject* Object, size_t typeSize);
void XContainerObject_free(XContainerObject* Object);
size_t XContainerObject_size(const XContainerObject* Object);
bool XContainerObject_empty(const XContainerObject* Object);
size_t XContainerObject_capacity(const XContainerObject* Object);
size_t XContainerObject_typeSize(const XContainerObject* Object);
void XContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
void XContainerObject_clear(XContainerObject* Object);
#endif // !ContainerObject_h
