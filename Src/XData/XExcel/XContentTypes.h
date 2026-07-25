/******************************************************************************
 * @file       XContentTypes.h
 * @brief      XContentTypes OOXML 内容类型类（对标 QXlsx::ContentTypes）
 * @author     XinYueC 团队
 * @note       管理 OOXML 包中的内容类型（[Content_Types].xml），
 *             包括默认类型和覆盖类型。对齐 QXlsx::ContentTypes 全部功能
 ******************************************************************************/
#ifndef XCONTENTTYPES_H
#define XCONTENTTYPES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XStringList.h"
#include "XMap.h"

/**
 * @brief      XContentTypes 内容类型管理类
 * @note       管理 [Content_Types].xml 中的默认类型和覆盖类型定义。
 *             对齐 QXlsx::ContentTypes 全部功能。
 */
typedef struct XContentTypes
{
    XMap* m_defaults;    /**< 默认类型映射（后缀->
MIME类型） */
    XMap* m_overrides;
    int m_worksheetCount;
    int m_chartsheetCount;
    int m_chartCount;
    int m_drawingCount;
    int m_commentCount;
    int m_tableCount;
    int m_externalLinkCount;
    int m_vmlCount;   /**< 覆盖类型映射（路径->MIME类型） */
} XContentTypes;

/**
 * @brief  创建内容类型管理器
 * @return 新创建的 XContentTypes 指针，失败返回 NULL
 */
XContentTypes* XContentTypes_create(void);

/**
 * @brief  销毁内容类型管理器，释放所有资源
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_delete(XContentTypes* self);

/**
 * @brief  添加默认内容类型（按文件扩展名）
 * @param  self   XContentTypes 实例指针
 * @param  key    文件扩展名（不含点号），如 "xml"、"rels"
 * @param  value  对应的 MIME 类型字符串
 */
void XContentTypes_addDefault(XContentTypes* self, const XString* key, const XString* value);

/**
 * @brief  添加覆盖内容类型（按部件路径）
 * @param  self   XContentTypes 实例指针
 * @param  key    部件路径，如 "/xl/workbook.xml"
 * @param  value  对应的 MIME 类型字符串
 */
void XContentTypes_addOverride(XContentTypes* self, const XString* key, const XString* value);

/**
 * @brief  注册核心文档属性部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addDocPropCore(XContentTypes* self);

/**
 * @brief  注册应用程序文档属性部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addDocPropApp(XContentTypes* self);

/**
 * @brief  注册样式部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addStyles(XContentTypes* self);

/**
 * @brief  注册主题部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addTheme(XContentTypes* self);

/**
 * @brief  注册工作簿部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addWorkbook(XContentTypes* self);

/**
 * @brief  按名称注册工作表部件的内容类型（自动递增计数）
 * @param  self  XContentTypes 实例指针
 * @param  name  工作表部件路径
 */
void XContentTypes_addWorksheetName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册图表工作表部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  图表工作表部件路径
 */
void XContentTypes_addChartsheetName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册图表部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  图表部件路径
 */
void XContentTypes_addChartName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册绘图部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  绘图部件路径
 */
void XContentTypes_addDrawingName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册批注部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  批注部件路径
 */
void XContentTypes_addCommentName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册表格部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  表格部件路径
 */
void XContentTypes_addTableName(XContentTypes* self, const XString* name);

/**
 * @brief  按名称注册外部链接部件的内容类型
 * @param  self  XContentTypes 实例指针
 * @param  name  外部链接部件路径
 */
void XContentTypes_addExternalLinkName(XContentTypes* self, const XString* name);

/**
 * @brief  注册共享字符串表部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addSharedString(XContentTypes* self);

/**
 * @brief  注册 VML 绘图部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addVmlName(XContentTypes* self);

/**
 * @brief  注册计算链部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addCalcChain(XContentTypes* self);

/**
 * @brief  注册 VBA 工程部件的内容类型
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_addVbaProject(XContentTypes* self);

/**
 * @brief  清除所有覆盖类型（保留默认类型）
 * @param  self  XContentTypes 实例指针
 */
void XContentTypes_clearOverrides(XContentTypes* self);

/**
 * @brief  将内容类型保存为 XML 文件
 * @param  self      XContentTypes 实例指针
 * @param  filePath  目标文件路径
 * @return 成功返回 true，失败返回 false
 */
bool XContentTypes_saveToXmlFile(const XContentTypes* self, const XString* filePath);

/**
 * @brief  将内容类型序列化为 XML 数据缓冲区
 * @param  self     XContentTypes 实例指针
 * @param  outData  输出数据指针（调用者负责释放）
 * @param  outLen   输出数据长度
 * @return 成功返回 true，失败返回 false
 */
bool XContentTypes_saveToXmlData(const XContentTypes* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  从 XML 数据缓冲区加载内容类型
 * @param  self  XContentTypes 实例指针
 * @param  data  XML 数据
 * @param  len   数据长度
 * @return 成功返回 true，失败返回 false
 */
bool XContentTypes_loadFromXmlData(XContentTypes* self, const uint8_t* data, size_t len);

/**
 * @brief  从 XML 文件加载内容类型
 * @param  self      XContentTypes 实例指针
 * @param  filePath  源文件路径
 * @return 成功返回 true，失败返回 false
 */
bool XContentTypes_loadFromXmlFile(XContentTypes* self, const XString* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XCONTENTTYPES_H */
