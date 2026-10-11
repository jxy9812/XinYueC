/**
 * @file       XUpgradeIdentity.h
 * @brief      升级模块设备标识与当前版本提供者回调契约。
 * @details    产品在初始化阶段经本接口注册序列号与当前版本来源（模式同
 *             XSystem_setRebootHandler），引擎检查、灰度判定与日志只经本
 *             头文件取值，不直接调用平台 API。回调应在单线程初始化阶段
 *             注册，注册完成后才允许并发调用；库不复制 userData，也不为
 *             回调本身加锁。设备序列号是否下沉 XSystem 待产品线裁定
 *             （设计文档 §17.2），一律经本接口注册。
 *             灰度设备键（deviceKey，§11）与序列号的关系：deviceKey 经
 *             XUpgradeIdentity_deviceKey 按缺省回退链求值——已注册序列号
 *             提供者且返回非空 → SN；否则平台主网卡 MAC（12 位小写十六
 *             进制，实现侧经 Drive 平台适配取值）；仍不可得 → 安装时生成
 *             的持久 UUID（首次生成后原子写入安装根下
 *             XUPGRADE_FILE_DEVICE_KEY）。回退链第一级即本文件的
 *             XUpgradeIdentity_serial。
 */

#ifndef XUPGRADEIDENTITY_H
#define XUPGRADEIDENTITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 当前产品版本缺省值（编译期注入）。
 * @details 桌面目标由 CMake 注入真实版本定义（-DXUPGRADE_PRODUCT_VERSION="x.y.z"）；
 *          嵌入式目标由镜像头内嵌版本，自检时读镜像头比对。未注入时回退
 *          该缺省值，表示"版本未知"，引擎只允许手动检查。
 */
#ifndef XUPGRADE_PRODUCT_VERSION
#define XUPGRADE_PRODUCT_VERSION "0.0.0"
#endif

/** @brief 灰度设备键持久化文件名：安装根直下（与 pointers/ 同级），不属于
 *         update/ 工作区——update/ 整目录删除不改变设备键，保证灰度分桶
 *         跨升级/跨启动稳定（§11 判定一致性 + §16 不变式 5）。 */
#define XUPGRADE_FILE_DEVICE_KEY "device.key"
/** @brief 灰度设备键容量（SN、12 位 MAC hex 或 UUID 串均可容纳；含结尾 NUL）。 */
#define XUPGRADE_DEVICE_KEY_MAX 40

/**
 * @brief 当前版本提供者回调。
 * @param userData 注册时传入的产品上下文；可为 NULL，库原样回传且不取得
 *                 其所有权。
 * @param out 输出缓冲区；调用方提供存储空间，回调写入以 NUL 结尾的 UTF-8
 *            版本串（如 "1.2.3"），超长时截断并保证 NUL 结尾。
 * @param cap 输出缓冲区容量（字节，含结尾 NUL）；建议不低于
 *            XUPGRADE_VERSION_MAX。
 * @return 无。无有效版本时写入空串；回调不得长时间阻塞。
 * @note 版本串白名单 [0-9A-Za-z._-]（设计文档 §2），不得含路径分隔符。
 */
typedef void (*XUpgradeVersionProvider)(void* userData, char* out, uint32_t cap);

/**
 * @brief 设备序列号提供者回调。
 * @param userData 注册时传入的产品上下文；可为 NULL，库原样回传且不取得
 *                 其所有权。
 * @param out 输出缓冲区；调用方提供存储空间，回调写入以 NUL 结尾的 UTF-8
 *            序列号，超长时截断并保证 NUL 结尾。
 * @param cap 输出缓冲区容量（字节，含结尾 NUL）。
 * @return 无。设备无序列号时写入空串；回调不得长时间阻塞。
 * @note 序列号是灰度 allow/deny 名单与分流键的第一级来源（§11）：灰度判定
 *       统一经 XUpgradeIdentity_deviceKey 取值，其回退链（SN → MAC → 持久
 *       UUID）的第一级即本回调结果；未注册回调时该级跳过，不在此做 MAC/
 *       UUID 推导。产品应尽量注册本回调——SN 缺失时设备落 MAC/UUID 分桶，
 *       与服务端 SN 名单语义不同。
 */
typedef void (*XUpgradeSerialProvider)(void* userData, char* out, uint32_t cap);

/**
 * @brief 注册或清除当前版本提供者回调。
 * @param handler 版本回调；可为 NULL，传 NULL 时清除注册并恢复编译期缺省
 *                值 XUPGRADE_PRODUCT_VERSION。
 * @param userData 原样传给 handler 的产品上下文；可为 NULL。handler 非 NULL
 *                 时库保存该借用指针直至覆盖或清除，但不释放所指资源；
 *                 handler 为 NULL 时忽略该参数。
 * @return 无。函数返回后，后续 XUpgradeIdentity_version 调用使用新配置。
 * @note 应在 XUpgrade_init 之前完成注册，运行期间不要并发修改。
 */
void XUpgrade_setVersionProvider(XUpgradeVersionProvider handler, void* userData);

/**
 * @brief 注册或清除设备序列号提供者回调。
 * @param handler 序列号回调；可为 NULL，传 NULL 时清除注册（deviceKey 回退
 *                链的 SN 级随之跳过）。
 * @param userData 原样传给 handler 的产品上下文；可为 NULL。handler 非 NULL
 *                 时库保存该借用指针直至覆盖或清除，但不释放所指资源；
 *                 handler 为 NULL 时忽略该参数。
 * @return 无。函数返回后，后续 XUpgradeIdentity_serial 调用使用新配置。
 * @note 应在 XUpgrade_init 之前完成注册，运行期间不要并发修改。
 */
void XUpgrade_setSerialProvider(XUpgradeSerialProvider handler, void* userData);

/**
 * @brief 读取当前产品版本。
 * @param out 输出缓冲区；成功时写入以 NUL 结尾的 UTF-8 版本串。
 * @param cap 缓冲区容量（字节，含结尾 NUL）；0 返回 false。
 * @return 成功返回 true；out 为空、cap 为 0 或写入失败返回 false（此时 out
 *         内容未定义，调用方不得读取）。
 * @note 已注册提供者时以回调结果为准；未注册时回退编译期缺省值
 *       XUPGRADE_PRODUCT_VERSION，两者都无效（缺省值被覆盖为空串）返回 false。
 */
bool XUpgradeIdentity_version(char* out, uint32_t cap);

/**
 * @brief 读取设备序列号（灰度设备键回退链的第一级）。
 * @param out 输出缓冲区；成功时写入以 NUL 结尾的 UTF-8 序列号。
 * @param cap 缓冲区容量（字节，含结尾 NUL）；0 返回 false。
 * @return 已注册提供者且返回非空串返回 true；未注册或回调返回空串返回
 *         false（调用方应改走 XUpgradeIdentity_deviceKey 的回退链，而非
 *         自行推导）。
 * @note 本入口只回序列号本身，不做 MAC/UUID 回退；灰度判定一律取
 *       XUpgradeIdentity_deviceKey。
 */
bool XUpgradeIdentity_serial(char* out, uint32_t cap);

/**
 * @brief 读取灰度判定设备键（§11 缺省回退链收口）。
 * @param out 输出缓冲区；成功时写入以 NUL 结尾的 UTF-8 设备键。
 * @param cap 缓冲区容量（字节，含结尾 NUL）；建议不低于
 *            XUPGRADE_DEVICE_KEY_MAX，0 返回 false。
 * @return 取得非空设备键返回 true；回退链全部不可用（未注册序列号、MAC
 *         不可得且持久 UUID 文件不可读写）返回 false，灰度按未命中处理。
 * @note 回退链（每次调用按序求值）：1) XUpgradeIdentity_serial 成功 → SN；
 *       2) 平台网卡 MAC，12 位小写十六进制（实现侧经 Drive 平台适配取值；
 *       取"枚举序首个 up+running 非 loopback 接口"，无主网卡优先级判定，
 *       VPN/TAP 虚拟网卡靠前时即为其键——分桶稳定性成立，与服务端按主
 *       网卡建名单的语义可能错位，产品应优先注册 SN 提供者规避）；
 *       3) 持久 UUID——首次调用生成并经 XSaveFile 原子写入安装根下
 *       XUPGRADE_FILE_DEVICE_KEY（与 pointers/ 同级，不属于 update/ 工作区，
 *       §16 不变式 5）。键串白名单 [0-9A-Za-z-]。灰度命中哈希对 salt 与
 *       本键拼接计算（XUpgradeManifest_grayHit）；判定一致性依赖键稳定，
 *       产品升级/重启不改变回退链各级可用性即保持同桶。
 */
bool XUpgradeIdentity_deviceKey(char* out, uint32_t cap);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADEIDENTITY_H */
