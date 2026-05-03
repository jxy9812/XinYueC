// XEventDispatcher_win.c
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include"IOCPInfo.h"
#include <objbase.h>  // 可选，但更明确
#include <mmsystem.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "XThread.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XMap.h"
#include "XPair.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include "XListSLinked.h"
#include <string.h>
#include <stdint.h>
#include "XThreadData.h"
#include "XTimeWheelGroup.h"
#pragma comment(lib, "winmm.lib")
//static  HANDLE ioCompletionPort=NULL;    // 全局 IOCP 句柄;
/**
 * @brief 定时器信息结构体 (Windows 私有)
 */
typedef struct {
    bool isHighPrecision;       ///< 标记是否为高精度定时器 
    bool isTimeWheel;//是否是时间轮
    XTimerType timerType;       ///< 定时器类型
    XTimerId timerId;           ///< 定时器 ID
    union
    {
        UINT_PTR winTimerId;        ///< Windows 定时器 ID (来自 SetTimer)
        MMRESULT mmTimerId;         ///< 多媒体定时器 ID (来自 timeSetEvent)，用于高精度定时器
        XTimerData* m_wheel;  ///时间轮定时器
    };
    XObject* object;            ///< 关联的对象   
    void* highResContext;       // 指向 HighResTimerContext 的指针
    int64_t interval;           ///< 间隔 (纳秒)
} XEventDispatcherWin32_TimerInfo;
/**
 * @brief Windows 主线程数据
 */
typedef struct
{
    XAbstractEventDispatcherPrivate m_dp;
    HANDLE ioCompletionPort;
    XHashMap* timers;           ///< 定时器映射: timerId  -> XEventDispatcherWin32_TimerInfo*
    XHashMap* sockets;          ///< 套接字映射: socket.value -> XEventDispatcherWin32_SocketInfo*
    bool timePeriodSet;          ///< 是否已调用 timeBeginPeriod(1)
    int highPrecisionTimerCount; ///< 当前活跃的高精度定时器数量
} MainThreadDataPrivate;

typedef struct XEventDispatcherWin32
{
    XAbstractEventDispatcher m_class; ///< 继承自 XObject
    HWND internalHwnd;          ///< 内部消息窗口句柄，用于接收定时器和网络事件
    bool interrupt;             ///< 中断标志，用于 interrupt()
    bool wakeUpSent;            ///< 标记是否已发送 WM_USER 消息用于 wakeUp()
} XEventDispatcherWin32;
XVtable* XEventDispatcherWin32_class_init();
// 辅助函数声明
static LRESULT CALLBACK XEventDispatcherWin32_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, _Out_ DWORD NumberOfBytesTransferred,
    _Out_ ULONG_PTR CompletionKey,
    _Out_ LPOVERLAPPED* Overlapped);
static void XEventDispatcherWin32_handleTimerMessage(XEventDispatcherWin32* dispatcher, UINT_PTR timerId);
//iocp绑定
bool IOCP_bind(XSocketDescriptor socket,XObject* obj);
// Windows 消息常量
#define XDISPATCHER_WM_SOCKET (WM_USER + 1)
#define XDISPATCHER_WM_WAKEUP (WM_USER + 2)

#define PlatformPrivate(Dispatcher)  ((MainThreadDataPrivate*)((XAbstractEventDispatcher*)Dispatcher)->d_ptr)
#define GetXMutex(Dispatcher)         PlatformPrivate(Dispatcher)->m_dp.mutex
// ========================
// XHashMap 键比较和哈希函数 (用于 timers 和 sockets)
// ========================

// 用于 sockets HashMap: 键是 socket value (intptr_t)
static int int_compareptr_t(const void* a, const void* b) {
    intptr_t va = *(const intptr_t*)a;
    intptr_t vb = *(const intptr_t*)b;
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}
// ========================
// 辅助函数实现
// ========================

/**
 * @brief Windows 窗口过程函数
 */
static LRESULT CALLBACK XEventDispatcherWin32_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //XPrintf("接收到系统事件\n");
    if (msg == XDISPATCHER_WM_SOCKET) {
        // 网络事件
        XEventDispatcherWin32* dispatcher = (XEventDispatcherWin32*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        /*if (dispatcher) {
            XEventDispatcherWin32_handleSocketMessage(dispatcher, hwnd, msg, wParam, lParam);
        }*/
        return 0;
    }
    else if (msg == XDISPATCHER_WM_WAKEUP) {
        XEventDispatcherWin32* dispatcher = (XEventDispatcherWin32*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        // 唤醒事件
        //MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
        // 重置 wakeUpSent 标志，允许下次再次发送唤醒消息
        dispatcher->wakeUpSent = false;
        return 0;
    }
    else  if (msg == WM_TIMER) {
        // 定时器事件
        XEventDispatcherWin32* dispatcher = (XEventDispatcherWin32*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (dispatcher) {
            XEventDispatcherWin32_handleTimerMessage(dispatcher, (UINT_PTR)wParam);
        }
        return 0;
    }

    // 处理本地事件过滤器
   /* XEventDispatcherWin32* dispatcher = (XEventDispatcherWin32*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (dispatcher) {
        intptr_t result = 0;
        if (XAbstractEventDispatcher_filterNativeEvent(dispatcher, "windows_generic_MSG", &msg, &result)) {
            return (LRESULT)result;
        }
    }*/

    return DefWindowProc(hwnd, msg, wParam, lParam);
}
/**
 * @brief 处理网络事件消息
 */
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, _Out_ DWORD NumberOfBytesTransferred,
    _Out_ ULONG_PTR completionKey,
    _Out_ LPOVERLAPPED* overlapped)
{
    XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    XEventSockAct* event = XEventSockAct_create(ioCtx->socket, XSocketAct_Invalid);
    if(!event)return;
    XMutex_lock(GetXMutex(dispatcher));
    if (ioCtx->eventMask & FD_READ)
        event->actType |= XSocketAct_Read;
    if (ioCtx->eventMask & FD_WRITE)
        event->actType |= XSocketAct_Write;
    XCoreApplication_postEvent(completionKey, event, XEVENT_PRIORITY_NORMAL);
    //套接字监听器
    XVector* notifiers = XMapBase_value_base(d->sockets, &socket);
    if (notifiers)
    {
        for_each_iterator(notifiers,XVector,it)
        {
            XSocketNotifier** lp = XVector_iterator_data(&it);
            if (lp)
            {
                XSocketNotifier* notifier = *lp;
                if(!notifier)continue;
                if (!XSocketNotifier_isEnabled(notifier))
                    continue;//当前监听器未启用
                XSocketNotifierType t = XSocketNotifier_type(notifier);
                if ((t & XSocketNotifier_Read && ioCtx->eventMask & FD_READ) || (t & XSocketNotifier_Write && ioCtx->eventMask & FD_WRITE))
                {//类型符合投递事件
                    XEventSockAct* e = XEventSockAct_create(ioCtx->socket, event->actType);
                    XCoreApplication_postEvent(notifier, e, XEVENT_PRIORITY_NORMAL);
                }
            }
        }
    }
    XMutex_unlock(GetXMutex(dispatcher));
}

/**
 * @brief 处理定时器消息
 */
static void XEventDispatcherWin32_handleTimerMessage(XEventDispatcherWin32* dispatcher, UINT_PTR timerId)
{
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    XMutex_lock(GetXMutex(dispatcher));

    size_t timer_key = (size_t)timerId;
    XHashMap_iterator it;
    if (XHashMap_find_base(d->timers, &timer_key, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
        if (timerInfo && timerInfo->object) {
            XEvent* timerEvent = XEventTimer_create(timerInfo->timerId);
            if(timerEvent)
            {
                timerEvent->posted = true;
                timerEvent->spontaneous = true;
                XCoreApplication_postEvent(timerInfo->object, timerEvent, XEVENT_PRIORITY_NORMAL);
            }
        }
    }

    XMutex_unlock(GetXMutex(dispatcher));
}
bool IOCP_bind(XSocketDescriptor socket, XObject* obj)
{
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    return CreateIoCompletionPort((HANDLE)XSocketDescriptor_toIntptr(socket), d->ioCompletionPort, obj, 0);
}
static void IOCP_handle(XAbstractEventDispatcher* dispatcher)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;

    BOOL success = GetQueuedCompletionStatus(
        d->ioCompletionPort,
        &bytesTransferred,
        &completionKey,
        &overlapped,
        0 // 非阻塞轮询
    );

    if (overlapped != NULL) 
    {
        //XPrintf("收到信息\n");
        // --- 主线程在此处“发现”事件，但绝不处理！ ---
        XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
        if(success)
        {
            if (ioCtx->type == XEventContextType_Type_Timer)
            {
                XEventDispatcherWin32_handleTimerMessage(dispatcher, ((XEventContext_Timer*)ioCtx)->id);
            }
            else if (ioCtx->type == XEventContextType_Type_Socket || ioCtx->type == XEventContextType_Type_File)
            {
                XEventDispatcherWin32_handleSocketMessage(dispatcher, bytesTransferred, completionKey, overlapped);
            }
        }
        else
        {
            // 情况 2: I/O 操作失败完成
            DWORD lastError = GetLastError(); // 注意：这里用 GetLastError() 而不是 WSAGetLastError()
            // 常见的“连接关闭”错误码
            if (lastError == ERROR_NETNAME_DELETED ||      // 64
                lastError == ERROR_OPERATION_ABORTED ||    // 995
                lastError == WSAENETRESET ||               // 10052
                lastError == WSAECONNRESET) {              // 10054

                //printf("Client disconnected! Error: %lu\notifier", lastError);
                //CleanupClient(pIoData); // 执行清理
                return;
            }
        }
        //// 创建跨平台 I/O 事件
        //XIoEvent* ioEvent = XEventIo_create(
        //    ioCtx->eventType,
        //    ioCtx->buffer,
        //    bytesTransferred,
        //    ioCtx->handle
        //);
        //ioEvent->posted = true;

        // 【关键】将事件转发给关联的对象
        // 对象会在其自己的线程上下文中处理此事件
        //XCoreApplication_postEvent(ioCtx->object, (XEvent*)ioEvent, XEVENT_PRIORITY_NORMAL);

        // 清理上下文（假设是一次性操作，实际应用中可能需要复用）
        //XEventDispatcherWin32_deleteIoContext(ioCtx);
       
    }
}
// ========================
// 虚函数实现
// ========================

static bool VXEventDispatcherWin32_processEvents(XAbstractEventDispatcher* dispatcher, XEventLoopProcessEventsFlags flags)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    bool processed = false;

    // 1. 首先处理基类的跨线程事件
    bool hasPostedEvents = (XVtableGetFunc(XAbstractEventDispatcher_class_init(), EXAbstractEventDispatcher_ProcessEvents, bool(*)(XAbstractEventDispatcher*, XEventLoopProcessEventsFlags))(dispatcher, flags));
    processed = hasPostedEvents;

    // 2. 处理所有 Windows 消息（包括 internalHwnd 的消息）
    if (self->internalHwnd)
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                XEvent* quitEvent = XEvent_create(XEVENT_TYPE_QUIT);
                XCoreApplication_postEvent(XCoreApplication_instance(), quitEvent, XEVENT_PRIORITY_HIGH);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            processed = true; // 只要处理了任何 Windows 消息，就标记为已处理
        }
    }

    // 3. 处理 IOCP（仅主线程）
    if (XAbstractEventDispatcher_isMainThread(dispatcher))
    {
        IOCP_handle(dispatcher);
    }

    // 4. 关键修复：正确判断是否需要等待
    // 只有当确实没有任何事件需要处理时，才进入等待状态
    if (!XAbstractEventDispatcher_isMainThread(dispatcher)) // 子线程
    {
        // 检查是否还有未处理的 Windows 消息（非阻塞检查）
        bool hasPendingMessages = false;
        if (self->internalHwnd) {
            MSG peekMsg;
            hasPendingMessages = PeekMessage(&peekMsg, NULL, 0, 0, PM_NOREMOVE);
        }

        // 只有在明确需要等待、没有中断、且确实没有事件时才等待
        if ((flags & XEventLoop_WaitForMoreEvents) &&
            !self->interrupt &&
            !processed &&
            !hasPendingMessages)
        {
            //XPrintf("XThread:%p 进入睡眠\n", XThread_currentThread());
            self->wakeUpSent = false;
            XAbstractEventDispatcher_aboutToBlock_signal(dispatcher);
            DWORD waitRet = MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

            //XPrintf("XThread:%p 被唤醒\n", XThread_currentThread());
            if (waitRet != WAIT_FAILED)
            {
                XAbstractEventDispatcher_awake_signal(dispatcher);
                // 被唤醒后，确保 wakeUpSent 标志会在消息处理中重置
                // 实际的消息处理会在下一次 processEvents 调用中完成
            }
        }
    }

    self->interrupt = false;
    return processed;
}

static void VXEventDispatcherWin32_registerSocketNotifier(XAbstractEventDispatcher* dispatcher, XSocketNotifier* notifier)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    XSocketDescriptor socket = XSocketNotifier_socket(notifier);
    if (!XSocketDescriptor_isValid(socket)) return;

    XMutex_lock(GetXMutex(dispatcher));
    if (!XMapBase_contains(d->sockets, &socket))
    {
        XVector v = { 0 };
        XVector_init(&v,sizeof(XSocketNotifier*));
        XContainerSetCompare(&v,uintptr_t_compare);
        XMapBase_insert_base(d->sockets, &socket,&v);
    }
    XVector* notifiers = XMapBase_value_base(d->sockets, &socket);
    if (notifiers&&XVector_indexOf(notifiers,&notifier,0)==-1)
    {
       /* if (XVector_isEmpty_base(notifiers))
        {
           if(CreateIoCompletionPort((HANDLE)XSocketDescriptor_toIntptr(socket), d->ioCompletionPort, notifiers, 0))
               XVector_append_base(notifiers, &notifier);
        }
        else*/
        {
            XVector_append_base(notifiers, &notifier);
        }
        
    }

  /*  XEventDispatcherWin32_SocketInfo* sockInfo = XEventDispatcherWin32_findOrCreateSocketInfo(self, socket);
    if (sockInfo) {
        XVector_push_back_base(sockInfo->notifiers, &notifier);
        XEventDispatcherWin32_updateSocketEventMask(sockInfo);
    }*/
    XMutex_unlock(GetXMutex(dispatcher));
}

static void VXEventDispatcherWin32_unregisterSocketNotifier(XAbstractEventDispatcher* dispatcher, XSocketNotifier* notifier)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    XSocketDescriptor socket = XSocketNotifier_socket(notifier);
    if (!XSocketDescriptor_isValid(socket)) return;

    XMutex_lock(GetXMutex(dispatcher));
    XVector* notifiers = XMapBase_value_base(d->sockets, &socket);
    if (notifiers)
    {
        int index = XVector_indexOf(notifiers, &notifier, 0);
        if (index != -1)
            XVector_remove_base(d->sockets,index,1);
    }
    //intptr_t socket_key = XSocketDescriptor_toIntptr(socket);
    //XHashMap_iterator it;
    //if (XHashMap_find_base(d->sockets, &socket_key, &it)) {
    //    XPair* pair = XHashMap_iterator_data(&it);
    //    XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);

    //    for (size_t i = 0; i < XVector_size_base(sockInfo->notifiers); ++i) {
    //        XSocketNotifier** pnotifier = (XSocketNotifier**)XVector_at_base(sockInfo->notifiers, i);
    //        if (*pnotifier == notifier) {
    //            XVector_removeAt_base(sockInfo->notifiers, i);
    //            break;
    //        }
    //    }
    //    XEventDispatcherWin32_updateSocketEventMask(sockInfo);

    //    if (XVector_isEmpty_base(sockInfo->notifiers)) {
    //        SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(socket);
    //        WSAAsyncSelect(s, NULL, 0, 0);
    //        XHashMap_remove_base(d->sockets, &socket_key);
    //        XVector_delete_base(sockInfo->notifiers);
    //        XMemory_free(sockInfo);
    //    }
    //}
    XMutex_unlock(GetXMutex(dispatcher));
}

/**
 * @brief 高精度定时器回调函数
 *
 * 注意：此函数在系统创建的高优先级线程中执行，不能直接操作 GUI 或加锁。
 * 因此，它通过 PostMessage 将事件转发到 dispatcher 的内部窗口消息队列，
 * 由主事件循环在正确的线程上下文中处理。
 */
static void CALLBACK XEventDispatcherWin32_HighResTimerCallback(
    UINT uID,          // mmTimerId
    UINT uMsg,
    DWORD_PTR dwUser,  // = (DWORD_PTR)dispatcher
    DWORD_PTR dw1,
    DWORD_PTR dw2
) {
    (void)uID; (void)uMsg; (void)dw1; (void)dw2;
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    HANDLE ioCompletionPort = d->ioCompletionPort;
    XEventContext_Timer* ctx = (XEventContext_Timer*)dwUser;

    if(ctx->id)
    {
        PostQueuedCompletionStatus(
            d->ioCompletionPort,           // IOCP 句柄
            0,                 // dwNumberOfBytesTransferred: 0 表示定时事件
            (ULONG_PTR)dwUser,// dwCompletionKey: 可传递用户数据（如 timer ID）
            dwUser            // lpOverlapped: 通常为 notifierullptr
        );
    }
}
static void XTimerTimeWheelCallback(void* userData, XTimerWheelData* timer)
{
    XObject* object = (XObject*)userData;
    if (!object)return;
    XEvent* timerEvent = XEventTimer_create(XTimerData_timerId(timer));
    if (timerEvent)
    {
        timerEvent->posted = true;
        timerEvent->spontaneous = true;
        XCoreApplication_postEvent(object, timerEvent, XEVENT_PRIORITY_NORMAL);
    }
}
static void VXEventDispatcherWin32_registerTimer(XAbstractEventDispatcher* ed, XTimerId timerId, XDuration intervalNs, XTimerType timerType, XObject* object)
{
    if (!timerId || !intervalNs || !object)return;
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    // 将纳秒转换为毫秒
    size_t intervalMs = (intervalNs + 999999) / 1000000;
    if (intervalMs <= 0) intervalMs = 1;
    UINT uInterval = (UINT)(intervalMs > UINT_MAX ? UINT_MAX : intervalMs);

    XMutex_lock(GetXMutex(dispatcher));

    XEventDispatcherWin32_TimerInfo timerInfo = { 0 };
    timerInfo.timerId = timerId;
    timerInfo.interval = intervalNs;
    timerInfo.timerType = timerType;
    timerInfo.object = object;
    timerInfo.isHighPrecision = false; // 默认为普通定时器
    timerInfo.isTimeWheel = false;
    timerInfo.highResContext = NULL;
    // --- 核心逻辑：根据 XTimerType 决定使用哪种定时器 ---
    if (timerType == XTimerType_PreciseTimer) {
        // --- 使用高精度多媒体定时器 ---
        timerInfo.isHighPrecision = true;
        XTimeWheelGroup* group = XTimeWheelGroup_global();
        if (group&& XTimeWheelGroup_max_time(group)> intervalMs)
        {//范围达标使用时间轮定时器
            XTimerData data = {0};
            XTimerData_setAutoDelete(&data, true);
            XTimerData_setTimerId(&data, timerId);
            XTimerData_setInterval(&data, intervalMs);
            XTimerData_setTimerCallback(&data, XTimerTimeWheelCallback);
            XTimerData_setUserData(&data, object);
            XTimerWheelData* timer = XTimeWheelGroup_addTimer_base(group, data);
            if (timer)
            {
                timerInfo.isTimeWheel = true;
                timerInfo.m_wheel = timer;
                goto save;
            }
            else
            {
                XTimer_deleteLater(timer);
            }

        }
        // 全局设置系统时钟精度（仅在第一个高精度定时器注册时调用）
        if (!d->timePeriodSet) {
            timeBeginPeriod(1); // 请求 1ms 系统时钟精度
            d->timePeriodSet = true;
        }
        // --- 关键修改：分配回调上下文 ---
        XEventContext_Timer* ctx = (XEventContext_Timer*)XMemory_malloc(sizeof(XEventContext_Timer));
        if (!ctx) 
        {
            XMutex_unlock(GetXMutex(dispatcher));
            return; // 内存不足
        }
        ctx->type = XEventContextType_Type_Timer;
        ctx->id = timerId;
        // 将上下文指针传给 dwUser
        timerInfo.mmTimerId = timeSetEvent(
            uInterval,
            0, // 无延迟
            XEventDispatcherWin32_HighResTimerCallback,
            (DWORD_PTR)ctx,
            TIME_CALLBACK_FUNCTION | TIME_PERIODIC
        );

        if (timerInfo.mmTimerId == 0)
        {
            // 失败：释放 ctx，回退到普通定时器
            XMemory_free(ctx);
            // timeSetEvent 失败，回退到普通定时器
            timerInfo.isHighPrecision = false;
            timerInfo.winTimerId = SetTimer(self->internalHwnd, (UINT_PTR)timerId, uInterval, NULL);
            // 如果回退也失败，winTimerId 将为0，后续会被清理
        }
        else 
        {
            timerInfo.highResContext = ctx;
            // 成功，增加计数
            d->highPrecisionTimerCount++;
        }
    }
    else if (timerType == XTimerType_VeryCoarseTimer) 
    {
        // --- 使用秒级精度的普通窗口定时器 ---
        timerInfo.isHighPrecision = false;

        // 将纳秒转换为秒，并向上取整
        size_t intervalSeconds = (intervalNs + 999999999ULL) / 1000000000ULL;
        if (intervalSeconds <= 0) intervalSeconds = 1;

        // 转换回毫秒用于 SetTimer
        UINT uVeryCoarseInterval = (UINT)(intervalSeconds * 1000);
        if (uVeryCoarseInterval > UINT_MAX) uVeryCoarseInterval = UINT_MAX;

        timerInfo.winTimerId = SetTimer(self->internalHwnd, (UINT_PTR)timerId, uVeryCoarseInterval, NULL);
    }
    else {
        // --- XTimerType_CoarseTimer: 使用普通的窗口定时器 ---
        timerInfo.isHighPrecision = false;
        timerInfo.winTimerId = SetTimer(self->internalHwnd, (UINT_PTR)timerId, uInterval, NULL);
    }
save:
    // 保存到 timers 表（无论哪种方式，只要有一个ID有效）
    bool hasValidId = (timerInfo.isHighPrecision && timerInfo.mmTimerId != 0) ||
        (!timerInfo.isHighPrecision && timerInfo.winTimerId != 0);

    if (hasValidId) {
        XHashMap_insert_base(d->timers, &timerId, &timerInfo);
    }

    XMutex_unlock(GetXMutex(dispatcher));
}
// 清理高精度定时的普通定时器回调
static VOID CALLBACK CleanupTimerCallback(
    PTP_CALLBACK_INSTANCE Instance,
    PVOID Context,
    PTP_TIMER Timer
) {
    (void)Instance; // 未使用
    (void)Timer;    // 未使用

    XEventContext_Timer* ctx = (XEventContext_Timer*)Context;

    // --- 关键：在这里安全地释放内存 ---
    // 因为这是在 unregister 之后触发的，
    // 所以我们可以直接释放，无需额外同步。
    XMemory_free(ctx);
    CloseThreadpoolTimer(Timer);
}
//清理高精度定时的普通定时器 延迟释放
static void clearHighPrecisionTimer(XEventContext_Timer* ctx)
{
    // 2. --- 关键：创建并启动一个单次的线程池定时器作为兜底 ---
            //    a. 创建定时器对象
    PTP_TIMER timer = CreateThreadpoolTimer(
        CleanupTimerCallback, // 回调函数
        ctx,                  // 传递 ctx 作为上下文
        NULL                  // 使用默认环境
    );

    if (timer == NULL) {
        // 创建失败，直接清理（虽然不太可能发生）
        XMemory_free(ctx);
        //return true;
    }
    // --- 设置为 50 毫秒的单次兜底定时器 ---
    ULARGE_INTEGER dueTime;
    dueTime.QuadPart = -5000000; // 50ms = 50 * 10,000 * 100ns

    SetThreadpoolTimer(
        timer,
        (FILETIME*)&dueTime, // 到期时间
        0,                   // msPeriod = 0 表示单次触发！
        0                    // msWindowLength (可选，设为0)
    );
}
static bool VXEventDispatcherWin32_unregisterTimer(XAbstractEventDispatcher* ed, XTimerId timerId)
{
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    XMutex_lock(GetXMutex(dispatcher));
    XEventDispatcherWin32_TimerInfo* timerInfo = XHashMap_value_base(d->timers,&timerId);
    if (!timerInfo)
    {
        XMutex_unlock(GetXMutex(dispatcher));
        return false;
    }
    if (timerInfo->isHighPrecision) 
    {
        if (timerInfo->isTimeWheel)
        {
            XTimerData_setUserData(timerInfo->m_wheel,NULL);
            XTimeWheelGroup_removeTimer_base(XTimeWheelGroup_global(), timerInfo->m_wheel);
        }
        else if (timerInfo->mmTimerId != 0) 
        {
            XEventContext_Timer* ctx = (XEventContext_Timer*)timerInfo->highResContext;
            ctx->id = 0;
            // 1. 立即杀死高精度多媒体定时器
            timeKillEvent((MMRESULT)timerInfo->mmTimerId);
            timerInfo->mmTimerId = 0;
            d->highPrecisionTimerCount--;
            clearHighPrecisionTimer(ctx);
            // 如果这是最后一个高精度定时器，恢复系统时钟精度
            if (d->highPrecisionTimerCount == 0 && d->timePeriodSet) {
                timeEndPeriod(1);
                d->timePeriodSet = false;
            }
            // 注意：此时我们不释放 ctx，而是将其“托管”给线程池定时器
            timerInfo->highResContext = NULL;
        }
    }
    else 
    {
        KillTimer(self->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
    }
    //将id放回列表
    XVector_push_back_base(d->m_dp.m_timerIds,&timerId);
    XHashMap_remove_base(d->timers, &timerId);
    XMutex_unlock(GetXMutex(dispatcher));
    return true;
}

static bool VXEventDispatcherWin32_unregisterTimers(XAbstractEventDispatcher* ed, XObject* object)
{
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    bool found = false;
    XMutex_lock(GetXMutex(dispatcher));

    XHashMap_iterator it = XHashMap_begin(d->timers);
    while (!XHashMap_iterator_isEnd(&it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
        if (timerInfo && timerInfo->object == object) 
        {
            if (timerInfo->isHighPrecision) 
            {
                if (timerInfo->isTimeWheel)
                {
                    XTimer_stop_base(timerInfo->m_wheel);
                }
                else if (timerInfo->mmTimerId != 0) 
                {
                    XEventContext_Timer* ctx = (XEventContext_Timer*)timerInfo->highResContext;
                    ctx->id = 0;
                    timeKillEvent((MMRESULT)timerInfo->mmTimerId);
                    timerInfo->mmTimerId = 0;
                    d->highPrecisionTimerCount--;
                    clearHighPrecisionTimer(ctx);
                }
            }
            else {
                KillTimer(self->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
            }
            //将id放回列表
            XVector_push_back_base(d->m_dp.m_timerIds, &timerInfo->timerId);
            XHashMap_erase_base(d->timers, &it, &it);
            found = true;
        }
        else {
            XHashMap_iterator_add(d->timers, &it);
        }
    }
    // 如果这是最后一个高精度定时器，恢复系统时钟精度
    if (d->highPrecisionTimerCount == 0 && d->timePeriodSet) {
        timeEndPeriod(1);
        d->timePeriodSet = false;
    }
    XMutex_unlock(GetXMutex(dispatcher));
    return found;
}

static XVector* VXEventDispatcherWin32_timersForObject(const XAbstractEventDispatcher* dispatcher, const XObject* object)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    XVector* result = XVector_Create(XAbstractEventDispatcher_TimerInfoV2);
    if (!result) return NULL;

    XMutex_lock(GetXMutex(dispatcher));

    XHashMap_iterator it = XHashMap_begin(d->timers);
    while (!XHashMap_iterator_isEnd(&it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
        if (timerInfo && timerInfo->object == (XObject*)object) {
            XAbstractEventDispatcher_TimerInfoV2 info = {
                .interval = timerInfo->interval,
                .timerId = timerInfo->timerId,
                .timerType = timerInfo->timerType
            };
            XVector_push_back_base(result, &info);
        }
        XHashMap_iterator_add(d->timers, &it);
    }

    XMutex_unlock(GetXMutex(dispatcher));
    return result;
}

static XDuration VXEventDispatcherWin32_remainingTime(const XAbstractEventDispatcher* dispatcher, XTimerId timerId)
{
    // Windows API 不提供直接获取剩余时间的方法
    // 这里返回 -1 表示不支持，或者可以尝试估算
    (void)dispatcher; (void)timerId;
    return -1;
}

static void VXEventDispatcherWin32_wakeUp(XAbstractEventDispatcher* dispatcher)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    // 防止重复发送
    if (!self->wakeUpSent)
    {
        self->wakeUpSent = true;
        //XPrintf("发送唤醒\n");
        PostMessage(self->internalHwnd, XDISPATCHER_WM_WAKEUP, 0, 0);
    }
}

static void VXEventDispatcherWin32_interrupt(XAbstractEventDispatcher* dispatcher)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);

    self->interrupt = true;
    VXEventDispatcherWin32_wakeUp(dispatcher); // 确保循环能退出
}

