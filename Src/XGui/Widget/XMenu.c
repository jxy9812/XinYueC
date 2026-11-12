/**
 * @file       XMenu.c
 * @brief      XMenu 弹出菜单实现（对标 Qt 6.8 QMenu）。
 * @details    对齐 QMenu 的动作容器、属性、弹出与信号语义：
 *             - 动作容器：addAction/addMenu/addSeparator/addSection/
 *               clear/isEmpty、actions()/actionAt()/menuAction()；
 *               addMenu/insertMenu 复用子菜单自己的 menuAction（标题联动），
 *               addSection 产物是带文本且 enabled 的分隔条；
 *             - 属性：title、defaultAction、activeAction、separators-
 *               Collapsible（缺省 true，布局期折叠开头/相邻/尾部分隔条）、
 *               toolTipsVisible、tearOffEnabled；
 *             - 弹出：popup(pos[, at]) 以顶层窗口显示并激活（at 是定位锚），
 *               点击条目触发动作后关闭；exec() 阻塞事件循环直至关闭并
 *               返回被选动作；
 *             - 信号：aboutToShow/aboutToHide/triggered(XAction*)/
 *               hovered(XAction*)。
 *             绘制使用固定调色（无 XPalette 主题依赖），键盘支持
 *             Up/Down/Left/Right/Enter/Escape；子菜单条目支持悬停延时
 *             展开/点击展开/Right 展开/Left 收起。本实现只使用 XinYueC
 *             的 XWidget/XAction/XString/XPainter 抽象层，不依赖任何
 *             平台 API。
 */
#if XGUI_ON

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
#include "XIcon.h"      /* P3-18：菜单条目图标列绘制（对标 action->icon()）。 */
#include "XPixmap.h"    /* P3-18：XIcon_pixmap → XPixmap_toImage 解码链。 */
#include "XToolTip.h"   /* P3-20：条目悬停提示（对标 QToolTip::showText）。 */


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
            /* 所有权标记与动作列表同位收缩，保持索引对齐。 */
            if (self->m_actionOwned &&
                XVector_size_base(
                    (const XContainer*)self->m_actionOwned) == count)
                XVector_remove_base((XContainer*)self->m_actionOwned,
                                    index, 1);
            XVector_remove_base((XContainer*)self->m_actions, index, 1);
            break;
        }
    }
}

/* ---- 逐项所有权标记（对标 XMenuBar 的 m_actionOwned 定式）----
 * true=addAction/addMenu 等便捷路径自建（菜单拥有，析构/clear 级联
 * 删除）；false=insertAction 注入的外部动作（仅借用，不取所有权，
 * 对标 QWidget::addAction）。标记缺失/不对齐时保守按「拥有」处理。 */
static bool xmenu_ownedAt(const XMenu* self, int64_t index)
{
    bool* flag;
    if (!self || !self->m_actionOwned || !self->m_actions) return true;
    if (XVector_size_base((const XContainer*)self->m_actionOwned) !=
        XVector_size_base((const XContainer*)self->m_actions))
        return true;
    flag = (bool*)XVector_at_base(self->m_actionOwned, index);
    return flag ? *flag : true;
}

static void xmenu_pushOwnedFlag(XMenu* self, bool owned)
{
    if (!self || !self->m_actionOwned) return;
    XVector_push_back_1_base(self->m_actionOwned, &owned);
}

static void xmenu_insertOwnedFlag(XMenu* self, int index, bool owned)
{
    int64_t n;
    if (!self || !self->m_actionOwned) return;
    n = XVector_size_base((const XContainer*)self->m_actionOwned);
    if (index < 0 || (int64_t)index >= n)
        XVector_push_back_1_base(self->m_actionOwned, &owned);
    else
        XVector_insert_1_base(self->m_actionOwned, index, &owned, 1);
}

static void xmenu_removeOwnedFlagAt(XMenu* self, int64_t index, int64_t total)
{
    if (!self || !self->m_actionOwned) return;
    if (XVector_size_base((const XContainer*)self->m_actionOwned) == total)
        XVector_remove_base((XContainer*)self->m_actionOwned, index, 1);
}

/* ==================== 条目布局（separatorsCollapsible 折叠） ==================== */

/** @brief 动作索引；非成员返回 -1（前向声明，定义见文件后段）。 */
static int xmenu_findIndex(const XMenu* self, XAction* action);

/** @brief 断开菜单对动作的两条转发/自摘连接（动作被摘出列表时调用）。
 * @details 不这么做会让「摘除后重新加入」重复建连（同一次 triggered 转发
 *          两次、destroyed 槽两次入表）；Qt 的动作转发是控件级单份监听，
 *          没有重复连接概念。 */
static void xmenu_disconnectAction(XMenu* self, XAction* action)
{
    if (!self || !action) return;
    XObject_disconnect((XObject*)action, XSignal(XAction_triggered_signal),
                         (XObject*)self, xmenu_actionTriggeredSlot);
    XObject_disconnect((XObject*)action, XSignal(XObject_destroyed_signal),
                         (XObject*)self, xmenu_actionDestroyedSlot);
}

/** @brief 纯分隔条判定（对标 qmenu.cpp:394-397 的 isPlainSeparator）。
 * @details 无文本的普通分隔条计入折叠；带文本/图标的节标题（addSection
 *          产物）在布局上按普通条目处理——Qt Fusion 的
 *          SH_Menu_SupportsSections 为 1（qfusionstyle.cpp:3550-3551），
 *          isSection 为真时 isPlainSeparator 为假，不参与折叠。
 * @param action 待判定动作；NULL 返回 false。
 * @return 纯分隔条返回 true。 */
static bool xmenu_isPlainSeparator(const XAction* action)
{
    const XString* text;
    if (!action || !XAction_isSeparator(action))
        return false;
    text = XAction_text_const(action);
    if (text && XString_length_base(text) > 0)
        return false;
    return true;
}

/** @brief 布局上界（对标 QMenuPrivate::getLastVisibleAction，
 *         qmenu.cpp:487-502）：自末项向前跳过不可见动作；开启折叠时
 *         继续跳过尾部分隔条。 */
static int64_t xmenu_lastLayoutIndex(const XMenu* self, int64_t n)
{
    int64_t i;
    if (!self || !self->m_actions)
        return -1;
    for (i = n - 1; i >= 0; --i) {
        XAction** item = (XAction**)XVector_at_base(
            (const XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        if (!action || !XAction_isVisible(action))
            continue;
        /* 尾部分隔条：Qt 的 getLastVisibleAction 对**所有**分隔条（含带文本
         * 的节标题）在 collapsible 下都跳过（qmenu.cpp:497-499）。 */
        if (self->m_separatorsCollapsible && XAction_isSeparator(action))
            continue;
        return i;
    }
    return -1;
}

/** @brief 动作的视觉行号；折叠/隐藏/尾部剔除时返回 -1。
 * @details 对标 QMenuPrivate::updateActionRects 的单遍扫描
 *          （qmenu.cpp:392-403）：
 *          - previousWasSeparator 初值 true（**去掉开头的分隔条**）；
 *          - separatorsCollapsible 开启时相邻纯分隔条只保留第一条；
 *          - 不可见动作不占行（Qt 同样给它们空 QRect）。
 * @param self 菜单对象；可为 NULL。
 * @param target 动作在 m_actions 中的索引。
 * @return 视觉行号（0 起）；不参与布局返回 -1。 */
static int xmenu_visualRow(const XMenu* self, int64_t target)
{
    int64_t i;
    int64_t n;
    int64_t last;
    int row = 0;
    bool previousWasSeparator = true;
    if (!self || !self->m_actions || target < 0)
        return -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    if (target >= n)
        return -1;
    last = xmenu_lastLayoutIndex(self, n);
    if (target > last)
        return -1;
    for (i = 0; i <= last; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        bool plain;
        if (!action)
            continue;
        plain = xmenu_isPlainSeparator(action);
        if (!XAction_isVisible(action) ||
            (self->m_separatorsCollapsible && previousWasSeparator && plain)) {
            if (i == target)
                return -1;
            continue;
        }
        previousWasSeparator = plain;
        if (i == target)
            return row;
        ++row;
    }
    return -1;
}

/** @brief 视觉行号 → 动作（与 xmenu_visualRow 同一扫描规则）。
 * @param self 菜单对象；可为 NULL。
 * @param row 视觉行号（0 起）。
 * @return 该行动作借用指针；越界返回 NULL。 */
static XAction* xmenu_actionAtRow(const XMenu* self, int row)
{
    int64_t i;
    int64_t n;
    int64_t last;
    int current = 0;
    bool previousWasSeparator = true;
    if (!self || !self->m_actions || row < 0)
        return NULL;
    n = XVector_size_base((const XContainer*)self->m_actions);
    last = xmenu_lastLayoutIndex(self, n);
    for (i = 0; i <= last; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        bool plain;
        if (!action)
            continue;
        plain = xmenu_isPlainSeparator(action);
        if (!XAction_isVisible(action) ||
            (self->m_separatorsCollapsible && previousWasSeparator && plain))
            continue;
        previousWasSeparator = plain;
        if (current == row)
            return action;
        ++current;
    }
    return NULL;
}

/** @brief 参与布局的视觉行数（用于 sizeHint 高度与命中越界判定）。 */
static int xmenu_layoutRowCount(const XMenu* self)
{
    int64_t i;
    int64_t n;
    int64_t last;
    int row = 0;
    bool previousWasSeparator = true;
    if (!self || !self->m_actions)
        return 0;
    n = XVector_size_base((const XContainer*)self->m_actions);
    last = xmenu_lastLayoutIndex(self, n);
    for (i = 0; i <= last; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        bool plain;
        if (!action)
            continue;
        plain = xmenu_isPlainSeparator(action);
        if (!XAction_isVisible(action) ||
            (self->m_separatorsCollapsible && previousWasSeparator && plain))
            continue;
        previousWasSeparator = plain;
        ++row;
    }
    return row;
}

static XAction* xmenu_addActionInternal(XMenu* self, XAction* action)
{
    if (!self || !action || !self->m_actions)
        return NULL;

    XObject_connect((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    XVector_push_back_1_base(self->m_actions, &action);
    xmenu_pushOwnedFlag(self, true); /* 便捷路径自建：菜单拥有。 */
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
    return action;
}

/** @brief 对标 QWidget::insertAction 的通用挂接：before 为 NULL 或不在
 *  菜单内时等价追加；动作已在菜单内时先摘除再插入（移动语义，Qt 同
 *  口径）。owned 标记随挂接路径登记。 */
static void xmenu_placeAction(XMenu* self, XAction* before,
                              XAction* action, bool owned)
{
    int index;
    int64_t n;
    bool wasMember = false;
    if (!self || !action || !self->m_actions) return;
    if (before == action) return;
    /* 已在菜单内：先摘除（含所有权标记同步），再按 before 重插。
     * 摘除即断连，重插只建一次（避免同动作重复转发）。 */
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (index = 0; (int64_t)index < n; ++index) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, index);
        if (item && *item == action) {
            wasMember = true;
            xmenu_removeOwnedFlagAt(self, index, n);
            XVector_remove_base((XContainer*)self->m_actions, index, 1);
            break;
        }
    }
    if (wasMember)
        xmenu_disconnectAction(self, action);
    index = -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    if (before) {
        int64_t i;
        for (i = 0; i < n; ++i) {
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, i);
            if (item && *item == before) {
                index = (int)i;
                break;
            }
        }
    }
    XObject_connect((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    if (index < 0) {
        XVector_push_back_1_base(self->m_actions, &action);
        xmenu_pushOwnedFlag(self, owned);
    } else {
        XVector_insert_1_base(self->m_actions, index, &action, 1);
        xmenu_insertOwnedFlag(self, index, owned);
    }
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 动作容器（对标 QMenu） ==================== */
/* [Qt 对齐] sizeHint 重算（定义见后段；addAction 等先于此调用）。 */
static void xmenu_updateSizeHint(XMenu* self);


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
    action = xmenu_addActionInternal(self, action);
    /* [Qt 对齐] addAction→itemsChanged→sizeHint 重算（须在入列之后）。 */
    xmenu_updateSizeHint((XMenu*)self);
    return action;
}

XAction* XMenu_addAction_2(XMenu* self, const char* utf8)
{
    XAction* action;

    if (!self)
        return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, utf8);
    if (!action)
        return NULL;
    action = xmenu_addActionInternal(self, action);
    /* [Qt 对齐] addAction→itemsChanged→sizeHint 重算（须在入列之后）。 */
    xmenu_updateSizeHint((XMenu*)self);
    return action;
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

XAction* XMenu_addMenu(XMenu* self, XMenu* menu)
{
    XAction* action;

    if (!self)
        return NULL;
    /* Qt 的 QMenu::addMenu(QMenu*) 对 NULL 直接解引用（qmenu.cpp:1868-1873
       `menu->menuAction()`）；本库保留容忍扩展：NULL 时创建分隔占位。 */
    if (!menu)
        return XMenu_addSeparator(self);
    /* 对标 QMenu::addMenu(QMenu*)（qmenu.cpp:1866-1873）：
       `QAction *action = menu->menuAction(); addAction(action);`
       ——复用子菜单**自己的** menuAction，不再另建代表动作：子菜单
       setTitle 时该动作文本随之更新（QMenu::setTitle 写 menuAction），
       父菜单条目文本因此联动（旧实现另建动作，标题不跟随，gaps 194）。 */
    action = XMenu_menuAction(menu);
    if (!action)
        return NULL;
    if (menu->m_parentMenu == self && xmenu_findIndex(self, action) >= 0) {
        /* 已加入：幂等返回该子菜单的 menuAction。 */
        return action;
    }

    /* 子菜单登记为本菜单的子控件，父菜单释放时级联释放。 */
    XWidget_setParent((XWidget*)menu, (XWidget*)self, 0);
    menu->m_parentMenu = self;

    /* menuAction 由子菜单拥有（Qt 同：QAction 以 QMenu 为父）；父菜单
       仅借用登记，析构/clear 不删除它（menu->menuAction 随子菜单消亡，
       本菜单经 destroyed 槽自动摘除）。 */
    xmenu_placeAction(self, NULL, action, false);
    xmenu_updateSizeHint(self);
    return action;
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
        XClassDelete(child);
        return NULL;
    }
    return child;
}

void XMenu_clear(XMenu* self)
{
    if (!self || !self->m_actions)
        return;
    /* 对标 QWidget::clear 语义：只销毁菜单拥有的动作；insertAction
       注入的外部动作仅随列表摘除（所有权归调用方）。逐头摘除：
       动作析构经 destroyed 自摘会收缩列表（不可按索引正序迭代）。 */
    while (XVector_size_base((XContainer*)self->m_actions) > 0) {
        int64_t sizeBefore =
            XVector_size_base((XContainer*)self->m_actions);
        XAction* action =
            *(XAction**)XVector_at_base((XContainer*)self->m_actions, 0);
        if (action && xmenu_ownedAt(self, 0))
            XClassDelete(action);
        /* 销毁回调会把元素移出列表；未连接或借用动作时显式摘除兜底。
         * [根修 2026-10-14] 兜底摘除必须同步收缩 m_actionOwned——否则
         * 标志数组与动作列表失同步，下一轮 ownedAt 走「尺寸不等→保守
         * 按拥有」兜底把借用动作误删（containers_menu_actions 实证：
         * clear 误删 ext1/ext2 → 调用方 XClassDelete 双释放）。 */
        if (XVector_size_base((XContainer*)self->m_actions) == sizeBefore) {
            /* 借用动作（未被删除）走此路径：摘除即断连，防重加入重复转发。
             * 拥有动作只会在 destroyed 连接缺失的防御分支里落到这里——
             * 此时对象可能已释放，**不得**再触碰（不加断连）。 */
            if (action && !xmenu_ownedAt(self, 0))
                xmenu_disconnectAction(self, action);
            XVector_remove_base((XContainer*)self->m_actions, 0, 1);
            if (self->m_actionOwned &&
                XVector_size_base((XContainer*)self->m_actionOwned) ==
                    sizeBefore)
                XVector_remove_base((XContainer*)self->m_actionOwned, 0, 1);
        }
    }
    if (self->m_actionOwned)
        XVector_clear_base((XContainer*)self->m_actionOwned);
    self->m_defaultAction = NULL;
    self->m_activeAction = NULL;
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XMenu_isEmpty(const XMenu* self)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions)
        return true;
    /* 对标 qmenu.cpp:2199-2211 QMenu::isEmpty：
       `if (!action->isSeparator() && action->isVisible()) return false;`
       ——判据是「没有**可见且非分隔条**的动作」，不是动作表为空：
       只有分隔条/节标题、或动作全部隐藏时 isEmpty() 为 true。 */
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base((const XContainer*)self->m_actions, i);
        if (item && *item && !XAction_isSeparator(*item) &&
            XAction_isVisible(*item))
            return false;
    }
    return true;
}

