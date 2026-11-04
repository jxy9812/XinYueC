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
 * Windows 头文件（提供 OVERLAPPED / HANDLE / DWORD / WSAEVENT 等类型）
 *
 * winsock2.h 必须先于 windows.h 包含，否则 windows.h 会先拉入 winsock.h
 * （两套 winsock 声明冲突，报 redefinition）。这里把 winsock2.h 提到最前，
 * 同时保留 windows.h——对既有调用方（串口/文件/USB 后端）完全透明：
 * 它们本来就先包含 winsock2.h 或不用 winsock 类型。
 * ================================================================ */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
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
    DWORD nativeError;                  /**< 原生完成错误码；成功为 ERROR_SUCCESS */
    bool completed;                      /**< 原生 I/O 是否成功完成 */
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

XVtable* XNetIoRingWin32_class_init(void);
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

/* ==================== 套接字就绪轮询表（select 模型） ==================== */

/**
 * @brief 把一个套接字上下文登记进本事件环的**就绪轮询表**。
 * @param ring  本事件环实例指针。
 * @param fd    该套接字对应的框架句柄 XFd（仅诊断用）。
 * @param owner 属主上下文指针（套接字后端的私有上下文）；不能为 NULL。
 * @return 登记成功返回 true；表满或参数非法返回 false。
 * @details 本通道专供 `XNETWORK_WIN_BACKEND_WSAEVENT` 的套接字后端使用。
 *
 *          ## 为什么是 select 而不是 WSAEventSelect 事件对象
 *
 *          起初按 `WSAEventSelect` 的事件对象设计：每个套接字建一个
 *          `WSAEVENT`，并进事件环等待集，就绪时由内核置位唤醒 poll。
 *          **实测该组合在本进程下不可靠**：事件对象从不置位
 *          （`select` 显示内核确有数据、`recv` 也能取到，但
 *          `WaitForSingleObject(event, 0)` 恒超时），导致读路径只能靠上层
 *          主动 `ContinueRead` 兜底、`waitForReadyRead` 超时。
 *
 *          Qt 6.8 的做法因此被直接照搬：**同步等待走 `select()`**
 *          （`qns_win.cpp:1495 QNativeSocketEnginePrivate::nativeSelect`），
 *          事件循环内的通知才用 `WSAAsyncSelect` 窗口消息。本框架没有独立
 *          窗口消息泵，故统一采用 `select`：每次 poll 对表内每个套接字做
 *          一次零超时 `select`，就绪者回调其属主——**没有需要维护和复位
 *          的事件对象状态，也就没有「漏置位」这一故障面**。
 *
 * @note 表里**只存属主指针**，不持任何内核对象：登记/注销都是纯数组
 *       操作，不存在句柄泄漏或失效句柄残留。
 * @warning 必须直接保存 owner 指针、**不能只存 fd 再反查**：`XFd` 是
 *          `XFixedPool` 的池槽位号，会随连接关闭被下一个连接复用。
 */
bool XNetIoRingWin32_addWsaEvent(XNetIoRingWin32* ring, XFd fd, void* owner);

/**
 * @brief 从就绪轮询表移除套接字上下文。
 * @param ring  本事件环实例指针。
 * @param owner 此前登记的属主上下文指针。
 * @return 无。未登记过时为空操作（幂等）。
 * @note 按 **owner 指针**匹配（不是 fd）：owner 在同一时刻唯一，能消除
 *       fd 复用带来的歧义。
 */
void XNetIoRingWin32_removeWsaEvent(XNetIoRingWin32* ring, void* owner);

/* ==================== IOCP 兼容包装（供 XDeviceNetwork_win32.c / XSerialPortWin32.c 使用） ==================== */

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
