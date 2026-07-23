/******************************************************************************
 * @file       XWorksheet.h
 * @brief      XWorksheet 工作表类（对标 QXlsx::Worksheet）
 * @author     XinYueC 团队
 * @note       提供单元格数据管理、写入、读取、列/行操作、图片、图表、合并、数据验证等。
 *             对齐 QXlsx::Worksheet 全部功能
 ******************************************************************************/
#ifndef XWORKSHEET_H
#define XWORKSHEET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XMap.h"
#include "XColor.h"
#include "XFont.h"
#include "XAbstractSheet.h"
#include "XCell.h"
#include "XCellLocation.h"
#include "XCellRange.h"
#include "XCellReference.h"
#include "XCellFormula.h"
#include "XFormat.h"
#include "XRichString.h"
#include "XDataValidation.h"
#include "XConditionalFormatting.h"
#include "XChart.h"
#include "XMediaFile.h"

/* 前向声明 */
typedef struct XWorkbook XWorkbook;
typedef struct XDrawing XDrawing;

/** @brief 列信息结构体 */
typedef struct XWorksheet_ColumnInfo {
    double m_width;               /**< 列宽 */
    XFormat* m_format;             /**< 列格式 */
    bool m_hidden;                 /**< 是否隐藏 */
    int m_outlineLevel;            /**< 分组级别 */
    bool m_collapsed;              /**< 是否折叠 */
} XWorksheet_ColumnInfo;

/** @brief 行信息结构体 */
typedef struct XWorksheet_RowInfo {
    double m_height;               /**< 行高 */
    XFormat* m_format;             /**< 行格式 */
    bool m_hidden;                 /**< 是否隐藏 */
    int m_outlineLevel;            /**< 分组级别 */
    bool m_collapsed;              /**< 是否折叠 */
} XWorksheet_RowInfo;

/** @brief 超链接结构体 */
typedef struct XWorksheet_Hyperlink {
    XCellRange m_range;            /**< 超链接范围 */
    XString* m_url;                /**< URL */
    XString* m_display;            /**< 显示文本 */
    XString* m_tip;                /**< 提示文本 */
} XWorksheet_Hyperlink;

/** @brief XWorksheet 工作表结构体 */
typedef struct XWorksheet {
    XAbstractSheet m_base;         /**< 基类 */
    /* 单元格数据 */
    XMap* m_cellTable;             /**< 单元格表，key=(row<<16)|col */
    XMap* m_shallowCellTable;      /**< 浅拷贝单元格表 */
    /* 列信息 */
    XVector* m_colInfo;            /**< 列信息数组，索引从0开始 */
    XMap* m_colInfoMap;            /**< 列信息映射 */
    /* 行信息 */
    XMap* m_rowInfoMap;            /**< 行信息映射 */
    /* 合并单元格 */
    XVector* m_mergedCells;        /**< 合并单元格列表 (XCellRange) */
    /* 数据验证 */
    XVector* m_dataValidations;    /**< 数据验证列表 (XDataValidation*) */
    /* 条件格式 */
    XVector* m_conditionalFormatting; /**< 条件格式列表 (XConditionalFormatting*) */
    /* 超链接 */
    XVector* m_hyperlinks;         /**< 超链接列表 (XWorksheet_Hyperlink) */
    /* 图片和图表 */
    XVector* m_mediaFiles;         /**< 媒体文件列表 (XMediaFile*) */
    XVector* m_chartFiles;         /**< 图表文件列表 (XChart*) */
    /* 属性 */
    bool m_windowProtection;       /**< 窗口保护 */
    bool m_showFormulas;           /**< 显示公式 */
    bool m_showGridLines;          /**< 显示网格线 */
    bool m_showRowColHeaders;      /**< 显示行列标题 */
    bool m_showZeros;              /**< 显示零值 */
    bool m_rightToLeft;            /**< 从右到左 */
    bool m_tabSelected;            /**< 标签选中 */
    bool m_showRuler;              /**< 显示标尺 */
    bool m_showOutlineSymbols;     /**< 显示大纲符号 */
    bool m_showWhiteSpace;         /**< 显示空白 */
    int m_startPage;               /**< 起始页码 */
    XCellRange m_dimension;        /**< 维度范围 */
    XMap* m_rowSpans;              /**< 行跨度映射 */
} XWorksheet;

