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
/**
 * @brief  创建默认数据验证对象
 * @return 新创建的数据验证对象指针
 */
XDataValidation* XDataValidation_create(void);

/**
 * @brief  创建带参数的数据验证对象
 * @param  type       验证类型
 * @param  op         验证运算符
 * @param  formula1   公式1（可为 NULL）
 * @param  formula2   公式2（可为 NULL）
 * @param  allowBlank 是否允许空值
 * @return 新创建的数据验证对象指针
 */
XDataValidation* XDataValidation_create_ex(XDataValidation_ValidationType type,
    XDataValidation_ValidationOperator op, const XString* formula1, const XString* formula2, bool allowBlank);

/**
 * @brief  深拷贝数据验证对象
 * @param  other 源数据验证对象
 * @return 新创建的数据验证对象副本
 */
XDataValidation* XDataValidation_copy(const XDataValidation* other);

/**
 * @brief  销毁数据验证对象并释放资源
 * @param  self 数据验证对象指针
 */
void XDataValidation_delete(XDataValidation* self);

/**
 * @brief  获取验证类型
 * @param  self 数据验证对象指针
 * @return 当前验证类型
 */
XDataValidation_ValidationType XDataValidation_validationType(const XDataValidation* self);

/**
 * @brief  设置验证类型
 * @param  self 数据验证对象指针
 * @param  type 验证类型
 */
void XDataValidation_setValidationType(XDataValidation* self, XDataValidation_ValidationType type);

/**
 * @brief  获取验证运算符
 * @param  self 数据验证对象指针
 * @return 当前验证运算符
 */
XDataValidation_ValidationOperator XDataValidation_validationOperator(const XDataValidation* self);

/**
 * @brief  设置验证运算符
 * @param  self 数据验证对象指针
 * @param  op   验证运算符
 */
void XDataValidation_setValidationOperator(XDataValidation* self, XDataValidation_ValidationOperator op);

/**
 * @brief  获取错误样式
 * @param  self 数据验证对象指针
 * @return 当前错误样式
 */
XDataValidation_ErrorStyle XDataValidation_errorStyle(const XDataValidation* self);

/**
 * @brief  设置错误样式
 * @param  self 数据验证对象指针
 * @param  es   错误样式
 */
void XDataValidation_setErrorStyle(XDataValidation* self, XDataValidation_ErrorStyle es);

/**
 * @brief  获取公式1
 * @param  self 数据验证对象指针
 * @return 公式1字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_formula1(const XDataValidation* self);

/**
 * @brief  设置公式1
 * @param  self    数据验证对象指针
 * @param  formula 公式字符串（内容被拷贝）
 */
void XDataValidation_setFormula1(XDataValidation* self, const XString* formula);

/**
 * @brief  获取公式2
 * @param  self 数据验证对象指针
 * @return 公式2字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_formula2(const XDataValidation* self);

/**
 * @brief  设置公式2
 * @param  self    数据验证对象指针
 * @param  formula 公式字符串（内容被拷贝）
 */
void XDataValidation_setFormula2(XDataValidation* self, const XString* formula);

/**
 * @brief  获取是否允许空值
 * @param  self 数据验证对象指针
 * @return true 允许空值，false 不允许
 */
bool XDataValidation_allowBlank(const XDataValidation* self);

/**
 * @brief  设置是否允许空值
 * @param  self   数据验证对象指针
 * @param  enable true 允许空值，false 不允许
 */
void XDataValidation_setAllowBlank(XDataValidation* self, bool enable);

/**
 * @brief  获取错误消息内容
 * @param  self 数据验证对象指针
 * @return 错误消息字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_errorMessage(const XDataValidation* self);

/**
 * @brief  获取错误消息标题
 * @param  self 数据验证对象指针
 * @return 错误消息标题字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_errorMessageTitle(const XDataValidation* self);

/**
 * @brief  获取提示消息内容
 * @param  self 数据验证对象指针
 * @return 提示消息字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_promptMessage(const XDataValidation* self);

/**
 * @brief  获取提示消息标题
 * @param  self 数据验证对象指针
 * @return 提示消息标题字符串指针（内部所有，勿释放）
 */
const XString* XDataValidation_promptMessageTitle(const XDataValidation* self);

/**
 * @brief  获取提示消息是否可见
 * @param  self 数据验证对象指针
 * @return true 可见，false 不可见
 */
bool XDataValidation_isPromptMessageVisible(const XDataValidation* self);

/**
 * @brief  获取错误消息是否可见
 * @param  self 数据验证对象指针
 * @return true 可见，false 不可见
 */
bool XDataValidation_isErrorMessageVisible(const XDataValidation* self);

/**
 * @brief  设置错误消息及标题
 * @param  self  数据验证对象指针
 * @param  error 错误消息内容（内容被拷贝）
 * @param  title 错误消息标题（内容被拷贝）
 */
void XDataValidation_setErrorMessage(XDataValidation* self, const XString* error, const XString* title);

/**
 * @brief  设置提示消息及标题
 * @param  self   数据验证对象指针
 * @param  prompt 提示消息内容（内容被拷贝）
 * @param  title  提示消息标题（内容被拷贝）
 */
void XDataValidation_setPromptMessage(XDataValidation* self, const XString* prompt, const XString* title);

/**
 * @brief  设置提示消息是否可见
 * @param  self    数据验证对象指针
 * @param  visible true 可见，false 不可见
 */
void XDataValidation_setPromptMessageVisible(XDataValidation* self, bool visible);

/**
 * @brief  设置错误消息是否可见
 * @param  self    数据验证对象指针
 * @param  visible true 可见，false 不可见
 */
void XDataValidation_setErrorMessageVisible(XDataValidation* self, bool visible);

/**
 * @brief  添加单个单元格到验证范围
 * @param  self 数据验证对象指针
 * @param  cell 单元格引用
 */
void XDataValidation_addCell(XDataValidation* self, const XCellReference* cell);

/**
 * @brief  通过行列号添加单个单元格到验证范围
 * @param  self 数据验证对象指针
 * @param  row  行号（从1开始）
 * @param  col  列号（从1开始）
 */
void XDataValidation_addCellRc(XDataValidation* self, int row, int col);

/**
 * @brief  添加矩形区域到验证范围
 * @param  self     数据验证对象指针
 * @param  firstRow 起始行号（从1开始）
 * @param  firstCol 起始列号（从1开始）
 * @param  lastRow  结束行号（从1开始）
 * @param  lastCol  结束列号（从1开始）
 */
void XDataValidation_addRange(XDataValidation* self, int firstRow, int firstCol, int lastRow, int lastCol);

/**
 * @brief  添加单元格范围对象到验证范围
 * @param  self  数据验证对象指针
 * @param  range 单元格范围对象
 */
void XDataValidation_addRangeEx(XDataValidation* self, const XCellRange* range);

/**
 * @brief  获取验证范围数量
 * @param  self 数据验证对象指针
 * @return 范围数量
 */
int XDataValidation_rangesCount(const XDataValidation* self);

/**
 * @brief  获取所有验证范围
 * @param  self  数据验证对象指针
 * @param  count [out] 输出范围数量
 * @return 范围数组指针（内部所有，勿释放）
 */
XCellRange* XDataValidation_ranges(const XDataValidation* self, int* count);
#ifdef __cplusplus
}
#endif
#endif
