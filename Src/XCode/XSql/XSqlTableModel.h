/**
 * @file       XSqlTableModel.h
 * @brief      SQL 表模型，对齐 Qt 6.8 QSqlTableModel。
 * @details    模型保存可编辑的记录缓存，通过 XSqlDatabase 向后端提交修改；
 *             它不拥有原始数据库连接，创建时仅复制连接句柄。
 */
#ifndef XSQLTABLEMODEL_H
#define XSQLTABLEMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlQueryModel.h"

XCLASS_DEFINE_BEGING(XSqlTableModel)
XCLASS_DEFINE_ENUM(XSqlTableModel, SetTable) = XCLASS_VTABLE_GET_SIZE(XSqlQueryModel),
XCLASS_DEFINE_ENUM(XSqlTableModel, SetEditStrategy),
XCLASS_DEFINE_ENUM(XSqlTableModel, SetSort),
XCLASS_DEFINE_ENUM(XSqlTableModel, SetFilter),
XCLASS_DEFINE_ENUM(XSqlTableModel, RevertRow),
XCLASS_DEFINE_ENUM(XSqlTableModel, Select),
XCLASS_DEFINE_ENUM(XSqlTableModel, SelectRow),
XCLASS_DEFINE_ENUM(XSqlTableModel, UpdateRowInTable),
XCLASS_DEFINE_ENUM(XSqlTableModel, InsertRowIntoTable),
XCLASS_DEFINE_ENUM(XSqlTableModel, DeleteRowFromTable),
XCLASS_DEFINE_ENUM(XSqlTableModel, OrderByClause),
XCLASS_DEFINE_ENUM(XSqlTableModel, SelectStatement),
XCLASS_DEFINE_END(XSqlTableModel)

/** @brief SQL 表模型。 */
typedef struct XSqlTableModel {
    XSqlQueryModel m_parent;       /**< 查询模型基类。 */
    XSqlDatabase* m_database;      /**< 数据库连接副本，由模型拥有。 */
    XString* m_tableName;          /**< 表名，由模型拥有。 */
    XString* m_filter;             /**< 过滤条件，由模型拥有。 */
    XSqlIndex m_primaryKey;        /**< 主键描述。 */
    XSqlTableEditStrategy m_strategy; /**< 编辑策略。 */
    int m_sortColumn;              /**< 排序列。 */
    XSqlSortOrder m_sortOrder;     /**< 排序方向。 */
    bool* m_dirty;                 /**< 行脏标记，由模型拥有。 */
    bool* m_inserted;              /**< 行是否尚未插入数据库，由模型拥有。 */
    bool* m_deleted;               /**< 行是否已标记删除但尚未提交，由模型拥有。 */
    XSqlRecord** m_originalRows;   /**< 修改前行快照，用于生成更新条件，由模型拥有。 */
    size_t m_dirtyCapacity;        /**< 脏标记容量。 */
    XSqlRecord** m_removedRows;    /**< 已从模型移除、等待提交删除的原始行，由模型拥有。 */
    size_t m_removedCount;         /**< 等待删除的行数。 */
    size_t m_removedCapacity;      /**< 等待删除数组容量。 */
} XSqlTableModel;

/**
 * @brief 初始化表模型虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlTableModel_class_init(void);
/**
 * @brief 初始化表模型。
 * @param model 待初始化模型；不能为 NULL。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 无；模型继承 XObject，不支持复制。
 */
void XSqlTableModel_init(XSqlTableModel* model, const XSqlDatabase* database);
/**
 * @brief 创建表模型。
 * @param database 数据库连接；创建时复制连接句柄，可为 NULL。
 * @return 新模型，调用者必须使用 XSqlTableModel_delete_base 释放；失败返回 NULL。
 */
XSqlTableModel* XSqlTableModel_create_ex(XMemoryType memory,  const XSqlDatabase* database);

/** @brief 调用 XClass 析构入口释放表模型的行缓存和连接句柄。 */
#define XSqlTableModel_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlTableModel_create 返回的表模型对象。 */
#define XSqlTableModel_delete_base XClass_delete_base

/**
 * @brief 设置表名。
 * @param model 表模型；不能为 NULL。
 * @param tableName UTF-8 表名；借用，函数会复制，可为 NULL。
 * @return 无；清除原模型数据，并从数据库加载字段和主键信息。
 */
void XSqlTableModel_setTable_utf8(XSqlTableModel* model, const char* tableName);
/**
 * @brief 使用 XString 设置表名。
 * @param model 表模型；不能为 NULL。
 * @param tableName 表名；借用并深复制，可为 NULL 以清空表选择。
 * @return 无；清空当前缓存，随后尝试从关联数据库载入字段和主键元数据。
 */
void XSqlTableModel_setTable(XSqlTableModel* model, const XString* tableName);
/**
 * @brief 获取表名副本。
 * @param model 表模型；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlTableModel_tableName(const XSqlTableModel* model);
/**
 * @brief 获取模型单元格标志。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @return 单元格属性位；越界返回 NoItemFlags。
 */
XSqlItemFlags XSqlTableModel_flags(const XSqlTableModel* model, int row, int column);
/**
 * @brief 获取当前模型记录副本。
 * @param model 表模型；NULL 返回空记录。
 * @return 新记录，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlTableModel_record_current(const XSqlTableModel* model);
/**
 * @brief 获取指定行记录副本。
 * @param model 表模型；NULL 返回空记录。
 * @param row 行号，从 0 开始。
 * @return 新记录，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlTableModel_record(const XSqlTableModel* model, int row);
/**
 * @brief 获取单元格数据副本。
 * @param model 表模型；NULL 返回空值对象。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param role 数据角色。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlTableModel_data(const XSqlTableModel* model, int row, int column, XSqlItemDataRole role);
/**
 * @brief 设置单元格数据并标记行脏。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param value 新值；借用，函数会复制。
 * @param role 数据角色；当前只接受 Edit。
 * @return 成功返回 true；参数非法或提交失败返回 false。
 */
bool XSqlTableModel_setData(XSqlTableModel* model, int row, int column, const XVariant* value, XSqlItemDataRole role);
/**
 * @brief 将指定单元格置为空值。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_clearItemData(XSqlTableModel* model, int row, int column);
/**
 * @brief 获取表头数据副本。
 * @param model 表模型；NULL 返回空值对象。
 * @param section 表头索引，从 0 开始。
 * @param orientation 水平或垂直方向。
 * @param role 数据角色。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlTableModel_headerData(const XSqlTableModel* model, int section, XSqlOrientation orientation, XSqlItemDataRole role);
/**
 * @brief 判断模型是否存在脏数据。
 * @param model 表模型；NULL 返回 false。
 * @return 存在未提交修改返回 true。
 */
bool XSqlTableModel_isDirty(const XSqlTableModel* model);
/**
 * @brief 判断指定行是否存在脏数据。
 * @param model 表模型；NULL 返回 false。
 * @param row 行号，从 0 开始。
 * @return 存在未提交修改返回 true。
 */
bool XSqlTableModel_isDirty_row(const XSqlTableModel* model, int row);
/**
 * @brief 清空模型数据和脏标记。
 * @param model 表模型；不能为 NULL。
 * @return 无；模型保持已初始化状态。
 */
void XSqlTableModel_clear(XSqlTableModel* model);
/**
 * @brief 设置编辑策略。
 * @param model 表模型；不能为 NULL。
 * @param strategy 编辑策略。
 * @return 无。
 */
void XSqlTableModel_setEditStrategy(XSqlTableModel* model, XSqlTableEditStrategy strategy);
/**
 * @brief 获取编辑策略。
 * @param model 表模型；NULL 返回 OnRowChange。
 * @return 当前编辑策略。
 */
XSqlTableEditStrategy XSqlTableModel_editStrategy(const XSqlTableModel* model);
/**
 * @brief 获取主键索引副本。
 * @param model 表模型；NULL 返回空索引。
 * @return 新索引，调用者必须使用 XSqlIndex_delete_base 释放。
 */
XSqlIndex* XSqlTableModel_primaryKey(const XSqlTableModel* model);
/**
 * @brief 获取数据库连接副本。
 * @param model 表模型；NULL 返回空连接。
 * @return 新连接句柄，调用者必须使用 XSqlDatabase_delete_base 释放。
 */
XSqlDatabase* XSqlTableModel_database(const XSqlTableModel* model);
/**
 * @brief 按 UTF-8 字段名获取字段索引。
 * @param model 表模型；NULL 返回 -1。
 * @param fieldName UTF-8 字段名；借用。
 * @return 字段索引；未找到返回 -1。
 */
int XSqlTableModel_fieldIndex(const XSqlTableModel* model, const char* fieldName);
/**
 * @brief 使用 XString 查找字段索引。
 * @param model 表模型；可为 NULL。
 * @param fieldName 字段名；借用，可为 NULL。
 * @return 字段索引；模型、名称无效或未找到时返回 -1。
 */
int XSqlTableModel_fieldIndex_2(const XSqlTableModel* model, const XString* fieldName);
/**
 * @brief 设置排序并重新查询。
 * @param model 表模型；不能为 NULL。
 * @param column 排序列，从 0 开始。
 * @param order 排序方向。
 * @return 无。
 */
void XSqlTableModel_sort(XSqlTableModel* model, int column, XSqlSortOrder order);
/**
 * @brief 设置排序参数。
 * @param model 表模型；不能为 NULL。
 * @param column 排序列，从 0 开始。
 * @param order 排序方向。
 * @return 无。
 */
void XSqlTableModel_setSort(XSqlTableModel* model, int column, XSqlSortOrder order);
/**
 * @brief 获取过滤表达式副本。
 * @param model 表模型；NULL 返回空字符串。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlTableModel_filter(const XSqlTableModel* model);
/**
 * @brief 设置 UTF-8 过滤表达式。
 * @param model 表模型；不能为 NULL。
 * @param filter SQL 过滤表达式；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlTableModel_setFilter_utf8(XSqlTableModel* model, const char* filter);
/**
 * @brief 使用 XString 设置 SQL 过滤表达式。
 * @param model 表模型；不能为 NULL。
 * @param filter SQL WHERE 片段；借用并深复制，可为 NULL 以清空过滤条件。
 * @return 无；调用 select 前只更新配置，已加载行不会自动重新查询。
 */
void XSqlTableModel_setFilter(XSqlTableModel* model, const XString* filter);
/**
 * @brief 获取模型行数。
 * @param model 表模型；NULL 返回 0。
 * @return 行数。
 */
int XSqlTableModel_rowCount(const XSqlTableModel* model);
/**
 * @brief 删除模型列。
 * @param model 表模型；不能为 NULL。
 * @param column 起始列。
 * @param count 删除数量。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_removeColumns(XSqlTableModel* model, int column, int count);
/**
 * @brief 删除模型行。
 * @param model 表模型；不能为 NULL。
 * @param row 起始行。
 * @param count 删除数量。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_removeRows(XSqlTableModel* model, int row, int count);
/**
 * @brief 插入空白模型行。
 * @param model 表模型；不能为 NULL。
 * @param row 插入位置。
 * @param count 插入数量。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_insertRows(XSqlTableModel* model, int row, int count);
/**
 * @brief 插入记录。
 * @param model 表模型；不能为 NULL。
 * @param row 插入位置，负值表示追加。
 * @param record 待插入记录；借用，函数会复制。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_insertRecord(XSqlTableModel* model, int row, const XSqlRecord* record);
/**
 * @brief 替换指定行记录。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param record 新记录；借用，函数会复制。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlTableModel_setRecord(XSqlTableModel* model, int row, const XSqlRecord* record);
/**
 * @brief 撤销指定行修改。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @return 无。
 */
void XSqlTableModel_revertRow(XSqlTableModel* model, int row);
/**
 * @brief 查询当前表。
 * @param model 表模型；不能为 NULL。
 * @return 查询成功返回 true，否则返回 false。
 */
bool XSqlTableModel_select(XSqlTableModel* model);
/**
 * @brief 查询并定位指定行。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @return 查询成功且行有效返回 true，否则返回 false。
 */
bool XSqlTableModel_selectRow(XSqlTableModel* model, int row);
/**
 * @brief 提交当前修改。
 * @param model 表模型；不能为 NULL。
 * @return 全部提交成功返回 true，否则返回 false。
 */
bool XSqlTableModel_submit(XSqlTableModel* model);
/**
 * @brief 撤销当前修改。
 * @param model 表模型；不能为 NULL。
 * @return 无。
 */
void XSqlTableModel_revert(XSqlTableModel* model);
/**
 * @brief 提交全部脏行。
 * @param model 表模型；不能为 NULL。
 * @return 全部提交成功返回 true，否则返回 false。
 */
bool XSqlTableModel_submitAll(XSqlTableModel* model);
/**
 * @brief 撤销全部修改。
 * @param model 表模型；不能为 NULL。
 * @return 无。
 */
void XSqlTableModel_revertAll(XSqlTableModel* model);

/**
 * @brief 更新数据库中的指定行。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param values 待更新字段；借用。
 * @return 更新成功返回 true，否则返回 false。
 */
bool XSqlTableModel_updateRowInTable(XSqlTableModel* model, int row, const XSqlRecord* values);
/**
 * @brief 向数据库插入记录。
 * @param model 表模型；不能为 NULL。
 * @param values 待插入字段；借用。
 * @return 插入成功返回 true，否则返回 false。
 */
bool XSqlTableModel_insertRowIntoTable(XSqlTableModel* model, const XSqlRecord* values);
/**
 * @brief 从数据库删除指定行。
 * @param model 表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @return 删除成功返回 true，否则返回 false。
 */
bool XSqlTableModel_deleteRowFromTable(XSqlTableModel* model, int row);
/**
 * @brief 生成排序子句。
 * @param model 表模型；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlTableModel_orderByClause(const XSqlTableModel* model);
/**
 * @brief 生成当前表查询语句。
 * @param model 表模型；NULL 或未设置表名时返回空字符串对象。
 * @return 新 SQL 字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlTableModel_selectStatement(const XSqlTableModel* model);
/**
 * @brief 设置主键索引。
 * @param model 表模型；不能为 NULL。
 * @param key 主键索引；借用，函数会复制。
 * @return 无；key 为 NULL 时保持原主键。
 */
void XSqlTableModel_setPrimaryKey(XSqlTableModel* model, const XSqlIndex* key);
/**
 * @brief 提取指定行的主键字段副本。
 * @param model 表模型；NULL 返回空记录。
 * @param row 行号，从 0 开始。
 * @return 新记录，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlTableModel_primaryValues(const XSqlTableModel* model, int row);

/**
 * @brief 发射 primeInsert 信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param row 待插入行号。
 * @return 信号句柄。
 */
void* XSqlTableModel_primeInsert_signal(XSqlTableModel* model, int row);
/**
 * @brief 发射 beforeInsert 信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param record 即将插入的记录；信号调用期间借用。
 * @return 信号句柄。
 */
void* XSqlTableModel_beforeInsert_signal(XSqlTableModel* model, XSqlRecord* record);
/**
 * @brief 发射 beforeUpdate 信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param row 即将更新的行号。
 * @param record 即将写入的记录；信号调用期间借用。
 * @return 信号句柄。
 */
void* XSqlTableModel_beforeUpdate_signal(XSqlTableModel* model, int row, XSqlRecord* record);
/**
 * @brief 发射 beforeDelete 信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param row 即将删除的行号。
 * @return 信号句柄。
 */
void* XSqlTableModel_beforeDelete_signal(XSqlTableModel* model, int row);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlTableModel_create
#define XSqlTableModel_create(...) XSqlTableModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XSQLTABLEMODEL_H */
