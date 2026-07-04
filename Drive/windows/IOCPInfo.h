#ifndef IOCPINFO_H
#define IOCPINFO_H
#include <windows.h>
#include "XSocketDescriptor.h"
#include "XTypes.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    XEventContextType_Type_Socket = 1,   // 套接字
    XEventContextType_Type_File,         // 普通文件 / 管道 / 串口等 HANDLE 类型
    XEventContextType_Type_Timer,        // 定时器（由 PostQueuedCompletionStatus 投递）
    XEventContextType_Type_Custom,       // 其他自定义事件
} XEventContextType;

/** @brief IOCP 事件上下文基类 */
typedef struct XEventContext {
    OVERLAPPED overlapped;              /**< 必须是第一个成员 */
    XEventContextType type;             /**< 事件类型 */
    XFd fd;                             /**< XFileDescriptor 统一标识符（套接字/文件/定时器共用） */
} XEventContext;

/** @brief 定时器事件上下文（继承 XEventContext） */
typedef struct XEventContext_Timer {
    XEventContext base;                 /**< 继承基类（base.fd 即 XTimerId = XFd） */
} XEventContext_Timer;

/** @brief 套接字/串口/文件 I/O 事件上下文（继承 XEventContext） */
typedef struct XEventContext_IOCP {
    XEventContext base;                 /**< 继承基类（base.fd 即 xfd） */
    XSocketDescriptor socket;           /**< 原始套接字/文件描述符（底层 I/O 使用） */
    long eventMask;                     /**< 当前注册的事件掩码 (FD_READ, FD_WRITE 等) */
    void* buffer;                       /**< 数据缓冲区 */
    size_t bufferSize;                  /**< 缓冲区大小 */
    size_t finishedBytes;               /**< 完成字节数 */
} XEventContext_IOCP;

#ifdef __cplusplus
}
#endif

#endif // XSOCKETNOTIFIER_H