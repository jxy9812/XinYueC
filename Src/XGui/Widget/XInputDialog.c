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
/* 真实弹窗依赖（对标 Qt 静态便捷函数的对话框组装路径）： */
#include <stdio.h>             /* snprintf：浮点初值文本化 */
#include <stdlib.h>            /* strtod：浮点输入解析 */
#include "XCoreApplication.h"  /* qApp 等价物：有应用实例才允许模态循环 */
#include "XGuiApplication.h"   /* 主屏查询（弹窗居中） */
#include "XScreen.h"           /* 屏幕几何 */
#include "XLabel.h"            /* 提示标签 */
#include "XLineEdit.h"         /* 文本/浮点输入 */
#include "XSpinBox.h"          /* 整数输入 */
#include "XComboBox.h"         /* 下拉选择 */
#include "XPushButton.h"       /* OK/Cancel */
#include "XPlainTextEdit.h"    /* 多行文本输入 */
#include "XBoxLayout.h"        /* 对话框布局 */

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

/* ==================== 静态便捷函数（真实弹窗） ====================
 * 对标 Qt QInputDialog::getText/getMultiLineText/getInt/getDouble/getItem
 * 静态便捷函数：构造 XDialog + 内嵌输入控件 + OK/Cancel 按钮行，经
 * XDialog_exec 阻塞式模态循环（应用模态，Escape→reject）至用户确认；
 * 无 GUI 环境（无 XCoreApplication 实例，如无头测试）保持桩约定：
 * 返回默认值且 *ok=false。 */

/** @brief GUI 环境探测：存在 XCoreApplication 实例才执行真实模态循环。 */
static bool xid_guiReady(void)
{
    return XCoreApplication_instance() != NULL;
}

/** @brief 以 UTF-8 设置对象 objectName（对标 QObject::setObjectName）。 */
static void xid_setName(XObject* obj, const char* name)
{
    XString tmp;
    if (!obj) return;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    XObject_setObjectName(obj, &tmp);
    XClass_deinit_base((XClass*)&tmp);
}

/* 子控件 objectName 常量（对标 Qt 对话框私有子对象命名；槽内经
 * findChild 取回，避免 C 语言的 d-pointer 方案）。 */
#define XID_NAME_EDIT  "qt_input_dialog_edit"
#define XID_NAME_SPIN  "qt_input_dialog_spin"
#define XID_NAME_COMBO "qt_input_dialog_combo"
#define XID_NAME_PLAIN "qt_input_dialog_plain"
#define XID_NAME_OK    "qt_input_dialog_ok"
#define XID_NAME_CANCEL "qt_input_dialog_cancel"

/** @brief 按 objectName 查找对话框直接子控件（对标 QObject::findChild）。 */
static XWidget* xid_childByName(XDialog* dlg, const char* name)
{
    XString tmp;
    XWidget* w;
    if (!dlg) return NULL;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    w = (XWidget*)XObject_findChild((XObject*)dlg, &tmp,
                                    XFindDirectChildrenOnly);
    XClass_deinit_base((XClass*)&tmp);
    return w;
}

/** @brief 弹窗主屏居中（对标 Qt 静态便捷函数把对话框定位于屏幕中央）。 */
static void xid_centerOnScreen(XWidget* w)
{
    /* 子控件形态对话框居中于父控件（几何为父系坐标；按屏幕坐标
     * move 会落到页面坐标系 right-bottom 之外被裁剪——实测溢出右
     * 缘）。无父时回退屏幕居中。 */
    XWidget* parent = w ? XWidget_parentWidget(w) : NULL;
    if (parent) {
        int pw = XWidget_width(parent);
        int ph = XWidget_height(parent);
        int dw = XWidget_width(w);
        int dh = XWidget_height(w);
        XWidget_move(w, pw > dw ? (pw - dw) / 2 : 0,
                        ph > dh ? (ph - dh) / 2 : 0);
        return;
    }
    {
        XScreen* screen;
        XRect g;
        if (!w) return;
        screen = XGuiApplication_primaryScreen();
        if (!screen) return;
        g = XScreen_geometry(screen);
        if (g.width <= 0 || g.height <= 0) return;
        XWidget_move(w, g.x + (g.width - XWidget_width(w)) / 2,
                        g.y + (g.height - XWidget_height(w)) / 2);
    }
}

/** @brief OK 槽：把内嵌控件当前值结算进对话框存储后 accept（对标 Qt
 *  QInputDialog 在 accept 前由输入控件同步 d->value 的路径）。 */
static void xid_acceptSlot(XObject* receiver, XVarList* args);

/** @brief Cancel 槽：reject 关闭（对标 cancel 按钮触发 reject()）。 */
static void xid_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XDialog_reject((XDialog*)receiver);
}

/** @brief 组装对话框骨架：Dialog 窗口标志 + 标题 + 垂直布局 + 可选标签。
 * @param outRoot 输出顶层布局；调用方在对话框删除后负责
 *                XLayout_delete_base（布局不随控件析构释放）。 */
static XInputDialog* xid_buildDialog(XWidget* parent, const XString* title,
                                     const XString* label,
                                     XBoxLayout** outRoot)
{
    XInputDialog* dlg;
    XBoxLayout* root;
    if (!outRoot) return NULL;
    *outRoot = NULL;
    /* XGui 单原生窗口模型：Dialog 窗口形态的独立 X 窗口首帧 flush
     * 不保证可达（映射前 flush 丢失后无脏区，表现为「透明、啥都没有」
     * ——用户实测输入/文件/颜色便捷路径全部命中）。便捷路径统一以
     * 子控件形态挂 parent（与 demo 消息框同款已验证路径），模态语义
     * 由 XDialog_exec 的应用模态承载。 */
    dlg = XInputDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    if (!dlg) return NULL;
    if (title)
        XWidget_setWindowTitle((XWidget*)dlg, title);
    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, (XWidget*)dlg);
    if (!root) {
        XInputDialog_delete_base((XClass*)dlg);
        return NULL;
    }
    XLayout_setContentsMargins((XLayout*)root, 12, 12, 12, 12);
    XLayout_setSpacing((XLayout*)root, 8);
    if (label) {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        if (lb) {
            XLabel_setText(lb, label);
            XBoxLayout_addWidget(root, (XWidget*)lb);
        }
    }
    *outRoot = root;
    return dlg;
}

/** @brief 组装 OK/Cancel 按钮行并连接 accept/reject 槽（按钮文本与
 *  XDialogButtonBox 标准按钮一致：确定/取消）。
 * @param outBar 输出按钮行布局；调用方负责删除（addLayout 子布局不归
 *               父布局所有，对标 QLayout 所有权语义）。 */
