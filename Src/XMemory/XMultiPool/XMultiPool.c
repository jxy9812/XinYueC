#include "XMultiPool.h"
#include "XMemory.h" 
#include "XMutex.h"
#include <string.h>
// ---------------------------------------------------------------------------- 
// 内部常量与辅助函数 
// ---------------------------------------------------------------------------- 

/** 定义用于存储池索引的数据类型和大小 */
typedef size_t pool_index_t;
#define POOL_INDEX_SIZE sizeof(pool_index_t)

// 这个函数在 multiplier=2 时很有用，但我们的实现是通用的。
static inline bool is_power_of_two(size_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}
/**
 * @brief 计算为了满足用户请求，子池所需的最小 user_block_size。
 *
 * 子池的 user_block_size 必须 >= (用户请求大小 + 索引大小)
 */
static inline size_t calculate_required_subpool_size(size_t user_request_size) {
    return ALIGN_UP(user_request_size + POOL_INDEX_SIZE,sizeof(void*));
}

/**
 * @brief 从 XFixedPool 分配的原始块指针，计算出最终返回给用户的指针。
 * 布局: [next_ptr | pool_index | user_data]
 */
static void* get_final_user_ptr(void* raw_block_from_fixed_pool) {
    // raw_block_from_fixed_pool 指向 next_ptr后(fixed_pool的user_data)
    // 用户数据 = next_ptr + sizeof(void*) + POOL_INDEX_SIZE
    return (char*)raw_block_from_fixed_pool + POOL_INDEX_SIZE;
}

/**
 * @brief 从用户提供的指针，恢复出 XFixedPool 需要的原始块指针。
 */
static void* get_raw_block_for_fixed_pool(void* user_ptr) {
    // raw_block - POOL_INDEX_SIZE 得到 pool_index 的地址
    return (char*)user_ptr - POOL_INDEX_SIZE ;
}

/**
 * @brief 从用户提供的指针，读取其池索引。
 */
static pool_index_t read_pool_index(void* user_ptr) {
    pool_index_t* index_ptr = (pool_index_t*)((char*)user_ptr - POOL_INDEX_SIZE);
    return *index_ptr;
}

/**
 * @brief 向用户提供的指针写入池索引。
 */
static void write_pool_index(void* raw_block, pool_index_t index) {
    pool_index_t* index_ptr = (pool_index_t*)(raw_block);
    *index_ptr = index;
}
// ----------------------------------------------------------------------------
// 内部辅助函数
// ----------------------------------------------------------------------------

/**
 * @brief 使用二分查找找到新池应插入的位置（保持 user_block_size 升序）
 */
static size_t find_insert_position(const XVector* sub_pools, size_t new_user_block_size) {
    size_t left = 0;
    size_t right = XVector_size_base(sub_pools);

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        XFixedPool* mid_pool = *(XFixedPool**)XVector_at_base(sub_pools, mid);

        if (mid_pool->user_block_size < new_user_block_size) {
            left = mid + 1;
        }
        else {
            right = mid;
        }
    }
    return left;
}

/**
 * @brief 使用二分查找找到能满足 size 需求的最小子池索引
 * 注意：这里的 size 已经是 (用户请求 + 索引大小)
 */
static inline int32_t find_suitable_pool_index(const XVector* sub_pools, size_t size) {
    size_t num_pools = XVector_size_base(sub_pools);
    if (num_pools == 0) {
        return -1;
    }

    size_t left=0, mid = 0;
    size_t right = num_pools;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        XFixedPool* mid_pool = *(XFixedPool**)XVector_at_base(sub_pools, mid);

        // 核心逻辑：只关心是否足够大
        if (mid_pool->user_block_size < size) {
            // 当前池太小，答案在右半部分
            left = mid + 1;
        }
        else {
            // 当前池足够大，它可能是答案，但我们要找最小的，所以继续在左半部分（包括mid）查找
            right = mid;
        }
    }

    // 循环结束时，left == right
    // left 就是第一个满足 user_block_size >= size 的索引
    // 如果 left == num_pools，说明所有池都太小
    if (left < num_pools) {
        return (int32_t)left;
    }
    else {
        return -1;
    }
}
// 返回满足 initial_size * multiplier^index >= required_size 的最小 index
static inline int32_t compute_pool_index(
    size_t required_size,
    size_t initial_size,
    size_t growth_multiplier
) {
    if (required_size <= initial_size) {
        return 0;
    }

    size_t current = initial_size;
    int32_t index = 0;

    // 循环直到 current >= required_size
    while (current < required_size) {
        // 防止溢出：如果 current > SIZE_MAX / growth_multiplier，再乘就会溢出
        if (current > SIZE_MAX / growth_multiplier) {
            // 已经是最大可能值，但仍不够？那就返回下一个 index（由调用者判断是否越界）
            index++;
            break;
        }
        current *= growth_multiplier;
        index++;
    }

    return index;
}
// ----------------------------------------------------------------------------
// 核心 API 实现
// ----------------------------------------------------------------------------
bool XMultiPool_enable_power_of_two_mode(XMultiPool* multi_pool, size_t initial_size, size_t multiplier) {
    if (!multi_pool || initial_size == 0 || multiplier <= 1) {
        return false;
    }

    // 检查是否已经有子池被添加。如果有，则不能切换模式。
    if (XVector_size_base(multi_pool->sub_pools) > 0) {
        return false; // 必须在添加任何池之前启用
    }

    multi_pool->is_power_of_two_mode = true;
    multi_pool->initial_size = initial_size;
    multi_pool->next_expected_size = initial_size;
    multi_pool->growth_multiplier = multiplier;

    return true;
}
size_t XMultiPool_getMaxUserSize(XMultiPool* mp, void* ptr)
{
    if (ptr == NULL) {
        return 0;
    }

    // 如果传入的是 NULL，使用全局内存池
    if (mp == NULL) {
        mp = XMultiPool_global();
        if (mp == NULL) {
            return 0; // 全局池未初始化
        }
    }

    // 判断指针是否来自此内存池
    if (!XMultiPool_is_from_pool(mp, ptr)) {
        return 0;
    }

    // 读取 pool_index
    pool_index_t idx = read_pool_index(ptr);
    if (idx >= XContainerSize(mp->sub_pools)) {
        return 0; // 索引无效
    }

    // 获取对应的 XFixedPool
    XFixedPool* fixed_pool = *(XFixedPool**)XVector_at_base(mp->sub_pools, idx);
    if (fixed_pool == NULL) {
        return 0;
    }

    // 返回用户可用大小（总块大小减去头部索引的大小）
    return fixed_pool->user_block_size - sizeof(pool_index_t);
}
void* XMultiPool_malloc(XMultiPool* multi_pool, size_t size) {
    if (!multi_pool || !multi_pool->sub_pools || size == 0) {
        return NULL;
    }
    const XVector* sub_pools = multi_pool->sub_pools;
    size_t num_pools = XContainerSize(sub_pools);

    // --- 计算查找所需的大小 ---
    size_t required_size = calculate_required_subpool_size(size);
    int32_t start_pool_idx = 0;
    if (multi_pool->is_power_of_two_mode)
        start_pool_idx = compute_pool_index(required_size, multi_pool->initial_size, multi_pool->growth_multiplier);
    else
        start_pool_idx = find_suitable_pool_index(sub_pools, required_size);
    if (start_pool_idx==-1||start_pool_idx >= num_pools) {
        return NULL; // 请求过大，无池可满足
    }

    // 从找到的最小子池开始，向后遍历所有更大的子池
    for (size_t i = start_pool_idx; i < num_pools; ++i) {
        XFixedPool* pool = *(XFixedPool**)XVector_at_base(sub_pools, i);
        if (pool->user_block_size < required_size)
            continue;
        // 1. 尝试从当前子池获取原始块
        void* raw_block = XFixedPool_malloc(pool);
        if (raw_block) 
        {
           // *((pool_index_t*)raw_block) = i;//这里写入的是实际分配成功的池索引 i
            write_pool_index(raw_block, (pool_index_t)i); // 注意：这里写入的是实际分配成功的池索引 i
            // 2. 分配成功，在原始块中写入当前池的索引
           // 布局: [next_ptr | pool_index | ...]
            void* user_data_start = get_final_user_ptr(raw_block);
            // 3. 返回最终的用户指针
            //XPrintf("XMultiPool  malloc index:%i ptr:%p\n", i, user_data_start);
            return user_data_start;
        }
        //XPrintf("当前池已满下一个 尝试\n");
        // 如果当前池已空，继续尝试下一个更大的池
    }

    // 所有合适的池都已耗尽
    XPrintf("所有池子都耗尽了\n");
    return NULL;
}
void* XMultiPool_calloc(XMultiPool* multi_pool, size_t count, size_t size)
{
    // 处理乘法溢出和边界情况
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (count > SIZE_MAX / size) {
        // 请求的总大小溢出
        return NULL;
    }
    size_t total_size = count * size;

    void* ptr = XMultiPool_malloc(multi_pool, total_size);
    if (ptr) {
        // 分配成功，清零内存
        memset(ptr, 0, total_size);
    }
    return ptr;
}
void* XMultiPool_realloc(XMultiPool* multi_pool, void* ptr, size_t new_size)
{
    // 边界情况 1: 新大小为 0 -> 释放并返回 NULL
    if (new_size == 0) {
        if (ptr != NULL) {
            XMultiPool_free(multi_pool, ptr);
        }
        return NULL;
    }

    // 边界情况 2: 原指针为 NULL -> 等同于 malloc
    if (ptr == NULL) {
        return XMultiPool_malloc(multi_pool, new_size);
    }

    // 验证指针是否来自此内存池
    if (!XMultiPool_is_from_pool(multi_pool, ptr)) {
        // 非法指针，通常应报错或断言，这里静默返回 NULL
        return NULL;
    }

    // 1. 获取原指针对应的子池索引和子池
    pool_index_t old_pool_idx = read_pool_index(ptr);
    XFixedPool* old_sub_pool = *(XFixedPool**)XVector_at_base(multi_pool->sub_pools, old_pool_idx);
    // 注意：user_block_size 包含了 POOL_INDEX_SIZE，所以用户实际可用大小需要减去它
    size_t old_user_size = old_sub_pool->user_block_size - POOL_INDEX_SIZE;

    // 2. 判断新请求是否可以被原块满足（包括缩小和原地不动）
    if (new_size <= old_user_size) {
        // 新请求小于或等于原块的用户可用大小，直接返回原指针
        return ptr;
    }

    // 3. 新请求更大，必须分配新块
    void* new_ptr = XMultiPool_malloc(multi_pool, new_size);
    if (new_ptr == NULL) {
        // 分配新块失败，不能动旧块，返回 NULL
        return NULL;
    }

    // 4. 复制旧数据到新块（只复制原块中的有效数据）
    memcpy(new_ptr, ptr, old_user_size);

    // 5. 释放旧块
    XMultiPool_free(multi_pool, ptr);

    return new_ptr;
}
void XMultiPool_free(XMultiPool* multi_pool, void* ptr) {
    if (!multi_pool || !multi_pool->sub_pools || !ptr) {
        return;
    }
    // 1. 从用户指针读取池索引
    pool_index_t pool_idx = read_pool_index(ptr);
    size_t num_pools = XContainerSize(multi_pool->sub_pools);

    // 安全检查：确保索引有效
    if (pool_idx >= num_pools) {
        // 可能是非法指针或已损坏，可以选择断言或静默忽略
        return;
    }

    // 2. 获取对应的子池
    XFixedPool* pool = *(XFixedPool**)XVector_at_base(multi_pool->sub_pools, pool_idx);

    // 3. 恢复出原始块指针并归还
    void* raw_block = get_raw_block_for_fixed_pool(ptr);
    //XPrintf("XMultiPool free index:%i ptr:%p\n", pool_idx, ptr);
    XFixedPool_free(pool, raw_block);
}


