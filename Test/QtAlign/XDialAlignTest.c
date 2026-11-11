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

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON */
