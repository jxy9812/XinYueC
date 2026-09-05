/******************************************************************************
 * @file       XConditionalFormatting.c
 * @brief      XConditionalFormatting 条件格式类实现（对标 QXlsx::ConditionalFormatting）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XConditionalFormatting.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>

#include <stdio.h>


/* ========== 创建与初始化 ========== */

XConditionalFormatting* XConditionalFormatting_create(void)
{
    XConditionalFormatting* self = (XConditionalFormatting*)XMalloc_System(sizeof(XConditionalFormatting));
    if (!self) return NULL;
    memset(self, 0, sizeof(XConditionalFormatting));
    self->m_rules = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XConditionalFormatting_Rule), true);
    self->m_ranges = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XCellRange), true);
    if (!self->m_rules || !self->m_ranges) {
        XConditionalFormatting_delete(self);
        return NULL;
    }
    return self;
}

static void rule_deinit(XConditionalFormatting_Rule* rule)
{
    if (!rule) return;
    if (rule->m_formula1) XString_delete_base(rule->m_formula1);
    if (rule->m_formula2) XString_delete_base(rule->m_formula2);
    if (rule->m_formula3) XString_delete_base(rule->m_formula3);
    if (rule->m_text) XString_delete_base(rule->m_text);
    if (rule->m_timePeriod) XString_delete_base(rule->m_timePeriod);
    if (rule->m_format) XFormat_delete(rule->m_format);
    memset(rule, 0, sizeof(*rule));
}

static bool append_rule(XConditionalFormatting* self, XConditionalFormatting_Rule* rule)
{
    if (!self || !self->m_rules || !rule ||
        !XVector_push_back_2(self->m_rules, rule, 1)) {
        rule_deinit(rule);
        return false;
    }
    return true;
}

XConditionalFormatting* XConditionalFormatting_copy(const XConditionalFormatting* other)
{
    if (!other) return NULL;
    XConditionalFormatting* self = XConditionalFormatting_create();
    if (!self) return NULL;
    /* 复制规则 */
    for (size_t i = 0; i < XVector_size_base(other->m_rules); ++i) {
        XConditionalFormatting_Rule* src = (XConditionalFormatting_Rule*)XVector_at_base(other->m_rules, i);
        XConditionalFormatting_Rule dst;
        memset(&dst, 0, sizeof(dst));
        dst.m_ruleType = src->m_ruleType;
        dst.m_highlightType = src->m_highlightType;
        /* BUG FIX: 之前是 dst.m_format = src->m_format; 浅拷贝指针会导致
           XConditionalFormatting_delete 多次释放同一 XFormat 触发 double-free。
           必须创建独立的 XFormat 副本。 */
        if (src->m_format)
        {
            dst.m_format = XFormat_create();
            if (dst.m_format) XFormat_copy(dst.m_format, src->m_format);
        }
        dst.m_color1 = src->m_color1;
        dst.m_color2 = src->m_color2;
        dst.m_color3 = src->m_color3;
        dst.m_valType1 = src->m_valType1;
        dst.m_valType2 = src->m_valType2;
        dst.m_valType3 = src->m_valType3;
        dst.m_rank = src->m_rank;
        dst.m_stdDev = src->m_stdDev;
        dst.m_showData = src->m_showData;
        dst.m_stopIfTrue = src->m_stopIfTrue;
        if (src->m_formula1) { dst.m_formula1 = XString_create(); XCopy(dst.m_formula1, src->m_formula1); }
        if (src->m_formula2) { dst.m_formula2 = XString_create(); XCopy(dst.m_formula2, src->m_formula2); }
        if (src->m_formula3) { dst.m_formula3 = XString_create(); XCopy(dst.m_formula3, src->m_formula3); }
        if (src->m_text) dst.m_text = XString_create_copy(src->m_text);
        if (src->m_timePeriod) dst.m_timePeriod = XString_create_copy(src->m_timePeriod);
        bool copied = (!src->m_format || dst.m_format) &&
            (!src->m_formula1 || dst.m_formula1) && (!src->m_formula2 || dst.m_formula2) &&
            (!src->m_formula3 || dst.m_formula3) && (!src->m_text || dst.m_text) &&
            (!src->m_timePeriod || dst.m_timePeriod);
        if (!copied || !append_rule(self, &dst)) {
            if (!copied) rule_deinit(&dst);
            XConditionalFormatting_delete(self);
            return NULL;
        }
    }
    /* 复制范围 */
    for (size_t i = 0; i < XVector_size_base(other->m_ranges); ++i) {
        XCellRange* r = (XCellRange*)XVector_at_base(other->m_ranges, i);
        if (!XVector_push_back_2(self->m_ranges, r, 1)) {
            XConditionalFormatting_delete(self);
            return NULL;
        }
    }
    return self;
}

void XConditionalFormatting_delete(XConditionalFormatting* self)
{
    if (!self) return;
    /* 释放规则中的字符串与 XFormat 副本 */
    for (size_t i = 0; self->m_rules && i < XVector_size_base(self->m_rules); ++i) {
        XConditionalFormatting_Rule* r = (XConditionalFormatting_Rule*)XVector_at_base(self->m_rules, i);
        rule_deinit(r);
    }
    if (self->m_rules) XVector_delete_base(self->m_rules);
    if (self->m_ranges) XVector_delete_base(self->m_ranges);
    XFree_System(self);
}

/* ========== 规则添加 ========== */

static XFormat* copy_rule_format(const XFormat* format)
{
    if (!format) return NULL;
    XFormat* copy = XFormat_create();
    if (copy) XFormat_copy(copy, format);
    return copy;
}

bool XConditionalFormatting_addHighlightCellsRule(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XFormat* format, bool stopIfTrue)
{
    if (!self || !self->m_rules || type < XCF_Highlight_LessThan ||
        type > XCF_Highlight_Expression) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = copy_rule_format(format);
    if (format && !rule.m_format) return false;
    rule.m_stopIfTrue = stopIfTrue;
    return append_rule(self, &rule);
}

bool XConditionalFormatting_addHighlightCellsRule2(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1,
    const XFormat* format, bool stopIfTrue)
{
    if (!self || !self->m_rules || type < XCF_Highlight_LessThan ||
        type > XCF_Highlight_Expression) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = copy_rule_format(format);
    if (format && !rule.m_format) return false;
    rule.m_stopIfTrue = stopIfTrue;
    if (formula1) rule.m_formula1 = XString_create_copy(formula1);
    if (formula1 && (type == XCF_Highlight_ContainsText ||
        type == XCF_Highlight_NotContainsText || type == XCF_Highlight_BeginsWith ||
        type == XCF_Highlight_EndsWith)) rule.m_text = XString_create_copy(formula1);
    if (formula1 && type == XCF_Highlight_TimePeriod)
        rule.m_timePeriod = XString_create_copy(formula1);
    if (formula1 && type >= XCF_Highlight_Top && type <= XCF_Highlight_BottomPercent)
        rule.m_rank = atoi(XString_toUtf8(formula1));
    if ((formula1 && !rule.m_formula1) ||
        (formula1 && type >= XCF_Highlight_ContainsText && type <= XCF_Highlight_EndsWith &&
         !rule.m_text) || (formula1 && type == XCF_Highlight_TimePeriod && !rule.m_timePeriod)) {
        rule_deinit(&rule);
        return false;
    }
    return append_rule(self, &rule);
}

bool XConditionalFormatting_addHighlightCellsRule3(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1, const XString* formula2,
    const XFormat* format, bool stopIfTrue)
{
    if (!self || !self->m_rules || type < XCF_Highlight_LessThan ||
        type > XCF_Highlight_Expression) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = copy_rule_format(format);
    if (format && !rule.m_format) return false;
    rule.m_stopIfTrue = stopIfTrue;
    if (formula1) rule.m_formula1 = XString_create_copy(formula1);
    if (formula2) rule.m_formula2 = XString_create_copy(formula2);
    if ((formula1 && !rule.m_formula1) || (formula2 && !rule.m_formula2)) {
        rule_deinit(&rule);
        return false;
    }
    return append_rule(self, &rule);
}

