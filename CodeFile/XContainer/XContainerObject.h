#ifndef CONTAINEROBJECT_H
#define CONTAINEROBJECT_H
#include<stdio.h>
#include<stdlib.h>
#include<stdbool.h>
//#define DEBUG_ON 1
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
#define ObjectData(Object,Type) (*(Type*)(Object->_data))
#define ObjectSize(Object) (((XContainerObject*)Object)->_size)
bool isNULL(const void*args/*参数数值*/,const char*argsName/*参数名字*/, const char* str/*附加参数*/,const char*funcName/*函数名字*/,const char* filePath/*所在文件路径*/,int line/*所在行号*/);
const bool XContainerObject_empty(const struct XContainerObject* Object);
const size_t XContainerObject_size(const struct XContainerObject* Object);
const size_t XContainerObject_capacity(const struct XContainerObject* Object);
const size_t XContainerObject_type(const struct XContainerObject* Object);
void XContainerObject_swap(struct XContainerObject* ObjectOne, struct XContainerObject* ObjectTwo);
void XContainerObject_init( struct XContainerObject* Object,size_t type);
#endif // !ContainerObject_h
