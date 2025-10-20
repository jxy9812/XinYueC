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
typedef struct { volatile void* value; }  XAtomic_ptr;
#define XAtomic_create(type)    XMemory_calloc(1,sizeof(XAtomic_##type))
#define XAtomic_delete(p)       XMemory_free(p)

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
 * @brief 初始化原子变量为指定值
 * @param var 原子变量（非指针）
 * @param value 初始值
 * @note 此宏直接对原子变量的内部值进行赋值，非原子操作，应在多线程访问前调用
*/
#define XAtomic_init(var, v) do { (var).value = (v); } while(0)
#define XAtomic_delete			XMemory_free
#ifdef __cplusplus
}
#endif
#endif