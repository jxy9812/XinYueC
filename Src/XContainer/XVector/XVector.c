#include"XVector.h"
#if XVector_ON
#include<stdlib.h>
#include<string.h>
XVector* XVector_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_vector = XMemory_malloc(sizeof(XVector));
	XVector_init(this_vector,typeSize);
	return this_vector;
}

XVector* XVector_create_copy(const XVector* other)
{
	if (other == NULL)
		return NULL;
	XVector*  v=XVector_create(XContainerTypeSize(other));
	XVector_copy_base(v,other);
	return v;
}

XVector* XVector_create_move(XVector* other)
{
	if (other == NULL)
		return NULL;
	XVector* v = XVector_create(XContainerTypeSize(other));
	XVector_move_base(v, other);
	return v;
}

bool XVector_resize_base(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	typedef bool (*funcPtr)(XVector*, size_t);
	XClassGetVirtualFunc(this_vector, EXVector_Resize, funcPtr)(this_vector, size);
}

bool XVector_push_front_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Front, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_front_move_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Front, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_push_back_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_back_move_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_insert(XVector* this_vector, int64_t index, const void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	 return XVector_insert_array_base(this_vector, index, pvValue, 1);
	//XClassGetVirtualFunc(this_vector, EXVector_Insert_Copy, void (*)(XVector*, int64_t, void*))(this_vector,index, pvValue);
}

bool XVector_insert_move(XVector* this_vector, int64_t index, const void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XVector_insert_array_move_base(this_vector, index, pvValue, 1);
	//XClassGetVirtualFunc(this_vector, EXVector_Insert_Move, void (*)(XVector*, int64_t, void*))(this_vector, index, pvValue);
}

bool XVector_insert_array_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "")  || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_insert_array_move_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "")  || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataMoveMethod(this_vector));
}

bool XVector_append_array_base(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_append_Array, bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_append_array_move_base(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_append_Array, bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataMoveMethod(this_vector));
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
	return XClassGetVirtualFunc(this_vector, EXVector_At, void* (*)(XVector*, int64_t))(this_vector, index);
}

void* XVector_front_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	return XClassGetVirtualFunc(this_vector, EXVector_Front, void* (*)(XVector*))(this_vector);
}

void* XVector_back_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	return XClassGetVirtualFunc(this_vector, EXVector_Back, void* (*)(XVector*))(this_vector);
}

bool XVector_find_base(const XVector* this_vector,const void* findVal, XVector_iterator* it)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), "")|| ISNULL(findVal, ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Find, bool(*)(XVector*, const void*, XVector_iterator*))(this_vector,findVal,it);
}

bool XVector_contains(const XVector* this_vector, const void* value)
{
	return XVector_find_base(this_vector,value,NULL);
}

int64_t XVector_indexOf(const XVector* this_vector, const void* value, int64_t from)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL") ||
		ISNULL(value, "Value is NULL") ||
		from < 0 ||
		from >= (int64_t)XVector_size_base(this_vector))
	{
		return -1;
	}

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t size = XVector_size_base(this_vector);
	const char* data = (const char*)XContainerDataPtr(this_vector);

	// 使用内存比较查找元素
	for (size_t i = from; i < size; ++i)
	{
		const void* element = &data[i * typeSize];
		if (XContainerCompare(this_vector))
		{
			if(XContainerCompare(this_vector)(element, value)==XCompare_Equality)
				return (int64_t)i;
		}
		else if (memcmp(element, value, typeSize) == 0)
		{
			return (int64_t)i;
		}
	}

	// 未找到元素
	return -1;
}

int64_t XVector_lastIndexOf(const XVector* this_vector, const void* value, int64_t from)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL") ||
		ISNULL(value, "Value is NULL"))
	{
		return -1;
	}

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t size = XVector_size_base(this_vector);
	if (size == 0) return -1; // 空容器直接返回

	// 处理from参数（默认为-1表示从最后一个元素开始）
	int64_t startIndex;
	if (from < 0) {
		startIndex = (int64_t)size - 1; // 从最后一个元素开始
	}
	else {
		// 确保起始位置不超过容器范围
		startIndex = (from >= (int64_t)size) ? (int64_t)size - 1 : from;
	}

	const char* data = (const char*)XContainerDataPtr(this_vector);

	// 从起始位置开始向前查找
	for (int64_t i = startIndex; i >= 0; --i)
	{
		const void* element = &data[(size_t)i * typeSize];
		if (XContainerCompare(this_vector))
		{
			if (XContainerCompare(this_vector)(element, value)==XCompare_Equality)
				return (int64_t)i;
		}
		else if (memcmp(element, value, typeSize) == 0)
		{
			return (int64_t)i;
		}
	}

	// 未找到元素
	return -1;
}

XVector* XVector_last(const XVector* this_vector, int64_t n)
{
	return XVector_mid(this_vector,XContainerSize(this_vector)-n,-1);
}

XVector* XVector_mid(const XVector* this_vector, int64_t pos, int64_t length)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL"))
		return NULL;

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t totalSize = XVector_size_base(this_vector);
	// 创建新向量
	XVector* result = XVector_create(typeSize);
	XContainerSetDataCopyMethod(result, XContainerDataCopyMethod(this_vector));
	XContainerSetDataMoveMethod(result, XContainerDataMoveMethod(this_vector));
	XContainerSetDataDeinitMethod(result, XContainerDataDeinitMethod(this_vector));
	if (ISNULL(result, "Failed to create XVector"))
		return NULL;

	// 处理起始位置越界情况
	if (pos < 0 || (size_t)pos >= totalSize) {
		return result; // 返回空向量
	}

	// 计算实际要获取的元素数量
	size_t remaining = totalSize - (size_t)pos;
	size_t actualLength;

	if (length < 0) {
		actualLength = remaining; // length为-1时取剩余所有元素
	}
	else if ((size_t)length >= remaining) {
		actualLength = remaining; // 超出剩余数量时取剩余所有元素
	}
	else {
		actualLength = (size_t)length; // 正常情况取指定长度
	}

	// 处理长度为0的情况
	if (actualLength == 0) {
		return result; // 返回空向量
	}

	// 获取源数据指针
	const char* srcData = (const char*)XContainerDataPtr(this_vector);

	// 使用push_back逐个添加子元素
	for (size_t i = 0; i < actualLength; ++i) {
		// 计算当前元素在源向量中的位置
		const void* element = srcData + (pos + i) * typeSize;

		// 使用push_back API添加元素
		if (!XVector_push_back_base(result, element)) {
			XVector_delete_base(result);
			return NULL;
		}
	}

	return result;
}

XVector* XVector_first(const XVector* this_vector, int64_t n)
{
	return XVector_mid(this_vector, 0, n);
}

void XVector_sort_base(XVector* this_vector, XSortOrder order)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return ;
	XClassGetVirtualFunc(this_vector, EXVector_Sort, void (*)(XVector*, XSortOrder))(this_vector, order);
}
bool XVector_replace(XVector* this_vector, int64_t index, void* pvValue)
{
	if (this_vector = NULL|| index<-1 ||index>=XVector_count_base(this_vector) || pvValue == NULL)
		return false;
	void* oldValue = XVector_at_base(this_vector,index);
	if (oldValue == NULL)
		return false;
	if (XContainerDataDeinitMethod(this_vector))
		XContainerDataDeinitMethod(this_vector)(oldValue);
	if (XContainerDataCopyMethod(this_vector))
		XContainerDataCopyMethod(this_vector)(oldValue, pvValue);
	else
		memcpy(oldValue, pvValue,XContainerTypeSize(this_vector));

}
bool XVector_replace_move(XVector* this_vector, int64_t index, void* pvValue)
{
	if (this_vector = NULL || index < -1 || index >= XVector_count_base(this_vector) || pvValue == NULL)
		return false;
	void* oldValue = XVector_at_base(this_vector, index);
	if (oldValue == NULL)
		return false;
	if (XContainerDataDeinitMethod(this_vector))
		XContainerDataDeinitMethod(this_vector)(oldValue);
	if (XContainerDataMoveMethod(this_vector))
		XContainerDataMoveMethod(this_vector)(oldValue, pvValue);
	else
		memcpy(oldValue, pvValue, XContainerTypeSize(this_vector));
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
//size_t XVector_size_base(const XVector* this_vector)
//{
//	return XContainerObject_size_base(this_vector);
//}
//
//size_t XVector_capacity_base(const XVector* this_vector)
//{
//	return XContainerObject_capacity_base(this_vector);
//}
//
//void XVector_swap_base(XVector* this_vectorOne, XVector* this_vectorTwo)
//{
//	XContainerObject_swap_base(this_vectorOne,this_vectorTwo);
//}
//
//size_t XVector_typeSize_base(XVector* this_vector)
//{
//	return XContainerObject_typeSize_base(this_vector);
//}
#endif