const XVector* XMenu_actions(const XMenu* self)
{
    return self ? self->m_actions : NULL;
}

XAction* XMenu_actionAt(const XMenu* self, const XPoint* pos)
{
    XRect rect;
    int row;
    int ah;

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
    ah = self->m_actionHeight > 0 ? self->m_actionHeight : 1;
    row = (pos->y - rect.y) / ah;
    if (row < 0 || row >= xmenu_layoutRowCount(self))
        return NULL;
    /* 行号按布局口径反查：折叠/隐藏的条目已不占行
     * （QMenuPrivate::actionAt → actionRects，qmenu.cpp:478-485 的
     *  空矩形条目不参与命中）。 */
    return xmenu_actionAtRow(self, row);
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
            XClassDelete((XClass*)title);
        }
        /* 对标 QMenuPrivate 的初始化（qmenu.cpp:165-166：
         * `defaultMenuAction = menuAction = new QAction(q);
         *  menuAction->setMenu(q);`）：代表动作自创建起就回指本菜单。
         * 这样 XAction_menu(menuAction) 在所有路径（addMenu、菜单栏
         * addMenu、外部 addAction）上都成立——父容器据此识别「子菜单
         * 入口」并抑制容器动作的 triggered 转发/绘制箭头。 */
        XAction_setMenu(self->m_menuAction, self);
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
        XClassDelete((XClass*)self->m_title);
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
    /* P3-18① 对标 QMenu::setIcon（qmenu.cpp:1112-1115）：
     *   `d_func()->menuAction->setIcon(icon);`
     * ——图标的事实源是 menuAction（Qt 即 menuAction->setIcon），父容器
     * （菜单栏/父菜单条目）经该动作取图标。本菜单侧的 m_icon 只作镜像
     * （getter 以 menuAction 为准，与 XMenu::icon() 读 menuAction 同口径）。
     * menuAction 懒创建（Qt 在构造期即建，此处首次 setIcon 时补建）。 */
    copy = icon ? XString_create_copy(icon) : NULL;
    if (icon && !copy)
        return;                 /* 拷贝失败保持原状态不变 */
    if (self->m_icon)
        XClassDelete((XClass*)self->m_icon);
    self->m_icon = copy;
    /* 清图标且 menuAction 尚未懒创建时无需强建（无图标可清，两个承载
     * 位都已是「无图标」）；其余情况同步写 menuAction。 */
    if (!icon && !self->m_menuAction)
        return;
    {
        XAction* menuAction = XMenu_menuAction(self);
        if (menuAction)
            XAction_setIcon(menuAction, icon);
    }
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
    XClassDeinit(&tmp);
}

const XString* XMenu_icon(const XMenu* self)
{
    const XString* icon;

    /* P3-19 对标 QMenu::icon（qmenu.cpp:1107-1110）：
     *   `return d_func()->menuAction->icon();`
     * ——getter 以 menuAction 的图标为准（外部直接 XAction_setIcon 到
     * menuAction 的改动同样可见，Qt 同口径）；menuAction 未创建（从未
     * setIcon/setMenu 过）时无图标返回 NULL。 */
    if (!self || !self->m_menuAction)
        return NULL;
    icon = XAction_icon_const(self->m_menuAction);
    /* XAction 清图标后保留空串占位（XAction_setIcon(NULL) 口径）；
     * QMenu::icon() 未设置时是 null 图标——空串归一为未设置返回 NULL。 */
    if (icon && XString_length_base(icon) == 0)
        return NULL;
    return icon;
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
    XClassDelete((XClass*)text);
}

XAction* XMenu_defaultAction(const XMenu* self)
{
    return self ? self->m_defaultAction : NULL;
}

void XMenu_setDefaultAction(XMenu* self, XAction* action)
{
    if (!self || self->m_defaultAction == action)
        return;
    self->m_defaultAction = action;
    /* P3-16：缺省动作有视觉线索（样式按 DefaultItem 用粗体）。Qt 的
     * setDefaultAction 只记指针（qmenu.cpp:2055-2058）无 update 调用，
     * 视觉随下一次重绘；本库补一次 update 让线索立即生效（无重绘则
     * 「设置了线索但看不出来」，与项目「选了要兑现」约定相悖）。 */
    XWidget_update((XWidget*)self);
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

bool XMenu_isTearOffEnabled(const XMenu* self)
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
    /* Qt 6.8 删除动态属性的唯一入口是 setProperty(name, QVariant())；
     * XObject_removeProperty 已于 2026-11-11 删除（Qt 无此名）。 */
    (void)XObject_setProperty((XObject*)self, &key, NULL);
    XClassDeinit(&key);
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
    /* 2026-11-11 口径：setProperty **总是**接管 value（Qt 按值语义），
     * 返回值改为「是否命中静态属性」（动态写入恒 false）⇒ 不再按返回值释放。 */
    (void)XObject_setProperty((XObject*)self, &key, v);
    XClassDeinit(&key);
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
    v = XObject_property_const((const XObject*)self, &key);
    XClassDeinit(&key);
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
    (void)XObject_setProperty((XObject*)self, &key, v);   /* 总是接管 v */
    XClassDeinit(&key);
}

static void* xmenu_ptrProp(const XMenu* self, const char* keyUtf8)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return NULL;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    v = XObject_property_const((const XObject*)self, &key);
    XClassDeinit(&key);
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
    id = (int64_t)XObject_startTimer(
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
    /* [弹窗随转·逻辑全局 2026-10-22] mapToGlobal 返回设备全局（旋转正变
       换后），XMenu_popup→setGeometryRect 需逻辑全局——90/270 下子菜单
       错位（同 XComboBox xcombo_popupReposition 根修）。自持父链累加逻
       辑偏移+顶层逻辑原点，rot==0 与 mapToGlobal 逐位一致。 */
    XPoint_init(&global, 0, 0);
    {
        XWidget* top = XWidget_topLevelWidget((XWidget*)self);
        const XWidget* w = (const XWidget*)self;
        while (w && !w->m_isWindow) {
            global.x += w->m_windowRect.x;
            global.y += w->m_windowRect.y;
            w = (const XWidget*)XObject_parent((XObject*)w);
        }
        if (top) {
            global.x += top->m_windowRect.x;
            global.y += top->m_windowRect.y;
        }
    }
    global.x += local.x;
    global.y += local.y;
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
        /* P3-20：菜单关闭即收起条目提示（Qt 关层不再有 ToolTip 目标）。 */
        XToolTip_hideText();
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
       路径不经过本函数，无二次删除风险。同步删改为延迟：本函数由
       mouseRelease/keyPress/triggered 槽路径进入，aboutToHide 已在本帧
       发射，事件分发帧返回途中仍会访问本控件——立即释放 UAF，
       Qt 的 close() 自删即 deleteLater 语义。 */
    if (XWidget_testAttribute((XWidget*)self,
                              XWidgetAttribute_DeleteOnClose))
        XObject_deleteLater((XObject*)self);
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
            XAction_isVisible(*item) && !XAction_isSeparator(*item)) {
            XMenu_setActiveAction(self, *item);
            xmenu_emitAction(self, (size_t)XMenu_hovered_signal, *item);
            return;
        }
    }
}

/* SizeHint 虚槽前置声明（《api命名风格》`*_base` 契约：业务体收编在
 * static VX* 槽内，经 class_init 注册虚表，对外唯一入口是
 * XWidget_sizeHint_base / XWidget_minimumSizeHint_base 虚分派；
 * 实现在下方「尺寸」节与 class_init 注册点前）。 */
static XSize VXMenu_sizeHint(const XWidget* self);

void XMenu_popup(XMenu* self, const XPoint* pos)
{
    XMenu_popup_2(self, pos, NULL);
}

void XMenu_popup_2(XMenu* self, const XPoint* pos, XAction* at)
{
    XSize hint;
    XRect rect;
    XPoint p;

    if (!self || self->m_popupActive)
        return;
    /* P3-22 卫生位：重新弹出时清掉上一轮残留的 NoMouseReplay（Qt 由
     * 内核在收到新鼠标事件时清位；X 侧弹出即视为新的一轮点击序）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_NoMouseReplay,
                         false);
    hint = VXMenu_sizeHint((const XWidget*)self);
    p.x = pos ? pos->x : 0;
    p.y = pos ? pos->y : 0;
    /* 对标 QMenu::popup(const QPoint&, QAction*)（qmenu.cpp:2301-2305 →
     * QMenuPrivate::popup，qmenu.cpp:2426-2450）：atAction 不是「高亮项」，
     * 而是**定位锚**——菜单上移「atAction 之前各条高度」使该条目正好落在
     * p 处（本机 Qt 6.8 探针：popup((100,100), second) 后菜单 y=75、
     * 第二行 y=25、activeAction 仍为 null）。折叠/隐藏条目按布局行号
     * 计入高度（与 xmenu_visualRow 同源）。 */
    if (at) {
        int index = xmenu_findIndex(self, at);
        int row = xmenu_visualRow(self, (int64_t)index);
        if (row > 0)
            p.y -= row * (self->m_actionHeight > 0
                              ? self->m_actionHeight : 20);
    }
    XRect_init(&rect, p.x, p.y,
               hint.width > 0 ? hint.width : 120,
               hint.height > 0 ? hint.height : 20);
    /* [屏幕钳制 2026-10-07] 对标 Qt QMenu::popup 的 adjustToDesktop 段
     * （qmenu.cpp：size boundedTo 屏幕尺寸，随后依屏幕边界回钳 x/y）：
     * 先按锚点所在屏可用区收敛宽高，再把 x/y 钳回屏内——屏幕边缘的
     * 菜单/子菜单不再溢出屏幕，超屏高的菜单收缩到可用区高（滚动
     * 降级留作后续扩展项）。 */
    {
        XScreen* screen = XGuiApplication_screenAt(&p);
        XRect avail;
        if (!screen)
            screen = XGuiApplication_primaryScreen();
        if (screen)
        {
            avail = XScreen_availableGeometry(screen);
            if (rect.width > avail.width) rect.width = avail.width;
            if (rect.height > avail.height) rect.height = avail.height;
            if (rect.x + rect.width > avail.x + avail.width)
                rect.x = avail.x + avail.width - rect.width;
            if (rect.y + rect.height > avail.y + avail.height)
            {
                rect.y = avail.y + avail.height - rect.height;
                if (rect.y < avail.y) rect.y = avail.y;
            }
            if (rect.x < avail.x) rect.x = avail.x;
            if (rect.y < avail.y) rect.y = avail.y;
        }
    }
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
        self->m_grabTimer = XObject_startTimer(
            (XObject*)self, 1u, XTimerType_PreciseTimer);
    }
    self->m_popupActive = true;
}

XAction* XMenu_exec(XMenu* self)
{
    /* 无参 exec 等价 `exec(pos, at = nullptr)`（Qt 同名内联转发，
       qmenu.cpp exec() → exec(QPoint(), nullptr) 口径）。 */
    return XMenu_exec_2(self, NULL, NULL);
}

XAction* XMenu_exec_2(XMenu* self, const XPoint* pos, XAction* at)
{
    bool deleteOnClose;
    XAction* result;

    if (!self)
        return NULL;
    /* exec 阻塞循环期间关闭路径仍在本帧事件分发中执行：若此时
       DeleteOnClose 自删，删除将处于对象自身事件分发帧，同步删 UAF。
       进入 exec 暂存并清除该属性，使菜单 exec 期间不自删，退出时按
       原状态兑现删除（与 XDialog_exec 同口径）。 */
    deleteOnClose = XWidget_testAttribute((XWidget*)self,
                                          XWidgetAttribute_DeleteOnClose);
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_DeleteOnClose,
                         false);
    self->m_execResult = NULL;
    /* 带位置/锚定动作弹出（对标 exec(const QPoint&, QAction*) 的
       popup(p, at) 前置）；pos==NULL 时 popup 按缺省位置处理。 */
    XMenu_popup_2(self, pos, at);
    while (self->m_popupActive)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    /* 先取结果再兑现删除：删除后不得再解引用 self。 */
    result = self->m_execResult;
    if (deleteOnClose)
        XClassDelete(self);
    return result;
}

/* ==================== P3-18：条目图标列（对标 maxIconWidth） ==================== */

/* 图标列宽 = 小图标尺寸(16) + 4（对标 QMenuPrivate::updateActionRects 的
 * `maxIconWidth = qMax<uint>(maxIconWidth, icone + 4)`，qmenu.cpp:376-380；
 * icone = PM_SmallIconSize = 16）。 */
#define XMENU_ICON_COLUMN_WIDTH 20
#define XMENU_ICON_PAINT_SIZE   16

/** @brief 菜单级图标列宽：任一可见非分隔条动作带「菜单中可见」的图标时
 *         返回列宽，否则 0。
 * @details 对标 qmenu.cpp:367-380 的 maxIconWidth 预扫（整列宽度对所有
 *          条目生效——有图标的菜单所有行文本起点统一右移，Qt 同口径）；
 *          门控加 XAction_isIconVisibleInMenu（台账 P3-18 定案：图标在
 *          菜单中被关闭的条目不贡献列宽；Qt 把该门控放在 initStyleOption
 *          清 opt.icon，净效果一致）。 */
static int xmenu_iconColumnWidth(const XMenu* self)
{
    int64_t i;
    int64_t n;

    if (!self || !self->m_actions)
        return 0;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (const XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        const XString* path;

        if (!action || !XAction_isVisible(action) ||
            XAction_isSeparator(action) ||
            !XAction_isIconVisibleInMenu(action))
            continue;
        path = XAction_icon_const(action);
        if (path && XString_length_base(path) > 0)
            return XMENU_ICON_COLUMN_WIDTH;
    }
    return 0;
}

/** @brief 在 (x, 行内垂直居中) 画动作图标（P3-18②）。
 * @details 解码链与 XTabBar 页签图标同款：XIcon_init_file_2 →
 *          XIcon_pixmap → XPixmap_toImage → XPainter_drawImage；
 *          isIconVisibleInMenu 关闭/路径为空/解码失败一律不画。 */
static void xmenu_drawActionIcon(const XAction* action, XPainter* painter,
                                 int x, int y, int cellHeight)
{
    const XString* path;
    const char* utf8;
    XIcon icon;
    XPixmap pixmap;
    XImage image;

    if (!action || !painter || cellHeight <= 0)
        return;
    if (!XAction_isIconVisibleInMenu(action))
        return;
    path = XAction_icon_const(action);
    if (!path || XString_length_base(path) == 0)
        return;
    utf8 = XString_toUtf8((XString*)path);
    if (!utf8 || !utf8[0])
        return;
    XIcon_init_file_2(&icon, utf8);
    if (XIcon_isNull(&icon)) {
        XClassDeinit((XClass*)&icon);
        return;
    }
    XPixmap_init(&pixmap);
    XIcon_pixmap(&icon, XMENU_ICON_PAINT_SIZE, XMENU_ICON_PAINT_SIZE,
                 XIconMode_Normal, XIconState_Off, &pixmap);
    XImage_init(&image);
    XPixmap_toImage(&pixmap, &image);
    if (XImage_width(&image) > 0 && XImage_height(&image) > 0) {
        int iy = y + (cellHeight - XImage_height(&image)) / 2;
        if (iy < y)
            iy = y;
        XPainter_drawImage(painter, &image, x, iy);
    }
    XClassDeinit((XClass*)&image);
    XClassDeinit((XClass*)&pixmap);
    XClassDeinit((XClass*)&icon);
}

/* ==================== 尺寸（对标 QMenu::sizeHint） ==================== */

/* SizeHint 虚槽体（对标 QMenu::sizeHint；《api命名风格》`*_base` 契约：
 * 业务实现只放 VX* 槽，经 XMenu_class_init 注册虚表，对外唯一入口是
 * XWidget_sizeHint_base 虚分派，不再设无后缀公开函数与转发桥）。 */
static XSize VXMenu_sizeHint(const XWidget* self)
{
    XSize out;
    const XMenu* menu = (const XMenu*)self;
    XFont font;
    int64_t count;
    int64_t i;
    int maxWidth;

    XSize_init(&out, 0, 0);
    if (!menu)
        return out;
    count = menu->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)menu->m_actions)
                : 0;
    font = XWidget_font((XWidget*)menu);
    maxWidth = 0;
    /* P3-18③：有「菜单中可见」图标的条目存在时整列右移（Qt 的
     * sizeFromContents(CT_MenuItem) 把 maxIconWidth 计入每条宽度，
     * qmenu.cpp:435-466）。 */
    {
        int iconCol = xmenu_iconColumnWidth(menu);
        for (i = 0; i < count; ++i) {
            XAction** item = (XAction**)XVector_at_base(
                (XContainer*)menu->m_actions, i);
            const XString* label;
            if (!item || !*item || !XAction_isVisible(*item))
                continue;
            label = XAction_text_const(*item);
            /* 宽只取**参与绘制文本**的条目：普通分隔条不贡献宽度；带文本的
             * 节标题（addSection）与普通动作一样计文本宽（Qt 的
             * updateActionRects 对 separator+text 同样量 text 宽）。 */
            if (XAction_isSeparator(*item) &&
                (!label || XString_length_base(label) == 0))
                continue;
            {
                const char* text = label ? XString_toUtf8((XString*)label)
                                         : NULL;
                int width;

                if (!text)
                    text = "";
                width = XPainter_textWidth(&font, text) + iconCol;
                if (XAction_menu(*item))
                    width += 16; /* 子菜单箭头预留。 */
                if (width > maxWidth)
                    maxWidth = width;
            }
        }
    }
    XClassDeinit(&font);
    out.width = maxWidth + 24;
    /* 高 = 参与布局的视觉行数 × 条目高：折叠的分隔条/隐藏动作不占行
     * （QMenu::sizeHint 取 actionRects 并集，qmenu.cpp:2260-2276）。 */
    out.height = xmenu_layoutRowCount(menu) *
                 (menu->m_actionHeight > 0 ? menu->m_actionHeight : 20);
    return out;
}

/* ==================== 虚槽实现（绘制与交互） ==================== */

/** @brief 绘制分隔条/节标题（对标 Fusion CE_MenuItem 的 Separator 分支，
 *         qfusionstyle.cpp:1424-1441）：带文本时节标题画在左侧，分隔线
 *         从其右侧延伸到右边距；无文本时只画居中分隔线。
 * @param self 菜单对象（取字体）。
 * @param painter 目标绘制器。
 * @param action 分隔动作（文本可空）。
 * @param cell 条目矩形（已含行偏移）。 */
static void xmenu_drawSeparator(const XMenu* self, XPainter* painter,
                                const XAction* action, const XRect* cell)
{
    const XString* label = XAction_text_const(action);
    const char* text = label ? XString_toUtf8((XString*)label) : NULL;
    const int margin = 6;
    int lineX = cell->x + margin;
    const int lineRight = cell->x + cell->width - margin;
    int lineY = cell->y + cell->height / 2;

    if (text && text[0]) {
        XFont font = XWidget_font((XWidget*)self);
        int textWidth;
        XPainter_setFont(painter, &font);
        textWidth = XPainter_textWidth(&font, text);
        XPainter_drawText(painter, cell->x + margin,
                          cell->y + (cell->height -
                                     XPainter_textHeight(&font)) / 2 +
                              XPainter_textAscent(&font),
                          text, 0xFF505050u);
        XClassDeinit(&font);
        lineX += textWidth + margin;
    }
    if (lineRight > lineX) {
        XRect line;
        XRect_init(&line, lineX, lineY, lineRight - lineX, 1);
        XPainter_fillRect(painter, &line, 0xFFA0A0A0u);
    }
}

void XMenu_drawContents(XMenu* self, XPainter* painter)
{
    XRect rect;
    XFont font;
    int64_t count;
    int64_t i;
    int iconColumn;

    if (!self || !painter)
        return;
    rect = XWidget_rect((XWidget*)self);
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共接管：菜单面板 + 条目走 PE_PanelMenu/CE_MenuItem
         * （分隔线保留原实现；选中项 highlight + HighlightedText）。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption panel;
        int64_t n;
        int64_t k;
        int iconCol;
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
        iconCol = xmenu_iconColumnWidth(self);
        n = self->m_actions
                ? (int64_t)XVector_size_base((const XContainer*)self->m_actions)
                : 0;
        for (k = 0; k < n; ++k) {
            XAction** it = (XAction**)XVector_at_base(
                (XContainer*)self->m_actions, k);
            XAction* act = it ? *it : NULL;
            XRect cell;
            int row;
            if (!act) continue;
            /* 行号按布局口径（折叠/隐藏条目不占行，gaps 195）。 */
            row = xmenu_visualRow(self, k);
            if (row < 0) continue;
            XRect_init(&cell, rect.x,
                       rect.y + row * self->m_actionHeight,
                       rect.width, self->m_actionHeight);
            if (XAction_isSeparator(act)) {
                xmenu_drawSeparator(self, painter, act, &cell);
                continue;
            }
            {
                /* P3-16/18：升级为派生选项 XStyleOptionMenuItem——
                 * m_menuItemType（DefaultItem 粗体由样式层消费，B2 串行；
                 * SubMenu 对标 QMenu::initStyleOption 的子菜单条目分类）、
                 * m_maxIconWidth（图标列有效值：无可见图标为 0，样式层
                 * 按此右移文本，qfusionstyle 同口径）。 */
                XStyleOptionMenuItem mi;
                XStyleOptionMenuItem_init(&mi, XStyleCE_MenuItem);
                mi.m_base.m_rect = cell;
                mi.m_base.m_state = XAction_isEnabled(act)
                    ? XStyleState_Enabled : 0;
                if (act == self->m_activeAction) {
                    mi.m_base.m_state |= XStyleState_Selected;
                    mi.m_base.m_selected = true;
                }
                mi.m_base.m_text = XString_toUtf8(
                    XAction_text_const(act) ? XAction_text_const(act) : NULL);
                if (XAction_menu(act))
                    mi.m_menuItemType = XStyleOptionMenuItemType_SubMenu;
                else if (act == self->m_defaultAction)
                    mi.m_menuItemType =
                        XStyleOptionMenuItemType_DefaultItem;
                mi.m_maxIconWidth = iconCol;
                /* P3-18③：图标列在菜单侧承载——传给样式的条目矩形收窄
                 * 到图标列右侧（样式在此起点画文本），图标本体由本函数在
                 * 收窄区外侧自画（见 drawControl 之后）。 */
                if (iconCol > 0) {
                    XRect_init(&mi.m_base.m_rect, cell.x + iconCol, cell.y,
                               cell.width - iconCol > 0
                                   ? cell.width - iconCol : 0,
                               cell.height);
                }
#if XPALETTE_ON
                mi.m_base.m_palette = XWidget_palette((XWidget*)self);
#endif
                XStyle_drawControl(style, XStyleCE_MenuItem, &mi.m_base,
                                   painter, (XWidget*)self);
                if (iconCol > 0)
                    xmenu_drawActionIcon(act, painter, rect.x + 6, cell.y,
                                         self->m_actionHeight);
            }
        }
        XClassDeinit(&font);
        return;
    }
#endif /* XSTYLE_ON */
    XPainter_fillRect(painter, &rect, 0xFFF0F0F0u);
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    count = self->m_actions
                ? (int64_t)XVector_size_base(
                      (const XContainer*)self->m_actions)
                : 0;
    iconColumn = xmenu_iconColumnWidth(self);
    for (i = 0; i < count; ++i) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, i);
        XAction* action = item ? *item : NULL;
        XRect cell;
        int row;
        int y;

        if (!action)
            continue;
        row = xmenu_visualRow(self, i);
        if (row < 0)
            continue;
        y = rect.y + row * self->m_actionHeight;
        XRect_init(&cell, rect.x, y, rect.width, self->m_actionHeight);
        if (XAction_isSeparator(action)) {
            xmenu_drawSeparator(self, painter, action, &cell);
            continue;
        }
        if (action == self->m_activeAction)
            XPainter_fillRect(painter, &cell, 0xFFB0C4DEu);
        {
            const char* text = XString_toUtf8(
                XAction_text_const(action) ? XAction_text_const(action)
                                           : NULL);
            const XFont* drawFont = &font;
            XFont boldFont;
            bool bold = (action == self->m_defaultAction);
            int ascent;
            int textX;
            uint32_t color;

            if (!text)
                text = "";
            /* P3-18②：图标列在文本起点左侧（列宽内垂直居中）。 */
            if (iconColumn > 0)
                xmenu_drawActionIcon(action, painter, rect.x + 6, y,
                                     self->m_actionHeight);
            /* P3-16 自绘分支：缺省动作用粗体（对标 Fusion CE_MenuItem
             * 的 DefaultItem 分支 qfusionstyle.cpp:1502-1512
             * `QFont f = opt->font; f.setBold(true)`）。 */
            if (bold) {
                boldFont = XWidget_font((XWidget*)self);
                XFont_setBold(&boldFont, true);
                XPainter_setFont(painter, &boldFont);
                drawFont = &boldFont;
            }
            textX = rect.x + 6 + iconColumn; /* P3-18③ 文本起点右移。 */
            ascent = XPainter_textAscent(drawFont);
            color = XAction_isEnabled(action) ? 0xFF000000u : 0xFF808080u;
            XPainter_drawText(painter, textX,
                              y + (self->m_actionHeight -
                                   XPainter_textHeight(drawFont)) /
                                      2 +
                                  ascent,
                              text, color);
            if (bold) {
                /* 恢复普通字重：后续条目/子菜单箭头不继承粗体。 */
                XPainter_setFont(painter, &font);
                XClassDeinit(&boldFont);
            }
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
    XClassDeinit(&font);
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

/* ---- P3-22：setNoReplayFor 的置位/消费（对标 qmenu.cpp:2902-2904）---- */

/** @brief 控件的全局矩形（mapToGlobal 原点 + 自身尺寸）。 */
static void xmenu_globalRectOf(XWidget* widget, XRect* out)
{
    XPoint origin;

    origin.x = 0;
    origin.y = 0;
    origin = XWidget_mapToGlobal(widget, &origin);
    out->x = origin.x;
    out->y = origin.y;
    out->width = XWidget_width(widget);
    out->height = XWidget_height(widget);
}

/** @brief 关闭性点击落在 noReplayFor 目标矩形内时，给本菜单置
 *         XWidgetAttribute_NoMouseReplay（P3-22）。
 * @details 对标 QMenu::mousePressEvent 的关层分支（qmenu.cpp:2902-2904）：
 *          `if (noReplayFor && QRect(noReplayFor->mapToGlobal(0,0),
 *          noReplayFor->size()).contains(globalPos)) setAttribute(
 *          WA_NoMouseReplay)`。全局坐标平台未注入（恒 0,0，XEvent.h
 *          globalPosition 契约）时按局部坐标换算兜底（虚拟键盘同款）。 */
static void xmenu_markNoReplayIfHit(XMenu* menu,
                                    const XMouseEvent* mouseEvent)
{
    XPoint g;
    XRect hit;

    if (!menu || !menu->m_noReplayFor)
        return;
    g = XMouseEvent_globalPosition(mouseEvent);
    if (g.x == 0 && g.y == 0) {
        XPoint local = XMouseEvent_position(mouseEvent);
        g = XWidget_mapToGlobal((XWidget*)menu, &local);
    }
    xmenu_globalRectOf(menu->m_noReplayFor, &hit);
    if (g.x >= hit.x && g.x < hit.x + hit.width &&
        g.y >= hit.y && g.y < hit.y + hit.height) {
        XWidget_setAttribute((XWidget*)menu,
                             XWidgetAttribute_NoMouseReplay, true);
    }
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
        /* P3-22：关闭性点击落在 setNoReplayFor 目标矩形内 → 置位
         * （Qt 同款在关层分支置 WA_NoMouseReplay）。 */
        xmenu_markNoReplayIfHit(menu, mouseEvent);
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
    /* P3-22：置位期间本次 release **不重放**（忽略）并清位。口径差异写
     * 注释：Qt 的 WA_NoMouseReplay 拦截的是「弹层关层后内核向光标下控件
     * 重放按下」（qapplication.cpp:3355-3359）且由内核在弹层收到新鼠标
     * 事件时清除；X 无通用关层重放管线（平台 hint
     * ReplayMousePressOutsidePopup 缺省 false，虚拟键盘自有重放路径），
     * 故以「release 见置位即忽略并清位」承载同一 setNoReplayFor 语义
     * （整改台账 P3-22 定案），并符合「收到新鼠标事件即清位」的属性契约。
     */
    if (XWidget_testAttribute((XWidget*)menu,
                              XWidgetAttribute_NoMouseReplay)) {
        XWidget_setAttribute((XWidget*)menu,
                             XWidgetAttribute_NoMouseReplay, false);
        XEvent_ignore(event);
        return;
    }
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

/* ---- P3-20：条目悬停提示（setToolTipsVisible 的消费路径）---- */

/** @brief 按 toolTipsVisible + 动作 toolTip 刷新悬停提示（P3-20）。
 * @details Qt 走 QEvent::ToolTip（qmenu.cpp:3044-3057：toolTipsVisible
 *          且 actionAt 命中时，tooltip 非空 → QToolTip::showText，
 *          为空 → hideText）；X 平台无 ToolTip 事件派发 → 改用 hover
 *          驱动（台账 P3-20 定案的口径差异，勿再补 Qt 式事件）。
 *          提示锚定条目全局矩形；移出（VXMenu_leaveEvent）与关闭
 *          （xmenu_close）时由 XToolTip_hideText 收起。 */
static void xmenu_updateHoverToolTip(XMenu* menu, XAction* action)
{
    const XString* tip;
    const char* utf8;

    if (!menu)
        return;
    if (!menu->m_toolTipsVisible || !action) {
        XToolTip_hideText();
        return;
    }
    tip = XAction_toolTip_const(action);
    utf8 = tip ? XString_toUtf8((XString*)tip) : NULL;
    if (!utf8 || !utf8[0]) {
        XToolTip_hideText();    /* Qt 同款：tip 为空 → hideText。 */
        return;
    }
    {
        XRect cell = XMenu_actionGeometry(menu, action);
        XPoint origin;
        XPoint g;
        XRect anchor;

        origin.x = cell.x;
        origin.y = cell.y;
        g = XWidget_mapToGlobal((XWidget*)menu, &origin);
        XRect_init(&anchor, g.x, g.y, cell.width, cell.height);
        XToolTip_showText_2(g.x, g.y, utf8, (XWidget*)menu, &anchor,
                            XWidget_toolTipDuration((XWidget*)menu));
    }
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
            /* P3-20：命中条目且高亮变化后刷新提示（toolTipsVisible 关闭
             * 或条目无 toolTip 时收起旧提示，见 helper）。 */
            xmenu_updateHoverToolTip(menu, action);
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
        /* P3-20：移出即收起条目提示（Qt 的 hideText 同口径）。 */
        XToolTip_hideText();
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
        XClassDelete((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title ? XString_create_copy(other->m_title)
                                   : NULL;
    /* P3-18：图标走 setIcon 同步（拷贝镜像的同时写入本菜单 menuAction，
     * 保持「getter 以 menuAction 为准」不变式；move 路径 m_menuAction 随
     * 移动迁移、天然成对，无需处理）。 */
    XMenu_setIcon(self, other->m_icon);
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
            /* 节标题（addSection：带文本的分隔条）复制为同样的带文本
             * 分隔条；无文本的普通分隔条保持 addSeparator。 */
            const XString* label = XAction_text_const(src);
            if (label && XString_length_base(label) > 0)
                dst = XMenu_addSection(self, label);
            else
                dst = XMenu_addSeparator(self);
        } else if (XAction_menu(src)) {
            XMenu* sub = XAction_menu(src);
            XMenu* subCopy;

            /* P2-13：XMenu_create_copy 公开包装已删（Qt QObject 禁止
             * 拷贝）；泛型拷贝走本虚槽（EXClass_Copy），子菜单深拷贝
             * 就地 create + XClassCopy。 */
            subCopy = XMenu_create();
            if (subCopy) {
                XClassCopy(subCopy, sub);
                XMenu_addMenu(self, subCopy);
            }
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
        XClassDelete((XClass*)self->m_title);
        self->m_title = NULL;
    }
    self->m_title = other->m_title;
    other->m_title = NULL;
    self->m_icon = other->m_icon;
    other->m_icon = NULL;
    self->m_actions = other->m_actions;
    other->m_actions = XVector_create(sizeof(XAction*));
    self->m_actionOwned = other->m_actionOwned;
    other->m_actionOwned = XVector_create(sizeof(bool));
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
            /* 仅销毁菜单拥有的动作；insertAction 注入的外部动作仅随
               列表摘除（对标 QWidget::addAction 不取得所有权）。 */
            if (item && *item && xmenu_ownedAt(self, 0))
                XClassDelete(*item);
            /* 动作析构经 destroyed 信号自摘（xmenu_actionDestroyedSlot
             * 已把自身移出向量）——尺寸已缩时不可再补 remove，否则会把
             * 下一个动作指针丢弃不删（隔个漏删，§8.0g6 ASan 复扫定位的
             * 真缺陷）；自摘未发生时（防御）手动摘除防死循环。
             * [根修 2026-10-15] 手动摘除必须同步收缩 m_actionOwned
             * （与 XMenu_clear 2026-10-14 根修同款失同步）：borrow 动作
             * 摘除后旗数>动作数，ownedAt 对齐守卫失效走「保守按拥有」，
             * 析构循环把剩余借用动作全部误删——containers_mainwindow
             * 实证：popup 借挂工具栏 toggleViewAction，析构误删后
             * XToolBar_deinit 悬垂 delete（ASan heap-use-after-free）。 */
            if (XVector_size_base((XContainer*)self->m_actions) == sizeBefore) {
                XVector_remove_base((XContainer*)self->m_actions, 0, 1);
                if (self->m_actionOwned &&
                    XVector_size_base((XContainer*)self->m_actionOwned) ==
                        sizeBefore)
                    XVector_remove_base((XContainer*)self->m_actionOwned,
                                        0, 1);
            }
        }
        XClassDelete(self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_actionOwned) {
        XClassDelete(self->m_actionOwned);
        self->m_actionOwned = NULL;
    }
    if (self->m_menuAction) {
        XClassDelete(self->m_menuAction);
        self->m_menuAction = NULL;
    }
    if (self->m_title) {
        XClassDelete((XClass*)self->m_title);
        self->m_title = NULL;
    }
    if (self->m_icon) {
        XClassDelete((XClass*)self->m_icon);
        self->m_icon = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* MinimumSizeHint 虚槽体（对标 Qt 的 minimumSizeHint 虚函数；Qt 的
 * QMenu 未覆写最小提示——槽内经 XClass_Parent 直调 XWidget 基类默认
 * 实现，不经对象自身虚表再入。《api命名风格》`*_base` 契约：对外唯一
 * 入口是 XWidget_minimumSizeHint_base 虚分派）。 */
static XSize VXMenu_minimumSizeHint(const XWidget* self)
{
    return XClass_Parent(XWidget, EXWidget_MinimumSizeHint,
                         XSize (*)(const XWidget*))(self);
}

XVtable* XMenu_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMenu)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXMenu_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXMenu_minimumSizeHint);
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

/** @brief 刷新尺寸提示（对标 Qt QMenu）。
 *  @details Qt 6.8.3（qmenu.cpp:2260-2276）：sizeHint = 全部动作矩形
 *           （actionRects）的并集尺寸——XMenu 侧对应 XMenu_actionGeometry
 *           逐动作矩形。菜单是 Popup 窗口、通常不参与盒布局分配，本项
 *           补齐是为「按尺寸摆放」类调用方（如弹层定位）提供同源口径。 */
static void xmenu_updateSizeHint(XMenu* self)
{
    XSize hint;
    const XVector* actions;
    size_t i;
    int bx = 0, by = 0, bx2 = 0, by2 = 0;
    bool any = false;
    if (!self) return;
    actions = XMenu_actions(self);
    if (actions) {
        size_t n = XVector_size_base((const XContainer*)actions);
        for (i = 0; i < n; ++i) {
            XAction* a = (XAction*)XVector_at_base(actions, (int64_t)i);
            XRect r;
            if (!a) continue;
            r = XMenu_actionGeometry(self, a);
            if (r.width <= 0 || r.height <= 0) continue;
            if (!any) {
                bx = r.x; by = r.y; bx2 = r.x + r.width; by2 = r.y + r.height;
                any = true;
                continue;
            }
            if (r.x < bx) bx = r.x;
            if (r.y < by) by = r.y;
            if (r.x + r.width > bx2) bx2 = r.x + r.width;
            if (r.y + r.height > by2) by2 = r.y + r.height;
        }
    }
    if (!any) XSize_init(&hint, 0, 0);
    else XSize_init(&hint, bx2 - bx, by2 - by);
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* Qt QMenu 未 override minimumSizeHint（继承 QWidget 缺省）——保持同口径：
     * 不另设最小尺寸。 */
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
    self->m_actionOwned = XVector_create(sizeof(bool));
    self->m_actionHeight = 20;
    self->m_separatorsCollapsible = true;
    self->m_grabTimer = XTIMER_INVALID_ID;
    if (utf8Title && utf8Title[0])
        XMenu_setTitle_2(self, utf8Title);
    xmenu_updateSizeHint(self);
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

/* P2-13/14：XMenu_create_copy / XMenu_create_move 公开包装已删
 * （Qt QObject 禁止拷贝/移动语义不对外）；VXMenu_copy / VXMenu_move
 * 虚槽保留给 XClass 泛型拷贝/移动（上方 EXClass_Copy / EXClass_Move
 * 虚表挂载）。 */

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
    XObject_connect((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xmenu_actionTriggeredSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, xmenu_actionDestroyedSlot,
                      XConnectionType_Direct);
    n = XVector_size_base((const XContainer*)self->m_actions);
    if (index < 0 || index >= (int)n) {
        XVector_push_back_1_base(self->m_actions, &action);
        xmenu_pushOwnedFlag(self, true); /* insert* 便捷路径自建：菜单拥有。 */
    }
    else {
        XVector_Insert(self->m_actions, index, XAction*, action);
        xmenu_insertOwnedFlag(self, index, true);
    }
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
    /* 对标 QMenu::addSection（qmenu.cpp:1945-1951）：
     *   `QAction *action = new QAction(text, this);
     *    action->setSeparator(true); addAction(action);`
     * ——节标题是 **isSeparator()==true 且保持 enabled** 的动作（文本
     * 作为节提示由样式决定是否绘制），不是「禁用的普通动作」。本机
     * Qt 6.8 探针实测 isSeparator=1/enabled=1/visible=1。 */
    XAction_setSeparator(action, true);
    return xmenu_insertActionInternal(self, action, -1);
}

XAction* XMenu_addSection_2(XMenu* self, const char* utf8)
{
    XAction* action;
    if (!self) return NULL;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                               utf8 ? utf8 : "");
    if (!action) return NULL;
    /* 同 XMenu_addSection：节标题 = 带文本的分隔条（qmenu.cpp:1945-1951）。 */
    XAction_setSeparator(action, true);
    return xmenu_insertActionInternal(self, action, -1);
}

void XMenu_addActionAction(XMenu* self, XAction* action)
{
    /* 对标 QMenu 经 `using QWidget::addAction` 继承的
       addAction(QAction*)（qmenu.h:42）：追加既有动作，借用不接管。 */
    xmenu_placeAction(self, NULL, action, false);
    /* [Qt 对齐 2026-11-02] addAction 触发 itemsChanged（更新几何）。 */
    xmenu_updateSizeHint((XMenu*)self);
}

void XMenu_insertAction(XMenu* self, XAction* before, XAction* action)
{
    xmenu_placeAction(self, before, action, false);
}

void XMenu_removeAction(XMenu* self, XAction* action)
{
    int64_t count;
    int64_t index;
    if (!self || !action || !self->m_actions) return;
    count = (int64_t)XVector_size_base((const XContainer*)self->m_actions);
    for (index = 0; index < count; ++index) {
        XAction** item = (XAction**)XVector_at_base(
            (XContainer*)self->m_actions, index);
        if (item && *item == action) {
            xmenu_removeOwnedFlagAt(self, index, count);
            XVector_remove_base((XContainer*)self->m_actions, index, 1);
            /* 摘除即断连：动作归还调用方后再加入不重复转发。 */
            xmenu_disconnectAction(self, action);
            /* 摘除的是当前默认/激活动作时清空引用，避免悬挂。 */
            if (self->m_defaultAction == action)
                self->m_defaultAction = NULL;
            if (self->m_activeAction == action)
                self->m_activeAction = NULL;
            if (self->m_execResult == action)
                self->m_execResult = NULL;
            XWidget_updateGeometry((XWidget*)self);
            XWidget_update((XWidget*)self);
            return;
        }
    }
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
    if (!self) return NULL;
    if (!menu) return XMenu_insertSeparator(self, before);
    /* 对标 QMenu::insertMenu（qmenu.cpp:1975-1981）：
       `QAction *action = menu->menuAction(); insertAction(before, action);`
       ——复用子菜单自己的 menuAction（标题联动同 addMenu，gaps 194）。 */
    XWidget_setParent((XWidget*)menu, (XWidget*)self, 0);
    menu->m_parentMenu = self;
    action = XMenu_menuAction(menu);
    if (!action) return NULL;
    /* before 不在表内时等价追加（placeAction 的 before=NULL 分支）。 */
    if (xmenu_findIndex(self, before) < 0)
        before = NULL;
    xmenu_placeAction(self, before, action, false);
    xmenu_updateSizeHint(self);
    return action;
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
    /* 对标 QMenu::insertSection（qmenu.cpp:2025-2031）：节标题 =
     * 带文本的分隔条，保持 enabled（同 addSection）。 */
    XAction_setSeparator(action, true);
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
    /* 同 insertSection：带文本的分隔条（qmenu.cpp:2025-2031）。 */
    XAction_setSeparator(action, true);
    index = xmenu_findIndex(self, before);
    return xmenu_insertActionInternal(self, action, index);
}

XRect XMenu_actionGeometry(const XMenu* self, XAction* action)
{
    XRect rect;
    XRect out;
    int index;
    int row;
    int ah;
    if (!self || !action || !self->m_actions) {
        XRect_init(&out, 0, 0, 0, 0);
        return out;
    }
    rect = XWidget_rect((XWidget*)self);
    ah = self->m_actionHeight > 0 ? self->m_actionHeight : 1;
    /* 折叠/隐藏/尾部分隔条在 Qt 里得到空 QRect
     * （QMenuPrivate::updateActionRects，qmenu.cpp:392-403 + 487-502）。 */
    index = xmenu_findIndex(self, action);
    row = xmenu_visualRow(self, (int64_t)index);
    if (row < 0) {
        XRect_init(&out, 0, 0, 0, 0);
        return out;
    }
    XRect_init(&out, rect.x, rect.y + (int)row * ah, rect.width, ah);
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

#endif /* XGUI_ON */
