/**
 * @file XVfdPollScheduler.c
 * @brief 总线轮询计划表实现（传输无关；由 XVfdLink 持有并驱动）。
 */
#include "XVfdPollScheduler.h"
#if XVFD_ON
#if XVFD_LINK_ON

#include "XDateTime.h"
#include <string.h>

void XVfdPollScheduler_init(XVfdPollScheduler* sched)
{
    if (!sched) return;
    memset(sched, 0, sizeof(XVfdPollScheduler));
    sched->m_maxFails = XVFD_POLLSCHED_DEFAULT_MAX_FAILS;
    sched->m_muteMs = XVFD_POLLSCHED_DEFAULT_MUTE_MS;
}

void XVfdPollScheduler_deinit(XVfdPollScheduler* sched)
{
    if (!sched) return;
    memset(sched, 0, sizeof(XVfdPollScheduler));
}

static XVfdPollEntry* schedFind(XVfdPollScheduler* sched, XObject* owner)
{
    int i;
    for (i = 0; i < XVFD_POLLSCHED_MAX_ENTRIES; ++i) {
        if (sched->m_entries[i].m_active && sched->m_entries[i].m_owner == owner)
            return &sched->m_entries[i];
    }
    return NULL;
}

int XVfdPollScheduler_register(XVfdPollScheduler* sched, XObject* owner, void* userData,
                               XVfdPollDueFunc dueFunc, int unitId, uint32_t intervalMs)
{
    int i;
    if (!sched || !owner || !dueFunc) return -1;
    if (schedFind(sched, owner)) return -1; /* 同设备重复登记拒绝 */
    for (i = 0; i < XVFD_POLLSCHED_MAX_ENTRIES; ++i) {
        XVfdPollEntry* e = &sched->m_entries[i];
        if (e->m_active) continue;
        e->m_owner = owner;
        e->m_userData = userData;
        e->m_due = dueFunc;
        e->m_unitId = unitId;
        e->m_intervalMs = intervalMs;
        e->m_lastSentMs = 0;
        e->m_failCount = 0;
        e->m_muteUntilMs = 0;
        e->m_active = true;
        ++sched->m_count;
        return i;
    }
    return -1;
}

void XVfdPollScheduler_unregisterOwner(XVfdPollScheduler* sched, XObject* owner)
{
    int i;
    if (!sched || !owner) return;
    for (i = 0; i < XVFD_POLLSCHED_MAX_ENTRIES; ++i) {
        XVfdPollEntry* e = &sched->m_entries[i];
        if (!e->m_active || e->m_owner != owner) continue;
        memset(e, 0, sizeof(XVfdPollEntry));
        if (sched->m_count > 0) --sched->m_count;
    }
}

bool XVfdPollScheduler_setInterval(XVfdPollScheduler* sched, XObject* owner, uint32_t intervalMs)
{
    XVfdPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    e->m_intervalMs = intervalMs;
    return true;
}

bool XVfdPollScheduler_markSent(XVfdPollScheduler* sched, XObject* owner, uint64_t nowMs)
{
    XVfdPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    e->m_lastSentMs = nowMs;
    return true;
}

bool XVfdPollScheduler_reportResult(XVfdPollScheduler* sched, XObject* owner, bool ok, uint64_t nowMs)
{
    XVfdPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    if (ok) {
        e->m_failCount = 0;
        e->m_muteUntilMs = 0;
        return false;
    }
    ++e->m_failCount;
    if (e->m_failCount >= sched->m_maxFails) {
        e->m_muteUntilMs = nowMs + (uint64_t)sched->m_muteMs;
        e->m_failCount = 0;
        return true; /* 进入静默退避 */
    }
    return false;
}

bool XVfdPollScheduler_tick(XVfdPollScheduler* sched, uint64_t nowMs)
{
    int i;
    int scanned;
    if (!sched || sched->m_count <= 0) return false;
    for (scanned = 0; scanned < XVFD_POLLSCHED_MAX_ENTRIES; ++scanned) {
        XVfdPollEntry* e;
        sched->m_cursor = (sched->m_cursor + 1) % XVFD_POLLSCHED_MAX_ENTRIES;
        e = &sched->m_entries[sched->m_cursor];
        if (!e->m_active || e->m_intervalMs == 0 || !e->m_due) continue;
        if (e->m_muteUntilMs != 0 && nowMs < e->m_muteUntilMs) continue;
        if (e->m_lastSentMs != 0 && nowMs - e->m_lastSentMs < (uint64_t)e->m_intervalMs) continue;
        /* 静默到期首拍：解除静默（探活） */
        if (e->m_muteUntilMs != 0 && nowMs >= e->m_muteUntilMs) e->m_muteUntilMs = 0;
        e->m_due(e->m_owner, e->m_userData);
        return true;
    }
    return false;
}

bool XVfdPollScheduler_isMuted(XVfdPollScheduler* sched, XObject* owner, uint64_t nowMs)
{
    XVfdPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    return e->m_muteUntilMs != 0 && nowMs < e->m_muteUntilMs;
}

void XVfdPollScheduler_setBackoff(XVfdPollScheduler* sched, uint32_t maxFails, uint32_t muteMs)
{
    if (!sched) return;
    if (maxFails >= 1) sched->m_maxFails = maxFails;
    if (muteMs >= 1000) sched->m_muteMs = muteMs;
}

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
