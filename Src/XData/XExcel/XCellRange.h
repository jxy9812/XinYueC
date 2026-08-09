/******************************************************************************
 * @file       XCellRange.h
 * @brief      XCellRange 单元格范围类（对标 QXlsx::CellRange）
 * @author     XinYueC 团队
 * @note       提供单元格范围（矩形区域）的表示与操作，支持"A1:B2"格式字符串
 *             与行列范围的相互转换，对齐 QXlsx::CellRange 全部功能
 ******************************************************************************/
#ifndef XCELLRANGE_H
#define XCELLRANGE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XCellReference.h"

/**
 * @brief      XCellRange 单元格范围结构体
 * @note       表示 Excel 中矩形单元格区域，由左上角和右下角行列号定义。
 *             对齐 QXlsx::CellRange 全部功能。
 */
typedef struct XCellRange
{
    int m_firstRow;     /**< 起始行号（1 索引） */
    int m_firstColumn;  /**< 起始列号（1 索引） */
    int m_lastRow;      /**< 结束行号（1 索引） */
    int m_lastColumn;   /**< 结束列号（1 索引） */
} XCellRange;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建一个无效的 XCellRange 对象
 * @return     无效的 XCellRange 对象（所有字段为 -1）
 */
XCellRange XCellRange_create(void);

/**
 * @brief      使用行列范围创建 XCellRange 对象
 * @param firstRow    起始行号（1 索引）
 * @param firstColumn 起始列号（1 索引）
 * @param lastRow     结束行号（1 索引）
 * @param lastColumn  结束列号（1 索引）
 * @return     对应的 XCellRange 对象
 */
XCellRange XCellRange_create_ex(int firstRow, int firstColumn, int lastRow, int lastColumn);

/**
 * @brief      使用两个 CellReference 创建 XCellRange 对象
 * @param topLeft     左上角引用
 * @param bottomRight 右下角引用
 * @return     对应的 XCellRange 对象
 */
XCellRange XCellRange_create_ref(const XCellReference* topLeft, const XCellReference* bottomRight);

/**
 * @brief      从字符串创建 XCellRange 对象（如 "A1:B2"、"A1"）
 * @param range 范围字符串
 * @return     解析后的 XCellRange 对象
 */
XCellRange XCellRange_create_str(const XString* range);
/**
 * @brief 从 UTF-8 范围文本创建单元格范围值对象。
 * @param range UTF-8 范围文本，例如 "A1:C3"。
 * @return 解析后的范围值对象；文本无效时返回无效范围。
 */
XCellRange XCellRange_create_str_utf8(const char* range);

/**
 * @brief      从 C 字符串字面量创建 XCellRange 对象
 * @param range 范围字符串
 * @return     解析后的 XCellRange 对象
 */
XCellRange XCellRange_create_char(const XString* range);

/**
 * @brief      复制 XCellRange 对象
 * @param self  目标指针
 * @param other 源指针
 */
void XCellRange_copy(XCellRange* self, const XCellRange* other);

/**
 * @brief      初始化 XCellRange 对象
 * @param self 待初始化的指针
 */
void XCellRange_init(XCellRange* self);

/**
 * @brief      使用行列范围初始化
 * @param self        待初始化的指针
 * @param firstRow    起始行号
 * @param firstColumn 起始列号
 * @param lastRow     结束行号
 * @param lastColumn  结束列号
 */
void XCellRange_init_ex(XCellRange* self, int firstRow, int firstColumn, int lastRow, int lastColumn);

/**
 * @brief      从字符串初始化
 * @param self  待初始化的指针
 * @param range 范围字符串
 */
void XCellRange_init_str(XCellRange* self, const XString* range);

/* ========== 访问方法 ========== */

/**
 * @brief      设置起始行号
 * @param self 指针
 * @param row  行号
 */
void XCellRange_setFirstRow(XCellRange* self, int row);

/**
 * @brief      设置结束行号
 * @param self 指针
 * @param row  行号
 */
void XCellRange_setLastRow(XCellRange* self, int row);

/**
 * @brief      设置起始列号
 * @param self 指针
 * @param col  列号
 */
void XCellRange_setFirstColumn(XCellRange* self, int col);

/**
 * @brief      设置结束列号
 * @param self 指针
 * @param col  列号
 */
void XCellRange_setLastColumn(XCellRange* self, int col);

/**
 * @brief      获取起始行号
 * @param self 指针
 * @return     起始行号
 */
int XCellRange_firstRow(const XCellRange* self);

/**
 * @brief      获取结束行号
 * @param self 指针
 * @return     结束行号
 */
int XCellRange_lastRow(const XCellRange* self);

/**
 * @brief      获取起始列号
 * @param self 指针
 * @return     起始列号
 */
int XCellRange_firstColumn(const XCellRange* self);

/**
 * @brief      获取结束列号
 * @param self 指针
 * @return     结束列号
 */
int XCellRange_lastColumn(const XCellRange* self);

/**
 * @brief      获取行数
 * @param self 指针
 * @return     行数
 */
int XCellRange_rowCount(const XCellRange* self);

/**
 * @brief      获取列数
 * @param self 指针
 * @return     列数
 */
int XCellRange_columnCount(const XCellRange* self);

/**
 * @brief      获取左上角引用
 * @param self 指针
 * @return     左上角 CellReference
 */
XCellReference XCellRange_topLeft(const XCellRange* self);

/**
 * @brief      获取右上角引用
 * @param self 指针
 * @return     右上角 CellReference
 */
XCellReference XCellRange_topRight(const XCellRange* self);

/**
 * @brief      获取左下角引用
 * @param self 指针
 * @return     左下角 CellReference
 */
XCellReference XCellRange_bottomLeft(const XCellRange* self);

/**
 * @brief      获取右下角引用
 * @param self 指针
 * @return     右下角 CellReference
 */
XCellReference XCellRange_bottomRight(const XCellRange* self);

/* ========== 查询方法 ========== */

/**
 * @brief      判断范围是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XCellRange_isValid(const XCellRange* self);

/**
 * @brief      将范围转换为字符串（如 "A1:B2"）
 * @param self     指针
 * @param row_abs  行是否绝对引用
 * @param col_abs  列是否绝对引用
 * @return     字符串表示（需调用 XString_deinit_base 释放）
 */
XString XCellRange_toString(const XCellRange* self, bool row_abs, bool col_abs);

/* ========== 比较运算符（内联函数） ========== */

static inline bool XCellRange_equals(const XCellRange* a, const XCellRange* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    return a->m_firstRow == b->m_firstRow && a->m_firstColumn == b->m_firstColumn
        && a->m_lastRow == b->m_lastRow && a->m_lastColumn == b->m_lastColumn;
}

static inline bool XCellRange_notEquals(const XCellRange* a, const XCellRange* b)
{
    return !XCellRange_equals(a, b);
}

/* ========== 便捷设置方法 ========== */

/**
 * @brief      设置范围为单个单元格
 * @param self 指针
 * @param row  行号
 * @param col  列号
 */
void XCellRange_setCell(XCellRange* self, int row, int col);

/**
 * @brief      使用 CellReference 设置范围为单个单元格
 * @param self 指针
 * @param cell 单元格引用
 */
void XCellRange_setCellReference(XCellRange* self, const XCellReference* cell);

/**
 * @brief      设置范围为完整矩形区域
 * @param self        指针
 * @param firstRow    起始行
 * @param firstCol    起始列
 * @param lastRow     结束行
 * @param lastCol     结束列
 */
void XCellRange_setFullRange(XCellRange* self, int firstRow, int firstCol, int lastRow, int lastCol);

#ifdef __cplusplus
}
#endif
#endif /* XCELLRANGE_H */
