/**
 * @file XMultiPool.h
 * @brief 基于 XVector 的多级固定大小内存池 
 *
 * 此内存池使用 XVector 动态管理多个 XFixedPool，并在添加子池时自动排序。
 * 分配内存时使用二分查找，提供 O(log N) 的查找性能。
 */

#ifndef XMULTIPOOL_H
#define XMULTIPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "XFixedPool.h"
#include "XVector.h"
//多级内存池
typedef struct XMultiPool //因为线程安全可以全局创建一个后公用
{
    /** 存储 XFixedPool* 指针的动态数组，按 user_block_size 升序排列 */
    XVector* sub_pools;

    /**
     * @brief 所有权标记
     *
     * - `true`: 内存池拥有所有子池的生命周期，
     *           在 `XMultiPool_delete` 中会调用 `XFixedPool_delete`。
     * - `false`: 内存池不拥有子池，子池由用户通过 `XFixedPool_init`
     *           初始化。在 `XMultiPool_deinit` 中只会调用 `XFixedPool_deinit`。
     */
    bool owns_memory;
} XMultiPool;

// ============================================================================
// API 函数声明
// ============================================================================

/**
 * @brief （动态模式）创建一个空的多级内存池
 *
 * 创建后需要通过 `XMultiPool_add_pool` 来添加具体的子池。
 *
 * @return XMultiPool* 成功时返回指针，失败时返回 NULL。
 */
XMultiPool* XMultiPool_create(void);

/**
 * @brief （动态模式）销毁多级内存池(一般用不到)
 *
 * 如果 `pool->owns_memory` 为 true，则会销毁所有子池。
 *
 * @param multi_pool 要销毁的多级内存池指针。
 */
void XMultiPool_delete(XMultiPool* multi_pool);

/**
 * @brief （静态/栈模式）初始化一个多级内存池实例
 *
 * 用户需要提供一个 `XMultiPool` 结构体实例。
 * 此模式下，`owns_memory` 被强制设为 `false`。
 *
 * @param multi_pool 指向待初始化的 `XMultiPool` 的指针。
 * @return bool 初始化成功返回 `true`，否则返回 `false`。
 */
bool XMultiPool_init(XMultiPool* multi_pool);

/**
 * @brief （静态/栈模式）反初始化一个多级内存池实例(一般用不到)
 *
 * @param multi_pool 指向要反初始化的内存池的指针。
 */
void XMultiPool_deinit(XMultiPool* multi_pool);

/**
 * @brief 向多级内存池中添加一个新的子池（动态模式）
 *
 * 此函数会接管 `pool` 的所有权。`pool` 必须是通过 `XFixedPool_create`
 * 或 `XFixedPool_create_from_memory` 创建的。
 * 添加后，内部的 `sub_pools` 向量会保持按 `user_block_size` 升序排列。
 *
 * @param multi_pool 目标多级内存池。
 * @param pool 要添加的子池指针。
 * @return bool 添加成功返回 `true`，否则返回 `false`。
 */
bool XMultiPool_add_pool(XMultiPool* multi_pool, XFixedPool* pool);

/**
 * @brief 从多级内存池中分配内存
 *
 * 自动选择能满足 `size` 需求的最小块大小的子池进行分配。
 * 使用二分查找，时间复杂度为 O(log N)。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param size 请求的内存大小（字节）。
 * @return void* 成功时返回指针，失败（所有池都空或无合适池）时返回 NULL。
 */
void* XMultiPool_malloc(XMultiPool* multi_pool, size_t size);

/**
 * @brief 将内存块归还给多级内存池
 *
 * 通过检查指针地址范围，自动判断应归还到哪个子池。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param ptr 要归还的内存块指针。
 */
void XMultiPool_free(XMultiPool* multi_pool, void* ptr);

XMultiPool* XMultiPool_global();
//初始化默认的全局多级池
void XMultiPool_initGlobal();
//用全局池分配
void* XMultiPool_mallocGlobal(size_t size);
//全局池释放
void XMultiPool_freeGlobal(void* ptr);
#ifdef __cplusplus
}
#endif

#endif // XMULTIPOOL_H