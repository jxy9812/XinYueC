/******************************************************************************
 * @file       XCssValue.c
 * @brief      CSS 值类型化解析扩展实现（合同见 XCssValue.h）。
 * @details    纯函数无状态：切词只做首尾空白收缩与顶层（引号/括号感知）
 *             逗号、冒号切分，不引入动态内存；渐变解析先落局部临时值、
 *             全部成功才整体写回输出（失败时输出保持原值）。禁 libc：
 *             字符分类/串比较经 XStringUtils，块拷贝/清零经 XMemory。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCssValue.h"

#if XSTYLE_ON

#include "XStyleSheetStyle.h" /* XCssParseColor（渐变停靠点颜色实参）。 */
#include "XStringUtils.h"     /* XIsSpace/XIsDigit/XStrlen/XStrcasecmp。 */
#include "XMemory.h"          /* XMemcpy/XMemset。 */
#if XPALETTE_ON
#include "XPalette.h"         /* XPaletteColorRole 角色名表。 */
#endif

/* ==================== 通用切词助手 ==================== */

/** @brief 判空白（unsigned char 提升规避负索引 UB）。 */
static bool xcv_isSpace(char c)
{
    return XIsSpace((unsigned char)c) != 0;
}

/** @brief 首尾空白收缩（不修改串，仅收 [b,e) 端点）。 */
static void xcv_trim(const char** b, const char** e)
{
    while (*b < *e && xcv_isSpace(**b)) ++*b;
    while (*e > *b && xcv_isSpace((*e)[-1])) --*e;
}

/** @brief 限长拷贝 [b,e) 并补 NUL；超容量（len+1 > cap）返回 false。 */
static bool xcv_copyToken(const char* b, const char* e, char* buf, size_t cap)
{
    size_t len = (size_t)(e - b);
    if (len + 1 > cap) return false;
    if (len > 0) XMemcpy(buf, b, len);
    buf[len] = '\0';
    return true;
}

/** @brief 十进制浮点解析（可选符号/整数/小数；无指数记号，CSS 值场景
 *         够用；至少 1 位数字才成功；失败不动 *pp）。
 *
 *  口径同 XStyleSheetStyle.c 的 xsss_parseDouble（Qt QStringView::
 *  toDouble 子集），此处因其为 static 而本文件独立成译码单元故重写。 */
static bool xcv_parseDouble(const char** pp, double* out)
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

/** @brief 数值坐标：十进制数 + 可选 '%'（读作 /100 归一到逻辑 0..1 域；
 *         Qt 仅收裸数，百分比为本库扩展，见头文件）。 */
static bool xcv_parseCoord(const char** pp, double* out)
{
    const char* p = *pp;
    double v;
    if (!xcv_parseDouble(&p, &v)) return false;
    if (*p == '%') {
        v /= 100.0;
        ++p;
    }
    *out = v;
    *pp = p;
    return true;
}

/** @brief 坐标实参整体解析：数值（可带 %）后容忍空白直达段尾，尾部
 *         垃圾（"0px" 的 px 等）整段拒绝。 */
static bool xcv_parseCoordTerm(const char* b, const char* e, double* out)
{
    xcv_trim(&b, &e);
    if (!xcv_parseCoord(&b, out)) return false;
    while (b < e && xcv_isSpace(*b)) ++b;
    return b == e;
}

/** @brief 顶层 ',' 切段扫描：引号段（'/'"）与括号深度内的逗号不切分
 *         （stop 段颜色实参如 rgba(255,0,0,255) 的内嵌逗号保护）。
 *
 *  返回段起点 [start,*termEnd)；*next=下一段起点（越过逗号），末段置
 *  NULL。越界 ')' 按深度下限兜底（容错不崩溃）。 */
static const char* xcv_nextTerm(const char* p, const char* end,
                                const char** termEnd, const char** next)
{
    const char* start = p;
    int depth = 0;
    char quote = 0;
    while (p < end) {
        char c = *p;
        if (quote != '\0') {
            if (c == quote) quote = '\0';
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth > 0) --depth;
        } else if (c == ',' && depth == 0) {
            *termEnd = p;
            *next = p + 1;
            return start;
        }
        ++p;
    }
    *termEnd = end;
    *next = NULL;
    return start;
}

