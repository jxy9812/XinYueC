/**
 * @file       XMessageBox.c
 * @brief      消息对话框控件实现（对标 Qt 6.8 QMessageBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMessageBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XDialog.h"
#include "XApplication.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XIcon.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XPainter.h"
#if XCHECKBOX_ON
#include "XCheckBox.h"
#endif

#if XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON

/* ==================== 内部工具 ==================== */

/** @brief 图标区是否存在（对标 Qt QMessageBoxPrivate::setupLayout 的
 *         hasIcon：iconPixmap 优先，其次非 NoIcon 的分级标准图标）。 */
static bool xmsg_hasIconArea(const XMessageBox* self)
{
    if (!self) return false;
    if (self->m_iconPixmap) return true;
    return self->m_icon != (int)XMessageBoxIcon_NoIcon;
}

/** @brief 内容区顶部 y（对标 Qt：标题栏以下为内容区起点；XGui 子控
 *         件形态对话框的标题由 XDialog 面板顶部带内绘制，有标题时
 *         内容下移让出标题文本条，无标题保持原布局不占位）。 */
static int xmsg_contentTop(const XMessageBox* self)
{
    const XString* title;
    const char* utf8;
    if (!self) return 12;
    title = XWidget_windowTitle((const XWidget*)self);
    utf8 = title ? XString_toUtf8(title) : NULL;
    return (utf8 && utf8[0]) ? 28 : 12;
}

static void xmsg_setupText(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int top;
    int x;
    if (!self || !self->m_textLabel) return;
    top = xmsg_contentTop(self);
    /* 对标 Qt QMessageBox 布局：图标列在文本左侧（indentSpacer 7px +
     * 图标区），无图标时文本占满内容行。 */
    x = xmsg_hasIconArea(self) ? 56 : 16;
    XRect_init(&r, x, top,
               w > x + 16 ? w - x - 16 : 0, 60);
    XWidget_setGeometry((XWidget*)self->m_textLabel,
                        r.x, r.y, r.width, r.height);
}

static void xmsg_setupCheckBox(XMessageBox* self)
{
#if XCHECKBOX_ON
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int top;
    if (!self || !self->m_checkBox) return;
    top = xmsg_contentTop(self);
    /* 复选框行固定位于消息文本（top,h=60）与按钮盒（底部 40）之间。 */
    if (h > top + 64 + 20 + 40)
        XWidget_setGeometry((XWidget*)self->m_checkBox, 16, top + 64,
                            w > 32 ? w - 32 : 0, 20);
#else
    (void)self;
#endif
}

static void xmsg_setupButtonBox(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_buttonBox) return;
    XRect_init(&r, 0, h > 40 ? h - 40 : 0, w, 40);
    XWidget_setGeometryRect((XWidget*)self->m_buttonBox, &r);
}

/* ==================== 事件处理 ==================== */

static void VX_messageBox_resizeEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    (void)event;
    if (!box) return;
    xmsg_setupText(box);
    xmsg_setupCheckBox(box);
    xmsg_setupButtonBox(box);
}

/* R-80 根因：按钮盒 accepted/rejected 槽此前只置 m_inExec=false，
   不发射 finished/accepted/rejected，与 Esc 路径（KeyPressEvent →
   XDialog_reject → 真发射 finished(0)+rejected）不对称。对标
   QDialog::done(r)：按钮点击同样经 XDialog_accept/reject 统一收口
   （done 式：隐藏+标脏+finished+accepted/rejected），再由 done 置
   m_inExec=false 结束 exec 循环。 */
/* 按钮盒 accepted → QDialog::accept 式收口。 */
static void xmsg_acceptedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    (void)args;
    if (box) XDialog_accept(&box->m_base);
}

/* 按钮盒 rejected → QDialog::reject 式收口。 */
static void xmsg_rejectedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    (void)args;
    if (box) XDialog_reject(&box->m_base);
}

/** @brief 按钮盒 clicked → 记录 clickedButton 并发射 buttonClicked。 */
static void xmsg_clickedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    if (!box || !args) return;
    XVarList_args_1(args, XAbstractButton*, button);
    box->m_clicked = (XAbstractButton*)button;
    if (box && ((XObject*)box)->m_signalSlot) {
        XVarList* out = XVarList_Create(XVar(XAbstractButton*, button));
        if (out) {
            XObject_emitSignal((XObject*)box,
                               (size_t)XMessageBox_buttonClicked_signal,
                               out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
}

/* ==================== 生命周期与虚表 ==================== */

static void VXMessageBox_deinit(XMessageBox* self)
{
    if (!self) return;
    if (self->m_text) {
        XString_delete_base(self->m_text);
        self->m_text = NULL;
    }
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_detailedText) {
        XString_delete_base(self->m_detailedText);
        self->m_detailedText = NULL;
    }
    if (self->m_informativeText) {
        XString_delete_base(self->m_informativeText);
        self->m_informativeText = NULL;
    }
        if (self->m_standards) {
        XVector_delete_base(self->m_standards);
        self->m_standards = NULL;
    }
    if (self->m_iconPixmap) {
        XImage_delete_base((XClass*)self->m_iconPixmap);
        self->m_iconPixmap = NULL;
    }
#if XCHECKBOX_ON
    if (self->m_checkBox) {
        XCheckBox_delete_base((XClass*)self->m_checkBox);
        self->m_checkBox = NULL;
    }
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/** @brief 键盘：Enter→defaultButton；Esc→escapeButton（未设置回退
 *         基类 Escape=reject）（对标 QMessageBox keyPressEvent）。 */
static void VXMessageBox_keyPressEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (box && event &&
        XEvent_type(event) == XEVENT_TYPE_KEY_PRESS) {
        int key = ((XKeyEvent*)event)->m_key;
        if (key == (int)XKey_Return || key == (int)XKey_Enter) {
            if (box->m_defaultButton) {
                XAbstractButton_click(box->m_defaultButton);
                XEvent_accept(event);
                return;
            }
        } else if (key == (int)XKey_Escape) {
            if (box->m_escapeButton) {
                XAbstractButton_click(box->m_escapeButton);
                XEvent_accept(event);
                return;
            }
            XDialog_reject(&box->m_base);
            XEvent_accept(event);
            return;
        }
    }
    /* 其余按键静态转发父类 XDialog 实现（对标 QDialog::keyPressEvent
       非 Esc 分支静态调用基类）：经 XClass_Parent 取 XDialog 类虚表
       keyPress 槽位（VXDialog_keyPressEvent，含 Escape→reject 回退）。
       此前经 XWidget_keyPressEvent_base 转发：该 _base 入口按对象虚表
       再分派回最派生重载 VXMessageBox_keyPressEvent，未处理的按键
       （如无 defaultButton 的 Enter）即无界自递归栈溢出（复扫 P0-1，
       XDialog 同根同修）。 */
    XClass_Parent(XDialog, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 图标区绘制（对标 Qt 6.8 qmessagebox.cpp
 *         QMessageBoxPrivate::standardIcon + setupLayout：图标按
 *         PM_MessageBoxIconSize 尺寸显示于文本左侧、内容区顶部对
 *         齐。此前 setIcon 仅存 m_icon、全文件无绘制调用，四种级别
 *         标准图标全部不渲染——夜间台账 #19）。图标来源优先级与
 *         Qt 一致：setIconPixmap 自定义位图优先，其次按 m_icon 分
 *         级取样式标准图标（XMessageBox_standardIcon → 样式
 *         SP_MessageBox* 图标，调用方持有，画后即删）。 */
static void xmsg_drawIcon(XMessageBox* box, XEvent* event)
{
    XImage* image;
    XPainter painter;
    XPoint offset;
    XIcon* icon = NULL;
    int top;
    if (!box || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    if (!xmsg_hasIconArea(box)) return;
    image = XWidget_paintImage((XWidget*)box);
    if (!image) return;
    if (box->m_iconPixmap) {
        XPixmap pm;
        icon = XIcon_create();
        if (!icon) return;
        XPixmap_init(&pm);
        XPixmap_init_image(&pm, (const XImage*)box->m_iconPixmap, 0);
        XIcon_init_pixmap(icon, &pm);
        XPixmap_deinit_base(&pm);
    } else {
        icon = XMessageBox_standardIcon(box->m_icon);
    }
    if (!icon) return;
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, image)) {
        offset = XWidget_paintOffset((XWidget*)box);
        if (offset.x != 0 || offset.y != 0)
            XPainter_translate(&painter, (float)offset.x, (float)offset.y);
        top = xmsg_contentTop(box);
        XIcon_paint(icon, &painter, 16, top, 32, 32,
                    (uint32_t)(XAlignment_Left | XAlignment_Top),
                    XIconMode_Normal, XIconState_Off);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XIcon_delete_base(icon);
}

/** @brief 绘制：先静态父调用 XDialog 面板绘制，再叠画图标区。 */
static void VXMessageBox_paintEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (!self || !event) return;
    /* 面板底色/描边/标题沿用 XDialog 实现（XClass_Parent 静态父调
       用，避免经对象虚表再分派回本重载）。 */
    XClass_Parent(XDialog, EXWidget_PaintEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    xmsg_drawIcon(box, event);
}

XVtable* XMessageBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMessageBox)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_messageBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXMessageBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXMessageBox_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMessageBox_deinit);
    return XVTABLE_DEFAULT;
}

void XMessageBox_init(XMessageBox* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMessageBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_icon = (int)XMessageBoxIcon_NoIcon;
    self->m_inExec = false;
    self->m_text = XString_create();
    self->m_title = XString_create();
    self->m_detailedText = XString_create();
    self->m_informativeText = XString_create();
    self->m_options = 0;
#if XDIALOGBUTTONBOX_ON
    self->m_buttonBox = XDialogButtonBox_create(self, 0);
    if (self->m_buttonBox) {
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_accepted_signal(self->m_buttonBox),
            (XObject*)self, xmsg_acceptedSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_rejected_signal(self->m_buttonBox),
            (XObject*)self, xmsg_rejectedSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_clicked_signal(
                self->m_buttonBox, NULL),
            (XObject*)self, xmsg_clickedSlot, XConnectionType_Direct);
    }
    self->m_standards = XVector_Create(int);
#endif
#if XLABEL_ON
    self->m_textLabel = XLabel_create(self, 0);
    if (self->m_textLabel)
        XLabel_setText_2(self->m_textLabel, "");
#endif
    XWidget_resize(self, 320, 140);
}

XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XMessageBox* self =
        (XMessageBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMessageBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本与图标 ==================== */

void XMessageBox_setText(XMessageBox* self, const char* utf8)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setText_2(self->m_textLabel, utf8 ? utf8 : "");
    if (!self->m_text) self->m_text = XString_create();
    if (self->m_text)
        XString_assign_utf8(self->m_text, utf8 ? utf8 : "");
}

const char* XMessageBox_text(const XMessageBox* self)
{
    if (!self || !self->m_textLabel)
        return "";
    return self->m_text ? XString_toUtf8(self->m_text) : "";
}

void XMessageBox_setTitle(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, utf8 ? utf8 : "");
    /* 对标 Qt：标题同步到窗口（此前仅存内部字符串，原生标题不变）。 */
    XWidget_setWindowTitle((XWidget*)self,
                           XString_create_utf8(utf8 ? utf8 : ""));
    /* 标题条出现/消失改变内容区起点（对标 Qt 标题栏下的内容布局）。 */
    xmsg_setupText(self);
    xmsg_setupCheckBox(self);
    XWidget_update((XWidget*)self);
}

const char* XMessageBox_title(const XMessageBox* self)
{
    {
        const char* text;
        if (!self || !self->m_title) return "";
        text = XString_toUtf8(self->m_title);
        return text ? text : "";
    }
}

void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon)
{
    if (!self) return;
    if (self->m_icon == (int)icon) return;
    self->m_icon = (int)icon;
    /* 图标区存在性/内容随分级变化（对标 Qt setIcon → updateIcon）。 */
    xmsg_setupText(self);
    XWidget_update((XWidget*)self);
}

XMessageBoxIcon XMessageBox_icon(const XMessageBox* self)
{
    return self ? (XMessageBoxIcon)self->m_icon
                : XMessageBoxIcon_NoIcon;
}

/* ==================== 按钮管理 ==================== */

void XMessageBox_setStandardButtons(XMessageBox* self, int buttons)
{
    if (!self || !self->m_buttonBox) return;
    XDialogButtonBox_setStandardButtons(self->m_buttonBox, buttons);
    xmsg_setupButtonBox(self);
}

int XMessageBox_standardButtons(const XMessageBox* self)
{
    if (!self || !self->m_buttonBox) return 0;
    return XDialogButtonBox_standardButtons(self->m_buttonBox);
}

XAbstractButton* XMessageBox_button(const XMessageBox* self,
                                    XDialogButtonBoxStandardButton which)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, which);
}

XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self)
{
    return self ? self->m_clicked : NULL;
}

/* ==================== 模态执行 ==================== */

XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self)
{
    XWidget* parent;
    if (!self) return XDialogButtonBoxStandard_NoButton;
    parent = (XWidget*)XObject_parent((XObject*)self);
    XWidget_show((XWidget*)self);
    /* 显示即标脏（根因同 XDialog_exec 注）：information/warning 等
       静态便捷路径的 MessageBox 以 parent+flags=0 构造为子控件形态，
       show 不调度重绘，exec 面板永远不上屏。 */
    XWidget_update((XWidget*)self);
    self->m_inExec = true;
    self->m_clicked = NULL;
    while (self->m_inExec)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    XWidget_hide((XWidget*)self);
    /* 隐藏后标脏原矩形，清除屏幕残影（同 XDialog_done 注）。 */
    XWidget_update((XWidget*)self);
    if (self->m_clicked)
        return XDialogButtonBox_standardButton(self->m_buttonBox,
                                               self->m_clicked);
    return XDialogButtonBoxStandard_NoButton;
}

/* ==================== 静态便捷方法 ==================== */

static XDialogButtonBoxStandardButton xmsg_runStatic(
    XWidget* parent, const char* title, const char* text, int buttons,
    int icon)
{
    XMessageBox* box = XMessageBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    XDialogButtonBoxStandardButton result;
    if (!box) return XDialogButtonBoxStandard_NoButton;
    XMessageBox_setTitle(box, title);
    XMessageBox_setText(box, text);
    XMessageBox_setIcon(box, (XMessageBoxIcon)icon);
    XMessageBox_setStandardButtons(box, buttons);
    result = XMessageBox_exec(box);
    XMessageBox_delete_base(box);
    return result;
}

XDialogButtonBoxStandardButton XMessageBox_information(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Information);
}

XDialogButtonBoxStandardButton XMessageBox_warning(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Warning);
}

XDialogButtonBoxStandardButton XMessageBox_critical(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Critical);
}

XDialogButtonBoxStandardButton XMessageBox_question(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Question);
}

void XMessageBox_about(XWidget* parent, const char* title,
                       const char* text)
{
    xmsg_runStatic(parent, title, text, (int)XDialogButtonBoxStandard_Ok,
                   (int)XMessageBoxIcon_Information);
}







































/* ==================== 补充文本 ==================== */

void XMessageBox_setDetailedText(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_detailedText) self->m_detailedText = XString_create();
    if (self->m_detailedText)
        XString_assign_utf8(self->m_detailedText, utf8 ? utf8 : "");
}

const char* XMessageBox_detailedText(const XMessageBox* self)
{
    if (!self || !self->m_detailedText) return "";
    return XString_toUtf8(self->m_detailedText);
}

void XMessageBox_setInformativeText(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_informativeText)
        self->m_informativeText = XString_create();
    if (self->m_informativeText)
        XString_assign_utf8(self->m_informativeText, utf8 ? utf8 : "");
}

const char* XMessageBox_informativeText(const XMessageBox* self)
{
    if (!self || !self->m_informativeText) return "";
    return XString_toUtf8(self->m_informativeText);
}

/* ==================== 按钮管理 ==================== */

void XMessageBox_addButton(XMessageBox* self, XAbstractButton* button,
                           int role)
{
    if (!self || !self->m_buttonBox || !button) return;
    XDialogButtonBox_addButton(self->m_buttonBox, button, role);
}

XAbstractButton* XMessageBox_addButton_2(XMessageBox* self,
                                         const char* text, int role)
{
    XPushButton* btn;
    if (!self || !self->m_buttonBox || !text) return NULL;
    btn = XDialogButtonBox_addButton_2(self->m_buttonBox, text, role);
    return (XAbstractButton*)btn;
}

XAbstractButton* XMessageBox_addButton_3(XMessageBox* self, int button)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_addButton_3(
        self->m_buttonBox, button);
}

void XMessageBox_setDefaultButton(XMessageBox* self,
                                  XAbstractButton* button)
{
    if (self) self->m_defaultButton = button;
}

void XMessageBox_setDefaultButton_2(XMessageBox* self, int button)
{
    XAbstractButton* btn;
    if (!self) return;
    btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                    button);
    if (!btn && self->m_buttonBox) {
        XDialogButtonBox_addButton_3(self->m_buttonBox, button);
        btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                        button);
    }
    if (btn) self->m_defaultButton = btn;
}

XAbstractButton* XMessageBox_defaultButton(const XMessageBox* self)
{ return self ? self->m_defaultButton : NULL; }

void XMessageBox_setEscapeButton(XMessageBox* self, XAbstractButton* button)
{
    if (self) self->m_escapeButton = button;
}

void XMessageBox_setEscapeButton_2(XMessageBox* self, int button)
{
    XAbstractButton* btn;
    if (!self) return;
    btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                    button);
    if (!btn && self->m_buttonBox) {
        XDialogButtonBox_addButton_3(self->m_buttonBox, button);
        btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                        button);
    }
    if (btn) self->m_escapeButton = btn;
}

XAbstractButton* XMessageBox_escapeButton(const XMessageBox* self)
{ return self ? self->m_escapeButton : NULL; }

XVector* XMessageBox_buttons(const XMessageBox* self)
{
    const XVector* src;
    XVector* out;
    size_t i;
    size_t n;
    if (!self || !self->m_buttonBox) return NULL;
    src = XDialogButtonBox_buttons(self->m_buttonBox);
    if (!src) return NULL;
    n = XVector_size_base((const XContainer*)src);
    out = XVector_Create(XAbstractButton*);
    if (!out) return NULL;
    for (i = 0; i < n; ++i) {
        XAbstractButton* b = XVector_At_Base(src, (int64_t)i,
                                             XAbstractButton*);
        XVector_push_back_1_base(out, &b);
    }
    return out;
}

int XMessageBox_standardButton(const XMessageBox* self,
                               XAbstractButton* button)
{
    /* 对标 Qt QMessageBox::standardButton：标准值登记在按钮盒的
     * m_buttons/m_standards 平行表里，直接委托反查（自定义按钮返回
     * NoButton）。此前走自持 m_standards 向量——该向量 init 创建后
     * 从未写入，恒为空，任何按钮都反查成 NoButton。 */
    if (!self || !self->m_buttonBox)
        return (int)XDialogButtonBoxStandard_NoButton;
    return (int)XDialogButtonBox_standardButton(self->m_buttonBox, button);
}

void XMessageBox_setOptions(XMessageBox* self, int options)
{
    if (self) self->m_options = options;
}

int XMessageBox_options(const XMessageBox* self)
{ return self ? self->m_options : 0; }

bool XMessageBox_testOption(const XMessageBox* self, int option)
{
    return self ? ((self->m_options & option) != 0) : false;
}

void XMessageBox_setOption(XMessageBox* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

void* XMessageBox_buttonClicked_signal(XMessageBox* self,
                                       XAbstractButton* button)
{
    (void)self; (void)button;
    return (void*)(size_t)XMessageBox_buttonClicked_signal;
}

/* ==================== 复选框 / 图标位图 / 文本呈现 ==================== */

#if XCHECKBOX_ON
void XMessageBox_setCheckBox(XMessageBox* self, XCheckBox* checkBox)
{
    if (!self || self->m_checkBox == checkBox) return;
    if (self->m_checkBox) {
        XCheckBox_delete_base((XClass*)self->m_checkBox);
        self->m_checkBox = NULL;
    }
    self->m_checkBox = checkBox;
    if (self->m_checkBox) {
        /* 收编：改为消息框子控件并显示在文本与按钮盒之间。 */
        XWidget_setParentPlain((XWidget*)self->m_checkBox, (XWidget*)self);
        XWidget_show((XWidget*)self->m_checkBox);
        xmsg_setupCheckBox(self);
    }
}

XCheckBox* XMessageBox_checkBox(const XMessageBox* self)
{ return self ? self->m_checkBox : NULL; }
#endif /* XCHECKBOX_ON */

void XMessageBox_setIconPixmap(XMessageBox* self, const XImage* pixmap)
{
    if (!self) return;
    if (!pixmap) {
        if (self->m_iconPixmap) {
            XImage_delete_base((XClass*)self->m_iconPixmap);
            self->m_iconPixmap = NULL;
            /* 清除后图标列可能消失，文本回填让位（对标 Qt setPixmap
               (QPixmap()) → updateIcon 布局刷新）。 */
            xmsg_setupText(self);
            XWidget_update((XWidget*)self);
        }
        return;
    }
    if (!self->m_iconPixmap) {
        self->m_iconPixmap = XImage_create();
        if (!self->m_iconPixmap) return;
    }
    XCopy(self->m_iconPixmap, (const XClass*)pixmap);
    /* 对标 Qt setPixmap → 图标列布局与重绘（文本为图标让位）。 */
    xmsg_setupText(self);
    XWidget_update((XWidget*)self);
}

const XImage* XMessageBox_iconPixmap(const XMessageBox* self)
{ return self ? self->m_iconPixmap : NULL; }

void XMessageBox_setTextFormat(XMessageBox* self, XLabelTextFormat format)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setTextFormat(self->m_textLabel, format);
}

XLabelTextFormat XMessageBox_textFormat(const XMessageBox* self)
{
    if (!self || !self->m_textLabel) return XLabelTextFormat_AutoText;
    return XLabel_textFormat(self->m_textLabel);
}

void XMessageBox_setTextInteractionFlags(XMessageBox* self,
                                         XLabelTextInteractionFlags flags)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setTextInteractionFlags(self->m_textLabel, flags);
}

XLabelTextInteractionFlags XMessageBox_textInteractionFlags(
    const XMessageBox* self)
{
    if (!self || !self->m_textLabel) return 0;
    return XLabel_textInteractionFlags(self->m_textLabel);
}

/* ==================== 按钮角色 / 移除 / 文本 ==================== */

XMessageBoxButtonRole XMessageBox_buttonRole(const XMessageBox* self,
                                             XAbstractButton* button)
{
    if (!self || !self->m_buttonBox || !button)
        return XMessageBoxButtonRole_InvalidRole;
    return (XMessageBoxButtonRole)XDialogButtonBox_buttonRole(
        self->m_buttonBox, button);
}

void XMessageBox_removeButton(XMessageBox* self, XAbstractButton* button)
{
    if (!self || !self->m_buttonBox || !button) return;
    XDialogButtonBox_removeButton(self->m_buttonBox, button);
    if (self->m_defaultButton == button) self->m_defaultButton = NULL;
    if (self->m_escapeButton == button) self->m_escapeButton = NULL;
    if (self->m_clicked == button) self->m_clicked = NULL;
    xmsg_setupButtonBox(self);
}

/** @brief 按标准按钮值取按钮盒中对应按钮（内部工具）。 */
static XAbstractButton* xmsg_standardButtonAt(XMessageBox* self, int button)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, (XDialogButtonBoxStandardButton)button);
}

XString* XMessageBox_buttonText(const XMessageBox* self, int button)
{
    XString* out;
    const XString* text;
    XAbstractButton* btn = xmsg_standardButtonAt((XMessageBox*)self, button);
    out = XString_create();
    if (!out) return NULL;
    text = btn ? XAbstractButton_text(btn) : NULL;
    if (text) XString_assign(out, text);
    else XString_assign_utf8(out, "");
    return out;
}

void XMessageBox_setButtonText(XMessageBox* self, int button,
                               const XString* text)
{
    XAbstractButton* btn = xmsg_standardButtonAt(self, button);
    if (!btn) return;
    if (text) XAbstractButton_setText(btn, text);
    else XAbstractButton_setText_2(btn, "");
}

void XMessageBox_setButtonText_2(XMessageBox* self, int button,
                                 const char* utf8)
{
    XAbstractButton* btn = xmsg_standardButtonAt(self, button);
    if (!btn) return;
    XAbstractButton_setText_2(btn, utf8 ? utf8 : "");
}

/* ==================== 静态便捷 ==================== */

void XMessageBox_aboutQt(XWidget* parent, const XString* title)
{
    (void)parent;
    (void)title;
    /* 与 XApplication_aboutQt 一致：XGui 无 Qt 运行时信息，文档化空操作。 */
}

XIcon* XMessageBox_standardIcon(int icon)
{
    XStyle* style;
    int sp;
    switch (icon) {
    case XMessageBoxIcon_Information:
        sp = XStyleSP_MessageBoxInformation; break;
    case XMessageBoxIcon_Warning:
        sp = XStyleSP_MessageBoxWarning; break;
    case XMessageBoxIcon_Critical:
        sp = XStyleSP_MessageBoxCritical; break;
    case XMessageBoxIcon_Question:
        sp = XStyleSP_MessageBoxQuestion; break;
    default:
        return NULL;
    }
    style = XApplication_style();
    if (!style) return NULL;
    return XStyle_standardIcon(style, sp, NULL, NULL);
}

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */