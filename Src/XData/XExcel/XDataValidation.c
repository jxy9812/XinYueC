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
    return self;
}

/// @brief 创建带参数的数据验证对象
XDataValidation* XDataValidation_create_ex(XDataValidation_ValidationType type,
    XDataValidation_ValidationOperator op, const char* formula1, const char* formula2, bool allowBlank)
{
    XDataValidation* self = XDataValidation_create();
    if (!self) return NULL;
    self->m_validationType = type;
    self->m_validationOperator = op;
    self->m_allowBlank = allowBlank;
    if (formula1) { self->m_formula1 = XString_create(); XString_append_utf8(self->m_formula1, formula1); }
    if (formula2) { self->m_formula2 = XString_create(); XString_append_utf8(self->m_formula2, formula2); }
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
    if (other->m_formula1) { self->m_formula1 = XString_create(); XString_copy_base(self->m_formula1, other->m_formula1); }
    if (other->m_formula2) { self->m_formula2 = XString_create(); XString_copy_base(self->m_formula2, other->m_formula2); }
    if (other->m_errorMessage) { self->m_errorMessage = XString_create(); XString_copy_base(self->m_errorMessage, other->m_errorMessage); }
    if (other->m_errorMessageTitle) { self->m_errorMessageTitle = XString_create(); XString_copy_base(self->m_errorMessageTitle, other->m_errorMessageTitle); }
    if (other->m_promptMessage) { self->m_promptMessage = XString_create(); XString_copy_base(self->m_promptMessage, other->m_promptMessage); }
    if (other->m_promptMessageTitle) { self->m_promptMessageTitle = XString_create(); XString_copy_base(self->m_promptMessageTitle, other->m_promptMessageTitle); }
    if (other->m_ranges) {
        XVector_clear_base(self->m_ranges);
        for (size_t i = 0; i < XVector_size_base(other->m_ranges); ++i) {
            XCellRange* r = (XCellRange*)XVector_at_base(other->m_ranges, i);
            XCellRange_copy(r, r);
            XVector_push_back_2(self->m_ranges, r, 1);
        }
    }
    return self;
}

/// @brief 释放数据验证对象
void XDataValidation_delete(XDataValidation* self)
{
    if (!self) return;
    if (self->m_formula1) { XString_deinit_base(self->m_formula1); XFree_System(self->m_formula1); }
    if (self->m_formula2) { XString_deinit_base(self->m_formula2); XFree_System(self->m_formula2); }
    if (self->m_errorMessage) { XString_deinit_base(self->m_errorMessage); XFree_System(self->m_errorMessage); }
    if (self->m_errorMessageTitle) { XString_deinit_base(self->m_errorMessageTitle); XFree_System(self->m_errorMessageTitle); }
    if (self->m_promptMessage) { XString_deinit_base(self->m_promptMessage); XFree_System(self->m_promptMessage); }
    if (self->m_promptMessageTitle) { XString_deinit_base(self->m_promptMessageTitle); XFree_System(self->m_promptMessageTitle); }
    if (self->m_ranges) { XFree_System(self->m_ranges); }
    XFree_System(self);
}

/* ========== 属性访问方法 ========== */
XDataValidation_ValidationType XDataValidation_validationType(const XDataValidation* self) { return self ? self->m_validationType : XDataValidation_None; }
void XDataValidation_setValidationType(XDataValidation* self, XDataValidation_ValidationType type) { if (self) self->m_validationType = type; }
XDataValidation_ValidationOperator XDataValidation_validationOperator(const XDataValidation* self) { return self ? self->m_validationOperator : XDataValidation_Between; }
void XDataValidation_setValidationOperator(XDataValidation* self, XDataValidation_ValidationOperator op) { if (self) self->m_validationOperator = op; }
XDataValidation_ErrorStyle XDataValidation_errorStyle(const XDataValidation* self) { return self ? self->m_errorStyle : XDataValidation_Stop; }
void XDataValidation_setErrorStyle(XDataValidation* self, XDataValidation_ErrorStyle es) { if (self) self->m_errorStyle = es; }
const char* XDataValidation_formula1(const XDataValidation* self) { return (self && self->m_formula1) ? XString_toUtf8(self->m_formula1) : ""; }
void XDataValidation_setFormula1(XDataValidation* self, const char* formula) {
    if (!self) return;
    if (!self->m_formula1) self->m_formula1 = XString_create();
    if (self->m_formula1) { XString_clear_base(self->m_formula1); XString_append_utf8(self->m_formula1, formula); }
}
const char* XDataValidation_formula2(const XDataValidation* self) { return (self && self->m_formula2) ? XString_toUtf8(self->m_formula2) : ""; }
void XDataValidation_setFormula2(XDataValidation* self, const char* formula) {
    if (!self) return;
    if (!self->m_formula2) self->m_formula2 = XString_create();
    if (self->m_formula2) { XString_clear_base(self->m_formula2); XString_append_utf8(self->m_formula2, formula); }
}
bool XDataValidation_allowBlank(const XDataValidation* self) { return self ? self->m_allowBlank : false; }
void XDataValidation_setAllowBlank(XDataValidation* self, bool enable) { if (self) self->m_allowBlank = enable; }
const char* XDataValidation_errorMessage(const XDataValidation* self) { return (self && self->m_errorMessage) ? XString_toUtf8(self->m_errorMessage) : ""; }
const char* XDataValidation_errorMessageTitle(const XDataValidation* self) { return (self && self->m_errorMessageTitle) ? XString_toUtf8(self->m_errorMessageTitle) : ""; }
const char* XDataValidation_promptMessage(const XDataValidation* self) { return (self && self->m_promptMessage) ? XString_toUtf8(self->m_promptMessage) : ""; }
const char* XDataValidation_promptMessageTitle(const XDataValidation* self) { return (self && self->m_promptMessageTitle) ? XString_toUtf8(self->m_promptMessageTitle) : ""; }
bool XDataValidation_isPromptMessageVisible(const XDataValidation* self) { return self ? self->m_promptMessageVisible : false; }
bool XDataValidation_isErrorMessageVisible(const XDataValidation* self) { return self ? self->m_errorMessageVisible : false; }
void XDataValidation_setErrorMessage(XDataValidation* self, const char* error, const char* title) {
    if (!self) return;
    if (error) { if (!self->m_errorMessage) self->m_errorMessage = XString_create(); if (self->m_errorMessage) { XString_clear_base(self->m_errorMessage); XString_append_utf8(self->m_errorMessage, error); } }
    if (title) { if (!self->m_errorMessageTitle) self->m_errorMessageTitle = XString_create(); if (self->m_errorMessageTitle) { XString_clear_base(self->m_errorMessageTitle); XString_append_utf8(self->m_errorMessageTitle, title); } }
}
void XDataValidation_setPromptMessage(XDataValidation* self, const char* prompt, const char* title) {
    if (!self) return;
    if (prompt) { if (!self->m_promptMessage) self->m_promptMessage = XString_create(); if (self->m_promptMessage) { XString_clear_base(self->m_promptMessage); XString_append_utf8(self->m_promptMessage, prompt); } }
    if (title) { if (!self->m_promptMessageTitle) self->m_promptMessageTitle = XString_create(); if (self->m_promptMessageTitle) { XString_clear_base(self->m_promptMessageTitle); XString_append_utf8(self->m_promptMessageTitle, title); } }
}
void XDataValidation_setPromptMessageVisible(XDataValidation* self, bool visible) { if (self) self->m_promptMessageVisible = visible; }
void XDataValidation_setErrorMessageVisible(XDataValidation* self, bool visible) { if (self) self->m_errorMessageVisible = visible; }

/* ========== 范围管理 ========== */
void XDataValidation_addCell(XDataValidation* self, const XCellReference* cell) {
    if (!self || !cell || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCellReference(&range, cell);
    XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addCellRc(XDataValidation* self, int row, int col) {
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_setCell(&range, row, col);
    XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addRange(XDataValidation* self, int firstRow, int firstCol, int lastRow, int lastCol) {
    if (!self || !self->m_ranges) return;
    XCellRange range;
    XCellRange_init(&range);
    XCellRange_init_ex(&range, firstRow, firstCol, lastRow, lastCol);
    XVector_push_back_2(self->m_ranges, &range, 1);
}
void XDataValidation_addRangeEx(XDataValidation* self, const XCellRange* range) {
    if (!self || !range || !self->m_ranges) return;
    XVector_push_back_2(self->m_ranges, (void*)range, 1);
}
int XDataValidation_rangesCount(const XDataValidation* self) {
    return (self && self->m_ranges) ? (int)XVector_size_base(self->m_ranges) : 0;
}
XCellRange* XDataValidation_ranges(const XDataValidation* self, int* count) {
    if (count) *count = XDataValidation_rangesCount(self);
    return (self && self->m_ranges) ? (XCellRange*)XVector_data(self->m_ranges) : NULL;
}