/** @brief 段内首个顶层 ':' 拆 name:value：名字入缓冲（超容量拒绝），
 *         value 段首尾空白收缩且非空；无 ':' 或空 value 返回 false。 */
static bool xcv_splitNameValue(const char* b, const char* e,
                               char* nameBuf, size_t cap,
                               const char** vb, const char** ve)
{
    const char* p = b;
    const char* colon = NULL;
    char quote = '\0';
    while (p < e) {
        char c = *p;
        if (quote != '\0') {
            if (c == quote) quote = '\0';
        } else if (c == '\'' || c == '"') {
            quote = c;
        } else if (c == ':') {
            colon = p;
            break;
        }
        ++p;
    }
    if (!colon) return false;
    {
        const char* nb = b;
        const char* ne = colon;
        xcv_trim(&nb, &ne);
        if (!xcv_copyToken(nb, ne, nameBuf, cap)) return false;
    }
    *vb = colon + 1;
    *ve = e;
    xcv_trim(vb, ve);
    return *vb != *ve;
}

/** @brief 段与关键字等值比较（大小写不敏感、容忍首尾空白；超长即假）。 */
static bool xcv_matchKeyword(const char* b, const char* e, const char* kw)
{
    char buf[16];
    xcv_trim(&b, &e);
    if (!xcv_copyToken(b, e, buf, sizeof(buf))) return false;
    return XStrcasecmp(buf, kw) == 0;
}

/* ==================== 渐变 ==================== */

bool XCssParseGradient(const char* value, XCssGradient* out)
{
    XCssGradient tmp;
    const char* b;
    const char* e;
    const char* p;
    int type = -1;
    bool hasX1 = false;
    bool hasY1 = false;
    bool hasX2 = false;
    bool hasY2 = false;
    bool hasCx = false;
    bool hasCy = false;
    bool hasRadius = false;
    bool hasAngle = false;
    if (!value || !out) return false;
    b = value;
    while (*b && xcv_isSpace(*b)) ++b;
    e = b + XStrlen(b);
    while (e > b && xcv_isSpace(e[-1])) --e;
    if (e - b < 2 || e[-1] != ')') return false;
    --e; /* 去最外层闭括号（渐变实参无引号包裹的 ')'，裸判定即稳）。 */
    if (e - b >= 16 && XStrncasecmp(b, "qlineargradient(", 16) == 0) {
        type = (int)XCssGradient_Linear;
        b += 16;
    } else if (e - b >= 16 && XStrncasecmp(b, "qradialgradient(", 16) == 0) {
        type = (int)XCssGradient_Radial;
        b += 16;
    } else if (e - b >= 17 && XStrncasecmp(b, "qconicalgradient(", 17) == 0) {
        type = (int)XCssGradient_Conical;
        b += 17;
    } else {
        return false;
    }
    XMemset(&tmp, 0, sizeof(tmp)); /* 未用坐标组置 0，语义见头文件。 */
    tmp.m_type = (XCssGradientType)type;
    p = b;
    while (p != NULL) {
        const char* termEnd;
        const char* next;
        const char* tb = xcv_nextTerm(p, e, &termEnd, &next);
        char name[16];
        const char* vb;
        const char* ve;
        p = next;
        if (!xcv_splitNameValue(tb, termEnd, name, sizeof(name), &vb, &ve))
            return false;
        if (XStrcasecmp(name, "stop") == 0) {
            double pos;
            if (tmp.m_stopCount >= (int)(sizeof(tmp.m_stops) /
                                         sizeof(tmp.m_stops[0])))
                return false; /* 超 16 停靠点整串拒绝。 */
            if (!xcv_parseCoord(&vb, &pos)) return false;
            if (!(pos >= 0.0 && pos <= 1.0)) return false; /* 越界拒绝。 */
            while (vb < ve && xcv_isSpace(*vb)) ++vb;
            if (vb == ve) return false; /* 缺颜色实参。 */
            {
                char colorBuf[64];
                if (!xcv_copyToken(vb, ve, colorBuf, sizeof(colorBuf)))
                    return false;
                if (!XCssParseColor(colorBuf,
                                    &tmp.m_stops[tmp.m_stopCount].m_color))
                    return false; /* 颜色解析失败整体拒绝。 */
            }
            tmp.m_stops[tmp.m_stopCount].m_position = pos;
            ++tmp.m_stopCount;
        } else if (XStrcasecmp(name, "spread") == 0) {
            /* pad/reflect/repeat：仅校验（QGradient 默认 PadSpread，无字段）。 */
            if (!xcv_matchKeyword(vb, ve, "pad") &&
                !xcv_matchKeyword(vb, ve, "reflect") &&
                !xcv_matchKeyword(vb, ve, "repeat"))
                return false;
        } else if (XStrcasecmp(name, "coordinatemode") == 0) {
            /* logical/stretchtodevice/objectbounding/object：仅校验。 */
            if (!xcv_matchKeyword(vb, ve, "logical") &&
                !xcv_matchKeyword(vb, ve, "stretchtodevice") &&
                !xcv_matchKeyword(vb, ve, "objectbounding") &&
                !xcv_matchKeyword(vb, ve, "object"))
                return false;
        } else if (XStrcasecmp(name, "fx") == 0 ||
                   XStrcasecmp(name, "fy") == 0) {
            /* 径向焦点：Qt 语法接受；合同结构无字段，校验后丢弃。 */
            double focal;
            if (!xcv_parseCoordTerm(vb, ve, &focal)) return false;
        } else {
            double* slot = NULL;
            bool* flag = NULL;
            if (XStrcasecmp(name, "x1") == 0) {
                slot = &tmp.m_x1;
                flag = &hasX1;
            } else if (XStrcasecmp(name, "y1") == 0) {
                slot = &tmp.m_y1;
                flag = &hasY1;
            } else if (XStrcasecmp(name, "x2") == 0) {
                slot = &tmp.m_x2;
                flag = &hasX2;
            } else if (XStrcasecmp(name, "y2") == 0) {
                slot = &tmp.m_y2;
                flag = &hasY2;
            } else if (XStrcasecmp(name, "cx") == 0) {
                slot = &tmp.m_cx;
                flag = &hasCx;
            } else if (XStrcasecmp(name, "cy") == 0) {
                slot = &tmp.m_cy;
                flag = &hasCy;
            } else if (XStrcasecmp(name, "radius") == 0) {
                slot = &tmp.m_radius;
                flag = &hasRadius;
            } else if (XStrcasecmp(name, "angle") == 0) {
                slot = &tmp.m_angle;
                flag = &hasAngle;
            } else {
                return false; /* 未知参数名。 */
            }
            if (!xcv_parseCoordTerm(vb, ve, slot)) return false;
            *flag = true; /* 同名重复段后写覆盖（对齐 Qt QHash::insert）。 */
        }
    }
    if (tmp.m_stopCount < 1) return false; /* 无停靠点拒绝。 */
    switch (tmp.m_type) {
    case XCssGradient_Linear:
        if (!hasX1 || !hasY1 || !hasX2 || !hasY2) return false;
        break;
    case XCssGradient_Radial:
        if (!hasCx || !hasCy || !hasRadius) return false;
        break;
    case XCssGradient_Conical:
        if (!hasCx || !hasCy || !hasAngle) return false;
        break;
    default:
        return false;
    }
    *out = tmp;
    return true;
}

/* ==================== 调色板角色 ==================== */

#if XPALETTE_ON
/** @brief QSS 角色名 → XPaletteColorRole 对照表（Qt qcssparser values 表
 *         连字符式全小写拼写，覆盖 XPalette.h 枚举全量角色）。 */
static const struct
{
    const char* m_name; /**< QSS 角色名。 */
    int m_role;         /**< 对应 XPaletteColorRole 枚举值。 */
} xcv_paletteRoles[] = {
    { "window-text",       (int)XPaletteColorRole_WindowText },
    { "button",            (int)XPaletteColorRole_Button },
    { "light",             (int)XPaletteColorRole_Light },
    { "midlight",          (int)XPaletteColorRole_Midlight },
    { "dark",              (int)XPaletteColorRole_Dark },
    { "mid",               (int)XPaletteColorRole_Mid },
    { "text",              (int)XPaletteColorRole_Text },
    { "bright-text",       (int)XPaletteColorRole_BrightText },
    { "button-text",       (int)XPaletteColorRole_ButtonText },
    { "base",              (int)XPaletteColorRole_Base },
    { "window",            (int)XPaletteColorRole_Window },
    { "shadow",            (int)XPaletteColorRole_Shadow },
    { "highlight",         (int)XPaletteColorRole_Highlight },
    { "highlighted-text",  (int)XPaletteColorRole_HighlightedText },
    { "link",              (int)XPaletteColorRole_Link },
    { "link-visited",      (int)XPaletteColorRole_LinkVisited },
    { "alternate-base",    (int)XPaletteColorRole_AlternateBase },
    { "no-role",           (int)XPaletteColorRole_NoRole },
    { "tooltip-base",      (int)XPaletteColorRole_ToolTipBase },
    { "tooltip-text",      (int)XPaletteColorRole_ToolTipText },
    { "placeholder-text",  (int)XPaletteColorRole_PlaceholderText },
    { "accent",            (int)XPaletteColorRole_Accent }
};
#endif /* XPALETTE_ON */

bool XCssParsePaletteRole(const char* value, int* roleOut)
{
    const char* b;
    const char* e;
    if (!value || !roleOut) return false;
    b = value;
    while (*b && xcv_isSpace(*b)) ++b;
    e = b + XStrlen(b);
    while (e > b && xcv_isSpace(e[-1])) --e;
    /* "palette(" = 8 字符前缀 + 最短角色 + ')'；空角色由查表兜底拒绝。 */
    if (e - b < 9 || XStrncasecmp(b, "palette(", 8) != 0 || e[-1] != ')')
        return false;
#if XPALETTE_ON
    {
        char roleBuf[32]; /* 最长角色名 "placeholder-text" = 16。 */
        const char* rb = b + 8;
        const char* re = e - 1;
        size_t i;
        xcv_trim(&rb, &re);
        if (!xcv_copyToken(rb, re, roleBuf, sizeof(roleBuf))) return false;
        for (i = 0; i < sizeof(xcv_paletteRoles) / sizeof(xcv_paletteRoles[0]);
             ++i) {
            if (XStrcasecmp(roleBuf, xcv_paletteRoles[i].m_name) == 0) {
                *roleOut = xcv_paletteRoles[i].m_role;
                return true;
            }
        }
    }
#else
    (void)b;
    (void)e;
#endif
    return false;
}

/* ==================== url 路径 ==================== */

bool XCssParseUrlPath(const char* value, char* out, size_t cap)
{
    const char* b;
    const char* e;
    if (!value || !out || cap == 0) return false;
    b = value;
    while (*b && xcv_isSpace(*b)) ++b;
    e = b + XStrlen(b);
    while (e > b && xcv_isSpace(e[-1])) --e;
    /* "url(" = 4 字符前缀 + 最短路径 + ')'。 */
    if (e - b < 6 || XStrncasecmp(b, "url(", 4) != 0 || e[-1] != ')')
        return false;
    {
        const char* pb = b + 4;
        const char* pe = e - 1;
        xcv_trim(&pb, &pe);
        if (pe - pb >= 2 &&
            ((pb[0] == '"' && pe[-1] == '"') ||
             (pb[0] == '\'' && pe[-1] == '\''))) {
            ++pb; /* 剥一层配对引号。 */
            --pe;
            xcv_trim(&pb, &pe); /* 引号内侧空白再收一轮。 */
        }
        if (pb == pe) return false; /* 空路径拒绝。 */
        return xcv_copyToken(pb, pe, out, cap); /* 不足即整体拒绝不截断。 */
    }
}

/* ==================== 背景重复/定位 ==================== */

bool XCssParseBackgroundRepeat(const char* value, XCssRepeatMode* out)
{
    char buf[16]; /* 最长关键字 "no-repeat" = 9。 */
    const char* b;
    const char* e;
    if (!value || !out) return false;
    b = value;
    while (*b && xcv_isSpace(*b)) ++b;
    e = b + XStrlen(b);
    while (e > b && xcv_isSpace(e[-1])) --e;
    if (!xcv_copyToken(b, e, buf, sizeof(buf))) return false;
    if (XStrcasecmp(buf, "repeat") == 0) {
        *out = XCssRepeat_Repeat;
        return true;
    }
    if (XStrcasecmp(buf, "repeat-x") == 0) {
        *out = XCssRepeat_RepeatX;
        return true;
    }
    if (XStrcasecmp(buf, "repeat-y") == 0) {
        *out = XCssRepeat_RepeatY;
        return true;
    }
    if (XStrcasecmp(buf, "no-repeat") == 0) {
        *out = XCssRepeat_NoRepeat;
        return true;
    }
    return false;
}

bool XCssParseBackgroundPosition(const char* value, double* outX, double* outY, bool* xIsPercent, bool* yIsPercent)
{
    const char* b;
    const char* e;
    const char* p;
    int n;
    double axisVal[2];   /* [0]=x 分量值，[1]=y 分量值。 */
    bool axisPct[2];     /* 分量是否百分比语义。 */
    bool has[2];         /* 轴是否已定。 */
    if (!value || !outX || !outY || !xIsPercent || !yIsPercent) return false;
    b = value;
    while (*b && xcv_isSpace(*b)) ++b;
    e = b + XStrlen(b);
    while (e > b && xcv_isSpace(e[-1])) --e;
    has[0] = has[1] = false;
    axisVal[0] = axisVal[1] = 0.0;
    axisPct[0] = axisPct[1] = false;
    n = 0;
    p = b;
    while (p < e) {
        const char* tb;
        const char* te;
        while (p < e && xcv_isSpace(*p)) ++p; /* 跳分量间空白。 */
        if (p >= e) break;
        tb = p;
        while (p < e && !xcv_isSpace(*p)) ++p;
        te = p;
        if (n >= 2) return false; /* 三分量以上拒绝。 */
        {
            char buf[32];
            double v = 0.0;
            bool pct = false;
            int axis = -1; /* -1=轴中立（center/数值）。 */
            if (!xcv_copyToken(tb, te, buf, sizeof(buf))) return false;
            if (XStrcasecmp(buf, "left") == 0) {
                axis = 0; v = 0.0; pct = true;
            } else if (XStrcasecmp(buf, "right") == 0) {
                axis = 0; v = 100.0; pct = true;
            } else if (XStrcasecmp(buf, "top") == 0) {
                axis = 1; v = 0.0; pct = true;
            } else if (XStrcasecmp(buf, "bottom") == 0) {
                axis = 1; v = 100.0; pct = true;
            } else if (XStrcasecmp(buf, "center") == 0) {
                v = 50.0; pct = true;
            } else {
                XCssLength len;
                if (!XCssParseLengthEx(buf, &len)) return false;
                if (len.m_unit == XCssLengthUnit_Percent) {
                    v = len.m_value;
                    pct = true;
                } else {
                    /* px/pt/em/ex 数值原样交消费端换算。 */
                    v = len.m_value;
                    pct = false;
                }
            }
            if (axis >= 0) {
                if (has[axis]) return false; /* 轴冲突（"left right"）。 */
                axisVal[axis] = v;
                axisPct[axis] = pct;
                has[axis] = true;
            } else if (!has[0]) {
                axisVal[0] = v; /* 轴中立按 x→y 落第一个未定轴。 */
                axisPct[0] = pct;
                has[0] = true;
            } else if (!has[1]) {
                axisVal[1] = v;
                axisPct[1] = pct;
                has[1] = true;
            } else {
                return false; /* 双轴已定仍来中立分量（"10px 20px center"）。 */
            }
        }
        ++n;
    }
    if (n < 1) return false; /* 空串拒绝。 */
    if (!has[1]) {
        axisVal[1] = 50.0; /* 缺省轴取 center（百分比语义）。 */
        axisPct[1] = true;
    }
    if (!has[0]) {
        axisVal[0] = 50.0;
        axisPct[0] = true;
    }
    *outX = axisVal[0];
    *outY = axisVal[1];
    *xIsPercent = axisPct[0];
    *yIsPercent = axisPct[1];
    return true;
}

#endif /* XSTYLE_ON */
