/**
 * @file XMultiPool.h
 * @brief 多级固定大小内存池（手动数组，零 XVector 依赖）
 *
 * 双模式支持：
 *   1. 堆模式：XMultiPool_create() → 自动分配子池数组，可动态扩容
 *   2. 静态模式：XMULTIPOOL_STATIC_DEFINE + XMULTIPOOL_STATIC_INIT → BSS 段零堆分配
 *
 * 特性：
 *   - 根据请求大小自动选择最合适的子池（二分查找，O(log N)）
 *   - 支持倍数增长模式（power-of-two），自动计算子池索引
 *   - 线程安全（基于 XAtomic 原子操作）
 *   - 支持 malloc / calloc / realloc / free 全套 API
 *
 * 使用示例（静态模式）：
 * @code
 *   // 文件作用域
 *   XMULTIPOOL_STATIC_DEFINE(myPool, 16);
 *
 *   // 初始化函数内
 *   XMULTIPOOL_STATIC_INIT(myPool, 16);
 *   XMultiPool_enable_power_of_two_mode(myPool, 32, 2);
 *   XMultiPool_add_pool(myPool, XFixedPool_create(32, 256));
 *   XMultiPool_add_pool(myPool, XFixedPool_create(64, 256));
 *
 *   // 使用
 *   void* ptr = XMultiPool_malloc(myPool, 100);
 *   XMultiPool_free(myPool, ptr);
 * @endcode
 *
 * 使用示例（全局池便捷 API）：
 * @code
 *   void* ptr = XMultiPool_global_malloc(100);
 *   XMultiPool_global_free(ptr);
 * @endcode
 */

#ifndef XMULTIPOOL_H
#define XMULTIPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>
#include "XFixedPool.h"
#include "XAtomic.h"

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief 多级内存池结构体
 *
 * 内部维护一个 XFixedPool* 指针数组（手动管理），按 user_block_size 升序排列。
 * 分配时使用二分查找定位最合适的子池。
 */
typedef struct XMultiPool {
    XFixedPool** sub_pools;           /**< 指针数组（堆分配或指向静态数组） */
    size_t       sub_pool_count;      /**< 当前子池数量 */
    size_t       sub_pool_capacity;   /**< 数组总容量（堆模式可扩容） */

    bool         owns_memory;         /**< 堆模式下为 true，delete 时释放子池 */
    bool         is_power_of_two_mode;/**< 是否启用倍数增长模式 */
    size_t       initial_size;        /**< 倍数模式下的初始块大小 */
    size_t       next_expected_size;  /**< 倍数模式下期望的下一个块大小 */
    size_t       growth_multiplier;   /**< 倍数模式下的增长倍数 */

    size_t         total_user_size;   /**< 总用户可用内存大小（字节） */
    XAtomic_size_t free_user_size;    /**< 当前剩余用户可用内存（原子变量） */
} XMultiPool;

/* ============================================================================
 * 动态模式（堆分配）
 * ============================================================================ */

/**
 * @brief      在堆上创建一个多级内存池。
 * @return     新建的 XMultiPool；分配失败返回 NULL，调用方必须使用
 *             XMultiPool_delete 释放。
 * @note       默认分配 16 个 XFixedPool* 槽位，可在需要时自动扩容。
 */
XMultiPool* XMultiPool_create(void);

/**
 * @brief      销毁堆上创建的多级内存池并释放其拥有的子池。
 * @param      multi_pool 由 XMultiPool_create 返回的对象；可为 NULL。
 * @return     无。传入 NULL 时不执行任何操作。
 */
void XMultiPool_delete(XMultiPool* multi_pool);

/* ============================================================================
 * 静态/栈模式（零堆分配）
 * ============================================================================ */

/**
 * @brief      初始化一个静态或栈上的 XMultiPool 实例。
 * @param      multi_pool 调用方提供的未初始化对象；不能为 NULL。
 * @return     成功返回 true；参数为 NULL 返回 false。
 * @note       此函数仅将结构体清零，子池数组需通过静态宏或手动设置。
 */
bool XMultiPool_init(XMultiPool* multi_pool);

/**
 * @brief      反初始化静态或栈模式的多级内存池。
 * @param      multi_pool 已初始化的对象；可为 NULL。
 * @return     无。不会释放静态或栈上的子池数组存储。
 * @note       函数只清理子池状态和对象拥有的动态资源，不释放调用方提供的数组。
 */
void XMultiPool_deinit(XMultiPool* multi_pool);

/* ============================================================================
 * 静态宏（BSS 段，零动态分配）
 * ============================================================================ */

/**
 * @brief 在文件作用域声明静态多级内存池
 * @param name     变量名前缀（将生成 name##_sub_array、name##_stack、name）
 * @param max_sub  最大子池数量（编译期常量）
 *
 * 使用示例：
 * @code
 *   XMULTIPOOL_STATIC_DEFINE(myPool, 16);
 * @endcode
 */
#define XMULTIPOOL_STATIC_DEFINE(name, max_sub) \
    static XFixedPool* name##_sub_array[max_sub];  \
    static XMultiPool  name##_stack;                \
    static XMultiPool* name = NULL

/**
 * @brief 初始化由 XMULTIPOOL_STATIC_DEFINE 声明的静态池
 * @param name     变量名前缀（与 DEFINE 一致）
 * @param max_sub  最大子池数量
 *
 * 使用示例：
 * @code
 *   void init(void) {
 *       XMULTIPOOL_STATIC_INIT(myPool, 16);
 *       XMultiPool_add_pool(myPool, ...);
 *   }
 * @endcode
 */
#define XMULTIPOOL_STATIC_INIT(name, max_sub) \
    do { \
        memset(&name##_stack, 0, sizeof(XMultiPool)); \
        name##_stack.sub_pools = name##_sub_array;     \
        name##_stack.sub_pool_capacity = max_sub;       \
        name = &name##_stack;                            \
    } while(0)

/**
 * @brief 在栈上初始化 XMultiPool（函数内使用）
 * @param pool       指向栈上的 XMultiPool 实例
 * @param array      指向栈上的 XFixedPool* 数组
 * @param array_size 数组元素数量
 *
 * 使用示例：
 * @code
 *   void func(void) {
 *       XFixedPool* arr[8];
 *       XMultiPool pool;
 *       XMULTIPOOL_STACK_INIT(&pool, arr, 8);
 *       // ... 使用 pool
 *   }
 * @endcode
 */
#define XMULTIPOOL_STACK_INIT(pool, array, array_size) \
    do { \
        memset((pool), 0, sizeof(XMultiPool)); \
        (pool)->sub_pools = (array); \
        (pool)->sub_pool_capacity = (array_size); \
    } while(0)

/* ============================================================================
 * 池管理操作
 * ============================================================================ */

/**
 * @brief      向多级内存池中添加一个子池。
 * @param      multi_pool 已初始化的目标内存池；不能为 NULL。
 * @param      pool 已初始化的子池；调用方转移其所有权，不能为 NULL。
 * @return     添加成功返回 true；参数非法、容量不足或大小重复返回 false。
 * @note       子池按 user_block_size 升序自动插入到正确位置。
 */
bool XMultiPool_add_pool(XMultiPool* multi_pool, XFixedPool* pool);

/**
 * @brief      启用倍数增长模式。
 * @param      multi_pool 已初始化且尚未添加子池的内存池；不能为 NULL。
 * @param      initial_size 第一个子池的用户块大小，单位为字节，必须大于 0。
 * @param      multiplier 增长倍数，必须大于 1 且不能造成 size_t 溢出。
 * @return     配置成功返回 true；状态、参数或溢出检查失败返回 false。
 * @note       必须在添加任何子池之前调用；启用后 add_pool 会严格校验大小。
 *
 * 示例：initial_size=32, multiplier=2 → 期望子池大小: 32, 64, 128, 256, ...
 */
bool XMultiPool_enable_power_of_two_mode(XMultiPool* multi_pool, size_t initial_size, size_t multiplier);

/* ============================================================================
 * 内存分配 / 释放 / 查询
 * ============================================================================ */

/**
 * @brief 从多级内存池中分配内存
 * @param multi_pool 内存池指针
 * @param size 请求的字节数
 * @return 成功返回指针，失败返回 NULL
 * @note 使用二分查找（O(log N)）定位最合适的子池，优先选择 size 最小的可用块
 */
void* XMultiPool_malloc(XMultiPool* multi_pool, size_t size);

/**
 * @brief 从多级内存池中分配并清零内存
 * @param multi_pool 内存池指针
 * @param count 元素个数
 * @param size 每个元素的字节数
 * @return 成功返回指针，失败返回 NULL
 */
void* XMultiPool_calloc(XMultiPool* multi_pool, size_t count, size_t size);

/**
 * @brief 调整已分配内存块的大小
 * @param multi_pool 内存池指针
 * @param ptr 原指针（可为 NULL，等同于 malloc）
 * @param new_size 新的字节数（为 0 则释放）
 * @return 成功返回新指针，失败返回 NULL
 * @note 如果 new_size <= 原块大小，直接返回原指针（无拷贝）
 */
void* XMultiPool_realloc(XMultiPool* multi_pool, void* ptr, size_t new_size);

/**
 * @brief      释放由 XMultiPool 分配的内存。
 * @param      multi_pool 分配该内存的池；不能为 NULL。
 * @param      ptr 由该池返回的指针；可为 NULL。
 * @return     无。指针不属于该池时不会释放并由实现记录错误状态。
 */
void XMultiPool_free(XMultiPool* multi_pool, void* ptr);

/**
 * @brief 检查指针是否由指定内存池分配
 * @param multi_pool 内存池指针
 * @param ptr 要检查的指针
 * @return 是则返回 true
 */
bool XMultiPool_is_from_pool(const XMultiPool* multi_pool, const void* ptr);

/**
 * @brief      获取由 XMultiPool 分配的内存块的最大用户可用大小。
 * @param      mp 内存池指针；为 NULL 时使用全局池。
 * @param      ptr 由指定池分配的内存指针；为 NULL 或不属于该池时无效。
 * @return     用户可用字节数；参数无效时返回 0。
 */
size_t XMultiPool_getMaxUserSize(XMultiPool* mp, void* ptr);

/* ============================================================================
 * 内存统计
 * ============================================================================ */

/**
 * @brief      获取多级内存池当前剩余可用内存大小。
 * @param      multi_pool 内存池指针；可为 NULL。
 * @return     剩余用户可用字节数；参数为 NULL 时返回 0。
 * @note       线程安全，读取使用原子操作。
 */
size_t XMultiPool_freeSize(XMultiPool* multi_pool);

/**
 * @brief      获取多级内存池总用户可用内存大小。
 * @param      multi_pool 内存池指针；可为 NULL。
 * @return     总用户可用字节数；参数为 NULL 时返回 0。
 */
size_t XMultiPool_totalSize(XMultiPool* multi_pool);

/**
 * @brief 获取多级内存池中的子池数量。
 * @param multi_pool 多级内存池指针；可为 NULL。
 * @return 当前子池数量；参数为 NULL 时返回 0。
 */
size_t XMultiPool_subPoolCount(const XMultiPool* multi_pool);

/**
 * @brief 按索引获取只读子池指针。
 * @param multi_pool 多级内存池指针；可为 NULL。
 * @param index 子池索引，从 0 开始。
 * @return 子池只读指针；索引越界或参数为 NULL 时返回 NULL。
 */
const XFixedPool* XMultiPool_subPoolAt(const XMultiPool* multi_pool, size_t index);

/* ============================================================================
 * 全局池便捷 API（零配置，自动初始化）
 * ============================================================================ */

/**
 * @brief      获取全局多级内存池实例。
 * @return     全局借用 XMultiPool 指针；首次调用自动初始化，失败返回 NULL。
 * @note       全局池由库管理，调用方不得删除或反初始化返回指针。
 */
XMultiPool* XMultiPool_global(void);

/**
 * @brief 从全局内存池中分配内存（零配置便捷版）
 * @param size 请求的字节数
 * @return 成功返回指针，失败返回 NULL
 * @note 全局池自动初始化，预配置 5 个子池（32/64/128/256/512）
 */
void* XMultiPool_global_malloc(size_t size);

/**
 * @brief 从全局内存池中分配并清零内存
 * @param count 元素个数
 * @param size 每个元素的字节数
 * @return 成功返回指针，失败返回 NULL
 */
void* XMultiPool_global_calloc(size_t count, size_t size);

/**
 * @brief 调整全局内存池中已分配内存块的大小
 * @param ptr 原指针
 * @param size 新的字节数
 * @return 成功返回新指针，失败返回 NULL
 */
void* XMultiPool_global_realloc(void* ptr, size_t size);

/**
 * @brief      释放由全局内存池分配的内存。
 * @param      ptr 由全局池返回的指针；可为 NULL。
 * @return     无。
 */
void XMultiPool_global_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif /* XMULTIPOOL_H */
