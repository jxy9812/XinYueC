/******************************************************************************
 * @file       XFormat.c
 * @brief      XFormat 单元格格式类实现（对标 QXlsx::Format）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFormat.h"
#include "XMemory.h"
#include "XString.h"
#include "XColor.h"
#include "XFont.h"
#include "XMap.h"
#include "XPair.h"
#include <stdlib.h>

#include <string.h>

#include <stdio.h>


/* XMap 存储方案：键为 int（属性ID），值为 int
 * 对于 bool：存储 0/1
 * 对于字符串：存储为 intptr_t（指针转型）
 * 对于颜色：存储为 ARGB 打包 int
 */

/* ========== 辅助函数：获取/设置属性 ========== */

static int getPropertyInt(const XFormat* self, int propertyId, int defaultValue)
{
    if (!self || !self->m_properties) return defaultValue;
    XMap_iterator it;
    if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) return *(int*)XPair_second(pair);
    }
    return defaultValue;
}

static void setPropertyInt(XFormat* self, int propertyId, int value)
{
    if (!self) return;
    if (!self->m_properties)
    {
        self->m_properties = XMap_Create(int, intptr_t, int_compare);
        if (!self->m_properties) return;
    }
    intptr_t val = value;
    XMapBase_insert_base((XMapBase*)self->m_properties, &propertyId, &val);
    self->m_dirty = true;
}

static bool getPropertyBool(const XFormat* self, int propertyId, bool defaultValue)
{
    return getPropertyInt(self, propertyId, defaultValue ? 1 : 0) != 0;
}

static void setPropertyBool(XFormat* self, int propertyId, bool value)
{
    setPropertyInt(self, propertyId, value ? 1 : 0);
}

/* 颜色打包/解包 */
static int packColor(const XColor* c)
{
    if (!c) return 0;
    return ((int)c->m_alpha << 24) | ((int)c->m_comp1 << 16) | ((int)c->m_comp2 << 8) | (int)c->m_comp3;
}

static XColor unpackColor(int packed)
{
    XColor c;
    c = XColor_create_rgb(        (uint8_t)((packed >> 16) & 0xFF),
        (uint8_t)((packed >> 8) & 0xFF),
        (uint8_t)(packed & 0xFF),
        (uint8_t)((packed >> 24) & 0xFF));
    return c;
}

static XColor getPropertyColor(const XFormat* self, int propertyId)
{
    int packed = getPropertyInt(self, propertyId, 0);
    return unpackColor(packed);
}

static void setPropertyColor(XFormat* self, int propertyId, const XColor* color)
{
    if (color) setPropertyInt(self, propertyId, packColor(color));
}

/* 字符串属性存储：使用 intptr_t 存储 XString* 指针 */
static const XString* getPropertyXString(const XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return NULL;
    XMap_iterator it;
    if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            intptr_t ptr = *(intptr_t*)XPair_second(pair);
            if (ptr) return (const XString*)ptr;
        }
    }
    return NULL;
}

static const char* getPropertyString(const XFormat* self, int propertyId)
{
    const XString* s = getPropertyXString(self, propertyId);
    return s ? XString_toUtf8(s) : "";
}

static void setPropertyString(XFormat* self, int propertyId, const char* value)
{
    if (!self || !value) return;
    if (!self->m_properties)
    {
        self->m_properties = XMap_Create(int, intptr_t, int_compare);
        if (!self->m_properties) return;
    }
    /* 先释放旧字符串 */
    {
        XMap_iterator it;
        if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                intptr_t oldPtr = *(intptr_t*)XPair_second(pair);
                if (oldPtr) { XString_deinit_base((XString*)oldPtr); XFree_System((XString*)oldPtr); }
            }
        }
    }
    XString* str = XString_create();
    if (str) XString_append_utf8(str, value);
    intptr_t ptr = (intptr_t)str;
    intptr_t val = ptr;
    XMapBase_insert_base((XMapBase*)self->m_properties, &propertyId, &val);
    self->m_dirty = true;
}

static bool hasProperty(const XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return false;
    XMap_iterator it;
    return XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it);
}

/* 释放所有字符串属性 */
static void freeStringProperties(XFormat* self)
{
    if (!self || !self->m_properties) return;
    int stringProps[] = {
        XFormat_P_NumFmt_FormatCode,
        XFormat_P_Font_Name,
        -1
    };
    for (int i = 0; stringProps[i] >= 0; i++)
    {
        XMap_iterator it;
        if (XMapBase_find_base((XMapBase*)self->m_properties, &stringProps[i], (XMapBase_iterator*)&it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                intptr_t ptr = *(intptr_t*)XPair_second(pair);
                if (ptr) { XString_deinit_base((XString*)ptr); XFree_System((XString*)ptr); }
            }
        }
    }
}

/* ========== 创建与初始化 ========== */

XFormat* XFormat_create(void)
{
    XFormat* self = (XFormat*)XMalloc_System(sizeof(XFormat));
    if (!self) return NULL;
    memset(self, 0, sizeof(XFormat));
    self->m_fontIndex = -1;
    self->m_borderIndex = -1;
    self->m_fillIndex = -1;
    self->m_xfIndex = -1;
    self->m_dxfIndex = -1;
    self->m_theme = -1;
    return self;
}

void XFormat_copy(XFormat* self, const XFormat* other)
{
    if (!self || !other) return;
    if (other->m_properties)
    {
        if (!self->m_properties)
            self->m_properties = XMap_Create(int, intptr_t, int_compare);
        if (self->m_properties)
        {
            XMap_clear_base((XMapBase*)self->m_properties);
            /* 复制所有属性 */
            XMap_iterator it = XMap_begin(other->m_properties);
            XMap_iterator end = XMap_end(other->m_properties);
            while (!XMap_iterator_isEnd(&it))
            {
                XPair* pair = XMap_iterator_data(&it);
                if (pair)
                {
                    int key = *(int*)XPair_first(pair);
                    intptr_t val = *(intptr_t*)XPair_second(pair);
                    /* 如果是字符串属性，深拷贝 */
                    if (key == XFormat_P_NumFmt_FormatCode || key == XFormat_P_Font_Name)
                    {
                        intptr_t oldPtr = (intptr_t)val;
                        if (oldPtr)
                        {
                            XString* str = XString_create();
                            if (str) XString_append_utf8(str, XString_toUtf8((XString*)oldPtr));
                            intptr_t newPtr = (intptr_t)str;
                            val = (int)newPtr;
                        }
                    }
                    XMapBase_insert_base((XMapBase*)self->m_properties, &key, &val);
                }
                XMap_iterator_add(other->m_properties, &it);
            }
        }
    }
    self->m_fontIndex = other->m_fontIndex;
    self->m_borderIndex = other->m_borderIndex;
    self->m_fillIndex = other->m_fillIndex;
    self->m_xfIndex = other->m_xfIndex;
    self->m_dxfIndex = other->m_dxfIndex;
    self->m_theme = other->m_theme;
    self->m_fontIndexValid = other->m_fontIndexValid;
    self->m_borderIndexValid = other->m_borderIndexValid;
    self->m_fillIndexValid = other->m_fillIndexValid;
    self->m_xfIndexValid = other->m_xfIndexValid;
    self->m_dxfIndexValid = other->m_dxfIndexValid;
    self->m_isDxfFormat = other->m_isDxfFormat;
    self->m_dirty = other->m_dirty;
}

void XFormat_delete(XFormat* self)
{
    if (self)
    {
        if (self->m_properties)
        {
            freeStringProperties(self);
            XMap_deinit_base((XMapBase*)self->m_properties);
            XFree_System(self->m_properties);
        }
        XFree_System(self);
    }
}

/* ========== 数字格式 ========== */

int XFormat_numberFormatIndex(const XFormat* self)
{
    return getPropertyInt(self, XFormat_P_NumFmt_Id, 0);
}

void XFormat_setNumberFormatIndex(XFormat* self, int format)
{
    setPropertyInt(self, XFormat_P_NumFmt_Id, format);
}

const XString* XFormat_numberFormat(const XFormat* self)
{
    return getPropertyXString(self, XFormat_P_NumFmt_FormatCode);
}

void XFormat_setNumberFormat(XFormat* self, const XString* format)
{
    if (format)
        setPropertyString(self, XFormat_P_NumFmt_FormatCode, XString_toUtf8(format));
}

void XFormat_setNumberFormat_ex(XFormat* self, int id, const XString* format)
{
    XFormat_setNumberFormatIndex(self, id);
    XFormat_setNumberFormat(self, format);
}

bool XFormat_isDateTimeFormat(const XFormat* self)
{
    int id = XFormat_numberFormatIndex(self);
    /* 日期时间格式 ID 范围：14-22, 27-36, 45-47, 50-58 */
    if ((id >= 14 && id <= 22) || (id >= 27 && id <= 36) ||
        (id >= 45 && id <= 47) || (id >= 50 && id <= 58))
        return true;
    const XString* fmtX = XFormat_numberFormat(self);
    const char* fmt = fmtX ? XString_toUtf8(fmtX) : NULL;
    if (fmt && *fmt)
    {
        if (strstr(fmt, "y") || strstr(fmt, "m") || strstr(fmt, "d") ||
            strstr(fmt, "h") || strstr(fmt, "s") || strstr(fmt, "Y") ||
            strstr(fmt, "M") || strstr(fmt, "D") || strstr(fmt, "H") ||
            strstr(fmt, "S"))
            return true;
    }
    return false;
}

/* ========== 字体属性 ========== */

int XFormat_fontSize(const XFormat* self)
{
    return getPropertyInt(self, XFormat_P_Font_Size, 0);
}

void XFormat_setFontSize(XFormat* self, int size)
{
    setPropertyInt(self, XFormat_P_Font_Size, size);
}

bool XFormat_fontItalic(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Font_Italic, false);
}

void XFormat_setFontItalic(XFormat* self, bool italic)
{
    setPropertyBool(self, XFormat_P_Font_Italic, italic);
}

bool XFormat_fontStrikeOut(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Font_StrikeOut, false);
}

void XFormat_setFontStrikeOut(XFormat* self, bool strikeOut)
{
    setPropertyBool(self, XFormat_P_Font_StrikeOut, strikeOut);
}

XColor XFormat_fontColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Font_Color);
}

void XFormat_setFontColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Font_Color, color);
}

bool XFormat_fontBold(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Font_Bold, false);
}

void XFormat_setFontBold(XFormat* self, bool bold)
{
    setPropertyBool(self, XFormat_P_Font_Bold, bold);
}

XFormat_FontScript XFormat_fontScript(const XFormat* self)
{
    return (XFormat_FontScript)getPropertyInt(self, XFormat_P_Font_Script, XFormat_FontScriptNormal);
}

void XFormat_setFontScript(XFormat* self, XFormat_FontScript script)
{
    setPropertyInt(self, XFormat_P_Font_Script, (int)script);
}

XFormat_FontUnderline XFormat_fontUnderline(const XFormat* self)
{
    return (XFormat_FontUnderline)getPropertyInt(self, XFormat_P_Font_Underline, XFormat_FontUnderlineNone);
}

void XFormat_setFontUnderline(XFormat* self, XFormat_FontUnderline underline)
{
    setPropertyInt(self, XFormat_P_Font_Underline, (int)underline);
}

bool XFormat_fontOutline(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Font_Outline, false);
}

void XFormat_setFontOutline(XFormat* self, bool outline)
{
    setPropertyBool(self, XFormat_P_Font_Outline, outline);
}

bool XFormat_fontShadow(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Font_Shadow, false);
}

void XFormat_setFontShadow(XFormat* self, bool shadow)
{
    setPropertyBool(self, XFormat_P_Font_Shadow, shadow);
}

const XString* XFormat_fontName(const XFormat* self)
{
    return getPropertyXString(self, XFormat_P_Font_Name);
}

void XFormat_setFontName(XFormat* self, const XString* name)
{
    if (name) setPropertyString(self, XFormat_P_Font_Name, XString_toUtf8(name));
}

/* ========== 对齐属性 ========== */

XFormat_HorizontalAlignment XFormat_horizontalAlignment(const XFormat* self)
{
    return (XFormat_HorizontalAlignment)getPropertyInt(self, XFormat_P_Alignment_AlignH, XFormat_AlignHGeneral);
}

void XFormat_setHorizontalAlignment(XFormat* self, XFormat_HorizontalAlignment align)
{
    setPropertyInt(self, XFormat_P_Alignment_AlignH, (int)align);
}

XFormat_VerticalAlignment XFormat_verticalAlignment(const XFormat* self)
{
    return (XFormat_VerticalAlignment)getPropertyInt(self, XFormat_P_Alignment_AlignV, XFormat_AlignBottom);
}

void XFormat_setVerticalAlignment(XFormat* self, XFormat_VerticalAlignment align)
{
    setPropertyInt(self, XFormat_P_Alignment_AlignV, (int)align);
}

bool XFormat_textWrap(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Alignment_Wrap, false);
}

void XFormat_setTextWrap(XFormat* self, bool wrap)
{
    setPropertyBool(self, XFormat_P_Alignment_Wrap, wrap);
}

int XFormat_rotation(const XFormat* self)
{
    return getPropertyInt(self, XFormat_P_Alignment_Rotation, 0);
}

void XFormat_setRotation(XFormat* self, int rotation)
{
    setPropertyInt(self, XFormat_P_Alignment_Rotation, rotation);
}

int XFormat_indent(const XFormat* self)
{
    return getPropertyInt(self, XFormat_P_Alignment_Indent, 0);
}

void XFormat_setIndent(XFormat* self, int indent)
{
    setPropertyInt(self, XFormat_P_Alignment_Indent, indent);
}

bool XFormat_shrinkToFit(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Alignment_ShinkToFit, false);
}

void XFormat_setShrinkToFit(XFormat* self, bool shrink)
{
    setPropertyBool(self, XFormat_P_Alignment_ShinkToFit, shrink);
}

/* ========== 边框属性 ========== */

void XFormat_setBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    XFormat_setLeftBorderStyle(self, style);
    XFormat_setRightBorderStyle(self, style);
    XFormat_setTopBorderStyle(self, style);
    XFormat_setBottomBorderStyle(self, style);
}

void XFormat_setBorderColor(XFormat* self, const XColor* color)
{
    if (color)
    {
        XFormat_setLeftBorderColor(self, color);
        XFormat_setRightBorderColor(self, color);
        XFormat_setTopBorderColor(self, color);
        XFormat_setBottomBorderColor(self, color);
    }
}

XFormat_BorderStyle XFormat_leftBorderStyle(const XFormat* self)
{
    return (XFormat_BorderStyle)getPropertyInt(self, XFormat_P_Border_LeftStyle, XFormat_BorderNone);
}

void XFormat_setLeftBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setPropertyInt(self, XFormat_P_Border_LeftStyle, (int)style);
}

XColor XFormat_leftBorderColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Border_LeftColor);
}

void XFormat_setLeftBorderColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Border_LeftColor, color);
}

XFormat_BorderStyle XFormat_rightBorderStyle(const XFormat* self)
{
    return (XFormat_BorderStyle)getPropertyInt(self, XFormat_P_Border_RightStyle, XFormat_BorderNone);
}

void XFormat_setRightBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setPropertyInt(self, XFormat_P_Border_RightStyle, (int)style);
}

XColor XFormat_rightBorderColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Border_RightColor);
}

void XFormat_setRightBorderColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Border_RightColor, color);
}

XFormat_BorderStyle XFormat_topBorderStyle(const XFormat* self)
{
    return (XFormat_BorderStyle)getPropertyInt(self, XFormat_P_Border_TopStyle, XFormat_BorderNone);
}

void XFormat_setTopBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setPropertyInt(self, XFormat_P_Border_TopStyle, (int)style);
}

XColor XFormat_topBorderColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Border_TopColor);
}

void XFormat_setTopBorderColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Border_TopColor, color);
}

XFormat_BorderStyle XFormat_bottomBorderStyle(const XFormat* self)
{
    return (XFormat_BorderStyle)getPropertyInt(self, XFormat_P_Border_BottomStyle, XFormat_BorderNone);
}

void XFormat_setBottomBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setPropertyInt(self, XFormat_P_Border_BottomStyle, (int)style);
}

XColor XFormat_bottomBorderColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Border_BottomColor);
}

void XFormat_setBottomBorderColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Border_BottomColor, color);
}

XFormat_BorderStyle XFormat_diagonalBorderStyle(const XFormat* self)
{
    return (XFormat_BorderStyle)getPropertyInt(self, XFormat_P_Border_DiagonalStyle, XFormat_BorderNone);
}

void XFormat_setDiagonalBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setPropertyInt(self, XFormat_P_Border_DiagonalStyle, (int)style);
}

XFormat_DiagonalBorderType XFormat_diagonalBorderType(const XFormat* self)
{
    return (XFormat_DiagonalBorderType)getPropertyInt(self, XFormat_P_Border_DiagonalType, XFormat_DiagonalBorderNone);
}

void XFormat_setDiagonalBorderType(XFormat* self, XFormat_DiagonalBorderType type)
{
    setPropertyInt(self, XFormat_P_Border_DiagonalType, (int)type);
}

