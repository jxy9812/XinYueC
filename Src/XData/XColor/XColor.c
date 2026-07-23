/******************************************************************************
 * @file       XColor.c
 * @brief      XColor 颜色类实现（对标 Qt 6.8 QColor）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XColor.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ========== 内部辅助函数 ========== */

/* 将 0~255 的整数转换为 0~65535 的 uint16 */
static uint16_t intToU16(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 65535;
    return (uint16_t)(v * 0x0101);
}

/* 将 0~65535 的 uint16 转换为 0~255 的整数 */
static int u16ToInt(uint16_t v)
{
    return (int)(v / 0x0101);
}

/* 将 0.0~1.0 的浮点转换为 0~65535 的 uint16 */
static uint16_t floatToU16(float v)
{
    if (v < 0.0f) return 0;
    if (v > 1.0f) return 65535;
    return (uint16_t)(v * 65535.0f + 0.5f);
}

/* 将 0~65535 的 uint16 转换为 0.0~1.0 的浮点 */
static float u16ToFloat(uint16_t v)
{
    return (float)v / 65535.0f;
}

/* 检查整数 RGB 范围 */
static bool isRgbValid(int r, int g, int b, int a)
{
    return (r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
            b >= 0 && b <= 255 && a >= 0 && a <= 255);
}

/* ========== 创建与初始化 ========== */

XColor XColor_create(void)
{
    XColor c;
    c.m_spec = XColor_Invalid;
    c.m_alpha = 65535;
    c.m_comp1 = 0;
    c.m_comp2 = 0;
    c.m_comp3 = 0;
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_rgb(int r, int g, int b, int a)
{
    XColor c;
    if (!isRgbValid(r, g, b, a)) {
        c.m_spec = XColor_Invalid;
        c.m_alpha = 65535; c.m_comp1 = 0; c.m_comp2 = 0; c.m_comp3 = 0; c.m_comp4 = 0;
        return c;
    }
    c.m_spec = XColor_Rgb;
    c.m_alpha = intToU16(a);
    c.m_comp1 = intToU16(r);
    c.m_comp2 = intToU16(g);
    c.m_comp3 = intToU16(b);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_rgbF(float r, float g, float b, float a)
{
    return XColor_create_rgb(
        (int)(r * 255.0f + 0.5f),
        (int)(g * 255.0f + 0.5f),
        (int)(b * 255.0f + 0.5f),
        (int)(a * 255.0f + 0.5f));
}

/* ========== HSV 辅助函数 ========== */

static void rgbToHsvInt(int r, int g, int b, int* h, int* s, int* v)
{
    int max, min, delta;
    max = r; if (g > max) max = g; if (b > max) max = b;
    min = r; if (g < min) min = g; if (b < min) min = b;
    delta = max - min;

    *v = max;
    if (delta == 0) {
        *h = 0; *s = 0;
    } else {
        *s = (delta * 255) / max;
        if (max == r)       *h = 60 * ((g - b) / delta);
        else if (max == g)  *h = 60 * (2 + (b - r) / delta);
        else                *h = 60 * (4 + (r - g) / delta);
        if (*h < 0) *h += 360;
        if (*h >= 360) *h = 0;
    }
}

XColor XColor_create_hsv(int h, int s, int v, int a)
{
    if (h < 0 || h >= 360 || s < 0 || s > 255 || v < 0 || v > 255 || a < 0 || a > 255) {
        return XColor_create();
    }
    /* 将 HSV 转换为 RGB */
    int region, remainder, p, q, t;
    int r = 0, g = 0, b = 0;
    if (s == 0) {
        r = g = b = v;
    } else {
        region = h / 60;
        remainder = (h % 60) * 255 / 60;
        p = (v * (255 - s)) / 255;
        q = (v * (255 - ((s * remainder) / 255))) / 255;
        t = (v * (255 - ((s * (255 - remainder)) / 255))) / 255;
        switch (region) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }
    XColor c = XColor_create_rgb(r, g, b, a);
    if (c.m_spec != XColor_Invalid) {
        c.m_spec = XColor_Hsv;
    }
    return c;
}

XColor XColor_create_hsvF(float h, float s, float v, float a)
{
    return XColor_create_hsv(
        (int)(h + 0.5f),
        (int)(s * 255.0f + 0.5f),
        (int)(v * 255.0f + 0.5f),
        (int)(a * 255.0f + 0.5f));
}

/* ========== CMYK 辅助函数 ========== */

static void rgbToCmykInt(int r, int g, int b, int* c, int* m, int* y, int* k)
{
    int kk = 255 - (r > g ? (g > b ? b : g) : (r > b ? b : r));
    if (kk == 255) {
        *c = 0; *m = 0; *y = 0; *k = 255;
    } else {
        *c = (255 - r - kk) * 255 / (255 - kk);
        *m = (255 - g - kk) * 255 / (255 - kk);
        *y = (255 - b - kk) * 255 / (255 - kk);
        *k = kk;
    }
}

XColor XColor_create_cmyk(int c, int m, int y, int k, int a)
{
    if (c < 0 || c > 255 || m < 0 || m > 255 || y < 0 || y > 255 || k < 0 || k > 255 || a < 0 || a > 255) {
        return XColor_create();
    }
    int r = (255 - c) * (255 - k) / 255;
    int g = (255 - m) * (255 - k) / 255;
    int b = (255 - y) * (255 - k) / 255;
    XColor col = XColor_create_rgb(r, g, b, a);
    if (col.m_spec != XColor_Invalid) {
        col.m_spec = XColor_Cmyk;
    }
    return col;
}

XColor XColor_create_cmykF(float c, float m, float y, float k, float a)
{
    return XColor_create_cmyk(
        (int)(c * 255.0f + 0.5f),
        (int)(m * 255.0f + 0.5f),
        (int)(y * 255.0f + 0.5f),
        (int)(k * 255.0f + 0.5f),
        (int)(a * 255.0f + 0.5f));
}

/* ========== HSL 辅助函数 ========== */

static void rgbToHslInt(int r, int g, int b, int* h, int* s, int* l)
{
    int max, min, delta;
    max = r; if (g > max) max = g; if (b > max) max = b;
    min = r; if (g < min) min = g; if (b < min) min = b;
    delta = max - min;

    *l = (max + min) / 2;
    if (delta == 0) {
        *h = 0; *s = 0;
    } else {
        if (*l <= 127) *s = (delta * 255) / (max + min);
        else           *s = (delta * 255) / (511 - max - min);
        if (max == r)       *h = 60 * ((g - b) / delta);
        else if (max == g)  *h = 60 * (2 + (b - r) / delta);
        else                *h = 60 * (4 + (r - g) / delta);
        if (*h < 0) *h += 360;
        if (*h >= 360) *h = 0;
    }
}

XColor XColor_create_hsl(int h, int s, int l, int a)
{
    if (h < 0 || h >= 360 || s < 0 || s > 255 || l < 0 || l > 255 || a < 0 || a > 255) {
        return XColor_create();
    }
    /* 将 HSL 转换为 RGB */
    int r = 0, g = 0, b = 0;
    if (s == 0) {
        r = g = b = l;
    } else {
        int tmp2 = (l < 128) ? (l * (255 + s)) / 255 : (l + s - (l * s) / 255);
        int tmp1 = 2 * l - tmp2;
        int hh = h * 255 / 60;
        int tmp3_r = (hh + 255 * 4) % (255 * 6);
        int tmp3_g = (hh + 255 * 0) % (255 * 6);
        int tmp3_b = (hh + 255 * 2) % (255 * 6);
        /* 辅助函数 */
        #define hsl_hue_to_rgb(t1, t2, t3) \\
            (t3 < 255 ? t1 + ((t2 - t1) * t3) / 255 : \\
             t3 < 255*3 ? t2 : \\
             t3 < 255*4 ? t1 + ((t2 - t1) * (255*4 - t3)) / 255 : t1)
        r = hsl_hue_to_rgb(tmp1, tmp2, tmp3_r);
        g = hsl_hue_to_rgb(tmp1, tmp2, tmp3_g);
        b = hsl_hue_to_rgb(tmp1, tmp2, tmp3_b);
        #undef hsl_hue_to_rgb
    }
    XColor col = XColor_create_rgb(r, g, b, a);
    if (col.m_spec != XColor_Invalid) {
        col.m_spec = XColor_Hsl;
    }
    return col;
}

XColor XColor_create_hslF(float h, float s, float l, float a)
{
    return XColor_create_hsl(
        (int)(h + 0.5f),
        (int)(s * 255.0f + 0.5f),
        (int)(l * 255.0f + 0.5f),
        (int)(a * 255.0f + 0.5f));
}

XColor XColor_create_rgba(uint32_t rgba)
{
    return XColor_create_rgb(
        (int)((rgba >> 16) & 0xFF),
        (int)((rgba >> 8) & 0xFF),
        (int)(rgba & 0xFF),
        (int)((rgba >> 24) & 0xFF));
}

XColor XColor_create_argb(uint32_t argb)
{
    return XColor_create_rgb(
        (int)((argb >> 16) & 0xFF),
        (int)((argb >> 8) & 0xFF),
        (int)(argb & 0xFF),
        (int)((argb >> 24) & 0xFF));
}

void XColor_init(XColor* self)
{
    if (self) *self = XColor_create();
}

void XColor_init_rgb(XColor* self, int r, int g, int b, int a)
{
    if (self) *self = XColor_create_rgb(r, g, b, a);
}

/* ========== 查询方法 ========== */

bool XColor_isValid(const XColor* self)
{
    return self && self->m_spec != XColor_Invalid;
}

XColor_Spec XColor_spec(const XColor* self)
{
    return self ? self->m_spec : XColor_Invalid;
}

char* XColor_toHexString(const XColor* self, XColor_NameFormat format, char* out)
{
    if (!self || !out) return out;
    if (self->m_spec == XColor_Invalid) {
        out[0] = '\\0';
        return out;
    }
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int a = u16ToInt(self->m_alpha);
    if (format == XColor_HexArgb)
        sprintf(out, "#%02X%02X%02X%02X", a, r, g, b);
    else
        sprintf(out, "#%02X%02X%02X", r, g, b);
    return out;
}

XColor XColor_fromString(const char* name)
{
    if (!name) return XColor_create();
    /* 尝试解析 #RRGGBB 或 #AARRGGBB */
    if (name[0] == '#') {
        int len = (int)strlen(name);
        if (len == 7) {
            unsigned int r, g, b;
            if (sscanf(name + 1, "%02x%02x%02x", &r, &g, &b) == 3)
                return XColor_create_rgb((int)r, (int)g, (int)b, 255);
        } else if (len == 9) {
            unsigned int a, r, g, b;
            if (sscanf(name + 1, "%02x%02x%02x%02x", &a, &r, &g, &b) == 4)
                return XColor_create_rgb((int)r, (int)g, (int)b, (int)a);
        }
        return XColor_create();
    }
    /* 尝试按名称查找 */
    return XColor_fromName(name);
}

/* ========== RGB 分量访问 ========== */

int XColor_alpha(const XColor* self)
{
    return self ? u16ToInt(self->m_alpha) : 0;
}

void XColor_setAlpha(XColor* self, int a)
{
    if (self && self->m_spec != XColor_Invalid)
        self->m_alpha = intToU16(a);
}

float XColor_alphaF(const XColor* self)
{
    return self ? u16ToFloat(self->m_alpha) : 0.0f;
}

void XColor_setAlphaF(XColor* self, float a)
{
    if (self && self->m_spec != XColor_Invalid)
        self->m_alpha = floatToU16(a);
}

int XColor_red(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    return u16ToInt(rgb.m_comp1);
}

int XColor_green(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    return u16ToInt(rgb.m_comp2);
}

int XColor_blue(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    return u16ToInt(rgb.m_comp3);
}

/* ... 其余 RGB setter/getter 类似 ... */

void XColor_setRed(XColor* self, int r)
{
    if (!self || self->m_spec == XColor_Invalid) return;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    rgb.m_comp1 = intToU16(r);
    *self = rgb;
}

void XColor_setGreen(XColor* self, int g)
{
    if (!self || self->m_spec == XColor_Invalid) return;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    rgb.m_comp2 = intToU16(g);
    *self = rgb;
}

void XColor_setBlue(XColor* self, int b)
{
    if (!self || self->m_spec == XColor_Invalid) return;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    rgb.m_comp3 = intToU16(b);
    *self = rgb;
}

float XColor_redF(const XColor* self)
{
    return u16ToFloat((uint16_t)XColor_red(self) * 0x0101);
}

float XColor_greenF(const XColor* self)
{
    return u16ToFloat((uint16_t)XColor_green(self) * 0x0101);
}

float XColor_blueF(const XColor* self)
{
    return u16ToFloat((uint16_t)XColor_blue(self) * 0x0101);
}

void XColor_setRedF(XColor* self, float r)
{
    XColor_setRed(self, (int)(r * 255.0f + 0.5f));
}

void XColor_setGreenF(XColor* self, float g)
{
    XColor_setGreen(self, (int)(g * 255.0f + 0.5f));
}

void XColor_setBlueF(XColor* self, float b)
{
    XColor_setBlue(self, (int)(b * 255.0f + 0.5f));
}

void XColor_getRgb(const XColor* self, int* r, int* g, int* b, int* a)
{
    if (!self) return;
    XColor rgb;
    XColor_toRgb(self, &rgb);
    if (r) *r = u16ToInt(rgb.m_comp1);
    if (g) *g = u16ToInt(rgb.m_comp2);
    if (b) *b = u16ToInt(rgb.m_comp3);
    if (a) *a = u16ToInt(rgb.m_alpha);
}

void XColor_setRgb(XColor* self, int r, int g, int b, int a)
{
    if (self) *self = XColor_create_rgb(r, g, b, a);
}

void XColor_getRgbF(const XColor* self, float* r, float* g, float* b, float* a)
{
    int ri, gi, bi, ai;
    XColor_getRgb(self, &ri, &gi, &bi, &ai);
    if (r) *r = ri / 255.0f;
    if (g) *g = gi / 255.0f;
    if (b) *b = bi / 255.0f;
    if (a) *a = ai / 255.0f;
}

void XColor_setRgbF(XColor* self, float r, float g, float b, float a)
{
    XColor_setRgb(self, (int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f),
                  (int)(b * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
}

/* ========== HSV 分量访问 ========== */

int XColor_hue(const XColor* self)
{
    return XColor_hsvHue(self);
}

int XColor_saturation(const XColor* self)
{
    return XColor_hsvSaturation(self);
}

int XColor_hsvHue(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, v;
    rgbToHsvInt(r, g, b, &h, &s, &v);
    return h;
}

int XColor_hsvSaturation(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, v;
    rgbToHsvInt(r, g, b, &h, &s, &v);
    return s;
}

int XColor_value(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, v;
    rgbToHsvInt(r, g, b, &h, &s, &v);
    return v;
}

float XColor_hueF(const XColor* self) { return (float)XColor_hue(self); }
float XColor_saturationF(const XColor* self) { return XColor_hsvSaturation(self) / 255.0f; }
float XColor_hsvHueF(const XColor* self) { return (float)XColor_hsvHue(self); }
float XColor_hsvSaturationF(const XColor* self) { return XColor_hsvSaturation(self) / 255.0f; }
float XColor_valueF(const XColor* self) { return XColor_value(self) / 255.0f; }

void XColor_getHsv(const XColor* self, int* h, int* s, int* v, int* a)
{
    if (!self) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int hh, ss, vv;
    rgbToHsvInt(r, g, b, &hh, &ss, &vv);
    if (h) *h = hh;
    if (s) *s = ss;
    if (v) *v = vv;
    if (a) *a = u16ToInt(self->m_alpha);
}

void XColor_setHsv(XColor* self, int h, int s, int v, int a)
{
    if (self) *self = XColor_create_hsv(h, s, v, a);
}

void XColor_getHsvF(const XColor* self, float* h, float* s, float* v, float* a)
{
    int hi, si, vi, ai;
    XColor_getHsv(self, &hi, &si, &vi, &ai);
    if (h) *h = (float)hi;
    if (s) *s = si / 255.0f;
    if (v) *v = vi / 255.0f;
    if (a) *a = ai / 255.0f;
}

void XColor_setHsvF(XColor* self, float h, float s, float v, float a)
{
    XColor_setHsv(self, (int)(h + 0.5f), (int)(s * 255.0f + 0.5f),
                  (int)(v * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
}

/* ========== CMYK 分量访问 ========== */

int XColor_cyan(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int c, m, y, k;
    rgbToCmykInt(r, g, b, &c, &m, &y, &k);
    return c;
}

int XColor_magenta(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int c, m, y, k;
    rgbToCmykInt(r, g, b, &c, &m, &y, &k);
    return m;
}

int XColor_yellow(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int c, m, y, k;
    rgbToCmykInt(r, g, b, &c, &m, &y, &k);
    return y;
}

int XColor_black(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int c, m, y, k;
    rgbToCmykInt(r, g, b, &c, &m, &y, &k);
    return k;
}

float XColor_cyanF(const XColor* self) { return XColor_cyan(self) / 255.0f; }
float XColor_magentaF(const XColor* self) { return XColor_magenta(self) / 255.0f; }
float XColor_yellowF(const XColor* self) { return XColor_yellow(self) / 255.0f; }
float XColor_blackF(const XColor* self) { return XColor_black(self) / 255.0f; }

void XColor_getCmyk(const XColor* self, int* c, int* m, int* y, int* k, int* a)
{
    if (!self) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int cc, mm, yy, kk;
    rgbToCmykInt(r, g, b, &cc, &mm, &yy, &kk);
    if (c) *c = cc; if (m) *m = mm; if (y) *y = yy; if (k) *k = kk;
    if (a) *a = u16ToInt(self->m_alpha);
}

void XColor_setCmyk(XColor* self, int c, int m, int y, int k, int a)
{
    if (self) *self = XColor_create_cmyk(c, m, y, k, a);
}

void XColor_getCmykF(const XColor* self, float* c, float* m, float* y, float* k, float* a)
{
    int ci, mi, yi, ki, ai;
    XColor_getCmyk(self, &ci, &mi, &yi, &ki, &ai);
    if (c) *c = ci / 255.0f; if (m) *m = mi / 255.0f;
    if (y) *y = yi / 255.0f; if (k) *k = ki / 255.0f;
    if (a) *a = ai / 255.0f;
}

void XColor_setCmykF(XColor* self, float c, float m, float y, float k, float a)
{
    XColor_setCmyk(self, (int)(c * 255.0f + 0.5f), (int)(m * 255.0f + 0.5f),
                   (int)(y * 255.0f + 0.5f), (int)(k * 255.0f + 0.5f),
                   (int)(a * 255.0f + 0.5f));
}

/* ========== HSL 分量访问 ========== */

int XColor_hslHue(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, l;
    rgbToHslInt(r, g, b, &h, &s, &l);
    return h;
}

int XColor_hslSaturation(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, l;
    rgbToHslInt(r, g, b, &h, &s, &l);
    return s;
}

int XColor_lightness(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, l;
    rgbToHslInt(r, g, b, &h, &s, &l);
    return l;
}

float XColor_hslHueF(const XColor* self) { return (float)XColor_hslHue(self); }
float XColor_hslSaturationF(const XColor* self) { return XColor_hslSaturation(self) / 255.0f; }
float XColor_lightnessF(const XColor* self) { return XColor_lightness(self) / 255.0f; }

void XColor_getHsl(const XColor* self, int* h, int* s, int* l, int* a)
{
    if (!self) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int hh, ss, ll;
    rgbToHslInt(r, g, b, &hh, &ss, &ll);
    if (h) *h = hh; if (s) *s = ss; if (l) *l = ll;
    if (a) *a = u16ToInt(self->m_alpha);
}

void XColor_setHsl(XColor* self, int h, int s, int l, int a)
{
    if (self) *self = XColor_create_hsl(h, s, l, a);
}

void XColor_getHslF(const XColor* self, float* h, float* s, float* l, float* a)
{
    int hi, si, li, ai;
    XColor_getHsl(self, &hi, &si, &li, &ai);
    if (h) *h = (float)hi; if (s) *s = si / 255.0f;
    if (l) *l = li / 255.0f; if (a) *a = ai / 255.0f;
}

void XColor_setHslF(XColor* self, float h, float s, float l, float a)
{
    XColor_setHsl(self, (int)(h + 0.5f), (int)(s * 255.0f + 0.5f),
                  (int)(l * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
}

/* ========== 转换与工具 ========== */

void XColor_toRgb(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    if (self->m_spec == XColor_Rgb || self->m_spec == XColor_Invalid) {
        *out = *self;
        return;
    }
    /* 从其他色彩空间转换到 RGB */
    int r = 0, g = 0, b = 0;
    if (self->m_spec == XColor_Hsv || self->m_spec == XColor_Hsl) {
        int h = (int)((float)self->m_comp1 * 360.0f / 65535.0f);
        int s = u16ToInt(self->m_comp2);
        int v = u16ToInt(self->m_comp3);
        if (self->m_spec == XColor_Hsv) {
            XColor tmp = XColor_create_hsv(h, s, v, 255);
            XColor_getRgb(&tmp, &r, &g, &b, NULL);
        } else {
            XColor tmp = XColor_create_hsl(h, s, v, 255);
            XColor_getRgb(&tmp, &r, &g, &b, NULL);
        }
    } else if (self->m_spec == XColor_Cmyk) {
        int c = u16ToInt(self->m_comp1);
        int m = u16ToInt(self->m_comp2);
        int y = u16ToInt(self->m_comp3);
        int k = u16ToInt(self->m_comp4);
        r = (255 - c) * (255 - k) / 255;
        g = (255 - m) * (255 - k) / 255;
        b = (255 - y) * (255 - k) / 255;
    }
    out->m_spec = XColor_Rgb;
    out->m_alpha = self->m_alpha;
    out->m_comp1 = intToU16(r);
    out->m_comp2 = intToU16(g);
    out->m_comp3 = intToU16(b);
    out->m_comp4 = 0;
}

void XColor_toHsv(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, v;
    rgbToHsvInt(r, g, b, &h, &s, &v);
    out->m_spec = XColor_Hsv;
    out->m_alpha = self->m_alpha;
    out->m_comp1 = intToU16(h * 255 / 360);
    out->m_comp2 = intToU16(s);
    out->m_comp3 = intToU16(v);
    out->m_comp4 = 0;
}

void XColor_toCmyk(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int c, m, y, k;
    rgbToCmykInt(r, g, b, &c, &m, &y, &k);
    out->m_spec = XColor_Cmyk;
    out->m_alpha = self->m_alpha;
    out->m_comp1 = intToU16(c);
    out->m_comp2 = intToU16(m);
    out->m_comp3 = intToU16(y);
    out->m_comp4 = intToU16(k);
}

void XColor_toHsl(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int h, s, l;
    rgbToHslInt(r, g, b, &h, &s, &l);
    out->m_spec = XColor_Hsl;
    out->m_alpha = self->m_alpha;
    out->m_comp1 = intToU16(h * 255 / 360);
    out->m_comp2 = intToU16(s);
    out->m_comp3 = intToU16(l);
    out->m_comp4 = 0;
}

uint32_t XColor_rgba(const XColor* self)
{
    if (!self || self->m_spec == XColor_Invalid) return 0;
    int r = u16ToInt(self->m_comp1);
    int g = u16ToInt(self->m_comp2);
    int b = u16ToInt(self->m_comp3);
    int a = u16ToInt(self->m_alpha);
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t XColor_rgb(const XColor* self)
{
    return XColor_rgba(self) & 0x00FFFFFF;
}

void XColor_setRgba(XColor* self, uint32_t argb)
{
    if (self) *self = XColor_create_argb(argb);
}

void XColor_setRgb_uint32(XColor* self, uint32_t rgb)
{
    if (self) *self = XColor_create_rgb(
        (int)((rgb >> 16) & 0xFF), (int)((rgb >> 8) & 0xFF),
        (int)(rgb & 0xFF), 255);
}

bool XColor_equals(const XColor* a, const XColor* b)
{
    if (!a || !b) return false;
    if (a->m_spec == XColor_Invalid && b->m_spec == XColor_Invalid) return true;
    if (a->m_spec != b->m_spec) return false;
    return a->m_alpha == b->m_alpha && a->m_comp1 == b->m_comp1 &&
           a->m_comp2 == b->m_comp2 && a->m_comp3 == b->m_comp3 &&
           a->m_comp4 == b->m_comp4;
}

/* ========== SVG 命名颜色表 ========== */

typedef struct { const char* name; uint8_t r, g, b; } XColorNamedEntry;

static const XColorNamedEntry s_namedColors[] = {
    {"aliceblue", 240, 248, 255}, {"antiquewhite", 250, 235, 215},
    {"aqua", 0, 255, 255}, {"aquamarine", 127, 255, 212},
    {"azure", 240, 255, 255}, {"beige", 245, 245, 220},
    {"bisque", 255, 228, 196}, {"black", 0, 0, 0},
    {"blanchedalmond", 255, 235, 205}, {"blue", 0, 0, 255},
    {"blueviolet", 138, 43, 226}, {"brown", 165, 42, 42},
    {"burlywood", 222, 184, 135}, {"cadetblue", 95, 158, 160},
    {"chartreuse", 127, 255, 0}, {"chocolate", 210, 105, 30},
    {"coral", 255, 127, 80}, {"cornflowerblue", 100, 149, 237},
    {"cornsilk", 255, 248, 220}, {"crimson", 220, 20, 60},
    {"cyan", 0, 255, 255}, {"darkblue", 0, 0, 139},
    {"darkcyan", 0, 139, 139}, {"darkgoldenrod", 184, 134, 11},
    {"darkgray", 169, 169, 169}, {"darkgreen", 0, 100, 0},
    {"darkgrey", 169, 169, 169}, {"darkkhaki", 189, 183, 107},
    {"darkmagenta", 139, 0, 139}, {"darkolivegreen", 85, 107, 47},
    {"darkorange", 255, 140, 0}, {"darkorchid", 153, 50, 204},
    {"darkred", 139, 0, 0}, {"darksalmon", 233, 150, 122},
    {"darkseagreen", 143, 188, 143}, {"darkslateblue", 72, 61, 139},
    {"darkslategray", 47, 79, 79}, {"darkturquoise", 0, 206, 209},
    {"darkviolet", 148, 0, 211}, {"deeppink", 255, 20, 147},
    {"deepskyblue", 0, 191, 255}, {"dimgray", 105, 105, 105},
    {"dimgrey", 105, 105, 105}, {"dodgerblue", 30, 144, 255},
    {"firebrick", 178, 34, 34}, {"floralwhite", 255, 250, 240},
    {"forestgreen", 34, 139, 34}, {"fuchsia", 255, 0, 255},
    {"gainsboro", 220, 220, 220}, {"ghostwhite", 248, 248, 255},
    {"gold", 255, 215, 0}, {"goldenrod", 218, 165, 32},
    {"gray", 128, 128, 128}, {"green", 0, 128, 0},
    {"greenyellow", 173, 255, 47}, {"grey", 128, 128, 128},
    {"honeydew", 240, 255, 240}, {"hotpink", 255, 105, 180},
    {"indianred", 205, 92, 92}, {"indigo", 75, 0, 130},
    {"ivory", 255, 255, 240}, {"khaki", 240, 230, 140},
    {"lavender", 230, 230, 250}, {"lavenderblush", 255, 240, 245},
    {"lawngreen", 124, 252, 0}, {"lemonchiffon", 255, 250, 205},
    {"lightblue", 173, 216, 230}, {"lightcoral", 240, 128, 128},
    {"lightcyan", 224, 255, 255}, {"lightgoldenrodyellow", 250, 250, 210},
    {"lightgray", 211, 211, 211}, {"lightgreen", 144, 238, 144},
    {"lightgrey", 211, 211, 211}, {"lightpink", 255, 182, 193},
    {"lightsalmon", 255, 160, 122}, {"lightseagreen", 32, 178, 170},
    {"lightskyblue", 135, 206, 250}, {"lightslategray", 119, 136, 153},
    {"lightsteelblue", 176, 196, 222}, {"lightyellow", 255, 255, 224},
    {"lime", 0, 255, 0}, {"limegreen", 50, 205, 50},
    {"linen", 250, 240, 230}, {"magenta", 255, 0, 255},
    {"maroon", 128, 0, 0}, {"mediumaquamarine", 102, 205, 170},
    {"mediumblue", 0, 0, 205}, {"mediumorchid", 186, 85, 211},
    {"mediumpurple", 147, 112, 219}, {"mediumseagreen", 60, 179, 113},
    {"mediumslateblue", 123, 104, 238}, {"mediumspringgreen", 0, 250, 154},
    {"mediumturquoise", 72, 209, 204}, {"mediumvioletred", 199, 21, 133},
    {"midnightblue", 25, 25, 112}, {"mintcream", 245, 255, 250},
    {"mistyrose", 255, 228, 225}, {"moccasin", 255, 228, 181},
    {"navajowhite", 255, 222, 173}, {"navy", 0, 0, 128},
    {"oldlace", 253, 245, 230}, {"olive", 128, 128, 0},
    {"olivedrab", 107, 142, 35}, {"orange", 255, 165, 0},
    {"orangered", 255, 69, 0}, {"orchid", 218, 112, 214},
    {"palegoldenrod", 238, 232, 170}, {"palegreen", 152, 251, 152},
    {"paleturquoise", 175, 238, 238}, {"palevioletred", 219, 112, 147},
    {"papayawhip", 255, 239, 213}, {"peachpuff", 255, 218, 185},
    {"peru", 205, 133, 63}, {"pink", 255, 192, 203},
    {"plum", 221, 160, 221}, {"powderblue", 176, 224, 230},
    {"purple", 128, 0, 128}, {"red", 255, 0, 0},
    {"rosybrown", 188, 143, 143}, {"royalblue", 65, 105, 225},
    {"saddlebrown", 139, 69, 19}, {"salmon", 250, 128, 114},
    {"sandybrown", 244, 164, 96}, {"seagreen", 46, 139, 87},
    {"seashell", 255, 245, 238}, {"sienna", 160, 82, 45},
    {"silver", 192, 192, 192}, {"skyblue", 135, 206, 235},
    {"slateblue", 106, 90, 205}, {"slategray", 112, 128, 144},
    {"snow", 255, 250, 250}, {"springgreen", 0, 255, 127},
    {"steelblue", 70, 130, 180}, {"tan", 210, 180, 140},
    {"teal", 0, 128, 128}, {"thistle", 216, 191, 216},
    {"tomato", 255, 99, 71}, {"turquoise", 64, 224, 208},
    {"violet", 238, 130, 238}, {"wheat", 245, 222, 179},
    {"white", 255, 255, 255}, {"whitesmoke", 245, 245, 245},
    {"yellow", 255, 255, 0}, {"yellowgreen", 154, 205, 50},
};

static const int s_namedColorCount = sizeof(s_namedColors) / sizeof(s_namedColors[0]);

XColor XColor_fromName(const char* name)
{
    if (!name) return XColor_create();
    for (int i = 0; i < s_namedColorCount; i++) {
        if (strcasecmp(name, s_namedColors[i].name) == 0) {
            return XColor_create_rgb(s_namedColors[i].r, s_namedColors[i].g, s_namedColors[i].b, 255);
        }
    }
    return XColor_create();
}

const char** XColor_colorNames(int* outCount)
{
    if (outCount) *outCount = s_namedColorCount;
    const char** names = (const char**)malloc((size_t)s_namedColorCount * sizeof(char*));
    if (!names) return NULL;
    for (int i = 0; i < s_namedColorCount; i++) {
        names[i] = s_namedColors[i].name;
    }
    return names;
}
