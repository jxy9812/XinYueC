#include "XStyleSheetStyle.h"
#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XFont.h"
#include "XObject.h"
#include "XWidget.h" /* XWidget_styleSheet（级联控件级源）+ isWidgetType 守卫。 */
#include "XVariant.h" /* XVariant_data 显式声明（此前隐式声明按 int 取返回，
                       * 64 位下指针截断，属实际隐患，顺带归位）。 */

/* ===== G4 新增 include（lane-palette-text 线：扩展取色链与文本属性消费） ===== */
#include "XChar.h" /* text-transform 码点映射（XChar_toUpper/ToLower/isSpace）。 */
#include "XCssValue.h" /* palette(role)/渐变类型化解析（值解析线合同 API）。 */
#include "XPalette.h" /* palette 角色色解析设施（XPalette_color）。 */

/* ===== G3 新增 include（lane-image-box 线：渐变栅格化数学原语） ===== */
#include <math.h> /* 渐变栅格化 sqrt/atan2/fmod（math.h 为白名单头）。 */
#include "XGuiApplication.h" /* palette 应用级回落（XGuiApplication_palette）。 */

#if XSTYLE_ON

/* ==================== 级联多源常量 ==================== */

/** @brief 样式表 origin 取值：对标 Qt 6.8.3 qcssparser_p.h
 *         StyleSheetOrigin_Inline（枚举第 5 位=4）；QStyleSheetStyle::
 *         styleRules 对应用级表与控件级表均置 Inline，源间位阶由 depth
 *         区分（应用级 depth=1、控件级第 i 个（近→远）depth=size-i+2，
 *         权重式 (origin+depth)*0x100000）。 */
#define XSSS_ORIGIN_INLINE 4

/** @brief 级联源收集上限（应用级 1 + 控件链 7）。Qt 无上限；本库按栈
 *         上收集数组定容，超深嵌套且逐层都带样式表的场景按近控件优先
 *         截断（深度位阶保证近者恒胜，截断只丢远端弱源）。 */
#define XSSS_MAX_CASCADE_SOURCES 8

/** @brief 控件源解析缓存容量。≥ 1+XSSS_MAX_CASCADE_SOURCES，保证单次
 *         收集走查内不会环形替换掉本次已收集的条目（收集到的规则指针
 *         在走查期间恒有效）。 */
#define XSSS_SOURCE_CACHE_SIZE 16



/* ==================== 值解析 ==================== */

/** @brief 16 进制单字符 → 值（-1 非法）。 */
static int xsss_hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 十进制浮点数解析（可选符号/整数/小数；无 libc；至少 1 位
 *         数字才成功）。
 *
 *  对标 QStringView::toDouble 子集：不认指数记号（CSS 长度/颜色分量
 *  场景够用）；失败时不动 *pp。 */
static bool xsss_parseDouble(const char** pp, double* out)
{
    const char* p = *pp;
    double v = 0.0;
    double frac = 0.0;
    double scale = 0.1;
    bool neg = false;
    bool any = false;
    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        ++p;
    }
    while (XIsDigit((unsigned char)*p)) {
        v = v * 10.0 + (double)(*p - '0');
        ++p;
        any = true;
    }
    if (*p == '.') {
        ++p;
        while (XIsDigit((unsigned char)*p)) {
            frac += (double)(*p - '0') * scale;
            scale *= 0.1;
            ++p;
            any = true;
        }
    }
    if (!any) return false;
    v += frac;
    *out = neg ? -v : v;
    *pp = p;
    return true;
}

/** @brief 长度单位后缀匹配（大小写不敏感；2 字符 px/pt/em/ex、
 *         1 字符 %；其余 None）。 */
static XCssLengthUnit xsss_lengthUnit(const char* p)
{
    if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 'x' || p[1] == 'X'))
        return XCssLengthUnit_Px;
    if ((p[0] == 'p' || p[0] == 'P') && (p[1] == 't' || p[1] == 'T'))
        return XCssLengthUnit_Pt;
    if ((p[0] == 'e' || p[0] == 'E') && (p[1] == 'm' || p[1] == 'M'))
        return XCssLengthUnit_Em;
    if ((p[0] == 'e' || p[0] == 'E') && (p[1] == 'x' || p[1] == 'X'))
        return XCssLengthUnit_Ex;
    if (p[0] == '%') return XCssLengthUnit_Percent;
    return XCssLengthUnit_None;
}

bool XCssParseLengthEx(const char* value, XCssLength* out)
{
    const char* p;
    double v = 0.0;
    if (!value || !out) return false;
    out->m_value = 0.0;
    out->m_unit = XCssLengthUnit_None;
    while (*value && XIsSpace((unsigned char)*value)) ++value;
    p = value;
    if (!xsss_parseDouble(&p, &v)) return false;
    while (*p && XIsSpace((unsigned char)*p)) ++p; /* 数字与单位间空白容忍。 */
    if (*p == '\0') {
        /* 裸数字按 Px（Qt LengthData::None 经 lengthValueFromData
         * scale=1 等效 px；此处直接归 Px 语义更明确）。 */
        out->m_value = v;
        out->m_unit = XCssLengthUnit_Px;
        return true;
    }
    {
        XCssLengthUnit u = xsss_lengthUnit(p);
        if (u == XCssLengthUnit_None) return false; /* 尾部垃圾整串拒绝。 */
        p += (u == XCssLengthUnit_Percent) ? 1 : 2;
        while (*p && XIsSpace((unsigned char)*p)) ++p; /* 单位后空白容忍。 */
        if (*p != '\0') return false; /* "12px3" 之类拒绝。 */
        out->m_value = v;
        out->m_unit = u;
        return true;
    }
}

/** @brief 舍入到最近整数（半值远离零，对标 qRound）。 */
static int xsss_roundHalfAway(double v)
{
    return (int)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

/** @brief Pt→Px（CSS：1pt = 1/72in、1px = 1/96in ⇒ px = pt × 96/72）。 */
static double xsss_ptToPx(double pt)
{
    return pt * 96.0 / 72.0;
}

bool XCssParseLength(const char* value, int* out)
{
    XCssLength len;
    if (!value || !out) return false;
    if (!XCssParseLengthEx(value, &len)) return false;
    switch (len.m_unit) {
    case XCssLengthUnit_Px:
        *out = xsss_roundHalfAway(len.m_value);
        return true;
    case XCssLengthUnit_Pt:
        *out = xsss_roundHalfAway(xsss_ptToPx(len.m_value));
        return true;
    case XCssLengthUnit_Em:
    case XCssLengthUnit_Ex:
        /* 旧签名无字体上下文：按 16px 基准（ex 取其半，与带上下文
         * 消费端的 x 高口径一致）；头文件注释同步声明。 */
        *out = xsss_roundHalfAway(len.m_value * 16.0 /
                                  (len.m_unit == XCssLengthUnit_Ex ? 2.0 : 1.0));
        return true;
    case XCssLengthUnit_Percent:
        /* 旧签名无矩形上下文：百分比按 0（"50%"→50px 属静默错值；
         * 带上下文的消费端经 xsss_lengthPx 换算）。 */
        *out = 0;
        return true;
    default:
        return false;
    }
}

/* ==================== 具名色 ==================== */

/* 148 项对标 Qt 6.8.3 rgbTbl + rebeccapurple 扩展（共 149 项 =
 * qcolor.cpp rgbTbl 全表逐项对标 [SVG 1.0 具名色全表 + transparent] +
 * rebeccapurple 扩展项——get_named_rgb 小写并剔除名字内部空白后二分；
 * 本表沿用其排序，值取 rgb(r,g,b) 8 位三元组 + alpha 字节，
 * transparent 为 Qt 表尾条目（alpha 0）。
 * 查找线性扫 + 大小写不敏感 + 忽略内部空白（语义同 get_named_rgb，
 * 条目量级线性即可）。 */
static const struct
{
    const char* n;
    unsigned char r, g, b, a;
} xsss_namedColors[] = {
    { "aliceblue", 240, 248, 255, 255 },
    { "antiquewhite", 250, 235, 215, 255 },
    { "aqua", 0, 255, 255, 255 },
    { "aquamarine", 127, 255, 212, 255 },
    { "azure", 240, 255, 255, 255 },
    { "beige", 245, 245, 220, 255 },
    { "bisque", 255, 228, 196, 255 },
    { "black", 0, 0, 0, 255 },
    { "blanchedalmond", 255, 235, 205, 255 },
    { "blue", 0, 0, 255, 255 },
    { "blueviolet", 138, 43, 226, 255 },
    { "brown", 165, 42, 42, 255 },
    { "burlywood", 222, 184, 135, 255 },
    { "cadetblue", 95, 158, 160, 255 },
    { "chartreuse", 127, 255, 0, 255 },
    { "chocolate", 210, 105, 30, 255 },
    { "coral", 255, 127, 80, 255 },
    { "cornflowerblue", 100, 149, 237, 255 },
    { "cornsilk", 255, 248, 220, 255 },
    { "crimson", 220, 20, 60, 255 },
    { "cyan", 0, 255, 255, 255 },
    { "darkblue", 0, 0, 139, 255 },
    { "darkcyan", 0, 139, 139, 255 },
    { "darkgoldenrod", 184, 134, 11, 255 },
    { "darkgray", 169, 169, 169, 255 },
    { "darkgreen", 0, 100, 0, 255 },
    { "darkgrey", 169, 169, 169, 255 },
    { "darkkhaki", 189, 183, 107, 255 },
    { "darkmagenta", 139, 0, 139, 255 },
    { "darkolivegreen", 85, 107, 47, 255 },
    { "darkorange", 255, 140, 0, 255 },
    { "darkorchid", 153, 50, 204, 255 },
    { "darkred", 139, 0, 0, 255 },
    { "darksalmon", 233, 150, 122, 255 },
    { "darkseagreen", 143, 188, 143, 255 },
    { "darkslateblue", 72, 61, 139, 255 },
    { "darkslategray", 47, 79, 79, 255 },
    { "darkslategrey", 47, 79, 79, 255 },
    { "darkturquoise", 0, 206, 209, 255 },
    { "darkviolet", 148, 0, 211, 255 },
    { "deeppink", 255, 20, 147, 255 },
    { "deepskyblue", 0, 191, 255, 255 },
    { "dimgray", 105, 105, 105, 255 },
    { "dimgrey", 105, 105, 105, 255 },
    { "dodgerblue", 30, 144, 255, 255 },
    { "firebrick", 178, 34, 34, 255 },
    { "floralwhite", 255, 250, 240, 255 },
    { "forestgreen", 34, 139, 34, 255 },
    { "fuchsia", 255, 0, 255, 255 },
    { "gainsboro", 220, 220, 220, 255 },
    { "ghostwhite", 248, 248, 255, 255 },
    { "gold", 255, 215, 0, 255 },
    { "goldenrod", 218, 165, 32, 255 },
    { "gray", 128, 128, 128, 255 },
    { "green", 0, 128, 0, 255 },
    { "greenyellow", 173, 255, 47, 255 },
    { "grey", 128, 128, 128, 255 },
    { "honeydew", 240, 255, 240, 255 },
    { "hotpink", 255, 105, 180, 255 },
    { "indianred", 205, 92, 92, 255 },
    { "indigo", 75, 0, 130, 255 },
    { "ivory", 255, 255, 240, 255 },
    { "khaki", 240, 230, 140, 255 },
    { "lavender", 230, 230, 250, 255 },
    { "lavenderblush", 255, 240, 245, 255 },
    { "lawngreen", 124, 252, 0, 255 },
    { "lemonchiffon", 255, 250, 205, 255 },
    { "lightblue", 173, 216, 230, 255 },
    { "lightcoral", 240, 128, 128, 255 },
    { "lightcyan", 224, 255, 255, 255 },
    { "lightgoldenrodyellow", 250, 250, 210, 255 },
    { "lightgray", 211, 211, 211, 255 },
    { "lightgreen", 144, 238, 144, 255 },
    { "lightgrey", 211, 211, 211, 255 },
    { "lightpink", 255, 182, 193, 255 },
    { "lightsalmon", 255, 160, 122, 255 },
    { "lightseagreen", 32, 178, 170, 255 },
    { "lightskyblue", 135, 206, 250, 255 },
    { "lightslategray", 119, 136, 153, 255 },
    { "lightslategrey", 119, 136, 153, 255 },
    { "lightsteelblue", 176, 196, 222, 255 },
    { "lightyellow", 255, 255, 224, 255 },
    { "lime", 0, 255, 0, 255 },
    { "limegreen", 50, 205, 50, 255 },
    { "linen", 250, 240, 230, 255 },
    { "magenta", 255, 0, 255, 255 },
    { "maroon", 128, 0, 0, 255 },
    { "mediumaquamarine", 102, 205, 170, 255 },
    { "mediumblue", 0, 0, 205, 255 },
    { "mediumorchid", 186, 85, 211, 255 },
    { "mediumpurple", 147, 112, 219, 255 },
    { "mediumseagreen", 60, 179, 113, 255 },
    { "mediumslateblue", 123, 104, 238, 255 },
    { "mediumspringgreen", 0, 250, 154, 255 },
    { "mediumturquoise", 72, 209, 204, 255 },
    { "mediumvioletred", 199, 21, 133, 255 },
    { "midnightblue", 25, 25, 112, 255 },
    { "mintcream", 245, 255, 250, 255 },
    { "mistyrose", 255, 228, 225, 255 },
    { "moccasin", 255, 228, 181, 255 },
    { "navajowhite", 255, 222, 173, 255 },
    { "navy", 0, 0, 128, 255 },
    { "oldlace", 253, 245, 230, 255 },
    { "olive", 128, 128, 0, 255 },
    { "olivedrab", 107, 142, 35, 255 },
    { "orange", 255, 165, 0, 255 },
    { "orangered", 255, 69, 0, 255 },
    { "orchid", 218, 112, 214, 255 },
    { "palegoldenrod", 238, 232, 170, 255 },
    { "palegreen", 152, 251, 152, 255 },
    { "paleturquoise", 175, 238, 238, 255 },
    { "palevioletred", 219, 112, 147, 255 },
    { "papayawhip", 255, 239, 213, 255 },
    { "peachpuff", 255, 218, 185, 255 },
    { "peru", 205, 133, 63, 255 },
    { "pink", 255, 192, 203, 255 },
    { "plum", 221, 160, 221, 255 },
    { "powderblue", 176, 224, 230, 255 },
    { "purple", 128, 0, 128, 255 },
    { "rebeccapurple", 102, 51, 153, 255 },
    { "red", 255, 0, 0, 255 },
    { "rosybrown", 188, 143, 143, 255 },
    { "royalblue", 65, 105, 225, 255 },
    { "saddlebrown", 139, 69, 19, 255 },
    { "salmon", 250, 128, 114, 255 },
    { "sandybrown", 244, 164, 96, 255 },
    { "seagreen", 46, 139, 87, 255 },
    { "seashell", 255, 245, 238, 255 },
    { "sienna", 160, 82, 45, 255 },
    { "silver", 192, 192, 192, 255 },
    { "skyblue", 135, 206, 235, 255 },
    { "slateblue", 106, 90, 205, 255 },
    { "slategray", 112, 128, 144, 255 },
    { "slategrey", 112, 128, 144, 255 },
    { "snow", 255, 250, 250, 255 },
    { "springgreen", 0, 255, 127, 255 },
    { "steelblue", 70, 130, 180, 255 },
    { "tan", 210, 180, 140, 255 },
    { "teal", 0, 128, 128, 255 },
    { "thistle", 216, 191, 216, 255 },
    { "tomato", 255, 99, 71, 255 },
    { "transparent", 0, 0, 0, 0 },
    { "turquoise", 64, 224, 208, 255 },
    { "violet", 238, 130, 238, 255 },
    { "wheat", 245, 222, 179, 255 },
    { "white", 255, 255, 255, 255 },
    { "whitesmoke", 245, 245, 245, 255 },
    { "yellow", 255, 255, 0, 255 },
    { "yellowgreen", 154, 205, 50, 255 },
};

/** @brief 具名色查找（大小写不敏感 + 忽略名字内部空白，对标
 *         qcolor.cpp get_named_rgb 的小写+去空白预处理）。 */
static bool xsss_lookupNamedColor(const char* p, size_t len, uint32_t* out)
{
    char buf[32];
    size_t w = 0;
    size_t i;
    for (i = 0; i < len; ++i) {
        char c = p[i];
        if (c == ' ' || c == '\t') continue;
        if (w + 1 >= sizeof(buf)) return false;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[w++] = c;
    }
    if (w == 0) return false;
    buf[w] = '\0';
    for (i = 0; i < sizeof(xsss_namedColors) / sizeof(xsss_namedColors[0]); ++i) {
        const uint32_t rgb =
            ((uint32_t)xsss_namedColors[i].r << 16) |
            ((uint32_t)xsss_namedColors[i].g << 8) |
            (uint32_t)xsss_namedColors[i].b;
        if (XStrcmp(xsss_namedColors[i].n, buf) == 0) {
            *out = ((uint32_t)xsss_namedColors[i].a << 24) | rgb;
            return true;
        }
    }
    return false;
}

/* ==================== 十六进制色 ==================== */

/** @brief # 十六进制色（对标 qcolor.cpp get_hex_rgb 的长度分支）：
 *         3=#RGB（位重复展开）、4=#RGBA（CSS Colors 4 新增，alpha
 *         收尾；Qt 6.8.3 无 4 位分支，按任务要求补）、6=#RRGGBB、
 *         8=#AARRGGBB（Qt len==8 口径 alpha 在前；CSS4 的 #RRGGBBAA
 *         与之同为 8 位冲突，保留 Qt/既有断言口径，其语义由 rgba()
 *         表达）、9=#RRRGGGBBB（12 位通道 → 高 8 位）、12=
 *         #RRRRGGGGBBBB（16 位通道 → 高 8 位）。输出 ARGB。 */
static bool xsss_parseHexColor(const char* p, size_t len, uint32_t* out)
{
    uint32_t r = 0, g = 0, b = 0, a = 0xFF;
    size_t i;
    if (len == 3 || len == 4) {
        for (i = 0; i < len; ++i) {
            int d = xsss_hexDigit(p[i]);
            if (d < 0) return false;
            d *= 17; /* 4 位展开 8 位。 */
            switch (i) {
            case 0: r = (uint32_t)d; break;
            case 1: g = (uint32_t)d; break;
            case 2: b = (uint32_t)d; break;
            default: a = (uint32_t)d; break; /* #RGBA：alpha 收尾。 */
            }
        }
    } else if (len == 6 || len == 8) {
        uint32_t ch[4] = { 0, 0, 0, 0 };
        for (i = 0; i < len; ++i) {
            int d = xsss_hexDigit(p[i]);
            if (d < 0) return false;
            ch[i / 2] = (ch[i / 2] << 4) | (uint32_t)d;
        }
        if (len == 6) {
            r = ch[0]; g = ch[1]; b = ch[2];
        } else {
            a = ch[0]; r = ch[1]; g = ch[2]; b = ch[3]; /* AARRGGBB。 */
        }
    } else if (len == 9 || len == 12) {
        int per = (len == 9) ? 3 : 4;
        uint32_t ch[3] = { 0, 0, 0 };
        int c;
        for (c = 0; c < 3; ++c) {
            for (i = 0; i < (size_t)per; ++i) {
                int d = xsss_hexDigit(p[c * per + i]);
                if (d < 0) return false;
                ch[c] = (ch[c] << 4) | (uint32_t)d;
            }
            ch[c] >>= (per == 3) ? 4 : 8; /* 12 位通道 >>4、16 位通道 >>8
                                              ——均取高 8 位（Qt len==12 走
                                              16 位 qRgba64 再折 8 位）。 */
        }
        r = ch[0]; g = ch[1]; b = ch[2];
    } else {
        return false;
    }
    *out = (a << 24) | (r << 16) | (g << 8) | b;
    return true;
}

/* ==================== 函数式色 ==================== */

/** @brief 色彩函数分量 → 0..maxRange 整数（% 按量程折算；对标
 *         qcssparser parseColorValue 的 Percentage 折算 + QVariant
 *         toInt 截断口径）。 */
static int xsss_compChannel(double v, bool pct, int maxRange)
{
    double d = pct ? v * (double)maxRange / 100.0 : v;
    if (d <= 0.0) return 0;
    if (d >= (double)maxRange) return maxRange;
    return (int)d; /* Qt toInt 截断（非舍入），保持一致。 */
}

/** @brief alpha 分量 → 0..255（Qt parseColorValue 口径：% ×255/100；
 *         裸数 ≤1 视作 0..1 浮点 ×255，>1 按 0..255 值；截断夹取）。 */
static int xsss_compAlpha(double v, bool pct)
{
    double d;
    if (pct)
        d = v * 255.0 / 100.0;
    else
        d = (v <= 1.0) ? v * 255.0 : v;
    if (d <= 0.0) return 0;
    if (d >= 255.0) return 255;
    return (int)d;
}

/** @brief HSL → RGB（对标 qcolor.cpp toRgb 的 HSL 分支：色相归一
 *         [0,1)、temp2 = l<0.5 ? l*(1+s) : l+s-l*s、temp1 = 2l-temp2、
 *         三相位各走六段折线）。 */
static double xsss_hslValue(double v1, double v2, double vH)
{
    if (vH < 0.0) vH += 1.0;
    else if (vH > 1.0) vH -= 1.0;
    if (6.0 * vH < 1.0) return v1 + (v2 - v1) * 6.0 * vH;
    if (2.0 * vH < 1.0) return v2;
    if (3.0 * vH < 2.0) return v1 + (v2 - v1) * (2.0 / 3.0 - vH) * 6.0;
    return v1;
}

static void xsss_hslToRgb(double h01, double s, double l,
                          double* r, double* g, double* b)
{
    double temp1, temp2, tr, tg, tb;
    if (s <= 0.0) { /* 无饱和（含 l==0 全黑 / l==1 全白）。 */
        *r = *g = *b = l;
        return;
    }
    if (l < 0.5)
        temp2 = l * (1.0 + s);
    else
        temp2 = l + s - l * s;
    temp1 = 2.0 * l - temp2;
    tr = h01 + 1.0 / 3.0;
    tg = h01;
    tb = h01 - 1.0 / 3.0;
    /* 参数序：Hue_2_RGB(v1=temp1 暗值, v2=temp2 亮值, vH=相位)。 */
    *r = xsss_hslValue(temp1, temp2, tr);
    *g = xsss_hslValue(temp1, temp2, tg);
    *b = xsss_hslValue(temp1, temp2, tb);
}

/** @brief HSV → RGB（对标 qcolor.cpp toRgb 的 HSV 分支六段折线：
 *         i=色相六分、i&1 ? q=v*(1-s*f) : q=v*(1-s*(1-f))）。 */
static void xsss_hsvToRgb(double hDeg, double s, double v,
                          double* r, double* g, double* b)
{
    int i;
    double f, p, q, t;
    if (s <= 0.0) {
        *r = *g = *b = v;
        return;
    }
    hDeg /= 60.0;
    i = (int)hDeg;
    f = hDeg - (double)i;
    p = v * (1.0 - s);
    if (i & 1)
        q = v * (1.0 - (s * f));
    else
        q = v * (1.0 - (s * (1.0 - f)));
    t = v * (1.0 - (s * (1.0 - f)));
    switch (i) {
    case 0: *r = v; *g = t; *b = p; break;
    case 1: *r = q; *g = v; *b = p; break;
    case 2: *r = p; *g = v; *b = t; break;
    case 3: *r = p; *g = q; *b = v; break;
    case 4: *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

/** @brief rgb/rgba/hsl/hsla/hsv/hsva 函数色解析（对标 qcssparser
 *         parseColorValue）：
 *         - 函数名 3/4 字符：rgb/hsv/hsl 前缀，恰 4 字符且第 4 字符
 *           'a' 即带 alpha（rgba/hsla/hsva）；
 *         - 逗号分隔分量（rgb/hsl/hsv 必 3 个、*a 必 4 个，数量不符
 *           整体失败——Qt tokenCount 校验同口径）；
 *         - 百分比量程 maxRange = (rgb || 分量非首) ? 255 : 359
 *           （色相百分比 ×359/100，Qt 原文口径）；
 *         - 裸数分量按 Qt QVariant::toInt 截断后夹取（rgb 0..255、
 *           色相 0..359、s/l/v 0..255）；
 *         - 换算后通道 qRound 到 8 位（Qt 走 16 位存储再 >>8，数学
 *           等价；其 16 位下的"值==1 归 0"舍入补偿在 8 位下无对应
 *           物，不实现）。 */
static bool xsss_parseFunctionColor(const char* p, size_t len, uint32_t* out)
{
    char name[8];
    char tok[32];
    size_t nl = 0;
    bool isRgb, isHsv, isHsl, hasAlpha;
    const char* open = p;
    const char* end;
    const char* q;
    const char* comp[4];
    size_t clen[4];
    double v[4];
    bool pct[4];
    int n = 0;
    int i;
    int c0, c1, c2, a = 255;
    double r, g, b;
    while (open < p + len && *open != '(') ++open;
    if (open >= p + len || p[len - 1] != ')') return false;
    nl = (size_t)(open - p);
    if (nl != 3 && nl != 4) return false;
    for (i = 0; i < (int)nl; ++i) {
        char c = p[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        name[i] = c;
    }
    name[nl] = '\0';
    isRgb = name[0] == 'r' && name[1] == 'g' && name[2] == 'b';
    isHsv = name[0] == 'h' && name[1] == 's' && name[2] == 'v';
    isHsl = name[0] == 'h' && name[1] == 's' && name[2] == 'l';
    if (!isRgb && !isHsv && !isHsl) return false;
    hasAlpha = (nl == 4 && name[3] == 'a');
    if (nl == 4 && !hasAlpha) return false; /* 4 字符仅允许 *a 变体。 */
    end = p + len - 1; /* 指向 ')'。 */
    q = open + 1;
    while (q < end && n < 4) {
        const char* tokStart = q;
        while (q < end && *q != ',') ++q;
        comp[n] = tokStart;
        clen[n] = (size_t)(q - tokStart);
        ++n;
        if (q < end && *q == ',') ++q;
    }
    if (q < end) return false; /* 分量多于 4。 */
    if (n != (hasAlpha ? 4 : 3)) return false;
    for (i = 0; i < n; ++i) {
        const char* s = comp[i];
        const char* e = comp[i] + clen[i];
        const char* tp;
        size_t tl = 0;
        while (s < e && XIsSpace((unsigned char)*s)) ++s;
        while (e > s && XIsSpace((unsigned char)e[-1])) --e;
        if (s < e && e[-1] == '%') {
            --e;
            pct[i] = true;
        } else {
            pct[i] = false;
        }
        while (s < e && XIsSpace((unsigned char)*s)) ++s;
        while (e > s && XIsSpace((unsigned char)e[-1])) --e;
        tl = (size_t)(e - s);
        if (tl == 0 || tl >= sizeof(tok)) return false;
        XMemcpy(tok, s, tl);
        tok[tl] = '\0';
        tp = tok;
        if (!xsss_parseDouble(&tp, &v[i]) || *tp != '\0') return false;
    }
    if (isRgb) {
        c0 = xsss_compChannel(v[0], pct[0], 255);
        c1 = xsss_compChannel(v[1], pct[1], 255);
        c2 = xsss_compChannel(v[2], pct[2], 255);
    } else {
        c0 = xsss_compChannel(v[0], pct[0], 359); /* 色相（Qt 量程 359）。 */
        c1 = xsss_compChannel(v[1], pct[1], 255);
        c2 = xsss_compChannel(v[2], pct[2], 255);
    }
    if (hasAlpha) a = xsss_compAlpha(v[3], pct[3]);
    if (isRgb) {
        /* 已是 0..255 整数，归一到 0..1 分数走统一 qRound 合成。 */
        r = (double)c0 / 255.0;
        g = (double)c1 / 255.0;
        b = (double)c2 / 255.0;
    } else {
        r = g = b = 0.0;
        if (isHsl)
            xsss_hslToRgb((double)c0 / 360.0, (double)c1 / 255.0,
                          (double)c2 / 255.0, &r, &g, &b);
        else
            xsss_hsvToRgb((double)c0, (double)c1 / 255.0,
                          (double)c2 / 255.0, &r, &g, &b);
    }
    {
        int ir = xsss_roundHalfAway(r * 255.0);
        int ig = xsss_roundHalfAway(g * 255.0);
        int ib = xsss_roundHalfAway(b * 255.0);
        if (ir < 0) ir = 0;
        if (ir > 255) ir = 255;
        if (ig < 0) ig = 0;
        if (ig > 255) ig = 255;
        if (ib < 0) ib = 0;
        if (ib > 255) ib = 255;
        *out = ((uint32_t)a << 24) | ((uint32_t)ir << 16) |
               ((uint32_t)ig << 8) | (uint32_t)ib;
    }
    return true;
}

/**
 * @brief 解析颜色（具名色 SVG 全表 / # 十六进制族 / rgb·hsl·hsv 函数
 *        族；详见头文件口径注释）。
 */
bool XCssParseColor(const char* value, uint32_t* out)
{
    const char* p;
    size_t len;
    if (!value || !out) return false;
    while (*value && XIsSpace((unsigned char)*value)) ++value;
    p = value;
    len = XStrlen(p);
    while (len > 0 && XIsSpace((unsigned char)p[len - 1])) --len;
    if (len > 0 && p[0] == '#') {
        ++p;
        --len;
        return xsss_parseHexColor(p, len, out);
    }
    {
        /* 函数式色：名字(参数) 形态（名字 3/4 字符、以 ')' 收尾）。
         * 直接进函数解析器（经 XCssParseColorToken 回环会无限递归）。 */
        size_t nl = 0;
        while (nl < len && p[nl] != '(') ++nl;
        if (nl >= 3 && nl <= 4 && nl < len && p[len - 1] == ')')
            return xsss_parseFunctionColor(p, len, out);
    }
    /* 具名色（含 transparent → 0x00000000，对标 Value_Transparent）。 */
    return xsss_lookupNamedColor(p, len, out);
}

/** @brief 限长颜色 token 解析（供 border 简写拆分；非 NUL 结尾）。 */
bool XCssParseColorToken(const char* value, size_t len, uint32_t* out)
{
    char buf[32];
    if (!value || len == 0 || len >= sizeof(buf)) return false;
    XMemcpy(buf, value, len);
    buf[len] = '\0';
    return XCssParseColor(buf, out);
}

/* ==================== 声明值读取（防御性剥注释） ==================== */

/** @brief 声明值防御性读取：拷贝进缓冲并剥块注释（斜杠星开、星斜杠
 *         收，未闭合剥到串尾），随后去首尾空白。
 *
 *  任务批一 #7 双保险：块注释本应由解析器滤除，消费端再剥一次，防
 *  解析器线实现差异或历史样式表漏网（如 "red" 与分号之间夹着未滤净
 *  的注释体，旧值带注释尾巴致颜色解析失败）。缓冲不足（值 > cap-1
 *  字节）返回 NULL，按无值处理——渲染回落底层样式，不给半截值。 */
static const char* xsss_declText(const XCssDeclaration* d, char* buf, size_t cap)
{
    const char* src;
    size_t w = 0;
    bool inComment = false;
    if (!d || !d->m_value || !buf || cap < 4) return NULL;
    src = XString_toUtf8(d->m_value);
    if (!src) return NULL;
    while (*src && w + 1 < cap) {
        if (!inComment && src[0] == '/' && src[1] == '*') {
            inComment = true;
            src += 2;
            continue;
        }
        if (inComment && src[0] == '*' && src[1] == '/') {
            inComment = false;
            src += 2;
            continue;
        }
        if (!inComment) buf[w++] = *src;
        ++src;
    }
    buf[w] = '\0';
    /* 去首尾空白。 */
    {
        char* b = buf;
        char* e = buf + w;
        while (*b && XIsSpace((unsigned char)*b)) ++b;
        while (e > b && XIsSpace((unsigned char)e[-1])) --e;
        *e = '\0';
        if (b != buf) XMemmove(buf, b, (size_t)(e - b) + 1);
    }
    return buf;
}

/* ==================== 长度消费端换算 ==================== */

/** @brief em/ex/% 参考不可得时的字体高回落基准（px）。
 *
 *  12pt @96dpi = 16px（对标 Qt 默认字号 12pt 的像素量级）；仅旧
 *  兼容签名 XCssParseLength 使用。 */
#define XSSS_FALLBACK_FONT_PX 16

/**
 * @brief 长度换算上下文（百分比 / em / ex 的参考系）。
 *
 *  消费端口径（任务批一 #1）：% 有矩形按矩形（横向类长度参考宽、
 *  纵向类参考高）；em/ex 按 painter 当前字体高（ex 取其半——Qt
 *  6.8.3 lengthValueFromData 用 QFontMetrics xHeight/height，XFont
 *  无 x 高查询，按高/2 近似）；参考不可得按 0 并注释。
 */
typedef struct XCssLengthContext
{
    int refWidth;      /**< 百分比横向参考（像素；0=不可得）。 */
    int refHeight;     /**< 百分比纵向参考（像素；0=不可得）。 */
    int fontPixelSize; /**< em/ex 参考字体高（像素；0=不可得）。 */
} XCssLengthContext;

/** @brief 长度 → 像素（None/参考不可得按 0；horizontal 选百分比
 *         参考轴：横向类长度参考宽、纵向类参考高）。 */
static int xsss_lengthPx(const XCssLength* len, const XCssLengthContext* ctx,
                         bool horizontal)
{
    double v = len->m_value;
    switch (len->m_unit) {
    case XCssLengthUnit_Px:
        return xsss_roundHalfAway(v);
    case XCssLengthUnit_Pt:
        return xsss_roundHalfAway(xsss_ptToPx(v));
    case XCssLengthUnit_Em:
        return (ctx && ctx->fontPixelSize > 0)
                   ? xsss_roundHalfAway(v * ctx->fontPixelSize)
                   : 0; /* 无字体上下文按 0（口径见结构注释）。 */
    case XCssLengthUnit_Ex:
        /* ex = x 高（Qt QFontMetrics::xHeight）；无查询按字体高/2。 */
        return (ctx && ctx->fontPixelSize > 0)
                   ? xsss_roundHalfAway(v * ctx->fontPixelSize / 2.0)
                   : 0;
    case XCssLengthUnit_Percent: {
        int ref = ctx ? (horizontal ? ctx->refWidth : ctx->refHeight) : 0;
        return ref > 0 ? xsss_roundHalfAway(v * ref / 100.0) : 0;
    }
    default:
        return 0;
    }
}

/** @brief 上下文构造：画笔字体 + 参考矩形（任一不可得传 NULL/0）。 */
static XCssLengthContext xsss_lengthContext(XPainter* painter,
                                            const XRect* rect)
{
    XCssLengthContext ctx;
    ctx.refWidth = rect ? rect->width : 0;
    ctx.refHeight = rect ? rect->height : 0;
    ctx.fontPixelSize = 0;
    if (painter) {
        const XFont* f = XPainter_font(painter);
        if (f) {
            ctx.fontPixelSize = XFont_pixelSize(f);
            if (ctx.fontPixelSize <= 0)
                ctx.fontPixelSize =
                    XFont_bitmapPixelSize(f, XSSS_FALLBACK_FONT_PX);
        }
    }
    return ctx;
}

/* ==================== 值/声明查询 ==================== */

/** @brief 在规则内查声明值（规则内同属性多声明：后声明胜）。
 *
 *  对标 Qt 6.8.3 两处口径：级联权重 = rule.order +
 *  specificity*0x100 + (origin+depth)*0x100000（StyleSelector::
 *  matchRule，qcssparser.cpp）——无 importance 轨道，!important 仅在
 *  解析期落 Declaration::important 标志、匹配期完全不消费；规则内
 *  值提取（ValueExtractor::extractBox/extractFont 等）按声明序扫描、
 *  同属性后者覆盖。根修：旧实现 `(d->m_important == found->m_important)`
 *  恒真且带 importance 分支，与上述两处口径均不符。 */
static const XCssDeclaration* xsss_findDecl(const XCssStyleRule* rule,
                                            XCssProperty id)
{
    int i;
    for (i = rule->m_declarationCount - 1; i >= 0; --i) {
        const XCssDeclaration* d = &rule->m_declarations[i];
        if (d->m_propertyId == id) return d;
    }
    return NULL;
}

/** @brief 对象类名（vtable 名；关闭名称配置时返回 NULL）。 */
static const char* xsss_className(const XObject* obj)
{
    XVtable* vt;
    if (!obj) return NULL;
    vt = XClassGetVtable((const XClass*)obj);
    return XVTABLE_GET_NAME(vt);
}

/** @brief 对象 objectName（UTF-8；无则 NULL）。 */
static const char* xsss_objectName(const XObject* obj)
{
    const XString* n;
    if (!obj) return NULL;
    n = XObject_objectName(obj);
    return n ? XString_toUtf8(n) : NULL;
}

/** @brief 由 XStyleState 位映射伪类位（用于规则匹配）。
 *
 *  逐位对标 Qt 6.8.3 qstylesheetstyle.cpp 的 pseudoClass(QStyle::State)：
 *  - Hover 在 Enabled 分支内派生（禁用控件不发 :hover），且 MouseOver
 *    与 Sunken 并置——按下时 :hover 与 :pressed 同时命中（Qt 语义；
 *    旧实现的「按下压制 Hover」系 night #5 历史偏离，本批按任务裁定
 *    恢复 Qt 口径，随之删除 qfusionstyle isDown 优先的旧注口径）；
 *  - On 态并入 Checked 位（Qt 的 PseudoClass_On 无名表入口，样式裁决
 *    只看 Checked）；Off → Unchecked（Qt 名表 "off"/"unchecked" 同位）；
 *  - Open/Closed：Qt 对 (Open|On|Sunken) 之外无条件发 Closed（显式负态）；
 *  - Horizontal/Vertical：Qt 对 !Horizontal 无条件发 Vertical（缺省垂直）；
 *  - 不映射位（两边一致或无 State 来源）：Raised、Down/UpArrow、
 *    FocusAtBorder、AutoRaise、Editing、KeyboardFocusChange（Qt
 *    pseudoClass 同样不产生）；Top/Bottom（Qt 由各绘制点手工附加，
 *    非 state 派生）；State_HasEditFocus（QT_KEYPAD_NAVIGATION 专属，
 *    本库无此位）。
 */
static uint32_t xsss_statePseudos(uint32_t state)
{
    uint32_t ps = 0;
    if (state & XStyleState_Enabled) {
        ps |= XCssPseudo_Enabled;
        if (state & XStyleState_MouseOver) ps |= XCssPseudo_Hover;
    } else {
        ps |= XCssPseudo_Disabled;
    }
    if (state & XStyleState_Active) ps |= XCssPseudo_Active;
    if (state & XStyleState_Window) ps |= XCssPseudo_Window;
    if (state & XStyleState_Sunken) ps |= XCssPseudo_Pressed;
    if (state & XStyleState_HasFocus) ps |= XCssPseudo_Focus;
    if (state & XStyleState_On) ps |= XCssPseudo_Checked;
    if (state & XStyleState_Off) ps |= XCssPseudo_Unchecked;
    if (state & XStyleState_NoChange) ps |= XCssPseudo_Indeterminate;
    if (state & XStyleState_Selected) ps |= XCssPseudo_Selected;
    if (state & (XStyleState_Open | XStyleState_On | XStyleState_Sunken))
        ps |= XCssPseudo_Open;
    else
        ps |= XCssPseudo_Closed;
    if (state & XStyleState_Children) ps |= XCssPseudo_Children;
    if (state & XStyleState_Sibling) ps |= XCssPseudo_Sibling;
    if (state & XStyleState_Item) ps |= XCssPseudo_Item;
    if (state & XStyleState_Horizontal) ps |= XCssPseudo_Horizontal;
    else ps |= XCssPseudo_Vertical;
    if (state & XStyleState_ReadOnly) ps |= XCssPseudo_ReadOnly;
    return ps;
}

/** @brief 关系链前段伪类通配位（全 1：`(伪类集 & ANY) == 伪类集` 恒真，
 *         即伪类约束被忽略）。
 *
 *  口径声明（R-90，对标 Qt 6.8 qcssparser：Selector::pseudoClass 只读
 *  basicSelectors.last()，祖先/父段伪类一律不参与匹配、规则照常应用）：
 *  此前前段以 statePseudos(0) 求值——state=0 映射出 Disabled 位，致
 *  :enabled/:hover/:focus 永不命中、:disabled 恒命中，整条关系规则
 *  失效/误应用，偏差方向恰与 Qt 相反。现按 Qt 口径忽略前段伪类。
 */
#define XSSS_PSEUDOS_ANY 0xFFFFFFFFu

/** @brief 单个属性选择器匹配（按 ValueMatchType 分派；[name] 存在性
 *         要求对全部准则生效）。
 *
 *  属性值来源：XObject 动态属性（XObject_property），对标 QSS 的
 *  QObject::property 匹配。
 */
static bool xsss_attrMatches(const XCssAttributeSelector* attr,
                             const XObject* obj)
{
    XString* name;
    XVariant* v;
    const char* want;
    if (!attr || !attr->m_name) return true; /* 无属性限定。 */
    if (!obj) return false;
    name = attr->m_name;
    v = XObject_property(obj, name);
    if (!v) return false; /* [name] 要求属性存在。 */
    want = XString_toUtf8(attr->m_value);
    {
        const char* actual = (const char*)XVariant_data(v);
        if (!actual) return false;
        if (!want) return false;
        /* 按 ValueMatchType 分派（对标 QCss::StyleSelector 匹配）。 */
        switch (attr->m_match) {
        case XCssValueMatch_NoMatch:
            return true;
        case XCssValueMatch_Equal:
            return XStrcmp(actual, want) == 0;
        case XCssValueMatch_Includes: {
            /* 空格分词包含。 */
            const char* q = actual;
            size_t wl = XStrlen(want);
            while (*q) {
                const char* tok;
                size_t tl;
                while (*q == ' ') ++q;
                tok = q;
                while (*q && *q != ' ') ++q;
                tl = (size_t)(q - tok);
                if (tl == wl && XStrncmp(tok, want, wl) == 0) return true;
            }
            return false;
        }
        case XCssValueMatch_DashMatch: {
            size_t wl = XStrlen(want);
            if (XStrcmp(actual, want) == 0) return true;
            return XStrncmp(actual, want, wl) == 0 && actual[wl] == '-';
        }
        case XCssValueMatch_BeginsWith:
            return XStrncmp(actual, want, XStrlen(want)) == 0;
        case XCssValueMatch_EndsWith: {
            size_t al = XStrlen(actual);
            size_t wl = XStrlen(want);
            return al >= wl && XStrcmp(actual + al - wl, want) == 0;
        }
        case XCssValueMatch_Contains:
            return XStrstr(actual, want) != NULL;
        default:
            return false;
        }
    }
}

/** @brief ID 列表匹配（对标 basicSelectorMatches 的
 *         `sel.ids != nodeIds(node)` 列表等值判定）。
 *
 *  Qt 口径：选择器 ID 列表与节点的 id 列表整体相等才算命中；QSS 的
 *  nodeIds 恒为 [objectName]（空 objectName → 空列表，
 *  qstylesheetstyle.cpp QStyleSheetStyleSelector::nodeIds）。故
 *  `#a` 命中 objectName==a；`#a#b` 多 ID 要求节点 id 列表逐位相等，
 *  单 objectName 恒不满足（Qt QSS 亦如此——多 ID 选择器永不命中，
 *  解析端仍按 Qt 全量存储，此处如实消费）。
 */
static bool xsss_idsMatch(const XCssBasicSelector* sel, const XObject* obj)
{
    const char* id;
    int wantCount;
    if (!sel->m_id) return true; /* 选择器无 ID 限定。 */
    id = xsss_objectName(obj);
    if (!id || id[0] == '\0') return false; /* 节点 id 列表空 ≠ 非空选择器列表。 */
    wantCount = 1 + sel->m_idCount; /* 选择器侧 ID 总数（首 ID + 尾部多 ID）。 */
    if (wantCount != 1) return false; /* 节点侧列表长恒 1：多 ID 恒不等值。 */
    {
        const char* want = XString_toUtf8(sel->m_id);
        return want && XStrcmp(id, want) == 0;
    }
}

/** @brief 属性选择器全列匹配（首属性 + 尾部多属性须全部命中，对标
 *         basicSelectorMatches 逐项校验、任一失败即整段失败）。 */
static bool xsss_attrsMatch(const XCssBasicSelector* sel, const XObject* obj)
{
    int i;
    if (!xsss_attrMatches(&sel->m_attribute, obj)) return false;
    for (i = 0; i < sel->m_attributeCount; ++i)
        if (!xsss_attrMatches(&sel->m_attributes[i], obj)) return false;
    return true;
}

/** @brief 末段伪元素判定（对标 Selector::pseudoElement() 只读末段：
 *         want==NULL 要求末段无伪元素（普通属性查询路径，Qt
 *         declarationsForNode 跳过带伪元素的规则）；want 非空要求末段
 *         伪元素与 want 等值（大小写不敏感；QSS 子控件名约定全小写）。
 *         前段的伪元素与伪类一样不参与匹配（XSSS_PSEUDOS_ANY 同口径）。 */
static bool xsss_pseudoElementMatches(const XCssBasicSelector* last,
                                      const char* want)
{
    const char* have;
    if (!last->m_pseudoElement) return want == NULL;
    if (!want) return false;
    have = XString_toUtf8(last->m_pseudoElement);
    return have && XStrcasecmp(have, want) == 0;
}

/** @brief 单段基础选择器匹配（元素名=类名、#id 列表、伪类、属性全列）。 */
static bool xsss_basicMatches(const XCssBasicSelector* sel,
                              const XObject* obj, uint32_t statePseudos)
{
    const char* cls;
    if (!sel) return false;
    if (sel->m_elementName) {
        const char* want = XString_toUtf8(sel->m_elementName);
        cls = xsss_className(obj);
        if (!cls || !want || XStrcasecmp(cls, want) != 0) return false;
    }
    if (!xsss_idsMatch(sel, obj)) return false;
    if (sel->m_pseudoClasses &&
        (sel->m_pseudoClasses & statePseudos) != sel->m_pseudoClasses)
        return false;
    return xsss_attrsMatch(sel, obj);
}

/** @brief 选择器链匹配（逐字对标 qcssparser.cpp
 *         StyleSelector::selectorMatches 的右→左 do-while 走查）。
 *
 *  末段在 obj 上匹配（伪类按 obj 状态求值；其余段伪类一律忽略，
 *  XSSS_PSEUDOS_ANY，对标「伪类仅末段生效」）。段间导航按本库存储的
 *  m_relationToPrev（存于后段，等价 Qt 存于前段的 relationToNext）：
 *  - Ancestor/IndirectAdjacent 段失败可重试（前走父链/兄链直到命中或
 *    耗尽），其余关系（Parent/DirectAdjacent）失败立即整链失败；
 *  - Parent/Ancestor 经 XObject_parent 前走；'+'/'~' 经
 *    XObject_previousSibling 前走（Qt 的 previousSiblingNode 为恒空桩、
 *    '+'/'~' 永不命中，本库按兄弟导航线 API 提供真实实现——行为超出
 *    而非偏离 Qt，对齐 QCss 遍历意图）。
 *  pseudoElementWant 见 xsss_pseudoElementMatches（只约束末段）。 */
static bool xsss_selectorMatches(const XCssSelector* sel, const XObject* obj,
                                 uint32_t statePseudos,
                                 const char* pseudoElementWant)
{
    const XObject* cur;
    int i;
    bool match;
    if (!sel || sel->m_basicCount <= 0) return false;
    if (sel->m_basicCount == 1) {
        /* 单段链：relationToPrev==None，无导航。 */
        return xsss_basicMatches(&sel->m_basics[0], obj, statePseudos) &&
               xsss_pseudoElementMatches(&sel->m_basics[0],
                                         pseudoElementWant);
    }
    i = sel->m_basicCount - 1;
    cur = obj;
    match = true;
    for (;;) {
        /* Qt：末段必须命中；其余段在 Ancestor/IndirectAdjacent 关系下
         * 失败可沿导航继续重试（段 i 与 i+1 的关系存于本库后段的
         * m_relationToPrev）。 */
        match = xsss_basicMatches(&sel->m_basics[i], cur,
                                  i == sel->m_basicCount - 1 ? statePseudos
                                                             : XSSS_PSEUDOS_ANY);
        if (match && i == sel->m_basicCount - 1 &&
            !xsss_pseudoElementMatches(&sel->m_basics[i],
                                       pseudoElementWant))
            match = false; /* 末段伪元素不符按未命中处理（可重试仅限
                              Ancestor/IndirectAdjacent 语义不适用——伪元素
                              只看末段形态本身，直接失败）。 */
        if (!match) {
            if (i == sel->m_basicCount - 1) break;
            if (sel->m_basics[i + 1].m_relationToPrev !=
                    XCssRelation_Ancestor &&
                sel->m_basics[i + 1].m_relationToPrev !=
                    XCssRelation_IndirectAdjacent)
                break;
        }
        if (match ||
            (sel->m_basics[i + 1].m_relationToPrev != XCssRelation_Ancestor &&
             sel->m_basics[i + 1].m_relationToPrev !=
                 XCssRelation_IndirectAdjacent))
            --i;
        if (i < 0) break;
        /* 段 i 与段 i+1 的关系驱动导航（对标 sel.relationToNext 分派）。 */
        if (sel->m_basics[i + 1].m_relationToPrev == XCssRelation_Ancestor ||
            sel->m_basics[i + 1].m_relationToPrev == XCssRelation_Parent) {
            cur = (const XObject*)XObject_parent((XObject*)cur);
        } else if (sel->m_basics[i + 1].m_relationToPrev ==
                       XCssRelation_DirectAdjacent ||
                   sel->m_basics[i + 1].m_relationToPrev ==
                       XCssRelation_IndirectAdjacent) {
            cur = XObject_previousSibling(cur);
        }
        /* None：Qt 原式两分支皆不命中、节点原地不动（解析器保证段间恒有
         * 关系，此分支防御性保持 Qt 同构的「不导航」而非回退父链）。 */
        if (!cur) {
            match = false;
            break;
        }
        /* Qt 循环条件：i>=0 且（命中 或 关系为可重试的 Ancestor/
         * IndirectAdjacent）。 */
        if (!(match ||
              sel->m_basics[i + 1].m_relationToPrev == XCssRelation_Ancestor ||
              sel->m_basics[i + 1].m_relationToPrev ==
                  XCssRelation_IndirectAdjacent))
            break;
    }
    return match;
}

/* ==================== 级联多源收集与解析缓存 ==================== */

/** @brief 控件级样式表源解析缓存：按源文本内容等值查找，未命中则解析
 *         落表（环形替换淘汰）。
 *
 *  缓存与失效时机（口径总注）：
 *  - 键=源文本深拷贝的内容等值（指针相同走快路径；控件 setStyleSheet
 *    恒产生新 XString，且深拷贝比对杜绝「旧串释放→新串复用同地址」的
 *    误命中）；
 *  - 解析失败按 Qt 口径整表弃用（缓存空表，避免逐帧反复重解析坏串）；
 *  - 淘汰=环形替换（容量 XSSS_SOURCE_CACHE_SIZE ≥ 单次收集触及的
 *    槽位上限 1+XSSS_MAX_CASCADE_SOURCES，保证单次收集走查内不会
 *    替换掉本次已收集条目——收集到的规则指针在走查期间恒有效）；
 *  - 替换在用条目前先失效渲染规则缓存（m_cacheRule/m_cacheSel 指进
 *    被替换条目的规则数组，悬空指针必须先行切断）；
 *  - XStyleSheetStyle_setStyleSheet 只失效渲染缓存、不清源缓存（应用
 *    级表变化与控件源互不牵连）；本对象析构经 xsss_sourceCacheClear
 *    全量释放。 */
static const XCssStyleSheet* xsss_sourceSheet(XStyleSheetStyle* self,
                                              const XString* text)
{
    int i;
    XStyleSheetStyleSource* e;
    int slot;
    if (!self || !text) return NULL;
    for (i = 0; i < self->m_sourceCapacity; ++i) {
        e = &self->m_sources[i];
        if (e->m_inUse &&
            (e->m_text == text ||
             (e->m_text && text &&
              XString_equals(e->m_text, text, XChar_CaseSensitive))))
            return &e->m_sheet;
    }
    if (self->m_sourceCapacity == 0) {
        int cap = XSSS_SOURCE_CACHE_SIZE;
        XStyleSheetStyleSource* grown =
            (XStyleSheetStyleSource*)XMalloc_System(
                sizeof(XStyleSheetStyleSource) * (size_t)cap);
        if (!grown) return NULL;
        for (i = 0; i < cap; ++i) {
            grown[i].m_text = NULL;
            XCssStyleSheet_init(&grown[i].m_sheet);
            grown[i].m_inUse = false;
        }
        self->m_sources = grown;
        self->m_sourceCapacity = cap;
        self->m_sourceCount = 0;
        self->m_sourceCursor = 0;
    }
    slot = self->m_sourceCursor % self->m_sourceCapacity;
    ++self->m_sourceCursor;
    e = &self->m_sources[slot];
    {
        bool wasInUse = e->m_inUse;
        if (wasInUse) {
            self->m_cacheValid = false; /* 渲染缓存可能指进被替换条目。 */
            XClassDelete((XClass*)e->m_text);
            XCssStyleSheet_clear(&e->m_sheet);
        } else {
            ++self->m_sourceCount;
        }
        e->m_text = XString_create_with_length_utf8(
            XString_toUtf8(text), XString_toUtf8_length(text));
        if (e->m_text) {
            e->m_inUse = true;
            (void)XCssStyleSheet_parse(&e->m_sheet,
                                       XString_toUtf8(e->m_text));
        } else {
            /* 拷贝失败（OOM）：不留半条目，回落无源。 */
            e->m_inUse = false;
            if (!wasInUse) --self->m_sourceCount;
        }
    }
    return e->m_inUse ? &e->m_sheet : NULL;
}

/** @brief 释放控件源解析缓存全量条目（析构路径）。 */
static void xsss_sourceCacheClear(XStyleSheetStyle* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_sourceCapacity; ++i) {
        XStyleSheetStyleSource* e = &self->m_sources[i];
        if (e->m_inUse) {
            XClassDelete((XClass*)e->m_text);
            XCssStyleSheet_clear(&e->m_sheet);
            e->m_inUse = false;
        }
        e->m_text = NULL;
    }
    if (self->m_sources) XFree_System(self->m_sources);
    self->m_sources = NULL;
    self->m_sourceCount = 0;
    self->m_sourceCapacity = 0;
    self->m_sourceCursor = 0;
}

/** @brief 收集级联源（应用级 + 控件链，控件链近→远），对标
 *         QStyleSheetStyle::styleRules 的 appSs/objectSs 装配。
 *
 *  - 应用级：self->m_sheet（经 XStyle_installStyleSheet 装在全局默认
 *    样式对象里时即全局表），origin=Inline、depth=1（Qt appSs 同值）；
 *  - 控件级：控件自身 + 沿 XObject_parent 的祖先（Qt objectSs 按
 *    控件→根收集、空表跳过），depth = size - i + 2（Qt 原式，近控件
 *    深度大者恒胜；远端祖先为弱源）；Qt 逐 QObject 查 styleSheet
 *    动态属性，本库文本挂在 XWidget 上，以 isWidgetType 守卫取
 *    XWidget_styleSheet（非控件祖先无源 ≈ Qt 空表跳过）；
 *  - 超 XSSS_MAX_CASCADE_SOURCES 截断远端（深度位阶下只丢弱源）。
 *
 *  @return 源数；sheets[i]/depths[i] 为源规则表与其 depth。 */
static int xsss_collectSources(XStyleSheetStyle* self, const XObject* obj,
                               const XCssStyleSheet* sheets[], int depths[])
{
    int count = 0;
    if (self->m_sheet.m_ruleCount > 0) {
        sheets[count] = &self->m_sheet;
        depths[count] = 1;
        ++count;
    }
    if (obj) {
        const XCssStyleSheet* chain[XSSS_MAX_CASCADE_SOURCES];
        int chainCount = 0;
        const XObject* cur = obj;
        while (cur && chainCount < XSSS_MAX_CASCADE_SOURCES) {
            if (XObject_isWidgetType(cur)) {
                const XString* text = XWidget_styleSheet((const XWidget*)cur);
                if (text && XString_toUtf8_length(text) > 0) {
                    const XCssStyleSheet* s = xsss_sourceSheet(self, text);
                    if (s && s->m_ruleCount > 0 && chainCount <
                            XSSS_MAX_CASCADE_SOURCES)
                        chain[chainCount++] = s;
                }
            }
            cur = (const XObject*)XObject_parent((XObject*)cur);
        }
        {
            int i;
            for (i = 0; i < chainCount && count < XSSS_MAX_CASCADE_SOURCES;
                 ++i) {
                sheets[count] = chain[i];
                depths[count] = chainCount - i + 2;
                ++count;
            }
        }
    }
    return count;
}

/** @brief 全源级联扫描：返回权重最高的匹配规则（同权后到者胜）。
 *
 *  逐条对标 Qt 6.8.3：
 *  - matchRule 权重式（qcssparser.cpp）：
 *      weight = rule.order + specificity*0x100 + (uint(origin)+depth)*0x100000
 *    （uint 运算同 Qt；rule.order=规则在表内序号，对标
 *    StyleSheet::buildIndexes 的 nr.order = i）；
 *  - 裁决序（styleRulesForNode 经 QMultiMap 升序收集后按序应用、后
 *    声明覆盖）：本扫描按源序（应用级→控件链近→远）×规则序×选择器序
 *    前走，`>=` 替换即同权取后；
 *  - pseudoElementWant：NULL=普通属性路径，末段不得带伪元素（对标
 *    declarationsForNode 跳过伪元素规则——旧实现伪元素规则误配基础
 *    控件，本批修正）；非 NULL=伪元素查询路径（末段伪元素须等值）。
 *
 *  @param outSel/outWeight 可选出参（缓存回填用）。
 */
static const XCssStyleRule* xsss_scanRules(XStyleSheetStyle* self,
                                           const XObject* obj, uint32_t state,
                                           const char* pseudoElementWant,
                                           const XCssSelector** outSel,
                                           uint32_t* outWeight)
{
    const XCssStyleSheet* sheets[XSSS_MAX_CASCADE_SOURCES];
    int depths[XSSS_MAX_CASCADE_SOURCES];
    uint32_t statePseudos = xsss_statePseudos(state);
    uint32_t bestWeight = 0;
    const XCssStyleRule* bestRule = NULL;
    const XCssSelector* bestSel = NULL;
    int count = xsss_collectSources(self, obj, sheets, depths);
    int s;
    for (s = 0; s < count; ++s) {
        const XCssStyleSheet* sheet = sheets[s];
        uint32_t originDepth =
            (uint32_t)(XSSS_ORIGIN_INLINE + depths[s]) * 0x100000u;
        int ri;
        for (ri = 0; ri < sheet->m_ruleCount; ++ri) {
            const XCssStyleRule* rule = &sheet->m_rules[ri];
            int si;
            for (si = 0; si < rule->m_selectorCount; ++si) {
                const XCssSelector* sel = &rule->m_selectors[si];
                uint32_t w;
                if (!xsss_selectorMatches(sel, obj, statePseudos,
                                          pseudoElementWant))
                    continue;
                w = (uint32_t)ri +
                    (uint32_t)sel->m_specificity * 0x100u + originDepth;
                if (!bestRule || w >= bestWeight) {
                    bestRule = rule;
                    bestSel = sel;
                    bestWeight = w;
                }
            }
        }
    }
    if (outSel) *outSel = bestSel;
    if (outWeight) *outWeight = bestWeight;
    return bestRule;
}

/**
 * @brief 在级联全源（应用级 + 控件自身 + 祖先 sheets）中查最优先命中
 *        的声明。带单槽渲染规则缓存：缓存规则=该 (对象,状态) 下的级联
 *        赢家（与扫描裁决同口径回填），缓存命中时规则内 findDecl 即
 *        完整级联在该属性上的结果，未声明该属性再回落全源扫描。
 */
static const XCssDeclaration* xsss_lookup(XStyleSheetStyle* self,
                                          const XObject* obj, uint32_t state,
                                          XCssProperty id)
{
    const XCssDeclaration* d;
    if (!self) return NULL;
    /* 缓存命中：同一 (对象,状态) 复用缓存规则。
     * R-29 时代的「全表无 !important 才直返」保守门禁随批一对齐删除：
     * Qt 语义下 importance 不参与裁决，缓存规则（回填口径=同权取后，
     * 与扫描 tie-break 一致）内 findDecl 与全源扫描结果恒等价。 */
    if (self->m_cacheValid && self->m_cacheObj == obj &&
        self->m_cacheState == state) {
        d = self->m_cacheRule ? xsss_findDecl(self->m_cacheRule, id) : NULL;
        if (d) return d;
        /* 缓存规则未声明该属性 → 回落全源扫描。 */
    }
    {
        const XCssSelector* sel = NULL;
        uint32_t w = 0;
        const XCssStyleRule* rule =
            xsss_scanRules(self, obj, state, NULL, &sel, &w);
        self->m_cacheObj = obj;
        self->m_cacheState = state;
        self->m_cacheRule = rule;
        self->m_cacheSel = sel;
        self->m_cacheWeight = w;
        self->m_cacheValid = true;
        return rule ? xsss_findDecl(rule, id) : NULL;
    }
}

/* ==================== 底层样式回落 ==================== */

/** @brief 底层样式（显式设置或全局默认）。 */
static XStyle* xsss_source(XStyleSheetStyle* self)
{
    if (self->m_source) return self->m_source;
    return XStyle_defaultStyle();
}

/* ==================== 绘制分派 ==================== */

/* ===== G4 新增区前置声明（定义在文件尾 G4 锚点内） ===== */
static bool xsss4_resolveColorValue(const char* value,
                                    const XStyleOption* option,
                                    uint32_t* outColor, bool* outIsGradient,
                                    XCssGradient* outGradient);
static bool xsss4_applyFontVariant(XFont* font, const char* text);
static bool xsss4_applySpacingLength(XFont* font, const char* text,
                                     const XCssLengthContext* ctx,
                                     bool isLetter);
static void xsss_applyTextTransform(XStyleSheetStyle* self,
                                    const XObject* obj,
                                    XStyleOption* option);

/** @brief 应用文本色覆盖（绘制前：影响底层文本渲染色）。
 *
 *  对标 QSS 级联：命中 color 时覆盖 option 文本色后交底层绘制；
 *  背景色在底层绘制之后单独覆盖（见 xsss_applyBackground）。
 *
 *  G4 扩展取色链：XCssParseColor 未命中时依次尝试 palette(角色名)
 *  与渐变函数（对标 qcssparser.cpp parseColorValue 的
 *  PlainColor→Palette→Gradient 三态序）。渐变命中时文本管线单色画
 *  笔不可消费——维持不覆盖（能力边界登记 XGui.md），渐变值在背景
 *  路径衔接栅格化消费（衔接点见文件尾 G4 锚注）。 */
static void xsss_applyTextColor(XStyleSheetStyle* self, const XObject* obj,
                                XStyleOption* option)
{
    const XCssDeclaration* fg;
    uint32_t color;
    char buf[256];
    const char* text;
    bool isGradient = false;
    XCssGradient gradient;
    if (!self || !option) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    fg = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_Color);
    if (!fg || !fg->m_value) return;
    text = xsss_declText(fg, buf, sizeof(buf));
    if (!text || !*text) return;
    if (!xsss4_resolveColorValue(text, option, &color, &isGradient,
                                 &gradient))
        return;
    if (isGradient) return;
    option->m_textColor = color;
}

/* ==================== 字体消费端 ==================== */

/** @brief 字号关键字 → 相对档位 -3..3（对标 qcssparser
 *         setFontSizeFromValue 的 fontSizeAdjustment：已核对子集
 *         small=-1/medium=0/large=1/x-large=2/xx-large=3；xx-small/
 *         x-small 不在 Qt KnownIdentifier 表，按任务要求补齐 CSS
 *         7 级同索引口径）。 */
static bool xsss_fontSizeKeyword(const char* s, int* adjustment)
{
    static const char* const kws[7] = { "xx-small", "x-small", "small",
                                        "medium",   "large",   "x-large",
                                        "xx-large" };
    int i;
    if (!s) return false;
    for (i = 0; i < 7; ++i) {
        if (XStrcasecmp(s, kws[i]) == 0) {
            *adjustment = i - 3;
            return true;
        }
    }
    return false;
}

/** @brief 应用字号（px/pt/关键字/em/ex/%）。
 *
 *  分派对标 setFontSizeFromValue：pt → XFont_setPointSizeF（Qt
 *  setPointSizeF 同构）、px → XFont_setPixelSize、关键字 → 相对
 *  档位。em/ex/% 以 ctx->fontPixelSize（画笔当前字号）为参考系——
 *  CSS 语义上字号 % 相对继承字号而非盒矩形（与盒模型 % 参考系不
 *  同，摘要已声明）；参考不可得按 0 不调整。关键字档位系数取 CSS
 *  规范 7 级 {60,75,89,100,120,150,200}%（Qt 端为 defaultFont 尺寸
 *  × updateFontHelper 系数表；该系数原文在沙箱内多路抓取均被截断
 *  未能逐值核对，索引口径按 setFontSizeFromValue 已核对部分对齐）。
 */
static bool xsss_applyFontSize(XFont* font, const char* text,
                               const XCssLengthContext* ctx)
{
    int adj = 0;
    XCssLength len;
    if (!font || !text || !*text) return false;
    if (xsss_fontSizeKeyword(text, &adj)) {
        static const int factors[7] = { 60, 75, 89, 100, 120, 150, 200 };
        if (!ctx || ctx->fontPixelSize <= 0) return false;
        XFont_setPixelSize(
            font, xsss_roundHalfAway(ctx->fontPixelSize *
                                     factors[adj + 3] / 100.0));
        return true;
    }
    if (!XCssParseLengthEx(text, &len)) return false;
    switch (len.m_unit) {
    case XCssLengthUnit_Px:
        if (len.m_value <= 0.0) return false; /* 0/负不调整（旧口径 px>0 生效）。 */
        XFont_setPixelSize(font, xsss_roundHalfAway(len.m_value));
        return true;
    case XCssLengthUnit_Pt:
        if (len.m_value <= 0.0) return false;
        XFont_setPointSizeF(font, len.m_value); /* 对标 Qt pt→setPointSizeF。 */
        return true;
    case XCssLengthUnit_Em:
    case XCssLengthUnit_Ex:
    case XCssLengthUnit_Percent: {
        double px;
        if (!ctx || ctx->fontPixelSize <= 0) return false;
        px = len.m_value * ctx->fontPixelSize;
        if (len.m_unit == XCssLengthUnit_Ex)
            px /= 2.0; /* x 高近似（见 XCssLengthContext 注释）。 */
        else if (len.m_unit == XCssLengthUnit_Percent)
            px /= 100.0;
        if (px <= 0.0) return false;
        XFont_setPixelSize(font, xsss_roundHalfAway(px));
        return true;
    }
    default:
        return false;
    }
}

/** @brief 应用 font-weight（bold/normal 关键字 + 100..900 数值；
 *         对标 setFontWeightFromValue：Normal/Bold 关键字 → 权重
 *         枚举、数值夹取——XFont 权重表 100..900，区间外忽略）。 */
static bool xsss_applyFontWeight(XFont* font, const char* text)
{
    const char* q;
    double v = 0.0;
    if (!font || !text) return false;
    if (XStrcasecmp(text, "bold") == 0) {
        XFont_setWeight(font, XFont_Bold);
        return true;
    }
    if (XStrcasecmp(text, "normal") == 0) {
        XFont_setWeight(font, XFont_Normal);
        return true;
    }
    if (!XIsDigit((unsigned char)text[0])) return false;
    q = text;
    if (!xsss_parseDouble(&q, &v) || *q != '\0') return false;
    if (v < 100.0 || v > 900.0) return false;
    XFont_setWeight(font, xsss_roundHalfAway(v));
    return true;
}

/** @brief 应用 font-style（italic/oblique/normal；对标
 *         setFontStyleFromValue）。 */
static bool xsss_applyFontStyle(XFont* font, const char* text)
{
    if (!font || !text) return false;
    if (XStrcasecmp(text, "italic") == 0 ||
        XStrcasecmp(text, "oblique") == 0) {
        XFont_setItalic(font, true);
        return true;
    }
    if (XStrcasecmp(text, "normal") == 0) {
        XFont_setItalic(font, false);
        return true;
    }
    return false;
}

/** @brief 应用 font-family（对标 setFontFamilyFromValues 列表语义 +
 *         XFont 单字族约束取首个有效字族）：
 *         - 逗号分隔字族列表取首个非空段；
 *         - 引号 "..."/'...' 剥除（引号内空格保留）；
 *         - 未引号多词字族（"Times New Roman"）空白折叠合并。 */
static bool xsss_applyFontFamily(XFont* font, const char* text)
{
    char family[128];
    const char* p = text;
    if (!font || !text) return false;
    while (*p) {
        const char* seg;
        const char* q;
        const char* e;
        char qch = 0;
        size_t w = 0;
        seg = p;
        while (*p && *p != ',') ++p;
        q = seg;
        e = p;
        if (*p == ',') ++p;
        while (q < e && XIsSpace((unsigned char)*q)) ++q;
        while (e > q && XIsSpace((unsigned char)e[-1])) --e;
        if (q < e && (q[0] == '"' || q[0] == '\'')) {
            qch = q[0];
            ++q;
            if (e > q && e[-1] == qch) --e;
        }
        for (; q < e && w + 1 < sizeof(family); ++q) {
            char c = q[0];
            if (qch == 0 && (c == '"' || c == '\'')) continue;
            if (qch == 0 && XIsSpace((unsigned char)c)) {
                if (w > 0 && family[w - 1] != ' ') family[w++] = ' ';
                continue;
            }
            family[w++] = c;
        }
        while (w > 0 && family[w - 1] == ' ') --w;
        family[w] = '\0';
        if (w > 0) {
            XFont_setFamily(font, family);
            return true; /* 首个有效字族即用（Qt setFamilies 首项生效）。 */
        }
        /* 首段为空（",, serif" 之类）→ 继续下一段。 */
    }
    return false;
}

/** @brief font 简写解析（对标 extractFont → parseShorthandFontProperty
 *         分派；token 流：样式/字重关键字 → 字号（含字号关键字）→
 *         字族）。
 *
 *  口径：
 *  - 斜杠行高容忍跳过："12px/30px" 截去行高、独立 "/1.5" token 整体
 *    忽略（行高不作用于 XFont；Qt 侧 line-height 不入 QFont）；
 *  - 引号段（"My Font"/'My Font'）整体作字族段（可含空格）；
 *  - 逗号字族列表取首个有效段；
 *  - 其余未知 token（小型大写变体等）忽略。 */
static bool xsss_applyFontShorthand(XFont* font, const char* text,
                                    const XCssLengthContext* ctx)
{
    char family[128];
    size_t fw = 0;
    bool hasFamily = false;
    bool familyDone = false;
    bool applied = false;
    const char* p = text;
    if (!font || !text) return false;
    while (*p) {
        char tok[64];
        size_t tl = 0;
        bool comma = false;
        if (XIsSpace((unsigned char)*p)) {
            ++p;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            /* 引号段：字族段，可含空格，剥引号。 */
            char qch = *p++;
            while (*p && *p != qch && tl + 1 < sizeof(tok)) {
                if (XIsSpace((unsigned char)*p)) {
                    if (tl > 0 && tok[tl - 1] != ' ') tok[tl++] = ' ';
                } else {
                    tok[tl++] = *p;
                }
                ++p;
            }
            if (*p == qch) ++p;
            while (tl > 0 && tok[tl - 1] == ' ') --tl;
            tok[tl] = '\0';
            if (*p == ',') {
                comma = true;
                ++p;
            }
            if (!hasFamily && !familyDone && tl > 0) {
                XMemcpy(family, tok, tl + 1);
                fw = tl;
                hasFamily = true;
            }
            if (comma) familyDone = true;
            continue;
        }
        while (*p && !XIsSpace((unsigned char)*p) && *p != ',' &&
               tl + 1 < sizeof(tok))
            tok[tl++] = *p++;
        tok[tl] = '\0';
        if (*p == ',') {
            comma = true;
            ++p;
        }
        if (tl == 0) continue;
        if (tok[0] == '/') continue; /* 独立行高段 "/1.5" 容忍跳过。 */
        {
            /* 截去贴写行高（"12px/30px"）。 */
            size_t k;
            for (k = 1; k < tl; ++k) {
                if (tok[k] == '/') {
                    tok[k] = '\0';
                    break;
                }
            }
        }
        /* 分类：样式/字重关键字 → 字号 → 字族累积。 */
        if (XStrcasecmp(tok, "bold") == 0) {
            XFont_setWeight(font, XFont_Bold);
            applied = true;
        } else if (XStrcasecmp(tok, "normal") == 0) {
            XFont_setWeight(font, XFont_Normal);
            XFont_setItalic(font, false);
            applied = true;
        } else if (XStrcasecmp(tok, "italic") == 0 ||
                   XStrcasecmp(tok, "oblique") == 0) {
            XFont_setItalic(font, true);
            applied = true;
        } else if (XIsDigit((unsigned char)tok[0]) &&
                   xsss_applyFontWeight(font, tok)) {
            applied = true;
        } else if (xsss_applyFontSize(font, tok, ctx)) {
            applied = true; /* 长度或字号关键字。 */
        } else if (!familyDone) {
            /* 未引号字族段：多词空格合并（"Times New Roman"）。 */
            size_t i;
            if (hasFamily && fw > 0 && fw + 1 < sizeof(family))
                family[fw++] = ' ';
            for (i = 0; tok[i] && fw + 1 < sizeof(family); ++i)
                family[fw++] = tok[i];
            hasFamily = true;
        }
        if (comma) familyDone = true;
    }
    if (hasFamily && fw > 0) {
        family[fw] = '\0';
        XFont_setFamily(font, family);
        applied = true;
    }
    return applied;
}

/** @brief 应用字体覆盖（绘制前：font-family/font-size/weight/style 及
 *         G4 的 font-variant/letter-spacing/word-spacing 命中时重设画
 *         家字体，对标 extractFont 的逐声明分派）。 */
static void xsss_applyFont(XStyleSheetStyle* self, const XObject* obj,
                           XStyleOption* option, XPainter* painter)
{
    const XCssDeclaration* family;
    const XCssDeclaration* size;
    const XCssDeclaration* weight;
    const XCssDeclaration* ital;
    const XCssDeclaration* fontShorthand;
    const XCssDeclaration* variant;
    const XCssDeclaration* letterSp;
    const XCssDeclaration* wordSp;
    XFont font;
    XCssLengthContext ctx;
    char buf[256];
    const char* text;
    XString* famOrig;
    bool changed = false;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    /* 基准字体与 em/ex/% 参考系：画笔当前字体（底层绘制会再按控件
     * 字体覆盖；字号相对单位的参考系=该基准的像素高）。 */
    {
        const XFont* cur = XPainter_font(painter);
        if (!cur) return;
        font = *cur;
        ctx = xsss_lengthContext(painter, NULL);
    }
    famOrig = font.m_family;
    family = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_FontFamily);
    size = xsss_lookup(self, obj, option->m_state,
                       XCssProperty_FontSize);
    weight = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_FontWeight);
    ital = xsss_lookup(self, obj, option->m_state,
                       XCssProperty_FontStyle);
    fontShorthand = xsss_lookup(self, obj, option->m_state,
                                XCssProperty_Font);
    if (family && family->m_value) {
        text = xsss_declText(family, buf, sizeof(buf));
        if (text && *text && xsss_applyFontFamily(&font, text)) changed = true;
    }
    if (size && size->m_value) {
        text = xsss_declText(size, buf, sizeof(buf));
        if (text && *text && xsss_applyFontSize(&font, text, &ctx))
            changed = true;
    }
    if (weight && weight->m_value) {
        text = xsss_declText(weight, buf, sizeof(buf));
        if (text && *text && xsss_applyFontWeight(&font, text)) changed = true;
    }
    if (ital && ital->m_value) {
        text = xsss_declText(ital, buf, sizeof(buf));
        if (text && *text && xsss_applyFontStyle(&font, text)) changed = true;
    }
    if (fontShorthand && fontShorthand->m_value) {
        text = xsss_declText(fontShorthand, buf, sizeof(buf));
        if (text && *text &&
            xsss_applyFontShorthand(&font, text, &ctx))
            changed = true;
    }
    /* G4 文本属性消费：font-variant / letter-spacing / word-spacing
     * → 画家字体旗标（对标 setTextVariantFromValue →
     * QFont::setCapitalization、setLetterSpacingFromValue →
     * QFont::setLetterSpacing(AbsoluteSpacing)）。可见效果受字体渲染
     * 器能力边界约束（旗标当前渲染链零消费，登记 XGui.md），旗标本
     * 身按 Qt 口径落盘。 */
    variant = xsss_lookup(self, obj, option->m_state,
                          XCssProperty_FontVariant);
    letterSp = xsss_lookup(self, obj, option->m_state,
                           XCssProperty_LetterSpacing);
    wordSp = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_WordSpacing);
    if (variant && variant->m_value) {
        text = xsss_declText(variant, buf, sizeof(buf));
        if (text && *text && xsss4_applyFontVariant(&font, text))
            changed = true;
    }
    if (letterSp && letterSp->m_value) {
        text = xsss_declText(letterSp, buf, sizeof(buf));
        if (text && *text &&
            xsss4_applySpacingLength(&font, text, &ctx, true))
            changed = true;
    }
    if (wordSp && wordSp->m_value) {
        text = xsss_declText(wordSp, buf, sizeof(buf));
        if (text && *text &&
            xsss4_applySpacingLength(&font, text, &ctx, false))
            changed = true;
    }
    if (changed) XPainter_setFont(painter, &font);
    /* 工作字体被 setFamily 替换出的自建家族串就地释放：画家经
     * VXFont_copy 已深拷贝接管，本地不释放即每次绘制漏一串（LSan
     * 实证，QSS font 简写/font-family 路径）。两种情形不可释——
     * 未替换（指针=初值，仍与画家当前字体共享）；共享默认家族单例
     * （XFont_isSharedFamily 为 XFont.c 私有，以 XFont_init 探针取
     * 单例指针比对）。 */
    if (font.m_family != famOrig) {
        XFont probe;
        XFont_init(&probe);
        if (font.m_family != probe.m_family)
            XClassDelete((XClass*)font.m_family);
    }
}

/** @brief 应用文本装饰（绘制前：作用于画家字体——完整对标 Qt
 *         setTextDecorationFromValues → QFont）。
 *
 *  Qt 语义：值表逐项处理——underline/overline/line-through 各自置
 *  位、none 清全部三者；blink 与未知标识不在 KnownIdentifier 表、
 *  直接忽略；空格组合即多值顺序逐个生效（"underline none" net 为
 *  无装饰，顺序敏感，与 Qt 一致）。装饰线由文本渲染器按字体标志
 *  绘制，而非在矩形上补画线。 */
static void xsss_applyTextDecoration(XStyleSheetStyle* self,
                                     const XObject* obj,
                                     XStyleOption* option,
                                     XPainter* painter)
{
    const XCssDeclaration* d;
    const XFont* cur;
    XFont font;
    char buf[256];
    const char* v;
    bool set = false;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_TextDecoration);
    if (!d || !d->m_value) return;
    v = xsss_declText(d, buf, sizeof(buf));
    if (!v || !*v) return;
    cur = XPainter_font(painter);
    if (!cur) return;
    font = *cur;
    while (*v) {
        char tok[64];
        size_t tl = 0;
        while (v[tl] && !XIsSpace((unsigned char)v[tl]) &&
               tl + 1 < sizeof(tok))
            ++tl;
        XMemcpy(tok, v, tl);
        tok[tl] = '\0';
        v += tl;
        while (*v && XIsSpace((unsigned char)*v)) ++v;
        if (tl == 0) continue;
        if (XStrcasecmp(tok, "underline") == 0) {
            XFont_setUnderline(&font, true);
            set = true;
        } else if (XStrcasecmp(tok, "overline") == 0) {
            XFont_setOverline(&font, true);
            set = true;
        } else if (XStrcasecmp(tok, "line-through") == 0) {
            XFont_setStrikeOut(&font, true);
            set = true;
        } else if (XStrcasecmp(tok, "none") == 0) {
            XFont_setUnderline(&font, false);
            XFont_setOverline(&font, false);
            XFont_setStrikeOut(&font, false);
            set = true;
        }
        /* blink 与未知装饰忽略（对标 Qt 未知标识跳过）。 */
    }
    if (set) XPainter_setFont(painter, &font);
}