static void xid_addButtons(XInputDialog* dlg, XBoxLayout* root,
                           XBoxLayout** outBar)
{
    XPushButton* ok;
    XPushButton* cancel;
    XBoxLayout* bar;
    if (!dlg || !root || !outBar) return;
    *outBar = NULL;
    bar = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
    if (!bar) return;
    XBoxLayout_addStretch(bar, 1);
    ok = XPushButton_create((XWidget*)dlg, 0);
    if (ok) {
        XAbstractButton_setText_2((XAbstractButton*)ok, "确定");
        XWidget_setMinimumSize((XWidget*)ok, 80, 28);
        xid_setName((XObject*)ok, XID_NAME_OK);
        XBoxLayout_addWidget(bar, (XWidget*)ok);
        XObject_connect_1((XObject*)ok,
                          (size_t)XAbstractButton_clicked_signal,
                          (XObject*)dlg, xid_acceptSlot,
                          XConnectionType_Direct);
    }
    cancel = XPushButton_create((XWidget*)dlg, 0);
    if (cancel) {
        XAbstractButton_setText_2((XAbstractButton*)cancel, "取消");
        XWidget_setMinimumSize((XWidget*)cancel, 80, 28);
        xid_setName((XObject*)cancel, XID_NAME_CANCEL);
        XBoxLayout_addWidget(bar, (XWidget*)cancel);
        XObject_connect_1((XObject*)cancel,
                          (size_t)XAbstractButton_clicked_signal,
                          (XObject*)dlg, xid_rejectSlot,
                          XConnectionType_Direct);
    }
    /* 对标 Qt 模态对话框内 Tab 焦点链不越出对话框的窗口级语义（详
     * 见 XDialogButtonBox.c xdb_relayout 同款注记）：以 setTabOrder
     * 单跳链接把确定/取消围成显式闭环，Tab/Shift+Tab 在两钮间环绕，
     * 不被 XWidget 的顶层窗口全域 Tab 兜底送出模态子树（夜间台账
     * #23/#24）。 */
    if (ok && cancel) {
        XWidget_setTabOrder((XWidget*)ok, (XWidget*)cancel);
        XWidget_setTabOrder((XWidget*)cancel, (XWidget*)ok);
    }
    XBoxLayout_addLayout(root, (XLayout*)bar);
    *outBar = bar;
}

/** @brief 初始焦点落到输入控件（对标 Qt 6.8 qinputdialog.cpp
 *  QInputDialog::setVisible：显示即 d->inputWidget->setFocus()，行
 *  编辑/自旋框同时 selectAll）。此前 exec 后初始焦点被
 *  dialog_grabInitialFocus 抢到默认按钮「确定」上，弹出后直接打字
 *  无效（夜间台账 #26）。exec 内 grabInitialFocus 见焦点已在对话
 *  框子树内即不抢占（dialog_containsFocus 门禁），故此处先聚焦。 */
static void xid_focusInputWidget(XInputDialog* dlg)
{
    XWidget* input = NULL;
    if (!dlg) return;
    switch (XInputDialog_inputMode(dlg)) {
    case XInputDialog_IntInput:
        input = xid_childByName(&dlg->m_base, XID_NAME_SPIN);
        break;
    case XInputDialog_DoubleInput:
        input = xid_childByName(&dlg->m_base, XID_NAME_EDIT);
        break;
    case XInputDialog_ComboBoxInput:
        input = xid_childByName(&dlg->m_base, XID_NAME_COMBO);
        break;
    case XInputDialog_TextInput:
    default: {
        XLineEdit* edit =
            (XLineEdit*)xid_childByName(&dlg->m_base, XID_NAME_EDIT);
        if (edit) {
            /* 对标 Qt：文本输入聚焦即全选预置文本，键入直接替换。 */
            XLineEdit_selectAll(edit);
            input = (XWidget*)edit;
        } else {
            input = xid_childByName(&dlg->m_base, XID_NAME_PLAIN);
        }
        break;
    }
    }
    if (input)
        XWidget_setFocusReason(input, XFocusReason_Other);
}

/** @brief 阻塞模态执行：定尺寸、主屏居中、exec（复用 XDialog 阻塞
 *  循环：应用模态 + Escape→reject）。返回是否接受。 */
static bool xid_execDialog(XInputDialog* dlg, int w, int h)
{
    int rc;
    if (!dlg) return false;
    XWidget_resize((XWidget*)dlg, w, h);
    xid_centerOnScreen((XWidget*)dlg);
    xid_focusInputWidget(dlg);
    rc = XDialog_exec(&dlg->m_base);
    return rc == 1; /* 对标 QDialog::Accepted。 */
}

/** @brief 收尾：先删布局（不随控件析构）再删对话框（子控件随对象树
 *  递归销毁，对标 Qt 父子所有权）。 */
static void xid_teardown(XInputDialog* dlg, XBoxLayout* root, XBoxLayout* bar)
{
    if (root) XLayout_delete_base((XLayout*)root);
    if (bar) XLayout_delete_base((XLayout*)bar);
    if (dlg) XInputDialog_delete_base((XClass*)dlg);
}

static void xid_acceptSlot(XObject* receiver, XVarList* args)
{
    XInputDialog* dlg = (XInputDialog*)receiver;
    (void)args;
    if (!dlg) return;
    /* P1-R26：Qt 语义下 *ValueSelected 由 done(Accepted) 发射（发射
     * 于 QDialog::done 之前）。本实现的 accept 收口在本槽：各分支
     * 结算完输入控件当前值后随即发射对应确认信号，再走
     * XDialog_accept（其内部发 finished/accepted）。 */
    switch (XInputDialog_inputMode(dlg)) {
    case XInputDialog_IntInput: {
        XSpinBox* spin =
            (XSpinBox*)xid_childByName(&dlg->m_base, XID_NAME_SPIN);
        if (spin) {
            int v = XSpinBox_value(spin);
            XInputDialog_setIntValue(dlg, v);
            XInputDialog_intValueSelected_signal(dlg, v);
        }
        break;
    }
    case XInputDialog_DoubleInput: {
        /* 浮点输入用行编辑承载（显示真实小数文本），accept 时解析并
         * 钳位到当前范围（对标 QInputDialog double 输入结算）。 */
        XLineEdit* edit =
            (XLineEdit*)xid_childByName(&dlg->m_base, XID_NAME_EDIT);
        if (edit) {
            const char* txt = XLineEdit_text(edit);
            char* end = NULL;
            double v = txt ? strtod(txt, &end) : dlg->m_doubleValue;
            if (!txt || end == txt) v = dlg->m_doubleValue;
            if (v < dlg->m_doubleMinimum) v = dlg->m_doubleMinimum;
            if (v > dlg->m_doubleMaximum) v = dlg->m_doubleMaximum;
            XInputDialog_setDoubleValue(dlg, v);
            XInputDialog_doubleValueSelected_signal(dlg, v);
        }
        break;
    }
    case XInputDialog_ComboBoxInput: {
        XComboBox* combo =
            (XComboBox*)xid_childByName(&dlg->m_base, XID_NAME_COMBO);
        if (combo) {
            XString* t = XComboBox_currentText(combo);
            if (t) {
                /* 对标 Qt：下拉值结算走 textValue 通道，确认信号为
                 * textValueSelected（载荷=当前选中项文本）。 */
                XInputDialog_setTextValue(dlg, t);
                XInputDialog_textValueSelected_signal(dlg, t);
                XString_delete_base((XClass*)t);
            }
        }
        break;
    }
    case XInputDialog_TextInput:
    default: {
        XLineEdit* edit =
            (XLineEdit*)xid_childByName(&dlg->m_base, XID_NAME_EDIT);
        XString* v = NULL;
        if (edit) {
            v = XString_create_utf8(XLineEdit_text(edit));
        } else {
            /* getMultiLineText 的多行编辑承载（XID_NAME_PLAIN）：
             * 对齐 Qt——输入控件值在 done(Accepted) 时点结算并发射
             * 确认信号，不留到 exec 返回后补采（那时确认信号已错
             * 过）。exec 返回后的同值回填因 setter 相等短路而幂等。 */
            XPlainTextEdit* plain = (XPlainTextEdit*)xid_childByName(
                &dlg->m_base, XID_NAME_PLAIN);
            if (plain) {
                char* buf = XPlainTextEdit_toPlainText(plain);
                if (buf) {
                    v = XString_create_utf8(buf);
                    XFree_System(buf);
                }
            }
        }
        if (v) {
            XInputDialog_setTextValue(dlg, v);
            XInputDialog_textValueSelected_signal(dlg, v);
            XString_delete_base((XClass*)v);
        }
        break;
    }
    }
    XDialog_accept(&dlg->m_base);
}

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
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    XString* result;
    bool accepted = false;
    if (ok) *ok = false;
    if (!xid_guiReady()) {
        /* 无 GUI 环境（无头测试）：返回空串，*ok=false（桩约定）。 */
        dlg = xinputdialog_tempSetup(parent, title, label);
        if (!dlg) return XString_create();
        XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
        XInputDialog_setTextValue(dlg, text);
        result = XString_create();
        XInputDialog_delete_base(dlg);
        return result;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
    XInputDialog_setTextEchoMode(dlg, echo);
    {
        XLineEdit* edit = XLineEdit_create((XWidget*)dlg, 0);
        if (edit) {
            XLineEdit_setEchoMode(edit, (int)echo);
            if (text)
                XLineEdit_setText(edit, XString_toUtf8(text));
            XWidget_setMinimumSize((XWidget*)edit, 220, 24);
            /* 对标 Qt 对话框私有子对象命名（xid_childByName 依赖）：
               此前漏登记 XID_NAME_EDIT，accept 结算 findChild 落空，
               m_textValue 保持 NULL，getText 回传空串而编辑框所见为
               「预置文本 …」（夜间台账 #25 所见非所回根因）。 */
            xid_setName((XObject*)edit, XID_NAME_EDIT);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)edit);
        }
    }
    xid_addButtons(dlg, root, &bar);
    accepted = xid_execDialog(dlg, 360, 140);
    if (ok) *ok = accepted;
    result = XInputDialog_textValue(dlg);
    xid_teardown(dlg, root, bar);
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
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    XString* result;
    bool accepted = false;
    if (ok) *ok = false;
    if (!xid_guiReady()) {
        /* 无 GUI 环境（无头测试）：返回空串，*ok=false（桩约定）。 */
        dlg = xinputdialog_tempSetup(parent, title, label);
        if (!dlg) return XString_create();
        XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
        XInputDialog_setTextValue(dlg, text);
        result = XString_create();
        XInputDialog_delete_base(dlg);
        return result;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_TextInput);
    {
        XPlainTextEdit* plain = XPlainTextEdit_create((XWidget*)dlg, 0);
        if (plain) {
            if (text)
                XPlainTextEdit_setPlainText(plain, XString_toUtf8(text));
            XWidget_setMinimumSize((XWidget*)plain, 260, 120);
            /* 对标 Qt 私有子对象命名：accept 结算经 findChild 取多行
               编辑当前文本（同 getText 的 #25 根因）。 */
            xid_setName((XObject*)plain, XID_NAME_PLAIN);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)plain);
        }
    }
    xid_addButtons(dlg, root, &bar);
    accepted = xid_execDialog(dlg, 400, 260);
    if (ok) *ok = accepted;
    {
        /* 多行编辑 harvest：toPlainText 返回堆缓冲，取值后按系统堆释放。 */
        XPlainTextEdit* plain =
            (XPlainTextEdit*)xid_childByName(&dlg->m_base, XID_NAME_PLAIN);
        if (plain) {
            char* buf = XPlainTextEdit_toPlainText(plain);
            if (buf) {
                XString* v = XString_create_utf8(buf);
                if (v) {
                    XInputDialog_setTextValue(dlg, v);
                    XString_delete_base((XClass*)v);
                }
                XFree_System(buf);
            }
        }
        result = XInputDialog_textValue(dlg);
    }
    xid_teardown(dlg, root, bar);
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
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    int result;
    bool accepted = false;
    if (ok) *ok = false;
    if (!xid_guiReady()) {
        /* 无 GUI 环境（无头测试）：返回入参 value，*ok=false（桩约定）。 */
        dlg = xinputdialog_tempSetup(parent, title, label);
        if (!dlg) return value;
        XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
        XInputDialog_setIntValue(dlg, value);
        XInputDialog_delete_base(dlg);
        return value;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return value;
    XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
    {
        XSpinBox* spin = XSpinBox_create((XWidget*)dlg, 0);
        if (spin) {
            if (minValue < maxValue)
                XSpinBox_setRange(spin, minValue, maxValue);
            XSpinBox_setSingleStep(spin, step > 0 ? step : 1);
            XSpinBox_setValue(spin, value);
            XWidget_setMinimumSize((XWidget*)spin, 160, 24);
            /* 对标 Qt 私有子对象命名：accept 结算经 findChild 取自旋
               框当前值（同 getText 的 #25 根因，getInt 此前确认后恒
               回初值）。 */
            xid_setName((XObject*)spin, XID_NAME_SPIN);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)spin);
        }
    }
    xid_addButtons(dlg, root, &bar);
    accepted = xid_execDialog(dlg, 340, 140);
    if (ok) *ok = accepted;
    result = XInputDialog_intValue(dlg);
    xid_teardown(dlg, root, bar);
    return result;
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
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    double result;
    bool accepted = false;
    if (ok) *ok = false;
    if (!xid_guiReady()) {
        /* 无 GUI 环境（无头测试）：返回入参 value，*ok=false（桩约定）。 */
        dlg = xinputdialog_tempSetup(parent, title, label);
        if (!dlg) return value;
        XInputDialog_setInputMode(dlg, XInputDialog_DoubleInput);
        XInputDialog_setDoubleValue(dlg, value);
        XInputDialog_delete_base(dlg);
        return value;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return value;
    XInputDialog_setInputMode(dlg, XInputDialog_DoubleInput);
    {
        /* 浮点输入用行编辑（XSpinBox 为整数值域，无法按 decimals 显示
         * 小数）；初值按 decimals 位小数文本化，accept 时解析钳位。 */
        XLineEdit* edit = XLineEdit_create((XWidget*)dlg, 0);
        if (edit) {
            char buf[64];
            int dec = decimals < 0 ? 6 : (decimals > 10 ? 10 : decimals);
            snprintf(buf, sizeof(buf), "%.*f", dec, value);
            XLineEdit_setText(edit, buf);
            XWidget_setMinimumSize((XWidget*)edit, 220, 24);
            /* 对标 Qt 私有子对象命名：accept 结算经 findChild 解析行
               编辑当前文本（同 getText 的 #25 根因，此前确认后恒回
               初值）。 */
            xid_setName((XObject*)edit, XID_NAME_EDIT);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)edit);
        }
    }
    dlg->m_doubleMinimum = minValue;
    dlg->m_doubleMaximum = maxValue;
    XInputDialog_setDoubleValue(dlg, value);
    xid_addButtons(dlg, root, &bar);
    accepted = xid_execDialog(dlg, 360, 140);
    if (ok) *ok = accepted;
    result = XInputDialog_doubleValue(dlg);
    xid_teardown(dlg, root, bar);
    return result;
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
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    XString* result;
    bool accepted = false;
    if (ok) *ok = false;
    if (!xid_guiReady()) {
        /* 无 GUI 环境（无头测试）：返回 items[current]（越界空串），
         * *ok=false（桩约定）。 */
        XString* item = NULL;
        dlg = xinputdialog_tempSetup(parent, title, label);
        if (!dlg) return XString_create();
        XInputDialog_setInputMode(dlg, XInputDialog_ComboBoxInput);
        XInputDialog_setComboBoxItems(dlg, items);
        if (items && current >= 0) {
            int64_t n = XStringList_size_base((const XContainer*)items);
            if ((int64_t)current < n)
                item = (XString*)(void*)XStringList_at_base(
                    (const XVector*)items, current);
        }
        result = item ? XString_create_copy(item) : XString_create();
        XInputDialog_delete_base(dlg);
        return result;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return XString_create();
    XInputDialog_setInputMode(dlg, XInputDialog_ComboBoxInput);
    {
        XComboBox* combo = XComboBox_create((XWidget*)dlg, 0);
        if (combo) {
            XComboBox_setEditable(combo, editable);
            if (items) {
                int64_t i, n =
                    XStringList_size_base((const XContainer*)items);
                for (i = 0; i < n; ++i) {
                    XString* item = (XString*)(void*)XStringList_at_base(
                        (const XVector*)items, i);
                    if (item)
                        XComboBox_insertItem((XComboBox*)combo, (int)i, item);
                }
            }
            if (current > 0)
                XComboBox_setCurrentIndex(combo, current);
            XWidget_setMinimumSize((XWidget*)combo, 200, 26);
            /* 对标 Qt 私有子对象命名：accept 结算经 findChild 取下拉
               当前项文本（同 getText 的 #25 根因）。 */
            xid_setName((XObject*)combo, XID_NAME_COMBO);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)combo);
        }
    }
    xid_addButtons(dlg, root, &bar);
    accepted = xid_execDialog(dlg, 360, 150);
    if (ok) *ok = accepted;
    result = XInputDialog_textValue(dlg);
    xid_teardown(dlg, root, bar);
    return result;
}

XString* XInputDialog_getItem_2(XWidget* parent, const char* title,
                                const char* label, const char* const* items,
                                int count, int current, bool editable, bool* ok)
{
    XStringList* list;
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    XString* result;
    int i;
    if (ok) *ok = false;
    list = XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!list) {
        if (t) XString_delete_base((XClass*)t);
        if (l) XString_delete_base((XClass*)l);
        return XString_create();
    }
    for (i = 0; i < count; ++i) {
        if (items && items[i])
            XStringList_push_back_utf8(list, items[i]);
        else
            XStringList_push_back_utf8(list, "");
    }
    /* 对标 Qt：UTF-8 重载与 XString 重载等价（修正此前标题/标签丢失）。 */
    result = XInputDialog_getItem(parent, t, l, list, current, editable, ok);
    XStringList_delete_base((XClass*)list);
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
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

/* P1-R26：*ValueSelected 与 *Changed 是两个独立信号（对标 Qt
 * QInputDialog：textValueSelected 等仅由 done(Accepted) 路径发射，
 * 与 textValueChanged/intValueChanged/doubleValueChanged 无任何联
 * 动）。信号函数只发射自身，不再交叉调用 *Changed 信号函数（修复
 * 前手动调确认信号会连带真发射对应 *Changed）。 */

