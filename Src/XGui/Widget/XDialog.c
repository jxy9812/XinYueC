/**
 * @file       XDialog.c
 * @brief      对话框控件实现（对标 Qt 6.8 QDialog 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDialog.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiApplication.h"
#include "XEventLoop.h"
#include "XGuiConfig.h"

#include "XCoreApplication.h"
#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XWindowEvent.h"
#include "XImage.h"
#include "XPalette.h"
#include "XContainer.h"
#include "XVector.h"
#include "XPushButton.h"
#include "XTextEdit.h"
#include "XPlainTextEdit.h"
#include "XTextControl.h"
#include "XPainter.h"

#if XWIDGET_ON && XDIALOG_ON

static void xdlg_emitVoid(XDialog* self, size_t signal)
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

static void xdlg_emitFinished(XDialog* self, int result)
{
    XVarList* args = XVarList_Create(XVar(int, result));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XDialog_finished_signal, args, NULL, NULL,
            XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 对话框面板背景绘制（对标 QDialog 由平台回填 palette Window 底）。
 *  @details 框架层根因说明（XWidget.c 不在本次修复所有权内，故在对话框
 *           层自绘面板）：XWidget_paintEvent_default（autofill 路径）把
 *           事件脏区先加 paintOffset 折算到顶层后备存储坐标，再用控件
 *           自身"局部尺寸"去裁剪——任何不在窗口原点的子控件，其填充矩形
 *           都会被错误裁剪（demo 对话框位于 (232,238)、尺寸 320x140，
 *           裁出负高直接整块跳过填充），面板因此永不上屏，文字/按钮
 *           悬浮在未渲染底色上，表现为"对话框弹不出"。本重载按正确
 *           顺序绘制：先在控件局部坐标用局部尺寸裁剪，再平移折算。 */
/** @brief      对话框首显居中到父窗口中央。
 *  @details    对标 QDialogPrivate::adjustPosition（qdialog.cpp:871，
 *              QDialog 首次显示按父窗口居中）：Qt 以父窗口中心
 *              p = mapToGlobal(0,0) + parent->size()/2 为基准，再
 *              p -= size()/2 得全局落点。子控件形态对话框几何为父
 *              系坐标，把全局落点换算回父系坐标必须减去父控件在顶
 *              层窗口内的偏移——此前公式误用加法（pw + (tw-dw)/2），
 *              页偏移被双倍计入，对话框整体被推向右下：文件/颜色
 *              便捷对话框（400/340 高）底缘因此越出 800x600 窗口
 *              底部（实测文件框 window y=256..656，底缘溢出 56px，
 *              确定/取消完全不可见——夜间台账 #27/#28）。顶层对话
 *              框（m_isWindow）交由调用方的平台居中处理，此处跳
 *              过。exec/open 每次显示均居中——demo 弹窗为常驻复用
 *              件，二次打开同样回到中央。 */
static void xdlg_centerToParentWindow(XDialog* self)
{
    XWidget* selfw = (XWidget*)self;
    XWidget* parent;
    XWidget* top;
    XWidget* w;
    int pw = 0;
    int py = 0;
    int dw;
    int dh;
    int tw;
    int th;
    if (!selfw || selfw->m_isWindow) return;
    parent = XWidget_parentWidget(selfw);
    if (!parent) return;
    top = XWidget_topLevelWidget(selfw);
    if (!top || top == selfw) return;
    dw = XWidget_width(selfw);
    dh = XWidget_height(selfw);
    tw = XWidget_width(top);
    th = XWidget_height(top);
    w = parent;
    while (w && w != top) {
        pw += XWidget_x(w);
        py += XWidget_y(w);
        w = XWidget_parentWidget(w);
    }
    /* 对标 qdialog.cpp adjustPosition 落点换算：目标=顶层窗口中心
     * 邻域（全局坐标 (tw-dw)/2, (th-dh)/2），换算回父系坐标减去父
     * 控件偏移 (pw,py)。Qt 的 WM 框架余量 extraw/extrah（10/40）仅
     * 对有原生装饰的窗口有意义，XGui 子控件形态无装饰，取 0。 */
    XWidget_move(selfw,
                 (tw > dw ? (tw - dw) / 2 : 0) - pw,
                 (th > dh ? (th - dh) / 2 : 0) - py);
}

static void VXDialog_paintEvent(XWidget* self, XEvent* event)
{
    XPaintEvent* pe;
    XImage* image;
    XRect rect;
    XPoint offset;
    XPalette palette;
    XColor color;
    int w;
    int h;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    pe = (XPaintEvent*)event;
    image = XWidget_paintImage(self);
    if (!image) return;
    palette = XWidget_palette(self);
    color = XPalette_color(&palette, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
    rect = XPaintEvent_rect(pe);
    w = XWidget_width(self);
    h = XWidget_height(self);
    /* 1) 控件局部坐标：脏区 ∩ 控件矩形。 */
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > w) rect.width = w - rect.x;
    if (rect.y + rect.height > h) rect.height = h - rect.y;
    if (rect.width <= 0 || rect.height <= 0) return;
    /* 2) 平移到顶层后备存储坐标后填充。 */
    offset = XWidget_paintOffset(self);
    rect.x += offset.x;
    rect.y += offset.y;
    XImage_fillRect(image, &rect, XColor_rgba(&color));
    /* 子控件形态对话框画 1px 面板描边：面板色（palette Window）与
     * 页面背景相同时（如 Fusion #F4F6F8 页面）无边框的对话框视觉
     * 不可见——用户实测「弹窗透明啥都没有」实为同色面板+未布局子
     * 控件。对标 QFrame 对话框面板的窗口边框语义。 */
    {
        /* 1px 面板描边用 setPixel 逐点画（XImage 无 drawLine；四边
           各一条水平/垂直线，量小代价可忽略）。 */
        int dw = XWidget_width(self);
        int dh = XWidget_height(self);
        uint32_t frame = 0xFF7A7A7Au;
        XPoint o2 = XWidget_paintOffset(self);
        int px;
        for (px = 0; px < dw; ++px) {
            XImage_setPixel(image, o2.x + px, o2.y, frame);
            XImage_setPixel(image, o2.x + px, o2.y + dh - 1, frame);
        }
        for (px = 0; px < dh; ++px) {
            XImage_setPixel(image, o2.x, o2.y + px, frame);
            XImage_setPixel(image, o2.x + dw - 1, o2.y + px, frame);
        }
    }
    /* 对标 QDialog 作为窗口时平台标题栏显示 windowTitle()：XGui 便
       捷路径对话框为子控件形态（单原生窗口模型，XGui.md §8.0g 声
       明边界），无标题栏可承载窗口标题——最小等价落地：面板顶部带
       内居中绘制 windowTitle 文本（无标题不占位，子控件布局不变）。
       夜间台账 #20：setTitle 后标题不可见。 */
    {
        const XString* title = XWidget_windowTitle(self);
        const char* utf8 = title ? XString_toUtf8(title) : NULL;
        /* 顶层窗口形态有平台标题栏承载标题，不再带内重画（避免双重
           标题）；仅子控件形态补画。 */
        if (utf8 && utf8[0] && !self->m_isWindow) {
            XPainter painter;
            XRect tr;
            XFont font = XWidget_font(self);
            XColor textColor = XPalette_color(&palette,
                                              XPaletteColorGroup_Active,
                                              XPaletteColorRole_WindowText);
            XPoint to = XWidget_paintOffset(self);
            tr.x = to.x + 8;
            tr.y = to.y + 4;
            tr.width = w - 16 > 0 ? w - 16 : 0;
            tr.height = 18;
            XPainter_init(&painter, NULL);
            if (XPainter_begin_image(&painter, image)) {
                XPainter_setFont(&painter, &font);
#if XPAINTER_TEXTLAYOUT_ON
                XPainter_drawTextRect(&painter, &tr,
                                      XPAINTER_TEXT_ALIGN_HCENTER |
                                      XPAINTER_TEXT_ALIGN_VCENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                      utf8, XColor_rgba(&textColor));
#else
                XPainter_drawText(&painter, tr.x + 2,
                                  tr.y + tr.height - 6, utf8,
                                  XColor_rgba(&textColor));
#endif
                XPainter_end(&painter);
            }
            XFont_deinit_base(&font);
            XPainter_deinit(&painter);
        }
    }
}

/** @brief      多行文本编辑判定（对话框 Enter 让键豁免）。
 *  @details    对标 Qt 6.8 qdialog.cpp keyPressEvent：焦点在
 *              QTextEdit/QPlainTextEdit 类多行编辑器时 Enter 交给
 *              编辑器换行，对话框不得抢去派发默认按钮。XGui 侧按
 *              vtable 精确比对：XTextEdit/XPlainTextEdit。注意
 *              XTextControl（多行编辑的模型控制器）继承 XObject 而非
 *              XWidget，不可能成为焦点控件，不参与判定。 */
