/******************************************************************************
 * @file       XCellReference.h
 * @brief      XCellReference 单元格引用类（对标 QXlsx::CellReference）
 * @author     XinYueC 团队
 * @note       提供单元格引用（行、列）的表示与操作，支持"A1"列号字符串表示法
 *             与 1 索引的行列号之间的转换，对齐 QXlsx::CellReference 全部功能
 ******************************************************************************/
#ifndef XCELLREFERENCE_H
#define XCELLREFERENCE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"

/**
 * @brief      XCellReference 单元格引用结构体
 * @note       表示 Excel 中单元格的行列位置，行列均为 1 索引（1-based）。
 *             支持 "A1" 格式字符串与行列号的相互转换。
 *             对齐 QXlsx::CellReference 全部功能。
 */
typedef struct XCellReference
{
    int m_row;     /**< 行号（1 索引，-1 表示无效） */
    int m_column;  /**< 列号（1 索引，-1 表示无效） */
} XCellReference;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建一个无效的 XCellReference 对象
 * @return     无效的 XCellReference 对象（行和列均为 -1）
 */
XCellReference XCellReference_create(void);

/**
 * @brief      使用行和列号创建 XCellReference 对象
 * @param row    行号（1 索引）
 * @param column 列号（1 索引）
 * @return     对应的 XCellReference 对象
 */
XCellReference XCellReference_create_ex(int row, int column);

/**
 * @brief      从字符串创建 XCellReference 对象（如 "A1"、"$B$2"、"AB123"）
 * @param cell  单元格引用字符串
 * @return     解析后的 XCellReference 对象，无效字符串返回无效对象
 */
XCellReference XCellReference_create_str(const XString* cell);
/**
 * @brief 从 UTF-8 单元格引用文本创建引用值对象。
 * @param cell UTF-8 引用文本，例如 "A1"。
 * @return 解析后的引用值对象；文本无效时返回无效引用。
 */
XCellReference XCellReference_create_str_utf8(const char* cell);

/**
 * @brief      从 C 字符串字面量创建 XCellReference 对象
 * @param cell  单元格引用字符串（如 "A1"）
 * @return     解析后的 XCellReference 对象
 */
XCellReference XCellReference_create_char(const XString* cell);

/**
 * @brief      复制 XCellReference 对象
 * @param self  目标 XCellReference 对象指针
 * @param other 源 XCellReference 对象指针
 */
void XCellReference_copy(XCellReference* self, const XCellReference* other);

/**
 * @brief      初始化 XCellReference 对象
 * @param self 待初始化的 XCellReference 对象指针
 * @note       初始化为无效值（行和列均为 -1）
 */
void XCellReference_init(XCellReference* self);

/**
 * @brief      使用行和列号初始化 XCellReference 对象
 * @param self   待初始化的 XCellReference 对象指针
 * @param row    行号（1 索引）
 * @param column 列号（1 索引）
 */
void XCellReference_init_ex(XCellReference* self, int row, int column);

/**
 * @brief      从字符串初始化 XCellReference 对象
 * @param self 待初始化的 XCellReference 对象指针
 * @param cell 单元格引用字符串（如 "A1"）
 */
void XCellReference_init_str(XCellReference* self, const XString* cell);
/**
 * @brief 使用 UTF-8 文本初始化单元格引用。
 * @param self 单元格引用值对象指针。
 * @param cell UTF-8 引用文本，例如 "B2"。
 */
void XCellReference_init_str_utf8(XCellReference* self, const char* cell);

/* ========== 访问方法 ========== */

/**
 * @brief      设置行号
 * @param self 目标 XCellReference 对象指针
 * @param row  行号（1 索引）
 */
void XCellReference_setRow(XCellReference* self, int row);

/**
 * @brief      设置列号
 * @param self   目标 XCellReference 对象指针
 * @param column 列号（1 索引）
 */
void XCellReference_setColumn(XCellReference* self, int column);

/**
 * @brief      获取行号
 * @param self 目标 XCellReference 对象指针
 * @return     行号（1 索引），无效时返回 -1
 */
int XCellReference_row(const XCellReference* self);

/**
 * @brief      获取列号
 * @param self 目标 XCellReference 对象指针
 * @return     列号（1 索引），无效时返回 -1
 */
int XCellReference_column(const XCellReference* self);

/* ========== 查询方法 ========== */

/**
 * @brief      判断引用是否有效
 * @param self 目标 XCellReference 对象指针
 * @return     有效返回 true，无效返回 false
 */
bool XCellReference_isValid(const XCellReference* self);

/**
 * @brief      将引用转换为字符串（如 "A1"）
 * @param self       目标 XCellReference 对象指针
 * @param row_abs    行是否绝对引用（$ 前缀）
 * @param col_abs    列是否绝对引用（$ 前缀）
 * @return     字符串表示（需要调用 XString_deinit_base 释放）
 */
XString XCellReference_toString(const XCellReference* self, bool row_abs, bool col_abs);

/* ========== 静态工具方法 ========== */

/**
 * @brief      将列号转换为列名字符串（如 1->
"A"，27->"AA"）
 * @param column 列号（1 索引）
 * @return     列名字符串（需要调用 XString_deinit_base 释放）
 */
XString XCellReference_columnToName(int column);

/**
 * @brief      将列名字符串转换为列号（如 "A"->1，"AA"->27）
 * @param colName 列名字符串
 * @return     列号（1 索引），无效时返回 -1
 */
int XCellReference_nameToColumn(const XString* colName);
/**
 * @brief 将 UTF-8 列名转换为 1 起始的列号。
 * @param colName UTF-8 列名，例如 "A" 或 "AA"。
 * @return 列号；名称无效返回 0 或负值，具体以实现约定为准。
 */
int XCellReference_nameToColumn_utf8(const char* colName);

/* ========== 比较运算符（内联函数） ========== */

/**
 * @brief      判断两个引用是否相等
 * @param a 引用 A
 * @param b 引用 B
 * @return    相等返回 true
 */
static inline bool XCellReference_equals(const XCellReference* a, const XCellReference* b)
{
    return a->m_row == b->m_row && a->m_column == b->m_column;
}

/**
 * @brief      判断两个引用是否不相等
 * @param a 引用 A
 * @param b 引用 B
 * @return    不相等返回 true
 */
static inline bool XCellReference_notEquals(const XCellReference* a, const XCellReference* b)
{
    return a->m_row != b->m_row || a->m_column != b->m_column;
}

#ifdef __cplusplus
}
#endif

#endif /* XCELLREFERENCE_H */
