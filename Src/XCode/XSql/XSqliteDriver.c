#include "XSqliteDriver.h"

#include "XMemory.h"
#include "XSqlDatabase.h"
#include "XSqlQuery.h"
#include "XString.h"
#include "XByteArray.h"
#include "XDate.h"
#include "XTime.h"
#include "XDateTime.h"

#include "sqlite3.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int XSqliteMemory_initialize(void);

typedef struct XSqliteResult {
    XSqlResult m_parent;
    sqlite3_stmt* m_statement;
    XVariant*** m_rows;
    size_t m_rowCount;
    size_t m_rowCapacity;
    int m_columnCount;
    bool m_done;
} XSqliteResult;

typedef struct XSqliteDriver {
    XSqlDriver m_parent;
    sqlite3* m_database;
    XStringList* m_notifications;
} XSqliteDriver;

XCLASS_DEFINE_BEGING(XSqliteResult)
XCLASS_DEFINE_EXTEND_END(XSqliteResult, XSqlResult)

XCLASS_DEFINE_BEGING(XSqliteDriver)
XCLASS_DEFINE_EXTEND_END(XSqliteDriver, XSqlDriver)

static XVtable* XSqliteResult_class_init(void);
static XVtable* XSqliteDriver_class_init(void);

static void VXSqliteResult_copy(XSqliteResult* dest, const XSqliteResult* src);
static void VXSqliteResult_move(XSqliteResult* dest, XSqliteResult* src);
static void VXSqliteResult_deinit(XSqliteResult* result);
static XVariant* VXSqliteResult_data(XSqlResult* result, int field);
static bool VXSqliteResult_isNull(XSqlResult* result, int field);
static bool VXSqliteResult_reset(XSqlResult* result, const XString* query);
static bool VXSqliteResult_fetch(XSqlResult* result, int index);
static bool VXSqliteResult_fetchNext(XSqlResult* result);
static bool VXSqliteResult_fetchPrevious(XSqlResult* result);
static bool VXSqliteResult_fetchFirst(XSqlResult* result);
static bool VXSqliteResult_fetchLast(XSqlResult* result);
static int VXSqliteResult_size(XSqlResult* result);
static bool VXSqliteResult_prepare(XSqlResult* result, const XString* query);
static bool VXSqliteResult_exec(XSqlResult* result);
static void VXSqliteResult_detachFromResultSet(XSqlResult* result);
static void* VXSqliteResult_handle(XSqlResult* result);

static void VXSqliteDriver_deinit(XSqliteDriver* driver);
static bool VXSqliteDriver_beginTransaction(XSqlDriver* driver);
static bool VXSqliteDriver_commitTransaction(XSqlDriver* driver);
static bool VXSqliteDriver_rollbackTransaction(XSqlDriver* driver);
static XStringList* VXSqliteDriver_tables(const XSqlDriver* driver, XSqlTableType type);
static XSqlIndex* VXSqliteDriver_primaryIndex(const XSqlDriver* driver, const XString* tableName);
static XSqlRecord* VXSqliteDriver_record(const XSqlDriver* driver, const XString* tableName);
static XString* VXSqliteDriver_formatValue(const XSqlDriver* driver, const XSqlField* field, bool trimStrings);
static XString* VXSqliteDriver_escapeIdentifier(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
static void* VXSqliteDriver_handle(const XSqlDriver* driver);
static bool VXSqliteDriver_hasFeature(const XSqlDriver* driver, XSqlDriverFeature feature);
static void VXSqliteDriver_close(XSqlDriver* driver);
static XSqlResult* VXSqliteDriver_createResult(const XSqlDriver* driver);
static bool VXSqliteDriver_open(XSqlDriver* driver, const XString* database,
                                const XString* user, const XString* password,
                                const XString* host, int port, const XString* options);
static bool VXSqliteDriver_isIdentifierEscaped(const XSqlDriver* driver,
                                               const XString* identifier,
                                               XSqlIdentifierType type);
static XString* VXSqliteDriver_stripDelimiters(const XSqlDriver* driver,
                                               const XString* identifier,
                                               XSqlIdentifierType type);
static bool VXSqliteDriver_subscribe(XSqlDriver* driver, const XString* name);
static bool VXSqliteDriver_unsubscribe(XSqlDriver* driver, const XString* name);
static XStringList* VXSqliteDriver_subscribed(const XSqlDriver* driver);
static bool VXSqliteDriver_cancelQuery(XSqlDriver* driver);

static void xsqlite_update_hook(void* context, int operation, const char* database,
                                const char* table, sqlite3_int64 rowid)
{
    XSqliteDriver* driver = (XSqliteDriver*)context;
    XString* name;
    XVariant* payload;
    (void)operation;
    (void)database;
    if (!driver || !driver->m_notifications || !table
        || !XStringList_contains_utf8(driver->m_notifications, table, XChar_CaseSensitive)) return;
    name = XString_create_utf8(table);
    payload = XVariant_create_int64((int64_t)rowid);
    if (name && payload)
        XSqlDriver_notification_signal(&driver->m_parent, name,
                                       XSqlNotificationSource_Unknown, payload);
    if (name) XString_delete_base(name);
    if (payload) XVariant_delete_base(payload);
}

static int xsqlite_type_from_name(const char* name)
{
    char type[32];
    size_t length = 0;
    size_t i;

    if (!name) return XVariantType_NULL;
    while (name[length] && length < sizeof(type) - 1) {
        type[length] = (char)tolower((unsigned char)name[length]);
        ++length;
    }
    type[length] = 0;
    if (strcmp(type, "integer") == 0 || strcmp(type, "int") == 0)
        return XVariantType_Int32;
    if (strcmp(type, "double") == 0 || strcmp(type, "float") == 0
        || strcmp(type, "real") == 0 || strncmp(type, "numeric", 7) == 0)
        return XVariantType_Double;
    if (strcmp(type, "blob") == 0) return XVariantType_ByteArray;
    if (strcmp(type, "boolean") == 0 || strcmp(type, "bool") == 0)
        return XVariantType_Bool;
    for (i = 0; i < length; ++i) {
        if (i + 2 < length && type[i] == 'i' && type[i + 1] == 'n' && type[i + 2] == 't')
            return XVariantType_Int32;
        if (type[i] == 'c' || type[i] == 'l' || type[i] == 'v' || type[i] == 't')
            return XVariantType_String;
    }
    return XVariantType_String;
}

static int xsqlite_type_from_column(sqlite3_stmt* statement, int column)
{
    const char* declared = sqlite3_column_decltype(statement, column);
    if (declared && declared[0]) return xsqlite_type_from_name(declared);
    switch (sqlite3_column_type(statement, column)) {
    case SQLITE_INTEGER: return XVariantType_Int64;
    case SQLITE_FLOAT: return XVariantType_Double;
    case SQLITE_BLOB: return XVariantType_ByteArray;
    case SQLITE_TEXT: return XVariantType_String;
    default: return XVariantType_NULL;
    }
}

static void xsqlite_clear_error(XSqlError* error)
{
    if (!error) return;
    XSqlError_deinit_base(error);
    XSqlError_init(error);
}

static void xsqlite_set_result_error(XSqliteResult* result, const char* text,
                                     XSqlErrorType type, int code)
{
    XSqlError* error;
    XString* errorCode;
    XString* driverText;
    XString* databaseText;
    sqlite3* database = result && result->m_parent.m_driver
        ? ((const XSqliteDriver*)result->m_parent.m_driver)->m_database : NULL;
    driverText = XString_create_utf8(text ? text : "");
    databaseText = XString_create_utf8(database ? sqlite3_errmsg(database) : "");
    errorCode = XString_create_fmt_utf8("%d", code);
    error = XSqlError_create(driverText, databaseText, type, errorCode);
    if (driverText) XString_delete_base(driverText);
    if (databaseText) XString_delete_base(databaseText);
    if (errorCode) XString_delete_base(errorCode);
    if (error) {
        XSqlResult_setLastError_base(&result->m_parent, error);
        XSqlError_delete_base(error);
    }
    (void)code;
}

static void xsqlite_set_driver_error(XSqliteDriver* driver, const char* text,
                                     XSqlErrorType type, int code)
{
    XSqlError* error;
    XString* errorCode;
    XString* driverText;
    XString* databaseText;
    driverText = XString_create_utf8(text ? text : "");
    databaseText = XString_create_utf8(driver && driver->m_database
                                           ? sqlite3_errmsg(driver->m_database) : "");
    errorCode = XString_create_fmt_utf8("%d", code);
    error = XSqlError_create(driverText, databaseText, type, errorCode);
    if (driverText) XString_delete_base(driverText);
    if (databaseText) XString_delete_base(databaseText);
    if (errorCode) XString_delete_base(errorCode);
    if (error) {
        XSqlDriver_setLastError(&driver->m_parent, error);
        XSqlError_delete_base(error);
    }
}

static void xsqlite_clear_rows(XSqliteResult* result)
{
    size_t row;
    int column;
    if (!result) return;
    for (row = 0; row < result->m_rowCount; ++row) {
        if (!result->m_rows[row]) continue;
        for (column = 0; column < result->m_columnCount; ++column)
            if (result->m_rows[row][column]) XVariant_delete_base(result->m_rows[row][column]);
        XFree_System(result->m_rows[row]);
    }
    if (result->m_rows) XFree_System(result->m_rows);
    result->m_rows = NULL;
    result->m_rowCount = 0;
    result->m_rowCapacity = 0;
}

static bool xsqlite_reserve_rows(XSqliteResult* result, size_t wanted)
{
    size_t capacity;
    XVariant*** rows;
    if (!result) return false;
    if (wanted <= result->m_rowCapacity) return true;
    capacity = result->m_rowCapacity ? result->m_rowCapacity * 2 : 8;
    while (capacity < wanted) capacity *= 2;
    rows = (XVariant***)XRealloc_System(result->m_rows, capacity * sizeof(*rows));
    if (!rows) return false;
    result->m_rows = rows;
    result->m_rowCapacity = capacity;
    return true;
}

static XVariant* xsqlite_column_value(sqlite3_stmt* statement, int column)
{
    switch (sqlite3_column_type(statement, column)) {
    case SQLITE_INTEGER:
        return XVariant_create_int64((int64_t)sqlite3_column_int64(statement, column));
    case SQLITE_FLOAT:
        return XVariant_create_double(sqlite3_column_double(statement, column));
    case SQLITE_BLOB: {
        int size = sqlite3_column_bytes(statement, column);
        if (size > 0)
            return XVariant_create_byteArray(sqlite3_column_blob(statement, column), (size_t)size);
        {
            XVariant* value = XVariant_create(NULL, sizeof(XByteArray), XVariantType_ByteArray);
            if (value) XByteArray_init(XByteArray_fromVariant_ref(value), true);
            return value;
        }
    }
    case SQLITE_TEXT: {
        XString* string = XString_create_with_length_utf8(
            (const char*)sqlite3_column_text(statement, column),
            (size_t)sqlite3_column_bytes(statement, column));
        XVariant* value = string ? XVariant_create_String_move(string) : NULL;
        if (string) XString_delete_base(string);
        return value;
    }
    default:
        return XVariant_create_null();
    }
}

static bool xsqlite_build_record(XSqliteResult* result)
{
    int column;
    if (!result || !result->m_statement) return false;
    XSqlRecord_clear(&result->m_parent.m_record);
    result->m_columnCount = sqlite3_column_count(result->m_statement);
    for (column = 0; column < result->m_columnCount; ++column) {
        const char* name = sqlite3_column_name(result->m_statement, column);
        const char* table = sqlite3_column_table_name(result->m_statement, column);
        XString* fieldName = XString_create_utf8(name ? name : "");
        XString* tableName = table ? XString_create_utf8(table) : NULL;
        XSqlField* field = XSqlField_create_ex(fieldName, xsqlite_type_from_column(result->m_statement, column), tableName);
        bool appended = field && XSqlRecord_append(&result->m_parent.m_record, field);
        if (fieldName) XString_delete_base(fieldName);
        if (tableName) XString_delete_base(tableName);
        if (field) XSqlField_delete_base(field);
        if (!appended) return false;
    }
    return true;
}

static bool xsqlite_append_row(XSqliteResult* result)
{
    XVariant** row;
    int column;
    if (!result || !xsqlite_reserve_rows(result, result->m_rowCount + 1)) return false;
    row = (XVariant**)XCalloc_System((size_t)result->m_columnCount, sizeof(*row));
    if (!row && result->m_columnCount > 0) return false;
    for (column = 0; column < result->m_columnCount; ++column) {
        row[column] = xsqlite_column_value(result->m_statement, column);
        if (!row[column]) {
            int cleanup;
            for (cleanup = 0; cleanup < column; ++cleanup)
                XVariant_delete_base(row[cleanup]);
            XFree_System(row);
            return false;
        }
    }
    result->m_rows[result->m_rowCount++] = row;
    return true;
}

static bool xsqlite_names_equal(const XString* boundName, const char* sqliteName)
{
    const char* name;
    if (!boundName || !sqliteName) return false;
    name = XString_toUtf8(boundName);
    if (!name) return false;
    if (strcmp(name, sqliteName) == 0) return true;
    if (name[0] != ':' && name[0] != '@' && name[0] != '$') {
        if (sqliteName[0] && strcmp(name, sqliteName + 1) == 0) return true;
    }
    return false;
}

static bool xsqlite_bind_variant(sqlite3_stmt* statement, int index, const XVariant* value)
{
    int type;
    if (!value || !XVariant_isValid(value)) return sqlite3_bind_null(statement, index) == SQLITE_OK;
    type = XVariant_type((XVariant*)value);
    switch (type) {
    case XVariantType_NULL:
        return sqlite3_bind_null(statement, index) == SQLITE_OK;
    case XVariantType_ByteArray: {
        const XByteArray* array = XByteArray_fromVariant_ref(value);
        return array && sqlite3_bind_blob(statement, index, XByteArray_data((XByteArray*)array),
                                          (int)XByteArray_size_base(array), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    case XVariantType_String: {
        const XString* string = XVariant_toString_const(value);
        const char* text = string ? XString_toUtf8(string) : "";
        return sqlite3_bind_text(statement, index, text ? text : "",
                                 (int)(text ? XString_toUtf8_length(string) : 0), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    case XVariantType_Bool:
        return sqlite3_bind_int(statement, index, XVariant_toBool(value) ? 1 : 0) == SQLITE_OK;
    case XVariantType_Double:
    case XVariantType_Float:
        return sqlite3_bind_double(statement, index, XVariant_toDouble(value)) == SQLITE_OK;
    case XVariantType_Date: {
        const XDate* date = XVariant_toDate_ref(value);
        XString* text = date && XDate_isValid(date)
            ? XString_create_fmt_utf8("%04d-%02d-%02d", XDate_year(date),
                                      XDate_month(date), XDate_day(date)) : XString_create();
        int result = text ? sqlite3_bind_text(statement, index, XString_toUtf8(text),
                                              (int)XString_toUtf8_length(text), SQLITE_TRANSIENT)
                          : SQLITE_NOMEM;
        if (text) XString_delete_base(text);
        return result == SQLITE_OK;
    }
    case XVariantType_Time: {
        const XTime* time = XVariant_toTime_ref(value);
        XString* text = time && XTime_isValid(time)
            ? XString_create_fmt_utf8("%02d:%02d:%02d.%03d", XTime_hour(time),
                                      XTime_minute(time), XTime_second(time), XTime_msec(time))
            : XString_create();
        int result = text ? sqlite3_bind_text(statement, index, XString_toUtf8(text),
                                              (int)XString_toUtf8_length(text), SQLITE_TRANSIENT)
                          : SQLITE_NOMEM;
        if (text) XString_delete_base(text);
        return result == SQLITE_OK;
    }
    case XVariantType_DateTime: {
        const XDateTime* datetime = XVariant_toDateTime_ref(value);
        const XDate* date = datetime ? &datetime->m_date : NULL;
        const XTime* time = datetime ? &datetime->m_time : NULL;
        XString* text = datetime && XDateTime_isValid(datetime)
            ? XString_create_fmt_utf8("%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                                      XDate_year(date), XDate_month(date), XDate_day(date),
                                      XTime_hour(time), XTime_minute(time),
                                      XTime_second(time), XTime_msec(time)) : XString_create();
        int result = text ? sqlite3_bind_text(statement, index, XString_toUtf8(text),
                                              (int)XString_toUtf8_length(text), SQLITE_TRANSIENT)
                          : SQLITE_NOMEM;
        if (text) XString_delete_base(text);
        return result == SQLITE_OK;
    }
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
        return sqlite3_bind_int64(statement, index, (sqlite3_int64)XVariant_toUint64(value)) == SQLITE_OK;
    default:
        return sqlite3_bind_int64(statement, index, (sqlite3_int64)XVariant_toInt64(value)) == SQLITE_OK;
    }
}

static bool xsqlite_bind_values(XSqliteResult* result)
{
    XSqlResult* base = &result->m_parent;
    int parameterCount = sqlite3_bind_parameter_count(result->m_statement);
    int parameter;
    bool hasNamed = false;
    size_t bound;

    for (bound = 0; bound < base->m_boundCount; ++bound)
        if (base->m_boundNames[bound]) { hasNamed = true; break; }
    for (parameter = 1; parameter <= parameterCount; ++parameter) {
        int source = parameter - 1;
        const char* parameterName = sqlite3_bind_parameter_name(result->m_statement, parameter);
        if (hasNamed && parameterName) {
            source = -1;
            for (bound = 0; bound < base->m_boundCount; ++bound) {
                if (xsqlite_names_equal(base->m_boundNames[bound], parameterName)) {
                    source = (int)bound;
                    break;
                }
            }
        }
        if (source < 0 || (size_t)source >= base->m_boundCount
            || !xsqlite_bind_variant(result->m_statement, parameter, base->m_boundValues[source])) {
            xsqlite_set_result_error(result, "Unable to bind parameters",
                                     XSqlErrorType_StatementError, SQLITE_ERROR);
            return false;
        }
    }
    if (!hasNamed && (size_t)parameterCount != base->m_boundCount) {
        xsqlite_set_result_error(result, "Parameter count mismatch",
                                 XSqlErrorType_StatementError, SQLITE_ERROR);
        return false;
    }
    return true;
}

static void xsqlite_finalize(XSqliteResult* result)
{
    if (!result) return;
    if (result->m_statement) sqlite3_finalize(result->m_statement);
    result->m_statement = NULL;
    result->m_parent.m_handle = NULL;
}

static void xsqlite_prepare_clear(XSqliteResult* result)
{
    if (!result) return;
    xsqlite_finalize(result);
    xsqlite_clear_rows(result);
    XSqlRecord_clear(&result->m_parent.m_record);
    XVariant_setValue_null(&result->m_parent.m_lastInsertId);
    xsqlite_clear_error(&result->m_parent.m_lastError);
    result->m_parent.m_at = XSqlLocation_BeforeFirstRow;
    result->m_parent.m_size = -1;
    result->m_parent.m_numRowsAffected = -1;
    result->m_parent.m_active = false;
    result->m_parent.m_select = false;
    result->m_columnCount = 0;
    result->m_done = false;
}

XVtable* XSqliteResult_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSqliteResult)
    XVTABLE_INHERIT_XCLASS(XSqlResult);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqliteResult_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqliteResult_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqliteResult_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Data, VXSqliteResult_data);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_IsNull, VXSqliteResult_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Reset, VXSqliteResult_reset);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Fetch, VXSqliteResult_fetch);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_FetchNext, VXSqliteResult_fetchNext);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_FetchPrevious, VXSqliteResult_fetchPrevious);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_FetchFirst, VXSqliteResult_fetchFirst);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_FetchLast, VXSqliteResult_fetchLast);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Size, VXSqliteResult_size);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Prepare, VXSqliteResult_prepare);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Exec, VXSqliteResult_exec);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_DetachFromResultSet, VXSqliteResult_detachFromResultSet);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Handle, VXSqliteResult_handle);
    return XVTABLE_DEFAULT;
}

static XSqliteResult* XSqliteResult_create(const XSqlDriver* driver)
{
    XSqliteResult* result = (XSqliteResult*)XMalloc_System(sizeof(*result));
    if (!result) return NULL;
    memset(result, 0, sizeof(*result));
    XSqlResult_init(&result->m_parent, driver);
    XClassSetVtable(result, XSqliteResult);
    Set_Class_MemoryFree(result, XFree_System);
    return result;
}

static void VXSqliteResult_deinit(XSqliteResult* result)
{
    if (!result) return;
    xsqlite_finalize(result);
    xsqlite_clear_rows(result);
    XClass_Deinit_Parent(XSqlResult, result);
}

static void VXSqliteResult_copy(XSqliteResult* dest, const XSqliteResult* src)
{
    size_t row;
    int column;
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) {
        XSqlResult_init(&dest->m_parent, src->m_parent.m_driver);
        XClassSetVtable(dest, XSqliteResult);
    }
    xsqlite_finalize(dest);
    xsqlite_clear_rows(dest);
    XClass_Parent(XSqlResult, EXClass_Copy, void(*)(XSqlResult*, const XSqlResult*))
        (&dest->m_parent, &src->m_parent);
    dest->m_columnCount = src->m_columnCount;
    /* sqlite3_stmt cannot be copied. Re-prepare a read-only query so a copy
     * made before the first fetch remains usable by QSqlQueryModel. */
    if (src->m_statement && src->m_parent.m_active && src->m_parent.m_select
        && src->m_parent.m_lastQuery) {
        if (VXSqliteResult_prepare(&dest->m_parent, dest->m_parent.m_lastQuery)
            && VXSqliteResult_exec(&dest->m_parent))
            return;
        xsqlite_finalize(dest);
        xsqlite_clear_rows(dest);
    }
    for (row = 0; row < src->m_rowCount; ++row) {
        if (!xsqlite_reserve_rows(dest, dest->m_rowCount + 1)) return;
        dest->m_rows[dest->m_rowCount] = (XVariant**)XCalloc_System(
            (size_t)dest->m_columnCount, sizeof(XVariant*));
        if (!dest->m_rows[dest->m_rowCount] && dest->m_columnCount > 0) return;
        for (column = 0; column < dest->m_columnCount; ++column)
            dest->m_rows[dest->m_rowCount][column] = XVariant_create_copy(src->m_rows[row][column]);
        ++dest->m_rowCount;
    }
    dest->m_statement = NULL;
    dest->m_parent.m_handle = NULL;
    dest->m_done = true;
}

static void VXSqliteResult_move(XSqliteResult* dest, XSqliteResult* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) {
        XSqlResult_init(&dest->m_parent, src->m_parent.m_driver);
        XClassSetVtable(dest, XSqliteResult);
    }
    xsqlite_finalize(dest);
    xsqlite_clear_rows(dest);
    XClass_Parent(XSqlResult, EXClass_Move, void(*)(XSqlResult*, XSqlResult*))
        (&dest->m_parent, &src->m_parent);
    dest->m_statement = src->m_statement;
    dest->m_rows = src->m_rows;
    dest->m_rowCount = src->m_rowCount;
    dest->m_rowCapacity = src->m_rowCapacity;
    dest->m_columnCount = src->m_columnCount;
    dest->m_done = src->m_done;
    dest->m_parent.m_handle = dest->m_statement;
    src->m_statement = NULL;
    src->m_rows = NULL;
    src->m_rowCount = 0;
    src->m_rowCapacity = 0;
    src->m_columnCount = 0;
    src->m_done = true;
    src->m_parent.m_handle = NULL;
}

static XVariant* VXSqliteResult_data(XSqlResult* base, int field)
{
    XSqliteResult* result = (XSqliteResult*)base;
    if (!result || base->m_at < 0 || (size_t)base->m_at >= result->m_rowCount
        || field < 0 || field >= result->m_columnCount) return XVariant_create_null();
    return XVariant_create_copy(result->m_rows[base->m_at][field]);
}

static bool VXSqliteResult_isNull(XSqlResult* base, int field)
{
    XVariant* value = VXSqliteResult_data(base, field);
    bool result = !value || !XVariant_isValid(value);
    if (value) XVariant_delete_base(value);
    return result;
}

static bool VXSqliteResult_prepare(XSqlResult* base, const XString* query)
{
    XSqliteResult* result = (XSqliteResult*)base;
    const XSqliteDriver* driver;
    const char* sql;
    const char* tail = NULL;
    int code;

    if (!result || !query || !base->m_driver) return false;
    driver = (const XSqliteDriver*)base->m_driver;
    if (!driver->m_database) {
        xsqlite_set_result_error(result, "Unable to prepare statement",
                                 XSqlErrorType_ConnectionError, SQLITE_MISUSE);
        return false;
    }
    xsqlite_prepare_clear(result);
    XSqlResult_setQuery_base(base, query);
    sql = XString_toUtf8(query);
    code = sqlite3_prepare_v2(driver->m_database, sql ? sql : "", -1,
                              &result->m_statement, &tail);
    if (code != SQLITE_OK) {
        xsqlite_set_result_error(result, "Unable to prepare statement",
                                 XSqlErrorType_StatementError, code);
        xsqlite_finalize(result);
        return false;
    }
    while (tail && *tail && isspace((unsigned char)*tail)) ++tail;
    if (tail && *tail) {
        xsqlite_set_result_error(result, "Unable to execute multiple statements at a time",
                                 XSqlErrorType_StatementError, SQLITE_MISUSE);
        xsqlite_finalize(result);
        return false;
    }
    base->m_bindingSyntax = XString_contains_char(query, XChar_fromLatin1(':'),
                                                   XChar_CaseSensitive)
        ? XSqlBindingSyntax_Named : XSqlBindingSyntax_Positional;
    base->m_handle = result->m_statement;
    if (!xsqlite_build_record(result)) {
        xsqlite_set_result_error(result, "Unable to build result record",
                                 XSqlErrorType_StatementError, SQLITE_NOMEM);
        xsqlite_finalize(result);
        return false;
    }
    base->m_select = sqlite3_column_count(result->m_statement) > 0;
    return true;
}

static bool VXSqliteResult_exec(XSqlResult* base)
{
    XSqliteResult* result = (XSqliteResult*)base;
    sqlite3* database;
    int code;

    if (!result || !result->m_statement || !base->m_driver) return false;
    database = ((const XSqliteDriver*)base->m_driver)->m_database;
    if (!database) return false;
    xsqlite_clear_rows(result);
    xsqlite_clear_error(&base->m_lastError);
    base->m_active = false;
    base->m_at = XSqlLocation_BeforeFirstRow;
    base->m_size = -1;
    base->m_numRowsAffected = -1;
    XVariant_setValue_null(&base->m_lastInsertId);
    code = sqlite3_reset(result->m_statement);
    if (code != SQLITE_OK || !xsqlite_bind_values(result)) {
        if (code != SQLITE_OK)
            xsqlite_set_result_error(result, "Unable to reset statement",
                                     XSqlErrorType_StatementError, code);
        sqlite3_reset(result->m_statement);
        return false;
    }
    result->m_done = false;
    code = sqlite3_step(result->m_statement);
    if (base->m_select && code == SQLITE_ROW) {
        if (!xsqlite_build_record(result) || !xsqlite_append_row(result)) {
            xsqlite_set_result_error(result, "Unable to cache result row",
                                     XSqlErrorType_UnknownError, SQLITE_NOMEM);
            sqlite3_reset(result->m_statement);
            return false;
        }
    } else if (code == SQLITE_DONE) {
        result->m_done = true;
        sqlite3_reset(result->m_statement);
    } else if (code != SQLITE_DONE) {
        xsqlite_set_result_error(result, "Unable to execute statement",
                                 XSqlErrorType_StatementError, code);
        sqlite3_reset(result->m_statement);
        return false;
    }
    base->m_active = true;
    base->m_at = XSqlLocation_BeforeFirstRow;
    if (base->m_select) {
        base->m_size = -1;
    } else {
        base->m_numRowsAffected = sqlite3_changes(database);
        XVariant_setValue_int64(&base->m_lastInsertId,
                                (int64_t)sqlite3_last_insert_rowid(database));
    }
    if (base->m_lastQuery) {
        if (base->m_executedQuery) XString_delete_base(base->m_executedQuery);
        base->m_executedQuery = XString_create_copy(base->m_lastQuery);
    }
    return true;
}

static bool VXSqliteResult_reset(XSqlResult* base, const XString* query)
{
    return query && VXSqliteResult_prepare(base, query) && VXSqliteResult_exec(base);
}

static bool VXSqliteResult_fetch(XSqlResult* base, int index)
{
    XSqliteResult* result = (XSqliteResult*)base;
    int code;
    if (!result || !base->m_active || !base->m_select || index < 0) {
        if (base) base->m_at = XSqlLocation_AfterLastRow;
        return false;
    }
    while ((size_t)index >= result->m_rowCount && !result->m_done) {
        code = sqlite3_step(result->m_statement);
        if (code == SQLITE_ROW) {
            if (!xsqlite_append_row(result)) {
                xsqlite_set_result_error(result, "Unable to cache result row",
                                         XSqlErrorType_UnknownError, SQLITE_NOMEM);
                base->m_active = false;
                result->m_done = true;
                base->m_at = XSqlLocation_AfterLastRow;
                return false;
            }
            continue;
        }
        result->m_done = true;
        sqlite3_reset(result->m_statement);
        if (code != SQLITE_DONE) {
            xsqlite_set_result_error(result, "Unable to fetch row",
                                     XSqlErrorType_StatementError, code);
            base->m_active = false;
        }
    }
    if ((size_t)index >= result->m_rowCount) {
        base->m_at = XSqlLocation_AfterLastRow;
        return false;
    }
    base->m_at = index;
    return true;
}

static bool VXSqliteResult_fetchNext(XSqlResult* base)
{
    if (!base || base->m_at == XSqlLocation_AfterLastRow) return false;
    return VXSqliteResult_fetch(base,
                                base->m_at == XSqlLocation_BeforeFirstRow ? 0 : base->m_at + 1);
}

static bool VXSqliteResult_fetchPrevious(XSqlResult* base)
{
    XSqliteResult* result = (XSqliteResult*)base;
    if (!result || !base->m_active || base->m_at == XSqlLocation_BeforeFirstRow)
        return false;
    if (base->m_at == XSqlLocation_AfterLastRow) {
        if (result->m_rowCount == 0) return false;
        base->m_at = (int)result->m_rowCount - 1;
        return true;
    }
    if (base->m_at <= 0) {
        base->m_at = XSqlLocation_BeforeFirstRow;
        return false;
    }
    --base->m_at;
    return true;
}

static bool VXSqliteResult_fetchFirst(XSqlResult* base)
{
    return VXSqliteResult_fetch(base, 0);
}

static bool VXSqliteResult_fetchLast(XSqlResult* base)
{
    XSqliteResult* result = (XSqliteResult*)base;
    if (!result || !base->m_active || !base->m_select) return false;
    while (!result->m_done) {
        if (!VXSqliteResult_fetch(base, (int)result->m_rowCount)) break;
    }
    return result->m_rowCount && VXSqliteResult_fetch(base, (int)result->m_rowCount - 1);
}

static int VXSqliteResult_size(XSqlResult* base)
{
    (void)base;
    return -1;
}

static void VXSqliteResult_detachFromResultSet(XSqlResult* base)
{
    XSqliteResult* result = (XSqliteResult*)base;
    if (!result) return;
    if (result->m_statement) sqlite3_reset(result->m_statement);
    xsqlite_clear_rows(result);
    base->m_active = false;
    base->m_at = XSqlLocation_BeforeFirstRow;
    base->m_size = -1;
    result->m_done = true;
}

static void* VXSqliteResult_handle(XSqlResult* base)
{
    return base ? base->m_handle : NULL;
}

static bool xsqlite_exec_simple(XSqliteDriver* driver, const char* sql, XSqlErrorType type,
                                const char* text)
{
    sqlite3_stmt* statement = NULL;
    int code;
    if (!driver || !driver->m_database) return false;
    code = sqlite3_prepare_v2(driver->m_database, sql, -1, &statement, NULL);
    if (code == SQLITE_OK) code = sqlite3_step(statement);
    if (statement) sqlite3_finalize(statement);
    if (code != SQLITE_DONE) {
        xsqlite_set_driver_error(driver, text, type, code);
        return false;
    }
    return true;
}

static XSqlResult* xsqlite_exec_metadata(const XSqlDriver* driver, const char* sql)
{
    XSqlResult* result = XSqlDriver_createResult_base(driver);
    XString* query = sql ? XString_create_utf8(sql) : NULL;
    if (!result || !query || !XSqlResult_reset_base(result, query)) {
        if (result) XSqlResult_delete_base(result);
        if (query) XString_delete_base(query);
        return NULL;
    }
    XString_delete_base(query);
    return result;
}

static void xsqlite_append_table_names(XStringList* list, XSqlResult* result)
{
    if (!list || !result || !XSqlResult_isActive(result)) return;
    while (XSqlResult_fetchNext_base(result)) {
        XVariant* value = XSqlResult_data_base(result, 0);
        XString* name = value ? XVariant_toString(value) : NULL;
        if (name) {
            XStringList_push_back_base(list, name);
            XString_delete_base(name);
        }
        if (value) XVariant_delete_base(value);
    }
}

static bool xsqlite_is_escaped(const XString* identifier)
{
    size_t length;
    XChar first;
    XChar last;
    if (!identifier) return false;
    length = XString_length_base(identifier);
    if (length <= 2) return false;
    first = XString_at(identifier, 0);
    last = XString_at(identifier, length - 1);
    return (first == XChar_fromLatin1('"') && last == XChar_fromLatin1('"'))
        || (first == XChar_fromLatin1('`') && last == XChar_fromLatin1('`'))
        || (first == XChar_fromLatin1('[') && last == XChar_fromLatin1(']'));
}

static XString* xsqlite_escape_part(const char* text, size_t length)
{
    XString* result = XString_create_utf8("\"");
    size_t i;
    if (!result) return NULL;
    for (i = 0; i < length; ++i) {
        if (text[i] == '"') XString_append_utf8(result, "\"\"");
        else XString_append_with_length_utf8(result, text + i, 1);
    }
    XString_append_utf8(result, "\"");
    return result;
}

static XSqlField* xsqlite_field_from_info(XSqlResult* result, const XString* tableName,
                                          bool primaryOnly, bool* primary)
{
    XVariant *nameValue, *typeValue, *notNullValue, *defaultValue, *primaryValue;
    XString *name, *type;
    XSqlField* field;
    int isPrimary;
    if (!result) return NULL;
    nameValue = XSqlResult_data_base(result, 1);
    typeValue = XSqlResult_data_base(result, 2);
    notNullValue = XSqlResult_data_base(result, 3);
    defaultValue = XSqlResult_data_base(result, 4);
    primaryValue = XSqlResult_data_base(result, 5);
    name = nameValue ? XVariant_toString(nameValue) : NULL;
    type = typeValue ? XVariant_toString(typeValue) : NULL;
    isPrimary = primaryValue ? XVariant_toInt(primaryValue) : 0;
    if (primary) *primary = isPrimary != 0;
    field = XSqlField_create_ex(name, xsqlite_type_from_name(type ? XString_toUtf8(type) : NULL), tableName);
    if (field) {
        XSqlField_setRequired(field, notNullValue && XVariant_toInt(notNullValue) != 0);
        if (defaultValue && XVariant_isValid(defaultValue)) XSqlField_setDefaultValue(field, defaultValue);
        if (isPrimary && type && strcmp(XString_toUtf8(type), "INTEGER") == 0)
            XSqlField_setAutoValue(field, true);
        if (primaryOnly && !isPrimary) {
            XSqlField_delete_base(field);
            field = NULL;
        }
    }
    if (nameValue) XVariant_delete_base(nameValue);
    if (typeValue) XVariant_delete_base(typeValue);
    if (notNullValue) XVariant_delete_base(notNullValue);
    if (defaultValue) XVariant_delete_base(defaultValue);
    if (primaryValue) XVariant_delete_base(primaryValue);
    if (name) XString_delete_base(name);
    if (type) XString_delete_base(type);
    return field;
}

static XSqlRecord* xsqlite_table_record(const XSqliteDriver* driver, const XString* tableName,
                                        bool primaryOnly)
{
    XSqlRecord* record = primaryOnly ? NULL : XSqlRecord_create();
    XSqlIndex* index = primaryOnly ? XSqlIndex_create() : NULL;
    XSqlResult* result;
    XString* escaped;
    XString* sql;
    bool primary;

    if (!driver || !driver->m_database || !tableName) goto fail;
    escaped = VXSqliteDriver_escapeIdentifier(&driver->m_parent, tableName, XSqlIdentifierType_TableName);
    sql = escaped ? XString_create_fmt_utf8("PRAGMA table_info(%s)", XString_toUtf8(escaped)) : NULL;
    if (escaped) XString_delete_base(escaped);
    result = sql ? xsqlite_exec_metadata(&driver->m_parent, XString_toUtf8(sql)) : NULL;
    if (sql) XString_delete_base(sql);
    if (!result) goto fail;
    while ((record || index) && XSqlResult_fetchNext_base(result)) {
        XSqlField* field = xsqlite_field_from_info(result, tableName, primaryOnly, &primary);
        if (!field && primaryOnly && XSqlResult_at(result) >= 0) continue;
        if (!field) break;
        if (record && !XSqlRecord_append(record, field)) { XSqlField_delete_base(field); goto fail_result; }
        if (index && primary && !XSqlIndex_append(index, field)) { XSqlField_delete_base(field); goto fail_result; }
        XSqlField_delete_base(field);
    }
    XSqlResult_delete_base(result);
    if (primaryOnly) return index;
    return record;

fail_result:
    XSqlResult_delete_base(result);
fail:
    if (record) XSqlRecord_delete_base(record);
    if (index) XSqlIndex_delete_base(index);
    return NULL;
}

XVtable* XSqliteDriver_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSqliteDriver)
    XVTABLE_INHERIT_XCLASS(XSqlDriver);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqliteDriver_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_BeginTransaction, VXSqliteDriver_beginTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CommitTransaction, VXSqliteDriver_commitTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_RollbackTransaction, VXSqliteDriver_rollbackTransaction);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Tables, VXSqliteDriver_tables);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_PrimaryIndex, VXSqliteDriver_primaryIndex);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Record, VXSqliteDriver_record);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_FormatValue, VXSqliteDriver_formatValue);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_EscapeIdentifier, VXSqliteDriver_escapeIdentifier);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Handle, VXSqliteDriver_handle);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_HasFeature, VXSqliteDriver_hasFeature);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Close, VXSqliteDriver_close);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CreateResult, VXSqliteDriver_createResult);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Open, VXSqliteDriver_open);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Subscribe, VXSqliteDriver_subscribe);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Unsubscribe, VXSqliteDriver_unsubscribe);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Subscribed, VXSqliteDriver_subscribed);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_IsIdentifierEscaped, VXSqliteDriver_isIdentifierEscaped);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_StripDelimiters, VXSqliteDriver_stripDelimiters);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CancelQuery, VXSqliteDriver_cancelQuery);
    return XVTABLE_DEFAULT;
}

XSqlDriver* XSqliteDriver_create(void)
{
    XSqliteDriver* driver = (XSqliteDriver*)XMalloc_System(sizeof(*driver));
    if (!driver) return NULL;
    memset(driver, 0, sizeof(*driver));
    XSqlDriver_init(&driver->m_parent, XSqlDriverType_Sqlite, XSqlDbmsType_Sqlite);
    XClassSetVtable(driver, XSqliteDriver);
    Set_Class_MemoryFree(driver, XFree_System);
    driver->m_notifications = XStringList_create();
    if (!driver->m_notifications) {
        XClass_delete_base((XClass*)driver);
        return NULL;
    }
    return &driver->m_parent;
}

static void VXSqliteDriver_deinit(XSqliteDriver* driver)
{
    if (!driver) return;
    VXSqliteDriver_close(&driver->m_parent);
    if (driver->m_notifications) {
        XStringList_delete_base(driver->m_notifications);
        driver->m_notifications = NULL;
    }
    XClass_Deinit_Parent(XSqlDriver, driver);
}

static bool VXSqliteDriver_open(XSqlDriver* base, const XString* database,
                                const XString* user, const XString* password,
                                const XString* host, int port, const XString* options)
{
    XSqliteDriver* driver = (XSqliteDriver*)base;
    const char* path;
    int code;
    (void)user;
    (void)password;
    (void)host;
    (void)port;
    (void)options;
    if (!driver) return false;
    if (base->m_open) VXSqliteDriver_close(base);
    code = XSqliteMemory_initialize();
    if (code != SQLITE_OK) {
        XSqlDriver_setOpenError(base, true);
        xsqlite_set_driver_error(driver, "Unable to initialize SQLite", XSqlErrorType_ConnectionError, code);
        return false;
    }
    path = database ? XString_toUtf8(database) : NULL;
    if (!path || !path[0]) path = ":memory:";
    code = sqlite3_open_v2(path, &driver->m_database,
                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            strcmp(path, ":memory:") == 0 ? NULL : "xin_xfile");
    if (code != SQLITE_OK) {
        xsqlite_set_driver_error(driver, "Unable to open database", XSqlErrorType_ConnectionError, code);
        if (driver->m_database) sqlite3_close_v2(driver->m_database);
        driver->m_database = NULL;
        XSqlDriver_setOpen(base, false);
        XSqlDriver_setOpenError(base, true);
        return false;
    }
    sqlite3_busy_timeout(driver->m_database, 5000);
    XSqlDriver_setOpen(base, true);
    XSqlDriver_setOpenError(base, false);
    return true;
}

static void VXSqliteDriver_close(XSqlDriver* base)
{
    XSqliteDriver* driver = (XSqliteDriver*)base;
    int code;
    if (driver && driver->m_database) sqlite3_update_hook(driver->m_database, NULL, NULL);
    if (driver && driver->m_notifications) XStringList_clear_base(driver->m_notifications);
    if (!driver || !driver->m_database) {
        if (base) XSqlDriver_setOpen(base, false);
        return;
    }
    code = sqlite3_close_v2(driver->m_database);
    if (code != SQLITE_OK) {
        xsqlite_set_driver_error(driver, "Unable to close database", XSqlErrorType_ConnectionError, code);
        XSqlDriver_setOpenError(base, true);
    }
    driver->m_database = NULL;
    XSqlDriver_setOpen(base, false);
}

static bool VXSqliteDriver_subscribe(XSqlDriver* base, const XString* name)
{
    XSqliteDriver* driver = (XSqliteDriver*)base;
    if (!driver || !base->m_open || !driver->m_database || !driver->m_notifications || !name
        || XString_length_base(name) == 0
        || XStringList_contains(driver->m_notifications, name, XChar_CaseSensitive)) return false;
    if (!XStringList_push_back_base(driver->m_notifications, (void*)name)) return false;
    if (XStringList_size_base(driver->m_notifications) == 1)
        sqlite3_update_hook(driver->m_database, xsqlite_update_hook, driver);
    return true;
}

static bool VXSqliteDriver_unsubscribe(XSqlDriver* base, const XString* name)
{
    XSqliteDriver* driver = (XSqliteDriver*)base;
    int64_t index;
    if (!driver || !base->m_open || !driver->m_database || !driver->m_notifications || !name)
        return false;
    index = XStringList_indexOf(driver->m_notifications, name, 0, XChar_CaseSensitive);
    if (index < 0) return false;
    XStringList_remove_base(driver->m_notifications, index, 1);
    if (XStringList_size_base(driver->m_notifications) == 0)
        sqlite3_update_hook(driver->m_database, NULL, NULL);
    return true;
}

static XStringList* VXSqliteDriver_subscribed(const XSqlDriver* base)
{
    const XSqliteDriver* driver = (const XSqliteDriver*)base;
    return driver && driver->m_notifications
        ? XStringList_create_copy(driver->m_notifications) : XStringList_create();
}

static bool VXSqliteDriver_beginTransaction(XSqlDriver* base)
{
    return xsqlite_exec_simple((XSqliteDriver*)base,
        "BEGIN", XSqlErrorType_BeginTransactionError, "Unable to begin transaction");
}

static bool VXSqliteDriver_commitTransaction(XSqlDriver* base)
{
    return xsqlite_exec_simple((XSqliteDriver*)base,
        "COMMIT", XSqlErrorType_CommitTransactionError, "Unable to commit transaction");
}

static bool VXSqliteDriver_rollbackTransaction(XSqlDriver* base)
{
    return xsqlite_exec_simple((XSqliteDriver*)base,
        "ROLLBACK", XSqlErrorType_RollbackTransactionError, "Unable to rollback transaction");
}

static XStringList* VXSqliteDriver_tables(const XSqlDriver* base, XSqlTableType type)
{
    XStringList* result = XStringList_create();
    XSqlResult* query;
    if (!result || !base || !base->m_open) return result;
    if (type & (XSqlTableType_Tables | XSqlTableType_Views)) {
        const char* filter = (type & XSqlTableType_Tables) && (type & XSqlTableType_Views)
            ? "type='table' OR type='view'"
            : (type & XSqlTableType_Tables) ? "type='table'" : "type='view'";
        char sql[256];
        (void)snprintf(sql, sizeof(sql),
                       "SELECT name FROM sqlite_master WHERE %s UNION ALL SELECT name FROM sqlite_temp_master WHERE %s",
                       filter, filter);
        query = xsqlite_exec_metadata(base, sql);
        if (query) {
            xsqlite_append_table_names(result, query);
            XSqlResult_delete_base(query);
        }
    }
    if (type & XSqlTableType_SystemTables) XStringList_push_back_utf8(result, "sqlite_master");
    return result;
}

static XSqlIndex* VXSqliteDriver_primaryIndex(const XSqlDriver* base, const XString* tableName)
{
    return (XSqlIndex*)xsqlite_table_record((const XSqliteDriver*)base, tableName, true);
}

static XSqlRecord* VXSqliteDriver_record(const XSqlDriver* base, const XString* tableName)
{
    return (XSqlRecord*)xsqlite_table_record((const XSqliteDriver*)base, tableName, false);
}

static XString* VXSqliteDriver_formatValue(const XSqlDriver* base, const XSqlField* field, bool trimStrings)
{
    return XClass_Parent(XSqlDriver, EXSqlDriver_FormatValue,
                         XString*(*)(const XSqlDriver*, const XSqlField*, bool))
        (base, field, trimStrings);
}

static XString* VXSqliteDriver_escapeIdentifier(const XSqlDriver* base, const XString* identifier,
                                                XSqlIdentifierType type)
{
    const char* text;
    const char* separator;
    XString* result;
    (void)base;
    (void)type;
    if (!identifier || XString_length_base(identifier) == 0 || xsqlite_is_escaped(identifier))
        return identifier ? XString_create_copy(identifier) : XString_create();
    text = XString_toUtf8(identifier);
    separator = text ? strchr(text, '.') : NULL;
    if (!separator) return xsqlite_escape_part(text ? text : "", text ? strlen(text) : 0);
    {
        XString* left = XString_create_with_length_utf8(text, (size_t)(separator - text));
        XString* right = XString_create_utf8(separator + 1);
        XString* escapedLeft = left && xsqlite_is_escaped(left)
            ? XString_create_copy(left)
            : xsqlite_escape_part(text, (size_t)(separator - text));
        XString* escapedRight = right && xsqlite_is_escaped(right)
            ? XString_create_copy(right)
            : xsqlite_escape_part(separator + 1, strlen(separator + 1));
        result = XString_create();
        if (result && escapedLeft && escapedRight
            && XString_append(result, escapedLeft)
            && XString_append_utf8(result, ".")
            && XString_append(result, escapedRight)) {
            /* result is complete. */
        } else {
            if (result) { XString_delete_base(result); result = NULL; }
        }
        if (left) XString_delete_base(left);
        if (right) XString_delete_base(right);
        if (escapedLeft) XString_delete_base(escapedLeft);
        if (escapedRight) XString_delete_base(escapedRight);
    }
    return result;
}

static void* VXSqliteDriver_handle(const XSqlDriver* base)
{
    const XSqliteDriver* driver = (const XSqliteDriver*)base;
    return driver ? driver->m_database : NULL;
}

static bool VXSqliteDriver_hasFeature(const XSqlDriver* base, XSqlDriverFeature feature)
{
    (void)base;
    switch (feature) {
    case XSqlDriverFeature_Transactions:
    case XSqlDriverFeature_Blob:
    case XSqlDriverFeature_Unicode:
    case XSqlDriverFeature_PreparedQueries:
    case XSqlDriverFeature_NamedPlaceholders:
    case XSqlDriverFeature_PositionalPlaceholders:
    case XSqlDriverFeature_LastInsertId:
    case XSqlDriverFeature_SimpleLocking:
    case XSqlDriverFeature_LowPrecisionNumbers:
    case XSqlDriverFeature_FinishQuery:
    case XSqlDriverFeature_EventNotifications:
        return true;
    default:
        return false;
    }
}

static XSqlResult* VXSqliteDriver_createResult(const XSqlDriver* base)
{
    XSqliteResult* result = XSqliteResult_create(base);
    return result ? &result->m_parent : NULL;
}

static bool VXSqliteDriver_isIdentifierEscaped(const XSqlDriver* base,
                                               const XString* identifier,
                                               XSqlIdentifierType type)
{
    (void)base;
    (void)type;
    return xsqlite_is_escaped(identifier);
}

static XString* VXSqliteDriver_stripDelimiters(const XSqlDriver* base,
                                               const XString* identifier,
                                               XSqlIdentifierType type)
{
    XString* result;
    size_t length;
    const char* text;
    const char* separator;
    (void)base;
    (void)type;
    if (!identifier) return XString_create();
    length = XString_length_base(identifier);
    text = XString_toUtf8(identifier);
    separator = text ? strchr(text, '.') : NULL;
    if (separator) {
        XString* left = XString_create_with_length_utf8(text, (size_t)(separator - text));
        XString* right = XString_create_utf8(separator + 1);
        bool leftEscaped = left && xsqlite_is_escaped(left);
        bool rightEscaped = right && xsqlite_is_escaped(right);
        if (leftEscaped || rightEscaped) {
            XString* leftText = leftEscaped ? XString_sliced_2(left, 1, XString_length_base(left) - 2) : XString_create_copy(left);
            XString* rightText = rightEscaped ? XString_sliced_2(right, 1, XString_length_base(right) - 2) : XString_create_copy(right);
            result = XString_create();
            if (result && leftText && rightText && XString_append(result, leftText)
                && XString_append_utf8(result, ".") && XString_append(result, rightText)) {
                /* result is complete. */
            } else if (result) {
                XString_delete_base(result);
                result = NULL;
            }
            if (leftText) XString_delete_base(leftText);
            if (rightText) XString_delete_base(rightText);
            if (left) XString_delete_base(left);
            if (right) XString_delete_base(right);
            return result;
        }
        if (left) XString_delete_base(left);
        if (right) XString_delete_base(right);
    }
    if (xsqlite_is_escaped(identifier)) return XString_sliced_2(identifier, 1, length - 2);
    result = XString_create_copy(identifier);
    return result;
}

static bool VXSqliteDriver_cancelQuery(XSqlDriver* base)
{
    (void)base;
    return false;
}

bool XSqliteDriver_register(void)
{
    static XSqlDriverCreator* creator;
    if (creator) return true;
    creator = XSqlDriverCreator_create(XSqliteDriver_create);
    if (!creator || !XSqlDatabase_registerSqlDriver_type(XSqlDriverType_Sqlite, &creator->m_parent)) {
        if (creator) XSqlDriverCreator_delete_base(creator);
        creator = NULL;
        return false;
    }
    return true;
}
