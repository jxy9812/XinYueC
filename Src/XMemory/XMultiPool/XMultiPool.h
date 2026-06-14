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
#include "XAtomic.h"
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
    // ============ 新增字段 ============
    /** 标记是否启用了倍数模式 */
    bool is_power_of_two_mode;

    size_t  initial_size;//初始大小
    /** 在倍数模式下，期望的下一个块大小 */
    size_t next_expected_size;

    /** 在倍数模式下，增长倍数（例如2） */
    size_t growth_multiplier;
    
    // ============ 内存统计字段 ============
    /** 总用户可用内存大小（字节），初始化时计算 */
    size_t total_user_size;
    /** 剩余用户可用内存大小（字节），原子变量，线程安全 */
    XAtomic_size_t free_user_size;
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
 * @brief 启用倍数模式。
 *
 * 启用后，`XMultiPool_add_pool` 将强制执行倍数和唯一性检查。
 * 此函数应在添加任何自定义池之前调用。
 *
 * @param multi_pool 目标多级内存池。
 * @param initial_size 第一个池的大小（字节）。
 * @param multiplier 增长倍数（必须大于1）。
 * @return bool 成功启用返回 `true`，否则返回 `false`。
 */
bool XMultiPool_enable_power_of_two_mode(XMultiPool* multi_pool, size_t initial_size, size_t multiplier);

/**
 * @brief 获取由 XMultiPool 分配的内存块的原始大小。
 * @param mp 指向 XMultiPool 实例的指针。如果为 NULL，则检查全局内存池。
 * @param ptr 由 XMultiPool_malloc 或相关函数分配的指针。
 * @return 如果 ptr 有效且来自指定的内存池，则返回其用户可用大小；否则返回 0。
 */
size_t XMultiPool_getMaxUserSize(XMultiPool* mp, void* ptr);
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
 * @brief 从多级内存池中分配并清零内存
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param count 元素个数。
 * @param size 每个元素的大小（字节）。
 * @return void* 成功时返回指针，失败时返回 NULL。
 */
void* XMultiPool_calloc(XMultiPool* multi_pool, size_t count, size_t size);

/**
 * @brief 重新调整之前分配的内存块的大小
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param ptr 之前由 XMultiPool_malloc/calloc/realloc 分配的指针。
 * @param size 新的内存大小（字节）。
 * @return void* 成功时返回新指针（可能与原指针相同），失败时返回 NULL。
 *         如果 size 为 0 且 ptr 不为 NULL，则释放内存并返回 NULL。
 */
void* XMultiPool_realloc(XMultiPool* multi_pool, void* ptr, size_t size);
/**
 * @brief 将内存块归还给多级内存池
 *
 * 通过检查指针地址范围，自动判断应归还到哪个子池。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param ptr 要归还的内存块指针。
 */
void XMultiPool_free(XMultiPool* multi_pool, void* ptr);
/**
 * @brief 检查一个指针是否由该多级内存池分配
 *
 * 此函数用于验证一个 `void*` 指针是否是由 `XMultiPool_malloc` 从此特定的多级内存池分配的。
 * 它通过读取指针中嵌入的池索引，并委托给对应的子池进行验证。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @param ptr 要检查的指针。
 * @return bool 如果指针有效且属于此多级内存池，则返回 `true`；否则返回 `false`。
 */
bool XMultiPool_is_from_pool(const XMultiPool* multi_pool, const void* ptr);

/**
 * @brief 获取多级内存池中剩余可用内存大小（字节）
 *
 * 此函数是线程安全的，使用原子读取。
 * 返回值为所有子池剩余用户可用内存的总和。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @return size_t 剩余可用内存字节数。如果 multi_pool 为 NULL，返回 0。
 */
size_t XMultiPool_freeSize(XMultiPool* multi_pool);

/**
 * @brief 获取多级内存池的总用户可用内存大小（字节）
 *
 * 返回值为所有子池用户可用内存的总和。
 *
 * @param multi_pool 指向多级内存池的指针。
 * @return size_t 总用户可用内存字节数。如果 multi_pool 为 NULL，返回 0。
 */
size_t XMultiPool_totalSize(XMultiPool* multi_pool);

XMultiPool* XMultiPool_global();
//用全局池分配
void* XMultiPool_global_malloc(size_t size);
void* XMultiPool_global_calloc(size_t count, size_t size);
void* XMultiPool_global_realloc(void* ptr, size_t size);
//全局池释放
void XMultiPool_global_free(void* ptr);
#ifdef __cplusplus
}
#endif

#endif // XMULTIPOOL_H