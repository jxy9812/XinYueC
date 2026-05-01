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
typedef struct
{
    OVERLAPPED overlapped;      // 必须是第一个成员
    XEventContextType type;            // 新增：事件类型
    XTimerId     id;
}XEventContext_Timer;
typedef struct XEventContext_IOCP {
    OVERLAPPED overlapped;      // 必须是第一个成员
    XEventContextType type;            // 新增：事件类型
    XSocketDescriptor socket;   ///< 套接字描述符
    long eventMask;             ///< 当前注册的事件掩码 (FD_READ, FD_WRITE 等)
    void* buffer;               // 数据缓冲区
    size_t bufferSize;          // 缓冲区大小
    size_t finishedBytes;       //完成字节数
} XEventContext_IOCP;

#ifdef __cplusplus
}
#endif

#endif // XSOCKETNOTIFIER_H