/* ========== 创建与初始化 ========== */
XWorksheet* XWorksheet_create(const char* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag);
void XWorksheet_delete(XWorksheet* self);
XWorksheet* XWorksheet_copy(const XWorksheet* self, const char* distName, int distId);

/* ========== 单元格写入 ========== */
bool XWorksheet_write(XWorksheet* self, int row, int column, const XVariant* value, const XFormat* format);
bool XWorksheet_writeRef(XWorksheet* self, const XCellReference* cell, const XVariant* value, const XFormat* format);

bool XWorksheet_writeString(XWorksheet* self, int row, int column, const char* value, const XFormat* format);
bool XWorksheet_writeStringRef(XWorksheet* self, const XCellReference* cell, const char* value, const XFormat* format);
bool XWorksheet_writeRichString(XWorksheet* self, int row, int column, const XRichString* value, const XFormat* format);
bool XWorksheet_writeRichStringRef(XWorksheet* self, const XCellReference* cell, const XRichString* value, const XFormat* format);

bool XWorksheet_writeInlineString(XWorksheet* self, int row, int column, const char* value, const XFormat* format);
bool XWorksheet_writeInlineStringRef(XWorksheet* self, const XCellReference* cell, const char* value, const XFormat* format);

bool XWorksheet_writeNumeric(XWorksheet* self, int row, int column, double value, const XFormat* format);
bool XWorksheet_writeNumericRef(XWorksheet* self, const XCellReference* cell, double value, const XFormat* format);

bool XWorksheet_writeFormula(XWorksheet* self, int row, int column, const XCellFormula* formula, const XFormat* format, double result);
bool XWorksheet_writeFormulaRef(XWorksheet* self, const XCellReference* cell, const XCellFormula* formula, const XFormat* format, double result);

bool XWorksheet_writeBlank(XWorksheet* self, int row, int column, const XFormat* format);
bool XWorksheet_writeBlankRef(XWorksheet* self, const XCellReference* cell, const XFormat* format);

bool XWorksheet_writeBool(XWorksheet* self, int row, int column, bool value, const XFormat* format);
bool XWorksheet_writeBoolRef(XWorksheet* self, const XCellReference* cell, bool value, const XFormat* format);

bool XWorksheet_writeDateTime(XWorksheet* self, int row, int column, int64_t timestampMs, const XFormat* format);
bool XWorksheet_writeDateTimeRef(XWorksheet* self, const XCellReference* cell, int64_t timestampMs, const XFormat* format);

bool XWorksheet_writeHyperlink(XWorksheet* self, int row, int column, const char* url, const XFormat* format, const char* display, const char* tip);
bool XWorksheet_writeHyperlinkRef(XWorksheet* self, const XCellReference* cell, const char* url, const XFormat* format, const char* display, const char* tip);

/* ========== 单元格读取 ========== */
XCell* XWorksheet_cellAt(XWorksheet* self, int row, int column);
XCell* XWorksheet_cellAtRef(XWorksheet* self, const XCellReference* cell);
XVariant* XWorksheet_read(XWorksheet* self, int row, int column);
XVariant* XWorksheet_readRef(XWorksheet* self, const XCellReference* cell);

/* ========== 数据验证与条件格式 ========== */
bool XWorksheet_addDataValidation(XWorksheet* self, XDataValidation* validation);
bool XWorksheet_addConditionalFormatting(XWorksheet* self, XConditionalFormatting* cf);

/* ========== 图片与图表 ========== */
int XWorksheet_insertImage(XWorksheet* self, int row, int column, const char* imagePath);
bool XWorksheet_getImage(XWorksheet* self, int imageIndex, XByteArray* imgData);
bool XWorksheet_getImageAt(XWorksheet* self, int row, int column, XByteArray* imgData);
uint XWorksheet_getImageCount(const XWorksheet* self);
XChart* XWorksheet_insertChart(XWorksheet* self, int row, int column, int width, int height);

/* ========== 合并单元格 ========== */
bool XWorksheet_mergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format);
bool XWorksheet_unmergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol);
XCellRange* XWorksheet_mergedCells(const XWorksheet* self, int* count);

/* ========== 列操作 ========== */
bool XWorksheet_setColumnWidth(XWorksheet* self, int colFirst, int colLast, double width);
bool XWorksheet_setColumnFormat(XWorksheet* self, int colFirst, int colLast, const XFormat* format);
bool XWorksheet_setColumnHidden(XWorksheet* self, int colFirst, int colLast, bool hidden);
double XWorksheet_columnWidth(const XWorksheet* self, int column);
XFormat* XWorksheet_columnFormat(const XWorksheet* self, int column);
bool XWorksheet_isColumnHidden(const XWorksheet* self, int column);
bool XWorksheet_groupColumns(XWorksheet* self, int colFirst, int colLast, bool collapsed);

/* ========== 行操作 ========== */
bool XWorksheet_setRowHeight(XWorksheet* self, int rowFirst, int rowLast, double height);
bool XWorksheet_setRowFormat(XWorksheet* self, int rowFirst, int rowLast, const XFormat* format);
bool XWorksheet_setRowHidden(XWorksheet* self, int rowFirst, int rowLast, bool hidden);
double XWorksheet_rowHeight(const XWorksheet* self, int row);
XFormat* XWorksheet_rowFormat(const XWorksheet* self, int row);
bool XWorksheet_isRowHidden(const XWorksheet* self, int row);
bool XWorksheet_groupRows(XWorksheet* self, int rowFirst, int rowLast, bool collapsed);

/* ========== 属性 ========== */
XCellRange XWorksheet_dimension(const XWorksheet* self);
bool XWorksheet_isWindowProtected(const XWorksheet* self);
void XWorksheet_setWindowProtected(XWorksheet* self, bool protect);
bool XWorksheet_isFormulasVisible(const XWorksheet* self);
void XWorksheet_setFormulasVisible(XWorksheet* self, bool visible);
bool XWorksheet_isGridLinesVisible(const XWorksheet* self);
void XWorksheet_setGridLinesVisible(XWorksheet* self, bool visible);
bool XWorksheet_isRowColumnHeadersVisible(const XWorksheet* self);
void XWorksheet_setRowColumnHeadersVisible(XWorksheet* self, bool visible);
bool XWorksheet_isZerosVisible(const XWorksheet* self);
void XWorksheet_setZerosVisible(XWorksheet* self, bool visible);
bool XWorksheet_isRightToLeft(const XWorksheet* self);
void XWorksheet_setRightToLeft(XWorksheet* self, bool enable);
bool XWorksheet_isSelected(const XWorksheet* self);
void XWorksheet_setSelected(XWorksheet* self, bool select);
bool XWorksheet_isRulerVisible(const XWorksheet* self);
void XWorksheet_setRulerVisible(XWorksheet* self, bool visible);
bool XWorksheet_isOutlineSymbolsVisible(const XWorksheet* self);
void XWorksheet_setOutlineSymbolsVisible(XWorksheet* self, bool visible);
bool XWorksheet_isWhiteSpaceVisible(const XWorksheet* self);
void XWorksheet_setWhiteSpaceVisible(XWorksheet* self, bool visible);
bool XWorksheet_setStartPage(XWorksheet* self, int spagen);

/* ========== 全单元格获取 ========== */
int XWorksheet_getFullCells(const XWorksheet* self, XCellLocation** locations, int* maxRow, int* maxCol);

/* ========== XML 读写 ========== */
bool XWorksheet_saveToXmlFile(XWorksheet* self, const char* filePath);
bool XWorksheet_loadFromXmlFile(XWorksheet* self, const char* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XWORKSHEET_H */
