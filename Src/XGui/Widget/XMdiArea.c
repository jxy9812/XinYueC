/**
 * @file       XMdiArea.c
 * @brief      多文档接口区域及子窗口控件实现（对标 Qt 6.8 QMdiArea/QMdiSubWindow 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMdiArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/* ==================== XMdiSubWindow ==================== */

static void VX_mdiSubWindow_paintEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect body;
    XRect exposed;
    uint32_t highlight;
    uint32_t windowText;
    uint32_t base;
    int w;
    int h;
    if (!sw || !event) return;
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Base);
        base = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
    base = 0xFFFFFFFFu;
#endif /* XPALETTE_ON */
    /* 只绘事件脏区（本地坐标，收进控件矩形）：此前恒整幅涂写标题条
     * +白底，多矩形脏区逐矩形派发时本窗的整幅白底会抹掉先派矩形刚
     * 恢复的内容标签像素，且抹写不受 present 区域保护（night #50
     * 交互擦除链的子窗一级；对标 Qt paintEvent 经裁剪只绘暴露区）。 */
    exposed = ((const XPaintEvent*)event)->m_rect;
    if (exposed.x < 0) { exposed.width += exposed.x; exposed.x = 0; }
    if (exposed.y < 0) { exposed.height += exposed.y; exposed.y = 0; }
    if (exposed.x + exposed.width > w) exposed.width = w - exposed.x;
    if (exposed.y + exposed.height > h) exposed.height = h - exposed.y;
    XRect_init(&head, 0, 0, w, 20);
    head = XRect_intersected(&head, &exposed);
    /* 子窗体体不透明填充（对标 QMdiSubWindow::paintEvent 的整框不透明
     * 绘制）：此前仅涂标题条，标题条以下透明透出视口背景，视觉上只剩
     * 「一条蓝色细线」，子窗体存在感全无。 */
    XRect_init(&body, 0, 20, w, h > 20 ? h - 20 : 0);
    body = XRect_intersected(&body, &exposed);
    if (head.width > 0 && head.height > 0)
        XPainter_fillRect(&painter, &head, highlight);
    if (body.width > 0 && body.height > 0)
        XPainter_fillRect(&painter, &body, base);
    if (head.width > 0 && head.height > 0) {
        XPainter_setClipRect(&painter, &head,
                             XPainterClipOperation_ReplaceClip);
        XPainter_drawText(&painter, 6, 14,
                          sw->m_title ? XString_toUtf8(sw->m_title) : "",
                          windowText);
        XPainter_setClipping(&painter, false);
    }
    XPainter_deinit(&painter);
}

/* ==================== 子窗标题条拖拽会话 ==================== */
/* 对标 Qt 6.8.3 qmdisubwindow.cpp 的标题条拖动链：mousePressEvent:
 * 3160 记录 mousePressPosition/oldGeometry → mouseMoveEvent:3302 以
 * 全局坐标映射父区驱动 setNewGeometry → setNewGeometry:1163-1175 按
 * 父矩形钳位（BoundaryMargin=5，:169）→ mouseReleaseEvent:3234 收尾。
 * XMdiArea.h 为公共契约头（结构体不可扩字段），拖拽会话状态按模块级
 * 单例收拢在实现文件内——鼠标单点语义下同一时刻至多一路拖拽，口径同
 * XSizeGrip.c 的 g_sizeGripDragActive 模块级登记。头带高度 20 与
 * paintEvent 头带（本文件 :80 XRect_init(&head,0,0,w,20)）及内容件
 * y=20 挂载（XMdiSubWindow_setWidget）共用同一常量口径。 */
#define XMDI_SUBWINDOW_TITLEBAR_HEIGHT 20
static bool g_mdiDragActive = false;      /**< 按下头带后进入拖拽会话。 */
static XMdiSubWindow* g_mdiDragWindow = NULL; /**< 会话归属子窗（防串扰）。 */
static XPoint g_mdiDragPressGlobal;       /**< 按下时屏幕全局坐标。 */
static XRect g_mdiDragStartGeometry;      /**< 按下时子窗几何（尺寸锚定）。 */

static int xmdi_dragClamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void VX_mdiSubWindow_mousePressEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton ||
        XMouseEvent_position(me).y >= XMDI_SUBWINDOW_TITLEBAR_HEIGHT) {
        /* 非左键或非头带（头带以下为内容件命中域）：交基类默认链
         * （对标 qmdisubwindow.cpp:3153 非左键 ignore 沿父链传播）。
         * 注意须用 XClass_Parent 直呼父类实现——XWidget_mousePressEvent_base
         * 是 vtable 派发助手，会重入本重载造成无限递归（复扫-5 实证）。 */
        XClass_Parent(XWidget, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 记录拖拽基线（对标 qmdisubwindow.cpp:3160-3162：按下记全局
     * 基准与 oldGeometry；移动事件以全局坐标差值算增量——局部坐标
     * 随命中接收者改写不可靠，同 XSizeGrip 口径）。 */
    g_mdiDragActive = true;
    g_mdiDragWindow = sw;
    g_mdiDragPressGlobal = XMouseEvent_globalPosition(me);
    g_mdiDragStartGeometry = XWidget_geometry(self);
    /* 对标 Qt 按压建立的隐式抓取（qt_button_down 后续 move/release
     * 直达按下控件）：拖拽中光标必然越出头带，显式抓取保证后续
     * 移动/释放仍送达本子窗（同 XSizeGrip.c:133 / XSplitter.c:456）。 */
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

static void VX_mdiSubWindow_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* parent;
    XPoint np;
    int dx;
    int dy;
    int newX;
    int newY;
    int vw;
    int vh;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    if (!g_mdiDragActive || g_mdiDragWindow != sw ||
        XMouseEvent_buttons(me) != XMouseButton_LeftButton) {
        /* 无按压基线/左键已释放/会话不归本窗：不动作（防御会话状态
         * 残留时误改几何，口径同 XSizeGrip.c:152-156）。基类链同 Press：
         * XClass_Parent 直呼（_base 助手会重入本重载，复扫-5 悬停栈
         * 溢出实证）。 */
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    parent = XWidget_parentWidget(self);
    np = XMouseEvent_globalPosition(me);
    dx = np.x - g_mdiDragPressGlobal.x;
    dy = np.y - g_mdiDragPressGlobal.y;
    newX = g_mdiDragStartGeometry.x + dx;
    newY = g_mdiDragStartGeometry.y + dy;
    /* 视口钳位（对标 qmdisubwindow.cpp:1163-1175 setNewGeometry 按
     * parentWidget()->rect() 钳位、BoundaryMargin=5；适配为子窗左上
     * 角域：竖向头带全程可及 [0, vh-20]，横向至少留 5px 头带可见
     * [-(w-5), vw-5]，父区缺失时不钳位）。 */
    if (parent) {
        vw = XWidget_width(parent);
        vh = XWidget_height(parent);
        newX = xmdi_dragClamp(newX,
                              -(XWidget_width(self) - 5),
                              vw - 5);
        newY = xmdi_dragClamp(newY, 0,
                              vh - XMDI_SUBWINDOW_TITLEBAR_HEIGHT);
    }
    /* 尺寸取按下时锚定值：拖动不缩放（对标 oldGeometry 锚定，仅
     * Move 操作改 top-left）。 */
    XWidget_setGeometry(self, newX, newY,
                        g_mdiDragStartGeometry.width,
                        g_mdiDragStartGeometry.height);
    XEvent_accept(event);
}

static void VX_mdiSubWindow_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sw || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 qmdisubwindow.cpp:3241 非左键交基类默认链（XClass_Parent
         * 直呼，同 Press/Move 防重入口径）。 */
        XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))((XWidget*)self, event);
        return;
    }
    /* 结束拖拽会话并解除抓取（对标 mouseReleaseEvent:3247 收尾 +
     * d->gotMousePress 复位；releaseMouse 仅当 self 为当前抓取者时
     * 生效，同 XSizeGrip.c:201-205）。 */
    if (g_mdiDragWindow == sw) g_mdiDragWindow = NULL;
    g_mdiDragActive = false;
    XWidget_releaseMouse(self);
    XEvent_accept(event);
}

