#include "XStateMachine.h"

#include "XCoreApplication.h"
#include "XMemory.h"
#include "XStateMachine_p.h"

#include <stdlib.h>

XCLASS_DEFINE_BEGING(XStateMachine_SignalEventClass)
XCLASS_DEFINE_EXTEND_END(XStateMachine_SignalEventClass, XEvent);
XCLASS_DEFINE_BEGING(XStateMachine_WrappedEventClass)
XCLASS_DEFINE_EXTEND_END(XStateMachine_WrappedEventClass, XEvent);

typedef struct XStateMachine_SignalConnection {
    XObject m_class;
    XStateMachine* m_machine;
    const XObject* m_sender;
    size_t m_signal;
    XConnection* m_connection;
    size_t m_referenceCount;
} XStateMachine_SignalConnection;

static XEventType g_processEventType = XEVENT_TYPE_NONE;
static XEventType g_zeroDelayedEventType = XEVENT_TYPE_NONE;
static XEventType g_signalEventType = XEVENT_TYPE_NONE;
static XEventType g_wrappedEventType = XEVENT_TYPE_NONE;

static bool VXStateMachine_event(XStateMachine* machine, XEvent* event);
static bool VXStateMachine_eventFilter(XStateMachine* machine, XObject* watched, XEvent* event);
static void VXStateMachine_timerEvent(XStateMachine* machine, XTimerEvent* event);
static void VXStateMachine_deinit(XStateMachine* machine);
static void VXStateMachine_hook(XStateMachine* machine, XEvent* event);

static void XStateMachine_startInternal(XStateMachine* machine);
static void XStateMachine_process(XStateMachine* machine);
static bool XStateMachine_microstep(XStateMachine* machine, XEvent* event, XVector* transitions);

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
        XClass_delete_base((XClass*)record);
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

static int XStateMachine_stateDepth(const XAbstractState* state)
{
    int depth = 0;
    for (const XAbstractState* current = state; current && current->m_parentState;
         current = (const XAbstractState*)current->m_parentState) {
        ++depth;
    }
    return depth;
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

static void XStateMachine_sortStates(XVector* states, bool deepestFirst)
{
    if (!states)
        return;
    for (int64_t i = 1; i < (int64_t)XVector_size_base((const XContainer*)states); ++i) {
        XAbstractState* key = XVector_At_Base(states, i, XAbstractState*);
        int keyDepth = XStateMachine_stateDepth(key);
        int64_t j = i - 1;
        while (j >= 0) {
            XAbstractState* previous = XVector_At_Base(states, j, XAbstractState*);
            int previousDepth = XStateMachine_stateDepth(previous);
            bool move = deepestFirst ? previousDepth < keyDepth : previousDepth > keyDepth;
            if (!move)
                break;
            XVector_At_Base(states, j + 1, XAbstractState*) = previous;
            --j;
        }
        XVector_At_Base(states, j + 1, XAbstractState*) = key;
    }
}

static void XStateMachine_clearOwnedEvents(XVector* queue)
{
    if (!queue)
        return;
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)queue); ++i) {
        XEvent* event = XVector_At_Base(queue, i, XEvent*);
        if (event)
            XEvent_delete_base((XClass*)event);
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
            XEvent_delete_base((XClass*)delayed->m_event);
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
            XEvent_delete_base((XClass*)delayed.m_event);
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
    machine->m_error = error;
    switch (error) {
    case XStateMachine_NoInitialStateError:
        machine->m_errorString = "Missing initial state in compound state";
        break;
    case XStateMachine_NoDefaultStateInHistoryStateError:
        machine->m_errorString = "Missing default state in history state";
        break;
    case XStateMachine_NoCommonAncestorForTransitionError:
        machine->m_errorString = "No common ancestor for transition source and targets";
        break;
    case XStateMachine_StateMachineChildModeSetToParallelError:
        machine->m_errorString = "Invalid state machine child mode";
        break;
    default:
        machine->m_errorString = "Unknown state machine error";
        break;
    }

    XAbstractState* errorState = XStateMachine_findErrorState(context);
    machine->m_pendingErrorState = errorState;
    if (!errorState)
        machine->m_stopRequested = true;
    return errorState;
}

static XVector* XStateMachine_effectiveTargets(XStateMachine* machine,
                                               XAbstractTransition* transition,
                                               bool executeHistoryDefault,
                                               XEvent* event)
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
        if (executeHistoryDefault)
            XAbstractTransition_onTransition_base(fallback, event);
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

    if (transition->m_transitionType == XAbstractTransition_InternalTransition
        && XStateMachine_isGroup(source)
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

    XAbstractState* candidate = source->m_kind == XAbstractState_StateMachine
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
    XVector* targets = XStateMachine_effectiveTargets(machine, transition, false, NULL);
    XAbstractState* domain = XStateMachine_transitionDomain(machine, transition, targets);
    if (domain) {
        for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
            XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
            if (XStateMachine_isDescendant(state, domain))
                XStateMachine_vectorAppendUnique(exitSet, state);
        }
    }
    if (targets)
        XVector_delete_base((XClass*)targets);
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
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* first = XVector_At_Base(transitions, i, XAbstractTransition*);
        XVector* firstExit = XStateMachine_exitSetForTransition(machine, first);
        for (int64_t j = i + 1; j < (int64_t)XVector_size_base((const XContainer*)transitions);) {
            XAbstractTransition* second = XVector_At_Base(transitions, j, XAbstractTransition*);
            XVector* secondExit = XStateMachine_exitSetForTransition(machine, second);
            if (!XStateMachine_vectorsIntersect(firstExit, secondExit)) {
                XVector_delete_base((XClass*)secondExit);
                ++j;
                continue;
            }

            if (XStateMachine_isDescendant((XAbstractState*)second->m_sourceState,
                                           (XAbstractState*)first->m_sourceState)) {
                XVector_removeAt_base(transitions, i);
                XVector_delete_base((XClass*)secondExit);
                --i;
                break;
            }

            XVector_removeAt_base(transitions, j);
            XVector_delete_base((XClass*)secondExit);
        }
        XVector_delete_base((XClass*)firstExit);
    }
}

static XVector* XStateMachine_selectTransitions(XStateMachine* machine, XEvent* event)
{
    XVector* enabled = XVector_Create(XAbstractTransition*);
    if (!enabled)
        return NULL;

    XStateMachine_beginSelectTransitions_base(machine, event);
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* atomic = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        if (!XStateMachine_isAtomic(machine, atomic))
            continue;

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
            XVector_delete_base((XClass*)local);
    }
    XStateMachine_sortStates(exitSet, true);
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
                XStateMachine_unregisterTransition_internal(machine, transition);
            }
        }
        XAbstractState_onExit_base(state, event);
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
        XVector_delete_base((XClass*)path);
        XStateMachine_setError(machine,
                               XStateMachine_NoCommonAncestorForTransitionError,
                               target);
        return false;
    }
    for (int64_t i = (int64_t)XVector_size_base((const XContainer*)path) - 1; i >= 0; --i) {
        XAbstractState* state = XVector_At_Base(path, i, XAbstractState*);
        XStateMachine_vectorAppendUnique(entries, state);
    }
    XVector_delete_base((XClass*)path);
    return true;
}

static bool XStateMachine_addDefaultEntry(XStateMachine* machine,
                                          XState* owner,
                                          XAbstractState* initial,
                                          XVector* entries,
                                          XAbstractState** errorTarget,
                                          XEvent* event)
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
    if (fallback)
        XAbstractTransition_onTransition_base(fallback, event);
    return true;
}

static bool XStateMachine_expandDefaultEntries(XStateMachine* machine,
                                               XVector* entries,
                                               XAbstractState** errorTarget,
                                               XEvent* event)
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
                                machine, state, child, entries, errorTarget, event)) {
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
                    machine, state, initial, entries, errorTarget, event)) {
                return false;
            }
            changed = true;
        }
    }
    return true;
}

static XVector* XStateMachine_computeEntrySet(XStateMachine* machine,
                                              XEvent* event,
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
            XVector* targets = XStateMachine_effectiveTargets(
                machine, transition, true, event);
            XAbstractState* domain = XStateMachine_transitionDomain(machine, transition, targets);
            if (machine->m_stopRequested && machine->m_error != XStateMachine_NoError) {
                if (targets)
                    XVector_delete_base((XClass*)targets);
                continue;
            }
            for (int64_t j = 0; targets && j < (int64_t)XVector_size_base((const XContainer*)targets); ++j) {
                XAbstractState* target = XVector_At_Base(targets, j, XAbstractState*);
                XStateMachine_addEntryPath(machine, target, domain, entries);
            }
            if (targets)
                XVector_delete_base((XClass*)targets);
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
            machine, entries, &errorTarget, event) && errorTarget) {
        machine->m_pendingErrorState = NULL;
        XVector_clear_base((XContainer*)entries);
        XStateMachine_addEntryPath(machine, errorTarget, (XAbstractState*)machine, entries);
        errorTarget = NULL;
        XStateMachine_expandDefaultEntries(machine, entries, &errorTarget, event);
    }
    XStateMachine_sortStates(entries, false);
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

static void XStateMachine_enterStates(XStateMachine* machine, XEvent* event, XVector* entries)
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

        XAbstractState_onEntry_base(state, event);
        XAbstractState_entered_signal(state);
        XAbstractState_setActive_internal(state, true);
        if (XStateMachine_isGroup(state))
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
    XVector* exitSet = XStateMachine_computeExitSet(machine, transitions);
    XStateMachine_exitStates(machine, event, exitSet);

    for (int64_t i = 0; transitions && i < (int64_t)XVector_size_base((const XContainer*)transitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(transitions, i, XAbstractTransition*);
        XAbstractTransition_onTransition_base(transition, event);
        XAbstractTransition_triggered_signal(transition);
    }

    XVector* entries = XStateMachine_computeEntrySet(machine, event, transitions, NULL);
    XStateMachine_enterStates(machine, event, entries);
    bool finished = XStateMachine_emitFinishedStates(machine, entries);

    if (exitSet)
        XVector_delete_base((XClass*)exitSet);
    if (entries)
        XVector_delete_base((XClass*)entries);
    return finished;
}

static void XStateMachine_unregisterConfigurationTransitions(XStateMachine* machine)
{
    if (!machine)
        return;
    const XVector* rootTransitions = XState_transitions_const((XState*)machine);
    for (int64_t i = 0; rootTransitions && i < (int64_t)XVector_size_base((const XContainer*)rootTransitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(rootTransitions, i, XAbstractTransition*);
        XStateMachine_unregisterTransition_internal(machine, transition);
    }
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        if (!XStateMachine_isGroup(state))
            continue;
        const XVector* transitions = XState_transitions_const((XState*)state);
        for (int64_t j = 0; transitions && j < (int64_t)XVector_size_base((const XContainer*)transitions); ++j) {
            XAbstractTransition* transition = XVector_At_Base(transitions, j, XAbstractTransition*);
            XStateMachine_unregisterTransition_internal(machine, transition);
        }
    }
}

static void XStateMachine_finish(XStateMachine* machine)
{
    machine->m_state = XStateMachine_NotRunning;
    machine->m_processing = false;
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_unregisterConfigurationTransitions(machine);
    XState_finished_signal((XState*)machine);
    XStateMachine_runningChanged_signal(machine, false);
}

static void XStateMachine_completeStop(XStateMachine* machine)
{
    machine->m_stopRequested = false;
    machine->m_state = XStateMachine_NotRunning;
    machine->m_processing = false;
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_unregisterConfigurationTransitions(machine);
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
            XVector_delete_base((XClass*)enabled);
            XEvent_deinit_base((XClass*)&nullEvent);
            continue;
        }
        if (enabled)
            XVector_delete_base((XClass*)enabled);
        XEvent_deinit_base((XClass*)&nullEvent);

        XEvent* event = NULL;
        do {
            event = XStateMachine_dequeue(machine->m_internalEventQueue);
            if (!event)
                event = XStateMachine_dequeue(machine->m_externalEventQueue);
            if (!event)
                break;
            enabled = XStateMachine_selectTransitions(machine, event);
            if (enabled && XVector_isEmpty_base((const XContainer*)enabled)) {
                XVector_delete_base((XClass*)enabled);
                enabled = NULL;
                XEvent_delete_base((XClass*)event);
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
        XVector_delete_base((XClass*)enabled);
        XEvent_delete_base((XClass*)event);
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

    XStateMachine_unregisterConfigurationTransitions(machine);
    for (int64_t i = 0; i < (int64_t)XVector_size_base((const XContainer*)machine->m_configuration); ++i) {
        XAbstractState* state = XVector_At_Base(machine->m_configuration, i, XAbstractState*);
        XAbstractState_setActive_internal(state, false);
    }
    XVector_clear_base((XContainer*)machine->m_configuration);
    XStateMachine_clearOwnedEvents(machine->m_internalEventQueue);
    XStateMachine_clearOwnedEvents(machine->m_externalEventQueue);
    XStateMachine_clearHistoryRecursive((XState*)machine);
    XStateMachine_clearError(machine);

    machine->m_state = XStateMachine_Running;
    const XVector* rootTransitions = XState_transitions_const((XState*)machine);
    for (int64_t i = 0; rootTransitions && i < (int64_t)XVector_size_base((const XContainer*)rootTransitions); ++i) {
        XAbstractTransition* transition = XVector_At_Base(rootTransitions, i, XAbstractTransition*);
        XStateMachine_registerTransition_internal(machine, transition);
    }

    XVector* entries = XVector_Create(XAbstractState*);
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
                    machine, root, child, entries, &ignoredErrorTarget, &nullEvent);
            } else if (child) {
                XStateMachine_addEntryPath(
                    machine, child, (XAbstractState*)machine, entries);
            }
        }
    } else if (root->m_initialState) {
        XAbstractState* ignoredErrorTarget = NULL;
        XStateMachine_addDefaultEntry(
            machine, root, root->m_initialState, entries,
            &ignoredErrorTarget, &nullEvent);
    }

    XAbstractState* errorTarget = NULL;
    if (!XStateMachine_expandDefaultEntries(
            machine, entries, &errorTarget, &nullEvent) && errorTarget) {
        machine->m_pendingErrorState = NULL;
        XVector_clear_base((XContainer*)entries);
        XStateMachine_addEntryPath(
            machine, errorTarget, (XAbstractState*)machine, entries);
        errorTarget = NULL;
        XStateMachine_expandDefaultEntries(
            machine, entries, &errorTarget, &nullEvent);
    }
    XStateMachine_sortStates(entries, false);
    XStateMachine_enterStates(machine, &nullEvent, entries);
    bool finished = XStateMachine_emitFinishedStates(machine, entries);
    XEvent_deinit_base((XClass*)&nullEvent);
    XVector_delete_base((XClass*)entries);

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
        XEvent_delete_base((XClass*)clone);
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
            XEvent_delete_base((XClass*)delayed.m_event);
        break;
    }
    XEvent_accept((XEvent*)event);
}

static void VXStateMachine_deinit(XStateMachine* machine)
{
    if (!machine)
        return;
    XStateMachine_unregisterConfigurationTransitions(machine);
    XStateMachine_clearSignalConnections(machine);
    XStateMachine_cancelAllDelayedEvents(machine);
    XStateMachine_clearOwnedEvents(machine->m_internalEventQueue);
    XStateMachine_clearOwnedEvents(machine->m_externalEventQueue);
    XVector_delete_base((XClass*)machine->m_configuration);
    XVector_delete_base((XClass*)machine->m_internalEventQueue);
    XVector_delete_base((XClass*)machine->m_externalEventQueue);
    XVector_delete_base((XClass*)machine->m_delayedEvents);
    XVector_delete_base((XClass*)machine->m_signalConnections);
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
	XCLASS_SET_CLASS_NAME_DEFAULT("XStateMachine");
    XVTABLE_INHERIT_XCLASS(XState);
    void* table[] = {
        VXStateMachine_hook, VXStateMachine_hook,
        VXStateMachine_hook, VXStateMachine_hook
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXStateMachine_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VXStateMachine_eventFilter);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXStateMachine_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_deinit);
    return XVTABLE_DEFAULT;
}

XStateMachine* XStateMachine_create(void)
{
    return XStateMachine_create_ex(XState_ExclusiveStates);
}

XStateMachine* XStateMachine_create_ex(XState_ChildMode childMode)
{
    XStateMachine* machine = XNew(XStateMachine);
    if (!machine)
        return NULL;
    XStateMachine_init_ex(machine, childMode);
    Set_Class_MemoryFree(machine, XFree_System);
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
    machine->m_internalEventQueue = XVector_Create(XEvent*);
    machine->m_externalEventQueue = XVector_Create(XEvent*);
    machine->m_delayedEvents = XVector_Create(XStateMachine_DelayedEvent);
    machine->m_signalConnections = XVector_Create(XStateMachine_SignalConnection*);
    machine->m_pendingErrorState = NULL;
    machine->m_state = XStateMachine_NotRunning;
    machine->m_error = XStateMachine_NoError;
    machine->m_errorString = NULL;
    machine->m_nextDelayedEventId = 0;
    machine->m_processing = false;
    machine->m_processingScheduled = false;
    machine->m_stopRequested = false;
}

bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state)
{
    return machine && state && XState_addChild_internal((XState*)machine, state);
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

const char* XStateMachine_errorString(const XStateMachine* machine)
{
    return machine ? machine->m_errorString : NULL;
}

void XStateMachine_clearError(XStateMachine* machine)
{
    if (!machine)
        return;
    machine->m_error = XStateMachine_NoError;
    machine->m_errorString = NULL;
    machine->m_pendingErrorState = NULL;
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
        XEvent_delete_base((XClass*)event);
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
        XEvent_delete_base((XClass*)delayed.m_event);
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

    XStateMachine_SignalConnection* record = XNew(XStateMachine_SignalConnection);
    if (!record)
        return;
    XObject_init((XObject*)record);
    Set_Class_MemoryFree(record, XFree_System);
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
        XClass_delete_base((XClass*)record);
        return;
    }

    transition->m_connection = record->m_connection;
    transition->m_registeredMachine = machine;
}

void XStateMachine_unregisterSignalTransition_internal(XStateMachine* machine,
                                                        XSignalTransition* transition)
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
        XClass_delete_base((XClass*)record);
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

void XStateMachine_unregisterTransition_internal(XStateMachine* machine, XAbstractTransition* transition)
{
    (void)machine;
    if (!transition)
        return;
    switch (transition->m_kind) {
    case XAbstractTransition_SignalTransition:
        XSignalTransition_unregister_internal((XSignalTransition*)transition);
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

static XVtable* XStateMachine_SignalEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachine_SignalEventClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XStateMachine_SignalEvent");
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_SignalEvent_deinit);
    return XVTABLE_DEFAULT;
}

XStateMachine_SignalEvent* XStateMachine_SignalEvent_create(XObject* sender,
                                                            size_t signal,
                                                            const XVarList* arguments)
{
    XStateMachine_SignalEvent* event = XNew(XStateMachine_SignalEvent);
    if (!event)
        return NULL;
    XEvent_init((XEvent*)event, g_signalEventType);
    XClassGetVtable(event) = XStateMachine_SignalEvent_class_init();
    event->m_sender = sender;
    event->m_signal = signal;
    event->m_arguments = XVarList_create_copy(arguments);
    Set_Class_MemoryFree(event, XFree_System);
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
        XEvent_delete_base((XClass*)event->m_event);
        event->m_event = NULL;
    }
    XVtableGetFunc(XEvent_class_init(), EXClass_Deinit,
                   void(*)(XEvent*))((XEvent*)event);
}

static XVtable* XStateMachine_WrappedEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachine_WrappedEventClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XStateMachine_WrappedEvent");
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStateMachine_WrappedEvent_deinit);
    return XVTABLE_DEFAULT;
}

XStateMachine_WrappedEvent* XStateMachine_WrappedEvent_create(XObject* object, XEvent* event)
{
    if (!event)
        return NULL;
    XStateMachine_WrappedEvent* wrapped = XNew(XStateMachine_WrappedEvent);
    if (!wrapped)
        return NULL;
    XEvent_init((XEvent*)wrapped, g_wrappedEventType);
    XClassGetVtable(wrapped) = XStateMachine_WrappedEvent_class_init();
    wrapped->m_object = object;
    wrapped->m_event = event;
    Set_Class_MemoryFree(wrapped, XFree_System);
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
