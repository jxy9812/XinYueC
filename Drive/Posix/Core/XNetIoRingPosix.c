/**
 * @file XNetIoRingPosix.c
 * @brief XAbstractNetIoRing Linux 后端实现（io_uring/epoll 双引擎）
 *
 * 继承 XAbstractNetIoRing，Linux 5.1+ 内核运行 io_uring 完成型异步
 * I/O；init 时 io_uring_setup 失败（-ENOSYS，Linux 4.x 及以下内核）
 * 自动回退 epoll 就绪模型（同一结构体、同一虚函数表、外部 API 签名
 * 完全一致，外部调用点零修改）。
 *
 * 双引擎编译期开关（XNetIoRingPosix.h 探测）：
 *   XNET_BUILD_IO_URING  工具链内核头含 linux/io_uring.h 且未强制 epoll
 *   XNET_BUILD_EPOLL     恒为 1（运行时回退引擎，保证低版本内核可用）
 *
 * io_uring 路径核心流程：
 *   pollPlatform:  io_uring CQ 非阻塞轮询 -> processOneCompletion 推 CQ
 *   waitForEvents: io_uring_enter(timeout) 阻塞；wakeUp 经 eventfd
 *
 * epoll 路径语义映射（就绪模型 -> 完成语义）：
 *   网络 RECV/RECVMSG/SEND/ACCEPT/CONNECT：提交时注册 epoll 兴趣挂起，
 *     就绪即执行非阻塞 IO 并推 CQ 条目（对上层等价完成通知）；
 *   文件 READ/WRITE/FSYNC：提交时同步执行（普通文件 epoll 不适用），
 *     完成入队供 waitCqe 立即取回；
 *   唤醒：eventfd 常驻 epoll 集合；定时器：timerfd 以 READ 提交。
 *
 * 外部 API（getSqe/submitSqe/waitCqe/ringFd）双模式分发，调用点
 * （XDeviceFile_posix_api.c / XDeviceNetwork_posix.c 直接构造 SQE）
 * 零修改；无 io_uring 头的老工具链由头文件伪 SQE 兼容层保证编译。
 */

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#if defined(__linux__)

#include "XAbstractNetIoRing.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XNetIoRingPosix.h"
#include "XFileDescriptor.h"
#include "XSocketDescriptor.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#if XNET_BUILD_EPOLL
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif /* XNET_BUILD_EPOLL */
#if XNET_BUILD_IO_URING
#include <sys/syscall.h>
#include <linux/io_uring.h>

/* 确保 io_uring 系统调用号可用 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif
#endif /* XNET_BUILD_IO_URING */

/* 单次 pollPlatform 最大处理的事件数，防止饿死其他任务 */
#ifndef XNETIORING_POSIX_POLL_BATCH
#define XNETIORING_POSIX_POLL_BATCH  64
#endif

/* io_uring SQ 条目数 */
#ifndef XNETIORING_POSIX_QUEUE_DEPTH
#define XNETIORING_POSIX_QUEUE_DEPTH  256
#endif

/* epoll 单次等待的最大事件数 */
#ifndef XNET_EPOLL_MAX_EVENTS
#define XNET_EPOLL_MAX_EVENTS 64
#endif

/* epoll 兴趣/操作封装（与 XSocketActType 解耦的内部常量） */
#define XNET_EPOLL_IN_INTEREST   (uint32_t)EPOLLIN
#define XNET_EPOLL_OUT_INTEREST  (uint32_t)EPOLLOUT
#define XNET_EPOLL_WANT_IN       0
#define XNET_EPOLL_WANT_OUT      1

/* ================================================================
 * 运行时后端模式
 * ================================================================ */
typedef enum XNetIoRingMode {
    XNET_MODE_NONE = 0,   /**< 未初始化 */
    XNET_MODE_IOURING,    /**< io_uring 完成型（Linux 5.1+ 内核） */
    XNET_MODE_EPOLL       /**< epoll 就绪型（低版本内核回退） */
} XNetIoRingMode;

/* ================================================================
 * 完成处理（公共辅助函数，双引擎共用）
 *
 * 将一条完成结果（res + user_data 上下文）转换为 CQ 条目或直接
 * 投递定时器事件。pollPlatform（非阻塞批量）和 waitForEvents
 * （阻塞单条）共用。对应 Win32 的 processOneCompletion。 */
static void processOneCompletion(XAbstractNetIoRing* self, int64_t res,
                                 void* userData) {
    XEventContext* ctx = (XEventContext*)(uintptr_t)userData;
    /* user_data==0 表示 wakeUp 唤醒，无完成需处理（对应 Win32 overlapped==NULL） */
    if (!ctx) return;

    ctx->result = res;
    if (ctx->type == XEventContextType_Type_Timer) {
        /* 定时器完成：直接投递定时器事件到应用层 */
        XFd timerFd = ctx->fd;
        if (timerFd != XFD_INVALID) {
            XTimerEvent* timerEv = XTimerEvent_create((XTimerId)timerFd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->posted = true;
                timerEvent->spontaneous = true;
                /* 从 fd 表获取 owner */
                XFileDescriptor* desc = XFd_get(timerFd);
                if (desc && desc->object) {
                    XCoreApplication_postEvent((XObject*)desc->object,
                                               timerEvent, XEVENT_PRIORITY_NORMAL);
                }
            }
        }
    }
    else if (ctx->type == XEventContextType_Type_Socket ||
             ctx->type == XEventContextType_Type_File) {
        /* Socket/File I/O 完成：转换为 CQ 条目 */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = (res >= 0) ? (uint32_t)res : 0;
        cqEntry.m_error = (res < 0) ? -res : 0;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;
        cqEntry.m_fdType = XFd_type(ctx->fd);

        /* XSocketActType 事件掩码直接使用 ctx->eventMask（与 Windows 不同，
         * io_uring 下 eventMask 由调用者直接设置为 XSocketActType 值） */
        cqEntry.m_events = ctx->eventMask;

        /* 取消请求只用于回收 user_data，不能在 fd 槽位复用后变成一条
         * 新的连接事件。上下文结果仍在上面写回，供关闭路径确认 CQE 已
         * 被消费。 */
        if (ctx->eventMask == 0)
            return;

        /* I/O 失败时：连接丢失等错误也标记为 Connect 事件 */
        if (res < 0 && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketAct_Connect;
        }

        ctx->finishedBytes = (res >= 0) ? (size_t)res : 0;
        XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * 主结构体（双引擎字段集，按编译开关收编）
 * ================================================================ */

/** @brief 挂起（等待 fd 就绪）的伪 SQE 请求（epoll 引擎）。 */
typedef struct XNetPendingOp {
    struct io_uring_sqe sqe;    /**< 伪/真 SQE 副本（提交时快照） */
    XEventContext* ctx;         /**< 完成上下文（= sqe.user_data） */
    struct XNetPendingOp* next; /**< 链表后继 */
} XNetPendingOp;

typedef struct XNetIoRingPosix {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    XNetIoRingMode m_mode;        /**< 运行时后端模式 */
    int     m_wakeFd;             /**< 跨线程唤醒 eventfd（双引擎共用） */
    bool    m_ownsRing;           /**< 是否拥有后端资源 */
#if XNET_BUILD_IO_URING
    int     m_ringFd;             /**< io_uring 环形缓冲区 fd */
    /* io_uring 内存映射区域 */
    unsigned* m_sqHeadPtr;        /**< SQ head 指针（内核写入） */
    unsigned* m_sqTailPtr;        /**< SQ tail 指针（用户空间写入） */
    unsigned* m_sqMaskPtr;        /**< SQ ring mask */
    unsigned* m_sqFlagsPtr;       /**< SQ ring flags */
    unsigned* m_sqArray;          /**< SQ 索引数组 */
    struct io_uring_cqe* m_cqes;  /**< CQ 条目数组 */
    unsigned* m_cqHeadPtr;        /**< CQ head 指针（用户空间写入） */
    unsigned* m_cqTailPtr;        /**< CQ tail 指针（内核写入） */
    unsigned* m_cqMaskPtr;        /**< CQ ring mask */
    struct io_uring_sqe* m_sqes;  /**< SQ 条目数组 */
    unsigned m_sqEntries;         /**< SQ 条目数 */
#endif /* XNET_BUILD_IO_URING */
#if XNET_BUILD_EPOLL
    int     m_epollFd;            /**< epoll 实例 fd */
    struct io_uring_sqe m_currentSqe; /**< getSqe 单槽暂存（外部填充） */
    bool    m_hasCurrentSqe;      /**< 暂存槽是否有效 */
    XNetPendingOp* m_pendingHead; /**< 挂起请求链表头 */
#endif /* XNET_BUILD_EPOLL */
} XNetIoRingPosix;

/* 前置声明（结构体定义在 XNetIoRingPosix.c）——保持 .h 兼容 */

/* ==================== 前向声明 ==================== */
static XFd   VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * io_uring 引擎（XNET_BUILD_IO_URING 时编译）
 * ================================================================ */
#if XNET_BUILD_IO_URING

/* 获取 SQ 空闲槽位索引，返回 SQE 指针 */
static struct io_uring_sqe* ioGetSqeSlot(XNetIoRingPosix* posix) {
    if (!posix->m_sqHeadPtr || !posix->m_sqTailPtr || !posix->m_sqMaskPtr || !posix->m_sqes)
        return NULL;
    unsigned head = __atomic_load_n(posix->m_sqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned next = tail + 1;

    /* SQ 已满 */
    if (next - head > posix->m_sqEntries)
        return NULL;

    unsigned index = tail & (*posix->m_sqMaskPtr);
    return &posix->m_sqes[index];
}

/* 提交已填充的 SQE 到内核 */
static void ioSubmitEntries(XNetIoRingPosix* posix, int toSubmit) {
    unsigned tail = __atomic_load_n(posix->m_sqTailPtr, __ATOMIC_RELAXED);
    unsigned startIndex = tail & (*posix->m_sqMaskPtr);

    /* 写入 SQ array 映射 */
    for (int i = 0; i < toSubmit; i++) {
        posix->m_sqArray[(startIndex + i) & (*posix->m_sqMaskPtr)] =
            (startIndex + i) & (*posix->m_sqMaskPtr);
    }

    /* 推进 SQ tail（发布到内核） */
    __atomic_store_n(posix->m_sqTailPtr, tail + toSubmit, __ATOMIC_RELEASE);

    /* 非 SQPOLL 模式必须调用 io_uring_enter 才会真正提交 SQE。 */
    syscall(__NR_io_uring_enter, posix->m_ringFd, toSubmit, 0, 0, NULL, 0);
}

/* 从 CQ 获取一个完成条目，非阻塞 */
static struct io_uring_cqe* ioPeekCqe(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_cqTailPtr, __ATOMIC_ACQUIRE);

    if (head == tail)
        return NULL;  /* CQ 为空 */

    return &posix->m_cqes[head & (*posix->m_cqMaskPtr)];
}

/* 推进 CQ head（标记已消费） */
static void ioAdvanceCq(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_RELAXED);
    __atomic_store_n(posix->m_cqHeadPtr, head + 1, __ATOMIC_RELEASE);
}

/* 非阻塞轮询 CQ（io 引擎） */
static void ioPollPlatform(XNetIoRingPosix* posix, XAbstractNetIoRing* self) {
    int batchCount = 0;
    if (posix->m_ringFd < 0) return;
    while (batchCount < XNETIORING_POSIX_POLL_BATCH) {
        struct io_uring_cqe* cqe = ioPeekCqe(posix);
        if (!cqe) break;
        processOneCompletion(self, cqe->res,
                             (void*)(uintptr_t)cqe->user_data);
        ioAdvanceCq(posix);
        batchCount++;
    }
}

/* 使用 io_uring_setup 创建实例并 mmap 环形缓冲区 */
static bool ioSetupRing(XNetIoRingPosix* ring) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags = 0;

    int ringFd = (int)syscall(__NR_io_uring_setup,
                              XNETIORING_POSIX_QUEUE_DEPTH, &params);
    if (ringFd < 0) return false;

    ring->m_ringFd = ringFd;
    ring->m_sqEntries = params.sq_entries;
    ring->m_ownsRing = true;

    size_t sqRingSize = (size_t)params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void* sqPtr = mmap(0, sqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQ_RING);
    if (sqPtr == MAP_FAILED) goto fail;

    ring->m_sqHeadPtr  = (unsigned*)((char*)sqPtr + params.sq_off.head);
    ring->m_sqTailPtr  = (unsigned*)((char*)sqPtr + params.sq_off.tail);
    ring->m_sqMaskPtr  = (unsigned*)((char*)sqPtr + params.sq_off.ring_mask);
    ring->m_sqFlagsPtr = (unsigned*)((char*)sqPtr + params.sq_off.flags);
    ring->m_sqArray    = (unsigned*)((char*)sqPtr + params.sq_off.array);

    size_t cqRingSize = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    void* cqPtr = mmap(0, cqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_CQ_RING);
    if (cqPtr == MAP_FAILED) goto fail;

    ring->m_cqHeadPtr = (unsigned*)((char*)cqPtr + params.cq_off.head);
    ring->m_cqTailPtr = (unsigned*)((char*)cqPtr + params.cq_off.tail);
    ring->m_cqMaskPtr = (unsigned*)((char*)cqPtr + params.cq_off.ring_mask);
    ring->m_cqes      = (struct io_uring_cqe*)((char*)cqPtr + params.cq_off.cqes);

    size_t sqesSize = params.sq_entries * sizeof(struct io_uring_sqe);
    void* sqesPtr = mmap(0, sqesSize, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQES);
    if (sqesPtr == MAP_FAILED) goto fail;

    ring->m_sqes = (struct io_uring_sqe*)sqesPtr;

    ring->m_wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ring->m_wakeFd < 0) goto fail;

    return true;

fail:
    if (ring->m_sqHeadPtr) { munmap(ring->m_sqHeadPtr, sqRingSize); ring->m_sqHeadPtr = NULL; }
    if (ring->m_cqHeadPtr) { munmap(ring->m_cqHeadPtr, cqRingSize); ring->m_cqHeadPtr = NULL; }
    if (ring->m_sqes)      { munmap(ring->m_sqes, sqesSize);      ring->m_sqes = NULL; }
    if (ring->m_wakeFd >= 0) { close(ring->m_wakeFd); ring->m_wakeFd = -1; }
    close(ringFd);
    ring->m_ringFd = -1;
    ring->m_ownsRing = false;
    return false;
}

/* io 引擎资源清理 */
static void ioDeinitCleanup(XNetIoRingPosix* posix) {
    if (posix->m_ownsRing && posix->m_ringFd >= 0) {
        size_t sqRingSize = (size_t)(posix->m_sqArray) -
                            (size_t)(posix->m_sqHeadPtr) +
                            posix->m_sqEntries * sizeof(unsigned);
        size_t cqRingSize = (size_t)(posix->m_cqMaskPtr) -
                            (size_t)(posix->m_cqHeadPtr) +
                            sizeof(unsigned) +
                            posix->m_sqEntries * sizeof(struct io_uring_cqe);
        size_t sqesSize = posix->m_sqEntries * sizeof(struct io_uring_sqe);

        if (posix->m_sqHeadPtr) {
            munmap(posix->m_sqHeadPtr, sqRingSize);
            posix->m_sqHeadPtr = NULL;
        }
        if (posix->m_cqHeadPtr) {
            munmap(posix->m_cqHeadPtr, cqRingSize);
            posix->m_cqHeadPtr = NULL;
        }
        if (posix->m_sqes) {
            munmap(posix->m_sqes, sqesSize);
            posix->m_sqes = NULL;
        }
        close(posix->m_ringFd);
    }
    posix->m_ringFd = -1;
    posix->m_ownsRing = false;
}
#endif /* XNET_BUILD_IO_URING */

/* ================================================================
 * epoll 引擎（恒编译：io_uring 失败的运行时回退）
 * ================================================================ */
#if XNET_BUILD_EPOLL

/** @brief 从挂起链中按 fd 与兴趣方向摘除一个请求（无则 NULL）。 */
static XNetPendingOp* epTakePending(XNetIoRingPosix* posix, int fd,
                                    int wantOut) {
    XNetPendingOp** pp = &posix->m_pendingHead;
    while (*pp) {
        XNetPendingOp* op = *pp;
        if (op->sqe.fd == fd) {
            unsigned want = (op->sqe.opcode == IORING_OP_SEND ||
                             op->sqe.opcode == IORING_OP_CONNECT)
                                ? XNET_EPOLL_WANT_OUT
                                : XNET_EPOLL_WANT_IN;
            if (want == wantOut) {
                *pp = op->next;
                return op;
            }
        }
        pp = &op->next;
    }
    return NULL;
}

/** @brief 执行挂起请求的 IO（fd 已就绪），完成推 CQ 条目。
 *  @return true 已完成并释放；false 仍在挂起（EAGAIN）。 */
static bool epExecutePending(XNetIoRingPosix* posix, XNetPendingOp* op,
                             XAbstractNetIoRing* self) {
    const struct io_uring_sqe* sqe = &op->sqe;
    ssize_t res;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_RECV:
        res = recv(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len, 0);
        break;
    case IORING_OP_RECVMSG:
        res = recvmsg(sqe->fd, (struct msghdr*)(uintptr_t)sqe->addr, 0);
        break;
    case IORING_OP_SEND:
        res = send(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len,
                   MSG_NOSIGNAL);
        break;
    case IORING_OP_ACCEPT:
        res = accept4(sqe->fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        break;
    case IORING_OP_CONNECT: {
        int err = 0;
        socklen_t errLen = sizeof(err);
        if (getsockopt(sqe->fd, SOL_SOCKET, SO_ERROR, &err, &errLen) == 0)
            res = (err == 0) ? 0 : -err;
        else
            res = -errno;
        break;
    }
    default:
        res = -EINVAL;
        break;
    }

    if (res < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false; /* 仍在挂起，等下一次就绪 */

    /* 完成：摘除 epoll 兴趣、推 CQ 条目并释放 */
    epoll_ctl(posix->m_epollFd, EPOLL_CTL_DEL, sqe->fd, NULL);
    processOneCompletion(self, (int64_t)res,
                         (void*)(uintptr_t)sqe->user_data);
    XFree_System(op);
    return true;
}

/** @brief 文件类操作同步执行（普通文件 epoll 不适用），完成入队。 */
static void epExecuteFileSync(XNetIoRingPosix* posix,
                              XAbstractNetIoRing* self,
                              const struct io_uring_sqe* sqe) {
    ssize_t res;
    (void)posix;
    errno = 0;
    switch (sqe->opcode) {
    case IORING_OP_READ:
        /* 先按偏移读（普通文件）；ESPIPE（管道/事件fd）回退当前位置读。 */
        res = pread(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len,
                    (off_t)sqe->off);
        if (res < 0 && errno == ESPIPE)
            res = read(sqe->fd, (void*)(uintptr_t)sqe->addr, sqe->len);
        break;
    case IORING_OP_WRITE:
        res = pwrite(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len,
                     (off_t)sqe->off);
        if (res < 0 && errno == ESPIPE)
            res = write(sqe->fd, (const void*)(uintptr_t)sqe->addr, sqe->len);
        break;
    case IORING_OP_FSYNC:
        res = (fsync(sqe->fd) == 0) ? 0 : -errno;
        break;
    default:
        res = -EINVAL;
        break;
    }
    processOneCompletion(self, (int64_t)res,
                         (void*)(uintptr_t)sqe->user_data);
}

/** @brief 非阻塞 epoll 轮询并执行就绪请求。 */
static void epPollPlatform(XNetIoRingPosix* posix, XAbstractNetIoRing* self) {
    struct epoll_event events[XNET_EPOLL_MAX_EVENTS];
    int n;
    int i;
    if (posix->m_epollFd < 0) return;
    do {
        n = epoll_wait(posix->m_epollFd, events, XNET_EPOLL_MAX_EVENTS, 0);
    } while (n < 0 && errno == EINTR);
    for (i = 0; i < n; ++i) {
        int fd = events[i].data.fd;
        if (fd == posix->m_wakeFd) {
            uint64_t value;
            while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                   errno == EINTR) {}
            continue;
        }
        {
            XNetPendingOp* op = epTakePending(posix, fd, XNET_EPOLL_WANT_IN);
            if (op) epExecutePending(posix, op, self);
        }
        if (events[i].events & XNET_EPOLL_OUT_INTEREST) {
            XNetPendingOp* op = epTakePending(posix, fd, XNET_EPOLL_WANT_OUT);
            if (op) epExecutePending(posix, op, self);
        }
    }
}

/** @brief 创建 epoll 实例并注册唤醒 eventfd。 */
static bool epSetup(XNetIoRingPosix* ring) {
    struct epoll_event ev;
    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) return false;
    ring->m_epollFd = epollFd;
    ring->m_ownsRing = true;

    ring->m_wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ring->m_wakeFd < 0) {
        close(epollFd);
        ring->m_epollFd = -1;
        ring->m_ownsRing = false;
        return false;
    }
    memset(&ev, 0, sizeof(ev));
    ev.events = XNET_EPOLL_IN_INTEREST;
    ev.data.fd = ring->m_wakeFd;
    if (epoll_ctl(ring->m_epollFd, EPOLL_CTL_ADD, ring->m_wakeFd, &ev) != 0) {
        close(ring->m_wakeFd);
        ring->m_wakeFd = -1;
        close(epollFd);
        ring->m_epollFd = -1;
        ring->m_ownsRing = false;
        return false;
    }
    return true;
}

/** @brief epoll 引擎资源清理。 */
static void epDeinitCleanup(XNetIoRingPosix* posix) {
    XNetPendingOp* op;
    while ((op = posix->m_pendingHead) != NULL) {
        posix->m_pendingHead = op->next;
        XFree_System(op);
    }
    if (posix->m_ownsRing) {
        if (posix->m_epollFd >= 0) close(posix->m_epollFd);
        if (posix->m_wakeFd >= 0) close(posix->m_wakeFd);
    }
    posix->m_epollFd = -1;
    posix->m_ownsRing = false;
}
#endif /* XNET_BUILD_EPOLL */

/* 虚函数表枚举：继承 XAbstractNetIoRing（vtable 大小由本宏生成） */
XCLASS_DEFINE_BEGING(XNetIoRingPosix)
XCLASS_DEFINE_EXTEND_END(XNetIoRingPosix, XAbstractNetIoRing)

/* ================================================================
 * 重载虚函数实现（双引擎 mode 分发）
 * ================================================================ */

static void VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        ioPollPlatform(posix, self);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    epPollPlatform(posix, self);
#endif
}

static XFd VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return XFD_INVALID;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING)
        return (posix->m_ringFd >= 0) ? (XFd)(intptr_t)posix->m_ringFd
                                      : XFD_INVALID;
#endif
#if XNET_BUILD_EPOLL
    return (posix->m_epollFd >= 0) ? (XFd)(intptr_t)posix->m_epollFd
                                   : XFD_INVALID;
#else
    return XFD_INVALID;
#endif
}

static bool VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true; /* io_uring SQE 自带 fd；epoll 在提交时按需 ADD */
}

static void VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self,
                                           int timeoutMs) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) {
        struct pollfd fds[2];
        nfds_t count = 0;
        if (posix->m_ringFd < 0) return;

        VXNetIoRingPosix_pollPlatform(self);
        if (ioPeekCqe(posix) != NULL) return;

        fds[count++] = (struct pollfd){ .fd = posix->m_ringFd, .events = POLLIN };
        if (posix->m_wakeFd >= 0)
            fds[count++] = (struct pollfd){ .fd = posix->m_wakeFd, .events = POLLIN };

        {
            int result;
            do {
                result = poll(fds, count, timeoutMs);
            } while (result < 0 && errno == EINTR);
            if (result > 0 && count > 1 && (fds[1].revents & POLLIN)) {
                uint64_t value;
                while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                       errno == EINTR) {}
            }
        }
        VXNetIoRingPosix_pollPlatform(self);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    epPollPlatform(posix, self); /* 先非阻塞一轮（可能在等待前已有完成） */
    {
        struct epoll_event events[XNET_EPOLL_MAX_EVENTS];
        int n;
        int i;
        do {
            n = epoll_wait(posix->m_epollFd, events, XNET_EPOLL_MAX_EVENTS,
                           timeoutMs);
        } while (n < 0 && errno == EINTR);
        for (i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == posix->m_wakeFd) {
                uint64_t value;
                while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
                       errno == EINTR) {}
                continue;
            }
            {
                XNetPendingOp* op =
                    epTakePending(posix, fd, XNET_EPOLL_WANT_IN);
                if (op) epExecutePending(posix, op, self);
            }
            if (events[i].events & XNET_EPOLL_OUT_INTEREST) {
                XNetPendingOp* op =
                    epTakePending(posix, fd, XNET_EPOLL_WANT_OUT);
                if (op) epExecutePending(posix, op, self);
            }
        }
    }
#endif
}

static void VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    uint64_t value = 1;
    if (!posix || posix->m_wakeFd < 0) return;
    while (write(posix->m_wakeFd, &value, sizeof(value)) < 0 &&
           errno == EINTR) {}
}

static void VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)obj;
    if (!posix) return;
#if XNET_BUILD_IO_URING
    if (posix->m_mode == XNET_MODE_IOURING) ioDeinitCleanup(posix);
#endif
#if XNET_BUILD_EPOLL
    if (posix->m_mode == XNET_MODE_EPOLL) epDeinitCleanup(posix);
#endif
    if (posix->m_wakeFd >= 0) close(posix->m_wakeFd);
    posix->m_wakeFd = -1;
    posix->m_ownsRing = false;
    posix->m_mode = XNET_MODE_NONE;

    XClass_Deinit_Parent(XAbstractNetIoRing, obj);
}

/** @brief 虚函数表初始化（双引擎共用同一虚函数表）。 */
XVtable* XNetIoRingPosix_class_init(void) {
    XVTABLE_INIT_DEFAULT(XNetIoRingPosix)
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingPosix_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingPosix_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingPosix_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingPosix_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingPosix_wakeUp);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingPosix_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * 构造与初始化（运行时回退链：io_uring 失败 -> epoll）
 * ================================================================ */
void XNetIoRingPosix_init(XNetIoRingPosix* ring) {
    bool enabled = false;
    if (!ring) return;

    memset(ring, 0, sizeof(XNetIoRingPosix));
    ring->m_wakeFd = -1;
#if XNET_BUILD_IO_URING
    ring->m_ringFd = -1;
#endif
#if XNET_BUILD_EPOLL
    ring->m_epollFd = -1;
#endif
    ring->m_mode = XNET_MODE_NONE;

    /* 初始化基类（设置基类虚函数表 + 创建 SQ/CQ 队列） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 重载为本类的虚函数表 */
    XClassSetVtable(ring, XNetIoRingPosix);

    /* 运行时回退链：优先 io_uring（Linux 5.1+ 内核）；io_uring_setup
     * 失败（-ENOSYS 等，如 4.x 内核）自动回退 epoll。 */
#if XNET_BUILD_IO_URING
    if (ioSetupRing(ring)) {
        ring->m_mode = XNET_MODE_IOURING;
        enabled = true;
    }
#endif
#if XNET_BUILD_EPOLL
    if (!enabled && epSetup(ring)) {
        ring->m_mode = XNET_MODE_EPOLL;
        enabled = true;
    }
#endif
    ((XAbstractNetIoRing*)ring)->m_enabled = enabled;
}

XNetIoRingPosix* XNetIoRingPosix_create_ex(XMemoryType memory) {
    XNetIoRingPosix* ring =
        (XNetIoRingPosix*)XMemory_malloc(sizeof(XNetIoRingPosix), memory);
    if (!ring) return NULL;
    XNetIoRingPosix_init(ring);
    Set_Class_Memory(ring, memory); Set_Class_IsHeap(ring, true);
    return ring;
}

/* ================================================================
 * 外部 I/O 提交 API（双模式分发，签名与调用点不变）
 * ================================================================ */
int XNetIoRingPosix_ringFd(const XNetIoRingPosix* ring) {
    if (!ring) return -1;
#if XNET_BUILD_IO_URING
    if (ring->m_mode == XNET_MODE_IOURING) return ring->m_ringFd;
#endif
#if XNET_BUILD_EPOLL
    if (ring->m_mode == XNET_MODE_EPOLL) return ring->m_epollFd;
#endif
    return -1;
}

int IoUring_getGlobalRingFd(void) {
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    return ring ? XNetIoRingPosix_ringFd(ring) : -1;
}

struct io_uring_sqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring) {
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING)
        return ioGetSqeSlot(ring);
#endif
#if XNET_BUILD_EPOLL
    if (ring && ring->m_mode == XNET_MODE_EPOLL) {
        memset(&ring->m_currentSqe, 0, sizeof(ring->m_currentSqe));
        ring->m_hasCurrentSqe = true;
        return &ring->m_currentSqe;
    }
#endif
    return NULL;
}

void XNetIoRingPosix_submitSqe(XNetIoRingPosix* ring, int toSubmit) {
    XAbstractNetIoRing* self = (XAbstractNetIoRing*)ring;
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING) {
        ioSubmitEntries(ring, toSubmit);
        return;
    }
#endif
#if XNET_BUILD_EPOLL
    if (ring && ring->m_mode == XNET_MODE_EPOLL) {
        XEventContext* ctx;
        struct io_uring_sqe* sqe;
        XNetPendingOp* op;
        struct epoll_event ev;
        if (!ring->m_hasCurrentSqe) return;
        sqe = &ring->m_currentSqe;
        ring->m_hasCurrentSqe = false;
        ctx = (XEventContext*)(uintptr_t)sqe->user_data;

        switch (sqe->opcode) {
        /* 文件类：同步执行（普通文件 epoll 不适用），完成立即入队。 */
        case IORING_OP_READ:
        case IORING_OP_WRITE:
        case IORING_OP_FSYNC:
            epExecuteFileSync(ring, self, sqe);
            return;
        /* 取消：从挂起链移除目标请求并以 -ECANCELED 完成。 */
        case IORING_OP_ASYNC_CANCEL: {
            XNetPendingOp** pp = &ring->m_pendingHead;
            XEventContext* target = (XEventContext*)(uintptr_t)sqe->addr;
            while (*pp) {
                XNetPendingOp* victim = *pp;
                if (victim->ctx == target) {
                    *pp = victim->next;
                    epoll_ctl(ring->m_epollFd, EPOLL_CTL_DEL,
                              victim->sqe.fd, NULL);
                    XFree_System(victim);
                    break;
                }
                pp = &victim->next;
            }
            if (target) target->result = -ECANCELED;
            processOneCompletion(self, target ? 0 : -ENOENT,
                                 (void*)(uintptr_t)sqe->user_data);
            return;
        }
        default:
            break;
        }

        /* 网络类：注册 epoll 兴趣并挂起，等事件循环就绪执行。 */
        op = (XNetPendingOp*)XMalloc_System(sizeof(*op));
        if (!op) {
            processOneCompletion(self, -ENOMEM,
                                 (void*)(uintptr_t)sqe->user_data);
            return;
        }
        op->sqe = *sqe;
        op->ctx = ctx;
        op->next = ring->m_pendingHead;
        ring->m_pendingHead = op;

        memset(&ev, 0, sizeof(ev));
        ev.events = (sqe->opcode == IORING_OP_SEND ||
                     sqe->opcode == IORING_OP_CONNECT)
                        ? XNET_EPOLL_OUT_INTEREST
                        : XNET_EPOLL_IN_INTEREST;
        ev.data.fd = sqe->fd;
        if (epoll_ctl(ring->m_epollFd, EPOLL_CTL_ADD, sqe->fd, &ev) != 0 &&
            errno != EEXIST) {
            ring->m_pendingHead = op->next;
            XFree_System(op);
            processOneCompletion(self, -errno,
                                 (void*)(uintptr_t)sqe->user_data);
        }
        (void)toSubmit; /* epoll 模式恒为单条提交 */
        return;
    }
#endif
}

int XNetIoRingPosix_waitCqe(XNetIoRingPosix* ring, uint64_t userData) {
#if XNET_BUILD_IO_URING
    if (ring && ring->m_mode == XNET_MODE_IOURING) {
        /* 文件 I/O 的完成由内核异步产生：有限次重试 GETEVENTS（每次
           1ms），直到目标 CQE 出现（此前单次非阻塞会在完成未到达时
           误报 -1）。 */
        int retry;
        for (retry = 0; retry < 100; ++retry) {
            struct __kernel_timespec ts = { 0, 1000000 }; /* 1ms 阻塞等待 */
            (void)syscall(__NR_io_uring_enter, ring->m_ringFd, 0, 1,
                          IORING_ENTER_GETEVENTS, &ts, NULL);
            {
                unsigned head = __atomic_load_n(ring->m_cqHeadPtr,
                                                __ATOMIC_ACQUIRE);
                unsigned tail = __atomic_load_n(ring->m_cqTailPtr,
                                                __ATOMIC_ACQUIRE);
                if (head != tail) {
                    struct io_uring_cqe* cqe =
                        &ring->m_cqes[head & (*ring->m_cqMaskPtr)];
                    if (userData == 0 || cqe->user_data == userData) {
                        __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                         __ATOMIC_RELEASE);
                        return cqe->res;
                    }
                    /* 非目标完成：跳过（推进 head 避免阻塞） */
                    __atomic_store_n(ring->m_cqHeadPtr, head + 1,
                                     __ATOMIC_RELEASE);
                }
            }
        }
        return -1;
    }
#endif
#if XNET_BUILD_EPOLL
    /* 文件类操作在 submitSqe 时已同步完成并写回 ctx->result；
       网络类挂起请求不走 waitCqe（由事件循环异步完成）。 */
    {
        XEventContext* ctx = (XEventContext*)(uintptr_t)userData;
        if (!ctx) return -1;
        return (int)ctx->result;
    }
#else
    (void)ring; (void)userData;
    return -1;
#endif
}

/* ================================================================
 * 平台钩子：创建 Linux 后端（io_uring 或 epoll 由运行时回退决定）
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingPosix_create();
}

#endif /* __linux__ */

#endif /* XAbstractNetIoRing_ON */
