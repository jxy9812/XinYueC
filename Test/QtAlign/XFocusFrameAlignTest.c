/**
 * @file       XFocusFrameAlignTest.c
 * @brief      XFocusFrame × Qt 6.8 QFocusFrame 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qfocusframe.h
 *             与 Qt 6.8.0 + Fusion + offscreen 实测：
 *             - 构造期 `WA_TransparentForMouseEvents` + focusPolicy
 *               `NoFocus`（qfocusframe.cpp:124-129）；
 *             - `setWidget(child)` 把框体挂到目标的**父控件**上，几何 = 目标
 *               矩形按 PM_FocusFrameHMargin/VMargin 外扩（Fusion 实测
 *               hmargin=vmargin=2：100x30 的目标 → 框体 (-2,-2,104,34)）；
 *             - 目标为顶层窗口时不跟踪（`d->widget = nullptr` + hide，
 *               qfocusframe.cpp:174-205，实测 widget()==nullptr）；
 *             - `setWidget(nullptr)` 解除关联；
 *             - 框体与父控件矩形不相交时隐藏（QFocusFramePrivate::update
 *               qfocusframe.cpp:36-44 `if (parentWidget()->rect().intersects(
 *               geometry())) show(); else hide();`）。
 *
 *             跟随机制与 Qt 同构：`installEventFilter(目标)` 后由
 *             QFocusFrame::eventFilter（qfocusframe.cpp:243-300）处理
 *             Move/Resize（贴合）、Show（update+show）、Hide（hide）、
 *             ParentChange（update）、PaletteChange（setPalette）、
 *             ZOrderChange（raise）。本框架 XWidget_sendEvent 先跑过滤器，
 *             故这些事件同步生效（无轮询定时器）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFOCUSFRAME_ON

#include "XFocusFrame.h"
#include "XWidget.h"
#include "XObject.h"

/** @brief 构造期属性（qfocusframe.cpp:124-129）。 */
XQ_TEST(XFocusFrame, constructorAttributes)
{
    XWidget* host = XWidget_create(NULL, 0);
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    frame = XFocusFrame_create(host, 0);
    XQ_REQUIRE(frame != NULL);

    /* 框体贴合目标后叠在其上，不得截获目标的点击/焦点。 */
    XQ_CHECK(XWidget_testAttribute((XWidget*)frame,
                                   XWidgetAttribute_TransparentForMouseEvents));
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)frame),
              XWidgetFocusPolicy_NoFocus);
    XQ_CHECK(XFocusFrame_widget(frame) == NULL);

    XClassDelete((XClass*)host);
}

/** @brief setWidget 贴合与父级重挂（qfocusframe.cpp:37-58 updateSize）。 */
XQ_TEST(XFocusFrame, tracksChildWidget)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* target = NULL;
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    target = XWidget_create(host, 0);
    frame = XFocusFrame_create(NULL, 0); /* 独立顶层：setWidget 会把它挂到目标父级。 */
    XQ_REQUIRE(target != NULL && frame != NULL);

    XWidget_setGeometry(target, 10, 20, 100, 30);
    XFocusFrame_setWidget(frame, target);

    XQ_CHECK(XFocusFrame_widget(frame) == target);
    /* 框体挂到目标的父控件（qfocusframe.cpp:37 `q->setParent(frameParent)`）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)frame) == host);
    /* 几何 = 目标外扩 2px（Qt 实测 (-2,-2,104,34) 的同一口径）。 */
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 8);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 18);
    XQ_EQ_I64(XWidget_width((XWidget*)frame), 104);
    XQ_EQ_I64(XWidget_height((XWidget*)frame), 34);
    /* 目标可见且框体落在父控件矩形内 ⇒ 框体显示（不显式隐藏）。 */
    XQ_CHECK(!XWidget_isHidden((XWidget*)frame));

    XClassDelete((XClass*)host); /* target 与 frame（已挂到 host）随之释放。 */
}

/** @brief 目标为顶层窗口 ⇒ 不跟踪；setWidget(NULL) ⇒ 解除。 */
XQ_TEST(XFocusFrame, ignoresTopLevelTarget)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* target = NULL;
    XWidget* win = NULL;
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    target = XWidget_create(host, 0);
    win = XWidget_create(NULL, 0); /* 顶层窗口。 */
    frame = XFocusFrame_create(host, 0);
    XQ_REQUIRE(target != NULL && win != NULL && frame != NULL);

    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);

    /* qfocusframe.cpp:174-205：目标为窗口 → d->widget = nullptr + hide()。 */
    XFocusFrame_setWidget(frame, win);
    XQ_CHECK(XFocusFrame_widget(frame) == NULL);
    XQ_CHECK(XWidget_isHidden((XWidget*)frame));

    /* 解除关联（qfocusframe.cpp:203-206 的 widget 为空分支）。 */
    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);
    XFocusFrame_setWidget(frame, NULL);
    XQ_CHECK(XFocusFrame_widget(frame) == NULL);
    XQ_CHECK(XWidget_isHidden((XWidget*)frame));

    XClassDelete((XClass*)win);
    XClassDelete((XClass*)host);
}

/** @brief 目标销毁 ⇒ 自动解除；框体与父矩形不相交 ⇒ 隐藏。 */
XQ_TEST(XFocusFrame, destroyAndOutOfParentRect)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* target = NULL;
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    target = XWidget_create(host, 0);
    frame = XFocusFrame_create(NULL, 0);
    XQ_REQUIRE(target != NULL && frame != NULL);

    XWidget_setGeometry(target, 10, 10, 50, 20);
    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);

    /* 把目标挪出父控件矩形，再重新贴合：QFocusFramePrivate::update 的
     * 「父矩形 ∩ 框体几何」判据不成立 ⇒ 隐藏（qfocusframe.cpp:39-44）。 */
    XWidget_move(target, -500, -500);
    XFocusFrame_setWidget(frame, NULL);
    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);
    XQ_CHECK(XWidget_isHidden((XWidget*)frame));

    /* 目标销毁：destroyed 连接解除关联（qfocusframe.cpp:281-283
     * `case QEvent::Destroy: setWidget(nullptr)`）。 */
    XWidget_move(target, 10, 10);
    XFocusFrame_setWidget(frame, NULL);
    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);
    XClassDelete((XClass*)target);
    XQ_CHECK(XFocusFrame_widget(frame) == NULL);

    XClassDelete((XClass*)host); /* frame 已挂到 host，随之释放。 */
}

/** @brief 同值 setWidget 幂等（qfocusframe.cpp:161-162 `if (widget == d->widget) return;`）。 */
XQ_TEST(XFocusFrame, sameWidgetEarlyExit)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* target = NULL;
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    target = XWidget_create(host, 0);
    frame = XFocusFrame_create(host, 0);
    XQ_REQUIRE(target != NULL && frame != NULL);

    XWidget_setGeometry(target, 5, 6, 40, 20);
    XFocusFrame_setWidget(frame, target);
    /* 目标移动：事件过滤器同步贴合（qfocusframe.cpp:249-252 Move→updateSize），
     * 框体 = 目标 (60,70) 外扩 2 ⇒ (58,68)。 */
    XWidget_move(target, 60, 70);
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 58);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 68);
    /* 再设同一目标：Qt 早退（qfocusframe.cpp:161-162），几何不变。 */
    XFocusFrame_setWidget(frame, target);
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 58);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 68);

    XClassDelete((XClass*)host);
}

/** @brief 事件过滤器跟随：Move/Resize/Show/Hide/PaletteChange
 *         （qfocusframe.cpp:243-300 逐事件对位；本机 Qt 6.8.0 实测
 *         目标 move 后框体同步到新位置、hide 后框体 isVisible=0）。 */
XQ_TEST(XFocusFrame, followsTargetEvents)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* target = NULL;
    XFocusFrame* frame = NULL;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 300, 200);
    target = XWidget_create(host, 0);
    frame = XFocusFrame_create(NULL, 0);
    XQ_REQUIRE(target != NULL && frame != NULL);

    XWidget_setGeometry(target, 10, 20, 100, 30);
    XFocusFrame_setWidget(frame, target);
    XQ_CHECK(XFocusFrame_widget(frame) == target);
    /* 显示宿主：目标与框体进入生效可见态，后续 hide/show 才会真正派发
     * HIDE/SHOW 事件（隐藏父链下的显隐不产生事件，Qt 同口径）。 */
    XWidget_show(host);
    XQ_CHECK(XWidget_isVisible(target));
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 8);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 18);
    XQ_EQ_I64(XWidget_width((XWidget*)frame), 104);
    XQ_EQ_I64(XWidget_height((XWidget*)frame), 34);

    /* Move → updateSize（qfocusframe.cpp:249-252）。 */
    XWidget_move(target, 50, 60);
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 48);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 58);

    /* Resize → updateSize（同一分支）。 */
    XWidget_resize(target, 80, 40);
    XQ_EQ_I64(XWidget_width((XWidget*)frame), 84);
    XQ_EQ_I64(XWidget_height((XWidget*)frame), 44);

    /* Hide → 框体 hide（qfocusframe.cpp:254-256）。 */
    XWidget_hide(target);
    XQ_CHECK(XWidget_isHidden((XWidget*)frame));

    /* Show → update + show（qfocusframe.cpp:266-269）。 */
    XWidget_show(target);
    XQ_CHECK(!XWidget_isHidden((XWidget*)frame));
    XQ_EQ_I64(XWidget_x((XWidget*)frame), 48);
    XQ_EQ_I64(XWidget_y((XWidget*)frame), 58);

#if XPALETTE_ON
    /* PaletteChange → setPalette(目标调色板)（qfocusframe.cpp:271-273）。 */
    {
        XPalette pal = XWidget_palette(target);
        XPalette framePal;
        XColor red = XColor_create();
        XColor got;
        XColor_setRgb(&red, 255, 0, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Current,
                          XPaletteColorRole_Window, red);
        XWidget_setPalette(target, &pal);
        framePal = XWidget_palette((XWidget*)frame);
        got = XPalette_color(&framePal, XPaletteColorGroup_Current,
                             XPaletteColorRole_Window);
        XQ_EQ_I64(XColor_rgba(&got), XColor_rgba(&red));
    }
#endif /* XPALETTE_ON */

    XClassDelete((XClass*)host); /* target 与 frame（已挂到 host）随之释放。 */
}

#endif /* XGUI_ON && XWIDGET_ON && XFOCUSFRAME_ON */
