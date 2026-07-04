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
#include "XAbstractSocket.h"
#include "XIODevicePrivate.h"
#include "XCoreApplication.h"
#include "XListSLinked.h"
#include "XFileDescriptor.h"
#include <string.h>
#include <stdint.h>
#include "XThreadData.h"
#include "XTimeWheelGroup.h"
#include "XHrTimerGroup.h"
#include "XDateTime.h"
#pragma comment(lib, "winmm.lib")
static HANDLE global_ioCompletionPort=NULL;
uint64_t get_current_nanoseconds_since_epoch();
//static  HANDLE global_ioCompletionPort=NULL;    // 全局 IOCP 句柄;
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
        XHandle* m_wheel;  ///时间轮定时器句柄
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
   
    //XHashMap* timers;           ///< 定时器映射: timerId  -> XEventDispatcherWin32_TimerInfo*
    //XHashMap* sockets;          ///< 套接字映射: socket.value -> XEventDispatcherWin32_SocketInfo*
    //bool timePeriodSet;          ///< 是否已调用 timeBeginPeriod(1)
    //int highPrecisionTimerCount; ///< 当前活跃的高精度定时器数量
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
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, DWORD bytesTransferred,
    ULONG_PTR completionKey,
    LPOVERLAPPED overlapped);
static void XEventDispatcherWin32_handleTimerMessage(XEventDispatcherWin32* dispatcher, UINT_PTR timerId);
//iocp绑定
bool IOCP_bind(XSocketDescriptor socket,XObject* obj);
//获取全局IOCP端口
HANDLE IOCP_getGlobalPort(void);
// Windows 消息常量
#define XDISPATCHER_WM_SOCKET (WM_USER + 1)
#define XDISPATCHER_WM_WAKEUP (WM_USER + 2)

#define PlatformPrivate(Dispatcher)  ((MainThreadDataPrivate*)((XAbstractEventDispatcher*)Dispatcher)->d_ptr)
#define GetXMutex(Dispatcher)         PlatformPrivate(Dispatcher)->m_dp.mutex
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
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, DWORD bytesTransferred,
    ULONG_PTR completionKey,
    LPOVERLAPPED overlapped)
{
    XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    
    // 保存传输字节数到 IOCP 上下文
    ioCtx->finishedBytes = bytesTransferred;

    // 计算实际活动类型
    int actType = XSocketAct_Invalid;
    if (ioCtx->eventMask & FD_READ)
        actType |= XSocketAct_Read;
    if (ioCtx->eventMask & FD_WRITE)
        actType |= XSocketAct_Write;
    if (ioCtx->eventMask & FD_CONNECT)
        actType |= XSocketAct_Connect;
    if (ioCtx->eventMask & FD_ACCEPT)
        actType |= XSocketAct_Accept;

    // 仅在有实际活动时创建并投递事件
    if (actType != XSocketAct_Invalid && completionKey)
    {
        XEventSockAct* event = XEventSockAct_create(ioCtx->base.fd, (XSocketActType)actType);
        if (event)
        {
            // 0 字节 + FD_READ 对套接字表示对端 FIN，发送关闭事件；对文件/串口则忽略
            if (bytesTransferred == 0 && (actType & XSocketAct_Read) && ioCtx->base.type == XEventContextType_Type_Socket)
            {
                XEvent* closeEvent = XEventSockClose_create(ioCtx->base.fd);
                if (closeEvent)
                {
                    closeEvent->posted = true;
                    closeEvent->spontaneous = true;
                    XCoreApplication_postEvent((XObject*)completionKey, closeEvent, XEVENT_PRIORITY_NORMAL);
                }
                XEvent_delete_base((XEvent*)event);
                return;
            }
            XEvent* e = (XEvent*)event;
            e->posted = true;
            e->spontaneous = true;
            XCoreApplication_postEvent((XObject*)completionKey, event, XEVENT_PRIORITY_NORMAL);
        }
    }
    /* 套接字/串口设备 notifier 统一查找
     * 通过 XEventContext 基类直接获取 xfd，无需再猜设备类型 */
    {
        XFd xfd = ((XEventContext*)overlapped)->fd;
        if (xfd >= 0 && dispatcher->m_class.d_ptr && dispatcher->m_class.d_ptr->notifiers)
        {
            XVector* v = (XVector*)XHashMap_value_base(dispatcher->m_class.d_ptr->notifiers, &xfd);
            if (v)
            {
                for_each_iterator(v, XVector, it)
                {
                    XSocketNotifier** lp = XVector_iterator_data(&it);
                    if (lp && *lp)
                    {
                        XSocketNotifier* notifier = *lp;
                        if (!XSocketNotifier_isEnabled(notifier))
                            continue;
                        XSocketNotifierType t = XSocketNotifier_type(notifier);
                        if ((t & XSocketNotifier_Read && ioCtx->eventMask & FD_READ) ||
                            (t & XSocketNotifier_Write && ioCtx->eventMask & FD_WRITE))
                        {
                            XSocketNotifier_activated_signal(notifier, ioCtx->socket, t);
                        }
                    }
                }
            }
        }
    }
}

/**
 * @brief 处理定时器消息
 */
static void XEventDispatcherWin32_handleTimerMessage(XEventDispatcherWin32* dispatcher, UINT_PTR timerId)
{
   /* MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
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

    XMutex_unlock(GetXMutex(dispatcher));*/
}

bool IOCP_bind(XSocketDescriptor socket, XObject* obj)
{
   /* XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);*/
   /* XPrintf("IOCP_bind:%p\n", obj);*/
    return CreateIoCompletionPort((HANDLE)XSocketDescriptor_toIntptr(socket), global_ioCompletionPort, obj, 0);
}

HANDLE IOCP_getGlobalPort(void)
{
    return global_ioCompletionPort;
}
static void IOCP_handle(XAbstractEventDispatcher* dispatcher)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    MainThreadDataPrivate* d = PlatformPrivate(dispatcher);
    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    LPOVERLAPPED overlapped = NULL;

    BOOL success = GetQueuedCompletionStatus(
        global_ioCompletionPort,
        &bytesTransferred,
        &completionKey,
        &overlapped,
        0 // 非阻塞轮询
    );

    if (overlapped != NULL) 
    {
        XEventContext* ctx = (XEventContext*)overlapped;
        XEventContext_IOCP* ioCtx = (XEventContext_IOCP*)overlapped;
        if(success)
        {
            if (ctx->type == XEventContextType_Type_Timer)
            {
                XEventDispatcherWin32_handleTimerMessage(dispatcher, (UINT_PTR)ctx->fd);
            }
            else if (ctx->type == XEventContextType_Type_Socket || ctx->type == XEventContextType_Type_File)
            {
                XEventDispatcherWin32_handleSocketMessage(dispatcher, bytesTransferred, completionKey, overlapped);
            }
        }
        else
        {
            // I/O 失败完成，处理连接/设备断开
            DWORD lastError = GetLastError();
            bool isConnectionLost = false;
            if (ioCtx->base.type == XEventContextType_Type_Socket)
            {
                isConnectionLost = (lastError == ERROR_NETNAME_DELETED ||      // 64
                                    lastError == WSAENETRESET ||               // 10052
                                    lastError == WSAECONNRESET ||              // 10054
                                    lastError == ERROR_OPERATION_ABORTED);     // 995
            }
            else if (ioCtx->base.type == XEventContextType_Type_File)
            {
                isConnectionLost = (lastError == ERROR_OPERATION_ABORTED ||    // 995
                                    lastError == ERROR_DEVICE_NOT_CONNECTED || // 1167
                                    lastError == ERROR_DEVICE_REMOVED);        // 1617
            }
            if (isConnectionLost && completionKey)
            {
                XEvent* closeEvent = XEventSockClose_create(ioCtx->base.fd);
                if (closeEvent) {
                    closeEvent->posted = true;
                    closeEvent->spontaneous = true;
                    XCoreApplication_postEvent((XObject*)completionKey, closeEvent, XEVENT_PRIORITY_NORMAL);
                }
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
           
            //DWORD waitRet = MsgWaitForMultipleObjectsEx(0, NULL, XAbstractEventDispatcher_isMainThread(dispatcher)? 1:INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            int64_t time = INFINITE;
            if (dispatcher->d_ptr->m_hrtimerGroup&& XHrTimerGroup_count(dispatcher->d_ptr->m_hrtimerGroup))
            {
                time=(XHrTimerGroup_getNextExpireTime(dispatcher->d_ptr->m_hrtimerGroup)- XDateTime_currentNSecsSinceEpoch())/ 1000000;
               // time = 100 / 1000000;
                if (time > 99999999999)
                {
                    //XPrintf("时间异常\n");
                    time = 0;
                }
            }
            if(time)
            {
                XAbstractEventDispatcher_aboutToBlock_signal(dispatcher);
                DWORD waitRet = MsgWaitForMultipleObjectsEx(0, NULL, time, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                //XPrintf("XThread:%p 被唤醒\n", XThread_currentThread());
                if (waitRet != WAIT_FAILED)
                {
                    XAbstractEventDispatcher_awake_signal(dispatcher);
                    // 被唤醒后，确保 wakeUpSent 标志会在消息处理中重置
                    // 实际的消息处理会在下一次 processEvents 调用中完成
                }
            }
        }
    }

    self->interrupt = false;
    return processed;
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
    //if (XAbstractEventDispatcher_isMainThread(self)&& d)
    //{
    //    // 清理本地过滤器
    //    XAbstractEventDispatcherPrivate_deinit(d);
    //    XFree_System(d);
    //}
    //else if(d)
    //{
    //    XAbstractEventDispatcherPrivate_deinit(d);
    //    XFree_System(d);
    //}
   

    if (self->internalHwnd) {
        DestroyWindow(self->internalHwnd);
        self->internalHwnd = NULL;
    }

   
    //PlatformPrivate(obj) = NULL;
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
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_RegisterSocketNotifier, (void*)VXEventDispatcherWin32_registerSocketNotifier);
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterSocketNotifier, (void*)VXEventDispatcherWin32_unregisterSocketNotifier);
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_RegisterTimer, (void*)VXEventDispatcherWin32_registerTimer);
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterTimer, (void*)VXEventDispatcherWin32_unregisterTimer);
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_UnregisterTimers, (void*)VXEventDispatcherWin32_unregisterTimers);
    //XVTABLE_OVERLOAD_DEFAULT(EXAbstractEventDispatcher_TimersForObject, (void*)VXEventDispatcherWin32_timersForObject);
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
        XFree_System(info->highResContext);
}
// ========================
// 工厂函数
// ========================

XAbstractEventDispatcher* XEventDispatcher_create(XObject* parent)
{
    if(!global_ioCompletionPort)
        global_ioCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    XEventDispatcherWin32* self = XNew(XEventDispatcherWin32);
    if (!self) return NULL;

    // 初始化基类
    XAbstractEventDispatcher_init(self, parent);
    XClassGetVtable(self) = XEventDispatcherWin32_class_init();
    Set_Class_MemoryFree(self, XFree_System);
    MainThreadDataPrivate* d = NULL;
    if (XThread_isMainThread())
    {
        d=(MainThreadDataPrivate*)XCalloc_System(1, sizeof(MainThreadDataPrivate));
        if (!d) {
            XFree_System(self);
            return NULL;
        }
        ((XAbstractEventDispatcher*)self)->type = XDISPATCHER_THREAD_TYPE_MAIN;
        XAbstractEventDispatcherPrivate_init(&d->m_dp);
        //d->m_dp.m_timerIds = XVector_Create(XTimerId);
        /*d->timers = NULL;
        d->sockets = NULL;*/

        //d->timers = XHashMap_Create(size_t, XEventDispatcherWin32_TimerInfo, size_t_compare);
        //XContainerSetDataDeinitMethod(d->timers, timersDataDeinit);
        //d->sockets = XHashMap_Create(intptr_t, XVector, int_compareptr_t);

        //if (!d->timers || !d->sockets) {
        //    // 错误处理
        //    if (d->timers) XHashMap_delete_base(d->timers);
        //    if (d->sockets) XHashMap_delete_base(d->sockets);
        //    XFree_System(d);
        //    XFree_System(self);
        //    return NULL;
        //}
       
    }
    else
    {
        d = (XAbstractEventDispatcherPrivate*)XCalloc_System(1, sizeof(XAbstractEventDispatcherPrivate));
        XAbstractEventDispatcherPrivate_init(d);
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
           /* XHashMap_delete_base(d->timers);
            XHashMap_delete_base(d->sockets);*/
            XAbstractEventDispatcherPrivate_deinit(&d->m_dp);
            XFree_System(d);
        }
        XFree_System(self);
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