#include "XFileDescriptor.h"
#include "XFixedPool.h"
#include <string.h>
#include <stddef.h>

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
    g_fdInitDone = true;
}

XFd XFd_alloc(XFdType type, void* handle, void* object)
{
    XFileDescriptor* desc;
    if (!g_fdInitDone) XFd_init();
    if (!g_fdPool) return XFD_INVALID;

    desc = (XFileDescriptor*)XFixedPool_malloc(g_fdPool);
    if (!desc) return XFD_INVALID;

    memset(desc, 0, sizeof(XFileDescriptor));
    desc->m_deviceCtx = handle;
    desc->object = object;
    desc->m_type = (uint8_t)type;
    return (XFd)XFd_indexOf(desc);
}

void XFd_free(XFd fd)
{
    XFileDescriptor* desc;
    if (fd < 0 || fd >= XFD_TABLE_SIZE) return;
    desc = XFd_byIndex((int)fd);
    if ((XFdType)desc->m_type == XFD_TYPE_FREE) return; /* Double-free protection */
    memset(desc, 0, sizeof(XFileDescriptor));
    XFixedPool_free(g_fdPool, desc);
}

XFileDescriptor* XFd_get(XFd fd)
{
    XFileDescriptor* desc;
    if (fd < 0 || fd >= XFD_TABLE_SIZE) return NULL;
    desc = XFd_byIndex((int)fd);
    return ((XFdType)desc->m_type != XFD_TYPE_FREE) ? desc : NULL;
}

void* XFd_handle(XFd fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? desc->m_deviceCtx : NULL;
}

XFdType XFd_type(XFd fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? (XFdType)desc->m_type : XFD_TYPE_FREE;
}

void* XFd_object(XFd fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    return desc ? desc->object : NULL;
}

void XFd_setObject(XFd fd, void* object)
{
    XFileDescriptor* desc = XFd_get(fd);
    if (desc) desc->object = object;
}
