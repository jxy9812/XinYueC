/**
 * @file       XSplitter.c
 * @brief      分割器控件实现（对标 Qt 6.8 QSplitter 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XSplitter.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XObject.h"
#include "XVariant.h"
#include "XString.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

static int xsp_childCount(const XSplitter* self)
{
    const XVector* children;
    if (!self) return 0;
    children = XObject_children((const XObject*)self);
    return children ? (int)XVector_size_base((const XContainer*)children) : 0;
}

static XWidget* xsp_childAt(const XSplitter* self, int index)
{
    const XVector* children;
    XObject** slot;
    if (!self || index < 0) return NULL;
    children = XObject_children((const XObject*)self);
    if (!children || index >= (int)XVector_size_base((const XContainer*)children))
        return NULL;
    slot = (XObject**)XVector_at_base(children, index);
    if (!slot || !*slot || !(*slot)->is_widget) return NULL;
    return (XWidget*)*slot;
}

/* xsp_childVisible 已移除：把手绘制隐藏口径统一为 isHidden（与
   xsp_contentLen/xsp_layout 同判据，见 xsp_hasLaterPage）。 */

/* ==================== 内部工具 ==================== */

#define XSPLITTER_MIN_SIZE 0

/** @brief 查询单页在拖动方向上的当前尺寸（水平取宽、垂直取高）。
 *  @note  定义于页面管理节，此处前置声明供 xsp_layout 使用。 */
static int xsp_pageSize(const XSplitter* self, int index);

static int xsp_horiz(const XSplitter* self)
{
    return self->m_orientation != 2; /* 非 Vertical 即水平 */
}

static int xsp_contentLen(const XSplitter* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int len = xsp_horiz(self) ? w : h;
    int hw = self->m_handleWidth;
    int count = xsp_childCount(self);
    int i;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        /* Qt 口径：QSplitterPrivate::layoutChildren 以 isHidden()（控件
           自身显式隐藏位）判页是否占分隔条位，不随父链 effective
           visible 波动——顶层分割器未 show 时子页同样扣把位（headless
           与已显示口径一致；xsp_layout 对全部子页分配几何，两处判据
           必须一致，否则把位泄漏使页宽偏大）。 */
        if (!child || XWidget_isHidden(child)) continue;
        len -= hw; /* 每页之后一个分隔条（最后一页之后的忽略误差）。 */
    }
    if (len < 0) len = 0;
    return len;
}

static void xsp_layout(XSplitter* self)
{
    int count = xsp_childCount(self);
    int i;
    int len;
    int used = 0;
    int perPage;
    int x = 0;
    int y = 0;
    int curTotal = 0;
    if (!self || count <= 0) return;
    len = xsp_contentLen(self);
    /* 比例保持（对标 Qt 拖动/setsizes 后容器缩放按既有尺寸比例重排，
       QSplitterPrivate::doResize）：存在非零现尺寸时按现尺寸比例分配
       len，拖动结果不因容器 resize 丢失；首次布局（现尺寸全零）均分。 */
    for (i = 0; i < count; ++i)
        curTotal += xsp_pageSize(self, i);
    perPage = len / (count > 0 ? count : 1);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        XRect r;
        int size = (curTotal > 0)
            ? (int)(((long long)len * xsp_pageSize(self, i)) / curTotal)
            : perPage;
        if (!child) continue;
        /* 几何分配不依赖当前可见性：子控件在隐藏时也需要正确尺寸，
         * 否则 show 后因 0x0 仍不可见（对标 QSplitterPrivate::layoutChildren）。 */
        if (i == count - 1) size = len - used;
        if (size < 0) size = 0;
        if (xsp_horiz(self))
            XRect_init(&r, x, 0, size, XWidget_height((XWidget*)self));
        else
            XRect_init(&r, 0, y, XWidget_width((XWidget*)self), size);
        XWidget_setGeometryRect(child, &r);
        used += size;
        if (xsp_horiz(self)) x += size + self->m_handleWidth;
        else y += size + self->m_handleWidth;
    }
}

