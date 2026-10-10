/* xgui_demo_splitter.c —— XGuiDemo 通用分割条实现（契约见头文件）。
 *
 * 形态：5px 条带 + 中央三圆点 grip（悬停/拖拽中变主题强调蓝）。
 * 行为：
 *  - 收起态：单击即 toggleCollapse（细条即展开把手）；
 *  - 展开态：按下记基准尺寸并 grabMouse，move 按 edge 符号折算增量
 *    连续回调 applySize（使用方钳位并重排），release 释放；
 *  - 双击：toggleCollapse。
 * 事件全部 accept：条带不把点击漏给底下的页面控件。
 */
#include "xgui_demo_splitter.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* paintImage/paintOffset（离屏缓冲绘制入口）。 */
#include "XEvent.h"
#include "XPainter.h"
#include "XClass.h"
#include "XMemory.h"
#if XCURSOR_ON
#include "XCursor.h"
#endif

/** @brief 分割条内部结构（m_base 必须为首成员）。 */
typedef struct DemoSplitter
{
    XWidget m_base;                 /**< 基类；必须是第一个成员。 */
    void* m_owner;                  /**< 回调上下文（借用）。 */
    DemoSplitterCallbacks m_cbs;    /**< 回调表（借用）。 */
    int m_edge;                     /**< 停靠边 0左 1右 2上 3下。 */
    bool m_collapsed;               /**< 所属侧收起态（细条=展开把手）。 */
    bool m_dragging;                /**< 拖拽进行中。 */
    int m_pressPos;                 /**< 按下点（本地 x 或 y）。 */
    int m_baseSize;                 /**< 按下时受控尺寸快照。 */
    bool m_active;                  /**< 悬停或拖拽中（grip 着色）。 */
} DemoSplitter;

XCLASS_DEFINE_BEGING(DemoSplitter)
XCLASS_DEFINE_EXTEND_END(DemoSplitter, XWidget)

static bool demoSplitter_verticalBar(int edge)
{
    return edge <= 1; /* 左/右停靠=竖条（拖宽）。 */
}

/** @brief 拖拽增量符号折算：左/上拖出为正。 */
static int demoSplitter_deltaOf(const DemoSplitter* self, int x, int y)
{
    if (self->m_edge == 0) return x - self->m_pressPos;
    if (self->m_edge == 1) return self->m_pressPos - x;
    if (self->m_edge == 2) return y - self->m_pressPos;
    return self->m_pressPos - y;
}

/** @brief 绘制：条带底 + 三圆点 grip（竖条横排点、横条纵排点）。 */
static void VDemoSplitter_paintEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    XImage* image;
    XPainter painter;
    XRect rect;
    XPoint offset;
    int w;
    int h;
    uint32_t bar = sp->m_active ? 0xffdbe7fdu : 0xffe9edf2u;
    uint32_t dot = sp->m_active ? 0xff1a66d0u : 0xff9aa6b2u;
    int cx;
    int cy;
    int i;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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
    w = XWidget_width(self);
    h = XWidget_height(self);
    XRect_init(&rect, 0, 0, w, h);
    XPainter_fillRect(&painter, &rect, bar);
    cx = w / 2 - 1;
    cy = h / 2 - 1;
    if (demoSplitter_verticalBar(sp->m_edge)) {
        for (i = -1; i <= 1; ++i) {
            XRect_init(&rect, cx, cy + i * 6, 3, 3);
            XPainter_fillRect(&painter, &rect, dot);
        }
    } else {
        for (i = -1; i <= 1; ++i) {
            XRect_init(&rect, cx + i * 6, cy, 3, 3);
            XPainter_fillRect(&painter, &rect, dot);
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

static void VDemoSplitter_mousePressEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    XPoint gpos;
    if (!mouse) return;
    gpos = XMouseEvent_globalPosition(mouse);
    if (sp->m_collapsed) {
        /* 收起态：细条即展开把手。 */
        if (sp->m_cbs.toggleCollapse)
            sp->m_cbs.toggleCollapse(sp->m_owner);
        XEvent_accept(event);
        return;
    }
    sp->m_dragging = true;
    /* 增量以屏幕全局坐标计：拖拽中分割条自身几何随尺寸变化，本地
     * 坐标系会跟着平移，本地差值不可用。 */
    sp->m_pressPos = demoSplitter_verticalBar(sp->m_edge) ? gpos.x : gpos.y;
    sp->m_baseSize = sp->m_cbs.sizeFor ? sp->m_cbs.sizeFor(sp->m_owner) : 0;
    sp->m_active = true;
    XWidget_grabMouse(self);
    XWidget_update(self);
    XEvent_accept(event);
}

static void VDemoSplitter_mouseMoveEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    XMouseEvent* mouse = (XMouseEvent*)event;
    XPoint gpos;
    if (!mouse) return;
    if (!sp->m_dragging) {
        XEvent_accept(event);
        return;
    }
    gpos = XMouseEvent_globalPosition(mouse);
    if (sp->m_cbs.applySize)
        sp->m_cbs.applySize(sp->m_owner,
                            sp->m_baseSize +
                                demoSplitter_deltaOf(sp, gpos.x, gpos.y));
    XEvent_accept(event);
}

static void VDemoSplitter_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    if (!sp) return;
    if (sp->m_dragging) {
        sp->m_dragging = false;
        XWidget_releaseMouse(self);
        XWidget_update(self);
    }
    if (event) XEvent_accept(event);
}

static void VDemoSplitter_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    if (!sp) return;
    if (!sp->m_collapsed && sp->m_cbs.toggleCollapse)
        sp->m_cbs.toggleCollapse(sp->m_owner);
    if (event) XEvent_accept(event);
}

static void VDemoSplitter_enterEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    (void)event;
    if (!sp) return;
    sp->m_active = true;
    XWidget_update(self);
}

static void VDemoSplitter_leaveEvent(XWidget* self, XEvent* event)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    (void)event;
    if (!sp) return;
    if (!sp->m_dragging) sp->m_active = false;
    XWidget_update(self);
}

/** @brief 初始化类虚函数表。 */
XVtable* DemoSplitter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoSplitter)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VDemoSplitter_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VDemoSplitter_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VDemoSplitter_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VDemoSplitter_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VDemoSplitter_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, VDemoSplitter_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VDemoSplitter_leaveEvent);
    return XVTABLE_DEFAULT;
}

void DemoSplitter_setState(XWidget* self, int edge, bool collapsed)
{
    DemoSplitter* sp = (DemoSplitter*)self;
    if (!sp) return;
    sp->m_edge = edge;
    sp->m_collapsed = collapsed;
    XWidget_update(self);
}

XWidget* DemoSplitter_create_ex(int memoryType, XWidget* parent, int edge,
                                const DemoSplitterCallbacks* cbs, void* owner)
{
    DemoSplitter* sp = (DemoSplitter*)XMemory_malloc(sizeof(DemoSplitter),
                                                     memoryType);
    if (!sp) return NULL;
    XMemset(sp, 0, sizeof(*sp));
    XWidget_init(&sp->m_base, parent, 0);
    XClassSetVtable(sp, DemoSplitter);
    Set_Class_Memory(sp, memoryType);
    Set_Class_IsHeap(sp, true);
    sp->m_owner = owner;
    if (cbs) sp->m_cbs = *cbs;
    sp->m_edge = edge;
#if XCURSOR_ON
    {
        /* 竖条=左右调宽（SizeHor），横条=上下调高（SizeVer）。 */
        XCursor* cursor = XCursor_create_shape(
            demoSplitter_verticalBar(edge) ? XCursor_SizeHor
                                           : XCursor_SizeVer);
        if (cursor) {
            XWidget_setCursor(&sp->m_base, cursor);
            XClassDelete(cursor);
        }
    }
#endif
    XWidget_setMouseTracking(&sp->m_base, true);
    /* 收起后细条贴窗口缘（落在 CSD 改尺寸带内）：声明 HitEdgeBand 让
       装饰模块边缘带让位，否则细条单击在桌面 CSD 下永不可达。 */
    XWidget_setAttribute(&sp->m_base, XWidgetAttribute_HitEdgeBand, true);
    XWidget_show(&sp->m_base);
    return &sp->m_base;
}
