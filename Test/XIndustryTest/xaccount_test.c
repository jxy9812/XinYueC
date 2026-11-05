/**
 * @file       xaccount_test.c
 * @brief      Industry 访问控制测试（账户管理 / 权限策略 / 登录策略 + 编解码）。
 * @details    覆盖（设计对齐 xautocontrol_test 收编形态）：
 *               - 账户：缺省账户引导（只建一次/缺省口令标记）、CRUD、
 *                 改密（验旧/管理员重置）、角色调整、verify 返回码；
 *               - 管理员不变式：删/降/锁最后一个未锁定管理员一律拒绝；
 *               - 锁定策略：人工锁定/解锁、失败计数达阈值自动锁定、
 *                 到期惰性解锁（isLockedNow）、成功清计数；
 *               - 权限策略：缺省表、check/checkAny、未知角色缺省拒绝、
 *                 Admin 恒全权（改小被拒）、resetPolicy、拒绝留痕信号；
 *               - 登录策略：缺省全关、总数上限、匿名双闸、同账并发
 *                 （DenyNew/KickOld）、拒绝原因可读、持久化往返；
 *               - 编解码：账户帧/权限策略帧/登录策略帧 BSON↔JSON 往返；
 *               - NULL 安全与泄漏压测（200 轮）。
 *             账户库/策略文件落在临时路径（跑前删、跑后删），不碰真实配置。
 */
#include "XAccount.h"
#include "XPermission.h"
#include "XLoginPolicy.h"
#include "XAccessControlCodec.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XDeviceFile.h"
#include "XString.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#if XINDUSTRY_ON && XACCOUNT_ON && XACCESS_CONTROL_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* ---- 信号记录 ---- */
static int g_permDenied = 0;
static int g_lastDeniedRole = -1;
static int g_permChanged = 0;
static int g_loginRejected = 0;
static int g_lockedEvents = 0;
static int g_lastLockCause = -1;

static void resetFlags(void)
{
    g_permDenied = 0;
    g_lastDeniedRole = -1;
    g_permChanged = 0;
    g_loginRejected = 0;
    g_lockedEvents = 0;
    g_lastLockCause = -1;
}

static void onPermDenied2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lastDeniedRole = XVarList_arg(args, int);
    (void)XVarList_arg(args, int32_t);
    (void)XVarList_arg(args, int64_t);
    ++g_permDenied;
}

static void onPermChanged2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_permChanged;
}

static void onLoginRejected2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    (void)XVarList_arg(args, const char*);
    (void)XVarList_arg(args, const char*);
    (void)XVarList_arg(args, int64_t);
    ++g_loginRejected;
}

static void onLocked2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    (void)XVarList_arg(args, const char*);
    g_lastLockCause = XVarList_arg(args, int);
    ++g_lockedEvents;
}

/** @brief 事件泵小睡（信号派发驱动）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/** @brief 临时文件路径（每个用例独立名，跑前删）。 */
static void tempPath(char* out, int cap, const char* tag)
{
    snprintf(out, (size_t)cap, "./xacct_test_%s.tmp", tag);
    {
        XString* sp = XString_create_utf8(out);
        if (sp) {
            XDeviceFile_removePermanent(sp);
            XClassDelete((XClass*)sp);
        }
    }
}

/* ==================== 用例：账户管理 ==================== */

static void test_accounts(void)
{
    XAccountManager* mgr;
    char path[128];
    uint8_t role = 0;
    XAccountSnapshot snap;

    resetFlags();
    tempPath(path, (int)sizeof(path), "users");
    mgr = XAccountManager_create();
    XObject_connect_2((XObject*)mgr,
                      XSignal(XAccountManager_accountLocked_signal), onLocked2);
    XAccountManager_setPath(mgr, path);
    CHECK("空库加载成功", XAccountManager_load(mgr));
    CHECK("空库 0 账户", XAccountManager_count(mgr) == 0);

    /* 缺省账户引导：只建一次。 */
    CHECK("首次引导创建缺省账户", XAccountManager_ensureDefaults(mgr));
    CHECK("缺省 2 账户", XAccountManager_count(mgr) == 2);
    CHECK("admin 为只读角色", XAccountManager_role(mgr, "admin") == XAccountRole_User);
    CHECK("root 为管理角色", XAccountManager_role(mgr, "root") == XAccountRole_Admin);
    CHECK("缺省口令标记（admin）", XAccountManager_isDefaultPassword(mgr, "admin"));
    CHECK("缺省口令标记（root）", XAccountManager_isDefaultPassword(mgr, "root"));
    CHECK("再次引导幂等", !XAccountManager_ensureDefaults(mgr) &&
          XAccountManager_count(mgr) == 2);

    /* verify 返回码（与存储层一致：0 过/1 无用户/2 锁定/3 口令错）。 */
    CHECK("verify 口令对", XAccountManager_verify(mgr, "root", "root", &role) == 0 &&
          role == (uint8_t)XAccountRole_Admin);
    CHECK("verify 口令错", XAccountManager_verify(mgr, "root", "bad", NULL) == 3);
    CHECK("verify 无此用户", XAccountManager_verify(mgr, "ghost", "x", NULL) == 1);

    /* CRUD。 */
    CHECK("加操作员", XAccountManager_add(mgr, "op1", "op123456",
                                          XAccountRole_User));
    CHECK("重名拒绝", !XAccountManager_add(mgr, "op1", "x2345678",
                                           XAccountRole_User));
    CHECK("口令过短拒绝", !XAccountManager_add(mgr, "op2", "123",
                                               XAccountRole_User));
    CHECK("非法角色拒绝", !XAccountManager_add(mgr, "op3", "x2345678",
                                               XAccountRole_None));
    CHECK("新增不是缺省口令", !XAccountManager_isDefaultPassword(mgr, "op1"));

    /* 改密：本人验旧；管理员强制重置不验旧；改密清缺省口令标记。 */
    CHECK("旧口令错拒绝", !XAccountManager_setPassword(mgr, "root", "wrong",
                                                       "newroot1"));
    CHECK("root 改密成功", XAccountManager_setPassword(mgr, "root", "root",
                                                       "newroot1"));
    CHECK("改密清缺省口令标记", !XAccountManager_isDefaultPassword(mgr, "root"));
    CHECK("新口令生效", XAccountManager_verify(mgr, "root", "newroot1", NULL) == 0);
    CHECK("管理员重置 op1", XAccountManager_resetPassword(mgr, "op1", "reset99"));
    CHECK("重置后新口令生效",
          XAccountManager_verify(mgr, "op1", "reset99", NULL) == 0);

    /* 管理员不变式：删/降/锁最后一个未锁定管理员一律拒绝。 */
    CHECK("删管理员拒绝", !XAccountManager_remove(mgr, "root"));
    CHECK("降管理员拒绝", !XAccountManager_setRole(mgr, "root",
                                                   XAccountRole_User));
    CHECK("锁管理员拒绝", !XAccountManager_lock(mgr, "root"));
    /* 第二个管理员存在时，可删其一。 */
    CHECK("加第二管理员", XAccountManager_add(mgr, "root2", "root222",
                                              XAccountRole_Admin));
    CHECK("删其一管理员放行", XAccountManager_remove(mgr, "root2"));
    CHECK("再删最后一管理员拒绝", !XAccountManager_remove(mgr, "root"));
    CHECK("删普通账户放行", XAccountManager_remove(mgr, "op1"));

    /* 角色调整（非降级场景）。 */
    CHECK("升 admin 为管理", XAccountManager_setRole(mgr, "admin",
                                                     XAccountRole_Admin));
    CHECK("角色回读", XAccountManager_role(mgr, "admin") == XAccountRole_Admin);
    CHECK("降回只读放行（root 仍在）", XAccountManager_setRole(mgr, "admin",
                                                     XAccountRole_User));

    /* 落盘 + 重载（缺省口令标记运行期不持久化）。 */
    CHECK("落盘成功", XAccountManager_save(mgr));
    CHECK("重载成功", XAccountManager_load(mgr));
    CHECK("重载后账户保持", XAccountManager_count(mgr) == 2 &&
          XAccountManager_exists(mgr, "root") &&
          XAccountManager_exists(mgr, "admin"));
    CHECK("重载后改密保持",
          XAccountManager_verify(mgr, "root", "newroot1", NULL) == 0);

    /* 快照投影（无凭据材料）。 */
    XAccountManager_snapshot(mgr, "root", &snap);
    CHECK("快照字段", strcmp(snap.m_name, "root") == 0 &&
          snap.m_role == (int)XAccountRole_Admin && !snap.m_locked);
    XAccountManager_snapshot(mgr, "ghost", &snap);
    CHECK("快照不存在全零", snap.m_name[0] == '\0');

    /* NULL 安全。 */
    CHECK("NULL 管理器安全", XAccountManager_count(NULL) == 0 &&
          !XAccountManager_exists(NULL, "root") &&
          XAccountManager_role(NULL, "root") == XAccountRole_None &&
          !XAccountManager_isDefaultPassword(NULL, "root"));

    {
        XString* sp = XString_create_utf8(path);
        if (sp) { XDeviceFile_removePermanent(sp); XClassDelete((XClass*)sp); }
    }
    XClassDelete((XClass*)mgr);
}

/* ==================== 用例：锁定策略 ==================== */

static void test_lockout(void)
{
    XAccountManager* mgr;
    char path[128];
    int i;
    uint64_t now = 10000;

    resetFlags();
    tempPath(path, (int)sizeof(path), "lock");
    mgr = XAccountManager_create();
    XObject_connect_2((XObject*)mgr,
                      XSignal(XAccountManager_accountLocked_signal), onLocked2);
    XAccountManager_setPath(mgr, path);
    XAccountManager_load(mgr);
    XAccountManager_ensureDefaults(mgr);
    CHECK("加操作员 lockme", XAccountManager_add(mgr, "lockme", "pass123456",
                                                 XAccountRole_User));
    CHECK("设阈值 3", XAccountManager_setLockThreshold(mgr, 3));
    CHECK("设锁定时长 5000ms", XAccountManager_setLockDurationMs(mgr, 5000));

    /* 失败 2 次：未锁。 */
    XAccountManager_noteFailure(mgr, "lockme", now);
    XAccountManager_noteFailure(mgr, "lockme", now);
    CHECK("未达阈值不锁", !XAccountManager_isLockedNow(mgr, "lockme", now));
    /* 第 3 次：自动锁定 + 事件。 */
    XAccountManager_noteFailure(mgr, "lockme", now);
    spinMs(50);
    CHECK("达阈值自动锁定", XAccountManager_isLockedNow(mgr, "lockme", now));
    CHECK("锁定事件已发（Failures）", g_lockedEvents == 1 &&
          g_lastLockCause == (int)XAccountLockCause_Failures);
    CHECK("verify 锁定返回 2",
          XAccountManager_verify(mgr, "lockme", "pass123456", NULL) == 2);

    /* 到期惰性解锁。 */
    CHECK("期内仍锁", XAccountManager_isLockedNow(mgr, "lockme", now + 4999));
    CHECK("到期自动解锁", !XAccountManager_isLockedNow(mgr, "lockme", now + 5001));
    spinMs(50);
    CHECK("解锁事件已发（None）", g_lockedEvents == 2 &&
          g_lastLockCause == (int)XAccountLockCause_None);
    CHECK("解锁后口令恢复", XAccountManager_verify(mgr, "lockme", "pass123456",
                                                   NULL) == 0);

    /* 成功清计数：再来 2 次失败不锁（此前成功已清零）。 */
    XAccountManager_noteFailure(mgr, "lockme", now + 6000);
    XAccountManager_noteFailure(mgr, "lockme", now + 6000);
    CHECK("成功后计数已清（未锁）",
          !XAccountManager_isLockedNow(mgr, "lockme", now + 6000));

    /* 人工锁定/解锁。 */
    CHECK("人工锁定", XAccountManager_lock(mgr, "lockme"));
    CHECK("人工锁幂等", !XAccountManager_lock(mgr, "lockme"));
    CHECK("人工锁不到期（lockUntil=0）",
          XAccountManager_isLockedNow(mgr, "lockme", now + 999999));
    CHECK("人工解锁", XAccountManager_unlock(mgr, "lockme"));
    CHECK("解锁后未锁", !XAccountManager_isLockedNow(mgr, "lockme", now));

    /* 不存在账户回灌安全。 */
    XAccountManager_noteFailure(mgr, "ghost", now);
    XAccountManager_noteSuccess(mgr, "ghost");
    CHECK("不存在账户回灌安全", true);

    {
        XString* sp = XString_create_utf8(path);
        if (sp) { XDeviceFile_removePermanent(sp); XClassDelete((XClass*)sp); }
    }
    XClassDelete((XClass*)mgr);
}

/* ==================== 用例：权限策略 ==================== */

static void test_permissions(void)
{
    XPermPolicy* pm;

    resetFlags();
    pm = XPermPolicy_create();
    XObject_connect_2((XObject*)pm,
                      XSignal(XPermPolicy_permissionDenied_signal), onPermDenied2);
    XObject_connect_2((XObject*)pm,
                      XSignal(XPermPolicy_policyChanged_signal), onPermChanged2);

    /* 缺省表。 */
    CHECK("匿名无权限", XPermPolicy_rolePerms(pm, XAccountRole_None) == 0);
    CHECK("操作员只读", XPermPolicy_rolePerms(pm, XAccountRole_User) ==
          (uint32_t)XAccessPerm_ViewTelemetry);
    CHECK("管理员全权", XPermPolicy_rolePerms(pm, XAccountRole_Admin) ==
          (uint32_t)XAccessPerm_All);

    /* check 全含语义。 */
    CHECK("匿名拉快照被拒", !XPermPolicy_check(pm, XAccountRole_None,
                                               (uint32_t)XAccessPerm_ViewTelemetry));
    CHECK("操作员拉快照放行", XPermPolicy_check(pm, XAccountRole_User,
                                                (uint32_t)XAccessPerm_ViewTelemetry));
    CHECK("操作员控制被拒", !XPermPolicy_check(pm, XAccountRole_User,
                                               (uint32_t)XAccessPerm_DeviceControl));
    CHECK("管理员控制放行", XPermPolicy_check(pm, XAccountRole_Admin,
                                              (uint32_t)(XAccessPerm_DeviceControl |
                                                         XAccessPerm_AutoControl)));
    CHECK("空要求恒真", XPermPolicy_check(pm, XAccountRole_None, 0));
    CHECK("未知角色缺省拒绝", !XPermPolicy_check(pm, (XAccountRole)99,
                                                 (uint32_t)XAccessPerm_ViewTelemetry));
    spinMs(50);
    CHECK("拒绝留痕已发", g_permDenied >= 3);

    /* checkAny 任一语义。 */
    CHECK("任一命中", XPermPolicy_checkAny(pm, XAccountRole_User,
                                           (uint32_t)(XAccessPerm_ViewTelemetry |
                                                      XAccessPerm_DeviceControl)));
    CHECK("任一未命中", !XPermPolicy_checkAny(pm, XAccountRole_User,
                                              (uint32_t)XAccessPerm_AccountAdmin));
    CHECK("任一空集恒假", !XPermPolicy_checkAny(pm, XAccountRole_Admin, 0));

    /* Admin 恒全权：改小被拒；None/User 可覆盖。 */
    CHECK("Admin 改小被拒", !XPermPolicy_setRolePerms(pm, XAccountRole_Admin,
                                                      (uint32_t)XAccessPerm_ViewTelemetry));
    CHECK("Admin 恒 All", XPermPolicy_rolePerms(pm, XAccountRole_Admin) ==
          (uint32_t)XAccessPerm_All);
    CHECK("User 加控制权放行", XPermPolicy_setRolePerms(
              pm, XAccountRole_User,
              (uint32_t)(XAccessPerm_ViewTelemetry | XAccessPerm_DeviceControl)));
    CHECK("覆盖后判定即时生效", XPermPolicy_check(pm, XAccountRole_User,
                                                  (uint32_t)XAccessPerm_DeviceControl));
    CHECK("未知位静默丢弃", XPermPolicy_setRolePerms(pm, XAccountRole_User, 0xFFFFFFu));
    CHECK("丢弃后仅合法位", XPermPolicy_rolePerms(pm, XAccountRole_User) ==
          (0xFFFFFFu & (uint32_t)XAccessPerm_All));
    CHECK("策略变更信号已发", g_permChanged >= 2);

    /* 回缺省表。 */
    CHECK("回缺省表", XPermPolicy_resetPolicy(pm));
    CHECK("回缺省后 User 只读", XPermPolicy_rolePerms(pm, XAccountRole_User) ==
          (uint32_t)XAccessPerm_ViewTelemetry);
    CHECK("未知角色覆盖拒绝", !XPermPolicy_setRolePerms(pm, (XAccountRole)9, 1));

    /* NULL 安全（策略未装配=全拒绝，且不留痕——装配错误由装配修复）。 */
    CHECK("NULL 策略全拒绝", !XPermPolicy_check(NULL, XAccountRole_Admin,
                                                (uint32_t)XAccessPerm_ViewTelemetry));
    CHECK("NULL rolePerms 为 0", XPermPolicy_rolePerms(NULL, XAccountRole_Admin) == 0);
    XClassDelete((XClass*)pm);
}

/* ==================== 用例：登录策略 ==================== */

static void test_loginPolicy(void)
{
    XLoginPolicy* lp;
    char path[128];
    char reason[XLOGIN_POLICY_REASON_MAX];
    bool kick = false;

    resetFlags();
    tempPath(path, (int)sizeof(path), "login");
    lp = XLoginPolicy_create();
    XObject_connect_2((XObject*)lp,
                      XSignal(XLoginPolicy_loginRejected_signal), onLoginRejected2);
    XLoginPolicy_setPath(lp, path);

    /* 缺省：匿名关/并发关/上限 8/DenyNew。 */
    CHECK("缺省匿名关", !XLoginPolicy_anonymousAllowed(lp));
    CHECK("缺省并发关", !XLoginPolicy_concurrentAllowed(lp));
    CHECK("缺省上限 8", XLoginPolicy_maxSessions(lp) == 8);
    CHECK("缺省 DenyNew", XLoginPolicy_concurrencyMode(lp) ==
          XLoginConcurrency_DenyNew);

    /* 闸 1：总数上限。 */
    CHECK("未达上限放行", XLoginPolicy_admitSession(lp, false, 7, 0,
                                                    reason, (int)sizeof(reason), &kick));
    CHECK("达上限拒绝", !XLoginPolicy_admitSession(lp, false, 8, 0,
                                                   reason, (int)sizeof(reason), &kick));
    CHECK("拒绝原因可读", strcmp(reason, "会话数已达上限") == 0);
    spinMs(50);
    CHECK("登录拒绝留痕已发", g_loginRejected >= 1);

    /* 闸 2：匿名（双闸第一道）。 */
    CHECK("匿名默认拒绝", !XLoginPolicy_admitSession(lp, true, 1, 0,
                                                     reason, (int)sizeof(reason), &kick));
    CHECK("匿名拒绝原因", strcmp(reason, "匿名登录未开放") == 0);
    CHECK("开匿名受理", XLoginPolicy_setAllowAnonymous(lp, true));
    CHECK("开匿名后放行", XLoginPolicy_admitSession(lp, true, 1, 0,
                                                    reason, (int)sizeof(reason), &kick));
    CHECK("关回匿名", XLoginPolicy_setAllowAnonymous(lp, false));

    /* 闸 3：同账并发 DenyNew。 */
    CHECK("同账并发默认拒绝", !XLoginPolicy_admitSession(lp, false, 2, 1,
                                                         reason, (int)sizeof(reason), &kick));
    CHECK("并发拒绝原因", strcmp(reason, "该账户已登录，不允许同时在线") == 0);
    CHECK("无同账会话放行", XLoginPolicy_admitSession(lp, false, 2, 0,
                                                      reason, (int)sizeof(reason), &kick));
    /* KickOld：准入 + 置踢同账标记（本批链路层尚不支持，调用方降级）。 */
    CHECK("切 KickOld", XLoginPolicy_setConcurrencyMode(lp, XLoginConcurrency_KickOld));
    kick = false;
    CHECK("KickOld 准入", XLoginPolicy_admitSession(lp, false, 2, 1,
                                                    reason, (int)sizeof(reason), &kick));
    CHECK("KickOld 置踢标记", kick);
    CHECK("KickOld 无同账不置标记", XLoginPolicy_admitSession(lp, false, 2, 0,
                                                              reason, (int)sizeof(reason), &kick) && !kick);
    CHECK("回 DenyNew", XLoginPolicy_setConcurrencyMode(lp, XLoginConcurrency_DenyNew));

    /* 持久化往返。 */
    CHECK("开并发", XLoginPolicy_setAllowConcurrent(lp, true));
    CHECK("设上限 4", XLoginPolicy_setMaxSessions(lp, 4));
    CHECK("落盘", XLoginPolicy_save(lp));
    {
        XLoginPolicy* lp2 = XLoginPolicy_create();
        XLoginPolicy_setPath(lp2, path);
        CHECK("重载成功", XLoginPolicy_load(lp2));
        CHECK("重载策略一致", XLoginPolicy_anonymousAllowed(lp2) == false &&
              XLoginPolicy_concurrentAllowed(lp2) == true &&
              XLoginPolicy_maxSessions(lp2) == 4 &&
              XLoginPolicy_concurrencyMode(lp2) == XLoginConcurrency_DenyNew);
        XClassDelete((XClass*)lp2);
    }

    /* NULL 安全。 */
    CHECK("NULL 策略全拒绝", !XLoginPolicy_admitSession(NULL, false, 0, 0,
                                                        reason, (int)sizeof(reason), &kick));
    CHECK("非法并发模式拒绝", !XLoginPolicy_setConcurrencyMode(lp, (XLoginConcurrencyMode)7));
    {
        XString* sp = XString_create_utf8(path);
        if (sp) { XDeviceFile_removePermanent(sp); XClassDelete((XClass*)sp); }
    }
    XClassDelete((XClass*)lp);
}

/* ==================== 用例：编解码往返 ==================== */

static void test_codec(void)
{
    XAccountManager* mgr;
    XAccountSnapshot snap;
    XAccountSnapshot snapBack;
    XPermPolicySnapshot perm;
    XPermPolicySnapshot permBack;
    XLoginPolicySnapshot login;
    XLoginPolicySnapshot loginBack;
    XByteArray* bytes;
    const uint8_t* data;
    uint32_t size;

    mgr = XAccountManager_create();
    XAccountManager_ensureDefaults(mgr);
    XAccountManager_snapshot(mgr, "root", &snap);

    /* 账户帧。 */
    bytes = XAccessControlCodec_accountToBson(&snap);
    CHECK("账户 BSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("账户 BSON 解码", XAccessControlCodec_accountFromBson(&snapBack, data, size));
        CHECK("账户 BSON 往返一致", strcmp(snapBack.m_name, snap.m_name) == 0 &&
              snapBack.m_role == snap.m_role && snapBack.m_locked == snap.m_locked &&
              snapBack.m_defaultPass == snap.m_defaultPass);
        XClassDelete((XClass*)bytes);
    }
    bytes = XAccessControlCodec_accountToJson(&snap);
    CHECK("账户 JSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("账户 JSON 解码", XAccessControlCodec_accountFromJson(&snapBack, data, size));
        CHECK("账户 JSON 往返一致", strcmp(snapBack.m_name, snap.m_name) == 0 &&
              snapBack.m_role == snap.m_role);
        XClassDelete((XClass*)bytes);
    }
    /* 列表帧（编码烟囱测试）。 */
    bytes = XAccessControlCodec_accountListToBson(mgr);
    CHECK("账户列表帧编码", bytes != NULL);
    if (bytes) XClassDelete((XClass*)bytes);
    bytes = XAccessControlCodec_accountListToJson(mgr);
    CHECK("账户列表 JSON 编码", bytes != NULL);
    if (bytes) XClassDelete((XClass*)bytes);

    /* 权限策略帧。 */
    perm.m_rolePerms[0] = 0;
    perm.m_rolePerms[1] = (uint32_t)XAccessPerm_ViewTelemetry;
    perm.m_rolePerms[2] = (uint32_t)XAccessPerm_All;
    bytes = XAccessControlCodec_permPolicyToBson(&perm);
    CHECK("权限策略 BSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("权限策略 BSON 解码", XAccessControlCodec_permPolicyFromBson(&permBack, data, size));
        CHECK("权限策略 BSON 往返一致", permBack.m_rolePerms[0] == perm.m_rolePerms[0] &&
              permBack.m_rolePerms[1] == perm.m_rolePerms[1] &&
              permBack.m_rolePerms[2] == perm.m_rolePerms[2]);
        XClassDelete((XClass*)bytes);
    }
    bytes = XAccessControlCodec_permPolicyToJson(&perm);
    CHECK("权限策略 JSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("权限策略 JSON 解码", XAccessControlCodec_permPolicyFromJson(&permBack, data, size));
        CHECK("权限策略 JSON 往返一致", permBack.m_rolePerms[1] == perm.m_rolePerms[1]);
        XClassDelete((XClass*)bytes);
    }

    /* 登录策略帧。 */
    login.m_allowAnonymous = false;
    login.m_allowConcurrent = true;
    login.m_concurrencyMode = (int)XLoginConcurrency_KickOld;
    login.m_maxSessions = 4;
    bytes = XAccessControlCodec_loginPolicyToBson(&login);
    CHECK("登录策略 BSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("登录策略 BSON 解码", XAccessControlCodec_loginPolicyFromBson(&loginBack, data, size));
        CHECK("登录策略 BSON 往返一致", loginBack.m_allowConcurrent == true &&
              loginBack.m_concurrencyMode == login.m_concurrencyMode &&
              loginBack.m_maxSessions == 4);
        XClassDelete((XClass*)bytes);
    }
    bytes = XAccessControlCodec_loginPolicyToJson(&login);
    CHECK("登录策略 JSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("登录策略 JSON 解码", XAccessControlCodec_loginPolicyFromJson(&loginBack, data, size));
        CHECK("登录策略 JSON 往返一致", loginBack.m_allowConcurrent == true);
        XClassDelete((XClass*)bytes);
    }

    /* 事件帧。 */
    bytes = XAccessControlCodec_permDeniedToBson(1, 0x10, 99);
    CHECK("权限拒绝事件帧编码", bytes != NULL);
    if (bytes) XClassDelete((XClass*)bytes);
    bytes = XAccessControlCodec_loginRejectedToJson("root", "该账户已登录，不允许同时在线", 5);
    CHECK("登录拒绝事件帧编码", bytes != NULL);
    if (bytes) XClassDelete((XClass*)bytes);

    /* 非法解码。 */
    CHECK("非法输入拒绝", !XAccessControlCodec_accountFromBson(&snapBack, NULL, 0) &&
          !XAccessControlCodec_permPolicyFromJson(&permBack, NULL, 0) &&
          !XAccessControlCodec_loginPolicyFromBson(&loginBack, NULL, 0));
    XClassDelete((XClass*)mgr);
}

/** @brief 跑一轮全部用例（泄漏压测循环体）。 */
static void accessRunAll(void)
{
    test_accounts();
    test_lockout();
    test_permissions();
    test_loginPolicy();
    test_codec();
}

/**
 * @brief 泄漏压测：全部用例循环 iterations 轮，堆在用字节数必须回基线。
 */
static void accessLeakLoop(int iterations)
{
    XMemoryStatistics base;
    XMemoryStatistics now;
    int i;
    if (!XMemory_statisticsEnabled()) {
        ++g_pass;
        XPrintf("[SKIP] 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < iterations; ++i) {
        accessRunAll();
        /* 文件设备/XObject 走 deleteLater：不泵事件循环会积压成"假泄漏"。 */
        XCoreApplication_processEvents(0);
    }
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("泄漏压测：堆在用字节数回基线", now.systemBytes == base.systemBytes);
    if (now.systemBytes != base.systemBytes)
        XPrintf("       基线=%llu 现在=%llu（差 %lld）\n",
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派） ==================== */

int XAccessControlTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    accessRunAll();
    XPrintf("\n===== XAccessControlTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    XPrintf("===== 泄漏压测 200 轮… =====\n");
    accessLeakLoop(200);
    XPrintf("===== XAccessControlTest(含压测): %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XAccessControlTest(XVariant* data)
{
    const int fails = XAccessControlTest_run();
    (void)data;
    XPrintf("=== XAccessControlTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XACCOUNT_ON || !XACCESS_CONTROL_ON */

#include "XPrintf.h"
int XAccessControlTest_run(void)
{
    XPrintf("XAccessControlTest: XINDUSTRY/XACCOUNT/XACCESS_CONTROL 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XACCOUNT_ON && XACCESS_CONTROL_ON */
