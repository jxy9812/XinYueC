#include"XVector.h"
//初始化函数
XVector* XVector_init(const size_t TypeSize)
{
	XVector* this_vector = malloc(sizeof(XVector));
	if (ISNULL(this_vector, "创建XVector失败"))
		return NULL;
	XContainerObject_init(this_vector, TypeSize);
	return this_vector;
}

void XVector_resize(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, size_t);
	ObjectVirtualFunc(this_vector, Resize, funcPtr)(this_vector, size);
}

void XVector_push_front(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, Push_Front, funcPtr)(this_vector, LpValue);
}

void XVector_push_back(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, Push_Back, funcPtr)(this_vector, LpValue);
}

void XVector_insert(XVector* this_vector, int64_t index, const void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, int64_t, void*);
	ObjectVirtualFunc(this_vector, Insert, funcPtr)(this_vector,index, LpValue);
}

void XVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	ObjectVirtualFunc(this_vector, Inserts, funcPtr)(this_vector, index, LpValue,n);
}

void XVector_insertArray(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	ObjectVirtualFunc(this_vector, InsertArray, funcPtr)(this_vector, index, begin, n);
}

void XVector_pop_front(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, Pop_Front, funcPtr)(this_vector);
}

void XVector_pop_back(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, Pop_Back, funcPtr)(this_vector);
}

void XVector_erase(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, void*);
	ObjectVirtualFunc(this_vector, Erase, funcPtr)(this_vector,LpValue);
}

void XVector_remove(XVector* this_vector, int64_t index, int64_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	ObjectVirtualFunc(this_vector, Erase, funcPtr)(this_vector, index,n);
}

void XVector_clear(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, Clear, funcPtr)(this_vector);
}

void* XVector_at(const XVector* this_vector, int64_t index)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, int64_t);
	return ObjectVirtualFunc(this_vector, At, funcPtr)(this_vector, index);
}

void* XVector_front(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return ObjectVirtualFunc(this_vector, Front, funcPtr)(this_vector);
}

void* XVector_back(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return ObjectVirtualFunc(this_vector, Back, funcPtr)(this_vector);
}

void* XVector_find(const XVector* this_vector, XEquality equality, const void* findVal)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, XEquality, const void*);
	return ObjectVirtualFunc(this_vector, Find, funcPtr)(this_vector, equality,findVal);
}

void XVector_sort(XVector* this_vector, XCompare compare)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*, XCompare);
	ObjectVirtualFunc(this_vector, Sort, funcPtr)(this_vector, compare);
}

void XVector_free(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(this_vector->object.vtable, ""))
		return NULL;
	typedef void (*funcPtr)(XVector*);
	ObjectVirtualFunc(this_vector, XContainerObject_Free, funcPtr)(this_vector);
}

bool XVector_empty(const XVector* this_vector)
{
	return XContainerObject_empty(this_vector);
}

int XVector_size(const XVector* this_vector)
{
	return XContainerObject_size(this_vector);
}

int XVector_capacity(const XVector* this_vector)
{
	return XContainerObject_capacity(this_vector);
}

void XVector_swap(XVector* this_vectorOne, XVector* this_vectorTwo)
{
	XContainerObject_swap(this_vectorOne,this_vectorTwo);
}

size_t XVector_typeSize(XVector* this_vector)
{
	return XContainerObject_typeSize(this_vector);
}
