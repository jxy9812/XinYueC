/**
 * @file       XSystem_Protected.h
 * @brief      XSystem 内部平台后端契约。
 * @details    本文件没有直接对应的 Qt 类型，仅供 XSystem 公共分发实现和
 *             Drive 平台文件包含。上层业务和 Shell 不得直接调用这些函数，
 *             也不得包含平台头文件。每个构建目标必须由 Linux、Windows、
 *             板级后端或 unsupported 后端提供一组同名实现。
 */

#ifndef XSYSTEM_PROTECTED_H
#define XSYSTEM_PROTECTED_H

#include "XSystem.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 调用当前平台的立即系统复位实现。
 * @param reason 复位原因；保证为有效的 XSystemResetReason 枚举值，平台可将其
 *               写入诊断或复位记录。
 * @return 平台接受请求返回 XSystemResult_Ok；权限不足、不支持或执行失败返回
 *         对应的 XSystemResult，函数不得返回未声明的正值。
 * @note 该函数不调用产品覆盖回调；真实硬件复位成功后通常不会返回。
 */
XSystemResult XSystem_platformReset(XSystemResetReason reason);

/**
 * @brief 调用当前平台的有序系统重启实现。
 * @param mode 重启后的启动目标；保证为有效的 XSystemRebootMode 枚举值。
 * @return 平台接受请求返回 XSystemResult_Ok；目标不受支持、权限不足或执行失败
 *         返回对应的 XSystemResult，函数不得返回未声明的正值。
 * @note 该函数不调用产品覆盖回调；真实系统重启成功后通常不会返回。
 */
XSystemResult XSystem_platformReboot(XSystemRebootMode mode);

/**
 * @brief 调用当前平台的系统关机实现。
 * @return 平台接受请求返回 XSystemResult_Ok；权限不足、不支持或执行失败返回
 *         对应的 XSystemResult，函数不得返回未声明的正值。
 * @note 该函数不调用产品覆盖回调；真实系统关机成功后通常不会返回。
 */
XSystemResult XSystem_platformShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* XSYSTEM_PROTECTED_H */