XColor XFormat_diagonalBorderColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Border_DiagonalColor);
}

void XFormat_setDiagonalBorderColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Border_DiagonalColor, color);
}

/* ========== 填充属性 ========== */

XFormat_FillPattern XFormat_fillPattern(const XFormat* self)
{
    return (XFormat_FillPattern)getPropertyInt(self, XFormat_P_Fill_Pattern, XFormat_PatternNone);
}

void XFormat_setFillPattern(XFormat* self, XFormat_FillPattern pattern)
{
    setPropertyInt(self, XFormat_P_Fill_Pattern, (int)pattern);
}

XColor XFormat_patternForegroundColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Fill_FgColor);
}

void XFormat_setPatternForegroundColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Fill_FgColor, color);
}

XColor XFormat_patternBackgroundColor(const XFormat* self)
{
    return getPropertyColor(self, XFormat_P_Fill_BgColor);
}

void XFormat_setPatternBackgroundColor(XFormat* self, const XColor* color)
{
    setPropertyColor(self, XFormat_P_Fill_BgColor, color);
}

/* ========== 保护属性 ========== */

bool XFormat_locked(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Protection_Locked, true);
}

void XFormat_setLocked(XFormat* self, bool locked)
{
    setPropertyBool(self, XFormat_P_Protection_Locked, locked);
}

bool XFormat_hidden(const XFormat* self)
{
    return getPropertyBool(self, XFormat_P_Protection_Hidden, false);
}

void XFormat_setHidden(XFormat* self, bool hidden)
{
    setPropertyBool(self, XFormat_P_Protection_Hidden, hidden);
}

/* ========== 格式操作 ========== */

void XFormat_mergeFormat(XFormat* self, const XFormat* modifier)
{
    if (!self || !modifier || !modifier->m_properties) return;
    if (!self->m_properties)
    {
        self->m_properties = XMap_Create(int, intptr_t, int_compare);
        if (!self->m_properties) return;
    }
    XMap_iterator it = XMap_begin(modifier->m_properties);
    XMap_iterator end = XMap_end(modifier->m_properties);
    while (!XMap_iterator_isEnd(&it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            int key = *(int*)XPair_first(pair);
            intptr_t val = *(intptr_t*)XPair_second(pair);
            /* 如果是字符串属性，深拷贝 */
            if (key == XFormat_P_NumFmt_FormatCode || key == XFormat_P_Font_Name)
            {
                intptr_t oldPtr = val;  /* BUG FIX: 之前是 (intptr_t)val 多余 */
                if (oldPtr)
                {
                    XString* str = XString_create();
                    if (str) XString_append_utf8(str, XString_toUtf8((XString*)oldPtr));
                    val = (intptr_t)str;  /* BUG FIX: 之前是 (int)newPtr 会截断为 32 位 */
                }
            }
            XMapBase_insert_base((XMapBase*)self->m_properties, &key, &val);
        }
        XMap_iterator_add(modifier->m_properties, &it);
    }
    self->m_dirty = true;
}

bool XFormat_isValid(const XFormat* self)
{
    return self != NULL;
}

bool XFormat_isEmpty(const XFormat* self)
{
    if (!self) return true;
    if (self->m_properties && XMap_size_base((XMapBase*)self->m_properties) > 0) return false;
    return true;
}

/* ========== 属性访问（通用） ========== */

void* XFormat_property(const XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return NULL;
    XMap_iterator it;
    if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) return XPair_second(pair);
    }
    return NULL;
}

void XFormat_setProperty(XFormat* self, int propertyId, void* value)
{
    if (!self || !value) return;
    setPropertyInt(self, propertyId, *(int*)value);
}

void XFormat_clearProperty(XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return;
    /* 如果是字符串属性，先释放 */
    if (propertyId == XFormat_P_NumFmt_FormatCode || propertyId == XFormat_P_Font_Name)
    {
        XMap_iterator it;
        if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                intptr_t ptr = *(intptr_t*)XPair_second(pair);
                if (ptr) { XString_deinit_base((XString*)ptr); XFree_System((XString*)ptr); }
            }
        }
    }
    XMap_iterator it;
    if (XMapBase_find_base((XMapBase*)self->m_properties, &propertyId, (XMapBase_iterator*)&it))
    {
        XMapBase_erase_base((XMapBase*)self->m_properties, (XMapBase_iterator*)&it, NULL);
        self->m_dirty = true;
    }
}

