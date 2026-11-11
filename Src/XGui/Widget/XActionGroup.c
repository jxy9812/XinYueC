/**
 * @file       XActionGroup.c
 * @brief      动作逻辑分组实现（对标 Qt 6.8 QActionGroup 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；成员动作借用不拥有，
 *             每个成员挂一个内信号桥转发 triggered/hovered，并处理
 *             动作先销毁、互斥选中与 enabled 转发。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XActionGroup.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XVector.h"
#include "XGuiConfig.h"

#if XWIDGET_ON && XACTION_ON

/* ==================== 内部工具 ==================== */

/** @brief 发射带动作参数的信号（无连接时释放参数，防泄漏）。 */
static void xactiongroup_emitAction(XActionGroup* self, size_t signal,
                                    XAction* action)
{
    XVarList* args = XVarList_Create(XVar(XAction*, action));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 成员索引；非成员返回 -1。 */
static int xactiongroup_indexOf(const XActionGroup* self,
                                const XAction* action)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions || !action) return -1;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item == action) return (int)i;
    }
    return -1;
}

/** @brief 互斥：选中某动作时取消其它成员选中（策略非 None 生效）。 */
static void xactiongroup_applyExclusive(XActionGroup* self,
                                        XAction* checked)
{
    int64_t i;
    int64_t n;
    if (!self->m_actions) return;
    /* 对标 qactiongroup.cpp:21-37 _q_actionChanged：策略 None 时组不
       干预成员选中（Exclusive 与 ExclusiveOptional 的联动部分相同）。 */
    if (self->m_exclusionPolicy == XActionGroupExclusionPolicy_None) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item && *item != checked &&
            XAction_isChecked(*item))
            XAction_setChecked(*item, false);
    }
}

/* [冗余清理 2026-11-11] xactiongroup_scanChecked 已删除：旧实现用「扫描
 * 成员里第一个 isChecked」冒充 checkedAction()，与 Qt 的缓存语义
 * （qactiongroup.cpp:296-301 返回 d->current）不符——非互斥组同时选中
 * 多个成员时 Qt 返回 NULL 而扫描会返回 A。缓存的唯一事实源已改为
 * m_checkedAction + changed 槽（见 xactiongroup_bridgeChangedSlot）。 */

/* ==================== 成员动作桥接（C 语言的 per-action 闭包等价物） ==================== */

XCLASS_DEFINE_BEGING(XActionGroupBridge)
XCLASS_DEFINE_EXTEND_END(XActionGroupBridge, XObject)

typedef struct XActionGroupBridge
{
    XObject     m_base;      /**< 基类成员；必须是第一个。 */
    XActionGroup* m_group;   /**< 所属组（借用）。 */
    XAction*    m_action;    /**< 桥接的成员动作（借用）。 */
    bool        m_actionDead;/**< 动作已销毁（跳过断连）。 */
    bool        m_groupDead; /**< 组已析构/正在析构（destroyed 槽只断连）。 */
} XActionGroupBridge;

static XVtable* XActionGroupBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XActionGroupBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static void xactiongroup_bridgeTriggeredSlot(XObject* receiver,
                                             XVarList* args);
static void xactiongroup_bridgeChangedSlot(XObject* receiver,
                                           XVarList* args);
static void xactiongroup_bridgeHoveredSlot(XObject* receiver,
                                           XVarList* args);
static void xactiongroup_bridgeDestroyedSlot(XObject* receiver,
                                             XVarList* args);

static XActionGroupBridge* xactiongroup_bridgeCreate(XActionGroup* group,
                                                     XAction* action)
{
    XActionGroupBridge* bridge =
        (XActionGroupBridge*)XMemory_malloc(sizeof(*bridge),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!bridge) return NULL;
    XMemset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XActionGroupBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_group = group;
    bridge->m_action = action;
    return bridge;
}

/* 桥脱离组：**两侧**连接必须成对摘除，且退出前清空两个借用指针。
 * @param deferred true=事件循环归还后回收（运行期路径，桥在
 *        triggered/destroyed Direct 发射链上）；false=立即回收（仅限析构路径）。
 * @details 【2026-11-11 ASan 实锅：heap-use-after-free @ XObject.c:955】
 *          原实现只断**动作侧**四条连接，组侧 destroyed 连接留着，而桥又走
 *          `XObject_deleteLater` 延迟回收。于是 `XActionGroup_removeAction()`
 *          之后存在一个"已不属于组、但仍接在组 destroyed 上、且借用着成员
 *          动作指针"的桥；动作随即被调用方释放、组又在**同一拍**（事件循环
 *          尚未跑）析构时，组的 destroyed 照常投递给这座桥 ⇒
 *          `XObject_disconnect_1(bridge->m_action, ...)` 读已释放内存。
 *          复现：`XinYueC_Static --test qt-align` 的
 *          XActionGroup.checkedActionCacheAndExclusive 用例
 *          （removeAction(foreign) → XClassDelete(foreign) → XClassDelete(group)）。
 *          对照 Qt 6.8 QActionGroup::removeAction（qactiongroup.cpp:139-146）：
 *          断开的是**双向**——`a->d_func()->group = nullptr` 且
 *          `QObject::disconnect(a, nullptr, this, nullptr)`，组从不保留对已移除
 *          成员的引用（Qt 也没有本仓库这种 per-action 桥对象）。这里同口径：
 *          组侧连接同步断开，`m_group/m_action` 置 NULL，使桥在延迟回收窗口内
 *          对任何迟到发射都据 NULL 早退。 */
static void xactiongroup_bridgeDestroyDeferred(XActionGroupBridge* bridge, bool deferred)
{
    if (!bridge) return;
    if (!bridge->m_actionDead && bridge->m_action) {
        XObject_disconnect_1((XObject*)bridge->m_action,
                             XSignal(XAction_triggered_signal),
                             (XObject*)bridge,
                             xactiongroup_bridgeTriggeredSlot);
        XObject_disconnect_1((XObject*)bridge->m_action,
                             XSignal(XAction_changed_signal),
                             (XObject*)bridge,
                             xactiongroup_bridgeChangedSlot);
        XObject_disconnect_1((XObject*)bridge->m_action,
                             XSignal(XAction_hovered_signal),
                             (XObject*)bridge,
                             xactiongroup_bridgeHoveredSlot);
        XObject_disconnect_1((XObject*)bridge->m_action,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)bridge,
                             xactiongroup_bridgeDestroyedSlot);
    }
    if (!bridge->m_groupDead && bridge->m_group) {
        XObject_disconnect_1((XObject*)bridge->m_group,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)bridge,
                             xactiongroup_bridgeDestroyedSlot);
    }
    bridge->m_action = NULL;
    bridge->m_group = NULL;
    if (deferred)
        XObject_deleteLater((XObject*)bridge);
    else
        XClassDelete((XClass*)bridge);
}

/** @brief triggered 转发：只发射组 triggered(action)（对标 Qt
 *         QActionGroup::_q_actionTriggered，qactiongroup.cpp:44-49——
 *         该槽只 `emit triggered(action)`，选中缓存的维护全部在
 *         `_q_actionChanged` 里）。 */
static void xactiongroup_bridgeTriggeredSlot(XObject* receiver,
                                             XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    if (!bridge || !bridge->m_group || !bridge->m_action || !args) return;
    if (bridge->m_groupDead) return;
    self = bridge->m_group;
    action = bridge->m_action;
    xactiongroup_emitAction(self, (size_t)XActionGroup_triggered_signal,
                            action);
}

/** @brief 成员 changed 转发：维护 checkedAction 缓存 + 互斥联动
 *         （对标 QActionGroup::_q_actionChanged，qactiongroup.cpp:25-42）。
 * @details    与 Qt 逐条同构：
 *             - 策略为 None 时**整段跳过**（非互斥组不跟踪选中缓存）；
 *             - 动作变选中：若缓存不是它，先取消旧缓存的选中
 *               （`previous->setChecked(false)`），再记它；
 *             - 动作取消选中且它就是缓存：清 NULL。
 *             注意 Qt 只在"缓存不是它"时取消**上一个缓存动作**，而
 *             不是遍历全部成员——本实现同口径（相比旧实现的
 *             applyExclusive 遍历全部成员，这里保留 applyExclusive 作为
 *             兜底不必要，已按 Qt 精确化）。 */
static void xactiongroup_bridgeChangedSlot(XObject* receiver,
                                           XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    XAction* previous;
    (void)args;
    if (!bridge || !bridge->m_group || !bridge->m_action ||
        bridge->m_groupDead)
        return;
    self = bridge->m_group;
    action = bridge->m_action;
    if (self->m_exclusionPolicy == XActionGroupExclusionPolicy_None)
        return;
    if (XAction_isChecked(action)) {
        if (action != self->m_checkedAction) {
            previous = self->m_checkedAction;
            if (previous)
                XAction_setChecked(previous, false);
            self->m_checkedAction = action;
        }
    } else if (action == self->m_checkedAction) {
        self->m_checkedAction = NULL;
    }
}

