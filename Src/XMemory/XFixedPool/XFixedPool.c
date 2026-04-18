#include "XFixedPool.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>
#ifndef ALIGN_UP
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#endif
/**
 * @brief 计算用户请求的块大小对应的内部块大小
 *
 * 内部块 = 隐藏的 next 指针 + 用户数据区 + 可能的填充（为了对齐）
 */
static size_t calculate_internal_block_size(size_t user_block_size) {
    // 确保用户数据区至少为 sizeof(void*)，以便能安全地存储指针
    size_t effective_user_size = (user_block_size < sizeof(void*)) ? sizeof(void*) : user_block_size;

    // 内部块大小 = next指针 + 对齐后的用户数据区
    // 我们需要确保整个内部块是 XFIXEDPOOL_ALIGN 对齐的
    size_t internal_size = sizeof(void*) + effective_user_size;
    return ALIGN_UP(internal_size, sizeof(void*));
}

/**
 * @brief 获取用户可用数据的起始地址
 */
static inline void* get_user_data_ptr(void* internal_block) {
    return (char*)internal_block + sizeof(void*);
}

/**
 * @brief 从用户数据指针恢复内部块地址
 */
static inline void* get_internal_block_ptr(void* user_data) {
    return (char*)user_data - sizeof(void*);
}

/**
 * @brief 在单线程环境下初始化空闲链表
 *
 * 此函数构建的是内部块的链表。
 */
static void initialize_free_list(XFixedPool* pool) {
    char* memory = (char*)pool->raw_memory;
    const size_t num_blocks = pool->total_raw_size / pool->block_size;

    void* current = memory;
    for (size_t i = 0; i < num_blocks - 1; ++i) {
        char* next_block_addr = memory + (i + 1) * pool->block_size;
        // 在内部块的开头存储下一个内部块的地址
        *(void**)current = next_block_addr;
        current = next_block_addr;
    }
    *(void**)current = NULL; // 链表尾
    XAtomic_init(pool->free_list_head, memory);
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
    pool->total_raw_size = num_blocks * internal_block_size;
    pool->owns_memory = false; // 静态模式下，用户拥有所有权

    initialize_free_list(pool);
    return true;
}

void XFixedPool_deinit(XFixedPool* pool) {
    if (pool) {
        memset(pool, 0, sizeof(XFixedPool));
    }
}

void* XFixedPool_malloc(XFixedPool* pool) {
    if (!pool) {
        return NULL;
    }

    void* internal_block;
    void* new_head;

    do {
        internal_block = XAtomic_load_ptr(&pool->free_list_head);
        if (internal_block == NULL) {
            return NULL; // 池已空
        }
        // new_head 是下一个内部块的地址
        new_head = *(void**)internal_block;
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head, &internal_block, new_head));

    // --- 关键修改: 返回用户数据区的地址 ---
    return get_user_data_ptr(internal_block);
}

void XFixedPool_free(XFixedPool* pool, void* block) {
    if (!pool || !block) {
        return;
    }

    // --- 关键修改: 从用户指针恢复内部块指针 ---
    void* internal_block = get_internal_block_ptr(block);
    void* old_head;

    do {
        old_head = XAtomic_load_ptr(&pool->free_list_head);
        // 将内部块的 next 指针指向当前的空闲链表头
        *(void**)internal_block = old_head;
    } while (!XAtomic_compare_exchange_strong_ptr(&pool->free_list_head, &old_head, internal_block));
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