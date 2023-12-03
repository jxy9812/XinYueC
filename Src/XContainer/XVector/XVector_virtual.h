#ifndef XVECTOR_VIRTUAL_H
#define XVECTOR_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XFunctionCallback.h"
typedef struct XVector XVector;
void VXVector_free(XVector* this_vector);//释放内存
void VXVector_resize(XVector* this_vector, size_t size);
void VXVector_push_front(XVector* this_vector, void* LpValue);
void VXVector_push_back(XVector* this_vector, void* LpValue);
void VXVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n);
void VXVector_insert(XVector* this_vector, int64_t index, const void* LpValue);
void VXVector_insertArray(XVector* this_vector, int64_t index, const void* begin, size_t n);
void VXVector_pop_front(XVector* this_vector);
void VXVector_pop_back(XVector* this_vector);
void VXVector_erase(XVector* this_vector, void* LpValue);
void VXVector_remove(XVector* this_vector, int64_t index, int64_t n);//删除数据 n<0 后面全部删除
void VXVector_clear(XVector* this_vector);
void* VXVector_at(const XVector* this_vector, int64_t index);
void* VXVector_front(const XVector* this_vector);
void* VXVector_back(const XVector* this_vector);
void* VXVector_find(const XVector* this_vector, const void* findVal);//查找数据，返回找到的指针，没有返回NULL
void VXVector_sort(XVector* this_vector, XCompare compare);//排序
#endif // !XVECTOR_VIRTUAL_H
