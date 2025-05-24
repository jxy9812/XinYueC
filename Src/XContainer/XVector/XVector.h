#include"XDataStructConfig.h"
#if !defined(XVECTOR_H)&& XVector_ON
#define XVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XContainerObject.h"
#include"XVectorTwo_func.h"
#include"XVector_iterator.h"
#include"XVector_reverse_iterator.h"
//XVector虚函数表
extern XVtable* XVectorVtable;
#define XVECTOR_VTABLE_SIZE (XCONTAINEROBJECT_VTABLE_SIZE+18)       //XVector容器虚函数表大小
//XVector虚函数表枚举
enum XVectorEnum
{
	EXVector_Resize= XCONTAINEROBJECT_VTABLE_SIZE,
	EXVector_Push_Front,
	EXVector_Push_Back,
	EXVector_Inserts,
	EXVector_Insert,
	EXVector_Insert_Array,
	EXVector_append_Array,
	EXVector_Pop_Front,
	EXVector_Pop_Back,
	EXVector_Erase,
	EXVector_Remove,
	EXVector_Copy,
	EXVector_Rcopy,
	EXVector_At,
	EXVector_Front,
	EXVector_Back,
	EXVector_Find,
	EXVector_Sort
};
typedef struct XVector
{
	XContainerObject m_object;
	XEquality m_equality;//相等比较函数
}XVector;
//初始化类
void XVector_class_init();
//开辟一个动态数组,初始化 size_t
XVector* XVector_new(size_t typeSize);
#define XVector_New(Type) XVector_new(sizeof(Type))
//初始化 XVector
void XVector_init(XVector* this_vector, size_t typeSize);
//释放内存
//void XVector_free(XVector* this_vector);
#define XVector_free   XContainerObject_free
//设置XVector的大小，超过大小插入0值数据，小于删除数据
void XVector_resize(XVector* this_vector,size_t size);
// 向量头部增加一个元素
void XVector_push_front(XVector* this_vector, void* LpValue);
#define XVector_Push_Front(this_vector,type,value){type t=value;XVector_push_front(this_vector,&t);}
// 向量尾部增加一个元素
void XVector_push_back(XVector* this_vector, void* LpValue);
#define XVector_Push_Back(this_vector,type,value){type t=value;XVector_push_back(this_vector,&t);}
// 向量中前增加一个元素
void XVector_insert(XVector* this_vector, int64_t index, const void* LpValue);
#define XVector_Insert(this_vector,index,type,value){type t=value;XVector_insert(this_vector,index,&t);}
// 向量中指向元素p前增加n个相同的元素x
void XVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n);
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void XVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n);
void XVector_append_array(XVector* this_vector, const void* begin, size_t n);
void XVector_pop_front(XVector* this_vector);
//删除向量中最后一个元素
void XVector_pop_back(XVector* this_vector);
//删除指针区间内的数据
void XVector_erase(XVector* this_vector, void* LpValue);
//删除数据 n<0 后面全部删除
void XVector_remove(XVector* this_vector, int64_t index, int64_t n);
//清空vector的队列，不是释放内存
//void XVector_clear(XVector* this_vector);
#define XVector_clear  XContainerObject_clear
//将this_Two拷贝到this_One
void XVector_copy(XVector* this_One, const XVector* this_Two);
//将this_Two逆序拷贝到this_One
void XVector_rcopy(XVector* this_One, const XVector* this_Two);
// 返回元素的指针
void* XVector_at(const XVector* this_vector, int64_t index);
#define XVector_At(vector,index,type) (*((type*)XVector_at(vector,index)))
//返回向量头指针，指向第一个元素
void* XVector_front(const  XVector* this_vector);
#define XVector_Front(vector,type) (*((type*)XVector_front(vector)))
//返回向量尾指针，指向向量最后一个元素
void* XVector_back(const  XVector* this_vector);
#define XVector_Back(vector,type) (*((type*)XVector_back(vector)))
//查找数据，返回找到的指针，没有返回NULL
void* XVector_find(const XVector* this_vector, const void* findVal);
//排序
void  XVector_sort(XVector* this_vector, XCompare compare);

//检测vector内是否为空，空为真 O(1)
//bool XVector_isEmpty(const  XVector* this_vector);
#define XVector_isEmpty		XContainerObject_isEmpty
//返回vector内元素的个数 O(1)
//size_t XVector_size(const  XVector* this_vector);
#define XVector_size		XContainerObject_size
//返回当前向量所能容纳的最大元素个数
//size_t  XVector_capacity(const  XVector* this_vector);
#define XVector_capacity		XContainerObject_capacity
//交换两个同类型向量的数据
//void XVector_swap(XVector* this_vectorOne, XVector* this_vectorTwo);
#define XVector_swap		XContainerObject_swap
//返回元素类型字节大小
//size_t XVector_typeSize(XVector* this_vector);
#define XVector_typeSize		XContainerObject_typeSize
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H





