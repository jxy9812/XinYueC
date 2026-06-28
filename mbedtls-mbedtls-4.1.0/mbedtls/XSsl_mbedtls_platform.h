/**
 * @file XSsl_mbedtls_platform.h
 * @brief mbedTLS平台适配层 - 嵌入式系统支持
 * 
 * 提供以下功能：
 * - 内存管理适配
 * - 随机数源适配
 * - 时间函数适配
 * - 线程支持适配
 * - 网络IO适配
 */

#ifndef XSSL_MBEDTLS_PLATFORM_H
#define XSSL_MBEDTLS_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 一、平台检测
 * ========================================================================= */

/* 自动检测平台 */
#if defined(STM32F4) || defined(STM32F7) || defined(STM32H7)
    #define XSSL_PLATFORM_STM32
#elif defined(ESP32)
    #define XSSL_PLATFORM_ESP32
#elif defined(__linux__)
    #define XSSL_PLATFORM_LINUX
#elif defined(_WIN32)
    #define XSSL_PLATFORM_WINDOWS
#elif defined(FREERTOS) || defined(USE_FREERTOS)
    #define XSSL_PLATFORM_FREERTOS
#elif defined(RTTHREAD) || defined(RT_USING_FINSH)
    #define XSSL_PLATFORM_RTTHREAD
#else
    #define XSSL_PLATFORM_BAREMETAL
#endif

/* =========================================================================
 * 二、内存管理适配
 * ========================================================================= */

/**
 * @brief 内存分配函数类型
 */
typedef void* (*XSSL_AllocFunc)(size_t size);
typedef void (*XSSL_FreeFunc)(void* ptr);
typedef void* (*XSSL_ReallocFunc)(void* ptr, size_t size);

/**
 * @brief 设置内存管理函数
 * @param alloc 分配函数
 * @param free 释放函数
 * @param realloc 重分配函数
 */
void XSSL_setMemoryFunctions(XSSL_AllocFunc alloc, XSSL_FreeFunc free, XSSL_ReallocFunc realloc);

/**
 * @brief 初始化内存池（可选）
 * @param buf 内存池缓冲区
 * @param len 缓冲区长度
 * @return 成功返回true
 */
bool XSSL_initMemoryPool(void* buf, size_t len);

/* =========================================================================
 * 三、随机数源适配
 * ========================================================================= */

/**
 * @brief 硬件随机数回调函数类型
 * @param buf 输出缓冲区
 * @param len 需要的字节数
 * @return 实际读取的字节数，失败返回负值
 */
typedef int (*XSSL_HardwareRngFunc)(uint8_t* buf, size_t len);

/**
 * @brief 设置硬件随机数源
 * @param rng_func 硬件随机数函数
 */
void XSSL_setHardwareRng(XSSL_HardwareRngFunc rng_func);

/**
 * @brief 检查是否有硬件随机数源
 * @return 有返回true
 */
bool XSSL_hasHardwareRng(void);

/* =========================================================================
 * 四、时间函数适配
 * ========================================================================= */

/**
 * @brief 时间戳类型
 */
typedef uint64_t XSSL_Timestamp;

/**
 * @brief 获取当前时间戳（毫秒）
 * @return 时间戳
 */
XSSL_Timestamp XSSL_getTimestamp(void);

/**
 * @brief 设置时间戳获取函数
 * @param get_ts 获取时间戳函数
 */
void XSSL_setTimestampFunc(XSSL_Timestamp (*get_ts)(void));

/**
 * @brief 获取当前时间（用于证书验证）
 * @param sec 秒
 * @param min 分钟
 * @param hour 小时
 * @param day 日
 * @param mon 月（1-12）
 * @param year 年
 * @return 成功返回true
 */
bool XSSL_getCurrentTime(int* sec, int* min, int* hour, int* day, int* mon, int* year);

/* =========================================================================
 * 五、线程支持适配
 * ========================================================================= */

#ifdef XSSL_PLATFORM_FREERTOS
    #include "FreeRTOS.h"
    #include "semphr.h"
    
    typedef SemaphoreHandle_t XSSL_Mutex;
    
    #define XSSL_MUTEX_INIT(m)    ((m) = xSemaphoreCreateMutex())
    #define XSSL_MUTEX_LOCK(m)    xSemaphoreTake((m), portMAX_DELAY)
    #define XSSL_MUTEX_UNLOCK(m)  xSemaphoreGive((m))
    #define XSSL_MUTEX_FREE(m)    vSemaphoreDelete((m))
    
#elif defined(XSSL_PLATFORM_RTTHREAD)
    #include <rtthread.h>
    
    typedef struct rt_mutex* XSSL_Mutex;
    
    #define XSSL_MUTEX_INIT(m)    ((m) = rt_mutex_create("ssl_mtx", RT_IPC_FLAG_PRIO))
    #define XSSL_MUTEX_LOCK(m)    rt_mutex_take((m), RT_WAITING_FOREVER)
    #define XSSL_MUTEX_UNLOCK(m)  rt_mutex_release((m))
    #define XSSL_MUTEX_FREE(m)    rt_mutex_delete((m))
    
#elif defined(XSSL_PLATFORM_LINUX) || defined(XSSL_PLATFORM_WINDOWS)
    /* 使用mbedTLS内置的线程支持 */
    #define XSSL_HAS_THREADING
    
#else
    /* 裸机 - 无线程支持 */
    typedef int XSSL_Mutex;
    
    #define XSSL_MUTEX_INIT(m)    ((m) = 0)
    #define XSSL_MUTEX_LOCK(m)    ((void)0)
    #define XSSL_MUTEX_UNLOCK(m)  ((void)0)
    #define XSSL_MUTEX_FREE(m)    ((void)0)
#endif

/* =========================================================================
 * 六、网络IO适配
 * ========================================================================= */

/**
 * @brief 网络上下文结构体
 */
typedef struct XSSL_NetContext {
    int fd;                     /**< 文件描述符或句柄 */
    void* user_data;            /**< 用户数据 */
    int timeout_ms;             /**< 超时时间（毫秒） */
} XSSL_NetContext;

/**
 * @brief 网络IO回调函数类型
 */
typedef int (*XSSL_NetSendFunc)(void* ctx, const uint8_t* buf, size_t len);
typedef int (*XSSL_NetRecvFunc)(void* ctx, uint8_t* buf, size_t len);
typedef int (*XSSL_NetRecvTimeoutFunc)(void* ctx, uint8_t* buf, size_t len, uint32_t timeout_ms);

/**
 * @brief 设置网络IO回调
 */
void XSSL_setNetCallbacks(XSSL_NetSendFunc send, XSSL_NetRecvFunc recv, 
                           XSSL_NetRecvTimeoutFunc recv_timeout);

/* =========================================================================
 * 七、调试输出适配
 * ========================================================================= */

/**
 * @brief 调试日志级别
 */
typedef enum XSSL_DebugLevel {
    XSSL_DEBUG_LEVEL_NONE = 0,
    XSSL_DEBUG_LEVEL_ERROR = 1,
    XSSL_DEBUG_LEVEL_WARN = 2,
    XSSL_DEBUG_LEVEL_INFO = 3,
    XSSL_DEBUG_LEVEL_DEBUG = 4
} XSSL_DebugLevel;

/**
 * @brief 调试输出回调
 */
typedef void (*XSSL_DebugOutputFunc)(XSSL_DebugLevel level, const char* file, 
                                       int line, const char* msg);

/**
 * @brief 设置调试输出回调
 * @param level 调试级别
 * @param output 输出函数
 */
void XSSL_setDebugOutput(XSSL_DebugLevel level, XSSL_DebugOutputFunc output);

/* =========================================================================
 * 八、平台初始化
 * ========================================================================= */

/**
 * @brief 初始化mbedTLS平台层
 * @return 成功返回true
 */
bool XSSL_platformInit(void);

/**
 * @brief 清理mbedTLS平台层
 */
void XSSL_platformCleanup(void);

/* =========================================================================
 * 九、mbedTLS回调函数（内部使用）
 * ========================================================================= */

/**
 * @brief mbedTLS平台初始化回调
 * 在mbedTLS编译时链接
 */
int mbedtls_platform_setup(void);
void mbedtls_platform_teardown(void);

#ifdef __cplusplus
}
#endif

#endif /* XSSL_MBEDTLS_PLATFORM_H */