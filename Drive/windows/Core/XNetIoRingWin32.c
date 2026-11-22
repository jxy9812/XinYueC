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
 *     纯 IOCP（无套接字登记）：GetQueuedCompletionStatus(timeout) 阻塞等待
 *     select 模型（WSAASYNC）+ 有套接字登记：MsgWaitForMultipleObjectsEx
 *       双源等待（IOCP 端口 + 本线程消息队列），任一源就绪立即唤醒
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
 * @details 仅 `XNETWORK_WIN_SOCKET_SELECT` 构建会用到（现即 WSAASYNC，
 *          WSAEVENT 实现已移除）；IOCP 构建下数组长度仍是常量（零动态
 *          分配），只是永远为空。
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

/* [MSGDIAG 临时诊断] 槽建/删计数（并发偶发排查用，定位后移除）。 */
typedef struct XNetIoRingMsgSlot {
    SOCKET m_socket;              /**< 登记的 socket */
    int    m_inUse;               /**< 1 = 槽有效（不依赖 socket 数值判空） */
    long   m_events;              /**< 累计的 FD_* 位 */
    int    m_errors[FD_MAX_EVENTS]; /**< 按 FD_*_BIT 索引的 WSA 错误码 */
} XNetIoRingMsgSlot;

typedef struct XNetIoRingWin32 {
    XAbstractNetIoRing m_class;   /**< 基类（必须位于第一位） */
    HANDLE  m_iocp;               /**< IOCP 完成端口句柄 */
    bool    m_ownsIocp;           /**< 是否由本实例创建 IOCP（false=使用外部端口） */
    volatile LONG m_wakePending;  /**< 端口里是否已有未取走的唤醒包（0/1，防洪水） */

    /* 套接字就绪轮询表（select 模型，WSAAsyncSelect 后端使用）。 */
    XNetIoRingWsaSlot m_wsaSlots[XNETIORING_WIN32_MAX_WSA_EVENTS];
    volatile LONG     m_wsaCount;   /**< 已登记槽数（跨线程读，用 LONG 保证对齐）。 */

    /* [窗口消息事件源 2026-11-21] message-only 隐藏窗口 + FD_* 位槽。
 *  Windows 上平台等待集由两类内核源构成：完成包（IOCP）与窗口消息。
 *  本 ring 作为等待集同时管理两者：WSAAsyncSelect 的 FD_* 事件由隐藏
 *  窗口的 WndProc 缓存进 m_msgSlots，pollPlatform 每轮泵送；未来其它
 *  窗口消息组件（设备变更/热键/电源）经 registerWindowSource 挂靠，
 *  不得各自另建窗口（进程一个 ring 一个窗口）。
 *  生命周期：init 清零；窗口在首次 msgWindow()/pumpMsgOnce() 惰性创建
 *  （建在事件循环线程，线程亲和）；deinit 销毁。 */
    HWND   m_msgHwnd;             /**< message-only 隐藏窗口；NULL=未建 */
    SRWLOCK m_msgLock;            /**< 槽表/窗口锁（窗口线程为主，SRW 兜底） */
    XNetIoRingMsgSlot m_msgSlots[XNETIORING_WIN32_MAX_WSA_EVENTS];
    int    m_msgSourceCount;      /**< 通用窗口消息源注册数 */
    struct XNetIoRingMsgSource {
        UINT  msg;                                /**< 感兴趣的窗口消息 */
        void (*cb)(void* ud, HWND hwnd, WPARAM wp, LPARAM lp); /**< 回调 */
        void* ud;                                 /**< 回调上下文 */
    } m_msgSources[8];            /**< 通用窗口消息源注册表 */

    uint8_t m_pad[7];             /**< 对齐填充 */
} XNetIoRingWin32;

/* 前向声明
 * ================================================================ */

/* 就绪轮询的实现见下方（本文件内）。 */
static void pollWsaEvents(XNetIoRingWin32* win);
static bool anyWsaEventSignaled(XNetIoRingWin32* win);

/* 窗口消息事件源（实现在本文件后部「窗口消息事件源」段）。 */
static void xnetio_ensureMsgWindow(XNetIoRingWin32* win);
static int  xnetio_pumpMsgOnce(XNetIoRingWin32* win);
static void xnetio_shutdownMsgWindow(XNetIoRingWin32* win);

