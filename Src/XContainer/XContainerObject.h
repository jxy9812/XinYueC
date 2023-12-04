#ifndef XCONTAINEROBJECT_H
#define XCONTAINEROBJECT_H
#include<stdio.h>
//#include<stdlib.h>
#include<stdbool.h>
//#define DEBUG_ON 1

//XContainerObject虚函数表
extern void* XContainerObjectVtable[];
//XContainerObject虚函数表枚举
enum XContainerObjectVtableEnum
{
	EXContainerObject_Empty,
	EXContainerObject_Size,
	EXContainerObject_Capacity,
	EXContainerObject_TypeSize,
	EXContainerObject_Swap,
	EXContainerObject_Clear,
	EXContainerObject_Free
};
//容器基类
typedef struct XContainerObject
{
	void** vtable;//虚函数表
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
#define isNULLInfo(args,str) args,#args,str ,__FUNCTION__,__FILE__,__LINE__
#define ISNULL(args,str)(isNULL(isNULLInfo(args,str)))
#define ObjectDataPtr(Object) ((char*)((XContainerObject*)Object)->_data)//当前数据指针
#define ObjectData(Object,Type) (*(Type*)ObjectDataPtr(Object))//当前数据
#define ObjectCapacity(Object) (((XContainerObject*)Object)->_capacity)//当前容器能容纳的最大元素数量
#define ObjectSize(Object) (((XContainerObject*)Object)->_size)//当前容器内的元素个数
#define ObjectTypeSize(Object) (((XContainerObject*)Object)->_typeSize)//类型占用字节数
#define ObjectVtableFunc(Vtable,Offset,Type) ((Type)((Vtable)[Offset]))//用虚函数表获取函数
#define ObjectVirtualFunc(Object,Offset,Type) ((Type)((((XContainerObject*)Object)->vtable)[Offset]))//用XContainerObject及其子类获取虚函数
bool isNULL(const void*args/*参数数值*/,const char*argsName/*参数名字*/, const char* str/*附加参数*/,const char*funcName/*函数名字*/,const char* filePath/*所在文件路径*/,int line/*所在行号*/);
bool XContainerObject_empty(const XContainerObject* Object);
size_t XContainerObject_size(const XContainerObject* Object);
size_t XContainerObject_capacity(const XContainerObject* Object);
size_t XContainerObject_typeSize(const XContainerObject* Object);
void XContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
void XContainerObject_init(XContainerObject* Object,size_t type);
void XContainerObject_clear(XContainerObject* Object);
void XContainerObject_free(XContainerObject* Object);
#endif // !ContainerObject_h
