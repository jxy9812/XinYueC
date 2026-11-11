/**
 * @file       XAbstractScrollAreaAlignTest.c
 * @brief      XAbstractScrollArea × Qt 6.8 QAbstractScrollArea 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *               qabstractscrollarea.h（视口/双条/策略/角件/附加条控件/
 *               viewportMargins/viewportSizeHint/setupViewport）；
 *             - Qt 6.8.3 源码 qabstractscrollarea.cpp：
 *               maximumViewportSize（:533-547 只按 AlwaysOn 扣条厚）、
 *               scrollBarWidgets（:787-803 按 Left/Right/Top/Bottom
 *               四类过滤拼接）、setViewport（:491-514 NULL 建缺省视口、
 *               先 setupViewport 后删旧视口）、sizeHint（:1426-1447
 *               AdjustIgnored 为字面 256x192，其余用 viewportSizeHint）、
 *               minimumSizeHint（:1405-1423 条长 + 条厚 + 帧宽）、
 *               scrollContentsBy 缺省 `viewport()->update()`（:1328-1331）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：默认策略
 *               AsNeeded、maximumViewportSize 198x198（200x200 控件、
 *               AsNeeded）、vAlwaysOn ⇒ 宽 184；scrollBarWidgets 分类
 *               计数 left=1/top=1/right=0/left|top=2；setViewport(新件)
 *               旧视口同步销毁。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && \
    XABSTRACTSCROLLAREA_ON

#include "XAbstractScrollArea.h"
#include "XAbstractScrollArea_Protected.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XScrollBar.h"
#include "XAlignment.h"
#include "XClass.h"
#include "XCoreApplication.h"

/* ==================== ① 构造默认值 + 视口/滚动条几何 ==================== */

XQ_TEST(XAbstractScrollArea, defaultsAndGeometry)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* viewport;
    XScrollBar* vbar;
    XScrollBar* hbar;
    XSize maxVp;
    int frame;
    int vbarW;

    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport(area);
    vbar = XAbstractScrollArea_verticalScrollBar(area);
    hbar = XAbstractScrollArea_horizontalScrollBar(area);
    XQ_CHECK_PTR(viewport);
    XQ_CHECK_PTR(vbar);
    XQ_CHECK_PTR(hbar);
    /* 视口/双条都是本控件的子控件（Qt init 同：viewport + 两容器）。 */
    XQ_CHECK(XWidget_parentWidget(viewport) == (XWidget*)area);
    XQ_CHECK(XWidget_parentWidget((XWidget*)vbar) == (XWidget*)area);

    /* 缺省策略 AsNeeded（qabstractscrollarea.cpp:545-560 文档口径）。 */
    XQ_EQ_I64(XAbstractScrollArea_verticalScrollBarPolicy(area),
              XScrollBarPolicy_AsNeeded);
    XQ_EQ_I64(XAbstractScrollArea_horizontalScrollBarPolicy(area),
              XScrollBarPolicy_AsNeeded);
    /* 缺省视口边距全 0（viewportMargins() 文档「By default all the
     * margins are zero」）。 */
    {
        XMargins m = XAbstractScrollArea_viewportMargins(area);
        XQ_CHECK(m.left == 0 && m.top == 0 && m.right == 0 && m.bottom == 0);
    }

    XWidget_resize((XWidget*)area, 200, 200);
    frame = 2 * XFrame_frameWidth((const XFrame*)area);
    /* 缺省帧为 NoFrame ⇒ frameWidth=0（Qt 的 QAbstractScrollArea 缺省
     * StyledPanel|Sunken、frameWidth=1；X 侧由 XFrame 缺省决定，本用例
     * 以实际帧宽参与断言，两种取值都成立）。 */
    XQ_CHECK(XFrame_frameWidth((const XFrame*)area) == 0);
    /* maximumViewportSize：尺寸 − 帧 − 边距，**只在 AlwaysOn 时**扣条厚
     * （qabstractscrollarea.cpp:533-547）。AsNeeded 下即使条亮着也不扣。 */
    maxVp = XAbstractScrollArea_maximumViewportSize(area);
    XQ_EQ_I64(maxVp.width, 200 - frame);
    XQ_EQ_I64(maxVp.height, 200 - frame);

    vbarW = XWidget_sizeHint((XWidget*)vbar).width;
    XQ_CHECK(vbarW > 0);
    XAbstractScrollArea_setVerticalScrollBarPolicy(area,
                                                   XScrollBarPolicy_AlwaysOn);
    maxVp = XAbstractScrollArea_maximumViewportSize(area);
    XQ_EQ_I64(maxVp.width, 200 - frame - vbarW);
    XQ_EQ_I64(maxVp.height, 200 - frame);
    /* 策略 AlwaysOn ⇒ 条不再处于「显式隐藏」态。不能用 isVisible 断言：
     * 本机 Qt 6.8 探针（offscreen）中 area 未 show 时
     * vbar->isVisible()==false（生效可见要求顶层可见），但
     * isHidden()==false；Qt 的 isVisibleTo(area) 也因条容器被显式隐藏
     * 而为 false——X 侧无容器层，等价判据是显式隐藏位。 */
    XQ_CHECK(!XWidget_isHidden((XWidget*)vbar));

    XClassDelete((XClass*)area);
}

/* ==================== ② viewportMargins / viewportSizeHint / sizeHint ==== */

XQ_TEST(XAbstractScrollArea, marginsAndSizeHint)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* viewport;
    XRect vpGeom;
    XSize hint;
    XSize vpHint;
    XMargins m;
    int frame;
    int vbarW;
    int hbarH;

    XQ_REQUIRE(area != NULL);
    viewport = XAbstractScrollArea_viewport(area);
    XWidget_resize((XWidget*)area, 200, 200);
    frame = 2 * XFrame_frameWidth((const XFrame*)area);

    /* setViewportMargins 原样存四向（Qt 不钳负值），视口几何同步内收
     * （qabstractscrollarea.cpp:819-857 + layoutChildren 的 adjust）。 */
    XAbstractScrollArea_setViewportMargins(area, 10, 5, 20, 7);
    m = XAbstractScrollArea_viewportMargins(area);
    XQ_CHECK(m.left == 10 && m.top == 5 && m.right == 20 && m.bottom == 7);
    vpGeom = XWidget_geometry(viewport);
    XQ_EQ_I64(vpGeom.x, 10);
    XQ_EQ_I64(vpGeom.y, 5);
    XQ_EQ_I64(vpGeom.width, 200 - 30 - frame);
    XQ_EQ_I64(vpGeom.height, 200 - 12 - frame);
    XQ_EQ_I64(XAbstractScrollArea_maximumViewportSize(area).width,
              vpGeom.width);
    /* 负值不再钳 0（Qt 同：负边距等价向外扩边）。 */
    XAbstractScrollArea_setViewportMargins(area, -4, 0, 0, 0);
    m = XAbstractScrollArea_viewportMargins(area);
    XQ_EQ_I64(m.left, -4);
    XAbstractScrollArea_setViewportMargins(area, 0, 0, 0, 0);

    /* viewportSizeHint 缺省：视口自身 sizeHint 有效则用它
     * （qabstractscrollarea.cpp:1450-1462）。 */
    {
        XSize vpOwn;
        XSize_init(&vpOwn, 120, 80);
        XWidget_setSizeHint(viewport, &vpOwn);
        XQ_EQ_I64(XAbstractScrollArea_viewportSizeHint(area).width, 120);
        XQ_EQ_I64(XAbstractScrollArea_viewportSizeHint(area).height, 80);
    }

    /* sizeHint：AdjustIgnored ⇒ 控件缓存的缺省首选尺寸 256x192（Qt 字面
     * 常量 qabstractscrollarea.cpp:1430-1431）；改用 AdjustToContents 后
     * = 帧 + 条厚（可见时）+ viewportSizeHint。 */
    hint = XAbstractScrollArea_sizeHint(area);
    XQ_EQ_I64(hint.width, 256);
    XQ_EQ_I64(hint.height, 192);

    XAbstractScrollArea_setSizeAdjustPolicy(
        area, XAbstractScrollAreaSizeAdjustPolicy_AdjustToContents);
    XQ_EQ_I64(XAbstractScrollArea_sizeAdjustPolicy(area),
              XAbstractScrollAreaSizeAdjustPolicy_AdjustToContents);
    vpHint = XAbstractScrollArea_viewportSizeHint(area);
    XQ_EQ_I64(vpHint.width, 120);
    /* AsNeeded 且条隐藏 ⇒ 不计条厚。 */
    hint = XAbstractScrollArea_sizeHint(area);
    XQ_EQ_I64(hint.width, frame + 120);
    XQ_EQ_I64(hint.height, frame + 80);

    vbarW = XWidget_sizeHint((XWidget*)
        XAbstractScrollArea_verticalScrollBar(area)).width;
    hbarH = XWidget_sizeHint((XWidget*)
        XAbstractScrollArea_horizontalScrollBar(area)).height;
    XQ_CHECK(vbarW > 0 && hbarH > 0);
    XAbstractScrollArea_setVerticalScrollBarPolicy(area,
                                                   XScrollBarPolicy_AlwaysOn);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(area,
                                                     XScrollBarPolicy_AlwaysOn);
    hint = XAbstractScrollArea_sizeHint(area);
    XQ_EQ_I64(hint.width, frame + vbarW + 120);
    XQ_EQ_I64(hint.height, frame + hbarH + 80);

    /* minimumSizeHint = 水平条长 + 垂直条厚 + 帧宽 / 垂直条长 + 水平条厚 + 帧宽
     * （qabstractscrollarea.cpp:1405-1423）。 */
    XQ_EQ_I64(XAbstractScrollArea_minimumSizeHint(area).width,
              XWidget_sizeHint((XWidget*)
                  XAbstractScrollArea_horizontalScrollBar(area)).width +
                  vbarW + frame);
    XQ_EQ_I64(XAbstractScrollArea_minimumSizeHint(area).height,
              XWidget_sizeHint((XWidget*)
                  XAbstractScrollArea_verticalScrollBar(area)).height +
                  hbarH + frame);

    /* setupViewport 缺省为空操作（qabstractscrollarea.cpp:1498-1501）。 */
    XAbstractScrollArea_setupViewport(area, viewport);
    XAbstractScrollArea_setupViewport_base(area, viewport);

    XClassDelete((XClass*)area);
}

/* ==================== ③ 附加条控件分类过滤 + 角件 ==================== */

XQ_TEST(XAbstractScrollArea, scrollBarWidgetsAndCorner)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* leftWidget = XWidget_create(NULL, 0);
    XWidget* topWidget = XWidget_create(NULL, 0);
    XWidget* corner = XWidget_create(NULL, 0);
    XWidget* corner2 = XWidget_create(NULL, 0);
    const XWidget** list;

    XQ_REQUIRE(area != NULL);
    XQ_REQUIRE(leftWidget && topWidget && corner && corner2);
    XWidget_resize((XWidget*)area, 200, 200);

    /* 归槽口径按 Qt（qabstractscrollarea.cpp:762-785）：Left/Right 归水平
     * 条带、Top/Bottom 归垂直条带；控件被 reparent 成本控件子控件。 */
    XAbstractScrollArea_addScrollBarWidget(area, leftWidget,
                                           XAlignment_Left);
    XAbstractScrollArea_addScrollBarWidget(area, topWidget, XAlignment_Top);
    XQ_CHECK(XWidget_parentWidget(leftWidget) == (XWidget*)area);

    /* 查询按对齐分类过滤（qabstractscrollarea.cpp:787-803：Left → 水平
     * 条带左区、Top → 垂直条带上区、Right 未加过 ⇒ 空）。 */
    list = XAbstractScrollArea_scrollBarWidgets(area, XAlignment_Left);
    XQ_CHECK_PTR(list);
    XQ_CHECK(list[0] == leftWidget && list[1] == NULL);
    list = XAbstractScrollArea_scrollBarWidgets(area, XAlignment_Top);
    XQ_CHECK(list && list[0] == topWidget && list[1] == NULL);
    list = XAbstractScrollArea_scrollBarWidgets(area, XAlignment_Right);
    XQ_CHECK(list && list[0] == NULL);
    /* 组合查询：按 Left→Right→Top→Bottom 顺序拼接（本例只剩两类）。 */
    list = XAbstractScrollArea_scrollBarWidgets(
        area, XAlignment_Left | XAlignment_Top);
    XQ_CHECK(list && list[0] == leftWidget && list[1] == topWidget &&
             list[2] == NULL);

    /* 角件：setCornerWidget reparent 归本控件（qabstractscrollarea.cpp:
     * 709-728）；换件只隐藏旧件不销毁；NULL 清除。 */
    XAbstractScrollArea_setCornerWidget(area, corner);
    XQ_CHECK(XAbstractScrollArea_cornerWidget(area) == corner);
    XQ_CHECK(XWidget_parentWidget(corner) == (XWidget*)area);
    XAbstractScrollArea_setCornerWidget(area, corner2);
    XQ_CHECK(XAbstractScrollArea_cornerWidget(area) == corner2);
    XQ_CHECK(!XWidget_isVisible(corner));
    XAbstractScrollArea_setCornerWidget(area, NULL);
    XQ_CHECK(XAbstractScrollArea_cornerWidget(area) == NULL);
    XQ_CHECK(!XWidget_isVisible(corner2));

    XClassDelete((XClass*)corner);
    XClassDelete((XClass*)corner2);
    XClassDelete((XClass*)area);
}

/* ==================== ④ 视口换装（含 NULL 建缺省视口） ==================== */

XQ_TEST(XAbstractScrollArea, viewportReplacement)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* oldViewport;
    XWidget* newViewport;

    XQ_REQUIRE(area != NULL);
    oldViewport = XAbstractScrollArea_viewport(area);
    XQ_REQUIRE(oldViewport != NULL);

    /* 显式新视口：接管并 reparent、设为焦点代理，旧视口延迟回收
     * （qabstractscrollarea.cpp:491-514）。 */
    newViewport = XWidget_create(NULL, 0);
    XQ_REQUIRE(newViewport != NULL);
    XAbstractScrollArea_setViewport(area, newViewport);
    XQ_CHECK(XAbstractScrollArea_viewport(area) == newViewport);
    XQ_CHECK(XWidget_parentWidget(newViewport) == (XWidget*)area);
    XQ_CHECK(XWidget_focusProxy(newViewport) == (XWidget*)area);

    /* NULL ⇒ 新建缺省视口（Qt 明确支持；此前直接忽略）。 */
    XAbstractScrollArea_setViewport(area, NULL);
    XQ_CHECK(XAbstractScrollArea_viewport(area) != NULL);
    XQ_CHECK(XAbstractScrollArea_viewport(area) != newViewport);
    XQ_CHECK(XWidget_parentWidget(XAbstractScrollArea_viewport(area)) ==
             (XWidget*)area);

    /* 旧视口走 deleteLater：先冲刷延迟删除（旧视口届时仍是本控件子控件，
     * 晚于容器析构才冲刷会走「父析构摘表」路径，回收时机不直观）。 */
    XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    XClassDelete((XClass*)area);
}

/* ==================== ⑤ 视口事件入口 viewportEvent（gap #171） ======== */

XCLASS_DEFINE_BEGING(XQViewportEventArea)
XCLASS_DEFINE_EXTEND_END(XQViewportEventArea, XAbstractScrollArea)

/** @brief 覆盖 viewportEvent 的测试子类。 */
typedef struct XQViewportEventArea
{
    XAbstractScrollArea m_base; /**< 基类；必须是第一个。 */
    int forwarded;              /**< 未被消费的视口事件计数。 */
    int consumed;               /**< 被消费的视口事件计数。 */
} XQViewportEventArea;

/** @brief 子类视口事件入口：USER 事件消费，其余计数后交基类口径。 */
static bool xq_vpArea_viewportEvent(XAbstractScrollArea* self, XEvent* event)
{
    XQViewportEventArea* area = (XQViewportEventArea*)self;
    if (!area || !event) return false;
    if (XEvent_type(event) == XEVENT_TYPE_USER) {
        ++area->consumed;
        return true; /* 消费：不再投递给视口自身。 */
    }
    ++area->forwarded;
    return XAbstractScrollArea_viewportEvent_base(self, event);
}

XVtable* XQViewportEventArea_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQViewportEventArea)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ViewportEvent,
                             xq_vpArea_viewportEvent);
    return XVTABLE_DEFAULT;
}

static void xq_vpArea_init(XQViewportEventArea* self)
{
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, XQViewportEventArea);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
}

XQ_TEST(XAbstractScrollArea, viewportEventHook)
{
    XQViewportEventArea area;
    XWidget* viewport;
    XWidget* viewport2;
    XEvent ev;
    bool handled;
    int consumedBefore;
    int forwardedBefore;

    xq_vpArea_init(&area);
    XWidget_resize((XWidget*)&area, 200, 200);
    viewport = XAbstractScrollArea_viewport(&area.m_base);
    XQ_REQUIRE(viewport != NULL);

    /* 缺省基类口径：不消费（viewportEvent_base 恒 false）。 */
    XEvent_init(&ev, XEVENT_TYPE_USER);
    XQ_CHECK(!XAbstractScrollArea_viewportEvent_base(&area.m_base, &ev));
    XClassDeinit((XClass*)&ev);

    /* 视口事件先经 viewportEvent 虚槽（对标 QAbstractScrollAreaFilter，
     * qabstractscrollarea.cpp:1036-1077 / qabstractscrollarea_p.h）。
     * 布局期视口改尺寸等内部事件同样会经本入口（这正是「视口事件全部转给
     * 钩子」的语义），故断言取**增量**而非绝对值。 */
    consumedBefore = area.consumed;
    forwardedBefore = area.forwarded;
    XEvent_init(&ev, XEVENT_TYPE_USER);
    handled = XCoreApplication_sendEvent((XObject*)viewport, &ev);
    XQ_CHECK_MSG(handled, "viewportEvent 返回 true 时事件应被消费");
    XQ_EQ_I64(area.consumed, consumedBefore + 1);
    XQ_EQ_I64(area.forwarded, forwardedBefore);
    XClassDeinit((XClass*)&ev);

    /* 未被消费的事件继续投递给视口（返回 false 只表示「不拦」）。 */
    consumedBefore = area.consumed;
    forwardedBefore = area.forwarded;
    XEvent_init(&ev, (XEventType)((int)XEVENT_TYPE_USER + 1));
    (void)XCoreApplication_sendEvent((XObject*)viewport, &ev);
    XQ_EQ_I64(area.forwarded, forwardedBefore + 1);
    XQ_EQ_I64(area.consumed, consumedBefore);
    XClassDeinit((XClass*)&ev);

    /* setViewport 换视口后过滤器同样装到新视口（qabstractscrollarea.cpp:501）。 */
    viewport2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(viewport2 != NULL);
    XAbstractScrollArea_setViewport(&area.m_base, viewport2);
    consumedBefore = area.consumed;
    XEvent_init(&ev, XEVENT_TYPE_USER);
    handled = XCoreApplication_sendEvent((XObject*)viewport2, &ev);
    XQ_CHECK(handled);
    XQ_EQ_I64(area.consumed, consumedBefore + 1);
    XClassDeinit((XClass*)&ev);

    XClassDeinit((XClass*)&area);
}

/* ==================== ⑥ 滚动条替换迁移属性（gap #173） ================= */

XQ_TEST(XAbstractScrollArea, scrollBarReplacementMigrates)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XScrollBar* oldBar;
    XScrollBar* newBar;
    int oldMax;

    XQ_REQUIRE(area != NULL);
    XWidget_resize((XWidget*)area, 200, 200);
    XAbstractScrollArea_setContentSize(area, 400, 400);
    oldBar = XAbstractScrollArea_verticalScrollBar(area);
    XQ_REQUIRE(oldBar != NULL);
    oldMax = XScrollBar_maximum(oldBar);
    XQ_CHECK(oldMax > 0);

    XScrollBar_setValue(oldBar, 50);
    XScrollBar_setSingleStep(oldBar, 7);
    XScrollBar_setInvertedAppearance(oldBar, true);
    XScrollBar_setInvertedControls(oldBar, true);
    XScrollBar_setTracking(oldBar, false);
    XScrollBar_setSliderDown(oldBar, true);

    newBar = XScrollBar_create_2(
        (int)XAbstractSliderOrientation_Vertical, NULL, 0);
    XQ_REQUIRE(newBar != NULL);
    XAbstractScrollArea_setVerticalScrollBar(area, newBar);

    /* 对标 QAbstractScrollAreaPrivate::replaceScrollBar（qabstractscrollarea.
     * cpp:601-660）：新条接管旧条的滑块属性，旧条销毁。 */
    XQ_CHECK(XAbstractScrollArea_verticalScrollBar(area) == newBar);
    XQ_CHECK(XWidget_parentWidget((XWidget*)newBar) == (XWidget*)area);
    XQ_EQ_I64(XScrollBar_singleStep(newBar), 7);
    XQ_CHECK(XScrollBar_invertedAppearance(newBar));
    XQ_CHECK(XScrollBar_invertedControls(newBar));
    XQ_CHECK(!XScrollBar_hasTracking(newBar));
    XQ_CHECK(XScrollBar_isSliderDown(newBar));
    XQ_EQ_I64(XScrollBar_value(newBar), 50);
    /* 范围/页步由本区的排布按当前内容与视口重算（与旧条此前的值同源），
     * 迁移后再排布不改变可滚动区间。 */
    XQ_EQ_I64(XScrollBar_maximum(newBar), oldMax);

    /* 新条已接回 valueChanged → scrollContentsBy：改值不崩且仍被钳位。 */
    XScrollBar_setValue(newBar, oldMax + 100);
    XQ_EQ_I64(XScrollBar_value(newBar), oldMax);

    XClassDelete((XClass*)area); /* 新条为其子控件，级联释放。 */
}

/* ==================== ⑦ 帧宽参与视口几何与最大视口（gap #175） ========= */

XQ_TEST(XAbstractScrollArea, frameInsetsViewport)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* viewport;
    XRect vp;
    XSize maxVp;
    int fw;

    XQ_REQUIRE(area != NULL);
    XWidget_resize((XWidget*)area, 200, 200);
    /* 缺省 NoFrame ⇒ frameWidth=0（既有行为）；换成 StyledPanel 后帧宽非 0，
     * 视口与 maximumViewportSize 都必须扣帧（qabstractscrollarea.cpp:
     * 535-541 `size() - QSize(f + left + right, ...)`）。 */
    XFrame_setFrameStyle((XFrame*)area,
                         XFrameShape_StyledPanel | XFrameShadow_Sunken);
    XAbstractScrollArea_resizeEvent_base(area);
    fw = XFrame_frameWidth((const XFrame*)area);
    XQ_CHECK_MSG(fw > 0, "StyledPanel 帧宽应非 0（实得 %d）", fw);
    viewport = XAbstractScrollArea_viewport(area);
    XQ_REQUIRE(viewport != NULL);
    vp = XWidget_geometry(viewport);
    XQ_EQ_I64(vp.x, fw);
    XQ_EQ_I64(vp.y, fw);
    XQ_EQ_I64(vp.width, 200 - 2 * fw);
    XQ_EQ_I64(vp.height, 200 - 2 * fw);
    maxVp = XAbstractScrollArea_maximumViewportSize(area);
    XQ_EQ_I64(maxVp.width, 200 - 2 * fw);
    XQ_EQ_I64(maxVp.height, 200 - 2 * fw);

    /* 帧 + 视口边距叠加（Qt：size - 2f - margins）。 */
    XAbstractScrollArea_setViewportMargins(area, 4, 3, 2, 1);
    maxVp = XAbstractScrollArea_maximumViewportSize(area);
    XQ_EQ_I64(maxVp.width, 200 - 2 * fw - 6);
    XQ_EQ_I64(maxVp.height, 200 - 2 * fw - 4);

    XClassDelete((XClass*)area);
}

/* ==================== ⑧ 条容器计入最小尺寸（gap #174） ================= */

XQ_TEST(XAbstractScrollArea, scrollBarWidgetInMinimumSizeHint)
{
    XAbstractScrollArea* area = XAbstractScrollArea_create(NULL, 0);
    XWidget* leftWidget;
    XSize before;
    XSize after;
    XSize hint;

    XQ_REQUIRE(area != NULL);
    XWidget_resize((XWidget*)area, 200, 200);
    before = XAbstractScrollArea_minimumSizeHint(area);

    /* 水平条带（Left 对齐）追加一个 30px 宽的控件：对标 Qt 的
     * QAbstractScrollAreaScrollBarContainer::sizeHint（容器是
     * [左件][滚动条][右件] 线性布局，宽度相加），minimumSizeHint 的宽
     * （qabstractscrollarea.cpp:1417）随之 +30。 */
    leftWidget = XWidget_create(NULL, 0);
    XQ_REQUIRE(leftWidget != NULL);
    XWidget_resize(leftWidget, 30, 20);
    XAbstractScrollArea_addScrollBarWidget(area, leftWidget,
                                           XAlignment_Left);
    after = XAbstractScrollArea_minimumSizeHint(area);
    XQ_EQ_I64(after.width, before.width + 30);
    XQ_EQ_I64(after.height, before.height);

    /* 垂直条带（Top 对齐）的控件只加高（Qt 垂直容器高度相加）。 */
    {
        XWidget* topWidget = XWidget_create(NULL, 0);
        XQ_REQUIRE(topWidget != NULL);
        XWidget_resize(topWidget, 20, 25);
        XAbstractScrollArea_addScrollBarWidget(area, topWidget,
                                               XAlignment_Top);
        hint = XAbstractScrollArea_minimumSizeHint(area);
        XQ_EQ_I64(hint.height, after.height + 25);
        XQ_EQ_I64(hint.width, after.width);
    }

    XClassDelete((XClass*)area);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */
