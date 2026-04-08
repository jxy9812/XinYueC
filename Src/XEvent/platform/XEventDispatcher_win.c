// XEventDispatcher_win.c
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "XEventDispatcher_win_p.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XPair.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include <string.h>
#include <stdint.h>

// 全局原子计数器，用于生成唯一的定时器ID
//static XAtomic_uint32_t g_nextTimerId = { .value = 1 };

// Windows 消息常量
#define XDISPATCHER_WM_SOCKET (WM_USER + 1)
#define XDISPATCHER_WM_WAKEUP (WM_USER + 2)

#define PlatformPrivate(Dispatcher)  ((XEventDispatcherWin32PlatformPrivate*)((XAbstractEventDispatcher*)Dispatcher)->d_ptr)
#define GetXMutex(Dispatcher)         PlatformPrivate(Dispatcher)->m_dp.mutex

// ========================
// XHashMap 键比较和哈希函数 (用于 timers 和 sockets)
// ========================

// 用于 sockets HashMap: 键是 socket value (intptr_t)
static int XCompare_intptr_t(const void* a, const void* b) {
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
    XEventDispatcherWin32* dispatcher = (XEventDispatcherWin32*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (dispatcher) {
        intptr_t result = 0;
        if (XAbstractEventDispatcher_filterNativeEvent(dispatcher, "windows_generic_MSG", &msg, &result)) {
            return (LRESULT)result;
        }
    }

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
            //XEvent* quitEvent = XEventQuit_create();
            //XCoreApplication_postEvent(quitEvent, XEVENT_PRIORITY_HIGH);
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

static void VXEventDispatcherWin32_registerTimer(XAbstractEventDispatcher* dispatcher, XTimerId timerId, int64_t intervalNs, XTimerType timerType, XObject* object)
{
    XEventDispatcherWin32* self = (XEventDispatcherWin32*)dispatcher;
    XEventDispatcherWin32PlatformPrivate* d = PlatformPrivate(dispatcher);

    // 将纳秒转换为毫秒，Windows 定时器精度有限
    int64_t intervalMs = (intervalNs + 999999) / 1000000;
    if (intervalMs <= 0) intervalMs = 1;
    UINT uElapse = (UINT)(intervalMs > UINT_MAX ? UINT_MAX : intervalMs);

    XMutex_lock(GetXMutex(dispatcher));
    XEventDispatcherWin32_TimerInfo timerInfo = {0};
    timerInfo.timerId = timerId;
    timerInfo.interval = intervalNs;
    timerInfo.timerType = timerType;
    timerInfo.object = object;
    timerInfo.winTimerId = SetTimer(d->internalHwnd, timerId, uElapse, NULL);
    if (timerInfo.winTimerId != 0) {
        // 插入到 timers HashMap
        size_t win_timer_id = (size_t)timerInfo.winTimerId;
        XMapBase_insert_base(d->timers, &timerId, &timerInfo);
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
    KillTimer(d->internalHwnd, timerInfo->winTimerId);
    //将id放回列表
    XVector_push_back_base(d->m_dp.m_timerIds,&timerId);
    XHashMap_remove_base(d->timers, &timerId);
    //XHashMap_iterator it = XHashMap_begin(d->timers);
    //while (!XHashMap_iterator_isEnd(&it)) {
    //    XPair* pair = XHashMap_iterator_data(&it);
    //    XEventDispatcherWin32_TimerInfo* timerInfo = (XEventDispatcherWin32_TimerInfo*)XPair_second(pair);
    //    if (timerInfo && timerInfo->timerId == timerId) {
    //        KillTimer(d->internalHwnd, timerInfo->winTimerId);
    //        XMapBase_erase_base(d->timers, &it,NULL); // 安全删除
    //        //XMemory_free(timerInfo);
    //        found = true;
    //        break;
    //    }
    //    XHashMap_iterator_add(d->timers, &it);
    //}

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
        if (timerInfo && timerInfo->object == object) {
            KillTimer(d->internalHwnd, timerInfo->winTimerId);
            //将id放回列表
            XVector_push_back_base(d->m_dp.m_timerIds, &timerInfo->timerId);
            XHashMap_erase_base(d->timers, &it, &it);
            //XMemory_free(timerInfo);
            found = true;
        }
        else {
            XHashMap_iterator_add(d->timers, &it);
        }
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
            KillTimer(d->internalHwnd, (UINT_PTR) * (size_t*)XPair_first(pair));
            //XMemory_free(timerInfo);
        }
        // 注意：不能在这里 erase，因为 deinit 不需要保留容器结构
        // 我们只是释放数据，容器本身会在 delete_base 中销毁
        XHashMap_iterator_add(d->timers, &it_timers);
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
    //XVector_delete_base(d->m_dp.);
    //XMutex_unlock(d->m_dp.mutex);
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

// ========================
// 工厂函数
// ========================

XAbstractEventDispatcher* XEventDispatcherWin32_create(XObject* parent)
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
    d->timers = XHashMap_create(sizeof(size_t), sizeof(XEventDispatcherWin32_TimerInfo), XHashMap_murmur3_32, XCompare_size_t);
    d->sockets = XHashMap_create(sizeof(intptr_t), sizeof(XEventDispatcherWin32_SocketInfo*), XHashMap_murmur3_32, XCompare_intptr_t);

    if (!d->timers || !d->sockets ) {
        // 错误处理
        //if (d->mutex) XMutex_delete(d->mutex);
        if (d->timers) XHashMap_delete_base(d->timers);
        if (d->sockets) XHashMap_delete_base(d->sockets);
        //if (d->nativeFilters) XVector_delete_base(d->nativeFilters);
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

    self->m_class.d_ptr = d;
    return self;
}
#endif