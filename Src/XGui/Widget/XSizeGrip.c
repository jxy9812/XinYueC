/**
 * @file       XSizeGrip.c
 * @brief      窗口尺寸拖拽把手控件实现（对标 Qt 6.8 QSizeGrip 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XSizeGrip.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XSIZEGRIP_ON

/* ==================== 拖拽会话状态 ==================== */
/* 对标 Qt qsizegrip.cpp 的 QSizeGripPrivate（d->p / d->r / d->gotMousePress
 * / m_corner）。XSizeGrip.h 为公共契约头（结构体仅 m_base），拖拽会话
 * 状态按模块级单例收拢在实现文件内——鼠标单点语义下同一时刻至多一路
 * 拖拽，与 g_mouseGrabWidget 的模块级登记口径一致。 */
static bool g_sizeGripDragActive = false;      /**< 按下后进入拖拽会话。 */
static XPoint g_sizeGripPressGlobal;           /**< 按下时屏幕全局坐标。 */
static XRect g_sizeGripStartGeometry;          /**< 按下时顶层窗口几何。 */
static bool g_sizeGripAtBottom = true;         /**< 手柄位于顶层下半（Qt d->atBottom）。 */
static bool g_sizeGripAtLeft = false;          /**< 手柄位于顶层左半（Qt d->atLeft）。 */

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
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：右下角斜点阵走 CE_SizeGrip。 */
        XStyle* style = XStyle_defaultStyle();
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
    XPoint zero;
    XPoint gripPos;
    if (!grip || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 对标 Qt qsizegrip.cpp:257 mousePressEvent：非左键交父类默认
         * 处理（默认忽略→沿父链传播）。 */
        XWidget_mousePressEvent_base(self, event);
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
    zero.x = 0;
    zero.y = 0;
    gripPos = XWidget_mapTo(self, top, &zero);
    g_sizeGripAtBottom = gripPos.y >= XWidget_height(top) / 2;
    g_sizeGripAtLeft = gripPos.x <= XWidget_width(top) / 2;
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
    /* 最小尺寸钳位（对标 qsizegrip.cpp:401 closestAcceptableSize 的
     * 最小可接受尺寸下限；无显式下限时至少保留 1x1）。 */
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
        /* 对标 Qt qsizegrip.cpp:413 mouseReleaseEvent：非左键交父类。 */
        XWidget_mouseReleaseEvent_base(self, event);
        return;
    }
    /* 结束拖拽会话并解除抓取（对标 d->gotMousePress = false;
     * d->p = QPoint()；releaseMouse 仅当 self 为当前抓取者时生效）。 */
    g_sizeGripDragActive = false;
    XWidget_releaseMouse(self);
    XEvent_accept(event);
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
    return XVTABLE_DEFAULT;
}

void XSizeGrip_init(XSizeGrip* self, XWidget* parent)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XSizeGrip);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize(self, 16, 16);
    hint.width = 16;
    hint.height = 16;
    XWidget_setSizeHint((XWidget*)self, &hint);
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
