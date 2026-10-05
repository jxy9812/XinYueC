/**
 * @file       XMemory_config.h
 * @brief      XMemory 内存模块的编译期配置。
 * @details    配置项可通过编译器 -D 覆盖，便于嵌入式工程按系统堆容量裁剪
 *             全局池。默认值随平台自动取舍（平台判定沿用
 *             Src/CXinYueConfig.h 的 XPLATFORM_DESKTOP）：
 *             - 桌面（Windows/POSIX）：大池，沿用全局池硬编码时期
 *               （2026-10 之前）的实测值，保证桌面行为零变化；
 *             - 嵌入式（FreeRTOS/裸机）：小池，避免小容量系统堆在首个
 *               XCoreApplication 初始化全局池时被一次性耗尽。
 *             每一项都用 #ifndef 包裹：CMake add_compile_definitions 或
 *             工程 -D 显式传入的值优先生效（如神舟 F407 分支）。
 */
#ifndef XMEMORY_CONFIG_H
#define XMEMORY_CONFIG_H

#include "CXinYueConfig.h"

/** @brief 全局池 32B 档块数（XMultiPool 全局池 power-of-two 模式首档）。 */
#ifndef XMP_GLOBAL_C32
#if XPLATFORM_DESKTOP
#define XMP_GLOBAL_C32 256
#else
#define XMP_GLOBAL_C32 16
#endif
#endif

/** @brief 全局池 64B 档块数。 */
#ifndef XMP_GLOBAL_C64
#if XPLATFORM_DESKTOP
#define XMP_GLOBAL_C64 256
#else
#define XMP_GLOBAL_C64 8
#endif
#endif

/** @brief 全局池 128B 档块数。 */
#ifndef XMP_GLOBAL_C128
#if XPLATFORM_DESKTOP
#define XMP_GLOBAL_C128 256
#else
#define XMP_GLOBAL_C128 4
#endif
#endif

/** @brief 全局池 256B 档块数。 */
#ifndef XMP_GLOBAL_C256
#if XPLATFORM_DESKTOP
#define XMP_GLOBAL_C256 128
#else
#define XMP_GLOBAL_C256 2
#endif
#endif

/** @brief 全局池 512B 档块数。 */
#ifndef XMP_GLOBAL_C512
#if XPLATFORM_DESKTOP
#define XMP_GLOBAL_C512 64
#else
#define XMP_GLOBAL_C512 1
#endif
#endif

#endif /* XMEMORY_CONFIG_H */
