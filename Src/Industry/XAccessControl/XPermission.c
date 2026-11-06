/**
 * @file       XPermPolicy.c
 * @brief      权限策略实现：三行位图 + O(1) 判定 + 拒绝留痕。
 * @details    判定路径上没有任何循环与比较缓冲——check 是两次移位比较
 *             （见头文件约束 4）。缺省表与覆盖规则见头文件。
 */
#include "XPermission.h"
#if !XACCOUNT_ON
#error PROBE_XACCOUNT_OFF
#endif
#if !defined(XLINK_AUTH_ON)
#error PROBE_XLINKAUTH_UNDEF
#elif !XLINK_AUTH_ON
#error PROBE_XLINKAUTH_ZERO
#endif
#if !defined(XINDUSTRY_ON)
#error PROBE_INDUSTRY_UNDEF
#endif

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

#include <string.h>

/* ==================== 内部辅助 ==================== */

/**
 * @brief 缺省策略表（None=无 / User=只读 / Admin=全权）。
 */
static const uint32_t s_permDefaults[3] = {
    (uint32_t)XAccessPerm_None,                  /* None：缺省拒绝。 */
    (uint32_t)XAccessPerm_ViewTelemetry,         /* User：只读面。 */
    (uint32_t)XAccessPerm_All                    /* Admin：全权。 */
};

/** @brief 角色收敛：未知值一律按 None（缺省拒绝，约束 1）。 */
static int permRoleIndex(XAccountRole role)
{
    if (role != XAccountRole_None && role != XAccountRole_User &&
        role != XAccountRole_Admin)
        return (int)XAccountRole_None;
    return (int)role;
}

/** @brief 未知位静默丢弃（前向兼容：老版本收到新版本位不误判）。 */
static uint32_t permMaskKnown(uint32_t mask)
{
    return mask & (uint32_t)XAccessPerm_All;
}

/* ==================== 生命周期 ==================== */

static void VXPermPolicy_deinit(XObject* obj)
{
    XPermPolicy* pm = (XPermPolicy*)obj;
    if (!pm) return;
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* XPermPolicy_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPermPolicy)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPermPolicy_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XPermPolicy);
    return XVTABLE_DEFAULT;
}

void XPermPolicy_init(XPermPolicy* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    XObject_init((XObject*)obj);
    XClassGetVtable(obj) = XPermPolicy_class_init();
    memcpy(obj->m_rolePerms, s_permDefaults, sizeof(s_permDefaults));
    obj->m_audit = true;
}

XPermPolicy* XPermPolicy_create(void)
{
    XPermPolicy* obj = (XPermPolicy*)XMemory_malloc(
        sizeof(XPermPolicy), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XPermPolicy_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 判定 ==================== */

bool XPermPolicy_check(XPermPolicy* pm, XAccountRole role, uint32_t permMask)
{
    int idx;
    uint32_t have;

    permMask = permMaskKnown(permMask);
    if (permMask == 0) return true;              /* 无要求不拒绝。 */
    if (!pm) {
        XPermPolicy_permissionDenied_signal(NULL, (int)role,
                                            (int32_t)permMask, 0);
        return false;
    }
    idx = permRoleIndex(role);
    have = pm->m_rolePerms[idx] & permMask;
    if (have == permMask) return true;
    if (pm->m_audit)
        XPermPolicy_permissionDenied_signal(pm, idx, (int32_t)permMask, 0);
    return false;
}

bool XPermPolicy_checkAny(XPermPolicy* pm, XAccountRole role, uint32_t permMask)
{
    int idx;
    uint32_t have;

    permMask = permMaskKnown(permMask);
    if (permMask == 0) return false;             /* 任一语义下空集不成立。 */
    if (!pm) {
        XPermPolicy_permissionDenied_signal(NULL, (int)role,
                                            (int32_t)permMask, 0);
        return false;
    }
    idx = permRoleIndex(role);
    have = pm->m_rolePerms[idx] & permMask;
    if (have != 0) return true;
    if (pm->m_audit)
        XPermPolicy_permissionDenied_signal(pm, idx, (int32_t)permMask, 0);
    return false;
}

uint32_t XPermPolicy_rolePerms(const XPermPolicy* pm, XAccountRole role)
{
    if (!pm) return 0;
    return pm->m_rolePerms[permRoleIndex(role)];
}

/* ==================== 策略覆盖 ==================== */

bool XPermPolicy_setRolePerms(XPermPolicy* pm, XAccountRole role, uint32_t permMask)
{
    int idx;

    if (!pm) return false;
    if (role != XAccountRole_None && role != XAccountRole_User &&
        role != XAccountRole_Admin)
        return false;
    /* Admin 恒全权：改小被拒（约束 2——杜绝自锁）。 */
    if (role == XAccountRole_Admin &&
        permMaskKnown(permMask) != (uint32_t)XAccessPerm_All)
        return false;
    idx = permRoleIndex(role);
    pm->m_rolePerms[idx] = permMaskKnown(permMask);
    XPermPolicy_policyChanged_signal(pm);
    return true;
}

bool XPermPolicy_resetPolicy(XPermPolicy* pm)
{
    if (!pm) return false;
    memcpy(pm->m_rolePerms, s_permDefaults, sizeof(s_permDefaults));
    XPermPolicy_policyChanged_signal(pm);
    return true;
}

bool XPermPolicy_setAuditEnabled(XPermPolicy* pm, bool enable)
{
    if (!pm) return false;
    pm->m_audit = enable;
    return true;
}

bool XPermPolicy_auditEnabled(const XPermPolicy* pm)
{
    return pm ? pm->m_audit : false;
}

void XPermPolicy_snapshot(const XPermPolicy* pm, XPermPolicySnapshot* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!pm) return;
    memcpy(out->m_rolePerms, pm->m_rolePerms, sizeof(out->m_rolePerms));
}

/* ==================== 信号 ==================== */

void* XPermPolicy_permissionDenied_signal(XPermPolicy* pm, XAccountRole role,
                                          int32_t permMask, int64_t atMs)
{
    /* [框架约定] self==NULL 时也必须返回自身地址（connect 侧
     * signalIdOf 以 NULL 调用解析定义模块真址）——不可提前返回 NULL，
     * 否则连接侧拿到 0、槽永远匹配不上（2026-11-05 实锅）。
     * pm 为 NULL 时 XEmitSignal 的守卫静默跳过发射。 */
    XEmitSignal(pm, XPermPolicy_permissionDenied_signal,
                XVarList_Create(XVar(int, role), XVar(int32_t, permMask),
                                XVar(int64_t, atMs)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XPermPolicy_policyChanged_signal(XPermPolicy* pm)
{
    XEmitSignal(pm, XPermPolicy_policyChanged_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */
