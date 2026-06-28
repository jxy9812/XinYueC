/**
 * @file XSsl_mbedtls_platform.c
 * @brief mbedTLS 平台适配层实现 - 使用 XinYueC 库组件
 */

#include "XSsl_mbedtls_platform.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XRandomGenerator.h"
#include <string.h>

/* mbedTLS 头文件 */
#include <ssl.h>
#include <debug.h>

/* 条件包含 - 根据mbedTLS配置 */
#if defined(MBEDTLS_PLATFORM_C)
    #include <mbedtls/platform.h>
    #define XSSL_HAS_PLATFORM
#endif

#if defined(MBEDTLS_ENTROPY_C) && defined(MBEDTLS_CTR_DRBG_C)
    #include <mbedtls/entropy.h>
    #include <mbedtls/ctr_drbg.h>
    #define XSSL_HAS_ENTROPY
#endif

#if defined(MBEDTLS_THREADING_C)
    #include <mbedtls/threading.h>
    #define XSSL_HAS_THREADING
#endif

/* ==================== 平台配置 ==================== */

#if defined(XSSL_PLATFORM_FREERTOS)
    #include "FreeRTOS.h"
    #include "task.h"
    #include "semphr.h"
    
    #define XSSL_THREADING_TYPE     1
    typedef SemaphoreHandle_t xssl_mutex_t;
    
#elif defined(XSSL_PLATFORM_RTTHREAD)
    #include "rtthread.h"
    
    #define XSSL_THREADING_TYPE     2
    typedef rt_mutex_t xssl_mutex_t;
    
#elif defined(XSSL_PLATFORM_LINUX)
    #include <pthread.h>
    
    #define XSSL_THREADING_TYPE     3
    typedef pthread_mutex_t xssl_mutex_t;
    
#elif defined(XSSL_PLATFORM_WINDOWS)
    #include <windows.h>
    
    #define XSSL_THREADING_TYPE     4
    typedef CRITICAL_SECTION xssl_mutex_t;
    
#else
    /* 裸机或无操作系统 */
    #define XSSL_THREADING_TYPE     0
#endif

/* ==================== 内存管理（使用 XMemory） ==================== */

/**
 * @brief mbedTLS 内存分配回调
 * @note 使用 XinYueC 的内存池系统
 */
static void* xssl_mbedtls_calloc(size_t count, size_t size)
{
    /* 使用 XCalloc 自动清零内存 */
    return XCalloc_System(count, size);
}

/**
 * @brief mbedTLS 内存释放回调
 */
static void xssl_mbedtls_free(void* ptr)
{
    XFree_System(ptr);
}

/**
 * @brief 初始化 mbedTLS 内存管理
 */
int xssl_mbedtls_setup_memory(void)
{
#ifdef XSSL_HAS_PLATFORM
    /* 设置 mbedTLS 的内存管理回调 */
    mbedtls_platform_set_calloc_free(xssl_mbedtls_calloc, xssl_mbedtls_free);
#endif
    return 0;
}

/* ==================== 随机数生成（使用 XRandomGenerator） ==================== */

/**
 * @brief 硬件随机数回调（可选，由用户设置）
 */
static XSSL_HardwareRngFunc g_hardwareRngCallback = NULL;

/**
 * @brief 设置硬件随机数源
 * @param rng_func 硬件随机数回调函数
 */
void XSSL_setHardwareRng(XSSL_HardwareRngFunc rng_func)
{
    g_hardwareRngCallback = rng_func;
}

/**
 * @brief 检查是否有硬件随机数源
 * @return 有返回true
 */
bool XSSL_hasHardwareRng(void)
{
    return g_hardwareRngCallback != NULL;
}

/**
 * @brief mbedTLS 随机数回调
 * @param data 用户数据（未使用）
 * @param output 输出缓冲区
 * @param len 需要生成的字节数
 * @return 0 成功，负值失败
 */
static int xssl_mbedtls_random(void* data, unsigned char* output, size_t len)
{
    (void)data;  /* 未使用 */
    
    if (output == NULL || len == 0) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    
    /* 优先使用硬件随机数 */
    if (g_hardwareRngCallback != NULL) {
        int result = g_hardwareRngCallback(output, len);
        if (result == (int)len) {
            return 0;
        }
        /* 硬件随机数失败，回退到软件随机数 */
    }
    
    /* 使用 XRandomGenerator 的安全随机数填充 */
    if (XRandomGenerator_fillSecure(output, len)) {
        return 0;
    }
    
    /* 如果安全随机数不可用，使用全局随机数生成器 */
    XRandomGenerator* gen = XRandomGenerator_global();
    if (gen != NULL) {
        /* 按字节填充 */
        size_t filled = 0;
        while (filled < len) {
            uint32_t random = XRandomGenerator_generate(gen);
            size_t to_copy = (len - filled) < sizeof(random) ? (len - filled) : sizeof(random);
            memcpy(output + filled, &random, to_copy);
            filled += to_copy;
        }
        return 0;
    }
    
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

/**
 * @brief 初始化 mbedTLS 随机数生成
 */
int xssl_mbedtls_setup_random(void* entropy_ctx, void* ctr_drbg_ctx)
{
#ifdef XSSL_HAS_ENTROPY
    mbedtls_entropy_context* entropy = (mbedtls_entropy_context*)entropy_ctx;
    mbedtls_ctr_drbg_context* ctr_drbg = (mbedtls_ctr_drbg_context*)ctr_drbg_ctx;
    int ret;
    
    /* 初始化熵源 */
    mbedtls_entropy_init(entropy);
    
    /* 添加自定义随机数源 */
    ret = mbedtls_entropy_add_source(entropy, xssl_mbedtls_random, NULL,
                                      MBEDTLS_ENTROPY_MIN_PLATFORM,
                                      MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (ret != 0) {
        return ret;
    }
    
    /* 初始化 CTR_DRBG */
    mbedtls_ctr_drbg_init(ctr_drbg);
    
    /* 使用熵源初始化 DRBG */
    ret = mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy, NULL, 0);
    if (ret != 0) {
        return ret;
    }
    
    return 0;
#else
    (void)entropy_ctx; (void)ctr_drbg_ctx;
    /* 没有熵源模块，返回成功（使用硬件随机数或外部随机源） */
    return 0;
#endif
}

/* ==================== 时间函数（使用 XDateTime） ==================== */

/**
 * @brief mbedTLS 时间回调
 * @return 当前时间戳（秒）
 */
static mbedtls_time_t xssl_mbedtls_time(mbedtls_time_t* time)
{
    /* 使用 XDateTime 获取当前秒时间戳 */
    int64_t secs = XDateTime_currentSecsSinceEpoch();
    
    if (time != NULL) {
        *time = (mbedtls_time_t)secs;
    }
    
    return (mbedtls_time_t)secs;
}

/**
 * @brief 获取当前时间（毫秒）
 * @return 当前时间戳（毫秒）
 */
int64_t XSSL_getTimeMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 初始化 mbedTLS 时间函数
 */
int xssl_mbedtls_setup_time(void)
{
#ifdef XSSL_HAS_PLATFORM
    /* 设置 mbedTLS 的时间回调 */
    mbedtls_platform_set_time(xssl_mbedtls_time);
#endif
    return 0;
}

/* ==================== 线程支持 ==================== */

#if XSSL_THREADING_TYPE > 0

/**
 * @brief 互斥锁结构
 */
typedef struct {
    xssl_mutex_t mutex;
} xssl_mbedtls_mutex_t;

/**
 * @brief 初始化互斥锁
 */
static int xssl_mutex_init(mbedtls_threading_mutex_t* mutex)
{
    if (mutex == NULL) {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    
    xssl_mbedtls_mutex_t* m = (xssl_mbedtls_mutex_t*)XMalloc_System(sizeof(xssl_mbedtls_mutex_t));
    if (m == NULL) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#if XSSL_THREADING_TYPE == 1  /* FreeRTOS */
    m->mutex = xSemaphoreCreateMutex();
    if (m->mutex == NULL) {
        XFree_System(m);
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 2  /* RT-Thread */
    m->mutex = rt_mutex_create("ssl_mtx", RT_IPC_FLAG_PRIO);
    if (m->mutex == RT_NULL) {
        XFree_System(m);
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 3  /* Linux */
    if (pthread_mutex_init(&m->mutex, NULL) != 0) {
        XFree_System(m);
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 4  /* Windows */
    InitializeCriticalSection(&m->mutex);
#endif
    
    mutex->mutex = m;
    return 0;
}

/**
 * @brief 获取互斥锁
 */
static int xssl_mutex_lock(mbedtls_threading_mutex_t* mutex)
{
    if (mutex == NULL || mutex->mutex == NULL) {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    
    xssl_mbedtls_mutex_t* m = (xssl_mbedtls_mutex_t*)mutex->mutex;
    
#if XSSL_THREADING_TYPE == 1  /* FreeRTOS */
    if (xSemaphoreTake(m->mutex, portMAX_DELAY) != pdTRUE) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 2  /* RT-Thread */
    if (rt_mutex_take(m->mutex, RT_WAITING_FOREVER) != RT_EOK) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 3  /* Linux */
    if (pthread_mutex_lock(&m->mutex) != 0) {
        return MBEDTLS_ERR_THREADING_MUTEX_ERROR;
    }
    
#elif XSSL_THREADING_TYPE == 4  /* Windows */
    EnterCriticalSection(&m->mutex);
#endif
    
    return 0;
}

/**
 * @brief 释放互斥锁
 */
static int xssl_mutex_unlock(mbedtls_threading_mutex_t* mutex)
{
    if (mutex == NULL || mutex->mutex == NULL) {
        return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    }
    
    xssl_mbedtls_mutex_t* m = (xssl_mbedtls_mutex_t*)mutex->mutex;
    
#if XSSL_THREADING_TYPE == 1  /* FreeRTOS */
    xSemaphoreGive(m->mutex);
    
#elif XSSL_THREADING_TYPE == 2  /* RT-Thread */
    rt_mutex_release(m->mutex);
    
#elif XSSL_THREADING_TYPE == 3  /* Linux */
    pthread_mutex_unlock(&m->mutex);
    
#elif XSSL_THREADING_TYPE == 4  /* Windows */
    LeaveCriticalSection(&m->mutex);
#endif
    
    return 0;
}

/**
 * @brief 销毁互斥锁
 */
static void xssl_mutex_free(mbedtls_threading_mutex_t* mutex)
{
    if (mutex == NULL || mutex->mutex == NULL) {
        return;
    }
    
    xssl_mbedtls_mutex_t* m = (xssl_mbedtls_mutex_t*)mutex->mutex;
    
#if XSSL_THREADING_TYPE == 1  /* FreeRTOS */
    vSemaphoreDelete(m->mutex);
    
#elif XSSL_THREADING_TYPE == 2  /* RT-Thread */
    rt_mutex_delete(m->mutex);
    
#elif XSSL_THREADING_TYPE == 3  /* Linux */
    pthread_mutex_destroy(&m->mutex);
    
#elif XSSL_THREADING_TYPE == 4  /* Windows */
    DeleteCriticalSection(&m->mutex);
#endif
    
    XFree_System(m);
    mutex->mutex = NULL;
}

/**
 * @brief 初始化线程支持
 */
int xssl_mbedtls_setup_threading(void)
{
#ifdef XSSL_HAS_THREADING
    mbedtls_threading_set_alt(xssl_mutex_init, xssl_mutex_free,
                               xssl_mutex_lock, xssl_mutex_unlock);
#endif
    return 0;
}

/**
 * @brief 清理线程支持
 */
void xssl_mbedtls_cleanup_threading(void)
{
#ifdef XSSL_HAS_THREADING
    mbedtls_threading_free_alt();
#endif
}

#else  /* XSSL_THREADING_TYPE == 0 */

/* 无操作系统，不需要线程支持 */
int xssl_mbedtls_setup_threading(void)
{
    return 0;
}

void xssl_mbedtls_cleanup_threading(void)
{
    /* 无操作 */
}

#endif  /* XSSL_THREADING_TYPE */

/* ==================== 调试输出 ==================== */

#ifdef XSSL_MBEDTLS_DEBUG

/**
 * @brief 调试输出回调
 */
static XSSL_DebugCallback g_debugCallback = NULL;
static XSSL_DebugLevel g_debugLevel = XSSL_DEBUG_LEVEL_INFO;

/**
 * @brief 设置调试输出
 * @param level 调试级别
 * @param callback 调试回调函数
 */
void XSSL_setDebugOutput(XSSL_DebugLevel level, XSSL_DebugCallback callback)
{
    g_debugLevel = level;
    g_debugCallback = callback;
}

/**
 * @brief mbedTLS 调试回调
 * @param ctx 上下文
 * @param level 调试级别
 * @param file 文件名
 * @param line 行号
 * @param str 消息
 */
static void xssl_mbedtls_debug(void* ctx, int level, const char* file, int line, const char* str)
{
    (void)ctx;
    
    if (g_debugCallback == NULL) {
        return;
    }
    
    /* 转换调试级别 */
    XSSL_DebugLevel xssl_level;
    switch (level) {
        case MBEDTLS_DEBUG_LEVEL_ERROR:
            xssl_level = XSSL_DEBUG_LEVEL_ERROR;
            break;
        case MBEDTLS_DEBUG_LEVEL_WARNING:
            xssl_level = XSSL_DEBUG_LEVEL_WARNING;
            break;
        case MBEDTLS_DEBUG_LEVEL_INFO:
            xssl_level = XSSL_DEBUG_LEVEL_INFO;
            break;
        case MBEDTLS_DEBUG_LEVEL_DEBUG:
        default:
            xssl_level = XSSL_DEBUG_LEVEL_DEBUG;
            break;
    }
    
    /* 过滤低于当前级别的消息 */
    if (xssl_level > g_debugLevel) {
        return;
    }
    
    g_debugCallback(xssl_level, file, line, str);
}

/**
 * @brief 设置 SSL 配置的调试回调
 */
void xssl_mbedtls_set_debug(mbedtls_ssl_config* conf, int level)
{
    mbedtls_ssl_conf_dbg(conf, xssl_mbedtls_debug, NULL);
    mbedtls_debug_set_threshold(level);
}

#endif  /* XSSL_MBEDTLS_DEBUG */

/* ==================== 平台初始化/清理 ==================== */

/**
 * @brief 平台初始化状态
 */
static bool g_platformInitialized = false;

/**
 * @brief 初始化 mbedTLS 平台
 * @return 0 成功，负值失败
 */
int XSSL_platformInit(void)
{
    if (g_platformInitialized) {
        return 0;
    }
    
    int ret;
    
    /* 1. 初始化内存管理 */
    ret = xssl_mbedtls_setup_memory();
    if (ret != 0) {
        return ret;
    }
    
    /* 2. 初始化时间函数 */
    ret = xssl_mbedtls_setup_time();
    if (ret != 0) {
        return ret;
    }
    
    /* 3. 初始化线程支持 */
    ret = xssl_mbedtls_setup_threading();
    if (ret != 0) {
        return ret;
    }
    
    g_platformInitialized = true;
    return 0;
}

/**
 * @brief 清理 mbedTLS 平台
 */
void XSSL_platformCleanup(void)
{
    if (!g_platformInitialized) {
        return;
    }
    
    /* 清理线程支持 */
    xssl_mbedtls_cleanup_threading();
    
    g_platformInitialized = false;
}

/* ==================== 内存池支持（可选） ==================== */

#ifdef XSSL_USE_MEMORY_POOL

static uint8_t* g_sslMemoryPool = NULL;
static size_t g_sslMemoryPoolSize = 0;
static size_t g_sslMemoryPoolUsed = 0;

/**
 * @brief 初始化 SSL 内存池
 * @param pool 内存池缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int XSSL_initMemoryPool(void* pool, size_t size)
{
    if (pool == NULL || size == 0) {
        return -1;
    }
    
    g_sslMemoryPool = (uint8_t*)pool;
    g_sslMemoryPoolSize = size;
    g_sslMemoryPoolUsed = 0;
    
    return 0;
}

/**
 * @brief 从内存池分配内存
 * @param size 请求大小
 * @return 内存指针，失败返回 NULL
 */
void* XSSL_poolAlloc(size_t size)
{
    if (g_sslMemoryPool == NULL) {
        return NULL;
    }
    
    /* 对齐到 8 字节 */
    size = (size + 7) & ~7;
    
    if (g_sslMemoryPoolUsed + size > g_sslMemoryPoolSize) {
        return NULL;
    }
    
    void* ptr = g_sslMemoryPool + g_sslMemoryPoolUsed;
    g_sslMemoryPoolUsed += size;
    
    return ptr;
}

/**
 * @brief 重置内存池
 */
void XSSL_resetMemoryPool(void)
{
    g_sslMemoryPoolUsed = 0;
}

/**
 * @brief 获取内存池使用情况
 * @param used 已使用字节数（输出）
 * @param total 总字节数（输出）
 */
void XSSL_getMemoryPoolUsage(size_t* used, size_t* total)
{
    if (used) *used = g_sslMemoryPoolUsed;
    if (total) *total = g_sslMemoryPoolSize;
}

#endif  /* XSSL_USE_MEMORY_POOL */

/* ==================== 硬件加速支持 ==================== */

#ifdef XSSL_MBEDTLS_HW_ACCEL

#if defined(XSSL_PLATFORM_STM32)
    #include "stm32f4xx_hal.h"
    
    /**
     * @brief STM32 硬件随机数
     */
    static int stm32_hardware_rng(uint8_t* output, size_t len)
    {
        /* 用户需要初始化 RNG 外设 */
        extern RNG_HandleTypeDef hrng;
        
        for (size_t i = 0; i < len; i += 4) {
            uint32_t random;
            if (HAL_RNG_GenerateRandomNumber(&hrng, &random) != HAL_OK) {
                return -1;
            }
            size_t to_copy = (len - i) < 4 ? (len - i) : 4;
            memcpy(output + i, &random, to_copy);
        }
        
        return (int)len;
    }
    
    /**
     * @brief 初始化 STM32 硬件加速
     */
    int XSSL_initHardwareAccel_stm32(void)
    {
        /* 设置硬件随机数源 */
        XSSL_setHardwareRng(stm32_hardware_rng);
        
        /* 可以在这里添加 AES/SHA 硬件加速初始化 */
        
        return 0;
    }
    
#elif defined(XSSL_PLATFORM_ESP32)
    #include "esp_random.h"
    
    /**
     * @brief ESP32 硬件随机数
     */
    static int esp32_hardware_rng(uint8_t* output, size_t len)
    {
        esp_fill_random(output, len);
        return (int)len;
    }
    
    /**
     * @brief 初始化 ESP32 硬件加速
     */
    int XSSL_initHardwareAccel_esp32(void)
    {
        XSSL_setHardwareRng(esp32_hardware_rng);
        return 0;
    }
    
#endif  /* Platform specific */

#endif  /* XSSL_MBEDTLS_HW_ACCEL */