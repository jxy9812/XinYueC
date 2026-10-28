/**
 * @file       XAbstractScrollArea.c
 * @brief      抽象滚动区域基类实现（对标 Qt 6.8 QAbstractScrollArea 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XAbstractScrollArea.h"
#include "XAbstractScrollArea_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XAlignment.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

/** @brief 滚轮：转发到滚动条步进（对标 QScrollArea 视口滚轮滚动）。
 *         默认滚垂直条，按住 Shift 滚水平条，120 角度 = 3 倍单步，
 *         保证滚动幅度肉眼可见。 */
static void VX_asa_wheelEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    XScrollBar* bar;
    int steps = 0;
    bool shift = false;
    bool horizontal = false;
    if (!area || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        int dy;
        shift = (XWheelEvent_modifiers(we) &
                 XKeyboardModifier_ShiftModifier) != 0;
        /* [方向根修 2026-10-14] Qt6 真机基准（Xvfb+XTEST 实测
         * QScrollBar valueChanged）：竖滚上推（angleDelta.y=+120）→
         * value 减小（露出文档前部），下推 → 增大；横滚右推
         * （angleDelta.x=+120）→ value 增大，左推 → 减小（QScrollBar
         * invertedControls 默认 true：竖滚双重翻转、横滚 delta=-delta
         * 后再翻转，净符号=竖负横正，实测复现）。本框架 StepBy 正值
         * 恒=value 增，故竖滚取负号、横滚保同号（此前两轴同号=竖滚
         * 方向颠倒）。选条口径对齐 QAbstractScrollArea::wheelEvent：
         * |x|>|y| 走横条；Shift 维持本框架横滚历史口径。 */
        horizontal = shift || abs(delta.x) > abs(delta.y) ||
                     (delta.y == 0 && delta.x != 0);
        dy = horizontal ? delta.x
                        : (delta.y != 0 ? delta.y : delta.x);
        steps = dy / 120;
    }
#endif
    if (steps == 0) { XEvent_accept(event); return; }
    bar = horizontal ? XAbstractScrollArea_horizontalScrollBar(area)
                     : XAbstractScrollArea_verticalScrollBar(area);
    XAbstractSlider_stepBy_base((XAbstractSlider*)bar,
                                horizontal ? steps * 3 : -steps * 3);
    XEvent_accept(event);
}
/** @brief 滚动条 value 变化 → 调 scrollContentsBy 虚槽。 */
static void xasa_vScrollChangedSlot(XObject* receiver, XVarList* args)
{
    XAbstractScrollArea* self = (XAbstractScrollArea*)receiver;
    if (!self || !args) return;
    (void)args;
    XAbstractScrollArea_scrollContentsBy_base(self, 0, 1);
}

static void xasa_hScrollChangedSlot(XObject* receiver, XVarList* args)
{
    XAbstractScrollArea* self = (XAbstractScrollArea*)receiver;
    if (!self || !args) return;
    (void)args;
    XAbstractScrollArea_scrollContentsBy_base(self, 1, 0);
}

/** @brief 连接滚动条 valueChanged → 内容滚动虚槽。 */
static void xasa_connectBar(XAbstractScrollArea* self, XScrollBar* bar,
                            bool horizontal)
{
    if (!self || !bar) return;
    if (horizontal)
        XObject_connect_1((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_hScrollChangedSlot,
                          XConnectionType_Direct);
    else
        XObject_connect_1((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_vScrollChangedSlot,
                          XConnectionType_Direct);
}

/** @brief 断开滚动条连接（替换滚动条前调用）。 */
static void xasa_disconnectBar(XAbstractScrollArea* self, XScrollBar* bar,
                               bool horizontal)
{
    if (!self || !bar) return;
    XObject_disconnect_1((XObject*)bar,
                         (size_t)XScrollBar_valueChanged_signal(bar, 0),
                         (XObject*)self,
                         horizontal ? xasa_hScrollChangedSlot
                                    : xasa_vScrollChangedSlot);
}

/** @brief 视口/滚动条几何排布前置声明（显隐联动重排用）。 */
static void xasa_layout(XAbstractScrollArea* self);

/** @brief AsNeeded 双条联立判定（两遍法，updateScrollBars 与 layout
 *         共用一份口径）。
 *  第一遍以「无条全视口」(w×h) 为基准定初判；第二遍按另一条的初判
 *  占位 (sbw) 修正基准补判。此前两处判定各自以当前视口 (vw/vh，可能
 *  仍被上一拍亮着的条占位) 独立判定——收缩过渡态双条同亮后即成自洽
 *  死点：竖条亮→横条以扣宽视口误亮→竖条以扣高视口续亮，内容明明
 *  放得下却双条常驻（Modbus 主站页 1244×1286→×700 实测复现，内容
 *  1044×586 < 视口 1044×594 仍双条悬挂）。两遍法各组合一期收敛，
 *  双亮仅当双条确都必需。 */
static void xasa_decideBars(const XAbstractScrollArea* self, int w, int h,
                            int sbw, bool* showV, bool* showH)
{
    bool vOn;
    bool hOn;
    if (!self) { *showV = false; *showH = false; return; }
    vOn = self->m_vPolicy != XScrollBarPolicy_AlwaysOff &&
          (self->m_vPolicy == XScrollBarPolicy_AlwaysOn ||
           self->m_contentHeight > h);
    hOn = self->m_hPolicy != XScrollBarPolicy_AlwaysOff &&
          (self->m_hPolicy == XScrollBarPolicy_AlwaysOn ||
           self->m_contentWidth > w);
    if (!vOn && hOn &&
        self->m_vPolicy == XScrollBarPolicy_AsNeeded &&
        self->m_contentHeight > h - sbw)
        vOn = true; /* 横条占位后竖向放不下：补亮竖条。 */
    if (!hOn && vOn &&
        self->m_hPolicy == XScrollBarPolicy_AsNeeded &&
        self->m_contentWidth > w - sbw)
        hOn = true; /* 竖条占位后横向放不下：补亮横条。 */
    *showV = vOn;
    *showH = hOn;
}

/** @brief 视口边距读取（对标 Qt d->viewportMargins 的内部承载；轻量
 *         等价物：setter 存四向 px，layout/decideBars/maximumViewport
 *         统一经此消费）。 */
static void xasa_margins(const XAbstractScrollArea* self, int* l, int* t,
                         int* r, int* b)
{
    if (!self) { *l = 0; *t = 0; *r = 0; *b = 0; return; }
    *l = self->m_marginLeft;
    *t = self->m_marginTop;
    *r = self->m_marginRight;
    *b = self->m_marginBottom;
}

/** @brief 依据策略与内容尺寸更新滚动条可见性与范围。
 *         pageStep/range 一律取「排布后视口终值」：此前按排布前视口
 *         尺寸写入，xasa_layout 收尾才把视口缩掉滚动条宽度，状态比
 *         几何滞后一拍——收敛依赖后续恰好再有一次本调用，脏区驱动、
 *         重绘停止得早的平台会把陈旧把手长度永久留在屏上（真机取证
 *         2026-10-08 条目视图页：树竖把手 105px = 槽114×130/(10+130)，
 *         桌面多收敛一轮才得 92px = 114×114/140，其余逐像素全同）。
 *         显隐旗先按当前视口口径判定（与 xasa_layout 同式），再以
 *         「控件全尺寸 − 对侧条宽」解析推导排布后视口终值，同一调用
 *         内几何与状态必然自洽；若 layout 实际改动了视口几何（AsNeeded
 *         显隐翻转改变对侧判定口径），最多整算第二遍取不动点（定长
 *         两遍、无递归）。 */
static void xasa_updateScrollBars(XAbstractScrollArea* self)
{
    int pass;
    for (pass = 0; pass < 2; ++pass)
    {
        int vw;
        int vh;
        int w;
        int h;
        int sbw = 16;
        int finalVw;
        int finalVh;
        int oldVw;
        int oldVh;
        bool showV;
        bool showH;
        int range;
        int ml, mt, mr, mb;
        if (!self || !self->m_viewport) return;
        w = XWidget_width((XWidget*)self);
        h = XWidget_height((XWidget*)self);
        /* 视口边距内收（对标 Qt layoutChildren 的 margins 口径）：
           条显隐判定与排布后视口推导均以收边后的可用区为基准。 */
        xasa_margins(self, &ml, &mt, &mr, &mb);
        w -= ml + mr;
        h -= mt + mb;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        vw = XWidget_width(self->m_viewport);
        vh = XWidget_height(self->m_viewport);
        oldVw = vw;
        oldVh = vh;
        /* 显隐判定联立两遍法（xasa_decideBars，与 xasa_layout 同一份
           口径）：基准为无条全视口，消除双条互锁死点。 */
        xasa_decideBars(self, w, h, sbw, &showV, &showH);
        /* 排布后视口终值：与 xasa_layout 的视口矩形同式推导（竖条显形
         * 扣宽、横条显形扣高）。翻页步进 = 视口尺寸（对标 Qt
         * QAbstractScrollArea，参照 XTextEdit.c:813 setPageStep(viewH)
         * 正确示范）；隐藏分支同样设置：range=0 时把手折算占满全槽，
         * 无显示副作用。 */
        finalVw = showV ? w - sbw : w;
        finalVh = showH ? h - sbw : h;
        if (finalVw < 0) finalVw = 0;
        if (finalVh < 0) finalVh = 0;
        XScrollBar_setPageStep(self->m_vScrollBar, finalVh > 0 ? finalVh : 1);
        XScrollBar_setPageStep(self->m_hScrollBar, finalVw > 0 ? finalVw : 1);
        XWidget_setVisible((XWidget*)self->m_vScrollBar, showV);
        XWidget_setVisible((XWidget*)self->m_hScrollBar, showH);
        if (showV) {
            range = self->m_contentHeight > finalVh
                        ? self->m_contentHeight - finalVh
                        : 0;
            XScrollBar_setRange(self->m_vScrollBar, 0, range);
        }
        else {
            /* 隐藏时同步清零范围：内容未超出视口时 value 恒为 0，滚轮与
               编程接口都不能滚动（对标 Qt 滚动条 max=0 的不可滚语义）。 */
            XScrollBar_setRange(self->m_vScrollBar, 0, 0);
        }
        if (showH) {
            range = self->m_contentWidth > finalVw
                        ? self->m_contentWidth - finalVw
                        : 0;
            XScrollBar_setRange(self->m_hScrollBar, 0, range);
        }
        else {
            XScrollBar_setRange(self->m_hScrollBar, 0, 0);
        }
        /* 对标 Qt updateScrollBars 的 layout 联动（qabstractscrollarea.cpp
         * 每次范围/显隐更新即 updateGeometries）：无条件全量重排。仅靠
         * 显隐翻转触发不够——条显隐恒真而控件几何早已变化时（如容器
         * resize 前 AsNeeded 已显示的条），几何会永久滞留旧值（实测水平
         * 条滞留 (0,84,200,16) 挂在内容区中部）。xasa_layout 幂等且不回调
         * 本函数，无递归。 */
        xasa_layout(self);
        if (XWidget_width(self->m_viewport) == oldVw &&
            XWidget_height(self->m_viewport) == oldVh)
            break; /* 视口几何未再变化：pageStep/range 已与终态自洽 */
    }
}

/* ==================== 事件处理 ==================== */

/** @brief 视口/滚动条/corner/附加控件几何排布（不含范围刷新）。 */
static void xasa_layout(XAbstractScrollArea* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int sbw = 16;
    bool showV;
    bool showH;
    XRect r;
    int ml, mt, mr, mb;
    int ox;
    int oy;
    if (!self || !self->m_viewport) return;
    /* 视口边距（setViewportMargins，对标 Qt layoutChildren 的
       margins 口径）：可视内容区整体内收，视口/滚动条/角件均在
       收边后的矩形内排布。 */
    xasa_margins(self, &ml, &mt, &mr, &mb);
    ox = ml;
    oy = mt;
    w -= ml + mr;
    h -= mt + mb;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* 显隐判定与 xasa_updateScrollBars 同一份联立口径（xasa_decideBars，
       基准=无条全视口）：两处分裂时条会「显示却不排布」，而各自独立
       按当前视口判定时会双条互锁（详见 xasa_decideBars 注）。 */
    xasa_decideBars(self, w, h, sbw, &showV, &showH);
    XRect_init(&r, ox, oy, showV ? w - sbw : w, showH ? h - sbw : h);
    XWidget_setGeometryRect(self->m_viewport, &r);
    if (showV) {
        XRect_init(&r, ox + w - sbw, oy, sbw, showH ? h - sbw : h);
        XWidget_setGeometryRect((XWidget*)self->m_vScrollBar, &r);
    }
    if (showH) {
        XRect_init(&r, ox, oy + h - sbw, showV ? w - sbw : w, sbw);
        XWidget_setGeometryRect((XWidget*)self->m_hScrollBar, &r);
    }
    /* 右下角控件：仅当两个滚动条都显示时可见。 */
    if (self->m_cornerWidget) {
        if (showV && showH) {
            XRect_init(&r, ox + w - sbw, oy + h - sbw, sbw, sbw);
            XWidget_setGeometryRect(self->m_cornerWidget, &r);
            XWidget_setVisible(self->m_cornerWidget, true);
        } else {
            XWidget_setVisible(self->m_cornerWidget, false);
        }
    }
    /* 附加滚动条控件（addScrollBarWidget）：按对齐位挂靠边缘；
     * 简化排布：覆盖在对应边缘，不参与视口尺寸计算（头文件注明）。 */
    {
        int i;
        for (i = 0; i < self->m_sbWidgetCount; ++i) {
            XWidget* sw = self->m_sbWidgets[i];
            int al = self->m_sbWidgetAligns[i];
            if (!sw) continue;
            if (al & (int)XAlignment_Top) {
                XRect_init(&r, ox, oy, w, sbw);
                XWidget_setGeometryRect(sw, &r);
            } else if (al & (int)XAlignment_Bottom) {
                XRect_init(&r, ox, oy + h - sbw, w, sbw);
                XWidget_setGeometryRect(sw, &r);
            } else if (al & (int)XAlignment_Left) {
                XRect_init(&r, ox, oy, sbw, h);
                XWidget_setGeometryRect(sw, &r);
            } else { /* Right（含默认） */
                XRect_init(&r, ox + w - sbw, oy, sbw, h);
                XWidget_setGeometryRect(sw, &r);
            }
        }
    }
}

static void VX_asa_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    (void)event;
    if (!area || !area->m_viewport) return;
    xasa_layout(area);
    xasa_updateScrollBars(area);
}

static void VX_asa_paintEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect line;
    uint32_t mid;
    int w;
    int h;
    if (!area || !event) return;
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
                                  XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 背景：基色清屏防止父控件渲染透出。 */
    {
        XRect bg = { 0, 0, w, h };
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor bc = XPalette_color(&palette, XPaletteColorGroup_Current,
                                   XPaletteColorRole_Base);
        XPainter_fillRect(&painter, &bg, XColor_rgba(&bc));
#else
        XPainter_fillRect(&painter, &bg, 0xFFFFFFFFu);
#endif /* XPALETTE_ON */
    }
    XRect_init(&line, 0, h - 1, w, 1);
    XPainter_fillRect(&painter, &line, mid);
    XPainter_deinit(&painter);
}

