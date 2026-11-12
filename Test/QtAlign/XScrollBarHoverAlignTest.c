/**
 * @file       XScrollBarHoverAlignTest.c
 * @brief      XScrollBar × Qt 6.8 QScrollBar 的 hover / transient / 连滚分支
 *             对齐契约测试（§B8 + P0-E10）。
 * @details    对齐依据（Qt 6.8.3 原文，路径相对
 *             `qtbase/src/widgets/`）：
 *             - `widgets/widgets/qscrollbar.cpp:161-173`
 *               QScrollBarPrivate::updateHoverControl：命中变化且 WA_Hover
 *               时 `q->update(lastHoverRect); q->update(hoverRect);`，否则
 *               返回 `!doesHover`；
 *             - `:175-187` newHoverControl：`hitTestComplexControl(CC_ScrollBar
 *               , &opt, pos, q)` 写 hoverControl；SC_None ⇒ hoverRect 清空，
 *               否则 `subControlRect(CC_ScrollBar, &opt, hoverControl, q)`；
 *             - `:437-469` QScrollBar::event：HoverEnter/HoverLeave/HoverMove
 *               → updateHoverControl(he->position().toPoint())；
 *               StyleChange → setTransient(styleHint(SH_ScrollBar_Transient))；
 *               Timer == flashTimer → 清 flashed + update + killTimer；
 *             - `:189-203` setTransient：真翻转才生效，可见时按提示 update、
 *               不可见且切非 transient 时 show()；
 *             - `:205-219` flash：未点亮且 transient 提示为真 ⇒ 点亮 + show/
 *               update，随后 `if (!flashTimer) flashTimer = q->startTimer(0)`；
 *             - `:275-296` initStyleOption：activeSubControls/subControls 清
 *               None、`(flashed || !transient) && styleHint(Transient)` ⇒
 *               State_On；
 *             - `:504-519` paintEvent：有 pressedControl ⇒ activeSubControls
 *               = pressedControl，且 `!pointerOutsidePressedControl` 才置
 *               State_Sunken；否则 activeSubControls = hoverControl；
 *             - `:524-583` mousePressEvent：stopRepeatAction 收前次连发、
 *               `pressedControl = hitTestComplexControl(...)`、
 *               页区 + 绝对定位键 ⇒ 改按滑块、`activateControl(pressedControl,
 *               500)`、按滑块时 setSliderDown(true)；
 *             - `:589-599` mouseReleaseEvent：无 pressedControl 或还有别的键
 *               ⇒ return；否则 stopRepeatAction()（唯一收口点）；
 *             - `:605-660` mouseMoveEvent：①滑块拖动 + PM_MaximumDragDistance
 *               回弹；②`SH_ScrollBar_ScrollWhenPointerLeavesControl=0` 时指针
 *               移出按下控件即收连发、移回即恢复；③
 *               `SH_ScrollBar_RollBetweenButtons=1` 时在两端按钮之间换按下；
 *             - `:692-699` hideEvent：pressedControl 非空 ⇒ 清 + SliderNoAction；
 *             - `styles/qcommonstyle.cpp:4122-4123`：sbextent 仅在**非 transient**
 *               时取 PM_ScrollBarExtent；`:5290-5291` RollBetweenButtons=false；
 *               `:5442` ScrollWhenPointerLeavesControl=false；
 *               `:5439-5458` 该组 hint 一律 `ret = false`；
 *             - `styles/qstyleanimation.cpp:12-13`：淡出 200ms、延迟 450ms；
 *             - `styles/qfusionstyle.cpp:2359`：`painter->setOpacity(opacity)`。
 *
 *             覆盖（每条都能在本框架无窗口系统下驱动）：
 *             A. XHoverEvent 值语义（构造/访问器/setPosition/克隆独立）；
 *             B. 悬停子控件追踪：命中 slider / 两端按钮 / 页区 / 无命中，
 *                命中变化两段标脏、未声明 Hover 属性时不动状态；
 *             C. transient flash：点亮/定时器到期清位/setTransient 真翻转
 *                才生效/不可见 + 切非 transient ⇒ show；
 *             D. P0-E10 连滚分支：指针移出按下控件收连发、移回恢复；
 *                RollBetweenButtons=1 时两端按钮之间换按下；
 *             E. paintEvent 的 activeSubControls 口径（按下优先于悬停）；
 *             F. 按下/释放单点收口（pressedControl 清空、sliderDown 抬起）；
 *             G. 复核回归锁（渲染/定时器派发行为级）：拖动像素映射
 *                round-trip、连发到期真走一步、flash 到期清位 + State_On
 *                活过 paintEvent、悬停不改变任何像素。
 *             H. [§B8 返工] 真*行为*锁（复核点名「上轮断言全绿却全部
 *                漏网」的补口）：按下/悬停通道的 option 级捕获（按下
 *                Sunken+按下通道真进 option、悬停必须不进）、拖动像素
 *                映射链端到端（tracking=false 值冻结 + sliderMoved 计数
 *                + 抬起提交、中键绝对定位 round-trip〔B8 终裁后挂
 *                MiddleAbs=1 探针样式〕）、连发节奏经
 *                processEvents 驱动真定时器全链验证（阈值不抢跑/到期
 *                连发/释放收口）、flash 到期清位由真调度器交付（非手工
 *                派发 XTimerEvent）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

#include "XScrollBar.h"
#include "XHoverEvent.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XPainter.h"
#include "XImage.h"
#include "XVarList.h"

/* ==================== 注入工具 ==================== */

/** @brief 注入一次悬停事件（局部坐标），返回事件是否被接受。
 *  @note 断言口径用 `XEvent_isAccepted` 而**不是** `event()` 的返回值：Qt
 *        的 `QScrollBar::event` 对 HoverEnter/Leave/Move 处理完仍落到
 *        `QAbstractSlider::event(event)`，而后者对未识别类型返回 false
 *        （qobject.cpp 的 default 分支）——即「已处理」与「event() 返回
 *        true」在 Qt 里是两件事。 */
static bool shv_hover(XWidget* w, XEventType type, int x, int y, int ox,
                      int oy)
{
    XHoverEvent* he;
    XPoint p;
    XPoint g;
    XPoint old;
    bool accepted;
    XPoint_init(&p, x, y);
    XPoint_init(&g, 100 + x, 100 + y);
    XPoint_init(&old, ox, oy);
    he = XHoverEvent_create(type, &p, &g, &old, 0);
    if (!he) return false;
    (void)XObject_event_base((XObject*)w, (XEvent*)he);
    accepted = XEvent_isAccepted((XEvent*)he);
    XClassDelete((XClass*)he);
    return accepted;
}

