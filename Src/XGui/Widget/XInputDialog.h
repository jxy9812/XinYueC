/******************************************************************************
 * @file       XInputDialog.h
 * @brief      XInputDialog 输入对话框控件（对标 Qt 6.8 QInputDialog : QDialog）。
 * @details    继承 XDialog，提供文本/整数/浮点/下拉选择四种输入模式：
 *             - 静态便捷函数 getText/getMultiLineText/getInt/getDouble/
 *               getItem（有 XCoreApplication 实例时构造真实对话框并
 *               exec 阻塞执行：内嵌输入控件 + 确定/取消按钮行，应用
 *               模态、Escape→reject；无 GUI 对话框环境返回默认值，
 *               *ok 置 false）；
 *             - 实例属性：inputMode/labelText/textValue/intValue/
 *               doubleValue/comboBoxItems/comboBoxEditable/
 *               okButtonText/cancelButtonText、InputDialogOption 选项位；
 *             - 信号：textValueChanged(int)/intValueChanged(int)/
 *               doubleValueChanged(double)/comboBoxTextChanged(text)。
 *             InputMode 数值对齐 Qt：TextInput=0/IntInput=1/DoubleInput=2；
 *             ComboBoxInput=3 为兼容扩展（Qt 5 同值，Qt 6 已移除该项，
 *             本实现保留以承载下拉输入 API）。
 * @note       模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）。
 * @note       无 GUI 对话框环境：静态函数创建临时实例、应用存储 setter，
 *             但不执行模态对话框循环，一律返回默认值（文本类返回空串、
 *             数值类返回入参 value），*ok 置 false；信号可由应用手动触发
 *             供测试。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XINPUTDIALOG_H
#define XINPUTDIALOG_H


#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XStringList.h"
#if XDIALOG_ON
#include "XDialog.h"
#endif /* XDIALOG_ON */

#if XWIDGET_ON && XDIALOG_ON

/** @brief 输入对话框选项位（对标 QInputDialog::InputDialogOption，数值一致）。 */
typedef enum XInputDialogOption
{
    XInputDialog_NoButtons                    = 0x00000001, /**< 不显示按钮。 */
    XInputDialog_UseListViewForComboBoxItems  = 0x00000002, /**< 下拉用列表视图。 */
    XInputDialog_UsePlainTextEditForTextInput = 0x00000004  /**< 文本输入用纯文本编辑。 */
} XInputDialogOption;
typedef uint32_t XInputDialogOptions;

/** @brief 输入模式（对标 QInputDialog::InputMode；ComboBoxInput 为兼容扩展）。 */
typedef enum XInputDialogInputMode
{
    XInputDialog_TextInput = 0,    /**< 单行文本输入。 */
    XInputDialog_IntInput = 1,     /**< 整数输入。 */
    XInputDialog_DoubleInput = 2,  /**< 浮点输入。 */
    XInputDialog_ComboBoxInput = 3 /**< 下拉选择输入（兼容扩展）。 */
} XInputDialogInputMode;

/** @brief 文本回显模式（对标 QLineEdit::EchoMode，数值一致）。 */
typedef enum XInputDialogEchoMode
{
    XInputDialogEchoMode_Normal = 0,           /**< 正常回显。 */
    XInputDialogEchoMode_NoEcho = 1,           /**< 不回显。 */
    XInputDialogEchoMode_Password = 2,         /**< 密码回显。 */
    XInputDialogEchoMode_PasswordEchoOnEdit = 3 /**< 编辑时明文、失焦密码。 */
} XInputDialogEchoMode;

XCLASS_DEFINE_BEGING(XInputDialog)
XCLASS_DEFINE_EXTEND_END(XInputDialog, XDialog)

/**
 * @brief      XInputDialog 输入对话框对象；m_base 必须是第一个成员。
 * @details    字符串字段为拥有型 XString*，列表字段为拥有型 XStringList*；
 *             销毁随 XInputDialog_deinit_base 一并释放。
 */
