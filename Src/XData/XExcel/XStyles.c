#include "XStyles.h"
#include "XMemory.h"
#include <stdlib.h>

#include "XByteArray.h"
#include <string.h>


XStyles* XStyles_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XStyles* self = (XStyles*)XMalloc_System(sizeof(XStyles));
    if (!self) return NULL;
    memset(self, 0, sizeof(XStyles));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_fontsList = XVector_create(sizeof(XFormat*));
    self->m_fillsList = XVector_create(sizeof(XFormat*));
    self->m_bordersList = XVector_create(sizeof(XFormat*));
    self->m_xfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_dxfFormatsList = XVector_create(sizeof(XFormat*));
    self->m_customNumFmtIdMap = XMap_create_ex(sizeof(int), sizeof(int), int_compare, false);
    self->m_nextCustomNumFmtId = 164;
    for (int i = 0; i < 64; i++) self->m_indexedColors[i] = XColor_create_rgb(0, 0, 0, 0);
    return self;
}

void XStyles_delete(XStyles* self)
{
    if (!self) return;
    if (self->m_fontsList) { XVector_deinit_base(self->m_fontsList); XFree_System(self->m_fontsList); }
    if (self->m_fillsList) { XVector_deinit_base(self->m_fillsList); XFree_System(self->m_fillsList); }
    if (self->m_bordersList) { XVector_deinit_base(self->m_bordersList); XFree_System(self->m_bordersList); }
    if (self->m_xfFormatsList) { XVector_deinit_base(self->m_xfFormatsList); XFree_System(self->m_xfFormatsList); }
    if (self->m_dxfFormatsList) { XVector_deinit_base(self->m_dxfFormatsList); XFree_System(self->m_dxfFormatsList); }
    if (self->m_customNumFmtIdMap) { XMap_deinit_base(self->m_customNumFmtIdMap); XFree_System(self->m_customNumFmtIdMap); }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

void XStyles_addXfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self) return;
    XFormat** p = (XFormat**)XVector_emplace_back(self->m_xfFormatsList);
    if (p) { *p = XFormat_create(); if (*p && format) XFormat_copy(*p, format); }
}

XFormat* XStyles_xfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_xfFormatsList || idx < 0) return NULL;
    size_t count = XVector_size_base(self->m_xfFormatsList);
    if ((size_t)idx >= count) return NULL;
    return *(XFormat**)XVector_at_base(self->m_xfFormatsList, idx);
}

void XStyles_addDxfFormat(XStyles* self, const XFormat* format, bool force)
{
    if (!self) return;
    XFormat** p = (XFormat**)XVector_emplace_back(self->m_dxfFormatsList);
    if (p) { *p = XFormat_create(); if (*p && format) XFormat_copy(*p, format); }
}

XFormat* XStyles_dxfFormat(XStyles* self, int idx)
{
    if (!self || !self->m_dxfFormatsList || idx < 0) return NULL;
    size_t count = XVector_size_base(self->m_dxfFormatsList);
    if ((size_t)idx >= count) return NULL;
    return *(XFormat**)XVector_at_base(self->m_dxfFormatsList, idx);
}

XColor XStyles_getColorByIndex(XStyles* self, int idx)
{
    if (self && idx >= 0 && idx < 64) return self->m_indexedColors[idx];
    return XColor_create_rgb(0, 0, 0, 0);
}

/* ========== XML 序列化 ========== */

/* 辅助：字体脚本枚举转字符串 */
static const char* fontScriptToStr(XFormat_FontScript script) {
    switch (script) {
        case XFormat_FontScriptSuper: return "superscript";
        case XFormat_FontScriptSub: return "subscript";
        default: return "baseline";
    }
}

/* 辅助：下划线枚举转字符串 */
static const char* underlineToStr(XFormat_FontUnderline ul) {
    switch (ul) {
        case XFormat_FontUnderlineSingle: return "single";
        case XFormat_FontUnderlineDouble: return "double";
        case XFormat_FontUnderlineSingleAccounting: return "singleAccounting";
        case XFormat_FontUnderlineDoubleAccounting: return "doubleAccounting";
        default: return "none";
    }
}

/* 辅助：边框样式枚举转字符串 */
static const char* borderStyleToStr(XFormat_BorderStyle style) {
    switch (style) {
        case XFormat_BorderThin: return "thin";
        case XFormat_BorderMedium: return "medium";
        case XFormat_BorderDashed: return "dashed";
        case XFormat_BorderDotted: return "dotted";
        case XFormat_BorderThick: return "thick";
        case XFormat_BorderDouble: return "double";
        case XFormat_BorderHair: return "hair";
        case XFormat_BorderMediumDashed: return "mediumDashed";
        case XFormat_BorderDashDot: return "dashDot";
        case XFormat_BorderMediumDashDot: return "mediumDashDot";
        case XFormat_BorderDashDotDot: return "dashDotDot";
        case XFormat_BorderMediumDashDotDot: return "mediumDashDotDot";
        case XFormat_BorderSlantDashDot: return "slantDashDot";
        default: return "none";
    }
}

/* 辅助：填充图案枚举转字符串 */
static const char* fillPatternToStr(XFormat_FillPattern pattern) {
    switch (pattern) {
        case XFormat_PatternSolid: return "solid";
        case XFormat_PatternMediumGray: return "mediumGray";
        case XFormat_PatternDarkGray: return "darkGray";
        case XFormat_PatternLightGray: return "lightGray";
        case XFormat_PatternDarkHorizontal: return "darkHorizontal";
        case XFormat_PatternDarkVertical: return "darkVertical";
        case XFormat_PatternDarkDown: return "darkDown";
        case XFormat_PatternDarkUp: return "darkUp";
        case XFormat_PatternDarkGrid: return "darkGrid";
        case XFormat_PatternDarkTrellis: return "darkTrellis";
        case XFormat_PatternLightHorizontal: return "lightHorizontal";
        case XFormat_PatternLightVertical: return "lightVertical";
        case XFormat_PatternLightDown: return "lightDown";
        case XFormat_PatternLightUp: return "lightUp";
        case XFormat_PatternLightTrellis: return "lightTrellis";
        case XFormat_PatternGray125: return "gray125";
        case XFormat_PatternGray0625: return "gray0625";
        case XFormat_PatternLightGrid: return "lightGrid";
        default: return "none";
    }
}

bool XStyles_saveToXmlData(const XStyles* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n");
    
    /* 写入 fonts */
    XByteArray_append_utf8(buf, "  <fonts count=\"");
    char countStr[32];
    int fontCount = self->m_fontsList ? (int)XVector_size_base(self->m_fontsList) : 0;
    snprintf(countStr, sizeof(countStr), "%d\">\n", fontCount > 0 ? fontCount : 1);
    XByteArray_append_utf8(buf, countStr);
    
    /* 至少写入一个默认字体 */
    if (fontCount == 0) {
        XByteArray_append_utf8(buf, "    <font/>\n");
    } else {
        for (int i = 0; i < fontCount; i++) {
            XFormat** fmtPtr = (XFormat**)XVector_at_base(self->m_fontsList, i);
            XFormat* fmt = fmtPtr ? *fmtPtr : NULL;
            XByteArray_append_utf8(buf, "    <font>");
            if (fmt) {
                int sz = XFormat_fontSize(fmt);
                if (sz > 0) {
                    snprintf(countStr, sizeof(countStr), "<sz val=\"%d\"/>", sz);
                    XByteArray_append_utf8(buf, countStr);
                }
                if (XFormat_fontBold(fmt)) XByteArray_append_utf8(buf, "<b/>");
                if (XFormat_fontItalic(fmt)) XByteArray_append_utf8(buf, "<i/>");
                if (XFormat_fontStrikeOut(fmt)) XByteArray_append_utf8(buf, "<strike/>");
                XFormat_FontUnderline ul = XFormat_fontUnderline(fmt);
                if (ul != XFormat_FontUnderlineNone) {
                    snprintf(countStr, sizeof(countStr), "<u val=\"%s\"/>", underlineToStr(ul));
                    XByteArray_append_utf8(buf, countStr);
                }
                const char* name = XFormat_fontName(fmt);
                if (name && strlen(name) > 0) {
                    XByteArray_append_utf8(buf, "<name val=\"");
                    XByteArray_append_utf8(buf, name);
                    XByteArray_append_utf8(buf, "\"/>");
                }
            }
            XByteArray_append_utf8(buf, "</font>\n");
        }
    }
    XByteArray_append_utf8(buf, "  </fonts>\n");
    
    /* 写入 fills */
    int fillCount = self->m_fillsList ? (int)XVector_size_base(self->m_fillsList) : 0;
    XByteArray_append_utf8(buf, "  <fills count=\"");
    snprintf(countStr, sizeof(countStr), "%d\">\n", fillCount > 0 ? fillCount : 2);
    XByteArray_append_utf8(buf, countStr);
    if (fillCount == 0) {
        XByteArray_append_utf8(buf, "    <fill><patternFill patternType=\"none\"/></fill>\n");
        XByteArray_append_utf8(buf, "    <fill><patternFill patternType=\"gray125\"/></fill>\n");
    } else {
        for (int i = 0; i < fillCount; i++) {
            XFormat** fmtPtr = (XFormat**)XVector_at_base(self->m_fillsList, i);
            XFormat* fmt = fmtPtr ? *fmtPtr : NULL;
            XByteArray_append_utf8(buf, "    <fill>");
            XByteArray_append_utf8(buf, "<patternFill patternType=\"");
            if (fmt) XByteArray_append_utf8(buf, fillPatternToStr(XFormat_fillPattern(fmt)));
            else XByteArray_append_utf8(buf, "none");
            XByteArray_append_utf8(buf, "\"/>");
            XByteArray_append_utf8(buf, "</fill>\n");
        }
    }
    XByteArray_append_utf8(buf, "  </fills>\n");
    
    /* 写入 borders */
    int borderCount = self->m_bordersList ? (int)XVector_size_base(self->m_bordersList) : 0;
    XByteArray_append_utf8(buf, "  <borders count=\"");
    snprintf(countStr, sizeof(countStr), "%d\">\n", borderCount > 0 ? borderCount : 1);
    XByteArray_append_utf8(buf, countStr);
    if (borderCount == 0) {
        XByteArray_append_utf8(buf, "    <border><left/><right/><top/><bottom/><diagonal/></border>\n");
    } else {
        for (int i = 0; i < borderCount; i++) {
            XFormat** fmtPtr = (XFormat**)XVector_at_base(self->m_bordersList, i);
            XByteArray_append_utf8(buf, "    <border>");
            XByteArray_append_utf8(buf, "<left/><right/><top/><bottom/><diagonal/>");
            XByteArray_append_utf8(buf, "</border>\n");
        }
    }
    XByteArray_append_utf8(buf, "  </borders>\n");
    
    /* 写入 cellStyleXfs (至少一个默认) */
    int xfCount = self->m_xfFormatsList ? (int)XVector_size_base(self->m_xfFormatsList) : 0;
    XByteArray_append_utf8(buf, "  <cellStyleXfs count=\"");
    snprintf(countStr, sizeof(countStr), "%d\">\n", xfCount > 0 ? xfCount : 1);
    XByteArray_append_utf8(buf, countStr);
    XByteArray_append_utf8(buf, "    <xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/>\n");
    XByteArray_append_utf8(buf, "  </cellStyleXfs>\n");
    
    /* 写入 cellXfs */
    XByteArray_append_utf8(buf, "  <cellXfs count=\"");
    snprintf(countStr, sizeof(countStr), "%d\">\n", xfCount > 0 ? xfCount : 1);
    XByteArray_append_utf8(buf, countStr);
    XByteArray_append_utf8(buf, "    <xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>\n");
    XByteArray_append_utf8(buf, "  </cellXfs>\n");
    
    /* 写入 dxfs (差分格式) */
    int dxfCount = self->m_dxfFormatsList ? (int)XVector_size_base(self->m_dxfFormatsList) : 0;
    XByteArray_append_utf8(buf, "  <dxfs count=\"");
    snprintf(countStr, sizeof(countStr), "%d\">\n", dxfCount);
    XByteArray_append_utf8(buf, countStr);
    XByteArray_append_utf8(buf, "  </dxfs>\n");
    
    /* 写入 tableStyles */
    XByteArray_append_utf8(buf, "  <tableStyles count=\"0\"/>\n");
    
    XByteArray_append_utf8(buf, "</styleSheet>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base(buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base(buf));
        *outLen = XByteArray_size_base(buf);
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

bool XStyles_saveToXmlFile(XStyles* self, const char* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XStyles_saveToXmlData(self, &data, &len)) return false;
    
    FILE* fp = fopen(filePath, "wb");
    if (!fp) { XFree_System(data); return false; }
    fwrite(data, 1, len, fp);
    fclose(fp);
    XFree_System(data);
    return true;
}

bool XStyles_loadFromXmlData(XStyles* self, const uint8_t* data, size_t len) {
    (void)self; (void)data; (void)len;
    /* 样式解析较复杂，暂时保留基本实现 */
    return true;
}

bool XStyles_loadFromXmlFile(XStyles* self, const char* filePath) {
    (void)self; (void)filePath;
    return true;
}