static void VXEventDispatcherWin32_startingUp(XAbstractEventDispatcher* dispatcher)
{
    (void)dispatcher;
    // Windows 平台可能需要在此初始化一些全局资源
}

static void VXEventDispatcherWin32_closingDown(XAbstractEventDispatcher* dispatcher)
{
    (void)dispatcher;
    // Windows 平台可能需要在此清理一些全局资源
}

// ========================
// 对象生命周期管理
// ========================

static void VXEventDispatcherWin32_deinit(XObject* obj)
{
    //XPrintf("事件调度器清理\n");
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)obj;
    MainThreadDataPrivate* d = PlatformPrivate(obj);
    if (d)
    {
        // 清理所有定时器
        XMutex_lock(d->m_dp.mutex);
        // --- 修复点 9: 正确遍历并清理 timers HashMap ---
        XHashMap_iterator it_timers = XHashMap_begin(d->timers);
        while (!XHashMap_iterator_isEnd(&it_timers)) {
            XPair* pair = XHashMap_iterator_data(&it_timers);
            XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
            if (timerInfo) {
                if (timerInfo->isHighPrecision) {
                    if (timerInfo->winTimerId != 0) {
                        timeKillEvent((MMRESULT)timerInfo->mmTimerId);
                        d->highPrecisionTimerCount--;
                    }
                }
                else {
                    KillTimer(self->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
                }
            }
            // 注意：不能在这里 erase，因为 deinit 不需要保留容器结构
            // 我们只是释放数据，容器本身会在 delete_base 中销毁
            XHashMap_iterator_add(d->timers, &it_timers);
        }
        // --- 恢复系统时钟精度 ---
        if (d->highPrecisionTimerCount > 0 && d->timePeriodSet) {
            timeEndPeriod(1);
            d->timePeriodSet = false;
        }
        // 清理所有套接字
        //XHashMap_iterator it_sockets = XHashMap_begin(d->sockets);
        //while (!XHashMap_iterator_isEnd(&it_sockets)) {
        //    XPair* pair = XHashMap_iterator_data(&it_sockets);
        //    XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);
        //    if (sockInfo) {
        //        SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(sockInfo->socket);
        //        WSAAsyncSelect(s, NULL, 0, 0);
        //        //XVector_delete_base(sockInfo->notifiers);
        //        XMemory_free(sockInfo);
        //    }
        //    XHashMap_iterator_add(d->sockets, &it_sockets);
        //}
        XMutex_unlock(d->m_dp.mutex);
        // 清理本地过滤器
        XAbstractEventDispatcherPrivate_deinit(d);
        // 销毁容器
        if (d->timers)
        {
            XHashMap_delete_base(d->timers);
            d->timers = NULL;
        }
        if (d->sockets)
        {
            XHashMap_delete_base(d->sockets);
            d->sockets = NULL;
        }
        XMemory_free(d);
    }
   

    if (self->internalHwnd) {
        DestroyWindow(self->internalHwnd);
        self->internalHwnd = NULL;
    }

   
    PlatformPrivate(obj) = NULL;
    XClass_Deinit_Parent(XAbstractEventDispatcher, obj);
}

XVtable* XEventDispatcherWin32_class_init()
{
    XVTABLE_CREAT_DEFAULT // static XVtable* XVTABLE_DEFAULT = NULL; if exists return

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XAbstractEventDispatcher))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

    // 继承 XObject 的虚函数表
    XVTABLE_INHERIT_XCLASS(XAbstractEventDispatcher);
    //XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_ProcessEvents, (void*)VXEventDispatcherWin32_processEvents);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_RegisterSocketNotifier, (void*)VXEventDispatcherWin32_registerSocketNotifier);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterSocketNotifier, (void*)VXEventDispatcherWin32_unregisterSocketNotifier);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_RegisterTimer, (void*)VXEventDispatcherWin32_registerTimer);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterTimer, (void*)VXEventDispatcherWin32_unregisterTimer);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterTimers, (void*)VXEventDispatcherWin32_unregisterTimers);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_TimersForObject, (void*)VXEventDispatcherWin32_timersForObject);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_RemainingTime, (void*)VXEventDispatcherWin32_remainingTime);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_WakeUp, (void*)VXEventDispatcherWin32_wakeUp);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_Interrupt, (void*)VXEventDispatcherWin32_interrupt);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_StartingUp, (void*)VXEventDispatcherWin32_startingUp);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_ClosingDown, (void*)VXEventDispatcherWin32_closingDown);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, (void*)VXEventDispatcherWin32_deinit);
#if SHOWCONTAINERSIZE
    printf("XEventDispatcher size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}
//数据释放的时候清理内存
static void timersDataDeinit(XEventDispatcherWin32_TimerInfo* info)
{
    if (info && info->highResContext)
        XMemory_free(info->highResContext);
}
// ========================
// 工厂函数
// ========================

XAbstractEventDispatcher* XEventDispatcher_create(XObject* parent)
{
    XEventDispatcherWin32* self = XNew(XEventDispatcherWin32);
    if (!self) return NULL;

    // 初始化基类
    XAbstractEventDispatcher_init(self, parent);
    XClassGetVtable(self) = XEventDispatcherWin32_class_init();
    Set_Class_MemoryFree(self, XFree);
    MainThreadDataPrivate* d = NULL;
    if (XThread_isMainThread())
    {
        d=(MainThreadDataPrivate*)XMemory_calloc(1, sizeof(MainThreadDataPrivate));
        if (!d) {
            XMemory_free(self);
            return NULL;
        }
        ((XAbstractEventDispatcher*)self)->type = XDISPATCHER_THREAD_TYPE_MAIN;
        XAbstractEventDispatcherPrivate_init(&d->m_dp);
        //d->m_dp.m_timerIds = XVector_Create(XTimerId);
        d->timers = NULL;
        d->sockets = NULL;

        d->timers = XHashMap_Create(size_t, XEventDispatcherWin32_TimerInfo, size_t_compare);
        XContainerSetDataDeinitMethod(d->timers, timersDataDeinit);
        d->sockets = XHashMap_Create(intptr_t, XVector, int_compareptr_t);

        if (!d->timers || !d->sockets) {
            // 错误处理
            if (d->timers) XHashMap_delete_base(d->timers);
            if (d->sockets) XHashMap_delete_base(d->sockets);
            XMemory_free(d);
            XMemory_free(self);
            return NULL;
        }
        d->ioCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    }
    else
    {
      
        ((XAbstractEventDispatcher*)self)->type = XDISPATCHER_THREAD_TYPE_WORKER;
    }
    self->m_class.d_ptr = d;

    // 创建内部消息窗口
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = XEventDispatcherWin32_WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "XEventDispatcherInternalWindow";
    if (!RegisterClass(&wc)) {
        // 可能已经注册，忽略错误
    }
    self->internalHwnd = 0;
    self->internalHwnd = CreateWindowEx(
        0, "XEventDispatcherInternalWindow", NULL,
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL
    );

    if (!self->internalHwnd)
    {
        if(XAbstractEventDispatcher_isMainThread(self))
        {
            XHashMap_delete_base(d->timers);
            XHashMap_delete_base(d->sockets);
            XAbstractEventDispatcherPrivate_deinit(&d->m_dp);
            XMemory_free(d);
        }
        XMemory_free(self);
        return NULL;
    }

    // 将 dispatcher 指针存入窗口
    SetWindowLongPtr(self->internalHwnd, GWLP_USERDATA, (LONG_PTR)self);
    

    if (!XThread_isMainThread()) {
        // 子线程需初始化 COM 和 OLE
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    }

   
    return self;
}

#endif