/**
 * @file XNetIoRingPosix.h
 * @brief XAbstractNetIoRing Linux 后端头文件（io_uring/epoll 双引擎）
 *
 * 包含：
 *   1. 后端编译期能力探测（XNET_HAS_IO_URING_HDR / XNET_BUILD_IO_URING）
 *   2. 事件上下文类型（对应 Windows XNetIoRingWin32.h）
 *   3. XNetIoRingPosix 类前置声明与 API
 *
 * 低版本工具链（内核头无 linux/io_uring.h）由伪 SQE/cqe 兼容层保证
 * 编译；运行时后端由 init 回退链决定（io_uring 失败自动回退 epoll）。
 */

#ifndef XNETIORINGPOSIX_H
#define XNETIORINGPOSIX_H

#include "XAbstractNetIoRing.h"

#ifdef __linux__
/* ================================================================
 * 双内核支持（编译期能力探测 XNET_HAS_IO_URING_HDR + 运行时回退）
 *
 *   XNET_HAS_IO_URING_HDR=1：工具链内核头含 <linux/io_uring.h>
 *     （编译期可用 io_uring 引擎与真实类型）；
 *   =0：无该头（老工具链），提供与 io_uring 同名/同布局子集的
 *     伪 SQE 与操作码常量（编译通过；运行时走 epoll）。
 *   -DXNET_FORCE_EPOLL=1：强制编译并运行 epoll 路径。
 *
 *   运行时后端由 init 决定：优先 io_uring_setup（Linux 5.1+ 内核），
 *   失败（-ENOSYS，如 4.x 内核）自动回退 epoll——同一目标板在新旧
 *   内核上同一份二进制均可用。
 * ================================================================ */
#if defined(XNET_FORCE_EPOLL) && XNET_FORCE_EPOLL
#define XNET_HAS_IO_URING_HDR 0
#elif defined(__has_include)
#if __has_include(<linux/io_uring.h>)
#define XNET_HAS_IO_URING_HDR 1
#else
#define XNET_HAS_IO_URING_HDR 0
#endif /* __has_include(<linux/io_uring.h>) */
#else /* 编译器无 __has_include：保守按有头处理（旧工具链罕见） */
#define XNET_HAS_IO_URING_HDR 1
#endif

#if XNET_HAS_IO_URING_HDR
#include <linux/io_uring.h>
#include <sys/syscall.h>
#endif
#include <unistd.h>

/* 强制 epoll 时不编译 io_uring 引擎（头存在也不编译）。 */
#if XNET_HAS_IO_URING_HDR && !(defined(XNET_FORCE_EPOLL) && XNET_FORCE_EPOLL)
#define XNET_BUILD_IO_URING 1
#else
#define XNET_BUILD_IO_URING 0
#endif
#define XNET_BUILD_EPOLL 1 /* epoll 引擎恒编译（io_uring 失败的运行时回退） */

#if !XNET_HAS_IO_URING_HDR
/* ================================================================
 * 伪 SQE 与操作码常量（无 io_uring 头的老工具链编译兼容层）：
 * 与 io_uring 同名/同布局子集，外部调用点字段语义一致、零修改。
 * ================================================================ */
struct io_uring_sqe {
    uint8_t  opcode;        /**< IORING_OP_* 操作码（真实内核取值） */
    uint8_t  pad0[3];       /**< 对齐填充 */
    int32_t  fd;            /**< 目标 fd */
    uint64_t addr;          /**< 缓冲/消息指针（RECVMSG 时为 msghdr） */
    uint32_t len;           /**< 长度 */
    uint32_t pad1;          /**< 对齐填充 */
    uint64_t off;           /**< 文件偏移 / connect 地址长度 */
    uint64_t user_data;     /**< 完成标识（XEventContext 指针） */
    uint32_t flags;         /**< SQE 标志（保留） */
    int32_t  cancel_flags;  /**< ASYNC_CANCEL 标志 */
    uint8_t  pad2[24];      /**< 填充至 64 字节（与真 SQE 同尺寸） */
};
struct io_uring_cqe {
    uint64_t user_data;     /**< 完成标识 */
    int32_t  res;           /**< 完成结果（负 errno） */
    int32_t  pad;           /**< 对齐填充 */
};

/* 内核真实操作码取值（linux/io_uring.h 5.1 的 enum io_uring_op） */
#define IORING_OP_NOP           0u
#define IORING_OP_READV         1u
#define IORING_OP_WRITEV        2u
#define IORING_OP_FSYNC         3u
#define IORING_OP_POLL_ADD      6u
#define IORING_OP_POLL_REMOVE   7u
#define IORING_OP_SENDMSG       9u
#define IORING_OP_RECVMSG       10u
#define IORING_OP_TIMEOUT       11u
#define IORING_OP_ACCEPT        13u
#define IORING_OP_ASYNC_CANCEL  14u
#define IORING_OP_CONNECT       16u
#define IORING_OP_READ          22u
#define IORING_OP_WRITE         23u
#define IORING_OP_SEND          26u
#define IORING_OP_RECV          27u
#endif /* !XNET_HAS_IO_URING_HDR */
#endif /* __linux__ */

#include "XSocketDescriptor.h"
#include "XTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 一、事件上下文类型（对应 Windows XNetIoRingWin32.h 的 XEventContext）
 * ================================================================ */

/* 与 Windows XNetIoRingWin32.h 保持一致的类型枚举 */
typedef enum {
    XEventContextType_Type_Socket = 1,   /* 套接字 */
    XEventContextType_Type_File,         /* 普通文件 / 管道 / 串口等 fd 类型 */
    XEventContextType_Type_Timer,        /* 定时器（由 io_uring timeout 或 eventfd 投递） */
    XEventContextType_Type_Custom,       /* 其他自定义事件 */
} XEventContextType;

/**
 * @brief io_uring 事件上下文基类
 *
 * 对应 Windows 的 XEventContext（首成员为 OVERLAPPED）。
 * POSIX 下没有 OVERLAPPED，io_uring CQE 的 user_data 字段指向本结构体。
 */
typedef struct XEventContext {
    XEventContextType type;             /* 事件类型 */
    XFd fd;                             /* XFileDescriptor 统一标识符 */
    uint32_t opcode;                    /* io_uring 操作码 (IORING_OP_*) */
    uint32_t eventMask;                 /* 当前注册的事件掩码 (XSocketActType) */
    void* buffer;                       /* 数据缓冲区（read/write 使用） */
    size_t bufferSize;                  /* 缓冲区大小 */
    size_t finishedBytes;               /* 完成字节数 */
    int64_t result;                     /* 原始完成结果，保留负 errno */
} XEventContext;

/** @brief 定时器事件上下文（继承 XEventContext） */
typedef struct XEventContext_Timer {
    XEventContext base;                 /* 继承基类（base.fd 即 XTimerId = XFd） */
} XEventContext_Timer;

/**
 * @brief 套接字/串口/文件 I/O 事件上下文（继承 XEventContext）
 *
 * 对应 Windows 的 XEventContext_IOCP，使用 XSocketDescriptor 存储原始 fd。
 */
typedef struct XEventContext_IO {
    XEventContext base;                 /* 继承基类 */
    XSocketDescriptor socket;           /* 原始套接字/文件描述符 */
} XEventContext_IO;

/* ================================================================
 * 二、XNetIoRingPosix 类前置声明与 API
 * ================================================================ */

/* 前置声明（结构体定义在 XNetIoRingPosix.c） */
typedef struct XNetIoRingPosix XNetIoRingPosix;

/* ==================== 构造与析构 ==================== */
void XNetIoRingPosix_init(XNetIoRingPosix* ring);
XNetIoRingPosix* XNetIoRingPosix_create_ex(XMemoryType memory);

/* ==================== io_uring 专属 API ==================== */

/**
 * @brief 获取 io_uring ring fd
 * @param ring XNetIoRingPosix 实例指针
 * @return ring fd，失败返回 -1
 */
int XNetIoRingPosix_ringFd(const XNetIoRingPosix* ring);

/**
 * @brief 获取全局 io_uring ring fd
 * @return 全局 ring fd，无则返回 -1
 */
int IoUring_getGlobalRingFd(void);

/**
 * @brief 获取 io_uring SQ 条目（供外部 I/O 提交使用）
 * @param ring XNetIoRingPosix 实例指针
 * @return SQE 指针，失败返回 NULL
 * @note 调用者填充 SQE 后调用 XNetIoRingPosix_submitSqe 提交
 */
struct io_uring_sqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring);

/**
 * @brief 提交已填充的 SQE 到内核
 * @param ring      XNetIoRingPosix 实例指针
 * @param toSubmit  待提交的 SQE 数量
 */
void XNetIoRingPosix_submitSqe(XNetIoRingPosix* ring, int toSubmit);

/**
 * @brief 同步等待并消费一条 CQE（用于文件 I/O 等同步场景）
 * @param ring      XNetIoRingPosix 实例指针
 * @param userData  期望匹配的 user_data（0 表示匹配任意）
 * @return 完成结果码，无匹配返回 -1
 */
int XNetIoRingPosix_waitCqe(XNetIoRingPosix* ring, uint64_t userData);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetIoRingPosix_create
#define XNetIoRingPosix_create() XNetIoRingPosix_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XNETIORINGPOSIX_H */
