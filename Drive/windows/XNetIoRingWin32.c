/**
 * @file XNetIoRingWin32.c
 * @brief XAbstractNetIoRing Windows IOCP 后端实现
 *
 * 继承 XAbstractNetIoRing，通过重载虚函数包装 Windows IOCP socket/file 异步 I/O。
 * 本文件仅处理 IOCP 相关逻辑，不涉及 lwIP pcap 轮询
 * （由基类 XAbstractNetIoRing.c 的 pollLwip() 提供）。
 *
 * 核心流程：
 *   pollPlatform:
 *     GetQueuedCompletionStatus(timeout=0) 循环
 *       -> XEventContext_IOCP 提取 eventMask / fd / bytes
 *       -> 转换为 CQ 条目推入 CQ
 *
 *   dispatchCQEntry（基类默认实现）:
 *     从 CQ 条目创建 XEventSockAct / XEventSockClose
 *       -> XCoreApplication_postEvent 投递到应用层
 *       -> 0 字节 + Read + IOCP 来源 => XEventSockClose（对端 FIN）
 *
 *   waitForEvents:
 *     GetQueuedCompletionStatus(timeout) 阻塞等待
 *       -> wakeUp 通过 PostQueuedCompletionStatus(NULL overlapped) 唤醒
 *
 * 虚函数表：
 *   本类继承 XAbstractNetIoRing（含 class_init + 9 个默认虚函数实现），
 *   仅重载 6 个 IOCP 专属虚函数：GetEventFd、PollPlatform、RegisterEvent、
 *   WaitForEvents、WakeUp、Deinit。其余沿用基类默认实现。
 */

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#if XPLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XAbstractNetIoRing.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XSocketDescriptor.h"
#include <winsock2.h>
#include "XNetIoRingWin32.h"
#include <string.h>
#include <stdlib.h>

/* 单次 pollPlatform 最大处理的 IOCP 完成数，防止饿死其他任务 */
#ifndef XNETIORING_WIN32_POLL_BATCH
#define XNETIORING_WIN32_POLL_BATCH  64
#endif

/* ================================================================
 * XNetIoRingWin32 类定义（虚函数表枚举 + 结构体完整定义）
 * 公开 API 与事件上下文类型见 XNetIoRingWin32.h
 * ================================================================ */

/* 虚函数表枚举：继承 XAbstractNetIoRing，不添加新虚函数 */
XCLASS_DEFINE_BEGING(XNetIoRingWin32)
XCLASS_DEFINE_EXTEND_END(XNetIoRingWin32, XAbstractNetIoRing)

typedef struct XNetIoRingWin32 {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    HANDLE  m_iocp;               /**< IOCP 完成端口句柄 */
    bool    m_ownsIocp;           /**< 是否由本实例创建 IOCP（false=使用外部端口） */
    uint8_t m_pad[7];             /**< 对齐填充 */
} XNetIoRingWin32;

/* ==================== 前向声明 ==================== */
static XFd   VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd);
static void  VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * IOCP 完成处理（公共辅助函数）
 * ================================================================ */

/* 将一条 IOCP 完成转换为 CQ 条目或直接投递定时器事件。
 * pollPlatform（非阻塞批量）和 waitForEvents（阻塞单条）共用。 */
static void processOneCompletion(XAbstractNetIoRing* self, BOOL success,
                                  DWORD bytesTransferred, ULONG_PTR completionKey,
                                  LPOVERLAPPED overlapped, DWORD lastError) {
    XEventContext* ctx = (XEventContext*)overlapped;
    XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
    XEventContextCompletionCallback completionCallback = NULL;
    void* completionUserData = NULL;

    if (ctx->type == XEventContextType_Type_Timer) {
        /* 定时器完成：直接投递定时器事件到应用层 */
        if (completionKey) {
            XTimerEvent* timerEv = XTimerEvent_create((XTimerId)ctx->fd);
            XEvent* timerEvent = (XEvent*)timerEv;
            if (timerEvent) {
                timerEvent->posted = true;
                timerEvent->spontaneous = true;
                XCoreApplication_postEvent((XObject*)completionKey,
                                           timerEvent, XEVENT_PRIORITY_NORMAL);
            }
        }
    }
    else if (ctx->type == XEventContextType_Type_Socket ||
             ctx->type == XEventContextType_Type_File) {
        bool shouldDispatch = true;
        completionCallback = ioCtx->completionCallback;
        completionUserData = ioCtx->completionUserData;

        /* Socket/File I/O 完成：转换为 CQ 条目 */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = bytesTransferred;
        cqEntry.m_error = (int)lastError;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;  /* 原生 I/O 完成（区别于 lwIP 的 Source_Netif） */
    cqEntry.m_fdType = XFd_type(ctx->fd);  /* 从 fd 表获取类型（Socket/File） */

        /* IOCP eventMask (FD_*) -> XSocketActType 转换
         * 注意：FD_CONNECT(16) != XSocketAct_Connect(4)，不能直接使用位掩码 */
        {
            uint32_t events = 0;
            if (ioCtx->eventMask & FD_READ)    events |= XSocketAct_Read;     /* FD_READ(1)     -> Read(1)    */
            if (ioCtx->eventMask & FD_WRITE)   events |= XSocketAct_Write;    /* FD_WRITE(2)    -> Write(2)   */
            if (ioCtx->eventMask & FD_CONNECT) events |= XSocketAct_Connect;  /* FD_CONNECT(16) -> Connect(4) */
            if (ioCtx->eventMask & FD_ACCEPT)  events |= XSocketAct_Accept;   /* FD_ACCEPT(8)   -> Accept(8)  */
            cqEntry.m_events = events;
        }

        /* I/O 失败时：连接丢失等错误也标记为 Connect 事件 */
        if (!success && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketAct_Connect;
        }

        ioCtx->finishedBytes = bytesTransferred;

        /* The callback may release the allocation containing ioCtx.  Copy
         * every needed field first and do not touch ioCtx afterwards.  A
         * canceled operation whose owner is being destroyed is consumed
         * here without dispatching an event through a potentially reused fd. */
        if (completionCallback)
            shouldDispatch = completionCallback(ioCtx, completionUserData);
        if (shouldDispatch)
            XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * 重载虚函数实现（仅 IOCP 专属，其余沿用基类默认实现）
 * ================================================================ */

/* 轮询 IOCP 完成端口（非阻塞），将完成事件转换为 CQ 条目 */
static void VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;
    int batchCount = 0;

    if (!win || !win->m_iocp) return;

    /* 非阻塞循环获取 IOCP 完成包 */
    while (batchCount < XNETIORING_WIN32_POLL_BATCH) {
        BOOL success = GetQueuedCompletionStatus(
            win->m_iocp, &bytesTransferred, &completionKey, &overlapped, 0);

        /* 无完成包可取（超时/无事件），退出循环 */
        if (overlapped == NULL) break;

        DWORD lastError = success ? 0 : GetLastError();
        processOneCompletion(self, success, bytesTransferred,
                             completionKey, overlapped, lastError);
        batchCount++;
    }
}

/* 获取平台事件 fd：返回 IOCP 句柄作为事件源标识 */
static XFd VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return XFD_INVALID;
    return (XFd)(intptr_t)win->m_iocp;
}

/* 注册事件源：关联 socket/file HANDLE 到 IOCP 端口 */
static bool VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    XFileDescriptor* desc;

    if (!win || !win->m_iocp) return false;

    desc = XFd_get(fd);
    if (!desc || !desc->handle) return false;

    return CreateIoCompletionPort((HANDLE)desc->handle, win->m_iocp,
                                  (ULONG_PTR)desc->ctx, 0) != NULL;
}

/* 阻塞等待事件（虚函数） */
static void VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;

    if (!win || !win->m_iocp) return;

    DWORD winTimeout = (timeoutMs < 0) ? INFINITE : (DWORD)timeoutMs;
    BOOL success = GetQueuedCompletionStatus(
        win->m_iocp, &bytesTransferred, &completionKey, &overlapped, winTimeout);

    /* overlapped==NULL 表示超时或 wakeUp 唤醒，无完成需处理 */
    if (overlapped == NULL) return;

    DWORD lastError = success ? 0 : GetLastError();
    processOneCompletion(self, success, bytesTransferred,
                         completionKey, overlapped, lastError);
}

/* 唤醒阻塞中的 WaitForEvents（虚函数） */
static void VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return;
    PostQueuedCompletionStatus(win->m_iocp, 0, 0, NULL);
}

/* ================================================================
 * deinit 析构实现
 *
 * 清理顺序：
 *   1. Win32 专属资源（IOCP 端口）
 *   2. 调用基类 deinit（清理 SQ/CQ 队列 + XClass 基类）
 * ================================================================ */
static void VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)obj;

    if (!win) return;

    /* 关闭 IOCP 端口（仅当本实例创建了它） */
    if (win->m_ownsIocp && win->m_iocp) {
        CloseHandle(win->m_iocp);
    }
    win->m_iocp = NULL;
    win->m_ownsIocp = false;

    /* 调用基类 deinit（清理 SQ/CQ 队列 + XClass 基类） */
    XClass_Deinit_Parent(XAbstractNetIoRing, obj);
}

/* ================================================================
 * 虚函数表初始化
 *
 * 继承基类 XAbstractNetIoRing 的虚函数表（含 9 个默认实现），
 * 仅重载 6 个 IOCP 专属虚函数。
 * ================================================================ */
XVtable* XNetIoRingWin32_class_init(void) {
    XVTABLE_INIT_DEFAULT(XNetIoRingWin32)
	XCLASS_SET_CLASS_NAME_DEFAULT("XNetIoRingWin32");
    /* 继承 XAbstractNetIoRing 虚函数表（含默认实现） */
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    /* 重载 IOCP 专属虚函数 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingWin32_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingWin32_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingWin32_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WaitForEvents, VXNetIoRingWin32_waitForEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_WakeUp,        VXNetIoRingWin32_wakeUp);

    /* 重载 deinit */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetIoRingWin32_deinit);

    return XVTABLE_DEFAULT;
}

/* ================================================================
 * 构造与初始化
 * ================================================================ */
void XNetIoRingWin32_init(XNetIoRingWin32* ring) {
    if (!ring) return;

    /* 清零整个子类结构体 */
    memset(ring, 0, sizeof(XNetIoRingWin32));

    /* 初始化基类（设置基类虚函数表 + 创建 SQ/CQ 队列） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 重载为本类的虚函数表（继承基类默认实现 + IOCP 重载） */
    XClassSetVtable(ring, XNetIoRingWin32);

    /* 创建 IOCP 完成端口 */
    ring->m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    ring->m_ownsIocp = (ring->m_iocp != NULL);

    /* 标记启用 */
    ((XAbstractNetIoRing*)ring)->m_enabled = (ring->m_iocp != NULL);
}

XNetIoRingWin32* XNetIoRingWin32_create(void) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XMalloc_System(sizeof(XNetIoRingWin32));
    if (!ring) return NULL;

    XNetIoRingWin32_init(ring);
    Set_Class_MemoryFree(ring, XFree_System);
    return ring;
}

/* ================================================================
 * Win32 专属 API
 * ================================================================ */
HANDLE XNetIoRingWin32_iocpHandle(const XNetIoRingWin32* ring) {
    return ring ? ring->m_iocp : NULL;
}

bool XNetIoRingWin32_assocHandle(XNetIoRingWin32* ring, HANDLE handle,
                                  ULONG_PTR completionKey) {
    if (!ring || !ring->m_iocp || !handle) return false;
    return CreateIoCompletionPort(handle, ring->m_iocp, completionKey, 0) != NULL;
}

bool XNetIoRingWin32_postCompletion(XNetIoRingWin32* ring,
                                     DWORD bytesTransferred,
                                     ULONG_PTR completionKey,
                                     LPOVERLAPPED overlapped) {
    if (!ring || !ring->m_iocp) return false;
    return PostQueuedCompletionStatus(ring->m_iocp, bytesTransferred,
                                      completionKey, overlapped) != 0;
}

/* ================================================================
 * IOCP 兼容包装（供 XNetwork_win32.c / XSerialPortWin32.c 使用）
 * ================================================================ */
HANDLE IOCP_getGlobalPort(void) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XAbstractNetIoRing_global();
    return ring ? XNetIoRingWin32_iocpHandle(ring) : NULL;
}

bool IOCP_bind(XSocketDescriptor socket, XObject* obj) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XAbstractNetIoRing_global();
    if (!ring) return false;
    return XNetIoRingWin32_assocHandle(ring,
        (HANDLE)XSocketDescriptor_toIntptr(socket), (ULONG_PTR)obj);
}

/* ================================================================
 * 平台钩子：创建 Windows IOCP 后端
 * ================================================================ */
XAbstractNetIoRing* XAbstractNetIoRing_createPlatform(void) {
    return (XAbstractNetIoRing*)XNetIoRingWin32_create();
}

#endif /* XPLATFORM_WINDOWS */

#endif /* XAbstractNetIoRing_ON */
