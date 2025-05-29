#include"XString.h"
#if XString_ON
#include<string.h>
//判断是中文
bool XString_isChinese(const char c);
//返回字符串中字符数量，中文算一个
size_t XString_charNumber(const char* str);
//返回对应XVector的索引
size_t XString_XVectorNsel(const struct XString* this_XString, const size_t nSel);
//设置XString的大小，实际大小自动+1存/0
static void VXString_resize(XString* this_string, size_t len);
//尾部增加一个字符
static void VXString_push_back(XString* this_string, char c);
//尾插
static void VXString_append(XString* this_string, const char* string);
static void VXString_insert(XString* this_string, const int64_t index, const char* string);
//static void VXString_pop_front(XString* this_string);
static void VXString_pop_back(XString* this_string);
static void VXString_remove(XString* this_string, int64_t index, int64_t n);
static void VXString_erase(XString* this_string, void* LpValue);
static void VXString_clear(XString* this_string);
static bool VXString_empty(const XString* this_string);
//返回当前字符串大小
static size_t VXString_size(const XString* this_string);
// 赋值
static void VXString_assign(XString* this_string, const char* string);
// 返回字符串
static const char* VXString_data(const XString* this_string);
static int64_t VXString_find_first_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_last_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_first_not_of(const XString* this_string, const char* subStr);
static int64_t VXString_find_last_not_of(const XString* this_string, const char* subStr);
//虚函数表定义
XVtable* XStringVtable = NULL;
#if VTABLE_ISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[31];//虚函数数据
#endif
void XString_class_init()
{
	if (XStringVtable)
		return;
	void* table[] = { VXString_assign,VXString_data,
		VXString_find_first_of,VXString_find_last_of,
		VXString_find_first_not_of,VXString_find_last_not_of
	};
#if !VTABLE_ISSTACK
	XStringVtable = XVtable_new();
#else
	XStringVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XStringVtable, XVectorVtable);
	//重写的函数
	XVtable_At(XStringVtable, EXVector_Erase) = VXString_erase;
	XVtable_At(XStringVtable, EXContainerObject_Clear)=VXString_clear;
	XVtable_At(XStringVtable, EXVector_Remove) = VXString_remove;
	XVtable_At(XStringVtable, EXVector_Push_Back) = VXString_push_back;
	XVtable_At(XStringVtable, EXVector_append_Array) = VXString_append;
	XVtable_At(XStringVtable, EXVector_Insert) = VXString_insert;
	//XVtable_At(XStringVtable, EXVector_Pop_Front) = VXString_pop_front;
	XVtable_At(XStringVtable, EXVector_Pop_Back) = VXString_pop_back;
	XVtable_At(XStringVtable, EXContainerObject_IsEmpty) = VXString_empty;
	XVtable_At(XStringVtable, EXContainerObject_Size) = VXString_size;
	//追加函数
	XVtable_append_array(XStringVtable, table, sizeof(table) / sizeof(table[0]));

#if SHOWCONTAINERSIZE
	printf("XString size:%d\n", XVtable_size(XStringVtable));
#endif // SHOWCONTAINERSIZE
}
/*
bool XString_isChinese(const char c)
{
	return (c & 0x8000);//是中文;
}
size_t XString_charNumber(const char* str)
{
	size_t sum = 0;
	for (size_t i = 0; i < strlen(str); i++)
	{
		if (XString_isChinese(str[i]))
			++i;
		++sum;
	}
	return sum;
}

size_t XString_XVectorNsel(const struct XString* this_XString, const size_t nSel)
{
	if (ISNULL(this_XString, "")))
		return;
	if (nSel < 0)
		return -1;
	struct XVector* v = ((struct XString*)this_XString)->m_data;
	size_t VnSel = -1;
	for (size_t i = 0; i < XVector_getSize_base(v) - 1; i++)
	{
		char c = *((char*)XVector_at_base(v, i));
		if (XString_isChinese(c))
			++i;
		++VnSel;
		if (VnSel == nSel)
			return i;
	}
	return -1;
}

//删除索引处字符
bool XString_eraseOne(struct XString* this_XString, const int nSel)
{
	if (ISNULL(this_XString, "")))
		return false;
	if (nSel < 0)
		return false;
	struct XVector* v = ((struct XString*)this_XString)->m_data;
	size_t VnSel = XString_XVectorNsel(this_XString, nSel);
	if (VnSel == -1)
		return false;

	int offset = 0;
	if (XString_isChinese(*((char*)XVector_at_base(v, VnSel))))//是中文
		++offset;
	//XVector_erase_int(v, VnSel - offset, VnSel);
	XVector_remove_base(v, VnSel - offset, offset);
	((struct XString*)this_XString)->m_size -= 1;
	return true;
}
//尾删
void XString_pop_back(struct XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return;
	XString_eraseOne(this_XString, XString_size(this_XString) - 1);
}
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n)
{
	if (ISNULL(this_XString, "")))
		return;
	if (nSel < 0 || n <= 0)
		return;
	for (size_t i = 0; i < n; i++)
	{
		if (!XString_eraseOne(this_XString, nSel))
			break;
	}
}
//清空字符串
void XString_clear(struct XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->m_data;
	int right = XVector_getSize_base(v) - 2;
	if (right >= 0)
		//XVector_erase_int(v, 0, right);
		XVector_remove_base(v, 0, right);
	((struct XString*)this_XString)->m_size = 0;
}

//查找函数
int XString_find_first_of(const struct XString* this_XString, const char* subStr)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(subStr);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == subStr[j])
				return i;
		}
	}
	return -1;
}
int XString_find_last_of(const struct XString* this_XString, const char* subStr)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(subStr);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == subStr[j])
				return sLen - i - 1;
		}
	}
	return -1;
}
int XString_find_first_not_of(const struct XString* this_XString, const char* subStr)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(subStr);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == subStr[j])
				break;
			n++;
		}
		if (n == fLen)
			return  i;
	}
	return -1;
}
int XString_find_last_not_of(const struct XString* this_XString, const char* subStr)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(subStr);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == subStr[j])
				break;
			n++;
		}
		if (n == fLen)
			return sLen - i - 1;
	}
	return -1;
}

//尾插
void XString_append(struct XString* this_XString, const char* str)
{
	if (ISNULL(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->m_data;
	XString_insert(this_XString, XVector_getSize_base(v) - 1, str);
}
// 赋值
void XString_assign(struct XString* this_XString, const char* str)
{
	if (ISNULL(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->m_data;
	XString_clear(this_XString);
	XString_insert(this_XString, 0, str);
}
// 第索引处开始插入字符串
void XString_insert(struct XString* this_XString, const int nSel, const char* str)
{
	if (ISNULL(this_XString, "")))
		return;
	if (str == NULL || nSel < 0)
		return;
	struct XString* string = (struct XString*)this_XString;
	struct XVector* v = string->m_data;

	//XVector_insert_base(v, nSel, str, str + strlen(str) - 1);
	((struct XString*)this_XString)->m_size += XString_charNumber(str);
}

//判断函数
bool XString_isEmpty(const struct XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->m_size == 0;
}
//返回当前元素大小
int XString_size(const struct XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->m_size;
}
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_XString)
//{
//	if (isObjectNULL(this_XString, "XString_capacity"))
//		return NULL;
//	struct XString* string = (struct XString*)this_XString;
//	return XVector_getCapacity_base(string->m_data);
//}
//交换
void XString_swap(struct XString* this_XStringOne, struct XString* this_XStringTwo)
{
	if (ISNULL(this_XStringOne, "")) || ArgIsNULL(isNULLInfo(this_XStringTwo, "")))
		return NULL;
	struct XString* stringOne = (struct XString*)this_XStringOne;
	struct XString* stringTwo = (struct XString*)this_XStringTwo;
	XVector_swap_base(stringOne->m_data, stringTwo->m_data);
	XSwap(&stringOne->m_size, &stringTwo->m_size, sizeof(size_t));
}
//释放容器
void XString_free(const struct XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	XVector_free_base(string->m_data);
	XMemory_free(this_XString);
}

// 返回索引处字符
char XString_at(const XString* this_XString, int nSel)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return *((char*)XVector_at_base(string->m_data, nSel));
}
// 返回字符串
char* XString_data(const XString* this_XString)
{
	if (ISNULL(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->m_data->object.m_data;
}
*/

void VXString_resize(XString* this_string, size_t len)
{
	typedef void (*funcPtr)(XVector*, size_t);
	XVtableGetFunc(XVectorVtable, EXVector_Resize, funcPtr)(this_string,len+1);
}

void VXString_push_back(XString* this_string, char c)
{
	if (ISNULL(this_string, "")||c==0)
		return;
	char arr[] = { c,0 };
	VXString_append(this_string, arr);
}

void VXString_append(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(string, ""))
		return;
	size_t len = strlen(string);
	if(XContainerSize(this_string)+ len +1>XContainerCapacity(this_string))
		VXString_resize(this_string, VXString_size(this_string)+strlen(string));
	strcat(XContainerDataPtr(this_string), string);
	XContainerSize(this_string)+=len;
}

void VXString_insert(XString* this_string, const int64_t index, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(string, ""))
		return;
	if (index<0 || index>=XContainerSize(this_string))
		return;
	//void XVector_inserts_base(XVector* this_vector, int64_t index, void* LpValue, size_t n);
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	XVtableGetFunc(XVectorVtable, EXVector_Inserts, funcPtr)(this_string,index,string,strlen(string));
}

