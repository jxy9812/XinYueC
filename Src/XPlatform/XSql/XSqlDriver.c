/**
 * @file       XSqlDriver.c
 * @brief      SQL 驱动抽象类实现。
 */
#include "XSqlDriver.h"

#include "XByteArray.h"
#include "XDate.h"
#include "XTime.h"
#include "XDateTime.h"
#include "XSqlResult.h"

#include <limits.h>
#include <string.h>

static bool VXSqlDriver_isOpen(const XSqlDriver* driver);
static bool VXSqlDriver_beginTransaction(XSqlDriver* driver);
static bool VXSqlDriver_commitTransaction(XSqlDriver* driver);
static bool VXSqlDriver_rollbackTransaction(XSqlDriver* driver);
static XStringList* VXSqlDriver_tables(const XSqlDriver* driver, XSqlTableType type);
static XSqlIndex* VXSqlDriver_primaryIndex(const XSqlDriver* driver, const XString* tableName);
static XSqlRecord* VXSqlDriver_record(const XSqlDriver* driver, const XString* tableName);
static XString* VXSqlDriver_formatValue(const XSqlDriver* driver, const XSqlField* field, bool trimStrings);
static XString* VXSqlDriver_escapeIdentifier(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
static XString* VXSqlDriver_sqlStatement(const XSqlDriver* driver, XSqlStatementType type,
                                         const XString* tableName, const XSqlRecord* record,
                                         bool preparedStatement);
static void* VXSqlDriver_handle(const XSqlDriver* driver);
static bool VXSqlDriver_hasFeature(const XSqlDriver* driver, XSqlDriverFeature feature);
static void VXSqlDriver_close(XSqlDriver* driver);
static XSqlResult* VXSqlDriver_createResult(const XSqlDriver* driver);
static bool VXSqlDriver_open(XSqlDriver* driver, const XString* database,
                             const XString* user, const XString* password,
                             const XString* host, int port, const XString* options);
static bool VXSqlDriver_subscribe(XSqlDriver* driver, const XString* name);
static bool VXSqlDriver_unsubscribe(XSqlDriver* driver, const XString* name);
static XStringList* VXSqlDriver_subscribed(const XSqlDriver* driver);
static bool VXSqlDriver_isIdentifierEscaped(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
static XString* VXSqlDriver_stripDelimiters(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
static bool VXSqlDriver_cancelQuery(XSqlDriver* driver);
static int VXSqlDriver_maximumIdentifierLength(const XSqlDriver* driver, XSqlIdentifierType type);
static void VXSqlDriver_setOpen(XSqlDriver* driver, bool open);
static void VXSqlDriver_setOpenError(XSqlDriver* driver, bool error);
static void VXSqlDriver_setLastError(XSqlDriver* driver, const XSqlError* error);
static void VXSqlDriver_deinit(XSqlDriver* driver);

XVtable* XSqlDriver_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlDriver)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXSqlDriver_isOpen, VXSqlDriver_beginTransaction,
        VXSqlDriver_commitTransaction, VXSqlDriver_rollbackTransaction,
        VXSqlDriver_tables, VXSqlDriver_primaryIndex, VXSqlDriver_record,
        VXSqlDriver_formatValue, VXSqlDriver_escapeIdentifier,
        VXSqlDriver_sqlStatement, VXSqlDriver_handle, VXSqlDriver_hasFeature,
        VXSqlDriver_close, VXSqlDriver_createResult, VXSqlDriver_open,
        VXSqlDriver_subscribe, VXSqlDriver_unsubscribe, VXSqlDriver_subscribed,
        VXSqlDriver_isIdentifierEscaped, VXSqlDriver_stripDelimiters,
        VXSqlDriver_cancelQuery, VXSqlDriver_maximumIdentifierLength,
        VXSqlDriver_setOpen, VXSqlDriver_setOpenError, VXSqlDriver_setLastError
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlDriver_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlDriver_init(XSqlDriver* driver, XSqlDriverType driverType, XSqlDbmsType dbmsType)
{
    if (!driver) return;
    memset(((unsigned char*)driver) + sizeof(XObject), 0, sizeof(*driver) - sizeof(XObject));
    XObject_init((XObject*)driver);
    XClassSetVtable(driver, XSqlDriver);
    driver->m_driverType = driverType;
    driver->m_dbmsType = dbmsType;
    XSqlError_init(&driver->m_lastError);
    driver->m_precisionPolicy = XSqlNumericalPrecisionPolicy_LowPrecisionDouble;
}

XSqlDriver* XSqlDriver_create(XSqlDriverType driverType, XSqlDbmsType dbmsType)
{
    XSqlDriver* driver = (XSqlDriver*)XMalloc_System(sizeof(XSqlDriver));
    if (!driver) return NULL;
    memset(driver, 0, sizeof(*driver));
    XSqlDriver_init(driver, driverType, dbmsType);
    Set_Class_MemoryFree(driver, XFree_System);
    return driver;
}

static void VXSqlDriver_deinit(XSqlDriver* driver)
{
    if (!driver) return;
    if (driver->m_open) XSqlDriver_close_base(driver);
    XSqlError_deinit_base(&driver->m_lastError);
    XClass_Deinit_Parent(XObject, driver);
}

XSqlDriverType XSqlDriver_driverType(const XSqlDriver* driver) { return driver ? driver->m_driverType : XSqlDriverType_Unknown; }
XSqlDbmsType XSqlDriver_dbmsType(const XSqlDriver* driver) { return driver ? driver->m_dbmsType : XSqlDbmsType_Unknown; }
bool XSqlDriver_isOpen(const XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_IsOpen, bool(*)(const XSqlDriver*))(driver); }
bool XSqlDriver_isOpenError(const XSqlDriver* driver) { return driver ? driver->m_openError : true; }
XSqlError* XSqlDriver_lastError(const XSqlDriver* driver) { return driver ? XSqlError_create_copy(&driver->m_lastError) : XSqlError_create(NULL, NULL, XSqlErrorType_UnknownError, NULL); }

bool XSqlDriver_beginTransaction_base(XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_BeginTransaction, bool(*)(XSqlDriver*))(driver); }
bool XSqlDriver_commitTransaction_base(XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_CommitTransaction, bool(*)(XSqlDriver*))(driver); }
bool XSqlDriver_rollbackTransaction_base(XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_RollbackTransaction, bool(*)(XSqlDriver*))(driver); }
XStringList* XSqlDriver_tables_base(const XSqlDriver* driver, XSqlTableType type) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_Tables, XStringList*(*)(const XSqlDriver*, XSqlTableType))(driver, type) : XStringList_create(); }
XSqlIndex* XSqlDriver_primaryIndex_base(const XSqlDriver* driver, const XString* tableName) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_PrimaryIndex, XSqlIndex*(*)(const XSqlDriver*, const XString*))(driver, tableName) : XSqlIndex_create(); }
XSqlRecord* XSqlDriver_record_base(const XSqlDriver* driver, const XString* tableName) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_Record, XSqlRecord*(*)(const XSqlDriver*, const XString*))(driver, tableName) : XSqlRecord_create(); }
XString* XSqlDriver_formatValue_base(const XSqlDriver* driver, const XSqlField* field, bool trimStrings) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_FormatValue, XString*(*)(const XSqlDriver*, const XSqlField*, bool))(driver, field, trimStrings) : XString_create(); }
XString* XSqlDriver_escapeIdentifier_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_EscapeIdentifier, XString*(*)(const XSqlDriver*, const XString*, XSqlIdentifierType))(driver, identifier, type) : XString_create(); }
XString* XSqlDriver_sqlStatement_base(const XSqlDriver* driver, XSqlStatementType type, const XString* tableName, const XSqlRecord* record, bool preparedStatement) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_SqlStatement, XString*(*)(const XSqlDriver*, XSqlStatementType, const XString*, const XSqlRecord*, bool))(driver, type, tableName, record, preparedStatement) : XString_create(); }
void* XSqlDriver_handle_base(const XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_Handle, void*(*)(const XSqlDriver*))(driver) : NULL; }
bool XSqlDriver_hasFeature_base(const XSqlDriver* driver, XSqlDriverFeature feature) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_HasFeature, bool(*)(const XSqlDriver*, XSqlDriverFeature))(driver, feature); }
void XSqlDriver_close_base(XSqlDriver* driver) { if (driver && !XClassIsVtableNull(driver)) XClassGetVirtualFunc(driver, EXSqlDriver_Close, void(*)(XSqlDriver*))(driver); }
XSqlResult* XSqlDriver_createResult_base(const XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_CreateResult, XSqlResult*(*)(const XSqlDriver*))(driver) : NULL; }
bool XSqlDriver_open_base(XSqlDriver* driver, const XString* database, const XString* user, const XString* password, const XString* host, int port, const XString* options) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_Open, bool(*)(XSqlDriver*, const XString*, const XString*, const XString*, const XString*, int, const XString*))(driver, database, user, password, host, port, options); }
bool XSqlDriver_subscribeToNotification_base(XSqlDriver* driver, const XString* name) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_Subscribe, bool(*)(XSqlDriver*, const XString*))(driver, name); }
bool XSqlDriver_unsubscribeFromNotification_base(XSqlDriver* driver, const XString* name) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_Unsubscribe, bool(*)(XSqlDriver*, const XString*))(driver, name); }
XStringList* XSqlDriver_subscribedToNotifications_base(const XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_Subscribed, XStringList*(*)(const XSqlDriver*))(driver) : XStringList_create(); }
bool XSqlDriver_isIdentifierEscaped_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_IsIdentifierEscaped, bool(*)(const XSqlDriver*, const XString*, XSqlIdentifierType))(driver, identifier, type); }
XString* XSqlDriver_stripDelimiters_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_StripDelimiters, XString*(*)(const XSqlDriver*, const XString*, XSqlIdentifierType))(driver, identifier, type) : XString_create(); }
void XSqlDriver_setNumericalPrecisionPolicy(XSqlDriver* driver, XSqlNumericalPrecisionPolicy policy) { if (driver) driver->m_precisionPolicy = policy; }
XSqlNumericalPrecisionPolicy XSqlDriver_numericalPrecisionPolicy(const XSqlDriver* driver) { return driver ? driver->m_precisionPolicy : XSqlNumericalPrecisionPolicy_HighPrecision; }
bool XSqlDriver_cancelQuery_base(XSqlDriver* driver) { return driver && !XClassIsVtableNull(driver) && XClassGetVirtualFunc(driver, EXSqlDriver_CancelQuery, bool(*)(XSqlDriver*))(driver); }
int XSqlDriver_maximumIdentifierLength_base(const XSqlDriver* driver, XSqlIdentifierType type) { return driver && !XClassIsVtableNull(driver) ? XClassGetVirtualFunc(driver, EXSqlDriver_MaximumIdentifierLength, int(*)(const XSqlDriver*, XSqlIdentifierType))(driver, type) : -1; }

void XSqlDriver_setOpen(XSqlDriver* driver, bool open) { if (driver && !XClassIsVtableNull(driver)) XClassGetVirtualFunc(driver, EXSqlDriver_SetOpen, void(*)(XSqlDriver*, bool))(driver, open); }
void XSqlDriver_setOpenError(XSqlDriver* driver, bool error) { if (driver && !XClassIsVtableNull(driver)) XClassGetVirtualFunc(driver, EXSqlDriver_SetOpenError, void(*)(XSqlDriver*, bool))(driver, error); }
void XSqlDriver_setLastError(XSqlDriver* driver, const XSqlError* error) { if (driver && !XClassIsVtableNull(driver)) XClassGetVirtualFunc(driver, EXSqlDriver_SetLastError, void(*)(XSqlDriver*, const XSqlError*))(driver, error); }

static bool VXSqlDriver_isOpen(const XSqlDriver* driver) { return driver && driver->m_open; }
static bool VXSqlDriver_beginTransaction(XSqlDriver* driver) { (void)driver; return false; }
static bool VXSqlDriver_commitTransaction(XSqlDriver* driver) { (void)driver; return false; }
static bool VXSqlDriver_rollbackTransaction(XSqlDriver* driver) { (void)driver; return false; }
static XStringList* VXSqlDriver_tables(const XSqlDriver* driver, XSqlTableType type) { (void)driver; (void)type; return XStringList_create(); }
static XSqlIndex* VXSqlDriver_primaryIndex(const XSqlDriver* driver, const XString* tableName) { (void)driver; (void)tableName; return XSqlIndex_create(); }
static XSqlRecord* VXSqlDriver_record(const XSqlDriver* driver, const XString* tableName) { (void)driver; (void)tableName; return XSqlRecord_create(); }

static XString* xsql_driver_value_text(const XVariant* value)
{
    XByteArray* bytes;
    XString* result;
    int type;
    if (!value) return XString_create();
    type = XVariant_type((XVariant*)value);
    switch (type) {
    case XVariantType_String:
        return XVariant_toString(value);
    case XVariantType_ByteArray:
        bytes = XByteArray_fromVariant_ref(value);
        result = XString_create_utf8("'");
        if (bytes && result) {
            const uint8_t* data = XByteArray_data(bytes);
            for (size_t i = 0; i < XByteArray_size_base(bytes); ++i) {
                char hex[3];
                static const char digits[] = "0123456789abcdef";
                hex[0] = digits[data[i] >> 4];
                hex[1] = digits[data[i] & 0x0f];
                hex[2] = 0;
                XString_append_with_length_utf8(result, hex, 2);
            }
        }
        if (result) XString_append_utf8(result, "'");
        return result;
    case XVariantType_Bool:
        return XString_create_utf8(XVariant_toBool(value) ? "1" : "0");
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
    case XVariantType_Size_t:
        return XString_create_fmt_utf8("%llu", (unsigned long long)XVariant_toUint64(value));
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Int64:
    case XVariantType_Int:
    case XVariantType_Char:
    case XVariantType_UChar:
        return XString_create_fmt_utf8("%lld", (long long)XVariant_toInt64(value));
    case XVariantType_Float:
    case XVariantType_Double:
        return XString_create_fmt_utf8("%.17g", XVariant_toDouble(value));
    case XVariantType_Date: {
        const XDate* date = XVariant_toDate_ref(value);
        return date && XDate_isValid(date)
            ? XString_create_fmt_utf8("%04d-%02d-%02d", XDate_year(date),
                                      XDate_month(date), XDate_day(date)) : XString_create();
    }
    case XVariantType_Time: {
        const XTime* time = XVariant_toTime_ref(value);
        return time && XTime_isValid(time)
            ? XString_create_fmt_utf8("%02d:%02d:%02d.%03d", XTime_hour(time),
                                      XTime_minute(time), XTime_second(time), XTime_msec(time)) : XString_create();
    }
    case XVariantType_DateTime: {
        const XDateTime* datetime = XVariant_toDateTime_ref(value);
        const XDate* date = datetime ? &datetime->m_date : NULL;
        const XTime* time = datetime ? &datetime->m_time : NULL;
        return datetime && XDateTime_isValid(datetime)
            ? XString_create_fmt_utf8("%04d-%02d-%02dT%02d:%02d:%02d.%03d",
                                      XDate_year(date), XDate_month(date), XDate_day(date),
                                      XTime_hour(time), XTime_minute(time),
                                      XTime_second(time), XTime_msec(time)) : XString_create();
    }
    default:
        return XString_create();
    }
}

static XString* xsql_driver_trim_right(const XString* text)
{
    size_t length;
    if (!text) return XString_create();
    length = XString_length_base(text);
    while (length > 0 && XChar_isSpace(XString_at(text, length - 1))) --length;
    return XString_sliced_2(text, 0, length);
}

static XString* VXSqlDriver_formatValue(const XSqlDriver* driver, const XSqlField* field, bool trimStrings)
{
    int valueType;
    if (!field || XSqlField_isNull(field)) return XString_create_utf8("NULL");
    XVariant* value = XSqlField_value(field);
    valueType = value ? XVariant_type(value) : XVariantType_NULL;
    if (valueType == XVariantType_Date || valueType == XVariantType_Time
        || valueType == XVariantType_DateTime) {
        bool valid = valueType == XVariantType_Date
            ? XDate_isValid(XVariant_toDate_ref(value))
            : valueType == XVariantType_Time
                ? XTime_isValid(XVariant_toTime_ref(value))
                : XDateTime_isValid(XVariant_toDateTime_ref(value));
        if (!valid) {
            if (value) XVariant_delete_base(value);
            return XString_create_utf8("NULL");
        }
    }
    XString* text = xsql_driver_value_text(value);
    bool blob = valueType == XVariantType_ByteArray;
    if (blob && !XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_Blob)) {
        XString* raw = XVariant_toString(value);
        if (text) XString_delete_base(text);
        if (value) XVariant_delete_base(value);
        return raw;
    }
    bool quote = value && (XVariant_type(value) == XVariantType_String
        || XVariant_type(value) == XVariantType_ByteArray
        || XVariant_type(value) == XVariantType_Date
        || XVariant_type(value) == XVariantType_Time
        || XVariant_type(value) == XVariantType_DateTime);
    if (blob) {
        if (value) XVariant_delete_base(value);
        return text;
    }
    XString* result = XString_create();
    if (text && trimStrings && valueType == XVariantType_String) {
        XString* trimmed = xsql_driver_trim_right(text);
        if (trimmed) { XString_delete_base(text); text = trimmed; }
    }
    if (quote) XString_append_utf8(result, "'");
    if (text) { if (quote) XString_replace_utf8(text, "'", "''", XChar_CaseSensitive); XString_append(result, text); }
    if (quote) XString_append_utf8(result, "'");
    if (text) XString_delete_base(text);
    if (value) XVariant_delete_base(value);
    return result;
}
static XString* VXSqlDriver_escapeIdentifier(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type)
{
    (void)driver; (void)type;
    return identifier ? XString_create_copy(identifier) : XString_create();
}
static XString* VXSqlDriver_sqlStatement(const XSqlDriver* driver, XSqlStatementType type, const XString* tableName, const XSqlRecord* record, bool preparedStatement)
{
    if (!tableName) return XString_create();
    XString* table = XSqlDriver_escapeIdentifier_base(driver, tableName, XSqlIdentifierType_TableName);
    XString* sql = XString_create();
    if (!table || !sql) { if (table) XString_delete_base(table); if (sql) XString_delete_base(sql); return NULL; }
    if (type == XSqlStatementType_WhereStatement) {
        int bound = 0;
        for (int i = 0; record && i < XSqlRecord_count(record); ++i) {
            XSqlField* field;
            XString* name;
            XString* escaped;
            XString* value;
            if (!XSqlRecord_isGenerated(record, i)) continue;
            field = XSqlRecord_field(record, i);
            name = XSqlRecord_fieldName(record, i);
            escaped = XSqlDriver_escapeIdentifier_base(driver, name, XSqlIdentifierType_FieldName);
            XString_append_utf8(sql, bound++ ? " AND " : " WHERE ");
            if (table && XString_length_base(table) > 0) {
                XString_append(sql, table);
                XString_append_utf8(sql, ".");
            }
            if (escaped) XString_append(sql, escaped);
            if (field && XSqlField_isNull(field)) {
                XString_append_utf8(sql, " IS NULL");
            } else if (preparedStatement) {
                XString_append_utf8(sql, " = ?");
            } else {
                value = XSqlDriver_formatValue_base(driver, field, false);
                XString_append_utf8(sql, " = ");
                if (value) XString_append(sql, value);
                if (value) XString_delete_base(value);
            }
            if (escaped) XString_delete_base(escaped);
            if (name) XString_delete_base(name);
            if (field) XSqlField_delete_base(field);
        }
        if (!bound) XString_clear_base(sql);
    } else if (type == XSqlStatementType_SelectStatement) {
        int selected = 0;
        for (int i = 0; record && i < XSqlRecord_count(record); ++i) {
            XString* name;
            XString* escaped;
            if (!XSqlRecord_isGenerated(record, i)) continue;
            name = XSqlRecord_fieldName(record, i);
            escaped = XSqlDriver_escapeIdentifier_base(driver, name, XSqlIdentifierType_FieldName);
            if (selected++) XString_append_utf8(sql, ", ");
            if (escaped) XString_append(sql, escaped);
            if (escaped) XString_delete_base(escaped);
            if (name) XString_delete_base(name);
        }
        if (selected) {
            XString* fields = XString_create_copy(sql);
            XString_clear_base(sql);
            XString_append_utf8(sql, "SELECT ");
            if (fields) {
                XString_append(sql, fields);
                XString_delete_base(fields);
            }
            XString_append_utf8(sql, " FROM ");
            XString_append(sql, table);
        }
    }
    else if (type == XSqlStatementType_DeleteStatement) { XString_append_utf8(sql, "DELETE FROM "); XString_append(sql, table); }
    else if (type == XSqlStatementType_InsertStatement) {
        XString_append_utf8(sql, "INSERT INTO "); XString_append(sql, table); XString_append_utf8(sql, " (");
        int bound = 0;
        for (int i = 0; record && i < XSqlRecord_count(record); ++i) if (XSqlRecord_isGenerated(record, i)) { XString* n = XSqlRecord_fieldName(record, i); XString* e = XSqlDriver_escapeIdentifier_base(driver, n, XSqlIdentifierType_FieldName); if (bound++) XString_append_utf8(sql, ", "); if (e) XString_append(sql, e); if (e) XString_delete_base(e); if (n) XString_delete_base(n); }
        if (!bound) { XString_clear_base(sql); XString_delete_base(table); return sql; }
        XString_append_utf8(sql, ") VALUES ("); bound = 0;
        for (int i = 0; record && i < XSqlRecord_count(record); ++i) if (XSqlRecord_isGenerated(record, i)) { if (bound++) XString_append_utf8(sql, ", "); if (preparedStatement) XString_append_utf8(sql, "?"); else { XSqlField* f = XSqlRecord_field(record, i); XString* v = VXSqlDriver_formatValue(driver, f, false); if (v) XString_append(sql, v); if (v) XString_delete_base(v); if (f) XSqlField_delete_base(f); } }
        XString_append_utf8(sql, ")");
    } else if (type == XSqlStatementType_UpdateStatement) {
        XString_append_utf8(sql, "UPDATE "); XString_append(sql, table); XString_append_utf8(sql, " SET "); int bound = 0;
        for (int i = 0; record && i < XSqlRecord_count(record); ++i) if (XSqlRecord_isGenerated(record, i)) { XString* n = XSqlRecord_fieldName(record, i); XString* e = XSqlDriver_escapeIdentifier_base(driver, n, XSqlIdentifierType_FieldName); XSqlField* f = XSqlRecord_field(record, i); if (bound++) XString_append_utf8(sql, ", "); if (e) XString_append(sql, e); if (preparedStatement) XString_append_utf8(sql, "=?"); else { XString* v = XSqlDriver_formatValue_base(driver, f, false); XString_append_utf8(sql, "="); if (v) XString_append(sql, v); if (v) XString_delete_base(v); } if (f) XSqlField_delete_base(f); if (e) XString_delete_base(e); if (n) XString_delete_base(n); }
        if (!bound) XString_clear_base(sql);
    }
    XString_delete_base(table);
    return sql;
}
static void* VXSqlDriver_handle(const XSqlDriver* driver) { (void)driver; return NULL; }
static bool VXSqlDriver_hasFeature(const XSqlDriver* driver, XSqlDriverFeature feature) { (void)driver; (void)feature; return false; }
static void VXSqlDriver_close(XSqlDriver* driver) { if (driver) { driver->m_open = false; driver->m_openError = false; } }
static XSqlResult* VXSqlDriver_createResult(const XSqlDriver* driver) { return XSqlResult_create(driver); }
static bool VXSqlDriver_open(XSqlDriver* driver, const XString* database, const XString* user, const XString* password, const XString* host, int port, const XString* options)
{
    (void)database; (void)user; (void)password; (void)host; (void)port; (void)options;
    if (!driver) return false;
    XSqlError* error = XSqlError_create_utf8("No SQL driver implementation is registered", NULL, XSqlErrorType_ConnectionError, NULL);
    if (error) { XSqlDriver_setLastError(driver, error); XSqlError_delete_base(error); }
    driver->m_open = false; driver->m_openError = true; return false;
}
static bool VXSqlDriver_subscribe(XSqlDriver* driver, const XString* name) { (void)driver; (void)name; return false; }
static bool VXSqlDriver_unsubscribe(XSqlDriver* driver, const XString* name) { (void)driver; (void)name; return false; }
static XStringList* VXSqlDriver_subscribed(const XSqlDriver* driver) { (void)driver; return XStringList_create(); }
static bool VXSqlDriver_isIdentifierEscaped(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type) { size_t length; (void)driver; (void)type; if (!identifier) return false; length = XString_length_base(identifier); return length > 2 && XString_at(identifier, 0) == XChar_fromLatin1('"') && XString_at(identifier, length - 1) == XChar_fromLatin1('"'); }
static XString* VXSqlDriver_stripDelimiters(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type) { (void)type; if (!identifier) return XString_create(); if (VXSqlDriver_isIdentifierEscaped(driver, identifier, type)) return XString_sliced_2(identifier, 1, XString_length_base(identifier) - 2); return XString_create_copy(identifier); }
static bool VXSqlDriver_cancelQuery(XSqlDriver* driver) { (void)driver; return false; }
static int VXSqlDriver_maximumIdentifierLength(const XSqlDriver* driver, XSqlIdentifierType type) { (void)driver; (void)type; return INT_MAX; }
static void VXSqlDriver_setOpen(XSqlDriver* driver, bool open) { if (driver) driver->m_open = open; }
static void VXSqlDriver_setOpenError(XSqlDriver* driver, bool error) { if (driver) { driver->m_openError = error; if (error) driver->m_open = false; } }
static void VXSqlDriver_setLastError(XSqlDriver* driver, const XSqlError* error) { if (driver && error) XSqlError_copy_base(&driver->m_lastError, error); }

void* XSqlDriver_notification_signal(XSqlDriver* driver, const XString* name,
                                     XSqlNotificationSource source,
                                     const XVariant* payload)
{
    XEmitSignal((XObject*)driver, XSqlDriver_notification_signal,
                XVarList_create(6, sizeof(const XString*), &name,
                                sizeof(XSqlNotificationSource), &source,
                                sizeof(const XVariant*), &payload), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}
