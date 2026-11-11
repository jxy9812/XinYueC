/**
 * @file       XToolButtonAlignTest.c
 * @brief      XToolButton × Qt 6.8 QToolButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtoolbutton.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qtoolbutton.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：toolButtonStyle=ToolButtonIconOnly、
 *               arrowType=NoArrow、popupMode=DelayedPopup、autoRaise=false、
 *               defaultAction=nullptr、menu=nullptr
 *               （qtoolbutton.cpp:98-110 QToolButtonPrivate::init）；
 *             - setDefaultAction 把动作的 text/checkable/checked/enabled
 *               镜像到按钮（qtoolbutton.cpp:913-955；探针 e0：
 *               checkable=1、checked=0）；
 *             - 点击时由 nextCheckState 触发动作（qtoolbutton.cpp:983-990），
 *               动作承担状态翻转；信号顺序（探针 e）：
 *               button.pressed → button.toggled → action.toggled →
 *               action.triggered → button.released → button.clicked；
 *             - checkStateSet 把按钮侧 setChecked 回写动作
 *               （qtoolbutton.cpp:971-977；探针 e2：action.checked 跟随变 0）；
 *             - 菜单指示宽只在 MenuButtonPopup + hasMenu 时预留
 *               （qtoolbutton.cpp:333-336；探针：无菜单 27、DelayedPopup
 *               27、MenuButtonPopup 39，arrowType 不加宽）；
 *             - hasMenu 含 defaultAction->menu()（qtoolbutton.cpp:73-78；
 *               探针：默认动作带菜单时 MenuButtonPopup 仍 +12）；
 *             - 文本助记符 '&' 的显示/度量与 shortcut 文本。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON

#include "XToolButton.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XStyle.h"
#include "XStyleOption.h"

#include <string.h>

/** @brief 信号顺序记录（p/t/o/a/r/c，按到达顺序拼接）。 */
static char g_xtbOrder[32];
static int g_xtbOrderLen;
static int g_xtbTriggered;
static const XAction* g_xtbLastAction;
static int g_xtbActionToggled;

static void xtb_reset(void)
{
    g_xtbOrder[0] = '\0';
    g_xtbOrderLen = 0;
    g_xtbTriggered = 0;
    g_xtbLastAction = NULL;
    g_xtbActionToggled = 0;
}

static void xtb_push(char tag)
{
    if (g_xtbOrderLen < (int)sizeof(g_xtbOrder) - 1) {
        g_xtbOrder[g_xtbOrderLen++] = tag;
        g_xtbOrder[g_xtbOrderLen] = '\0';
    }
}

static void xtb_pressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xtb_push('p');
}

static void xtb_releasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xtb_push('r');
}

static void xtb_clickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xtb_push('c');
}

static void xtb_toggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xtb_push('t');
}

static void xtb_triggeredSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, XAction*, action);

    (void)receiver;
    ++g_xtbTriggered;
    g_xtbLastAction = action;
    xtb_push('a');
}

static void xtb_actionToggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtbActionToggled;
    xtb_push('o');
}

/** @brief 接满按钮四路信号 + triggered 转发 + 动作 toggled。 */
static void xtb_hook(XToolButton* button, XAction* action)
{
    XObject_connect_1((XObject*)button,
                      (size_t)XAbstractButton_pressed_signal(NULL),
                      (XObject*)button, xtb_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      (size_t)XAbstractButton_released_signal(NULL),
                      (XObject*)button, xtb_releasedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      (size_t)XAbstractButton_clicked_signal(NULL, false),
                      (XObject*)button, xtb_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      (size_t)XAbstractButton_toggled_signal(NULL, false),
                      (XObject*)button, xtb_toggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)button,
                      (size_t)XToolButton_triggered_signal(NULL, NULL),
                      (XObject*)button, xtb_triggeredSlot,
                      XConnectionType_Direct);
    if (action)
        XObject_connect_1((XObject*)action,
                          (size_t)XAction_toggled_signal(NULL, false),
                          (XObject*)button, xtb_actionToggledSlot,
                          XConnectionType_Direct);
}

/** @brief 按钮文本的 UTF-8 借用指针（空串归一）。 */
static const char* xtb_text(const XToolButton* button)
{
    const XString* text = XAbstractButton_text((const XAbstractButton*)button);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;

    return utf8 ? utf8 : "";
}

/* ==================== ① 构造默认值 + 属性往返 ==================== */

XQ_TEST(XToolButton, constructorDefaultsAndSetters)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolButton* button;
    XSize hint;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 120);
    button = XToolButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 40, 24);

    /* QToolButtonPrivate::init 默认值（qtoolbutton.cpp:98-110）。 */
    XQ_EQ_I64(XToolButton_toolButtonStyle(button),
              XToolButtonStyle_IconOnly);
    XQ_EQ_I64(XToolButton_arrowType(button), XToolButtonArrowType_NoArrow);
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_DelayedPopup);
    XQ_CHECK(!XToolButton_autoRaise(button));
    XQ_CHECK(XToolButton_defaultAction(button) == NULL);
    XQ_CHECK(XToolButton_menu(button) == NULL);
    XQ_EQ_STR(xtb_text(button), "");
    XQ_CHECK(!XAbstractButton_isCheckable((XAbstractButton*)button));

    /* 外观属性往返。 */
    XToolButton_setToolButtonStyle(button, XToolButtonStyle_TextUnderIcon);
    XQ_EQ_I64(XToolButton_toolButtonStyle(button),
              XToolButtonStyle_TextUnderIcon);
    XToolButton_setArrowType(button, XToolButtonArrowType_Down);
    XQ_EQ_I64(XToolButton_arrowType(button), XToolButtonArrowType_Down);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_MenuButtonPopup);
    XToolButton_setAutoRaise(button, true);
    XQ_CHECK(XToolButton_autoRaise(button));
    XToolButton_setAutoRaise(button, false);
    XQ_CHECK(!XToolButton_autoRaise(button));

    /* sizeHint：NULL 安全、文本样式随文本增大。 */
    XQ_CHECK(XToolButton_sizeHint(NULL).width == 0 &&
             XToolButton_sizeHint(NULL).height == 0);
    XToolButton_setToolButtonStyle(button, XToolButtonStyle_TextOnly);
    {
        XSize emptyHint = XToolButton_sizeHint(button);

        XAbstractButton_setText_2((XAbstractButton*)button,
                                  "long tool button label");
        hint = XToolButton_sizeHint(button);
        XQ_CHECK(hint.width > emptyHint.width);
        XQ_CHECK(hint.height > 0);
    }

    XClassDelete((XClass*)host);
}

/* ==================== ② 默认动作：镜像 / 触发顺序 / 回写 ==================== */

XQ_TEST(XToolButton, defaultActionMirrorAndTrigger)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolButton* button;
    XAction* action;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 120);
    button = XToolButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    action = XAction_create();
    XQ_REQUIRE(action != NULL);

    XAction_setText_2(action, "动作");
    XAction_setCheckable(action, true);
    xtb_hook(button, action);

    /* setDefaultAction 镜像 text/checkable（qtoolbutton.cpp:913-955；
     * 探针 e0：checkable=1、checked=0）。 */
    XToolButton_setDefaultAction(button, action);
    XQ_CHECK(XToolButton_defaultAction(button) == action);
    XQ_EQ_STR(xtb_text(button), "动作");
    XQ_CHECK(XAbstractButton_isCheckable((XAbstractButton*)button));
    XQ_CHECK(!XAbstractButton_isChecked((XAbstractButton*)button));

    /* 动作选中态镜像到按钮（changed/toggled 桥）。 */
    XAction_setChecked(action, true);
    XQ_CHECK(XAbstractButton_isChecked((XAbstractButton*)button));
    XAction_setChecked(action, false);
    XQ_CHECK(!XAbstractButton_isChecked((XAbstractButton*)button));

    /* 点击：nextCheckState 触发动作，信号顺序
     * p(ressed) → t(oggled) → o(action toggled) → a(triggered) →
     * r(eleased) → c(licked)（本机 Qt 6.8 探针 e 的同一顺序）。 */
    xtb_reset();
    XAbstractButton_click((XAbstractButton*)button);
    XQ_EQ_STR(g_xtbOrder, "ptoarc");
    XQ_EQ_I64(g_xtbTriggered, 1); /* 每次点击只转发一次（无双桥重复触发） */
    XQ_CHECK(g_xtbLastAction == action);
    XQ_EQ_I64(g_xtbActionToggled, 1);
    /* 动作承担翻转：动作与按钮都变选中。 */
    XQ_CHECK(XAction_isChecked(action));
    XQ_CHECK(XAbstractButton_isChecked((XAbstractButton*)button));

    /* checkStateSet：按钮侧 setChecked 回写动作
     * （qtoolbutton.cpp:971-977；探针 e2：action.checked 跟随变 0）。 */
    XAbstractButton_setChecked((XAbstractButton*)button, false);
    XQ_CHECK(!XAction_isChecked(action));
    XQ_CHECK(!XAbstractButton_isChecked((XAbstractButton*)button));

    /* 动作禁用镜像到按钮；禁用后点击无信号。 */
    XAction_setEnabled(action, false);
    XQ_CHECK(!XWidget_isEnabled((XWidget*)button));
    xtb_reset();
    XAbstractButton_click((XAbstractButton*)button);
    XQ_EQ_STR(g_xtbOrder, "");
    XQ_EQ_I64(g_xtbTriggered, 0);

    /* 解除关联后恢复启用、按钮不再镜像动作。 */
    XToolButton_setDefaultAction(button, NULL);
    XQ_CHECK(XToolButton_defaultAction(button) == NULL);
    XQ_CHECK(XWidget_isEnabled((XWidget*)button));
    XQ_CHECK(!XAbstractButton_isCheckable((XAbstractButton*)button));

    /* 无默认动作时回落基类翻转：可选中按钮点击只翻自己，不触发动作。 */
    XAbstractButton_setCheckable((XAbstractButton*)button, true);
    xtb_reset();
    XAbstractButton_click((XAbstractButton*)button);
    XQ_CHECK(XAbstractButton_isChecked((XAbstractButton*)button));
    XQ_EQ_I64(g_xtbTriggered, 0);
    XQ_EQ_STR(g_xtbOrder, "ptrc");

    XClassDelete((XClass*)action);
    XClassDelete((XClass*)host);
}

/* ==================== ③ 菜单指示宽 + defaultAction 菜单（hasMenu） ==================== */

XQ_TEST(XToolButton, menuIndicatorAndDefaultActionMenu)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolButton* button;
    XMenu* menu;
    XMenu* actionMenu;
    XAction* action;
    XStyleOption opt;
    XSize plain;
    XSize delayed;
    XSize menuButton;
    XSize withActionDelayed;
    XSize withActionMenuButton;
    int indicator;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    button = XToolButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    XToolButton_setToolButtonStyle(button, XToolButtonStyle_TextOnly);
    XAbstractButton_setText_2((XAbstractButton*)button, "Hi");

    /* 基准：无菜单。 */
    plain = XToolButton_sizeHint(button);

    menu = XMenu_create();
    XQ_REQUIRE(menu != NULL);
    XToolButton_setMenu(button, menu);

    /* DelayedPopup 不预留指示宽（Qt 探针 27 == 无菜单 27）。 */
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    delayed = XToolButton_sizeHint(button);
    XQ_EQ_I64(delayed.width, plain.width);

    /* MenuButtonPopup 预留 PM_MenuButtonIndicator（探针 39 - 27 = 12）。 */
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    menuButton = XToolButton_sizeHint(button);
    XStyleOption_init(&opt, XStyleCT_ToolButton);
    opt.m_rect = XWidget_rect((XWidget*)button);
    indicator = XStyle_pixelMetric(XWidget_style((const XWidget*)button),
                                   XStylePM_MenuButtonIndicator, &opt);
    XQ_CHECK(indicator > 0);
    XQ_EQ_I64(menuButton.width, plain.width + indicator);

    /* arrowType 自身不加宽（探针 setArrowType(Down) 仍 27）。 */
    XToolButton_setMenu(button, NULL);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    XToolButton_setArrowType(button, XToolButtonArrowType_Down);
    XQ_EQ_I64(XToolButton_sizeHint(button).width, plain.width);
    XToolButton_setArrowType(button, XToolButtonArrowType_NoArrow);

    /* defaultAction->menu() 也算 hasMenu：XToolButton_menu() 仍是 NULL，
     * 但 MenuButtonPopup 必须预留指示宽（qtoolbutton.cpp:73-78）。 */
    actionMenu = XMenu_create();
    XQ_REQUIRE(actionMenu != NULL);
    action = XAction_create();
    XQ_REQUIRE(action != NULL);
    XAction_setText_2(action, "Hi");
    XAction_setMenu(action, actionMenu);
    XToolButton_setDefaultAction(button, action);
    XQ_CHECK(XToolButton_menu(button) == NULL);
    XQ_CHECK(XToolButton_defaultAction(button) == action);

    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    withActionDelayed = XToolButton_sizeHint(button);
    XQ_EQ_I64(withActionDelayed.width, plain.width);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    withActionMenuButton = XToolButton_sizeHint(button);
    XQ_EQ_I64(withActionMenuButton.width, plain.width + indicator);

    /* 文本助记符：显示/度量去 '&'，shortcut 文本 Alt+字符。 */
    XToolButton_setDefaultAction(button, NULL);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    XAbstractButton_setText_2((XAbstractButton*)button, "&File");
    display = NULL;
    text = XAbstractButton_displayTextUtf8((XAbstractButton*)button,
                                           &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    shortcut = XAbstractButton_shortcut((XAbstractButton*)button);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+F");

    XClassDelete((XClass*)action);
    XClassDelete((XClass*)actionMenu);
    XClassDelete((XClass*)menu);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON */
