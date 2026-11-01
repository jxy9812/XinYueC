/**
 * @file       XSqlGlobal.c
 * @brief      SQL 公共枚举辅助函数。
 */
#if XSQL_ON

#include "XSqlGlobal.h"

#include "XStringUtils.h" /* strcmp 直出改经 XStrcmp（外部依赖约束） */

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
    if (XStrcmp(name, "XMSSQLSERVER") == 0 || XStrcmp(name, "mssql") == 0 || XStrcmp(name, "sqlserver") == 0)
        return XSqlDriverType_MsSqlServer;
    if (XStrcmp(name, "QSQLITE") == 0 || XStrcmp(name, "sqlite") == 0)
        return XSqlDriverType_Sqlite;
    if (XStrcmp(name, "QMYSQL") == 0 || XStrcmp(name, "mysql") == 0 || XStrcmp(name, "mariadb") == 0)
        return XSqlDriverType_MySql;
    if (XStrcmp(name, "QPSQL") == 0 || XStrcmp(name, "psql") == 0 || XStrcmp(name, "postgresql") == 0)
        return XSqlDriverType_PostgreSql;
    if (XStrcmp(name, "QOCI") == 0 || XStrcmp(name, "oci") == 0 || XStrcmp(name, "oracle") == 0)
        return XSqlDriverType_Oracle;
    if (XStrcmp(name, "QTDS") == 0 || XStrcmp(name, "tds") == 0 || XStrcmp(name, "sybase") == 0)
        return XSqlDriverType_Sybase;
    if (XStrcmp(name, "QIBASE") == 0 || XStrcmp(name, "ibase") == 0 || XStrcmp(name, "firebird") == 0)
        return XSqlDriverType_Interbase;
    if (XStrcmp(name, "QDB2") == 0 || XStrcmp(name, "db2") == 0)
        return XSqlDriverType_Db2;
    if (XStrcmp(name, "QMIMER") == 0 || XStrcmp(name, "mimer") == 0)
        return XSqlDriverType_MimerSql;
    if (XStrcmp(name, "QODBC") == 0 || XStrcmp(name, "odbc") == 0)
        return XSqlDriverType_Odbc;
    if (XStrcmp(name, "XEMBEDDED") == 0 || XStrcmp(name, "embedded") == 0)
        return XSqlDriverType_Embedded;
    if (XStrcmp(name, "XCUSTOM") == 0 || XStrcmp(name, "custom") == 0)
        return XSqlDriverType_Custom;
    return XSqlDriverType_Unknown;
}

#endif /* XSQL_ON */
