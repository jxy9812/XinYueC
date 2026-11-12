/**
 * @file       XScrollBarAlignTest.c
 * @brief      XScrollBar × Qt 6.8 QScrollBar 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qscrollbar.h
 *               （QScrollBar(QWidget*) 无方向构造、sizeHint 覆写、
 *               hideEvent/mousePressEvent 等保护钩子）；
 *             - Qt 6.8.3 源码 qtbase/src/widgets/widgets/qscrollbar.cpp：
 *               QScrollBarPrivate::init 置 invertedControls=true（:345-347）、
 *               transient=SH_ScrollBar_Transient（Fusion=0）⇒ 画两端按钮
 *               （:353）、NoFocus（:355）、
 *               sizePolicy(Minimum, Fixed, Slider) 垂直转置（:357-361）、
 *               sizeHint = PM_ScrollBarExtent*2+PM_ScrollBarSliderMin 且
 *               横竖互换（:411-425）、wheelEvent 按 |dx|>|dy| 选轴并对
 *               水平轨道做混沌滚动守卫（:475-497）、mousePressEvent 的
 *               中键绝对定位（:585-596）、hideEvent 清 pressedControl +
 *               SliderNoAction（:692-700）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：缺省方向
 *               Vertical(2)、范围 0..99、value 0、single 1、page 10、
 *               invertedControls=1、focusPolicy=0(NoFocus)、
 *               sizePolicy=(0,1)（Fixed/Minimum 转置）、
 *               竖直 sizeHint 14x54 / 水平 54x14、minimumSizeHint (-1,-1)、
 *               SH_ScrollBar_Transient=0、SH_MiddleClickAbsolutePosition=1；
 *               水平条 dx=+120 → 47、竖直条 dx=+120 → 47、
 *               水平条 dx=+120 且 dy=+120 → 忽略。
 *
 *             覆盖：缺省属性（方向/范围/翻转/焦点/按钮/尺寸/策略）、
 *             无范围时按下被忽略、按钮/滑块/轨道命中、中键绝对定位、
 *             跨轴滚轮矩阵、隐藏收闸、父子与信号集成。
 *             [2026-11-11 P0-17/18/67] 追加：
 *             - 滑块几何（subControlRect）与**整控件离屏渲染**都按
 *               sliderPosition 与 upsideDown（= invertedAppearance）定位；
 *             - 两端步进按钮的按下态按 m_activeIsAdd 分按钮加深（只有被
 *               按下的那个变色，两端「按下色」一致）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

#include "XScrollBar.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XImage.h"
#include "XVarList.h"

/* ==================== 信号探针 ==================== */

static int sb_valueChangedCount = 0;
static int sb_pressedCount = 0;
static int sb_lastValue = -1;

static void sb_valueChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    sb_lastValue = v;
    ++sb_valueChangedCount;
}

static void sb_pressedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++sb_pressedCount;
}

/** @brief 注入一次鼠标按下，返回事件是否被接受。 */
static bool sb_pressButton(XWidget* w, XMouseButton button, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS, button, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
    return XEvent_isAccepted((XEvent*)&me);
}

/** @brief 注入一次左键按下，返回事件是否被接受。 */
static bool sb_press(XWidget* w, int x, int y)
{
    return sb_pressButton(w, XMouseButton_LeftButton, x, y);
}

/** @brief 注入一次指定按键释放。 */
static void sb_releaseButton(XWidget* w, XMouseButton button, int x, int y)
{
    XMouseEvent me;
    XPoint p = { x, y };
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, button, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&me);
}

/** @brief 注入一次左键释放。 */
static void sb_release(XWidget* w, int x, int y)
{
    sb_releaseButton(w, XMouseButton_LeftButton, x, y);
}

/** @brief 注入一次滚轮（dx/dy 角度增量），返回事件是否被接受。 */
static bool sb_wheel(XWidget* w, int dx, int dy)
{
    XWheelEvent we;
    XPoint pos = { 5, 5 };
    XPoint gpos = { 50, 50 };
    XPoint delta = { dx, dy };
    XWheelEvent_init(&we, XEVENT_TYPE_WHEEL, &pos, &gpos, &delta,
                     XMouseButton_NoButton, 0);
    XObject_event_base((XObject*)w, (XEvent*)&we);
    return XEvent_isAccepted((XEvent*)&we);
}

/** @brief 取滚动条度量（与实现同源：PM_ScrollBarExtent / SliderMin）。 */
static void sb_metrics(XScrollBar* sb, int* extent, int* sliderMin)
{
    XStyleOption opt;
    XStyleOption_init(&opt, XStyleCC_ScrollBar);
    opt.m_horizontal =
        XAbstractSlider_orientation((const XAbstractSlider*)sb) ==
        XGuiOrientation_Horizontal;
    *extent = XStyle_pixelMetric(XWidget_style((const XWidget*)sb),
                                 XStylePM_ScrollBarExtent, &opt);
    *sliderMin = XStyle_pixelMetric(XWidget_style((const XWidget*)sb),
                                    XStylePM_ScrollBarSliderMin, &opt);
    if (*extent <= 0) *extent = 16;
    if (*sliderMin <= 0) *sliderMin = 16;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值（qscrollbar.cpp:313-327, 345-361, 411-425）。 */
XQ_TEST(XScrollBar, defaults)
{
    XScrollBar* sb = XScrollBar_create(NULL, 0);
    XScrollBar* h = XScrollBar_create_2(
        (int)XGuiOrientation_Horizontal, NULL, 0);
    XSize hint;
    XSize minHint;
    XWidgetSizePolicy policy;
    int extent = 0;
    int sliderMin = 0;
    XQ_REQUIRE(sb != NULL);
    XQ_REQUIRE(h != NULL);

    /* 无方向构造 = Vertical（qscrollbar.cpp:313-327；Qt 实测方向 2）。 */
    XQ_EQ_I64(XScrollBar_orientation(sb),
              (int)XGuiOrientation_Vertical);
    XQ_EQ_I64(XScrollBar_minimum(sb), 0);
    XQ_EQ_I64(XScrollBar_maximum(sb), 99);
    XQ_EQ_I64(XScrollBar_value(sb), 0);
    XQ_EQ_I64(XScrollBar_singleStep(sb), 1);
    XQ_EQ_I64(XScrollBar_pageStep(sb), 10);
    /* 滚动条缺省控制翻转（qscrollbar.cpp:348 `invertedControls = true`）。 */
    XQ_CHECK(XAbstractSlider_invertedControls((XAbstractSlider*)sb));
    /* 焦点策略 NoFocus（qscrollbar.cpp:355；Qt 实测 focusPolicy=0）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)sb),
              (int)XWidgetFocusPolicy_NoFocus);

    /* 两端按钮缺省可见：Fusion SH_ScrollBar_Transient=0（实测），
     * QScrollBarPrivate::init 取原值 ⇒ !transient 画 Add/SubLine。 */
    XQ_CHECK(XScrollBar_showButtons(sb));
    XQ_CHECK(XScrollBar_showButtons(h));

    /* sizeHint = extent*2 + sliderMin，横竖互换（qscrollbar.cpp:411-425）。 */
    sb_metrics(sb, &extent, &sliderMin);
    hint = XScrollBar_sizeHint(sb);
    XQ_EQ_I64(hint.width, extent);
    XQ_EQ_I64(hint.height, extent * 2 + sliderMin);
    sb_metrics(h, &extent, &sliderMin);
    hint = XScrollBar_sizeHint(h);
    XQ_EQ_I64(hint.width, extent * 2 + sliderMin);
    XQ_EQ_I64(hint.height, extent);

    /* sizeHint 随方向切换重算（虚槽现场重算，无陈旧缓存）。 */
    XAbstractSlider_setOrientation((XAbstractSlider*)sb,
                                   XGuiOrientation_Horizontal);
    XQ_EQ_I64(XScrollBar_sizeHint(sb).width,
              XScrollBar_sizeHint(h).width);
    XQ_EQ_I64(XScrollBar_sizeHint(sb).height,
              XScrollBar_sizeHint(h).height);
    /* 切回垂直供下面的策略断言（转置是对称的）。 */
    XAbstractSlider_setOrientation((XAbstractSlider*)sb,
                                   XGuiOrientation_Vertical);

    /* minimumSizeHint 不覆写：Qt 实测 (-1,-1)（QWidget 基类无效尺寸）。 */
    minHint = XScrollBar_minimumSizeHint(sb);
    XQ_EQ_I64(minHint.width, -1);
    XQ_EQ_I64(minHint.height, -1);

    /* sizePolicy(Minimum, Fixed, Slider)，垂直转置 ⇒ (Fixed, Minimum)
     * （qscrollbar.cpp:357-361，Qt 实测 0/1）。 */
    policy = XWidget_sizePolicy((XWidget*)h);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&policy),
              (int)XWidgetSizePolicyControl_Slider);
    policy = XWidget_sizePolicy((XWidget*)sb);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Minimum);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)h);
}

/** @brief 无范围时按下被忽略；按钮/滑块命中；隐藏收闸。 */
XQ_TEST(XScrollBar, pressAndHideGuards)
{
    XScrollBar* sb = XScrollBar_create(NULL, 0);
    int extent = 0;
    int sliderMin = 0;
    int sliderLen;
    int handleLen;
    XQ_REQUIRE(sb != NULL);
    XWidget_resize((XWidget*)sb, 16, 100);
    XWidget_show((XWidget*)sb);
    sb_pressedCount = 0;
    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_sliderPressed_signal(sb),
                      sb_pressedSlot);

    /* 退化范围（max==min）：Qt mousePressEvent 首行直接 return
     * （qscrollbar.cpp:545-548）——不发 sliderPressed、不进入拖动。 */
    XScrollBar_setRange(sb, 10, 10);
    XQ_CHECK(!sb_press((XWidget*)sb, 8, 50));
    XQ_CHECK(!XScrollBar_isSliderDown(sb));
    XQ_EQ_I64(sb_pressedCount, 0);

    /* 正常范围 value=0：handle 位于滑轨起点（按钮之后）。 */
    XScrollBar_setRange(sb, 0, 99);
    XScrollBar_setValue(sb, 0);
    sb_metrics(sb, &extent, &sliderMin);
    sliderLen = 100 - 2 * extent;
    handleLen = sliderLen * 10 / (99 + 10);
    if (handleLen < sliderMin) handleLen = sliderMin;
    /* 两端按钮命中：按到起始按钮 ⇒ 单步减 + 连发。 */
    XQ_CHECK(sb_press((XWidget*)sb, 8, extent / 2));
    XQ_EQ_I64(XScrollBar_value(sb), 0); /* 已在最小：单步减不变。 */
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)sb),
              XAbstractSliderSliderAction_SingleStepSub);
    sb_release((XWidget*)sb, 8, extent / 2);
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);

    /* 滑块命中：进入拖动并发 sliderPressed。 */
    XQ_CHECK(sb_press((XWidget*)sb, 8, extent + handleLen / 2));
    XQ_CHECK(XScrollBar_isSliderDown(sb));
    XQ_EQ_I64(sb_pressedCount, 1);

    /* 隐藏：拖动/按下态与重复定时器一并收闸（qscrollbar.cpp:692-700）。 */
    XWidget_hide((XWidget*)sb);
    XQ_CHECK(!XScrollBar_isSliderDown(sb));
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);

    /* 轨道按下 → 翻页并布防连发；隐藏后连发被收（Qt 同 hideEvent）。
     * y=50 才是轨道：控件高 100、两端按钮各占一个 extent（16px），
     * 90 落在**结束按钮**命中带（≥84）会走 SingleStepAdd（value=1）。
     * 初始 value=0 时滑块贴起点，50 在滑块之后 ⇒ PageStepAdd。 */
    XWidget_show((XWidget*)sb);
    XQ_CHECK(sb_press((XWidget*)sb, 8, 50));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)sb), 10);
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)sb),
              XAbstractSliderSliderAction_PageStepAdd);
    XWidget_hide((XWidget*)sb);
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)sb),
              XAbstractSliderSliderAction_NoAction);
    sb_release((XWidget*)sb, 8, 50);

    XClassDelete((XClass*)sb);
}

/** @brief 中键绝对定位（qscrollbar.cpp:585-596，
 *         SH_ScrollBar_MiddleClickAbsolutePosition=1）。 */
XQ_TEST(XScrollBar, middleClickAbsolute)
{
    XScrollBar* sb = XScrollBar_create(NULL, 0);
    int extent = 0;
    int sliderMin = 0;
    int sliderLen;
    int handleLen;
    int travel;
    int rel;
    int expected;
    XQ_REQUIRE(sb != NULL);
    XWidget_resize((XWidget*)sb, 16, 100);
    XWidget_show((XWidget*)sb);
    sb_metrics(sb, &extent, &sliderMin);
    XScrollBar_setRange(sb, 0, 99);
    XScrollBar_setPageStep(sb, 10);
    XScrollBar_setValue(sb, 0);
    sliderLen = 100 - 2 * extent;
    handleLen = sliderLen * 10 / (99 + 10);
    if (handleLen < sliderMin) handleLen = sliderMin;
    travel = sliderLen - handleLen;
    /* Qt：setSliderPosition(pixelPosToRangeValue(click - sliderLength/2))。 */
    rel = 50 - extent - handleLen / 2;
    if (rel < 0) rel = 0;
    if (rel > travel) rel = travel;
    expected = (rel * 99 + travel / 2) / travel;

    /* 左键点轨道同一位置 ⇒ 只是一页（10），不是绝对定位。 */
    XQ_CHECK(sb_press((XWidget*)sb, 8, 50));
    sb_release((XWidget*)sb, 8, 50);
    XQ_EQ_I64(XScrollBar_value(sb), 10);

    /* 中键点轨道 ⇒ 滑块中心跳到点击处并进入拖动（值 ≈50）。 */
    XQ_CHECK(sb_pressButton((XWidget*)sb, XMouseButton_MiddleButton, 8, 50));
    XQ_CHECK(XScrollBar_isSliderDown(sb));
    XQ_EQ_I64(XScrollBar_value(sb), expected);
    XQ_CHECK(expected > 20 && expected < 80);
    sb_releaseButton((XWidget*)sb, XMouseButton_MiddleButton, 8, 50);
    XQ_CHECK(!XScrollBar_isSliderDown(sb));

    /* 中键按在滑块上等同左键拖动（不跳位）。 */
    XScrollBar_setValue(sb, 30);
    {
        int handleTop = extent + (30 * travel) / 99;
        XQ_CHECK(sb_pressButton((XWidget*)sb, XMouseButton_MiddleButton,
                                8, handleTop + handleLen / 2));
        XQ_EQ_I64(XScrollBar_value(sb), 30);
        sb_releaseButton((XWidget*)sb, XMouseButton_MiddleButton,
                         8, handleTop + handleLen / 2);
    }

    XClassDelete((XClass*)sb);
}

/** @brief 滚轮按 |dx|>|dy| 择轴 + 水平轨道混沌滚动守卫
 *         （qscrollbar.cpp:475-497，Qt 6.8 实测矩阵）。 */
XQ_TEST(XScrollBar, wheelAxisSelection)
{
    XScrollBar* h = XScrollBar_create_2(
        (int)XGuiOrientation_Horizontal, NULL, 0);
    XScrollBar* v = XScrollBar_create(NULL, 0);
    XQ_REQUIRE(h != NULL);
    XQ_REQUIRE(v != NULL);
    XScrollBar_setRange(h, 0, 99);
    XScrollBar_setRange(v, 0, 99);

    /* 水平条 dx=+120：|dx|>|dy| ⇒ 水平轴，内部取负两次 + invertedControls
     * ⇒ -3：50→47（Qt 实测）。 */
    XScrollBar_setValue(h, 50);
    XQ_CHECK(sb_wheel((XWidget*)h, 120, 0));
    XQ_EQ_I64(XScrollBar_value(h), 47);
    /* 水平条 dy=+120（无水平份量）：走竖直轴 ⇒ invertedControls ⇒ -3。 */
    XScrollBar_setValue(h, 50);
    XQ_CHECK(sb_wheel((XWidget*)h, 0, 120));
    XQ_EQ_I64(XScrollBar_value(h), 47);
    /* 竖直条 dx=+120：|dx|>|dy| ⇒ 水平轴 ⇒ 47（Qt 实测）。 */
    XScrollBar_setValue(v, 50);
    XQ_CHECK(sb_wheel((XWidget*)v, 120, 0));
    XQ_EQ_I64(XScrollBar_value(v), 47);
    /* 水平条 dx=+240 且 dy=+120：|dx|>|dy| ⇒ 水平轴，两格 = -6 ⇒ 44。 */
    XScrollBar_setValue(h, 50);
    XQ_CHECK(sb_wheel((XWidget*)h, 240, 120));
    XQ_EQ_I64(XScrollBar_value(h), 44);
    /* 水平条 dx=+120 且 dy=+120：|dx| 不更大、x 非零、轨道水平 ⇒
     * 混沌滚动守卫直接忽略（值不变、事件不被接受）。 */
    XScrollBar_setValue(h, 50);
    XQ_CHECK(!sb_wheel((XWidget*)h, 120, 120));
    XQ_EQ_I64(XScrollBar_value(h), 50);
    /* 竖直条同时带 dx=+120/dy=+120：守卫只针对水平轨道 ⇒ 走竖直轴。 */
    XScrollBar_setValue(v, 50);
    XQ_CHECK(sb_wheel((XWidget*)v, 120, 120));
    XQ_EQ_I64(XScrollBar_value(v), 47);

    XClassDelete((XClass*)h);
    XClassDelete((XClass*)v);
}

/** @brief StyleChange 事件按 transient 提示回写按钮显隐，X 扩展的显式
 *         覆盖不被回写（对标 QScrollBar::event 的 StyleChange 分支
 *         qscrollbar.cpp:445-451）。 */
XQ_TEST(XScrollBar, styleChangeButtons)
{
    XScrollBar* sb = XScrollBar_create(NULL, 0);
    XEvent ev;
    XQ_REQUIRE(sb != NULL);

    /* 显式关闭后 StyleChange 不得把按钮打开（XScrollBar_setShowButtons
     * 扩展契约：显式设置优先）。 */
    XScrollBar_setShowButtons(sb, false);
    XEvent_init(&ev, XEVENT_TYPE_STYLE_CHANGE);
    XObject_event_base((XObject*)sb, &ev);
    XQ_CHECK(!XScrollBar_showButtons(sb));

    /* 未显式设置时按 SH_ScrollBar_Transient（XCommonStyle/XFusionStyle
     * 均返回 0 ⇒ 非 transient ⇒ 按钮可见）。 */
    {
        XScrollBar* d = XScrollBar_create(NULL, 0);
        XQ_REQUIRE(d != NULL);
        XEvent_init(&ev, XEVENT_TYPE_STYLE_CHANGE);
        XObject_event_base((XObject*)d, &ev);
        XQ_CHECK(XScrollBar_showButtons(d));
        XClassDelete((XClass*)d);
    }

    XClassDelete((XClass*)sb);
}

/** @brief 父子所有权 + valueChanged 集成。 */
XQ_TEST(XScrollBar, parentAndSignals)
{
    XWidget* host = XWidget_create(NULL, 0);
    XScrollBar* sb = NULL;
    XQ_REQUIRE(host != NULL);
    sb = XScrollBar_create(host, 0);
    XQ_REQUIRE(sb != NULL);

    XObject_connect_2((XObject*)sb,
                      (size_t)XScrollBar_valueChanged_signal(sb, 0),
                      sb_valueChangedSlot);
    XScrollBar_setValue(sb, 5);
    XQ_EQ_I64(sb_valueChangedCount, 1);
    XQ_EQ_I64(sb_lastValue, 5);
    /* 同值不重发（qabstractslider.cpp:498-507）。 */
    XScrollBar_setValue(sb, 5);
    XQ_EQ_I64(sb_valueChangedCount, 1);
    /* 越界钳位到 max 并发一次。 */
    XScrollBar_setValue(sb, 200);
    XQ_EQ_I64(XScrollBar_value(sb), 99);
    XQ_EQ_I64(sb_valueChangedCount, 2);

    XQ_CHECK(XWidget_parentWidget((XWidget*)sb) == host);
    XClassDelete((XClass*)host);
}

/* ==================== P0-17/P0-18：滑块几何按位置 + upsideDown ==================== */

/** @brief CC_ScrollBar 的 SC_ScrollBarSlider 矩形（VXCommonStyle_subControlRect）
 *         以 sliderPosition 为准（P0-18）、upsideDown 从另一端数（P0-17）。
 *
 *  几何（XCommonStyle）：extent=16、maxlen=100-32=68、range=99、page=10 ⇒
 *  sliderlen = max(68*10/109, PM_ScrollBarSliderMin=9) = 9，
 *  可走行程 = 68-9 = 59；sliderstart = 16 + positionFromValue(...,59,up)。 */
XQ_TEST(XScrollBar, sliderGeometryFollowsPositionAndUpsideDown)
{
    XCommonStyle* style = XCommonStyle_create();
    XStyleOption opt;
    XRect slider;
    XQ_REQUIRE(style != NULL);

    XStyleOption_init(&opt, XStyleCC_ScrollBar);
    opt.m_rect = (XRect){0, 0, 20, 100};
    opt.m_horizontal = 0;
    opt.m_state = XStyleState_Enabled;
    opt.m_sliderMin = 0;
    opt.m_sliderMax = 99;
    opt.m_sliderPageStep = 10;
    opt.m_sliderValue = 0;
    opt.m_sliderPosition = 0;
    opt.m_sliderPositionSet = 1;
    opt.m_scrollSubLine = 1;
    opt.m_scrollAddLine = 1;
    opt.m_upsideDown = 0;

    /* value=position=0、未倒置 ⇒ 滑块贴按钮之后（y=16）。 */
    slider = XStyle_subControlRect((XStyle*)style, XStyleCC_ScrollBar, &opt,
                                   XStyleSC_ScrollBarSlider, NULL);
    XQ_EQ_I64(slider.y, 16);

    /* 倒置（P0-17）⇒ 贴另一端（16+59=75）。 */
    opt.m_upsideDown = 1;
    slider = XStyle_subControlRect((XStyle*)style, XStyleCC_ScrollBar, &opt,
                                   XStyleSC_ScrollBarSlider, NULL);
    XQ_EQ_I64(slider.y, 75);

    /* position=99（P0-18；value 仍 0）：倒置时回到起点，未倒置到终点。 */
    opt.m_sliderPosition = 99;
    slider = XStyle_subControlRect((XStyle*)style, XStyleCC_ScrollBar, &opt,
                                   XStyleSC_ScrollBarSlider, NULL);
    XQ_EQ_I64(slider.y, 16);
    opt.m_upsideDown = 0;
    slider = XStyle_subControlRect((XStyle*)style, XStyleCC_ScrollBar, &opt,
                                   XStyleSC_ScrollBarSlider, NULL);
    XQ_EQ_I64(slider.y, 75);

    /* 未置 m_sliderPositionSet 的手搓选项回落 sliderValue（兼容旧口径）。 */
    opt.m_sliderPositionSet = 0;
    opt.m_sliderValue = 0;
    slider = XStyle_subControlRect((XStyle*)style, XStyleCC_ScrollBar, &opt,
                                   XStyleSC_ScrollBarSlider, NULL);
    XQ_EQ_I64(slider.y, 16);

    XClassDelete((XClass*)style);
}

/* ==================== P0-67：按下按钮方向 ==================== */

/** @brief 抓一次控件快照并取两个探针像素（XWidget_grab 同步重绘路径）。 */
static bool sb_grabTwo(XScrollBar* sb, int x1, int y1, uint32_t* p1,
                       int x2, int y2, uint32_t* p2)
{
    XImage* image = XWidget_grab((const XWidget*)sb);
    if (!image) return false;
    *p1 = XImage_pixel(image, x1, y1);
    *p2 = XImage_pixel(image, x2, y2);
    XClassDelete((XClass*)image);
    return true;
}

/** @brief 两端按钮按下态只加深被按的那个（P0-67）。
 *
 *  旧实现用单个 bool `m_activeSub` 填样式选项 ⇒ 两个按钮同时上按下色；
 *  现在按 `m_activeIsAdd` 填 m_scrollActiveDir（1=SubLine / 2=AddLine），
 *  样式层分按钮取色。断言方式：整控件离屏渲染后探针两端按钮填充色。 */
XQ_TEST(XScrollBar, pressedButtonDirection)
{
    /* sb 必须是**子控件**：XWidget_grab 对顶层窗口走「后备存储拷贝」
       路径（XWidget.c XWidget_grab 路径 1），无事件循环时返回最近一次
       上屏的陈旧瓦片——注入的按下态永不进图，两端按钮恒呈旧态色。
       挂到宿主控件下使 grab 走路径 2（同步完整重绘），按下态即时可见。 */
    XWidget* host = XWidget_create(NULL, 0);
    XScrollBar* sb = XScrollBar_create(host, 0);
    int extent = 0;
    int sliderMin = 0;
    uint32_t subUp = 0;
    uint32_t addUp = 0;
    uint32_t addIdle = 0;
    uint32_t subIdle = 0;
    XQ_REQUIRE(sb != NULL);
    XWidget_resize((XWidget*)host, 20, 100);
    XWidget_setGeometry((XWidget*)sb, 0, 0, 20, 100);
    XWidget_show((XWidget*)host);
    XScrollBar_setRange(sb, 0, 99);
    XScrollBar_setValue(sb, 40);
    sb_metrics(sb, &extent, &sliderMin);

    /* 按 SubLine（顶部按钮）：m_activeSub=1、m_activeIsAdd=0。 */
    XQ_CHECK(sb_press((XWidget*)sb, 10, extent / 2));
    XQ_EQ_I64((int)sb->m_activeSub, 1);
    XQ_EQ_I64((int)sb->m_activeIsAdd, 0);
    XQ_CHECK(sb_grabTwo(sb, 2, 3, &subUp, 2, 100 - 4, &addIdle));
    sb_release((XWidget*)sb, 10, extent / 2);

    /* 按 AddLine（底部按钮）：m_activeSub=1、m_activeIsAdd=1。 */
    XQ_CHECK(sb_press((XWidget*)sb, 10, 100 - extent / 2));
    XQ_EQ_I64((int)sb->m_activeSub, 1);
    XQ_EQ_I64((int)sb->m_activeIsAdd, 1);
    XQ_CHECK(sb_grabTwo(sb, 2, 3, &subIdle, 2, 100 - 4, &addUp));
    sb_release((XWidget*)sb, 10, 100 - extent / 2);
    XQ_EQ_I64((int)sb->m_activeSub, 0);

    /* 按下端比常态暗、未按端保持常态；两端「按下色」一致。 */
    XQ_CHECK_MSG(subUp != subIdle,
                 "SubLine 按下应只加深自己：up=0x%08x idle=0x%08x",
                 subUp, subIdle);
    XQ_CHECK_MSG(addUp != addIdle,
                 "AddLine 按下应只加深自己：up=0x%08x idle=0x%08x",
                 addUp, addIdle);
    XQ_CHECK_MSG(subUp == addUp,
                 "两端按下色应一致：sub=0x%08x add=0x%08x", subUp, addUp);
    XQ_CHECK_MSG(addIdle == subIdle,
                 "两端常态色应一致：addIdle=0x%08x subIdle=0x%08x",
                 addIdle, subIdle);

    XClassDelete((XClass*)sb);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */
