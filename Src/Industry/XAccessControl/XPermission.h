/**
 * @file       XPermission.h
 * @brief      权限策略类公开 API：角色→权限位图（RBAC）+ O(1) 判定 + 拒绝留痕。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场访问控制矩阵：哪类人
 *             能动哪些东西，一张表定死，判定不跑循环）。
 *
 *             [分工红线——只授权不认证]
 *             "角色从哪来"（登录/挑战应答）归链路层（XLink），"谁有哪些
 *             账户"归 XAccountManager；本类只回答"**这个角色能不能做这件
 *             事**"。判定入口全部是纯函数口径（角色+权限点 → bool），无
 *             会话、无身份、无 IO——热路径每命令一次判定，位掩码 O(1)。
 *
 *             [四档角色矩阵（本批扩展）]
 *               匿名 None     —— 只能查看：订阅遥测/快照/历史分页；
 *               普通 User     —— 查看 + 下载数据（导出/下载）；
 *               操作者 Operator—— 普通 + 设备控制（变频器/DO/阀/泵）；
 *               管理员 Admin  —— 全部（模式切换/自动运行/账户与配置）。
 *             角色值 Operator 为 **追加值 3**（不插到 1/2 之间，存量账户库
 *             不受影响）；故本类**按角色值开槽查表**，绝不做数值大小比较。
 *
 *             ============================================================================
 *                               设计约束（实现与使用方都必须遵守）
 *             ============================================================================
 *              1. **缺省拒绝**——未知角色、表里没写的位一律 false。多给=漏洞，
 *                 少给=配置问题（可查）。
 *              2. **Admin 恒全权**——管理员权限集不可被改小（setRolePerms 对
 *                 Admin 只接受 All）；同时**各档不得越过自己的上限**
 *                 （roleCeiling）——4 档"逐级包含"是产品约束，不可配。
 *                 杜绝"把最后一个管理员改瞎"自锁——与 XAccountManager 的
 *                 "最后一个管理员不可删/降"同一不变式的权限侧。
 *              3. **权限点是稳定契约**——XAccessPerm 数值上线不重排，两端
 *                 共用口径只加不减；新增能力只加位。
 *              4. **判定零遍历**——位掩码 O(1)；禁止在 check 路径上做任何
 *                 循环查表/字符串比较。
 *              5. **拒绝必须留痕**——check 失败恒发 permissionDenied（带角色/
 *                 权限/时刻），安全审计最小面；使用方决定落日志还是推远端。
 *                 [留痕开关] setAuditEnabled(false) 可关（高频轮询场景防刷），
 *                 缺省开。
 *              6. **策略变更必广播**——policyChanged 后使用方让在途会话按新
 *                 策略重判；本类**刻意不缓存**判定结果（有缓存就有失效窗口，
 *                 窗口即漏洞）。
 *              7. **只授权不认证**——见分工红线；本类不猜身份。
 *
 *             对象为句柄性质：禁止拷贝/移动语义；可能在自身信号调用栈上被
 *             删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XPERMISSION_H
#define XPERMISSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

#include "XAccount.h"   /* XAccountRole（角色枚举与账户类共用一张口径）。 */

/* ==================== 权限点与角色 ==================== */

/**
 * @brief 权限策略快照（值语义；序列化输入，XAccessControlCodec 消费）。
 * @details 下标=XAccountRole 值（0=匿名 1=User 2=Admin 3=Operator）。
 *          **表按角色值开槽，不是按权限档位排序**——Operator 是后追加的
 *          角色值 3，语义上位于 User 与 Admin 之间（见 XAccountRole 注释）。
 */
typedef struct XPermPolicySnapshot {
    uint32_t m_rolePerms[4];   /**< 角色→权限位图。 */
} XPermPolicySnapshot;

/** @brief 角色槽位数（= XAccountRole 取值个数）。 */
#define XPERM_ROLE_SLOTS 4

/**
 * @brief 权限点（位掩码；**稳定契约**——数值上线不重排，只加不减）。
 * @details 覆盖当前全部命令面的最小集合；新增能力时追加位并给出缺省表
 *          归属（下表"缺省"列），不改旧位。
 *
 *          四档角色缺省表（严格逐级包含）：
 *            匿名 None     = ViewTelemetry
 *            普通 User     = ViewTelemetry | Download
 *            操作者 Operator= ViewTelemetry | Download | DeviceControl
 *            管理 Admin    = All
 */
typedef enum XAccessPerm {
    XAccessPerm_None          = 0,
    XAccessPerm_ViewTelemetry = 1 << 0,  /**< 只读面：订阅遥测/拉快照/看历史（缺省 匿名+）。 */
    XAccessPerm_DeviceControl = 1 << 1,  /**< 设备控制：变频器/DO/阀/泵（缺省 操作者+）。 */
    XAccessPerm_ModeSwitch    = 1 << 2,  /**< 运行模式切换：手/自动（缺省 管理员）。 */
    XAccessPerm_AutoControl   = 1 << 3,  /**< 自动运行：启停/急停/复位（缺省 管理员）。 */
    XAccessPerm_AccountAdmin  = 1 << 4,  /**< 账户与权限管理/服务端配置（缺省 管理员）。 */
    XAccessPerm_Download      = 1 << 5,  /**< 数据下载/导出（缺省 普通用户+；**追加位**，不重排旧位）。 */
    XAccessPerm_All = 0x3Fu              /**< 全权限（Admin 恒为此值）。 */
} XAccessPerm;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XPermPolicy)
XCLASS_DEFINE_EXTEND_END(XPermPolicy, XObject)

/**
 * @brief XPermPolicy 权限策略。
 * @details 成员禁止直接访问，一律经 API 操作。策略表是三行位图（None/User/
 *          Admin），缺省表见各权限点注释；覆盖经 setRolePerms（Admin 行
 *          恒为 All，不可改小）。
 */
typedef struct XPermPolicy {
    XObject m_class;             /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    uint32_t m_rolePerms[XPERM_ROLE_SLOTS]; /**< 角色→权限位图（下标=XAccountRole 值）。 */
    bool m_audit;                /**< 拒绝留痕开关（缺省开）。 */
} XPermPolicy;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XPermPolicy 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XPermPolicy_class_init(void);

/**
 * @brief 栈上初始化权限策略（缺省表：匿名=只读 / 普通=只读+下载 /
 *        操作者=+控制 / 管理=全权）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 */
void XPermPolicy_init(XPermPolicy* obj);

/**
 * @brief 堆上创建权限策略。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XPermPolicy* XPermPolicy_create(void);

/* ==================== 判定（零遍历热路径） ==================== */

/**
 * @brief 判定角色是否持有 permMask 的**全部**权限位（位全含才真）。
 * @param pm 目标策略；NULL 按全拒绝处理（缺省拒绝，约束 1）。
 * @param role 角色（未知值按 None 处理）。
 * @param permMask 要求的权限位组合（一命令可一次验多个权限点）。
 * @return 全部持有返回 true；否则 false（并按 m_audit 发 permissionDenied）。
 * @note permMask=0 恒 true（没有要求就不拒绝）；未知角色恒 false。
 */
bool XPermPolicy_check(XPermPolicy* pm, XAccountRole role, uint32_t permMask);

/**
 * @brief 判定角色是否持有 permMask 的**任一**权限位（"读或写"类分支用）。
 * @note 任一持有即真；permMask=0 恒 false（任一语义下空集不成立）。
 */
bool XPermPolicy_checkAny(XPermPolicy* pm, XAccountRole role, uint32_t permMask);

/**
 * @brief 角色的权限位图（未知角色返回 0——缺省拒绝）。
 */
uint32_t XPermPolicy_rolePerms(const XPermPolicy* pm, XAccountRole role);

/* ==================== 策略覆盖 ==================== */

/**
 * @brief 覆盖某角色的权限位图（落盘归调用方；覆盖即发 policyChanged）。
 * @param pm 目标策略；不能为 NULL。
 * @param role 目标角色（四档均可覆盖；**每档有上限**：匿名⊆只读、普通⊆
 *             只读+下载、操作者⊆只读+下载+控制、**Admin 恒为 All**——
 *             超上限一律拒绝，约束 2）。
 * @param permMask 新权限位图（**超上限位/未知位一律拒绝**——与约束 2 的
 *              "逐级包含"配套；不再静默丢弃，避免"设了但没生效"的静默差异）。
 * @return 设置成功返回 true；角色非法/超上限返回 false。
 * @note  上限表就是为了保证四档"逐级包含"的现场语义：即使运维把策略
 *        改成匿名可控制，本类也会拒绘——"匿名只能看"是产品约束，不是
 *        可配置项。
 */
bool XPermPolicy_setRolePerms(XPermPolicy* pm, XAccountRole role, uint32_t permMask);

/**
 * @brief 回缺省表（匿名=只读 / 普通=只读+下载 / 操作者=+控制 / 管理=全权）
 *        并发 policyChanged。
 * @return 成功返回 true；参数非法返回 false。
 */
bool XPermPolicy_resetPolicy(XPermPolicy* pm);

/**
 * @brief 角色上限表（该角色**最多**能持有的权限位；越界位会被丢弃）。
 * @param role 角色（未知值按匿名上限处理）。
 * @return 权限位上上限。
 * @note  使用方可用于界面灰化/协议侧自校；判定仍以 check 为准。
 */
uint32_t XPermPolicy_roleCeiling(XAccountRole role);

/**
 * @brief 角色显示名（中文，供界面/日志；未知值返回"未知"）。
 * @return 静态常量串，不可释放。
 */
const char* XPermPolicy_roleName(XAccountRole role);

/**
 * @brief 拒绝留痕开关（缺省开；高频轮询场景可关防刷，约束 5）。
 */
bool XPermPolicy_setAuditEnabled(XPermPolicy* pm, bool enable);
bool XPermPolicy_auditEnabled(const XPermPolicy* pm);

/**
 * @brief 填充权限策略快照（序列化输入）。
 * @param pm 目标策略；可为 NULL（out 清零）。
 * @param out 输出快照（调用方提供存储；不能为 NULL）。
 */
void XPermPolicy_snapshot(const XPermPolicy* pm, XPermPolicySnapshot* out);

/* ==================== 信号 ==================== */

/**
 * @brief permissionDenied 信号：一次判定被拒（参数：role int, perms int32
 *        ——被拒的 permMask, atMs int64）。
 * @param pm 发射信号的策略；经 XSignal() 宏取地址连接，禁止直调。
 * @param role 被拒角色。
 * @param permMask 被拒的权限组合。
 * @param atMs 判定时刻（单调毫秒）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XPermPolicy_permissionDenied_signal(XPermPolicy* pm, XAccountRole role,
                                          int32_t permMask, int64_t atMs);

/**
 * @brief policyChanged 信号：策略表变化（setRolePerms/resetPolicy 都发）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XPermPolicy_policyChanged_signal(XPermPolicy* pm);

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPERMISSION_H */
