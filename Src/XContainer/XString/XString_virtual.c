#include"XString.h"
#include<string.h>
//判断是中文
bool XString_isChinese(const char c);
//返回字符串中字符数量，中文算一个
size_t XString_charNumber(const char* str);
//返回对应XVector的索引
size_t XString_XVectorNsel(const struct XString* this_XString, const size_t nSel);
//设置XString的大小，实际大小自动+1存/0
void VXString_resize(XString* this_string, size_t len);
//尾部增加一个字符
void VXString_push_back(XString* this_string, char c);
//尾插
void VXString_append(XString* this_string, const char* string);
void VXString_insert(XString* this_string, const int64_t index, const char* string);
//void VXString_pop_front(XString* this_string);
void VXString_pop_back(XString* this_string);
void VXString_remove(XString* this_string, int64_t index, int64_t n);
void VXString_erase(XString* this_string, void* LpValue);
void VXString_clear(XString* this_string);
bool VXString_empty(const XString* this_string);
//返回当前字符串大小
size_t VXString_size(const XString* this_string);
// 赋值
void VXString_assign(XString* this_string, const char* string);
// 返回字符串
const char* VXString_data(const XString* this_string);

//虚函数表定义
XVtable* XStringVtable = NULL;
#if VtableIsStack
	static XVtable vtable;//虚函数类
	static void* vtable_data[27];//虚函数数据
#endif
void XString_class_init()
{
	if (XStringVtable)
		return;
	void* table[] = { VXString_assign,VXString_data
		
	};
#if !VtableIsStack
	XStringVtable = XVtable_new();
#else
	XStringVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XStringVtable, XVectorVtable);
	//重写的函数
	XVtable_At(XStringVtable, EXVector_Erase) = VXString_erase;
	XVtable_At(XStringVtable,EXVector_Clear)=VXString_clear;
	XVtable_At(XStringVtable, EXVector_Remove) = VXString_remove;
	XVtable_At(XStringVtable, EXVector_Push_Back) = VXString_push_back;
	XVtable_At(XStringVtable, EXVector_append_Array) = VXString_append;
	XVtable_At(XStringVtable, EXVector_Insert) = VXString_insert;
	//XVtable_At(XStringVtable, EXVector_Pop_Front) = VXString_pop_front;
	XVtable_At(XStringVtable, EXVector_Pop_Back) = VXString_pop_back;
	XVtable_At(XStringVtable, EXContainerObject_Empty) = VXString_empty;
	XVtable_At(XStringVtable, EXContainerObject_Size) = VXString_size;
	//追加函数
	XVtable_append_array(XStringVtable, table, sizeof(table) / sizeof(table[0]));

#if ShowContainerSize
	printf("XString size:%d\n", XVtable_size(XStringVtable));
#endif // ShowContainerSize
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
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (nSel < 0)
		return -1;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	size_t VnSel = -1;
	for (size_t i = 0; i < XVector_size(v) - 1; i++)
	{
		char c = *((char*)XVector_at(v, i));
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
	if (isNULL(isNULLInfo(this_XString, "")))
		return false;
	if (nSel < 0)
		return false;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	size_t VnSel = XString_XVectorNsel(this_XString, nSel);
	if (VnSel == -1)
		return false;

	int offset = 0;
	if (XString_isChinese(*((char*)XVector_at(v, VnSel))))//是中文
		++offset;
	//XVector_erase_int(v, VnSel - offset, VnSel);
	XVector_remove(v, VnSel - offset, offset);
	((struct XString*)this_XString)->_size -= 1;
	return true;
}
//尾删
void XString_pop_back(struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	XString_eraseOne(this_XString, XString_size(this_XString) - 1);
}
//删除索引处开始的n个字符
void XString_erase(struct XString* this_XString, const int nSel, const int n)
{
	if (isNULL(isNULLInfo(this_XString, "")))
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
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	int right = XVector_size(v) - 2;
	if (right >= 0)
		//XVector_erase_int(v, 0, right);
		XVector_remove(v, 0, right);
	((struct XString*)this_XString)->_size = 0;
}

//查找函数
int XString_find_first_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == find[j])
				return i;
		}
	}
	return -1;
}
int XString_find_last_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == find[j])
				return sLen - i - 1;
		}
	}
	return -1;
}
int XString_find_first_not_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[i] == find[j])
				break;
			n++;
		}
		if (n == fLen)
			return  i;
	}
	return -1;
}
int XString_find_last_not_of(const struct XString* this_XString, const char* find)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	char* str = XString_data(this_XString);
	size_t sLen = strlen(str);
	size_t fLen = strlen(find);
	for (size_t i = 0; i < sLen; i++)
	{
		size_t n = 0;
		for (size_t j = 0; j < fLen; j++)
		{
			if (str[sLen - i - 1] == find[j])
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
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	XString_insert(this_XString, XVector_size(v) - 1, str);
}
// 赋值
void XString_assign(struct XString* this_XString, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	struct XVector* v = ((struct XString*)this_XString)->_data;
	XString_clear(this_XString);
	XString_insert(this_XString, 0, str);
}
// 第索引处开始插入字符串
void XString_insert(struct XString* this_XString, const int nSel, const char* str)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return;
	if (str == NULL || nSel < 0)
		return;
	struct XString* string = (struct XString*)this_XString;
	struct XVector* v = string->_data;

	//XVector_insert(v, nSel, str, str + strlen(str) - 1);
	((struct XString*)this_XString)->_size += XString_charNumber(str);
}

