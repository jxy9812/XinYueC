/******************************************************************************
 * @file       XCell.h
 * @brief      XCell 单元格类（对标 QXlsx::Cell）
 * @details    对齐 QXlsx::Cell；纯 C99 实现，仅依赖 XinYueC 库内组件
 *             （XString/XFormat/XCellFormula/XRichString 等）与白名单
 *             标准头，不调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 * @note       提供单元格数据管理，包括类型、值、格式、公式、富文本等。
 *             对齐 QXlsx::Cell 全部功能
 ******************************************************************************/
#ifndef XCELL_H
#define XCELL_H

#if XEXCEL_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XColor.h"
#include "XFont.h"
#include "XFormat.h"
#include "XCellFormula.h"
#include "XRichString.h"
#include "XCellReference.h"
#include "XSharedData.h"

/**
 * @brief      单元格类型枚举（参见 ECMA 376, 18.18.11. ST_CellType）
 */
typedef enum XCell_CellType
{
    XCell_BooleanType = 0,        /**< 布尔类型 */
    XCell_DateType = 1,           /**< 日期类型 */
    XCell_ErrorType = 2,          /**< 错误类型 */
    XCell_InlineStringType = 3,   /**< 内联字符串类型 */
    XCell_NumberType = 4,         /**< 数字类型 */
    XCell_SharedStringType = 5,   /**< 共享字符串类型 */
    XCell_StringType = 6,         /**< 字符串类型 */
    XCell_CustomType = 7          /**< 自定义/未定义类型 */
} XCell_CellType;

/**
 * @brief      XCell 单元格结构体
 * @note       提供单元格数据管理，包含类型、值、格式、公式、富文本等。
 *             对齐 QXlsx::Cell 全部功能。
 */
typedef struct XCell
{
    XString* m_value;              /**< 单元格值（字符串表示）；可为 NULL（未设置值时），由本对象拥有并在 XCell_delete 中释放；禁止调用者直接修改或释放 */
    XFormat* m_format;             /**< 单元格格式；可为 NULL，仅借用不归本对象拥有（由样式表管理），调用者不得释放 */
    XCellFormula* m_formula;       /**< 单元格公式（可选）；可为 NULL，由本对象拥有（setFormula 转移所有权）并在 XCell_delete 中释放 */
    XRichString* m_richString;     /**< 富文本字符串（可选）；可为 NULL，由本对象拥有（setRichString 转移所有权）并在 XCell_delete 中释放 */
    int m_styleNumber;             /**< 样式编号 */
    int m_row;                     /**< 行号（1 索引） */
    int m_column;                  /**< 列号（1 索引） */
    XCell_CellType m_cellType;     /**< 单元格类型 */
} XCell;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建一个空单元格
 * @return     指向新创建的 XCell 的指针，失败返回 NULL
 */
XCell* XCell_create(void);

/**
 * @brief      使用值、类型和格式创建单元格
 * @param value    单元格值（字符串表示）
 * @param type     单元格类型
 * @param format   单元格格式（可为 NULL）
 * @return     指向新创建的 XCell 的指针，失败返回 NULL
 */
XCell* XCell_create_ex(const XString* value, XCell_CellType type, XFormat* format);
/**
 * @brief 使用 UTF-8 文本创建指定类型的单元格。
 * @param value  UTF-8 单元格文本，可为 NULL。
 * @param type   单元格类型。
 * @param format 初始格式指针，可为 NULL；单元格按实现规则复制或引用格式。
 * @return 新单元格指针；失败返回 NULL，调用方负责释放。
 */
XCell* XCell_create_ex_utf8(const char* value, XCell_CellType type, XFormat* format);

/**
 * @brief      复制单元格
 * @param other 源单元格
 * @return     指向新创建的 XCell 的指针，失败返回 NULL
 */
XCell* XCell_copy(const XCell* other);

/**
 * @brief      删除单元格
 * @param self 待删除的指针
 */
void XCell_delete(XCell* self);

/* ========== 访问方法 ========== */

/**
 * @brief      获取单元格类型
 * @param self 指针
 * @return     单元格类型
 */
XCell_CellType XCell_cellType(const XCell* self);

/**
 * @brief      设置单元格类型
 * @param self 指针
 * @param type 单元格类型
 * @return     无。参数非法时保持原对象不变。
 */
void XCell_setCellType(XCell* self, XCell_CellType type);

/**
 * @brief      获取单元格值（字符串表示）
 * @param self 指针
 * @return     值字符串指针（借用内部对象，禁止释放或修改；对象销毁或
 *             setValue 后失效）；未设置值或 self 为 NULL 时返回 NULL
 * @warning    返回的 XString* 属于对象内部存储，不能调用 XClassDelete 释放。
 */
const XString* XCell_value(const XCell* self);

/**
 * @brief      设置单元格值
 * @param self  指针
 * @param value 值字符串
 * @return     无。参数非法时保持原对象不变。
 * @note       值会被复制；已有公式和富文本会被清除。
 */
void XCell_setValue(XCell* self, const XString* value);
/**
 * @brief 使用 UTF-8 文本设置单元格值。
 * @param self  单元格指针。
 * @param value UTF-8 文本，可为 NULL 表示空值。
 * @return 无。参数非法时保持原对象不变。
 */
void XCell_setValue_utf8(XCell* self, const char* value);

/**
 * @brief      获取单元格格式
 * @param self 指针
 * @return     指向 XFormat 的指针（借用，不归本对象拥有，禁止释放）；
 *             未设置格式或 self 为 NULL 时返回 NULL
 */
XFormat* XCell_format(const XCell* self);

/**
 * @brief      设置单元格格式
 * @param self   指针
 * @param format 格式指针
 * @return     无。参数非法时保持原对象不变。
 */
void XCell_setFormat(XCell* self, XFormat* format);

/**
 * @brief      判断是否有公式
 * @param self 指针
 * @return     有公式返回 true
 */
bool XCell_hasFormula(const XCell* self);

/**
 * @brief      获取公式
 * @param self 指针
 * @return     指向 XCellFormula 的指针（借用，由单元格拥有，禁止调用方
 *             释放），无公式返回 NULL
 */
XCellFormula* XCell_formula(const XCell* self);

/**
 * @brief      设置公式
 * @param self    指针
 * @param formula 公式指针；所有权转移给单元格，传入 NULL 可清除公式
 * @return     无。self 为 NULL 时保持原对象不变；formula 为 NULL 是合法参数，
 *             表示清除公式。
 */
void XCell_setFormula(XCell* self, XCellFormula* formula);

/**
 * @brief      判断是否为日期时间类型
 * @param self 指针
 * @return     是日期时间返回 true
 */
bool XCell_isDateTime(const XCell* self);

/**
 * @brief      获取日期时间值（毫秒时间戳）
 * @param self 指针
 * @param date1904 是否使用1904日期系统
 * @return     毫秒时间戳，非日期类型返回 0
 */
int64_t XCell_dateTime(const XCell* self, bool date1904);

/**
 * @brief      获取单元格的读取值（解析共享字符串等后的值）
 * @param self 指针
 * @return     解析后的值字符串指针（借用内部对象，禁止释放或修改）；
 *             未设置值或 self 为 NULL 时返回 NULL
 */
const XString* XCell_readValue(const XCell* self);

/**
 * @brief      判断是否为富文本
 * @param self 指针
 * @return     是富文本返回 true
 */
bool XCell_isRichString(const XCell* self);

/**
 * @brief      获取富文本字符串
 * @param self 指针
 * @return     指向 XRichString 的指针（借用，由单元格拥有，禁止调用方
 *             释放），无富文本返回 NULL
 */
XRichString* XCell_richString(const XCell* self);

/**
 * @brief      设置富文本字符串
 * @param self  指针
 * @param rich  富文本指针；所有权转移给单元格，传入 NULL 可清除富文本
 * @return     无。self 为 NULL 时保持原对象不变；rich 为 NULL 是合法参数，
 *             表示清除富文本。
 */
void XCell_setRichString(XCell* self, XRichString* rich);

/**
 * @brief      获取样式编号
 * @param self 指针
 * @return     样式编号
 */
int XCell_styleNumber(const XCell* self);

/**
 * @brief      设置样式编号
 * @param self  指针
 * @param style 样式编号
 * @return     无。参数非法时保持原对象不变。
 */
void XCell_setStyleNumber(XCell* self, int style);

/**
 * @brief      获取行号
 * @param self 指针
 * @return     行号（1 索引）
 */
int XCell_row(const XCell* self);

/**
 * @brief      设置行号
 * @param self 指针
 * @param row  行号（1 索引）
 * @return     无。参数非法时保持原对象不变。
 */
void XCell_setRow(XCell* self, int row);

/**
 * @brief      获取列号
 * @param self 指针
 * @return     列号（1 索引）
 */
int XCell_column(const XCell* self);

/**
 * @brief      设置列号
 * @param self   指针
 * @param column 列号（1 索引）
 * @return     无。参数非法时保持原对象不变。
 */
void XCell_setColumn(XCell* self, int column);

/* ========== 静态方法 ========== */

/**
 * @brief      判断单元格类型是否为日期类型
 * @param cellType 单元格类型
 * @param format   格式指针
 * @return     是日期类型返回 true
 */
bool XCell_isDateType(XCell_CellType cellType, const XFormat* format);

#ifdef __cplusplus
}
#endif

#endif /* XEXCEL_ON */

#endif /* XCELL_H */
