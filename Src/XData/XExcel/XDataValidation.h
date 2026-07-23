/******************************************************************************
 * @file       XDataValidation.h
 * @brief      XDataValidation 数据验证类
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDATAVALIDATION_H
#define XDATAVALIDATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XVector.h"
#include "XCellRange.h"
#include "XCellReference.h"
typedef enum XDataValidation_ValidationType {
    XDataValidation_None, XDataValidation_Whole, XDataValidation_Decimal,
    XDataValidation_List, XDataValidation_Date, XDataValidation_Time,
    XDataValidation_TextLength, XDataValidation_Custom
} XDataValidation_ValidationType;
typedef enum XDataValidation_ValidationOperator {
    XDataValidation_Between, XDataValidation_NotBetween, XDataValidation_Equal,
    XDataValidation_NotEqual, XDataValidation_LessThan, XDataValidation_LessThanOrEqual,
    XDataValidation_GreaterThan, XDataValidation_GreaterThanOrEqual
} XDataValidation_ValidationOperator;
typedef enum XDataValidation_ErrorStyle {
    XDataValidation_Stop, XDataValidation_Warning, XDataValidation_Information
} XDataValidation_ErrorStyle;
typedef struct XDataValidation {
    XDataValidation_ValidationType m_validationType;
    XDataValidation_ValidationOperator m_validationOperator;
    XDataValidation_ErrorStyle m_errorStyle;
    XString* m_formula1; XString* m_formula2;
    XString* m_errorMessage; XString* m_errorMessageTitle;
    XString* m_promptMessage; XString* m_promptMessageTitle;
    bool m_allowBlank; bool m_promptMessageVisible; bool m_errorMessageVisible;
    XVector* m_ranges;
} XDataValidation;
XDataValidation* XDataValidation_create(void);
XDataValidation* XDataValidation_create_ex(XDataValidation_ValidationType type,
    XDataValidation_ValidationOperator op, const char* formula1, const char* formula2, bool allowBlank);
XDataValidation* XDataValidation_copy(const XDataValidation* other);
void XDataValidation_delete(XDataValidation* self);
XDataValidation_ValidationType XDataValidation_validationType(const XDataValidation* self);
void XDataValidation_setValidationType(XDataValidation* self, XDataValidation_ValidationType type);
XDataValidation_ValidationOperator XDataValidation_validationOperator(const XDataValidation* self);
void XDataValidation_setValidationOperator(XDataValidation* self, XDataValidation_ValidationOperator op);
XDataValidation_ErrorStyle XDataValidation_errorStyle(const XDataValidation* self);
void XDataValidation_setErrorStyle(XDataValidation* self, XDataValidation_ErrorStyle es);
const char* XDataValidation_formula1(const XDataValidation* self);
void XDataValidation_setFormula1(XDataValidation* self, const char* formula);
const char* XDataValidation_formula2(const XDataValidation* self);
void XDataValidation_setFormula2(XDataValidation* self, const char* formula);
bool XDataValidation_allowBlank(const XDataValidation* self);
void XDataValidation_setAllowBlank(XDataValidation* self, bool enable);
const char* XDataValidation_errorMessage(const XDataValidation* self);
const char* XDataValidation_errorMessageTitle(const XDataValidation* self);
const char* XDataValidation_promptMessage(const XDataValidation* self);
const char* XDataValidation_promptMessageTitle(const XDataValidation* self);
bool XDataValidation_isPromptMessageVisible(const XDataValidation* self);
bool XDataValidation_isErrorMessageVisible(const XDataValidation* self);
void XDataValidation_setErrorMessage(XDataValidation* self, const char* error, const char* title);
void XDataValidation_setPromptMessage(XDataValidation* self, const char* prompt, const char* title);
void XDataValidation_setPromptMessageVisible(XDataValidation* self, bool visible);
void XDataValidation_setErrorMessageVisible(XDataValidation* self, bool visible);
void XDataValidation_addCell(XDataValidation* self, const XCellReference* cell);
void XDataValidation_addCellRc(XDataValidation* self, int row, int col);
void XDataValidation_addRange(XDataValidation* self, int firstRow, int firstCol, int lastRow, int lastCol);
void XDataValidation_addRangeEx(XDataValidation* self, const XCellRange* range);
int XDataValidation_rangesCount(const XDataValidation* self);
XCellRange* XDataValidation_ranges(const XDataValidation* self, int* count);
#ifdef __cplusplus
}
#endif
#endif
