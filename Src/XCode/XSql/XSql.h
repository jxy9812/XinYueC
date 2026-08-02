/**
 * @file       XSql.h
 * @brief      XinYueC SQL 公共 API 聚合头文件。
 * @details    所有 SQL 公共类都通过抽象驱动工作；具体数据库后端在源码中
 *             继承 XSqlDriver 并静态注册，不依赖动态插件和平台 API。
 */
#ifndef XSQL_H
#define XSQL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlGlobal.h"
#include "XSqlError.h"
#include "XSqlField.h"
#include "XSqlRecord.h"
#include "XSqlIndex.h"
#include "XSqlResult.h"
#include "XSqlDriver.h"
#include "XSqlDatabase.h"
#include "XSqlQuery.h"
#include "XSqlQueryModel.h"
#include "XSqlTableModel.h"
#include "XSqlRelation.h"
#include "XSqlRelationalTableModel.h"
#include "XSqlRelationalDelegate.h"
#include "XSqlDriverPlugin.h"
#include "XMySqlDriver.h"
#include "XSqliteDriver.h"

#ifdef __cplusplus
}
#endif

#endif /* XSQL_H */
