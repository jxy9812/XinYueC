#ifndef XFIXEDPOOL_H
#define XFIXEDPOOL_H
/**
 * @file XFixedPool.h
 * @brief 固定大小内存池（对齐、多模式）
 *
 * 此内存池提供 O(1) 时间复杂度的分配和释放操作，并且是线程安全的。
 * 它支持两种使用模式以适应不同场景：
 * - **动态模式**: 库内部负责分配内存池结构体和数据缓冲区。
 * - **静态/栈模式**: 用户在栈或全局区提供内存池结构体和数据缓冲区，库只负责初始化逻辑。
 *
 * 主要特性:
 * - 可配置的内存块对齐，优化 CPU 缓存性能并避免 false sharing。
 * - 零外部依赖（除了XAtomic 原子库）。
 */
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XAtomic.h" 
#include "XSpinLock.h"
typedef struct XFixedPool 
{
    XAtomic_ptr free_list_head_packed; //打包头指针
    size_t block_size;          // 对齐后的块大小
    size_t user_block_size;     // 用户视角的块大小
    void* raw_memory;           // 指向数据缓冲区的指针
    size_t total_raw_size;      // 数据缓冲区的总有效字节数
    size_t num_blocks; // 新增：总块数，用于判断空和计算索引
    // --- 无锁辅助字段 ---
    size_t index_bits;          // 用于存储索引的位数
    uintptr_t index_mask;       // 索引掩码
    uintptr_t version_mask;     // 版本号掩码
    bool owns_memory;  //所有权标记
} XFixedPool;
/**
 * @brief （动态模式）创建一个新的固定大小内存池
 *
 * 此函数会在堆上分配 `XFixedPool` 结构体和所需的数据缓冲区。
 * 使用完毕后，必须调用 `XFixedPool_delete` 来释放所有资源。
 *
 * @param block_size 用户期望的每个内存块的大小（字节）。
 * @param num_blocks 池中预分配的块数量。
 * @return XFixedPool* 成功时返回指向新内存池的指针，失败时返回 NULL。
 */
XFixedPool* XFixedPool_create(size_t block_size, size_t num_blocks);

/**
 * @brief （动态模式）从一个已有的内存数组创建内存池
 *
 * 此函数会在堆上分配 `XFixedPool` 结构体，但使用用户提供的 `memory` 作为数据缓冲区。
 * 销毁时 (`XFixedPool_delete`) 会释放结构体，但不会动 `memory`。
 *
 * @param memory 用户提供的、足够大的连续内存块。
 * @param total_bytes `memory` 的总大小（字节）。
 * @param block_size 用户期望的每个内存块的大小（字节）。
 * @return XFixedPool* 成功时返回指向新内存池的指针，失败时返回 NULL。
 */
XFixedPool* XFixedPool_create_from_memory(void* memory, size_t total_bytes, size_t block_size);

/**
 * @brief （动态模式）销毁内存池
 *
 * 如果 `pool->owns_memory` 为 true，则会释放 `pool->raw_memory` 和 `pool` 本身。
 * 如果为 false，则只释放 `pool` 结构体。
 *
 * @param pool 要销毁的内存池指针。
 */
void XFixedPool_delete(XFixedPool* pool);

/**
 * @brief （静态/栈模式）初始化一个内存池实例
 *
 * 此函数不进行任何动态内存分配。用户必须自行提供：
 * 1. 一个 `XFixedPool` 结构体实例（在栈、全局区或自定义内存中）。
 * 2. 一个足够大的、连续的内存缓冲区 `memory`。
 *
 * @param pool 指向待初始化的 `XFixedPool` 结构体的指针。
 * @param memory 指向用户提供的内存缓冲区的指针。
 * @param total_bytes `memory` 缓冲区的总大小（字节）。
 * @param block_size 用户期望的每个内存块的大小（字节）。
 * @return bool 初始化成功返回 `true`，失败（如参数无效）返回 `false`。
 */
bool XFixedPool_init(XFixedPool* pool, void* memory, size_t total_bytes, size_t block_size);

/**
 * @brief （静态/栈模式）反初始化一个内存池实例
 *
 * 此函数通常不是必须的，因为它不释放任何内存。
 * 它只是将结构体成员清零，作为一种良好的编程习惯。
 * 仅当 `pool` 是通过 `XFixedPool_init` 初始化时才应调用。
 *
 * @param pool 指向要反初始化的内存池的指针。
 */
void XFixedPool_deinit(XFixedPool* pool);

/**
 * @brief 从内存池中分配一个内存块 (O(1), 无锁)
 *
 * 此函数在两种模式下均可使用。
 *
 * @param pool 指向内存池的指针。
 * @return void* 成功时返回指向内存块的指针，失败（池空）时返回 NULL。
 */
void* XFixedPool_malloc(XFixedPool* pool);

/**
 * @brief 将内存块归还给内存池 (O(1), 无锁)
 *
 * 此函数在两种模式下均可使用。
 *
 * @param pool 指向内存池的指针。
 * @param block 要归还的内存块指针。
 */
void XFixedPool_free(XFixedPool* pool, void* block);

/**
 * @brief 检查一个指针是否由该内存池分配
 *
 * 此函数用于验证一个 `void*` 指针是否是由 `XFixedPool_malloc` 从此特定内存池分配的。
 * 它通过检查指针的地址范围和对齐方式来实现。
 *
 * @param pool 指向内存池的指针。
 * @param ptr 要检查的指针。
 * @return bool 如果指针有效且属于此内存池，则返回 `true`；否则返回 `false`。
 */
bool XFixedPool_is_from_pool(const XFixedPool* pool, const void* ptr);
#ifdef __cplusplus
}
#endif

#endif // XFIXEDPOOL_H