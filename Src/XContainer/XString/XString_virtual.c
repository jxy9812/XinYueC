#include"XString.h"
#if XString_ON
#include<string.h>
static void VXString_detach(XString* str);
static size_t utf8_to_unicode(const char* utf8, uint32_t* code_point);
static size_t unicode_to_utf8(uint32_t code_point, char* utf8);
static size_t unicode_to_utf16(uint32_t code_point, uint16_t* utf16);
static uint32_t utf16_to_unicode(const uint16_t** utf16);
//设置XString的大小
static bool VXString_resize(XString* this_string, size_t len);
//尾部增加一个字符
static void VXString_push_back(XString* this_string, char c);
//尾插
static void VXString_append(XString* this_string, const char* string);
static void VXString_insert(XString* this_string, const int64_t index, const char* string);
static void VXString_pop_back(XString* this_string);
static void VXString_remove(XString* this_string, int64_t index, int64_t n);
static void VXString_erase(XString* this_string, void* pvValue);
static void VXString_clear(XString* this_string);
static bool VXString_empty(const XString* this_string);
//返回当前字符串大小
static size_t VXString_size(const XString* this_string);
// 赋值
static void VXString_assign(XString* this_string, const char* string);
static int64_t VXString_find_first_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_last_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_first_not_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_last_not_of(const XString* this_string, const char* subStr);
XVtable* XString_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XSTRING_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XVector_class_init());
	void* table[] = { VXString_assign,
		VXString_find_first_of,VXString_find_last_of,
		VXString_find_first_not_of,VXString_find_last_not_of
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	//重写的函数
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_Erase, VXString_erase);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXString_clear);
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_Remove, VXString_remove);
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_Push_Back_Copy, VXString_push_back);
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_append_Array_Copy, VXString_append);
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_Resize, VXString_resize);
	//XVTABLE_OVERLOAD_DEFAULT(EXVector_Pop_Back, VXString_pop_back);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_IsEmpty, VXString_empty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Size, VXString_size);

