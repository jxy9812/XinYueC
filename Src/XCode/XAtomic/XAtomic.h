/**
 * @file XAtomic.h
 * @brief 原子操作基础类型与内存序公开 API。
 * @details 定义原子内存序枚举、各宽度原子类型、线程局部存储限定符和屏障
 *          声明，内存序语义与 C11/C++11 标准对应。实现只依赖 XinYueC 抽象
 *          层，禁止调用 Win32、POSIX、Qt 或其他平台 API；平台相关原子指令
 *          由 Drive/ 平台适配层实现。
 */
#ifndef XATOMIC_H
#define XATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include <stdint.h>
#include <stdbool.h>
 /**
 * @brief 原子操作内存序类型
 * @note 这些值对应于 C11/C++11 标准中的内存序语义
 */
typedef enum {
    /**
     * @brief 宽松内存序
     * @note 只保证原子性，不提供任何同步或顺序保证。性能最高。
     */
    XAtomic_MemoryOrder_Relaxed,

    /**
     * @brief 消费内存序 (Consumes)
     * @note 用于依赖于从另一个线程获取的数据的读取操作。
     * @note 注意：MSVC 不直接支持此内存序，在 MSVC 实现中会退化为 Acquire。
     */
    XAtomic_MemoryOrder_Consume,

    /**
     * @brief 获取内存序
     * @note 保证此操作之后的所有读写不会被重排到此操作之前。
     * @note 通常用于临界区的入口（锁获取）。
     */
    XAtomic_MemoryOrder_Acquire,

    /**
     * @brief 释放内存序
     * @note 保证此操作之前的所有读写不会被重排到此操作之后。
     * @note 通常用于临界区的出口（锁释放）。
     */
    XAtomic_MemoryOrder_Release,

    /**
     * @brief 获取-释放内存序
     * @note 对于读-修改-写操作（如 exchange, CAS, fetch_add），
     *       同时具有 Acquire 和 Release 语义。
     */
    XAtomic_MemoryOrder_AcqRel,

    /**
     * @brief 顺序一致性内存序
     * @note 最强的内存序，保证所有线程看到的操作全局顺序一致。
     * @note 性能开销最大，但行为最直观。这是您现有 API 的默认行为。
     */
    XAtomic_MemoryOrder_SeqCst
} XAtomic_MemoryOrder;
/**
 * @brief 原子布尔类型定义
 * @note 内部包含一个volatile修饰的布尔值，确保多线程环境下的可见性
 */
#ifdef _MSC_VER
typedef struct { volatile long m_value; } XAtomic_Bool;
#else
typedef struct { volatile bool m_value; } XAtomic_Bool;
#endif

/**
 * @brief 原子32位有符号整数类型定义
 * @note 内部包含一个volatile修饰的32位有符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_Int32 { volatile int32_t m_value; } XAtomic_Int32;

/**
 * @brief 原子32位无符号整数类型定义
 * @note 内部包含一个volatile修饰的32位无符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_Uint32 { volatile uint32_t m_value; }  XAtomic_Uint32;

/**
 * @brief 原子64位有符号整数类型定义
 * @note 内部包含一个volatile修饰的64位有符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_Int64 { volatile int64_t m_value; }  XAtomic_Int64;

/**
 * @brief 原子64位无符号整数类型定义
 * @note 内部包含一个volatile修饰的64位无符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_Uint64 { volatile uint64_t m_value; }  XAtomic_Uint64;

/**
 * @brief 原子size_t类型定义
 * @note 内部包含一个volatile修饰的size_t类型值，确保多线程环境下的可见性
 */
typedef struct XAtomic_SizeT { volatile size_t m_value; }  XAtomic_SizeT;

/**
 * @brief 原子指针类型定义
 * @note 内部包含一个volatile修饰的void*指针，确保多线程环境下的可见性
 */
typedef struct { volatile void* m_value; }  XAtomic_Uintptr;

/*
 * @brief 原子模块统一提供线程局部存储限定符。
 * @details
 * 平台线程局部存储语法属于编译器/运行时实现细节，不能散落到无锁容器、
 * 算法等上层模块中。各平台驱动只需要包含 XAtomic.h，即可通过这个限定符
 * 声明真正的线程私有状态；上层代码不需要判断 MSVC、GCC 或 Clang。
 */
#if defined(_MSC_VER)
#define XATOMIC_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define XATOMIC_THREAD_LOCAL __thread
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define XATOMIC_THREAD_LOCAL _Thread_local
#else
/* 保留对旧式嵌入式编译器的语法兼容；使用锁自由容器时仍需提供 TLS。 */
#define XATOMIC_THREAD_LOCAL _Thread_local
#endif

/**
 * @brief 创建原子变量
 * @param type 原子类型段名（如 Bool、Int32），与 XAtomic_ 前缀拼接成完整原子类型
 * @return 指向新分配并清零的原子变量的指针；分配失败返回 NULL。
 * @note 使用系统默认分配器 XCalloc_System 分配，需与 XAtomic_delete 配对释放
 */
#define XAtomic_create(type)    XCalloc_System(1,sizeof(XAtomic_##type))
/**
 * @brief 初始化原子变量为指定值
 * @param var 原子变量（非指针）
 * @param value 初始值
 * @note 此宏直接对原子变量的内部值进行赋值，非原子操作，应在多线程访问前调用
*/
#define XAtomic_init(var, v) do { (var).m_value = (v); } while(0)
/**
 * @brief 删除原子变量，释放其内存
 * @note 等价于 XFree_System，与 XAtomic_create 配对使用
 */
#define XAtomic_delete			XFree_System

#include"XAtomic_load.h"
#include"XAtomic_store.h"
#include"XAtomic_exchange.h"
#include"XAtomic_compare.h"
#include"XAtomic_add.h"
#include"XAtomic_sub.h"

/**
 * @brief 完全内存屏障，确保所有读写操作按顺序执行
 * @note 阻止编译器和CPU对屏障前后的内存操作进行重排序
 */
void  XAtomic_memory_barrier();

/**
 * @brief 获取屏障，确保后续读操作不会重排到屏障前
 * @note 用于保证在屏障之后的读取操作能看到屏障之前的写入操作结果
 */
void  XAtomic_memory_barrier_acquire();

/**
 * @brief 释放屏障，确保之前的写操作不会重排到屏障后
 * @note 用于保证在屏障之前的写入操作能被其他线程的获取屏障之后的读取操作看到
 */
void  XAtomic_memory_barrier_release();


/**
 * @brief 计算索引字段位掩码
 * @param index_bits 索引占用的位数
 * @return 低 index_bits 位全为 1 的掩码
 */
#define XAtomic_index_mask(index_bits)      ((((size_t)1) << index_bits) - 1)
/**
 * @brief 计算版本号字段占用的位数
 * @param index_bits 索引占用的位数
 * @return size_t 总位数减去索引位数，即版本号可用位数
 */
#define XAtomic_version_bits(index_bits)    (sizeof(size_t) * 8 - index_bits)
/**
 * @brief 计算版本号字段位掩码
 * @param index_bits 索引占用的位数
 * @return 版本号字段对应的全 1 掩码
 */
#define XAtomic_version_mask(index_bits)    ((((size_t)1) << XAtomic_version_bits(index_bits)) - 1)
// 计算能容纳 [0, max_value] 所需的最少位数-获取 index_bits
size_t XAtomic_index_bits(size_t max_value);
// 打包索引和版本号
size_t XAtomic_pack_index_version(size_t index, size_t version, size_t index_bits, uintptr_t version_mask);
// 从打包值中解包出索引
size_t XAtomic_unpack_index(size_t packed, size_t index_mask);
// 从打包值中解包出版本号
size_t XAtomic_unpack_version(size_t packed, size_t index_bits, size_t version_mask);

/**
 * @brief 目标平台缓存行大小（字节）
 * @note 可在包含本头文件前预定义，以适配实际硬件的缓存行大小
 */
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

/**
 * @brief 按指定字节数对齐类型或变量
 * @param n 对齐字节数
 * @note 按编译器选择对齐语法：GCC/Clang 用 __attribute__，MSVC 用 __declspec，其余用 C11 _Alignas
 */
#if defined(__GNUC__) || defined(__clang__)
#define XALIGNAS(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define XALIGNAS(n) __declspec(align(n))
#else
#define XALIGNAS(n) _Alignas(n)
#endif

/**
 * @brief 按缓存行大小对齐的便捷限定符
 * @note 等价于 XALIGNAS(CACHE_LINE_SIZE)，用于避免多核伪共享
 */
#define XCACHE_ALIGN  XALIGNAS(CACHE_LINE_SIZE)

/**
 * @brief 指针大小对齐值（字节）。
 * @details MSVC 的 __declspec(align(n)) 仅接受字面整数常量，不支持 sizeof 表达式，
 *          因此预定义为常量 8 (64 位) 或 4 (32 位)；其他编译器直接使用 sizeof(void*)。
 */
#if defined(_MSC_VER)
  #if defined(_WIN64)
    #define XALIGN_PTR_SIZE 8
  #else
    #define XALIGN_PTR_SIZE 4
  #endif
#else
  #define XALIGN_PTR_SIZE sizeof(void*)
#endif

#ifdef __cplusplus
}
#endif
#endif
