/**
 * @file       XMessageBox.h
 * @brief      XMessageBox 消息对话框控件（对标 Qt 6.8 QMessageBox 核心
 *             公共 API）。
 * @details    功能范围：
 *             - Icon 枚举（NoIcon/Information/Warning/Critical/Question，
 *               数值对齐）；StandardButton 位标志（与 XDialogButtonBox
 *               的枚举复用，位值逐项对齐 Qt）；
 *             - setText/title/icon、setStandardButtons/standardButtons、
 *               addButton(button, role)/addButton(text, role)/
 *               addButton(standard)、button(standard)、clickedButton；
 *             - exec()：模态事件循环（processEvents 驱动，对标 QDialog
 *               的模态语义）；
 *             - 静态便捷方法：information/warning/critical/question/
 *               about（阻塞直至用户选择，返回被点击的标准按钮）。
 *             与 Qt 的差异：XMessageBox 继承 XWidget（Qt 为 QDialog），
 *             因库内对话框基础设施尚在建设中；API 形状保持一致。
 * @note       模块总开关 XMESSAGEBOX_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XDIALOGBUTTONBOX_ON、
 *             XPUSHBUTTON_ON、XLABEL_ON。
 * @author     XinYueC 团队
 */
#ifndef XMESSAGEBOX_H
#define XMESSAGEBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XDIALOG_ON
#include "XDialog.h"
#endif
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"
#endif
#if XLABEL_ON
#include "XLabel.h"
#endif

#if XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON

/** @brief 消息图标（对标 QMessageBox::Icon，数值一致）。 */
typedef enum XMessageBoxIcon
{
    XMessageBoxIcon_NoIcon = 0,
    XMessageBoxIcon_Information = 1,
    XMessageBoxIcon_Warning = 2,
    XMessageBoxIcon_Critical = 3,
    XMessageBoxIcon_Question = 4
} XMessageBoxIcon;

/** @brief 按钮角色（对标 QMessageBox::ButtonRole，与 XDialogButtonBox
 *         Role 数值一致）。 */
typedef enum XMessageBoxButtonRole
{
    XMessageBoxButtonRole_InvalidRole = -1,
    XMessageBoxButtonRole_AcceptRole = 0,
    XMessageBoxButtonRole_RejectRole = 1,
    XMessageBoxButtonRole_DestructiveRole = 2,
    XMessageBoxButtonRole_ActionRole = 3,
    XMessageBoxButtonRole_HelpRole = 4,
    XMessageBoxButtonRole_YesRole = 5,
    XMessageBoxButtonRole_NoRole = 6,
    XMessageBoxButtonRole_ResetRole = 7,
    XMessageBoxButtonRole_ApplyRole = 8,
    XMessageBoxButtonRole_NRoles = 9
} XMessageBoxButtonRole;

XCLASS_DEFINE_BEGING(XMessageBox)
XCLASS_DEFINE_EXTEND_END(XMessageBox, XDialog)

typedef struct XMessageBox
{
    XWidget m_base;              /**< 基类成员；必须是第一个。 */
    XLabel* m_textLabel;         /**< 消息文本标签（拥有）。 */
#if XDIALOGBUTTONBOX_ON
    XDialogButtonBox* m_buttonBox; /**< 按钮盒（拥有）。 */
    XVector* m_standards;        /**< 与成员顺序对应的标准按钮值。 */
#endif
    char m_title[128];           /**< 窗口标题。 */
    char m_text[512];            /**< 消息文本缓存（对标 text）。 */
    int m_icon;                  /**< 图标（XMessageBoxIcon）。 */
    XAbstractButton* m_clicked;  /**< 最近点击的按钮（exec 结果）。 */
    bool m_inExec;               /**< exec 循环进行中。 */
} XMessageBox;

XVtable* XMessageBox_class_init(void);
void XMessageBox_init(XMessageBox* self, XWidget* parent,
                      XWidgetFlags flags);
#define XMessageBox_create(parent, flags) XMessageBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XMessageBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMessageBox_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 文本与图标 ==================== */

void XMessageBox_setText(XMessageBox* self, const char* utf8);
const char* XMessageBox_text(const XMessageBox* self);
void XMessageBox_setTitle(XMessageBox* self, const char* utf8);
const char* XMessageBox_title(const XMessageBox* self);
void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon);
XMessageBoxIcon XMessageBox_icon(const XMessageBox* self);

/* ==================== 按钮管理 ==================== */

/**
 * @brief      设置标准按钮组。
 */
void XMessageBox_setStandardButtons(XMessageBox* self, int buttons);
/**
 * @brief      获取标准按钮组。
 */
int XMessageBox_standardButtons(const XMessageBox* self);
/**
 * @brief      获取标准按钮。
 */
XAbstractButton* XMessageBox_button(const XMessageBox* self,
                                    XDialogButtonBoxStandardButton which);
/**
 * @brief      获取被点击按钮。
 */
XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self);

/* ==================== 模态执行 ==================== */

/**
 * @brief      启动模态对话框事件循环（对标 QDialog::exec）。
 */
XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self);

/* ==================== 静态便捷方法 ==================== */

/**
 * @brief      信息对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_information(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      警告对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_warning(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      严重错误对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_critical(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      询问对话框（静态）。
 */
XDialogButtonBoxStandardButton XMessageBox_question(
    XWidget* parent, const char* title, const char* text, int buttons);
/**
 * @brief      关于对话框（静态）。
 */
void XMessageBox_about(XWidget* parent, const char* title,
                       const char* text);

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMESSAGEBOX_H */