#if SHOWCONTAINERSIZE
	printf("XString size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

void VXString_detach(XString* str)
{
	if (str==NULL || XContainerDataPtr(str) ==NULL|| !str->m_is_shared)
		return;

	// 如果只有一个引用，不需要复制
	if (*str->m_ref_count == 1) 
	{
		str->m_is_shared = false;
		XMemory_free(str->m_ref_count);
		str->m_ref_count = NULL;
		return;
	}

	// 复制数据
	uint16_t* new_data = (uint16_t*)XMemory_malloc(XContainerCapacity(str)* sizeof(uint16_t));
	if (!new_data)
		return;
	memcpy(new_data, XContainerDataPtr(str), XContainerSize(str) * sizeof(uint16_t));

	// 更新引用计数
	(*str->m_ref_count)--;

	// 更新当前对象
	XContainerDataPtr(str) = new_data;
	str->m_is_shared = false;
	free(str->m_ref_count);
	str->m_ref_count = NULL;
}

size_t utf8_to_unicode(const char* utf8, uint32_t* code_point)
{
	if (!utf8 || !code_point) return 0;

	uint8_t c = (uint8_t)*utf8;

	if ((c & 0x80) == 0) {
		// 1字节：0xxxxxxx
		*code_point = c;
		return 1;
	}
	else if ((c & 0xE0) == 0xC0) {
		// 2字节：110xxxxx 10xxxxxx
		if (!utf8[1]) return 0;

		*code_point = ((c & 0x1F) << 6) | (utf8[1] & 0x3F);
		return 2;
	}
	else if ((c & 0xF0) == 0xE0) {
		// 3字节：1110xxxx 10xxxxxx 10xxxxxx
		if (!utf8[1] || !utf8[2]) return 0;

		*code_point = ((c & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
		return 3;
	}
	else if ((c & 0xF8) == 0xF0) {
		// 4字节：11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		if (!utf8[1] || !utf8[2] || !utf8[3]) return 0;

		*code_point = ((c & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) |
			((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
		return 4;
	}

	// 无效的UTF-8序列
	return 0;
}

size_t unicode_to_utf8(uint32_t code_point, char* utf8)
{
	if (!utf8) return 0;

	if (code_point <= 0x7F) {
		utf8[0] = (char)code_point;
		return 1;
	}
	else if (code_point <= 0x7FF) {
		utf8[0] = (char)(0xC0 | (code_point >> 6));
		utf8[1] = (char)(0x80 | (code_point & 0x3F));
		return 2;
	}
	else if (code_point <= 0xFFFF) {
		utf8[0] = (char)(0xE0 | (code_point >> 12));
		utf8[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
		utf8[2] = (char)(0x80 | (code_point & 0x3F));
		return 3;
	}
	else if (code_point <= 0x10FFFF) {
		utf8[0] = (char)(0xF0 | (code_point >> 18));
		utf8[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
		utf8[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
		utf8[3] = (char)(0x80 | (code_point & 0x3F));
		return 4;
	}

	// 无效的Unicode码点
	return 0;
}

size_t unicode_to_utf16(uint32_t code_point, uint16_t* utf16)
{
	if (!utf16) return 0;

	if (code_point <= 0xFFFF) {
		utf16[0] = (uint16_t)code_point;
		return 1;
	}
	else if (code_point <= 0x10FFFF) {
		// 转换为代理对
		code_point -= 0x10000;
		utf16[0] = (uint16_t)(UTF16_HIGH_SURROGATE_START + (code_point >> 10));
		utf16[1] = (uint16_t)(UTF16_LOW_SURROGATE_START + (code_point & 0x3FF));
		return 2;
	}

	// 无效的Unicode码点
	return 0;
}

uint32_t utf16_to_unicode(const uint16_t** utf16)
{
	if (!utf16 || !*utf16) return 0;

	uint16_t high = **utf16;

	// 检查是否为高代理项
	if (high >= UTF16_HIGH_SURROGATE_START && high <= UTF16_HIGH_SURROGATE_END) {
		(*utf16)++;
		uint16_t low = **utf16;

		// 检查是否为低代理项
		if (low >= UTF16_LOW_SURROGATE_START && low <= UTF16_LOW_SURROGATE_END) {
			(*utf16)++;
			return 0x10000 + ((high - UTF16_HIGH_SURROGATE_START) << 10) +
				(low - UTF16_LOW_SURROGATE_START);
		}
	}

	// 不是代理对，返回单个码点
	(*utf16)++;
	return high;
}

bool VXString_resize(XString* this_string, size_t capacity)
{
	if (!this_string || capacity <= XContainerCapacity(this_string))
		return;

	VXString_detach(this_string);

	uint16_t* new_data = NULL;
	if(!XMemory_realloc_isNULL())
	{
		new_data = (uint16_t*)XMemory_realloc(XContainerDataPtr(this_string), capacity * sizeof(uint16_t));
	}
	else
	{
		new_data = (uint16_t*)XMemory_malloc(capacity * sizeof(uint16_t));
		memcpy(new_data, XContainerDataPtr(this_string), XContainerSize(this_string) * sizeof(uint16_t));
		XMemory_free(XContainerDataPtr(this_string));
	}
	if (new_data)
	{
		XContainerDataPtr(this_string) = new_data;
		XContainerCapacity(this_string) = capacity;
	}
}

void VXString_push_back(XString* this_string, char c)
{

}

void VXString_append(XString* this_string, const char* utf8_str)
{

}

void VXString_insert(XString* this_string, const int64_t index, const char* string)
{
	
}

void VXString_pop_front(XString* this_string)
{
	
}

void VXString_pop_back(XString* this_string)
{
	
}

void VXString_remove(XString* this_string, int64_t index, int64_t n)
{
	
}

void VXString_erase(XString* this_string, void* pvValue)
{
	
}

void VXString_clear(XString* this_string)
{
	
}

bool VXString_empty(const XString* this_string)
{
	return VXString_size(this_string)==0;
}

size_t VXString_size(const XString* this_string)
{

}

void VXString_assign(XString* this_string, const char* string)
{

}

int64_t VXString_find_first_of(const XString* this_string, const char* subStr)
{
}
//static char* strrstr(const char* haystack, const char* needle) {
//	size_t haystack_len = strlen(haystack);
//	size_t needle_len = strlen(needle);
//
//	if (needle_len == 0) {
//		return (char*)haystack + haystack_len;
//	}
//
//	const char* last_occurrence = NULL;
//	const char* current_occurrence = haystack;
//
//	while ((current_occurrence = strstr(current_occurrence, needle)) != NULL) {
//		last_occurrence = current_occurrence;
//		current_occurrence += needle_len;
//	}
//
//	return (char*)last_occurrence;
//}
int64_t VXString_find_last_of(const XString* this_string, const char* subStr)
{

}

int64_t VXString_find_first_not_of(const XString* this_string, const char* subStr)
{

}

int64_t VXString_find_last_not_of(const XString* this_string, const char* subStr)
{
	
}


#endif