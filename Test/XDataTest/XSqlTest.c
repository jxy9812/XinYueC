/**
 * @file       XSqlTest.c
 * @brief      SQL 抽象驱动和公共类回归测试。
 */
#include "XSqlTest.h"
#include "XSqlMySqlTest.h"
#include "XSql.h"
#include "XDateTime.h"
#include "XByteArray.h"
#include "XFile.h"
#include "XAtomic.h"
#include "XThread.h"

#include <stdio.h>
#include <string.h>

typedef struct XSqlTestResult {
    XSqlResult m_parent; /**< SQL 结果基类。 */
} XSqlTestResult;

typedef struct XSqlTestDriver {
    XSqlDriver m_parent; /**< SQL 驱动基类。 */
} XSqlTestDriver;

static bool XSqlTest_resultReset(XSqlResult* result, const XString* query);
static bool XSqlTest_resultFetch(XSqlResult* result, int index);
static int XSqlTest_resultSize(XSqlResult* result);
static XVariant* XSqlTest_resultData(XSqlResult* result, int field);
static bool XSqlTest_driverOpen(XSqlDriver* driver, const XString* database,
                                const XString* user, const XString* password,
                                const XString* host, int port, const XString* options);
static bool XSqlTest_driverHasFeature(const XSqlDriver* driver, XSqlDriverFeature feature);
static XSqlRecord* XSqlTest_driverRecord(const XSqlDriver* driver, const XString* tableName);
static XSqlResult* XSqlTest_driverCreateResult(const XSqlDriver* driver);
static bool XSqlTest_run_sqlite(void);
static bool XSqlTest_run_value_api(const XSqlDatabase* database);
static void XSqlTest_thread_affinity_probe(XThread* thread, XVarList* list);
static XAtomic_bool g_sqlThreadAffinityOk = { false };

XCLASS_DEFINE_BEGING(XSqlTestResult)
XCLASS_DEFINE_EXTEND_END(XSqlTestResult, XSqlResult)

XCLASS_DEFINE_BEGING(XSqlTestDriver)
XCLASS_DEFINE_EXTEND_END(XSqlTestDriver, XSqlDriver)

static XVtable* XSqlTestResult_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlTestResult))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlResult);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Data, XSqlTest_resultData);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Reset, XSqlTest_resultReset);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Fetch, XSqlTest_resultFetch);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlResult_Size, XSqlTest_resultSize);
    return XVTABLE_DEFAULT;
}

static XSqlTestResult* XSqlTestResult_create(const XSqlDriver* driver)
{
    XSqlTestResult* result = (XSqlTestResult*)XMalloc_System(sizeof(XSqlTestResult));
    if (!result) return NULL;
    XSqlResult_init(&result->m_parent, driver);
    XClassSetVtable(result, XSqlTestResult);
    Set_Class_MemoryFree(result, XFree_System);
    return result;
}

static bool XSqlTest_resultReset(XSqlResult* result, const XString* query)
{
    if (!result || !query) return false;
    XSqlResult_clear(result);
    XSqlResult_setQuery_base(result, query);
    XSqlResult_setSelect_base(result, true);
    XSqlResult_setAt_base(result, XSqlLocation_BeforeFirstRow);
    result->m_size = 2;
    XString* idName = XString_create_utf8("id");
    XString* textName = XString_create_utf8("text");
    XSqlField* idField = XSqlField_create_ex(idName, XVariantType_Int32, NULL);
    XSqlField* textField = XSqlField_create_ex(textName, XVariantType_String, NULL);
    bool ok = idField && textField && XSqlRecord_append(&result->m_record, idField)
        && XSqlRecord_append(&result->m_record, textField);
    if (idName) XString_delete_base(idName);
    if (textName) XString_delete_base(textName);
    if (idField) XSqlField_delete_base(idField);
    if (textField) XSqlField_delete_base(textField);
    XSqlResult_setActive_base(result, ok);
    return ok;
}

static bool XSqlTest_resultFetch(XSqlResult* result, int index)
{
    if (!result || index < 0 || index >= 2) {
        if (result) result->m_at = XSqlLocation_AfterLastRow;
        return false;
    }
    result->m_at = index;
    return true;
}

static int XSqlTest_resultSize(XSqlResult* result)
{
    return result && result->m_active ? 2 : -1;
}

static XVariant* XSqlTest_resultData(XSqlResult* result, int field)
{
    if (!result || result->m_at < 0 || result->m_at >= 2) return XVariant_create_null();
    if (field == 0) return XVariant_create_int32(result->m_at + 1);
    if (field == 1) return XVariant_create_utf8_str(result->m_at == 0 ? "alpha" : "beta");
    return XVariant_create_null();
}

static XVtable* XSqlTestDriver_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlTestDriver))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlDriver);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Open, XSqlTest_driverOpen);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_HasFeature, XSqlTest_driverHasFeature);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_Record, XSqlTest_driverRecord);
    XVTABLE_OVERLOAD_DEFAULT(EXSqlDriver_CreateResult, XSqlTest_driverCreateResult);
    return XVTABLE_DEFAULT;
}

static XSqlTestDriver* XSqlTestDriver_create(void)
{
    XSqlTestDriver* driver = (XSqlTestDriver*)XMalloc_System(sizeof(XSqlTestDriver));
    if (!driver) return NULL;
    XSqlDriver_init(&driver->m_parent, XSqlDriverType_Custom, XSqlDbmsType_Sqlite);
    XClassSetVtable(driver, XSqlTestDriver);
    Set_Class_MemoryFree(driver, XFree_System);
    return driver;
}

static bool XSqlTest_driverOpen(XSqlDriver* driver, const XString* database,
                                const XString* user, const XString* password,
                                const XString* host, int port, const XString* options)
{
    (void)database; (void)user; (void)password; (void)host; (void)port; (void)options;
    XSqlDriver_setOpen(driver, true);
    XSqlDriver_setOpenError(driver, false);
    return true;
}

static bool XSqlTest_driverHasFeature(const XSqlDriver* driver, XSqlDriverFeature feature)
{
    (void)driver;
    return feature == XSqlDriverFeature_QuerySize;
}

static XSqlRecord* XSqlTest_driverRecord(const XSqlDriver* driver, const XString* tableName)
{
    XSqlRecord* record = XSqlRecord_create();
    XSqlField* idField;
    XSqlField* textField;
    XString* idName;
    XString* textName;
    (void)driver;
    if (!record || !tableName || !XString_equals_utf8(tableName, "test", XChar_CaseSensitive))
        return record;
    idName = XString_create_utf8("id");
    textName = XString_create_utf8("text");
    idField = XSqlField_create_ex(idName, XVariantType_Int32, NULL);
    textField = XSqlField_create_ex(textName, XVariantType_String, NULL);
    if (!idField || !textField || !XSqlRecord_append(record, idField)
        || !XSqlRecord_append(record, textField)) {
        XSqlRecord_clear(record);
    }
    if (idField) XSqlField_delete_base(idField);
    if (textField) XSqlField_delete_base(textField);
    if (idName) XString_delete_base(idName);
    if (textName) XString_delete_base(textName);
    return record;
}

static XSqlResult* XSqlTest_driverCreateResult(const XSqlDriver* driver)
{
    XSqlTestResult* result = XSqlTestResult_create(driver);
    return result ? &result->m_parent : NULL;
}

static XSqlDriver* XSqlTest_createDriver(void)
{
    XSqlTestDriver* driver = XSqlTestDriver_create();
    return driver ? &driver->m_parent : NULL;
}

static bool XSqlTest_run_value_api(const XSqlDatabase* database)
{
    XSqlError* leftError = XSqlError_create_utf8("left", NULL, XSqlErrorType_StatementError, NULL);
    XSqlError* rightError = XSqlError_create_utf8("right", NULL, XSqlErrorType_ConnectionError, NULL);
    XString* leftName = XString_create_utf8("left_field");
    XString* rightName = XString_create_utf8("right_field");
    XSqlField* leftField = XSqlField_create_ex(leftName, XVariantType_String, NULL);
    XSqlField* rightField = XSqlField_create_ex(rightName, XVariantType_Int64, NULL);
    XSqlRecord* leftRecord = XSqlRecord_create();
    XSqlRecord* rightRecord = XSqlRecord_create();
    XSqlRecord* keyValues = NULL;
    XSqlIndex* leftIndex = XSqlIndex_create_utf8("left_cursor", "left_index");
    XSqlIndex* rightIndex = XSqlIndex_create_utf8("right_cursor", "right_index");
    XSqlQuery* leftQuery = XSqlQuery_create_database(database);
    XSqlQuery* rightQuery = XSqlQuery_create_database(database);
    XSqlDriver* stateDriver = XSqlDriver_create(XSqlDriverType_Custom, XSqlDbmsType_Unknown);
    XString* quotedIdentifier = XString_create_utf8("\"field\"");
    XString* plainIdentifier = XString_create_utf8("field");
    XString* strippedIdentifier = NULL;
    XVariant* boundValue = XVariant_create_int32(7);
    XString* text = NULL;
    bool ok = leftError && rightError && leftName && rightName && leftField && rightField
        && leftRecord && rightRecord && leftIndex && rightIndex && leftQuery && rightQuery
        && boundValue && stateDriver && quotedIdentifier && plainIdentifier;

    if (stateDriver) {
        XSqlDriver_setOpen(stateDriver, true);
        XSqlDriver_setOpenError(stateDriver, true);
        ok = ok && !XSqlDriver_isOpen(stateDriver) && XSqlDriver_isOpenError(stateDriver);
        strippedIdentifier = XSqlDriver_stripDelimiters_base(stateDriver, quotedIdentifier,
                                                              XSqlIdentifierType_FieldName);
        ok = ok && XSqlDriver_isIdentifierEscaped_base(stateDriver, quotedIdentifier,
                                                        XSqlIdentifierType_FieldName)
            && !XSqlDriver_isIdentifierEscaped_base(stateDriver, plainIdentifier,
                                                     XSqlIdentifierType_FieldName)
            && strippedIdentifier
            && XString_equals_utf8(strippedIdentifier, "field", XChar_CaseSensitive);
    }

    if (ok) ok = XSqlRecord_append(leftRecord, leftField)
        && XSqlRecord_append(rightRecord, rightField)
        && XSqlQuery_prepare_utf8(leftQuery, "SELECT :outValue")
        && XSqlQuery_prepare_utf8(rightQuery, "SELECT ?")
        && XSqlQuery_result(leftQuery) && XSqlQuery_result(rightQuery);
    if (ok) {
        XVariant* keyValue = XVariant_create_int32(11);
        XSqlRecord_setValue(leftRecord, 0, keyValue);
        keyValues = XSqlRecord_keyValues(leftRecord, leftRecord);
        ok = keyValue && keyValues && XSqlRecord_equals(leftRecord, keyValues);
        if (keyValue) XVariant_delete_base(keyValue);
        XSqlField_setValue(rightField, NULL);
        ok = ok && XSqlField_isNull(rightField) && XSqlField_isValid(rightField);
        {
            XVariant* readOnlyValue = XVariant_create_int64(11);
            XVariant* retainedValue;
            XSqlField_setValue(rightField, readOnlyValue);
            XSqlField_setReadOnly(rightField, true);
            XSqlField_setValue(rightField, boundValue);
            XSqlField_clear(rightField);
            retainedValue = XSqlField_value(rightField);
            XSqlField_setSqlType(rightField, 42);
            ok = ok && retainedValue && XVariant_toInt64(retainedValue) == 11
                && XSqlField_typeId(rightField) == 42
                && XSqlField_typeID(rightField) == 42;
            XSqlField_setSqlType(leftField, 42);
            ok = ok && !XSqlField_equals(leftField, rightField);
            XSqlField_setSqlType(leftField, -1);
            if (retainedValue) XVariant_delete_base(retainedValue);
            if (readOnlyValue) XVariant_delete_base(readOnlyValue);
            XSqlField_setReadOnly(rightField, false);
        }
    }
    if (ok) {
        XSqlQuery_bindValue_utf8(leftQuery, ":outValue", boundValue, XSqlParamType_Out);
        XSqlQuery_bindValue(rightQuery, 0, boundValue, XSqlParamType_InOut);
        XSqlError_swap(leftError, rightError);
        XSqlField_swap(leftField, rightField);
        XSqlRecord_swap(leftRecord, rightRecord);
        XSqlIndex_swap(leftIndex, rightIndex);
        XSqlQuery_swap(leftQuery, rightQuery);
        text = XSqlError_driverText(leftError);
        ok = text && XString_equals_utf8(text, "right", XChar_CaseSensitive);
        if (text) { XString_delete_base(text); text = NULL; }
        text = XSqlField_name(leftField);
        ok = ok && text && XString_equals_utf8(text, "right_field", XChar_CaseSensitive);
        if (text) { XString_delete_base(text); text = NULL; }
        text = XSqlRecord_fieldName(leftRecord, 0);
        ok = ok && text && XString_equals_utf8(text, "right_field", XChar_CaseSensitive);
        if (text) { XString_delete_base(text); text = NULL; }
        text = XSqlIndex_name(leftIndex);
        ok = ok && text && XString_equals_utf8(text, "right_index", XChar_CaseSensitive);
        if (text) { XString_delete_base(text); text = NULL; }
        ok = ok && XSqlResult_bindValueType(XSqlQuery_result(leftQuery), 0) == XSqlParamType_InOut
            && XSqlResult_bindValueType_utf8(XSqlQuery_result(rightQuery), ":outValue") == XSqlParamType_Out;
    }

    if (text) XString_delete_base(text);
    if (boundValue) XVariant_delete_base(boundValue);
    if (leftQuery) XSqlQuery_delete_base(leftQuery);
    if (rightQuery) XSqlQuery_delete_base(rightQuery);
    if (leftIndex) XSqlIndex_delete_base(leftIndex);
    if (rightIndex) XSqlIndex_delete_base(rightIndex);
    if (leftRecord) XSqlRecord_delete_base(leftRecord);
    if (rightRecord) XSqlRecord_delete_base(rightRecord);
    if (keyValues) XSqlRecord_delete_base(keyValues);
    if (leftField) XSqlField_delete_base(leftField);
    if (rightField) XSqlField_delete_base(rightField);
    if (leftName) XString_delete_base(leftName);
    if (rightName) XString_delete_base(rightName);
    if (leftError) XSqlError_delete_base(leftError);
    if (rightError) XSqlError_delete_base(rightError);
    if (strippedIdentifier) XString_delete_base(strippedIdentifier);
    if (quotedIdentifier) XString_delete_base(quotedIdentifier);
    if (plainIdentifier) XString_delete_base(plainIdentifier);
    if (stateDriver) XSqlDriver_delete_base(stateDriver);
    return ok;
}

int XSqlTest_run(void)
{
    XStringList* builtinDrivers = XSqlDatabase_drivers();
    bool builtinDriversOk = builtinDrivers
        && XStringList_contains_utf8(builtinDrivers, "QSQLITE", XChar_CaseSensitive)
        && XStringList_contains_utf8(builtinDrivers, "QMYSQL", XChar_CaseSensitive);
    if (builtinDrivers) XStringList_delete_base(builtinDrivers);
    if (!builtinDriversOk) return 1;
    XSqlDriverCreator* creator = XSqlDriverCreator_create(XSqlTest_createDriver);
    if (!creator || !XSqlDatabase_registerSqlDriver_type(XSqlDriverType_Custom, &creator->m_parent)) {
        if (creator) XSqlDriverCreator_delete_base(creator);
        return 1;
    }
    XSqlDatabase* database = XSqlDatabase_addDatabase(XSqlDriverType_Custom, "xsql-test");
    if (!database) return 2;
    if (!XSqlDatabase_open(database)) { XSqlDatabase_delete_base(database); return 2; }
    XSqlQuery* query = XSqlDatabase_exec_utf8(database, "  SELECT id, text FROM test  ");
    if (!query || !XSqlQuery_isActive(query) || XSqlQuery_size(query) != 2 || !XSqlQuery_first(query)) {
        if (query) XSqlQuery_delete_base(query);
        XSqlDatabase_delete_base(database);
        return 3;
    }
    bool validPositionOk = XSqlQuery_isValid(query);
    XSqlResult_setActive_base(XSqlQuery_result(query), false);
    validPositionOk = validPositionOk && XSqlQuery_isValid(query);
    XSqlResult_setActive_base(XSqlQuery_result(query), true);
    if (!validPositionOk) {
        XSqlQuery_delete_base(query);
        XSqlDatabase_removeDatabase("xsql-test");
        XSqlDatabase_delete_base(database);
        return 3;
    }
    XVariant* id = XSqlQuery_value_utf8(query, "id");
    XVariant* text = XSqlQuery_value_utf8(query, "text");
    XString* textValue = text ? XVariant_toString(text) : NULL;
    XString* executedQuery = XSqlQuery_executedQuery(query);
    bool valuesOk = id && textValue && executedQuery && XVariant_toInt32(id) == 1
        && XString_equals_utf8(textValue, "alpha", XChar_CaseSensitive)
        && XString_equals_utf8(executedQuery, "SELECT id, text FROM test", XChar_CaseSensitive);
    if (id) XVariant_delete_base(id);
    if (text) XVariant_delete_base(text);
    if (textValue) XString_delete_base(textValue);
    if (executedQuery) XString_delete_base(executedQuery);
    XSqlQueryModel* queryModel = XSqlQueryModel_create();
    bool queryModelSet = queryModel && XSqlQueryModel_setQuery_utf8(queryModel, "SELECT id, text FROM test", database);
    bool modelOk = queryModelSet
        && XSqlQueryModel_rowCount(queryModel) == 2 && XSqlQueryModel_columnCount(queryModel) == 2;
    XVariant* invalidRole = queryModel ? XSqlQueryModel_data(queryModel, 0, 0,
                                                               XSqlItemDataRole_User) : NULL;
    XVariant* verticalHeader = queryModel ? XSqlQueryModel_headerData(queryModel, 0,
                                                                        XSqlOrientation_Vertical,
                                                                        XSqlItemDataRole_Display) : NULL;
    XStringList* queryRoles = queryModel ? XSqlQueryModel_roleNames(queryModel) : NULL;
    modelOk = modelOk && invalidRole && !XVariant_isValid(invalidRole)
        && verticalHeader && XVariant_toInt32(verticalHeader) == 1
        && queryRoles && XStringList_size_base(queryRoles) == 1;
    if (invalidRole) XVariant_delete_base(invalidRole);
    if (verticalHeader) XVariant_delete_base(verticalHeader);
    if (queryRoles) XStringList_delete_base(queryRoles);
    bool inserted = modelOk && XSqlQueryModel_insertColumns(queryModel, 1, 1)
        && XSqlQueryModel_columnCount(queryModel) == 3;
    XVariant* insertedColumn = inserted ? XSqlQueryModel_data(queryModel, 0, 1,
                                                               XSqlItemDataRole_Display) : NULL;
    modelOk = inserted && insertedColumn && !XVariant_isValid(insertedColumn)
        && XSqlQueryModel_removeColumns(queryModel, 1, 1)
        && XSqlQueryModel_columnCount(queryModel) == 2;
    if (insertedColumn) XVariant_delete_base(insertedColumn);
    XVariant* editHeader = XVariant_create_utf8_str("编辑列");
    XVariant* userHeader = XVariant_create_utf8_str("自定义列");
    bool headerRoles = queryModel && editHeader && userHeader
        && XSqlQueryModel_setHeaderData(queryModel, 1, XSqlOrientation_Horizontal,
                                        editHeader, XSqlItemDataRole_Edit)
        && XSqlQueryModel_setHeaderData(queryModel, 1, XSqlOrientation_Horizontal,
                                        userHeader, XSqlItemDataRole_User);
    XVariant* displayHeader = headerRoles ? XSqlQueryModel_headerData(
        queryModel, 1, XSqlOrientation_Horizontal, XSqlItemDataRole_Display) : NULL;
    XVariant* returnedUserHeader = headerRoles ? XSqlQueryModel_headerData(
        queryModel, 1, XSqlOrientation_Horizontal, XSqlItemDataRole_User) : NULL;
    XString* displayHeaderText = displayHeader ? XVariant_toString(displayHeader) : NULL;
    XString* returnedUserHeaderText = returnedUserHeader ? XVariant_toString(returnedUserHeader) : NULL;
    headerRoles = headerRoles && displayHeader && returnedUserHeader
        && displayHeaderText && returnedUserHeaderText
        && XString_equals_utf8(displayHeaderText, "编辑列", XChar_CaseSensitive)
        && XString_equals_utf8(returnedUserHeaderText, "自定义列", XChar_CaseSensitive);
    if (displayHeaderText) XString_delete_base(displayHeaderText);
    if (returnedUserHeaderText) XString_delete_base(returnedUserHeaderText);
    if (displayHeader) XVariant_delete_base(displayHeader);
    if (returnedUserHeader) XVariant_delete_base(returnedUserHeader);
    if (editHeader) XVariant_delete_base(editHeader);
    if (userHeader) XVariant_delete_base(userHeader);
    modelOk = modelOk && headerRoles;
    XSqlRelation* relation = XSqlRelation_create_utf8("test", "id", "text");
    XSqlRelationalTableModel* relationalModel = XSqlRelationalTableModel_create(database);
    bool relationalOk = true;
    if (relationalModel && relation) {
        XSqlRelationalTableModel_setTable_utf8(relationalModel, "test");
        XSqlRelationalTableModel_setRelation(relationalModel, 0, relation);
        bool relationalSelect = XSqlRelationalTableModel_select(relationalModel);
        relationalOk = relationalSelect;
        XSqlTableModel* relationData = XSqlRelationalTableModel_relationModel(relationalModel, 0);
        XSqlTableModel* relationDataAgain = XSqlRelationalTableModel_relationModel(relationalModel, 0);
        relationalOk = relationalOk && relationData && relationData == relationDataAgain;
        XVariant* relationKey = XVariant_create_int32(2);
        bool relationEdit = relationKey && XSqlRelationalTableModel_setData(
            relationalModel, 0, 0, relationKey, XSqlItemDataRole_Edit);
        XVariant* display = XSqlRelationalTableModel_data(relationalModel, 0, 0, XSqlItemDataRole_Display);
        XString* displayText = display ? XVariant_toString(display) : NULL;
        relationalOk = relationalOk && relationEdit && displayText
            && XString_equals_utf8(displayText, "beta", XChar_CaseSensitive);
        modelOk = modelOk && relationalOk;
        if (relationKey) XVariant_delete_base(relationKey);
        if (displayText) XString_delete_base(displayText);
        if (display) XVariant_delete_base(display);
    } else {
        relationalOk = false;
        modelOk = false;
    }
    if (relationalModel) XSqlRelationalTableModel_delete_base(relationalModel);
    if (relation) XSqlRelation_delete_base(relation);
    if (queryModel) XSqlQueryModel_delete_base(queryModel);
    bool valueApiOk = XSqlTest_run_value_api(database);
    XSqlQuery_delete_base(query);
    XSqlDatabase_removeDatabase("xsql-test");
    XSqlDatabase_delete_base(database);
    if (!valuesOk || !modelOk || !valueApiOk) {
        return 4;
    }
    if (!XSqlMySqlTest_run()) return 5;
    if (!XSqlTest_run_sqlite()) return 6;
    printf("XSqlTest 测试通过\n");
    return 0;
}

static bool XSqlTest_run_sqlite(void)
{
    XSqlDatabase* database = XSqlDatabase_addDatabase(XSqlDriverType_Sqlite, "xsql-sqlite");
    XSqlDatabase* openedDatabase = NULL;
    XSqlQuery* query = NULL;
    XString* databaseName = XString_create_utf8("xsql-test.sqlite");
    XString* walName = XString_create_utf8("xsql-test.sqlite-wal");
    XString* shmName = XString_create_utf8("xsql-test.sqlite-shm");
    XVariant* value = NULL;
    XVariant* nameValue = NULL;
    XByteArray* blob = NULL;
    XSqlRecord* record = NULL;
    XSqlIndex* primaryIndex = NULL;
    XSqlField* metadataField = NULL;
    XSqlQueryModel* batchModel = NULL;
    XSqlQueryModel* copyModel = NULL;
    XSqlQuery* modelQuery = NULL;
    XSqlTableModel* tableModel = NULL;
    XSqlRecord* modelRecord = NULL;
    XStringList* tables = NULL;
    XThread* databaseThread = NULL;
    XSqlDriver* sqliteDriver = NULL;
    XString* notificationName = NULL;
    XStringList* notificationNames = NULL;
    XStringList* notificationNamesAfterClose = NULL;
    XString* peopleName = NULL;
    XString* nameField = NULL;
    XString* nameText = NULL;
    XString* filterText = NULL;
    XString* connectionName = NULL;
    bool notificationSubscribed = false;
    bool ok = database && databaseName && walName && shmName;

    XFile_remove_static(databaseName);
    XFile_remove_static(walName);
    XFile_remove_static(shmName);
    if (ok) XSqlDatabase_setDatabaseName(database, databaseName);
    ok = ok && XSqlDatabase_open(database);
    printf("SQLite 打开数据库：%s\n", ok ? "通过" : "失败");
    if (ok) {
        openedDatabase = XSqlDatabase_database("xsql-sqlite", true);
        ok = openedDatabase && XSqlDatabase_isOpen(openedDatabase);
        if (openedDatabase) {
            XSqlDatabase_delete_base(openedDatabase);
            openedDatabase = NULL;
        }
    }
    printf("SQLite 已打开连接获取：%s\n", ok ? "通过" : "失败");
    if (ok) {
        XAtomic_store_bool(&g_sqlThreadAffinityOk, false, XAtomic_MemoryOrder_Relaxed);
        XThread* affinityThread = XThread_create_func(
            XSqlTest_thread_affinity_probe,
            XVarList_Create(XVar(XSqlDatabase*, database)));
        if (affinityThread && XThread_start(affinityThread)) {
            XThread_wait(affinityThread, 5000);
            XClass_delete_base((XClass*)affinityThread);
        } else if (affinityThread) {
            XClass_delete_base((XClass*)affinityThread);
        }
        ok = XAtomic_load_bool(&g_sqlThreadAffinityOk, XAtomic_MemoryOrder_Acquire);
    }
    printf("SQLite 跨线程连接访问拒绝：%s\n", ok ? "通过" : "失败");
    if (ok) {
        sqliteDriver = XSqlDatabase_driver(database);
        notificationName = XString_create_utf8("people");
        notificationSubscribed = sqliteDriver && notificationName
            && XSqlDriver_hasFeature_base(sqliteDriver, XSqlDriverFeature_EventNotifications)
            && !XSqlDriver_hasFeature_base(sqliteDriver, XSqlDriverFeature_CancelQuery)
            && !XSqlDriver_cancelQuery_base(sqliteDriver)
            && XSqlDriver_subscribeToNotification_base(sqliteDriver, notificationName);
        notificationNames = sqliteDriver
            ? XSqlDriver_subscribedToNotifications_base(sqliteDriver) : NULL;
        ok = notificationSubscribed && notificationNames
            && XStringList_contains_utf8(notificationNames, "people", XChar_CaseSensitive)
            && !XSqlDriver_subscribeToNotification_base(sqliteDriver, notificationName);
    }
    printf("SQLite 事件通知订阅：%s\n", ok ? "通过" : "失败");
    databaseThread = ok ? XSqlDatabase_thread(database) : NULL;
    ok = ok && databaseThread && XSqlDatabase_moveToThread(database, databaseThread);
    printf("SQLite 连接线程亲和性：%s\n", ok ? "通过" : "失败");
    query = ok ? XSqlDatabase_exec_utf8(database,
        "CREATE TABLE people (id INTEGER PRIMARY KEY, name TEXT NOT NULL, payload BLOB, score REAL)") : NULL;
    ok = ok && query && XSqlQuery_isActive(query);
    printf("SQLite 创建表：%s\n", ok ? "通过" : "失败");
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    if (ok) {
        XSqlQuery* repeatQuery = XSqlQuery_create_database(database);
        XVariant* firstValue = XVariant_create_int32(11);
        XVariant* secondValue = XVariant_create_int32(22);
        XVariant* readValue = NULL;
        XString* boundName = NULL;
        bool repeatOk = repeatQuery && firstValue && secondValue
            && XSqlQuery_prepare_utf8(repeatQuery, "SELECT ?") ;
        if (repeatOk) {
            boundName = XSqlResult_boundValueName(XSqlQuery_result(repeatQuery), 0);
            repeatOk = XSqlResult_boundValueCount(XSqlQuery_result(repeatQuery)) == 1
                && boundName && XString_equals_utf8(boundName, ":0", XChar_CaseSensitive);
        }
        printf("SQLite 位置占位符元数据：%s\n", repeatOk ? "通过" : "失败");
        ok = ok && repeatOk;
        if (boundName) { XString_delete_base(boundName); boundName = NULL; }
        if (repeatOk) {
            XSqlQuery_addBindValue(repeatQuery, firstValue, XSqlParamType_In);
            repeatOk = XSqlQuery_exec(repeatQuery) && XSqlQuery_first(repeatQuery);
            readValue = repeatOk ? XSqlQuery_value(repeatQuery, 0) : NULL;
            repeatOk = repeatOk && readValue && XVariant_toInt32(readValue) == 11;
            if (readValue) { XVariant_delete_base(readValue); readValue = NULL; }
        }
        if (repeatOk) {
            XSqlQuery_addBindValue(repeatQuery, secondValue, XSqlParamType_In);
            repeatOk = XSqlQuery_exec(repeatQuery) && XSqlQuery_first(repeatQuery);
            readValue = repeatOk ? XSqlQuery_value(repeatQuery, 0) : NULL;
            repeatOk = repeatOk && readValue && XVariant_toInt32(readValue) == 22;
        }
        printf("SQLite addBindValue 重复执行：%s\n", repeatOk ? "通过" : "失败");
        ok = ok && repeatOk;
        if (readValue) XVariant_delete_base(readValue);
        if (firstValue) XVariant_delete_base(firstValue);
        if (secondValue) XVariant_delete_base(secondValue);
        if (repeatQuery) XSqlQuery_delete_base(repeatQuery);
    }

    query = ok ? XSqlQuery_create_database(database) : NULL;
    ok = ok && query && XSqlQuery_prepare_utf8(query,
        "INSERT INTO people (name, payload, score) VALUES (?, ?, ?)");
    value = XVariant_create_utf8_str("Alice");
    if (ok) XSqlQuery_bindValue(query, 0, value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    value = XVariant_create_byteArray("xy", 2);
    if (ok) XSqlQuery_bindValue(query, 1, value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    value = XVariant_create_double(1.5);
    if (ok) XSqlQuery_bindValue(query, 2, value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    ok = ok && XSqlQuery_exec(query) && XSqlQuery_numRowsAffected(query) == 1;
    printf("SQLite 位置参数绑定：%s\n", ok ? "通过" : "失败");
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    query = ok ? XSqlQuery_create_database(database) : NULL;
    ok = ok && query && XSqlQuery_prepare_utf8(query,
        "INSERT INTO people (name, payload, score) VALUES (:name, :payload, :score)");
    {
        XStringList* boundNames = query ? XSqlResult_boundValueNames(XSqlQuery_result(query)) : NULL;
        bool metadataOk = query
            && XSqlResult_boundValueCount(XSqlQuery_result(query)) == 3
            && boundNames && XStringList_size_base(boundNames) == 3;
        printf("SQLite 命名占位符元数据：%s\n", metadataOk ? "通过" : "失败");
        ok = ok && metadataOk;
        if (boundNames) XStringList_delete_base(boundNames);
    }
    value = XVariant_create_utf8_str("Bob");
    if (ok) XSqlQuery_bindValue_utf8(query, ":name", value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    value = XVariant_create_null();
    if (ok) XSqlQuery_bindValue_utf8(query, ":payload", value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    value = XVariant_create_double(2.5);
    if (ok) XSqlQuery_bindValue_utf8(query, ":score", value, XSqlParamType_In);
    if (value) { XVariant_delete_base(value); value = NULL; }
    ok = ok && XSqlQuery_exec(query) && XSqlQuery_numRowsAffected(query) == 1;
    printf("SQLite 命名参数绑定：%s\n", ok ? "通过" : "失败");
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    if (ok) {
        XSqlQuery* duplicateQuery = XSqlQuery_create_database(database);
        XVariant* duplicateInput = XVariant_create_int32(5);
        XVariant* duplicateOutput = NULL;
        bool duplicateOk = duplicateQuery && duplicateInput
            && XSqlQuery_prepare_utf8(duplicateQuery, "SELECT :value + :value")
            && XSqlResult_boundValueCount(XSqlQuery_result(duplicateQuery)) == 2;
        if (duplicateOk)
            XSqlQuery_bindValue_utf8(duplicateQuery, ":value", duplicateInput, XSqlParamType_In);
        duplicateOk = duplicateOk && XSqlQuery_exec(duplicateQuery)
            && XSqlQuery_first(duplicateQuery);
        duplicateOutput = duplicateOk ? XSqlQuery_value(duplicateQuery, 0) : NULL;
        duplicateOk = duplicateOk && duplicateOutput && XVariant_toInt32(duplicateOutput) == 10;
        printf("SQLite 重复命名占位符绑定：%s\n", duplicateOk ? "通过" : "失败");
        ok = ok && duplicateOk;
        if (duplicateOutput) XVariant_delete_base(duplicateOutput);
        if (duplicateInput) XVariant_delete_base(duplicateInput);
        if (duplicateQuery) XSqlQuery_delete_base(duplicateQuery);
    }

    if (ok) {
        XDate date = XDate_create_date(2024, 2, 29);
        XTime time = XTime_create_time(23, 45, 6, 123);
        XDateTime datetime = XDateTime_create_datetime(date, time);
        XVariant* dateValue = XVariant_create_Date(&date);
        XVariant* datetimeValue = XVariant_create_DateTime(&datetime);
        XVariant* timeValue = XVariant_create_Time(&time);
        XVariant* nullDateValue = XVariant_create_null();
        XVariant* nullTimeValue = XVariant_create_null();
        XVariant* nullDateTimeValue = XVariant_create_null();
        XVariant* dateRead = NULL;
        XVariant* datetimeRead = NULL;
        XVariant* timeRead = NULL;
        XString* dateText = NULL;
        XString* datetimeText = NULL;
        XString* timeText = NULL;
        bool temporalOk;
        bool nullTemporalOk = nullDateValue && nullTimeValue && nullDateTimeValue;
        if (nullTemporalOk) {
            XVariant_setValue_Date(nullDateValue, NULL);
            XVariant_setValue_Time(nullTimeValue, NULL);
            XVariant_setValue_DateTime(nullDateTimeValue, NULL);
            nullTemporalOk = XDate_isNull(XVariant_toDate_ref(nullDateValue))
                && XTime_isNull(XVariant_toTime_ref(nullTimeValue))
                && XDateTime_isNull(XVariant_toDateTime_ref(nullDateTimeValue));
        }
        printf("XVariant NULL 日期时间值：%s\n", nullTemporalOk ? "通过" : "失败");
        ok = ok && nullTemporalOk;
        query = XSqlDatabase_exec_utf8(database,
            "CREATE TABLE temporal (date_value DATE, datetime_value DATETIME, time_value TIME)");
        temporalOk = query && XSqlQuery_isActive(query);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        query = temporalOk ? XSqlQuery_create_database(database) : NULL;
        temporalOk = temporalOk && query && XSqlQuery_prepare_utf8(query,
            "INSERT INTO temporal VALUES (?, ?, ?)");
        if (temporalOk) {
            XSqlQuery_bindValue(query, 0, dateValue, XSqlParamType_In);
            XSqlQuery_bindValue(query, 1, datetimeValue, XSqlParamType_In);
            XSqlQuery_bindValue(query, 2, timeValue, XSqlParamType_In);
            temporalOk = XSqlQuery_exec(query);
        }
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        query = temporalOk ? XSqlDatabase_exec_utf8(database,
            "SELECT date_value, datetime_value, time_value FROM temporal") : NULL;
        temporalOk = temporalOk && query && XSqlQuery_first(query);
        if (temporalOk) {
            dateRead = XSqlQuery_value(query, 0);
            datetimeRead = XSqlQuery_value(query, 1);
            timeRead = XSqlQuery_value(query, 2);
            dateText = dateRead ? XVariant_toString(dateRead) : NULL;
            datetimeText = datetimeRead ? XVariant_toString(datetimeRead) : NULL;
            timeText = timeRead ? XVariant_toString(timeRead) : NULL;
            temporalOk = dateText && datetimeText && timeText
                && XString_equals_utf8(dateText, "2024-02-29", XChar_CaseSensitive)
                && XString_equals_utf8(datetimeText, "2024-02-29T23:45:06.123", XChar_CaseSensitive)
                && XString_equals_utf8(timeText, "23:45:06.123", XChar_CaseSensitive);
        }
        printf("SQLite 日期时间对象绑定：%s\n", temporalOk ? "通过" : "失败");
        ok = ok && temporalOk;
        if (dateText) XString_delete_base(dateText);
        if (datetimeText) XString_delete_base(datetimeText);
        if (timeText) XString_delete_base(timeText);
        if (dateRead) XVariant_delete_base(dateRead);
        if (datetimeRead) XVariant_delete_base(datetimeRead);
        if (timeRead) XVariant_delete_base(timeRead);
        if (query) XSqlQuery_delete_base(query);
        if (dateValue) XVariant_delete_base(dateValue);
        if (datetimeValue) XVariant_delete_base(datetimeValue);
        if (timeValue) XVariant_delete_base(timeValue);
        if (nullDateValue) XVariant_delete_base(nullDateValue);
        if (nullTimeValue) XVariant_delete_base(nullTimeValue);
        if (nullDateTimeValue) XVariant_delete_base(nullDateTimeValue);
        query = XSqlDatabase_exec_utf8(database, "DROP TABLE IF EXISTS temporal");
        if (query) XSqlQuery_delete_base(query);
    }

    query = ok ? XSqlDatabase_exec_utf8(database,
        "SELECT id, name, payload, score FROM people ORDER BY id") : NULL;
    ok = ok && query && XSqlQuery_isActive(query) && XSqlQuery_isSelect(query)
        && XSqlQuery_size(query) == -1 && XSqlQuery_first(query);
    ok = ok && !XSqlQuery_previous(query)
        && XSqlQuery_at(query) == XSqlLocation_BeforeFirstRow
        && XSqlQuery_next(query);
    if (ok) {
        XSqlRecord* rowRecord = XSqlQuery_record(query);
        XVariant* rowValue = rowRecord ? XSqlRecord_value(rowRecord, 1) : NULL;
        XString* rowText = rowValue ? XVariant_toString(rowValue) : NULL;
        bool recordValueOk = rowRecord && rowValue && rowText
            && XString_equals_utf8(rowText, "Alice", XChar_CaseSensitive);
        printf("SQLite 查询记录携带当前行值：%s\n", recordValueOk ? "通过" : "失败");
        ok = ok && recordValueOk;
        if (rowText) XString_delete_base(rowText);
        if (rowValue) XVariant_delete_base(rowValue);
        if (rowRecord) XSqlRecord_delete_base(rowRecord);
    }
    nameField = ok ? XString_create_utf8("name") : NULL;
    value = ok ? XSqlQuery_value(query, 2) : NULL;
    nameValue = ok ? XSqlQuery_value_2(query, nameField) : NULL;
    nameText = nameValue ? XVariant_toString(nameValue) : NULL;
    blob = value ? XByteArray_fromVariant(value) : NULL;
    ok = ok && blob && XByteArray_size_base(blob) == 2
        && memcmp(XByteArray_data(blob), "xy", 2) == 0
        && nameText && XString_equals_utf8(nameText, "Alice", XChar_CaseSensitive);
    if (blob) XByteArray_delete_base(blob);
    if (value) XVariant_delete_base(value);
    if (nameText) XString_delete_base(nameText);
    if (nameValue) XVariant_delete_base(nameValue);
    if (nameField) { XString_delete_base(nameField); nameField = NULL; }
    ok = ok && XSqlQuery_next(query) && XSqlQuery_isNull(query, 2)
        && XSqlQuery_last(query) && XSqlQuery_at(query) == 1
        && !XSqlQuery_next(query) && XSqlQuery_at(query) == XSqlLocation_AfterLastRow;
    XSqlQuery_finish(query);
    ok = ok && !XSqlQuery_isActive(query)
        && XSqlQuery_at(query) == XSqlLocation_BeforeFirstRow
        && XSqlQuery_size(query) == -1 && XSqlQuery_numRowsAffected(query) == -1
        && XSqlQuery_exec(query) && XSqlQuery_first(query);
    printf("SQLite 查询和记录定位：%s\n", ok ? "通过" : "失败");
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    ok = ok && XSqlDatabase_transaction(database);
    query = ok ? XSqlDatabase_exec_utf8(database,
        "INSERT INTO people (name, score) VALUES ('rollback', 3.5)") : NULL;
    ok = ok && query && XSqlQuery_isActive(query);
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    ok = ok && XSqlDatabase_rollback(database);
    query = ok ? XSqlDatabase_exec_utf8(database, "SELECT count(*) FROM people") : NULL;
    ok = ok && query && XSqlQuery_first(query);
    value = ok ? XSqlQuery_value(query, 0) : NULL;
    ok = ok && value && XVariant_toInt64(value) == 2;
    printf("SQLite 事务回滚：%s\n", ok ? "通过" : "失败");
    if (value) XVariant_delete_base(value);
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    tables = ok ? XSqlDatabase_tables(database, XSqlTableType_Tables) : NULL;
    peopleName = ok ? XString_create_utf8("people") : NULL;
    nameField = ok ? XString_create_utf8("name") : NULL;
    connectionName = ok ? XString_create_utf8("xsql-sqlite") : NULL;
    record = ok ? XSqlDatabase_record(database, peopleName) : NULL;
    primaryIndex = ok ? XSqlDatabase_primaryIndex_utf8(database, "people") : NULL;
    metadataField = record ? XSqlRecord_field(record, 0) : NULL;
    ok = ok && tables && XStringList_size_base(tables) >= 1
        && record && XSqlRecord_count(record) == 4
        && metadataField && XSqlField_typeId(metadataField) < 0
        && primaryIndex && XSqlRecord_count(&primaryIndex->m_parent) == 1
        && peopleName && connectionName
        && XSqlDatabase_contains_2(connectionName);
    printf("SQLite 元数据：%s\n", ok ? "通过" : "失败");

    if (ok) {
        bool batchFetch;
        bool copyQuerySet;
        const char* batchSql =
            "WITH RECURSIVE number(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM number WHERE n < 300) SELECT n FROM number";
        modelQuery = XSqlDatabase_exec_utf8(database, batchSql);
        copyModel = modelQuery ? XSqlQueryModel_create() : NULL;
        copyQuerySet = copyModel && modelQuery
            && XSqlQueryModel_setQuery_copy(copyModel, modelQuery)
            && XSqlQueryModel_rowCount(copyModel) == 255
            && XSqlQueryModel_canFetchMore(copyModel);
        if (copyQuerySet) XSqlQueryModel_fetchMore(copyModel);
        copyQuerySet = copyQuerySet && XSqlQueryModel_rowCount(copyModel) == 300
            && !XSqlQueryModel_canFetchMore(copyModel);
        if (modelQuery) { XSqlQuery_delete_base(modelQuery); modelQuery = NULL; }
        batchModel = XSqlQueryModel_create();
        batchFetch = batchModel && XSqlQueryModel_setQuery_utf8(batchModel,
            batchSql,
            database)
            && XSqlQueryModel_rowCount(batchModel) == 255
            && XSqlQueryModel_canFetchMore(batchModel);
        if (batchFetch) XSqlQueryModel_fetchMore(batchModel);
        value = batchFetch ? XSqlQueryModel_data(batchModel, 299, 0,
                                                  XSqlItemDataRole_Display) : NULL;
        batchFetch = batchFetch && XSqlQueryModel_rowCount(batchModel) == 300
            && !XSqlQueryModel_canFetchMore(batchModel)
            && value && XVariant_toInt64(value) == 300;
        if (value) { XVariant_delete_base(value); value = NULL; }
        ok = ok && batchFetch && copyQuerySet;
        printf("SQLite 查询模型分批取数和复制查询重载：%s\n",
               batchFetch && copyQuerySet ? "通过" : "失败");
    }

    if (ok) {
        XVariant* name = NULL;
        XVariant* score = NULL;
        XString* text = NULL;
        bool tableSelect;
        bool tableUpdate = false;
        bool tableInsertDelete = false;
        bool tableRead = false;
        bool tableRevert = false;
        bool tableFieldChange = false;
        bool tableRefresh = false;
        bool tableManualSubmit = false;
        bool tableManualDelete = false;
        bool tableStrategyRevert = false;
        bool tableColumnRemoval = false;
        tableModel = XSqlTableModel_create(database);
        if (tableModel) XSqlTableModel_setTable(tableModel, peopleName);
        tableSelect = tableModel && XSqlTableModel_select(tableModel)
            && XSqlTableModel_rowCount(tableModel) == 2
            && nameField && XSqlTableModel_fieldIndex_2(tableModel, nameField) == 1;
        filterText = XString_create_utf8("name <> ''");
        XSqlTableModel_setFilter(tableModel, filterText);
        XSqlTableModel_setFilter(tableModel, NULL);
        XSqlTableModel_setEditStrategy(tableModel, XSqlTableEditStrategy_OnManualSubmit);
        name = XVariant_create_utf8_str("Alice-model");
        tableUpdate = tableSelect && name && XSqlTableModel_setData(tableModel, 0, 1, name,
                                                                     XSqlItemDataRole_Edit);
        if (name) { XVariant_delete_base(name); name = NULL; }
        modelRecord = tableModel ? XSqlTableModel_record_current(tableModel) : NULL;
        name = XVariant_create_utf8_str("Model-insert");
        score = XVariant_create_double(7.5);
        if (modelRecord && name && score) {
            XSqlRecord_setValue_utf8(modelRecord, "name", name);
            XSqlRecord_setValue_utf8(modelRecord, "score", score);
        }
        tableInsertDelete = tableUpdate && modelRecord
            && XSqlTableModel_insertRecord(tableModel, -1, modelRecord)
            && XSqlTableModel_removeRows(tableModel, 1, 1)
            && XSqlTableModel_submitAll(tableModel);
        if (name) { XVariant_delete_base(name); name = NULL; }
        if (score) { XVariant_delete_base(score); score = NULL; }
        if (modelRecord) { XSqlRecord_delete_base(modelRecord); modelRecord = NULL; }
        query = tableInsertDelete ? XSqlDatabase_exec_utf8(database,
            "SELECT name FROM people ORDER BY id") : NULL;
        tableRead = query && XSqlQuery_first(query);
        value = tableRead ? XSqlQuery_value(query, 0) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableRead = tableRead && text && XString_equals_utf8(text, "Alice-model", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        tableRead = tableRead && XSqlQuery_next(query);
        value = tableRead ? XSqlQuery_value(query, 0) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableRead = tableRead && text && XString_equals_utf8(text, "Model-insert", XChar_CaseSensitive)
            && !XSqlQuery_next(query);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        if (query) { XSqlQuery_delete_base(query); query = NULL; }

        query = tableRead ? XSqlDatabase_exec_utf8(database,
            "UPDATE people SET name = 'external-refresh' WHERE id = 1") : NULL;
        tableRefresh = query && XSqlQuery_isActive(query) && XSqlTableModel_selectRow(tableModel, 0);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        value = tableRefresh ? XSqlTableModel_data(tableModel, 0, 1,
                                                    XSqlItemDataRole_Display) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableRefresh = tableRefresh && text
            && XString_equals_utf8(text, "external-refresh", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }

        name = XVariant_create_utf8_str("discarded");
        tableRevert = tableRefresh && name && XSqlTableModel_setData(tableModel, 0, 1, name,
                                                                   XSqlItemDataRole_Edit);
        if (name) { XVariant_delete_base(name); name = NULL; }
        if (tableModel) XSqlTableModel_revertRow(tableModel, 0);

        XSqlTableModel_setEditStrategy(tableModel, XSqlTableEditStrategy_OnManualSubmit);
        name = XVariant_create_utf8_str("manual-pending");
        tableManualSubmit = tableRevert && name
            && XSqlTableModel_setData(tableModel, 0, 1, name, XSqlItemDataRole_Edit)
            && XSqlTableModel_submit(tableModel) && XSqlTableModel_isDirty(tableModel);
        if (name) { XVariant_delete_base(name); name = NULL; }
        query = tableManualSubmit ? XSqlDatabase_exec_utf8(database,
            "SELECT name FROM people WHERE id = 1") : NULL;
        tableManualSubmit = tableManualSubmit && query && XSqlQuery_first(query);
        value = tableManualSubmit ? XSqlQuery_value(query, 0) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableManualSubmit = tableManualSubmit && text
            && XString_equals_utf8(text, "external-refresh", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        if (query) { XSqlQuery_delete_base(query); query = NULL; }

        tableManualDelete = tableManualSubmit && XSqlTableModel_removeRows(tableModel, 1, 1)
            && XSqlTableModel_rowCount(tableModel) == 2
            && XSqlTableModel_isDirty_row(tableModel, 1);
        value = tableManualDelete ? XSqlTableModel_headerData(tableModel, 1,
                                                               XSqlOrientation_Vertical,
                                                               XSqlItemDataRole_Display) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableManualDelete = tableManualDelete && text
            && XString_equals_utf8(text, "!", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        if (tableModel) XSqlTableModel_revertRow(tableModel, 1);
        value = tableManualDelete ? XSqlTableModel_headerData(tableModel, 1,
                                                               XSqlOrientation_Vertical,
                                                               XSqlItemDataRole_Display) : NULL;
        tableManualDelete = tableManualDelete && value && XVariant_toInt32(value) == 2
            && !XSqlTableModel_isDirty_row(tableModel, 1);
        if (value) XVariant_delete_base(value);

        XSqlTableModel_setEditStrategy(tableModel, XSqlTableEditStrategy_OnFieldChange);
        value = tableManualSubmit ? XSqlTableModel_data(tableModel, 0, 1,
                                                         XSqlItemDataRole_Display) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableStrategyRevert = tableManualSubmit && !XSqlTableModel_isDirty(tableModel)
            && text && XString_equals_utf8(text, "external-refresh", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        name = XVariant_create_utf8_str("Model-field-change");
        tableFieldChange = tableStrategyRevert && name && XSqlTableModel_setData(tableModel, 1, 1, name,
                                                                          XSqlItemDataRole_Edit);
        if (name) { XVariant_delete_base(name); name = NULL; }
        query = tableFieldChange ? XSqlDatabase_exec_utf8(database,
            "SELECT name FROM people ORDER BY id") : NULL;
        tableFieldChange = tableFieldChange && query && XSqlQuery_first(query);
        value = tableFieldChange ? XSqlQuery_value(query, 0) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableFieldChange = tableFieldChange && text
            && XString_equals_utf8(text, "external-refresh", XChar_CaseSensitive);
        if (value) { XVariant_delete_base(value); value = NULL; }
        if (text) { XString_delete_base(text); text = NULL; }
        tableFieldChange = tableFieldChange && XSqlQuery_next(query);
        value = tableFieldChange ? XSqlQuery_value(query, 0) : NULL;
        text = value ? XVariant_toString(value) : NULL;
        tableFieldChange = tableFieldChange && text
            && XString_equals_utf8(text, "Model-field-change", XChar_CaseSensitive);
        if (value) XVariant_delete_base(value);
        if (text) XString_delete_base(text);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        tableFieldChange = tableFieldChange && XSqlTableModel_removeRows(tableModel, 1, 1);
        query = tableFieldChange ? XSqlDatabase_exec_utf8(database,
            "SELECT count(*) FROM people") : NULL;
        tableFieldChange = tableFieldChange && query && XSqlQuery_first(query);
        value = tableFieldChange ? XSqlQuery_value(query, 0) : NULL;
        tableFieldChange = tableFieldChange && value && XVariant_toInt64(value) == 1;
        if (value) XVariant_delete_base(value);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        tableColumnRemoval = tableFieldChange
            && XSqlTableModel_removeColumns(tableModel, 2, 1)
            && XSqlQueryModel_columnCount(&tableModel->m_parent) == 3;
        text = tableColumnRemoval ? XSqlTableModel_selectStatement(tableModel) : NULL;
        tableColumnRemoval = tableColumnRemoval && text
            && !XString_contains_utf8(text, "payload", XChar_CaseInsensitive)
            && XString_contains_utf8(text, "score", XChar_CaseInsensitive);
        if (text) { XString_delete_base(text); text = NULL; }
        ok = ok && tableSelect && tableUpdate && tableInsertDelete && tableRead
            && tableRefresh && tableRevert && tableManualSubmit && tableStrategyRevert
            && tableManualDelete && tableFieldChange && tableColumnRemoval;
        printf("SQLite 表模型插入、更新、删除和编辑策略：%s\n", ok ? "通过" : "失败");
        if (!ok) {
            XSqlError* error = tableModel ? XSqlQueryModel_lastError(&tableModel->m_parent) : NULL;
            XString* errorText = error ? XSqlError_text(error) : NULL;
            printf("SQLite 表模型诊断：加载=%s，更新=%s，插入删除提交=%s，写回读取=%s，单行刷新=%s，撤销=%s，手动提交=%s，手动删除=%s，策略切换=%s，即时提交=%s，移除列重查=%s\n",
                   tableSelect ? "通过" : "失败", tableUpdate ? "通过" : "失败",
                   tableInsertDelete ? "通过" : "失败", tableRead ? "通过" : "失败",
                   tableRefresh ? "通过" : "失败", tableRevert ? "通过" : "失败",
                   tableManualSubmit ? "通过" : "失败", tableManualDelete ? "通过" : "失败",
                   tableStrategyRevert ? "通过" : "失败",
                   tableFieldChange ? "通过" : "失败", tableColumnRemoval ? "通过" : "失败");
            if (errorText && XString_length_base(errorText) > 0)
                printf("SQLite 表模型错误：%s\n", XString_toUtf8(errorText));
            if (errorText) XString_delete_base(errorText);
            if (error) XSqlError_delete_base(error);
        }
    }

    if (ok) {
        query = XSqlDatabase_exec_utf8(database, "PRAGMA journal_mode=WAL");
        ok = query && XSqlQuery_isActive(query) && XSqlQuery_first(query);
        value = ok ? XSqlQuery_value(query, 0) : NULL;
        XString* journalMode = value ? XVariant_toString(value) : NULL;
        ok = ok && journalMode && XString_equals_utf8(journalMode, "wal", XChar_CaseInsensitive);
        printf("SQLite 共享内存映射：%s\n", ok ? "通过" : "失败");
        if (journalMode) XString_delete_base(journalMode);
        if (value) XVariant_delete_base(value);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
        query = ok ? XSqlDatabase_exec_utf8(database,
            "INSERT INTO people (name, score) VALUES ('wal', 4.5)") : NULL;
        ok = ok && query && XSqlQuery_isActive(query);
        if (query) { XSqlQuery_delete_base(query); query = NULL; }
    }

    if (tables) XStringList_delete_base(tables);
    if (record) XSqlRecord_delete_base(record);
    if (metadataField) XSqlField_delete_base(metadataField);
    if (primaryIndex) XSqlIndex_delete_base(primaryIndex);
    if (batchModel) XSqlQueryModel_delete_base(batchModel);
    if (copyModel) XSqlQueryModel_delete_base(copyModel);
    if (modelQuery) XSqlQuery_delete_base(modelQuery);
    if (modelRecord) XSqlRecord_delete_base(modelRecord);
    if (tableModel) XSqlTableModel_delete_base(tableModel);
    XSqlDatabase_close(database);
    notificationNamesAfterClose = sqliteDriver
        ? XSqlDriver_subscribedToNotifications_base(sqliteDriver) : NULL;
    ok = ok && notificationNamesAfterClose
        && XStringList_size_base(notificationNamesAfterClose) == 0;
    notificationSubscribed = false;
    ok = ok && XFile_exists_static(databaseName);
    if (ok) {
        ok = XSqlDatabase_open(database);
        query = ok ? XSqlDatabase_exec_utf8(database, "SELECT count(*) FROM people") : NULL;
        ok = ok && query && XSqlQuery_first(query);
        value = ok ? XSqlQuery_value(query, 0) : NULL;
        ok = ok && value && XVariant_toInt64(value) == 2;
        if (value) XVariant_delete_base(value);
        if (query) XSqlQuery_delete_base(query);
        XSqlDatabase_close(database);
    }
    XFile_remove_static(databaseName);
    XFile_remove_static(walName);
    XFile_remove_static(shmName);
    if (notificationSubscribed && sqliteDriver && notificationName)
        XSqlDriver_unsubscribeFromNotification_base(sqliteDriver, notificationName);
    if (notificationNames) XStringList_delete_base(notificationNames);
    if (notificationNamesAfterClose) XStringList_delete_base(notificationNamesAfterClose);
    if (notificationName) XString_delete_base(notificationName);
    if (peopleName) XString_delete_base(peopleName);
    if (nameField) XString_delete_base(nameField);
    if (filterText) XString_delete_base(filterText);
    if (connectionName) XString_delete_base(connectionName);
    if (openedDatabase) XSqlDatabase_delete_base(openedDatabase);
    XString_delete_base(databaseName);
    XString_delete_base(walName);
    XString_delete_base(shmName);
    XSqlDatabase_removeDatabase("xsql-sqlite");
    if (database) XSqlDatabase_delete_base(database);
    printf("SQLite XFile 文件抽象：%s\n", ok ? "通过" : "失败");
    return ok;
}

static void XSqlTest_thread_affinity_probe(XThread* thread, XVarList* list)
{
    bool result;
    (void)thread;
    XVarList_args_1(list, XSqlDatabase*, database);
    result = XSqlDatabase_database("xsql-sqlite", false) == NULL
        && XSqlDatabase_exec_utf8(database, "SELECT 1") == NULL
        && XSqlDatabase_driver(database) == NULL;
    XAtomic_store_bool(&g_sqlThreadAffinityOk, result, XAtomic_MemoryOrder_Release);
}