//void VXString_pop_front(XString* this_string)
//{
//	if (VXString_empty(this_string))
//		return;
//	typedef void (*funcPtr)(XVector*, void*);
//	XVtableGetFunc(XVectorVtable, EXVector_Push_Back, funcPtr)(this_string, "");
//}

void VXString_pop_back(XString* this_string)
{
	if (VXString_empty(this_string))
		return;
	//void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n);
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	XVtableGetFunc(XVectorVtable, EXVector_Remove, funcPtr)(this_string,XContainerSize(this_string)-2,1);
}

void VXString_remove(XString* this_string, int64_t index, int64_t n)
{
	if (VXString_empty(this_string))
		return;
	if (index < 0 || index >= XContainerSize(this_string)-1)
		return;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	XVtableGetFunc(XVectorVtable, EXVector_Remove, funcPtr)(this_string, index, n);
}

void VXString_erase(XString* this_string, void* LpValue)
{
	if (VXString_empty(this_string))
		return;
	if ((char*)XContainerDataPtr(this_string) + XContainerSize(this_string) - 1 == LpValue)
		return;
	typedef void (*funcPtr)(XVector*, void*);
	XVtableGetFunc(XVectorVtable, EXVector_Erase, funcPtr)(this_string, LpValue);
}

void VXString_clear(XString* this_string)
{
	if (ISNULL(this_string, ""))
		return;
	XContainerSize(this_string)=0;
	typedef void (*funcPtr)(XVector*, void*);
	XVtableGetFunc(XVectorVtable, EXVector_Push_Back, funcPtr)(this_string,"");
}

bool VXString_empty(const XString* this_string)
{
	return VXString_size(this_string)==0;
}

size_t VXString_size(const XString* this_string)
{
	if (ISNULL(this_string, ""))
		return 0;
	size_t len = XContainerSize(this_string);
	if (len == 0)
		return 0;
	return len -1;
}

void VXString_assign(XString* this_string, const char* string)
{
	if (ISNULL(this_string, "")|| ISNULL(string, ""))
		return;
	VXString_clear(this_string);
	VXString_append(this_string,string);
}

const char* VXString_data(const XString* this_string)
{
	if (ISNULL(this_string, ""))
		return NULL;
	return XContainerDataPtr(this_string);
}

int64_t VXString_find_first_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "")|| ISNULL(subStr, ""))
		return -1;
	char* ret = strstr(XContainerDataPtr(this_string),subStr);
	if (ret == NULL)
		return -1;
	return ret - (char*)XContainerDataPtr(this_string);
	/*char* str = XString_data(this_string);
	size_t sLen = strlen(str);
	size_t fLen = strlen(subStr);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == subStr[j])
				return i;
		}
	}*/
	//return -1;
}
static char* strrstr(const char* haystack, const char* needle) {
	size_t haystack_len = strlen(haystack);
	size_t needle_len = strlen(needle);

	if (needle_len == 0) {
		return (char*)haystack + haystack_len;
	}

	const char* last_occurrence = NULL;
	const char* current_occurrence = haystack;

	while ((current_occurrence = strstr(current_occurrence, needle)) != NULL) {
		last_occurrence = current_occurrence;
		current_occurrence += needle_len;
	}

	return (char*)last_occurrence;
}
int64_t VXString_find_last_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(subStr, ""))
		return -1;
	char* ret = strrstr(XContainerDataPtr(this_string), subStr);
	if (ret == NULL)
		return -1;
	return ret - (char*)XContainerDataPtr(this_string);
}

int64_t VXString_find_first_not_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(subStr, ""))
		return -1;
	const char* haystack=XContainerDataPtr(this_string);
	const char* needle=subStr;
	size_t haystack_len = strlen(haystack);
	size_t needle_len = strlen(needle);

	if (needle_len == 0) {
		return -1;
	}

	const char* last_occurrence = NULL;
	const char* current_occurrence = haystack;

	while ((current_occurrence = strstr(current_occurrence, needle)) != NULL) {
		if (last_occurrence != NULL && current_occurrence > last_occurrence + needle_len)
			return last_occurrence + needle_len;
		last_occurrence = current_occurrence;
		current_occurrence += needle_len;
	}

	return -1;
}

int64_t VXString_find_last_not_of(const XString* this_string, const char* subStr)
{
	if (ISNULL(this_string, "") || ISNULL(subStr, ""))
		return -1;
	int64_t index= VXString_find_last_of(this_string, subStr);
	if (index == -1)
		return 0;
	size_t len = strlen(subStr);
	if (index + len < VXString_size(this_string))
		return index + len;
	return -1;
}


#endif