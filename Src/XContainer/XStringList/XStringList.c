#include"XStringList.h"
#include"XVariant.h"
#include"XVariantTypeOps.h"
#if XStringList_ON
#include<string.h>
#include"XString.h"

XVARIANT_TYPE_OPS_DEFINE(XStringList, sizeof(XStringList), XStringList_copy_base,
	XStringList_move_base, XStringList_clear_base, XStringList_deinit_base,
	NULL, "XStringList");

XVariant* XStringList_toVariant(const XStringList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, sizeof(XStringList), XVariantType_StringList);
	if (!var)
		return NULL;
	XStringList_init((XStringList*)XVariant_data(var));
	XStringList_copy_base(XVariant_data(var), list);
	return var;
}

XVariant* XStringList_toVariant_move(XStringList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, sizeof(XStringList), XVariantType_StringList);
	if (!var)
		return NULL;
	XStringList_init((XStringList*)XVariant_data(var));
	XStringList_move_base(XVariant_data(var), list);
	return var;
}

XVariant* XStringList_toVariant_ref(XStringList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, 0, XVariantType_StringList);
	if (!var)
		return NULL;
	XVariant_setDataRef(var, list, sizeof(XStringList), XVariantType_StringList);
	return var;
}

XStringList* XStringList_fromVariant(const XVariant* var)
{
	return XStringList_create_copy(XStringList_fromVariant_ref(var));
}

XStringList* XStringList_fromVariant_ref(const XVariant* var)
{
	return (XStringList*)XVariant_toRef(var, XVariantType_StringList);
}

static bool XStringList_prepareVariant(XVariant* var)
{
	if (!var)
		return false;
	if (var->m_type != XVariantType_StringList)
	{
		XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XStringList));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XStringList);
		XStringList_init((XStringList*)var->m_data);
		var->m_type = XVariantType_StringList;
	}
	else if (!var->m_data || var->m_dataSize != sizeof(XStringList))
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XStringList));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XStringList);
		XStringList_init((XStringList*)var->m_data);
	}
	return true;
}

void XStringList_setVariant(XVariant* var, const XStringList* list)
{
	if (!list || !XStringList_prepareVariant(var))
		return;
	XStringList_copy_base(XVariant_data(var), list);
}

void XStringList_setVariant_move(XVariant* var, XStringList* list)
{
	if (!list || !XStringList_prepareVariant(var))
		return;
	XStringList_move_base(XVariant_data(var), list);
}

void XStringList_setVariant_ref(XVariant* var, XStringList* list)
{
	if (!var || !list)
		return;
	XVariant_setDataRef(var, list, sizeof(XStringList), XVariantType_StringList);
}
#if XRegularExpression_ON
#include "XRegularExpression.h"
#endif
XVtable* XStringList_class_init()
{
	return XVector_class_init();
//	XVTABLE_CREAT_DEFAULT
//		//虚函数表初始化
//#if VTABLE_ISSTACK
//		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XStringList))
//#else
//		XVTABLE_HEAP_INIT_DEFAULT
//#endif
//		//继承类
//		XVTABLE_INHERIT_DEFAULT(XVector_class_init());
//	//void* table[] = { };
//	//追加虚函数
//	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
//
//	//重写的函数
//#if SHOWCONTAINERSIZE
//	printf("XStringList size:%d\n", XVtable_size(XVTABLE_DEFAULT));
//#endif // SHOWCONTAINERSIZE
//	return XVTABLE_DEFAULT;
}
XStringList* XStringList_create()
{
	XStringList* vector=XMalloc_System(sizeof(XStringList));
	XStringList_init(vector);
	Set_Class_MemoryFree(vector, XFree_System);
	return vector;
}
XStringList* XStringList_create_copy(const XStringList* other)
{
	if (other == NULL)
		return NULL;
	XStringList* list = XStringList_create();
	if(list==NULL)
		return NULL;
	XStringList_copy_base(list,other);
	return list;
}
XStringList* XStringList_create_move(XStringList* other)
{
	if (other == NULL)
		return NULL;
	XStringList* list = XStringList_create();
	if (list == NULL)
		return NULL;
	XStringList_move_base(list, other);
	return list;
}
void XStringList_init(XStringList* strList)
{
	if (strList == NULL)
		return;
	XVector_init(strList,sizeof(XString),true);
	XClassGetVtable(strList) = XStringList_class_init();
	XContainerSetDataCopyMethod(strList, XClass_copy_base);
	XContainerSetDataMoveMethod(strList, XClass_move_base);
	XContainerSetDataDeinitMethod(strList, XClass_deinit_base);
	XContainerSetCompare(strList,XString_compare);
}
void XStringList_push_front_utf8(XStringList* strList, const char* utf8_str)
{
	XString_Init_Utf8(str,utf8_str);
	XStringList_push_front_move_base(strList, str);
	XString_deinit_base(str);
}
void XStringList_push_back_utf8(XStringList* strList, const char* utf8_str)
{
	XString_Init_Utf8(str, utf8_str);
	XStringList_push_back_move_base(strList, str);
	XString_deinit_base(str);
}
void XStringList_insert_utf8(XStringList* strList, int64_t index, const char* utf8_str)
{
	XString_Init_Utf8(str, utf8_str);
	XStringList_insert_move_base(strList,index, str);
	XString_deinit_base(str);
}

