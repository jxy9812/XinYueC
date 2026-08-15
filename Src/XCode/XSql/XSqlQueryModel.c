/**
 * @file       XSqlQueryModel.c
 * @brief      SQL 查询模型实现。
 */
#include "XSqlQueryModel.h"

#include <string.h>

#define XSQL_QUERY_MODEL_PREFETCH 255

static void VXSqlQueryModel_deinit(XSqlQueryModel* model);
static void VXSqlQueryModel_clear(XSqlQueryModel* model);
static void VXSqlQueryModel_queryChange(XSqlQueryModel* model);
static XSqlModelIndex VXSqlQueryModel_indexInQuery(const XSqlQueryModel* model, XSqlModelIndex item);

XVtable* XSqlQueryModel_class_init(void)
{
    static void* table[] = {
        (void*)VXSqlQueryModel_clear, (void*)VXSqlQueryModel_queryChange,
        (void*)VXSqlQueryModel_indexInQuery
    };
    XVTABLE_INIT_DEFAULT(XSqlQueryModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlQueryModel_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlQueryModel_init(XSqlQueryModel* model)
{
    if (!model) return;
    memset(((unsigned char*)model) + sizeof(XObject), 0, sizeof(*model) - sizeof(XObject));
    XObject_init((XObject*)model);
    XClassSetVtable(model, XSqlQueryModel);
    XSqlQuery_init(&model->m_query);
    XSqlRecord_init(&model->m_record);
    XSqlError_init(&model->m_lastError);
}

XSqlQueryModel* XSqlQueryModel_create_ex(XMemoryType memory)
{
    XSqlQueryModel* model = (XSqlQueryModel*)XMemory_malloc(sizeof(XSqlQueryModel), memory);
    if (!model) return NULL;
    memset(model, 0, sizeof(*model));
    XSqlQueryModel_init(model);
    Set_Class_Memory(model, memory); Set_Class_IsHeap(model, true);
    return model;
}

static void xsql_model_clear_rows(XSqlQueryModel* model)
{
    if (!model) return;
    for (size_t i = 0; i < model->m_rowCount; ++i) if (model->m_rows[i]) XSqlRecord_delete_base(model->m_rows[i]);
    if (model->m_rows) XFree_System(model->m_rows);
    model->m_rows = NULL;
    model->m_rowCount = 0;
    model->m_rowCapacity = 0;
}

static bool xsql_model_reserve_rows(XSqlQueryModel* model, size_t wanted)
{
    if (wanted <= model->m_rowCapacity) return true;
    size_t capacity = model->m_rowCapacity ? model->m_rowCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    XSqlRecord** rows = (XSqlRecord**)XRealloc_System(model->m_rows, capacity * sizeof(XSqlRecord*));
    if (!rows) return false;
    model->m_rows = rows;
    model->m_rowCapacity = capacity;
    return true;
}

static bool xsql_model_reserve_headers(XSqlQueryModel* model, size_t wanted)
{
    if (wanted <= model->m_headerCapacity) return true;
    size_t capacity = model->m_headerCapacity ? model->m_headerCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    XSqlModelHeaderValue* headers = (XSqlModelHeaderValue*)XRealloc_System(
        model->m_headers, capacity * sizeof(XSqlModelHeaderValue));
    if (!headers) return false;
    for (size_t i = model->m_headerCapacity; i < capacity; ++i) {
        headers[i].m_section = -1;
        headers[i].m_role = 0;
        headers[i].m_value = NULL;
    }
    model->m_headers = headers;
    model->m_headerCapacity = capacity;
    return true;
}

static void xsql_model_clear_headers(XSqlQueryModel* model)
{
    if (!model) return;
    for (size_t i = 0; i < model->m_headerCount; ++i)
        if (model->m_headers[i].m_value) XString_delete_base(model->m_headers[i].m_value);
    if (model->m_headers) XFree_System(model->m_headers);
    model->m_headers = NULL;
    model->m_headerCount = 0;
    model->m_headerCapacity = 0;
}

static XSqlModelHeaderValue* xsql_model_find_header(XSqlQueryModel* model, int section, int role)
{
    if (!model) return NULL;
    for (size_t i = 0; i < model->m_headerCount; ++i) {
        if (model->m_headers[i].m_section == section && model->m_headers[i].m_role == role)
            return &model->m_headers[i];
    }
    return NULL;
}

static const XSqlModelHeaderValue* xsql_model_find_header_const(const XSqlQueryModel* model,
                                                                 int section, int role)
{
    return xsql_model_find_header((XSqlQueryModel*)model, section, role);
}

static void VXSqlQueryModel_deinit(XSqlQueryModel* model)
{
    if (!model) return;
    xsql_model_clear_rows(model);
    xsql_model_clear_headers(model);
    XSqlQuery_deinit_base(&model->m_query);
    XSqlRecord_deinit_base(&model->m_record);
    XSqlError_deinit_base(&model->m_lastError);
    XClass_Deinit_Parent(XObject, model);
}

int XSqlQueryModel_rowCount(const XSqlQueryModel* model) { return model ? (int)model->m_rowCount : 0; }
int XSqlQueryModel_columnCount(const XSqlQueryModel* model) { return model ? XSqlRecord_count(&model->m_record) : 0; }
XSqlRecord* XSqlQueryModel_record(const XSqlQueryModel* model, int row) { return model && row >= 0 && (size_t)row < model->m_rowCount ? XSqlRecord_create_copy(model->m_rows[row]) : XSqlRecord_create(); }
XSqlRecord* XSqlQueryModel_record_current(const XSqlQueryModel* model) { return model ? XSqlRecord_create_copy(&model->m_record) : XSqlRecord_create(); }
XVariant* XSqlQueryModel_data(const XSqlQueryModel* model, int row, int column, XSqlItemDataRole role)
{
    if (!model || (role != XSqlItemDataRole_Display && role != XSqlItemDataRole_Edit)
        || row < 0 || column < 0 || (size_t)row >= model->m_rowCount)
        return XVariant_create_null();
    if (column >= XSqlRecord_count(&model->m_record)
        || !XSqlRecord_isGenerated(&model->m_record, column)) return XVariant_create_null();
    return XSqlRecord_value(model->m_rows[row], column);
}
XVariant* XSqlQueryModel_headerData(const XSqlQueryModel* model, int section, XSqlOrientation orientation, XSqlItemDataRole role)
{
    const XSqlModelHeaderValue* header;
    if (!model || section < 0) return XVariant_create_null();
    if (orientation == XSqlOrientation_Vertical)
        return role == XSqlItemDataRole_Display && section < XSqlQueryModel_rowCount(model)
                   ? XVariant_create_int32(section + 1)
                                                        : XVariant_create_null();
    if (section >= XSqlQueryModel_columnCount(model)) return XVariant_create_null();
    header = xsql_model_find_header_const(model, section, role);
    if (!header && role == XSqlItemDataRole_Display)
        header = xsql_model_find_header_const(model, section, XSqlItemDataRole_Edit);
    XString* name = header && header->m_value ? XString_create_copy(header->m_value)
                                               : (role == XSqlItemDataRole_Display
                                                      ? XSqlRecord_fieldName(&model->m_record, section)
                                                      : NULL);
    XVariant* result = name ? XVariant_create_String_move(name) : XVariant_create_null();
    if (name) XString_delete_base(name);
    return result;
}
bool XSqlQueryModel_setHeaderData(XSqlQueryModel* model, int section, XSqlOrientation orientation, const XVariant* value, XSqlItemDataRole role)
{
    XString* headerValue;
    XSqlModelHeaderValue* header;
    if (!model || orientation != XSqlOrientation_Horizontal || section < 0
        || section >= XSqlQueryModel_columnCount(model) || !value) return false;
    headerValue = XVariant_toString(value);
    if (!headerValue) return false;
    header = xsql_model_find_header(model, section, role);
    if (header) {
        if (header->m_value) XString_delete_base(header->m_value);
        header->m_value = headerValue;
        return true;
    }
    if (!xsql_model_reserve_headers(model, model->m_headerCount + 1)) {
        XString_delete_base(headerValue);
        return false;
    }
    header = &model->m_headers[model->m_headerCount++];
    header->m_section = section;
    header->m_role = role;
    header->m_value = headerValue;
    return true;
}

static bool xsql_model_insert_record_columns(XSqlRecord* record, int column, int count)
{
    int inserted = 0;
    if (!record || column < 0 || count < 0 || column > XSqlRecord_count(record)) return false;
    for (; inserted < count; ++inserted) {
        XSqlField* field = XSqlField_create();
        if (field) {
            XSqlField_setReadOnly(field, true);
            XSqlField_setGenerated(field, false);
        }
        bool ok = field && XSqlRecord_insert(record, column + inserted, field);
        if (field) XSqlField_delete_base(field);
        if (!ok) break;
    }
    if (inserted == count) return true;
    while (inserted > 0) XSqlRecord_remove(record, column + --inserted);
    return false;
}

static void xsql_model_remove_record_columns(XSqlRecord* record, int column, int count)
{
    if (!record || column < 0 || count <= 0) return;
    for (int i = 0; i < count; ++i) XSqlRecord_remove(record, column);
}

bool XSqlQueryModel_insertColumns(XSqlQueryModel* model, int column, int count)
{
    int oldColumns;
    if (!model || column < 0 || count <= 0) return false;
    oldColumns = XSqlQueryModel_columnCount(model);
    if (column > oldColumns) return false;
    if (!xsql_model_insert_record_columns(&model->m_record, column, count)) return false;
    for (size_t row = 0; row < model->m_rowCount; ++row) {
        if (!xsql_model_insert_record_columns(model->m_rows[row], column, count)) {
            while (row > 0) xsql_model_remove_record_columns(model->m_rows[--row], column, count);
            xsql_model_remove_record_columns(&model->m_record, column, count);
            return false;
        }
    }
    for (size_t i = 0; i < model->m_headerCount; ++i)
        if (model->m_headers[i].m_section >= column)
            model->m_headers[i].m_section += count;
    XSqlQueryModel_modelReset_signal(model);
    return true;
}

bool XSqlQueryModel_removeColumns(XSqlQueryModel* model, int column, int count)
{
    int oldColumns;
    if (!model || column < 0 || count <= 0) return false;
    oldColumns = XSqlQueryModel_columnCount(model);
    if (column > oldColumns || count > oldColumns - column) return false;
    xsql_model_remove_record_columns(&model->m_record, column, count);
    for (size_t row = 0; row < model->m_rowCount; ++row)
        xsql_model_remove_record_columns(model->m_rows[row], column, count);
    for (size_t i = 0; i < model->m_headerCount;) {
        XSqlModelHeaderValue* header = &model->m_headers[i];
        if (header->m_section >= column && header->m_section < column + count) {
            if (header->m_value) XString_delete_base(header->m_value);
            if (i + 1 < model->m_headerCount)
                memmove(header, header + 1,
                        (model->m_headerCount - i - 1) * sizeof(XSqlModelHeaderValue));
            --model->m_headerCount;
            model->m_headers[model->m_headerCount].m_section = -1;
            model->m_headers[model->m_headerCount].m_role = 0;
            model->m_headers[model->m_headerCount].m_value = NULL;
            continue;
        }
        if (header->m_section >= column + count)
            header->m_section -= count;
        ++i;
    }
    XSqlQueryModel_modelReset_signal(model);
    return true;
}
static bool xsql_model_append_current_row(XSqlQueryModel* model)
{
    XSqlRecord* row;
    if (!model) return false;
    row = XSqlQuery_record(&model->m_query);
    if (!row || !xsql_model_reserve_rows(model, model->m_rowCount + 1)) {
        if (row) XSqlRecord_delete_base(row);
        return false;
    }
    for (int column = 0; column < XSqlRecord_count(row); ++column) {
        XVariant* value = XSqlQuery_value(&model->m_query, column);
        if (value) {
            XSqlRecord_setValue(row, column, value);
            XVariant_delete_base(value);
        }
    }
    model->m_rows[model->m_rowCount++] = row;
    return true;
}

static bool xsql_model_fetch_rows(XSqlQueryModel* model, size_t limit)
{
    size_t fetched = 0;
    if (!model || !model->m_canFetchMore || !XSqlQuery_isActive(&model->m_query))
        return false;
    while (fetched < limit) {
        if (!XSqlQuery_next(&model->m_query)) {
            model->m_canFetchMore = false;
            break;
        }
        if (!xsql_model_append_current_row(model)) {
            model->m_canFetchMore = false;
            break;
        }
        ++fetched;
    }
    return fetched != 0;
}

static void xsql_model_load_query(XSqlQueryModel* model)
{
    const XSqlDriver* driver;
    XSqlRecord* record;
    bool hasQuerySize;
    if (!model) return;
    xsql_model_clear_rows(model);
    XSqlRecord_clear(&model->m_record);
    model->m_canFetchMore = false;
    if (!XSqlQuery_isActive(&model->m_query) || !XSqlQuery_isSelect(&model->m_query))
        return;
    record = XSqlQuery_record(&model->m_query);
    if (record) {
        XSqlRecord_move_base(&model->m_record, record);
        XSqlRecord_delete_base(record);
    }
    driver = XSqlQuery_driver(&model->m_query);
    hasQuerySize = driver && XSqlDriver_hasFeature_base(driver,
                                                         XSqlDriverFeature_QuerySize);
    model->m_canFetchMore = true;
    if (hasQuerySize) {
        while (xsql_model_fetch_rows(model, XSQL_QUERY_MODEL_PREFETCH)) {
            if (!model->m_canFetchMore) break;
        }
    } else {
        xsql_model_fetch_rows(model, XSQL_QUERY_MODEL_PREFETCH);
    }
}
void XSqlQueryModel_setQuery_move(XSqlQueryModel* model, XSqlQuery* query)
{
    XSqlError* error;
    if (!model || !query || model->m_query.m_result == query->m_result) return;
    if (model->m_query.m_ownsResult && model->m_query.m_result) XSqlResult_delete_base(model->m_query.m_result);
    model->m_query.m_result = query->m_result;
    model->m_query.m_ownsResult = query->m_ownsResult;
    query->m_result = NULL;
    query->m_ownsResult = false;
    if (XSqlQuery_isForwardOnly(&model->m_query)) {
        xsql_model_clear_rows(model);
        XSqlRecord_clear(&model->m_record);
        error = XSqlError_create_utf8("Forward-only queries cannot be used in a data model",
                                      NULL, XSqlErrorType_ConnectionError, NULL);
        if (error) {
            XSqlQueryModel_setLastError(model, error);
            XSqlError_delete_base(error);
        }
        model->m_canFetchMore = false;
    } else if (!XSqlQuery_isActive(&model->m_query)) {
        error = XSqlQuery_lastError(&model->m_query);
        if (error) {
            XSqlQueryModel_setLastError(model, error);
            XSqlError_delete_base(error);
        }
        xsql_model_clear_rows(model);
        XSqlRecord_clear(&model->m_record);
        model->m_canFetchMore = false;
    } else {
        XSqlError_deinit_base(&model->m_lastError);
        XSqlError_init(&model->m_lastError);
        xsql_model_load_query(model);
    }
    XSqlQueryModel_modelReset_signal(model);
    XSqlQueryModel_queryChange(model);
}

bool XSqlQueryModel_setQuery_copy(XSqlQueryModel* model, const XSqlQuery* query)
{
    XSqlQuery* copy;
    bool ok;
    if (!model || !query) return false;
    copy = XSqlQuery_create_copy(query);
    if (!copy) return false;
    ok = XSqlQuery_isActive(copy) && !XSqlQuery_isForwardOnly(copy);
    XSqlQueryModel_setQuery_move(model, copy);
    XSqlQuery_delete_base(copy);
    return ok;
}
bool XSqlQueryModel_setQuery(XSqlQueryModel* model, const XString* sql, const XSqlDatabase* database) { if (!model || !sql) return false; XSqlQuery* query = XSqlDatabase_exec(database, sql); if (!query) return false; bool ok = XSqlQuery_isActive(query) && !XSqlQuery_isForwardOnly(query); XSqlQueryModel_setQuery_move(model, query); XSqlQuery_delete_base(query); return ok; }
bool XSqlQueryModel_setQuery_utf8(XSqlQueryModel* model, const char* sql, const XSqlDatabase* database) { XString* text = sql ? XString_create_utf8(sql) : NULL; bool result = XSqlQueryModel_setQuery(model, text, database); if (text) XString_delete_base(text); return result; }
const XSqlQuery* XSqlQueryModel_query_const(const XSqlQueryModel* model) { return model ? &model->m_query : NULL; }
XSqlQuery* XSqlQueryModel_query(const XSqlQueryModel* model) { return model ? XSqlQuery_create_copy(&model->m_query) : XSqlQuery_create(); }
static void VXSqlQueryModel_clear(XSqlQueryModel* model)
{
    if (!model) return;
    xsql_model_clear_rows(model);
    xsql_model_clear_headers(model);
    XSqlQuery_clear(&model->m_query);
    XSqlRecord_clear(&model->m_record);
    XSqlError_deinit_base(&model->m_lastError);
    XSqlError_init(&model->m_lastError);
    model->m_canFetchMore = false;
    XSqlQueryModel_modelReset_signal(model);
}
void XSqlQueryModel_clear(XSqlQueryModel* model)
{
    if (model && !XClassIsVtableNull(model))
        XClassGetVirtualFunc(model, EXSqlQueryModel_Clear, void(*)(XSqlQueryModel*))(model);
}
XSqlError* XSqlQueryModel_lastError(const XSqlQueryModel* model) { return model ? XSqlError_create_copy(&model->m_lastError) : XSqlError_create(NULL, NULL, XSqlErrorType_UnknownError, NULL); }
void XSqlQueryModel_fetchMore(XSqlQueryModel* model)
{
    if (xsql_model_fetch_rows(model, XSQL_QUERY_MODEL_PREFETCH))
        XSqlQueryModel_modelReset_signal(model);
}
bool XSqlQueryModel_canFetchMore(const XSqlQueryModel* model) { return model && model->m_canFetchMore; }
XStringList* XSqlQueryModel_roleNames(const XSqlQueryModel* model) { (void)model; XStringList* result = XStringList_create(); if (result) XStringList_push_back_utf8(result, "display"); return result; }
static XSqlModelIndex VXSqlQueryModel_indexInQuery(const XSqlQueryModel* model, XSqlModelIndex item) { (void)model; return item; }
XSqlModelIndex XSqlQueryModel_indexInQuery(const XSqlQueryModel* model, XSqlModelIndex item)
{
    return model && !XClassIsVtableNull(model)
        ? XClassGetVirtualFunc(model, EXSqlQueryModel_IndexInQuery, XSqlModelIndex(*)(const XSqlQueryModel*, XSqlModelIndex))(model, item)
        : item;
}
void XSqlQueryModel_setLastError(XSqlQueryModel* model, const XSqlError* error) { if (model && error) XSqlError_copy_base(&model->m_lastError, error); }
static void VXSqlQueryModel_queryChange(XSqlQueryModel* model) { (void)model; }
void XSqlQueryModel_queryChange(XSqlQueryModel* model)
{
    if (model && !XClassIsVtableNull(model))
        XClassGetVirtualFunc(model, EXSqlQueryModel_QueryChange, void(*)(XSqlQueryModel*))(model);
}

void* XSqlQueryModel_modelReset_signal(XSqlQueryModel* model)
{
    XEmitSignal((XObject*)model, XSqlQueryModel_modelReset_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XSqlQueryModel_dataChanged_signal(XSqlQueryModel* model, int firstRow,
                                        int firstColumn, int lastRow, int lastColumn)
{
    XEmitSignal((XObject*)model, XSqlQueryModel_dataChanged_signal,
                XVarList_create(8, sizeof(int), &firstRow, sizeof(int), &firstColumn,
                                sizeof(int), &lastRow, sizeof(int), &lastColumn),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