static bool dialog_focusIsMultilineEditor(const XWidget* widget)
{
    XVtable* vtable;
    if (!widget) return false;
    vtable = XClassGetVtable(widget);
    return vtable == XTextEdit_class_init() ||
           vtable == XPlainTextEdit_class_init();
}

/** @brief      先序遍历对话框子树找默认按钮。
 *  @details    对标 Qt 6.8 QDialog::keyPressEvent 无显式默认时回落
 *              「第一个可见可用 autoDefault 按钮」（findChildren 顺
 *              序=插入序，先序遍历同序）。显式 setDefault 的按钮全
 *              树最高优先（Qt d->defaultButton 语义），命中即短路。
 *              可见性取有效可见（XWidget_isVisible 已对齐 Qt 的
 *              isVisible 语义），可用性按 WA_Disabled 位（同 Qt
 *              isEnabled）。 */
static void dialog_walkForDefaultButton(XObject* object,
                                        XPushButton** explicitDefault,
                                        XPushButton** firstAutoDefault)
{
    int count;
    int i;
    if (!object || *explicitDefault) return;
    if (XClassGetVtable(object) == XPushButton_class_init()) {
        XPushButton* button = (XPushButton*)object;
        if (XWidget_isVisible((XWidget*)button) &&
            XWidget_isEnabled((XWidget*)button)) {
            if (button->m_defaultButton) {
                *explicitDefault = button;
                return;
            }
            /* 对标 Qt 6.8 QPushButton::autoDefault 语义：对话框内的
             * QPushButton 默认即 autoDefault（autoDefaultControl 是
             * 按父链 windowType==Dialog 判定，而 XGui 存在以
             * parent+flags=0 构造的子控件形态对话框——如 XMessageBox
             * 便捷路径——其内按钮按该判定会失去 autoDefault）。本遍
             * 历起点即对话框，子树内按钮仅显式 Off 才退出候选。 */
            if (!*firstAutoDefault &&
                button->m_autoDefault != XPushButtonAutoDefault_Off)
                *firstAutoDefault = button;
        }
    }
    if (object->m_children) {
        count = XVector_size_base((const XContainer*)object->m_children);
        for (i = 0; i < count; ++i) {
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            dialog_walkForDefaultButton(children[i],
                                        explicitDefault, firstAutoDefault);
            if (*explicitDefault) return;
        }
    }
}

/** @brief      解析对话框当前生效的默认按钮（显式优先，回落首个
 *              可见可用 autoDefault）；无则 NULL。 */
static XPushButton* dialog_defaultButton(XDialog* dialog)
{
    XPushButton* explicitDefault = NULL;
    XPushButton* firstAutoDefault = NULL;
    dialog_walkForDefaultButton((XObject*)dialog,
                                &explicitDefault, &firstAutoDefault);
    return explicitDefault ? explicitDefault : firstAutoDefault;
}

/** @brief      对话框子树内是否已持有应用焦点控件。 */
static bool dialog_containsFocus(const XDialog* self)
{
    const XWidget* focus = XWidget_appFocusWidget();
    const XWidget* w;
    if (!focus) return false;
    for (w = focus; w; w = XWidget_parentWidget(w))
        if ((const XWidget*)self == w) return true;
    return false;
}

/** @brief      对话框可见后抢占初始焦点（对标 Qt showModal 的
 *              initialFocusWidget 落点：默认按钮优先，无则对话框
 *              自身）。不抢焦点时平台键按原生窗口树投递，对话框收
 *              不到任何按键（配合 VXGuiApplication_notify 的键重定
 *              向注释）。 */
static void dialog_grabInitialFocus(XDialog* self)
{
    XPushButton* button;
    if (!self || dialog_containsFocus(self)) return;
    button = dialog_defaultButton(self);
    XWidget_setFocusReason(button ? (XWidget*)button : (XWidget*)self,
                           XFocusReason_Other);
}

/** @brief      先序收集对话框子树内可 Tab 聚焦的子控件。
 *  @details    对标 Qt 6.8 qapplication.cpp
 *              focusNextPrevChild_helper 的候选判定：enabled、可见、
 *              focusPolicy 含 TabFocus 位（StrongFocus 含该位）。文档
 *              序（先序）即 Qt 焦点链顺序。 */
