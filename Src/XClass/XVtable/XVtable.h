#ifndef XVtable_H
#define XVtable_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XTypes.h"
typedef struct 
{
	void** data;//
	size_t size;
	size_t capacity;//当前容器能容纳的最大元素数量
	bool isStack;//定义在栈上
}XVtable;
XVtable* XVtable_create();
//定义在栈上
void XVtable_init_stack(XVtable* this_vtable, void** data, size_t size);
#define XVtable_Init_Stack(this_vtable,vtable_data) XVtable_init_stack((this_vtable),vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]))
//初始化堆上的
void XVtable_init(XVtable* this_vtable);
void XVtable_insert(XVtable* this_vtable, int64_t index, const void* func);
void XVtable_insert_array(XVtable* this_vtable, int64_t index, const void** begin, size_t n);
void XVtable_append_array(XVtable* this_vtable, const void** begin, size_t n);
void XVtable_append_vtable(XVtable* this_vtable, XVtable* table);
void XVtable_push_back(XVtable* this_vtable, void* func);
void XVtable_pop_back(XVtable* this_vtable);
void XVtable_clear(XVtable* this_vtable);
bool XVtable_empty(XVtable* this_vtable);
size_t XVtable_size(XVtable* this_vtable);
//重写函数
void* XVtable_at(XVtable* this_vtable, size_t index);
#define XVtable_At(this_vtable,index) (*(this_vtable->data + index))

//定义类虚函数枚举开始
#define XCLASS_DEFINE_BEGING(Class)  enum Class##VtableEnum{
//定义类的虚函数枚举
#define XCLASS_DEFINE_ENUM(Class,Value) E##Class##_##Value
//定义类的虚函数枚举结束
#define XCLASS_DEFINE_END(Class)    XCLASS_VTABLE_GET_SIZE(Class)};
#define XCLASS_DEFINE_EXTEND_END(Class,Parent)    XCLASS_VTABLE_GET_SIZE(Class)=XCLASS_VTABLE_GET_SIZE(Parent)};
//获取类虚函数表大小
#define XCLASS_VTABLE_GET_SIZE(Class)   E##Class##_END_SIZE
#ifdef __cplusplus
}
#endif
#endif// !XVtable_H
