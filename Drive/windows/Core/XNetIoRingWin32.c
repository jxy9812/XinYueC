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
#include "XDevice.h"            /* XDevice_class / XDeviceType（按类型取真实句柄）。 */
#include "XDeviceNetwork.h"     /* XDeviceNetwork_socketDescriptor。 */
#include <winsock2.h>
#include "XNetIoRingWin32.h"
#include <string.h>
#include <stdlib.h>

/* 网络设备后端提供的平台钩子（不属于 XDeviceNetwork 公共 API；
 * 实现在 Drive/windows/Device/Network/ 的两套后端之一）。 */
intptr_t XDeviceNetwork_socketDescriptor(XFd xfd);

/* 单次 pollPlatform 最大处理的 IOCP 完成数，防止饿死其他任务 */
#ifndef XNETIORING_WIN32_POLL_BATCH
#define XNETIORING_WIN32_POLL_BATCH  64
#endif

/* 套接字就绪轮询表容量（`select` 模型）。
 * 取值依据：`XFD_TABLE_SIZE` 默认 128 —— 同时存在的套接字数不可能超过
 * fd 表容量，故 128 是天然上界。表里只存「属主上下文指针」，不持任何
 * 内核对象，注册/注销都是 O(n) 数组操作。 */
#ifndef XNETIORING_WIN32_MAX_WSA_EVENTS
#define XNETIORING_WIN32_MAX_WSA_EVENTS  128
#endif

/* ================================================================
 * XNetIoRingWin32 类定义（虚函数表枚举 + 结构体完整定义）
 * 公开 API 与事件上下文类型见 XNetIoRingWin32.h
 * ================================================================ */

/* 虚函数表枚举：继承 XAbstractNetIoRing，不添加新虚函数 */
XCLASS_DEFINE_BEGING(XNetIoRingWin32)
XCLASS_DEFINE_EXTEND_END(XNetIoRingWin32, XAbstractNetIoRing)

/* ================================================================
 * 套接字就绪轮询表（select 模型）
 * ================================================================ */

/**
 * @brief 一个已登记的就绪轮询项（只存属主指针，不持内核对象）。
 * @details 仅 `XNETWORK_WIN_SOCKET_WSAEVENT` 构建会用到；IOCP 构建下
 *          数组长度仍是常量（零动态分配），只是永远为空。
 *
 *          **必须直接保存属主上下文指针**，不能只存 XFd 再反查：
 *          `XFd` 是 `XFixedPool` 的池槽位号，会随连接关闭被下一个连接
 *          复用（实测同一轮测试里 fd=0 先后对应 5 个不同 socket）。就绪
 *          回调若按 fd 反查，会把事件派给「刚复用该槽位的新连接」——
 *          表现为串包/丢包，且极难定位。Qt 的 `QEventDispatcherWin32`
 *          同样是用 notifier 的 `wp` 直接寻址而非 fd 反查。
 */
typedef struct XNetIoRingWsaSlot {
    XFd      m_fd;      /**< 该套接字对应的框架句柄（仅诊断用）。 */
    void*    m_owner;   /**< 属主上下文指针；NULL = 空槽。 */
} XNetIoRingWsaSlot;

typedef struct XNetIoRingWin32 {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    HANDLE  m_iocp;               /**< IOCP 完成端口句柄 */
    bool    m_ownsIocp;           /**< 是否由本实例创建 IOCP（false=使用外部端口） */

    /* 套接字就绪轮询表（select 模型，WSAEventSelect 后端使用）。 */
    XNetIoRingWsaSlot m_wsaSlots[XNETIORING_WIN32_MAX_WSA_EVENTS];
    volatile LONG     m_wsaCount;   /**< 已登记槽数（跨线程读，用 LONG 保证对齐）。 */

    uint8_t m_pad[7];             /**< 对齐填充 */
} XNetIoRingWin32;

/* 前向声明
 * ================================================================ */

/* 就绪轮询的实现见下方（本文件内）。 */
static void pollWsaEvents(XNetIoRingWin32* win);
static bool anyWsaEventSignaled(XNetIoRingWin32* win);

/* 套接字后端的就绪分派（定义在 XDeviceNetwork_win32_select.c）。
 * IOCP 构建下不编译该通道、也不登记任何套接字，槽表恒空，
 * 本声明只为让「WSAEventSelect 构建」单侧可见——用编译期宏判定，
 * 不依赖链接期弱符号（后者在 MSVC/Clang 之间行为不一致）。 */
#if XNETWORK_WIN_SOCKET_WSAEVENT
void XDeviceNetwork_win32_selectDispatchReady(void* owner);
#endif

/* ==================== 前向声明 ==================== */
static XFd   VXNetIoRingWin32_getEventFd(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_pollPlatform(XAbstractNetIoRing* self);
static bool  VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd);
static bool  VXNetIoRingWin32_registerPassiveOsFd(XAbstractNetIoRing* self,
                                                   int osFd,
                                                   void (*notify)(int osFd));
static bool  VXNetIoRingWin32_unregisterPassiveOsFd(XAbstractNetIoRing* self,
                                                     int osFd);
static void  VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs);
static void  VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self);
static void  VXNetIoRingWin32_deinit(XAbstractNetIoRing* obj);

/* ================================================================
 * WSA 就绪事件通道（WSAEventSelect 套接字后端）
 *
 * 设计要点：
 *   1. 事件对象数量可超过 `MAXIMUM_WAIT_OBJECTS`（64），因此**不能**
 *      直接把所有 WSAEVENT 塞进 WaitForMultipleObjects。
 *   2. 这里用「零等待轮询 + 有事件即唤醒主等待」的两段式：
 *      - 等待集里只放 IOCP 端口（主源）+ 一个自建「中转事件」；
 *      - 登记 WSA 事件时零等待扫一遍，若已置位则立即把中转事件置信号态；
 *      - 每次 PollPlatform/WaitForEvents 返回时再扫一遍，把就绪的
 *        事件逐个回调属主（属主自己 WSAEnumNetworkEvents 复位）。
 *   3. 这样每个套接字就绪都能唤醒主循环（通过中转事件），但等待点始终
 *      只有一个，符合「不新增线程、不新增等待集」的既有约束。
 *   4. 中转事件在本文件自建、自管：不依赖任何第三方事件对象。
 * ================================================================ */

/**
 * @brief 对全部已登记套接字做一次 `select` 就绪查询，就绪者回调其属主。
 * @details 与 Qt `QNativeSocketEnginePrivate::nativeSelect`（qns_win.cpp:1495）
 *          同一机制：**直接问内核**，不依赖任何事件对象的置位历史。这消除了
 *          `WSAEventSelect` + 事件对象在本进程里「从不置位」的故障面。
 *
 *          就绪判断与后续读取由属主自己做（`XDeviceNetwork_win32_selectDispatchReady`
 *          内部先 select 再 recv），本环只负责「到点问一遍」。
 */
static void pollWsaEvents(XNetIoRingWin32* win)
{
#if XNETWORK_WIN_SOCKET_WSAEVENT
    LONG count;
    LONG i;

    if (!win) return;
    count = win->m_wsaCount;
    if (count <= 0) return;
    if (count > XNETIORING_WIN32_MAX_WSA_EVENTS)
        count = XNETIORING_WIN32_MAX_WSA_EVENTS;

    for (i = 0; i < count; ++i) {
        XNetIoRingWsaSlot* slot = &win->m_wsaSlots[i];
        void* owner = slot->m_owner;
        if (!owner) continue;
        /* 属主自己做 select + 收发；未就绪时它内部立即返回。 */
        XDeviceNetwork_win32_selectDispatchReady(owner);
    }
#else
    (void)win;
#endif
}

/**
 * @brief 是否有任一已登记套接字处于就绪状态。
 * @details `WSAEventSelect` 模型下这里查事件对象置位；`select` 模型下等价
 *          于「对全部套接字做一次零超时 select」。为避免与 pollWsaEvents
 *          重复查询，这里直接返回「表非空」——就绪判定交给 poll 里的属主
 *          回调完成（多一次零超时 select 与少一次对主循环无实质影响，
 *          而省掉一次重复系统调用更划算）。
 */
static bool anyWsaEventSignaled(XNetIoRingWin32* win)
{
#if XNETWORK_WIN_SOCKET_WSAEVENT
    return win && win->m_wsaCount > 0;
#else
    (void)win;
    return false;
#endif
}

/**
 * @brief 阻塞等待：IOCP 端口 + WSA 事件双源。
 * @param timeoutMs 超时；负值表示按 IOCP 语义无限等待，但 WSA 侧需要
 *                  周期性轮询（事件对象无法加入等待集），故无限等待
 *                  退化为分片等待。
 * @details WSA 侧分片间隔取 10ms：与框架主循环 20ms 心跳同量级，既保证
 *          就绪后能在半个心跳内被看见，又不会把 CPU 拉满。分片只在
 *          「有 WSA 事件登记」时启用；纯 IOCP 构建的阻塞语义逐位不变。
 */
#define XNETIORING_WSA_POLL_SLICE_MS 10

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
                timerEvent->m_posted = true;
                timerEvent->m_spontaneous = true;
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
        /* [2026-11-04 假 EOF 复核必备] 填真实 OS 套接字句柄。
         * 此前 Win32 后端**从未填过该字段**（memset 后恒为 0），于是
         * Src/ 侧假 EOF 复核把 PEEK 打到 fd 0（stdin）上——stdin 读到 0，
         * 一切 0 字节读完成都被判成"真对端 FIN"，对端健在的链路被误断
         * （实测：服务端 socket 莫名 Unconnected 而客户端从未断开，导致
         * 会话被复位、切页订阅全丢、推送积压）。语义与 POSIX 对齐：
         * 流式套接字填真实句柄；非套接字/无句柄填 -1（不适用关闭判定）。 */
        cqEntry.m_osFd = (ctx->type == XEventContextType_Type_Socket)
                             ? (int)XSocketDescriptor_toIntptr(ioCtx->socket)
                             : -1;
        cqEntry.m_bytes = bytesTransferred;
        cqEntry.m_error = (int)lastError;
        if (ctx->type == XEventContextType_Type_Socket)
        cqEntry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;  /* 原生 I/O 完成（区别于 lwIP 的 Source_Netif） */
    cqEntry.m_fdType = XFd_type(ctx->fd);  /* 从 fd 表获取类型（Socket/File） */

        /* IOCP eventMask (FD_*) -> XSocketActType 转换
         * 注意：FD_CONNECT(16) != XSocketAct_Connect(4)，不能直接使用位掩码 */
        {
            uint32_t events = 0;
            if (ioCtx->eventMask & FD_READ)    events |= XSocketActType_Read;     /* FD_READ(1)     -> Read(1)    */
            if (ioCtx->eventMask & FD_WRITE)   events |= XSocketActType_Write;    /* FD_WRITE(2)    -> Write(2)   */
            if (ioCtx->eventMask & FD_CONNECT) events |= XSocketActType_Connect;  /* FD_CONNECT(16) -> Connect(4) */
            if (ioCtx->eventMask & FD_ACCEPT)  events |= XSocketActType_Accept;   /* FD_ACCEPT(8)   -> Accept(8)  */
            cqEntry.m_events = events;
        }

        /* I/O 失败时：连接丢失等错误也标记为 Connect 事件 */
        if (!success && cqEntry.m_events == 0) {
            cqEntry.m_events = XSocketActType_Connect;
        }

        ioCtx->finishedBytes = bytesTransferred;
        ioCtx->nativeError = success ? ERROR_SUCCESS : lastError;
        ioCtx->completed = success != FALSE;

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

    /* [2026-11-04] 先扫 WSA 就绪事件：WSAEventSelect 套接字后端不使用
     * 完成包，就绪只体现为事件对象置位。放在 IOCP 之前，保证同一次
     * pollPlatform 内网络就绪不被 IO 完成批量挤到下一轮。 */
    pollWsaEvents(win);

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

/* 注册事件源：关联 socket/file HANDLE 到 IOCP 端口。
 *
 * @note [2026-11-04 结构性错误修正] 旧实现把 `desc->m_deviceCtx` 直接强制
 *       转换当 HANDLE 传给 CreateIoCompletionPort。但按 XFileDescriptor.h
 *       的字段约定，`m_deviceCtx` 保存的是 **XDeviceContext\***（设备打开
 *       上下文指针），**不是内核句柄**：
 *
 *         "m_deviceCtx 统一保存设备打开上下文：XDevice 流程保存 Open 虚函数
 *          返回的 XDeviceContext*（借用）"
 *
 *       把结构体指针当句柄传，结果只有两种：CreateIoCompletionPort 失败
 *       （无害），或**碰巧**落在一个有效句柄值上——那会把一个毫不相关的
 *       内核对象挂进本完成端口，此后该端口的完成包来源被污染，属于典型的
 *       "读完成时有时无"类故障的成因。
 *
 *       正确做法是按设备类型取真实 OS 句柄：网络设备走
 *       `XDeviceNetwork_socketDescriptor`（内部即平台 SOCKET/HANDLE）。
 *       非网络类型本函数不再尝试关联——文件类设备若需要 IOCP，应由其自身
 *       后端在打开时完成关联（与网络后端 `iocp_assoc` 同口径）。 */
