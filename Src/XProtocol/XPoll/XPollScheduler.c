/**
 * @file XPollScheduler.c
 * @brief 通用总线轮询计划表实现（传输无关纯逻辑；时刻一律由调用方注入 nowMs）。
 */
#include "XPollScheduler.h"
#if XPOLL_SCHEDULER_ON

#include <string.h>

void XPollScheduler_init(XPollScheduler* sched)
{
    if (!sched) return;
    memset(sched, 0, sizeof(XPollScheduler));
    sched->m_maxFails = XPOLL_SCHED_DEFAULT_MAX_FAILS;
    sched->m_muteMs = XPOLL_SCHED_DEFAULT_MUTE_MS;
}

void XPollScheduler_deinit(XPollScheduler* sched)
{
    if (!sched) return;
    memset(sched, 0, sizeof(XPollScheduler));
}

/* =============== 内部查找 =============== */

/** @brief owner 键查找（XVfd 语义；命中首个在用同 owner 条目）。 */
static XPollEntry* schedFind(XPollScheduler* sched, XObject* owner)
{
    int i;
    for (i = 0; i < XPOLL_SCHED_MAX_ENTRIES; ++i) {
        if (sched->m_entries[i].m_active && sched->m_entries[i].m_owner == owner)
            return &sched->m_entries[i];
    }
    return NULL;
}

/** @brief unit 键查找（只命中 owner 为空的簿记条目，避免与 owner 条目串位）。 */
static XPollEntry* schedFindByKey(XPollScheduler* sched, int key)
{
    int i;
    for (i = 0; i < XPOLL_SCHED_MAX_ENTRIES; ++i) {
        if (sched->m_entries[i].m_active && sched->m_entries[i].m_owner == NULL &&
            sched->m_entries[i].m_unitId == key)
            return &sched->m_entries[i];
    }
    return NULL;
}

/* =============== 登记（owner 键） =============== */

int XPollScheduler_register(XPollScheduler* sched, XObject* owner, void* userData,
                            XPollDueFunc dueFunc, int unitId, uint32_t intervalMs)
{
    int i;
    if (!sched || !owner || !dueFunc) return -1;
    if (schedFind(sched, owner)) return -1; /* 同设备重复登记拒绝 */
    for (i = 0; i < XPOLL_SCHED_MAX_ENTRIES; ++i) {
        XPollEntry* e = &sched->m_entries[i];
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

void XPollScheduler_unregisterOwner(XPollScheduler* sched, XObject* owner)
{
    int i;
    if (!sched || !owner) return;
    for (i = 0; i < XPOLL_SCHED_MAX_ENTRIES; ++i) {
        XPollEntry* e = &sched->m_entries[i];
        if (!e->m_active || e->m_owner != owner) continue;
        memset(e, 0, sizeof(XPollEntry));
        if (sched->m_count > 0) --sched->m_count;
    }
}

bool XPollScheduler_setInterval(XPollScheduler* sched, XObject* owner, uint32_t intervalMs)
{
    XPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    e->m_intervalMs = intervalMs;
    return true;
}

/* =============== 登记（unit 键扩展） =============== */

int XPollScheduler_registerKey(XPollScheduler* sched, int key, uint32_t intervalMs)
{
    int i;
    if (!sched || key < 0) return -1;
    if (schedFindByKey(sched, key)) return -1; /* 同键重复登记拒绝 */
    for (i = 0; i < XPOLL_SCHED_MAX_ENTRIES; ++i) {
        XPollEntry* e = &sched->m_entries[i];
        if (e->m_active) continue;
        e->m_owner = NULL;
        e->m_userData = NULL;
        e->m_due = NULL;          /* 纯簿记条目：tick 恒跳过 */
        e->m_unitId = key;
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

/* =============== 节拍与健康簿记 =============== */

bool XPollScheduler_markSent(XPollScheduler* sched, XObject* owner, uint64_t nowMs)
{
    XPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    e->m_lastSentMs = nowMs;
    return true;
}

bool XPollScheduler_reportResult(XPollScheduler* sched, XObject* owner, bool ok, uint64_t nowMs)
{
    XPollEntry* e = sched ? schedFind(sched, owner) : NULL;
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

bool XPollScheduler_reportResultKey(XPollScheduler* sched, int key, bool ok, uint64_t nowMs)
{
    XPollEntry* e = sched ? schedFindByKey(sched, key) : NULL;
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

bool XPollScheduler_tick(XPollScheduler* sched, uint64_t nowMs)
{
    int i;
    int scanned;
    if (!sched || sched->m_count <= 0) return false;
    for (scanned = 0; scanned < XPOLL_SCHED_MAX_ENTRIES; ++scanned) {
        XPollEntry* e;
        sched->m_cursor = (sched->m_cursor + 1) % XPOLL_SCHED_MAX_ENTRIES;
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

bool XPollScheduler_isMuted(XPollScheduler* sched, XObject* owner, uint64_t nowMs)
{
    XPollEntry* e = sched ? schedFind(sched, owner) : NULL;
    if (!e) return false;
    return e->m_muteUntilMs != 0 && nowMs < e->m_muteUntilMs;
}

bool XPollScheduler_isMutedKey(XPollScheduler* sched, int key, uint64_t nowMs)
{
    XPollEntry* e = sched ? schedFindByKey(sched, key) : NULL;
    if (!e) return false;
    return e->m_muteUntilMs != 0 && nowMs < e->m_muteUntilMs;
}

bool XPollScheduler_isLostKey(XPollScheduler* sched, int key)
{
    XPollEntry* e = sched ? schedFindByKey(sched, key) : NULL;
    if (!e) return false;
    /* 静默中或静默到期未探活成功（muteUntil 尚未清零）均视为失联。 */
    return e->m_muteUntilMs != 0;
}

void XPollScheduler_setBackoff(XPollScheduler* sched, uint32_t maxFails, uint32_t muteMs)
{
    if (!sched) return;
    if (maxFails >= 1) sched->m_maxFails = maxFails;
    if (muteMs >= 1000) sched->m_muteMs = muteMs;
}

#endif /* XPOLL_SCHEDULER_ON */
