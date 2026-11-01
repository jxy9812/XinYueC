/**
 * @file XProcessConfig.h
 * @brief XProcess 模块的统一编译配置。
 * @details
 * 本文件只定义 XProcess 及其公开辅助能力的编译开关和容量参数；不调用
 * 平台 API，也不包含平台头文件。实际进程创建、管道和回收由 XinYueC
 * Drive 后端实现，公共层只依赖本文件定义的抽象。
 */

#ifndef XPROCESS_CONFIG_H
#define XPROCESS_CONFIG_H

/* 允许直接包含本文件的用户获得与 CXinYueConfig 相同的容器开关。 */
#include "XContainerConfig.h"

/** @brief XProcess 总开关；1 启用公开进程 API，0 从构建中裁剪整个模块。 */
#ifndef XProcess_ON
#define XProcess_ON 1
#endif

/** @brief QProcessEnvironment 公开 API 开关。 */
#ifndef XPROCESS_ENVIRONMENT_ON
#define XPROCESS_ENVIRONMENT_ON 1
#endif

/** @brief 同步启动、等待、读写和状态查询 API 开关。 */
#ifndef XPROCESS_SYNC_ON
#define XPROCESS_SYNC_ON 1
#endif

/** @brief XObject 信号和 XIODevice 信号适配开关。 */
#ifndef XPROCESS_SIGNAL_ON
#define XPROCESS_SIGNAL_ON 1
#endif

/** @brief detached 启动 API 开关。 */
#ifndef XPROCESS_DETACHED_ON
#define XPROCESS_DETACHED_ON 1
#endif

/** @brief 标准输入/输出/错误重定向 API 开关。 */
#ifndef XPROCESS_REDIRECT_ON
#define XPROCESS_REDIRECT_ON 1
#endif

/** @brief 进程间标准输出连接 API 开关。 */
#ifndef XPROCESS_PIPE_ON
#define XPROCESS_PIPE_ON 1
#endif

/** @brief UnixProcessParameters 公开 API 开关；非 Unix 后端保留字段但不执行。 */
#ifndef XPROCESS_UNIX_PARAMETERS_ON
#define XPROCESS_UNIX_PARAMETERS_ON 1
#endif

/** @brief 公共 API 默认的最长程序名和工作目录字节数。 */
#ifndef XPROCESS_MAX_PATH
#define XPROCESS_MAX_PATH 4096
#endif

/**
 * @brief 进程标准输入、标准输出和标准错误环形缓冲的单个 chunk 大小。
 * @details
 * XProcess 使用 XRingBuffer 分段保存管道数据；该值是每段的容量，不是
 * 整个缓冲区的上限。缓冲区在数据持续增长时按相同大小追加 chunk，设置
 * 较小值可降低空闲内存，设置较大值可减少分段和分配次数。
 */
#ifndef XPROCESS_IO_BUFFER_SIZE
#define XPROCESS_IO_BUFFER_SIZE 4096
#endif

/** @brief 进程参数数量上限；0 表示由动态列表容量限制。 */
#ifndef XPROCESS_MAX_ARGUMENTS
#define XPROCESS_MAX_ARGUMENTS 256
#endif

/* 关闭总开关时强制关闭所有派生能力，避免头文件和实现不一致。 */
#if !XProcess_ON
#undef XPROCESS_ENVIRONMENT_ON
#define XPROCESS_ENVIRONMENT_ON 0
#undef XPROCESS_SYNC_ON
#define XPROCESS_SYNC_ON 0
#undef XPROCESS_SIGNAL_ON
#define XPROCESS_SIGNAL_ON 0
#undef XPROCESS_DETACHED_ON
#define XPROCESS_DETACHED_ON 0
#undef XPROCESS_REDIRECT_ON
#define XPROCESS_REDIRECT_ON 0
#undef XPROCESS_PIPE_ON
#define XPROCESS_PIPE_ON 0
#undef XPROCESS_UNIX_PARAMETERS_ON
#define XPROCESS_UNIX_PARAMETERS_ON 0
#endif

/* 环境、参数列表需要字符串容器；关闭依赖时不生成半可用的接口。 */
#if XProcess_ON && (!XPROCESS_ENVIRONMENT_ON || !XString_ON)
#if XPROCESS_ENVIRONMENT_ON && !XString_ON
#undef XPROCESS_ENVIRONMENT_ON
#define XPROCESS_ENVIRONMENT_ON 0
#endif
#endif

#if (XProcess_ON != 0) && (XProcess_ON != 1)
#error "XProcess: XProcess_ON 只能设置为 0 或 1"
#endif
#if (XPROCESS_IO_BUFFER_SIZE < 64)
#error "XProcess: XPROCESS_IO_BUFFER_SIZE 不能小于 64"
#endif
#if (XPROCESS_MAX_ARGUMENTS < 1)
#error "XProcess: XPROCESS_MAX_ARGUMENTS 必须大于 0"
#endif

#endif /* XPROCESS_CONFIG_H */
