#include"CXinYueConfig.h"
#ifndef XATOMIC_H
#define XATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
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
typedef struct { volatile long value; } XAtomic_bool;
#else
typedef struct { volatile bool value; } XAtomic_bool;
#endif

/**
 * @brief 原子32位有符号整数类型定义
 * @note 内部包含一个volatile修饰的32位有符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_int32_t { volatile int32_t value; } XAtomic_int32_t;

/**
 * @brief 原子32位无符号整数类型定义
 * @note 内部包含一个volatile修饰的32位无符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_uint32_t { volatile uint32_t value; }  XAtomic_uint32_t;

/**
 * @brief 原子64位有符号整数类型定义
 * @note 内部包含一个volatile修饰的64位有符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_int64_t { volatile int64_t value; }  XAtomic_int64_t;

/**
 * @brief 原子64位无符号整数类型定义
 * @note 内部包含一个volatile修饰的64位无符号整数，确保多线程环境下的可见性
 */
typedef struct XAtomic_uint64_t { volatile uint64_t value; }  XAtomic_uint64_t;

/**
 * @brief 原子size_t类型定义
 * @note 内部包含一个volatile修饰的size_t类型值，确保多线程环境下的可见性
 */
typedef struct XAtomic_size_t { volatile size_t value; }  XAtomic_size_t;

/**
 * @brief 原子指针类型定义
 * @note 内部包含一个volatile修饰的void*指针，确保多线程环境下的可见性
 */
typedef struct { volatile void* value; }  XAtomic_uintptr_t;
//创建原子变量
#define XAtomic_create(type)    XCalloc(1,sizeof(XAtomic_##type))
/**
 * @brief 初始化原子变量为指定值
 * @param var 原子变量（非指针）
 * @param value 初始值
 * @note 此宏直接对原子变量的内部值进行赋值，非原子操作，应在多线程访问前调用
*/
#define XAtomic_init(var, v) do { (var).value = (v); } while(0)
#define XAtomic_delete			XFree

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


#define XAtomic_index_mask(index_bits)      ((((size_t)1) << index_bits) - 1)
#define XAtomic_version_mask(index_bits)    ((((size_t)1) << XAtomic_version_bits(index_bits)) - 1)
#define XAtomic_version_bits(index_bits)    (sizeof(size_t) * 8 - index_bits)
// 计算能容纳 [0, max_value] 所需的最少位数-获取 index_bits
size_t XAtomic_index_bits(size_t max_value);
// 打包索引和版本号
size_t XAtomic_pack_index_version(size_t index, size_t version, size_t index_bits, uintptr_t version_mask);
// 从打包值中解包出索引
size_t XAtomic_unpack_index(size_t packed, size_t index_mask);
// 从打包值中解包出版本号
size_t XAtomic_unpack_version(size_t packed, size_t index_bits, size_t version_mask);
#ifdef __cplusplus
}
#endif
#endif