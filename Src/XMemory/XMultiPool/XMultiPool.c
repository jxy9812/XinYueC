#include "XMultiPool.h"
#include "XMemory.h"
#include "XMutex.h"
#include <string.h>

/* ============================================================================
 * 内部常量
 * ============================================================================ */

typedef size_t pool_index_t;
#define POOL_INDEX_SIZE sizeof(pool_index_t)

#ifndef XMULTIPOOL_DEFAULT_CAPACITY
#define XMULTIPOOL_DEFAULT_CAPACITY 16
#endif

/* ============================================================================
 * 内部辅助函数（从 user ptr 读/写 pool_index）
 * ============================================================================ */

static inline size_t calculate_required_subpool_size(size_t user_request_size) {
    return XFIXEDPOOL_ALIGN_UP(user_request_size + POOL_INDEX_SIZE, sizeof(void*));
}

static void* get_final_user_ptr(void* raw_block) {
    return (char*)raw_block + POOL_INDEX_SIZE;
}

static void* get_raw_block_for_fixed_pool(void* user_ptr) {
    return (char*)user_ptr - POOL_INDEX_SIZE;
}

static pool_index_t read_pool_index(void* user_ptr) {
    return *(pool_index_t*)((char*)user_ptr - POOL_INDEX_SIZE);
}

static void write_pool_index(void* raw_block, pool_index_t index) {
    *(pool_index_t*)raw_block = index;
}

/* ============================================================================
 * 手动数组操作辅助（替代 XVector）
 * ============================================================================ */

static bool ensure_capacity(XMultiPool* mp, size_t needed) {
    if (mp->sub_pool_capacity >= needed) return true;
    /* 仅堆模式可扩展 */
    if (mp->owns_memory) {
        size_t new_cap = mp->sub_pool_capacity ? mp->sub_pool_capacity * 2 : XMULTIPOOL_DEFAULT_CAPACITY;
        if (new_cap < needed) new_cap = needed;
        XFixedPool** new_arr = (XFixedPool**)XMalloc_System(new_cap * sizeof(XFixedPool*));
        if (!new_arr) return false;
        if (mp->sub_pools) {
            memcpy(new_arr, mp->sub_pools, mp->sub_pool_count * sizeof(XFixedPool*));
            XFree_System(mp->sub_pools);
        }
        mp->sub_pools = new_arr;
        mp->sub_pool_capacity = new_cap;
        return true;
    }
    return false;
}

static bool array_insert(XMultiPool* mp, size_t pos, XFixedPool* pool) {
    size_t n = mp->sub_pool_count;
    if (n >= mp->sub_pool_capacity) return false;
    memmove(&mp->sub_pools[pos + 1], &mp->sub_pools[pos], (n - pos) * sizeof(XFixedPool*));
    mp->sub_pools[pos] = pool;
    mp->sub_pool_count++;
    return true;
}

static bool array_push_back(XMultiPool* mp, XFixedPool* pool) {
    return array_insert(mp, mp->sub_pool_count, pool);
}

/* ============================================================================
 * 二分查找（手动实现，替代 XVector 版本）
 * ============================================================================ */

static size_t find_insert_position(XMultiPool* mp, size_t new_user_block_size) {
    size_t left = 0, right = mp->sub_pool_count;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (mp->sub_pools[mid]->user_block_size < new_user_block_size)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

static int32_t find_suitable_pool_index(XMultiPool* mp, size_t size) {
    if (mp->sub_pool_count == 0) return -1;
    size_t left = 0, right = mp->sub_pool_count;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (mp->sub_pools[mid]->user_block_size < size)
            left = mid + 1;
        else
            right = mid;
    }
    return (left < mp->sub_pool_count) ? (int32_t)left : -1;
}

static int32_t compute_pool_index(size_t required_size, size_t initial_size, size_t growth_multiplier) {
    if (required_size <= initial_size) return 0;
    size_t current = initial_size;
    int32_t index = 0;
    while (current < required_size) {
        if (current > SIZE_MAX / growth_multiplier) { index++; break; }
        current *= growth_multiplier;
        index++;
    }
    return index;
}

/* ============================================================================
 * 核心 API 实现
 * ============================================================================ */

bool XMultiPool_enable_power_of_two_mode(XMultiPool* mp, size_t initial_size, size_t multiplier) {
    if (!mp || initial_size == 0 || multiplier <= 1) return false;
    if (mp->sub_pool_count > 0) return false;
    mp->is_power_of_two_mode = true;
    mp->initial_size = initial_size;
    mp->next_expected_size = initial_size;
    mp->growth_multiplier = multiplier;
    return true;
}

size_t XMultiPool_getMaxUserSize(XMultiPool* mp, void* ptr) {
    if (!ptr) return 0;
    if (!mp) { mp = XMultiPool_global(); if (!mp) return 0; }
    if (!XMultiPool_is_from_pool(mp, ptr)) return 0;
    pool_index_t idx = read_pool_index(ptr);
    if (idx >= mp->sub_pool_count) return 0;
    return mp->sub_pools[idx]->user_block_size - POOL_INDEX_SIZE;
}

void* XMultiPool_malloc(XMultiPool* mp, size_t size) {
    if (!mp || size == 0) return NULL;
    size_t required_size = calculate_required_subpool_size(size);
    int32_t start_idx;
    if (mp->is_power_of_two_mode)
        start_idx = compute_pool_index(required_size, mp->initial_size, mp->growth_multiplier);
    else
        start_idx = find_suitable_pool_index(mp, required_size);
    if (start_idx < 0 || (size_t)start_idx >= mp->sub_pool_count) return NULL;

    for (size_t i = (size_t)start_idx; i < mp->sub_pool_count; ++i) {
        XFixedPool* pool = mp->sub_pools[i];
        if (pool->user_block_size < required_size) continue;
        void* raw = XFixedPool_malloc(pool);
        if (raw) {
            write_pool_index(raw, (pool_index_t)i);
            void* user = get_final_user_ptr(raw);
            size_t alloc_sz = pool->user_block_size - POOL_INDEX_SIZE;
            XAtomic_fetch_sub_size_t(&mp->free_user_size, alloc_sz, XAtomic_MemoryOrder_Release);
            return user;
        }
    }
    XERROR_PRINTF("XMultiPool,所有池子都耗尽了,当前请求块大小:%d\n", size);
    return NULL;
}

void* XMultiPool_calloc(XMultiPool* mp, size_t count, size_t size) {
    if (!mp) return NULL;
    if (count == 0 || size == 0) return NULL;
    if (count > SIZE_MAX / size) return NULL;
    size_t total = count * size;
    void* ptr = XMultiPool_malloc(mp, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void* XMultiPool_realloc(XMultiPool* mp, void* ptr, size_t new_size) {
    if (!mp) return NULL;
    if (new_size == 0) { XMultiPool_free(mp, ptr); return NULL; }
    if (!ptr) return XMultiPool_malloc(mp, new_size);
    if (!XMultiPool_is_from_pool(mp, ptr)) return NULL;

    pool_index_t idx = read_pool_index(ptr);
    size_t old_user_size = mp->sub_pools[idx]->user_block_size - POOL_INDEX_SIZE;
    if (new_size <= old_user_size) return ptr;

    void* new_ptr = XMultiPool_malloc(mp, new_size);
    if (!new_ptr) return NULL;
    memcpy(new_ptr, ptr, old_user_size);
    XMultiPool_free(mp, ptr);
    return new_ptr;
}

void XMultiPool_free(XMultiPool* mp, void* ptr) {
    if (!mp || !ptr) return;
    pool_index_t idx = read_pool_index(ptr);
    if (idx >= mp->sub_pool_count) return;
    XFixedPool* pool = mp->sub_pools[idx];
    void* raw = get_raw_block_for_fixed_pool(ptr);
    XFixedPool_free(pool, raw);
    size_t freed_sz = pool->user_block_size - POOL_INDEX_SIZE;
    XAtomic_fetch_add_size_t(&mp->free_user_size, freed_sz, XAtomic_MemoryOrder_Release);
}

/* ============================================================================
 * 动态模式
 * ============================================================================ */

XMultiPool* XMultiPool_create(void) {
    XMultiPool* mp = (XMultiPool*)XMalloc_System(sizeof(XMultiPool));
    if (!mp) return NULL;
    memset(mp, 0, sizeof(XMultiPool));
    mp->sub_pools = (XFixedPool**)XMalloc_System(XMULTIPOOL_DEFAULT_CAPACITY * sizeof(XFixedPool*));
    if (!mp->sub_pools) { XFree_System(mp); return NULL; }
    mp->sub_pool_capacity = XMULTIPOOL_DEFAULT_CAPACITY;
    mp->owns_memory = true;
    XAtomic_init(mp->free_user_size, 0);
    return mp;
}

void XMultiPool_delete(XMultiPool* mp) {
    if (!mp) return;
    if (mp->owns_memory) {
        for (size_t i = 0; i < mp->sub_pool_count; ++i) {
            XFixedPool* p = mp->sub_pools[i];
            if (p) XFixedPool_delete(p);
        }
        if (mp->sub_pools) XFree_System(mp->sub_pools);
        XFree_System(mp);
    }
}

bool XMultiPool_add_pool(XMultiPool* mp, XFixedPool* sub_pool) {
    if (!mp || !sub_pool) return false;
    /* 堆模式自动扩容 */
    if (mp->owns_memory && !ensure_capacity(mp, mp->sub_pool_count + 1)) return false;

    if (mp->is_power_of_two_mode) {
        if (sub_pool->user_block_size != mp->next_expected_size) return false;
        mp->next_expected_size *= mp->growth_multiplier;
        if (!array_push_back(mp, sub_pool)) return false;
    } else {
        size_t pos = find_insert_position(mp, sub_pool->user_block_size);
        if (!array_insert(mp, pos, sub_pool)) return false;
    }

    size_t pool_user_size = (sub_pool->user_block_size - POOL_INDEX_SIZE) * sub_pool->num_blocks;
    mp->total_user_size += pool_user_size;
    XAtomic_fetch_add_size_t(&mp->free_user_size, pool_user_size, XAtomic_MemoryOrder_Release);
    return true;
}

/* ============================================================================
 * 静态/栈模式
 * ============================================================================ */

bool XMultiPool_init(XMultiPool* mp) {
    if (!mp) return false;
    memset(mp, 0, sizeof(XMultiPool));
    mp->sub_pools = NULL;
    mp->sub_pool_capacity = 0;
    mp->owns_memory = false;
    XAtomic_init(mp->free_user_size, 0);
    return true;
}

void XMultiPool_deinit(XMultiPool* mp) {
    if (!mp || mp->owns_memory) return;
    for (size_t i = 0; i < mp->sub_pool_count; ++i)
        XFixedPool_deinit(mp->sub_pools[i]);
    mp->sub_pool_count = 0;
}

bool XMultiPool_is_from_pool(const XMultiPool* mp, const void* ptr) {
    if (!mp || !ptr) return false;
    pool_index_t idx = read_pool_index((void*)ptr);
    if (idx >= mp->sub_pool_count) return false;
    XFixedPool* sub = mp->sub_pools[idx];
    if (!sub) return false;
    void* raw = get_raw_block_for_fixed_pool((void*)ptr);
    return XFixedPool_is_from_pool(sub, raw);
}

/* ============================================================================
 * 全局池（静态模式 + XFixedPool_create 子池）
 * ============================================================================ */

XMULTIPOOL_STATIC_DEFINE(global_pool, 5);
static bool global_pool_inited = false;

static void XMultiPool_initGlobal(void) {
    if (global_pool_inited) return;
    XMULTIPOOL_STATIC_INIT(global_pool, 5);
    XMultiPool_enable_power_of_two_mode(global_pool, 32, 2);

    XMultiPool_add_pool(global_pool, XFixedPool_create(32,  256));
    XMultiPool_add_pool(global_pool, XFixedPool_create(64,  256));
    XMultiPool_add_pool(global_pool, XFixedPool_create(128, 256));
    XMultiPool_add_pool(global_pool, XFixedPool_create(256, 128));
    XMultiPool_add_pool(global_pool, XFixedPool_create(512, 64));
    //XMultiPool_add_pool(global_pool, XFixedPool_create(1024, 32));
    //XMultiPool_add_pool(global_pool, XFixedPool_create(2048, 32));
    //XMultiPool_add_pool(global_pool, XFixedPool_create(4096, 16));
    //XMultiPool_add_pool(global_pool, XFixedPool_create(8192, 8));

    global_pool_inited = true;
}

XMultiPool* XMultiPool_global(void) {
    if (!global_pool_inited) XMultiPool_initGlobal();
    return global_pool;
}

void* XMultiPool_global_malloc(size_t size) {
    if (!global_pool_inited) XMultiPool_initGlobal();
    return XMultiPool_malloc(global_pool, size);
}

void* XMultiPool_global_calloc(size_t count, size_t size) {
    if (!global_pool_inited) XMultiPool_initGlobal();
    return XMultiPool_calloc(global_pool, count, size);
}

void* XMultiPool_global_realloc(void* ptr, size_t size) {
    if (!global_pool_inited) XMultiPool_initGlobal();
    return XMultiPool_realloc(global_pool, ptr, size);
}

void XMultiPool_global_free(void* ptr) {
    if (!global_pool_inited) return;
    XMultiPool_free(global_pool, ptr);
}

/* ============================================================================
 * 内存统计
 * ============================================================================ */

size_t XMultiPool_freeSize(XMultiPool* mp) {
    if (!mp) return 0;
    return XAtomic_load_size_t(&mp->free_user_size, XAtomic_MemoryOrder_Relaxed);
}

size_t XMultiPool_totalSize(XMultiPool* mp) {
    if (!mp) return 0;
    return mp->total_user_size;
}

size_t XMultiPool_subPoolCount(const XMultiPool* mp) {
    return mp ? mp->sub_pool_count : 0;
}

const XFixedPool* XMultiPool_subPoolAt(const XMultiPool* mp, size_t index) {
    if (!mp || index >= mp->sub_pool_count || !mp->sub_pools) return NULL;
    return mp->sub_pools[index];
}
