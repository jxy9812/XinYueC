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

XContentTypes* XContentTypes_create(void);
void XContentTypes_delete(XContentTypes* self);
void XContentTypes_addDefault(XContentTypes* self, const char* key, const char* value);
void XContentTypes_addOverride(XContentTypes* self, const char* key, const char* value);
void XContentTypes_addDocPropCore(XContentTypes* self);
void XContentTypes_addDocPropApp(XContentTypes* self);
void XContentTypes_addStyles(XContentTypes* self);
void XContentTypes_addTheme(XContentTypes* self);
void XContentTypes_addWorkbook(XContentTypes* self);
void XContentTypes_addWorksheetName(XContentTypes* self, const char* name);
void XContentTypes_addChartsheetName(XContentTypes* self, const char* name);
void XContentTypes_addChartName(XContentTypes* self, const char* name);
void XContentTypes_addDrawingName(XContentTypes* self, const char* name);
void XContentTypes_addCommentName(XContentTypes* self, const char* name);
void XContentTypes_addTableName(XContentTypes* self, const char* name);
void XContentTypes_addExternalLinkName(XContentTypes* self, const char* name);
void XContentTypes_addSharedString(XContentTypes* self);
void XContentTypes_addVmlName(XContentTypes* self);
void XContentTypes_addCalcChain(XContentTypes* self);
void XContentTypes_addVbaProject(XContentTypes* self);
void XContentTypes_clearOverrides(XContentTypes* self);
bool XContentTypes_saveToXmlFile(const XContentTypes* self, const char* filePath);
bool XContentTypes_saveToXmlData(const XContentTypes* self, uint8_t** outData, size_t* outLen);
bool XContentTypes_loadFromXmlData(XContentTypes* self, const uint8_t* data, size_t len);
bool XContentTypes_loadFromXmlFile(XContentTypes* self, const char* filePath);

#ifdef __cplusplus
}
#endif
#endif /* XCONTENTTYPES_H */
