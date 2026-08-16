/**
 * @file XMySqlSharedMemory.c
 * @brief MySQL 共享内存本地传输的跨平台通用实现（MYSQL_PROTOCOL_MEMORY）。
 * @details
 * 本文件是 XMySqlSharedMemory.h 声明的唯一实现，位于共享代码
 * （Src/XCode/XSql），不包含任何平台头文件、不使用任何平台 API。
 * 所有跨进程能力都通过 XFileSystem 的共享内存原语访问：
 * - XFileSystem_openSharedMemory：按名打开/创建命名共享内存段，平台实现
 *   会为每个段内建一个信令通道（POSIX 为 Unix domain 流式套接字，
 *   Windows 为命名管道），返回的 XFd 可直接用 XFileSystem_read /
 *   XFileSystem_write 在信令通道上收发 1 字节通知；
 * - XFileSystem_map / XFileSystem_unmap：建立/解除共享内存视图；
 * - XFileSystem_close：释放共享内存段描述符。
 *
 * 跨进程收发同步完全复刻网络套接字的异步接收语义，不做共享内存状态字段
 * 轮询：数据方写完一块数据后向信令通道写入 1 个通知字节，对端在信令通道
 * 上阻塞等待（XFileSystem_read 进入内核等待，信号到达立即唤醒；POSIX 侧
 * 平台设置 SO_RCVTIMEO 用于支持整体超时，超时返回后检查截止时间而非
 * 忙轮询）。信令字节按取值区分用途：
 *   'D' 数据就绪（写入方写完一块）；'S' 空间可用（读取方消费完一块）；
 *   'C' 连接关闭（关闭方在释放前通知对端）。
 * 信令通道为全双工字节流，两个方向的通知交错到达，本实现用小型待处理
 * 缓冲暂存与当前等待路径无关的字节，保证读路径与写路径互不丢失通知。
 *
 * 连接握手（连接协商段 <BASE>_CONNECT_DATA）：
 *   客户端置 request=1 并发送 'D' → 服务端写连接编号并置 answer=1，
 *   发送 'D' → 客户端读取编号、复位 request=0 并发送 'S' →
 *   服务端复位 answer。数据段创建后服务端映射并写魔数，再发送 'D'
 *   通知客户端数据段就绪。
 *
 * 数据收发（数据段 <BASE>_<编号>_DATA，双通道全双工）：
 *   服务器→客户端通道：服务器写块后置 status=1 并发送 'D'，
 *   客户端读完后置 status=0 并发送 'S'；
 *   客户端→服务器通道：客户端写块后置 status=1 并发送 'D'，
 *   服务器读完后置 status=0 并发送 'S'。
 *   每条通道独立，互不阻塞，支持任意方向连续多块大包传输。
 */
#include "XMySqlSharedMemory.h"

#include "XFileSystem.h"
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"
#include "XThread.h"

#include <stdio.h>
#include <string.h>

/* 命名段名称后缀（服务器端与客户端必须使用同一组名称）。 */
#define XMYSQL_SHM_SEG_CONNECT "_CONNECT_DATA"
#define XMYSQL_SHM_SEG_DATA "_DATA"
#define XMYSQL_SHM_NAME_MAX 768

/* 通道状态取值（防御性标记，真正的同步由信令通道完成）。 */
#define XMYSQL_SHM_STATUS_IDLE 0u
#define XMYSQL_SHM_STATUS_READY 1u

/* 信令字节取值（通过 XFileSystem_read / XFileSystem_write 收发）。 */
#define XMYSQL_SHM_SIGNAL_DATA ((uint8_t)'D')  /* 数据就绪 */
#define XMYSQL_SHM_SIGNAL_SPACE ((uint8_t)'S') /* 空间可用 */
#define XMYSQL_SHM_SIGNAL_CLOSE ((uint8_t)'C') /* 连接关闭 */

/* 本端信令字节待处理缓冲容量：单线程请求-应答模型下最多积压少量字节。 */
#define XMYSQL_SHM_PENDING_CAPACITY 64u

/* 打开连接协商段时针对服务器尚未就绪的重试次数与间隔：
   服务器创建段先 shm_open 再 bind/listen，客户端可能在服务器创建前
   发起打开（ENOENT），按网络套接字 connect 语义在截止时间内有限重试。 */
#define XMYSQL_SHM_OPEN_RETRY 250
#define XMYSQL_SHM_OPEN_RETRY_WAIT_MS 20

struct XMySqlSharedMemory {
    XFd m_signalFd;                       /**< 数据段 XFd（句柄为信令通道） */
    XMySqlSharedMemorySegment* m_segment; /**< 数据段映射视图（全双工通道） */
    uint8_t m_pending[XMYSQL_SHM_PENDING_CAPACITY]; /**< 待路由信令字节缓冲 */
    size_t m_pendingCount;                /**< 缓冲内字节数 */
    bool m_writeGranted;                  /**< 本端写通道是否已获得写许可 */
};

/* 编译期校验线协议布局（4 字节对齐的 XAtomic_uint32_t 无填充）。 */
typedef char xmysql_shm_layout_check_channel[
    (sizeof(XMySqlSharedMemoryChannel) == 12 + XMYSQL_SHARED_MEMORY_DATA_SIZE) ? 1 : -1];
typedef char xmysql_shm_layout_check_segment[
    (sizeof(XMySqlSharedMemorySegment) == 4 + 2 * (12 + XMYSQL_SHARED_MEMORY_DATA_SIZE)) ? 1 : -1];
typedef char xmysql_shm_layout_check_connect[
    (sizeof(XMySqlSharedMemoryConnect) == 16) ? 1 : -1];

/* 计算信令字节对应的等待掩码位（DATA/SPACE/CLOSE 各占一位）。 */
static uint32_t xmysql_shm_signal_mask(uint8_t sig)
{
    if (sig == XMYSQL_SHM_SIGNAL_DATA) return 1u << 0;
    if (sig == XMYSQL_SHM_SIGNAL_SPACE) return 1u << 1;
    if (sig == XMYSQL_SHM_SIGNAL_CLOSE) return 1u << 2;
    return 0u;
}

/* 从待处理缓冲中取出匹配掩码的信令字节；无匹配返回 0。 */
static uint8_t xmysql_shm_pending_take(XMySqlSharedMemory* shared, uint32_t mask)
{
    size_t i;
    uint32_t closeMask = xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_CLOSE);
    for (i = 0; i < shared->m_pendingCount; ++i) {
        uint32_t m = xmysql_shm_signal_mask(shared->m_pending[i]);
        if (m != 0u && ((mask & m) != 0u || (m & closeMask) != 0u)) {
            uint8_t sig = shared->m_pending[i];
            memmove(&shared->m_pending[i], &shared->m_pending[i + 1],
                    shared->m_pendingCount - i - 1);
            --shared->m_pendingCount;
            return sig;
        }
    }
    return 0u;
}

/* 向信令通道写入 1 个信令字节；失败（通道已断开）返回 false。 */
static bool xmysql_shm_send_signal(XFd signalFd, uint8_t sig)
{
    return XFileSystem_write(signalFd, &sig, 1) == 1;
}

/* 等待信令通道上的通知字节：
 *   mask 指定当前路径接受的 DATA/SPACE 位，CLOSE 始终结束等待；
 *   命中返回字节值（DATA/SPACE/CLOSE），超时或通道错误返回 0。
 * 等待期间先检查待处理缓冲，再阻塞读取信令通道（平台实现内核等待，
 * 不做共享内存轮询；POSIX 平台设置 SO_RCVTIMEO，read 周期返回后
 * 检查 deadline 以支持整体超时）。 */
static uint8_t xmysql_shm_wait_signal(XMySqlSharedMemory* shared, uint32_t mask,
                                      int64_t deadline)
{
    uint32_t closeMask = xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_CLOSE);
    for (;;) {
        uint8_t sig = xmysql_shm_pending_take(shared, mask);
        if (sig != 0u) return sig;

        if (shared->m_pendingCount >= XMYSQL_SHM_PENDING_CAPACITY)
            return 0u; /* 待处理缓冲溢出，视为协议错误 */

        {
            uint8_t byte = 0u;
            int64_t n = XFileSystem_read(shared->m_signalFd, &byte, 1);
            if (n == 1) {
                uint32_t m = xmysql_shm_signal_mask(byte);
                if (m == 0u) return 0u; /* 未知信令字节，协议错误 */
                if ((mask & m) != 0u || (m & closeMask) != 0u)
                    return byte;
                shared->m_pending[shared->m_pendingCount++] = byte;
                continue;
            }
            if (n < 0) return 0u; /* 通道错误/对端断开 */
            /* n == 0：平台 read 在无信号时阻塞后返回（POSIX 超时片），
               此时检查整体截止时间；信号到达时内核会立即唤醒，不忙轮询。 */
            if (deadline >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline)
                return 0u;
        }
    }
}

/* 等待信令通道（基于 XFd 直接等待，用于共享传输对象创建前的
 * 连接协商段）：与 xmysql_shm_wait_signal 相同的阻塞内核等待语义，
 * 但协商段只有单一请求-应答路径，不存在读写路径交错，因此不维护
 * 待处理缓冲；与当前路径无关的信令直接丢弃。
 * mask 指定接受的信令位，CLOSE 始终结束等待；
 * 返回信令字节，超时或通道错误返回 0。 */
static uint8_t xmysql_shm_wait_signal_fd(XFd signalFd, uint32_t mask,
                                         int64_t deadline)
{
    uint32_t closeMask = xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_CLOSE);
    for (;;) {
        uint8_t byte = 0u;
        int64_t n = XFileSystem_read(signalFd, &byte, 1);
        if (n == 1) {
            uint32_t m = xmysql_shm_signal_mask(byte);
            if (m == 0u) return 0u; /* 未知信令字节，协议错误 */
            if ((mask & m) != 0u || (m & closeMask) != 0u)
                return byte;
            continue; /* 与当前等待路径无关的信令：协商段无交错，直接丢弃 */
        }
        if (n < 0) return 0u; /* 通道错误/对端断开 */
        /* n == 0：平台 read 超时片返回，检查整体截止时间而非忙轮询。 */
        if (deadline >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline)
            return 0u;
    }
}

/* 释放传输对象持有资源（不通知服务器，用于打开失败清理）。 */
static void xmysql_shm_release_resources(XMySqlSharedMemory* shared)
{
    if (!shared) return;
    if (shared->m_segment) {
        XFileSystem_unmap(shared->m_segment, (int64_t)sizeof(XMySqlSharedMemorySegment));
        shared->m_segment = NULL;
    }
    if (shared->m_signalFd >= 0) {
        XFileSystem_close(shared->m_signalFd);
        shared->m_signalFd = XFD_INVALID;
    }
}

XMySqlSharedMemory* XMySqlSharedMemory_open(const XString* baseName, int timeoutMs)
{
    const char* base;
    char nameBuf[XMYSQL_SHM_NAME_MAX];
    XString* name = NULL;
    XMySqlSharedMemoryConnect* connectMap = NULL;
    XFd connectFd = XFD_INVALID;
    XMySqlSharedMemory* shared = NULL;
    uint32_t number;
    uint8_t sig;
    int64_t deadline = (timeoutMs >= 0)
        ? XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs : -1;

    if (!baseName) return NULL;
    base = XString_toUtf8(baseName);
    if (!base) return NULL;

    /* 1. 打开连接协商段并映射（由服务器创建）。服务器与客户端可能同时
       启动，段创建存在竞态：按网络套接字 connect 语义在截止时间内
       有限重试（平台 openSharedMemory 内部同样对信令通道重试）。 */
    if (snprintf(nameBuf, sizeof(nameBuf), "%s%s", base, XMYSQL_SHM_SEG_CONNECT)
        >= (int)sizeof(nameBuf))
        goto fail;
    name = XString_create_utf8(nameBuf);
    if (!name) goto fail;
    {
        int attempt;
        for (attempt = 0; attempt < XMYSQL_SHM_OPEN_RETRY; ++attempt) {
            connectFd = XFileSystem_openSharedMemory(name, false, 0, NULL);
            if (connectFd >= 0) break;
            if (deadline >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline)
                break;
            XThread_msleep(XMYSQL_SHM_OPEN_RETRY_WAIT_MS);
        }
    }
    XString_delete_base(name);
    name = NULL;
    if (connectFd < 0) goto fail;
    connectMap = (XMySqlSharedMemoryConnect*)XFileSystem_map(
        connectFd, 0, (int64_t)sizeof(XMySqlSharedMemoryConnect), 0x2);
    if (!connectMap) goto fail;

    /* 2. 校验魔数后发起连接请求，等待服务端应答（信令通道阻塞等待）。 */
    if (XAtomic_load_uint32(&connectMap->magic, XAtomic_MemoryOrder_Acquire)
        != XMYSQL_SHARED_MEMORY_MAGIC)
        goto fail;
    XAtomic_store_uint32(&connectMap->request, 1u, XAtomic_MemoryOrder_Release);
    if (!xmysql_shm_send_signal(connectFd, XMYSQL_SHM_SIGNAL_DATA)) goto fail;
    sig = xmysql_shm_wait_signal_fd(connectFd,
        xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_DATA), deadline);
    if (sig != XMYSQL_SHM_SIGNAL_DATA) goto fail;
    number = XAtomic_load_uint32(&connectMap->number, XAtomic_MemoryOrder_Acquire);
    /* 应答确认：复位请求标志，服务端据此复位应答标志、服务下一个连接。 */
    XAtomic_store_uint32(&connectMap->request, 0u, XAtomic_MemoryOrder_Release);
    if (!xmysql_shm_send_signal(connectFd, XMYSQL_SHM_SIGNAL_SPACE)) goto fail;

    XFileSystem_unmap(connectMap, (int64_t)sizeof(XMySqlSharedMemoryConnect));
    connectMap = NULL;
    XFileSystem_close(connectFd);
    connectFd = XFD_INVALID;

    /* 3. 按连接编号打开该连接的数据段并映射（服务端已创建）。 */
    if (snprintf(nameBuf, sizeof(nameBuf), "%s%u%s", base, number, XMYSQL_SHM_SEG_DATA)
        >= (int)sizeof(nameBuf))
        goto fail;
    name = XString_create_utf8(nameBuf);
    if (!name) goto fail;
    {
        int attempt;
        for (attempt = 0; attempt < XMYSQL_SHM_OPEN_RETRY; ++attempt) {
            connectFd = XFileSystem_openSharedMemory(name, false, 0, NULL);
            if (connectFd >= 0) break;
            if (deadline >= 0 && XDateTime_currentMSecsSinceEpoch() >= deadline)
                break;
            XThread_msleep(XMYSQL_SHM_OPEN_RETRY_WAIT_MS);
        }
    }
    XString_delete_base(name);
    name = NULL;
    if (connectFd < 0) goto fail;

    shared = (XMySqlSharedMemory*)XCalloc_System(1, sizeof(XMySqlSharedMemory));
    if (!shared) goto fail;
    shared->m_signalFd = connectFd;
    shared->m_segment = (XMySqlSharedMemorySegment*)XFileSystem_map(
        connectFd, 0, (int64_t)sizeof(XMySqlSharedMemorySegment), 0x2);
    if (!shared->m_segment) goto fail;

    /* 4. 等待服务端映射并写入魔数后的就绪通知，再校验魔数。 */
    sig = xmysql_shm_wait_signal(shared,
        xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_DATA), deadline);
    if (sig != XMYSQL_SHM_SIGNAL_DATA) goto fail;
    if (XAtomic_load_uint32(&shared->m_segment->magic, XAtomic_MemoryOrder_Acquire)
        != XMYSQL_SHARED_MEMORY_MAGIC)
        goto fail;

    /* 初始状态：客户端→服务器通道空闲，首次写无需等待空间许可。 */
    shared->m_writeGranted = true;

    XFree_System((void*)base);
    return shared;

fail:
    if (name) XString_delete_base(name);
    if (connectMap)
        XFileSystem_unmap(connectMap, (int64_t)sizeof(XMySqlSharedMemoryConnect));
    if (connectFd >= 0)
        XFileSystem_close(connectFd);
    if (shared) {
        xmysql_shm_release_resources(shared);
        XFree_System(shared);
    }
    XFree_System((void*)base);
    return NULL;
}

void XMySqlSharedMemory_close(XMySqlSharedMemory* shared)
{
    if (!shared) return;
    /* 通知服务器连接关闭（关闭标志 + 信令字节），随后释放全部资源。 */
    if (shared->m_segment) {
        XAtomic_store_uint32(&shared->m_segment->clientToServer.closed, 1u,
                             XAtomic_MemoryOrder_Release);
        if (shared->m_signalFd >= 0)
            (void)xmysql_shm_send_signal(shared->m_signalFd, XMYSQL_SHM_SIGNAL_CLOSE);
    }
    xmysql_shm_release_resources(shared);
    XFree_System(shared);
}

bool XMySqlSharedMemory_read(XMySqlSharedMemory* shared, void* data,
                             size_t size, int timeoutMs)
{
    XMySqlSharedMemoryChannel* channel;
    uint8_t* target;
    size_t offset = 0;
    uint32_t length;
    int64_t deadline = (timeoutMs >= 0)
        ? XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs : -1;

    if (!shared || !data || !shared->m_segment) return false;
    if (size == 0) return true;
    channel = &shared->m_segment->serverToClient;
    if (XAtomic_load_uint32(&channel->closed, XAtomic_MemoryOrder_Acquire) != 0u)
        return false;
    target = (uint8_t*)data;
    while (offset < size) {
        /* 阻塞等待服务器写入一块数据的就绪通知（信令通道，非轮询）。 */
        uint8_t sig = xmysql_shm_wait_signal(shared,
            xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_DATA), deadline);
        if (sig == XMYSQL_SHM_SIGNAL_CLOSE || sig == 0u) return false;
        length = XAtomic_load_uint32(&channel->length, XAtomic_MemoryOrder_Acquire);
        if (length == 0 || length > XMYSQL_SHARED_MEMORY_DATA_SIZE)
            return false;
        if ((size_t)length > size - offset) {
            /* 服务器块超出本次请求长度，说明流失步，视为协议错误。 */
            return false;
        }
        memcpy(target + offset, channel->data, (size_t)length);
        offset += (size_t)length;
        /* 块消费完毕，许可服务器写入下一块。 */
        XAtomic_store_uint32(&channel->status, XMYSQL_SHM_STATUS_IDLE,
                             XAtomic_MemoryOrder_Release);
        if (!xmysql_shm_send_signal(shared->m_signalFd, XMYSQL_SHM_SIGNAL_SPACE))
            return false;
    }
    return true;
}

bool XMySqlSharedMemory_write(XMySqlSharedMemory* shared, const void* data,
                              size_t size, int timeoutMs)
{
    XMySqlSharedMemoryChannel* channel;
    const uint8_t* source;
    size_t offset = 0;
    size_t chunk;
    int64_t deadline = (timeoutMs >= 0)
        ? XDateTime_currentMSecsSinceEpoch() + (int64_t)timeoutMs : -1;

    if (!shared || !data || !shared->m_segment) return false;
    if (size == 0) return true;
    channel = &shared->m_segment->clientToServer;
    if (XAtomic_load_uint32(&channel->closed, XAtomic_MemoryOrder_Acquire) != 0u)
        return false;
    source = (const uint8_t*)data;
    while (offset < size) {
        /* 等待服务器读走上一块（空间许可）后获得写许可；首块已内置许可。 */
        if (!shared->m_writeGranted) {
            uint8_t sig = xmysql_shm_wait_signal(shared,
                xmysql_shm_signal_mask(XMYSQL_SHM_SIGNAL_SPACE), deadline);
            if (sig == XMYSQL_SHM_SIGNAL_CLOSE || sig == 0u) return false;
        }
        chunk = size - offset;
        if (chunk > XMYSQL_SHARED_MEMORY_DATA_SIZE) chunk = XMYSQL_SHARED_MEMORY_DATA_SIZE;
        memcpy(channel->data, source + offset, chunk);
        /* 先写数据再写块长与状态，最后发送信令：Release 保证对端收到
           信令后读取 length/data 能看到完整内容。 */
        XAtomic_store_uint32(&channel->length, (uint32_t)chunk, XAtomic_MemoryOrder_Release);
        XAtomic_store_uint32(&channel->status, XMYSQL_SHM_STATUS_READY,
                             XAtomic_MemoryOrder_Release);
        if (!xmysql_shm_send_signal(shared->m_signalFd, XMYSQL_SHM_SIGNAL_DATA))
            return false;
        shared->m_writeGranted = false;
        offset += chunk;
    }
    return true;
}
