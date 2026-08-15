/**
 * @file       XSqlDriver.h
 * @brief      SQL 驱动抽象类，对齐 Qt 6.8 QSqlDriver。
 * @details    该类是所有数据库后端的源码适配边界。具体后端通过继承
 *             XSqlDriver 并重载 vtable 实现连接、事务、SQL 方言和结果集创建。
 */
#ifndef XSQLDRIVER_H
#define XSQLDRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlError.h"
#include "XSqlIndex.h"
#include "XObject.h"
#include "XStringList.h"

typedef struct XSqlResult XSqlResult;

/**
 * @brief XSqlDriver 的虚函数槽位定义。
 * @details 槽位为具体驱动实现连接、方言和结果创建提供 ABI；调用方使用
 *          本文件公开的 *_base 分派函数，不得直接访问槽位。
 */
XCLASS_DEFINE_BEGING(XSqlDriver)
XCLASS_DEFINE_ENUM(XSqlDriver, IsOpen) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XSqlDriver, BeginTransaction),
XCLASS_DEFINE_ENUM(XSqlDriver, CommitTransaction),
XCLASS_DEFINE_ENUM(XSqlDriver, RollbackTransaction),
XCLASS_DEFINE_ENUM(XSqlDriver, Tables),
XCLASS_DEFINE_ENUM(XSqlDriver, PrimaryIndex),
XCLASS_DEFINE_ENUM(XSqlDriver, Record),
XCLASS_DEFINE_ENUM(XSqlDriver, FormatValue),
XCLASS_DEFINE_ENUM(XSqlDriver, EscapeIdentifier),
XCLASS_DEFINE_ENUM(XSqlDriver, SqlStatement),
XCLASS_DEFINE_ENUM(XSqlDriver, Handle),
XCLASS_DEFINE_ENUM(XSqlDriver, HasFeature),
XCLASS_DEFINE_ENUM(XSqlDriver, Close),
XCLASS_DEFINE_ENUM(XSqlDriver, CreateResult),
XCLASS_DEFINE_ENUM(XSqlDriver, Open),
XCLASS_DEFINE_ENUM(XSqlDriver, Subscribe),
XCLASS_DEFINE_ENUM(XSqlDriver, Unsubscribe),
XCLASS_DEFINE_ENUM(XSqlDriver, Subscribed),
XCLASS_DEFINE_ENUM(XSqlDriver, IsIdentifierEscaped),
XCLASS_DEFINE_ENUM(XSqlDriver, StripDelimiters),
XCLASS_DEFINE_ENUM(XSqlDriver, CancelQuery),
XCLASS_DEFINE_ENUM(XSqlDriver, MaximumIdentifierLength),
XCLASS_DEFINE_ENUM(XSqlDriver, SetOpen),
XCLASS_DEFINE_ENUM(XSqlDriver, SetOpenError),
XCLASS_DEFINE_ENUM(XSqlDriver, SetLastError),
XCLASS_DEFINE_END(XSqlDriver)

/**
 * @brief SQL 驱动抽象对象。
 */
typedef struct XSqlDriver {
    XObject m_class;                        /**< 第一个成员，由 XObject 管理；不可复制。 */
    XSqlDriverType m_driverType;            /**< 驱动实现类型。 */
    XSqlDbmsType m_dbmsType;                /**< 实际数据库类型。 */
    XSqlError m_lastError;                  /**< 最近错误。 */
    XSqlNumericalPrecisionPolicy m_precisionPolicy; /**< 数值精度策略。 */
    bool m_open;                            /**< 当前是否已打开。 */
    bool m_openError;                       /**< 最近一次打开是否失败。 */
} XSqlDriver;

/**
 * @brief 初始化 XSqlDriver 虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlDriver_class_init(void);
/**
 * @brief 初始化抽象驱动。
 * @param driver 待初始化对象；不能为 NULL。
 * @param driverType 驱动实现类型。
 * @param dbmsType 实际数据库类型。
 * @return 无；对象初始化失败时保持未初始化状态。
 * @note XSqlDriver 派生对象不可复制，具体后端应通过 XSqlDriver 的 vtable 实现。
 */
void XSqlDriver_init(XSqlDriver* driver, XSqlDriverType driverType, XSqlDbmsType dbmsType);
/**
 * @brief 创建抽象驱动对象。
 * @param driverType 驱动实现类型。
 * @param dbmsType 实际数据库类型。
 * @return 新驱动对象，调用者必须使用 XSqlDriver_delete_base 释放；失败返回 NULL。
 */
XSqlDriver* XSqlDriver_create_ex(XMemoryType memory,  XSqlDriverType driverType, XSqlDbmsType dbmsType);

/** @brief 调用 XClass 析构入口释放驱动对象及其最近错误。 */
#define XSqlDriver_deinit_base XClass_deinit_base
/** @brief 释放由具体驱动创建函数返回的驱动对象。 */
#define XSqlDriver_delete_base XClass_delete_base

/**
 * @brief 获取驱动实现类型。
 * @param driver 驱动对象；NULL 返回 Unknown。
 * @return 驱动实现类型。
 */
XSqlDriverType XSqlDriver_driverType(const XSqlDriver* driver);
/**
 * @brief 获取实际数据库类型。
 * @param driver 驱动对象；NULL 返回 Unknown。
 * @return 数据库类型。
 */
XSqlDbmsType XSqlDriver_dbmsType(const XSqlDriver* driver);
/**
 * @brief 判断驱动是否已打开。
 * @param driver 驱动对象；NULL 返回 false。
 * @return 已打开返回 true，否则返回 false。
 */
bool XSqlDriver_isOpen(const XSqlDriver* driver);
/**
 * @brief 判断最近一次打开是否失败。
 * @param driver 驱动对象；NULL 返回 true。
 * @return 打开失败返回 true，否则返回 false。
 */
bool XSqlDriver_isOpenError(const XSqlDriver* driver);
/**
 * @brief 获取最近一次错误的副本。
 * @param driver 驱动对象；NULL 返回未知错误对象。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放。
 */
XSqlError* XSqlDriver_lastError(const XSqlDriver* driver);
/**
 * @brief 开始事务。
 * @param driver 驱动对象；不能为 NULL。
 * @return 成功返回 true；驱动不支持或执行失败返回 false。
 */
bool XSqlDriver_beginTransaction_base(XSqlDriver* driver);
/**
 * @brief 提交事务。
 * @param driver 驱动对象；不能为 NULL。
 * @return 成功返回 true；驱动不支持或执行失败返回 false。
 */
bool XSqlDriver_commitTransaction_base(XSqlDriver* driver);
/**
 * @brief 回滚事务。
 * @param driver 驱动对象；不能为 NULL。
 * @return 成功返回 true；驱动不支持或执行失败返回 false。
 */
bool XSqlDriver_rollbackTransaction_base(XSqlDriver* driver);
/**
 * @brief 获取数据库表名列表。
 * @param driver 驱动对象；不能为 NULL。
 * @param type 表类型过滤标志，可按位组合。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlDriver_tables_base(const XSqlDriver* driver, XSqlTableType type);
/**
 * @brief 获取表的主键索引。
 * @param driver 驱动对象；不能为 NULL。
 * @param tableName 表名；借用，函数调用期间有效。
 * @return 新索引对象，调用者必须使用 XSqlIndex_delete_base 释放。
 */
XSqlIndex* XSqlDriver_primaryIndex_base(const XSqlDriver* driver, const XString* tableName);
/**
 * @brief 获取表字段记录描述。
 * @param driver 驱动对象；不能为 NULL。
 * @param tableName 表名；借用，函数调用期间有效。
 * @return 新记录对象，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlDriver_record_base(const XSqlDriver* driver, const XString* tableName);
/**
 * @brief 将字段值格式化为 SQL 文本。
 * @param driver 驱动对象；不能为 NULL。
 * @param field 字段描述；借用，可为 NULL。
 * @param trimStrings 是否去除字符串首尾空白。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlDriver_formatValue_base(const XSqlDriver* driver, const XSqlField* field, bool trimStrings);
/**
 * @brief 转义 SQL 标识符。
 * @param driver 驱动对象；不能为 NULL。
 * @param identifier 待转义标识符；借用。
 * @param type 标识符类型。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlDriver_escapeIdentifier_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
/**
 * @brief 生成驱动相关的 SQL 语句。
 * @param driver 驱动对象；不能为 NULL。
 * @param type 语句类型。
 * @param tableName 目标表名；借用。
 * @param record 字段记录；借用，可为 NULL。
 * @param preparedStatement 是否生成预处理占位符。
 * @return 新 SQL 字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlDriver_sqlStatement_base(const XSqlDriver* driver, XSqlStatementType type,
                                      const XString* tableName, const XSqlRecord* record, bool preparedStatement);
/**
 * @brief 获取后端原生句柄。
 * @param driver 驱动对象；不能为 NULL。
 * @return 后端借用句柄；公共层不释放，未实现时返回 NULL。
 */
void* XSqlDriver_handle_base(const XSqlDriver* driver);
/**
 * @brief 查询驱动能力。
 * @param driver 驱动对象；不能为 NULL。
 * @param feature 能力标志。
 * @return 支持该能力返回 true，否则返回 false。
 */
bool XSqlDriver_hasFeature_base(const XSqlDriver* driver, XSqlDriverFeature feature);
/**
 * @brief 关闭数据库连接。
 * @param driver 驱动对象；不能为 NULL。
 * @return 无；失败状态由 XSqlDriver_lastError 获取。
 */
void XSqlDriver_close_base(XSqlDriver* driver);
/**
 * @brief 创建结果抽象对象。
 * @param driver 驱动对象；不能为 NULL。
 * @return 新结果对象，调用者必须使用 XSqlResult_delete_base 释放；失败返回 NULL。
 */
XSqlResult* XSqlDriver_createResult_base(const XSqlDriver* driver);
/**
 * @brief 打开数据库连接。
 * @param driver 驱动对象；不能为 NULL。
 * @param database 数据库名称；借用，可为空。
 * @param user 用户名；借用，可为空。
 * @param password 密码；借用，可为空。
 * @param host 主机名；借用，可为空。
 * @param port 端口，未知时使用 -1。
 * @param options 连接选项；借用，可为空。
 * @return 打开成功返回 true，否则返回 false。
 */
bool XSqlDriver_open_base(XSqlDriver* driver, const XString* database, const XString* user,
                          const XString* password, const XString* host, int port, const XString* options);
/**
 * @brief 订阅数据库通知。
 * @param driver 驱动对象；不能为 NULL。
 * @param name 通知名称；借用。
 * @return 订阅成功返回 true，否则返回 false。
 */
bool XSqlDriver_subscribeToNotification_base(XSqlDriver* driver, const XString* name);
/**
 * @brief 取消数据库通知订阅。
 * @param driver 驱动对象；不能为 NULL。
 * @param name 通知名称；借用。
 * @return 取消成功返回 true，否则返回 false。
 */
bool XSqlDriver_unsubscribeFromNotification_base(XSqlDriver* driver, const XString* name);
/**
 * @brief 获取当前通知订阅名称。
 * @param driver 驱动对象；不能为 NULL。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlDriver_subscribedToNotifications_base(const XSqlDriver* driver);
/**
 * @brief 判断标识符是否已经转义。
 * @param driver 驱动对象；不能为 NULL。
 * @param identifier 标识符；借用。
 * @param type 标识符类型。
 * @return 已转义返回 true，否则返回 false。
 */
bool XSqlDriver_isIdentifierEscaped_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
/**
 * @brief 移除标识符转义符。
 * @param driver 驱动对象；不能为 NULL。
 * @param identifier 标识符；借用。
 * @param type 标识符类型。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlDriver_stripDelimiters_base(const XSqlDriver* driver, const XString* identifier, XSqlIdentifierType type);
/**
 * @brief 设置数值精度策略。
 * @param driver 驱动对象；不能为 NULL。
 * @param policy 数值精度策略。
 * @return 无；该函数对齐 Qt QSqlDriver::setNumericalPrecisionPolicy，非虚函数。
 */
void XSqlDriver_setNumericalPrecisionPolicy(XSqlDriver* driver, XSqlNumericalPrecisionPolicy policy);
/**
 * @brief 获取数值精度策略。
 * @param driver 驱动对象；NULL 返回 HighPrecision。
 * @return 当前数值精度策略。
 */
XSqlNumericalPrecisionPolicy XSqlDriver_numericalPrecisionPolicy(const XSqlDriver* driver);
/**
 * @brief 取消当前查询。
 * @param driver 驱动对象；不能为 NULL。
 * @return 取消成功返回 true，否则返回 false。
 */
bool XSqlDriver_cancelQuery_base(XSqlDriver* driver);
/**
 * @brief 获取标识符最大长度。
 * @param driver 驱动对象；不能为 NULL。
 * @param type 标识符类型。
 * @return 最大长度；未知时返回 -1。
 */
int XSqlDriver_maximumIdentifierLength_base(const XSqlDriver* driver, XSqlIdentifierType type);
/**
 * @brief 设置驱动打开状态。
 * @param driver 驱动对象；不能为 NULL。
 * @param open true 表示已打开，false 表示已关闭。
 * @return 无；该函数对应 Qt 的保护虚函数。
 */
void XSqlDriver_setOpen(XSqlDriver* driver, bool open);
/**
 * @brief 设置打开错误状态。
 * @param driver 驱动对象；不能为 NULL。
 * @param error true 表示最近一次打开失败。
 * @return 无；该函数对应 Qt 的保护虚函数。
 */
void XSqlDriver_setOpenError(XSqlDriver* driver, bool error);
/**
 * @brief 设置最近一次驱动错误。
 * @param driver 驱动对象；不能为 NULL。
 * @param error 错误对象；借用，函数会复制内容。
 * @return 无；error 为 NULL 时保持原错误不变。
 */
void XSqlDriver_setLastError(XSqlDriver* driver, const XSqlError* error);

/**
 * @brief 发射数据库通知信号。
 * @param driver 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param name 通知名称；信号调用期间借用，不转移所有权。
 * @param source 通知来源。
 * @param payload 通知负载；信号调用期间借用，可为 NULL。
 * @return 信号句柄。
 */
void* XSqlDriver_notification_signal(XSqlDriver* driver, const XString* name,
                                     XSqlNotificationSource source,
                                     const XVariant* payload);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlDriver_create
#define XSqlDriver_create(...) XSqlDriver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XSQLDRIVER_H */
