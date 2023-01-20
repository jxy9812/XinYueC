#ifndef  XVECTOR_FUNC_H
#define XVECTOR_FUNC_H
#include<stdbool.h>
struct XVector;
//清空vector的队列，释放内存v
void XVector_clear(struct XVector* this_vector);
// 向量尾部增加一个元素X
void XVector_Push_Back(struct XVector* this_vector, void* LValue);
// 向量中指向元素p前增加一个元素x
void XVector_insert_front(struct XVector* this_vector, const void* p, const void* LValue);
// 向量中指向元素p前增加n个相同的元素x
void XVector_insert_nfront(struct XVector* this_vector, const void* p, const int n, const void* LValue);
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
void XVector_insert(struct XVector* this_vector, const void* p, const void* p1, const void* p2);
//删除向量中最后一个元素
void XVector_pop_back(struct XVector* this_vector);
//删除指针区间内的数据
void XVector_erase_p(struct XVector* this_vector, const void* p1, const void* p2);
//删除区间内的数据
void XVector_erase_int(struct XVector* this_vector, const int left, const int right);
// 返回元素的指针
void* XVector_at(const struct XVector* this_vector, int i);
//返回向量头指针，指向第一个元素
void* XVector_front(const  struct XVector* this_vector);
//返回向量尾指针，指向向量最后一个元素
void* XVector_back(const  struct XVector* this_vector);
//查找数据，返回找到的指针，没有返回NULL
void* XVector_find(const struct XVector* this_vector, const void* val, bool(*fi)(const void* val1, const void* val2));
//排序
void  XVector_sort(struct XVector* this_vector, int (*Sort)(const void* LPrevValue, const void* LNextValue));
//释放内存
void XVector_free(const  struct XVector* this_vector);
//检测vector内是否为空，空为真 O(1)
bool XVector_empty(const  struct XVector* this_vector);
//返回vector内元素的个数 O(1)
int XVector_size(const  struct XVector* this_vector);
//返回当前向量所能容纳的最大元素个数
int  XVector_capacity(const  struct XVector* this_vector);
//交换两个同类型向量的数据
void XVector_swap(struct XVector* this_vectorOne, struct XVector* this_vectorTwo);
//开辟一个动态数组,初始化
struct XVector* XVector_init(const char* arr, ...);

#endif // ! ITERATOR_H