typedef struct XInputDialog
{
    XDialog m_base;               /**< 基类成员；必须是第一个。 */
    XInputDialogInputMode m_inputMode; /**< 输入模式；默认 TextInput。 */
    XInputDialogOptions m_options;     /**< 选项位集；默认 0。 */
    XString* m_labelText;         /**< 标签文本（拥有）。 */
    XString* m_textValue;         /**< 文本输入值（拥有）。 */
    XString* m_comboBoxText;      /**< 下拉当前文本（拥有；comboBoxTextChanged 载荷）。 */
    XStringList* m_comboBoxItems; /**< 下拉项列表（拥有）。 */
    bool m_comboBoxEditable;      /**< 下拉可编辑；默认 false。 */
    XInputDialogEchoMode m_echoMode; /**< 文本回显模式；默认 Normal。 */
    int m_intValue;               /**< 整数值。 */
    double m_doubleValue;         /**< 浮点值。 */
    XString* m_okButtonText;      /**< OK 按钮文本（拥有）。 */
    XString* m_cancelButtonText;  /**< 取消按钮文本（拥有）。 */
    int m_intMinimum;             /**< 整数下限（默认 -2147483648）。 */
    int m_intMaximum;             /**< 整数上限（默认 2147483647）。 */
    int m_intStep;                /**< 整数步进（默认 1）。 */
    double m_doubleMinimum;       /**< 浮点下限（默认 -1e308）。 */
    double m_doubleMaximum;       /**< 浮点上限（默认 1e308）。 */
    double m_doubleStep;          /**< 浮点步进（默认 1）。 */
    int m_doubleDecimals;         /**< 浮点小数位（默认 2）。 */
} XInputDialog;

/**
 * @brief      XInputDialog 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XInputDialog_class_init(void);

/**
 * @brief      初始化 XInputDialog（对标 QInputDialog 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XInputDialog_init(XInputDialog* self, XWidget* parent, XWidgetFlags flags);
#define XInputDialog_create(parent, flags) XInputDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建 XInputDialog。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；失败返回 NULL。
 */
XInputDialog* XInputDialog_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags);
#define XInputDialog_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XInputDialog_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 实例属性（对标 QInputDialog） ==================== */

/**
 * @brief      设置输入模式（对标 QInputDialog::setInputMode）。
 * @param      self 目标对话框。
 * @param      mode 输入模式（XInputDialogInputMode）。
 * @return     无返回值。
 */
void XInputDialog_setInputMode(XInputDialog* self, XInputDialogInputMode mode);
/**
 * @brief      获取输入模式（对标 QInputDialog::inputMode）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前输入模式；无效时返回 XInputDialog_TextInput。
 */
XInputDialogInputMode XInputDialog_inputMode(const XInputDialog* self);
/**
 * @brief      设置标签文本（对标 QInputDialog::setLabelText）。
 * @param      self 目标对话框。
 * @param      text 标签文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XInputDialog_setLabelText(XInputDialog* self, const XString* text);
/**
 * @brief      获取标签文本副本（对标 QInputDialog::labelText）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XInputDialog_labelText(const XInputDialog* self);
/**
 * @brief      设置文本值（对标 QInputDialog::setTextValue）。
 * @details    文本实际变化时发射 textValueChanged(XString*)。
 * @param      self 目标对话框。
 * @param      text 新文本；可为 NULL 视为空串。
 * @return     无返回值。
 */
void XInputDialog_setTextValue(XInputDialog* self, const XString* text);
/**
 * @brief      获取文本值副本（对标 QInputDialog::textValue）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XInputDialog_textValue(const XInputDialog* self);
/**
 * @brief      设置整数值（对标 QInputDialog::setIntValue）。
 * @details    数值实际变化时发射 intValueChanged(int)。
 * @param      self 目标对话框。
 * @param      value 新整数值。
 * @return     无返回值。
 */
void XInputDialog_setIntValue(XInputDialog* self, int value);
/**
 * @brief      获取整数值（对标 QInputDialog::intValue）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前整数值；无效返回 0。
 */
int XInputDialog_intValue(const XInputDialog* self);
/**
 * @brief      设置浮点值（对标 QInputDialog::setDoubleValue）。
 * @details    数值实际变化时发射 doubleValueChanged(double)。
 * @param      self 目标对话框。
 * @param      value 新浮点值。
 * @return     无返回值。
 */
void XInputDialog_setDoubleValue(XInputDialog* self, double value);
/**
 * @brief      获取浮点值（对标 QInputDialog::doubleValue）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前浮点值；无效返回 0.0。
 */
double XInputDialog_doubleValue(const XInputDialog* self);
/**
 * @brief      设置下拉项列表（对标 QInputDialog::setComboBoxItems）。
 * @param      self 目标对话框。
 * @param      items 下拉项列表（深拷贝）；可为 NULL 清空。
 * @return     无返回值。
 */
void XInputDialog_setComboBoxItems(XInputDialog* self, const XStringList* items);
/**
 * @brief      获取下拉项列表副本（对标 QInputDialog::comboBoxItems）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XStringList 深拷贝，调用方拥有，须
 *             XStringList_delete_base；无效时返回空列表。
 */
XStringList* XInputDialog_comboBoxItems(const XInputDialog* self);
/**
 * @brief      设置下拉可编辑（对标 QInputDialog::setComboBoxEditable）。
 * @param      self 目标对话框。
 * @param      editable true=可编辑，false=只读。
 * @return     无返回值。
 */
void XInputDialog_setComboBoxEditable(XInputDialog* self, bool editable);
/**
 * @brief      查询下拉是否可编辑（对标 QInputDialog::isComboBoxEditable）。
 * @param      self 目标对话框；可为 NULL。
 * @return     可编辑返回 true；无效返回 false。
 */
bool XInputDialog_isComboBoxEditable(const XInputDialog* self);
/**
 * @brief      设置 OK 按钮文本（对标 QInputDialog::setOkButtonText）。
 * @param      self 目标对话框。
 * @param      text 按钮文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XInputDialog_setOkButtonText(XInputDialog* self, const XString* text);
/**
 * @brief      获取 OK 按钮文本副本（对标 QInputDialog::okButtonText）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XInputDialog_okButtonText(const XInputDialog* self);
/**
 * @brief      设置取消按钮文本（对标 QInputDialog::setCancelButtonText）。
 * @param      self 目标对话框。
 * @param      text 按钮文本；可为 NULL 清空。
 * @return     无返回值。
 */
void XInputDialog_setCancelButtonText(XInputDialog* self, const XString* text);
/**
 * @brief      获取取消按钮文本副本（对标 QInputDialog::cancelButtonText）。
 * @param      self 目标对话框；可为 NULL。
 * @return     新建的 XString 拷贝，调用方拥有，须 XString_delete_base；
 *             无效时返回空串。
 */
XString* XInputDialog_cancelButtonText(const XInputDialog* self);
/**
 * @brief      设置单个选项位（对标 QInputDialog::setOption）。
 * @param      self 目标对话框。
 * @param      option 选项位（XInputDialogOption）。
 * @param      on true=置位，false=清位。
 * @return     无返回值。
 */
void XInputDialog_setOption(XInputDialog* self, XInputDialogOption option, bool on);
/**
 * @brief      测试选项位（对标 QInputDialog::testOption）。
 * @param      self 目标对话框；可为 NULL。
 * @param      option 选项位（XInputDialogOption）。
 * @return     置位返回 true；无效返回 false。
 */
bool XInputDialog_testOption(const XInputDialog* self, XInputDialogOption option);
/**
 * @brief      设置整个选项位集（对标 QInputDialog::setOptions）。
 * @param      self 目标对话框。
 * @param      options 选项位组合。
 * @return     无返回值。
 */
void XInputDialog_setOptions(XInputDialog* self, XInputDialogOptions options);
/**
 * @brief      获取选项位集（对标 QInputDialog::options）。
 * @param      self 目标对话框；可为 NULL。
 * @return     当前选项位组合；无效返回 0。
 */
XInputDialogOptions XInputDialog_options(const XInputDialog* self);

/* ==================== 静态便捷函数（对标 QInputDialog） ==================== */

/**
 * @brief      弹出单行文本输入（对标 QInputDialog::getText）。
 * @note       无 GUI 对话框环境：返回空串，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      label 提示标签；可为 NULL。
 * @param      echo 回显模式（XInputDialogEchoMode）。
 * @param      text 初始文本；可为 NULL。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getText(XWidget* parent, const XString* title,
                              const XString* label, XInputDialogEchoMode echo,
                              const XString* text, bool* ok);
/**
 * @brief      弹出单行文本输入（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空串，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      label 提示标签（UTF-8）；可为 NULL。
 * @param      echo 回显模式（XInputDialogEchoMode）。
 * @param      text 初始文本（UTF-8）；可为 NULL。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getText_2(XWidget* parent, const char* title,
                                const char* label, XInputDialogEchoMode echo,
                                const char* text, bool* ok);
/**
 * @brief      弹出多行文本输入（对标 QInputDialog::getMultiLineText）。
 * @note       无 GUI 对话框环境：返回空串，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      label 提示标签；可为 NULL。
 * @param      text 初始文本；可为 NULL。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getMultiLineText(XWidget* parent, const XString* title,
                                       const XString* label, const XString* text,
                                       bool* ok);
/**
 * @brief      弹出多行文本输入（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回空串，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      label 提示标签（UTF-8）；可为 NULL。
 * @param      text 初始文本（UTF-8）；可为 NULL。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getMultiLineText_2(XWidget* parent, const char* title,
                                         const char* label, const char* text,
                                         bool* ok);
/**
 * @brief      弹出整数输入（对标 QInputDialog::getInt）。
 * @note       无 GUI 对话框环境：返回 value，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      label 提示标签；可为 NULL。
 * @param      value 初始值。
 * @param      minValue 最小值。
 * @param      maxValue 最大值。
 * @param      step 步进。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     输入的整数；无 GUI 环境返回 value。
 */
int XInputDialog_getInt(XWidget* parent, const XString* title,
                        const XString* label, int value, int minValue,
                        int maxValue, int step, bool* ok);
/**
 * @brief      弹出整数输入（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回 value，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      label 提示标签（UTF-8）；可为 NULL。
 * @param      value 初始值。
 * @param      minValue 最小值。
 * @param      maxValue 最大值。
 * @param      step 步进。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     输入的整数；无 GUI 环境返回 value。
 */
int XInputDialog_getInt_2(XWidget* parent, const char* title, const char* label,
                          int value, int minValue, int maxValue, int step,
                          bool* ok);
/**
 * @brief      弹出浮点输入（对标 QInputDialog::getDouble）。
 * @note       无 GUI 对话框环境：返回 value，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      label 提示标签；可为 NULL。
 * @param      value 初始值。
 * @param      minValue 最小值。
 * @param      maxValue 最大值。
 * @param      decimals 小数位数。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     输入的浮点数；无 GUI 环境返回 value。
 */
double XInputDialog_getDouble(XWidget* parent, const XString* title,
                              const XString* label, double value,
                              double minValue, double maxValue, int decimals,
                              bool* ok);
/**
 * @brief      弹出浮点输入（UTF-8 重载）。
 * @note       无 GUI 对话框环境：返回 value，*ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      label 提示标签（UTF-8）；可为 NULL。
 * @param      value 初始值。
 * @param      minValue 最小值。
 * @param      maxValue 最大值。
 * @param      decimals 小数位数。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     输入的浮点数；无 GUI 环境返回 value。
 */
double XInputDialog_getDouble_2(XWidget* parent, const char* title,
                                const char* label, double value,
                                double minValue, double maxValue, int decimals,
                                bool* ok);
/**
 * @brief      弹出下拉选择（对标 QInputDialog::getItem）。
 * @note       无 GUI 对话框环境：返回 items[current]（越界返回空串），
 *             *ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题；可为 NULL。
 * @param      label 提示标签；可为 NULL。
 * @param      items 下拉项列表。
 * @param      current 当前选中下标（0 起）。
 * @param      editable 是否可编辑。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getItem(XWidget* parent, const XString* title,
                              const XString* label, const XStringList* items,
                              int current, bool editable, bool* ok);
/**
 * @brief      弹出下拉选择（UTF-8 重载；items 以 UTF-8 字符串数组给出）。
 * @note       无 GUI 对话框环境：返回 items[current]（越界返回空串），
 *             *ok 置 false。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      title 对话框标题（UTF-8）；可为 NULL。
 * @param      label 提示标签（UTF-8）；可为 NULL。
 * @param      items UTF-8 字符串数组。
 * @param      count 数组元素个数。
 * @param      current 当前选中下标（0 起）。
 * @param      editable 是否可编辑。
 * @param      ok 输出：是否确认（可为 NULL）。
 * @return     新建的 XString，调用方拥有，须 XString_delete_base。
 */
XString* XInputDialog_getItem_2(XWidget* parent, const char* title,
                                const char* label, const char* const* items,
                                int count, int current, bool editable, bool* ok);

/* ==================== 信号（对标 QInputDialog） ==================== */

/**
 * @brief      文本值变化信号（对标 QInputDialog::textValueChanged；
 *             setTextValue 与手动触发时发射）。
 * @param      self 目标对话框。
 * @param      text 新文本（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XInputDialog_textValueChanged_signal(XInputDialog* self, const XString* text);
/**
 * @brief      整数值变化信号（对标 QInputDialog::intValueChanged；
 *             setIntValue 与手动触发时发射）。
 * @param      self 目标对话框。
 * @param      value 新整数值。
 * @return     信号标识。
 */
void* XInputDialog_intValueChanged_signal(XInputDialog* self, int value);
/**
 * @brief      浮点值变化信号（对标 QInputDialog::doubleValueChanged；
 *             setDoubleValue 与手动触发时发射）。
 * @param      self 目标对话框。
 * @param      value 新浮点值。
 * @return     信号标识。
 */
void* XInputDialog_doubleValueChanged_signal(XInputDialog* self, double value);
/**
 * @brief      下拉文本变化信号（Qt 5 QInputDialog::comboBoxTextChanged；
 *             本实现由手动触发发射）。
 * @param      self 目标对话框。
 * @param      text 新下拉文本（XString*，载荷深拷贝）。
 * @return     信号标识。
 */
void* XInputDialog_comboBoxTextChanged_signal(XInputDialog* self,
                                              const XString* text);
/** @brief 设置整数范围（对标 setIntRange）。 */
void XInputDialog_setIntRange(XInputDialog* self, int min, int max);
/** @brief 整数下限（对标 intMinimum）。 */
int XInputDialog_intMinimum(const XInputDialog* self);
/** @brief 设置整数下限（对标 setIntMinimum）。 */
void XInputDialog_setIntMinimum(XInputDialog* self, int min);
/** @brief 整数上限（对标 intMaximum）。 */
int XInputDialog_intMaximum(const XInputDialog* self);
/** @brief 设置整数上限（对标 setIntMaximum）。 */
void XInputDialog_setIntMaximum(XInputDialog* self, int max);
/** @brief 整数步进（对标 intStep）。 */
int XInputDialog_intStep(const XInputDialog* self);
/** @brief 设置整数步进（对标 setIntStep）。 */
void XInputDialog_setIntStep(XInputDialog* self, int step);
/** @brief 设置浮点范围（对标 setDoubleRange）。 */
void XInputDialog_setDoubleRange(XInputDialog* self, double min, double max);
/** @brief 浮点下限（对标 doubleMinimum）。 */
double XInputDialog_doubleMinimum(const XInputDialog* self);
/** @brief 设置浮点下限（对标 setDoubleMinimum）。 */
void XInputDialog_setDoubleMinimum(XInputDialog* self, double min);
/** @brief 浮点上限（对标 doubleMaximum）。 */
double XInputDialog_doubleMaximum(const XInputDialog* self);
/** @brief 设置浮点上限（对标 setDoubleMaximum）。 */
void XInputDialog_setDoubleMaximum(XInputDialog* self, double max);
/** @brief 浮点步进（对标 doubleStep）。 */
double XInputDialog_doubleStep(const XInputDialog* self);
/** @brief 设置浮点步进（对标 setDoubleStep）。 */
void XInputDialog_setDoubleStep(XInputDialog* self, double step);
/** @brief 浮点小数位（对标 doubleDecimals）。 */
int XInputDialog_doubleDecimals(const XInputDialog* self);
/** @brief 设置浮点小数位（对标 setDoubleDecimals）。 */
void XInputDialog_setDoubleDecimals(XInputDialog* self, int decimals);
/** @brief 文本回显模式（对标 textEchoMode）。 */
XInputDialogEchoMode XInputDialog_textEchoMode(const XInputDialog* self);
/** @brief 设置文本回显模式（对标 setTextEchoMode）。 */
void XInputDialog_setTextEchoMode(XInputDialog* self,
                                  XInputDialogEchoMode mode);
/** @brief 文本值确认信号（对标 textValueSelected）。 */
void* XInputDialog_textValueSelected_signal(XInputDialog* self,
                                            const XString* text);
/** @brief 整数值确认信号（对标 intValueSelected）。 */
void* XInputDialog_intValueSelected_signal(XInputDialog* self, int value);
/** @brief 浮点值确认信号（对标 doubleValueSelected）。 */
void* XInputDialog_doubleValueSelected_signal(XInputDialog* self,
                                              double value);
/** @brief 下拉文本确认信号（对标 comboBoxTextValueSelected）。 */
void* XInputDialog_comboBoxTextValueSelected_signal(XInputDialog* self,
                                                    const XString* text);


#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XINPUTDIALOG_H */
