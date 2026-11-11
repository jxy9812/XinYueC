/**
 * @file       XSizeGrip.c
 * @brief      窗口尺寸拖拽把手控件实现（对标 Qt 6.8 QSizeGrip 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XSizeGrip.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XCursor.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XSIZEGRIP_ON

/* ==================== 拖拽会话状态 ==================== */
/* 对标 Qt qsizegrip.cpp 的 QSizeGripPrivate（d->p / d->r / d->gotMousePress
 * / m_corner）。XSizeGrip.h 为公共契约头（结构体仅 m_base + 借用顶层），
 * 拖拽会话状态按模块级单例收拢在实现文件内——鼠标单点语义下同一时刻至多
 * 一路拖拽，与 g_mouseGrabWidget 的模块级登记口径一致。 */
static bool g_sizeGripDragActive = false;      /**< 按下后进入拖拽会话。 */
static XPoint g_sizeGripPressGlobal;           /**< 按下时屏幕全局坐标。 */
static XRect g_sizeGripStartGeometry;          /**< 按下时顶层窗口几何。 */
static bool g_sizeGripAtBottom = true;         /**< 手柄位于顶层下半（Qt d->atBottom）。 */
static bool g_sizeGripAtLeft = false;          /**< 手柄位于顶层左半（Qt d->atLeft）。 */

/** @brief 按角落刷新斜向调整光标（对标 Qt 6.8 qsizegrip.cpp:420-424
 *         QSizeGrip::moveEvent 尾段与 QSizeGripPrivate::init 的口径）：
 *         左上/右下角用 SizeFDiag、右上/左下角用 SizeBDiag。
 * @note       Qt 的**文档**（Qt::SizeFDiagCursor = "top-right or
 *             bottom-left"）与**代码**（TopLeft/BottomRight → SizeFDiag）
 *             相反——此处按代码对齐（对齐判据是行为，不是文档措辞）；
 *             判定式 `atBottom != atLeft` 恰是「左上 或 右下」。 */
static void xsg_applyCornerCursor(XSizeGrip* self, bool atBottom, bool atLeft)
{
    XCursor* cursor;
    XCursorShape shape;
    if (!self) return;
    shape = (atBottom != atLeft) ? XCursor_SizeFDiag : XCursor_SizeBDiag;
    cursor = XCursor_create_shape(shape);
    if (!cursor) return;
    XWidget_setCursor((XWidget*)self, cursor);
    XClassDelete(cursor);
}

/** @brief 被观察顶层销毁槽：清借用指针（对标 Qt 用 QPointer<QWidget> tlw
 *         的自动置空）。X 的 m_topLevel 是裸借用指针——不接信号的话顶层
 *         先亡后，xsg_updateTopLevel 会对已释放对象调 removeEventFilter（UAF）。 */
static void xsg_topLevelDestroyedSlot(XObject* receiver, XVarList* args)
{
    XSizeGrip* grip = (XSizeGrip*)receiver;
    (void)args;
    if (grip) grip->m_topLevel = NULL;
}

/** @brief 解析并（重新）挂接顶层控件的事件过滤器（对标
 *         QSizeGripPrivate::updateTopLevelWidget，qsizegrip.cpp:56-69）。 */
static void xsg_updateTopLevel(XSizeGrip* self)
{
    XWidget* tlw;
    if (!self) return;
    tlw = XWidget_topLevelWidget((XWidget*)self);
    if (tlw == self->m_topLevel) return;
    if (self->m_topLevel) {
        XObject_disconnect_1((XObject*)self->m_topLevel,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xsg_topLevelDestroyedSlot);
        XObject_removeEventFilter((XObject*)self->m_topLevel, (XObject*)self);
    }
    self->m_topLevel = tlw;
    if (tlw) {
        XObject_installEventFilter((XObject*)tlw, (XObject*)self);
        /* 顶层销毁时置空借用指针（见 xsg_topLevelDestroyedSlot）。 */
        XObject_connect_1((XObject*)tlw, XSignal(XObject_destroyed_signal),
                          (XObject*)self, xsg_topLevelDestroyedSlot,
                          XConnectionType_Direct);
    }
}

/** @brief 当前手柄所在角落（对标 QSizeGripPrivate::corner，
 *         qsizegrip.cpp:91-106）：手柄原点映射到顶层后按上下/左右半区取角。 */
static void xsg_cornerOf(const XSizeGrip* self, bool* atBottom, bool* atLeft)
{
    XWidget* top;
    XPoint zero;
    XPoint gripPos;
    bool bottom = true;
    bool left = false;
    if (!self) { if (atBottom) *atBottom = true; if (atLeft) *atLeft = false; return; }
    top = XWidget_topLevelWidget((const XWidget*)self);
    if (top) {
        zero.x = 0;
        zero.y = 0;
        gripPos = XWidget_mapTo((const XWidget*)self, top, &zero);
        bottom = gripPos.y >= XWidget_height(top) / 2;
        left = gripPos.x <= XWidget_width(top) / 2;
    }
    if (atBottom) *atBottom = bottom;
    if (atLeft) *atLeft = left;
}

static void VX_sizeGrip_paintEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    int w;
    int h;
    uint32_t mid;
    int i;
    if (!grip || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：右下角斜点阵走 CE_SizeGrip。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_SizeGrip);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled(self) ? XStyleState_Enabled : 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawControl(style, XStyleCE_SizeGrip, &opt, &painter, self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 右下角斜纹三角（对标 QSizeGrip 的风格化绘制）。 */
    for (i = 0; i < w + h; i += 4) {
        XRect dot;
        int px = w - 3 - i;
        int py = h - 3;
        while (px < w - 3 && py < h - 3) {
            ++px;
            ++py;
        }
        if (px < 0 || py < 0) continue;
        XRect_init(&dot, px, py, 2, 2);
        XPainter_fillRect(&painter, &dot, mid);
    }
    XPainter_deinit(&painter);
}

static void VX_sizeGrip_mousePressEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* top;
    if (!grip || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 Qt qsizegrip.cpp:257 mousePressEvent：非左键交父类默认
         * 处理（默认忽略→沿父链传播）。须用 XClass_Parent 直呼父类
         * 实现——XWidget_mousePressEvent_base 是 vtable 派发助手，会
         * 重入本重载无限递归（同 XMdiArea.c 复扫-5 实证）。 */
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    top = XWidget_topLevelWidget(self);
    if (!top) return;
    /* 记录拖拽基线（对标 qsizegrip.cpp:264-266：d->p = 全局坐标、
     * d->r = 顶层 geometry、d->gotMousePress = true）。移动事件以
     * 全局坐标差值算增量，局部坐标随命中接收者改写不可靠。 */
    g_sizeGripDragActive = true;
    g_sizeGripPressGlobal = XMouseEvent_globalPosition(me);
    g_sizeGripStartGeometry = XWidget_geometry(top);
    /* 角落判定（对标 qsizegrip.cpp:91-106 QSizeGripPrivate::corner：
     * 手柄原点映射到顶层，按上下/左右半区取角）。 */
    xsg_cornerOf(grip, &g_sizeGripAtBottom, &g_sizeGripAtLeft);
    /* 对标 Qt 按压建立的隐式抓取（qwidgetwindow.cpp:634 qt_button_down
     * 后续 move/release 直达按下控件）：拖拽中光标必然离开 16x16 手柄，
     * 显式抓取保证后续移动/释放仍送达本控件。 */
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

static void VX_sizeGrip_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* top;
    XPoint np;
    int dx;
    int dy;
    int newW;
    int newH;
    int newX;
    int newY;
    XSize minSize;
    if (!grip || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (!g_sizeGripDragActive ||
        XMouseEvent_buttons(me) != XMouseButton_LeftButton) {
        /* 对标 qsizegrip.cpp:374：无按压基线或左键已释放不动作
         * （同时防御会话状态残留时误改顶层窗口）。 */
        return;
    }
    top = XWidget_topLevelWidget(self);
    if (!top) return;
    np = XMouseEvent_globalPosition(me);
    dx = np.x - g_sizeGripPressGlobal.x;
    dy = np.y - g_sizeGripPressGlobal.y;
    /* 尺寸钳位（对标 qsizegrip.cpp:386-401：ns 先按 dxMax/dyMax 夹到可用
     * 屏幕区，再经 QLayout::closestAcceptableSize(tlw, ns) 夹进控件的
     * min/max 尺寸约束）。本适配无多屏可用区信息，按**顶层控件的
     * minimumSize/maximumSize** 双向钳位——下界等价 closestAcceptableSize
     * 的 minimumSize 分支，上界等价其 maximumSize 分支（此前只钳下界，
     * 设了 setMaximumSize 的窗口能被手柄拖破上限）。 */
    minSize = XWidget_minimumSize(top);
    if (minSize.width < 1) minSize.width = 1;
    if (minSize.height < 1) minSize.height = 1;
    /* 对标 qsizegrip.cpp:382-392：Bottom/Right 角加增量，Top/Left 角
     * 减增量；新几何锚定对角（Bottom-Right 锚定左上角，其余对称），
     * setGeometry 一步到位（旧实现拆成两次 resize 且用局部坐标当
     * 绝对尺寸，永不生效）。 */
    newW = g_sizeGripAtLeft ? g_sizeGripStartGeometry.width - dx
                            : g_sizeGripStartGeometry.width + dx;
    newH = g_sizeGripAtBottom ? g_sizeGripStartGeometry.height + dy
                              : g_sizeGripStartGeometry.height - dy;
    if (newW < minSize.width) newW = minSize.width;
    if (newH < minSize.height) newH = minSize.height;
    {
        XSize maxSize = XWidget_maximumSize(top);
        if (newW > maxSize.width) newW = maxSize.width;
        if (newH > maxSize.height) newH = maxSize.height;
    }
    newX = g_sizeGripAtLeft
               ? g_sizeGripStartGeometry.x +
                     g_sizeGripStartGeometry.width - newW
               : g_sizeGripStartGeometry.x;
    newY = g_sizeGripAtBottom
               ? g_sizeGripStartGeometry.y
               : g_sizeGripStartGeometry.y +
                     g_sizeGripStartGeometry.height - newH;
    XWidget_setGeometry(top, newX, newY, newW, newH);
    XEvent_accept(event);
}

static void VX_sizeGrip_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!grip || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 Qt qsizegrip.cpp:413 mouseReleaseEvent：非左键交父类
         * （XClass_Parent 直呼，同 Press 防重入口径）。 */
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 结束拖拽会话并解除抓取（对标 d->gotMousePress = false;
     * d->p = QPoint()；releaseMouse 仅当 self 为当前抓取者时生效）。 */
    g_sizeGripDragActive = false;
    XWidget_releaseMouse(self);
    XEvent_accept(event);
}

/* ==================== 显隐与顶层窗口状态联动 ==================== */

/** @brief 事件过滤器（对标 QSizeGrip::eventFilter，qsizegrip.cpp:451-465）：
 *         顶层控件窗口状态变为 Maximized/FullScreen 时隐藏手柄，退出这两态
 *         时恢复（Qt 原文：「Don't show the size grip if the tlw is maximized
 *         or in full screen mode.」）。
 * @details  Qt 在同一分支里把 WA_WState_ExplicitShowHide 清位——这次显隐是
 *          框架行为而非用户显式请求，不能让后续 showChildren 把它当成
 *          「用户隐藏过」。本实现照做（XWidget_setAttribute 清位）。
 * @return     恒 false（不吞事件，与 Qt `return QWidget::eventFilter(o, e)`
 *             的基类默认一致）。 */
static bool VX_sizeGrip_eventFilter(XObject* object, XObject* watched,
                                    XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)object;
    XWindowStates state;
    bool wantHide;
    if (!grip || !watched || !event) return false;
    if ((const XWidget*)watched != grip->m_topLevel ||
        XEvent_type(event) != XEVENT_TYPE_WINDOW_STATE_CHANGE)
        return false;
    /* Qt 前置条件 `d->isExplicitlyHidden()`：**用户显式**隐藏过的手柄不参与
     * 自动显隐（否则窗口状态来回切会把用户故意隐藏的手柄又弹出来）。
     * 判据 = 显式隐藏位 ∧ ExplicitShowHide 登记位：框架在下方
     * setVisible 之后清 ExplicitShowHide（Qt 同款），故「因最大化而隐藏」
     * 不算显式隐藏，还原时还能自动回来。Qt 6.8 + offscreen 实测
     * （probe：showMaximized→grip hidden=1；showNormal→hidden=0；
     * 再 max/normal 一轮同样往返）。 */
    if (XWidget_isHidden((XWidget*)grip) &&
        XWidget_testAttribute((XWidget*)grip,
                              XWidgetAttribute_ExplicitShowHide))
        return false;
    state = XWidget_windowState((XWidget*)watched);
    wantHide = (state & (XWindowStates)(XWindowState_Maximized |
                                        XWindowState_FullScreen)) != 0;
    XWidget_setVisible((XWidget*)grip, !wantHide);
    XWidget_setAttribute((XWidget*)grip, XWidgetAttribute_ExplicitShowHide,
                         false);
    return false;
}

/** @brief 显隐扩展槽（对标 QSizeGrip::setVisible 覆写，qsizegrip.h:23）：
 *         先走基类显隐流程，再重解析顶层控件（手柄被重挂父级后观察对象
 *         要跟着换，否则新顶层窗口的最大化不再驱动它隐藏）。
 * @note       必须经 `XClass_Parent` **直呼** XWidget 的默认槽
 *             （xwidget_setVisible_default）：`XWidget_setVisible_base` 是
 *             虚表派发入口，在槽内再调它会重入本槽无限递归（同文件
 *             mousePress 的 XClass_Parent 口径）。 */
static void VX_sizeGrip_setVisible(XWidget* self, bool visible)
{
    XClass_Parent(XWidget, EXWidget_SetVisible,
                  void (*)(XWidget*, bool))((XWidget*)self, visible);
    xsg_updateTopLevel((XSizeGrip*)self);
}

