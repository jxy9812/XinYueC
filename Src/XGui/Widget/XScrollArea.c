/**
 * @file       XScrollArea.c
 * @brief      滚动区域控件实现（对标 Qt 6.8 QScrollArea 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XScrollArea.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XAlignment.h"
#include "XWidget_Protected.h"
#include "XAbstractScrollArea_Protected.h"
#include "XPainter.h" /* sizeHint/viewportSizeHint 的字体度量（对标 fontMetrics）。 */
#include "XLayout_Internal.h" /* XLayout_invalidateCacheOnly（只清缓存不投递，
                                   用于打断布局活锁，见下方注释）。 */

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

/** @brief 字体行高（Qt fontMetrics().height() 的 X 侧口径；至少 1）。 */
static int xsa_fontHeight(const XScrollArea* self)
{
    int h = 0;
#if XPAINTER_ON
    XFont font = XWidget_font((const XWidget*)self);
    h = XPainter_textHeight(&font);
    XClassDeinit(&font);
#else
    (void)self;
#endif /* XPAINTER_ON */
    return h > 0 ? h : 1;
}

/** @brief 内容控件的「首选/现尺寸」：resizable 取 sizeHint，否则取现尺寸
 *         （对标 QScrollArea::sizeHint / viewportSizeHint 的
 *         `d->resizable ? widget->sizeHint() : widget->size()`）。
 *  @details X 的裸 XWidget 缺省 sizeHint 为 (-1,-1)（Qt 同样 if invalid），
 *           此处对无效提示回落现尺寸，避免把 -1 直接加进合计尺寸。 */
static XSize xsa_widgetMeasure(const XScrollArea* self)
{
    XSize out;
    if (self->m_widget && self->m_resizable) {
        out = XWidget_sizeHint(self->m_widget);
        if (XSize_isValid(&out)) return out;
    }
    if (self->m_widget) return XWidget_size(self->m_widget);
    XSize_init(&out, 0, 0);
    return out;
}

/** @brief 内容矩形原点（对标 QScrollAreaPrivate::updateWidgetPosition，
 *         qscrollarea.cpp:113-121）。
 *  @details Qt 口径：某轴上内容尺寸 ≤ 视口时按 alignment 对齐；> 视口时
 *           取「已滚动」偏移 −value（与 scrollContentsBy 的 -(-value) 同
 *           一套值域）。此前此处两处一律把原点钉成 0/对齐位，并把
 *           「> 视口」的情形也钉 0——滚到中途的内容在其后任意一次重排版
 *           （视口 resize；2026-10-30 起内容 resize 同样触发）里被拽回
 *           开头。content 尺寸变化时保护 scroll 位置，才能安全地把内容
 *           resize 也纳入重排版入口。 */
static void xsa_contentOrigin(const XScrollArea* self, int vw, int vh,
                              int cw, int ch, int* outX, int* outY)
{
    XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)self);
    XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    int hVal = hsb ? XScrollBar_value(hsb) : 0;
    int vVal = vsb ? XScrollBar_value(vsb) : 0;
    int x;
    int y;
    if (self->m_alignment & (int)XAlignment_Right)
        x = cw > vw ? -hVal : vw - cw;
    else if (self->m_alignment & (int)XAlignment_HCenter)
        x = cw > vw ? -hVal : (vw - cw) / 2;
    else
        x = cw > vw ? -hVal : 0;
    if (self->m_alignment & (int)XAlignment_Bottom)
        y = ch > vh ? -vVal : vh - ch;
    else if (self->m_alignment & (int)XAlignment_VCenter)
        y = ch > vh ? -vVal : (vh - ch) / 2;
    else
        y = ch > vh ? -vVal : 0;
    *outX = x;
    *outY = y;
}

/** @brief 依据 resizable/alignment 调整内容控件几何。
 *  @note   resizable 分支对标 Qt QScrollArea widgetResizable 语义
 *          （qscrollarea.cpp updateScrollBars：内容尺寸在视口与内容
 *          合计最小/合计首选间取大，经内容自身 min/max 约束钳口，滚动
 *          范围随内容）：此前把内容矩形直接钉成视口尺寸后提前返回，
 *          无 minimumSizeHint/sizeHint 下界——内容高于视口即被裁剪且
 *          不产生竖滚，迫使应用页各自子类化自管内容几何。两遍收敛：
 *          第一遍 setGeometry 触发内容布局 activate 与内容 resizeEvent
 *          （页内档位重排/换行）可能改合计提示，第二遍读实测收敛
 *          （定长两遍、无递归，同应用页此前自管壳的收敛定式）。
 *          重入安全：本函数几何写回会同步触发内容 resizeEvent，页内
 *          钩子若再经公共入口请求重排版，以 m_inGeometryUpdate 挡住
 *          （外层两遍收敛负责终态），防页钩子与几何写回互递归。 */
static void xsa_updateWidgetGeometry(XScrollArea* self)
{
    XWidget* viewport;
    XWidget* content;
    int vw;
    int vh;
    int cw;
    int ch;
    int x = 0;
    int y = 0;
    if (!self || !self->m_widget) return;
    if (self->m_inGeometryUpdate) return;
    viewport = XAbstractScrollArea_viewport(self);
    if (!viewport) return;
    vw = XWidget_width(viewport);
    vh = XWidget_height(viewport);
    content = self->m_widget;
    self->m_inGeometryUpdate = 1;
    if (self->m_resizable) {
        int pass;
        for (pass = 0; pass < 2; ++pass) {
            XSize hint;
            XSize minHint;
            int w;
            int h;
            XRect r;
/* 布局合计提示带缓存（XLayoutItem_sizeHint_base 读
               m_cachedHint）：失效后重读，同页壳自管时代的口径。
               [自激闭环根修 2026-11-01] 用 invalidateCacheOnly 而非
               invalidate：清缓存的语义同（随后 sizeHint 重算），
               但**不投递 LayoutRequest**——本函数自己就是重排版的发起方，
               不需要再被通知一次。用 invalidate 时该事件回到滚动区
               ⇒ 再次几何更新 ⇒ 再次失效 ⇒ 再次投递，自我维持成活锁：
               板端实测 act=3535 次/秒、inval=49472 次/秒、CPU 恒 99%
               （有滚动区的 page 18/19）；无滚动区的 page 0 仅 act=6、
               CPU 4%。详见 XLayout_Internal.h 的 XLayout_invalidateCacheOnly。 */
            if (XWidget_layout(content))
                XLayout_invalidateCacheOnly(XWidget_layout(content));
            hint = XWidget_sizeHint(content);
            minHint = XWidget_minimumSizeHint(content);
            w = vw > 0 ? vw : 0;
            h = vh > 0 ? vh : 0;
            if (minHint.width > w) w = minHint.width;
            /* [Qt 对齐 2026-11-02] 首选宽**不**阻止收窄：Qt 的
               widgetResizable 语义是「内容随视口宽收缩，只受最小宽
               约束」（qscrollarea.cpp updateScrollBars /
               QScrollArea::resizeEvent → widget->resize(viewport)，
               下方 boundedTo(max) 收口）。此前把 sizeHint 也当硬下限
               → 内容恒保持首选宽（如窄窗 400 时网络页内容仍 564）
               → 视口裁切、控件右侧被切（无头截图实测）。 */
            (void)hint;
            if (minHint.height > h) h = minHint.height;
            if (hint.height > h) h = hint.height;
            if (w < 0) w = 0;
            if (h < 0) h = 0;
            /* [Qt 对齐复核 2026-11-03] Qt 的 updateScrollBars 末尾
             * widget->resize(p.expandedTo(min).boundedTo(max))，此处
             * 不显式 boundedTo(max)：XWidget_setGeometryRect →
             * XWidget_setGeometry 内 clampSize(w, min, max) 与 Qt
             * resize 路径同为「min 下限 + maximumSize 上限」钳口
             * （hi<lo 时 max 优先，两侧一致），数学等价——内容最大
             * 尺寸约束由 setGeometry 收口，勿在此重复钳（会绕过
             * setGeometry 的 WA_Resized 历史状态语义）。 */
            XRect_init(&r, 0, 0, w, h);
            /* 经 XWidget_setGeometry 的 min/max 钳口：页面给内容定尺
             * （setFixed*）时以页面约束为准（对标 Qt
             * expandedTo(min).boundedTo(max)）。 */
            XWidget_setGeometryRect(content, &r);
        }
        /* 滚动范围随内容（回读钳后实际尺寸，页面定尺约束优先）。 */
        cw = XWidget_width(content);
        ch = XWidget_height(content);
        /* 位置尾部校正（对标 Qt：updateScrollBars 末尾
         * updateWidgetPosition 才定位，尺寸与位置一次到位）：内容大于
         * 视口时按已滚动偏移落位，不归零。仅位置变化触发 Move 事件，
         * 不触发 Resize，无回到重排版入口的回路。 */
        xsa_contentOrigin(self, vw, vh, cw, ch, &x, &y);
        if (x != XWidget_x(content) || y != XWidget_y(content)) {
            XRect tail;
            XRect_init(&tail, x, y, cw, ch);
            XWidget_setGeometryRect(content, &tail);
        }
        XAbstractScrollArea_setContentSize(
            (XAbstractScrollArea*)self,
            cw > vw ? cw : vw, ch > vh ? ch : vh);
        self->m_inGeometryUpdate = 0;
        return;
    }
    cw = XWidget_width(content);
    ch = XWidget_height(content);
    xsa_contentOrigin(self, vw, vh, cw, ch, &x, &y);
    {
        XRect r;
        XRect_init(&r, x, y, cw, ch);
        XWidget_setGeometryRect(content, &r);
    }
    /* 内容尺寸驱动滚动范围（负值=无水平滚动需求）。 */
    XAbstractScrollArea_setContentSize(
        (XAbstractScrollArea*)self,
        cw > vw ? cw : vw, ch > vh ? ch : vh);
    self->m_inGeometryUpdate = 0;
}

/* ==================== 虚表与生命周期 ==================== */

/** @brief scrollContentsBy：按滚动条值平移内容控件（第一版简化：
 *         直接按绝对值定位；平移后刷新视口暴露区域）。 */
static void VXScrollArea_scrollContentsBy(XAbstractScrollArea* self,
                                           int dx, int dy)
{
    XScrollArea* area = (XScrollArea*)self;
    XScrollBar* vsb;
    XScrollBar* hsb;
    XWidget* viewport;
    int vx = 0;
    int vy = 0;
    (void)dx;
    (void)dy;
    if (!area || !area->m_widget) return;
    vsb = XAbstractScrollArea_verticalScrollBar(self);
    hsb = XAbstractScrollArea_horizontalScrollBar(self);
    if (vsb) vy = -XScrollBar_value(vsb);
    if (hsb) vx = -XScrollBar_value(hsb);
    {
        XRect r;
        int cw = XWidget_width(area->m_widget);
        int ch = XWidget_height(area->m_widget);
        XRect_init(&r, vx, vy, cw, ch);
        XWidget_setGeometryRect(area->m_widget, &r);
    }
    /* 暴露区域重绘：内容平移后刷新视口。 */
    viewport = XAbstractScrollArea_viewport(self);
    if (viewport) XWidget_update(viewport);
}

/** @brief 内容件被销毁时的收口槽（QPointer 语义的等价物）。
 *  @details Qt 的 QScrollAreaPrivate 用 `QPointer<QWidget> widget` 持有内容，
 *           内容被外部提前删除时自动置空；本框架无弱引用，改为连接内容的
 *           `destroyed()`（对标 qobject.cpp ~QObject 早段发射），在内容析构
 *           的第一时间把 m_widget 置空——否则滚动区析构/重排版会触碰已释放
 *           的内容指针（UAF）。页容器场景（XToolBox 每页一只 XScrollArea）
 *           会由业务先删页再删工具箱，正需要这条收口。 */
static void xsa_contentDestroyedSlot(XObject* receiver, XVarList* args)
{
    XScrollArea* self = (XScrollArea*)receiver;
    (void)args;
    if (!self) return;
    self->m_widget = NULL;
    self->m_widgetSizeValid = false;
}

/** @brief 内容件的 Resize 事件过滤器（对标 QScrollArea::eventFilter，
 *         qscrollarea.cpp:275-291：`if (o == d->widget && e->type() ==
 *         QEvent::Resize) d->updateScrollBars();`）。
 *  @details Qt 的滚动区靠**内容件的 Resize 事件**维持续列表范围与显隐：
 *           内容一改尺寸（页内增删行、换行重排、手风琴换分类后重算内容
 *           高），过滤器重算范围。此前本框架无此环——内容件几何由业务
 *           代码直接 resize，滚动区收不到任何通知，m_contentWidth/Height
 *           停留在上一次（构造/show/滚动区自身 resize）算出的旧值：
 *           AsNeeded 的条因而「该出现的出现不了、该消失的消不了」，只有
 *           改窗大小触发滚动区 resizeEvent 才顺带刷新（2026-10-30 实测：
 *           左侧导航矮分类→高分类后内容被裁却不出现竖滚条，高→矮后残
 *           留的竖滚条也不消失）。
 *  @return  恒 false（同 Qt：过滤器只旁路观察，从不吞掉内容件自身的
 *           Resize，业务方的 resizeEvent 处理器照常收到）。 */
static bool VXScrollArea_eventFilter(XObject* self, XObject* watched,
                                     XEvent* event)
{
    XScrollArea* area = (XScrollArea*)self;
    if (!area || !event) return false;
    if (!area->m_widget || watched != (XObject*)area->m_widget)
        return false;
    if (XEvent_type(event) == XEVENT_TYPE_RESIZE)
        xsa_updateWidgetGeometry(area);
    return false;
}

/** @brief 事件入口：LayoutRequest/StyleChange → 重排版内容（对标
 *         QScrollArea::event，qscrollarea.cpp:274-278：`if (e->type() ==
 *         QEvent::StyleChange || e->type() == QEvent::LayoutRequest)
 *         d->updateScrollBars();` 后仍交基类）。
 *  @details 内容提示变化（标签文本增长/字体切换等）经「无布局父链」的
 *           LayoutRequest 到达本槽（XWidget_updateGeometry else 分支
 *           投递 + VXWidget_event 无布局分支上抛，Qt 侧为视口过滤器
 *           转发同位），随后仍交基类走默认处理（Qt 同样不吞）。与内容
 *           件 Resize 过滤器（上一函数，Qt 同位）互补：Resize 环覆盖
 *           「内容几何被直接 resize」，本环覆盖「提示变化而内容几何
 *           未动」——二者合并即 Qt 的两条通知路径全集。
 *  @return  交基类 VXWidget_event 的结果（LayoutRequest 基类无布局即
 *           空处理返回 true；StyleChange 走 ChangeEvent 段）。 */
static bool VXScrollArea_event(XWidget* self, XEvent* event)
{
    XScrollArea* area = (XScrollArea*)self;
    if (area && event) {
        XEventType type = XEvent_type(event);
        if (type == XEVENT_TYPE_LAYOUT_REQUEST ||
            type == XEVENT_TYPE_STYLE_CHANGE)
            xsa_updateWidgetGeometry(area);
    }
    return XClass_Parent(XAbstractScrollArea, EXObject_Event,
                         bool (*)(XWidget*, XEvent*))((XWidget*)self, event);
}

static void VXScrollArea_deinit(XScrollArea* self)
{
    if (!self) return;
    /* 内容件可能先于/晚于本控件销毁：显式摘过滤器，避免内容件再收到
     * Resize 时向已析构的滚动区回调（Qt 由 QPointer<widget> 自动置空，
     * 本框架无弱引用，需在析构前显式摘除）。 */
    if (self->m_widget)
        XObject_removeEventFilter((XObject*)self->m_widget,
                                  (XObject*)self);
    XClass_Deinit_Parent(XAbstractScrollArea,
                         (XAbstractScrollArea*)self);
}

/** @brief resizeEvent：先由基类完成视口/滚动条重排，再重排版内容。
 *  对标 QScrollArea::resizeEvent（qscrollarea.cpp:313-316 →
 *  updateScrollBars → updateWidgetPosition）：容器尺寸变化（如页签
 *  容器拉伸页面）后内容对齐位与滚动范围必须重算——此前缺失该重载，
 *  对齐/内容尺寸停留在旧视口尺寸上（页签4 内容呈现异常的组成部分）。 */
static void VXScrollArea_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    xsa_updateWidgetGeometry((XScrollArea*)self);
}

/** @brief 焦点遍历：交给基类遍历后把新焦点控件滚进视口
 *         （对标 QScrollArea::focusNextPrevChild，qscrollarea.cpp:510-518：
 *         `if (QWidget::focusNextPrevChild(next)) { if (QWidget *fw =
 *         focusWidget()) ensureWidgetVisible(fw); return true; }`）。
 *  @note  基类槽经 XClass_Parent(XWidget, EXWidget_FocusNextPrevChild)
 *         直取，不经对象虚表（否则回到本函数自身递归）。 */
static bool VXScrollArea_focusNextPrevChild(XWidget* self, bool next)
{
    bool handled;
    if (!self) return false;
    handled = XClass_Parent(XWidget, EXWidget_FocusNextPrevChild,
                            bool (*)(XWidget*, bool))(self, next);
    if (handled) {
        XWidget* fw = XWidget_focusWidget(self);
        /* ensureWidgetVisible 的缺省边距 50（qscrollarea.h:41）。 */
        if (fw) XScrollArea_ensureWidgetVisible((XScrollArea*)self, fw, 50, 50);
    }
    return handled;
}

/** @brief showEvent：以当前实际尺寸重排视口/滚动条与内容几何。
 *  对标 Qt「show 时补排版」语义（WA_PendingResizeEvent 在 show 统一
 *  刷新 + QScrollArea::eventFilter 对内容 Resize 的 updateScrollBars
 *  联动，qscrollarea.cpp:328）：构造期容器尚未显示，页签容器此后的
 *  拉伸虽同步派发 resize，但视口/内容的中间态几何（负宽夹 0 等）不
 *  会自发重算——显示瞬间按最终几何再断言一次，消除隐藏期遗留的
 *  陈旧视口尺寸（页签4 六行文本被裁成 ~1px 残条的收口）。 */
static void VXScrollArea_showEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  XWidgetEventSlot)(self, event);
    VXScrollArea_resizeEvent(self, event);
}

/** @brief 滚轮 → 滚动条步进（复扫 r2 #48 根修）。
 *  @note  对标 QAbstractScrollArea::wheelEvent 的主导轴选条
 *         （qabstractscrollarea.cpp:1170-1176：|x|>|y| 事件走水平条、
 *         否则走垂直条）与 QScrollBarPrivate::scrollByDelta 的步进口径
 *         （qabstractslider.cpp:668 起：offset=delta/120，缺省
 *         wheelScrollLines=3 行/格，横向 delta 取反）；单步 20px 取
 *         QScrollArea 构造口径（qscrollarea.cpp:108/121 vbar/hbar
 *         setSingleStep(20)）。方向与 XTreeWidget 滚轮修复同口径
 *         （XTreeWidget.c:2197-2199）：value = 当前值 − steps*3*20，
 *         正角度（滚向内容开头）减小值、下滚值增大。此前依赖基类
 *         VX_asa_wheelEvent 的 stepBy(steps*3)：其符号与 scrollByDelta
 *         相反（下滚 steps=−1 → 值减 → 顶部钳位 0），且单步仅缺省
 *         1px（XAbstractSlider.c:397）——「下滚×3 视口 0 像素差、把手
 *         拖拽可滚」即此二因叠加（复扫 r2 lane1 #48 实证）。直接
 *         setValue 由内部钳位兜底（XAbstractSlider.c:599-602），
 *         valueChanged → scrollContentsBy → 重绘链既有。 */
static void VXScrollArea_wheelEvent(XWidget* self, XEvent* event)
{
    XScrollArea* area = (XScrollArea*)self;
    XAbstractScrollArea* base;
    XScrollBar* bar;
    XPoint delta;
    int steps;
    int value;
    if (!area || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    /* 滚轮接管：停触摸跟手/惯性（基类滚轮槽同口径；本覆写不链基类，
     * 须经保护入口自行收口）。 */
    XAbstractScrollArea_stopTouchScroll((XAbstractScrollArea*)area);
#if XWINDOWEVENT_ON
    delta = XWheelEvent_angleDelta((XWheelEvent*)event);
#else
    delta.x = 0;
    delta.y = 0;
#endif
    {
        /* 主导轴分派（同 XTreeWidget 口径：dy 优先，|x|>|y| 才走横向
         * 并按 Qt scrollByDelta 对横向 delta 取反）。 */
        int ay = delta.y >= 0 ? delta.y : -delta.y;
        int ax = delta.x >= 0 ? delta.x : -delta.x;
        base = (XAbstractScrollArea*)area;
        if (ax > ay) {
            steps = -(delta.x / 120); /* 横向取反（Qt scrollByDelta）。 */
            bar = XAbstractScrollArea_horizontalScrollBar(base);
        } else {
            steps = delta.y / 120;
            bar = XAbstractScrollArea_verticalScrollBar(base);
        }
    }
    if (steps == 0) {
        XEvent_accept(event); /* 不足一格：消费不滚动（同基类口径）。 */
        return;
    }
    if (!bar) {
        XEvent_accept(event);
        return;
    }
    /* 单步 20px × 3 行/格（qscrollarea.cpp:108/121 +
     * scrollByDelta wheelScrollLines）；setValue 内部钳位到
     * [minimum,maximum]，越界即自然停在端点。 */
    value = XScrollBar_value(bar) - steps * 3 * 20;
    XScrollBar_setValue(bar, value);
    XEvent_accept(event);
}

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint 虚函数）。 */
static XSize VXScrollArea_sizeHintSlot(const XWidget* self);
static XSize VXScrollArea_minimumSizeHintSlot(const XWidget* self);

static XSize VXScrollArea_sizeHintSlot(const XWidget* self)
{
    return XScrollArea_sizeHint((const XScrollArea*)self);
}

/** @brief viewportSizeHint 虚槽桥接（对标 QScrollArea 重载的同名虚函数）。 */
static XSize VXScrollArea_viewportSizeHintSlot(
    const XAbstractScrollArea* self)
{
    return XScrollArea_viewportSizeHint((const XScrollArea*)self);
}

static XSize VXScrollArea_minimumSizeHintSlot(const XWidget* self)
{
    /* Qt 的 QScrollArea 不 override minimumSizeHint，沿用
     * QAbstractScrollArea::minimumSizeHint（条长 + 条厚 + 帧宽）；
     * 现算而非读缓存，条换装后仍成立。 */
    return XAbstractScrollArea_minimumSizeHint(
        (const XAbstractScrollArea*)self);
}

XVtable* XScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXScrollArea_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXScrollArea_minimumSizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportSizeHint,
                             VXScrollArea_viewportSizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(
        EXAbstractScrollArea_ScrollContentsBy,
        VXScrollArea_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VXScrollArea_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent,
                             VXScrollArea_showEvent);
    /* 对标 QScrollArea::focusNextPrevChild（qscrollarea.cpp:510-518）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusNextPrevChild,
                             VXScrollArea_focusNextPrevChild);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent,
                             VXScrollArea_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VXScrollArea_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXScrollArea_event);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXScrollArea_deinit);
    return XVTABLE_DEFAULT;
}

void XScrollArea_init(XScrollArea* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XScrollArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_resizable = false;
    self->m_alignment = 0;
    /* 对标 QScrollArea 构造（qscrollarea.cpp:108-109/121-122）：两条
     * singleStep 缺省 20（QScrollBar 缺省 1）——滚轮一格的基准步长，
     * 与 VXScrollArea_wheelEvent 的 3 行/格口径配套。 */
    if (XAbstractScrollArea_verticalScrollBar(
            (const XAbstractScrollArea*)self))
        XScrollBar_setSingleStep(
            XAbstractScrollArea_verticalScrollBar(
                (const XAbstractScrollArea*)self), 20);
    if (XAbstractScrollArea_horizontalScrollBar(
            (const XAbstractScrollArea*)self))
        XScrollBar_setSingleStep(
            XAbstractScrollArea_horizontalScrollBar(
                (const XAbstractScrollArea*)self), 20);
}

XScrollArea* XScrollArea_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XScrollArea* self = (XScrollArea*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollArea_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 内容管理 ==================== */

void XScrollArea_setWidget(XScrollArea* self, XWidget* widget)
{
    XWidget* viewport;
    XWidget* old;
    if (!self || !widget) return;
    viewport = XAbstractScrollArea_viewport(self);
    if (!viewport) return;
    if (self->m_widget == widget) return;
    /* 对标 QScrollArea::setWidget（qscrollarea.cpp:231-252）首行
       `delete d->widget`：内容控件所有权移交滚动区，装新件前销毁
       旧件（XClassDelete 对栈对象仅反初始化，分配器归属安全）。 */
    old = self->m_widget;
    if (old) {
        self->m_widget = NULL;
        /* 摘过滤器先于销毁：Qt 用 QPointer 自动失效，本框架需显式拆
         * 除，否则销毁期产生的几何变化会回调到正在析构的对象。 */
        XObject_removeEventFilter((XObject*)old, (XObject*)self);
        XClassDelete((XClass*)old);
    }
    /* 对标 qscrollarea.cpp:239-240：换件即复位滚动位置。 */
    {
        XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(
            (const XAbstractScrollArea*)self);
        XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
            (const XAbstractScrollArea*)self);
        if (hsb) XScrollBar_setValue(hsb, 0);
        if (vsb) XScrollBar_setValue(vsb, 0);
    }
    XWidget_setParent(widget, viewport, 0);
    /* 对标 qscrollarea.cpp:241-242：新件尚无尺寸（未置 WA_Resized）时
     * 按自身 sizeHint 摆一次尺寸。X 侧附加两条护栏：提示无效
     * （裸 XWidget 缺省 (-1,-1)）或某轴为 0 时不动——Qt 的 resize(0,0)
     * 在「布局提示暂为 0」的空容器上会把内容压成 0，X 的内容控件多由
     * 业务在 setWidget 之后才填布局，压 0 会白屏一帧。 */
    if (!XWidget_testAttribute(widget, XWidgetAttribute_Resized)) {
        XSize hint = XWidget_sizeHint(widget);
        if (XSize_isValid(&hint) && hint.width > 0 && hint.height > 0)
            XWidget_resize(widget, hint.width, hint.height);
    }
    self->m_widget = widget;
    /* 对标 qscrollarea.cpp:246 `d->widgetSize = QSize();`：换内容即失效首选
     * 尺寸缓存（sizeHint 下次查询按新内容重算一次，此后不再跟随内容变化）。 */
    self->m_widgetSizeValid = false;
    /* 对标 qscrollarea.cpp:250 `widget->installEventFilter(this)`：内容件
     * 此后的每次尺寸变化都经 VXScrollArea_eventFilter 回流到本控件，
     * 滚动范围/显隐随内容自动跟随（业务方不再需要手工触发重排版）。 */
    XObject_installEventFilter((XObject*)widget, (XObject*)self);
    /* 内容被外部提前销毁时把 m_widget 置空（QPointer 等价收口；见
     * xsa_contentDestroyedSlot）。信号标识必须走 XSignal（以 NULL 调信号函
     * 数取地址），**不能**在内容上直接调 XObject_destroyed_signal——那会当场
     * 发一次 destroyed（XEmitSignal 宏末的 return 前先发射）。 */
    XObject_connect_1((XObject*)widget,
                      XSignal(XObject_destroyed_signal),
                      (XObject*)self, xsa_contentDestroyedSlot,
                      XConnectionType_Direct);
    xsa_updateWidgetGeometry(self);
    /* 对标 qscrollarea.cpp:252 末尾 `d->widget->show()`：显隐收口放在
     * 几何落定之后（此前在换装前 show，重排版期间会带一次多余显隐）。 */
    XWidget_setVisible(widget, true);
}

XWidget* XScrollArea_takeWidget(XScrollArea* self)
{
    XWidget* widget;
    if (!self || !self->m_widget) return NULL;
    widget = self->m_widget;
    /* 对标 Qt：交还所有权即不再监视该件（setParent(nullptr) 后其后续
     * resize 与本控件无关）。 */
    XObject_removeEventFilter((XObject*)widget, (XObject*)self);
    XWidget_setParent(widget, NULL, 0);
    /* 对标 QWidget::setParent 的父变更语义（qwidget.cpp setParent_sys：
     * 父变更即隐藏，调用方复用前需 show）；Qt takeWidget 只做
     * setParent(nullptr)，隐藏是 setParent 的副作用。 */
    XWidget_setVisible(widget, false);
    self->m_widget = NULL;
    return widget;
}

XWidget* XScrollArea_widget(const XScrollArea* self)
{
    return self ? self->m_widget : NULL;
}

bool XScrollArea_widgetResizable(const XScrollArea* self)
{
    return self ? self->m_resizable : false;
}

void XScrollArea_setWidgetResizable(XScrollArea* self, bool resizable)
{
    if (!self || self->m_resizable == resizable) return;
    self->m_resizable = resizable;
    /* 对标 qscrollarea.cpp:440-447：updateGeometry() + updateScrollBars()。 */
    XWidget_updateGeometry((XWidget*)self);
    xsa_updateWidgetGeometry(self);
}

/** @brief 按当前视口/内容提示重排版内容几何与滚动范围（公共收口）。
 *  @details 对标 QScrollAreaPrivate::updateWidgetPosition（Qt 为私有，
 *           本框架 C API 无 friend 语义故公开）：内容提示在无几何事件
 *           的前提下变化时（页内展开/收起、档位重排改子件约束），由
 *           调用方触发一次重排版，替代应用页各自复制的「fit 内容宽高
 *           + setContentSize」自管壳。内部即 resizable/alignment 排版
 *           的唯一实现，幂等（几何未变时 setContentSize 值不变短路）。 */
void XScrollArea_updateWidgetGeometry(XScrollArea* self)
{
    if (!self) return;
    xsa_updateWidgetGeometry(self);
}

int XScrollArea_alignment(const XScrollArea* self)
{
    return self ? self->m_alignment : 0;
}

void XScrollArea_setAlignment(XScrollArea* self, int alignment)
{
    if (!self) return;
    self->m_alignment = alignment;
    xsa_updateWidgetGeometry(self);
}

/** @brief 最小滚动使目标点连同边距可见（对标 QScrollArea::ensureVisible
 *         的可见性判定：已可见不动；越上/左缘滚到 点-边距，越下/右缘
 *         滚到 点+边距-视口。此前恒滚到原点+边距，已可见也跳动）。 */
void XScrollArea_ensureVisible(XScrollArea* self, int x, int y,
                               int xmargin, int ymargin)
{
    XScrollBar* hsb;
    XScrollBar* vsb;
    XWidget* viewport;
    int pw;
    int ph;
    int relX;
    int relY;
    if (!self) return;
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)self);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)self);
    pw = viewport ? XWidget_width(viewport) : 0;
    ph = viewport ? XWidget_height(viewport) : 0;
    relX = x - (hsb ? XScrollBar_value(hsb) : 0);
    relY = y - (vsb ? XScrollBar_value(vsb) : 0);
    if (hsb) {
        if (relX < xmargin)
            XScrollBar_setValue(hsb, x - xmargin);
        else if (relX + xmargin > pw)
            XScrollBar_setValue(hsb, x + xmargin - pw);
    }
    if (vsb) {
        if (relY < ymargin)
            XScrollBar_setValue(vsb, y - ymargin);
        else if (relY + ymargin > ph)
            XScrollBar_setValue(vsb, y + ymargin - ph);
    }
}

/** @brief 最小滚动使子控件矩形连同边距可见（对标
 *         QScrollArea::ensureWidgetVisible，qscrollarea.cpp:360-392）。
 *  @details Qt 口径逐条照搬：
 *           ① 目标必须是内容控件的后代（isAncestorOf 不成立即返回；
 *              内容控件本体不是自己的后代 ⇒ 传本体直接返回）；
 *           ② 目标矩形已完整落在可见区内 ⇒ 不动（边距不参与该判定）；
 *           ③ 加边距后若矩形比视口还宽/高 ⇒ 居中；
 *              否则越右/下缘滚到 `right - 视口 + 1`、越左/上缘滚到 left/top。
 *          此前版本缺 ②（已可见也跳动）与 ③ 的居中分支，且右下缘少了
 *          Qt 的 +1 对齐偏移。 */
void XScrollArea_ensureWidgetVisible(XScrollArea* self,
                                     XWidget* childWidget,
                                     int xmargin, int ymargin)
{
    XScrollBar* hsb;
    XScrollBar* vsb;
    XWidget* viewport;
    XWidget* content;
    XRect geom;
    int pw;
    int ph;
    int left;
    int top;
    int right;
    int bottom;
    int visLeft;
    int visTop;
    int visRight;
    int visBottom;
    if (!self || !childWidget) return;
    content = self->m_widget;
    /* 对标 Qt：目标即内容控件本体时无可确保（本体不是自己的后代）。 */
    if (!content || childWidget == content) return;
    geom = XWidget_geometry(childWidget);
    /* 子控件几何逐级映射到内容控件坐标（Qt 仅支持直接子控件并告警；
     * 此处向上累加偏移，父链不达内容控件时整体忽略）。 */
    {
        XWidget* p = XWidget_parentWidget(childWidget);
        while (p && p != content) {
            XRect pg = XWidget_geometry(p);
            geom.x += pg.x;
            geom.y += pg.y;
            p = XWidget_parentWidget(p);
        }
        if (p != content) return;
    }
    viewport = XAbstractScrollArea_viewport(
        (const XAbstractScrollArea*)self);
    pw = viewport ? XWidget_width(viewport) : 0;
    ph = viewport ? XWidget_height(viewport) : 0;
    hsb = XAbstractScrollArea_horizontalScrollBar(
        (const XAbstractScrollArea*)self);
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)self);
    visLeft = hsb ? XScrollBar_value(hsb) : 0;
    visTop = vsb ? XScrollBar_value(vsb) : 0;
    visRight = visLeft + pw - 1;
    visBottom = visTop + ph - 1;
    left = geom.x;
    top = geom.y;
    right = geom.x + geom.width - 1;
    bottom = geom.y + geom.height - 1;
    /* ② 已完整可见：不动（Qt `visibleRect.contains(focusRect)` 早退）。 */
    if (left >= visLeft && top >= visTop &&
        right <= visRight && bottom <= visBottom)
        return;
    /* ③ 加边距后再判：整体比可见区还大 ⇒ 居中；否则对齐越界边。 */
    left -= xmargin;
    top -= ymargin;
    right += xmargin;
    bottom += ymargin;
    if (hsb) {
        if (right - left + 1 > pw)
            XScrollBar_setValue(hsb, (left + right) / 2 - pw / 2);
        else if (right > visRight)
            XScrollBar_setValue(hsb, right - pw + 1);
        else if (left < visLeft)
            XScrollBar_setValue(hsb, left);
    }
    if (vsb) {
        if (bottom - top + 1 > ph)
            XScrollBar_setValue(vsb, (top + bottom) / 2 - ph / 2);
        else if (bottom > visBottom)
            XScrollBar_setValue(vsb, bottom - ph + 1);
        else if (top < visTop)
            XScrollBar_setValue(vsb, top);
    }
}

XSize XScrollArea_sizeHint(const XScrollArea* self)
{
    XSize out;
    XSize content;
    int h;
    int frame;
    int limitW;
    int limitH;
    if (!self) return XAbstractScrollArea_sizeHint(
                       (const XAbstractScrollArea*)self);
    /* 对标 QScrollArea::sizeHint（qscrollarea.cpp:466-487）：
     * 帧宽×2 + （有内容 ? 内容尺寸 : 12h × 8h）
     *        + （条策略 AlwaysOn 时的条厚）
     * 最后 boundedTo(36h, 24h)。
     * 内容尺寸走 QScrollAreaPrivate::widgetSize 缓存（qscrollarea.cpp:
     * 470-472 `if (!d->widgetSize.isValid()) d->widgetSize = resizable ?
     * widget->sizeHint() : widget->size();`）：**只算一次**，失效点唯一——
     * setWidget（:246 `d->widgetSize = QSize();`）。这正是 Qt 的口径：滚动
     * 区的首选尺寸不随内容后续变化而漂移（内容变大是"该滚动"而不是
     * "该长大"），takeWidget/setWidgetResizable 均不清缓存（widget 为空时
     * 本函数走 else 分支，缓存不被读）。 */
    frame = 2 * XFrame_frameWidth((const XFrame*)self);
    h = xsa_fontHeight(self);
    if (self->m_widget) {
        XScrollArea* mutableSelf = (XScrollArea*)self;
        if (!self->m_widgetSizeValid) {
            mutableSelf->m_widgetSize = xsa_widgetMeasure(self);
            mutableSelf->m_widgetSizeValid = true;
        }
        content = self->m_widgetSize;
        XSize_init(&out, frame + (content.width > 0 ? content.width : 0),
                   frame + (content.height > 0 ? content.height : 0));
    } else {
        XSize_init(&out, frame + 12 * h, frame + 8 * h);
    }
    {
        const XAbstractScrollArea* base = (const XAbstractScrollArea*)self;
        if (XAbstractScrollArea_verticalScrollBarPolicy(base) ==
            XScrollBarPolicy_AlwaysOn) {
            XScrollBar* bar = XAbstractScrollArea_verticalScrollBar(base);
            if (bar) {
                XSize s = XWidget_sizeHint((XWidget*)bar);
                if (s.width > 0) out.width += s.width;
            }
        }
        if (XAbstractScrollArea_horizontalScrollBarPolicy(base) ==
            XScrollBarPolicy_AlwaysOn) {
            XScrollBar* bar = XAbstractScrollArea_horizontalScrollBar(base);
            if (bar) {
                XSize s = XWidget_sizeHint((XWidget*)bar);
                if (s.height > 0) out.height += s.height;
            }
        }
    }
    limitW = 36 * h;
    limitH = 24 * h;
    if (out.width > limitW) out.width = limitW;
    if (out.height > limitH) out.height = limitH;
    return out;
}

XSize XScrollArea_viewportSizeHint(const XScrollArea* self)
{
    XSize out;
    int h;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 对标 QScrollArea::viewportSizeHint（qscrollarea.cpp:496-504）：
     * 有内容取（resizable ? 内容 sizeHint : 内容现尺寸），无内容 6h × 4h。 */
    if (self->m_widget) {
        out = xsa_widgetMeasure(self);
        if (XSize_isValid(&out)) return out;
    }
    h = xsa_fontHeight(self);
    XSize_init(&out, 6 * h, 4 * h);
    return out;
}












#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON */

#endif /* XGUI_ON */
