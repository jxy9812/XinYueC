/**
 * @file pcre2_xin_memory.c
 * @brief PCRE2 与 XinYueC XMemory 的内存分配桥接实现。
 */

#include "pcre2_xin_memory.h"
#include "XMemory.h"

/* PCRE2 的 allocator callback 只能接收 malloc/free，因此不使用 calloc。
 * 使用 XMemory 的 SYSTEM 通道，避免 Hybrid 通道对大块系统内存做内存池回读。
 */
static void *pcre2_xin_malloc(size_t size, void *memory_data)
{
    (void)memory_data;
    return XMalloc_System(size);
}

static void pcre2_xin_free(void *block, void *memory_data)
{
    (void)memory_data;
    XFree_System(block);
}

pcre2_general_context *pcre2_xin_general_context_create(void)
{
    return pcre2_general_context_create(pcre2_xin_malloc, pcre2_xin_free, NULL);
}
