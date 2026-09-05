/**
 * @file       XSqlIndex.h
 * @brief      SQL 索引描述类，对齐 Qt 6.8 QSqlIndex。
 * @details    继承 XSqlRecord 保存索引字段，并额外保存游标名、索引名和逐字段排序方向。
 */
#ifndef XSQLINDEX_H
#define XSQLINDEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlRecord.h"

XCLASS_DEFINE_BEGING(XSqlIndex)
XCLASS_DEFINE_EXTEND_END(XSqlIndex, XSqlRecord)

/**
 * @brief SQL 索引字段集合。
 */
typedef struct XSqlIndex {
    XSqlRecord m_parent; /**< 基类记录，必须位于第一个成员。 */
    XString* m_cursorName; /**< 游标名，由对象拥有。 */
    XString* m_name;       /**< 索引名，由对象拥有。 */
    bool* m_descending;    /**< 各字段降序标记，由对象拥有。 */
    size_t m_sortCount;    /**< 排序标记数量。 */
    size_t m_sortCapacity; /**< 排序标记容量。 */
} XSqlIndex;

/**
 * @brief 初始化索引对象虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 */
XVtable* XSqlIndex_class_init(void);
/** @brief 初始化空索引。 @param index 待初始化索引；不能为 NULL。 @return 无；对象进入可析构的空索引状态。 */
void XSqlIndex_init(XSqlIndex* index);
/** @brief 创建空索引。 @return 新索引所有权；调用者使用 XSqlIndex_delete_base 释放，失败返回 NULL。 */
XSqlIndex* XSqlIndex_create_ex(XMemoryType memory);
/** @brief 使用 UTF-8 游标名和索引名创建索引。 @param cursorName 游标名；借用并复制，可为 NULL。 @param name 索引名；借用并复制，可为 NULL。 @return 新索引所有权；调用者使用 XSqlIndex_delete_base 释放，失败返回 NULL。 */
XSqlIndex* XSqlIndex_create_utf8(const char* cursorName, const char* name);
/** @brief 使用 XString 游标名和索引名创建索引。 @param cursorName 游标名；借用并复制，可为 NULL。 @param name 索引名；借用并复制，可为 NULL。 @return 新索引所有权；调用者使用 XSqlIndex_delete_base 释放，失败返回 NULL。 */
XSqlIndex* XSqlIndex_create_2(const XString* cursorName, const XString* name);
/** @brief 深拷贝创建索引。 @param other 源索引；借用，不能为 NULL。 @return 新索引所有权；调用者使用 XSqlIndex_delete_base 释放，失败返回 NULL。 */
XSqlIndex* XSqlIndex_create_copy(const XSqlIndex* other);
/** @brief 移动创建索引。 @param other 源索引；不能为 NULL，成功后资源被移出但对象仍需反初始化。 @return 新索引所有权；调用者使用 XSqlIndex_delete_base 释放，失败返回 NULL。 */
XSqlIndex* XSqlIndex_create_move(XSqlIndex* other);

/** @brief 调用 XClass 析构入口释放索引对象和继承的字段记录。 */
#define XSqlIndex_deinit_base XClass_deinit_base
/** @brief 释放由 XSqlIndex_create 系列函数返回的索引对象。 */
#define XSqlIndex_delete_base XClass_delete_base

/** @brief 交换两个索引对象内容。 @param left 左索引；不能为 NULL。 @param right 右索引；不能为 NULL。 @return 无；字段、名称与排序方向一并交换。 */
void XSqlIndex_swap(XSqlIndex* left, XSqlIndex* right);

/** @brief 设置 UTF-8 游标名。 @param index 索引对象；不能为 NULL。 @param name 游标名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlIndex_setCursorName_utf8(XSqlIndex* index, const char* name);
/** @brief 设置 XString 游标名。 @param index 索引对象；不能为 NULL。 @param name 游标名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlIndex_setCursorName(XSqlIndex* index, const XString* name);
/** @brief 获取游标名副本。 @param index 索引对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlIndex_cursorName(const XSqlIndex* index);
/** @brief 设置 UTF-8 索引名。 @param index 索引对象；不能为 NULL。 @param name 索引名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlIndex_setName_utf8(XSqlIndex* index, const char* name);
/** @brief 设置 XString 索引名。 @param index 索引对象；不能为 NULL。 @param name 索引名；借用并深复制，可为 NULL。 @return 无；内存不足时保留旧名称。 */
void XSqlIndex_setName(XSqlIndex* index, const XString* name);
/** @brief 获取索引名副本。 @param index 索引对象；可为 NULL。 @return 新字符串所有权；调用者使用 XString_delete_base 释放。 */
XString* XSqlIndex_name(const XSqlIndex* index);
/** @brief 追加升序字段副本。 @param index 索引对象；不能为 NULL。 @param field 字段；借用，函数深复制，不能为 NULL。 @return 追加成功返回 true；内存不足或参数无效返回 false。 */
bool XSqlIndex_append(XSqlIndex* index, const XSqlField* field);
/** @brief 追加字段副本并设置排序方向。 @param index 索引对象；不能为 NULL。 @param field 字段；借用，函数深复制，不能为 NULL。 @param descending 是否降序。 @return 追加成功返回 true；内存不足或参数无效返回 false。 */
bool XSqlIndex_append_2(XSqlIndex* index, const XSqlField* field, bool descending);
/** @brief 获取指定字段降序标志。 @param index 索引对象；可为 NULL。 @param fieldIndex 字段索引，从 0 开始。 @return 降序返回 true；越界或 NULL 返回 false。 */
bool XSqlIndex_isDescending(const XSqlIndex* index, int fieldIndex);
/** @brief 设置指定字段降序标志。 @param index 索引对象；不能为 NULL。 @param fieldIndex 字段索引，从 0 开始。 @param descending 是否降序。 @return 无；越界时对象保持不变。 */
void XSqlIndex_setDescending(XSqlIndex* index, int fieldIndex, bool descending);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqlIndex_create
#define XSqlIndex_create() XSqlIndex_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XSQLINDEX_H */
