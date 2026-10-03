/******************************************************************************
 * @file       XColor.c
 * @brief      XColor 颜色类实现（对标 Qt 6.8.3 QColor）
 * @author     XinYueC 团队
 * @details    逐函数移植 out/qt683-ref/qcolor.cpp（Qt 6.8.3）：
 *             - 存储布局为"按规格位存该空间分量"（对标 QColor::CT 联合，
 *               qcolor.h:215-257），RGB/HSV/CMYK/HSL 四视图共用 u16 通道；
 *             - 色相无定义（灰色）存 65535 哨兵、读出 -1（对标 USHRT_MAX）；
 *             - u16→int 统一走 qt_div_257 四舍五入除法（对标
 *               qdrawhelper_p.h qt_div_257）；
 *             - set* 越界两档口径：单通道"警告+钳位后照设"（对标
 *               QCOLOR_INT/REAL_RANGE_CHECK，qcolor.cpp:579-593）、整组
 *               "警告+置无效"（对标各 setter/invalidate）。
 ******************************************************************************/
#include "XColor.h"

/* ========== 换算原语（对标 Qt 内联族） ========== */

/* int 0~255 → u16 复制式扩展（对标 Qt `v * 0x101`，qcolor.cpp:2406-2409）。
 * 调用方按两档越界口径先行校验/钳位，此处钳位仅为内部兜底。 */
static uint16_t intToU16(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 65535;
    return (uint16_t)(v * 0x0101);
}

/* u16 → int：四舍五入除 257（对标 Qt qt_div_257：
 * qt_div_257(x) = qt_div_257_floor(x + 128)，qt_div_257_floor(y) =
 * (y - (y >> 8)) >> 8）。对复制式值（k×0x101）结果为 k；对原生 16 位值
 * （fromRgbF/16 位图来源，如 254）为四舍五入（→1），替代旧截断除的 0。 */
static int qt_div_257(uint16_t v)
{
    uint32_t y = (uint32_t)v + 128u;
    return (int)((y - (y >> 8)) >> 8);
}

/* float 0.0~1.0 → u16：对标 qRound(f * USHRT_MAX)（qcolor.cpp:2447-2450 等
 * 模式；qRound 为半值远离零，f≥0 域内与 +0.5 截断等价）。调用方按两档口径
 * 先行校验/钳位，此处钳位仅为内部兜底。 */
static uint16_t floatToU16(float v)
{
    if (v < 0.0f) return 0;
    if (v > 1.0f) return 65535;
    return (uint16_t)(v * 65535.0f + 0.5f);
}

/* u16 → float：对标 v / float(USHRT_MAX)（qcolor.cpp:1261-1265 等模式） */
static float u16ToFloat(uint16_t v)
{
    return (float)v / 65535.0f;
}

/* Hsv/Hsl 色相哨兵（无定义色相 = 灰色）：存 65535（对标 USHRT_MAX），读出 -1 */
#define XCOLOR_HUE_SENTINEL 65535u

/* 百分之一度（×100）→ 度（整除截断，36000 读出 360 为 Qt 原样怪癖） */
static int hueU16ToInt(uint16_t h)
{
    return (h == XCOLOR_HUE_SENTINEL) ? -1 : (int)(h / 100);
}

/* 百分之一度 → 轮分数 / -1.0f 哨兵（qcolor.cpp:1720, 1783 读出口径） */
static float hueU16ToFloat(uint16_t h)
{
    return (h == XCOLOR_HUE_SENTINEL) ? -1.0f : (float)h / 36000.0f;
}

/* 置无效（对标 QColor::invalidate()，qcolor.cpp:2930-2938） */
static void invalidateColor(XColor* c)
{
    c->m_spec = XColor_Invalid;
    c->m_alpha = 65535;
    c->m_comp1 = 0;
    c->m_comp2 = 0;
    c->m_comp3 = 0;
    c->m_comp4 = 0;
}

/* 单通道整型钳位（对标 QCOLOR_INT_RANGE_CHECK 的 var = qMax(0, qMin(var,255))，
 * qcolor.cpp:579-585） */
static int clampI0255(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

/* 单通道浮点钳位（对标 QCOLOR_REAL_RANGE_CHECK 的 qMax/qMin [0,1]，
 * qcolor.cpp:587-593） */
static float clampF01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

/* 整数 RGBA 合法性（对标 QColor::isRgbaValid，qcolor.h:209-212） */
static bool isRgbValid(int r, int g, int b, int a)
{
    return (r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
            b >= 0 && b <= 255 && a >= 0 && a <= 255);
}

/* 三值最大/最小（对标 Q_MAX_3/Q_MIN_3，qcolor.cpp:2177-2178） */
static float max3f(float a, float b, float c)
{
    return (a > b && a > c) ? a : (b > c ? b : c);
}

static float min3f(float a, float b, float c)
{
    return (a < b && a < c) ? a : (b < c ? b : c);
}

/* ========== 创建与初始化（对标 fromRgb/fromHsv/fromCmyk/fromHsl 工厂族） ========== */

XColor XColor_create(void)
{
    XColor c;
    invalidateColor(&c);
    return c;
}

XColor XColor_create_rgb(int r, int g, int b, int a)
{
    XColor c;
    /* 对标 QColor::fromRgb（qcolor.cpp:2397-2412）：越界警告 + 返回无效色 */
    if (!isRgbValid(r, g, b, a)) {
        XERROR_PRINTF("XColor_create_rgb: RGB parameters out of range");
        invalidateColor(&c);
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
    XColor c;
    /* 对标 QColor::fromRgbF（qcolor.cpp:2425-2453）：alpha 越界警告 + 无效 */
    if (a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_create_rgbF: Alpha parameter out of range");
        invalidateColor(&c);
        return c;
    }
    /* 偏差声明（§8 台账候选）：Qt 对 r/g/b 越界转 ExtendedRgb
     * （qcolor.cpp:2432-2443）；XColor 按声明边界不支持该规格，改警告 +
     * 钳位 [0,1] 保持 Rgb。 */
    if (r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f) {
        XERROR_PRINTF("XColor_create_rgbF: RGB parameters out of range, clamped");
        r = clampF01(r);
        g = clampF01(g);
        b = clampF01(b);
    }
    c.m_spec = XColor_Rgb;
    c.m_alpha = floatToU16(a);
    c.m_comp1 = floatToU16(r);
    c.m_comp2 = floatToU16(g);
    c.m_comp3 = floatToU16(b);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_hsv(int h, int s, int v, int a)
{
    XColor c;
    /* 对标 QColor::fromHsv（qcolor.cpp:2497-2515）：合法域 h = {-1} ∪
     * [0,360)（h≥360 亦拒绝，qcolor.cpp:2499——与 setter 族 setHsv 的回卷
     * 口径不同，两族不得混同）；直接按规格位存储，不再转 RGB（缺陷 #1/
     * #6/#7 根除） */
    if (((h < 0 || h >= 360) && h != -1) ||
        s < 0 || s > 255 || v < 0 || v > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_create_hsv: HSV parameters out of range");
        invalidateColor(&c);
        return c;
    }
    c.m_spec = XColor_Hsv;
    c.m_alpha = intToU16(a);                                    /* qcolor.cpp:2509 */
    c.m_comp1 = (h == -1) ? XCOLOR_HUE_SENTINEL                 /* qcolor.cpp:2510 */
                          : (uint16_t)((h % 360) * 100);
    c.m_comp2 = intToU16(s);
    c.m_comp3 = intToU16(v);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_hsvF(float h, float s, float v, float a)
{
    XColor c;
    /* 对标 QColor::fromHsvF（qcolor.cpp:2528-2546）：hue 存 qRound(h×36000)
     * 且 36000 不回卷（h=1.0 存 36000，Qt 原样怪癖——toRgb 按 0 处理、
     * hsvHue 读出 360） */
    if (((h < 0.0f || h > 1.0f) && h != -1.0f) ||
        s < 0.0f || s > 1.0f || v < 0.0f || v > 1.0f || a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_create_hsvF: HSV parameters out of range");
        invalidateColor(&c);
        return c;
    }
    c.m_spec = XColor_Hsv;
    c.m_alpha = floatToU16(a);                                  /* qcolor.cpp:2540 */
    c.m_comp1 = (h == -1.0f) ? XCOLOR_HUE_SENTINEL              /* qcolor.cpp:2541 */
                          : (uint16_t)(h * 36000.0f + 0.5f);
    c.m_comp2 = floatToU16(s);
    c.m_comp3 = floatToU16(v);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_cmyk(int c, int m, int y, int k, int a)
{
    XColor col;
    /* 对标 QColor::fromCmyk（qcolor.cpp:2739-2758）：直接按规格位存储，
     * 不再转 RGB */
    if (c < 0 || c > 255 || m < 0 || m > 255 || y < 0 || y > 255 ||
        k < 0 || k > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_create_cmyk: CMYK parameters out of range");
        invalidateColor(&col);
        return col;
    }
    col.m_spec = XColor_Cmyk;
    col.m_alpha = intToU16(a);                                  /* qcolor.cpp:2752 */
    col.m_comp1 = intToU16(c);
    col.m_comp2 = intToU16(m);
    col.m_comp3 = intToU16(y);
    col.m_comp4 = intToU16(k);
    return col;
}

XColor XColor_create_cmykF(float c, float m, float y, float k, float a)
{
    XColor col;
    /* 对标 QColor::fromCmykF（qcolor.cpp:2771-2790） */
    if (c < 0.0f || c > 1.0f || m < 0.0f || m > 1.0f ||
        y < 0.0f || y > 1.0f || k < 0.0f || k > 1.0f ||
        a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_create_cmykF: CMYK parameters out of range");
        invalidateColor(&col);
        return col;
    }
    col.m_spec = XColor_Cmyk;
    col.m_alpha = floatToU16(a);
    col.m_comp1 = floatToU16(c);
    col.m_comp2 = floatToU16(m);
    col.m_comp3 = floatToU16(y);
    col.m_comp4 = floatToU16(k);
    return col;
}

XColor XColor_create_hsl(int h, int s, int l, int a)
{
    XColor c;
    /* 对标 QColor::fromHsl（qcolor.cpp:2560-2578）：合法域 h = {-1} ∪
     * [0,360)（h≥360 亦拒绝，qcolor.cpp:2562） */
    if (((h < 0 || h >= 360) && h != -1) ||
        s < 0 || s > 255 || l < 0 || l > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_create_hsl: HSL parameters out of range");
        invalidateColor(&c);
        return c;
    }
    c.m_spec = XColor_Hsl;
    c.m_alpha = intToU16(a);                                    /* qcolor.cpp:2572 */
    c.m_comp1 = (h == -1) ? XCOLOR_HUE_SENTINEL                 /* qcolor.cpp:2573 */
                          : (uint16_t)((h % 360) * 100);
    c.m_comp2 = intToU16(s);
    c.m_comp3 = intToU16(l);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_hslF(float h, float s, float l, float a)
{
    XColor c;
    /* 对标 QColor::fromHslF（qcolor.cpp:2592-2612）：hue qRound(h×36000)
     * 后 ==36000 回卷为 0（fromHsvF 无此回卷——忠实保留 Qt 的不对称） */
    if (((h < 0.0f || h > 1.0f) && h != -1.0f) ||
        s < 0.0f || s > 1.0f || l < 0.0f || l > 1.0f || a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_create_hslF: HSL parameters out of range");
        invalidateColor(&c);
        return c;
    }
    c.m_spec = XColor_Hsl;
    c.m_alpha = floatToU16(a);                                  /* qcolor.cpp:2604 */
    c.m_comp1 = (h == -1.0f) ? XCOLOR_HUE_SENTINEL              /* qcolor.cpp:2605-2607 */
                          : (uint16_t)(h * 36000.0f + 0.5f);
    if (c.m_comp1 == 36000u)
        c.m_comp1 = 0;
    c.m_comp2 = floatToU16(s);
    c.m_comp3 = floatToU16(l);
    c.m_comp4 = 0;
    return c;
}

XColor XColor_create_rgba(uint32_t rgb)
{
    /* 对标 QColor(QRgb)（qcolor.cpp:728-736）/ fromRgb(QRgb)：高 8 位
     * alpha 忽略、alpha 恒 255（qcolor.cpp:731 alpha=0xffff） */
    return XColor_create_rgb(
        (int)((rgb >> 16) & 0xFF),
        (int)((rgb >> 8) & 0xFF),
        (int)(rgb & 0xFF),
        255);
}

XColor XColor_create_argb(uint32_t argb)
{
    /* 对标 QColor::fromRgba(QRgb)（qcolor.cpp:2383-2386）：含 alpha */
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
    return self && self->m_spec != XColor_Invalid;   /* qcolor.h:285-286 */
}

XColor_Spec XColor_spec(const XColor* self)
{
    return self ? self->m_spec : XColor_Invalid;
}

XString XColor_toHexString(const XColor* self, XColor_NameFormat format)
{
    XString_Init_Utf8(result, "");
    if (!self) return _result;   /* C 护栏：调用者没有提供对象 */
    /* 对标 QColor::name()（qcolor.cpp:832-842）：无效颜色不特判，经 rgba()
     * 按无效存储（alpha=65535、分量全 0）换算 → "#000000"/"#ff000000"
     * （qcolor.cpp:836-839），废除旧实现空串分支。 */
    uint32_t argb = XColor_rgba(self);
    if (format == XColor_HexArgb)
        XString_assign_fmt_utf8(&_result, "#%02X%02X%02X%02X",
                                (unsigned)((argb >> 24) & 0xFF),
                                (unsigned)((argb >> 16) & 0xFF),
                                (unsigned)((argb >> 8) & 0xFF),
                                (unsigned)(argb & 0xFF));
    else
        XString_assign_fmt_utf8(&_result, "#%02X%02X%02X",
                                (unsigned)((argb >> 16) & 0xFF),
                                (unsigned)((argb >> 8) & 0xFF),
                                (unsigned)(argb & 0xFF));
    return _result;
}

/* 十六进制字符序列 → 整数，任一非法字符返回 -1（对标 QtMiscUtils::fromHex
 * + hex2int，qcolor.cpp:33-46） */
static int hex2int(const char* s, int n)
{
    if (n < 0) return -1;
    int result = 0;
    for (; n > 0; --n) {
        result = result * 16;
        const char ch = *s++;
        int h = -1;
        if (ch >= '0' && ch <= '9')      h = ch - '0';
        else if (ch >= 'a' && ch <= 'f') h = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') h = ch - 'A' + 10;
        if (h < 0) return -1;
        result += h;
    }
    return result;
}

/* 十六进制颜色解析（移植 get_hex_rgb，qcolor.cpp:48-88；len 为 '#' 后字符
 * 数）：任一非十六进制字符即整体无效；解析成功得 u16 直存四通道。 */
static bool getHexRgb(const char* name, int len,
                      uint16_t* r, uint16_t* g, uint16_t* b, uint16_t* a)
{
    int ri, gi, bi;
    int ai = 65535;
    if (len == 12) {          /* #RRRRGGGGBBBB：每通道 4 位直取 u16（:56-59） */
        ri = hex2int(name + 0, 4);
        gi = hex2int(name + 4, 4);
        bi = hex2int(name + 8, 4);
    } else if (len == 9) {    /* #RRRGGGBBB：每通道 3 位，(r<<4)|(r>>8)（:60-68） */
        ri = hex2int(name + 0, 3);
        gi = hex2int(name + 3, 3);
        bi = hex2int(name + 6, 3);
        if (ri == -1 || gi == -1 || bi == -1) return false;
        ri = (ri << 4) | (ri >> 8);
        gi = (gi << 4) | (gi >> 8);
        bi = (bi << 4) | (bi >> 8);
    } else if (len == 8) {    /* #AARRGGBB：alpha+每通道 2 位 ×0x101（:69-73） */
        ai = hex2int(name + 0, 2) * 0x101;
        ri = hex2int(name + 2, 2) * 0x101;
        gi = hex2int(name + 4, 2) * 0x101;
        bi = hex2int(name + 6, 2) * 0x101;
    } else if (len == 6) {    /* #RRGGBB：每通道 2 位 ×0x101（:74-77） */
        ri = hex2int(name + 0, 2) * 0x101;
        gi = hex2int(name + 2, 2) * 0x101;
        bi = hex2int(name + 4, 2) * 0x101;
    } else if (len == 3) {    /* #RGB：每通道 1 位 ×0x1111（:78-81） */
        ri = hex2int(name + 0, 1) * 0x1111;
        gi = hex2int(name + 1, 1) * 0x1111;
        bi = hex2int(name + 2, 1) * 0x1111;
    } else {                  /* 其余长度无效（:82-86） */
        return false;
    }
    if ((unsigned)ri > 65535u || (unsigned)gi > 65535u ||
        (unsigned)bi > 65535u || (unsigned)ai > 65535u)
        return false;
    *r = (uint16_t)ri;
    *g = (uint16_t)gi;
    *b = (uint16_t)bi;
    *a = (uint16_t)ai;
    return true;
}

XColor XColor_fromString(const XString* name)
{
    /* 对标 QColor::fromString（qcolor.cpp:978-993）：'#' 前缀走十六进制
     * 变体，失败不回退名称查找（qcolor.cpp:983-992）；非 '#' 前缀走命名
     * 色查找；空串无效（qcolor.cpp:980-981）。 */
    if (!name) return XColor_create();
    int len = (int)XString_size((const XContainer*)name);
    if (len <= 0) return XColor_create();
    const char* utf8 = XString_toUtf8(name);
    if (!utf8) return XColor_create();
    if (utf8[0] == '#') {
        uint16_t r, g, b, a;
        if (getHexRgb(utf8 + 1, len - 1, &r, &g, &b, &a)) {
            /* 解析成功走 fromRgba64 等价（qcolor.cpp:984-985 → fromRgba64
             * 2465-2470 → setRgba64 1420-1428）：u16 直存 Rgb，不经 8 位
             * 中转（缺陷 #10 根除，替换旧 sscanf 3/9 两态分派） */
            XColor c;
            c.m_spec = XColor_Rgb;
            c.m_alpha = a;
            c.m_comp1 = r;
            c.m_comp2 = g;
            c.m_comp3 = b;
            c.m_comp4 = 0;
            return c;
        }
        return XColor_create();   /* qcolor.cpp:992：'#' 失败即无效 */
    }
    return XColor_fromName(name);
}

void XColor_setNamedColor(XColor* self, const XString* name)
{
    /* 对标 QColor::setNamedColor（qcolor.cpp:871-874，Qt 6.6 起标废弃、
     * 推荐 fromString；XinYueC 保留孪生入口对齐 Qt API 族）：即
     * *self = fromString(name) */
    if (self) *self = XColor_fromString(name);
}

void XColor_setNamedColor_2(XColor* self, const char* name)
{
    if (!self) return;
    if (!name) { invalidateColor(self); return; }
    /* 风格文档 _2 惯例：只建临时 XString 转发原名，不维护第二份字符串状态
     *（宏展开后 tmp 本身即 XString*，指向栈对象 _tmp） */
    XString_Init_Utf8(tmp, name);
    *self = XColor_fromString(tmp);
    XClassDeinit((XClass*)tmp);   /* 前例：XComboBox.c:1746 */
}

bool XColor_isValidColorName(const XString* name)
{
    /* 对标 QColor::isValidColorName（qcolor.cpp:948-951）：
     * return fromString(name).isValid() */
    XColor c = XColor_fromString(name);
    return XColor_isValid(&c);
}

bool XColor_isValidColorName_2(const char* name)
{
    if (!name) return false;
    XString_Init_Utf8(tmp, name);
    bool ok = XColor_isValidColorName(tmp);
    XClassDeinit((XClass*)tmp);
    return ok;
}

/* ========== RGB 分量访问 ========== */

int XColor_alpha(const XColor* self)
{
    if (!self) return 0;
    return qt_div_257(self->m_alpha);
}

void XColor_setAlpha(XColor* self, int a)
{
    if (!self) return;
    /* 单通道钳位档（对标 QCOLOR_INT_RANGE_CHECK + setAlpha，qcolor.cpp:
     * 1479-1488）：越界警告 + 钳位后照设，不校验规格 */
    if (a < 0 || a > 255) {
        XERROR_PRINTF("XColor_setAlpha: invalid value %d", a);
        a = clampI0255(a);
    }
    self->m_alpha = intToU16(a);
}

float XColor_alphaF(const XColor* self)
{
    if (!self) return 0.0f;
    return u16ToFloat(self->m_alpha);
}

void XColor_setAlphaF(XColor* self, float a)
{
    if (!self) return;
    /* 单通道钳位档（对标 setAlphaF，qcolor.cpp:1509-1518） */
    if (a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_setAlphaF: invalid value %g", (double)a);
        a = clampF01(a);
    }
    self->m_alpha = floatToU16(a);
}

int XColor_red(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 三段式第二段：先转 RGB 再读（qcolor.cpp:1528-1529） */
        return qt_div_257(rgb.m_comp1);
    }
    return qt_div_257(self->m_comp1);   /* Rgb/Invalid：直读（Invalid 分量 0） */
}

int XColor_green(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return qt_div_257(rgb.m_comp2);
    }
    return qt_div_257(self->m_comp2);
}

int XColor_blue(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return qt_div_257(rgb.m_comp3);
    }
    return qt_div_257(self->m_comp3);
}

void XColor_setRed(XColor* self, int r)
{
    if (!self) return;
    /* 单通道钳位档（对标 setRed，qcolor.cpp:1539-1546） */
    if (r < 0 || r > 255) {
        XERROR_PRINTF("XColor_setRed: invalid value %d", r);
        r = clampI0255(r);
    }
    if (self->m_spec != XColor_Rgb)
        /* 非 Rgb（含 Invalid）整组重写并改判 Rgb（qcolor.cpp:1542-1543 →
         * setRgb 内 cspec=Rgb，qcolor.cpp:1356）。对无效色 green()/blue()
         * 读 0、alpha() 读 255，即结果 (r,0,0,255)——忠实保留 Qt 怪癖 */
        XColor_setRgb(self, r, XColor_green(self), XColor_blue(self),
                      XColor_alpha(self));
    else
        self->m_comp1 = intToU16(r);
}

void XColor_setGreen(XColor* self, int g)
{
    if (!self) return;
    /* 单通道钳位档（对标 setGreen，qcolor.cpp:1566-1573） */
    if (g < 0 || g > 255) {
        XERROR_PRINTF("XColor_setGreen: invalid value %d", g);
        g = clampI0255(g);
    }
    if (self->m_spec != XColor_Rgb)
        XColor_setRgb(self, XColor_red(self), g, XColor_blue(self),
                      XColor_alpha(self));
    else
        self->m_comp2 = intToU16(g);
}

void XColor_setBlue(XColor* self, int b)
{
    if (!self) return;
    /* 单通道钳位档（对标 setBlue，qcolor.cpp:1595-1602） */
    if (b < 0 || b > 255) {
        XERROR_PRINTF("XColor_setBlue: invalid value %d", b);
        b = clampI0255(b);
    }
    if (self->m_spec != XColor_Rgb)
        XColor_setRgb(self, XColor_red(self), XColor_green(self), b,
                      XColor_alpha(self));
    else
        self->m_comp3 = intToU16(b);
}

float XColor_redF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return u16ToFloat(rgb.m_comp1);   /* u16 直读，废除旧 red()×0x101 回环 */
    }
    return u16ToFloat(self->m_comp1);   /* Rgb/Invalid：u16/65535.0f（qcolor.cpp:1611-1612） */
}

float XColor_greenF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return u16ToFloat(rgb.m_comp2);
    }
    return u16ToFloat(self->m_comp2);
}

float XColor_blueF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return u16ToFloat(rgb.m_comp3);
    }
    return u16ToFloat(self->m_comp3);
}

void XColor_setRedF(XColor* self, float r)
{
    if (!self) return;
    /* 对标 setRedF（qcolor.cpp:1626-1634）：Rgb 且域内单通道直写；否则走
     * setRgbF 整组重写（越界=警告+钳位保持 Rgb） */
    if (self->m_spec == XColor_Rgb && r >= 0.0f && r <= 1.0f)
        self->m_comp1 = floatToU16(r);
    else
        XColor_setRgbF(self, r, XColor_greenF(self), XColor_blueF(self),
                       XColor_alphaF(self));
}

void XColor_setGreenF(XColor* self, float g)
{
    if (!self) return;
    /* 对标 setGreenF（qcolor.cpp:1658-1666） */
    if (self->m_spec == XColor_Rgb && g >= 0.0f && g <= 1.0f)
        self->m_comp2 = floatToU16(g);
    else
        XColor_setRgbF(self, XColor_redF(self), g, XColor_blueF(self),
                       XColor_alphaF(self));
}

void XColor_setBlueF(XColor* self, float b)
{
    if (!self) return;
    /* 对标 setBlueF（qcolor.cpp:1688-1696） */
    if (self->m_spec == XColor_Rgb && b >= 0.0f && b <= 1.0f)
        self->m_comp3 = floatToU16(b);
    else
        XColor_setRgbF(self, XColor_redF(self), XColor_greenF(self), b,
                       XColor_alphaF(self));
}

void XColor_getRgb(const XColor* self, int* r, int* g, int* b, int* a)
{
    if (!self) return;
    if (!r && !g && !b && !a) return;   /* C 护栏：输出指针全 NULL */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 三段式：非 Rgb（Invalid 除外）先转再读（qcolor.cpp:1290-1293） */
        XColor_getRgb(&rgb, r, g, b, a);
        return;
    }
    /* 直读本规格存储（含 Invalid：照读得 (0,0,0,255)——qcolor.cpp:1290
     * 条件不含 Invalid） */
    if (r) *r = qt_div_257(self->m_comp1);
    if (g) *g = qt_div_257(self->m_comp2);
    if (b) *b = qt_div_257(self->m_comp3);
    if (a) *a = qt_div_257(self->m_alpha);
}

void XColor_setRgb(XColor* self, int r, int g, int b, int a)
{
    if (self) *self = XColor_create_rgb(r, g, b, a);   /* 越界=警告+整色置无效 */
}

void XColor_getRgbF(const XColor* self, float* r, float* g, float* b, float* a)
{
    if (!self) return;
    if (!r && !g && !b && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 三段式（qcolor.cpp:1255-1258） */
        XColor_getRgbF(&rgb, r, g, b, a);
        return;
    }
    /* u16/65535.0f 直读，16 位精度不经 8 位中转（qcolor.cpp:1261-1265，
     * 废除旧"整数/255"两跳） */
    if (r) *r = u16ToFloat(self->m_comp1);
    if (g) *g = u16ToFloat(self->m_comp2);
    if (b) *b = u16ToFloat(self->m_comp3);
    if (a) *a = u16ToFloat(self->m_alpha);
}

void XColor_setRgbF(XColor* self, float r, float g, float b, float a)
{
    if (!self) return;
    /* 对标 setRgbF（qcolor.cpp:1315-1339）：alpha 越界警告 + 整色置无效 */
    if (a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_setRgbF: Alpha parameter is out of range");
        invalidateColor(self);
        return;
    }
    /* 偏差声明（§8 台账候选）：Qt 对 r/g/b 越界转 ExtendedRgb
     * （qcolor.cpp:1322-1332）；XColor 按声明边界不支持该规格，改警告 +
     * 钳位保持 Rgb。 */
    if (r < 0.0f || r > 1.0f || g < 0.0f || g > 1.0f || b < 0.0f || b > 1.0f) {
        XERROR_PRINTF("XColor_setRgbF: RGB parameters out of range, clamped");
        r = clampF01(r);
        g = clampF01(g);
        b = clampF01(b);
    }
    self->m_spec = XColor_Rgb;
    self->m_comp1 = floatToU16(r);
    self->m_comp2 = floatToU16(g);
    self->m_comp3 = floatToU16(b);
    self->m_alpha = floatToU16(a);
    self->m_comp4 = 0;
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
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);   /* 三段式：非 Hsv（Invalid 除外）先转再读（qcolor.cpp:1718-1719） */
        return hueU16ToInt(hsv.m_comp1);
    }
    /* 哨兵 → -1（灰色无定义色相，缺陷 #4 根除），否则百分之一度整除 100
     * （36000 读出 360，忠实保留 Qt 怪癖，qcolor.cpp:1720） */
    return hueU16ToInt(self->m_comp1);
}

int XColor_hsvSaturation(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);
        return qt_div_257(hsv.m_comp2);
    }
    return qt_div_257(self->m_comp2);
}

int XColor_value(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);
        return qt_div_257(hsv.m_comp3);
    }
    return qt_div_257(self->m_comp3);
}

float XColor_hueF(const XColor* self)
{
    return XColor_hsvHueF(self);
}

float XColor_saturationF(const XColor* self)
{
    return XColor_hsvSaturationF(self);
}

float XColor_hsvHueF(const XColor* self)
{
    if (!self) return -1.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);
        return hueU16ToFloat(hsv.m_comp1);
    }
    /* 哨兵 → -1.0f，否则 /36000.0f 轮分数（qcolor.cpp:1783；qcolor.h:119-123
     * 的 0.0<=hueF<360.0 注释系 Qt 陈旧文档错，以实现为准） */
    return hueU16ToFloat(self->m_comp1);
}

float XColor_hsvSaturationF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);
        return u16ToFloat(hsv.m_comp2);   /* u16 直读，废除旧 /255 两跳 */
    }
    return u16ToFloat(self->m_comp2);
}

float XColor_valueF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);
        return u16ToFloat(hsv.m_comp3);
    }
    return u16ToFloat(self->m_comp3);
}

void XColor_getHsv(const XColor* self, int* h, int* s, int* v, int* a)
{
    if (!self) return;
    if (!h && !s && !v && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);   /* 三段式（qcolor.cpp:1048-1051） */
        XColor_getHsv(&hsv, h, s, v, a);
        return;
    }
    /* 直读本规格存储（含 Invalid：分量照读，hue 分量为 0 非 → 0）；
     * 灰色哨兵 → -1（qcolor.cpp:1053） */
    if (h) *h = hueU16ToInt(self->m_comp1);
    if (s) *s = qt_div_257(self->m_comp2);
    if (v) *v = qt_div_257(self->m_comp3);
    if (a) *a = qt_div_257(self->m_alpha);
}

void XColor_setHsv(XColor* self, int h, int s, int v, int a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setHsv，qcolor.cpp:1097-1111）：h < -1 或 s/v/a
     * 越界 → 警告 + 置无效；h≥360 不拒、按 (h%360)×100 回卷（与工厂族
     * create_hsv 的 h≥360 拒绝口径不同，两族不得混同） */
    if (h < -1 || s < 0 || s > 255 || v < 0 || v > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_setHsv: HSV parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Hsv;
    self->m_alpha = intToU16(a);                                /* qcolor.cpp:1106 */
    self->m_comp1 = (h == -1) ? XCOLOR_HUE_SENTINEL             /* qcolor.cpp:1107 */
                          : (uint16_t)((h % 360) * 100);
    self->m_comp2 = intToU16(s);
    self->m_comp3 = intToU16(v);
    self->m_comp4 = 0;
}

void XColor_getHsvF(const XColor* self, float* h, float* s, float* v, float* a)
{
    if (!self) return;
    if (!h && !s && !v && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsv) {
        XColor hsv;
        XColor_toHsv(self, &hsv);   /* 三段式（qcolor.cpp:1020-1023） */
        XColor_getHsvF(&hsv, h, s, v, a);
        return;
    }
    if (h) *h = hueU16ToFloat(self->m_comp1);
    if (s) *s = u16ToFloat(self->m_comp2);
    if (v) *v = u16ToFloat(self->m_comp3);
    if (a) *a = u16ToFloat(self->m_alpha);
}

void XColor_setHsvF(XColor* self, float h, float s, float v, float a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setHsvF，qcolor.cpp:1069-1086）：h ∉[0,1] 且
     * ≠-1.0，或 s/v/a 越界 → 警告 + 置无效 */
    if (((h < 0.0f || h > 1.0f) && h != -1.0f) ||
        s < 0.0f || s > 1.0f || v < 0.0f || v > 1.0f || a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_setHsvF: HSV parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Hsv;
    self->m_alpha = floatToU16(a);                              /* qcolor.cpp:1081 */
    self->m_comp1 = (h == -1.0f) ? XCOLOR_HUE_SENTINEL          /* qcolor.cpp:1082 */
                          : (uint16_t)(h * 36000.0f + 0.5f);
    self->m_comp2 = floatToU16(s);
    self->m_comp3 = floatToU16(v);
    self->m_comp4 = 0;
}

/* ========== CMYK 分量访问 ========== */

int XColor_cyan(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);   /* 三段式（qcolor.cpp:1914-1915） */
        return qt_div_257(cmyk.m_comp1);
    }
    return qt_div_257(self->m_comp1);
}

int XColor_magenta(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return qt_div_257(cmyk.m_comp2);
    }
    return qt_div_257(self->m_comp2);
}

int XColor_yellow(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return qt_div_257(cmyk.m_comp3);
    }
    return qt_div_257(self->m_comp3);
}

int XColor_black(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return qt_div_257(cmyk.m_comp4);
    }
    return qt_div_257(self->m_comp4);
}

float XColor_cyanF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return u16ToFloat(cmyk.m_comp1);
    }
    return u16ToFloat(self->m_comp1);
}

float XColor_magentaF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return u16ToFloat(cmyk.m_comp2);
    }
    return u16ToFloat(self->m_comp2);
}

float XColor_yellowF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return u16ToFloat(cmyk.m_comp3);
    }
    return u16ToFloat(self->m_comp3);
}

float XColor_blackF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);
        return u16ToFloat(cmyk.m_comp4);
    }
    return u16ToFloat(self->m_comp4);
}

void XColor_getCmyk(const XColor* self, int* c, int* m, int* y, int* k, int* a)
{
    if (!self) return;
    if (!c && !m && !y && !k && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);   /* 三段式（qcolor.cpp:2629-2632） */
        XColor_getCmyk(&cmyk, c, m, y, k, a);
        return;
    }
    if (c) *c = qt_div_257(self->m_comp1);
    if (m) *m = qt_div_257(self->m_comp2);
    if (y) *y = qt_div_257(self->m_comp3);
    if (k) *k = qt_div_257(self->m_comp4);
    if (a) *a = qt_div_257(self->m_alpha);
}

void XColor_setCmyk(XColor* self, int c, int m, int y, int k, int a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setCmyk，qcolor.cpp:2680-2698） */
    if (c < 0 || c > 255 || m < 0 || m > 255 || y < 0 || y > 255 ||
        k < 0 || k > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_setCmyk: CMYK parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Cmyk;
    self->m_alpha = intToU16(a);                                /* qcolor.cpp:2693 */
    self->m_comp1 = intToU16(c);
    self->m_comp2 = intToU16(m);
    self->m_comp3 = intToU16(y);
    self->m_comp4 = intToU16(k);
}

void XColor_getCmykF(const XColor* self, float* c, float* m, float* y, float* k, float* a)
{
    if (!self) return;
    if (!c && !m && !y && !k && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Cmyk) {
        XColor cmyk;
        XColor_toCmyk(self, &cmyk);   /* 三段式（qcolor.cpp:2658-2661） */
        XColor_getCmykF(&cmyk, c, m, y, k, a);
        return;
    }
    if (c) *c = u16ToFloat(self->m_comp1);
    if (m) *m = u16ToFloat(self->m_comp2);
    if (y) *y = u16ToFloat(self->m_comp3);
    if (k) *k = u16ToFloat(self->m_comp4);
    if (a) *a = u16ToFloat(self->m_alpha);
}

void XColor_setCmykF(XColor* self, float c, float m, float y, float k, float a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setCmykF，qcolor.cpp:2710-2728） */
    if (c < 0.0f || c > 1.0f || m < 0.0f || m > 1.0f ||
        y < 0.0f || y > 1.0f || k < 0.0f || k > 1.0f ||
        a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_setCmykF: CMYK parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Cmyk;
    self->m_alpha = floatToU16(a);
    self->m_comp1 = floatToU16(c);
    self->m_comp2 = floatToU16(m);
    self->m_comp3 = floatToU16(y);
    self->m_comp4 = floatToU16(k);
}

/* ========== HSL 分量访问 ========== */

int XColor_hslHue(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);   /* 三段式（qcolor.cpp:1832-1833） */
        return hueU16ToInt(hsl.m_comp1);
    }
    return hueU16ToInt(self->m_comp1);   /* 哨兵 → -1（qcolor.cpp:1834） */
}

int XColor_hslSaturation(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);
        return qt_div_257(hsl.m_comp2);
    }
    return qt_div_257(self->m_comp2);
}

int XColor_lightness(const XColor* self)
{
    if (!self) return 0;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);
        return qt_div_257(hsl.m_comp3);
    }
    return qt_div_257(self->m_comp3);
}

float XColor_hslHueF(const XColor* self)
{
    if (!self) return -1.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);
        return hueU16ToFloat(hsl.m_comp1);
    }
    return hueU16ToFloat(self->m_comp1);
}

float XColor_hslSaturationF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);
        return u16ToFloat(hsl.m_comp2);
    }
    return u16ToFloat(self->m_comp2);
}

float XColor_lightnessF(const XColor* self)
{
    if (!self) return 0.0f;
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);
        return u16ToFloat(hsl.m_comp3);
    }
    return u16ToFloat(self->m_comp3);
}

void XColor_getHsl(const XColor* self, int* h, int* s, int* l, int* a)
{
    if (!self) return;
    if (!h && !s && !l && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);   /* 三段式（qcolor.cpp:1160-1163） */
        XColor_getHsl(&hsl, h, s, l, a);
        return;
    }
    if (h) *h = hueU16ToInt(self->m_comp1);
    if (s) *s = qt_div_257(self->m_comp2);
    if (l) *l = qt_div_257(self->m_comp3);
    if (a) *a = qt_div_257(self->m_alpha);
}

void XColor_setHsl(XColor* self, int h, int s, int l, int a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setHsl，qcolor.cpp:1213-1227）：h < -1 或 s/l/a
     * 越界 → 警告 + 置无效；h≥360 不拒、按 (h%360)×100 回卷（与工厂族
     * create_hsl 的 h≥360 拒绝口径不同，两族不得混同） */
    if (h < -1 || s < 0 || s > 255 || l < 0 || l > 255 || a < 0 || a > 255) {
        XERROR_PRINTF("XColor_setHsl: HSL parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Hsl;
    self->m_alpha = intToU16(a);                                /* qcolor.cpp:1222 */
    self->m_comp1 = (h == -1) ? XCOLOR_HUE_SENTINEL             /* qcolor.cpp:1223 */
                          : (uint16_t)((h % 360) * 100);
    self->m_comp2 = intToU16(s);
    self->m_comp3 = intToU16(l);
    self->m_comp4 = 0;
}

void XColor_getHslF(const XColor* self, float* h, float* s, float* l, float* a)
{
    if (!self) return;
    if (!h && !s && !l && !a) return;   /* C 护栏 */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Hsl) {
        XColor hsl;
        XColor_toHsl(self, &hsl);   /* 三段式（qcolor.cpp:1130-1133） */
        XColor_getHslF(&hsl, h, s, l, a);
        return;
    }
    if (h) *h = hueU16ToFloat(self->m_comp1);
    if (s) *s = u16ToFloat(self->m_comp2);
    if (l) *l = u16ToFloat(self->m_comp3);
    if (a) *a = u16ToFloat(self->m_alpha);
}

void XColor_setHslF(XColor* self, float h, float s, float l, float a)
{
    if (!self) return;
    /* 整组拒绝档（对标 setHslF，qcolor.cpp:1183-1200）；注意 setHslF 的
     * hue 不做 36000 回卷（qcolor.cpp:1196 原样，仅 fromHslF 回卷） */
    if (((h < 0.0f || h > 1.0f) && h != -1.0f) ||
        s < 0.0f || s > 1.0f || l < 0.0f || l > 1.0f || a < 0.0f || a > 1.0f) {
        XERROR_PRINTF("XColor_setHslF: HSL parameters out of range");
        invalidateColor(self);
        return;
    }
    self->m_spec = XColor_Hsl;
    self->m_alpha = floatToU16(a);
    self->m_comp1 = (h == -1.0f) ? XCOLOR_HUE_SENTINEL
                          : (uint16_t)(h * 36000.0f + 0.5f);
    self->m_comp2 = floatToU16(s);
    self->m_comp3 = floatToU16(l);
    self->m_comp4 = 0;
}

/* ========== 转换族 ========== */

/*
 * 布局裁定（2026-10-03 对齐定版）：存储为"按规格位存该空间分量"——
 * m_comp1..4 在 Rgb/Hsv/Cmyk/Hsl 四规格下分别解释为该空间自己的分量
 * （对标 QColor::CT 联合的 argb/ahsv/acmyk/ahsl 视图），废除旧过渡契约
 * "分量恒存 RGB、规格位只记指定空间"。因此 toRgb/toHsv/toCmyk/toHsl 是
 * 真正的空间换算，而非规格位改记。
 *
 * 禁止回环：toRgb/toHsv/toCmyk/toHsl 及其私有换算路径内禁止调用
 * create_hsv/create_hsl/create_cmyk/setXxx 族与任何对外 API，只在裸存储
 * 上换算（static 辅助 + 局部变量直写 m_comp 字段）。2026-10-03 曾因
 * toRgb 内回环调用 create_hsv 打穿线程栈（颜色对话框点标准色块即崩，
 * ASan 栈溢出为凭）；C 无法以静态断言表达"禁调用"约束，以本注释固化
 * 纪律，后续改动任何转换函数时必须先读本段。
 */

void XColor_toRgb(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    if (self->m_spec == XColor_Invalid || self->m_spec == XColor_Rgb) {
        *out = *self;   /* 对标 qcolor.cpp:2035：Invalid/已是 Rgb 原样返回 */
        return;
    }

    XColor c = *self;
    c.m_spec = XColor_Rgb;
    c.m_comp4 = 0;
    /* alpha 原样携带（qcolor.cpp:2041） */

    if (self->m_spec == XColor_Hsv) {
        /* 对标 qcolor.cpp:2045-2102 */
        if (self->m_comp2 == 0 || self->m_comp1 == XCOLOR_HUE_SENTINEL) {
            /* 消色差（sat==0）或灰色哨兵：rgb = v 直拷（qcolor.cpp:2047-2050） */
            c.m_comp1 = self->m_comp3;
            c.m_comp2 = self->m_comp3;
            c.m_comp3 = self->m_comp3;
        } else {
            /* 色差分支：60° 扇区单位 + 奇偶扇区法（qcolor.cpp:2054-2101） */
            const float h = (self->m_comp1 == 36000u) ? 0.0f : (float)self->m_comp1 / 6000.0f;
            const float s = u16ToFloat(self->m_comp2);
            const float v = u16ToFloat(self->m_comp3);
            const int i = (int)h;
            const float f = h - (float)i;
            const float p = v * (1.0f - s);
            uint16_t r = 0, g = 0, b = 0;

            if (i & 1) {
                const float q = v * (1.0f - (s * f));
                switch (i) {
                case 1: r = floatToU16(q); g = floatToU16(v); b = floatToU16(p); break;
                case 3: r = floatToU16(p); g = floatToU16(q); b = floatToU16(v); break;
                case 5: r = floatToU16(v); g = floatToU16(p); b = floatToU16(q); break;
                default: break;
                }
            } else {
                const float t = v * (1.0f - (s * (1.0f - f)));
                switch (i) {
                case 0: r = floatToU16(v); g = floatToU16(t); b = floatToU16(p); break;
                case 2: r = floatToU16(p); g = floatToU16(v); b = floatToU16(t); break;
                case 4: r = floatToU16(t); g = floatToU16(p); b = floatToU16(v); break;
                default: break;
                }
            }
            c.m_comp1 = r;
            c.m_comp2 = g;
            c.m_comp3 = b;
        }
    } else if (self->m_spec == XColor_Hsl) {
        /* 对标 qcolor.cpp:2104-2148 */
        if (self->m_comp2 == 0 || self->m_comp1 == XCOLOR_HUE_SENTINEL) {
            /* 消色差/灰色哨兵：rgb = lightness 直拷（qcolor.cpp:2106-2108） */
            c.m_comp1 = self->m_comp3;
            c.m_comp2 = self->m_comp3;
            c.m_comp3 = self->m_comp3;
        } else if (self->m_comp3 == 0) {
            /* 亮度 0：全 0（qcolor.cpp:2109-2111） */
            c.m_comp1 = 0;
            c.m_comp2 = 0;
            c.m_comp3 = 0;
        } else {
            /* 色差分支：temp1/temp2/temp3 六分段（qcolor.cpp:2114-2144）。
             * Qt 写 ct.array[i+1]（array[0]=alpha，[1..3]=r/g/b），此处以
             * 局部数组等价落 r/g/b。 */
            const float h = (self->m_comp1 == 36000u) ? 0.0f : (float)self->m_comp1 / 36000.0f;   /* qcolor.cpp:2114 */
            const float s = u16ToFloat(self->m_comp2);
            const float l = u16ToFloat(self->m_comp3);
            float temp2;
            float temp3[3];
            uint16_t rgbArr[3];
            int i;

            if (l < 0.5f)
                temp2 = l * (1.0f + s);
            else
                temp2 = l + s - (l * s);

            const float temp1 = (2.0f * l) - temp2;

            temp3[0] = h + (1.0f / 3.0f);
            temp3[1] = h;
            temp3[2] = h - (1.0f / 3.0f);

            for (i = 0; i != 3; ++i) {
                if (temp3[i] < 0.0f)
                    temp3[i] += 1.0f;
                else if (temp3[i] > 1.0f)
                    temp3[i] -= 1.0f;

                const float sixtemp3 = temp3[i] * 6.0f;
                if (sixtemp3 < 1.0f)
                    rgbArr[i] = floatToU16(temp1 + (temp2 - temp1) * sixtemp3);
                else if ((temp3[i] * 2.0f) < 1.0f)
                    rgbArr[i] = floatToU16(temp2);
                else if ((temp3[i] * 3.0f) < 2.0f)
                    rgbArr[i] = floatToU16(temp1 + (temp2 - temp1) * (2.0f / 3.0f - temp3[i]) * 6.0f);
                else
                    rgbArr[i] = floatToU16(temp1);
            }
            /* u16==1 → 0 修正（qcolor.cpp:2145-2147） */
            if (rgbArr[0] == 1u) rgbArr[0] = 0;
            if (rgbArr[1] == 1u) rgbArr[1] = 0;
            if (rgbArr[2] == 1u) rgbArr[2] = 0;
            c.m_comp1 = rgbArr[0];
            c.m_comp2 = rgbArr[1];
            c.m_comp3 = rgbArr[2];
        }
    } else if (self->m_spec == XColor_Cmyk) {
        /* 对标 qcolor.cpp:2151-2161：1-(c(1-k)+k)（缺陷 #5 根除） */
        const float cc = u16ToFloat(self->m_comp1);
        const float mm = u16ToFloat(self->m_comp2);
        const float yy = u16ToFloat(self->m_comp3);
        const float kk = u16ToFloat(self->m_comp4);
        c.m_comp1 = floatToU16(1.0f - (cc * (1.0f - kk) + kk));
        c.m_comp2 = floatToU16(1.0f - (mm * (1.0f - kk) + kk));
        c.m_comp3 = floatToU16(1.0f - (yy * (1.0f - kk) + kk));
    } else {
        /* 不可达（仅 Rgb/Hsv/Cmyk/Hsl 会进入本函数，Rgb/Invalid 已早退） */
        *out = *self;
        return;
    }

    *out = c;
}

void XColor_toHsv(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    if (self->m_spec == XColor_Invalid || self->m_spec == XColor_Hsv) {
        *out = *self;   /* 对标 qcolor.cpp:2188-2189 */
        return;
    }
    if (self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 对标 qcolor.cpp:2191-2192：先转 RGB 链 */
        XColor_toHsv(&rgb, out);
        return;
    }

    /* RGB→HSV 浮点逐度（缺陷 #1 根除）。qFuzzy* 属 qnumeric.h（未随包提
     * 供，任何具体阈值不可引证）；对 u16 量化值任一公开阈值口径下（绝对
     * 阈或相对阈）精确比较等价：量化值不等 ⇒ |r−max| ≥ 1/65535 ≈ 1.5e-5，
     * 高于两口径阈值，故 delta==0.0f / r==max 精确比较安全。max 相同时
     * 段序保持 r→g→b（qcolor.cpp:2214-2219）。 */
    XColor c = *self;
    c.m_spec = XColor_Hsv;
    c.m_comp4 = 0;
    /* alpha 原样携带（qcolor.cpp:2196） */

    const float r = u16ToFloat(self->m_comp1);
    const float g = u16ToFloat(self->m_comp2);
    const float b = u16ToFloat(self->m_comp3);
    const float max = max3f(r, g, b);
    const float min = min3f(r, g, b);
    const float delta = max - min;

    c.m_comp3 = floatToU16(max);   /* value（qcolor.cpp:2205） */
    if (delta == 0.0f) {
        /* 消色差：hue 无定义 → 哨兵、sat 0（qcolor.cpp:2206-2209，缺陷 #4 根除） */
        c.m_comp1 = XCOLOR_HUE_SENTINEL;
        c.m_comp2 = 0;
    } else {
        float hue;
        c.m_comp2 = floatToU16(delta / max);   /* qcolor.cpp:2213 */
        if (r == max)
            hue = (g - b) / delta;
        else if (g == max)
            hue = 2.0f + (b - r) / delta;
        else
            hue = 4.0f + (r - g) / delta;   /* 此时 b == max 必然成立 */
        hue *= 60.0f;
        if (hue < 0.0f)
            hue += 360.0f;
        /* 百分之一度直存（qcolor.cpp:2226；验收样例 rgb(255,165,0) 存
         * 3882，getHsv/hsvHue 读出 hue/100 整除截断 = 38） */
        c.m_comp1 = (uint16_t)(hue * 100.0f + 0.5f);
    }
    *out = c;
}

void XColor_toCmyk(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    if (self->m_spec == XColor_Invalid || self->m_spec == XColor_Cmyk) {
        *out = *self;   /* 对标 qcolor.cpp:2295-2296 */
        return;
    }
    if (self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 对标 qcolor.cpp:2297-2298 */
        XColor_toCmyk(&rgb, out);
        return;
    }

    XColor c = *self;
    c.m_spec = XColor_Cmyk;
    /* alpha 原样携带（qcolor.cpp:2302） */
    if (self->m_comp1 == 0 && self->m_comp2 == 0 && self->m_comp3 == 0) {
        /* RGB 全零（仅全零进此支，守卫窄于旧实现 kk==255 分支）：避免
         * 除零（qcolor.cpp:2304-2309） */
        c.m_comp1 = 0;
        c.m_comp2 = 0;
        c.m_comp3 = 0;
        c.m_comp4 = XCOLOR_HUE_SENTINEL;   /* k = 65535（USHRT_MAX） */
    } else {
        /* rgb → cmy → cmyk（qcolor.cpp:2311-2328，缺陷 #3 根除） */
        const float r = u16ToFloat(self->m_comp1);
        const float g = u16ToFloat(self->m_comp2);
        const float b = u16ToFloat(self->m_comp3);
        float cc = 1.0f - r;
        float mm = 1.0f - g;
        float yy = 1.0f - b;

        const float kk = min3f(cc, mm, yy);
        cc = (cc - kk) / (1.0f - kk);
        mm = (mm - kk) / (1.0f - kk);
        yy = (yy - kk) / (1.0f - kk);

        c.m_comp1 = floatToU16(cc);
        c.m_comp2 = floatToU16(mm);
        c.m_comp3 = floatToU16(yy);
        c.m_comp4 = floatToU16(kk);
    }
    *out = c;
}

void XColor_toHsl(const XColor* self, XColor* out)
{
    if (!self || !out) return;
    if (self->m_spec == XColor_Invalid || self->m_spec == XColor_Hsl) {
        *out = *self;   /* 对标 qcolor.cpp:2239-2240 */
        return;
    }
    if (self->m_spec != XColor_Rgb) {
        XColor rgb;
        XColor_toRgb(self, &rgb);   /* 对标 qcolor.cpp:2242-2243 */
        XColor_toHsl(&rgb, out);
        return;
    }

    /* RGB→HSL 浮点逐度（缺陷 #2 根除）；qFuzzy* 精确比较等价性论证同
     * XColor_toHsv 注 */
    XColor c = *self;
    c.m_spec = XColor_Hsl;
    c.m_comp4 = 0;
    /* alpha 原样携带（qcolor.cpp:2247） */

    const float r = u16ToFloat(self->m_comp1);
    const float g = u16ToFloat(self->m_comp2);
    const float b = u16ToFloat(self->m_comp3);
    const float max = max3f(r, g, b);
    const float min = min3f(r, g, b);
    const float delta = max - min;
    const float delta2 = max + min;
    const float lightness = 0.5f * delta2;

    c.m_comp3 = floatToU16(lightness);   /* qcolor.cpp:2258 */
    if (delta == 0.0f) {
        /* 消色差：hue 哨兵、sat 0（qcolor.cpp:2259-2262） */
        c.m_comp1 = XCOLOR_HUE_SENTINEL;
        c.m_comp2 = 0;
    } else {
        float hue;
        /* 饱和度按亮度 <0.5 切换分母 delta2 / 2-delta2（qcolor.cpp:2266-2269） */
        if (lightness < 0.5f)
            c.m_comp2 = floatToU16(delta / delta2);
        else
            c.m_comp2 = floatToU16(delta / (2.0f - delta2));
        if (r == max)
            hue = (g - b) / delta;
        else if (g == max)
            hue = 2.0f + (b - r) / delta;
        else
            hue = 4.0f + (r - g) / delta;
        hue *= 60.0f;
        if (hue < 0.0f)
            hue += 360.0f;
        c.m_comp1 = (uint16_t)(hue * 100.0f + 0.5f);   /* qcolor.cpp:2282 */
    }
    *out = c;
}

XColor XColor_convertTo(const XColor* self, XColor_Spec colorSpec)
{
    if (!self) return XColor_create();
    /* 对标 QColor::convertTo（qcolor.cpp:2334-2353） */
    if (colorSpec == self->m_spec) return *self;
    switch (colorSpec) {
    case XColor_Rgb: {
        XColor o;
        XColor_toRgb(self, &o);
        return o;
    }
    case XColor_Hsv: {
        XColor o;
        XColor_toHsv(self, &o);
        return o;
    }
    case XColor_Cmyk: {
        XColor o;
        XColor_toCmyk(self, &o);
        return o;
    }
    case XColor_Hsl: {
        XColor o;
        XColor_toHsl(self, &o);
        return o;
    }
    case XColor_ExtendedRgb:
        /* 声明边界（裁定 #12）：ExtendedRgb 不实现，落 Invalid 同款路径
         *（偏差声明，见 XColor.h 枚举注） */
    case XColor_Invalid:
        break;
    }
    return XColor_create(); /* must be invalid */
}

XColor XColor_lighter(const XColor* self, int factor)
{
    if (!self) return XColor_create();
    /* 对标 QColor::lighter（qcolor.cpp:2810-2835） */
    if (factor <= 0) return *self;                                  /* invalid lightness factor */
    if (factor < 100) return XColor_darker(self, 10000 / factor);   /* makes color darker */

    XColor hsv;
    XColor_toHsv(self, &hsv);
    {
        int s = hsv.m_comp2;
        uint32_t v = hsv.m_comp3;

        v = ((uint32_t)factor * v) / 100u;
        if (v > 65535u) {
            /* overflow... adjust saturation（qcolor.cpp:2822-2828 原样照抄） */
            s -= (int)(v - 65535u);
            if (s < 0)
                s = 0;
            v = 65535u;
        }

        hsv.m_comp2 = (uint16_t)s;
        hsv.m_comp3 = (uint16_t)v;
    }

    /* convert back to same color spec as original color */
    return XColor_convertTo(&hsv, self->m_spec);
}

XColor XColor_darker(const XColor* self, int factor)
{
    if (!self) return XColor_create();
    /* 对标 QColor::darker（qcolor.cpp:2855-2867） */
    if (factor <= 0) return *self;                                  /* invalid darkness factor */
    if (factor < 100) return XColor_lighter(self, 10000 / factor);  /* makes color lighter */

    XColor hsv;
    XColor_toHsv(self, &hsv);
    hsv.m_comp3 = (uint16_t)(((uint32_t)hsv.m_comp3 * 100u) / (uint32_t)factor);

    /* convert back to same color spec as original color */
    return XColor_convertTo(&hsv, self->m_spec);
}

/* ========== 32 位值 / 比较 ========== */

uint32_t XColor_rgba(const XColor* self)
{
    if (!self) return 0;   /* C 护栏（Qt 无此分支） */
    if (self->m_spec != XColor_Invalid && self->m_spec != XColor_Rgb) {
        /* 三段式：非 Rgb（Invalid 除外）先转再读（qcolor.cpp:1376-1377）。
         * 局部换算，不递归调用自身（禁止回环纪律，见 XColor_toRgb 头注）。 */
        XColor rgb;
        XColor_toRgb(self, &rgb);
        return ((uint32_t)qt_div_257(rgb.m_alpha) << 24) |
               ((uint32_t)qt_div_257(rgb.m_comp1) << 16) |
               ((uint32_t)qt_div_257(rgb.m_comp2) << 8) |
               (uint32_t)qt_div_257(rgb.m_comp3);
    }
    /* Invalid 不特判：按无效存储（alpha=65535、分量全 0）换算得
     * 0xFF000000（qcolor.cpp:1378；Qt 文档称该值"未指定"，以实现为准，
     * 旧实现的 Invalid→0 早退废除） */
    return ((uint32_t)qt_div_257(self->m_alpha) << 24) |
           ((uint32_t)qt_div_257(self->m_comp1) << 16) |
           ((uint32_t)qt_div_257(self->m_comp2) << 8) |
           (uint32_t)qt_div_257(self->m_comp3);
}

uint32_t XColor_rgb(const XColor* self)
{
    /* 对标 QColor::rgb()（qcolor.cpp:1437-1442）+ qRgb 四元组 (255,r,g,b)
     * （qcolor.cpp:3123-3129）：高位字节置 0xFF（旧 &0x00FFFFFF 实现与
     * "0x00RRGGBB" 注释均废除） */
    return 0xFF000000u | (XColor_rgba(self) & 0x00FFFFFFu);
}

void XColor_setRgba(XColor* self, uint32_t argb)
{
    if (!self) return;
    /* 对标 QColor::setRgba(QRgb)（qcolor.cpp:1386-1394）：含 alpha，各通
     * 道 ×0x101 直存（保留不删——qcolor.h:108 有对应物，规格 §2 改判） */
    self->m_spec = XColor_Rgb;
    self->m_alpha = intToU16((int)((argb >> 24) & 0xFF));
    self->m_comp1 = intToU16((int)((argb >> 16) & 0xFF));
    self->m_comp2 = intToU16((int)((argb >> 8) & 0xFF));
    self->m_comp3 = intToU16((int)(argb & 0xFF));
    self->m_comp4 = 0;
}

void XColor_setRgb_uint32(XColor* self, uint32_t rgb)
{
    if (!self) return;
    /* 对标 QColor::setRgb(QRgb)（qcolor.cpp:1449-1457）：alpha 置 0xffff、
     * 入参高位忽略（保留不删——qcolor.h:111 有对应物，规格 §2 改判） */
    self->m_spec = XColor_Rgb;
    self->m_alpha = 65535;
    self->m_comp1 = intToU16((int)((rgb >> 16) & 0xFF));
    self->m_comp2 = intToU16((int)((rgb >> 8) & 0xFF));
    self->m_comp3 = intToU16((int)(rgb & 0xFF));
    self->m_comp4 = 0;
}

bool XColor_equals(const XColor* a, const XColor* b)
{
    /* 对标 QColor::operator==（qcolor.cpp:2885-2903） */
    if (!a || !b) return false;   /* C 护栏 */
    if (a->m_spec == XColor_Invalid && b->m_spec == XColor_Invalid) return true;
    if (a->m_spec != b->m_spec) return false;
    /* ExtendedRgb↔Rgb 互容分支（qcolor.cpp:2887-2892）按声明边界不实现：
     * ExtendedRgb 永不实例化 */
    if (a->m_alpha != b->m_alpha) return false;
    if (a->m_spec == XColor_Hsv || a->m_spec == XColor_Hsl) {
        /* hue 模 36000 比较：36000≡0；哨兵 65535≡29535，双方一致仍可等
         * （qcolor.cpp:2896-2897） */
        if (((int)a->m_comp1 % 36000) != ((int)b->m_comp1 % 36000))
            return false;
    } else {
        if (a->m_comp1 != b->m_comp1) return false;
    }
    if (a->m_comp2 != b->m_comp2) return false;
    if (a->m_comp3 != b->m_comp3) return false;
    /* Qt 经联合把 black 落在 pad 位比较（qcolor.cpp:2901）；C 具名字段直
     * 比：Cmyk 比 comp4（黑色），其余规格 comp4 为填充位恒 0，同效 */
    if (a->m_spec == XColor_Cmyk && a->m_comp4 != b->m_comp4) return false;
    return true;
}

/* ========== SVG/CSS 命名颜色表 ========== */

/* CSS 色名 = SVG 1.0 色名 + transparent（rgba(0,0,0,0)）；与 Qt rgbTbl
 * （qcolor.cpp:130-279，148 项）全量对齐。a 字段支持 transparent 的
 * alpha=0（qcolor.cpp:271 { "transparent", 0 }），其余恒 255。 */
typedef struct { const char* name; uint8_t r, g, b, a; } XColorNamedEntry;

static const XColorNamedEntry s_namedColors[] = {
    {"aliceblue", 240, 248, 255, 255}, {"antiquewhite", 250, 235, 215, 255},
    {"aqua", 0, 255, 255, 255}, {"aquamarine", 127, 255, 212, 255},
    {"azure", 240, 255, 255, 255}, {"beige", 245, 245, 220, 255},
    {"bisque", 255, 228, 196, 255}, {"black", 0, 0, 0, 255},
    {"blanchedalmond", 255, 235, 205, 255}, {"blue", 0, 0, 255, 255},
    {"blueviolet", 138, 43, 226, 255}, {"brown", 165, 42, 42, 255},
    {"burlywood", 222, 184, 135, 255}, {"cadetblue", 95, 158, 160, 255},
    {"chartreuse", 127, 255, 0, 255}, {"chocolate", 210, 105, 30, 255},
    {"coral", 255, 127, 80, 255}, {"cornflowerblue", 100, 149, 237, 255},
    {"cornsilk", 255, 248, 220, 255}, {"crimson", 220, 20, 60, 255},
    {"cyan", 0, 255, 255, 255}, {"darkblue", 0, 0, 139, 255},
    {"darkcyan", 0, 139, 139, 255}, {"darkgoldenrod", 184, 134, 11, 255},
    {"darkgray", 169, 169, 169, 255}, {"darkgreen", 0, 100, 0, 255},
    {"darkgrey", 169, 169, 169, 255}, {"darkkhaki", 189, 183, 107, 255},
    {"darkmagenta", 139, 0, 139, 255}, {"darkolivegreen", 85, 107, 47, 255},
    {"darkorange", 255, 140, 0, 255}, {"darkorchid", 153, 50, 204, 255},
    {"darkred", 139, 0, 0, 255}, {"darksalmon", 233, 150, 122, 255},
    {"darkseagreen", 143, 188, 143, 255}, {"darkslateblue", 72, 61, 139, 255},
    {"darkslategray", 47, 79, 79, 255}, {"darkslategrey", 47, 79, 79, 255},
    {"darkturquoise", 0, 206, 209, 255}, {"darkviolet", 148, 0, 211, 255},
    {"deeppink", 255, 20, 147, 255}, {"deepskyblue", 0, 191, 255, 255},
    {"dimgray", 105, 105, 105, 255}, {"dimgrey", 105, 105, 105, 255},
    {"dodgerblue", 30, 144, 255, 255}, {"firebrick", 178, 34, 34, 255},
    {"floralwhite", 255, 250, 240, 255}, {"forestgreen", 34, 139, 34, 255},
    {"fuchsia", 255, 0, 255, 255}, {"gainsboro", 220, 220, 220, 255},
    {"ghostwhite", 248, 248, 255, 255}, {"gold", 255, 215, 0, 255},
    {"goldenrod", 218, 165, 32, 255}, {"gray", 128, 128, 128, 255},
    {"green", 0, 128, 0, 255}, {"greenyellow", 173, 255, 47, 255},
    {"grey", 128, 128, 128, 255}, {"honeydew", 240, 255, 240, 255},
    {"hotpink", 255, 105, 180, 255}, {"indianred", 205, 92, 92, 255},
    {"indigo", 75, 0, 130, 255}, {"ivory", 255, 255, 240, 255},
    {"khaki", 240, 230, 140, 255}, {"lavender", 230, 230, 250, 255},
    {"lavenderblush", 255, 240, 245, 255}, {"lawngreen", 124, 252, 0, 255},
    {"lemonchiffon", 255, 250, 205, 255}, {"lightblue", 173, 216, 230, 255},
    {"lightcoral", 240, 128, 128, 255}, {"lightcyan", 224, 255, 255, 255},
    {"lightgoldenrodyellow", 250, 250, 210, 255}, {"lightgray", 211, 211, 211, 255},
    {"lightgreen", 144, 238, 144, 255}, {"lightgrey", 211, 211, 211, 255},
    {"lightpink", 255, 182, 193, 255}, {"lightsalmon", 255, 160, 122, 255},
    {"lightseagreen", 32, 178, 170, 255}, {"lightskyblue", 135, 206, 250, 255},
    {"lightslategray", 119, 136, 153, 255}, {"lightslategrey", 119, 136, 153, 255},
    {"lightsteelblue", 176, 196, 222, 255}, {"lightyellow", 255, 255, 224, 255},
    {"lime", 0, 255, 0, 255}, {"limegreen", 50, 205, 50, 255},
    {"linen", 250, 240, 230, 255}, {"magenta", 255, 0, 255, 255},
    {"maroon", 128, 0, 0, 255}, {"mediumaquamarine", 102, 205, 170, 255},
    {"mediumblue", 0, 0, 205, 255}, {"mediumorchid", 186, 85, 211, 255},
    {"mediumpurple", 147, 112, 219, 255}, {"mediumseagreen", 60, 179, 113, 255},
    {"mediumslateblue", 123, 104, 238, 255}, {"mediumspringgreen", 0, 250, 154, 255},
    {"mediumturquoise", 72, 209, 204, 255}, {"mediumvioletred", 199, 21, 133, 255},
    {"midnightblue", 25, 25, 112, 255}, {"mintcream", 245, 255, 250, 255},
    {"mistyrose", 255, 228, 225, 255}, {"moccasin", 255, 228, 181, 255},
    {"navajowhite", 255, 222, 173, 255}, {"navy", 0, 0, 128, 255},
    {"oldlace", 253, 245, 230, 255}, {"olive", 128, 128, 0, 255},
    {"olivedrab", 107, 142, 35, 255}, {"orange", 255, 165, 0, 255},
    {"orangered", 255, 69, 0, 255}, {"orchid", 218, 112, 214, 255},
    {"palegoldenrod", 238, 232, 170, 255}, {"palegreen", 152, 251, 152, 255},
    {"paleturquoise", 175, 238, 238, 255}, {"palevioletred", 219, 112, 147, 255},
    {"papayawhip", 255, 239, 213, 255}, {"peachpuff", 255, 218, 185, 255},
    {"peru", 205, 133, 63, 255}, {"pink", 255, 192, 203, 255},
    {"plum", 221, 160, 221, 255}, {"powderblue", 176, 224, 230, 255},
    {"purple", 128, 0, 128, 255}, {"red", 255, 0, 0, 255},
    {"rosybrown", 188, 143, 143, 255}, {"royalblue", 65, 105, 225, 255},
    {"saddlebrown", 139, 69, 19, 255}, {"salmon", 250, 128, 114, 255},
    {"sandybrown", 244, 164, 96, 255}, {"seagreen", 46, 139, 87, 255},
    {"seashell", 255, 245, 238, 255}, {"sienna", 160, 82, 45, 255},
    {"silver", 192, 192, 192, 255}, {"skyblue", 135, 206, 235, 255},
    {"slateblue", 106, 90, 205, 255}, {"slategray", 112, 128, 144, 255},
    {"slategrey", 112, 128, 144, 255}, {"snow", 255, 250, 250, 255},
    {"springgreen", 0, 255, 127, 255}, {"steelblue", 70, 130, 180, 255},
    {"tan", 210, 180, 140, 255}, {"teal", 0, 128, 128, 255},
    {"thistle", 216, 191, 216, 255}, {"tomato", 255, 99, 71, 255},
    {"transparent", 0, 0, 0, 0}, {"turquoise", 64, 224, 208, 255},
    {"violet", 238, 130, 238, 255}, {"wheat", 245, 222, 179, 255},
    {"white", 255, 255, 255, 255}, {"whitesmoke", 245, 245, 245, 255},
    {"yellow", 255, 255, 0, 255}, {"yellowgreen", 154, 205, 50, 255},
};

static const int s_namedColorCount = sizeof(s_namedColors) / sizeof(s_namedColors[0]);

XColor XColor_fromName(const XString* name)
{
    /* 对标 QColor 构造路径的 get_named_rgb（qcolor.cpp:314-329）：查找前
     * 剔除全部空格与 \t、ASCII 小写化、长度上限 255（qcolor.cpp:316-326） */
    if (!name) return XColor_create();
    if ((int)XString_size((const XContainer*)name) > 255) return XColor_create();   /* qcolor.cpp:316-317 */
    const char* utf8 = XString_toUtf8(name);
    if (!utf8 || utf8[0] == '\0') return XColor_create();

    char nameNoSpace[256];
    int pos = 0;
    const char* p;
    for (p = utf8; *p; ++p) {
        char ch = *p;
        if (ch == ' ' || ch == '\t')
            continue;   /* 剔除空格与制表符（qcolor.cpp:322） */
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');   /* ASCII 小写化（toAsciiLower） */
        nameNoSpace[pos++] = ch;
    }
    nameNoSpace[pos] = '\0';

    for (int i = 0; i < s_namedColorCount; i++) {
        /* 表项均小写 ASCII。逐字符受界比较（不得对短于 pos 的字符串字面
         * 量做 memcmp(pos)——会越过字面量存储越界读，ASan 实证） */
        const char* e = s_namedColors[i].name;
        int j = 0;
        while (j < pos && e[j] != '\0' && nameNoSpace[j] == e[j])
            ++j;
        if (j == pos && e[j] == '\0') {
            return XColor_create_rgb(s_namedColors[i].r, s_namedColors[i].g,
                                     s_namedColors[i].b, s_namedColors[i].a);
        }
    }
    return XColor_create();
}

XStringList* XColor_colorNames(void)
{
    /* 对齐 QColor::colorNames()（qcolor.cpp:1000-1003）：148 项 */
    XStringList* list = XStringList_create();
    if (!list) return NULL;
    for (int i = 0; i < s_namedColorCount; i++) {
        XStringList_push_back_utf8(list, s_namedColors[i].name);
    }
    return list;
}
