// XEventDispatcher_win.c
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#include "XPair.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XCoreApplication.h"
#include "XListSLinked.h"
#include <string.h>
#include <stdint.h>
#pragma comment(lib, "winmm.lib")
/**
 * @brief 定时器信息结构体 (Windows 私有)
 */
typedef struct {
    XTimerId timerId;           ///< 定时器 ID
    union
    {
        UINT_PTR winTimerId;        ///< Windows 定时器 ID (来自 SetTimer)
        MMRESULT mmTimerId;         ///< 多媒体定时器 ID (来自 timeSetEvent)，用于高精度定时器
    };
    bool isHighPrecision;       ///< 标记是否为高精度定时器 
    XTimerType timerType;       ///< 定时器类型
    XObject* object;            ///< 关联的对象   
    int64_t interval;           ///< 间隔 (纳秒)
    void* highResContext;       // 指向 HighResTimerContext 的指针
} XEventDispatcherWin32_TimerInfo;

/**
 * @brief 套接字信息结构体 (Windows 私有)
 *
 * 使用 WSAAsyncSelect 模型，每个套接字关联一个窗口句柄。
 */
typedef struct XEventDispatcherWin32_SocketInfo {
    XSocketDescriptor socket;   ///< 套接字描述符
    HWND hwnd;                  ///< 用于接收网络事件的隐藏窗口句柄
    long eventMask;             ///< 当前注册的事件掩码 (FD_READ, FD_WRITE 等)
    XListSLinked* notifiers;    ///< 该套接字上注册的通知器列表
} XEventDispatcherWin32_SocketInfo;

/**
 * @brief Windows 平台私有数据
 */
typedef struct
{
    XAbstractEventDispatcherPrivate m_dp;
    HWND internalHwnd;          ///< 内部消息窗口句柄，用于接收定时器和网络事件
    XHashMap* timers;           ///< 定时器映射: timerId  -> XEventDispatcherWin32_TimerInfo*
    XHashMap* sockets;          ///< 套接字映射: socket.value -> XEventDispatcherWin32_SocketInfo*
    bool interrupt;             ///< 中断标志，用于 interrupt()
    bool wakeUpSent;            ///< 标记是否已发送 WM_USER 消息用于 wakeUp()
    bool timePeriodSet;          ///< 是否已调用 timeBeginPeriod(1)
    int highPrecisionTimerCount; ///< 当前活跃的高精度定时器数量
} XEventDispatcherWin32PlatformPrivate;

typedef struct XEventDispatcherWin32
{
    XAbstractEventDispatcher m_class; ///< 继承自 XObject
} XEventDispatcherWin32;
XVtable* XEventDispatcherWin32_class_init();
// 辅助函数声明
static LRESULT CALLBACK XEventDispatcherWin32_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void XEventDispatcherWin32_handleTimerMessage(XEventDispatcherWin32* dispatcher, UINT_PTR timerId);
static XEventDispatcherWin32_SocketInfo* XEventDispatcherWin32_findOrCreateSocketInfo(XEventDispatcherWin32* dispatcher, XSocketDescriptor socket);
static void XEventDispatcherWin32_updateSocketEventMask(XEventDispatcherWin32_SocketInfo* sockInfo);

// Windows 消息常量
#define XDISPATCHER_WM_SOCKET (WM_USER + 1)
#define XDISPATCHER_WM_WAKEUP (WM_USER + 2)

#define PlatformPrivate(Dispatcher)  ((XEventDispatcherWin32PlatformPrivate*)((XAbstractEventDispatcher*)Dispatcher)->d_ptr)
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
        if (dispatcher) {
            XEventDispatcherWin32_handleSocketMessage(dispatcher, hwnd, msg, wParam, lParam);
        }
        return 0;
    }
    else if (msg == XDISPATCHER_WM_WAKEUP) {
        // 唤醒事件
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
static void XEventDispatcherWin32_handleSocketMessage(XEventDispatcherWin32* dispatcher, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd; (void)msg;
    SOCKET socket = (SOCKET)wParam;
    int event = WSAGETSELECTEVENT(lParam);
    int error = WSAGETSELECTERROR(lParam);

    if (error != 0) {
        event = FD_CLOSE;
    }

    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);
    XMutex_lock(GetXMutex(dispatcher));

    intptr_t socket_key = (intptr_t)socket;
    XHashMap_iterator it;
    if (XHashMap_find_base(d->sockets, &socket_key, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);

        for (size_t i = 0; i < XVector_size_base(sockInfo->notifiers); ++i) {
            XSocketNotifier* notifier = *(XSocketNotifier**)XVector_at_base(sockInfo->notifiers, i);
            if (notifier && XSocketNotifier_isEnabled(notifier)) {
                XSocketNotifierType type = XSocketNotifier_type(notifier);
                if ((type == XSocketNotifier_Read && (event & (FD_READ | FD_ACCEPT | FD_CLOSE))) ||
                    (type == XSocketNotifier_Write && (event & (FD_WRITE | FD_CONNECT))) ||
                    (type == XSocketNotifier_Exception && (event & FD_OOB))) {
                    XSocketNotifier_activated_signal(notifier, XSocketDescriptor_fromIntptr((intptr_t)socket), type);
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
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);
    XMutex_lock(GetXMutex(dispatcher));

    size_t timer_key = (size_t)timerId;
    XHashMap_iterator it;
    if (XHashMap_find_base(d->timers, &timer_key, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
        if (timerInfo && timerInfo->object) {
            XEvent* timerEvent = XEventTimer_create(timerInfo->timerId);
            //timerEvent->spontaneous = false;
            timerEvent->posted = true;
            XCoreApplication_postEvent(timerInfo->object, timerEvent, XEVENT_PRIORITY_NORMAL);
        }
    }

    XMutex_unlock(GetXMutex(dispatcher));
}

/**
 * @brief 查找或创建套接字信息
 */
static XEventDispatcherWin32_SocketInfo* XEventDispatcherWin32_findOrCreateSocketInfo(XEventDispatcherWin32* dispatcher, XSocketDescriptor socket)
{
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);
    SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(socket);
    intptr_t socket_key = (intptr_t)s;

    XHashMap_iterator it;
    if (XHashMap_find_base(d->sockets, &socket_key, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        return (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);
    }
    else {
        XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XMemory_calloc(1, sizeof(XEventDispatcherWin32_SocketInfo));
        if (!sockInfo) return NULL;
        sockInfo->socket = socket;
        sockInfo->hwnd = d->internalHwnd;
        sockInfo->notifiers = XVector_Create(XSocketNotifier*);
        if (!sockInfo->notifiers) {
            XMemory_free(sockInfo);
            return NULL;
        }

        // 插入到 sockets HashMap
        //XPair* newPair = XPair_create(sizeof(intptr_t), sizeof(XEventDispatcherWin32_SocketInfo*));
        //XPair_insert(newPair, &socket_key, &sockInfo);
        XHashMap_insert_base(d->sockets, &socket_key, &sockInfo);
        //XPair_delete(newPair);

        return sockInfo;
    }
}

/**
 * @brief 更新套接字的事件掩码
 */
static void XEventDispatcherWin32_updateSocketEventMask(XEventDispatcherWin32_SocketInfo* sockInfo)
{
    long newMask = 0;
    for (size_t i = 0; i < XVector_size_base(sockInfo->notifiers); ++i) {
        XSocketNotifier* notifier = *(XSocketNotifier**)XVector_at_base(sockInfo->notifiers, i);
        if (notifier && XSocketNotifier_isEnabled(notifier)) {
            XSocketNotifierType type = XSocketNotifier_type(notifier);
            if (type == XSocketNotifier_Read) newMask |= FD_READ | FD_ACCEPT | FD_CLOSE;
            else if (type == XSocketNotifier_Write) newMask |= FD_WRITE | FD_CONNECT;
            else if (type == XSocketNotifier_Exception) newMask |= FD_OOB;
        }
    }

    if (newMask != sockInfo->eventMask) {
        SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(sockInfo->socket);
        if (newMask == 0) {
            WSAAsyncSelect(s, sockInfo->hwnd, 0, 0);
        }
        else {
            WSAAsyncSelect(s, sockInfo->hwnd, XDISPATCHER_WM_SOCKET, newMask);
        }
        sockInfo->eventMask = newMask;
    }
}

// ========================
// 虚函数实现
// ========================

static bool VXEventDispatcherWin32_processEvents(XAbstractEventDispatcher* dispatcher, XEventLoopProcessEventsFlags flags)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    // 发送 awake 信号
    XAbstractEventDispatcher_awake_signal(dispatcher);

    MSG msg;
    bool processed = false;

    // 先处理所有已排队的消息（PeekMessage 不会阻塞）
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            XEvent* quitEvent = XEvent_create(XEVENT_TYPE_QUIT);
            XCoreApplication_postEvent(XCoreApplication_instance(), quitEvent, XEVENT_PRIORITY_HIGH);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg); // ← 这会调用 WndProc（处理 socket/timer 等）
        processed = true;
    }

    // 【关键修改】只有设置了 WaitForMoreEvents，并且当前没有中断，才进入等待
    if ((flags & XEventLoop_WaitForMoreEvents) && !d->interrupt) {
        XAbstractEventDispatcher_aboutToBlock_signal(dispatcher);
        DWORD waitRet = MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waitRet != WAIT_FAILED) {
            // 被唤醒后，可以再次尝试处理新消息（可选，通常由下一次 processEvents 调用处理）
            // 这里我们不立即处理，保持与 Qt 一致：WaitForMoreEvents 只保证“至少等一个事件到来”
            // 实际处理留到下次调用
        }
    }

    d->interrupt = false; // 重置中断标志
    return (XVtableGetFunc(XAbstractEventDispatcher_class_init(), EXAbstractEventDispatcher_ProcessEvents, bool(*)(XAbstractEventDispatcher*, XEventLoopProcessEventsFlags))(dispatcher,flags));
}

static void VXEventDispatcherWin32_registerSocketNotifier(XAbstractEventDispatcher* dispatcher, XSocketNotifier* notifier)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    XSocketDescriptor socket = XSocketNotifier_socket(notifier);
    if (!XSocketDescriptor_isValid(socket)) return;

    XMutex_lock(GetXMutex(dispatcher));
    XEventDispatcherWin32_SocketInfo* sockInfo = XEventDispatcherWin32_findOrCreateSocketInfo(self, socket);
    if (sockInfo) {
        XVector_push_back_base(sockInfo->notifiers, &notifier);
        XEventDispatcherWin32_updateSocketEventMask(sockInfo);
    }
    XMutex_unlock(GetXMutex(dispatcher));
}

static void VXEventDispatcherWin32_unregisterSocketNotifier(XAbstractEventDispatcher* dispatcher, XSocketNotifier* notifier)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    XSocketDescriptor socket = XSocketNotifier_socket(notifier);
    if (!XSocketDescriptor_isValid(socket)) return;

    XMutex_lock(GetXMutex(dispatcher));
    intptr_t socket_key = XSocketDescriptor_toIntptr(socket);
    XHashMap_iterator it;
    if (XHashMap_find_base(d->sockets, &socket_key, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);

        for (size_t i = 0; i < XVector_size_base(sockInfo->notifiers); ++i) {
            XSocketNotifier** pnotifier = (XSocketNotifier**)XVector_at_base(sockInfo->notifiers, i);
            if (*pnotifier == notifier) {
                XVector_removeAt_base(sockInfo->notifiers, i);
                break;
            }
        }
        XEventDispatcherWin32_updateSocketEventMask(sockInfo);

        if (XVector_isEmpty_base(sockInfo->notifiers)) {
            SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(socket);
            WSAAsyncSelect(s, NULL, 0, 0);
            XHashMap_remove_base(d->sockets, &socket_key);
            XVector_delete_base(sockInfo->notifiers);
            XMemory_free(sockInfo);
        }
    }
    XMutex_unlock(GetXMutex(dispatcher));
}
// 用于高精度定时器回调的上下文
typedef struct {
    XEventDispatcherWin32* dispatcher;
    XTimerId timerId;
} HighResTimerContext;
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
    HighResTimerContext* ctx = (HighResTimerContext*)dwUser;
    if (!ctx)return;
    if (ctx->dispatcher) {
        XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(ctx->dispatcher);
        // 转发到内部窗口，复用 WM_TIMER 消息
        PostMessage(d->internalHwnd, WM_TIMER, (WPARAM)ctx->timerId, 0);
    }
}
static void VXEventDispatcherWin32_registerTimer(XAbstractEventDispatcher* dispatcher, XTimerId timerId, XDuration intervalNs, XTimerType timerType, XObject* object)
{
    if (!timerId || !intervalNs || !object)return;
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

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
    timerInfo.highResContext = NULL;
    // --- 核心逻辑：根据 XTimerType 决定使用哪种定时器 ---
    if (timerType == XTimerType_PreciseTimer) {
        // --- 使用高精度多媒体定时器 ---
        timerInfo.isHighPrecision = true;

        // 全局设置系统时钟精度（仅在第一个高精度定时器注册时调用）
        if (!d->timePeriodSet) {
            timeBeginPeriod(1); // 请求 1ms 系统时钟精度
            d->timePeriodSet = true;
        }
        // --- 关键修改：分配回调上下文 ---
        HighResTimerContext* ctx = (HighResTimerContext*)XMemory_malloc(sizeof(HighResTimerContext));
        if (!ctx) {
            XMutex_unlock(GetXMutex(dispatcher));
            return; // 内存不足
        }
        ctx->dispatcher = self;
        ctx->timerId = timerId;
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
            timerInfo.winTimerId = SetTimer(d->internalHwnd, (UINT_PTR)timerId, uInterval, NULL);
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

        timerInfo.winTimerId = SetTimer(d->internalHwnd, (UINT_PTR)timerId, uVeryCoarseInterval, NULL);
    }
    else {
        // --- XTimerType_CoarseTimer: 使用普通的窗口定时器 ---
        timerInfo.isHighPrecision = false;
        timerInfo.winTimerId = SetTimer(d->internalHwnd, (UINT_PTR)timerId, uInterval, NULL);
    }

    // 保存到 timers 表（无论哪种方式，只要有一个ID有效）
    bool hasValidId = (timerInfo.isHighPrecision && timerInfo.mmTimerId != 0) ||
        (!timerInfo.isHighPrecision && timerInfo.winTimerId != 0);

    if (hasValidId) {
        XHashMap_insert_base(d->timers, &timerId, &timerInfo);
    }

    XMutex_unlock(GetXMutex(dispatcher));
}
static bool VXEventDispatcherWin32_unregisterTimer(XAbstractEventDispatcher* dispatcher, XTimerId timerId)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    bool found = false;
    XMutex_lock(GetXMutex(dispatcher));
    XEventDispatcherWin32_TimerInfo* timerInfo = XHashMap_value_base(d->timers,&timerId);
    if (!timerInfo)return false;
    if (timerInfo->isHighPrecision) {
        if (timerInfo->mmTimerId != 0) {
            timeKillEvent((MMRESULT)timerInfo->mmTimerId);
            timerInfo->mmTimerId = 0;
            d->highPrecisionTimerCount--;

            // 如果这是最后一个高精度定时器，恢复系统时钟精度
            if (d->highPrecisionTimerCount == 0 && d->timePeriodSet) {
                timeEndPeriod(1);
                d->timePeriodSet = false;
            }
        }
    }
    else {
        KillTimer(d->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
    }
    //将id放回列表
    XVector_push_back_base(d->m_dp.m_timerIds,&timerId);
    XHashMap_remove_base(d->timers, &timerId);
    XMutex_unlock(GetXMutex(dispatcher));
    return found;
}

