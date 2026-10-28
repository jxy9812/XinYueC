/**
 * @file XVfdLink.c
 * @brief 总线链路基类实现（计划表 + 从站健康 + 排拍定时器）。
 */
#include "XVfdLink.h"
#if XVFD_ON
#if XVFD_LINK_ON

#include "XVfdLink_Protected.h"
#include "XDateTime.h"
#include "XTimer.h"
#include <string.h>

/* =============== 内部工具 =============== */
static XVfdLinkSlave* linkFindSlave(XVfdLink* link, XObject* owner)
{
    int i;
    for (i = 0; i < XVFD_LINK_SLAVE_CAP; ++i) {
        if (link->m_slaves[i].m_used && link->m_slaves[i].m_owner == owner)
            return &link->m_slaves[i];
    }
    return NULL;
}

static XVfdLinkSlave* linkTakeSlaveSlot(XVfdLink* link)
{
    int i;
    for (i = 0; i < XVFD_LINK_SLAVE_CAP; ++i) {
        if (!link->m_slaves[i].m_used) return &link->m_slaves[i];
    }
    return NULL;
}

static void linkTickTimerStop(XVfdLink* link)
{
    if (!link) return;
    if (link->m_tickTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)link, link->m_tickTimer);
        link->m_tickTimer = XTIMER_INVALID_ID;
    }
}

/* =============== 虚函数实现 =============== */
static void VXVfdLink_timerEvent(XObject* obj, XTimerEvent* event)
{
    XVfdLink* link;
    if (!obj || !event) goto parent_call;
    link = (XVfdLink*)obj;
    if (XTimerEvent_timerId(event) != link->m_tickTimer) goto parent_call;
    XEvent_accept(event);
    XVfdPollScheduler_tick(&link->m_sched, XDateTime_currentMSecsSinceEpoch());
    return;

parent_call:
    /* 未知定时器 id 交父类链处理：第一参=直接父类（XClass_Parent 语义） */
    XClass_Parent(XObject, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

static void VXVfdLink_deinit(XObject* obj)
{
    XVfdLink* link = (XVfdLink*)obj;
    if (!link) return;
    linkTickTimerStop(link);
    XVfdPollScheduler_deinit(&link->m_sched);
    XClass_Parent(XObject, EXClass_Deinit, void (*)(XObject*))((XObject*)link);
}

/* =============== 虚函数表 =============== */
XVtable* XVfdLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdLink)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXVfdLink_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVfdLink_deinit);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdLink");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdLink);
    return XVTABLE_DEFAULT;
}

/* =============== 创建 / 初始化 =============== */
XVfdLink* XVfdLink_create_ex(XMemoryType memory)
{
    XVfdLink* link = (XVfdLink*)XMemory_malloc(sizeof(XVfdLink), memory);
    if (!link) return NULL;
    XVfdLink_init(link);
    Set_Class_Memory(link, memory);
    Set_Class_IsHeap(link, true);
    return link;
}

void XVfdLink_init(XVfdLink* link)
{
    if (!link) return;
    memset(link, 0, sizeof(XVfdLink));
    XObject_init(&link->m_class);
    XClassGetVtable(link) = XVfdLink_class_init();
    XVfdPollScheduler_init(&link->m_sched);
    link->m_tickTimer = XTIMER_INVALID_ID;
    link->m_tickMs = XVFD_LINK_DEFAULT_TICK_MS;
}

/* =============== 轮询计划登记 =============== */
int XVfdLink_registerPoll(XVfdLink* link, XObject* owner, void* userData,
                          XVfdPollDueFunc dueFunc, int unitId, uint32_t intervalMs)
{
    XVfdLinkSlave* slave;
    if (!link || !owner || !dueFunc) return -1;
    if (unitId < 0 || unitId > 247) return -1;
    slave = linkFindSlave(link, owner);
    if (!slave) {
        slave = linkTakeSlaveSlot(link);
        if (!slave) return -1;
        slave->m_owner = owner;
        slave->m_used = true;
    }
    slave->m_unitId = unitId;
    return XVfdPollScheduler_register(&link->m_sched, owner, userData, dueFunc, unitId, intervalMs);
}

void XVfdLink_unregisterPollOwner(XVfdLink* link, XObject* owner)
{
    XVfdLinkSlave* slave;
    if (!link || !owner) return;
    XVfdPollScheduler_unregisterOwner(&link->m_sched, owner);
    slave = linkFindSlave(link, owner);
    if (slave) memset(slave, 0, sizeof(XVfdLinkSlave));
}

bool XVfdLink_setPollInterval(XVfdLink* link, XObject* owner, uint32_t intervalMs)
{
    return link ? XVfdPollScheduler_setInterval(&link->m_sched, owner, intervalMs) : false;
}

bool XVfdLink_markPollSent(XVfdLink* link, XObject* owner)
{
    return link ? XVfdPollScheduler_markSent(&link->m_sched, owner,
                                             XDateTime_currentMSecsSinceEpoch()) : false;
}

bool XVfdLink_reportResult(XVfdLink* link, XObject* owner, int unitId, bool ok)
{
    XVfdLinkSlave* slave;
    bool muted;
    uint64_t now;
    if (!link || !owner) return false;
    now = XDateTime_currentMSecsSinceEpoch();
    muted = XVfdPollScheduler_reportResult(&link->m_sched, owner, ok, now);
    slave = linkFindSlave(link, owner);
    if (slave) {
        slave->m_unitId = unitId;
        if (ok) {
            if (slave->m_lost) {
                slave->m_lost = false;
                XVfdLink_slaveAlive_signal(link, unitId);
            }
        } else if (muted && !slave->m_lost) {
            slave->m_lost = true;
            XVfdLink_slaveLost_signal(link, unitId);
        }
    }
    return muted;
}

/* =============== 轮询总闸 =============== */
void XVfdLink_startPolling(XVfdLink* link)
{
    if (!link || link->m_polling) return;
    link->m_polling = true;
    if (link->m_tickTimer == XTIMER_INVALID_ID) {
        link->m_tickTimer = XObject_startTimer_ms((XObject*)link,
            (uint64_t)(link->m_tickMs ? link->m_tickMs : XVFD_LINK_DEFAULT_TICK_MS),
            XTimerType_CoarseTimer);
    }
}

void XVfdLink_stopPolling(XVfdLink* link)
{
    if (!link) return;
    link->m_polling = false;
    linkTickTimerStop(link);
}

bool XVfdLink_isPolling(const XVfdLink* link)
{
    return link ? link->m_polling : false;
}

void XVfdLink_setTickMs(XVfdLink* link, uint32_t tickMs)
{
    if (!link) return;
    if (tickMs < 10) tickMs = 10;
    link->m_tickMs = tickMs;
}

void XVfdLink_setBackoff(XVfdLink* link, uint32_t maxFails, uint32_t muteMs)
{
    if (link) XVfdPollScheduler_setBackoff(&link->m_sched, maxFails, muteMs);
}

/* =============== 信号 =============== */
void* XVfdLink_slaveLost_signal(XVfdLink* link, int unitId)
{
    if (link) {
        int copy = unitId;
        XVarList* args = XVarList_Create(XVar(int, copy));
        XObject_emitSignal((XObject*)link, (size_t)XVfdLink_slaveLost_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdLink_slaveLost_signal;
}

void* XVfdLink_slaveAlive_signal(XVfdLink* link, int unitId)
{
    if (link) {
        int copy = unitId;
        XVarList* args = XVarList_Create(XVar(int, copy));
        XObject_emitSignal((XObject*)link, (size_t)XVfdLink_slaveAlive_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdLink_slaveAlive_signal;
}

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
