/******************************************************************************
 * @file       XDocument.h
 * @brief      XDocument 文档主类（对标 QXlsx::Document）
 * @author     XinYueC 团队
 * @note       提供 XLSX 文档的创建、读写、保存、加载等操作。
 *             对齐 QXlsx::Document 全部功能
 ******************************************************************************/
#ifndef XDOCUMENT_H
#define XDOCUMENT_H
#ifdef __cplusplus
extern "C" {

/* ========== CSV 导出 ========== */
/**
 * @brief 将当前活动工作表导出为 CSV 文件。
 * @param self        XDocument 文档指针。
 * @param csvFileName 目标 CSV 文件路径；调用方持有，函数不会释放。
 * @return 导出成功返回 true；文档、活动工作表或文件路径无效时返回 false。
 * @note 以当前活动工作表为数据源，使用逗号分隔字段；该操作是同步完成的。
 */
bool XDocument_saveAsCsv(const XDocument* self, const XString* csvFileName);
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XColor.h"
#include "XFont.h"
#include "XFormat.h"
#include "XCell.h"
#include "XCellRange.h"
#include "XCellReference.h"
#include "XCellFormula.h"
#include "XAbstractSheet.h"
#include "XWorksheet.h"
#include "XWorkbook.h"
#include "XDataValidation.h"
#include "XConditionalFormatting.h"
#include "XChart.h"
#include "XDocPropsApp.h"
#include "XDocPropsCore.h"
#include "XZipReader.h"
#include "XZipWriter.h"
#include "XReadSax.h"
#include "XMediaFile.h"

/* 前向声明 */
typedef struct XChartsheet XChartsheet;

/** @brief XDocument 文档主结构体 */
typedef struct XDocument {
    XWorkbook* m_workbook;              /**< 工作簿 */
    XDocPropsApp* m_docPropsApp;        /**< 应用程序属性 */
    XDocPropsCore* m_docPropsCore;      /**< 核心属性 */
    XString* m_filePath;                /**< 文件路径 */
    XByteArray* m_packageData;          /**< 从设备加载时保留的 XLSX 原始数据 */
    bool m_isLoaded;                    /**< 是否已加载 */
    bool m_isModified;                  /**< 是否已修改 */
} XDocument;

/* ========== 创建与初始化 ========== */
/**
 * @brief 创建一个新的 XLSX 文档，并建立默认工作簿和工作表。
 * @return 成功返回新文档指针；内存分配失败返回 NULL。
 * @note 返回对象由调用方负责调用 XDocument_delete 释放。
 */
XDocument* XDocument_create(void);

/**
 * @brief 从 XLSX 文件创建并加载文档。
 * @param xlsxName XLSX 文件路径；函数只读取该字符串，不接管所有权。
 * @return 加载成功返回文档指针；文件不存在、格式无效或解析失败返回 NULL。
 * @note 返回对象由调用方负责释放，加载过程同步完成。
 */
XDocument* XDocument_createFromFile(const XString* xlsxName);

/**
 * @brief 从 XIODevice 读取并加载 XLSX 文档。
 * @param device 输入设备指针；设备由调用方持有，函数不会销毁；未打开时函数会临时打开并在读取后关闭。
 * @return 加载成功返回文档指针；设备不可读、数据为空或 XLSX 无效返回 NULL。
 * @note 函数会从设备当前位置读取数据，读取过程同步完成。
 */
XDocument* XDocument_createFromDevice(struct XIODevice* device);

/**
 * @brief 销毁文档并释放工作簿、属性、缓存数据等所有内部资源。
 * @param self 待销毁的文档指针；允许为 NULL。
 */
void XDocument_delete(XDocument* self);

/* ========== 单元格写入 ========== */
/**
 * @brief 向当前工作表指定单元格写入值和可选格式。
 * @param self   XDocument 文档指针。
 * @param row    行号，从 1 开始。
 * @param col    列号，从 1 开始。
 * @param value  待写入值；函数只读取该对象，不接管所有权。
 * @param format 可选单元格格式；传 NULL 使用默认格式，函数不接管所有权。
 * @return 写入成功返回 true；坐标、文档或当前工作表无效返回 false。
 */
bool XDocument_write(XDocument* self, int row, int col, const XVariant* value, const XFormat* format);

/**
 * @brief 按单元格引用向当前工作表写入值和可选格式。
 * @param self   XDocument 文档指针。
 * @param cell   单元格引用；函数只读取该对象，不接管所有权。
 * @param value  待写入值；函数只读取该对象，不接管所有权。
 * @param format 可选单元格格式；传 NULL 使用默认格式，函数不接管所有权。
 * @return 写入成功返回 true；引用、文档或当前工作表无效返回 false。
 */
bool XDocument_writeRef(XDocument* self, const XCellReference* cell, const XVariant* value, const XFormat* format);

/* ========== 单元格读取 ========== */
/**
 * @brief 读取当前工作表指定单元格的值。
 * @param self XDocument 文档指针。
 * @param row  行号，从 1 开始。
 * @param col  列号，从 1 开始。
 * @return 新创建的 XVariant 指针；单元格为空、坐标无效或读取失败返回 NULL；返回对象由调用方释放。
 */
XVariant* XDocument_read(const XDocument* self, int row, int col);

/**
 * @brief 按单元格引用读取当前工作表的值。
 * @param self XDocument 文档指针。
 * @param cell 单元格引用；函数只读取该对象，不接管所有权。
 * @return 新创建的 XVariant 指针；读取失败返回 NULL，调用方负责释放。
 */
XVariant* XDocument_readRef(const XDocument* self, const XCellReference* cell);

/**
 * @brief 获取当前工作表指定位置的单元格对象。
 * @param self XDocument 文档指针。
 * @param row  行号，从 1 开始。
 * @param col  列号，从 1 开始。
 * @return 文档内部持有的 XCell 指针；不存在或坐标无效返回 NULL；调用方不得释放。
 */
XCell* XDocument_cellAt(const XDocument* self, int row, int col);

/**
 * @brief 按引用获取当前工作表的单元格对象。
 * @param self XDocument 文档指针。
 * @param cell 单元格引用；函数只读取该对象，不接管所有权。
 * @return 文档内部持有的 XCell 指针；不存在返回 NULL；调用方不得释放。
 */
XCell* XDocument_cellAtRef(const XDocument* self, const XCellReference* cell);

/* ========== 图片 ========== */
/**
 * @brief 将图片插入当前工作表指定单元格位置。
 * @param self      XDocument 文档指针。
 * @param row       锚点行号，从 1 开始。
 * @param col       锚点列号，从 1 开始。
 * @param imagePath 图片文件路径；函数只读取该字符串，不接管所有权。
 * @return 图片在当前工作表中的索引；失败返回 -1。
 */
int XDocument_insertImage(XDocument* self, int row, int col, const XString* imagePath);

/**
 * @brief 使用 UTF-8 路径将图片插入当前工作表。
 * @param self      XDocument 文档指针。
 * @param row       锚点行号，从 1 开始。
 * @param col       锚点列号，从 1 开始。
 * @param imagePath UTF-8 编码的图片文件路径，可为 NULL。
 * @return 图片索引；失败返回 -1。
 */
int XDocument_insertImage_utf8(XDocument* self, int row, int col, const char* imagePath);

/**
 * @brief 按图片索引读取图片字节。
 * @param self      XDocument 文档指针。
 * @param imageIndex 图片索引，从 0 开始。
 * @param imgData   输出字节数组；函数会写入或替换其内容，调用方负责传入已初始化对象。
 * @return 读取成功返回 true；索引或输出对象无效返回 false。
 */
bool XDocument_getImage(const XDocument* self, int imageIndex, XByteArray* imgData);

/**
 * @brief 按图片锚点位置读取图片字节。
 * @param self    XDocument 文档指针。
 * @param row     锚点行号，从 1 开始。
 * @param col     锚点列号，从 1 开始。
 * @param imgData 输出字节数组；函数会写入或替换其内容，调用方负责传入已初始化对象。
 * @return 找到并读取成功返回 true，否则返回 false。
 */
bool XDocument_getImageAt(const XDocument* self, int row, int col, XByteArray* imgData);

/**
 * @brief 获取当前工作表中的图片数量。
 * @param self XDocument 文档指针。
 * @return 图片数量；文档或当前工作表无效返回 0。
 */
unsigned int XDocument_getImageCount(const XDocument* self);

/* ========== 图表 ========== */
/**
 * @brief 在当前工作表中插入图表锚点并创建图表对象。
 * @param self   XDocument 文档指针。
 * @param row    锚点行号，从 1 开始。
 * @param col    锚点列号，从 1 开始。
 * @param width  图表宽度，使用工作表坐标单位。
 * @param height 图表高度，使用工作表坐标单位。
 * @return 新创建的图表指针；失败返回 NULL；图表由工作表/文档持有。
 */
XChart* XDocument_insertChart(XDocument* self, int row, int col, int width, int height);

/* ========== 合并单元格 ========== */
/**
 * @brief 合并当前工作表的矩形单元格区域。
 * @param self      XDocument 文档指针。
 * @param firstRow  起始行号，从 1 开始。
 * @param firstCol  起始列号，从 1 开始。
 * @param lastRow   结束行号，必须不小于起始行号。
 * @param lastCol   结束列号，必须不小于起始列号。
 * @param format    可选合并区域格式；传 NULL 保持现有格式，函数不接管所有权。
 * @return 合并成功返回 true，否则返回 false。
 */
bool XDocument_mergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format);

/**
 * @brief 取消当前工作表指定矩形区域的合并。
 * @param self      XDocument 文档指针。
 * @param firstRow  起始行号，从 1 开始。
 * @param firstCol  起始列号，从 1 开始。
 * @param lastRow   结束行号。
 * @param lastCol   结束列号。
 * @return 取消成功返回 true，否则返回 false。
 */
bool XDocument_unmergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol);

/* ========== 列操作 ========== */
/**
 * @brief 设置当前工作表一段列范围的列宽。
 * @param self     XDocument 文档指针。
 * @param colFirst 起始列号，从 1 开始。
 * @param colLast  结束列号，必须不小于起始列号。
 * @param width    列宽值；通常以字符宽度为单位。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setColumnWidth(XDocument* self, int colFirst, int colLast, double width);

/**
 * @brief 设置当前工作表一段列范围的格式。
 * @param self     XDocument 文档指针。
 * @param colFirst 起始列号，从 1 开始。
 * @param colLast  结束列号。
 * @param format   要应用的格式；函数只读取对象，不接管所有权。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setColumnFormat(XDocument* self, int colFirst, int colLast, const XFormat* format);

/**
 * @brief 设置当前工作表一段列范围的隐藏状态。
 * @param self     XDocument 文档指针。
 * @param colFirst 起始列号，从 1 开始。
 * @param colLast  结束列号。
 * @param hidden   true 表示隐藏，false 表示显示。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setColumnHidden(XDocument* self, int colFirst, int colLast, bool hidden);

/**
 * @brief 获取指定列的列宽。
 * @param self   XDocument 文档指针。
 * @param column 列号，从 1 开始。
 * @return 列宽；文档或列号无效返回 -1.0。
 */
double XDocument_columnWidth(const XDocument* self, int column);

/**
 * @brief 获取指定列的格式副本。
 * @param self   XDocument 文档指针。
 * @param column 列号，从 1 开始。
 * @return 新创建的格式对象；失败返回 NULL，调用方负责释放。
 */
XFormat* XDocument_columnFormat(const XDocument* self, int column);

/**
 * @brief 查询指定列是否隐藏。
 * @param self   XDocument 文档指针。
 * @param column 列号，从 1 开始。
 * @return 已隐藏返回 true；无效参数或未隐藏返回 false。
 */
bool XDocument_isColumnHidden(const XDocument* self, int column);

/* ========== 行操作 ========== */
/**
 * @brief 设置当前工作表一段行范围的行高。
 * @param self     XDocument 文档指针。
 * @param rowFirst 起始行号，从 1 开始。
 * @param rowLast  结束行号，必须不小于起始行号。
 * @param height   行高值。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setRowHeight(XDocument* self, int rowFirst, int rowLast, double height);

/**
 * @brief 设置当前工作表一段行范围的格式。
 * @param self     XDocument 文档指针。
 * @param rowFirst 起始行号，从 1 开始。
 * @param rowLast  结束行号。
 * @param format   要应用的格式；函数只读取对象，不接管所有权。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setRowFormat(XDocument* self, int rowFirst, int rowLast, const XFormat* format);

/**
 * @brief 设置当前工作表一段行范围的隐藏状态。
 * @param self     XDocument 文档指针。
 * @param rowFirst 起始行号，从 1 开始。
 * @param rowLast  结束行号。
 * @param hidden   true 表示隐藏，false 表示显示。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_setRowHidden(XDocument* self, int rowFirst, int rowLast, bool hidden);

/**
 * @brief 获取指定行的行高。
 * @param self XDocument 文档指针。
 * @param row  行号，从 1 开始。
 * @return 行高；文档或行号无效返回 -1.0。
 */
