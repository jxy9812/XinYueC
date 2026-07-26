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
    XString* m_relationshipId;     /**< 加载 XML 时的关系 ID（内部使用） */
    XString* m_display;            /**< 显示文本 */
    XString* m_tip;                /**< 提示文本 */
} XWorksheet_Hyperlink;

/** @brief 工作表中图片的单元格锚点位置 */
typedef struct XWorksheet_ImagePosition {
    int m_row;                     /**< 图片所在行（从 1 开始） */
    int m_column;                  /**< 图片所在列（从 1 开始） */
} XWorksheet_ImagePosition;

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
    XVector* m_imagePositions;     /**< 图片位置列表 (XWorksheet_ImagePosition)，与 m_mediaFiles 同索引 */
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
/**
 * @brief 创建工作表对象。
 * @param sheetName 工作表名称；函数会复制该名称。
 * @param sheetId   工作表 ID。
 * @param book      所属工作簿指针；由调用方或工作簿持有。
 * @param flag      创建标志，区分新建和从已有文件加载。
 * @return 成功返回工作表指针；失败返回 NULL。
 */
XWorksheet* XWorksheet_create(const XString* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief 销毁工作表并释放其单元格、格式、图片、图表等资源。
 * @param self 待销毁的工作表指针；允许为 NULL。
 */
void XWorksheet_delete(XWorksheet* self);

/**
 * @brief 深拷贝工作表。
 * @param self     源工作表指针。
 * @param distName 副本名称；函数会复制该名称。
 * @param distId   副本工作表 ID。
 * @return 新工作表指针；失败返回 NULL，调用方负责释放。
 */
XWorksheet* XWorksheet_copy(const XWorksheet* self, const XString* distName, int distId);

/* ========== 单元格写入 ========== */
/**
 * @brief 写入任意类型单元格值。
 * @param self    工作表指针。
 * @param row     行号，从 1 开始。
 * @param column  列号，从 1 开始。
 * @param value   待写入值；函数只读取，不接管所有权。
 * @param format  可选格式；传 NULL 使用默认格式，函数不接管所有权。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_write(XWorksheet* self, int row, int column, const XVariant* value, const XFormat* format);

/**
 * @brief 按单元格引用写入任意类型值。
 * @param self 工作表指针。
 * @param cell 单元格引用；函数只读取，不接管所有权。
 * @param value 待写入值；函数只读取，不接管所有权。
 * @param format 可选格式；传 NULL 使用默认格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeRef(XWorksheet* self, const XCellReference* cell, const XVariant* value, const XFormat* format);

/**
 * @brief 写入字符串单元格。
 * @param self 工作表指针；@param row 行号，从 1 开始；@param column 列号，从 1 开始。
 * @param value 字符串对象；@param format 可选格式，传 NULL 使用默认格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeString(XWorksheet* self, int row, int column, const XString* value, const XFormat* format);

/**
 * @brief 使用 UTF-8 文本写入字符串单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param value UTF-8 文本；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeString_utf8(XWorksheet* self, int row, int column, const char* value, const XFormat* format);

/**
 * @brief 按引用写入字符串单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param value 字符串对象；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeStringRef(XWorksheet* self, const XCellReference* cell, const XString* value, const XFormat* format);

/**
 * @brief 写入富文本单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param value 富文本对象；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeRichString(XWorksheet* self, int row, int column, const XRichString* value, const XFormat* format);

/**
 * @brief 按引用写入富文本单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param value 富文本对象；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeRichStringRef(XWorksheet* self, const XCellReference* cell, const XRichString* value, const XFormat* format);

/**
 * @brief 写入内联字符串单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param value 字符串对象；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeInlineString(XWorksheet* self, int row, int column, const XString* value, const XFormat* format);

/**
 * @brief 按引用写入内联字符串单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param value 字符串对象；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeInlineStringRef(XWorksheet* self, const XCellReference* cell, const XString* value, const XFormat* format);

/**
 * @brief 写入数值单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param value 数值；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeNumeric(XWorksheet* self, int row, int column, double value, const XFormat* format);

/**
 * @brief 按引用写入数值单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param value 数值；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeNumericRef(XWorksheet* self, const XCellReference* cell, double value, const XFormat* format);

/**
 * @brief 写入公式单元格及缓存结果。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param formula 公式对象；@param format 可选格式；@param result 缓存数值结果。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeFormula(XWorksheet* self, int row, int column, const XCellFormula* formula, const XFormat* format, double result);

/**
 * @brief 按引用写入公式单元格及缓存结果。
 * @param self 工作表指针；@param cell 单元格引用；@param formula 公式对象；@param format 可选格式；@param result 缓存数值结果。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeFormulaRef(XWorksheet* self, const XCellReference* cell, const XCellFormula* formula, const XFormat* format, double result);

/**
 * @brief 写入空白单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeBlank(XWorksheet* self, int row, int column, const XFormat* format);

/**
 * @brief 按引用写入空白单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeBlankRef(XWorksheet* self, const XCellReference* cell, const XFormat* format);

/**
 * @brief 写入布尔单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param value 布尔值；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeBool(XWorksheet* self, int row, int column, bool value, const XFormat* format);

/**
 * @brief 按引用写入布尔单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param value 布尔值；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeBoolRef(XWorksheet* self, const XCellReference* cell, bool value, const XFormat* format);

/**
 * @brief 按毫秒时间戳写入日期时间单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param timestampMs Unix 毫秒时间戳；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeDateTime(XWorksheet* self, int row, int column, int64_t timestampMs, const XFormat* format);

/**
 * @brief 按引用写入日期时间单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param timestampMs Unix 毫秒时间戳；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeDateTimeRef(XWorksheet* self, const XCellReference* cell, int64_t timestampMs, const XFormat* format);

/**
 * @brief 写入日期单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param year 年；@param month 月；@param day 日；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeDate(XWorksheet* self, int row, int column, int year, int month, int day, const XFormat* format);

/**
 * @brief 按引用写入日期单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param year 年；@param month 月；@param day 日；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeDateRef(XWorksheet* self, const XCellReference* cell, int year, int month, int day, const XFormat* format);

/**
 * @brief 写入时间单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param hour 时；@param minute 分；@param second 秒，可带小数；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeTime(XWorksheet* self, int row, int column, int hour, int minute, double second, const XFormat* format);

/**
 * @brief 按引用写入时间单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param hour 时；@param minute 分；@param second 秒，可带小数；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeTimeRef(XWorksheet* self, const XCellReference* cell, int hour, int minute, double second, const XFormat* format);

/**
 * @brief 写入超链接单元格。
 * @param self 工作表指针；@param row 行号；@param column 列号；@param url 链接地址；@param format 可选格式；@param display 显示文本，可为 NULL；@param tip 提示文本，可为 NULL。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeHyperlink(XWorksheet* self, int row, int column, const XString* url, const XFormat* format, const XString* display, const XString* tip);

/**
 * @brief 按引用写入超链接单元格。
 * @param self 工作表指针；@param cell 单元格引用；@param url 链接地址；@param format 可选格式；@param display 显示文本，可为 NULL；@param tip 提示文本，可为 NULL。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_writeHyperlinkRef(XWorksheet* self, const XCellReference* cell, const XString* url, const XFormat* format, const XString* display, const XString* tip);

/* ========== 单元格读取 ========== */
/**
 * @brief 获取指定位置的单元格对象。
 * @param self 工作表指针；@param row 行号，从 1 开始；@param column 列号，从 1 开始。
 * @return 工作表内部持有的单元格指针；不存在返回 NULL，调用方不得释放。
 */
XCell* XWorksheet_cellAt(XWorksheet* self, int row, int column);

/**
 * @brief 按引用获取单元格对象。
 * @param self 工作表指针；@param cell 单元格引用，函数只读取不接管所有权。
 * @return 工作表内部持有的单元格指针；不存在返回 NULL，调用方不得释放。
 */
XCell* XWorksheet_cellAtRef(XWorksheet* self, const XCellReference* cell);

/**
 * @brief 读取指定位置的单元格值。
 * @param self 工作表指针；@param row 行号，从 1 开始；@param column 列号，从 1 开始。
 * @return 新创建的 XVariant；读取失败返回 NULL，调用方负责释放。
 */
XVariant* XWorksheet_read(XWorksheet* self, int row, int column);

/**
 * @brief 按引用读取单元格值。
 * @param self 工作表指针；@param cell 单元格引用，函数只读取不接管所有权。
 * @return 新创建的 XVariant；读取失败返回 NULL，调用方负责释放。
 */
XVariant* XWorksheet_readRef(XWorksheet* self, const XCellReference* cell);

/* ========== 数据验证与条件格式 ========== */
/**
 * @brief 添加数据验证规则。
 * @param self 工作表指针。
 * @param validation 数据验证对象；成功后工作表接管所有权，失败时调用方负责释放。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XWorksheet_addDataValidation(XWorksheet* self, XDataValidation* validation);

/**
 * @brief 添加条件格式规则。
 * @param self 工作表指针。
 * @param cf 条件格式对象；成功后工作表接管所有权，失败时调用方负责释放。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XWorksheet_addConditionalFormatting(XWorksheet* self, XConditionalFormatting* cf);

/* ========== 图片与图表 ========== */
/**
 * @brief 将图片插入指定单元格锚点。
 * @param self 工作表指针；@param row 锚点行号；@param column 锚点列号；@param imagePath 图片路径，函数只读取不接管所有权。
 * @return 图片索引，从 0 开始；失败返回 -1。
 */
int XWorksheet_insertImage(XWorksheet* self, int row, int column, const XString* imagePath);

/**
 * @brief 按图片索引读取图片字节。
 * @param self 工作表指针；@param imageIndex 图片索引，从 0 开始；@param imgData 输出字节数组。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_getImage(XWorksheet* self, int imageIndex, XByteArray* imgData);

/**
 * @brief 按图片锚点位置读取图片字节。
 * @param self 工作表指针；@param row 锚点行号；@param column 锚点列号；@param imgData 输出字节数组。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_getImageAt(XWorksheet* self, int row, int column, XByteArray* imgData);

/**
 * @brief 获取工作表图片数量。
 * @param self 工作表指针。
 * @return 图片数量；无效指针返回 0。
 */
unsigned int XWorksheet_getImageCount(const XWorksheet* self);

/**
 * @brief 在指定位置插入图表。
 * @param self 工作表指针；@param row 锚点行号；@param column 锚点列号；@param width 图表宽度；@param height 图表高度。
 * @return 新图表指针；失败返回 NULL；图表由工作表持有。
 */
XChart* XWorksheet_insertChart(XWorksheet* self, int row, int column, int width, int height);

/* ========== 合并单元格 ========== */
/**
 * @brief 合并矩形单元格区域。
 * @param self 工作表指针；@param firstRow 起始行；@param firstCol 起始列；@param lastRow 结束行；@param lastCol 结束列；@param format 可选格式。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_mergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format);

/**
 * @brief 取消矩形单元格区域合并。
 * @param self 工作表指针；@param firstRow 起始行；@param firstCol 起始列；@param lastRow 结束行；@param lastCol 结束列。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_unmergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol);

/**
 * @brief 获取合并区域列表。
 * @param self 工作表指针。
 * @param count 输出区域数量指针，可为 NULL。
 * @return 新分配的 XCellRange 数组；失败返回 NULL，调用方负责释放数组。
 */
XCellRange* XWorksheet_mergedCells(const XWorksheet* self, int* count);

/* ========== 列操作 ========== */
/**
 * @brief 设置列宽。
 * @param self 工作表指针；@param colFirst 起始列；@param colLast 结束列；@param width 列宽值。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setColumnWidth(XWorksheet* self, int colFirst, int colLast, double width);

/**
 * @brief 设置列范围格式。
 * @param self 工作表指针；@param colFirst 起始列；@param colLast 结束列；@param format 要应用的格式，不接管所有权。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setColumnFormat(XWorksheet* self, int colFirst, int colLast, const XFormat* format);

/**
 * @brief 设置列范围隐藏状态。
 * @param self 工作表指针；@param colFirst 起始列；@param colLast 结束列；@param hidden true 隐藏，false 显示。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setColumnHidden(XWorksheet* self, int colFirst, int colLast, bool hidden);

/**
 * @brief 获取列宽。
 * @param self 工作表指针；@param column 列号，从 1 开始。
 * @return 列宽；无效参数返回 -1.0。
 */
double XWorksheet_columnWidth(const XWorksheet* self, int column);

/**
 * @brief 获取列格式副本。
 * @param self 工作表指针；@param column 列号，从 1 开始。
 * @return 新格式对象；失败返回 NULL，调用方负责释放。
 */
XFormat* XWorksheet_columnFormat(const XWorksheet* self, int column);

/**
 * @brief 查询列是否隐藏。
 * @param self 工作表指针；@param column 列号，从 1 开始。
 * @return 隐藏返回 true，否则返回 false。
 */
bool XWorksheet_isColumnHidden(const XWorksheet* self, int column);

/**
 * @brief 设置列范围分组折叠状态。
 * @param self 工作表指针；@param colFirst 起始列；@param colLast 结束列；@param collapsed true 折叠，false 展开。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_groupColumns(XWorksheet* self, int colFirst, int colLast, bool collapsed);

/**
 * @brief 按范围对象设置列分组折叠状态。
 * @param self 工作表指针；@param range 列范围对象；@param collapsed true 折叠，false 展开。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_groupColumnsRange(XWorksheet* self, const XCellRange* range, bool collapsed);

/* ========== 行操作 ========== */
/**
 * @brief 设置行高。
 * @param self 工作表指针；@param rowFirst 起始行；@param rowLast 结束行；@param height 行高值。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setRowHeight(XWorksheet* self, int rowFirst, int rowLast, double height);

/**
 * @brief 设置行范围格式。
 * @param self 工作表指针；@param rowFirst 起始行；@param rowLast 结束行；@param format 要应用的格式，不接管所有权。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setRowFormat(XWorksheet* self, int rowFirst, int rowLast, const XFormat* format);

/**
 * @brief 设置行范围隐藏状态。
 * @param self 工作表指针；@param rowFirst 起始行；@param rowLast 结束行；@param hidden true 隐藏，false 显示。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_setRowHidden(XWorksheet* self, int rowFirst, int rowLast, bool hidden);

/**
 * @brief 获取行高。
 * @param self 工作表指针；@param row 行号，从 1 开始。
 * @return 行高；无效参数返回 -1.0。
 */
double XWorksheet_rowHeight(const XWorksheet* self, int row);

/**
 * @brief 获取行格式副本。
 * @param self 工作表指针；@param row 行号，从 1 开始。
 * @return 新格式对象；失败返回 NULL，调用方负责释放。
 */
XFormat* XWorksheet_rowFormat(const XWorksheet* self, int row);

/**
 * @brief 查询行是否隐藏。
 * @param self 工作表指针；@param row 行号，从 1 开始。
 * @return 隐藏返回 true，否则返回 false。
 */
bool XWorksheet_isRowHidden(const XWorksheet* self, int row);

/**
 * @brief 设置行范围分组折叠状态。
 * @param self 工作表指针；@param rowFirst 起始行；@param rowLast 结束行；@param collapsed true 折叠，false 展开。
 * @return 成功返回 true，否则返回 false。
 */
bool XWorksheet_groupRows(XWorksheet* self, int rowFirst, int rowLast, bool collapsed);

/* ========== 属性 ========== */
/**
 * @brief 获取工作表实际使用区域。
 * @param self 工作表指针。
 * @return 使用区域值对象；无效工作表返回无效范围。
 */
XCellRange XWorksheet_dimension(const XWorksheet* self);

/**
 * @brief 查询窗口保护状态。
 * @param self 工作表指针。
 * @return 已保护返回 true，否则返回 false。
 */
bool XWorksheet_isWindowProtected(const XWorksheet* self);

/**
 * @brief 设置窗口保护状态。
 * @param self    工作表指针。
 * @param protect true 启用保护，false 取消保护。
 */
void XWorksheet_setWindowProtected(XWorksheet* self, bool protect);

/**
 * @brief 查询是否显示公式。
 * @param self 工作表指针。
 * @return 显示公式返回 true，否则返回 false。
 */
bool XWorksheet_isFormulasVisible(const XWorksheet* self);

/**
 * @brief 设置公式显示状态。
 * @param self    工作表指针。
 * @param visible true 显示公式，false 显示计算结果。
 */
void XWorksheet_setFormulasVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否显示网格线。
 * @param self 工作表指针。
 * @return 显示网格线返回 true，否则返回 false。
 */
bool XWorksheet_isGridLinesVisible(const XWorksheet* self);

/**
 * @brief 设置网格线显示状态。
 * @param self    工作表指针。
 * @param visible true 显示网格线，false 隐藏网格线。
 */
void XWorksheet_setGridLinesVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否显示行列标题。
 * @param self 工作表指针。
 * @return 显示行列标题返回 true，否则返回 false。
 */
bool XWorksheet_isRowColumnHeadersVisible(const XWorksheet* self);

/**
 * @brief 设置行列标题显示状态。
 * @param self    工作表指针。
 * @param visible true 显示行列标题，false 隐藏行列标题。
 */
void XWorksheet_setRowColumnHeadersVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否显示零值。
 * @param self 工作表指针。
 * @return 显示零值返回 true，否则返回 false。
 */
bool XWorksheet_isZerosVisible(const XWorksheet* self);

/**
 * @brief 设置零值显示状态。
 * @param self    工作表指针。
 * @param visible true 显示零值，false 隐藏零值。
 */
void XWorksheet_setZerosVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否采用从右到左布局。
 * @param self 工作表指针。
 * @return 从右到左布局返回 true，否则返回 false。
 */
bool XWorksheet_isRightToLeft(const XWorksheet* self);

/**
 * @brief 设置工作表从右到左布局。
 * @param self   工作表指针。
 * @param enable true 启用从右到左布局，false 使用从左到右布局。
 */
void XWorksheet_setRightToLeft(XWorksheet* self, bool enable);

/**
 * @brief 查询工作表标签是否选中。
 * @param self 工作表指针。
 * @return 已选中返回 true，否则返回 false。
 */
bool XWorksheet_isSelected(const XWorksheet* self);

/**
 * @brief 设置工作表标签选中状态。
 * @param self   工作表指针。
 * @param select true 选中，false 取消选中。
 */
void XWorksheet_setSelected(XWorksheet* self, bool select);

/**
 * @brief 查询是否显示标尺。
 * @param self 工作表指针。
 * @return 显示标尺返回 true，否则返回 false。
 */
bool XWorksheet_isRulerVisible(const XWorksheet* self);

/**
 * @brief 设置标尺显示状态。
 * @param self    工作表指针。
 * @param visible true 显示标尺，false 隐藏标尺。
 */
void XWorksheet_setRulerVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否显示大纲符号。
 * @param self 工作表指针。
 * @return 显示大纲符号返回 true，否则返回 false。
 */
bool XWorksheet_isOutlineSymbolsVisible(const XWorksheet* self);

/**
 * @brief 设置大纲符号显示状态。
 * @param self    工作表指针。
 * @param visible true 显示大纲符号，false 隐藏大纲符号。
 */
void XWorksheet_setOutlineSymbolsVisible(XWorksheet* self, bool visible);

/**
 * @brief 查询是否显示打印空白区域。
 * @param self 工作表指针。
 * @return 显示空白区域返回 true，否则返回 false。
 */
bool XWorksheet_isWhiteSpaceVisible(const XWorksheet* self);

/**
 * @brief 设置打印空白区域显示状态。
 * @param self    工作表指针。
 * @param visible true 显示空白区域，false 隐藏空白区域。
 */
void XWorksheet_setWhiteSpaceVisible(XWorksheet* self, bool visible);

/**
 * @brief 设置工作表起始页码。
 * @param self   工作表指针。
 * @param spagen 起始页码。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XWorksheet_setStartPage(XWorksheet* self, int spagen);

/* ========== 全单元格获取 ========== */
/**
 * @brief 获取工作表中的全部单元格位置。
 * @param self      工作表指针。
 * @param locations 输出位置数组地址；成功后由调用方释放。
 * @param maxRow    输出最大行号，可为 NULL。
 * @param maxCol    输出最大列号，可为 NULL。
 * @return 单元格数量；失败返回负数。
 */
int XWorksheet_getFullCells(const XWorksheet* self, XCellLocation** locations, int* maxRow, int* maxCol);

/* ========== XML 读写 ========== */
/**
 * @brief 将工作表 XML 保存到文件。
 * @param self     工作表指针。
 * @param filePath 目标 XML 文件路径。
 * @return 保存成功返回 true，否则返回 false。
 */
bool XWorksheet_saveToXmlFile(XWorksheet* self, const XString* filePath);

/**
 * @brief 从 XML 文件加载工作表。
 * @param self     工作表指针。
 * @param filePath XML 文件路径。
 * @return 加载成功返回 true，否则返回 false。
 */
bool XWorksheet_loadFromXmlFile(XWorksheet* self, const XString* filePath);

/**
 * @brief 将工作表序列化为 XML 字节数组。
 * @param self    工作表指针。
 * @param outData 输出数据指针；成功后由调用方使用 XFree_System 释放。
 * @param outLen  输出数据长度指针。
 * @return 序列化成功返回 true，否则返回 false。
 */
bool XWorksheet_saveToXmlData(const XWorksheet* self, uint8_t** outData, size_t* outLen);

/**
 * @brief 从定长 XML 字节数据加载工作表。
 * @param self 工作表指针。
 * @param data XML 数据地址。
 * @param len  XML 数据长度（字节），数据无需 NUL 终止。
 * @return 加载成功返回 true，否则返回 false。
 */
bool XWorksheet_loadFromXmlData(XWorksheet* self, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* XWORKSHEET_H */
