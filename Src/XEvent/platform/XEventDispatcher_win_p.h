// XEventDispatcher_win_p.h
#ifndef XEVENTDISPATCHER_WIN_P_H
#define XEVENTDISPATCHER_WIN_P_H

#include "XAbstractEventDispatcher.h"
#include "XHashMap.h"
#include "XListSLinked.h"
#include "XMutex.h"
#include <windows.h>

// 前向声明
struct XSocketNotifier;
struct XAbstractNativeEventFilter;
typedef struct XEventDispatcherWin32_SocketInfo XEventDispatcherWin32_SocketInfo;
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
XAbstractEventDispatcher* XEventDispatcherWin32_create(XObject* parent);
#endif // XEVENTDISPATCHER_WIN_P_H