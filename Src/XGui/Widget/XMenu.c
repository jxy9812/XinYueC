/**
 * @file       XMenu.c
 * @brief      XMenu 弹出菜单实现（对标 Qt 6.8 QMenu）。
 * @details    对齐 QMenu 的动作容器、属性、弹出与信号语义：
 *             - 动作容器：addAction/addMenu/addSeparator/clear/isEmpty、
 *               actions()/actionAt()/menuAction()；
 *             - 属性：title、defaultAction、activeAction、separators-
 *               Collapsible、toolTipsVisible、tearOffEnabled；
 *             - 弹出：popup() 以顶层窗口显示并激活，点击条目触发动作后
 *               关闭；exec() 阻塞事件循环直至关闭并返回被选动作；
 *             - 信号：aboutToShow/aboutToHide/triggered(XAction*)/
 *               hovered(XAction*)。
 *             绘制使用固定调色（无 XPalette 主题依赖），键盘支持
 *             Up/Down/Left/Right/Enter/Escape；子菜单条目支持悬停延时
 *             展开/点击展开/Right 展开/Left 收起。本实现只使用 XinYueC
 *             的 XWidget/XAction/XString/XPainter 抽象层，不依赖任何
 *             平台 API。
 */
#include "XMenu.h"

#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XWidget_Protected.h"
#include "XWindow.h"
#include "XGuiApplication.h"
#include "XMemory.h"
#include "XVector.h"
#include "XPainter.h"
#include "XFont.h"
#include "XCoreApplication.h"
#include "XVarList.h"
#include "XObject.h"
#include "XVariant.h"


#if XWIDGET_ON && XMENU_ON

/* ==================== 信号发射辅助 ==================== */

static void xmenu_emitVoid(XMenu* self, size_t signal)
{
    XVarList* args = XVarList_create(0);

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xmenu_emitAction(XMenu* self, size_t signal, XAction* action)
{
    XVarList* args = XVarList_Create(XVar(XAction*, action));

    if (!args)
        return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 动作管理 ==================== */

/* 动作 triggered 转发槽：动作被触发时转发菜单 triggered(action) 信号。
 * 根因修复（复扫 R-18）：子菜单容器动作（addMenu 产物）被"触发"在对标
 * Qt 里是打开子菜单而非激活条目，QMenu::triggered 从不为它发射；此前
 * addActionInternal/insertActionInternal 对容器动作照连 triggered 转发，
 * 导致打开子菜单路径误发菜单 triggered(容器动作)。此处按发射时点鉴别
 * 容器动作并抑制转发，同时覆盖 addMenu/insertMenu 两条挂接路径。 */
static void xmenu_actionTriggeredSlot(XObject* receiver, XVarList* args)
{
    XMenu* self = (XMenu*)receiver;
    XObject* sender = XObject_sender(receiver);

    (void)args;
    if (self && sender && !XAction_menu((XAction*)sender))
        xmenu_emitAction(self, (size_t)XMenu_triggered_signal,
                         (XAction*)sender);
}

/* 动作销毁槽：动作被外部释放时从菜单列表移除，避免悬挂指针。 */
static void xmenu_actionDestroyedSlot(XObject* receiver, XVarList* args)
{
    XMenu* self = (XMenu*)receiver;
    XObject* sender = XObject_sender(receiver);
    int64_t count;
    int64_t index;

    (void)args;
    if (!self || !sender || !self->m_actions)
        return;
    count = (int64_t)XVector_size_base((const XContainer*)self->m_actions);
    for (index = 0; index < count; ++index) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, index);
        if (item && *item == (XAction*)sender) {
            XVector_remove_base((XContainer*)self->m_actions, index, 1);
            break;
        }
    }
}

static XAction* xmenu_addActionInternal(XMenu* self, XAction* action)
{
    if (!self || !action || !self->m_actions)
        return NULL;

    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
    return action;
}

/* ==================== 动作容器（对标 QMenu） ==================== */

XAction* XMenu_addAction(XMenu* self, const XString* text)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return NULL;
    if (text)
        XAction_setText(action, text);
    return xmenu_addActionInternal(self, action);
}

XAction* XMenu_addAction_2(XMenu* self, const char* utf8)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, utf8);
    if (!action)
        return NULL;
    return xmenu_addActionInternal(self, action);
}

XAction* XMenu_addSeparator(XMenu* self)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return NULL;
    XAction_setSeparator(action, true);
    return xmenu_addActionInternal(self, action);
}

bool XMenu_addMenu(XMenu* self, XMenu* menu)
{
    XAction* action;
    XString* title;

    if (!self)
        return false;
    if (!menu)
        return XMenu_addSeparator(self) != NULL;
    if (menu->m_parentMenu == self)
        return true;

    /* 子菜单登记为本菜单的子控件，父菜单释放时级联释放。 */
    XWidget_setParent((XWidget*)menu, (XWidget*)self, 0);
    menu->m_parentMenu = self;

    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action)
        return false;
    title = XMenu_title(menu);
    if (title) {
        XAction_setText(action, title);
        XString_delete_base((XClass*)title);
    }
    XAction_setMenu(action, menu);
    return xmenu_addActionInternal(self, action) != NULL;
}

XMenu* XMenu_addMenu_2(XMenu* self, const char* utf8Title)
{
    XMenu* child;

    if (!self)
        return NULL;
    child = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self,
                            utf8Title);
    if (!child)
        return NULL;
    if (!XMenu_addMenu(self, child)) {
        XMenu_delete_base(child);
        return NULL;
    }
    return child;
}

void XMenu_clear(XMenu* self)
{
    XAction* action;

    if (!self || !self->m_actions)
        return;
    while (XVector_size_base((XContainer*)self->m_actions) > 0) {
        action = *(XAction**)XVector_at_base((XContainer*)self->m_actions,
                                             0);
        if (action)
            XAction_delete_base(action);
        /* 销毁回调会把元素移出列表；未连接时下面显式清空兜底。 */
        if (XVector_size_base((XContainer*)self->m_actions) > 0)
            XVector_remove_base((XContainer*)self->m_actions, 0, 1);
    }
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XMenu_isEmpty(const XMenu* self)
{
    if (!self || !self->m_actions)
        return true;
    return XVector_size_base((const XContainer*)self->m_actions) == 0;
}

const XVector* XMenu_actions(const XMenu* self)
{
    return self ? self->m_actions : NULL;
}

XAction* XMenu_actionAt(const XMenu* self, const XPoint* pos)
{
    XRect rect;
    int index;
    int64_t count;

    if (!self || !pos)
        return NULL;
    rect = XWidget_rect((XWidget*)self);
    /* X 必须落在菜单宽度内：此前只查 Y——点击菜单右侧/左侧之外的
     * 任意位置（y 恰在条目行内）都会被判为"点在条目上"，弹出菜单
     * 无法通过点击外部关闭（14.124 补充五）。 */
    if (pos->x < rect.x || pos->x >= rect.x + rect.width)
        return NULL;
    if (pos->y < rect.y)
        return NULL;
    index = (pos->y - rect.y) /
            (self->m_actionHeight > 0 ? self->m_actionHeight : 1);
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                    (const XContainer*)self->m_actions)
                : 0;
    if (index < 0 || index >= count)
        return NULL;
    return *(XAction**)XVector_at_base((XContainer*)self->m_actions, index);
}

XAction* XMenu_menuAction(XMenu* self)
{
    XString* title;

    if (!self)
        return NULL;
    if (!self->m_menuAction) {
        self->m_menuAction =
            XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
        if (!self->m_menuAction)
            return NULL;
        title = XMenu_title(self);
        if (title) {
            XAction_setText(self->m_menuAction, title);
            XString_delete_base((XClass*)title);
        }
    }
    return self->m_menuAction;
}

XMenu* XMenu_menuInAction(const XAction* action)
{
    /*
     * 对标 QMenu::menuInAction：qobject_cast<QMenu*>(action->menuObject())。
     * 本项目 XAction::m_menu 只承载 XMenu*，无第二对象类型，省去
     * qobject_cast 鉴别，直接返回登记的菜单（未关联返回 NULL）。
     */
    return XAction_menu(action);
}

/* ==================== 属性（对标 QMenu） ==================== */

XString* XMenu_title(const XMenu* self)
{
    if (!self || !self->m_title)
        return NULL;
    return XString_create_copy(self->m_title);
}

const XString* XMenu_title_const(const XMenu* self)
{
    return self ? self->m_title : NULL;
}

void XMenu_setTitle(XMenu* self, const XString* title)
{
    XString* copy;

    if (!self)
        return;
    if (self->m_title && title &&
        XString_equals(self->m_title, title, XChar_CaseSensitive)) {
        return;
    }
    copy = title ? XString_create_copy(title) : XString_create();
    if (!copy)
        return;
    if (self->m_title)
        XString_delete_base((XClass*)self->m_title);
    self->m_title = copy;
    if (self->m_menuAction)
        XAction_setText(self->m_menuAction, self->m_title);
    XWidget_update((XWidget*)self);
}

void XMenu_setIcon(XMenu* self, const XString* icon)
{
    XString* copy;

    if (!self)
        return;
    if (!icon) {
        if (self->m_icon) {
            XString_delete_base((XClass*)self->m_icon);
            self->m_icon = NULL;
        }
        return;
    }
    copy = XString_create_copy(icon);
    if (!copy)
        return;
    if (self->m_icon)
        XString_delete_base((XClass*)self->m_icon);
    self->m_icon = copy;
}

void XMenu_setIcon_2(XMenu* self, const char* utf8)
{
    XString tmp;

    if (!self)
        return;
    if (!utf8) {
        XMenu_setIcon(self, NULL);
        return;
    }
    XString_init(&tmp);
    XString_assign_utf8(&tmp, utf8);
    XMenu_setIcon(self, &tmp);
    XString_deinit_base(&tmp);
}

const XString* XMenu_icon(const XMenu* self)
{
    return self ? self->m_icon : NULL;
}

void XMenu_setTitle_2(XMenu* self, const char* utf8)
{
    XString* text;

    if (!self)
        return;
    text = XString_create_utf8(utf8 ? utf8 : "");
    if (!text)
        return;
    XMenu_setTitle(self, text);
    XString_delete_base((XClass*)text);
}

XAction* XMenu_defaultAction(const XMenu* self)
{
    return self ? self->m_defaultAction : NULL;
}

void XMenu_setDefaultAction(XMenu* self, XAction* action)
{
    if (self)
        self->m_defaultAction = action;
}

XAction* XMenu_activeAction(const XMenu* self)
{
    return self ? self->m_activeAction : NULL;
}

void XMenu_setActiveAction(XMenu* self, XAction* action)
{
    if (!self || self->m_activeAction == action)
        return;
    self->m_activeAction = action;
    XWidget_update((XWidget*)self);
}

bool XMenu_separatorsCollapsible(const XMenu* self)
{
    return self ? self->m_separatorsCollapsible : false;
}

void XMenu_setSeparatorsCollapsible(XMenu* self, bool collapse)
{
    if (!self || self->m_separatorsCollapsible == collapse)
        return;
    self->m_separatorsCollapsible = collapse;
    XWidget_update((XWidget*)self);
}

bool XMenu_toolTipsVisible(const XMenu* self)
{
    return self ? self->m_toolTipsVisible : false;
}

void XMenu_setToolTipsVisible(XMenu* self, bool visible)
{
    if (self)
        self->m_toolTipsVisible = visible;
}

bool XMenu_tearOffEnabled(const XMenu* self)
{
    return self ? self->m_tearOffEnabled : false;
}

void XMenu_setTearOffEnabled(XMenu* self, bool enable)
{
    if (self)
        self->m_tearOffEnabled = enable;
}

/* ==================== 弹出（对标 QMenu） ==================== */

/* ---- 子菜单弹出状态（复扫 R-18） ----
 * XMenu.h 归主线头文件批次、不在本批改动范围：悬停延时弹出定时器与
 * 当前展开子菜单的记账经对象动态属性（XObject_setProperty，XVariant
 * 随对象析构自动释放）承载，对标 Qt QMenuPrivate 的 popupDelay 定时器
 * 与 sloppy/active 状态，不引入跨对象静态量。 */

/** @brief 动态属性键：未决的悬停弹出定时器 id（int64 变体承载 XTimerId）。 */
#define XMENU_PROP_HOVER_TIMER "xgui.menu.hoverTimer"
/** @brief 动态属性键：当前展开的子菜单（Ptr 变体承载 XMenu*，借用）。 */
#define XMENU_PROP_OPEN_SUB "xgui.menu.openSub"

/** @brief 悬停子菜单条目后的延时弹出间隔（对标 Qt
 *         SH_Menu_SubMenuPopupDelay 缺省 300ms）。 */
#define XMENU_SUBMENU_POPUP_DELAY_MS 300

static void xmenu_removeProp(XMenu* self, const char* keyUtf8)
{
    XString key;

    if (!self || !keyUtf8)
        return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    XObject_removeProperty((XObject*)self, &key);
    XString_deinit_base(&key);
}

static void xmenu_setInt64Prop(XMenu* self, const char* keyUtf8,
                               int64_t value)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return;
    v = XVariant_create_int64(value);
    if (!v)
        return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    /* setProperty 成功后变体所有权转移给对象；失败则自回滚防泄漏。 */
    if (!XObject_setProperty((XObject*)self, &key, v))
        XVariant_delete_base((XClass*)v);
    XString_deinit_base(&key);
}

static int64_t xmenu_int64Prop(const XMenu* self, const char* keyUtf8,
                               int64_t fallback)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return fallback;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    v = XObject_property((const XObject*)self, &key);
    XString_deinit_base(&key);
    return v ? XVariant_toInt64(v) : fallback;
}

static void xmenu_setPtrProp(XMenu* self, const char* keyUtf8, void* value)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return;
    v = XVariant_create_ptr(value);
    if (!v)
        return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    if (!XObject_setProperty((XObject*)self, &key, v))
        XVariant_delete_base((XClass*)v);
    XString_deinit_base(&key);
}

static void* xmenu_ptrProp(const XMenu* self, const char* keyUtf8)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return NULL;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    v = XObject_property((const XObject*)self, &key);
    XString_deinit_base(&key);
    return v ? XVariant_toPtr(v) : NULL;
}

/** @brief 取消未决的悬停弹出定时器（无未决时为幂等）。 */
static void xmenu_cancelHoverPopup(XMenu* self)
{
    int64_t id;

    if (!self)
        return;
    id = xmenu_int64Prop(self, XMENU_PROP_HOVER_TIMER,
                         (int64_t)XTIMER_INVALID_ID);
    if (id != (int64_t)XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, (XTimerId)id);
    xmenu_removeProp(self, XMENU_PROP_HOVER_TIMER);
}

/** @brief 为子菜单条目安排延时弹出（悬停展开，对标 QMenu 悬停延时）。 */
static void xmenu_scheduleHoverPopup(XMenu* self, XAction* action)
{
    int64_t id;

    if (!self || !action || !XAction_menu(action))
        return;
    if (XAction_menu(action)->m_popupActive)
        return;
    xmenu_cancelHoverPopup(self);
    id = (int64_t)XObject_startTimer_ms(
        (XObject*)self, XMENU_SUBMENU_POPUP_DELAY_MS,
        XTimerType_PreciseTimer);
    if (id == (int64_t)XTIMER_INVALID_ID)
        return;
    xmenu_setInt64Prop(self, XMENU_PROP_HOVER_TIMER, id);
}

/** @brief 立即展开 action 承载的子菜单：弹出位置对标 QMenu（条目右缘
 *         外侧、顶对齐），并登记为父菜单当前展开子菜单（悬停切换/
 *         再次点击可收起、父级关闭时级联收起）。幂等：已展开不重复弹。 */
static void xmenu_popupSubmenu(XMenu* self, XAction* action)
{
    XMenu* sub;
    XRect geo;
    XPoint local;
    XPoint global;

    if (!self || !action)
        return;
    sub = XAction_menu(action);
    if (!sub || sub->m_popupActive)
        return;
    geo = XMenu_actionGeometry(self, action);
    local.x = geo.x + geo.width;
    local.y = geo.y;
    global = XWidget_mapToGlobal((XWidget*)self, &local);
    xmenu_setPtrProp(self, XMENU_PROP_OPEN_SUB, sub);
    XMenu_popup(sub, &global);
}

static void xmenu_close(XMenu* self)
{
    if (!self || !self->m_popupActive)
        return;
    self->m_popupActive = false;
    /* 对标 QMenu：父菜单关闭时级联收起其展开的子菜单（子菜单是独立
     * 顶层弹窗，父级 hide 不会自动带隐）；先摘记账再收起，防自删后
     * 悬垂（DeleteOnClose）。 */
    {
        XMenu* openSub = xmenu_ptrProp(self, XMENU_PROP_OPEN_SUB);
        xmenu_removeProp(self, XMENU_PROP_OPEN_SUB);
        xmenu_cancelHoverPopup(self);
        if (openSub && openSub != self && openSub->m_popupActive)
            xmenu_close(openSub);
    }
    if (self->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_grabTimer);
        self->m_grabTimer = XTIMER_INVALID_ID;
    }
    /* 释放模态鼠标抓取：公共层直投 + 平台 XUngrabPointer。
       键盘抓取同步解除（对标 Qt closePopup→ungrabMouseForPopup/
       ungrabKeyboardForPopup 成对解抓，qapplication.cpp:3362-3365）；
       漏解会使菜单收起后全局按键仍被劫持。 */
    XWidget_releaseMouse((XWidget*)self);
    XWidget_releaseKeyboard((XWidget*)self);
    {
        XWindow* handle = XWidget_windowHandle((XWidget*)self);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, false);
            XWindow_setKeyboardGrabEnabled(handle, false);
        }
    }
    /* 嵌套弹层交接（对标 QApplicationPrivate::closePopup 的
       qapplication.cpp:3379-3386「A popup was closed, so the previous
       popup gets the focus … grabForPopup(popupWin->widget())」）：
       子菜单关闭后由仍存活的父菜单重新抓取鼠标+键盘，否则父菜单失去
       平台抓取，Esc/菜单外点击关闭随之失效。 */
    if (self->m_parentMenu && self->m_parentMenu->m_popupActive) {
        XWindow* parentHandle =
            XWidget_windowHandle((XWidget*)self->m_parentMenu);
        if (parentHandle) {
            XWindow_setMouseGrabEnabled(parentHandle, true);
            XWindow_setKeyboardGrabEnabled(parentHandle, true);
        }
    }
    /* 本菜单若为子菜单：清父菜单的展开记账，防父菜单悬挂本对象。 */
    if (self->m_parentMenu &&
        xmenu_ptrProp(self->m_parentMenu, XMENU_PROP_OPEN_SUB) == self)
        xmenu_removeProp(self->m_parentMenu, XMENU_PROP_OPEN_SUB);
    xmenu_emitVoid(self, (size_t)XMenu_aboutToHide_signal);
    XWidget_hide((XWidget*)self);
    /* 焦点回交（对标 Qt closePopup 的焦点还原，qapplication.cpp:
       3368-3377）：弹出时 activateWindow 曾把应用焦点窗口指向菜单
       窗口；收起后若焦点仍滞留菜单窗口，按键会投递给已隐藏窗口，
       宿主顶层窗口重新激活以恢复按键链。 */
    {
        XWidget* host = XWidget_topLevelWidget((XWidget*)self);
        if (host && host != (XWidget*)self && host->m_isWindow &&
            XGuiApplication_focusWindow() ==
                (XWindow*)XWidget_windowHandle((XWidget*)self))
            XWidget_activateWindow(host);
    }
    /* 对标 Qt WA_DeleteOnClose：设置该属性的弹出菜单在关闭时自删。
       仅在交互关闭路径（动作触发/菜单外点击/Escape）执行；对象析构
       路径不经过本函数，无二次删除风险。 */
    if (XWidget_testAttribute((XWidget*)self,
                              XWidgetAttribute_DeleteOnClose))
        XWidget_delete_base((XClass*)self);
}

/* 移动高亮到下一个/上一个可选条目；wrap 循环。 */
static void xmenu_moveActive(XMenu* self, int direction)
{
    int64_t count;
    int64_t index;
    int64_t next;
    int steps;

    if (!self || !self->m_actions)
        return;
    count = (int64_t)XVector_size_base(
        (const XContainer*)self->m_actions);
    if (count <= 0)
        return;
    index = -1;
    if (self->m_activeAction) {
        for (index = 0; index < count; ++index) {
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, index);
            if (item && *item == self->m_activeAction)
                break;
        }
        if (index >= count)
            index = -1;
    }
    for (steps = 0; steps < (int)count; ++steps) {
        XAction** item;

        next = index + direction;
        if (next < 0)
            next = count - 1;
        else if (next >= count)
            next = 0;
        index = next;
        item = (XAction**)XVector_at_base((XContainer*)self->m_actions,
                                          index);
        if (item && *item && XAction_isEnabled(*item) &&
            !XAction_isSeparator(*item)) {
            XMenu_setActiveAction(self, *item);
            xmenu_emitAction(self, (size_t)XMenu_hovered_signal, *item);
            return;
        }
    }
}

void XMenu_popup(XMenu* self, const XPoint* pos)
{
    XSize hint;
    XRect rect;
    XPoint p;

    if (!self || self->m_popupActive)
        return;
    hint = XMenu_sizeHint(self);
    p.x = pos ? pos->x : 0;
    p.y = pos ? pos->y : 0;
    XRect_init(&rect, p.x, p.y,
               hint.width > 0 ? hint.width : 120,
               hint.height > 0 ? hint.height : 20);
    xmenu_emitVoid(self, (size_t)XMenu_aboutToShow_signal);
    XWidget_setGeometryRect((XWidget*)self, &rect);
    XWidget_show((XWidget*)self);
    /* 对标 Qt QWidgetPrivate::show_helper 的 Popup 分支（qwidget.cpp:
     * 8038-8043「new popups and tools need to be raised」）：弹层每次
     * show 都必须置顶。XWidget_raise→XWindow_raise 当前为平台无关
     * no-op（XWindow.c:2019 无平台 Z 序接口），而 X11 规范 MapWindow
     * 不改堆叠序（x11protocol.txt MapWindow 节）——菜单 X11 窗口若早
     * 于主窗口建立即永居其下，表现为「已映射、缓冲有内容、屏幕不可
     * 见」。平台唯一置顶通道是 requestActivate→XRaiseWindow
     * （XPlatformNativeWindow_posix.c:5144）；未映射窗口的激活在平台
     * 层挂起（m_deferredActivation，MapNotify 后补做），旧注释担忧的
     * BadMatch 已由该挂起机制消除。 */
    XWidget_activateWindow((XWidget*)self);
    XWidget_raise((XWidget*)self);
    /* 立即建立后备存储并完成首帧绘制上屏：菜单作为独立顶层窗口没有
     * 宿主帧泵，若不主动 flush，backingStore 一直为空、paintDevice 返回
     * NULL，弹出后内容空白。 */
    XWidget_flushBackingStore((XWidget*)self, NULL);
    /* 模态鼠标抓取（对标 QMenu 弹窗）：公共层立即设置直投目标；平台层
     * XGrabPointer 需要窗口已完成映射，因此延迟到 1ms 定时器（事件循环
     * 处理时窗口已映射）再执行，避免对未映射窗口抓取失败。
     * 键盘抓取同步建立（对标 Qt openPopup→grabForPopup 鼠标+键盘成对
     * 抓取，qapplication.cpp:3327-3396）——Esc/方向键直达菜单，修复
     * 「Esc 不关闭、点击外部不关闭」。 */
    XWidget_grabMouse((XWidget*)self);
    XWidget_grabKeyboard((XWidget*)self);
    if (self->m_grabTimer == XTIMER_INVALID_ID) {
        self->m_grabTimer = XObject_startTimer_ms(
            (XObject*)self, 1u, XTimerType_PreciseTimer);
    }
    self->m_popupActive = true;
}

XAction* XMenu_exec(XMenu* self)
{
    if (!self)
        return NULL;
    self->m_execResult = NULL;
    XMenu_popup(self, NULL);
    while (self->m_popupActive)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    return self->m_execResult;
}

/* ==================== 尺寸（对标 QMenu::sizeHint） ==================== */

XSize XMenu_sizeHint(const XMenu* self)
{
    XSize out;
    XFont font;
    int64_t count;
    int64_t i;
    int maxWidth;

    XSize_init(&out, 0, 0);
    if (!self)
        return out;
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    font = XWidget_font((XWidget*)self);
    maxWidth = 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, i);
        if (item && *item && !XAction_isSeparator(*item)) {
            const char* text = XString_toUtf8(
                XAction_text_const(*item) ? XAction_text_const(*item)
                                          : NULL);
            int width;

            if (!text)
                text = "";
            width = XPainter_textWidth(&font, text);
            if (XAction_menu(*item))
                width += 16; /* 子菜单箭头预留。 */
            if (width > maxWidth)
                maxWidth = width;
        }
    }
    XFont_deinit_base(&font);
    out.width = maxWidth + 24;
    out.height = (int)count * (self->m_actionHeight > 0
                                   ? self->m_actionHeight
                                   : 20);
    return out;
}

/* ==================== 虚槽实现（绘制与交互） ==================== */

void XMenu_drawContents(XMenu* self, XPainter* painter)
{
    XRect rect;
    XFont font;
    int64_t count;
    int64_t i;

    if (!self || !painter)
        return;
    rect = XWidget_rect((XWidget*)self);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共接管：菜单面板 + 条目走 PE_PanelMenu/CE_MenuItem
         * （分隔线保留原实现；选中项 highlight + HighlightedText）。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption panel;
        int64_t n;
        int64_t k;
        XStyleOption_init(&panel, XStylePE_PanelMenu);
        panel.m_rect = rect;
        panel.m_state = XWidget_isEnabled((XWidget*)self)
            ? XStyleState_Enabled : 0;
#if XPALETTE_ON
        panel.m_palette = XWidget_palette((XWidget*)self);
#endif
        XPainter_fillRect(painter, &rect, 0xFFF0F0F0u);
        font = XWidget_font((XWidget*)self);
        XPainter_setFont(painter, &font);
        XFont_deinit_base(&font);
        n = self->m_actions
                ? (int64_t)XVector_size_base((const XContainer*)self->m_actions)
                : 0;
        for (k = 0; k < n; ++k) {
            XAction** it = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, k);
            XAction* act = it ? *it : NULL;
            XRect cell;
            int yy;
            if (!act) continue;
            yy = rect.y + (int)k * self->m_actionHeight;
            XRect_init(&cell, rect.x, yy, rect.width, self->m_actionHeight);
            if (XAction_isSeparator(act)) {
                XRect line;
                XRect_init(&line, rect.x + 6, yy + self->m_actionHeight / 2,
                           rect.width - 12, 1);
                XPainter_fillRect(painter, &line, 0xFFA0A0A0u);
                continue;
            }
            {
                XStyleOption mi;
                XStyleOption_init(&mi, XStyleCE_MenuItem);
                mi.m_rect = cell;
                mi.m_state = XAction_isEnabled(act)
                    ? XStyleState_Enabled : 0;
                if (act == self->m_activeAction) {
                    mi.m_state |= XStyleState_Selected;
                    mi.m_selected = true;
                }
                mi.m_text = XString_toUtf8(
                    XAction_text_const(act) ? XAction_text_const(act) : NULL);
#if XPALETTE_ON
                mi.m_palette = XWidget_palette((XWidget*)self);
#endif
                XStyle_drawControl(style, XStyleCE_MenuItem, &mi, painter,
                                   (XWidget*)self);
            }
        }
        return;
    }
#endif /* XSTYLE_ON */
    XPainter_fillRect(painter, &rect, 0xFFF0F0F0u);
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    XFont_deinit_base(&font);
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        XRect cell;
        int y;

        if (!action)
            continue;
        y = rect.y + (int)i * self->m_actionHeight;
        XRect_init(&cell, rect.x, y, rect.width, self->m_actionHeight);
        if (XAction_isSeparator(action)) {
            XRect line;
            XRect_init(&line, rect.x + 6, y + self->m_actionHeight / 2,
                       rect.width - 12, 1);
            XPainter_fillRect(painter, &line, 0xFFA0A0A0u);
            continue;
        }
        if (action == self->m_activeAction)
            XPainter_fillRect(painter, &cell, 0xFFB0C4DEu);
        {
            const char* text = XString_toUtf8(
                XAction_text_const(action) ? XAction_text_const(action)
                                           : NULL);
            int ascent;
            uint32_t color;

            if (!text)
                text = "";
            ascent = XPainter_textAscent(&font);
            color = XAction_isEnabled(action) ? 0xFF000000u : 0xFF808080u;
            XPainter_drawText(painter, rect.x + 6,
                              y + (self->m_actionHeight -
                                   XPainter_textHeight(&font)) /
                                      2 +
                                  ascent,
                              text, color);
        }
        if (XAction_menu(action)) {
            int arrowX = rect.x + rect.width - 14;
            int arrowY = y + (self->m_actionHeight / 2) - 2;
            XRect tri;
            XRect_init(&tri, arrowX + 2, arrowY, 1, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 1, arrowY + 1, 3, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX, arrowY + 2, 5, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 1, arrowY + 3, 3, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
            XRect_init(&tri, arrowX + 2, arrowY + 4, 1, 1);
            XPainter_fillRect(painter, &tri, 0xFF606060u);
        }
    }
    XFont_deinit_base(&font);
}

static void VXMenu_paintEvent(XWidget* self, XEvent* event)
{
#if !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XMenu* menu = (XMenu*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_PAINT)
        return;
    image = XWidget_paintImage(self);
    if (!image)
        return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XMenu_drawContents(menu, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XWINDOWEVENT_ON */
}

static void VXMenu_mousePressEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        return;
    }
    mouseEvent = (XMouseEvent*)event;
    /* 菜单外点击（任意按键，含左/中/右键）一律关闭并释放抓取；只有
     * 落在菜单条目上时才保留，等待 release 触发动作。 */
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (!action) {
        menu->m_execResult = NULL;
        xmenu_close(menu);
        XEvent_accept(event);
        return;
    }
    /* 子菜单条目按下即展开（复扫 R-18，对标 QMenu）：不触发容器动作、
     * 不关父菜单；对已展开的子菜单再次按下则收起（Qt 反复点击切换）。 */
    if (!XAction_isSeparator(action) && XAction_isEnabled(action) &&
        XAction_menu(action)) {
        if (action != menu->m_activeAction) {
            XMenu_setActiveAction(menu, action);
            xmenu_emitAction(menu, (size_t)XMenu_hovered_signal, action);
        }
        xmenu_cancelHoverPopup(menu);
        if (xmenu_ptrProp(menu, XMENU_PROP_OPEN_SUB) == XAction_menu(action))
            xmenu_close(XAction_menu(action));
        else
            xmenu_popupSubmenu(menu, action);
    }
    XEvent_accept(event);
}

static void VXMenu_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }
    mouseEvent = (XMouseEvent*)event;
    if (XMouseEvent_button(mouseEvent) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (!action || XAction_isSeparator(action) || !XAction_isEnabled(action)) {
        XEvent_ignore(event);
        return;
    }
    if (XAction_menu(action)) {
        /* 子菜单条目（复扫 R-18）：release 只确保子菜单展开，不触发
         * 容器动作、不关父菜单；press 路径已展开时此处幂等 no-op。 */
        xmenu_popupSubmenu(menu, action);
        XEvent_accept(event);
        return;
    }
    menu->m_execResult = action;
    /* 先触发后关闭：动作由菜单拥有，设置 DeleteOnClose 的菜单会在
       关闭路径中自删，必须保证触发时对象仍存活。 */
    XAction_trigger(action);
    xmenu_close(menu);
    XEvent_accept(event);
}

static void VXMenu_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    XMouseEvent* mouseEvent;
    XPoint pos;
    XAction* action;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    mouseEvent = (XMouseEvent*)event;
    pos = XMouseEvent_position(mouseEvent);
    action = XMenu_actionAt(menu, &pos);
    if (action && !XAction_isSeparator(action)) {
        if (action != menu->m_activeAction) {
            /* 移入新条目（复扫 R-18）：取消未决的悬停弹出并收起上一个
             * 已展开的子菜单（对标 QMenu 悬停切换条目），随后高亮 +
             * hovered。 */
            xmenu_cancelHoverPopup(menu);
            {
                XMenu* openSub = xmenu_ptrProp(menu, XMENU_PROP_OPEN_SUB);
                if (openSub && openSub != XAction_menu(action) &&
                    openSub->m_popupActive)
                    xmenu_close(openSub);
            }
            XMenu_setActiveAction(menu, action);
            xmenu_emitAction(menu, (size_t)XMenu_hovered_signal, action);
        }
        /* 子菜单条目悬停即安排延时展开——高亮未变（如键盘选中后再悬停
         * 同一条目）而子菜单未开时同样适用（对标 QMenu 悬停展开；
         * xmenu_scheduleHoverPopup 内部幂等：已展开不再安排）。 */
        if (XAction_isEnabled(action))
            xmenu_scheduleHoverPopup(menu, action);
    }
}

static void VXMenu_keyPressEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;
    int key;

    if (!menu || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;
    key = XKeyEvent_key((XKeyEvent*)event);
    if (key == XKey_Up) {
        xmenu_cancelHoverPopup(menu);
        xmenu_moveActive(menu, -1);
        XEvent_accept(event);
    } else if (key == XKey_Down) {
        xmenu_cancelHoverPopup(menu);
        xmenu_moveActive(menu, 1);
        XEvent_accept(event);
    } else if (key == XKey_Right) {
        /* Right 打开高亮条目的子菜单（复扫 R-18，对标 QMenu 键盘导航）。 */
        XAction* action = menu->m_activeAction;

        if (action && XAction_isEnabled(action) &&
            !XAction_isSeparator(action) && XAction_menu(action)) {
            xmenu_cancelHoverPopup(menu);
            xmenu_popupSubmenu(menu, action);
        }
        XEvent_accept(event);
    } else if (key == XKey_Left) {
        /* Left 收起当前子菜单回到父菜单（对标 QMenu）；顶层菜单无父级
         * 不消费，交由上层（菜单栏）处理。 */
        if (menu->m_parentMenu) {
            menu->m_execResult = NULL;
            xmenu_close(menu);
            XEvent_accept(event);
        } else {
            XEvent_ignore(event);
        }
    } else if (key == XKey_Return || key == XKey_Enter) {
        XAction* action = menu->m_activeAction;

        if (action && XAction_isEnabled(action) &&
            !XAction_isSeparator(action)) {
            if (XAction_menu(action)) {
                /* 高亮条目是子菜单入口：Enter 展开，不触发动作、不关
                 * 父菜单（复扫 R-18）。 */
                xmenu_cancelHoverPopup(menu);
                xmenu_popupSubmenu(menu, action);
            } else {
                menu->m_execResult = action;
                /* 先触发后关闭（动作由菜单拥有，DeleteOnClose 自删后
                   不得再访问 action）。 */
                XAction_trigger(action);
                xmenu_close(menu);
            }
        }
        XEvent_accept(event);
    } else if (key == XKey_Escape) {
        menu->m_execResult = NULL;
        xmenu_close(menu);
        XEvent_accept(event);
    } else {
        XEvent_ignore(event);
    }
}

static void VXMenu_leaveEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;

    if (menu && event) {
        /* 指针移出菜单：未决的悬停弹出一并取消（已展开的子菜单保留，
         * 指针可能正移入该子菜单，对标 QMenu 的 sloppy 保留语义）。 */
        xmenu_cancelHoverPopup(menu);
        XMenu_setActiveAction(menu, NULL);
        XEvent_accept(event);
    }
}

/* 外部 hide（平台关闭/失焦收起）时结束弹出状态，避免 exec 卡死。 */
static void VXMenu_hideEvent(XWidget* self, XEvent* event)
{
    XMenu* menu = (XMenu*)self;

    if (menu && event) {
        if (menu->m_popupActive)
            xmenu_close(menu);
        XEvent_accept(event);
    }
}

/* 弹出后的延迟抓取定时器：窗口映射完成后执行平台 XGrabPointer，
 * 使点击菜单外部的按键都送达本窗口（模态关闭）。 */
static void VXMenu_timerEvent(XObject* object, XTimerEvent* event)
{
    XMenu* self = (XMenu*)object;
    XTimerId id;
    XWindow* handle;

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_grabTimer) {
        id = self->m_grabTimer;
        self->m_grabTimer = XTIMER_INVALID_ID;
        XObject_killTimer((XObject*)self, id);
        handle = XWidget_windowHandle((XWidget*)self);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, true);
            /* 对标 Qt grabForPopup 双抓取：平台 XGrabKeyboard 同在此
             * 延迟点执行（需窗口完成映射）；键盘事件自此直达菜单。 */
            XWindow_setKeyboardGrabEnabled(handle, true);
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    /* 悬停延时弹出（复扫 R-18）：到期弹出当前高亮条目的子菜单；先摘
     * 定时器记账再展开（展开内部会登记 OPEN_SUB 记账，二者互不冲突）。 */
    if (self && event &&
        XTimerEvent_timerId(event) ==
            (XTimerId)xmenu_int64Prop(self, XMENU_PROP_HOVER_TIMER,
                                      (int64_t)XTIMER_INVALID_ID)) {
        XAction* action;

        xmenu_removeProp(self, XMENU_PROP_HOVER_TIMER);
        action = self->m_activeAction;
        if (action && XAction_isEnabled(action) &&
            !XAction_isSeparator(action))
            xmenu_popupSubmenu(self, action);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 生命周期 ==================== */

static void VXMenu_copy(XMenu* self, const XMenu* other)
{
    int64_t count;
    int64_t i;

    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XMenu_init(self, NULL);

    XMenu_clear(self);
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title ? XString_create_copy(other->m_title)
                                   : NULL;
    if (self->m_icon) {
        XString_delete_base((XClass*)self->m_icon);
        self->m_icon = NULL;
    }
    self->m_icon = other->m_icon ? XString_create_copy(other->m_icon)
                                 : NULL;
    self->m_separatorsCollapsible = other->m_separatorsCollapsible;
    self->m_toolTipsVisible = other->m_toolTipsVisible;
    self->m_tearOffEnabled = other->m_tearOffEnabled;
    self->m_actionHeight = other->m_actionHeight;
    count = other->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)other->m_actions)
                : 0;
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)other->m_actions, i);
        XAction* src = item ? *item : NULL;
        XAction* dst;

        if (!src)
            continue;
        if (XAction_isSeparator(src)) {
            dst = XMenu_addSeparator(self);
        } else if (XAction_menu(src)) {
            XMenu* sub = XAction_menu(src);
            XMenu* subCopy;

            subCopy = XMenu_create_copy(sub);
            if (subCopy)
                XMenu_addMenu(self, subCopy);
            continue;
        } else {
            dst = XMenu_addAction(self, XAction_text_const(src));
        }
        (void)dst;
    }
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
}

static void VXMenu_move(XMenu* self, XMenu* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XMenu_init(self, NULL);

    XMenu_clear(self);
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title;
    other->m_title = NULL;
    self->m_icon = other->m_icon;
    other->m_icon = NULL;
    self->m_actions = other->m_actions;
    other->m_actions = XVector_create(sizeof(XAction*));
    self->m_menuAction = other->m_menuAction;
    other->m_menuAction = NULL;
    self->m_parentMenu = other->m_parentMenu;
    other->m_parentMenu = NULL;
    self->m_defaultAction = other->m_defaultAction;
    other->m_defaultAction = NULL;
    self->m_activeAction = other->m_activeAction;
    other->m_activeAction = NULL;
    self->m_popupActive = false;
    other->m_popupActive = false;
    self->m_separatorsCollapsible = other->m_separatorsCollapsible;
    other->m_separatorsCollapsible = true;
    self->m_toolTipsVisible = other->m_toolTipsVisible;
    other->m_toolTipsVisible = false;
    self->m_tearOffEnabled = other->m_tearOffEnabled;
    other->m_tearOffEnabled = false;
    self->m_actionHeight = other->m_actionHeight;
    other->m_actionHeight = 20;
}

static void VXMenu_deinit(XMenu* self)
{
    if (!self)
        return;

    /* 防御：析构前撤销未决的悬停弹出定时器（动态属性随 XObject 析构
     * 自动释放，无需手工清理）。 */
    xmenu_cancelHoverPopup(self);
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
    self->m_execResult = NULL;
    if (self->m_actions) {
        while (XVector_size_base((XContainer*)self->m_actions) > 0) {
            int64_t sizeBefore =
                XVector_size_base((XContainer*)self->m_actions);
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, 0);
            if (item && *item)
                XAction_delete_base(*item);
            /* 动作析构经 destroyed 信号自摘（xmenu_actionDestroyedSlot
             * 已把自身移出向量）——尺寸已缩时不可再补 remove，否则会把
             * 下一个动作指针丢弃不删（隔个漏删，§8.0g6 ASan 复扫定位的
             * 真缺陷）；自摘未发生时（防御）手动摘除防死循环。 */
            if (XVector_size_base((XContainer*)self->m_actions) == sizeBefore)
                XVector_remove_base((XContainer*)self->m_actions, 0, 1);
        }
        XVector_delete_base(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_menuAction) {
        XAction_delete_base(self->m_menuAction);
        self->m_menuAction = NULL;
    }
    if (self->m_title) {
        XString_delete_base((XClass*)self->m_title);
        self->m_title = NULL;
    }
    if (self->m_icon) {
        XString_delete_base((XClass*)self->m_icon);
        self->m_icon = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMenu_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMenu)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXMenu_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXMenu_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXMenu_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXMenu_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXMenu_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VXMenu_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXMenu_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXMenu_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXMenu_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXMenu_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMenu_deinit);
    return XVTABLE_DEFAULT;
}

void XMenu_init(XMenu* self, XWidget* parent)
{
    XMenu_init_2(self, parent, NULL);
}

void XMenu_init_2(XMenu* self, XWidget* parent, const char* utf8Title)
{
    if (!self)
        return;
    XMemset(self, 0, sizeof(XMenu));
    /* 菜单使用 Popup 窗口类型（对标 Qt::Popup）：无边框、无标题栏、
     * 无关闭按钮，弹出时覆盖式显示；X11 平台据此设置 override-redirect。 */
    XWidget_init(&self->m_base, parent, (XWidgetFlags)XWindowType_Popup);
    XClassSetVtable(self, XMenu);
    self->m_actions = XVector_create(sizeof(XAction*));
    self->m_actionHeight = 20;
    self->m_separatorsCollapsible = true;
    self->m_grabTimer = XTIMER_INVALID_ID;
    if (utf8Title && utf8Title[0])
        XMenu_setTitle_2(self, utf8Title);
}

XMenu* XMenu_create(void)
{
    return XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
}

XMenu* XMenu_create_ex(XMemoryType memory, XWidget* parent,
                       const char* utf8Title)
{
    XMenu* self = (XMenu*)XMemory_malloc(sizeof(XMenu), memory);

    if (!self)
        return NULL;
    XMemset(self, 0, sizeof(XMenu));
    XMenu_init_2(self, parent, utf8Title);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XMenu* XMenu_create_copy(const XMenu* other)
{
    XMenu* self;

    if (!other)
        return NULL;
    self = XMenu_create();
    if (!self)
        return NULL;
    XCopy(self, other);
    return self;
}

XMenu* XMenu_create_move(XMenu* other)
{
    XMenu* self;

    if (!other)
        return NULL;
    self = XMenu_create();
    if (!self)
        return NULL;
    XMove(self, other);
    return self;
}

/* ==================== 公开信号 ==================== */

void* XMenu_aboutToShow_signal(XMenu* self)
{
    if (!self)
        return (void*)(size_t)XMenu_aboutToShow_signal;
    xmenu_emitVoid(self, (size_t)XMenu_aboutToShow_signal);
    return (void*)(size_t)XMenu_aboutToShow_signal;
}

void* XMenu_aboutToHide_signal(XMenu* self)
{
    if (!self)
        return (void*)(size_t)XMenu_aboutToHide_signal;
    xmenu_emitVoid(self, (size_t)XMenu_aboutToHide_signal);
    return (void*)(size_t)XMenu_aboutToHide_signal;
}

void* XMenu_triggered_signal(XMenu* self, XAction* action)
{
    if (!self)
        return (void*)(size_t)XMenu_triggered_signal;
    xmenu_emitAction(self, (size_t)XMenu_triggered_signal, action);
    return (void*)(size_t)XMenu_triggered_signal;
}

void* XMenu_hovered_signal(XMenu* self, XAction* action)
{
    if (!self)
        return (void*)(size_t)XMenu_hovered_signal;
    xmenu_emitAction(self, (size_t)XMenu_hovered_signal, action);
    return (void*)(size_t)XMenu_hovered_signal;
}

/* ==================== Task 2.10：节/插入/几何/平台 API ============== */

/** @brief 查找动作在菜单列表中的索引；未找到返回 -1。 */
static int xmenu_findIndex(const XMenu* self, XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions || !action) return -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions,
                                       i);
        if (item && *item == action) return (int)i;
    }
    return -1;
}

/** @brief 连接动作信号并在指定位置插入；index<0 或越界时追加。 */
static XAction* xmenu_insertActionInternal(XMenu* self, XAction* action,
                                           int index)
{
    int64_t n;
    if (!self || !action || !self->m_actions) return NULL;
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    n = XVector_size_base((const XContainer*)self->m_actions);
    if (index < 0 || index >= (int)n)
        XVector_push_back_1_base(self->m_actions, &action);
    else
        XVector_Insert(self->m_actions, index, XAction*, action);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
    return action;
}

XAction* XMenu_addSection(XMenu* self, const XString* text)
{
    XAction* action;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    if (text) XAction_setText(action, text);
    /* Qt 的节标题动作禁用且不可选中。 */
    XAction_setDisabled(action, true);
    return xmenu_insertActionInternal(self, action, -1);
}

XAction* XMenu_addSection_2(XMenu* self, const char* utf8)
{
    XAction* action;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                               utf8 ? utf8 : "");
    if (!action) return NULL;
    XAction_setDisabled(action, true);
    return xmenu_insertActionInternal(self, action, -1);
}

XAction* XMenu_insertSeparator(XMenu* self, XAction* before)
{
    XAction* action;
    int index;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    XAction_setSeparator(action, true);
    index = xmenu_findIndex(self, before);
    return xmenu_insertActionInternal(self, action, index);
}

XAction* XMenu_insertMenu(XMenu* self, XAction* before, XMenu* menu)
{
    XAction* action;
    XString* title;
    int index;
    if (!self) return NULL;
    if (!menu) return XMenu_insertSeparator(self, before);
    XWidget_setParent((XWidget*)menu, (XWidget*)self, 0);
    menu->m_parentMenu = self;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    title = XMenu_title(menu);
    if (title) {
        XAction_setText(action, title);
        XString_delete_base((XClass*)title);
    }
    XAction_setMenu(action, menu);
    index = xmenu_findIndex(self, before);
    return xmenu_insertActionInternal(self, action, index);
}

XAction* XMenu_insertSection(XMenu* self, XAction* before,
                             const XString* text)
{
    XAction* action;
    int index;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    if (text) XAction_setText(action, text);
    XAction_setDisabled(action, true);
    index = xmenu_findIndex(self, before);
    return xmenu_insertActionInternal(self, action, index);
}

XAction* XMenu_insertSection_2(XMenu* self, XAction* before,
                               const char* utf8)
{
    XAction* action;
    int index;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                               utf8 ? utf8 : "");
    if (!action) return NULL;
    XAction_setDisabled(action, true);
    index = xmenu_findIndex(self, before);
    return xmenu_insertActionInternal(self, action, index);
}

XRect XMenu_actionGeometry(const XMenu* self, XAction* action)
{
    XRect rect;
    XRect out;
    int64_t i;
    int64_t n;
    int ah;
    if (!self || !action || !self->m_actions) {
        XRect_init(&out, 0, 0, 0, 0);
        return out;
    }
    rect = XWidget_rect((XWidget*)self);
    ah = self->m_actionHeight > 0 ? self->m_actionHeight : 1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions,
                                       i);
        if (item && *item == action) {
            XRect_init(&out, rect.x, rect.y + (int)i * ah,
                       rect.width, ah);
            return out;
        }
    }
    XRect_init(&out, 0, 0, 0, 0);
    return out;
}

void XMenu_setNoReplayFor(XMenu* self, XWidget* widget)
{
    if (self) self->m_noReplayFor = widget;
}

void XMenu_setPlatformMenu(XMenu* self, void* platformMenu)
{
    if (self) self->m_platformMenu = platformMenu;
}

void* XMenu_platformMenu(const XMenu* self)
{
    return self ? self->m_platformMenu : NULL;
}

void XMenu_setAsDockMenu(XMenu* self)
{
    /* 跨平台裁剪：无 macOS Dock 概念，仅记录调用。 */
    if (self) self->m_platformMenu = (void*)(size_t)1;
}

void XMenu_showTearOffMenu(XMenu* self)
{
    if (!self) return;
    self->m_tearOffMenuVisible = true;
    XWidget_update((XWidget*)self);
}

void XMenu_hideTearOffMenu(XMenu* self)
{
    if (!self) return;
    self->m_tearOffMenuVisible = false;
    XWidget_update((XWidget*)self);
}

bool XMenu_isTearOffMenuVisible(const XMenu* self)
{
    return self ? self->m_tearOffMenuVisible : false;
}





















#endif /* XWIDGET_ON && XMENU_ON */
