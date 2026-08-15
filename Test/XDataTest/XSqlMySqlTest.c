/**
 * @file       XSqlMySqlTest.c
 * @brief      MySQL/MariaDB 真实服务器全流程联调测试。
 * @details    测试只通过 XSqlDatabase、XSqlQuery、模型和公共 SQL API 操作，
 *             不直接包含 MySQL 客户端头文件。账号密码通过环境变量传入，
 *             避免写入源码和测试日志。
 */
#include "XPrintf.h"
#include "XSqlMySqlTest.h"
#include "XSql.h"
#include "XByteArray.h"
#include "XDateTime.h"
#include "XFile.h"
#include "XVariantList.h"

#include <stdlib.h>
#include <string.h>

#define XMYSQL_TEST_TEMP_DATABASE "xin_sql_mysql_test_database"

static const char* xmysql_test_env(const char* name, const char* defaultValue)
{
    const char* value = getenv(name);
    return value && value[0] ? value : defaultValue;
}

static bool xmysql_test_env_set(const char* name)
{
    const char* value = getenv(name);
    return value && value[0];
}

static int xmysql_test_port(void)
{
    const char* value = getenv("XMYSQL_TEST_PORT");
    char* end = NULL;
    long port;
    if (!value || !value[0]) return 3306;
    port = strtol(value, &end, 10);
    return end && *end == '\0' && port > 0 && port <= 65535 ? (int)port : 3306;
}

static XVariant* xmysql_test_string_value(const char* text)
{
    XString* string = XString_create_utf8(text ? text : "");
    XVariant* value = string ? XVariant_create_String_move(string) : NULL;
    if (string) XString_delete_base(string);
    return value;
}

static void xmysql_test_print_error(const char* prefix, const XSqlError* error)
{
    XString* text = error ? XSqlError_text(error) : NULL;
    XPrintf("MySQL：%s%s%s\n", prefix ? prefix : "错误",
           text && XString_length_base(text) > 0 ? "：" : "",
           text ? XString_toUtf8(text) : "未知错误");
    if (text) XString_delete_base(text);
}

static bool xmysql_test_query_ok(const char* label, XSqlQuery* query)
{
    bool ok = query && XSqlQuery_isActive(query);
    XPrintf("MySQL：%s：%s\n", label, ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("查询错误", error);
        if (error) XSqlError_delete_base(error);
    }
    return ok;
}

static bool xmysql_test_statement(const XSqlDatabase* database, const char* label,
                                  const char* sql)
{
    XSqlQuery* query = XSqlDatabase_exec_utf8(database, sql);
    bool ok = xmysql_test_query_ok(label, query);
    if (query) XSqlQuery_delete_base(query);
    return ok;
}

static bool xmysql_test_affected_statement(const XSqlDatabase* database,
                                           const char* label, const char* sql,
                                           int expected)
{
    XSqlQuery* query = XSqlDatabase_exec_utf8(database, sql);
    bool ok = xmysql_test_query_ok(label, query)
        && XSqlQuery_numRowsAffected(query) == expected;
    XPrintf("MySQL：%s影响行数：%s\n", label, ok ? "通过" : "失败");
    if (query) XSqlQuery_delete_base(query);
    return ok;
}

static XString* xmysql_test_quoted_name(const XSqlDatabase* database, const char* name)
{
    XSqlDriver* driver = XSqlDatabase_driver(database);
    XString* source = name ? XString_create_utf8(name) : NULL;
    XString* result = driver && source
        ? XSqlDriver_escapeIdentifier_base(driver, source, XSqlIdentifierType_TableName) : NULL;
    if (source) XString_delete_base(source);
    return result;
}

static XString* xmysql_test_sql(const char* format, const XString* first,
                                const XString* second)
{
    return XString_create_fmt_utf8(format,
                                   first ? XString_toUtf8(first) : "",
                                   second ? XString_toUtf8(second) : "");
}

static XString* xmysql_test_category_insert_sql(const XString* category)
{
    XString* sql = XString_create_utf8("INSERT INTO ");
    if (!sql || !category || !XString_append(sql, category)
        || !XString_append_utf8(sql, " (title) VALUES ('分类一'), ('分类二')")) {
        if (sql) XString_delete_base(sql);
        return NULL;
    }
    return sql;
}

static XSqlDatabase* xmysql_test_open_admin(const char* host, int port,
                                             const char* user, const char* password)
{
    XSqlDatabase* database = XSqlDatabase_addDatabase(XSqlDriverType_MySql,
                                                       "xsql-mysql-admin");
    XString* hostText = host ? XString_create_utf8(host) : NULL;
    bool ok = database && hostText;
    if (ok) {
        XSqlDatabase_setHostName(database, hostText);
        XSqlDatabase_setPort(database, port);
        ok = XSqlDatabase_open_utf8(database, user, password);
    }
    if (hostText) XString_delete_base(hostText);
    if (!ok) {
        if (database) {
            XSqlError* error = XSqlDatabase_lastError(database);
            xmysql_test_print_error("管理连接错误", error);
            if (error) XSqlError_delete_base(error);
            XSqlDatabase_close(database);
            XSqlDatabase_removeDatabase("xsql-mysql-admin");
            XSqlDatabase_delete_base(database);
        }
        return NULL;
    }
    return database;
}

static bool xmysql_test_prepare_temporary_database(const char* host, int port,
                                                   const char* user, const char* password,
                                                   bool* created)
{
    XSqlDatabase* admin = xmysql_test_open_admin(host, port, user, password);
    XSqlQuery* query = NULL;
    bool ok;
    bool exists;
    if (created) *created = false;
    if (!admin) return false;
    query = XSqlDatabase_exec_utf8(admin,
        "SHOW DATABASES LIKE 'xin_sql_mysql_test_database'");
    ok = query && XSqlQuery_isActive(query);
    exists = ok && XSqlQuery_size(query) > 0;
    XPrintf("MySQL：检查临时测试库：%s\n", ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("检查临时测试库错误", error);
        if (error) XSqlError_delete_base(error);
    }
    if (query) XSqlQuery_delete_base(query);
    if (ok && !exists) {
        query = XSqlDatabase_exec_utf8(admin,
            "CREATE DATABASE `xin_sql_mysql_test_database` CHARACTER SET utf8mb4");
        ok = query && XSqlQuery_isActive(query);
        XPrintf("MySQL：创建临时测试库：%s\n", ok ? "通过" : "失败");
        if (!ok && query) {
            XSqlError* error = XSqlQuery_lastError(query);
            xmysql_test_print_error("创建临时测试库错误", error);
            if (error) XSqlError_delete_base(error);
        }
        if (ok && created) *created = true;
        if (query) XSqlQuery_delete_base(query);
    }
    XSqlDatabase_close(admin);
    XSqlDatabase_removeDatabase("xsql-mysql-admin");
    XSqlDatabase_delete_base(admin);
    return ok;
}

static void xmysql_test_drop_temporary_database(const char* host, int port,
                                                const char* user, const char* password)
{
    XSqlDatabase* admin = xmysql_test_open_admin(host, port, user, password);
    XSqlQuery* query;
    if (!admin) return;
    query = XSqlDatabase_exec_utf8(admin,
        "DROP DATABASE IF EXISTS `xin_sql_mysql_test_database`");
    if (query) {
        XPrintf("MySQL：删除临时测试库：%s\n",
               XSqlQuery_isActive(query) ? "通过" : "失败");
        if (!XSqlQuery_isActive(query)) {
            XSqlError* error = XSqlQuery_lastError(query);
            xmysql_test_print_error("删除临时测试库错误", error);
            if (error) XSqlError_delete_base(error);
        }
        XSqlQuery_delete_base(query);
    }
    XSqlDatabase_close(admin);
    XSqlDatabase_removeDatabase("xsql-mysql-admin");
    XSqlDatabase_delete_base(admin);
}

static bool xmysql_test_caching_sha2_rsa(const char* host, int port,
                                         const char* adminUser,
                                         const char* adminPassword)
{
    static const char testUser[] = "xin_sql_caching_sha2_test";
    static const char testPassword[] = "XinSqlRsa_Test_981103";
    XSqlDatabase* admin = NULL;
    XSqlDatabase* rsaDatabase = NULL;
    XSqlQuery* query = NULL;
    XString* hostText = NULL;
    XString* options = NULL;
    XString* userText = NULL;
    XString* passwordText = NULL;
    bool pluginAvailable = false;
    bool created = false;
    bool ok = true;

    admin = xmysql_test_open_admin(host, port, adminUser, adminPassword);
    if (!admin) return false;
    query = XSqlDatabase_exec_utf8(admin,
        "SELECT PLUGIN_NAME FROM INFORMATION_SCHEMA.PLUGINS "
        "WHERE PLUGIN_NAME = 'caching_sha2_password'");
    pluginAvailable = query && XSqlQuery_isActive(query) && XSqlQuery_size(query) > 0;
    if (query) XSqlQuery_delete_base(query);
    if (!pluginAvailable) {
        XPrintf("MySQL：caching_sha2_password RSA 测试：服务器未安装插件，跳过\n");
        XSqlDatabase_close(admin);
        XSqlDatabase_removeDatabase("xsql-mysql-admin");
        XSqlDatabase_delete_base(admin);
        return true;
    }
    query = XSqlDatabase_exec_utf8(admin,
        "DROP USER IF EXISTS 'xin_sql_caching_sha2_test'@'%'");
    ok = query && XSqlQuery_isActive(query);
    if (query) XSqlQuery_delete_base(query);
    query = ok ? XSqlDatabase_exec_utf8(admin,
        "CREATE USER 'xin_sql_caching_sha2_test'@'%' "
        "IDENTIFIED WITH caching_sha2_password BY 'XinSqlRsa_Test_981103'") : NULL;
    ok = ok && query && XSqlQuery_isActive(query);
    if (query) XSqlQuery_delete_base(query);
    created = ok;
    XSqlDatabase_close(admin);
    XSqlDatabase_removeDatabase("xsql-mysql-admin");
    XSqlDatabase_delete_base(admin);
    if (!created) {
        XPrintf("MySQL：caching_sha2_password RSA 测试账号创建：失败\n");
        return false;
    }

    rsaDatabase = XSqlDatabase_addDatabase(XSqlDriverType_MySql, "xsql-mysql-rsa");
    if (rsaDatabase) {
        hostText = XString_create_utf8(host);
        options = XString_create_utf8("SSL_MODE=disabled");
        userText = XString_create_utf8(testUser);
        passwordText = XString_create_utf8(testPassword);
        XSqlDatabase_setHostName(rsaDatabase, hostText);
        XSqlDatabase_setPort(rsaDatabase, port);
        XSqlDatabase_setUserName(rsaDatabase, userText);
        XSqlDatabase_setPassword(rsaDatabase, passwordText);
        XSqlDatabase_setConnectOptions(rsaDatabase, options);
        ok = hostText && options && userText && passwordText && XSqlDatabase_open(rsaDatabase);
        if (hostText) XString_delete_base(hostText);
        if (options) XString_delete_base(options);
        if (userText) XString_delete_base(userText);
        if (passwordText) XString_delete_base(passwordText);
        hostText = NULL;
        options = NULL;
        userText = NULL;
        passwordText = NULL;
    } else {
        ok = false;
    }
    XPrintf("MySQL：caching_sha2_password 无 TLS RSA 认证：%s\n", ok ? "通过" : "失败");
    if (!ok && rsaDatabase) {
        XSqlError* error = XSqlDatabase_lastError(rsaDatabase);
        xmysql_test_print_error("RSA 认证错误", error);
        if (error) XSqlError_delete_base(error);
    }
    if (rsaDatabase) {
        XSqlDatabase_close(rsaDatabase);
        XSqlDatabase_removeDatabase("xsql-mysql-rsa");
        XSqlDatabase_delete_base(rsaDatabase);
    }

    admin = xmysql_test_open_admin(host, port, adminUser, adminPassword);
    if (admin) {
        query = XSqlDatabase_exec_utf8(admin,
            "DROP USER IF EXISTS 'xin_sql_caching_sha2_test'@'%'");
        ok = ok && query && XSqlQuery_isActive(query);
        if (query) XSqlQuery_delete_base(query);
        XSqlDatabase_close(admin);
        XSqlDatabase_removeDatabase("xsql-mysql-admin");
        XSqlDatabase_delete_base(admin);
    } else {
        ok = false;
    }
    return ok;
}

static bool xmysql_test_check_values(XSqlQuery* query)
{
    XVariant* id = NULL;
    XVariant* name = NULL;
    XVariant* score = NULL;
    XVariant* flag = NULL;
    XVariant* payload = NULL;
    XVariant* note = NULL;
    XString* nameText = NULL;
    bool idOk;
    bool nameOk;
    bool scoreOk;
    bool flagOk;
    bool payloadOk;
    bool noteOk;
    bool nullOk;
    bool ok;
    if (!query || !XSqlQuery_first(query)) return false;
    id = XSqlQuery_value_utf8(query, "id");
    name = XSqlQuery_value_utf8(query, "name");
    score = XSqlQuery_value_utf8(query, "score");
    flag = XSqlQuery_value_utf8(query, "flag");
    payload = XSqlQuery_value_utf8(query, "payload");
    note = XSqlQuery_value_utf8(query, "note");
    nameText = name ? XVariant_toString(name) : NULL;
    idOk = id && XVariant_type(id) == XVariantType_Int64;
    nameOk = nameText && XString_equals_utf8(nameText, "中文用户", XChar_CaseSensitive);
    scoreOk = score && XVariant_type(score) == XVariantType_Double
        && XVariant_toDouble(score) > 12.49 && XVariant_toDouble(score) < 12.51;
    flagOk = flag && XVariant_type(flag) == XVariantType_Int32 && XVariant_toBool(flag);
    payloadOk = payload && XVariant_type(payload) == XVariantType_ByteArray;
    if (payloadOk) payloadOk = XByteArray_size_base(XByteArray_fromVariant_ref(payload)) == 4;
    noteOk = note && !XVariant_isValid(note);
    nullOk = XSqlQuery_isNull_utf8(query, "note");
    ok = idOk && nameOk && scoreOk && flagOk && payloadOk && noteOk && nullOk;
    if (!ok) {
        XPrintf("MySQL：字段值诊断：编号=%s，中文=%s，数值=%s，布尔=%s，BLOB=%s，NULL值=%s，NULL判断=%s\n",
               idOk ? "通过" : "失败", nameOk ? "通过" : "失败",
               scoreOk ? "通过" : "失败", flagOk ? "通过" : "失败",
               payloadOk ? "通过" : "失败", noteOk ? "通过" : "失败",
               nullOk ? "通过" : "失败");
    }
    if (id) XVariant_delete_base(id);
    if (name) XVariant_delete_base(name);
    if (score) XVariant_delete_base(score);
    if (flag) XVariant_delete_base(flag);
    if (payload) XVariant_delete_base(payload);
    if (note) XVariant_delete_base(note);
    if (nameText) XString_delete_base(nameText);
    return ok;
}

static bool xmysql_test_basic_queries(const XSqlDatabase* database,
                                      const XString* table,
                                      const XString* category)
{
    XString* sql = NULL;
    XSqlQuery* query = NULL;
    XVariant* value = NULL;
    XVariant* id = NULL;
    XVariant* name = NULL;
    XVariant* score = NULL;
    XVariant* flag = NULL;
    XVariant* payload = NULL;
    XVariant* note = NULL;
    XVariant* categoryId = NULL;
    uint8_t blobData[] = { 0x00, 0x11, 0x7f, 0xff };
    bool ok = true;

    sql = xmysql_test_category_insert_sql(category);
    ok = ok && sql && xmysql_test_statement(database, "插入分类数据", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }

    query = XSqlQuery_create_database(database);
    id = XVariant_create_int64(0);
    name = xmysql_test_string_value("中文用户");
    score = XVariant_create_double(12.5);
    flag = XVariant_create_bool(true);
    payload = XVariant_create_byteArray(blobData, sizeof(blobData));
    note = XVariant_create_null();
    categoryId = XVariant_create_int64(1);
    sql = xmysql_test_sql("INSERT INTO %s (name, score, flag, payload, note, category_id) VALUES (?, ?, ?, ?, ?, ?)", table, NULL);
    ok = ok && query && sql && XSqlQuery_prepare(query, sql);
    if (ok) {
        XSqlQuery_bindValue(query, 0, name, XSqlParamType_In);
        XSqlQuery_bindValue(query, 1, score, XSqlParamType_In);
        XSqlQuery_bindValue(query, 2, flag, XSqlParamType_In);
        XSqlQuery_bindValue(query, 3, payload, XSqlParamType_In | XSqlParamType_Binary);
        XSqlQuery_bindValue(query, 4, note, XSqlParamType_In);
        XSqlQuery_bindValue(query, 5, categoryId, XSqlParamType_In);
        ok = XSqlQuery_exec(query);
    }
    XPrintf("MySQL：位置参数插入：%s\n", ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("位置绑定错误", error);
        if (error) XSqlError_delete_base(error);
    }
    value = query ? XSqlQuery_lastInsertId(query) : NULL;
    if (value && XVariant_isValid(value)) XVariant_setValue_int64(id, XVariant_toInt64(value));
    if (value) XVariant_delete_base(value);
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }
    if (id) { XVariant_delete_base(id); id = NULL; }

    query = XSqlQuery_create_database(database);
    sql = xmysql_test_sql("INSERT INTO %s (name, score, flag, payload, note, category_id) VALUES (:name, :score, :flag, :payload, :note, :category)", table, NULL);
    ok = ok && query && sql && XSqlQuery_prepare(query, sql);
    if (ok) {
        XSqlQuery_bindValue_utf8(query, ":name", name, XSqlParamType_In);
        XSqlQuery_bindValue_utf8(query, ":score", score, XSqlParamType_In);
        XSqlQuery_bindValue_utf8(query, ":flag", flag, XSqlParamType_In);
        XSqlQuery_bindValue_utf8(query, ":payload", payload, XSqlParamType_In | XSqlParamType_Binary);
        XSqlQuery_bindValue_utf8(query, ":note", note, XSqlParamType_In);
        XSqlQuery_bindValue_utf8(query, ":category", categoryId, XSqlParamType_In);
        ok = XSqlQuery_exec(query);
    }
    XPrintf("MySQL：命名参数插入：%s\n", ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("命名绑定错误", error);
        if (error) XSqlError_delete_base(error);
    }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }
    if (name) { XVariant_delete_base(name); name = NULL; }
    if (score) { XVariant_delete_base(score); score = NULL; }
    if (flag) { XVariant_delete_base(flag); flag = NULL; }
    if (payload) { XVariant_delete_base(payload); payload = NULL; }
    if (note) { XVariant_delete_base(note); note = NULL; }
    if (categoryId) { XVariant_delete_base(categoryId); categoryId = NULL; }

    sql = xmysql_test_sql("INSERT INTO %s (name, score, flag, category_id) VALUES ('临时数据', 1.0, 1, 1)", table, NULL);
    ok = ok && sql && xmysql_test_affected_statement(database, "更新删除测试插入", XString_toUtf8(sql), 1);
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = xmysql_test_sql("UPDATE %s SET score = 2.0 WHERE name = '临时数据'", table, NULL);
    ok = ok && sql && xmysql_test_affected_statement(database, "更新记录", XString_toUtf8(sql), 1);
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = xmysql_test_sql("DELETE FROM %s WHERE name = '临时数据'", table, NULL);
    ok = ok && sql && xmysql_test_affected_statement(database, "删除记录", XString_toUtf8(sql), 1);
    if (sql) { XString_delete_base(sql); sql = NULL; }

    sql = xmysql_test_sql("SELECT id, name, score, flag, payload, note, category_id FROM %s ORDER BY id", table, NULL);
    query = XSqlDatabase_exec(database, sql);
    ok = ok && xmysql_test_query_ok("查询结果集", query)
        && XSqlQuery_isSelect(query) && XSqlQuery_size(query) == 2
        && xmysql_test_check_values(query);
    XPrintf("MySQL：NULL、BLOB、Unicode 和字段读取：%s\n", ok ? "通过" : "失败");
    if (query) {
        ok = ok && XSqlQuery_last(query) && XSqlQuery_previous(query)
            && XSqlQuery_seek(query, 0, false) && XSqlQuery_isValid(query);
        XPrintf("MySQL：首行、末行、上一行和定位：%s\n", ok ? "通过" : "失败");
        XSqlQuery_finish(query);
        XPrintf("MySQL：结束结果集：%s\n", !XSqlQuery_isActive(query) ? "通过" : "失败");
        XSqlQuery_delete_base(query);
        query = NULL;
    }
    if (sql) { XString_delete_base(sql); sql = NULL; }
    return ok;
}

static bool xmysql_test_transactions(const XSqlDatabase* database, const XString* table)
{
    XString* sql = xmysql_test_sql("SELECT count(*) FROM %s", table, NULL);
    XString* insertSql = xmysql_test_sql("INSERT INTO %s (name, score, flag, category_id) VALUES ('事务回滚', 1.0, 1, 1)", table, NULL);
    XString* commitSql = xmysql_test_sql("INSERT INTO %s (name, score, flag, category_id) VALUES ('事务提交', 2.0, 1, 2)", table, NULL);
    XSqlQuery* query;
    bool ok = XSqlDatabase_transaction((XSqlDatabase*)database);
    ok = ok && insertSql && xmysql_test_statement(database, "事务中插入", XString_toUtf8(insertSql));
    ok = ok && XSqlDatabase_rollback((XSqlDatabase*)database);
    query = ok ? XSqlDatabase_exec(database, sql) : NULL;
    ok = ok && query && XSqlQuery_first(query);
    if (ok) {
        XVariant* value = XSqlQuery_value(query, 0);
        ok = value && XVariant_toInt64(value) == 2;
        if (value) XVariant_delete_base(value);
    }
    if (query) XSqlQuery_delete_base(query);
    XPrintf("MySQL：事务回滚：%s\n", ok ? "通过" : "失败");
    ok = ok && XSqlDatabase_transaction((XSqlDatabase*)database)
        && commitSql && xmysql_test_statement(database, "事务提交插入", XString_toUtf8(commitSql))
        && XSqlDatabase_commit((XSqlDatabase*)database);
    query = ok ? XSqlDatabase_exec(database, sql) : NULL;
    ok = ok && query && XSqlQuery_first(query);
    if (ok) {
        XVariant* value = XSqlQuery_value(query, 0);
        ok = value && XVariant_toInt64(value) == 3;
        if (value) XVariant_delete_base(value);
    }
    if (query) XSqlQuery_delete_base(query);
    XPrintf("MySQL：事务提交：%s\n", ok ? "通过" : "失败");
    if (sql) XString_delete_base(sql);
    if (insertSql) XString_delete_base(insertSql);
    if (commitSql) XString_delete_base(commitSql);
    return ok;
}

static bool xmysql_test_metadata_and_models(const XSqlDatabase* database,
                                            const XString* table,
                                            const XString* category)
{
    XSqlDriver* driver = XSqlDatabase_driver(database);
    XString* plainTable = driver
        ? XSqlDriver_stripDelimiters_base(driver, table, XSqlIdentifierType_TableName) : NULL;
    XStringList* tables = XSqlDatabase_tables(database, XSqlTableType_AllTables);
    XSqlRecord* record = XSqlDatabase_record(database, plainTable);
    XSqlIndex* index = XSqlDatabase_primaryIndex(database, plainTable);
    XSqlField* idField = record ? XSqlRecord_field_utf8(record, "id") : NULL;
    XSqlField* dateField = record ? XSqlRecord_field_utf8(record, "date_value") : NULL;
    XSqlField* datetimeField = record ? XSqlRecord_field_utf8(record, "datetime_value") : NULL;
    XSqlQueryModel* queryModel = XSqlQueryModel_create();
    XSqlTableModel* tableModel = XSqlTableModel_create(database);
    XSqlRelationalTableModel* relationModel = XSqlRelationalTableModel_create(database);
    XString* modelFilter = XString_create_utf8("category_id = 1");
    XString* relationTableName = XString_create_utf8("xin_sql_mysql_category");
    XString* relationIndexColumn = XString_create_utf8("id");
    XString* relationDisplayColumn = XString_create_utf8("title");
    XString* qualifiedIdentifier = XString_create_utf8("db.schema.table");
    XString* qualifiedEscaped = driver
        ? XSqlDriver_escapeIdentifier_base(driver, qualifiedIdentifier,
                                           XSqlIdentifierType_TableName) : NULL;
    XSqlRelation* relation = XSqlRelation_create_2(relationTableName,
                                                   relationIndexColumn,
                                                   relationDisplayColumn);
    XString* escaped = XSqlDriver_escapeIdentifier_base(driver, plainTable,
                                                         XSqlIdentifierType_TableName);
    XString* modelSql = xmysql_test_sql("SELECT id, name, category_id FROM %s ORDER BY id", table, NULL);
    XVariant* modelValue = NULL;
    XVariant* relationValue = NULL;
    bool tableOk = plainTable && tables;
    bool recordOk = record && XSqlRecord_count(record) >= 7;
    bool temporalMetaOk = dateField && datetimeField
        && XSqlField_metaType(dateField) == XVariantType_Date
        && XSqlField_metaType(datetimeField) == XVariantType_DateTime;
    bool sqlTypeIdOk = idField && XSqlField_typeId(idField) < 0
        && XSqlField_typeID(idField) < 0;
    bool indexOk = index && XSqlRecord_count(&index->m_parent) >= 1;
    bool escapedOk = escaped && XString_equals(escaped, table, XChar_CaseSensitive);
    bool qualifiedEscapedOk = qualifiedEscaped
        && XString_equals_utf8(qualifiedEscaped, "`db`.`schema`.`table`",
                                XChar_CaseSensitive);
    bool stringApiOk = modelFilter && relationTableName && relationIndexColumn
        && relationDisplayColumn && relation;
    bool ok = tableOk && recordOk && temporalMetaOk && sqlTypeIdOk
        && indexOk && escapedOk && qualifiedEscapedOk && stringApiOk;
    XPrintf("MySQL：表、字段、主键和标识符元数据：%s\n", ok ? "通过" : "失败");
    if (!ok) {
        XPrintf("MySQL：元数据诊断：表列表=%s，字段数=%d，日期类型=%s，主键字段数=%d，标识符转义=%s\n",
               tableOk ? "通过" : "失败", record ? XSqlRecord_count(record) : -1,
               temporalMetaOk ? "通过" : "失败",
               index ? XSqlRecord_count(&index->m_parent) : -1,
               escapedOk && qualifiedEscapedOk ? "通过" : "失败");
    }
    if (queryModel) {
        ok = ok && modelSql && XSqlQueryModel_setQuery(queryModel, modelSql, database)
            && XSqlQueryModel_rowCount(queryModel) == 3
            && XSqlQueryModel_columnCount(queryModel) == 3;
        modelValue = XSqlQueryModel_data(queryModel, 0, 1, XSqlItemDataRole_Display);
        ok = ok && modelValue && XVariant_type(modelValue) == XVariantType_String;
        XPrintf("MySQL：查询模型：%s\n", ok ? "通过" : "失败");
    }
    if (tableModel) {
        XSqlTableModel_setTable(tableModel, plainTable);
        XSqlTableModel_setFilter(tableModel, modelFilter);
        ok = ok && XSqlTableModel_select(tableModel) && XSqlTableModel_rowCount(tableModel) == 2;
        XPrintf("MySQL：表模型查询和过滤：%s\n", ok ? "通过" : "失败");
    }
    if (relationModel && relation) {
        XSqlRelationalTableModel_setTable(relationModel, plainTable);
        XSqlRelationalTableModel_setRelation(relationModel, 6, relation);
        ok = ok && XSqlRelationalTableModel_select(relationModel);
        relationValue = XSqlRelationalTableModel_data(relationModel, 0, 6, XSqlItemDataRole_Display);
        ok = ok && relationValue && XVariant_type(relationValue) == XVariantType_String;
        XPrintf("MySQL：关系表模型：%s\n", ok ? "通过" : "失败");
    }
    if (relationModel) {
        XSqlTableModel* editorModel = XSqlRelationalDelegate_createEditorModel(relationModel, 6);
        XPrintf("MySQL：关系委托编辑模型：%s\n", editorModel ? "通过" : "失败");
        ok = ok && editorModel != NULL;
    }
    if (modelValue) XVariant_delete_base(modelValue);
    if (relationValue) XVariant_delete_base(relationValue);
    if (relation) XSqlRelation_delete_base(relation);
    if (qualifiedEscaped) XString_delete_base(qualifiedEscaped);
    if (qualifiedIdentifier) XString_delete_base(qualifiedIdentifier);
    if (relationModel) XSqlRelationalTableModel_delete_base(relationModel);
    if (tableModel) XSqlTableModel_delete_base(tableModel);
    if (queryModel) XSqlQueryModel_delete_base(queryModel);
    if (index) XSqlIndex_delete_base(index);
    if (record) XSqlRecord_delete_base(record);
    if (idField) XSqlField_delete_base(idField);
    if (dateField) XSqlField_delete_base(dateField);
    if (datetimeField) XSqlField_delete_base(datetimeField);
    if (tables) XStringList_delete_base(tables);
    if (escaped) XString_delete_base(escaped);
    if (plainTable) XString_delete_base(plainTable);
    if (modelSql) XString_delete_base(modelSql);
    if (modelFilter) XString_delete_base(modelFilter);
    if (relationTableName) XString_delete_base(relationTableName);
    if (relationIndexColumn) XString_delete_base(relationIndexColumn);
    if (relationDisplayColumn) XString_delete_base(relationDisplayColumn);
    (void)category;
    return ok;
}

static bool xmysql_test_table_model_writes(const XSqlDatabase* database, const XString* table)
{
    XSqlDriver* driver = XSqlDatabase_driver(database);
    XString* plainTable = driver
        ? XSqlDriver_stripDelimiters_base(driver, table, XSqlIdentifierType_TableName) : NULL;
    XSqlTableModel* model = XSqlTableModel_create(database);
    XSqlRecord* record = NULL;
    XSqlQuery* query = NULL;
    XVariant* name = NULL;
    XVariant* score = NULL;
    XVariant* flag = NULL;
    XVariant* category = NULL;
    XVariant* value = NULL;
    XString* text = NULL;
    XString* sql = NULL;
    bool ok = plainTable && model;
    bool selectOk = false;
    bool updateOk = false;
    bool insertOk = false;
    bool removeOk = false;
    bool submitOk = false;

    if (model) {
        XSqlTableModel_setTable_utf8(model, XString_toUtf8(plainTable));
        XSqlTableModel_setFilter_utf8(model, "category_id = 1");
        XSqlTableModel_setSort(model, 0, XSqlSortOrder_Ascending);
    }
    selectOk = XSqlTableModel_select(model) && XSqlTableModel_rowCount(model) == 2;
    ok = ok && selectOk;
    XSqlTableModel_setEditStrategy(model, XSqlTableEditStrategy_OnManualSubmit);
    name = xmysql_test_string_value("模型更新");
    updateOk = selectOk && name
        && XSqlTableModel_setData(model, 0, 1, name, XSqlItemDataRole_Edit);
    ok = ok && updateOk;
    if (name) { XVariant_delete_base(name); name = NULL; }

    record = model ? XSqlTableModel_record_current(model) : NULL;
    name = xmysql_test_string_value("模型新增");
    score = XVariant_create_double(6.5);
    flag = XVariant_create_bool(true);
    category = XVariant_create_int64(1);
    if (record && name && score && flag && category) {
        XSqlRecord_setValue_utf8(record, "name", name);
        XSqlRecord_setValue_utf8(record, "score", score);
        XSqlRecord_setValue_utf8(record, "flag", flag);
        XSqlRecord_setValue_utf8(record, "category_id", category);
    }
    insertOk = updateOk && record && XSqlTableModel_insertRecord(model, -1, record);
    removeOk = insertOk && XSqlTableModel_removeRows(model, 1, 1);
    submitOk = removeOk && XSqlTableModel_submitAll(model);
    ok = ok && insertOk && removeOk && submitOk;
    if (name) XVariant_delete_base(name);
    if (score) XVariant_delete_base(score);
    if (flag) XVariant_delete_base(flag);
    if (category) XVariant_delete_base(category);
    if (record) XSqlRecord_delete_base(record);

    sql = xmysql_test_sql("SELECT name FROM %s WHERE category_id = 1 ORDER BY id", table, NULL);
    query = submitOk && sql ? XSqlDatabase_exec(database, sql) : NULL;
    ok = ok && query && XSqlQuery_first(query);
    value = ok ? XSqlQuery_value(query, 0) : NULL;
    text = value ? XVariant_toString(value) : NULL;
    ok = ok && text && XString_equals_utf8(text, "模型更新", XChar_CaseSensitive);
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (text) { XString_delete_base(text); text = NULL; }
    ok = ok && XSqlQuery_next(query);
    value = ok ? XSqlQuery_value(query, 0) : NULL;
    text = value ? XVariant_toString(value) : NULL;
    ok = ok && text && XString_equals_utf8(text, "模型新增", XChar_CaseSensitive)
        && !XSqlQuery_next(query);
    if (value) XVariant_delete_base(value);
    if (text) XString_delete_base(text);
    if (query) XSqlQuery_delete_base(query);
    if (sql) XString_delete_base(sql);
    if (!ok) {
        XSqlError* error = model ? XSqlQueryModel_lastError(&model->m_parent) : NULL;
        XPrintf("MySQL：表模型写回诊断：加载=%s（行数=%d），更新=%s，插入=%s，删除=%s，提交=%s\n",
               selectOk ? "通过" : "失败", model ? XSqlTableModel_rowCount(model) : -1,
               updateOk ? "通过" : "失败",
               insertOk ? "通过" : "失败", removeOk ? "通过" : "失败",
               submitOk ? "通过" : "失败");
        if (error) {
            xmysql_test_print_error("表模型错误", error);
            XSqlError_delete_base(error);
        }
    }
    if (model) XSqlTableModel_delete_base(model);
    if (plainTable) XString_delete_base(plainTable);
    XPrintf("MySQL：表模型插入、更新和删除写回：%s\n", ok ? "通过" : "失败");
    return ok;
}

static bool xmysql_test_temporal_values(const XSqlDatabase* database, const XString* table)
{
    XSqlQuery* query = NULL;
    XString* sql = NULL;
    XVariant* name = xmysql_test_string_value("日期绑定");
    XVariant* score = XVariant_create_double(3.5);
    XVariant* flag = XVariant_create_bool(true);
    XVariant* category = XVariant_create_int64(1);
    XDate date = XDate_create_date(2024, 2, 29);
    XTime time = XTime_create_time(23, 45, 6, 123);
    XDateTime datetime = XDateTime_create_datetime(date, time);
    XVariant* dateValue = XVariant_create_Date(&date);
    XVariant* datetimeValue = XVariant_create_DateTime(&datetime);
    XVariant* timeValue = XVariant_create_Time(&time);
    XVariant* readDate = NULL;
    XVariant* readDatetime = NULL;
    XVariant* readTime = NULL;
    XVariant* readWideTime = NULL;
    const XDate* dateRef;
    const XDateTime* datetimeRef;
    XString* timeText = NULL;
    XString* wideTimeText = NULL;
    const char* timeUtf8;
    const char* wideTimeUtf8;
    bool ok = name && score && flag && category && dateValue && datetimeValue && timeValue;

    sql = xmysql_test_sql("INSERT INTO %s (name, score, flag, category_id, date_value, datetime_value, time_value) VALUES (?, ?, ?, ?, ?, ?, ?)", table, NULL);
    query = XSqlQuery_create_database(database);
    ok = ok && query && sql && XSqlQuery_prepare(query, sql);
    if (ok) {
        XSqlQuery_bindValue(query, 0, name, XSqlParamType_In);
        XSqlQuery_bindValue(query, 1, score, XSqlParamType_In);
        XSqlQuery_bindValue(query, 2, flag, XSqlParamType_In);
        XSqlQuery_bindValue(query, 3, category, XSqlParamType_In);
        XSqlQuery_bindValue(query, 4, dateValue, XSqlParamType_In);
        XSqlQuery_bindValue(query, 5, datetimeValue, XSqlParamType_In);
        XSqlQuery_bindValue(query, 6, timeValue, XSqlParamType_In);
        ok = XSqlQuery_exec(query);
    }
    XPrintf("MySQL：日期时间对象绑定写入：%s\n", ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("日期时间绑定错误", error);
        if (error) XSqlError_delete_base(error);
    }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }

    sql = xmysql_test_sql("SELECT date_value, datetime_value, time_value, CAST('838:59:59' AS TIME) AS wide_time FROM %s WHERE name = '日期绑定'", table, NULL);
    query = ok && sql ? XSqlDatabase_exec(database, sql) : NULL;
    ok = ok && query && XSqlQuery_first(query);
    if (ok) {
        readDate = XSqlQuery_value(query, 0);
        readDatetime = XSqlQuery_value(query, 1);
        readTime = XSqlQuery_value(query, 2);
        readWideTime = XSqlQuery_value(query, 3);
        dateRef = readDate ? XVariant_toDate_ref(readDate) : NULL;
        datetimeRef = readDatetime ? XVariant_toDateTime_ref(readDatetime) : NULL;
        timeText = readTime ? XVariant_toString(readTime) : NULL;
        wideTimeText = readWideTime ? XVariant_toString(readWideTime) : NULL;
        timeUtf8 = timeText ? XString_toUtf8(timeText) : NULL;
        wideTimeUtf8 = wideTimeText ? XString_toUtf8(wideTimeText) : NULL;
        ok = readDate && XVariant_type(readDate) == XVariantType_Date
            && dateRef && XDate_year(dateRef) == 2024 && XDate_month(dateRef) == 2
            && XDate_day(dateRef) == 29
            && readDatetime && XVariant_type(readDatetime) == XVariantType_DateTime
            && datetimeRef && XDateTime_date(datetimeRef).m_jd == date.m_jd
            && XTime_msec(&datetimeRef->m_time) == 123
            && readTime && XVariant_type(readTime) == XVariantType_String
            && timeUtf8 && strncmp(timeUtf8, "23:45:06.123", 12) == 0
            && readWideTime && XVariant_type(readWideTime) == XVariantType_String
            && wideTimeUtf8 && strncmp(wideTimeUtf8, "838:59:59", 9) == 0;
    }
    XPrintf("MySQL：DATE/DATETIME 类型读取、TIME 字符串范围兼容：%s\n", ok ? "通过" : "失败");
    if (!ok && query) {
        XSqlError* error = XSqlQuery_lastError(query);
        xmysql_test_print_error("日期时间读取错误", error);
        if (error) XSqlError_delete_base(error);
    }
    if (timeText) XString_delete_base(timeText);
    if (wideTimeText) XString_delete_base(wideTimeText);
    if (readDate) XVariant_delete_base(readDate);
    if (readDatetime) XVariant_delete_base(readDatetime);
    if (readTime) XVariant_delete_base(readTime);
    if (readWideTime) XVariant_delete_base(readWideTime);
    if (query) XSqlQuery_delete_base(query);
    if (sql) XString_delete_base(sql);
    if (name) XVariant_delete_base(name);
    if (score) XVariant_delete_base(score);
    if (flag) XVariant_delete_base(flag);
    if (category) XVariant_delete_base(category);
    if (dateValue) XVariant_delete_base(dateValue);
    if (datetimeValue) XVariant_delete_base(datetimeValue);
    if (timeValue) XVariant_delete_base(timeValue);
    return ok;
}

static void xmysql_test_batch_value(XVariantList* list, XVariant* value)
{
    if (list && value) XVariantList_push_back_base(list, value);
    if (value) XVariant_delete_base(value);
}

static bool xmysql_test_extended_features(XSqlDatabase* database, const XString* table)
{
    XSqlDriver* driver = XSqlDatabase_driver(database);
    XSqlQuery* query = NULL;
    XString* sql = NULL;
    XVariant* value = NULL;
    XString* text = NULL;
    XVariantList* names = NULL;
    XVariantList* scores = NULL;
    XVariantList* flags = NULL;
    XVariantList* categories = NULL;
    XVariant* listValue = NULL;
    bool timeZoneOk;
    bool featureOk = driver
        && XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_Transactions)
        && XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_PreparedQueries)
        && XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_MultipleResultSets)
        && !XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_CancelQuery)
        && !XSqlDriver_hasFeature_base(driver, XSqlDriverFeature_BatchOperations);
    XPrintf("MySQL：服务端预处理、多结果和 Qt 能力声明：%s\n",
           featureOk ? "通过" : "失败");

    query = XSqlDatabase_exec_utf8(database, "SELECT @@session.time_zone AS session_time_zone");
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value_utf8(query, "session_time_zone");
    text = value ? XVariant_toString(value) : NULL;
    timeZoneOk = text
        && XString_equals_utf8(text, "+00:00", XChar_CaseSensitive);
    featureOk = featureOk && timeZoneOk;
    XPrintf("MySQL：Qt 6.8 UTC 会话时区：%s\n", timeZoneOk ? "通过" : "失败");
    if (text) { XString_delete_base(text); text = NULL; }
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    sql = xmysql_test_sql("SELECT flag FROM %s ORDER BY id LIMIT 1", table, NULL);
    query = XSqlQuery_create_database(database);
    if (query && sql && XSqlQuery_prepare(query, sql) && XSqlQuery_exec(query)
        && XSqlQuery_first(query))
        value = XSqlQuery_value(query, 0);
    featureOk = featureOk && value && XVariant_type(value) == XVariantType_Int32
        && XVariant_toInt32(value) == 1;
    XPrintf("MySQL：二进制预处理读取 TINYINT：%s\n", featureOk ? "通过" : "失败");
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }

    XSqlDatabase_setNumericalPrecisionPolicy(database,
        XSqlNumericalPrecisionPolicy_LowPrecisionInt32);
    query = XSqlDatabase_exec_utf8(database,
        "SELECT CAST('123.456789012345678901234567890' AS DECIMAL(30,27)) AS decimal_value");
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value_utf8(query, "decimal_value");
    featureOk = featureOk && value && XVariant_type(value) == XVariantType_Int32
        && XVariant_toInt32(value) == 123;
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    XSqlDatabase_setNumericalPrecisionPolicy(database,
        XSqlNumericalPrecisionPolicy_LowPrecisionInt64);
    query = XSqlDatabase_exec_utf8(database,
        "SELECT CAST('123.456789012345678901234567890' AS DECIMAL(30,27)) AS decimal_value");
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value_utf8(query, "decimal_value");
    featureOk = featureOk && value && XVariant_type(value) == XVariantType_Int64
        && XVariant_toInt64(value) == 123;
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    XSqlDatabase_setNumericalPrecisionPolicy(database,
        XSqlNumericalPrecisionPolicy_LowPrecisionDouble);
    query = XSqlDatabase_exec_utf8(database,
        "SELECT CAST('123.456789012345678901234567890' AS DECIMAL(30,27)) AS decimal_value");
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value_utf8(query, "decimal_value");
    featureOk = featureOk && value && XVariant_type(value) == XVariantType_Double;
    if (value) { XVariant_delete_base(value); value = NULL; }
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    XSqlDatabase_setNumericalPrecisionPolicy(database,
        XSqlNumericalPrecisionPolicy_HighPrecision);
    query = XSqlDatabase_exec_utf8(database,
        "SELECT CAST('123.456789012345678901234567890' AS DECIMAL(30,27)) AS decimal_value");
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value_utf8(query, "decimal_value");
    text = value ? XVariant_toString(value) : NULL;
    featureOk = featureOk && text
        && XString_equals_utf8(text, "123.456789012345678901234567890", XChar_CaseSensitive);
    XPrintf("MySQL：DECIMAL 数值精度策略和高精度保持：%s\n", featureOk ? "通过" : "失败");
    if (text) XString_delete_base(text);
    if (value) XVariant_delete_base(value);
    if (query) { XSqlQuery_delete_base(query); query = NULL; }

    names = XVariantList_create();
    scores = XVariantList_create();
    flags = XVariantList_create();
    categories = XVariantList_create();
    xmysql_test_batch_value(names, xmysql_test_string_value("批量一"));
    xmysql_test_batch_value(names, xmysql_test_string_value("批量二"));
    xmysql_test_batch_value(scores, XVariant_create_double(8.5));
    xmysql_test_batch_value(scores, XVariant_create_double(9.5));
    xmysql_test_batch_value(flags, XVariant_create_bool(true));
    xmysql_test_batch_value(flags, XVariant_create_bool(true));
    xmysql_test_batch_value(categories, XVariant_create_int64(1));
    xmysql_test_batch_value(categories, XVariant_create_int64(2));
    sql = xmysql_test_sql("INSERT INTO %s (name, score, flag, category_id) VALUES (?, ?, ?, ?)",
                          table, NULL);
    query = XSqlQuery_create_database(database);
    if (names && scores && flags && categories && sql && query
        && XSqlQuery_prepare(query, sql)) {
        listValue = XVariant_create_list(names);
        XSqlQuery_bindValue(query, 0, listValue, XSqlParamType_In);
        if (listValue) { XVariant_delete_base(listValue); listValue = NULL; }
        listValue = XVariant_create_list(scores);
        XSqlQuery_bindValue(query, 1, listValue, XSqlParamType_In);
        if (listValue) { XVariant_delete_base(listValue); listValue = NULL; }
        listValue = XVariant_create_list(flags);
        XSqlQuery_bindValue(query, 2, listValue, XSqlParamType_In);
        if (listValue) { XVariant_delete_base(listValue); listValue = NULL; }
        listValue = XVariant_create_list(categories);
        XSqlQuery_bindValue(query, 3, listValue, XSqlParamType_In);
        if (listValue) { XVariant_delete_base(listValue); listValue = NULL; }
        featureOk = featureOk && XSqlQuery_execBatch(query, XSqlBatchExecutionMode_ValuesAsRows)
            && XSqlQuery_numRowsAffected(query) == 2;
        featureOk = featureOk && XSqlQuery_execBatch(query, XSqlBatchExecutionMode_ValuesAsColumns)
            && XSqlQuery_numRowsAffected(query) == 2;
    } else {
        featureOk = false;
    }
    XPrintf("MySQL：ValuesAsRows/ValuesAsColumns 批量回退：%s\n", featureOk ? "通过" : "失败");
    if (query) XSqlQuery_delete_base(query);
    if (sql) XString_delete_base(sql);
    if (names) XVariantList_delete_base(names);
    if (scores) XVariantList_delete_base(scores);
    if (flags) XVariantList_delete_base(flags);
    if (categories) XVariantList_delete_base(categories);

    if (xmysql_test_env_set("XMYSQL_TEST_MULTI_RESULTS")) {
        XSqlRecord* emptyRecord = NULL;
        bool multiResultOk = true;
        query = XSqlDatabase_exec_utf8(database, "SELECT 1 AS first_value; SELECT 2 AS second_value");
        if (query && XSqlQuery_first(query)) {
            value = XSqlQuery_value(query, 0);
            multiResultOk = multiResultOk && value && XVariant_toInt64(value) == 1;
            if (value) { XVariant_delete_base(value); value = NULL; }
        } else {
            multiResultOk = false;
        }
        if (query && XSqlQuery_nextResult(query) && XSqlQuery_first(query)) {
            value = XSqlQuery_value(query, 0);
            multiResultOk = multiResultOk && value && XVariant_toInt64(value) == 2;
            if (value) XVariant_delete_base(value);
        } else {
            multiResultOk = false;
        }
        multiResultOk = multiResultOk && query && !XSqlQuery_nextResult(query)
            && !XSqlQuery_isActive(query)
            && XSqlQuery_at(query) == XSqlLocation_BeforeFirstRow
            && XSqlQuery_size(query) == -1
            && XSqlQuery_numRowsAffected(query) == -1;
        emptyRecord = query ? XSqlQuery_record(query) : NULL;
        multiResultOk = multiResultOk && emptyRecord && XSqlRecord_count(emptyRecord) == 0;
        XPrintf("MySQL：多语句多结果集及结束状态：%s\n", multiResultOk ? "通过" : "失败");
        featureOk = featureOk && multiResultOk;
        if (emptyRecord) XSqlRecord_delete_base(emptyRecord);
        if (query) XSqlQuery_delete_base(query);
    }
    return featureOk;
}

static bool xmysql_test_local_infile(XSqlDatabase* database)
{
    static const char pathText[] = "/tmp/xin_sql_mysql_local_infile.csv";
    const char* options;
    XString* path = NULL;
    XString* table = NULL;
    XString* sql = NULL;
    XSqlQuery* query = NULL;
    XSqlQuery* variableQuery = NULL;
    XFile* file = NULL;
    XVariant* variable = NULL;
    XVariant* value = NULL;
    XString* variableText = NULL;
    XString* valueText = NULL;
    bool ok = true;
    const char* rowText = "local-infile-row\n";

    options = getenv("XMYSQL_TEST_OPTIONS");
    if (!options || !strstr(options, "MYSQL_OPT_LOCAL_INFILE=1")) {
        XPrintf("MySQL：LOAD DATA LOCAL INFILE：未显式启用，跳过\n");
        return true;
    }
    variableQuery = XSqlDatabase_exec_utf8(database,
        "SHOW VARIABLES LIKE 'local_infile'");
    if (variableQuery && XSqlQuery_first(variableQuery))
        variable = XSqlQuery_value(variableQuery, 1);
    variableText = variable ? XVariant_toString(variable) : NULL;
    if (!variableText || !XString_equals_utf8(variableText, "ON", XChar_CaseInsensitive)) {
        XPrintf("MySQL：LOAD DATA LOCAL INFILE：服务器未启用，跳过\n");
        if (variableText) XString_delete_base(variableText);
        if (variable) XVariant_delete_base(variable);
        if (variableQuery) XSqlQuery_delete_base(variableQuery);
        return true;
    }
    path = XString_create_utf8(pathText);
    table = xmysql_test_quoted_name(database, "xin_sql_mysql_local_infile");
    XFile_remove_static(path);
    file = path ? XFile_create_2(path) : NULL;
    ok = file && XFile_open_2(file, XIODevice_WriteOnly | XIODevice_NewOnly, 0)
        && XIODevice_write_1((XIODevice*)file, rowText, (int64_t)strlen(rowText))
            == (int64_t)strlen(rowText);
    if (file) {
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    }
    file = NULL;
    sql = table ? XString_create_fmt_utf8("DROP TABLE IF EXISTS %s", XString_toUtf8(table)) : NULL;
    ok = ok && sql && xmysql_test_statement(database, "清理 LOCAL INFILE 测试表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = table ? XString_create_fmt_utf8("CREATE TABLE %s (value VARCHAR(128) NOT NULL)",
                                          XString_toUtf8(table)) : NULL;
    ok = ok && sql && xmysql_test_statement(database, "创建 LOCAL INFILE 测试表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = ok ? XString_create_fmt_utf8(
        "LOAD DATA LOCAL INFILE '%s' INTO TABLE %s FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n' (value)",
        pathText, XString_toUtf8(table)) : NULL;
    query = sql ? XSqlDatabase_exec(database, sql) : NULL;
    ok = ok && query && XSqlQuery_isActive(query);
    if (query) { XSqlQuery_delete_base(query); query = NULL; }
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = ok ? XString_create_fmt_utf8("SELECT value FROM %s", XString_toUtf8(table)) : NULL;
    query = sql ? XSqlDatabase_exec(database, sql) : NULL;
    if (query && XSqlQuery_first(query)) value = XSqlQuery_value(query, 0);
    valueText = value ? XVariant_toString(value) : NULL;
    ok = ok && valueText && XString_equals_utf8(valueText, "local-infile-row", XChar_CaseSensitive);
    XPrintf("MySQL：LOAD DATA LOCAL INFILE：%s\n", ok ? "通过" : "失败");
    if (query) XSqlQuery_delete_base(query);
    if (sql) XString_delete_base(sql);
    if (valueText) XString_delete_base(valueText);
    if (value) XVariant_delete_base(value);
    if (variableText) XString_delete_base(variableText);
    if (variable) XVariant_delete_base(variable);
    if (variableQuery) XSqlQuery_delete_base(variableQuery);
    if (table) {
        sql = XString_create_fmt_utf8("DROP TABLE IF EXISTS %s", XString_toUtf8(table));
        if (sql) { xmysql_test_statement(database, "删除 LOCAL INFILE 测试表", XString_toUtf8(sql)); XString_delete_base(sql); }
        XString_delete_base(table);
    }
    if (path) {
        XFile_remove_static(path);
        XString_delete_base(path);
    }
    return ok;
}

bool XSqlMySqlTest_run(void)
{
    const char* host = xmysql_test_env("XMYSQL_TEST_HOST", "127.0.0.1");
    const bool useTemporaryDatabase = !xmysql_test_env_set("XMYSQL_TEST_DATABASE");
    const char* databaseName = useTemporaryDatabase
        ? XMYSQL_TEST_TEMP_DATABASE : xmysql_test_env("XMYSQL_TEST_DATABASE", "test");
    const char* user = getenv("XMYSQL_TEST_USER");
    const char* password = xmysql_test_env("XMYSQL_TEST_PASSWORD", "");
    const char* tableName = xmysql_test_env("XMYSQL_TEST_TABLE", "xin_sql_mysql_test");
    XSqlDatabase* database = NULL;
    XSqlDriver* driver = NULL;
    XStringList* drivers = NULL;
    XString* driverName = NULL;
    XString* hostText = NULL;
    XString* databaseText = NULL;
    XString* userText = NULL;
    XString* passwordText = NULL;
    XString* optionsText = NULL;
    XString* table = NULL;
    XString* category = NULL;
    XString* sql = NULL;
    bool temporaryDatabaseCreated = false;
    bool ok = true;

    if (!user || !user[0]) {
        XPrintf("MySQL：未配置服务器，已跳过真实联调。\n");
        XPrintf("MySQL：请设置 XMYSQL_TEST_HOST、XMYSQL_TEST_PORT、XMYSQL_TEST_DATABASE、XMYSQL_TEST_USER、XMYSQL_TEST_PASSWORD。未设置数据库时会自动创建临时测试库。\n");
        return true;
    }
    XPrintf("\n开始---------------MySQL/MariaDB 联调测试---------------\n");
    XPrintf("MySQL：服务器 %s:%d，数据库 %s，用户 %s\n", host, xmysql_test_port(), databaseName, user);
    database = XSqlDatabase_addDatabase(XSqlDriverType_MySql, "xsql-mysql-live");
    drivers = XSqlDatabase_drivers();
    driver = database ? XSqlDatabase_driver(database) : NULL;
    driverName = database ? XSqlDatabase_driverName(database) : NULL;
    hostText = XString_create_utf8(host);
    databaseText = XString_create_utf8(databaseName);
    userText = XString_create_utf8(user);
    passwordText = XString_create_utf8(password);
    optionsText = XString_create_utf8(xmysql_test_env("XMYSQL_TEST_OPTIONS", ""));
    table = xmysql_test_quoted_name(database, tableName);
    category = xmysql_test_quoted_name(database, "xin_sql_mysql_category");
    ok = database && drivers && driver && driverName
        && XStringList_contains_utf8(drivers, "QMYSQL", XChar_CaseInsensitive)
        && XSqlDriver_driverType(driver) == XSqlDriverType_MySql
        && XString_equals_utf8(driverName, "QMYSQL", XChar_CaseSensitive)
        && hostText && databaseText && userText && passwordText && optionsText && table && category;
    XPrintf("MySQL：驱动注册和连接管理：%s\n", ok ? "通过" : "失败");
    if (ok && useTemporaryDatabase)
        ok = xmysql_test_prepare_temporary_database(host, xmysql_test_port(), user,
                                                     password, &temporaryDatabaseCreated);
    if (ok) {
        XSqlDatabase_setHostName(database, hostText);
        XSqlDatabase_setPort(database, xmysql_test_port());
        XSqlDatabase_setDatabaseName(database, databaseText);
        XSqlDatabase_setUserName(database, userText);
        XSqlDatabase_setPassword(database, passwordText);
        XSqlDatabase_setConnectOptions(database, optionsText);
        ok = XSqlDatabase_open(database);
    }
    XPrintf("MySQL：打开连接：%s\n", ok ? "通过" : "失败");
    if (!ok && database) {
        XSqlError* error = XSqlDatabase_lastError(database);
        xmysql_test_print_error("连接错误", error);
        if (error) XSqlError_delete_base(error);
        goto cleanup;
    }

    if (xmysql_test_env_set("XMYSQL_TEST_RSA"))
        ok = ok && xmysql_test_caching_sha2_rsa(host, xmysql_test_port(), user, password);

    sql = xmysql_test_sql("DROP TABLE IF EXISTS %s", category, NULL);
    ok = ok && sql && xmysql_test_statement(database, "清理分类表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = xmysql_test_sql("DROP TABLE IF EXISTS %s", table, NULL);
    ok = ok && sql && xmysql_test_statement(database, "清理测试表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = xmysql_test_sql("CREATE TABLE %s (id BIGINT NOT NULL AUTO_INCREMENT, title VARCHAR(128) NOT NULL, PRIMARY KEY (id))", category, NULL);
    ok = ok && sql && xmysql_test_statement(database, "创建分类表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    sql = xmysql_test_sql("CREATE TABLE %s (id BIGINT NOT NULL AUTO_INCREMENT, name VARCHAR(128) NOT NULL, score DOUBLE NOT NULL, flag TINYINT NOT NULL, payload BLOB NULL, note VARCHAR(128) NULL, category_id BIGINT NOT NULL, date_value DATE NULL, datetime_value DATETIME(6) NULL, time_value TIME(6) NULL, PRIMARY KEY (id))", table, NULL);
    ok = ok && sql && xmysql_test_statement(database, "创建测试表", XString_toUtf8(sql));
    if (sql) { XString_delete_base(sql); sql = NULL; }
    ok = ok && xmysql_test_basic_queries(database, table, category);
    ok = ok && xmysql_test_transactions(database, table);
    ok = ok && xmysql_test_metadata_and_models(database, table, category);
    ok = ok && xmysql_test_table_model_writes(database, table);
    ok = ok && xmysql_test_temporal_values(database, table);
    ok = ok && xmysql_test_extended_features(database, table);
    ok = ok && xmysql_test_local_infile(database);
    XPrintf("MySQL：完整联调结果：%s\n", ok ? "通过" : "失败");

cleanup:
    if (database && XSqlDatabase_isOpen(database)) {
        sql = xmysql_test_sql("DROP TABLE IF EXISTS %s", table, NULL);
        if (sql) { xmysql_test_statement(database, "删除测试表", XString_toUtf8(sql)); XString_delete_base(sql); sql = NULL; }
        sql = xmysql_test_sql("DROP TABLE IF EXISTS %s", category, NULL);
        if (sql) { xmysql_test_statement(database, "删除分类表", XString_toUtf8(sql)); XString_delete_base(sql); sql = NULL; }
        XSqlDatabase_close(database);
    }
    XSqlDatabase_removeDatabase("xsql-mysql-live");
    if (database) XSqlDatabase_delete_base(database);
    if (temporaryDatabaseCreated)
        xmysql_test_drop_temporary_database(host, xmysql_test_port(), user, password);
    if (drivers) XStringList_delete_base(drivers);
    if (driverName) XString_delete_base(driverName);
    if (hostText) XString_delete_base(hostText);
    if (databaseText) XString_delete_base(databaseText);
    if (userText) XString_delete_base(userText);
    if (passwordText) XString_delete_base(passwordText);
    if (optionsText) XString_delete_base(optionsText);
    if (table) XString_delete_base(table);
    if (category) XString_delete_base(category);
    XPrintf("结束---------------MySQL/MariaDB 联调测试---------------\n\n");
    return ok;
}