bool XConditionalFormatting_addDataBarRule(XConditionalFormatting* self,
    const XColor* color, bool showData, bool stopIfTrue)
{
    if (!self || !self->m_rules || !color || !XColor_isValid(color)) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_DataBar;
    rule.m_color1 = *color;
    rule.m_showData = showData;
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Max;
    return append_rule(self, &rule);
}

bool XConditionalFormatting_addDataBarRuleEx(XConditionalFormatting* self,
    const XColor* color, XConditionalFormatting_ValueObjectType type1, const XString* val1,
    XConditionalFormatting_ValueObjectType type2, const XString* val2,
    bool showData, bool stopIfTrue)
{
    if (!self || !self->m_rules || !color || !XColor_isValid(color) ||
        type1 < XCF_VOT_Formula || type1 > XCF_VOT_Percentile ||
        type2 < XCF_VOT_Formula || type2 > XCF_VOT_Percentile) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_DataBar;
    rule.m_color1 = *color;
    rule.m_valType1 = type1;
    rule.m_valType2 = type2;
    rule.m_showData = showData;
    rule.m_stopIfTrue = stopIfTrue;
    if (val1) rule.m_formula1 = XString_create_copy(val1);
    if (val2) rule.m_formula2 = XString_create_copy(val2);
    if ((val1 && !rule.m_formula1) || (val2 && !rule.m_formula2)) {
        rule_deinit(&rule);
        return false;
    }
    return append_rule(self, &rule);
}

bool XConditionalFormatting_add2ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* maxColor, bool stopIfTrue)
{
    if (!self || !self->m_rules || !minColor || !maxColor ||
        !XColor_isValid(minColor) || !XColor_isValid(maxColor)) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_ColorScale2;
    rule.m_color1 = *minColor;
    rule.m_color2 = *maxColor;
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Max;
    return append_rule(self, &rule);
}

bool XConditionalFormatting_add3ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* midColor, const XColor* maxColor, bool stopIfTrue)
{
    if (!self || !self->m_rules || !minColor || !midColor || !maxColor ||
        !XColor_isValid(minColor) || !XColor_isValid(midColor) ||
        !XColor_isValid(maxColor)) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_ColorScale3;
    rule.m_color1 = *minColor;
    rule.m_color2 = *midColor;
    rule.m_color3 = *maxColor;
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Percentile;
    rule.m_valType3 = XCF_VOT_Max;
    rule.m_formula2 = XString_create_utf8("50");
    if (!rule.m_formula2) return false;
    return append_rule(self, &rule);
}

/* ========== 范围管理 ========== */

XCellRange* XConditionalFormatting_ranges(const XConditionalFormatting* self, int* count)
{
    if (count) *count = (self && self->m_ranges) ? (int)XVector_size_base(self->m_ranges) : 0;
    return (self && self->m_ranges) ? (XCellRange*)XVector_data(self->m_ranges) : NULL;
}

void XConditionalFormatting_addCell(XConditionalFormatting* self, const XCellReference* cell)
{
    if (!self || !cell || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCellReference(&range, cell);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addCellRc(XConditionalFormatting* self, int row, int col)
{
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCell(&range, row, col);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addRange(XConditionalFormatting* self, int firstRow, int firstCol, int lastRow, int lastCol)
{
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setFullRange(&range, firstRow, firstCol, lastRow, lastCol);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addRangeEx(XConditionalFormatting* self, const XCellRange* range)
{
    if (!self || !range || !self->m_ranges) return;
    if (XCellRange_isValid(range)) XVector_push_back_2(self->m_ranges, (void*)range, 1);
}

int XConditionalFormatting_rulesCount(const XConditionalFormatting* self)
{
    return (self && self->m_rules) ? (int)XVector_size_base(self->m_rules) : 0;
}

XConditionalFormatting_Rule* XConditionalFormatting_rule(const XConditionalFormatting* self, int index)
{
    if (!self || !self->m_rules || index < 0 || (size_t)index >= XVector_size_base(self->m_rules)) return NULL;
    return (XConditionalFormatting_Rule*)XVector_at_base(self->m_rules, (size_t)index);
}