//判断函数
bool XString_empty(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_size == 0;
}
//返回当前元素大小
int XString_size(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_size;
}
////返回当前容器的最大容量
//int XString_capacity(const struct XString* this_XString)
//{
//	if (isObjectNULL(this_XString, "XString_capacity"))
//		return NULL;
//	struct XString* string = (struct XString*)this_XString;
//	return XVector_capacity(string->_data);
//}
//交换
void XString_swap(struct XString* this_XStringOne, struct XString* this_XStringTwo)
{
	if (isNULL(isNULLInfo(this_XStringOne, "")) || isNULL(isNULLInfo(this_XStringTwo, "")))
		return NULL;
	struct XString* stringOne = (struct XString*)this_XStringOne;
	struct XString* stringTwo = (struct XString*)this_XStringTwo;
	XVector_swap(stringOne->_data, stringTwo->_data);
	XSwap(&stringOne->_size, &stringTwo->_size, sizeof(size_t));
}
//释放容器
void XString_free(const struct XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	XVector_free(string->_data);
	free(this_XString);
}

// 返回索引处字符
char XString_at(const XString* this_XString, int nSel)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return *((char*)XVector_at(string->_data, nSel));
}
// 返回字符串
char* XString_data(const XString* this_XString)
{
	if (isNULL(isNULLInfo(this_XString, "")))
		return NULL;
	struct XString* string = (struct XString*)this_XString;
	return string->_data->object._data;
}
*/

void VXString_resize(XString* this_string, size_t len)
{
	typedef void (*funcPtr)(XVector*, size_t);
	VtableFunc(XVectorVtable, EXVector_Resize, funcPtr)(this_string,len+1);
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
	if(ContainerSize(this_string)+ len +1>ContainerCapacity(this_string))
		VXString_resize(this_string, VXString_size(this_string)+strlen(string));
	strcat(ContainerDataPtr(this_string), string);
	ContainerSize(this_string)+=len;
}

void VXString_insert(XString* this_string, const int64_t index, const char* string)
{
	if (ISNULL(this_string, "") || ISNULL(string, ""))
		return;
	if (index<0 || index>=ContainerSize(this_string))
		return;
	//void XVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n);
	typedef void (*funcPtr)(XVector*, int64_t, void*, size_t);
	VtableFunc(XVectorVtable, EXVector_Inserts, funcPtr)(this_string,index,string,strlen(string));
}

//void VXString_pop_front(XString* this_string)
//{
//	if (VXString_empty(this_string))
//		return;
//	typedef void (*funcPtr)(XVector*, void*);
//	VtableFunc(XVectorVtable, EXVector_Push_Back, funcPtr)(this_string, "");
//}

void VXString_pop_back(XString* this_string)
{
	if (VXString_empty(this_string))
		return;
	//void XVector_remove(XVector* this_vector, int64_t index, int64_t n);
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	VtableFunc(XVectorVtable, EXVector_Remove, funcPtr)(this_string,ContainerSize(this_string)-2,1);
}

void VXString_remove(XString* this_string, int64_t index, int64_t n)
{
	if (VXString_empty(this_string))
		return;
	if (index < 0 || index >= ContainerSize(this_string)-1)
		return;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	VtableFunc(XVectorVtable, EXVector_Remove, funcPtr)(this_string, index, n);
}

void VXString_erase(XString* this_string, void* LpValue)
{
	if (VXString_empty(this_string))
		return;
	if ((char*)ContainerDataPtr(this_string) + ContainerSize(this_string) - 1 == LpValue)
		return;
	typedef void (*funcPtr)(XVector*, void*);
	VtableFunc(XVectorVtable, EXVector_Erase, funcPtr)(this_string, LpValue);
}

void VXString_clear(XString* this_string)
{
	if (ISNULL(this_string, ""))
		return;
	ContainerSize(this_string)=0;
	typedef void (*funcPtr)(XVector*, void*);
	VtableFunc(XVectorVtable, EXVector_Push_Back, funcPtr)(this_string,"");
}

bool VXString_empty(const XString* this_string)
{
	return VXString_size(this_string)==0;
}

size_t VXString_size(const XString* this_string)
{
	if (ISNULL(this_string, ""))
		return 0;
	size_t len = ContainerSize(this_string);
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
	return ContainerDataPtr(this_string);
}
