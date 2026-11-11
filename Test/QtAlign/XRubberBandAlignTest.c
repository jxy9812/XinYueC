/**
 * @file       XRubberBandAlignTest.c
 * @brief      XRubberBand × Qt 6.8 QRubberBand 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qrubberband.h
 *             与 Qt 6.8.0 + Fusion + offscreen 实测：
 *             - 构造期属性 `WA_TransparentForMouseEvents`、
 *               `WA_NoSystemBackground`、focusPolicy `NoFocus`（qrubberband.cpp
 *               构造原文，实测三项均置位）；
 *             - `shape()` 往返 Line/Rectangle；
 *             - `move(x,y)` **保持尺寸**、`resize(w,h)` **保持位置**
 *               （qrubberband.h:54-56 的 inline 实现即 setGeometry 变体，
 *               实测 setGeometry(1,2,30,40)+move(5,6) → (5,6,30,40)）；
 *             - **构造即隐藏**：构造函数末尾 `setVisible(false)`
 *               （qrubberband.cpp:104-105，实测新建 QRubberBand(parent)
 *               的 isHidden()=1、isVisible()=0）；
 *             - 带普通控件父构造时 `isWindow()=0`、`windowType()=0`
 *               （qrubberband.cpp:97-98：`(p && p->windowType() !=
 *               Qt::Desktop) ? Qt::Widget : RUBBERBAND_WINDOW_TYPE`；
 *               实测 parent=&widget → isWindow=0/type=0，只有无父构造才是
 *               Qt::ToolTip 顶层窗）。故本适配的「父控件内子件」模型与
 *               Qt 带父构造一致，不是偏差。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XRUBBERBAND_ON

#include "XRubberBand.h"
#include "XWidget.h"
#include "XObject.h"

/** @brief 构造期属性与形状（qrubberband.cpp 构造原文 + qrubberband.h:26）。 */
XQ_TEST(XRubberBand, constructorAttributes)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRubberBand* rect = NULL;
    XRubberBand* line = NULL;
    XQ_REQUIRE(host != NULL);

    rect = XRubberBand_create(XRubberBandShape_Rectangle, host);
    line = XRubberBand_create(XRubberBandShape_Line, host);
    XQ_REQUIRE(rect != NULL && line != NULL);

    XQ_EQ_I64(XRubberBand_shape(rect), XRubberBandShape_Rectangle);
    XQ_EQ_I64(XRubberBand_shape(line), XRubberBandShape_Line);

    /* 鼠标穿透：框选期间按下/移动必须继续送达下层控件
     * （Qt 构造 `setAttribute(Qt::WA_TransparentForMouseEvents)`）。 */
    XQ_CHECK(XWidget_testAttribute((XWidget*)rect,
                                   XWidgetAttribute_TransparentForMouseEvents));
    /* 不绘制系统背景：框选矩形只画半透明填充+边框，透出底图。 */
    XQ_CHECK(XWidget_testAttribute((XWidget*)rect,
                                   XWidgetAttribute_NoSystemBackground));
    /* 永不接受键盘焦点（Qt 构造 `setFocusPolicy(Qt::NoFocus)`）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)rect),
              XWidgetFocusPolicy_NoFocus);

    /* 带父构造 ⇒ Qt::Widget（qrubberband.cpp:97-98；Qt 实测 isWindow=0），
     * 本适配同为父控件内子件：非窗口、父链归属正确。 */
    XQ_CHECK(!XWidget_isWindow((XWidget*)rect));
    XQ_CHECK(XWidget_parentWidget((XWidget*)rect) == host);
    /* 构造末尾 setVisible(false)（qrubberband.cpp:104-105）：新建即显式隐藏，
     * 只在拖动开始时由调用方 show()。 */
    XQ_CHECK(XWidget_isHidden((XWidget*)rect));
    XQ_CHECK(XWidget_isHidden((XWidget*)line));

    XClassDelete((XClass*)host); /* rect/line 随 host 析构。 */
}

/** @brief move/resize 语义（qrubberband.h:30-37 inline 语义）。 */
XQ_TEST(XRubberBand, moveResizeSemantics)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRubberBand* rb = NULL;
    XQ_REQUIRE(host != NULL);
    rb = XRubberBand_create(XRubberBandShape_Rectangle, host);
    XQ_REQUIRE(rb != NULL);

    /* Qt 实测：setGeometry(1,2,30,40) → geometry 原样（未显示也能读回）。 */
    XWidget_setGeometry((XWidget*)rb, 1, 2, 30, 40);
    XQ_EQ_I64(XWidget_x((XWidget*)rb), 1);
    XQ_EQ_I64(XWidget_y((XWidget*)rb), 2);
    XQ_EQ_I64(XWidget_width((XWidget*)rb), 30);
    XQ_EQ_I64(XWidget_height((XWidget*)rb), 40);

    /* move 只改位置、保持尺寸（Qt 实测 move(5,6) → (5,6,30,40)）。 */
    XWidget_move((XWidget*)rb, 5, 6);
    XQ_EQ_I64(XWidget_x((XWidget*)rb), 5);
    XQ_EQ_I64(XWidget_y((XWidget*)rb), 6);
    XQ_EQ_I64(XWidget_width((XWidget*)rb), 30);
    XQ_EQ_I64(XWidget_height((XWidget*)rb), 40);

    /* resize 只改尺寸、保持位置（qrubberband.h:34-37 等价 setGeometry(x,y,w,h)）。 */
    XWidget_resize((XWidget*)rb, 80, 60);
    XQ_EQ_I64(XWidget_x((XWidget*)rb), 5);
    XQ_EQ_I64(XWidget_y((XWidget*)rb), 6);
    XQ_EQ_I64(XWidget_width((XWidget*)rb), 80);
    XQ_EQ_I64(XWidget_height((XWidget*)rb), 60);

    XClassDelete((XClass*)host);
}

/** @brief 显隐与析构（橡皮筋是纯指示件，随父销毁）。 */
XQ_TEST(XRubberBand, visibilityAndOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRubberBand* rb = NULL;
    const XVector* children;
    XQ_REQUIRE(host != NULL);
    rb = XRubberBand_create(XRubberBandShape_Line, host);
    XQ_REQUIRE(rb != NULL);

    /* 构造即显式隐藏（Qt 构造 setVisible(false)），show 后才参与绘制。 */
    XQ_CHECK(XWidget_isHidden((XWidget*)rb));
    XWidget_show((XWidget*)rb);
    XQ_CHECK(!XWidget_isHidden((XWidget*)rb));
    XWidget_hide((XWidget*)rb);
    XQ_CHECK(XWidget_isHidden((XWidget*)rb));

    /* 只登记一个子件（橡皮筋不是分割器/主窗的特殊子树）。 */
    children = XObject_children((XObject*)host);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);

    XClassDelete((XClass*)host); /* rb 随 host 释放。 */
}

#endif /* XGUI_ON && XWIDGET_ON && XRUBBERBAND_ON */
