/* xgui_demo_apitest_buttons.c —— 控件 API 测试族：buttons。
 *
 * 覆盖控件（对标 Qt 6.8.3）：XAbstractButton / XPushButton / XCheckBox /
 * XRadioButton / XToolButton / XCommandLinkButton / XButtonGroup。
 *
 * 测试口径（见 xgui_demo_apitest.h 契约）：
 *  - 属性 setter/getter 往返一致；文档默认值确定的直接断言，不确定的
 *    写注释不硬断言（防误报）；
 *  - 信号断言经 XObject_event_base 直发合成鼠标/键盘事件（与真实输入
 *    同路径，坐标为控件本地坐标），或直连公开信号函数计数；
 *  - 无头语义：控件不 show 直接调 API（全程未调用 XWidget_show）；
 *  - 每条断言中文注释标对标 Qt 的哪个行为；渲染/视觉效果不在断言职责。
 */
#include "xgui_demo_apitest.h"

#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON
#include "XAbstractButton.h"
#include "XVector.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON
#include "XCheckBox.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON
#include "XRadioButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON
#include "XToolButton.h"
#include "XAction.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
#include "XCommandLinkButton.h"
#endif
#if XABSTRACTBUTTON_ON && XBUTTONGROUP_ON
#include "XButtonGroup.h"
#endif

#include <string.h>

#if XWIDGET_ON && XABSTRACTBUTTON_ON

/* ==================== 信号记录器（对标 QSignalSpy 的最小等价物） ==================== */

/** @brief XAbstractButton 四信号计数与最近参数/发射顺序（'P'/'T'/'R'/'C'）。 */
typedef struct BtnSigRec
{
    int  pressed;              /**< pressed 次数。 */
    int  released;             /**< released 次数。 */
    int  clicked;              /**< clicked 次数。 */
    int  toggled;              /**< toggled 次数。 */
    bool lastClickedChecked;   /**< 最近一次 clicked(bool) 参数。 */
    bool lastToggledChecked;   /**< 最近一次 toggled(bool) 参数。 */
    char order[24];            /**< 发射顺序串（单按钮场景）。 */
    int  orderLen;             /**< order 已写入长度。 */
} BtnSigRec;

static BtnSigRec g_btnSig;

static void btnsig_reset(void)
{
    memset(&g_btnSig, 0, sizeof(g_btnSig));
}

static void btnsig_pressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_btnSig.pressed;
    if (g_btnSig.orderLen < (int)sizeof(g_btnSig.order) - 1)
        g_btnSig.order[g_btnSig.orderLen++] = 'P';
}

static void btnsig_releasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_btnSig.released;
    if (g_btnSig.orderLen < (int)sizeof(g_btnSig.order) - 1)
        g_btnSig.order[g_btnSig.orderLen++] = 'R';
}

static void btnsig_clickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_btnSig.clicked;
    g_btnSig.lastClickedChecked = checked;
    if (g_btnSig.orderLen < (int)sizeof(g_btnSig.order) - 1)
        g_btnSig.order[g_btnSig.orderLen++] = 'C';
}

static void btnsig_toggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_btnSig.toggled;
    g_btnSig.lastToggledChecked = checked;
    if (g_btnSig.orderLen < (int)sizeof(g_btnSig.order) - 1)
        g_btnSig.order[g_btnSig.orderLen++] = 'T';
}

/** @brief 按钮四信号统一装配（sender==receiver 自连定式）。 */
static void btnsig_connect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XAbstractButton_pressed_signal),
                      sender, btnsig_pressedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAbstractButton_released_signal),
                      sender, btnsig_releasedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAbstractButton_clicked_signal),
                      sender, btnsig_clickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAbstractButton_toggled_signal),
                      sender, btnsig_toggledSlot, XConnectionType_Direct);
}

/** @brief XCheckBox checkStateChanged(XCheckState) 计数与最近状态。 */
static int g_cbStateChanged;
static int g_cbLastState;

static void btn_cbStateChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, st);
    ++g_cbStateChanged;
    g_cbLastState = st;
}

static void btn_cbReset(void)
{
    g_cbStateChanged = 0;
    g_cbLastState = -1;
}

/** @brief XToolButton triggered(XAction*) 计数与最近动作。 */
static int g_tbTriggered;
static XAction* g_tbLastAction;

static void btn_tbTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_tbTriggered;
    g_tbLastAction = (XAction*)act;
}

/** @brief XButtonGroup 八信号计数与最近参数。 */
typedef struct BGroupRec
{
    int btnClicked;            /**< buttonClicked 次数。 */
    int btnPressed;            /**< buttonPressed 次数。 */
    int btnReleased;           /**< buttonReleased 次数。 */
    int btnToggled;            /**< buttonToggled 次数。 */
    int idClicked;             /**< idClicked 次数。 */
    int idPressed;             /**< idPressed 次数。 */
    int idReleased;            /**< idReleased 次数。 */
    int idToggled;             /**< idToggled 次数。 */
    XAbstractButton* lastBtnClicked;  /**< 最近 buttonClicked 参数。 */
    XAbstractButton* lastBtnToggled;  /**< 最近 buttonToggled 参数。 */
    bool lastBtnToggledChecked;       /**< 最近 buttonToggled 选中参数。 */
    int  lastIdClicked;               /**< 最近 idClicked 参数。 */
    int  lastIdToggled;               /**< 最近 idToggled id 参数。 */
    bool lastIdToggledChecked;        /**< 最近 idToggled 选中参数。 */
} BGroupRec;

static BGroupRec g_grp;

static void btn_grpReset(void)
{
    memset(&g_grp, 0, sizeof(g_grp));
    g_grp.lastIdClicked = -100;
    g_grp.lastIdToggled = -100;
}

static void btn_grpBtnClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAbstractButton*, b);
    ++g_grp.btnClicked;
    g_grp.lastBtnClicked = (XAbstractButton*)b;
}

static void btn_grpBtnPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_grp.btnPressed;
}

static void btn_grpBtnReleasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_grp.btnReleased;
}

static void btn_grpBtnToggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, XAbstractButton*, b, bool, checked);
    ++g_grp.btnToggled;
    g_grp.lastBtnToggled = (XAbstractButton*)b;
    g_grp.lastBtnToggledChecked = checked;
}

static void btn_grpIdClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, id);
    ++g_grp.idClicked;
    g_grp.lastIdClicked = id;
}

static void btn_grpIdPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_grp.idPressed;
}

static void btn_grpIdReleasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_grp.idReleased;
}

static void btn_grpIdToggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, id, bool, checked);
    ++g_grp.idToggled;
    g_grp.lastIdToggled = id;
    g_grp.lastIdToggledChecked = checked;
}

static void btn_grpConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XButtonGroup_buttonClicked_signal),
                      sender, btn_grpBtnClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_buttonPressed_signal),
                      sender, btn_grpBtnPressedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_buttonReleased_signal),
                      sender, btn_grpBtnReleasedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_buttonToggled_signal),
                      sender, btn_grpBtnToggledSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_idClicked_signal),
                      sender, btn_grpIdClickedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_idPressed_signal),
                      sender, btn_grpIdPressedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_idReleased_signal),
                      sender, btn_grpIdReleasedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XButtonGroup_idToggled_signal),
                      sender, btn_grpIdToggledSlot, XConnectionType_Direct);
}

/* ==================== 合成事件注入（XObject_event_base 直发，与真实输入同路径） ==================== */

static void btn_injectMouse(XWidget* target, XEventType type, int x, int y,
                            bool leftHeld)
{
    XMouseEvent me;
    XPoint pos;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    /* m_buttons 为事件发生时按住的按键位掩码（对标 QMouseEvent::buttons）；
     * 拖拽移动场景需显式置左键按住。 */
    me.m_buttons = leftHeld ? XMouseButton_LeftButton : XMouseButton_NoButton;
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

static void btn_injectPress(XWidget* target, int x, int y)
{
    btn_injectMouse(target, XEVENT_TYPE_MOUSE_BUTTON_PRESS, x, y, true);
}

static void btn_injectRelease(XWidget* target, int x, int y)
{
    btn_injectMouse(target, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, x, y, false);
}

static void btn_injectMove(XWidget* target, int x, int y)
{
    btn_injectMouse(target, XEVENT_TYPE_MOUSE_MOVE, x, y, true);
}

static void btn_injectKey(XWidget* target, XEventType type, int key)
{
    XKeyEvent ke;
    XKeyEvent_init(&ke, type, key, 0);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */

/* ==================== 入口 ==================== */

int xapi_buttons_run(void)
{
    int failures = 0;

#if XWIDGET_ON && XABSTRACTBUTTON_ON

    /* ================================================================
     * 1. XAbstractButton：基类公共合同（直接实例化；C 无抽象类概念，
     *    对标 QAbstractButton 的属性/激活/信号行为经本对象验证）。
     * ================================================================ */
    {
        XAbstractButton b;
        const XString* t;
        const XString* sc;
        XString* s;
        XIcon icon;
        XSize isz;
        XSize want;

        XAbstractButton_init(&b, NULL, 0);
        btnsig_connect((XObject*)&b);

        /* ---- 默认值（Qt 6.8 QAbstractButton 构造默认） ---- */
        t = XAbstractButton_text(&b);
        XAPI_EXPECT(t != NULL && xapi_u8(t)[0] == '\0',
                    "AbstractButton 默认文本=空串");
        XAPI_EXPECT(!XAbstractButton_isCheckable(&b),
                    "AbstractButton 默认 checkable=false");
        XAPI_EXPECT(!XAbstractButton_isChecked(&b),
                    "AbstractButton 默认 checked=false");
        XAPI_EXPECT(!XAbstractButton_isDown(&b),
                    "AbstractButton 默认 down=false");
        XAPI_EXPECT(!XAbstractButton_autoRepeat(&b),
                    "AbstractButton 默认 autoRepeat=false");
        XAPI_EXPECT(XAbstractButton_autoRepeatDelay(&b) == 300,
                    "AbstractButton 默认 autoRepeatDelay=300");
        XAPI_EXPECT(XAbstractButton_autoRepeatInterval(&b) == 100,
                    "AbstractButton 默认 autoRepeatInterval=100");
        XAPI_EXPECT(!XAbstractButton_autoExclusive(&b),
                    "AbstractButton 默认 autoExclusive=false");
        XAPI_EXPECT(XAbstractButton_shortcut(&b) == NULL,
                    "AbstractButton 默认 shortcut 未设置=NULL");
        XAPI_EXPECT(XAbstractButton_group(&b) == NULL,
                    "AbstractButton 默认 group 未登记=NULL");
        isz = XAbstractButton_iconSize(&b);
        XAPI_EXPECT(isz.width == 0 && isz.height == 0,
                    "AbstractButton 默认 iconSize=(0,0) 未设置");
        icon = XAbstractButton_icon(&b);
        XAPI_EXPECT(XIcon_isNull(&icon),
                    "AbstractButton 默认 icon=空图标");
        XIcon_deinit_base(&icon);

        /* ---- text/setText 往返（QAbstractButton::setText/text） ---- */
        XAbstractButton_setText_2(&b, "确定");
        t = XAbstractButton_text(&b);
        XAPI_EXPECT(t && strcmp(xapi_u8(t), "确定") == 0,
                    "AbstractButton setText_2(UTF-8) 往返=确定");
        XAbstractButton_setText_2(&b, "确定");
        XAPI_EXPECT(XAbstractButton_text(&b) == t,
                    "AbstractButton 重复 setText 相同文本为无操作");
        s = XString_create_utf8("再试");
        XAbstractButton_setText(&b, s);
        XString_delete_base((XClass*)s);
        t = XAbstractButton_text(&b);
        XAPI_EXPECT(t && strcmp(xapi_u8(t), "再试") == 0,
                    "AbstractButton setText(XString) 深拷贝往返");
        XAbstractButton_setText_2(&b, NULL);
        t = XAbstractButton_text(&b);
        XAPI_EXPECT(t != NULL && xapi_u8(t)[0] == '\0',
                    "AbstractButton setText(NULL) 等价空串");

        /* ---- iconSize/setIconSize（QAbstractButton::setIconSize） ---- */
        XSize_init(&want, 24, 24);
        XAbstractButton_setIconSize(&b, &want);
        isz = XAbstractButton_iconSize(&b);
        XAPI_EXPECT(isz.width == 24 && isz.height == 24,
                    "AbstractButton setIconSize(24,24) 往返");
        XAbstractButton_setIconSize(&b, NULL);
        isz = XAbstractButton_iconSize(&b);
        XAPI_EXPECT(isz.width == 0 && isz.height == 0,
                    "AbstractButton setIconSize(NULL) 复位默认 (0,0)");
        XSize_init(&want, -3, 8);
        XAbstractButton_setIconSize(&b, &want);
        isz = XAbstractButton_iconSize(&b);
        XAPI_EXPECT(isz.width == 0 && isz.height == 0,
                    "AbstractButton 非正 iconSize 不被接受");
        /* setIcon 空图标=清除（QAbstractButton::setIcon 空图标语义） */
        XAbstractButton_setIcon(&b, NULL);
        icon = XAbstractButton_icon(&b);
        XAPI_EXPECT(XIcon_isNull(&icon),
                    "AbstractButton setIcon(NULL) 保持空图标");
        XIcon_deinit_base(&icon);

        /* ---- 非 checkable 时 setChecked/toggle 均不生效
         *      （Qt: setChecked 对非 checkable 提前返回） ---- */
        XAbstractButton_setChecked(&b, true);
        XAPI_EXPECT(!XAbstractButton_isChecked(&b),
                    "AbstractButton 非 checkable 时 setChecked 无效");
        XAbstractButton_toggle(&b);
        XAPI_EXPECT(!XAbstractButton_isChecked(&b),
                    "AbstractButton 非 checkable 时 toggle 无效");

        /* ---- checkable/checked/toggle 状态迁移与 toggled 信号 ---- */
        XAbstractButton_setCheckable(&b, true);
        XAPI_EXPECT(XAbstractButton_isCheckable(&b),
                    "AbstractButton setCheckable(true) 往返");
        btnsig_reset();
        XAbstractButton_setChecked(&b, true);
        XAPI_EXPECT(XAbstractButton_isChecked(&b),
                    "AbstractButton setChecked(true) 往返");
        XAPI_EXPECT(g_btnSig.toggled == 1 && g_btnSig.lastToggledChecked,
                    "AbstractButton setChecked 变化发 toggled(true)");
        XAbstractButton_setChecked(&b, true);
        XAPI_EXPECT(g_btnSig.toggled == 1,
                    "AbstractButton setChecked 同值不重发 toggled");
        XAbstractButton_toggle(&b);
        XAPI_EXPECT(!XAbstractButton_isChecked(&b) &&
                    g_btnSig.toggled == 2 && !g_btnSig.lastToggledChecked,
                    "AbstractButton toggle 反转并发 toggled(false)");
        /* setCheckable(false) 静默清除 checked 不发 toggled（头文件对标语义） */
        XAbstractButton_setCheckable(&b, false);
        XAPI_EXPECT(!XAbstractButton_isCheckable(&b) &&
                    !XAbstractButton_isChecked(&b) &&
                    g_btnSig.toggled == 2,
                    "AbstractButton setCheckable(false) 静默清 checked");
        XAbstractButton_setCheckable(&b, true);
        XAbstractButton_setChecked(&b, false);

        /* ---- setDown 只改视觉按下态，不发 pressed/released
         *      （QAbstractButton::setDown 文档语义） ---- */
        btnsig_reset();
        XAbstractButton_setDown(&b, true);
        XAPI_EXPECT(XAbstractButton_isDown(&b) &&
                    g_btnSig.pressed == 0 && g_btnSig.released == 0,
                    "AbstractButton setDown(true) 不发 pressed/released");
        XAbstractButton_setDown(&b, false);
        XAPI_EXPECT(!XAbstractButton_isDown(&b),
                    "AbstractButton setDown(false) 往返");

        /* ---- autoRepeat 属性往返（保存调用方整数，注册时才收敛） ---- */
        XAbstractButton_setAutoRepeat(&b, true);
        XAPI_EXPECT(XAbstractButton_autoRepeat(&b),
                    "AbstractButton setAutoRepeat(true) 往返");
        XAbstractButton_setAutoRepeat(&b, false);
        XAPI_EXPECT(!XAbstractButton_autoRepeat(&b),
                    "AbstractButton setAutoRepeat(false) 往返");
        XAbstractButton_setAutoRepeatDelay(&b, 500);
        XAPI_EXPECT(XAbstractButton_autoRepeatDelay(&b) == 500,
                    "AbstractButton setAutoRepeatDelay(500) 往返");
        XAbstractButton_setAutoRepeatDelay(&b, 0);
        XAPI_EXPECT(XAbstractButton_autoRepeatDelay(&b) == 0,
                    "AbstractButton setAutoRepeatDelay(0) 原样保存");
        XAbstractButton_setAutoRepeatDelay(&b, -50);
        XAPI_EXPECT(XAbstractButton_autoRepeatDelay(&b) == -50,
                    "AbstractButton 负延迟原样保存（注册定时器时才降级）");
        XAbstractButton_setAutoRepeatInterval(&b, 1000);
        XAPI_EXPECT(XAbstractButton_autoRepeatInterval(&b) == 1000,
                    "AbstractButton setAutoRepeatInterval(1000) 往返");
        XAbstractButton_setAutoRepeatDelay(&b, 300);
        XAbstractButton_setAutoRepeatInterval(&b, 100);

        /* ---- click() 激活流程：pressed→toggled→released→clicked
         *      （Qt QAbstractButton::click 信号顺序） ---- */
        btnsig_reset();
        XAbstractButton_click(&b);
        XAPI_EXPECT(g_btnSig.pressed == 1 && g_btnSig.released == 1 &&
                    g_btnSig.clicked == 1 && g_btnSig.toggled == 1,
                    "AbstractButton click() 四信号各发射一次");
        XAPI_EXPECT(g_btnSig.lastClickedChecked == true,
                    "AbstractButton clicked(bool) 参数=点击后 checked");
        XAPI_EXPECT(strcmp(g_btnSig.order, "PTRC") == 0,
                    "AbstractButton click() 顺序 pressed→toggled→released→clicked");
        XAPI_EXPECT(XAbstractButton_isChecked(&b),
                    "AbstractButton click() 翻转 checked");

        /* ---- 禁用按钮：click() 与鼠标事件全部无效（Qt 禁用态语义） ---- */
        XWidget_setEnabled((XWidget*)&b, false);
        btnsig_reset();
        XAbstractButton_click(&b);
        XAPI_EXPECT(g_btnSig.clicked == 0 && !XAbstractButton_isDown(&b),
                    "AbstractButton 禁用后 click() 无操作");
        XWidget_setGeometry((XWidget*)&b, 0, 0, 40, 24);
        btn_injectPress((XWidget*)&b, 5, 5);
        XAPI_EXPECT(!XAbstractButton_isDown(&b) && g_btnSig.pressed == 0,
                    "AbstractButton 禁用吞掉鼠标按下");
        XWidget_setEnabled((XWidget*)&b, true);

        /* ---- 鼠标按下/释放注入（XObject_event_base 直发合成事件） ---- */
        XAbstractButton_setChecked(&b, false);
        btnsig_reset();
        btn_injectPress((XWidget*)&b, 5, 5);
        XAPI_EXPECT(XAbstractButton_isDown(&b) && g_btnSig.pressed == 1,
                    "AbstractButton 鼠标按下进入 down 并发 pressed");
        btn_injectRelease((XWidget*)&b, 5, 5);
        XAPI_EXPECT(g_btnSig.released == 1 && g_btnSig.clicked == 1 &&
                    XAbstractButton_isChecked(&b),
                    "AbstractButton 控件内释放产生 clicked 并翻转选中");
        /* 控件外按下不置 down（hitButton 命中测试语义） */
        btnsig_reset();
        btn_injectPress((XWidget*)&b, 100, 100);
        XAPI_EXPECT(!XAbstractButton_isDown(&b) && g_btnSig.pressed == 0,
                    "AbstractButton 控件外按下不进入 down");
        /* 按住拖出控件：down 取消；随后控件外释放不产生 clicked
         * （Qt: 释放点在按钮外不触发点击） */
        btn_injectPress((XWidget*)&b, 5, 5);
        btn_injectMove((XWidget*)&b, 100, 100);
        XAPI_EXPECT(!XAbstractButton_isDown(&b),
                    "AbstractButton 拖出控件取消 down");
        btn_injectRelease((XWidget*)&b, 100, 100);
        XAPI_EXPECT(g_btnSig.clicked == 0,
                    "AbstractButton 控件外释放不产生 clicked");
        XAbstractButton_setChecked(&b, false);

        /* ---- 空格键激活（Qt: 空格按下/释放触发按钮点击） ---- */
        btnsig_reset();
        btn_injectKey((XWidget*)&b, XEVENT_TYPE_KEY_PRESS, XKey_Space);
        XAPI_EXPECT(XAbstractButton_isDown(&b) && g_btnSig.pressed == 1,
                    "AbstractButton 空格按下进入 down 并发 pressed");
        btn_injectKey((XWidget*)&b, XEVENT_TYPE_KEY_RELEASE, XKey_Space);
        XAPI_EXPECT(g_btnSig.clicked == 1 && XAbstractButton_isChecked(&b),
                    "AbstractButton 空格释放触发 clicked 并翻转");
        XAbstractButton_setChecked(&b, false);

        /* ---- animateClick：立即进入 down 并发一次 pressed
         *      （QAbstractButton::animateClick 文档语义） ---- */
        btnsig_reset();
        XAbstractButton_animateClick(&b);
        XAPI_EXPECT(XAbstractButton_isDown(&b) && g_btnSig.pressed == 1,
                    "AbstractButton animateClick 立即 down+pressed");
        XAbstractButton_animateClick(&b);
        /* 重复调用不重发 pressed 依赖 100ms 动画定时器注册成功；纯无头
         * 环境定时器体系不保证已起，此处不断言 pressed 计数（防误报）。 */
        XAPI_EXPECT(XAbstractButton_isDown(&b),
                    "AbstractButton 重复 animateClick 保持按下态");

        /* ---- shortcut 文本承载往返（触发体系未建，头文件已注明） ---- */
        XAbstractButton_setShortcut_2(&b, "Ctrl+S");
        sc = XAbstractButton_shortcut(&b);
        XAPI_EXPECT(sc && strcmp(xapi_u8(sc), "Ctrl+S") == 0,
                    "AbstractButton setShortcut_2 文本往返");
        s = XString_create_utf8("Ctrl+O");
        XAbstractButton_setShortcut(&b, s);
        XString_delete_base((XClass*)s);
        sc = XAbstractButton_shortcut(&b);
        XAPI_EXPECT(sc && strcmp(xapi_u8(sc), "Ctrl+O") == 0,
                    "AbstractButton setShortcut(XString) 文本往返");
        XAbstractButton_setShortcut(&b, NULL);
        XAPI_EXPECT(XAbstractButton_shortcut(&b) == NULL,
                    "AbstractButton setShortcut(NULL) 清除承载");

        /* ---- NULL 安全与信号标识（self=NULL 只返回标识不发射） ---- */
        XAPI_EXPECT(XAbstractButton_text(NULL) == NULL &&
                    !XAbstractButton_isChecked(NULL) &&
                    !XAbstractButton_isDown(NULL),
                    "AbstractButton NULL 对象查询安全返回");
        XAPI_EXPECT(XAbstractButton_pressed_signal(NULL) != NULL,
                    "AbstractButton NULL 时返回 pressed 信号标识");
        XAPI_EXPECT((size_t)XAbstractButton_clicked_signal(&b, false) ==
                    (size_t)XAbstractButton_clicked_signal(NULL, false),
                    "AbstractButton clicked 信号标识与实例无关");

        XAbstractButton_deinit_base(&b);
    }

#if XPUSHBUTTON_ON
    /* ================================================================
     * 2. XPushButton：QPushButton 特有属性 + 继承宏别名 + 鼠标注入。
     * ================================================================ */
    {
        XPushButton pb;
        XMenu menu;
        XPoint p;
        XSize sh;
        const XString* t;

        XPushButton_init(&pb, NULL, 0);
        XMenu_init(&menu, NULL);
        XWidget_setGeometry((XWidget*)&pb, 0, 0, 40, 24);
        btnsig_connect((XObject*)&pb);

        /* ---- QPushButton 默认值（Qt 6.8 QPushButton 构造默认） ---- */
        XAPI_EXPECT(!XPushButton_isFlat(&pb),
                    "PushButton 默认 flat=false");
        XAPI_EXPECT(!XPushButton_isDefault(&pb),
                    "PushButton 默认 default=false");
        XAPI_EXPECT(XPushButton_menu(&pb) == NULL,
                    "PushButton 默认 menu=NULL");
        /* autoDefault 三态默认 Auto：无父对话框链时生效值=false
         * （对齐 Qt 非对话框场景 autoDefault=false）。 */
        XAPI_EXPECT(!XPushButton_autoDefault(&pb),
                    "PushButton 无父对话框时 autoDefault 生效值=false");
        /* 继承默认值经宏别名可达（抽查基类合同）。 */
        XAPI_EXPECT(!XPushButton_isCheckable(&pb) &&
                    XPushButton_autoRepeatDelay(&pb) == 300 &&
                    XPushButton_autoRepeatInterval(&pb) == 100,
                    "PushButton 继承基类默认 checkable/delay/interval");

        /* ---- 特有属性往返 ---- */
        XPushButton_setFlat(&pb, true);
        XAPI_EXPECT(XPushButton_isFlat(&pb),
                    "PushButton setFlat(true) 往返");
        XPushButton_setFlat(&pb, false);
        XAPI_EXPECT(!XPushButton_isFlat(&pb),
                    "PushButton setFlat(false) 往返");
        XPushButton_setDefault(&pb, true);
        XAPI_EXPECT(XPushButton_isDefault(&pb),
                    "PushButton setDefault(true) 往返");
        /* 注：当前实现不做对话框内默认按钮唯一性收敛（头文件已注明待办），
         * Qt 中同对话框第二枚 setDefault(true) 会挤掉前者，此处不硬断言。 */
        XPushButton_setDefault(&pb, false);
        XPushButton_setAutoDefault(&pb, true);
        XAPI_EXPECT(XPushButton_autoDefault(&pb),
                    "PushButton setAutoDefault(true) 往返");
        XPushButton_setAutoDefault(&pb, false);
        XAPI_EXPECT(!XPushButton_autoDefault(&pb),
                    "PushButton setAutoDefault(false) 往返");

        /* ---- 继承 text/setDown 经宏别名（QPushButton : QAbstractButton） ---- */
        XPushButton_setText_2(&pb, "按钮");
        t = XPushButton_text(&pb);
        XAPI_EXPECT(t && strcmp(xapi_u8(t), "按钮") == 0,
                    "PushButton 宏别名 setText/text 往返");
        XPushButton_setDown(&pb, true);
        XAPI_EXPECT(XPushButton_isDown(&pb),
                    "PushButton 宏别名 setDown/isDown 往返");
        XPushButton_setDown(&pb, false);

        /* ---- menu/setMenu/showMenu（无平台弹层，showMenu 只置按下态） ---- */
        XPushButton_setMenu(&pb, &menu);
        XAPI_EXPECT(XPushButton_menu(&pb) == &menu,
                    "PushButton setMenu 借用指针往返");
        XPushButton_setMenu(&pb, NULL);
        XAPI_EXPECT(XPushButton_menu(&pb) == NULL,
                    "PushButton setMenu(NULL) 解除关联");
        XPushButton_showMenu(&pb);
        XAPI_EXPECT(!XPushButton_isDown(&pb),
                    "PushButton 无菜单时 showMenu 无操作");
        XPushButton_setMenu(&pb, &menu);
        XPushButton_showMenu(&pb);
        XAPI_EXPECT(XPushButton_isDown(&pb),
                    "PushButton showMenu 进入按下态（对标 Qt setDown(true)）");
        XPushButton_setMenu(&pb, NULL);
        XPushButton_setDown(&pb, false);

        /* ---- hitButton 按控件矩形命中（无主题资源时近似） ---- */
        XPoint_init(&p, 5, 5);
        XAPI_EXPECT(XPushButton_hitButton(&pb, &p),
                    "PushButton hitButton 控件内命中");
        XPoint_init(&p, 100, 5);
        XAPI_EXPECT(!XPushButton_hitButton(&pb, &p),
                    "PushButton hitButton 控件外不命中");
        XAPI_EXPECT(!XPushButton_hitButton(&pb, NULL),
                    "PushButton hitButton(NULL) 安全返回 false");

        /* ---- sizeHint/minimumSizeHint（Qt: minimumSizeHint=sizeHint） ---- */
        sh = XPushButton_sizeHint(&pb);
        XAPI_EXPECT(sh.width > 0 && sh.height > 0,
                    "PushButton sizeHint 空文本也有占位有效尺寸");
        {
            XSize msh = XPushButton_minimumSizeHint(&pb);
            XAPI_EXPECT(msh.width == sh.width && msh.height == sh.height,
                        "PushButton minimumSizeHint 等于 sizeHint");
        }
        {
            XSize sh2;
            XPushButton_setText_2(&pb, "更长的按钮文本内容");
            sh2 = XPushButton_sizeHint(&pb);
            XAPI_EXPECT(sh2.width > sh.width,
                        "PushButton 文本变化经 contentChanged 刷新 sizeHint");
        }
        XAPI_EXPECT(XPushButton_sizeHint(NULL).width == -1,
                    "PushButton sizeHint(NULL)=无效尺寸 (-1,-1)");

        /* ---- 鼠标注入点击：非 checkable 只发信号不翻转选中 ---- */
        XPushButton_setText_2(&pb, "按钮");
        btnsig_reset();
        btn_injectPress((XWidget*)&pb, 5, 5);
        btn_injectRelease((XWidget*)&pb, 5, 5);
        XAPI_EXPECT(g_btnSig.pressed == 1 && g_btnSig.released == 1 &&
                    g_btnSig.clicked == 1,
                    "PushButton 鼠标点击发 pressed/released/clicked");
        XAPI_EXPECT(!XPushButton_isChecked(&pb) && g_btnSig.toggled == 0,
                    "PushButton 非 checkable 点击不翻转选中");
        XAPI_EXPECT(g_btnSig.lastClickedChecked == false,
                    "PushButton clicked(bool) 参数=点击后 checked=false");

        XPushButton_deinit_base(&pb);
        XMenu_deinit_base(&menu);
    }
#endif /* XPUSHBUTTON_ON */

#if XCHECKBOX_ON
    /* ================================================================
     * 3. XCheckBox：三态模型 + checkStateChanged + 继承合同。
     * ================================================================ */
    {
        XCheckBox cb;
        XCheckBox cb2;
        XPoint p;
        XSize sh;
        int n;

        XCheckBox_init(&cb, NULL, 0);
        XWidget_setGeometry((XWidget*)&cb, 0, 0, 80, 24);
        btnsig_connect((XObject*)&cb);
        XObject_connect_1((XObject*)&cb,
                          XSignal(XCheckBox_checkStateChanged_signal),
                          (XObject*)&cb, btn_cbStateChangedSlot,
                          XConnectionType_Direct);

        /* ---- 默认值（Qt 6.8 QCheckBox 构造：checkable=true） ---- */
        XAPI_EXPECT(XCheckBox_isCheckable(&cb),
                    "CheckBox 默认 checkable=true");
        XAPI_EXPECT(!XCheckBox_isTristate(&cb),
                    "CheckBox 默认 tristate=false");
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Unchecked,
                    "CheckBox 默认 checkState=Unchecked");
        XAPI_EXPECT(!XCheckBox_isChecked(&cb),
                    "CheckBox 默认 isChecked=false");
        btn_cbReset();

        /* ---- 二态 click 翻转 + checkStateChanged 跟随 ---- */
        XCheckBox_click(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Checked &&
                    XCheckBox_isChecked(&cb),
                    "CheckBox 二态 click 翻转为 Checked");
        XAPI_EXPECT(g_cbStateChanged == 1 &&
                    g_cbLastState == (int)XCheckState_Checked,
                    "CheckBox 状态变化发 checkStateChanged(Checked)");
        XCheckBox_click(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Unchecked,
                    "CheckBox 二态再点回到 Unchecked");

        /* ---- setCheckState(PartiallyChecked) 自动开启 tristate
         *      （对标 Qt setCheckState 置 tristate+noChange） ---- */
        XCheckBox_setCheckState(&cb, XCheckState_PartiallyChecked);
        XAPI_EXPECT(XCheckBox_isTristate(&cb),
                    "CheckBox setCheckState(Partial) 自动开启 tristate");
        /* Qt 6.8.3 setCheckState 内部 setChecked(state != Qt::Unchecked)：
         * PartiallyChecked 同样置 checked 真值，isChecked()=true，仅
         * checkState() 区分 Partial（qcheckbox.cpp 口径；实现一致）。 */
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_PartiallyChecked &&
                    XCheckBox_isChecked(&cb),
                    "CheckBox PartiallyChecked 态 isChecked=true");

        /* ---- setCheckState 同值去重（publishedState 语义） ---- */
        XCheckBox_setCheckState(&cb, XCheckState_Checked);
        n = g_cbStateChanged;
        XCheckBox_setCheckState(&cb, XCheckState_Checked);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Checked &&
                    g_cbStateChanged == n,
                    "CheckBox setCheckState 同值不重发 checkStateChanged");

        /* ---- 三态 toggle 循环 (state+1)%3（对标 Qt nextCheckState） ---- */
        XCheckBox_toggle(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Unchecked,
                    "CheckBox 三态 toggle Checked→Unchecked");
        XCheckBox_toggle(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_PartiallyChecked,
                    "CheckBox 三态 toggle Unchecked→PartiallyChecked");

        /* ---- setTristate(false) 只关三态标志（Qt 6.8.3 setTristate 仅
         *      置 d->tristate，不触碰状态）：PartiallyChecked 期间 checked
         *      真值已为 true，checkState() 的 tristate&&noChange 门失效后
         *      按真值报 Checked，isChecked 保持 true ---- */
        XCheckBox_setTristate(&cb, false);
        XAPI_EXPECT(!XCheckBox_isTristate(&cb) &&
                    XCheckBox_checkState(&cb) == XCheckState_Checked &&
                    XCheckBox_isChecked(&cb),
                    "CheckBox setTristate(false) 只关标志，状态报 Checked");
        XCheckBox_setTristate(NULL, true); /* void 返回：NULL 仅验证不崩溃。 */

        /* ---- 三态点击循环 Unchecked→Partially→Checked→Unchecked ---- */
        XCheckBox_setTristate(&cb, true);
        XCheckBox_setCheckState(&cb, XCheckState_Unchecked);
        XCheckBox_click(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_PartiallyChecked,
                    "CheckBox 三态点击第 1 次→PartiallyChecked");
        XCheckBox_click(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Checked &&
                    XCheckBox_isChecked(&cb),
                    "CheckBox 三态点击第 2 次→Checked");
        XCheckBox_click(&cb);
        XAPI_EXPECT(XCheckBox_checkState(&cb) == XCheckState_Unchecked,
                    "CheckBox 三态点击第 3 次→Unchecked 回环");

        /* ---- setChecked 经 checkStateSet 同步三态标志（noChange 清除） ---- */
        XCheckBox_setChecked(&cb, true);
        XAPI_EXPECT(XCheckBox_isChecked(&cb) &&
                    XCheckBox_checkState(&cb) == XCheckState_Checked,
                    "CheckBox setChecked(true) 同步为 Checked");

        /* ---- 继承宏别名抽查：down 往返 ---- */
        XCheckBox_setDown(&cb, true);
        XAPI_EXPECT(XCheckBox_isDown(&cb),
                    "CheckBox 宏别名 setDown/isDown 往返");
        XCheckBox_setDown(&cb, false);

        /* ---- hitButton：整控件矩形（indicator∪文本区，头文件注） ---- */
        XPoint_init(&p, 6, 12);
        XAPI_EXPECT(XCheckBox_hitButton(&cb, &p),
                    "CheckBox hitButton indicator 区命中");
        XPoint_init(&p, 70, 12);
        XAPI_EXPECT(XCheckBox_hitButton(&cb, &p),
                    "CheckBox hitButton 文本/右侧空档同命中");
        XPoint_init(&p, 200, 5);
        XAPI_EXPECT(!XCheckBox_hitButton(&cb, &p),
                    "CheckBox hitButton 控件外不命中");

        /* ---- sizeHint/minimumSizeHint（Qt: minimumSizeHint=sizeHint） ---- */
        sh = XCheckBox_sizeHint(&cb);
        XAPI_EXPECT(sh.width > 0 && sh.height > 0,
                    "CheckBox sizeHint 有效（indicator+文本+边距）");
        {
            XSize msh = XCheckBox_minimumSizeHint(&cb);
            XAPI_EXPECT(msh.width == sh.width && msh.height == sh.height,
                        "CheckBox minimumSizeHint 等于 sizeHint");
        }
        XCheckBox_deinit_base(&cb);

        /* ---- 鼠标注入点击：Unchecked→Checked 完整输入链路 ---- */
        XCheckBox_init(&cb2, NULL, 0);
        XWidget_setGeometry((XWidget*)&cb2, 0, 0, 80, 24);
        btnsig_connect((XObject*)&cb2);
        /* 鼠标释放与 click() 同走 clickInternal→nextCheckState→checkStateSet
         * 发 checkStateChanged；此处须对本发送者补连计数槽。 */
        XObject_connect_1((XObject*)&cb2,
                          XSignal(XCheckBox_checkStateChanged_signal),
                          (XObject*)&cb2, btn_cbStateChangedSlot,
                          XConnectionType_Direct);
        btnsig_reset();
        btn_cbReset();
        btn_injectPress((XWidget*)&cb2, 6, 12);
        XAPI_EXPECT(XCheckBox_isDown(&cb2) && g_btnSig.pressed == 1,
                    "CheckBox 鼠标按下进入 down 并发 pressed");
        btn_injectRelease((XWidget*)&cb2, 6, 12);
        XAPI_EXPECT(XCheckBox_checkState(&cb2) == XCheckState_Checked &&
                    g_btnSig.clicked == 1 && g_btnSig.toggled == 1,
                    "CheckBox 鼠标释放翻转选中并发 clicked/toggled");
        XAPI_EXPECT(g_cbStateChanged == 1 &&
                    g_cbLastState == (int)XCheckState_Checked,
                    "CheckBox 鼠标链路发 checkStateChanged(Checked)");
        XCheckBox_deinit_base(&cb2);

        /* ---- NULL 安全（头文件：NULL 返回 Unchecked/false） ---- */
        XAPI_EXPECT(XCheckBox_checkState(NULL) == XCheckState_Unchecked &&
                    !XCheckBox_isTristate(NULL),
                    "CheckBox NULL 对象查询安全返回");
    }
#endif /* XCHECKBOX_ON */

#if XRADIOBUTTON_ON
    /* ================================================================
     * 4. XRadioButton：默认 checkable+autoExclusive、同父互斥组、
     *    唯一选中项反选守卫、鼠标注入互斥迁移。
     * ================================================================ */
    {
        XWidget* parent = XWidget_create(NULL, 0);
        XRadioButton* r1 = XRadioButton_create(parent, 0);
        XRadioButton* r2 = XRadioButton_create(parent, 0);
        XPoint p;
        XSize sh;

        XWidget_setGeometry((XWidget*)r1, 0, 0, 80, 20);
        XWidget_setGeometry((XWidget*)r2, 0, 0, 80, 20);
        btnsig_connect((XObject*)r1);
        btnsig_connect((XObject*)r2);

        /* ---- 默认值（对标 Qt 6.8 QRadioButtonPrivate::init） ---- */
        XAPI_EXPECT(XRadioButton_isCheckable(r1),
                    "RadioButton 默认 checkable=true");
        XAPI_EXPECT(XRadioButton_autoExclusive(r1),
                    "RadioButton 默认 autoExclusive=true");

        /* ---- text 往返 ---- */
        XRadioButton_setText_2(r1, "甲");
        XAPI_EXPECT(strcmp(xapi_u8(XRadioButton_text(r1)), "甲") == 0,
                    "RadioButton 宏别名 setText/text 往返");

        /* ---- setChecked 往返 + toggled；同值不重发 ---- */
        btnsig_reset();
        XRadioButton_setChecked(r1, true);
        XAPI_EXPECT(XRadioButton_isChecked(r1) && !XRadioButton_isChecked(r2),
                    "RadioButton setChecked(true) 往返");
        XAPI_EXPECT(g_btnSig.toggled == 1 && g_btnSig.lastToggledChecked,
                    "RadioButton setChecked 发 toggled(true)");
        XRadioButton_setChecked(r1, true);
        XAPI_EXPECT(g_btnSig.toggled == 1,
                    "RadioButton setChecked 同值不重发 toggled");

        /* ---- autoExclusive 互斥：新选中自动取消旧选中并发 toggled(false)
         *      （Qt autoExclusive 同父联动语义） ---- */
        btnsig_reset();
        XRadioButton_setChecked(r2, true);
        XAPI_EXPECT(XRadioButton_isChecked(r2) && !XRadioButton_isChecked(r1),
                    "RadioButton 互斥组新选中取消旧选中");
        /* r1 反选 toggled(false) + r2 选中 toggled(true) 共两次
         * （无互斥时只会有 r2 自己的一次）。 */
        XAPI_EXPECT(g_btnSig.toggled == 2,
                    "RadioButton 互斥取消旧按钮额外发 toggled(false)");

        /* ---- 互斥组唯一选中项不可 setChecked(false)
         *      （XAbstractButton_setChecked 头文件对标语义） ---- */
        XRadioButton_setChecked(r2, false);
        XAPI_EXPECT(XRadioButton_isChecked(r2),
                    "RadioButton 唯一选中项不可直接反选");

        /* ---- 关闭 autoExclusive 后守卫解除，可正常反选 ---- */
        XRadioButton_setAutoExclusive(r2, false);
        XRadioButton_setChecked(r2, false);
        XAPI_EXPECT(!XRadioButton_isChecked(r2) &&
                    !XRadioButton_autoExclusive(r2),
                    "RadioButton 关互斥后可反选");
        XRadioButton_setAutoExclusive(r2, true);
        XAPI_EXPECT(XRadioButton_autoExclusive(r2),
                    "RadioButton setAutoExclusive(true) 往返");

        /* ---- 鼠标注入点击 r1：点击路径同样驱动互斥迁移 ---- */
        XRadioButton_setChecked(r2, true); /* 让 r2 成为唯一选中项。 */
        btnsig_reset();
        btn_injectPress((XWidget*)r1, 6, 10);
        btn_injectRelease((XWidget*)r1, 6, 10);
        XAPI_EXPECT(XRadioButton_isChecked(r1) && !XRadioButton_isChecked(r2),
                    "RadioButton 鼠标点击选中 r1 并互斥取消 r2");
        XAPI_EXPECT(g_btnSig.clicked == 1 && g_btnSig.toggled == 2,
                    "RadioButton 点击发 clicked，互斥取消另发 toggled(false)");
        XAPI_EXPECT(g_btnSig.lastClickedChecked == true,
                    "RadioButton clicked(bool) 参数=true");

        /* ---- hitButton（indicator 外接矩形近似，控件内/外） ---- */
        XPoint_init(&p, 6, 10);
        XAPI_EXPECT(XRadioButton_hitButton(r1, &p),
                    "RadioButton hitButton 控件内命中");
        XPoint_init(&p, 200, 10);
        XAPI_EXPECT(!XRadioButton_hitButton(r1, &p),
                    "RadioButton hitButton 控件外不命中");
        XAPI_EXPECT(!XRadioButton_hitButton(r1, NULL),
                    "RadioButton hitButton(NULL) 安全返回 false");

        /* ---- sizeHint/minimumSizeHint ---- */
        sh = XRadioButton_sizeHint(r1);
        XAPI_EXPECT(sh.width > 0 && sh.height > 0,
                    "RadioButton sizeHint 有效（indicator+文本）");
        {
            XSize msh = XRadioButton_minimumSizeHint(r1);
            XAPI_EXPECT(msh.width == sh.width && msh.height == sh.height,
                        "RadioButton minimumSizeHint 等于 sizeHint");
        }

        /* ---- 继承抽查：autoRepeat 往返 ---- */
        XRadioButton_setAutoRepeat(r1, true);
        XRadioButton_setAutoRepeatDelay(r1, 200);
        XRadioButton_setAutoRepeatInterval(r1, 40);
        XAPI_EXPECT(XRadioButton_autoRepeat(r1) &&
                    XRadioButton_autoRepeatDelay(r1) == 200 &&
                    XRadioButton_autoRepeatInterval(r1) == 40,
                    "RadioButton 继承 autoRepeat 属性往返");
        XRadioButton_setAutoRepeat(r1, false);

        /* ---- 程序化 click() 在互斥组内转移选中 ---- */
        btnsig_reset();
        XRadioButton_click(r2);
        XAPI_EXPECT(XRadioButton_isChecked(r2) && !XRadioButton_isChecked(r1),
                    "RadioButton click() 互斥转移选中");
        XAPI_EXPECT(g_btnSig.clicked == 1,
                    "RadioButton click() 发 clicked");

        /* 父控件级联析构两枚单选按钮（堆对象经父子链释放）。 */
        XWidget_delete_base(parent);
    }
#endif /* XRADIOBUTTON_ON */

#if XTOOLBUTTON_ON
    /* ================================================================
     * 5. XToolButton：外观属性往返 + 默认动作镜像/触发转发。
     * ================================================================ */
    {
        XToolButton tb;
        XMenu menu;
        XAction* act;
        XSize sh;

        XToolButton_init(&tb, NULL, 0);
        XMenu_init(&menu, NULL);
        XWidget_setGeometry((XWidget*)&tb, 0, 0, 40, 24);
        btnsig_connect((XObject*)&tb);
        XObject_connect_1((XObject*)&tb, XSignal(XToolButton_triggered_signal),
                          (XObject*)&tb, btn_tbTriggeredSlot,
                          XConnectionType_Direct);

        /* ---- 默认值（Qt 6.8 QToolButton 构造默认） ---- */
        XAPI_EXPECT(XToolButton_toolButtonStyle(&tb) ==
                    XToolButtonStyle_IconOnly,
                    "ToolButton 默认 toolButtonStyle=IconOnly");
        XAPI_EXPECT(XToolButton_arrowType(&tb) == XToolButtonArrowType_NoArrow,
                    "ToolButton 默认 arrowType=NoArrow");
        XAPI_EXPECT(XToolButton_popupMode(&tb) ==
                    XToolButtonPopupMode_DelayedPopup,
                    "ToolButton 默认 popupMode=DelayedPopup");
        XAPI_EXPECT(!XToolButton_autoRaise(&tb),
                    "ToolButton 默认 autoRaise=false");
        XAPI_EXPECT(XToolButton_defaultAction(&tb) == NULL &&
                    XToolButton_menu(&tb) == NULL,
                    "ToolButton 默认无默认动作、无菜单");

        /* ---- 外观属性往返 ---- */
        XToolButton_setToolButtonStyle(&tb, XToolButtonStyle_TextUnderIcon);
        XAPI_EXPECT(XToolButton_toolButtonStyle(&tb) ==
                    XToolButtonStyle_TextUnderIcon,
                    "ToolButton setToolButtonStyle 往返");
        XToolButton_setArrowType(&tb, XToolButtonArrowType_Down);
        XAPI_EXPECT(XToolButton_arrowType(&tb) == XToolButtonArrowType_Down,
                    "ToolButton setArrowType 往返");
        XToolButton_setPopupMode(&tb, XToolButtonPopupMode_MenuButtonPopup);
        XAPI_EXPECT(XToolButton_popupMode(&tb) ==
                    XToolButtonPopupMode_MenuButtonPopup,
                    "ToolButton setPopupMode 往返");
        XToolButton_setAutoRaise(&tb, true);
        XAPI_EXPECT(XToolButton_autoRaise(&tb),
                    "ToolButton setAutoRaise(true) 往返");

        /* ---- setMenu/menu 往返（showMenu 依赖平台弹层，无头不调用） ---- */
        XToolButton_setMenu(&tb, &menu);
        XAPI_EXPECT(XToolButton_menu(&tb) == &menu,
                    "ToolButton setMenu 借用指针往返");
        XToolButton_setMenu(&tb, NULL);
        XAPI_EXPECT(XToolButton_menu(&tb) == NULL,
                    "ToolButton setMenu(NULL) 解除关联");

        /* ---- setDefaultAction 镜像 text/checkable（Qt 镜像语义） ---- */
        act = XAction_create();
        XAction_setText_2(act, "动作");
        XAction_setCheckable(act, true);
        XToolButton_setDefaultAction(&tb, act);
        XAPI_EXPECT(XToolButton_defaultAction(&tb) == act,
                    "ToolButton setDefaultAction 往返");
        XAPI_EXPECT(strcmp(xapi_u8(XAbstractButton_text(
                        (XAbstractButton*)&tb)), "动作") == 0,
                    "ToolButton 镜像动作文本");
        XAPI_EXPECT(XAbstractButton_isCheckable((XAbstractButton*)&tb),
                    "ToolButton 镜像动作 checkable");

        /* ---- 动作选中镜像到按钮（toggled 桥接） ---- */
        XAction_setChecked(act, true);
        XAPI_EXPECT(XAbstractButton_isChecked((XAbstractButton*)&tb),
                    "ToolButton 镜像动作选中状态");

        /* ---- 按钮点击触发动作并转发 triggered(action) ---- */
        btnsig_reset();
        g_tbTriggered = 0;
        g_tbLastAction = NULL;
        XAbstractButton_click((XAbstractButton*)&tb);
        XAPI_EXPECT(g_tbTriggered == 1 && g_tbLastAction == act,
                    "ToolButton 点击触发动作并转发 triggered(action)");
        XAPI_EXPECT(!XAction_isChecked(act) &&
                    !XAbstractButton_isChecked((XAbstractButton*)&tb),
                    "ToolButton 由动作承担状态翻转（Qt 语义）");
        XAPI_EXPECT(g_btnSig.clicked == 1,
                    "ToolButton 点击仍发射 clicked");

        /* ---- 动作禁用镜像到按钮；禁用后点击无信号 ---- */
        XAction_setEnabled(act, false);
        XAPI_EXPECT(!XWidget_isEnabled((XWidget*)&tb),
                    "ToolButton 镜像动作禁用");
        btnsig_reset();
        g_tbTriggered = 0;
        XAbstractButton_click((XAbstractButton*)&tb);
        XAPI_EXPECT(g_tbTriggered == 0 && g_btnSig.clicked == 0,
                    "ToolButton 禁用后点击不触发动作");

        /* ---- 解除关联：按钮恢复启用（镜像 NULL 分支） ---- */
        XToolButton_setDefaultAction(&tb, NULL);
        XAPI_EXPECT(XToolButton_defaultAction(&tb) == NULL &&
                    XWidget_isEnabled((XWidget*)&tb),
                    "ToolButton 解除动作恢复默认启用");

        /* ---- 动作销毁自动解绑（头文件语义） ---- */
        XToolButton_setDefaultAction(&tb, act);
        XAction_delete_base((XClass*)act);
        XAPI_EXPECT(XToolButton_defaultAction(&tb) == NULL,
                    "ToolButton 动作销毁自动解绑");

        /* ---- sizeHint：有文本时有效；NULL 返回 (0,0)（头文件口径） ---- */
        XToolButton_setToolButtonStyle(&tb, XToolButtonStyle_TextOnly);
        XAbstractButton_setText_2((XAbstractButton*)&tb, "工具");
        sh = XToolButton_sizeHint(&tb);
        XAPI_EXPECT(sh.width > 0 && sh.height > 0,
                    "ToolButton 文本样式下 sizeHint 有效");
        XAPI_EXPECT(XToolButton_sizeHint(NULL).width == 0 &&
                    XToolButton_sizeHint(NULL).height == 0,
                    "ToolButton sizeHint(NULL)=(0,0)");
        XToolButton_setToolButtonStyle(&tb, XToolButtonStyle_IconOnly);

        XToolButton_deinit_base(&tb);
        XMenu_deinit_base(&menu);
    }
#endif /* XTOOLBUTTON_ON */

#if XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON
    /* ================================================================
     * 6. XCommandLinkButton：description 属性 + 默认 iconSize 20x20 +
     *    XPushButton 继承宏别名。
     * ================================================================ */
    {
        XCommandLinkButton cl;
        XPoint p;
        XSize h1;
        XSize h2;
        XString* d;

        XCommandLinkButton_init(&cl, NULL, 0);
        XWidget_setGeometry((XWidget*)&cl, 0, 0, 160, 48);
        btnsig_connect((XObject*)&cl);

        /* ---- 默认值（对标 qcommandlinkbutton.cpp 构造） ---- */
        /* description 为拥有型 XString，构造即空串承载、查询不返回 NULL
         * （对标 Qt 空 QString；与本仓库 text() 空串口径一致）。 */
        XAPI_EXPECT(XCommandLinkButton_description(&cl) != NULL &&
                    xapi_u8(XCommandLinkButton_description(&cl))[0] == '\0',
                    "CommandLinkButton 默认 description=空串");
        h1 = XCommandLinkButton_iconSize(&cl);
        XAPI_EXPECT(h1.width == 20 && h1.height == 20,
                    "CommandLinkButton 默认 iconSize=20x20（大图标）");
        XAPI_EXPECT(!XCommandLinkButton_isFlat(&cl) &&
                    !XCommandLinkButton_isDefault(&cl),
                    "CommandLinkButton 继承 flat/default=false");
        XAPI_EXPECT(!XCommandLinkButton_autoDefault(&cl),
                    "CommandLinkButton 无父对话框 autoDefault=false");

        /* ---- 标题 text 往返 ---- */
        XCommandLinkButton_setText_2(&cl, "标题");
        XAPI_EXPECT(strcmp(xapi_u8(XCommandLinkButton_text(&cl)),
                           "标题") == 0,
                    "CommandLinkButton 标题 setText/text 往返");

        /* ---- description 往返 + 双行 sizeHint ---- */
        h1 = XCommandLinkButton_sizeHint(&cl);
        XCommandLinkButton_setDescription_2(&cl, "这是一行说明文本");
        XAPI_EXPECT(strcmp(xapi_u8(
                        XCommandLinkButton_description(&cl)),
                        "这是一行说明文本") == 0,
                    "CommandLinkButton setDescription_2 往返");
        h2 = XCommandLinkButton_sizeHint(&cl);
        XAPI_EXPECT(h2.width > 0 && h2.height >= h1.height,
                    "CommandLinkButton 描述参与 sizeHint（双行高度）");
        {
            XSize msh = XCommandLinkButton_minimumSizeHint(&cl);
            XAPI_EXPECT(msh.width == h2.width && msh.height == h2.height,
                        "CommandLinkButton minimumSizeHint=sizeHint");
        }
        d = XString_create_utf8("XString 描述");
        XCommandLinkButton_setDescription(&cl, d);
        XString_delete_base((XClass*)d);
        XAPI_EXPECT(strcmp(xapi_u8(
                        XCommandLinkButton_description(&cl)),
                        "XString 描述") == 0,
                    "CommandLinkButton setDescription(XString) 往返");
        XCommandLinkButton_setDescription(&cl, NULL);
        /* NULL 输入按头文件口径视为空：落空串承载，查询非 NULL。 */
        XAPI_EXPECT(XCommandLinkButton_description(&cl) != NULL &&
                    xapi_u8(XCommandLinkButton_description(&cl))[0] == '\0',
                    "CommandLinkButton setDescription(NULL)=空串承载");

        /* ---- XPushButton 特有宏别名抽查 ---- */
        XCommandLinkButton_setFlat(&cl, true);
        XAPI_EXPECT(XCommandLinkButton_isFlat(&cl),
                    "CommandLinkButton 宏别名 setFlat 往返");
        XCommandLinkButton_setFlat(&cl, false);
        XCommandLinkButton_setDefault(&cl, true);
        XAPI_EXPECT(XCommandLinkButton_isDefault(&cl),
                    "CommandLinkButton 宏别名 setDefault 往返");
        XCommandLinkButton_setDefault(&cl, false);

        /* ---- checkable/checked/toggled + click 信号 ---- */
        btnsig_reset();
        XCommandLinkButton_setCheckable(&cl, true);
        XCommandLinkButton_setChecked(&cl, true);
        XAPI_EXPECT(XCommandLinkButton_isChecked(&cl) &&
                    g_btnSig.toggled == 1,
                    "CommandLinkButton checkable/checked 往返并发 toggled");
        btnsig_reset();
        XCommandLinkButton_click(&cl);
        XAPI_EXPECT(g_btnSig.clicked == 1 &&
                    !XCommandLinkButton_isChecked(&cl),
                    "CommandLinkButton click() 翻转选中并发 clicked");

        /* ---- hitButton（继承 XPushButton 控件矩形口径） ---- */
        XPoint_init(&p, 10, 10);
        XAPI_EXPECT(XCommandLinkButton_hitButton(&cl, &p),
                    "CommandLinkButton hitButton 控件内命中");
        XPoint_init(&p, 500, 10);
        XAPI_EXPECT(!XCommandLinkButton_hitButton(&cl, &p),
                    "CommandLinkButton hitButton 控件外不命中");
        XAPI_EXPECT(XCommandLinkButton_sizeHint(NULL).width == -1,
                    "CommandLinkButton sizeHint(NULL)=无效尺寸");

        XCommandLinkButton_deinit_base(&cl);
    }
#endif /* XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON */

#if XBUTTONGROUP_ON
    /* ================================================================
     * 7. XButtonGroup：成员管理 / id 分配 / 互斥 / 八信号桥接。
     * ================================================================ */
    {
        XButtonGroup grp;
        XCheckBox* b1;
        XCheckBox* b2;
        XCheckBox outsider;
        const XVector* members;
        int64_t count;

        XCheckBox_init(&outsider, NULL, 0);
        b1 = XCheckBox_create(NULL, 0);
        b2 = XCheckBox_create(NULL, 0);
        XButtonGroup_init(&grp, NULL);
        btn_grpConnect((XObject*)&grp);

        /* ---- 默认值：exclusive=true（Qt QButtonGroup 默认互斥） ---- */
        XAPI_EXPECT(XButtonGroup_exclusive(&grp) &&
                    XButtonGroup_isExclusive(&grp),
                    "ButtonGroup 默认 exclusive=true");
        members = XButtonGroup_buttons(&grp);
        XAPI_EXPECT(members == NULL ||
                    XVector_size_base((const XContainer*)members) == 0,
                    "ButtonGroup 初始无成员");

        /* ---- addButton 指定 id + group 承载回写 ---- */
        XButtonGroup_addButton(&grp, (XAbstractButton*)b1, 10);
        members = XButtonGroup_buttons(&grp);
        count = members ? (int64_t)XVector_size_base(
                               (const XContainer*)members) : 0;
        XAPI_EXPECT(count == 1 &&
                    XButtonGroup_button(&grp, 10) == (XAbstractButton*)b1,
                    "ButtonGroup addButton 指定 id=10 往返");
        XAPI_EXPECT(XAbstractButton_group((XAbstractButton*)b1) ==
                    (void*)&grp,
                    "ButtonGroup addButton 回写按钮 group() 承载");

        /* ---- 自动 id：负序，首个 -2（Qt 文档：自动分配负 id 从 -2 起） ---- */
        XButtonGroup_addButton(&grp, (XAbstractButton*)b2, -1);
        XAPI_EXPECT(XButtonGroup_id(&grp, (XAbstractButton*)b2) == -2,
                    "ButtonGroup 自动分配 id=-2（Qt 文档约定）");

        /* ---- buttons() 按加入顺序 ---- */
        members = XButtonGroup_buttons(&grp);
        count = members ? (int64_t)XVector_size_base(
                               (const XContainer*)members) : 0;
        XAPI_EXPECT(count == 2 &&
                    *(XAbstractButton**)XVector_at_base(members, 0) ==
                        (XAbstractButton*)b1 &&
                    *(XAbstractButton**)XVector_at_base(members, 1) ==
                        (XAbstractButton*)b2,
                    "ButtonGroup buttons() 按加入顺序 [b1,b2]");

        /* ---- button/id 查询与未命中 ---- */
        XAPI_EXPECT(XButtonGroup_button(&grp, -2) == (XAbstractButton*)b2 &&
                    XButtonGroup_button(&grp, 999) == NULL,
                    "ButtonGroup button(id) 命中/未命中");
        XAPI_EXPECT(XButtonGroup_id(&grp, (XAbstractButton*)b1) == 10 &&
                    XButtonGroup_id(&grp, (XAbstractButton*)&outsider) == -1,
                    "ButtonGroup id(成员/非成员) 查询");

        /* ---- 桥接信号：click(b1) → 四类信号 + idClicked(10) ---- */
        btn_grpReset();
        XAbstractButton_click((XAbstractButton*)b1);
        XAPI_EXPECT(g_grp.btnClicked == 1 &&
                    g_grp.lastBtnClicked == (XAbstractButton*)b1,
                    "ButtonGroup 成员点击转发 buttonClicked(b1)");
        XAPI_EXPECT(g_grp.idClicked == 1 && g_grp.lastIdClicked == 10,
                    "ButtonGroup idClicked 携带成员 id=10");
        XAPI_EXPECT(g_grp.btnPressed == 1 && g_grp.btnReleased == 1 &&
                    g_grp.idPressed == 1 && g_grp.idReleased == 1,
                    "ButtonGroup pressed/released 同步转发（按钮+id 维度）");
        XAPI_EXPECT(g_grp.btnToggled == 1 && g_grp.lastBtnToggledChecked,
                    "ButtonGroup toggled(true) 转发");
        XAPI_EXPECT(XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b1 &&
                    XButtonGroup_checkedId(&grp) == 10,
                    "ButtonGroup checkedButton/checkedId 跟踪选中");

        /* ---- exclusive：click(b2) 自动取消 b1 ---- */
        btn_grpReset();
        XAbstractButton_click((XAbstractButton*)b2);
        XAPI_EXPECT(!XAbstractButton_isChecked((XAbstractButton*)b1) &&
                    XAbstractButton_isChecked((XAbstractButton*)b2),
                    "ButtonGroup 互斥：新选中自动取消旧选中");
        XAPI_EXPECT(XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b2 &&
                    XButtonGroup_checkedId(&grp) == -2,
                    "ButtonGroup checked 跟踪转移至 b2(id=-2)");
        XAPI_EXPECT(g_grp.btnToggled == 2 && g_grp.idToggled == 2 &&
                    g_grp.lastIdToggled == -2 && g_grp.lastIdToggledChecked,
                    "ButtonGroup 互斥取消+新选中各发一对 toggled(idToggled)");
        /* exclusive 组 tracked 选中按钮不可 setChecked(false)
         * （对标 Qt QButtonGroup exclusive 守卫）。 */
        XAbstractButton_setChecked((XAbstractButton*)b2, false);
        XAPI_EXPECT(XAbstractButton_isChecked((XAbstractButton*)b2),
                    "ButtonGroup tracked 选中按钮不可直接反选");

        /* ---- 关互斥：可双选中，checkedButton=最近选中 ---- */
        XButtonGroup_setExclusive(&grp, false);
        XAPI_EXPECT(!XButtonGroup_isExclusive(&grp),
                    "ButtonGroup setExclusive(false) 往返");
        XAbstractButton_setChecked((XAbstractButton*)b1, true);
        XAPI_EXPECT(XAbstractButton_isChecked((XAbstractButton*)b1) &&
                    XAbstractButton_isChecked((XAbstractButton*)b2),
                    "ButtonGroup 非互斥模式允许双选中");
        XAPI_EXPECT(XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b1,
                    "ButtonGroup checkedButton=最近选中的成员");

        /* ---- 恢复互斥：Qt 守卫只保护组 tracked 的 checkedButton
         *      （qabstractbutton.cpp：!checked && queryCheckedButton()==this
         *       && exclusive 才拦截）。此刻 tracked=最近选中的 b1，故 b1
         *      不可反选；非 tracked 的 b2 可反选且 tracked 保持 b1。 ---- */
        XButtonGroup_setExclusive(&grp, true);
        XAbstractButton_setChecked((XAbstractButton*)b1, false);
        XAPI_EXPECT(XAbstractButton_isChecked((XAbstractButton*)b1) &&
                    XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b1,
                    "ButtonGroup 恢复互斥后 tracked(b1) 仍不可直接反选");
        XAbstractButton_setChecked((XAbstractButton*)b2, false);
        XAPI_EXPECT(!XAbstractButton_isChecked((XAbstractButton*)b2) &&
                    XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b1,
                    "ButtonGroup 恢复互斥后非 tracked 成员可反选");

        /* ---- setId 覆盖旧 id ---- */
        XButtonGroup_setId(&grp, (XAbstractButton*)b1, 42);
        XAPI_EXPECT(XButtonGroup_id(&grp, (XAbstractButton*)b1) == 42 &&
                    XButtonGroup_button(&grp, 42) == (XAbstractButton*)b1 &&
                    XButtonGroup_button(&grp, 10) == NULL,
                    "ButtonGroup setId 覆盖旧 id 映射");

        /* ---- 重复 addButton 为无操作（Qt: 已在组内不再加入） ---- */
        XButtonGroup_addButton(&grp, (XAbstractButton*)b1, 5);
        members = XButtonGroup_buttons(&grp);
        count = members ? (int64_t)XVector_size_base(
                               (const XContainer*)members) : 0;
        XAPI_EXPECT(count == 2 &&
                    XButtonGroup_id(&grp, (XAbstractButton*)b1) == 42,
                    "ButtonGroup 重复 addButton 无操作（id 不变）");

        /* ---- removeButton：承载清空 / id 失效 / tracked 复位 ---- */
        /* 先让 b2 重新成为 tracked 选中项（Qt notifyChecked：最近选中者
         * 成为组 tracked），再移除以验证复位：Qt removeButton→
         * detectCheckedButton 在 exclusive 组不复扫，checkedButton 落
         * NULL/checkedId=-1。 */
        XAbstractButton_setChecked((XAbstractButton*)b2, true);
        XAPI_EXPECT(XButtonGroup_checkedButton(&grp) ==
                        (XAbstractButton*)b2 &&
                    XButtonGroup_checkedId(&grp) == -2,
                    "ButtonGroup 重新选中 b2 成为 tracked(id=-2)");
        XButtonGroup_removeButton(&grp, (XAbstractButton*)b2);
        XAPI_EXPECT(XAbstractButton_group((XAbstractButton*)b2) == NULL,
                    "ButtonGroup removeButton 清空按钮 group() 承载");
        XAPI_EXPECT(XButtonGroup_id(&grp, (XAbstractButton*)b2) == -1,
                    "ButtonGroup 移除后按钮 id 查询=-1");
        XAPI_EXPECT(XButtonGroup_checkedButton(&grp) == NULL &&
                    XButtonGroup_checkedId(&grp) == -1,
                    "ButtonGroup 移除 tracked 按钮复位 checkedButton/Id");
        members = XButtonGroup_buttons(&grp);
        count = members ? (int64_t)XVector_size_base(
                               (const XContainer*)members) : 0;
        XAPI_EXPECT(count == 1,
                    "ButtonGroup removeButton 后成员数=1");

        /* 析构次序：先组（桥断连需按钮存活），后按钮。 */
        XButtonGroup_deinit_base(&grp);
        XCheckBox_delete_base(b1);
        XCheckBox_delete_base(b2);
        XCheckBox_deinit_base(&outsider);
    }
#endif /* XBUTTONGROUP_ON */

#else /* XWIDGET_ON && XABSTRACTBUTTON_ON */

/* 按钮族整体裁剪时的非空翻译单元哨兵。 */
typedef int xgui_demo_apitest_buttons_disabled_sentinel;

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */

    return failures;
}
