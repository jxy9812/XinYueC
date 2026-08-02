/**
 * @file       XSqlGlobal.c
 * @brief      SQL 公共枚举辅助函数。
 */
#include "XSqlGlobal.h"

#include <string.h>

const char* XSqlDriverType_name(XSqlDriverType type)
{
    switch (type) {
    case XSqlDriverType_MsSqlServer: return "XMSSQLSERVER";
    case XSqlDriverType_MySql: return "QMYSQL";
    case XSqlDriverType_PostgreSql: return "QPSQL";
    case XSqlDriverType_Oracle: return "QOCI";
    case XSqlDriverType_Sybase: return "QTDS";
    case XSqlDriverType_Sqlite: return "QSQLITE";
    case XSqlDriverType_Interbase: return "QIBASE";
    case XSqlDriverType_Db2: return "QDB2";
    case XSqlDriverType_MimerSql: return "QMIMER";
    case XSqlDriverType_Odbc: return "QODBC";
    case XSqlDriverType_Embedded: return "XEMBEDDED";
    case XSqlDriverType_Custom: return "XCUSTOM";
    case XSqlDriverType_Unknown:
    default: return "";
    }
}

XSqlDriverType XSqlDriverType_fromName_utf8(const char* name)
{
    if (!name) return XSqlDriverType_Unknown;
    if (strcmp(name, "XMSSQLSERVER") == 0 || strcmp(name, "mssql") == 0 || strcmp(name, "sqlserver") == 0)
        return XSqlDriverType_MsSqlServer;
    if (strcmp(name, "QSQLITE") == 0 || strcmp(name, "sqlite") == 0)
        return XSqlDriverType_Sqlite;
    if (strcmp(name, "QMYSQL") == 0 || strcmp(name, "mysql") == 0 || strcmp(name, "mariadb") == 0)
        return XSqlDriverType_MySql;
    if (strcmp(name, "QPSQL") == 0 || strcmp(name, "psql") == 0 || strcmp(name, "postgresql") == 0)
        return XSqlDriverType_PostgreSql;
    if (strcmp(name, "QOCI") == 0 || strcmp(name, "oci") == 0 || strcmp(name, "oracle") == 0)
        return XSqlDriverType_Oracle;
    if (strcmp(name, "QTDS") == 0 || strcmp(name, "tds") == 0 || strcmp(name, "sybase") == 0)
        return XSqlDriverType_Sybase;
    if (strcmp(name, "QIBASE") == 0 || strcmp(name, "ibase") == 0 || strcmp(name, "firebird") == 0)
        return XSqlDriverType_Interbase;
    if (strcmp(name, "QDB2") == 0 || strcmp(name, "db2") == 0)
        return XSqlDriverType_Db2;
    if (strcmp(name, "QMIMER") == 0 || strcmp(name, "mimer") == 0)
        return XSqlDriverType_MimerSql;
    if (strcmp(name, "QODBC") == 0 || strcmp(name, "odbc") == 0)
        return XSqlDriverType_Odbc;
    if (strcmp(name, "XEMBEDDED") == 0 || strcmp(name, "embedded") == 0)
        return XSqlDriverType_Embedded;
    if (strcmp(name, "XCUSTOM") == 0 || strcmp(name, "custom") == 0)
        return XSqlDriverType_Custom;
    return XSqlDriverType_Unknown;
}
