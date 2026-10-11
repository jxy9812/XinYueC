/**
 * @file       XAbstractEventDispatcher_Protected.h
 * @brief      XAbstractEventDispatcher 私有实现结构体与初始化接口（protected/内部接口）。
 * @details     仅供本类实现与内部模块（XThreadData、XDeviceTimer 等需解引用
 *             m_d_ptr 的文件）使用，不对外公开；普通用户 API 头文件链不得传递引入，
 *             使用者必须显式 include 本文件。
 */

#ifndef XABSTRACTEVENTDISPATCHER_PROTECTED_H
#define XABSTRACTEVENTDISPATCHER_PROTECTED_H

#include "XAbstractEventDispatcher.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief XAbstractEventDispatcher 私有数据（PIMPL，内部结构体）。
 * @note 完整定义仅对本保护头文件的使用者可见；公开头只保留不透明前向 typedef。
 */
typedef struct XAbstractEventDispatcherPrivate
{
    /* ---- 指针簇前置 ---- */
    XHrTimerGroup* m_hrtimerGroup;//高精度定时器组
    XHashMap* m_notifiers;         ///< XFd → XVector<XSocketNotifier*>，notifier 注册表
    XAbstractNetIoRing* m_ioRing;          ///< I/O 事件环（平台无关 I/O 抽象）
    XThreadData* m_threadData;    ///< 反向引用：本调度器所属线程的 XThreadData（工作线程信号量唤醒用）
    //XMutex* mutex;              ///< 保护原生事件过滤器的互斥锁
    void* m_consoleTransport;     ///< 默认 Shell 的标准输入传输（由调度器持有并释放）
    void* m_consoleShell;         ///< 默认 Shell 实例（由调度器持有，析构时释放）
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON && XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
    void* m_consoleSshServer;     ///< 默认 Shell 的 SSH TCP Server（由调度器持有）
    void* m_consoleSshAdapter;    ///< 默认 Shell 的 SSH 多会话适配器（由调度器持有）
#endif
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON && XCONSOLE_SHELL_XTELNETSERVER_BACKEND_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON
    void* m_consoleTelnetServer;  ///< 默认 Shell 的 Telnet TCP Server（由调度器持有）
    void* m_consoleTelnetAdapter; ///< 默认 Shell 的 Telnet 多会话适配器（由调度器持有）
#endif
    /* 周期轮询回调链表（USB Host/Gadget、串口等需在事件循环中定期处理的设备） */
    struct XAbstractEventDispatcher_PollEntry* m_pollCallbacks;
    /* ---- 标志移尾（对齐缝填充，各自 #if 守卫保持不变） ---- */
    XAtomic_Bool m_interrupt;      ///< 中断标志，阻止阻塞等待
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
    bool m_consolePromptShown;    ///< 默认 Shell 初始提示符是否已经输出
#endif
}XAbstractEventDispatcherPrivate;

/**
 * @brief 初始化事件调度器私有数据成员。
 * @param dp 待初始化的私有数据指针，不能为 NULL。
 */
void XAbstractEventDispatcherPrivate_init(XAbstractEventDispatcherPrivate* dp);

/**
 * @brief 反初始化事件调度器私有数据成员（释放其持有的资源）。
 * @param dp 待反初始化的私有数据指针，不能为 NULL。
 */
void XAbstractEventDispatcherPrivate_deinit(XAbstractEventDispatcherPrivate* dp);

#ifdef __cplusplus
}
#endif

#endif // XABSTRACTEVENTDISPATCHER_PROTECTED_H
