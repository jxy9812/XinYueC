/**
 * @file XNetIoRingWin32.h
 * @brief XAbstractNetIoRing Windows IOCP 后端头文件
 *
 * 包含：
 *   1. IOCP 事件上下文类型（原 IOCPInfo.h 内容，合并至此）
 *   2. XNetIoRingWin32 类前置声明与 API
 *
 * 对应 Linux 平台的 XNetIoRingPosix.h。
 */

#ifndef XNETIORINGWIN32_H
#define XNETIORINGWIN32_H

#include "CXinYueConfig.h"
#if XPLATFORM_WINDOWS

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Windows 头文件（提供 OVERLAPPED / HANDLE / DWORD 等类型）
 *
 * 若包含本头文件的 .c 同时需要 winsock2.h，须在包含本文件之前
 * 先 #include <winsock2.h>，以避免 windows.h 与 winsock2.h 冲突。
 * ================================================================ */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>

#include "XTypes.h"               /* XFd, XObject 前置声明, bool 等 */
#include "XSocketDescriptor.h"    /* XSocketDescriptor */

/* ================================================================
 * 一、IOCP 事件上下文类型（原 IOCPInfo.h 内容）
 *
 * 与 POSIX 端 XNetIoRingPosix.h 的 XEventContextType 枚举保持一致，
 * 但 XEventContext 结构体因平台差异（Windows 首成员为 OVERLAPPED）而不同。
 * ================================================================ */

typedef enum {
    XEventContextType_Type_Socket = 1,   /**< 套接字 */
    XEventContextType_Type_File,         /**< 普通文件 / 管道 / 串口等 HANDLE 类型 */
    XEventContextType_Type_Timer,        /**< 定时器（由 PostQueuedCompletionStatus 投递） */
    XEventContextType_Type_Custom,       /**< 其他自定义事件 */
} XEventContextType;

/** @brief IOCP 事件上下文基类（首成员必须为 OVERLAPPED） */
typedef struct XEventContext {
    OVERLAPPED overlapped;              /**< 必须是第一个成员，供 GetQueuedCompletionStatus 使用 */
    XEventContextType type;             /**< 事件类型 */
    XFd fd;                             /**< XFileDescriptor 统一标识符（套接字/文件/定时器共用） */
} XEventContext;

/** @brief 定时器事件上下文（继承 XEventContext） */
typedef struct XEventContext_Timer {
    XEventContext base;                 /**< 继承基类（base.fd 即 XTimerId = XFd） */
} XEventContext_Timer;

struct XEventContext_IOCP;
typedef bool (*XEventContextCompletionCallback)(
    struct XEventContext_IOCP* context, void* userData);

/** @brief 套接字/串口/文件 I/O 事件上下文（继承 XEventContext） */
typedef struct XEventContext_IOCP {
    XEventContext base;                 /**< 继承基类（base.fd 即 xfd） */
    XSocketDescriptor socket;           /**< 原始套接字/文件描述符（底层 I/O 使用） */
    long eventMask;                     /**< 当前注册的事件掩码 (FD_READ, FD_WRITE 等) */
    void* buffer;                       /**< 数据缓冲区 */
    size_t bufferSize;                  /**< 缓冲区大小 */
    size_t finishedBytes;               /**< 完成字节数 */
    XEventContextCompletionCallback completionCallback; /**< 完成包出队后的生命周期/投递过滤回调 */
    void* completionUserData;           /**< completionCallback 的调用方上下文 */
} XEventContext_IOCP;

/* ================================================================
 * 二、XNetIoRingWin32 类前置声明与 API
 *
 * 结构体完整定义在 XNetIoRingWin32.c 中（含 IOCP 句柄等私有成员）。
 * ================================================================ */

/* 前置声明（结构体定义在 XNetIoRingWin32.c） */
typedef struct XNetIoRingWin32 XNetIoRingWin32;

/* ==================== 构造与析构 ==================== */

/**
 * @brief 初始化 XNetIoRingWin32 实例（栈/静态分配）
 * @param ring XNetIoRingWin32 实例指针
 * @note 创建内部 IOCP 完成端口，继承 XAbstractNetIoRing 基类
 */
void XNetIoRingWin32_init(XNetIoRingWin32* ring);

/**
 * @brief 创建 XNetIoRingWin32 实例（堆分配）
 * @return XNetIoRingWin32 实例指针，失败返回 NULL
 * @note 调用者负责通过 XClass 释放机制释放
 */
XNetIoRingWin32* XNetIoRingWin32_create_ex(XMemoryType memory);

/* ==================== IOCP 专属 API ==================== */

/**
 * @brief 获取 IOCP 完成端口句柄
 * @param ring XNetIoRingWin32 实例指针
 * @return IOCP 端口句柄，失败返回 NULL
 */
HANDLE XNetIoRingWin32_iocpHandle(const XNetIoRingWin32* ring);

/**
 * @brief 关联 HANDLE 到 IOCP 完成端口
 * @param ring           XNetIoRingWin32 实例指针
 * @param handle         待关联的 HANDLE（socket / file / 串口）
 * @param completionKey  完成键（通常为 XObject*）
 * @return true=成功；false=失败
 */
bool XNetIoRingWin32_assocHandle(XNetIoRingWin32* ring, HANDLE handle,
                                  ULONG_PTR completionKey);

/**
 * @brief 向 IOCP 端口投递一个完成包（用于唤醒 / 自定义事件）
 * @param ring              XNetIoRingWin32 实例指针
 * @param bytesTransferred  传输字节数
 * @param completionKey     完成键
 * @param overlapped        OVERLAPPED 指针（NULL 表示纯唤醒）
 * @return true=成功；false=失败
 */
bool XNetIoRingWin32_postCompletion(XNetIoRingWin32* ring,
                                     DWORD bytesTransferred,
                                     ULONG_PTR completionKey,
                                     LPOVERLAPPED overlapped);

/* ==================== IOCP 兼容包装（供 XNetwork_win32.c / XSerialPortWin32.c 使用） ==================== */

/**
 * @brief 获取全局 IOCP 完成端口句柄
 * @return 全局 IoRing 实例的 IOCP 端口句柄，未设置返回 NULL
 */
HANDLE IOCP_getGlobalPort(void);

/**
 * @brief 将套接字绑定到全局 IOCP 完成端口
 * @param socket 套接字描述符
 * @param obj    关联的 XObject（作为完成键）
 * @return true=成功；false=失败
 */
bool IOCP_bind(XSocketDescriptor socket, XObject* obj);

#ifdef __cplusplus
}
#endif

#endif /* XPLATFORM_WINDOWS */


/* XClass create API default-memory wrappers. */
#undef XNetIoRingWin32_create
#define XNetIoRingWin32_create() XNetIoRingWin32_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XNETIORINGWIN32_H */
