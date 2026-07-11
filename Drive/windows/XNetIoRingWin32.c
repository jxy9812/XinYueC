/**
 * @file XNetIoRingWin32.c
 * @brief XAbstractNetIoRing Windows IOCP 后端实现
 *
 * 继承 XAbstractNetIoRing，通过重载虚函数包装 Windows IOCP socket/file 异步 I/O。
 * 本文件仅处理 IOCP 相关逻辑，不涉及 lwIP pcap 轮询（由 XNetIoRing_lwip.c 提供）。
 *
 * 核心流程：
 *   pollPlatform:
 *     GetQueuedCompletionStatus(timeout=0) 循环
 *       -> XEventContext_IOCP 提取 eventMask / fd / bytes
 *       -> 转换为 CQ 条目推入 CQ
 *
 *   dispatchCQEntry:
 *     从 CQ 条目创建 XEventSockAct / XEventSockClose
 *       -> XCoreApplication_postEvent 投递到应用层
 *       -> 0 字节 + Read + Socket 类型 => XEventSockClose（对端 FIN）
 *
 *   waitForEvents:
 *     GetQueuedCompletionStatus(timeout) 阻塞等待
 *       -> wakeUp 通过 PostQueuedCompletionStatus(NULL overlapped) 唤醒
 *
 * 虚函数表：
 *   由于 XAbstractNetIoRing 是纯虚类（无 class_init），本类直接从 XClass 继承，
 *   然后添加全部 9 个虚函数。省去了一层基类虚函数表，节约内存。
 */

#include "CXinYueConfig.h"
#if XAbstractNetIoRing_ON

#ifdef _WIN32

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
#include "IOCPInfo.h"
#include "XSocketDescriptor.h"
#include <winsock2.h>
#include <string.h>
#include <stdlib.h>

/* 单次 pollPlatform 最大处理的 IOCP 完成数，防止饿死其他任务 */
#ifndef XNETIORING_WIN32_POLL_BATCH
#define XNETIORING_WIN32_POLL_BATCH  64
#endif

/* ================================================================
 * XNetIoRingWin32 类定义（原 XNetIoRingWin32.h 内容合并至此）
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
static XFd VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self);
static void VXNetIoRingWin32_processSource(XAbstractNetIoRing* self,
                                            const XAbstractNetIoRing_SQEntry* entry);
static void VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self);
static bool VXNetIoRingWin32_hasPendingInput(const XAbstractNetIoRing* self);
static bool VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd);
static bool VXNetIoRingWin32_unregisterEvent(XAbstractNetIoRing* self, XFd fd);
static void VXNetIoRingWin32_dispatchCQEntry(XAbstractNetIoRing* self,
                                              const XAbstractNetIoRing_CQEntry* entry);
static void VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self);
static void VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj);

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

    if (ctx->type == XEventContextType_Type_Timer) {
        /* 定时器完成：直接投递定时器事件到应用层 */
        if (completionKey) {
            XEventTimer* timerEv = XEventTimer_create((XTimerId)ctx->fd);
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
        /* Socket/File I/O 完成：转换为 CQ 条目 */
        XAbstractNetIoRing_CQEntry cqEntry;
        memset(&cqEntry, 0, sizeof(cqEntry));

        cqEntry.m_fd = ctx->fd;
        cqEntry.m_bytes = bytesTransferred;
        cqEntry.m_error = (int)lastError;
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_IOCP;  /* IOCP 完成来源（区别于 lwIP 的 Source_Netif） */

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
        XAbstractNetIoRing_pushCompletion(self, &cqEntry);
    }
}

/* ================================================================
 * 默认虚函数实现
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

/* 处理 SQ 条目：IOCP 后端无额外源处理（完成已在 pollPlatform 中直接推入 CQ） */
static void VXNetIoRingWin32_processSource(XAbstractNetIoRing* self,
                                            const XAbstractNetIoRing_SQEntry* entry) {
    (void)self;
    (void)entry;
}

/* 获取平台事件 fd：返回 IOCP 句柄作为事件源标识 */
static XFd VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return XFD_INVALID;
    return (XFd)(intptr_t)win->m_iocp;
}

/* 检查是否有待处理输入 */
static bool VXNetIoRingWin32_hasPendingInput(const XAbstractNetIoRing* self) {
    if (!self) return false;
    if (self->m_sq && !XLockFreeQueue_isEmpty_base(self->m_sq))
        return true;
    return false;
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

/* 注销事件源：IOCP 不支持显式注销，关闭 HANDLE 即可 */
static bool VXNetIoRingWin32_unregisterEvent(XAbstractNetIoRing* self, XFd fd) {
    (void)self;
    (void)fd;
    return true;
}

/* 分发 CQ 完成事件到应用层 */
static void VXNetIoRingWin32_dispatchCQEntry(XAbstractNetIoRing* self,
                                              const XAbstractNetIoRing_CQEntry* entry) {
    XFileDescriptor* desc;
    XObject* owner;

    if (!self || !entry) return;

    desc = XFd_get(entry->m_fd);
    if (!desc || !desc->ctx) return;
    owner = (XObject*)desc->ctx;

    /* 0 字节 + Read 事件 + Socket 类型 => 对端 FIN，发送关闭事件 */
    if (entry->m_bytes == 0 &&
        (entry->m_events & XSocketAct_Read) &&
        entry->m_sourceType == XAbstractNetIoRing_Source_IOCP) {
        XEventSockClose* closeEvent = XEventSockClose_create(entry->m_fd);
        if (closeEvent) {
            ((XEvent*)closeEvent)->posted = true;
            ((XEvent*)closeEvent)->spontaneous = true;
            XCoreApplication_postEvent(owner, (XEvent*)closeEvent,
                                       XEVENT_PRIORITY_NORMAL);
        }
        return;
    }

    /* 常规事件：创建 XEventSockAct 投递到应用层 */
    if (entry->m_events != 0) {
        XEventSockAct* ev = XEventSockAct_create(entry->m_fd,
                                                   (XSocketActType)entry->m_events);
        if (ev) {
            ((XEvent*)ev)->posted = true;
            ((XEvent*)ev)->spontaneous = true;
            XCoreApplication_postEvent(owner, (XEvent*)ev,
                                       XEVENT_PRIORITY_NORMAL);
        }
    }
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
 * 由于 XAbstractNetIoRing 是纯虚类（无 deinit），本函数直接清理：
 *   1. Win32 专属资源（IOCP 端口）
 *   2. 基类 SQ/CQ 队列（通过 XAbstractNetIoRing_cleanupQueues）
 *   3. XClass 基类
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

    /* 清理 SQ/CQ 队列（基类纯虚，无 deinit，需手动清理） */
    XAbstractNetIoRing_cleanupQueues(obj);

    /* 调用 XClass 基类 deinit */
    XClass_Deinit_Parent(XClass, (XClass*)obj);
}

/* ================================================================
 * 虚函数表初始化
 *
 * 纯虚基类无 class_init，本类直接从 XClass 继承并添加全部 9 个虚函数。
 * ================================================================ */
XVtable* XNetIoRingWin32_class_init(void) {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XNetIoRingWin32))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    /* 继承 XClass 虚函数表（Copy/Move/Deinit） */
    XVTABLE_INHERIT_XCLASS(XClass);

    /* 添加全部 9 个虚函数（XAbstractNetIoRing 的虚函数） */
    void* table[] = {
        VXNetIoRingWin32_getEventFd,
        VXNetIoRingWin32_processSource,
        VXNetIoRingWin32_pollPlatform,
        VXNetIoRingWin32_hasPendingInput,
        VXNetIoRingWin32_registerEvent,
        VXNetIoRingWin32_unregisterEvent,
        VXNetIoRingWin32_dispatchCQEntry,
        VXNetIoRingWin32_waitForEvents,
        VXNetIoRingWin32_wakeUp
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

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

    /* 初始化基类（创建 SQ/CQ 队列，不设置虚函数表） */
    XAbstractNetIoRing_init((XAbstractNetIoRing*)ring);

    /* 设置本类的虚函数表 */
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

#endif /* _WIN32 */

#endif /* XAbstractNetIoRing_ON */
