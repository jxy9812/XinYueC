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
    bool m_isLoaded;                    /**< 是否已加载 */
    bool m_isModified;                  /**< 是否已修改 */
} XDocument;

/* ========== 创建与初始化 ========== */
XDocument* XDocument_create(void);
XDocument* XDocument_createFromFile(const XString* xlsxName);
XDocument* XDocument_createFromDevice(struct XIODevice* device);
void XDocument_delete(XDocument* self);

/* ========== 单元格写入 ========== */
bool XDocument_write(XDocument* self, int row, int col, const XVariant* value, const XFormat* format);
bool XDocument_writeRef(XDocument* self, const XCellReference* cell, const XVariant* value, const XFormat* format);

/* ========== 单元格读取 ========== */
XVariant* XDocument_read(const XDocument* self, int row, int col);
XVariant* XDocument_readRef(const XDocument* self, const XCellReference* cell);
XCell* XDocument_cellAt(const XDocument* self, int row, int col);
XCell* XDocument_cellAtRef(const XDocument* self, const XCellReference* cell);

/* ========== 图片 ========== */
int XDocument_insertImage(XDocument* self, int row, int col, const XString* imagePath);
int XDocument_insertImage_utf8(XDocument* self, int row, int col, const char* imagePath);
bool XDocument_getImage(const XDocument* self, int imageIndex, XByteArray* imgData);
bool XDocument_getImageAt(const XDocument* self, int row, int col, XByteArray* imgData);
unsigned int XDocument_getImageCount(const XDocument* self);

/* ========== 图表 ========== */
XChart* XDocument_insertChart(XDocument* self, int row, int col, int width, int height);

/* ========== 合并单元格 ========== */
bool XDocument_mergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format);
bool XDocument_unmergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol);

/* ========== 列操作 ========== */
bool XDocument_setColumnWidth(XDocument* self, int colFirst, int colLast, double width);
bool XDocument_setColumnFormat(XDocument* self, int colFirst, int colLast, const XFormat* format);
bool XDocument_setColumnHidden(XDocument* self, int colFirst, int colLast, bool hidden);
double XDocument_columnWidth(const XDocument* self, int column);
XFormat* XDocument_columnFormat(const XDocument* self, int column);
bool XDocument_isColumnHidden(const XDocument* self, int column);

/* ========== 行操作 ========== */
bool XDocument_setRowHeight(XDocument* self, int rowFirst, int rowLast, double height);
bool XDocument_setRowFormat(XDocument* self, int rowFirst, int rowLast, const XFormat* format);
bool XDocument_setRowHidden(XDocument* self, int rowFirst, int rowLast, bool hidden);
double XDocument_rowHeight(const XDocument* self, int row);
XFormat* XDocument_rowFormat(const XDocument* self, int row);
bool XDocument_isRowHidden(const XDocument* self, int row);

/* ========== 分组 ========== */
bool XDocument_groupRows(XDocument* self, int rowFirst, int rowLast, bool collapsed);
bool XDocument_groupColumns(XDocument* self, int colFirst, int colLast, bool collapsed);

/* ========== 数据验证与条件格式 ========== */
bool XDocument_addDataValidation(XDocument* self, XDataValidation* validation);
bool XDocument_addConditionalFormatting(XDocument* self, XConditionalFormatting* cf);

/* ========== 定义名称 ========== */
bool XDocument_defineName(XDocument* self, const XString* name, const XString* formula, const XString* comment, const XString* scope);

/* ========== 维度 ========== */
XCellRange XDocument_dimension(const XDocument* self);

/* ========== 文档属性 ========== */
const XString* XDocument_documentProperty(const XDocument* self, const XString* name);
const XString* XDocument_documentProperty_utf8(const XDocument* self, const char* name);
void XDocument_setDocumentProperty(XDocument* self, const XString* name, const XString* property);
void XDocument_setDocumentProperty_utf8(XDocument* self, const char* name, const char* property);
int XDocument_documentPropertyNames(const XDocument* self, XString*** names);

/* ========== 工作表管理 ========== */
int XDocument_sheetNames(const XDocument* self, XString*** names);
bool XDocument_addSheet(XDocument* self, const XString* name, XAbstractSheet_SheetType type);
bool XDocument_addSheet_utf8(XDocument* self, const char* name, XAbstractSheet_SheetType type);
bool XDocument_insertSheet(XDocument* self, int index, const XString* name, XAbstractSheet_SheetType type);
bool XDocument_selectSheet(XDocument* self, const XString* name);
bool XDocument_selectSheet_utf8(XDocument* self, const char* name);
bool XDocument_selectSheetByIndex(XDocument* self, int index);
bool XDocument_renameSheet(XDocument* self, const XString* oldName, const XString* newName);
bool XDocument_renameSheet_utf8(XDocument* self, const char* oldName, const char* newName);
bool XDocument_copySheet(XDocument* self, const XString* srcName, const XString* distName);
bool XDocument_moveSheet(XDocument* self, const XString* srcName, int distIndex);
bool XDocument_deleteSheet(XDocument* self, const XString* name);
bool XDocument_deleteSheet_utf8(XDocument* self, const char* name);

/* ========== 工作簿和当前工作表访问 ========== */
XWorkbook* XDocument_workbook(const XDocument* self);
XAbstractSheet* XDocument_sheet(const XDocument* self, const XString* sheetName);
XAbstractSheet* XDocument_currentSheet(const XDocument* self);
XWorksheet* XDocument_currentWorksheet(const XDocument* self);

/* ========== 保存和加载 ========== */
bool XDocument_save(const XDocument* self);
bool XDocument_saveAs(const XDocument* self, const XString* xlsxName);
bool XDocument_saveAs_utf8(const XDocument* self, const char* xlsxName);
bool XDocument_saveAsDevice(const XDocument* self, struct XIODevice* device);
bool XDocument_isLoadPackage(const XDocument* self);
bool XDocument_load(XDocument* self);

/* ========== SAX 读取 ========== */
bool XDocument_readSheetSax(XDocument* self, const XString* sheetName, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData);
bool XDocument_readSheetSaxByIndex(XDocument* self, int sheetIndex, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData);

/* ========== 自动列宽 ========== */
bool XDocument_autosizeColumnWidth(XDocument* self, int colFirst, int colLast);
bool XDocument_autosizeColumnWidthAll(XDocument* self);

#ifdef __cplusplus
}
#endif
#endif /* XDOCUMENT_H */

/* ========== 样式复制 ========== */
/**
 * @brief      从源文件复制样式到目标文件（静态方法）
 * @param fromPath 源 XLSX 文件路径
 * @param toPath   目标 XLSX 文件路径
 * @return     成功返回 true
 * @note       对标 QXlsx::Document::copyStyle
 */
bool XDocument_copyStyle(const XString* fromPath, const XString* toPath);

/* ========== 图片修改 ========== */
/**
 * @brief      修改文档中的图片
 * @param self           文档指针
 * @param imageIndex     图片索引
 * @param newImagePath   新图片路径
 * @return              成功返回 true
 * @note       对标 QXlsx::Document::changeimage
 */
bool XDocument_changeImage(XDocument* self, int imageIndex, const XString* newImagePath);
