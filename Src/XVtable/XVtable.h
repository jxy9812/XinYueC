#ifndef XVtable_H
#define XVtable_H
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
typedef struct XVtable
{
	void** data;
	size_t size;
	size_t capacity;//当前容器能容纳的最大元素数量
}XVtable;
XVtable* XVtable_new();
void XVtable_init(XVtable* this_vtable);
void XVtable_insert(XVtable* this_vtable, int64_t index, const void* func);
void XVtable_insertArray(XVtable* this_vtable, int64_t index, const void** begin, size_t n);
void XVtable_push_back(XVtable* this_vtable, void* func);
void XVtable_pop_back(XVtable* this_vtable);
void XVtable_clear(XVtable* this_vtable);
bool XVtable_empty(XVtable* this_vtable);
size_t XVtable_size(XVtable* this_vtable);
void* XVtable_at(XVtable* this_vtable, int64_t index);
#define XVtable_At(this_vtable,index) (*(this_vtable->data + index))
#endif // !XVtable_H
