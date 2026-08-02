/**
 * @file       XSqlQuery.c
 * @brief      SQL 查询类实现。
 */
#include "XSqlQuery.h"

#include <string.h>

static void VXSqlQuery_copy(XSqlQuery* dest, const XSqlQuery* src);
static void VXSqlQuery_move(XSqlQuery* dest, XSqlQuery* src);
static void VXSqlQuery_deinit(XSqlQuery* query);

static void xsql_query_clear_error(XSqlResult* result)
{
    XSqlError* empty;
    if (!result) return;
    empty = XSqlError_create(NULL, NULL, XSqlErrorType_NoError, NULL);
    if (!empty) return;
    XSqlResult_setLastError_base(result, empty);
    XSqlError_delete_base(empty);
}

XVtable* XSqlQuery_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlQuery))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlQuery_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlQuery_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlQuery_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlQuery_init(XSqlQuery* query)
{
    if (!query) return;
    memset(((unsigned char*)query) + sizeof(XClass), 0, sizeof(*query) - sizeof(XClass));
    XClass_init((XClass*)query);
    XClassSetVtable(query, XSqlQuery);
}

void XSqlQuery_init_database(XSqlQuery* query, const XSqlDatabase* database)
{
    XSqlQuery_init(query);
    if (!query || !database) return;
    XSqlDriver* driver = XSqlDatabase_driver(database);
    if (driver) {
        query->m_result = XSqlDriver_createResult_base(driver);
        query->m_ownsResult = query->m_result != NULL;
        if (query->m_result)
            query->m_result->m_precisionPolicy = XSqlDatabase_numericalPrecisionPolicy(database);
    }
}

XSqlQuery* XSqlQuery_create(void)
{
    XSqlQuery* query = (XSqlQuery*)XMalloc_System(sizeof(XSqlQuery));
    if (!query) return NULL;
    memset(query, 0, sizeof(*query));
    XSqlQuery_init(query);
    Set_Class_MemoryFree(query, XFree_System);
    return query;
}

XSqlQuery* XSqlQuery_create_database(const XSqlDatabase* database)
{
    XSqlQuery* query = XSqlQuery_create();
    if (query) XSqlQuery_init_database(query, database);
    return query;
}

XSqlQuery* XSqlQuery_create_result(XSqlResult* result)
{
    XSqlQuery* query = XSqlQuery_create();
    if (query) { query->m_result = result; query->m_ownsResult = result != NULL; }
    return query;
}

static void VXSqlQuery_deinit(XSqlQuery* query)
{
    if (!query) return;
    if (query->m_ownsResult && query->m_result) XSqlResult_delete_base(query->m_result);
    query->m_result = NULL;
    query->m_ownsResult = false;
    XClass_Deinit_Parent(XClass, query);
}

static void VXSqlQuery_copy(XSqlQuery* dest, const XSqlQuery* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlQuery_init(dest);
    if (dest->m_ownsResult && dest->m_result) XSqlResult_delete_base(dest->m_result);
    dest->m_result = NULL;
    dest->m_ownsResult = false;
    if (src->m_result) {
        dest->m_result = src->m_result->m_driver
            ? XSqlDriver_createResult_base(src->m_result->m_driver)
            : XSqlResult_create(NULL);
        if (dest->m_result) {
            XSqlResult_copy_base(dest->m_result, src->m_result);
            dest->m_ownsResult = true;
        }
    }
}

static void VXSqlQuery_move(XSqlQuery* dest, XSqlQuery* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlQuery_init(dest);
    if (dest->m_ownsResult && dest->m_result) XSqlResult_delete_base(dest->m_result);
    dest->m_result = src->m_result;
    dest->m_ownsResult = src->m_ownsResult;
    src->m_result = NULL;
    src->m_ownsResult = false;
}

XSqlQuery* XSqlQuery_create_copy(const XSqlQuery* other) { if (!other) return NULL; XSqlQuery* result = XSqlQuery_create(); if (result) XSqlQuery_copy_base(result, other); return result; }
XSqlQuery* XSqlQuery_create_move(XSqlQuery* other) { if (!other) return NULL; XSqlQuery* result = XSqlQuery_create(); if (result) XSqlQuery_move_base(result, other); return result; }
void XSqlQuery_swap(XSqlQuery* left, XSqlQuery* right)
{
    if (!left || !right || left == right) return;
    XSqlQuery* temp = XSqlQuery_create_move(left);
    if (!temp) return;
    XSqlQuery_move_base(left, right);
    XSqlQuery_move_base(right, temp);
    XSqlQuery_delete_base(temp);
}
bool XSqlQuery_isValid(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isValid(query->m_result); }
bool XSqlQuery_isActive(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isActive(query->m_result); }
bool XSqlQuery_isNull(const XSqlQuery* query, int field) { return !query || !query->m_result || !XSqlResult_isActive(query->m_result) || !XSqlResult_isValid(query->m_result) || field < 0 || XSqlResult_isNull_base(query->m_result, field); }
bool XSqlQuery_isNull_utf8(const XSqlQuery* query, const char* name) { XSqlRecord* record = XSqlQuery_record(query); int index = XSqlRecord_indexOf_utf8(record, name); bool result = XSqlQuery_isNull(query, index); XSqlRecord_delete_base(record); return result; }
bool XSqlQuery_isNull_2(const XSqlQuery* query, const XString* name) { XSqlRecord* record = XSqlQuery_record(query); int index = XSqlRecord_indexOf(record, name); bool result = XSqlQuery_isNull(query, index); XSqlRecord_delete_base(record); return result; }
int XSqlQuery_at(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_at(query->m_result) : XSqlLocation_BeforeFirstRow; }
XString* XSqlQuery_lastQuery(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_lastQuery(query->m_result) : XString_create(); }
int XSqlQuery_numRowsAffected(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isActive(query->m_result) ? XSqlResult_numRowsAffected_base(query->m_result) : -1; }
XSqlError* XSqlQuery_lastError(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_lastError(query->m_result) : XSqlError_create(NULL, NULL, XSqlErrorType_UnknownError, NULL); }
bool XSqlQuery_isSelect(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isSelect(query->m_result); }
int XSqlQuery_size(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isActive(query->m_result)
    && XSqlDriver_hasFeature_base(XSqlResult_driver(query->m_result), XSqlDriverFeature_QuerySize)
    ? XSqlResult_size_base(query->m_result) : -1; }
const XSqlDriver* XSqlQuery_driver(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_driver(query->m_result) : NULL; }
const XSqlResult* XSqlQuery_result(const XSqlQuery* query) { return query ? query->m_result : NULL; }
bool XSqlQuery_isForwardOnly(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isForwardOnly(query->m_result); }
XSqlRecord* XSqlQuery_record(const XSqlQuery* query)
{
    XSqlRecord* record;
    int field;
    if (!query || !query->m_result || !XSqlResult_isActive(query->m_result))
        return XSqlRecord_create();
    record = XSqlResult_record_base(query->m_result);
    if (!record || !XSqlResult_isValid(query->m_result)) return record;
    for (field = 0; field < XSqlRecord_count(record); ++field) {
        XVariant* value = XSqlQuery_value(query, field);
        XSqlRecord_setValue(record, field, value);
        if (value) XVariant_delete_base(value);
    }
    return record;
}
void XSqlQuery_setForwardOnly(XSqlQuery* query, bool forwardOnly) { if (query && query->m_result) XSqlResult_setForwardOnly_base(query->m_result, forwardOnly); }
bool XSqlQuery_exec_query(XSqlQuery* query, const XString* sql)
{
    XString* trimmed;
    bool result;
    if (!query || !query->m_result || !sql || XString_length_base(sql) == 0) return false;
    XSqlResult_clear(query->m_result);
    trimmed = XString_trimmed(sql);
    XSqlResult_setQuery_base(query->m_result, trimmed ? trimmed : sql);
    XSqlResult_setAt_base(query->m_result, XSqlLocation_BeforeFirstRow);
    xsql_query_clear_error(query->m_result);
    result = XSqlResult_reset_base(query->m_result, sql);
    XSqlResult_setQuery_base(query->m_result, trimmed ? trimmed : sql);
    if (trimmed) XString_delete_base(trimmed);
    return result;
}
bool XSqlQuery_exec_utf8(XSqlQuery* query, const char* sql) { XString* text = sql ? XString_create_utf8(sql) : NULL; bool result = XSqlQuery_exec_query(query, text); if (text) XString_delete_base(text); return result; }
XVariant* XSqlQuery_value(const XSqlQuery* query, int field) { return query && query->m_result && XSqlResult_isActive(query->m_result) && XSqlResult_isValid(query->m_result) && field >= 0 ? XSqlResult_data_base(query->m_result, field) : XVariant_create_null(); }
XVariant* XSqlQuery_value_utf8(const XSqlQuery* query, const char* name) { XSqlRecord* record = XSqlQuery_record(query); int index = XSqlRecord_indexOf_utf8(record, name); XVariant* result = XSqlQuery_value(query, index); XSqlRecord_delete_base(record); return result; }
XVariant* XSqlQuery_value_2(const XSqlQuery* query, const XString* name) { XSqlRecord* record = XSqlQuery_record(query); int index = XSqlRecord_indexOf(record, name); XVariant* result = XSqlQuery_value(query, index); XSqlRecord_delete_base(record); return result; }
void XSqlQuery_setNumericalPrecisionPolicy(XSqlQuery* query, XSqlNumericalPrecisionPolicy policy) { if (query && query->m_result) XSqlResult_setNumericalPrecisionPolicy_base(query->m_result, policy); }
XSqlNumericalPrecisionPolicy XSqlQuery_numericalPrecisionPolicy(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_numericalPrecisionPolicy(query->m_result) : XSqlNumericalPrecisionPolicy_HighPrecision; }
void XSqlQuery_setPositionalBindingEnabled(XSqlQuery* query, bool enable) { if (query && query->m_result) XSqlResult_setPositionalBindingEnabled(query->m_result, enable); }
bool XSqlQuery_isPositionalBindingEnabled(const XSqlQuery* query) { return query && query->m_result && XSqlResult_isPositionalBindingEnabled(query->m_result); }
bool XSqlQuery_seek(XSqlQuery* query, int index, bool relative)
{
    XSqlResult* result;
    int actual;
    if (!query || !(result = query->m_result) || !XSqlResult_isSelect(result)
        || !XSqlResult_isActive(result)) return false;
    if (!relative) {
        if (index < 0) { XSqlResult_setAt_base(result, XSqlLocation_BeforeFirstRow); return false; }
        actual = index;
    } else {
        switch (XSqlResult_at(result)) {
        case XSqlLocation_BeforeFirstRow:
            if (index <= 0) return false;
            actual = index - 1;
            break;
        case XSqlLocation_AfterLastRow:
            if (index >= 0 || !XSqlResult_fetchLast_base(result)) return false;
            actual = XSqlResult_at(result) + index + 1;
            break;
        default:
            if (XSqlResult_at(result) + index < 0) {
                XSqlResult_setAt_base(result, XSqlLocation_BeforeFirstRow);
                return false;
            }
            actual = XSqlResult_at(result) + index;
            break;
        }
    }
    if (XSqlResult_isForwardOnly(result) && actual < XSqlResult_at(result)) return false;
    if (actual == XSqlResult_at(result) + 1 && XSqlResult_at(result) != XSqlLocation_BeforeFirstRow) {
        if (!XSqlResult_fetchNext_base(result)) { XSqlResult_setAt_base(result, XSqlLocation_AfterLastRow); return false; }
        return true;
    }
    if (actual == XSqlResult_at(result) - 1) {
        if (!XSqlResult_fetchPrevious_base(result)) { XSqlResult_setAt_base(result, XSqlLocation_BeforeFirstRow); return false; }
        return true;
    }
    if (!XSqlResult_fetch_base(result, actual)) { XSqlResult_setAt_base(result, XSqlLocation_AfterLastRow); return false; }
    return true;
}
bool XSqlQuery_next(XSqlQuery* query)
{
    XSqlResult* result;
    if (!query || !(result = query->m_result) || !XSqlResult_isSelect(result) || !XSqlResult_isActive(result)) return false;
    if (XSqlResult_at(result) == XSqlLocation_BeforeFirstRow) return XSqlResult_fetchFirst_base(result);
    if (XSqlResult_at(result) == XSqlLocation_AfterLastRow) return false;
    if (!XSqlResult_fetchNext_base(result)) { XSqlResult_setAt_base(result, XSqlLocation_AfterLastRow); return false; }
    return true;
}
bool XSqlQuery_previous(XSqlQuery* query)
{
    XSqlResult* result;
    if (!query || !(result = query->m_result) || !XSqlResult_isSelect(result) || !XSqlResult_isActive(result)) return false;
    if (XSqlResult_isForwardOnly(result) || XSqlResult_at(result) == XSqlLocation_BeforeFirstRow) return false;
    if (XSqlResult_at(result) == XSqlLocation_AfterLastRow) return XSqlResult_fetchLast_base(result);
    if (!XSqlResult_fetchPrevious_base(result)) { XSqlResult_setAt_base(result, XSqlLocation_BeforeFirstRow); return false; }
    return true;
}
bool XSqlQuery_first(XSqlQuery* query)
{
    XSqlResult* result;
    if (!query || !(result = query->m_result) || !XSqlResult_isSelect(result) || !XSqlResult_isActive(result)) return false;
    if (XSqlResult_isForwardOnly(result) && XSqlResult_at(result) > XSqlLocation_BeforeFirstRow) return false;
    return XSqlResult_fetchFirst_base(result);
}
bool XSqlQuery_last(XSqlQuery* query) { return query && query->m_result && XSqlResult_isSelect(query->m_result) && XSqlResult_isActive(query->m_result) && XSqlResult_fetchLast_base(query->m_result); }
void XSqlQuery_clear(XSqlQuery* query)
{
    if (!query || !query->m_result) return;
    XSqlResult_clear(query->m_result);
    query->m_result->m_forwardOnly = false;
    query->m_result->m_positionalBindingEnabled = true;
    query->m_result->m_bindingSyntax = XSqlBindingSyntax_Positional;
    query->m_result->m_precisionPolicy = XSqlDriver_numericalPrecisionPolicy(
        XSqlResult_driver(query->m_result));
    query->m_result->m_handle = NULL;
}
bool XSqlQuery_exec(XSqlQuery* query)
{
    if (!query || !query->m_result) return false;
    XSqlResult_resetBindCount(query->m_result);
    xsql_query_clear_error(query->m_result);
    return XSqlResult_exec_base(query->m_result);
}
bool XSqlQuery_execBatch(XSqlQuery* query, XSqlBatchExecutionMode mode)
{
    if (!query || !query->m_result) return false;
    XSqlResult_resetBindCount(query->m_result);
    xsql_query_clear_error(query->m_result);
    return XSqlResult_execBatch_base(query->m_result, mode);
}
bool XSqlQuery_prepare(XSqlQuery* query, const XString* sql)
{
    if (!query || !query->m_result || !sql || XString_length_base(sql) == 0) return false;
    XSqlResult_setActive_base(query->m_result, false);
    XSqlResult_setAt_base(query->m_result, XSqlLocation_BeforeFirstRow);
    xsql_query_clear_error(query->m_result);
    return XSqlResult_savePrepare_base(query->m_result, sql);
}
bool XSqlQuery_prepare_utf8(XSqlQuery* query, const char* sql) { XString* text = sql ? XString_create_utf8(sql) : NULL; bool result = XSqlQuery_prepare(query, text); if (text) XString_delete_base(text); return result; }
void XSqlQuery_bindValue_utf8(XSqlQuery* query, const char* placeholder, const XVariant* value, XSqlParamType type) { if (query && query->m_result) XSqlResult_bindValue_utf8_base(query->m_result, placeholder, value, type); }
void XSqlQuery_bindValue_2(XSqlQuery* query, const XString* placeholder, const XVariant* value, XSqlParamType type) { if (query && query->m_result) XSqlResult_bindValue_2_base(query->m_result, placeholder, value, type); }
void XSqlQuery_bindValue(XSqlQuery* query, int position, const XVariant* value, XSqlParamType type) { if (query && query->m_result) XSqlResult_bindValue_base(query->m_result, position, value, type); }
void XSqlQuery_addBindValue(XSqlQuery* query, const XVariant* value, XSqlParamType type) { if (query && query->m_result) XSqlResult_addBindValue(query->m_result, value, type); }
XVariant* XSqlQuery_boundValue_utf8(const XSqlQuery* query, const char* placeholder) { return query && query->m_result ? XSqlResult_boundValue_utf8(query->m_result, placeholder) : XVariant_create_null(); }
XVariant* XSqlQuery_boundValue_2(const XSqlQuery* query, const XString* placeholder) { return query && query->m_result ? XSqlResult_boundValue_2(query->m_result, placeholder) : XVariant_create_null(); }
XVariant* XSqlQuery_boundValue(const XSqlQuery* query, int position) { return query && query->m_result ? XSqlResult_boundValue(query->m_result, position) : XVariant_create_null(); }
XVariantList* XSqlQuery_boundValues(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_boundValues(query->m_result) : XVariantList_create(); }
XStringList* XSqlQuery_boundValueNames(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_boundValueNames(query->m_result) : XStringList_create(); }
XString* XSqlQuery_boundValueName(const XSqlQuery* query, int position) { return query && query->m_result ? XSqlResult_boundValueName(query->m_result, position) : XString_create(); }
XString* XSqlQuery_executedQuery(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_executedQuery(query->m_result) : XString_create(); }
XVariant* XSqlQuery_lastInsertId(const XSqlQuery* query) { return query && query->m_result ? XSqlResult_lastInsertId_base(query->m_result) : XVariant_create_null(); }
void XSqlQuery_finish(XSqlQuery* query)
{
    if (!query || !query->m_result || !XSqlResult_isActive(query->m_result)) return;
    xsql_query_clear_error(query->m_result);
    XSqlResult_setAt_base(query->m_result, XSqlLocation_BeforeFirstRow);
    XSqlResult_detachFromResultSet_base(query->m_result);
    XSqlResult_setActive_base(query->m_result, false);
}
bool XSqlQuery_nextResult(XSqlQuery* query) { return query && query->m_result && XSqlResult_isActive(query->m_result) && XSqlResult_nextResult_base(query->m_result); }
