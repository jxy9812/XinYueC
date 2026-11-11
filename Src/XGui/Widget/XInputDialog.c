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

#if XGUI_ON
#include "XInputDialog.h"
#include "XString.h"
#include "XStringList.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"
/* 真实弹窗依赖（对标 Qt 静态便捷函数的对话框组装路径）： */
#include "XCoreApplication.h"  /* qApp 等价物：有应用实例才允许模态循环 */
#include "XGuiApplication.h"   /* 主屏查询（弹窗居中） */
#include "XScreen.h"           /* 屏幕几何 */
#include "XLabel.h"            /* 提示标签 */
#include "XFont.h"             /* 标签行高钉底（字模像素字号） */
#include "XFont8x16.h"         /* 行高兜底常量 */
#include "XLineEdit.h"         /* 文本/浮点输入 */
#include "XSpinBox.h"          /* 整数输入 */
#include "XComboBox.h"         /* 下拉选择 */
#include "XPushButton.h"       /* OK/Cancel */
#include "XPlainTextEdit.h"    /* 多行文本输入 */
#include "XBoxLayout.h"        /* 对话框布局 */
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"  /* 实例布局的标准按钮行（对标 QDialogButtonBox，
                                * qinputdialog.cpp:208-215）。 */
#endif
#include "XLayoutItem_Protected.h" /* XLayoutItem_widget_base 声明：缺
                                    * 它时隐式 int 返回 + cltq 截断
                                    * 64 位控件指针（r1#4/#10 回归锁
                                    * 首跑 SIGSEGV 实锚，2026-10-01） */

#if XWIDGET_ON && XDIALOG_ON

#include "XWidget_Protected.h"
#include "XObject.h"          /* XObject_deleteLater/XObject_children：
                               * 取消崩溃根修（2026-10-21，XFileDialog
                               * xff_teardown 同款移植）延迟回收与子树
                               * 输入侧摘除依赖。 */
#include "XVector.h"          /* XVector_size_base/at_base：子树遍历。 */

/* ==================== 内部辅助 ==================== */

/* ---------- 取消崩溃根修（2026-10-21，XFileDialog 同款移植） ----------
 * 静态便捷路径的收尾不得在取消/确定槽尚未返回、输入派发仍以本对话框
 * 子树为靶的栈窗口内同步删除任何子树对象——XFileDialog 侧 ASan 实抓：
 * 取消钮在收尾同步删除后，控制权带着事件循环回到派发层，下一记落在
 * 对话框区域的鼠标事件（释放/移动）经 g_buttonDownWidget 按压靶派发
 * 到已释放按钮，XWidget_accumulateOffset 读已释放内存 UAF（Windows
 * MSVC 消息时序必现）。两条纪律：
 *  1. 收口槽（确定/取消）前置子树输入侧摘除（xid_detachInputRecursive：
 *     子树逐控件显式隐藏，隐藏分支清焦点/抓取/按压靶/触点表登记）；
 *  2. 对话框子树走 XObject_deleteLater（DeferredDelete 的 loopLevel
 *     语义保证析构发生在控制权返回事件循环之后），布局句柄（root/bar，
 *     addLayout 子布局不归父布局所有，对标 QLayout 所有权语义）经
 *     下方登记表挂到 VXInputDialog_deinit 析构帧兑现（析构级联第一个
 *     子类帧，此刻子控件仍存活，根布局先行删除清子行 m_parentLayout
 *     反指针后再删子行，删除序安全）。
 * 串行不变量：便捷路径同一时刻至多一个模态对话框，登记表单槽即可；
 * owner 指针校验保证非本对话框的析构不误回收他人句柄。
 * open_2 复用路径不受影响：收口槽摘除以 !m_openReceiver 守卫——open_2
 * 的对话框 done 后仍存活可复开，子树显式隐藏会使其复开呈现空面板。 */

/** @brief 子树输入侧登记摘除：先子后父逐控件显式隐藏（隐藏分支在
 *  各自 oldVisible=1 现场执行，清按钮按压靶/焦点/抓取/触点表）。
 *  须在父对话框尚未隐藏时调用；跳过子窗（弹层另行收口）。 */
static void xid_detachInputRecursive(XWidget* w)
{
    /* [根修 2026-10-27] 原实现"逐控件 setVisible(false)"会把子控件永久
     * 标记为显式隐藏（WA_WState_Hidden），对话框复用时 showChildren 跳过
     * 它们 → 复开空面板（XMessageBox 二次 open 用例实证）。真正需要的
     * 只是"清事件层指向子树的输入侧登记"，与显隐状态无关——改用框架
     * 新增的 XWidget_deactivateInputRegistrations（与析构路径同一份清单，
     * 对标 Qt QWidget::destroy → deactivateWidgetCleanup）。 */
    XWidget_deactivateInputRegistrations(w);
}

/** @brief 排空后回收登记表（单槽 + owner 校验，见上方块注）。 */
static XInputDialog* s_xidPendingDlg = NULL;
static XBoxLayout* s_xidPendingLayouts[2] = {0}; /**< [0]=root，[1]=bar。 */

/** @brief 对话框析构帧兑现登记：回收布局句柄（根布局先行清子行
 *  反指针，再删子行；见上方块注删除序）。仅当登记 owner 即本次
 *  析构对话框时兑现（VXInputDialog_deinit 调用）。 */
static void xid_pendingReclaim(XInputDialog* dying)
{
    int i;
    if (!dying || s_xidPendingDlg != dying) return;
    for (i = 0; i < 2; ++i) {
        if (s_xidPendingLayouts[i])
            XClassDelete((XLayout*)s_xidPendingLayouts[i]);
        s_xidPendingLayouts[i] = NULL;
    }
    s_xidPendingDlg = NULL;
}

/** @brief teardown 登记：布局句柄移交登记表随对话框析构帧回收
 *  （时序与安全性见上方块注）。覆写前先回收旧登记（同槽连续两次
 *  便捷调用、前一 DeferredDelete 尚未兑现时防其句柄泄漏——旧登记
 *  对话框此刻仍存活且已隐藏，立即删其布局句柄安全：删除仅清反
 *  指针，其析构帧读到 NULL 空转）。 */
static void xid_pendingRegister(XInputDialog* dlg, XBoxLayout* root,
                                XBoxLayout* bar)
{
    if (!dlg) return;
    if (s_xidPendingDlg && s_xidPendingDlg != dlg)
        xid_pendingReclaim(s_xidPendingDlg);
    s_xidPendingDlg = dlg;
    s_xidPendingLayouts[0] = root;
    s_xidPendingLayouts[1] = bar;
}

/** @brief 释放并清空拥有型 XString 字段。 */
static void xinputdialog_freeString(XString** slot)
{
    if (slot && *slot) {
        XClassDelete((XClass*)*slot);
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
        XClassDelete((XClass*)text);
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
        XClassDelete((XClass*)copy);
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

/* ==================== 子控件命名/查找 ==================== */

/** @brief 以 UTF-8 设置对象 objectName（对标 QObject::setObjectName）。 */
static void xid_setName(XObject* obj, const char* name)
{
    XString tmp;
    if (!obj) return;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    XObject_setObjectName(obj, &tmp);
    XClassDeinit((XClass*)&tmp);
}

/* 子控件 objectName 常量（对标 Qt 对话框私有子对象命名；槽内经
 * findChild 取回，避免 C 语言的 d-pointer 方案）。实例懒建与静态
 * 便捷路径共用同一批名字（accept 结算按名字回落）。 */
#define XID_NAME_EDIT  "qt_input_dialog_edit"
#define XID_NAME_SPIN  "qt_input_dialog_spin"
#define XID_NAME_COMBO "qt_input_dialog_combo"
#define XID_NAME_PLAIN "qt_input_dialog_plain"
#define XID_NAME_OK    "qt_input_dialog_ok"
#define XID_NAME_CANCEL "qt_input_dialog_cancel"
/* 实例懒建布局的标签/按钮行（Qt 侧无 objectName；本库给名便于
 * findChild 取回与测试断言，语义同 Qt 子对象定位）。 */
#define XID_NAME_LABEL "qt_input_dialog_label"
#define XID_NAME_BUTTONS "qt_input_dialog_buttons"

/** @brief 按 objectName 查找对话框直接子控件（对标 QObject::findChild）。 */
static XWidget* xid_childByName(XDialog* dlg, const char* name)
{
    XString tmp;
    XWidget* w;
    if (!dlg) return NULL;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    w = (XWidget*)XObject_findChild((XObject*)dlg, &tmp,
                                    XFindChildOption_DirectChildrenOnly);
    XClassDeinit((XClass*)&tmp);
    return w;
}

/* ==================== 类与实例生命周期 ==================== */

static void VXInputDialog_showEvent(XWidget* self, XEvent* event);
static void VXInputDialog_setVisible(XWidget* self, bool visible);
static void VXInputDialog_closeEvent(XWidget* self, XEvent* event);
static void xid_openCleanup(XInputDialog* dlg);
/* 实例级懒建布局（对标 qinputdialog.cpp:190-240；实现见静态便捷函数
 * 区块之后，setter 在文件前段即需调用）。 */
static void xid_ensureLayout(XInputDialog* self);
static void xid_applyInputMode(XInputDialog* self);
static void xid_syncInputWidget(XInputDialog* self);
static void xid_settleFromInputWidget(XInputDialog* self);
static void xid_applyNoButtons(XInputDialog* self);

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
        XClassDelete((XClass*)self->m_comboBoxItems);
        self->m_comboBoxItems = NULL;
    }
#if XWIDGET_ON && XDIALOG_ON
    /* 排空后回收：静态便捷路径收尾登记的布局句柄随本对话框析构帧
     * 兑现（取消崩溃根修；登记/删除序与 owner 校验见文件头块注）。
     * owner 非本对话框（无登记/他人登记）时空转。 */
    xid_pendingReclaim(self);
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XInputDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XInputDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXInputDialog_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SetVisible, VXInputDialog_setVisible);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXInputDialog_closeEvent);
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
    self->m_suppressAutoFocus = 0;
    self->m_options = 0;
    /* 标签缺省 "Enter a value:"（对标 qinputdialog.cpp:203-207
     * ensureLayout 建 QLabel 的缺省文本；labelText() 一旦取用即触发
     * ensureLayout，故本机 Qt 探针 Z0 构造后立刻读到该串）。 */
    self->m_labelText = XString_create_utf8("Enter a value:");
    self->m_textValue = NULL;
    self->m_comboBoxText = NULL;
    self->m_comboBoxItems = NULL;
    self->m_comboBoxEditable = false;
    self->m_echoMode = XInputDialogEchoMode_Normal;
    self->m_intValue = 0;
    self->m_doubleValue = 0.0;
    self->m_okButtonText = NULL;
    self->m_cancelButtonText = NULL;
    /* 默认范围按 Qt 6.8 口径（qinputdialog.cpp:786-830/894-940：未建
     * 自旋框时 intMinimum()/intMaximum() 返回 QSpinBox 缺省 0/99，
     * doubleMinimum()/doubleMaximum() 返回 QDoubleSpinBox 缺省
     * 0/99.99；本机 Qt 6.8.0 探针 I0 实测）。spawn 后范围仍由 setter
     * 维护，与 QSpinBox::setRange 的钳位/边界收敛语义同口径（见各
     * setter）。步进 1、小数位 2 与 Qt 一致。 */
    self->m_intMinimum = 0;
    self->m_intMaximum = 99;
    self->m_intStep = 1;
    self->m_doubleMinimum = 0.0;
    self->m_doubleMaximum = 99.99;
    self->m_doubleStep = 1.0;
    self->m_doubleDecimals = 2;
    self->m_openReceiver = NULL;
    self->m_openMember = NULL;
    self->m_openSignal = XInputDialog_OpenFinished;
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
{
    if (!self) return;
    self->m_inputMode = mode;
    /* 实例布局已建时换装对应输入控件（对标 QInputDialog::setInputMode，
     * qinputdialog.cpp:509-533 的 ensure*SpinBox/chooseRight 分支）；
     * 静态便捷路径无实例布局，天然早退。 */
    xid_applyInputMode(self);
}

