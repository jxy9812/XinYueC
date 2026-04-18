#include "XMultiPool.h"
#include "XMemory.h" // 用于 XMalloc/XFree
#include <string.h>

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
 * 假设 sub_pools 已按 user_block_size 升序排列。
 */
static size_t find_suitable_pool_index(const XVector* sub_pools, size_t size) {
    size_t num_pools = XVector_size_base(sub_pools);
    if (num_pools == 0) {
        return num_pools;
    }

    size_t left = 0;
    size_t right = num_pools;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        XFixedPool* mid_pool = *(XFixedPool**)XVector_at_base(sub_pools, mid);

        if (mid_pool->user_block_size < size) {
            left = mid + 1;
        }
        else {
            right = mid;
        }
    }
    return left; // left 是第一个满足 user_block_size >= size 的池索引
}

/**
 * @brief 判断指针属于哪个子池
 */
static size_t find_pool_index_by_ptr(const XVector* sub_pools, void* ptr) {
    if (!ptr) return XVector_size_base(sub_pools);

    char* user_ptr = (char*)ptr;
    size_t num_pools = XVector_size_base(sub_pools);

    for (size_t i = 0; i < num_pools; ++i) {
        XFixedPool* pool = *(XFixedPool**)XVector_at_base(sub_pools, i);
        char* start = (char*)pool->raw_memory;
        char* end = start + pool->total_raw_size;

        // 用户指针的有效范围: [start + sizeof(void*), end)
        if (user_ptr >= (start + sizeof(void*)) && user_ptr < end) {
            return i;
        }
    }
    return num_pools; // 未找到
}

// ----------------------------------------------------------------------------
// 核心 API 实现
// ----------------------------------------------------------------------------

void* XMultiPool_malloc(XMultiPool* multi_pool, size_t size) {
    if (!multi_pool || !multi_pool->sub_pools || size == 0) {
        return NULL;
    }

    const XVector* sub_pools = multi_pool->sub_pools;
    size_t pool_idx = find_suitable_pool_index(sub_pools, size);
    if (pool_idx >= XVector_size_base(sub_pools)) {
        return NULL; // 请求过大，无池可满足
    }

    XFixedPool* pool = *(XFixedPool**)XVector_at_base(sub_pools, pool_idx);
    return XFixedPool_malloc(pool);
}

void XMultiPool_free(XMultiPool* multi_pool, void* ptr) {
    if (!multi_pool || !multi_pool->sub_pools || !ptr) {
        return;
    }

    const XVector* sub_pools = multi_pool->sub_pools;
    size_t pool_idx = find_pool_index_by_ptr(sub_pools, ptr);
    if (pool_idx >= XVector_size_base(sub_pools)) {
        // 错误：尝试释放一个不属于任何子池的指针
        // 在生产代码中，这里可以加入断言或日志
        return;
    }

    XFixedPool* pool = *(XFixedPool**)XVector_at_base(sub_pools, pool_idx);
    XFixedPool_free(pool, ptr);
}

// ----------------------------------------------------------------------------
// 动态模式 API 实现
// ----------------------------------------------------------------------------

XMultiPool* XMultiPool_create(void) {
    XMultiPool* multi_pool = (XMultiPool*)XMalloc(sizeof(XMultiPool));
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
    XFree(multi_pool);
}

bool XMultiPool_add_pool(XMultiPool* multi_pool, XFixedPool* pool) {
    if (!multi_pool || !multi_pool->sub_pools || !pool) {
        return false;
    }

    // 1. 找到正确的插入位置以保持 user_block_size 升序
    size_t insert_pos = find_insert_position(multi_pool->sub_pools, pool->user_block_size);

    // 2. 在指定位置插入指针
    // XVector_insert 的 index 参数是 int64_t 类型
    return XVector_insert(multi_pool->sub_pools, (int64_t)insert_pos, &pool);
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