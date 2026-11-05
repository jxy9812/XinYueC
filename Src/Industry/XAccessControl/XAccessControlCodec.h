/**
 * @file       XAccessControlCodec.h
 * @brief      访问控制编解码公开 API：账户/权限策略/登录策略/事件 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XAutoControlCodec / XSolenoidValveCodec 同一套约定：
 *               - JSON 与 BSON 键名一致，格式可互转；
 *               - 解码产出值结构（快照零分配）；
 *               - **凭据材料永不出本 Codec**——账户帧只含 name/role/locked/
 *                 defaultPass 投影，摘要/盐/迭代数不序列化（约束 1 的
 *                 序列化侧执行点）。
 *             典型用途：远端账户/权限管理经 XLink 拉策略快照、收拒绝事件
 *             （命令面 CMD_ACCOUNT_* 为扩展位，本批不实现）。
 */
#ifndef XACCESSCONTROLCODEC_H
#define XACCESSCONTROLCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XAccount.h"
#include "XPermission.h"
#include "XLoginPolicy.h"

/* ========================================================================== */
/*                      账户帧（单账户快照 / 全量列表）                         */
/* ========================================================================== */

/**
 * @brief 单账户快照 → BSON/JSON 帧（{name,role,locked,defaultPass}）。
 * @param snap 源快照（借用；NULL 视为全零快照）。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方释放。
 */
XByteArray* XAccessControlCodec_accountToBson(const XAccountSnapshot* snap);
XByteArray* XAccessControlCodec_accountToJson(const XAccountSnapshot* snap);

/**
 * @brief 账户帧 → 快照（就地填充；非法输入返回 false 且 out 已清零）。
 */
bool XAccessControlCodec_accountFromBson(XAccountSnapshot* out,
                                         const uint8_t* data, size_t size);
bool XAccessControlCodec_accountFromJson(XAccountSnapshot* out,
                                         const uint8_t* data, size_t size);

/**
 * @brief 全量账户列表帧 → BSON/JSON（{count, v{i}_name/role/locked/
 *        defaultPass}；前缀风格与阀/泵快照一致）。
 * @param mgr 源管理器（借用；NULL 输出 {count:0}）。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方释放。
 */
XByteArray* XAccessControlCodec_accountListToBson(const XAccountManager* mgr);
XByteArray* XAccessControlCodec_accountListToJson(const XAccountManager* mgr);

/* ========================================================================== */
/*                      权限策略帧                                             */
/* ========================================================================== */

/**
 * @brief 权限策略 → BSON/JSON 帧（{nonePerms,userPerms,adminPerms}；int 位图）。
 */
XByteArray* XAccessControlCodec_permPolicyToBson(const XPermPolicySnapshot* snap);
XByteArray* XAccessControlCodec_permPolicyToJson(const XPermPolicySnapshot* snap);

/** @brief 权限策略帧 → 快照（就地填充）。 */
bool XAccessControlCodec_permPolicyFromBson(XPermPolicySnapshot* out,
                                            const uint8_t* data, size_t size);
bool XAccessControlCodec_permPolicyFromJson(XPermPolicySnapshot* out,
                                            const uint8_t* data, size_t size);

/* ========================================================================== */
/*                      登录策略帧                                             */
/* ========================================================================== */

/**
 * @brief 登录策略 → BSON/JSON 帧（{allowAnonymous,allowConcurrent,
 *        concurrencyMode,maxSessions}）。
 */
XByteArray* XAccessControlCodec_loginPolicyToBson(const XLoginPolicySnapshot* snap);
XByteArray* XAccessControlCodec_loginPolicyToJson(const XLoginPolicySnapshot* snap);

/** @brief 登录策略帧 → 快照（就地填充）。 */
bool XAccessControlCodec_loginPolicyFromBson(XLoginPolicySnapshot* out,
                                             const uint8_t* data, size_t size);
bool XAccessControlCodec_loginPolicyFromJson(XLoginPolicySnapshot* out,
                                             const uint8_t* data, size_t size);

/* ========================================================================== */
/*                      事件帧（拒绝留痕的远端投影）                            */
/* ========================================================================== */

/**
 * @brief 权限拒绝事件帧 → BSON/JSON（{role,perm,atMs}）。
 * @param role 被拒角色；permMask 被拒权限组合；atMs 时刻（单调毫秒）。
 */
XByteArray* XAccessControlCodec_permDeniedToBson(int role, int32_t permMask,
                                                 int64_t atMs);
XByteArray* XAccessControlCodec_permDeniedToJson(int role, int32_t permMask,
                                                 int64_t atMs);

/**
 * @brief 登录拒绝事件帧 → BSON/JSON（{name,reason,atMs}；匿名 name=""）。
 */
XByteArray* XAccessControlCodec_loginRejectedToBson(const char* name,
                                                    const char* reason,
                                                    int64_t atMs);
XByteArray* XAccessControlCodec_loginRejectedToJson(const char* name,
                                                    const char* reason,
                                                    int64_t atMs);

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XACCESSCONTROLCODEC_H */
