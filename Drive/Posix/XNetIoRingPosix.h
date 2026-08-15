/**
 * @file XNetIoRingPosix.h
 * @brief XAbstractNetIoRing Linux io_uring 后端头文件
 *
 * 包含：
 *   1. io_uring 事件上下文类型（对应 Windows XNetIoRingWin32.h）
 *   2. XNetIoRingPosix 类前置声明与 API
 */

#ifndef XNETIORINGPOSIX_H
#define XNETIORINGPOSIX_H

#include "XAbstractNetIoRing.h"

#ifdef __linux__
#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

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
#define XNetIoRingPosix_create(...) XNetIoRingPosix_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XNETIORINGPOSIX_H */