static void xdlg_collectTabCandidates(XObject* object, XVector* out)
{
    int count;
    int i;
    if (!object || !out) return;
    if (object->is_widget) {
        XWidget* w = (XWidget*)object;
        if (XWidget_isEnabled(w) && XWidget_isVisible(w) &&
            (XWidget_focusPolicy(w) & XWidgetFocusPolicy_TabFocus))
            XVector_push_back_1_base(out, &w);
    }
    if (object->m_children) {
        count = XVector_size_base((const XContainer*)object->m_children);
        for (i = 0; i < count; ++i) {
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            xdlg_collectTabCandidates(children[i], out);
        }
    }
}

/** @brief      模态子树内 Tab/Shift+Tab 焦点环绕（不越出对话框）。
 *  @details    对标 Qt 6.8 QWidget::focusNextPrevChild：QDialog 是独
 *              立原生窗口，焦点链天然局限在对话框子树内。XGui 单原
 *              生窗口模型下，XWidget 事件层 Tab 兜底按顶层窗口全域
 *              文档序移动焦点（XWidget_focusChainTarget），Tab×N 会
 *              越出模态子树（夜间台账 #23：Tab×2 后焦点落在页面触
 *              发按钮上，Esc 随即失效、对话框滞留）。本函数把移动限
 *              制在对话框子树内并环绕。注意：按键自子控件沿父链上
 *              抛到达本对话框时，子控件自身的窗口域兜底可能已消费
 *              Tab——按钮盒路径由盒的显式 Tab 环链（XWidget_setTab
 *              Order 闭环，见 XDialogButtonBox.c xdb_relayout）先行
 *              在子树内消费；本函数兜住其余情形（焦点在对话框自身
 *              或非 Tab 候选控件上）。 */
static bool xdlg_focusNextPrevChild(XDialog* self, bool next)
{
    XVector* list;
    XWidget* current;
    XWidget* target = NULL;
    int count;
    int i;
    int idx = -1;
    if (!self) return false;
    list = XVector_Create(XWidget*);
    if (!list) return false;
    xdlg_collectTabCandidates((XObject*)self, list);
    count = (int)XVector_size_base((const XContainer*)list);
    if (count == 0) {
        XVector_delete_base((XClass*)list);
        return false;
    }
    current = XWidget_appFocusWidget();
    for (i = 0; i < count; ++i) {
        if (XVector_At_Base(list, (int64_t)i, XWidget*) == current) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        /* 焦点不在子树内（如初始焦点落在对话框自身）：Qt 语义从链
         * 首（Tab）/链尾（Shift+Tab）进入。 */
        target = XVector_At_Base(list, next ? 0 : (int64_t)count - 1,
                                 XWidget*);
    } else {
        int step = next ? 1 : count - 1;
        target = XVector_At_Base(list, (int64_t)((idx + step) % count),
                                 XWidget*);
    }
    XVector_delete_base((XClass*)list);
    if (!target || target == current) return false;
    XWidget_setFocusReason(target, next ? XFocusReason_Tab
                                        : XFocusReason_Backtab);
    return true;
}

