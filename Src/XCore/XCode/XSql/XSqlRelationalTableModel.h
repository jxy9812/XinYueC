/**
 * @file       XSqlRelationalTableModel.h
 * @brief      SQL 关系表模型，对齐 Qt 6.8 QSqlRelationalTableModel。
 * @details    在 XSqlTableModel 的编辑缓存之上管理外键关系。显示值来自关联表，
 *             写入数据库时仍使用基础表的键值。
 */
#ifndef XSQLRELATIONALTABLEMODEL_H
#define XSQLRELATIONALTABLEMODEL_H

#if XSQL_ON

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlTableModel.h"
#include "XSqlRelation.h"

XCLASS_DEFINE_BEGING(XSqlRelationalTableModel)
XCLASS_DEFINE_ENUM(XSqlRelationalTableModel, SetRelation) = XCLASS_VTABLE_GET_SIZE(XSqlTableModel),
XCLASS_DEFINE_ENUM(XSqlRelationalTableModel, RelationModel),
XCLASS_DEFINE_END(XSqlRelationalTableModel)

/**
 * @brief 支持外键显示值的表模型。
 * @details 关系查询由驱动生成 SQL；模型本身不依赖 GUI 和平台 API。
 */
typedef struct XSqlRelationalTableModel {
    XSqlTableModel m_parent;       /**< 表模型基类。 */
    XSqlRecord m_baseRecord;       /**< 基础表字段描述，关系查询与写入时使用。 */
    XSqlRelation** m_relations;    /**< 各列关系，由模型拥有。 */
    XSqlTableModel** m_relationModels; /**< 各列关系子模型，由本模型拥有。 */
    size_t m_relationCount;        /**< 关系数组逻辑长度。 */
    size_t m_relationCapacity;     /**< 关系数组容量。 */
    XSqlRelationJoinMode m_joinMode; /**< 连接方式。 */
} XSqlRelationalTableModel;

/**
 * @brief 初始化关系表模型虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlRelationalTableModel_class_init(void);
/**
 * @brief 初始化关系表模型。
 * @param model 待初始化模型；不能为 NULL。
 * @param database 数据库连接；借用，可为 NULL。
 * @return 无；模型继承 XObject，不支持复制。
 */
void XSqlRelationalTableModel_init(XSqlRelationalTableModel* model, const XSqlDatabase* database);
/**
 * @brief 创建关系表模型。
 * @param memory 内存类型。
 * @param database 数据库连接；创建时复制连接句柄，可为 NULL。
 * @return 新模型，调用者必须使用 XClassDelete 释放；失败返回 NULL。
 */
XSqlRelationalTableModel* XSqlRelationalTableModel_create_ex(XMemoryType memory,  const XSqlDatabase* database);
/** @brief 调用 XClass 析构入口释放关系、子模型与行缓存。 */
/** @brief 释放由 XSqlRelationalTableModel_create 返回的关系表模型。 */
/**
 * @brief 获取关系字段显示值或编辑值副本。
 * @param model 关系表模型；NULL 返回空值对象。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param role 数据角色。
 * @return 新值对象，调用者必须使用 XClassDelete 释放。
 */
XVariant* XSqlRelationalTableModel_data(const XSqlRelationalTableModel* model, int row, int column, XSqlItemDataRole role);
/**
 * @brief 设置关系模型单元格数据。
 * @param model 关系表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @param column 列号，从 0 开始。
 * @param value 新值；借用，函数会复制。
 * @param role 数据角色。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlRelationalTableModel_setData(XSqlRelationalTableModel* model, int row, int column, const XVariant* value, XSqlItemDataRole role);
/**
 * @brief 删除关系模型列及对应关系。
 * @param model 关系表模型；不能为 NULL。
 * @param column 起始列。
 * @param count 删除数量。
 * @return 成功返回 true，否则返回 false。
 */
bool XSqlRelationalTableModel_removeColumns(XSqlRelationalTableModel* model, int column, int count);
/**
 * @brief 清空关系模型。
 * @param model 关系表模型；不能为 NULL。
 * @return 无。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlQueryModel_Clear（本类覆写该
 *           槽、未新增同名槽位），按约束文档 §497 条款 3 以宏别名复用基座唯一
 *           入口 `XSqlQueryModel_clear_base`；查槽分派行为与原分派体一致
 *           （首成员即 XSqlQueryModel，强转等价）。
 */
#define XSqlRelationalTableModel_clear_base(model) \
    XSqlQueryModel_clear_base((XSqlQueryModel*)(model))
/**
 * @brief 执行关系查询。
 * @param model 关系表模型；不能为 NULL。
 * @return 查询成功返回 true，否则返回 false。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlTableModel_Select，宏别名
 *           复用 `XSqlTableModel_select_base`（本类覆写该槽，分派落在本类覆写体）。
 */
#define XSqlRelationalTableModel_select_base(model) \
    XSqlTableModel_select_base((XSqlTableModel*)(model))
/**
 * @brief 设置 UTF-8 表名。
 * @param model 关系表模型；不能为 NULL。
 * @param tableName 表名；借用，函数会复制，可为 NULL。
 * @return 无。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlTableModel_SetTable，宏别名
 *           复用 `XSqlTableModel_setTable_utf8_base`（分派落在本类覆写体）。
 */
#define XSqlRelationalTableModel_setTable_utf8_base(model, tableName) \
    XSqlTableModel_setTable_utf8_base((XSqlTableModel*)(model), (tableName))
/**
 * @brief 使用 XString 设置基础表名。
 * @param model 关系表模型；不能为 NULL。
 * @param tableName 表名；借用并深复制，可为 NULL 以清空表选择。
 * @return 无；旧的关系查询缓存被清空，随后尝试加载基础表元数据。
 */
void XSqlRelationalTableModel_setTable(XSqlRelationalTableModel* model, const XString* tableName);
/**
 * @brief 设置字段关系。
 * @param model 关系表模型；不能为 NULL。
 * @param column 外键列，从 0 开始。
 * @param relation 关系描述；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlRelationalTableModel_setRelation_base(XSqlRelationalTableModel* model, int column, const XSqlRelation* relation);
/**
 * @brief 获取关系描述副本。
 * @param model 关系表模型；NULL 返回空关系对象。
 * @param column 外键列，从 0 开始。
 * @return 新关系对象，调用者必须使用 XClassDelete 释放。
 */
XSqlRelation* XSqlRelationalTableModel_relation(const XSqlRelationalTableModel* model, int column);
/**
 * @brief 获取关系字段的子模型。
 * @param model 关系表模型；不能为 NULL。
 * @param column 外键列，从 0 开始。
 * @return 本模型拥有的借用子模型；下次重设该列关系、清空或销毁本模型后失效。无有效关系时返回 NULL。
 */
XSqlTableModel* XSqlRelationalTableModel_relationModel_base(const XSqlRelationalTableModel* model, int column);
/**
 * @brief 设置关系连接模式。
 * @param model 关系表模型；不能为 NULL。
 * @param mode 内连接或左连接。
 * @return 无。
 */
void XSqlRelationalTableModel_setJoinMode(XSqlRelationalTableModel* model, XSqlRelationJoinMode mode);
/**
 * @brief 获取关系连接模式。
 * @param model 关系表模型；NULL 返回 InnerJoin。
 * @return 当前连接模式。
 */
XSqlRelationJoinMode XSqlRelationalTableModel_joinMode(const XSqlRelationalTableModel* model);
/**
 * @brief 生成包含关系连接的 SELECT 语句。
 * @param model 关系表模型；NULL 或未设置表名时返回空字符串对象。
 * @return 新 SQL 字符串，调用者必须使用 XClassDelete 释放。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlTableModel_SelectStatement，
 *           宏别名复用 `XSqlTableModel_selectStatement_base`（分派落在本类覆写体）。
 */
#define XSqlRelationalTableModel_selectStatement_base(model) \
    XSqlTableModel_selectStatement_base((const XSqlTableModel*)(model))
/**
 * @brief 生成关系模型排序子句。
 * @param model 关系表模型；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XClassDelete 释放。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlTableModel_OrderByClause，
 *           宏别名复用 `XSqlTableModel_orderByClause_base`（原转调体传
 *           `&model->m_parent` 与传 `(XSqlTableModel*)model` 同地址同虚表，
 *           分派行为一致）。
 */
#define XSqlRelationalTableModel_orderByClause_base(model) \
    XSqlTableModel_orderByClause_base((const XSqlTableModel*)(model))
/**
 * @brief 撤销关系模型指定行修改。
 * @param model 关系表模型；不能为 NULL。
 * @param row 行号，从 0 开始。
 * @return 无。
 * @note     [契约收口 2026-11-14] 槽位为父类 EXSqlTableModel_RevertRow，
 *           宏别名复用 `XSqlTableModel_revertRow_base`（分派落在本类覆写体）。
 */
#define XSqlRelationalTableModel_revertRow_base(model, row) \
    XSqlTableModel_revertRow_base((XSqlTableModel*)(model), (row))

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlRelationalTableModel_create
#define XSqlRelationalTableModel_create(...) XSqlRelationalTableModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XSQL_ON */

#endif /* XSQLRELATIONALTABLEMODEL_H */
