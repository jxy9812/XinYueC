/******************************************************************************
 * @file       XInputDialog.c
 * @brief      输入对话框控件实现（对标 Qt 6.8 QInputDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setTextValue/setIntValue/
 *             setDoubleValue 在实际变化时分别发射 textValueChanged/
 *             intValueChanged/doubleValueChanged；comboBoxTextChanged
 *             由应用手动触发。静态便捷函数创建临时实例并应用存储 setter，
 *             无 GUI 环境不执行模态循环，返回默认值且 *ok 置 false。
 * @note       本文件不依赖任何平台 API；原生输入面板为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XStringList.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON && XDIALOG_ON

#include "XInputDialog.h"
#include "XWidget_Protected.h"

/* ==================== 内部辅助 ==================== */

/** @brief 释放并清空拥有型 XString 字段。 */
static void xinputdialog_freeString(XString** slot)
{
    if (slot && *slot) {
        XString_delete_base((XClass*)*slot);
        *slot = NULL;
    }
}

/** @brief 深拷贝 XString；NULL 视为空串。失败返回 NULL（按空处理）。 */
static XString* xinputdialog_dupString(const XString* src)
{
    if (!src) return XString_create();
    return XString_create_copy(src);
}

/** @brief 字符串信号参数释放回调：释放列表内拷贝的 XString。 */
static void xinputdialog_stringSignal_del(XVarList* list)
{
    XVarList_args_1(list, XString*, text);
    if (text)
        XString_delete_base((XClass*)text);
}

/** @brief 发射携带 XString* 深拷贝的信号；无接收者时释放参数列表。 */
static void xinputdialog_emitString(XInputDialog* self, size_t signal,
                                    const XString* text)
{
    XString* copy;
    XVarList* args;
    /* XSignal() 以 NULL 单参调用信号函数：self 为空时不得读取载荷。 */
    if (!self) return;
    copy = xinputdialog_dupString(text);
    if (!copy) return;
    args = XVarList_Create(XVar(XString*, copy));
    if (!args) {
        XString_delete_base((XClass*)copy);
        return;
    }
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args,
                           xinputdialog_stringSignal_del, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_setArgsDel(args, xinputdialog_stringSignal_del);
        XVarList_delete(args);
    }
}

/** @brief 发射携带 int 参数的信号。 */
static void xinputdialog_emitInt(XInputDialog* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射携带 double 参数的信号。 */
static void xinputdialog_emitDouble(XInputDialog* self, size_t signal,
                                    double value)
{
    XVarList* args = XVarList_Create(XVar(double, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 类与实例生命周期 ==================== */

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXInputDialog_deinit(XInputDialog* self)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_labelText);
    xinputdialog_freeString(&self->m_textValue);
    xinputdialog_freeString(&self->m_comboBoxText);
    xinputdialog_freeString(&self->m_okButtonText);
    xinputdialog_freeString(&self->m_cancelButtonText);
    if (self->m_comboBoxItems) {
        XStringList_delete_base((XClass*)self->m_comboBoxItems);
        self->m_comboBoxItems = NULL;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XInputDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XInputDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXInputDialog_deinit);
    return XVTABLE_DEFAULT;
}

void XInputDialog_init(XInputDialog* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XInputDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_inputMode = XInputDialog_TextInput;
    self->m_options = 0;
    self->m_labelText = NULL;
    self->m_textValue = NULL;
    self->m_comboBoxText = NULL;
    self->m_comboBoxItems = NULL;
    self->m_comboBoxEditable = false;
    self->m_echoMode = XInputDialogEchoMode_Normal;
    self->m_intValue = 0;
    self->m_doubleValue = 0.0;
    self->m_okButtonText = NULL;
    self->m_cancelButtonText = NULL;
    /* 默认范围对齐 Qt QInputDialog：int 全范围、double 全范围、
       步进 1、小数位 2。 */
    self->m_intMinimum = -2147483647 - 1;
    self->m_intMaximum = 2147483647;
    self->m_intStep = 1;
    self->m_doubleMinimum = -1.0e308;
    self->m_doubleMaximum = 1.0e308;
    self->m_doubleStep = 1.0;
    self->m_doubleDecimals = 2;
}

XInputDialog* XInputDialog_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags)
{
    XInputDialog* self = (XInputDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XInputDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

void XInputDialog_setInputMode(XInputDialog* self, XInputDialogInputMode mode)
{ if (self) self->m_inputMode = mode; }

XInputDialogInputMode XInputDialog_inputMode(const XInputDialog* self)
{ return self ? self->m_inputMode : XInputDialog_TextInput; }

void XInputDialog_setLabelText(XInputDialog* self, const XString* text)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_labelText);
    self->m_labelText = xinputdialog_dupString(text);
}

XString* XInputDialog_labelText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_labelText) : XString_create();
}

void XInputDialog_setTextValue(XInputDialog* self, const XString* text)
{
    XString* copy;
    if (!self) return;
    copy = xinputdialog_dupString(text);
    if (!copy) return;
    if (self->m_textValue &&
        XString_compare(self->m_textValue, copy) == 0) {
        XString_delete_base((XClass*)copy);
        return;
    }
    xinputdialog_freeString(&self->m_textValue);
    self->m_textValue = copy;
    xinputdialog_emitString(self,
                            (size_t)XInputDialog_textValueChanged_signal,
                            self->m_textValue);
}

XString* XInputDialog_textValue(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_textValue) : XString_create();
}

void XInputDialog_setIntValue(XInputDialog* self, int value)
{
    if (!self || self->m_intValue == value) return;
    self->m_intValue = value;
    xinputdialog_emitInt(self, (size_t)XInputDialog_intValueChanged_signal,
                         value);
}

int XInputDialog_intValue(const XInputDialog* self)
{ return self ? self->m_intValue : 0; }

void XInputDialog_setDoubleValue(XInputDialog* self, double value)
{
    if (!self || self->m_doubleValue == value) return;
    self->m_doubleValue = value;
    xinputdialog_emitDouble(self, (size_t)XInputDialog_doubleValueChanged_signal,
                            value);
}

double XInputDialog_doubleValue(const XInputDialog* self)
{ return self ? self->m_doubleValue : 0.0; }

void XInputDialog_setComboBoxItems(XInputDialog* self, const XStringList* items)
{
    int64_t i, n;
    if (!self) return;
    if (self->m_comboBoxItems) {
        XStringList_clear_base((XContainer*)self->m_comboBoxItems);
    } else {
        self->m_comboBoxItems =
            XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    }
    if (!self->m_comboBoxItems || !items) return;
    n = XStringList_size_base((const XContainer*)items);
    for (i = 0; i < n; ++i) {
        XString* item = (XString*)XStringList_at_base(items, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base(self->m_comboBoxItems, copy);
            XString_delete_base((XClass*)copy);
            copy = NULL;
        }
    }
}

XStringList* XInputDialog_comboBoxItems(const XInputDialog* self)
{
    XStringList* out;
    int64_t i, n;
    if (!self) return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    out = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!out) return NULL;
    if (!self->m_comboBoxItems) return out;
    n = XStringList_size_base((const XContainer*)self->m_comboBoxItems);
    for (i = 0; i < n; ++i) {
        XString* item = (XString*)XStringList_at_base(self->m_comboBoxItems, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base(out, copy);
            XString_delete_base((XClass*)copy);
            copy = NULL;
        }
    }
    return out;
}

void XInputDialog_setComboBoxEditable(XInputDialog* self, bool editable)
{ if (self) self->m_comboBoxEditable = editable; }

bool XInputDialog_isComboBoxEditable(const XInputDialog* self)
{ return self ? self->m_comboBoxEditable : false; }

void XInputDialog_setOkButtonText(XInputDialog* self, const XString* text)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_okButtonText);
    self->m_okButtonText = xinputdialog_dupString(text);
}

XString* XInputDialog_okButtonText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_okButtonText) : XString_create();
}

void XInputDialog_setCancelButtonText(XInputDialog* self, const XString* text)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_cancelButtonText);
    self->m_cancelButtonText = xinputdialog_dupString(text);
}

XString* XInputDialog_cancelButtonText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_cancelButtonText)
                : XString_create();
}

void XInputDialog_setOption(XInputDialog* self, XInputDialogOption option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= (XInputDialogOptions)option;
    else self->m_options &= (XInputDialogOptions)~option;
}

bool XInputDialog_testOption(const XInputDialog* self, XInputDialogOption option)
{
    return self ? (self->m_options & (XInputDialogOptions)option) != 0 : false;
}

void XInputDialog_setOptions(XInputDialog* self, XInputDialogOptions options)
{ if (self) self->m_options = options; }

XInputDialogOptions XInputDialog_options(const XInputDialog* self)
{ return self ? self->m_options : 0; }

/* ==================== 静态便捷函数 ==================== */

/** @brief 创建临时实例并按静态参数应用存储 setter（无模态执行）。 */
static XInputDialog* xinputdialog_tempSetup(XWidget* parent,
                                            const XString* title,
                                            const XString* label)
{
    XInputDialog* dlg = XInputDialog_create(parent, 0);
    if (!dlg) return NULL;
    if (title)
        XWidget_setWindowTitle((XWidget*)dlg, title);
    XInputDialog_setLabelText(dlg, label);
    return dlg;
}

XString* XInputDialog_getText(XWidget* parent, const XString* title,
                              const XString* label, XInputDialogEchoMode echo,
                              const XString* text, bool* ok)
{
    XInputDialog* dlg;
    XString* result;
    if (ok) *ok = false;
    dlg = xinputdialog_tempSetup(parent, title, label);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
    XInputDialog_setTextValue(dlg, text);
    dlg->m_echoMode = echo;
    result = XString_create();
    XInputDialog_delete_base(dlg);
    return result;
}

XString* XInputDialog_getText_2(XWidget* parent, const char* title,
                                const char* label, XInputDialogEchoMode echo,
                                const char* text, bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    XString* v = text ? XString_create_utf8(text) : NULL;
    XString* result = XInputDialog_getText(parent, t, l, echo, v, ok);
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    if (v) XString_delete_base((XClass*)v);
    return result;
}

XString* XInputDialog_getMultiLineText(XWidget* parent, const XString* title,
                                       const XString* label, const XString* text,
                                       bool* ok)
{
    XInputDialog* dlg;
    XString* result;
    if (ok) *ok = false;
    dlg = xinputdialog_tempSetup(parent, title, label);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
    XInputDialog_setTextValue(dlg, text);
    result = XString_create();
    XInputDialog_delete_base(dlg);
    return result;
}

XString* XInputDialog_getMultiLineText_2(XWidget* parent, const char* title,
                                         const char* label, const char* text,
                                         bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    XString* v = text ? XString_create_utf8(text) : NULL;
    XString* result = XInputDialog_getMultiLineText(parent, t, l, v, ok);
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    if (v) XString_delete_base((XClass*)v);
    return result;
}

int XInputDialog_getInt(XWidget* parent, const XString* title,
                        const XString* label, int value, int minValue,
                        int maxValue, int step, bool* ok)
{
    XInputDialog* dlg;
    (void)minValue; (void)maxValue; (void)step;
    if (ok) *ok = false;
    dlg = xinputdialog_tempSetup(parent, title, label);
    if (!dlg) return value;
    XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
    XInputDialog_setIntValue(dlg, value);
    XInputDialog_delete_base(dlg);
    return value;
}

int XInputDialog_getInt_2(XWidget* parent, const char* title, const char* label,
                          int value, int minValue, int maxValue, int step,
                          bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    int result = XInputDialog_getInt(parent, t, l, value, minValue, maxValue,
                                     step, ok);
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    return result;
}

double XInputDialog_getDouble(XWidget* parent, const XString* title,
                              const XString* label, double value,
                              double minValue, double maxValue, int decimals,
                              bool* ok)
{
    XInputDialog* dlg;
    (void)minValue; (void)maxValue; (void)decimals;
    if (ok) *ok = false;
    dlg = xinputdialog_tempSetup(parent, title, label);
    if (!dlg) return value;
    XInputDialog_setInputMode(dlg, XInputDialog_DoubleInput);
    XInputDialog_setDoubleValue(dlg, value);
    XInputDialog_delete_base(dlg);
    return value;
}

double XInputDialog_getDouble_2(XWidget* parent, const char* title,
                                const char* label, double value,
                                double minValue, double maxValue, int decimals,
                                bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    double result = XInputDialog_getDouble(parent, t, l, value, minValue,
                                           maxValue, decimals, ok);
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    return result;
}

XString* XInputDialog_getItem(XWidget* parent, const XString* title,
                              const XString* label, const XStringList* items,
                              int current, bool editable, bool* ok)
{
    XInputDialog* dlg;
    XString* result;
    XString* item = NULL;
    if (ok) *ok = false;
    dlg = xinputdialog_tempSetup(parent, title, label);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_ComboBoxInput);
    XInputDialog_setComboBoxItems(dlg, items);
    XInputDialog_setComboBoxEditable(dlg, editable);
    if (items && current >= 0) {
        int64_t n = XStringList_size_base((const XContainer*)items);
        if ((int64_t)current < n)
            item = (XString*)XStringList_at_base(items, current);
    }
    result = item ? XString_create_copy(item) : XString_create();
    XInputDialog_delete_base(dlg);
    return result;
}

XString* XInputDialog_getItem_2(XWidget* parent, const char* title,
                                const char* label, const char* const* items,
                                int count, int current, bool editable, bool* ok)
{
    XStringList* list;
    XString* result;
    int i;
    if (ok) *ok = false;
    list = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!list) return XString_create();
    for (i = 0; i < count; ++i) {
        if (items && items[i])
            XStringList_push_back_utf8(list, items[i]);
        else
            XStringList_push_back_utf8(list, "");
    }
    result = XInputDialog_getItem(parent, NULL, NULL, list, current, editable, NULL);
    XStringList_delete_base((XClass*)list);
    return result;
}

/* ==================== 信号 ==================== */

void* XInputDialog_textValueChanged_signal(XInputDialog* self, const XString* text)
{
    xinputdialog_emitString(self, (size_t)XInputDialog_textValueChanged_signal,
                            text);
    return (void*)(size_t)XInputDialog_textValueChanged_signal;
}

void* XInputDialog_intValueChanged_signal(XInputDialog* self, int value)
{
    xinputdialog_emitInt(self, (size_t)XInputDialog_intValueChanged_signal,
                         value);
    return (void*)(size_t)XInputDialog_intValueChanged_signal;
}

void* XInputDialog_doubleValueChanged_signal(XInputDialog* self, double value)
{
    xinputdialog_emitDouble(self, (size_t)XInputDialog_doubleValueChanged_signal,
                            value);
    return (void*)(size_t)XInputDialog_doubleValueChanged_signal;
}

void* XInputDialog_comboBoxTextChanged_signal(XInputDialog* self,
                                              const XString* text)
{
    /* XSignal() 以 NULL 单参调用信号函数：self 为空时不得读取载荷。 */
    if (!self) return (void*)(size_t)XInputDialog_comboBoxTextChanged_signal;
    xinputdialog_emitString(self,
                            (size_t)XInputDialog_comboBoxTextChanged_signal,
                            text);
    xinputdialog_freeString(&self->m_comboBoxText);
    self->m_comboBoxText = xinputdialog_dupString(text);
    return (void*)(size_t)XInputDialog_comboBoxTextChanged_signal;
}

#endif /* XWIDGET_ON && XDIALOG_ON */

/* ==================== Task 2.21 回检补齐：范围/回显/确认信号 =========== */

void XInputDialog_setIntRange(XInputDialog* self, int min, int max)
{
    if (!self) return;
    self->m_intMinimum = min;
    self->m_intMaximum = max;
    if (self->m_intValue < min) self->m_intValue = min;
    if (self->m_intValue > max) self->m_intValue = max;
}

int XInputDialog_intMinimum(const XInputDialog* self)
{ return self ? self->m_intMinimum : -2147483647 - 1; }

void XInputDialog_setIntMinimum(XInputDialog* self, int min)
{ if (self) self->m_intMinimum = min; }

int XInputDialog_intMaximum(const XInputDialog* self)
{ return self ? self->m_intMaximum : 2147483647; }

void XInputDialog_setIntMaximum(XInputDialog* self, int max)
{ if (self) self->m_intMaximum = max; }

int XInputDialog_intStep(const XInputDialog* self)
{ return self ? self->m_intStep : 1; }

void XInputDialog_setIntStep(XInputDialog* self, int step)
{ if (self && step > 0) self->m_intStep = step; }

void XInputDialog_setDoubleRange(XInputDialog* self, double min, double max)
{
    if (!self) return;
    self->m_doubleMinimum = min;
    self->m_doubleMaximum = max;
    if (self->m_doubleValue < min) self->m_doubleValue = min;
    if (self->m_doubleValue > max) self->m_doubleValue = max;
}

double XInputDialog_doubleMinimum(const XInputDialog* self)
{ return self ? self->m_doubleMinimum : -1.0e308; }

void XInputDialog_setDoubleMinimum(XInputDialog* self, double min)
{ if (self) self->m_doubleMinimum = min; }

double XInputDialog_doubleMaximum(const XInputDialog* self)
{ return self ? self->m_doubleMaximum : 1.0e308; }

void XInputDialog_setDoubleMaximum(XInputDialog* self, double max)
{ if (self) self->m_doubleMaximum = max; }

double XInputDialog_doubleStep(const XInputDialog* self)
{ return self ? self->m_doubleStep : 1.0; }

void XInputDialog_setDoubleStep(XInputDialog* self, double step)
{ if (self && step > 0.0) self->m_doubleStep = step; }

int XInputDialog_doubleDecimals(const XInputDialog* self)
{ return self ? self->m_doubleDecimals : 2; }

void XInputDialog_setDoubleDecimals(XInputDialog* self, int decimals)
{ if (self && decimals >= 0) self->m_doubleDecimals = decimals; }

XInputDialogEchoMode XInputDialog_textEchoMode(const XInputDialog* self)
{ return self ? self->m_echoMode : XInputDialogEchoMode_Normal; }

void XInputDialog_setTextEchoMode(XInputDialog* self,
                                  XInputDialogEchoMode mode)
{ if (self) self->m_echoMode = mode; }

void* XInputDialog_textValueSelected_signal(XInputDialog* self,
                                            const XString* text)
{
    if (!self) return (void*)(size_t)XInputDialog_textValueSelected_signal;
    XInputDialog_textValueChanged_signal(self, text);
    return (void*)(size_t)XInputDialog_textValueSelected_signal;
}

void* XInputDialog_intValueSelected_signal(XInputDialog* self, int value)
{
    if (!self) return (void*)(size_t)XInputDialog_intValueSelected_signal;
    XInputDialog_intValueChanged_signal(self, value);
    return (void*)(size_t)XInputDialog_intValueSelected_signal;
}

void* XInputDialog_doubleValueSelected_signal(XInputDialog* self,
                                              double value)
{
    if (!self) return (void*)(size_t)XInputDialog_doubleValueSelected_signal;
    XInputDialog_doubleValueChanged_signal(self, value);
    return (void*)(size_t)XInputDialog_doubleValueSelected_signal;
}

void* XInputDialog_comboBoxTextValueSelected_signal(XInputDialog* self,
                                                    const XString* text)
{
    if (!self)
        return (void*)(size_t)XInputDialog_comboBoxTextValueSelected_signal;
    XInputDialog_comboBoxTextChanged_signal(self, text);
    return (void*)(size_t)XInputDialog_comboBoxTextValueSelected_signal;
}