double XDocument_rowHeight(const XDocument* self, int row);

/**
 * @brief 获取指定行的格式副本。
 * @param self XDocument 文档指针。
 * @param row  行号，从 1 开始。
 * @return 新创建的格式对象；失败返回 NULL，调用方负责释放。
 */
XFormat* XDocument_rowFormat(const XDocument* self, int row);

/**
 * @brief 查询指定行是否隐藏。
 * @param self XDocument 文档指针。
 * @param row  行号，从 1 开始。
 * @return 已隐藏返回 true；无效参数或未隐藏返回 false。
 */
bool XDocument_isRowHidden(const XDocument* self, int row);

/* ========== 分组 ========== */
/**
 * @brief 设置行范围的分组折叠状态。
 * @param self      XDocument 文档指针。
 * @param rowFirst  起始行号，从 1 开始。
 * @param rowLast   结束行号。
 * @param collapsed true 表示折叠，false 表示展开。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_groupRows(XDocument* self, int rowFirst, int rowLast, bool collapsed);

/**
 * @brief 设置列范围的分组折叠状态。
 * @param self      XDocument 文档指针。
 * @param colFirst  起始列号，从 1 开始。
 * @param colLast   结束列号。
 * @param collapsed true 表示折叠，false 表示展开。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XDocument_groupColumns(XDocument* self, int colFirst, int colLast, bool collapsed);

/* ========== 数据验证与条件格式 ========== */
/**
 * @brief 将数据验证规则添加到当前工作表。
 * @param self       XDocument 文档指针。
 * @param validation 数据验证对象；成功后工作表接管其所有权，失败时调用方仍负责释放。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XDocument_addDataValidation(XDocument* self, XDataValidation* validation);

/**
 * @brief 将条件格式规则添加到当前工作表。
 * @param self XDocument 文档指针。
 * @param cf   条件格式对象；成功后工作表接管其所有权，失败时调用方仍负责释放。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XDocument_addConditionalFormatting(XDocument* self, XConditionalFormatting* cf);

/* ========== 定义名称 ========== */
/**
 * @brief 添加工作簿或工作表级定义名称。
 * @param self    XDocument 文档指针。
 * @param name    定义名称，不能为空。
 * @param formula 名称对应的公式或引用，不能为空。
 * @param comment 可选注释；可为 NULL。
 * @param scope   可选作用域工作表名称；为 NULL 表示工作簿级别。
 * @return 添加成功返回 true，否则返回 false。
 * @note 所有输入字符串只在调用期间读取，函数会复制需要保存的内容。
 */
bool XDocument_defineName(XDocument* self, const XString* name, const XString* formula, const XString* comment, const XString* scope);

/**
 * @brief 使用 UTF-8 字符串添加定义名称。
 * @param self    XDocument 文档指针。
 * @param name    定义名称，UTF-8 编码，不能为空。
 * @param formula 名称对应的公式或引用，UTF-8 编码，不能为空。
 * @param comment 可选 UTF-8 注释；可为 NULL。
 * @param scope   可选 UTF-8 作用域工作表名称；NULL 表示工作簿级别。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XDocument_defineName_utf8(XDocument* self, const char* name, const char* formula, const char* comment, const char* scope);

/* ========== 维度 ========== */
/**
 * @brief 获取当前工作表实际使用区域的最小外接范围。
 * @param self XDocument 文档指针。
 * @return 单元格范围值对象；文档或当前工作表无效时返回无效范围。
 */
XCellRange XDocument_dimension(const XDocument* self);

/* ========== 文档属性 ========== */
/**
 * @brief 按名称读取核心文档属性。
 * @param self XDocument 文档指针。
 * @param name 属性名称；函数只读取该字符串。
 * @return 文档内部属性字符串；未找到返回 NULL，调用方不得释放。
 */
const XString* XDocument_documentProperty(const XDocument* self, const XString* name);

/**
 * @brief 使用 UTF-8 名称读取核心文档属性。
 * @param self XDocument 文档指针。
 * @param name UTF-8 编码的属性名称。
 * @return 文档内部属性字符串；未找到返回 NULL，调用方不得释放。
 */
const XString* XDocument_documentProperty_utf8(const XDocument* self, const char* name);

/**
 * @brief 设置核心文档属性。
 * @param self     XDocument 文档指针。
 * @param name     属性名称；不能为空。
 * @param property 属性值；不能为空。
 * @note 文档会复制名称和值，调用方仍负责管理输入对象。
 */
void XDocument_setDocumentProperty(XDocument* self, const XString* name, const XString* property);

/**
 * @brief 使用 UTF-8 字符串设置核心文档属性。
 * @param self     XDocument 文档指针。
 * @param name     UTF-8 编码的属性名称。
 * @param property UTF-8 编码的属性值。
 */
void XDocument_setDocumentProperty_utf8(XDocument* self, const char* name, const char* property);

/**
 * @brief 枚举文档属性名称。
 * @param self  XDocument 文档指针。
 * @param names 输出数组地址；成功后返回新分配的 XString* 数组，调用方负责释放数组及其中字符串。
 * @return 属性数量；失败返回 0。
 */
int XDocument_documentPropertyNames(const XDocument* self, XString*** names);

/* ========== 工作表管理 ========== */
/**
 * @brief 获取工作表名称列表。
 * @param self  XDocument 文档指针。
 * @param names 输出数组地址；成功后返回新分配的名称数组，调用方负责释放数组及其中字符串。
 * @return 工作表数量；失败返回 0。
 */
int XDocument_sheetNames(const XDocument* self, XString*** names);

/**
 * @brief 在工作簿末尾添加工作表或图表表。
 * @param self XDocument 文档指针。
 * @param name 工作表名称；NULL 时由工作簿自动生成。
 * @param type 工作表类型，由 XAbstractSheet_SheetType 指定。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XDocument_addSheet(XDocument* self, const XString* name, XAbstractSheet_SheetType type);

/**
 * @brief 使用 UTF-8 名称在末尾添加工作表或图表表。
 * @param self XDocument 文档指针。
 * @param name UTF-8 工作表名称；NULL 时自动生成。
 * @param type 工作表类型。
 * @return 添加成功返回 true，否则返回 false。
 */
bool XDocument_addSheet_utf8(XDocument* self, const char* name, XAbstractSheet_SheetType type);

/**
 * @brief 在指定索引位置插入工作表或图表表。
 * @param self  XDocument 文档指针。
 * @param index 插入索引，从 0 开始；允许的位置为 0 到当前数量。
 * @param name  工作表名称；NULL 时自动生成。
 * @param type  工作表类型。
 * @return 插入成功返回 true，否则返回 false。
 */
bool XDocument_insertSheet(XDocument* self, int index, const XString* name, XAbstractSheet_SheetType type);

/**
 * @brief 按名称选择活动工作表。
 * @param self XDocument 文档指针。
 * @param name 工作表名称；函数只读取该字符串。
 * @return 选择成功返回 true，否则返回 false。
 */
bool XDocument_selectSheet(XDocument* self, const XString* name);

/**
 * @brief 使用 UTF-8 名称选择活动工作表。
 * @param self XDocument 文档指针。
 * @param name UTF-8 工作表名称。
 * @return 选择成功返回 true，否则返回 false。
 */
bool XDocument_selectSheet_utf8(XDocument* self, const char* name);

/**
 * @brief 按索引选择活动工作表。
 * @param self  XDocument 文档指针。
 * @param index 工作表索引，从 0 开始。
 * @return 选择成功返回 true，否则返回 false。
 */
bool XDocument_selectSheetByIndex(XDocument* self, int index);

/**
 * @brief 按名称重命名工作表。
 * @param self    XDocument 文档指针。
 * @param oldName 当前工作表名称。
 * @param newName 新工作表名称；必须符合 Excel 工作表命名规则且不能重复。
 * @return 重命名成功返回 true，否则返回 false。
 */
bool XDocument_renameSheet(XDocument* self, const XString* oldName, const XString* newName);

/**
 * @brief 使用 UTF-8 名称重命名工作表。
 * @param self    XDocument 文档指针。
 * @param oldName UTF-8 当前工作表名称。
 * @param newName UTF-8 新工作表名称。
 * @return 重命名成功返回 true，否则返回 false。
 */
bool XDocument_renameSheet_utf8(XDocument* self, const char* oldName, const char* newName);

/**
 * @brief 按名称复制工作表。
 * @param self     XDocument 文档指针。
 * @param srcName  源工作表名称。
 * @param distName 副本名称；NULL 或空字符串时自动生成唯一名称。
 * @return 复制成功返回 true，否则返回 false。
 */
bool XDocument_copySheet(XDocument* self, const XString* srcName, const XString* distName);

/**
 * @brief 按名称将工作表移动到目标索引。
 * @param self      XDocument 文档指针。
 * @param srcName   要移动的工作表名称。
 * @param distIndex 目标索引，从 0 开始。
 * @return 移动成功返回 true，否则返回 false。
 */
bool XDocument_moveSheet(XDocument* self, const XString* srcName, int distIndex);

/**
 * @brief 按名称删除工作表。
 * @param self XDocument 文档指针。
 * @param name 要删除的工作表名称。
 * @return 删除成功返回 true，否则返回 false。
 */
bool XDocument_deleteSheet(XDocument* self, const XString* name);

/**
 * @brief 使用 UTF-8 名称删除工作表。
 * @param self XDocument 文档指针。
 * @param name UTF-8 要删除的工作表名称。
 * @return 删除成功返回 true，否则返回 false。
 */
bool XDocument_deleteSheet_utf8(XDocument* self, const char* name);

/* ========== 工作簿和当前工作表访问 ========== */
/**
 * @brief 获取文档内部的工作簿对象。
 * @param self XDocument 文档指针。
 * @return 文档内部持有的 XWorkbook 指针；无效文档返回 NULL；调用方不得释放。
 */
XWorkbook* XDocument_workbook(const XDocument* self);

/**
 * @brief 按名称获取工作表对象。
 * @param self      XDocument 文档指针。
 * @param sheetName 工作表名称；函数只读取该字符串。
 * @return 文档内部持有的工作表指针；未找到返回 NULL；调用方不得释放。
 */
XAbstractSheet* XDocument_sheet(const XDocument* self, const XString* sheetName);

/**
 * @brief 获取当前活动工作表对象。
 * @param self XDocument 文档指针。
 * @return 文档内部持有的活动工作表指针；无效文档返回 NULL；调用方不得释放。
 */
XAbstractSheet* XDocument_currentSheet(const XDocument* self);

/**
 * @brief 获取当前活动工作表，并要求其为普通工作表类型。
 * @param self XDocument 文档指针。
 * @return 当前 XWorksheet 指针；活动页是 Chartsheet 或参数无效时返回 NULL；调用方不得释放。
 */
XWorksheet* XDocument_currentWorksheet(const XDocument* self);

/* ========== 保存和加载 ========== */
/**
 * @brief 将文档保存到其当前文件路径。
 * @param self XDocument 文档指针；必须已设置文件路径。
 * @return 保存成功返回 true；没有路径或写入失败返回 false。
 * @note 保存过程同步完成，返回 true 时文件内容已经写入并关闭。
 */
bool XDocument_save(const XDocument* self);

/**
 * @brief 将文档保存为指定 XLSX 文件。
 * @param self    XDocument 文档指针。
 * @param xlsxName 目标 XLSX 文件路径；函数只读取该字符串。
 * @return 保存成功返回 true，否则返回 false。
 * @note 保存过程同步完成，返回 true 时目标文件已经完整写出。
 */
bool XDocument_saveAs(const XDocument* self, const XString* xlsxName);

/**
 * @brief 使用 UTF-8 路径保存 XLSX 文件。
 * @param self    XDocument 文档指针。
 * @param xlsxName UTF-8 目标 XLSX 文件路径。
 * @return 保存成功返回 true，否则返回 false。
 */
bool XDocument_saveAs_utf8(const XDocument* self, const char* xlsxName);

