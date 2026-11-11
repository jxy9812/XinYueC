/**
 * @file       XStyleOptionAlignTest.c
 * @brief      XStyleOption / XCommonStyle / XStyleSheetStyle 样式层
 *             × Qt 6.8 对齐契约测试（2026-11-11 短期收口三项）。
 * @details    对齐依据与关键实测（本机 Qt 6.8.3 + offscreen 探针）：
 *             ① `QStyleOptionSlider::tickPosition` 的字段类型是
 *                `QSlider::TickPosition`（qstyleoption.h:503，枚举按 int
 *                尺寸存储）——XStyleOption 的 m_sliderTickPosition 契约同
 *                为 int，外部原样值 >255 不得截断；绘制/命中只判
 *                `!= 0` 与低 2 位（&1/&2），位判定与字段宽度无关。
 *             ② `QSizeGrip::sizeHint()` = `sizeFromContents(CT_SizeGrip,
 *                opt, QSize(13,13))`（qsizegrip.cpp:199-204）；
 *                QFusionStyle::sizeFromContents 对 CT_SizeGrip 加
 *                `QSize(4,4)`（qfusionstyle.cpp:3090-3092）⇒ Fusion
 *                17x17、QCommonStyle/Windows 13x13（探针逐值确认）。
 *             ③ QStyleSheetStyle::subElementRect 的按钮三件套
 *                （qstylesheetstyle.cpp:6143-6170，按钮分支 6153-6168）：
 *                命中盒模型时 SE_PushButtonBevel =
 *                `rule.borderRect(opt->rect)`（原矩形 − margin），
 *                SE_PushButtonContents/FocusRect =
 *                `rule.contentsRect(opt->rect)`（再扣边框与 padding）。
 *                探针（按钮 100x40，`margin:6px; padding:3px;
 *                border:2px solid`）实测 bevel=(6,6,88,28)、
 *                contents/focus=(11,11,78,18)；纯 margin:6px 时
 *                bevel=(6,6,88,28)；纯 padding:3px 时 bevel=原矩形。
 *
 * @note       本文件只覆盖样式层；控件层 `XSizeGrip.sizeHint` 的 17x17
 *             断言在 XSizeGripAlignTest.c，`XPushButton.hitButton` 的
 *             事件路径在 XPushButtonAlignTest.c（各自独立回归）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XSTYLE_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON

#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XFusionStyle.h"
#include "XStyleSheetStyle.h"
#include "XWidget.h"
#include "XObject.h"
#include "XPushButton.h"
#include "XClass.h"
#include "XMemory.h"

/* ==================== ① m_sliderTickPosition 为 int（>255 不截断） ==================== */

/** @brief 字段类型/初始化/原样往返（qstyleoption.h:503 QSlider::TickPosition）。 */
XQ_TEST(XStyleOption, sliderTickPositionIsInt)
{
    XStyleOption opt;
    XStyleOptionSlider sopt;

    XStyleOption_init(&opt, XStyleCC_Slider);
    XQ_EQ_I64(opt.m_sliderTickPosition, 0); /* init 清零。 */
    /* 字段宽度 = int（Qt 的 tickPosition 字段按 int 存储；不是 uint8_t）。 */
    XQ_EQ_SIZE(sizeof(opt.m_sliderTickPosition), sizeof(int));

    /* >255 原样保留（旧 uint8_t 会把 300 截成 44、1000 截成 232）。 */
    opt.m_sliderTickPosition = 300;
    XQ_EQ_I64(opt.m_sliderTickPosition, 300);
    opt.m_sliderTickPosition = 1000;
    XQ_EQ_I64(opt.m_sliderTickPosition, 1000);
    opt.m_sliderTickPosition = 0x7FFFFFFF;
    XQ_EQ_I64(opt.m_sliderTickPosition, 0x7FFFFFFF);

    /* 滑块派生选项同一契约（XStyleOptionSlider 构造经基类 init 清零）。 */
    XStyleOptionSlider_init(&sopt, XStyleCC_Slider);
    XQ_EQ_I64(sopt.m_base.m_sliderTickPosition, 0);
    sopt.m_base.m_sliderTickPosition = 300;
    XQ_EQ_I64(sopt.m_base.m_sliderTickPosition, 300);
}

/* ==================== ② CT_SizeGrip：13x13 + QSize(4,4) = 17x17 ==================== */

/** @brief QSizeGrip 起算 13x13，样式 CT_SizeGrip 分支 +4 ⇒ 17x17
 *         （qfusionstyle.cpp:3090-3092；Qt 6.8.3 探针 Fusion=17x17）。 */
XQ_TEST(XCommonStyle, sizeGripFromContents)
{
    XCommonStyle* common;
    XFusionStyle* fusion;
    XStyleOption opt;
    XSize content;
    XSize out;

    common = XCommonStyle_create();
    fusion = XFusionStyle_create();
    XQ_REQUIRE(common != NULL);
    XQ_REQUIRE(fusion != NULL);

    XStyleOption_init(&opt, XStyleCT_SizeGrip);
    XSize_init(&content, 13, 13);

    /* QSizeGrip::sizeHint 的入参（qsizegrip.cpp:199-204）。 */
    out = XStyle_sizeFromContents((XStyle*)common, XStyleCT_SizeGrip, &opt,
                                  content);
    XQ_EQ_I64(out.width, 17);
    XQ_EQ_I64(out.height, 17);

    /* XFusionStyle 不重载 sizeFromContents（继承 XCommonStyle）⇒ 同为 17x17，
     * 与 Qt QFusionStyle 实测 17x17 一致。 */
    out = XStyle_sizeFromContents((XStyle*)fusion, XStyleCT_SizeGrip, &opt,
                                  content);
    XQ_EQ_I64(out.width, 17);
    XQ_EQ_I64(out.height, 17);

    XClassDelete((XClass*)fusion);
    XClassDelete((XClass*)common);
}

/* ==================== ③ QSS margin/padding/border 收窄按钮子元素矩形 ==================== */

/** @brief SE_PushButtonBevel/Contents/FocusRect 按 rule.borderRect/
 *         contentsRect 收窄（qstylesheetstyle.cpp:6143-6170 的
 *         6153-6168 分支），并经 XPushButton.hitButton 的真实调用链生效。 */
XQ_TEST(XStyleSheetStyle, pushButtonSubElementRectFollowsBox)
{
    XWidget* host;
    XPushButton* button;
    XStyleSheetStyle* style;
    XStyleOptionButton opt; /* 按钮选项（m_optionType==Button，对标
                             * qstyleoption_cast<QStyleOptionButton*>）。 */
    XRect bevel;
    XRect contents;
    XRect focus;
    XPoint inside;
    XPoint marginBand;
    XPoint outside;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    button = XPushButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 100, 40);
    style = XStyleSheetStyle_create();
    XQ_REQUIRE(style != NULL);

    /* 与 Qt 探针同一规则：margin:6px; padding:3px; border:2px solid。 */
    XQ_REQUIRE(XStyleSheetStyle_setStyleSheet(
        style,
        "XPushButton { margin: 6px; padding: 3px; border: 2px solid red; }"));

    XStyleOptionButton_init(&opt, XStyleCT_PushButton);
    opt.m_base.m_rect = XWidget_rect((const XWidget*)button);
    opt.m_base.m_state = XStyleState_Enabled;

    /* bevel = 原矩形 − margin：100-12=88、40-12=28，原点 (6,6)
     * （Qt 6.8.3 探针实测同值）。 */
    bevel = XStyle_subElementRect((XStyle*)style, XStyleSE_PushButtonBevel,
                                  &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(bevel.x, 6);
    XQ_EQ_I64(bevel.y, 6);
    XQ_EQ_I64(bevel.width, 88);
    XQ_EQ_I64(bevel.height, 28);

    /* contents/focus = 再扣边框 2 + padding 3：88-10=78、28-10=18，
     * 原点 (11,11)（Qt 探针实测同值）。 */
    contents = XStyle_subElementRect((XStyle*)style,
                                     XStyleSE_PushButtonContents,
                                     &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(contents.x, 11);
    XQ_EQ_I64(contents.y, 11);
    XQ_EQ_I64(contents.width, 78);
    XQ_EQ_I64(contents.height, 18);
    focus = XStyle_subElementRect((XStyle*)style,
                                  XStyleSE_PushButtonFocusRect,
                                  &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(focus.x, contents.x);
    XQ_EQ_I64(focus.y, contents.y);
    XQ_EQ_I64(focus.width, contents.width);
    XQ_EQ_I64(focus.height, contents.height);

    /* 只有 margin（无 padding/border）：bevel 仍收 6px，contents 与 bevel
     * 同值。注：Qt 的 contentsRect 还会再扣 nativeFrameWidth=1
     * （探针 7,7,86,26）；X 的 QSS 代理无 native 边框宽概念，属已登记
     * 偏差（见 XStyleSheetStyle.c 的函数注释 ②）。 */
    XQ_REQUIRE(XStyleSheetStyle_setStyleSheet(
        style, "XPushButton { margin: 6px; }"));
    bevel = XStyle_subElementRect((XStyle*)style, XStyleSE_PushButtonBevel,
                                  &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(bevel.x, 6);
    XQ_EQ_I64(bevel.y, 6);
    XQ_EQ_I64(bevel.width, 88);
    XQ_EQ_I64(bevel.height, 28);
    contents = XStyle_subElementRect((XStyle*)style,
                                     XStyleSE_PushButtonContents,
                                     &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(contents.x, 6);
    XQ_EQ_I64(contents.y, 6);
    XQ_EQ_I64(contents.width, 88);
    XQ_EQ_I64(contents.height, 28);

    /* 无盒模型（只有 color）⇒ 回落源样式的 bevel（== 原矩形），与
     * 重载前逐字节一致。 */
    XQ_REQUIRE(XStyleSheetStyle_setStyleSheet(
        style, "XPushButton { color: red; }"));
    bevel = XStyle_subElementRect((XStyle*)style, XStyleSE_PushButtonBevel,
                                  &opt.m_base, (const XWidget*)button);
    XQ_EQ_I64(bevel.x, 0);
    XQ_EQ_I64(bevel.y, 0);
    XQ_EQ_I64(bevel.width, 100);
    XQ_EQ_I64(bevel.height, 40);

    /* 真实调用链：把 QSS 样式装到按钮上，hitButton 按 bevel 判定。 */
    XQ_REQUIRE(XStyleSheetStyle_setStyleSheet(
        style, "XPushButton { margin: 6px; }"));
    XWidget_setStyle((XWidget*)button, (XStyle*)style);
    XPoint_init(&inside, 50, 20);  /* bevel (6,6,88,28) 内。 */
    XPoint_init(&marginBand, 2, 2); /* 控件内、margin 收窄后的 bevel 外。 */
    XPoint_init(&outside, 150, 60); /* 控件外。 */
    XQ_CHECK(XPushButton_hitButton(button, &inside));
    XQ_CHECK(!XPushButton_hitButton(button, &marginBand));
    XQ_CHECK(!XPushButton_hitButton(button, &outside));

    XWidget_setStyle((XWidget*)button, NULL);
    XClassDelete((XClass*)style);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XSTYLE_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */
