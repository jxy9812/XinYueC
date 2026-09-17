/******************************************************************************
 * @file       XProgressDialog.c
 * @brief      进度对话框控件实现（对标 Qt 6.8 QProgressDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setValue 钳位后按 Qt 语义
 *             处理 autoReset/autoClose；cancel() 发射 canceled() 并复位；
 *             forceShow 直接 XWidget_show。setBar 仅存储借用指针。
 * @note       本文件不依赖任何平台 API；子控件布局与原生进度面板为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON && XDIALOG_ON

#include "XProgressDialog.h"
#include "XWidget_Protected.h"
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif

/* ==================== 内部辅助 ==================== */

/** @brief 释放并清空拥有型 XString 字段。 */
static void xprogressdialog_freeString(XString** slot)
{
    if (slot && *slot) {
        XString_delete_base((XClass*)*slot);
        *slot = NULL;
    }
}

/** @brief 深拷贝 XString；NULL 视为空串。 */
static XString* xprogressdialog_dupString(const XString* src)
{
    if (!src) return XString_create();
    return XString_create_copy(src);
}

/** @brief 发射空参信号（canceled）。 */
static void xprogressdialog_emitVoid(XProgressDialog* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 把值钳位到 [min,max]（min>max 时视为 [max,min]）。 */
static int xprogressdialog_clamp(const XProgressDialog* self, int value)
{
    int lo, hi;
    if (!self) return value;
    lo = self->m_minimum <= self->m_maximum ? self->m_minimum : self->m_maximum;
    hi = self->m_minimum <= self->m_maximum ? self->m_maximum : self->m_minimum;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* ==================== 类与实例生命周期 ==================== */

/** @brief 按当前尺寸重排自定义标签与取消按钮（顶部文本行 + 右下按钮，
 *         对标 Qt 进度对话框的子控件布局）。 */
static void xprogressdialog_relayout(XProgressDialog* self)
{
    int w;
    int h;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        int lw = w - 32;
        if (lw < 0) lw = 0;
        XWidget_setGeometry((XWidget*)self->m_label, 16, 12, lw, 20);
    }
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    if (self->m_cancelButton)
        XWidget_setGeometry((XWidget*)self->m_cancelButton,
                            w - 84, h - 34, 80, 26);
#endif
}

/** @brief 尺寸变化后重排自定义子控件（对标 Qt 的布局更新）。 */
static void VXProgressDialog_resizeEvent(XWidget* self, XEvent* event)
{
    XProgressDialog* dlg = (XProgressDialog*)self;
    (void)event;
    if (!dlg) return;
    xprogressdialog_relayout(dlg);
}

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
/** @brief 自定义取消按钮 clicked → cancel()（对标 Qt 的
 *         clicked→canceled→cancel 链路，XGui 合并为一次 cancel 调用）。 */
static void xprogressdialog_cancelClickedSlot(XObject* receiver, XVarList* args)
{
    XProgressDialog* self = (XProgressDialog*)receiver;
    (void)args;
    if (self) XProgressDialog_cancel(self);
}
#endif

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXProgressDialog_deinit(XProgressDialog* self)
{
    if (!self) return;
    xprogressdialog_freeString(&self->m_labelText);
    xprogressdialog_freeString(&self->m_cancelButtonText);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        XLabel_delete_base((XClass*)self->m_label);
        self->m_label = NULL;
    }
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    if (self->m_cancelButton) {
        XPushButton_delete_base((XClass*)self->m_cancelButton);
        self->m_cancelButton = NULL;
    }
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XProgressDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProgressDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXProgressDialog_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXProgressDialog_resizeEvent);
    return XVTABLE_DEFAULT;
}

void XProgressDialog_init(XProgressDialog* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XProgressDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minimum = 0;
    self->m_maximum = 100;
    self->m_value = 0;
    self->m_minimumDuration = 4000;
    self->m_wasCanceled = false;
    self->m_autoReset = true;
    self->m_autoClose = true;
    self->m_labelText = NULL;
    self->m_cancelButtonText = NULL;
    self->m_bar = NULL;
}

void XProgressDialog_init_full(XProgressDialog* self, const XString* labelText,
                               const XString* cancelButtonText, int minimum,
                               int maximum, XWidget* parent)
{
    XProgressDialog_init(self, parent, 0);
    if (!self) return;
    XProgressDialog_setRange(self, minimum, maximum);
    XProgressDialog_setLabelText(self, labelText);
    XProgressDialog_setCancelButtonText(self, cancelButtonText);
}

XProgressDialog* XProgressDialog_create_ex(XMemoryType memory, XWidget* parent,
                                           XWidgetFlags flags)
{
    XProgressDialog* self =
        (XProgressDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XProgressDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 范围与值 ==================== */

void XProgressDialog_setRange(XProgressDialog* self, int minimum, int maximum)
{
    int tmp;
    if (!self) return;
    if (minimum > maximum) {
        tmp = minimum;
        minimum = maximum;
        maximum = tmp;
    }
    self->m_minimum = minimum;
    self->m_maximum = maximum;
    self->m_value = xprogressdialog_clamp(self, self->m_value);
}

void XProgressDialog_setMinimum(XProgressDialog* self, int minimum)
{
    if (!self) return;
    XProgressDialog_setRange(self, minimum, self->m_maximum);
}

void XProgressDialog_setMaximum(XProgressDialog* self, int maximum)
{
    if (!self) return;
    XProgressDialog_setRange(self, self->m_minimum, maximum);
}

int XProgressDialog_minimum(const XProgressDialog* self)
{ return self ? self->m_minimum : 0; }

int XProgressDialog_maximum(const XProgressDialog* self)
{ return self ? self->m_maximum : 100; }

void XProgressDialog_setValue(XProgressDialog* self, int progress)
{
    if (!self) return;
    self->m_value = xprogressdialog_clamp(self, progress);
    /* Qt：入参等于 maximum 且 autoReset 时复位。 */
    if (progress == self->m_maximum && self->m_autoReset)
        XProgressDialog_reset(self);
}

int XProgressDialog_value(const XProgressDialog* self)
{ return self ? self->m_value : 0; }

void XProgressDialog_reset(XProgressDialog* self)
{
    if (!self) return;
    if (self->m_autoClose)
        XWidget_setVisible((XWidget*)self, false);
    self->m_value = self->m_minimum;
    self->m_wasCanceled = false;
}

/* ==================== 文本与取消 ==================== */

void XProgressDialog_setLabelText(XProgressDialog* self, const XString* text)
{
    if (!self) return;
#if XFRAME_ON && XLABEL_ON
    /* 对标 Qt：存在自定义标签时文本转发给标签。 */
    if (self->m_label) {
        XLabel_setText(self->m_label, text);
        return;
    }
#endif
    xprogressdialog_freeString(&self->m_labelText);
    self->m_labelText = xprogressdialog_dupString(text);
}

XString* XProgressDialog_labelText(const XProgressDialog* self)
{
    if (!self) return XString_create();
#if XFRAME_ON && XLABEL_ON
    if (self->m_label)
        return xprogressdialog_dupString(XLabel_text(self->m_label));
#endif
    return xprogressdialog_dupString(self->m_labelText);
}

void XProgressDialog_setCancelButtonText(XProgressDialog* self,
                                         const XString* text)
{
    if (!self) return;
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    /* 对标 Qt：存在自定义取消按钮时文本转发给按钮。 */
    if (self->m_cancelButton) {
        XAbstractButton_setText((XAbstractButton*)self->m_cancelButton, text);
        return;
    }
#endif
    xprogressdialog_freeString(&self->m_cancelButtonText);
    self->m_cancelButtonText = xprogressdialog_dupString(text);
}

#if XFRAME_ON && XLABEL_ON
void XProgressDialog_setLabel(XProgressDialog* self, XLabel* label)
{
    if (!self || self->m_label == label) return;
    /* 对标 Qt setLabel：删除旧标签并接管新标签所有权。 */
    if (self->m_label) {
        XLabel_delete_base((XClass*)self->m_label);
        self->m_label = NULL;
    }
    self->m_label = label;
    if (self->m_label) {
        XWidget_setParentPlain((XWidget*)self->m_label, (XWidget*)self);
        XWidget_show((XWidget*)self->m_label);
        xprogressdialog_relayout(self);
    }
}

XLabel* XProgressDialog_label(const XProgressDialog* self)
{ return self ? self->m_label : NULL; }
#endif /* XFRAME_ON && XLABEL_ON */

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
void XProgressDialog_setCancelButton(XProgressDialog* self,
                                     XPushButton* button)
{
    if (!self || self->m_cancelButton == button) return;
    /* 对标 Qt setCancelButton：删除旧按钮、接管新按钮所有权并接线
     * clicked → cancel()。 */
    if (self->m_cancelButton) {
        XObject_disconnect_1((XObject*)self->m_cancelButton,
                             XSignal(XAbstractButton_clicked_signal),
                             (XObject*)self, xprogressdialog_cancelClickedSlot);
        XPushButton_delete_base((XClass*)self->m_cancelButton);
        self->m_cancelButton = NULL;
    }
    self->m_cancelButton = button;
    if (self->m_cancelButton) {
        XWidget_setParentPlain((XWidget*)self->m_cancelButton, (XWidget*)self);
        XWidget_show((XWidget*)self->m_cancelButton);
        if (self->m_cancelButtonText) {
            XAbstractButton_setText((XAbstractButton*)self->m_cancelButton,
                                    self->m_cancelButtonText);
        }
        XObject_connect_1((XObject*)self->m_cancelButton,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)self,
                          xprogressdialog_cancelClickedSlot,
                          XConnectionType_Direct);
        xprogressdialog_relayout(self);
    }
}

XPushButton* XProgressDialog_cancelButton(const XProgressDialog* self)
{ return self ? self->m_cancelButton : NULL; }
#endif /* XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */

void XProgressDialog_setBar(XProgressDialog* self, XProgressBar* bar)
{ if (self) self->m_bar = bar; }

void XProgressDialog_cancel(XProgressDialog* self)
{
    if (!self) return;
    /* Qt 可观察效果：canceled() 发射 → cancel() → 复位 + wasCanceled=true；
     * Qt 经 forceHide 保证取消时无条件隐藏（即使 autoClose=false）。 */
    xprogressdialog_emitVoid(self, (size_t)XProgressDialog_canceled_signal);
    XProgressDialog_reset(self);
    XWidget_setVisible((XWidget*)self, false);
    self->m_wasCanceled = true;
}

bool XProgressDialog_wasCanceled(const XProgressDialog* self)
{ return self ? self->m_wasCanceled : false; }

/* ==================== 行为属性 ==================== */

void XProgressDialog_setAutoReset(XProgressDialog* self, bool reset)
{ if (self) self->m_autoReset = reset; }

bool XProgressDialog_autoReset(const XProgressDialog* self)
{ return self ? self->m_autoReset : true; }

void XProgressDialog_setAutoClose(XProgressDialog* self, bool close)
{ if (self) self->m_autoClose = close; }

bool XProgressDialog_autoClose(const XProgressDialog* self)
{ return self ? self->m_autoClose : true; }

void XProgressDialog_setMinimumDuration(XProgressDialog* self, int ms)
{ if (self) self->m_minimumDuration = ms; }

int XProgressDialog_minimumDuration(const XProgressDialog* self)
{ return self ? self->m_minimumDuration : 4000; }

void XProgressDialog_forceShow(XProgressDialog* self)
{
    if (!self) return;
    XWidget_show((XWidget*)self);
}

/* ==================== 信号 ==================== */

void* XProgressDialog_canceled_signal(XProgressDialog* self)
{
    xprogressdialog_emitVoid(self, (size_t)XProgressDialog_canceled_signal);
    return (void*)(size_t)XProgressDialog_canceled_signal;
}

#endif /* XWIDGET_ON && XDIALOG_ON */
