#include"XDataStructConfig.h"
#include"XClass.h"
#if !defined(XCONTAINEROBJECT_H)&& XContainerObject_ON
#define XCONTAINEROBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>

#include"XMemory.h"
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
	void* _data;//指向容器数据的指针
	size_t  _capacity;//当前容器能容纳的最大元素数量
	size_t _size;//当前容器内的元素个数
	size_t _typeSize;//类型占用字节数
}XContainerObject;

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
#ifdef __cplusplus
}
#endif
#endif // !ContainerObject_h
