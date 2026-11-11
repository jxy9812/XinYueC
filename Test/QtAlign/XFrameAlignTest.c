/**
 * @file       XFrameAlignTest.c
 * @brief      XFrame × Qt 6.8 QFrame 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qframe.h
 *               （frameStyle/frameWidth/frameRect/sizeHint/Shape/Shadow）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen **实测矩阵**（逐格写进
 *               断言注释）：`Shape × Shadow × lineWidth/midLineWidth` 的
 *               frameWidth、HLine/VLine 的 sizeHint、setFrameRect 后的
 *               contentsMargins/contentsRect、setFrameStyle 的尺寸策略接管。
 *
 *             覆盖：缺省值、frameWidth 矩阵、frameRect↔contentsMargins
 *             互换、sizeHint 特例、尺寸策略接管与 OwnSizePolicy 保留、
 *             initStyleOption 契约、父子所有权。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON

#include "XFrame.h"
#include "XWidget.h"
#include "XObject.h"
#if XSTYLE_ON
#include "XStyle.h"
#include "XStyleOption.h"
#include "XFusionStyle.h"
#include "XCommonStyle.h"
#endif /* XSTYLE_ON */

/* ==================== 用例 ==================== */

/** @brief 缺省值与 HLine/VLine 的 sizeHint 特例（qframe.h:31-36）。 */
XQ_TEST(XFrame, defaultsAndSizeHint)
{
    XFrame* f = XFrame_create(NULL, 0);
    XSize hint;
    XQ_REQUIRE(f != NULL);

    /* Qt 6.8 实测：QFrame 缺省 frameStyle = NoFrame|Plain = 16、frameWidth=0、
     * sizeHint = (-1,-1)（无效尺寸，布局按策略处理）。 */
    XQ_EQ_I64(XFrame_frameStyle(f),
              (int)(XFrameShape_NoFrame | XFrameShadow_Plain));
    XQ_EQ_I64(XFrame_frameWidth(f), 0);
    hint = XFrame_sizeHint(f);
    XQ_CHECK(hint.width == -1 && hint.height == -1);

    /* Qt 6.8 实测：HLine → sizeHint (-1,3)、frameWidth = lineWidth(=1)；
     * VLine → sizeHint (3,-1)、frameWidth = 1。 */
    XFrame_setFrameShape(f, XFrameShape_HLine);
    hint = XFrame_sizeHint(f);
    XQ_CHECK(hint.width == -1 && hint.height == 3);
    XQ_EQ_I64(XFrame_frameWidth(f), 1);
    XQ_EQ_I64(XFrame_frameShape(f), XFrameShape_HLine);

    XFrame_setFrameShape(f, XFrameShape_VLine);
    hint = XFrame_sizeHint(f);
    XQ_CHECK(hint.width == 3 && hint.height == -1);
    XQ_EQ_I64(XFrame_frameWidth(f), 1);
    /* setFrameShape 保留阴影掩码（qframe.h:60-63）。 */
    XQ_EQ_I64(XFrame_frameShadow(f), XFrameShadow_Plain);

    XClassDelete((XClass*)f);
}

/** @brief frameWidth 矩阵（Qt 6.8 Fushion 实测逐格比对）。 */
XQ_TEST(XFrame, frameWidthMatrix)
{
    XFrame* f = XFrame_create(NULL, 0);
    XQ_REQUIRE(f != NULL);

    /* Box|Plain，lw=2 / mlw=1 → 2（Plain 只用 lineWidth）。 */
    XFrame_setFrameStyle(f, XFrameShape_Box | XFrameShadow_Plain);
    XFrame_setLineWidth(f, 2);
    XFrame_setMidLineWidth(f, 1);
    XQ_EQ_I64(XFrame_frameWidth(f), 2);

    /* Box|Raised，lw=1 / mlw=1 → 2*1+1 = 3（立体框合成两线+中线）。 */
    XFrame_setLineWidth(f, 1);
    XFrame_setFrameShadow(f, XFrameShadow_Raised);
    XQ_EQ_I64(XFrame_frameWidth(f), 3);

    /* Box|Sunken 同 Raised。 */
    XFrame_setFrameShadow(f, XFrameShadow_Sunken);
    XQ_EQ_I64(XFrame_frameWidth(f), 3);

    /* Panel 恒为 lineWidth（不受阴影影响）。 */
    XFrame_setFrameStyle(f, XFrameShape_Panel | XFrameShadow_Sunken);
    XFrame_setLineWidth(f, 2);
    XQ_EQ_I64(XFrame_frameWidth(f), 2);

    /* WinPanel 恒为 2。 */
    XFrame_setFrameShape(f, XFrameShape_WinPanel);
    XQ_EQ_I64(XFrame_frameWidth(f), 2);

    /* StyledPanel：Qt 6.8 `QFramePrivate::updateStyledFrameWidths`
     * （qframe.cpp:385-397）经 SE_ShapedFrameContents 反算；QCommonStyle 的
     * StyledPanel 分支转调 SE_FrameContents（= PM_DefaultFrameWidth 内缩，
     * qcommonstyle.cpp:3030-3038/2991-2996），故 frameWidth 恒等于当前
     * 样式的 PM_DefaultFrameWidth，与 lineWidth/midLineWidth/阴影无关。
     * 本机 Qt 6.8.0 + offscreen 实测：QCommonStyle/Windows=2、Fusion=1。 */
    XFrame_setFrameShape(f, XFrameShape_StyledPanel);
#if XSTYLE_ON
    {
        XStyle* style = XWidget_style((const XWidget*)f);
        int expect;
        XQ_REQUIRE(style != NULL);
        expect = XStyle_pixelMetric(style, XStylePM_DefaultFrameWidth, NULL);
        XQ_CHECK_MSG(XFrame_frameWidth(f) == expect,
                     "StyledPanel 应等于 PM_DefaultFrameWidth（实际 %d 期望 %d）",
                     XFrame_frameWidth(f), expect);
        /* 缺省样式 XCommonStyle 是 QCommonStyle 的逐值移植（=2）。 */
        XQ_EQ_I64(expect, 2);
        XQ_EQ_I64(XFrame_frameWidth(f), 2);
    }
#else
    XQ_EQ_I64(XFrame_frameWidth(f), 2); /* 无样式时的兜底值。 */
#endif /* XSTYLE_ON */

    /* NoFrame 恒 0。 */
    XFrame_setFrameShape(f, XFrameShape_NoFrame);
    XQ_EQ_I64(XFrame_frameWidth(f), 0);

    XClassDelete((XClass*)f);
}

#if XSTYLE_ON
/** @brief StyledPanel 的 frameWidth 随样式 PM_DefaultFrameWidth 变
 *         （Qt 6.8 实测：QCommonStyle=2、Fusion=1）。
 * @details 走 Qt 的对齐入口 `QWidget::setStyle` → `setStyle_helper` 末尾
 *          `sendEvent(StyleChange)` → `QFrame::event` 重算 frameWidth
 *          （qwidget.cpp:2693-2705）。测试宿主只建 XCoreApplication、
 *          没有 QApplication 级 allWidgets 广播，故用控件级换样式验证
 *          同一契约；同时避免改进程缺省样式污染其他用例。
 *          `setStyle(NULL)` 回落应用缺省样式（QWidget::style 语义）。 */
XQ_TEST(XFrame, styledPanelFollowsStyle)
{
    XFrame* f = XFrame_create(NULL, 0);
    XFusionStyle* fusion;
    XQ_REQUIRE(f != NULL);
    XFrame_setFrameShape(f, XFrameShape_StyledPanel);
    /* 缺省 XCommonStyle：PM_DefaultFrameWidth=2（Qt QCommonStyle 实测 2）。 */
    XQ_EQ_I64(XFrame_frameWidth(f), 2);

    fusion = XFusionStyle_create();
    XQ_REQUIRE(fusion != NULL);
    XWidget_setStyle((XWidget*)f, (XStyle*)fusion);
    /* Qt 6.8.0 + Fusion 实测 StyledPanel frameWidth=1。 */
    XQ_EQ_I64(XFrame_frameWidth(f), 1);
    /* 还原应用缺省样式族（控件级样式为借用，setStyle(NULL) 不释放它）。 */
    XWidget_setStyle((XWidget*)f, NULL);
    XQ_EQ_I64(XFrame_frameWidth(f), 2);

    XClassDelete((XClass*)fusion); /* 借用：控件未接管所有权。 */
    XClassDelete((XClass*)f);
}
#endif /* XSTYLE_ON */

/** @brief frameRect ↔ contentsMargins/contentsRect（qframe.h:71-72）。
 *  @details 期望值取自 Qt 6.8.0 实测（QFrame 100x50）：
 *           Box|Raised(fw=2) → contentsRect=(2,2,96,46)、margins=(2,2,2,2)；
 *           setFrameRect(5,5,80,40) → margins=(7,7,17,7)、
 *           contentsRect=(7,7,76,36)、frameRect 读回不变。 */
XQ_TEST(XFrame, frameRectAndContentsMargins)
{
    XFrame* f = XFrame_create(NULL, 0);
    XRect fr;
    XRect cr;
    XMargins m;
    XRect want;
    XQ_REQUIRE(f != NULL);

    XWidget_resize((XWidget*)f, 100, 50);
    XFrame_setFrameStyle(f, XFrameShape_Box | XFrameShadow_Raised);
    XFrame_setLineWidth(f, 1);

    /* 缺省 frameRect = 整个控件矩形（Qt 实测 0,0,100,50）。 */
    fr = XFrame_frameRect(f);
    XQ_CHECK(fr.x == 0 && fr.y == 0 && fr.width == 100 && fr.height == 50);

    /* 边框宽度进 contentsMargins（Qt 实测 2,2,2,2）。 */
    m = XWidget_contentsMargins((XWidget*)f);
    XQ_CHECK(m.left == 2 && m.top == 2 && m.right == 2 && m.bottom == 2);
    cr = XWidget_contentsRect((XWidget*)f);
    XQ_CHECK(cr.x == 2 && cr.y == 2);
    XQ_EQ_I64(cr.width, 96);
    XQ_EQ_I64(cr.height, 46);

    /* setFrameRect 后：frameRect 读回设置值，边距按「设置值内缩 fw」重算。 */
    XRect_init(&want, 5, 5, 80, 40);
    XFrame_setFrameRect(f, &want);
    fr = XFrame_frameRect(f);
    XQ_CHECK(fr.x == 5 && fr.y == 5 && fr.width == 80 && fr.height == 40);
    m = XWidget_contentsMargins((XWidget*)f);
    XQ_CHECK(m.left == 7 && m.top == 7);
    XQ_CHECK(m.right == 17 && m.bottom == 7);
    cr = XWidget_contentsRect((XWidget*)f);
    XQ_EQ_I64(cr.width, 76);
    XQ_EQ_I64(cr.height, 36);

    /* setFrameRect(NULL) 等价整控件矩形（qframe.h setFrameRect 无效矩形语义）。 */
    XFrame_setFrameRect(f, NULL);
    fr = XFrame_frameRect(f);
    XQ_CHECK(fr.x == 0 && fr.y == 0 && fr.width == 100 && fr.height == 50);

    XClassDelete((XClass*)f);
}

/** @brief 尺寸策略接管与 OwnSizePolicy 保留（Qt 6.8 实测 4/5 两行）。 */
XQ_TEST(XFrame, sizePolicyTakeover)
{
    XFrame* f = XFrame_create(NULL, 0);
    XWidgetSizePolicy sp;
    XQ_REQUIRE(f != NULL);

    /* Qt 实测：Box|Raised → Preferred/Preferred；HLine → Minimum/Fixed +
     * Line 控件类型（qframe.cpp setFrameStyle 的 SetShape 分支）。 */
    XFrame_setFrameStyle(f, XFrameShape_Box | XFrameShadow_Raised);
    sp = XWidget_sizePolicy((XWidget*)f);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&sp),
              XWidgetSizePolicy_Preferred);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&sp),
              XWidgetSizePolicy_Preferred);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&sp),
              (int)XWidgetSizePolicyControl_Frame);

    XFrame_setFrameShape(f, XFrameShape_HLine);
    sp = XWidget_sizePolicy((XWidget*)f);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&sp),
              XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&sp),
              XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&sp),
              (int)XWidgetSizePolicyControl_Line);

    /* Qt 实测：显式 setSizePolicy 后再改形状，用户策略保留（WA_WState_
     * OwnSizePolicy，qframe.cpp:setFrameStyle 首行判定）。 */
    XWidget_setSizePolicy((XWidget*)f, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    XFrame_setFrameShape(f, XFrameShape_VLine);
    sp = XWidget_sizePolicy((XWidget*)f);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&sp),
              XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&sp),
              XWidgetSizePolicy_Expanding);

    XClassDelete((XClass*)f);
}

/** @brief initStyleOption 契约（qframe.h:83 initStyleOption）。 */
XQ_TEST(XFrame, initStyleOption)
{
    XFrame* f = XFrame_create(NULL, 0);
    XFrameStyleOption opt;
    XQ_REQUIRE(f != NULL);

    XWidget_resize((XWidget*)f, 60, 30);
    XFrame_setFrameStyle(f, XFrameShape_Panel | XFrameShadow_Sunken);
    XFrame_setLineWidth(f, 3);
    XFrame_setMidLineWidth(f, 2);

    /* 输出结构体由实现内部清零（XFrame.c:XFrame_initStyleOption 首行
     * XMemset），调用方无需预置。 */
    XFrame_initStyleOption(f, &opt);
    XQ_EQ_I64(opt.m_frameShape, XFrameShape_Panel);
    XQ_EQ_I64(opt.m_frameShadow, XFrameShadow_Sunken);
    XQ_EQ_I64(opt.m_lineWidth, 3);
    XQ_EQ_I64(opt.m_midLineWidth, 2);
    XQ_CHECK(opt.m_sunken);
    XQ_CHECK(!opt.m_raised);
    XQ_CHECK(opt.m_rect.x == 0 && opt.m_rect.y == 0 &&
             opt.m_rect.width == 60 && opt.m_rect.height == 30);

    /* Raised 与 Sunken 互斥（qframe.h Shadow 枚举是互斥取值）。 */
    XFrame_setFrameShadow(f, XFrameShadow_Raised);
    XFrame_initStyleOption(f, &opt);
    XQ_CHECK(opt.m_raised && !opt.m_sunken);

    XClassDelete((XClass*)f);
}

/** @brief 父子所有权：XFrame 作为子控件随父析构释放。 */
XQ_TEST(XFrame, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XFrame* child = NULL;
    const XVector* children;
    XQ_REQUIRE(host != NULL);

    child = XFrame_create(host, 0);
    XQ_REQUIRE(child != NULL);

    XQ_CHECK(XWidget_parentWidget((XWidget*)child) == host);
    /* XFrame 是 XWidget 派生（qframe.h:16 `class QFrame : public QWidget`）。 */
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)child));
    children = XObject_children((XObject*)host);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);

    /* 子控件不手动删除：随 host 析构，漏删即运行器判 [LEAK]。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON */
