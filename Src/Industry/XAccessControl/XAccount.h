/**
 * @file       XAccount.h
 * @brief      账户管理类公开 API：账户 CRUD / 改密 / 锁定策略 / 缺省账户引导。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场账户管理：操作员/管理员
 *             两级角色 + 锁定保护 + 首次引导缺省账户）。
 *
 *             [与 XLinkUserStore 的分工——存储与管理两层]
 *             XLinkUserStore（XCore/XProtocol/XLink）是**存储与认证格式**：
 *             盐 + 迭代 HMAC 摘要 + 角色 + 锁定位的 JSON 库，XLinkServer
 *             挑战应答认证与 XConsoleShellLogin 共用同一格式。本类是
 *             **管理面**：持有它的值成员 + 落盘路径，在其上提供策略
 *             （口令长度/失败锁定）、不变式（管理员保底）、缺省账户引导、
 *             运行期失败计数与事件信号。记录格式**逐字节沿用**——现有
 *             hsf_users.json 直接迁移，两端口令互通不破。
 *
 *             [认证路径原样不动] 登录校验仍走 XLinkServer 内建挑战应答
 *             （底层 XLinkUserStore_verify）；本类的 verify 只是同一存储的
 *             直查入口（供服务端认证回调/测试用）。本类不认证、不持有
 *             会话——"角色从哪来"归链路层。
 *
 *             ============================================================================
 *                               设计约束（实现与使用方都必须遵守）
 *             ============================================================================
 *              1. **口令明文即刻派生摘要、不留存不落盘**——沿用盐+迭代 HMAC
 *                 （XLinkUserStore_upsert），原文过了就没了；任何日志/信号/
 *                 序列化都不得出现口令明文。
 *              2. **管理员不变式**——任何操作后必须仍存在 ≥1 个未锁定的
 *                 Admin。remove/setRole/lock 三处统一过闸：动最后一个未锁定
 *                 管理员一律拒绝（漏一处系统就没人能管）。
 *              3. **缺省账户只引导一次**——ensureDefaults 仅空库时建
 *                 admin/admin（只读）与 root/root（管理），建过一次永不重建；
 *                 建完置"缺省口令"运行期标记，改密即清；标记经
 *                 XAccount_isDefaultPassword 暴露，使用方负责把它变成
 *                 界面提示/告警（缺省口令长期不改是隐患）。
 *              4. **锁定是保护不是惩罚**——阈值锁定（连续失败≥lockThreshold）
 *                 的解除 = 人工 unlock 或超时自动解（isLockedNow 惰性判定），
 *                 不删账户不降角色；失败计数成功即清零。
 *              5. **单线程事件循环口径**——全部 API 不阻塞不加锁（与 XLink
 *                 会话同域）；load/save 走 XLinkUserStore 的原子写。
 *              6. **失败回灌只认结果**——noteFailure/noteSuccess 由认证方
 *                 喂，本类不猜登录成败。
 *
 *             对象为句柄性质：禁止拷贝/移动语义；可能在自身信号调用栈上被
 *             删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XACCOUNT_H
#define XACCOUNT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if XINDUSTRY_ON && XACCOUNT_ON

#include "XString.h"
#include "XLinkUserStore.h"   /* 记录格式与 JSON 文件互通（同款盐+迭代 HMAC）。 */

/* ==================== 常量与角色 ==================== */

/**
 * @brief 账户角色（与 XLinkAuthRole **逐值一致**，注释互相点名，不做
 *        运行时转换——两边换值必须同步改，见 XLinkProto.h XLinkAuthRole）。
 */
typedef enum XAccountRole {
    XAccountRole_None  = 0,   /**< 未认证/匿名（与 XLinkAuthRole_Anonymous 同值）。 */
    XAccountRole_User  = 1,   /**< 操作员/只读（与 XLinkAuthRole_User 同值）。 */
    XAccountRole_Admin = 2    /**< 管理员/全权（与 XLinkAuthRole_Admin 同值）。 */
} XAccountRole;

/**
 * @brief 锁定原因（accountLocked 信号与审计口径）。
 */
typedef enum XAccountLockCause {
    XAccountLockCause_None = 0,        /**< 占位/解锁事件不带原因。 */
    XAccountLockCause_Failures = 1,    /**< 连续失败达阈值（自动锁定）。 */
    XAccountLockCause_Manual = 2       /**< 人工锁定。 */
} XAccountLockCause;

/** @brief 用户名缓冲口径（与 XLinkUserStore 同上限，含结尾 0）。 */
#define XACCOUNT_NAME_MAX XLINK_AUTH_NAME_BYTES

/**
 * @brief 单账户快照（值语义，零分配；序列化输入，XAccessControlCodec 消费）。
 * @details 面向远端管理界面的**非敏感投影**：无摘要/盐/迭代数——凭据材料
 *          永不出账户库（约束 1）。
 */
typedef struct XAccountSnapshot {
    char m_name[XACCOUNT_NAME_MAX]; /**< 用户名（UTF-8）。 */
    int  m_role;                    /**< 角色（XAccountRole）。 */
    bool m_locked;                  /**< 是否锁定（持久位）。 */
    bool m_defaultPass;             /**< 是否仍在用缺省口令（运行期标记）。 */
} XAccountSnapshot;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XAccountManager)
XCLASS_DEFINE_EXTEND_END(XAccountManager, XObject)

/**
 * @brief XAccountManager 账户管理器。
 * @details 成员禁止直接访问，一律经 API 操作。m_store 为值成员（拥有记录），
 *          m_path 为路径存储；失败计数/缺省口令标记是**运行期**表（不落盘
 *          ——重启即归零，锁定态本身在记录里有持久位，不会因重启丢失）。
 */
typedef struct XAccountManager {
    XObject m_class;                  /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XLinkUserStore m_store;           /**< 账户库（值成员；记录格式与 XLink 完全一致）。 */
    char m_path[600];                 /**< 账户库 JSON 路径（空串=未设，save 拒绝）。 */
    uint32_t m_minPassLen;            /**< 口令最小长度（缺省 6；0=不限）。 */
    uint32_t m_lockThreshold;         /**< 连续失败锁定阈值（缺省 5；0=不自动锁）。 */
    uint64_t m_lockMs;                /**< 锁定时长（毫秒；缺省 300000；0=人工解锁才放）。 */
    /** @brief 运行期失败计数表（按账户名；容量=库容量）。 */
    struct {
        char m_name[XACCOUNT_NAME_MAX];
        uint32_t m_fails;
        uint64_t m_lockUntilMs;       /**< 自动锁定到期时刻（0=非自动锁定/已解）。 */
    } m_failTrack[XLINK_AUTH_USER_SLOTS];
    /** @brief 缺省口令标记表（ensureDefaults 建的账户置位，改密清除）。 */
    bool m_defaultPass[XLINK_AUTH_USER_SLOTS];
} XAccountManager;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XAccountManager 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XAccountManager_class_init(void);

/**
 * @brief 栈上初始化账户管理器（空库、缺省策略）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；缺省策略见各 set 函数。
 */
void XAccountManager_init(XAccountManager* obj);

/**
 * @brief 堆上创建账户管理器。
 * @return 新管理器对象；内存分配失败返回 NULL。不再使用时必须调用
 *         XClassDelete 释放；对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XAccountManager* XAccountManager_create(void);

/* ==================== 持久化 ==================== */

/**
 * @brief 设置账户库 JSON 路径（load/save 用；重复设置覆盖）。
 * @param mgr 目标管理器；不能为 NULL。
 * @param path UTF-8 路径（借用，内部拷贝；NULL/空串=清除）。
 * @return 设置成功返回 true；参数非法返回 false。
 */
bool XAccountManager_setPath(XAccountManager* mgr, const char* path);

/**
 * @brief 从盘加载账户库（须先 setPath）。
 * @param mgr 目标管理器。
 * @return 加载成功（含"文件不存在=空库"）返回 true；文件损坏返回 false
 *         （库保持清空，不半加载）。
 * @note 加载后运行期表（失败计数/缺省口令标记）全部复位——缺省口令标记
 *       不持久化，重启后如需重新提示由调用方按需重建。
 */
bool XAccountManager_load(XAccountManager* mgr);

/**
 * @brief 账户库落盘（原子写：临时文件+重命名；须先 setPath）。
 * @return 成功返回 true；未设路径/写失败返回 false。
 * @note 所有变更 API 成功后**不自动落盘**——落盘时机归调用方（批量变更
 *       一次写），本类只提供入口并发 storeSaved 信号。
 */
bool XAccountManager_save(XAccountManager* mgr);

/**
 * @brief 当前库文件路径（借用；未设返回空串）。
 */
const char* XAccountManager_path(const XAccountManager* mgr);

/* ==================== 策略 ==================== */

/** @brief 口令最小长度（0=不限）；add/setPassword/resetPassword 统一闸。 */
bool XAccountManager_setMinPasswordLen(XAccountManager* mgr, uint32_t len);
uint32_t XAccountManager_minPasswordLen(const XAccountManager* mgr);

/** @brief 连续失败锁定阈值（0=不自动锁；缺省 5）。 */
bool XAccountManager_setLockThreshold(XAccountManager* mgr, uint32_t count);
uint32_t XAccountManager_lockThreshold(const XAccountManager* mgr);

/** @brief 锁定时长（毫秒；0=人工解锁才放；缺省 5 分钟）。 */
bool XAccountManager_setLockDurationMs(XAccountManager* mgr, uint64_t ms);
uint64_t XAccountManager_lockDurationMs(const XAccountManager* mgr);

/* ==================== 缺省账户引导 ==================== */

/**
 * @brief 空库时创建缺省账户（admin/admin 只读 + root/root 管理）并标记
 *        "缺省口令"；非空库为幂等无操作。
 * @param mgr 目标管理器；不能为 NULL。
 * @return 创建了缺省账户返回 true；库非空（无需引导）返回 false。
 * @note 只查 count==0，不识别"半破坏库"——文件损坏时 load 已回空库，
 *       引导是正确行为（现场恢复路径）。
 */
bool XAccountManager_ensureDefaults(XAccountManager* mgr);

/* ==================== 账户 CRUD ==================== */

/**
 * @brief 新增账户（重名/满容/口令过短/角色非法拒绝）。
 * @param mgr 目标管理器；不能为 NULL。
 * @param name 用户名（UTF-8 非空；超长拒绝）。
 * @param password 口令明文（借用；即刻派生摘要，不留存）。
 * @param role 角色（None 视为非法——账户必须有身份）。
 * @return 成功返回 true；失败返回 false（不变更库）。
 * @note 成功发 accountAdded(name)；不落盘（save 归调用方）。
 */
bool XAccountManager_add(XAccountManager* mgr, const char* name,
                         const char* password, XAccountRole role);

/**
 * @brief 删除账户（**最后一个未锁定管理员不可删**——约束 2）。
 * @return 成功返回 true；不存在/不变式拒绝返回 false。
 * @note 成功发 accountRemoved(name)，并清该账户运行期表。
 */
bool XAccountManager_remove(XAccountManager* mgr, const char* name);

/**
 * @brief 调整角色（**把最后一个未锁定管理员降级被拒**——约束 2）。
 * @param role 目标角色（None 视为非法）。
 * @return 成功返回 true；不存在/非法/不变式拒绝返回 false。
 * @note 成功发 accountChanged(name)。
 */
bool XAccountManager_setRole(XAccountManager* mgr, const char* name,
                             XAccountRole role);

/**
 * @brief 本人改密（验证旧口令；锁定账户拒绝改密）。
 * @param name 用户名。
 * @param oldPassword 旧口令明文（借用）。
 * @param newPassword 新口令明文（借用；长度过短拒绝）。
 * @return 成功返回 true；验证失败/参数非法返回 false。
 * @note 成功清"缺省口令"标记并发 accountChanged(name)。
 */
bool XAccountManager_setPassword(XAccountManager* mgr, const char* name,
                                 const char* oldPassword, const char* newPassword);

/**
 * @brief 管理员强制重置口令（不验旧口令；锁定账户允许——解锁流程一部分）。
 * @return 成功返回 true；账户不存在/口令过短返回 false。
 * @note 与 setPassword 的区别是**授权语义**：调用方必须已确认持有
 *       AccountAdmin 权限（本类不认证，见头文件分工）。
 */
bool XAccountManager_resetPassword(XAccountManager* mgr, const char* name,
                                   const char* newPassword);

/* ==================== 锁定 ==================== */

/**
 * @brief 人工锁定（**锁最后一个未锁定管理员被拒**——约束 2）。
 * @return 成功返回 true；不存在/已锁/不变式拒绝返回 false。
 * @note 成功发 accountLocked(name, Manual)。
 */
bool XAccountManager_lock(XAccountManager* mgr, const char* name);

/**
 * @brief 人工解锁（清失败计数 + 清锁定持久位 + 清自动锁定到期时刻）。
 * @return 成功返回 true；不存在/未锁返回 false。
 * @note 成功发 accountLocked(name, None)——解锁也是账户状态事件。
 */
bool XAccountManager_unlock(XAccountManager* mgr, const char* name);

/**
 * @brief 认证回灌：一次失败（连续计数 +1；达阈值→自动锁定+信号）。
 * @param nowMs 当前单调毫秒（锁定到期时刻口径）。
 */
void XAccountManager_noteFailure(XAccountManager* mgr, const char* name,
                                 uint64_t nowMs);

/**
 * @brief 认证回灌：一次成功（清失败计数）。
 */
void XAccountManager_noteSuccess(XAccountManager* mgr, const char* name);

/**
 * @brief 账户当前是否锁定（含**到期自动解**的惰性判定）。
 * @param nowMs 当前单调毫秒；自动锁定到期即就地解锁（发 accountLocked
 *              (name, None)）并返回 false。
 * @return 锁定中返回 true；账户不存在返回 false。
 * @note 持久锁定位（XLinkUserStore）与运行期到期时刻共同判定——重启后
 *       自动锁定按"已到期"处理（时长语义跨重启不保留，锁定持久位保留）。
 */
bool XAccountManager_isLockedNow(XAccountManager* mgr, const char* name,
                                 uint64_t nowMs);

/* ==================== 查询 ==================== */

/** @brief 账户数。 */
uint32_t XAccountManager_count(const XAccountManager* mgr);

/**
 * @brief 按用户名查记录（只读借用；有效至下一次修改）。
 * @return 未找到返回 NULL。
 */
const XLinkUserRecord* XAccountManager_find(const XAccountManager* mgr,
                                            const char* name);

/** @brief 账户是否存在。 */
bool XAccountManager_exists(const XAccountManager* mgr, const char* name);

/**
 * @brief 账户角色（None=不存在/匿名）。
 */
XAccountRole XAccountManager_role(const XAccountManager* mgr, const char* name);

/**
 * @brief 该账户是否仍在用缺省口令（ensureDefaults 建的账户，改密前为真）。
 * @return 是返回 true；不存在/非缺省账户返回 false。
 */
bool XAccountManager_isDefaultPassword(const XAccountManager* mgr, const char* name);

/**
 * @brief 直查校验（存储层 verify 的薄透传；返回码与其一致）。
 * @return 0=通过；1=用户不存在；2=账户锁定（持久位，不含到期判定）；
 *         3=口令错误。**不回灌 noteFailure/noteSuccess**——回灌归认证方。
 */
int XAccountManager_verify(XAccountManager* mgr, const char* name,
                           const char* password, uint8_t* outRole);

/**
 * @brief 填充单账户快照（序列化输入）。
 * @param mgr 目标管理器；可为 NULL（out 清零）。
 * @param name 用户名；不存在时 out 清零。
 * @param out 输出快照（调用方提供存储；不能为 NULL）。
 */
void XAccountManager_snapshot(const XAccountManager* mgr, const char* name,
                              XAccountSnapshot* out);

/* ==================== 信号 ==================== */

/** @brief accountAdded 信号：新增账户（参数：name utf8）。 */
void* XAccountManager_accountAdded_signal(XAccountManager* mgr, const char* name);
/** @brief accountRemoved 信号：删除账户（参数：name utf8）。 */
void* XAccountManager_accountRemoved_signal(XAccountManager* mgr, const char* name);
/** @brief accountChanged 信号：角色/口令变更（参数：name utf8）。 */
void* XAccountManager_accountChanged_signal(XAccountManager* mgr, const char* name);
/** @brief accountLocked 信号：锁定状态变化（参数：name utf8, cause int——
 *        XAccountLockCause；None=解锁）。 */
void* XAccountManager_accountLocked_signal(XAccountManager* mgr, const char* name,
                                           int cause);
/** @brief storeSaved 信号：账户库已落盘（参数：path utf8）。 */
void* XAccountManager_storeSaved_signal(XAccountManager* mgr, const char* path);

#endif /* XINDUSTRY_ON && XACCOUNT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XACCOUNT_H */
