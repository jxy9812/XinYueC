/******************************************************************************
 * @file       XFont.c
 * @brief      XFont 字体类实现（对标 Qt 6.8 QFont）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFont.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========== 内部静态虚函数实现 ========== */

static void VXFont_deinit(XFont* self)
{
    if (!self) return;
    XString_delete(self->m_family);
    self->m_family = NULL;
    XString_delete(self->m_styleName);
    self->m_styleName = NULL;
}

static void VXFont_copy(XFont* dest, const XFont* src)
{
    if (!dest || !src) return;
    /* 复制家族字符串 */
    if (src->m_family) {
        dest->m_family = XString_create_copy(src->m_family);
    } else {
        dest->m_family = NULL;
    }
    /* 复制样式名称 */
    if (src->m_styleName) {
        dest->m_styleName = XString_create_copy(src->m_styleName);
    } else {
        dest->m_styleName = NULL;
    }
    /* 复制值字段 */
    dest->m_pointSizeF = src->m_pointSizeF;
    dest->m_pixelSize = src->m_pixelSize;
    dest->m_weight = src->m_weight;
    dest->m_style = src->m_style;
    dest->m_stretch = src->m_stretch;
    dest->m_underline = src->m_underline;
    dest->m_strikeOut = src->m_strikeOut;
    dest->m_overline = src->m_overline;
    dest->m_fixedPitch = src->m_fixedPitch;
    dest->m_kerning = src->m_kerning;
    dest->m_capitalization = src->m_capitalization;
    dest->m_letterSpacing = src->m_letterSpacing;
    dest->m_wordSpacing = src->m_wordSpacing;
    dest->m_styleHint = src->m_styleHint;
    dest->m_styleStrategy = src->m_styleStrategy;
    dest->m_hintingPreference = src->m_hintingPreference;
    dest->m_letterSpacingValue = src->m_letterSpacingValue;
    dest->m_wordSpacingValue = src->m_wordSpacingValue;
    dest->m_letterSpacingType = src->m_letterSpacingType;
    dest->m_resolveMask = src->m_resolveMask;
}

static void VXFont_move(XFont* dest, XFont* src)
{
    if (!dest || !src) return;
    /* 转移字符串所有权 */
    dest->m_family = src->m_family;
    dest->m_styleName = src->m_styleName;
    src->m_family = NULL;
    src->m_styleName = NULL;
    /* 复制值字段 */
    VXFont_copy(dest, src);
    /* 清零源对象 */
    src->m_pointSizeF = 0.0;
    src->m_pixelSize = 0;
    src->m_weight = XFont_Normal;
    src->m_style = XFont_StyleNormal;
    src->m_stretch = XFont_Unstretched;
    src->m_underline = 0;
    src->m_strikeOut = 0;
    src->m_overline = 0;
    src->m_fixedPitch = 0;
    src->m_kerning = 1;
    src->m_capitalization = XFont_MixedCase;
    src->m_letterSpacing = 0;
    src->m_wordSpacing = 0;
    src->m_styleHint = XFont_AnyStyle;
    src->m_styleStrategy = XFont_PreferDefault;
    src->m_hintingPreference = XFont_PreferDefaultHinting;
    src->m_letterSpacingValue = 0.0f;
    src->m_wordSpacingValue = 0.0f;
    src->m_letterSpacingType = XFont_PercentageSpacing;
    src->m_resolveMask = 0;
}

/* ========== 虚函数表初始化 ========== */

XVtable* XFont_class_init(void)
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_SIZE);
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFont_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFont_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFont_deinit);
    return XVTABLE_DEFAULT;
}

/* ========== 创建与初始化 ========== */

XFont* XFont_create(void)
{
    XFont* self = (XFont*)XMalloc_System(sizeof(XFont));
    if (!self) return NULL;
    XFont_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XFont* XFont_create_ex(const char* family, int pointSize, int weight, bool italic)
{
    XFont* self = XFont_create();
    if (!self) return NULL;
    if (family) XFont_setFamily(self, family);
    if (pointSize > 0) XFont_setPointSize(self, pointSize);
    if (weight > 0) XFont_setWeight(self, weight);
    if (italic) XFont_setItalic(self, true);
    return self;
}

void XFont_init(XFont* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XFont));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XFont);
    /* 默认值 */
    self->m_weight = XFont_Normal;
    self->m_style = XFont_StyleNormal;
    self->m_stretch = XFont_Unstretched;
    self->m_kerning = 1;
    self->m_capitalization = XFont_MixedCase;
    self->m_styleHint = XFont_AnyStyle;
    self->m_styleStrategy = XFont_PreferDefault;
    self->m_hintingPreference = XFont_PreferDefaultHinting;
    self->m_pointSizeF = 12.0;
    self->m_pixelSize = -1;
    self->m_letterSpacingType = XFont_PercentageSpacing;
}

void XFont_init_ex(XFont* self, const char* family, int pointSize, int weight, bool italic)
{
    XFont_init(self);
    if (family) XFont_setFamily(self, family);
    if (pointSize > 0) XFont_setPointSize(self, pointSize);
    if (weight > 0) XFont_setWeight(self, weight);
    if (italic) XFont_setItalic(self, true);
}

void XFont_copy(XFont* self, const XFont* other)
{
    if (!self) return;
    XFont_init(self);
    XFont_copy_base(self, other);
}

void XFont_move(XFont* self, XFont* other)
{
    if (!self) return;
    XFont_init(self);
    XFont_move_base(self, other);
}

void XFont_deinit(XFont* self)
{
    if (!self) return;
    VXFont_deinit(self);
    XClass_deinit_base((XClass*)self);
}

void XFont_delete(XFont* self)
{
    if (self) XFont_delete_base(self);
}

/* ========== 虚函数调度 ========== */

void XFont_copy_base(XFont* dest, const XFont* src)
{
    if (ISNULL(dest, "XFont") || ISNULL(src, "XFont")) return;
    void (*func)(XFont*, const XFont*) = XClassGetVirtualFunc(dest, EXClass_Copy, void(*)(XFont*, const XFont*));
    if (func) func(dest, src);
}

void XFont_move_base(XFont* dest, XFont* src)
{
    if (ISNULL(dest, "XFont") || ISNULL(src, "XFont")) return;
    void (*func)(XFont*, XFont*) = XClassGetVirtualFunc(dest, EXClass_Move, void(*)(XFont*, XFont*));
    if (func) func(dest, src);
}

void XFont_deinit_base(XFont* self)
{
    if (ISNULL(self, "XFont")) return;
    void (*func)(XFont*) = XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XFont*));
    if (func) func(self);
}

void XFont_delete_base(XFont* self)
{
    if (ISNULL(self, "XFont")) return;
    XFont_deinit_base(self);
    XClass_delete_base((XClass*)self);
}

/* ========== 属性访问 ========== */

const char* XFont_family(const XFont* self)
{
    if (!self) return "";
    return self->m_family ? XString_data(self->m_family) : "";
}

void XFont_setFamily(XFont* self, const char* family)
{
    if (!self) return;
    if (self->m_family) XString_delete(self->m_family);
    if (family && family[0]) {
        self->m_family = XString_create_utf8(family);
    } else {
        self->m_family = NULL;
    }
}

const char* XFont_styleName(const XFont* self)
{
    if (!self) return "";
    return self->m_styleName ? XString_data(self->m_styleName) : "";
}

void XFont_setStyleName(XFont* self, const char* styleName)
{
    if (!self) return;
    if (self->m_styleName) XString_delete(self->m_styleName);
    if (styleName && styleName[0]) {
        self->m_styleName = XString_create_utf8(styleName);
    } else {
        self->m_styleName = NULL;
    }
}

int XFont_pointSize(const XFont* self)
{
    if (!self) return -1;
    return (int)(self->m_pointSizeF + 0.5);
}

void XFont_setPointSize(XFont* self, int pointSize)
{
    if (self) self->m_pointSizeF = (double)pointSize;
}

double XFont_pointSizeF(const XFont* self)
{
    return self ? self->m_pointSizeF : 0.0;
}

void XFont_setPointSizeF(XFont* self, double pointSize)
{
    if (self) self->m_pointSizeF = pointSize;
}

int XFont_pixelSize(const XFont* self)
{
    return self ? self->m_pixelSize : -1;
}

void XFont_setPixelSize(XFont* self, int pixelSize)
{
    if (self) self->m_pixelSize = pixelSize;
}

int XFont_weight(const XFont* self)
{
    return self ? self->m_weight : XFont_Normal;
}

void XFont_setWeight(XFont* self, int weight)
{
    if (self) self->m_weight = weight;
}

bool XFont_bold(const XFont* self)
{
    return self ? (self->m_weight > XFont_Medium) : false;
}

void XFont_setBold(XFont* self, bool bold)
{
    if (self) self->m_weight = bold ? XFont_Bold : XFont_Normal;
}

int XFont_style(const XFont* self)
{
    return self ? self->m_style : XFont_StyleNormal;
}

void XFont_setStyle(XFont* self, int style)
{
    if (self) self->m_style = style;
}

bool XFont_italic(const XFont* self)
{
    return self ? (self->m_style != XFont_StyleNormal) : false;
}

void XFont_setItalic(XFont* self, bool italic)
{
    if (self) self->m_style = italic ? XFont_StyleItalic : XFont_StyleNormal;
}

bool XFont_underline(const XFont* self)
{
    return self ? (bool)self->m_underline : false;
}

void XFont_setUnderline(XFont* self, bool underline)
{
    if (self) self->m_underline = underline ? 1 : 0;
}

bool XFont_strikeOut(const XFont* self)
{
    return self ? (bool)self->m_strikeOut : false;
}

void XFont_setStrikeOut(XFont* self, bool strikeOut)
{
    if (self) self->m_strikeOut = strikeOut ? 1 : 0;
}

bool XFont_overline(const XFont* self)
{
    return self ? (bool)self->m_overline : false;
}

void XFont_setOverline(XFont* self, bool overline)
{
    if (self) self->m_overline = overline ? 1 : 0;
}

bool XFont_fixedPitch(const XFont* self)
{
    return self ? (bool)self->m_fixedPitch : false;
}

void XFont_setFixedPitch(XFont* self, bool fixedPitch)
{
    if (self) self->m_fixedPitch = fixedPitch ? 1 : 0;
}

bool XFont_kerning(const XFont* self)
{
    return self ? (bool)self->m_kerning : false;
}

void XFont_setKerning(XFont* self, bool kerning)
{
    if (self) self->m_kerning = kerning ? 1 : 0;
}

int XFont_capitalization(const XFont* self)
{
    return self ? (int)self->m_capitalization : XFont_MixedCase;
}

void XFont_setCapitalization(XFont* self, int capitalization)
{
    if (self) self->m_capitalization = (uint32_t)capitalization;
}

int XFont_stretch(const XFont* self)
{
    return self ? self->m_stretch : XFont_Unstretched;
}

void XFont_setStretch(XFont* self, int stretch)
{
    if (self) self->m_stretch = stretch;
}

int XFont_styleHint(const XFont* self)
{
    return self ? (int)self->m_styleHint : XFont_AnyStyle;
}

void XFont_setStyleHint(XFont* self, int styleHint)
{
    if (self) self->m_styleHint = (uint32_t)styleHint;
}

int XFont_styleStrategy(const XFont* self)
{
    return self ? (int)self->m_styleStrategy : XFont_PreferDefault;
}

void XFont_setStyleStrategy(XFont* self, int styleStrategy)
{
    if (self) self->m_styleStrategy = (uint32_t)styleStrategy;
}

int XFont_hintingPreference(const XFont* self)
{
    return self ? (int)self->m_hintingPreference : XFont_PreferDefaultHinting;
}

void XFont_setHintingPreference(XFont* self, int hintingPreference)
{
    if (self) self->m_hintingPreference = (uint32_t)hintingPreference;
}

float XFont_letterSpacing(const XFont* self)
{
    return self ? self->m_letterSpacingValue : 0.0f;
}

void XFont_setLetterSpacing(XFont* self, float spacing)
{
    if (self) {
        self->m_letterSpacing = 1;
        self->m_letterSpacingValue = spacing;
    }
}

int XFont_letterSpacingType(const XFont* self)
{
    return self ? self->m_letterSpacingType : XFont_PercentageSpacing;
}

void XFont_setLetterSpacingType(XFont* self, int type)
{
    if (self) self->m_letterSpacingType = type;
}

float XFont_wordSpacing(const XFont* self)
{
    return self ? self->m_wordSpacingValue : 0.0f;
}

void XFont_setWordSpacing(XFont* self, float spacing)
{
    if (self) {
        self->m_wordSpacing = 1;
        self->m_wordSpacingValue = spacing;
    }
}

uint32_t XFont_resolveMask(const XFont* self)
{
    return self ? self->m_resolveMask : 0;
}

void XFont_setResolveMask(XFont* self, uint32_t mask)
{
    if (self) self->m_resolveMask = mask;
}

/* ========== 工具方法 ========== */

bool XFont_equals(const XFont* a, const XFont* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (strcmp(XFont_family(a), XFont_family(b)) != 0) return false;
    if (a->m_pointSizeF != b->m_pointSizeF) return false;
    if (a->m_weight != b->m_weight) return false;
    if (a->m_style != b->m_style) return false;
    if (a->m_underline != b->m_underline) return false;
    if (a->m_strikeOut != b->m_strikeOut) return false;
    if (a->m_fixedPitch != b->m_fixedPitch) return false;
    if (a->m_stretch != b->m_stretch) return false;
    if (a->m_kerning != b->m_kerning) return false;
    if (a->m_capitalization != b->m_capitalization) return false;
    return true;
}

XString* XFont_toString(const XFont* self)
{
    if (!self) return NULL;
    char buf[256];
    const char* family = XFont_family(self);
    if (family[0] == '\\0') family = "Unknown";
    snprintf(buf, sizeof(buf), "%s,%d,%d,%d", family,
             (int)self->m_pointSizeF, self->m_weight, self->m_style);
    return XString_create_utf8(buf);
}

bool XFont_fromString(XFont* out, const char* str)
{
    if (!out || !str) return false;
    char family[128];
    int pointSize = 12, weight = XFont_Normal, style = XFont_StyleNormal;
    if (sscanf(str, "%127[^,],%d,%d,%d", family, &pointSize, &weight, &style) >= 1) {
        XFont_setFamily(out, family);
        XFont_setPointSize(out, pointSize);
        XFont_setWeight(out, weight);
        XFont_setStyle(out, style);
        return true;
    }
    return false;
}

void XFont_swap(XFont* a, XFont* b)
{
    if (!a || !b) return;
    XFont tmp = *a;
    *a = *b;
    *b = tmp;
    /* 交换后 vtable 要保持正确，所以需要重新设置 */
    XClassSetVtable(a, XFont);
    XClassSetVtable(b, XFont);
}