void* XInputDialog_textValueSelected_signal(XInputDialog* self,
                                            const XString* text)
{
    if (!self) return (void*)(size_t)XInputDialog_textValueSelected_signal;
    xinputdialog_emitString(self,
                            (size_t)XInputDialog_textValueSelected_signal,
                            text);
    return (void*)(size_t)XInputDialog_textValueSelected_signal;
}

void* XInputDialog_intValueSelected_signal(XInputDialog* self, int value)
{
    if (!self) return (void*)(size_t)XInputDialog_intValueSelected_signal;
    xinputdialog_emitInt(self,
                         (size_t)XInputDialog_intValueSelected_signal,
                         value);
    return (void*)(size_t)XInputDialog_intValueSelected_signal;
}

void* XInputDialog_doubleValueSelected_signal(XInputDialog* self,
                                              double value)
{
    if (!self) return (void*)(size_t)XInputDialog_doubleValueSelected_signal;
    xinputdialog_emitDouble(self,
                            (size_t)XInputDialog_doubleValueSelected_signal,
                            value);
    return (void*)(size_t)XInputDialog_doubleValueSelected_signal;
}

void* XInputDialog_comboBoxTextValueSelected_signal(XInputDialog* self,
                                                    const XString* text)
{
    if (!self)
        return (void*)(size_t)XInputDialog_comboBoxTextValueSelected_signal;
    /* 只发射确认信号自身；不再借道 comboBoxTextChanged_signal
     * （其兼有 m_comboBoxText 属性存储副作用，属 *Changed 通道）。 */
    xinputdialog_emitString(
        self, (size_t)XInputDialog_comboBoxTextValueSelected_signal, text);
    return (void*)(size_t)XInputDialog_comboBoxTextValueSelected_signal;
}

#endif /* XWIDGET_ON && XDIALOG_ON */