XInputDialogInputMode XInputDialog_inputMode(const XInputDialog* self)
{ return self ? self->m_inputMode : XInputDialog_TextInput; }

void XInputDialog_setLabelText(XInputDialog* self, const XString* text)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_labelText);
    self->m_labelText = xinputdialog_dupString(text);
    /* 标签已建（实例布局）时同步文本（对标 setLabelText 的
     * `d->label->setText(text)` 分支，qinputdialog.cpp:556-567）。 */
    if (self->m_label)
        XLabel_setText((XLabel*)self->m_label, self->m_labelText);
}

XString* XInputDialog_labelText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_labelText) : XString_create();
}

void XInputDialog_setTextValue(XInputDialog* self, const XString* text)
{
    XString* copy;
    bool changed = false;
    if (!self) return;
    /* 对标 QInputDialog::setTextValue（qinputdialog.cpp:690-707）：先把
     * 输入模式切回 TextInput（Qt 由 setInputMode 选择行编辑/纯文本/
     * 下拉承载），再把值落进承载控件。 */
    self->m_inputMode = XInputDialog_TextInput;
    xid_applyInputMode(self);
    copy = xinputdialog_dupString(text);
    if (!copy) return;
    if (self->m_textValue &&
        XString_compare(self->m_textValue, copy) == 0) {
        XClassDelete((XClass*)copy);
    } else {
        xinputdialog_freeString(&self->m_textValue);
        self->m_textValue = copy;
        changed = true;
    }
    /* 值先落控件再发信号（Qt 的顺序：控件 setText → textChanged →
     * textValueChanged，槽里读控件已是新值）。 */
    xid_syncInputWidget(self);
    if (changed)
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
    bool changed;
    if (!self) return;
    /* 对标 QInputDialog::setIntValue（qinputdialog.cpp:768-773）：
     * setInputMode(IntInput) 后交给 QSpinBox::setValue——按当前范围
     * 钳位（本机 Qt 6.8 实测：默认范围 0..99 下 setIntValue(500)→99）。 */
    self->m_inputMode = XInputDialog_IntInput;
    if (value < self->m_intMinimum) value = self->m_intMinimum;
    if (value > self->m_intMaximum) value = self->m_intMaximum;
    changed = (self->m_intValue != value);
    self->m_intValue = value;
    xid_applyInputMode(self);
    xid_syncInputWidget(self);
    if (!changed) return;
    xinputdialog_emitInt(self, (size_t)XInputDialog_intValueChanged_signal,
                         value);
}

int XInputDialog_intValue(const XInputDialog* self)
{ return self ? self->m_intValue : 0; }

void XInputDialog_setDoubleValue(XInputDialog* self, double value)
{
    bool changed;
    if (!self) return;
    /* 对标 QInputDialog::setDoubleValue（qinputdialog.cpp:884-889）：
     * setInputMode(DoubleInput) 后交给 QDoubleSpinBox::setValue——按
     * 当前范围钳位（本机 Qt 6.8 实测：默认范围 0..99.99 下
     * setDoubleValue(1234.5)→99.99）。 */
    self->m_inputMode = XInputDialog_DoubleInput;
    if (value < self->m_doubleMinimum) value = self->m_doubleMinimum;
    if (value > self->m_doubleMaximum) value = self->m_doubleMaximum;
    changed = (self->m_doubleValue != value);
    self->m_doubleValue = value;
    xid_applyInputMode(self);
    xid_syncInputWidget(self);
    if (!changed) return;
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
        XString* item = (XString*)XStringList_at_base((const XVector*)items, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base(
                (XVector*)self->m_comboBoxItems, copy);
            XClassDelete((XClass*)copy);
            copy = NULL;
        }
    }
    /* 对标 setComboBoxItems（qinputdialog.cpp:733-748）：条目变化后若在
     * 文本模式，重选承载控件（有条目⇒下拉框）。 */
    if (self->m_inputMode == XInputDialog_TextInput)
        xid_applyInputMode(self);
    xid_syncInputWidget(self);
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
        XString* item = (XString*)XStringList_at_base(
            (const XVector*)self->m_comboBoxItems, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base((XVector*)out, copy);
            XClassDelete((XClass*)copy);
            copy = NULL;
        }
    }
    return out;
}

void XInputDialog_setComboBoxEditable(XInputDialog* self, bool editable)
{
    if (!self) return;
    self->m_comboBoxEditable = editable;
    /* 对标 setComboBoxEditable（qinputdialog.cpp:709-716）：ensureComboBox
     * 后 setEditable，文本模式下重选承载控件。 */
    if (self->m_inputMode == XInputDialog_TextInput)
        xid_applyInputMode(self);
    xid_syncInputWidget(self);
}

bool XInputDialog_isComboBoxEditable(const XInputDialog* self)
{ return self ? self->m_comboBoxEditable : false; }

void XInputDialog_setOkButtonText(XInputDialog* self, const XString* text)
{
    XPushButton* ok;
    if (!self) return;
    xinputdialog_freeString(&self->m_okButtonText);
    self->m_okButtonText = xinputdialog_dupString(text);
    /* 对标 setOkButtonText（qinputdialog.cpp:982-987）：ensureLayout 后
     * 直接改 buttonBox 的 Ok 按钮文本。 */
#if XDIALOGBUTTONBOX_ON
    if (self->m_buttonBox) {
        ok = XDialogButtonBox_button((XDialogButtonBox*)self->m_buttonBox,
                                     XDialogButtonBoxStandard_Ok);
        if (ok && self->m_okButtonText)
            XAbstractButton_setText_2((XAbstractButton*)ok,
                                      XString_toUtf8(self->m_okButtonText));
    }
#endif
    if (!self->m_buttonBox) {
        /* 无按钮盒构建：按私有名找直挂按钮。 */
        ok = (XPushButton*)xid_childByName(&self->m_base, XID_NAME_OK);
        if (ok && self->m_okButtonText)
            XAbstractButton_setText_2((XAbstractButton*)ok,
                                      XString_toUtf8(self->m_okButtonText));
    }
}

XString* XInputDialog_okButtonText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_okButtonText) : XString_create();
}

void XInputDialog_setCancelButtonText(XInputDialog* self, const XString* text)
{
    XPushButton* cancel;
    if (!self) return;
    xinputdialog_freeString(&self->m_cancelButtonText);
    self->m_cancelButtonText = xinputdialog_dupString(text);
#if XDIALOGBUTTONBOX_ON
    if (self->m_buttonBox) {
        cancel = XDialogButtonBox_button(
            (XDialogButtonBox*)self->m_buttonBox,
            XDialogButtonBoxStandard_Cancel);
        if (cancel && self->m_cancelButtonText)
            XAbstractButton_setText_2(
                (XAbstractButton*)cancel,
                XString_toUtf8(self->m_cancelButtonText));
    }
#endif
    if (!self->m_buttonBox) {
        cancel = (XPushButton*)xid_childByName(&self->m_base, XID_NAME_CANCEL);
        if (cancel && self->m_cancelButtonText)
            XAbstractButton_setText_2(
                (XAbstractButton*)cancel,
                XString_toUtf8(self->m_cancelButtonText));
    }
}

XString* XInputDialog_cancelButtonText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_cancelButtonText)
                : XString_create();
}

void XInputDialog_setOption(XInputDialog* self, XInputDialogOption option, bool on)
{
    XInputDialogOptions options;
    if (!self) return;
    /* 对标 QInputDialog::setOption（qinputdialog.cpp:598-601）：经
     * setOptions 统一走显隐/换控件的副作用。 */
    options = self->m_options;
    if (on) options |= (XInputDialogOptions)option;
    else options &= ~(XInputDialogOptions)option;
    XInputDialog_setOptions(self, options);
}

bool XInputDialog_testOption(const XInputDialog* self, XInputDialogOption option)
{
    return self ? (self->m_options & (XInputDialogOptions)option) != 0 : false;
}

void XInputDialog_setOptions(XInputDialog* self, XInputDialogOptions options)
{
    XInputDialogOptions changed;
    if (!self) return;
    changed = options ^ self->m_options;
    self->m_options = options;
    if (!changed) return;
    /* 对标 QInputDialog::setOptions（qinputdialog.cpp:625-641）：
     * NoButtons 控制按钮行显隐；文本模式下两个选项可能换承载控件。 */
    if (changed & (XInputDialogOptions)XInputDialog_NoButtons)
        xid_applyNoButtons(self);
    if (changed & ((XInputDialogOptions)XInputDialog_UsePlainTextEditForTextInput |
                   (XInputDialogOptions)XInputDialog_UseListViewForComboBoxItems)) {
        if (self->m_inputMode == XInputDialog_TextInput)
            xid_applyInputMode(self);
    }
}

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


/** @brief OK 槽：把内嵌控件当前值结算进对话框存储后 accept（对标 Qt
 *  QInputDialog 在 accept 前由输入控件同步 d->value 的路径）。 */
static void xid_acceptSlot(XObject* receiver, XVarList* args);

/** @brief Cancel 槽：reject 关闭（对标 cancel 按钮触发 reject()）。 */
static void xid_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) {
        /* 取消收口（对标 QDialog::reject = done(Rejected)）前置子树
         * 输入侧摘除：取消槽运行在取消钮 clicked(Direct) 发射帧内
         * （按压靶此刻正指向取消钮），对话框延迟回收后事件层不得再
         * 有登记指向子树成员（见文件头取消崩溃根修块注）。open_2
         * 复用路径跳过摘除：对话框 done 后仍存活可复开，子树显式
         * 隐藏会使复开呈现空面板。 */
        if (!((XInputDialog*)receiver)->m_openReceiver)
            xid_detachInputRecursive(
                &((XInputDialog*)receiver)->m_base.m_base);
        XDialog_reject((XDialog*)receiver);
    }
    /* open_2 连接收口：reject→done 发射 finished 时连接仍在（receiver
     * 恰回调一次），收口后断开，防连接与记录跨关闭存活（XMessageBox
     * 按钮点击路径同款）。 */
    xid_openCleanup((XInputDialog*)receiver);
}

/* ==================== 实例级懒建输入控件与布局 ====================
 * 对标 QInputDialogPrivate::ensureLayout/ensure*（qinputdialog.cpp:190-240）：
 * 实例路径首次 setVisible(true)（show/open）时按 inputMode 建提示标签 +
 * 输入控件 + 标准按钮行并竖排（label / inputWidget / buttonBox 三段，
 * 对应 Qt 的 QVBoxLayout 装配顺序与 con 边距），随后聚焦并全选。
 * 与 Qt 的差异（均已登记）：
 *  ① 触发点：Qt 在 labelText()/okButtonText() 等取值器与 setOptions 里
 *     也会 ensureLayout；本库只在 setVisible(true) 建布局——静态便捷
 *     路径复用同一批 setter 在**自建布局**上装配控件，取值触发懒建会
 *     与它叠两层（xid_ensureLayout 另以「已有布局即早退」兜底）。
 *  ② 浮点输入用 XLineEdit 承载（本库无 QDoubleSpinBox 等价物；静态
 *     便捷路径同款），初值按 decimals 文本化、确认时解析并钳位。
 *  ③ UseListViewForComboBoxItems 暂按下拉框呈现（无
 *     QInputDialogListView 等价物），登记为偏差。
 * 控件一律以本对话框为父（随控件树析构释放），根布局经
 * XWidget_setLayout_ref 交控件接管——实例路径不产生无人释放的布局。 */

/** @brief 显示/隐藏输入控件期间的「回灌抑制」开关：程序化写控件触发的
 *  textChanged 不得反向覆盖存储值（尤其 DoubleInput 的行编辑文本不是
 *  m_textValue 的镜像），对标 Qt 里 N/A——Qt 的 inputWidget 与存储分离
 *  且二次同步幂等。 */
static void xid_beginSync(XInputDialog* self)
{
    if (self) self->m_syncingInput = 1;
}

static void xid_endSync(XInputDialog* self)
{
    if (self) self->m_syncingInput = 0;
}

/** @brief 把文本存进 m_textValue（变化才发 textValueChanged）。
 *  对标 QInputDialogPrivate::textChanged（qinputdialog.cpp:414-422）：
 *  **不切输入模式**（用户在下拉框里改文本不该把模式改回 TextInput）。 */
static void xid_storeText(XInputDialog* self, const char* utf8)
{
    XString* copy;
    if (!self || self->m_syncingInput) return;
    copy = XString_create_utf8(utf8 ? utf8 : "");
    if (!copy) return;
    if (self->m_textValue && XString_compare(self->m_textValue, copy) == 0) {
        XClassDelete((XClass*)copy);
        return;
    }
    xinputdialog_freeString(&self->m_textValue);
    self->m_textValue = copy;
    xinputdialog_emitString(self,
                            (size_t)XInputDialog_textValueChanged_signal,
                            self->m_textValue);
}

/** @brief 行编辑/下拉框文本变化槽（载荷：const char*，借用）。
 *  @note 浮点模式时行编辑只是 QDoubleSpinBox 的等价承载，其文本**不是**
 *        textValue 通道（Qt 的 doubleSpinBox 无 textValue 联动）——忽略，
 *        否则数字文本会污染下一次文本模式的初值。 */
static void xid_editTextSlot(XObject* receiver, XVarList* args)
{
    XInputDialog* self = (XInputDialog*)receiver;
    const char* text = NULL;
    if (!self || self->m_inputMode == XInputDialog_DoubleInput) return;
    if (args) {
        XVarList_args_1(args, const char*, t);
        text = t;
    }
    xid_storeText(self, text ? text : "");
}

/** @brief 纯文本编辑变化槽（无载荷：取控件全量文本）。 */
static void xid_plainTextSlot(XObject* receiver, XVarList* args)
{
    XInputDialog* self = (XInputDialog*)receiver;
    XWidget* w;
    char* buf;
    (void)args;
    if (!self || self->m_syncingInput) return;
    w = self->m_inputWidget;
    if (!w || XClassGetVtable((XObject*)w) != XPlainTextEdit_class_init())
        w = xid_childByName(&self->m_base, XID_NAME_PLAIN);
    if (!w) return;
    buf = XPlainTextEdit_toPlainText((XPlainTextEdit*)w);
    if (!buf) return;
    xid_storeText(self, buf);
    XFree_System(buf);
}

/** @brief 自旋框值变化槽（载荷：int）。 */
static void xid_spinValueSlot(XObject* receiver, XVarList* args)
{
    XInputDialog* self = (XInputDialog*)receiver;
    int value = 0;
    if (!self || !args || self->m_syncingInput) return;
    XVarList_args_1(args, int, v);
    value = v;
    if (self->m_intValue == value) return;
    self->m_intValue = value;
    xinputdialog_emitInt(self,
                         (size_t)XInputDialog_intValueChanged_signal, value);
}

/** @brief 建/取行编辑（对标 ensureLineEdit，qinputdialog.cpp:218-232）。
 *  文本与浮点模式共用同一行编辑（浮点模式按 decimals 显示）。 */
static XLineEdit* xid_ensureLineEdit(XInputDialog* self)
{
    XLineEdit* edit = (XLineEdit*)xid_childByName(&self->m_base,
                                                  XID_NAME_EDIT);
    if (edit) return edit;
    edit = XLineEdit_create((XWidget*)self, 0);
    if (!edit) return NULL;
    xid_setName((XObject*)edit, XID_NAME_EDIT);
    XWidget_hide((XWidget*)edit);   /* 同 Qt：先隐藏，装配时再 show。 */
    XObject_connect_1((XObject*)edit, XSignal(XLineEdit_textChanged_signal),
                      (XObject*)self, xid_editTextSlot,
                      XConnectionType_Direct);
    return edit;
}

/** @brief 建/取纯文本编辑（对标 ensurePlainTextEdit，:234-247）。 */
static XPlainTextEdit* xid_ensurePlainTextEdit(XInputDialog* self)
{
    XPlainTextEdit* plain = (XPlainTextEdit*)xid_childByName(
        &self->m_base, XID_NAME_PLAIN);
    if (plain) return plain;
    plain = XPlainTextEdit_create((XWidget*)self, 0);
    if (!plain) return NULL;
    xid_setName((XObject*)plain, XID_NAME_PLAIN);
    /* 同 Qt：setLineWrapMode(NoWrap)。 */
    XPlainTextEdit_setLineWrapMode(plain, XPlainTextEditMode_NoWrap);
    XWidget_hide((XWidget*)plain);
    XObject_connect_1((XObject*)plain, XSignal(XPlainTextEdit_textChanged_signal),
                      (XObject*)self, xid_plainTextSlot,
                      XConnectionType_Direct);
    return plain;
}

/** @brief 建/取下拉框（对标 ensureComboBox，:250-270）。 */
static XComboBox* xid_ensureComboBox(XInputDialog* self)
{
    XComboBox* combo = (XComboBox*)xid_childByName(&self->m_base,
                                                   XID_NAME_COMBO);
    if (combo) return combo;
    combo = XComboBox_create((XWidget*)self, 0);
    if (!combo) return NULL;
    xid_setName((XObject*)combo, XID_NAME_COMBO);
    XWidget_hide((XWidget*)combo);
    XObject_connect_1((XObject*)combo,
                      XSignal(XComboBox_currentTextChanged_signal),
                      (XObject*)self, xid_editTextSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)combo, XSignal(XComboBox_editTextChanged_signal),
                      (XObject*)self, xid_editTextSlot,
                      XConnectionType_Direct);
    return combo;
}

/** @brief 建/取整数自旋框（对标 ensureIntSpinBox，:288-298）。 */
static XSpinBox* xid_ensureIntSpinBox(XInputDialog* self)
{
    XSpinBox* spin = (XSpinBox*)xid_childByName(&self->m_base, XID_NAME_SPIN);
    if (spin) return spin;
    spin = XSpinBox_create((XWidget*)self, 0);
    if (!spin) return NULL;
    xid_setName((XObject*)spin, XID_NAME_SPIN);
    XWidget_hide((XWidget*)spin);
    /* 先落范围/步进/值再连信号：初值回灌不得发 intValueChanged。 */
    XSpinBox_setRange(spin, self->m_intMinimum, self->m_intMaximum);
    XSpinBox_setSingleStep(spin, self->m_intStep);
    XSpinBox_setValue(spin, self->m_intValue);
    XObject_connect_1((XObject*)spin, XSignal(XSpinBox_valueChanged_signal),
                      (XObject*)self, xid_spinValueSlot,
                      XConnectionType_Direct);
    return spin;
}

/** @brief 把对话框存储态回灌到当前输入控件（幂等；回灌期间抑制反向
 *  同步）。 */
static void xid_syncInputWidget(XInputDialog* self)
{
    XWidget* w;
    if (!self || !(w = self->m_inputWidget)) return;
    xid_beginSync(self);
    if (XClassGetVtable((XObject*)w) == XSpinBox_class_init()) {
        XSpinBox_setRange((XSpinBox*)w, self->m_intMinimum,
                          self->m_intMaximum);
        XSpinBox_setSingleStep((XSpinBox*)w, self->m_intStep);
        XSpinBox_setValue((XSpinBox*)w, self->m_intValue);
    } else if (XClassGetVtable((XObject*)w) == XComboBox_class_init()) {
        XComboBox* combo = (XComboBox*)w;
        int64_t i;
        int64_t n = self->m_comboBoxItems
                        ? (int64_t)XStringList_size_base(
                              (const XContainer*)self->m_comboBoxItems)
                        : 0;
        bool same = (n == (int64_t)XComboBox_count(combo));
        for (i = 0; same && i < n; ++i) {
            const char* cur = XComboBox_itemText_2(combo, (int)i);
            XString* item = (XString*)XStringList_at_base(
                (const XVector*)self->m_comboBoxItems, i);
            const char* want = item ? XString_toUtf8(item) : NULL;
            if (!cur || !want ||
                !XString_equals_utf8(item, cur, XChar_CaseSensitive))
                same = false;
        }
        XComboBox_setEditable(combo, self->m_comboBoxEditable);
        if (!same) {
            /* 条目变化才重建（对标 setComboBoxItems 的 clear+addItems）：
             * 回灌路径不重建，避免每次同步重置当前项/收起弹层。 */
            XComboBox_clear(combo);
            for (i = 0; i < n; ++i) {
                XString* item = (XString*)XStringList_at_base(
                    (const XVector*)self->m_comboBoxItems, i);
                XComboBox_addItem(combo, item);
            }
        }
        /* 对标 setComboBoxText：有条目则选中，可编辑时可写入任意文本。 */
        if (self->m_textValue &&
            XString_size_base((const XContainer*)self->m_textValue) > 0) {
            const char* text = XString_toUtf8(self->m_textValue);
            if (text && XComboBox_findText_2(combo, text) >= 0)
                XComboBox_setCurrentText_2(combo, text);
            else if (self->m_comboBoxEditable)
                XComboBox_setEditText_2(combo, text ? text : "");
        }
    } else if (XClassGetVtable((XObject*)w) == XPlainTextEdit_class_init()) {
        /* 空 XString 的 XString_toUtf8 返回 NULL，而 setPlainText/setText
         * 拿到 NULL 会整段跳过（等于不清旧文本）——统一兜底空串。 */
        const char* t = self->m_textValue
                            ? XString_toUtf8(self->m_textValue) : NULL;
        XPlainTextEdit_setPlainText((XPlainTextEdit*)w, t ? t : "");
    } else if (XClassGetVtable((XObject*)w) == XLineEdit_class_init()) {
        XLineEdit* edit = (XLineEdit*)w;
        XLineEdit_setEchoMode(edit, (int)self->m_echoMode);
        if (self->m_inputMode == XInputDialog_DoubleInput) {
            /* 浮点承载：按 decimals 文本化（静态便捷路径同款口径）。 */
            int dec = self->m_doubleDecimals;
            XString* txt;
            if (dec < 0) dec = 0;
            if (dec > 10) dec = 10;
            txt = XString_create_fmt_utf8("%.*f", dec, self->m_doubleValue);
            XLineEdit_setText(edit, txt ? XString_toUtf8(txt) : "");
            if (txt) XClassDelete((XClass*)txt);
        } else {
            const char* t = self->m_textValue
                                ? XString_toUtf8(self->m_textValue) : NULL;
            XLineEdit_setText(edit, t ? t : "");
        }
    }
    xid_endSync(self);
}

/** @brief 文本模式的承载控件选择（对标 chooseRightTextInputWidget，
 *  qinputdialog.cpp:361-390：有下拉条目→下拉框；UsePlainTextEdit→纯
 *  文本；否则行编辑）。 */
static XWidget* xid_chooseTextInputWidget(XInputDialog* self)
{
    bool hasItems = self->m_comboBoxItems &&
                    XStringList_size_base((const XContainer*)
                                          self->m_comboBoxItems) > 0;
    if (hasItems)
        return (XWidget*)xid_ensureComboBox(self);
    if (self->m_options &
        (XInputDialogOptions)XInputDialog_UsePlainTextEditForTextInput)
        return (XWidget*)xid_ensurePlainTextEdit(self);
    return (XWidget*)xid_ensureLineEdit(self);
}

/** @brief 按当前 inputMode 建/取应有的输入控件（不装配，见
 *  xid_setInputWidget）。 */
static XWidget* xid_createInputWidget(XInputDialog* self)
{
    switch (self->m_inputMode) {
    case XInputDialog_IntInput:
        return (XWidget*)xid_ensureIntSpinBox(self);
    case XInputDialog_DoubleInput:
        /* 无 QDoubleSpinBox 等价物：行编辑承载（见上方差异②）。 */
        return (XWidget*)xid_ensureLineEdit(self);
    case XInputDialog_ComboBoxInput:
        return (XWidget*)xid_ensureComboBox(self);
    case XInputDialog_TextInput:
    default:
        return xid_chooseTextInputWidget(self);
    }
}

/** @brief 在既有布局里换输入控件（对标 setInputWidget，:319-354：
 *  移除旧控件并隐藏、在索引 1 插入新控件并显示，随后把存储态回灌）。 */
static void xid_setInputWidget(XInputDialog* self, XWidget* w)
{
    XWidget* old;
    if (!self || !w) return;
    old = self->m_inputWidget;
    if (old != w) {
        if (self->m_mainLayout && old) {
            XLayout_removeWidget(self->m_mainLayout, old);
            XWidget_hide(old);
        }
        if (self->m_mainLayout)
            XBoxLayout_insertWidget((XBoxLayout*)self->m_mainLayout, 1, w, 0);
        XWidget_show(w);
        self->m_inputWidget = w;
        if (self->m_mainLayout)
            XLayout_invalidate(self->m_mainLayout);
    }
    xid_syncInputWidget(self);
}

/** @brief 模式切换后的控件装配（仅实例布局已建时生效；静态便捷路径
 *  自带控件，不得插入）。 */
static void xid_applyInputMode(XInputDialog* self)
{
    if (!self || !self->m_mainLayout) return;
    xid_setInputWidget(self, xid_createInputWidget(self));
}

/** @brief 按钮行显隐（对标 ensureLayout/setOptions 的
 *  `buttonBox->setVisible(!(opts & NoButtons))`）。无按钮盒构建时
 *  按私有名收盘（按钮直挂根布局）。 */
static void xid_applyNoButtons(XInputDialog* self)
{
    bool show;
    if (!self) return;
    show = (self->m_options & (XInputDialogOptions)XInputDialog_NoButtons)
               == 0;
    if (self->m_buttonBox) {
        XWidget_setVisible(self->m_buttonBox, show);
        return;
    }
    {
        XWidget* ok = xid_childByName(&self->m_base, XID_NAME_OK);
        XWidget* cancel = xid_childByName(&self->m_base, XID_NAME_CANCEL);
        if (ok) XWidget_setVisible(ok, show);
        if (cancel) XWidget_setVisible(cancel, show);
    }
}

/** @brief 建实例布局（对标 ensureLayout，qinputdialog.cpp:196-240）。
 *  幂等：已有实例布局返回；**静态便捷路径自建布局时直接早退**（见
 *  上方差异①）。 */
static void xid_ensureLayout(XInputDialog* self)
{
    XBoxLayout* root;
    XWidget* input;
    if (!self) return;
    if (self->m_mainLayout) return;
    if (XWidget_layout((XWidget*)self) != NULL) return;

    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, NULL);
    if (!root) return;   /* 建不了根布局：一个子控件都不建（重试幂等）。 */
    input = xid_createInputWidget(self);
    if (!self->m_label) {
        XLabel* lb = XLabel_create((XWidget*)self, 0);
        if (lb) {
            if (self->m_labelText)
                XLabel_setText(lb, self->m_labelText);
            xid_setName((XObject*)lb, XID_NAME_LABEL);
            self->m_label = (XWidget*)lb;
        }
    }
    /* 标准按钮行（对标 `new QDialogButtonBox(Ok|Cancel, Horizontal)`，
     * 且 accepted→accept / rejected→reject）。 */