/**
 * @brief 将文档同步写入指定输出设备。
 * @param self   XDocument 文档指针。
 * @param device 可写的 XIODevice；设备由调用方持有，函数不会销毁或接管所有权。
 * @return 写入成功返回 true，否则返回 false。
 * @note 设备必须可写；函数不会隐式改变异步语义，返回时 ZIP 数据已提交到设备。
 */
bool XDocument_saveAsDevice(const XDocument* self, struct XIODevice* device);

/**
 * @brief 查询文档是否从已加载的 XLSX 包创建。
 * @param self XDocument 文档指针。
 * @return 已成功加载 XLSX 包返回 true，否则返回 false。
 */
bool XDocument_isLoadPackage(const XDocument* self);

/**
 * @brief 从文档当前文件路径加载或重新加载 XLSX 内容。
 * @param self XDocument 文档指针；必须已设置非空文件路径。
 * @return 加载成功返回 true，否则返回 false。
 * @note 加载过程同步完成；失败时文档内容可能已被部分更新，调用方应检查返回值。
 */
bool XDocument_load(XDocument* self);

/* ========== SAX 读取 ========== */
/**
 * @brief 按工作表名称以 SAX 方式流式读取单元格。
 * @param self     已加载 XLSX 文件的 XDocument 文档指针。
 * @param sheetName 工作表名称；传 NULL 时不能按名称匹配。
 * @param opt      SAX 读取选项；可为 NULL 使用默认选项。
 * @param onCell   单元格回调；每读取一个单元格调用一次，不能为空。
 * @param userData 回调用户数据指针，原样传回，不由函数释放。
 * @return 读取成功返回 true；文件、工作表或回调无效返回 false。
 */
bool XDocument_readSheetSax(XDocument* self, const XString* sheetName, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData);

/**
 * @brief 按工作表索引以 SAX 方式流式读取单元格。
 * @param self       已加载 XLSX 文件的 XDocument 文档指针。
 * @param sheetIndex 工作表索引，从 0 开始。
 * @param opt        SAX 读取选项；可为 NULL 使用默认选项。
 * @param onCell     单元格回调；不能为空。
 * @param userData   回调用户数据指针，原样传回，不由函数释放。
 * @return 读取成功返回 true；索引、文件或回调无效返回 false。
 */
bool XDocument_readSheetSaxByIndex(XDocument* self, int sheetIndex, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData);

/* ========== 自动列宽 ========== */
/**
 * @brief 根据当前工作表单元格内容自动调整指定列范围的宽度。
 * @param self     XDocument 文档指针。
 * @param colFirst 起始列号，从 1 开始。
 * @param colLast  结束列号。
 * @return 调整成功返回 true；当前页不是普通工作表或没有有效单元格返回 false。
 */
bool XDocument_autosizeColumnWidth(XDocument* self, int colFirst, int colLast);

/**
 * @brief 根据当前工作表内容自动调整所有已使用列的宽度。
 * @param self XDocument 文档指针。
 * @return 调整成功返回 true；当前页不是普通工作表或没有有效单元格返回 false。
 */
bool XDocument_autosizeColumnWidthAll(XDocument* self);

/* ========== 样式与图片 ========== */
/**
 * @brief 将源 XLSX 的样式复制到目标 XLSX。
 * @param fromPath 源 XLSX 文件路径；函数只读取该字符串。
 * @param toPath   目标 XLSX 文件路径；函数会更新该文件。
 * @return 复制成功返回 true，否则返回 false。
 * @note 目标文件必须是可读写的有效 XLSX；操作通过临时文件完成。
 */
bool XDocument_copyStyle(const XString* fromPath, const XString* toPath);

/**
 * @brief 替换当前工作表指定索引的图片内容。
 * @param self         XDocument 文档指针。
 * @param imageIndex   图片索引，从 0 开始。
 * @param newImagePath 新图片文件路径；函数只读取该字符串。
 * @return 替换成功返回 true，否则返回 false。
 * @note 新图片类型由文件扩展名推断，图片索引不会改变。
 */
bool XDocument_changeImage(XDocument* self, int imageIndex, const XString* newImagePath);

#ifdef __cplusplus
}
#endif
#endif /* XDOCUMENT_H */
