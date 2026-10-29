/** @file       XLinkUserStore.h
 * @brief      XLink 登录账户库：JSON 文件持久化（盐 + 迭代 HMAC 摘要 + 角色）。
 * @details    XLINK_AUTH_ON 下的账户存储实现，供 XLinkServer 认证策略使用：
 *               - 服务端只保存口令派生摘要 H（原文即刻派生、不留存），
 *                 与 XConsoleShellLogin 的账户库格式一致（随机盐 +
 *                 迭代 HMAC-SHA256），两边可互换/迁移；
 *               - 角色（匿名/普通/管理员）随账户保存，认证通过后由
 *                 AuthResult 下发；
 *               - 文件读写全部经 XDeviceFile/XJsonDocument，不直接使用
 *                 stdio 或平台 API；
 *               - 路径由调用方指定（通常为可写分区上的 JSON 文件）。
 *
 *             线程模型：单线程主循环口径，与 XLink 会话一致——所有 API
 *             不可阻塞、不加锁，须在持有者的事件循环内使用。
 * @author     XinYueC 团队
 */
#ifndef XLINKUSERSTORE_H
#define XLINKUSERSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XLink_config.h"

#if XLINK_ON && XLINK_AUTH_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"

/**
 * @brief 账户记录（定长摘要 + XString 用户名）。
 * @details 布局优化：三个布尔/枚举状态位收敛为一个位域单元；用户名走
 *          XString（UTF-8 API 原生），避免定长截断与手工 memcpy。
 *          口令摘要与盐保持定长二进制（它们是线格式的一部分，不做字符串化）。
 */
typedef struct XLinkUserRecord {
    XString* m_name;                              /**< 用户名（对象拥有，XString UTF-8）。 */
    uint32_t m_iterations;                        /**< 口令派生迭代次数。 */
    uint8_t  m_salt[XLINK_AUTH_SALT_BYTES];       /**< 随机盐。 */
    uint8_t  m_hash[XLINK_AUTH_NONCE_BYTES];      /**< H = iteratedHMAC(口令, 盐)。 */
    uint8_t  m_role;                              /**< 角色，见 XLinkAuthRole。 */
    uint8_t  m_flags;                             /**< 状态位（见 XLINK_USER_FLAG_*）。 */
} XLinkUserRecord;

/** @brief 账户状态位：已设口令。 */
#define XLINK_USER_FLAG_HAS_PASSWORD 0x01u
/** @brief 账户状态位：已锁定。 */
#define XLINK_USER_FLAG_LOCKED       0x02u

/**
 * @brief 账户库对象（内联数组，无动态容器）。
 * @details 用 init/deinit 管理生命周期；`XLinkUserStore` 为值类型，
 *          可嵌入服务端私有块，也可栈上使用。
 */
typedef struct XLinkUserStore {
    XLinkUserRecord m_users[XLINK_AUTH_USER_SLOTS]; /**< 账户表（定容）。 */
    uint32_t m_count;                                /**< 当前账户数。 */
} XLinkUserStore;

/**
 * @brief      初始化账户库（空表）。
 * @param      store 账户库（调用方提供存储）；NULL 时无操作。
 */
void XLinkUserStore_init(XLinkUserStore* store);

/**
 * @brief      清空账户库（不落盘；敏感摘要零化）。
 * @param      store 账户库；NULL 时无操作。
 */
void XLinkUserStore_clear(XLinkUserStore* store);

/**
 * @brief      从 JSON 文件加载账户库。
 * @param      store 账户库。
 * @param      path UTF-8 文件路径。
 * @return     成功返回 true。
 * @note       文件不存在视为成功（空库，*outExists 置 false）；文件损坏
 *             返回 false 且库保持清空状态（不半加载）。
 */
bool XLinkUserStore_load(XLinkUserStore* store, const char* path,
                         bool* outExists);

/**
 * @brief      保存账户库到 JSON 文件（原子写：临时文件 + 重命名）。
 * @param      store 账户库。
 * @param      path UTF-8 文件路径。
 * @return     成功返回 true。
 * @note       文件权限尽力设为仅属主可读写（不支持的后端忽略）。
 */
bool XLinkUserStore_save(XLinkUserStore* store, const char* path);

/**
 * @brief      添加或更新账户（口令给出时同时设置/重置口令）。
 * @param      store 账户库。
 * @param      user 用户名（UTF-8，非空，长度 < XLINK_AUTH_NAME_BYTES）。
 * @param      password 口令明文（借用，不保存）；为 NULL/空串表示不改口令。
 * @param      role 角色（XLinkAuthRole）。
 * @return     成功返回 true；库满/参数非法/派生失败返回 false。
 * @note       新建账户必须给口令；已存在账户仅更新角色/口令。
 */
bool XLinkUserStore_upsert(XLinkUserStore* store, const char* user,
                           const char* password, uint8_t role);

/**
 * @brief      删除账户。
 * @param      store 账户库。
 * @param      user 用户名。
 * @return     删除成功返回 true；不存在返回 false。
 */
bool XLinkUserStore_remove(XLinkUserStore* store, const char* user);

/**
 * @brief      设置账户锁定状态。
 * @param      store 账户库；user 用户名；locked 是否锁定。
 * @return     成功返回 true；用户不存在返回 false。
 */
bool XLinkUserStore_setLocked(XLinkUserStore* store, const char* user,
                              bool locked);

/**
 * @brief      按用户名查找账户（只读视图）。
 * @param      store 账户库。
 * @param      user 用户名。
 * @return     借用指针（有效至下一次修改/清理）；未找到返回 NULL。
 */
const XLinkUserRecord* XLinkUserStore_find(const XLinkUserStore* store,
                                           const char* user);

/**
 * @brief      获取账户用户名（UTF-8 借用视图）。
 * @param      record 账户记录（借用）；可为 NULL。
 * @return     借用指针；NULL 或空名返回 NULL。
 * @note       指向 XString 内部编码缓存，账户被修改/删除后失效。
 */
const char* XLinkUserStore_name_const(const XLinkUserRecord* record);

/**
 * @brief      查询账户是否已锁定。
 * @param      record 账户记录（借用）；可为 NULL。
 * @return     已锁定返回 true。
 */
bool XLinkUserStore_isLocked(const XLinkUserRecord* record);

/**
 * @brief      查询账户是否已设置口令。
 * @param      record 账户记录（借用）；可为 NULL。
 * @return     已设口令返回 true。
 */
bool XLinkUserStore_hasPassword(const XLinkUserRecord* record);

/**
 * @brief      获取账户数量。
 * @param      store 账户库；NULL 时返回 0。
 * @return     账户数。
 */
uint32_t XLinkUserStore_count(const XLinkUserStore* store);

/**
 * @brief      按下标获取账户（只读视图）。
 * @param      store 账户库。
 * @param      index 下标 [0, count)。
 * @return     借用指针；越界返回 NULL。
 */
const XLinkUserRecord* XLinkUserStore_at(const XLinkUserStore* store,
                                         uint32_t index);

/**
 * @brief      校验口令（供认证内核使用）。
 * @param      store 账户库。
 * @param      user 用户名。
 * @param      password 口令明文（借用）。
 * @param      outRole 输出角色（可为 NULL）。
 * @return     0 = 通过；1 = 用户不存在；2 = 账户锁定；3 = 口令错误。
 * @note       返回码与 XLinkSessionAuthLookupResult 数值对齐，便于直接
 *             作为查询回调结果使用；比较走常量时间。
 */
int XLinkUserStore_verify(XLinkUserStore* store, const char* user,
                          const char* password, uint8_t* outRole);

#endif /* XLINK_ON && XLINK_AUTH_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLINKUSERSTORE_H */