#if XDIALOGBUTTONBOX_ON
    {
        XDialogButtonBox* box = XDialogButtonBox_create((XWidget*)self, 0);
        if (box) {
            XPushButton* ok = XDialogButtonBox_addButton_3(
                box, XDialogButtonBoxStandard_Ok);
            XPushButton* cancel = XDialogButtonBox_addButton_3(
                box, XDialogButtonBoxStandard_Cancel);
            /* 盒控件无自报 sizeHint：给布局一个行高/行宽下限
             * （XColorDialog 同款口径：两钮 80 宽 + 间距/边距）。 */
            XWidget_setMinimumSize((XWidget*)box, 176, 34);
            xid_setName((XObject*)box, XID_NAME_BUTTONS);
            if (ok && self->m_okButtonText)
                XAbstractButton_setText_2((XAbstractButton*)ok,
                                          XString_toUtf8(
                                              self->m_okButtonText));
            if (cancel && self->m_cancelButtonText)
                XAbstractButton_setText_2((XAbstractButton*)cancel,
                                          XString_toUtf8(
                                              self->m_cancelButtonText));
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_accepted_signal),
                              (XObject*)self, xid_acceptSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_rejected_signal),
                              (XObject*)self, xid_rejectSlot,
                              XConnectionType_Direct);
            self->m_buttonBox = (XWidget*)box;
        }
    }
#endif
    XLayout_setContentsMargins((XLayout*)root, 12, 12, 12, 12);
    XLayout_setSpacing((XLayout*)root, 8);
    if (self->m_label)
        XBoxLayout_addWidget(root, self->m_label);
    if (input) {
        self->m_inputWidget = input;
        XBoxLayout_addWidget(root, input);
        XWidget_show(input);
    }
    if (self->m_buttonBox)
        XBoxLayout_addWidget(root, self->m_buttonBox);
#if !XDIALOGBUTTONBOX_ON
    /* 无按钮盒模块：两个按钮直挂根布局（不建子布局，避免多出一个
     * 无人释放的布局句柄）。 */
    {
        XPushButton* ok = XPushButton_create((XWidget*)self, 0);
        XPushButton* cancel = XPushButton_create((XWidget*)self, 0);
        XBoxLayout_addStretch(root, 1);
        if (ok) {
            XAbstractButton_setText_2(
                (XAbstractButton*)ok,
                self->m_okButtonText ? XString_toUtf8(self->m_okButtonText)
                                     : "确定");
            XWidget_setMinimumSize((XWidget*)ok, 80, 28);
            xid_setName((XObject*)ok, XID_NAME_OK);
            XBoxLayout_addWidget(root, (XWidget*)ok);
            XObject_connect_1((XObject*)ok,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xid_acceptSlot,
                              XConnectionType_Direct);
        }
        if (cancel) {
            XAbstractButton_setText_2(
                (XAbstractButton*)cancel,
                self->m_cancelButtonText
                    ? XString_toUtf8(self->m_cancelButtonText) : "取消");
            XWidget_setMinimumSize((XWidget*)cancel, 80, 28);
            xid_setName((XObject*)cancel, XID_NAME_CANCEL);
            XBoxLayout_addWidget(root, (XWidget*)cancel);
            XObject_connect_1((XObject*)cancel,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xid_rejectSlot,
                              XConnectionType_Direct);
        }
    }
#endif
    XWidget_setLayout_ref((XWidget*)self, (XLayout*)root);
    self->m_mainLayout = (XLayout*)root;
    xid_syncInputWidget(self);
    xid_applyNoButtons(self);
}

/** @brief 结算输入控件当前值进存储（确认前调用；对标 Qt 由控件持有
 *  值、accept 时读控件的路径）。静态便捷路径的控件按私有名查找，
 *  实例路径直接用 m_inputWidget。 */
static void xid_settleFromInputWidget(XInputDialog* self)
{
    if (!self) return;
    switch (self->m_inputMode) {
    case XInputDialog_IntInput: {
        XSpinBox* spin = NULL;
        if (self->m_inputWidget &&
            XClassGetVtable((XObject*)self->m_inputWidget) ==
                XSpinBox_class_init())
            spin = (XSpinBox*)self->m_inputWidget;
        if (!spin)
            spin = (XSpinBox*)xid_childByName(&self->m_base, XID_NAME_SPIN);
        if (spin) XInputDialog_setIntValue(self, XSpinBox_value(spin));
        break;
    }
    case XInputDialog_DoubleInput: {
        XLineEdit* edit = NULL;
        if (self->m_inputWidget &&
            XClassGetVtable((XObject*)self->m_inputWidget) ==
                XLineEdit_class_init())
            edit = (XLineEdit*)self->m_inputWidget;
        if (!edit)
            edit = (XLineEdit*)xid_childByName(&self->m_base, XID_NAME_EDIT);
        if (edit) {
            const char* txt = XLineEdit_text(edit);
            XString* parsed = txt ? XString_create_utf8(txt) : NULL;
            bool ok = false;
            double v = parsed ? XString_toDouble(parsed, &ok)
                              : self->m_doubleValue;
            if (parsed) XClassDelete((XClass*)parsed);
            if (!ok) v = self->m_doubleValue;
            if (v < self->m_doubleMinimum) v = self->m_doubleMinimum;
            if (v > self->m_doubleMaximum) v = self->m_doubleMaximum;
            XInputDialog_setDoubleValue(self, v);
        }
        break;
    }
    case XInputDialog_TextInput:
    case XInputDialog_ComboBoxInput:
    default: {
        XWidget* w = self->m_inputWidget;
        /* 静态便捷路径没有 m_inputWidget：ComboBoxInput 的下拉框按私有
         * 名取回（getItem 自建控件），文本模式在下方按行编辑/多行回落。 */
        if (!w && self->m_inputMode == XInputDialog_ComboBoxInput)
            w = xid_childByName(&self->m_base, XID_NAME_COMBO);
        if (w && XClassGetVtable((XObject*)w) == XComboBox_class_init()) {
            XString* t = XComboBox_currentText((XComboBox*)w);
            if (t) {
                XInputDialog_setTextValue(self, t);
                XClassDelete((XClass*)t);
            }
        } else if (w &&
                   XClassGetVtable((XObject*)w) ==
                       XPlainTextEdit_class_init()) {
            char* buf = XPlainTextEdit_toPlainText((XPlainTextEdit*)w);
            if (buf) {
                XString* v = XString_create_utf8(buf);
                XFree_System(buf);
                if (v) {
                    XInputDialog_setTextValue(self, v);
                    XClassDelete((XClass*)v);
                }
            }
        } else if (w && XClassGetVtable((XObject*)w) ==
                            XLineEdit_class_init()) {
            XString* v = XString_create_utf8(XLineEdit_text((XLineEdit*)w));
            if (v) {
                XInputDialog_setTextValue(self, v);
                XClassDelete((XClass*)v);
            }
        } else {
            /* 静态便捷路径：按私有子对象名找（行编辑优先，多行兜底）。 */
            XLineEdit* edit = (XLineEdit*)xid_childByName(&self->m_base,
                                                          XID_NAME_EDIT);
            XString* v = NULL;
            if (edit) {
                v = XString_create_utf8(XLineEdit_text(edit));
            } else {
                XPlainTextEdit* plain = (XPlainTextEdit*)xid_childByName(
                    &self->m_base, XID_NAME_PLAIN);
                if (plain) {
                    char* buf = XPlainTextEdit_toPlainText(plain);
                    if (buf) {
                        v = XString_create_utf8(buf);
                        XFree_System(buf);
                    }
                }
            }
            if (v) {
                XInputDialog_setTextValue(self, v);
                XClassDelete((XClass*)v);
            }
        }
        break;
    }
    }
}

/** @brief 组装对话框骨架：Dialog 窗口标志 + 标题 + 垂直布局 + 可选标签。
 * @param outRoot 输出顶层布局；调用方在对话框删除后负责
 *                XClassDelete（布局不随控件析构释放）。 */
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
    /* 对标 QInputDialog 静态便捷函数：一律 dialog.setLabelText(label)
     * （label 为 NULL/空串时标签文本为空——Qt getText 的 label 缺省
     * QString() 走同一路径，不会保留实例构造时的 "Enter a value:"），
     * 且 ensureLayout **恒建 QLabel**（qinputdialog.cpp:203-207）。 */
    XInputDialog_setLabelText(dlg, label);
    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, (XWidget*)dlg);
    if (!root) {
        XClassDelete((XClass*)dlg);
        return NULL;
    }
    XLayout_setContentsMargins((XLayout*)root, 12, 12, 12, 12);
    XLayout_setSpacing((XLayout*)root, 8);
    {
        XLabel* lb = XLabel_create((XWidget*)dlg, 0);
        if (lb) {
            XString* labelText = XInputDialog_labelText(dlg);
            if (labelText) {
                XLabel_setText(lb, labelText);
                XClassDelete((XClass*)labelText);
            }
            /* 提示行行高钉底在 xid_applyExecSize 做：主题字模在装配时
             * 尚未落到控件（XFont_pixelSize 实测为 0），SHOW/exec 定尺
             * 时已解析，见该函数内「标签行高修正」。 */
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

/** @brief 对象字模单行行高（像素字号优先，字库位图行高兜底，再退 16；
 *  XLabel label_lineHeight 同源量纲）。 */
static int xid_fontLineHeight(const XWidget* w)
{
    XFont font;
    const XFontFace* face;
    XFontFaceInfo info;
    int base = XFONT8X16_HEIGHT;
    int scaleNum;
    if (!w) return base;
    font = XWidget_font((XWidget*)w);
    XMemset(&info, 0, sizeof(info));
    face = XFont_face(&font);
    if (face && XFontFace_info_base(face, &font, &info) &&
        info.m_kind == XFontFace_Bitmap && info.m_bitmap.m_height > 0)
        base = info.m_bitmap.m_height;
    scaleNum = XFont_pixelSize(&font) > 0 ? XFont_pixelSize(&font) : base;
    XClassDeinit((XClass*)&font);
    return scaleNum < 1 ? 1 : scaleNum;
}

/** @brief exec/显示定尺：内容实测 + CSD 装饰高（账本 #4/#10 定尺口径，
 *  exec 预定尺与 SHOW 精修共用）。
 *  @details 定尺 = 内容实际需求：调用方基线（360x140 等，对标 Qt
 *           QInputDialog 各便捷函数的常规默认）只作下限，实际取根布局
 *           totalSizeHint 实测（margins 12+12 + 标签行 + spacing 8×2 +
 *           输入行 + 按钮行）——CSD 平台装饰条画在客户区顶部，窗高不
 *           随装饰追加即标签/行编辑/按钮行被压缩裁出窗外。
 *
 *           软键盘不占对话框几何（2026-10-02 所有者裁定：对话框内不塞
 *           内嵌屏幕键盘——XVirtualKeyboard_popup 对 Dialog 宿主改以独立
 *           Popup 浮层弹于编辑器下方，可越出对话框边界，见该函数宿主
 *           解析分支）。本函数旧版的「软键盘避让带」（band=max(base,120)、
 *           布局下边距 12+band）随对话框内嵌键盘路径一并拆除：键盘不再
 *           是对话框子树浮层，无需为其预留空白带，对话框几何回归 Qt
 *           常规默认（内容实测+装饰高；算大与算小同为缺陷）。
 *
 *           exec 预定尺 + SHOW 复算双入口：装饰高查询在建窗前返回预
 *           测条高、建窗后为真实条高，两者一致时几何不变；SHOW 复算
 *           幂等（totalSizeHint 缓存恒为原始内容提示，见函数内注），
 *           兜底装饰态差异并重居中。 */
static void xid_applyExecSize(XInputDialog* dlg, int w, int h)
{
    XLayout* root;
    if (!dlg) return;
    root = XWidget_layout((XWidget*)dlg);
    if (!root) {
        XWidget_resize((XWidget*)dlg, w, h);
        return;
    }
    {
        /* 标签行高修正（账本 #4/#10 配套）：主题字模下标签 sizeHint
         * 实测可为 0x0（装配时字模未落控件），定尺按 0 高解算会把提示
         * 行压成 0 高（标签不可见）。按对象字模行高改写其缓存 sizeHint
         * （盒布局 hint 数学不钳 minimumSize，改写缓存是唯一入口），再
         * 失效布局提示缓存重取。 */
        XLayoutItem* it0 = XLayout_count_base(root) > 0
                               ? XLayout_itemAt_base(root, 0)
                               : NULL;
        XWidget* first = it0 ? XLayoutItem_widget_base(it0) : NULL;
        if (first) {
            XSize h0 = XWidget_sizeHint(first);
            int rowH = xid_fontLineHeight(first);
            if (h0.height < rowH) {
                h0.height = rowH;
                XWidget_setSizeHint(first, &h0);
            }
        }
        /* totalSizeHint 为解算缓存（XLayout.h 缓存口径）——改写控件
         * sizeHint 后需失效重取。exec 预定尺与 SHOW 精修重复调用天然
         * 幂等（标签行高修正单调：已 ≥ 行高则不再改写）。
         * [update→invalidate 2026-10-31] XLayout_update 已对齐 Qt 失效
         * 语义（不再承担缓存清理）；「失效+重取度量」的 Qt 惯用口令是
         * invalidate（qlayout.cpp:469 清缓存 + update）。 */
        XLayout_invalidate(root);
        XSize hint = XLayout_totalSizeHint(root);
        int needW = hint.width > 0 ? hint.width : 0;
        int base = (hint.height > 0 ? hint.height : 0) +
                   XDialog_decorationTopOffset(&dlg->m_base);
        if (w < needW) w = needW;
        if (h < base) h = base;
    }
    XWidget_resize((XWidget*)dlg, w, h);
}

/** @brief 取当前输入控件：实例路径直接读 m_inputWidget（对标
 *  d->inputWidget），静态便捷路径按 Qt 私有子对象名回落查找。 */
static XWidget* xid_inputWidget(XInputDialog* self)
{
    if (!self) return NULL;
    if (self->m_inputWidget) return self->m_inputWidget;
    switch (self->m_inputMode) {
    case XInputDialog_IntInput:
        return xid_childByName(&self->m_base, XID_NAME_SPIN);
    case XInputDialog_DoubleInput:
        return xid_childByName(&self->m_base, XID_NAME_EDIT);
    case XInputDialog_ComboBoxInput:
        return xid_childByName(&self->m_base, XID_NAME_COMBO);
    case XInputDialog_TextInput:
    default:
        {
            XWidget* edit = xid_childByName(&self->m_base, XID_NAME_EDIT);
            if (!edit)
                edit = xid_childByName(&self->m_base, XID_NAME_PLAIN);
            return edit;
        }
    }
}

/** @brief 显示时聚焦输入控件并全选（对标 QInputDialog::setVisible，
 *  qinputdialog.cpp:1060-1080：`inputWidget->setFocus()` + 行编辑/
 *  纯文本/INT/DOUBLE 自旋框各自的 `selectAll()`；下拉框只聚焦不全选）。
 *  @note 静态便捷路径经 m_suppressAutoFocus 抑制——现场 2026-10-02
 *        所有者裁定「点击输入框才弹虚拟键盘」（见 xid_execDialog 注），
 *        该裁定只针对便捷函数一次性弹出的对话框；程序化 show()/open()
 *        的实例路径按 Qt 语义聚焦。 */
static void xid_focusInputWidget(XInputDialog* self)
{
    XWidget* w;
    if (!self) return;
    w = xid_inputWidget(self);
    if (!w) return;
    XWidget_setFocus(w);
    if (XClassGetVtable((XObject*)w) == XLineEdit_class_init()) {
        XLineEdit_selectAll((XLineEdit*)w);
    } else if (XClassGetVtable((XObject*)w) == XPlainTextEdit_class_init()) {
        XPlainTextEdit_selectAll((XPlainTextEdit*)w);
    } else if (XClassGetVtable((XObject*)w) == XSpinBox_class_init()) {
        XSpinBox_selectAll((XSpinBox*)w);
    }
    /* 下拉框（ComboBoxInput）：Qt 只 setFocus，不 selectAll。 */
}

/** @brief 显隐槽（对标 QInputDialog::setVisible 覆写，qinputdialog.cpp:
 *  1060-1080）：显示时先确保布局、再聚焦输入控件并全选。
 *  @note 聚焦放在基类流程**之后**——本框架在隐藏态聚焦会被焦点门钳住，
 *        故与 Qt 的「ensureLayout→setFocus→QDialog::setVisible」顺序
 *        等价倒置成「ensureLayout→QDialog::setVisible→setFocus」。
 *        重复 setVisible(true) 同样重新聚焦/全选（Qt 每调用一次都执行
 *        ensureLayout+selectAll）。 */
static void VXInputDialog_setVisible(XWidget* self, bool visible)
{
    XInputDialog* dlg = (XInputDialog*)self;
    /* 对标 QInputDialog::setVisible（qinputdialog.cpp:1060-1066）：
     * 先 ensureLayout 再 QDialog::setVisible——实例路径在此按
     * inputMode 懒建标签/输入控件/按钮行（静态便捷路径已自建布局，
     * xid_ensureLayout 内早退）。 */
    if (dlg && visible)
        xid_ensureLayout(dlg);
    XClass_Parent(XDialog, EXWidget_SetVisible,
                  void (*)(XWidget*, bool))(self, visible);
    if (dlg && visible && !dlg->m_suppressAutoFocus)
        xid_focusInputWidget(dlg);
}

/** @brief 显示事件：exec 预定尺后按当前装饰/边距态复算一次（幂等；
 *  CSD 真实条高建窗后才可查询，首显精修兜底），随后父类 showEvent 的
 *  CSD 避让链只补差额——避让改写布局顶边距发生在本次布局解算之后，
 *  末尾显式 activate 一次按新边距重排（标签/行编辑/按钮行否则仍按
 *  旧顶边距落位、被装饰条压住）。 */
static void VXInputDialog_showEvent(XWidget* self, XEvent* event)
{
    XLayout* root = NULL;
    if (self && event && XEvent_type(event) == XEVENT_TYPE_SHOW) {
        xid_applyExecSize((XInputDialog*)self,
                          XWidget_width(self), XWidget_height(self));
        root = XWidget_layout(self);
        /* 布局改回 SetNoConstraint：默认 SetDefault 约束会在每次
           activate（含 show 后 updateGeometry 的挂起激活）把布局
           minimumSize 写回对话框，覆盖下方锁死值——根布局退出写回，
           排布照常，顶层尺寸完全由 exec 定稿 + setFixedSize 管。 */
        if (root)
            XLayout_setSizeConstraint(root,
                                      XLayoutSizeConstraint_SetNoConstraint);
    }
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (root) {
        XLayout_activate(root);
        /* 尺寸锁定（2026-10-03 所有者问询盘点，对标 qinputdialog.cpp
         * QInputDialog 全模式用户不可拖拽改尺寸的既有效果）：activate
         * 排布完成（默认约束会把布局 minimum 写回控件，故锁死必须居
         * 其后）再 setFixedSize——min=max=exec 定稿尺寸（360x143/
         * 400x260/... 视觉不变），平台层经
         * XPlatformNativeWindow_setSizeHints 落 WM_NORMAL_HINTS
         * PMinSize/PMaxSize（kwin/DDE 拖拽钳制）。幂等：每次 SHOW
         * 以同一 exec 尺寸重锁。 */
        XWidget_setFixedSize(self,
                             XWidget_width(self), XWidget_height(self));
    }
}

/** @brief 关闭事件：[×] 关闭经基类 reject→done 收口（finished 发射
 *  时 open_2 连接仍在，receiver 恰回调一次），收口后断开并清记录
 *  ——超出 Qt 严格对等的健壮性收口，防连接与记录跨关闭存活、下次
 *  open_2 叠连（XMessageBox VXMessageBox_closeEvent 同款）。 */
static void VXInputDialog_closeEvent(XWidget* self, XEvent* event)
{
    bool close = self && event && XEvent_type(event) == XEVENT_TYPE_CLOSE;
    XClass_Parent(XDialog, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (close)
        xid_openCleanup((XInputDialog*)self);
}

/** @brief 阻塞模态执行：定尺寸、主屏居中、exec（复用 XDialog 阻塞
 *  循环：应用模态 + Escape→reject）。返回是否接受。
 *  @details 不再预聚焦输入控件（2026-10-02 所有者点击驱动裁定，
 *           覆盖夜间台账 #26 的预聚焦方案）：旧路径 exec 前
 *           xid_focusInputWidget 抢先把焦点放输入控件，键盘弹出是
 *           焦点驱动守护（XVirtualKeyboard autoPopup 默认开、宿主类
 *           型无关）→ 输入控件一获焦即弹外置键盘，「打开即有键盘」
 *           与裁定「点击输入框才弹」相悖。删除后初始焦点走
 *           XDialog::dialog_grabInitialFocus 既有落点（默认按钮优
 *           先，无则对话框自身）——打开无键盘；点击输入框时
 *           XLineEdit mousePress → setFocus（点击驱动语义与主窗编
 *           辑框完全一致）→ 外置键盘在编辑框下方弹出。取舍得失：
 *           打开后不点输入框直接物理键入无效（焦点在确定钮，属标
 *           准对话框语义，登记为有意取舍非遗忘）。 */
static bool xid_execDialog(XInputDialog* dlg, int w, int h)
{
    int rc;
    if (!dlg) return false;
    xid_applyExecSize(dlg, w, h);
    /* 现场裁定保留（见上方注释）：便捷函数一次性弹出的对话框不预聚焦
     * 输入控件；程序化 show()/open() 的实例仍走 Qt 的聚焦+全选语义。 */
    dlg->m_suppressAutoFocus = 1;
    /* 居中统一走 XDialog_exec 漏斗（xdlg_centerToParentWindow，对标
     * QDialogPrivate::adjustPosition）——私有屏幕居中已清理，两套并
     * 存时本处先行 move 会置 Moved 位、反令标准漏斗失明（2026-10-03
     * 所有者裁定合并为一套）。 */
    rc = XDialog_exec(&dlg->m_base);
    return rc == 1; /* 对标 QDialog::Accepted。 */
}

/** @brief 收尾：对话框子树与布局句柄一律延迟回收（取消崩溃根修，
 *  见文件头块注）。返回值已由调用方于本函数之前提取，删除时机不
 *  影响结果回读（对标 Qt QDialog::exec 返回后 QObject 树的延迟消亡）。 */
static void xid_teardown(XInputDialog* dlg, XBoxLayout* root, XBoxLayout* bar)
{
    if (!dlg) return;
    xid_pendingRegister(dlg, root, bar);
    XObject_deleteLater((XObject*)dlg);
}

/** @brief open_2 信号选择→信号地址（对标 Qt signalForMember 候选信号
 *  集：文本/整数/浮点载荷→对应 *ValueSelected，兜底 finished）。 */
static size_t xid_openSignalId(XInputDialogOpenSignal openSignal)
{
    switch (openSignal) {
    case XInputDialog_OpenTextValueSelected:
        return (size_t)XInputDialog_textValueSelected_signal;
    case XInputDialog_OpenIntValueSelected:
        return (size_t)XInputDialog_intValueSelected_signal;
    case XInputDialog_OpenDoubleValueSelected:
        return (size_t)XInputDialog_doubleValueSelected_signal;
    case XInputDialog_OpenFinished:
    default:
        return (size_t)XDialog_finished_signal;
    }
}

/** @brief open_2 连接收口：断开 open 记录的连接并清空记录（按钮
 *  点击/done 收口与 [×] 关闭两路共用；未 open 过或已清理时空操作，
 *  幂等。XMessageBox xmsg_openCleanup 同款）。 */
static void xid_openCleanup(XInputDialog* dlg)
{
    if (!dlg) return;
    if (dlg->m_openReceiver && dlg->m_openMember) {
        XObject_disconnect_1((XObject*)dlg,
                             xid_openSignalId(dlg->m_openSignal),
                             dlg->m_openReceiver, dlg->m_openMember);
    }
    dlg->m_openReceiver = NULL;
    dlg->m_openMember = NULL;
    dlg->m_openSignal = XInputDialog_OpenFinished;
}

static void xid_acceptSlot(XObject* receiver, XVarList* args)
{
    XInputDialog* dlg = (XInputDialog*)receiver;
    (void)args;
    if (!dlg) return;
    /* Qt 语义下 *ValueSelected 由 done(result!=0) 发射（被调方
     * QInputDialog::done，qinputdialog.cpp:1101-1121）。本槽先把输入
     * 控件当前值结算进存储属性，再经 XInputDialog_done 走统一发射点
     * ——程序化 accept()/done(1) 与按钮点击共用同一条信号路径。
     * 结算按承载控件类型分派（实例路径读 m_inputWidget，静态便捷
     * 路径按私有子对象名回落，见 xid_settleFromInputWidget）。 */
    xid_settleFromInputWidget(dlg);
    /* 确定收口前置子树输入侧摘除（与取消收口同纪律：确定槽同样运行
     * 在真实按压的发射帧内；open_2 复用路径守卫跳过，见 xid_rejectSlot
     * 注）。随后经 XInputDialog_done 统一收口——确认信号由该覆写按
     * 「先 QDialog::done、后 *ValueSelected」的 Qt 顺序发射。 */
    if (!dlg->m_openReceiver)
        xid_detachInputRecursive(&dlg->m_base.m_base);
    XInputDialog_done(dlg, (int)XDialogCode_Accepted);
    /* open_2 连接收口：done 发射 finished 时连接仍在（receiver 恰回调
     * 一次），收口后断开（XMessageBox 按钮点击路径同款）。 */
    xid_openCleanup(dlg);
}

void XInputDialog_done(XInputDialog* self, int result)
{
    if (!self) return;
    /* 对标 QInputDialog::done（qinputdialog.cpp:1101-1121）：先
     * QDialog::done(result)，再在 result != 0 时按当前输入模式发射
     * *ValueSelected（本机 Qt 6.8 实测：done(1)→textValueSelected、
     * done(0)→无确认信号）。ComboBoxInput 是 X 扩展模式，其下拉值走
     * textValue 通道。
     * 程序化 done(1) 也要先把控件当前值结算进存储（Qt 的 intValue()
     * 等直接读控件，本库读存储，故统一在收口点结算一次）。 */
    if (result)
        xid_settleFromInputWidget(self);
    XDialog_done(&self->m_base, result);
    if (result) {
        switch (self->m_inputMode) {
        case XInputDialog_IntInput:
            XInputDialog_intValueSelected_signal(self, self->m_intValue);
            break;
        case XInputDialog_DoubleInput:
            XInputDialog_doubleValueSelected_signal(self, self->m_doubleValue);
            break;
        case XInputDialog_TextInput:
        case XInputDialog_ComboBoxInput:
        default:
            XInputDialog_textValueSelected_signal(self, self->m_textValue);
            break;
        }
    }
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

/** @brief getText 共同实现（对标 QInputDialog::getText + 占位提示扩展）。
 *  @param placeholder 占位提示（可为 NULL）：仅空文本时在内部行编辑灰显
 *         （XLineEdit 占位绘制契约：PlaceholderText 调色色，输入即消失，
 *         不进入 accept 结算值）。本库扩展——Qt 6.8 qinputdialog.h 无
 *         placeholder 族（全文件 grep 0 命中）。 */
static XString* xid_getTextImpl(XWidget* parent, const XString* title,
                                const XString* label,
                                XInputDialogEchoMode echo,
                                const XString* text,
                                const XString* placeholder, bool* ok)
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
        XClassDelete(dlg);
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
            if (placeholder && XString_toUtf8(placeholder) &&
                XString_toUtf8(placeholder)[0])
                XLineEdit_setPlaceholderText(edit,
                                             XString_toUtf8(placeholder));
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

XString* XInputDialog_getText(XWidget* parent, const XString* title,
                              const XString* label, XInputDialogEchoMode echo,
                              const XString* text, bool* ok)
{
    return xid_getTextImpl(parent, title, label, echo, text, NULL, ok);
}

XString* XInputDialog_getText_2(XWidget* parent, const char* title,
                                const char* label, XInputDialogEchoMode echo,
                                const char* text, bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    XString* v = text ? XString_create_utf8(text) : NULL;
    XString* result = XInputDialog_getText(parent, t, l, echo, v, ok);
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
    if (v) XClassDelete((XClass*)v);
    return result;
}

XString* XInputDialog_getText_3(XWidget* parent, const char* title,
                                const char* label, XInputDialogEchoMode echo,
                                const char* text, const char* placeholder,
                                bool* ok)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XString* l = label ? XString_create_utf8(label) : NULL;
    XString* v = text ? XString_create_utf8(text) : NULL;
    XString* ph = placeholder ? XString_create_utf8(placeholder) : NULL;
    XString* result = xid_getTextImpl(parent, t, l, echo, v, ph, ok);
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
    if (v) XClassDelete((XClass*)v);
    if (ph) XClassDelete((XClass*)ph);
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
        XClassDelete(dlg);
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
                    XClassDelete((XClass*)v);
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
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
    if (v) XClassDelete((XClass*)v);
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
        XClassDelete(dlg);
        return value;
    }
    dlg = xid_buildDialog(parent, title, label, &root);
    if (!dlg) return value;
    XInputDialog_setInputMode(dlg, XInputDialog_IntInput);
    /* 对标 QInputDialog::getInt（qinputdialog.cpp:1237-1244）：
     * setIntRange(min,max) → setIntValue(value) → setIntStep(step)。范围
     * 必须先落到对话框存储（缺省 0..99），否则 accept 结算
     * setIntValue(自旋框当前值) 会按缺省范围二次钳位（大于 99 的合法
     * 输入被截断）。 */
    XInputDialog_setIntRange(dlg, minValue, maxValue);
    XInputDialog_setIntValue(dlg, value);
    XInputDialog_setIntStep(dlg, step < 0 ? 0 : step);
    {
        XSpinBox* spin = XSpinBox_create((XWidget*)dlg, 0);
        if (spin) {
            XSpinBox_setRange(spin, dlg->m_intMinimum, dlg->m_intMaximum);
            /* 与 setIntStep 同口径：Qt 6.8 QSpinBox::setSingleStep 负值钳 0、
             * 0 原样保存（探针 setSingleStep(0)→0、-5→0）。 */
            XSpinBox_setSingleStep(spin, step < 0 ? 0 : step);
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
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
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
        XClassDelete(dlg);
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
            XString* txt;
            int dec = decimals < 0 ? 6 : (decimals > 10 ? 10 : decimals);
            txt = XString_create_fmt_utf8("%.*f", dec, value);
            XLineEdit_setText(edit, txt ? XString_toUtf8(txt) : "");
            if (txt) XClassDelete((XClass*)txt);
            XWidget_setMinimumSize((XWidget*)edit, 220, 24);
            /* 对标 Qt 私有子对象命名：accept 结算经 findChild 解析行
               编辑当前文本（同 getText 的 #25 根因，此前确认后恒回
               初值）。 */
            xid_setName((XObject*)edit, XID_NAME_EDIT);
            if (root)
                XBoxLayout_addWidget(root, (XWidget*)edit);
        }
    }
    /* 对标 QInputDialog::getDouble（qinputdialog.cpp:1305-1313）：
     * setDoubleRange(min,max) → setDoubleValue(value) →
     * setDoubleDecimals(decimals)。 */
    XInputDialog_setDoubleRange(dlg, minValue, maxValue);
    XInputDialog_setDoubleValue(dlg, value);
    XInputDialog_setDoubleDecimals(dlg, decimals);
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
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
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
        XClassDelete(dlg);
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
        if (t) XClassDelete((XClass*)t);
        if (l) XClassDelete((XClass*)l);
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
    XClassDelete((XClass*)list);
    if (t) XClassDelete((XClass*)t);
    if (l) XClassDelete((XClass*)l);
    return result;
}

/* ==================== 非阻塞打开（对标 QInputDialog::open） ==================== */

void XInputDialog_open_2(XInputDialog* self, XObject* receiver,
                         XSlotFunc1 member, XInputDialogOpenSignal openSignal)
{
    if (!self || !member) return;
    /* 重复 open 防叠连：旧记录尚存时先断开旧连接（disconnect_1 只移
     * 除首个匹配，直接叠连会残留旧连接，receiver 每次收口被多路误
     * 触发。XMessageBox_open_2 同款）。 */
    if (self->m_openReceiver || self->m_openMember)
        xid_openCleanup(self);
    self->m_openReceiver = receiver;
    self->m_openMember = member;
    self->m_openSignal = openSignal;
    if (receiver) {
        XObject_connect_1((XObject*)self, xid_openSignalId(openSignal),
                          receiver, member, XConnectionType_Direct);
    }
    /* 对标 QDialog::open：窗口模态显示并立即返回。 */
    XDialog_open(&self->m_base);
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
    int oldValue;
    if (!self) return;
    /* 对标 QSpinBox::setRange：max<min 时 max 收敛到 min
     * （setRange(50,20)→50..50、value=50）；随后当前值按新范围钳位
     * 并在实变时发 intValueChanged（本机 Qt 6.8 探针 I6）。 */
    if (max < min) max = min;
    oldValue = self->m_intValue;
    self->m_intMinimum = min;
    self->m_intMaximum = max;
    if (self->m_intValue < min) self->m_intValue = min;
    if (self->m_intValue > max) self->m_intValue = max;
    xid_syncInputWidget(self);
    if (self->m_intValue != oldValue)
        xinputdialog_emitInt(self,
                             (size_t)XInputDialog_intValueChanged_signal,
                             self->m_intValue);
}

int XInputDialog_intMinimum(const XInputDialog* self)
{ return self ? self->m_intMinimum : 0; }

void XInputDialog_setIntMinimum(XInputDialog* self, int min)
{
    int oldValue;
    if (!self) return;
    /* QSpinBox::setMinimum：新下限高于当前上限时上限收敛到下限
     * （探针 setMinimum(30) @10..20 → 30..30、value=30）。 */
    oldValue = self->m_intValue;
    if (min > self->m_intMaximum) self->m_intMaximum = min;
    self->m_intMinimum = min;
    if (self->m_intValue < min) self->m_intValue = min;
    xid_syncInputWidget(self);
    if (self->m_intValue != oldValue)
        xinputdialog_emitInt(self,
                             (size_t)XInputDialog_intValueChanged_signal,
                             self->m_intValue);
}

int XInputDialog_intMaximum(const XInputDialog* self)
{ return self ? self->m_intMaximum : 99; }

void XInputDialog_setIntMaximum(XInputDialog* self, int max)
{
    int oldValue;
    if (!self) return;
    /* QSpinBox::setMaximum：新上限低于当前下限时下限收敛到上限。 */
    oldValue = self->m_intValue;
    if (max < self->m_intMinimum) self->m_intMinimum = max;
    self->m_intMaximum = max;
    if (self->m_intValue > max) self->m_intValue = max;
    xid_syncInputWidget(self);
    if (self->m_intValue != oldValue)
        xinputdialog_emitInt(self,
                             (size_t)XInputDialog_intValueChanged_signal,
                             self->m_intValue);
}

int XInputDialog_intStep(const XInputDialog* self)
{ return self ? self->m_intStep : 1; }

void XInputDialog_setIntStep(XInputDialog* self, int step)
{
    /* Qt 6.8：QInputDialog::setIntStep 直接转发 QSpinBox::setSingleStep
     * （qinputdialog.cpp `d->intSpinBox->setSingleStep(step)`），负值钳 0、
     * 0 原样保存。本机 Qt 6.8 探针：setSingleStep(-5)→singleStep()==0、
     * setSingleStep(0)→0（旧实现忽略非正步进，与 Qt 冲突）。 */
    if (self) {
        self->m_intStep = step < 0 ? 0 : step;
        xid_syncInputWidget(self);
    }
}

void XInputDialog_setDoubleRange(XInputDialog* self, double min, double max)
{
    double oldValue;
    if (!self) return;
    /* 同 setIntRange：QDoubleSpinBox::setRange 的边界收敛与值钳位
     * （探针 setDoubleRange(10.0,5.0)→10.0..10.0、value=10.0）。 */
    if (max < min) max = min;
    oldValue = self->m_doubleValue;
    self->m_doubleMinimum = min;
    self->m_doubleMaximum = max;
    if (self->m_doubleValue < min) self->m_doubleValue = min;
    if (self->m_doubleValue > max) self->m_doubleValue = max;
    xid_syncInputWidget(self);
    if (self->m_doubleValue != oldValue)
        xinputdialog_emitDouble(
            self, (size_t)XInputDialog_doubleValueChanged_signal,
            self->m_doubleValue);
}

double XInputDialog_doubleMinimum(const XInputDialog* self)
{ return self ? self->m_doubleMinimum : 0.0; }

void XInputDialog_setDoubleMinimum(XInputDialog* self, double min)
{
    double oldValue;
    if (!self) return;
    oldValue = self->m_doubleValue;
    if (min > self->m_doubleMaximum) self->m_doubleMaximum = min;
    self->m_doubleMinimum = min;
    if (self->m_doubleValue < min) self->m_doubleValue = min;
    xid_syncInputWidget(self);
    if (self->m_doubleValue != oldValue)
        xinputdialog_emitDouble(
            self, (size_t)XInputDialog_doubleValueChanged_signal,
            self->m_doubleValue);
}

double XInputDialog_doubleMaximum(const XInputDialog* self)
{ return self ? self->m_doubleMaximum : 99.99; }

void XInputDialog_setDoubleMaximum(XInputDialog* self, double max)
{
    double oldValue;
    if (!self) return;
    oldValue = self->m_doubleValue;
    if (max < self->m_doubleMinimum) self->m_doubleMinimum = max;
    self->m_doubleMaximum = max;
    if (self->m_doubleValue > max) self->m_doubleValue = max;
    xid_syncInputWidget(self);
    if (self->m_doubleValue != oldValue)
        xinputdialog_emitDouble(
            self, (size_t)XInputDialog_doubleValueChanged_signal,
            self->m_doubleValue);
}

double XInputDialog_doubleStep(const XInputDialog* self)
{ return self ? self->m_doubleStep : 1.0; }

void XInputDialog_setDoubleStep(XInputDialog* self, double step)
{
    /* 同 setIntStep：Qt 6.8 QDoubleSpinBox::setSingleStep 负值钳 0、0 原样
     * 保存。本机探针：setSingleStep(-2.5)→0、setSingleStep(0.0)→0。 */
    if (self) {
        self->m_doubleStep = step < 0.0 ? 0.0 : step;
        /* 浮点承载是行编辑（无步进语义），仍回灌一次保持幂等口径。 */
        xid_syncInputWidget(self);
    }
}

int XInputDialog_doubleDecimals(const XInputDialog* self)
{ return self ? self->m_doubleDecimals : 2; }

void XInputDialog_setDoubleDecimals(XInputDialog* self, int decimals)
{
    /* Qt 6.8 QDoubleSpinBox::setDecimals 把小数位钳到 [0,323]（本机探针：
     * setDecimals(-1)→0、setDecimals(400)→323；旧实现只拒负值且上限
     * 不设，getter 会回读出 Qt 采不到的 400）。 */
    if (!self) return;
    if (decimals < 0) decimals = 0;
    if (decimals > 323) decimals = 323;
    self->m_doubleDecimals = decimals;
    xid_syncInputWidget(self);
}

XInputDialogEchoMode XInputDialog_textEchoMode(const XInputDialog* self)
{ return self ? self->m_echoMode : XInputDialogEchoMode_Normal; }

void XInputDialog_setTextEchoMode(XInputDialog* self,
                                  XInputDialogEchoMode mode)
{
    if (!self) return;
    self->m_echoMode = mode;
    /* 对标 setTextEchoMode（qinputdialog.cpp:687-692）：直接改行编辑的
     * echoMode；实例布局未建时留待回灌。 */
    xid_syncInputWidget(self);
}

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

#endif /* XGUI_ON */
