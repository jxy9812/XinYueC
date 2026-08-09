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

/**
 * @brief  创建工作簿对象
 * @param  flag  创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新工作簿指针，失败返回 NULL
 */
XWorkbook* XWorkbook_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁工作簿并释放所有子资源（工作表、样式、主题等）
 * @param  self  工作簿指针
 */
void XWorkbook_delete(XWorkbook* self);

/* ========== 工作表管理 ========== */

/**
 * @brief  获取工作表数量
 * @param  self  工作簿指针
 * @return 工作表总数
 */
int XWorkbook_sheetCount(const XWorkbook* self);

/**
 * @brief  按索引获取工作表
 * @param  self   工作簿指针
 * @param  index  工作表索引（从 0 开始）
 * @return 工作表指针，索引无效返回 NULL
 */
XAbstractSheet* XWorkbook_sheet(const XWorkbook* self, int index);

/**
 * @brief  在末尾添加工作表
 * @param  self  工作簿指针
 * @param  name  工作表名称（NULL 则自动生成）
 * @param  type  工作表类型（工作表/图表表）
 * @return 新工作表指针，失败返回 NULL
 */
XAbstractSheet* XWorkbook_addSheet(XWorkbook* self, const XString* name, XAbstractSheet_SheetType type);
/**
 * @brief 使用 UTF-8 名称在工作簿末尾添加工作表。
 * @param self 工作簿指针。
 * @param name UTF-8 工作表名称；NULL 时自动生成。
 * @param type 工作表类型。
 * @return 新工作表指针；失败返回 NULL，工作簿持有成功添加的对象。
 */
XAbstractSheet* XWorkbook_addSheet_utf8(XWorkbook* self, const char* name, XAbstractSheet_SheetType type);

/**
 * @brief  在指定位置插入工作表
 * @param  self   工作簿指针
 * @param  index  插入位置索引
 * @param  name   工作表名称（NULL 则自动生成）
 * @param  type   工作表类型
 * @return 新工作表指针，失败返回 NULL
 */
XAbstractSheet* XWorkbook_insertSheet(XWorkbook* self, int index, const XString* name, XAbstractSheet_SheetType type);

/**
 * @brief  重命名工作表
 * @param  self   工作簿指针
 * @param  index  工作表索引
 * @param  name   新名称
 * @return 成功返回 true
 */
bool XWorkbook_renameSheet(XWorkbook* self, int index, const XString* name);

/**
 * @brief  删除工作表
 * @param  self   工作簿指针
 * @param  index  工作表索引
 * @return 成功返回 true
 */
bool XWorkbook_deleteSheet(XWorkbook* self, int index);

/**
 * @brief  复制工作表
 * @param  self     工作簿指针
 * @param  index    源工作表索引
 * @param  newName  副本名称
 * @return 成功返回 true
 */
bool XWorkbook_copySheet(XWorkbook* self, int index, const XString* newName);

/**
 * @brief  移动工作表位置
 * @param  self       工作簿指针
 * @param  srcIndex   源索引
 * @param  distIndex  目标索引
 * @return 成功返回 true
 */
bool XWorkbook_moveSheet(XWorkbook* self, int srcIndex, int distIndex);

/**
 * @brief  获取当前活动工作表
 * @param  self  工作簿指针
 * @return 活动工作表指针
 */
XAbstractSheet* XWorkbook_activeSheet(const XWorkbook* self);

/**
 * @brief  设置活动工作表
 * @param  self   工作簿指针
 * @param  index  工作表索引
 * @return 成功返回 true
 */
bool XWorkbook_setActiveSheet(XWorkbook* self, int index);

/* ========== 定义名称 ========== */

/**
 * @brief  添加定义名称（命名范围）
 * @param  self     工作簿指针
 * @param  name     名称
 * @param  formula  公式/引用
 * @param  comment  注释（可为 NULL）
 * @param  scope    作用范围（可为 NULL 表示工作簿级别）
 * @return 成功返回 true
 */
bool XWorkbook_defineName(XWorkbook* self, const XString* name, const XString* formula, const XString* comment, const XString* scope);

/* ========== 属性 ========== */

/**
 * @brief  查询是否使用 1904 日期系统
 * @param  self  工作簿指针
 * @return 使用 1904 系统返回 true
 */
bool XWorkbook_isDate1904(const XWorkbook* self);

/**
 * @brief  设置是否使用 1904 日期系统
 * @param  self     工作簿指针
 * @param  date1904 true 启用 1904 系统
 */
void XWorkbook_setDate1904(XWorkbook* self, bool date1904);

/**
 * @brief  查询是否启用字符串自动转数字
 * @param  self  工作簿指针
 * @return 启用返回 true
 */
bool XWorkbook_isStringsToNumbersEnabled(const XWorkbook* self);

/**
 * @brief  设置是否启用字符串自动转数字
 * @param  self    工作簿指针
 * @param  enable  true 启用
 */
void XWorkbook_setStringsToNumbersEnabled(XWorkbook* self, bool enable);

/**
 * @brief  查询是否启用字符串自动转超链接
 * @param  self  工作簿指针
 * @return 启用返回 true
 */
bool XWorkbook_isStringsToHyperlinksEnabled(const XWorkbook* self);

/**
 * @brief  设置是否启用字符串自动转超链接
 * @param  self    工作簿指针
 * @param  enable  true 启用
 */
void XWorkbook_setStringsToHyperlinksEnabled(XWorkbook* self, bool enable);

/**
 * @brief  查询是否启用 HTML 转富文本
 * @param  self  工作簿指针
 * @return 启用返回 true
 */
bool XWorkbook_isHtmlToRichStringEnabled(const XWorkbook* self);

/**
 * @brief  设置是否启用 HTML 转富文本
 * @param  self    工作簿指针
 * @param  enable  true 启用
 */
void XWorkbook_setHtmlToRichStringEnabled(XWorkbook* self, bool enable);

/**
 * @brief  获取默认日期格式字符串
 * @param  self  工作簿指针
 * @return 日期格式字符串
 */
const XString* XWorkbook_defaultDateFormat(const XWorkbook* self);

/**
 * @brief  设置默认日期格式
 * @param  self    工作簿指针
 * @param  format  日期格式字符串（如 "yyyy-mm-dd"）
 */
void XWorkbook_setDefaultDateFormat(XWorkbook* self, const XString* format);

/**
 * @brief  设置日期是否以文本形式写入
 * @param  self    工作簿指针
 * @param  enable  true 以文本写入
 */
void XWorkbook_setWriteDatesAsText(XWorkbook* self, bool enable);

/**
 * @brief  查询日期是否以文本形式写入
 * @param  self  工作簿指针
 * @return 以文本写入返回 true
 */
bool XWorkbook_writeDatesAsText(const XWorkbook* self);

/* ========== 内部接口 ========== */

/**
 * @brief  获取共享字符串表
 * @param  self  工作簿指针
 * @return 共享字符串表指针
 */
XSharedStrings* XWorkbook_sharedStrings(const XWorkbook* self);

/**
 * @brief  获取样式管理器
 * @param  self  工作簿指针
 * @return 样式管理器指针
 */
XStyles* XWorkbook_styles(XWorkbook* self);

/**
 * @brief  获取主题对象
 * @param  self  工作簿指针
 * @return 主题指针
 */
XTheme* XWorkbook_theme(const XWorkbook* self);

/**
 * @brief  添加媒体文件（图片等）
 * @param  self   工作簿指针
 * @param  media  媒体文件对象（非拥有引用，资源由所属工作表管理）
 * @param  force  为 true 时强制添加（即使已存在相同文件）
 */
void XWorkbook_addMediaFile(XWorkbook* self, XMediaFile* media, bool force);

/**
 * @brief  获取所有媒体文件列表
 * @param  self   工作簿指针
 * @param  count  [out] 接收媒体文件数量
 * @return 媒体文件指针数组
 */
XMediaFile** XWorkbook_mediaFiles(const XWorkbook* self, int* count);

/**
 * @brief  添加图表文件
 * @param  self       工作簿指针
 * @param  chartFile  图表对象（非拥有引用，资源由所属工作表管理）
 */
void XWorkbook_addChartFile(XWorkbook* self, XChart* chartFile);

/**
 * @brief  获取所有图表文件列表
 * @param  self   工作簿指针
 * @param  count  [out] 接收图表文件数量
 * @return 图表文件指针数组
 */
XChart** XWorkbook_chartFiles(const XWorkbook* self, int* count);

/**
 * @brief  按类型获取工作表列表
 * @param  self   工作簿指针
 * @param  type   工作表类型
 * @param  count  [out] 接收匹配数量
 * @return 工作表指针数组
 */
XAbstractSheet** XWorkbook_getSheetsByTypes(const XWorkbook* self, XAbstractSheet_SheetType type, int* count);

/**
 * @brief  获取所有工作表名称列表
 * @param  self   工作簿指针
 * @param  count  [out] 接收名称数量
 * @return 名称字符串数组
 */
XString** XWorkbook_worksheetNames(const XWorkbook* self, int* count);

/**
 * @brief  添加工作表（指定 sheetId，内部使用）
 * @param  self     工作簿指针
 * @param  name     工作表名称
 * @param  sheetId  工作表 ID
 * @param  type     工作表类型
 * @return 新工作表指针，失败返回 NULL
 */
XAbstractSheet* XWorkbook_addSheetEx(XWorkbook* self, const XString* name, int sheetId, XAbstractSheet_SheetType type);

/* ========== XML 读写 ========== */

/**
 * @brief  将工作簿保存为 XML 文件（xl/workbook.xml）
 * @param  self      工作簿指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XWorkbook_saveToXmlFile(XWorkbook* self, const XString* filePath);

/**
 * @brief  从 XML 文件加载工作簿
 * @param  self      工作簿指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XWorkbook_loadFromXmlFile(XWorkbook* self, const XString* filePath);

#ifdef __cplusplus
}
#endif
/**
 * @brief  将工作簿序列化为 XML 数据
 * @param  self    工作簿指针
 * @param  outData [out] 接收 XML 数据缓冲区的指针，调用者负责释放
 * @param  outLen  [out] 接收数据长度
 * @return 成功返回 true
 */
bool XWorkbook_saveToXmlData(const XWorkbook* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  从 XML 数据加载工作簿
 * @param  self  工作簿指针
 * @param  data  XML 数据缓冲区
 * @param  len   数据长度（字节）
 * @return 成功返回 true
 */
bool XWorkbook_loadFromXmlData(XWorkbook* self, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* XWORKBOOK_H */
