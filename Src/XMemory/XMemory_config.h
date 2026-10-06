/**
 * @file       XMemory_config.h
 * @brief      XMemory 内存模块的编译期配置。
 * @details    配置项可通过编译器 -D 覆盖，便于嵌入式工程按系统堆容量裁剪
 *             内存池与全局池。默认值随平台自动取舍（平台判定沿用
 *             Src/CXinYueConfig.h 的 XPLATFORM_DESKTOP）：
 *             - 桌面（Windows/POSIX）：大池，沿用全局池硬编码时期
 *               （2026-10 之前）的实测值，保证桌面行为零变化；
 *             - 嵌入式（FreeRTOS/裸机）：小池，避免小容量系统堆在首个
 *               XCoreApplication 初始化全局池时被一次性耗尽。
 *             每一项都用 #ifndef 包裹：CMake add_compile_definitions 或
 *             工程 -D 显式传入的值优先生效（如神舟 F407 分支）。
 *
 *             三大区块：
 *             1. 内存池裁剪开关——按池逐个裁剪（XFixedPool/XMultiPool/
 *                XVariablePool/HYBRID），裁掉的池整个编译单元变空，
 *                不占 Flash/RAM，对应分发槽位按回落链自动换装；
 *             2. 全局多级池（XMultiPool global）默认参数；
 *             3. 全局可变池（XVariablePool global）默认参数——嵌入式平台
 *                默认接入 XMemory 分发（裸机系统槽回落到它）。
 */
#ifndef XMEMORY_CONFIG_H
#define XMEMORY_CONFIG_H

#include "CXinYueConfig.h"

/* ============================================================================
 * 1. 内存池裁剪开关
 * ============================================================================ */

/**
 * @brief XVariablePool（TLSF 可变大小池）总开关。
 * @details 置 0 时 XVariablePool.h/.c 整体裁剪，XMEMORY_TYPE_VARIABLEPOOL
 *          槽位回落系统槽；裸机系统槽也退回空实现（无池可回落）。
 *          嵌入式推荐保留：它是裸机上默认的系统级分配后端。默认开。
 */
#ifndef XMEMORY_VARIABLEPOOL_ON
#define XMEMORY_VARIABLEPOOL_ON 1
#endif

/**
 * @brief XMultiPool（多级固定池）总开关。
 * @details 置 0 时 XMultiPool.h/.c 整体裁剪（连带全局池），MULTIPOOL 槽位
 *          按回落链换装：可变池可用时回落全局可变池，否则回落系统槽，
 *          XMalloc_MultiPool 等既有调用点无需改动。默认开。
 */
#ifndef XMEMORY_MULTIPOOL_ON
#define XMEMORY_MULTIPOOL_ON 1
#endif

/**
 * @brief XFixedPool（固定块池）总开关。
 * @details XFixedPool 是 XMultiPool 唯一的库内消费者，默认跟随
 *          XMEMORY_MULTIPOOL_ON（裁掉多级池即连带裁掉固定块池，省
 *          Flash）；单独直接使用 XFixedPool 的工程可显式置 1 保留。
 */
#ifndef XMEMORY_FIXEDPOOL_ON
#define XMEMORY_FIXEDPOOL_ON XMEMORY_MULTIPOOL_ON
#endif

#if XMEMORY_MULTIPOOL_ON && !XMEMORY_FIXEDPOOL_ON
#error "XMEMORY_MULTIPOOL_ON=1 依赖 XMEMORY_FIXEDPOOL_ON=1（XMultiPool 以 XFixedPool 为底座）"
#endif

/**
 * @brief HYBRID 混合槽位开关。
 * @details 置 0 时 HYBRID 槽位回落系统槽，hybrid_* 分派代码整体裁剪。
 *          默认开。
 */
#ifndef XMEMORY_HYBRID_ON
#define XMEMORY_HYBRID_ON 1
#endif

/**
 * @brief HYBRID 混合模式小块阈值（字节）：小于等于该值的申请走
 *        MULTIPOOL 槽位（随之裁剪回落），大于则走系统堆。
 */
#ifndef XMEMORY_HYBRID_THRESHOLD
#define XMEMORY_HYBRID_THRESHOLD 256
#endif

/* ============================================================================
 * 2. 全局多级池（XMultiPool global）默认参数
 * ============================================================================ */

/**
 * @brief 全局池倍数模式首档块大小（字节）与增长倍数。
 * @details 全局池按 initial_size × multiplier^n 逐档建子池，
 *          默认 32 起步、翻倍：32/64/128/256/512 共五档。
 */
#ifndef XMP_GLOBAL_INITIAL_SIZE
#define XMP_GLOBAL_INITIAL_SIZE 32
#endif

#ifndef XMP_GLOBAL_GROWTH_MULTIPLIER
#define XMP_GLOBAL_GROWTH_MULTIPLIER 2
#endif

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

/* ============================================================================
 * 3. 全局可变池（XVariablePool global）默认参数
 *    嵌入式默认接入 XMemory 分发：裸机系统槽回落到它，FreeRTOS/桌面
 *    经 XMEMORY_TYPE_VARIABLEPOOL 槽位使用。
 * ============================================================================ */

/**
 * @brief 全局 TLSF 池 arena 字节数。
 * @details 桌面/FreeRTOS 首次使用时经系统堆（malloc/pvPortMalloc）惰性
 *          创建，不用不占内存；裸机为 .bss 静态 arena，常驻 RAM——
 *          裸机默认给小值，按固件实际用量以 -D 收放。
 */
#ifndef XVP_GLOBAL_ARENA_BYTES
#if XPLATFORM_DESKTOP
#define XVP_GLOBAL_ARENA_BYTES (256 * 1024)
#elif XPLATFORM_FREERTOS
#define XVP_GLOBAL_ARENA_BYTES (32 * 1024)
#else
#define XVP_GLOBAL_ARENA_BYTES (16 * 1024)
#endif
#endif

/**
 * @brief 全局 TLSF 池用户数据对齐（字节）。
 * @details 0 = 使用 sizeof(void*)；须为 2 的幂。DMA/Cache 行对齐需求
 *          的工程可显式指定（如 32）。
 */
#ifndef XVP_GLOBAL_ALIGNMENT
#define XVP_GLOBAL_ALIGNMENT 0
#endif

/**
 * @brief 全局 TLSF 池是否加 XAtomic 自旋锁保护。
 * @details 默认有 OS（桌面/FreeRTOS）开启、裸机关闭（单线程固件免锁
 *          开销）；裸机多任务共用全局池时须显式置 1。
 *          置 0 时全局池为纯单线程快速路径。
 */
#ifndef XVP_GLOBAL_THREADSAFE
#define XVP_GLOBAL_THREADSAFE XPLATFORM_HAS_OS
#endif

#endif /* XMEMORY_CONFIG_H */
