/******************************************************************************
 * @file       XCellFormula.h
 * @brief      XCellFormula 单元格公式类（对标 QXlsx::CellFormula）
 * @author     XinYueC 团队
 * @note       提供单元格公式的表示，支持普通公式、共享公式和数组公式。
 *             对齐 QXlsx::CellFormula 全部功能
 ******************************************************************************/
#ifndef XCELLFORMULA_H
#define XCELLFORMULA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XCellReference.h"

/**
 * @brief      公式类型枚举
 */
typedef enum XCellFormula_Type
{
    XCellFormula_Normal = 0,      /**< 普通公式 */
    XCellFormula_Shared = 1,      /**< 共享公式 */
    XCellFormula_Array = 2        /**< 数组公式 */
} XCellFormula_Type;

/**
 * @brief      XCellFormula 单元格公式结构体
 * @note       包含公式文本、类型和共享公式信息。
 *             对齐 QXlsx::CellFormula 全部功能。
 */
typedef struct XCellFormula
{
    XString* m_text;             /**< 公式文本（如 "SUM(A1:A10)"） */
    XCellFormula_Type m_type;    /**< 公式类型 */
    int m_sharedIndex;           /**< 共享公式索引（-1 表示非共享公式） */
    XCellReference m_reference;  /**< 共享公式的引用单元格 */
    XString* m_ca;               /**< 数组公式的单元格范围字符串 */
} XCellFormula;

/**
 * @brief      创建一个空的 XCellFormula 对象
 * @return     指向新创建的 XCellFormula 的指针，失败返回 NULL
 */
XCellFormula* XCellFormula_create(void);

/**
 * @brief      使用公式文本创建 XCellFormula 对象
 * @param text 公式文本
 * @return     指向新创建的 XCellFormula 的指针
 */
XCellFormula* XCellFormula_create_ex(const char* text);

/**
 * @brief      在堆上删除 XCellFormula 实例
 * @param self 待删除的指针
 */
void XCellFormula_delete(XCellFormula* self);

/**
 * @brief      判断是否有公式
 * @param self 指针
 * @return     有公式返回 true
 */
bool XCellFormula_isValid(const XCellFormula* self);

/**
 * @brief      获取公式文本
 * @param self 指针
 * @return     公式文本字符串
 */
const char* XCellFormula_text(const XCellFormula* self);

/**
 * @brief      设置公式文本
 * @param self 指针
 * @param text 公式文本
 */
void XCellFormula_setText(XCellFormula* self, const char* text);

/**
 * @brief      获取公式类型
 * @param self 指针
 * @return     公式类型
 */
XCellFormula_Type XCellFormula_type(const XCellFormula* self);

/**
 * @brief      设置公式类型
 * @param self 指针
 * @param type 公式类型
 */
void XCellFormula_setType(XCellFormula* self, XCellFormula_Type type);

#ifdef __cplusplus
}
#endif
#endif /* XCELLFORMULA_H */