static bool VXEventDispatcherWin32_unregisterTimers(XAbstractEventDispatcher* dispatcher, XObject* object)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    bool found = false;
    XMutex_lock(GetXMutex(dispatcher));

    XHashMap_iterator it = XHashMap_begin(d->timers);
    while (!XHashMap_iterator_isEnd(&it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
        if (timerInfo && timerInfo->object == object) 
        {
            if (timerInfo->isHighPrecision) {
                if (timerInfo->mmTimerId != 0) {
                    timeKillEvent((MMRESULT)timerInfo->mmTimerId);
                    timerInfo->mmTimerId = 0;
                    d->highPrecisionTimerCount--;
                }
            }
            else {
                KillTimer(d->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
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
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

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
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    // 防止重复发送
    if (!d->wakeUpSent) {
        d->wakeUpSent = true;
        PostMessage(d->internalHwnd, XDISPATCHER_WM_WAKEUP, 0, 0);
    }
}

static void VXEventDispatcherWin32_interrupt(XAbstractEventDispatcher* dispatcher)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    d->interrupt = true;
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
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)obj;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(obj);

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
                KillTimer(d->internalHwnd, (UINT_PTR)timerInfo->winTimerId);
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
    XHashMap_iterator it_sockets = XHashMap_begin(d->sockets);
    while (!XHashMap_iterator_isEnd(&it_sockets)) {
        XPair* pair = XHashMap_iterator_data(&it_sockets);
        XEventDispatcherWin32_SocketInfo* sockInfo = (XEventDispatcherWin32_SocketInfo*)XPair_second(pair);
        if (sockInfo) {
            SOCKET s = (SOCKET)XSocketDescriptor_toIntptr(sockInfo->socket);
            WSAAsyncSelect(s, NULL, 0, 0);
            XVector_delete_base(sockInfo->notifiers);
            XMemory_free(sockInfo);
        }
        XHashMap_iterator_add(d->sockets, &it_sockets);
    }

    // 清理本地过滤器
    XAbstractEventDispatcherPrivate_deinit(d);

    if (d->internalHwnd) {
        DestroyWindow(d->internalHwnd);
        d->internalHwnd = NULL;
    }

    // 销毁容器
    XHashMap_delete_base(d->timers);
    XHashMap_delete_base(d->sockets);
    //XMutex_delete(d->m_dp.mutex);
    XMemory_free(d);

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
        XVTABLE_INHERIT_DEFAULT(XObject_class_init());
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
    SET_CLASS_HEAP(self);
    XClassGetVtable(self) = XEventDispatcherWin32_class_init();

    XEventDispatcherWin32PlatformPrivate* d = (XEventDispatcherWin32PlatformPrivate*)XMemory_calloc(1, sizeof(XEventDispatcherWin32PlatformPrivate));
    if (!d) {
        XMemory_free(self);
        return NULL;
    }
    XAbstractEventDispatcherPrivate_init(d);

    // --- 修复点 11: 正确初始化 XHashMap ---
    d->timers = XHashMap_create(sizeof(size_t), sizeof(XEventDispatcherWin32_TimerInfo), XHashMap_murmur3_32, size_t_compare);
    XContainerSetDataDeinitMethod(d->timers, timersDataDeinit);
    d->sockets = XHashMap_create(sizeof(intptr_t), sizeof(XEventDispatcherWin32_SocketInfo*), XHashMap_murmur3_32, int_compareptr_t);

    if (!d->timers || !d->sockets ) {
        // 错误处理
        //if (d->mutex) XMutex_delete(d->mutex);
        if (d->timers) XHashMap_delete_base(d->timers);
        if (d->sockets) XHashMap_delete_base(d->sockets);
        XMemory_free(d);
        XMemory_free(self);
        return NULL;
    }

    // 创建内部消息窗口
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = XEventDispatcherWin32_WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "XEventDispatcherInternalWindow";
    if (!RegisterClass(&wc)) {
        // 可能已经注册，忽略错误
    }
    d->internalHwnd = 0;
    d->internalHwnd = CreateWindowEx(
        0, "XEventDispatcherInternalWindow", NULL,
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, GetModuleHandle(NULL), NULL
    );

    if (!d->internalHwnd) {
        XHashMap_delete_base(d->timers);
        XHashMap_delete_base(d->sockets);
        XAbstractEventDispatcherPrivate_deinit(d);
        XMemory_free(d);
        XMemory_free(self);
        return NULL;
    }

    // 将 dispatcher 指针存入窗口
    SetWindowLongPtr(d->internalHwnd, GWLP_USERDATA, (LONG_PTR)self);

    if (XThread_currentThread()) {
        // 子线程需初始化 COM 和 OLE
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    }

    self->m_class.d_ptr = d;
    return self;
}
#endif