/** @brief 移动事件（对标 QSizeGrip::moveEvent，qsizegrip.cpp:413-424）：
 *         手柄换角后刷新斜向调整光标；拖动中（d->p 非空）不刷新——
 *         Qt 原文「We're inside a resize operation; no update necessary.」。 */
static void VX_sizeGrip_moveEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    bool atBottom;
    bool atLeft;
    if (!grip || !event) return;
    if (g_sizeGripDragActive) return;
    xsg_cornerOf(grip, &atBottom, &atLeft);
    xsg_applyCornerCursor(grip, atBottom, atLeft);
    XClass_Parent(XWidget, EXWidget_MoveEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
}

XVtable* XSizeGrip_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSizeGrip)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_sizeGrip_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_sizeGrip_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_sizeGrip_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_sizeGrip_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MoveEvent, VX_sizeGrip_moveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SetVisible, VX_sizeGrip_setVisible);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VX_sizeGrip_eventFilter);
    return XVTABLE_DEFAULT;
}

/** @brief 按 Qt 6.8 `QSizeGrip::sizeHint` 计算首选尺寸。
 * @details 原文（qsizegrip.cpp:199-204）：
 *          `style()->sizeFromContents(CT_SizeGrip, &opt, QSize(13,13), this)`。
 *          本机 Qt 6.8.0 + offscreen 实测：QCommonStyle=13x13、
 *          QFusionStyle=17x17（QFusionStyle::sizeFromContents 对
 *          CT_SizeGrip 加 QSize(4,4)，qfusionstyle.cpp:3102-3104）、
 *          QWindowsStyle=13x13。
 *          本函数按同一公式向当前样式查询；XCommonStyle 无 CT_SizeGrip
 *          分支时返回入参 13x13——与 Qt QCommonStyle 的 13x13 逐值一致。
 *          （XFusionStyle 的 CT_SizeGrip 分支尚缺，属 XStyle 侧待补；
 *          补上后本控件自动得到 17x17，无需再改这里。） */
static XSize xsg_sizeHintOf(XSizeGrip* self)
{
    XSize hint;
    XSize_init(&hint, 13, 13);
#if XSTYLE_ON
    if (self && XWidget_style((const XWidget*)self) != NULL) {
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCT_SizeGrip);
        opt.m_rect = XWidget_rect((const XWidget*)self);
        opt.m_state = XWidget_isEnabled((const XWidget*)self)
                          ? XStyleState_Enabled
                          : 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((const XWidget*)self);
#endif
        hint = XStyle_sizeFromContents(XWidget_style((const XWidget*)self),
                                       XStyleCT_SizeGrip, &opt, hint);
    }
#else
    (void)self;
#endif /* XSTYLE_ON */
    return hint;
}

void XSizeGrip_init(XSizeGrip* self, XWidget* parent)
{
    XSize hint;
    bool atBottom = true;
    bool atLeft = false;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XSizeGrip);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt 6.8 QSizeGrip：首选尺寸由样式 CT_SizeGrip 给出（13x13 起算），
     * 不再固定 16x16（见 xsg_sizeHintOf）。构造期初始几何也取该值，
     * 使未进布局时手柄的可视/命中区域与 sizeHint 一致。 */
    hint = xsg_sizeHintOf(self);
    XWidget_resize((XWidget*)self, hint.width, hint.height);
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* [minimumSizeHint 补齐 2026-10-27 框架对齐] Qt 同类控件的
     * minimumSizeHint 不低于其 sizeHint（对标 Qt 6.8.3 各控件
     * minimumSizeHint 实现）：补齐后布局竖排收缩有真实下界，
     * 此前 min=0 控件可被压塌至不可见。 */
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
    /* 构造期对齐 Qt 6.8 QSizeGripPrivate::init（qsizegrip.cpp:154-166）：
     * ① 尺寸策略 (Minimum, Fixed)——水平可涨到 sizeHint、竖直钉死手柄高；
     * ② 按角落设斜向调整光标（TL/BR→SizeFDiag，TR/BL→SizeBDiag）；
     * ③ 解析顶层并在其上装事件过滤器（最大化/全屏自动隐藏）。 */
    XWidget_setSizePolicy((XWidget*)self, XWidgetSizePolicy_Minimum,
                          XWidgetSizePolicy_Fixed);
    xsg_cornerOf(self, &atBottom, &atLeft);
    xsg_applyCornerCursor(self, atBottom, atLeft);
    xsg_updateTopLevel(self);
}

XSizeGrip* XSizeGrip_create_ex(XMemoryType memory, XWidget* parent)
{
    XSizeGrip* self = (XSizeGrip*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSizeGrip_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XSIZEGRIP_ON */

#endif /* XGUI_ON */
