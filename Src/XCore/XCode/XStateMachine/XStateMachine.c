#include "XStateMachine.h"

#include "XCoreApplication.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XStateMachine_p.h"
#include "XStateMachine_Protected.h"

/** @brief 延迟事件的内部记录。仅供本实现文件使用，不进入公开头文件。 */
typedef struct XStateMachine_DelayedEvent {
    int m_id;              ///< 对外返回的延迟事件编号。
    XTimerId m_timerId;    ///< XObject 定时器编号；零延迟时为 XTIMER_INVALID_ID。
    XEvent* m_event;       ///< 到期后进入外部队列的事件。
} XStateMachine_DelayedEvent;

typedef struct XStateMachine_SignalConnection {
    XObject m_class;
    XStateMachine* m_machine;
    const XObject* m_sender;
    size_t m_signal;
    XConnection* m_connection;
    size_t m_referenceCount;
} XStateMachine_SignalConnection;

/**
 * @brief 进入状态与动画的一条绑定（对标 QStateMachinePrivate::animationsForState）。
 * @details 动画对象不转移所有权；记录以 XObject 身份充当 finished 信号的
 *          接收者。m_spent 防"自然结束槽"与"退出路径终止"双路回收。
 */
typedef struct XStateMachine_AnimationBinding {
    XObject m_class;
    XStateMachine* m_machine;        ///< 所属状态机，不取得所有权。
    XAbstractState* m_state;         ///< 绑定的进入状态，不取得所有权。
    XAbstractAnimation* m_animation; ///< 绑定的动画，借用不取得所有权。
    XConnection* m_connection;       ///< finished 信号连接。
    bool m_spent;                    ///< 已被任一路径回收标记（防双删）。
} XStateMachine_AnimationBinding;

/**
 * @brief 一条属性恢复记录（对标 QStateMachinePrivate::RestorableId + 保存值）。
 * @details 元素由 computePendingRestorables_internal 产出、由调用方释放。
 */
typedef struct XStateMachine_Restorable {
    XObject* m_object;       ///< 属性所属对象，不转移所有权。
    char* m_propertyName; ///< 属性名 UTF-8 文本，由记录拥有。
    XVariant* m_value;       ///< 需要恢复的旧值，由记录拥有。
} XStateMachine_Restorable;

/**
 * @brief 一条恢复表项：某对象某属性在进入某状态前的旧值。
 * @note 对标 QStateMachinePrivate::registeredRestorablesForState 的
 *       QHash&lt;RestorableId, QVariant&gt; 单条记录。
 */
typedef struct XStateMachine_RestorableEntry {
    XAbstractState* m_state; ///< 归属状态（登记时的进入状态），不取得所有权。
    XObject* m_object;       ///< 属性所属对象，不转移所有权。
    char* m_propertyName; ///< 属性名 UTF-8 文本，由表项拥有。
    XVariant* m_value;       ///< 进入该状态前的旧值，由表项拥有。
} XStateMachine_RestorableEntry;

static XEventType g_processEventType = XEVENT_TYPE_NONE;
static XEventType g_zeroDelayedEventType = XEVENT_TYPE_NONE;
static XEventType g_signalEventType = XEVENT_TYPE_NONE;
static XEventType g_wrappedEventType = XEVENT_TYPE_NONE;

static bool VXStateMachine_event(XStateMachine* machine, XEvent* event);
static bool VXStateMachine_eventFilter(XStateMachine* machine, XObject* watched, XEvent* event);
static void VXStateMachine_timerEvent(XStateMachine* machine, XTimerEvent* event);
static void VXStateMachine_deinit(XStateMachine* machine);
static void VXStateMachine_hook(XStateMachine* machine, XEvent* event);
static void VXStateMachine_onEntry(XAbstractState* state, XEvent* event);
static void VXStateMachine_onExit(XAbstractState* state, XEvent* event);

static void XStateMachine_startInternal(XStateMachine* machine);
static void XStateMachine_process(XStateMachine* machine);
static bool XStateMachine_microstep(XStateMachine* machine, XEvent* event, XVector* transitions);
static void XStateMachine_terminateStateAnimations(XStateMachine* machine,
                                                   XAbstractState* state, bool deferred);

static bool XStateMachine_vectorContains(const XVector* vector, const void* pointer)
{
    return vector && XVector_indexOf(vector, &pointer, 0) >= 0;
}

static void XStateMachine_signalSlot(XObject* receiver, XVarList* arguments)
{
    XStateMachine_SignalConnection* record = (XStateMachine_SignalConnection*)receiver;
    if (!record || !record->m_machine || !record->m_sender || !record->m_signal)
        return;
    XStateMachine_SignalEvent* event = XStateMachine_SignalEvent_create(
        (XObject*)record->m_sender, record->m_signal, arguments);
    if (event)
        XStateMachine_postInternalEvent_internal(record->m_machine, (XEvent*)event);
}

static void XStateMachine_clearSignalConnections(XStateMachine* machine)
{
    if (!machine || !machine->m_signalConnections)
        return;
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_signalConnections); ++i) {
        XStateMachine_SignalConnection* record = XVector_At_Base(
            machine->m_signalConnections, i, XStateMachine_SignalConnection*);
        if (!record)
            continue;
        if (record->m_connection) {
            XObject_disconnect_1(
                (XObject*)record->m_sender, record->m_signal,
                (XObject*)record, XStateMachine_signalSlot);
        }
        record->m_connection = NULL;
        XClassDelete((XClass*)record);
    }
    XVector_clear_base((XContainer*)machine->m_signalConnections);
}

static bool XStateMachine_vectorAppendUnique(XVector* vector, void* pointer)
{
    if (!vector || !pointer)
        return false;
    if (XStateMachine_vectorContains(vector, pointer))
        return true;
    return XVector_push_back_1_base(vector, &pointer);
}

static void XStateMachine_vectorRemovePointer(XVector* vector, const void* pointer)
{
    if (!vector || !pointer)
        return;
    int64_t index = XVector_indexOf(vector, &pointer, 0);
    if (index >= 0)
        XVector_removeAt_base(vector, index);
}

static bool XStateMachine_isDescendant(const XAbstractState* state, const XAbstractState* ancestor)
{
    if (!state || !ancestor || state == ancestor)
        return false;
    for (XState* parent = state->m_parentState; parent; parent = parent->m_class.m_parentState) {
        if ((XAbstractState*)parent == ancestor)
            return true;
    }
    return false;
}

static bool XStateMachine_isGroup(const XAbstractState* state)
{
    return state && (state->m_kind == XAbstractState_StandardState
                     || state->m_kind == XAbstractState_StateMachine);
}

static bool XStateMachine_hasRealChildren(const XState* state)
{
    const XVector* children = XState_childStates_const_internal(state);
    return children && !XVector_isEmpty_base((const XContainer*)children);
}

static bool XStateMachine_hasActiveChild(const XStateMachine* machine, const XState* state)
{
    for (int64_t i = 0; machine && machine->m_configuration
         && i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* candidate = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        if (candidate && candidate->m_parentState == state)
            return true;
    }
    return false;
}

static bool XStateMachine_isAtomic(const XStateMachine* machine, const XAbstractState* state)
{
    if (!state || state->m_kind == XAbstractState_HistoryState)
        return false;
    if (!XStateMachine_isGroup(state))
        return true;
    if (state->m_kind == XAbstractState_StateMachine && state != (const XAbstractState*)machine)
        return true;
    return !XStateMachine_hasActiveChild(machine, (const XState*)state);
}

/* ==================== 进入/退出排序（对标 Qt 的三个 LessThan 比较器） ==================== */

/**
 * @brief 找到 first 与 second 的最近公共祖先（对标 findLCA，非 onlyCompound）。
 * @details 沿 first 的真祖先链（含状态机根）找第一个同时是 second 祖先的状态；
 *          找不到时按 Qt 的回退语义返回状态机根。
 */
static XAbstractState* XStateMachine_findLCA(const XStateMachine* machine,
                                             const XAbstractState* first,
                                             const XAbstractState* second)
{
    for (XState* ancestor = first->m_parentState; ancestor;
         ancestor = ancestor->m_class.m_parentState) {
        if (XStateMachine_isDescendant(second, (XAbstractState*)ancestor))
            return (XAbstractState*)ancestor;
    }
    return (XAbstractState*)machine;
}

/** @brief 返回 group 的直接子状态中包含 descendant 的那个的下标（对标 indexOfDescendant）。 */
static int64_t XStateMachine_indexOfDescendant(const XState* group,
                                               const XAbstractState* descendant)
{
    const XVector* children = XState_childStates_const_internal(group);
    for (int64_t i = 0;
         children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (child && (child == descendant || XStateMachine_isDescendant(descendant, child)))
            return i;
    }
    return -1;
}

/** @brief descendant 到 ancestor 的层数（对标 descendantDepth，ancestor 层不计）。 */
static int XStateMachine_descendantDepth(const XAbstractState* descendant,
                                         const XAbstractState* ancestor)
{
    int depth = 0;
    for (const XAbstractState* current = descendant; current && current != ancestor;
         current = (const XAbstractState*)current->m_parentState) {
        ++depth;
    }
    return depth;
}

/** @brief 进入序比较（对标 stateEntryLessThan）：浅者优先、同父按子序、跨分支按 LCA 下子序。 */
static bool XStateMachine_stateEntryLessThan(const XAbstractState* first,
                                             const XAbstractState* second)
{
    if (first == second)
        return false;
    if (first->m_parentState == second->m_parentState && first->m_parentState) {
        const XVector* children = XState_childStates_const_internal(first->m_parentState);
        const void* keyFirst = first;
        const void* keySecond = second;
        return XVector_indexOf(children, &keyFirst, 0) < XVector_indexOf(children, &keySecond, 0);
    }
    if (XStateMachine_isDescendant(first, second))
        return false;
    if (XStateMachine_isDescendant(second, first))
        return true;
    XAbstractState* lca = XStateMachine_findLCA(NULL, first, second);
    return XStateMachine_indexOfDescendant((XState*)lca, first)
        < XStateMachine_indexOfDescendant((XState*)lca, second);
}

/** @brief 退出序比较（对标 stateExitLessThan）：深者优先、同父按子序倒排。 */
static bool XStateMachine_stateExitLessThan(const XAbstractState* first,
                                            const XAbstractState* second)
{
    if (first == second)
        return false;
    if (first->m_parentState == second->m_parentState && first->m_parentState) {
        const XVector* children = XState_childStates_const_internal(first->m_parentState);
        const void* keyFirst = first;
        const void* keySecond = second;
        return XVector_indexOf(children, &keySecond, 0) < XVector_indexOf(children, &keyFirst, 0);
    }
    if (XStateMachine_isDescendant(first, second))
        return true;
    if (XStateMachine_isDescendant(second, first))
        return false;
    XAbstractState* lca = XStateMachine_findLCA(NULL, first, second);
    return XStateMachine_indexOfDescendant((XState*)lca, second)
        < XStateMachine_indexOfDescendant((XState*)lca, first);
}

/**
 * @brief 转换源状态进入序比较（对标 transitionStateEntryLessThan）：
 *        源深者优先；同深度按 LCA 下子序。
 */
static bool XStateMachine_transitionEntryLessThan(const XAbstractTransition* first,
                                                  const XAbstractTransition* second)
{
    if (first == second)
        return false;
    const XState* sourceFirst = first ? first->m_sourceState : NULL;
    const XState* sourceSecond = second ? second->m_sourceState : NULL;
    if (sourceFirst == sourceSecond)
        return false;
    if (!sourceFirst || !sourceSecond)
        return sourceSecond == NULL;
    if (XStateMachine_isDescendant((const XAbstractState*)sourceFirst,
                                   (const XAbstractState*)sourceSecond))
        return true;
    if (XStateMachine_isDescendant((const XAbstractState*)sourceSecond,
                                   (const XAbstractState*)sourceFirst))
        return false;
    XAbstractState* lca = XStateMachine_findLCA(NULL, (const XAbstractState*)sourceFirst,
                                                (const XAbstractState*)sourceSecond);
    const int depthFirst = XStateMachine_descendantDepth((const XAbstractState*)sourceFirst, lca);
    const int depthSecond
        = XStateMachine_descendantDepth((const XAbstractState*)sourceSecond, lca);
    if (depthFirst == depthSecond)
        return XStateMachine_indexOfDescendant((XState*)lca, (const XAbstractState*)sourceFirst)
            < XStateMachine_indexOfDescendant((XState*)lca, (const XAbstractState*)sourceSecond);
    return depthFirst > depthSecond;
}

static void XStateMachine_sortStatesBy(XVector* states,
                                       bool (*less)(const XAbstractState*, const XAbstractState*))
{
    if (!states || !less)
        return;
    for (int64_t i = 1; i < (int64_t)XVector_size_base((const XContainer*)states); ++i) {
        XAbstractState* key = XVector_At_Base(states, i, XAbstractState*);
        int64_t j = i - 1;
        while (j >= 0 && less(key, XVector_At_Base(states, j, XAbstractState*))) {
            XVector_At_Base(states, j + 1, XAbstractState*) = XVector_At_Base(states, j, XAbstractState*);
            --j;
        }
        XVector_At_Base(states, j + 1, XAbstractState*) = key;
    }
}

static void XStateMachine_sortTransitionsBy(XVector* transitions,
                                            bool (*less)(const XAbstractTransition*,
                                                         const XAbstractTransition*))
{
    if (!transitions || !less)
        return;
    for (int64_t i = 1; i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* key = XVector_At_Base(transitions, i, XAbstractTransition*);
        int64_t j = i - 1;
        while (j >= 0 && less(key, XVector_At_Base(transitions, j, XAbstractTransition*))) {
            XVector_At_Base(transitions, j + 1, XAbstractTransition*)
                = XVector_At_Base(transitions, j, XAbstractTransition*);
            --j;
        }
        XVector_At_Base(transitions, j + 1, XAbstractTransition*) = key;
    }
}

static void XStateMachine_clearOwnedEvents(XVector* queue)
{
    if (!queue)
        return;
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)queue); ++i) {
        XEvent* event = XVector_At_Base(queue, i, XEvent*);
        if (event)
            XClassDelete((XClass*)event);
    }
    XVector_clear_base((XContainer*)queue);
}

static XEvent* XStateMachine_dequeue(XVector* queue)
{
    if (!queue || XVector_isEmpty_base((const XContainer*)queue))
        return NULL;
    XEvent* event = XVector_At_Base(queue, 0, XEvent*);
    XVector_removeAt_base(queue, 0);
    return event;
}

static void XStateMachine_cancelAllDelayedEvents(XStateMachine* machine)
{
    if (!machine || !machine->m_delayedEvents)
        return;
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_delayedEvents); ++i) {
        XStateMachine_DelayedEvent* delayed = XVector_at_base(machine->m_delayedEvents, i);
        if (delayed->m_timerId != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)machine, delayed->m_timerId);
        if (delayed->m_event)
            XClassDelete((XClass*)delayed->m_event);
    }
    XVector_clear_base((XContainer*)machine->m_delayedEvents);
}

static void XStateMachine_activateZeroDelayedEvents(XStateMachine* machine)
{
    if (!machine || !machine->m_delayedEvents)
        return;
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)machine->m_delayedEvents);) {
        XStateMachine_DelayedEvent delayed = XVector_At_Base(
            machine->m_delayedEvents, i, XStateMachine_DelayedEvent);
        if (delayed.m_timerId != XTIMER_INVALID_ID) {
            ++i;
            continue;
        }

        XVector_removeAt_base(machine->m_delayedEvents, i);
        if (machine->m_state != XStateMachine_Running
            || !XVector_push_back_1_base(
                machine->m_externalEventQueue, &delayed.m_event)) {
            XClassDelete((XClass*)delayed.m_event);
        }
    }
}

static void XStateMachine_clearHistoryRecursive(XState* state)
{
    const XVector* children = XState_childStates_const_internal(state);
    for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (!child)
            continue;
        if (child->m_kind == XAbstractState_HistoryState) {
            XHistoryState* history = (XHistoryState*)child;
            XVector_clear_base((XContainer*)history->m_configuration);
        } else if (XStateMachine_isGroup(child)) {
            XStateMachine_clearHistoryRecursive((XState*)child);
        }
    }
}

/* ==================== 属性赋值与恢复（对标 Qt 的 assignProperty 机制） ==================== */

/**
 * @brief 深拷贝属性名（XString_create_copy 与源共享底层缓冲，源释放后不可依赖）。
 */
static char* XStateMachine_copyUtf8(const char* text)
{
    return XStrdup(text);
}

/** @brief 按 UTF-8 文本比较属性名。 */
static bool XStateMachine_samePropertyName(const char* left, const char* right)
{
    if (!left || !right)
        return false;
    return XStrcmp(left, right) == 0;
}

/** @brief 释放属性名文本。 */
static void XStateMachine_freePropertyName(char* name)
{
    if (name)
        XMemory_free(name, XCLASS_DEFAULT_MEMORY_TYPE);
}

/**
 * @brief 查找某状态的恢复记录（对标 QStateMachinePrivate::hasRestorable）。
 * @param machine 状态机。
 * @param state 目标状态。
 * @param object 属性所属对象。
 * @param name 属性名。
 * @return 表项指针；未登记时返回 NULL。
 */
static XStateMachine_RestorableEntry* XStateMachine_findRestorable(
    XStateMachine* machine, XAbstractState* state, XObject* object,
    const char* name)
{
    if (!machine || !machine->m_restorables || !state || !object || !name)
        return NULL;
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)machine->m_restorables); ++i) {
        XStateMachine_RestorableEntry* entry = XVector_at_base(machine->m_restorables, i);
        if (entry->m_state == state && entry->m_object == object
            && XStateMachine_samePropertyName(entry->m_propertyName, name)) {
            return entry;
        }
    }
    return NULL;
}

void XStateMachine_registerRestorable_internal(XStateMachine* machine, XAbstractState* state,
                                               XObject* object, const char* propertyName,
                                               const XVariant* value)
{
    if (!machine || !machine->m_restorables || !state || !object || !propertyName)
        return;
    if (XStateMachine_findRestorable(machine, state, object, propertyName))
        return; /* 同一状态同一属性只保存第一份旧值（对标 Qt 的 contains 判断）。 */

    XStateMachine_RestorableEntry entry;
    entry.m_state = state;
    entry.m_object = object;
    entry.m_propertyName = XStateMachine_copyUtf8(propertyName);
    entry.m_value = value ? XVariant_create_copy(value) : NULL;
    if (!entry.m_propertyName || (value && !entry.m_value)
        || !XVector_push_back_1_base(machine->m_restorables, &entry)) {
        if (entry.m_propertyName)
            XClassDelete((XClass*)entry.m_propertyName);
        if (entry.m_value)
            XClassDelete((XClass*)entry.m_value);
    }
}

void XStateMachine_unregisterRestorables_internal(XStateMachine* machine, const XVector* states,
                                                  XObject* object, const char* propertyName)
{
    if (!machine || !machine->m_restorables || !states || !object || !propertyName)
        return;
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)machine->m_restorables) - 1;
         i >= 0; --i) {
        XStateMachine_RestorableEntry* entry = XVector_at_base(machine->m_restorables, i);
        if (entry->m_object != object
            || !XStateMachine_samePropertyName(entry->m_propertyName, propertyName)) {
            continue;
        }
        bool listed = false;
        for (int64_t j = 0; j < (int64_t)XVector_size_base((const XContainer*)states); ++j) {
            if (XVector_At_Base(states, j, XAbstractState*) == entry->m_state) {
                listed = true;
                break;
            }
        }
        if (!listed)
            continue;
        if (entry->m_propertyName)
            XStateMachine_freePropertyName(entry->m_propertyName);
        if (entry->m_value)
            XClassDelete((XClass*)entry->m_value);
        XVector_removeAt_base(machine->m_restorables, i);
    }
}

void XStateMachine_dropRestorablesForState_internal(XStateMachine* machine, XAbstractState* state)
{
    if (!machine || !machine->m_restorables || !state)
        return;
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)machine->m_restorables) - 1;
         i >= 0; --i) {
        XStateMachine_RestorableEntry* entry = XVector_at_base(machine->m_restorables, i);
        if (entry->m_state != state)
            continue;
        if (entry->m_propertyName)
            XStateMachine_freePropertyName(entry->m_propertyName);
        if (entry->m_value)
            XClassDelete((XClass*)entry->m_value);
        XVector_removeAt_base(machine->m_restorables, i);
    }
}

/**
 * @brief 收集退出集合需要恢复的旧值（对标 computePendingRestorables）。
 * @details 从最外层被退出的状态往回找：同一 (对象, 属性) 只保留最外层登记的旧值。
 * @return 元素类型为 XStateMachine_Restorable 的容器，调用方用
 *         XStateMachine_restorablesRelease_internal 释放。
 */
XVector* XStateMachine_computePendingRestorables_internal(const XStateMachine* machine,
                                                          const XVector* statesToExit)
{
    XVector* restorables = XVector_Create(XStateMachine_Restorable);
    if (!restorables || !machine || !machine->m_restorables || !statesToExit)
        return restorables;

    const int64_t exitCount = (int64_t)XVector_size_base((const XContainer*)statesToExit);
    for (int64_t i = exitCount - 1; i >= 0; --i) {
        XAbstractState* state = XVector_At_Base(statesToExit, i, XAbstractState*);
        for (int64_t j = 0;
             j < (int64_t)XVector_size_base((const XContainer*)machine->m_restorables); ++j) {
            XStateMachine_RestorableEntry* entry = XVector_at_base(machine->m_restorables, j);
            if (entry->m_state != state)
                continue;
            bool exists = false;
            for (int64_t k = 0;
                 k < (int64_t)XVector_size_base((const XContainer*)restorables); ++k) {
                XStateMachine_Restorable* candidate = XVector_at_base(restorables, k);
                if (candidate->m_object == entry->m_object
                    && XStateMachine_samePropertyName(candidate->m_propertyName,
                                                     entry->m_propertyName)) {
                    exists = true;
                    break;
                }
            }
            if (exists)
                continue;
            XStateMachine_Restorable restorable;
            restorable.m_object = entry->m_object;
            restorable.m_propertyName = XStateMachine_copyUtf8(entry->m_propertyName);
            restorable.m_value = entry->m_value ? XVariant_create_copy(entry->m_value) : NULL;
            if (!restorable.m_propertyName
                || (entry->m_value && !restorable.m_value)
                || !XVector_push_back_1_base(restorables, &restorable)) {
                if (restorable.m_propertyName)
                    XStateMachine_freePropertyName(restorable.m_propertyName);
                if (restorable.m_value)
                    XClassDelete((XClass*)restorable.m_value);
            }
        }
    }
    return restorables;
}

void XStateMachine_restorablesRelease_internal(XVector* restorables)
{
    if (!restorables)
        return;
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)restorables); ++i) {
        XStateMachine_Restorable* restorable = XVector_at_base(restorables, i);
        if (restorable->m_propertyName)
            XStateMachine_freePropertyName(restorable->m_propertyName);
        if (restorable->m_value)
            XClassDelete((XClass*)restorable->m_value);
    }
    XClassDelete((XClass*)restorables);
}

/**
 * @brief 把一条属性赋值写进对象（对标 QPropertyAssignment::write）。
 * @param object 目标对象。
 * @param name 属性名 UTF-8 文本。
 * @param value 赋值；内部会被 XObject_setProperty 接管所有权。
 * @return 写入成功返回 true；失败时 value 所有权仍在调用方。
 */
static bool XStateMachine_writeProperty(XObject* object, const char* name, XVariant* value)
{
    if (!object || !name)
        return false;
    XString* key = XString_create_utf8(name);
    if (!key)
        return false;
    bool ok = XObject_setProperty(object, key, value);
    XClassDelete((XClass*)key);
    return ok;
}

/**
 * @brief 取出某状态登记的属性赋值（对标 QStatePrivate::propertyAssignments）。
 * @param state 目标状态。
 * @param index 记录下标。
 * @return 记录指针；越界或 state 非标准状态时返回 NULL。
 */
static XState_PropertyAssignment* XStateMachine_propertyAssignmentAt(
    const XAbstractState* state, int64_t index)
{
    if (!state || (state->m_kind != XAbstractState_StandardState
                   && state->m_kind != XAbstractState_StateMachine)) {
        return NULL;
    }
    const XVector* assignments = XState_propertyAssignments_const((const XState*)state);
    if (!assignments || index < 0
        || index >= (int64_t)XVector_size_base((const XContainer*)assignments)) {
        return NULL;
    }
    return XVector_At_Base(assignments, index, XState_PropertyAssignment*);
}

/**
 * @brief 进入状态时执行属性赋值，并按全局恢复策略登记旧值。
 * @details 对标 QStateMachinePrivate::enterStates 中"立即设置未动画属性"的整段逻辑。
 * @param machine 状态机。
 * @param state 即将进入的状态。
 * @param exitedStates 本次退出的状态集合（用于把旧值的来源交给新状态）。
 */
static void XStateMachine_applyPropertyAssignments(XStateMachine* machine,
                                                   XAbstractState* state,
                                                   const XVector* exitedStates)
{
    if (!machine || !state)
        return;
    /* 只有 XState 与状态机根节点带属性赋值表；最终/历史状态没有该成员，
     * 直接读会把后续成员当成容器指针（曾在此踩到段错误）。 */
    if (state->m_kind != XAbstractState_StandardState
        && state->m_kind != XAbstractState_StateMachine) {
        return;
    }
    const XVector* assignments = XState_propertyAssignments_const((const XState*)state);
    if (!assignments)
        return;

    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)assignments); ++i) {
        XState_PropertyAssignment* assignment = XStateMachine_propertyAssignmentAt(state, i);
        if (!assignment || !assignment->m_object || !assignment->m_propertyName)
            continue;

        if (machine->m_globalRestorePolicy == XState_RestoreProperties) {
            if (!XStateMachine_findRestorable(machine, state, assignment->m_object,
                                              assignment->m_propertyName)) {
                /* 旧值优先取更外层被退出状态登记的记录，否则取对象当前值。 */
                XVariant* saved = NULL;
                for (int64_t j = (int64_t)XVector_size_base((const XContainer*)exitedStates) - 1;
                     j >= 0 && !saved; --j) {
                    XAbstractState* exited = XVector_At_Base(exitedStates, j, XAbstractState*);
                    XStateMachine_RestorableEntry* entry = XStateMachine_findRestorable(
                        machine, exited, assignment->m_object, assignment->m_propertyName);
                    if (entry && entry->m_value)
                        saved = XVariant_create_copy(entry->m_value);
                }
                if (!saved) {
                    XString* key = XString_create_utf8(assignment->m_propertyName);
                    XVariant* current = key
                        ? XObject_property(assignment->m_object, key)
                        : NULL;
                    saved = current ? XVariant_create_copy(current) : NULL;
                    if (key)
                        XClassDelete((XClass*)key);
                }
                XStateMachine_registerRestorable_internal(
                    machine, state, assignment->m_object,
                    assignment->m_propertyName, saved);
                if (saved)
                    XClassDelete((XClass*)saved);
                /* 被新状态接管的旧值记录从已退出状态上移除，避免陈旧副本复活。 */
                XStateMachine_unregisterRestorables_internal(
                    machine, exitedStates, assignment->m_object,
                    assignment->m_propertyName);
            }
        }

        XVariant* value = assignment->m_value
            ? XVariant_create_copy(assignment->m_value)
            : NULL;
        if (!XStateMachine_writeProperty(assignment->m_object,
                                         assignment->m_propertyName, value)) {
            if (value)
                XClassDelete((XClass*)value);
        }
    }
}

/**
 * @brief 把需要恢复的旧值写回对象（对标 restorablesToPropertyList 的写入阶段）。
 * @param machine 状态机。
 * @param pending 由 computePendingRestorables_internal 产出的待恢复集合。
 */
static void XStateMachine_restoreProperties(XStateMachine* machine, const XVector* pending)
{
    if (!machine || !pending)
        return;
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)pending); ++i) {
        XStateMachine_Restorable* restorable = XVector_at_base(pending, i);
        if (!restorable->m_value || !restorable->m_object || !restorable->m_propertyName)
            continue;
        XVariant* value = XVariant_create_copy(restorable->m_value);
        if (value && !XStateMachine_writeProperty(restorable->m_object,
                                                  restorable->m_propertyName, value)) {
            XClassDelete((XClass*)value);
        }
    }
}

static XAbstractState* XStateMachine_findErrorState(XAbstractState* context)
{
    for (XAbstractState* current = context; current; current = (XAbstractState*)current->m_parentState) {
        if (XStateMachine_isGroup(current)) {
            XAbstractState* errorState = XState_errorState((XState*)current);
            if (errorState && errorState != context)
                return errorState;
        }
    }
    return NULL;
}

static XAbstractState* XStateMachine_setError(XStateMachine* machine,
                                              XStateMachine_Error error,
                                              XAbstractState* context)
{
    if (!machine)
        return NULL;
    const char* errorText = NULL;
    machine->m_error = error;
    switch (error) {
    case XStateMachine_NoInitialStateError:
        errorText = "Missing initial state in compound state";
        break;
    case XStateMachine_NoDefaultStateInHistoryStateError:
        errorText = "Missing default state in history state";
        break;
    case XStateMachine_NoCommonAncestorForTransitionError:
        errorText = "No common ancestor for targets and source of transition from state";
        break;
    case XStateMachine_StateMachineChildModeSetToParallelError:
        errorText = "Child mode of state machine";
        break;
    default:
        errorText = "Unknown error";
        break;
    }
    /* 错误描述按 XString 主版本存储（对标 Qt QString errorString）；
     * 字面量仅在此转换为库内字符串，禁止以 char* 长期保存。
     * 对标 Qt 的 tr(...).arg(context->objectName())：四条模板与 Qt 6.8 逐字
     * 对齐；上下文名恒以 'name' 形式拼入（Qt 的 arg 对空 objectName 同样
     * 产出 ''）。ChildModeSetToParallelError 的上下文在 Qt 里是状态机本身
     * （findLCA 回退传 q_func()），故取 machine 的 objectName。 */
    XString* text = XString_create_utf8(errorText);
    if (text) {
        const XString* contextName = NULL;
        if (error == XStateMachine_StateMachineChildModeSetToParallelError) {
            contextName = XObject_objectName((const XObject*)machine);
        } else if (context) {
            contextName = XObject_objectName((const XObject*)context);
        }
        XString_append_utf8(text, " '");
        if (contextName)
            XString_append(text, contextName);
        if (error == XStateMachine_StateMachineChildModeSetToParallelError)
            XString_append_utf8(text, "' is not 'ExclusiveStates'.");
        else
            XString_append_utf8(text, "'");
        if (machine->m_errorString)
            XClassDelete((XClass*)machine->m_errorString);
        machine->m_errorString = text;
    }

    XAbstractState* errorState = XStateMachine_findErrorState(context);
    machine->m_pendingErrorState = errorState;
    if (!errorState)
        machine->m_stopRequested = true;
    return errorState;
}

static XVector* XStateMachine_effectiveTargets(XStateMachine* machine,
                                               XAbstractTransition* transition)
{
    XVector* targets = XVector_Create(XAbstractState*);
    if (!targets || !transition)
        return targets;

    const XVector* configuredTargets = XAbstractTransition_targetStates_const(transition);
    for (int64_t i = 0; configuredTargets && i < (int64_t)XVector_size_base((const XContainer*)configuredTargets); ++i) {
        XAbstractState* target = XVector_At_Base(configuredTargets, i, XAbstractState*);
        if (!target)
            continue;
        if (target->m_kind != XAbstractState_HistoryState) {
            XStateMachine_vectorAppendUnique(targets, target);
            continue;
        }

        XHistoryState* history = (XHistoryState*)target;
        if (history->m_configuration && !XVector_isEmpty_base((const XContainer*)history->m_configuration)) {
            for (int64_t j = 0; j < (int64_t)XVector_size_base((const XContainer*)history->m_configuration); ++j) {
                XAbstractState* saved = XVector_At_Base(history->m_configuration, j, XAbstractState*);
                XStateMachine_vectorAppendUnique(targets, saved);
            }
            continue;
        }

        /* 对标 Qt：历史无记录时取默认转换的目标集，但默认转换的 onTransition
         * 与 eventTest 都不会被调用（Qt 的 enterStates 历史分支实际不可达）。 */
        XAbstractTransition* fallback = XHistoryState_defaultTransition(history);
        const XVector* fallbackTargets = fallback
            ? XAbstractTransition_targetStates_const(fallback)
            : NULL;
        if (!fallback || !fallbackTargets || XVector_isEmpty_base((const XContainer*)fallbackTargets)) {
            XStateMachine_setError(machine,
                                   XStateMachine_NoDefaultStateInHistoryStateError,
                                   (XAbstractState*)history);
            continue;
        }
        for (int64_t j = 0; j < (int64_t)XVector_size_base((const XContainer*)fallbackTargets); ++j) {
            XAbstractState* saved = XVector_At_Base(fallbackTargets, j, XAbstractState*);
            XStateMachine_vectorAppendUnique(targets, saved);
        }
    }
    return targets;
}

static XAbstractState* XStateMachine_transitionDomain(XStateMachine* machine,
                                                      XAbstractTransition* transition,
                                                      const XVector* targets)
{
    if (!transition || !targets || XVector_isEmpty_base((const XContainer*)targets))
        return NULL;

    XAbstractState* source = (XAbstractState*)transition->m_sourceState;
    if (!source)
        return NULL;

    /* 对标 Qt isCompound：只有标准状态或根状态机是复合态；嵌套状态机
     * 一律按原子对待（isMachine && group != rootState() → false），
     * 因此嵌套机不能充当内转换域。 */
    if (transition->m_transitionType == XAbstractTransition_InternalTransition
        && XStateMachine_isGroup(source)
        && (source->m_kind != XAbstractState_StateMachine
            || source == (XAbstractState*)machine)
        && ((XState*)source)->m_childMode == XState_ExclusiveStates
        && XStateMachine_hasRealChildren((XState*)source)) {
        bool allDescendants = true;
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)targets); ++i) {
            XAbstractState* target = XVector_At_Base(targets, i, XAbstractState*);
            if (!XStateMachine_isDescendant(target, source)) {
                allDescendants = false;
                break;
            }
        }
        if (allDescendants)
            return source;
    }

    /* 对标 Qt findLCA：域候选恒为源状态的真祖先（getProperAncestors 从
     * parent 起步），嵌套机因此不能成为自己的转换域。唯一例外是根状态机
     * 自身作源（根没有父状态；Qt 该场景会误报 ChildModeSetToParallel 并
     * 停机，X 侧按"根即域"的增强语义直接取根，不报错）。 */
    XAbstractState* candidate = source == (XAbstractState*)machine
        ? source
        : (XAbstractState*)source->m_parentState;
    for (; candidate; candidate = (XAbstractState*)candidate->m_parentState) {
        bool containsAll = true;
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)targets); ++i) {
            XAbstractState* target = XVector_At_Base(targets, i, XAbstractState*);
            if (!XStateMachine_isDescendant(target, candidate)) {
                containsAll = false;
                break;
            }
        }
        if (containsAll) {
            if (candidate == (XAbstractState*)machine
                && machine->m_class.m_childMode == XState_ParallelStates) {
                XStateMachine_setError(
                    machine, XStateMachine_StateMachineChildModeSetToParallelError, source);
            }
            return candidate;
        }
    }

    XAbstractState* errorTarget = XStateMachine_setError(
        machine, XStateMachine_NoCommonAncestorForTransitionError, source);
    if (machine->m_class.m_childMode == XState_ParallelStates) {
        XStateMachine_setError(
            machine, XStateMachine_StateMachineChildModeSetToParallelError, source);
        return (XAbstractState*)machine;
    }
    if (!errorTarget)
        return NULL;
    for (XAbstractState* ancestor = (XAbstractState*)source->m_parentState;
         ancestor; ancestor = (XAbstractState*)ancestor->m_parentState) {
        if (XStateMachine_isDescendant(errorTarget, ancestor))
            return ancestor;
    }
    return (XAbstractState*)machine;
}

static XVector* XStateMachine_exitSetForTransition(XStateMachine* machine,
                                                   XAbstractTransition* transition)
{
    XVector* exitSet = XVector_Create(XAbstractState*);
    XVector* targets = XStateMachine_effectiveTargets(machine, transition);
    XAbstractState* domain = XStateMachine_transitionDomain(machine, transition, targets);
    if (domain) {
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
            XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
            if (XStateMachine_isDescendant(state, domain))
                XStateMachine_vectorAppendUnique(exitSet, state);
        }
    }
    if (targets)
        XClassDelete((XClass*)targets);
    return exitSet;
}

static bool XStateMachine_vectorsIntersect(const XVector* first, const XVector* second)
{
    for (int64_t i = 0; first && i < (int64_t)XVector_size_base((const XContainer*)first); ++i) {
        void* value = XVector_At_Base(first, i, void*);
        if (XStateMachine_vectorContains(second, value))
            return true;
    }
    return false;
}

static void XStateMachine_removeConflictingTransitions(XStateMachine* machine, XVector* transitions)
{
    if (!machine || !transitions)
        return;
    /* 对标 Qt：先按源状态进入序排序（深源优先），再做冲突消解。 */
    XStateMachine_sortTransitionsBy(transitions, XStateMachine_transitionEntryLessThan);
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* first = XVector_At_Base(transitions, i, XAbstractTransition*);
        XVector* firstExit = XStateMachine_exitSetForTransition(machine, first);
        for (int64_t j = i + 1; j < (int64_t)XVector_size_base((const XContainer*)transitions);) {
            XAbstractTransition* second = XVector_At_Base(transitions, j, XAbstractTransition*);
            XVector* secondExit = XStateMachine_exitSetForTransition(machine, second);
            if (!XStateMachine_vectorsIntersect(firstExit, secondExit)) {
                XClassDelete((XClass*)secondExit);
                ++j;
                continue;
            }

            if (XStateMachine_isDescendant((XAbstractState*)second->m_sourceState,
                                           (XAbstractState*)first->m_sourceState)) {
                XVector_removeAt_base(transitions, i);
                XClassDelete((XClass*)secondExit);
                --i;
                break;
            }

            XVector_removeAt_base(transitions, j);
            XClassDelete((XClass*)secondExit);
        }
        XClassDelete((XClass*)firstExit);
    }
}

static XVector* XStateMachine_selectTransitions(XStateMachine* machine, XEvent* event)
{
    XVector* enabled = XVector_Create(XAbstractTransition*);
    if (!enabled)
        return NULL;

    /* 对标 Qt：先取配置中的原子状态并按进入序排序，再逐个沿祖先链选择转换。 */
    XVector* atomics = XVector_Create(XAbstractState*);
    if (!atomics) {
        XClassDelete((XClass*)enabled);
        return NULL;
    }
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* atomic = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        if (XStateMachine_isAtomic(machine, atomic))
            XStateMachine_vectorAppendUnique(atomics, atomic);
    }
    XStateMachine_sortStatesBy(atomics, XStateMachine_stateEntryLessThan);

    XStateMachine_beginSelectTransitions_base(machine, event);
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)atomics); ++i) {
        XAbstractState* atomic = XVector_At_Base(atomics, i, XAbstractState*);

        XState* state = atomic->m_kind == XAbstractState_StandardState
            ? (XState*)atomic
            : atomic->m_parentState;
        for (; state; state = state->m_class.m_parentState) {
            const XVector* transitions = XState_transitions_const(state);
            bool found = false;
            for (int64_t j = 0; transitions && j < (int64_t)XVector_size_base((const XContainer*)transitions); ++j) {
                XAbstractTransition* transition = XVector_At_Base(transitions, j, XAbstractTransition*);
                if (XAbstractTransition_eventTest_base(transition, event)) {
                    XStateMachine_vectorAppendUnique(enabled, transition);
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
    }
    XStateMachine_removeConflictingTransitions(machine, enabled);
    XStateMachine_endSelectTransitions_base(machine, event);
    XClassDelete((XClass*)atomics);
    return enabled;
}

static void XStateMachine_recordHistory(XStateMachine* machine, XState* group)
{
    const XVector* children = XState_childStates_const_internal(group);
    for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (!child || child->m_kind != XAbstractState_HistoryState)
            continue;

        XHistoryState* history = (XHistoryState*)child;
        XVector_clear_base((XContainer*)history->m_configuration);
        for (int64_t j = 0; j < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++j) {
            XAbstractState* active = XVector_At_Base(machine->m_configuration, j, XAbstractState*);
            if (history->m_historyType == XHistoryState_ShallowHistory) {
                if (active->m_parentState == group)
                    XStateMachine_vectorAppendUnique(history->m_configuration, active);
            } else if (XStateMachine_isAtomic(machine, active)
                       && XStateMachine_isDescendant(active, (XAbstractState*)group)) {
                XStateMachine_vectorAppendUnique(history->m_configuration, active);
            }
        }
    }
}

static XVector* XStateMachine_computeExitSet(XStateMachine* machine, XVector* transitions)
{
    XVector* exitSet = XVector_Create(XAbstractState*);
    for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(transitions, i, XAbstractTransition*);
        XVector* local = XStateMachine_exitSetForTransition(machine, transition);
        for (int64_t j = 0; local && j < (int64_t)XVector_size_base((const XContainer*)local); ++j) {
            XAbstractState* state = XVector_At_Base(local, j, XAbstractState*);
            XStateMachine_vectorAppendUnique(exitSet, state);
        }
        if (local)
            XClassDelete((XClass*)local);
    }
    XStateMachine_sortStatesBy(exitSet, XStateMachine_stateExitLessThan);
    return exitSet;
}

static void XStateMachine_exitStates(XStateMachine* machine, XEvent* event, XVector* exitSet)
{
    for (int64_t i = 0; exitSet && i < (int64_t)XVector_size_base((const XContainer*)exitSet); ++i) {
        XAbstractState* state = XVector_At_Base(exitSet, i, XAbstractState*);
        if (XStateMachine_isGroup(state))
            XStateMachine_recordHistory(machine, (XState*)state);
    }

    for (int64_t i = 0; exitSet && i < (int64_t)XVector_size_base((const XContainer*)exitSet); ++i) {
        XAbstractState* state = XVector_At_Base(exitSet, i, XAbstractState*);
        if (XStateMachine_isGroup(state)) {
            const XVector* transitions = XState_transitions_const((XState*)state);
            for (int64_t j = 0; transitions && j < (int64_t)XVector_size_base((const XContainer*)transitions); ++j) {
                XAbstractTransition* transition = XVector_At_Base(transitions, j, XAbstractTransition*);
                XStateMachine_unregisterTransition_internal(machine, transition, true); /* 运行期:延迟回收 */
            }
        }
        XAbstractState_onExit_base(state, event);
        /* 对标 Qt：onExit 之后终止该状态的活动动画（terminateActiveAnimations）。 */
        XStateMachine_terminateStateAnimations(machine, state, true); /* 运行期:延迟回收 */
        XAbstractState_setActive_internal(state, false);
        XStateMachine_vectorRemovePointer(machine->m_configuration, state);
        XAbstractState_exited_signal(state);
    }
}

static bool XStateMachine_entryContainsBranch(const XVector* entries,
                                              const XState* group,
                                              const XAbstractState* directChild)
{
    for (int64_t i = 0; entries && i < (int64_t)XVector_size_base((const XContainer*)entries); ++i) {
        XAbstractState* state = XVector_At_Base(entries, i, XAbstractState*);
        if (state == directChild)
            return true;
        if (state && XStateMachine_isDescendant(state, directChild)
            && XStateMachine_isDescendant(state, (const XAbstractState*)group)) {
            return true;
        }
    }
    return false;
}

static bool XStateMachine_addEntryPath(XStateMachine* machine,
                                       XAbstractState* target,
                                       XAbstractState* domain,
                                       XVector* entries)
{
    if (!target || target->m_kind == XAbstractState_HistoryState)
        return false;

    XVector* path = XVector_Create(XAbstractState*);
    if (!path)
        return false;
    XAbstractState* current = target;
    while (current && current != domain) {
        XVector_push_back_1_base(path, &current);
        current = (XAbstractState*)current->m_parentState;
    }
    if (domain && current != domain) {
        XClassDelete((XClass*)path);
        XStateMachine_setError(machine,
                               XStateMachine_NoCommonAncestorForTransitionError,
                               target);
        return false;
    }
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)path) - 1; i >= 0; --i) {
        XAbstractState* state = XVector_At_Base(path, i, XAbstractState*);
        XStateMachine_vectorAppendUnique(entries, state);
    }
    XClassDelete((XClass*)path);
    return true;
}

static bool XStateMachine_addDefaultEntry(XStateMachine* machine,
                                          XState* owner,
                                          XAbstractState* initial,
                                          XVector* entries,
                                          XAbstractState** errorTarget)
{
    if (!machine || !owner || !initial || !entries)
        return false;
    if (initial->m_kind != XAbstractState_HistoryState) {
        return XStateMachine_addEntryPath(
            machine, initial, (XAbstractState*)owner, entries);
    }

    XHistoryState* history = (XHistoryState*)initial;
    XAbstractTransition* fallback = history->m_configuration
        && !XVector_isEmpty_base((const XContainer*)history->m_configuration)
        ? NULL
        : history->m_defaultTransition;
    const XVector* targets = history->m_configuration
        && !XVector_isEmpty_base((const XContainer*)history->m_configuration)
        ? history->m_configuration
        : (fallback ? XAbstractTransition_targetStates_const(fallback) : NULL);
    if (!targets || XVector_isEmpty_base((const XContainer*)targets)) {
        if (errorTarget) {
            *errorTarget = XStateMachine_setError(
                machine, XStateMachine_NoDefaultStateInHistoryStateError, initial);
        }
        return false;
    }

    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)targets); ++i) {
        XAbstractState* target = XVector_At_Base(targets, i, XAbstractState*);
        if (!XStateMachine_addEntryPath(
                machine, target, (XAbstractState*)owner, entries)) {
            return false;
        }
    }
    /* 对标 Qt：默认转换的 onTransition 不会被调用（Qt enterStates 的历史
     * 分支因历史状态从不进入 configuration 而不可达），这里同样不调。 */
    return true;
}

static bool XStateMachine_expandDefaultEntries(XStateMachine* machine,
                                               XVector* entries,
                                               XAbstractState** errorTarget)
{
    bool changed = true;
    while (changed) {
        changed = false;
        int64_t count = (int64_t)XVector_size_base((const XContainer*)entries);
        for (int64_t i = 0; i < count; ++i) {
            XAbstractState* abstractState = XVector_At_Base(entries, i, XAbstractState*);
            if (!XStateMachine_isGroup(abstractState))
                continue;
            XState* state = (XState*)abstractState;
            const XVector* children = XState_childStates_const_internal(state);
            if (!XStateMachine_hasRealChildren(state))
                continue;

            if (state->m_childMode == XState_ParallelStates) {
                for (int64_t j = 0; children && j < (int64_t)XVector_size_base((const XContainer*)children); ++j) {
                    XAbstractState* child = XVector_At_Base(children, j, XAbstractState*);
                    if (!child)
                        continue;
                    if (child->m_kind == XAbstractState_HistoryState) {
                        if (XStateMachine_vectorContains(entries, child))
                            continue;
                        XStateMachine_vectorAppendUnique(entries, child);
                        if (!XStateMachine_addDefaultEntry(
                                machine, state, child, entries, errorTarget)) {
                            return false;
                        }
                        changed = true;
                        continue;
                    }
                    if (!XStateMachine_entryContainsBranch(entries, state, child)) {
                        XStateMachine_addEntryPath(
                            machine, child, abstractState, entries);
                        changed = true;
                    }
                }
                continue;
            }

            bool hasPlannedChild = false;
            for (int64_t j = 0; children && j < (int64_t)XVector_size_base((const XContainer*)children); ++j) {
                XAbstractState* child = XVector_At_Base(children, j, XAbstractState*);
                if (child && child->m_kind != XAbstractState_HistoryState
                    && XStateMachine_entryContainsBranch(entries, state, child)) {
                    hasPlannedChild = true;
                    break;
                }
            }
            if (hasPlannedChild)
                continue;

            XAbstractState* initial = state->m_initialState;
            if (!initial) {
                *errorTarget = XStateMachine_setError(machine,
                                                       XStateMachine_NoInitialStateError,
                                                       abstractState);
                return false;
            }
            if (!XStateMachine_addDefaultEntry(
                    machine, state, initial, entries, errorTarget)) {
                return false;
            }
            changed = true;
        }
    }
    return true;
}

static XVector* XStateMachine_computeEntrySet(XStateMachine* machine,
                                              XVector* transitions,
                                              XAbstractState* forcedTarget)
{
    XVector* entries = XVector_Create(XAbstractState*);
    if (!entries)
        return NULL;

    if (!forcedTarget && machine->m_pendingErrorState) {
        forcedTarget = machine->m_pendingErrorState;
        machine->m_pendingErrorState = NULL;
    }

    if (forcedTarget) {
        XStateMachine_addEntryPath(machine, forcedTarget, (XAbstractState*)machine, entries);
    } else {
        for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
            XAbstractTransition* transition = XVector_At_Base(transitions, i, XAbstractTransition*);
            XVector* targets = XStateMachine_effectiveTargets(machine, transition);
            XAbstractState* domain = XStateMachine_transitionDomain(machine, transition, targets);
            if (machine->m_stopRequested && machine->m_error != XStateMachine_NoError) {
                if (targets)
                    XClassDelete((XClass*)targets);
                continue;
            }
            for (int64_t j = 0; targets && j < (int64_t)XVector_size_base((const XContainer*)targets); ++j) {
                XAbstractState* target = XVector_At_Base(targets, j, XAbstractState*);
                XStateMachine_addEntryPath(machine, target, domain, entries);
            }
            if (targets)
                XClassDelete((XClass*)targets);
        }
        if (machine->m_pendingErrorState) {
            XAbstractState* pendingErrorState = machine->m_pendingErrorState;
            machine->m_pendingErrorState = NULL;
            XVector_clear_base((XContainer*)entries);
            XStateMachine_addEntryPath(
                machine, pendingErrorState, (XAbstractState*)machine, entries);
        }
    }

    XAbstractState* errorTarget = NULL;
    if (!XStateMachine_expandDefaultEntries(
            machine, entries, &errorTarget) && errorTarget) {
        machine->m_pendingErrorState = NULL;
        XVector_clear_base((XContainer*)entries);
        XStateMachine_addEntryPath(machine, errorTarget, (XAbstractState*)machine, entries);
        errorTarget = NULL;
        XStateMachine_expandDefaultEntries(machine, entries, &errorTarget);
    }
    XStateMachine_sortStatesBy(entries, XStateMachine_stateEntryLessThan);
    return entries;
}

bool XStateMachine_isConfigured_internal(const XStateMachine* machine, const XAbstractState* state)
{
    if (!machine || !state)
        return false;
    if (state == (const XAbstractState*)machine)
        return machine->m_state == XStateMachine_Running;
    return XStateMachine_vectorContains(machine->m_configuration, state);
}

/* ==================== 动画管道（对标 selectAnimations/initializeAnimations/terminateActiveAnimations） ==================== */

/**
 * @brief 动画绑定记录的 finished 槽：动画结束后解除绑定，状态的全部动画
 *        结束时补发 propertiesAssigned（对标 _q_animationFinished）。
 * @note 记录可能正处于动画信号发射快照中，回收走 deleteLater（框架约定：
 *       XObject 派生异步释放）；m_spent 保证与退出路径互斥。
 */
static void XStateMachine_animationFinishedSlot(XObject* receiver, XVarList* arguments)
{
    (void)arguments;
    XStateMachine_AnimationBinding* binding = (XStateMachine_AnimationBinding*)receiver;
    if (!binding || binding->m_spent)
        return;
    binding->m_spent = true;
    XStateMachine* machine = binding->m_machine;
    XAbstractState* state = binding->m_state;

    XVector* bindings = machine ? machine->m_animationBindings : NULL;
    bool removedFromRegistry = false;
    if (bindings) {
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)bindings); ++i) {
            if (XVector_At_Base(bindings, i, XStateMachine_AnimationBinding*) == binding) {
                XVector_removeAt_base(bindings, i);
                removedFromRegistry = true;
                break;
            }
        }
    }
    binding->m_connection = NULL;
    XObject_deleteLater((XObject*)binding);
    if (!removedFromRegistry)
        return; /* 已被退出路径摘除登记，propertiesAssigned 由退出流程负责。 */

    bool stillAnimating = false;
    if (bindings) {
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)bindings); ++i) {
            XStateMachine_AnimationBinding* other
                = XVector_At_Base(bindings, i, XStateMachine_AnimationBinding*);
            if (other && other->m_state == state) {
                stillAnimating = true;
                break;
            }
        }
    }
    if (!stillAnimating && state && XStateMachine_isGroup(state))
        XState_propertiesAssigned_signal((XState*)state);
}

/**
 * @brief 清空指定状态（或全部状态，state==NULL）的动画绑定。
 * @param deferred true=运行期路径，记录走 deleteLater（可能处于动画信号
 *        发射快照中）；false=析构路径，同步删防事件循环退出后泄漏
 *        （参照 XActionGroup 双模式先例）。
 */
static void XStateMachine_terminateStateAnimations(XStateMachine* machine,
                                                   XAbstractState* state, bool deferred)
{
    if (!machine || !machine->m_animationBindings)
        return;
    XVector* bindings = machine->m_animationBindings;
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)bindings) - 1; i >= 0; --i) {
        XStateMachine_AnimationBinding* binding
            = XVector_At_Base(bindings, i, XStateMachine_AnimationBinding*);
        if (!binding || binding->m_spent || (state && binding->m_state != state))
            continue;
        XVector_removeAt_base(bindings, i);
        binding->m_spent = true;
        if (binding->m_connection && binding->m_animation) {
            XObject_disconnect_1((XObject*)binding->m_animation,
                                 (size_t)XAbstractAnimation_finished_signal,
                                 (XObject*)binding, XStateMachine_animationFinishedSlot);
        }
        binding->m_connection = NULL;
        /* 对标 terminateActiveAnimations：退出状态时停止仍在运行的动画。
         * 端点停止会同步发 finished，槽回调经 m_spent 防重入。 */
        if (binding->m_animation
            && XAbstractAnimation_state(binding->m_animation) != XAbstractAnimation_Stopped) {
            XAbstractAnimation_stop(binding->m_animation);
        }
        if (deferred)
            XObject_deleteLater((XObject*)binding);
        else
            XClassDelete((XClass*)binding);
    }
}

/**
 * @brief 为进入的状态启动选中的动画并登记绑定（对标 initializeAnimations）。
 * @details Qt 中属性接管依赖 QPropertyAnimation 匹配 (targetObject, propertyName)；
 *          XinYueC 本批次尚无插值动画类，因此属性赋值恒立即写入（与 Qt 在
 *          "无 QVariantAnimation 匹配"场景的行为一致），动画照常启动并把
 *          propertiesAssigned 推迟到全部动画结束（对标 Qt 文档语义）。
 */
static void XStateMachine_startStateAnimations(XStateMachine* machine,
                                               XAbstractState* state,
                                               const XVector* selectedAnimations)
{
    if (!machine || !state || !selectedAnimations
        || XVector_isEmpty_base((const XContainer*)selectedAnimations))
        return;
    if (!machine->m_animationBindings)
        return;
    /* 与 Qt 一致：只对带属性赋值的状态启动动画（initializeAnimations 的
     * assignments 空表早退）。 */
    const XVector* assignments = XState_propertyAssignments_const((const XState*)state);
    if (!assignments || XVector_isEmpty_base((const XContainer*)assignments))
        return;

    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)selectedAnimations); ++i) {
        XAbstractAnimation* animation = XVector_At_Base(selectedAnimations, i, XAbstractAnimation*);
        if (!animation)
            continue;
        XStateMachine_AnimationBinding* binding = XClass_Malloc(XStateMachine_AnimationBinding);
        if (!binding)
            continue;
        XObject_init((XObject*)binding);
        Set_Class_IsHeap(binding, true);
        binding->m_machine = machine;
        binding->m_state = state;
        binding->m_animation = animation;
        binding->m_connection = NULL;
        binding->m_spent = false;
        binding->m_connection = XObject_connect_1(
            (XObject*)animation, (size_t)XAbstractAnimation_finished_signal,
            (XObject*)binding, XStateMachine_animationFinishedSlot, XConnectionType_Auto);
        if (!binding->m_connection
            || !XVector_push_back_1_base(machine->m_animationBindings, &binding)) {
            if (binding->m_connection) {
                XObject_disconnect_1((XObject*)animation,
                                     (size_t)XAbstractAnimation_finished_signal,
                                     (XObject*)binding, XStateMachine_animationFinishedSlot);
            }
            XClassDelete((XClass*)binding);
            continue;
        }
        if (XAbstractAnimation_state(animation) != XAbstractAnimation_Running)
            XAbstractAnimation_start(animation);
    }
}

/** @brief 收集本次 microstep 选中转换的关联动画与状态机默认动画（对标 selectAnimations）。 */
static XVector* XStateMachine_selectAnimations(XStateMachine* machine, XVector* transitions)
{
    XVector* selected = NULL;
    if (!machine->m_animated)
        return NULL;
    selected = XVector_Create(XAbstractAnimation*);
    if (!selected)
        return NULL;
    for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(transitions, i, XAbstractTransition*);
        if (!transition)
            continue;
        const XVector* own = transition->m_animations;
        for (int64_t j = 0; own && j < (int64_t)XVector_size_base((const XContainer*)own); ++j) {
            XAbstractAnimation* animation = XVector_At_Base(own, j, XAbstractAnimation*);
            XStateMachine_vectorAppendUnique(selected, animation);
        }
    }
    const XVector* defaults = machine->m_defaultAnimations;
    for (int64_t j = 0; defaults && j < (int64_t)XVector_size_base((const XContainer*)defaults); ++j) {
        XAbstractAnimation* animation = XVector_At_Base(defaults, j, XAbstractAnimation*);
        XStateMachine_vectorAppendUnique(selected, animation);
    }
    if (XVector_isEmpty_base((const XContainer*)selected)) {
        XClassDelete((XClass*)selected);
        return NULL;
    }
    return selected;
}

static void XStateMachine_enterStates(XStateMachine* machine, XEvent* event, XVector* entries,
                                      const XVector* exitedStates,
                                      const XVector* selectedAnimations)
{
    for (int64_t i = 0; entries && i < (int64_t)XVector_size_base((const XContainer*)entries); ++i) {
        XAbstractState* state = XVector_At_Base(entries, i, XAbstractState*);
        if (!state || state->m_kind == XAbstractState_HistoryState)
            continue;
        if (XStateMachine_isConfigured_internal(machine, state))
            continue;
        XStateMachine_vectorAppendUnique(machine->m_configuration, state);

        if (XStateMachine_isGroup(state)) {
            const XVector* transitions = XState_transitions_const((XState*)state);
            for (int64_t j = 0; transitions && j < (int64_t)XVector_size_base((const XContainer*)transitions); ++j) {
                XAbstractTransition* transition = XVector_At_Base(transitions, j, XAbstractTransition*);
                XStateMachine_registerTransition_internal(machine, transition);
            }
        }

        /* 属性赋值必须在 onEntry 之前写入，与 Qt 的 enterStates 次序一致；
         * 动画在其后启动（Qt 的 initializeAnimations 同样先于 callOnEntry）。 */
        XStateMachine_applyPropertyAssignments(machine, state, exitedStates);
        XStateMachine_startStateAnimations(machine, state, selectedAnimations);
        XAbstractState_onEntry_base(state, event);
        XAbstractState_entered_signal(state);
        XAbstractState_setActive_internal(state, true);
        /* 对标 Qt：状态仍有活动动画时 propertiesAssigned 推迟到全部动画结束。 */
        bool animating = false;
        if (machine->m_animationBindings) {
            const XVector* bindings = machine->m_animationBindings;
            for (int64_t j = 0; j < (int64_t)XVector_size_base((const XContainer*)bindings); ++j) {
                XStateMachine_AnimationBinding* binding
                    = XVector_At_Base(bindings, j, XStateMachine_AnimationBinding*);
                if (binding && binding->m_state == state) {
                    animating = true;
                    break;
                }
            }
        }
        if (XStateMachine_isGroup(state) && !animating)
            XState_propertiesAssigned_signal((XState*)state);
    }
}

static bool XStateMachine_isInFinalState(XStateMachine* machine, XAbstractState* state)
{
    if (!machine || !state || !XStateMachine_isGroup(state))
        return false;

    XState* group = (XState*)state;
    const XVector* children = XState_childStates_const_internal(group);
    if (group->m_childMode == XState_ExclusiveStates) {
        for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
            XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
            if (child && child->m_kind == XAbstractState_FinalState
                && XStateMachine_isConfigured_internal(machine, child)) {
                return true;
            }
        }
        return false;
    }

    bool hasRegion = false;
    for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (!child || child->m_kind == XAbstractState_HistoryState)
            continue;
        hasRegion = true;
        if (!XStateMachine_isInFinalState(machine, child))
            return false;
    }
    return hasRegion;
}

static bool XStateMachine_emitFinishedStates(XStateMachine* machine, XVector* entries)
{
    for (int64_t i = 0; entries && i < (int64_t)XVector_size_base((const XContainer*)entries); ++i) {
        XAbstractState* state = XVector_At_Base(entries, i, XAbstractState*);
        if (!state || state->m_kind != XAbstractState_FinalState || !state->m_parentState)
            continue;
        XState* parent = state->m_parentState;
        if ((XAbstractState*)parent != (XAbstractState*)machine)
            XState_finished_signal(parent);

        XState* grandparent = parent->m_class.m_parentState;
        if (grandparent && grandparent->m_childMode == XState_ParallelStates
            && XStateMachine_isInFinalState(machine, (XAbstractState*)grandparent)
            && (XAbstractState*)grandparent != (XAbstractState*)machine) {
            XState_finished_signal(grandparent);
        }
    }
    return XStateMachine_isInFinalState(machine, (XAbstractState*)machine);
}

