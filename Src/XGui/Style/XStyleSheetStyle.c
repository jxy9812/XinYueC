#include "XStyleSheetStyle.h"
#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XFont.h"
#include "XObject.h"

#if XSTYLE_ON

/* ==================== 值解析 ==================== */

/** @brief 16 进制单字符 → 值（-1 非法）。 */
static int xsss_hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 解析颜色（#RGB/#RRGGBB/#AARRGGBB/rgb(r,g,b)，含少量具名色）。 */
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
        int i;
        uint32_t v = 0;
        ++p; --len;
        if (len != 3 && len != 6 && len != 8) return false;
        for (i = 0; i < (int)len; ++i) {
            int d = xsss_hexDigit(p[i]);
            if (d < 0) return false;
            v = (v << 4) | (uint32_t)d;
        }
        if (len == 3) {
            /* #RGB 展开为 #RRGGBB。 */
            uint32_t r = (v >> 8) & 0xF;
            uint32_t g = (v >> 4) & 0xF;
            uint32_t b = v & 0xF;
            *out = 0xFF000000u | (r << 20) | (r << 16) | (g << 12) |
                   (g << 8) | (b << 4) | b;
        } else if (len == 6) {
            *out = 0xFF000000u | v;
        } else {
            *out = v; /* #AARRGGBB。 */
        }
        return true;
    }
    if (XStrncasecmp(p, "rgb", 3) == 0) {
        int r = 0;
        int g = 0;
        int b = 0;
        int a = 255;
        {
            /* rgb(r,g,b)/rgba(r,g,b,a) 手写解析（对标 sscanf 子集）。 */
            const char* q = p + 3;
            if (*q == 'a' || *q == 'A') ++q;
            if (*q == '(') {
                ++q;
                r = (int)XStrtol(q, (char**)&q, 10);
                if (*q == ',') ++q;
                g = (int)XStrtol(q, (char**)&q, 10);
                if (*q == ',') ++q;
                b = (int)XStrtol(q, (char**)&q, 10);
                if ((p[3] == 'a' || p[3] == 'A') && *q == ',') {
                    ++q;
                    a = (int)XStrtol(q, (char**)&q, 10);
                }
            }
        }
        {
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;
            if (a < 0) a = 0; if (a > 255) a = 255;
            *out = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                   ((uint32_t)g << 8) | (uint32_t)b;
            return true;
        }
        return false;
    }
    /* 具名色子集（对标 CSS 基础色）。 */
    {
        static const struct { const char* n; uint32_t v; } names[] = {
            { "black", 0xFF000000u }, { "white", 0xFFFFFFFFu },
            { "red", 0xFFFF0000u }, { "green", 0xFF008000u },
            { "blue", 0xFF0000FFu }, { "gray", 0xFF808080u },
            { "grey", 0xFF808080u }, { "transparent", 0x00000000u },
        };
        size_t i;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (XStrlen(names[i].n) == len &&
                XStrncasecmp(names[i].n, p, len) == 0) {
                *out = names[i].v;
                return true;
            }
        }
    }
    return false;
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

/** @brief 解析长度（"12px"/"12" → 12）。 */
bool XCssParseLength(const char* value, int* out)
{
    if (!value || !out) return false;
    while (*value && XIsSpace((unsigned char)*value)) ++value;
    if (!XIsDigit((unsigned char)*value) && *value != '-' && *value != '+')
        return false;
    {
        /* 前缀解析（对标原 atoi："12px"→12），XStrtol 不要求全串数字。 */
        *out = (int)XStrtol(value, NULL, 10);
        return true;
    }
}

/* ==================== 值/声明查询 ==================== */

/** @brief 在规则内查声明值（!important 优先；同重要性后出现者覆盖，
 *         对标 CSS 级联）。 */
static const XCssDeclaration* xsss_findDecl(const XCssStyleRule* rule,
                                            XCssProperty id)
{
    int i;
    const XCssDeclaration* found = NULL;
    for (i = 0; i < rule->m_declarationCount; ++i) {
        const XCssDeclaration* d = &rule->m_declarations[i];
        if (d->m_propertyId != id) continue;
        if (!found || (d->m_important && !found->m_important) ||
            (d->m_important == found->m_important))
            found = d;
    }
    return found;
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

/** @brief 由 XStyleState 位映射伪类位（用于规则匹配）。 */
static uint32_t xsss_statePseudos(uint32_t state)
{
    uint32_t ps = 0;
    if (state & XStyleState_Enabled) ps |= XCssPseudo_Enabled;
    else ps |= XCssPseudo_Disabled;
    if (state & XStyleState_MouseOver) ps |= XCssPseudo_Hover;
    if (state & XStyleState_Sunken) ps |= XCssPseudo_Pressed;
    if (state & XStyleState_HasFocus) ps |= XCssPseudo_Focus;
    if (state & XStyleState_On) ps |= XCssPseudo_Checked;
    if (state & XStyleState_Selected) ps |= XCssPseudo_Selected;
    return ps;
}

/** @brief 属性选择器匹配（[name] 存在 / [name=value] 相等）。
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

/** @brief 单段基础选择器匹配（元素名=类名、#id=objectName、伪类、属性）。 */
static bool xsss_basicMatches(const XCssBasicSelector* sel,
                              const XObject* obj, uint32_t statePseudos)
{
    const char* cls;
    const char* id;
    if (!sel) return false;
    if (sel->m_elementName) {
        const char* want = XString_toUtf8(sel->m_elementName);
        cls = xsss_className(obj);
        if (!cls || !want || XStrcasecmp(cls, want) != 0) return false;
    }
    if (sel->m_id) {
        const char* want = XString_toUtf8(sel->m_id);
        id = xsss_objectName(obj);
        if (!id || !want || XStrcmp(id, want) != 0) return false;
    }
    if (sel->m_pseudoClasses &&
        (sel->m_pseudoClasses & statePseudos) != sel->m_pseudoClasses)
        return false;
    return xsss_attrMatches(&sel->m_attribute, obj);
}

/** @brief 选择器链匹配（对标 CSS 关系选择器）。
 *
 *  末段在 obj 上匹配；前段按 relationToPrev 沿 XObject parent 链回溯
 *  （Ancestor=任意祖先，Parent=直接父）。 */
static bool xsss_selectorMatches(const XCssSelector* sel, const XObject* obj,
                                 uint32_t statePseudos)
{
    const XObject* cur;
    int idx;
    if (!sel || sel->m_basicCount <= 0) return false;
    if (!xsss_basicMatches(&sel->m_basics[sel->m_basicCount - 1], obj,
                           statePseudos))
        return false;
    cur = obj;
    for (idx = sel->m_basicCount - 2; idx >= 0; --idx) {
        XCssRelation rel = sel->m_basics[idx + 1].m_relationToPrev;
        const XObject* parent = XObject_parent(cur);
        bool matched = false;
        if (rel == XCssRelation_Parent) {
            if (parent &&
                xsss_basicMatches(&sel->m_basics[idx], parent,
                                  xsss_statePseudos(0)))
                matched = true;
            cur = parent;
        } else if (rel == XCssRelation_Ancestor) {
            const XObject* a = parent;
            while (a) {
                if (xsss_basicMatches(&sel->m_basics[idx], a,
                                      xsss_statePseudos(0))) {
                    matched = true;
                    break;
                }
                a = XObject_parent(a);
            }
            cur = parent;
            if (matched && a) cur = a;
        } else {
            /* None：同级顺序无关，按父匹配一次。 */
            if (parent &&
                xsss_basicMatches(&sel->m_basics[idx], parent,
                                  xsss_statePseudos(0)))
                matched = true;
            cur = parent;
        }
        if (!matched) return false;
    }
    return true;
}

/**
 * @brief 在样式表中查最优先命中的声明（!important > 特异度 > 后定义；
 *        对标 CSS 级联）。带单槽渲染规则缓存：命中 (对象,状态) 时直接
 *        复用最高特异度规则，未命中时全表扫描并回填缓存。
 */
static const XCssDeclaration* xsss_lookup(XStyleSheetStyle* self,
                                          const XObject* obj, uint32_t state,
                                          XCssProperty id)
{
    const XCssStyleSheet* sheet;
    int ri;
    int si;
    const XCssDeclaration* best = NULL;
    int bestImportance = -1;
    int bestSpec = -1;
    int bestIndex = -1;
    uint32_t statePseudos;
    const XCssStyleRule* cacheRule = NULL;
    const XCssSelector* cacheSel = NULL;
    int cacheSpec = -1;
    if (!self) return NULL;
    sheet = &self->m_sheet;
    if (!sheet || sheet->m_ruleCount == 0) return NULL;
    statePseudos = xsss_statePseudos(state);
    /* 缓存命中：同一 (对象,状态) 直接复用最高特异度规则。 */
    if (self->m_cacheValid && self->m_cacheObj == obj &&
        self->m_cacheState == state) {
        const XCssDeclaration* d = self->m_cacheRule
            ? xsss_findDecl(self->m_cacheRule, id) : NULL;
        if (d) return d;
        cacheRule = self->m_cacheRule;
        cacheSel = self->m_cacheSel;
        cacheSpec = self->m_cacheSpec;
        /* 缓存规则未声明该属性时回落全表扫描（缓存本身仍有效）。 */
    }
    for (ri = 0; ri < sheet->m_ruleCount; ++ri) {
        const XCssStyleRule* rule = &sheet->m_rules[ri];
        for (si = 0; si < rule->m_selectorCount; ++si) {
            const XCssSelector* sel = &rule->m_selectors[si];
            const XCssDeclaration* d;
            if (!xsss_selectorMatches(sel, obj, statePseudos))
                continue;
            d = xsss_findDecl(rule, id);
            if (d) {
                int importance = d->m_important ? 1 : 0;
                if (importance > bestImportance ||
                    (importance == bestImportance &&
                     (sel->m_specificity > bestSpec ||
                      (sel->m_specificity == bestSpec && ri > bestIndex)))) {
                    best = d;
                    bestImportance = importance;
                    bestSpec = sel->m_specificity;
                    bestIndex = ri;
                }
            }
            /* 回填最高特异度规则（无论是否声明目标属性）。 */
            if (!cacheRule || sel->m_specificity > cacheSpec) {
                cacheRule = rule;
                cacheSel = sel;
                cacheSpec = sel->m_specificity;
            }
        }
    }
    self->m_cacheObj = obj;
    self->m_cacheState = state;
    self->m_cacheRule = cacheRule;
    self->m_cacheSel = cacheSel;
    self->m_cacheSpec = cacheSpec;
    self->m_cacheValid = true;
    return best;
}

/* ==================== 底层样式回落 ==================== */

/** @brief 底层样式（显式设置或全局默认）。 */
static XStyle* xsss_source(XStyleSheetStyle* self)
{
    if (self->m_source) return self->m_source;
    return XStyle_defaultStyle();
}

/* ==================== 绘制分派 ==================== */

/** @brief 应用文本色覆盖（绘制前：影响底层文本渲染色）。
 *
 *  对标 QSS 级联：命中 color 时覆盖 option 文本色后交底层绘制；
 *  背景色在底层绘制之后单独覆盖（见 xsss_applyBackground）。
 */
static void xsss_applyTextColor(XStyleSheetStyle* self, const XObject* obj,
                                XStyleOption* option)
{
    const XCssDeclaration* fg;
    uint32_t color;
    if (!self || !option) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    fg = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_Color);
    if (fg && fg->m_value &&
        XCssParseColor(XString_toUtf8(fg->m_value), &color))
        option->m_textColor = color;
}

/** @brief 应用字体覆盖（绘制前：font-family/font-size 命中时重设
 *         画家字体，对标 QSS font 属性）。 */
static void xsss_applyFont(XStyleSheetStyle* self, const XObject* obj,
                           XStyleOption* option, XPainter* painter)
{
    const XCssDeclaration* family;
    const XCssDeclaration* size;
    const XCssDeclaration* weight;
    const XCssDeclaration* ital;
    const XCssDeclaration* fontShorthand;
    XFont font;
    bool changed = false;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    family = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_FontFamily);
    /* 基准字体：画家当前字体（底层绘制会再按控件字体覆盖）。 */
    {
        XFont* cur = XPainter_font(painter);
        if (!cur) return;
        font = *cur;
    }
    size = xsss_lookup(self, obj, option->m_state,
                       XCssProperty_FontSize);
    weight = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_FontWeight);
    ital = xsss_lookup(self, obj, option->m_state,
                       XCssProperty_FontStyle);
    fontShorthand = xsss_lookup(self, obj, option->m_state,
                                XCssProperty_Font);
    if (family && family->m_value) {
        const char* f = XString_toUtf8(family->m_value);
        if (f && f[0]) {
            XFont_setFamily(&font, f);
            changed = true;
        }
    }
    if (size && size->m_value) {
        int px = 0;
        if (XCssParseLength(XString_toUtf8(size->m_value), &px) && px > 0) {
            XFont_setPixelSize(&font, px);
            changed = true;
        }
    }
    if (weight && weight->m_value) {
        const char* w = XString_toUtf8(weight->m_value);
        if (XStrcasecmp(w, "bold") == 0) {
            XFont_setBold(&font, true);
            changed = true;
        } else if (XStrcasecmp(w, "normal") == 0) {
            XFont_setBold(&font, false);
            changed = true;
        } else {
            int v = (int)XStrtol(w, NULL, 10);
            if (v >= 100 && v <= 900) {
                XFont_setWeight(&font, v);
                changed = true;
            }
        }
    }
    if (ital && ital->m_value) {
        const char* s = XString_toUtf8(ital->m_value);
        if (XStrcasecmp(s, "italic") == 0 || XStrcasecmp(s, "oblique") == 0) {
            XFont_setItalic(&font, true);
            changed = true;
        } else if (XStrcasecmp(s, "normal") == 0) {
            XFont_setItalic(&font, false);
            changed = true;
        }
    }
    /* font 简写："italic bold 12px Serif" → 样式/字重/字号/字族。
     * 解析：字号为带 px 的数字 token，非数字 token 中最后一段为字族。 */
    if (fontShorthand && fontShorthand->m_value) {
        const char* v = XString_toUtf8(fontShorthand->m_value);
        const char* last = NULL;
        int sizePx = 0;
        while (v && *v) {
            const char* tok = v;
            while (*v && !XIsSpace((unsigned char)*v)) ++v;
            if (v > tok) {
                size_t tl = (size_t)(v - tok);
                char buf[32];
                if (tl < sizeof(buf)) {
                    XMemcpy(buf, tok, tl);
                    buf[tl] = '\0';
                    if (XStrcasecmp(buf, "bold") == 0) {
                        XFont_setBold(&font, true);
                        changed = true;
                    } else if (XStrcasecmp(buf, "italic") == 0 ||
                               XStrcasecmp(buf, "oblique") == 0) {
                        XFont_setItalic(&font, true);
                        changed = true;
                    } else if (XStrcasecmp(buf, "normal") == 0) {
                        changed = true;
                    } else if (XCssParseLength(buf, &sizePx) && sizePx > 0) {
                        XFont_setPixelSize(&font, sizePx);
                        changed = true;
                    } else {
                        last = tok;
                    }
                }
            }
            while (*v && XIsSpace((unsigned char)*v)) ++v;
        }
        if (last) {
            XFont_setFamily(&font, last);
            changed = true;
        }
    }
    if (changed) XPainter_setFont(painter, &font);
}

/** @brief 应用文本装饰（绘制前：underline/overline/line-through 设置到
 *         画家字体——完整对标 Qt setTextDecorationFromValues → QFont）。
 *
 *  Qt 语义：text-decoration 作用于 QFont 的 underline/overline/strikeOut，
 *  由文本渲染器绘制装饰线，而非在矩形上补画线。
 */
static void xsss_applyTextDecoration(XStyleSheetStyle* self,
                                     const XObject* obj,
                                     XStyleOption* option,
                                     XPainter* painter)
{
    const XCssDeclaration* d;
    const char* v;
    XFont* cur;
    XFont font;
    bool has = false;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    d = xsss_lookup(self, obj, option->m_state,
                    XCssProperty_TextDecoration);
    if (!d || !d->m_value) return;
    v = XString_toUtf8(d->m_value);
    if (!v) return;
    cur = XPainter_font(painter);
    if (!cur) return;
    font = *cur;
    if (XStrcasecmp(v, "underline") == 0) {
        XFont_setUnderline(&font, true);
        has = true;
    } else if (XStrcasecmp(v, "line-through") == 0) {
        XFont_setStrikeOut(&font, true);
        has = true;
    } else if (XStrcasecmp(v, "overline") == 0) {
        XFont_setOverline(&font, true);
        has = true;
    } else if (XStrcasecmp(v, "none") == 0) {
        XFont_setUnderline(&font, false);
        XFont_setStrikeOut(&font, false);
        XFont_setOverline(&font, false);
        has = true;
    }
    if (has) XPainter_setFont(painter, &font);
}

/** @brief 应用背景色覆盖（绘制后：QSS 背景优先于底层面板填充）。 */
static void xsss_applyBackground(XStyleSheetStyle* self, const XObject* obj,
                                 const XStyleOption* option,
                                 XPainter* painter, const XRect* boxRect)
{
    const XCssDeclaration* bg;
    uint32_t color;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    bg = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BackgroundColor);
    if (!bg)
        bg = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_Background);
    if (bg && bg->m_value &&
        XCssParseColor(XString_toUtf8(bg->m_value), &color))
        XPainter_fillRect(painter, boxRect ? boxRect : &option->m_rect,
                          color);
}

/** @brief 查询盒模型内边距（padding 系列声明；CSS 四值简写展开）。 */
static void xsss_padding(XStyleSheetStyle* self, const XObject* obj,
                         uint32_t state, int out[4])
{
    const XCssDeclaration* d;
    int v = 0;
    out[0] = out[1] = out[2] = out[3] = 0; /* 左/上/右/下。 */
    d = xsss_lookup(self, obj, state, XCssProperty_Padding);
    if (d && d->m_value &&
        XCssParseLength(XString_toUtf8(d->m_value), &v)) {
        /* 简写：单值四边。 */
        out[0] = out[1] = out[2] = out[3] = v;
        return;
    }
    d = xsss_lookup(self, obj, state, XCssProperty_PaddingLeft);
    if (d && d->m_value) XCssParseLength(XString_toUtf8(d->m_value), &out[0]);
    d = xsss_lookup(self, obj, state, XCssProperty_PaddingTop);
    if (d && d->m_value) XCssParseLength(XString_toUtf8(d->m_value), &out[1]);
    d = xsss_lookup(self, obj, state, XCssProperty_PaddingRight);
    if (d && d->m_value) XCssParseLength(XString_toUtf8(d->m_value), &out[2]);
    d = xsss_lookup(self, obj, state, XCssProperty_PaddingBottom);
    if (d && d->m_value) XCssParseLength(XString_toUtf8(d->m_value), &out[3]);
}

/** @brief 查询盒模型外边距（margin 系列声明；CSS 四值简写展开）。 */
static void xsss_margin(XStyleSheetStyle* self, const XObject* obj,
                        uint32_t state, int out[4])
{
    const XCssDeclaration* d;
    int v = 0;
    out[0] = out[1] = out[2] = out[3] = 0; /* 左/上/右/下。 */
    d = xsss_lookup(self, obj, state, XCssProperty_Margin);
    if (d && d->m_value &&
        XCssParseLength(XString_toUtf8(d->m_value), &v)) {
        out[0] = out[1] = out[2] = out[3] = v;
        return;
    }
    if (d && d->m_value) {
        /* 简写多值展开：1 值四边 / 2 值 上下/左右 / 4 值 上右下左。 */
        const char* vstr = XString_toUtf8(d->m_value);
        int vals[4] = { 0, 0, 0, 0 };
        int n = 0;
        while (vstr && *vstr && n < 4) {
            int val;
            const char* tok = vstr;
            while (*vstr && !XIsSpace((unsigned char)*vstr)) ++vstr;
            {
                char buf[32];
                size_t tl = (size_t)(vstr - tok);
                if (tl > 0 && tl < sizeof(buf)) {
                    XMemcpy(buf, tok, tl);
                    buf[tl] = '\0';
                    if (XCssParseLength(buf, &val)) vals[n++] = val;
                }
            }
            while (*vstr && XIsSpace((unsigned char)*vstr)) ++vstr;
        }
        if (n == 1) {
            out[0] = out[1] = out[2] = out[3] = vals[0];
        } else if (n == 2) {
            out[0] = out[2] = vals[0];
            out[1] = out[3] = vals[1];
        } else if (n == 4) {
            out[0] = vals[0]; /* 上 */
            out[1] = vals[1]; /* 右 */
            out[2] = vals[2]; /* 下 */
            out[3] = vals[3]; /* 左 */
        }
    }
}

/** @brief 绘制 QSS 边框（border-style/width/color/radius 命中时；
 *         solid/dashed/dotted/none 全支持，对标 qcss 的 BorderStyle）。 */
static void xsss_drawBorder(XStyleSheetStyle* self, const XObject* obj,
                            const XStyleOption* option, XPainter* painter,
                            const XRect* boxRect)
{
    const XCssDeclaration* bw;
    const XCssDeclaration* bc;
    const XCssDeclaration* br;
    const XCssDeclaration* bs;
    int width = 0;
    uint32_t color = 0; /* 0=未指定；简写扫描或回落文本色。 */
    int radius = 0;
    int style = 0; /* 0 solid / 1 dashed / 2 dotted / -1 none。 */
    XRect r;
    if (!self || !option || !painter) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    bw = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderWidth);
    if (!bw)
        bw = xsss_lookup(self, obj, option->m_state,
                         XCssProperty_Border);
    if (bw && bw->m_value)
        XCssParseLength(XString_toUtf8(bw->m_value), &width);
    bc = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderColor);
    if (bc && bc->m_value)
        XCssParseColor(XString_toUtf8(bc->m_value), &color);
    bs = xsss_lookup(self, obj, option->m_state,
                     XCssProperty_BorderStyle);
    if (bs && bs->m_value) {
        const char* v = XString_toUtf8(bs->m_value);
        if (XStrcasecmp(v, "none") == 0) return;
        if (XStrcasecmp(v, "dashed") == 0) style = 1;
        else if (XStrcasecmp(v, "dotted") == 0) style = 2;
    }
    /* border 简写（"2px solid #FF0000"）：提取颜色 token 与风格关键字
     * （对标 CSS border 简写的组成解析）。 */
    if (bw && bw->m_value) {
        const char* v = XString_toUtf8(bw->m_value);
        while (v && *v) {
            uint32_t token = 0;
            const char* tok = v;
            while (*v && !XIsSpace((unsigned char)*v)) ++v;
            if (color == 0 &&
                XCssParseColorToken(tok, (size_t)(v - tok), &token)) {
                color = token;
            } else {
                size_t tl = (size_t)(v - tok);
                if (tl == 5 && XStrncasecmp(tok, "solid", 5) == 0) {
                    style = 0;
                } else if (tl == 6 && XStrncasecmp(tok, "dashed", 6) == 0) {
                    style = 1;
                } else if (tl == 6 && XStrncasecmp(tok, "dotted", 6) == 0) {
                    style = 2;
                } else if (tl == 4 && XStrncasecmp(tok, "none", 4) == 0) {
                    return;
                }
            }
            while (*v && XIsSpace((unsigned char)*v)) ++v;
        }
    }
    if (width <= 0) return;
    /* 设置画笔色（否则沿用上一绘制状态的画笔色）。 */
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
    if (br && br->m_value)
        XCssParseLength(XString_toUtf8(br->m_value), &radius);