static void VX_mdiSubWindow_deinit(XMdiSubWindow* self)
{
    if (!self) return;
    if (self->m_widget) {
        XWidget_delete_base(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_systemMenu) {
        XClass_delete_base((XClass*)self->m_systemMenu);
        self->m_systemMenu = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMdiSubWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiSubWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VX_mdiSubWindow_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_mdiSubWindow_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_mdiSubWindow_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_mdiSubWindow_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiSubWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiSubWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize(self, 200, 150);
    /* 默认值与 Qt QMdiSubWindowPrivate 一致。 */
    self->m_keyboardSingleStep = 5;
    self->m_keyboardPageStep = 20;
}

XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XMdiSubWindow* self =
        (XMdiSubWindow*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiSubWindow_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget)
{
    XRect r;
    int w;
    int h;
    if (!self) return;
    if (self->m_widget) return;
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self) > 20
        ? XWidget_height((XWidget*)self) - 20 : 0;
    XRect_init(&r, 0, 20, w, h);
    XWidget_setGeometryRect(widget, &r);
}

XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self)
{
    return self ? self->m_widget : NULL;
}

void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self)
{
    const char* text;
    if (!self || !self->m_title) return "";
    text = XString_toUtf8(self->m_title);
    return text ? text : "";
}

/* ==================== XMdiArea ==================== */

static void xmdi_emitActivated(XMdiArea* self, XMdiSubWindow* window)
{
    XVarList* args = XVarList_Create(XVar(XMdiSubWindow*, window));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XMdiArea_subWindowActivated_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xmdi_emitAboutToActivate(XMdiSubWindow* sw)
{
    if (!sw) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_aboutToActivate_signal,
            NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

static void xmdi_emitStateChanged(XMdiSubWindow* sw,
                                  int oldState, int newState)
{
    XVarList* args;
    if (!sw) return;
    args = XVarList_Create(XVar(int, oldState), XVar(int, newState));
    if (!args) return;
    if (((XObject*)sw)->m_signalSlot) {
        XObject_emitSignal((XObject*)sw,
            (size_t)XMdiSubWindow_windowStateChanged_signal, args,
            NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      派生判定子窗是否仍处 tileSubWindows 铺出的网格态
 *             （对标 Qt 6.8.3 QMdiAreaPrivate::isSubWindowsTiled）。
 * @details    根因（二次复扫 R-24）：resizeEvent 曾无条件 tileSubWindows，
 *             默认 SubWindowView 下用户摆位每次区域尺寸变化即被摧毁。
 *             Qt 6.8.3 的 resizeEvent 仅在 isSubWindowsTiled 为真时重铺
 *             （qmdiarea.cpp:2288），该标志由 tileSubWindows 置位、用户
 *             移动/缩放子窗清零。本实现 XMdiArea 结构体在头文件中
 *             （本修复不可改头文件）且子窗无 move/resize 事件挂点，无
 *             字段承载该标志，故按几何派生：平铺网格（cols=2）的特征
 *             是全部子窗等宽等高、第 i 窗位于基准窗 +(列*宽, 行*高)
 *             的网格点。区域 resize 不移动子窗、相对关系保持 → 判定为
 *             平铺并随新尺寸重铺；用户挪动任一子窗即破坏网格 → 不再
 *             重铺。单子窗（n==1）时"平铺铺满整域"与"用户随意摆位"
 *             在 resize 后几何上不可区分，保守判否不重铺，保证永不毁
 *             摆位（退化：单子窗少了"铺满新域"一步）。
 */
static bool xmdi_isTiled(const XMdiArea* self)
{
    int64_t i;
    int64_t n;
    XMdiSubWindow* base;
    int baseX;
    int baseY;
    int baseW;
    int baseH;
    if (!self || !self->m_subWindows) return false;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n < 2) return false;
    base = *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, 0);
    if (!base) return false;
    baseX = XWidget_x((XWidget*)base);
    baseY = XWidget_y((XWidget*)base);
    baseW = XWidget_width((XWidget*)base);
    baseH = XWidget_height((XWidget*)base);
    for (i = 1; i < n; ++i) {
        XMdiSubWindow* sw =
            *(XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        int col = (int)(i % 2); /* 与 XMdiArea_tileSubWindows 的列布局一致。 */
        int row = (int)(i / 2);
        if (!sw) return false;
        if (XWidget_x((XWidget*)sw) != baseX + col * baseW ||
            XWidget_y((XWidget*)sw) != baseY + row * baseH ||
            XWidget_width((XWidget*)sw) != baseW ||
            XWidget_height((XWidget*)sw) != baseH)
            return false;
    }
    return true;
}

static void VX_mdiArea_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    /* 链回基类视口/滚动条重排（框架口径同 XScrollArea 的
       XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent) 链；
       Qt 中该职责由 QAbstractScrollArea 私有排版机制承担，本框架
       落在 VX_asa_resizeEvent）。此前未链回：MDI 区被页签容器拉伸
       后视口停留 creation 尺寸 200x150——addSubWindow 的级联摆位
       按视口宽高钳位把子窗 2 钳回 (0,0)，与子窗 1 完全叠死（文档 1
       标签被盖、两条蓝标题条重合成一条），滚动条排版也永不发生
       （gdb 树遍历实证：viewport rect=(0,0 200x150) 而 ASA
       rect=(0,0 752x430)）。 */
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    /* 对标 Qt 6.8.3 QMdiArea::resizeEvent（qmdiarea.cpp:2273-2291）：
       仅子窗仍处平铺网格态时才随区域尺寸重铺；默认 SubWindowView 下
       用户摆位不再被无条件摧毁。 */
    if (xmdi_isTiled((const XMdiArea*)self))
        XMdiArea_tileSubWindows((XMdiArea*)self);
}

/** @brief 绘制：只填充 paint 事件携带的脏区（对标 QMdiArea::paintEvent，
 *         qmdiarea.cpp:2669-2674「for (const QRect &exposedRect :
 *         paintEvent->region()) fillRect(exposedRect, background)」）。
 * @details 根因（night #50 交互擦除）：基类 VX_asa_paintEvent 恒以
 *          (0,0,w,h) 整幅涂写——XWidget_paintTree 按单矩形逐个派发且
 *          各 widget 绘制不受该矩形硬裁剪（仅表面裁剪=整区域外接框），
 *          于是 ①脏区外子窗像素在部分重绘期间即被抹白（后备帧内）；
 *          ②多矩形区域逐矩形派发时，后派矩形的整幅涂抹抹掉先派矩形
 *          刚恢复的子窗内容，整区域一次性 present 后即白屏（点击/
 *          拖拽后子窗铬架+内容全空的实锚：gdb 探针 pe=(0,0 200x150)
 *          全量子窗重绘后紧随的另一矩形派发把标题条抹白）。改为只涂
 *          事件脏区（含底边框线也按脏区裁剪），每矩形派发只改动自身
 *          矩形，多矩形序列可交换且与 present 区域严格闭合。 */
static void VX_mdiArea_paintEvent(XWidget* self, XEvent* event)
{
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect rect;
    XRect line;
    uint32_t base;
    uint32_t mid;
    int w;
    int h;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    base = 0xFFFFFFFFu;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 脏区∩控件矩形：事件矩形可能越出控件边界，先收进自身范围
       （同 XWidget_paintEvent_default 的裁剪口径）。 */
    rect = ((const XPaintEvent*)event)->m_rect;
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > w) rect.width = w - rect.x;
    if (rect.y + rect.height > h) rect.height = h - rect.y;
    if (rect.width > 0 && rect.height > 0)
        XPainter_fillRect(&painter, &rect, base);
    /* 底边框线（同基类的 Mid 1px 线）：仅绘制落在脏区内的段落。 */
    if (h > 0 && (h - 1) >= rect.y &&
        (h - 1) < rect.y + rect.height && rect.width > 0) {
        XRect_init(&line, rect.x, h - 1, rect.width, 1);
        XPainter_fillRect(&painter, &line, mid);
    }
    XPainter_deinit(&painter);
}

static void VX_mdiArea_deinit(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClass_delete_base((XClass*)*sw);
        }
        XVector_delete_base(self->m_subWindows);
        self->m_subWindows = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XMdiArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMdiArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_mdiArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_mdiArea_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mdiArea_deinit);
    return XVTABLE_DEFAULT;
}

void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMdiArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_subWindows = XVector_Create(XMdiSubWindow*);
    self->m_active = NULL;
    self->m_viewMode = (int)XMdiAreaViewMode_SubWindowView;
    self->m_background = 0;
    self->m_tabPosition = 0;
    self->m_tabShape = 0;
    self->m_tabsMovable = false;
    self->m_tabsClosable = false;
    self->m_activationOrder = (int)XMdiAreaWindowOrder_CreationOrder;
    self->m_options = 0;
    self->m_documentMode = false;
}

XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XMdiArea* self = (XMdiArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMdiArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget)
{
    XMdiSubWindow* sw;
    XWidget* viewport;
    if (!self || !widget || !self->m_subWindows) return NULL;
    /* 子窗挂到视口（对标 QMdiAreaPrivate::appendChild，qmdiarea.cpp:
     * 791-797「child->setParent(viewport, ...)」）：挂到区域本体时子窗
     * 与视口为兄弟、被视口绘制次序压住且几何/裁剪口径错位——表现为
     * 子窗不可见仅剩残迹。 */
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)self);
    sw = XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                 viewport ? viewport : (XWidget*)self, 0);
    if (!sw) return NULL;
    XMdiSubWindow_setWidget(sw, widget);
    /* 级联摆放（对标 appendChild 的 place(placer, child)：新子窗不全
       等位叠死在前一窗上）；越界时回落视口原点。 */
    {
        int64_t index = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        int step = 24;
        int vw = viewport ? XWidget_width(viewport) : 0;
        int vh = viewport ? XWidget_height(viewport) : 0;
        int swx = (int)(index % 8) * step;
        int swy = (int)(index % 8) * step;
        int swW = XWidget_width((XWidget*)sw);
        int swH = XWidget_height((XWidget*)sw);
        if (vw > 0 && swx + swW > vw) swx = 0;
        if (vh > 0 && swy + swH > vh) swy = 0;
        XWidget_move((XWidget*)sw, swx, swy);
    }
    XVector_push_back_1_base(self->m_subWindows, &sw);
    XWidget_show((XWidget*)sw);
    XMdiArea_setActiveSubWindow(self, sw);
    return sw;
}

void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (sw && *sw && XMdiSubWindow_widget(*sw) == widget) {
            if (self->m_active == *sw) self->m_active = NULL;
            XClass_delete_base((XClass*)*sw);
            XVector_remove_base(self->m_subWindows, i, 1);
            return;
        }
    }
}

const XVector* XMdiArea_subWindowList(const XMdiArea* self)
{
    return self ? self->m_subWindows : NULL;
}

XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self)
{
    return self ? self->m_active : NULL;
}

void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window)
{
    if (!self || !window) return;
    if (self->m_active == window) return;
    if (self->m_active) {
        xmdi_emitStateChanged(self->m_active, self->m_active->m_state, 0);
        self->m_active->m_state = 0;
    }
    xmdi_emitAboutToActivate(window);
    self->m_active = window;
    xmdi_emitStateChanged(window, 0, window->m_state);
    xmdi_emitActivated(self, window);
}

void XMdiArea_closeAllSubWindows(XMdiArea* self)
{
    if (!self) return;
    if (self->m_subWindows) {
        int64_t i;
        int64_t n = XVector_size_base(
            (const XContainer*)self->m_subWindows);
        for (i = 0; i < n; ++i) {
            XMdiSubWindow** sw =
                (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
            if (sw && *sw)
                XClass_delete_base((XClass*)*sw);
        }
        XVector_clear_base(self->m_subWindows);
    }
    self->m_active = NULL;
}

void XMdiArea_cascadeSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int offset = 0;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        if (!sw || !*sw) continue;
        XRect_init(&r, offset, offset,
                   w > 60 ? w - 40 - offset : 200,
                   h > 60 ? h - 40 - offset : 150);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
        offset += 24;
    }
}

void XMdiArea_tileSubWindows(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    int count;
    int cols = 2;
    int rows;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    count = (int)n;
    if (count <= 0) return;
    rows = (count + cols - 1) / cols;
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** sw =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        XRect r;
        int col = (int)(i % cols);
        int row = (int)(i / cols);
        if (!sw || !*sw) continue;
        XRect_init(&r, col * w / cols, row * h / rows,
                   w / cols, h / rows);
        XWidget_setGeometryRect((XWidget*)*sw, &r);
    }
}

void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode)
{
    if (!self) return;
    self->m_viewMode = (int)mode;
}

XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self)
{
    return self ? (XMdiAreaViewMode)self->m_viewMode
                : XMdiAreaViewMode_SubWindowView;
}

int XMdiArea_subWindowCount(const XMdiArea* self)
{
    return (self && self->m_subWindows)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_subWindows)
               : 0;
}

/* ==================== 信号 ==================== */

void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window)
{
    (void)self; (void)window;
    return (void*)(size_t)XMdiArea_subWindowActivated_signal;
}

void* XMdiSubWindow_aboutToActivate_signal(XMdiSubWindow* self)
{
    (void)self;
    return (void*)(size_t)XMdiSubWindow_aboutToActivate_signal;
}
void* XMdiSubWindow_windowStateChanged_signal(XMdiSubWindow* self,
                                              int oldState, int newState)
{
    (void)self; (void)oldState; (void)newState;
    return (void*)(size_t)XMdiSubWindow_windowStateChanged_signal;
}

/* ==================== Task 2.7：MdiArea 补充 API ==================== */

XMdiSubWindow* XMdiArea_currentSubWindow(const XMdiArea* self)
{ return self ? self->m_active : NULL; }

void XMdiArea_setBackground(XMdiArea* self, uint32_t color)
{
    if (self) {
        self->m_background = color;
        XWidget_update((XWidget*)self);
    }
}
uint32_t XMdiArea_background(const XMdiArea* self)
{ return self ? self->m_background : 0; }

void XMdiArea_setTabPosition(XMdiArea* self, int position)
{ if (self) self->m_tabPosition = position; }
int XMdiArea_tabPosition(const XMdiArea* self)
{ return self ? self->m_tabPosition : 0; }

void XMdiArea_setTabsMovable(XMdiArea* self, bool movable)
{ if (self) self->m_tabsMovable = movable; }
bool XMdiArea_tabsMovable(const XMdiArea* self)
{ return self ? self->m_tabsMovable : false; }

void XMdiArea_setTabsClosable(XMdiArea* self, bool closable)
{ if (self) self->m_tabsClosable = closable; }
bool XMdiArea_tabsClosable(const XMdiArea* self)
{ return self ? self->m_tabsClosable : false; }

void XMdiArea_setActivationOrder(XMdiArea* self, int order)
{ if (self) self->m_activationOrder = order; }
int XMdiArea_activationOrder(const XMdiArea* self)
{ return self ? self->m_activationOrder : 0; }

/* ==================== Task 2.7：XMdiSubWindow 补充 API ==================== */

void XMdiSubWindow_setOption(XMdiSubWindow* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiSubWindow_testOption(const XMdiSubWindow* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step)
{ if (self && step > 0) self->m_keyboardSingleStep = step; }

int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardSingleStep : 0; }

void XMdiSubWindow_setKeyboardPageStep(XMdiSubWindow* self, int step)
{ if (self && step > 0) self->m_keyboardPageStep = step; }

int XMdiSubWindow_keyboardPageStep(const XMdiSubWindow* self)
{ return self ? self->m_keyboardPageStep : 0; }

bool XMdiSubWindow_isShaded(const XMdiSubWindow* self)
{ return self ? self->m_shaded : false; }

void XMdiSubWindow_showShaded(XMdiSubWindow* self)
{
    int oldState;
    if (!self) return;
    oldState = self->m_state;
    self->m_shaded = !self->m_shaded;
    if (self->m_shaded) {
        /* 折叠：仅保留标题条高度。 */
        self->m_state |= XMdiSubWindowState_Shaded;
        XWidget_resize((XWidget*)self, XWidget_width((XWidget*)self), 20);
    } else {
        self->m_state &= ~XMdiSubWindowState_Shaded;
        XWidget_resize((XWidget*)self, XWidget_width((XWidget*)self), 150);
    }
    XWidget_update((XWidget*)self);
    if (oldState != self->m_state)
        xmdi_emitStateChanged(self, oldState, self->m_state);
}

void XMdiSubWindow_showSystemMenu(XMdiSubWindow* self)
{
    XPoint pos;
    if (!self || !self->m_systemMenu) return;
    XPoint_init(&pos, 4, XWidget_height((XWidget*)self) > 20 ? 18 : 0);
    XMenu_popup(self->m_systemMenu, &pos);
}

void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu)
{
    if (!self) return;
    if (self->m_systemMenu == systemMenu) return;
    if (self->m_systemMenu)
        XClass_delete_base((XClass*)self->m_systemMenu);
    self->m_systemMenu = systemMenu;
}

XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self)
{ return self ? self->m_systemMenu : NULL; }

XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self)
{
    XWidget* parent;
    if (!self) return NULL;
    parent = XWidget_parentWidget((XWidget*)self);
    return (XMdiArea*)parent;
}

XSize XMdiSubWindow_sizeHint(const XMdiSubWindow* self)
{
    XSize size;
    int w = 200;
    int h = 150;
    if (!self) {
        XSize_init(&size, w, h);
        return size;
    }
    if (self->m_widget) {
        XSize ws = XWidget_sizeHint(self->m_widget);
        w = ws.width;
        h = ws.height;
    }
    if (w < 200) w = 200;
    if (h < 150) h = 150;
    XSize_init(&size, w, h + 20); /* 标题条 */
    return size;
}

XSize XMdiSubWindow_minimumSizeHint(const XMdiSubWindow* self)
{
    XSize size;
    (void)self;
    XSize_init(&size, 50, 20);
    return size;
}

XWidget* XMdiSubWindow_maximizedButtonsWidget(const XMdiSubWindow* self)
{ (void)self; return NULL; }

XWidget* XMdiSubWindow_maximizedSystemMenuIconWidget(
    const XMdiSubWindow* self)
{ (void)self; return NULL; }

/* ==================== Task 2.7：XMdiArea 补充 API ==================== */

void XMdiArea_setOption(XMdiArea* self, int option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= option;
    else self->m_options &= ~option;
}

bool XMdiArea_testOption(const XMdiArea* self, int option)
{
    return self ? (self->m_options & option) != 0 : false;
}

void XMdiArea_setDocumentMode(XMdiArea* self, bool enabled)
{
    if (self) {
        self->m_documentMode = enabled;
        XWidget_update((XWidget*)self);
    }
}

bool XMdiArea_documentMode(const XMdiArea* self)
{ return self ? self->m_documentMode : false; }

void XMdiArea_setTabShape(XMdiArea* self, int shape)
{ if (self) self->m_tabShape = shape; }

int XMdiArea_tabShape(const XMdiArea* self)
{ return self ? self->m_tabShape : 0; }

void XMdiArea_closeActiveSubWindow(XMdiArea* self)
{
    XMdiSubWindow* sw;
    int64_t i;
    int64_t n;
    if (!self) return;
    sw = self->m_active;
    if (!sw) return;
    self->m_active = NULL;
    if (!self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (slot && *slot == sw) {
            XClass_delete_base((XClass*)sw);
            XVector_remove_base(self->m_subWindows, i, 1);
            break;
        }
    }
    /* 对标 Qt：关闭活动窗口后激活剩余的第一个子窗口。 */
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n > 0) {
        XMdiSubWindow** first = (XMdiSubWindow**)XVector_at_base(
            self->m_subWindows, 0);
        if (first && *first)
            XMdiArea_setActiveSubWindow(self, *first);
    }
}

void XMdiArea_activateNextSubWindow(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n <= 0) return;
    if (!self->m_active) {
        XMdiSubWindow** first = (XMdiSubWindow**)XVector_at_base(
            self->m_subWindows, 0);
        if (first && *first)
            XMdiArea_setActiveSubWindow(self, *first);
        return;
    }
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (slot && *slot == self->m_active) {
            XMdiSubWindow** next = (XMdiSubWindow**)XVector_at_base(
                self->m_subWindows, (i + 1) % n);
            if (next && *next)
                XMdiArea_setActiveSubWindow(self, *next);
            return;
        }
    }
    /* 活动窗口不在列表：激活第一个。 */
    {
        XMdiSubWindow** first = (XMdiSubWindow**)XVector_at_base(
            self->m_subWindows, 0);
        if (first && *first)
            XMdiArea_setActiveSubWindow(self, *first);
    }
}

void XMdiArea_activatePreviousSubWindow(XMdiArea* self)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_subWindows) return;
    n = XVector_size_base((const XContainer*)self->m_subWindows);
    if (n <= 0) return;
    if (!self->m_active) {
        XMdiSubWindow** last = (XMdiSubWindow**)XVector_at_base(
            self->m_subWindows, n - 1);
        if (last && *last)
            XMdiArea_setActiveSubWindow(self, *last);
        return;
    }
    for (i = 0; i < n; ++i) {
        XMdiSubWindow** slot =
            (XMdiSubWindow**)XVector_at_base(self->m_subWindows, i);
        if (slot && *slot == self->m_active) {
            XMdiSubWindow** prev = (XMdiSubWindow**)XVector_at_base(
                self->m_subWindows, (i - 1 + n) % n);
            if (prev && *prev)
                XMdiArea_setActiveSubWindow(self, *prev);
            return;
        }
    }
    {
        XMdiSubWindow** last = (XMdiSubWindow**)XVector_at_base(
            self->m_subWindows, n - 1);
        if (last && *last)
            XMdiArea_setActiveSubWindow(self, *last);
    }
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */