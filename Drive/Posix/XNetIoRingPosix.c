/**
 * @file XNetIoRingPosix.c
 * @brief XAbstractNetIoRing Linux io_uring 后端实现
 *
 * 继承 XAbstractNetIoRing，通过重载虚函数包装 Linux io_uring 异步 I/O。
 * 本文件仅处理 io_uring 相关逻辑，不涉及 lwIP pcap 轮询
 * （由基类 XAbstractNetIoRing.c 的 pollLwip() 提供）。
 *
 * 核心流程：
 *   pollPlatform:
 *     io_uring CQ 非阻塞轮询
 *       -> XEventContext 提取 eventMask / fd / bytes
 *       -> 转换为 CQ 条目推入 CQ
 *
 *   dispatchCQEntry（基类默认实现）:
 *     从 CQ 条目创建 XEventSockAct / XEventSockClose
 *       -> XCoreApplication_postEvent 投递到应用层
 *       -> 0 字节 + Read + io_uring 来源 => XEventSockClose（对端 FIN）
 *
 *   waitForEvents:
 *     io_uring_enter(timeout) 阻塞等待
 *       -> wakeUp 通过提交 NOP SQE（user_data=0）唤醒
 *
 * 虚函数表：
 *   本类继承 XAbstractNetIoRing（含 class_init + 9 个默认虚函数实现），
 *   仅重载 6 个 io_uring 专属虚函数：GetEventFd、PollPlatform、RegisterEvent、
 *   WaitForEvents、WakeUp、Deinit。其余沿用基类默认实现。
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
#include <sys/syscall.h>
#include <linux/io_uring.h>

/* 确保 io_uring 系统调用号可用 */
#ifndef __NR_io_uring_setup
#define __NR_io_uring_setup 425
#endif
#ifndef __NR_io_uring_enter
#define __NR_io_uring_enter 426
#endif

/* 单次 pollPlatform 最大处理的 CQE 完成数，防止饿死其他任务 */
#ifndef XNETIORING_POSIX_POLL_BATCH
#define XNETIORING_POSIX_POLL_BATCH  64
#endif

/* io_uring SQ 条目数 */
#ifndef XNETIORING_POSIX_QUEUE_DEPTH
#define XNETIORING_POSIX_QUEUE_DEPTH  256
#endif

/* ================================================================
 * XNetIoRingPosix 类定义
 * ================================================================ */

/* 虚函数表枚举：继承 XAbstractNetIoRing，不添加新虚函数 */
XCLASS_DEFINE_BEGING(XNetIoRingPosix)
XCLASS_DEFINE_EXTEND_END(XNetIoRingPosix, XAbstractNetIoRing)

typedef struct XNetIoRingPosix {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    int     m_ringFd;             /**< io_uring 环形缓冲区 fd */
    int     m_wakeFd;             /**< 跨线程唤醒事件循环的 eventfd */
    bool    m_ownsRing;           /**< 是否由本实例创建 io_uring */
    uint8_t m_pad[3];             /**< 对齐填充 */

    /* io_uring 内存映射区域 */
    unsigned* m_sqHeadPtr;        /**< SQ head 指针（内核写入） */
    unsigned* m_sqTailPtr;        /**< SQ tail 指针（用户空间写入） */
    unsigned* m_sqMaskPtr;        /**< SQ ring mask */
    unsigned* m_sqFlagsPtr;       /**< SQ ring flags */
    unsigned* m_sqArray;          /**< SQ 索引数组（映射 SQ entry -> SQE index） */

    struct io_uring_cqe* m_cqes;  /**< CQ 条目数组 */
    unsigned* m_cqHeadPtr;        /**< CQ head 指针（用户空间写入） */
    unsigned* m_cqTailPtr;        /**< CQ tail 指针（内核写入） */
    unsigned* m_cqMaskPtr;        /**< CQ ring mask */

    struct io_uring_sqe* m_sqes;  /**< SQ 条目数组 */
    unsigned m_sqEntries;         /**< SQ 条目数 */
} XNetIoRingPosix;

/* ==================== 前向声明 ==================== */
static XFd   VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * io_uring 内部辅助函数
 * ================================================================ */

/* 获取 SQ 空闲槽位索引，返回 SQE 指针 */
static struct io_uring_sqe* getSqe(XNetIoRingPosix* posix) {
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
static void submitSqe(XNetIoRingPosix* posix, int toSubmit) {
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
static struct io_uring_cqe* peekCqe(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(posix->m_cqTailPtr, __ATOMIC_ACQUIRE);

    if (head == tail)
        return NULL;  /* CQ 为空 */

    return &posix->m_cqes[head & (*posix->m_cqMaskPtr)];
}

/* 推进 CQ head（标记已消费） */
static void advanceCq(XNetIoRingPosix* posix) {
    unsigned head = __atomic_load_n(posix->m_cqHeadPtr, __ATOMIC_RELAXED);
    __atomic_store_n(posix->m_cqHeadPtr, head + 1, __ATOMIC_RELEASE);
}

/* ================================================================
 * io_uring 完成处理（公共辅助函数）
 *
 * 将一条 io_uring CQE 转换为 CQ 条目或直接投递定时器事件。
 * pollPlatform（非阻塞批量）和 waitForEvents（阻塞单条）共用。
 * 对应 Win32 的 processOneCompletion。 */
static void processOneCompletion(XAbstractNetIoRing* self, struct io_uring_cqe* cqe) {
    int res = cqe->res;
    XEventContext* ctx = (XEventContext*)(uintptr_t)cqe->user_data;

    /* user_data==0 表示 wakeUp 唤醒，无完成需处理（对应 Win32 overlapped==NULL） */
    if (!ctx) return;

    if (ctx->type == XEventContextType_Type_Timer) {
        /* 定时器完成：直接投递定时器事件到应用层 */
        XFd timerFd = ctx->fd;
        if (timerFd != XFD_INVALID) {
            XEventTimer* timerEv = XEventTimer_create((XTimerId)timerFd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->posted = true;
                timerEvent->spontaneous = true;
                /* 从 fd 表获取 owner */
                XFileDescriptor* desc = XFd_get(timerFd);
                if (desc && desc->ctx) {
                    XCoreApplication_postEvent((XObject*)desc->ctx,
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

        /* I/O 失败时：连接丢失等错误也标记为 Connect 事件 */
        if (res < 0 && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketAct_Connect;
        }

        ctx->finishedBytes = (res >= 0) ? (size_t)res : 0;
        XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * 重载虚函数实现（仅 io_uring 专属，其余沿用基类默认实现）
 * ================================================================ */

/* 轮询 io_uring 完成队列（非阻塞），将完成事件转换为 CQ 条目 */
static void VXNetIoRingPosix_pollPlatform(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    int batchCount = 0;

    if (!posix || posix->m_ringFd < 0) return;

    /* 非阻塞循环获取 CQE */
    while (batchCount < XNETIORING_POSIX_POLL_BATCH) {
        struct io_uring_cqe* cqe = peekCqe(posix);

        /* 无完成包可取，退出循环 */
        if (!cqe) break;

        processOneCompletion(self, cqe);
        advanceCq(posix);
        batchCount++;
    }
}

/* 获取平台事件 fd：返回 io_uring ring fd 作为事件源标识 */
static XFd VXNetIoRingPosix_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix || posix->m_ringFd < 0) return XFD_INVALID;
    return (XFd)(intptr_t)posix->m_ringFd;
}

/* 注册事件源：io_uring 无需预注册 fd（每个 SQE 自带 fd），直接返回 true。
 * 对应 Win32 的 CreateIoCompletionPort 关联 HANDLE 到 IOCP。 */
static bool VXNetIoRingPosix_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/* 阻塞等待事件（虚函数） */
static void VXNetIoRingPosix_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;

    if (!posix || posix->m_ringFd < 0) return;

    /* 先尝试非阻塞获取（可能在等待期间已有完成） */
    VXNetIoRingPosix_pollPlatform(self);

    /* 如果已有事件，直接返回 */
    if (peekCqe(posix) != NULL) return;

    struct pollfd fds[2];
    nfds_t count = 0;
    fds[count++] = (struct pollfd){ .fd = posix->m_ringFd, .events = POLLIN };
    if (posix->m_wakeFd >= 0)
        fds[count++] = (struct pollfd){ .fd = posix->m_wakeFd, .events = POLLIN };

    int result;
    do {
        result = poll(fds, count, timeoutMs);
    } while (result < 0 && errno == EINTR);

    if (result > 0 && count > 1 && (fds[1].revents & POLLIN)) {
        uint64_t value;
        while (read(posix->m_wakeFd, &value, sizeof(value)) < 0 && errno == EINTR) {}
    }

    /* 处理完成事件（可能被 wakeUp 的 NOP 唤醒） */
    VXNetIoRingPosix_pollPlatform(self);
}

/* 唤醒阻塞中的 WaitForEvents（虚函数） */
static void VXNetIoRingPosix_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)self;
    if (!posix || posix->m_wakeFd < 0) return;

    uint64_t value = 1;
    while (write(posix->m_wakeFd, &value, sizeof(value)) < 0 && errno == EINTR) {}
}

/* ================================================================
 * deinit 析构实现
 *
 * 清理顺序：
 *   1. io_uring 专属资源（unmap + close ring fd）
 *   2. 调用基类 deinit（清理 SQ/CQ 队列 + XClass 基类）
 * ================================================================ */
static void VXNetIoRingPosix_deinit(XAbstractNetIoRing* obj) {
    XNetIoRingPosix* posix = (XNetIoRingPosix*)obj;

    if (!posix) return;

    /* 释放 io_uring 内存映射区域 */
    if (posix->m_ownsRing && posix->m_ringFd >= 0) {
        /* 获取 mmap 区域大小 */
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
    if (posix->m_wakeFd >= 0)
        close(posix->m_wakeFd);
    posix->m_ringFd = -1;
    posix->m_wakeFd = -1;
    posix->m_ownsRing = false;

    /* 调用基类 deinit（清理 SQ/CQ 队列 + XClass 基类） */
    XClass_Deinit_Parent(XAbstractNetIoRing, obj);
}

/* ================================================================
 * 虚函数表初始化
 *
 * 继承基类 XAbstractNetIoRing 的虚函数表（含 9 个默认实现），
 * 仅重载 6 个 io_uring 专属虚函数。
 * ================================================================ */
XVtable* XNetIoRingPosix_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetIoRingPosix))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    /* 继承 XAbstractNetIoRing 虚函数表（含默认实现） */
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    /* 重载 io_uring 专属虚函数 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingPosix_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingPosix_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingPosix_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingPosix_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingPosix_wakeUp);

    /* 重载 deinit */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingPosix_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * io_uring 初始化辅助函数
 * ================================================================ */

/* 使用 io_uring_setup 创建 io_uring 实例并 mmap 映射环形缓冲区 */
static bool setupIoUring(XNetIoRingPosix* ring) {
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    params.flags = 0;

    int ringFd = (int)syscall(__NR_io_uring_setup,
                              XNETIORING_POSIX_QUEUE_DEPTH, &params);
    if (ringFd < 0) return false;

    ring->m_ringFd = ringFd;
    ring->m_sqEntries = params.sq_entries;
    ring->m_ownsRing = true;

    /* mmap SQ 环形缓冲区 */
    size_t sqRingSize = (size_t)params.sq_off.array + params.sq_entries * sizeof(unsigned);
    void* sqPtr = mmap(0, sqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_SQ_RING);
    if (sqPtr == MAP_FAILED) goto fail;

    ring->m_sqHeadPtr  = (unsigned*)((char*)sqPtr + params.sq_off.head);
    ring->m_sqTailPtr  = (unsigned*)((char*)sqPtr + params.sq_off.tail);
    ring->m_sqMaskPtr  = (unsigned*)((char*)sqPtr + params.sq_off.ring_mask);
    ring->m_sqFlagsPtr = (unsigned*)((char*)sqPtr + params.sq_off.flags);
    ring->m_sqArray    = (unsigned*)((char*)sqPtr + params.sq_off.array);

    /* mmap CQ 环形缓冲区 */
    size_t cqRingSize = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
    void* cqPtr = mmap(0, cqRingSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_POPULATE, ringFd, IORING_OFF_CQ_RING);
    if (cqPtr == MAP_FAILED) goto fail;

    ring->m_cqHeadPtr = (unsigned*)((char*)cqPtr + params.cq_off.head);
    ring->m_cqTailPtr = (unsigned*)((char*)cqPtr + params.cq_off.tail);
    ring->m_cqMaskPtr = (unsigned*)((char*)cqPtr + params.cq_off.ring_mask);
    ring->m_cqes      = (struct io_uring_cqe*)((char*)cqPtr + params.cq_off.cqes);

    /* mmap SQ 条目数组 */
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

/* ================================================================
 * 构造与初始化
 * ================================================================ */
void XNetIoRingPosix_init(XNetIoRingPosix* ring) {
    if (!ring) return;

    /* 清零整个子类结构体 */
    memset(ring, 0, sizeof(XNetIoRingPosix));
    ring->m_ringFd = -1;
    ring->m_wakeFd = -1;

    /* 初始化基类（设置基类虚函数表 + 创建 SQ/CQ 队列） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 重载为本类的虚函数表（继承基类默认实现 + io_uring 重载） */
    XClassSetVtable(ring, XNetIoRingPosix);

    /* 创建 io_uring 实例 */
    bool ok = setupIoUring(ring);

    /* 标记启用 */
    ((XAbstractNetIoRing*)ring)->m_enabled = ok;
}

XNetIoRingPosix* XNetIoRingPosix_create(void) {
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XMalloc_System(sizeof(XNetIoRingPosix));
    if (!ring) return NULL;

    XNetIoRingPosix_init(ring);
    Set_Class_MemoryFree(ring, XFree_System);
    return ring;
}

/* ================================================================
 * io_uring 专属 API（供 XNetwork_posix.c / XSerialPortPosix.c 使用）
 * ================================================================ */
int XNetIoRingPosix_ringFd(const XNetIoRingPosix* ring) {
    return ring ? ring->m_ringFd : -1;
}

/* 获取全局 io_uring ring fd */
int IoUring_getGlobalRingFd(void) {
    XNetIoRingPosix* ring = (XNetIoRingPosix*)XAbstractNetIoRing_global();
    return ring ? XNetIoRingPosix_ringFd(ring) : -1;
}

/**
 * @brief 获取 io_uring SQ 条目（供外部 I/O 提交使用）
 * @param ring XNetIoRingPosix 实例指针
 * @return SQE 指针，失败返回 NULL
 * @note 调用者填充 SQE 后调用 XNetIoRingPosix_submitSqe 提交
 */
struct io_uring_sqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring) {
    return ring ? getSqe(ring) : NULL;
}

/**
 * @brief 提交已填充的 SQE 到内核
 * @param ring      XNetIoRingPosix 实例指针
 * @param toSubmit  待提交的 SQE 数量
 */
void XNetIoRingPosix_submitSqe(XNetIoRingPosix* ring, int toSubmit) {
    if (ring) submitSqe(ring, toSubmit);
}

int XNetIoRingPosix_waitCqe(XNetIoRingPosix* ring, uint64_t userData) {
    if (!ring) return -1;

    /* 同步等待完成 */
    struct __kernel_timespec ts = { 0, 0 };
    int ret = (int)syscall(__NR_io_uring_enter, ring->m_ringFd, 0, 1,
                           IORING_ENTER_GETEVENTS, &ts, NULL);
    if (ret < 0) return -1;

    /* 从 CQ 获取完成条目 */
    unsigned head = __atomic_load_n(ring->m_cqHeadPtr, __ATOMIC_ACQUIRE);
    unsigned tail = __atomic_load_n(ring->m_cqTailPtr, __ATOMIC_ACQUIRE);

    if (head != tail) {
        struct io_uring_cqe* cqe = &ring->m_cqes[head & (*ring->m_cqMaskPtr)];
        __atomic_store_n(ring->m_cqHeadPtr, head + 1, __ATOMIC_RELEASE);

        if (userData == 0 || cqe->user_data == userData) {
            return cqe->res;
        }
    }

    return -1;
}

/* ================================================================
 * 平台钩子：创建 Linux io_uring 后端
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingPosix_create();
}

#endif /* __linux__ */

#endif /* XAbstractNetIoRing_ON */