// ----------------------------------------------------------------------------
// 动态模式 API 实现
// ----------------------------------------------------------------------------

XMultiPool* XMultiPool_create(void) {
    XMultiPool* multi_pool = (XMultiPool*)XMalloc_System(sizeof(XMultiPool));
    if (!multi_pool) {
        return NULL;
    }
    XMultiPool_init(multi_pool);

    multi_pool->owns_memory = true;
    return multi_pool;
}

void XMultiPool_delete(XMultiPool* multi_pool) {
    if (!multi_pool) {
        return;
    }

    if (multi_pool->owns_memory && multi_pool->sub_pools) {
        size_t num_pools = XVector_size_base(multi_pool->sub_pools);
        for (size_t i = 0; i < num_pools; ++i) {
            XFixedPool* pool = *(XFixedPool**)XVector_at_base(multi_pool->sub_pools, i);
            if (pool) {
                XFixedPool_delete(pool);
            }
        }
    }

    if (multi_pool->sub_pools) {
        XVector_delete_base(multi_pool->sub_pools);
    }
    XFree_System(multi_pool);
}

bool XMultiPool_add_pool(XMultiPool* multi_pool, XFixedPool* sub_pool) {
    if (!multi_pool || !multi_pool->sub_pools || !sub_pool) {
        return false;
    }
    size_t new_pool_size = sub_pool->user_block_size;

    // ========== 分支：倍数模式下的严格检查 ==========
    if (multi_pool->is_power_of_two_mode) {
        // 1. 检查大小是否匹配期望值
        if (new_pool_size != multi_pool->next_expected_size) {
            return false; // 大小不符合倍数规则
        }

        // 2. 在倍数模式下，由于我们总是按顺序添加，且检查了期望值，
        //    重复检查是冗余的，但为了代码清晰和防御性编程，可以保留。
        //    这里为了效率，我们省略它，因为逻辑上不可能重复。

        // 更新期望的下一个大小
        multi_pool->next_expected_size *= multi_pool->growth_multiplier;

        // 在倍数模式下，直接追加到末尾，保持 O(1) 插入
        if (!XVector_push_back_1_base(multi_pool->sub_pools, &sub_pool)) {
            return false;
        }
    }
    // ========== 分支：普通模式（高效二分查找 + 重复检查） ==========
    else {
        // 1. 找到正确的插入位置以保持 user_block_size 升序
        size_t insert_pos = find_insert_position(multi_pool->sub_pools, sub_pool->user_block_size);

        // 2. 在指定位置插入指针
        // XVector_insert_2 的 index 参数是 int64_t 类型
        return XVector_insert_2(multi_pool->sub_pools, (int64_t)insert_pos, &sub_pool);
    }
}

// ----------------------------------------------------------------------------
// 静态/栈模式 API 实现
// ----------------------------------------------------------------------------

bool XMultiPool_init(XMultiPool* multi_pool) {
    if (!multi_pool) {
        return false;
    }
    memset(multi_pool, 0, sizeof(XMultiPool));

    // 初始化一个 XVector 来存放 XFixedPool* 指针
    multi_pool->sub_pools = XVector_Create(XFixedPool*);
    if (!multi_pool->sub_pools) {
        return false;
    }

    multi_pool->owns_memory = false; // 静态模式下不拥有所有权
    return true;
}

void XMultiPool_deinit(XMultiPool* multi_pool) {
    if (!multi_pool) {
        return;
    }

    if (!multi_pool->owns_memory && multi_pool->sub_pools) {
        size_t num_pools = XVector_size_base(multi_pool->sub_pools);
        for (size_t i = 0; i < num_pools; ++i) {
            XFixedPool* pool = *(XFixedPool**)XVector_at_base(multi_pool->sub_pools, i);
            if (pool) {
                XFixedPool_deinit(pool);
            }
        }
    }

    if (multi_pool->sub_pools) {
        XVector_delete_base(multi_pool->sub_pools);
        multi_pool->sub_pools = NULL;
    }
}

bool XMultiPool_is_from_pool(const XMultiPool* multi_pool, const void* ptr)
{
    if (!multi_pool || !multi_pool->sub_pools || !ptr) {
        return false;
    }

    // 1. 从用户指针读取池索引
    pool_index_t pool_idx = read_pool_index((void*)ptr);
    size_t num_pools = XContainerSize(multi_pool->sub_pools);

    // 2. 检查索引是否在有效范围内
    if (pool_idx >= num_pools) {
        return false;
    }

    // 3. 获取对应的子池
    XFixedPool* sub_pool = *(XFixedPool**)XVector_at_base(multi_pool->sub_pools, pool_idx);
    if (!sub_pool) {
        return false; // 子池指针不应为空
    }

    // 4. 将原始块指针委托给子池进行最终验证
    //    注意：我们需要将用户指针转换回 XFixedPool 能理解的原始块指针
    void* raw_block = get_raw_block_for_fixed_pool((void*)ptr);
    return XFixedPool_is_from_pool(sub_pool, raw_block);
}

static XMultiPool* global_pool = NULL;
//static XMutex* m_mutex = NULL;
static void XMultiPool_initGlobal()
{
    if (global_pool)return;
    global_pool = XMultiPool_create();
    XMultiPool_enable_power_of_two_mode(global_pool,32,2);
    //m_mutex = XMutex_create(XLock_NonRecursive);
   /* global_pool = XFixedPool_create(512, 500);
    return;*/
    XMultiPool_add_pool(global_pool, XFixedPool_create(32, 200));
    XMultiPool_add_pool(global_pool, XFixedPool_create(64, 100));
    XMultiPool_add_pool(global_pool, XFixedPool_create(128, 50));
    XMultiPool_add_pool(global_pool, XFixedPool_create(256, 10));
    XMultiPool_add_pool(global_pool, XFixedPool_create(512, 5));
}
XMultiPool* XMultiPool_global()
{
    if (!global_pool)
        XMultiPool_initGlobal();
    return global_pool;
}
void* XMultiPool_global_malloc(size_t size)
{
    /*return XMalloc_System(size);*/
    //XMutex_lock(m_mutex);
    void* ptr = global_pool ? XMultiPool_malloc(global_pool, size) : NULL;
    //XMutex_unlock(m_mutex);
    return ptr;
}
void* XMultiPool_global_calloc(size_t count, size_t size)
{
    return XMultiPool_calloc(global_pool,count,size);
}
void* XMultiPool_global_realloc(void* ptr, size_t size)
{
    return XMultiPool_realloc(global_pool, ptr, size);
}
void XMultiPool_global_free(void* ptr)
{
   /* XFree_System(ptr);
    return;*/
    //XMutex_lock(m_mutex);
    if(global_pool&&ptr)
        XMultiPool_free(global_pool, ptr);
    //XMutex_unlock(m_mutex);
}