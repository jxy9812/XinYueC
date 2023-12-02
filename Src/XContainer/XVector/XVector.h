#ifndef VECTOR_H
#define VECTOR_H
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XContainerObject.h"
#include"XVectorTwo_func.h"
#include"XVector_macro.h"
#include"XVector_Iterator/XVector_iterator.h"
#include"XVector_Iterator/XVector_reverse_iterator.h"
//XVector虚函数表
extern void* XVectorVtable[];
//XVector虚函数表枚举
enum XVectorEnum
{
	Resize=XContainerObject_Free + 1,
	Push_Front,
	Push_Back,
	Inserts,
	Insert,
	InsertArray,
	Pop_Front,
	Pop_Back,
	Erase,
	Remove,
	Clear,
	At,
	Front,
	Back,
	Find,
	Sort
};
typedef struct XVector
{
	XContainerObject object;
}XVector;
//设置XVector的大小，超过大小插入0值数据，小于删除数据
void XVector_resize(XVector* this_vector,size_t size);
// 向量头部增加一个元素
void XVector_push_front(XVector* this_vector, void* LpValue);
// 向量尾部增加一个元素
void XVector_push_back(XVector* this_vector, void* LpValue);
// 向量中前增加一个元素
void XVector_insert(XVector* this_vector, int64_t index, const void* LpValue);
// 向量中指向元素p前增加n个相同的元素x
void XVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n);
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void XVector_insertArray(XVector* this_vector, int64_t index, const void* begin, size_t n);

void XVector_pop_front(XVector* this_vector);
//删除向量中最后一个元素
void XVector_pop_back(XVector* this_vector);
//删除指针区间内的数据
void XVector_erase(XVector* this_vector, void* LpValue);
//删除数据 n<0 后面全部删除
void XVector_remove(XVector* this_vector, int64_t index, int64_t n);
//清空vector的队列，不是释放内存
void XVector_clear(XVector* this_vector);
// 返回元素的指针
void* XVector_at(const XVector* this_vector, int64_t index);
//返回向量头指针，指向第一个元素
void* XVector_front(const  XVector* this_vector);
//返回向量尾指针，指向向量最后一个元素
void* XVector_back(const  XVector* this_vector);
//查找数据，返回找到的指针，没有返回NULL
void* XVector_find(const XVector* this_vector, XEquality equality, const void* findVal);
//排序
void  XVector_sort(XVector* this_vector, XCompare compare);
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
//返回元素类型字节大小
size_t XVector_typeSize(XVector* this_vector);
//开辟一个动态数组,初始化
XVector* XVector_init(const size_t TypeSize);

#endif // !VECTOR_H





