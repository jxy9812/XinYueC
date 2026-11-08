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
 * @brief 缺省策略表（匿名=只读 / 普通=只读+下载 / 管理=全权 / 操作者=+控制）。
 * @note  下标严格等于 XAccountRole 值（2=Admin 在 3=Operator 之前）。
 */
static const uint32_t s_permDefaults[XPERM_ROLE_SLOTS] = {
    /* None（匿名）：只能查看。 */
    (uint32_t)XAccessPerm_ViewTelemetry,
    /* User（普通用户）：查看 + 下载数据。 */
    (uint32_t)(XAccessPerm_ViewTelemetry | XAccessPerm_Download),
    /* Admin（管理员）：全权。 */
    (uint32_t)XAccessPerm_All,
    /* Operator（操作者）：查看 + 下载 + 设备控制（不含模式/自动/账户）。 */
    (uint32_t)(XAccessPerm_ViewTelemetry | XAccessPerm_Download |
               XAccessPerm_DeviceControl)
};

/**
 * @brief 各角色权限上限（四档逐级包含的产品约束，不可配置）。
 */
static const uint32_t s_permCeilings[XPERM_ROLE_SLOTS] = {
    (uint32_t)XAccessPerm_ViewTelemetry,
    (uint32_t)(XAccessPerm_ViewTelemetry | XAccessPerm_Download),
    (uint32_t)XAccessPerm_All,
    (uint32_t)(XAccessPerm_ViewTelemetry | XAccessPerm_Download |
               XAccessPerm_DeviceControl)
};

/** @brief 角色收敛：**未知值返回 -1（全拒绝）**。
 *  @details 不能把未知角色当匿名：匿名字段现在持有 ViewTelemetry，
 *           映射到匿名会让"越界/伪造角色值"白得只读权限（缺省拒绝是
 *           安全组件的第一铁律）。 */
static int permRoleIndex(XAccountRole role)
{
    switch (role) {
    case XAccountRole_None:     return 0;
    case XAccountRole_User:     return 1;
    case XAccountRole_Admin:    return 2;
    case XAccountRole_Operator: return 3;
    default:                    return -1;
    }
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
    if (idx < 0) {
        if (pm->m_audit)
            XPermPolicy_permissionDenied_signal(pm, (int)role,
                                                (int32_t)permMask, 0);
        return false;
    }
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
    if (idx < 0) {
        if (pm->m_audit)
            XPermPolicy_permissionDenied_signal(pm, (int)role,
                                                (int32_t)permMask, 0);
        return false;
    }
    have = pm->m_rolePerms[idx] & permMask;
    if (have != 0) return true;
    if (pm->m_audit)
        XPermPolicy_permissionDenied_signal(pm, idx, (int32_t)permMask, 0);
    return false;
}

uint32_t XPermPolicy_rolePerms(const XPermPolicy* pm, XAccountRole role)
{
    int idx;
    if (!pm) return 0;
    idx = permRoleIndex(role);
    return (idx < 0) ? 0 : pm->m_rolePerms[idx];
}

uint32_t XPermPolicy_roleCeiling(XAccountRole role)
{
    int idx = permRoleIndex(role);
    return (idx < 0) ? 0 : s_permCeilings[idx];
}

const char* XPermPolicy_roleName(XAccountRole role)
{
    switch (role) {
    case XAccountRole_None:     return "匿名";
    case XAccountRole_User:     return "普通用户";
    case XAccountRole_Admin:    return "管理员";
    case XAccountRole_Operator: return "操作者";
    default:                    return "未知";
    }
}

/* ==================== 策略覆盖 ==================== */

bool XPermPolicy_setRolePerms(XPermPolicy* pm, XAccountRole role, uint32_t permMask)
{
    int idx;
    uint32_t known;
    uint32_t ceiling;

    if (!pm) return false;
    if (role != XAccountRole_None && role != XAccountRole_User &&
        role != XAccountRole_Admin && role != XAccountRole_Operator)
        return false;
    idx = permRoleIndex(role);
    known = permMaskKnown(permMask);
    ceiling = s_permCeilings[idx];
    /* 四档逐级包含（产品约束）：越上限位一律拒绝。Admin 上限=All，
     * 等价于"Admin 只接受 All"（改小同样被拒——否则自锁）。 */
    if ((known & ~ceiling) != 0) return false;
    if (idx == (int)XAccountRole_Admin && known != (uint32_t)XAccessPerm_All)
        return false;
    pm->m_rolePerms[idx] = known;
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