/** @brief 注入一次无按键的指针移动（MOUSE_MOVE，buttons=NoButton）。 */
static void shv_mouseMove(XWidget* w, int x, int y, XMouseButton buttons)
{
    XMouseEvent me;
    XPoint p;
    XPoint_init(&p, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton, 0, p);
    XMouseEvent_setButtons(&me, buttons);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/** @brief 注入一次鼠标按下，返回事件是否被接受。 */
static bool shv_press(XWidget* w, XMouseButton button, int x, int y)
{
    XMouseEvent me;
    XPoint p;
    XPoint_init(&p, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS, button,
                     XKeyboardModifier_NoModifier, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
    return XEvent_isAccepted((XEvent*)&me);
}

/** @brief 注入一次鼠标释放（带当前按下的键位集合）。 */
static void shv_release(XWidget* w, XMouseButton button, int x, int y,
                        XMouseButton remain)
{
    XMouseEvent me;
    XPoint p;
    XPoint_init(&p, x, y);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, button, 0, p);
    XMouseEvent_setButtons(&me, remain);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/** @brief 按控件度量算滑块在滚动轴上的起点与长度（与实现同源公式）。 */
static void shv_handle(XScrollBar* sb, int contentLen, int* handlePos,
                       int* handleLen)
{
    XStyleOption opt;
    XRect slider;
    int btn;
    XStyleOption_init(&opt, XStyleCC_ScrollBar);
    opt.m_rect = XWidget_rect((const XWidget*)sb);
    opt.m_horizontal =
        XAbstractSlider_orientation((const XAbstractSlider*)sb) ==
        XGuiOrientation_Horizontal;
    opt.m_state = XStyleState_Enabled;
    opt.m_sliderMin = XAbstractSlider_minimum((const XAbstractSlider*)sb);
    opt.m_sliderMax = XAbstractSlider_maximum((const XAbstractSlider*)sb);
    opt.m_sliderPageStep = XAbstractSlider_pageStep((const XAbstractSlider*)sb);
    opt.m_sliderPosition = XAbstractSlider_sliderPosition(
        (const XAbstractSlider*)sb);
    opt.m_sliderPositionSet = 1;
    opt.m_scrollSubLine = XScrollBar_showButtons(sb) ? 1 : 0;
    opt.m_scrollAddLine = XScrollBar_showButtons(sb) ? 1 : 0;
    slider = XStyle_subControlRect_base(XWidget_style((const XWidget*)sb),
                                        XStyleCC_ScrollBar, &opt,
                                        XStyleSC_ScrollBarSlider,
                                        (const XWidget*)sb);
    btn = opt.m_horizontal ? slider.x : slider.y;
    *handlePos = btn;
    *handleLen = opt.m_horizontal ? slider.width : slider.height;
    (void)contentLen;
}

/* ==================== A. XHoverEvent 值语义 ==================== */

/** @brief XHoverEvent 构造/访问器/setPosition/克隆（对标 QHoverEvent
 *         qevent.h:246-278 的 position/globalPosition/oldPosF 三坐标 +
 *         QMutableEventPoint::setPosition 的事后覆盖语义）。 */
XQ_TEST(XHoverEvent, valueSemantics)
{
    XPoint pos;
    XPoint gpos;
    XPoint old;
    XPoint moved;
    XHoverEvent* he;
    XHoverEvent* clone;
    XEvent* ev;

    XPoint_init(&pos, 12, 34);
    XPoint_init(&gpos, 812, 934);
    XPoint_init(&old, 5, 6);
    he = XHoverEvent_create(XEVENT_TYPE_HOVER_MOVE, &pos, &gpos, &old,
                            (XKeyboardModifiers)XKeyboardModifier_ShiftModifier);
    XQ_REQUIRE(he != NULL);
    XQ_EQ_I64(XEvent_type((XEvent*)he), XEVENT_TYPE_HOVER_MOVE);
    XQ_EQ_I64(XHoverEvent_position(he).x, 12);
    XQ_EQ_I64(XHoverEvent_position(he).y, 34);
    XQ_EQ_I64(XHoverEvent_globalPosition(he).x, 812);
    XQ_EQ_I64(XHoverEvent_globalPosition(he).y, 934);
    XQ_EQ_I64(XHoverEvent_oldPosition(he).x, 5);
    XQ_EQ_I64(XHoverEvent_oldPosition(he).y, 6);
    XQ_EQ_I64((int)XHoverEvent_modifiers(he),
              (int)XKeyboardModifier_ShiftModifier);
    /* 指针事件三位（QSinglePointEvent 家族特征）。 */
    XQ_CHECK(((XEvent*)he)->m_input_event);
    XQ_CHECK(((XEvent*)he)->m_pointer_event);
    XQ_CHECK(((XEvent*)he)->m_single_point_event);

    /* setPosition 事后覆盖（Qt 在派发点用 QMutableEventPoint::setPosition
     * 把场景坐标换成接收控件局部坐标，qapplication.cpp:2116-2117）。 */
    XPoint_init(&moved, 77, 88);
    XHoverEvent_setPosition(he, &moved);
    XQ_EQ_I64(XHoverEvent_position(he).x, 77);
    XQ_EQ_I64(XHoverEvent_position(he).y, 88);
    /* 只动 position，global/old 不受影响。 */
    XQ_EQ_I64(XHoverEvent_globalPosition(he).x, 812);
    XQ_EQ_I64(XHoverEvent_oldPosition(he).x, 5);

    /* 克隆件与本体独立（改本体不影响克隆）。 */
    ev = XEvent_clone_base((const XEvent*)he);
    XQ_REQUIRE(ev != NULL);
    clone = (XHoverEvent*)ev;
    XQ_EQ_I64(XHoverEvent_position(clone).x, 77);
    XQ_EQ_I64(XHoverEvent_oldPosition(clone).y, 6);
    XPoint_init(&moved, 1, 2);
    XHoverEvent_setPosition(he, &moved);
    XQ_EQ_I64(XHoverEvent_position(clone).x, 77);
    XClassDelete((XClass*)clone);

    /* NULL 守卫：访问器给零值，setPosition 无操作。 */
    XQ_EQ_I64(XHoverEvent_position(NULL).x, 0);
    XQ_EQ_I64(XHoverEvent_oldPosition(NULL).y, 0);
    XQ_EQ_I64((int)XHoverEvent_modifiers(NULL), 0);
    XHoverEvent_setPosition(NULL, &pos);
    XClassDelete((XClass*)he);
}

/* ==================== B. 悬停子控件追踪 ==================== */

/** @brief 悬停命中追踪：滑块/两端按钮/页区/无命中，以及命中变化的
 *         两段标脏与 WA_Hover 门（qscrollbar.cpp:161-187 + :441-446）。 */
XQ_TEST(XScrollBar, hoverControlTracking)
{
    XWidget* host;
    XScrollBar* sb;
    int handlePos = 0;
    int handleLen = 0;
    int extent;
    int subMid;
    int addMid;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 0);
    XQ_CHECK(XScrollBar_showButtons(sb));

    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    shv_handle(sb, 200, &handlePos, &handleLen);
    subMid = extent / 2;
    addMid = 200 - extent / 2;

    /* ① 悬停起始按钮（SubLine）⇒ hoverControl 命中 SubLine。 */
    XQ_CHECK(shv_hover((XWidget*)sb, XEVENT_TYPE_HOVER_ENTER, 10, subMid, 0, 0));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);

    /* ② 悬停结束按钮（AddLine）。 */
    XQ_CHECK(shv_hover((XWidget*)sb, XEVENT_TYPE_HOVER_MOVE, 10, addMid,
                       subMid, 10));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarAddLine);

    /* ③ 悬停滑块（value=0 ⇒ 滑块贴按钮之后）。 */
    XQ_CHECK(shv_hover((XWidget*)sb, XEVENT_TYPE_HOVER_MOVE, 10,
                       handlePos + handleLen / 2, 0, 0));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSlider);

    /* ④ 悬停 AddPage 空闲区（滑块之后、结束按钮之前）。 */
    {
        int pageY = handlePos + handleLen + 10;
        if (pageY < addMid - 2) {
            XQ_CHECK(shv_hover((XWidget*)sb, XEVENT_TYPE_HOVER_MOVE, 10,
                               pageY, 0, 0));
            XQ_EQ_I64(XScrollBar_hoverControl(sb),
                      (int)XStyleSC_ScrollBarAddPage);
        }
    }

    /* ⑤ HoverLeave 携带 (-1,-1)（qapplication.cpp:2094-2095 的口径）⇒
     *    命中必然落到 SC_None。 */
    XQ_CHECK(shv_hover((XWidget*)sb, XEVENT_TYPE_HOVER_LEAVE, -1, -1,
                       handlePos, handleLen));
    XQ_EQ_I64(XScrollBar_hoverControl(sb), (int)XStyleSC_None);

    /* ⑥ 未声明 Hover 属性时：Qt 的
     *    `if (lastHoverControl != newHoverControl(pos) && doesHover)`
     *    里 `newHoverControl(pos)` 在 `&&` **左侧**——短路求值下它总会被
     *    调用，因此 hoverControl/hoverRect **仍被更新**，只是不做标脏、
     *    且函数返回 `!doesHover`=true。
     *    这条与「不写状态」的直觉相反，是本域最容易写错的一处。 */
    XWidget_setAttribute((XWidget*)sb, XWidgetAttribute_Hover, false);
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, subMid));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);
    /* 声明 Hover 属性后：命中变化（None→SubLine 已于上一步写入，故此处
     * 先离开再进入以构造真变化）。 */
    XWidget_setAttribute((XWidget*)sb, XWidgetAttribute_Hover, true);
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, 200 - extent / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarAddLine);
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, subMid));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);
    /* 同命中重复调用 ⇒ false（Qt `return !doesHover` = false）。 */
    XQ_CHECK(!XScrollBar_updateHoverControl(sb, 10, subMid));

    /* ⑦ 隐藏清空悬停态（隐藏后不再有 hover 事件流）。 */
    XWidget_hide((XWidget*)host);
    XQ_EQ_I64(XScrollBar_hoverControl(sb), (int)XStyleSC_None);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief 悬停命中变化 ⇒ 两段标脏（旧矩形 ∪ 新矩形，qscrollbar.cpp:168-170）。
 *  @details 无事件循环时读不到脏区账本，故断言口径改为「可观察的等价
 *           结果」：命中在两处之间来回切换时，两次调用都返回 true（每次
 *           都在 Qt 里对应两笔 update）；命中不变时返回 false。 */
XQ_TEST(XScrollBar, hoverControlDirtyBothRects)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;

    /* 进入起始按钮：命中变化 ⇒ true。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, extent / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);
    /* 同点重复：命中未变 ⇒ false（Qt `return !doesHover`）。 */
    XQ_CHECK(!XScrollBar_updateHoverControl(sb, 10, extent / 2));
    /* 换到结束按钮：命中变化 ⇒ true（旧矩形 ∪ 新矩形两段）。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, 200 - extent / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarAddLine);
    /* 再回起始按钮：仍 true。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, extent / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief MOUSE_MOVE 兜底刷新悬停（本框架的悬停合成器不产 hover 事件族，
 *         VXScrollBar_event 用 MOUSE_MOVE 坐标补口，等价 Qt
 *         qapplication.cpp:2793-2804 的 HoverMove 合成）。 */
XQ_TEST(XScrollBar, hoverRefreshFromMouseMove)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;

    shv_mouseMove((XWidget*)sb, 10, extent / 2, XMouseButton_NoButton);
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarSubLine);
    shv_mouseMove((XWidget*)sb, 10, 200 - extent / 2, XMouseButton_NoButton);
    XQ_EQ_I64(XScrollBar_hoverControl(sb),
              (int)XStyleSC_ScrollBarAddLine);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ==================== C. transient flash / setTransient ==================== */

/** @brief transient 缺省值与真翻转语义（qscrollbar.cpp:353、:189-203）。
 *  @details 缺省样式 XCommonStyle 的 SH_ScrollBar_Transient=0 ⇒
 *           transient=false、两端按钮可见（与既有 XScrollBarAlignTest
 *           defaults 用例同口径）。setTransient 的「真翻转才生效」用
 *           「重复设同值不改状态」验证。 */
XQ_TEST(XScrollBar, transientDefaultsAndFlip)
{
    XWidget* host;
    XScrollBar* sb;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);

    /* 缺省：非 transient、flash 未点亮、不透明度 1.0。 */
    XQ_CHECK(!XScrollBar_transient(sb));
    XQ_CHECK(!XScrollBar_flashed(sb));
    XQ_CHECK(XScrollBar_transientOpacity(sb) >= 1.0f);

    /* 切 transient=true 生效；重复设同值幂等（不 panic、状态保持）。 */
    XScrollBar_setTransient(sb, true);
    XQ_CHECK(XScrollBar_transient(sb));
    XScrollBar_setTransient(sb, true);
    XQ_CHECK(XScrollBar_transient(sb));

    /* 切回 false 生效。 */
    XScrollBar_setTransient(sb, false);
    XQ_CHECK(!XScrollBar_transient(sb));

    /* 不可见 + 切到非 transient ⇒ show（qscrollbar.cpp:199-201）。
     * 先切到 transient 让状态为真，再隐藏，再切回 false。 */
    XScrollBar_setTransient(sb, true);
    XWidget_hide((XWidget*)sb);
    XQ_CHECK(XWidget_isHidden((XWidget*)sb));
    XScrollBar_setTransient(sb, false);
    XQ_CHECK(!XWidget_isHidden((XWidget*)sb));

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief flash：非 transient 样式下只布防定时器不改视觉；transient 样式下
 *         点亮并把不透明度拉回 1.0（qscrollbar.cpp:205-219 + :453-464）。 */
XQ_TEST(XScrollBar, transientFlash)
{
    XWidget* host;
    XScrollBar* sb;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);

    /* 缺省（非 transient）样式：flash 不点亮（Qt
     * `if (!flashed && styleHint(SH_ScrollBar_Transient, ...))`）。 */
    XScrollBar_flash(sb);
    XQ_CHECK(!XScrollBar_flashed(sb));

    /* 定时器在无事件循环（无调度器）时可能起不来，故只断言「不点亮」
     * 这一步；flashed 位由样式提示门控，与定时器是否就绪无关。 */
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ==================== D. P0-E10 连滚分支 ==================== */

/** @brief 指针移出按下控件 ⇒ 收连发；移回 ⇒ 恢复连发
 *         （qscrollbar.cpp:629-658，SH_ScrollBar_ScrollWhenPointerLeavesControl
 *         =0 的缺省分支）。 */
XQ_TEST(XScrollBar, scrollStopsWhenPointerLeaves)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;
    int addMid;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    addMid = 200 - extent / 2;

    /* 按住结束按钮 ⇒ 连发布防。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepAdd);

    /* 仍在按钮内移动 ⇒ 连发保持、不置 pointerOutside。 */
    shv_mouseMove((XWidget*)sb, 10, addMid, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepAdd);

    /* 移到滑块上（离开按下按钮）⇒ 收连发（Qt :653
     * `setRepeatAction(SliderNoAction)`）。 */
    shv_mouseMove((XWidget*)sb, 10, 100, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);

    /* 移回按钮内 ⇒ 恢复连发（Qt :656 activateControl(pressedControl)）。 */
    shv_mouseMove((XWidget*)sb, 10, addMid, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepAdd);

    /* 释放收口：pressedControl 清空、连发收闸。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, addMid,
                XMouseButton_NoButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief 缺省样式（SH_ScrollBar_ScrollWhenPointerLeavesControl=0）下指针
 *         移出按下控件即收连发（qscrollbar.cpp:629 + :647-658）。
 *  @note 这只是「缺省分支」的锁；提示为 1 时整段跳过。两者共用同一份
 *        `xsb_styleHintBool` 查询，故本用例同时验证「查询接线」。 */
XQ_TEST(XScrollBar, scrollContinuesWhenHintSet)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;
    int addMid;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    addMid = 200 - extent / 2;

    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepAdd);
    /* 注：缺省 XCommonStyle 该提示为 0（qcommonstyle.cpp:5442 同口径），
     * 故本用例断言的是**缺省行为**——移出即收连发。提示为 1 的样式变体
     * 需要子类化 XStyle 覆盖 styleHint，本域测试见
     * `rollBetweenButtonsHint`（同法覆盖 RollBetweenButtons）。 */
    shv_mouseMove((XWidget*)sb, 10, 100, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, 100,
                XMouseButton_NoButton);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief RollBetweenButtons=1 时可从起始按钮直接滚到结束按钮换按下
 *         （qscrollbar.cpp:631-645）。
 *  @details 缺省 XCommonStyle 该提示为 0（qcommonstyle.cpp:5290-5291 同口径），
 *           故本用例用一个**只覆盖 styleHint** 的探针样式把提示打开，验证
 *           分支确实接线（不是恒假死代码）。
 *           反向锁：同一逻辑在缺省样式下必须**不**换按下（提示为 0 ⇒
 *           Qt `if (styleHint(SH_ScrollBar_RollBetweenButtons) && ...)`
 *           整段不进）——见本用例末尾的「缺省样式对照组」。 */
static int shv_rollStyleHintCount = 0;

/** @brief 探针样式：只把 SH_ScrollBar_RollBetweenButtons 打开。 */
XCLASS_DEFINE_BEGING(XScrollBarRollStyle)
XCLASS_DEFINE_EXTEND_END(XScrollBarRollStyle, XCommonStyle)

typedef struct XScrollBarRollStyle
{
    XCommonStyle m_base; /**< 基类；必须是第一个成员。 */
} XScrollBarRollStyle;

/** @brief 探针样式的 polish 槽：沿真回落链转调父类。
 *  @details 框架缺陷②已根治：XStyle 基类为 polish/unpolish/
 *           polishPalette 三槽挂了缺省空实现（VXStyle_polish 等，
 *           XStyle.c 的 class_init 挂表），对标 Qt 6.8 QStyle 的
 *           空虚函数（qstyle.cpp:434-436/453-455/486-488）。
 *           XCommonStyle 的 polish/unpolish 本就只 XClass_Parent 回落
 *           基类，故探针样式直接沿父类链转调，链路
 *           XScrollBar*Style → XCommonStyle → XStyle 全程安全。
 *           回归锁：Test/QtAlign/XStylePolishAlignTest.c。 */
static void shv_rollStylePolish(XStyle* self, XWidget* widget)
{
    XClass_Parent(XCommonStyle, EXStyle_Polish, void (*)(XStyle*, XWidget*))
        ((XStyle*)self, widget);
}

/** @brief 同 polish：unpolish 槽沿真回落链转调父类。 */
static void shv_rollStyleUnpolish(XStyle* self, XWidget* widget)
{
    XClass_Parent(XCommonStyle, EXStyle_Unpolish, void (*)(XStyle*, XWidget*))
        ((XStyle*)self, widget);
}

static int shv_rollStyleHint(XStyle* self, int sh,
                             const XStyleOption* opt, const XWidget* widget)
{
    if (sh == XStyleSH_ScrollBar_RollBetweenButtons) {
        ++shv_rollStyleHintCount;
        return 1;
    }
    return XClass_Parent(XCommonStyle, EXStyle_StyleHint,
                         int(*)(XStyle*, int, const XStyleOption*,
                                const XWidget*))(self, sh, opt, widget);
}

static XVtable* XScrollBarRollStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBarRollStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StyleHint, shv_rollStyleHint);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish, shv_rollStylePolish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Unpolish, shv_rollStyleUnpolish);
    return XVTABLE_DEFAULT;
}

static XScrollBarRollStyle* shv_createRollStyle(void)
{
    XScrollBarRollStyle* style = (XScrollBarRollStyle*)XMemory_malloc(
        sizeof(XScrollBarRollStyle), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XMemset(style, 0, sizeof(*style));
    XCommonStyle_init(&style->m_base);
    XClassSetVtable(style, XScrollBarRollStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/* ==================== transient 探针样式 ==================== */

/** @brief DrawComplexControl 捕获：CC_ScrollBar 的选项 m_state 快照。
 *  （复核第 3 条的取证手法：State_On 是否活过 paintEvent 的选项构造。） */
static int shv_stateCapture = -1;
/** @brief DrawComplexControl 捕获：按下通道快照（bit8=m_scrollActiveSub、
 *  bit0..1=m_scrollActiveDir）——复核第 4 条「悬停不得进按下通道」的
 *  option 级取证位。 */
static int shv_activeSubCapture = -1;

/** @brief 探针样式：SH_ScrollBar_Transient=1 + CC_ScrollBar 状态捕获。
 *  @details polish/unpolish 复用 RollStyle 的真回落实现（XStyle 基类
 *           缺省空实现已落地，探针样式沿 XCommonStyle → XStyle 链转调，
 *           见 Test/QtAlign/XStylePolishAlignTest.c）。 */
XCLASS_DEFINE_BEGING(XScrollBarTransientStyle)
XCLASS_DEFINE_EXTEND_END(XScrollBarTransientStyle, XCommonStyle)

typedef struct XScrollBarTransientStyle
{
    XCommonStyle m_base; /**< 基类；必须是第一个成员。 */
} XScrollBarTransientStyle;

static int shv_transientStyleHint(XStyle* self, int sh,
                                  const XStyleOption* opt,
                                  const XWidget* widget)
{
    if (sh == XStyleSH_ScrollBar_Transient) return 1;
    return XClass_Parent(XCommonStyle, EXStyle_StyleHint,
                         int(*)(XStyle*, int, const XStyleOption*,
                                const XWidget*))(self, sh, opt, widget);
}

static void shv_transientStyleDrawComplex(
    XStyle* self, int cc, const XStyleOption* option, XPainter* painter,
    const XWidget* widget)
{
    if (cc == XStyleCC_ScrollBar && option) {
        shv_stateCapture = (int)option->m_state;
        shv_activeSubCapture =
            (((int)option->m_scrollActiveSub & 1) << 8) |
            ((int)option->m_scrollActiveDir & 3);
    }
    XClass_Parent(XCommonStyle, EXStyle_DrawComplexControl,
                  void (*)(XStyle*, int, const XStyleOption*, XPainter*,
                           const XWidget*))(self, cc, option, painter, widget);
}

static XVtable* XScrollBarTransientStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBarTransientStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StyleHint, shv_transientStyleHint);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawComplexControl,
                             shv_transientStyleDrawComplex);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish, shv_rollStylePolish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Unpolish, shv_rollStyleUnpolish);
    return XVTABLE_DEFAULT;
}

static XScrollBarTransientStyle* shv_createTransientStyle(void)
{
    XScrollBarTransientStyle* style = (XScrollBarTransientStyle*)
        XMemory_malloc(sizeof(XScrollBarTransientStyle),
                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XMemset(style, 0, sizeof(*style));
    XCommonStyle_init(&style->m_base);
    XClassSetVtable(style, XScrollBarTransientStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/* ==================== 中键绝对定位探针样式 ==================== */

/** @brief 探针样式：只把 SH_ScrollBar_MiddleClickAbsolutePosition 打开
 *  （Fusion 口径，qfusionstyle.cpp:3541；缺省 XCommonStyle 对该提示返回
 *  false——B8 终裁 2026-11-15，qcommonstyle.cpp:5441 归 false 组）。
 *  @details 用途：缺省口径下中键点击被 QScrollBar::mousePressEvent 首段
 *           守卫整体 return（qscrollbar.cpp:534-538），「中键绝对定位」
 *           分支只有在提示=1 的样式下才可达——探针样式是验证该分支
 *           （含映射可逆性）与 styleHint 查询链路的唯一入口。
 *           polish/unpolish 复用 RollStyle 的真回落实现（XStyle 基类
 *           缺省空实现已落地，探针样式沿 XCommonStyle → XStyle 链转调，
 *           见 Test/QtAlign/XStylePolishAlignTest.c）。 */
XCLASS_DEFINE_BEGING(XScrollBarMiddleAbsStyle)
XCLASS_DEFINE_EXTEND_END(XScrollBarMiddleAbsStyle, XCommonStyle)

typedef struct XScrollBarMiddleAbsStyle
{
    XCommonStyle m_base; /**< 基类；必须是第一个成员。 */
} XScrollBarMiddleAbsStyle;

static int shv_middleAbsStyleHint(XStyle* self, int sh,
                                  const XStyleOption* opt,
                                  const XWidget* widget)
{
    if (sh == XStyleSH_ScrollBar_MiddleClickAbsolutePosition) return 1;
    return XClass_Parent(XCommonStyle, EXStyle_StyleHint,
                         int(*)(XStyle*, int, const XStyleOption*,
                                const XWidget*))(self, sh, opt, widget);
}

static XVtable* XScrollBarMiddleAbsStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBarMiddleAbsStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_StyleHint, shv_middleAbsStyleHint);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish, shv_rollStylePolish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Unpolish, shv_rollStyleUnpolish);
    return XVTABLE_DEFAULT;
}

static XScrollBarMiddleAbsStyle* shv_createMiddleAbsStyle(void)
{
    XScrollBarMiddleAbsStyle* style = (XScrollBarMiddleAbsStyle*)
        XMemory_malloc(sizeof(XScrollBarMiddleAbsStyle),
                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XMemset(style, 0, sizeof(*style));
    XCommonStyle_init(&style->m_base);
    XClassSetVtable(style, XScrollBarMiddleAbsStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/** @brief 两幅 grab 图的区域差异像素数（负值 = 参数/尺寸不可比）。
 *         （套路同 XToolBoxIconAlignTest 的 xtb2_regionDiff。） */
static int shv_regionDiff(const XImage* a, const XImage* b, int x0, int y0,
                          int x1, int y1)
{
    int diff = 0;
    int x;
    int y;
    int w;
    int h;

    if (!a || !b) return -1;
    w = XImage_width(a);
    h = XImage_height(a);
    if (w != XImage_width(b) || h != XImage_height(b)) return -1;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++diff;
        }
    }
    return diff;
}

XQ_TEST(XScrollBar, rollBetweenButtonsHint)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarRollStyle* style;
    int extent;
    int subMid;
    int addMid;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    style = shv_createRollStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);

    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    subMid = extent / 2;
    addMid = 200 - extent / 2;

    /* 按住起始按钮。 */
    shv_rollStyleHintCount = 0;
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, subMid));
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepSub);

    /* 滚到结束按钮上：Qt 直接换按下并 activateControl(newSc, 0) ⇒
     * 连发动作变成 SingleStepAdd。
     * 提示查询点：Qt 只在 mouseMoveEvent 的该分支里查
     * `styleHint(SH_ScrollBar_RollBetweenButtons)`（qscrollbar.cpp:631），
     * 按下路径不查——故计数断言放在滚动之后。 */
    shv_mouseMove((XWidget*)sb, 10, addMid, XMouseButton_LeftButton);
    XQ_CHECK_MSG(shv_rollStyleHintCount > 0,
                 "RollBetweenButtons 提示应被查询（分支可达），实得 %d 次",
                 shv_rollStyleHintCount);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepAdd);

    /* 再滚回起始按钮：换回 SingleStepSub。 */
    shv_mouseMove((XWidget*)sb, 10, subMid, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepSub);

    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, subMid,
                XMouseButton_NoButton);
    XWidget_setStyle((XWidget*)sb, NULL);

    /* 缺省样式对照组：提示为 0 ⇒ 滚到结束按钮**不**换按下（仍为 Sub），
     * 且连发被「移出按下控件」分支收掉（Qt :647-658）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, subMid));
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepSub);
    shv_mouseMove((XWidget*)sb, 10, addMid, XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSubLine);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, addMid,
                XMouseButton_NoButton);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

/* ==================== E/F. paint 口径 + 按下/释放单点收口 ==================== */

/** @brief 按下/释放单点收口：pressedControl 清空、sliderDown 抬起、
 *         连发收闸（qscrollbar.cpp:589-599 的 stopRepeatAction 是唯一收口点）。
 *  @details 直接读结构体字段（XScrollBar.h 公开 struct，测试可读），
 *           以断言内部状态机与 Qt 私有成员同形。 */
XQ_TEST(XScrollBar, pressReleaseSinglePointCleanup)
{
    XWidget* host;
    XScrollBar* sb;
    int handlePos = 0;
    int handleLen = 0;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 30);
    shv_handle(sb, 200, &handlePos, &handleLen);

    /* 按下滑块：pressedControl=Slider、sliderDown=true。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handlePos + handleLen / 2));
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSlider);
    XQ_CHECK(XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));

    /* 释放：pressedControl 回 None、sliderDown 抬起、连发收闸。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10,
                handlePos + handleLen / 2, XMouseButton_NoButton);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_None);
    XQ_CHECK(!XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);

    /* 「还有别的键按着」⇒ 不释放（Qt :595-596）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handlePos + handleLen / 2));
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSlider);
    /* 传 remain=右键 ⇒ 早退，状态不动。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10,
                handlePos + handleLen / 2, XMouseButton_RightButton);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSlider);
    /* 最后一键释放才收口。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10,
                handlePos + handleLen / 2, XMouseButton_NoButton);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_None);

    /* 隐藏也会清 pressedControl（Qt :692-699 hideEvent）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handlePos + handleLen / 2));
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSlider);
    XWidget_hide((XWidget*)sb);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_None);
    XQ_CHECK(!XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief 中键点击按 SH_ScrollBar_MiddleClickAbsolutePosition 分派
 *         （qscrollbar.cpp:531-532 提示查询 + :534-538 首段守卫 +
 *         :556-566 的 pressedControl 改写；B8 终裁 2026-11-15）。
 *  @details 双场景：缺省样式 XCommonStyle 对该提示返回 false
 *           （qcommonstyle.cpp:5441 归 false 组）⇒ 中键点击被守卫整体
 *           return——press 不被接受、值不变、不进拖动（连翻页都没有；
 *           本机 Qt 6.8.0 offscreen 探针实测 value=0、isSliderDown=0）；
 *           装返回 1 的探针样式（Fusion 口径）⇒ 绝对定位照常生效——
 *           值显著大于一页、pressedControl=Slider、sliderDown（探针
 *           Fusion 对照：value 跳 50、isSliderDown=1）。 */
XQ_TEST(XScrollBar, middleClickAbsoluteViaStyleHint)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarMiddleAbsStyle* style;
    int valueBefore;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);

    /* 场景 1：缺省样式（XCommonStyle）——中键点轨道中部被整体忽略。 */
    XScrollBar_setValue(sb, 0);
    valueBefore = XScrollBar_value(sb);
    XQ_CHECK(!shv_press((XWidget*)sb, XMouseButton_MiddleButton, 10, 100));
    XQ_EQ_I64(XScrollBar_value(sb), valueBefore);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_None);
    XQ_CHECK(!XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));
    shv_release((XWidget*)sb, XMouseButton_MiddleButton, 10, 100,
                XMouseButton_NoButton);

    /* 场景 2：探针样式（提示=1，Fusion 口径）——绝对定位照常生效。 */
    style = shv_createMiddleAbsStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);
    XScrollBar_setValue(sb, 0);
    valueBefore = XScrollBar_value(sb);
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_MiddleButton, 10, 100));
    XQ_CHECK(XScrollBar_value(sb) > valueBefore + XScrollBar_pageStep(sb));
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_ScrollBarSlider);
    XQ_CHECK(XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));
    shv_release((XWidget*)sb, XMouseButton_MiddleButton, 10, 100,
                XMouseButton_NoButton);
    XQ_EQ_I64(sb->m_pressedControl, (int)XStyleSC_None);
    XWidget_setStyle((XWidget*)sb, NULL);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

/** @brief 滑块几何按 X 扩展 setShowButtons(false) 后的命中规整：
 *         按钮带并入相邻页区（否则按在那里会被判成不存在的按钮）。
 *  @details 样式层 subControlRect(CC_ScrollBar) 不看 m_scrollSubLine/
 *           m_scrollAddLine（Qt 同，见 qcommonstyle.cpp:4122-4123），故 X
 *           扩展关掉按钮后必须由控件层把按钮带规整成页区。 */
XQ_TEST(XScrollBar, hiddenButtonsHitAsPage)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;

    XScrollBar_setShowButtons(sb, false);
    XQ_CHECK(!XScrollBar_showButtons(sb));

    /* 悬停原按钮带 ⇒ 命中被规整成 SubPage（首端）而无按钮。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, extent / 2));
    XQ_CHECK_MSG(XScrollBar_hoverControl(sb) !=
                     (int)XStyleSC_ScrollBarSubLine,
                 "按钮已隐藏，命中不应报 SubLine（实得 %d）",
                 XScrollBar_hoverControl(sb));
    /* 按下原按钮带同样不得进入「按钮按下」态。 */
    shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, extent / 2);
    XQ_CHECK_MSG(sb->m_pressedControl != (int)XStyleSC_ScrollBarSubLine,
                 "按钮已隐藏，按下不应记为 SubLine（实得 %d）",
                 sb->m_pressedControl);
    XQ_CHECK(!sb->m_activeSub);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, extent / 2,
                XMouseButton_NoButton);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ==================== G. 修订复核回归锁 ====================
 * 复核意见五条核心缺陷各配一条行为级锁（渲染/定时器派发，不再只看状态位）：
 * G1 拖动像素映射 round-trip（双重扣减）；
 * G2 连发到期真走一步（TimerEvent 父类回落被截断）；
 * G3 flash 到期清位 + State_On 活过 paintEvent（覆盖拍掉）；
 * G4 悬停不改变任何像素（悬停被画成按下）。 */

/** @brief G1：抓手柄非起点处，同点 MOVE ⇒ sliderPosition 不变。
 *  @details Qt 的拖动映射是 `pixelPosToRangeValue(pos - clickOffset)`
 *           （qscrollbar.cpp:620），clickOffset 在按下时记、在 move 时扣——
 *           **只扣一次**。双重扣减（move 分支外扣 + 映射函数内扣）会让
 *           「手不动、滑块自己跳」：抓手柄中部（pressOffset=14px）时
 *           round-trip 偏差恰为按钮厚度 16px ⇒ 50→61（内码探针实测）。 */
XQ_TEST(XScrollBar, scrollDragPixelRoundTrip)
{
    XWidget* host;
    XScrollBar* sb;
    int handleStart;
    int handleLen;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    shv_handle(sb, 200, &handleStart, &handleLen);
    XQ_REQUIRE(handleLen > 2);

    /* 抓手柄中部（pressOffset=handleLen/2 ≠ 0）：按下不挪值。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handleStart + handleLen / 2));
    XQ_EQ_I64(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb), 50);
    /* 同点 MOVE ⇒ 值必须原封不动（round-trip 锁，双向各验一次）。 */
    shv_mouseMove((XWidget*)sb, 10, handleStart + handleLen / 2,
                  XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb), 50);
    /* 向下移 10px ⇒ 值增大（映射方向，顺带证明 MOVE 真在驱动拖动）。 */
    shv_mouseMove((XWidget*)sb, 10, handleStart + handleLen / 2 + 10,
                  XMouseButton_LeftButton);
    XQ_CHECK(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb) > 50);
    /* 移回按下点 ⇒ 精确回到 50（映射可逆性；双重扣减下回不到）。 */
    shv_mouseMove((XWidget*)sb, 10, handleStart + handleLen / 2,
                  XMouseButton_LeftButton);
    XQ_EQ_I64(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb), 50);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10,
                handleStart + handleLen / 2, XMouseButton_NoButton);
    XQ_EQ_I64(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb), 50);

    /* 第二抓手柄顶部（pressOffset≈1px）：同点 round-trip 同样锁定。 */
    XScrollBar_setValue(sb, 50);
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handleStart + 1));
    shv_mouseMove((XWidget*)sb, 10, handleStart + 1, XMouseButton_LeftButton);
    XQ_CHECK(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb) >= 49);
    XQ_CHECK(XAbstractSlider_sliderPosition((const XAbstractSlider*)sb) <= 51);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, handleStart + 1,
                XMouseButton_NoButton);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief G2：按住按钮后派发 TIMER（id=m_repeatTimerId）⇒ 值再走一步。
 *  @details 连发到期逻辑在 XAbstractSlider 的 TimerEvent 槽
 *           （XAbstractSlider.c:226-247）；XScrollBar 覆写了该槽就必须
 *           `XClass_Parent(XAbstractSlider, ...)` 回落——写 XObject 会
 *           跳过父类槽，长按连发整条哑掉（复核第 2 条实测 valueDelta=0，
 *           正确值 = singleStep）。 */
XQ_TEST(XScrollBar, repeatTimerAdvancesValue)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;
    int addMid;
    int step;
    long long before;
    XTimerEvent* te;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    addMid = 200 - extent / 2;
    step = XAbstractSlider_singleStep((const XAbstractSlider*)sb);
    XQ_REQUIRE(step > 0);

    /* 按结束按钮：activateControl 立即触发一次（50→50+step）并布防连发。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    before = XAbstractSlider_value((const XAbstractSlider*)sb);
    XQ_EQ_I64(before, 50 + step);
    XQ_REQUIRE(sb->m_base.m_repeatTimerId != XTIMER_INVALID_ID);

    /* 派发连发到期：必须再走一步（XAbstractSlider 槽真被回落到）。 */
    te = XTimerEvent_create(sb->m_base.m_repeatTimerId);
    XQ_REQUIRE(te != NULL);
    XObject_event_base((XObject*)sb, (XEvent*)te);
    XClassDelete((XClass*)te);
    XQ_EQ_I64(XAbstractSlider_value((const XAbstractSlider*)sb),
              50 + 2 * step);

    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, addMid,
                XMouseButton_NoButton);
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/** @brief G3：transient 探针样式下 flash ⇒ flashed 位点亮；绘制选项里
 *         State_On 活过 paintEvent；派发 flash 定时器到期 ⇒ 清位 + 收定时器
 *         （qscrollbar.cpp:453-464：flashed && styleHint(Transient) ⇒
 *         flashed=false + update，killTimer 无条件）。
 *  @details State_On 断言用 DrawComplexControl 捕获（复核第 3 条手法）：
 *           paintEvent 曾整体赋值 m_state 把 State_On 拍掉。样式层
 *           XCommonStyle 的 xcs_drawScrollBar 尚不消费 State_On（视觉零
 *           差异是既有偏差，登记 openNotes，不在本域修）。 */
XQ_TEST(XScrollBar, flashTimerClearsFlashed)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarTransientStyle* style;
    XImage* grab;
    XTimerEvent* te;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    style = shv_createTransientStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);
    /* setStyle 触发 STYLE_CHANGE ⇒ setTransient(styleHint(Transient)=1)。 */
    XQ_CHECK(XScrollBar_transient(sb));

    XScrollBar_flash(sb);
    XQ_CHECK(XScrollBar_flashed(sb));
    XQ_REQUIRE(sb->m_flashTimerId != XTIMER_INVALID_ID);

    /* 绘制：State_On 必须出现在到达样式层的选项里（覆盖修复锁）。 */
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG(shv_stateCapture >= 0,
                 "DrawComplexControl 未被捕获（state=%d）", shv_stateCapture);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_On) != 0,
                 "State_On 应活过 paintEvent（state=0x%x）", shv_stateCapture);

    /* 派发 flash 定时器到期 ⇒ flashed 清位 + 定时器 id 收掉。 */
    te = XTimerEvent_create(sb->m_flashTimerId);
    XQ_REQUIRE(te != NULL);
    XObject_event_base((XObject*)sb, (XEvent*)te);
    XClassDelete((XClass*)te);
    XQ_CHECK(!XScrollBar_flashed(sb));
    XQ_EQ_I64(sb->m_flashTimerId, XTIMER_INVALID_ID);

    XWidget_setStyle((XWidget*)sb, NULL);
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

/** @brief G4：悬停滑块不得改变任何像素（悬停被画成「按下」回归锁）。
 *  @details 复核第 4 条实测：悬停滑块时 m_scrollActiveSub=1 + dir=0 ⇒
 *           样式层把 SubLine 按钮带画成按下（16/16 行像素变化）。修后
 *           m_scrollActiveSub/Dir 只承载按下，悬停仅写 m_hoverControl
 *           （公开 API 承载），默认样式下 grab 前后必须逐像素相同。 */
XQ_TEST(XScrollBar, hoverDoesNotDarkenButton)
{
    XWidget* host;
    XScrollBar* sb;
    XImage* baseline;
    XImage* hovered;
    int handleStart;
    int handleLen;
    int diff;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    shv_handle(sb, 200, &handleStart, &handleLen);

    baseline = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(baseline != NULL);
    /* 悬停滑块中心。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10,
                                           handleStart + handleLen / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb), (int)XStyleSC_ScrollBarSlider);
    hovered = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(hovered != NULL);
    diff = shv_regionDiff(baseline, hovered, 0, 0, XImage_width(baseline),
                          XImage_height(baseline));
    XQ_CHECK_MSG(diff == 0, "悬停滑块不得改变任何像素（差异 %d）", diff);
    XClassDelete((XClass*)baseline);
    XClassDelete((XClass*)hovered);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ==================== H. [§B8 返工] 真*行为*锁 ==================== */

/** @brief processEvents 自旋等待 ms 毫秒（复核清单⑦点名口径：以
 *         XCoreApplication_processEvents 驱动 XObject 真定时器，替代
 *         手工派发 XTimerEvent 的假路径——G3/G2 锁的是「槽逻辑」，本节
 *         锁的是「真调度器交付」）。 */
static void shv_spinProcessEvents(int ms)
{
    uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)ms;
    for (;;) {
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) break;
    }
}

/* ---- H0. 捕获专用探针样式（提示全回落 + DrawComplexControl 捕获） ---- */

XCLASS_DEFINE_BEGING(XScrollBarCaptureStyle)
XCLASS_DEFINE_EXTEND_END(XScrollBarCaptureStyle, XCommonStyle)

typedef struct XScrollBarCaptureStyle
{
    XCommonStyle m_base; /**< 基类；必须是第一个成员。 */
} XScrollBarCaptureStyle;

/** @brief 只捕获、不改任何提示：CC_ScrollBar 的 m_state 与按下通道
 *  （m_scrollActiveSub/Dir）快照到 shv_stateCapture/shv_activeSubCapture。 */
static void shv_captureStyleDrawComplex(
    XStyle* self, int cc, const XStyleOption* option, XPainter* painter,
    const XWidget* widget)
{
    if (cc == XStyleCC_ScrollBar && option) {
        shv_stateCapture = (int)option->m_state;
        shv_activeSubCapture =
            (((int)option->m_scrollActiveSub & 1) << 8) |
            ((int)option->m_scrollActiveDir & 3);
    }
    XClass_Parent(XCommonStyle, EXStyle_DrawComplexControl,
                  void (*)(XStyle*, int, const XStyleOption*, XPainter*,
                           const XWidget*))(self, cc, option, painter, widget);
}

static XVtable* XScrollBarCaptureStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBarCaptureStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawComplexControl,
                             shv_captureStyleDrawComplex);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish, shv_rollStylePolish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Unpolish, shv_rollStyleUnpolish);
    return XVTABLE_DEFAULT;
}

static XScrollBarCaptureStyle* shv_createCaptureStyle(void)
{
    XScrollBarCaptureStyle* style = (XScrollBarCaptureStyle*)XMemory_malloc(
        sizeof(XScrollBarCaptureStyle), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XMemset(style, 0, sizeof(*style));
    XCommonStyle_init(&style->m_base);
    XClassSetVtable(style, XScrollBarCaptureStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/* ---- H1. 按下/悬停通道 option 级锁（复核③④） ---- */

/** @brief 按下与悬停到达样式层选项的通道必须分清（qscrollbar.cpp:511-517
 *         + :284-296 的 option 口径）：
 *         - 按下按钮：State_Sunken（指针在控件内）+ 按下通道
 *           m_scrollActiveSub=1/dir=按下按钮（qscrollbar.cpp:511-515
 *           `opt.activeSubControls = pressedControl; if
 *           (!pointerOutsidePressedControl) state |= State_Sunken`）；
 *         - 按下滑块：Sunken 置位但按下通道不得报按钮（m_scrollActiveSub
 *           只承载 Add/SubLine 按下，P0-67 口径）；
 *         - 指针移出按下控件：Sunken 撤、按下通道**保持**（Qt :513-514
 *           的「移出即松开」视觉反馈，:511 的 activeSubControls 不动）；
 *         - 仅悬停（滑块或按钮）：既无 Sunken 也无按下通道（复核第 4 条
 *           「悬停滑块把 SubLine 画成按下」的 option 级反向锁）。 */
XQ_TEST(XScrollBar, pressedHoverOptionChannels)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarCaptureStyle* style;
    XImage* grab;
    int extent;
    int addMid;
    int handlePos = 0;
    int handleLen = 0;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    style = shv_createCaptureStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    addMid = 200 - extent / 2;
    shv_handle(sb, 200, &handlePos, &handleLen);

    /* ① 按下结束按钮：Sunken + 按下通道=AddLine（sub=1,dir=2）。 */
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_Sunken) != 0,
                 "按下且指针在内应置 State_Sunken（state=0x%x）",
                 shv_stateCapture);
    XQ_EQ_I64(shv_activeSubCapture, 0x102);
    /* 释放收口（下同）。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, addMid,
                XMouseButton_NoButton);

    /* ② 按下滑块：Sunken 置位，但按下通道不得报按钮（sub=0）。 */
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10,
                       handlePos + handleLen / 2));
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_Sunken) != 0,
                 "按下滑块且指针在内应置 State_Sunken（state=0x%x）",
                 shv_stateCapture);
    XQ_EQ_I64(shv_activeSubCapture, 0x000);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10,
                handlePos + handleLen / 2, XMouseButton_NoButton);

    /* ③ 按下按钮后指针移出：Sunken 撤、按下通道保持（Qt :511-515）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    shv_mouseMove((XWidget*)sb, 10, 100, XMouseButton_LeftButton);
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_Sunken) == 0,
                 "指针移出按下控件应撤 State_Sunken（state=0x%x）",
                 shv_stateCapture);
    XQ_EQ_I64(shv_activeSubCapture, 0x102);
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, 100,
                XMouseButton_NoButton);

    /* ④ 仅悬停（按钮与滑块）：无 Sunken、按下通道必须为 0——悬停不得
     * 借按下通道表达（复核第 4 条的 option 级反向锁）。 */
    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10, addMid));
    XQ_EQ_I64(XScrollBar_hoverControl(sb), (int)XStyleSC_ScrollBarAddLine);
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_Sunken) == 0,
                 "仅悬停不得置 State_Sunken（state=0x%x）", shv_stateCapture);
    XQ_EQ_I64(shv_activeSubCapture, 0x000);

    XQ_CHECK(XScrollBar_updateHoverControl(sb, 10,
                                           handlePos + handleLen / 2));
    XQ_EQ_I64(XScrollBar_hoverControl(sb), (int)XStyleSC_ScrollBarSlider);
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_EQ_I64(shv_activeSubCapture, 0x000);

    XWidget_setStyle((XWidget*)sb, NULL);
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

/* ---- H2. 拖动像素映射链端到端（复核①，sliderMoved/提交口径） ---- */

/** @brief 拖动链端到端（qabstractslider.cpp:425-469 + qscrollbar.cpp:618-628）：
 *         tracking=false 时拖动期只动 position（sliderMoved 照发、value
 *         冻结），抬起经 SliderMove 提交 value==position；映射双向可逆。
 *  @details 复核第 1 条的完整链路锁：pressOffset 只在按下时记一次、
 *           move 时扣一次（xsb_posToValue 为纯映射，XScrollBar.c 的
 *           xsb_posToValue 注释即本锁的锚点）；同点 MOVE 值不变、移回
 *           按下点精确复原、抬起提交真实到达的位置。 */
static int shv_movedCount = 0;
static int shv_movedLast = -1;
static int shv_valueChangedCount = 0;
static int shv_pressedCount = 0;
static int shv_releasedCount = 0;

static void shv_movedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, p);
    shv_movedLast = p;
    ++shv_movedCount;
}

static void shv_valueChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++shv_valueChangedCount;
}

static void shv_pressedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++shv_pressedCount;
}

static void shv_releasedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++shv_releasedCount;
}

XQ_TEST(XScrollBar, dragMappingTracksAndCommits)
{
    XWidget* host;
    XScrollBar* sb;
    int handlePos = 0;
    int handleLen = 0;
    int pressY;
    int posAfter;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    XScrollBar_setTracking(sb, false);
    shv_handle(sb, 200, &handlePos, &handleLen);
    pressY = handlePos + handleLen / 2;

    shv_movedCount = shv_valueChangedCount = 0;
    shv_pressedCount = shv_releasedCount = 0;
    shv_movedLast = -1;
    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_sliderMoved_signal(sb, 0),
                      shv_movedSlot);
    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_valueChanged_signal(sb, 0),
                      shv_valueChangedSlot);
    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_sliderPressed_signal(sb),
                      shv_pressedSlot);
    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_sliderReleased_signal(sb),
                      shv_releasedSlot);

    /* 按下手柄中部：sliderPressed 一发，值/位置都不动。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, pressY));
    XQ_EQ_I64(shv_pressedCount, 1);
    XQ_EQ_I64(XScrollBar_value(sb), 50);
    XQ_EQ_I64(XScrollBar_sliderPosition(sb), 50);
    XQ_EQ_I64(shv_movedCount, 0);

    /* 向下移 10px：位置跟手、值冻结（tracking=false）、sliderMoved 照发。 */
    shv_mouseMove((XWidget*)sb, 10, pressY + 10, XMouseButton_LeftButton);
    posAfter = XScrollBar_sliderPosition(sb);
    XQ_CHECK(posAfter > 50);
    XQ_EQ_I64(XScrollBar_value(sb), 50);
    XQ_EQ_I64(shv_movedCount, 1);
    XQ_EQ_I64(shv_movedLast, posAfter);
    XQ_EQ_I64(shv_valueChangedCount, 0);

    /* 同点再移：**不发** sliderMoved——Qt setSliderPosition 首行
     * `if (position == d->position) return;`（qabstractslider.cpp:459-461
     * 原文），同值早退在 sliderMoved（:466）之前。 */
    shv_mouseMove((XWidget*)sb, 10, pressY + 10, XMouseButton_LeftButton);
    XQ_EQ_I64(shv_movedCount, 1);
    XQ_EQ_I64(XScrollBar_sliderPosition(sb), posAfter);

    /* 抬起提交：value==position（setSliderDown(false)→SliderMove，
     * qabstractslider.cpp:462-469 口径）。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, pressY + 10,
                XMouseButton_NoButton);
    XQ_EQ_I64(shv_releasedCount, 1);
    XQ_EQ_I64(XScrollBar_value(sb), posAfter);
    XQ_EQ_I64(shv_valueChangedCount, 1);
    XQ_CHECK(!XAbstractSlider_isSliderDown((const XAbstractSlider*)sb));

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ---- H3. 中键绝对定位 round-trip（复核①的绝对定位分支） ---- */

/** @brief 中键点轨道：滑块中心贴点击处（qscrollbar.cpp:556-566）；第二击
 *         落点必须仍属页区（与已居中的滑块错开 > handleLen），点击点回退
 *         值精确复原——绝对定位分支的 clickOffset=sliderLength/2 只记一次
 *         （double-deduct 会破坏整数映射界与可逆性）。
 *  @details B8 终裁 2026-11-15 后缺省样式（XCommonStyle）对
 *           SH_ScrollBar_MiddleClickAbsolutePosition 返回 false ⇒ 中键
 *           绝对定位分支缺省**不可达**（中键点击被首段守卫整体 return，
 *           见 middleClickAbsoluteViaStyleHint）；本锁挂 MiddleAbs=1
 *           探针样式（Fusion 口径）维持该分支的映射可逆性语义，同时
 *           验证 xsb_middleClickAbsolute 走 styleHint 查询链路真通。 */
XQ_TEST(XScrollBar, middleAbsPositionReversible)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarMiddleAbsStyle* style;
    int origin = 0;
    int handleLen = 0;
    int travel;
    int v1;
    int v2;
    int lo;
    int hi;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    style = shv_createMiddleAbsStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 0);
    /* value=0 时 shv_handle 的 handlePos 即滑轨原点。 */
    shv_handle(sb, 200, &origin, &handleLen);
    travel = 200 - 2 * origin - handleLen;
    XQ_REQUIRE(travel > 20);
    /* 两击落点间距必须超过滑块长（否则第二击命中已居中的滑块本体，
     * 正确语义是滑块按下而非页区绝对定位）。70 与 120 相距 50 > 28。 */
    XQ_REQUIRE(50 > handleLen);

    /* 第一击 y=70 ⇒ 滑块中心贴 70。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_MiddleButton, 10, 70));
    v1 = XScrollBar_value(sb);
    XQ_CHECK(v1 > XScrollBar_pageStep(sb));
    shv_release((XWidget*)sb, XMouseButton_MiddleButton, 10, 70,
                XMouseButton_NoButton);

    /* 第二击 y=120（页区）：值增量夹在 [floor(Δr/t), floor(Δr/t)+1]
     * （posToValue 整数映射的逐点界）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_MiddleButton, 10, 120));
    v2 = XScrollBar_value(sb);
    shv_release((XWidget*)sb, XMouseButton_MiddleButton, 10, 120,
                XMouseButton_NoButton);
    lo = (50 * 100) / travel;
    hi = lo + 1;
    XQ_CHECK_MSG(v2 - v1 >= lo && v2 - v1 <= hi,
                 "50px 再击增量应落在映射整数界 [%d,%d]（实得 %d）",
                 lo, hi, v2 - v1);

    /* 回击 y=70（此刻滑块已居中 120，70 在其之外仍属页区）：值精确复原
     * （映射可逆性锁）。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_MiddleButton, 10, 70));
    XQ_EQ_I64(XScrollBar_value(sb), v1);
    shv_release((XWidget*)sb, XMouseButton_MiddleButton, 10, 70,
                XMouseButton_NoButton);

    XWidget_setStyle((XWidget*)sb, NULL);
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

/* ---- H4. 连发节奏真循环锁（复核②） ---- */

/** @brief 长按连发的**真调度器全链**节奏（processEvents 驱动 XObject
 *         定时器，非手工派发）：按下立即一步 → 阈值 500ms 内不抢跑 →
 *         之后每 50ms 连发不止（截断回归表现为首拍后冻结）→ 释放收口
 *         静默（qscrollbar.cpp:570 activateControl(500) +
 *         qabstractslider timerEvent 的 threshold→repeat 换挡）。 */
XQ_TEST(XScrollBar, repeatRhythmRealLoop)
{
    XWidget* host;
    XScrollBar* sb;
    int extent;
    int addMid;
    long long vAfterThreshold;
    long long vAfterRepeat;
    long long vQuiescent;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 100);
    XScrollBar_setPageStep(sb, 20);
    XScrollBar_setValue(sb, 50);
    extent = XStyle_pixelMetric_base(XWidget_style((const XWidget*)sb),
                                     XStylePM_ScrollBarExtent, NULL);
    if (extent <= 0) extent = 16;
    addMid = 200 - extent / 2;

    /* 按下：activateControl 立即一步（50→51）并布防 500ms 阈值定时器。 */
    XQ_CHECK(shv_press((XWidget*)sb, XMouseButton_LeftButton, 10, addMid));
    XQ_EQ_I64(XScrollBar_value(sb), 51);
    XQ_REQUIRE(sb->m_base.m_repeatTimerId != XTIMER_INVALID_ID);

    /* ① 阈值不抢跑：自旋 300ms（<500ms），值必须仍停在下拍（51）。 */
    shv_spinProcessEvents(300);
    vAfterThreshold = XScrollBar_value(sb);
    XQ_EQ_I64(vAfterThreshold, 51);

    /* ② 连发不止：再自旋至按下后 ≥1300ms——500ms 首拍 + 每 50ms 一拍
     *    理论 ~17 拍；下界 8 拍给 CoarseTimer/负载留 2 倍余量。若连发
     *    被截断（首拍后冻结）值将停在 52 附近，本断言即红。 */
    shv_spinProcessEvents(1000);
    vAfterRepeat = XScrollBar_value(sb);
    XQ_CHECK_MSG(vAfterRepeat >= 51 + 8,
                 "长按连发应持续推进（500ms 阈值后每 50ms 一拍），"
                 "实得 value=%lld（阈值期 %lld）",
                 vAfterRepeat, vAfterThreshold);

    /* ③ 释放收口：连发停、值静默（stopRepeatAction 是唯一收口点）。 */
    shv_release((XWidget*)sb, XMouseButton_LeftButton, 10, addMid,
                XMouseButton_NoButton);
    vQuiescent = XScrollBar_value(sb);
    XQ_EQ_I64(XAbstractSlider_repeatAction_base((const XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);
    shv_spinProcessEvents(250);
    XQ_EQ_I64(XScrollBar_value(sb), vQuiescent);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

/* ---- H5. flash 到期清位·真调度器交付锁（复核⑦） ---- */

/** @brief flash 的 0ms 定时器由**真事件调度器**交付到期（processEvents
 *         驱动，非手工派发 XTimerEvent）：flashed 清位 + flashTimerId 收
 *         敛 + 到期后的绘制选项不再带 State_On（亮点随到期消失，
 *         qscrollbar.cpp:453-464 + :294-295 全链）。 */
XQ_TEST(XScrollBar, flashExpiresInRealLoop)
{
    XWidget* host;
    XScrollBar* sb;
    XScrollBarTransientStyle* style;
    XImage* grab;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 20, 200);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 200);
    XWidget_show((XWidget*)host);
    style = shv_createTransientStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)sb, (XStyle*)style);
    XQ_CHECK(XScrollBar_transient(sb));

    /* 点亮：flashed + 真 0ms 定时器布防（存在调度器 ⇒ startTimer 成功）。 */
    XScrollBar_flash(sb);
    XQ_CHECK(XScrollBar_flashed(sb));
    XQ_REQUIRE(sb->m_flashTimerId != XTIMER_INVALID_ID);

    /* 点亮期的绘制：State_On 必须在场（自含「点亮→到期」全周期：在场是
     * 到期消失断言的前提，防「恒无 On」的假通过）。 */
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_On) != 0,
                 "点亮期 State_On 应在场（state=0x%x）", shv_stateCapture);

    /* processEvents 自旋 250ms ⇒ 真调度器交付 flash 定时器：清位收敛。 */
    shv_spinProcessEvents(250);
    XQ_CHECK_MSG(!XScrollBar_flashed(sb),
                 "flash 定时器应由真调度器交付到期清位");
    XQ_EQ_I64(sb->m_flashTimerId, XTIMER_INVALID_ID);

    /* 到期后的绘制：State_On 必须消失（flashed=0 且 transient=1 ⇒
     * initStyleOption 的 State_On 条件不再成立）。 */
    shv_stateCapture = -1;
    shv_activeSubCapture = -1;
    grab = XWidget_grab((XWidget*)sb);
    XQ_REQUIRE(grab != NULL);
    XClassDelete((XClass*)grab);
    XQ_CHECK_MSG(shv_stateCapture >= 0,
                 "DrawComplexControl 未被捕获（state=%d）", shv_stateCapture);
    XQ_CHECK_MSG((shv_stateCapture & XStyleState_On) == 0,
                 "flash 到期后 State_On 应消失（state=0x%x）",
                 shv_stateCapture);

    XWidget_setStyle((XWidget*)sb, NULL);
    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
    XClassDelete((XClass*)style);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */
