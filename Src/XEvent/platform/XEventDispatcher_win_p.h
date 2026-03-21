// XEventDispatcher_win_p.h
#ifndef XEVENTDISPATCHER_WIN_P_H
#define XEVENTDISPATCHER_WIN_P_H

#include "XEventDispatcher.h"
#include "XAbstractEventDispatcher.h"
#include "XHashMap.h"
#include "XMutex.h"
#include <windows.h>

// 前向声明
struct XSocketNotifier;
struct XAbstractNativeEventFilter;

/**
 * @brief 定时器信息结构体 (Windows 私有)
 */
typedef struct {
    XTimerId timerId;           ///< 定时器 ID
    UINT_PTR winTimerId;        ///< Windows 定时器 ID (来自 SetTimer)
    int64_t interval;           ///< 间隔 (纳秒)
    XTimerType timerType;       ///< 定时器类型
    XObject* object;            ///< 关联的对象
} XEventDispatcherWin32_TimerInfo;

/**
 * @brief 套接字信息结构体 (Windows 私有)
 *
 * 使用 WSAAsyncSelect 模型，每个套接字关联一个窗口句柄。
 */
typedef struct {
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
    XHashMap* timers;           ///< 定时器映射: winTimerId -> XEventDispatcherWin32_TimerInfo*
    XHashMap* sockets;          ///< 套接字映射: socket.value -> XEventDispatcherWin32_SocketInfo*
    bool interrupt;             ///< 中断标志，用于 interrupt()
    bool wakeUpSent;            ///< 标记是否已发送 WM_USER 消息用于 wakeUp()
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