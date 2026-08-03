#include "XVariablePool.h"
#include "XMemory.h"
#include <string.h>

#define XVARIABLEPOOL_BLOCK_ALLOCATED ((size_t)1u)
#define XVARIABLEPOOL_BLOCK_SIZE_MASK (~(size_t)1u)
#define XVARIABLEPOOL_BLOCK_MAGIC UINT32_C(0x58565042)
#define XVARIABLEPOOL_SMALL_BLOCK_SIZE ((size_t)1u << XVARIABLEPOOL_SLI_BITS)

struct XVariablePoolBlock
{
    size_t size_flags;             /* 内部块总大小，最低位表示已分配。 */
    size_t previous_size;          /* 前一个物理块的内部总大小。 */
    uint32_t magic;                /* 用于识别本池块并拦截明显的非法指针。 */
    uint32_t reserved;             /* 保持结构布局稳定并满足常见对齐要求。 */
};

typedef struct XVariablePoolFreeLinks
{
    XVariablePoolBlock* previous;
    XVariablePoolBlock* next;
} XVariablePoolFreeLinks;

static bool XVariablePool_normalize_alignment(size_t requested, size_t* alignment)
{
    size_t value = requested ? requested : XVARIABLEPOOL_DEFAULT_ALIGNMENT;
    if (requested != 0u && (requested & (requested - 1u)) != 0u)
        return false;
    if (value < sizeof(void*))
        value = sizeof(void*);
    if ((value & (value - 1u)) != 0)
        return false;
    *alignment = value;
    return true;
}

static bool XVariablePool_align_up_size(size_t value, size_t alignment, size_t* result)
{
    size_t remainder = value & (alignment - 1u);
    size_t padding = remainder ? alignment - remainder : 0;
    if (value > SIZE_MAX - padding)
        return false;
    *result = value + padding;
    return true;
}

static uintptr_t XVariablePool_align_up_address(uintptr_t value, size_t alignment)
{
    uintptr_t mask = (uintptr_t)(alignment - 1u);
    return (value + mask) & ~mask;
}

static uintptr_t XVariablePool_align_down_address(uintptr_t value, size_t alignment)
{
    return value & ~(uintptr_t)(alignment - 1u);
}

static inline size_t XVariablePool_block_size(const XVariablePoolBlock* block)
{
    return block->size_flags & XVARIABLEPOOL_BLOCK_SIZE_MASK;
}

static inline bool XVariablePool_block_is_allocated(const XVariablePoolBlock* block)
{
    return (block->size_flags & XVARIABLEPOOL_BLOCK_ALLOCATED) != 0;
}

static inline XVariablePoolFreeLinks* XVariablePool_free_links(const XVariablePool* pool,
                                                                XVariablePoolBlock* block)
{
    return (XVariablePoolFreeLinks*)((uint8_t*)block + pool->header_size);
}

static inline uintptr_t XVariablePool_begin_address(const XVariablePool* pool)
{
    return (uintptr_t)pool->managed_memory;
}

static inline uintptr_t XVariablePool_end_address(const XVariablePool* pool)
{
    return XVariablePool_begin_address(pool) + pool->total_managed_size;
}

static unsigned XVariablePool_floor_log2(size_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    if (sizeof(size_t) == sizeof(unsigned long))
        return (unsigned)(sizeof(size_t) * 8u - 1u - __builtin_clzl((unsigned long)value));
#endif
    {
        unsigned result = 0;
        while (value > 1u) {
            value >>= 1u;
            result++;
        }
        return result;
    }
}

static unsigned XVariablePool_first_bit(size_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    if (sizeof(size_t) == sizeof(unsigned long))
        return (unsigned)__builtin_ctzl((unsigned long)value);
#endif
    {
        unsigned result = 0;
        while ((value & (size_t)1u) == 0u) {
            value >>= 1u;
            result++;
        }
        return result;
    }
}

static void XVariablePool_mapping_insert(size_t size, unsigned* fl, unsigned* sl)
{
    if (size < XVARIABLEPOOL_SMALL_BLOCK_SIZE) {
        *fl = 0;
        *sl = (unsigned)size;
        if (*sl >= XVARIABLEPOOL_SLI_COUNT)
            *sl = (unsigned)XVARIABLEPOOL_SLI_COUNT - 1u;
        return;
    }

    *fl = XVariablePool_floor_log2(size);
    {
        unsigned shift = *fl - XVARIABLEPOOL_SLI_BITS;
        size_t scaled = size >> shift;
        if (scaled >= XVARIABLEPOOL_SLI_COUNT)
            *sl = (unsigned)(scaled - XVARIABLEPOOL_SLI_COUNT);
        else
            *sl = 0;
        if (*sl >= XVARIABLEPOOL_SLI_COUNT)
            *sl = (unsigned)XVARIABLEPOOL_SLI_COUNT - 1u;
    }
}

static void XVariablePool_mapping_search(size_t size, unsigned* fl, unsigned* sl)
{
    if (size < XVARIABLEPOOL_SMALL_BLOCK_SIZE) {
        *fl = 0;
        *sl = (unsigned)size;
        if (*sl >= XVARIABLEPOOL_SLI_COUNT)
            *sl = (unsigned)XVARIABLEPOOL_SLI_COUNT - 1u;
        return;
    }

    *fl = XVariablePool_floor_log2(size);
    {
        unsigned shift = *fl - XVARIABLEPOOL_SLI_BITS;
        size_t scaled = size >> shift;
        size_t remainder = 0;
        if (shift > 0u)
            remainder = size & (((size_t)1u << shift) - 1u);
        *sl = scaled >= XVARIABLEPOOL_SLI_COUNT
            ? (unsigned)(scaled - XVARIABLEPOOL_SLI_COUNT)
            : 0u;
        if (remainder != 0u)
            (*sl)++;
        if (*sl >= XVARIABLEPOOL_SLI_COUNT) {
            if (*fl + 1u < XVARIABLEPOOL_FL_COUNT) {
                (*fl)++;
                *sl = 0;
            }
            else {
                *sl = (unsigned)XVARIABLEPOOL_SLI_COUNT - 1u;
            }
        }
    }
}

static void XVariablePool_set_bin_bits(XVariablePool* pool, unsigned fl, unsigned sl)
{
    pool->fl_bitmap |= ((size_t)1u << fl);
    pool->sl_bitmap[fl] |= ((size_t)1u << sl);
}

static void XVariablePool_clear_bin_bits(XVariablePool* pool, unsigned fl, unsigned sl)
{
    if (pool->sl_bitmap[fl] == 0u)
        return;
    pool->sl_bitmap[fl] &= ~((size_t)1u << sl);
    if (pool->sl_bitmap[fl] == 0u)
        pool->fl_bitmap &= ~((size_t)1u << fl);
}

static void XVariablePool_insert_free_block(XVariablePool* pool, XVariablePoolBlock* block)
{
    unsigned fl;
    unsigned sl;
    XVariablePoolFreeLinks* links;
    XVariablePool_mapping_insert(XVariablePool_block_size(block), &fl, &sl);
    links = XVariablePool_free_links(pool, block);
    links->previous = NULL;
    links->next = pool->free_lists[fl][sl];
    if (links->next)
        XVariablePool_free_links(pool, links->next)->previous = block;
    pool->free_lists[fl][sl] = block;
    XVariablePool_set_bin_bits(pool, fl, sl);
    pool->free_block_count++;
}

static void XVariablePool_remove_free_block(XVariablePool* pool, XVariablePoolBlock* block)
{
    unsigned fl;
    unsigned sl;
    XVariablePoolFreeLinks* links;
    XVariablePool_mapping_insert(XVariablePool_block_size(block), &fl, &sl);
    links = XVariablePool_free_links(pool, block);
    if (links->previous)
        XVariablePool_free_links(pool, links->previous)->next = links->next;
    else
        pool->free_lists[fl][sl] = links->next;
    if (links->next)
        XVariablePool_free_links(pool, links->next)->previous = links->previous;
    if (!pool->free_lists[fl][sl])
        XVariablePool_clear_bin_bits(pool, fl, sl);
    links->previous = NULL;
    links->next = NULL;
    if (pool->free_block_count > 0u)
        pool->free_block_count--;
}

static bool XVariablePool_block_is_valid(const XVariablePool* pool,
                                         const XVariablePoolBlock* block,
                                         bool require_allocated)
{
    uintptr_t begin;
    uintptr_t end;
    uintptr_t address;
    size_t block_size;

    if (!pool || !pool->initialized || !block)
        return false;
    begin = XVariablePool_begin_address(pool);
    end = XVariablePool_end_address(pool);
    address = (uintptr_t)block;
    if (address < begin || address > end - pool->header_size)
        return false;
    if (((address - begin) & (pool->alignment - 1u)) != 0u)
        return false;
    if (block->magic != XVARIABLEPOOL_BLOCK_MAGIC)
        return false;
    block_size = XVariablePool_block_size(block);
    if (block_size < pool->header_size ||
        (block_size & (pool->alignment - 1u)) != 0u ||
        block_size > end - address)
        return false;
    if (require_allocated && !XVariablePool_block_is_allocated(block))
        return false;
    return true;
}

static XVariablePoolBlock* XVariablePool_block_from_user(const XVariablePool* pool,
                                                          const void* ptr,
                                                          bool require_allocated)
{
    uintptr_t begin;
    uintptr_t end;
    uintptr_t user_address;
    uintptr_t block_address;
    XVariablePoolBlock* block;

    if (!pool || !pool->initialized || !ptr)
        return NULL;
    begin = XVariablePool_begin_address(pool);
    end = XVariablePool_end_address(pool);
    user_address = (uintptr_t)ptr;
    if (user_address < begin + pool->header_size || user_address >= end)
        return NULL;
    if (((user_address - begin - pool->header_size) & (pool->alignment - 1u)) != 0u)
        return NULL;
    block_address = user_address - pool->header_size;
    block = (XVariablePoolBlock*)block_address;
    if ((uintptr_t)block + pool->header_size > end)
        return NULL;
    if (!XVariablePool_block_is_valid(pool, block, require_allocated))
        return NULL;
    return block;
}

static XVariablePoolBlock* XVariablePool_next_block(const XVariablePool* pool,
                                                     const XVariablePoolBlock* block)
{
    (void)pool;
    return (XVariablePoolBlock*)((uintptr_t)block + XVariablePool_block_size(block));
}

static XVariablePoolBlock* XVariablePool_previous_block(const XVariablePool* pool,
                                                         const XVariablePoolBlock* block)
{
    if (block->previous_size == 0u || block->previous_size > (uintptr_t)block - XVariablePool_begin_address(pool))
        return NULL;
    return (XVariablePoolBlock*)((uintptr_t)block - block->previous_size);
}

static size_t XVariablePool_block_user_size(const XVariablePool* pool,
                                            const XVariablePoolBlock* block)
{
    size_t block_size = XVariablePool_block_size(block);
    return block_size >= pool->header_size ? block_size - pool->header_size : 0u;
}

static XVariablePoolBlock* XVariablePool_find_suitable_block(XVariablePool* pool, size_t requested_size)
{
    unsigned fl;
    unsigned sl;
    size_t sl_map;
    size_t fl_map;
    XVariablePoolBlock* block;

    XVariablePool_mapping_search(requested_size, &fl, &sl);
    sl_map = pool->sl_bitmap[fl] & (~(size_t)0u << sl);
    while (sl_map != 0u) {
        unsigned candidate_sl = XVariablePool_first_bit(sl_map);
        block = pool->free_lists[fl][candidate_sl];
        while (block) {
            if (XVariablePool_block_size(block) >= requested_size)
                return block;
            block = XVariablePool_free_links(pool, block)->next;
        }
        sl_map &= sl_map - 1u;
    }

    if (fl >= XVARIABLEPOOL_FL_COUNT - 1u)
        return NULL;
    fl_map = pool->fl_bitmap & ~(((size_t)1u << (fl + 1u)) - 1u);
    if (fl_map == 0u)
        return NULL;
    fl = XVariablePool_first_bit(fl_map);
    sl_map = pool->sl_bitmap[fl];
    if (sl_map == 0u)
        return NULL;
    while (sl_map != 0u) {
        sl = XVariablePool_first_bit(sl_map);
        block = pool->free_lists[fl][sl];
        while (block) {
            if (XVariablePool_block_size(block) >= requested_size)
                return block;
            block = XVariablePool_free_links(pool, block)->next;
        }
        sl_map &= sl_map - 1u;
    }
    return NULL;
}

