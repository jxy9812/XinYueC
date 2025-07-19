#include"XVector.h"
#if XVector_ON
#include<stdlib.h>
XVector* XVector_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_vector = XMemory_malloc(sizeof(XVector));
	XVector_init(this_vector,typeSize);
	return this_vector;
}

bool XVector_resize_base(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	typedef bool (*funcPtr)(XVector*, size_t);
	XClassGetVirtualFunc(this_vector, EXVector_Resize, funcPtr)(this_vector, size);
}

void XVector_push_front_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, void*);
	XClassGetVirtualFunc(this_vector, EXVector_Push_Front, funcPtr)(this_vector, pvValue);
}

void XVector_push_back_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, void*);
	XClassGetVirtualFunc(this_vector, EXVector_Push_Back, funcPtr)(this_vector, pvValue);
}

void XVector_insert_base(XVector* this_vector, int64_t index, const void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, void*);
	XClassGetVirtualFunc(this_vector, EXVector_Insert, funcPtr)(this_vector,index, pvValue);
}

void XVector_inserts_base(XVector* this_vector, int64_t index, void* pvValue, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	XClassGetVirtualFunc(this_vector, EXVector_Inserts, funcPtr)(this_vector, index, pvValue,n);
}

void XVector_insert_array_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(index, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, funcPtr)(this_vector, index, begin, n);
}

void XVector_append_array_base(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*, void*, size_t);
	XClassGetVirtualFunc(this_vector, EXVector_append_Array, funcPtr)(this_vector, begin, n);
}

void XVector_pop_front_base(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*);
	XClassGetVirtualFunc(this_vector, EXVector_Pop_Front, funcPtr)(this_vector);
}

void XVector_pop_back_base(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*);
	XClassGetVirtualFunc(this_vector, EXVector_Pop_Back, funcPtr)(this_vector);
}

void XVector_erase_base(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	XClassGetVirtualFunc(this_vector, EXVector_Erase, void(*)(XVector*, const XVector_iterator*, XVector_iterator*))(this_vector,it,next);
}

void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	XClassGetVirtualFunc(this_vector, EXVector_Remove, funcPtr)(this_vector, index,n);
}

//void XVector_clear_base(XVector* this_vector)
//{
//	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
//		return ;
//	typedef void (*funcPtr)(XVector*);
//	XClassGetVirtualFunc(this_vector, EXVector_Clear, funcPtr)(this_vector);
//}

void XVector_copy_base(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	typedef void(*funcPtr)(XVector*, XVector*);
	XClassGetVirtualFunc(this_One, EXVector_Copy, funcPtr)(this_One, this_Two);
}

void XVector_rcopy_base(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	typedef void(*funcPtr)(XVector*, XVector*);
	XClassGetVirtualFunc(this_One, EXVector_Rcopy, funcPtr)(this_One, this_Two);
}

void* XVector_at_base(const XVector* this_vector, int64_t index)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, int64_t);
	return XClassGetVirtualFunc(this_vector, EXVector_At, funcPtr)(this_vector, index);
}

void* XVector_front_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return XClassGetVirtualFunc(this_vector, EXVector_Front, funcPtr)(this_vector);
}

void* XVector_back_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*);
	return XClassGetVirtualFunc(this_vector, EXVector_Back, funcPtr)(this_vector);
}

void* XVector_find_base(const XVector* this_vector,const void* findVal)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	typedef void* (*funcPtr)(XVector*, const void*);
	return XClassGetVirtualFunc(this_vector, EXVector_Find, funcPtr)(this_vector,findVal);
}

void XVector_sort_base(XVector* this_vector, XCompare compare)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	typedef void (*funcPtr)(XVector*, XCompare);
	XClassGetVirtualFunc(this_vector, EXVector_Sort, funcPtr)(this_vector, compare);
}
// 内部核心函数：格式化文本并追加到向量
bool XVector_format_text_core(XVector* vector, bool appendNull, const char* format, va_list args) 
{
	if (vector == NULL || format == NULL)
		return false;

	// 复制 va_list 以便后续重用
	va_list args_copy;
	va_copy(args_copy, args);

	// 计算所需缓冲区大小
	int len = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (len <= 0) return false;

	// 调整向量大小
	const size_t newSize = len + 1;
	if (!XVector_resize_base(vector, newSize))
		return false;

	// 格式化文本到向量（重用 va_list）
	va_copy(args_copy, args);
	vsnprintf((char*)XContainerDataPtr(vector), len + 1, format, args_copy);
	va_end(args_copy);

	// 如果不保留\0，移除末尾字符
	if (!appendNull && newSize > 0) {
		XVector_pop_back_base(vector);
	}

	return true;
}
bool XVector_append_text_fmt(XVector* this_vector, bool appendNull, const char* format, ...)
{
	if (this_vector == NULL|| format==NULL)
		return false;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(this_vector, appendNull, format, args);
	va_end(args);

	return result;
}

XVector* XVector_create_text_fmt(bool appendNull, const char* format, ...)
{
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return NULL;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(data, appendNull, format, args);
	va_end(args);

	// 如果失败，释放内存并返回NULL
	if (!result) {
		XVector_delete_base(data);
		return NULL;
	}

	return data;
}

//void XVector_delete_base(XVector* this_vector)
//{
//	return XContainerObject_delete_base(this_vector);
//}
//
//bool XVector_isEmpty_base(const XVector* this_vector)
//{
//	return XContainerObject_isEmpty_base(this_vector);
//}
//
//size_t XVector_getSize_base(const XVector* this_vector)
//{
//	return XContainerObject_getSize_base(this_vector);
//}
//
//size_t XVector_getCapacity_base(const XVector* this_vector)
//{
//	return XContainerObject_getCapacity_base(this_vector);
//}
//
//void XVector_swap_base(XVector* this_vectorOne, XVector* this_vectorTwo)
//{
//	XContainerObject_swap_base(this_vectorOne,this_vectorTwo);
//}
//
//size_t XVector_getTypeSize_base(XVector* this_vector)
//{
//	return XContainerObject_getTypeSize_base(this_vector);
//}
#endif