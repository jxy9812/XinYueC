/******************************************************************************
 * @file       XDataValidation.c
 * @brief      XDataValidation 数据验证类实现（对标 QXlsx::DataValidation）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDataValidation.h"
#include "XMemory.h"
#include "XCellRange.h"
#include "XCellReference.h"
#include <stdlib.h>

#include <string.h>

#include <stdio.h>

static bool validation_type_is_valid(XDataValidation_ValidationType type)
{
    return type >= XDataValidation_None && type <= XDataValidation_Custom;
}

static bool validation_operator_is_valid(XDataValidation_ValidationOperator op)
{
    return op >= XDataValidation_Between && op <= XDataValidation_GreaterThanOrEqual;
}

static bool error_style_is_valid(XDataValidation_ErrorStyle style)
{
    return style >= XDataValidation_Stop && style <= XDataValidation_Information;
}

static bool copy_optional_string(XString** target, const XString* source)
{
    if (!target) return false;
    if (!source) {
        *target = NULL;
        return true;
    }
    *target = XString_create_copy(source);
    return *target != NULL;
}

static void assign_optional_string(XString** target, const XString* value)
{
    if (!target) return;
    if (!value) {
        if (*target) XString_delete_base(*target);
        *target = NULL;
        return;
    }

    XString* copy = XString_create_copy(value);
    if (!copy) return;
    if (*target) XString_delete_base(*target);
    *target = copy;
}


/* ========== 创建与初始化 ========== */

/// @brief 创建默认数据验证对象
XDataValidation* XDataValidation_create(void)
{
    XDataValidation* self = (XDataValidation*)XMalloc_System(sizeof(XDataValidation));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDataValidation));
    self->m_validationType = XDataValidation_None;
    self->m_validationOperator = XDataValidation_Between;
    self->m_errorStyle = XDataValidation_Stop;
    self->m_allowBlank = false;
    self->m_promptMessageVisible = true;
    self->m_errorMessageVisible = true;
    self->m_ranges = XVector_Create(XCellRange);
    if (!self->m_ranges) {
        XFree_System(self);
        return NULL;
    }
    return self;
}

/// @brief 创建带参数的数据验证对象
XDataValidation* XDataValidation_create_ex(XDataValidation_ValidationType type,
    XDataValidation_ValidationOperator op, const XString* formula1, const XString* formula2, bool allowBlank)
{
    if (!validation_type_is_valid(type) || !validation_operator_is_valid(op)) return NULL;
    XDataValidation* self = XDataValidation_create();
    if (!self) return NULL;
    self->m_validationType = type;
    self->m_validationOperator = op;
    self->m_allowBlank = allowBlank;
    if (!copy_optional_string(&self->m_formula1, formula1) ||
        !copy_optional_string(&self->m_formula2, formula2)) {
        XDataValidation_delete(self);
        return NULL;
    }
    return self;
}

/// @brief 拷贝构造
XDataValidation* XDataValidation_copy(const XDataValidation* other)
{
    if (!other) return NULL;
    XDataValidation* self = XDataValidation_create();
    if (!self) return NULL;
    self->m_validationType = other->m_validationType;
    self->m_validationOperator = other->m_validationOperator;
    self->m_errorStyle = other->m_errorStyle;
    self->m_allowBlank = other->m_allowBlank;
    self->m_promptMessageVisible = other->m_promptMessageVisible;
    self->m_errorMessageVisible = other->m_errorMessageVisible;
    if (!copy_optional_string(&self->m_formula1, other->m_formula1) ||
        !copy_optional_string(&self->m_formula2, other->m_formula2) ||
        !copy_optional_string(&self->m_errorMessage, other->m_errorMessage) ||
        !copy_optional_string(&self->m_errorMessageTitle, other->m_errorMessageTitle) ||
        !copy_optional_string(&self->m_promptMessage, other->m_promptMessage) ||
        !copy_optional_string(&self->m_promptMessageTitle, other->m_promptMessageTitle)) {
        XDataValidation_delete(self);
        return NULL;
    }
    if (other->m_ranges) {
        XVector_clear_base(self->m_ranges);
        for (size_t i = 0; i < XVector_size_base(other->m_ranges); ++i) {
            XCellRange* r = (XCellRange*)XVector_at_base(other->m_ranges, i);
            if (r && !XVector_push_back_2(self->m_ranges, r, 1)) {
                XDataValidation_delete(self);
                return NULL;
            }
        }
    }
    return self;
}

/// @brief 释放数据验证对象
void XDataValidation_delete(XDataValidation* self)
{
    if (!self) return;
    if (self->m_formula1) { XString_delete_base(self->m_formula1);  }
    if (self->m_formula2) { XString_delete_base(self->m_formula2);  }
    if (self->m_errorMessage) { XString_delete_base(self->m_errorMessage);  }
    if (self->m_errorMessageTitle) { XString_delete_base(self->m_errorMessageTitle); }
    if (self->m_promptMessage) { XString_delete_base(self->m_promptMessage); }
    if (self->m_promptMessageTitle) { XString_delete_base(self->m_promptMessageTitle);  }
    if (self->m_ranges) {
        XVector_delete_base(self->m_ranges);
    }
    XFree_System(self);
}