/* G3 新增区（文件尾锚点之间）背景图像/渐变/border-image/outline/image
 * 属性消费入口的前置声明（定义在尾部）。 */
/** @brief url 路径缓冲容量（资源路径 + NUL；超长整串拒绝不截断）。 */
#define XSSS3_URL_CAP 512

static void xg3_drawGradient(XStyleSheetStyle* self, const XObject* obj,
                             const XStyleOption* option, XPainter* painter,
                             const XRect* boxRect, const XCssGradient* grad,
                             const char* keyText);
static void xg3_drawBackgroundImage(XStyleSheetStyle* self,
                                    const XObject* obj,
                                    const XStyleOption* option,
                                    XPainter* painter, const XRect* boxRect,
                                    const char* shorthandUrl);
static bool xg3_borderImageDraw(XStyleSheetStyle* self, const XObject* obj,
                                const XStyleOption* option, XPainter* painter,
                                const XRect* boxRect,
                                const XCssLengthContext* ctx);
static void xg3_outlineDraw(XStyleSheetStyle* self, const XObject* obj,
                            const XStyleOption* option, XPainter* painter,
                            const XRect* boxRect, const XCssLengthContext* ctx);
static void xg3_ruleImageDraw(XStyleSheetStyle* self, const XObject* obj,
                              const XStyleOption* option, XPainter* painter,
                              const XRect* boxRect,
                              const XCssLengthContext* ctx);
/** @brief 普通边框绘制实现（原 xsss_drawBorder 函数体；定义见分派壳
 *         之后）。 */
static void xg3_borderDrawImpl(XStyleSheetStyle* self, const XObject* obj,
                               const XStyleOption* option, XPainter* painter,
                               const XRect* boxRect,
                               const XCssLengthContext* ctx);

/** @brief 应用背景色/背景图像覆盖（绘制后：QSS 背景优先于底层面板填充）。
 *
 *  透明语义（任务批一 #6）：alpha==0 跳过 fillRect（对标 Qt 透明
 *  画刷填充为无操作，底层面板透出；transparent 具名色即此路径）。
 *  G3 扩展（对标 QRenderRule::drawBackground 的分支序）：背景值经
 *  G4 取色链 xsss4_resolveColorValue（色→palette→渐变三态序，G4 锚
 *  注约定接线点）解析——纯色/palette 角色色按原口径填充、渐变走
 *  G3 栅格化（xg3_drawGradient，ObjectBoundingMode 坐标）；值非色
 *  时尝试 url(...)（uri 分支——装载平铺绘制）；background-image
 *  声明独立消费（repeat/position 见 G3 新增区 xg3_drawBackground-
 *  Image）。无图像/渐变声明时各扩展入口首行短路，行为与旧实现
 *  逐字节一致（G4 并入前未识别值=不绘制，此后 palette 色生效）。 */
static void xsss_applyBackground(XStyleSheetStyle* self, const XObject* obj,
                                 const XStyleOption* option,
                                 XPainter* painter, const XRect* boxRect)
{
    const XCssDeclaration* bg;
    uint32_t color;
    char buf[256];
    const char* text;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    bg = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BackgroundColor);
    if (!bg)
        bg = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_Background);
    text = (bg && bg->m_value) ? xsss_declText(bg, buf, sizeof(buf)) : NULL;
    if (text && *text) {
        bool isGradient = false;
        XCssGradient grad;
        char url[XSSS3_URL_CAP];
        if (xsss4_resolveColorValue(text, option, &color, &isGradient,
                                    &grad)) {
            if (isGradient) {
                /* 渐变画刷（对标 drawBackground 的 brush 分支；坐标
                 * ObjectBoundingMode 口径，栅格化在 xg3_drawGradient）。 */
                xg3_drawGradient(self, obj, option, painter, boxRect,
                                 &grad, text);
            } else if ((color & 0xFF000000u) != 0) {
                /* 全透明 → 不填充（透明画刷无像素，同旧口径）。 */
                XPainter_fillRect(painter,
                                  boxRect ? boxRect : &option->m_rect,
                                  color);
            }
        } else if (XCssParseUrlPath(text, url, sizeof(url))) {
            /* background 简写携带 url(...)（对标 extractBackground 的
             * uri 分支）；repeat/position 与 background-image 同路。 */
            xg3_drawBackgroundImage(self, obj, option, painter, boxRect,
                                    url);
        }
    }
    xg3_drawBackgroundImage(self, obj, option, painter, boxRect, NULL);
}

/* ==================== 盒模型 ==================== */

/** @brief 单声明长度取值 → 像素（带上下文；失败不动 *out）。 */
static bool xsss_declPx(XStyleSheetStyle* self, const XObject* obj,
                        uint32_t state, XCssProperty id,
                        const XCssLengthContext* ctx, bool horizontal,
                        int* out)
{
    const XCssDeclaration* d;
    char buf[256];
    const char* text;
    XCssLength len;
    d = xsss_lookup(self, obj, state, id);
    if (!d || !d->m_value) return false;
    text = xsss_declText(d, buf, sizeof(buf));
    if (!text || !*text) return false;
    if (!XCssParseLengthEx(text, &len)) return false;
    *out = xsss_lengthPx(&len, ctx, horizontal);
    return true;
}

/** @brief CSS 盒简写多值展开（对标 qtbase qcssparser
 *         ValueExtractor::lengthValues）。
 *
 *  根因（R-28）：此前简写值整体喂给前缀解析，"4px 8px" 被吞成 4 当
 *  单值四边；margin 的多值展开分支因此成死代码。CSS 盒语义（Qt 同）：
 *  token 序为顺时针「上 右 下 左」——1 值=四边；2 值=上下/左右；
 *  3 值=上/左右/下；4 值=上右下左。out 为调用方约定的 左/上/右/下
 *  序；返回有效长度 token 数（0=无可解析长度，交由单侧声明回落）。
 *  各边独立换算（% 按轴：左右参考宽、上下参考高；em/ex 按上下文
 *  字体高）。 */
static int xsss_boxExpand(const char* value, const XCssLengthContext* ctx,
                          int out[4])
{
    XCssLength vals[4];
    XCssLength ordered[4];
    int n = 0;
    int i;
    for (i = 0; i < 4; ++i) {
        vals[i].m_value = 0.0;
        vals[i].m_unit = XCssLengthUnit_None;
        ordered[i] = vals[i];
        out[i] = 0;
    }
    while (value && *value && n < 4) {
        char buf[64];
        size_t tl = 0;
        while (value[tl] && !XIsSpace((unsigned char)value[tl]) &&
               tl + 1 < sizeof(buf))
            ++tl;
        XMemcpy(buf, value, tl);
        buf[tl] = '\0';
        value += tl;
        while (*value && XIsSpace((unsigned char)*value)) ++value;
        if (tl > 0 && XCssParseLengthEx(buf, &vals[n])) ++n;
    }
    switch (n) {
    case 1: /* 单值四边。 */
        ordered[0] = ordered[1] = ordered[2] = ordered[3] = vals[0];
        break;
    case 2: /* 上下=值1、左右=值2。 */
        ordered[1] = ordered[3] = vals[0];
        ordered[0] = ordered[2] = vals[1];
        break;
    case 3: /* 上=值1、左右=值2、下=值3。 */
        ordered[1] = vals[0];
        ordered[0] = ordered[2] = vals[1];
        ordered[3] = vals[2];
        break;
    case 4: /* 顺时针 上右下左 → 左/上/右/下。 */
        ordered[1] = vals[0];
        ordered[2] = vals[1];
        ordered[3] = vals[2];
        ordered[0] = vals[3];
        break;
    default:
        break;
    }
    for (i = 0; i < 4; ++i)
        out[i] = xsss_lengthPx(&ordered[i], ctx, i == 0 || i == 2);
    return n;
}

/** @brief 查询盒模型内边距（padding 系列声明；1/2/3/4 值简写展开）。 */
static void xsss_padding(XStyleSheetStyle* self, const XObject* obj,
                         uint32_t state, const XCssLengthContext* ctx,
                         int out[4])
{
    const XCssDeclaration* d;
    char buf[256];
    const char* text;
    out[0] = out[1] = out[2] = out[3] = 0; /* 左/上/右/下。 */
    d = xsss_lookup(self, obj, state, XCssProperty_Padding);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        if (text && *text && xsss_boxExpand(text, ctx, out) > 0)
            return; /* 简写可解析 → 四边生效（多值按 CSS 盒序展开）。 */
    }
    /* 单侧回落（% 按轴：左右参考宽、上下参考高）。 */
    (void)xsss_declPx(self, obj, state, XCssProperty_PaddingLeft, ctx,
                      true, &out[0]);
    (void)xsss_declPx(self, obj, state, XCssProperty_PaddingTop, ctx,
                      false, &out[1]);
    (void)xsss_declPx(self, obj, state, XCssProperty_PaddingRight, ctx,
                      true, &out[2]);
    (void)xsss_declPx(self, obj, state, XCssProperty_PaddingBottom, ctx,
                      false, &out[3]);
}

/** @brief 查询盒模型外边距（margin 声明；1/2/3/4 值简写展开）。
 *
 *  注：属性表（XCssStyleSheet 的枚举与 k_propNames）只有 margin 整体
 *  声明、无 margin-left 等单侧成员，故仅简写一源（与枚举一致）。
 */
static void xsss_margin(XStyleSheetStyle* self, const XObject* obj,
                        uint32_t state, const XCssLengthContext* ctx,
                        int out[4])
{
    const XCssDeclaration* d;
    char buf[256];
    const char* text;
    out[0] = out[1] = out[2] = out[3] = 0; /* 左/上/右/下。 */
    d = xsss_lookup(self, obj, state, XCssProperty_Margin);
    if (!d || !d->m_value) return;
    text = xsss_declText(d, buf, sizeof(buf));
    if (text && *text) xsss_boxExpand(text, ctx, out);
}

/** @brief 绘制 QSS 边框（border-style/width/color/radius 命中时；
 *         solid/dashed/dotted/none 全支持，对标 qcss 的 BorderStyle）。
 *
 *  G3 起为分派壳：border-image 命中时九宫格切片替代普通边框（对标
 *  QRenderRule::drawBorder 的 hasBorderImage 提前返回分支）；否则
 *  普通边框；随后 outline 族描边与 image:/icon 属性绘制接力（对标
 *  drawRule 的 frame → drawImage 序）。各扩展入口无命中时首行短路，
 *  无 border-image/outline/image 声明时行为与旧实现逐字节一致。
 */
static void xsss_drawBorder(XStyleSheetStyle* self, const XObject* obj,
                            const XStyleOption* option, XPainter* painter,
                            const XRect* boxRect, const XCssLengthContext* ctx)
{
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    if (!xg3_borderImageDraw(self, obj, option, painter, boxRect, ctx))
        xg3_borderDrawImpl(self, obj, option, painter, boxRect, ctx);
    xg3_outlineDraw(self, obj, option, painter, boxRect, ctx);
    xg3_ruleImageDraw(self, obj, option, painter, boxRect, ctx);
}

/** @brief 普通边框绘制（原 xsss_drawBorder 函数体，逐字节保留）。
 *
 *  宽度取值口径：border 简写整串不再整喂解析（严格化后带尾 token
 *  会整串拒绝），改为逐 token 取首个可解析长度分量；宽度/圆角走
 *  带上下文换算（boxRect 为 % 参考矩形、画笔字体为 em/ex 参考）。
 *  颜色口径（任务批一 #6）：hasColor 与色值分离——旧实现以 0 双义
 *  「未指定/全透明」，透明色被误回落文本色；现全透明（alpha==0）
 *  直接跳过绘制（对标透明画笔不产生像素），未指定才走 currentColor
 *  回落；setPen 移到色值落定之后（旧实现回落色算出后未再上笔）。
 */
static void xg3_borderDrawImpl(XStyleSheetStyle* self, const XObject* obj,
                               const XStyleOption* option, XPainter* painter,
                               const XRect* boxRect,
                               const XCssLengthContext* ctx)
{
    const XCssDeclaration* bw;
    const XCssDeclaration* bc;
    const XCssDeclaration* br;
    const XCssDeclaration* bs;
    int width = 0;
    bool hasWidth = false;
    bool hasColor = false; /* 「未指定色」与「色为全透明」分离。 */
    uint32_t color = 0;
    int radius = 0;
    int style = 0; /* 0 solid / 1 dashed / 2 dotted / -1 none。 */
    char bbuf[256];
    const char* text;
    XRect r;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    bw = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderWidth);
    if (!bw)
        bw = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_Border);
    /* 宽度：简写/单值统一逐 token 取首个长度分量。 */
    if (bw && bw->m_value) {
        text = xsss_declText(bw, bbuf, sizeof(bbuf));
        while (text && *text && !hasWidth) {
            char tok[64];
            size_t tl = 0;
            XCssLength len;
            while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, text, tl);
            tok[tl] = '\0';
            text += tl;
            while (*text && XIsSpace((unsigned char)*text)) ++text;
            if (XCssParseLengthEx(tok, &len)) {
                width = xsss_lengthPx(&len, ctx, true);
                hasWidth = true;
            }
        }
    }
    bc = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderColor);
    if (bc && bc->m_value) {
        text = xsss_declText(bc, bbuf, sizeof(bbuf));
        if (text && *text && XCssParseColor(text, &color)) hasColor = true;
    }
    bs = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderStyle);
    if (bs && bs->m_value) {
        text = xsss_declText(bs, bbuf, sizeof(bbuf));
        if (text && *text && XStrcasecmp(text, "none") == 0) return;
        if (text && *text && XStrcasecmp(text, "dashed") == 0) style = 1;
        else if (text && *text && XStrcasecmp(text, "dotted") == 0) style = 2;
    }
    /* border 简写扫描（"2px solid #FF0000"）：颜色 token 与风格关键字。 */
    if (bw && bw->m_value) {
        text = xsss_declText(bw, bbuf, sizeof(bbuf));
        while (text && *text) {
            char tok[64];
            size_t tl = 0;
            uint32_t token = 0;
            while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, text, tl);
            tok[tl] = '\0';
            text += tl;
            while (*text && XIsSpace((unsigned char)*text)) ++text;
            if (!hasColor && XCssParseColorToken(tok, tl, &token)) {
                color = token;
                hasColor = true;
            } else if (XStrcasecmp(tok, "solid") == 0) {
                style = 0;
            } else if (XStrcasecmp(tok, "dashed") == 0) {
                style = 1;
            } else if (XStrcasecmp(tok, "dotted") == 0) {
                style = 2;
            } else if (XStrcasecmp(tok, "none") == 0) {
                return;
            }
        }
    }
    if (!hasWidth || width <= 0) return;
    if (hasColor && (color & 0xFF000000u) == 0)
        return; /* 全透明边框不产生像素（对标透明画笔）。 */
    if (!hasColor) {
        /* CSS 语义：border 无色时回落 currentColor（QSS color 声明
         * → 文本色 → 不透明白）。 */
        const XCssDeclaration* fg = xsss_lookup(self, obj,
                                                option->m_state,
                                                XCssProperty_Color);
        text = fg ? xsss_declText(fg, bbuf, sizeof(bbuf)) : NULL;
        if (text && *text && XCssParseColor(text, &color))
            hasColor = true;
        else if (option->m_textColor) {
            color = option->m_textColor;
            hasColor = true;
        } else {
            color = 0xFF000000u;
        }
    }
    /* 画笔色落定后再上笔（alpha 通道随色值完整传递）。 */
    XPainter_setPen(painter, color);
    r = boxRect ? *boxRect : option->m_rect;
    if (width >= 2) {
        r.x += width / 2;
        r.y += width / 2;
        r.width -= width;
        r.height -= width;
    }
    br = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderRadius);
    if (br && br->m_value) {
        text = xsss_declText(br, bbuf, sizeof(bbuf));
        if (text && *text)
            (void)xsss_declPx(self, obj, option->m_state,
                              XCssProperty_BorderRadius, ctx, true,
                              &radius);
    }
#if XPAINTER_SHAPE_ON
    if (radius > 0) {
        /* 圆角描边内收到边框环中线（D1 根修，2026-10-01）：整数光栅
         * 的闭合折线描边把路径右/下边界坐标 x+w/y+h 落成盒外 1px 的
         * 像素列/行（XFusionStyle.c 圆角框「右/下内缩 1px」与
         * XVirtualKeyboard.c xkb_roundRect「宽高 -5」皆为此既档口径
         * 的调用侧补偿），再经控件裁剪后 1px 圆角边框只剩顶/左、
         * 右/底直段整段缺失（用户实机报告 D1 实锚：堆叠页/导航钮四
         * 边逐像素比对，右/底缺失处为纯白）。描边矩形改按「左/上
         * +W/2、宽/高 -1-2*(W/2)」内收：W=1 时右/下各入 1px，环完整
         * 落在盒内；W>=3 奇数宽的右/下中心线 x1-W/2 与旧径逐像素同
         * 值；偶数宽中心线内移 1px（旧落环外圈列，现无在绘调用方）。
         * 直角/虚线分支仍用上方旧 r（drawLine 含端点，几何本就闭
         * 合）；radius 仍取 QSS 声明值；带画刷的填充口径不经本分支
         * （xsss2_ruleBorder / outline 镜像块不在本任务白名单，维持
         * 旧径，登记 XGui.md）。 */
        XRect sr = boxRect ? *boxRect : option->m_rect;
        sr.x += width / 2;
        sr.y += width / 2;
        sr.width -= 1 + (width / 2) * 2;
        sr.height -= 1 + (width / 2) * 2;
        if (sr.width > 0 && sr.height > 0)
            XPainter_drawRoundedRect(painter, &sr, radius, radius);
        return;
    }
#endif
    if (style == 1 || style == 2) {
        /* 虚线/点线：四边分段绘制（4px 段+4px 空 / 2px 点+4px 空）。 */
        int seg = (style == 1) ? 4 : 2;
        int gap = 4;
        int i;
        int x0 = r.x;
        int y0 = r.y;
        int x1 = r.x + r.width - 1;
        int y1 = r.y + r.height - 1;
        for (i = 0; i <= x1 - x0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > x1 - x0) e = x1 - x0;
            XPainter_drawLine(painter, x0 + i, y0, x0 + e, y0);
            XPainter_drawLine(painter, x0 + i, y1, x0 + e, y1);
        }
        for (i = 0; i <= y1 - y0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > y1 - y0) e = y1 - y0;
            XPainter_drawLine(painter, x0, y0 + i, x0, y0 + e);
            XPainter_drawLine(painter, x1, y0 + i, x1, y0 + e);
        }
        return;
    }
    /* 直角边框：drawLine 四边（线宽 1px/次，width>1 时多层内缩）。 */
    {
        int i;
        for (i = 0; i < width; ++i) {
            XPainter_drawLine(painter, r.x + i, r.y + i,
                              r.x + r.width - 1 - i, r.y + i);
            XPainter_drawLine(painter, r.x + r.width - 1 - i, r.y + i,
                              r.x + r.width - 1 - i,
                              r.y + r.height - 1 - i);
            XPainter_drawLine(painter, r.x + r.width - 1 - i,
                              r.y + r.height - 1 - i, r.x + i,
                              r.y + r.height - 1 - i);
            XPainter_drawLine(painter, r.x + i, r.y + r.height - 1 - i,
                              r.x + i, r.y + i);
        }
    }
}

/** @brief 应用盒模型（绘制前：margin 外扩 / padding 内缩内容区，
 *         对标 QStyleSheetStyle 的 box 计算；长度换算带上下文）。 */
static void xsss_applyBoxModel(XStyleSheetStyle* self, const XObject* obj,
                               XStyleOption* option,
                               const XCssLengthContext* ctx)
{
    int pad[4];
    int mar[4];
    if (!self || !option) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    xsss_padding(self, obj, option->m_state, ctx, pad);
    xsss_margin(self, obj, option->m_state, ctx, mar);
    if (pad[0] || pad[1] || pad[2] || pad[3] ||
        mar[0] || mar[1] || mar[2] || mar[3]) {
        /* content = 原矩形 + margin - padding。 */
        option->m_rect.x += mar[0] - pad[0];
        option->m_rect.y += mar[1] - pad[1];
        option->m_rect.width += mar[0] + mar[2] - pad[0] - pad[2];
        option->m_rect.height += mar[1] + mar[3] - pad[1] - pad[3];
        if (option->m_rect.width < 1) option->m_rect.width = 1;
        if (option->m_rect.height < 1) option->m_rect.height = 1;
    }
}

/** @brief 计算背景/边框盒矩形（原矩形 + margin）。 */
static XRect xsss_boxRect(XStyleSheetStyle* self, const XObject* obj,
                          uint32_t state, const XRect* rect,
                          const XCssLengthContext* ctx)
{
    XRect box = *rect;
    int mar[4];
    xsss_margin(self, obj, state, ctx, mar);
    box.x -= mar[0];
    box.y -= mar[1];
    box.width += mar[0] + mar[2];
    box.height += mar[1] + mar[3];
    if (box.width < 1) box.width = 1;
    if (box.height < 1) box.height = 1;
    return box;
}

static void VXStyleSheetStyle_drawPrimitive(XStyle* self, int pe,
                                            const XStyleOption* option,
                                            XPainter* painter,
                                            const XWidget* widget)
{
    XStyleSheetStyle* ss = (XStyleSheetStyle*)self;
    XStyle* src = xsss_source(ss);
    XStyleOption opt;
    if (option) {
        XCssLengthContext ctx =
            xsss_lengthContext(painter, &option->m_rect);
        opt = *option;
        xsss_applyTextColor(ss, (const XObject*)widget, &opt);
        xsss_applyFont(ss, (const XObject*)widget, &opt, painter);
        xsss_applyTextDecoration(ss, (const XObject*)widget, &opt, painter);
        /* G4：text-transform（option->m_text 通道纯字符串变换，见
         * xsss_applyTextTransform 注释的消费面与不接入面登记）。 */
        xsss_applyTextTransform(ss, (const XObject*)widget, &opt);
        xsss_applyBoxModel(ss, (const XObject*)widget, &opt, &ctx);
        if (src && src != self)
            XStyle_drawPrimitive(src, pe, &opt, painter, widget);
        {
            XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                     option->m_state, &option->m_rect,
                                     &ctx);
            xsss_applyBackground(ss, (const XObject*)widget, option,
                                 painter, &box);
            xsss_drawBorder(ss, (const XObject*)widget, option, painter,
                            &box, &ctx);
        }
        return;
    }
    if (src && src != self)
        XStyle_drawPrimitive(src, pe, option, painter, widget);
}

static void VXStyleSheetStyle_drawControl(XStyle* self, int ce,
                                          const XStyleOption* option,
                                          XPainter* painter,
                                          const XWidget* widget)
{
    XStyleSheetStyle* ss = (XStyleSheetStyle*)self;
    XStyle* src = xsss_source(ss);
    XStyleOption opt;
    if (option) {
        XCssLengthContext ctx =
            xsss_lengthContext(painter, &option->m_rect);
        opt = *option;
        xsss_applyTextColor(ss, (const XObject*)widget, &opt);
        xsss_applyFont(ss, (const XObject*)widget, &opt, painter);
        xsss_applyTextDecoration(ss, (const XObject*)widget, &opt, painter);
        /* G4：text-transform（option->m_text 通道纯字符串变换，见
         * xsss_applyTextTransform 注释的消费面与不接入面登记）。 */
        xsss_applyTextTransform(ss, (const XObject*)widget, &opt);
        xsss_applyBoxModel(ss, (const XObject*)widget, &opt, &ctx);
        if (src && src != self)
            XStyle_drawControl(src, ce, &opt, painter, widget);
        {
            XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                     option->m_state, &option->m_rect,
                                     &ctx);
            xsss_applyBackground(ss, (const XObject*)widget, option,
                                 painter, &box);
            xsss_drawBorder(ss, (const XObject*)widget, option, painter,
                            &box, &ctx);
        }
        return;
    }
    if (src && src != self)
        XStyle_drawControl(src, ce, option, painter, widget);
}

/* G2 新增区（文件尾锚点之间）子控件分派入口的前置声明（定义在尾部）。 */
static void xsss2_dispatchSubControls(XStyleSheetStyle* self, int cc,
                                      const XStyleOption* option,
                                      XPainter* painter,
                                      const XWidget* widget);

static void VXStyleSheetStyle_drawComplexControl(XStyle* self, int cc,
                                                 const XStyleOption* option,
                                                 XPainter* painter,
                                                 const XWidget* widget)
{
    XStyleSheetStyle* ss = (XStyleSheetStyle*)self;
    XStyle* src = xsss_source(ss);
    /* 禁止基类切片拷贝：XStyleOption opt = *option 只复制基类字段，
     * Complex 族扩展（subControls/activeSubControls 及派生选项）全部
     * 丢失，下游按原尺寸读取=栈垃圾——CC_TitleBar 的 subControls 读到
     * 堆指针值致标题栏按钮/标签全部不绘（昆仑通态真机 2026-09-28 探
     * 针实证 sc=0xbe8565a0）。老复杂控件（滚动条/滑块）恰好只用基类
     * 字段（m_scroll 系与 m_horizontal 等均在基类）故潜伏。字体是画笔级状态
     * 仍照常生效；textColor/box 基字段改写对复杂控件跳过（QSS 对复
     * 杂控件子件的着色本就应画在子基元上）。 */
    if (option)
        xsss_applyFont(ss, (const XObject*)widget, option, painter);
    if (src && src != self)
        XStyle_drawComplexControl(src, cc, option, painter, widget);
    if (option) {
        XCssLengthContext ctx =
            xsss_lengthContext(painter, &option->m_rect);
        XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                 option->m_state, &option->m_rect, &ctx);
        xsss_applyBackground(ss, (const XObject*)widget, option, painter,
                             &box);
        xsss_drawBorder(ss, (const XObject*)widget, option, painter, &box,
                        &ctx);
        /* 子控件分派（对标 QStyleSheetStyle::drawComplexControl 的
         * renderRule(w, opt, pseudoElement) 逐子控件查询）：命中子控件
         * 规则的子件按 QRenderRule 语义（背景/边框/圆角）覆盖绘制，
         * 未命中与无规则零附加绘制（函数内首行短路，行为逐字节不变）。
         * 置于控件级盒覆盖之后——子件 chrome 位于控件背景之上（Qt
         * 同序：控件盒 → 子控件）。 */
        xsss2_dispatchSubControls(ss, cc, option, painter, widget);
    }
}

/* ==================== 尺寸计算（对标 QStyleSheetStyle::sizeFromContents：
 *   底层样式尺寸 + width/height 覆盖 + min/max 夹取 + margin/padding
 *   外扩） ==================== */

static XSize VXStyleSheetStyle_sizeFromContents(XStyle* self, int ct,
                                                const XStyleOption* option,
                                                XSize contentSize)
{
    XStyleSheetStyle* ss = (XStyleSheetStyle*)self;
    XStyle* src = xsss_source(ss);
    XSize size;
    int v = 0;
    int pad[4];
    int mar[4];
    XCssLengthContext ctx;
    const XObject* obj = option ? (const XObject*)option->m_styleObject
                                : NULL;
    uint32_t state = option ? option->m_state : 0;
    if (src && src != self)
        size = XStyle_sizeFromContents(src, ct, option, contentSize);
    else
        size = contentSize;
    if (!ss || ss->m_sheet.m_ruleCount == 0) return size;
    /* 尺寸路径无画笔/矩形上下文：% 与 em/ex 参考不可得按 0（不改写
     * 尺寸，见 XCssLengthContext 口径注释）；px/pt 正常换算。 */
    ctx.refWidth = 0;
    ctx.refHeight = 0;
    ctx.fontPixelSize = 0;
    /* width/height 覆盖（v>0 才写，0 值不塌缩尺寸）。 */
    if (xsss_declPx(ss, obj, state, XCssProperty_Width, &ctx, true, &v) &&
        v > 0)
        size.width = v;
    if (xsss_declPx(ss, obj, state, XCssProperty_Height, &ctx, false,
                    &v) &&
        v > 0)
        size.height = v;
    /* min/max 夹取（v>0 才夹）。 */
    if (xsss_declPx(ss, obj, state, XCssProperty_MinWidth, &ctx, true,
                    &v) &&
        v > 0 && size.width < v)
        size.width = v;
    if (xsss_declPx(ss, obj, state, XCssProperty_MinHeight, &ctx, false,
                    &v) &&
        v > 0 && size.height < v)
        size.height = v;
    if (xsss_declPx(ss, obj, state, XCssProperty_MaxWidth, &ctx, true,
                    &v) &&
        v > 0 && size.width > v)
        size.width = v;
    if (xsss_declPx(ss, obj, state, XCssProperty_MaxHeight, &ctx, false,
                    &v) &&
        v > 0 && size.height > v)
        size.height = v;
    /* margin + padding 外扩。 */
    xsss_padding(ss, obj, state, &ctx, pad);
    xsss_margin(ss, obj, state, &ctx, mar);
    size.width += mar[0] + mar[2] + pad[0] + pad[2];
    size.height += mar[1] + mar[3] + pad[1] + pad[3];
    return size;
}

/* ==================== 生命周期 ==================== */

static void VXStyleSheetStyle_deinit(XStyleSheetStyle* self);

XVtable* XStyleSheetStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStyleSheetStyle)
    XVTABLE_INHERIT_XCLASS(XWindowsStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStyleSheetStyle_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawPrimitive,
                             VXStyleSheetStyle_drawPrimitive);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl,
                             VXStyleSheetStyle_drawControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawComplexControl,
                             VXStyleSheetStyle_drawComplexControl);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_SizeFromContents,
                             VXStyleSheetStyle_sizeFromContents);
    return XVTABLE_DEFAULT;
}

void XStyleSheetStyle_init(XStyleSheetStyle* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWindowsStyle_init(&self->m_base);
    XClassSetVtable(self, XStyleSheetStyle);
    XCssStyleSheet_init(&self->m_sheet);
    self->m_source = NULL;
}

XStyleSheetStyle* XStyleSheetStyle_create_ex(XMemoryType memory)
{
    XStyleSheetStyle* self = (XStyleSheetStyle*)XMemory_malloc(
        sizeof(*self), memory);
    if (!self) return NULL;
    XStyleSheetStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXStyleSheetStyle_deinit(XStyleSheetStyle* self)
{
    if (!self) return;
    xsss_sourceCacheClear(self); /* 控件源解析缓存（含其拥有的字符串）。 */
    XCssStyleSheet_clear(&self->m_sheet);
    if (self->m_sourceOwned && self->m_source) {
        XClassDelete(self->m_source);
        self->m_source = NULL;
        self->m_sourceOwned = false;
    }
    XClass_Deinit_Parent(XWindowsStyle, (XWindowsStyle*)self);
}

void XStyleSheetStyle_invalidateRenderCache(XStyleSheetStyle* self)
{
    if (!self) return;
    self->m_cacheValid = false;
    self->m_cacheObj = NULL;
    self->m_cacheRule = NULL;
    self->m_cacheSel = NULL;
    self->m_cacheWeight = 0;
}

bool XStyleSheetStyle_setStyleSheet(XStyleSheetStyle* self, const char* css)
{
    bool ok;
    if (!self) return false;
    ok = XCssStyleSheet_parse(&self->m_sheet, css);
    /* 应用级规则表变化 → 渲染规则缓存失效（控件级源解析缓存与本表
     * 互不牵连，不随本次失效）。 */
    XStyleSheetStyle_invalidateRenderCache(self);
    return ok;
}

void XStyleSheetStyle_setSourceStyle(XStyleSheetStyle* self, XStyle* source)
{
    if (!self) return;
    self->m_source = source;
    self->m_sourceOwned = false;
    /* 对标 QStyleSheetStyle：被包装样式即代理样式（QStyle::proxy）。 */
    ((XStyle*)self)->m_proxy = source;
}
void XStyleSheetStyle_setSourceStyle_move(XStyleSheetStyle* self, XStyle* source)
{
    if (!self) return;
    if (self->m_sourceOwned && self->m_source && self->m_source != source)
        XClassDelete(self->m_source);
    self->m_source = source;
    self->m_sourceOwned = (source != NULL);
    /* 对标 QStyleSheetStyle：被包装样式即代理样式（QStyle::proxy）。 */
    ((XStyle*)self)->m_proxy = source;
}

int XStyleSheetStyle_ruleCount(const XStyleSheetStyle* self)
{
    return self ? self->m_sheet.m_ruleCount : 0;
}

const XCssStyleRule* XStyleSheetStyle_styleRuleForPseudoElement(
    XStyleSheetStyle* self, const XWidget* widget, uint32_t state,
    const char* pseudoElement)
{
    if (!self || !widget) return NULL;
    /* 查询路径：即时全源扫描（不经渲染规则缓存，见头文件口径）；
     * 绘制端按子控件分派（对标 renderRule(w, opt, pseudoElement)）
     * deferred，本接口先服务查询/诊断与后续分派接入。 */
    return xsss_scanRules(self, (const XObject*)widget, state,
                          (pseudoElement && *pseudoElement) ? pseudoElement
                                                            : NULL,
                          NULL, NULL);
}

bool XStyleSheetStyle_hasStyleRuleForPseudoElement(
    XStyleSheetStyle* self, const XWidget* widget, uint32_t state,
    const char* pseudoElement)
{
    return XStyleSheetStyle_styleRuleForPseudoElement(self, widget, state,
                                                      pseudoElement) != NULL;
}

/* ==================== ===== G2 新增区 开始（lane-subcontrol 线专属，
   其他线勿碰；复杂控件子控件分派辅助函数集中于此） ==================== */

/* ---- 子控件 → Qt QSS 伪元素名映射与覆盖裁定（如实登记） ----
 *
 * 对标 Qt 6.8.3 QStyleSheetStyle::drawComplexControl 的
 * renderRule(w, opt, pseudoElement) 逐子控件查询。子控件名取 Qt QSS
 * 命名（qstylesheetstyle.cpp knownPseudoElements / Qt 文档
 * 「Style Sheet Syntax」子控件节；本会话经 code.qt.io 抓取 6.8.3
 * qstylesheetstyle.cpp 佐证的映射：ComboBox 的 drop-down 映射
 * SC_ComboBoxArrow、Slider 族 Groove/Handle/Tickmark、ScrollBar 族
 * Slider/AddPage/SubPage/AddLine/SubLine、SpinBox 族 Up/DownButton、
 * GroupBox 族 Title/Indicator）：
 *   SC_SpinBoxUp/Down        → "up-button" / "down-button"
 *   SC_ComboBoxArrow         → "drop-down"
 *   SC_ScrollBarSubLine/AddLine → "sub-line" / "add-line"
 *   SC_ScrollBarSubPage/AddPage → "sub-page" / "add-page"
 *   SC_ScrollBarSlider       → "handle"
 *   SC_SliderGroove/Handle   → "groove" / "handle"
 *   SC_SliderTickmarks       → "tickmark"
 *   SC_GroupBoxCheckBox/Label → "indicator" / "title"
 *
 * 不覆盖的 CC/SC（避免虚构，逐项登记）：
 * - CC_TitleBar：Qt 存在 PseudoElement_TitleBar* 但其名表字符串未能在
 *   本会话核实（网络抓取在 drawComplexControl 前截断），不凭记忆造名；
 * - CC_Dial：knownPseudoElements 无 Dial 伪元素（同上佐证）；
 * - CC_MdiControls：Mdi* 名表未核实，且底层分派器对该 CC 无实现
 *   （XCommonStyle.c VXCommonStyle_drawComplexControl default 分支）；
 * - CC_ToolButton：底层 xcs_drawToolButton 无独立菜单钮几何（箭头
 *   居中绘制，XCommonStyle.c:2010-2041），::menu-button 的 chrome
 *   无对应落位，画了即错位；
 * - SC_SpinBoxFrame/EditField、SC_ComboBoxFrame/EditField/
 *   ListBoxPopup、SC_GroupBoxFrame/Contents、SC_ScrollBarFirst/Last：
 *   Qt 不按伪元素绘制（frame/edit-field 属控件级盒，走本文件既有的
 *   控件级盒覆盖路径）；
 * - CC_Slider 的 sub-page/add-page：Qt 6.8 有 SliderSubPage/AddPage
 *   伪元素，但本库 SC 位集无对应位（XStyleOption.h:249-251 仅
 *   Groove/Handle/Tickmarks），无位可分派。
 *
 * 几何口径（如实登记为近似）：本库底层样式（XCommonStyle 各
 * xcs_draw*）按内联固定值绘制子控件，与 VXCommonStyle_subControlRect
 * 的公式不一致（如 xcs_drawSpinBox 按钮列恒 16px、xcs_drawScrollBar
 * 的 groove 从按钮区后起槽）；故子控件矩形逐 CC 镜像底层绘制的几何
 * 表达式（注释逐处标注出处行），仅 CC_TitleBar 例外（排版三方同源，
 * 可用 XStyle_subControlRect）——因无名表未启用，此例外暂无消费方。
 * m_subControls 位域不读：本库绝大多数复杂控件调用点传基类
 * XStyleOption（XScrollBar.c:164、XSlider.c:340、XSpinBox.c:510、
 * XComboBox.c:152、XGroupBox.c:166 均为 XStyleOption_init），该字段
 * 越界读取=栈垃圾（本函数区上方注释记载的真机事故同源）；子件存在
 * 性改按各 CC 底层绘制的实际条件（基类字段）判定。
 */

/** @brief 子控件候选（子控件位 + 对应 Qt QSS 伪元素/子控件名）。 */
typedef struct XSSSSubControlSpec
{
    int m_sc;           /**< 子控件位（XStyleSubControl 位标志）。 */
    const char* m_name; /**< 伪元素/子控件名（UTF-8，Qt QSS 命名）。 */
} XSSSSubControlSpec;

/** @brief 单控件子控件候选容量上限（各表最大 5 项，取整 8 防御）。 */
#define XSSS2_MAX_SUBS 8

/* 各 CC 候选表（覆盖裁定见区首注释；顺序即绘制覆盖序，后项叠前项）。 */
static const XSSSSubControlSpec xsss2_spinBoxSubs[] = {
    { XStyleSC_SpinBoxUp, "up-button" },
    { XStyleSC_SpinBoxDown, "down-button" },
};
static const XSSSSubControlSpec xsss2_comboBoxSubs[] = {
    { XStyleSC_ComboBoxArrow, "drop-down" },
};
static const XSSSSubControlSpec xsss2_scrollBarSubs[] = {
    { XStyleSC_ScrollBarSubLine, "sub-line" },
    { XStyleSC_ScrollBarAddLine, "add-line" },
    { XStyleSC_ScrollBarSubPage, "sub-page" },
    { XStyleSC_ScrollBarAddPage, "add-page" },
    { XStyleSC_ScrollBarSlider, "handle" },
};
static const XSSSSubControlSpec xsss2_sliderSubs[] = {
    { XStyleSC_SliderGroove, "groove" },
    { XStyleSC_SliderHandle, "handle" },
    { XStyleSC_SliderTickmarks, "tickmark" },
};
static const XSSSSubControlSpec xsss2_groupBoxSubs[] = {
    { XStyleSC_GroupBoxCheckBox, "indicator" },
    { XStyleSC_GroupBoxLabel, "title" },
};

/** @brief 取 CC 的子控件候选表（无映射的 CC 返回 NULL，调用侧跳过）。 */
static const XSSSSubControlSpec* xsss2_subControlSpecs(int cc, int* count)
{
    *count = 0;
    switch (cc) {
    case XStyleCC_SpinBox:
        *count = (int)(sizeof(xsss2_spinBoxSubs) /
                       sizeof(xsss2_spinBoxSubs[0]));
        return xsss2_spinBoxSubs;
    case XStyleCC_ComboBox:
        *count = (int)(sizeof(xsss2_comboBoxSubs) /
                       sizeof(xsss2_comboBoxSubs[0]));
        return xsss2_comboBoxSubs;
    case XStyleCC_ScrollBar:
        *count = (int)(sizeof(xsss2_scrollBarSubs) /
                       sizeof(xsss2_scrollBarSubs[0]));
        return xsss2_scrollBarSubs;
    case XStyleCC_Slider:
        *count = (int)(sizeof(xsss2_sliderSubs) /
                       sizeof(xsss2_sliderSubs[0]));
        return xsss2_sliderSubs;
    case XStyleCC_GroupBox:
        *count = (int)(sizeof(xsss2_groupBoxSubs) /
                       sizeof(xsss2_groupBoxSubs[0]));
        return xsss2_groupBoxSubs;
    default:
        /* CC_TitleBar/CC_Dial/CC_MdiControls/CC_ToolButton：见区首裁定。 */
        return NULL;
    }
}

/** @brief 子件存在性判定（对齐底层绘制的实际条件，基类字段）。
 *
 *  各分支条件与 XCommonStyle 的 xcs_draw* 一致（出处逐行标注）；
 *  未列出的 sc 恒存在（底层无条件绘制）。 */
static bool xsss2_subControlPresent(int cc, const XStyleOption* option,
                                    int sc)
{
    switch (cc) {
    case XStyleCC_SpinBox:
        /* NoButtons（m_spinSymbols==2）不画按钮
         * （XCommonStyle.c:1245 xcs_drawSpinBox）。 */
        if (sc == XStyleSC_SpinBoxUp || sc == XStyleSC_SpinBoxDown)
            return option->m_spinSymbols != 2;
        return true;
    case XStyleCC_ComboBox:
        /* 箭头区只在宽度容纳时画（XCommonStyle.c:2432
         * r.width > arrowW+4，arrowW=16）。 */
        if (sc == XStyleSC_ComboBoxArrow)
            return option->m_rect.width > 16 + 4;
        return true;
    case XStyleCC_ScrollBar:
        /* 两端步进按钮按选项开关（XCommonStyle.c:1934
         * m_scrollSubLine/m_scrollAddLine）。 */
        if (sc == XStyleSC_ScrollBarSubLine) return option->m_scrollSubLine;
        if (sc == XStyleSC_ScrollBarAddLine) return option->m_scrollAddLine;
        return true;
    case XStyleCC_Slider:
        /* 刻度按刻度位置开关（XCommonStyle.c:1676
         * m_sliderTickPosition != 0）。 */
        if (sc == XStyleSC_SliderTickmarks)
            return option->m_sliderTickPosition != 0;
        return true;
    case XStyleCC_GroupBox:
        /* 勾选框按 m_checkable、标题按 m_text（XCommonStyle.c:1509/
         * 1516 xcs_drawGroupBox）。 */
        if (sc == XStyleSC_GroupBoxCheckBox) return option->m_checkable;
        if (sc == XStyleSC_GroupBoxLabel)
            return option->m_text && option->m_text[0] != '\0';
        return true;
    default:
        return true;
    }
}

/** @brief 子控件矩形（逐 CC 镜像底层绘制的几何表达式；近似口径见区首）。
 *
 *  @return false = 底层该 CC 早退不绘（如矩形过小）或子件无几何。 */
static bool xsss2_subControlRect(int cc, const XStyleOption* option, int sc,
                                 XRect* out)
{
    const XRect* r = &option->m_rect;
    switch (cc) {
    case XStyleCC_SpinBox: {
        /* xcs_drawSpinBox（XCommonStyle.c:1174-1180,1245-1260）：
         * 按钮列恒 16px 右缘贴齐、上下各半。 */
        int bh;
        int bx;
        if (r->width <= 2 || r->height <= 2) return false;
        bh = r->height / 2;
        bx = r->x + r->width - 16;
        if (sc == XStyleSC_SpinBoxUp)
            XRect_init(out, bx, r->y, 16, bh);
        else if (sc == XStyleSC_SpinBoxDown)
            XRect_init(out, bx, r->y + bh, 16, bh);
        else
            return false;
        return true;
    }
    case XStyleCC_ComboBox: {
        /* xcs_drawComboBox（XCommonStyle.c:2433-2435）：右端 16px 列。 */
        if (r->width <= 2 || r->height <= 2) return false;
        if (sc != XStyleSC_ComboBoxArrow) return false;
        XRect_init(out, r->x + r->width - 16, r->y, 16, r->height);
        return true;
    }
    case XStyleCC_ScrollBar: {
        /* xcs_drawScrollBar（XCommonStyle.c:1825-1849 groove、1889-1893
         * slider、1936-1944 两端按钮；handleLen/handlePos 同源整数式）：
         * groove=居中 8px 带（有按钮时扣两端 16px）、slider 在 groove
         * 内按值游走、页面=groove 减 slider 的两段。 */
        bool horizontal = option->m_horizontal;
        int btn = (option->m_scrollSubLine || option->m_scrollAddLine)
                      ? 16 : 0;
        int contentLen = horizontal ? r->width : r->height;
        int range = option->m_sliderMax - option->m_sliderMin;
        int page = option->m_sliderPageStep;
        int handleLen;
        int travel;
        int handlePos;
        XRect groove;
        int sliderStart;
        if (r->width <= 2 || r->height <= 2) return false;
        if (horizontal)
            XRect_init(&groove, btn, (r->height - 8) / 2,
                       r->width - btn * 2, 8);
        else
            XRect_init(&groove, (r->width - 8) / 2, btn,
                       8, r->height - btn * 2);
        if (range <= 0) {
            handleLen = contentLen;
        } else {
            handleLen = contentLen * page / (range + page);
            if (handleLen < 16) handleLen = 16;
            if (handleLen > contentLen) handleLen = contentLen;
        }
        travel = contentLen - handleLen;
        handlePos = (range > 0 && travel > 0)
            ? (option->m_sliderValue - option->m_sliderMin) * travel / range
            : 0;
        sliderStart = (horizontal ? groove.x : groove.y) + handlePos;
        switch (sc) {
        case XStyleSC_ScrollBarSubLine:
            if (horizontal) XRect_init(out, r->x, r->y, 16, r->height);
            else XRect_init(out, r->x, r->y, r->width, 16);
            return true;
        case XStyleSC_ScrollBarAddLine:
            if (horizontal)
                XRect_init(out, r->x + r->width - 16, r->y, 16, r->height);
            else
                XRect_init(out, r->x, r->y + r->height - 16,
                           r->width, 16);
            return true;
        case XStyleSC_ScrollBarGroove:
            *out = groove;
            return true;
        case XStyleSC_ScrollBarSlider:
            if (horizontal)
                XRect_init(out, sliderStart, groove.y, handleLen,
                           groove.height);
            else
                XRect_init(out, groove.x, sliderStart, groove.width,
                           handleLen);
            return true;
        case XStyleSC_ScrollBarSubPage:
            if (horizontal)
                XRect_init(out, groove.x, groove.y,
                           sliderStart - groove.x, groove.height);
            else
                XRect_init(out, groove.x, groove.y, groove.width,
                           sliderStart - groove.y);
            return true;
        case XStyleSC_ScrollBarAddPage: {
            int end = (horizontal ? groove.x + groove.width
                                  : groove.y + groove.height);
            if (horizontal)
                XRect_init(out, sliderStart + handleLen, groove.y,
                           end - (sliderStart + handleLen), groove.height);
            else
                XRect_init(out, groove.x, sliderStart + handleLen,
                           groove.width, end - (sliderStart + handleLen));
            return true;
        }
        default:
            return false;
        }
    }
    case XStyleCC_Slider: {
        /* xcs_drawSlider（XCommonStyle.c:1617-1637）：groove=居中 5px
         * 带、handle 恒 16px 按值游走、刻度铺整条（排除后成刻度带）。 */
        bool horizontal = option->m_horizontal;
        int range = option->m_sliderMax - option->m_sliderMin;
        double frac = (range > 0)
            ? (double)(option->m_sliderValue - option->m_sliderMin) /
                  (double)range
            : 0.0;
        if (r->width <= 2 || r->height <= 2) return false;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        if (sc == XStyleSC_SliderGroove) {
            if (horizontal)
                XRect_init(out, r->x + 1, r->y + (r->height - 5) / 2,
                           r->width - 2, 5);
            else
                XRect_init(out, r->x + (r->width - 5) / 2, r->y + 1,
                           5, r->height - 2);
            return true;
        }
        if (sc == XStyleSC_SliderHandle) {
            if (horizontal) {
                int avail = r->width - 16;
                int handlePos = 8 + (int)(avail * frac);
                XRect_init(out, handlePos - 8, r->y + 1, 16, r->height - 2);
            } else {
                int avail = r->height - 16;
                int handlePos = 8 + (int)(avail * (1.0 - frac));
                XRect_init(out, r->x + 1, handlePos - 8, r->width - 2, 16);
            }
            return true;
        }
        if (sc == XStyleSC_SliderTickmarks) {
            *out = *r; /* 刻度带=整矩形，排除 groove/handle 后成上下条带。 */
            return true;
        }
        return false;
    }
    case XStyleCC_GroupBox: {
        /* xcs_drawGroupBox（XCommonStyle.c:1509-1520）：titleH=16、
         * 勾选框 (x+6, y+1, 13, 13)、标题文本区随勾选框右移 16px、
         * 文本宽=XStrlen 字节数（与底层绘制的既有口径一致）。 */
        int titleH = 16;
        if (r->width <= 2 || r->height <= 2) return false;
        if (sc == XStyleSC_GroupBoxCheckBox) {
            XRect_init(out, r->x + 6, r->y + (titleH - 13) / 2, 13, 13);
            return true;
        }
        if (sc == XStyleSC_GroupBoxLabel) {
            if (!option->m_text) return false;
            XRect_init(out,
                       r->x + 6 + (option->m_checkable ? 16 : 0), r->y,
                       (int)XStrlen(option->m_text), titleH);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

/** @brief 按规则取声明值文本（复用声明读取与解析设施；失败返回 NULL）。 */
static const char* xsss2_ruleDeclText(const XCssStyleRule* rule,
                                      XCssProperty id, char* buf,
                                      size_t cap)
{
    const XCssDeclaration* d = xsss_findDecl(rule, id);
    if (!d || !d->m_value) return NULL;
    return xsss_declText(d, buf, cap);
}

/** @brief 按规则绘制背景（QRenderRule 语义子集：纯色背景，透明跳过；
 *         逐语义镜像并行线的 xsss_applyBackground，数据源改给定的
 *         子控件规则）。 */
static void xsss2_ruleBackground(const XCssStyleRule* rule,
                                 XPainter* painter, const XRect* rect)
{
    uint32_t color;
    char buf[256];
    const char* text;
    text = xsss2_ruleDeclText(rule, XCssProperty_BackgroundColor, buf,
                              sizeof(buf));
    if (!text)
        text = xsss2_ruleDeclText(rule, XCssProperty_Background, buf,
                                  sizeof(buf));
    if (!text || !*text) return;
    if (!XCssParseColor(text, &color)) return;
    if ((color & 0xFF000000u) == 0) return; /* 全透明 → 不填充。 */
    XPainter_fillRect(painter, rect, color);
}

/** @brief 按规则绘制边框（QRenderRule 语义子集：width/color/style/
 *         radius，逐语义镜像并行线的 xsss_drawBorder，数据源改给定的
 *         子控件规则；无 width 不画、style none 不画、全透明色不画）。 */
static void xsss2_ruleBorder(const XCssStyleRule* rule,
                             const XStyleOption* option,
                             XPainter* painter, const XRect* rect,
                             const XCssLengthContext* ctx)
{
    const char* text;
    char buf[256];
    int width = 0;
    bool hasWidth = false;
    bool hasColor = false;
    uint32_t color = 0;
    int radius = 0;
    int style = 0; /* 0 solid / 1 dashed / 2 dotted / -1 none。 */
    XRect r = *rect;
    /* 宽度：BorderWidth 单声明，缺省回落 Border 简写；逐 token 取
     * 首个可解析长度分量（镜像 xsss_drawBorder 口径）。 */
    text = xsss2_ruleDeclText(rule, XCssProperty_BorderWidth, buf,
                              sizeof(buf));
    if (!text) text = xsss2_ruleDeclText(rule, XCssProperty_Border, buf,
                                         sizeof(buf));
    while (text && *text && !hasWidth) {
        char tok[64];
        size_t tl = 0;
        XCssLength len;
        while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
               tl + 1 < sizeof(tok))
            ++tl;
        XMemcpy(tok, text, tl);
        tok[tl] = '\0';
        text += tl;
        while (*text && XIsSpace((unsigned char)*text)) ++text;
        if (XCssParseLengthEx(tok, &len)) {
            width = xsss_lengthPx(&len, ctx, true);
            hasWidth = true;
        }
    }
    /* 颜色：BorderColor 单声明。 */
    text = xsss2_ruleDeclText(rule, XCssProperty_BorderColor, buf,
                              sizeof(buf));
    if (text && *text && XCssParseColor(text, &color)) hasColor = true;
    /* 风格：BorderStyle 单声明（none 整体不画）。 */
    text = xsss2_ruleDeclText(rule, XCssProperty_BorderStyle, buf,
                              sizeof(buf));
    if (text && *text) {
        if (XStrcasecmp(text, "none") == 0) return;
        if (XStrcasecmp(text, "dashed") == 0) style = 1;
        else if (XStrcasecmp(text, "dotted") == 0) style = 2;
    }
    /* Border 简写扫描：颜色 token 与风格关键字（镜像 xsss_drawBorder）。 */
    text = xsss2_ruleDeclText(rule, XCssProperty_Border, buf, sizeof(buf));
    while (text && *text) {
        char tok[64];
        size_t tl = 0;
        uint32_t token = 0;
        while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
               tl + 1 < sizeof(tok))
            ++tl;
        XMemcpy(tok, text, tl);
        tok[tl] = '\0';
        text += tl;
        while (*text && XIsSpace((unsigned char)*text)) ++text;
        if (!hasColor && XCssParseColorToken(tok, tl, &token)) {
            color = token;
            hasColor = true;
        } else if (XStrcasecmp(tok, "solid") == 0) {
            style = 0;
        } else if (XStrcasecmp(tok, "dashed") == 0) {
            style = 1;
        } else if (XStrcasecmp(tok, "dotted") == 0) {
            style = 2;
        } else if (XStrcasecmp(tok, "none") == 0) {
            return;
        }
    }
    if (!hasWidth || width <= 0) return;
    if (hasColor && (color & 0xFF000000u) == 0)
        return; /* 全透明边框不产生像素（对标透明画笔）。 */
    if (!hasColor) {
        /* CSS 语义回落 currentColor：本规则 color 声明 → 选项文本色
         * → 不透明白（镜像 xsss_drawBorder 回落链，色源限本规则）。 */
        text = xsss2_ruleDeclText(rule, XCssProperty_Color, buf,
                                  sizeof(buf));
        if (text && *text && XCssParseColor(text, &color))
            hasColor = true;
        else if (option->m_textColor) {
            color = option->m_textColor;
            hasColor = true;
        } else {
            color = 0xFF000000u;
        }
    }
    XPainter_setPen(painter, color);
    if (width >= 2) {
        r.x += width / 2;
        r.y += width / 2;
        r.width -= width;
        r.height -= width;
    }
    /* 圆角：BorderRadius 带上下文换算（镜像 xsss_drawBorder）。 */
    text = xsss2_ruleDeclText(rule, XCssProperty_BorderRadius, buf,
                              sizeof(buf));
    if (text && *text) {
        XCssLength len;
        if (XCssParseLengthEx(text, &len))
            radius = xsss_lengthPx(&len, ctx, true);
    }
#if XPAINTER_SHAPE_ON
    if (radius > 0) {
        XPainter_drawRoundedRect(painter, &r, radius, radius);
        return;
    }
#endif
    if (style == 1 || style == 2) {
        /* 虚线/点线：四边分段绘制（4px 段+4px 空 / 2px 点+4px 空）。 */
        int seg = (style == 1) ? 4 : 2;
        int gap = 4;
        int i;
        int x0 = r.x;
        int y0 = r.y;
        int x1 = r.x + r.width - 1;
        int y1 = r.y + r.height - 1;
        for (i = 0; i <= x1 - x0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > x1 - x0) e = x1 - x0;
            XPainter_drawLine(painter, x0 + i, y0, x0 + e, y0);
            XPainter_drawLine(painter, x0 + i, y1, x0 + e, y1);
        }
        for (i = 0; i <= y1 - y0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > y1 - y0) e = y1 - y0;
            XPainter_drawLine(painter, x0, y0 + i, x0, y0 + e);
            XPainter_drawLine(painter, x1, y0 + i, x1, y0 + e);
        }
        return;
    }
    /* 直角边框：drawLine 四边（线宽 1px/次，width>1 时多层内缩）。 */
    {
        int i;
        for (i = 0; i < width; ++i) {
            XPainter_drawLine(painter, r.x + i, r.y + i,
                              r.x + r.width - 1 - i, r.y + i);
            XPainter_drawLine(painter, r.x + r.width - 1 - i, r.y + i,
                              r.x + r.width - 1 - i,
                              r.y + r.height - 1 - i);
            XPainter_drawLine(painter, r.x + r.width - 1 - i,
                              r.y + r.height - 1 - i, r.x + i,
                              r.y + r.height - 1 - i);
            XPainter_drawLine(painter, r.x + i, r.y + r.height - 1 - i,
                              r.x + i, r.y + i);
        }
    }
}

/** @brief 按规则绘制子件 chrome（背景 → 边框；盒模型几何不在本批，
 *         仅背景色/边框/圆角——任务口径）。 */
static void xsss2_drawRuleChrome(const XCssStyleRule* rule,
                                 const XStyleOption* option,
                                 XPainter* painter, const XRect* rect,
                                 const XCssLengthContext* ctx)
{
    xsss2_ruleBackground(rule, painter, rect);
    xsss2_ruleBorder(rule, option, painter, rect, ctx);
}

/** @brief 裁剪到「子件矩形扣除同控件其余子件矩形」的区域。
 *
 *  语义：命中规则的子件 chrome 只落自己的矩形；未命中子件的底层
 *  外观不被覆盖（「未命中的子件整体转发底层样式」的绘制域保障，
 *  如 slider::groove 规则不得抹掉底层 handle）。
 *  @return 区域为空（完全被兄弟覆盖）返回 false，调用侧跳过绘制。
 */
static bool xsss2_clipExclude(XPainter* painter, const XRect* rect,
                              const XRect* siblings, int count, int skip)
{
    XRegion region;
    XRegion sib;
    XRegion tmp;
    bool empty = true;
    int i;
    XRegion_init(&region);
    XRegion_addRect(&region, rect);
    for (i = 0; i < count; ++i) {
        if (i == skip) continue;
        if (XRegion_isEmpty(&region)) break;
        XRegion_init(&sib);
        XRegion_addRect(&sib, &siblings[i]);
        XRegion_init(&tmp);
        XRegion_subtracted(&region, &sib, &tmp);
        XRegion_copy(&tmp, &region);
        XRegion_deinit(&tmp);
        XRegion_deinit(&sib);
    }
    empty = XRegion_isEmpty(&region);
    if (!empty)
        XPainter_setClipRegion(painter, &region,
                               XPainterClipOperation_IntersectClip);
    XRegion_deinit(&region);
    return !empty;
}

/** @brief 复杂控件子控件分派（对标 QStyleSheetStyle::drawComplexControl
 *         的 renderRule(w, opt, pseudoElement) 逐子控件查询）。
 *
 *  流程：候选表 → 存在性（基类字段条件）→ 矩形（镜像底层几何）→
 *  规则查询（xsss_scanRules 按伪元素名，级联口径与属性查询一致）→
 *  命中者按 QRenderRule 语义（背景/边框/圆角）绘制，裁剪排除同控件
 *  其余子件矩形。无规则（m_sheet 空）或全不命中：零附加绘制，行为
 *  与转发底层样式逐字节一致。 */
static void xsss2_dispatchSubControls(XStyleSheetStyle* self, int cc,
                                      const XStyleOption* option,
                                      XPainter* painter,
                                      const XWidget* widget)
{
    const XSSSSubControlSpec* specs;
    XRect rects[XSSS2_MAX_SUBS];
    const XCssStyleRule* rules[XSSS2_MAX_SUBS];
    int specCount = 0;
    int count = 0;
    int i;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return; /* 无规则：零行为差。 */
    specs = xsss2_subControlSpecs(cc, &specCount);
    if (!specs || specCount <= 0) return;
    if (specCount > XSSS2_MAX_SUBS) specCount = XSSS2_MAX_SUBS;
    /* 首趟：存在判定 + 几何近似 + 规则查询（未命中也收集矩形——
     * 兄弟排除需要全部在绘子件的矩形）。 */
    for (i = 0; i < specCount; ++i) {
        XRect r;
        if (!xsss2_subControlPresent(cc, option, specs[i].m_sc)) continue;
        if (!xsss2_subControlRect(cc, option, specs[i].m_sc, &r)) continue;
        rects[count] = r;
        rules[count] = xsss_scanRules(self, (const XObject*)widget,
                                      option->m_state, specs[i].m_name,
                                      NULL, NULL);
        ++count;
    }
    /* 二趟：命中规则的子件绘制 chrome（背景/边框/圆角），painter
     * 状态（含裁剪）save/restore 包裹，不外泄。 */
    for (i = 0; i < count; ++i) {
        XCssLengthContext ctx;
        if (!rules[i]) continue;
        ctx = xsss_lengthContext(painter, &rects[i]);
        if (!XPainter_save(painter)) continue;
        if (xsss2_clipExclude(painter, &rects[i], rects, count, i))
            xsss2_drawRuleChrome(rules[i], option, painter, &rects[i],
                                 &ctx);
        XPainter_restore(painter);
    }
}

/* ==================== ===== G2 新增区 结束 ==================== */

/* ==================== ===== G4 新增区 开始（lane-palette-text 线专属，
 * 其他线勿动） ==================== */

/* ---- G4 ① 扩展取色链：XCssParseColor → palette(角色) → 渐变 ----
 *
 * 【渐变消费衔接点（交付 A 线=image-box 线）】本线已把渐变值解析为
 * 类型化 XCssGradient 并送达调用侧；栅格化消费（XImage 缓存绘制，
 * 性能红线=装饰尺寸变化才重栅格化）是 A 线名下职责。A 线并入时的
 * 唯一接线点：xsss_applyBackground（A 线名下函数）内
 * 「if (!XCssParseColor(text, &color)) return;」改调
 * xsss4_resolveColorValue(text, option, &color, &isGradient, &g)，
 * isGradient==true 分支按 (尺寸,值) 单槽缓存把 g 栅格化后填充 boxRect。
 * 同 TU 内 static 函数直接可见，无需头文件改动。A 线并入前该值维持
 * 原行为（未识别→不绘制），零回归。 */

/** @brief palette 角色取色（角色枚举 → ARGB；0=取色无效）。
 *
 *  调色板两源序：选项调色板 option->m_palette（XStyleOption.h:611
 *  「按需填充」；控件级 prep 位点已按 XWidget_palette 折入应用级，
 *  如 XRubberBand.c:65 opt.m_palette = XWidget_palette(...)）→ 应用
 *  调色板 XGuiApplication_palette()（对标 QGuiApplication::palette
 *  兜底）。组口径与既有 xcs_color（XCommonStyle.c:144）一致取
 *  Current（读写映射 Active）。XPALETTE_ON=0 时恒 0（解析侧
 *  XCssParsePaletteRole 同门控恒 false，本函数成死分支仍保编译）。 */
static uint32_t xsss4_paletteRoleColor(const XStyleOption* option, int role)
{
#if XPALETTE_ON
    XColor c;
    uint32_t argb;
    if (!option) return 0;
    c = XPalette_color((XPalette*)&option->m_palette,
                       XPaletteColorGroup_Current, (XPaletteColorRole)role);
    argb = XColor_rgba(&c);
    if (argb != 0) return argb;
#if XGUIAPPLICATION_ON
    /* 选项未携带调色板（按需填充未覆盖的 prep 位点）→ 应用级回落。 */
    {
        XPalette app = XGuiApplication_palette();
        c = XPalette_color(&app, XPaletteColorGroup_Current,
                           (XPaletteColorRole)role);
        argb = XColor_rgba(&c);
        if (argb != 0) return argb;
    }
#endif /* XGUIAPPLICATION_ON */
    return 0;
#else
    (void)option;
    (void)role;
    return 0;
#endif /* XPALETTE_ON */
}

/** @brief 扩展取色链（XCssParseColor 消费侧扩展点）。
 *
 *  对标 Qt 6.8.3 qcssparser.cpp parseColorValue 的 PlainColor→Palette
 *  →Gradient 三态序，整串受理不留半截值：
 *  - XCssParseColor 命中 → 纯色 *outColor；
 *  - palette(角色名) 命中 → 经 xsss4_paletteRoleColor 解析为纯色；
 *    两源调色板均取色无效（NoRole/未填充，rgba()==0）按未命中处理；
 *  - 渐变函数整串解析命中 → *outGradient 且 *outIsGradient=true
 *    （值送达语义，栅格化消费衔接点见本区头注）。
 *  全未命中返回 false 且不写任何输出（同 XCssParseColor 族口径）。 */
static bool xsss4_resolveColorValue(const char* value,
                                    const XStyleOption* option,
                                    uint32_t* outColor, bool* outIsGradient,
                                    XCssGradient* outGradient)
{
    uint32_t color = 0;
    int role = 0;
    XCssGradient gradient;
    if (!value || !*value || !outColor || !outIsGradient || !outGradient)
        return false;
    if (XCssParseColor(value, &color)) {
        *outColor = color;
        *outIsGradient = false;
        return true;
    }
    if (XCssParsePaletteRole(value, &role)) {
        color = xsss4_paletteRoleColor(option, role);
        if (color == 0) return false;
        *outColor = color;
        *outIsGradient = false;
        return true;
    }
    if (XCssParseGradient(value, &gradient)) {
        *outGradient = gradient;
        *outIsGradient = true;
        return true;
    }
    return false;
}

/* ---- G4 ② 文本属性消费 ---- */

/** @brief font-variant 应用（normal/small-caps → 大小写旗标；对标
 *         setTextVariantFromValue → QFont::setCapitalization）。
 *
 *  能力边界：XFont.m_capitalization 当前渲染链零消费（全库引用仅
 *  XFont.c/.h 的存储/拷贝/比较，Src/XData/XFont/ 之外无读者，字形
 *  绘制管线 XPainter.c painterDrawCodepoint 无大小写分支）——旗标
 *  按 Qt 口径落画家字体，可见效果待字体渲染器支持，登记 XGui.md
 *  不硬造。 */
static bool xsss4_applyFontVariant(XFont* font, const char* text)
{
    if (!font || !text) return false;
    if (XStrcasecmp(text, "small-caps") == 0) {
        XFont_setCapitalization(font, XFont_SmallCaps);
        return true;
    }
    if (XStrcasecmp(text, "normal") == 0) {
        XFont_setCapitalization(font, XFont_MixedCase);
        return true;
    }
    return false;
}

/** @brief letter-spacing / word-spacing 长度值应用（common）。
 *
 *  对标 setLetterSpacingFromValue / setWordSpacingFromValue：normal
 *  → 间距清零；<length> → 绝对像素间距（px 直取、pt 经 xsss_ptToPx、
 *  em/ex 按画家字号参考——与 xsss_applyFontSize 同参考系；% 非 CSS
 *  间距语法，拒绝）。XFont 间距为绝对像素口径（XFont_SpacingType，
 *  无 word 间距类型字段）。能力边界：m_letterSpacing/m_wordSpacing
 *  两字段渲染链零消费（XPainter 字形 advance 链
 *  painterDrawCodepoint/painterDrawTextRun 无间距钩点）——值落盘、
 *  可见效果登记 XGui.md。 */
static bool xsss4_applySpacingLength(XFont* font, const char* text,
                                     const XCssLengthContext* ctx,
                                     bool isLetter)
{
    XCssLength len;
    float px;
    if (!font || !text || !*text) return false;
    if (XStrcasecmp(text, "normal") == 0) {
        if (isLetter) {
            XFont_setLetterSpacingType(font, XFont_AbsoluteSpacing);
            XFont_setLetterSpacing(font, 0.0f);
        } else {
            XFont_setWordSpacing(font, 0.0f);
        }
        return true;
    }
    if (!XCssParseLengthEx(text, &len)) return false;
    switch (len.m_unit) {
    case XCssLengthUnit_Px:
        px = (float)len.m_value;
        break;
    case XCssLengthUnit_Pt:
        px = (float)xsss_ptToPx(len.m_value);
        break;
    case XCssLengthUnit_Em:
    case XCssLengthUnit_Ex:
        if (!ctx || ctx->fontPixelSize <= 0) return false;
        px = (float)(len.m_value * (double)ctx->fontPixelSize);
        if (len.m_unit == XCssLengthUnit_Ex) px /= 2.0f;
        break;
    default:
        return false;
    }
    if (isLetter) {
        XFont_setLetterSpacingType(font, XFont_AbsoluteSpacing);
        XFont_setLetterSpacing(font, px);
    } else {
        XFont_setWordSpacing(font, px);
    }
    return true;
}

/** @brief text-transform 模式（对标 qcssparser 值域四关键字：
 *         uppercase/lowercase/capitalize/none；未知值忽略）。 */
typedef enum XCssTextTransform
{
    XCssTextTransform_None = 0,   /**< none：不变换（MixedCase）。 */
    XCssTextTransform_Upper,      /**< uppercase：全大写。 */
    XCssTextTransform_Lower,      /**< lowercase：全小写。 */
    XCssTextTransform_Capitalize  /**< capitalize：词首大写。 */
} XCssTextTransform;

/** @brief text-transform 值关键字 → 模式。 */
static bool xsss4_textTransformMode(const char* text, XCssTextTransform* out)
{
    if (!text || !out) return false;
    if (XStrcasecmp(text, "uppercase") == 0) {
        *out = XCssTextTransform_Upper;
        return true;
    }
    if (XStrcasecmp(text, "lowercase") == 0) {
        *out = XCssTextTransform_Lower;
        return true;
    }
    if (XStrcasecmp(text, "capitalize") == 0) {
        *out = XCssTextTransform_Capitalize;
        return true;
    }
    if (XStrcasecmp(text, "none") == 0) {
        *out = XCssTextTransform_None;
        return true;
    }
    return false;
}

/** @brief 变换文本暂存环（进程级单例；绘制主线程顺序消费）。
 *
 *  option->m_text 为借用指针（XStyleOption.h:614）不可就地改写，而
 *  变换结果须存活到源样式绘制返回之后——落环不落单发堆（免逐帧分
 *  配、免所有权移交）。环深 4 覆盖同帧嵌套分派（实践深度 ≤2，如组
 *  合框弹层）；更深嵌套同帧并存 >4 份变换文本时最早槽被回收复用
 *  （能力边界，登记 XGui.md）。LSan 口径：BSS 静态对象退出时仍可达，
 *  不计漏。 */
static const char* xsss4_transformScratch(const char* utf8, size_t len)
{
    static XString ring[4];
    static int slot = 0;
    static bool seeded = false;
    if (!seeded) {
        int i;
        for (i = 0; i < 4; ++i) XString_init(&ring[i]);
        seeded = true;
    }
    slot = (slot + 1) & 3;
    if (!XString_assign_with_length_utf8(&ring[slot], utf8, len))
        return NULL;
    return XString_toUtf8(&ring[slot]);
}

/** @brief UTF-8 文本 text-transform（码点级映射；失败返回 NULL）。
 *
 *  通道：XChar_fromUtf8Stream 解码 → 逐单元映射 → XChar_toUtf8Stream
 *  回编码（代理对双向安全：XChar.c:905-911 解码侧、:927-938 编码侧
 *  实证）。口径：
 *  - 大小写映射逐单元（QChar::toUpper/toLower 对齐），无 QString
 *    toUpper 的 1:N 膨胀（ß→SS 类），输出字节数不膨胀 → 绘制端字节
 *    宽度计量（XCommonStyle textW=XStrlen(m_text)）无位移副作用；
 *  - 增补平面（代理对单元）无单单元映射，原样保留（登记 XGui.md）；
 *  - capitalize 空白分词近似（XChar_isSpace，含 U+00A0/U+3000）：
 *    词首单元大写、其余不动（QFont::Capitalize 文档口径「每词首字
 *    母大写」；Qt 用 Unicode 分词，此处为登记近似）。 */
static const char* xsss4_transformUtf8(const char* src,
                                       XCssTextTransform mode)
{
    size_t len;
    XChar* chs = NULL;
    int64_t count;
    int64_t i;
    int64_t bytes;
    char* out;
    const char* result;
    bool atWordStart = true;
    if (!src) return NULL;
    len = XStrlen(src);
    if (len == 0) return NULL;
    /* 容量：XChar 单元数 ≤ UTF-8 字节数（4 字节序列产 2 单元），
     * +1 供终止符（fromUtf8Stream 终止符必须放得下，否则整串拒绝）。 */
    chs = (XChar*)XMalloc_System((len + 1) * sizeof(XChar));
    if (!chs) return NULL;
    count = XChar_fromUtf8Stream((const uint8_t*)src, len, chs, len + 1);
    if (count <= 0) {
        XFree_System(chs);
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        XChar ch = chs[i];
        if (XChar_isHighSurrogate(ch) || XChar_isLowSurrogate(ch)) {
            atWordStart = false; /* 增补平面占位：不映射、不切词。 */
            continue;
        }
        if (mode == XCssTextTransform_Upper) {
            chs[i] = XChar_toUpper(ch);
        } else if (mode == XCssTextTransform_Lower) {
            chs[i] = XChar_toLower(ch);
        } else if (mode == XCssTextTransform_Capitalize) {
            if (XChar_isSpace(ch)) {
                atWordStart = true;
            } else if (atWordStart) {
                chs[i] = XChar_toUpper(ch);
                atWordStart = false;
            }
        }
    }
    bytes = XChar_toUtf8Stream(chs, count, NULL, 0);
    if (bytes < 0) {
        XFree_System(chs);
        return NULL;
    }
    out = (char*)XMalloc_System((size_t)bytes + 1);
    if (!out) {
        XFree_System(chs);
        return NULL;
    }
    if (XChar_toUtf8Stream(chs, count, (uint8_t*)out, (size_t)bytes + 1) <
        0) {
        XFree_System(chs);
        XFree_System(out);
        return NULL;
    }
    XFree_System(chs);
    result = xsss4_transformScratch(out, (size_t)bytes);
    XFree_System(out);
    return result;
}

/** @brief 应用 text-transform（绘制前：变换 option 文本副本）。
 *
 *  纯字符串变换（任务口径），消费面=option->m_text 通道：底层样式
 *  绘制按钮标签/页签/复选框/表项等文本均读该字段（XCommonStyle.c
 *  :544/:594/:1578 等）。不接入面（登记 XGui.md）：
 *  - drawComplexControl 复杂控件路径（与 m_textColor 同口径——切片
 *    拷贝风险，见该函数注释）；
 *  - 尺寸提示链 sizeFromContents（Qt 经 QFontMetrics 感知
 *    capitalization，本库尺寸链不感知变换——紧布局下变换文本可能
 *    溢出原生尺寸，登记为已知偏差）；
 *  - 行编辑/文本编辑等控件自绘文本不经 option->m_text 通道。 */
static void xsss_applyTextTransform(XStyleSheetStyle* self,
                                    const XObject* obj,
                                    XStyleOption* option)
{
    const XCssDeclaration* d;
    XCssTextTransform mode;
    char buf[256];
    const char* text;
    const char* transformed;
    if (!self || !option) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    if (!option->m_text || !option->m_text[0]) return;
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_TextTransform);
    if (!d || !d->m_value) return;
    text = xsss_declText(d, buf, sizeof(buf));
    if (!text || !*text) return;
    if (!xsss4_textTransformMode(text, &mode)) return;
    if (mode == XCssTextTransform_None) return;
    transformed = xsss4_transformUtf8(option->m_text, mode);
    if (!transformed) return;
    option->m_text = transformed;
}

/* ==================== ===== G4 新增区 结束 ==================== */

/* ==================== ===== G3 新增区 开始（lane-image-box 线专属，
   其他线勿碰；背景图像/渐变栅格化/border-image 九宫格/outline 族/
   image-icon 属性消费辅助函数集中于此） ==================== */

/** @brief 图像装载单槽缓存。
 *
 *  键=url 路径内容等值（性能红线：禁止每帧装载磁盘图像——命中同键
 *  直接复用 XImage；装载失败也记账（m_failed），避免失败路径逐帧重
 *  试磁盘）。槽内 XImage 懒创建、进程期持有（内容键控，样式表更换
 *  后同键图像内容恒等，无需失效钩子；静态可达不被 LSan 计泄漏，同
 *  单例语义）。绘制管线单线程（主线程渲染），无锁。 */
typedef struct XSSS3ImageSlot
{
    bool m_keySet;      /**< m_key 是否有效。 */
    bool m_failed;      /**< 当前键上次装载失败（负缓存）。 */
    char m_key[XSSS3_URL_CAP]; /**< 缓存键（url 路径）。 */
    XImage* m_image;    /**< 缓存图像（槽拥有；懒创建）。 */
} XSSS3ImageSlot;

/** @brief 渐变栅格化单槽缓存：键=(盒宽, 盒高, 声明值文本)。
 *
 *  性能红线：装饰尺寸变化才重栅格化——尺寸或值文本任一变化即换键
 *  重算，命中则零重算直接 drawImage（对标 QStyleSheetStyle 装饰缓
 *  存的单槽近似；多控件同值同尺寸共享同一栅格图，内容等值故正确）。 */
typedef struct XSSS3GradSlot
{
    bool m_valid;       /**< 槽内容是否有效。 */
    int m_width;        /**< 缓存栅格宽。 */
    int m_height;       /**< 缓存栅格高。 */
    char m_key[256];    /**< 缓存键（声明值文本，超长截断仅损命中率）。 */
    XImage* m_image;    /**< 缓存栅格（槽拥有；懒创建，ARGB32 预乘）。 */
} XSSS3GradSlot;

static XSSS3ImageSlot xg3_bgImageSlot;    /**< background-image / background:url(...)。 */
static XSSS3ImageSlot xg3_borderImageSlot; /**< border-image。 */
static XSSS3ImageSlot xg3_iconImageSlot;  /**< image:/icon: 属性。 */
static XSSS3GradSlot xg3_gradSlot;        /**< 渐变栅格。 */

/** @brief 铺贴模式（对标 qcssparser TileMode：1=stretch 2=repeat
 *         3=round；0=未给出）。 */
#define XSSS3_TILE_STRETCH 1
#define XSSS3_TILE_REPEAT 2
#define XSSS3_TILE_ROUND 3

/** @brief 有界串拷贝（NUL 收尾；超长截断——仅损缓存命中率不损正确）。 */
static void xg3_keyCopy(char* dst, size_t cap, const char* src)
{
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (src) {
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

/** @brief 大小写不敏感前缀匹配（无 libc；url( 外壳识别用）。 */
static bool xg3_istartsWith(const char* p, const char* prefix)
{
    size_t i;
    if (!p || !prefix) return false;
    for (i = 0; prefix[i]; ++i) {
        char a = p[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != prefix[i]) return false;
    }
    return true;
}

/** @brief 就地切出 url(...) 段：把配对 ')' 改写为 NUL，*restOut 指向
 *         其后（供后续切片/关键字 token 解析）。
 *
 *  @return text 以 url( 开头（大小写不敏感）且找到配对括号返回 true；
 *          路径含 ')' 不支持（CSS 引号语法内括号场景，登记不支持）。 */
static bool xg3_splitUrlToken(char* text, char** restOut)
{
    char* p = text;
    if (!text || !restOut) return false;
    while (*p && XIsSpace((unsigned char)*p)) ++p;
    if (!xg3_istartsWith(p, "url(")) return false;
    p += 4;
    while (*p && *p != ')') ++p;
    if (*p != ')') return false;
    *p = '\0';
    *restOut = p + 1;
    return true;
}

/** @brief 切分后的 url token 路径提取：xg3_splitUrlToken 已把配对
 *         ')' 改写为 NUL，而 XCssParseUrlPath 要求末字符为 ')'——
 *         此处临时还原该字符并把串终止在切分点，按完整 url(...)
 *         外壳复用解析器（剥引号/收空白/超长整体拒绝同其合同），
 *         解析完成后复原切分态（rest token 消费方不受扰）。
 *
 *  @param buf 就地切分后的声明缓冲（切分点前为 url token 区）。
 *  @param split 切分点（原 ')' 的下一槽位，即 rest 首地址）。
 *  @return 解析成功且路径写入 url 返回 true；外壳残缺/路径超长
 *          返回 false（不截断）。 */
static bool xg3_urlPathFromSplit(char* buf, char* split, char* url,
                                 size_t cap)
{
    char saved;
    bool ok;
    if (!buf || !split || split <= buf) return false;
    saved = *split;   /* 暂存 rest 首 token 首字符。 */
    *split = '\0';    /* 串终止在切分点（暂掩 rest）。 */
    split[-1] = ')';  /* 还原 url( 外壳右括号。 */
    ok = XCssParseUrlPath(buf, url, cap);
    split[-1] = '\0'; /* 复原切分态。 */
    *split = saved;
    return ok;
}

/** @brief 图像槽取图：键命中直返（负缓存命中返 NULL）；换键重装载。 */
static const XImage* xg3_slotImage(XSSS3ImageSlot* slot, const char* url)
{
    if (!slot || !url || !*url) return NULL;
    if (!slot->m_image) {
        slot->m_image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!slot->m_image) return NULL;
    }
    if (slot->m_keySet && XStrcmp(slot->m_key, url) == 0)
        return slot->m_failed ? NULL : slot->m_image;
    /* 换键：重装载（失败置负缓存，免逐帧重试磁盘——性能红线）。 */
    if (!XImage_load_2(slot->m_image, url, NULL) ||
        XImage_isNull(slot->m_image)) {
        slot->m_failed = true;
    } else {
        slot->m_failed = false;
    }
    slot->m_keySet = true;
    xg3_keyCopy(slot->m_key, sizeof(slot->m_key), url);
    return slot->m_failed ? NULL : slot->m_image;
}

/** @brief 当前 (对象,状态) 的级联最高权重规则（镜像 xsss_lookup 的
 *         缓存回填口径；供按名取声明——opacity/icon-size 等无名表
 *         属性走声明原名扫描）。 */
static const XCssStyleRule* xg3_winningRule(XStyleSheetStyle* self,
                                            const XObject* obj,
                                            uint32_t state)
{
    if (self->m_cacheValid && self->m_cacheObj == obj &&
        self->m_cacheState == state)
        return self->m_cacheRule;
    {
        const XCssSelector* sel = NULL;
        uint32_t w = 0;
        const XCssStyleRule* rule =
            xsss_scanRules(self, obj, state, NULL, &sel, &w);
        self->m_cacheObj = obj;
        self->m_cacheState = state;
        self->m_cacheRule = rule;
        self->m_cacheSel = sel;
        self->m_cacheWeight = w;
        self->m_cacheValid = true;
        return rule;
    }
}

/** @brief 按声明原名（大小写不敏感）取值文本（无名表属性消费口：
 *         解析器对未识别属性恒存原名配 Unknown，见
 *         XCssStyleSheet.c 声明追加注释）。
 *
 *  @return 命中返回声明值文本（buf 内）；未命中返回 NULL。同规则内
 *          取首个命中声明（级联裁决已由 winningRule 完成）。 */
static const char* xg3_declTextByName(const XCssStyleRule* rule,
                                      const char* name, char* buf,
                                      size_t cap)
{
    int i;
    if (!rule || !name || !buf || cap < 4) return NULL;
    for (i = 0; i < rule->m_declarationCount; ++i) {
        const XCssDeclaration* d = &rule->m_declarations[i];
        const char* nm = d->m_propertyName ? XString_toUtf8(d->m_propertyName)
                                           : NULL;
        if (nm && XStrcasecmp(nm, name) == 0)
            return xsss_declText(d, buf, cap);
    }
    return NULL;
}

/** @brief opacity 声明值（0.0~1.0，缺省 1.0）。
 *
 *  定性（Qt 6.8.3 原文）：Qt 的 "opacity" 是 styleHint 名
 *  （SH_ToolTipLabel_Opacity，qstylesheetstyle.cpp knownStyleHints）
 *  ——仅提示 tooltip 窗口整体透明度，不存在元素级绘制透明消费；此处
 *  为 XGui 扩展语义：作用于本线绘制的装饰图像（背景图/渐变/九宫格/
 *  image 属性）的 painter 透明度，非元素整体（登记偏离）。 */
static float xg3_opacityOf(XStyleSheetStyle* self, const XObject* obj,
                           uint32_t state)
{
    const XCssStyleRule* rule;
    char buf[64];
    const char* text;
    const char* p;
    double v = 0.0;
    if (!self) return 1.0f;
    rule = xg3_winningRule(self, obj, state);
    if (!rule) return 1.0f;
    text = xg3_declTextByName(rule, "opacity", buf, sizeof(buf));
    if (!text || !*text) return 1.0f;
    p = text;
    if (!xsss_parseDouble(&p, &v)) return 1.0f;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return (float)v;
}

/** @brief 装饰绘制透明度进入（<1 时设画笔透明度；@return 是否需收尾，
 *         prevOut 带出进入前的画笔透明度供收尾还原）。 */
static bool xg3_opacityBegin(XStyleSheetStyle* self, const XObject* obj,
                             uint32_t state, XPainter* painter,
                             float* prevOut)
{
    float o;
    if (!painter) return false;
    if (prevOut) *prevOut = XPainter_opacity(painter);
    o = xg3_opacityOf(self, obj, state);
    if (o >= 0.999f) return false;
    XPainter_setOpacity(painter, o);
    return true;
}

/** @brief 装饰绘制透明度收尾（还原进入前值；不依赖 save/restore 配对）。 */
static void xg3_opacityEnd(bool active, XPainter* painter, float prev)
{
    if (active && painter) XPainter_setOpacity(painter, prev);
}

/** @brief 渐变参数 t（0~1，pad spread 口径钳位）。
 *
 *  坐标口径（对标 Qt QSS 渐变默认 ObjectBoundingMode——qcssparser
 *  parseBrushValue 的 CoordinateMode 缺省分支）：坐标组按盒宽/高分
 *  轴缩放（椭圆径向与 Qt 光栅引擎的对象包围盒矩阵一致）。
 *  - 线性：点在 (p1→p2) 轴上的投影比；轴退化（零向量）取 0；
 *  - 径向：到圆心的分轴归一化距离；radius≤0 取 1（外停靠色）；
 *  - 锥形：点方位角（度，y 向下屏幕系）减起始角 mod 360 → [0,1)。
 *  spread：XCssValue 合同仅校验不落盘（Qt QSS 缺省 PadSpread），
 *  按钳位实现；reflect/repeat 未实现（登记）。 */
static double xg3_gradientT(const XCssGradient* g, double px, double py,
                            int w, int h)
{
    double t;
    switch (g->m_type) {
    case XCssGradient_Linear: {
        double p1x = g->m_x1 * (double)w;
        double p1y = g->m_y1 * (double)h;
        double dx = (g->m_x2 - g->m_x1) * (double)w;
        double dy = (g->m_y2 - g->m_y1) * (double)h;
        double len2 = dx * dx + dy * dy;
        if (len2 <= 0.0) return 0.0;
        t = ((px - p1x) * dx + (py - p1y) * dy) / len2;
        break;
    }
    case XCssGradient_Radial: {
        double rx = g->m_radius * (double)w;
        double ry = g->m_radius * (double)h;
        double u;
        double v;
        if (rx <= 0.0 || ry <= 0.0) return 1.0;
        u = (px - g->m_cx * (double)w) / rx;
        v = (py - g->m_cy * (double)h) / ry;
        t = sqrt(u * u + v * v);
        break;
    }
    default: { /* XCssGradient_Conical。 */
        double dx = px - g->m_cx * (double)w;
        double dy = py - g->m_cy * (double)h;
        double deg;
        if (dx == 0.0 && dy == 0.0) return 0.0;
        deg = atan2(dy, dx) * (180.0 / 3.14159265358979323846);
        t = fmod(deg - g->m_angle, 360.0) / 360.0;
        if (t < 0.0) t += 1.0;
        break;
    }
    }
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return t;
}

/** @brief 停靠点插值取直连 ARGB（对标 QGradient::setStops 的按位
 *         置排序 + 区间线性插值；界外取端点停靠色）。 */
static uint32_t xg3_stopColorAt(const XCssGradientStop* stops, int n,
                                double t)
{
    int i;
    if (n <= 0) return 0xFF000000u;
    if (t <= stops[0].m_position) return stops[0].m_color;
    for (i = 1; i < n; ++i) {
        if (t <= stops[i].m_position) {
            double span = stops[i].m_position - stops[i - 1].m_position;
            double f = span > 0.0 ? (t - stops[i - 1].m_position) / span
                                  : 0.0;
            uint32_t c0 = stops[i - 1].m_color;
            uint32_t c1 = stops[i].m_color;
            /* 四通道同式：两侧先转 int 再相减（uint32_t 与 int 相减
             * 按常规算术转换走模 2^32——0-255 回绕成 ~4.29e9，乘 f
             * 截断后 <<24/<<16/<<8 高位叠字节产出杂纹，故 b 通道旧
             * 写法恒对而 a/r/g 渐深即错）。 */
            uint32_t a = (uint32_t)((double)(int)((c0 >> 24) & 0xFFu) +
                                    f * (double)((int)((c1 >> 24) & 0xFFu) -
                                                 (int)((c0 >> 24) & 0xFFu)));
            uint32_t r = (uint32_t)((double)(int)((c0 >> 16) & 0xFFu) +
                                    f * (double)((int)((c1 >> 16) & 0xFFu) -
                                                 (int)((c0 >> 16) & 0xFFu)));
            uint32_t gg = (uint32_t)((double)(int)((c0 >> 8) & 0xFFu) +
                                     f * (double)((int)((c1 >> 8) & 0xFFu) -
                                                  (int)((c0 >> 8) & 0xFFu)));
            uint32_t b = (uint32_t)((double)(int)(c0 & 0xFFu) +
                                    f * (double)((int)(c1 & 0xFFu) -
                                                 (int)(c0 & 0xFFu)));
            return (a << 24) | (r << 16) | (gg << 8) | b;
        }
    }
    return stops[n - 1].m_color;
}

/** @brief 渐变逐像素栅格化（直连 ARGB 插值后预乘写入 ARGB32 预乘
 *         目标；Qt 光栅按预乘插值，直连插值在半透明停靠点处有亚阈值
 *         观感差，登记口径）。 */
static void xg3_gradientFill(const XCssGradient* g, int w, int h,
                             XImage* img)
{
    XCssGradientStop stops[16];
    int n = g->m_stopCount > 16 ? 16 : g->m_stopCount;
    int i;
    int x;
    int y;
    int bpl;
    if (n <= 0) return;
    for (i = 0; i < n; ++i) stops[i] = g->m_stops[i];
    /* 插入排序按位置升序（n≤16；对标 QGradient::setStops 排序语义）。 */
    for (i = 1; i < n; ++i) {
        XCssGradientStop key = stops[i];
        int j = i - 1;
        while (j >= 0 && stops[j].m_position > key.m_position) {
            stops[j + 1] = stops[j];
            --j;
        }
        stops[j + 1] = key;
    }
    bpl = XImage_bytesPerLine(img);
    if (bpl < w * 4) return;
    for (y = 0; y < h; ++y) {
        uint32_t* row = (uint32_t*)(XImage_scanLine(img, y));
        if (!row) return;
        for (x = 0; x < w; ++x) {
            double t = xg3_gradientT(g, (double)x + 0.5, (double)y + 0.5,
                                     w, h);
            uint32_t c = xg3_stopColorAt(stops, n, t);
            uint32_t a = (c >> 24) & 0xFFu;
            uint32_t r = (c >> 16) & 0xFFu;
            uint32_t gg = (c >> 8) & 0xFFu;
            uint32_t b = c & 0xFFu;
            if (a < 0xFFu) { /* 预乘（(v*a+127)/255 舍入）。 */
                r = (r * a + 127u) / 255u;
                gg = (gg * a + 127u) / 255u;
                b = (b * a + 127u) / 255u;
            }
            row[x] = (a << 24) | (r << 16) | (gg << 8) | b;
        }
    }
}

/** @brief 渐变栅格取图（单槽缓存：键=(宽,高,值文本)；换键重算）。 */
static const XImage* xg3_gradientImage(const XCssGradient* g, int w, int h,
                                       const char* keyText)
{
    XSSS3GradSlot* slot = &xg3_gradSlot;
    if (!g || w < 1 || h < 1) return NULL;
    if (slot->m_valid && slot->m_width == w && slot->m_height == h &&
        slot->m_image && XStrcmp(slot->m_key, keyText) == 0)
        return slot->m_image;
    if (!slot->m_image) {
        slot->m_image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!slot->m_image) return NULL;
    }
    if (!XImage_reinit_ex(slot->m_image, w, h,
                          XImageFormat_ARGB32_Premultiplied))
        return NULL;
    xg3_gradientFill(g, w, h, slot->m_image);
    slot->m_valid = true;
    slot->m_width = w;
    slot->m_height = h;
    xg3_keyCopy(slot->m_key, sizeof(slot->m_key), keyText);
    return slot->m_image;
}

/** @brief 渐变背景绘制入口（栅格化到盒尺寸后整幅绘制；pad 口径下
 *         与画刷填充等价）。 */
static void xg3_drawGradient(XStyleSheetStyle* self, const XObject* obj,
                             const XStyleOption* option, XPainter* painter,
                             const XRect* boxRect, const XCssGradient* grad,
                             const char* keyText)
{
    XRect r;
    const XImage* img;
    bool opSaved;
    float opPrev = 1.0f;
    if (!self || !option || !painter || !grad || !keyText) return;
    r = boxRect ? *boxRect : option->m_rect;
    if (r.width < 1 || r.height < 1) return;
    img = xg3_gradientImage(grad, r.width, r.height, keyText);
    if (!img) return;
    opSaved = xg3_opacityBegin(self, obj, option->m_state, painter,
                               &opPrev);
    XPainter_drawImage(painter, img, r.x, r.y);
    xg3_opacityEnd(opSaved, painter, opPrev);
}

/** @brief 边框宽查询（镜像 xg3_borderDrawImpl 宽度口径：BorderWidth
 *         声明回落 Border 简写，逐 token 取首个可解析长度）。 */
static int xg3_borderWidthPx(XStyleSheetStyle* self, const XObject* obj,
                             uint32_t state, const XCssLengthContext* ctx)
{
    const XCssDeclaration* bw;
    char buf[256];
    const char* text;
    int width = 0;
    if (!self || !ctx) return 0;
    bw = xsss_lookup(self, obj, state, XCssProperty_BorderWidth);
    if (!bw) bw = xsss_lookup(self, obj, state, XCssProperty_Border);
    if (!bw || !bw->m_value) return 0;
    text = xsss_declText(bw, buf, sizeof(buf));
    while (text && *text) {
        char tok[64];
        size_t tl = 0;
        XCssLength len;
        while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
               tl + 1 < sizeof(tok))
            ++tl;
        XMemcpy(tok, text, tl);
        tok[tl] = '\0';
        text += tl;
        while (*text && XIsSpace((unsigned char)*text)) ++text;
        if (XCssParseLengthEx(tok, &len)) {
            width = xsss_lengthPx(&len, ctx, true);
            break;
        }
    }
    return width;
}

/** @brief 单片绘制：同尺寸且源在原点走 drawImage 直绘，其余走源
 *         矩形缩放（九宫格/边条/中心共用；3 参 drawImage 整幅直绘
 *         语义只与 sx=sy=0 等价——九宫非原点角片误入此路会画成整图
 *         左上内容）。 */
static void xg3_drawImagePiece(XPainter* painter, const XImage* img,
                               int dx, int dy, int dw, int dh,
                               int sx, int sy, int sw, int sh)
{
    if (!painter || !img || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0)
        return;
#if XPAINTER_IMAGE_RECT_ON
    if (dw == sw && dh == sh && sx == 0 && sy == 0) {
        XPainter_drawImage(painter, img, dx, dy);
        return;
    }
    XPainter_drawImage_3(painter, dx, dy, dw, dh, img, sx, sy, sw, sh);
#else
    /* 裁剪配置退路（XSTYLE_ON 构建下 IMAGE_RECT_ON 恒 1，见
     * XGuiConfig.h 裁剪链；此分支仅保编译完备）：非 1:1 缩到源尺寸。 */
    {
        XImage piece;
        XRect src;
        if (dw != sw || dh != sh) {
            dw = sw < dw ? sw : dw;
            dh = sh < dh ? sh : dh;
            sw = dw;
            sh = dh;
        }
        XImage_init(&piece);
        src.x = sx;
        src.y = sy;
        src.width = sw;
        src.height = sh;
        XImage_copyRect(img, &src, &piece);
        XPainter_drawImage(painter, &piece, dx, dy);
        XClassDeinit(&piece);
    }
#endif /* XPAINTER_IMAGE_RECT_ON */
}

/** @brief 横向边条绘制（顶/底边：横向按铺贴规则、纵向拉伸到条高）。 */
static void xg3_stripDrawH(XPainter* painter, const XImage* img, int mode,
                           int tx, int ty, int tw, int th,
                           int sx, int sy, int sw, int sh)
{
    if (tw <= 0 || th <= 0 || sw <= 0 || sh <= 0) return;
    if (mode == XSSS3_TILE_REPEAT) {
        int x0 = tx;
        while (x0 < tx + tw) {
            int wi = (sw < tx + tw - x0) ? sw : (tx + tw - x0);
            xg3_drawImagePiece(painter, img, x0, ty, wi, th, sx, sy, wi,
                               sh);
            x0 += sw;
        }
        return;
    }
    if (mode == XSSS3_TILE_ROUND) {
        int n = (tw + sw / 2) / sw;
        int tileW;
        int k;
        if (n < 1) n = 1;
        tileW = tw / n;
        for (k = 0; k < n; ++k)
            xg3_drawImagePiece(painter, img, tx + k * tileW, ty, tileW,
                               th, sx, sy, sw, sh);
        return;
    }
    xg3_drawImagePiece(painter, img, tx, ty, tw, th, sx, sy, sw, sh);
}

/** @brief 纵向边条绘制（左/右边：纵向按铺贴规则、横向拉伸到条宽）。 */
static void xg3_stripDrawV(XPainter* painter, const XImage* img, int mode,
                           int tx, int ty, int tw, int th,
                           int sx, int sy, int sw, int sh)
{
    if (tw <= 0 || th <= 0 || sw <= 0 || sh <= 0) return;
    if (mode == XSSS3_TILE_REPEAT) {
        int y0 = ty;
        while (y0 < ty + th) {
            int hi = (sh < ty + th - y0) ? sh : (ty + th - y0);
            xg3_drawImagePiece(painter, img, tx, y0, tw, hi, sx, sy, sw,
                               hi);
            y0 += sh;
        }
        return;
    }
    if (mode == XSSS3_TILE_ROUND) {
        int n = (th + sh / 2) / sh;
        int tileH;
        int k;
        if (n < 1) n = 1;
        tileH = th / n;
        for (k = 0; k < n; ++k)
            xg3_drawImagePiece(painter, img, tx, ty + k * tileH, tw,
                               tileH, sx, sy, sw, sh);
        return;
    }
    xg3_drawImagePiece(painter, img, tx, ty, tw, th, sx, sy, sw, sh);
}

/** @brief border-image 消费：命中并成功绘制返回 true（调用侧跳过
 *         普通边框，对标 QRenderRule::drawBorder 的 hasBorderImage
 *         提前返回分支 + qDrawBorderPixmap 九宫格语义）。
 *
 *  语法（对标 Declaration::borderImageValue）：url(...) 后 1~4 个整
 *  数切片（顺时针 上右下左，1 值=四边、2 值=下=上/左=右、3 值=
 *  左=右），尾关键字 repeat/round/stretch（1 个=双向、2 个=横/纵；
 *  缺省 stretch）；切片缺省回落边框宽（对标 QRenderRule::fixupBorder
 *  的 cuts[0]==-1 分支）。目标边距=边框宽（无边框宽声明时=切片尺寸，
 *  自然尺寸框）。角片 1:1 不缩放、边条按横/纵铺贴规则、中心拉伸
 *  （qDrawBorderPixmap 缺省口径）。装载失败回落普通边框（不黑块）。 */
static bool xg3_borderImageDraw(XStyleSheetStyle* self, const XObject* obj,
                                const XStyleOption* option,
                                XPainter* painter, const XRect* boxRect,
                                const XCssLengthContext* ctx)
{
    const XCssDeclaration* d;
    char buf[512];
    char* rest = NULL;
    char url[XSSS3_URL_CAP];
    const XImage* img;
    int cuts[4]; /* 上/右/下/左。 */
    int hMode = XSSS3_TILE_STRETCH;
    int vMode = XSSS3_TILE_STRETCH;
    int kwCount = 0;      /* 铺贴关键字个数（1=双向，2=横/纵）。 */
    int cutCount = 0;
    int bw;
    int tm[4];
    int iw;
    int ih;
    XRect r;
    bool opSaved;
    float opPrev = 1.0f;
    int i;
    if (!self || !option || !painter) return false;
    if (self->m_sheet.m_ruleCount == 0) return false;
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_BorderImage);
    if (!d || !d->m_value) return false;
    if (!xsss_declText(d, buf, sizeof(buf)) || !buf[0]) return false;
    if (!xg3_splitUrlToken(buf, &rest)) return false;
    if (!xg3_urlPathFromSplit(buf, rest, url, sizeof(url))) return false;
    /* 切片整数（顺时针）与尾关键字解析。 */
    while (rest && *rest) {
        char tok[64];
        size_t tl = 0;
        const char* p = rest;
        double num = 0.0;
        while (p[tl] && !XIsSpace((unsigned char)p[tl]) &&
               tl + 1 < sizeof(tok))
            ++tl;
        XMemcpy(tok, p, tl);
        tok[tl] = '\0';
        rest = (char*)p + tl;
        while (*rest && XIsSpace((unsigned char)*rest)) ++rest;
        if (XStrcasecmp(tok, "repeat") == 0 ||
            XStrcasecmp(tok, "round") == 0 ||
            XStrcasecmp(tok, "stretch") == 0) {
            /* 关键字序（CSS/Qt 双关键字口径）：1 个=横纵同值；
             * 2 个=首者横向、后写者纵向。 */
            int mode = (XStrcasecmp(tok, "repeat") == 0)
                           ? XSSS3_TILE_REPEAT
                           : (XStrcasecmp(tok, "round") == 0)
                                 ? XSSS3_TILE_ROUND
                                 : XSSS3_TILE_STRETCH;
            if (kwCount == 0) {
                hMode = mode;
                vMode = mode;
            } else {
                vMode = mode; /* 首关键字已占横向。 */
            }
            ++kwCount;
        } else if (xsss_parseDouble(&p, &num) && cutCount < 4) {
            int v = xsss_roundHalfAway(num);
            if (v < 0) v = 0;
            switch (cutCount) {
            case 0: /* 1 值=四边。 */
                cuts[0] = cuts[1] = cuts[2] = cuts[3] = v;
                break;
            case 1: /* 2 值：右/左。 */
                cuts[1] = cuts[3] = v;
                break;
            case 2: /* 3 值：下。 */
                cuts[2] = v;
                break;
            default: /* 4 值：左。 */
                cuts[3] = v;
                break;
            }
            ++cutCount;
        }
        /* 其余 token 忽略（对标未知值跳过）。 */
    }
    if (cutCount == 0) {
        /* 切片缺省 → 边框宽（对标 fixupBorder）。 */
        bw = xg3_borderWidthPx(self, obj, option->m_state, ctx);
        cuts[0] = cuts[1] = cuts[2] = cuts[3] = bw > 0 ? bw : 0;
    }
    img = xg3_slotImage(&xg3_borderImageSlot, url);
    if (!img) return false;
    iw = XImage_width(img);
    ih = XImage_height(img);
    if (iw < 1 || ih < 1) return false;
    /* 切片钳位到图像范围（左右/上下互挤）。 */
    for (i = 0; i < 4; ++i) {
        if (cuts[i] < 0) cuts[i] = 0;
    }
    if (cuts[0] > ih) cuts[0] = ih;
    if (cuts[2] > ih - cuts[0]) cuts[2] = ih - cuts[0];
    if (cuts[1] > iw) cuts[1] = iw;
    if (cuts[3] > iw - cuts[1]) cuts[3] = iw - cuts[1];
    /* 目标边距：边框宽声明优先，否则切片尺寸（自然框）。 */
    bw = xg3_borderWidthPx(self, obj, option->m_state, ctx);
    for (i = 0; i < 4; ++i) tm[i] = bw > 0 ? bw : cuts[i];
    r = boxRect ? *boxRect : option->m_rect;
    if (r.width < 1 || r.height < 1) return true;
    opSaved = xg3_opacityBegin(self, obj, option->m_state, painter,
                               &opPrev);
    /* 四角（1:1 不缩放）。 */
    if (tm[3] > 0 && tm[0] > 0)
        xg3_drawImagePiece(painter, img, r.x, r.y, cuts[3], cuts[0],
                           0, 0, cuts[3], cuts[0]);
    if (tm[2] > 0 && tm[0] > 0)
        xg3_drawImagePiece(painter, img, r.x + r.width - cuts[1], r.y,
                           cuts[1], cuts[0], iw - cuts[1], 0, cuts[1],
                           cuts[0]);
    if (tm[3] > 0 && tm[2] > 0)
        xg3_drawImagePiece(painter, img, r.x, r.y + r.height - cuts[3],
                           cuts[3], cuts[2], 0, ih - cuts[2],
                           cuts[3], cuts[2]);
    if (tm[1] > 0 && tm[2] > 0)
        xg3_drawImagePiece(painter, img, r.x + r.width - cuts[1],
                           r.y + r.height - cuts[2], cuts[1], cuts[2],
                           iw - cuts[1], ih - cuts[2], cuts[1],
                           cuts[2]);
    /* 上/下边条（横铺贴规则）。 */
    if (r.width > cuts[1] + cuts[3] && tm[0] > 0)
        xg3_stripDrawH(painter, img, hMode, r.x + cuts[3], r.y,
                       r.width - cuts[1] - cuts[3], tm[0], cuts[3], 0,
                       iw - cuts[1] - cuts[3], cuts[0]);
    if (r.width > cuts[1] + cuts[3] && tm[2] > 0)
        xg3_stripDrawH(painter, img, hMode, r.x + cuts[3],
                       r.y + r.height - tm[2], r.width - cuts[1] - cuts[3],
                       tm[2], cuts[3], ih - cuts[2],
                       iw - cuts[1] - cuts[3], cuts[2]);
    /* 左/右边条（纵铺贴规则）。 */
    if (r.height > cuts[0] + cuts[2] && tm[3] > 0)
        xg3_stripDrawV(painter, img, vMode, r.x, r.y + cuts[0], tm[3],
                       r.height - cuts[0] - cuts[2], 0, cuts[0], cuts[3],
                       ih - cuts[0] - cuts[2]);
    if (r.height > cuts[0] + cuts[2] && tm[1] > 0)
        xg3_stripDrawV(painter, img, vMode, r.x + r.width - tm[1],
                       r.y + cuts[0], tm[1], r.height - cuts[0] - cuts[2],
                       iw - cuts[1], cuts[0], cuts[1],
                       ih - cuts[0] - cuts[2]);
    /* 中心（双向拉伸；对标 qDrawBorderPixmap 中片的 stretch 缺省）。 */
    if (r.width > cuts[1] + cuts[3] && r.height > cuts[0] + cuts[2])
        xg3_drawImagePiece(painter, img, r.x + cuts[3], r.y + cuts[0],
                           r.width - cuts[1] - cuts[3],
                           r.height - cuts[0] - cuts[2], cuts[3], cuts[0],
                           iw - cuts[1] - cuts[3], ih - cuts[0] - cuts[2]);
    xg3_opacityEnd(opSaved, painter, opPrev);
    return true;
}

/** @brief 背景图像消费：background-image 声明（或 background 简写
 *         的 url，经 shorthandUrl 入参）装载 + repeat 四模式平铺 +
 *         position 定位（对标 QRenderRule::drawBackgroundImage）。
 *
 *  口径：缺省 repeat=Repeat（双向平铺）、position=左上（对标
 *  QRenderRule 缺省 Repeat_XY/AlignTop|AlignLeft）；覆盖盒=边框盒内
 *  缩边框宽（Qt 缺省 Origin_Padding → paddingRect）；裁剪=边框盒
 *  （Qt 缺省 Origin_Border）。background-origin/clip 声明按
 *  border/padding/content 消费（见下）；background-attachment：本
 *  绘制入口无滚动偏移输入，scroll 与 fixed 等价（登记）。平铺相位
 *  保持对齐位置（off = 对齐点相对覆盖盒原点的负模，对标 Qt 的
 *  phase 保持）。 */
static void xg3_drawBackgroundImage(XStyleSheetStyle* self,
                                    const XObject* obj,
                                    const XStyleOption* option,
                                    XPainter* painter, const XRect* boxRect,
                                    const char* shorthandUrl)
{
    const XCssDeclaration* d;
    char buf[512];
    char url[XSSS3_URL_CAP];
    const char* path = NULL;
    const XImage* img;
    XCssGradient grad;
    XCssRepeatMode repeat = XCssRepeat_Repeat; /* Qt 缺省 Repeat_XY。 */
    double posX = 0.0;
    double posY = 0.0;
    bool posXIsPct = false;
    bool posYIsPct = false;
    XCssLengthContext ctx;
    XRect clipR;
    XRect area;
    int bw = 0;
    int iw;
    int ih;
    int x0;
    int y0;
    int offX;
    int offY;
    int startX;
    int startY;
    int x;
    int y;
    bool opSaved;
    float opPrev = 1.0f;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    if (shorthandUrl && *shorthandUrl) {
        path = shorthandUrl;
    } else {
        d = xsss_lookup(self, obj, option->m_state,
                        XCssProperty_BackgroundImage);
        if (!d || !d->m_value) return;
        if (!xsss_declText(d, buf, sizeof(buf)) || !buf[0]) return;
        /* background-image 也可为渐变函数（对标 extractBackground）。 */
        if (XCssParseGradient(buf, &grad)) {
            xg3_drawGradient(self, obj, option, painter, boxRect, &grad,
                             buf);
            return;
        }
        /* 路径取值对齐 background 简写/image: 属性绿通路：对未切分
         * 的完整声明文本直接解析（url(...) 外壳完整；勿先经
         * xg3_splitUrlToken——其把 ')' 改写为 NUL 后
         * XCssParseUrlPath 的末字符 ')' 校验恒败）。 */
        if (!XCssParseUrlPath(buf, url, sizeof(url))) return;
        path = url;
    }
    img = xg3_slotImage(&xg3_bgImageSlot, path);
    if (!img) return;
    iw = XImage_width(img);
    ih = XImage_height(img);
    if (iw < 1 || ih < 1) return;
    /* repeat/position 声明（缺省 Repeat/左上）。 */
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_BackgroundRepeat);
    if (d && d->m_value) {
        char rbuf[64];
        const char* rtext = xsss_declText(d, rbuf, sizeof(rbuf));
        if (rtext && *rtext)
            (void)XCssParseBackgroundRepeat(rtext, &repeat);
    }
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_BackgroundPosition);
    if (d && d->m_value) {
        char pbuf[128];
        const char* ptext = xsss_declText(d, pbuf, sizeof(pbuf));
        if (ptext && *ptext)
            (void)XCssParseBackgroundPosition(ptext, &posX, &posY,
                                              &posXIsPct, &posYIsPct);
    }
    ctx = xsss_lengthContext(painter, boxRect ? boxRect : &option->m_rect);
    clipR = boxRect ? *boxRect : option->m_rect;
    /* 覆盖盒 = 边框盒内缩边框宽（Qt Origin_Padding 缺省）。 */
    bw = xg3_borderWidthPx(self, obj, option->m_state, &ctx);
    area = clipR;
    area.x += bw;
    area.y += bw;
    area.width -= bw * 2;
    area.height -= bw * 2;
    /* background-origin 声明（border/padding/content；缺省 padding）。 */
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_BackgroundOrigin);
    if (d && d->m_value) {
        char obuf[64];
        const char* otext = xsss_declText(d, obuf, sizeof(obuf));
        if (otext && *otext && XStrcasecmp(otext, "border") == 0) {
            area = clipR;
        } else if (otext && *otext &&
                   XStrcasecmp(otext, "content") == 0) {
            int padO[4];
            xsss_padding(self, obj, option->m_state, &ctx, padO);
            area.x += padO[0];
            area.y += padO[1];
            area.width -= padO[0] + padO[2];
            area.height -= padO[1] + padO[3];
        }
    }
    /* background-clip 声明（border/padding/content；缺省 border）。 */
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_BackgroundClip);
    if (d && d->m_value) {
        char cbuf[64];
        const char* ctext = xsss_declText(d, cbuf, sizeof(cbuf));
        if (ctext && *ctext && XStrcasecmp(ctext, "padding") == 0) {
            clipR.x += bw;
            clipR.y += bw;
            clipR.width -= bw * 2;
            clipR.height -= bw * 2;
        } else if (ctext && *ctext && XStrcasecmp(ctext, "content") == 0) {
            int padC[4];
            xsss_padding(self, obj, option->m_state, &ctx, padC);
            clipR.x += bw + padC[0];
            clipR.y += bw + padC[1];
            clipR.width -= (bw + padC[0]) + (bw + padC[2]);
            clipR.height -= (bw + padC[1]) + (bw + padC[3]);
        }
    }
    if (area.width < 1 || area.height < 1) return;
    if (clipR.width < 1 || clipR.height < 1) return;
    /* 对齐位置（对标 QStyle::alignedRect 的位置分量）。 */
    x0 = area.x + (posXIsPct
                       ? (int)((double)(area.width - iw) * posX / 100.0)
                       : xsss_roundHalfAway(posX));
    y0 = area.y + (posYIsPct
                       ? (int)((double)(area.height - ih) * posY / 100.0)
                       : xsss_roundHalfAway(posY));
    opSaved = xg3_opacityBegin(self, obj, option->m_state, painter,
                               &opPrev);
    XPainter_save(painter);
    XPainter_setClipRect(painter, &clipR,
                         XPainterClipOperation_IntersectClip);
    switch (repeat) {
    case XCssRepeat_NoRepeat:
        XPainter_drawImage(painter, img, x0, y0);
        break;
    case XCssRepeat_RepeatX:
        /* 横向全带宽平铺（相位保持），纵向单行由裁剪约束。 */
        offX = (x0 - area.x) % iw;
        if (offX < 0) offX += iw;
        startX = area.x - offX;
        for (x = startX; x < area.x + area.width; x += iw)
            XPainter_drawImage(painter, img, x, y0);
        break;
    case XCssRepeat_RepeatY:
        offY = (y0 - area.y) % ih;
        if (offY < 0) offY += ih;
        startY = area.y - offY;
        for (y = startY; y < area.y + area.height; y += ih)
            XPainter_drawImage(painter, img, x0, y);
        break;
    default: /* XCssRepeat_Repeat：双向平铺。 */
        offX = (x0 - area.x) % iw;
        if (offX < 0) offX += iw;
        offY = (y0 - area.y) % ih;
        if (offY < 0) offY += ih;
        startX = area.x - offX;
        startY = area.y - offY;
        for (y = startY; y < area.y + area.height; y += ih)
            for (x = startX; x < area.x + area.width; x += iw)
                XPainter_drawImage(painter, img, x, y);
        break;
    }
    XPainter_restore(painter);
    xg3_opacityEnd(opSaved, painter, opPrev);
}

/** @brief 矩形描边（outline 用；镜像 xg3_borderDrawImpl 的直角多层/
 *         虚线点线分段/圆角三路，独立成 helper 供 outline 单独消费）。 */
static void xg3_strokeRect(XPainter* painter, XRect r, int width,
                           uint32_t color, int style, int radius)
{
    int i;
    if (width < 1 || r.width < 1 || r.height < 1) return;
    XPainter_setPen(painter, color);
    if (width >= 2) {
        r.x += width / 2;
        r.y += width / 2;
        r.width -= width;
        r.height -= width;
    }
#if XPAINTER_SHAPE_ON
    if (radius > 0) {
        XPainter_drawRoundedRect(painter, &r, radius, radius);
        return;
    }
#endif
    if (style == 1 || style == 2) {
        /* 虚线/点线：四边分段绘制（4px 段+4px 空 / 2px 点+4px 空）。 */
        int seg = (style == 1) ? 4 : 2;
        int gap = 4;
        int x0 = r.x;
        int y0 = r.y;
        int x1 = r.x + r.width - 1;
        int y1 = r.y + r.height - 1;
        for (i = 0; i <= x1 - x0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > x1 - x0) e = x1 - x0;
            XPainter_drawLine(painter, x0 + i, y0, x0 + e, y0);
            XPainter_drawLine(painter, x0 + i, y1, x0 + e, y1);
        }
        for (i = 0; i <= y1 - y0; i += seg + gap) {
            int e = i + seg - 1;
            if (e > y1 - y0) e = y1 - y0;
            XPainter_drawLine(painter, x0, y0 + i, x0, y0 + e);
            XPainter_drawLine(painter, x1, y0 + i, x1, y0 + e);
        }
        return;
    }
    /* 直角描边：drawLine 四边（线宽 1px/次，width>1 时多层内缩）。 */
    for (i = 0; i < width; ++i) {
        XPainter_drawLine(painter, r.x + i, r.y + i,
                          r.x + r.width - 1 - i, r.y + i);
        XPainter_drawLine(painter, r.x + r.width - 1 - i, r.y + i,
                          r.x + r.width - 1 - i, r.y + r.height - 1 - i);
        XPainter_drawLine(painter, r.x + r.width - 1 - i,
                          r.y + r.height - 1 - i, r.x + i,
                          r.y + r.height - 1 - i);
        XPainter_drawLine(painter, r.x + i, r.y + r.height - 1 - i,
                          r.x + i, r.y + i);
    }
}

/** @brief outline 族描边消费（outline/outline-width/color/style/
 *         radius/offset；对标 QRenderRule::drawOutline 的
 *         qDrawBorder 语义）。
 *
 *  定性：Qt 的 drawOutline 直接按给定矩形 qDrawBorder（outline
 *  offset 在 6.8.3 绘制路径未被消费，仅解析）；此处按 CSS 口径把
 *  outline-offset 应用为矩形外扩/内缩（登记与 Qt 的差异）。宽度/
 *  风格/颜色回落链镜像普通边框（单侧声明回落简写、无色回落
 *  currentColor 链）；"outline: none" 与 outline-style:none 均不画。 */
static void xg3_outlineDraw(XStyleSheetStyle* self, const XObject* obj,
                            const XStyleOption* option, XPainter* painter,
                            const XRect* boxRect, const XCssLengthContext* ctx)
{
    const XCssDeclaration* d;
    char buf[256];
    const char* text;
    int width = 0;
    bool hasWidth = false;
    bool hasColor = false;
    uint32_t color = 0;
    int style = 0;
    int radius = 0;
    int offset = 0;
    XRect r;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    /* 风格：none 短路（含 outline 简写 none）。 */
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_OutlineStyle);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        if (text && *text && XStrcasecmp(text, "none") == 0) return;
        if (text && *text && XStrcasecmp(text, "dashed") == 0) style = 1;
        else if (text && *text && XStrcasecmp(text, "dotted") == 0) style = 2;
    }
    /* 宽度：OutlineWidth 回落 Outline 简写首个可解析长度。 */
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_OutlineWidth);
    if (!d) d = xsss_lookup(self, obj, option->m_state, XCssProperty_Outline);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        while (text && *text && !hasWidth) {
            char tok[64];
            size_t tl = 0;
            XCssLength len;
            while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, text, tl);
            tok[tl] = '\0';
            text += tl;
            while (*text && XIsSpace((unsigned char)*text)) ++text;
            if (XCssParseLengthEx(tok, &len)) {
                width = xsss_lengthPx(&len, ctx, true);
                hasWidth = true;
            }
            if (XStrcasecmp(tok, "none") == 0) return;
        }
    }
    if (!hasWidth || width <= 0) return;
    /* 颜色：OutlineColor → Outline 简写色 token → currentColor 链。 */
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_OutlineColor);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        if (text && *text && XCssParseColor(text, &color)) hasColor = true;
    }
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_Outline);
    if (!hasColor && d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        while (text && *text && !hasColor) {
            char tok[64];
            size_t tl = 0;
            uint32_t token = 0;
            while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, text, tl);
            tok[tl] = '\0';
            text += tl;
            while (*text && XIsSpace((unsigned char)*text)) ++text;
            if (XCssParseColorToken(tok, tl, &token)) {
                color = token;
                hasColor = true;
            }
        }
    }
    if (!hasColor) {
        const XCssDeclaration* fg = xsss_lookup(self, obj,
                                                option->m_state,
                                                XCssProperty_Color);
        text = fg ? xsss_declText(fg, buf, sizeof(buf)) : NULL;
        if (text && *text && XCssParseColor(text, &color)) hasColor = true;
        else if (option->m_textColor) {
            color = option->m_textColor;
            hasColor = true;
        } else {
            color = 0xFF000000u;
        }
    }
    if (hasColor && (color & 0xFF000000u) == 0)
        return; /* 全透明描边不产生像素（对标透明画笔）。 */
    /* 偏移与圆角。 */
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_OutlineOffset);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        if (text && *text) {
            XCssLength len;
            if (XCssParseLengthEx(text, &len))
                offset = xsss_lengthPx(&len, ctx, true);
        }
    }
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_OutlineRadius);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        if (text && *text) {
            XCssLength len;
            if (XCssParseLengthEx(text, &len))
                radius = xsss_lengthPx(&len, ctx, true);
        }
    }
    r = boxRect ? *boxRect : option->m_rect;
    r.x -= offset;
    r.y -= offset;
    r.width += offset * 2;
    r.height += offset * 2;
    xg3_strokeRect(painter, r, width, color, style, radius);
}

/** @brief image:/icon: 属性消费（对标 QRenderRule::drawImage：内容
 *         盒内按对齐绘制，缺省居中）。
 *
 *  口径：image（或 icon）声明值为 url(...)，经单槽缓存装载；尺寸=
 *  图像自然尺寸，icon-size（无名表属性，按声明原名扫描）为两整数
 *  时缩放到该尺寸；对齐=image-position 声明关键字（left/right/top/
 *  bottom/center 组合），缺省居中。内容盒=边框盒内缩（边框宽+
 *  padding）。登记：Qt 的 icon 属性在 CE_PushButtonLabel 按规则
 *  替换控件自身图标（label 路径不在本线名下文件），XGui 此处为
 *  叠加绘制——控件自带图标时会并存；子控件规则（::drop-down 等）
 *  的 image 消费在 G2 线锚点区内，不在本线范围。 */
static void xg3_ruleImageDraw(XStyleSheetStyle* self, const XObject* obj,
                              const XStyleOption* option, XPainter* painter,
                              const XRect* boxRect,
                              const XCssLengthContext* ctx)
{
    const XCssDeclaration* d;
    const XCssStyleRule* rule;
    char buf[256];
    char sbuf[64];
    const char* text;
    char url[XSSS3_URL_CAP];
    const XImage* img;
    XRect r;
    int pad[4];
    int bw;
    int iw;
    int ih;
    int dw;
    int dh;
    int x;
    int y;
    int ax = -1; /* -1=center 0=left/top 1=right/bottom。 */
    int ay = -1;
    bool opSaved;
    float opPrev = 1.0f;
    if (!self || !option || !painter || !ctx) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    d = xsss_lookup(self, obj, option->m_state, XCssProperty_QtImage);
    if (!d) d = xsss_lookup(self, obj, option->m_state, XCssProperty_QtIcon);
    if (!d || !d->m_value) return;
    text = xsss_declText(d, buf, sizeof(buf));
    if (!text || !*text) return;
    if (!XCssParseUrlPath(text, url, sizeof(url))) return;
    img = xg3_slotImage(&xg3_iconImageSlot, url);
    if (!img) return;
    iw = XImage_width(img);
    ih = XImage_height(img);
    if (iw < 1 || ih < 1) return;
    dw = iw;
    dh = ih;
    /* icon-size（无名表属性，按原名扫描）：两整数 = 目标尺寸。 */
    rule = xg3_winningRule(self, obj, option->m_state);
    text = xg3_declTextByName(rule, "icon-size", sbuf, sizeof(sbuf));
    if (text && *text) {
        int vals[2];
        int n = 0;
        const char* p = text;
        while (*p && n < 2) {
            char tok[32];
            size_t tl = 0;
            double num = 0.0;
            const char* q = p;
            while (p[tl] && !XIsSpace((unsigned char)p[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, p, tl);
            tok[tl] = '\0';
            p += tl;
            while (*p && XIsSpace((unsigned char)*p)) ++p;
            if (xsss_parseDouble(&q, &num)) {
                int v = xsss_roundHalfAway(num);
                if (v < 1) v = 1;
                vals[n] = v;
                ++n;
            }
        }
        if (n == 1) {
            dw = dh = vals[0];
        } else if (n == 2) {
            dw = vals[0];
            dh = vals[1];
        }
    }
    /* image-position 对齐关键字。 */
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_QtImageAlignment);
    if (d && d->m_value) {
        text = xsss_declText(d, buf, sizeof(buf));
        while (text && *text) {
            char tok[32];
            size_t tl = 0;
            while (text[tl] && !XIsSpace((unsigned char)text[tl]) &&
                   tl + 1 < sizeof(tok))
                ++tl;
            XMemcpy(tok, text, tl);
            tok[tl] = '\0';
            text += tl;
            while (*text && XIsSpace((unsigned char)*text)) ++text;
            if (XStrcasecmp(tok, "left") == 0) ax = 0;
            else if (XStrcasecmp(tok, "right") == 0) ax = 1;
            else if (XStrcasecmp(tok, "top") == 0) ay = 0;
            else if (XStrcasecmp(tok, "bottom") == 0) ay = 1;
            /* center 轴中立：保持缺省 -1。 */
        }
    }
    /* 内容盒 = 边框盒内缩（边框宽 + padding）。 */
    r = boxRect ? *boxRect : option->m_rect;
    bw = xg3_borderWidthPx(self, obj, option->m_state, ctx);
    xsss_padding(self, obj, option->m_state, ctx, pad);
    r.x += bw + pad[0];
    r.y += bw + pad[1];
    r.width -= (bw + pad[0]) + (bw + pad[2]);
    r.height -= (bw + pad[1]) + (bw + pad[3]);
    if (r.width < 1 || r.height < 1) return;
    x = r.x + (ax == 0 ? 0
                       : (ax == 1 ? r.width - dw : (r.width - dw) / 2));
    y = r.y + (ay == 0 ? 0
                       : (ay == 1 ? r.height - dh : (r.height - dh) / 2));
    if (x < r.x) x = r.x;
    if (y < r.y) y = r.y;
    opSaved = xg3_opacityBegin(self, obj, option->m_state, painter,
                               &opPrev);
    xg3_drawImagePiece(painter, img, x, y, dw, dh, 0, 0, iw, ih);
    xg3_opacityEnd(opSaved, painter, opPrev);
}

/* ==================== ===== G3 新增区 结束 ==================== */

#endif /* XSTYLE_ON */