static uint32_t xsp_color(const XSplitter* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void xsp_emitMoved(XSplitter* self, int pos, int index)
{
    XVarList* args = XVarList_Create(XVar(int, pos), XVar(int, index));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XSplitter_splitterMoved_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_splitter_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xsp_layout((XSplitter*)self);
}

static void VX_splitBar_paintEvent(XWidget* self, XEvent* event);

/** @brief index 之后是否还存在参与布局的页（对标 Qt 把手数量 =
 *  非隐藏页数-1：最后一页之后没有把手）。隐藏口径与 xsp_contentLen/
 *  xsp_layout 一致取 isHidden（控件自身显隐位）。 */
static bool xsp_hasLaterPage(const XSplitter* self, int index)
{
    int i;
    int count;
    if (!self) return false;
    count = xsp_childCount(self);
    for (i = index + 1; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (child && !XWidget_isHidden(child)) return true;
    }
    return false;
}

static void VX_splitter_paintEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    uint32_t mid;
    XRect line;
    int w;
    int h;
    int count;
    int i;
    int x;
    int y;
    if (!sp || !event) return;
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
    mid = xsp_color(sp, XPaletteColorRole_Mid);
    count = xsp_childCount(self);
    x = 0;
    y = 0;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        XStyle* style = NULL;
        /* 隐藏口径与 xsp_contentLen/xsp_layout 一致（isHidden）——此前
         * 绘制用 effective visible 而布局用 isHidden，两口径混用会使
         * 把手位置与页几何错位。 */
        if (!child || XWidget_isHidden(child)) continue;
#if XSTYLE_ON
        style = XStyle_defaultStyle();
#endif
        if (xsp_horiz(sp)) {
            x += XWidget_width(child);
            /* off-by-one 根修：最后一页之后不再绘制把手（对标 Qt 把手
             * 数=非隐藏页数-1，QSplitter 在最后一页右缘没有把手条带）；
             * 此前循环对每页都画，页右缘多出一条同款灰竖线+凹槽白点。 */
            if (!xsp_hasLaterPage(sp, i)) break;
            XRect_init(&line, x, 0, sp->m_handleWidth, h);
#if XSTYLE_ON
            if (style != NULL) {
                XStyleOption opt;
                XStyleOption_init(&opt, XStyleCE_Splitter);
                opt.m_rect = line;
                opt.m_state = XWidget_isEnabled(self)
                    ? XStyleState_Enabled : 0;
                opt.m_horizontal = false; /* 垂直分隔条。 */
#if XPALETTE_ON
                opt.m_palette = XWidget_palette(self);
#endif
                XStyle_drawControl(style, XStyleCE_Splitter, &opt,
                                   &painter, self);
            } else
#endif
            XPainter_fillRect(&painter, &line, mid);
            x += sp->m_handleWidth;
        } else {
            y += XWidget_height(child);
            if (!xsp_hasLaterPage(sp, i)) break;
            XRect_init(&line, 0, y, w, sp->m_handleWidth);
#if XSTYLE_ON
            if (style != NULL) {
                XStyleOption opt;
                XStyleOption_init(&opt, XStyleCE_Splitter);
                opt.m_rect = line;
                opt.m_state = XWidget_isEnabled(self)
                    ? XStyleState_Enabled : 0;
                opt.m_horizontal = true; /* 水平分隔条。 */
#if XPALETTE_ON
                opt.m_palette = XWidget_palette(self);
#endif
                XStyle_drawControl(style, XStyleCE_Splitter, &opt,
                                   &painter, self);
            } else
#endif
            XPainter_fillRect(&painter, &line, mid);
            y += sp->m_handleWidth;
        }
    }
    (void)y;
    (void)x;
    XPainter_deinit(&painter);
}

static void VX_splitBar_paintEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
}

/* ---- 拖动状态（XSplitter.h 为契约头不扩字段）：按压偏移经对象动态
 * 属性承载（同 XMenu 悬停弹出记账的定式，XObject_setProperty）；
 * 把手索引用既有 m_dragIndex 字段（此前为死状态，见台账 #60）。 ---- */
#define XSPLITTER_PROP_PRESS_OFFSET "xgui.splitter.pressOffset"

static void xsp_setPressOffset(XSplitter* self, int64_t value)
{
    XString key;
    XVariant* v;
    if (!self) return;
    v = XVariant_create_int64(value);
    if (!v) return;
    XString_init(&key);
    XString_assign_utf8(&key, XSPLITTER_PROP_PRESS_OFFSET);
    /* setProperty 成功后变体所有权转移给对象；失败则自回滚防泄漏。 */
    if (!XObject_setProperty((XObject*)self, &key, v))
        XVariant_delete_base((XClass*)v);
    XString_deinit_base(&key);
}

static int64_t xsp_pressOffset(const XSplitter* self)
{
    XString key;
    XVariant* v;
    if (!self) return 0;
    XString_init(&key);
    XString_assign_utf8(&key, XSPLITTER_PROP_PRESS_OFFSET);
    v = XObject_property((const XObject*)self, &key);
    XString_deinit_base(&key);
    return v ? XVariant_toInt64(v) : 0;
}

/** @brief 拾取坐标（水平分割取 x、垂直取 y，对标 QSplitterPrivate::pick）。 */
static int xsp_pick(int x, int y, bool horiz)
{
    return horiz ? x : y;
}

/** @brief 命中测试：局部坐标落在第 index 个分隔条条带内则返回其索引。 */
static int xsp_handleAt(XSplitter* self, int x, int y)
{
    int count;
    int i;
    if (!self) return -1;
    count = xsp_childCount(self);
    for (i = 0; i < count - 1; ++i) {
        XRect hr;
        if (!XSplitter_handle(self, i, &hr)) continue;
        if (xsp_horiz(self)) {
            if (x >= hr.x && x < hr.x + hr.width) return i;
        } else {
            if (y >= hr.y && y < hr.y + hr.height) return i;
        }
    }
    return -1;
}

/** @brief 把分隔条 index 移到 pos（对标 QSplitter::moveSplitter，
 *  qsplitter.cpp:1391-1419：先夹取到合法区间，index 页吸收位移、
 *  index+1 页让出，其后各页整体平移；末端发射 splitterMoved）。
 *  非透明拖动（m_opaqueResize=false）简化为同实时路径。
 *  @note  pos 语义 = 分隔条条带原点 = 页 index 右缘/下缘（本文件
 *  xsp_handleAt/XSplitter_handle 的条带坐标口径，即 Qt handle 的
 *  pick(s->rect.bottomRight())+1）。二次复扫钉死的破坏性根因：此前
 *  oldPos 取页 index 左/上缘（页 0 恒 0），首次 MOVE 的 delta 被放大
 *  成「目标位-0」（按复扫 d1→d2 帧推算 ≈ +381），next 页几何立即负
 *  宽归零、把手推出容器外——整页擦空且把手/右页永不恢复（rescan
 *  d1~d4 四帧证据）。
 *  Qt 的 oldP= pick(页 index rect.topLeft()) 之所以可用，是因为其 pos
 *  以「页 index 左缘」为坐标（setGeo: positions[index]=hPos+hs，把位
 *  = 页左缘-hs）；本实现的 pos 以条带原点为坐标，两口径差一个页宽，
 *  不得混用。 */
static void xsp_moveSplitter(XSplitter* self, int pos, int index)
{
    XWidget* cur;
    XWidget* next;
    int min;
    int max;
    int oldPos;
    int delta;
    int count;
    int i;
    int trailing;
    if (!self) return;
    count = xsp_childCount(self);
    if (index < 0 || index >= count - 1) return;
    cur = xsp_childAt(self, index);
    next = xsp_childAt(self, index + 1);
    if (!cur || !next) return;
    /* 合法区间（折叠语义并入，见 getRange；对标 adjustPos 的夹取）。 */
    if (!XSplitter_getRange(self, index, &min, &max)) return;
    if (pos < min) pos = min;
    if (pos > max) pos = max;
    /* 旧把位 = 条带原点 = 页 index 右缘（水平）/下缘（垂直）。 */
    oldPos = xsp_horiz(self)
        ? XWidget_x(cur) + XWidget_width(cur)
        : XWidget_y(cur) + XWidget_height(cur);
    delta = pos - oldPos;
    if (delta == 0) return;
    /* 防御：夹取后两页尺寸均不得为负（Qt setGeo 允许折叠为 0，
       但不允许负几何——负值经 clampSize 归 0 会让页永久消失）。 */
    if (xsp_pageSize(self, index) + delta < 0) delta = -xsp_pageSize(self, index);
    if (xsp_pageSize(self, index + 1) - delta < 0) delta = xsp_pageSize(self, index + 1);
    if (delta == 0) return;
    if (xsp_horiz(self)) {
        XRect r;
        XRect_init(&r, XWidget_x(cur), 0,
                   XWidget_width(cur) + delta, XWidget_height(cur));
        XWidget_setGeometryRect(cur, &r);
        XRect_init(&r, XWidget_x(next) + delta, 0,
                   XWidget_width(next) - delta, XWidget_height(next));
        XWidget_setGeometryRect(next, &r);
        /* 后续页保持自身尺寸整体平移（对标 doMove 的尾页处理简化）。
           next 的几何已按新尺寸落位，其右缘 + 把手宽 = 后续页起点。 */
        trailing = XWidget_x(next) + XWidget_width(next)
                   + self->m_handleWidth;
        for (i = index + 2; i < count; ++i) {
            XWidget* ch = xsp_childAt(self, i);
            if (!ch) continue;
            XRect_init(&r, trailing, 0, XWidget_width(ch),
                       XWidget_height((XWidget*)self));
            XWidget_setGeometryRect(ch, &r);
            trailing += XWidget_width(ch) + self->m_handleWidth;
        }
    } else {
        XRect r;
        XRect_init(&r, 0, XWidget_y(cur), XWidget_width((XWidget*)self),
                   XWidget_height(cur) + delta);
        XWidget_setGeometryRect(cur, &r);
        XRect_init(&r, 0, XWidget_y(next) + delta,
                   XWidget_width((XWidget*)self),
                   XWidget_height(next) - delta);
        XWidget_setGeometryRect(next, &r);
        /* 同水平分支：next 已按新几何落位，下缘 + 把手高 = 后续页起点。 */
        trailing = XWidget_y(next) + XWidget_height(next)
                   + self->m_handleWidth;
        for (i = index + 2; i < count; ++i) {
            XWidget* ch = xsp_childAt(self, i);
            if (!ch) continue;
            XRect_init(&r, 0, trailing, XWidget_width((XWidget*)self),
                       XWidget_height(ch));
            XWidget_setGeometryRect(ch, &r);
            trailing += XWidget_height(ch) + self->m_handleWidth;
        }
    }
    XWidget_update((XWidget*)self);
    xsp_emitMoved(self, pos, index);
}

static void VX_splitter_mousePressEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    XRect hr;
    int handle;
    if (!sp || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    handle = xsp_handleAt(sp, pos.x, pos.y);
    if (handle < 0) {
        /* 非把手区域：不占用事件（页内点击照常穿透）。 */
        XEvent_ignore(event);
        return;
    }
    if (XSplitter_handle(sp, handle, &hr)) {
        int offset = xsp_pick(pos.x, pos.y, xsp_horiz(sp) != 0)
                     - xsp_pick(hr.x, hr.y, xsp_horiz(sp) != 0);
        xsp_setPressOffset(sp, offset);
    }
    sp->m_dragIndex = handle;
    /* 抓取鼠标：释放点可能已移出分割器（快速拖动），不抓取会把
       RELEASE 路由给别的控件、m_dragIndex 卡在拖动态（对标 QWidget::
       grabMouse 的拖动语义，同 XScrollBar 拖滑块口径）。 */
    XWidget_grabMouse((XWidget*)sp);
    XEvent_accept(event);
}

/** @brief 拖动中：把手目标位 = 指针拾取坐标 - 按压偏移（对标
 *  QSplitterHandle::mouseMoveEvent，qsplitter.cpp:255-265）。 */
static void VX_splitter_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int target;
    if (!sp || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (sp->m_dragIndex < 0) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    target = xsp_pick(pos.x, pos.y, xsp_horiz(sp) != 0)
             - (int)xsp_pressOffset(sp);
    xsp_moveSplitter(sp, target, sp->m_dragIndex);
    XEvent_accept(event);
}

/** @brief 释放结束拖动（对标 QSplitterHandle::mouseReleaseEvent，
 *  qsplitter.cpp:286-300：收尾并把手回到静止态）。 */
static void VX_splitter_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sp || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (sp->m_dragIndex < 0) {
        XEvent_ignore(event);
        return;
    }
    if (XMouseEvent_button(me) != XMouseButton_NoButton &&
        XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    sp->m_dragIndex = -1;
    XWidget_releaseMouse((XWidget*)sp);
    XEvent_accept(event);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_splitter_deinit(XSplitter* self)
{
    if (!self) return;
    if (self->m_collapsible) {
        XFree_System(self->m_collapsible);
        self->m_collapsible = NULL;
        self->m_collapsibleCap = 0;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XVtable* XSplitter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplitter)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_splitter_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_splitter_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_splitter_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_splitter_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_splitter_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_splitter_deinit);
    return XVTABLE_DEFAULT;
}