static bool XVariablePool_normalize_block_size(const XVariablePool* pool,
                                               size_t user_size,
                                               size_t* block_size)
{
    size_t required;
    if (user_size > SIZE_MAX - pool->header_size)
        return false;
    required = user_size + pool->header_size;
    if (!XVariablePool_align_up_size(required, pool->alignment, &required))
        return false;
    if (required < pool->minimum_block_size)
        required = pool->minimum_block_size;
    *block_size = required;
    return true;
}

static void XVariablePool_lock_pool(XVariablePool* pool)
{
    if (pool && pool->lock)
        pool->lock(pool->lock_context);
}

static void XVariablePool_unlock_pool(XVariablePool* pool)
{
    if (pool && pool->unlock)
        pool->unlock(pool->lock_context);
}

bool XVariablePool_init(XVariablePool* pool, void* memory, size_t total_bytes, size_t alignment)
{
    return XVariablePool_init_ex(pool, memory, total_bytes, alignment, NULL, NULL, NULL);
}

bool XVariablePool_init_ex(XVariablePool* pool,
                           void* memory,
                           size_t total_bytes,
                           size_t alignment,
                           XVariablePool_LockMethod lock,
                           XVariablePool_LockMethod unlock,
                           void* lock_context)
{
    size_t normalized_alignment;
    size_t header_size;
    size_t minimum_block_size;
    uintptr_t raw_begin;
    uintptr_t raw_end;
    uintptr_t managed_begin;
    uintptr_t managed_end;
    size_t managed_size;
    size_t first_block_size;
    XVariablePoolBlock* first_block;
    XVariablePoolBlock* sentinel;

    if (!pool)
        return false;
    memset(pool, 0, sizeof(XVariablePool));
    if (!memory || total_bytes == 0u || (lock == NULL) != (unlock == NULL))
        return false;
    if (!XVariablePool_normalize_alignment(alignment, &normalized_alignment))
        return false;
    if (!XVariablePool_align_up_size(sizeof(XVariablePoolBlock), normalized_alignment, &header_size))
        return false;
    if (header_size > SIZE_MAX - sizeof(XVariablePoolFreeLinks))
        return false;
    if (!XVariablePool_align_up_size(header_size + sizeof(XVariablePoolFreeLinks),
                                     normalized_alignment, &minimum_block_size))
        return false;

    raw_begin = (uintptr_t)memory;
    if ((uintptr_t)total_bytes > UINTPTR_MAX - raw_begin)
        return false;
    raw_end = raw_begin + (uintptr_t)total_bytes;
    managed_begin = XVariablePool_align_up_address(raw_begin, normalized_alignment);
    if (managed_begin < raw_begin || managed_begin > raw_end)
        return false;
    managed_end = XVariablePool_align_down_address(raw_end, normalized_alignment);
    if (managed_end <= managed_begin)
        return false;
    managed_size = (size_t)(managed_end - managed_begin);
    if (managed_size <= header_size)
        return false;
    first_block_size = managed_size - header_size;
    if (first_block_size < minimum_block_size)
        return false;

    pool->raw_memory = memory;
    pool->managed_memory = (void*)managed_begin;
    pool->total_raw_size = total_bytes;
    pool->total_managed_size = managed_size;
    pool->alignment = normalized_alignment;
    pool->header_size = header_size;
    pool->minimum_block_size = minimum_block_size;
    pool->lock = lock;
    pool->unlock = unlock;
    pool->lock_context = lock_context;
    pool->owns_memory = false;
    pool->initialized = true;

    first_block = (XVariablePoolBlock*)managed_begin;
    first_block->size_flags = first_block_size;
    first_block->previous_size = 0u;
    first_block->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    first_block->reserved = 0u;

    sentinel = (XVariablePoolBlock*)(managed_begin + first_block_size);
    sentinel->size_flags = header_size | XVARIABLEPOOL_BLOCK_ALLOCATED;
    sentinel->previous_size = first_block_size;
    sentinel->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    sentinel->reserved = 0u;

    pool->total_user_size = first_block_size - header_size;
    pool->free_user_size = pool->total_user_size;
    XVariablePool_insert_free_block(pool, first_block);
    return true;
}

XVariablePool* XVariablePool_create(size_t total_bytes, size_t alignment)
{
    XVariablePool* pool;
    void* memory;
    size_t normalized_alignment;
    size_t raw_size;

    if (total_bytes == 0u || !XVariablePool_normalize_alignment(alignment, &normalized_alignment))
        return NULL;
    if (total_bytes > SIZE_MAX - (normalized_alignment - 1u))
        return NULL;
    raw_size = total_bytes + normalized_alignment - 1u;
    pool = (XVariablePool*)XMalloc_System(sizeof(XVariablePool));
    if (!pool)
        return NULL;
    memory = XMalloc_System(raw_size);
    if (!memory) {
        XFree_System(pool);
        return NULL;
    }
    if (!XVariablePool_init(pool, memory, raw_size, normalized_alignment)) {
        XFree_System(memory);
        XFree_System(pool);
        return NULL;
    }
    pool->owns_memory = true;
    return pool;
}

XVariablePool* XVariablePool_create_from_memory(void* memory, size_t total_bytes, size_t alignment)
{
    XVariablePool* pool;
    if (!memory || total_bytes == 0u)
        return NULL;
    pool = (XVariablePool*)XMalloc_System(sizeof(XVariablePool));
    if (!pool)
        return NULL;
    if (!XVariablePool_init(pool, memory, total_bytes, alignment)) {
        XFree_System(pool);
        return NULL;
    }
    return pool;
}

bool XVariablePool_setLockMethod(XVariablePool* pool,
                                 XVariablePool_LockMethod lock,
                                 XVariablePool_LockMethod unlock,
                                 void* lock_context)
{
    if (!pool || !pool->initialized || (lock == NULL) != (unlock == NULL))
        return false;
    if (pool->allocation_count != 0u)
        return false;
    pool->lock = lock;
    pool->unlock = unlock;
    pool->lock_context = lock_context;
    return true;
}

void XVariablePool_deinit(XVariablePool* pool)
{
    if (pool)
        memset(pool, 0, sizeof(XVariablePool));
}

void XVariablePool_delete(XVariablePool* pool)
{
    void* raw_memory;
    bool owns_memory;
    if (!pool)
        return;
    raw_memory = pool->raw_memory;
    owns_memory = pool->owns_memory;
    XVariablePool_deinit(pool);
    if (owns_memory)
        XFree_System(raw_memory);
    XFree_System(pool);
}

void* XVariablePool_malloc(XVariablePool* pool, size_t size)
{
    size_t requested_block_size;
    size_t old_block_size;
    size_t old_user_size;
    size_t allocated_user_size;
    XVariablePoolBlock* block;
    XVariablePoolBlock* remainder;
    XVariablePoolBlock* next;

    if (!pool || !pool->initialized || size == 0u)
        return NULL;
    if (!XVariablePool_normalize_block_size(pool, size, &requested_block_size))
        return NULL;

    XVariablePool_lock_pool(pool);
    block = XVariablePool_find_suitable_block(pool, requested_block_size);
    if (!block) {
        XVariablePool_unlock_pool(pool);
        return NULL;
    }
    old_block_size = XVariablePool_block_size(block);
    old_user_size = old_block_size - pool->header_size;
    XVariablePool_remove_free_block(pool, block);
    remainder = NULL;
    if (old_block_size - requested_block_size >= pool->minimum_block_size) {
        remainder = (XVariablePoolBlock*)((uintptr_t)block + requested_block_size);
        remainder->size_flags = old_block_size - requested_block_size;
        remainder->previous_size = requested_block_size;
        remainder->magic = XVARIABLEPOOL_BLOCK_MAGIC;
        remainder->reserved = 0u;
        block->size_flags = requested_block_size | XVARIABLEPOOL_BLOCK_ALLOCATED;
        next = XVariablePool_next_block(pool, remainder);
        next->previous_size = XVariablePool_block_size(remainder);
        XVariablePool_insert_free_block(pool, remainder);
        pool->free_user_size += XVariablePool_block_user_size(pool, remainder);
    }
    else {
        block->size_flags = old_block_size | XVARIABLEPOOL_BLOCK_ALLOCATED;
        next = XVariablePool_next_block(pool, block);
        next->previous_size = old_block_size;
    }
    block->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    allocated_user_size = XVariablePool_block_user_size(pool, block);
    pool->free_user_size -= old_user_size;
    pool->allocated_user_size += allocated_user_size;
    pool->allocation_count++;
    XVariablePool_unlock_pool(pool);
    return (void*)((uint8_t*)block + pool->header_size);
}

