#include "XFileDescriptor.h"
#include "XFixedPool.h"
#include <string.h>

/* ============================================================================
 * XFixedPool 状态
 * ============================================================================ */

XFIXEDPOOL_DEFINE(g_fdPool, sizeof(XFileDescriptor), XFD_TABLE_SIZE);
static bool g_fdInitDone = false;

/* fd → XFileDescriptor*：通过 XFixedPool 内部块计算
 * 内部块结构：[size_t 头部(存放next链表)][用户数据区]
 * 块间距 = pool->block_size (已对齐)
 * XFixedPool_malloc 返回用户数据区起始地址 (internal + sizeof(size_t))
 */
static inline XFileDescriptor* XFd_byIndex(int idx)
{
    char* internal = (char*)g_fdPool->raw_memory + idx * g_fdPool->block_size;
    return (XFileDescriptor*)(internal + sizeof(size_t));
}

/* 指针 → fd 索引：逆向计算
 * XFixedPool_malloc 返回的 desc 指向用户数据区
 * 内部块指针 = (char*)desc - sizeof(size_t)
 * 索引 = (内部块 - raw_memory) / block_size
 */
static inline int XFd_indexOf(XFileDescriptor* desc)
{
    char* internal = (char*)desc - sizeof(size_t);
    ptrdiff_t diff = internal - (char*)g_fdPool->raw_memory;
    return (int)(diff / g_fdPool->block_size);
}

/* ============================================================================
 * 公共 API
 * ============================================================================ */

void XFd_init(void)
{
    if (g_fdInitDone) return;
    XFIXEDPOOL_INIT(g_fdPool, sizeof(XFileDescriptor));
    /* XFixedPool_init 内部已通过 initialize_free_list 设置链表，
     * 用户数据区在 XFd_alloc 时 memset 清零 */
    g_fdInitDone = true;
}

intptr_t XFd_alloc(XFdType type, void* handle, void* ctx)
{
    if (!g_fdInitDone) XFd_init();
    if (!g_fdPool) return -1;

    /* XFixedPool_malloc 返回空闲块指针 → 指针算术算出索引即 fd */
    XFileDescriptor* desc = (XFileDescriptor*)XFixedPool_malloc(g_fdPool);
    if (!desc) return -1;

    memset(desc, 0, sizeof(XFileDescriptor));
    desc->handle = handle;
    desc->ctx = ctx;
    desc->type = type;
    desc->refCount = 1;

    return (intptr_t)XFd_indexOf(desc);
}

void XFd_free(intptr_t fd)
{
    if (fd < 0 || fd >= XFD_TABLE_SIZE) return;
    XFileDescriptor* desc = XFd_byIndex((int)fd);
    if ((XFdType)desc->type == XFD_TYPE_FREE) return; /* Double-free protection */
    memset(desc, 0, sizeof(XFileDescriptor));
    XFixedPool_free(g_fdPool, desc);
}

XFileDescriptor* XFd_get(intptr_t fd)
{
    if (fd < 0 || fd >= XFD_TABLE_SIZE) return NULL;
    XFileDescriptor* desc = XFd_byIndex((int)fd);
    return ((XFdType)desc->type != XFD_TYPE_FREE) ? desc : NULL;
}

void* XFd_handle(intptr_t fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? desc->handle : NULL;
}

XFdType XFd_type(intptr_t fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? (XFdType)desc->type : XFD_TYPE_FREE;
}

void* XFd_ctx(intptr_t fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? desc->ctx : NULL;
}
