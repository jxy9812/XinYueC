#include "XFixedPool.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>
#ifndef ALIGN_UP
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#endif
#define XFIXEDPOOL_BLOCK_ALLOCATED ((void*)(uintptr_t)(-1))

// 计算能容纳 [0, max_value] 所需的最少位数
// 例如: max_value=0 -> 1 bit, max_value=1 -> 1 bit, max_value=2 -> 2 bits, max_value=63 -> 6 bits, max_value=64 -> 7 bits.
static size_t calculate_bits_needed_for_max(uintptr_t max_value) {
    if (max_value == 0) {
        return 1;
    }

    // 我们需要找到最小的 n，使得 (1 << n) > max_value
    // 这等价于 ceil(log2(max_value + 1))
    size_t bits = 0;
    uintptr_t value = max_value;

    // 标准的位扫描方法
    while (value > 0) {
        bits++;
        value >>= 1;
    }

    // 特殊情况：如果 max_value+1 刚好是2的幂，上面的循环会少算一位
    // 但我们可以通过检查 (1ULL << (bits-1)) == (max_value+1) 来判断
    // 不过，对于我们的用途，直接返回 bits 即可，因为它已经足够表示 [0, max_value]
    // 因为 (1 << bits) >= max_value + 1
    return bits;
}

/**
 * @brief 计算用户请求的块大小对应的内部块大小
 *
 * 内部块 = 隐藏的 next 指针 + 用户数据区 + 可能的填充（为了对齐）
 */
static size_t calculate_internal_block_size(size_t user_block_size) 
{
    // 我们需要确保整个内部块是 XFIXEDPOOL_ALIGN 对齐的
    size_t internal_size = sizeof(void*) + user_block_size;
    return ALIGN_UP(internal_size, sizeof(void*));
}

/**
 * @brief 获取用户可用数据的起始地址
 */
static void* get_user_data_ptr(void* internal_block) 
{
    return (char*)internal_block + sizeof(void*);
}

/**
 * @brief 从用户数据指针恢复内部块地址
 */
static void* get_internal_block_ptr(void* user_data) {
    return (char*)user_data - sizeof(void*);
}

/**
 * @brief 根据索引获取内部块地址
 */
static inline void* get_block_by_index(XFixedPool* pool, size_t index) {
    return (char*)pool->raw_memory + (index * pool->block_size);
}

/**
 * @brief 在单线程环境下初始化空闲链表
 *
 * 此函数构建的是内部块的链表。
 */
static void initialize_free_list(XFixedPool* pool) {
    char* memory = (char*)pool->raw_memory;
    const size_t num_blocks = pool->num_blocks;

    // 使用 char* 进行指针算术更清晰
    char* current = memory;
    for (size_t i = 0; i < num_blocks; ++i)
    {
        char* next_block = ((uint8_t*)current) + pool->block_size;
        *((size_t*)current) = i + 1;//存储下一块的索引
       
        current = next_block;
    }
}

/**
 * @brief 打包索引和版本号
 */
static inline void* pack_index_version(size_t index, size_t version, XFixedPool* pool) {
    uintptr_t index_part = index & pool->index_mask;
    // 移除版本号重置逻辑，利用无符号数自然溢出（永不重置）
    uintptr_t version_part = (version & pool->version_mask) << pool->index_bits;
    return (void*)(index_part | version_part);
}
/**
 * @brief 从打包值中解包出索引
 */
static inline size_t unpack_index(void* packed, XFixedPool* pool) {
    return (size_t)((uintptr_t)packed & pool->index_mask);
}
/**
 * @brief 从打包值中解包出版本号
 */
static inline size_t unpack_version(void* packed, XFixedPool* pool) {
    uintptr_t shifted = (uintptr_t)packed >> pool->index_bits;
    return (size_t)(shifted & pool->version_mask);
}

// ----------------------------------------------------------------------------
// 核心 API 实现
// ----------------------------------------------------------------------------

bool XFixedPool_init(XFixedPool* pool, void* memory, size_t total_bytes, size_t block_size) {
    if (!pool || !memory || total_bytes == 0 || block_size == 0) {
        return false;
    }

    // --- 关键修改: 计算包含隐藏指针的内部块大小 ---
    size_t internal_block_size = calculate_internal_block_size(block_size);

    size_t num_blocks = total_bytes / internal_block_size;
    if (num_blocks == 0) {
        return false;
    }
    pool->block_size = internal_block_size;
    pool->user_block_size = internal_block_size - sizeof(void*);
    pool->raw_memory = memory;
    pool->total_raw_size = total_bytes;
    pool->num_blocks = num_blocks;
    pool->owns_memory = false; // 静态模式下，用户拥有所有权


    // --- 关键修改: 计算索引和版本号位数 ---
    pool->index_bits = calculate_bits_needed_for_max(num_blocks - 1);
    pool->index_mask = ((uintptr_t)1 << pool->index_bits) - 1;
    pool->version_mask = ((uintptr_t)1 << (sizeof(void*) * 8 - pool->index_bits)) - 1;

    // 安全检查：确保有足够的版本号位
    if ((sizeof(void*) * 8 - pool->index_bits) < 16) {
        return false; // ABA风险过高
    }

    initialize_free_list(pool);

    // --- 初始化打包的头指针 (初始索引为0) ---
    void* initial_packed = pack_index_version(0, 0, pool);
    XAtomic_init(pool->free_list_head_packed, initial_packed);
    return true;
}

void XFixedPool_deinit(XFixedPool* pool) {
    if (pool) {
        memset(pool, 0, sizeof(XFixedPool));
    }
}

void* XFixedPool_malloc(XFixedPool* pool) {
    if (!pool) return NULL;

    void* old_head_packed=NULL;
    void* new_head_packed = NULL;
    void* old_head_block = NULL;
    size_t old_head_index=0,new_head_index=0;

    do {
        // 1. 读取当前头
        old_head_packed = XAtomic_load_ptr(&pool->free_list_head_packed, XAtomic_MemoryOrder_Relaxed);
        old_head_index = unpack_index(old_head_packed, pool);

        // 2. 检查是否为空
        if (old_head_index >= pool->num_blocks) 
        {
            return NULL;
        }

        // 3. 获取旧头块的地址
        old_head_block = get_block_by_index(pool, old_head_index);

        new_head_index = *(volatile size_t*)old_head_block; // 在无锁算法中，非原子读取 next 是安全的，因为 CAS 会验证整个操作
        // 5. 打包新头
        size_t old_version = unpack_version(old_head_packed, pool);
        new_head_packed = pack_index_version(new_head_index, old_version + 1, pool);

        // 6. 尝试替换头
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head_packed, &old_head_packed, new_head_packed, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));

    // --- 关键修改: 标记为已分配 ---
    *(volatile size_t*)old_head_block = (size_t)XFIXEDPOOL_BLOCK_ALLOCATED;
    //printf("XFixedPool malloc pool:%p block_size:%d num_blocks:%d index :%d ptr:%p\n", pool->raw_memory, pool->block_size,pool->num_blocks, old_head_index, get_user_data_ptr(old_head_block));

    return get_user_data_ptr(old_head_block);
}

void XFixedPool_free(XFixedPool* pool, void* user_ptr) {
    if (!pool || !user_ptr) return;
    if (!XFixedPool_is_from_pool(pool, user_ptr))return;
    // 1. 从用户指针恢复内部块地址
    void* internal_block = get_internal_block_ptr(user_ptr);
    int64_t freed_index = ((char*)internal_block - (char*)pool->raw_memory) / pool->block_size;
    if (freed_index<0||freed_index >= pool->num_blocks) {
        return; // Invalid pointer
    }

    // 【增强健壮性】检查是否是双重释放
    if (*(volatile size_t*)internal_block != (size_t)XFIXEDPOOL_BLOCK_ALLOCATED) {
        return;
    }
 
    void* old_head_packed;
    void* new_head_packed;
    do {
        // a. 读取当前头 (index + version)
        old_head_packed = XAtomic_load_ptr(&pool->free_list_head_packed, XAtomic_MemoryOrder_Relaxed);
        size_t old_head_index = unpack_index(old_head_packed, pool);

        *(volatile size_t*)internal_block = old_head_index;

        // c. 打包新头: 被释放的块成为新的头
        size_t old_version = unpack_version(old_head_packed, pool);
        new_head_packed = pack_index_version(freed_index, old_version + 1, pool);

        // d. 尝试原子地更新头指针
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head_packed, &old_head_packed, new_head_packed, XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed));
    //printf("XFixedPool free pool:%p   index:%lld ptr:%p\n", pool->raw_memory, freed_index, user_ptr);
}

bool XFixedPool_is_from_pool(const XFixedPool* pool, const void* ptr)
{
    if (!pool || !ptr) {
        return false;
    }

    // 1. 从用户指针恢复内部块地址
    const char* internal_block = (const char*)ptr - sizeof(void*);

    // 2. 获取内存池数据区的起始和结束地址
    const char* pool_start = (const char*)pool->raw_memory;
    const char* pool_end = pool_start + (pool->block_size * pool->num_blocks);

    // 3. 检查内部块地址是否在池的有效范围内
    if (internal_block < pool_start || internal_block >= pool_end) {
        return false;
    }

    // 4. 计算该地址相对于池起始位置的偏移量
    uintptr_t offset = (uintptr_t)(internal_block - pool_start);

    // 5. 检查偏移量是否能被块大小整除（即是否对齐到块边界）
    if (offset % pool->block_size != 0) {
        return false;
    }

    // 6. （可选但推荐）额外检查：确保该块当前处于已分配状态
    //    在您的实现中，已分配的块其第一个 sizeof(size_t) 字节会被设为 XFIXEDPOOL_BLOCK_ALLOCATED。
    //    这可以防止函数将一个已经归还但尚未被再次分配的块误判为“有效”。
    if (*(const volatile size_t*)internal_block != (size_t)XFIXEDPOOL_BLOCK_ALLOCATED) {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// 动态模式 API 实现
// ----------------------------------------------------------------------------

XFixedPool* XFixedPool_create_from_memory(void* memory, size_t total_bytes, size_t block_size) {
    if (!memory || total_bytes == 0 || block_size == 0) {
        return NULL;
    }
    XFixedPool* pool = (XFixedPool*)XMalloc(sizeof(XFixedPool));
    if (!pool) {
        return NULL;
    }
    if (!XFixedPool_init(pool, memory, total_bytes, block_size))
    {
        XFree(pool);
        return NULL;
    }
    return pool;
}

XFixedPool* XFixedPool_create(size_t block_size, size_t num_blocks) {
    if (block_size == 0 || num_blocks == 0) {
        return NULL;
    }

    size_t internal_block_size = calculate_internal_block_size(block_size);
    size_t total_bytes = internal_block_size * num_blocks;

    void* raw_memory = XMemory_malloc(total_bytes);
    if (!raw_memory) {
        return NULL;
    }
    memset(raw_memory,0, total_bytes);
    XFixedPool* pool = XFixedPool_create_from_memory(raw_memory, total_bytes, block_size);
    if (pool) {
        pool->owns_memory = true; // 标记为完全拥有
    }
    else {
        XFree(raw_memory);
    }

    return pool;
}

void XFixedPool_delete(XFixedPool* pool) {
    if (!pool) {
        return;
    }

    // 根据 owns_memory 标志决定是否释放数据缓冲区
    if (pool->owns_memory) {
        XFree(pool->raw_memory);
    }
    // 无论如何都要释放 pool 结构体本身
    XFree(pool);
}