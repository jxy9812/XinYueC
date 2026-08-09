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
#include "XCellRange.h"

/**
 * @brief      公式类型枚举
 */
typedef enum XCellFormula_Type
{
    XCellFormula_Normal = 0,      /**< 普通公式 */
    XCellFormula_Array = 1,       /**< 数组公式 */
    XCellFormula_DataTable = 2,   /**< 数据表公式 */
    XCellFormula_Shared = 3       /**< 共享公式 */
} XCellFormula_Type;

/**
 * @brief      XCellFormula 单元格公式结构体
 * @note       包含公式文本、类型、引用范围和共享公式信息。
 *             对齐 QXlsx::CellFormula 全部功能。
 */
typedef struct XCellFormula
{
    XString* m_text;             /**< 公式文本（如 "SUM(A1:A10)"） */
    XCellFormula_Type m_type;    /**< 公式类型 */
    int m_sharedIndex;           /**< 共享公式索引（-1 表示非共享公式） */
    XCellRange m_reference;      /**< 公式引用范围（共享/数组公式的区域） */
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
XCellFormula* XCellFormula_create_ex(const XString* text);
/**
 * @brief 使用 UTF-8 文本创建公式对象。
 * @param text UTF-8 公式文本，可为 NULL。
 * @return 新公式对象；失败返回 NULL，调用方负责释放。
 */
XCellFormula* XCellFormula_create_ex_utf8(const char* text);

/**
 * @brief      使用公式文本和类型创建 XCellFormula 对象
 * @param text 公式文本
 * @param type 公式类型
 * @return     指向新创建的 XCellFormula 的指针
 */
XCellFormula* XCellFormula_create_typed(const XString* text, XCellFormula_Type type);

/**
 * @brief      使用公式文本、引用范围和类型创建 XCellFormula 对象
 * @param text 公式文本
 * @param ref  引用范围
 * @param type 公式类型
 * @return     指向新创建的 XCellFormula 的指针
 */
XCellFormula* XCellFormula_create_withRef(const XString* text, const XCellRange* ref, XCellFormula_Type type);

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
 * @brief      获取公式类型
 * @param self 指针
 * @return     公式类型
 */
XCellFormula_Type XCellFormula_formulaType(const XCellFormula* self);

/**
 * @brief      获取公式文本
 * @param self 指针
 * @return     公式文本字符串
 */
const XString* XCellFormula_formulaText(const XCellFormula* self);

/**
 * @brief      获取公式引用范围
 * @param self 指针
 * @return     引用范围
 */
XCellRange XCellFormula_reference(const XCellFormula* self);

/**
 * @brief      获取共享公式索引
 * @param self 指针
 * @return     共享公式索引（-1 表示非共享公式）
 */
int XCellFormula_sharedIndex(const XCellFormula* self);

/**
 * @brief      设置公式文本
 * @param self 指针
 * @param text 公式文本
 */
void XCellFormula_setText(XCellFormula* self, const XString* text);
/**
 * @brief 使用 UTF-8 文本设置公式内容。
 * @param self 公式对象指针。
 * @param text UTF-8 公式文本。
 */
void XCellFormula_setText_utf8(XCellFormula* self, const char* text);

/**
 * @brief      设置公式类型
 * @param self 指针
 * @param type 公式类型
 */
void XCellFormula_setType(XCellFormula* self, XCellFormula_Type type);

/**
 * @brief      设置引用范围
 * @param self 指针
 * @param ref  引用范围
 */
void XCellFormula_setReference(XCellFormula* self, const XCellRange* ref);

/**
 * @brief      设置共享公式索引
 * @param self  指针
 * @param index 共享公式索引
 */
void XCellFormula_setSharedIndex(XCellFormula* self, int index);

/**
 * @brief      复制公式对象
 * @param src  源指针
 * @return     新的 XCellFormula 指针
 */
XCellFormula* XCellFormula_copy(const XCellFormula* src);

/**
 * @brief      判断两个公式是否相等
 * @param a 公式 A
 * @param b 公式 B
 * @return    相等返回 true
 */
bool XCellFormula_equals(const XCellFormula* a, const XCellFormula* b);

/**
 * @brief      判断两个公式是否不相等
 * @param a 公式 A
 * @param b 公式 B
 * @return    不相等返回 true
 */
bool XCellFormula_notEquals(const XCellFormula* a, const XCellFormula* b);

#ifdef __cplusplus
}
#endif
#endif /* XCELLFORMULA_H */