XString* XStringList_join(const XStringList* strList, const XString* separator)
{
	if (strList == NULL || separator == NULL)
		return NULL;
	if (XString_isEmpty_base(separator))
		return NULL;
	XString* str = XString_create();
	for_each_iterator(strList, XStringList, it)
	{
		XString* s = XStringList_iterator_data(&it);
		if (!XString_isEmpty_base(s))
		{
			XString_append(str, s);
			XString_append(str, separator);
		}
	}
	if (XString_endsWith(str, separator, XChar_CaseInsensitive))
	{
		XString_remove_base(str, XString_lastIndexOf(str, separator, 0, XChar_CaseInsensitive),XString_length_base(separator));
	}
	return str;
}

XString* XStringList_join_utf8(const XStringList* strList, const char* separator)
{
	if(strList==NULL|| separator==NULL)
		return NULL;
	size_t len = strlen(separator);
	if (len == 0 )
		return NULL;
	XString* temp = XString_create_utf8(separator);
	XString* str = XStringList_join(strList, temp);
	XString_delete_base(temp);
	return str;
}

#if XRegularExpression_ON

static XRegularExpression* XStringList_exactRegularExpression(
        const XRegularExpression* expression)
{
    if (!expression) return NULL;
    XString* pattern = XRegularExpression_pattern(expression);
    if (!pattern) return NULL;
    XString* anchored = XRegularExpression_anchoredPattern_2(pattern);
    XRegularExpression* result = NULL;
    if (anchored) {
        result = XRegularExpression_create();
        if (result) {
            XRegularExpression_setPattern(result, anchored);
            XRegularExpression_setPatternOptions(result,
                                                  XRegularExpression_patternOptions(expression));
        }
    }
    if (anchored) XString_delete_base(anchored);
    XString_delete_base(pattern);
    return result;
}

XStringList* XStringList_filter_regularExpression(const XStringList* strList,
                                                  const XRegularExpression* expression)
{
    if (!strList || !expression) return NULL;
    XStringList* result = XStringList_create();
    if (!result) return NULL;
    size_t count = XStringList_size_base(strList);
    for (size_t i = 0; i < count; ++i) {
        const XString* value = (const XString*)XStringList_at_base(strList, (int64_t)i);
        if (value && XString_contains_regularExpression(value, expression))
            XStringList_push_back_base(result, (void*)value);
    }
    return result;
}

int64_t XStringList_indexOf_regularExpression(const XStringList* strList,
                                              const XRegularExpression* expression,
                                              int64_t from)
{
    if (!strList || !expression) return -1;
    int64_t count = (int64_t)XStringList_size_base(strList);
    if (from < 0) from += count;
    if (from < 0) from = 0;
    XRegularExpression* exact = XStringList_exactRegularExpression(expression);
    if (!exact) return -1;
    for (int64_t i = from; i < count; ++i) {
        const XString* value = (const XString*)XStringList_at_base(strList, (int64_t)i);
        if (value && XString_contains_regularExpression(value, exact)) {
            XRegularExpression_delete_base(exact);
            return (int64_t)i;
        }
    }
    XRegularExpression_delete_base(exact);
    return -1;
}

int64_t XStringList_lastIndexOf_regularExpression(const XStringList* strList,
                                                   const XRegularExpression* expression,
                                                   int64_t from)
{
    if (!strList || !expression) return -1;
    int64_t count = (int64_t)XStringList_size_base(strList);
    if (from < 0) from += count;
    else if (from >= count) from = count - 1;
    XRegularExpression* exact = XStringList_exactRegularExpression(expression);
    if (!exact) return -1;
    for (int64_t i = from; i >= 0; --i) {
        const XString* value = (const XString*)XStringList_at_base(strList, i);
        if (value && XString_contains_regularExpression(value, exact)) {
            XRegularExpression_delete_base(exact);
            return i;
        }
    }
    XRegularExpression_delete_base(exact);
    return -1;
}

bool XStringList_replaceInStrings_regularExpression(XStringList* strList,
                                                     const XRegularExpression* expression,
                                                     const XString* after)
{
    if (!strList || !expression || !after) return false;
    size_t count = XStringList_size_base(strList);
    for (size_t i = 0; i < count; ++i) {
        XString* value = (XString*)XStringList_at_base(strList, (int64_t)i);
        if (!value || !XString_replace_regularExpression(value, expression, after)) return false;
    }
    return true;
}

#endif /* XRegularExpression_ON */


// -------------------------- Qt 6.8 对齐：字符串列表特有方法 --------------------------

/**
 * @brief 字符串列表排序比较器（大小写敏感）
 * @note qsort回调，比较两个XString值
 */
static int XStringList_sortCompare_cs(const void* a, const void* b)
{
	const XString* sa = (const XString*)a;
	const XString* sb = (const XString*)b;
	return XString_compare(sa, sb);
}

/**
 * @brief 字符串列表排序比较器（大小写不敏感）
 * @note qsort回调，使用折叠后字符比较
 */
static int XStringList_sortCompare_ci(const void* a, const void* b)
{
	const XString* sa = (const XString*)a;
	const XString* sb = (const XString*)b;
	size_t lena = XString_length_base(sa);
	size_t lenb = XString_length_base(sb);
	size_t min_len = lena < lenb ? lena : lenb;
	const XChar* da = XString_unicode(sa);
	const XChar* db = XString_unicode(sb);
	for (size_t i = 0; i < min_len; i++) {
		XChar ca = XChar_toCaseFolded(da[i]);
		XChar cb = XChar_toCaseFolded(db[i]);
		if (ca < cb) return -1;
		if (ca > cb) return 1;
	}
	if (lena < lenb) return -1;
	if (lena > lenb) return 1;
	return 0;
}

void XStringList_sort(XStringList* strList, XChar_CaseSensitivity cs)
{
	if (!strList) return;
	size_t count = XContainer_size_base(strList);
	if (count <= 1) return;
	void* data = XContainerDataAddr(strList);
	if (!data) return;
	size_t elemSize = XContainer_typeSize_base(strList);
	qsort(data, count, elemSize,
		cs == XChar_CaseInsensitive ? XStringList_sortCompare_ci : XStringList_sortCompare_cs);
}

size_t XStringList_removeDuplicates(XStringList* strList)
{
	if (!strList) return 0;
	size_t count = XContainer_size_base(strList);
	if (count <= 1) return 0;
	size_t removed = 0;
	for (size_t i = 0; i < count; ) {
		XString* si = (XString*)XStringList_at_base(strList, i);
		for (size_t j = i + 1; j < count; j++) {
			XString* sj = (XString*)XStringList_at_base(strList, j);
			if (XString_compare(si, sj) == 0) {
				XVector_remove_base(strList, (int64_t)j, 1);
				count--;
				removed++;
				j--;
			}
		}
		i++;
	}
	return removed;
}

