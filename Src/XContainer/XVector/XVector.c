#include"XVector.h"
#if XVector_ON
#include<stdlib.h>

XVector* XVector_new(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_vector = XMemory_malloc(sizeof(XVector));
	XVector_init(this_vector,typeSize);
	return this_vector;
}
//初始化函数
void XVector_init(XVector* this_vector,size_t typeSize )
{
	if (ISNULL(this_vector, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_vector, typeSize);
	XVector_class_init();
	ObjectVtable(this_vector)= XVectorVtable;
	this_vector->m_equality = NULL;
}

void XVector_resize(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, size_t);
	ObjectVirtualFunc(this_vector, EXVector_Resize, funcPtr)(this_vector, size);
}

void XVector_push_front(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, EXVector_Push_Front, funcPtr)(this_vector, LpValue);
}

void XVector_push_back(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, EXVector_Push_Back, funcPtr)(this_vector, LpValue);
}

void XVector_insert(XVector* this_vector, int64_t index, const void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, void*);
	ObjectVirtualFunc(this_vector, EXVector_Insert, funcPtr)(this_vector,index, LpValue);
}

void XVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	ObjectVirtualFunc(this_vector, EXVector_Inserts, funcPtr)(this_vector, index, LpValue,n);
}

void XVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	ObjectVirtualFunc(this_vector, EXVector_Insert_Array, funcPtr)(this_vector, index, begin, n);
}

void XVector_append_array(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*, void*, size_t);
	ObjectVirtualFunc(this_vector, EXVector_append_Array, funcPtr)(this_vector, begin, n);
}

void XVector_pop_front(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, EXVector_Pop_Front, funcPtr)(this_vector);
}

void XVector_pop_back(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, EXVector_Pop_Back, funcPtr)(this_vector);
}

void XVector_erase(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, EXVector_Erase, funcPtr)(this_vector,LpValue);
}

void XVector_remove(XVector* this_vector, int64_t index, int64_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	ObjectVirtualFunc(this_vector, EXVector_Remove, funcPtr)(this_vector, index,n);
}

//void XVector_clear(XVector* this_vector)
//{
//	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
//		return ;
//	typedef void (*funcPtr)(XVector*);
//	ObjectVirtualFunc(this_vector, EXVector_Clear, funcPtr)(this_vector);
//}

void XVector_copy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	typedef void(*funcPtr)(XVector*, XVector*);
	ObjectVirtualFunc(this_One, EXVector_Copy, funcPtr)(this_One, this_Two);
}

void XVector_rcopy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	typedef void(*funcPtr)(XVector*, XVector*);
	ObjectVirtualFunc(this_One, EXVector_Rcopy, funcPtr)(this_One, this_Two);
}

void* XVector_at(const XVector* this_vector, int64_t index)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, int64_t);
	return ObjectVirtualFunc(this_vector, EXVector_At, funcPtr)(this_vector, index);
}

void* XVector_front(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return ObjectVirtualFunc(this_vector, EXVector_Front, funcPtr)(this_vector);
}

void* XVector_back(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return ObjectVirtualFunc(this_vector, EXVector_Back, funcPtr)(this_vector);
}

void* XVector_find(const XVector* this_vector,const void* findVal)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, const void*);
	return ObjectVirtualFunc(this_vector, EXVector_Find, funcPtr)(this_vector,findVal);
}

void XVector_sort(XVector* this_vector, XCompare compare)
{
	if (ISNULL(this_vector, "") || ISNULL(ObjectVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, XCompare);
	ObjectVirtualFunc(this_vector, EXVector_Sort, funcPtr)(this_vector, compare);
}

//void XVector_free(XVector* this_vector)
//{
//	return XContainerObject_free(this_vector);
//}
//
//bool XVector_isEmpty(const XVector* this_vector)
//{
//	return XContainerObject_isEmpty(this_vector);
//}
//
//size_t XVector_size(const XVector* this_vector)
//{
//	return XContainerObject_size(this_vector);
//}
//
//size_t XVector_capacity(const XVector* this_vector)
//{
//	return XContainerObject_capacity(this_vector);
//}
//
//void XVector_swap(XVector* this_vectorOne, XVector* this_vectorTwo)
//{
//	XContainerObject_swap(this_vectorOne,this_vectorTwo);
//}
//
//size_t XVector_typeSize(XVector* this_vector)
//{
//	return XContainerObject_typeSize(this_vector);
//}
#endif