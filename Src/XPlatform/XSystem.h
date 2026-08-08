/**
 * @file       XSystem.h
 * @brief      系统复位、关机和有序重启的平台抽象接口。
 * @details    本接口没有直接对应的 Qt 类型，只定义跨平台的复位、关机、重启契约，
 *             不包含 Linux、Windows、FreeRTOS、Zephyr 或具体芯片头文件。
 *             Shell 和业务层只能调用本文件的公共 API；平台默认实现位于
 *             Drive，其他目标可在启动阶段注册产品回调。产品回调优先于平台
 *             默认实现。回调应在单线程初始化阶段注册，注册完成后才允许并发
 *             调用；库不为回调本身增加锁，也不取得 userData 所有权。
 */

#ifndef XSYSTEM_H
#define XSYSTEM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统复位或重启操作的统一结果。
 * @details 枚举值互斥、不可按位组合；调用方不得依赖未列出的负值。
 */
typedef enum XSystemResult {
    XSystemResult_Ok = 0,                /**< 请求已接受；实际操作可能不会返回。 */
    XSystemResult_InvalidArgument = -1,  /**< 参数无效。 */
    XSystemResult_NotSupported = -2,     /**< 当前平台或产品未提供后端。 */
    XSystemResult_Failed = -3,           /**< 后端拒绝或执行失败。 */
    XSystemResult_PermissionDenied = -4  /**< 当前执行上下文没有复位或重启权限。 */
} XSystemResult;

/**
 * @brief 触发硬件或系统复位时使用的原因。
 * @details 枚举值互斥、不可按位组合；超出已声明范围的值会被公共 API 拒绝。
 */
typedef enum XSystemResetReason {
    XSystemResetReason_Shell = 0,        /**< 由 Shell reset 命令触发。 */
    XSystemResetReason_Watchdog,         /**< 由看门狗或故障恢复逻辑触发。 */
    XSystemResetReason_Firmware          /**< 由固件升级或产品流程触发。 */
} XSystemResetReason;

/**
 * @brief 有序重启的启动目标。
 * @details 枚举值互斥、不可按位组合；平台无法进入指定目标时返回
 *          XSystemResult_NotSupported。
 */
typedef enum XSystemRebootMode {
    XSystemRebootMode_Normal = 0,        /**< 正常启动当前固件或系统。 */
    XSystemRebootMode_Bootloader         /**< 请求进入产品指定的启动加载器。 */
} XSystemRebootMode;

/**
 * @brief 系统复位回调。
 * @param userData 注册时传入的产品上下文；可为 NULL。库保存该指针值直至回调
 *                 被清除，但不复制或释放其指向的资源。
 * @param reason 触发复位的统一原因；保证为有效的 XSystemResetReason 枚举值。
 * @return 请求接受返回 XSystemResult_Ok；未实现、权限不足或执行失败返回对应结果。
 * @note 真实硬件复位通常不会返回；回调不得在返回后继续使用已销毁资源。
 */
typedef XSystemResult (*XSystemResetHandler)(void* userData,
                                             XSystemResetReason reason);

/**
 * @brief 有序重启回调。
 * @param userData 注册时传入的产品上下文；可为 NULL。库保存该指针值直至回调
 *                 被清除，但不复制或释放其指向的资源。
 * @param mode 重启后的启动目标；保证为有效的 XSystemRebootMode 枚举值。
 * @return 请求接受返回 XSystemResult_Ok；未实现、权限不足或执行失败返回对应结果。
 * @note 回调应先完成产品允许的同步、停止服务和持久化，再进入重启流程。
 */
typedef XSystemResult (*XSystemRebootHandler)(void* userData,
                                              XSystemRebootMode mode);

/**
 * @brief 系统关机回调。
 * @param userData 注册时传入的产品上下文；可为 NULL。库保存该指针值直至回调
 *                 被清除，但不复制或释放其指向的资源。
 * @return 请求接受返回 XSystemResult_Ok；未实现、权限不足或执行失败返回对应结果。
 * @note 回调应先完成产品允许的同步、停止服务和持久化，再进入关机流程；真实
 *       关机成功后通常不会返回。
 */
typedef XSystemResult (*XSystemShutdownHandler)(void* userData);

/**
 * @brief 注册或清除系统复位覆盖回调。
 * @param handler 复位回调；可为 NULL，传 NULL 时清除覆盖并恢复平台默认后端。
 * @param userData 原样传给 handler 的产品上下文；可为 NULL。handler 非 NULL 时
 *                 库保存该借用指针直至覆盖或清除回调，但不释放所指资源；
 *                 handler 为 NULL 时忽略该参数。
 * @return 无。函数返回后，后续 XSystem_reset 调用使用新配置。
 * @note 应在 Shell 启动前完成配置，运行期间不要并发修改。
 */
void XSystem_setResetHandler(XSystemResetHandler handler, void* userData);

/**
 * @brief 注册或清除有序重启覆盖回调。
 * @param handler 重启回调；可为 NULL，传 NULL 时清除覆盖并恢复平台默认后端。
 * @param userData 原样传给 handler 的产品上下文；可为 NULL。handler 非 NULL 时
 *                 库保存该借用指针直至覆盖或清除回调，但不释放所指资源；
 *                 handler 为 NULL 时忽略该参数。
 * @return 无。函数返回后，后续 XSystem_reboot 调用使用新配置。
 * @note 应在 Shell 启动前完成配置，运行期间不要并发修改。
 */
void XSystem_setRebootHandler(XSystemRebootHandler handler, void* userData);

/**
 * @brief 注册或清除系统关机覆盖回调。
 * @param handler 关机回调；可为 NULL，传 NULL 时清除覆盖并恢复平台默认后端。
 * @param userData 原样传给 handler 的产品上下文；可为 NULL。handler 非 NULL 时
 *                 库保存该借用指针直至覆盖或清除回调，但不释放所指资源；
 *                 handler 为 NULL 时忽略该参数。
 * @return 无。函数返回后，后续 XSystem_shutdown 调用使用新配置。
 * @note 应在 Shell 启动前完成配置，运行期间不要并发修改。
 */
void XSystem_setShutdownHandler(XSystemShutdownHandler handler, void* userData);

/**
 * @brief 请求立即系统复位。
 * @param reason 复位原因；必须是已声明的 XSystemResetReason 枚举值。
 * @return 回调或平台后端接受请求返回 XSystemResult_Ok；reason 越界返回
 *         XSystemResult_InvalidArgument；后端未提供、权限不足或执行失败返回对应结果。
 * @note 已注册产品回调时不会调用平台默认后端；当 XPLATFORM_HAS_OS 为 0
 *       时也不会调用默认后端并返回 XSystemResult_NotSupported；真实复位
 *       成功后通常不会返回。
 */
XSystemResult XSystem_reset(XSystemResetReason reason);

/**
 * @brief 请求有序系统重启。
 * @param mode 重启后的启动目标；必须是已声明的 XSystemRebootMode 枚举值。
 * @return 回调或平台后端接受请求返回 XSystemResult_Ok；mode 越界返回
 *         XSystemResult_InvalidArgument；后端未提供、权限不足或执行失败返回对应结果。
 * @note 已注册产品回调时不会调用平台默认后端；当 XPLATFORM_HAS_OS 为 0
 *       时也不会调用默认后端并返回 XSystemResult_NotSupported；真实重启
 *       成功后通常不会返回。
 */
XSystemResult XSystem_reboot(XSystemRebootMode mode);

/**
 * @brief 请求立即关闭当前系统。
 * @return 回调或平台后端接受请求返回 XSystemResult_Ok；后端未提供、权限不足
 *         或执行失败返回对应结果。
 * @note 已注册产品回调时不会调用平台默认后端；当 XPLATFORM_HAS_OS 为 0
 *       时也不会调用默认后端并返回 XSystemResult_NotSupported；真实关机
 *       成功后通常不会返回。
 */
XSystemResult XSystem_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* XSYSTEM_H */