#if XPAINTER_SHAPE_ON
    if (radius > 0) {
        XPainter_drawRoundedRect(painter, &r, radius, radius);
        return;
    }
#endif
    if (color == 0) {
        /* CSS 语义：border 无色时回落当前颜色（对标 border 默认
         * currentColor）。 */
        const XCssDeclaration* fg = xsss_lookup(self, obj,
                                               option->m_state,
                                               XCssProperty_Color);
        color = (fg && fg->m_value &&
                 XCssParseColor(XString_toUtf8(fg->m_value), &color))
            ? color
            : (option->m_textColor
                   ? option->m_textColor : 0xFF000000u);
    }
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
 *         对标 QStyleSheetStyle 的 box 计算）。 */
static void xsss_applyBoxModel(XStyleSheetStyle* self, const XObject* obj,
                               XStyleOption* option)
{
    int pad[4];
    int mar[4];
    if (!self || !option) return;
    if (self->m_sheet.m_ruleCount == 0) return;
    xsss_padding(self, obj, option->m_state, pad);
    xsss_margin(self, obj, option->m_state, mar);
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
                          uint32_t state, const XRect* rect)
{
    XRect box = *rect;
    int mar[4];
    xsss_margin(self, obj, state, mar);
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
        opt = *option;
        xsss_applyTextColor(ss, (const XObject*)widget, &opt);
        xsss_applyFont(ss, (const XObject*)widget, &opt, painter);
        xsss_applyTextDecoration(ss, (const XObject*)widget, &opt, painter);
        xsss_applyBoxModel(ss, (const XObject*)widget, &opt);
    }
    if (src && src != self)
        XStyle_drawPrimitive(src, pe, option ? &opt : option, painter,
                             widget);
    if (option) {
        XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                 option->m_state, &option->m_rect);
        xsss_applyBackground(ss, (const XObject*)widget, option, painter,
                             &box);
        xsss_drawBorder(ss, (const XObject*)widget, option, painter, &box);
    }
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
        opt = *option;
        xsss_applyTextColor(ss, (const XObject*)widget, &opt);
        xsss_applyFont(ss, (const XObject*)widget, &opt, painter);
        xsss_applyTextDecoration(ss, (const XObject*)widget, &opt, painter);
        xsss_applyBoxModel(ss, (const XObject*)widget, &opt);
    }
    if (src && src != self)
        XStyle_drawControl(src, ce, option ? &opt : option, painter,
                           widget);
    if (option) {
        XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                 option->m_state, &option->m_rect);
        xsss_applyBackground(ss, (const XObject*)widget, option, painter,
                             &box);
        xsss_drawBorder(ss, (const XObject*)widget, option, painter, &box);
    }
}

static void VXStyleSheetStyle_drawComplexControl(XStyle* self, int cc,
                                                 const XStyleOption* option,
                                                 XPainter* painter,
                                                 const XWidget* widget)
{
    XStyleSheetStyle* ss = (XStyleSheetStyle*)self;
    XStyle* src = xsss_source(ss);
    XStyleOption opt;
    if (option) {
        opt = *option;
        xsss_applyTextColor(ss, (const XObject*)widget, &opt);
        xsss_applyFont(ss, (const XObject*)widget, &opt, painter);
        xsss_applyTextDecoration(ss, (const XObject*)widget, &opt, painter);
        xsss_applyBoxModel(ss, (const XObject*)widget, &opt);
    }
    if (src && src != self)
        XStyle_drawComplexControl(src, cc, option ? &opt : option, painter,
                                  widget);
    if (option) {
        XRect box = xsss_boxRect(ss, (const XObject*)widget,
                                 option->m_state, &option->m_rect);
        xsss_applyBackground(ss, (const XObject*)widget, option, painter,
                             &box);
        xsss_drawBorder(ss, (const XObject*)widget, option, painter, &box);
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
    const XCssDeclaration* d;
    int v;
    int pad[4];
    int mar[4];
    const XObject* obj = option ? (const XObject*)option->m_styleObject
                                : NULL;
    uint32_t state = option ? option->m_state : 0;
    if (src && src != self)
        size = XStyle_sizeFromContents(src, ct, option, contentSize);
    else
        size = contentSize;
    if (!ss || ss->m_sheet.m_ruleCount == 0) return size;
    /* width/height 覆盖。 */
    d = xsss_lookup(ss, obj, state, XCssProperty_Width);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v))
        size.width = v;
    d = xsss_lookup(ss, obj, state, XCssProperty_Height);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v))
        size.height = v;
    /* min/max 夹取。 */
    d = xsss_lookup(ss, obj, state, XCssProperty_MinWidth);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v) &&
        size.width < v)
        size.width = v;
    d = xsss_lookup(ss, obj, state, XCssProperty_MinHeight);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v) &&
        size.height < v)
        size.height = v;
    d = xsss_lookup(ss, obj, state, XCssProperty_MaxWidth);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v) &&
        size.width > v)
        size.width = v;
    d = xsss_lookup(ss, obj, state, XCssProperty_MaxHeight);
    if (d && d->m_value && XCssParseLength(XString_toUtf8(d->m_value), &v) &&
        size.height > v)
        size.height = v;
    /* margin + padding 外扩。 */
    xsss_padding(ss, obj, state, pad);
    xsss_margin(ss, obj, state, mar);
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
    XCssStyleSheet_clear(&self->m_sheet);
    if (self->m_sourceOwned && self->m_source) {
        XStyle_delete_base(self->m_source);
        self->m_source = NULL;
        self->m_sourceOwned = false;
    }
    XClass_Deinit_Parent(XWindowsStyle, (XWindowsStyle*)self);
}

bool XStyleSheetStyle_setStyleSheet(XStyleSheetStyle* self, const char* css)
{
    bool ok;
    if (!self) return false;
    ok = XCssStyleSheet_parse(&self->m_sheet, css);
    /* 规则表变化 → 渲染规则缓存失效。 */
    self->m_cacheValid = false;
    self->m_cacheObj = NULL;
    self->m_cacheRule = NULL;
    self->m_cacheSel = NULL;
    self->m_cacheSpec = -1;
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
        XStyle_delete_base(self->m_source);
    self->m_source = source;
    self->m_sourceOwned = (source != NULL);
    /* 对标 QStyleSheetStyle：被包装样式即代理样式（QStyle::proxy）。 */
    ((XStyle*)self)->m_proxy = source;
}

int XStyleSheetStyle_ruleCount(const XStyleSheetStyle* self)
{
    return self ? self->m_sheet.m_ruleCount : 0;
}

#endif /* XSTYLE_ON */
