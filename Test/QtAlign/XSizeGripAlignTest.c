/**
 * @file       XSizeGripAlignTest.c
 * @brief      XSizeGrip × Qt 6.8 QSizeGrip 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qsizegrip.h
 *             与 Qt 6.8.0 + Fusion + offscreen 实测：
 *             - 构造期尺寸策略 (Minimum, Fixed)、按角落设斜向光标
 *               （左上/右下 SizeFDiag、右上/左下 SizeBDiag，实测
 *               BR/TL=8=SizeFDiag、TR=7=SizeBDiag）；
 *             - 顶层窗口最大化/全屏时手柄自动隐藏，还原后自动恢复
 *               （qsizegrip.cpp:451-465 eventFilter + `_q_showIfNotHidden`，
 *               实测 showMaximized→hidden=1、showNormal→hidden=0，可往返）；
 *             - 拖动改顶层几何并锚定对角、受 min/max 约束
 *               （qsizegrip.cpp:355-401）。
 *
 *             `sizeHint()` 按 Qt 公式
 *             `style()->sizeFromContents(CT_SizeGrip, opt, QSize(13,13))`
 *             （qsizegrip.cpp:199-204）现场向样式查询，不再固定 16x16。
 *             本机 Qt 6.8.3 + offscreen 实测：QCommonStyle=13x13、
 *             QFusionStyle=17x17（+QSize(4,4)，qfusionstyle.cpp:3090-3092）、
 *             QWindowsStyle=13x13。X 侧按用户 2026-11-11 定案把
 *             `newSize += QSize(4,4)` 分支放进共享的 XCommonStyle
 *             sizeFromContents（XFusionStyle 不重载该函数、项目缺省样式
 *             是 XCommonStyle）⇒ 缺省 XCommonStyle/XFusionStyle 都得到
 *             17x17；与 Qt QCommonStyle（无分支=13x13）的差异属有意偏离，
 *             登记在 XCommonStyle.c 的 CT_SizeGrip 分支注释。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XSIZEGRIP_ON

#include "XSizeGrip.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XCursor.h"
#include "XCoreApplication.h"
#if XSTYLE_ON
#include "XStyle.h"
#include "XStyleOption.h"
#endif /* XSTYLE_ON */

/** @brief 构造期尺寸策略/尺寸提示/光标（qsizegrip.cpp:154-166 + 199-204）。 */
XQ_TEST(XSizeGrip, constructorDefaults)
{
    XWidget* host = XWidget_create(NULL, 0);
    XSizeGrip* grip = NULL;
    XWidgetSizePolicy sp;
    XSize hint;
    XCursor cur;
    XQ_REQUIRE(host != NULL);
    grip = XSizeGrip_create(host);
    XQ_REQUIRE(grip != NULL);

    /* sizeHint 按 Qt 公式现场查询样式（qsizegrip.cpp:199-204）：
     * sizeFromContents(CT_SizeGrip, opt, QSize(13,13))。用同一公式独立
     * 复算一遍作对照，避免把「恰好 13/16」当成对齐。 */
    hint = XWidget_sizeHint_base((XWidget*)grip);
#if XSTYLE_ON
    {
        XStyle* style = XWidget_style((const XWidget*)grip);
        XStyleOption opt;
        XSize content;
        XSize expect;
        XQ_REQUIRE(style != NULL);
        XStyleOption_init(&opt, XStyleCT_SizeGrip);
        opt.m_rect = XWidget_rect((const XWidget*)grip);
        opt.m_state = XWidget_isEnabled((const XWidget*)grip)
                          ? XStyleState_Enabled
                          : 0;
        XSize_init(&content, 13, 13);
        expect = XStyle_sizeFromContents(style, XStyleCT_SizeGrip, &opt,
                                         content);
        XQ_EQ_I64(hint.width, expect.width);
        XQ_EQ_I64(hint.height, expect.height);
        /* 缺省 XCommonStyle 的 CT_SizeGrip 分支 = Qt QFusionStyle 的
         * `newSize += QSize(4,4)`（qfusionstyle.cpp:3090-3092）⇒
         * 13+4 = 17x17（本机 Qt 6.8.3 Fusion 实测 17x17）。 */
        XQ_EQ_I64(hint.width, 17);
        XQ_EQ_I64(hint.height, 17);
    }
#else
    /* 无样式：xsg_sizeHintOf 回落 Qt 公式的入参 13x13。 */
    XQ_EQ_I64(hint.width, 13);
    XQ_EQ_I64(hint.height, 13);
#endif /* XSTYLE_ON */

    /* Qt 构造 `setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed)`
     * （qsizegrip.cpp:161，实测 h=1=Minimum、v=0=Fixed）。 */
    sp = XWidget_sizePolicy((XWidget*)grip);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&sp),
              XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&sp),
              XWidgetSizePolicy_Fixed);

    /* 构造期按当前角落设斜向调整光标；手柄在宿主左上 ⇒ 左上角
     * ⇒ SizeFDiag（qsizegrip.cpp:160-163 + 420-424，实测 TL=SizeFDiag）。 */
    cur = XWidget_cursor((XWidget*)grip);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SizeFDiag);
    XClassDeinit((XClass*)&cur);

    /* 手柄本身是子件、不抢焦点（Qt 未设焦点策略 ⇒ NoFocus 缺省）。 */
    XQ_CHECK(!XWidget_isWindow((XWidget*)grip));
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)grip),
              XWidgetFocusPolicy_NoFocus);

    XClassDelete((XClass*)host);
}

/** @brief 换角后光标跟随（qsizegrip.cpp:413-424 moveEvent）。 */
XQ_TEST(XSizeGrip, cornerCursorFollowsMove)
{
    XWidget* host = XWidget_create(NULL, 0);
    XSizeGrip* grip = NULL;
    XCursor cur;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    grip = XSizeGrip_create(host);
    XQ_REQUIRE(grip != NULL);

    /* 移到右下角：右下 ⇒ SizeFDiag（Qt 实测 BR=SizeFDiag）。 */
    XWidget_move((XWidget*)grip, 200 - 16, 100 - 16);
    cur = XWidget_cursor((XWidget*)grip);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SizeFDiag);
    XClassDeinit((XClass*)&cur);

    /* 移到右上角：右上 ⇒ SizeBDiag（Qt 实测 TR=SizeBDiag）。 */
    XWidget_move((XWidget*)grip, 200 - 16, 0);
    cur = XWidget_cursor((XWidget*)grip);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SizeBDiag);
    XClassDeinit((XClass*)&cur);

    /* 移到左下角：左下 ⇒ SizeBDiag。 */
    XWidget_move((XWidget*)grip, 0, 100 - 16);
    cur = XWidget_cursor((XWidget*)grip);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SizeBDiag);
    XClassDeinit((XClass*)&cur);

    XClassDelete((XClass*)host);
}

/** @brief 顶层最大化/全屏 ⇒ 自动隐藏，还原 ⇒ 自动恢复
 *         （qsizegrip.cpp:451-465 eventFilter；Qt 实测可往返）。 */
XQ_TEST(XSizeGrip, hidesWhenTopLevelMaximized)
{
    XWidget* host = XWidget_create(NULL, 0);
    XSizeGrip* grip = NULL;
    XEvent stateEvent;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    grip = XSizeGrip_create(host);
    XQ_REQUIRE(grip != NULL);
    XWidget_show((XWidget*)grip);

    /* 最大化：状态位落地 + 派发 WINDOW_STATE_CHANGE（手柄是宿主窗口的
     * 事件过滤器，收到即按状态隐藏自己）。 */
    XWidget_setWindowState(host, XWindowState_Maximized);
    XEvent_init(&stateEvent, XEVENT_TYPE_WINDOW_STATE_CHANGE);
    XCoreApplication_sendEvent((XObject*)host, &stateEvent);
    XQ_CHECK(XWidget_isHidden((XWidget*)grip));

    /* 还原：自动恢复显示（Qt 实测 showNormal 后 hidden=0）。 */
    XWidget_setWindowState(host, XWindowState_NoState);
    XEvent_init(&stateEvent, XEVENT_TYPE_WINDOW_STATE_CHANGE);
    XCoreApplication_sendEvent((XObject*)host, &stateEvent);
    XQ_CHECK(!XWidget_isHidden((XWidget*)grip));

    /* 用户**显式**隐藏的手柄不参与自动显隐（Qt 前置条件
     * `d->isExplicitlyHidden()`）：最大化事件不得把它弹出来。 */
    XWidget_hide((XWidget*)grip);
    XWidget_setWindowState(host, XWindowState_FullScreen);
    XEvent_init(&stateEvent, XEVENT_TYPE_WINDOW_STATE_CHANGE);
    XCoreApplication_sendEvent((XObject*)host, &stateEvent);
    XQ_CHECK(XWidget_isHidden((XWidget*)grip));

    XClassDelete((XClass*)host);
}

/** @brief 拖动改顶层几何并锚定对角（qsizegrip.cpp:355-401）。 */
XQ_TEST(XSizeGrip, dragResizesTopLevel)
{
    XWidget* host = XWidget_create(NULL, 0);
    XSizeGrip* grip = NULL;
    XMouseEvent press;
    XMouseEvent move;
    XMouseEvent release;
    XPoint pos;
    XPoint global;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    grip = XSizeGrip_create(host);
    XQ_REQUIRE(grip != NULL);
    /* 右下角：拖拽锚定左上角（qsizegrip.cpp:382-392）。 */
    XWidget_move((XWidget*)grip, 200 - 16, 100 - 16);

    pos.x = 0;
    pos.y = 0;
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    global.x = 500;
    global.y = 400;
    XMouseEvent_setGlobalPosition(&press, &global);
    XCoreApplication_sendEvent((XObject*)grip, (XEvent*)&press);
    XClassDeinit((XClass*)&press);

    /* 指针右下移 (+30,+10) ⇒ 顶层 230x110、左上角不动。 */
    XMouseEvent_init(&move, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    global.x = 530;
    global.y = 410;
    XMouseEvent_setGlobalPosition(&move, &global);
    XCoreApplication_sendEvent((XObject*)grip, (XEvent*)&move);
    XClassDeinit((XClass*)&move);

    XQ_EQ_I64(XWidget_width(host), 230);
    XQ_EQ_I64(XWidget_height(host), 110);
    XQ_EQ_I64(XWidget_x(host), 0);
    XQ_EQ_I64(XWidget_y(host), 0);

    /* 上界钳位（等价 Qt closestAcceptableSize 的 maximumSize 分支）。 */
    XWidget_setMaximumSize(host, 210, 105);
    XMouseEvent_init(&move, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    global.x = 600;
    global.y = 500;
    XMouseEvent_setGlobalPosition(&move, &global);
    XCoreApplication_sendEvent((XObject*)grip, (XEvent*)&move);
    XClassDeinit((XClass*)&move);
    XQ_EQ_I64(XWidget_width(host), 210);
    XQ_EQ_I64(XWidget_height(host), 105);

    /* 释放结束拖动会话（模块级状态必须收口，否则后续用例受污染）。 */
    XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XCoreApplication_sendEvent((XObject*)grip, (XEvent*)&release);
    XClassDeinit((XClass*)&release);

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XSIZEGRIP_ON */
