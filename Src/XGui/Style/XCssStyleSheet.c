#include "XCssStyleSheet.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#if XSTYLE_ON

/* ==================== 属性名表 ==================== */

/** @brief 属性名/ID 映射表（对标 qcssparser 的 findKnownValue）。 */
static const struct
{
    const char* name;
    XCssProperty id;
} k_propNames[] = {
    { "color", XCssProperty_Color },
    { "background-color", XCssProperty_BackgroundColor },
    { "background", XCssProperty_Background },
    { "border", XCssProperty_Border },
    { "border-color", XCssProperty_BorderColor },
    { "border-width", XCssProperty_BorderWidth },
    { "border-radius", XCssProperty_BorderRadius },
    { "padding", XCssProperty_Padding },
    { "padding-left", XCssProperty_PaddingLeft },
    { "padding-right", XCssProperty_PaddingRight },
    { "padding-top", XCssProperty_PaddingTop },
    { "padding-bottom", XCssProperty_PaddingBottom },
    { "margin", XCssProperty_Margin },
    { "font", XCssProperty_Font },
    { "font-family", XCssProperty_FontFamily },
    { "font-size", XCssProperty_FontSize },
    { "font-weight", XCssProperty_FontWeight },
    { "font-style", XCssProperty_FontStyle },
    { "min-width", XCssProperty_MinWidth },
    { "min-height", XCssProperty_MinHeight },
    { "max-width", XCssProperty_MaxWidth },
    { "max-height", XCssProperty_MaxHeight },
    { "width", XCssProperty_Width },
    { "height", XCssProperty_Height },
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
        if (strlen(n) == len && strncasecmp(n, name, len) == 0)
            return k_propNames[i].id;
    }
    return XCssProperty_Unknown;
}

/* ==================== 生命周期 ==================== */

void XCssStyleSheet_init(XCssStyleSheet* sheet)
{
    if (!sheet) return;
    memset(sheet, 0, sizeof(*sheet));
}

/** @brief 释放单条规则的字符串/数组。 */
static void xcss_freeRule(XCssStyleRule* rule)
{
    int i;
    if (!rule) return;
    for (i = 0; i < rule->m_selectorCount; ++i) {
        if (rule->m_selectors[i].m_basic.m_elementName)
            XString_delete_base(rule->m_selectors[i].m_basic.m_elementName);
        if (rule->m_selectors[i].m_basic.m_id)
            XString_delete_base(rule->m_selectors[i].m_basic.m_id);
    }
    if (rule->m_selectors) XFree_System(rule->m_selectors);
    for (i = 0; i < rule->m_declarationCount; ++i)
        if (rule->m_declarations[i].m_value)
            XString_delete_base(rule->m_declarations[i].m_value);
    if (rule->m_declarations) XFree_System(rule->m_declarations);
    memset(rule, 0, sizeof(*rule));
}

void XCssStyleSheet_clear(XCssStyleSheet* sheet)
{
    int i;
    if (!sheet) return;
    for (i = 0; i < sheet->m_ruleCount; ++i)
        xcss_freeRule(&sheet->m_rules[i]);
    if (sheet->m_rules) XFree_System(sheet->m_rules);
    sheet->m_rules = NULL;
    sheet->m_ruleCount = 0;
    sheet->m_ruleCapacity = 0;
}

/* ==================== 解析辅助 ==================== */

/** @brief 追加一条空规则（返回新规则指针）。 */
static XCssStyleRule* xcss_appendRule(XCssStyleSheet* sheet)
{
    XCssStyleRule* rule;
    if (sheet->m_ruleCount >= sheet->m_ruleCapacity) {
        int cap = sheet->m_ruleCapacity > 0 ? sheet->m_ruleCapacity * 2 : 8;
        int oldCap = sheet->m_ruleCapacity;
        int ri;
        XCssStyleRule* grown = (XCssStyleRule*)XRealloc_System(
            sheet->m_rules, sizeof(XCssStyleRule) * (size_t)cap);
        if (!grown) return NULL;
        sheet->m_rules = grown;
        for (ri = oldCap; ri < cap; ++ri)
            memset(&sheet->m_rules[ri], 0, sizeof(XCssStyleRule));
        sheet->m_ruleCapacity = cap;
    }
    rule = &sheet->m_rules[sheet->m_ruleCount++];
    memset(rule, 0, sizeof(*rule));
    return rule;
}

/** @brief 追加声明。 */
static bool xcss_appendDecl(XCssStyleRule* rule, XCssProperty id,
                            const char* value, size_t vlen, bool important)
{
    XCssDeclaration* d;
    if (rule->m_declarationCount % 8 == 0) {
        int cap = rule->m_declarationCount + 8;
        XCssDeclaration* grown = (XCssDeclaration*)XRealloc_System(
            rule->m_declarations,
            sizeof(XCssDeclaration) * (size_t)cap);
        if (!grown) return false;
        rule->m_declarations = grown;
    }
    d = &rule->m_declarations[rule->m_declarationCount];
    memset(d, 0, sizeof(*d));
    d->m_propertyId = id;
    d->m_important = important;
    d->m_value = XString_create_with_length_utf8(value, vlen);
    rule->m_declarationCount++;
    return d->m_value != NULL;
}

/** @brief 追加选择器。 */
static bool xcss_appendSelector(XCssStyleRule* rule,
                                const char* element, size_t elen,
                                const char* id, size_t idlen,
                                uint32_t pseudos)
{
    XCssSelector* sel;
    if (rule->m_selectorCount % 4 == 0) {
        int cap = rule->m_selectorCount + 4;
        XCssSelector* grown = (XCssSelector*)XRealloc_System(
            rule->m_selectors, sizeof(XCssSelector) * (size_t)cap);
        if (!grown) return false;
        rule->m_selectors = grown;
    }
    sel = &rule->m_selectors[rule->m_selectorCount];
    memset(sel, 0, sizeof(*sel));
    if (elen) {
        sel->m_basic.m_elementName = XString_create_with_length_utf8(
            element, elen);
        if (!sel->m_basic.m_elementName) return false;
    }
    if (idlen) {
        sel->m_basic.m_id = XString_create_with_length_utf8(id, idlen);
        if (!sel->m_basic.m_id) return false;
    }
    sel->m_basic.m_pseudoClasses = pseudos;
    /* 特异度：id*100 + 伪类/类*10 + 元素（对标 specificity 粗略排序）。 */
    sel->m_specificity = (idlen ? 100 : 0) + (pseudos ? 10 : 0) +
                         (elen ? 1 : 0);
    rule->m_selectorCount++;
    return true;
}

/** @brief 跳过空白与注释，返回推进后指针。 */
static const char* xcss_skipWs(const char* p, const char* end)
{
    for (;;) {
        while (p < end && isspace((unsigned char)*p)) ++p;
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) ++p;
            if (p + 1 < end) p += 2;
            continue;
        }
        return p;
    }
}

/** @brief 解析伪类片段（:hover 等），推进 p 并累积伪类位。 */
static const char* xcss_parsePseudos(const char* p, const char* end,
                                     uint32_t* pseudos)
{
    while (p < end && *p == ':') {
        const char* name;
        size_t len;
        ++p;
        name = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '-')) ++p;
        len = (size_t)(p - name);
        if (len == 5 && strncasecmp(name, "hover", 5) == 0)
            *pseudos |= XCssPseudo_Hover;
        else if (len == 7 && strncasecmp(name, "pressed", 7) == 0)
            *pseudos |= XCssPseudo_Pressed;
        else if (len == 5 && strncasecmp(name, "focus", 5) == 0)
            *pseudos |= XCssPseudo_Focus;
        else if (len == 8 && strncasecmp(name, "disabled", 8) == 0)
            *pseudos |= XCssPseudo_Disabled;
        else if (len == 7 && strncasecmp(name, "enabled", 7) == 0)
            *pseudos |= XCssPseudo_Enabled;
        else if (len == 7 && strncasecmp(name, "checked", 7) == 0)
            *pseudos |= XCssPseudo_Checked;
        else if (len == 8 && strncasecmp(name, "selected", 8) == 0)
            *pseudos |= XCssPseudo_Selected;
        else if (len == 8 && strncasecmp(name, "readonly", 8) == 0)
            *pseudos |= XCssPseudo_ReadOnly;
        p = xcss_skipWs(p, end);
    }
    return p;
}

/* ==================== 主解析 ==================== */

bool XCssStyleSheet_parse(XCssStyleSheet* sheet, const char* css)
{
    const char* p;
    const char* end;
    if (!sheet) return false;
    XCssStyleSheet_clear(sheet);
    if (!css) return true;
    p = css;
    end = css + strlen(css);
    p = xcss_skipWs(p, end);
    while (p < end) {
        /* --- 选择器段：到 '{' 为止（逗号分隔多选择器）。 --- */
        XCssStyleRule* rule = NULL;
        while (p < end && *p != '{' && *p != '}') {
            char element[64];
            char id[64];
            uint32_t pseudos = 0;
            size_t elen = 0;
            size_t idlen = 0;
            /* 单个选择器：element#id:pseudo。 */
            p = xcss_skipWs(p, end);
            if (p >= end || *p == '{' || *p == '}') break;
            element[0] = '\0';
            id[0] = '\0';
            /* 元素名。 */
            {
                const char* n = p;
                while (p < end && (isalnum((unsigned char)*p) ||
                                   *p == '_' || *p == '-' || *p == '.'))
                    ++p;
                elen = (size_t)(p - n);
                if (elen >= sizeof(element)) elen = sizeof(element) - 1;
                memcpy(element, n, elen);
                element[elen] = '\0';
                /* 类选择器 ".XFoo" 视作元素名（控件类匹配）。 */
                if (elen && element[0] == '.') {
                    memmove(element, element + 1, elen);
                    --elen;
                    element[elen] = '\0';
                }
            }
            p = xcss_skipWs(p, end);
            /* ID。 */
            if (p < end && *p == '#') {
                const char* n;
                ++p;
                n = p;
                while (p < end && (isalnum((unsigned char)*p) ||
                                   *p == '_' || *p == '-'))
                    ++p;
                idlen = (size_t)(p - n);
                if (idlen >= sizeof(id)) idlen = sizeof(id) - 1;
                memcpy(id, n, idlen);
                id[idlen] = '\0';
                p = xcss_skipWs(p, end);
            }
            /* 伪类。 */
            p = xcss_parsePseudos(p, end, &pseudos);
            /* 记录（无 rule 则先建）。 */
            if (!rule) rule = xcss_appendRule(sheet);
            if (!rule) return false;
            xcss_appendSelector(rule, element, elen, id, idlen, pseudos);
            /* 逗号继续；否则等 '{'。 */
            if (p < end && *p == ',') {
                ++p;
                continue;
            }
            break;
        }
        p = xcss_skipWs(p, end);
        if (p >= end) break;
        if (*p != '{') {
            /* 非规则段（如 '}'），跳过。 */
            ++p;
            p = xcss_skipWs(p, end);
            continue;
        }
        ++p; /* 越过 '{'。 */
        /* --- 声明段：到 '}'。 --- */
        while (p < end && *p != '}') {
            const char* name;
            const char* value;
            const char* vstart;
            size_t nlen;
            size_t vlen;
            XCssProperty id;
            bool important = false;
            p = xcss_skipWs(p, end);
            if (p >= end || *p == '}') break;
            name = p;
            while (p < end && *p != ':' && *p != ';' && *p != '}') ++p;
            nlen = (size_t)(p - name);
            while (nlen > 0 && isspace((unsigned char)name[nlen - 1]))
                --nlen;
            if (p < end && *p != ':') {
                /* 无冒号：跳过该片段。 */
                p = xcss_skipWs(p, end);
                if (p < end && *p == ';') ++p;
                continue;
            }
            ++p; /* 越过 ':'。 */
            value = p;
            while (p < end && *p != ';' && *p != '}') ++p;
            vstart = value;
            vlen = (size_t)(p - value);
            /* 去首尾空白。 */
            while (vlen > 0 && isspace((unsigned char)vstart[0])) {
                ++vstart;
                --vlen;
            }
            while (vlen > 0 && isspace((unsigned char)vstart[vlen - 1]))
                --vlen;
            /* !important。 */
            if (vlen > 10 &&
                strncasecmp(vstart + vlen - 10, "!important", 10) == 0) {
                important = true;
                vlen -= 10;
                while (vlen > 0 &&
                       isspace((unsigned char)vstart[vlen - 1]))
                    --vlen;
            }
            id = xcss_findProperty(name, nlen);
            if (id != XCssProperty_Unknown && rule && vlen > 0)
                xcss_appendDecl(rule, id, vstart, vlen, important);
            if (p < end && *p == ';') ++p;
        }
        if (p < end) ++p; /* 越过 '}'。 */
        p = xcss_skipWs(p, end);
        rule = NULL;
    }
    return true;
}

#endif /* XSTYLE_ON */
