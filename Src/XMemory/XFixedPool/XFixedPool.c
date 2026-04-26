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
    const size_t num_blocks = pool->total_raw_size / pool->block_size;

    // 使用 char* 进行指针算术更清晰
    char* current = memory;
    for (size_t i = 0; i < num_blocks - 1; ++i) {
        char* next_block = current + pool->block_size;
        // 在当前块的开头存储下一个块的地址
        // 假设 current 地址已按 sizeof(void*) 对齐
        *(void**)current = next_block;
       
        current = next_block;
    }
    // 链表尾
    *(void**)current = NULL;
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
    XSpinLock_init(&pool->lock);
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

    void* old_head_packed;
    void* new_head_packed;
    void* old_head_block;
    XSpinLock_lock(&pool->lock);
start:
    do {
        // 1. 读取当前头
        old_head_packed = XAtomic_load_ptr(&pool->free_list_head_packed);
        size_t old_head_index = unpack_index(old_head_packed, pool);

        // 2. 检查是否为空
        if (old_head_index >= pool->num_blocks) 
        {
            XSpinLock_unlock(&pool->lock);
            return NULL;
        }

        // 3. 获取旧头块的地址
        old_head_block = get_block_by_index(pool, old_head_index);

        // 4. 【关键】读取旧头块的 next 指针，以确定新头
        void* next_block_ptr = XAtomic_load_ptr(old_head_block);
        //void* next_block_ptr = *(void**)old_head_block;
        size_t new_head_index;
        if (next_block_ptr == NULL) 
        {
            new_head_index = pool->num_blocks; // Sentinel for empty
        }
        else if (next_block_ptr == XFIXEDPOOL_BLOCK_ALLOCATED)
        {//有线程抢先一步获得了这个内存块
            goto start;//重试
        }
        else 
        {
            new_head_index = ((char*)next_block_ptr - (char*)pool->raw_memory) / pool->block_size;
            if (new_head_index >= pool->num_blocks) 
            {
                XSpinLock_unlock(&pool->lock);
                return NULL;
            }
        }

        // 5. 打包新头
        size_t old_version = unpack_version(old_head_packed, pool);
        new_head_packed = pack_index_version(new_head_index, old_version + 1, pool);

        // 6. 尝试替换头
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head_packed, &old_head_packed, new_head_packed));

    // --- 关键修改: 标记为已分配 ---
    XAtomic_store_ptr(old_head_block, XFIXEDPOOL_BLOCK_ALLOCATED);
    //*(void**)old_head_block = XFIXEDPOOL_BLOCK_ALLOCATED;
     XSpinLock_unlock(&pool->lock);
    printf("XFixedPool malloc pool:%p  ptr:%p\n", pool->raw_memory, get_user_data_ptr(old_head_block));

    return get_user_data_ptr(old_head_block);
}

void XFixedPool_free(XFixedPool* pool, void* user_ptr) {
    if (!pool || !user_ptr) return;

    // 1. 从用户指针恢复内部块地址
    void* internal_block = get_internal_block_ptr(user_ptr);
    int64_t freed_index = ((char*)internal_block - (char*)pool->raw_memory) / pool->block_size;
    if (freed_index<0||freed_index >= pool->num_blocks) {
        return; // Invalid pointer
    }

    // --- 关键修改: 双重释放检查 ---
    void** next_ptr = (void**)internal_block;
    if (*next_ptr != XFIXEDPOOL_BLOCK_ALLOCATED) {
        // 如果 next_ptr 不是我们设置的 "已分配" 标记，那就是双重释放或无效指针！
        printf("XFixedPool_free: Double free or invalid pointer detected! "
            "Ptr: %p, Index: %zu, Current next_ptr: %p\n",
            user_ptr, freed_index, *next_ptr);
        return;
    }
 

    void* old_head_packed;
    void* new_head_packed;

    do {
        // a. 读取当前头 (index + version)
        old_head_packed = XAtomic_load_ptr(&pool->free_list_head_packed);
        size_t old_head_index = unpack_index(old_head_packed, pool);

        // b. 【关键】直接将 old_head_index (或其对应的sentinel) 写入被释放块的 next 字段
        //    不需要先把它转换成指针！我们可以直接存储索引，或者用一个统一的sentinel值。
        //    为了与 malloc 保持一致，我们在这里也使用指针语义。
        void* new_next_ptr;
        if (old_head_index >= pool->num_blocks) {
            new_next_ptr = NULL; // 链表为空
        }
        else {
            new_next_ptr = get_block_by_index(pool, old_head_index); // 转换为指针
        }
        *(void**)internal_block = new_next_ptr; // 这次写入是非原子的，但仅此线程会写这里

        // c. 打包新头: 被释放的块成为新的头
        size_t old_version = unpack_version(old_head_packed, pool);
        new_head_packed = pack_index_version(freed_index, old_version + 1, pool);

        // d. 尝试原子地更新头指针
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head_packed, &old_head_packed, new_head_packed));

    printf("XFixedPool free pool:%p   index:%lld ptr:%p\n", pool->raw_memory, freed_index, user_ptr);
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

    void* raw_memory = XMalloc(total_bytes);
    if (!raw_memory) {
        return NULL;
    }

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