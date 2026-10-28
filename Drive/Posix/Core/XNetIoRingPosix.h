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
#include <errno.h>
#include <stddef.h> /* offsetof（XNetSqe/XNetCqe 的 ABI 静态断言用）。 */

/**
 * @brief 判定一个 POSIX errno 是否属于「链路已死」（对端不可达/已断开）。
 *
 * @details [2026-10-26 物理断链根修·平台层共用口径] 拔网线（链路层静默
 *          断链）后，套接字 I/O 会以这些 errno 报错。白名单而非黑名单：
 *          EAGAIN/EWOULDBLOCK（资源暂不可用）、EINTR（被信号中断）、
 *          ENOMEM（资源不足）等瞬态错误都**不是**链路死亡，误判会导致
 *          正常连接被当作断链而反复重连。
 *
 *          白名单内容：
 *            EPIPE        对端已关闭（写向已断开的连接）
 *            ECONNRESET   对端复位（RST）
 *            ECONNABORTED 本地协议栈中止连接
 *            ETIMEDOUT    链路超时（keepalive 探测耗尽/重传耗尽）
 *            ENETUNREACH  网络不可达
 *            EHOSTUNREACH 主机不可达
 *            ENOTCONN     套接字未连接
 *            ECONNREFUSED 对端拒绝（从站未监听/已停机）
 *            ENETDOWN     网卡已下线
 *            ENETRESET    网络栈复位
 *
 * @param err POSIX errno（正数）。
 * @return true 表示链路已死，应上报断链。
 */
static inline bool XNetIoRingPosix_isLinkDeadErrno(int err)
{
    switch (err) {
    case EPIPE:
    case ECONNRESET:
    case ECONNABORTED:
    case ETIMEDOUT:
    case ENETUNREACH:
    case EHOSTUNREACH:
    case ENOTCONN:
    case ECONNREFUSED:
    case ENETDOWN:
    case ENETRESET:
        return true;
    default:
        return false;
    }
}
#endif /* __linux__ */

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
#if __has_include(<linux/time_types.h>)
/* struct __kernel_timespec 所在头；5.6 前的内核由 io_uring.h 内嵌，
 * 新内核头拆分后需显式包含（Ubuntu 22.04 实测缺它报 incomplete type）。 */
#include <linux/time_types.h>
#endif
#endif
#include <unistd.h>

/* 强制 epoll 时不编译 io_uring 引擎（头存在也不编译）。
 * [2026-10-24 安卓双引擎] Android 与 Linux 桌面同口径：io_uring/epoll
 * 双引擎都编译，运行时由 init 回退链自动选择（内核版本门 ≥5.10 +
 * io_uring_setup 探测）。历史背景：曾因 seccomp 处置不一（bionic 白名单
 * 返回 ENOSYS 可优雅回退；WSA 等定制策略 SECCOMP_RET_TRAP 直发 SIGSYS）
 * 在安卓强制 epoll；现探测改为 SIGSYS handler + siglongjmp 兜底，两派
 * 策略都安全落地 epoll，不再需要编译期一刀切。 */
#if XNET_HAS_IO_URING_HDR && \
    !(defined(XNET_FORCE_EPOLL) && XNET_FORCE_EPOLL)
#define XNET_BUILD_IO_URING 1
#else
#define XNET_BUILD_IO_URING 0
#endif
#define XNET_BUILD_EPOLL 1 /* epoll 引擎恒编译（io_uring 失败的运行时回退） */

#if defined(__ANDROID__) && XNET_BUILD_IO_URING
/**
 * @brief  安卓平台的 io_uring_setup 包装（seccomp 处置差异的收口点）。
 * @details 安卓 seccomp 对被禁系统调用的处置分两派：bionic 白名单策略返回
 *          ENOSYS（可优雅回退 epoll）；WSA 等定制策略 SECCOMP_RET_TRAP 直发
 *          SIGSYS 杀进程（真机实测）。本包装前置 SIGSYS handler +
 *          siglongjmp 兜底，两派都安全落 epoll。
 * @note    实现位于**平台层** Drive/Android/Core/XNetIoRingAndroid.c——
 *          共享的 POSIX 驱动不该内联安卓的 seccomp 行为。
 */
int XAndroid_netIoUringSetup(unsigned entries, struct io_uring_params* params);
#endif

#if !XNET_HAS_IO_URING_HDR
/* ================================================================
 * 无 io_uring 头的老工具链：**自有伪 SQE 载体**（不冒充内核结构体）
 * ================================================================
 *
 * 【统一口径 2026-10-26】此前此处定义了一份「与 io_uring 同名同布局」
 * 的 struct io_uring_sqe / io_uring_cqe，但它与**内核真实 ABI 不一致**
 * （sqe 72 字节 vs 内核 64；addr/off 位置互换；缺 poll_events/flags 字段），
 * 且「有真头」与「无真头」两套定义并存 ⇒ 同一份源码在两种工具链下会
 * 得到**不同的字节布局**，是一颗随时会炸的雷（一旦有人把伪结构体喂给
 * 内核即错位）。
 *
 * 统一后的口径：
 *  - **有真头**（桌面/armel 工具链现状）：io_uring 引擎只用内核真实
 *    结构体，并由下方静态断言锁死 ABI 偏移；
 *  - **无真头**：io_uring 引擎根本不编译（XNET_BUILD_IO_URING=0，见上），
 *    此处这份结构体**只被 epoll 引擎当作「操作描述载体」**使用——它从
 *    不与内核打交道，字段语义完全自洽（opcode/fd/addr/len/off/user_data
 *    即 epoll 提交路径需要的全部信息）。故改名为自有类型
 *    XNetPseudoSqe / XNetPseudoCqe，与内核 ABI **彻底解耦**：名字不再
 *    暗示内核布局，杜绝误用。
 */

/** @brief 操作描述载体（epoll 引擎的伪 SQE；仅无 io_uring 头时存在）。 */
typedef struct XNetPseudoSqe {
    uint8_t  opcode;        /**< IORING_OP_* 操作码（沿用内核取值，便于两引擎同码） */
    int32_t  fd;            /**< 目标 fd */
    uint64_t addr;          /**< 缓冲/消息指针（RECVMSG 时为 msghdr） */
    uint32_t len;           /**< 长度 */
    uint64_t off;           /**< 文件偏移 / connect 地址长度 */
    uint64_t user_data;     /**< 完成标识（XEventContext 指针） */
} XNetPseudoSqe;

/** @brief 完成条目载体（epoll 引擎的伪 CQE；仅无 io_uring 头时存在）。 */
typedef struct XNetPseudoCqe {
    uint64_t user_data;     /**< 完成标识 */
    int32_t  res;           /**< 完成结果（负 errno） */
} XNetPseudoCqe;

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

/* ================================================================
 * 引擎内 SQE/CQE 类型统一（2026-10-26 统一口径）
 * ================================================================
 * 有 io_uring 头：别名到**内核真实结构体**（io_uring 引擎直接与内核交互，
 *   布局必须逐字节等于内核 ABI——下方静态断言锁死）。
 * 无 io_uring 头：别名到上方的**自有伪载体**（epoll 引擎的操作描述载体，
 *   从不与内核交互，布局自洽即可）。
 *
 * C 文件内一律使用 XNetSqe / XNetCqe，**不再直接写 struct io_uring_***
 * ——消除「两套同名不同布局」的定义，杜绝误用与版本漂移。
 */
#if XNET_HAS_IO_URING_HDR
typedef struct io_uring_sqe XNetSqe;
typedef struct io_uring_cqe XNetCqe;
/* 【ABI 锁】内核 io_uring 结构体布局自 5.1 起未变；工具链头若来自更早
 * 或被裁剪的版本，偏移会变——此时静态断言直接编译失败（宁可构建失败，
 * 也不静默编出与内核错位的结构体）。 */
_Static_assert(sizeof(XNetSqe) == 64, "io_uring_sqe ABI 变化：sizeof != 64");
_Static_assert(offsetof(XNetSqe, opcode) == 0, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, fd) == 4, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, off) == 8, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, addr) == 16, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, len) == 24, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, poll_events) == 28, "io_uring_sqe ABI 变化");
_Static_assert(offsetof(XNetSqe, user_data) == 32, "io_uring_sqe ABI 变化");
_Static_assert(sizeof(XNetCqe) == 16, "io_uring_cqe ABI 变化");
_Static_assert(offsetof(XNetCqe, user_data) == 0, "io_uring_cqe ABI 变化");
_Static_assert(offsetof(XNetCqe, res) == 8, "io_uring_cqe ABI 变化");
_Static_assert(offsetof(XNetCqe, flags) == 12, "io_uring_cqe ABI 变化");
#else
typedef XNetPseudoSqe XNetSqe;
typedef XNetPseudoCqe XNetCqe;
#endif
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
 * @note 调用者填充 SQE 后调用 XNetIoRingPosix_submitSqe 提交。
 *       返回类型是 **XNetSqe**（引擎内统一类型）：有 io_uring 头时即内核
 *       真实 struct io_uring_sqe，无头时为自有伪载体（仅 epoll 引擎用）。
 */
XNetSqe* XNetIoRingPosix_getSqe(XNetIoRingPosix* ring);

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
