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
#include "XMap/XMap.h"
#include "XPair.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== 辅助函数：获取/设置属性 ========== */

static void* getProperty(const XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return NULL;
    void* val = NULL;
    /* XMap 查找 */
    XMapIterator it = XMap_find_base(self->m_properties, &propertyId, sizeof(int));
    if (it != XMap_end_base(self->m_properties))
    {
        XPair* pair = XMapIterator_toPair(it);
        if (pair) val = pair->m_value;
    }
    return val;
}

static void setProperty(XFormat* self, int propertyId, void* value, size_t valueSize)
{
    if (!self) return;
    if (!self->m_properties)
    {
        self->m_properties = XMap_create();
        if (!self->m_properties) return;
    }
    XMap_insert_base(self->m_properties, &propertyId, sizeof(int), value, valueSize);
    self->m_dirty = true;
}

static bool hasProperty(const XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return false;
    XMapIterator it = XMap_find_base(self->m_properties, &propertyId, sizeof(int));
    return it != XMap_end_base(self->m_properties);
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
            self->m_properties = XMap_create();
        if (self->m_properties)
            XMap_clear_base(self->m_properties);
        /* 复制所有属性 */
        XMapIterator it = XMap_begin_base(other->m_properties);
        XMapIterator end = XMap_end_base(other->m_properties);
        for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it))
        {
            XPair* pair = XMapIterator_toPair(it);
            if (pair)
                XMap_insert_base(self->m_properties, XPair_getKey(pair), XPair_getKeySize(pair),
                                 XPair_getValue(pair), XPair_getValueSize(pair));
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
        if (self->m_properties) { XMap_clear_base(self->m_properties); XMap_deinit_base(self->m_properties); XFree_System(self->m_properties); }
        XFree_System(self);
    }
}

/* ========== 数字格式 ========== */

int XFormat_numberFormatIndex(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_NumFmt_Id);
    return val ? *(int*)val : 0;
}

void XFormat_setNumberFormatIndex(XFormat* self, int format)
{
    setProperty(self, XFormat_P_NumFmt_Id, &format, sizeof(int));
}

const char* XFormat_numberFormat(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_NumFmt_FormatCode);
    return val ? (const char*)val : "";
}

void XFormat_setNumberFormat(XFormat* self, const char* format)
{
    if (format)
        setProperty(self, XFormat_P_NumFmt_FormatCode, (void*)format, strlen(format) + 1);
}

void XFormat_setNumberFormat_ex(XFormat* self, int id, const char* format)
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
    const char* fmt = XFormat_numberFormat(self);
    if (fmt && *fmt)
    {
        if (strstr(fmt, "y") || strstr(fmt, "m") || strstr(fmt, "d") ||
            strstr(fmt, "h") || strstr(fmt, "s") || strstr(fmt, "Y") ||
            strstr(fmt, "M") || strstr(fmt, "D") || strstr(fmt, "H") || strstr(fmt, "S"))
            return true;
    }
    return false;
}

/* ========== 字体属性 ========== */

int XFormat_fontSize(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Size);
    return val ? *(int*)val : 0;
}

void XFormat_setFontSize(XFormat* self, int size)
{
    setProperty(self, XFormat_P_Font_Size, &size, sizeof(int));
}

bool XFormat_fontItalic(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Italic);
    return val ? *(bool*)val : false;
}

void XFormat_setFontItalic(XFormat* self, bool italic)
{
    setProperty(self, XFormat_P_Font_Italic, &italic, sizeof(bool));
}

bool XFormat_fontStrikeOut(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_StrikeOut);
    return val ? *(bool*)val : false;
}

void XFormat_setFontStrikeOut(XFormat* self, bool strikeOut)
{
    setProperty(self, XFormat_P_Font_StrikeOut, &strikeOut, sizeof(bool));
}

XColor XFormat_fontColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Color);
    if (val) return *(XColor*)val;
    return XColor_create(); /* 无效颜色 */
}

void XFormat_setFontColor(XFormat* self, const XColor* color)
{
    if (color)
        setProperty(self, XFormat_P_Font_Color, (void*)color, sizeof(XColor));
}

bool XFormat_fontBold(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Bold);
    return val ? *(bool*)val : false;
}

void XFormat_setFontBold(XFormat* self, bool bold)
{
    setProperty(self, XFormat_P_Font_Bold, &bold, sizeof(bool));
}

XFormat_FontScript XFormat_fontScript(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Script);
    return val ? *(XFormat_FontScript*)val : XFormat_FontScriptNormal;
}

void XFormat_setFontScript(XFormat* self, XFormat_FontScript script)
{
    setProperty(self, XFormat_P_Font_Script, &script, sizeof(XFormat_FontScript));
}

XFormat_FontUnderline XFormat_fontUnderline(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Underline);
    return val ? *(XFormat_FontUnderline*)val : XFormat_FontUnderlineNone;
}

void XFormat_setFontUnderline(XFormat* self, XFormat_FontUnderline underline)
{
    setProperty(self, XFormat_P_Font_Underline, &underline, sizeof(XFormat_FontUnderline));
}

bool XFormat_fontOutline(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Outline);
    return val ? *(bool*)val : false;
}

void XFormat_setFontOutline(XFormat* self, bool outline)
{
    setProperty(self, XFormat_P_Font_Outline, &outline, sizeof(bool));
}

const char* XFormat_fontName(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Font_Name);
    return val ? (const char*)val : "";
}

void XFormat_setFontName(XFormat* self, const char* name)
{
    if (name)
        setProperty(self, XFormat_P_Font_Name, (void*)name, strlen(name) + 1);
}

XFont* XFormat_font(const XFormat* self)
{
    /* 创建 XFont 对象并从格式属性填充 */
    XFont* font = XFont_create();
    if (!font) return NULL;
    if (XFormat_fontBold(self)) XFont_setBold(font, true);
    if (XFormat_fontItalic(self)) XFont_setItalic(font, true);
    int size = XFormat_fontSize(self);
    if (size > 0) XFont_setPointSize(font, (double)size);
    const char* name = XFormat_fontName(self);
    if (name && *name) XFont_setFamily(font, name);
    XColor color = XFormat_fontColor(self);
    if (XColor_isValid(&color)) XFont_setColor(font, &color);
    return font;
}

void XFormat_setFont(XFormat* self, const XFont* font)
{
    if (!self || !font) return;
    XFormat_setFontBold(self, XFont_isBold(font));
    XFormat_setFontItalic(self, XFont_isItalic(font));
    XFormat_setFontSize(self, (int)XFont_pointSize(font));
    XFormat_setFontName(self, XFont_family(font));
    XColor color = XFont_color(font);
    XFormat_setFontColor(self, &color);
}

/* ========== 对齐属性 ========== */

/**
 * @brief      获取水平对齐方式
 * @param self 指针
 * @return     水平对齐枚举值
 */
XFormat_HorizontalAlignment XFormat_horizontalAlignment(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_AlignH);
    return val ? *(XFormat_HorizontalAlignment*)val : XFormat_AlignHGeneral;
}

/**
 * @brief      设置水平对齐方式
 * @param self  指针
 * @param align 水平对齐枚举值
 */
void XFormat_setHorizontalAlignment(XFormat* self, XFormat_HorizontalAlignment align)
{
    setProperty(self, XFormat_P_Alignment_AlignH, &align, sizeof(XFormat_HorizontalAlignment));
}

/**
 * @brief      获取垂直对齐方式
 * @param self 指针
 * @return     垂直对齐枚举值
 */
XFormat_VerticalAlignment XFormat_verticalAlignment(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_AlignV);
    return val ? *(XFormat_VerticalAlignment*)val : XFormat_AlignBottom;
}

/**
 * @brief      设置垂直对齐方式
 * @param self  指针
 * @param align 垂直对齐枚举值
 */
void XFormat_setVerticalAlignment(XFormat* self, XFormat_VerticalAlignment align)
{
    setProperty(self, XFormat_P_Alignment_AlignV, &align, sizeof(XFormat_VerticalAlignment));
}

/**
 * @brief      获取文本自动换行标志
 * @param self 指针
 * @return     启用返回 true
 */
bool XFormat_textWrap(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_Wrap);
    return val ? *(bool*)val : false;
}

/**
 * @brief      设置文本自动换行
 * @param self     指针
 * @param textWrap 是否启用自动换行
 */
void XFormat_setTextWrap(XFormat* self, bool textWrap)
{
    setProperty(self, XFormat_P_Alignment_Wrap, &textWrap, sizeof(bool));
}

/**
 * @brief      获取文本旋转角度
 * @param self 指针
 * @return     旋转角度（0-180，或 255 表示垂直）
 */
int XFormat_rotation(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_Rotation);
    return val ? *(int*)val : 0;
}

/**
 * @brief      设置文本旋转角度
 * @param self     指针
 * @param rotation 旋转角度（0-180，或 255 表示垂直）
 */
void XFormat_setRotation(XFormat* self, int rotation)
{
    setProperty(self, XFormat_P_Alignment_Rotation, &rotation, sizeof(int));
}

/**
 * @brief      获取缩进量
 * @param self 指针
 * @return     缩进级别
 */
int XFormat_indent(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_Indent);
    return val ? *(int*)val : 0;
}

/**
 * @brief      设置缩进量
 * @param self   指针
 * @param indent 缩进级别
 */
void XFormat_setIndent(XFormat* self, int indent)
{
    setProperty(self, XFormat_P_Alignment_Indent, &indent, sizeof(int));
}

/**
 * @brief      获取是否缩小字体以适应单元格
 * @param self 指针
 * @return     启用返回 true
 */
bool XFormat_shrinkToFit(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Alignment_ShinkToFit);
    return val ? *(bool*)val : false;
}

/**
 * @brief      设置是否缩小字体以适应单元格
 * @param self  指针
 * @param shink 是否启用
 */
void XFormat_setShrinkToFit(XFormat* self, bool shink)
{
    setProperty(self, XFormat_P_Alignment_ShinkToFit, &shink, sizeof(bool));
}

/* ========== 边框属性 ========== */

/**
 * @brief      设置所有边框样式（统一设置）
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    XFormat_setLeftBorderStyle(self, style);
    XFormat_setRightBorderStyle(self, style);
    XFormat_setTopBorderStyle(self, style);
    XFormat_setBottomBorderStyle(self, style);
}

/**
 * @brief      设置所有边框颜色（统一设置）
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setBorderColor(XFormat* self, const XColor* color)
{
    XFormat_setLeftBorderColor(self, color);
    XFormat_setRightBorderColor(self, color);
    XFormat_setTopBorderColor(self, color);
    XFormat_setBottomBorderColor(self, color);
}

/**
 * @brief      获取左边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_leftBorderStyle(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_LeftStyle);
    return val ? *(XFormat_BorderStyle*)val : XFormat_BorderNone;
}

/**
 * @brief      设置左边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setLeftBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setProperty(self, XFormat_P_Border_LeftStyle, &style, sizeof(XFormat_BorderStyle));
}

/**
 * @brief      获取左边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_leftBorderColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_LeftColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置左边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setLeftBorderColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Border_LeftColor, (void*)color, sizeof(XColor));
}

/**
 * @brief      获取右边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_rightBorderStyle(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_RightStyle);
    return val ? *(XFormat_BorderStyle*)val : XFormat_BorderNone;
}

/**
 * @brief      设置右边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setRightBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setProperty(self, XFormat_P_Border_RightStyle, &style, sizeof(XFormat_BorderStyle));
}

/**
 * @brief      获取右边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_rightBorderColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_RightColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置右边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setRightBorderColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Border_RightColor, (void*)color, sizeof(XColor));
}

/**
 * @brief      获取上边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_topBorderStyle(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_TopStyle);
    return val ? *(XFormat_BorderStyle*)val : XFormat_BorderNone;
}

/**
 * @brief      设置上边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setTopBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setProperty(self, XFormat_P_Border_TopStyle, &style, sizeof(XFormat_BorderStyle));
}

/**
 * @brief      获取上边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_topBorderColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_TopColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置上边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setTopBorderColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Border_TopColor, (void*)color, sizeof(XColor));
}

/**
 * @brief      获取下边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_bottomBorderStyle(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_BottomStyle);
    return val ? *(XFormat_BorderStyle*)val : XFormat_BorderNone;
}

/**
 * @brief      设置下边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setBottomBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setProperty(self, XFormat_P_Border_BottomStyle, &style, sizeof(XFormat_BorderStyle));
}

/**
 * @brief      获取下边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_bottomBorderColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_BottomColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置下边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setBottomBorderColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Border_BottomColor, (void*)color, sizeof(XColor));
}

/**
 * @brief      获取对角线边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_diagonalBorderStyle(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_DiagonalStyle);
    return val ? *(XFormat_BorderStyle*)val : XFormat_BorderNone;
}

/**
 * @brief      设置对角线边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setDiagonalBorderStyle(XFormat* self, XFormat_BorderStyle style)
{
    setProperty(self, XFormat_P_Border_DiagonalStyle, &style, sizeof(XFormat_BorderStyle));
}

/**
 * @brief      获取对角线边框类型
 * @param self 指针
 * @return     对角线边框类型
 */
XFormat_DiagonalBorderType XFormat_diagonalBorderType(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_DiagonalType);
    return val ? *(XFormat_DiagonalBorderType*)val : XFormat_DiagonalBorderNone;
}

/**
 * @brief      设置对角线边框类型
 * @param self 指针
 * @param type 对角线边框类型
 */
void XFormat_setDiagonalBorderType(XFormat* self, XFormat_DiagonalBorderType type)
{
    setProperty(self, XFormat_P_Border_DiagonalType, &type, sizeof(XFormat_DiagonalBorderType));
}

/**
 * @brief      获取对角线边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_diagonalBorderColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Border_DiagonalColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置对角线边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setDiagonalBorderColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Border_DiagonalColor, (void*)color, sizeof(XColor));
}

/* ========== 填充属性 ========== */

/**
 * @brief      获取填充图案类型
 * @param self 指针
 * @return     填充图案
 */
XFormat_FillPattern XFormat_fillPattern(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Fill_Pattern);
    return val ? *(XFormat_FillPattern*)val : XFormat_PatternNone;
}

/**
 * @brief      设置填充图案类型
 * @param self    指针
 * @param pattern 填充图案
 */
void XFormat_setFillPattern(XFormat* self, XFormat_FillPattern pattern)
{
    setProperty(self, XFormat_P_Fill_Pattern, &pattern, sizeof(XFormat_FillPattern));
}

/**
 * @brief      获取前景色（图案颜色）
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_patternForegroundColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Fill_FgColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置前景色（图案颜色）
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setPatternForegroundColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Fill_FgColor, (void*)color, sizeof(XColor));
}

/**
 * @brief      获取背景色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_patternBackgroundColor(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Fill_BgColor);
    if (val) return *(XColor*)val;
    XColor c; XColor_create_init(&c, 0, 0, 0, 0); return c;
}

/**
 * @brief      设置背景色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setPatternBackgroundColor(XFormat* self, const XColor* color)
{
    if (color) setProperty(self, XFormat_P_Fill_BgColor, (void*)color, sizeof(XColor));
}

/* ========== 保护属性 ========== */

/**
 * @brief      获取锁定状态
 * @param self 指针
 * @return     锁定返回 true
 */
bool XFormat_locked(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Protection_Locked);
    return val ? *(bool*)val : true;
}

/**
 * @brief      设置锁定状态
 * @param self   指针
 * @param locked 是否锁定
 */
void XFormat_setLocked(XFormat* self, bool locked)
{
    setProperty(self, XFormat_P_Protection_Locked, &locked, sizeof(bool));
}

/**
 * @brief      获取隐藏状态
 * @param self 指针
 * @return     隐藏返回 true
 */
bool XFormat_hidden(const XFormat* self)
{
    void* val = getProperty(self, XFormat_P_Protection_Hidden);
    return val ? *(bool*)val : false;
}

/**
 * @brief      设置隐藏状态
 * @param self   指针
 * @param hidden 是否隐藏
 */
void XFormat_setHidden(XFormat* self, bool hidden)
{
    setProperty(self, XFormat_P_Protection_Hidden, &hidden, sizeof(bool));
}

/* ========== 格式操作 ========== */

/**
 * @brief      合并格式（将 modifier 中设置的属性应用到当前格式）
 * @param self     目标格式
 * @param modifier 源格式
 */
void XFormat_mergeFormat(XFormat* self, const XFormat* modifier)
{
    if (!self || !modifier || !modifier->m_properties) return;
    if (!self->m_properties)
    {
        self->m_properties = XMap_create();
        if (!self->m_properties) return;
    }
    /* 遍历 modifier 的所有属性，复制到 self */
    XMapIterator it = XMap_begin_base(modifier->m_properties);
    XMapIterator end = XMap_end_base(modifier->m_properties);
    for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it))
    {
        XPair* pair = XMapIterator_toPair(it);
        if (pair)
        {
            void* key = XPair_getKey(pair);
            size_t keySize = XPair_getKeySize(pair);
            void* value = XPair_getValue(pair);
            size_t valueSize = XPair_getValueSize(pair);
            XMap_insert_base(self->m_properties, key, keySize, value, valueSize);
        }
    }
    self->m_dirty = true;
}

/**
 * @brief      判断格式是否有效（有属性映射或索引有效）
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_isValid(const XFormat* self)
{
    if (!self) return false;
    if (self->m_properties && !XMap_empty_base(self->m_properties)) return true;
    if (self->m_xfIndex >= 0) return true;
    if (self->m_dxfIndex >= 0) return true;
    return false;
}

/**
 * @brief      判断格式是否为空（无属性设置）
 * @param self 指针
 * @return     为空返回 true
 */
bool XFormat_isEmpty(const XFormat* self)
{
    if (!self) return true;
    if (self->m_properties && !XMap_empty_base(self->m_properties)) return false;
    return true;
}

/* ========== 属性访问（通用） ========== */

/**
 * @brief      获取属性值
 * @param self         指针
 * @param propertyId   属性 ID
 * @return     属性值指针，未找到返回 NULL
 */
void* XFormat_property(const XFormat* self, int propertyId)
{
    return getProperty(self, propertyId);
}

/**
 * @brief      设置属性值
 * @param self        指针
 * @param propertyId  属性 ID
 * @param value       值指针
 */
void XFormat_setProperty(XFormat* self, int propertyId, void* value)
{
    if (!self || !value) return;
    /* 根据属性类型确定大小 */
    size_t size = 0;
    if (propertyId >= XFormat_P_Font_Size && propertyId <= XFormat_P_Font_Scheme)
    {
        if (propertyId == XFormat_P_Font_Size || propertyId == XFormat_P_Font_Family ||
            propertyId == XFormat_P_Font_Charset)
            size = sizeof(int);
        else if (propertyId == XFormat_P_Font_Color)
            size = sizeof(XColor);
        else if (propertyId == XFormat_P_Font_Name || propertyId == XFormat_P_Font_Scheme)
            size = strlen((const char*)value) + 1;
        else
            size = sizeof(bool);
    }
    else if (propertyId >= XFormat_P_Border_LeftStyle && propertyId <= XFormat_P_Border_DiagonalType)
    {
        if (propertyId >= XFormat_P_Border_LeftColor && propertyId <= XFormat_P_Border_DiagonalColor)
            size = sizeof(XColor);
        else if (propertyId == XFormat_P_Border_DiagonalType)
            size = sizeof(XFormat_DiagonalBorderType);
        else
            size = sizeof(XFormat_BorderStyle);
    }
    else if (propertyId >= XFormat_P_Fill_Pattern && propertyId <= XFormat_P_Fill_FgColor)
    {
        if (propertyId == XFormat_P_Fill_Pattern)
            size = sizeof(XFormat_FillPattern);
        else
            size = sizeof(XColor);
    }
    else if (propertyId >= XFormat_P_Alignment_AlignH && propertyId <= XFormat_P_Alignment_ShinkToFit)
    {
        if (propertyId == XFormat_P_Alignment_AlignH)
            size = sizeof(XFormat_HorizontalAlignment);
        else if (propertyId == XFormat_P_Alignment_AlignV)
            size = sizeof(XFormat_VerticalAlignment);
        else if (propertyId == XFormat_P_Alignment_Rotation || propertyId == XFormat_P_Alignment_Indent)
            size = sizeof(int);
        else
            size = sizeof(bool);
    }
    else if (propertyId == XFormat_P_NumFmt_Id)
        size = sizeof(int);
    else if (propertyId == XFormat_P_NumFmt_FormatCode)
        size = strlen((const char*)value) + 1;
    else if (propertyId == XFormat_P_Protection_Locked || propertyId == XFormat_P_Protection_Hidden)
        size = sizeof(bool);
    else
        size = sizeof(int);

    if (size > 0)
        setProperty(self, propertyId, value, size);
}

/**
 * @brief      清除属性
 * @param self        指针
 * @param propertyId  属性 ID
 */
void XFormat_clearProperty(XFormat* self, int propertyId)
{
    if (!self || !self->m_properties) return;
    XMapIterator it = XMap_find_base(self->m_properties, &propertyId, sizeof(int));
    if (it != XMap_end_base(self->m_properties))
    {
        XMap_erase_base(self->m_properties, it);
        self->m_dirty = true;
    }
}

/**
 * @brief      判断是否有指定属性
 * @param self        指针
 * @param propertyId  属性 ID
 * @return     有返回 true
 */
bool XFormat_hasProperty(const XFormat* self, int propertyId)
{
    return hasProperty(self, propertyId);
}

/* ========== 键/索引管理 ========== */

/**
 * @brief      判断是否有数字格式数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasNumFmtData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_NumFmt_Id) || hasProperty(self, XFormat_P_NumFmt_FormatCode);
}

/**
 * @brief      判断是否有字体数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasFontData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Font_Size) || hasProperty(self, XFormat_P_Font_Bold) ||
           hasProperty(self, XFormat_P_Font_Italic) || hasProperty(self, XFormat_P_Font_Name) ||
           hasProperty(self, XFormat_P_Font_Color) || hasProperty(self, XFormat_P_Font_Underline) ||
           hasProperty(self, XFormat_P_Font_Script) || hasProperty(self, XFormat_P_Font_StrikeOut) ||
           hasProperty(self, XFormat_P_Font_Outline) || hasProperty(self, XFormat_P_Font_Shadow) ||
           hasProperty(self, XFormat_P_Font_Family) || hasProperty(self, XFormat_P_Font_Charset) ||
           hasProperty(self, XFormat_P_Font_Scheme) || hasProperty(self, XFormat_P_Font_Condense) ||
           hasProperty(self, XFormat_P_Font_Extend);
}

/**
 * @brief      判断是否有填充数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasFillData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Fill_Pattern) ||
           hasProperty(self, XFormat_P_Fill_FgColor) ||
           hasProperty(self, XFormat_P_Fill_BgColor);
}

/**
 * @brief      判断是否有边框数据
 * @param self 指针
 * @return     有返回 true
 */
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

/**
 * @brief      判断是否有对齐数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasAlignmentData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Alignment_AlignH) ||
           hasProperty(self, XFormat_P_Alignment_AlignV) ||
           hasProperty(self, XFormat_P_Alignment_Wrap) ||
           hasProperty(self, XFormat_P_Alignment_Rotation) ||
           hasProperty(self, XFormat_P_Alignment_Indent) ||
           hasProperty(self, XFormat_P_Alignment_ShinkToFit);
}

/**
 * @brief      判断是否有保护数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasProtectionData(const XFormat* self)
{
    return hasProperty(self, XFormat_P_Protection_Locked) ||
           hasProperty(self, XFormat_P_Protection_Hidden);
}

/**
 * @brief      判断字体索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_fontIndexValid(const XFormat* self)
{
    return self ? self->m_fontIndexValid : false;
}

/**
 * @brief      获取字体索引
 * @param self 指针
 * @return     字体索引，无效返回 -1
 */
int XFormat_fontIndex(const XFormat* self)
{
    return self ? self->m_fontIndex : -1;
}

/**
 * @brief      判断边框索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_borderIndexValid(const XFormat* self)
{
    return self ? self->m_borderIndexValid : false;
}

/**
 * @brief      获取边框索引
 * @param self 指针
 * @return     边框索引，无效返回 -1
 */
int XFormat_borderIndex(const XFormat* self)
{
    return self ? self->m_borderIndex : -1;
}

/**
 * @brief      判断填充索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_fillIndexValid(const XFormat* self)
{
    return self ? self->m_fillIndexValid : false;
}

/**
 * @brief      获取填充索引
 * @param self 指针
 * @return     填充索引，无效返回 -1
 */
int XFormat_fillIndex(const XFormat* self)
{
    return self ? self->m_fillIndex : -1;
}

/**
 * @brief      判断格式索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_xfIndexValid(const XFormat* self)
{
    return self ? self->m_xfIndexValid : false;
}

/**
 * @brief      获取格式索引
 * @param self 指针
 * @return     格式索引，无效返回 -1
 */
int XFormat_xfIndex(const XFormat* self)
{
    return self ? self->m_xfIndex : -1;
}

/**
 * @brief      判断 dxf 索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_dxfIndexValid(const XFormat* self)
{
    return self ? self->m_dxfIndexValid : false;
}

/**
 * @brief      获取 dxf 索引
 * @param self 指针
 * @return     dxf 索引，无效返回 -1
 */
int XFormat_dxfIndex(const XFormat* self)
{
    return self ? self->m_dxfIndex : -1;
}

/**
 * @brief      设置字体索引
 * @param self  指针
 * @param index 字体索引
 */
void XFormat_setFontIndex(XFormat* self, int index)
{
    if (self) { self->m_fontIndex = index; self->m_fontIndexValid = true; }
}

/**
 * @brief      设置边框索引
 * @param self  指针
 * @param index 边框索引
 */
void XFormat_setBorderIndex(XFormat* self, int index)
{
    if (self) { self->m_borderIndex = index; self->m_borderIndexValid = true; }
}

/**
 * @brief      设置填充索引
 * @param self  指针
 * @param index 填充索引
 */
void XFormat_setFillIndex(XFormat* self, int index)
{
    if (self) { self->m_fillIndex = index; self->m_fillIndexValid = true; }
}

/**
 * @brief      设置格式索引
 * @param self  指针
 * @param index 格式索引
 */
void XFormat_setXfIndex(XFormat* self, int index)
{
    if (self) { self->m_xfIndex = index; self->m_xfIndexValid = true; }
}

/**
 * @brief      设置 dxf 索引
 * @param self  指针
 * @param index dxf 索引
 */
void XFormat_setDxfIndex(XFormat* self, int index)
{
    if (self) { self->m_dxfIndex = index; self->m_dxfIndexValid = true; }
}