/* ========== 属性访问方法 ========== */
XDataValidation_ValidationType XDataValidation_validationType(const XDataValidation* self) { return self ? self->m_validationType : XDataValidation_None; }
void XDataValidation_setValidationType(XDataValidation* self, XDataValidation_ValidationType type) { if (self && validation_type_is_valid(type)) self->m_validationType = type; }
XDataValidation_ValidationOperator XDataValidation_validationOperator(const XDataValidation* self) { return self ? self->m_validationOperator : XDataValidation_Between; }
void XDataValidation_setValidationOperator(XDataValidation* self, XDataValidation_ValidationOperator op) { if (self && validation_operator_is_valid(op)) self->m_validationOperator = op; }
XDataValidation_ErrorStyle XDataValidation_errorStyle(const XDataValidation* self) { return self ? self->m_errorStyle : XDataValidation_Stop; }
void XDataValidation_setErrorStyle(XDataValidation* self, XDataValidation_ErrorStyle es) { if (self && error_style_is_valid(es)) self->m_errorStyle = es; }
const XString* XDataValidation_formula1(const XDataValidation* self) { return (self && self->m_formula1) ? self->m_formula1 : NULL; }
void XDataValidation_setFormula1(XDataValidation* self, const XString* formula) {
    if (!self) return;
    assign_optional_string(&self->m_formula1, formula);
}
const XString* XDataValidation_formula2(const XDataValidation* self) { return (self && self->m_formula2) ? self->m_formula2 : NULL; }
void XDataValidation_setFormula2(XDataValidation* self, const XString* formula) {
    if (!self) return;
    assign_optional_string(&self->m_formula2, formula);
}
bool XDataValidation_allowBlank(const XDataValidation* self) { return self ? self->m_allowBlank : false; }
void XDataValidation_setAllowBlank(XDataValidation* self, bool enable) { if (self) self->m_allowBlank = enable; }
const XString* XDataValidation_errorMessage(const XDataValidation* self) { return (self && self->m_errorMessage) ? self->m_errorMessage : NULL; }
const XString* XDataValidation_errorMessageTitle(const XDataValidation* self) { return (self && self->m_errorMessageTitle) ? self->m_errorMessageTitle : NULL; }
const XString* XDataValidation_promptMessage(const XDataValidation* self) { return (self && self->m_promptMessage) ? self->m_promptMessage : NULL; }
const XString* XDataValidation_promptMessageTitle(const XDataValidation* self) { return (self && self->m_promptMessageTitle) ? self->m_promptMessageTitle : NULL; }
bool XDataValidation_isPromptMessageVisible(const XDataValidation* self) { return self ? self->m_promptMessageVisible : false; }
bool XDataValidation_isErrorMessageVisible(const XDataValidation* self) { return self ? self->m_errorMessageVisible : false; }
void XDataValidation_setErrorMessage(XDataValidation* self, const XString* error, const XString* title) {
    if (!self) return;
    assign_optional_string(&self->m_errorMessage, error);
    assign_optional_string(&self->m_errorMessageTitle, title);
}
void XDataValidation_setPromptMessage(XDataValidation* self, const XString* prompt, const XString* title) {
    if (!self) return;
    assign_optional_string(&self->m_promptMessage, prompt);
    assign_optional_string(&self->m_promptMessageTitle, title);
}
void XDataValidation_setPromptMessageVisible(XDataValidation* self, bool visible) { if (self) self->m_promptMessageVisible = visible; }
void XDataValidation_setErrorMessageVisible(XDataValidation* self, bool visible) { if (self) self->m_errorMessageVisible = visible; }

/* ========== 范围管理 ========== */
void XDataValidation_addCell(XDataValidation* self, const XCellReference* cell) {
    if (!self || !cell || !self->m_ranges || !XCellReference_isValid(cell)) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCellReference(&range, cell);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addCellRc(XDataValidation* self, int row, int col) {
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCell(&range, row, col);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addRange(XDataValidation* self, int firstRow, int firstCol, int lastRow, int lastCol) {
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_init_ex(&range, firstRow, firstCol, lastRow, lastCol);
    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addRangeEx(XDataValidation* self, const XCellRange* range) {
    if (!self || !range || !self->m_ranges || !XCellRange_isValid(range)) return;
    XVector_push_back_2(self->m_ranges, (void*)range, 1);
}
int XDataValidation_rangesCount(const XDataValidation* self) {
    return (self && self->m_ranges) ? (int)XVector_size_base(self->m_ranges) : 0;
}
XCellRange* XDataValidation_ranges(const XDataValidation* self, int* count) {
    if (count) *count = XDataValidation_rangesCount(self);
    return (self && self->m_ranges) ? (XCellRange*)XVector_data(self->m_ranges) : NULL;
}
