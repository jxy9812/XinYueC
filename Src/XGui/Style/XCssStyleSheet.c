#include "XCssStyleSheet.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XMemory.h"

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
    { "text-decoration", XCssProperty_TextDecoration },
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

/** @brief 释放单条规则的字符串/数组。 */
static void xcss_freeRule(XCssStyleRule* rule)
{
    int i;
    if (!rule) return;
    for (i = 0; i < rule->m_selectorCount; ++i) {
        int bi;
        for (bi = 0; bi < rule->m_selectors[i].m_basicCount; ++bi) {
            XCssBasicSelector* b = &rule->m_selectors[i].m_basics[bi];
            if (b->m_elementName) XString_delete_base(b->m_elementName);
            if (b->m_id) XString_delete_base(b->m_id);
            if (b->m_attribute.m_name)
                XString_delete_base(b->m_attribute.m_name);
            if (b->m_attribute.m_value)
                XString_delete_base(b->m_attribute.m_value);
        }
        if (rule->m_selectors[i].m_basics)
            XFree_System(rule->m_selectors[i].m_basics);
    }
    if (rule->m_selectors) XFree_System(rule->m_selectors);
    for (i = 0; i < rule->m_declarationCount; ++i)
        if (rule->m_declarations[i].m_value)
            XString_delete_base(rule->m_declarations[i].m_value);
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
            XMemset(&sheet->m_rules[ri], 0, sizeof(XCssStyleRule));
        sheet->m_ruleCapacity = cap;
    }
    rule = &sheet->m_rules[sheet->m_ruleCount++];
    XMemset(rule, 0, sizeof(*rule));
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
    XMemset(d, 0, sizeof(*d));
    d->m_propertyId = id;
    d->m_important = important;
    d->m_value = XString_create_with_length_utf8(value, vlen);
    rule->m_declarationCount++;
    return d->m_value != NULL;
}

/** @brief 在规则中追加一个空选择器槽（返回其指针）。 */
static XCssSelector* xcss_appendSelectorSlot(XCssStyleRule* rule)
{
    XCssSelector* sel;
    if (rule->m_selectorCount % 4 == 0) {
        int cap = rule->m_selectorCount + 4;
        XCssSelector* grown = (XCssSelector*)XRealloc_System(
            rule->m_selectors, sizeof(XCssSelector) * (size_t)cap);
        if (!grown) return NULL;
        rule->m_selectors = grown;
    }
    sel = &rule->m_selectors[rule->m_selectorCount];
    XMemset(sel, 0, sizeof(*sel));
    rule->m_selectorCount++;
    return sel;
}

/** @brief 向选择器链追加一个基础段（relation 为与前段的关系）。 */
static bool xcss_appendBasic(XCssSelector* sel, XCssRelation rel,
                             const char* element, size_t elen,
                             const char* id, size_t idlen,
                             uint32_t pseudos,
                             const char* attrName, size_t anlen,
                             const char* attrValue, size_t avlen,
                             XCssValueMatch match)
{
    XCssBasicSelector* b;
    if (sel->m_basicCount % 4 == 0) {
        int cap = sel->m_basicCount + 4;
        XCssBasicSelector* grown = (XCssBasicSelector*)XRealloc_System(
            sel->m_basics, sizeof(XCssBasicSelector) * (size_t)cap);
        if (!grown) return false;
        sel->m_basics = grown;
    }
    b = &sel->m_basics[sel->m_basicCount];
    XMemset(b, 0, sizeof(*b));
    b->m_relationToPrev = rel;
    if (elen) {
        b->m_elementName = XString_create_with_length_utf8(element, elen);
        if (!b->m_elementName) return false;
    }
    if (idlen) {
        b->m_id = XString_create_with_length_utf8(id, idlen);
        if (!b->m_id) return false;
    }
    b->m_pseudoClasses = pseudos;
    if (anlen) {
        b->m_attribute.m_name = XString_create_with_length_utf8(attrName,
                                                                anlen);
        if (!b->m_attribute.m_name) return false;
        if (avlen) {
            b->m_attribute.m_value = XString_create_with_length_utf8(
                attrValue, avlen);
            if (!b->m_attribute.m_value) return false;
        }
        b->m_attribute.m_match = match;
    }
    sel->m_basicCount++;
    sel->m_specificity += (idlen ? 100 : 0) + (pseudos ? 10 : 0) +
                          (elen ? 1 : 0);
    return true;
}

/** @brief 跳过空白与注释，返回推进后指针。 */
static const char* xcss_skipWs(const char* p, const char* end)
{
    for (;;) {
        while (p < end && XIsSpace((unsigned char)*p)) ++p;
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
        while (p < end && (XIsAlnum((unsigned char)*p) || *p == '-')) ++p;
        len = (size_t)(p - name);
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
        else if (len == 8 && XStrncasecmp(name, "readonly", 8) == 0)
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
    end = css + XStrlen(css);
    p = xcss_skipWs(p, end);
    while (p < end) {
        /* --- 选择器段：到 '{' 为止（逗号分隔多选择器 + 关系链）。 --- */
        XCssStyleRule* rule = NULL;
        while (p < end && *p != '{' && *p != '}') {
            XCssSelector* sel;
            XCssRelation rel = XCssRelation_None;
            if (!rule) rule = xcss_appendRule(sheet);
            if (!rule) return false;
            sel = xcss_appendSelectorSlot(rule);
            if (!sel) return false;
            /* 基础选择器链：element#id[pseudo][attr]，段间空格(后代)/>(子代)。 */
            for (;;) {
                char element[64];
                char idbuf[64];
                char attrN[64];
                char attrV[64];
                size_t elen = 0;
                size_t idlen = 0;
                size_t anlen = 0;
                size_t avlen = 0;
                uint32_t pseudos = 0;
                XCssValueMatch match = XCssValueMatch_NoMatch;
                const char* beforeWs = p;
                element[0] = idbuf[0] = attrN[0] = attrV[0] = '\0';
                p = xcss_skipWs(p, end);
                if (p >= end || *p == '{' || *p == '}' || *p == ',') break;
                /* 前导关系符（兜底）：'>' 子代。 */
                if (sel->m_basicCount > 0 && p < end && *p == '>') {
                    rel = XCssRelation_Parent;
                    p = xcss_skipWs(p + 1, end);
                }
                (void)beforeWs;
                /* 元素名（.ClassName 亦视为元素名）。 */
                {
                    const char* n = p;
                    while (p < end && (XIsAlnum((unsigned char)*p) ||
                                       *p == '_' || *p == '-' || *p == '.'))
                        ++p;
                    elen = (size_t)(p - n);
                    if (elen >= sizeof(element)) elen = sizeof(element) - 1;
                    XMemcpy(element, n, elen);
                    element[elen] = '\0';
                    if (elen && element[0] == '.') {
                        XMemmove(element, element + 1, elen);
                        --elen;
                        element[elen] = '\0';
                    }
                }
                p = xcss_skipWs(p, end);
                /* ID 选择器。 */
                if (p < end && *p == '#') {
                    const char* n;
                    ++p;
                    n = p;
                    while (p < end && (XIsAlnum((unsigned char)*p) ||
                                       *p == '_' || *p == '-'))
                        ++p;
                    idlen = (size_t)(p - n);
                    if (idlen >= sizeof(idbuf)) idlen = sizeof(idbuf) - 1;
                    XMemcpy(idbuf, n, idlen);
                    idbuf[idlen] = '\0';
                    p = xcss_skipWs(p, end);
                }
                /* 属性选择器：[name]、[name=value]、[name~=v]、[name|=v]、
                 * [name^=v]、[name$=v]、[name*=v]（对标 ValueMatchType）。 */
                if (p < end && *p == '[') {
                    const char* n;
                    match = XCssValueMatch_Equal;
                    ++p;
                    n = p;
                    while (p < end && *p != ']' && *p != '=' &&
                           *p != '~' && *p != '|' && *p != '^' &&
                           *p != '$' && *p != '*')
                        ++p;
                    anlen = (size_t)(p - n);
                    while (anlen > 0 &&
                           XIsSpace((unsigned char)n[anlen - 1]))
                        --anlen;
                    if (anlen >= sizeof(attrN)) anlen = sizeof(attrN) - 1;
                    XMemcpy(attrN, n, anlen);
                    attrN[anlen] = '\0';
                    /* 操作符。 */
                    if (p < end && (*p == '~' || *p == '|' || *p == '^' ||
                                    *p == '$' || *p == '*')) {
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
                    if (p < end && *p != ']') {
                        const char* v;
                        v = p;
                        if (p < end && (*p == '"' || *p == '\'')) {
                            char q = *p;
                            ++p;
                            v = p;
                            while (p < end && *p != q) ++p;
                            avlen = (size_t)(p - v);
                            if (p < end) ++p;
                        } else {
                            while (p < end && *p != ']' &&
                                   !XIsSpace((unsigned char)*p))
                                ++p;
                            avlen = (size_t)(p - v);
                        }
                        if (avlen >= sizeof(attrV))
                            avlen = sizeof(attrV) - 1;
                        XMemcpy(attrV, v, avlen);
                        attrV[avlen] = '\0';
                    }
                    while (p < end && *p != ']') ++p;
                    if (p < end) ++p;
                    p = xcss_skipWs(p, end);
                }
                /* 伪类。 */
                p = xcss_parsePseudos(p, end, &pseudos);
                if (!xcss_appendBasic(sel, rel, element, elen, idbuf, idlen,
                                      pseudos, attrN, anlen, attrV, avlen,
                                      match))
                    return false;
                /* 段尾关系：空白由 p[-1] 判定（伪类解析已跳过空白，
                 * 不能再用 skipWs 前后比较）。 */
                {
                    bool wsBefore = (p > css &&
                        (p[-1] == ' ' || p[-1] == '\t' ||
                         p[-1] == '\n' || p[-1] == '\r'));
                    const char* q = xcss_skipWs(p, end);
                    if (q < end && *q == '>') {
                        rel = XCssRelation_Parent;
                        p = xcss_skipWs(q + 1, end);
                        continue;
                    }
                    if (wsBefore && q < end && *q != ',' && *q != '{' &&
                        *q != '}') {
                        rel = XCssRelation_Ancestor;
                        p = q;
                        continue;
                    }
                    p = q;
                }
                break;
            }
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
            while (nlen > 0 && XIsSpace((unsigned char)name[nlen - 1]))
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
            while (vlen > 0 && XIsSpace((unsigned char)vstart[0])) {
                ++vstart;
                --vlen;
            }
            while (vlen > 0 && XIsSpace((unsigned char)vstart[vlen - 1]))
                --vlen;
            /* !important。 */
            if (vlen > 10 &&
                XStrncasecmp(vstart + vlen - 10, "!important", 10) == 0) {
                important = true;
                vlen -= 10;
                while (vlen > 0 &&
                       XIsSpace((unsigned char)vstart[vlen - 1]))
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
