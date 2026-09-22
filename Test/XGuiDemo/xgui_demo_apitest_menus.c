/* xgui_demo_apitest_menus.c —— 控件 API 测试族：menus（框架杂项族）。
 *
 * 覆盖控件（对标 Qt 6.8.3）：XMenu / XMenuBar / XToolBar / XStatusBar /
 * XAction / XActionGroup / XShortcut / XToolTip / XErrorMessage /
 * XSplashScreen / XFocusFrame / XSizeGrip / XRubberBand。
 *
 * 测试口径（见 xgui_demo_apitest.h 契约）：
 *  - 属性 setter/getter 往返一致；Qt 文档默认值确定的直接断言，实现与
 *    Qt 语义存在偏差或头文件未标明的一律写注释不按 Qt 硬断言（防误报）；
 *  - 信号断言经 XObject_event_base 直发合成事件（与真实输入同路径），
 *    或连接槽后驱动公开 API 的真实发射点（如 XAction_trigger → 菜单/
 *    工具栏转发），句柄型信号函数按头文件"发射"语义直发验证连接通路；
 *  - 无头语义：控件不 show 直接调 API（全程不进入窗口流程；XMenu_
 *    popup/exec 因弹顶层窗口/exec 阻塞不在本套件调用，见函数内注释）；
 *  - 每条断言中文注释标对标 Qt 的哪个行为；渲染/视觉效果不在断言职责。
 */
#include "xgui_demo_apitest.h"

#include <stdio.h>
#include <string.h>

#include "XObject.h"
#include "XEvent.h"   /* XKey 枚举（快捷键键码，对标 QKeySequence 单键）。 */
#include "XVarList.h"
#include "XVector.h"
#include "XAlignment.h"

#if XACTION_ON
#include "XAction.h"
#endif
#if XWIDGET_ON && XMENU_ON
#include "XMenu.h"
#endif
#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON
#include "XMenuBar.h"
#endif
#if XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON
#include "XToolBar.h"
#endif
#if XWIDGET_ON && XSTATUSBAR_ON
#include "XStatusBar.h"
#endif
#if XWIDGET_ON && XACTION_ON
#include "XActionGroup.h"
#endif
#if XWIDGET_ON
#include "XShortcut.h"
#include "XToolTip.h"
#endif
#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON
#include "XErrorMessage.h"
#endif
#if XWIDGET_ON && XSPLASHSCREEN_ON
#include "XSplashScreen.h"
#endif
#if XWIDGET_ON && XFOCUSFRAME_ON
#include "XFocusFrame.h"
#endif
#if XWIDGET_ON && XSIZEGRIP_ON
#include "XSizeGrip.h"
#endif
#if XWIDGET_ON && XRUBBERBAND_ON
#include "XRubberBand.h"
#endif
#if XPALETTE_ON
#include "XPalette.h"
#endif
#if XACTION_ON
#include "XVariant.h"
#endif

/* ==================== 公共小工具 ==================== */

#if XWIDGET_ON && XMENU_ON
/** @brief 在菜单动作列表中反查动作下标（未命中返回 -1）。 */
static int menus_actionIndex(const XVector* actions, const XAction* target)
{
    int64_t i;
    int64_t n;
    if (!actions)
        return -1;
    n = XVector_size_base((const XContainer*)actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(actions, i);
        if (item && (const XAction*)*item == target)
            return (int)i;
    }
    return -1;
}
#endif /* XWIDGET_ON && XMENU_ON */

/* ==================== XAction 信号记录器 ==================== */

#if XACTION_ON
static int  g_actChanged;          /**< changed 计数。 */
static int  g_actEnabledChanged;   /**< enabledChanged 计数。 */
static bool g_actLastEnabled;      /**< 最近 enabledChanged 参数。 */
static int  g_actCheckableChanged; /**< checkableChanged 计数。 */
static bool g_actLastCheckable;    /**< 最近 checkableChanged 参数。 */
static int  g_actToggled;          /**< toggled 计数。 */
static bool g_actLastToggled;      /**< 最近 toggled 参数。 */
static int  g_actTriggered;        /**< triggered 计数。 */
static bool g_actLastTriggered;    /**< 最近 triggered 参数。 */
static int  g_actHovered;          /**< hovered 计数。 */
static int  g_actVisibleChanged;   /**< visibleChanged 计数。 */

static void menus_actReset(void)
{
    g_actChanged = 0;
    g_actEnabledChanged = 0;
    g_actLastEnabled = false;
    g_actCheckableChanged = 0;
    g_actLastCheckable = false;
    g_actToggled = 0;
    g_actLastToggled = false;
    g_actTriggered = 0;
    g_actLastTriggered = false;
    g_actHovered = 0;
    g_actVisibleChanged = 0;
}

static void menus_actChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_actChanged;
}

static void menus_actEnabledChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, enabled);
    ++g_actEnabledChanged;
    g_actLastEnabled = enabled;
}

static void menus_actCheckableChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checkable);
    ++g_actCheckableChanged;
    g_actLastCheckable = checkable;
}

static void menus_actToggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_actToggled;
    g_actLastToggled = checked;
}

static void menus_actTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_actTriggered;
    g_actLastTriggered = checked;
}

static void menus_actHoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_actHovered;
}

static void menus_actVisibleChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_actVisibleChanged;
}

/** @brief QAction 七信号统一装配（sender==receiver 自连定式）。 */
static void menus_actConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XAction_changed_signal),
                      sender, menus_actChangedSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_enabledChanged_signal),
                      sender, menus_actEnabledChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_checkableChanged_signal),
                      sender, menus_actCheckableChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_toggled_signal),
                      sender, menus_actToggledSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_triggered_signal),
                      sender, menus_actTriggeredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_hovered_signal),
                      sender, menus_actHoveredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XAction_visibleChanged_signal),
                      sender, menus_actVisibleChangedSlot,
                      XConnectionType_Direct);
}
#endif /* XACTION_ON */

/* ==================== XMenu 信号记录器 ==================== */

#if XWIDGET_ON && XMENU_ON
static int      g_menuTriggered;   /**< triggered(action) 计数。 */
static XAction* g_menuLastAction;  /**< 最近 triggered/hovered 参数。 */
static int      g_menuHovered;     /**< hovered(action) 计数。 */
static int      g_menuAboutToShow; /**< aboutToShow 计数。 */
static int      g_menuAboutToHide; /**< aboutToHide 计数。 */

static void menus_menuReset(void)
{
    g_menuTriggered = 0;
    g_menuLastAction = NULL;
    g_menuHovered = 0;
    g_menuAboutToShow = 0;
    g_menuAboutToHide = 0;
}

static void menus_menuTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_menuTriggered;
    g_menuLastAction = (XAction*)act;
}

static void menus_menuHoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_menuHovered;
    g_menuLastAction = (XAction*)act;
}

static void menus_menuAboutToShowSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_menuAboutToShow;
}

static void menus_menuAboutToHideSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_menuAboutToHide;
}

static void menus_menuConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XMenu_triggered_signal),
                      sender, menus_menuTriggeredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XMenu_hovered_signal),
                      sender, menus_menuHoveredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XMenu_aboutToShow_signal),
                      sender, menus_menuAboutToShowSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XMenu_aboutToHide_signal),
                      sender, menus_menuAboutToHideSlot,
                      XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XMENU_ON */

/* ==================== XActionGroup 信号记录器 ==================== */

#if XWIDGET_ON && XACTION_ON
static int      g_grpTriggered;    /**< triggered(action) 计数。 */
static XAction* g_grpLastAction;   /**< 最近 triggered/hovered 参数。 */
static int      g_grpHovered;      /**< hovered(action) 计数。 */

static void menus_grpReset(void)
{
    g_grpTriggered = 0;
    g_grpLastAction = NULL;
    g_grpHovered = 0;
}

static void menus_grpTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_grpTriggered;
    g_grpLastAction = (XAction*)act;
}

static void menus_grpHoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_grpHovered;
    g_grpLastAction = (XAction*)act;
}

static void menus_grpConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XActionGroup_triggered_signal),
                      sender, menus_grpTriggeredSlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XActionGroup_hovered_signal),
                      sender, menus_grpHoveredSlot, XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XACTION_ON */

/* ==================== XToolBar 信号记录器 ==================== */

#if XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON
static int      g_tbActionTriggered; /**< actionTriggered(action) 计数。 */
static XAction* g_tbLastAction;      /**< 最近 triggered/hovered 参数。 */
static int      g_tbActionHovered;   /**< actionHovered(action) 计数。 */
static int      g_tbMovableChanged;  /**< movableChanged(bool) 计数。 */
static bool     g_tbLastMovable;     /**< 最近 movableChanged 参数。 */
static int      g_tbOrientChanged;   /**< orientationChanged(int) 计数。 */
static int      g_tbLastOrient;      /**< 最近 orientationChanged 参数。 */
static int      g_tbAreasChanged;    /**< allowedAreasChanged(int) 计数。 */
static int      g_tbLastAreas;       /**< 最近 allowedAreasChanged 参数。 */
static int      g_tbStyleChanged;    /**< toolButtonStyleChanged(int) 计数。 */
static int      g_tbLastStyle;       /**< 最近 toolButtonStyleChanged 参数。 */
static int      g_tbIconSizeChanged; /**< iconSizeChanged(int,int) 计数。 */
static int      g_tbVisibility;      /**< visibilityChanged(bool) 计数。 */
static bool     g_tbLastVisibility;  /**< 最近 visibilityChanged 参数。 */
static int      g_tbTopLevel;        /**< topLevelChanged(bool) 计数。 */

static void menus_tbReset(void)
{
    memset(&g_tbLastAction, 0, sizeof(g_tbLastAction));
    g_tbActionTriggered = 0;
    g_tbActionHovered = 0;
    g_tbMovableChanged = 0;
    g_tbLastMovable = false;
    g_tbOrientChanged = 0;
    g_tbLastOrient = 0;
    g_tbAreasChanged = 0;
    g_tbLastAreas = 0;
    g_tbStyleChanged = 0;
    g_tbLastStyle = 0;
    g_tbIconSizeChanged = 0;
    g_tbVisibility = 0;
    g_tbLastVisibility = false;
    g_tbTopLevel = 0;
}

static void menus_tbActionTriggeredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_tbActionTriggered;
    g_tbLastAction = (XAction*)act;
}

static void menus_tbActionHoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAction*, act);
    ++g_tbActionHovered;
    g_tbLastAction = (XAction*)act;
}

static void menus_tbMovableChangedSlot(XObject* receiver, XVarList* args)
{
    int movable;
    (void)receiver;
    /* 发射侧经 xtb_emitVoid 以 int 承载布尔载荷（0/1），按 int 解包。 */
    XVarList_args_1(args, int, movableRaw);
    movable = movableRaw;
    ++g_tbMovableChanged;
    g_tbLastMovable = movable != 0;
}

static void menus_tbOrientChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, orientation);
    ++g_tbOrientChanged;
    g_tbLastOrient = orientation;
}

static void menus_tbAreasChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, areas);
    ++g_tbAreasChanged;
    g_tbLastAreas = areas;
}

static void menus_tbStyleChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, style);
    ++g_tbStyleChanged;
    g_tbLastStyle = style;
}

static void menus_tbIconSizeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_tbIconSizeChanged;
}

static void menus_tbVisibilitySlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, visible);
    ++g_tbVisibility;
    g_tbLastVisibility = visible;
}

static void menus_tbTopLevelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_tbTopLevel;
}

static void menus_tbConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XToolBar_actionTriggered_signal),
                      sender, menus_tbActionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_actionHovered_signal),
                      sender, menus_tbActionHoveredSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_movableChanged_signal),
                      sender, menus_tbMovableChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_orientationChanged_signal),
                      sender, menus_tbOrientChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_allowedAreasChanged_signal),
                      sender, menus_tbAreasChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_toolButtonStyleChanged_signal),
                      sender, menus_tbStyleChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_iconSizeChanged_signal),
                      sender, menus_tbIconSizeChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_visibilityChanged_signal),
                      sender, menus_tbVisibilitySlot, XConnectionType_Direct);
    XObject_connect_1(sender, XSignal(XToolBar_topLevelChanged_signal),
                      sender, menus_tbTopLevelSlot, XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON */

/* ==================== XStatusBar 信号记录器 ==================== */

#if XWIDGET_ON && XSTATUSBAR_ON
static int  g_sbMessageChanged;  /**< messageChanged(text) 计数。 */
static char g_sbLastMessage[96]; /**< 最近消息文本拷贝（参数 XString 归属
                                      发射侧，槽内立即拷贝为纯文本）。 */

static void menus_sbReset(void)
{
    g_sbMessageChanged = 0;
    g_sbLastMessage[0] = '\0';
}

static void menus_sbMessageChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, str);
    ++g_sbMessageChanged;
    snprintf(g_sbLastMessage, sizeof(g_sbLastMessage), "%s",
             str ? xapi_u8(str) : "");
}

static void menus_sbConnect(XObject* sender)
{
    XObject_connect_1(sender, XSignal(XStatusBar_messageChanged_signal),
                      sender, menus_sbMessageChangedSlot,
                      XConnectionType_Direct);
}
#endif /* XWIDGET_ON && XSTATUSBAR_ON */

/* ==================== XShortcut / XSplashScreen 信号记录器 ==================== */

#if XWIDGET_ON
static int g_scActivated; /**< activated() 计数。 */

static void menus_scActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_scActivated;
}
#endif /* XWIDGET_ON */

#if XWIDGET_ON && XSPLASHSCREEN_ON
static int  g_spMessageChanged;  /**< messageChanged(text) 计数。 */
static char g_spLastMessage[96]; /**< 最近消息文本拷贝。 */

static void menus_spMessageChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, str);
    ++g_spMessageChanged;
    snprintf(g_spLastMessage, sizeof(g_spLastMessage), "%s",
             str ? xapi_u8(str) : "");
}
#endif /* XWIDGET_ON && XSPLASHSCREEN_ON */

/* ==================== 入口 ==================== */

int xapi_menus_run(void)
{
    int failures = 0;

#if XWIDGET_ON
    /* 套件根控件：堆创建，全部测试控件挂其下，结束时级联析构
     * （对标 Qt 父子所有权；栈对象在各自小节显式 deinit 解挂）。 */
    XWidget* root = XWidget_create(NULL, 0);
    XAPI_EXPECT(root != NULL, "套件根控件创建成功");
    if (root) {
        XString* strTmp;

        /* ============================================================
         * 1. XMenu：弹出菜单（对标 Qt 6.8 QMenu）。
         *    无头口径：不调 popup/exec（popup 弹顶层窗口、exec 阻塞于
         *    事件循环）；aboutToShow/aboutToHide 经头文件声明的发射函
         *    数直发验证连接通路，triggered 经动作触发真实转发路径。
         * ============================================================ */
#if XMENU_ON
        {
            XMenu* m = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root,
                                       NULL);
            XMenu m2;
            XMenu m3;
            XAction* a1;
            XAction* a2;
            XAction* sep;
            XAction* sec;
            XMenu* sub1;
            XAction* insSep;
            XAction* insSec;
            XAction* insMenuAct;
            XMenu* sub2;
            XAction* aA;
            XAction* aB;
            XAction* outsider;
            XAction* mA;
            const XVector* acts;
            XSize hint;
            XPoint pos;
            XRect geo;

            XAPI_EXPECT(m != NULL, "Menu create_ex 创建成功");

            /* ---- 默认值（QMenu 构造默认：无动作、标题空、不撕离） ---- */
            XAPI_EXPECT(XMenu_isEmpty(m), "Menu 默认 isEmpty=true");
            XAPI_EXPECT(XMenu_title_const(m) == NULL,
                        "Menu 默认标题=NULL（QMenu 默认空标题）");
            XAPI_EXPECT(XMenu_separatorsCollapsible(m),
                        "Menu 默认 separatorsCollapsible=true（QMenu 文档默认）");
            XAPI_EXPECT(!XMenu_toolTipsVisible(m),
                        "Menu 默认 toolTipsVisible=false（QMenu 文档默认）");
            XAPI_EXPECT(!XMenu_tearOffEnabled(m),
                        "Menu 默认 tearOffEnabled=false（QMenu 文档默认）");
            XAPI_EXPECT(XMenu_activeAction(m) == NULL &&
                        XMenu_defaultAction(m) == NULL,
                        "Menu 默认 active/defaultAction=NULL");
            XAPI_EXPECT(XMenu_platformMenu(m) == NULL,
                        "Menu 默认 platformMenu=NULL");
            acts = XMenu_actions(m);
            XAPI_EXPECT(acts != NULL &&
                        XVector_size_base((const XContainer*)acts) == 0,
                        "Menu 默认 actions 空表（非 NULL 空向量）");

            /* ---- 带标题栈对象初始化（对标 QMenu(title,parent) 构造） ---- */
            XMenu_init_2(&m2, root, "标题菜单");
            XAPI_EXPECT(XMenu_title_const(&m2) != NULL &&
                        strcmp(xapi_u8(XMenu_title_const(&m2)),
                               "标题菜单") == 0,
                        "Menu init_2 标题=标题菜单");
            XMenu_deinit_base(&m2);

            /* ---- title/setTitle 往返（QMenu::setTitle/title） ---- */
            XMenu_setTitle_2(m, "菜单标题");
            strTmp = XMenu_title(m);
            XAPI_EXPECT(strTmp && strcmp(xapi_u8(strTmp),
                                         "菜单标题") == 0,
                        "Menu setTitle_2 后 title 拷贝往返=菜单标题");
            if (strTmp) XString_delete_base((XClass*)strTmp);

            /* ---- addAction 族（QMenu::addAction(text)） ---- */
            a1 = XMenu_addAction_2(m, "打开");
            XAPI_EXPECT(a1 != NULL, "Menu addAction_2 返回非 NULL 动作");
            XAPI_EXPECT(menus_actionIndex(XMenu_actions(m), a1) == 0 &&
                        !XMenu_isEmpty(m),
                        "Menu addAction 后动作数=1 且 isEmpty=false");
            XAPI_EXPECT(XAction_text_const(a1) &&
                        strcmp(xapi_u8(XAction_text_const(a1)),
                               "打开") == 0,
                        "Menu addAction_2 动作文本=打开");
            strTmp = XString_create_utf8("另存");
            a2 = XMenu_addAction(m, strTmp);
            XString_delete_base((XClass*)strTmp);
            XAPI_EXPECT(a2 != NULL &&
                        menus_actionIndex(XMenu_actions(m), a2) == 1,
                        "Menu addAction(XString) 追加到下标 1");

            /* ---- 分隔条（QMenu::addSeparator → isSeparator 动作） ---- */
            sep = XMenu_addSeparator(m);
            XAPI_EXPECT(sep != NULL && XAction_isSeparator(sep),
                        "Menu addSeparator 动作 isSeparator=true");

            /* ---- 节标题（QMenu::addSection：禁用不可选中） ---- */
            sec = XMenu_addSection_2(m, "分组");
            XAPI_EXPECT(sec != NULL &&
                        menus_actionIndex(XMenu_actions(m), sec) == 3,
                        "Menu addSection_2 追加节标题到末尾");
            XAPI_EXPECT(!XAction_isEnabled(sec),
                        "Menu 节标题动作禁用（QMenu section 不可选中）");
            XAPI_EXPECT(XAction_text_const(sec) &&
                        strcmp(xapi_u8(XAction_text_const(sec)),
                               "分组") == 0,
                        "Menu 节标题文本=分组");

            /* ---- 子菜单（QMenu::addMenu(QMenu*)：menuInAction 关联） ---- */
            sub1 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root,
                                   "子菜单");
            XAPI_EXPECT(XMenu_addMenu(m, sub1),
                        "Menu addMenu(子菜单) 返回 true");
            acts = XMenu_actions(m);
            {
                XAction* subAct = acts
                    ? *(XAction**)XVector_at_base(acts, 4)
                    : NULL;
                XAPI_EXPECT(subAct != NULL && XAction_menu(subAct) == sub1,
                            "Menu 第 5 项动作关联 sub1（QAction::menu）");
                XAPI_EXPECT(XMenu_menuInAction(subAct) == sub1,
                            "Menu menuInAction 反查=子菜单（QMenu::menuInAction）");
            }

            /* ---- addMenu(NULL)：头文件口径（XMenu.h @return）——NULL 时
             *      仅创建分隔动作占位并计入动作表，成功加入返回 true ---- */
            XAPI_EXPECT(XMenu_addMenu(m, NULL),
                        "Menu addMenu(NULL) 创建分隔占位返回 true（头文件口径）");
            XAPI_EXPECT(XVector_size_base(
                            (const XContainer*)XMenu_actions(m)) == 6,
                        "Menu addMenu(NULL) 追加分隔占位（头文件口径）");

            /* ---- 插入族（QMenu::insertSeparator/insertSection/insertMenu） ---- */
            insSep = XMenu_insertSeparator(m, a1);
            XAPI_EXPECT(insSep != NULL &&
                        XAction_isSeparator(insSep) &&
                        menus_actionIndex(XMenu_actions(m), insSep) ==
                            menus_actionIndex(XMenu_actions(m), a1) - 1,
                        "Menu insertSeparator(before=打开) 插到其前");
            insSec = XMenu_insertSection_2(m, a1, "插入节");
            XAPI_EXPECT(insSec != NULL && !XAction_isEnabled(insSec) &&
                        menus_actionIndex(XMenu_actions(m), insSec) ==
                            menus_actionIndex(XMenu_actions(m), a1) - 1,
                        "Menu insertSection_2 插入禁用节标题");
            sub2 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root,
                                   "二级");
            insMenuAct = XMenu_insertMenu(m, a1, sub2);
            XAPI_EXPECT(insMenuAct != NULL && XMenu_menuInAction(insMenuAct) == sub2 &&
                        menus_actionIndex(XMenu_actions(m), insMenuAct) ==
                            menus_actionIndex(XMenu_actions(m), a1) - 1,
                        "Menu insertMenu(before=打开) 子菜单插到其前");
            XAPI_EXPECT(XVector_size_base(
                            (const XContainer*)XMenu_actions(m)) == 9,
                        "Menu 插入族后动作总数=9");

            /* ---- clear（QMenu::clear：全部动作清空、isEmpty 复位） ---- */
            XMenu_clear(m);
            XAPI_EXPECT(XMenu_isEmpty(m) &&
                        XVector_size_base(
                            (const XContainer*)XMenu_actions(m)) == 0,
                        "Menu clear 后动作清空 isEmpty=true");
            XAPI_EXPECT(XMenu_defaultAction(m) == NULL &&
                        XMenu_activeAction(m) == NULL,
                        "Menu clear 清空 default/activeAction 引用");

            /* ---- 几何：actionGeometry/actionAt/sizeHint
             *      （QMenu::actionGeometry/actionAt/sizeHint） ---- */
            aA = XMenu_addAction_2(m, "菜单项A");
            aB = XMenu_addAction_2(m, "菜单项B");
            XWidget_setGeometry((XWidget*)m, 0, 0, 200, 100);
            hint = XMenu_sizeHint(m);
            XAPI_EXPECT(hint.width > 0 && hint.height > 0,
                        "Menu sizeHint 两动作后宽高均为正");
            geo = XMenu_actionGeometry(m, aA);
            XAPI_EXPECT(geo.width == XWidget_width((XWidget*)m) &&
                        geo.height > 0,
                        "Menu actionGeometry 宽=菜单宽、条目高为正");
            XPoint_init(&pos, geo.x + geo.width / 2,
                        geo.y + geo.height / 2);
            XAPI_EXPECT(XMenu_actionAt(m, &pos) == aA,
                        "Menu actionAt(条目A中心)=菜单项A");
            geo = XMenu_actionGeometry(m, aB);
            XPoint_init(&pos, geo.x + geo.width / 2,
                        geo.y + geo.height / 2);
            XAPI_EXPECT(XMenu_actionAt(m, &pos) == aB,
                        "Menu actionAt(条目B中心)=菜单项B");
            XPoint_init(&pos, 500, 10);
            XAPI_EXPECT(XMenu_actionAt(m, &pos) == NULL,
                        "Menu actionAt(X 越界)=NULL（宽度护栏）");
            XAPI_EXPECT(XMenu_actionAt(m, NULL) == NULL,
                        "Menu actionAt(NULL 坐标)=NULL");
            outsider = XAction_create();
            geo = XMenu_actionGeometry(m, outsider);
            XAPI_EXPECT(geo.x == 0 && geo.y == 0 && geo.width == 0 &&
                        geo.height == 0,
                        "Menu actionGeometry(非成员动作)=空矩形");

            /* ---- menuAction 懒创建缓存 + setTitle 同步
             *      （QMenu::menuAction；setTitle 更新动作文本） ---- */
            mA = XMenu_menuAction(m);
            XAPI_EXPECT(mA != NULL, "Menu menuAction 首次懒创建非 NULL");
            XAPI_EXPECT(XMenu_menuAction(m) == mA,
                        "Menu menuAction 二次调用同一指针（缓存）");
            XMenu_setTitle_2(m, "新标题");
            XAPI_EXPECT(XAction_text_const(mA) &&
                        strcmp(xapi_u8(XAction_text_const(mA)),
                               "新标题") == 0,
                        "Menu setTitle 后 menuAction 文本同步=新标题");

            /* ---- setTitle XString 主版本（QMenu::setTitle(Qstring)） ---- */
            strTmp = XString_create_utf8("串标题");
            XMenu_setTitle(m, strTmp);
            XString_delete_base((XClass*)strTmp);
            XAPI_EXPECT(XMenu_title_const(m) &&
                        strcmp(xapi_u8(XMenu_title_const(m)),
                               "串标题") == 0,
                        "Menu setTitle(XString) 往返=串标题");

            /* ---- default/activeAction（QMenu::setDefaultAction 等） ---- */
            XMenu_setDefaultAction(m, aA);
            XAPI_EXPECT(XMenu_defaultAction(m) == aA,
                        "Menu setDefaultAction 往返");
            XMenu_setDefaultAction(m, NULL);
            XAPI_EXPECT(XMenu_defaultAction(m) == NULL,
                        "Menu setDefaultAction(NULL) 清除");
            XMenu_setActiveAction(m, aA);
            XAPI_EXPECT(XMenu_activeAction(m) == aA,
                        "Menu setActiveAction 高亮往返");
            XMenu_setActiveAction(m, aA);
            XAPI_EXPECT(XMenu_activeAction(m) == aA,
                        "Menu setActiveAction 重复设置为无操作");
            XMenu_setActiveAction(m, NULL);
            XAPI_EXPECT(XMenu_activeAction(m) == NULL,
                        "Menu setActiveAction(NULL) 清除高亮");

            /* ---- icon（QMenu::setIcon/icon；路径字符串承载） ---- */
            XMenu_setIcon_2(m, "menu.png");
            XAPI_EXPECT(XMenu_icon(m) &&
                        strcmp(xapi_u8(XMenu_icon(m)),
                               "menu.png") == 0,
                        "Menu setIcon_2 往返=menu.png");
            strTmp = XString_create_utf8("alt.png");
            XMenu_setIcon(m, strTmp);
            XString_delete_base((XClass*)strTmp);
            XAPI_EXPECT(XMenu_icon(m) &&
                        strcmp(xapi_u8(XMenu_icon(m)),
                               "alt.png") == 0,
                        "Menu setIcon(XString) 覆盖=alt.png");
            XMenu_setIcon_2(m, NULL);
            XAPI_EXPECT(XMenu_icon(m) == NULL,
                        "Menu setIcon(NULL) 清除图标");

            /* ---- 平台菜单句柄（QMenu::setPlatformMenu；不透明存储） ---- */
            XMenu_setPlatformMenu(m, (void*)(size_t)0x1234);
            XAPI_EXPECT(XMenu_platformMenu(m) == (void*)(size_t)0x1234,
                        "Menu platformMenu 不透明句柄往返");
            XMenu_setPlatformMenu(m, NULL);
            XAPI_EXPECT(XMenu_platformMenu(m) == NULL,
                        "Menu setPlatformMenu(NULL) 清除");
            /* setNoReplayFor/setAsDockMenu：仅存储/记录，无公开 getter
             * （对标 QMenu::setNoReplayFor/setAsDockMenu 调用面消化）。 */
            XMenu_setNoReplayFor(m, root);
            XMenu_setAsDockMenu(m);

            /* ---- 撕离（QMenu::setTearOffEnabled/showTearOffMenu） ---- */
            XMenu_setTearOffEnabled(m, true);
            XAPI_EXPECT(XMenu_tearOffEnabled(m) &&
                        XMenu_isTearOffEnabled(m),
                        "Menu tearOffEnabled 往返（含 isTearOffEnabled 宏）");
            XMenu_showTearOffMenu(m);
            XAPI_EXPECT(XMenu_isTearOffMenuVisible(m),
                        "Menu showTearOffMenu 后撕离可见位=true");
            XMenu_hideTearOffMenu(m);
            XAPI_EXPECT(!XMenu_isTearOffMenuVisible(m),
                        "Menu hideTearOffMenu 后撕离可见位=false");
            XMenu_setTearOffEnabled(m, false);
            XAPI_EXPECT(!XMenu_tearOffEnabled(m),
                        "Menu setTearOffEnabled(false) 复位");

            /* ---- 分隔合并/工具提示开关（QMenu 同名属性） ---- */
            XMenu_setSeparatorsCollapsible(m, false);
            XAPI_EXPECT(!XMenu_separatorsCollapsible(m),
                        "Menu setSeparatorsCollapsible(false) 往返");
            XMenu_setSeparatorsCollapsible(m, false);
            XMenu_setSeparatorsCollapsible(m, true);
            XAPI_EXPECT(XMenu_separatorsCollapsible(m),
                        "Menu separatorsCollapsible 复位=true（QMenu 默认）");
            XMenu_setToolTipsVisible(m, true);
            XAPI_EXPECT(XMenu_toolTipsVisible(m),
                        "Menu setToolTipsVisible(true) 往返");
            XMenu_setToolTipsVisible(m, false);
            XAPI_EXPECT(!XMenu_toolTipsVisible(m),
                        "Menu setToolTipsVisible(false) 复位（QMenu 默认）");

            /* ---- 空菜单 sizeHint：条目数 0 → 高度 0（布局事实） ---- */
            XMenu_init(&m3, NULL);
            hint = XMenu_sizeHint(&m3);
            XAPI_EXPECT(hint.height == 0,
                        "Menu 空菜单 sizeHint 高度=0");
            XMenu_deinit_base(&m3);

            /* ---- 信号：triggered 经动作触发真实转发
             *      （QMenu::triggered(action)） ---- */
            menus_menuReset();
            menus_menuConnect((XObject*)m);
            XAction_trigger(aA);
            XAPI_EXPECT(g_menuTriggered == 1 && g_menuLastAction == aA,
                        "Menu 触发条目A 转发 triggered(A)");
            /* hovered/aboutToShow/aboutToHide：发射函数按头文件语义直发
             * （真实发射点在弹出/悬停路径，无头不弹窗）。 */
            XMenu_hovered_signal(m, aB);
            XAPI_EXPECT(g_menuHovered == 1 && g_menuLastAction == aB,
                        "Menu hovered 发射函数直发通路");
            XMenu_aboutToShow_signal(m);
            XAPI_EXPECT(g_menuAboutToShow == 1,
                        "Menu aboutToShow 发射函数直发通路");
            XMenu_aboutToHide_signal(m);
            XAPI_EXPECT(g_menuAboutToHide == 1,
                        "Menu aboutToHide 发射函数直发通路");

            /* ---- NULL 边界（头文件契约：NULL 不执行操作） ---- */
            XAPI_EXPECT(XMenu_addAction(NULL, NULL) == NULL,
                        "Menu addAction(NULL 菜单)=NULL");
            XAPI_EXPECT(XMenu_addSeparator(NULL) == NULL,
                        "Menu addSeparator(NULL 菜单)=NULL");
            XAPI_EXPECT(XMenu_isEmpty(NULL),
                        "Menu isEmpty(NULL)=true");
            XAPI_EXPECT(XMenu_title(NULL) == NULL,
                        "Menu title(NULL)=NULL");
            XMenu_clear(NULL); /* NULL 无操作不崩溃（契约口径）。 */

            XAction_delete_base(outsider);
            XMenu_delete_base(m); /* 级联释放 sub1/sub2（子控件登记）。 */
        }
#endif /* XMENU_ON */

        /* ============================================================
         * 2. XMenuBar：菜单栏（对标 Qt 6.8 QMenuBar）。
         *    信号说明：triggered/hovered 的真实发射点经 addMenu 桥在
         *    动作触发并弹出菜单的路径上（无头不弹窗），此处断言连接
         *    通路；文本型 addAction 在本实现无触发转发（头文件口径
         *    ——m_menus 无对应项），不按 QMenuBar::triggered 断言。
         * ============================================================ */
#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON
        {
            XMenuBar* bar = XMenuBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               root, 0);
            XMenu* menu1;
            XMenu* menu2;
            XMenu* menu3;
            XAction* help;
            XAction* ext;
            XAction* view;
            XAction* insSep;
            XAction* insMenuAct;
            XWidget* cornerL;
            XWidget* cornerR;
            XSize hint;
            XSize minHint;
            XPoint pos;

            XAPI_EXPECT(bar != NULL, "MenuBar create_ex 创建成功");

            /* ---- 默认值（QMenuBar 构造默认） ---- */
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 0,
                        "MenuBar 默认动作数=0");
            XAPI_EXPECT(!XMenuBar_isDefaultUp(bar),
                        "MenuBar 默认 defaultUp=false（QMenuBar 文档默认）");
            XAPI_EXPECT(!XMenuBar_isNativeMenuBar(bar),
                        "MenuBar 默认 nativeMenuBar=false");
            XAPI_EXPECT(XMenuBar_platformMenuBar(bar) == NULL,
                        "MenuBar platformMenuBar 恒=NULL（无平台菜单栏）");
            XAPI_EXPECT(XMenuBar_activeAction(bar) == NULL,
                        "MenuBar 默认 activeAction=NULL");
            XAPI_EXPECT(XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopLeft) ==
                            NULL &&
                        XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopRight) ==
                            NULL,
                        "MenuBar 默认角落控件=NULL");
            hint = XMenuBar_sizeHint(bar);
            XAPI_EXPECT(hint.height > 0,
                        "MenuBar sizeHint 高度为正（样式派生，不硬断常量）");
            minHint = XMenuBar_minimumSizeHint(bar);
            XAPI_EXPECT(minHint.width == hint.width &&
                        minHint.height == hint.height,
                        "MenuBar minimumSizeHint=sizeHint（实现一致性）");
            XAPI_EXPECT(XMenuBar_heightForWidth(bar, 100) == hint.height,
                        "MenuBar heightForWidth=固定高度（简化口径，Qt 随宽变化）");

            /* ---- addMenu_2：以标题建菜单并注册
             *      （QMenuBar::addMenu(const QString&)） ---- */
            menu1 = XMenuBar_addMenu_2(bar, "文件");
            XAPI_EXPECT(menu1 != NULL,
                        "MenuBar addMenu_2 返回新菜单（归调用方）");
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 1,
                        "MenuBar addMenu_2 后动作数=1");
            {
                XAction* fileAct = XMenuBar_actionAt(bar, &(XPoint){4, 10});
                XAPI_EXPECT(fileAct != NULL && XAction_text_const(fileAct) &&
                            strcmp(xapi_u8(XAction_text_const(fileAct)),
                                   "文件") == 0,
                            "MenuBar 首项动作文本=菜单标题（文件）");
            }

            /* ---- addMenu(QMenu*)：注册既有菜单 ---- */
            menu2 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                    "编辑");
            XAPI_EXPECT(XMenuBar_addMenu(bar, menu2) != NULL,
                        "MenuBar addMenu(既有菜单) 返回挂接动作");
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 2,
                        "MenuBar addMenu 后动作数=2");

            /* ---- addSeparator / addAction_2 ---- */
            XAPI_EXPECT(XMenuBar_addSeparator(bar) != NULL &&
                        XMenuBar_actionCount(bar) == 3,
                        "MenuBar addSeparator 动作数=3");
            help = XMenuBar_addAction_2(bar, "帮助");
            XAPI_EXPECT(help != NULL &&
                        XAction_text_const(help) &&
                        strcmp(xapi_u8(XAction_text_const(help)),
                               "帮助") == 0,
                        "MenuBar addAction_2 文本往返=帮助");

            /* ---- setActiveAction（QMenuBar::setActiveAction） ---- */
            XMenuBar_setActiveAction(bar, help);
            XAPI_EXPECT(XMenuBar_activeAction(bar) == help,
                        "MenuBar setActiveAction 往返");

            /* ---- 插入族（insertSeparator/insertMenu/insertAction，
             *      均对标 Qt 同名 API 的 before 语义） ---- */
            insSep = XMenuBar_insertSeparator(bar, help);
            XAPI_EXPECT(insSep != NULL &&
                        XMenuBar_actionGeometry(bar, insSep).x <
                            XMenuBar_actionGeometry(bar, help).x,
                        "MenuBar insertSeparator(before=帮助) 插到其前");
            menu3 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                    "工具");
            insMenuAct = XMenuBar_insertMenu(bar, help, menu3);
            XAPI_EXPECT(insMenuAct != NULL &&
                        XMenuBar_actionGeometry(bar, insMenuAct).x <
                            XMenuBar_actionGeometry(bar, help).x,
                        "MenuBar insertMenu(before=帮助) 菜单插到其前");
            ext = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    (XObject*)root, "外部");
            XAPI_EXPECT(XMenuBar_insertAction(bar, help, ext) == ext &&
                        XMenuBar_actionCount(bar) == 7 &&
                        XMenuBar_actionGeometry(bar, ext).x <
                            XMenuBar_actionGeometry(bar, help).x,
                        "MenuBar insertAction 插到帮助前（返回 action 本身）");

            /* ---- removeAction：借用动作摘除后归调用方
             *      （对标 QWidget::removeAction 所有权语义） ---- */
            XMenuBar_removeAction(bar, ext);
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 6,
                        "MenuBar removeAction 后动作数=6");
            XAPI_EXPECT(XMenuBar_insertAction(bar, NULL, ext) == ext &&
                        XMenuBar_actionGeometry(bar, ext).x >
                            XMenuBar_actionGeometry(bar, help).x,
                        "MenuBar insertAction(before=NULL) 等价追加末尾");
            XMenuBar_removeAction(bar, ext); /* ext 归 root 级联管理。 */

            /* ---- removeAction 摘除当前激活动作时清空引用 ---- */
            view = XMenuBar_addAction_2(bar, "视图");
            XMenuBar_setActiveAction(bar, view);
            XMenuBar_removeAction(bar, view);
            XAPI_EXPECT(XMenuBar_activeAction(bar) == NULL,
                        "MenuBar 移除激活动作后 activeAction=NULL");
            XAction_delete_base(view); /* 所有权已归还调用方。 */
            XMenuBar_removeAction(bar, help);
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 5,
                        "MenuBar removeAction(帮助) 后动作数=5");
            XAction_delete_base(help); /* 头文件口径：移除后归调用方销毁。 */

            /* ---- setDefaultUp / setNativeMenuBar（存储位） ---- */
            XMenuBar_setDefaultUp(bar, true);
            XAPI_EXPECT(XMenuBar_isDefaultUp(bar),
                        "MenuBar setDefaultUp(true) 往返");
            XMenuBar_setDefaultUp(bar, false);
            XAPI_EXPECT(!XMenuBar_isDefaultUp(bar),
                        "MenuBar setDefaultUp(false) 复位（QMenuBar 默认）");
            XMenuBar_setNativeMenuBar(bar, true);
            XAPI_EXPECT(XMenuBar_isNativeMenuBar(bar),
                        "MenuBar setNativeMenuBar 存储位往返");
            XMenuBar_setNativeMenuBar(bar, false);

            /* ---- 角落控件（QMenuBar::setCornerWidget/cornerWidget） ---- */
            cornerL = XWidget_create(root, 0);
            cornerR = XWidget_create(root, 0);
            XMenuBar_setCornerWidget(bar, cornerL,
                                     (int)XMenuBarCorner_TopLeft);
            XAPI_EXPECT(XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopLeft) ==
                            cornerL,
                        "MenuBar 左上角控件往返");
            XAPI_EXPECT(XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopRight) ==
                            NULL,
                        "MenuBar 右上角不受左上角设置影响");
            XMenuBar_setCornerWidget(bar, cornerR,
                                     (int)XMenuBarCorner_TopRight);
            XAPI_EXPECT(XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopRight) ==
                            cornerR,
                        "MenuBar 右上角控件往返");
            XMenuBar_setCornerWidget(bar, NULL,
                                     (int)XMenuBarCorner_TopLeft);
            XAPI_EXPECT(XMenuBar_cornerWidget(bar,
                                              (int)XMenuBarCorner_TopLeft) ==
                            NULL,
                        "MenuBar setCornerWidget(NULL) 清除（Qt 语义）");

            /* ---- actionAt 边界（QMenuBar::actionAt） ---- */
            XPoint_init(&pos, 10, 100);
            XAPI_EXPECT(XMenuBar_actionAt(bar, &pos) == NULL,
                        "MenuBar actionAt(Y 越界)=NULL（栏高护栏）");
            XAPI_EXPECT(XMenuBar_actionAt(bar, NULL) == NULL,
                        "MenuBar actionAt(NULL 坐标)=NULL");

            /* ---- 信号连接通路（句柄型；发射点见小节头注释） ---- */
#if XACTION_ON
            XAPI_EXPECT(XObject_connect_1(
                            (XObject*)bar,
                            XSignal(XMenuBar_triggered_signal),
                            (XObject*)bar,
                            menus_grpTriggeredSlot,
                            XConnectionType_Direct) != NULL,
                        "MenuBar triggered 信号可连接（句柄通路）");
            XAPI_EXPECT(XObject_connect_1(
                            (XObject*)bar,
                            XSignal(XMenuBar_hovered_signal),
                            (XObject*)bar,
                            menus_grpHoveredSlot,
                            XConnectionType_Direct) != NULL,
                        "MenuBar hovered 信号可连接（句柄通路）");
#endif

            /* ---- clear：销毁自有动作、摘除借用（对标 QMenuBar::clear） ---- */
            XMenuBar_clear(bar);
            XAPI_EXPECT(XMenuBar_actionCount(bar) == 0,
                        "MenuBar clear 后动作数=0");

            XMenu_delete_base(menu1);
            XMenu_delete_base(menu2);
            XMenu_delete_base(menu3); /* 菜单归调用方（头文件口径）。 */
        }
#endif /* XWIDGET_ON && XMENU_ON && XMENUBAR_ON */

        /* ============================================================
         * 3. XToolBar：工具栏（对标 Qt 6.8 QToolBar）。
         * ============================================================ */
#if XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON
        {
            XToolBar* bar = XToolBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               root, 0);
            XAction* a1;
            XAction* sep0;
            XAction* ext;
            XAction* insSep;
            XAction* tav;
            XWidget* w;
            XAction* wAct;
            XPoint pos;
            XRect geo;
            int i;
            bool v0;

            XAPI_EXPECT(bar != NULL, "ToolBar create_ex 创建成功");
            menus_tbReset();
            menus_tbConnect((XObject*)bar);

            /* ---- 默认值（QToolBar 构造默认） ---- */
            XAPI_EXPECT(XToolBar_isMovable(bar),
                        "ToolBar 默认 movable=true（QToolBar 文档默认）");
            XAPI_EXPECT(XToolBar_isFloatable(bar),
                        "ToolBar 默认 floatable=true（QToolBar 文档默认）");
            XAPI_EXPECT(XToolBar_orientation(bar) == 1,
                        "ToolBar 默认方向=1 水平（Qt::Horizontal=1）");
            XAPI_EXPECT(XToolBar_allowedAreas(bar) ==
                            ((int)XToolBarArea_Left |
                             (int)XToolBarArea_Right |
                             (int)XToolBarArea_Top |
                             (int)XToolBarArea_Bottom),
                        "ToolBar 默认允许全部停靠区（QToolBar AllAreas）");
            XAPI_EXPECT(XToolBar_iconSize(bar) == 16,
                        "ToolBar 默认图标 16（头文件口径，Qt 由样式派生）");
            /* 按钮风格默认：实现为 TextOnly(1)，Qt 文档默认
             * ToolButtonIconOnly(0)——实现与 Qt 存在偏差，不按 Qt 硬
             * 断言（偏差已记录于套件 notes），仅断言确定取值。 */
            XAPI_EXPECT(XToolBar_toolButtonStyle(bar) ==
                            (int)XToolButtonStyle_TextOnly,
                        "ToolBar 默认按钮风格=TextOnly（实现口径，非 Qt 默认）");
            XAPI_EXPECT(!XToolBar_isFloating(bar),
                        "ToolBar 默认 isFloating=false（存储位）");
            XAPI_EXPECT(XToolBar_actionCount(bar) == 0,
                        "ToolBar 默认动作数=0");
            XToolBar_setTitle(bar, "工具条");
            XAPI_EXPECT(strcmp(xapi_cstr(XToolBar_title(bar)), "工具条") == 0,
                        "ToolBar setTitle/title 往返=工具条");

            /* ---- movable/floatable 往返 + movableChanged 真发射 ---- */
            XToolBar_setMovable(bar, false);
            XAPI_EXPECT(!XToolBar_isMovable(bar) && g_tbMovableChanged == 1 &&
                        !g_tbLastMovable,
                        "ToolBar setMovable(false) 往返并发射 movableChanged");
            XToolBar_setMovable(bar, false);
            XAPI_EXPECT(g_tbMovableChanged == 1,
                        "ToolBar 重复 setMovable 同值为无发射（changed 判定）");
            XToolBar_setMovable(bar, true);
            XToolBar_setFloatable(bar, false);
            XAPI_EXPECT(!XToolBar_isFloatable(bar),
                        "ToolBar setFloatable(false) 往返");
            XToolBar_setFloatable(bar, true);
            XAPI_EXPECT(XToolBar_isFloatable(bar),
                        "ToolBar setFloatable(true) 复位（QToolBar 默认）");

            /* ---- orientation 往返 + orientationChanged ---- */
            XToolBar_setOrientation(bar, 2 /* 竖排（Qt::Vertical=2） */);
            XAPI_EXPECT(XToolBar_orientation(bar) == 2 &&
                        g_tbOrientChanged == 1 && g_tbLastOrient == 2,
                        "ToolBar setOrientation(2) 往返并发射");
            XToolBar_setOrientation(bar, 1);
            XAPI_EXPECT(XToolBar_orientation(bar) == 1,
                        "ToolBar 方向复位水平（Qt::Horizontal）");

            /* ---- allowedAreas/isAreaAllowed + 变化信号 ---- */
            XToolBar_setAllowedAreas(bar, (int)XToolBarArea_Top |
                                          (int)XToolBarArea_Left);
            XAPI_EXPECT(XToolBar_allowedAreas(bar) ==
                            ((int)XToolBarArea_Top | (int)XToolBarArea_Left),
                        "ToolBar setAllowedAreas 位掩码往返");
            XAPI_EXPECT(XToolBar_isAreaAllowed(bar, (int)XToolBarArea_Top) &&
                        !XToolBar_isAreaAllowed(bar,
                                                (int)XToolBarArea_Bottom),
                        "ToolBar isAreaAllowed 按位判定（isAreaAllowed）");
            XToolBar_setAllowedAreas(bar, (int)XToolBarArea_Left |
                                          (int)XToolBarArea_Right |
                                          (int)XToolBarArea_Top |
                                          (int)XToolBarArea_Bottom);

            /* ---- iconSize：正往返 + 非正值忽略（实现边界口径） ---- */
            XToolBar_setIconSize(bar, 24);
            XAPI_EXPECT(XToolBar_iconSize(bar) == 24 &&
                        g_tbIconSizeChanged == 1,
                        "ToolBar setIconSize(24) 往返并发射 iconSizeChanged");
            XToolBar_setIconSize(bar, 0);
            XAPI_EXPECT(XToolBar_iconSize(bar) == 24,
                        "ToolBar setIconSize(0) 非正值忽略（实现边界）");
            XToolBar_setIconSize(bar, 16);

            /* ---- toolButtonStyle 往返 + 变化信号 ---- */
            XToolBar_setToolButtonStyle(bar,
                                        (int)XToolButtonStyle_TextBesideIcon);
            XAPI_EXPECT(XToolBar_toolButtonStyle(bar) ==
                            (int)XToolButtonStyle_TextBesideIcon,
                        "ToolBar setToolButtonStyle 往返=TextBesideIcon");
            XToolBar_setToolButtonStyle(bar, (int)XToolButtonStyle_TextOnly);

            /* ---- 动作管理：addAction_2/addSeparator/addAction(外部) ---- */
            a1 = XToolBar_addAction_2(bar, "新建");
            XAPI_EXPECT(a1 != NULL && XToolBar_actionCount(bar) == 1,
                        "ToolBar addAction_2 创建并追加动作");
            sep0 = XToolBar_addSeparator(bar);
            XAPI_EXPECT(sep0 != NULL && XAction_isSeparator(sep0) &&
                        XToolBar_actionCount(bar) == 2,
                        "ToolBar addSeparator 追加分隔动作");
            ext = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    (XObject*)root, "外部");
            XToolBar_addAction(bar, ext);
            XAPI_EXPECT(XToolBar_action(bar, 2) == ext,
                        "ToolBar addAction(外部动作) 追加（动作归调用方）");
            insSep = XToolBar_insertSeparator(bar, a1);
            XAPI_EXPECT(insSep != NULL && XToolBar_action(bar, 0) == insSep &&
                        XToolBar_action(bar, 1) == a1,
                        "ToolBar insertSeparator(before=新建) 插到表头");
            XAPI_EXPECT(XToolBar_action(bar, 99) == NULL &&
                        XToolBar_action(bar, -1) == NULL,
                        "ToolBar action(index) 越界返回 NULL");

            /* ---- addWidget/widgetForAction（QToolBar::addWidget） ---- */
            w = XWidget_create(root, 0);
            XWidget_setGeometry(w, 0, 0, 60, 20);
            XToolBar_addWidget(bar, w);
            XAPI_EXPECT(XToolBar_actionCount(bar) == 5,
                        "ToolBar addWidget 占位动作计入数量");
            wAct = NULL;
            for (i = 0; i < XToolBar_actionCount(bar); ++i) {
                if (XToolBar_widgetForAction(bar,
                                             XToolBar_action(bar, i)) == w) {
                    wAct = XToolBar_action(bar, i);
                    break;
                }
            }
            XAPI_EXPECT(wAct != NULL && XToolBar_action(bar, 4) == wAct,
                        "ToolBar widgetForAction 反查=插入的控件位");
            XAPI_EXPECT(XToolBar_widgetForAction(bar, a1) == NULL,
                        "ToolBar 动作按钮无关联控件（头文件口径）");

            /* ---- actionTriggered/actionHovered 转发（真发射经桥） ---- */
            XAction_trigger(a1);
            XAPI_EXPECT(g_tbActionTriggered == 1 && g_tbLastAction == a1,
                        "ToolBar 触发动作转发 actionTriggered(新建)");
            XAction_hover(a1);
            XAPI_EXPECT(g_tbActionHovered == 1 && g_tbLastAction == a1,
                        "ToolBar 悬停动作转发 actionHovered(新建)");

            /* ---- actionAt/actionGeometry（QToolBar 同名几何 API） ---- */
            XAPI_EXPECT(XToolBar_actionAt(bar, NULL) == NULL,
                        "ToolBar actionAt(NULL 坐标)=NULL");
            XPoint_init(&pos, 20, 10);
            XAPI_EXPECT(XToolBar_actionAt(bar, &pos) == insSep,
                        "ToolBar actionAt(首段中心)=分隔动作");
            XPoint_init(&pos, 70, 10);
            XAPI_EXPECT(XToolBar_actionAt(bar, &pos) == a1,
                        "ToolBar actionAt(第二段)=新建");
            XPoint_init(&pos, 160, 10);
            XAPI_EXPECT(XToolBar_actionAt(bar, &pos) == ext,
                        "ToolBar actionAt(第四段)=外部动作");
            XPoint_init(&pos, 20, 40);
            XAPI_EXPECT(XToolBar_actionAt(bar, &pos) == NULL,
                        "ToolBar actionAt(Y 越界)=NULL");
            geo = XToolBar_actionGeometry(bar, a1);
            XAPI_EXPECT(geo.width > 0 && geo.height > 0,
                        "ToolBar actionGeometry 宽高均为正");
            XPoint_init(&pos, geo.x + geo.width / 2,
                        geo.y + geo.height / 2);
            XAPI_EXPECT(XToolBar_actionAt(bar, &pos) == a1,
                        "ToolBar actionGeometry/actionAt 自洽");
            geo = XToolBar_actionGeometry(bar, XToolBar_action(bar, 1) == a1
                                                ? a1 : a1);
            XAPI_EXPECT(geo.x >= 0, "ToolBar 动作几何起点非负");

            /* ---- toggleViewAction（QToolBar::toggleViewAction） ---- */
            tav = XToolBar_toggleViewAction(bar);
            XAPI_EXPECT(tav != NULL &&
                        XToolBar_toggleViewAction(bar) == tav,
                        "ToolBar toggleViewAction 懒创建且缓存同指针");
            XAPI_EXPECT(XAction_isCheckable(tav),
                        "ToolBar toggleViewAction 可选中（QToolBar 口径）");
            /* 生效可见性受父链牵制（套件 root 未 show，isVisible 恒
             * false，见 XWidget 显隐口径）；开关动作对标 Qt _q_toggleView
             * 操作显式隐藏位：断言 isHidden 翻转、isVisibleTo(父) 随动。 */
            v0 = XWidget_isHidden((XWidget*)bar);
            XAction_trigger(tav);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)bar) != v0 &&
                        XWidget_isVisibleTo((XWidget*)bar, root) == v0,
                        "ToolBar 触发开关动作切换显式可见性（toggleView）");
            XAction_trigger(tav);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)bar) == v0,
                        "ToolBar 再次触发恢复显式可见性（往返）");

            /* ---- 手动发射句柄（visibilityChanged/topLevelChanged，
             *      头文件口径：无内部自动发射点，供上层驱动） ---- */
            XToolBar_visibilityChanged_signal(bar, true);
            XAPI_EXPECT(g_tbVisibility == 1 && g_tbLastVisibility,
                        "ToolBar visibilityChanged 句柄直发通路");
            XToolBar_topLevelChanged_signal(bar, true);
            XAPI_EXPECT(g_tbTopLevel == 1,
                        "ToolBar topLevelChanged 句柄直发通路");

            /* ---- removeAction/clear（本实现连带销毁动作——实现口径） ---- */
            XToolBar_removeAction(bar, a1);
            XAPI_EXPECT(XToolBar_actionCount(bar) == 4,
                        "ToolBar removeAction 后动作数=4");
            XToolBar_clear(bar);
            XAPI_EXPECT(XToolBar_actionCount(bar) == 0 &&
                        XToolBar_actionAt(bar, &pos) == NULL,
                        "ToolBar clear 后动作清空");
        }
#endif /* XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON */

        /* ============================================================
         * 4. XStatusBar：状态栏（对标 Qt 6.8 QStatusBar）。
         * ============================================================ */
#if XWIDGET_ON && XSTATUSBAR_ON
        {
            XStatusBar* sb = XStatusBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                  root, 0);
            XWidget* w1;
            XWidget* w2;
            XWidget* w3;
            XWidget* w4;
            XWidget* p1;
            XWidget* p2;

            XAPI_EXPECT(sb != NULL, "StatusBar create_ex 创建成功");
            menus_sbReset();
            menus_sbConnect((XObject*)sb);

            /* ---- 默认值（QStatusBar 构造默认） ---- */
            XAPI_EXPECT(strcmp(xapi_cstr(XStatusBar_currentMessage(sb)), "") == 0,
                        "StatusBar 默认当前消息=空串");
            XAPI_EXPECT(XStatusBar_isSizeGripEnabled(sb),
                        "StatusBar 默认 sizeGrip=true（QStatusBar 文档默认）");

            /* ---- showMessage/clearMessage/currentMessage + 信号 ---- */
            XStatusBar_showMessage(sb, "已保存", 0);
            XAPI_EXPECT(strcmp(xapi_cstr(XStatusBar_currentMessage(sb)), "已保存") == 0,
                        "StatusBar showMessage 往返=已保存");
            XAPI_EXPECT(g_sbMessageChanged == 1 &&
                        strcmp(g_sbLastMessage, "已保存") == 0,
                        "StatusBar showMessage 发射 messageChanged(已保存)");
            XStatusBar_showMessage(sb, NULL, 0);
            XAPI_EXPECT(strcmp(xapi_cstr(XStatusBar_currentMessage(sb)), "") == 0,
                        "StatusBar showMessage(NULL) 等价清除（QStatusBar 语义）");
            XAPI_EXPECT(g_sbMessageChanged == 2 &&
                        strcmp(g_sbLastMessage, "") == 0,
                        "StatusBar 清除发射 messageChanged(空串)");
            XStatusBar_clearMessage(sb);
            /* Qt 6.8 hideOrShow 无条件发射 messageChanged（空消息再清除
             * 仍发射）；showMessage(NULL) 置空那次已发射计 1 次。 */
            XAPI_EXPECT(g_sbMessageChanged == 3,
                        "StatusBar 空消息再清除仍发射（Qt 无条件发射口径）");
            XStatusBar_showMessage(sb, "加载中", 0);
            XStatusBar_clearMessage(sb);
            XAPI_EXPECT(strcmp(xapi_cstr(XStatusBar_currentMessage(sb)), "") == 0 &&
                        g_sbMessageChanged == 5,
                        "StatusBar clearMessage 清空并发射 messageChanged");
            /* timeout>0 自动清除为定时器路径（QStatusBar 超时语义），
             * 无头同步运行不经事件循环，不硬断言其到期行为。 */
            XStatusBar_showMessage(sb, "临时", 50);
            XAPI_EXPECT(strcmp(xapi_cstr(XStatusBar_currentMessage(sb)), "临时") == 0,
                        "StatusBar showMessage 带超时立即生效");
            XStatusBar_clearMessage(sb);

            /* ---- 普通区插入：索引语义（QStatusBar::insertWidget） ---- */
            w1 = XWidget_create(root, 0);
            w2 = XWidget_create(root, 0);
            w3 = XWidget_create(root, 0);
            w4 = XWidget_create(root, 0);
            XAPI_EXPECT(XStatusBar_insertWidget(sb, 0, w1, 0) == 0,
                        "StatusBar insertWidget(0) 返回实际下标 0");
            XAPI_EXPECT(XStatusBar_insertWidget(sb, 99, w2, 0) == 1,
                        "StatusBar 越界下标钳制到末尾（返回 1）");
            XAPI_EXPECT(XStatusBar_insertWidget(sb, -1, w3, 0) == 2,
                        "StatusBar 负下标等价追加（返回 2）");
            XAPI_EXPECT(XStatusBar_insertWidget(sb, 0, NULL, 0) == -1,
                        "StatusBar insertWidget(NULL 控件)=-1");

            /* ---- 消息显示期间普通区控件被隐藏（QStatusBar hideOrShow
             *      语义）；生效可见性受父链牵制，按 isHidden 显式口径
             *      断言（见 XWidget 显隐口径）。 ---- */
            XStatusBar_showMessage(sb, "消息中", 0);
            XAPI_EXPECT(XWidget_isHidden(w1),
                        "StatusBar showMessage 致隐已有普通区控件（hideOrShow）");
            XStatusBar_addWidget(sb, w4, 0);
            XAPI_EXPECT(!XWidget_isVisible(w4),
                        "StatusBar 消息显示时新增普通区控件隐藏");
            XStatusBar_clearMessage(sb);
            XAPI_EXPECT(!XWidget_isHidden(w1),
                        "StatusBar clearMessage 恢复消息致隐的普通区控件");

            /* ---- 永久区（QStatusBar::addPermanentWidget：不被消息遮挡；
             *      hideOrShow 只遍历普通区，显式口径断言见上） ---- */
            p1 = XWidget_create(root, 0);
            p2 = XWidget_create(root, 0);
            XStatusBar_addPermanentWidget(sb, p1, 0);
            XAPI_EXPECT(XWidget_parentWidget(p1) == (XWidget*)sb,
                        "StatusBar 永久区控件挂到状态栏");
            XAPI_EXPECT(XStatusBar_insertPermanentWidget(sb, -1, p2, 0) == 1,
                        "StatusBar insertPermanentWidget 追加返回下标 1");
            XStatusBar_showMessage(sb, "遮挡验证", 0);
            XAPI_EXPECT(!XWidget_isHidden(p1) && !XWidget_isHidden(p2),
                        "StatusBar 永久区控件不受消息显示影响（isHidden 显式口径）");
            XStatusBar_clearMessage(sb);

            /* ---- removeWidget：解除父子关系、归调用方（Qt 语义） ---- */
            XStatusBar_removeWidget(sb, w1);
            XAPI_EXPECT(XWidget_parentWidget(w1) == NULL,
                        "StatusBar removeWidget 后控件父级=NULL");
            XWidget_setParent(w1, root, 0); /* 归还根控件便于级联回收。 */
            XStatusBar_removeWidget(sb, w1);
            XAPI_EXPECT(XWidget_parentWidget(w1) == (XWidget*)root,
                        "StatusBar removeWidget 不在栏内为无操作");
            XStatusBar_removeWidget(sb, p1);
            XAPI_EXPECT(XWidget_parentWidget(p1) == NULL,
                        "StatusBar removeWidget 横跨普通/永久区查找");
            XWidget_setParent(p1, root, 0);

            /* ---- sizeGrip 开关（QStatusBar::setSizeGripEnabled） ---- */
            XStatusBar_setSizeGripEnabled(sb, false);
            XAPI_EXPECT(!XStatusBar_isSizeGripEnabled(sb),
                        "StatusBar setSizeGripEnabled(false) 往返");
            XStatusBar_setSizeGripEnabled(sb, true);
            XAPI_EXPECT(XStatusBar_isSizeGripEnabled(sb),
                        "StatusBar setSizeGripEnabled(true) 复位（默认）");
        }
#endif /* XWIDGET_ON && XSTATUSBAR_ON */

        /* ============================================================
         * 5. XAction：动作（对标 Qt 6.8 QAction）。
         * ============================================================ */
#if XACTION_ON
        {
            XAction a;
            XAction a2;
            XVariant* v;
            const XString* t;
            XMenu* sub;
            bool saw;

            XAction_init(&a);
            menus_actReset();
            menus_actConnect((XObject*)&a);

            /* ---- 默认值（QAction 构造默认） ---- */
            XAPI_EXPECT(!XAction_isCheckable(&a),
                        "Action 默认 checkable=false（QAction 文档默认）");
            XAPI_EXPECT(!XAction_isChecked(&a),
                        "Action 默认 checked=false");
            XAPI_EXPECT(XAction_isEnabled(&a),
                        "Action 默认 enabled=true（QAction 文档默认）");
            XAPI_EXPECT(XAction_isVisible(&a),
                        "Action 默认 visible=true（QAction 文档默认）");
            XAPI_EXPECT(!XAction_isSeparator(&a),
                        "Action 默认 separator=false");
            XAPI_EXPECT(XAction_priority(&a) == XActionPriority_Normal,
                        "Action 默认 priority=Normal（QAction 文档默认）");
            XAPI_EXPECT(XAction_menuRole(&a) ==
                            XActionMenuRole_TextHeuristicRole,
                        "Action 默认 menuRole=TextHeuristicRole（Qt 默认）");
            XAPI_EXPECT(!XAction_isIconVisibleInMenu(&a),
                        "Action 默认菜单不显示图标（实现初始化口径；Qt 文档默认显示——偏差记 notes）");
            XAPI_EXPECT(!XAction_isShortcutVisibleInContextMenu(&a),
                        "Action 默认上下文菜单不显示快捷键（Qt 同默认）");
            XAPI_EXPECT(XAction_data(&a) == NULL,
                        "Action 默认 data=NULL");
            XAPI_EXPECT(XAction_menu(&a) == NULL,
                        "Action 默认 menu=NULL");
            XAPI_EXPECT(XAction_text(&a) == NULL &&
                        XAction_text_const(&a) == NULL,
                        "Action 未设置文本时 text=NULL（头文件口径，Qt 为空串）");

            /* ---- 文本族往返（QAction::setText/toolTip/statusTip 等） ---- */
            XAction_setText_2(&a, "打开");
            t = XAction_text_const(&a);
            XAPI_EXPECT(t && strcmp(xapi_u8(t), "打开") == 0,
                        "Action setText_2 往返=打开");
            XAction_setText_2(&a, "打开");
            XAPI_EXPECT(g_actChanged == 1,
                        "Action 重复 setText 相同文本仅首次发射 changed");
            XAction_setText_2(&a, NULL);
            t = XAction_text_const(&a);
            XAPI_EXPECT(t && xapi_u8(t)[0] == '\0',
                        "Action setText_2(NULL) 置空文本（QAction 空串）");
            XAction_setIconText_2(&a, "图标文本");
            t = XAction_iconText_const(&a);
            XAPI_EXPECT(t && strcmp(xapi_u8(t), "图标文本") == 0,
                        "Action setIconText_2 往返");
            XAction_setToolTip_2(&a, "提示");
            t = XAction_toolTip_const(&a);
            XAPI_EXPECT(t && strcmp(xapi_u8(t), "提示") == 0,
                        "Action setToolTip_2 往返");
            XAction_setStatusTip_2(&a, "状态提示");
            t = XAction_statusTip_const(&a);
            XAPI_EXPECT(t && strcmp(xapi_u8(t), "状态提示") == 0,
                        "Action setStatusTip_2 往返");
            XAction_setWhatsThis_2(&a, "帮助说明");
            t = XAction_whatsThis_const(&a);
            XAPI_EXPECT(t && strcmp(xapi_u8(t), "帮助说明") == 0,
                        "Action setWhatsThis_2 往返");

            /* ---- 图标路径往返（QAction::setIcon；§8.0g14 图标承载） ---- */
            {
                int changedBefore = g_actChanged;
                XAction_setIcon_2(&a, "icons/apitest-icon.svg");
                t = XAction_icon_const(&a);
                XAPI_EXPECT(t && strcmp(xapi_u8(t), "icons/apitest-icon.svg") == 0,
                            "Action setIcon_2 往返=icons/apitest-icon.svg");
                XAPI_EXPECT(g_actChanged == changedBefore + 1,
                            "Action setIcon 新路径发射 changed");
                XAction_setIcon_2(&a, "icons/apitest-icon.svg");
                XAPI_EXPECT(g_actChanged == changedBefore + 1,
                            "Action setIcon 相同路径不重发 changed");
                XAction_setIcon_2(&a, NULL);
                t = XAction_icon_const(&a);
                XAPI_EXPECT(t && xapi_u8(t)[0] == '\0',
                            "Action setIcon_2(NULL) 置空图标路径（族约定空串）");
                XAPI_EXPECT(g_actChanged == changedBefore + 2,
                            "Action setIcon 清空路径发射 changed");
            }

            /* ---- 非可选中时 setChecked/toggle（QAction 语义） ---- */
            XAction_setChecked(&a, true);
            XAPI_EXPECT(!XAction_isChecked(&a) && g_actToggled == 0,
                        "Action 非可选中 setChecked 不生效不发射 toggled");
            XAction_toggle(&a);
            XAPI_EXPECT(!XAction_isChecked(&a),
                        "Action 非可选中 toggle 不改变状态");

            /* ---- checkable/checked + checkableChanged/toggled ---- */
            XAction_setCheckable(&a, true);
            XAPI_EXPECT(XAction_isCheckable(&a) &&
                        g_actCheckableChanged == 1 && g_actLastCheckable,
                        "Action setCheckable(true) 发射 checkableChanged");
            XAction_setChecked(&a, true);
            XAPI_EXPECT(XAction_isChecked(&a) && g_actToggled == 1 &&
                        g_actLastToggled,
                        "Action setChecked(true) 发射 toggled(true)");
            XAction_toggle(&a);
            XAPI_EXPECT(!XAction_isChecked(&a) && g_actToggled == 2,
                        "Action toggle 翻转选中（QAction::toggle）");

            /* ---- trigger（QAction::trigger：可选中翻转 + triggered） ---- */
            XAction_trigger(&a);
            XAPI_EXPECT(XAction_isChecked(&a) && g_actTriggered == 1 &&
                        g_actLastTriggered,
                        "Action trigger 翻转并发送 triggered(选中=true)");

            /* ---- hover（QAction::hover） ---- */
            XAction_hover(&a);
            XAPI_EXPECT(g_actHovered == 1,
                        "Action hover 发射 hovered");

            /* ---- 禁用语义：enabledChanged + 禁用忽略触发 ---- */
            XAction_setEnabled(&a, false);
            XAPI_EXPECT(!XAction_isEnabled(&a) &&
                        g_actEnabledChanged == 1 && !g_actLastEnabled,
                        "Action setEnabled(false) 发射 enabledChanged(false)");
            saw = g_actTriggered;
            XAction_trigger(&a);
            XAPI_EXPECT(g_actTriggered == saw,
                        "Action 禁用时 trigger 被忽略（QAction activate 口径）");
            XAction_setDisabled(&a, false);
            XAPI_EXPECT(XAction_isEnabled(&a),
                        "Action setDisabled(false) 等价 setEnabled(true)");
            XAction_setEnabled(&a, false);
            XAction_resetEnabled(&a);
            XAPI_EXPECT(XAction_isEnabled(&a),
                        "Action resetEnabled 清除显式禁用恢复默认启用");

            /* ---- 可见性联动（QAction：不可见动作强制禁用） ---- */
            XAction_setVisible(&a, false);
            XAPI_EXPECT(!XAction_isVisible(&a) && !XAction_isEnabled(&a),
                        "Action 隐藏动作有效启用被强制为 false（Qt 语义）");
            XAPI_EXPECT(g_actVisibleChanged >= 1,
                        "Action setVisible 发射 visibleChanged");
            XAction_setVisible(&a, true);
            XAPI_EXPECT(XAction_isVisible(&a) && XAction_isEnabled(&a),
                        "Action 恢复可见后启用随显式值恢复");

            /* ---- data（QAction::data；所有权转移为项目差异） ---- */
            v = XVariant_create_int32(42);
            XAction_setData(&a, v);
            XAPI_EXPECT(XAction_data(&a) &&
                        XVariant_toInt32(XAction_data(&a)) == 42,
                        "Action setData(42) 往返（所有权随动作）");
            XAction_setData(&a, NULL);
            XAPI_EXPECT(XAction_data(&a) == NULL,
                        "Action setData(NULL) 清空数据");

            /* ---- menu 关联（QAction::setMenu/menu） ---- */
#if XMENU_ON
            sub = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root, NULL);
            XAction_setMenu(&a, sub);
            XAPI_EXPECT(XAction_menu(&a) == sub,
                        "Action setMenu 往返（借用不取得所有权）");
            XAction_setMenu(&a, NULL);
            XAPI_EXPECT(XAction_menu(&a) == NULL,
                        "Action setMenu(NULL) 解除关联");
            XMenu_delete_base(sub);
#endif

            /* ---- priority/menuRole/separator/开关位往返 ---- */
            XAction_setPriority(&a, XActionPriority_High);
            XAPI_EXPECT(XAction_priority(&a) == XActionPriority_High,
                        "Action setPriority(High) 往返（QAction::HighPriority）");
            XAction_setMenuRole(&a, XActionMenuRole_AboutRole);
            XAPI_EXPECT(XAction_menuRole(&a) == XActionMenuRole_AboutRole,
                        "Action setMenuRole(AboutRole) 往返");
            XAction_setSeparator(&a, true);
            XAPI_EXPECT(XAction_isSeparator(&a),
                        "Action setSeparator(true) 往返（isSeparator）");
            XAction_setIconVisibleInMenu(&a, true);
            XAPI_EXPECT(XAction_isIconVisibleInMenu(&a),
                        "Action setIconVisibleInMenu 往返");
            XAction_setShortcutVisibleInContextMenu(&a, true);
            XAPI_EXPECT(XAction_isShortcutVisibleInContextMenu(&a),
                        "Action setShortcutVisibleInContextMenu 往返");

            /* ---- 带文本构造重载（QAction(text) 构造） ---- */
            XAction_init_2(&a2, NULL, "退出");
            XAPI_EXPECT(XAction_text_const(&a2) &&
                        strcmp(xapi_u8(XAction_text_const(&a2)),
                               "退出") == 0,
                        "Action init_2 初始文本=退出");
            XAction_deinit_base(&a2);
            XAction_deinit_base(&a);
        }
#endif /* XACTION_ON */

        /* ============================================================
         * 6. XActionGroup：动作分组（对标 QActionGroup）。
         * ============================================================ */
#if XACTION_ON
        {
            XActionGroup* grp = XActionGroup_create(NULL);
            XAction* a1 = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XObject*)root, "动作1");
            XAction* a2 = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XObject*)root, "动作2");
            XAction* a3 = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XObject*)root, "动作3");
            XVector* members;

            XAPI_EXPECT(grp != NULL, "ActionGroup create 创建成功");
            menus_grpReset();
            menus_grpConnect((XObject*)grp);

            /* ---- 默认值（QActionGroup 构造默认） ---- */
            XAPI_EXPECT(XActionGroup_isExclusive(grp),
                        "ActionGroup 默认 exclusive=true（Qt 文档默认）");
            XAPI_EXPECT(XActionGroup_isEnabled(grp),
                        "ActionGroup 默认 enabled=true");
            XAPI_EXPECT(XActionGroup_checkedAction(grp) == NULL,
                        "ActionGroup 默认无选中动作");

            /* ---- 成员管理（QActionGroup::addAction/actions） ---- */
            XActionGroup_addAction(grp, a1);
            XActionGroup_addAction(grp, a2);
            members = XActionGroup_actions(grp);
            XAPI_EXPECT(members &&
                        XVector_size_base((const XContainer*)members) == 2,
                        "ActionGroup addAction×2 成员数=2");
            if (members) XVector_delete_base((XClass*)members);
            XActionGroup_addAction(grp, a1);
            members = XActionGroup_actions(grp);
            XAPI_EXPECT(members &&
                        XVector_size_base((const XContainer*)members) == 2,
                        "ActionGroup 重复 addAction 忽略（Qt 口径）");
            if (members) XVector_delete_base((XClass*)members);
            XActionGroup_addAction(grp, NULL);
            members = XActionGroup_actions(grp);
            XAPI_EXPECT(members &&
                        XVector_size_base((const XContainer*)members) == 2,
                        "ActionGroup addAction(NULL) 忽略");
            if (members) XVector_delete_base((XClass*)members);

            /* ---- 选中与互斥（QActionGroup::checkedAction/exclusive） ---- */
            XAction_setCheckable(a1, true);
            XAction_setCheckable(a2, true);
            XActionGroup_setCheckedAction(grp, a1);
            XAPI_EXPECT(XActionGroup_checkedAction(grp) == a1,
                        "ActionGroup setCheckedAction 选中动作1");
            XAPI_EXPECT(g_grpTriggered == 1 && g_grpLastAction == a1,
                        "ActionGroup setCheckedAction 发射 triggered(动作1)");
            XActionGroup_setCheckedAction(grp, a2);
            XAPI_EXPECT(XActionGroup_checkedAction(grp) == a2 &&
                        !XAction_isChecked(a1),
                        "ActionGroup 互斥：选动作2 自动取消动作1");

            /* ---- 触发转发：成员触发 → 组 triggered + 互斥联动 ----
             * 计数口径：setCheckedAction 每次成功都发射组 triggered
             * （XActionGroup.h @details 口径）——选动作1、选动作2 已各
             * 发射 1 次，此处成员触发为第 3 次。 */
            XAction_trigger(a1);
            XAPI_EXPECT(g_grpTriggered == 3 && g_grpLastAction == a1 &&
                        XActionGroup_checkedAction(grp) == a1 &&
                        !XAction_isChecked(a2),
                        "ActionGroup 成员触发转发并互斥取消动作2");
            XAction_hover(a2);
            XAPI_EXPECT(g_grpHovered == 1 && g_grpLastAction == a2,
                        "ActionGroup 成员悬停转发 hovered");

            /* ---- 非互斥：允许多选（QActionGroup::setExclusive(false)） ---- */
            XActionGroup_setExclusive(grp, false);
            XAPI_EXPECT(!XActionGroup_isExclusive(grp),
                        "ActionGroup setExclusive(false) 往返");
            XAction_setChecked(a1, false);
            XAction_setChecked(a2, false);
            XAction_setChecked(a1, true);
            XAction_setChecked(a2, true);
            XAPI_EXPECT(XAction_isChecked(a1) && XAction_isChecked(a2),
                        "ActionGroup 非互斥下两成员可同时选中");
            XActionGroup_setExclusive(grp, true);

            /* ---- 分组启用转发（QActionGroup::setEnabled） ---- */
            XActionGroup_setEnabled(grp, false);
            XAPI_EXPECT(!XActionGroup_isEnabled(grp) &&
                        !XAction_isEnabled(a1) && !XAction_isEnabled(a2),
                        "ActionGroup 禁用转发到全部成员");
            XActionGroup_addAction(grp, a3);
            XAPI_EXPECT(!XAction_isEnabled(a3),
                        "ActionGroup 禁用期间新成员同样禁用");
            XActionGroup_setEnabled(grp, true);
            XAPI_EXPECT(XAction_isEnabled(a1) && XAction_isEnabled(a3),
                        "ActionGroup 恢复启用转发到成员");

            /* ---- removeAction：摘除成员、复位选中缓存 ---- */
            XActionGroup_setCheckedAction(grp, a3);
            XActionGroup_removeAction(grp, a3);
            members = XActionGroup_actions(grp);
            XAPI_EXPECT(members &&
                        XVector_size_base((const XContainer*)members) == 2,
                        "ActionGroup removeAction 后成员数=2");
            if (members) XVector_delete_base((XClass*)members);
            XAPI_EXPECT(XAction_isEnabled(a3),
                        "ActionGroup 移除不销毁动作对象（借用语义）");
            XActionGroup_setCheckedAction(grp, a3);
            XAPI_EXPECT(XActionGroup_checkedAction(grp) != a3,
                        "ActionGroup 非成员 setCheckedAction 为无操作");

            XActionGroup_delete_base(grp);
        }
#endif /* XACTION_ON */

        /* ============================================================
         * 7. XShortcut：快捷键（对标 QShortcut）。
         *    匹配简化语义见 XShortcut.h @note；创建即注册（头文件口径）。
         * ============================================================ */
        {
            XShortcut sc;
            XShortcut* scF1;
            XShortcut* scF5;
            XShortcut* scF2;
            XWidget* wFocus;
            XString* wt;

            XShortcut_init(&sc, NULL);
            /* ---- 默认值（QShortcut 构造默认） ---- */
            XAPI_EXPECT(XShortcut_key(&sc) == 0,
                        "Shortcut 默认 key=0（未设置）");
            XAPI_EXPECT(XShortcut_isEnabled(&sc),
                        "Shortcut 默认 enabled=true（QShortcut 文档默认）");
            XAPI_EXPECT(XShortcut_autoRepeat(&sc),
                        "Shortcut 默认 autoRepeat=true（QShortcut 文档默认）");
            XAPI_EXPECT(XShortcut_context(&sc) ==
                            XShortcutContext_WindowShortcut,
                        "Shortcut 默认 context=WindowShortcut（Qt 默认）");
            XShortcut_deinit_base(&sc);

            wFocus = XWidget_create(root, 0);

            /* ---- 带键创建即注册（XShortcut_create_2 + 注册表口径） ---- */
            scF1 = XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (int)XKey_F1, (XObject*)root);
            XAPI_EXPECT(scF1 != NULL && XShortcut_key(scF1) == (int)XKey_F1,
                        "Shortcut create_2 键码=F1");
            XShortcut_setContext(scF1, XShortcutContext_ApplicationShortcut);
            XAPI_EXPECT(XShortcut_match((int)XKey_F1,
                                        XShortcutContext_WindowShortcut,
                                        NULL) == scF1,
                        "Shortcut 应用级快捷键无焦点也匹配（简化语义）");
            XAPI_EXPECT(XShortcut_match((int)XKey_F2,
                                        XShortcutContext_ApplicationShortcut,
                                        NULL) == NULL,
                        "Shortcut 键不匹配返回 NULL");
            XShortcut_setEnabled(scF1, false);
            XAPI_EXPECT(XShortcut_match((int)XKey_F1,
                                        XShortcutContext_ApplicationShortcut,
                                        NULL) == NULL,
                        "Shortcut 禁用后 match 不命中（QShortcut::setEnabled）");
            XShortcut_setEnabled(scF1, true);
            XShortcut_unregister(scF1);
            XAPI_EXPECT(XShortcut_match((int)XKey_F1,
                                        XShortcutContext_ApplicationShortcut,
                                        NULL) == NULL,
                        "Shortcut unregister 后 match 不命中");
            XShortcut_register(scF1);
            XAPI_EXPECT(XShortcut_match((int)XKey_F1,
                                        XShortcutContext_ApplicationShortcut,
                                        NULL) == scF1,
                        "Shortcut register 后恢复命中");
            XShortcut_setKey(scF1, 0);
            XAPI_EXPECT(XShortcut_key(scF1) == 0,
                        "Shortcut setKey(0) 清除键码");
            XShortcut_setKey(scF1, (int)XKey_F1);
            XShortcut_delete_base(scF1); /* delete 自动注销（头文件口径）。 */
            XAPI_EXPECT(XShortcut_match((int)XKey_F1,
                                        XShortcutContext_ApplicationShortcut,
                                        NULL) == NULL,
                        "Shortcut 删除后自动注销（注册表口径）");

            /* ---- WidgetShortcut：焦点控件=创建 parent 才命中 ---- */
            scF5 = XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (int)XKey_F5,
                                         (XObject*)wFocus);
            XShortcut_setContext(scF5, XShortcutContext_WidgetShortcut);
            XAPI_EXPECT(XShortcut_match((int)XKey_F5,
                                        XShortcutContext_WidgetShortcut,
                                        wFocus) == scF5,
                        "Shortcut WidgetShortcut 焦点=parent 命中");
            XAPI_EXPECT(XShortcut_match((int)XKey_F5,
                                        XShortcutContext_WidgetShortcut,
                                        root) == NULL,
                        "Shortcut WidgetShortcut 焦点非 parent 不命中");

            /* ---- WindowShortcut：同顶层窗口命中（活动窗口语义） ---- */
            scF2 = XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (int)XKey_F2, (XObject*)root);
            XAPI_EXPECT(XShortcut_match((int)XKey_F2,
                                        XShortcutContext_WindowShortcut,
                                        root) == scF2,
                        "Shortcut WindowShortcut 同顶层命中（parent 根）");
            XAPI_EXPECT(XShortcut_match((int)XKey_F2,
                                        XShortcutContext_WindowShortcut,
                                        wFocus) == scF2,
                        "Shortcut WindowShortcut 焦点在同顶层子控件命中");
            XShortcut_setKey(scF2, (int)XKey_A);
            XAPI_EXPECT(XShortcut_key(scF2) == (int)XKey_A,
                        "Shortcut setKey 往返=Key_A");
            XShortcut_setContext(scF2,
                                 XShortcutContext_WidgetWithChildrenShortcut);
            XAPI_EXPECT(XShortcut_context(scF2) ==
                            XShortcutContext_WidgetWithChildrenShortcut,
                        "Shortcut setContext 往返=WidgetWithChildren");
            XShortcut_setAutoRepeat(scF2, false);
            XAPI_EXPECT(!XShortcut_autoRepeat(scF2),
                        "Shortcut setAutoRepeat(false) 往返（存储位）");
            XShortcut_setWhatsThis_2(scF2, "快捷键说明");
            wt = XShortcut_whatsThis(scF2);
            XAPI_EXPECT(wt && strcmp(xapi_u8(wt), "快捷键说明") == 0,
                        "Shortcut setWhatsThis_2 往返");
            if (wt) XString_delete_base((XClass*)wt);
            XShortcut_setWhatsThis(scF2, NULL);
            XAPI_EXPECT(XShortcut_whatsThis(scF2) == NULL,
                        "Shortcut setWhatsThis(NULL) 清除");

            /* ---- activated 发射（对标 QShortcut::activated） ---- */
            g_scActivated = 0;
            XObject_connect_1((XObject*)scF2,
                              XSignal(XShortcut_activated_signal),
                              (XObject*)scF2, menus_scActivatedSlot,
                              XConnectionType_Direct);
            XShortcut_activate(scF2);
            XAPI_EXPECT(g_scActivated == 1,
                        "Shortcut activate 发射 activated");
            /* activatedAmbiguously：本实现未建模歧义，仅验证可连接。 */
            XAPI_EXPECT(XObject_connect_1(
                            (XObject*)scF2,
                            XSignal(XShortcut_activatedAmbiguously_signal),
                            (XObject*)scF2, menus_scActivatedSlot,
                            XConnectionType_Direct) != NULL,
                        "Shortcut activatedAmbiguously 可连接（保留信号）");
            /* scF2/scF5 挂 root 级联释放，析构自动注销注册表。 */
        }

        /* ============================================================
         * 8. XToolTip：工具提示静态 API（对标 QToolTip）。
         * ============================================================ */
#if XWIDGET_ON
        {
            XString* txt;
            XFont f;
            XFont got;
#if XPALETTE_ON
            XPalette pal;
            XColor c;
#endif
            XWidget* w = XWidget_create(root, 0);

            XToolTip_hideText();
            /* ---- 初始态（QToolTip::hideText 后不可见、无文本） ---- */
            XAPI_EXPECT(!XToolTip_isVisible(),
                        "ToolTip hideText 后 isVisible=false");
            XAPI_EXPECT(XToolTip_text() == NULL,
                        "ToolTip hideText 后 text=NULL");

            /* ---- showText_2（QToolTip::showText 全参重载） ---- */
            XToolTip_showText_2(10, 20, "提示文本", NULL, NULL, -1);
            XAPI_EXPECT(XToolTip_isVisible(),
                        "ToolTip showText_2 后可见");
            txt = XToolTip_text();
            XAPI_EXPECT(txt && strcmp(xapi_u8(txt), "提示文本") == 0,
                        "ToolTip text 往返=提示文本");
            if (txt) XString_delete_base((XClass*)txt);

            /* ---- showText XString 主版本 ---- */
            txt = XString_create_utf8("再提示");
            XToolTip_showText(0, 0, txt, w, NULL, -1);
            XString_delete_base((XClass*)txt);
            txt = XToolTip_text();
            XAPI_EXPECT(txt && strcmp(xapi_u8(txt), "再提示") == 0,
                        "ToolTip showText(XString) 覆盖文本");
            if (txt) XString_delete_base((XClass*)txt);

            /* ---- 空文本等价 hideText（QToolTip 实现口径） ---- */
            XToolTip_showText(0, 0, NULL, w, NULL, -1);
            XAPI_EXPECT(!XToolTip_isVisible() && XToolTip_text() == NULL,
                        "ToolTip showText(NULL 文本) 等价 hideText");
            XToolTip_showText_2(0, 0, NULL, w, NULL, -1);
            XAPI_EXPECT(!XToolTip_isVisible(),
                        "ToolTip showText_2(NULL 文本) 等价 hideText");
            XToolTip_showText_2(1, 2, "驻留", w, NULL, 3000);
            XAPI_EXPECT(XToolTip_isVisible(),
                        "ToolTip 带时长/关联控件再次显示");
            XToolTip_hideText();
            XAPI_EXPECT(!XToolTip_isVisible() && XToolTip_text() == NULL,
                        "ToolTip hideText 清空文本与可见位");
            XToolTip_showText_2(3, 4, "复显", w, NULL, -1);
            XAPI_EXPECT(XToolTip_isVisible(),
                        "ToolTip 隐藏后再次 showText 复显（show/hide 往返）");
            XToolTip_hideText();

            /* ---- text() 返回副本（QToolTip::text 值语义） ---- */
            XToolTip_showText_2(5, 6, "原文", w, NULL, -1);
            txt = XToolTip_text();
            if (txt) {
                XString_assign_utf8(txt, "调用方改写");
                XString_delete_base((XClass*)txt);
            }
            txt = XToolTip_text();
            XAPI_EXPECT(txt && strcmp(xapi_u8(txt), "原文") == 0,
                        "ToolTip text 返回独立副本（改写副本不改内部）");
            if (txt) XString_delete_base((XClass*)txt);
            XToolTip_hideText();

            /* ---- font/setFont（QToolTip::font/setFont） ---- */
            XFont_init(&f);
            XFont_setPixelSize(&f, 17);
            XToolTip_setFont(&f);
            got = XToolTip_font();
            XAPI_EXPECT(XFont_pixelSize(&got) == 17,
                        "ToolTip setFont(17px) 后 font 往返");
            XFont_deinit_base((XClass*)&got);
            XFont_deinit_base((XClass*)&f);
            XToolTip_setFont(NULL); /* 实现：NULL 忽略（头文件称恢复默认
                                       ——偏差记 notes），字体保持不变。 */
            got = XToolTip_font();
            XAPI_EXPECT(XFont_pixelSize(&got) == 17,
                        "ToolTip setFont(NULL) 忽略保持原字体（实现口径）");
            XFont_deinit_base((XClass*)&got);

#if XPALETTE_ON
            /* ---- palette/setPalette（QToolTip::palette/setPalette） ---- */
            pal = XToolTip_palette();
            c = XColor_create_argb(0xFF336699u);
            XPalette_setColor(&pal, XPaletteColorGroup_Active,
                              XPaletteColorRole_Window, c);
            XToolTip_setPalette(&pal);
            {
                XPalette gotPal = XToolTip_palette();
                XColor wc = XPalette_color(&gotPal,
                                           XPaletteColorGroup_Active,
                                           XPaletteColorRole_Window);
                XAPI_EXPECT(XColor_equals(&wc, &c),
                            "ToolTip setPalette 颜色往返一致");
            }
            XToolTip_setPalette(NULL); /* 实现：NULL 忽略（不按头文件
                                          "恢复默认"断言——偏差记 notes）。 */
            {
                XPalette gotPal = XToolTip_palette();
                XColor wc = XPalette_color(&gotPal,
                                           XPaletteColorGroup_Active,
                                           XPaletteColorRole_Window);
                XAPI_EXPECT(XColor_equals(&wc, &c),
                            "ToolTip setPalette(NULL) 忽略保持原调色板");
            }
#endif

            XToolTip_hideText(); /* 套件收尾：复位全局提示状态。 */
        }
#endif /* XWIDGET_ON */

        /* ============================================================
         * 9. XErrorMessage：错误消息对话框（对标 QErrorMessage）。
         * ============================================================ */
#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON
        {
            XErrorMessage* em = XErrorMessage_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, root, 0);
            XErrorMessage em2;

            XAPI_EXPECT(em != NULL, "ErrorMessage create_ex 创建成功");
            XAPI_EXPECT(XErrorMessage_class_init() != NULL,
                        "ErrorMessage class_init 虚表非 NULL");
            XErrorMessage_init(&em2, root, 0);

            /* ---- 默认值 ---- */
            XAPI_EXPECT(XErrorMessage_isDoneShown(&em2),
                        "ErrorMessage 默认 doneShown=true（允许展示）");
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(&em2)), "") == 0,
                        "ErrorMessage 默认当前消息=空串");
            XClass_deinit_base((XClass*)&em2);
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(NULL)), "") == 0,
                        "ErrorMessage currentMessage(NULL)=空串（契约）");

            /* ---- showMessage/currentMessage（QErrorMessage::showMessage；
             *      实现经 XWidget_show 置显式显示位。套件 root 未 show，
             *      生效可见性（isVisible 含父链）恒 false，断言用
             *      isHidden/isVisibleTo 显式口径，见 XWidget 显隐口径） ---- */
            XErrorMessage_showMessage(em, "错误 A");
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "错误 A") == 0,
                        "ErrorMessage showMessage 往返=错误 A");
            XAPI_EXPECT(!XWidget_isHidden((XWidget*)em) &&
                        XWidget_isVisibleTo((XWidget*)em, root),
                        "ErrorMessage showMessage 置对话框为显示态（显式口径）");
            XErrorMessage_showMessage(em, "错误 B");
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "错误 B") == 0,
                        "ErrorMessage showMessage 覆盖旧消息");
            XErrorMessage_showMessage(em, "错误 B");
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "错误 B") == 0,
                        "ErrorMessage 重复 showMessage 同文本保持");
            XErrorMessage_showMessage(em, "");
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "") == 0,
                        "ErrorMessage showMessage(空串) 置空消息");
            XErrorMessage_showMessage(em, "保留");
            XErrorMessage_showMessage(em, NULL);
            XAPI_EXPECT(strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "保留") == 0,
                        "ErrorMessage showMessage(NULL) 为无操作（契约）");

            /* ---- setDoneShown/isDoneShown（Qt 6.6+ 同名 API） ---- */
            XErrorMessage_setDoneShown(em, false);
            XAPI_EXPECT(!XErrorMessage_isDoneShown(em),
                        "ErrorMessage setDoneShown(false) 往返");
            XErrorMessage_setDoneShown(em, true);
            XAPI_EXPECT(XErrorMessage_isDoneShown(em),
                        "ErrorMessage setDoneShown(true) 复位");

            /* ---- 未 show 直接调显隐（无头口径；显式口径断言） ---- */
            XWidget_setVisible((XWidget*)em, false);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)em),
                        "ErrorMessage setVisible(false) 状态关闭");
            XErrorMessage_showMessage(em, "再现");
            XAPI_EXPECT(!XWidget_isHidden((XWidget*)em) &&
                        strcmp(xapi_cstr(XErrorMessage_currentMessage(em)), "再现") == 0,
                        "ErrorMessage showMessage 再次置消息并显示");
        }
#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */

        /* ============================================================
         * 10. XSplashScreen：启动画面（对标 QSplashScreen）。
         * ============================================================ */
#if XWIDGET_ON && XSPLASHSCREEN_ON
        {
            XSplashScreen* sp = XSplashScreen_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, root, 0);
            XWidget* mainWin = XWidget_create(root, 0);

            XAPI_EXPECT(sp != NULL, "SplashScreen create_ex 创建成功");
            XAPI_EXPECT(XSplashScreen_class_init() != NULL,
                        "SplashScreen class_init 虚表非 NULL");
            g_spMessageChanged = 0;
            g_spLastMessage[0] = '\0';
            XObject_connect_1((XObject*)sp,
                              XSignal(XSplashScreen_messageChanged_signal),
                              (XObject*)sp, menus_spMessageChangedSlot,
                              XConnectionType_Direct);

            /* ---- 默认值（头文件口径：无图时 400x300） ---- */
            XAPI_EXPECT(XWidget_width((XWidget*)sp) == 400 &&
                        XWidget_height((XWidget*)sp) == 300,
                        "SplashScreen 默认尺寸=400x300（头文件口径）");
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "") == 0,
                        "SplashScreen 默认消息=空串");
#if XPIXMAP_ON
            XAPI_EXPECT(XSplashScreen_pixmap(sp) == NULL,
                        "SplashScreen 默认无背景图");
#endif

            /* ---- showMessage/message（QSplashScreen::showMessage） ---- */
            XSplashScreen_showMessage(sp, "启动中...",
                                      (int)XAlignment_Left, 0xFFFFFFFFu);
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "启动中...") == 0,
                        "SplashScreen showMessage 往返=启动中...");
            XAPI_EXPECT(g_spMessageChanged == 1 &&
                        strcmp(g_spLastMessage, "启动中...") == 0,
                        "SplashScreen showMessage 发射 messageChanged");
            XSplashScreen_showMessage(sp, "60%", (int)XAlignment_HCenter,
                                      0xFFFFFFFFu);
            XAPI_EXPECT(g_spMessageChanged == 2 &&
                        strcmp(g_spLastMessage, "60%") == 0,
                        "SplashScreen 二次 showMessage 覆盖并再发射");
            XSplashScreen_showMessage(sp, NULL, 0, 0);
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "") == 0,
                        "SplashScreen showMessage(NULL) 置空消息");
            /* Qt 6.8 qsplashscreen.cpp：showMessage/clearMessage 无条件
             * 发射 messageChanged（空消息再清除仍发射）。计数：启动中(1)
             * →60%(2)→NULL 置空(3)→空清除(4)。 */
            XSplashScreen_clearMessage(sp);
            XAPI_EXPECT(g_spMessageChanged == 4,
                        "SplashScreen clearMessage 空消息仍发射（Qt 无条件口径）");
            XSplashScreen_showMessage(sp, "收尾", (int)XAlignment_Left,
                                      0xFFFFFFFFu);
            XSplashScreen_clearMessage(sp);
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "") == 0 &&
                        g_spMessageChanged == 6,
                        "SplashScreen clearMessage 清空并发射（Qt 无条件口径）");
            XSplashScreen_showMessage(sp, "结束",
                                      (int)XAlignment_HCenter |
                                          (int)XAlignment_VCenter,
                                      0xFFFFFFFFu);
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "结束") == 0,
                        "SplashScreen 居中对齐变体消息往返");

            /* ---- finish（QSplashScreen::finish；简化为直接关闭） ---- */
            XSplashScreen_finish(sp, mainWin);
            XAPI_EXPECT(!XWidget_isVisible((XWidget*)sp),
                        "SplashScreen finish 后画面关闭（可见位=false）");

            /* ---- repaint（QSplashScreen::repaint：仅重绘，不改消息；
             *      此处消息应为上一条 showMessage 的"结束"） ---- */
            XSplashScreen_repaint(sp);
            XAPI_EXPECT(strcmp(xapi_cstr(XSplashScreen_message(sp)), "结束") == 0,
                        "SplashScreen repaint 不改变消息状态（仍=结束）");

            /* ---- 基类几何（QRubberBand 同款沿用 XWidget 几何口径） ---- */
            XWidget_resize((XWidget*)sp, 320, 240);
            XAPI_EXPECT(XWidget_width((XWidget*)sp) == 320 &&
                        XWidget_height((XWidget*)sp) == 240,
                        "SplashScreen resize 往返=320x240");
        }
#endif /* XWIDGET_ON && XSPLASHSCREEN_ON */

        /* ============================================================
         * 11. XFocusFrame：焦点框（对标 QFocusFrame）。
         * ============================================================ */
#if XWIDGET_ON && XFOCUSFRAME_ON
        {
            XFocusFrame ff;
            XFocusFrame* ffh;
            XWidget* w1 = XWidget_create(root, 0);
            XWidget* w2 = XWidget_create(root, 0);

            XAPI_EXPECT(XFocusFrame_class_init() != NULL,
                        "FocusFrame class_init 虚表非 NULL");
            XFocusFrame_init(&ff, root, 0);

            /* ---- widget 默认/关联往返（QFocusFrame::setWidget） ---- */
            XAPI_EXPECT(XFocusFrame_widget(&ff) == NULL,
                        "FocusFrame 默认未关联目标控件");
            XFocusFrame_setWidget(&ff, w1);
            XAPI_EXPECT(XFocusFrame_widget(&ff) == w1,
                        "FocusFrame setWidget 往返=w1");
            XFocusFrame_setWidget(&ff, w1);
            XAPI_EXPECT(XFocusFrame_widget(&ff) == w1,
                        "FocusFrame 重复 setWidget 同控件保持");
            XFocusFrame_setWidget(&ff, NULL);
            XAPI_EXPECT(XFocusFrame_widget(&ff) == NULL,
                        "FocusFrame setWidget(NULL) 解除关联（Qt 语义）");
            XFocusFrame_setWidget(&ff, w1);
            XFocusFrame_setWidget(&ff, w2);
            XAPI_EXPECT(XFocusFrame_widget(&ff) == w2,
                        "FocusFrame 切换目标控件=w2");
            XFocusFrame_setWidget(&ff, NULL);

            /* ---- 框体几何（沿 XWidget 基类，几何事实可断言） ---- */
            XWidget_setGeometry((XWidget*)&ff, 3, 4, 80, 26);
            XAPI_EXPECT(XWidget_x((XWidget*)&ff) == 3 &&
                        XWidget_y((XWidget*)&ff) == 4,
                        "FocusFrame setGeometry 位置往返=(3,4)");
            XAPI_EXPECT(XWidget_width((XWidget*)&ff) == 80 &&
                        XWidget_height((XWidget*)&ff) == 26,
                        "FocusFrame setGeometry 尺寸往返=80x26");
            XWidget_resize((XWidget*)&ff, 90, 30);
            XAPI_EXPECT(XWidget_width((XWidget*)&ff) == 90 &&
                        XWidget_height((XWidget*)&ff) == 30,
                        "FocusFrame resize 往返=90x30");
            {
                XPoint mv;
                XPoint_init(&mv, 7, 8);
                XWidget_movePoint((XWidget*)&ff, &mv);
            }
            XAPI_EXPECT(XWidget_x((XWidget*)&ff) == 7 &&
                        XWidget_y((XWidget*)&ff) == 8,
                        "FocusFrame move 位置往返=(7,8)");
            XWidget_setVisible((XWidget*)&ff, false);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)&ff),
                        "FocusFrame setVisible(false) 显式关闭");
            XWidget_setVisible((XWidget*)&ff, true);
            /* 生效可见性受父链牵制（套件 root 未 show，isVisible 恒
             * false），断言显式口径 isVisibleTo(父)（见 XWidget 显隐
             * 口径，同 core 族）。 */
            XAPI_EXPECT(XWidget_isVisibleTo((XWidget*)&ff, root) &&
                        !XWidget_isHidden((XWidget*)&ff),
                        "FocusFrame setVisible(true) 显式开启（isVisibleTo）");
            XFocusFrame_deinit_base(&ff);

            /* ---- 堆创建（含无父创建的未 show 边界） ---- */
            ffh = XFocusFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
            XAPI_EXPECT(ffh != NULL && XFocusFrame_widget(ffh) == NULL,
                        "FocusFrame 无父堆创建成功且未关联");
            XFocusFrame_setWidget(ffh, w1);
            XAPI_EXPECT(XFocusFrame_widget(ffh) == w1,
                        "FocusFrame 堆对象 setWidget 往返");
            XWidget_setGeometry((XWidget*)ffh, 1, 2, 40, 20);
            XAPI_EXPECT(XWidget_rect((XWidget*)ffh).width == 40 &&
                        XWidget_rect((XWidget*)ffh).height == 20,
                        "FocusFrame rect 几何与 setter 一致");
            XFocusFrame_delete_base(ffh);
        }
#endif /* XWIDGET_ON && XFOCUSFRAME_ON */

        /* ============================================================
         * 12. XSizeGrip：尺寸手柄（对标 QSizeGrip）。
         *     交互拖拽需真实输入路径，无头仅断言存在性与几何状态。
         * ============================================================ */
#if XWIDGET_ON && XSIZEGRIP_ON
        {
            XSizeGrip sg;
            XSizeGrip* sgh;
            XSizeGrip* sgh2;

            XAPI_EXPECT(XSizeGrip_class_init() != NULL,
                        "SizeGrip class_init 虚表非 NULL");
            XSizeGrip_init(&sg, root);
            XAPI_EXPECT(XWidget_width((XWidget*)&sg) == 16 &&
                        XWidget_height((XWidget*)&sg) == 16,
                        "SizeGrip 默认尺寸=16x16（实现口径，Qt 无固定公开尺寸）");
            XWidget_resize((XWidget*)&sg, 24, 24);
            XAPI_EXPECT(XWidget_width((XWidget*)&sg) == 24 &&
                        XWidget_height((XWidget*)&sg) == 24,
                        "SizeGrip resize 往返=24x24（QWidget 几何口径）");
            XWidget_setGeometry((XWidget*)&sg, 10, 10, 20, 20);
            XAPI_EXPECT(XWidget_x((XWidget*)&sg) == 10 &&
                        XWidget_y((XWidget*)&sg) == 10 &&
                        XWidget_width((XWidget*)&sg) == 20,
                        "SizeGrip setGeometry 位置与宽往返");
            XAPI_EXPECT(XWidget_height((XWidget*)&sg) == 20,
                        "SizeGrip setGeometry 高往返=20");
            {
                XPoint mv;
                XPoint_init(&mv, 30, 40);
                XWidget_movePoint((XWidget*)&sg, &mv);
            }
            XAPI_EXPECT(XWidget_x((XWidget*)&sg) == 30 &&
                        XWidget_y((XWidget*)&sg) == 40,
                        "SizeGrip move 位置往返=(30,40)");
            XAPI_EXPECT(XWidget_rect((XWidget*)&sg).width == 20,
                        "SizeGrip rect 与 setter 一致（QWidget 几何）");
            /* rect 为客户区局部矩形（XWidget.h：0,0,w,h，对标
             * QWidget::rect），不含位置——位置经 x/y/pos 查询。 */
            XAPI_EXPECT(XWidget_rect((XWidget*)&sg).x == 0 &&
                        XWidget_rect((XWidget*)&sg).y == 0,
                        "SizeGrip rect 局部原点=0,0（Qt rect 口径，位置不含其中）");
            XWidget_setVisible((XWidget*)&sg, false);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)&sg),
                        "SizeGrip setVisible(false) 显式关闭");
            XWidget_setVisible((XWidget*)&sg, true);
            /* 生效可见性受父链牵制（root 未 show），断言显式口径
             * isVisibleTo(父)（见 XWidget 显隐口径，同 core 族）。 */
            XAPI_EXPECT(XWidget_isVisibleTo((XWidget*)&sg, root) &&
                        !XWidget_isHidden((XWidget*)&sg),
                        "SizeGrip setVisible(true) 显式开启（isVisibleTo）");
            XWidget_setEnabled((XWidget*)&sg, false);
            XAPI_EXPECT(!XWidget_isEnabled((XWidget*)&sg),
                        "SizeGrip setEnabled(false) 状态关闭");
            XWidget_setEnabled((XWidget*)&sg, true);
            XAPI_EXPECT(XWidget_isEnabled((XWidget*)&sg),
                        "SizeGrip setEnabled(true) 状态开启");
            XSizeGrip_deinit_base(&sg);

            sgh = XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root);
            XAPI_EXPECT(sgh != NULL && XWidget_parentWidget((XWidget*)sgh) ==
                                           root,
                        "SizeGrip 堆创建并挂到父控件");
            XAPI_EXPECT(XWidget_width((XWidget*)sgh) == 16 &&
                        XWidget_height((XWidget*)sgh) == 16,
                        "SizeGrip 堆创建默认尺寸=16x16（同 init 口径）");
            sgh2 = XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL);
            XAPI_EXPECT(sgh2 != NULL,
                        "SizeGrip 无父堆创建成功（未 show 边界）");
            XSizeGrip_delete_base(sgh);
            XSizeGrip_delete_base(sgh2);
        }
#endif /* XWIDGET_ON && XSIZEGRIP_ON */

        /* ============================================================
         * 13. XRubberBand：橡皮筋（对标 QRubberBand）。
         * ============================================================ */
#if XWIDGET_ON && XRUBBERBAND_ON
        {
            XRubberBand rb;
            XRubberBand* rbh;
            XRubberBand* rbh2;

            /* ---- 形状枚举数值对齐（QRubberBand::Shape 数值一致） ---- */
            XAPI_EXPECT((int)XRubberBandShape_Line == 0 &&
                        (int)XRubberBandShape_Rectangle == 1,
                        "RubberBand 形状枚举数值=Qt（Line=0/Rectangle=1）");
            XRubberBand_init(&rb, XRubberBandShape_Line, root);
            XAPI_EXPECT(XRubberBand_shape(&rb) == XRubberBandShape_Line,
                        "RubberBand init(Line) 后 shape=Line");
            XRubberBand_deinit_base(&rb);
            XRubberBand_init(&rb, XRubberBandShape_Rectangle, root);
            XAPI_EXPECT(XRubberBand_shape(&rb) ==
                            XRubberBandShape_Rectangle,
                        "RubberBand init(Rectangle) 后 shape=Rectangle");

            /* ---- 几何（QRubberBand::setGeometry/move/resize 沿基类） ---- */
            XWidget_setGeometry((XWidget*)&rb, 5, 6, 100, 50);
            XAPI_EXPECT(XWidget_x((XWidget*)&rb) == 5 &&
                        XWidget_y((XWidget*)&rb) == 6,
                        "RubberBand setGeometry 位置往返=(5,6)");
            XAPI_EXPECT(XWidget_width((XWidget*)&rb) == 100 &&
                        XWidget_height((XWidget*)&rb) == 50,
                        "RubberBand setGeometry 尺寸往返=100x50");
            XWidget_resize((XWidget*)&rb, 120, 60);
            XAPI_EXPECT(XWidget_width((XWidget*)&rb) == 120 &&
                        XWidget_height((XWidget*)&rb) == 60,
                        "RubberBand resize 往返=120x60");
            {
                XPoint mv;
                XPoint_init(&mv, 9, 11);
                XWidget_movePoint((XWidget*)&rb, &mv);
            }
            XAPI_EXPECT(XWidget_x((XWidget*)&rb) == 9 &&
                        XWidget_y((XWidget*)&rb) == 11,
                        "RubberBand move 位置往返=(9,11)");
            XWidget_setGeometry((XWidget*)&rb, 0, 0, 30, 40);
            XAPI_EXPECT(XWidget_x((XWidget*)&rb) == 0 &&
                        XWidget_width((XWidget*)&rb) == 30,
                        "RubberBand 重复 setGeometry 覆盖旧几何");
            XWidget_setVisible((XWidget*)&rb, false);
            XAPI_EXPECT(XWidget_isHidden((XWidget*)&rb),
                        "RubberBand setVisible(false) 显式关闭");
            XWidget_setVisible((XWidget*)&rb, true);
            /* 生效可见性受父链牵制（root 未 show），断言显式口径
             * isVisibleTo(父)（见 XWidget 显隐口径，同 core 族）。 */
            XAPI_EXPECT(XWidget_isVisibleTo((XWidget*)&rb, root) &&
                        !XWidget_isHidden((XWidget*)&rb),
                        "RubberBand setVisible(true) 显式开启（isVisibleTo）");
            XRubberBand_deinit_base(&rb);

            /* ---- 堆创建两种形状（含无父创建边界） ---- */
            rbh = XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        XRubberBandShape_Line, root);
            XAPI_EXPECT(rbh != NULL &&
                        XRubberBand_shape(rbh) == XRubberBandShape_Line,
                        "RubberBand 堆创建 Line 形状");
            XWidget_setGeometry((XWidget*)rbh, 2, 3, 44, 55);
            XAPI_EXPECT(XWidget_rect((XWidget*)rbh).height == 55 &&
                        XWidget_rect((XWidget*)rbh).width == 44,
                        "RubberBand 堆对象几何往返=44x55");
            XAPI_EXPECT(XWidget_parentWidget((XWidget*)rbh) == root,
                        "RubberBand 堆对象挂到传入父控件（QRubberBand(parent)）");
            rbh2 = XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XRubberBandShape_Rectangle, NULL);
            XAPI_EXPECT(rbh2 != NULL &&
                        XRubberBand_shape(rbh2) ==
                            XRubberBandShape_Rectangle,
                        "RubberBand 无父堆创建 Rectangle 形状");
            XAPI_EXPECT(XWidget_parentWidget((XWidget*)rbh2) == NULL,
                        "RubberBand 无父创建时父级=NULL（顶层语义）");
            XRubberBand_delete_base(rbh);
            XRubberBand_delete_base(rbh2);
        }
#endif /* XWIDGET_ON && XRUBBERBAND_ON */

        /* 全部小节完成：根控件级联析构（子控件/动作/快捷键注销）。 */
        XWidget_delete_base(root);
    }
#endif /* XWIDGET_ON */

    return failures;
}
