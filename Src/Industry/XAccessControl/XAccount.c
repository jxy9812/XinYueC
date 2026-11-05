/**
 * @file       XAccount.c
 * @brief      账户管理类实现：CRUD / 改密 / 锁定策略 / 缺省账户引导。
 * @details    全部记录操作走 XLinkUserStore（存储与认证格式层），本文件只
 *             做策略闸（口令长度）、不变式（管理员保底）、运行期表
 *             （失败计数/缺省口令标记）与事件信号。见头文件设计约束。
 */
#include "XAccount.h"

#if XINDUSTRY_ON && XACCOUNT_ON

#include <string.h>
#include <stdio.h>

/* ==================== 内部辅助 ==================== */

#define XACCOUNT_DEFAULT_MIN_PASS_LEN  6u
#define XACCOUNT_DEFAULT_LOCK_THRESHOLD 5u
#define XACCOUNT_DEFAULT_LOCK_MS       300000u   /* 5 分钟。 */

/** @brief 安全拷贝（恒 0 结尾；返回是否完整拷下）。 */
static bool accountCopyName(char* dst, size_t cap, const char* src)
{
    size_t n;
    if (!dst || cap == 0) return false;
    if (!src) { dst[0] = '\0'; return false; }
    n = strlen(src);
    if (n == 0 || n >= cap) { dst[0] = '\0'; return false; }
    memcpy(dst, src, n + 1);
    return true;
}

/** @brief 运行期失败表按下标定位（线性；容量=库容量，量小）。 */
static int accountFailIndex(const XAccountManager* mgr, const char* name)
{
    int i;
    for (i = 0; i < (int)XLINK_AUTH_USER_SLOTS; ++i) {
        if (mgr->m_failTrack[i].m_name[0] != '\0' &&
            strcmp(mgr->m_failTrack[i].m_name, name) == 0)
            return i;
    }
    return -1;
}

/** @brief 运行期失败表占用槽（无则占空槽）；满返回 -1。 */
static int accountFailAcquire(XAccountManager* mgr, const char* name)
{
    int i;
    int freeSlot = -1;
    for (i = 0; i < (int)XLINK_AUTH_USER_SLOTS; ++i) {
        if (mgr->m_failTrack[i].m_name[0] == '\0') {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (strcmp(mgr->m_failTrack[i].m_name, name) == 0) return i;
    }
    if (freeSlot >= 0)
        accountCopyName(mgr->m_failTrack[freeSlot].m_name,
                        sizeof(mgr->m_failTrack[freeSlot].m_name), name);
    return freeSlot;
}

static void accountFailRelease(XAccountManager* mgr, const char* name)
{
    int i = accountFailIndex(mgr, name);
    if (i >= 0) {
        mgr->m_failTrack[i].m_name[0] = '\0';
        mgr->m_failTrack[i].m_fails = 0;
        mgr->m_failTrack[i].m_lockUntilMs = 0;
    }
}

/** @brief 缺省口令标记下标（仅已存在账户有位）。 */
static int accountDefaultIndex(const XAccountManager* mgr, const char* name)
{
    uint32_t i;
    for (i = 0; i < mgr->m_store.m_count; ++i) {
        const char* n = XLinkUserStore_name_const(XLinkUserStore_at(&mgr->m_store, i));
        if (n && strcmp(n, name) == 0) return (int)i;
    }
    return -1;
}

/**
 * @brief 管理员不变式闸：动 targetName 之后是否仍有 ≥1 个未锁定管理员。
 * @param removing true=按"删除该账户"评估；false=按"该账户降级为 User 或
 *                 被锁定"评估。
 * @details 约束 2 的唯一判定入口，remove/setRole/lock 三处共用——漏一处
 *          系统就没人能管。
 */
static bool accountAdminInvariantHolds(XAccountManager* mgr,
                                       const char* targetName, bool removing)
{
    (void)removing;   /* 删除与降级/锁定对目标的处理已统一（见循环内说明）。 */
    uint32_t i;
    int unlockedAdmins = 0;
    for (i = 0; i < mgr->m_store.m_count; ++i) {
        const XLinkUserRecord* r = XLinkUserStore_at(&mgr->m_store, i);
        const char* n;
        if (!r) continue;
        n = XLinkUserStore_name_const(r);
        if (!n) continue;
        if (targetName && strcmp(n, targetName) == 0) {
            /* 目标账户一律排除：删除=没了，降级/锁定=不再是管理员——
             * 两种处置后目标都不再具备"未锁定管理员"资格。 */
            continue;
        }
        if (r->m_role == (uint8_t)XAccountRole_Admin &&
            !XLinkUserStore_isLocked(r))
            ++unlockedAdmins;
    }
    return unlockedAdmins >= 1;
}

/* ==================== 生命周期 ==================== */

static void VXAccountManager_deinit(XObject* obj)
{
    XAccountManager* mgr = (XAccountManager*)obj;
    if (!mgr) return;
    XLinkUserStore_clear(&mgr->m_store);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* XAccountManager_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAccountManager)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAccountManager_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XAccountManager);
    return XVTABLE_DEFAULT;
}

void XAccountManager_init(XAccountManager* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    XObject_init((XObject*)obj);
    XClassGetVtable(obj) = XAccountManager_class_init();
    XLinkUserStore_init(&obj->m_store);
    obj->m_minPassLen = XACCOUNT_DEFAULT_MIN_PASS_LEN;
    obj->m_lockThreshold = XACCOUNT_DEFAULT_LOCK_THRESHOLD;
    obj->m_lockMs = XACCOUNT_DEFAULT_LOCK_MS;
}

XAccountManager* XAccountManager_create(void)
{
    XAccountManager* obj = (XAccountManager*)XMemory_malloc(
        sizeof(XAccountManager), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XAccountManager_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 持久化 ==================== */

bool XAccountManager_setPath(XAccountManager* mgr, const char* path)
{
    if (!mgr) return false;
    if (!path || !path[0]) {
        mgr->m_path[0] = '\0';
        return true;
    }
    return accountCopyName(mgr->m_path, sizeof(mgr->m_path), path);
}

bool XAccountManager_load(XAccountManager* mgr)
{
    bool exists = false;
    if (!mgr || !mgr->m_path[0]) return false;
    if (!XLinkUserStore_load(&mgr->m_store, mgr->m_path, &exists))
        return false;
    /* 运行期表复位（见头文件 load 说明）。 */
    memset(mgr->m_failTrack, 0, sizeof(mgr->m_failTrack));
    memset(mgr->m_defaultPass, 0, sizeof(mgr->m_defaultPass));
    return true;
}

bool XAccountManager_save(XAccountManager* mgr)
{
    bool ok;
    if (!mgr || !mgr->m_path[0]) return false;
    ok = XLinkUserStore_save(&mgr->m_store, mgr->m_path);
    if (ok) XAccountManager_storeSaved_signal(mgr, mgr->m_path);
    return ok;
}

const char* XAccountManager_path(const XAccountManager* mgr)
{
    return mgr ? mgr->m_path : "";
}

/* ==================== 策略 ==================== */

bool XAccountManager_setMinPasswordLen(XAccountManager* mgr, uint32_t len)
{
    if (!mgr) return false;
    mgr->m_minPassLen = len;
    return true;
}

uint32_t XAccountManager_minPasswordLen(const XAccountManager* mgr)
{
    return mgr ? mgr->m_minPassLen : 0u;
}

bool XAccountManager_setLockThreshold(XAccountManager* mgr, uint32_t count)
{
    if (!mgr) return false;
    mgr->m_lockThreshold = count;
    return true;
}

uint32_t XAccountManager_lockThreshold(const XAccountManager* mgr)
{
    return mgr ? mgr->m_lockThreshold : 0u;
}

bool XAccountManager_setLockDurationMs(XAccountManager* mgr, uint64_t ms)
{
    if (!mgr) return false;
    mgr->m_lockMs = ms;
    return true;
}

uint64_t XAccountManager_lockDurationMs(const XAccountManager* mgr)
{
    return mgr ? mgr->m_lockMs : 0u;
}

/* ==================== 缺省账户引导 ==================== */

bool XAccountManager_ensureDefaults(XAccountManager* mgr)
{
    bool ok;
    int di;
    if (!mgr) return false;
    if (XLinkUserStore_count(&mgr->m_store) != 0) return false;
    /* admin=只读（User），root=管理（Admin）——与 HSF 既有缺省口径一致。 */
    ok = XLinkUserStore_upsert(&mgr->m_store, "admin", "admin",
                               (uint8_t)XAccountRole_User) &&
         XLinkUserStore_upsert(&mgr->m_store, "root", "root",
                               (uint8_t)XAccountRole_Admin);
    if (!ok) return false;
    di = accountDefaultIndex(mgr, "admin");
    if (di >= 0) mgr->m_defaultPass[di] = true;
    di = accountDefaultIndex(mgr, "root");
    if (di >= 0) mgr->m_defaultPass[di] = true;
    XAccountManager_accountAdded_signal(mgr, "admin");
    XAccountManager_accountAdded_signal(mgr, "root");
    return true;
}

/* ==================== 账户 CRUD ==================== */

bool XAccountManager_add(XAccountManager* mgr, const char* name,
                         const char* password, XAccountRole role)
{
    if (!mgr || !name || !name[0]) return false;
    if (role != XAccountRole_User && role != XAccountRole_Admin) return false;
    if (!password || !password[0]) return false;
    if (mgr->m_minPassLen && strlen(password) < mgr->m_minPassLen) return false;
    if (XLinkUserStore_find(&mgr->m_store, name)) return false;   /* 重名拒绝。 */
    if (XLinkUserStore_count(&mgr->m_store) >= XLINK_AUTH_USER_SLOTS) return false;
    if (!XLinkUserStore_upsert(&mgr->m_store, name, password, (uint8_t)role))
        return false;
    XAccountManager_accountAdded_signal(mgr, name);
    return true;
}

bool XAccountManager_remove(XAccountManager* mgr, const char* name)
{
    if (!mgr || !name || !XLinkUserStore_find(&mgr->m_store, name)) return false;
    if (!accountAdminInvariantHolds(mgr, name, true)) return false;
    if (!XLinkUserStore_remove(&mgr->m_store, name)) return false;
    accountFailRelease(mgr, name);
    XAccountManager_accountRemoved_signal(mgr, name);
    return true;
}

bool XAccountManager_setRole(XAccountManager* mgr, const char* name,
                             XAccountRole role)
{
    const XLinkUserRecord* r;
    if (!mgr || !name) return false;
    if (role != XAccountRole_User && role != XAccountRole_Admin) return false;
    r = XLinkUserStore_find(&mgr->m_store, name);
    if (!r) return false;
    /* 降级闸：目标当前是未锁定管理员且目标角色非 Admin → 先评不变式。 */
    if (role != XAccountRole_Admin &&
        r->m_role == (uint8_t)XAccountRole_Admin &&
        !XLinkUserStore_isLocked(r) &&
        !accountAdminInvariantHolds(mgr, name, false))
        return false;
    /* upsert 不带口令 = 仅更新角色（存储层契约）。 */
    if (!XLinkUserStore_upsert(&mgr->m_store, name, NULL, (uint8_t)role))
        return false;
    XAccountManager_accountChanged_signal(mgr, name);
    return true;
}

bool XAccountManager_setPassword(XAccountManager* mgr, const char* name,
                                 const char* oldPassword, const char* newPassword)
{
    if (!mgr || !name || !newPassword || !newPassword[0]) return false;
    if (mgr->m_minPassLen && strlen(newPassword) < mgr->m_minPassLen) return false;
    if (!XLinkUserStore_find(&mgr->m_store, name)) return false;
    /* 旧口令必须对（常量时间比较在存储层；锁定=拒绝改密，约束 4）。 */
    if (XLinkUserStore_verify(&mgr->m_store, name, oldPassword, NULL) != 0)
        return false;
    /* upsert 无"忽略角色"哨兵：读旧角色原样回写，只换口令摘要。 */
    {
        const XLinkUserRecord* r = XLinkUserStore_find(&mgr->m_store, name);
        if (!r) return false;
        if (!XLinkUserStore_upsert(&mgr->m_store, name, newPassword, r->m_role))
            return false;
    }
    {
        int di = accountDefaultIndex(mgr, name);
        if (di >= 0) mgr->m_defaultPass[di] = false;
    }
    XAccountManager_accountChanged_signal(mgr, name);
    return true;
}

bool XAccountManager_resetPassword(XAccountManager* mgr, const char* name,
                                   const char* newPassword)
{
    if (!mgr || !name || !newPassword || !newPassword[0]) return false;
    if (mgr->m_minPassLen && strlen(newPassword) < mgr->m_minPassLen) return false;
    if (!XLinkUserStore_find(&mgr->m_store, name)) return false;
    /* 读旧角色原样回写：只换口令摘要，角色/锁定态不动（锁定账户允许重置
     * ——解锁流程的一部分，约束 4）。 */
    {
        const XLinkUserRecord* r = XLinkUserStore_find(&mgr->m_store, name);
        if (!r) return false;
        if (!XLinkUserStore_upsert(&mgr->m_store, name, newPassword, r->m_role))
            return false;
    }
    {
        int di = accountDefaultIndex(mgr, name);
        if (di >= 0) mgr->m_defaultPass[di] = false;
    }
    XAccountManager_accountChanged_signal(mgr, name);
    return true;
}

/* ==================== 锁定 ==================== */

bool XAccountManager_lock(XAccountManager* mgr, const char* name)
{
    const XLinkUserRecord* r;
    if (!mgr || !name) return false;
    r = XLinkUserStore_find(&mgr->m_store, name);
    if (!r) return false;
    if (XLinkUserStore_isLocked(r)) return false;
    if (r->m_role == (uint8_t)XAccountRole_Admin &&
        !accountAdminInvariantHolds(mgr, name, false))
        return false;
    if (!XLinkUserStore_setLocked(&mgr->m_store, name, true)) return false;
    XAccountManager_accountLocked_signal(mgr, name,
                                         (int)XAccountLockCause_Manual);
    return true;
}

bool XAccountManager_unlock(XAccountManager* mgr, const char* name)
{
    int fi;
    if (!mgr || !name) return false;
    if (!XLinkUserStore_find(&mgr->m_store, name)) return false;
    if (!XLinkUserStore_isLocked(XLinkUserStore_find(&mgr->m_store, name)))
        return false;
    if (!XLinkUserStore_setLocked(&mgr->m_store, name, false)) return false;
    fi = accountFailIndex(mgr, name);
    if (fi >= 0) {
        mgr->m_failTrack[fi].m_fails = 0;
        mgr->m_failTrack[fi].m_lockUntilMs = 0;
    }
    XAccountManager_accountLocked_signal(mgr, name, (int)XAccountLockCause_None);
    return true;
}

void XAccountManager_noteFailure(XAccountManager* mgr, const char* name,
                                 uint64_t nowMs)
{
    int fi;
    const XLinkUserRecord* r;
    if (!mgr || !name) return;
    r = XLinkUserStore_find(&mgr->m_store, name);
    if (!r) return;
    fi = accountFailAcquire(mgr, name);
    if (fi < 0) return;
    ++mgr->m_failTrack[fi].m_fails;
    if (mgr->m_lockThreshold && mgr->m_failTrack[fi].m_fails >= mgr->m_lockThreshold &&
        !XLinkUserStore_isLocked(r)) {
        /* 达阈值：自动锁定（管理员受不变式保护——锁不住，计数保留）。 */
        if (r->m_role == (uint8_t)XAccountRole_Admin &&
            !accountAdminInvariantHolds(mgr, name, false))
            return;
        if (XLinkUserStore_setLocked(&mgr->m_store, name, true)) {
            mgr->m_failTrack[fi].m_lockUntilMs =
                mgr->m_lockMs ? (nowMs + mgr->m_lockMs) : 0;
            XAccountManager_accountLocked_signal(
                mgr, name, (int)XAccountLockCause_Failures);
        }
    }
}

void XAccountManager_noteSuccess(XAccountManager* mgr, const char* name)
{
    int fi;
    if (!mgr || !name) return;
    fi = accountFailIndex(mgr, name);
    if (fi >= 0) mgr->m_failTrack[fi].m_fails = 0;
}

bool XAccountManager_isLockedNow(XAccountManager* mgr, const char* name,
                                 uint64_t nowMs)
{
    int fi;
    const XLinkUserRecord* r;
    if (!mgr || !name) return false;
    r = XLinkUserStore_find(&mgr->m_store, name);
    if (!r) return false;
    fi = accountFailIndex(mgr, name);
    if (XLinkUserStore_isLocked(r)) {
        /* 自动锁定且到期：就地解锁（惰性；人工锁 lockUntilMs=0 永不到期）。 */
        if (fi >= 0 && mgr->m_failTrack[fi].m_lockUntilMs != 0 &&
            nowMs >= mgr->m_failTrack[fi].m_lockUntilMs) {
            XLinkUserStore_setLocked(&mgr->m_store, name, false);
            mgr->m_failTrack[fi].m_fails = 0;
            mgr->m_failTrack[fi].m_lockUntilMs = 0;
            XAccountManager_accountLocked_signal(mgr, name,
                                                 (int)XAccountLockCause_None);
            return false;
        }
        return true;
    }
    return false;
}

/* ==================== 查询 ==================== */

uint32_t XAccountManager_count(const XAccountManager* mgr)
{
    return mgr ? XLinkUserStore_count(&mgr->m_store) : 0;
}

const XLinkUserRecord* XAccountManager_find(const XAccountManager* mgr,
                                            const char* name)
{
    return mgr ? XLinkUserStore_find(&mgr->m_store, name) : NULL;
}

bool XAccountManager_exists(const XAccountManager* mgr, const char* name)
{
    return mgr && name && XLinkUserStore_find(&mgr->m_store, name) != NULL;
}

XAccountRole XAccountManager_role(const XAccountManager* mgr, const char* name)
{
    const XLinkUserRecord* r = XAccountManager_find(mgr, name);
    return r ? (XAccountRole)r->m_role : XAccountRole_None;
}

bool XAccountManager_isDefaultPassword(const XAccountManager* mgr, const char* name)
{
    int di;
    if (!mgr || !name) return false;
    di = accountDefaultIndex(mgr, name);
    return di >= 0 && mgr->m_defaultPass[di];
}

int XAccountManager_verify(XAccountManager* mgr, const char* name,
                           const char* password, uint8_t* outRole)
{
    if (!mgr) return 1;
    return XLinkUserStore_verify(&mgr->m_store, name, password, outRole);
}

void XAccountManager_snapshot(const XAccountManager* mgr, const char* name,
                              XAccountSnapshot* out)
{
    const XLinkUserRecord* r;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!mgr || !name) return;
    r = XLinkUserStore_find(&mgr->m_store, name);
    if (!r) return;
    snprintf(out->m_name, sizeof(out->m_name), "%s",
             XLinkUserStore_name_const(r) ? XLinkUserStore_name_const(r) : "");
    out->m_role = (int)r->m_role;
    out->m_locked = XLinkUserStore_isLocked(r);
    out->m_defaultPass = XAccountManager_isDefaultPassword(mgr, name);
}

/* ==================== 信号 ==================== */

void* XAccountManager_accountAdded_signal(XAccountManager* mgr, const char* name)
{
    XEmitSignal(mgr, XAccountManager_accountAdded_signal,
                XVarList_Create(XVar(const char*, name)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAccountManager_accountRemoved_signal(XAccountManager* mgr, const char* name)
{
    XEmitSignal(mgr, XAccountManager_accountRemoved_signal,
                XVarList_Create(XVar(const char*, name)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAccountManager_accountChanged_signal(XAccountManager* mgr, const char* name)
{
    XEmitSignal(mgr, XAccountManager_accountChanged_signal,
                XVarList_Create(XVar(const char*, name)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAccountManager_accountLocked_signal(XAccountManager* mgr, const char* name,
                                           int cause)
{
    XEmitSignal(mgr, XAccountManager_accountLocked_signal,
                XVarList_Create(XVar(const char*, name), XVar(int, cause)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAccountManager_storeSaved_signal(XAccountManager* mgr, const char* path)
{
    XEmitSignal(mgr, XAccountManager_storeSaved_signal,
                XVarList_Create(XVar(const char*, path)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

#endif /* XINDUSTRY_ON && XACCOUNT_ON */
