#include"XDataStructConfig.h"
#ifndef XATOMIC_H
#define XATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
// 原子类型定义
#ifdef _MSC_VER
typedef struct { volatile long value; } XAtomic_bool;
#else
typedef struct { volatile bool value; } XAtomic_bool;
#endif
typedef struct { volatile int32_t value; } XAtomic_int32_t;
typedef struct { volatile uint32_t value; }  XAtomic_uint32_t;
typedef struct { volatile int64_t value; }  XAtomic_int64_t;
typedef struct { volatile uint64_t value; }  XAtomic_uint64_t;
typedef struct { volatile size_t value; }  XAtomic_size_t;
typedef struct { volatile void* value; }  XAtomic_ptr_t;
#include"XAtomic_load.h"
#include"XAtomic_store.h"
#include"XAtomic_exchange.h"
#include"XAtomic_compare.h"
#include"XAtomic_add.h"
#include"XAtomic_sub.h"
/**
* @brief 完全内存屏障，确保所有读写操作按顺序执行
*/
void  XAtomic_memory_barrier();
/**
 * @brief 获取屏障，确保后续读操作不会重排到屏障前
 */
void  XAtomic_memory_barrier_acquire();
/**
 * @brief 释放屏障，确保之前的写操作不会重排到屏障后
 */
void  XAtomic_memory_barrier_release();
/**
 * @brief 初始化原子变量为指定值
 * @param var 指向原子变量的指针
 * @param value 初始值
 */
#define XAtomic_init(var, v) do { (var).value = (v); } while(0)



#ifdef __cplusplus
}
#endif
#endif