/** @brief hovered 转发。 */
static void xactiongroup_bridgeHoveredSlot(XObject* receiver,
                                           XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    if (!bridge || !bridge->m_group || !bridge->m_action || !args) return;
    if (bridge->m_groupDead) return;
    self = bridge->m_group;
    action = bridge->m_action;
    xactiongroup_emitAction(self, (size_t)XActionGroup_hovered_signal,
                            action);
}

/** @brief 动作 destroyed 转发：动作先于组销毁时自动移除成员并删桥。
 * @details 本槽同时是「动作 destroyed」与「组 destroyed」的接收者，靠
 *          `XObject_sender()` 区分两条来源；两条来源都可能与**延迟回收窗口**
 *          重叠，故对 `m_action` / `m_group` 一律先判有效性再解引用
 *          （详见 xactiongroup_bridgeDestroyDeferred 的 2026-11-11 ASan 记录）。 */
static void xactiongroup_bridgeDestroyedSlot(XObject* receiver,
                                             XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    int64_t i;
    int64_t n;
    int index = -1;
    if (!bridge || !bridge->m_group) return;
    {
        XObject* sender = XObject_sender(receiver);
        if (sender == (XObject*)bridge->m_group) {
            /* 组亡发射帧：组内存此刻仍存。置亡标记 + 断开动作侧
               destroyed 连接，此后动作后亡不再进本槽。
               动作侧指针可能已失效（动作先亡/已被移除后释放），
               仅在借用仍有效时断连。 */
            bridge->m_groupDead = true;
            if (bridge->m_action && !bridge->m_actionDead)
                XObject_disconnect_1((XObject*)bridge->m_action,
                                     XSignal(XObject_destroyed_signal),
                                     (XObject*)bridge,
                                     xactiongroup_bridgeDestroyedSlot);
            return;
        }
    }
    if (bridge->m_groupDead) {
        /* 组已析构：组侧数据不可访问；桥随组析构流程回收，
           此处只断开与动作的连接，防动作后亡时再次触发本槽。 */
        if (bridge->m_action && !bridge->m_actionDead)
            XObject_disconnect_1((XObject*)bridge->m_action,
                                 XSignal(XObject_destroyed_signal),
                                 (XObject*)bridge,
                                 xactiongroup_bridgeDestroyedSlot);
        return;
    }
    self = bridge->m_group;
    bridge->m_actionDead = true;
    action = bridge->m_action;
    /* 动作已亡：清空借用，桥在延迟回收窗口内对任何迟到发射都据 NULL 早退
       （组 destroyed 若在此期间到达，上面两个分支不会再碰已释放的动作）。 */
    bridge->m_action = NULL;
    /* 组侧 destroyed 同步断开 + 清借用：成员已被摘除，桥不再需要组的生命
       周期通知。延迟回收窗口内组若析构，不得再回调这座桥
       （组内存到期后任何解引用都是 UAF）。 */
    XObject_disconnect_1((XObject*)self,
                         XSignal(XObject_destroyed_signal),
                         (XObject*)bridge,
                         xactiongroup_bridgeDestroyedSlot);
    bridge->m_group = NULL;
    if (!self->m_bridges) return;
    n = XVector_size_base((const XContainer*)self->m_bridges);
    for (i = 0; i < n; ++i) {
        XActionGroupBridge** bp =
            (XActionGroupBridge**)XVector_at_base(self->m_bridges, i);
        if (bp && *bp == bridge) {
            index = (int)i;
            break;
        }
    }
    if (index >= 0) {
        if (self->m_actions)
            XVector_remove_base(self->m_actions, index, 1);
        XVector_remove_base(self->m_bridges, index, 1);
        if (self->m_checkedAction == action)
            self->m_checkedAction = NULL;
        /* 本槽运行在动作 destroyed 发射帧上；延迟回收桥。 */
        XObject_deleteLater((XObject*)bridge);
    }
}

