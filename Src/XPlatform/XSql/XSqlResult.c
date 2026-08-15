/**
 * @file       XSqlResult.c
 * @brief      SQL 查询结果抽象类实现。
 */
#include "XSqlResult.h"

#include <string.h>

static XVariant* VXSqlResult_data(XSqlResult* result, int field);
static bool VXSqlResult_isNull(XSqlResult* result, int field);
static bool VXSqlResult_reset(XSqlResult* result, const XString* query);
static bool VXSqlResult_fetch(XSqlResult* result, int index);
static bool VXSqlResult_fetchNext(XSqlResult* result);
static bool VXSqlResult_fetchPrevious(XSqlResult* result);
static bool VXSqlResult_fetchFirst(XSqlResult* result);
static bool VXSqlResult_fetchLast(XSqlResult* result);
static int VXSqlResult_size(XSqlResult* result);
static int VXSqlResult_numRowsAffected(XSqlResult* result);
static XSqlRecord* VXSqlResult_record(XSqlResult* result);
static XVariant* VXSqlResult_lastInsertId(XSqlResult* result);
static bool VXSqlResult_exec(XSqlResult* result);
static bool VXSqlResult_prepare(XSqlResult* result, const XString* query);
static bool VXSqlResult_savePrepare(XSqlResult* result, const XString* query);
static void VXSqlResult_bindValuePos(XSqlResult* result, int position,
                                     const XVariant* value, XSqlParamType type);
static void VXSqlResult_bindValueName(XSqlResult* result, const XString* name,
                                      const XVariant* value, XSqlParamType type);
static void VXSqlResult_setAt(XSqlResult* result, int at);
static void VXSqlResult_setActive(XSqlResult* result, bool active);
static void VXSqlResult_setLastError(XSqlResult* result, const XSqlError* error);
static void VXSqlResult_setQuery(XSqlResult* result, const XString* query);
static void VXSqlResult_setSelect(XSqlResult* result, bool select);
static void VXSqlResult_setForwardOnly(XSqlResult* result, bool forwardOnly);
static bool VXSqlResult_execBatch(XSqlResult* result, XSqlBatchExecutionMode mode);
static void VXSqlResult_detachFromResultSet(XSqlResult* result);
static void VXSqlResult_setNumericalPrecisionPolicy(XSqlResult* result,
                                                    XSqlNumericalPrecisionPolicy policy);
static bool VXSqlResult_nextResult(XSqlResult* result);
static void* VXSqlResult_handle(XSqlResult* result);
static void VXSqlResult_copy(XSqlResult* dest, const XSqlResult* src);
static void VXSqlResult_move(XSqlResult* dest, XSqlResult* src);
static void VXSqlResult_deinit(XSqlResult* result);

XVtable* XSqlResult_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSqlResult)
    XVTABLE_INHERIT_XCLASS(XClass);
    void* table[] = {
        VXSqlResult_data, VXSqlResult_isNull, VXSqlResult_reset,
        VXSqlResult_fetch, VXSqlResult_fetchNext, VXSqlResult_fetchPrevious,
        VXSqlResult_fetchFirst, VXSqlResult_fetchLast, VXSqlResult_size,
        VXSqlResult_numRowsAffected, VXSqlResult_record, VXSqlResult_lastInsertId,
        VXSqlResult_exec, VXSqlResult_prepare, VXSqlResult_savePrepare,
        VXSqlResult_bindValuePos, VXSqlResult_bindValueName,
        VXSqlResult_setAt, VXSqlResult_setActive, VXSqlResult_setLastError,
        VXSqlResult_setQuery, VXSqlResult_setSelect, VXSqlResult_setForwardOnly,
        VXSqlResult_execBatch, VXSqlResult_detachFromResultSet,
        VXSqlResult_setNumericalPrecisionPolicy, VXSqlResult_nextResult,
        VXSqlResult_handle
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlResult_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlResult_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlResult_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlResult_init(XSqlResult* result, const XSqlDriver* driver)
{
    if (!result) return;
    memset(((unsigned char*)result) + sizeof(XClass), 0, sizeof(*result) - sizeof(XClass));
    XClass_init((XClass*)result);
    XClassSetVtable(result, XSqlResult);
    result->m_driver = driver;
    XSqlError_init(&result->m_lastError);
    XSqlRecord_init(&result->m_record);
    XVariant_init(&result->m_lastInsertId, NULL, 0, XVariantType_NULL);
    result->m_at = XSqlLocation_BeforeFirstRow;
    result->m_size = -1;
    result->m_numRowsAffected = -1;
    result->m_positionalBindingEnabled = true;
    result->m_precisionPolicy = XSqlNumericalPrecisionPolicy_LowPrecisionDouble;
}

XSqlResult* XSqlResult_create_ex(XMemoryType memory, const XSqlDriver* driver)
{
    XSqlResult* result = (XSqlResult*)XMemory_malloc(sizeof(XSqlResult), memory);
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));
    XSqlResult_init(result, driver);
    Set_Class_Memory(result, memory); Set_Class_IsHeap(result, true);
    return result;
}

static void xsql_result_assign_string(XString** target, const XString* source)
{
    if (!target || *target == source) return;
    if (*target) {
        XString_delete_base(*target);
        *target = NULL;
    }
    if (source) *target = XString_create_copy(source);
}

static void xsql_result_clear_bindings(XSqlResult* result)
{
    if (!result) return;
    for (size_t i = 0; i < result->m_boundCount; ++i) {
        if (result->m_boundValues && result->m_boundValues[i]) XVariant_delete_base(result->m_boundValues[i]);
        if (result->m_boundNames && result->m_boundNames[i]) XString_delete_base(result->m_boundNames[i]);
    }
    if (result->m_boundValues) XFree_System(result->m_boundValues);
    if (result->m_boundTypes) XFree_System(result->m_boundTypes);
    if (result->m_boundNames) XFree_System(result->m_boundNames);
    result->m_boundValues = NULL;
    result->m_boundTypes = NULL;
    result->m_boundNames = NULL;
    result->m_boundCount = 0;
    result->m_boundCapacity = 0;
    result->m_bindCount = 0;
}

static bool xsql_result_reserve_bindings(XSqlResult* result, size_t wanted)
{
    if (wanted <= result->m_boundCapacity) return true;
    size_t capacity = result->m_boundCapacity ? result->m_boundCapacity * 2 : 4;
    while (capacity < wanted) capacity *= 2;
    XVariant** values = (XVariant**)XRealloc_System(result->m_boundValues, capacity * sizeof(XVariant*));
    if (!values) return false;
    XSqlParamType* types = (XSqlParamType*)XRealloc_System(result->m_boundTypes, capacity * sizeof(XSqlParamType));
    if (!types) return false;
    XString** names = (XString**)XRealloc_System(result->m_boundNames, capacity * sizeof(XString*));
    if (!names) return false;
    for (size_t i = result->m_boundCapacity; i < capacity; ++i) {
        values[i] = NULL;
        types[i] = XSqlParamType_In;
        names[i] = NULL;
    }
    result->m_boundValues = values;
    result->m_boundTypes = types;
    result->m_boundNames = names;
    result->m_boundCapacity = capacity;
    return true;
}

static bool xsql_result_is_bind_name_char(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
        || (value >= '0' && value <= '9') || value == '_' || value == '$';
}

static bool xsql_result_store_value(XSqlResult* result, size_t position,
                                    const XVariant* value, XSqlParamType type)
{
    XVariant* copy;
    if (!result || !value || position >= result->m_boundCount) return false;
    copy = XVariant_create_copy(value);
    if (!copy) return false;
    if (result->m_boundValues[position]) XVariant_delete_base(result->m_boundValues[position]);
    result->m_boundValues[position] = copy;
    result->m_boundTypes[position] = type;
    return true;
}

static bool xsql_result_append_placeholder(XSqlResult* result, XString* name)
{
    size_t position;
    if (!result) {
        if (name) XString_delete_base(name);
        return false;
    }
    position = result->m_boundCount;
    if (!xsql_result_reserve_bindings(result, position + 1)) {
        if (name) XString_delete_base(name);
        return false;
    }
    result->m_boundValues[position] = XVariant_create_null();
    result->m_boundTypes[position] = XSqlParamType_In;
    result->m_boundNames[position] = name;
    result->m_boundCount = position + 1;
    return result->m_boundValues[position] != NULL;
}

static void xsql_result_parse_bindings(XSqlResult* result, const XString* query)
{
    const char* text;
    size_t length;
    size_t index;
    size_t positional = 0;
    char quote = 0;
    bool named = false;
    if (!result || !query) return;
    text = XString_toUtf8(query);
    length = XString_toUtf8_length(query);
    for (index = 0; index < length; ++index) {
        char current = text[index];
        if (quote) {
            if (current == '\\' && index + 1 < length) {
                ++index;
            } else if (current == quote) {
                if (index + 1 < length && text[index + 1] == quote) ++index;
                else quote = 0;
            }
            continue;
        }
        if (current == '\'' || current == '"' || current == '`') {
            quote = current;
        } else if (current == '?' && result->m_positionalBindingEnabled) {
            if (!xsql_result_append_placeholder(result, NULL)) return;
            ++positional;
        } else if ((current == ':' || current == '@' || current == '$')
                   && (current != ':' || index == 0 || text[index - 1] != ':')
                   && index + 1 < length && xsql_result_is_bind_name_char(text[index + 1])) {
            size_t begin = index;
            XString* name;
            ++index;
            while (index + 1 < length && xsql_result_is_bind_name_char(text[index + 1])) ++index;
            name = XString_create_with_length_utf8(text + begin, index - begin + 1);
            if (!xsql_result_append_placeholder(result, name)) return;
            named = true;
        }
    }
    if (named) result->m_bindingSyntax = XSqlBindingSyntax_Named;
    else if (positional > 0) result->m_bindingSyntax = XSqlBindingSyntax_Positional;
}

static int xsql_result_find_name(const XSqlResult* result, const char* name)
{
    if (!result || !name) return -1;
    for (size_t i = 0; i < result->m_boundCount; ++i) {
        if (result->m_boundNames[i] && XString_equals_utf8(result->m_boundNames[i], name, XChar_CaseSensitive))
            return (int)i;
    }
    return -1;
}

static bool xsql_result_set_binding(XSqlResult* result, int position,
                                    const XString* name, const XVariant* value,
                                    XSqlParamType type)
{
    if (!result || !value || position < 0) return false;
    if (!xsql_result_reserve_bindings(result, (size_t)position + 1)) return false;
    while (result->m_boundCount <= (size_t)position) {
        size_t index = result->m_boundCount++;
        result->m_boundValues[index] = XVariant_create_null();
        result->m_boundTypes[index] = XSqlParamType_In;
        result->m_boundNames[index] = NULL;
    }
    if (!xsql_result_store_value(result, (size_t)position, value, type)) return false;
    if (result->m_boundNames[position]) XString_delete_base(result->m_boundNames[position]);
    result->m_boundNames[position] = name ? XString_create_copy(name) : NULL;
    return result->m_boundValues[position] != NULL;
}

static void VXSqlResult_deinit(XSqlResult* result)
{
    if (!result) return;
    xsql_result_clear_bindings(result);
    if (result->m_lastQuery) XString_delete_base(result->m_lastQuery);
    if (result->m_executedQuery) XString_delete_base(result->m_executedQuery);
    result->m_lastQuery = NULL;
    result->m_executedQuery = NULL;
    XSqlError_deinit_base(&result->m_lastError);
    XSqlRecord_deinit_base(&result->m_record);
    XVariant_deinit_base(&result->m_lastInsertId);
    XClass_Deinit_Parent(XClass, result);
}

static void VXSqlResult_copy(XSqlResult* dest, const XSqlResult* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlResult_init(dest, src->m_driver);
    dest->m_driver = src->m_driver;
    XSqlError_copy_base(&dest->m_lastError, &src->m_lastError);
    XSqlRecord_copy_base(&dest->m_record, &src->m_record);
    XVariant_copy_base(&dest->m_lastInsertId, &src->m_lastInsertId);
    xsql_result_assign_string(&dest->m_lastQuery, src->m_lastQuery);
    xsql_result_assign_string(&dest->m_executedQuery, src->m_executedQuery);
    xsql_result_clear_bindings(dest);
    for (size_t i = 0; i < src->m_boundCount; ++i)
        xsql_result_set_binding(dest, (int)i, src->m_boundNames[i], src->m_boundValues[i], src->m_boundTypes[i]);
    dest->m_at = src->m_at;
    dest->m_size = src->m_size;
    dest->m_numRowsAffected = src->m_numRowsAffected;
    dest->m_active = src->m_active;
    dest->m_select = src->m_select;
    dest->m_forwardOnly = src->m_forwardOnly;
    dest->m_positionalBindingEnabled = src->m_positionalBindingEnabled;
    dest->m_bindingSyntax = src->m_bindingSyntax;
    dest->m_precisionPolicy = src->m_precisionPolicy;
    dest->m_handle = src->m_handle;
}

int XSqlResult_at(const XSqlResult* result) { return result ? result->m_at : XSqlLocation_BeforeFirstRow; }
XString* XSqlResult_lastQuery(const XSqlResult* result) { return result && result->m_lastQuery ? XString_create_copy(result->m_lastQuery) : XString_create(); }
XString* XSqlResult_executedQuery(const XSqlResult* result) { return result && result->m_executedQuery ? XString_create_copy(result->m_executedQuery) : XString_create(); }
XSqlError* XSqlResult_lastError(const XSqlResult* result) { return result ? XSqlError_create_copy(&result->m_lastError) : XSqlError_create(NULL, NULL, XSqlErrorType_UnknownError, NULL); }
bool XSqlResult_isValid(const XSqlResult* result)
{
    /* QSqlResult::isValid() is a cursor-position check; active state is
     * intentionally independent from validity in the Qt API. */
    return result && result->m_at != XSqlLocation_BeforeFirstRow
        && result->m_at != XSqlLocation_AfterLastRow;
}
bool XSqlResult_isActive(const XSqlResult* result) { return result && result->m_active; }
bool XSqlResult_isSelect(const XSqlResult* result) { return result && result->m_select; }
bool XSqlResult_isForwardOnly(const XSqlResult* result) { return result && result->m_forwardOnly; }
const XSqlDriver* XSqlResult_driver(const XSqlResult* result) { return result ? result->m_driver : NULL; }

void XSqlResult_setAt_base(XSqlResult* result, int at) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetAt, void(*)(XSqlResult*, int))(result, at); }
void XSqlResult_setActive_base(XSqlResult* result, bool active) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetActive, void(*)(XSqlResult*, bool))(result, active); }
void XSqlResult_setLastError_base(XSqlResult* result, const XSqlError* error) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetLastError, void(*)(XSqlResult*, const XSqlError*))(result, error); }
void XSqlResult_setQuery_base(XSqlResult* result, const XString* query) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetQuery, void(*)(XSqlResult*, const XString*))(result, query); }
void XSqlResult_setSelect_base(XSqlResult* result, bool select) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetSelect, void(*)(XSqlResult*, bool))(result, select); }
void XSqlResult_setForwardOnly_base(XSqlResult* result, bool forwardOnly) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetForwardOnly, void(*)(XSqlResult*, bool))(result, forwardOnly); }
bool XSqlResult_exec_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_Exec, bool(*)(XSqlResult*))(result); }
bool XSqlResult_prepare_base(XSqlResult* result, const XString* query) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_Prepare, bool(*)(XSqlResult*, const XString*))(result, query); }
bool XSqlResult_savePrepare_base(XSqlResult* result, const XString* query) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_SavePrepare, bool(*)(XSqlResult*, const XString*))(result, query); }
void XSqlResult_bindValue_base(XSqlResult* result, int position, const XVariant* value, XSqlParamType type) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_BindValuePos, void(*)(XSqlResult*, int, const XVariant*, XSqlParamType))(result, position, value, type); }
void XSqlResult_bindValue_2_base(XSqlResult* result, const XString* name, const XVariant* value, XSqlParamType type) { if (result && name && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_BindValueName, void(*)(XSqlResult*, const XString*, const XVariant*, XSqlParamType))(result, name, value, type); }
void XSqlResult_bindValue_utf8_base(XSqlResult* result, const char* name, const XVariant* value, XSqlParamType type) { XString* key = name ? XString_create_utf8(name) : NULL; if (result && key && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_BindValueName, void(*)(XSqlResult*, const XString*, const XVariant*, XSqlParamType))(result, key, value, type); if (key) XString_delete_base(key); }
void XSqlResult_addBindValue(XSqlResult* result, const XVariant* value, XSqlParamType type) { if (result) { XSqlResult_bindValue_base(result, (int)result->m_bindCount, value, type); ++result->m_bindCount; } }

XVariant* XSqlResult_boundValue_utf8(const XSqlResult* result, const char* name) { return XSqlResult_boundValue(result, xsql_result_find_name(result, name)); }
XVariant* XSqlResult_boundValue_2(const XSqlResult* result, const XString* name) { return XSqlResult_boundValue(result, xsql_result_find_name(result, name ? XString_toUtf8(name) : NULL)); }
XVariant* XSqlResult_boundValue(const XSqlResult* result, int position) { return result && position >= 0 && (size_t)position < result->m_boundCount && result->m_boundValues[position] ? XVariant_create_copy(result->m_boundValues[position]) : XVariant_create_null(); }
XVariantList* XSqlResult_boundValues(const XSqlResult* result) { XVariantList* list = XVariantList_create(); if (list && result) for (size_t i = 0; i < result->m_boundCount; ++i) XVariantList_push_back_base(list, result->m_boundValues[i]); return list; }
XStringList* XSqlResult_boundValueNames(const XSqlResult* result) { XStringList* list = XStringList_create(); if (list && result) for (size_t i = 0; i < result->m_boundCount; ++i) if (result->m_boundNames[i]) XStringList_push_back_base(list, result->m_boundNames[i]); return list; }
XString* XSqlResult_boundValueName(const XSqlResult* result, int position)
{
    if (!result || position < 0) return XString_create();
    if ((size_t)position < result->m_boundCount && result->m_boundNames[position])
        return XString_create_copy(result->m_boundNames[position]);
    return XString_create_fmt_utf8(":%d", position);
}
XSqlParamType XSqlResult_bindValueType_utf8(const XSqlResult* result, const char* name) { return XSqlResult_bindValueType(result, xsql_result_find_name(result, name)); }
XSqlParamType XSqlResult_bindValueType(const XSqlResult* result, int position) { return result && position >= 0 && (size_t)position < result->m_boundCount ? result->m_boundTypes[position] : XSqlParamType_In; }
int XSqlResult_boundValueCount(const XSqlResult* result) { return result ? (int)result->m_boundCount : 0; }
bool XSqlResult_hasOutValues(const XSqlResult* result) { if (!result) return false; for (size_t i = 0; i < result->m_boundCount; ++i) if (result->m_boundTypes[i] & XSqlParamType_Out) return true; return false; }
XSqlBindingSyntax XSqlResult_bindingSyntax(const XSqlResult* result) { return result ? result->m_bindingSyntax : XSqlBindingSyntax_Positional; }

static XVariant* VXSqlResult_data(XSqlResult* result, int field) { (void)field; return result && result->m_at >= 0 ? XSqlRecord_value(&result->m_record, field) : XVariant_create_null(); }
static bool VXSqlResult_isNull(XSqlResult* result, int field) { XVariant* value = VXSqlResult_data(result, field); bool null = !value || !XVariant_isValid(value); if (value) XVariant_delete_base(value); return null; }
static bool VXSqlResult_reset(XSqlResult* result, const XString* query) { XSqlResult_setQuery_base(result, query); return XSqlResult_exec_base(result); }
static bool VXSqlResult_fetch(XSqlResult* result, int index) { if (!result || index < 0 || (result->m_size >= 0 && index >= result->m_size)) { if (result) result->m_at = XSqlLocation_AfterLastRow; return false; } result->m_at = index; return true; }
static bool VXSqlResult_fetchNext(XSqlResult* result) { return result ? XSqlResult_fetch_base(result, result->m_at + 1) : false; }
static bool VXSqlResult_fetchPrevious(XSqlResult* result) { return result ? XSqlResult_fetch_base(result, result->m_at - 1) : false; }
static bool VXSqlResult_fetchFirst(XSqlResult* result) { return result ? XSqlResult_fetch_base(result, 0) : false; }
static bool VXSqlResult_fetchLast(XSqlResult* result) { return result && result->m_size > 0 ? XSqlResult_fetch_base(result, result->m_size - 1) : false; }
static int VXSqlResult_size(XSqlResult* result) { return result ? result->m_size : -1; }
static int VXSqlResult_numRowsAffected(XSqlResult* result) { return result ? result->m_numRowsAffected : -1; }
static XSqlRecord* VXSqlResult_record(XSqlResult* result)
{
    return result && result->m_active && result->m_select
        ? XSqlRecord_create_copy(&result->m_record) : XSqlRecord_create();
}
static XVariant* VXSqlResult_lastInsertId(XSqlResult* result) { return result && result->m_active ? XVariant_create_copy(&result->m_lastInsertId) : XVariant_create_null(); }
static bool VXSqlResult_exec(XSqlResult* result) { (void)result; return false; }
static bool VXSqlResult_prepare(XSqlResult* result, const XString* query) { if (!result) return false; XSqlResult_setQuery_base(result, query); result->m_bindingSyntax = query && XString_contains_char(query, XChar_fromLatin1(':'), XChar_CaseSensitive) ? XSqlBindingSyntax_Named : XSqlBindingSyntax_Positional; return query != NULL; }
static bool VXSqlResult_savePrepare(XSqlResult* result, const XString* query)
{
    bool prepared;
    if (!result || !query) return false;
    xsql_result_clear_bindings(result);
    result->m_active = false;
    result->m_at = XSqlLocation_BeforeFirstRow;
    prepared = XSqlResult_prepare_base(result, query);
    if (prepared) xsql_result_parse_bindings(result, query);
    return prepared;
}
static void VXSqlResult_bindValuePos(XSqlResult* result, int position, const XVariant* value, XSqlParamType type)
{
    if (!result || !value || position < 0
        || !xsql_result_reserve_bindings(result, (size_t)position + 1)) return;
    while (result->m_boundCount <= (size_t)position) {
        result->m_boundValues[result->m_boundCount] = XVariant_create_null();
        result->m_boundTypes[result->m_boundCount] = XSqlParamType_In;
        result->m_boundNames[result->m_boundCount] = NULL;
        ++result->m_boundCount;
    }
    xsql_result_store_value(result, (size_t)position, value, type);
}
static void VXSqlResult_bindValueName(XSqlResult* result, const XString* name, const XVariant* value, XSqlParamType type)
{
    size_t index;
    bool matched = false;
    if (!result || !name || !value) return;
    for (index = 0; index < result->m_boundCount; ++index) {
        if (result->m_boundNames[index] && XString_equals(result->m_boundNames[index], name, XChar_CaseSensitive)) {
            xsql_result_store_value(result, index, value, type);
            matched = true;
        }
    }
    if (!matched) {
        int position = (int)result->m_boundCount;
        VXSqlResult_bindValuePos(result, position, value, type);
        if ((size_t)position < result->m_boundCount)
            result->m_boundNames[position] = XString_create_copy(name);
    }
}
static void VXSqlResult_setAt(XSqlResult* result, int at) { if (result) result->m_at = at; }
static void VXSqlResult_setActive(XSqlResult* result, bool active) { if (result) { if (active) xsql_result_assign_string(&result->m_executedQuery, result->m_lastQuery); result->m_active = active; } }
static void VXSqlResult_setLastError(XSqlResult* result, const XSqlError* error) { if (result && error) XSqlError_copy_base(&result->m_lastError, error); }
static void VXSqlResult_setQuery(XSqlResult* result, const XString* query) { if (result) xsql_result_assign_string(&result->m_lastQuery, query); }
static void VXSqlResult_setSelect(XSqlResult* result, bool select) { if (result) result->m_select = select; }
static void VXSqlResult_setForwardOnly(XSqlResult* result, bool forwardOnly) { if (result) result->m_forwardOnly = forwardOnly; }
static bool VXSqlResult_execBatch(XSqlResult* result, XSqlBatchExecutionMode mode) { (void)result; (void)mode; return false; }
static void VXSqlResult_detachFromResultSet(XSqlResult* result) { if (result) result->m_active = false; }
static void VXSqlResult_setNumericalPrecisionPolicy(XSqlResult* result, XSqlNumericalPrecisionPolicy policy) { if (result) result->m_precisionPolicy = policy; }
static bool VXSqlResult_nextResult(XSqlResult* result) { (void)result; return false; }
static void* VXSqlResult_handle(XSqlResult* result) { return result ? result->m_handle : NULL; }

