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

//数据释放方法
typedef void (*XCDataDeleteMethod)(void* args);

#define XCONTAINEROBJECT_VTABLE_SIZE   (XCLASS_VTABLE_SIZE+6)      //容器基类虚函数表大小
//XContainerObject虚函数表枚举
enum XContainerObjectVtableEnum
{
	//EXContainerObject_Delete,
	EXContainerObject_IsEmpty= XCLASS_VTABLE_SIZE,
	EXContainerObject_Size,
	EXContainerObject_Capacity,
	EXContainerObject_TypeSize,
	EXContainerObject_Swap,
	EXContainerObject_Clear,
};
//容器基类
typedef struct XContainerObject
{
	XClass m_parent;
	void* m_data;//指向容器数据的指针
	XCDataDeleteMethod m_dataDeleteMethod;//数据释放方法
	size_t  m_capacity;//当前容器能容纳的最大元素数量
	size_t m_size;//当前容器内的元素个数
	size_t m_typeSize;//类型占用字节数
}XContainerObject;
//宏函数
#define XContainerValue(LPVal,type) (*(type*)LPVal)//派生类 读取数据 
#define XContainerDataPtr(Object) ((XContainerObject*)(Object))->m_data//当前数据指针
#define XContainerData(Object,Type) (*(Type*)XContainerDataPtr(Object))//当前数据
#define XContainerCapacity(Object) (((XContainerObject*)(Object))->m_capacity)//当前容器能容纳的最大元素数量
#define XContainerSize(Object) (((XContainerObject*)(Object))->m_size)//当前容器内的元素个数
#define XContainerIsEmpty(Object) (XContainerSize(Object)==0)//当前容器是否时空的
#define XContainerTypeSize(Object) (((XContainerObject*)(Object))->m_typeSize)//类型占用字节数
#define XContainerDataDeleteMethod(Object) (((XContainerObject*)(Object))->m_dataDeleteMethod)//获取容器数据释放方法
#define XContainerSetDataDeleteMethod(Object,method) (((XContainerObject*)(Object))->m_dataDeleteMethod=method)//设置容器的数据释放方法
//默认释放派生类的方法
void XContainerDefaultDerivedClassDataDeleteMethod(void* args);
XVtable* XContainerObject_class_init();
void XContainerObject_init(XContainerObject* Object, size_t typeSize);
#define XContainerObject_delete_base	XClass_delete_base
size_t XContainerObject_getSize_base(const XContainerObject* Object);
bool XContainerObject_isEmpty_base(const XContainerObject* Object);
size_t XContainerObject_getCapacity_base(const XContainerObject* Object);
size_t XContainerObject_getTypeSize_base(const XContainerObject* Object);
void XContainerObject_swap_base(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
void XContainerObject_clear_base(XContainerObject* Object);
#ifdef __cplusplus
}
#endif
#endif // !ContainerObject_h
