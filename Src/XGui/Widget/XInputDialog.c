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
#include <stdlib.h>            /* strtod：浮点输入解析（前缀解析语义，XString_toDouble 为全串校验不等价，暂留） */
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
#include "XLayoutItem_Protected.h" /* XLayoutItem_widget_base 声明：缺
                                    * 它时隐式 int 返回 + cltq 截断
                                    * 64 位控件指针（r1#4/#10 回归锁
                                    * 首跑 SIGSEGV 实锚，2026-10-01） */

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

static void VXInputDialog_showEvent(XWidget* self, XEvent* event);
static void VXInputDialog_closeEvent(XWidget* self, XEvent* event);
static void xid_openCleanup(XInputDialog* dlg);

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXInputDialog_deinit(XInputDialog* self)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_labelText);
    xinputdialog_freeString(&self->m_textValue);
    xinputdialog_freeString(&self->m_comboBoxText);
    xinputdialog_freeString(&self->m_okButtonText);
    xinputdialog_freeString(&self->m_cancelButtonText);
    xinputdialog_freeString(&self->m_placeholderText);
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXInputDialog_showEvent);
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
    self->m_placeholderText = NULL;
    /* 默认范围对齐 Qt QInputDialog：int 全范围、double 全范围、
       步进 1、小数位 2。 */
    self->m_intMinimum = -2147483647 - 1;
    self->m_intMaximum = 2147483647;
    self->m_intStep = 1;
    self->m_doubleMinimum = -1.0e308;
    self->m_doubleMaximum = 1.0e308;
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
        XString* item = (XString*)XStringList_at_base((const XVector*)items, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base(
                (XVector*)self->m_comboBoxItems, copy);
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
        XString* item = (XString*)XStringList_at_base(
            (const XVector*)self->m_comboBoxItems, i);
        XString* copy = item ? XString_create_copy(item) : XString_create();
        if (copy) {
            XStringList_push_back_move_base((XVector*)out, copy);
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

void XInputDialog_setPlaceholderText(XInputDialog* self, const XString* text)
{
    if (!self) return;
    xinputdialog_freeString(&self->m_placeholderText);
    self->m_placeholderText = xinputdialog_dupString(text);
}

XString* XInputDialog_placeholderText(const XInputDialog* self)
{
    return self ? xinputdialog_dupString(self->m_placeholderText)
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

/** @brief OK 槽：把内嵌控件当前值结算进对话框存储后 accept（对标 Qt
 *  QInputDialog 在 accept 前由输入控件同步 d->value 的路径）。 */
static void xid_acceptSlot(XObject* receiver, XVarList* args);

/** @brief Cancel 槽：reject 关闭（对标 cancel 按钮触发 reject()）。 */
static void xid_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XDialog_reject((XDialog*)receiver);
    /* open_2 连接收口：reject→done 发射 finished 时连接仍在（receiver
     * 恰回调一次），收口后断开，防连接与记录跨关闭存活（XMessageBox
     * 按钮点击路径同款）。 */
    xid_openCleanup((XInputDialog*)receiver);
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
    XFont_deinit_base((XClass*)&font);
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
         * sizeHint 后需 update 失效重取。exec 预定尺与 SHOW 精修重复
         * 调用天然幂等（标签行高修正单调：已 ≥ 行高则不再改写）。 */
        XLayout_update(root);
        XSize hint = XLayout_totalSizeHint(root);
        int needW = hint.width > 0 ? hint.width : 0;
        int base = (hint.height > 0 ? hint.height : 0) +
                   XDialog_decorationTopOffset(&dlg->m_base);
        if (w < needW) w = needW;
        if (h < base) h = base;
    }
    XWidget_resize((XWidget*)dlg, w, h);
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
    /* 居中统一走 XDialog_exec 漏斗（xdlg_centerToParentWindow，对标
     * QDialogPrivate::adjustPosition）——私有屏幕居中已清理，两套并
     * 存时本处先行 move 会置 Moved 位、反令标准漏斗失明（2026-10-03
     * 所有者裁定合并为一套）。 */
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
    /* open_2 连接收口：accept→done 发射 finished 时连接仍在（receiver
     * 恰回调一次），收口后断开（XMessageBox 按钮点击路径同款）。 */
    xid_openCleanup(dlg);
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
 *         不进入 accept 结算值），对标 QInputDialog::setPlaceholderText。 */
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
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    if (v) XString_delete_base((XClass*)v);
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
    if (t) XString_delete_base((XClass*)t);
    if (l) XString_delete_base((XClass*)l);
    if (v) XString_delete_base((XClass*)v);
    if (ph) XString_delete_base((XClass*)ph);
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
            XString* txt;
            int dec = decimals < 0 ? 6 : (decimals > 10 ? 10 : decimals);
            txt = XString_create_fmt_utf8("%.*f", dec, value);
            XLineEdit_setText(edit, txt ? XString_toUtf8(txt) : "");
            if (txt) XString_delete_base((XClass*)txt);
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