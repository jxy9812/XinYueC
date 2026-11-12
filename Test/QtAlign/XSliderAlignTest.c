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
 *             [2026-11-11 P0-17/18/19] 追加：
 *             - initStyleOption 填 m_upsideDown / m_sliderPosition(Set)
 *               （Qt qslider.cpp:98-127）；
 *             - CC_Slider 的 subControlRect 与**真实绘制**（ARGB32 离屏
 *               像素探针）都按 upsideDown + sliderPosition 定位把手；
 *             - 凹槽按下的按键语义按样式提示分派：中键 = 绝对定位并继续
 *               拖动，左键 = PageStepAdd/Sub + 500/50 连发且不进拖动；
 *               tracking=false 拖动只动 position、释放才提交 value。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON

#include "XSlider.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XEvent.h"
#include "XImage.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"
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
    XSlider* horiz = XSlider_create_2(XGuiOrientation_Horizontal,
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
              (int)XGuiOrientation_Vertical);
    XQ_EQ_I64(XAbstractSlider_orientation((XAbstractSlider*)horiz),
              (int)XGuiOrientation_Horizontal);
    /* 非法方向回落水平（与 XAbstractSlider_setOrientation 的校验一致）。 */
    XQ_EQ_I64(XAbstractSlider_orientation((XAbstractSlider*)bad),
              (int)XGuiOrientation_Horizontal);

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
    XSlider* s = XSlider_create_2(XGuiOrientation_Horizontal,
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
                                   XGuiOrientation_Horizontal);
    policy = XWidget_sizePolicy((XWidget*)s);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    /* 切回垂直 ⇒ 再转置。 */
    XAbstractSlider_setOrientation((XAbstractSlider*)s,
                                   XGuiOrientation_Vertical);
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
                                   XGuiOrientation_Horizontal);
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
    XSlider* s = XSlider_create_2(XGuiOrientation_Horizontal,
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
    /* [P0-17/P0-18] upsideDown + sliderPosition（qslider.cpp:113-117）：
     * 水平 LTR 且未倒置 ⇒ upsideDown=false；direction 被固定为 LTR
     * （"we use the upsideDown option instead"）；位置=值（未分离）。 */
    XQ_CHECK(!opt.m_upsideDown);
    XQ_CHECK(opt.m_sliderPositionSet);
    XQ_EQ_I64(opt.m_sliderPosition, 7);
    XQ_EQ_I64(opt.m_direction, 0);
    XQ_EQ_I64(opt.m_sliderSingleStep, 2);
    XQ_EQ_I64(opt.m_sliderPageStep, 8);
    XQ_EQ_I64(opt.m_sliderTickInterval, 3);
    /* tickPosition 原样（不因越界被清零）。 */
    XQ_EQ_I64((int)opt.m_sliderTickPosition, 9);
    XQ_CHECK((opt.m_state & XStyleState_Enabled) != 0);

    /* 外观翻转 ⇒ upsideDown 置位（P0-17）。 */
    XAbstractSlider_setInvertedAppearance((XAbstractSlider*)s, true);
    XSlider_initStyleOption(s, &opt);
    XQ_CHECK(opt.m_upsideDown);
    XAbstractSlider_setInvertedAppearance((XAbstractSlider*)s, false);

    /* 位置与值分离时，选项分别携带二者（P0-18）：tracking=false 时
     * setSliderPosition 只动位置（qabstractslider.cpp:455-473）。 */
    XAbstractSlider_setTracking((XAbstractSlider*)s, false);
    XAbstractSlider_setSliderPosition((XAbstractSlider*)s, 19);
    XSlider_initStyleOption(s, &opt);
    XQ_EQ_I64(opt.m_sliderPosition, 19);
    XQ_EQ_I64(opt.m_sliderValue, 7);

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

/* ==================== P0-17/P0-18：几何按 upsideDown + sliderPosition ==================== */

/** @brief 离屏渲染一次 CC_Slider（100x20），取 (x,y) 像素。
 *
 *  调色板刻意为三色分离（Base 黑 / Highlight 红 / Button 绿），便于
 *  用像素断言「把手在哪一端」而不依赖 Fusion 的渐变公式。 */
static uint32_t sl_renderSlider(XCommonStyle* style, XStyleOption* opt,
                                int x, int y)
{
    XImage img;
    XPainter painter;
    uint32_t px = 0u;
    XImage_init_ex(&img, 100, 20, XImageFormat_ARGB32);
    XImage_fill(&img, 0u);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, &img)) {
        opt->m_rect.x = 0;
        opt->m_rect.y = 0;
        opt->m_rect.width = 100;
        opt->m_rect.height = 20;
        XStyle_drawComplexControl((XStyle*)style, XStyleCC_Slider, opt,
                                  &painter, NULL);
        XPainter_end(&painter);
        px = XImage_pixel(&img, x, y);
    }
    XPainter_deinit(&painter);
    XClassDeinit(&img);
    return px;
}

/** @brief CC_Slider 的绘制与 subControlRect 都按 upsideDown（P0-17）与
 *         sliderPosition（P0-18）定位把手。
 *
 *  几何（xcs_drawSlider / VXCommonStyle_subControlRect 同式）：
 *  handlePos = 8 + (rect 主轴 - 16) * down，handle 宽 16；
 *  down = upsideDown ? 1-frac : frac；frac 由 sliderPosition 求得。
 *  Qt 对照：QCommonStyle::subControlRect 的
 *  `sliderPositionFromValue(min,max,sliderPosition,span,upsideDown)`。 */
XQ_TEST(XSlider, handleGeometryFromPositionAndUpsideDown)
{
    XCommonStyle* style = XCommonStyle_create();
    XStyleOption opt;
    XPalette palette;
    XColor base;
    XColor hl;
    XColor button;
    XRect handle;
    uint32_t leftPx;
    uint32_t rightPx;
    XQ_REQUIRE(style != NULL);

    /* 三色分离：Base=黑、Highlight=红、Button=绿。 */
    palette = XPalette_create();
    base = XColor_create_rgb(0, 0, 0, 255);
    hl = XColor_create_rgb(255, 0, 0, 255);
    button = XColor_create_rgb(0, 255, 0, 255);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Base, base);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Highlight, hl);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Button, button);

    XStyleOption_init(&opt, XStyleCC_Slider);
    opt.m_horizontal = 1;
    opt.m_state = XStyleState_Enabled;
    opt.m_palette = palette;
    opt.m_sliderMin = 0;
    opt.m_sliderMax = 100;
    opt.m_sliderValue = 0;
    opt.m_sliderPosition = 0;
    opt.m_sliderPositionSet = 1;
    opt.m_upsideDown = 0;

    /* subControlRect（命中矩形）：pos=0、未倒置 ⇒ 把手贴左端。 */
    opt.m_rect = (XRect){0, 0, 100, 20};
    handle = XStyle_subControlRect((XStyle*)style, XStyleCC_Slider, &opt,
                                   XStyleSC_SliderHandle, NULL);
    XQ_EQ_I64(handle.x, 0);
    XQ_EQ_I64(handle.width, 16);

    /* 真实绘制：同一选项下左端是把手（绿，不是红/黑），右端是槽底（黑）。 */
    leftPx = sl_renderSlider(style, &opt, 8, 10);
    rightPx = sl_renderSlider(style, &opt, 92, 10);
    XQ_CHECK_MSG(leftPx != 0xFFFF0000u && leftPx != 0xFF000000u,
                 "未倒置时左端应为把手填充：0x%08x", leftPx);
    XQ_EQ_I64(rightPx, 0xFF000000u); /* 槽底 Base。 */

    /* 倒置（P0-17）：把手到右端，左端变槽内已填充段（高亮红）。 */
    opt.m_upsideDown = 1;
    handle = XStyle_subControlRect((XStyle*)style, XStyleCC_Slider, &opt,
                                   XStyleSC_SliderHandle, NULL);
    XQ_EQ_I64(handle.x, 84); /* 100 - PM_SliderLength(16)。 */
    leftPx = sl_renderSlider(style, &opt, 8, 10);
    rightPx = sl_renderSlider(style, &opt, 92, 10);
    XQ_EQ_I64(leftPx, 0xFFFF0000u); /* 左端已被 sub-page 高亮覆盖。 */
    XQ_CHECK_MSG(rightPx != 0xFFFF0000u && rightPx != 0xFF000000u,
                 "倒置时右端应为把手填充：0x%08x", rightPx);
    opt.m_upsideDown = 0;

    /* 位置（P0-18）：value 仍 0，但 sliderPosition=90 ⇒ 把手移到 ~90%。
     * 定位值走 Qt 的 sliderPositionFromValue（span=84）：
     * (2·90·84 + 100) / (2·100) = 76。 */
    opt.m_sliderPosition = 90;
    handle = XStyle_subControlRect((XStyle*)style, XStyleCC_Slider, &opt,
                                   XStyleSC_SliderHandle, NULL);
    XQ_EQ_I64(handle.x, 76);
    leftPx = sl_renderSlider(style, &opt, 8, 10);
    rightPx = sl_renderSlider(style, &opt, 84, 10); /* 把手 76..91 内。 */
    XQ_EQ_I64(leftPx, 0xFFFF0000u); /* 左侧成为已填充段。 */
    XQ_CHECK_MSG(rightPx != 0xFFFF0000u && rightPx != 0xFF000000u,
                 "sliderPosition=90 时把手应在 ~90%% 处：0x%08x", rightPx);

    /* 未置 m_sliderPositionSet 的手搓选项回落 sliderValue（兼容旧口径）。 */
    opt.m_sliderPositionSet = 0;
    opt.m_sliderPosition = 90;
    opt.m_sliderValue = 0;
    handle = XStyle_subControlRect((XStyle*)style, XStyleCC_Slider, &opt,
                                   XStyleSC_SliderHandle, NULL);
    XQ_EQ_I64(handle.x, 0);

    XClassDelete((XClass*)style);
}

/* ==================== P0-19：凹槽按按键语义分派 ==================== */

/** @brief 注入一次鼠标按键事件并返回是否被接受。 */
static bool sl_mouseButton(XWidget* w, XEventType type, XMouseButton button,
                           int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, button, 0, pos);
    if (type == XEVENT_TYPE_MOUSE_MOVE) me.m_buttons = button;
    XObject_event_base((XObject*)w, (XEvent*)&me);
    return XEvent_isAccepted((XEvent*)&me);
}

/** @brief 凹槽点击语义（对标 QSlider::mousePressEvent qslider.cpp:324-384）：
 *         中键（SH_Slider_AbsoluteSetButtons=MiddleButton）= 绝对定位 +
 *         SliderMove 并进入拖动；左键（SH_Slider_PageSetButtons=
 *         LeftButton）= PageStepAdd/Sub + 500/50 连发、**不进入拖动**。 */
XQ_TEST(XSlider, groovePressSemantics)
{
    XSlider* s = XSlider_create_2(XGuiOrientation_Horizontal,
                                  NULL, 0);
    XStyleOption opt;
    int absButtons;
    int pageButtons;
    XQ_REQUIRE(s != NULL);
    XWidget_resize((XWidget*)s, 100, 20);
    XWidget_show((XWidget*)s);
    XAbstractSlider_setRange((XAbstractSlider*)s, 0, 100);
    XAbstractSlider_setSingleStep((XAbstractSlider*)s, 1);
    XAbstractSlider_setPageStep((XAbstractSlider*)s, 10);
    XAbstractSlider_setValue((XAbstractSlider*)s, 50);

    /* 样式层回报的按键集（XCommonStyle VXCommonStyle_styleHint）：
     * Absolute=MiddleButton(0x4)、Page=LeftButton(0x1)——旧实现
     * Absolute 返回 0x10（ForwardButton），中键永远匹配不上。 */
    XSlider_initStyleOption(s, &opt);
    absButtons = XStyle_styleHint(XWidget_style((const XWidget*)s),
                                  XStyleSH_Slider_AbsoluteSetButtons,
                                  &opt, (const XWidget*)s);
    pageButtons = XStyle_styleHint(XWidget_style((const XWidget*)s),
                                   XStyleSH_Slider_PageSetButtons,
                                   &opt, (const XWidget*)s);
    XQ_EQ_I64(absButtons, XMouseButton_MiddleButton);
    XQ_EQ_I64(pageButtons, XMouseButton_LeftButton);

    /* handle 中心 = 8 + 84*0.5 = 50；x=5 在把手之外（左侧凹槽）。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            XMouseButton_LeftButton, 5, 10));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 40); /* -1 页。 */
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)s),
              XAbstractSliderSliderAction_PageStepSub);
    XQ_CHECK(!XAbstractSlider_isSliderDown((XAbstractSlider*)s)); /* 不进拖动。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                            XMouseButton_LeftButton, 5, 10));
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)s),
              XAbstractSliderSliderAction_NoAction);

    /* 右侧凹槽 ⇒ 翻页增。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            XMouseButton_LeftButton, 95, 10));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 50);
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)s),
              XAbstractSliderSliderAction_PageStepAdd);
    XQ_CHECK(!XAbstractSlider_isSliderDown((XAbstractSlider*)s));
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                            XMouseButton_LeftButton, 95, 10));

    /* 左键按在把手上：进入拖动，不翻页。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            XMouseButton_LeftButton, 50, 10));
    XQ_CHECK(XAbstractSlider_isSliderDown((XAbstractSlider*)s));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 50);
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                            XMouseButton_LeftButton, 50, 10));
    XQ_CHECK(!XAbstractSlider_isSliderDown((XAbstractSlider*)s));

    /* 中键点左端凹槽：绝对定位（≈0）并进入拖动，且不布防连发。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            XMouseButton_MiddleButton, 5, 10));
    XQ_CHECK(XAbstractSlider_isSliderDown((XAbstractSlider*)s));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 0);
    XQ_EQ_I64(XAbstractSlider_repeatAction((XAbstractSlider*)s),
              XAbstractSliderSliderAction_NoAction);
    /* 继续拖到右端（绝对定位拖动）⇒ 值跟到最大。 */
    sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_MOVE,
                   XMouseButton_MiddleButton, 99, 10);
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 100);
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                            XMouseButton_MiddleButton, 99, 10));
    XQ_CHECK(!XAbstractSlider_isSliderDown((XAbstractSlider*)s));

    /* 不支持按键（右键）落在凹槽：忽略，不改值。 */
    XQ_CHECK(!sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                             XMouseButton_RightButton, 95, 10));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 100);

    XClassDelete((XClass*)s);
}

/** @brief tracking=false：拖动只动 sliderPosition，画面（选项）跟手，
 *         释放才把位置提交到 value（P0-18；qabstractslider.cpp:455-473）。 */
XQ_TEST(XSlider, trackingOffDragKeepsValue)
{
    XSlider* s = XSlider_create_2(XGuiOrientation_Horizontal,
                                  NULL, 0);
    XStyleOption opt;
    XQ_REQUIRE(s != NULL);
    XWidget_resize((XWidget*)s, 100, 20);
    XWidget_show((XWidget*)s);
    XAbstractSlider_setRange((XAbstractSlider*)s, 0, 100);
    XAbstractSlider_setValue((XAbstractSlider*)s, 20);
    XAbstractSlider_setTracking((XAbstractSlider*)s, false);

    /* 按下把手（中心 = 8 + 84*0.2 = 24），拖到 x=80。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                            XMouseButton_LeftButton, 24, 10));
    XQ_CHECK(XAbstractSlider_isSliderDown((XAbstractSlider*)s));
    sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_MOVE,
                   XMouseButton_LeftButton, 80, 10);
    /* 拖动中：value 不动，position 前进；样式选项携带新位置（画面跟手）。 */
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s), 20);
    XQ_CHECK(XAbstractSlider_sliderPosition((XAbstractSlider*)s) > 70);
    XSlider_initStyleOption(s, &opt);
    XQ_EQ_I64(opt.m_sliderValue, 20);
    XQ_EQ_I64(opt.m_sliderPosition,
              XAbstractSlider_sliderPosition((XAbstractSlider*)s));

    /* 释放：位置提交到值（setSliderDown(false) → SliderMove）。 */
    XQ_CHECK(sl_mouseButton((XWidget*)s, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                            XMouseButton_LeftButton, 80, 10));
    XQ_CHECK(!XAbstractSlider_isSliderDown((XAbstractSlider*)s));
    XQ_EQ_I64(XAbstractSlider_value((XAbstractSlider*)s),
              XAbstractSlider_sliderPosition((XAbstractSlider*)s));
    XQ_CHECK(XAbstractSlider_value((XAbstractSlider*)s) > 70);

    XClassDelete((XClass*)s);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON */
