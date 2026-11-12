/**
 * @file       XCheckBoxAlignTest.c
 * @brief      XCheckBox × Qt 6.8 QCheckBox 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qcheckbox.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qcheckbox.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：checkable=true、tristate=false、
 *               checkState=Unchecked、isChecked=false，鼠标跟踪开启、
 *               前景角色 WindowText（qcheckbox.cpp:117-123）；
 *             - 三态点击循环 Unchecked→PartiallyChecked→Checked→Unchecked
 *               （qcheckbox.cpp:347-356；探针 c1/c2/c3 依次 state=1/2/0）；
 *             - setCheckState(PartiallyChecked) ⇒ isChecked()==true 且
 *               checkState()==Partial（探针 d1）；
 *             - 半选态 setChecked(false) ⇒ checkState 回落 Unchecked 并发
 *               checkStateChanged(Unchecked)（checkStateSet 清 noChange，
 *               探针 d2）；
 *             - setCheckState/setChecked 同值不重复发 checkStateChanged
 *               （探针 d4：四次同值调用只发 1 次）；
 *             - toggle() 是非虚槽 setChecked(!checked)，三态下只在
 *               Unchecked↔Checked 之间跳（qabstractbutton.cpp:843-851）；
 *             - stateChanged(int) 在 Qt 6.8 头文件里**存在**：
 *               qcheckbox.h:39-41 的 `#if QT_DEPRECATED_SINCE(6, 9)` 展开为
 *               `QT_VERSION_CHECK(6,9,0) > QT_DISABLE_DEPRECATED_UP_TO`
 *               ≈ 恒真（本机 g++ -E 实测有该声明）⇒ 本实现同时提供
 *               stateChanged(int)，且一次状态变化先发 checkStateChanged、
 *               后发 stateChanged（qcheckbox.cpp:324-338）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON

#include "XCheckBox.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XVarList.h"
#include "XPalette.h"
#include "XString.h"
#include "XClass.h"

/** @brief checkStateChanged 计数与最近状态。 */
static int g_xcbStateChanges;
static int g_xcbLastState;

static void xcb_stateChangedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, int, state);

    (void)receiver;
    ++g_xcbStateChanges;
    g_xcbLastState = state;
}

static void xcb_hook(XCheckBox* box)
{
    XObject_connect((XObject*)box,
                      (size_t)XCheckBox_checkStateChanged_signal(
                          NULL, XCheckState_Unchecked),
                      (XObject*)box, xcb_stateChangedSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 构造默认值 + 三态点击循环 ==================== */

XQ_TEST(XCheckBox, defaultsAndTristateCycle)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCheckBox* box;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 120);
    box = XCheckBox_create(host, 0);
    XQ_REQUIRE(box != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 120, 24);
    xcb_hook(box);

    /* 构造默认值（QCheckBoxPrivate 构造 + init）。 */
    XQ_CHECK(XCheckBox_isCheckable(box));
    XQ_CHECK(!XCheckBox_isTristate(box));
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);
    XQ_CHECK(!XCheckBox_isChecked(box));
    /* 前景角色 WindowText（qcheckbox.cpp:122），与基类缺省 ButtonText 不同。 */
    XQ_EQ_I64(XWidget_foregroundRole((XWidget*)box),
              XPaletteColorRole_WindowText);
    /* 鼠标跟踪开启（qcheckbox.cpp:119）。 */
    XQ_CHECK(XWidget_hasMouseTracking((XWidget*)box));
    /* 尺寸提示有内容（indicator 13 + 标题间距 + 文本占位）。 */
    {
        XSize hint = XWidget_sizeHint_base((const XWidget*)box);

        XQ_CHECK(hint.width > 0 && hint.height > 0);
    }

    /* 二态点击：Unchecked → Checked → Unchecked，各发一次
     * checkStateChanged。 */
    g_xcbStateChanges = 0;
    XCheckBox_click(box);
    XQ_CHECK(XCheckBox_isChecked(box));
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Checked);
    XQ_EQ_I64(g_xcbStateChanges, 1);
    XQ_EQ_I64(g_xcbLastState, XCheckState_Checked);
    XCheckBox_click(box);
    XQ_CHECK(!XCheckBox_isChecked(box));
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);
    XQ_EQ_I64(g_xcbStateChanges, 2);
    XQ_EQ_I64(g_xcbLastState, XCheckState_Unchecked);

    /* 三态点击循环（qcheckbox.cpp:347-356；探针 c1/c2/c3=1/2/0）。 */
    XCheckBox_setTristate(box, true);
    XQ_CHECK(XCheckBox_isTristate(box));
    g_xcbStateChanges = 0;
    XCheckBox_click(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_PartiallyChecked);
    XQ_EQ_I64(g_xcbLastState, XCheckState_PartiallyChecked);
    XCheckBox_click(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Checked);
    XQ_EQ_I64(g_xcbLastState, XCheckState_Checked);
    XCheckBox_click(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);
    XQ_EQ_I64(g_xcbLastState, XCheckState_Unchecked);
    XQ_EQ_I64(g_xcbStateChanges, 3);

    /* toggle() 非虚槽：三态下也只在 Unchecked↔Checked 间跳
     * （qabstractbutton.cpp:843-851）。 */
    XCheckBox_toggle(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Checked);
    XCheckBox_toggle(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);

    XClassDelete((XClass*)host);
}

/* ==================== ② setCheckState 语义 + 同值去重 ==================== */

XQ_TEST(XCheckBox, setCheckStateSemantics)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCheckBox* box;

    XQ_REQUIRE(host != NULL);
    box = XCheckBox_create(host, 0);
    XQ_REQUIRE(box != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 120, 24);
    xcb_hook(box);

    /* 传 PartiallyChecked 自动开三态并置 noChange；isChecked 为 true
     * （qcheckbox.cpp:359-370 setCheckState 的 state != Unchecked，
     * 探针 d1：checkState=1、checked=1）。 */
    g_xcbStateChanges = 0;
    XCheckBox_setCheckState(box, XCheckState_PartiallyChecked);
    XQ_CHECK(XCheckBox_isTristate(box));
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_PartiallyChecked);
    XQ_CHECK(XCheckBox_isChecked(box));
    XQ_EQ_I64(g_xcbStateChanges, 1);
    XQ_EQ_I64(g_xcbLastState, XCheckState_PartiallyChecked);

    /* 半选态 setChecked(false)：checkStateSet 清 noChange ⇒ 回落
     * Unchecked 并发布（探针 d2：state=0、checked=0）。 */
    g_xcbStateChanges = 0;
    XCheckBox_setChecked(box, false);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);
    XQ_CHECK(!XCheckBox_isChecked(box));
    XQ_EQ_I64(g_xcbStateChanges, 1);
    XQ_EQ_I64(g_xcbLastState, XCheckState_Unchecked);

    /* 同值调用不重复发信号（探针 d4：只发 1 次）。 */
    g_xcbStateChanges = 0;
    XCheckBox_setCheckState(box, XCheckState_Checked);
    XCheckBox_setCheckState(box, XCheckState_Checked);
    XCheckBox_setChecked(box, true);
    XCheckBox_setChecked(box, true);
    XQ_EQ_I64(g_xcbStateChanges, 1);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Checked);

    /* 关三态：PartiallyChecked 期间关闭后按真值报 Checked（Qt 只翻
     * tristate 位；探针 d3 在 d2 之后故报 0，此处按 Qt 语义走
     * 「part → 直接关三态」，checkState 由 noChange&&tristate 门失效
     * 后按 checked 报 Checked）。 */
    XCheckBox_setCheckState(box, XCheckState_PartiallyChecked);
    XCheckBox_setTristate(box, false);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Checked);
    XQ_CHECK(XCheckBox_isChecked(box));

    /* 禁用 checkable 后点击不改状态（基类 setChecked 早退）。 */
    XCheckBox_setCheckable(box, false);
    XQ_CHECK(!XCheckBox_isChecked(box));
    g_xcbStateChanges = 0;
    XCheckBox_click(box);
    XQ_EQ_I64(XCheckBox_checkState(box), XCheckState_Unchecked);
    XQ_EQ_I64(g_xcbStateChanges, 0);

    XClassDelete((XClass*)host);
}

/* ==================== ③ 助记符 '&'：显示/度量 + shortcut 文本 ==================== */

XQ_TEST(XCheckBox, mnemonicDisplayAndSizeHint)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCheckBox* withMnemonic;
    XCheckBox* plain;
    XSize withHint;
    XSize plainHint;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 260, 120);
    withMnemonic = XCheckBox_create(host, 0);
    plain = XCheckBox_create(host, 0);
    XQ_REQUIRE(withMnemonic != NULL && plain != NULL);
    XWidget_setGeometry((XWidget*)withMnemonic, 0, 0, 120, 24);
    XWidget_setGeometry((XWidget*)plain, 0, 30, 120, 24);

    XCheckBox_setText_2(withMnemonic, "&File");
    XCheckBox_setText_2(plain, "File");

    text = XAbstractButton_displayTextUtf8((XAbstractButton*)withMnemonic,
                                           &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    withHint = XWidget_sizeHint_base((const XWidget*)withMnemonic);
    plainHint = XWidget_sizeHint_base((const XWidget*)plain);
    XQ_EQ_I64(withHint.width, plainHint.width);
    XQ_EQ_I64(withHint.height, plainHint.height);

    shortcut = XAbstractButton_shortcut((XAbstractButton*)withMnemonic);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+F");

    XClassDelete((XClass*)host);
}

/* ==================== ④ stateChanged(int) 与 checkStateChanged 成对发射 ===== */

/** @brief P3-01：两路信号的计数、载荷与到达顺序。 */
static int g_xcbOrderedChecks;
static int g_xcbLegacyChanges;
static int g_xcbLegacyState;
static char g_xcbOrder[16];
static int g_xcbOrderLen;

static void xcb_orderPush(char tag)
{
    if (g_xcbOrderLen < (int)sizeof(g_xcbOrder) - 1) {
        g_xcbOrder[g_xcbOrderLen++] = tag;
        g_xcbOrder[g_xcbOrderLen] = '\0';
    }
}

static void xcb_orderCheckSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, int, state);

    (void)receiver;
    (void)state;
    ++g_xcbOrderedChecks;
    xcb_orderPush('c');
}

static void xcb_orderLegacySlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, int, state);

    (void)receiver;
    ++g_xcbLegacyChanges;
    g_xcbLegacyState = state;
    xcb_orderPush('s');
}

XQ_TEST(XCheckBox, stateChangedIntSignal)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCheckBox* box;

    XQ_REQUIRE(host != NULL);
    box = XCheckBox_create(host, 0);
    XQ_REQUIRE(box != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 120, 24);
    /* 两个信号令牌必须不同（同址会变成「一次发射打两条连接」）。 */
    XQ_CHECK((size_t)XCheckBox_stateChanged_signal(NULL, 0) !=
             (size_t)XCheckBox_checkStateChanged_signal(
                 NULL, XCheckState_Unchecked));
    XObject_connect((XObject*)box,
                      (size_t)XCheckBox_checkStateChanged_signal(
                          NULL, XCheckState_Unchecked),
                      (XObject*)box, xcb_orderCheckSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)box,
                      (size_t)XCheckBox_stateChanged_signal(NULL, 0),
                      (XObject*)box, xcb_orderLegacySlot,
                      XConnectionType_Direct);
    g_xcbOrderedChecks = 0;
    g_xcbLegacyChanges = 0;
    g_xcbLegacyState = -1;
    g_xcbOrder[0] = '\0';
    g_xcbOrderLen = 0;

    /* 三态点击三次：每次状态变化两路各发一次，且 checkStateChanged
     * **先**到（Qt 6.8.3 qcheckbox.cpp:324-338 的发射顺序），
     * stateChanged 的 int 载荷就是新状态的整数值。 */
    XCheckBox_setTristate(box, true);
    XCheckBox_click(box); /* → PartiallyChecked */
    XQ_EQ_STR(g_xcbOrder, "cs");
    XQ_EQ_I64(g_xcbOrderedChecks, 1);
    XQ_EQ_I64(g_xcbLegacyChanges, 1);
    XQ_EQ_I64(g_xcbLegacyState, XCheckState_PartiallyChecked);
    XCheckBox_click(box); /* → Checked */
    XQ_EQ_STR(g_xcbOrder, "cscs");
    XQ_EQ_I64(g_xcbOrderedChecks, 2);
    XQ_EQ_I64(g_xcbLegacyChanges, 2);
    XQ_EQ_I64(g_xcbLegacyState, XCheckState_Checked);
    XCheckBox_click(box); /* → Unchecked */
    XQ_EQ_STR(g_xcbOrder, "cscscs");
    XQ_EQ_I64(g_xcbOrderedChecks, 3);
    XQ_EQ_I64(g_xcbLegacyChanges, 3);
    XQ_EQ_I64(g_xcbLegacyState, XCheckState_Unchecked);

    /* 同值 setCheckState 不重复发（两路共用 publishedState 去重闸）。 */
    XCheckBox_setCheckState(box, XCheckState_Unchecked);
    XQ_EQ_I64(g_xcbOrderedChecks, 3);
    XQ_EQ_I64(g_xcbLegacyChanges, 3);

    /* 直接调用 stateChanged 信号入口只发本信号（不连发另一路）。 */
    g_xcbOrder[0] = '\0';
    g_xcbOrderLen = 0;
    (void)XCheckBox_stateChanged_signal(box, XCheckState_Checked);
    XQ_EQ_STR(g_xcbOrder, "s");
    XQ_EQ_I64(g_xcbLegacyChanges, 4);
    XQ_EQ_I64(g_xcbOrderedChecks, 3);

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON */
