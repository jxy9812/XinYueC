/**
 * @file       XSliderAlignTest.c
 * @brief      XSlider × Qt 6.8 QSlider 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qslider.h
 *               （TickPosition 含 TicksLeft/TicksRight 同值别名、
 *               QSlider(Qt::Orientation, parent) 构造、sizeHint/
 *               minimumSizeHint、tickInterval/tickPosition）；
 *             - Qt 6.8.3 源码 qtbase/src/widgets/widgets/qslider.cpp：
 *               `QSlider(QWidget*)` 委托 `QSlider(Qt::Vertical, parent)`
 *               （:253-255）；QSliderPrivate::init 的
 *               QSizePolicy(Expanding, Fixed, Slider) 与垂直转置（:57-63）；
 *               sizeHint 的 `SliderLength = 84, TickSpace = 5` 与
 *               **按位** `tickPosition & TicksAbove/TicksBelow`（:424-445）；
 *               minimumSizeHint 主轴回落 PM_SliderLength（:447-460）；
 *               setTickPosition **不校验、原样存 int**（:472-479）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：
 *               QSlider(QWidget*) 方向=2（Vertical）；
 *               垂直缺省 sizeHint 15x84、minimumSizeHint 15x15；
 *               水平 84x15；TicksBothSides 垂直 25x84 / 水平 84x25；
 *               缺省 sizePolicy=(Fixed, Expanding, Slider)，
 *               setOrientation 后转置为 (Expanding, Fixed, Slider)；
 *               setTickPosition(9) 读回 9（sizeHint 位判定加一份 TickSpace）。
 *
 *             覆盖：方向构造（缺省垂直）、刻度别名/位判定/原样存储、
 *             尺寸提示（TickSpace/minimumSizeHint）、sizePolicy 与方向
 *             转置、initStyleOption 填充、父子与信号集成。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON

#include "XSlider.h"
#include "XWidget.h"
#include "XObject.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XVarList.h"

/* ==================== 信号探针 ==================== */

static int sl_valueChangedCount = 0;
static int sl_lastValue = -1;

static void sl_valueChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    sl_lastValue = v;
    ++sl_valueChangedCount;
}

/* ==================== 用例 ==================== */

/** @brief 方向构造与缺省尺寸/策略（qslider.cpp:253-255, 57-63, 424-460）。 */
XQ_TEST(XSlider, defaultsAndOrientation)
{
    XSlider* def = XSlider_create(NULL, 0);
    XSlider* horiz = XSlider_create_2(XAbstractSliderOrientation_Horizontal,
                                      NULL, 0);
    XSlider* bad = XSlider_create_2(9, NULL, 0);
    XSize vHint;
    XSize vMinHint;
    XSize hHint;
    XSize hMinHint;
    XWidgetSizePolicy policy;
    XQ_REQUIRE(def != NULL);
    XQ_REQUIRE(horiz != NULL);
    XQ_REQUIRE(bad != NULL);

    /* Qt 缺省垂直（qslider.cpp:253-255，实测方向=2）。 */
    XQ_EQ_I64(XAbstractSlider_orientation((XAbstractSlider*)def),
              (int)XAbstractSliderOrientation_Vertical);
    XQ_EQ_I64(XAbstractSlider_orientation((XAbstractSlider*)horiz),
              (int)XAbstractSliderOrientation_Horizontal);
    /* 非法方向回落水平（与 XAbstractSlider_setOrientation 的校验一致）。 */
    XQ_EQ_I64(XAbstractSlider_orientation((XAbstractSlider*)bad),
              (int)XAbstractSliderOrientation_Horizontal);

    /* 范围/步进/焦点与 QSlider 缺省一致（QAbstractSliderPrivate:178-193；
     * QSliderPrivate::init 取 SH_Button_FocusPolicy = StrongFocus）。 */
    XQ_EQ_I64(XAbstractSlider_minimum((XAbstractSlider*)def), 0);
    XQ_EQ_I64(XAbstractSlider_maximum((XAbstractSlider*)def), 99);
    XQ_EQ_I64(XAbstractSlider_singleStep((XAbstractSlider*)def), 1);
    XQ_EQ_I64(XAbstractSlider_pageStep((XAbstractSlider*)def), 10);
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)def),
              (int)XWidgetFocusPolicy_StrongFocus);
    XQ_EQ_I64(XSlider_tickPosition(def), (int)XSliderTickPosition_NoTicks);
    XQ_EQ_I64(XSlider_tickInterval(def), 0);

    /* sizeHint：主轴恒 84（Qt SliderLength 常量），副轴 = PM_SliderThickness；
     * 垂直缺省 ⇒ 高 84，水平 ⇒ 宽 84。minimumSizeHint 主轴回落到
     * PM_SliderLength。 */
    vHint = XSlider_sizeHint(def);
    XQ_EQ_I64(vHint.height, 84);
    XQ_CHECK(vHint.width > 0);
    vMinHint = XSlider_minimumSizeHint(def);
    XQ_EQ_I64(vMinHint.width, vHint.width);
    XQ_CHECK(vMinHint.height <= vHint.height);

    hHint = XSlider_sizeHint(horiz);
    XQ_EQ_I64(hHint.width, 84);
    XQ_CHECK(hHint.height > 0);
    hMinHint = XSlider_minimumSizeHint(horiz);
    XQ_EQ_I64(hMinHint.height, hHint.height);
    XQ_CHECK(hMinHint.width <= hHint.width);

    /* 缺省 sizePolicy：垂直 = (Fixed, Expanding, Slider)（Expanding/Fixed
     * 转置），水平 = (Expanding, Fixed, Slider)（qslider.cpp:57-63）。 */
    policy = XWidget_sizePolicy((XWidget*)def);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&policy),
              (int)XWidgetSizePolicyControl_Slider);
    /* 构造末尾把 OwnSizePolicy 清回 false（Qt 同），方向切换才继续转置。 */
    XQ_CHECK(!XWidget_testAttribute((XWidget*)def,
                                    XWidgetAttribute_WState_OwnSizePolicy));
    policy = XWidget_sizePolicy((XWidget*)horiz);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    XClassDelete((XClass*)def);
    XClassDelete((XClass*)horiz);
    XClassDelete((XClass*)bad);
}

/** @brief 刻度：别名、TickSpace 尺寸、tickInterval 全 int 往返、
 *         tickPosition 原样存储与按位判定（qslider.cpp:424-445, 472-479）。 */
XQ_TEST(XSlider, tickGeometry)
{
    XSlider* s = XSlider_create_2(XAbstractSliderOrientation_Horizontal,
                                  NULL, 0);
    XSize base;
    XSize above;
    XSize below;
    XSize both;
    XSize invalid;
    XSize minAbove;
    XQ_REQUIRE(s != NULL);

    /* Qt qslider.h:27-28：TicksLeft = TicksAbove、TicksRight = TicksBelow。 */
    XQ_EQ_I64(XSliderTickPosition_TicksLeft, XSliderTickPosition_TicksAbove);
    XQ_EQ_I64(XSliderTickPosition_TicksRight, XSliderTickPosition_TicksBelow);

    base = XSlider_sizeHint(s);
    /* 刻度每占一条边，厚度 +TickSpace(5)（Qt 实测 15→20→25）。 */
    XSlider_setTickPosition(s, XSliderTickPosition_TicksAbove);
    above = XSlider_sizeHint(s);
    XQ_EQ_I64(above.height, base.height + 5);
    XQ_EQ_I64(above.width, base.width);

    XSlider_setTickPosition(s, XSliderTickPosition_TicksBelow);
    below = XSlider_sizeHint(s);
    XQ_EQ_I64(below.height, base.height + 5);

    XSlider_setTickPosition(s, XSliderTickPosition_TicksBothSides);
    both = XSlider_sizeHint(s);
    XQ_EQ_I64(both.height, base.height + 10);

    /* minimumSizeHint 副轴同样含刻度留白（Qt 实测 TicksAbove 15x20）。 */
    minAbove = XSlider_minimumSizeHint(s);
    XQ_EQ_I64(minAbove.height, both.height);
    XQ_CHECK(minAbove.width <= both.width);

    /* tickPosition 原样存 int（qslider.cpp:472-479：Qt 不校验）。 */
    XSlider_setTickPosition(s, 9);
    XQ_EQ_I64(XSlider_tickPosition(s), 9);
    /* 尺寸提示按位判定（Qt 原文 `tickPosition & TicksAbove`）：9 的
     * bit0=1、bit1=0 ⇒ 只加一份 TickSpace。 */
    invalid = XSlider_sizeHint(s);
    XQ_EQ_I64(invalid.height, base.height + 5);
    XSlider_setTickPosition(s, 6); /* bit0=0、bit1=1 ⇒ 下侧一份。 */
    XQ_EQ_I64(XSlider_tickPosition(s), 6);
    XQ_EQ_I64(XSlider_sizeHint(s).height, base.height + 5);
    XSlider_setTickPosition(s, 0);

    /* tickInterval 为全 int：Qt 的 QSliderPrivate::tickInterval 是 int，
     * >65535 不得截断（旧实现收 16 位位域，70000 读回 4464）。 */
    XSlider_setTickInterval(s, 70000);
    XQ_EQ_I64(XSlider_tickInterval(s), 70000);
    /* 负值按 0（qMax(0, ts)）；同值早退不改变读数。 */
    XSlider_setTickInterval(s, -3);
    XQ_EQ_I64(XSlider_tickInterval(s), 0);
    XSlider_setTickInterval(s, 5);
    XQ_EQ_I64(XSlider_tickInterval(s), 5);

    XClassDelete((XClass*)s);
}

/** @brief 方向切换转置 sizePolicy，显式设置后不再转置
 *         （qabstractslider.cpp:264-277）。 */
XQ_TEST(XSlider, sizePolicyTranspose)
{
    XSlider* s = XSlider_create(NULL, 0); /* 垂直 */
    XWidgetSizePolicy policy;
    XQ_REQUIRE(s != NULL);

    /* 缺省垂直 = (Fixed, Expanding)。 */
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    /* 切水平 ⇒ 转置为 (Expanding, Fixed)。 */
    XAbstractSlider_setOrientation((XAbstractSlider*)s,
                                   XAbstractSliderOrientation_Horizontal);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    /* 切回垂直 ⇒ 再转置。 */
    XAbstractSlider_setOrientation((XAbstractSlider*)s,
                                   XAbstractSliderOrientation_Vertical);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);

    /* 显式 setSizePolicy（置 OwnSizePolicy）后方向切换不动策略。 */
    XWidget_setSizePolicy((XWidget*)s, XWidgetSizePolicy_Minimum,
                          XWidgetSizePolicy_Fixed);
    XQ_CHECK(XWidget_testAttribute((XWidget*)s,
                                   XWidgetAttribute_WState_OwnSizePolicy));
    XAbstractSlider_setOrientation((XAbstractSlider*)s,
                                   XAbstractSliderOrientation_Horizontal);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    XClassDelete((XClass*)s);
}

/** @brief initStyleOption 填充（qslider.cpp:416-446）。 */
XQ_TEST(XSlider, initStyleOption)
{
    XSlider* s = XSlider_create_2(XAbstractSliderOrientation_Horizontal,
                                  NULL, 0);
    XStyleOption opt;
    XQ_REQUIRE(s != NULL);
    XAbstractSlider_setRange((XAbstractSlider*)s, 1, 21);
    XAbstractSlider_setSingleStep((XAbstractSlider*)s, 2);
    XAbstractSlider_setPageStep((XAbstractSlider*)s, 8);
    XAbstractSlider_setValue((XAbstractSlider*)s, 7);
    XSlider_setTickInterval(s, 3);
    XSlider_setTickPosition(s, 9);

    XSlider_initStyleOption(s, &opt);
    XQ_CHECK(opt.m_horizontal);
    XQ_EQ_I64(opt.m_sliderMin, 1);
    XQ_EQ_I64(opt.m_sliderMax, 21);
    XQ_EQ_I64(opt.m_sliderValue, 7);
    XQ_EQ_I64(opt.m_sliderSingleStep, 2);
    XQ_EQ_I64(opt.m_sliderPageStep, 8);
    XQ_EQ_I64(opt.m_sliderTickInterval, 3);
    /* tickPosition 原样（不因越界被清零）。 */
    XQ_EQ_I64((int)opt.m_sliderTickPosition, 9);
    XQ_CHECK((opt.m_state & XStyleState_Enabled) != 0);

    XClassDelete((XClass*)s);
}

/** @brief 父子所有权 + 信号集成（setValue / triggerAction）。 */
XQ_TEST(XSlider, parentAndSignals)
{
    XWidget* host = XWidget_create(NULL, 0);
    XSlider* s = NULL;
    XQ_REQUIRE(host != NULL);
    s = XSlider_create(host, 0);
    XQ_REQUIRE(s != NULL);

    XObject_connect_2((XObject*)s,
                      (size_t)XSlider_valueChanged_signal(s, 0),
                      sl_valueChangedSlot);
    XAbstractSlider_setRange((XAbstractSlider*)s, 0, 100);
    XAbstractSlider_setSingleStep((XAbstractSlider*)s, 5);
    XAbstractSlider_setValue((XAbstractSlider*)s, 20);
    XQ_EQ_I64(sl_valueChangedCount, 1);
    XQ_EQ_I64(sl_lastValue, 20);
    /* 同值不重复发（qabstractslider.cpp:500）。 */
    XAbstractSlider_setValue((XAbstractSlider*)s, 20);
    XQ_EQ_I64(sl_valueChangedCount, 1);
    XAbstractSlider_triggerAction((XAbstractSlider*)s,
                                  XAbstractSliderSliderAction_SingleStepAdd);
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 25);
    XQ_EQ_I64(sl_valueChangedCount, 2);

    /* 子控件随 host 析构（漏删由运行器 [LEAK] 判失败）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)s) == host);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON */
