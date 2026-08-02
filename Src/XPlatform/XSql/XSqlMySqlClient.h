/**
 * @file       XSqlMySqlClient.h
 * @brief      MySQL 客户端源码适配接口。
 * @details    本文件只定义 MySQL 协议客户端与 XSql 驱动之间的抽象边界，
 *             不包含 mysql.h、MariaDB 头文件或任何平台 API。默认实现位于
 *             Src/XCode/XSql，并通过 XinYueC 的网络和内存接口工作。
 */
#ifndef XSQLMYSQLCLIENT_H
#define XSQLMYSQLCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XSqlGlobal.h"

/** @brief MySQL 客户端连接的不透明实现类型；只能由客户端函数表创建和销毁。 */
typedef struct XSqlMySqlClient XSqlMySqlClient;
/** @brief MySQL 查询结果的不透明实现类型；只能由客户端函数表销毁。 */
typedef struct XSqlMySqlResult XSqlMySqlResult;

/**
 * @brief MySQL 字段和值的通用类型。
 */
typedef enum XSqlMySqlValueType {
    XSqlMySqlValueType_Unknown = 0,      /**< 未知类型。 */
    XSqlMySqlValueType_Null,             /**< NULL。 */
    XSqlMySqlValueType_Integer,          /**< 有符号整数。 */
    XSqlMySqlValueType_UnsignedInteger, /**< 无符号整数。 */
    XSqlMySqlValueType_Real,             /**< 浮点数或定点数。 */
    XSqlMySqlValueType_String,           /**< 文本字符串。 */
    XSqlMySqlValueType_ByteArray,        /**< 二进制数据。 */
    XSqlMySqlValueType_Date,             /**< 日期值。 */
    XSqlMySqlValueType_Time,             /**< 时间值。 */
    XSqlMySqlValueType_DateTime          /**< 日期时间值。 */
} XSqlMySqlValueType;

/**
 * @brief MySQL 二进制预处理参数的无所有权视图。
 * @note m_data 在 executePrepared 返回前必须保持有效；客户端不会保存该指针。
 */
typedef struct XSqlMySqlBind {
    XSqlMySqlValueType m_type; /**< 参数值类型。 */
    const void* m_data;        /**< 参数数据借用指针；非 NULL 值必须在调用结束前有效。 */
    size_t m_size;             /**< 参数数据字节数；文本不要求包含末尾 NUL。 */
    bool m_isNull;             /**< 为 true 时忽略 m_data 和 m_size，并绑定 SQL NULL。 */
    bool m_unsigned;           /**< 整数参数是否按无符号值编码。 */
} XSqlMySqlBind;

/**
 * @brief MySQL 字段元数据视图。
 * @note 所有字符串均由结果对象借用，结果对象销毁后失效。
 */
typedef struct XSqlMySqlField {
    const char* m_name;          /**< 字段名称，UTF-8。 */
    const char* m_table;         /**< 所属表名称，UTF-8。 */
    const char* m_database;      /**< 所属数据库名称，UTF-8。 */
    XSqlMySqlValueType m_type;   /**< 字段类型。 */
    uint32_t m_length;           /**< 服务端声明的字段长度。 */
    uint32_t m_flags;            /**< MySQL 字段标志。 */
    uint8_t m_nativeType;        /**< MySQL 原生字段类型代码。 */
    uint8_t m_decimals;          /**< 定点或浮点字段的小数位数。 */
    bool m_unsigned;             /**< 是否为无符号数。 */
} XSqlMySqlField;

/**
 * @brief MySQL 结果值视图。
 * @note m_data 由结果对象持有，调用者只能在当前结果对象有效期间读取。
 */
typedef struct XSqlMySqlValue {
    const void* m_data;          /**< 原始值数据，不保证以 NULL 结尾。 */
    size_t m_size;               /**< 原始值长度。 */
    XSqlMySqlValueType m_type;   /**< 值类型。 */
    bool m_isNull;               /**< 是否为 NULL。 */
} XSqlMySqlValue;

/**
 * @brief MySQL 客户端错误视图。
 * @note 字符串由客户端对象持有，下一次操作或对象销毁后失效。
 */
typedef struct XSqlMySqlError {
    const char* m_driverText;    /**< 驱动错误文本。 */
    const char* m_databaseText;  /**< 服务端错误文本。 */
    const char* m_errorCode;     /**< 服务端错误码文本。 */
    XSqlErrorType m_type;        /**< XinYueC SQL 错误类型。 */
} XSqlMySqlError;

/**
 * @brief 可替换的 MySQL 客户端函数表。
 * @details XMySqlDriver 只依赖本函数表。移植到嵌入式时可以保留默认的
 *          XinYueC 协议实现，也可以提供基于厂商客户端源码的另一份函数表。
 */
typedef struct XSqlMySqlClientApi {
    /** @brief 创建未连接的 MySQL 客户端。 @return 新客户端所有权；失败返回 NULL。 */
    XSqlMySqlClient* (*create)(void);
    /** @brief 销毁客户端及其连接。 @param client 客户端所有权；可为 NULL。 */
    void (*destroy)(XSqlMySqlClient* client);
    /**
     * @brief 打开 MySQL 连接。
     * @param client 客户端；不能为 NULL，由调用方借用。
     * @param database 默认数据库名；UTF-8 借用字符串，可为 NULL。
     * @param user 用户名；UTF-8 借用字符串，可为 NULL。
     * @param password 密码；UTF-8 借用字符串，可为 NULL，不由客户端保存。
     * @param host 主机、Unix 套接字或本地传输端点；UTF-8 借用字符串，可为 NULL。
     * @param port TCP 端口；小于 0 时由客户端使用默认值。
     * @param options 分号分隔的连接选项；UTF-8 借用字符串，可为 NULL。
     * @return 成功返回 true；失败时客户端记录可由 lastError 获取的错误。
     */
    bool (*open)(XSqlMySqlClient* client, const char* database, const char* user,
                 const char* password, const char* host, int port, const char* options);
    /**
     * @brief 关闭当前连接。
     * @param client 客户端；可为 NULL，由调用方借用。
     * @return 无；关闭后已有结果由结果对象继续管理，客户端不再执行 SQL。
     */
    void (*close)(XSqlMySqlClient* client);
    /**
     * @brief 执行 UTF-8 SQL 文本。
     * @param client 已连接客户端；不能为 NULL，由调用方借用。
     * @param query SQL 字节序列；借用，长度由 length 指定，不要求 NUL 结尾。
     * @param length query 的字节数。
     * @param result 输出结果；成功时写入新结果所有权，无结果集时仍可为 NULL。
     * @return 服务端接受并完成请求返回 true；失败时 result 必须保持 NULL。
     */
    bool (*execute)(XSqlMySqlClient* client, const char* query, size_t length,
                    XSqlMySqlResult** result);
    /**
     * @brief 使用服务端二进制预处理协议执行 SQL。
     * @param client 已连接客户端；不能为 NULL，由调用方借用。
     * @param query SQL 字节序列；借用，长度由 length 指定。
     * @param length query 的字节数。
     * @param binds 参数数组；借用，bindCount 为 0 时可为 NULL。
     * @param bindCount binds 中的参数个数。
     * @param result 输出结果；成功时写入新结果所有权，无结果集时仍可为 NULL。
     * @return 成功返回 true；未支持或执行失败返回 false。
     */
    bool (*executePrepared)(XSqlMySqlClient* client, const char* query, size_t length,
                            const XSqlMySqlBind* binds, size_t bindCount,
                            XSqlMySqlResult** result);
    /** @brief 销毁查询结果。 @param result 结果所有权；可为 NULL。 */
    void (*resultDestroy)(XSqlMySqlResult* result);
    /** @brief 返回结果列数。 @param result 结果；可为 NULL。 @return 列数；无结果或 NULL 返回 0。 */
    int (*resultColumnCount)(const XSqlMySqlResult* result);
    /**
     * @brief 获取字段元数据借用视图。
     * @param result 结果；不能为 NULL。
     * @param index 列索引，从 0 开始。
     * @return 字段借用视图；越界返回 NULL。
     */
    const XSqlMySqlField* (*resultField)(const XSqlMySqlResult* result, int index);
    /**
     * @brief 定位结果行。
     * @param result 结果；不能为 NULL。
     * @param index 行索引，从 0 开始。
     * @return 定位成功返回 true；越界或失败返回 false。
     */
    bool (*resultFetch)(XSqlMySqlResult* result, int index);
    /**
     * @brief 获取当前行列值借用视图。
     * @param result 已定位结果；不能为 NULL。
     * @param index 列索引，从 0 开始。
     * @return 值借用视图；无当前行或越界返回 NULL。
     */
    const XSqlMySqlValue* (*resultValue)(const XSqlMySqlResult* result, int index);
    /** @brief 返回可知的结果行数。 @param result 结果；可为 NULL。 @return 行数；未知时返回 -1。 */
    int (*resultSize)(const XSqlMySqlResult* result);
    /** @brief 返回最近语句的受影响行数。 @param result 结果；可为 NULL。 @return 受影响行数；未知时返回 -1。 */
    int64_t (*resultRowsAffected)(const XSqlMySqlResult* result);
    /** @brief 返回最近插入的自增 ID。 @param result 结果；可为 NULL。 @return 自增 ID；不可用时返回 0。 */
    uint64_t (*resultLastInsertId)(const XSqlMySqlResult* result);
    /** @brief 判断是否为有列的 SELECT 结果。 @param result 结果；可为 NULL。 @return 是 SELECT 返回 true，否则返回 false。 */
    bool (*resultIsSelect)(const XSqlMySqlResult* result);
    /**
     * @brief 切换到下一个服务端结果。
     * @param result 当前结果；不能为 NULL。
     * @param next 输出下一个结果所有权；无下一个结果时保持 NULL。
     * @return 已切换返回 true，否则返回 false。
     */
    bool (*resultNext)(XSqlMySqlResult* result, XSqlMySqlResult** next);
    /**
     * @brief 获取最近客户端错误借用视图。
     * @param client 客户端；可为 NULL。
     * @return 错误借用视图；无错误信息时返回 NULL。
     */
    const XSqlMySqlError* (*lastError)(const XSqlMySqlClient* client);
    /**
     * @brief 获取后端原生句柄。
     * @param client 客户端；可为 NULL。
     * @return 借用句柄；调用方不得释放，未实现时返回 NULL。
     */
    void* (*handle)(const XSqlMySqlClient* client);
    /**
     * @brief 请求取消当前查询。
     * @param client 客户端；不能为 NULL。
     * @return 已取消返回 true；未实现或无法取消返回 false。
     */
    bool (*cancel)(XSqlMySqlClient* client);
    /**
     * @brief 查询当前连接是否支持事务。
     * @param client 客户端对象；由驱动借用。
     * @return 支持返回 true；不支持返回 false。
     * @note 可选回调；为 NULL 时驱动按连接已打开处理，以兼容旧适配器。
     */
    bool (*supportsTransactions)(const XSqlMySqlClient* client);
    /**
     * @brief 查询当前连接是否支持服务端预处理。
     * @param client 客户端对象；由驱动借用。
     * @return 支持返回 true；不支持返回 false。
     * @note 可选回调；为 NULL 时驱动按连接已打开处理，以兼容旧适配器。
     */
    bool (*supportsPreparedQueries)(const XSqlMySqlClient* client);
} XSqlMySqlClientApi;

/**
 * @brief 获取默认 MySQL 客户端实现。
 * @return 进程内共享函数表；不得释放。没有可用实现时返回 NULL。
 */
const XSqlMySqlClientApi* XSqlMySqlClient_defaultApi(void);

#ifdef __cplusplus
}
#endif

#endif /* XSQLMYSQLCLIENT_H */
