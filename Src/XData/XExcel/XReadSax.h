/******************************************************************************
 * @file       XReadSax.h
 * @brief      XReadSax SAX 流式单元格读取（对标 QXlsx::read_sax）
 * @author     XinYueC 团队
 * @note       提供基于 SAX 的流式单元格读取功能，避免加载整个文档到内存。
 *             对齐 QXlsx::read_sax 全部功能
 ******************************************************************************/
#ifndef XREADSAX_H
#define XREADSAX_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>#include <stdbool.h>
#include "XString.h"
#include "XCellLocation.h"

/** @brief SAX 读取选项结构体 */
typedef struct XReadSax_Options {
    int m_minRow;              /**< 最小行号，0表示不限制 */
    int m_maxRow;              /**< 最大行号，0表示不限制 */
    int m_minCol;              /**< 最小列号，0表示不限制 */
    int m_maxCol;              /**< 最大列号，0表示不限制 */
    bool m_skipEmptyRows;     /**< 跳过空行 */
    bool m_skipEmptyColumns;  /**< 跳过空列 */
} XReadSax_Options;

/** @brief SAX 单元格回调函数类型 */
typedef void (*XReadSax_CellCallback)(int row, int col, const char* value, const char* type, void* userData);

/** @brief 默认 SAX 选项 */
extern const XReadSax_Options XReadSax_DefaultOptions;

#ifdef __cplusplus
}
#endif
#endif
