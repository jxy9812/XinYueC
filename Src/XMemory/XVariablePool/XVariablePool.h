/**
 * @file       XVariablePool.h
 * @brief      基于 TLSF 的可变大小内存池公开 API。
 * @details    支持静态内存区域和堆分配模式；在一个连续 arena 内提供任意
 *             对齐大小的分配、释放、分裂和相邻空闲块合并。实现只依赖
 *             XinYueC 的 XMemory 和基础类型接口，不调用平台专用 API。
 *             默认 API 为单线程模式；多线程使用时通过锁回调保护同一个池。
 */
#ifndef XVARIABLEPOOL_H
#define XVARIABLEPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "XAtomic.h"

/** @brief TLSF 二级索引的位数；取值越大分级越细、元数据越多。 */
#ifndef XVARIABLEPOOL_SLI_BITS
#define XVARIABLEPOOL_SLI_BITS 4u
#endif

#if XVARIABLEPOOL_SLI_BITS < 3u || XVARIABLEPOOL_SLI_BITS > 5u
#error "XVARIABLEPOOL_SLI_BITS must be between 3 and 5"
#endif

/** @brief TLSF 每个一级索引包含的二级空闲链表数量。 */
#define XVARIABLEPOOL_SLI_COUNT ((size_t)1u << XVARIABLEPOOL_SLI_BITS)
/** @brief size_t 可表达的一级索引数量。 */
#define XVARIABLEPOOL_FL_COUNT (sizeof(size_t) * 8u)
/** @brief 默认按指针大小对齐。 */
#define XVARIABLEPOOL_DEFAULT_ALIGNMENT (sizeof(void*))

typedef struct XVariablePoolBlock XVariablePoolBlock;

/**
 * @brief 内存池临界区进入/离开回调类型。
 * @param context 调用者提供的锁上下文；函数不取得其所有权。
 * @return 无。
 * @note lock 和 unlock 必须成对设置；都为 NULL 表示调用方保证单线程访问。
 */
typedef void (*XVariablePool_LockMethod)(void* context);

/**
 * @brief TLSF 可变大小内存池。
 * @details 结构体本身可以放在栈、静态区或堆中。free_lists、bitmap 和块
 *          元数据仅供实现使用，初始化后调用方不得修改。
 */
typedef struct XVariablePool
{
    void* raw_memory;                                                /**< 调用方或本对象持有的原始内存起始地址。 */
    void* managed_memory;                                            /**< 实际按 alignment 对齐后的 arena 起始地址。 */
    size_t total_raw_size;                                           /**< raw_memory 可用的原始字节数。 */
    size_t total_managed_size;                                       /**< TLSF 管理的连续字节数，包含尾部哨兵块。 */
    size_t alignment;                                                 /**< 用户数据和内部块的对齐字节数，必须为 2 的幂。 */
    size_t header_size;                                               /**< 内部块头按 alignment 向上取整后的字节数。 */
    size_t minimum_block_size;                                       /**< 可进入空闲链表的最小内部块大小。 */

    size_t total_user_size;                                           /**< 初始 arena 可提供的用户容量快照，不含首个块头。 */
    size_t free_user_size;                                            /**< 当前所有空闲块可复用的用户容量总和。 */
    size_t allocated_user_size;                                      /**< 当前已分配块的可用容量总和。 */
    size_t allocation_count;                                         /**< 当前已分配块数量。 */
    size_t free_block_count;                                         /**< 当前空闲块数量。 */

    size_t fl_bitmap;                                                  /**< 一级索引非空位图。 */
    size_t sl_bitmap[XVARIABLEPOOL_FL_COUNT];                        /**< 每个一级索引的二级非空位图。 */
    XVariablePoolBlock* free_lists[XVARIABLEPOOL_FL_COUNT][XVARIABLEPOOL_SLI_COUNT]; /**< 空闲块链表头，仅供实现使用。 */

    XVariablePool_LockMethod lock;                                    /**< 可选的临界区进入回调，不取得 context 所有权。 */
    XVariablePool_LockMethod unlock;                                  /**< 可选的临界区离开回调。 */
    void* lock_context;                                               /**< lock/unlock 使用的借用上下文。 */
    bool owns_memory;                                                  /**< delete 时是否释放 raw_memory。 */
    bool initialized;                                                  /**< 是否已经成功完成 init。 */
} XVariablePool;

/**
 * @brief 定义一个静态 TLSF 内存池及其 backing buffer。
 * @param name 生成的池指针变量名前缀。
 * @param total_bytes 编译期常量，backing buffer 的字节数。
 * @note 需要在文件作用域使用；初始化必须调用 XVARIABLEPOOL_INIT。
 */
#define XVARIABLEPOOL_DEFINE(name, total_bytes)                              \
    static struct {                                                            \
        XVariablePool pool;                                                    \
        XALIGNAS(XALIGN_PTR_SIZE) unsigned char buffer[(total_bytes)];        \
    } name##_data = {0};                                                       \
    static XVariablePool* name = NULL

/**
 * @brief 初始化由 XVARIABLEPOOL_DEFINE 定义的静态池。
 * @param name XVARIABLEPOOL_DEFINE 生成的变量名前缀。
 * @param alignment 用户数据对齐值，为 0 时使用默认对齐。
 * @return 无；初始化失败时 name 为 NULL。
 */
#define XVARIABLEPOOL_INIT(name, alignment)                                   \
    do {                                                                       \
        if (XVariablePool_init(&name##_data.pool, name##_data.buffer,         \
                               sizeof(name##_data.buffer), (alignment)))      \
            name = &name##_data.pool;                                          \
        else                                                                   \
            name = NULL;                                                       \
    } while (0)

/**
 * @brief 在栈上声明并初始化一个 TLSF 内存池。
 * @param name 栈上池变量名。
 * @param total_bytes 编译期常量，栈 buffer 的字节数。
 * @param alignment 用户数据对齐值，为 0 时使用默认对齐。
 * @return 初始化成功返回 true，失败返回 false。
 * @warning buffer 生命周期必须覆盖池的全部使用时间，不能返回局部作用域。
 */
#define XVARIABLEPOOL_STACK(name, total_bytes, alignment)                      \
    XVariablePool name;                                                         \
    XALIGNAS(XALIGN_PTR_SIZE) unsigned char name##_buffer[(total_bytes)];       \
    XVariablePool_init(&(name), (name##_buffer), sizeof(name##_buffer),         \
                       (alignment))

/**
 * @brief 初始化静态或调用方持有的 TLSF 内存池。
 * @param pool 待初始化的池结构体；调用方提供存储空间。
 * @param memory 连续 backing buffer；函数只借用，不释放。
 * @param total_bytes memory 的总字节数。
 * @param alignment 用户数据对齐值；为 0 时使用 sizeof(void*)，必须为 2 的幂。
 * @return 参数有效且 arena 至少能容纳一个可分配块时返回 true；失败时 pool 保持未初始化状态。
 */
bool XVariablePool_init(XVariablePool* pool, void* memory, size_t total_bytes, size_t alignment);

/**
 * @brief 初始化 TLSF 内存池并配置可选的锁回调。
 * @param pool 待初始化的池结构体；调用方提供存储空间。
 * @param memory 连续 backing buffer；函数只借用，不释放。
 * @param total_bytes memory 的总字节数。
 * @param alignment 用户数据对齐值；为 0 时使用 sizeof(void*)。
 * @param lock 多线程访问时的加锁回调；与 unlock 必须同时为 NULL 或非 NULL。
 * @param unlock 多线程访问时的解锁回调。
 * @param lock_context 传递给 lock/unlock 的上下文；不取得所有权。
 * @return 成功返回 true，失败时 pool 保持未初始化状态。
 */
bool XVariablePool_init_ex(XVariablePool* pool,
                           void* memory,
                           size_t total_bytes,
                           size_t alignment,
                           XVariablePool_LockMethod lock,
                           XVariablePool_LockMethod unlock,
                           void* lock_context);

/**
 * @brief 在堆上创建一个 TLSF 内存池。
 * @param total_bytes 请求的 arena 字节数；实际分配会额外预留对齐损耗。
 * @param alignment 用户数据对齐值；为 0 时使用 sizeof(void*)。
 * @return 成功返回新池；失败返回 NULL。
 * @note 池结构体和 backing buffer 均由 XMemory 系统分配器创建，必须使用 XVariablePool_delete 释放。
 */
XVariablePool* XVariablePool_create(size_t total_bytes, size_t alignment);

/**
 * @brief 从调用方提供的内存区域创建堆上的 TLSF 池控制结构。
 * @param memory 连续 backing buffer；调用方保持所有权和生命周期。
 * @param total_bytes memory 的总字节数。
 * @param alignment 用户数据对齐值；为 0 时使用 sizeof(void*)。
 * @return 成功返回新池；失败返回 NULL。
 * @note 只释放池控制结构，不释放 memory。
 */
XVariablePool* XVariablePool_create_from_memory(void* memory, size_t total_bytes, size_t alignment);

/**
 * @brief 设置池的锁回调。
 * @param pool 已初始化且尚未开始并发使用的池。
 * @param lock 加锁回调；与 unlock 必须同时为 NULL 或非 NULL。
 * @param unlock 解锁回调。
 * @param lock_context 传递给回调的借用上下文。
 * @return 设置成功返回 true；参数不成对或 pool 无效时返回 false。
 * @warning 必须在第一次分配前调用，运行中切换锁回调会导致未定义行为。
 */
bool XVariablePool_setLockMethod(XVariablePool* pool,
                                 XVariablePool_LockMethod lock,
                                 XVariablePool_LockMethod unlock,
                                 void* lock_context);

/**
 * @brief 反初始化静态或调用方持有内存的池。
 * @param pool 已初始化的池；NULL 时不执行任何操作。
 * @return 无；不释放 pool 或 raw_memory。
 * @warning 调用前必须确保没有未释放的用户块和并发访问。
 */
void XVariablePool_deinit(XVariablePool* pool);

/**
 * @brief 删除堆上创建的池。
 * @param pool XVariablePool_create 或 XVariablePool_create_from_memory 返回的池。
 * @return 无；拥有 backing buffer 时同时释放 backing buffer。
 * @warning 不得传入栈对象或由 XVariablePool_init 初始化的对象。
 */
void XVariablePool_delete(XVariablePool* pool);

/**
 * @brief 从 TLSF 池分配一块内存。
 * @param pool 已初始化的池。
 * @param size 请求的用户字节数；0 返回 NULL。
 * @return 成功返回按 alignment 对齐的用户指针；失败返回 NULL。
 * @note 返回块的实际可用容量可通过 XVariablePool_getMaxUserSize 查询。
 */
void* XVariablePool_malloc(XVariablePool* pool, size_t size);

/**
 * @brief 从 TLSF 池分配并清零一块内存。
 * @param pool 已初始化的池。
 * @param count 元素数量。
 * @param size 每个元素的字节数。
 * @return 成功返回已清零的用户指针；乘法溢出、参数为 0 或空间不足时返回 NULL。
 */
void* XVariablePool_calloc(XVariablePool* pool, size_t count, size_t size);

/**
 * @brief 调整已分配块的大小。
 * @param pool 已初始化的池。
 * @param ptr 本池返回的用户指针；NULL 等同于 malloc。
 * @param new_size 新的用户字节数；0 等同于 free 并返回 NULL。
 * @return 成功返回原指针或新指针；失败返回 NULL 且原块保持不变。
 * @note 函数优先尝试原地收缩或利用相邻空闲块扩展，必要时才分配、复制和释放。
 */
void* XVariablePool_realloc(XVariablePool* pool, void* ptr, size_t new_size);

/**
 * @brief 释放由本池分配的用户块。
 * @param pool 已初始化的池。
 * @param ptr 本池返回的用户指针；NULL 不执行任何操作。
 * @return 无；释放后会立即合并相邻空闲块。
 * @note 非法指针和重复释放会被忽略，不会调用底层系统释放器。
 */
void XVariablePool_free(XVariablePool* pool, void* ptr);

/**
 * @brief 判断指针是否为本池当前持有的已分配块。
 * @param pool 待检查的池。
 * @param ptr 待检查的用户指针。
 * @return 属于本池且块仍处于已分配状态时返回 true，否则返回 false。
 * @note 这是无修改的快照查询；多线程下结果可能在返回后立即变化。
 */
bool XVariablePool_is_from_pool(const XVariablePool* pool, const void* ptr);

/**
 * @brief 获取已分配块当前可用的最大用户容量。
 * @param pool 待查询的池。
 * @param ptr 本池返回的用户指针。
 * @return 块的对齐后可用容量；参数无效或块未分配时返回 0。
 */
size_t XVariablePool_getMaxUserSize(const XVariablePool* pool, const void* ptr);

/**
 * @brief 获取当前所有空闲块的可复用用户容量总和。
 * @param pool 待查询的池。
 * @return 空闲容量总和；NULL 或未初始化时返回 0。
 * @note 该值不代表可以一次性申请同样大小的连续块。
 */
size_t XVariablePool_freeSize(const XVariablePool* pool);

/**
 * @brief 获取池初始可提供的用户容量。
 * @param pool 待查询的池。
 * @return 初始 arena 用户容量；NULL 或未初始化时返回 0。
 */
size_t XVariablePool_totalSize(const XVariablePool* pool);

/**
 * @brief 获取当前已分配块的可用容量总和。
 * @param pool 待查询的池。
 * @return 已分配容量总和；NULL 或未初始化时返回 0。
 */
size_t XVariablePool_allocatedSize(const XVariablePool* pool);

/**
 * @brief 获取当前最大的连续空闲用户容量。
 * @param pool 待查询的池。
 * @return 最大连续空闲容量；NULL 或未初始化时返回 0。
 * @note 查询会遍历物理块链，因此不应放在实时分配路径中。
 */
size_t XVariablePool_largestFreeSize(const XVariablePool* pool);

/**
 * @brief 获取当前空闲块数量。
 * @param pool 待查询的池。
 * @return 空闲块数量；NULL 或未初始化时返回 0。
 */
size_t XVariablePool_freeBlockCount(const XVariablePool* pool);

/**
 * @brief 检查物理块链和关键统计值是否一致。
 * @param pool 待检查的池。
 * @return 通过检查返回 true；发现越界、块大小损坏、前后块信息不一致或统计错误时返回 false。
 * @note 该函数用于测试和故障诊断，复杂度为 O(块数量)，不应放在生产分配路径中。
 */
bool XVariablePool_check(const XVariablePool* pool);

#ifdef __cplusplus
}
#endif

#endif /* XVARIABLEPOOL_H */
