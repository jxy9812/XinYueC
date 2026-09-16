/**
 * @file       XActionGroup.c
 * @brief      动作逻辑分组实现（对标 Qt 6.8 QActionGroup 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；成员动作借用不拥有，
 *             每个成员挂一个内信号桥转发 triggered/hovered，并处理
 *             动作先销毁、互斥选中与 enabled 转发。
 * @author     XinYueC 团队
 */

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

/** @brief 互斥：选中某动作时取消其它成员选中（exclusive 语义）。 */
static void xactiongroup_applyExclusive(XActionGroup* self,
                                        XAction* checked)
{
    int64_t i;
    int64_t n;
    if (!self->m_exclusive || !self->m_actions) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item && *item != checked &&
            XAction_isChecked(*item))
            XAction_setChecked(*item, false);
    }
}

/** @brief 扫描成员返回第一个选中动作；无选中返回 NULL。 */
static XAction* xactiongroup_scanChecked(const XActionGroup* self)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_actions) return NULL;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item && XAction_isChecked(*item))
            return *item;
    }
    return NULL;
}

/* ==================== 成员动作桥接（C 语言的 per-action 闭包等价物） ==================== */

XCLASS_DEFINE_BEGING(XActionGroupBridge)
XCLASS_DEFINE_EXTEND_END(XActionGroupBridge, XObject)

typedef struct XActionGroupBridge
{
    XObject     m_base;      /**< 基类成员；必须是第一个。 */
    XActionGroup* m_group;   /**< 所属组（借用）。 */
    XAction*    m_action;    /**< 桥接的成员动作（借用）。 */
    bool        m_actionDead;/**< 动作已销毁（跳过断连）。 */
} XActionGroupBridge;

static XVtable* XActionGroupBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XActionGroupBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static void xactiongroup_bridgeTriggeredSlot(XObject* receiver,
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

static void xactiongroup_bridgeDestroy(XActionGroupBridge* bridge)
{
    if (!bridge) return;
    if (bridge->m_actionDead) {
        /* 动作已销毁：其连接表随之消亡，直接删除桥。 */
        XClass_delete_base((XClass*)bridge);
        return;
    }
    XObject_disconnect_1((XObject*)bridge->m_action,
                         XSignal(XAction_triggered_signal),
                         (XObject*)bridge,
                         xactiongroup_bridgeTriggeredSlot);
    XObject_disconnect_1((XObject*)bridge->m_action,
                         XSignal(XAction_hovered_signal),
                         (XObject*)bridge,
                         xactiongroup_bridgeHoveredSlot);
    XObject_disconnect_1((XObject*)bridge->m_action,
                         XSignal(XObject_destroyed_signal),
                         (XObject*)bridge,
                         xactiongroup_bridgeDestroyedSlot);
    XClass_delete_base((XClass*)bridge);
}

/** @brief triggered 转发：互斥维护 + 发射组 triggered(action)。 */
static void xactiongroup_bridgeTriggeredSlot(XObject* receiver,
                                             XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    if (!bridge || !bridge->m_group || !bridge->m_action || !args) return;
    self = bridge->m_group;
    action = bridge->m_action;
    XVarList_args_1(args, bool, checked);
    if (checked) {
        xactiongroup_applyExclusive(self, action);
        self->m_checkedAction = action;
    } else if (self->m_checkedAction == action) {
        self->m_checkedAction = NULL;
    }
    xactiongroup_emitAction(self, (size_t)XActionGroup_triggered_signal,
                            action);
}

/** @brief hovered 转发。 */
static void xactiongroup_bridgeHoveredSlot(XObject* receiver,
                                           XVarList* args)
{
    XActionGroupBridge* bridge = (XActionGroupBridge*)receiver;
    XActionGroup* self;
    XAction* action;
    if (!bridge || !bridge->m_group || !bridge->m_action || !args) return;
    self = bridge->m_group;
    action = bridge->m_action;
    xactiongroup_emitAction(self, (size_t)XActionGroup_hovered_signal,
                            action);
}

/** @brief 动作 destroyed 转发：动作先于组销毁时自动移除成员并删桥。 */
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
    self = bridge->m_group;
    bridge->m_actionDead = true;
    action = bridge->m_action;
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
        XClass_delete_base((XClass*)bridge);
    }
}

/** @brief 为成员动作创建桥并连接三类信号。 */
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
    return bridge;
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_actionGroup_deinit(XActionGroup* self)
{
    if (!self) return;
    if (self->m_actions) {
        XVector_delete_base((XClass*)self->m_actions);
        self->m_actions = NULL;
    }
    if (self->m_bridges) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_bridges);
        for (i = 0; i < n; ++i) {
            XActionGroupBridge** bp =
                (XActionGroupBridge**)XVector_at_base(self->m_bridges, i);
            if (bp && *bp)
                xactiongroup_bridgeDestroy(*bp);
        }
        XVector_delete_base((XClass*)self->m_bridges);
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
    self->m_exclusive = true;
    self->m_enabled = true;
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

void XActionGroup_addAction(XActionGroup* self, XAction* action)
{
    if (!self || !action ||
        xactiongroup_indexOf(self, action) >= 0) return;
    if (!self->m_actions || !self->m_bridges) return;
    XVector_push_back_1_base(self->m_actions, &action);
    {
        XActionGroupBridge* bridge =
            xactiongroup_connectAction(self, action);
        XVector_push_back_1_base(self->m_bridges, &bridge);
    }
    if (XAction_isChecked(action)) {
        xactiongroup_applyExclusive(self, action);
        self->m_checkedAction = action;
    }
    if (!self->m_enabled)
        XAction_setEnabled(action, false);
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
            xactiongroup_bridgeDestroy(*bp);
    }
    XVector_remove_base(self->m_bridges, index, 1);
    XVector_remove_base(self->m_actions, index, 1);
    if (self->m_checkedAction == action)
        self->m_checkedAction = NULL;
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
    return xactiongroup_scanChecked(self);
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
    return self ? self->m_exclusive : false;
}

void XActionGroup_setExclusive(XActionGroup* self, bool exclusive)
{
    if (!self) return;
    self->m_exclusive = exclusive;
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
    self->m_enabled = enabled;
    if (!self->m_actions) return;
    n = XVector_size_base((const XContainer*)self->m_actions);
    for (i = 0; i < n; ++i) {
        XAction** item =
            (XAction**)XVector_at_base(self->m_actions, i);
        if (item && *item)
            XAction_setEnabled(*item, enabled);
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
