/**
 * @file       XSqlRelation.h
 * @brief      SQL 外键关系描述类，对齐 Qt 6.8 QSqlRelation。
 * @details    保存显示列和实际键列的映射，不执行查询，也不拥有数据库连接。
 */
#ifndef XSQLRELATION_H
#define XSQLRELATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlGlobal.h"
#include "XString.h"

XCLASS_DEFINE_BEGING(XSqlRelation)
XCLASS_DEFINE_EXTEND_END(XSqlRelation, XClass)

/**
 * @brief SQL 外键关系描述。
 * @details 关系包含被引用表、被引用键列和显示列。该类不持有数据库连接。
 */
typedef struct XSqlRelation {
    XClass m_class;             /**< 第一个成员，由 XClass 管理。 */
    XString* m_tableName;       /**< 被引用表名，由对象拥有。 */
    XString* m_indexColumn;     /**< 被引用索引列名，由对象拥有。 */
    XString* m_displayColumn;   /**< 被引用显示列名，由对象拥有。 */
} XSqlRelation;

/**
 * @brief 初始化关系对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlRelation_class_init(void);
/**
 * @brief 初始化关系对象。
 * @param relation 待初始化对象；不能为 NULL。
 * @return 无。
 */
void XSqlRelation_init(XSqlRelation* relation);
/**
 * @brief 使用 UTF-8 名称初始化关系对象。
 * @param relation 待初始化对象；不能为 NULL。
 * @param tableName 被引用表名；借用，函数会复制，可为 NULL。
 * @param indexColumn 被引用索引列；借用，函数会复制，可为 NULL。
 * @param displayColumn 被引用显示列；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlRelation_init_utf8(XSqlRelation* relation, const char* tableName,
                            const char* indexColumn, const char* displayColumn);
/**
 * @brief 使用 XString 名称初始化关系对象。
 * @param relation 待初始化对象；不能为 NULL。
 * @param tableName 被引用表名；借用并深复制，可为 NULL。
 * @param indexColumn 被引用键列；借用并深复制，可为 NULL。
 * @param displayColumn 显示列；借用并深复制，可为 NULL。
 * @return 无；内存不足时对象保持可析构的空状态。
 */
void XSqlRelation_init_2(XSqlRelation* relation, const XString* tableName,
                         const XString* indexColumn, const XString* displayColumn);
/**
 * @brief 创建空关系对象。
 * @return 新关系对象，调用者必须使用 XSqlRelation_delete_base 释放；失败返回 NULL。
 */
XSqlRelation* XSqlRelation_create_ex(XMemoryType memory);
/**
 * @brief 使用 UTF-8 名称创建关系对象。
 * @param tableName 被引用表名；借用。
 * @param indexColumn 被引用索引列；借用。
 * @param displayColumn 被引用显示列；借用。
 * @return 新关系对象，调用者必须使用 XSqlRelation_delete_base 释放；失败返回 NULL。
 */
XSqlRelation* XSqlRelation_create_utf8(const char* tableName, const char* indexColumn, const char* displayColumn);
/**
 * @brief 使用 XString 名称创建关系对象。
 * @param tableName 被引用表名；借用，可为 NULL。
 * @param indexColumn 被引用键列；借用，可为 NULL。
 * @param displayColumn 显示列；借用，可为 NULL。
 * @return 新关系对象所有权；调用者使用 XSqlRelation_delete_base 释放，失败返回 NULL。
 */
XSqlRelation* XSqlRelation_create_2(const XString* tableName, const XString* indexColumn, const XString* displayColumn);
/**
 * @brief 深拷贝创建关系对象。
 * @param other 源关系；借用，不能为 NULL。
 * @return 新关系对象，调用者必须使用 XSqlRelation_delete_base 释放；失败返回 NULL。
 */
XSqlRelation* XSqlRelation_create_copy(const XSqlRelation* other);
/**
 * @brief 移动创建关系对象。
 * @param other 源关系；不能为 NULL，成功后资源被移出。
 * @return 新关系对象，调用者必须使用 XSqlRelation_delete_base 释放；失败返回 NULL。
 */
XSqlRelation* XSqlRelation_create_move(XSqlRelation* other);
/** @brief 调用 XClass 析构入口释放关系对象持有的三个名称。 */
#define XSqlRelation_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlRelation_create 系列函数返回的关系对象。 */
#define XSqlRelation_delete_base XClass_delete_base
/** @brief 将关系对象深复制到既有目标对象的基础复制入口。 */
#define XSqlRelation_copy_base XClass_copy_base
/** @brief 将关系对象资源移入既有目标对象的基础移动入口。 */
#define XSqlRelation_move_base XClass_move_base
/**
 * @brief 交换两个关系对象内容。
 * @param left 左关系；不能为 NULL。
 * @param right 右关系；不能为 NULL。
 * @return 无。
 */
void XSqlRelation_swap(XSqlRelation* left, XSqlRelation* right);
/**
 * @brief 设置 UTF-8 关系表名。
 * @param relation 关系对象；不能为 NULL。
 * @param tableName 表名；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlRelation_setTableName_utf8(XSqlRelation* relation, const char* tableName);
/** @brief 设置 XString 关系表名。 @param relation 关系对象；不能为 NULL。 @param tableName 表名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlRelation_setTableName(XSqlRelation* relation, const XString* tableName);
/**
 * @brief 设置 UTF-8 关联索引列。
 * @param relation 关系对象；不能为 NULL。
 * @param indexColumn 列名；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlRelation_setIndexColumn_utf8(XSqlRelation* relation, const char* indexColumn);
/** @brief 设置 XString 关联索引列。 @param relation 关系对象；不能为 NULL。 @param indexColumn 键列名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlRelation_setIndexColumn(XSqlRelation* relation, const XString* indexColumn);
/**
 * @brief 设置 UTF-8 显示列。
 * @param relation 关系对象；不能为 NULL。
 * @param displayColumn 列名；借用，函数会复制，可为 NULL。
 * @return 无。
 */
void XSqlRelation_setDisplayColumn_utf8(XSqlRelation* relation, const char* displayColumn);
/** @brief 设置 XString 显示列。 @param relation 关系对象；不能为 NULL。 @param displayColumn 显示列名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlRelation_setDisplayColumn(XSqlRelation* relation, const XString* displayColumn);
/**
 * @brief 获取关系表名副本。
 * @param relation 关系对象；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlRelation_tableName(const XSqlRelation* relation);
/**
 * @brief 获取关联索引列副本。
 * @param relation 关系对象；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlRelation_indexColumn(const XSqlRelation* relation);
/**
 * @brief 获取显示列副本。
 * @param relation 关系对象；NULL 返回空字符串对象。
 * @return 新字符串，调用者必须使用 XString_delete_base 释放。
 */
XString* XSqlRelation_displayColumn(const XSqlRelation* relation);
/**
 * @brief 判断关系描述是否完整。
 * @param relation 关系对象；NULL 返回 false。
 * @return 三个字段均非空返回 true，否则返回 false。
 */
bool XSqlRelation_isValid(const XSqlRelation* relation);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlRelation_create
#define XSqlRelation_create(...) XSqlRelation_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XSQLRELATION_H */
