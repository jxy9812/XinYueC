/******************************************************************************
 * @file       XStyles.h
 * @brief      XStyles OOXML 样式管理器类
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSTYLES_H
#define XSTYLES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XByteArray.h"
#include "XColor.h"
#include "XVector.h"
#include "XMap.h"
#include "XFormat.h"
#include "XAbstractOOXmlFile.h"
typedef struct XlsxFormatNumberData { int m_formatIndex; XString* m_formatString; } XlsxFormatNumberData;
typedef struct XStyles {
    XAbstractOOXmlFile m_base;
    XVector* m_fontsList;
    XVector* m_fillsList;
    XVector* m_bordersList;
    XVector* m_xfFormatsList;
    XVector* m_dxfFormatsList;
    XMap* m_customNumFmtIdMap;
    int m_nextCustomNumFmtId;
    XColor m_indexedColors[64];
    bool m_emptyFormatAdded;
} XStyles;
XStyles* XStyles_create(XAbstractOOXmlFile_CreateFlag flag);
void XStyles_delete(XStyles* self);
void XStyles_addXfFormat(XStyles* self, const XFormat* format, bool force);
XFormat* XStyles_xfFormat(XStyles* self, int idx);
void XStyles_addDxfFormat(XStyles* self, const XFormat* format, bool force);
XFormat* XStyles_dxfFormat(XStyles* self, int idx);
XColor XStyles_getColorByIndex(XStyles* self, int idx);
#ifdef __cplusplus
}
#endif
#endif