bool XFormat_hasProperty(const XFormat* self, int propertyId)
{
    return hasProperty(self, propertyId);
}

/* ========== 键/索引管理 ========== */

bool XFormat_hasNumFmtData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_NumFmt_Id) ||
           hasProperty(self, XFormat_P_NumFmt_FormatCode);
}

bool XFormat_hasFontData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Font_Size) ||
           hasProperty(self, XFormat_P_Font_Italic) ||
           hasProperty(self, XFormat_P_Font_StrikeOut) ||
           hasProperty(self, XFormat_P_Font_Color) ||
           hasProperty(self, XFormat_P_Font_Bold) ||
           hasProperty(self, XFormat_P_Font_Script) ||
           hasProperty(self, XFormat_P_Font_Underline) ||
           hasProperty(self, XFormat_P_Font_Outline) ||
           hasProperty(self, XFormat_P_Font_Shadow) ||
           hasProperty(self, XFormat_P_Font_Name);
}

bool XFormat_hasFillData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Fill_Pattern) ||
           hasProperty(self, XFormat_P_Fill_FgColor) ||
           hasProperty(self, XFormat_P_Fill_BgColor);
}

bool XFormat_hasBorderData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Border_LeftStyle) ||
           hasProperty(self, XFormat_P_Border_RightStyle) ||
           hasProperty(self, XFormat_P_Border_TopStyle) ||
           hasProperty(self, XFormat_P_Border_BottomStyle) ||
           hasProperty(self, XFormat_P_Border_DiagonalStyle) ||
           hasProperty(self, XFormat_P_Border_LeftColor) ||
           hasProperty(self, XFormat_P_Border_RightColor) ||
           hasProperty(self, XFormat_P_Border_TopColor) ||
           hasProperty(self, XFormat_P_Border_BottomColor) ||
           hasProperty(self, XFormat_P_Border_DiagonalColor) ||
           hasProperty(self, XFormat_P_Border_DiagonalType);
}

bool XFormat_hasAlignmentData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Alignment_AlignH) ||
           hasProperty(self, XFormat_P_Alignment_AlignV) ||
           hasProperty(self, XFormat_P_Alignment_Wrap) ||
           hasProperty(self, XFormat_P_Alignment_Rotation) ||
           hasProperty(self, XFormat_P_Alignment_Indent) ||
           hasProperty(self, XFormat_P_Alignment_ShinkToFit);
}

bool XFormat_hasProtectionData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Protection_Locked) ||
           hasProperty(self, XFormat_P_Protection_Hidden);
}

bool XFormat_fontIndexValid(const XFormat* self)
{
    return self ? self->m_fontIndexValid : false;
}

int XFormat_fontIndex(const XFormat* self)
{
    return self ? self->m_fontIndex : -1;
}

bool XFormat_borderIndexValid(const XFormat* self)
{
    return self ? self->m_borderIndexValid : false;
}

int XFormat_borderIndex(const XFormat* self)
{
    return self ? self->m_borderIndex : -1;
}

bool XFormat_fillIndexValid(const XFormat* self)
{
    return self ? self->m_fillIndexValid : false;
}

int XFormat_fillIndex(const XFormat* self)
{
    return self ? self->m_fillIndex : -1;
}

bool XFormat_xfIndexValid(const XFormat* self)
{
    return self ? self->m_xfIndexValid : false;
}

int XFormat_xfIndex(const XFormat* self)
{
    return self ? self->m_xfIndex : -1;
}

bool XFormat_dxfIndexValid(const XFormat* self)
{
    return self ? self->m_dxfIndexValid : false;
}

int XFormat_dxfIndex(const XFormat* self)
{
    return self ? self->m_dxfIndex : -1;
}

void XFormat_setFontIndex(XFormat* self, int index)
{
    if (self) { self->m_fontIndex = index; self->m_fontIndexValid = true; }
}

void XFormat_setBorderIndex(XFormat* self, int index)
{
    if (self) { self->m_borderIndex = index; self->m_borderIndexValid = true; }
}

void XFormat_setFillIndex(XFormat* self, int index)
{
    if (self) { self->m_fillIndex = index; self->m_fillIndexValid = true; }
}

void XFormat_setXfIndex(XFormat* self, int index)
{
    if (self) { self->m_xfIndex = index; self->m_xfIndexValid = true; }
}

void XFormat_setDxfIndex(XFormat* self, int index)
{
    if (self) { self->m_dxfIndex = index; self->m_dxfIndexValid = true; }
}

/* ========== 键生成（用于样式去重） ========== */

/* 简单键生成：将相关属性序列化为字节数组 */
static void appendInt(uint8_t** buf, size_t* len, size_t* cap, int val)
{
    if (*len + 4 > *cap) { *cap = (*cap + 4) * 2; *buf = (uint8_t*)XRealloc_System(*buf, *cap); }
    memcpy(*buf + *len, &val, 4); *len += 4;
}

void XFormat_fontKey(const XFormat* self, uint8_t** outKey, size_t* outLen)
{
    size_t cap = 64, len = 0;
    uint8_t* buf = (uint8_t*)XMalloc_System(cap);
    if (!buf) { *outKey = NULL; *outLen = 0; return; }
    appendInt(&buf, &len, &cap, XFormat_fontSize(self));
    appendInt(&buf, &len, &cap, XFormat_fontBold(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, XFormat_fontItalic(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, XFormat_fontStrikeOut(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, (int)XFormat_fontScript(self));
    appendInt(&buf, &len, &cap, (int)XFormat_fontUnderline(self));
    appendInt(&buf, &len, &cap, XFormat_fontOutline(self) ? 1 : 0);
    XColor fc = XFormat_fontColor(self);
    appendInt(&buf, &len, &cap, packColor(&fc));
    /* 字体名称 */
    const XString* nameX = XFormat_fontName(self);
    const char* name = nameX ? XString_toUtf8(nameX) : NULL;
    int nameLen = name ? (int)strlen(name) : 0;
    appendInt(&buf, &len, &cap, nameLen);
    if (nameLen > 0) {
        if (len + (size_t)nameLen > cap) { cap = (len + nameLen) * 2; buf = (uint8_t*)XRealloc_System(buf, cap); }
        memcpy(buf + len, name, nameLen); len += nameLen;
    }
    *outKey = buf; *outLen = len;
}

void XFormat_borderKey(const XFormat* self, uint8_t** outKey, size_t* outLen)
{
    size_t cap = 64, len = 0;
    uint8_t* buf = (uint8_t*)XMalloc_System(cap);
    if (!buf) { *outKey = NULL; *outLen = 0; return; }
    appendInt(&buf, &len, &cap, (int)XFormat_leftBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_rightBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_topBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_bottomBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_diagonalBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_diagonalBorderType(self));
    XColor lc = XFormat_leftBorderColor(self); appendInt(&buf, &len, &cap, packColor(&lc));
    XColor rc = XFormat_rightBorderColor(self); appendInt(&buf, &len, &cap, packColor(&rc));
    XColor tc = XFormat_topBorderColor(self); appendInt(&buf, &len, &cap, packColor(&tc));
    XColor bc = XFormat_bottomBorderColor(self); appendInt(&buf, &len, &cap, packColor(&bc));
    XColor dc = XFormat_diagonalBorderColor(self); appendInt(&buf, &len, &cap, packColor(&dc));
    *outKey = buf; *outLen = len;
}

void XFormat_fillKey(const XFormat* self, uint8_t** outKey, size_t* outLen)
{
    size_t cap = 32, len = 0;
    uint8_t* buf = (uint8_t*)XMalloc_System(cap);
    if (!buf) { *outKey = NULL; *outLen = 0; return; }
    appendInt(&buf, &len, &cap, (int)XFormat_fillPattern(self));
    XColor fg = XFormat_patternForegroundColor(self); appendInt(&buf, &len, &cap, packColor(&fg));
    XColor bg = XFormat_patternBackgroundColor(self); appendInt(&buf, &len, &cap, packColor(&bg));
    *outKey = buf; *outLen = len;
}

void XFormat_formatKey(const XFormat* self, uint8_t** outKey, size_t* outLen)
{
    size_t cap = 128, len = 0;
    uint8_t* buf = (uint8_t*)XMalloc_System(cap);
    if (!buf) { *outKey = NULL; *outLen = 0; return; }
    /* 数字格式 */
    appendInt(&buf, &len, &cap, XFormat_numberFormatIndex(self));
    const XString* nfX = XFormat_numberFormat(self);
    const char* nf = nfX ? XString_toUtf8(nfX) : NULL;
    int nfLen = nf ? (int)strlen(nf) : 0;
    appendInt(&buf, &len, &cap, nfLen);
    if (nfLen > 0) {
        if (len + (size_t)nfLen > cap) { cap = (len + nfLen) * 2; buf = (uint8_t*)XRealloc_System(buf, cap); }
        memcpy(buf + len, nf, nfLen); len += nfLen;
    }
    /* 字体 */
    appendInt(&buf, &len, &cap, XFormat_fontSize(self));
    appendInt(&buf, &len, &cap, XFormat_fontBold(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, XFormat_fontItalic(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, (int)XFormat_fontUnderline(self));
    /* 对齐 */
    appendInt(&buf, &len, &cap, (int)XFormat_horizontalAlignment(self));
    appendInt(&buf, &len, &cap, (int)XFormat_verticalAlignment(self));
    appendInt(&buf, &len, &cap, XFormat_textWrap(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, XFormat_rotation(self));
    appendInt(&buf, &len, &cap, XFormat_indent(self));
    appendInt(&buf, &len, &cap, XFormat_shrinkToFit(self) ? 1 : 0);
    /* 填充 */
    appendInt(&buf, &len, &cap, (int)XFormat_fillPattern(self));
    /* 边框 */
    appendInt(&buf, &len, &cap, (int)XFormat_leftBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_rightBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_topBorderStyle(self));
    appendInt(&buf, &len, &cap, (int)XFormat_bottomBorderStyle(self));
    /* 保护 */
    appendInt(&buf, &len, &cap, XFormat_locked(self) ? 1 : 0);
    appendInt(&buf, &len, &cap, XFormat_hidden(self) ? 1 : 0);
    *outKey = buf; *outLen = len;
}

/* ========== 数字格式修正 ========== */

void XFormat_fixNumberFormat(XFormat* self, int id, const XString* format)
{
    if (!self) return;
    setPropertyInt(self, XFormat_P_NumFmt_Id, id);
    if (format) setPropertyString(self, XFormat_P_NumFmt_FormatCode, XString_toUtf8(format));
    self->m_dirty = true;
}

/* ========== 主题 ========== */

int XFormat_theme(const XFormat* self)
{
    return self ? self->m_theme : 0;
}

/* ========== 类型化属性访问 ========== */

bool XFormat_boolProperty(const XFormat* self, int propertyId, bool defaultValue)
{
    return getPropertyBool(self, propertyId, defaultValue);
}

int XFormat_intProperty(const XFormat* self, int propertyId, int defaultValue)
{
    return getPropertyInt(self, propertyId, defaultValue);
}

double XFormat_doubleProperty(const XFormat* self, int propertyId, double defaultValue)
{
    int v = getPropertyInt(self, propertyId, (int)(defaultValue * 1000.0));
    return (double)v / 1000.0;
}

const XString* XFormat_stringProperty(const XFormat* self, int propertyId, const XString* defaultValue)
{
    const XString* s = getPropertyXString(self, propertyId);
    return (s && XString_size_base(s) > 0) ? s : defaultValue;
}

XColor XFormat_colorProperty(const XFormat* self, int propertyId, const XColor* defaultValue)
{
    if (!hasProperty(self, propertyId)) {
        return defaultValue ? *defaultValue : XColor_create_rgb(0, 0, 0, 255);
    }
    return getPropertyColor(self, propertyId);
}

/* ========== 比较运算符 ========== */

bool XFormat_equals(const XFormat* a, const XFormat* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    /* 使用 formatKey 进行比较 */
    uint8_t *keyA = NULL, *keyB = NULL;
    size_t lenA = 0, lenB = 0;
    XFormat_formatKey(a, &keyA, &lenA);
    XFormat_formatKey(b, &keyB, &lenB);
    bool eq = (lenA == lenB) && (lenA == 0 || memcmp(keyA, keyB, lenA) == 0);
    XFree_System(keyA);
    XFree_System(keyB);
    return eq;
}

bool XFormat_notEquals(const XFormat* a, const XFormat* b)
{
    return !XFormat_equals(a, b);
}

/* ========== UTF-8 便捷变体 ========== */

void XFormat_setFontName_utf8(XFormat* self, const char* name)
{
    if (name) setPropertyString(self, XFormat_P_Font_Name, name);
}

const char* XFormat_fontName_utf8(const XFormat* self)
{
    const XString* s = XFormat_fontName(self);
    return s ? XString_toUtf8(s) : NULL;
}
