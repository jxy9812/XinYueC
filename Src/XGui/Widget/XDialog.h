/**
 * @file       XDialog.h
 * @brief      XDialog 对话框控件（对标 Qt 6.8 QDialog 核心公共 API）。
 * @details    继承 XWidget；exec() 事件循环模态；accepted()/rejected()
 *             信号；done(int) 完成；setModal/setResult/result。
 * @note       模块总开关 XDIALOG_ON。
 * @author     XinYueC 团队
 */
#ifndef XDIALOG_H
#define XDIALOG_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XDIALOG_ON

XCLASS_DEFINE_BEGING(XDialog)
XCLASS_DEFINE_EXTEND_END(XDialog, XWidget)

typedef struct XDialog
{
    XWidget m_base;    /**< 基类成员；必须是第一个。 */
    int m_result;      /**< 对标 result()；accepted=1/rejected=0。 */
    bool m_modal;      /**< 对标 modal 属性（默认 true）。 */
    bool m_inExec;     /**< exec() 循环标志。 */
} XDialog;

XVtable* XDialog_class_init(void);
void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags);
#define XDialog_create(parent, flags) XDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XDialog_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XDialog_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      启动模态对话框事件循环（对标 QDialog::exec）。
 */
int XDialog_exec(XDialog* self);
/**
 * @brief      关闭对话框并设置结果码（对标 QDialog::done）。
 */
void XDialog_done(XDialog* self, int result);
/**
 * @brief      以接受方式关闭对话框（对标 QDialog::accept；result=1）。
 */
void XDialog_accept(XDialog* self);
/**
 * @brief      以拒绝方式关闭对话框（对标 QDialog::reject；result=0）。
 */
void XDialog_reject(XDialog* self);
/**
 * @brief      获取最近一次结果码（对标 QDialog::result）。
 */
int XDialog_result(const XDialog* self);
/**
 * @brief      设置结果码（对标 QDialog::setResult）。
 */
void XDialog_setResult(XDialog* self, int result);
/**
 * @brief      设置模态标志（对标 QDialog::setModal）。
 */
void XDialog_setModal(XDialog* self, bool modal);
/**
 * @brief      获取模态标志（对标 QDialog::isModal）。
 */
bool XDialog_isModal(const XDialog* self);

/* ==================== 信号 ==================== */

/**
 * @brief      接受信号（真发射）。
 */
void* XDialog_accepted_signal(XDialog* self);
/**
 * @brief      拒绝信号（真发射）。
 */
void* XDialog_rejected_signal(XDialog* self);
/**
 * @brief      完成信号（真发射）。
 */
void* XDialog_finished_signal(XDialog* self, int result);

#endif /* XWIDGET_ON && XDIALOG_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIALOG_H */