static bool XStateMachine_microstep(XStateMachine* machine, XEvent* event, XVector* transitions)
{
    /* 对标 Qt microstep 的次序：先算退出集与恢复集合，再算进入集（Qt 的
     * computeEntrySet 也在 exitStates 之前），然后退出 → 转换内容 → 进入。 */
    XVector* exitSet = XStateMachine_computeExitSet(machine, transitions);
    /* 退出前先取恢复集合：被退出的状态登记着进入时的旧值（对标
     * microstep 里 computePendingRestorables 早于 exitStates 的次序）。 */
    XVector* pendingRestorables =
        XStateMachine_computePendingRestorables_internal(machine, exitSet);
    XVector* entries = XStateMachine_computeEntrySet(machine, transitions, NULL);
    XStateMachine_exitStates(machine, event, exitSet);

    for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(transitions, i, XAbstractTransition*);
        XAbstractTransition_onTransition_base(transition, event);
        XAbstractTransition_triggered_signal(transition);
    }

    /* Qt 把"隐式恢复赋值"挂到最外层进入状态上：这里在进入前统一写回，
     * 让随后 enterStates 的属性赋值能够覆盖它（后者优先级更高）。 */
    XStateMachine_restoreProperties(machine, pendingRestorables);
    XVector* selectedAnimations = XStateMachine_selectAnimations(machine, transitions);
    XStateMachine_enterStates(machine, event, entries, exitSet, selectedAnimations);
    bool finished = XStateMachine_emitFinishedStates(machine, entries);

    if (selectedAnimations)
        XClassDelete((XClass*)selectedAnimations);
    XStateMachine_restorablesRelease_internal(pendingRestorables);
    if (exitSet)
        XClassDelete((XClass*)exitSet);
    if (entries)
        XClassDelete((XClass*)entries);
    return finished;
}

/* @param deferred 运行期(finish)路径须延迟回收;析构路径必须同步删,
 *        防事件循环退出后泄漏(参照 XActionGroup 双模式先例)。 */
static void XStateMachine_unregisterConfigurationTransitions(XStateMachine* machine, bool deferred)
{
    if (!machine)
        return;
    const XVector* rootTransitions = XState_transitions_const((XState*)machine);
    for (int64_t i = 0; rootTransitions && i < (int64_t)XVector_size_base((const XContainer*)rootTransitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(rootTransitions, i, XAbstractTransition*);
        XStateMachine_unregisterTransition_internal(machine, transition, deferred);
    }
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        if (!XStateMachine_isGroup(state))
            continue;
        const XVector* transitions = XState_transitions_const((XState*)state);
        for (int64_t j = 0; transitions && j < (int64_t)XVector_size_base((const XContainer*)transitions); ++j) {
            XAbstractTransition* transition = XVector_At_Base(transitions, j, XAbstractTransition*);
            XStateMachine_unregisterTransition_internal(machine, transition, deferred);
        }
    }
}

static void XStateMachine_finish(XStateMachine* machine)
{
    machine->m_state = XStateMachine_NotRunning;
    machine->m_processing = false;
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_unregisterConfigurationTransitions(machine, true); /* 运行期:延迟回收 */
    XState_finished_signal((XState*)machine);
    XStateMachine_runningChanged_signal(machine, false);
}

static void XStateMachine_completeStop(XStateMachine* machine)
{
    machine->m_stopRequested = false;
    machine->m_state = XStateMachine_NotRunning;
    machine->m_processing = false;
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_unregisterConfigurationTransitions(machine, true); /* 运行期:延迟回收 */
    XStateMachine_stopped_signal(machine);
    XStateMachine_runningChanged_signal(machine, false);
}

static void XStateMachine_process(XStateMachine* machine)
{
    if (!machine || machine->m_state != XStateMachine_Running || machine->m_processing)
        return;

    machine->m_processing = true;
    bool finished = false;
    while (machine->m_processing && !machine->m_stopRequested && !finished) {
        XEvent nullEvent;
        XEvent_init(&nullEvent, XEVENT_TYPE_NONE);
        XVector* enabled = XStateMachine_selectTransitions(machine, &nullEvent);
        if (enabled && !XVector_isEmpty_base((const XContainer*)enabled)) {
            XStateMachine_beginMicrostep_base(machine, &nullEvent);
            finished = XStateMachine_microstep(machine, &nullEvent, enabled);
            XStateMachine_endMicrostep_base(machine, &nullEvent);
            XClassDelete((XClass*)enabled);
            XClassDeinit((XClass*)&nullEvent);
            continue;
        }
        if (enabled)
            XClassDelete((XClass*)enabled);
        XClassDeinit((XClass*)&nullEvent);

        XEvent* event = NULL;
        do {
            event = XStateMachine_dequeue(machine->m_internalEventQueue);
            if (!event)
                event = XStateMachine_dequeue(machine->m_externalEventQueue);
            if (!event)
                break;
            enabled = XStateMachine_selectTransitions(machine, event);
            if (!enabled) {
                /* 转换选择失败（内存分配失败返回 NULL）：已出队事件归本循环所有，
                 * 先释放再停止处理，防止事件指针被下一轮循环覆盖后泄漏。 */
                XClassDelete((XClass*)event);
                event = NULL;
                break;
            }
            if (XVector_isEmpty_base((const XContainer*)enabled)) {
                XClassDelete((XClass*)enabled);
                enabled = NULL;
                XClassDelete((XClass*)event);
                event = NULL;
            }
        } while (!enabled);

        if (!event || !enabled) {
            machine->m_processing = false;
            break;
        }

        XStateMachine_beginMicrostep_base(machine, event);
        finished = XStateMachine_microstep(machine, event, enabled);
        XStateMachine_endMicrostep_base(machine, event);
        XClassDelete((XClass*)enabled);
        XClassDelete((XClass*)event);
    }

    if (finished) {
        XStateMachine_finish(machine);
    } else if (machine->m_stopRequested) {
        XStateMachine_completeStop(machine);
    } else {
        machine->m_processing = false;
    }
}

static void XStateMachine_startInternal(XStateMachine* machine)
{
    if (!machine || machine->m_state != XStateMachine_Starting)
        return;

    XStateMachine_unregisterConfigurationTransitions(machine, true); /* 运行期:延迟回收 */
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        XAbstractState_setActive_internal(state, false);
    }
    XVector_clear_base((XContainer*)machine->m_configuration);
    XStateMachine_clearOwnedEvents(machine->m_internalEventQueue);
    XStateMachine_clearOwnedEvents(machine->m_externalEventQueue);
    XStateMachine_clearHistoryRecursive((XState*)machine);
    /* 对标 Qt：_q_start 不清 error/errorString，上一次错误保留到显式 clearError。 */

    machine->m_state = XStateMachine_Running;
    const XVector* rootTransitions = XState_transitions_const((XState*)machine);
    for (int64_t i = 0; rootTransitions && i < (int64_t)XVector_size_base((const XContainer*)rootTransitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(rootTransitions, i, XAbstractTransition*);
        XStateMachine_registerTransition_internal(machine, transition);
    }

    XVector* entries = XVector_Create(XAbstractState*);
    XAssert(entries, "Memory allocation failed");
    XEvent nullEvent;
    XEvent_init(&nullEvent, XEVENT_TYPE_NONE);
    XState* root = (XState*)machine;
    if (root->m_childMode == XState_ParallelStates) {
        const XVector* children = XState_childStates_const_internal(root);
        for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
            XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
            if (child && child->m_kind == XAbstractState_HistoryState) {
                XAbstractState* ignoredErrorTarget = NULL;
                XStateMachine_vectorAppendUnique(entries, child);
                XStateMachine_addDefaultEntry(
                    machine, root, child, entries, &ignoredErrorTarget);
            } else if (child) {
                XStateMachine_addEntryPath(
                    machine, child, (XAbstractState*)machine, entries);
            }
        }
    } else if (root->m_initialState) {
        XAbstractState* ignoredErrorTarget = NULL;
        XStateMachine_addDefaultEntry(
            machine, root, root->m_initialState, entries,
            &ignoredErrorTarget);
    }

    XAbstractState* errorTarget = NULL;
    if (!XStateMachine_expandDefaultEntries(
            machine, entries, &errorTarget) && errorTarget) {
        machine->m_pendingErrorState = NULL;
        XVector_clear_base((XContainer*)entries);
        XStateMachine_addEntryPath(
            machine, errorTarget, (XAbstractState*)machine, entries);
        errorTarget = NULL;
        XStateMachine_expandDefaultEntries(machine, entries, &errorTarget);
    }
    XStateMachine_sortStatesBy(entries, XStateMachine_stateEntryLessThan);
    /* 启动路径没有退出集合，隐式恢复集合必然为空；启动转换无关联动画。 */
    XStateMachine_enterStates(machine, &nullEvent, entries, NULL, NULL);
    bool finished = XStateMachine_emitFinishedStates(machine, entries);
    XClassDeinit((XClass*)&nullEvent);
    XClassDelete((XClass*)entries);

    XStateMachine_started_signal(machine);
    XStateMachine_runningChanged_signal(machine, true);
    if (finished)
        XStateMachine_finish(machine);
    else
        XStateMachine_process(machine);
}

static void VXStateMachine_hook(XStateMachine* machine, XEvent* event)
{
    (void)machine;
    (void)event;
}

/* 对标 QStateMachine::onEntry/onExit：状态机作为另一台状态机的子状态被
 * 进入/退出时自动启停自身。 */
static void VXStateMachine_onEntry(XAbstractState* state, XEvent* event)
{
    (void)event;
    XStateMachine_start((XStateMachine*)state);
}

static void VXStateMachine_onExit(XAbstractState* state, XEvent* event)
{
    (void)event;
    XStateMachine_stop((XStateMachine*)state);
}

static bool VXStateMachine_event(XStateMachine* machine, XEvent* event)
{
    if (!machine || !event)
        return false;
    if (XEvent_type(event) == g_zeroDelayedEventType) {
        XStateMachine_activateZeroDelayedEvents(machine);
        if (machine->m_state == XStateMachine_Running)
            XStateMachine_process(machine);
        XEvent_accept(event);
        return true;
    }
    if (XEvent_type(event) == g_processEventType) {
        machine->m_processingScheduled = false;
        if (machine->m_state == XStateMachine_Starting)
            XStateMachine_startInternal(machine);
        else if (machine->m_state == XStateMachine_Running)
            XStateMachine_process(machine);
        XEvent_accept(event);
        return true;
    }
    return XVtableGetFunc(XState_class_init(), EXObject_Event,
                          bool(*)(XObject*, XEvent*))((XObject*)machine, event);
}

static bool VXStateMachine_eventFilter(XStateMachine* machine, XObject* watched, XEvent* event)
{
    if (!machine || !watched || !event || machine->m_state != XStateMachine_Running)
        return false;
    XEvent* clone = XEvent_clone_base(event);
    if (!clone)
        return false;
    XStateMachine_WrappedEvent* wrapped = XStateMachine_WrappedEvent_create(watched, clone);
    if (!wrapped) {
        XClassDelete((XClass*)clone);
        return false;
    }
    XStateMachine_postInternalEvent_internal(machine, (XEvent*)wrapped);
    return false;
}

static void VXStateMachine_timerEvent(XStateMachine* machine, XTimerEvent* event)
{
    if (!machine || !event)
        return;
    XTimerId timerId = XTimerEvent_timerId(event);
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_delayedEvents); ++i) {
        XStateMachine_DelayedEvent delayed = XVector_At_Base(
            machine->m_delayedEvents, i, XStateMachine_DelayedEvent);
        if (delayed.m_timerId != timerId)
            continue;
        XObject_killTimer((XObject*)machine, timerId);
        XVector_removeAt_base(machine->m_delayedEvents, i);
        if (!XStateMachine_postEvent(machine, delayed.m_event, XStateMachine_NormalPriority))
            XClassDelete((XClass*)delayed.m_event);
        break;
    }
    XEvent_accept((XEvent*)event);
}

static void VXStateMachine_deinit(XStateMachine* machine)
{
    if (!machine)
        return;
    XStateMachine_unregisterConfigurationTransitions(machine, false); /* 析构路径:同步删 */
    XStateMachine_clearSignalConnections(machine);
    XStateMachine_terminateStateAnimations(machine, NULL, false); /* 析构路径:同步删 */
    if (machine->m_animationBindings) {
        XClassDelete((XClass*)machine->m_animationBindings);
        machine->m_animationBindings = NULL;
    }
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_clearOwnedEvents(machine->m_internalEventQueue);
    XStateMachine_clearOwnedEvents(machine->m_externalEventQueue);
    XClassDelete((XClass*)machine->m_configuration);
    XClassDelete((XClass*)machine->m_internalEventQueue);
    XClassDelete((XClass*)machine->m_externalEventQueue);
    XClassDelete((XClass*)machine->m_delayedEvents);
    XClassDelete((XClass*)machine->m_signalConnections);
    if (machine->m_defaultAnimations) {
        XClassDelete((XClass*)machine->m_defaultAnimations);
        machine->m_defaultAnimations = NULL;
    }
    if (machine->m_restorables) {
        for (int64_t i = 0;
             i < (int64_t)XVector_size_base((const XContainer*)machine->m_restorables); ++i) {
            XStateMachine_RestorableEntry* entry = XVector_at_base(machine->m_restorables, i);
            if (entry->m_propertyName)
                XStateMachine_freePropertyName(entry->m_propertyName);
            if (entry->m_value)
                XClassDelete((XClass*)entry->m_value);
        }
        XClassDelete((XClass*)machine->m_restorables);
        machine->m_restorables = NULL;
    }
    if (machine->m_errorString) {
        XClassDelete((XClass*)machine->m_errorString);
        machine->m_errorString = NULL;
    }
    machine->m_configuration = NULL;
    machine->m_internalEventQueue = NULL;
    machine->m_externalEventQueue = NULL;
    machine->m_delayedEvents = NULL;
    machine->m_signalConnections = NULL;
    XVtableGetFunc(XState_class_init(), EXClass_Deinit,
                   void(*)(XState*))((XState*)machine);
}

XVtable* XStateMachine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachine)
    XVTABLE_INHERIT_XCLASS(XState);
    void* table[] = {
        VXStateMachine_hook, VXStateMachine_hook,
        VXStateMachine_hook, VXStateMachine_hook
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXStateMachine_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VXStateMachine_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXStateMachine_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnEntry, VXStateMachine_onEntry);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractState_OnExit, VXStateMachine_onExit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_deinit);
    return XVTABLE_DEFAULT;
}

XStateMachine* XStateMachine_create_ex(XMemoryType memory, XState_ChildMode childMode)
{
    XStateMachine* machine = XMemory_malloc(sizeof(XStateMachine), memory);
    if (!machine)
        return NULL;
    XStateMachine_init_ex(machine, childMode);
    Set_Class_Memory(machine, memory); Set_Class_IsHeap(machine, true);
    return machine;
}

void XStateMachine_init(XStateMachine* machine)
{
    XStateMachine_init_ex(machine, XState_ExclusiveStates);
}

void XStateMachine_init_ex(XStateMachine* machine, XState_ChildMode childMode)
{
    if (!machine)
        return;
    if (g_processEventType == XEVENT_TYPE_NONE) {
        g_processEventType = (XEventType)XEvent_registerEventType(-1);
        g_zeroDelayedEventType = (XEventType)XEvent_registerEventType(-1);
        g_signalEventType = (XEventType)XEvent_registerEventType(-1);
        g_wrappedEventType = (XEventType)XEvent_registerEventType(-1);
    }

    XState_init_ex((XState*)machine, childMode, NULL);
    XClassSetVtable(machine, XStateMachine);
    machine->m_class.m_class.m_kind = XAbstractState_StateMachine;
    machine->m_class.m_class.m_machine = machine;
    machine->m_configuration = XVector_Create(XAbstractState*);
    XAssert(machine->m_configuration, "Memory allocation failed");
    machine->m_internalEventQueue = XVector_Create(XEvent*);
    XAssert(machine->m_internalEventQueue, "Memory allocation failed");
    machine->m_externalEventQueue = XVector_Create(XEvent*);
    XAssert(machine->m_externalEventQueue, "Memory allocation failed");
    machine->m_delayedEvents = XVector_Create(XStateMachine_DelayedEvent);
    XAssert(machine->m_delayedEvents, "Memory allocation failed");
    machine->m_signalConnections = XVector_Create(XStateMachine_SignalConnection*);
    XAssert(machine->m_signalConnections, "Memory allocation failed");
    machine->m_restorables = XVector_Create(XStateMachine_RestorableEntry);
    XAssert(machine->m_restorables, "Memory allocation failed");
    machine->m_defaultAnimations = XVector_Create(XAbstractAnimation*);
    XAssert(machine->m_defaultAnimations, "Memory allocation failed");
    machine->m_animationBindings = XVector_Create(XStateMachine_AnimationBinding*);
    XAssert(machine->m_animationBindings, "Memory allocation failed");
    machine->m_pendingErrorState = NULL;
    machine->m_state = XStateMachine_NotRunning;
    machine->m_error = XStateMachine_NoError;
    machine->m_errorString = NULL;
    machine->m_nextDelayedEventId = 0;
    machine->m_animated = true; /* 对标 Qt：animated 属性默认 true。 */
    machine->m_processing = false;
    machine->m_processingScheduled = false;
    machine->m_stopRequested = false;
    machine->m_globalRestorePolicy = XState_DontRestoreProperties;
}

bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state)
{
    if (!machine || !state)
        return false;
    /* 对标 Qt：已属于本机的状态不重复挂接（Qt 侧发 qWarning 后返回）。 */
    if (state->m_parentState == (XState*)machine)
        return false;
    return XState_addChild_internal((XState*)machine, state);
}

bool XStateMachine_removeState(XStateMachine* machine, XAbstractState* state)
{
    if (!machine || !state || state->m_parentState != (XState*)machine)
        return false;
    if (XStateMachine_isConfigured_internal(machine, state))
        return false;
    XState_removeChild_internal((XState*)machine, state);
    return true;
}

XStateMachine_Error XStateMachine_error(const XStateMachine* machine)
{
    return machine ? machine->m_error : XStateMachine_NoError;
}

const XString* XStateMachine_errorString(const XStateMachine* machine)
{
    return (machine && machine->m_errorString) ? machine->m_errorString : NULL;
}

const char* XStateMachine_errorString_utf8(const XStateMachine* machine)
{
    /* 兼容视图：文本来自内部 XString 的 UTF-8 转换缓存，核心存储是 errorString 的 XString。 */
    return (machine && machine->m_errorString) ? XString_toUtf8(machine->m_errorString) : NULL;
}

void XStateMachine_clearError(XStateMachine* machine)
{
    if (!machine)
        return;
    /* 对标 Qt：只清错误码与描述，不动活动配置、运行状态和已排队的错误状态。 */
    machine->m_error = XStateMachine_NoError;
    if (machine->m_errorString) {
        XClassDelete((XClass*)machine->m_errorString);
        machine->m_errorString = NULL;
    }
}

XState_RestorePolicy XStateMachine_globalRestorePolicy(const XStateMachine* machine)
{
    return machine ? machine->m_globalRestorePolicy : XState_DontRestoreProperties;
}

void XStateMachine_setGlobalRestorePolicy(XStateMachine* machine,
                                          XState_RestorePolicy restorePolicy)
{
    if (!machine)
        return;
    machine->m_globalRestorePolicy = restorePolicy;
}

bool XStateMachine_isAnimated(const XStateMachine* machine)
{
    return machine ? machine->m_animated : false;
}

void XStateMachine_setAnimated(XStateMachine* machine, bool enabled)
{
    if (machine)
        machine->m_animated = enabled;
}

bool XStateMachine_addDefaultAnimation(XStateMachine* machine, XAbstractAnimation* animation)
{
    if (!machine || !machine->m_defaultAnimations || !animation)
        return false;
    return XVector_push_back_1_base(machine->m_defaultAnimations, &animation);
}

const XVector* XStateMachine_defaultAnimations_const(const XStateMachine* machine)
{
    return machine ? machine->m_defaultAnimations : NULL;
}

void XStateMachine_removeDefaultAnimation(XStateMachine* machine, XAbstractAnimation* animation)
{
    if (!machine || !machine->m_defaultAnimations || !animation)
        return;
    const void* key = animation;
    /* 对标 Qt 的 removeAll：移除全部匹配项。 */
    int64_t index;
    while ((index = XVector_indexOf(machine->m_defaultAnimations, &key, 0)) >= 0)
        XVector_removeAt_base(machine->m_defaultAnimations, index);
}

bool XStateMachine_isRunning(const XStateMachine* machine)
{
    return machine && machine->m_state == XStateMachine_Running;
}

void XStateMachine_start(XStateMachine* machine)
{
    if (!machine || machine->m_state != XStateMachine_NotRunning)
        return;
    if (machine->m_class.m_childMode == XState_ExclusiveStates
        && !machine->m_class.m_initialState) {
        return;
    }
    machine->m_state = XStateMachine_Starting;
    machine->m_stopRequested = false;
    XStateMachine_scheduleProcess_internal(machine);
}

void XStateMachine_stop(XStateMachine* machine)
{
    if (!machine || machine->m_state == XStateMachine_NotRunning)
        return;
    machine->m_stopRequested = true;
    XStateMachine_scheduleProcess_internal(machine);
}

void XStateMachine_setRunning(XStateMachine* machine, bool running)
{
    if (running)
        XStateMachine_start(machine);
    else
        XStateMachine_stop(machine);
}

bool XStateMachine_postEvent(XStateMachine* machine, XEvent* event,
                             XStateMachine_EventPriority priority)
{
    if (!machine || !event
        || (machine->m_state != XStateMachine_Running
            && machine->m_state != XStateMachine_Starting)) {
        return false;
    }
    XVector* queue = priority == XStateMachine_HighPriority
        ? machine->m_internalEventQueue
        : machine->m_externalEventQueue;
    if (!XVector_push_back_1_base(queue, &event))
        return false;
    XStateMachine_scheduleProcess_internal(machine);
    return true;
}

void XStateMachine_postInternalEvent_internal(XStateMachine* machine, XEvent* event)
{
    if (!XStateMachine_postEvent(machine, event, XStateMachine_HighPriority) && event)
        XClassDelete((XClass*)event);
}

int XStateMachine_postDelayedEvent(XStateMachine* machine, XEvent* event, int delayMs)
{
    if (!machine || !event || delayMs < 0 || machine->m_state != XStateMachine_Running)
        return -1;
    XTimerId timerId = XTIMER_INVALID_ID;
    if (delayMs > 0) {
        timerId = XObject_startTimer_ms((XObject*)machine, (uint64_t)delayMs,
                                        XTimerType_PreciseTimer);
        if (timerId == XTIMER_INVALID_ID)
            return -1;
    }

    XStateMachine_DelayedEvent delayed;
    delayed.m_id = machine->m_nextDelayedEventId++;
    delayed.m_timerId = timerId;
    delayed.m_event = event;
    if (!XVector_push_back_1_base(machine->m_delayedEvents, &delayed)) {
        if (timerId != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)machine, timerId);
        return -1;
    }
    if (delayMs == 0) {
        XEvent* activationEvent = XEvent_create(g_zeroDelayedEventType);
        if (!activationEvent) {
            XVector_removeAt_base(
                machine->m_delayedEvents,
                (int64_t)XVector_size_base((const XContainer*)machine->m_delayedEvents) - 1);
            return -1;
        }
        XCoreApplication_postEvent(
            (XObject*)machine, activationEvent, XEVENT_PRIORITY_NORMAL);
    }
    return delayed.m_id;
}

bool XStateMachine_cancelDelayedEvent(XStateMachine* machine, int id)
{
    if (!machine || machine->m_state != XStateMachine_Running)
        return false;
    for (int64_t i = 0;
         i < (int64_t)XVector_size_base((const XContainer*)machine->m_delayedEvents); ++i) {
        XStateMachine_DelayedEvent delayed = XVector_At_Base(
            machine->m_delayedEvents, i, XStateMachine_DelayedEvent);
        if (delayed.m_id != id)
            continue;
        if (delayed.m_timerId != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)machine, delayed.m_timerId);
        XClassDelete((XClass*)delayed.m_event);
        XVector_removeAt_base(machine->m_delayedEvents, i);
        return true;
    }
    return false;
}

const XVector* XStateMachine_configuration_const(const XStateMachine* machine)
{
    return machine ? machine->m_configuration : NULL;
}

void XStateMachine_scheduleProcess_internal(XStateMachine* machine)
{
    if (!machine || machine->m_processing || machine->m_processingScheduled)
        return;
    XEvent* event = XEvent_create(g_processEventType);
    if (!event)
        return;
    machine->m_processingScheduled = true;
    XCoreApplication_postEvent((XObject*)machine, event, XEVENT_PRIORITY_NORMAL);
}

void XStateMachine_registerSignalTransition_internal(XStateMachine* machine,
                                                      XSignalTransition* transition)
{
    if (!machine || !transition || transition->m_connection
        || !transition->m_senderObject || !transition->m_signal) {
        return;
    }

    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_signalConnections); ++i) {
        XStateMachine_SignalConnection* record = XVector_At_Base(
            machine->m_signalConnections, i, XStateMachine_SignalConnection*);
        if (record && record->m_sender == transition->m_senderObject
            && record->m_signal == transition->m_signal) {
            ++record->m_referenceCount;
            transition->m_connection = record->m_connection;
            transition->m_registeredMachine = machine;
            return;
        }
    }

    XStateMachine_SignalConnection* record = XClass_Malloc(XStateMachine_SignalConnection);
    if (!record)
        return;
    XObject_init((XObject*)record);
    Set_Class_IsHeap(record, true);
    record->m_machine = machine;
    record->m_sender = transition->m_senderObject;
    record->m_signal = transition->m_signal;
    record->m_referenceCount = 1;
    record->m_connection = XObject_connect_1(
        (XObject*)record->m_sender, record->m_signal, (XObject*)record,
        XStateMachine_signalSlot, XConnectionType_Auto);
    if (!record->m_connection
        || !XVector_push_back_1_base(machine->m_signalConnections, &record)) {
        if (record->m_connection) {
            XObject_disconnect_1(
                (XObject*)record->m_sender, record->m_signal,
                (XObject*)record, XStateMachine_signalSlot);
        }
        XClassDelete((XClass*)record);
        return;
    }

    transition->m_connection = record->m_connection;
    transition->m_registeredMachine = machine;
}

/* @param deferred 运行期路径(setSignal/setSenderObject/removeTransition)可能处于
 *        sender 发射快照中,须延迟回收,同步删 UAF(对标 Qt deleteLater 语义);
 *        析构路径必须同步删,防事件循环退出后泄漏。参照 XActionGroup 双模式先例。 */
void XStateMachine_unregisterSignalTransition_internal(XStateMachine* machine,
                                                        XSignalTransition* transition,
                                                        bool deferred)
{
    if (!transition)
        return;
    if (!machine)
        machine = transition->m_registeredMachine;
    if (!machine || !machine->m_signalConnections) {
        transition->m_connection = NULL;
        transition->m_registeredMachine = NULL;
        return;
    }

    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_signalConnections); ++i) {
        XStateMachine_SignalConnection* record = XVector_At_Base(
            machine->m_signalConnections, i, XStateMachine_SignalConnection*);
        if (!record || record->m_sender != transition->m_senderObject
            || record->m_signal != transition->m_signal) {
            continue;
        }
        transition->m_connection = NULL;
        transition->m_registeredMachine = NULL;
        if (record->m_referenceCount > 1) {
            --record->m_referenceCount;
            return;
        }
        if (record->m_connection) {
            XObject_disconnect_1(
                (XObject*)record->m_sender, record->m_signal,
                (XObject*)record, XStateMachine_signalSlot);
        }
        record->m_connection = NULL;
        XVector_removeAt_base(machine->m_signalConnections, i);
        /* record 可能处于 sender 发射快照中:运行期延迟回收防同步删 UAF;
         * 析构路径(deferred=false)必须同步,防事件循环退出后泄漏。 */
        if (deferred)
            XObject_deleteLater((XObject*)record);
        else
            XClassDelete((XClass*)record);
        return;
    }

    transition->m_connection = NULL;
    transition->m_registeredMachine = NULL;
}

static void XStateMachine_removeTargetFromTransition(XAbstractTransition* transition,
                                                     XAbstractState* target)
{
    if (!transition || !transition->m_targetStates || !target)
        return;
    XAbstractState* previousFirst = XAbstractTransition_targetState(transition);
    bool changed = false;
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)transition->m_targetStates) - 1; i >= 0; --i) {
        if (XVector_At_Base(transition->m_targetStates, i, XAbstractState*) == target) {
            XVector_removeAt_base(transition->m_targetStates, i);
            changed = true;
        }
    }
    if (!changed)
        return;
    if (previousFirst != XAbstractTransition_targetState(transition))
        XAbstractTransition_targetStateChanged_signal(transition);
    XAbstractTransition_targetStatesChanged_signal(transition);
}

static void XStateMachine_removeTargetRecursive(XState* state, XAbstractState* target)
{
    if (!state || !target)
        return;
    const XVector* transitions = XState_transitions_const(state);
    for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XStateMachine_removeTargetFromTransition(
            XVector_At_Base(transitions, i, XAbstractTransition*), target);
    }
    const XVector* children = XState_childStates_const_internal(state);
    for (int64_t i = 0; children && i < (int64_t)XVector_size_base((const XContainer*)children); ++i) {
        XAbstractState* child = XVector_At_Base(children, i, XAbstractState*);
        if (!child)
            continue;
        if (child->m_kind == XAbstractState_HistoryState) {
            XStateMachine_removeTargetFromTransition(
                ((XHistoryState*)child)->m_defaultTransition, target);
        } else if (XStateMachine_isGroup(child)) {
            XStateMachine_removeTargetRecursive((XState*)child, target);
        }
    }
}

void XStateMachine_removeTargetState_internal(XStateMachine* machine,
                                              XAbstractState* target)
{
    if (!machine || !target || target == (XAbstractState*)machine)
        return;
    XStateMachine_removeTargetRecursive((XState*)machine, target);
    XStateMachine_vectorRemovePointer(machine->m_configuration, target);
    if (machine->m_pendingErrorState == target)
        machine->m_pendingErrorState = NULL;
}

void XStateMachine_registerTransition_internal(XStateMachine* machine, XAbstractTransition* transition)
{
    if (!machine || !transition)
        return;
    switch (transition->m_kind) {
    case XAbstractTransition_SignalTransition:
        XSignalTransition_register_internal((XSignalTransition*)transition);
        break;
    case XAbstractTransition_EventTransition:
    case XAbstractTransition_KeyEventTransition:
    case XAbstractTransition_MouseEventTransition:
        XEventTransition_register_internal((XEventTransition*)transition);
        break;
    default:
        break;
    }
}

/* @param deferred 运行期路径可能处于 sender 发射快照中,须延迟回收;析构路径必须
 *        同步删,防事件循环退出后泄漏(参照 XActionGroup 双模式先例)。 */
void XStateMachine_unregisterTransition_internal(XStateMachine* machine, XAbstractTransition* transition, bool deferred)
{
    (void)machine;
    if (!transition)
        return;
    switch (transition->m_kind) {
    case XAbstractTransition_SignalTransition:
        XSignalTransition_unregister_internal((XSignalTransition*)transition, deferred);
        break;
    case XAbstractTransition_EventTransition:
    case XAbstractTransition_KeyEventTransition:
    case XAbstractTransition_MouseEventTransition:
        XEventTransition_unregister_internal((XEventTransition*)transition);
        break;
    default:
        break;
    }
}

void XStateMachine_beginSelectTransitions_base(XStateMachine* machine, XEvent* event)
{
    if (ISNULL(machine, "XStateMachine") || ISNULL(XClassGetVtable(machine), "Vtable"))
        return;
    XClassGetVirtualFunc(machine, EXStateMachine_BeginSelectTransitions,
                         void(*)(XStateMachine*, XEvent*))(machine, event);
}

void XStateMachine_endSelectTransitions_base(XStateMachine* machine, XEvent* event)
{
    if (ISNULL(machine, "XStateMachine") || ISNULL(XClassGetVtable(machine), "Vtable"))
        return;
    XClassGetVirtualFunc(machine, EXStateMachine_EndSelectTransitions,
                         void(*)(XStateMachine*, XEvent*))(machine, event);
}

void XStateMachine_beginMicrostep_base(XStateMachine* machine, XEvent* event)
{
    if (ISNULL(machine, "XStateMachine") || ISNULL(XClassGetVtable(machine), "Vtable"))
        return;
    XClassGetVirtualFunc(machine, EXStateMachine_BeginMicrostep,
                         void(*)(XStateMachine*, XEvent*))(machine, event);
}

void XStateMachine_endMicrostep_base(XStateMachine* machine, XEvent* event)
{
    if (ISNULL(machine, "XStateMachine") || ISNULL(XClassGetVtable(machine), "Vtable"))
        return;
    XClassGetVirtualFunc(machine, EXStateMachine_EndMicrostep,
                         void(*)(XStateMachine*, XEvent*))(machine, event);
}

XEventType XStateMachine_signalEventType_internal(void)
{
    return g_signalEventType;
}

XEventType XStateMachine_wrappedEventType_internal(void)
{
    return g_wrappedEventType;
}

static void VXStateMachine_SignalEvent_deinit(XStateMachine_SignalEvent* event)
{
    if (event && event->m_arguments) {
        XVarList_delete(event->m_arguments);
        event->m_arguments = NULL;
    }
    XVtableGetFunc(XEvent_class_init(), EXClass_Deinit,
                   void(*)(XEvent*))((XEvent*)event);
}

XVtable* XStateMachine_SignalEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachine_SignalEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_SignalEvent_deinit);
    return XVTABLE_DEFAULT;
}

void XStateMachine_SignalEvent_init(XStateMachine_SignalEvent* event)
{
    if (!event)
        return;
    XEvent_init((XEvent*)event, g_signalEventType);
    XClassSetVtable(event, XStateMachine_SignalEvent);
    event->m_sender = NULL;
    event->m_signal = 0;
    event->m_arguments = NULL;
}

XStateMachine_SignalEvent* XStateMachine_SignalEvent_create_ex(XMemoryType memory, XObject* sender,
                                                            size_t signal,
                                                            const XVarList* arguments)
{
    XStateMachine_SignalEvent* event = XMemory_malloc(sizeof(XStateMachine_SignalEvent), memory);
    if (!event)
        return NULL;
    XStateMachine_SignalEvent_init(event);
    event->m_sender = sender;
    event->m_signal = signal;
    event->m_arguments = XVarList_create_copy(arguments);
    Set_Class_Memory(event, memory); Set_Class_IsHeap(event, true);
    return event;
}

XObject* XStateMachine_SignalEvent_sender(const XStateMachine_SignalEvent* event)
{
    return event ? event->m_sender : NULL;
}

size_t XStateMachine_SignalEvent_signal(const XStateMachine_SignalEvent* event)
{
    return event ? event->m_signal : 0;
}

const XVarList* XStateMachine_SignalEvent_arguments_const(const XStateMachine_SignalEvent* event)
{
    return event ? event->m_arguments : NULL;
}

static void VXStateMachine_WrappedEvent_deinit(XStateMachine_WrappedEvent* event)
{
    if (event && event->m_event) {
        XClassDelete((XClass*)event->m_event);
        event->m_event = NULL;
    }
    XVtableGetFunc(XEvent_class_init(), EXClass_Deinit,
                   void(*)(XEvent*))((XEvent*)event);
}

static XVtable* XStateMachine_WrappedEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachine_WrappedEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_WrappedEvent_deinit);
    return XVTABLE_DEFAULT;
}

XStateMachine_WrappedEvent* XStateMachine_WrappedEvent_create_ex(XMemoryType memory, XObject* object, XEvent* event)
{
    if (!event)
        return NULL;
    XStateMachine_WrappedEvent* wrapped = XMemory_malloc(sizeof(XStateMachine_WrappedEvent), memory);
    if (!wrapped)
        return NULL;
    XEvent_init((XEvent*)wrapped, g_wrappedEventType);
    XClassGetVtable(wrapped) = XStateMachine_WrappedEvent_class_init();
    wrapped->m_object = object;
    wrapped->m_event = event;
    Set_Class_Memory(wrapped, memory); Set_Class_IsHeap(wrapped, true);
    return wrapped;
}

XObject* XStateMachine_WrappedEvent_object(const XStateMachine_WrappedEvent* event)
{
    return event ? event->m_object : NULL;
}

XEvent* XStateMachine_WrappedEvent_event(const XStateMachine_WrappedEvent* event)
{
    return event ? event->m_event : NULL;
}

void* XStateMachine_started_signal(XStateMachine* machine)
{
    XEmitSignal((XObject*)machine, XStateMachine_started_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XStateMachine_stopped_signal(XStateMachine* machine)
{
    XEmitSignal((XObject*)machine, XStateMachine_stopped_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XStateMachine_runningChanged_signal(XStateMachine* machine, bool running)
{
    XEmitSignal((XObject*)machine, XStateMachine_runningChanged_signal,
                XVarList_create(2, sizeof(bool), &running),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