void* XVariablePool_calloc(XVariablePool* pool, size_t count, size_t size)
{
    size_t total;
    void* memory;
    if (!pool || count == 0u || size == 0u || count > SIZE_MAX / size)
        return NULL;
    total = count * size;
    memory = XVariablePool_malloc(pool, total);
    if (memory)
        memset(memory, 0, total);
    return memory;
}

static bool XVariablePool_shrink_locked(XVariablePool* pool,
                                        XVariablePoolBlock* block,
                                        size_t new_block_size)
{
    size_t old_block_size = XVariablePool_block_size(block);
    size_t old_user_size = XVariablePool_block_user_size(pool, block);
    size_t remainder_size = old_block_size - new_block_size;
    XVariablePoolBlock* remainder;
    XVariablePoolBlock* next;

    if (remainder_size < pool->minimum_block_size)
        return false;
    remainder = (XVariablePoolBlock*)((uintptr_t)block + new_block_size);
    remainder->size_flags = remainder_size;
    remainder->previous_size = new_block_size;
    remainder->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    remainder->reserved = 0u;
    block->size_flags = new_block_size | XVARIABLEPOOL_BLOCK_ALLOCATED;
    next = XVariablePool_next_block(pool, remainder);
    if (!XVariablePool_block_is_allocated(next)) {
        size_t next_size = XVariablePool_block_size(next);
        size_t next_user_size = XVariablePool_block_user_size(pool, next);
        XVariablePool_remove_free_block(pool, next);
        remainder_size += next_size;
        pool->free_user_size -= next_user_size;
    }
    remainder->size_flags = remainder_size;
    next = XVariablePool_next_block(pool, remainder);
    next->previous_size = remainder_size;
    XVariablePool_insert_free_block(pool, remainder);
    pool->free_user_size += XVariablePool_block_user_size(pool, remainder);
    pool->allocated_user_size -= old_user_size;
    pool->allocated_user_size += XVariablePool_block_user_size(pool, block);
    return true;
}

static bool XVariablePool_grow_locked(XVariablePool* pool,
                                      XVariablePoolBlock* block,
                                      size_t new_block_size)
{
    size_t old_block_size = XVariablePool_block_size(block);
    size_t old_user_size = XVariablePool_block_user_size(pool, block);
    XVariablePoolBlock* next = XVariablePool_next_block(pool, block);
    size_t next_size;
    size_t combined_size;
    size_t actual_size;
    XVariablePoolBlock* remainder;
    XVariablePoolBlock* after;

    if (XVariablePool_block_is_allocated(next))
        return false;
    next_size = XVariablePool_block_size(next);
    combined_size = old_block_size + next_size;
    if (combined_size < new_block_size)
        return false;

    XVariablePool_remove_free_block(pool, next);
    pool->free_user_size -= XVariablePool_block_user_size(pool, next);
    actual_size = combined_size;
    if (combined_size - new_block_size >= pool->minimum_block_size) {
        actual_size = new_block_size;
        remainder = (XVariablePoolBlock*)((uintptr_t)block + actual_size);
        remainder->size_flags = combined_size - actual_size;
        remainder->previous_size = actual_size;
        remainder->magic = XVARIABLEPOOL_BLOCK_MAGIC;
        remainder->reserved = 0u;
        XVariablePool_insert_free_block(pool, remainder);
        pool->free_user_size += XVariablePool_block_user_size(pool, remainder);
        after = XVariablePool_next_block(pool, remainder);
        after->previous_size = XVariablePool_block_size(remainder);
    }
    else {
        after = (XVariablePoolBlock*)((uintptr_t)block + actual_size);
        after->previous_size = actual_size;
    }
    block->size_flags = actual_size | XVARIABLEPOOL_BLOCK_ALLOCATED;
    pool->allocated_user_size -= old_user_size;
    pool->allocated_user_size += XVariablePool_block_user_size(pool, block);
    return true;
}

void* XVariablePool_realloc(XVariablePool* pool, void* ptr, size_t new_size)
{
    XVariablePoolBlock* block;
    size_t requested_block_size;
    size_t old_user_size;
    void* new_memory;

    if (!pool || !pool->initialized)
        return NULL;
    if (!ptr)
        return new_size ? XVariablePool_malloc(pool, new_size) : NULL;
    if (new_size == 0u) {
        XVariablePool_free(pool, ptr);
        return NULL;
    }
    if (!XVariablePool_normalize_block_size(pool, new_size, &requested_block_size))
        return NULL;

    XVariablePool_lock_pool(pool);
    block = XVariablePool_block_from_user(pool, ptr, true);
    if (!block) {
        XVariablePool_unlock_pool(pool);
        return NULL;
    }
    old_user_size = XVariablePool_block_user_size(pool, block);
    if (requested_block_size <= XVariablePool_block_size(block)) {
        XVariablePool_shrink_locked(pool, block, requested_block_size);
        XVariablePool_unlock_pool(pool);
        return ptr;
    }
    if (XVariablePool_grow_locked(pool, block, requested_block_size)) {
        XVariablePool_unlock_pool(pool);
        return ptr;
    }
    XVariablePool_unlock_pool(pool);

    new_memory = XVariablePool_malloc(pool, new_size);
    if (!new_memory)
        return NULL;
    if (old_user_size < new_size)
        memcpy(new_memory, ptr, old_user_size);
    else
        memcpy(new_memory, ptr, new_size);
    XVariablePool_free(pool, ptr);
    return new_memory;
}

void XVariablePool_free(XVariablePool* pool, void* ptr)
{
    XVariablePoolBlock* block;
    XVariablePoolBlock* previous;
    XVariablePoolBlock* next;
    size_t original_size;
    size_t original_user_size;
    size_t merged_size;

    if (!pool || !pool->initialized || !ptr)
        return;
    XVariablePool_lock_pool(pool);
    block = XVariablePool_block_from_user(pool, ptr, true);
    if (!block) {
        XVariablePool_unlock_pool(pool);
        return;
    }
    original_size = XVariablePool_block_size(block);
    original_user_size = XVariablePool_block_user_size(pool, block);
    previous = XVariablePool_previous_block(pool, block);
    if (previous && (!XVariablePool_block_is_valid(pool, previous, false) ||
                     XVariablePool_block_is_allocated(previous)))
        previous = NULL;
    next = XVariablePool_next_block(pool, block);
    if (!XVariablePool_block_is_valid(pool, next, false) ||
        XVariablePool_block_is_allocated(next))
        next = NULL;

    if (previous) {
        size_t previous_size = XVariablePool_block_size(previous);
        size_t previous_user_size = XVariablePool_block_user_size(pool, previous);
        XVariablePool_remove_free_block(pool, previous);
        pool->free_user_size -= previous_user_size;
        merged_size = previous_size + original_size;
        pool->free_user_size += merged_size - pool->header_size;
        block = previous;
    }
    else {
        merged_size = original_size;
        pool->free_user_size += original_user_size;
    }

    block->size_flags = merged_size;
    block->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    if (next) {
        size_t next_size = XVariablePool_block_size(next);
        size_t next_user_size = XVariablePool_block_user_size(pool, next);
        size_t current_user_size = merged_size - pool->header_size;
        XVariablePool_remove_free_block(pool, next);
        pool->free_user_size -= next_user_size;
        merged_size += next_size;
        pool->free_user_size -= current_user_size;
        pool->free_user_size += merged_size - pool->header_size;
    }
    block->size_flags = merged_size;
    block->magic = XVARIABLEPOOL_BLOCK_MAGIC;
    next = XVariablePool_next_block(pool, block);
    next->previous_size = merged_size;
    XVariablePool_insert_free_block(pool, block);
    pool->allocated_user_size -= original_user_size;
    if (pool->allocation_count > 0u)
        pool->allocation_count--;
    XVariablePool_unlock_pool(pool);
}

bool XVariablePool_is_from_pool(const XVariablePool* pool, const void* ptr)
{
    return XVariablePool_block_from_user(pool, ptr, true) != NULL;
}

size_t XVariablePool_getMaxUserSize(const XVariablePool* pool, const void* ptr)
{
    XVariablePoolBlock* block = XVariablePool_block_from_user(pool, ptr, true);
    return block ? XVariablePool_block_user_size(pool, block) : 0u;
}

size_t XVariablePool_freeSize(const XVariablePool* pool)
{
    size_t value;
    if (!pool || !pool->initialized)
        return 0u;
    XVariablePool_lock_pool((XVariablePool*)pool);
    value = pool->free_user_size;
    XVariablePool_unlock_pool((XVariablePool*)pool);
    return value;
}

size_t XVariablePool_totalSize(const XVariablePool* pool)
{
    return pool && pool->initialized ? pool->total_user_size : 0u;
}

size_t XVariablePool_allocatedSize(const XVariablePool* pool)
{
    size_t value;
    if (!pool || !pool->initialized)
        return 0u;
    XVariablePool_lock_pool((XVariablePool*)pool);
    value = pool->allocated_user_size;
    XVariablePool_unlock_pool((XVariablePool*)pool);
    return value;
}

size_t XVariablePool_largestFreeSize(const XVariablePool* pool)
{
    uintptr_t address;
    uintptr_t end;
    size_t largest = 0u;
    if (!pool || !pool->initialized)
        return 0u;
    XVariablePool_lock_pool((XVariablePool*)pool);
    address = XVariablePool_begin_address(pool);
    end = XVariablePool_end_address(pool);
    while (address < end) {
        XVariablePoolBlock* block = (XVariablePoolBlock*)address;
        size_t block_size = XVariablePool_block_size(block);
        if (block_size == pool->header_size && address + block_size == end)
            break;
        if (!XVariablePool_block_is_allocated(block) &&
            XVariablePool_block_user_size(pool, block) > largest)
            largest = XVariablePool_block_user_size(pool, block);
        if (block_size == 0u || block_size > end - address)
            break;
        address += block_size;
    }
    XVariablePool_unlock_pool((XVariablePool*)pool);
    return largest;
}

size_t XVariablePool_freeBlockCount(const XVariablePool* pool)
{
    size_t value;
    if (!pool || !pool->initialized)
        return 0u;
    XVariablePool_lock_pool((XVariablePool*)pool);
    value = pool->free_block_count;
    XVariablePool_unlock_pool((XVariablePool*)pool);
    return value;
}

bool XVariablePool_check(const XVariablePool* pool)
{
    uintptr_t address;
    uintptr_t end;
    size_t expected_previous_size = 0u;
    size_t free_user_size = 0u;
    size_t allocated_user_size = 0u;
    size_t allocation_count = 0u;
    size_t free_block_count = 0u;
    size_t steps = 0u;
    bool valid = true;

    if (!pool || !pool->initialized)
        return false;
    XVariablePool_lock_pool((XVariablePool*)pool);
    address = XVariablePool_begin_address(pool);
    end = XVariablePool_end_address(pool);
    while (address < end && steps <= pool->total_managed_size / pool->alignment + 1u) {
        XVariablePoolBlock* block = (XVariablePoolBlock*)address;
        size_t block_size;
        bool allocated;
        steps++;
        if (block->magic != XVARIABLEPOOL_BLOCK_MAGIC) {
            valid = false;
            break;
        }
        block_size = XVariablePool_block_size(block);
        allocated = XVariablePool_block_is_allocated(block);
        if (block->previous_size != expected_previous_size ||
            block_size < pool->header_size ||
            (block_size & (pool->alignment - 1u)) != 0u ||
            block_size > end - address) {
            valid = false;
            break;
        }
        if (block_size == pool->header_size && allocated && address + block_size == end) {
            address = end;
            break;
        }
        if (block_size < pool->minimum_block_size) {
            valid = false;
            break;
        }
        if (allocated) {
            allocated_user_size += block_size - pool->header_size;
            allocation_count++;
        }
        else {
            free_user_size += block_size - pool->header_size;
            free_block_count++;
        }
        expected_previous_size = block_size;
        address += block_size;
    }
    if (address != end || steps > pool->total_managed_size / pool->alignment + 1u)
        valid = false;
    if (free_user_size != pool->free_user_size ||
        allocated_user_size != pool->allocated_user_size ||
        allocation_count != pool->allocation_count ||
        free_block_count != pool->free_block_count)
        valid = false;
    XVariablePool_unlock_pool((XVariablePool*)pool);
    return valid;
}