XVariant* XSqlResult_data_base(XSqlResult* result, int field) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_Data, XVariant*(*)(XSqlResult*, int))(result, field) : XVariant_create_null(); }
bool XSqlResult_isNull_base(XSqlResult* result, int field) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_IsNull, bool(*)(XSqlResult*, int))(result, field); }
bool XSqlResult_reset_base(XSqlResult* result, const XString* query) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_Reset, bool(*)(XSqlResult*, const XString*))(result, query); }
bool XSqlResult_fetch_base(XSqlResult* result, int index) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_Fetch, bool(*)(XSqlResult*, int))(result, index); }
bool XSqlResult_fetchNext_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_FetchNext, bool(*)(XSqlResult*))(result); }
bool XSqlResult_fetchPrevious_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_FetchPrevious, bool(*)(XSqlResult*))(result); }
bool XSqlResult_fetchFirst_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_FetchFirst, bool(*)(XSqlResult*))(result); }
bool XSqlResult_fetchLast_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_FetchLast, bool(*)(XSqlResult*))(result); }
int XSqlResult_size_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_Size, int(*)(XSqlResult*))(result) : -1; }
int XSqlResult_numRowsAffected_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_NumRowsAffected, int(*)(XSqlResult*))(result) : -1; }
XSqlRecord* XSqlResult_record_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_Record, XSqlRecord*(*)(XSqlResult*))(result) : XSqlRecord_create(); }
XVariant* XSqlResult_lastInsertId_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_LastInsertId, XVariant*(*)(XSqlResult*))(result) : XVariant_create_null(); }
bool XSqlResult_execBatch_base(XSqlResult* result, XSqlBatchExecutionMode mode) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_ExecBatch, bool(*)(XSqlResult*, XSqlBatchExecutionMode))(result, mode); }
void XSqlResult_detachFromResultSet_base(XSqlResult* result) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_DetachFromResultSet, void(*)(XSqlResult*))(result); }
void XSqlResult_setNumericalPrecisionPolicy_base(XSqlResult* result, XSqlNumericalPrecisionPolicy policy) { if (result && !XClassIsVtableNull(result)) XClassGetVirtualFunc(result, EXSqlResult_SetNumericalPrecisionPolicy, void(*)(XSqlResult*, XSqlNumericalPrecisionPolicy))(result, policy); }
XSqlNumericalPrecisionPolicy XSqlResult_numericalPrecisionPolicy(const XSqlResult* result) { return result ? result->m_precisionPolicy : XSqlNumericalPrecisionPolicy_HighPrecision; }
void XSqlResult_setPositionalBindingEnabled(XSqlResult* result, bool enable) { if (result) result->m_positionalBindingEnabled = enable; }
bool XSqlResult_isPositionalBindingEnabled(const XSqlResult* result) { return result && result->m_positionalBindingEnabled; }
bool XSqlResult_nextResult_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) && XClassGetVirtualFunc(result, EXSqlResult_NextResult, bool(*)(XSqlResult*))(result); }
void* XSqlResult_handle_base(XSqlResult* result) { return result && !XClassIsVtableNull(result) ? XClassGetVirtualFunc(result, EXSqlResult_Handle, void*(*)(XSqlResult*))(result) : NULL; }
void XSqlResult_clear(XSqlResult* result) { if (!result) return; XSqlResult_detachFromResultSet_base(result); xsql_result_clear_bindings(result); if (result->m_lastQuery) { XString_delete_base(result->m_lastQuery); result->m_lastQuery = NULL; } if (result->m_executedQuery) { XString_delete_base(result->m_executedQuery); result->m_executedQuery = NULL; } XSqlRecord_clear(&result->m_record); XSqlError_deinit_base(&result->m_lastError); XSqlError_init(&result->m_lastError); XVariant_setValue_null(&result->m_lastInsertId); result->m_at = XSqlLocation_BeforeFirstRow; result->m_size = -1; result->m_numRowsAffected = -1; result->m_active = false; result->m_select = false; }
void XSqlResult_resetBindCount(XSqlResult* result) { if (result) result->m_bindCount = 0; }
static void VXSqlResult_move(XSqlResult* dest, XSqlResult* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlResult_init(dest, src->m_driver);
    XSqlResult_clear(dest);
    dest->m_driver = src->m_driver;
    XSqlError_move_base(&dest->m_lastError, &src->m_lastError);
    XSqlRecord_move_base(&dest->m_record, &src->m_record);
    XVariant_move_base(&dest->m_lastInsertId, &src->m_lastInsertId);
    dest->m_lastQuery = src->m_lastQuery;
    dest->m_executedQuery = src->m_executedQuery;
    dest->m_boundValues = src->m_boundValues;
    dest->m_boundTypes = src->m_boundTypes;
    dest->m_boundNames = src->m_boundNames;
    dest->m_boundCount = src->m_boundCount;
    dest->m_boundCapacity = src->m_boundCapacity;
    dest->m_bindCount = src->m_bindCount;
    dest->m_at = src->m_at;
    dest->m_size = src->m_size;
    dest->m_numRowsAffected = src->m_numRowsAffected;
    dest->m_active = src->m_active;
    dest->m_select = src->m_select;
    dest->m_forwardOnly = src->m_forwardOnly;
    dest->m_positionalBindingEnabled = src->m_positionalBindingEnabled;
    dest->m_bindingSyntax = src->m_bindingSyntax;
    dest->m_precisionPolicy = src->m_precisionPolicy;
    dest->m_handle = src->m_handle;
    src->m_lastQuery = NULL; src->m_executedQuery = NULL;
    src->m_boundValues = NULL; src->m_boundTypes = NULL; src->m_boundNames = NULL;
    src->m_boundCount = src->m_boundCapacity = src->m_bindCount = 0;
    src->m_at = XSqlLocation_BeforeFirstRow; src->m_active = false;
}
