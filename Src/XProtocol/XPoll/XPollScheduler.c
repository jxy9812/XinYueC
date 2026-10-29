/**
 * @file XPollScheduler.c
 * @brief 通用总线轮询计划表实现（传输无关纯逻辑；时刻一律由调用方注入 nowMs）。
 */
#include "XPollScheduler.h"
#if XPOLL_SCHEDULER_ON

#include <string.h>
#include "XVector.h"

/* =============== 内部工具 =============== */

/** @brief 在用条目数（动态表实时口径；未建表=0）。 */
static int schedCount(const XPollScheduler* sched)
{
    return (sched && sched->m_entries) ? (int)XVector_size_base(sched->m_entries) : 0;
}

/** @brief 惰性建表：零初始化内嵌（未经 init）也可直接登记。 */
static bool schedEnsureTable(XPollScheduler* sched)
{
    if (sched->m_entries) return true;
    sched->m_entries = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         sizeof(XPollEntry), false);
    return sched->m_entries != NULL;
}

/**
 * @brief 尾交换删除 index：末条移入被删位后弹出（O(1)，不保序）。
 * @note  不保序是动态化的代价：条目下标只在登记时刻有效。游标随删除
 *        修正（删除位之后左移一位、越界收敛 0），轮转错峰语义不变。
 */
static void schedEraseAt(XPollScheduler* sched, int index)
{
    const int last = (int)XVector_size_base(sched->m_entries) - 1;
    if (index < 0 || index > last) return;
    if (index != last) {
        *(XPollEntry*)XVector_at_base(sched->m_entries, index) =
            *(const XPollEntry*)XVector_at_base(sched->m_entries, last);
    }
    XVector_pop_back_base(sched->m_entries);
    if (sched->m_cursor > index) --sched->m_cursor;
    if (sched->m_cursor < 0 || sched->m_cursor >= last) sched->m_cursor = 0;
}

/* =============== 生命周期 =============== */

void XPollScheduler_init(XPollScheduler* sched)
{
    if (!sched) return;
    if (sched->m_entries) {
        XClassDelete(sched->m_entries);
        sched->m_entries = NULL;
    }
    memset(sched, 0, sizeof(XPollScheduler));
    sched->m_maxFails = XPOLL_SCHED_DEFAULT_MAX_FAILS;
    sched->m_muteMs = XPOLL_SCHED_DEFAULT_MUTE_MS;
}

void XPollScheduler_deinit(XPollScheduler* sched)
{
    if (!sched) return;
    if (sched->m_entries) {
        XClassDelete(sched->m_entries);
        sched->m_entries = NULL;
    }
    memset(sched, 0, sizeof(XPollScheduler));
}

/* =============== 内部查找 =============== */

/** @brief owner 键查找（XVfd 语义；命中首个同 owner 条目）。 */
static XPollEntry* schedFind(XPollScheduler* sched, XObject* owner)
{
    int i;
    int n;
    if (!owner) return NULL; /* key 簿记条目 owner 为空，不在此命中 */
    n = schedCount(sched);
    for (i = 0; i < n; ++i) {
        XPollEntry* e = (XPollEntry*)XVector_at_base(sched->m_entries, i);
        if (e->m_owner == owner) return e;
    }
    return NULL;
}

/** @brief unit 键查找（只命中 owner 为空的簿记条目，避免与 owner 条目串位）。 */
static XPollEntry* schedFindByKey(XPollScheduler* sched, int key)
{
    int i;
    int n = schedCount(sched);
    for (i = 0; i < n; ++i) {
        XPollEntry* e = (XPollEntry*)XVector_at_base(sched->m_entries, i);
        if (e->m_owner == NULL && e->m_unitId == key) return e;
    }
    return NULL;
}

/* =============== 登记（owner 键） =============== */

int XPollScheduler_register(XPollScheduler* sched, XObject* owner, void* userData,
                            XPollDueFunc dueFunc, int unitId, uint32_t intervalMs)
{
    XPollEntry e;
    if (!sched || !owner || !dueFunc) return -1;
    if (schedFind(sched, owner)) return -1; /* 同设备重复登记拒绝 */
    if (!schedEnsureTable(sched)) return -1;
    memset(&e, 0, sizeof(e));
    e.m_owner = owner;
    e.m_userData = userData;
    e.m_due = dueFunc;
    e.m_unitId = unitId;
    e.m_intervalMs = intervalMs;
    if (!XVector_push_back_1_base(sched->m_entries, &e)) return -1;
    return (int)XVector_size_base(sched->m_entries) - 1;
}

void XPollScheduler_unregisterOwner(XPollScheduler* sched, XObject* owner)
{
    int i;
    if (!sched || !owner) return;
    /* 倒序删 + 尾交换：换入位（原末位）必已访问过，不漏条目。 */
    for (i = schedCount(sched) - 1; i >= 0; --i) {
        const XPollEntry* e = (const XPollEntry*)XVector_at_base(sched->m_entries, i);
        if (e && e->m_owner == owner) schedEraseAt(sched, i);
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
    XPollEntry e;
    if (!sched || key < 0) return -1;
    if (schedFindByKey(sched, key)) return -1; /* 同键重复登记拒绝 */
    if (!schedEnsureTable(sched)) return -1;
    memset(&e, 0, sizeof(e));
    e.m_unitId = key;             /* owner/due 恒空：纯簿记条目，tick 恒跳过 */
    e.m_intervalMs = intervalMs;
    if (!XVector_push_back_1_base(sched->m_entries, &e)) return -1;
    return (int)XVector_size_base(sched->m_entries) - 1;
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
    int scanned;
    int count;
    if (!sched) return false;
    count = schedCount(sched);
    if (count <= 0) {
        sched->m_cursor = 0;
        return false;
    }
    if (sched->m_cursor < 0 || sched->m_cursor >= count) sched->m_cursor = 0;
    for (scanned = 0; scanned < count; ++scanned) {
        XPollEntry* e;
        XObject* owner;
        void* userData;
        sched->m_cursor = (sched->m_cursor + 1) % count;
        e = (XPollEntry*)XVector_at_base(sched->m_entries, sched->m_cursor);
        if (!e->m_due || e->m_intervalMs == 0) continue;
        if (e->m_muteUntilMs != 0 && nowMs < e->m_muteUntilMs) continue;
        if (e->m_lastSentMs != 0 && nowMs - e->m_lastSentMs < (uint64_t)e->m_intervalMs) continue;
        /* 回调可能增删条目（realloc 搬移表体）：先快照透传参数、解除静默，
           此后绝不再经由 e 访问表体。 */
        owner = e->m_owner;
        userData = e->m_userData;
        if (e->m_muteUntilMs != 0) e->m_muteUntilMs = 0; /* 静默到期首拍：探活 */
        e->m_due(owner, userData);
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
