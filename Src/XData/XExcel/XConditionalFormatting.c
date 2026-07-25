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
    self->m_rules = XVector_create_ex(sizeof(XConditionalFormatting_Rule), true);
    self->m_ranges = XVector_create_ex(sizeof(XCellRange), true);
    return self;
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
        dst.m_showData = src->m_showData;
        dst.m_stopIfTrue = src->m_stopIfTrue;
        if (src->m_formula1) { dst.m_formula1 = XString_create(); XString_copy_base(dst.m_formula1, src->m_formula1); }
        if (src->m_formula2) { dst.m_formula2 = XString_create(); XString_copy_base(dst.m_formula2, src->m_formula2); }
        XVector_push_back_2(self->m_rules, &dst, 1);
    }
    /* 复制范围 */
    for (size_t i = 0; i < XVector_size_base(other->m_ranges); ++i) {
        XCellRange* r = (XCellRange*)XVector_at_base(other->m_ranges, i);
        XVector_push_back_2(self->m_ranges, r, 1);
    }
    return self;
}

void XConditionalFormatting_delete(XConditionalFormatting* self)
{
    if (!self) return;
    /* 释放规则中的字符串与 XFormat 副本 */
    for (size_t i = 0; i < XVector_size_base(self->m_rules); ++i) {
        XConditionalFormatting_Rule* r = (XConditionalFormatting_Rule*)XVector_at_base(self->m_rules, i);
        if (r->m_formula1) { XString_deinit_base(r->m_formula1); XFree_System(r->m_formula1); }
        if (r->m_formula2) { XString_deinit_base(r->m_formula2); XFree_System(r->m_formula2); }
        /* BUG FIX: delete 释放 copy 时深拷贝得到的 XFormat 副本 */
        if (r->m_format) XFormat_delete(r->m_format);
    }
    if (self->m_rules) { XVector_deinit_base(self->m_rules); XFree_System(self->m_rules); }
    if (self->m_ranges) { XVector_deinit_base(self->m_ranges); XFree_System(self->m_ranges); }
    XFree_System(self);
}

/* ========== 规则添加 ========== */

bool XConditionalFormatting_addHighlightCellsRule(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XFormat* format, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = (XFormat*)format;
    rule.m_stopIfTrue = stopIfTrue;
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_addHighlightCellsRule2(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1,
    const XFormat* format, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = (XFormat*)format;
    rule.m_stopIfTrue = stopIfTrue;
    if (formula1) { rule.m_formula1 = XString_create(); XString_append(rule.m_formula1, formula1); }
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_addHighlightCellsRule3(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1, const XString* formula2,
    const XFormat* format, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_HighlightCellsRule;
    rule.m_highlightType = type;
    rule.m_format = (XFormat*)format;
    rule.m_stopIfTrue = stopIfTrue;
    if (formula1) { rule.m_formula1 = XString_create(); XString_append(rule.m_formula1, formula1); }
    if (formula2) { rule.m_formula2 = XString_create(); XString_append(rule.m_formula2, formula2); }
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_addDataBarRule(XConditionalFormatting* self,
    const XColor* color, bool showData, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_DataBar;
    rule.m_color1 = color ? *color : XColor_create();
    rule.m_showData = showData;
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Max;
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_addDataBarRuleEx(XConditionalFormatting* self,
    const XColor* color, XConditionalFormatting_ValueObjectType type1, const XString* val1,
    XConditionalFormatting_ValueObjectType type2, const XString* val2,
    bool showData, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_DataBar;
    rule.m_color1 = color ? *color : XColor_create();
    rule.m_valType1 = type1;
    rule.m_valType2 = type2;
    rule.m_showData = showData;
    rule.m_stopIfTrue = stopIfTrue;
    if (val1) { rule.m_formula1 = XString_create(); XString_append(rule.m_formula1, val1); }
    if (val2) { rule.m_formula2 = XString_create(); XString_append(rule.m_formula2, val2); }
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_add2ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* maxColor, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_ColorScale2;
    rule.m_color1 = minColor ? *minColor : XColor_create();
    rule.m_color2 = maxColor ? *maxColor : XColor_create();
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Max;
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
}

bool XConditionalFormatting_add3ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* midColor, const XColor* maxColor, bool stopIfTrue)
{
    if (!self) return false;
    XConditionalFormatting_Rule rule;
    memset(&rule, 0, sizeof(rule));
    rule.m_ruleType = XCF_Rule_ColorScale3;
    rule.m_color1 = minColor ? *minColor : XColor_create();
    rule.m_color2 = midColor ? *midColor : XColor_create();
    rule.m_color3 = maxColor ? *maxColor : XColor_create();
    rule.m_stopIfTrue = stopIfTrue;
    rule.m_valType1 = XCF_VOT_Min;
    rule.m_valType2 = XCF_VOT_Max;
    XVector_push_back_2(self->m_rules, &rule, 1);
    return true;
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
    XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addCellRc(XConditionalFormatting* self, int row, int col)
{
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCell(&range, row, col);
    XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addRange(XConditionalFormatting* self, int firstRow, int firstCol, int lastRow, int lastCol)
{
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setFullRange(&range, firstRow, firstCol, lastRow, lastCol);
    XVector_push_back_2(self->m_ranges, &range, 1);
}

void XConditionalFormatting_addRangeEx(XConditionalFormatting* self, const XCellRange* range)
{
    if (!self || !range || !self->m_ranges) return;
    XVector_push_back_2(self->m_ranges, (void*)range, 1);
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