static void xsp_ensureCollapsibleCap(XSplitter* self, int pages)
{
    if (pages <= self->m_collapsibleCap) return;
    {
        int newCap = pages + 8;
        int* p = (int*)XRealloc_System(self->m_collapsible,
                                       (size_t)newCap * sizeof(int));
        int i;
        if (!p) return;
        /* 新槽位必须补 -1（未覆写哨兵）：isCollapsible 以 <0 回退
           childrenCollapsible 全局开关；realloc 遗留垃圾曾使全局回退
           失效（垃圾非 0 即被当作“已覆写=true”）。 */
        for (i = self->m_collapsibleCap; i < newCap; ++i) p[i] = -1;
        self->m_collapsible = p;
        self->m_collapsibleCap = newCap;
    }
}

void XSplitter_init(XSplitter* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XSplitter);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_orientation = 1;
    self->m_handleWidth = 5;
    self->m_childrenCollapsible = true;
    self->m_opaqueResize = true;
    self->m_dragIndex = -1;
}

void XSplitter_init_2(XSplitter* self, int orientation, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XSplitter_init(self, parent, flags);
    self->m_orientation = orientation;
}

XSplitter* XSplitter_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XSplitter* XSplitter_create_ex_2(XMemoryType memory, int orientation,
                                 XWidget* parent, XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init_2(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

void XSplitter_addWidget(XSplitter* self, XWidget* widget)
{
    if (!self || !widget) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, true);
    xsp_layout(self);
}

void XSplitter_insertWidget(XSplitter* self, int index, XWidget* widget)
{
    if (!self || !widget) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, true);
    /* Qt insertWidget 按插入顺序布局；第一版以加入顺序近似。 */
    xsp_layout(self);
}

XWidget* XSplitter_widget(const XSplitter* self, int index)
{
    if (!self || index < 0 || index >= xsp_childCount(self))
        return NULL;
    return xsp_childAt(self, index);
}

int XSplitter_count(const XSplitter* self)
{
    return self ? xsp_childCount(self) : 0;
}

int XSplitter_indexOf(const XSplitter* self, const XWidget* widget)
{
    int i;
    int count;
    if (!self || !widget) return -1;
    count = xsp_childCount(self);
    for (i = 0; i < count; ++i) {
        if (xsp_childAt(self, i) == widget) return i;
    }
    return -1;
}

/* ==================== 把手与替换（对标 QSplitter::handle/getRange/replaceWidget） ==================== */

/** @brief 查询单页在拖动方向上的当前尺寸（水平取宽、垂直取高）。 */
static int xsp_pageSize(const XSplitter* self, int index)
{
    XWidget* child = xsp_childAt(self, index);
    if (!child) return 0;
    return xsp_horiz(self) ? XWidget_width(child) : XWidget_height(child);
}

XWidget* XSplitter_replaceWidget(XSplitter* self, int index, XWidget* widget)
{
    XWidget* current;
    XRect geom;
    bool wasVisible;
    XVector* children;
    if (!self || !widget) return NULL;
    if (index < 0 || index >= xsp_childCount(self)) return NULL;
    current = xsp_childAt(self, index);
    if (!current || current == widget) return NULL;
    /* Qt 护栏：新控件已是本分割器子控件（兄弟）时不替换。 */
    if (XSplitter_indexOf(self, widget) >= 0) return NULL;
    geom = XWidget_geometry(current);
    /* Qt 口径：replaceWidget 以 isHidden()（控件自身显式显隐位）继承
     * 可见性，不取 effective visible——后者随父链（顶层分割器未 show
     * 时恒 false）波动，headless 下会把新页误标隐藏，进而被把位扣除
     * 跳过（与 xsp_contentLen 同口径）。 */
    wasVisible = !XWidget_isHidden(current);
    /* 旧控件解除父子关系（自 children 向量移除）并隐藏，交还调用方
       管理（不销毁，对标 Qt replaceWidget 的 setParent(nullptr)）。 */
    XWidget_setParent(current, NULL, 0);
    XWidget_setVisible(current, false);
    /* 新控件经 reparent 挂为本控件子控件（追加到 children 末尾，
       所有权归分割器父子链，与 addWidget 一致）。 */
    XWidget_setParent(widget, (XWidget*)self, 0);
    children = (XVector*)XObject_children((const XObject*)self);
    if (children)
        XVector_move(children,
                     (int64_t)XVector_size_base((const XContainer*)children) - 1,
                     index);
    /* 继承被替换控件的几何与可见状态（对标 Qt）。 */
    XWidget_setGeometryRect(widget, &geom);
    XWidget_setVisible(widget, wasVisible);
    xsp_layout(self);
    return current;
}

bool XSplitter_handle(const XSplitter* self, int index, XRect* out)
{
    XWidget* child;
    int count;
    if (!self || index < 0) return false;
    count = xsp_childCount(self);
    /* 分隔点 index 位于页 index 与页 index+1 之间，有效 0..count-2。 */
    if (count < 2 || index >= count - 1) return false;
    child = xsp_childAt(self, index);
    if (!child) return false;
    if (out) {
        /* 内部无把手部件对象（Qt 为 QSplitterHandle*）：以页 index
           几何之后的 handleWidth 条带矩形承载（局部坐标）。 */
        if (xsp_horiz(self))
            XRect_init(out, XWidget_x(child) + XWidget_width(child), 0,
                       self->m_handleWidth,
                       XWidget_height((XWidget*)self));
        else
            XRect_init(out, 0, XWidget_y(child) + XWidget_height(child),
                       XWidget_width((XWidget*)self),
                       self->m_handleWidth);
    }
    return true;
}

bool XSplitter_getRange(const XSplitter* self, int index, int* min, int* max)
{
    int count;
    int total;
    int minPos = 0;
    int maxPos;
    int i;
    if (!self) return false;
    count = xsp_childCount(self);
    if (index < 0 || count < 2 || index >= count - 1) return false;
    total = xsp_contentLen(self);
    /* 公开 getRange 含折叠语义（对标 Qt farMin/farMax）：可折叠页最
       小贡献 0；不可折叠页以当前尺寸作为最小值代理（无
       minimumSizeHint 承载，Qt 以 qSmartMinSize 计算）。 */
    for (i = 0; i <= index; ++i)
        if (!XSplitter_isCollapsible(self, i)) minPos += xsp_pageSize(self, i);
    maxPos = total;
    for (i = index + 1; i < count; ++i)
        if (!XSplitter_isCollapsible(self, i)) maxPos -= xsp_pageSize(self, i);
    if (maxPos < minPos) maxPos = minPos;
    if (minPos < 0) minPos = 0;
    if (min) *min = minPos;
    if (max) *max = maxPos;
    return true;
}

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, int orientation)
{
    if (!self || self->m_orientation == orientation) return;
    self->m_orientation = orientation;
    xsp_layout(self);
}

int XSplitter_orientation(const XSplitter* self)
{
    return self ? self->m_orientation : 1;
}

void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible)
{
    if (!self) return;
    self->m_childrenCollapsible = collapsible;
}

bool XSplitter_childrenCollapsible(const XSplitter* self)
{
    return self ? self->m_childrenCollapsible : false;
}

void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible)
{
    if (!self || index < 0) return;
    xsp_ensureCollapsibleCap(self, index + 1);
    if (!self->m_collapsible || index >= self->m_collapsibleCap) return;
    self->m_collapsible[index] = collapsible ? 1 : 0;
}

bool XSplitter_isCollapsible(const XSplitter* self, int index)
{
    if (!self || index < 0) return false;
    if (!self->m_collapsible || index >= self->m_collapsibleCap)
        return self->m_childrenCollapsible;
    if (self->m_collapsible[index] < 0)
        return self->m_childrenCollapsible;
    return self->m_collapsible[index] != 0;
}

void XSplitter_setOpaqueResize(XSplitter* self, bool opaque)
{
    if (!self) return;
    self->m_opaqueResize = opaque;
}

bool XSplitter_opaqueResize(const XSplitter* self)
{
    return self ? self->m_opaqueResize : false;
}

int XSplitter_handleWidth(const XSplitter* self)
{
    return self ? self->m_handleWidth : 0;
}

void XSplitter_setHandleWidth(XSplitter* self, int width)
{
    if (!self || width < 0) return;
    self->m_handleWidth = width;
    xsp_layout(self);
}

void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch)
{
    /* 对标 Qt：stretch 因子参与尺寸分配；第一版布局为均分，
       stretch 仅存储于折叠数组的扩展语义之外（无可见行为差异）。 */
    (void)self;
    (void)index;
    (void)stretch;
}

void XSplitter_refresh(XSplitter* self)
{
    xsp_layout(self);
}

/* ==================== 尺寸与状态 ==================== */

void XSplitter_sizes(const XSplitter* self, int* outSizes, int count)
{
    int i;
    if (!self || !outSizes || count <= 0) return;
    for (i = 0; i < count; ++i) {
        XWidget* child = (i < xsp_childCount(self))
                             ? xsp_childAt(self, i) : NULL;
        if (!child) {
            outSizes[i] = 0;
            continue;
        }
        outSizes[i] = xsp_horiz(self) ? XWidget_width(child)
                                      : XWidget_height(child);
    }
}

void XSplitter_setSizes(XSplitter* self, const int* sizes, int count)
{
    int total;
    int sum = 0;
    int i;
    int x = 0;
    int y = 0;
    if (!self || !sizes || count <= 0) return;
    total = xsp_contentLen(self);
    for (i = 0; i < count; ++i) sum += sizes[i] > 0 ? sizes[i] : 0;
    if (sum <= 0) return;
    for (i = 0; i < count && i < xsp_childCount(self); ++i) {
        XWidget* child = xsp_childAt(self, i);
        XRect r;
        int size = (int)(((long long)sizes[i] > 0 ? sizes[i] : 0) *
                         total / sum);
        if (!child) continue;
        if (xsp_horiz(self))
            XRect_init(&r, x, 0, size, XWidget_height((XWidget*)self));
        else
            XRect_init(&r, 0, y, XWidget_width((XWidget*)self), size);
        XWidget_setGeometryRect(child, &r);
        if (xsp_horiz(self)) x += size + self->m_handleWidth;
        else y += size + self->m_handleWidth;
    }
}

#if XByteArray_ON
XByteArray* XSplitter_saveState(const XSplitter* self)
{
    XByteArray* out;
    int i;
    int count;
    char header[32];
    if (!self) return NULL;
    out = XByteArray_create();
    if (!out) return NULL;
    count = xsp_childCount(self);
    XSnprintf(header, sizeof(header), "XSP%03d%03d", self->m_orientation,
             count);
    XByteArray_append_utf8(out, header);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        char buf[16];
        int size = 0;
        if (child)
            size = xsp_horiz(self) ? XWidget_width(child)
                                   : XWidget_height(child);
        XSnprintf(buf, sizeof(buf), "%05d", size);
        XByteArray_append_utf8(out, buf);
    }
    return out;
}

bool XSplitter_restoreState(XSplitter* self, const XByteArray* state)
{
    const char* data;
    int orientation;
    int count;
    int i;
    char buf[8];
    if (!self || !state) return false;
    data = (const char*)XByteArray_constData(state);
    if (!data || XStrncmp(data, "XSP", 3) != 0) return false;
    orientation = (data[3] - '0') * 100 + (data[4] - '0') * 10 +
                  (data[5] - '0');
    count = (data[6] - '0') * 100 + (data[7] - '0') * 10 +
            (data[8] - '0');
    if (orientation != 1 && orientation != 2) return false;
    if (count != xsp_childCount(self)) return false;
    self->m_orientation = orientation;
    {
        int* sizes = (int*)XMalloc_System((size_t)count * sizeof(int));
        if (!sizes) return false;
        for (i = 0; i < count; ++i) {
            XMemcpy(buf, data + 9 + i * 5, 5);
            buf[5] = '\0';
            {
                int32_t v = 0;
                sizes[i] = (str_to_int32(buf, &v) == CONV_OK) ? (int)v : 0;
            }
        }
        XSplitter_setSizes(self, sizes, count);
        XFree_System(sizes);
    }
    return true;
}
#endif /* XByteArray_ON */

/* ==================== 信号 ==================== */

void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index)
{
    (void)self;
    (void)pos;
    (void)index;
    return (void*)(size_t)XSplitter_splitterMoved_signal;
}










#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
