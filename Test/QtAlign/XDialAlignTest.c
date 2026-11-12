/**
 * @file       XDialAlignTest.c
 * @brief      XDial × Qt 6.8 QDial 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdial.h
 *               （wrapping/notchSize/notchTarget/notchesVisible、
 *               sizeHint/minimumSizeHint 覆写）；
 *             - Qt 6.8 源码 qdial.cpp（jsdelivr
 *               qtbase@v6.8.0/src/widgets/widgets/qdial.cpp）：
 *               `QDialPrivate::init` 置 setFocusPolicy(WheelFocus)（:49-54）、
 *               sizeHint 恒 100x100（:425-428）/ minimumSizeHint 50x50
 *               （:416-419）、notchSize 公式（:346-363）、
 *               bound 的 wrapping 取模（:56-70）、valueFromPoint 的
 *               角度映射（:106-139）、mousePress/Move/Release 的
 *               setSliderPosition/setValue + sliderDown 顺序（:239-296）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测（100x100、0..100）：
 *               3 点=80、12 点=50、6 点=100（死角钳到 max）、9 点=20；
 *               wrapping 时 3 点=75、6 点=100、12 点=50；
 *               invertedAppearance 时 3 点=20；
 *               范围 -50..50 时 3 点=30、12 点=0、6 点=50；
 *               缺省 notchTarget=3.7、notchSize=1，100x100 时 notchSize=4。
 *
 *             覆盖：缺省属性、notchSize/notchTarget、wrapping 值回绕、
 *             鼠标角度映射矩阵（含 wrapping/invertedAppearance/负下限）、
 *             父子所有权。
 *             [2026-11-11 P0-17/18] 追加：指示针角度按 sliderPosition 求值、
 *             upsideDown（QDial 的 `!invertedAppearance`，qdial.cpp:92）
 *             翻转 —— 样式层离屏像素探针 + 整控件渲染两态差异。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON

#include "XDial.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XImage.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"

/** @brief 在 (x,y) 处按下并释放左键（一次完整点击）。 */
static void dl_click(XWidget* w, int x, int y)
{
    XMouseEvent press;
    XMouseEvent release;
    XPoint p = { x, y };
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&press);
    XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, p);
    XObject_event_base((XObject*)w, (XEvent*)&release);
}

/* ==================== 用例 ==================== */

/** @brief 缺省值 / 焦点 / 尺寸提示（qdial.cpp:49-54,416-428）。 */
XQ_TEST(XDial, defaultsAndSizeHint)
{
    XDial* d = XDial_create(NULL, 0);
    XSize hint;
    XSize minHint;
    XQ_REQUIRE(d != NULL);

    XQ_EQ_I64(XDial_minimum(d), 0);
    XQ_EQ_I64(XDial_maximum(d), 99);
    XQ_EQ_I64(XDial_value(d), 0);
    XQ_EQ_I64(XDial_singleStep(d), 1);
    XQ_EQ_I64(XDial_pageStep(d), 10);
    /* Qt 实测 notchTarget 缺省 3.7、刻度隐藏、不环绕。 */
    XQ_CHECK(XDial_notchTarget(d) > 3.69 && XDial_notchTarget(d) < 3.71);
    XQ_CHECK(!XDial_notchesVisible(d));
    XQ_CHECK(!XDial_wrapping(d));
    /* QDialPrivate::init：setFocusPolicy(Qt::WheelFocus)（qdial.cpp:53；
     * Qt 实测 focusPolicy=15）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)d),
              (int)XWidgetFocusPolicy_WheelFocus);

    /* sizeHint 100x100 / minimumSizeHint 50x50（qdial.cpp:416-428）。 */
    hint = XDial_sizeHint(d);
    XQ_EQ_I64(hint.width, 100);
    XQ_EQ_I64(hint.height, 100);
    minHint = XDial_minimumSizeHint(d);
    XQ_EQ_I64(minHint.width, 50);
    XQ_EQ_I64(minHint.height, 50);

    XClassDelete((XClass*)d);
}

/** @brief notchSize / notchTarget（qdial.cpp:346-363, 364-370）。 */
XQ_TEST(XDial, notchSize)
{
    XDial* d = XDial_create(NULL, 0);
    XQ_REQUIRE(d != NULL);

    /* 几何决定 notchSize（pixels = min(w,h)），先按 100x100 固定下来。
     * 注：XWidget 缺省几何对顶层是 640x480（XWidget.c 预置），不显式
     * resize 的读数不可作为契约，故此处不锁未给几何的取值。 */
    XWidget_resize((XWidget*)d, 100, 100);
    /* 100x100、0..99、target 3.7 → 4（Qt 实测 4；公式见 qdial.cpp:346-363）。 */
    XQ_EQ_I64(XDial_notchSize(d), 4);

    /* setNotchTarget(0) 不校验、原值可见；notchSize 退化为 singleStep×1
     * （Qt 实测 notchSize=1）。 */
    XDial_setNotchTarget(d, 0.0);
    XQ_CHECK(XDial_notchTarget(d) == 0.0);
    XQ_EQ_I64(XDial_notchSize(d), 1);
    /* 负值同样原样接受（Qt 实测 -2 读回 -2）。 */
    XDial_setNotchTarget(d, -2.0);
    XQ_CHECK(XDial_notchTarget(d) == -2.0);

    XClassDelete((XClass*)d);
}

/** @brief wrapping 值回绕（QDialPrivate::bound qdial.cpp:56-70）。
 *  @details 0..10 播种：wrapping 时 setValue(15) → 5、setValue(-1) → 9
 *           （Qt 实测）；关闭后 setValue(15) 钳到 max=10。 */
XQ_TEST(XDial, wrappingBound)
{
    XDial* d = XDial_create(NULL, 0);
    XQ_REQUIRE(d != NULL);
    XDial_setRange(d, 0, 10);
    XDial_setWrapping(d, true);

    XDial_setValue(d, 15);
    XQ_EQ_I64(XDial_value(d), 5);
    XDial_setValue(d, -1);
    XQ_EQ_I64(XDial_value(d), 9);
    /* 回绕对 setSliderPosition 同样生效（经 bound 虚槽全链路）。 */
    XAbstractSlider_setSliderPosition((XAbstractSlider*)d, 12);
    XQ_EQ_I64(XAbstractSlider_sliderPosition((XAbstractSlider*)d), 2);

    XDial_setWrapping(d, false);
    XDial_setValue(d, 15);
    XQ_EQ_I64(XDial_value(d), 10);

    XClassDelete((XClass*)d);
}

/** @brief 鼠标角度映射（QDialPrivate::valueFromPoint qdial.cpp:106-139）。 */
XQ_TEST(XDial, mouseAngleMapping)
{
    XDial* d = XDial_create(NULL, 0);
    XQ_REQUIRE(d != NULL);
    XWidget_resize((XWidget*)d, 100, 100);
    XDial_setRange(d, 0, 100);
    XDial_setValue(d, 0);

    /* 非 wrapping：300° 行程（4π/3 为零点）。Qt 实测 3 点=80、12 点=50、
     * 6 点=100（下死角钳到 max）、9 点=20。 */
    dl_click((XWidget*)d, 90, 50);
    XQ_EQ_I64(XDial_value(d), 80);
    dl_click((XWidget*)d, 50, 10);
    XQ_EQ_I64(XDial_value(d), 50);
    dl_click((XWidget*)d, 50, 90);
    XQ_EQ_I64(XDial_value(d), 100);
    dl_click((XWidget*)d, 10, 50);
    XQ_EQ_I64(XDial_value(d), 20);

    /* wrapping：整圆 360°（3π/2 为零点）。Qt 实测 3 点=75、6 点=100、
     * 12 点=50——旧实现 wrapping 也按 300° 折算，3 点会得 80。 */
    XDial_setWrapping(d, true);
    XDial_setValue(d, 0);
    dl_click((XWidget*)d, 90, 50);
    XQ_EQ_I64(XDial_value(d), 75);
    dl_click((XWidget*)d, 50, 90);
    XQ_EQ_I64(XDial_value(d), 100);
    dl_click((XWidget*)d, 50, 10);
    XQ_EQ_I64(XDial_value(d), 50);

    /* invertedAppearance：maximum - bound(v)。Qt 实测 3 点=20。 */
    XDial_setWrapping(d, false);
    XDial_setInvertedAppearance(d, true);
    XDial_setValue(d, 0);
    dl_click((XWidget*)d, 90, 50);
    XQ_EQ_I64(XDial_value(d), 20);
    XDial_setInvertedAppearance(d, false);

    /* 负下限：Qt 先平移到 0 起点再算角度。实测 -50..50 时
     * 3 点=30、12 点=0、6 点=50。 */
    XDial_setRange(d, -50, 50);
    XDial_setValue(d, 0);
    dl_click((XWidget*)d, 90, 50);
    XQ_EQ_I64(XDial_value(d), 30);
    dl_click((XWidget*)d, 50, 10);
    XQ_EQ_I64(XDial_value(d), 0);
    dl_click((XWidget*)d, 50, 90);
    XQ_EQ_I64(XDial_value(d), 50);

    XClassDelete((XClass*)d);
}

/** @brief 父子所有权：旋钮作为子控件随父析构释放。 */
XQ_TEST(XDial, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XDial* d = NULL;
    XQ_REQUIRE(host != NULL);
    d = XDial_create(host, 0);
    XQ_REQUIRE(d != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)d) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)d));
    XClassDelete((XClass*)host);
}

/* ==================== P0-17/P0-18：指示针按 upsideDown + sliderPosition ==================== */

/** @brief 用给定选项渲染一次 CC_Dial（100x100），返回 (x,y) 像素。
 *
 *  几何（xcs_drawDial，与 Qt calcArrow 同式）：radius=50、bigLine=8、
 *  br=(9,9,82,82)、圆心 (50,50)、br2=41、len=br2-bigLine-5=28、back=14。
 *  指针三角顶点 = 圆心 + len·(cosθ, -sinθ)，θ = 240° - 300°·frac：
 *  - upsideDown=1（QDial 缺省）+ frac=0 ⇒ θ=240°，针尖 (36,74)（左下）；
 *  - upsideDown=0（invertedAppearance=true）⇒ frac 镜像 ⇒ θ=300°，
 *    针尖 (64,74)（右下），与上一态关于竖直轴镜像。 */
static uint32_t dl_renderDial(XCommonStyle* style, XStyleOption* opt,
                              int x, int y)
{
    XImage image;
    XPainter painter;
    uint32_t px = 0u;
    XImage_init_ex(&image, 100, 100, XImageFormat_ARGB32);
    XImage_fill(&image, 0u);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, &image)) {
        XStyle_drawComplexControl((XStyle*)style, XStyleCC_Dial, opt,
                                  &painter, NULL);
        XPainter_end(&painter);
        px = XImage_pixel(&image, x, y);
    }
    XPainter_deinit(&painter);
    XClassDeinit(&image);
    return px;
}

/** @brief CC_Dial 指示针以 sliderPosition 求值（P0-18）、按 upsideDown
 *         翻转（P0-17）。探针取针体轴线上一点（(40,67) 及其镜像
 *         (60,67)）：Button 绿色 = 针体、Base 黑色 = 表盘底。 */
XQ_TEST(XDial, indicatorFollowsPositionAndUpsideDown)
{
    XCommonStyle* style = XCommonStyle_create();
    XStyleOption opt;
    XPalette palette;
    XColor base;
    XColor button;
    uint32_t px;
    XQ_REQUIRE(style != NULL);

    palette = XPalette_create();
    base = XColor_create_rgb(0, 0, 0, 255);
    button = XColor_create_rgb(0, 255, 0, 255);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Base, base);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Button, button);

    XStyleOption_init(&opt, XStyleCC_Dial);
    opt.m_rect = (XRect){0, 0, 100, 100};
    opt.m_state = XStyleState_Enabled;
    opt.m_palette = palette;
    opt.m_sliderMin = 0;
    opt.m_sliderMax = 100;
    opt.m_sliderValue = 0;
    opt.m_sliderPosition = 0;
    opt.m_sliderPositionSet = 1;
    opt.m_upsideDown = 1; /* QDial 缺省 = !invertedAppearance（qdial.cpp:92）。 */

    /* 位置 0、缺省方向：针指左下。 */
    XQ_EQ_I64(dl_renderDial(style, &opt, 40, 67), 0xFF00FF00u);
    XQ_EQ_I64(dl_renderDial(style, &opt, 60, 67), 0xFF000000u);

    /* 位置 100（value 仍 0）：针到右下（P0-18）。 */
    opt.m_sliderPosition = 100;
    px = dl_renderDial(style, &opt, 60, 67);
    XQ_EQ_I64(px, 0xFF00FF00u);
    px = dl_renderDial(style, &opt, 40, 67);
    XQ_EQ_I64(px, 0xFF000000u);
    opt.m_sliderPosition = 0;

    /* upsideDown=false（QDial invertedAppearance=true）：镜像（P0-17）。 */
    opt.m_upsideDown = 0;
    px = dl_renderDial(style, &opt, 60, 67);
    XQ_EQ_I64(px, 0xFF00FF00u);
    px = dl_renderDial(style, &opt, 40, 67);
    XQ_EQ_I64(px, 0xFF000000u);

    XClassDelete((XClass*)style);
}

/** @brief 整控件渲染两态差异：invertedAppearance 翻转后 (40,67)/(60,67)
 *         的探针互换（控件端填充 m_upsideDown = !invertedAppearance 的
 *         端到端证据）。 */
XQ_TEST(XDial, invertedAppearanceMirrorsIndicator)
{
    XDial* d = XDial_create(NULL, 0);
    XImage* normal;
    XImage* inverted;
    uint32_t leftNormal;
    uint32_t rightNormal;
    uint32_t leftInverted;
    uint32_t rightInverted;
    XQ_REQUIRE(d != NULL);
    XWidget_resize((XWidget*)d, 100, 100);
    XWidget_show((XWidget*)d);
    XDial_setRange(d, 0, 100);
    XDial_setValue(d, 0);

    normal = XWidget_grab((const XWidget*)d); /* 针指左下。 */
    XQ_REQUIRE(normal != NULL);
    XDial_setInvertedAppearance(d, true);
    inverted = XWidget_grab((const XWidget*)d); /* 针指右下。 */
    XQ_REQUIRE(inverted != NULL);

    leftNormal = XImage_pixel(normal, 40, 67);
    rightNormal = XImage_pixel(normal, 60, 67);
    leftInverted = XImage_pixel(inverted, 40, 67);
    rightInverted = XImage_pixel(inverted, 60, 67);

    /* 单态内：针只在一侧（Button #EFEFEF ≠ Base 白）。 */
    XQ_CHECK_MSG(leftNormal != rightNormal,
                 "缺省针应只指一侧：left=0x%08x right=0x%08x",
                 leftNormal, rightNormal);
    XQ_CHECK_MSG(leftInverted != rightInverted,
                 "翻转后针应只指另一侧：left=0x%08x right=0x%08x",
                 leftInverted, rightInverted);
    /* 两态互为镜像。 */
    XQ_CHECK_MSG(leftNormal == rightInverted &&
                 rightNormal == leftInverted,
                 "两态应互为镜像：ln=0x%08x rn=0x%08x li=0x%08x ri=0x%08x",
                 leftNormal, rightNormal, leftInverted, rightInverted);

    XClassDelete((XClass*)inverted);
    XClassDelete((XClass*)normal);
    XClassDelete((XClass*)d);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON */
