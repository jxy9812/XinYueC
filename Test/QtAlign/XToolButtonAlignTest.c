/**
 * @file       XToolButtonAlignTest.c
 * @brief      XToolButton × Qt 6.8 QToolButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qtoolbutton.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qtoolbutton.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：toolButtonStyle=ToolButtonIconOnly、
 *               arrowType=NoArrow、popupMode=DelayedPopup、autoRaise=false
 *               （普通父；父为 XToolBar 时 true，见 ④）、
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
 *             - 构造缺省 autoRaise：父为 XToolBar ⇒ true、普通父/NULL ⇒
 *               false（qtoolbutton.cpp:171-178）；
 *             - setDefaultAction 的镜像面：文本取 iconText（未设回落 text）、
 *               toolTip/statusTip/whatsThis、动作带菜单且原先无菜单 ⇒
 *               popupMode 自动切 MenuButtonPopup（qtoolbutton.cpp:922-955）。
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
#include "XMenu.h"
#if XTOOLBAR_ON
#include "XToolBar.h" /* P0-64：工具栏内的构造缺省值断言。 */
#endif

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
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_pressed_signal(NULL),
                      (XObject*)button, xtb_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_released_signal(NULL),
                      (XObject*)button, xtb_releasedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_clicked_signal(NULL, false),
                      (XObject*)button, xtb_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_toggled_signal(NULL, false),
                      (XObject*)button, xtb_toggledSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XToolButton_triggered_signal(NULL, NULL),
                      (XObject*)button, xtb_triggeredSlot,
                      XConnectionType_Direct);
    if (action)
        XObject_connect((XObject*)action,
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
    XQ_EQ_I64(XToolButton_arrowType(button), XArrowType_NoArrow);
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
    XToolButton_setArrowType(button, XArrowType_Down);
    XQ_EQ_I64(XToolButton_arrowType(button), XArrowType_Down);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_MenuButtonPopup);
    XToolButton_setAutoRaise(button, true);
    XQ_CHECK(XToolButton_autoRaise(button));
    XToolButton_setAutoRaise(button, false);
    XQ_CHECK(!XToolButton_autoRaise(button));

    /* sizeHint：NULL 安全、文本样式随文本增大（NULL 入口按基类
     * `*_base` 契约返回无效尺寸 (-1,-1)）。 */
    XQ_CHECK(XWidget_sizeHint_base(NULL).width == -1 &&
             XWidget_sizeHint_base(NULL).height == -1);
    XToolButton_setToolButtonStyle(button, XToolButtonStyle_TextOnly);
    {
        XSize emptyHint = XWidget_sizeHint_base((const XWidget*)button);

        XAbstractButton_setText_2((XAbstractButton*)button,
                                  "long tool button label");
        hint = XWidget_sizeHint_base((const XWidget*)button);
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
    plain = XWidget_sizeHint_base((const XWidget*)button);

    menu = XMenu_create();
    XQ_REQUIRE(menu != NULL);
    XToolButton_setMenu(button, menu);

    /* DelayedPopup 不预留指示宽（Qt 探针 27 == 无菜单 27）。 */
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    delayed = XWidget_sizeHint_base((const XWidget*)button);
    XQ_EQ_I64(delayed.width, plain.width);

    /* MenuButtonPopup 预留 PM_MenuButtonIndicator（探针 39 - 27 = 12）。 */
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    menuButton = XWidget_sizeHint_base((const XWidget*)button);
    XStyleOption_init(&opt, XStyleCT_ToolButton);
    opt.m_rect = XWidget_rect((XWidget*)button);
    indicator = XStyle_pixelMetric(XWidget_style((const XWidget*)button),
                                   XStylePM_MenuButtonIndicator, &opt);
    XQ_CHECK(indicator > 0);
    XQ_EQ_I64(menuButton.width, plain.width + indicator);

    /* arrowType 自身不加宽（探针 setArrowType(Down) 仍 27）。 */
    XToolButton_setMenu(button, NULL);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    XToolButton_setArrowType(button, XArrowType_Down);
    XQ_EQ_I64(XWidget_sizeHint_base((const XWidget*)button).width, plain.width);
    XToolButton_setArrowType(button, XArrowType_NoArrow);

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
    withActionDelayed = XWidget_sizeHint_base((const XWidget*)button);
    XQ_EQ_I64(withActionDelayed.width, plain.width);
    XToolButton_setPopupMode(button, XToolButtonPopupMode_MenuButtonPopup);
    withActionMenuButton = XWidget_sizeHint_base((const XWidget*)button);
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

/* ==================== ④ 构造缺省：工具栏内的 autoRaise（P0-64） ========== */

#if XTOOLBAR_ON
XQ_TEST(XToolButton, autoRaiseDefaultsInToolBar)
{
    XToolBar* bar = XToolBar_create(NULL, 0);
    XWidget* plainHost = XWidget_create(NULL, 0);
    XToolButton* inBar;
    XToolButton* inPlain;
    XToolButton* standalone;

    XQ_REQUIRE(bar != NULL && plainHost != NULL);
    XWidget_resize((XWidget*)bar, 200, 40);
    XWidget_resize(plainHost, 200, 40);
    inBar = XToolButton_create((XWidget*)bar, 0);
    inPlain = XToolButton_create(plainHost, 0);
    standalone = XToolButton_create(NULL, 0);
    XQ_REQUIRE(inBar != NULL && inPlain != NULL && standalone != NULL);

    /* Qt 6.8.3 QToolButtonPrivate::init（qtoolbutton.cpp:171-178）：
     * `if (qobject_cast<QToolBar*>(parent)) autoRaise = true; else false;`
     * —— 工具栏内新建的工具按钮缺省自动凸起，普通父（含 NULL）不是。
     * 旧实现 m_autoRaise 由 memset 保持 0 ⇒ 工具栏按钮返回 false、
     * 外观是凸起边框（与 Qt 相反）。 */
    XQ_CHECK_MSG(XToolButton_autoRaise(inBar),
                 "XToolBar 为父 ⇒ autoRaise 缺省 true");
    XQ_CHECK_MSG(!XToolButton_autoRaise(inPlain),
                 "普通父控件 ⇒ autoRaise 缺省 false");
    XQ_CHECK_MSG(!XToolButton_autoRaise(standalone),
                 "无父 ⇒ autoRaise 缺省 false");

    /* 构造期只是缺省值：显式 setAutoRaise 覆盖仍生效。 */
    XToolButton_setAutoRaise(inBar, false);
    XQ_CHECK(!XToolButton_autoRaise(inBar));

    XClassDelete((XClass*)standalone);
    XClassDelete((XClass*)plainHost);
    XClassDelete((XClass*)bar); /* inBar/inPlain 随父释放。 */
}
#endif /* XTOOLBAR_ON */

/* ==================== ⑤ 默认动作镜像面：iconText/提示族/popupMode（P0-16） */

XQ_TEST(XToolButton, defaultActionMirrorsIconTextTipsAndPopupMode)
{
    XWidget* host = XWidget_create(NULL, 0);
    XToolButton* button;
    XAction* action;
    XAction* action2;
    XAction* action3;
    XAction* action4;
    XMenu* menu;
    XMenu* menu2;
    const XString* tip;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    button = XToolButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    action = XAction_create();
    XQ_REQUIRE(action != NULL);

    /* 只设 text（iconText 未设）：按钮文本回落 text
     * （qtoolbutton.cpp:925-930 + QAction::iconText 的 strippedText 回落）。 */
    XAction_setText_2(action, "保存");
    XAction_setToolTip_2(action, "保存文件");
    XAction_setStatusTip_2(action, "把当前文档写入磁盘");
    XAction_setWhatsThis_2(action, "点这里保存当前文档");
    XToolButton_setDefaultAction(button, action);
    XQ_EQ_STR(xtb_text(button), "保存");
    tip = XWidget_toolTip((XWidget*)button);
    XQ_EQ_STR(tip ? XString_toUtf8(tip) : NULL, "保存文件");
    tip = XWidget_statusTip((XWidget*)button);
    XQ_EQ_STR(tip ? XString_toUtf8(tip) : NULL, "把当前文档写入磁盘");
    tip = XWidget_whatsThis((XWidget*)button);
    XQ_EQ_STR(tip ? XString_toUtf8(tip) : NULL, "点这里保存当前文档");
    /* 动作无菜单 ⇒ popupMode 不动（缺省 DelayedPopup）。 */
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_DelayedPopup);

    /* 设了 iconText ⇒ 按钮标题改取 iconText（动作 changed 触发重镜像）。 */
    XAction_setIconText_2(action, "保存图标文本");
    XQ_EQ_STR(xtb_text(button), "保存图标文本");

    /* 动作带菜单且按钮原先无菜单 ⇒ 自动切 MenuButtonPopup
     * （qtoolbutton.cpp:941-947）。 */
    menu = XMenu_create();
    XQ_REQUIRE(menu != NULL);
    action2 = XAction_create();
    XQ_REQUIRE(action2 != NULL);
    XAction_setText_2(action2, "更多");
    XAction_setMenu(action2, menu);
    XToolButton_setDefaultAction(button, action2);
    XQ_CHECK(XToolButton_defaultAction(button) == action2);
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_MenuButtonPopup);
    XQ_EQ_STR(xtb_text(button), "更多");

    /* 按钮原先**已有**菜单（setMenu）⇒ hadMenu 为 true，popupMode 不动
     * （Qt `if (action->menu() && !hadMenu)`）。 */
    XToolButton_setPopupMode(button, XToolButtonPopupMode_DelayedPopup);
    action3 = XAction_create();
    XQ_REQUIRE(action3 != NULL);
    XAction_setText_2(action3, "无菜单");
    XToolButton_setDefaultAction(button, action3); /* action3 无菜单 ⇒ 不切 */
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_DelayedPopup);
    menu2 = XMenu_create();
    XQ_REQUIRE(menu2 != NULL);
    XToolButton_setMenu(button, menu2); /* 按钮自己有菜单 ⇒ hasMenu=true */
    action4 = XAction_create();
    XQ_REQUIRE(action4 != NULL);
    XAction_setText_2(action4, "又带菜单");
    XAction_setMenu(action4, menu);
    XToolButton_setDefaultAction(button, action4);
    XQ_EQ_I64(XToolButton_popupMode(button),
              XToolButtonPopupMode_DelayedPopup);

    /* 解除关联：提示族与文本一起清空（动作镜像为覆盖语义）。 */
    XToolButton_setDefaultAction(button, NULL);
    XQ_EQ_STR(xtb_text(button), "");
    XQ_CHECK(XWidget_toolTip((XWidget*)button) == NULL);

    XToolButton_setMenu(button, NULL);
    XClassDelete((XClass*)action4);
    XClassDelete((XClass*)action3);
    XClassDelete((XClass*)action2);
    XClassDelete((XClass*)action);
    XClassDelete((XClass*)menu2);
    XClassDelete((XClass*)menu);
    XClassDelete((XClass*)host); /* button 随父释放。 */
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON */
