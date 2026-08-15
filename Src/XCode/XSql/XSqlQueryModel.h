/**
 * @file       XSqlQueryModel.h
 * @brief      SQL 查询模型，对齐 Qt 6.8 QSqlQueryModel。
 * @details    XinYueC 没有强制依赖 Qt 的 QModelIndex/QAbstractItemModel；本类使用
 *             XSqlModelIndex 和行列参数表达相同的表格模型行为。
 */
#ifndef XSQLQUERYMODEL_H
#define XSQLQUERYMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlQuery.h"
#include "XObject.h"

/** @brief 无 GUI 的模型索引。 */
typedef struct XSqlModelIndex {
    int m_row;    /**< 行号，从 0 开始；无效索引为 -1。 */
    int m_column; /**< 列号，从 0 开始；无效索引为 -1。 */
} XSqlModelIndex;

/** @brief 一个带模型列和角色的自定义表头值。 */
typedef struct XSqlModelHeaderValue {
    int m_section;              /**< 水平表头列号。 */
    int m_role;                 /**< Qt ItemDataRole 数值。 */
    XString* m_value;           /**< 表头值，由模型对象拥有。 */
} XSqlModelHeaderValue;

/** @brief XSqlQueryModel 的内部虚函数槽位；具体模型通过这些槽位扩展装载和索引映射行为。 */
XCLASS_DEFINE_BEGING(XSqlQueryModel)
XCLASS_DEFINE_ENUM(XSqlQueryModel, Clear) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XSqlQueryModel, QueryChange),
XCLASS_DEFINE_ENUM(XSqlQueryModel, IndexInQuery),
XCLASS_DEFINE_END(XSqlQueryModel)

/** @brief SQL 查询模型。 */
typedef struct XSqlQueryModel {
    XObject m_class;             /**< 第一个成员，由 XObject 管理；模型不可复制。 */
    XSqlQuery m_query;           /**< 当前查询对象。 */
    XSqlRecord m_record;         /**< 查询字段描述。 */
    XSqlRecord** m_rows;         /**< 已加载行，由模型对象拥有。 */
    size_t m_rowCount;           /**< 已加载行数。 */
    size_t m_rowCapacity;        /**< 行数组容量。 */
    XSqlModelHeaderValue* m_headers; /**< 按列和角色保存的自定义表头，由模型对象拥有。 */
    size_t m_headerCount;        /**< 自定义表头值数量。 */
    size_t m_headerCapacity;     /**< 表头容量。 */
    XSqlError m_lastError;       /**< 最近模型错误。 */
    bool m_canFetchMore;         /**< 是否还有未加载数据。 */
} XSqlQueryModel;

/**
 * @brief 初始化查询模型虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlQueryModel_class_init(void);
/**
 * @brief 初始化查询模型。
 * @param model 待初始化模型；不能为 NULL。
 * @return 无；模型继承 XObject，不能复制。
 */
void XSqlQueryModel_init(XSqlQueryModel* model);
/**
 * @brief 创建查询模型。
 * @return 新模型，调用者必须使用 XSqlQueryModel_delete_base 释放；失败返回 NULL。
 */
XSqlQueryModel* XSqlQueryModel_create_ex(XMemoryType memory);

/** @brief 调用 XClass 析构入口释放查询、行缓存、表头和最近错误。 */
#define XSqlQueryModel_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlQueryModel_create 返回的查询模型。 */
#define XSqlQueryModel_delete_base XClass_delete_base

/**
 * @brief 获取当前已加载行数。
 * @param model 查询模型；NULL 返回 0。
 * @return 已加载行数。
 */
int XSqlQueryModel_rowCount(const XSqlQueryModel* model);
/**
 * @brief 获取列数。
 * @param model 查询模型；NULL 返回 0。
 * @return 列数。
 */
int XSqlQueryModel_columnCount(const XSqlQueryModel* model);
/**
 * @brief 获取指定行记录副本。
 * @param model 查询模型；NULL 返回空记录。
 * @param row 行号，从 0 开始。
 * @return 新记录对象，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlQueryModel_record(const XSqlQueryModel* model, int row);
/**
 * @brief 获取当前查询字段记录副本。
 * @param model 查询模型；NULL 返回空记录。
 * @return 新记录对象，调用者必须使用 XSqlRecord_delete_base 释放。
 */
XSqlRecord* XSqlQueryModel_record_current(const XSqlQueryModel* model);
/**
 * @brief 获取模型单元格数据副本。
 * @param model 查询模型；NULL 返回空值对象。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param role 数据角色。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlQueryModel_data(const XSqlQueryModel* model, int row, int column, XSqlItemDataRole role);
/**
 * @brief 获取表头数据副本。
 * @param model 查询模型；NULL 返回空值对象。
 * @param section 表头索引，从 0 开始。
 * @param orientation 水平或垂直方向。
 * @param role 数据角色。
 * @return 新值对象，调用者必须使用 XVariant_delete_base 释放。
 */
XVariant* XSqlQueryModel_headerData(const XSqlQueryModel* model, int section, XSqlOrientation orientation, XSqlItemDataRole role);
/**
 * @brief 设置表头数据。
 * @param model 查询模型；不能为 NULL。
 * @param section 表头索引，从 0 开始。
 * @param orientation 水平或垂直方向；当前实现只支持水平。
 * @param value 表头值；借用，函数会复制内容。
 * @param role 数据角色。
 * @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XSqlQueryModel_setHeaderData(XSqlQueryModel* model, int section, XSqlOrientation orientation, const XVariant* value, XSqlItemDataRole role);
/**
 * @brief 插入模型列。
 * @param model 查询模型；不能为 NULL。
 * @param column 插入位置。
 * @param count 插入数量。
 * @return 成功返回 true；插入的列初始为无效值，不会修改底层查询。
 */
bool XSqlQueryModel_insertColumns(XSqlQueryModel* model, int column, int count);
/**
 * @brief 删除模型列。
 * @param model 查询模型；不能为 NULL。
 * @param column 删除起始列。
 * @param count 删除数量。
 * @return 成功返回 true；删除只影响模型缓存，不会修改底层查询。
 */
bool XSqlQueryModel_removeColumns(XSqlQueryModel* model, int column, int count);
/**
 * @brief 移入查询对象并替换当前结果。
 * @param model 查询模型；不能为 NULL。
 * @param query 查询对象；借用外壳，函数转移其结果对象所有权。
 * @return 无；query 的结果对象被清空，query 本身仍由调用者释放。
 */
void XSqlQueryModel_setQuery_move(XSqlQueryModel* model, XSqlQuery* query);
/**
 * @brief 复制查询并设置模型，兼容 Qt QSqlQueryModel::setQuery(const QSqlQuery&)。
 * @param model 查询模型；不能为 NULL。
 * @param query 源查询；借用，函数复制其内容。
 * @return 查询有效并成功装载返回 true，否则返回 false。
 */
bool XSqlQueryModel_setQuery_copy(XSqlQueryModel* model, const XSqlQuery* query);
/**
 * @brief 执行 SQL 并设置模型查询。
 * @param model 查询模型；不能为 NULL。
 * @param sql SQL 文本；借用。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 执行并装载成功返回 true，否则返回 false。
 */
bool XSqlQueryModel_setQuery(XSqlQueryModel* model, const XString* sql, const XSqlDatabase* database);
/**
 * @brief 使用 UTF-8 SQL 执行并设置模型查询。
 * @param model 查询模型；不能为 NULL。
 * @param sql UTF-8 SQL 字符串；借用。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 执行并装载成功返回 true，否则返回 false。
 */
bool XSqlQueryModel_setQuery_utf8(XSqlQueryModel* model, const char* sql, const XSqlDatabase* database);
/**
 * @brief 获取当前查询内部借用指针。
 * @param model 查询模型；NULL 返回 NULL。
 * @return 内部查询对象；模型销毁或下一次设置查询后失效，调用者不得释放。
 */
const XSqlQuery* XSqlQueryModel_query_const(const XSqlQueryModel* model);
/**
 * @brief 获取当前查询副本。
 * @param model 查询模型；NULL 返回空查询对象。
 * @return 新查询对象，调用者必须使用 XSqlQuery_delete_base 释放。
 */
XSqlQuery* XSqlQueryModel_query(const XSqlQueryModel* model);
/**
 * @brief 清空查询和模型行数据。
 * @param model 查询模型；不能为 NULL。
 * @return 无；模型保持已初始化状态，并发射 modelReset 信号。
 */
void XSqlQueryModel_clear(XSqlQueryModel* model);
/**
 * @brief 获取最近模型错误副本。
 * @param model 查询模型；NULL 返回未知错误对象。
 * @return 新错误对象，调用者必须使用 XSqlError_delete_base 释放。
 */
XSqlError* XSqlQueryModel_lastError(const XSqlQueryModel* model);
/**
 * @brief 请求加载下一批结果行。
 * @param model 查询模型；不能为 NULL。
 * @return 无；对于不支持 QuerySize 的驱动，每次最多追加 255 行。
 */
void XSqlQueryModel_fetchMore(XSqlQueryModel* model);
/**
 * @brief 判断是否还有更多结果行。
 * @param model 查询模型；NULL 返回 false。
 * @return 仍可加载返回 true，否则返回 false。
 */
bool XSqlQueryModel_canFetchMore(const XSqlQueryModel* model);
/**
 * @brief 获取角色名称列表。
 * @param model 查询模型；当前参数仅用于保持 API 一致，可为 NULL。
 * @return 新字符串列表，调用者必须使用 XStringList_delete_base 释放。
 */
XStringList* XSqlQueryModel_roleNames(const XSqlQueryModel* model);
/**
 * @brief 将模型索引映射到查询索引。
 * @param model 查询模型；可为 NULL。
 * @param item 模型索引。
 * @return 查询索引；无映射时返回原索引。
 */
XSqlModelIndex XSqlQueryModel_indexInQuery(const XSqlQueryModel* model, XSqlModelIndex item);
/**
 * @brief 设置最近模型错误。
 * @param model 查询模型；不能为 NULL。
 * @param error 错误对象；借用，函数会复制内容。
 * @return 无；error 为 NULL 时保持原错误。
 */
void XSqlQueryModel_setLastError(XSqlQueryModel* model, const XSqlError* error);
/**
 * @brief 通知模型查询发生变化并重新装载行。
 * @param model 查询模型；不能为 NULL。
 * @return 无；完成后模型行缓存反映当前查询。
 */
void XSqlQueryModel_queryChange(XSqlQueryModel* model);

/**
 * @brief 发射模型重置信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @return 信号句柄。
 */
void* XSqlQueryModel_modelReset_signal(XSqlQueryModel* model);

/**
 * @brief 发射模型数据变更信号。
 * @param model 信号发送对象；可为 NULL，仅用于获取信号标识。
 * @param firstRow 变更区域起始行，按 0 起始计数。
 * @param firstColumn 变更区域起始列，按 0 起始计数。
 * @param lastRow 变更区域结束行，按 0 起始计数。
 * @param lastColumn 变更区域结束列，按 0 起始计数。
 * @return 信号句柄。
 */
void* XSqlQueryModel_dataChanged_signal(XSqlQueryModel* model, int firstRow,
                                        int firstColumn, int lastRow, int lastColumn);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlQueryModel_create
#define XSqlQueryModel_create() XSqlQueryModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XSQLQUERYMODEL_H */
