/******************************************************************************
 * @file       XWorkbook.h
 * @brief      XWorkbook 工作簿类（对标 QXlsx::Workbook）
 * @author     XinYueC 团队
 * @note       提供工作簿管理，包含工作表、共享字符串、样式、主题、媒体文件、图表等。
 *             对齐 QXlsx::Workbook 全部功能
 ******************************************************************************/
#ifndef XWORKBOOK_H
#define XWORKBOOK_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XVector.h"
#include "XAbstractOOXmlFile.h"
#include "XAbstractSheet.h"
#include "XSharedStrings.h"
#include "XStyles.h"
#include "XTheme.h"
#include "XChart.h"
#include "XMediaFile.h"

/* 前向声明 */
typedef struct XWorksheet XWorksheet;
typedef struct XChartsheet XChartsheet;
typedef struct XDrawing XDrawing;

/** @brief 定义名称结构体 */
typedef struct XWorkbook_DefineName {
    XString* m_name;     /**< 名称 */
    XString* m_formula;  /**< 公式 */
    XString* m_comment;  /**< 注释 */
    XString* m_scope;    /**< 范围 */
} XWorkbook_DefineName;

/** @brief XWorkbook 工作簿结构体 */
typedef struct XWorkbook {
    XAbstractOOXmlFile m_base;         /**< 基类 */
    XVector* m_sheets;                 /**< 工作表列表 (XAbstractSheet*) */
    int m_activeSheetIndex;            /**< 活动工作表索引 */
    XSharedStrings* m_sharedStrings;   /**< 共享字符串表 */
    XStyles* m_styles;                 /**< 样式管理器 */
    XTheme* m_theme;                   /**< 主题 */
    XVector* m_mediaFiles;             /**< 媒体文件列表 (XMediaFile*) */
    XVector* m_chartFiles;             /**< 图表文件列表 (XChart*) */
    XVector* m_defineNames;            /**< 定义名称列表 (XWorkbook_DefineName) */
    bool m_date1904;                   /**< 是否使用1904日期系统 */
    bool m_stringsToNumbers;           /**< 字符串转数字 */
    bool m_stringsToHyperlinks;        /**< 字符串转超链接 */
    bool m_htmlToRichString;           /**< HTML转富文本 */
    bool m_writeDatesAsText;           /**< 日期写入为文本 */
    XString* m_defaultDateFormat;      /**< 默认日期格式 */
    int m_nextSheetId;                 /**< 下一个工作表ID */
} XWorkbook;

/* ========== 创建与初始化 ========== */
XWorkbook* XWorkbook_create(XAbstractOOXmlFile_CreateFlag flag);
void XWorkbook_delete(XWorkbook* self);

/* ========== 工作表管理 ========== */
int XWorkbook_sheetCount(const XWorkbook* self);
XAbstractSheet* XWorkbook_sheet(const XWorkbook* self, int index);
XAbstractSheet* XWorkbook_addSheet(XWorkbook* self, const char* name, XAbstractSheet_SheetType type);
XAbstractSheet* XWorkbook_insertSheet(XWorkbook* self, int index, const char* name, XAbstractSheet_SheetType type);
bool XWorkbook_renameSheet(XWorkbook* self, int index, const char* name);
bool XWorkbook_deleteSheet(XWorkbook* self, int index);
bool XWorkbook_copySheet(XWorkbook* self, int index, const char* newName);
bool XWorkbook_moveSheet(XWorkbook* self, int srcIndex, int distIndex);
XAbstractSheet* XWorkbook_activeSheet(const XWorkbook* self);
bool XWorkbook_setActiveSheet(XWorkbook* self, int index);

/* ========== 定义名称 ========== */
bool XWorkbook_defineName(XWorkbook* self, const char* name, const char* formula, const char* comment, const char* scope);

/* ========== 属性 ========== */
bool XWorkbook_isDate1904(const XWorkbook* self);
void XWorkbook_setDate1904(XWorkbook* self, bool date1904);
bool XWorkbook_isStringsToNumbersEnabled(const XWorkbook* self);
void XWorkbook_setStringsToNumbersEnabled(XWorkbook* self, bool enable);
bool XWorkbook_isStringsToHyperlinksEnabled(const XWorkbook* self);
void XWorkbook_setStringsToHyperlinksEnabled(XWorkbook* self, bool enable);
bool XWorkbook_isHtmlToRichStringEnabled(const XWorkbook* self);
void XWorkbook_setHtmlToRichStringEnabled(XWorkbook* self, bool enable);
const char* XWorkbook_defaultDateFormat(const XWorkbook* self);
void XWorkbook_setDefaultDateFormat(XWorkbook* self, const char* format);
void XWorkbook_setWriteDatesAsText(XWorkbook* self, bool enable);
bool XWorkbook_writeDatesAsText(const XWorkbook* self);

/* ========== 内部接口 ========== */
XSharedStrings* XWorkbook_sharedStrings(const XWorkbook* self);
XStyles* XWorkbook_styles(XWorkbook* self);
XTheme* XWorkbook_theme(const XWorkbook* self);
void XWorkbook_addMediaFile(XWorkbook* self, XMediaFile* media, bool force);
XMediaFile** XWorkbook_mediaFiles(const XWorkbook* self, int* count);
void XWorkbook_addChartFile(XWorkbook* self, XChart* chartFile);
XChart** XWorkbook_chartFiles(const XWorkbook* self, int* count);
XAbstractSheet** XWorkbook_getSheetsByTypes(const XWorkbook* self, XAbstractSheet_SheetType type, int* count);
XString** XWorkbook_worksheetNames(const XWorkbook* self, int* count);
XAbstractSheet* XWorkbook_addSheetEx(XWorkbook* self, const char* name, int sheetId, XAbstractSheet_SheetType type);

/* ========== XML 读写 ========== */
bool XWorkbook_saveToXmlFile(XWorkbook* self, const char* filePath);
bool XWorkbook_loadFromXmlFile(XWorkbook* self, const char* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XWORKBOOK_H */
bool XWorkbook_saveToXmlData(const XWorkbook* self, uint8_t** outData, size_t* outLen);
bool XWorkbook_loadFromXmlData(XWorkbook* self, const uint8_t* data, size_t len);
