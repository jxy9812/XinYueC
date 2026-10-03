#include "XCssStyleSheet.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XMemory.h"

#if XSTYLE_ON

/* ==================== 属性名表（对标 qcssparser 的 findKnownValue） ====================
 *
 * 名表内容=Qt 6.8.3 qcssparser.cpp properties[] 全集的逐名转录（120 名，
 * 含 -qt-* 私有项），名集/名序/名→ID 映射经 v6.8.3 原文逐名核对一致。
 * 口径：排序去重表、精确匹配——原表字母序且无重复键（NumProperties-1
 * 条），Qt 查找为 lower_bound 二分 + 等值比较（大小写敏感，非表内精确
 * 名一律 Unknown）；本库按原表序线性扫、大小写不敏感（已声明偏差：表内
 * 精确名两者结果恒一致，大写变体名本库可识别而 Qt 判 Unknown，库内样式
 * 表与回归用例均小写，不受该偏差影响）。未识别属性按 Qt 存 Unknown +
 * 原名。
 */

/** @brief 属性名/ID 映射表（对标 QCssKnownValue properties[]，6.8.3 全集）。 */
static const struct
{
    const char* name;
    XCssProperty id;
} k_propNames[] = {
    { "-qt-background-role", XCssProperty_QtBackgroundRole },
    { "-qt-block-indent", XCssProperty_QtBlockIndent },
    { "-qt-fg-texture-cachekey", XCssProperty_QtForegroundTextureCacheKey },
    { "-qt-foreground", XCssProperty_QtForeground },
    { "-qt-line-height-type", XCssProperty_QtLineHeightType },
    { "-qt-list-indent", XCssProperty_QtListIndent },
    { "-qt-list-number-prefix", XCssProperty_QtListNumberPrefix },
    { "-qt-list-number-suffix", XCssProperty_QtListNumberSuffix },
    { "-qt-paragraph-type", XCssProperty_QtParagraphType },
    { "-qt-stroke-color", XCssProperty_QtStrokeColor },
    { "-qt-stroke-dasharray", XCssProperty_QtStrokeDashArray },
    { "-qt-stroke-dashoffset", XCssProperty_QtStrokeDashOffset },
    { "-qt-stroke-linecap", XCssProperty_QtStrokeLineCap },
    { "-qt-stroke-linejoin", XCssProperty_QtStrokeLineJoin },
    { "-qt-stroke-miterlimit", XCssProperty_QtStrokeMiterLimit },
    { "-qt-stroke-width", XCssProperty_QtStrokeWidth },
    { "-qt-style-features", XCssProperty_QtStyleFeatures },
    { "-qt-table-type", XCssProperty_QtTableType },
    { "-qt-user-state", XCssProperty_QtUserState },
    { "accent-color", XCssProperty_QtAccent },
    { "alternate-background-color", XCssProperty_AlternateBackgroundColor },
    { "background", XCssProperty_Background },
    { "background-attachment", XCssProperty_BackgroundAttachment },
    { "background-clip", XCssProperty_BackgroundClip },
    { "background-color", XCssProperty_BackgroundColor },
    { "background-image", XCssProperty_BackgroundImage },
    { "background-origin", XCssProperty_BackgroundOrigin },
    { "background-position", XCssProperty_BackgroundPosition },
    { "background-repeat", XCssProperty_BackgroundRepeat },
    { "border", XCssProperty_Border },
    { "border-bottom", XCssProperty_BorderBottom },
    { "border-bottom-color", XCssProperty_BorderBottomColor },
    { "border-bottom-left-radius", XCssProperty_BorderBottomLeftRadius },
    { "border-bottom-right-radius", XCssProperty_BorderBottomRightRadius },
    { "border-bottom-style", XCssProperty_BorderBottomStyle },
    { "border-bottom-width", XCssProperty_BorderBottomWidth },
    { "border-collapse", XCssProperty_BorderCollapse },
    { "border-color", XCssProperty_BorderColor },
    { "border-image", XCssProperty_BorderImage },
    { "border-left", XCssProperty_BorderLeft },
    { "border-left-color", XCssProperty_BorderLeftColor },
    { "border-left-style", XCssProperty_BorderLeftStyle },
    { "border-left-width", XCssProperty_BorderLeftWidth },
    { "border-radius", XCssProperty_BorderRadius },
    { "border-right", XCssProperty_BorderRight },
    { "border-right-color", XCssProperty_BorderRightColor },
    { "border-right-style", XCssProperty_BorderRightStyle },
    { "border-right-width", XCssProperty_BorderRightWidth },
    { "border-style", XCssProperty_BorderStyle },
    { "border-top", XCssProperty_BorderTop },
    { "border-top-color", XCssProperty_BorderTopColor },
    { "border-top-left-radius", XCssProperty_BorderTopLeftRadius },
    { "border-top-right-radius", XCssProperty_BorderTopRightRadius },
    { "border-top-style", XCssProperty_BorderTopStyle },
    { "border-top-width", XCssProperty_BorderTopWidth },
    { "border-width", XCssProperty_BorderWidth },
    { "bottom", XCssProperty_Bottom },
    { "color", XCssProperty_Color },
    { "float", XCssProperty_Float },
    { "font", XCssProperty_Font },
    { "font-family", XCssProperty_FontFamily },
    { "font-kerning", XCssProperty_FontKerning },
    { "font-size", XCssProperty_FontSize },
    { "font-style", XCssProperty_FontStyle },
    { "font-variant", XCssProperty_FontVariant },
    { "font-weight", XCssProperty_FontWeight },
    { "height", XCssProperty_Height },
    { "icon", XCssProperty_QtIcon },
    { "image", XCssProperty_QtImage },
    { "image-position", XCssProperty_QtImageAlignment },
    { "left", XCssProperty_Left },
    { "letter-spacing", XCssProperty_LetterSpacing },
    { "line-height", XCssProperty_LineHeight },
    { "list-style", XCssProperty_ListStyle },
    { "list-style-type", XCssProperty_ListStyleType },
    { "margin", XCssProperty_Margin },
    { "margin-bottom", XCssProperty_MarginBottom },
    { "margin-left", XCssProperty_MarginLeft },
    { "margin-right", XCssProperty_MarginRight },
    { "margin-top", XCssProperty_MarginTop },
    { "max-height", XCssProperty_MaxHeight },
    { "max-width", XCssProperty_MaxWidth },
    { "min-height", XCssProperty_MinHeight },
    { "min-width", XCssProperty_MinWidth },
    { "outline", XCssProperty_Outline },
    { "outline-bottom-left-radius", XCssProperty_OutlineBottomLeftRadius },
    { "outline-bottom-right-radius", XCssProperty_OutlineBottomRightRadius },
    { "outline-color", XCssProperty_OutlineColor },
    { "outline-offset", XCssProperty_OutlineOffset },
    { "outline-radius", XCssProperty_OutlineRadius },
    { "outline-style", XCssProperty_OutlineStyle },
    { "outline-top-left-radius", XCssProperty_OutlineTopLeftRadius },
    { "outline-top-right-radius", XCssProperty_OutlineTopRightRadius },
    { "outline-width", XCssProperty_OutlineWidth },
    { "padding", XCssProperty_Padding },
    { "padding-bottom", XCssProperty_PaddingBottom },
    { "padding-left", XCssProperty_PaddingLeft },
    { "padding-right", XCssProperty_PaddingRight },
    { "padding-top", XCssProperty_PaddingTop },
    { "page-break-after", XCssProperty_PageBreakAfter },
    { "page-break-before", XCssProperty_PageBreakBefore },
    { "placeholder-text-color", XCssProperty_QtPlaceholderTextColor },
    { "position", XCssProperty_Position },
    { "right", XCssProperty_Right },
    { "selection-background-color", XCssProperty_SelectionBackgroundColor },
    { "selection-color", XCssProperty_SelectionColor },
    { "spacing", XCssProperty_QtSpacing },
    { "subcontrol-origin", XCssProperty_QtOrigin },
    { "subcontrol-position", XCssProperty_QtPosition },
    { "text-align", XCssProperty_TextAlignment },
    { "text-decoration", XCssProperty_TextDecoration },
    { "text-decoration-color", XCssProperty_TextDecorationColor },
    { "text-indent", XCssProperty_TextIndent },
    { "text-transform", XCssProperty_TextTransform },
    { "text-underline-style", XCssProperty_TextUnderlineStyle },
    { "top", XCssProperty_Top },
    { "vertical-align", XCssProperty_VerticalAlignment },
    { "white-space", XCssProperty_Whitespace },
    { "width", XCssProperty_Width },
    { "word-spacing", XCssProperty_WordSpacing }
};

const char* XCssProperty_name(XCssProperty id)
{
    size_t i;
    for (i = 0; i < sizeof(k_propNames) / sizeof(k_propNames[0]); ++i)
        if (k_propNames[i].id == id) return k_propNames[i].name;
    return NULL;
}

/** @brief 属性名 → ID（大小写不敏感；未识别返回 Unknown）。 */
static XCssProperty xcss_findProperty(const char* name, size_t len)
{
    size_t i;
    for (i = 0; i < sizeof(k_propNames) / sizeof(k_propNames[0]); ++i) {
        const char* n = k_propNames[i].name;
        if (XStrlen(n) == len && XStrncasecmp(n, name, len) == 0)
            return k_propNames[i].id;
    }
    return XCssProperty_Unknown;
}

/* ==================== 生命周期 ==================== */

void XCssStyleSheet_init(XCssStyleSheet* sheet)
{
    if (!sheet) return;
    XMemset(sheet, 0, sizeof(*sheet));
}

/** @brief 释放属性选择器的两个字符串（不对数组元素误伤）。 */
static void xcss_freeAttr(XCssAttributeSelector* a)
{
    if (!a) return;
    if (a->m_name) XString_delete_base(a->m_name);
    if (a->m_value) XString_delete_base(a->m_value);
    XMemset(a, 0, sizeof(*a));
}

/** @brief 释放基础选择器自身拥有的全部资源（元素名/ID 列表/属性列表/
 *         伪元素名；clear 与解析失败回滚共用）。 */
static void xcss_freeBasic(XCssBasicSelector* b)
{
    int i;
    if (!b) return;
    if (b->m_elementName) XString_delete_base(b->m_elementName);
    if (b->m_id) XString_delete_base(b->m_id);
    for (i = 0; i < b->m_idCount; ++i)
        if (b->m_ids[i]) XString_delete_base(b->m_ids[i]);
    if (b->m_ids) XFree_System(b->m_ids);
    xcss_freeAttr(&b->m_attribute);
    for (i = 0; i < b->m_attributeCount; ++i)
        xcss_freeAttr(&b->m_attributes[i]);
    if (b->m_attributes) XFree_System(b->m_attributes);
    if (b->m_pseudoElement) XString_delete_base(b->m_pseudoElement);
    XMemset(b, 0, sizeof(*b));
}

/** @brief 释放单条规则的字符串/数组（含选择器新字段与声明原名）。 */
static void xcss_freeRule(XCssStyleRule* rule)
{
    int i;
    if (!rule) return;
    for (i = 0; i < rule->m_selectorCount; ++i) {
        int bi;
        for (bi = 0; bi < rule->m_selectors[i].m_basicCount; ++bi)
            xcss_freeBasic(&rule->m_selectors[i].m_basics[bi]);
        if (rule->m_selectors[i].m_basics)
            XFree_System(rule->m_selectors[i].m_basics);
    }
    if (rule->m_selectors) XFree_System(rule->m_selectors);
    for (i = 0; i < rule->m_declarationCount; ++i) {
        if (rule->m_declarations[i].m_value)
            XString_delete_base(rule->m_declarations[i].m_value);
        if (rule->m_declarations[i].m_propertyName)
            XString_delete_base(rule->m_declarations[i].m_propertyName);
    }
    if (rule->m_declarations) XFree_System(rule->m_declarations);
    XMemset(rule, 0, sizeof(*rule));
}

/** @brief 释放 @import 规则。 */
static void xcss_freeImportRule(XCssImportRule* rule)
{
    int i;
    if (!rule) return;
    if (rule->m_href) XString_delete_base(rule->m_href);
    for (i = 0; i < rule->m_mediaCount; ++i)
        if (rule->m_media[i]) XString_delete_base(rule->m_media[i]);
    if (rule->m_media) XFree_System(rule->m_media);
    XMemset(rule, 0, sizeof(*rule));
}

/** @brief 释放 @media 规则（媒体名单 + 内嵌规则集）。 */
static void xcss_freeMediaRule(XCssMediaRule* rule)
{
    int i;
    if (!rule) return;
    for (i = 0; i < rule->m_mediaCount; ++i)
        if (rule->m_media[i]) XString_delete_base(rule->m_media[i]);
    if (rule->m_media) XFree_System(rule->m_media);
    for (i = 0; i < rule->m_ruleCount; ++i)
        xcss_freeRule(&rule->m_rules[i]);
    if (rule->m_rules) XFree_System(rule->m_rules);
    XMemset(rule, 0, sizeof(*rule));
}

/** @brief 释放 @page 规则。 */
static void xcss_freePageRule(XCssPageRule* rule)
{
    int i;
    if (!rule) return;
    if (rule->m_selector) XString_delete_base(rule->m_selector);
    for (i = 0; i < rule->m_declarationCount; ++i) {
        if (rule->m_declarations[i].m_value)
            XString_delete_base(rule->m_declarations[i].m_value);
        if (rule->m_declarations[i].m_propertyName)
            XString_delete_base(rule->m_declarations[i].m_propertyName);
    }
    if (rule->m_declarations) XFree_System(rule->m_declarations);
    XMemset(rule, 0, sizeof(*rule));
}

void XCssStyleSheet_clear(XCssStyleSheet* sheet)
{
    int i;
    if (!sheet) return;
    for (i = 0; i < sheet->m_ruleCount; ++i)
        xcss_freeRule(&sheet->m_rules[i]);
    if (sheet->m_rules) XFree_System(sheet->m_rules);
    for (i = 0; i < sheet->m_importCount; ++i)
        xcss_freeImportRule(&sheet->m_imports[i]);
    if (sheet->m_imports) XFree_System(sheet->m_imports);
    for (i = 0; i < sheet->m_mediaRuleCount; ++i)
        xcss_freeMediaRule(&sheet->m_mediaRules[i]);
    if (sheet->m_mediaRules) XFree_System(sheet->m_mediaRules);
    for (i = 0; i < sheet->m_pageRuleCount; ++i)
        xcss_freePageRule(&sheet->m_pageRules[i]);
    if (sheet->m_pageRules) XFree_System(sheet->m_pageRules);
    /* 整表清零：规则/at 规则计数与容量一并复位。 */
    XMemset(sheet, 0, sizeof(*sheet));
}

/* ==================== 转义预处理（对标 Scanner::preprocess + Symbol::lexem） ====================
 *
 * 对标 Qt 6.8.3 qcssparser.cpp：
 *  1) \XXXXXX（1..6 位十六进制）→ 码点 → UTF-8（对标 preprocess 的 hex
 *     分支；Qt 换 QChar 截 16 位，本库按完整码点做合法 UTF-8，>0x10FFFF
 *     折叠为 U+FFFD，符合 CSS Syntax 规范）；
 *  2) \c 字面转义 → c（对标 preprocess 留反斜杠 + Symbol::lexem 跳反斜杠
 *     的合成效果；'\'+换行同理折叠为换行）;
 *  3) 引号外的 块注释 → 单个空格（对标 scanner 注释→S 空白 token；引号
 *     内注释体按 STRING token 语义原样保留）。
 * 输出恒不大于输入（逐项收缩），故一次性按输入长度分配，无需扩容。
 */

/** @brief 十六进制位值（非十六进制字符返回 -1）。 */
static int xcss_hexVal(char c)
{
    if (XIsDigit((unsigned char)c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 码点 → UTF-8 编码（返回字节数，1..4）。 */
static size_t xcss_utf8Encode(uint32_t code, char* out)
{
    if (code > 0x10FFFFu) code = 0xFFFDu;
    if (code < 0x80u) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800u) {
        out[0] = (char)(0xC0u | (code >> 6));
        out[1] = (char)(0x80u | (code & 0x3Fu));
        return 2;
    }
    if (code < 0x10000u) {
        out[0] = (char)(0xE0u | (code >> 12));
        out[1] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (code & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (code >> 18));
    out[1] = (char)(0x80u | ((code >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((code >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (code & 0x3Fu));
    return 4;
}

/** @brief 转义/注释预处理，返回输出长度（<= 输入长度）。 */
static size_t xcss_preprocess(const char* css, size_t len, char* out)
{
    size_t i = 0;
    size_t o = 0;
    bool inStr = false;
    char quote = 0;
    while (i < len) {
        char c = css[i];
        if (!inStr && c == '/' && i + 1 < len && css[i + 1] == '*') {
            /* 块注释 → 单空格（未闭合注释按 scanner 语义吞到末尾）。 */
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) ++i;
            if (i + 1 < len) i += 2;
            else i = len;
            out[o++] = ' ';
            continue;
        }
        if (c == '"' || c == '\'') {
            if (!inStr) {
                inStr = true;
                quote = c;
            } else if (c == quote) {
                inStr = false;
            }
            out[o++] = c;
            ++i;
            continue;
        }
        if (c == '\\') {
            int n = 0;
            uint32_t code = 0;
            while (n < 6) {
                int hv = (i + 1 + n < len) ? xcss_hexVal(css[i + 1 + n]) : -1;
                if (hv < 0) break;
                code = (code << 4) | (uint32_t)hv;
                ++n;
            }
            if (n > 0) {
                o += xcss_utf8Encode(code, out + o);
                i += 1 + (size_t)n;
                continue;
            }
            if (i + 1 < len) { /* \c 字面转义（含 \+换行）。 */
                out[o++] = css[i + 1];
                i += 2;
                continue;
            }
            out[o++] = c; /* 末尾孤立反斜杠原样保留。 */
            ++i;
            continue;
        }
        out[o++] = c;
        ++i;
    }
    return o;
}

/* ==================== 解析上下文 ==================== */

/** @brief 解析上下文（对标 Parser：符号位置 + 错误记录）。 */
typedef struct XCssParseCtx
{
    const char* p;        /**< 当前位置（解码后缓冲内）。 */
    const char* end;      /**< 缓冲尾。 */
    const char* begin;    /**< 缓冲首（行号计算用）。 */
    XCssStyleSheet* sheet;
    XCssParseError* err;
} XCssParseCtx;

/** @brief 记录结构性错误（对标 recordError：仅记录首个）并返回 false。 */
static bool xcss_failAt(XCssParseCtx* ctx, const char* msg, const char* pos)
{
    if (ctx->err && !ctx->err->m_message) {
        int off = 0;
        int line = 1;
        const char* q;
        for (q = ctx->begin; q < pos; ++q) {
            if (*q == '\n') ++line;
            ++off;
        }
        ctx->err->m_message = msg;
        ctx->err->m_offset = off;
        ctx->err->m_line = line;
    }
    return false;
}

/** @brief 记录内存不足错误（属执行失败，同样走错误契约）。 */
static bool xcss_failOom(XCssParseCtx* ctx)
{
    return xcss_failAt(ctx, "内存不足", ctx->p);
}

/** @brief 跳过空白，返回推进后指针（注释已由预处理剥离）。 */
static const char* xcss_skipWs(const char* p, const char* end)
{
    while (p < end && XIsSpace((unsigned char)*p)) ++p;
    return p;
}

/** @brief 标识符字符集（对标 scanner nmchar：字母数字下划线连字符；
 *         转义已在预处理展开，故无需再处理反斜杠）。 */
static bool xcss_isIdentChar(char c)
{
    return XIsAlnum((unsigned char)c) || c == '_' || c == '-';
}

/** @brief 从 [s, s+len) 建 UTF-8 XString。 */
static XString* xcss_newString(const char* s, size_t len)
{
    return XString_create_with_length_utf8(s, len);
}

/* ==================== 数组追加 ==================== */

/** @brief 样式表追加规则（容量倍增；结构赋值转移所有权）。 */
static bool xcss_sheetAppendRule(XCssParseCtx* ctx, XCssStyleRule* rule)
{
    XCssStyleSheet* sheet = ctx->sheet;
    if (sheet->m_ruleCount >= sheet->m_ruleCapacity) {
        int cap = sheet->m_ruleCapacity > 0 ? sheet->m_ruleCapacity * 2 : 8;
        int oldCap = sheet->m_ruleCapacity;
        int ri;
        XCssStyleRule* grown = (XCssStyleRule*)XRealloc_System(
            sheet->m_rules, sizeof(XCssStyleRule) * (size_t)cap);
        if (!grown) return false;
        sheet->m_rules = grown;
        for (ri = oldCap; ri < cap; ++ri)
            XMemset(&sheet->m_rules[ri], 0, sizeof(XCssStyleRule));
        sheet->m_ruleCapacity = cap;
    }
    sheet->m_rules[sheet->m_ruleCount++] = *rule;
    XMemset(rule, 0, sizeof(*rule));
    return true;
}

/** @brief 追加规则到 @media 内嵌集（count%4 增长）。 */
static bool xcss_mediaAppendRule(XCssMediaRule* media, XCssStyleRule* rule)
{
    if (media->m_ruleCount % 4 == 0) {
        int cap = media->m_ruleCount + 4;
        XCssStyleRule* grown = (XCssStyleRule*)XRealloc_System(
            media->m_rules, sizeof(XCssStyleRule) * (size_t)cap);
        if (!grown) return false;
        media->m_rules = grown;
    }
    media->m_rules[media->m_ruleCount++] = *rule;
    XMemset(rule, 0, sizeof(*rule));
    return true;
}

/** @brief 声明追加（count%8 增长；名称恒存原名，未知属性存 Unknown，
 *         对标 Declaration 的 property/propertyId 双轨）。 */
static bool xcss_appendDecl(XCssDeclaration** arr, int* count,
                            const char* name, size_t nlen,
                            const char* value, size_t vlen,
                            XCssProperty id, bool important)
{
    XCssDeclaration* d;
    if (*count % 8 == 0) {
        int cap = *count + 8;
        XCssDeclaration* grown = (XCssDeclaration*)XRealloc_System(
            *arr, sizeof(XCssDeclaration) * (size_t)cap);
        if (!grown) return false;
        *arr = grown;
    }
    d = &(*arr)[*count];
    XMemset(d, 0, sizeof(*d));
    d->m_propertyId = id;
    d->m_important = important;
    d->m_value = xcss_newString(value, vlen);
    d->m_propertyName = xcss_newString(name, nlen);
    if (!d->m_value || !d->m_propertyName) {
        if (d->m_value) XString_delete_base(d->m_value);
        if (d->m_propertyName) XString_delete_base(d->m_propertyName);
        return false;
    }
    ++*count;
    return true;
}

/** @brief 字符串指针数组追加（count%4 增长；失败时由调用方释放 s）。 */
static bool xcss_appendString(XString*** arr, int* count, XString* s)
{
    if (*count % 4 == 0) {
        int cap = *count + 4;
        XString** grown = (XString**)XRealloc_System(
            *arr, sizeof(XString*) * (size_t)cap);
        if (!grown) return false;
        *arr = grown;
    }
    (*arr)[(*count)++] = s;
    return true;
}

/* ==================== 特异度 ==================== */

/** @brief 伪类位数（对标 Qt 按伪类个数逐个计权）。 */
static int xcss_pseudoCount(uint32_t v)
{
    int n = 0;
    while (v) {
        v &= v - 1;
        ++n;
    }
    return n;
}

/** @brief 向选择器链追加基础段（整段结构赋值转移所有权）。
 *
 *  特异度对标 Qt 6.8.3 qcssparser.cpp Selector::specificity()（逐段累加：
 *  elementName 非空 +1、(pseudos 数 + attributeSelectors 数)×0x10、
 *  ids 数×0x100——多 ID/多属性逐项计权，本批随匹配端一并接入）；
 *  .Foo 类选择器沿用既有 R-91 口径（与伪类/属性同档 0x10，Qt 原式
 *  对剥点后的 elementName 计 1，属已声明偏差）。
 */
static bool xcss_appendBasic(XCssSelector* sel, XCssRelation rel,
                             XCssBasicSelector* basic, bool isClass)
{
    XCssBasicSelector* b;
    size_t elen = basic->m_elementName
                      ? XString_toUtf8_length(basic->m_elementName) : 0;
    if (sel->m_basicCount % 4 == 0) {
        int cap = sel->m_basicCount + 4;
        XCssBasicSelector* grown = (XCssBasicSelector*)XRealloc_System(
            sel->m_basics, sizeof(XCssBasicSelector) * (size_t)cap);
        if (!grown) return false;
        sel->m_basics = grown;
    }
    b = &sel->m_basics[sel->m_basicCount];
    *b = *basic;                      /* 结构赋值：所有权转移至链。 */
    b->m_relationToPrev = rel;
    XMemset(basic, 0, sizeof(*basic)); /* 清源，防双重释放。 */
    ++sel->m_basicCount;
    /* 多 ID（m_id 之外的 #b #c）各 0x100、多属性（m_attribute 之外的
     * [b][c]）各 0x10：对齐 Selector::specificity 的 ids/attrs 全量计数。 */
    sel->m_specificity += (b->m_id ? 0x100 : 0) +
                          b->m_idCount * 0x100 +
                          (xcss_pseudoCount(b->m_pseudoClasses) +
                           (b->m_attribute.m_name ? 1 : 0) +
                           b->m_attributeCount +
                           (elen > 0 && isClass ? 1 : 0)) * 0x10 +
                          (elen > 0 && !isClass ? 1 : 0);
    return true;
}

/* ==================== 选择器解析 ==================== */

/** @brief 解析属性选择器（p 位于 '['；对标 parseAttrib）。
 *
 *  形态：[name]、[name op value]，op ∈ { =, ~=, |=, ^=, $=, *= }；
 *  值可带单双引号。错误路径（缺名/未闭合/引号未闭合）对标 Qt 返回 false，
 *  值后到 ']' 的杂料保持旧版容忍（跳过）。
 */
static bool xcss_parseAttribute(XCssParseCtx* ctx, XCssAttributeSelector* a)
{
    const char* p = ctx->p + 1; /* 越过 '['。 */
    const char* end = ctx->end;
    const char* n;
    size_t nlen;
    XCssValueMatch match = XCssValueMatch_Equal;
    XString* name = NULL;
    XString* value = NULL;
    XMemset(a, 0, sizeof(*a));
    p = xcss_skipWs(p, end);
    n = p;
    while (p < end && *p != ']' && *p != '=' && *p != '~' && *p != '|' &&
           *p != '^' && *p != '$' && *p != '*')
        ++p;
    nlen = (size_t)(p - n);
    while (nlen > 0 && XIsSpace((unsigned char)n[nlen - 1])) --nlen;
    if (nlen == 0) return xcss_failAt(ctx, "属性选择器缺少名称", p);
    /* 操作符。 */
    if (p < end && (*p == '~' || *p == '|' || *p == '^' || *p == '$' ||
                    *p == '*')) {
        switch (*p) {
        case '~': match = XCssValueMatch_Includes; break;
        case '|': match = XCssValueMatch_DashMatch; break;
        case '^': match = XCssValueMatch_BeginsWith; break;
        case '$': match = XCssValueMatch_EndsWith; break;
        default: match = XCssValueMatch_Contains; break;
        }
        ++p;
        if (p < end && *p == '=') ++p;
    } else if (p < end && *p == '=') {
        match = XCssValueMatch_Equal;
        ++p;
    }
    while (p < end && XIsSpace((unsigned char)*p)) ++p;
    name = xcss_newString(n, nlen);
    if (!name) return xcss_failOom(ctx);
    /* 值。 */
    if (p < end && *p != ']') {
        const char* v;
        size_t vlen = 0;
        v = p;
        if (*p == '"' || *p == '\'') {
            char q = *p;
            ++p;
            v = p;
            while (p < end && *p != q) ++p;
            if (p >= end) {
                XString_delete_base(name);
                return xcss_failAt(ctx, "属性值引号未闭合", p);
            }
            vlen = (size_t)(p - v);
            ++p;
        } else {
            while (p < end && *p != ']' && !XIsSpace((unsigned char)*p)) ++p;
            vlen = (size_t)(p - v);
        }
        if (vlen > 0) {
            value = xcss_newString(v, vlen);
            if (!value) {
                XString_delete_base(name);
                return xcss_failOom(ctx);
            }
        }
        while (p < end && *p != ']') ++p;
    }
    if (p >= end) {
        if (name) XString_delete_base(name);
        if (value) XString_delete_base(value);
        return xcss_failAt(ctx, "属性选择器未闭合（缺少 ']'）", p);
    }
    ++p; /* 越过 ']'。 */
    a->m_name = name;
    a->m_value = value;
    a->m_match = match;
    ctx->p = p;
    return true;
}

/** @brief 伪类名 → 位（对标 qcssparser.cpp findKnownValue(pseudos[])；
 *         只收可由 XStyleState 位派生的名（xsss_statePseudos 有映射源），
 *         未知名单冒号伪类按旧行为忽略。
 *
 *  Qt 6.8.3 pseudos[] 共 45 名，其中无 State 来源的名字（alternate/
 *  bottom/closable/default/edit-focus/editable/exclusive/first/flat/
 *  floatable/last/left/maximized/middle/minimized/movable/next-selected/
 *  no-frame/non-exclusive/only-one/previous-selected/right/top 等）在本库
 *  永不命中（XStyleState 无对应位、置位侧亦无生产者），不收入名表；
 *  :top/:bottom/:left/:right 在 Qt 由各绘制点手工附加（非 state 派生），
 *  随子控件分派（deferred）再议。拼写陷阱：Qt 名是 read-only（带连
 *  字符），本表为主名；旧拼写 readonly 保留兼容。
 */
static void xcss_applyPseudo(const char* name, size_t len, uint32_t* pseudos)
{
    if (len == 5 && XStrncasecmp(name, "hover", 5) == 0)
        *pseudos |= XCssPseudo_Hover;
    else if (len == 7 && XStrncasecmp(name, "pressed", 7) == 0)
        *pseudos |= XCssPseudo_Pressed;
    else if (len == 5 && XStrncasecmp(name, "focus", 5) == 0)
        *pseudos |= XCssPseudo_Focus;
    else if (len == 8 && XStrncasecmp(name, "disabled", 8) == 0)
        *pseudos |= XCssPseudo_Disabled;
    else if (len == 7 && XStrncasecmp(name, "enabled", 7) == 0)
        *pseudos |= XCssPseudo_Enabled;
    else if (len == 7 && XStrncasecmp(name, "checked", 7) == 0)
        *pseudos |= XCssPseudo_Checked;
    else if (len == 8 && XStrncasecmp(name, "selected", 8) == 0)
        *pseudos |= XCssPseudo_Selected;
    else if (len == 9 && XStrncasecmp(name, "read-only", 9) == 0)
        *pseudos |= XCssPseudo_ReadOnly; /* Qt 主名（连字符拼写）。 */
    else if (len == 8 && XStrncasecmp(name, "readonly", 8) == 0)
        *pseudos |= XCssPseudo_ReadOnly; /* 旧拼写兼容。 */
    else if (len == 2 && XStrncasecmp(name, "on", 2) == 0)
        *pseudos |= XCssPseudo_Checked; /* Qt 名表 "on"→PseudoClass_Checked。 */
    else if (len == 3 && XStrncasecmp(name, "off", 3) == 0)
        *pseudos |= XCssPseudo_Unchecked; /* Qt 名表 "off"→PseudoClass_Unchecked。 */
    else if (len == 9 && XStrncasecmp(name, "unchecked", 9) == 0)
        *pseudos |= XCssPseudo_Unchecked;
    else if (len == 13 && XStrncasecmp(name, "indeterminate", 13) == 0)
        *pseudos |= XCssPseudo_Indeterminate;
    else if (len == 6 && XStrncasecmp(name, "active", 6) == 0)
        *pseudos |= XCssPseudo_Active;
    else if (len == 6 && XStrncasecmp(name, "window", 6) == 0)
        *pseudos |= XCssPseudo_Window;
    else if (len == 4 && XStrncasecmp(name, "open", 4) == 0)
        *pseudos |= XCssPseudo_Open;
    else if (len == 6 && XStrncasecmp(name, "closed", 6) == 0)
        *pseudos |= XCssPseudo_Closed;
    else if (len == 12 && XStrncasecmp(name, "has-children", 12) == 0)
        *pseudos |= XCssPseudo_Children;
    else if (len == 12 && XStrncasecmp(name, "has-siblings", 12) == 0)
        *pseudos |= XCssPseudo_Sibling;
    else if (len == 12 && XStrncasecmp(name, "adjoins-item", 12) == 0)
        *pseudos |= XCssPseudo_Item; /* Qt 名表 "adjoins-item"→PseudoClass_Item。 */
    else if (len == 4 && XStrncasecmp(name, "item", 4) == 0)
        *pseudos |= XCssPseudo_Item;
    else if (len == 10 && XStrncasecmp(name, "horizontal", 10) == 0)
        *pseudos |= XCssPseudo_Horizontal;
    else if (len == 8 && XStrncasecmp(name, "vertical", 8) == 0)
        *pseudos |= XCssPseudo_Vertical;
}

/** @brief 解析一个基础选择器段（对标 parseSimpleSelector）。
 *
 *  形态：[element][#id]*([attr])*(:pseudo | ::pseudoElement)*；
 *  element 兼容 .Class 前导点（剥点存储，特异度按类计，见
 *  xcss_appendBasic）。段内未存任何成分时按 Qt minCount 语义判错
 *  （选择器段无法归属的垃圾，如段首关系符）。
 */
static bool xcss_parseBasic(XCssParseCtx* ctx, XCssBasicSelector* basic,
                            bool* isClass)
{
    const char* p = ctx->p;
    const char* end = ctx->end;
    bool stored = false;
    *isClass = false;
    /* 元素名（含前导 '.' 的类选择器；字符集沿用旧版含 '.'）。 */
    if (p < end && (XIsAlnum((unsigned char)*p) || *p == '_' || *p == '-' ||
                    *p == '.')) {
        const char* n = p;
        while (p < end && (XIsAlnum((unsigned char)*p) || *p == '_' ||
                           *p == '-' || *p == '.'))
            ++p;
        if ((size_t)(p - n) > 0) {
            const char* name = n;
            size_t len = (size_t)(p - n);
            if (name[0] == '.') {
                *isClass = true;
                ++name;
                --len;
            }
            if (len > 0) {
                basic->m_elementName = xcss_newString(name, len);
                if (!basic->m_elementName) return xcss_failOom(ctx);
                stored = true;
            }
        }
    }
    /* ID 列表：#a#b 全量存储（首存 m_id，其余进 m_ids）。 */
    while (p < end && *p == '#') {
        const char* n;
        size_t len;
        XString* s;
        ++p;
        n = p;
        while (p < end && xcss_isIdentChar(*p)) ++p;
        len = (size_t)(p - n);
        if (len == 0) return xcss_failAt(ctx, "ID 选择器缺少名称", p);
        s = xcss_newString(n, len);
        if (!s) return xcss_failOom(ctx);
        if (!basic->m_id) {
            basic->m_id = s;
        } else if (!xcss_appendString(&basic->m_ids, &basic->m_idCount, s)) {
            XString_delete_base(s);
            return xcss_failOom(ctx);
        }
        stored = true;
    }
    /* 属性列表：[a][b] 全量存储（首存 m_attribute，其余进 m_attributes）。 */
    while (p < end && *p == '[') {
        XCssAttributeSelector attr;
        ctx->p = p;
        if (!xcss_parseAttribute(ctx, &attr)) {
            xcss_freeAttr(&attr);
            return false;
        }
        p = ctx->p;
        if (!basic->m_attribute.m_name) {
            basic->m_attribute = attr;
        } else if (basic->m_attributeCount % 4 == 0) {
            int cap = basic->m_attributeCount + 4;
            XCssAttributeSelector* grown = (XCssAttributeSelector*)
                XRealloc_System(basic->m_attributes,
                                sizeof(XCssAttributeSelector) * (size_t)cap);
            if (!grown) {
                xcss_freeAttr(&attr);
                return xcss_failOom(ctx);
            }
            basic->m_attributes = grown;
            basic->m_attributes[basic->m_attributeCount++] = attr;
        } else {
            basic->m_attributes[basic->m_attributeCount++] = attr;
        }
        stored = true;
    }
    /* 伪类 / 伪元素。 */
    while (p < end && *p == ':') {
        if (p + 1 < end && p[1] == ':') {
            /* ::name —— 伪元素/子控件名（对标 Selector::pseudoElement：
             * 取首个未入伪类名表的伪；此处按任务口径双冒号恒记录名）。 */
            const char* n;
            ++p;
            ++p;
            n = p;
            while (p < end && xcss_isIdentChar(*p)) ++p;
            if (p == n) return xcss_failAt(ctx, "伪元素缺少名称", p);
            if (!basic->m_pseudoElement) {
                basic->m_pseudoElement = xcss_newString(
                    n, (size_t)(p - n));
                if (!basic->m_pseudoElement) return xcss_failOom(ctx);
            }
        } else {
            /* :name —— 伪类（已知入位；未知名单冒号伪类保持旧行为忽略）。 */
            const char* n;
            ++p;
            n = p;
            while (p < end && xcss_isIdentChar(*p)) ++p;
            if (p == n) return xcss_failAt(ctx, "伪类缺少名称", p);
            xcss_applyPseudo(n, (size_t)(p - n), &basic->m_pseudoClasses);
        }
        stored = true;
    }
    if (!stored)
        return xcss_failAt(ctx, "选择器段无法归属的垃圾", ctx->p);
    ctx->p = p;
    return true;
}

/** @brief 解析一条选择器链（段间关系：空格后代 / > 子代 / + 相邻 /
 *         ~ 通用兄弟；对标 parseSelector + parseCombinator）。
 *
 *  Qt 把关系存于前段 relationToNext，本库等价存于后段
 *  m_relationToPrev。段首出现关系符（含 '>' 首段）或空链按 Qt
 *  testSelector/minCount 语义判错。
 */
static bool xcss_parseChain(XCssParseCtx* ctx, XCssSelector* sel)
{
    XCssRelation rel = XCssRelation_None;
    for (;;) {
        const char* p = xcss_skipWs(ctx->p, ctx->end);
        XCssBasicSelector basic;
        bool isClass = false;
        bool wsBefore;
        const char* q;
        ctx->p = p;
        if (p >= ctx->end || *p == ',' || *p == '{' || *p == '}')
            return xcss_failAt(ctx, "空选择器", p);
        XMemset(&basic, 0, sizeof(basic));
        if (!xcss_parseBasic(ctx, &basic, &isClass)) {
            xcss_freeBasic(&basic);
            return false;
        }
        if (!xcss_appendBasic(sel, rel, &basic, isClass)) {
            xcss_freeBasic(&basic); /* 失败时所有权未转移，堆串就地释放。 */
            return xcss_failOom(ctx);
        }
        /* 段间关系检测（p 停在上一 token 后，未吞空白）。 */
        wsBefore = (ctx->p < ctx->end &&
                    XIsSpace((unsigned char)*ctx->p));
        q = xcss_skipWs(ctx->p, ctx->end);
        if (q < ctx->end && (*q == '>' || *q == '+' || *q == '~')) {
            rel = (*q == '>') ? XCssRelation_Parent
                : (*q == '+') ? XCssRelation_DirectAdjacent
                              : XCssRelation_IndirectAdjacent;
            ctx->p = xcss_skipWs(q + 1, ctx->end);
            continue;
        }
        if (wsBefore && q < ctx->end && *q != ',' && *q != '{' && *q != '}') {
            rel = XCssRelation_Ancestor;
            ctx->p = q;
            continue; /* 下一基础段；垃圾在段首判错。 */
        }
        ctx->p = q;
        return true;
    }
}

/** @brief 解析声明序列到 '}' 并消费之（对标 parseRuleset 声明循环 +
 *         parseNextDeclaration）。
 *
 *  值扫描引号感知（引号内的 ';' '}' 不截断）；坏声明（缺冒号）按 Qt
 *  parseRuleset 容忍度跳过至 ';' 或 '}'；未识别属性按 Qt 存为
 *  UnknownProperty 并保留原名。花括号失衡在此判错。
 */
static bool xcss_parseDeclarations(XCssParseCtx* ctx,
                                   XCssDeclaration** arr, int* count)
{
    const char* end = ctx->end;
    for (;;) {
        const char* p = xcss_skipWs(ctx->p, end);
        const char* name;
        size_t nlen;
        if (p >= end) return xcss_failAt(ctx, "花括号失衡（缺少 '}'）", p);
        if (*p == '}') {
            ctx->p = p + 1;
            return true;
        }
        /* 声明名：到 ':' ';' '}'。 */
        name = p;
        while (p < end && *p != ':' && *p != ';' && *p != '}') ++p;
        nlen = (size_t)(p - name);
        while (nlen > 0 && XIsSpace((unsigned char)name[nlen - 1])) --nlen;
        if (p >= end || *p != ':') {
            /* 无冒号坏声明：容忍，跳至 ';' / '}'（不消费 '}'）。 */
            while (p < end && *p != ';' && *p != '}') ++p;
            if (p < end && *p == ';') ++p;
            ctx->p = p;
            continue;
        }
        ++p; /* 越过 ':'。 */
        /* 值：引号感知扫描至顶层 ';' 或 '}'。 */
        {
            const char* vstart;
            size_t vlen;
            XCssProperty id;
            bool important = false;
            vstart = p;
            while (p < end && *p != ';' && *p != '}') {
                if (*p == '"' || *p == '\'') {
                    char q = *p;
                    ++p;
                    while (p < end && *p != q) ++p;
                    if (p < end) ++p; /* 收口引号（容忍 EOF）。 */
                } else {
                    ++p;
                }
            }
            vlen = (size_t)(p - vstart);
            while (vlen > 0 && XIsSpace((unsigned char)vstart[0])) {
                ++vstart;
                --vlen;
            }
            while (vlen > 0 && XIsSpace((unsigned char)vstart[vlen - 1]))
                --vlen;
            /* !important 后缀。 */
            if (vlen > 10 &&
                XStrncasecmp(vstart + vlen - 10, "!important", 10) == 0) {
                important = true;
                vlen -= 10;
                while (vlen > 0 &&
                       XIsSpace((unsigned char)vstart[vlen - 1]))
                    --vlen;
            }
            id = xcss_findProperty(name, nlen);
            if (vlen > 0 && !xcss_appendDecl(arr, count, name, nlen,
                                             vstart, vlen, id, important))
                return xcss_failOom(ctx);
        }
        if (p < end && *p == ';') ++p;
        ctx->p = p;
    }
}

/** @brief 规则集落点（样式表层 或 @media 内嵌）。 */
typedef struct XCssRuleTarget
{
    XCssMediaRule* media; /**< 非空时规则入 @media 内嵌集，否则入样式表层。 */
} XCssRuleTarget;

/** @brief 解析一个规则集并落入目标（对标 parseRuleset）。
 *
 *  选择器列表 chain (',' chain)* 先行，其后必须 '{'；声明段到 '}' 并
 *  由 xcss_parseDeclarations 消费（失衡在其中判错）。任一步失败时
 *  本规则整体释放（对标 Qt：解析失败整表弃用）。
 */
static bool xcss_parseRuleset(XCssParseCtx* ctx, XCssRuleTarget* target)
{
    XCssStyleRule rule;
    XMemset(&rule, 0, sizeof(rule));
    for (;;) {
        XCssSelector* sel;
        if (rule.m_selectorCount % 4 == 0) {
            int cap = rule.m_selectorCount + 4;
            XCssSelector* grown = (XCssSelector*)XRealloc_System(
                rule.m_selectors, sizeof(XCssSelector) * (size_t)cap);
            if (!grown) {
                xcss_freeRule(&rule);
                return xcss_failOom(ctx);
            }
            rule.m_selectors = grown;
        }
        sel = &rule.m_selectors[rule.m_selectorCount];
        XMemset(sel, 0, sizeof(*sel));
        ++rule.m_selectorCount;
        if (!xcss_parseChain(ctx, sel)) {
            xcss_freeRule(&rule);
            return false;
        }
        if (ctx->p < ctx->end && *ctx->p == ',') {
            ++ctx->p;
            continue;
        }
        break;
    }
    if (ctx->p >= ctx->end || *ctx->p != '{') {
        xcss_failAt(ctx, "选择器后缺少 '{'", ctx->p);
        xcss_freeRule(&rule);
        return false;
    }
    ++ctx->p; /* 越过 '{'。 */
    if (!xcss_parseDeclarations(ctx, &rule.m_declarations,
                                &rule.m_declarationCount)) {
        xcss_freeRule(&rule);
        return false;
    }
    if (target->media) {
        if (!xcss_mediaAppendRule(target->media, &rule)) {
            xcss_freeRule(&rule);
            return xcss_failOom(ctx);
        }
    } else if (!xcss_sheetAppendRule(ctx, &rule)) {
        xcss_freeRule(&rule);
        return xcss_failOom(ctx);
    }
    return true;
}

/* ==================== at 规则 ==================== */

/** @brief 解析媒体名单（IDENT (',' IDENT)*；对标 parseMedium 循环）。 */
static bool xcss_parseMediaList(XCssParseCtx* ctx, XString*** arr,
                                int* count, bool* progressed)
{
    const char* end = ctx->end;
    *progressed = false;
    for (;;) {
        const char* p = xcss_skipWs(ctx->p, end);
        const char* n;
        size_t len;
        XString* s;
        ctx->p = p;
        if (p >= end || !xcss_isIdentChar(*p)) break;
        n = p;
        while (p < end && xcss_isIdentChar(*p)) ++p;
        len = (size_t)(p - n);
        s = xcss_newString(n, len);
        if (!s) return xcss_failOom(ctx);
        if (!xcss_appendString(arr, count, s)) {
            XString_delete_base(s);
            return xcss_failOom(ctx);
        }
        *progressed = true;
        ctx->p = p;
        p = xcss_skipWs(ctx->p, end);
        if (p < end && *p == ',') {
            ctx->p = p + 1;
            continue;
        }
        ctx->p = p;
        break;
    }
    return true;
}

/** @brief 解析 href（"..." 字符串 / url(...) / 裸 token；对标
 *         testAndParseUri），成功时 *out 持有新建 XString。 */
static bool xcss_parseHref(XCssParseCtx* ctx, XString** out)
{
    const char* p = xcss_skipWs(ctx->p, ctx->end);
    const char* end = ctx->end;
    const char* v;
    size_t len;
    ctx->p = p;
    if (p >= end) return xcss_failAt(ctx, "@import 缺少目标", p);
    if ((size_t)(end - p) > 4 && XStrncasecmp(p, "url(", 4) == 0) {
        const char* close;
        v = p + 4;
        close = v;
        while (close < end && *close != ')') ++close;
        if (close >= end)
            return xcss_failAt(ctx, "url() 未闭合", close);
        len = (size_t)(close - v);
        while (len > 0 && XIsSpace((unsigned char)v[0])) { ++v; --len; }
        while (len > 0 && XIsSpace((unsigned char)v[len - 1])) --len;
        if (len > 1 && (v[0] == '"' || v[0] == '\'') && v[len - 1] == v[0]) {
            ++v;
            len -= 2;
        }
        p = close + 1;
    } else if (*p == '"' || *p == '\'') {
        char q = *p;
        ++p;
        v = p;
        while (p < end && *p != q) ++p;
        if (p >= end) return xcss_failAt(ctx, "字符串未闭合", p);
        len = (size_t)(p - v);
        ++p;
    } else {
        v = p;
        while (p < end && !XIsSpace((unsigned char)*p) && *p != ';') ++p;
        len = (size_t)(p - v);
        if (len == 0) return xcss_failAt(ctx, "@import 缺少目标", p);
    }
    ctx->p = p;
    *out = xcss_newString(v, len);
    if (!*out) return xcss_failOom(ctx);
    return true;
}

/** @brief @charset "..." ;（对标 parseCharset：形态错误即失败）。 */
static bool xcss_parseCharset(XCssParseCtx* ctx)
{
    const char* p = xcss_skipWs(ctx->p, ctx->end);
    const char* end = ctx->end;
    ctx->p = p;
    if (p >= end || (*p != '"' && *p != '\''))
        return xcss_failAt(ctx, "@charset 缺少编码字符串", p);
    {
        char q = *p;
        ++p;
        while (p < end && *p != q) ++p;
        if (p >= end) return xcss_failAt(ctx, "字符串未闭合", p);
        ++p;
    }
    p = xcss_skipWs(p, end);
    if (p >= end || *p != ';')
        return xcss_failAt(ctx, "@charset 缺少 ';' 结尾", p);
    ctx->p = p + 1;
    return true;
}

/** @brief @import href [media] ;（对标 parseImport；解析失败即错误）。 */
static bool xcss_parseImport(XCssParseCtx* ctx)
{
    XCssImportRule rule;
    bool progressed = false;
    const char* p;
    XMemset(&rule, 0, sizeof(rule));
    if (!xcss_parseHref(ctx, &rule.m_href)) {
        xcss_freeImportRule(&rule);
        return false;
    }
    if (!xcss_parseMediaList(ctx, &rule.m_media, &rule.m_mediaCount,
                             &progressed)) {
        xcss_freeImportRule(&rule);
        return false;
    }
    p = xcss_skipWs(ctx->p, ctx->end);
    if (p >= ctx->end || *p != ';') {
        xcss_freeImportRule(&rule);
        return xcss_failAt(ctx, "@import 缺少 ';' 结尾", p);
    }
    ctx->p = p + 1;
    /* 落表（count%4 增长）。 */
    if (ctx->sheet->m_importCount % 4 == 0) {
        int cap = ctx->sheet->m_importCount + 4;
        XCssImportRule* grown = (XCssImportRule*)XRealloc_System(
            ctx->sheet->m_imports,
            sizeof(XCssImportRule) * (size_t)cap);
        if (!grown) {
            xcss_freeImportRule(&rule);
            return xcss_failOom(ctx);
        }
        ctx->sheet->m_imports = grown;
    }
    ctx->sheet->m_imports[ctx->sheet->m_importCount++] = rule;
    return true;
}

/** @brief @media 名单 { 规则集* }（对标 parseMedia；内层 at 规则与
 *         结构错误均失败；块未闭合吞到末尾属 Qt 容忍）。 */
static bool xcss_parseMedia(XCssParseCtx* ctx)
{
    XCssMediaRule rule;
    bool progressed = false;
    XMemset(&rule, 0, sizeof(rule));
    if (!xcss_parseMediaList(ctx, &rule.m_media, &rule.m_mediaCount,
                             &progressed)) {
        xcss_freeMediaRule(&rule);
        return false;
    }
    {
        const char* p = xcss_skipWs(ctx->p, ctx->end);
        if (p >= ctx->end || *p != '{') {
            xcss_freeMediaRule(&rule);
            return xcss_failAt(ctx, "@media 缺少 '{'", p);
        }
        ctx->p = p + 1;
    }
    for (;;) {
        const char* p = xcss_skipWs(ctx->p, ctx->end);
        XCssRuleTarget target;
        if (p >= ctx->end) break; /* Qt：媒体块未闭合吞到末尾，容忍。 */
        if (*p == '}') {
            ctx->p = p + 1;
            break;
        }
        if (*p == '@') {
            xcss_freeMediaRule(&rule);
            return xcss_failAt(ctx, "@media 内不允许 at 规则", p);
        }
        target.media = &rule;
        if (!xcss_parseRuleset(ctx, &target)) {
            xcss_freeMediaRule(&rule);
            return false;
        }
    }
    /* 落表。 */
    if (ctx->sheet->m_mediaRuleCount % 4 == 0) {
        int cap = ctx->sheet->m_mediaRuleCount + 4;
        XCssMediaRule* grown = (XCssMediaRule*)XRealloc_System(
            ctx->sheet->m_mediaRules,
            sizeof(XCssMediaRule) * (size_t)cap);
        if (!grown) {
            xcss_freeMediaRule(&rule);
            return xcss_failOom(ctx);
        }
        ctx->sheet->m_mediaRules = grown;
    }
    ctx->sheet->m_mediaRules[ctx->sheet->m_mediaRuleCount++] = rule;
    return true;
}

/** @brief @page [name][:pseudo] (';' | '{' 声明 '}')
 *         （对标 parsePage；选择器名与 pseudoPage 以 ':' 拼接存储，
 *         对标 parsePseudoPage 的 "name:pseudo" 组串口径）。 */
static bool xcss_parsePage(XCssParseCtx* ctx)
{
    XCssPageRule rule;
    const char* p = xcss_skipWs(ctx->p, ctx->end);
    const char* end = ctx->end;
    const char* n;
    size_t len;
    XMemset(&rule, 0, sizeof(rule));
    ctx->p = p;
    /* 页名（可空）与 pseudoPage（':' 起，可空）两段独立（对标 parsePage +
     * parsePseudoPage；"@page :first" 无页名场景 Qt 同样受理）。 */
    if (p < end && xcss_isIdentChar(*p)) {
        n = p;
        while (p < end && xcss_isIdentChar(*p)) ++p;
        len = (size_t)(p - n);
        rule.m_selector = xcss_newString(n, len);
        if (!rule.m_selector) {
            xcss_freePageRule(&rule);
            return xcss_failOom(ctx);
        }
        ctx->p = p;
    }
    p = xcss_skipWs(ctx->p, end);
    if (p < end && *p == ':') {
        /* pseudoPage。 */
        XString* joined;
        ++p;
        n = p;
        while (p < end && xcss_isIdentChar(*p)) ++p;
        if (p == n) {
            xcss_freePageRule(&rule);
            return xcss_failAt(ctx, "pseudoPage 缺少名称", p);
        }
        joined = xcss_newString(
            rule.m_selector ? XString_toUtf8(rule.m_selector) : "",
            rule.m_selector ? XString_toUtf8_length(rule.m_selector) : 0);
        if (!joined ||
            !XString_append_with_length_utf8(joined, ":", 1) ||
            !XString_append_with_length_utf8(joined, n, (size_t)(p - n))) {
            if (joined) XString_delete_base(joined);
            xcss_freePageRule(&rule);
            return xcss_failOom(ctx);
        }
        if (rule.m_selector) XString_delete_base(rule.m_selector);
        rule.m_selector = joined;
        ctx->p = p;
    }
    /* ';' 空规则 或 '{' 声明块。 */
    p = xcss_skipWs(ctx->p, end);
    if (p < end && *p == ';') {
        ctx->p = p + 1;
    } else if (p < end && *p == '{') {
        ctx->p = p + 1;
        if (!xcss_parseDeclarations(ctx, &rule.m_declarations,
                                    &rule.m_declarationCount)) {
            xcss_freePageRule(&rule);
            return false;
        }
    } else {
        xcss_freePageRule(&rule);
        return xcss_failAt(ctx, "@page 缺少 ';' 或 '{'", p);
    }
    /* 落表。 */
    if (ctx->sheet->m_pageRuleCount % 4 == 0) {
        int cap = ctx->sheet->m_pageRuleCount + 4;
        XCssPageRule* grown = (XCssPageRule*)XRealloc_System(
            ctx->sheet->m_pageRules,
            sizeof(XCssPageRule) * (size_t)cap);
        if (!grown) {
            xcss_freePageRule(&rule);
            return xcss_failOom(ctx);
        }
        ctx->sheet->m_pageRules = grown;
    }
    ctx->sheet->m_pageRules[ctx->sheet->m_pageRuleCount++] = rule;
    return true;
}

/** @brief 分发 at 规则（对标 parseStyleSheet 的 at 分支）。 */
static bool xcss_parseAtRule(XCssParseCtx* ctx)
{
    const char* p = ctx->p + 1; /* 越过 '@'。 */
    const char* end = ctx->end;
    const char* kw = p;
    size_t len;
    while (p < end && xcss_isIdentChar(*p)) ++p;
    len = (size_t)(p - kw);
    ctx->p = p;
    if (len == 0) return xcss_failAt(ctx, "非法 at 关键字", ctx->p);
    if (len == 7 && XStrncasecmp(kw, "charset", 7) == 0)
        return xcss_parseCharset(ctx);
    if (len == 6 && XStrncasecmp(kw, "import", 6) == 0)
        return xcss_parseImport(ctx);
    if (len == 5 && XStrncasecmp(kw, "media", 5) == 0)
        return xcss_parseMedia(ctx);
    if (len == 4 && XStrncasecmp(kw, "page", 4) == 0)
        return xcss_parsePage(ctx);
    /* 未识别 at 关键字：对标 lexemUntil(RBRACE)，吞到 '}'（含）或末尾。 */
    while (ctx->p < end && *ctx->p != '}') ++ctx->p;
    if (ctx->p < end) ++ctx->p;
    return true;
}

/* ==================== 主解析 ==================== */

/** @brief 样式表主体循环（对标 parseStyleSheet）。 */
static bool xcss_parseSheet(XCssParseCtx* ctx)
{
    for (;;) {
        const char* p = xcss_skipWs(ctx->p, ctx->end);
        XCssRuleTarget target;
        ctx->p = p;
        if (p >= ctx->end) break;
        if (*p == '@') {
            if (!xcss_parseAtRule(ctx)) return false;
            continue;
        }
        if (*p == '{')
            return xcss_failAt(ctx, "孤立的 '{'（缺少选择器）", p);
        if (*p == '}')
            return xcss_failAt(ctx, "多余的 '}'（花括号失衡）", p);
        target.media = NULL;
        if (!xcss_parseRuleset(ctx, &target)) return false;
    }
    return true;
}

bool XCssStyleSheet_parse_ex(XCssStyleSheet* sheet, const char* css,
                             XCssParseError* err)
{
    XCssParseCtx ctx;
    char* buf;
    size_t clen;
    size_t blen;
    if (!sheet) return false;
    if (err) {
        err->m_message = NULL;
        err->m_offset = -1;
        err->m_line = 0;
    }
    XCssStyleSheet_clear(sheet);
    if (!css || !css[0]) return true;
    clen = XStrlen(css);
    buf = (char*)XMalloc_System(clen + 1);
    if (!buf) {
        if (err) {
            err->m_message = "内存不足";
            err->m_offset = -1;
            err->m_line = 0;
        }
        return false;
    }
    blen = xcss_preprocess(css, clen, buf);
    buf[blen] = '\0';
    ctx.p = buf;
    ctx.end = buf + blen;
    ctx.begin = buf;
    ctx.sheet = sheet;
    ctx.err = err;
    if (!xcss_parseSheet(&ctx)) {
        /* 对标 Qt：解析失败整表弃用。 */
        XCssStyleSheet_clear(sheet);
        XFree_System(buf);
        return false;
    }
    XFree_System(buf);
    return true;
}

bool XCssStyleSheet_parse(XCssStyleSheet* sheet, const char* css)
{
    return XCssStyleSheet_parse_ex(sheet, css, NULL);
}

#endif /* XSTYLE_ON */