XStringList* XStringList_filter(const XStringList* strList, const XString* str, XChar_CaseSensitivity cs)
{
	if (!strList || !str) return NULL;
	XStringList* result = XStringList_create();
	if (!result) return NULL;
	size_t count = XContainer_size_base(strList);
	for (size_t i = 0; i < count; i++) {
		XString* s = (XString*)XStringList_at_base(strList, i);
		if (XString_contains(s, str, cs)) {
			XVector_push_back_1_base(result, s);
		}
	}
	return result;
}

XStringList* XStringList_filter_utf8(const XStringList* strList, const char* utf8_str, XChar_CaseSensitivity cs)
{
	if (!strList || !utf8_str) return NULL;
	XString* temp = XString_create_utf8(utf8_str);
	if (!temp) return NULL;
	XStringList* result = XStringList_filter(strList, temp, cs);
	XString_delete_base(temp);
	return result;
}

void XStringList_replaceInStrings(XStringList* strList, const XString* before, const XString* after, XChar_CaseSensitivity cs)
{
	if (!strList || !before || !after) return;
	size_t count = XContainer_size_base(strList);
	for (size_t i = 0; i < count; i++) {
		XString* s = (XString*)XStringList_at_base(strList, i);
		XString_replace(s, before, after, cs);
	}
}

void XStringList_replaceInStrings_utf8(XStringList* strList, const char* before, const char* after, XChar_CaseSensitivity cs)
{
	if (!strList || !before || !after) return;
	XString* b = XString_create_utf8(before);
	XString* a = XString_create_utf8(after);
	if (!b || !a) {
		XString_delete_base(b);
		XString_delete_base(a);
		return;
	}
	XStringList_replaceInStrings(strList, b, a, cs);
	XString_delete_base(b);
	XString_delete_base(a);
}

bool XStringList_contains(const XStringList* strList, const XString* str, XChar_CaseSensitivity cs)
{
	if (!strList || !str) return false;
	size_t count = XContainer_size_base(strList);
	for (size_t i = 0; i < count; i++) {
		XString* s = (XString*)XStringList_at_base(strList, i);
		if (XString_equals(s, str, cs))
			return true;
	}
	return false;
}

bool XStringList_contains_utf8(const XStringList* strList, const char* utf8_str, XChar_CaseSensitivity cs)
{
	if (!strList || !utf8_str) return false;
	XString* temp = XString_create_utf8(utf8_str);
	if (!temp) return false;
	bool result = XStringList_contains(strList, temp, cs);
	XString_delete_base(temp);
	return result;
}

int64_t XStringList_indexOf(const XStringList* strList, const XString* str, int64_t from, XChar_CaseSensitivity cs)
{
	if (!strList || !str) return -1;
	size_t count = XContainer_size_base(strList);
	if (from < 0) from = 0;
	for (int64_t i = from; i < (int64_t)count; i++) {
		XString* s = (XString*)XStringList_at_base(strList, i);
		if (XString_equals(s, str, cs))
			return i;
	}
	return -1;
}

int64_t XStringList_indexOf_utf8(const XStringList* strList, const char* utf8_str, int64_t from, XChar_CaseSensitivity cs)
{
	if (!strList || !utf8_str) return -1;
	XString* temp = XString_create_utf8(utf8_str);
	if (!temp) return -1;
	int64_t result = XStringList_indexOf(strList, temp, from, cs);
	XString_delete_base(temp);
	return result;
}

int64_t XStringList_lastIndexOf(const XStringList* strList, const XString* str, int64_t from, XChar_CaseSensitivity cs)
{
	if (!strList || !str) return -1;
	size_t count = XContainer_size_base(strList);
	if (count == 0) return -1;
	if (from < 0 || from >= (int64_t)count) from = (int64_t)count - 1;
	for (int64_t i = from; i >= 0; i--) {
		XString* s = (XString*)XStringList_at_base(strList, i);
		if (XString_equals(s, str, cs))
			return i;
	}
	return -1;
}

int64_t XStringList_lastIndexOf_utf8(const XStringList* strList, const char* utf8_str, int64_t from, XChar_CaseSensitivity cs)
{
	if (!strList || !utf8_str) return -1;
	XString* temp = XString_create_utf8(utf8_str);
	if (!temp) return -1;
	int64_t result = XStringList_lastIndexOf(strList, temp, from, cs);
	XString_delete_base(temp);
	return result;
}
#endif
