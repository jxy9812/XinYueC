#include"XDataStructConfig.h"
#if !defined(XVECTOR_H)&& XVector_ON
#define XVECTOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include<stdarg.h>
#include"XContainerObject.h"
#include"XVectorTwo_func.h"
#include"XVector_iterator.h"
#include"XVector_reverse_iterator.h"
#define XVECTOR_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XVector))       //XVector容器虚函数表大小
//XVector虚函数表枚举
XCLASS_DEFINE_BEGING(XVector)
XCLASS_DEFINE_ENUM(XVector, Resize)=XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XVector, Push_Front),
XCLASS_DEFINE_ENUM(XVector, Push_Back),
XCLASS_DEFINE_ENUM(XVector, Inserts),
XCLASS_DEFINE_ENUM(XVector, Insert),
XCLASS_DEFINE_ENUM(XVector, Insert_Array),
XCLASS_DEFINE_ENUM(XVector, append_Array),
XCLASS_DEFINE_ENUM(XVector, Pop_Front),
XCLASS_DEFINE_ENUM(XVector, Pop_Back),
XCLASS_DEFINE_ENUM(XVector, Erase),
XCLASS_DEFINE_ENUM(XVector, Remove),
XCLASS_DEFINE_ENUM(XVector, Copy),
XCLASS_DEFINE_ENUM(XVector, Rcopy),
XCLASS_DEFINE_ENUM(XVector, At),
XCLASS_DEFINE_ENUM(XVector, Front),
XCLASS_DEFINE_ENUM(XVector, Back),
XCLASS_DEFINE_ENUM(XVector, Find),
XCLASS_DEFINE_ENUM(XVector, Sort),
XCLASS_DEFINE_END(XVector)

typedef struct XVector
{
	XContainerObject m_parent;
	XEquality m_equality;//相等比较函数
}XVector;

XVtable* XVector_class_init();
//开辟一个动态数组,初始化 size_t
XVector* XVector_create(size_t typeSize);
#define XVector_Create(Type) XVector_create(sizeof(Type))
//初始化 XVector
void XVector_init(XVector* this_vector, size_t typeSize);
//设置XVector的大小，超过大小插入0值数据，小于删除数据
bool XVector_resize_base(XVector* this_vector,size_t size);
// 向量头部增加一个元素
void XVector_push_front_base(XVector* this_vector, void* pvValue);
#define XVector_Push_Front_Base(this_vector,type,value){type t=value;XVector_push_front_base(this_vector,&t);}
// 向量尾部增加一个元素
void XVector_push_back_base(XVector* this_vector, void* pvValue);
#define XVector_Push_Back_Base(this_vector,type,value){type t=value;XVector_push_back_base(this_vector,&t);}
// 向量中前增加一个元素
void XVector_insert_base(XVector* this_vector, int64_t index, const void* pvValue);
#define XVector_Insert_Base(this_vector,index,type,value){type t=value;XVector_insert_base(this_vector,index,&t);}
// 向量中指向元素p前增加n个相同的元素x
void XVector_inserts_base(XVector* this_vector, int64_t index, void* pvValue, size_t n);
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void XVector_insert_array_base(XVector* this_vector, int64_t index, const void* begin, size_t n);
void XVector_append_array_base(XVector* this_vector, const void* begin, size_t n);
void XVector_pop_front_base(XVector* this_vector);
//删除向量中最后一个元素
void XVector_pop_back_base(XVector* this_vector);
//删除迭代器数据，并返回下一个迭代器
void XVector_erase_base(XVector* this_vector,const XVector_iterator* it, XVector_iterator*next);
//删除数据 n<0 后面全部删除
void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n);
//将this_Two拷贝到this_One
void XVector_copy_base(XVector* this_One, const XVector* this_Two);
//将this_Two逆序拷贝到this_One
void XVector_rcopy_base(XVector* this_One, const XVector* this_Two);
// 返回元素的指针
void* XVector_at_base(const XVector* this_vector, int64_t index);
#define XVector_At_Base(vector,index,type) (*((type*)XVector_at_base(vector,index)))
//返回向量头指针，指向第一个元素
void* XVector_front_base(const  XVector* this_vector);
#define XVector_Front_Base(vector,type) (*((type*)XVector_front_base(vector)))
//返回向量尾指针，指向向量最后一个元素
void* XVector_back_base(const  XVector* this_vector);
#define XVector_Back_Base(vector,type) (*((type*)XVector_back_base(vector)))
//查找数据，返回找到的指针，没有返回NULL
void* XVector_find_base(const XVector* this_vector, const void* findVal);
//排序
void  XVector_sort_base(XVector* this_vector, XCompare compare);
//释放内存
#define XVector_delete_base				XContainerObject_delete_base
//清空vector的队列，不是释放内存
#define XVector_clear_base				XContainerObject_clear_base
//检测vector内是否为空，空为真 O(1)
#define XVector_isEmpty_base			XContainerObject_isEmpty_base
//返回vector内元素的个数 O(1)
#define XVector_getSize_base			XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XVector_getCapacity_base		XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XVector_swap_base				XContainerObject_swap_base
//返回元素类型字节大小
#define XVector_getTypeSize_base		XContainerObject_getTypeSize_base

//格式构造字符串
bool XVector_format_text_core(XVector* vector, bool appendNull, const char* format, va_list args);
bool XVector_append_text_fmt(XVector* this_vector, bool appendNull, const char* format, ...);
XVector* XVector_create_text_fmt(bool appendNull, const char* format, ...);
#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H