/* 套接字后端的就绪分派（定义在 XDeviceNetwork_win32_select.c）。
 * IOCP 构建下不编译该通道、也不登记任何套接字，槽表恒空，
 * 本声明只为让「select 模型构建」（WSAASYNC）单侧可见——
 * 用编译期宏判定，不依赖链接期弱符号（后者在 MSVC/Clang 之间行为不一致）。 */
#if XNETWORK_WIN_SOCKET_SELECT
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
 * WSA 就绪事件通道（select 模型套接字后端：WSAAsyncSelect）
 *
 * 设计要点：
 *   1. 就绪源是窗口消息位（WSAAsyncSelect → WndProc 缓存进本 ring 的
 *      消息槽），数量可超过 `MAXIMUM_WAIT_OBJECTS`（64），因此**不能**
 *      直接塞进 WaitForMultipleObjects。
 *   2. 这里用「轮询表 + 属主回调」：表里只存属主指针数组，不持任何
 *      内核对象；泵送/等待路径到点后由 pollWsaEvents 逐个回调属主，
 *      属主自己从消息槽取位并推进。
 *   3. 等待点始终只有一个（WSAASYNC 构建下是 IOCP 端口 + 消息队列的
 *      双源 MsgWaitForMultipleObjectsEx），符合「不新增线程、
 *      不新增等待集」的既有约束。
 * ================================================================ */

/**
 * @brief 对全部已登记套接字做一次就绪推进，就绪者回调其属主。
 * @details 就绪判断与后续读取由属主自己做
 *          （`XDeviceNetwork_win32_selectDispatchReady`
 *          内部从消息槽取位再推进读路径），本环只负责「到点问一遍」。
 *
 *          [2026-11-21] 泵送/等待路径先把窗口消息位缓存进本 ring 的消息槽
 *          （`xnetio_pumpMsgOnce` / MsgWait 唤醒后的排空），再由本函数让各
 *          owner 的 selectDispatchReady 从缓存取位。
 */
static void pollWsaEvents(XNetIoRingWin32* win)
{
#if XNETWORK_WIN_SOCKET_SELECT
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
 * @brief 是否有任一已登记套接字（即「可能就绪」）。
 * @details 为避免与 pollWsaEvents 重复查询，这里直接返回「表非空」——
 *          就绪判定交给 poll 里的属主回调完成（就绪位在消息槽里，
 *          属主回调自取；多问一遍与少问一遍对主循环无实质影响，
 *          而省掉一次重复扫描更划算）。
 */
static bool anyWsaEventSignaled(XNetIoRingWin32* win)
{
#if XNETWORK_WIN_SOCKET_SELECT
    return win && win->m_wsaCount > 0;
#else
    (void)win;
    return false;
#endif
}

/**
 * @brief 阻塞等待的等待片上限（毫秒）：双源等待里单次 MsgWait 的超时片。
 * @details 取 10ms：与框架主循环 20ms 心跳同量级。只对「本轮 MsgWait 没
 *          等到任何源」的等待片生效；任何源就绪都立即唤醒。
 *          纯 IOCP 构建不走该路径（无套接字登记），阻塞语义逐位不变。
 */
#define XNETIORING_WSA_POLL_SLICE_MS 10

/* ================================================================
 * 完成端口检查计数器
 *
 * 用途：把"完成包丢失"与"完成包还在端口里排队、只是本线程尚未取走"
 * 区分开。`GetOverlappedResult` 只看状态块——内核已完成而包还在端口里
 * 时它同样返回 TRUE，仅凭它判定丢包会把正常排队误判成丢包
 * （实测：GUI 切页重绘让一个投递批次耗时数秒，客户端连续误判 3 次，
 * 把 3 个读槽全部退役，读通道随之停摆）。
 *
 * 判据：自挂读起，端口至少被**检查**过 N 次而该包仍未出队，才成立丢包。
 * 每次 `pollPlatform` / `waitForEvents` 都算一次检查。
 * ================================================================ */
static volatile LONG g_netIoRingPollSeq = 0;
/** @brief 已取走的唤醒包累计数（诊断：唤醒洪水/排队饥饿是否成立）。 */
static volatile LONG g_netIoRingWakePackets = 0;

uint32_t XNetIoRingWin32_pollSeq(void)
{
    return (uint32_t)InterlockedCompareExchange(&g_netIoRingPollSeq, 0, 0);
}

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
    int wakeTaken = 0;

    if (!win || !win->m_iocp) return;

    /* [2026-11-04] 先扫 WSA 就绪事件：select 模型套接字后端不使用
     * 完成包，就绪由 ring 消息槽的 FD_* 位承载。放在 IOCP 之前，保证同一次
     * pollPlatform 内网络就绪不被 IO 完成批量挤到下一轮。 */
    (void)InterlockedIncrement(&g_netIoRingPollSeq);
#if XNETWORK_WIN_SOCKET_WSAASYNC
    /* WSAAsyncSelect: 先泵隐藏窗口消息（WndProc 把 FD_* 位缓存进 pump
     * 模块），再让 pollWsaEvents 的各 owner 从缓存取位。必须放在
     * pollWsaEvents 之前，否则本帧就绪要等下一轮才见。窗口惰性创建，
     * 保证建在事件循环线程（pollPlatform 在主线程被调）。 */
    xnetio_ensureMsgWindow(win);
    (void)xnetio_pumpMsgOnce(win);
#endif
    pollWsaEvents(win);


    /* 非阻塞循环获取 IOCP 完成包 */
    while (batchCount < XNETIORING_WIN32_POLL_BATCH) {
        BOOL success = GetQueuedCompletionStatus(
            win->m_iocp, &bytesTransferred, &completionKey, &overlapped, 0);

        if (overlapped == NULL) {
            if (!success) break;          /* 超时：确实无包可取 */
            /* 唤醒包（PostQueuedCompletionStatus 带 NULL overlapped）**不是
             * 完成包**。旧实现遇到它就 break，于是只要端口里有唤醒包，后面的
             * 真实完成包每轮只能前进一个包位——而任意线程 postEvent 都会投
             * 一个唤醒包，唤醒包一多，完成包就被"排队饥饿"：内核早已完成、
             * 状态块写好 750 字节，包却要等数秒才被取走（实测跨进程联调：
             * 读看门狗因此反复误判"完成包丢失"并退役读槽，最终静默判线）。
             * 这里继续取，把唤醒包一次性排空。 */
            (void)InterlockedExchange(&win->m_wakePending, 0);
            (void)InterlockedIncrement(&g_netIoRingWakePackets);
            if (++wakeTaken >= XNETIORING_WIN32_POLL_BATCH) break;
            continue;
        }

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

    /* [2026-11-21 WSAASYNC·双源等待] 有套接字登记（select 模型构建）时，
     * IOCP 端口与本线程消息队列进**同一个等待点**（见循环体内的说明）；
     * 无登记时语义不变（一次等待到底，IOCP 完成包即时唤醒）。
     * WSAEVENT 时代的「GQCS(10ms) 分片 + 到点轮询」路径已随其实现移除。 */
    if (win->m_wsaCount > 0) {
        int remaining = timeoutMs;
        /* [2026-11-21 双源等待] **IOCP 完成包 + 窗口消息一次等待、立即唤醒**
         * （Qt qeventdispatcher_win 同款模式）：MsgWaitForMultipleObjectsEx
         * 把 IOCP 端口句柄与本线程消息队列放进同一个等待点——
         *   ① 完成包/wakeUp 到达 → 句柄唤醒 → GQCS(0) 取包处理 → 返回；
         *   ② 窗口消息到达（WSAAsyncSelect 的 FD_* 事件）→ QS_ALLINPUT
         *      **立即**唤醒 → 全量排空本线程消息队列（DispatchMessage 路由
         *      到各 WndProc，FD_* 位缓存进消息槽）→ pollWsaEvents 派发 →
         *      返回。
         * 相比旧「GQCS(10ms) 分片 + 到点泵」，事件唤醒延迟从 ≤10ms 降为
         * 立即，且分片循环消失。**必须有界排空**（MWMO_INPUTAVAILABLE +
         * PeekMessage 循环到空）：否则队列里已报告过的消息会让 MsgWait
         * 立即再醒而本轮又没取完 ⇒ 忙等。PeekMessage 不带 hwnd 过滤——
         * 本线程其它窗口（GUI 顶窗）的消息同样被 DispatchMessage 路由到
         * 各自 WndProc，与 XGui 泵「谁取到谁处理」并发安全。
         * 隐藏窗口建在事件循环线程（线程亲和，见结构体「窗口消息事件源」注）。
         * IOCP 构建不会进入本分支（m_wsaCount 恒 0，走函数末段纯 IOCP 等待）。 */
        (void)xnetio_ensureMsgWindow(win);
        for (;;) {
            HANDLE waits[1];
            DWORD  r;
            DWORD  slice;
            BOOL   success;
            if (remaining >= 0 && remaining <= XNETIORING_WSA_POLL_SLICE_MS)
                slice = (DWORD)remaining;
            else
                slice = (DWORD)XNETIORING_WSA_POLL_SLICE_MS;

            waits[0] = win->m_iocp;
            r = MsgWaitForMultipleObjectsEx(1, waits, slice, QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE);
            if (r == WAIT_OBJECT_0 + 1) {
                /* 窗口消息就绪：排空本线程消息队列（无界消息也必终——
                 * PM_REMOVE 每条只取一次；上界以防极端洪流饿死其它事件）。 */
                int pumped = 0;
                MSG wmsg;
                while (PeekMessageW(&wmsg, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&wmsg);
                    DispatchMessageW(&wmsg);
                    if (++pumped >= 256) break;
                }
                /* WSA 事件位已由 WndProc 缓存：派发并返回。 */
                if (anyWsaEventSignaled(win)) {
                    pollWsaEvents(win);
                    return;
                }
                /* 只有非 ring 消息（GUI 顶窗等）：本轮已处理，继续等待。 */
                if (remaining >= 0) {
                    remaining -= (int)slice;
                    if (remaining <= 0) return;
                }
                continue;
            }
            if (r == WAIT_OBJECT_0) {
                /* IOCP 完成包/wakeUp：非阻塞取包；取到真实完成包处理后返回
                 * （与旧语义一致：一次等待处理一个完成，余量交 pollPlatform）。 */
                for (;;) {
                    success = GetQueuedCompletionStatus(
                        win->m_iocp, &bytesTransferred, &completionKey,
                        &overlapped, 0);
                    if (overlapped != NULL) {
                        DWORD lastError = success ? 0 : GetLastError();
                        processOneCompletion(self, success, bytesTransferred,
                                             completionKey, overlapped, lastError);
                        return;
                    }
                    if (success) {
                        /* 唤醒包：清"包在队"标记（否则 wakeUp 幂等停投）。 */
                        (void)InterlockedExchange(&win->m_wakePending, 0);
                        (void)InterlockedIncrement(&g_netIoRingWakePackets);
                        continue;
                    }
                    break;   /* 超时：包已排空 */
                }
                /* 唤醒包驱动的唤醒：返回让上层 processEvents 继续。 */
                return;
            }
            if (r == WAIT_TIMEOUT) {
                /* 到点：消息可能在超时沿才入队——保险查一遍 WSA 就绪。 */
                if (anyWsaEventSignaled(win)) {
                    xnetio_pumpMsgOnce(win);
                    pollWsaEvents(win);
                    return;
                }
                if (remaining >= 0) {
                    remaining -= (int)slice;
                    if (remaining <= 0) return;
                }
                continue;
            }
            /* WAIT_FAILED：罕见（句柄失效）；直接返回交上层超时兜底。 */
            return;
        }
    }

    winTimeout = (timeoutMs < 0) ? INFINITE : (DWORD)timeoutMs;
    {
        BOOL success;
        (void)InterlockedIncrement(&g_netIoRingPollSeq);
        success = GetQueuedCompletionStatus(
            win->m_iocp, &bytesTransferred, &completionKey, &overlapped, winTimeout);

        /* overlapped==NULL 表示超时或 wakeUp 唤醒，无完成需处理 */
        if (overlapped == NULL) {
            if (success) {
                (void)InterlockedExchange(&win->m_wakePending, 0);
                (void)InterlockedIncrement(&g_netIoRingWakePackets);
            }
            return;
        }

        DWORD lastError = success ? 0 : GetLastError();
        processOneCompletion(self, success, bytesTransferred,
                             completionKey, overlapped, lastError);
    }
}

/* 唤醒阻塞中的 WaitForEvents（虚函数） */
static void VXNetIoRingWin32_wakeUp(XAbstractNetIoRing* self) {
    XNetIoRingWin32* win = (XNetIoRingWin32*)self;
    if (!win || !win->m_iocp) return;
    /* 幂等：端口里已有唤醒包就不再投。旧实现每次唤醒都无条件投一个，
     * 高频 postEvent（多线程定时器/日志/推帧）会把端口灌成唤醒包队列，
     * 真实完成包被挤在后面（见 pollPlatform 的排空注释）。 */
    if (InterlockedExchange(&win->m_wakePending, 1) != 0) return;
    if (!PostQueuedCompletionStatus(win->m_iocp, 0, 0, NULL))
        (void)InterlockedExchange(&win->m_wakePending, 0);
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

#if XNETWORK_WIN_SOCKET_WSAASYNC
    /* WSAAsyncSelect 后端：销毁隐藏窗口/释放消息泵（幂等）。
     * IOCP 构建不含该调用（本宏为 0），析构行为逐位不变。 */
    xnetio_shutdownMsgWindow(win);
#endif

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
 * 窗口消息事件源（WSAAsyncSelect + 通用窗口消息挂靠）
 *
 * [2026-11-21] Windows 平台等待集的第二类内核源：窗口消息。此前该能力
 * 以独立模块（进程级静态单例）存在；现并入 ring——
 * 「平台等待集同时管理完成包与窗口消息」本就是这个抽象的语义，且
 * 生命周期随 ring（init 清零 / 首用惰性建窗 / deinit 销毁），未来
 * 其它窗口消息组件经 registerWindowSource 挂靠，**不得各自另建窗口**。
 *
 * 线程亲和：WSAAsyncSelect 的消息投递到注册 socket 的线程；窗口在
 * 事件循环线程惰性创建（pollPlatform/probeArm 首次触达），WndProc 与
 * pumpOnce/takeMsgEvents 同在该线程。SRWLOCK 为跨线程误用兜底。
 * ================================================================ */

#define XNETIORING_MSG_PUMP_BATCH 64   /**< 单轮泵送上限，防饿死其它事件 */

/** 按 socket 找槽；未登记返回 NULL（调用方需已持写锁）。 */
static XNetIoRingMsgSlot* xnetio_findMsgSlot(XNetIoRingWin32* win, SOCKET socket)
{
    int i;
    for (i = 0; i < XNETIORING_WIN32_MAX_WSA_EVENTS; ++i) {
        if (win->m_msgSlots[i].m_inUse && win->m_msgSlots[i].m_socket == socket)
            return &win->m_msgSlots[i];
    }
    return NULL;
}

/** 登记一个新槽；表满返回 NULL（调用方需已持写锁）。 */
static XNetIoRingMsgSlot* xnetio_addMsgSlot(XNetIoRingWin32* win, SOCKET socket)
{
    int i;
    for (i = 0; i < XNETIORING_WIN32_MAX_WSA_EVENTS; ++i) {
        if (!win->m_msgSlots[i].m_inUse) {
            memset(&win->m_msgSlots[i], 0, sizeof(win->m_msgSlots[i]));
            win->m_msgSlots[i].m_socket = socket;
            win->m_msgSlots[i].m_inUse = 1;
            return &win->m_msgSlots[i];
        }
    }
    return NULL;
}

/** 把本次消息的 WSA 错误码写入各置位事件对应的 FD_*_BIT 槽。 */
static void xnetio_storeMsgErrors(XNetIoRingMsgSlot* slot, long events, int error)
{
    int bit;
    for (bit = 0; bit < FD_MAX_EVENTS; ++bit) {
        if (events & (long)(1u << bit))
            slot->m_errors[bit] = error;
    }
}

/** 统计 events 里置位的 FD_* 位数。 */
static int xnetio_countMsgBits(long events)
{
    int n = 0;
    int bit;
    for (bit = 0; bit < FD_MAX_EVENTS; ++bit) {
        if (events & (long)(1u << bit))
            ++n;
    }
    return n;
}

/** message-only 隐藏窗口过程：FD_* 位按 SOCKET 缓存 + 通用源分发。 */
static LRESULT CALLBACK xnetio_msgWndProc(HWND hwnd, UINT msg,
                                          WPARAM wparam, LPARAM lparam)
{
    XNetIoRingWin32* win;

    if (msg == WM_CREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    win = (XNetIoRingWin32*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (win == NULL)
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    /* 通用窗口消息源（未来组件挂靠：设备变更/热键/电源…）。 */
    {
        int i;
        for (i = 0; i < win->m_msgSourceCount; ++i) {
            if (win->m_msgSources[i].msg == msg) {
                win->m_msgSources[i].cb(win->m_msgSources[i].ud, hwnd, wparam, lparam);
                return 0;
            }
        }
    }

    /* WSAAsyncSelect：WPARAM=SOCKET，lParam 低字=FD_* 位，高字=WSA 错。 */
    if (msg == XNETIORING_WM_SOCKET) {
        SOCKET socket = (SOCKET)wparam;
        long   events = (long)WSAGETSELECTEVENT(lparam);
        int    error  = (int)WSAGETSELECTERROR(lparam);

        AcquireSRWLockExclusive(&win->m_msgLock);
        {
            /* [槽泄漏根治 2026-11-21] **无槽即丢弃，绝不自动建槽**。
             * 实测（MSGDIAG）：FULL inUse=128 addCnt=253 rmCnt=125 —— accept
             * 继承关联会让 WndProc 对 accepted socket 自动建槽；socket 关闭后
             * 其句柄值未必被复用，滞留队列的毒消息（对端断开的 FD_CLOSE 等）
             * 到达时 WndProc 又为死句柄建新槽 ⇒ 槽永久泄漏、洪流用例丢位。
             * 改为：槽只在 WSAAsyncSelect 注册成功后由 probeArm 显式建立
             * （resetMsgSlot 无槽则建空槽）；取走点（泵/dispatch）都在注册点
             * 之后的 processEvents 轮，注册窗口内不会丢消息；死句柄滞留消息
             * 天然无槽可落 ⇒ 自动回收语义。 */
            XNetIoRingMsgSlot* slot = xnetio_findMsgSlot(win, socket);
            if (slot != NULL) {
                slot->m_events |= events;
                xnetio_storeMsgErrors(slot, events, error);
            }
        }
        ReleaseSRWLockExclusive(&win->m_msgLock);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/** 惰性创建 message-only 隐藏窗口（进程一个 ring 一个；建在调用线程）。 */
static void xnetio_ensureMsgWindow(XNetIoRingWin32* win)
{
    if (!win) return;
    AcquireSRWLockExclusive(&win->m_msgLock);
    if (win->m_msgHwnd == NULL) {
        WNDCLASSEXW wc;
        HINSTANCE   hinst = GetModuleHandleW(NULL);
        ATOM        cls;
        DWORD       err;

        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = 0;
        wc.lpfnWndProc   = xnetio_msgWndProc;
        wc.hInstance     = hinst;
        wc.lpszClassName = L"XinYueCNetIoRingMsgClass";

        cls = RegisterClassExW(&wc);
        if (cls == 0) {
            err = GetLastError();
            /* 类已存在（重复 ensure）视为成功。 */
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                ReleaseSRWLockExclusive(&win->m_msgLock);
                return;
            }
        }
        {
            /* lpCreateParams 传 ring 指针，WM_CREATE 时写入 GWLP_USERDATA。 */
            win->m_msgHwnd = CreateWindowExW(
                0, L"XinYueCNetIoRingMsgClass", L"", WS_POPUP,
                0, 0, 0, 0,
                HWND_MESSAGE, NULL, hinst, (LPVOID)win);
        }
    }
    ReleaseSRWLockExclusive(&win->m_msgLock);
}

/** 泵送一帧本窗口的 pending 消息（非阻塞；单批上限防饿死其它事件）。 */
static int xnetio_pumpMsgOnce(XNetIoRingWin32* win)
{
    MSG  wmsg;
    int  count = 0;
    HWND hwnd;

    if (!win) return 0;
    if (win->m_msgHwnd == NULL)
        xnetio_ensureMsgWindow(win);
    hwnd = win->m_msgHwnd;
    if (hwnd == NULL)
        return 0;

    /* 只泵本 ring 的隐藏窗口消息；PeekMessage 非阻塞，无消息立即返回。 */
    while (count < XNETIORING_MSG_PUMP_BATCH &&
           PeekMessageW(&wmsg, hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&wmsg);
        DispatchMessageW(&wmsg);
        ++count;
    }
    return count;
}

/** 销毁窗口（ring deinit；幂等）。 */
static void xnetio_shutdownMsgWindow(XNetIoRingWin32* win)
{
    if (!win) return;
    AcquireSRWLockExclusive(&win->m_msgLock);
    if (win->m_msgHwnd != NULL) {
        DestroyWindow(win->m_msgHwnd);
        win->m_msgHwnd = NULL;
    }
    memset(win->m_msgSlots, 0, sizeof(win->m_msgSlots));
    ReleaseSRWLockExclusive(&win->m_msgLock);
}

HWND XNetIoRingWin32_msgWindow(XNetIoRingWin32* ring)
{
    if (!ring) return NULL;
    if (ring->m_msgHwnd == NULL)
        xnetio_ensureMsgWindow(ring);
    return ring->m_msgHwnd;
}

int XNetIoRingWin32_pumpMsgOnce(XNetIoRingWin32* ring)
{
    return xnetio_pumpMsgOnce(ring);
}

int XNetIoRingWin32_takeMsgEvents(XNetIoRingWin32* ring, SOCKET socket,
                                  long* outEvents, int outErrorSlot[FD_MAX_EVENTS])
{
    int count = 0;
    XNetIoRingMsgSlot* slot;

    if (!ring) return 0;
    AcquireSRWLockExclusive(&ring->m_msgLock);
    slot = xnetio_findMsgSlot(ring, socket);
    if (slot != NULL) {
        long events = slot->m_events;
        int  bit;

        count = xnetio_countMsgBits(events);
        if (outEvents != NULL)
            *outEvents |= events;

        for (bit = 0; bit < FD_MAX_EVENTS; ++bit) {
            if (events & (long)(1u << bit)) {
                /* 即使调用方不取错误码也一并清掉，避免陈旧错误码泄漏到后续 take。 */
                if (outErrorSlot != NULL)
                    outErrorSlot[bit] = slot->m_errors[bit];
                slot->m_errors[bit] = 0;
            }
        }
        /* 取走即清（含 FD_READ）：常驻/补位由 socket 后端负责。 */
        slot->m_events = 0;
    }
    ReleaseSRWLockExclusive(&ring->m_msgLock);
    return count;
}

void XNetIoRingWin32_resetMsgSlot(XNetIoRingWin32* ring, SOCKET socket)
{
    XNetIoRingMsgSlot* slot;
    int bit;
    if (!ring) return;
    AcquireSRWLockExclusive(&ring->m_msgLock);
    slot = xnetio_findMsgSlot(ring, socket);
    if (slot == NULL) {
        /* 注册即建槽（空槽）：此后该 socket 的消息才被 WndProc 缓存——
         * 无槽消息一律丢弃（防死句柄滞留毒消息建槽泄漏，见 WndProc 注）。 */
        slot = xnetio_addMsgSlot(ring, socket);
    }
    if (slot != NULL) {
        slot->m_events = 0;
        for (bit = 0; bit < FD_MAX_EVENTS; ++bit)
            slot->m_errors[bit] = 0;
    }
    ReleaseSRWLockExclusive(&ring->m_msgLock);
}

void XNetIoRingWin32_removeMsgSlot(XNetIoRingWin32* ring, SOCKET socket)
{
    XNetIoRingMsgSlot* slot;
    if (!ring) return;
    AcquireSRWLockExclusive(&ring->m_msgLock);
    slot = xnetio_findMsgSlot(ring, socket);
    if (slot != NULL)
    {
        memset(slot, 0, sizeof(*slot));   /* m_inUse=0 => 槽回收 */
    }
    ReleaseSRWLockExclusive(&ring->m_msgLock);
}

bool XNetIoRingWin32_registerWindowSource(XNetIoRingWin32* ring, UINT msg,
                                          void (*cb)(void* ud, HWND hwnd, WPARAM wp, LPARAM lp),
                                          void* ud)
{
    int i;
    if (!ring || cb == NULL) return false;
    AcquireSRWLockExclusive(&ring->m_msgLock);
    for (i = 0; i < ring->m_msgSourceCount; ++i) {
        if (ring->m_msgSources[i].msg == msg) {   /* 幂等覆盖 */
            ring->m_msgSources[i].cb = cb;
            ring->m_msgSources[i].ud = ud;
            ReleaseSRWLockExclusive(&ring->m_msgLock);
            return true;
        }
    }
    if (ring->m_msgSourceCount >= (int)(sizeof(ring->m_msgSources) / sizeof(ring->m_msgSources[0]))) {
        ReleaseSRWLockExclusive(&ring->m_msgLock);
        return false;
    }
    ring->m_msgSources[ring->m_msgSourceCount].msg = msg;
    ring->m_msgSources[ring->m_msgSourceCount].cb = cb;
    ring->m_msgSources[ring->m_msgSourceCount].ud = ud;
    ++ring->m_msgSourceCount;
    ReleaseSRWLockExclusive(&ring->m_msgLock);
    return true;
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
    int err;

    if (osFd < 0)
        return true; /* 无 OS fd 可查：保持"0 字节即关闭"的保守语义。 */

    /* 临时切非阻塞：链路存活但无数据时 PEEK 不能阻塞事件循环。
     * ioctlsocket 失败即放弃复核，按"真关闭"保守处理（不引入新语义）。 */
    if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
        return true;

    pr = (int)recv(s, &peekByte, 1, MSG_PEEK);
    /* [2026-11-18 根修] **错误码必须立刻取**：下面的复原 ioctlsocket 成功
     * 时会清掉套接字的 last error ⇒ 旧实现在第 791 行才读 WSAGetLastError()
     * 拿到的是 0（不是 recv 的错误）⇒ switch 落进 default ⇒ 一律按"还活着"
     * 处理 ⇒ WSAECONNRESET/WSAECONNABORTED 这类**硬错误**永远变不成关闭
     * 事件（对端被 RST 踢掉后本端不察觉，0 字节读完成被当假 EOF 丢弃）。
     * 取值紧跟 recv，不受后续调用影响。 */
    err = (pr == SOCKET_ERROR) ? WSAGetLastError() : 0;
    /* 复原：两套 Win32 套接字后端创建套接字时都是**非阻塞**
     * （`ioctlsocket(FIONBIO, 1)`）。旧注释写"恢复为阻塞（与创建时一致）"
     * 与事实相反：这里若真置 0，会把套接字留成阻塞态，后续任何一次
     * 非重叠 `recv/send`（代理握手、GSSAPI、假 EOF 复核自身）都可能
     * 永久阻塞事件循环。故恢复为 1。 */
    nonBlocking = 1;
    (void)ioctlsocket(s, FIONBIO, &nonBlocking);

    if (pr == 0)
        return true;                       /* 真 FIN */
    if (pr != SOCKET_ERROR)
        return false;                      /* 有数据待读：非关闭 */
    /* recv 出错时要分两类，旧实现把两类混成"非 FIN"：链路被 RST 之后
     * recv 恒返 SOCKET_ERROR/WSAECONNRESET，若按"活着"处理，0 字节读完成
     * 会被当成假 EOF 丢弃 —— 表现为对端已踢线而本端永不察觉（实测
     * XLink 踢线用例 10 项失败：不进 ReconnectingState、不拆旧会话）。 */
    switch (err) {
    case WSAEWOULDBLOCK:                   /* 活着但暂无数据 */
        return false;
    case WSAECONNRESET:
    case WSAECONNABORTED:
    case WSAENOTCONN:
    case WSAESHUTDOWN:
    case WSAENOTSOCK:                      /* 连接已不可用 ⇒ 真关闭 */
    case ERROR_NETNAME_DELETED:            /* 64：对端对象已消失（GQCS/recv 都可能报） */
    case ERROR_CONNECTION_ABORTED:         /* 1236：连接被中止 */
    case ERROR_BROKEN_PIPE:                /* 109：管道断裂（命名管道路径） */
        return true;
    default:
        return false;                      /* 未知错误：保守按活着处理 */
    }
}

#endif /* XPLATFORM_WINDOWS */

#endif /* XAbstractNetIoRing_ON */
