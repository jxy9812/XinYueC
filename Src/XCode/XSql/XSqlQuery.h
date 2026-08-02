/**
 * @file       XSqlQuery.h
 * @brief      SQL 查询类，对齐 Qt 6.8 QSqlQuery。
 * @details    查询对象拥有结果对象；驱动实现只通过 XSqlResult 抽象接口接入。
 */
#ifndef XSQLQUERY_H
#define XSQLQUERY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDatabase.h"
#include "XSqlResult.h"

XCLASS_DEFINE_BEGING(XSqlQuery)
XCLASS_DEFINE_EXTEND_END(XSqlQuery, XClass)

/**
 * @brief SQL 查询对象。
 */
typedef struct XSqlQuery {
    XClass m_class;          /**< 第一个成员，由 XClass 管理。 */
    XSqlResult* m_result;    /**< 结果对象，由查询对象拥有。 */
    bool m_ownsResult;       /**< 是否负责释放结果对象。 */
} XSqlQuery;

/**
 * @brief 初始化查询对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlQuery_class_init(void);
/**
 * @brief 初始化空查询。
 * @param query 待初始化查询；不能为 NULL。
 * @return 无。
 */
void XSqlQuery_init(XSqlQuery* query);
/**
 * @brief 使用数据库连接初始化查询。
 * @param query 待初始化查询；不能为 NULL。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 无；查询会创建结果对象并借用驱动。
 */
void XSqlQuery_init_database(XSqlQuery* query, const XSqlDatabase* database);
/**
 * @brief 创建空查询。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败返回 NULL。
 */
XSqlQuery* XSqlQuery_create(void);
/**
 * @brief 使用数据库连接创建查询。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败返回 NULL。
 */
XSqlQuery* XSqlQuery_create_database(const XSqlDatabase* database);
/**
 * @brief 使用结果对象创建查询。
 * @param result 结果对象；转移所有权，可为 NULL。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败时 result 仍由调用者负责。
 */
XSqlQuery* XSqlQuery_create_result(XSqlResult* result);
/**
 * @brief 深拷贝创建查询。
 * @param other 源查询；借用，不能为 NULL。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败返回 NULL。
 */
XSqlQuery* XSqlQuery_create_copy(const XSqlQuery* other);
/**
 * @brief 移动创建查询。
 * @param other 源查询；不能为 NULL，成功后资源被移出。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放；失败返回 NULL。
 */
XSqlQuery* XSqlQuery_create_move(XSqlQuery* other);
/**
 * @brief 交换两个查询对象内容。
 * @param left 左查询对象；不能为 NULL，且必须已初始化。
 * @param right 右查询对象；不能为 NULL，且必须已初始化。
 * @return 无；结果、绑定值和所有权标志一并交换。
 */
void XSqlQuery_swap(XSqlQuery* left, XSqlQuery* right);

/** @brief 调用 XClass 析构入口释放查询对象内部资源。 */
#define XSqlQuery_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlQuery_create 系列函数返回的查询对象。 */
#define XSqlQuery_delete_base XClass_delete_base
/** @brief 将查询对象深复制到既有目标对象的基础复制入口。 */
#define XSqlQuery_copy_base XClass_copy_base
/** @brief 将查询对象资源移入既有目标对象的基础移动入口。 */
#define XSqlQuery_move_base XClass_move_base

/** @brief 判断查询是否位于有效记录。 @param query 查询对象；可为 NULL。 @return 位于有效记录返回 true，否则返回 false。 */
bool XSqlQuery_isValid(const XSqlQuery* query);
/** @brief 判断查询是否激活。 @param query 查询对象；可为 NULL。 @return 已激活返回 true，否则返回 false。 */
bool XSqlQuery_isActive(const XSqlQuery* query);
/** @brief 判断当前记录字段是否为 SQL NULL。 @param query 查询对象；可为 NULL。 @param field 字段索引，从 0 开始。 @return 字段为 NULL、查询无效或越界返回 true。 */
bool XSqlQuery_isNull(const XSqlQuery* query, int field);
/** @brief 按 UTF-8 字段名判断当前字段是否为 SQL NULL。 @param query 查询对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段为 NULL、名称不存在或查询无效返回 true。 */
bool XSqlQuery_isNull_utf8(const XSqlQuery* query, const char* name);
/** @brief 按 XString 字段名判断当前字段是否为 SQL NULL。 @param query 查询对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 字段为 NULL、名称不存在或查询无效返回 true。 */
bool XSqlQuery_isNull_2(const XSqlQuery* query, const XString* name);
/** @brief 获取当前游标位置。 @param query 查询对象；可为 NULL。 @return 行索引；无效位置使用 XSqlLocation 特殊值。 */
int XSqlQuery_at(const XSqlQuery* query);
/** @brief 获取最近设置的查询文本副本。 @param query 查询对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlQuery_lastQuery(const XSqlQuery* query);
/** @brief 获取最近语句的受影响行数。 @param query 查询对象；可为 NULL。 @return 行数；不可用时返回 -1。 */
int XSqlQuery_numRowsAffected(const XSqlQuery* query);
/** @brief 获取最近错误副本。 @param query 查询对象；可为 NULL。 @return 新错误所有权；调用者使用 XSqlError_delete_base 释放。 */
XSqlError* XSqlQuery_lastError(const XSqlQuery* query);
/** @brief 判断当前结果是否为 SELECT 结果集。 @param query 查询对象；可为 NULL。 @return 是 SELECT 返回 true，否则返回 false。 */
bool XSqlQuery_isSelect(const XSqlQuery* query);
/** @brief 获取结果行数。 @param query 查询对象；可为 NULL。 @return 行数；驱动未知时返回 -1。 */
int XSqlQuery_size(const XSqlQuery* query);
/** @brief 获取查询使用的驱动借用指针。 @param query 查询对象；可为 NULL。 @return 内部驱动借用指针；不得释放，查询销毁后失效。 */
const XSqlDriver* XSqlQuery_driver(const XSqlQuery* query);
/** @brief 获取内部结果借用指针。 @param query 查询对象；可为 NULL。 @return 内部结果指针；不得释放，查询清空或销毁后失效。 */
const XSqlResult* XSqlQuery_result(const XSqlQuery* query);
/** @brief 获取只向前遍历状态。 @param query 查询对象；可为 NULL。 @return 启用时返回 true，否则返回 false。 */
bool XSqlQuery_isForwardOnly(const XSqlQuery* query);
/** @brief 获取当前记录描述副本。 @param query 查询对象；可为 NULL。 @return 新记录所有权；调用者使用 XSqlRecord_delete_base 释放。 */
XSqlRecord* XSqlQuery_record(const XSqlQuery* query);
/** @brief 设置查询是否只向前遍历。 @param query 查询对象；不能为 NULL。 @param forwardOnly 是否启用只向前模式。 @return 无；已激活查询改变模式可能失败或清空结果。 */
void XSqlQuery_setForwardOnly(XSqlQuery* query, bool forwardOnly);
/** @brief 执行 SQL 文本并替换当前结果。 @param query 查询对象；不能为 NULL。 @param sql SQL 文本；借用，调用期间有效。 @return 执行成功返回 true，否则返回 false，并保留最近错误。 */
bool XSqlQuery_exec_query(XSqlQuery* query, const XString* sql);
/** @brief 执行 UTF-8 SQL 文本并替换当前结果。 @param query 查询对象；不能为 NULL。 @param sql SQL 字符串；借用，调用期间有效，可为 NULL。 @return 执行成功返回 true，否则返回 false。 */
bool XSqlQuery_exec_utf8(XSqlQuery* query, const char* sql);
/** @brief 获取当前记录字段值副本。 @param query 查询对象；可为 NULL。 @param field 字段索引，从 0 开始。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，越界返回空值对象。 */
XVariant* XSqlQuery_value(const XSqlQuery* query, int field);
/** @brief 按 UTF-8 字段名获取当前值副本。 @param query 查询对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，未找到返回空值对象。 */
XVariant* XSqlQuery_value_utf8(const XSqlQuery* query, const char* name);
/** @brief 按 XString 字段名获取当前值副本。 @param query 查询对象；可为 NULL。 @param name 字段名；借用，可为 NULL。 @return 新值所有权；调用者使用 XVariant_delete_base 释放，未找到返回空值对象。 */
XVariant* XSqlQuery_value_2(const XSqlQuery* query, const XString* name);
/**
 * @brief 设置数值精度策略。
 * @param query 查询对象；不能为 NULL。
 * @param policy 数值精度策略。
 * @return 无。
 */
void XSqlQuery_setNumericalPrecisionPolicy(XSqlQuery* query, XSqlNumericalPrecisionPolicy policy);
/**
 * @brief 获取数值精度策略。
 * @param query 查询对象；NULL 返回 HighPrecision。
 * @return 当前策略。
 */
XSqlNumericalPrecisionPolicy XSqlQuery_numericalPrecisionPolicy(const XSqlQuery* query);
/**
 * @brief 设置是否启用位置绑定。
 * @param query 查询对象；不能为 NULL。
 * @param enable 是否启用。
 * @return 无。
 */
void XSqlQuery_setPositionalBindingEnabled(XSqlQuery* query, bool enable);
/**
 * @brief 查询位置绑定是否启用。
 * @param query 查询对象；NULL 返回 false。
 * @return 已启用返回 true，否则返回 false。
 */
bool XSqlQuery_isPositionalBindingEnabled(const XSqlQuery* query);
/**
 * @brief 移动到指定记录。
 * @param query 查询对象；不能为 NULL。
 * @param index 目标索引，从 0 开始。
 * @param relative 是否相对当前位置移动。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XSqlQuery_seek(XSqlQuery* query, int index, bool relative);
/** @brief 移动到下一条记录。 @param query 查询对象；不能为 NULL。 @return 成功定位返回 true；没有下一条记录或读取失败返回 false。 */
bool XSqlQuery_next(XSqlQuery* query);
/** @brief 移动到上一条记录。 @param query 查询对象；不能为 NULL。 @return 成功定位返回 true；只向前模式、没有上一条记录或读取失败返回 false。 */
bool XSqlQuery_previous(XSqlQuery* query);
/** @brief 移动到第一条记录。 @param query 查询对象；不能为 NULL。 @return 成功定位返回 true；结果为空或读取失败返回 false。 */
bool XSqlQuery_first(XSqlQuery* query);
/** @brief 移动到最后一条记录。 @param query 查询对象；不能为 NULL。 @return 成功定位返回 true；只向前模式、结果为空或读取失败返回 false。 */
bool XSqlQuery_last(XSqlQuery* query);
/**
 * @brief 清空查询结果和绑定参数。
 * @param query 查询对象；不能为 NULL。
 * @return 无；查询保持已初始化状态。
 */
void XSqlQuery_clear(XSqlQuery* query);
/**
 * @brief 执行已准备查询。
 * @param query 查询对象；不能为 NULL。
 * @return 执行成功返回 true，否则返回 false。
 */
bool XSqlQuery_exec(XSqlQuery* query);
/**
 * @brief 批量执行查询。
 * @param query 查询对象；不能为 NULL。
 * @param mode 批量展开模式。
 * @return 执行成功返回 true，否则返回 false。
 */
bool XSqlQuery_execBatch(XSqlQuery* query, XSqlBatchExecutionMode mode);
/**
 * @brief 准备 SQL 查询。
 * @param query 查询对象；不能为 NULL。
 * @param sql SQL 文本；借用，函数会复制。
 * @return 准备成功返回 true，否则返回 false。
 */
bool XSqlQuery_prepare(XSqlQuery* query, const XString* sql);
/**
 * @brief 使用 UTF-8 SQL 准备查询。
 * @param query 查询对象；不能为 NULL。
 * @param sql UTF-8 SQL 字符串；借用。
 * @return 准备成功返回 true，否则返回 false。
 */
bool XSqlQuery_prepare_utf8(XSqlQuery* query, const char* sql);
/**
 * @brief 按 UTF-8 占位符名称绑定参数。
 * @param query 查询对象；不能为 NULL。
 * @param placeholder 占位符名称；借用。
 * @param value 参数值；借用，函数会复制。
 * @param type 参数方向和属性。
 * @return 无。
 */
void XSqlQuery_bindValue_utf8(XSqlQuery* query, const char* placeholder, const XVariant* value, XSqlParamType type);
/**
 * @brief 按 XString 占位符名称绑定参数。
 * @param query 查询对象；不能为 NULL。
 * @param placeholder 占位符名称；借用，可为 NULL。
 * @param value 参数值；借用，函数会复制，可为 NULL 表示 SQL NULL。
 * @param type 参数方向和属性；使用 XSqlParamTypeFlag 组合。
 * @return 无；非法位置或内存不足时查询绑定状态保持可用部分。
 */
void XSqlQuery_bindValue_2(XSqlQuery* query, const XString* placeholder, const XVariant* value, XSqlParamType type);
/**
 * @brief 按位置绑定参数。
 * @param query 查询对象；不能为 NULL。
 * @param position 参数位置，从 0 开始。
 * @param value 参数值；借用，函数会复制。
 * @param type 参数方向和属性。
 * @return 无。
 */
void XSqlQuery_bindValue(XSqlQuery* query, int position, const XVariant* value, XSqlParamType type);
/**
 * @brief 追加绑定参数。
 * @param query 查询对象；不能为 NULL。
 * @param value 参数值；借用，函数会复制。
 * @param type 参数方向和属性。
 * @return 无。
 */
void XSqlQuery_addBindValue(XSqlQuery* query, const XVariant* value, XSqlParamType type);
/**
 * @brief 按 UTF-8 名称获取绑定值副本。
 * @param query 查询对象；NULL 返回空值对象。
 * @param placeholder 占位符名称；借用。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlQuery_boundValue_utf8(const XSqlQuery* query, const char* placeholder);
/**
 * @brief 按 XString 占位符名称获取绑定值副本。
 * @param query 查询对象；可为 NULL。
 * @param placeholder 占位符名称；借用，可为 NULL。
 * @return 新值所有权；调用者使用 XVariant_delete_base 释放，名称不存在返回空值对象。
 */
XVariant* XSqlQuery_boundValue_2(const XSqlQuery* query, const XString* placeholder);
/**
 * @brief 按位置获取绑定值副本。
 * @param query 查询对象；NULL 返回空值对象。
 * @param position 参数位置，从 0 开始。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlQuery_boundValue(const XSqlQuery* query, int position);
/**
 * @brief 获取全部绑定值副本。
 * @param query 查询对象；NULL 返回空列表。
 * @return 新列表，调用者必须使用 XVariantList_delete_base 释放。
 */
XVariantList* XSqlQuery_boundValues(const XSqlQuery* query);
/**
 * @brief 获取绑定名称列表。
 * @param query 查询对象；NULL 返回空列表。
 * @return 新列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlQuery_boundValueNames(const XSqlQuery* query);
/**
 * @brief 按位置获取绑定名称副本。
 * @param query 查询对象；NULL 返回空字符串对象。
 * @param position 参数位置，从 0 开始。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlQuery_boundValueName(const XSqlQuery* query, int position);
/**
 * @brief 获取实际执行 SQL 副本。
 * @param query 查询对象；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlQuery_executedQuery(const XSqlQuery* query);
/**
 * @brief 获取最后插入 ID 副本。
 * @param query 查询对象；NULL 返回空值对象。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlQuery_lastInsertId(const XSqlQuery* query);
/**
 * @brief 结束当前结果集。
 * @param query 查询对象；不能为 NULL。
 * @return 无；结果仍可由对象释放。
 */
void XSqlQuery_finish(XSqlQuery* query);
/**
 * @brief 切换到下一个结果集。
 * @param query 查询对象；不能为 NULL。
 * @return 切换成功返回 true，否则返回 false。
 */
bool XSqlQuery_nextResult(XSqlQuery* query);

#ifdef __cplusplus
}
#endif

#endif /* XSQLQUERY_H */