static bool VXNetIoRingWin32_registerEvent(XAbstractNetIoRing* self, XFd fd) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    XFileDescriptor* desc;
    intptr_t osHandle = -1;

    if (!win || !win->m_iocp) return false;

    desc = XFd_get(fd);
    if (!desc) return false;
    if (desc->m_type == XFD_TYPE_CONSOLE) return false;

    {
        XDevice* device = XDevice_class(fd);
        if (!device || device->m_type != XDeviceType_Socket)
            return false;   /* 非网络设备：句柄语义因设备而异，不在此猜测。 */
        osHandle = XDeviceNetwork_socketDescriptor(fd);
    }
    if (osHandle < 0) return false;

    /* 已完成关联的句柄重复关联会失败（返回 NULL，ERROR_INVALID_PARAMETER）；
     * 网络后端在 open 时已经关联过一次，这里的失败属正常幂等情形。 */
    return CreateIoCompletionPort((HANDLE)osHandle, win->m_iocp,
                                  (ULONG_PTR)desc->object, 0) != NULL;
}

/* 注册裸 OS fd 为被动可读事件源：Windows 无等价原语，恒不支持。
 *
 * IOCP 只接纳与 OVERLAPPED I/O 绑定的 HANDLE，不存在「把一个只读设备 fd
 * 挂进完成端口等待集」的原语；Windows 的输入侧本就由窗口消息驱动
 * （WM_POINTER / WM_MOUSE），经 XPlatformNativeWindow 的事件泵进入框架，
 * 不需要本通道。返回 false ⇒ 调用方（fbdev 触摸泵等）保留轮询兜底路径，
 * Windows 行为与本 API 引入前逐位一致。 */
static bool VXNetIoRingWin32_registerPassiveOsFd(XAbstractNetIoRing* self,
                                                  int osFd,
                                                  void (*notify)(int osFd)) {
    (void)self;
    (void)osFd;
    (void)notify;
    return false;
}

/* 注销裸 OS fd 被动事件源：Windows 从未注册（见上），恒幂等成功。 */
static bool VXNetIoRingWin32_unregisterPassiveOsFd(XAbstractNetIoRing* self,
                                                    int osFd) {
    (void)self;
    (void)osFd;
    return true;
}

/* 阻塞等待事件（虚函数） */
static void VXNetIoRingWin32_waitForEvents(XAbstractNetIoRing* self, int timeoutMs) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;
    DWORD winTimeout;

    if (!win || !win->m_iocp) return;

    /* [2026-11-04 WSA 就绪通道] 有 WSA 事件登记时，等待必须分片：
     * 事件对象无法挂进 GetQueuedCompletionStatus 的等待集，只能周期性
     * 检查。分片间隔取 10ms（半个主循环心跳），既保证就绪后能被及时
     * 看见，又不会把空闲 CPU 拉起来。无登记时语义与改动前逐位一致
     * （一次等待到底，IOCP 完成包即时唤醒）。 */
    if (win->m_wsaCount > 0) {
        int remaining = timeoutMs;
        for (;;) {
            DWORD slice;
            BOOL success;
            if (remaining >= 0 && remaining <= XNETIORING_WSA_POLL_SLICE_MS)
                slice = (DWORD)remaining;
            else
                slice = (DWORD)XNETIORING_WSA_POLL_SLICE_MS;

            success = GetQueuedCompletionStatus(
                win->m_iocp, &bytesTransferred, &completionKey, &overlapped, slice);
            if (overlapped != NULL) {
                DWORD lastError = success ? 0 : GetLastError();
                processOneCompletion(self, success, bytesTransferred,
                                     completionKey, overlapped, lastError);
                return;
            }
            /* 分片到点：检查 WSA 就绪；有就绪即视为本次等待已获事件。 */
            if (anyWsaEventSignaled(win)) {
                pollWsaEvents(win);
                return;
            }
            if (remaining >= 0) {
                remaining -= (int)slice;
                if (remaining <= 0) return;
            }
        }
    }

    winTimeout = (timeoutMs < 0) ? INFINITE : (DWORD)timeoutMs;
    {
        BOOL success = GetQueuedCompletionStatus(
            win->m_iocp, &bytesTransferred, &completionKey, &overlapped, winTimeout);

        /* overlapped==NULL 表示超时或 wakeUp 唤醒，无完成需处理 */
        if (overlapped == NULL) return;

        DWORD lastError = success ? 0 : GetLastError();
        processOneCompletion(self, success, bytesTransferred,
                             completionKey, overlapped, lastError);
    }
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
    /* 继承 XAbstractNetIoRing 虚函数表（含默认实现） */
    XVTABLE_INHERIT_XCLASS(XAbstractNetIoRing);

    /* 重载 IOCP 专属虚函数 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_GetEventFd,    VXNetIoRingWin32_getEventFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_PollPlatform,  VXNetIoRingWin32_pollPlatform);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterEvent, VXNetIoRingWin32_registerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_RegisterPassiveOsFd, VXNetIoRingWin32_registerPassiveOsFd);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractNetIoRing_UnregisterPassiveOsFd, VXNetIoRingWin32_unregisterPassiveOsFd);
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

    /* WSA 事件槽初始化为空（memset 已把 m_event 清零，这里只补 fd）。 */
    {
        int i;
        for (i = 0; i < XNETIORING_WIN32_MAX_WSA_EVENTS; ++i)
            ring->m_wsaSlots[i].m_fd = XFD_INVALID;
    }
    ring->m_wsaCount = 0;

    /* 标记启用 */
    ((XAbstractNetIoRing*)ring)->m_enabled = (ring->m_iocp != NULL);
}

XNetIoRingWin32* XNetIoRingWin32_create_ex(XMemoryType memory) {
    XNetIoRingWin32* ring = (XNetIoRingWin32*)XMemory_malloc(sizeof(XNetIoRingWin32), memory);
    if (!ring) return NULL;

    XNetIoRingWin32_init(ring);
    Set_Class_Memory(ring, memory); Set_Class_IsHeap(ring, true);
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
 * WSA 就绪事件通道（公开 API）
 *
 * 登记/注销都在事件循环线程上被调用（套接字打开/关闭都在主线程），
 * 因此槽表本身不需要额外加锁；`m_wsaCount` 用 LONG 只为了让等待循环
 * 读到自洽的值（不会被编译器跨语句缓存）。
 * ================================================================ */

/**
 * @brief 把一个套接字上下文登记进就绪轮询表（`select` 模型）。
 * @param ring  本事件环实例指针。
 * @param fd    该套接字对应的框架句柄 XFd（仅诊断用）。
 * @param owner 属主上下文指针；不能为 NULL。
 * @return 登记成功返回 true；表满或参数非法返回 false。
 * @note 幂等：同一 owner 重复登记只更新 fd。**不持任何内核对象**，
 *       因此没有关闭/泄漏问题；注销只需 removeWsaEvent(owner)。
 */
bool XNetIoRingWin32_addWsaEvent(XNetIoRingWin32* ring, XFd fd, void* owner) {
    LONG count;
    LONG i;

    if (!ring) return false;
    /* 属主指针必须有效：就绪时要直接回调它（不按 fd 反查，见槽结构注释）。 */
    if (!owner) return false;

    count = ring->m_wsaCount;
    if (count > XNETIORING_WIN32_MAX_WSA_EVENTS)
        count = XNETIORING_WIN32_MAX_WSA_EVENTS;

    /* 幂等：同一 owner 已在表内则只更新 fd。
     * 注意不能按 fd 匹配——XFd 是池槽位号，前一个连接关闭后会被新连接
     * 复用；按 fd 覆盖会把旧连接的属主留在表里（已释放）或漏登记新连接。 */
    for (i = 0; i < count; ++i) {
        if (ring->m_wsaSlots[i].m_owner == owner) {
            ring->m_wsaSlots[i].m_fd = fd;
            return true;
        }
    }
    /* 补空洞优先（关闭的连接会把槽置空）。 */
    for (i = 0; i < XNETIORING_WIN32_MAX_WSA_EVENTS; ++i) {
        if (!ring->m_wsaSlots[i].m_owner) {
            ring->m_wsaSlots[i].m_owner = owner;
            ring->m_wsaSlots[i].m_fd = fd;
            if (i >= count)
                InterlockedIncrement(&ring->m_wsaCount);
            return true;
        }
    }
    return false;   /* 表满：调用方仍可用（只是没有阻塞唤醒源）。 */
}

/**
 * @brief 从就绪轮询表移除套接字上下文。
 * @param ring  本事件环实例指针。
 * @param owner 此前登记的属主上下文指针。
 * @return 无。未登记过时为空操作（幂等）。
 * @note 按 **owner 指针**匹配（不是 fd，也不是事件对象）：owner 在同一
 *       时刻唯一，能消除 fd 复用带来的歧义。
 */
void XNetIoRingWin32_removeWsaEvent(XNetIoRingWin32* ring, void* owner) {
    LONG i;

    if (!ring || !owner) return;
    for (i = 0; i < XNETIORING_WIN32_MAX_WSA_EVENTS; ++i) {
        if (ring->m_wsaSlots[i].m_owner == owner) {
            ring->m_wsaSlots[i].m_owner = NULL;
            ring->m_wsaSlots[i].m_fd = XFD_INVALID;
            return;
        }
    }
}

/* ================================================================
 * IOCP 兼容包装（供 XDeviceNetwork_win32.c / XSerialPortWin32.c 使用）
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

/* ================================================================
 * 平台钩子：复核「0 字节读完成」是否真是对端 FIN（假 EOF 规避）
 * ================================================================
 * [2026-11-04 本机 IOCP 环回实测] 服务端 socket 在链路健在时被判成
 * Unconnected（客户端全程未断开、无任何断开日志），后果是会话被复位、
 * 客户端切页订阅全部丢失、服务端推送帧积压在 tx 队列里发不出去。
 * 这与 POSIX 侧 2026-10-16 记录的昆仑通态定制内核假 EOF 属同一类现象，
 * 故判定口径与 POSIX 完全一致（判据文档见
 * XAbstractNetIoRing_platformPeerReallyClosed 的声明处）。
 *
 * Windows 侧实现要点：
 *   - winsock 的 recv 没有 MSG_DONTWAIT；非阻塞由 ioctlsocket(FIONBIO)
 *     控制。这里临时置非阻塞、PEEK 读 1 字节、随后复原，避免长期改变
 *     该 fd 的语义（它可能仍被设备层使用）。
 *   - recv 返回 SOCKET_ERROR 时：WSAEWOULDBLOCK = 连接活着（假 EOF）；
 *     其余错误（WSAENOTSOCK/WSAENOTCONN 等）同样不当作对端 FIN。
 *   - recv 返回 0 → 真 FIN。
 */
bool XAbstractNetIoRing_platformPeerReallyClosed(int osFd) {
    SOCKET s = (SOCKET)osFd;
    unsigned long nonBlocking = 1;
    char peekByte = 0;
    int pr;

    if (osFd < 0)
        return true; /* 无 OS fd 可查：保持"0 字节即关闭"的保守语义。 */

    /* 临时切非阻塞：链路存活但无数据时 PEEK 不能阻塞事件循环。
     * ioctlsocket 失败即放弃复核，按"真关闭"保守处理（不引入新语义）。 */
    if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
        return true;

    pr = (int)recv(s, &peekByte, 1, MSG_PEEK);
    /* 复原：本框架套接字的收发全走 IOCP 重叠 I/O，fd 的阻塞标志不参与
     * 收发路径；这里统一恢复为阻塞（与创建时一致）。 */
    nonBlocking = 0;
    (void)ioctlsocket(s, FIONBIO, &nonBlocking);

    if (pr == 0)
        return true;                       /* 真 FIN */
    if (pr == SOCKET_ERROR)
        return false;                      /* EWOULDBLOCK/其他错误：非对端 FIN */
    return false;                          /* 有数据待读：更非关闭 */
}

#endif /* XPLATFORM_WINDOWS */

#endif /* XAbstractNetIoRing_ON */