/** @brief 默认内容滚动：平移视口内的内容偏移（第一版为空操作占位，
 *         派生类 XScrollArea 覆写驱动内容 widget 位置）。 */
static void VX_asa_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    (void)self;
    (void)dx;
    (void)dy;
}

void XAbstractScrollArea_scrollContentsBy_base(XAbstractScrollArea* self,
                                               int dx, int dy)
{
    void (*fn)(XAbstractScrollArea*, int, int);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXAbstractScrollArea_ScrollContentsBy,
                              void (*)(XAbstractScrollArea*, int, int));
    /* 子类未覆盖时无默认实现：不调用，避免空槽指针崩溃。 */
    if (!fn) return;
    fn(self, dx, dy);
}

void XAbstractScrollArea_resizeEvent_base(XAbstractScrollArea* self)
{
    if (!self) return;
    /* 直调父类槽位实现（XAbstractScrollArea_class_init() 虚表，等价
       XClass_Parent）：不可走对象虚表，否则子类重载 ResizeEvent 后会
       经对象表再次进入子类自身（递归/栈溢出，见代码风格文档约定）。 */
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)((XWidget*)self, NULL);
}

/* ==================== 生命周期与虚表 ==================== */

/* [死码清理] VX_asa_deinit 已删除：全仓无调用点（见审计清单）。
 */

XVtable* XAbstractScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractScrollArea)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_asa_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_asa_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_asa_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_asa_wheelEvent);
    return XVTABLE_DEFAULT;
}

void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent,
                              XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractScrollArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt QAbstractScrollArea::init 缺省 Qt::StrongFocus
       （qabstractscrollarea.cpp:273）：点击可聚焦，滚轮/键盘可接管。
       此前沿基类 NoFocus——点击不改焦点，表格内 cellWidget 编辑器
       失焦链路（点表格→编辑器 focusOut→确认收框）死路。 */
    self->m_base.m_base.m_focusPolicy = XWidgetFocusPolicy_StrongFocus;
    self->m_viewport = XWidget_create(self, 0);
    /* [2026-10-15 定版撤回] Qt 对标的视口 Base+autofill 两行经实验
       撤回：增量重绘路径下视口底色存在父/子绘序倒挂——滚动后视口
       autofill 的 Base 填充落在内容控件（表格）绘制之后，整块盖掉
       表格（桌面 1000x600 滚轮复现，回读取证 entry=ffffff→end=
       efefef）。底色语义由内容控件自身 autofill 承担（见 Modbus 页
       body 同日改动），视口维持透明。 */
    /* 对标 qabstractscrollarea.cpp:272 viewport->setFocusProxy(q)：
       视口命中时焦点代理到滚动区本体（viewport 缺省 NoFocus，点击
       事件按命中靶查策略位——无代理则点击视口永不聚焦滚动区）。 */
    XWidget_setFocusProxy(self->m_viewport, (XWidget*)self);
    /* 对标 qabstractscrollarea.cpp:272 viewport->setFocusProxy(q)：
       视口命中时焦点代理到滚动区本体（viewport 缺省 NoFocus，点击
       事件按命中靶查策略位——无代理则点击视口永不聚焦滚动区）。 */
    XWidget_setFocusProxy(self->m_viewport, (XWidget*)self);
    /* FocusOwnerQuiet（脏区收窄 2026-10-15）：滚区/条目视图族的
       paintEvent 不读 HasFocus 态（焦点视觉=当前格高亮，由子矩形路径
       自管），焦点迁移免整件补 update——真机上免掉每次点击聚焦的
       视口带整刷（RGB565 ~380KB 提交税）。 */
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_FocusOwnerQuiet, true);
    /* viewport 是真实焦点靶（deepestFocusProxy 终点），焦点默认槽的
     * FocusIn/FocusOut 直接落在它身上——同免整件 update（否则每次
     * 聚焦变化仍经默认槽刷整个视口带）。 */
    XWidget_setAttribute((XWidget*)self->m_viewport,
                         XWidgetAttribute_FocusOwnerQuiet, true);
    self->m_vScrollBar = XScrollBar_create_2(
        (int)XAbstractSliderOrientation_Vertical, self, 0);
    self->m_hScrollBar = XScrollBar_create_2(
        (int)XAbstractSliderOrientation_Horizontal, self, 0);
    self->m_vPolicy = (uint8_t)XScrollBarPolicy_AsNeeded;
    self->m_hPolicy = (uint8_t)XScrollBarPolicy_AsNeeded;
    XWidget_setVisible((XWidget*)self->m_vScrollBar, false);
    XWidget_setVisible((XWidget*)self->m_hScrollBar, false);
    xasa_connectBar(self, self->m_vScrollBar, false);
    xasa_connectBar(self, self->m_hScrollBar, true);
    XWidget_resize(self, 200, 150);
    hint.width = 256;
    hint.height = 192;
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* [minimumSizeHint 补齐 2026-10-27 框架对齐] Qt
     * QAbstractScrollArea::minimumSizeHint（qabstractscrollarea.cpp:
     * 1405-1419）= 滚动条尺寸 + 边框（2×frameWidth）——本类无「滚动条
     * 容器」概念（Qt 的 scrollBarContainers 在 XGui 合并进滚动区自身），
     * 故取「视口最小 + 对向滚动条厚度 + 边框」。补齐后布局竖排收缩有
     * 真实下界（此前 min=0，视图族控件可被压塌至不可见）。 */
    {
        XSize minHint;
        int hbarH = 0;
        int vbarW = 0;
        XSize sh;
        if (self->m_hScrollBar) {
            sh = XWidget_sizeHint((XWidget*)self->m_hScrollBar);
            hbarH = sh.height;
        }
        if (self->m_vScrollBar) {
            sh = XWidget_sizeHint((XWidget*)self->m_vScrollBar);
            vbarW = sh.width;
        }
        XSize_init(&minHint, vbarW + 2, hbarH + 2);
        XWidget_setMinimumSizeHint((XWidget*)self, &minHint);
    }
}

XAbstractScrollArea* XAbstractScrollArea_create_ex(
    XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XAbstractScrollArea* self =
        (XAbstractScrollArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractScrollArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

XWidget* XAbstractScrollArea_viewport(const XAbstractScrollArea* self)
{
    return self ? self->m_viewport : NULL;
}

XScrollBar* XAbstractScrollArea_verticalScrollBar(
    const XAbstractScrollArea* self)
{
    return self ? self->m_vScrollBar : NULL;
}

XScrollBar* XAbstractScrollArea_horizontalScrollBar(
    const XAbstractScrollArea* self)
{
    return self ? self->m_hScrollBar : NULL;
}

void XAbstractScrollArea_setVerticalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy)
{
    if (!self) return;
    self->m_vPolicy = (uint8_t)policy;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XScrollBarPolicy XAbstractScrollArea_verticalScrollBarPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XScrollBarPolicy)self->m_vPolicy
                : XScrollBarPolicy_AsNeeded;
}

void XAbstractScrollArea_setHorizontalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy)
{
    if (!self) return;
    self->m_hPolicy = (uint8_t)policy;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XScrollBarPolicy XAbstractScrollArea_horizontalScrollBarPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XScrollBarPolicy)self->m_hPolicy
                : XScrollBarPolicy_AsNeeded;
}

void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self,
                                         XWidget* widget)
{
    XWidget* oldWidget;
    if (!self) return;
    /* 对标 QAbstractScrollArea::setCornerWidget（qabstractscrollarea.cpp:
       709-728）：换件先隐藏旧件，新件 reparent 归本控件，重排子控件
       后 show（此前为两行赋值：角件既不成为子控件也不可见）。 */
    oldWidget = self->m_cornerWidget;
    if (oldWidget != widget) {
        if (oldWidget)
            XWidget_setVisible(oldWidget, false);
        self->m_cornerWidget = widget;
        if (widget && XWidget_parentWidget(widget) != (XWidget*)self)
            XWidget_setParent(widget, (XWidget*)self, 0);
        VX_asa_resizeEvent((XWidget*)self, NULL); /* 布局 + 范围刷新。 */
        /* reparent 只挂父链不产生可见性（框架显式 show 语义），对齐
           Qt 的 widget->show() 收口；xasa_layout 随后按双条显隐仲裁
           其最终显隐。 */
        if (widget)
            XWidget_show(widget);
    } else {
        VX_asa_resizeEvent((XWidget*)self, NULL);
    }
}

XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self)
{
    return self ? self->m_cornerWidget : NULL;
}

void XAbstractScrollArea_setContentSize(XAbstractScrollArea* self,
                                        int width, int height)
{
    if (!self) return;
    /* 值不变短路：resize 逐帧路径上调用方（页面 adapt/布局）以相同
       内容尺寸反复设置，短路避免每帧 updateScrollBars→setRange→
       setValue 钳位→scrollContentsBy→viewport 全量重绘的级联。 */
    if (self->m_contentWidth == width && self->m_contentHeight == height)
        return;
    self->m_contentWidth = width;
    self->m_contentHeight = height;
    xasa_updateScrollBars(self);
}

/* ==================== Task 2.9：滚动条/视口替换与尺寸 API ========== */

void XAbstractScrollArea_setVerticalScrollBar(XAbstractScrollArea* self,
                                              XScrollBar* scrollbar)
{
    if (!self) return;
    /* 对标 Qt 6.8 首行判空拒绝（qWarning 后 return）：此前接受 NULL
     * ——删旧条后视口恒 w-16 留 16px 死列且滚动静默失效。 */
    if (!scrollbar) return;
    if (self->m_vScrollBar == scrollbar) return;
    if (self->m_vScrollBar) {
        xasa_disconnectBar(self, self->m_vScrollBar, false);
        XClassDelete((XWidget*)self->m_vScrollBar);
    }
    self->m_vScrollBar = scrollbar;
    if (scrollbar) {
        XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
        xasa_connectBar(self, scrollbar, false);
    }
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

void XAbstractScrollArea_setHorizontalScrollBar(XAbstractScrollArea* self,
                                                XScrollBar* scrollbar)
{
    if (!self) return;
    /* 同 setVerticalScrollBar：NULL 入参拒绝（Qt 对两个 setter 同一口径）。 */
    if (!scrollbar) return;
    if (self->m_hScrollBar == scrollbar) return;
    if (self->m_hScrollBar) {
        xasa_disconnectBar(self, self->m_hScrollBar, true);
        XClassDelete((XWidget*)self->m_hScrollBar);
    }
    self->m_hScrollBar = scrollbar;
    if (scrollbar) {
        XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
        xasa_connectBar(self, scrollbar, true);
    }
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

void XAbstractScrollArea_addScrollBarWidget(XAbstractScrollArea* self,
                                            XWidget* widget, int alignment)
{
    int i;
    if (!self || !widget) return;
    for (i = 0; i < self->m_sbWidgetCount; ++i)
        if (self->m_sbWidgets[i] == widget) return; /* 去重 */
    if (self->m_sbWidgetCount >= 8) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    self->m_sbWidgets[self->m_sbWidgetCount] = widget;
    self->m_sbWidgetAligns[self->m_sbWidgetCount] = (uint16_t)alignment;
    ++self->m_sbWidgetCount;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

const XWidget** XAbstractScrollArea_scrollBarWidgets(
    const XAbstractScrollArea* self, int alignment)
{
    (void)alignment;
    if (!self) return NULL;
    return (const XWidget**)self->m_sbWidgets;
}

void XAbstractScrollArea_setViewport(XAbstractScrollArea* self,
                                     XWidget* widget)
{
    if (!self || !widget) return;
    if (self->m_viewport == widget) return;
    if (self->m_viewport) {
        /* 视口是滚动区输入/绘制事件的接收主体：在视口自身事件栈内
           （视口事件→槽→setViewport）被换时，同步删会在其事件处理帧
           返回途中释放结构体造成 UAF；延迟到事件循环归还后回收
           （与 XWindowDecoration 控件释放契约同口径）。 */
        XObject_deleteLater((XObject*)self->m_viewport);
    }
    self->m_viewport = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XSize XAbstractScrollArea_maximumViewportSize(
    const XAbstractScrollArea* self)
{
    XSize out;
    int w;
    int h;
    int sbw = 16;
    bool showV;
    bool showH;
    int ml, mt, mr, mb;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    /* 对标 Qt：最大视口在视口边距内收后的矩形内推导。 */
    xasa_margins(self, &ml, &mt, &mr, &mb);
    w -= ml + mr;
    h -= mt + mb;
    showV = self->m_vScrollBar &&
            (self->m_vPolicy == XScrollBarPolicy_AlwaysOn ||
             XWidget_isVisible((XWidget*)self->m_vScrollBar));
    showH = self->m_hScrollBar &&
            (self->m_hPolicy == XScrollBarPolicy_AlwaysOn ||
             XWidget_isVisible((XWidget*)self->m_hScrollBar));
    XSize_init(&out, showV ? w - sbw : w, showH ? h - sbw : h);
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

void XAbstractScrollArea_setViewportMargins(XAbstractScrollArea* self,
                                            int left, int top,
                                            int right, int bottom)
{
    if (!self) return;
    /* 对标 QAbstractScrollArea::setViewportMargins（qabstractscrollarea.h:
       71-72，protected 面；此处按框架惯例公开）：负值钳 0，值不变
       短路，变更即重排（Qt 由此触发 layoutChildren）。 */
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right < 0) right = 0;
    if (bottom < 0) bottom = 0;
    if (self->m_marginLeft == left && self->m_marginTop == top &&
        self->m_marginRight == right && self->m_marginBottom == bottom)
        return;
    self->m_marginLeft = left;
    self->m_marginTop = top;
    self->m_marginRight = right;
    self->m_marginBottom = bottom;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

XSize XAbstractScrollArea_sizeHint(const XAbstractScrollArea* self)
{
    XSize out;
    int frame;
    int sbw = 0;
    int sbh = 0;
    bool vbarHidden;
    bool hbarHidden;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 对标 Qt QAbstractScrollArea::sizeHint：
     * - AdjustIgnored：用控件自身尺寸提示（XGui 初始化为 256x192，
     *   与 Qt 的固定返回值一致）；
     * - AdjustToContents：每次查询都按 帧宽 + 滚动条 + 视口提示 重算；
     * - AdjustToContentsOnFirstShow：首次计算后缓存（对标 Qt 的
     *   d->sizeHint 缓存），策略变更时由 setSizeAdjustPolicy 清除。 */
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored) {
        return XWidget_sizeHint((const XWidget*)self);
    }
    if (self->m_sizeAdjustPolicy ==
            XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow &&
        self->m_sizeHintCached) {
        return self->m_sizeHintCache;
    }
    frame = 2 * XFrame_frameWidth((const XFrame*)self);
    vbarHidden = !self->m_vScrollBar ||
                 self->m_vPolicy == XScrollBarPolicy_AlwaysOff;
    hbarHidden = !self->m_hScrollBar ||
                 self->m_hPolicy == XScrollBarPolicy_AlwaysOff;
    if (!vbarHidden) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_vScrollBar);
        sbw = s.width > 0 ? s.width : 0;
    }
    if (!hbarHidden) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_hScrollBar);
        sbh = s.height > 0 ? s.height : 0;
    }
    XSize_init(&out, frame + sbw + self->m_contentWidth,
               frame + sbh + self->m_contentHeight);
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow) {
        ((XAbstractScrollArea*)self)->m_sizeHintCache = out;
        ((XAbstractScrollArea*)self)->m_sizeHintCached = true;
    }
    return out;
}

XAbstractScrollAreaSizeAdjustPolicy XAbstractScrollArea_sizeAdjustPolicy(
    const XAbstractScrollArea* self)
{
    return self ? (XAbstractScrollAreaSizeAdjustPolicy)self->m_sizeAdjustPolicy
                : XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored;
}

void XAbstractScrollArea_setSizeAdjustPolicy(
    XAbstractScrollArea* self, XAbstractScrollAreaSizeAdjustPolicy policy)
{
    if (!self) return;
    if (self->m_sizeAdjustPolicy == (int)policy) return;
    self->m_sizeAdjustPolicy = (uint8_t)policy;
    /* 对标 Qt：清缓存并请求重新布局。 */
    self->m_sizeHintCached = false;
    XSize_init(&self->m_sizeHintCache, 0, 0);
    XWidget_updateGeometry((XWidget*)self);
}

XSize XAbstractScrollArea_minimumSizeHint(const XAbstractScrollArea* self)
{
    XSize out;
    int vsbExt = 15; /* XScrollBar 默认尺寸提示 15x15。 */
    int hsbExt = 15;
    int extra = 0;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    if (self->m_vScrollBar) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_vScrollBar);
        if (s.width > 0) vsbExt = s.width;
    }
    if (self->m_hScrollBar) {
        XSize s = XWidget_sizeHint((XWidget*)self->m_hScrollBar);
        if (s.height > 0) hsbExt = s.height;
    }
    extra = 2 * XFrame_frameWidth((const XFrame*)self);
    XSize_init(&out, vsbExt + extra, hsbExt + extra);
    return out;
}






#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#endif /* XGUI_ON */
