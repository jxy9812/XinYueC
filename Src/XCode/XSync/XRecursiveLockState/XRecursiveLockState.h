// XRecursiveLockState.h

#ifndef XRECURSIVELOCKSTATE_H
#define XRECURSIVELOCKSTATE_H
#include "XSync_config.h"
#if XSYNC_ON
#if XRECURSIVELOCKSTATE_ON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// 前向声明 XHashMap
typedef struct XHashMap XHashMap;

/**
 * @brief XRecursiveLockState - 通用的递归锁状态
 * 此结构体用于记录单个线程对某个特定锁对象的持有情况。
 */
typedef struct 
{
    union {
        size_t lock_count; // 锁被当前线程持有的总次数 (对于互斥锁，这等同于递归深度；对于读写锁，这可以是读或写计数)
        size_t reader_count;
    };
    union {
        size_t writer_count; // 当前线程持有的写锁计数 (0 or >=1)
        size_t flags;    // 预留标志位，例如区分读/写 (XReadWriteLock 可能需要)
    };
} XRecursiveLockState;

/**
 * @brief 初始化全局递归状态管理器
 * 此函数是线程安全的，可以被多次调用。
 */
void XRecursiveLockState_init();

/**
 * @brief 获取指定锁对象在当前线程中的递归状态
 * 如果状态不存在，则会自动创建一个初始状态 {lock_count: 0, flags: 0}。
 *
 * @param lock_obj 指向锁对象的指针 (如 XMutex* 或 XReadWriteLock*)
 * @return 指向当前线程对此锁的递归状态的指针
 */
XRecursiveLockState* XRecursiveLockState_get(void* lock_obj);

/**
 * @brief 清除指定锁对象的所有线程的递归状态
 * 通常在销毁锁对象时调用。
 *
 * @param lock_obj 指向锁对象的指针
 */
void XRecursiveLockState_clear(void* lock_obj);

#ifdef __cplusplus
}
#endif

#endif // XRECURSIVELOCKSTATE_ON
#endif /* XSYNC_ON */
#endif // XRECURSIVELOCKSTATE_H