/** @brief 为成员动作创建桥并连接四类信号。 */
static XActionGroupBridge* xactiongroup_connectAction(XActionGroup* self,
                                                      XAction* action)
{
    XActionGroupBridge* bridge =
        xactiongroup_bridgeCreate(self, action);
    if (!bridge) return NULL;
    XObject_connect_1((XObject*)action,
                      XSignal(XAction_triggered_signal),
                      (XObject*)bridge,
                      xactiongroup_bridgeTriggeredSlot,
                      XConnectionType_Direct);
    /* 对标 QActionGroup::addAction 的 `connect(a, &QAction::changed,
     * this, &QActionGroup::_q_actionChanged)`（qactiongroup.cpp:129）：
     * 选中缓存的维护挂在 changed 上，程序化 setChecked 与用户触发
     * 走同一条路径。 */
    XObject_connect_1((XObject*)action,
                      XSignal(XAction_changed_signal),
                      (XObject*)bridge,
                      xactiongroup_bridgeChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action,
                      XSignal(XAction_hovered_signal),
                      (XObject*)bridge,
                      xactiongroup_bridgeHoveredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action,
                      XSignal(XObject_destroyed_signal),
                      (XObject*)bridge,
                      xactiongroup_bridgeDestroyedSlot,
                      XConnectionType_Direct);
    /* 组自身 destroyed 也接入本槽：组亡时组内存尚存，槽内置 groupDead
       并断开动作侧连接，堵住"组先亡、动作后亡"窗口期 UAF。 */
    XObject_connect_1((XObject*)self,
                      XSignal(XObject_destroyed_signal),
                      (XObject*)bridge,
                      xactiongroup_bridgeDestroyedSlot,
                      XConnectionType_Direct);
    return bridge;
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_actionGroup_deinit(XActionGroup* self)
{
    if (!self) return;
    /* 先置组亡标记：后续回收桥/基类析构发射 destroyed 时，桥槽据此
       停止访问组侧数据（动作销毁顺序不受组控制）。 */
    if (self->m_bridges) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XActionGroupBridge** bp =
                (XActionGroupBridge**)XVector_at_base(self->m_bridges, i);
            if (bp && *bp) (*bp)->m_groupDead = true;
        }
    }
    if (self->m_actions) {
        /* 组析构先清成员回指（对标 QActionPrivate::group 的 QPointer
           自动置空，qaction_p.h:55/77）：外部借用动作可能比组活得久，
           不清空会留下悬垂的 m_group；同时恢复被组强制的启用态。
           桥的 m_groupDead 已在上方置位，changed 槽会据它早退。 */
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_actions);
        for (i = 0; i < n; ++i) {
            XAction** item =
                (XAction**)XVector_at_base(self->m_actions, i);
            if (item && *item)
                XAction_detachGroupQuiet(*item);
        }
        XClassDelete((XClass*)self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_bridges) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XActionGroupBridge** bp =
                (XActionGroupBridge**)XVector_at_base(self->m_bridges, i);
            if (bp && *bp)
                xactiongroup_bridgeDestroyDeferred(*bp, false);
        }
        XClassDelete((XClass*)self->m_bridges);
        self->m_bridges = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XActionGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XActionGroup)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_actionGroup_deinit);
    return XVTABLE_DEFAULT;
}

void XActionGroup_init(XActionGroup* self, XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    if (parent)
        XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XActionGroup);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt：默认互斥（ExclusionPolicy::Exclusive，qactiongroup.h:83
       Q_PROPERTY exclusionPolicy）；enabled/visible 默认 true。 */
    self->m_exclusionPolicy = XActionGroupExclusionPolicy_Exclusive;
    self->m_enabled = true;
    self->m_visible = true;
    self->m_checkedAction = NULL;
    self->m_actions = XVector_Create(XAction*);
    self->m_bridges = XVector_Create(XActionGroupBridge*);
}

XActionGroup* XActionGroup_create_ex(XMemoryType memory, XObject* parent)
{
    XActionGroup* self =
        (XActionGroup*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XActionGroup_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 成员管理（对标 QActionGroup） ==================== */

XAction* XActionGroup_addAction(XActionGroup* self, XAction* action)
{
    struct XActionGroup* oldGroup;
    /* 头文件契约：self/action 任一为 NULL 返回 NULL（C 侧 NULL 入参
     * 防御口径；Qt 的成员函数无法表达 NULL this）。此前 self==NULL 时
     * 原样返回 action，调用方会误以为加入成功。 */
    if (!self || !action)
        return NULL;
    if (xactiongroup_indexOf(self, action) < 0) {
        if (!self->m_actions || !self->m_bridges) return NULL;
        /* 对标 qactiongroup.cpp:139-146：动作已属其它组时先从旧组摘除
           （旧组不再引用该动作；本库的摘除同时恢复被旧组强制的启用态）。 */
        oldGroup = XAction_actionGroup(action);
        if (oldGroup && oldGroup != self)
            XActionGroup_removeAction(oldGroup, action);
        XVector_push_back_1_base(self->m_actions, &action);
        {
            XActionGroupBridge* bridge =
                xactiongroup_connectAction(self, action);
            XVector_push_back_1_base(self->m_bridges, &bridge);
        }
        if (XAction_isChecked(action)) {
            /* 对标 QActionGroup::addAction（qactiongroup.cpp:127-137）：
             *   `if (a->isChecked()) d->current = a;`
             * ——**只记缓存、不取消其它已选中成员**。Qt 的互斥收敛只发生
             * 在 `_q_actionChanged`（成员选中态变化）路径；加入一个已
             * 选中的外部动作时既有成员保持选中，checkedAction() 返回新
             * 成员（本机 Qt 6.8 探针：a/b 均 checked、checkedAction=b）。
             * 旧实现此处走 applyExclusive 取消其余成员（gaps 205）。 */
            self->m_checkedAction = action;
        }
        /* 组回指（对标 QActionPrivate::group，qaction_p.h:55）：
           bind 内部按组 enabled 走 byGroup 路径裁决，不写显式启用位。 */
        XAction_bindGroup(action, self, self->m_enabled);
    }
    /* 对标 qactiongroup.cpp:135-137：重复加入也重新套用组 enabled/visible
       （Qt 无条件 `setEnabled(d->enabled, true)` / `setVisible(d->visible)`），
       组状态为开时不会覆盖成员的显式禁用（XAction_setEnabledByGroup 裁决）。 */
    XAction_setEnabledByGroup(action, self->m_enabled);
    XAction_setVisible(action, self->m_visible);
    return action;
}

XAction* XActionGroup_addAction_2(XActionGroup* self, const char* utf8Text)
{
    XAction* action;
    /* self==NULL 时不建动作：addAction 已按契约返回 NULL，先建后交会
     * 留下无人拥有的孤儿（本函数返回 NULL 也无法回收）。 */
    if (!self) return NULL;
    /* 对标 QActionGroup::addAction(text)：新建动作以组为父
       （qactiongroup.cpp:159-162 的 new QAction(text, this)）。 */
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)self,
                               utf8Text);
    if (!action) return NULL;
    return XActionGroup_addAction(self, action);
}

XAction* XActionGroup_addAction_3(XActionGroup* self, const char* iconUtf8,
                                  const char* utf8Text)
{
    XAction* action;
    if (!self) return NULL; /* 同 addAction_2：NULL 组不建孤儿动作。 */
    /* 对标 QActionGroup::addAction(icon, text)
       （qactiongroup.cpp:173-176）；图标以路径承载（XAction_setIcon）。 */
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)self,
                               utf8Text);
    if (!action) return NULL;
    if (iconUtf8)
        XAction_setIcon_2(action, iconUtf8);
    return XActionGroup_addAction(self, action);
}

void XActionGroup_removeAction(XActionGroup* self, XAction* action)
{
    int index;
    if (!self || !self->m_actions || !self->m_bridges) return;
    index = xactiongroup_indexOf(self, action);
    if (index < 0) return;
    {
        XActionGroupBridge** bp =
            (XActionGroupBridge**)XVector_at_base(self->m_bridges, index);
        if (bp && *bp)
            xactiongroup_bridgeDestroyDeferred(*bp, true);
    }
    XVector_remove_base(self->m_bridges, index, 1);
    XVector_remove_base(self->m_actions, index, 1);
    if (self->m_checkedAction == action)
        self->m_checkedAction = NULL;
    /* 对标 qactiongroup.cpp:193 `action->d_func()->group = nullptr`；
     * 并恢复该动作被组强制修改前的启用态（XGui 扩展，见
     * XAction_bindGroup 的 @details）。 */
    XAction_bindGroup(action, NULL, true);
}

XVector* XActionGroup_actions(const XActionGroup* self)
{
    XVector* out;
    int64_t i;
    int64_t n;
    out = XVector_Create(XAction*);
    if (!out) return NULL;
    if (!self || !self->m_actions) return out;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item)
            XVector_push_back_1_base(out, item);
    }
    return out;
}

/* ==================== 选中与状态（对标 QActionGroup） ==================== */

XAction* XActionGroup_checkedAction(const XActionGroup* self)
{
    /* 对标 qactiongroup.cpp:296-301：返回缓存的 d->current，不做成员
       扫描——非互斥（None）策略下缓存从不被写入，Qt 实测同时选中多个
       成员时 checkedAction() 为 NULL。 */
    return self ? self->m_checkedAction : NULL;
}

void XActionGroup_setCheckedAction(XActionGroup* self, XAction* action)
{
    if (!self || !action) return;
    if (xactiongroup_indexOf(self, action) < 0) return;
    if (!XAction_isCheckable(action)) return;
    xactiongroup_applyExclusive(self, action);
    XAction_setChecked(action, true);
    self->m_checkedAction = action;
    xactiongroup_emitAction(self, (size_t)XActionGroup_triggered_signal,
                            action);
}

bool XActionGroup_isExclusive(const XActionGroup* self)
{
    /* 对标 qactiongroup.cpp:263-270：策略非 None 即互斥。 */
    return self ? self->m_exclusionPolicy !=
                      XActionGroupExclusionPolicy_None : false;
}

void XActionGroup_setExclusive(XActionGroup* self, bool exclusive)
{
    /* 对标 qactiongroup.cpp:247-257 setExclusive：便捷映射到策略。 */
    if (!self) return;
    self->m_exclusionPolicy = exclusive
        ? XActionGroupExclusionPolicy_Exclusive
        : XActionGroupExclusionPolicy_None;
}

XActionGroupExclusionPolicy XActionGroup_exclusionPolicy(
    const XActionGroup* self)
{
    return self ? (XActionGroupExclusionPolicy)self->m_exclusionPolicy
                : XActionGroupExclusionPolicy_None;
}

void XActionGroup_setExclusionPolicy(XActionGroup* self,
                                     XActionGroupExclusionPolicy policy)
{
    /* 对标 qactiongroup.cpp:239-245 setExclusionPolicy：直接记录策略，
       已有成员的选中态不回溯重排（Qt 亦然）。 */
    if (!self) return;
    if (policy != XActionGroupExclusionPolicy_None &&
        policy != XActionGroupExclusionPolicy_Exclusive &&
        policy != XActionGroupExclusionPolicy_ExclusiveOptional)
        return; /* 越界枚举防御（位域只承载 0..2）。 */
    self->m_exclusionPolicy = (uint8_t)policy;
}

bool XActionGroup_isEnabled(const XActionGroup* self)
{
    return self ? self->m_enabled : false;
}

void XActionGroup_setEnabled(XActionGroup* self, bool enabled)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    /* 对标 QActionGroup::setEnabled（qactiongroup.cpp:276-283）：Qt 6.8
     * 这里**没有**同值早退，而是无条件把 d->enabled 转发给全部成员；
     * 成员侧经 QActionPrivate::setEnabled(b, byGroup=true) 裁决：
     * 组禁用一律生效；组启用时显式设置过启用态的成员按显式值裁决
     * （qaction.cpp:940-951 `if (b && byGroup && explicitEnabled)
     * b = explicitEnabledValue;`）。本库同口径：走
     * XAction_setEnabledByGroup（不写显式位），不再用 XAction_setEnabled
     * 把组状态误记为成员的显式设置。 */
    self->m_enabled = enabled;
    if (!self->m_actions) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item)
            XAction_setEnabledByGroup(*item, enabled);
    }
}

void XActionGroup_setDisabled(XActionGroup* self, bool disabled)
{
    /* 对标 qactiongroup.h:76 `inline void setDisabled(bool b)
       { setEnabled(!b); }`：内联便捷形态，语义即取反后转 setEnabled。 */
    XActionGroup_setEnabled(self, !disabled);
}

bool XActionGroup_isVisible(const XActionGroup* self)
{
    return self ? self->m_visible : false;
}

void XActionGroup_setVisible(XActionGroup* self, bool visible)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    self->m_visible = visible;
    /* 对标 qactiongroup.cpp:288-300 setVisible：成员动作跟随组可见态
       （Qt 的 forceInvisible 豁免为控件内嵌动作概念，本库无对应）。 */
    if (!self->m_actions) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item)
            XAction_setVisible(*item, visible);
    }
}

/* ==================== 信号 ==================== */

void* XActionGroup_triggered_signal(XActionGroup* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XActionGroup_triggered_signal;
}

void* XActionGroup_hovered_signal(XActionGroup* self, XAction* action)
{
    (void)self;
    (void)action;
    return (void*)(size_t)XActionGroup_hovered_signal;
}

#endif /* XWIDGET_ON && XACTION_ON */

#endif /* XGUI_ON */
