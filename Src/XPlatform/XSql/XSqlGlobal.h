/**
 * @file       XSqlGlobal.h
 * @brief      XinYueC SQL 公共枚举和数据库类型定义。
 * @details    对齐 Qt 6.8 QtSql 的公共枚举；本文件只依赖 XinYueC 抽象层，
 *             不调用操作系统、数据库或动态插件 API。
 */
#ifndef XSQLGLOBAL_H
#define XSQLGLOBAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "XVariant.h"

/**
 * @brief 查询游标的特殊位置。
 */
typedef enum XSqlLocation {
    XSqlLocation_BeforeFirstRow = -1, /**< 游标位于第一行之前。 */
    XSqlLocation_AfterLastRow = -2    /**< 游标位于最后一行之后。 */
} XSqlLocation;

/**
 * @brief 绑定参数方向和属性。
 */
typedef enum XSqlParamTypeFlag {
    XSqlParamType_In = 0x00000001u,     /**< 输入参数。 */
    XSqlParamType_Out = 0x00000002u,    /**< 输出参数。 */
    XSqlParamType_InOut = 0x00000003u,  /**< 输入输出参数。 */
    XSqlParamType_Binary = 0x00000004u  /**< 二进制参数。 */
} XSqlParamTypeFlag;
typedef uint32_t XSqlParamType;

/**
 * @brief 数据库表类型过滤标志。
 */
typedef enum XSqlTableType {
    XSqlTableType_Tables = 0x01u,       /**< 普通表。 */
    XSqlTableType_SystemTables = 0x02u, /**< 系统表。 */
    XSqlTableType_Views = 0x04u,        /**< 视图。 */
    XSqlTableType_AllTables = 0xffu     /**< 所有表类型。 */
} XSqlTableType;

/**
 * @brief 数值精度策略。
 */
typedef enum XSqlNumericalPrecisionPolicy {
    XSqlNumericalPrecisionPolicy_LowPrecisionInt32 = 0x01,  /**< 使用 32 位整数。 */
    XSqlNumericalPrecisionPolicy_LowPrecisionInt64 = 0x02,  /**< 使用 64 位整数。 */
    XSqlNumericalPrecisionPolicy_LowPrecisionDouble = 0x04,/**< 使用 double。 */
    XSqlNumericalPrecisionPolicy_HighPrecision = 0          /**< 保留高精度表示。 */
} XSqlNumericalPrecisionPolicy;

/**
 * @brief 数据库驱动类型。
 * @details 该枚举描述驱动实现，不等同于实际数据库服务器类型；例如 ODBC
 *          驱动可以连接 PostgreSQL、MySQL 或 SQL Server。
 */
typedef enum XSqlDriverType {
    XSqlDriverType_Unknown = 0,      /**< 未知驱动。 */
    XSqlDriverType_MsSqlServer,      /**< SQL Server 专用驱动。 */
    XSqlDriverType_MySql,            /**< MySQL/MariaDB 驱动。 */
    XSqlDriverType_PostgreSql,       /**< PostgreSQL 驱动。 */
    XSqlDriverType_Oracle,           /**< Oracle OCI 驱动。 */
    XSqlDriverType_Sybase,           /**< Sybase 驱动。 */
    XSqlDriverType_Sqlite,           /**< SQLite 驱动。 */
    XSqlDriverType_Interbase,        /**< InterBase/Firebird 驱动。 */
    XSqlDriverType_Db2,              /**< IBM DB2 驱动。 */
    XSqlDriverType_MimerSql,         /**< Mimer SQL 驱动。 */
    XSqlDriverType_Odbc,             /**< ODBC 通用驱动。 */
    XSqlDriverType_Embedded,         /**< 嵌入式数据库驱动。 */
    XSqlDriverType_Custom            /**< 用户自定义驱动。 */
} XSqlDriverType;

/**
 * @brief 数据库实际类型。
 */
typedef enum XSqlDbmsType {
    XSqlDbmsType_Unknown = 0,      /**< 未知数据库。 */
    XSqlDbmsType_MsSqlServer,      /**< Microsoft SQL Server。 */
    XSqlDbmsType_MySql,            /**< MySQL/MariaDB。 */
    XSqlDbmsType_PostgreSql,       /**< PostgreSQL。 */
    XSqlDbmsType_Oracle,           /**< Oracle。 */
    XSqlDbmsType_Sybase,           /**< Sybase。 */
    XSqlDbmsType_Sqlite,           /**< SQLite。 */
    XSqlDbmsType_Interbase,        /**< InterBase/Firebird。 */
    XSqlDbmsType_Db2,              /**< IBM DB2。 */
    XSqlDbmsType_MimerSql          /**< Mimer SQL。 */
} XSqlDbmsType;

/**
 * @brief 驱动能力位标志。
 */
typedef enum XSqlDriverFeature {
    XSqlDriverFeature_Transactions = 1u << 0,       /**< 支持事务。 */
    XSqlDriverFeature_QuerySize = 1u << 1,           /**< 能获取结果行数。 */
    XSqlDriverFeature_Blob = 1u << 2,                /**< 支持 BLOB。 */
    XSqlDriverFeature_Unicode = 1u << 3,             /**< 支持 Unicode。 */
    XSqlDriverFeature_PreparedQueries = 1u << 4,     /**< 支持预处理查询。 */
    XSqlDriverFeature_NamedPlaceholders = 1u << 5,   /**< 支持命名占位符。 */
    XSqlDriverFeature_PositionalPlaceholders = 1u << 6, /**< 支持位置占位符。 */
    XSqlDriverFeature_LastInsertId = 1u << 7,        /**< 支持最后插入 ID。 */
    XSqlDriverFeature_BatchOperations = 1u << 8,     /**< 支持批量操作。 */
    XSqlDriverFeature_SimpleLocking = 1u << 9,       /**< 支持简单锁。 */
    XSqlDriverFeature_LowPrecisionNumbers = 1u << 10,/**< 支持低精度数值策略。 */
    XSqlDriverFeature_EventNotifications = 1u << 11, /**< 支持事件通知。 */
    XSqlDriverFeature_FinishQuery = 1u << 12,        /**< 支持结束查询。 */
    XSqlDriverFeature_MultipleResultSets = 1u << 13, /**< 支持多结果集。 */
    XSqlDriverFeature_CancelQuery = 1u << 14        /**< 支持取消查询。 */
} XSqlDriverFeature;

/**
 * @brief 驱动生成 SQL 时使用的语句类型。
 */
typedef enum XSqlStatementType {
    XSqlStatementType_WhereStatement = 0, /**< WHERE 语句。 */
    XSqlStatementType_SelectStatement,    /**< SELECT 语句。 */
    XSqlStatementType_UpdateStatement,    /**< UPDATE 语句。 */
    XSqlStatementType_InsertStatement,    /**< INSERT 语句。 */
    XSqlStatementType_DeleteStatement     /**< DELETE 语句。 */
} XSqlStatementType;

/** @brief SQL 标识符类别。 */
typedef enum XSqlIdentifierType {
    XSqlIdentifierType_FieldName = 0, /**< 字段名。 */
    XSqlIdentifierType_TableName      /**< 表名。 */
} XSqlIdentifierType;

/** @brief 数据库通知来源。 */
typedef enum XSqlNotificationSource {
    XSqlNotificationSource_Unknown = 0, /**< 未知来源。 */
    XSqlNotificationSource_Self,        /**< 当前连接。 */
    XSqlNotificationSource_Other        /**< 其他连接。 */
} XSqlNotificationSource;

/** @brief 批量执行模式。 */
typedef enum XSqlBatchExecutionMode {
    XSqlBatchExecutionMode_ValuesAsRows = 0, /**< 每个绑定值集合作为一行。 */
    XSqlBatchExecutionMode_ValuesAsColumns  /**< 每个绑定值集合按列展开。 */
} XSqlBatchExecutionMode;

/** @brief SQL 错误类别。 */
typedef enum XSqlErrorType {
    XSqlErrorType_NoError = 0,         /**< 无错误。 */
    XSqlErrorType_ConnectionError,     /**< 连接错误。 */
    XSqlErrorType_BeginTransactionError, /**< 开始事务错误。 */
    XSqlErrorType_CommitTransactionError, /**< 提交事务错误。 */
    XSqlErrorType_RollbackTransactionError, /**< 回滚事务错误。 */
    XSqlErrorType_StatementError,      /**< 语句错误。 */
    XSqlErrorType_TransactionError,    /**< 事务错误。 */
    XSqlErrorType_UnknownError         /**< 未知错误。 */
} XSqlErrorType;

/** @brief 字段必需状态。 */
typedef enum XSqlFieldRequiredStatus {
    XSqlFieldRequiredStatus_Unknown = -1, /**< 未知。 */
    XSqlFieldRequiredStatus_Optional = 0, /**< 可选。 */
    XSqlFieldRequiredStatus_Required     /**< 必需。 */
} XSqlFieldRequiredStatus;

/** @brief 表模型编辑策略。 */
typedef enum XSqlTableEditStrategy {
    XSqlTableEditStrategy_OnFieldChange = 0, /**< 字段变化时提交。 */
    XSqlTableEditStrategy_OnRowChange,      /**< 行变化时提交。 */
    XSqlTableEditStrategy_OnManualSubmit    /**< 手动提交。 */
} XSqlTableEditStrategy;

/** @brief 关系表模型连接模式。 */
typedef enum XSqlRelationJoinMode {
    XSqlRelationJoinMode_InnerJoin = 0, /**< 内连接。 */
    XSqlRelationJoinMode_LeftJoin       /**< 左连接。 */
} XSqlRelationJoinMode;

/** @brief 模型数据角色。 */
typedef enum XSqlItemDataRole {
    XSqlItemDataRole_Display = 0, /**< 显示值。 */
    XSqlItemDataRole_Edit = 2,    /**< 编辑值。 */
    XSqlItemDataRole_User = 256   /**< 用户自定义角色起点。 */
} XSqlItemDataRole;

/** @brief 模型方向。 */
typedef enum XSqlOrientation {
    XSqlOrientation_Horizontal = 0, /**< 水平方向。 */
    XSqlOrientation_Vertical         /**< 垂直方向。 */
} XSqlOrientation;

/** @brief 排序方向。 */
typedef enum XSqlSortOrder {
    XSqlSortOrder_Ascending = 0, /**< 升序。 */
    XSqlSortOrder_Descending     /**< 降序。 */
} XSqlSortOrder;

/** @brief 模型项属性位。 */
typedef uint32_t XSqlItemFlags;
typedef enum XSqlItemFlag {
    XSqlItemFlag_NoItemFlags = 0,                 /**< 不具备任何属性。 */
    XSqlItemFlag_ItemIsSelectable = 1u << 0,      /**< 项可被选择。 */
    XSqlItemFlag_ItemIsEditable = 1u << 1,         /**< 项可被编辑。 */
    XSqlItemFlag_ItemIsDragEnabled = 1u << 2,      /**< 项可被拖动。 */
    XSqlItemFlag_ItemIsDropEnabled = 1u << 3,      /**< 项可接受放置。 */
    XSqlItemFlag_ItemIsUserCheckable = 1u << 4,    /**< 项支持用户勾选。 */
    XSqlItemFlag_ItemIsEnabled = 1u << 5,          /**< 项已启用。 */
    XSqlItemFlag_ItemIsAutoTristate = 1u << 6,     /**< 项支持自动三态。 */
    XSqlItemFlag_ItemNeverHasChildren = 1u << 7,   /**< 项不会拥有子项。 */
    XSqlItemFlag_ItemIsUserTristate = 1u << 8      /**< 项支持用户三态。 */
} XSqlItemFlag;

/**
 * @brief 返回驱动类型名称。
 * @param type 驱动类型。
 * @return 库静态持有的 UTF-8 名称；未知类型返回 Unknown，不得释放。
 */
const char* XSqlDriverType_name(XSqlDriverType type);
/**
 * @brief 将 UTF-8 驱动名称转换为驱动类型。
 * @param name UTF-8 驱动名称；借用，可为 NULL。
 * @return 匹配的驱动类型；无法匹配返回 Unknown。
 */
XSqlDriverType XSqlDriverType_fromName_utf8(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* XSQLGLOBAL_H */
