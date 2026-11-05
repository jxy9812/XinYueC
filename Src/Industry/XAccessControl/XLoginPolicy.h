/**
 * @file       XLoginPolicy.h
 * @brief      登录策略类公开 API：匿名登录 / 同账并发 / 会话上限的准入判定。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场"谁能进系统、能不能
 *             两处同时在线"的准入规则）。
 *
 *             [与访问控制三件的分工]
 *             XAccountManager（谁：凭证/CRUD/锁定）→ XPermission（能做什么：
 *             角色→权限）→ **本类（能不能进来/能不能并存：会话准入）**。
 *             本类不持有会话——会话表在链路层（XLink），本类只回答"这次
 *             登录准不准入"（调用方喂当前会话数与目标账户），与"只授权
 *             不认证"同一口径。
 *
 *             ============================================================================
 *                               设计约束（实现与使用方都必须遵守）
 *             ============================================================================
 *              8. **匿名是双闸**——allowAnonymous 只是"准进来"，匿名会话拿
 *                 XAccountRole_None，**能看到什么仍由权限策略表的 None 行
 *                 决定**（缺省拒绝 ⇒ 开了匿名也看不到东西；两道闸各自独立、
 *                 缺省全关）。
 *              9. **Kick 只踢同账户**——并发接管绝不能殃及其他账户的会话
 *                 （B 换班不该把 A 踢下线）；踢谁（会话句柄）由调用方选，
 *                 本类只给"该踢同账户旧会话"的结论。
 *             10. **拒绝必须可读**——admitSession 拒绝时给中文原因，客户端
 *                 原样展示；拒绝事件恒发 loginRejected（审计面，与
 *                 permissionDenied 同款）。
 *             11. **策略改动即广播**——policyChanged 后新会话立即按新策略
 *                 判定；在途会话**不追溯**（已准入的连接不中途掐断——掐断
 *                 是显式管理动作，不是策略的副作用）。
 *
 *             持久化：策略存独立小 JSON（XJsonDocument，格式自由）——不与
 *             账户库混文件（账户库格式要跨端互通，不能加段）。
 *
 *             对象为句柄性质：禁止拷贝/移动语义；可能在自身信号调用栈上被
 *             删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XLOGINPOLICY_H
#define XLOGINPOLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

/* ==================== 常量 ==================== */

/** @brief 拒绝原因缓冲容量（UTF-8 中文结论；超长截断）。 */
#define XLOGIN_POLICY_REASON_MAX 96

/** @brief 缺省会话总数上限（监控端/平板数量级；防资源滥用）。 */
#define XLOGIN_POLICY_DEFAULT_MAX_SESSIONS 8u

/**
 * @brief 同账并发处置（并发被拒时对旧会话怎么办）。
 */
typedef enum XLoginConcurrencyMode {
    XLoginConcurrency_DenyNew = 0,  /**< 拒绝新登录，旧会话保住（缺省：防误开第二处控制）。 */
    XLoginConcurrency_KickOld       /**< 踢旧会话，新登录接管（换班接管用，须显式选）。 */
} XLoginConcurrencyMode;

/**
 * @brief 登录策略快照（值语义；序列化输入，XAccessControlCodec 消费）。
 */
typedef struct XLoginPolicySnapshot {
    bool     m_allowAnonymous;   /**< 是否允许匿名登录。 */
    bool     m_allowConcurrent;  /**< 是否允许同账户多会话。 */
    int      m_concurrencyMode;  /**< 并发处置（XLoginConcurrencyMode）。 */
    uint32_t m_maxSessions;      /**< 会话总数上限。 */
} XLoginPolicySnapshot;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XLoginPolicy)
XCLASS_DEFINE_EXTEND_END(XLoginPolicy, XObject)

/**
 * @brief XLoginPolicy 登录策略。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct XLoginPolicy {
    XObject m_class;                    /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    bool m_allowAnonymous;              /**< 是否允许匿名登录（缺省 false）。 */
    bool m_allowConcurrent;             /**< 是否允许同账户多会话（缺省 false）。 */
    int m_concurrencyMode;              /**< 并发处置（XLoginConcurrencyMode；缺省 DenyNew）。 */
    uint32_t m_maxSessions;             /**< 会话总数上限（缺省 8；0=不限）。 */
    char m_path[600];                   /**< 策略 JSON 路径（空串=未设）。 */
} XLoginPolicy;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XLoginPolicy 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XLoginPolicy_class_init(void);

/**
 * @brief 栈上初始化登录策略（全关/上限 8/DenyNew）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 */
void XLoginPolicy_init(XLoginPolicy* obj);

/**
 * @brief 堆上创建登录策略。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XLoginPolicy* XLoginPolicy_create(void);

/* ==================== 策略项 ==================== */

/**
 * @brief 是否允许匿名登录（缺省 false）。
 * @note 匿名是**双闸**：这里只开"准进来"，匿名会话（XAccountRole_None）
 *       能看什么仍由 XPermission 的 None 行决定（约束 8）。
 */
bool XLoginPolicy_setAllowAnonymous(XLoginPolicy* lp, bool allow);
bool XLoginPolicy_anonymousAllowed(const XLoginPolicy* lp);

/**
 * @brief 是否允许同账户多会话（缺省 false：root 两处同时在线=问责断链）。
 */
bool XLoginPolicy_setAllowConcurrent(XLoginPolicy* lp, bool allow);
bool XLoginPolicy_concurrentAllowed(const XLoginPolicy* lp);

/**
 * @brief 并发处置模式（缺省 DenyNew；KickOld 给换班接管，须显式选）。
 * @return 设置成功返回 true；mode 非法返回 false。
 */
bool XLoginPolicy_setConcurrencyMode(XLoginPolicy* lp, XLoginConcurrencyMode mode);
XLoginConcurrencyMode XLoginPolicy_concurrencyMode(const XLoginPolicy* lp);

/**
 * @brief 会话总数上限（0=不限；缺省 8）。
 */
bool XLoginPolicy_setMaxSessions(XLoginPolicy* lp, uint32_t maxSessions);
uint32_t XLoginPolicy_maxSessions(const XLoginPolicy* lp);

/* ==================== 准入判定 ==================== */

/**
 * @brief 会话准入判定（建会话前的唯一闸；依次过：总数上限→匿名→同账并发）。
 * @param lp 目标策略；NULL 按"全拒绝"处理（缺省拒绝口径）。
 * @param anonymous 本次登录是否匿名（无账户名）。
 * @param activeSessions 当前总会话数（调用方从链路层喂；本类不持有会话）。
 * @param sameAccountSessions 该账户当前会话数（匿名传 0）。
 * @param outReason 拒绝原因输出（UTF-8 中文结论，可原样回给客户端；
 *                  可为 NULL）。允许时不写。
 * @param cap outReason 容量。
 * @param outKickSameAccount 判定为 KickOld 且同账已有会话时置 true（调用方
 *                           据此去链路层踢**同账户**旧会话——踢谁由调用方
 *                           选，约束 9）；可为 NULL。允许且无需踢时不写。
 * @return 准入返回 true；被拒返回 false（并按约束 10 发 loginRejected）。
 */
bool XLoginPolicy_admitSession(XLoginPolicy* lp, bool anonymous,
                               uint32_t activeSessions,
                               uint32_t sameAccountSessions,
                               char* outReason, int cap,
                               bool* outKickSameAccount);

/* ==================== 持久化 ==================== */

/**
 * @brief 设置策略 JSON 路径（load/save 用）。
 * @return 设置成功返回 true；参数非法返回 false。
 */
bool XLoginPolicy_setPath(XLoginPolicy* lp, const char* path);

/**
 * @brief 从盘加载策略（须先 setPath；文件不存在=保持当前值返回 true）。
 * @return 加载成功返回 true；文件损坏返回 false（策略不变，不半加载）。
 */
bool XLoginPolicy_load(XLoginPolicy* lp);

/**
 * @brief 策略落盘（原子写；须先 setPath；成功发 storeSaved）。
 */
bool XLoginPolicy_save(XLoginPolicy* lp);

/**
 * @brief 填充登录策略快照（序列化输入）。
 * @param lp 目标策略；可为 NULL（out 清零）。
 * @param out 输出快照（调用方提供存储；不能为 NULL）。
 */
void XLoginPolicy_snapshot(const XLoginPolicy* lp, XLoginPolicySnapshot* out);

/* ==================== 信号 ==================== */

/**
 * @brief loginRejected 信号：一次准入被拒（参数：name utf8——匿名传空串；
 *        reason utf8；atMs int64）。
 * @param lp 发射信号的策略；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XLoginPolicy_loginRejected_signal(XLoginPolicy* lp, const char* name,
                                        const char* reason, int64_t atMs);

/**
 * @brief policyChanged 信号：任一策略项变化（set 族都发）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XLoginPolicy_policyChanged_signal(XLoginPolicy* lp);

/**
 * @brief storeSaved 信号：策略已落盘（参数：path utf8）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XLoginPolicy_storeSaved_signal(XLoginPolicy* lp, const char* path);

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLOGINPOLICY_H */