static void VXDialog_keyPressEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event &&
        XEvent_type(event) == XEVENT_TYPE_KEY_PRESS) {
        /* 对标 QDialog::keyPressEvent：Escape 触发 reject()。 */
        if (((XKeyEvent*)event)->m_key == (int)XKey_Escape) {
            XDialog_reject(dialog);
            XEvent_accept(event);
            return;
        }
        /* 对标 Qt 6.8 qpushbutton.cpp QPushButton::keyPressEvent：焦
           点落在 autoDefault 按钮上时，Enter/Return 点击的是聚焦按
           钮本身而非默认按钮。XGui 子控件形态对话框以 parent+flags=0
           构造，XPushButton_autoDefault 的「父链 windowType==Dialog」
           判定失效（Auto 恒解为 false），聚焦取消钮后回车仍触发确
           定（夜间台账 #24）——此处按 dialog_walkForDefaultButton 同
           口径直接判 m_autoDefault != Off。 */
        if (((XKeyEvent*)event)->m_key == (int)XKey_Tab ||
            ((XKeyEvent*)event)->m_key == (int)XKey_Backtab) {
            /* 对标 QWidget::event 的 Tab/Shift+Tab 焦点遍历，限定在
               模态子树内环绕（详见 xdlg_focusNextPrevChild 注）。
               Shift 修饰按 Qt 惯例把 Tab 反向为 Backtab。 */
            int key = ((XKeyEvent*)event)->m_key;
            int mods = (int)((XKeyEvent*)event)->m_modifiers;
            bool next = (key == (int)XKey_Tab) ==
                        ((mods & (int)XKeyboardModifier_ShiftModifier) == 0);
            if ((mods & ~(int)XKeyboardModifier_ShiftModifier) == 0 &&
                xdlg_focusNextPrevChild(dialog, next)) {
                XEvent_accept(event);
                return;
            }
        }
        if (((XKeyEvent*)event)->m_key == (int)XKey_Return ||
            ((XKeyEvent*)event)->m_key == (int)XKey_Enter) {
            XWidget* focus = XWidget_focusWidget(self);
            if (!dialog_focusIsMultilineEditor(focus)) {
                XPushButton* button = dialog_defaultButton(dialog);
                /* 焦点在可见可用 autoDefault 按钮上：点击聚焦按钮
                   （Qt QPushButton::keyPressEvent 语义，见上注）。 */
                if (focus &&
                    XClassGetVtable(focus) == XPushButton_class_init()) {
                    XPushButton* focused = (XPushButton*)focus;
                    if (XWidget_isVisible(focus) &&
                        XWidget_isEnabled(focus) &&
                        focused->m_autoDefault != XPushButtonAutoDefault_Off)
                        button = focused;
                }
                if (button) {
                    XPushButton_click(button);
                    XEvent_accept(event);
                    return;
                }
            }
        }
    }
    /* 对标 QDialog::keyPressEvent 非 Esc 分支静态调用基类实现
       （QWidget::keyPressEvent 默认 ignore 以便沿父链传播）。
       此前经 XWidget_keyPressEvent_base 转发：该 _base 入口按对象
       虚表再分派回最派生重载 VXDialog_keyPressEvent，非 Esc 按键
       即形成无界自递归栈溢出（复扫 P0-1；XWizard/XInputDialog/
       XColorDialog/XFileDialog/XProgressDialog/XErrorMessage 均未
       覆写 keyPress，收到按键全数命中）。现按 XDockWidget/
       XToolBar 的 XClass_Parent 口径静态取 XWidget 类虚表 keyPress
       槽位（即 XWidget 本类默认实现 XWidget_ignoreEvent_default，
       与 VXWidget_event 分派到 XWidget 本类时所用同一层）。 */
    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

XVtable* XDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDialog)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXDialog_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXDialog_keyPressEvent);
    return XVTABLE_DEFAULT;
}

void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt：QDialog 恒为顶层窗口、自动回填 palette Window 背景。
     * 本框架允许以 parent+flags=0 构造出"子控件形态"对话框（demo
     * 对话框页即此形态），子控件默认 autoFillBackground=false 且
     * XDialog 无自绘面板——对话框除按钮盒外整块透明，文本黑字落在
     * 未渲染底上即"弹不出"。此处开启背景回补面板底色。 */
    XWidget_setAutoFillBackground((XWidget*)self, true);
    /* R-81 根因：m_modal 默认 true 与 Qt QDialog 默认 false 相反，且
       show() 不消费该属性（只有 exec/open 模态化），isModal() 查询值
       与实际行为不自洽。对标 Qt 6.8.3：modal 默认 false，仅 exec（无
       条件应用模态）/open（setModal(true)）时模态化。 */
    self->m_modal = false;
    self->m_result = 0;
    self->m_inExec = false;
    self->m_sizeGripEnabled = false;
}

XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XDialog* self = (XDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XDialog_exec(XDialog* self)
{
    if (!self) return 0;
    self->m_inExec = true;
    xdlg_centerToParentWindow(self);
    XWidget_show((XWidget*)self);
    /* 显示即标脏本对话框矩形：子控件形态的对话框（flags 无 Window 位）
     * 走 XWidget_setVisible 的非窗口分支，该分支不调度重绘（只有顶层
     * 窗口分支才有 show→update），脏区合成器只重画脏矩形，导致对话框
     * 已 visible 却永远不上屏（复现：demo 对话框页九键中六个非阻塞/
     * 常驻对话框点击后屏幕无任何面板墨迹）。此处对窗口形态是冗余的
     * 一次重复标脏，无副作用。 */
    XWidget_update((XWidget*)self);
    /* 对标 QDialog::exec（Qt 6.8.3 qdialog.cpp）：exec 期间无条件
       应用模态（setWindowModality(ApplicationModal)），不受 modal
       属性默认值影响——m_modal 默认改 false 后若仍以此门禁，存量
       未调 setModal(true) 的 exec 调用将静默失去模态。 */
    XWidget_setApplicationModalWidget((XWidget*)self);
    /* show 后强制激活布局：子控件形态下布局的挂起激活不保证随 show
     * 走到（XBoxLayout 子控件曾零几何不绘制）。幂等。 */
    XWidget_updateGeometry((XWidget*)self);
    /* 对标 QDialog::exec：阻塞于事件循环直到 done()。此前仅处理一批
       事件即返回，模态语义不成立。 */
    dialog_grabInitialFocus(self);
    while (self->m_inExec) {
        XCoreApplication_processEvents(XEventLoop_AllEvents |
                                       XEventLoop_WaitForMoreEvents);
        if (self->m_inExec)
            XWidget_setApplicationModalWidget((XWidget*)self);
    }
    if (XWidget_applicationModalWidget() == (XWidget*)self)
        XWidget_setApplicationModalWidget(NULL);
    return self->m_result;
}

void XDialog_done(XDialog* self, int result)
{
    if (!self) return;
    self->m_result = result;
    self->m_inExec = false;
    if (XWidget_applicationModalWidget() == (XWidget*)self)
        XWidget_setApplicationModalWidget(NULL);
    XWidget_setVisible((XWidget*)self, false);
    /* 隐藏后标脏原矩形：子控件形态对话框走非窗口隐藏分支，无重绘
       调度，屏幕残留对话框最后一帧鬼影；把矩形折算进顶层脏区后，
       合成器按可见内容重画该区域（对话框已隐藏即父级/邻居内容）。 */
    XWidget_update((XWidget*)self);
    xdlg_emitFinished(self, result);
}

void XDialog_accept(XDialog* self)
{
    if (!self) return;
    self->m_result = 1;
    XDialog_done(self, 1);
    xdlg_emitVoid(self, (size_t)XDialog_accepted_signal);
}

void XDialog_reject(XDialog* self)
{
    if (!self) return;
    self->m_result = 0;
    XDialog_done(self, 0);
    xdlg_emitVoid(self, (size_t)XDialog_rejected_signal);
}

int XDialog_result(const XDialog* self) { return self ? self->m_result : 0; }
void XDialog_setResult(XDialog* self, int result) { if (self) self->m_result = result; }
void XDialog_setModal(XDialog* self, bool modal)
{
    if (!self || self->m_modal == modal) return;
    self->m_modal = modal;
    /* 可见的对话框即时生效模态登记（对标 open()/setModal 语义）。 */
    if (modal && XWidget_isVisible((XWidget*)self))
        XWidget_setApplicationModalWidget((XWidget*)self);
}
bool XDialog_isModal(const XDialog* self) { return self ? self->m_modal : false; }

void XDialog_open(XDialog* self)
{
    if (!self) return;
    XDialog_setModal(self, true);
    xdlg_centerToParentWindow(self);
    XWidget_show((XWidget*)self);
    /* 显示即标脏本对话框矩形（根因同 XDialog_exec 注）：子控件形态
     * 对话框 show 不产生脏区，open 后对话框永远不可见，需在此补一次
     * update 让下一帧把面板/文本/按钮真实画上屏幕。 */
    XWidget_update((XWidget*)self);
    XWidget_setApplicationModalWidget((XWidget*)self);
    dialog_grabInitialFocus(self);
}

void XDialog_setSizeGripEnabled(XDialog* self, bool enable)
{ if (self) self->m_sizeGripEnabled = enable; }
bool XDialog_isSizeGripEnabled(const XDialog* self)
{ return self ? self->m_sizeGripEnabled : false; }

void* XDialog_accepted_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_accepted_signal; }
void* XDialog_rejected_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_rejected_signal; }
void* XDialog_finished_signal(XDialog* self, int result)
{ (void)self; (void)result; return (void*)(size_t)XDialog_finished_signal; }








#endif /* XWIDGET_ON && XDIALOG_ON */