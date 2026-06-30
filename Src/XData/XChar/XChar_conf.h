/**
 * @file XChar_conf.h
 * @brief XChar模块配置文件
 *
 * 通过此配置文件可以选择GBK编码转换的实现模式：
 *   1. 代码模式（静态数组）- 编译时嵌入数据
 *   2. 文件模式（读取外部文件）- 运行时读取BIN文件
 *   3. 系统API模式 - 调用操作系统API
 *
 * 优先级（从高到低）：
 *   XCHAR_USE_CODE_GBK > XCHAR_USE_FILE_GBK > XCHAR_USE_SYSTEM_GBK
 */

#ifndef XCHAR_CONF_H
#define XCHAR_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        模式选择                                            */
/* ========================================================================== */

/**
 * @brief GBK编码转换模式选择
 *
 * 三种模式只能启用一种，优先级：CODE > FILE > SYSTEM
 *
 * XCHAR_USE_CODE_GBK   - 代码模式（静态数组）
 *                        优点：无需外部文件，启动快，天生线程安全
 *                        缺点：增加固件大小（约80KB）
 *                        适用：Flash充足的嵌入式设备
 *                        线程安全：是（静态只读数据，多线程读取无竞争）
 *
 * XCHAR_USE_FILE_GBK   - 文件模式（读取外部文件）
 *                        优点：固件小，数据可更新
 *                        缺点：需要文件系统，启动时需打开文件
 *                        适用：有文件系统的嵌入式设备
 *                        线程安全：可选（通过XCHAR_FILE_THREAD_SAFE配置）
 *
 * XCHAR_USE_SYSTEM_GBK - 系统API模式
 *                        优点：无需额外数据，利用系统功能
 *                        缺点：依赖操作系统，嵌入式通常不支持
 *                        适用：Windows/Linux/macOS桌面应用
 *                        线程安全：是（系统API保证）
 */

/* 取消注释以启用对应模式 */

 #define XCHAR_USE_CODE_GBK      /* 代码模式 */
/* #define XCHAR_USE_FILE_GBK    */  /* 文件模式 */
/* #define XCHAR_USE_SYSTEM_GBK  */  /* 系统API模式 */

/* ========================================================================== */
/*                        文件模式配置                                         */
/* ========================================================================== */

#if defined(XCHAR_USE_FILE_GBK)

/**
 * @brief 文件模式线程安全配置
 *
 * XCHAR_FILE_THREAD_SAFE = 1  - 启用线程安全（每个线程独立文件句柄）
 *                                适用：多线程环境
 *                                开销：每线程一个文件句柄 + HashMap
 *
 * XCHAR_FILE_THREAD_SAFE = 0  - 禁用线程安全（单文件句柄）
 *                                适用：单线程环境或调用者自行加锁
 *                                开销：最小内存占用
 */
#ifndef XCHAR_FILE_THREAD_SAFE
#define XCHAR_FILE_THREAD_SAFE   1
#endif

/**
 * @brief 字符映射数据文件路径
 *
 * 可通过编译选项覆盖：-DXCHAR_COMPACT_PATH="path/to/file.BIN"
 */
#ifndef XCHAR_COMPACT_PATH
#define XCHAR_COMPACT_PATH       "XCHAR_COMPACT.BIN"
#endif

#endif /* XCHAR_USE_FILE_GBK */

/* ========================================================================== */
/*                        代码模式配置                                         */
/* ========================================================================== */

#if defined(XCHAR_USE_CODE_GBK)

/**
 * @brief 代码模式数据来源
 *
 * 可选择将数据编译进固件，或存放在特定Flash区域
 * XCHAR_CODE_DATA_ADDR - 如果定义，从指定地址读取（如外部Flash）
 *                         如果未定义，使用编译进固件的静态数组
 */
 //#define XCHAR_CODE_DATA_ADDR  0x08080000 

#endif /* XCHAR_USE_CODE_GBK */

/* ========================================================================== */
/*                        系统API模式配置                                      */
/* ========================================================================== */

#if defined(XCHAR_USE_SYSTEM_GBK)

/**
 * @brief 系统API模式配置
 *
 * Windows: 使用 MultiByteToWideChar/WideCharToMultiByte
 * Linux:   使用 iconv
 * macOS:   使用 CoreFoundation CFStringConvertEncodingToNSStringEncoding
 */

/* 是否启用ICU库（更强功能的Unicode转换） */
/* #define XCHAR_USE_ICU */

#endif /* XCHAR_USE_SYSTEM_GBK */

/* ========================================================================== */
/*                        自动模式检测                                         */
/* ========================================================================== */

/**
 * @brief 自动选择默认模式
 *
 * 如果用户未指定任何模式，根据平台自动选择：
 *   - Windows/Linux/macOS -> XCHAR_USE_SYSTEM_GBK
 *   - 其他平台            -> XCHAR_USE_FILE_GBK
 */
#if !defined(XCHAR_USE_CODE_GBK) && !defined(XCHAR_USE_FILE_GBK) && !defined(XCHAR_USE_SYSTEM_GBK)

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XCHAR_USE_SYSTEM_GBK
#else
#define XCHAR_USE_FILE_GBK
#endif

#endif

/* ========================================================================== */
/*                        模式验证                                             */
/* ========================================================================== */

/* 检查是否同时启用了多个模式 */
#if (defined(XCHAR_USE_CODE_GBK) && defined(XCHAR_USE_FILE_GBK)) || \
    (defined(XCHAR_USE_CODE_GBK) && defined(XCHAR_USE_SYSTEM_GBK)) || \
    (defined(XCHAR_USE_FILE_GBK) && defined(XCHAR_USE_SYSTEM_GBK))
#error "XChar: 不能同时启用多个GBK转换模式，请只选择一种模式"
#endif

#ifdef __cplusplus
}
#endif

#endif /* XCHAR_CONF_H */