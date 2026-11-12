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
#include "XGestureEvent.h"   /* 手势事件载荷（减负对齐 QTouchEvent）。 */
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XAlignment.h"
#include "XPainter.h"     /* 视口建议尺寸的字体度量（对标 fontMetrics）。 */
#include "XDateTime.h"    /* 惯性/速度采样单调毫秒（约束文档时间源规则）。 */

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/* ==================== 内部工具 ==================== */

/* 停触摸滚动（定义在本文件触摸滚动节；滚轮/把手按下路径先行引用）。 */
static void xasa_touchScrollStop(XAbstractScrollArea* self);

/* ==================== 视口事件过滤器（对标 QAbstractScrollAreaFilter） ====
 * [Qt 对齐 2026-11-11] gap #171：Qt 给 viewport 装一个私有 QObject 过滤器，
 * 每个事件先问滚动区的 viewportEvent()（qabstractscrollarea.cpp:1036-1077 +
 * qabstractscrollarea_p.h 的 QAbstractScrollAreaFilter）。
 * 这里用同等结构的**独立过滤器对象**，而不是把滚动区自身装成视口过滤器：
 * 滚动区子类可能重载 EXObject_EventFilter（XScrollArea 监视内容件 resize，
 * XTextBrowser/XCalendarWidget/XDateTimeEdit/XMdiArea 各有一份），对象虚表
 * 上的 EventFilter 会被这些重载遮蔽；独立对象走自己的虚表，不会被遮蔽。
 * 本私有类不新增虚槽，虚表容量直接取父类 XObject 的槽数
 * （XVTABLE_INIT_DEFAULT_SIZE(EXObject_END_SIZE)）——用自定义枚举会把容量
 * 定成 1，XVTABLE_INHERIT 追加父槽时触发「stack 槽位溢出」。 */

/** @brief 视口事件过滤器（私有；归滚动区所有，随其级联释放）。 */
typedef struct XAbstractScrollAreaFilter
{
    XObject m_base;              /**< 基类；必须是第一个。 */
    XAbstractScrollArea* m_area; /**< 被观察的滚动区（借用；恒长于过滤器）。 */
} XAbstractScrollAreaFilter;

/** @brief 过滤器槽：只转本区视口的事件（同 Qt `o == d->viewport ? ...`）。
 *  @return 视口事件入口的返回值（true=消费，不再投递给视口）。 */
static bool VX_asaFilter_eventFilter(XObject* self, XObject* watched,
                                     XEvent* event)
{
    XAbstractScrollAreaFilter* filter = (XAbstractScrollAreaFilter*)self;
    if (!filter || !filter->m_area || !watched || !event) return false;
    if (watched != (XObject*)filter->m_area->m_viewport) return false;
    return XAbstractScrollArea_viewportEvent(filter->m_area, event);
}

XVtable* XAbstractScrollAreaFilter_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(EXObject_END_SIZE)
    /* 私有类也要有类名：Qt 的私有过滤器类名是 QAbstractScrollAreaFilter，
     * XObject_className/dumpObjectTree 依赖它（否则回落 "XObject"）。 */
    XCLASS_SET_CLASS_NAME_DEFAULT("XAbstractScrollAreaFilter");
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VX_asaFilter_eventFilter);
    return XVTABLE_DEFAULT;
}

/** @brief 创建过滤器并挂到滚动区对象树（借父链随本区析构级联释放）。
 *  @details 过滤器是**子对象**而非值成员：视口先亡时 XObject 析构会双向
 *           反登记（把视口从 m_watchedObjects 摘掉），不会留下悬挂引用。 */
static XObject* xasa_filterCreate(XAbstractScrollArea* area)
{
    XAbstractScrollAreaFilter* filter = (XAbstractScrollAreaFilter*)
        XMemory_malloc(sizeof(XAbstractScrollAreaFilter),
                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!filter) return NULL;
    XObject_init((XObject*)filter);
    XClassSetVtable(filter, XAbstractScrollAreaFilter);
    Set_Class_Memory(filter, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(filter, true);
    filter->m_area = area;
    XObject_setParent((XObject*)filter, (XObject*)area);
    return (XObject*)filter;
}

/** @brief 给视口装/换装本区的事件过滤器（幂等：重复装同对象仅前置一次）。 */
static void xasa_installViewportFilter(XAbstractScrollArea* self)
{
    if (!self || !self->m_viewport || !self->m_viewportFilter) return;
    XObject_installEventFilter((XObject*)self->m_viewport,
                               self->m_viewportFilter);
}

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
    xasa_touchScrollStop(area); /* 滚轮接管：停跟手/惯性（社会滚动让位）。 */
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

/** @brief 滚动条把手按下 → 停触摸滚动（社会滚动接管：把手拖拽与惯性
 *         不打架；连接随条替换同步挂/摘）。 */
static void xasa_sliderPressKillSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) xasa_touchScrollStop((XAbstractScrollArea*)receiver);
}

/** @brief 连接滚动条 valueChanged → 内容滚动虚槽。 */
static void xasa_connectBar(XAbstractScrollArea* self, XScrollBar* bar,
                            bool horizontal)
{
    if (!self || !bar) return;
    if (horizontal)
        XObject_connect((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_hScrollChangedSlot,
                          XConnectionType_Direct);
    else
        XObject_connect((XObject*)bar,
                          (size_t)XScrollBar_valueChanged_signal(bar, 0),
                          (XObject*)self, xasa_vScrollChangedSlot,
                          XConnectionType_Direct);
    XObject_connect((XObject*)bar,
                      (size_t)XAbstractSlider_sliderPressed_signal(
                          (XAbstractSlider*)bar),
                      (XObject*)self, xasa_sliderPressKillSlot,
                      XConnectionType_Direct);
}

/** @brief 断开滚动条连接（替换滚动条前调用）。 */
static void xasa_disconnectBar(XAbstractScrollArea* self, XScrollBar* bar,
                               bool horizontal)
{
    if (!self || !bar) return;
    XObject_disconnect((XObject*)bar,
                         (size_t)XScrollBar_valueChanged_signal(bar, 0),
                         (XObject*)self,
                         horizontal ? xasa_hScrollChangedSlot
                                    : xasa_vScrollChangedSlot);
    XObject_disconnect((XObject*)bar,
                         (size_t)XAbstractSlider_sliderPressed_signal(
                             (XAbstractSlider*)bar),
                         (XObject*)self, xasa_sliderPressKillSlot);
}

/** @brief 视口/滚动条几何排布前置声明（显隐联动重排用）。 */
static void xasa_layout(XAbstractScrollArea* self);

/* ==================== 附加滚动条控件分类（对标 Qt 容器模型） ==========
 * [Qt 对齐 2026-11-11] Qt 的 addScrollBarWidget 把控件放进两个
 * QAbstractScrollAreaScrollBarContainer（水平条容器/垂直条容器），
 * 每个容器再按 LogicalLeft/LogicalRight 分区（qabstractscrollarea.cpp:
 * 762-803）。本框架无容器类，用「条带类别」四值承载同一语义：
 *   - Left/Right → 水平条带（与水平条同一行），Left=条左侧、Right=右侧；
 *   - Top/Bottom → 垂直条带（与垂直条同一列），Top=条上方、Bottom=下方。
 * 判定顺序按 Qt：先看水平四位的存在性（Left 优先于 Right），再落到
 * 垂直条带（Right/Bottom 为「逻辑右/下」端点）。 */
#define XASA_SBW_LEFT   0x0001
#define XASA_SBW_RIGHT  0x0002
#define XASA_SBW_TOP    0x0004
#define XASA_SBW_BOTTOM 0x0008

/** @brief 对齐位 → 条带类别（四值之一）。 */
static int xasa_sbWidgetCategory(int alignment)
{
    if (alignment & (int)XAlignment_Left) return XASA_SBW_LEFT;
    if (alignment & (int)XAlignment_Right) return XASA_SBW_RIGHT;
    if (alignment & (int)XAlignment_Top) return XASA_SBW_TOP;
    return XASA_SBW_BOTTOM;
}

/** @brief 控件的条带占宽/占高：优先现尺寸，无尺寸时取 sizeHint。 */
static int xasa_sbWidgetExtent(const XWidget* widget, bool horizontal)
{
    XSize hint;
    int extent;
    if (!widget) return 0;
    extent = horizontal ? XWidget_width((XWidget*)widget)
                        : XWidget_height((XWidget*)widget);
    if (extent > 0) return extent;
    hint = XWidget_sizeHint_base((XWidget*)widget);
    extent = horizontal ? hint.width : hint.height;
    return extent > 0 ? extent : 0;
}

/** @brief 滚动条条厚（Qt 口径：一律取条自身的 sizeHint）。
 *  @details 对标 Qt 6.8 qabstractscrollarea.cpp：
 *           - maximumViewportSize（:535-547）扣 `vbar->sizeHint().width()`
 *             / `hbar->sizeHint().height()`；
 *           - layoutChildren_helper（:295+）以同一对 sizeHint 作条带厚度；
 *           - minimumSizeHint（:1405-1423）同源。
 *           此前三处各自写死 16，而 XScrollBar 的 sizeHint 是 15×15
 *           （XScrollBarAlignTest 锁定的本库基准值）⇒ 非 AlwaysOn 的
 *           sizeHint 链按 15、视口几何按 16，两者恒差 1px；AlwaysOn 下
 *           maximumViewportSize 也与实际视口不一致。
 * @param self 目标滚动区；NULL 时回退 16（无对象可取提示的安全下限）。
 * @param horizontal true=水平条（取 sizeHint.height），false=竖条
 *                   （取 sizeHint.width）。
 * @return 条厚像素；提示非正时回退 16。
 */
static int xasa_barExtent(const XAbstractScrollArea* self, bool horizontal)
{
    XWidget* bar;
    XSize hint;
    if (!self) return 16;
    bar = horizontal ? (XWidget*)self->m_hScrollBar
                     : (XWidget*)self->m_vScrollBar;
    if (!bar) return 16;
    hint = XWidget_sizeHint_base(bar);
    if (horizontal)
        return hint.height > 0 ? hint.height : 16;
    return hint.width > 0 ? hint.width : 16;
}

/** @brief 滚动条容器在排布轴上的合计长度（对标 Qt
 *         QAbstractScrollAreaScrollBarContainer 的 sizeHint）。
 *  @details Qt 把滚动条与附加控件装进两个 QWidget 容器（水平条容器
 *           LeftToRight、垂直条容器 TopToBottom，qabstractscrollarea.cpp:
 *           145-160/166-185），容器 sizeHint = 各件 sizeHint 之和，且
 *           QAbstractScrollArea::minimumSizeHint 用的正是「水平容器宽 /
 *           垂直容器高」（qabstractscrollarea.cpp:1417-1418）。本框架无容器
 *           类，此前 minimumSizeHint 只算条自身 sizeHint——附加控件完全不
 *           参与（gap #174 的剩余差异）。这里按同一加法口径补齐：条长 +
 *           本轴两端附加件的 sizeHint 长。
 *  @param self 目标滚动区域。
 *  @param horizontal true=水平条容器（Left/Right 附加件），false=垂直条容器。
 *  @return 合计长度像素（无效提示按 0 计）。
 */
static int xasa_containerExtent(const XAbstractScrollArea* self,
                                bool horizontal)
{
    XWidget* bar;
    XSize hint;
    int sum = 0;
    int i;
    int bandA = horizontal ? XASA_SBW_LEFT : XASA_SBW_TOP;
    int bandB = horizontal ? XASA_SBW_RIGHT : XASA_SBW_BOTTOM;
    if (!self) return 0;
    bar = horizontal ? (XWidget*)self->m_hScrollBar
                     : (XWidget*)self->m_vScrollBar;
    if (bar) {
        hint = XWidget_sizeHint_base(bar);
        if (horizontal) {
            if (hint.width > 0) sum += hint.width;
        } else {
            if (hint.height > 0) sum += hint.height;
        }
    }
    for (i = 0; i < self->m_sbWidgetCount; ++i) {
        if (!self->m_sbWidgets[i]) continue;
        if ((int)self->m_sbWidgetAligns[i] != bandA &&
            (int)self->m_sbWidgetAligns[i] != bandB)
            continue;
        sum += xasa_sbWidgetExtent(self->m_sbWidgets[i], horizontal);
    }
    return sum;
}

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
                            int vsbExt, int hsbExt,
                            bool* showV, bool* showH)
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
        self->m_contentHeight > h - hsbExt)
        vOn = true; /* 横条占位后竖向放不下：补亮竖条。 */
    if (!hOn && vOn &&
        self->m_hPolicy == XScrollBarPolicy_AsNeeded &&
        self->m_contentWidth > w - vsbExt)
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

/** @brief 滚动区内容矩形（扣 QFrame 帧宽后的可用区，对标 Qt
 *         layoutChildren 里的 `q->contentsRect()` 与 maximumViewportSize 的
 *         `size() - QSize(2*frameWidth + left + right, ...)`）。
 *  @details XFrame 把帧宽写进 XWidget 的内容边距（XFrame_setFrameRect →
 *           XWidget_setContentsMargins），故 contentsRect 已是扣帧后的矩形；
 *           默认 NoFrame ⇒ 与控件全矩形相同（既有行为零变化）。此前排布/
 *           最大视口一律按控件全矩形算，带 StyledPanel 等非零帧宽的滚动区
 *           会把视口压在帧线上、maximumViewportSize 也比实际视口大 2×帧宽
 *           （gap #175）。 */
static void xasa_contentRect(const XAbstractScrollArea* self, int* x, int* y,
                             int* w, int* h)
{
    XRect cr;
    if (!self) {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    cr = XWidget_contentsRect((const XWidget*)self);
    if (x) *x = cr.x;
    if (y) *y = cr.y;
    if (w) *w = cr.width;
    if (h) *h = cr.height;
}

/* ==================== 触摸跟手滚动 + 惯性（XGui 扩展） ====================
 * [触摸滚动 2026-10-29] 移动端操作习惯的触摸滚动：按住视口拖动，内容
 * 随手指同向跟移（手指下滑=内容下移=滚向开头），抬手按近端位移采样给
 * 惯性衰减滚动（fling）。接入点=框架手势状态机的 Track 拖动模式
 * （DragBegin 认领时标 XGestureDragMode_Track，后续 UPDATE/END/异常收口
 * 改发 DragUpdate/DragEnd/DragCancel 原始位移流，XWidget.c 状态机）；
 * 此前触摸拖动只能走「无人接受→合成滚轮」回落路径（60px 一格量化、无
 * 惯性）。方向口径：主点 dy>0（手下移）→ value 减小 → scrollContentsBy
 * 平移内容向下——与滚轮/把手同用一套滚动条值域，编程滚动天然一致。 */

/** 惯性步进节拍 ms（粗定时器；dt 按实测毫秒积分，节拍只定调度频率）。 */
#define XASA_FLING_TICK_MS 16
/** 启动惯性的最低速度 px/s（低于此值抬手即停：轻放不算甩）。 */
#define XASA_FLING_START_VEL 150
/** 惯性停机速度阈值 px/s（衰减到此即停，防无限尾速）。 */
#define XASA_FLING_STOP_VEL 40
/** 速度采样窗口 ms（只取最近样本对算速度；抬手前停顿超窗=无惯性）。 */
#define XASA_VEL_WINDOW_MS 150
/** 惯性每拍速度衰减（1e-3 域）：16ms ×0.95 ≈ 半衰 215ms，行程
 *  v0×0.32s（v0=1500px/s → ~480px，对标 Android 列表 fling 观感）。 */
#define XASA_FLING_DECAY 50

/** @brief 单调毫秒（惯性积分/采样共用一手时钟）。 */
static int64_t xasa_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 停惯性（撤定时器清状态；幂等）。 */
static void xasa_flingStop(XAbstractScrollArea* self)
{
    if (!self) return;
    if (self->m_flingTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_flingTimerId);
        self->m_flingTimerId = XTIMER_INVALID_ID;
    }
    self->m_flingActive = false;
    self->m_flingVelX = 0;
    self->m_flingVelY = 0;
    self->m_flingRemX = 0;
    self->m_flingRemY = 0;
}

/** @brief 停跟手+惯性（公共收口：滚轮/resize/隐藏/滚动条把手按下）。 */
static void xasa_touchScrollStop(XAbstractScrollArea* self)
{
    if (!self) return;
    self->m_touchScrolling = false;
    self->m_velSampleCount = 0;
    self->m_velSampleHead = 0;
    xasa_flingStop(self);
}

/** @brief 启动惯性（速度过小零操作；定时器宿主=自身，TimerEvent 槽按
 *         id 守卫只消费本惯性拍，子类业务拍照常链回父类）。 */
static void xasa_flingStart(XAbstractScrollArea* self, int velX, int velY)
{
    if (!self) return;
    xasa_flingStop(self);
    {
        int ax = velX >= 0 ? velX : -velX;
        int ay = velY >= 0 ? velY : -velY;
        if (ax < XASA_FLING_START_VEL && ay < XASA_FLING_START_VEL)
            return; /* 轻放：无惯性。 */
    }
    self->m_flingVelX = velX;
    self->m_flingVelY = velY;
    self->m_flingRemX = 0;
    self->m_flingRemY = 0;
    self->m_flingLastMs = xasa_nowMs();
    self->m_flingTimerId = XObject_startTimer(
        (XObject*)self, XASA_FLING_TICK_MS, XTimerType_CoarseTimer);
    if (self->m_flingTimerId == XTIMER_INVALID_ID)
        return; /* 无事件环环境：静默不惯性（跟手滚动不受影响）。 */
    self->m_flingActive = true;
}

/** @brief 惯性拍：速度衰减 + 位移积分写入滚动条（值域钳位在
 *         XScrollBar_setValue 内部；端点顶死即停——对标 Android 列表
 *         边界急停，无回弹）。 */
static void xasa_flingTick(XAbstractScrollArea* self)
{
    int64_t now;
    int64_t dt;
    XScrollBar* vsb;
    XScrollBar* hsb;
    int velX;
    int velY;
    bool alive;
    if (!self || self->m_flingTimerId == XTIMER_INVALID_ID) return;
    now = xasa_nowMs();
    dt = now - self->m_flingLastMs;
    if (dt <= 0) dt = 1;
    if (dt > 64) dt = 64; /* 长空窗（挂起/断拍）限幅防跳变。 */
    self->m_flingLastMs = now;
    velX = self->m_flingVelX;
    velY = self->m_flingVelY;
    /* 指数衰减（1e-3 定标整数算术；dt 实测毫秒）。 */
    velX = velX * (1000 - (XASA_FLING_DECAY * (int)dt) / XASA_FLING_TICK_MS) / 1000;
    velY = velY * (1000 - (XASA_FLING_DECAY * (int)dt) / XASA_FLING_TICK_MS) / 1000;
    self->m_flingVelX = velX;
    self->m_flingVelY = velY;
    vsb = XAbstractScrollArea_verticalScrollBar(self);
    hsb = XAbstractScrollArea_horizontalScrollBar(self);
    alive = false;
    /* 纵轴积分：+v*dt 位移（1e-3 px 域）→ 内容随指方向=值减。 */
    if (vsb && velY != 0) {
        int64_t acc = (int64_t)self->m_flingRemY +
                      (int64_t)velY * dt;
        int step = (int)(acc / 1000);
        self->m_flingRemY = (int)(acc - (int64_t)step * 1000);
        if (step != 0) {
            int before = XScrollBar_value(vsb);
            XScrollBar_setValue(vsb, before - step);
            if (XScrollBar_value(vsb) == before)
                velY = 0; /* 端点顶死：该轴停摆。 */
            else
                alive = true;
        } else {
            alive = true; /* 亚像素节拍：余数累积中，仍在滑行。 */
        }
    }
    /* 横轴积分：+x=内容右移=值减。 */
    if (hsb && velX != 0) {
        int64_t acc = (int64_t)self->m_flingRemX +
                      (int64_t)velX * dt;
        int step = (int)(acc / 1000);
        self->m_flingRemX = (int)(acc - (int64_t)step * 1000);
        if (step != 0) {
            int before = XScrollBar_value(hsb);
            XScrollBar_setValue(hsb, before - step);
            if (XScrollBar_value(hsb) == before)
                velX = 0;
            else
                alive = true;
        } else {
            alive = true;
        }
    }
    {
        int ax = velX >= 0 ? velX : -velX;
        int ay = velY >= 0 ? velY : -velY;
        if (!alive || (ax < XASA_FLING_STOP_VEL && ay < XASA_FLING_STOP_VEL))
            xasa_flingStop(self); /* 端点/尾速：停机。 */
    }
}

/** @brief 采样环复位（DragBegin/收口清空）。 */
static void xasa_velSamplesReset(XAbstractScrollArea* self)
{
    if (!self) return;
    self->m_velSampleCount = 0;
    self->m_velSampleHead = 0;
}

/** @brief 推一帧主点采样（容量 4 满后覆盖最旧）。 */
static void xasa_velSamplePush(XAbstractScrollArea* self, int x, int y,
                               int64_t ms)
{
    int head;
    if (!self) return;
    head = self->m_velSampleHead;
    self->m_velSampleX[head] = x;
    self->m_velSampleY[head] = y;
    self->m_velSampleMs[head] = ms;
    self->m_velSampleHead = (head + 1) % 4;
    if (self->m_velSampleCount < 4)
        ++self->m_velSampleCount;
}

/** @brief 按采样环估抬手速度 px/s：取窗口内最旧样本对最新样本的位移
 *         差（跨度 <20ms 抖动大、>XASA_VEL_WINDOW_MS 视为停顿后轻放，
 *         均不给惯性）。方向与跟手同口径：+y=内容下移方向。 */
static void xasa_velSampleVelocity(const XAbstractScrollArea* self,
                                   int64_t nowMs, int* velX, int* velY)
{
    int i;
    int oldest = -1;
    int newest = -1;
    *velX = 0;
    *velY = 0;
    if (!self || self->m_velSampleCount < 2) return;
    for (i = 0; i < self->m_velSampleCount; ++i) {
        if (oldest < 0 ||
            self->m_velSampleMs[i] < self->m_velSampleMs[oldest])
            oldest = i;
        if (newest < 0 ||
            self->m_velSampleMs[i] > self->m_velSampleMs[newest])
            newest = i;
    }
    if (oldest == newest) return;
    {
        int64_t span = self->m_velSampleMs[newest] -
                       self->m_velSampleMs[oldest];
        if (span < 20 || span > XASA_VEL_WINDOW_MS) return;
        *velX = (int)(((int64_t)(self->m_velSampleX[newest] -
                                 self->m_velSampleX[oldest])) * 1000 / span);
        *velY = (int)(((int64_t)(self->m_velSampleY[newest] -
                                 self->m_velSampleY[oldest])) * 1000 / span);
    }
}

/** @brief 触摸手势槽（Track 滚动拖动消费）：DragBegin 可滚才认领并标
 *         Track（不可滚放行=回落合成滚轮历史语义，行为不变）；
 *         DragUpdate 按帧差分直接写滚动条（跟手无量化）；DragEnd 按采
 *         样速度给惯性；DragCancel 只停跟手。Tap/DoubleTap/LongPress 与
 *         非本区认领的位移流一律放行（嵌套滚动区/兄弟手势不受扰）。 */
#if XGESTURE_ON
static void VX_asa_touchDragEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    int gestureType;
    int gestureState;
    if (!area || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_TOUCH_DRAG) return;
    /* [减负对齐 QTouchEvent 2026-10-29] 手势载荷改 XGestureEvent：拖动
       四类映射为平移手势的四个状态（Started/Updated/Finished/Canceled，
       对标 Qt QPanGesture 状态序列）。 */
    gestureType = (int)XGestureEvent_gestureType((XGestureEvent*)event);
    gestureState = (int)XGestureEvent_state((XGestureEvent*)event);
    if (gestureType != (int)XGestureEventType_Pan) {
        /* Tap/DoubleTap/TapAndHold 与其它手势：不认领不干扰（嵌套滚动
           区/兄弟手势不受扰）。 */
        return;
    }
    switch (gestureState) {
    case (int)XGestureState_Started: {
        XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(area);
        XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(area);
        XPoint pos;
        bool scrollable;
        if (!area->m_touchScrollEnabled) return;
        scrollable = (vsb && XScrollBar_maximum(vsb) > 0) ||
                     (hsb && XScrollBar_maximum(hsb) > 0);
        if (!scrollable) return;
        xasa_touchScrollStop(area);
        pos = XGestureEvent_position((XGestureEvent*)event);
        area->m_touchScrolling = true;
        area->m_dragLastX = pos.x;
        area->m_dragLastY = pos.y;
        xasa_velSamplesReset(area);
        xasa_velSamplePush(area, pos.x, pos.y, xasa_nowMs());
        XGestureEvent_setDragMode((XGestureEvent*)event,
                                  XGestureDragMode_Track);
        XEvent_accept(event);
        return;
    }
    case (int)XGestureState_Updated: {
        XPoint pos;
        int dx;
        int dy;
        XScrollBar* bar;
        if (!area->m_touchScrolling) return;
        pos = XGestureEvent_position((XGestureEvent*)event);
        dx = pos.x - area->m_dragLastX;
        dy = pos.y - area->m_dragLastY;
        area->m_dragLastX = pos.x;
        area->m_dragLastY = pos.y;
        if (dy != 0) {
            bar = XAbstractScrollArea_verticalScrollBar(area);
            if (bar) {
                XScrollBar_setValue(bar, XScrollBar_value(bar) - dy);
            }
        }
        if (dx != 0) {
            bar = XAbstractScrollArea_horizontalScrollBar(area);
            if (bar) XScrollBar_setValue(bar, XScrollBar_value(bar) - dx);
        }
        xasa_velSamplePush(area, pos.x, pos.y, xasa_nowMs());
        XEvent_accept(event);
        return;
    }
    case (int)XGestureState_Finished: {
        int velX;
        int velY;
        if (!area->m_touchScrolling) return;
        area->m_touchScrolling = false;
        xasa_velSampleVelocity(area, xasa_nowMs(), &velX, &velY);
        xasa_velSamplesReset(area);
        xasa_flingStart(area, velX, velY);
        XEvent_accept(event);
        return;
    }
    case (int)XGestureState_Canceled: {
        if (!area->m_touchScrolling) return;
        /* 异常收口（CANCEL/新序列打断）：只停跟手，不给惯性。 */
        area->m_touchScrolling = false;
        xasa_velSamplesReset(area);
        XEvent_accept(event);
        return;
    }
    default:
        return;
    }
}
#endif /* XGESTURE_ON */

/** @brief 惯性定时器拍（id 守卫：只消费本区惯性拍；子类业务拍——如
 *         XPlainTextEdit 光标闪烁经 XClass_Parent 链入本类——原样链回
 *         父类槽，历史行为逐位保持）。 */
static void VX_asa_timerEvent(XObject* object, XTimerEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)object;
    if (!area || !event) return;
    if (area->m_flingTimerId == XTIMER_INVALID_ID ||
        XTimerEvent_timerId(event) != area->m_flingTimerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))(object, event);
        return;
    }
    xasa_flingTick(area);
    XEvent_accept((XEvent*)event);
}

/** @brief 隐藏即停触摸滚动（惯性拍在隐藏控件上写值是纯浪费）。
 * @note  [自递归根修 2026-10-29] 父类实现按**槽的真正拥有者**取：
 *         XClass_Parent(Type, Enum, Sig) 展开为 XVtableGetFunc(
 *         Type##_class_init(), Enum, Sig)——只按槽位下标读函数指针，
 *         **不跳过本类自身**；按本类名取被自己重载过的槽即拿到自己 →
 *         无限递归（切页隐藏页根触发 HideEvent → 递归爆栈，表现为
 *         点击任意菜单按钮即崩）。HideEvent 槽由 XWidget 提供默认
 *         实现，故按 XWidget 取。 */
static void VX_asa_hideEvent(XWidget* self, XEvent* event)
{
    if (self) xasa_touchScrollStop((XAbstractScrollArea*)self);
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  XWidgetEventSlot)(self, event);
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
    int vsbExt = xasa_barExtent(self, false);
    int hsbExt = xasa_barExtent(self, true);
    int pass;
    for (pass = 0; pass < 2; ++pass)
    {
        int vw;
        int vh;
        int w;
        int h;
        int finalVw;
        int finalVh;
        int oldVw;
        int oldVh;
        bool showV;
        bool showH;
        int range;
        int ml, mt, mr, mb;
        int cw, ch;
        if (!self || !self->m_viewport) return;
        /* 可用区 = 内容矩形（扣帧宽）− 视口边距（同 Qt layoutChildren）。 */
        xasa_contentRect(self, NULL, NULL, &cw, &ch);
        w = cw;
        h = ch;
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
        xasa_decideBars(self, w, h, vsbExt, hsbExt, &showV, &showH);
        /* 排布后视口终值：与 xasa_layout 的视口矩形同式推导（竖条显形
         * 扣宽、横条显形扣高）。翻页步进 = 视口尺寸（对标 Qt
         * QAbstractScrollArea，参照 XTextEdit.c:813 setPageStep(viewH)
         * 正确示范）；隐藏分支同样设置：range=0 时把手折算占满全槽，
         * 无显示副作用。 */
        finalVw = showV ? w - vsbExt : w;
        finalVh = showH ? h - hsbExt : h;
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
    int w = 0;
    int h = 0;
    int baseX = 0;
    int baseY = 0;
    int vsbExt = xasa_barExtent(self, false);
    int hsbExt = xasa_barExtent(self, true);
    bool showV;
    bool showH;
    XRect r;
    int ml, mt, mr, mb;
    int ox;
    int oy;
    if (!self || !self->m_viewport) return;
    /* 基准 = 内容矩形（扣 QFrame 帧宽，对标 Qt layoutChildren 的
       contentsRect），无帧时与控件全矩形相同。 */
    xasa_contentRect(self, &baseX, &baseY, &w, &h);
    /* 视口边距（setViewportMargins，对标 Qt layoutChildren 的
       margins 口径）：可视内容区整体内收，视口/滚动条/角件均在
       收边后的矩形内排布。 */
    xasa_margins(self, &ml, &mt, &mr, &mb);
    ox = baseX + ml;
    oy = baseY + mt;
    w -= ml + mr;
    h -= mt + mb;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* 显隐判定与 xasa_updateScrollBars 同一份联立口径（xasa_decideBars，
       基准=无条全视口）：两处分裂时条会「显示却不排布」，而各自独立
       按当前视口判定时会双条互锁（详见 xasa_decideBars 注）。 */
    xasa_decideBars(self, w, h, vsbExt, hsbExt, &showV, &showH);
    XRect_init(&r, ox, oy, showV ? w - vsbExt : w,
               showH ? h - hsbExt : h);
    XWidget_setGeometryRect(self->m_viewport, &r);
    if (showV) {
        XRect_init(&r, ox + w - vsbExt, oy, vsbExt,
                   showH ? h - hsbExt : h);
        XWidget_setGeometryRect((XWidget*)self->m_vScrollBar, &r);
    }
    if (showH) {
        XRect_init(&r, ox, oy + h - hsbExt, showV ? w - vsbExt : w,
                   hsbExt);
        XWidget_setGeometryRect((XWidget*)self->m_hScrollBar, &r);
    }
    /* 右下角控件：仅当两个滚动条都显示时可见。 */
    if (self->m_cornerWidget) {
        if (showV && showH) {
            XRect_init(&r, ox + w - vsbExt, oy + h - hsbExt,
                       vsbExt, hsbExt);
            XWidget_setGeometryRect(self->m_cornerWidget, &r);
            XWidget_setVisible(self->m_cornerWidget, true);
        } else {
            XWidget_setVisible(self->m_cornerWidget, false);
        }
    }
    /* 附加滚动条控件（addScrollBarWidget）：按 Qt 容器模型落在
     * 「滚动条条带」内，与条共用同一条行/列、不额外侵占视口
     * （qabstractscrollarea.cpp:762-785：控件入水平/垂直条容器，
     * 容器与视口并列）。此前实现把控件直接覆盖在视口边缘上——既遮挡
     * 内容，也不随条的显隐收敛。 */
    {
        int i;
        for (i = 0; i < self->m_sbWidgetCount; ++i) {
            XWidget* sw = self->m_sbWidgets[i];
            int category = (int)self->m_sbWidgetAligns[i];
            bool bandVisible;
            bool shown;
            if (!sw) continue;
            bandVisible = (category == XASA_SBW_LEFT ||
                           category == XASA_SBW_RIGHT) ? showH : showV;
            if (category == XASA_SBW_LEFT || category == XASA_SBW_RIGHT) {
                int cw = xasa_sbWidgetExtent(sw, true);
                int x;
                if (cw < 0) cw = 0;
                if (cw > w) cw = w;
                x = (category == XASA_SBW_LEFT) ? ox : ox + w - cw;
                XRect_init(&r, x, oy + h - hsbExt, cw, hsbExt);
                XWidget_setGeometryRect(sw, &r);
            } else {
                int ch = xasa_sbWidgetExtent(sw, false);
                int y;
                if (ch < 0) ch = 0;
                if (ch > h) ch = h;
                y = (category == XASA_SBW_TOP) ? oy : oy + h - ch;
                XRect_init(&r, ox + w - vsbExt, y, vsbExt, ch);
                XWidget_setGeometryRect(sw, &r);
            }
            /* 条带隐藏 ⇒ 附加控件随之隐藏（Qt 控件住在条容器里，容器
             * 不可见时控件亦不可见）；判同值再下发，避免显隐抖动带出
             * 布局失效风暴（XWidget_setVisible 无同值早退）。 */
            shown = !XWidget_isHidden(sw);
            if (shown != bandVisible) XWidget_setVisible(sw, bandVisible);
        }
    }
}

static void VX_asa_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractScrollArea* area = (XAbstractScrollArea*)self;
    (void)event;
    if (!area || !area->m_viewport) return;
    xasa_touchScrollStop(area); /* 几何重排：值域即将重算，停跟手/惯性。 */
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

/** @brief 默认内容滚动：请求视口重绘（对标 QAbstractScrollArea::
 *         scrollContentsBy 的缺省实现 `viewport()->update();`，
 *         见 Qt 6.8 qabstractscrollarea.cpp:1328-1331）。
 *         派生类（XScrollArea 等）覆写后改为平移内容控件。 */
static void VX_asa_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    XWidget* viewport;
    (void)dx;
    (void)dy;
    if (!self) return;
    viewport = self->m_viewport;
    if (viewport) XWidget_update(viewport);
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

/* SizeHint 族业务槽（`VX*` 槽体，定义在下方 SizeHint 段；按《api命名
 * 风格》`*_base` 契约：业务逻辑只放 `VX*` 槽体，对外唯一入口是分派
 * `XAbstractScrollArea_sizeHint_base` /
 * `XAbstractScrollArea_minimumSizeHint_base`）。 */
static XSize VXAbstractScrollArea_sizeHint(const XAbstractScrollArea* self);
static XSize VXAbstractScrollArea_minimumSizeHint(
    const XAbstractScrollArea* self);

/* ViewportSizeHint 基类默认实现（`VX*` 槽体，定义在 viewportSizeHint 段；
 * 按《api命名风格》`*_base` 契约注册虚表默认槽）。 */
static XSize VXAbstractScrollArea_viewportSizeHint(
    const XAbstractScrollArea* self);

XVtable* XAbstractScrollArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractScrollArea)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXAbstractScrollArea_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint,
                             VXAbstractScrollArea_minimumSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_asa_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_asa_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy,
                             VX_asa_scrollContentsBy);
    /* [Qt 对齐 2026-11-11] 两个保护虚槽的缺省实现。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_SetupViewport,
                             XAbstractScrollArea_setupViewport_base);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportSizeHint,
                             VXAbstractScrollArea_viewportSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportEvent,
                             XAbstractScrollArea_viewportEvent_base);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_asa_wheelEvent);
    /* [触摸滚动 2026-10-29] Track 拖动模式消费 + 惯性拍 + 隐藏停滚。 */
#if XGESTURE_ON
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchDragEvent, VX_asa_touchDragEvent);
#endif
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VX_asa_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VX_asa_hideEvent);
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
    /* 对标 Qt QAbstractScrollAreaPrivate::init（qabstractscrollarea.cpp:
     * 268-273）末尾 setSizePolicy(Expanding, Expanding)：滚动区族
     * （QTextEdit/QGraphicsView/QTableView/QTableWidget…）缺省双向
     * Expanding——布局余量吃满、随窗口伸缩。XGui 缺省 Preferred 使
     * 滚动区在布局里钉在 sizeHint（主轴 stretch>0 时 smartSizeHint
     * 取 min、交叉轴 crossExpanding=false 不铺满），如数据页表格
     * 右侧整段留白（无头截图实证）。 */
    XWidget_setSizePolicy((XWidget*)self, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    self->m_viewport = XWidget_create(self, 0);
    /* [2026-10-15 定版撤回] Qt 对标的视口 Base+autofill 两行经实验
       撤回：增量重绘路径下视口底色存在父/子绘序倒挂——滚动后视口
       autofill 的 Base 填充落在内容控件（表格）绘制之后，整块盖掉
       表格（桌面 1000x600 滚轮复现，回读取证 entry=ffffff→end=
       efefef）。底色语义由内容控件自身 autofill 承担（见 Modbus 页
       body 同日改动），视口维持透明。 */
    /* 对标 qabstractscrollarea.cpp:272 viewport->setFocusProxy(q)：
       视口命中时焦点代理到滚动区本体（viewport 缺省 NoFocus，点击
       事件按命中靶查策略位——无代理则点击视口永不聚焦滚动区）。
       【redundant#4 2026-11-05】此前此处连续两行完全相同的
       setFocusProxy 调用（含逐字重复的注释块）——无效重复，Qt 每次
       setFocusProxy 只调一次（qwidget.cpp:6407-6411）。已删重复的那一份，
       零行为变化。 */
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
        (int)XGuiOrientation_Vertical, self, 0);
    self->m_hScrollBar = XScrollBar_create_2(
        (int)XGuiOrientation_Horizontal, self, 0);
    /* 视口事件过滤器（对标 QAbstractScrollAreaPrivate::init 的
     * viewportFilter 安装，qabstractscrollarea.cpp:270-271）。 */
    self->m_viewportFilter = xasa_filterCreate(self);
    xasa_installViewportFilter(self);
    self->m_vPolicy = (uint8_t)XScrollBarPolicy_AsNeeded;
    self->m_hPolicy = (uint8_t)XScrollBarPolicy_AsNeeded;
    /* [触摸滚动 2026-10-29] 跟手/惯性默认开；定时器 id 显式置无效
     * （XMemset 已清零，XTIMER_INVALID_ID 非零哨兵时防误判）。 */
    self->m_touchScrollEnabled = true;
    self->m_flingTimerId = XTIMER_INVALID_ID;
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
     * 1405-1423）= 水平条容器宽 + 垂直条厚 + 边框。本类无「滚动条容器」
     * 概念，容器宽/高即条 sizeHint 宽/高；缓存值与虚槽同源
     * （VXAbstractScrollArea_minimumSizeHint），随条换装由调用方重取。 */
    {
        XSize minHint = VXAbstractScrollArea_minimumSizeHint(self);
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

/** @brief 把旧条的全部滑块属性迁到新条（对标
 *         QAbstractScrollAreaPrivate::replaceScrollBar，qabstractscrollarea.cpp:
 *         601-660 的逐行顺序：可见性 → 反向外观/控制 → 范围 → 方向 →
 *         页步 → 单步 → 把手按下 → 把手位置 → 跟踪 → 值）。
 *  @details 顺序是语义的一部分：范围先于 pageStep/value，否则 value 会按旧
 *           范围被钳位；值最后写。Qt 在迁移**之后**才 connect 新条的
 *           valueChanged（:652），迁移期不会触发滑动回调——本函数同样不做
 *           信号连接，调用方在迁移后接。 */
static void xasa_migrateBar(XAbstractScrollArea* self, XScrollBar* oldBar,
                            XScrollBar* newBar)
{
    if (!self || !oldBar || !newBar) return;
    XWidget_setVisible((XWidget*)newBar, !XWidget_isHidden((XWidget*)oldBar));
    XScrollBar_setInvertedAppearance(
        newBar, XScrollBar_invertedAppearance(oldBar));
    XScrollBar_setInvertedControls(
        newBar, XScrollBar_invertedControls(oldBar));
    XScrollBar_setRange(newBar, XScrollBar_minimum(oldBar),
                        XScrollBar_maximum(oldBar));
    XScrollBar_setOrientation(newBar, XScrollBar_orientation(oldBar));
    XScrollBar_setPageStep(newBar, XScrollBar_pageStep(oldBar));
    XScrollBar_setSingleStep(newBar, XScrollBar_singleStep(oldBar));
    XScrollBar_setSliderDown(newBar, XScrollBar_isSliderDown(oldBar));
    XScrollBar_setSliderPosition(newBar, XScrollBar_sliderPosition(oldBar));
    XScrollBar_setTracking(newBar, XScrollBar_hasTracking(oldBar));
    XScrollBar_setValue(newBar, XScrollBar_value(oldBar));
}

void XAbstractScrollArea_setVerticalScrollBar(XAbstractScrollArea* self,
                                              XScrollBar* scrollbar)
{
    XScrollBar* oldBar;
    if (!self) return;
    /* 对标 Qt 6.8 首行判空拒绝（qWarning 后 return）：此前接受 NULL
     * ——删旧条后视口恒 w-16 留 16px 死列且滚动静默失效。 */
    if (!scrollbar) return;
    if (self->m_vScrollBar == scrollbar) return;
    oldBar = self->m_vScrollBar;
    self->m_vScrollBar = scrollbar;
    XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
    if (oldBar) {
        /* 对标 replaceScrollBar：先摘旧条连接（本框架以信号槽承载 Qt 的
         * 内部槽连接），迁移属性，再销毁旧条。 */
        xasa_disconnectBar(self, oldBar, false);
        xasa_migrateBar(self, oldBar, scrollbar);
        XClassDelete((XWidget*)oldBar);
    }
    xasa_connectBar(self, scrollbar, false);
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

void XAbstractScrollArea_setHorizontalScrollBar(XAbstractScrollArea* self,
                                                XScrollBar* scrollbar)
{
    XScrollBar* oldBar;
    if (!self) return;
    /* 同 setVerticalScrollBar：NULL 入参拒绝（Qt 对两个 setter 同一口径）。 */
    if (!scrollbar) return;
    if (self->m_hScrollBar == scrollbar) return;
    oldBar = self->m_hScrollBar;
    self->m_hScrollBar = scrollbar;
    XWidget_setParent((XWidget*)scrollbar, (XWidget*)self, 0);
    if (oldBar) {
        xasa_disconnectBar(self, oldBar, true);
        xasa_migrateBar(self, oldBar, scrollbar);
        XClassDelete((XWidget*)oldBar);
    }
    xasa_connectBar(self, scrollbar, true);
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
    /* 存「条带类别」而非原始位掩码：查询/排布都按 Qt 的四类分区口径
       （见 xasa_sbWidgetCategory）。 */
    self->m_sbWidgetAligns[self->m_sbWidgetCount] =
        (uint16_t)xasa_sbWidgetCategory(alignment);
    ++self->m_sbWidgetCount;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

const XWidget** XAbstractScrollArea_scrollBarWidgets(
    const XAbstractScrollArea* self, int alignment)
{
    XAbstractScrollArea* mutableSelf;
    int order[4];
    int flags[4];
    int n = 0;
    int i;
    int k;
    if (!self) return NULL;
    /* 分类过滤 + 固定拼接顺序 Left→Right→Top→Bottom（对标 Qt
     * scrollBarWidgets 逐类 += 的顺序，qabstractscrollarea.cpp:787-803）。
     * 【2026-11-11 根修】过滤必须按**公开对齐位**（XAlignment_*）判定：
     * m_sbWidgetAligns 存的是内部四值类别（XASA_SBW_*，0x1/0x2/0x4/
     * 0x8），此前直接拿 `alignment & order[k]` 比较——Top(0x20)/
     * Bottom(0x40) 永远命中不了内部 TOP(0x4)/BOTTOM(0x8)，只有
     * Left/Right 因数值碰巧相同而可用（对齐测试实测 Top 查询恒空）。 */
    order[0] = XASA_SBW_LEFT;
    order[1] = XASA_SBW_RIGHT;
    order[2] = XASA_SBW_TOP;
    order[3] = XASA_SBW_BOTTOM;
    flags[0] = (int)XAlignment_Left;
    flags[1] = (int)XAlignment_Right;
    flags[2] = (int)XAlignment_Top;
    flags[3] = (int)XAlignment_Bottom;
    mutableSelf = (XAbstractScrollArea*)self;
    for (k = 0; k < 4; ++k) {
        if (!(alignment & flags[k])) continue;
        for (i = 0; i < self->m_sbWidgetCount && n < 8; ++i) {
            if ((int)self->m_sbWidgetAligns[i] == order[k] &&
                self->m_sbWidgets[i])
                mutableSelf->m_sbWidgetQuery[n++] = self->m_sbWidgets[i];
        }
    }
    mutableSelf->m_sbWidgetQuery[n] = NULL;
    return (const XWidget**)mutableSelf->m_sbWidgetQuery;
}

void XAbstractScrollArea_setViewport(XAbstractScrollArea* self,
                                     XWidget* widget)
{
    XWidget* oldViewport;
    if (!self) return;
    /* 对标 Qt（qabstractscrollarea.cpp:491-514）：
     * ① widget 为 NULL ⇒ 新建缺省视口（此前直接忽略，Qt 明确支持）；
     * ② 同指针无操作；
     * ③ 新视口 reparent + 焦点代理，调 setupViewport，**最后**删旧视口。 */
    if (!widget) {
        widget = XWidget_create((XWidget*)self, 0);
        if (!widget) return;
    }
    if (self->m_viewport == widget) return;
    oldViewport = self->m_viewport;
    self->m_viewport = widget;
    XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 qabstractscrollarea.cpp:504 `viewport->setFocusProxy(this)`：
     * 视口命中时焦点代理到滚动区本体。 */
    XWidget_setFocusProxy(widget, (XWidget*)self);
    /* 视口是真实焦点靶：免整件补 update（与 init 同口径）。 */
    XWidget_setAttribute(widget, XWidgetAttribute_FocusOwnerQuiet, true);
    /* 新视口同样装本区的事件过滤器（对标 setViewport 的
     * `d->viewport->installEventFilter(d->viewportFilter.data())`，
     * qabstractscrollarea.cpp:501）。旧视口上的登记随其析构自动摘除。 */
    xasa_installViewportFilter(self);
    VX_asa_resizeEvent((XWidget*)self, NULL);
    XAbstractScrollArea_setupViewport(self, widget);
    if (oldViewport) {
        /* 视口是滚动区输入/绘制事件的接收主体：在视口自身事件栈内
           （视口事件→槽→setViewport）被换时，同步删会在其事件处理帧
           返回途中释放结构体造成 UAF；延迟到事件循环归还后回收
           （与 XWindowDecoration 控件释放契约同口径；Qt 为同步 delete，
           此处仅回收时机不同，语义同为「旧视口随换装销毁」）。 */
        XObject_deleteLater((XObject*)oldViewport);
    }
}

XSize XAbstractScrollArea_maximumViewportSize(
    const XAbstractScrollArea* self)
{
    XSize out;
    int w;
    int h;
    bool showV;
    bool showH;
    int ml, mt, mr, mb;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    w = 0;
    h = 0;
    /* 对标 Qt：最大视口 = 内容矩形（已扣 2×frameWidth，qabstractscrollarea.cpp:
     * 539-541 `size() - QSize(f + left + right, f + top + bottom)`）再扣视口
     * 边距。无帧时与控件全矩形相同（既有行为不变）。 */
    xasa_contentRect(self, NULL, NULL, &w, &h);
    /* 对标 Qt：最大视口在视口边距内收后的矩形内推导。 */
    xasa_margins(self, &ml, &mt, &mr, &mb);
    w -= ml + mr;
    h -= mt + mb;
    /* 对标 QAbstractScrollArea::maximumViewportSize（qabstractscrollarea.cpp:
     * 533-547）：只在策略为 AlwaysOn 时扣条厚，条厚取条自身
     * sizeHint（vbar.width / hbar.height，与 layoutChildren 同源）——
     * AsNeeded 下「最大视口」是「条没有有效范围时」的尺寸，与条当前
     * 是否恰好亮着无关。 */
    showV = self->m_vScrollBar && self->m_vPolicy == XScrollBarPolicy_AlwaysOn;
    showH = self->m_hScrollBar && self->m_hPolicy == XScrollBarPolicy_AlwaysOn;
    XSize_init(&out,
               showV ? w - xasa_barExtent(self, false) : w,
               showH ? h - xasa_barExtent(self, true) : h);
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
       71-72，protected 面；此处按框架惯例公开）：Qt 原样存四向 px
       （qabstractscrollarea.cpp:819-827 不钳负值——负值等价向外扩边），
       值不变时短路省掉一次幂等重排。 */
    if (self->m_marginLeft == left && self->m_marginTop == top &&
        self->m_marginRight == right && self->m_marginBottom == bottom)
        return;
    self->m_marginLeft = left;
    self->m_marginTop = top;
    self->m_marginRight = right;
    self->m_marginBottom = bottom;
    VX_asa_resizeEvent((XWidget*)self, NULL);
}

/** @brief SizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；对标
 *         QAbstractScrollArea::sizeHint）。按《api命名风格》`*_base`
 *         契约，业务逻辑只放此处，对外唯一入口是分派
 *         `XAbstractScrollArea_sizeHint_base`。 */
static XSize VXAbstractScrollArea_sizeHint(const XAbstractScrollArea* self)
{
    XSize out;
    int frame;
    int sbw = 0;
    int sbh = 0;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 对标 Qt QAbstractScrollArea::sizeHint（qabstractscrollarea.cpp:
     * 1426-1447）：
     * - AdjustIgnored：直接给缺省首选尺寸 256x192（Qt 为字面常量；
     *   本框架把该值存进控件的 sizeHint 缓存，初值同值，应用显式
     *   XWidget_setSizeHint 可覆盖——X 侧的一处便利偏差）；
     * - AdjustToContents / AdjustToContentsOnFirstShow：帧宽 + 条尺寸
     *   （条确实可见且策略非 AlwaysOff 时）+ **viewportSizeHint()**
     *   （此前用 m_contentWidth/Height：Qt 从不读内容尺寸，视口建议
     *   尺寸才是口径，且子类可重载 viewportSizeHint）；
     * - OnFirstShow 首次计算后缓存（对标 Qt 的 d->sizeHint 缓存），
     *   策略变更时由 setSizeAdjustPolicy 清除。 */
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored) {
        /* 回落基类：非虚读取（否则槽 → 本函数 → 槽 死循环）。 */
        return XClass_Parent(XWidget, EXWidget_SizeHint,
                             XSize (*)(const XWidget*))((const XWidget*)self);
    }
    if (self->m_sizeAdjustPolicy ==
            XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow &&
        self->m_sizeHintCached) {
        return self->m_sizeHintCache;
    }
    frame = 2 * XFrame_frameWidth((const XFrame*)self);
    /* Qt 判据 `!vbar->isVisibleTo(this) || policy == AlwaysOff`：条在
     * 本控件子树内可见才计入；X 侧以显式隐藏位（isHidden，即
     * visibleTo 的等价面）承载。 */
    if (self->m_vScrollBar &&
        self->m_vPolicy != XScrollBarPolicy_AlwaysOff &&
        (self->m_vPolicy == XScrollBarPolicy_AlwaysOn ||
         !XWidget_isHidden((XWidget*)self->m_vScrollBar))) {
        XSize s = XWidget_sizeHint_base((XWidget*)self->m_vScrollBar);
        sbw = s.width > 0 ? s.width : 0;
    }
    if (self->m_hScrollBar &&
        self->m_hPolicy != XScrollBarPolicy_AlwaysOff &&
        (self->m_hPolicy == XScrollBarPolicy_AlwaysOn ||
         !XWidget_isHidden((XWidget*)self->m_hScrollBar))) {
        XSize s = XWidget_sizeHint_base((XWidget*)self->m_hScrollBar);
        sbh = s.height > 0 ? s.height : 0;
    }
    {
        XSize vp = XAbstractScrollArea_viewportSizeHint_base(self);
        XSize_init(&out, frame + sbw + vp.width, frame + sbh + vp.height);
    }
    if (self->m_sizeAdjustPolicy ==
        XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow) {
        ((XAbstractScrollArea*)self)->m_sizeHintCache = out;
        ((XAbstractScrollArea*)self)->m_sizeHintCached = true;
    }
    return out;
}

/** @brief ViewportSizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；
 *         按《api命名风格》`*_base` 契约，业务逻辑只放此处，对外唯一
 *         入口是分派 `XAbstractScrollArea_viewportSizeHint_base`；
 *         2026-11-12 命名契约整改：原倒置形状已交换。 */
static XSize VXAbstractScrollArea_viewportSizeHint(
    const XAbstractScrollArea* self)
{
    XSize out;
    int h;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 对标 Qt 缺省实现（qabstractscrollarea.cpp:1450-1462）：
     * 视口自身 sizeHint 有效则用它，否则 h = max(10, 字高) 的 6h × 4h。 */
    if (self->m_viewport) {
        XSize sh = XWidget_sizeHint_base(self->m_viewport);
        if (XSize_isValid(&sh)) return sh;
    }
#if XPAINTER_ON
    {
        XFont font = XWidget_font((const XWidget*)self);
        h = XPainter_textHeight(&font);
        XClassDeinit(&font);
    }
#else
    h = 0;
#endif /* XPAINTER_ON */
    if (h < 10) h = 10;
    XSize_init(&out, 6 * h, 4 * h);
    return out;
}

/** @brief 视口建议尺寸（虚函数**调度入口**，只查虚表分派；
 *         子类可重载 EXAbstractScrollArea_ViewportSizeHint，槽内回落用
 *         XClass_Parent——《api命名风格》`*_base` 契约）。 */
XSize XAbstractScrollArea_viewportSizeHint_base(const XAbstractScrollArea* self)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) {
        XSize out;
        XSize_init(&out, 0, 0);
        return out;
    }
    return XClassGetVirtualFunc(self, EXAbstractScrollArea_ViewportSizeHint,
                                XSize (*)(const XAbstractScrollArea*))(self);
}

void XAbstractScrollArea_setupViewport(XAbstractScrollArea* self,
                                      XWidget* viewport)
{
    void (*fn)(XAbstractScrollArea*, XWidget*);
    if (!self) return;
    if (!XClassGetVtable(self)) return;
    fn = XClassGetVirtualFunc(self, EXAbstractScrollArea_SetupViewport,
                              void (*)(XAbstractScrollArea*, XWidget*));
    if (fn && fn != XAbstractScrollArea_setupViewport_base)
        fn(self, viewport);
    else
        XAbstractScrollArea_setupViewport_base(self, viewport);
}

void XAbstractScrollArea_setupViewport_base(XAbstractScrollArea* self,
                                           XWidget* viewport)
{
    /* 对标 Qt 缺省实现：空操作（qabstractscrollarea.cpp:1498-1501）。 */
    (void)self;
    (void)viewport;
}

bool XAbstractScrollArea_viewportEvent(XAbstractScrollArea* self,
                                       XEvent* event)
{
    bool (*fn)(XAbstractScrollArea*, XEvent*);
    if (!self || !event) return false;
    if (!XClassGetVtable(self)) return false;
    /* 虚槽分发（对标 qabstractscrollarea.h:75 的 protected virtual）。 */
    fn = XClassGetVirtualFunc(self, EXAbstractScrollArea_ViewportEvent,
                              bool (*)(XAbstractScrollArea*, XEvent*));
    if (!fn) return false;
    return fn(self, event);
}

bool XAbstractScrollArea_viewportEvent_base(XAbstractScrollArea* self,
                                            XEvent* event)
{
    /* 缺省不消费视口事件：本框架的视口是真实事件靶，绘制在滚动区本体上
     * （见头文件 @note 与 XTextEdit.c 的 paintEvent），未处理事件沿父链
     * 上抛到滚动区——再做 Qt 式的「重映射给滚动区处理器」会重复投递。 */
    (void)self;
    (void)event;
    return false;
}

XMargins XAbstractScrollArea_viewportMargins(const XAbstractScrollArea* self)
{
    XMargins margins;
    if (!self) {
        margins.left = 0;
        margins.top = 0;
        margins.right = 0;
        margins.bottom = 0;
        return margins;
    }
    margins.left = self->m_marginLeft;
    margins.top = self->m_marginTop;
    margins.right = self->m_marginRight;
    margins.bottom = self->m_marginBottom;
    return margins;
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

/* ==================== 触摸滚动（XGui 扩展） ==================== */

void XAbstractScrollArea_setTouchScrollEnabled(XAbstractScrollArea* self,
                                               bool enabled)
{
    if (!self || self->m_touchScrollEnabled == enabled) return;
    self->m_touchScrollEnabled = enabled;
    if (!enabled)
        xasa_touchScrollStop(self); /* 关断即收：跟手/惯性一并停。 */
}

bool XAbstractScrollArea_touchScrollEnabled(const XAbstractScrollArea* self)
{
    return self ? self->m_touchScrollEnabled : false;
}

void XAbstractScrollArea_stopTouchScroll(XAbstractScrollArea* self)
{
    xasa_touchScrollStop(self);
}

/** @brief MinimumSizeHint 基类默认实现（`VX*` 槽体，注册虚表默认槽；
 *         对标 QAbstractScrollArea::minimumSizeHint）。按《api命名风格》
 *         `*_base` 契约，业务逻辑只放此处，对外唯一入口是分派
 *         `XAbstractScrollArea_minimumSizeHint_base`。 */
static XSize VXAbstractScrollArea_minimumSizeHint(
    const XAbstractScrollArea* self)
{
    XSize out;
    int vsbExt = 15; /* XScrollBar 默认尺寸提示 15x15。 */
    int hsbExt = 15;
    int hbarW = 15;  /* 水平条 sizeHint 宽（Qt: scrollBarContainers[Horizontal]） */
    int vbarH = 15;  /* 垂直条 sizeHint 高（Qt: scrollBarContainers[Vertical]） */
    int extra = 0;
    if (!self) {
        XSize_init(&out, 0, 0);
        return out;
    }
    /* 本框架无「滚动条容器」类：容器 sizeHint 由 xasa_containerExtent 按
       Qt 的加法口径现算（条长 + 本轴两端附加控件 sizeHint 之和）。 */
    if (self->m_vScrollBar) {
        XSize s = XWidget_sizeHint_base((XWidget*)self->m_vScrollBar);
        if (s.width > 0) vsbExt = s.width;
    }
    if (self->m_hScrollBar) {
        XSize s = XWidget_sizeHint_base((XWidget*)self->m_hScrollBar);
        if (s.height > 0) hsbExt = s.height;
    }
    hbarW = xasa_containerExtent(self, true);
    vbarH = xasa_containerExtent(self, false);
    if (hbarW <= 0) hbarW = 15;
    if (vbarH <= 0) vbarH = 15;
    extra = 2 * XFrame_frameWidth((const XFrame*)self);
    /* 对标 QAbstractScrollArea::minimumSizeHint（qabstractscrollarea.cpp:
     * 1405-1423）：宽 = 水平条容器宽 + 垂直条厚 + 帧宽，
     * 高 = 垂直条容器高 + 水平条厚 + 帧宽。此前只算条厚（缺条长项），
     * 布局收缩下界比 Qt 小一截（Qt Fusion 实测 70x70 = 54+14+2）。 */
    XSize_init(&out, hbarW + vsbExt + extra, vbarH + hsbExt + extra);
    return out;
}






#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#endif /* XGUI_ON */
