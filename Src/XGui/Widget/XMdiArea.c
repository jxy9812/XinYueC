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
#include "XStringUtils.h"
#include "XWidget_Protected.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/* ==================== XMdiSubWindow ==================== */

/** @brief 子窗 chrome 绘制（对标 QMdiSubWindow::paintEvent，
 *         qmdisubwindow.cpp:3086-3138 的标准条构造）。
 * @details 第八轮总验收 FAIL② 根修：此前仅涂「裸蓝标题条 + 白体」，
 *          无窗框无图标无按钮位（r3e_mdi_zoom.png）。补齐：
 *          - 窗框 1px（Mid；对标 PE_FrameWindow，宽度按本项目设计
 *            语言取 1px 而非 Fusion 的 PM_MdiSubWindowFrameWidth=4）；
 *          - 标题带：激活=Highlight 底（qmdisubwindow.cpp:1637-1644
 *            State_Active 分支；激活判定=mdiArea()->activeSubWindow
 *            ==self），非激活=Window 底；
 *          - 带左系统菜单图标位（迷你窗占位图形，对标 CC_TitleBar 的
 *            SC_TitleBarSysMenu）、右最小化/关闭按钮位（SC_TitleBar
 *            MinButton/CloseButton；按任务裁定为绘制占位，不接交互，
 *            命中域仍归头带拖拽）；
 *          - 标题文本自 m_title；空则回落内容件 windowTitle（Qt
 *            addSubWindow/setWidget 标题随内容件，qmdisubwindow.cpp:
 *            994 标题同步链的静态版）。
 *          脏区口径不变：只绘事件脏区∩各铬件（night #50 修复保持）。
 *          内容件挂载已内收 1px（XMdiSubWindow_setWidget/resizeEvent），
 *          侧框线不再被子控件覆盖。 */
static void VX_mdiSubWindow_paintEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect body;
    XRect exposed;
    XRect piece;
    XRect clipHead;
    const char* titleText;
    uint32_t bandColor;
    uint32_t textColor;
    uint32_t frameColor;
    uint32_t base;
    bool active;
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
                                  XPaletteColorRole_Base);
        base = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        frameColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Window);
        /* 非激活标题带底色默认（对标 titleBarOptions 的 Inactive 色组，
         * qmdisubwindow.cpp:1640-1643）。 */
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        textColor = XColor_rgba(&c);
    }
#else
    bandColor = 0xFFD8D8D8u;
    textColor = 0xFF000000u;
    base = 0xFFFFFFFFu;
    frameColor = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 激活判定：所属 MDI 区当前活动子窗（脱离区域的独立子窗按非激
       活呈现，同 Qt 无 parent 时无活动态）；激活带=Highlight 底 +
       HighlightedText 前景（qmdisubwindow.cpp:1637-1639）。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        active = area && XMdiArea_activeSubWindow(area) ==
                            (XMdiSubWindow*)sw;
    }
    if (active) {
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        bandColor = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_HighlightedText);
        textColor = XColor_rgba(&c);
#else
        bandColor = 0xFF3080C0u;
        textColor = 0xFFFFFFFFu;
#endif /* XPALETTE_ON */
    }
    /* 只绘事件脏区（本地坐标，收进控件矩形）：此前恒整幅涂写标题条
     * +白底，多矩形脏区逐矩形派发时本窗的整幅白底会抹掉先派矩形刚
     * 恢复的内容标签像素，且抹写不受 present 区域保护（night #50
     * 交互擦除链的子窗一级；对标 Qt paintEvent 经裁剪只绘暴露区）。 */
    exposed = ((const XPaintEvent*)event)->m_rect;
    if (exposed.x < 0) { exposed.width += exposed.x; exposed.x = 0; }
    if (exposed.y < 0) { exposed.height += exposed.y; exposed.y = 0; }
    if (exposed.x + exposed.width > w) exposed.width = w - exposed.x;
    if (exposed.y + exposed.height > h) exposed.height = h - exposed.y;
    /* 体不透明填充（行 20 起、内收侧/底框线）：此前仅涂标题条，标题条
     * 以下透明透出视口背景。 */
    if (h > 21 && w > 2) {
        XRect_init(&body, 1, 20, w - 2, h - 21);
        body = XRect_intersected(&body, &exposed);
        if (body.width > 0 && body.height > 0)
            XPainter_fillRect(&painter, &body, base);
    }
    /* 标题带（框线内 1..19 行；第 19 行兼作带下分隔）。 */
    XRect_init(&head, 1, 1, w > 2 ? w - 2 : 0, 19);
    clipHead = head;
    head = XRect_intersected(&head, &exposed);
    if (head.width > 0 && head.height > 0)
        XPainter_fillRect(&painter, &head, bandColor);
    /* 窗框 1px：顶/左/右/底（最后绘制，保证框线压住体填充）。 */
    if (w > 0 && h > 0) {
        if (0 >= exposed.y && 0 < exposed.y + exposed.height) {
            XRect_init(&piece, exposed.x, 0, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (h - 1 >= exposed.y && h - 1 < exposed.y + exposed.height &&
            exposed.width > 0) {
            XRect_init(&piece, exposed.x, h - 1, exposed.width, 1);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (0 >= exposed.x && 0 < exposed.x + exposed.width) {
            XRect_init(&piece, 0, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
        if (w - 1 >= exposed.x && w - 1 < exposed.x + exposed.width &&
            exposed.height > 0) {
            XRect_init(&piece, w - 1, exposed.y, 1, exposed.height);
            XPainter_fillRect(&painter, &piece, frameColor);
        }
    }
    if (head.width > 0 && head.height > 0) {
        XPainter_setClipRect(&painter, &clipHead,
                             XPainterClipOperation_ReplaceClip);
        /* 左：系统菜单图标位（迷你窗占位：1px 外框 + 顶部标题条填充，
         * 对标 CC_TitleBar 的 SC_TitleBarSysMenu 图标位）。 */
        XPainter_setPen(&painter, textColor);
        XPainter_drawRect_2(&painter, 4.0f, 4.0f, 10.0f, 10.0f);
        XRect_init(&piece, 5, 5, 8, 3);
        XPainter_fillRect(&painter, &piece, textColor);
        /* 标题文本：x=18 让出图标位；基线 14 同 XDockWidget 标题条口径。 */
        titleText = "";
        if (sw->m_title &&
            XString_size((const XContainer*)sw->m_title) > 0)
            titleText = XString_toUtf8(sw->m_title);
        if (titleText && *titleText)
            XPainter_drawText(&painter, 18, 14, titleText, textColor);
        /* 右：最小化（8x1 横线）+ 关闭（"×" 字形）按钮位（绘制占位，
         * 不接交互；对标 SC_TitleBarMinButton/SC_TitleBarCloseButton）。 */
        if (w > 40) {
            XRect_init(&piece, w - 34, 10, 8, 1);
            XPainter_fillRect(&painter, &piece, textColor);
            XPainter_drawText(&painter, w - 18, 14, "\xc3\x97", textColor);
        }
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

/** @brief 内容件在子窗内的挂载矩形（标题带下方、框线内收 1px）。
 * @details chrome 窗框（paintEvent）绘制占用的 1px 边缘不归内容件：
 *          x=1 起、宽 w-2（避侧框线），y=20（标题带 0..19 行）起、
 *          高 h-21（底框线 1 行）；尺寸不足时钳 0（折叠态 h=20 时
 *          内容件 0 高不绘，同 Qt 折叠仅标题条）。setWidget 挂载与
 *          resizeEvent 随框布局共用同一口径。 */
static void xmdi_subWindowContentRect(const XMdiSubWindow* self, XRect* out)
{
    int w;
    int h;
    if (!out) return;
    w = self ? XWidget_width((const XWidget*)self) : 0;
    h = self ? XWidget_height((const XWidget*)self) : 0;
    XRect_init(out, 1, XMDI_SUBWINDOW_TITLEBAR_HEIGHT,
               w > 2 ? w - 2 : 0, h > 21 ? h - 21 : 0);
}

static int xmdi_dragClamp(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

/** @brief 拖动一步的脏区全覆盖（对标 QMdiAreaPrivate 移动 update 系统）。
 * @details 第八轮总验收 FAIL① 根修：拖后标题栏左端「台阶状缺角」
 *          （r3e_mdi_drag2_zoom.png）= 旧位部分行列从未进入任何已派发
 *          脏区快照。XWidget_setGeometry 内联的 old∪new 包围盒失效
 *          （XWidget.c recomputeGeometry）与显式 update 共用同一异步
 *          管线（顶层脏区 → PAINT 快照 → 求差派发），单次快照竞态下
 *          可能漏行。本助手在每步拖动后追加一次显式 update：
 *          旧位 ∪ 新位 ∪ 平移扫掠 delta 带（横/纵四条带）逐矩形入队，
 *          同区域多次入队幂等，任一次快照漏区都被其余入队补齐——
 *          脏区覆盖从「单来源单快照」变为「双来源多快照」全闭合。
 * @param      parent 子窗父容器（视口；矩形同子窗局部坐标系）。
 * @param      oldRect 本步拖动前子窗矩形。
 * @param      newRect 本步拖动后子窗矩形。
 * @return     无返回值。
 */
static void xmdi_dragUpdateCoverage(XWidget* parent,
                                    const XRect* oldRect,
                                    const XRect* newRect)
{
    XRegion region;
    XRect band;
    int dx = newRect->x - oldRect->x;
    int dy = newRect->y - oldRect->y;
    int minY = oldRect->y < newRect->y ? oldRect->y : newRect->y;
    int minX = oldRect->x < newRect->x ? oldRect->x : newRect->x;
    int spanH = oldRect->height > newRect->height
                    ? oldRect->height : newRect->height;
    int spanW = oldRect->width > newRect->width
                    ? oldRect->width : newRect->width;
    if (!parent) return;
    XRegion_init(&region);
    XRegion_addRect(&region, oldRect);
    XRegion_addRect(&region, newRect);
    /* 横向扫掠带：水平位移在新旧位之间扫过的竖条（含对角移动的
       斜向耦合段，覆盖域=平移 Minkowski 和，宽 |dx| 高 h+|dy|）。 */
    if (dx > 0) {
        XRect_init(&band, oldRect->x + oldRect->width, minY, dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    } else if (dx < 0) {
        XRect_init(&band, newRect->x + newRect->width, minY, -dx,
                   spanH + (dy < 0 ? -dy : dy));
        XRegion_addRect(&region, &band);
    }
    /* 纵向扫掠带：垂直位移扫过的横条（宽 w+|dx| 高 |dy|）。 */
    if (dy > 0) {
        XRect_init(&band, minX, oldRect->y + oldRect->height,
                   spanW + (dx < 0 ? -dx : dx), dy);
        XRegion_addRect(&region, &band);
    } else if (dy < 0) {
        XRect_init(&band, minX, newRect->y + newRect->height,
                   spanW + (dx < 0 ? -dx : dx), -dy);
        XRegion_addRect(&region, &band);
    }
    XWidget_updateRegion(parent, &region);
    XRegion_deinit(&region);
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
    /* 头带按下即激活本子窗（对标 qmdisubwindow.cpp:958 按压激活链，
     * qmdiarea.cpp 视口事件过滤器的 setActiveSubWindow 同义）：激活态
     * 驱动标题带 Highlight/Window 配色（paintEvent），拖动非活动窗时
     * 配色即时翻转，不再出现「灰带被拖」。区域缺失（独立子窗）跳过。 */
    {
        XMdiArea* area = XMdiSubWindow_mdiArea(sw);
        if (area) XMdiArea_setActiveSubWindow(area, sw);
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
     * Move 操作改 top-left）。步内旧位→新位+扫掠 delta 显式全覆盖
     * 失效（xmdi_dragUpdateCoverage；setGeometry 内联的包围盒失效
     * 之外的双保险，第八轮 FAIL① 台阶缺角根修）。 */
    {
        XRect oldRect = XWidget_geometry(self);
        XRect newRect;
        XWidget_setGeometry(self, newX, newY,
                            g_mdiDragStartGeometry.width,
                            g_mdiDragStartGeometry.height);
        newRect = XWidget_geometry(self);
        if (parent && (newRect.x != oldRect.x || newRect.y != oldRect.y))
            xmdi_dragUpdateCoverage(parent, &oldRect, &newRect);
    }
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

/** @brief 尺寸变化时内容件随框布局（对标 QMdiSubWindow::resizeEvent，
 *         qmdisubwindow.cpp:3026：缩放后内容几何与框保持一致）。
 * @details 此前子窗 resize 后内容件保持 creation 尺寸——chrome 窗框
 *          绘制后表现为框大内容小、框线内残白。拖动只改位置不发
 *          RESIZE（recomputeGeometry 仅 sizeChanged 派发），跟手链
 *          不受本槽影响。链式口径同 XSplitter resizeEvent（不链基
 *          类，XWidget 默认 Resize 槽为空实现）。 */
static void VX_mdiSubWindow_resizeEvent(XWidget* self, XEvent* event)
{
    XMdiSubWindow* sw = (XMdiSubWindow*)self;
    XRect r;
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (sw->m_widget) {
        xmdi_subWindowContentRect(sw, &r);
        XWidget_setGeometryRect(sw->m_widget, &r);
    }
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_mdiSubWindow_resizeEvent);
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
    if (!self) return;
    if (self->m_widget) return;
    self->m_widget = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 内容件内收 1px 挂载（x=1 起，底留框线行）：chrome 窗框绘制后
     * 侧框线不再被内容件覆盖；标题带区（0..19 行）仍为拖拽命中域。 */
    xmdi_subWindowContentRect(self, &r);
    XWidget_setGeometryRect(widget, &r);
    /* 对标 Qt 标题随内容件（qmdisubwindow.cpp:994 窗口标题同步链的
     * 静态版）：子窗标题为空且内容件带 windowTitle 时采纳之，标题条
     * 不至于空白。 */
    if ((!self->m_title ||
         XString_size((const XContainer*)self->m_title) == 0) &&
        widget) {
        const XString* wt = XWidget_windowTitle(widget);
        if (wt && XString_size((const XContainer*)wt) > 0) {
            const char* utf8 = XString_toUtf8(wt);
            if (utf8 && *utf8) XMdiSubWindow_setWindowTitle_2(self, utf8);
        }
    }
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
        /* 活动态驱动标题带配色（paintEvent 激活分支），失活窗立即
           重绘，否则保留 Highlight 底的陈旧像素。 */
        XWidget_update((XWidget*)self->m_active);
    }
    xmdi_emitAboutToActivate(window);
    self->m_active = window;
    xmdi_emitStateChanged(window, 0, window->m_state);
    xmdi_emitActivated(self, window);
    XWidget_update((XWidget*)window);
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
    XWidget* node;
    if (!self) return NULL;
    parent = XWidget_parentWidget((XWidget*)self);
    /* 对标 QMdiSubWindow::mdiArea（qmdisubwindow.cpp:2527-2538）：
     * 沿父链上溯，命中 XMdiArea 祖先且「本子窗直父=该区视口」才认
     * 归属。此前直呼 parent 强转 XMdiArea*——addSubWindow 实际把子
     * 窗挂在视口（qmdiarea.cpp:791-797 appendChild 同构），反查恒返
     * 视口指针，回归断言 "[MDI-FAIL] ext: mdiArea 反查" 即此根因。
     * 类判定经虚表类名（qobject_cast 的本框架等价，同 XStyle.c:710
     * 口径），非本框架裸指针强转。 */
    for (node = parent; node; node = XWidget_parentWidget(node)) {
        XVtable* vt = XClassGetVtable((XClass*)node);
        if (vt && XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XMdiArea") == 0) {
            XWidget* viewport = XAbstractScrollArea_viewport(
                (XAbstractScrollArea*)node);
            if (viewport == parent) return (XMdiArea*)node;
        }
    }
    return NULL;
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