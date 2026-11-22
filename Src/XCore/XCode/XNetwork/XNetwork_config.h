/**
 * @file XNetwork_config.h
 * @brief XNetwork 模块网络后端配置文件
 *
 * 通过此配置文件可以选择网络的底层实现：
 *   1. XNETWORK_USE_PLATFORM_API - 平台API模式（Windows IOCP / Linux epoll）
 *   2. XNETWORK_USE_LWIP       - lwIP 协议栈模式（嵌入式设备）
 *
 * 优先级（从高到低）：
 *   XNETWORK_USE_PLATFORM_API > XNETWORK_USE_LWIP
 */

#ifndef XNETWORK_CONFIG_H
#define XNETWORK_CONFIG_H

/* 引入平台自动检测宏（XPLATFORM_* 系列），
 * 保证 lwIP 源码编译时 XPLATFORM_LWIP_NO_SYS_DEFAULT 可用 */
#include "CXinYueConfig.h"

#ifdef __cplusplus
extern "C" {
#endif
/* ========================================================================== */
/*                        模块总开关                                        */
/* ========================================================================== */
/** @brief XNetwork 模块总开关；置 0 时裁剪整个 XNetwork 对外公开 API 及所有子功能。
 * @note 【2026-10-31 重复宏合并】取值唯一权威 = CXinYueConfig.h 的设备派生
 *       定义（XDEVICE_ON && XDEVICE_NETWORK_ON；本文件 :18 自举包含后它
 *       总是先行生效，原字面量 1 兜底已在所有路径下死亡且语义误导——
 *       无网络设备时仍放行网络编译——已删除）。
 *       关闭后若仍有其它模块无条件引用 XNetwork 符号，需同步裁剪对应依赖。 */
#if XNETWORK_ON

/** @brief 抽象套接字模块开关；关闭后 XAbstractSocket / XTcpSocket 相关 API 不编译。 */
#ifndef XNETWORK_ABSTRACT_SOCKET_ON
#define XNETWORK_ABSTRACT_SOCKET_ON 1
#endif

/** @brief TCP 服务器模块开关；关闭后 XTcpServer 相关 API 不编译。 */
#ifndef XNETWORK_TCPSERVER_ON
#define XNETWORK_TCPSERVER_ON 1
#endif

/** @brief UDP 套接字模块开关；关闭后 XUdpSocket 相关 API 不编译。 */
#ifndef XNETWORK_UDPSOCKET_ON
#define XNETWORK_UDPSOCKET_ON 1
#endif

/** @brief 数据报模块开关；关闭后 XNetworkDatagram 相关 API 不编译。 */
#ifndef XNETWORK_DATAGRAM_ON
#define XNETWORK_DATAGRAM_ON 1
#endif

/** @brief 主机信息模块开关；关闭后 XHostInfo 相关 DNS API 不编译。 */
#ifndef XNETWORK_HOSTINFO_ON
#define XNETWORK_HOSTINFO_ON 1
#endif

/** @brief 网络接口模块开关；关闭后 XNetworkInterface 相关 API 不编译。 */
#ifndef XNETWORK_INTERFACE_ON
#define XNETWORK_INTERFACE_ON 1
#endif

/** @brief 代理模块开关；关闭后 XNetworkProxy SOCKS5/HTTP 相关 API 不编译。 */
#ifndef XNETWORK_PROXY_ON
#define XNETWORK_PROXY_ON 1
#endif

/** @brief SSL 模块开关；关闭后 XSslSocket TLS/SSL 相关 API 不编译。 */
#ifndef XNETWORK_SSL_ON
#define XNETWORK_SSL_ON 1
#endif

/* ========================================================================== */
/*                          模式选择                                           */
/* ========================================================================== */

/**
 * @brief 网络后端模式选择
 *
 * 两种模式只能启用一种，优先级：PLATFORM_API > LWIP
 *
 * XNETWORK_USE_PLATFORM_API - 平台API模式
 *                             优点：完整支持所有API，性能最优
 *                             缺点：依赖操作系统
 *                             适用：Windows/Linux/macOS桌面应用
 *
 * XNETWORK_USE_LWIP         - lwIP协议栈模式
 *                             优点：跨平台，无操作系统依赖，适合嵌入式
 *                             缺点：受lwIP功能限制
 *                             适用：STM32等嵌入式设备
 */

/* 未由构建系统或产品配置指定时，桌面构建默认使用平台 API。
 * 保留外部覆盖能力，避免嵌入式构建同时定义两个互斥后端。 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)
#define XNETWORK_USE_PLATFORM_API
#endif

/* ========================================================================== */
/*                        lwIP模式配置                                         */
/* ========================================================================== */

/* lwIP is compiled as a static library for every backend. Keep these
 * compile-time defaults available even when the selected runtime backend is
 * the platform API; only lwIP consumes them. */
#ifndef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192
#endif

#ifndef XNETWORK_LWIP_SEND_BUFFER_SIZE
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  8192
#endif

#if defined(XNETWORK_USE_LWIP)

/**
 * @brief lwIP 最大套接字数
 */
#ifndef XNETWORK_LWIP_MAX_SOCKETS
#define XNETWORK_LWIP_MAX_SOCKETS     8
#endif

/**
 * @brief lwIP 每个活跃接收 socket 的缓冲预算（字节）
 *
 * 同时作为 TCP 接收窗口。缓冲首次收到数据时才分配，空闲 socket 不占用
 * 该预算。范围为 TCP_MSS（当前 1460）到 65535；小内存建议 1460/2920，
 * 大内存或高吞吐场景建议 16384/32768。
 */
#ifndef XNETWORK_LWIP_RECV_BUFFER_SIZE
#define XNETWORK_LWIP_RECV_BUFFER_SIZE  8192
#endif

/**
 * @brief lwIP 每个 TCP socket 的发送缓冲预算（字节）
 *
 * 范围为 2 * TCP_MSS（当前 2920）到 65535。值越大，允许同时等待确认的
 * 数据越多，吞吐更高，但动态内存峰值也越大。XFtp 会按该预算调整上传
 * 分块；小预算减少栈和 TLS 临时队列，大预算扩大批量以减少事件开销。
 */
#ifndef XNETWORK_LWIP_SEND_BUFFER_SIZE
#define XNETWORK_LWIP_SEND_BUFFER_SIZE  8192
#endif

/**
 * @brief lwIP TCP 服务器最大监听数
 */
#ifndef XNETWORK_LWIP_MAX_LISTEN_BACKLOG
#define XNETWORK_LWIP_MAX_LISTEN_BACKLOG  4
#endif

/**
 * @brief lwIP DNS 查询超时时间（毫秒）
 */
#ifndef XNETWORK_LWIP_DNS_TIMEOUT_MS
#define XNETWORK_LWIP_DNS_TIMEOUT_MS  5000
#endif

#ifndef XNETWORK_LWIP_TICK_MS
#define XNETWORK_LWIP_TICK_MS  1
#endif
/**
 * @brief lwIP 操作系统模式选择
 *
 * 两种模式：
 *   0 = OS 模式（NO_SYS=0）：使用 XMutex/XSemaphore/XThread 完整 sys_arch，
 *        lwIP 内部可创建线程/信号量/邮箱，支持 LWIP_TCPIP_CORE_LOCKING。
 *        适用：Windows/Linux 桌面、FreeRTOS 等带 OS 的环境。
 *
 *   1 = 裸机模式（NO_SYS=1）：最小 sys_arch，仅保留 sys_arch_protect/sys_now
 *        等必要接口，lwIP 内部不创建线程/信号量/邮箱，所有处理由主循环轮询。
 *        适用：STM32 裸机、无 OS 的嵌入式环境。
 *
 * @note 两种模式都使用 sys_arch_protect（递归互斥锁）保证线程安全，
 *       都通过 XTimeWheelGroup 定时器轮询 sys_check_timeouts()。
 */
/**
 * @brief lwIP IPv6 支持
 *
 * 0 = 关闭 IPv6（默认），仅 IPv4
 * 1 = 开启 IPv6 双栈支持
 */
#ifndef XNETWORK_LWIP_IPV6
#define XNETWORK_LWIP_IPV6  1
#endif

/**
 * @brief lwIP IGMP 多播支持
 *
 * 0 = 关闭 IGMP（默认）
 * 1 = 开启 IGMP 多播组管理
 */
#ifndef XNETWORK_LWIP_IGMP
#define XNETWORK_LWIP_IGMP  1
#endif
#ifndef XNETWORK_LWIP_NO_SYS
/* 对接 CXinYueConfig.h 的平台自动检测：
 *   裸机环境 -> NO_SYS=1（最小 sys_arch，无 tcpip_thread）
 *   有 OS 环境 -> NO_SYS=0（完整 sys_arch，lwIP 内部创建 tcpip_thread）
 * 用户可在此处上方显式 #define XNETWORK_LWIP_NO_SYS 覆盖默认值 */
#define XNETWORK_LWIP_NO_SYS  XPLATFORM_LWIP_NO_SYS_DEFAULT
#endif

#endif /* XNETWORK_USE_LWIP */

/* ========================================================================== */
/*                       平台API模式配置                                       */
/* ========================================================================== */

#if defined(XNETWORK_USE_PLATFORM_API)

/**
 * @brief 读取缓冲区大小
 */
#ifndef XNETWORK_PLATFORM_READ_BUFFER_SIZE
#define XNETWORK_PLATFORM_READ_BUFFER_SIZE   8192
#endif

/**
 * @brief 写入缓冲区大小
 */
#ifndef XNETWORK_PLATFORM_WRITE_BUFFER_SIZE
#define XNETWORK_PLATFORM_WRITE_BUFFER_SIZE  8192
#endif

#endif /* XNETWORK_USE_PLATFORM_API */

/* ========================================================================== */
/*                  Windows 套接字后端选择（2026-11-04）                       */
/* ========================================================================== */

/**
 * @brief Windows 套接字 I/O 后端选择。
 * @details 两套后端**同时保留**在 `Drive/windows/Device/Network/` 下，由本宏
 *          在编译期择一，互不覆盖：
 *
 *          `XNETWORK_WIN_BACKEND_IOCP`（1，对照后端）
 *            - 即 `XDeviceNetwork_win32.c` 的 IOCP 重叠 I/O 实现。
 *            - 收发全走 `WSARecv/WSASend + OVERLAPPED`，完成包经
 *              `GetQueuedCompletionStatus` 出队。
 *            - [2026-11-06 根修] 历史上"本机环回读完成丢失/挂死"已定性为
 *              本实现的三处缺陷（在飞 OVERLAPPED 复用、完成端口唤醒包饥饿、
 *              完成包归属按句柄数值判定），全部修掉并有跨进程联调 A/B 证据
 *              （机制级复现器见 `verify/iocp_overlapped_alias_repro.c`）。
 *
 *          `XNETWORK_WIN_BACKEND_WSAEVENT`（2，**当前缺省**）
 *            - 即 `XDeviceNetwork_win32_select.c`，口径对齐 Qt 6.8
 *              `qnativesocketengine_win.cpp`：**不做 overlapped I/O、不依赖
 *              完成包投递**。
 *            - 套接字恒非阻塞，收发是一次同步 `recv/send`；就绪检测由
 *              `WSAEventSelect` 的事件对象承担：**关联一次**（arm-once），
 *              此后每轮只做 `WSAWaitForMultipleEvents(…,0,…)` + `WSAEnumNetworkEvents`
 *              取回事件位（候选3，见后端文件头注：零超时 `select()` 在病态句柄
 *              上会永久悬挂、每轮重发 `WSAEventSelect` 也会挂在 AFD 端点锁）。
 *            - [2026-11-21 订正] 事件对象**并没有**并入框架事件环的等待集：
 *              环里只存「属主上下文指针」数组（`XNetIoRingWin32.c` 的
 *              `m_wsaSlots`），每轮由 `pollWsaEvents` 逐个回调属主做一次
 *              `Wait(0)+Enum`（全仓唯一调用 `WSAWaitForMultipleEvents` 的地方
 *              是后端的 `xw32_selectOne`）。代价：只要有一个套接字登记，
 *              `waitForEvents` 必然在第一个 10ms 片后返回 ⇒ 主循环节拍被钉在
 *              ~100Hz。
 *            - 不依赖完成包投递，因此完全绕开完成端口相关故障面；
 *              保留 IOCP 后端作对照与一键回退（切换后同样必须全量重编）。
 *
 * @note 只影响 Windows 的套接字路径；USB / 串口 / 文件设备的 IOCP 通道在
 *       两种取值下都保持不变（它们与网络无关，且无此缺陷）。
 * @warning 切换后必须全量重编——改的是公共头里的条件编译宏，增量编译会
 *          留下按旧口径编出的 obj（见 Windows/Ninja 缺头文件依赖）。
 *          本仓提供 `verify\rebuild_full.bat`（删全部 .obj 后全量重编）。
 */
#define XNETWORK_WIN_BACKEND_IOCP       1  /**< IOCP 重叠 I/O 后端（**当前缺省**：高性能可选，err=64 分类已正）。 */
#define XNETWORK_WIN_BACKEND_WSAEVENT   2  /**< 【已移除实现，2026-11-21 晚】该值不再合法（选它编译期报错）。WSAEventSelect 曾为 2026-11-21 前缺省，WSAAsyncSelect 落地并跨进程验证后按拍板移除（git 历史 be76f1c8 可查其全部实现）。 */
#define XNETWORK_WIN_BACKEND_WSAASYNC   3  /**< WSAAsyncSelect 后端（Qt 6.8 同款：隐藏窗口 + WM_APP 消息投递）；曾短暂为缺省，被 IOCP 取代后保留为回退；与已移除的 WSAEVENT 共用同一套 socket 语义实现，仅"就绪获取层"不同。 */

#ifndef XNETWORK_WIN_BACKEND
/* [2026-11-21 最终裁定] 后端 = **IOCP**（用户拍板：err=64 分类已根治 +
 * 跨进程联调验证健康后，IOCP 数十年成熟、高性能可选做缺省）。
 *
 * 演进史：11-17 裁定 WSAEventSelect（当时 IOCP 会「杀掉健康连接」，见下
 * 的历史定性）→ 11-21 WSAAsyncSelect 落地（Qt 6.8 同款通知机制，ring 级
 * 窗口事件源）→ 同日晚 err=64 定性为**错误分类残留**（不是 IOCP 不可用）：
 * 未决重叠读在对端关闭时以 Win32 码 64 完成，分类落 Unknown ⇒ 兜底
 * NetworkError ⇒ 上层对端关闭守卫不命中。分类修复（RemoteHostClosed）+
 * 跨进程联调（python 连/断后 64 正确判死、服务端继续接受新连接）通过后
 * 切 IOCP 缺省。WSAEVENT 实现同日移除（WSAASYNC 复用其语义段）。
 *
 * 为什么不是 IOCP（远端 43da00a6 与本地提交 f77663ee 的缺省）：IOCP 在本
 * 机会**杀掉健康连接**——服务端日志实证：
 *   [XNetwork-W32] 读完成 致命错误 err=64，判定连接已死，投递关闭事件
 *   [xlsess] 传输断开，会话复位（原 state=2，积压=0B）
 *   HsfServerApp: 会话断开（当前 0 路）
 * ⇒ 跨端联调里表现为「客户端登录成功 ↔ 断开循环、界面卡片在真值/-- 之间
 * 闪烁」。本次合并复验时再次复现（Python 客户端连上隔离 C 服务端后，
 * 紧接着的历史查询直接报「未连接」）。WSAEventSelect 后端不做 overlapped
 * I/O、不依赖完成包投递，完全绕开该故障面。
 * 板端 Linux（epoll/io_uring）不受影响。
 *
 * [遗留项·已根治 2026-11-18，2026-11-21 复验] 旧记录说「select 后端的
 * 对端断开检测比 IOCP 慢：实测对端断开后 1510ms 仍未察觉，要等 6s 静默
 * 超时才断链」，并列出四套 socket 断言红（xlink-client 81/11、
 * xsocket-stall 6/9、xmqtt-tcp-api 46/3、xconsole-shell-backend 3 失败）。
 * **该记录已过期**：根因是 `xw32_selectOne` 把「异常」从「可读」里扣减，
 * 而 `FD_CLOSE` 同时计入可读掩码与异常掩码 ⇒ 「对端关闭」算出 ret=0 ⇒
 * 派发处 `n > 0` 的分支全部落空 ⇒ `FD_CLOSE` 被静默吞掉。2026-11-18 已根修
 * （三类事件各自计一次，见后端 `xw32_selectOne` 的根修注）。
 * 2026-11-21 复验：四套门禁**全绿**（xlink-client 92/0、xsocket-stall 23/0、
 * xmqtt-tcp-api 49/0、xconsole-shell-backend 通过），门禁脚本见
 * `verify\run_socket_gates.bat`（7 套）。
 *
 * [2026-11-21 本轮成果摘要] WSAEventSelect 后端按 Qt 6.8 口径做了全面整改：
 * ① 写侧致命错误不再被吞成「已排队」（原会导致写通道静默永久卡死）；
 * ② 直取路径不再吞 EOF；③ 一次就绪读到 would-block 为止（Qt `readFromSocket`
 * 口径，EOF 与末批数据同轮可见）；④ 槽表注册失败必须显式（不再静默失联）；
 * ⑤ 事件位一次取回并缓存（不再「取走但丢弃」）；⑥ 关闭成为独立事件；
 * ⑦ 命名管道通道从死代码扶正为可用；⑧ 新增 WSA→`XAbstractSocket_SocketError`
 * 的错误码映射（Qt `setErrorFromWSAError` 口径），对端关闭报
 * `RemoteHostClosedError`。详见
 * `docs/Windows_WSAEventSelect后端Qt对齐与覆盖扩充_20261121.md`。
 *
 * 切换注意：这是公共头条件编译宏，**改完必须全量重编**（增量编译会留
 * 旧口径 obj）——用 `verify\rebuild_full.bat`。若要对照，把下面这行改成
 * `XNETWORK_WIN_BACKEND_IOCP` 并全量重编即可（2026-11-21 实测：socket 层
 * 23 条新用例在两套后端下**都全绿** ⇒ 新用例锁的是跨后端契约）。 */
#define XNETWORK_WIN_BACKEND  XNETWORK_WIN_BACKEND_IOCP
#endif

#if (XNETWORK_WIN_BACKEND != XNETWORK_WIN_BACKEND_IOCP) && \
    (XNETWORK_WIN_BACKEND != XNETWORK_WIN_BACKEND_WSAASYNC) && \
    (XNETWORK_WIN_BACKEND == XNETWORK_WIN_BACKEND_WSAEVENT)
#error "XNetwork: XNETWORK_WIN_BACKEND 只能是 IOCP / WSAASYNC（WSAEVENT 实现已移除，2026-11-21 晚；见 XNETWORK_WIN_BACKEND_WSAEVENT 注）"
#endif
/* WSAEVENT(2) 保留宏定义但**不再合法**：上面的 #error 会在选它时报错。
 * （写成"追加 WSAEVENT==自身即报错"的条件，而非简单从合法集合剔除，
 *  为的是让错误信息点名 WSAEVENT 的移除原因。） */

/** @brief WSAEventSelect 后端**实现已移除**（2026-11-21 晚）⇒ 恒 0。
 *         宏名保留：防止散落的历史引用在 `#if` 里静默取 0 走进错误分支
 *         ——显式恒 0 + 全仓无 1 分支（grep 可查），行为可预期。 */
#define XNETWORK_WIN_SOCKET_WSAEVENT 0

/** @brief 当前构建是否使用 IOCP 套接字后端。 */
#if XNETWORK_WIN_BACKEND == XNETWORK_WIN_BACKEND_IOCP
#define XNETWORK_WIN_SOCKET_IOCP 1
#else
#define XNETWORK_WIN_SOCKET_IOCP 0
#endif

/** @brief 当前构建是否使用 WSAAsyncSelect 套接字后端（Qt 6.8 同款）。
 * @details 该后端与 WSAEVENT 共用 `XDeviceNetwork_win32_select.c` 的 socket
 *          语义实现，仅"就绪获取层"（probeArm/pollEvents）不同：
 *          - WSAEVENT：`WSAEventSelect` + `Wait(0)+Enum`；
 *          - WSAASYNC：`WSAAsyncSelect` + 隐藏窗口 WM_APP 消息 → 位缓存。
 *          故两者用同一个派生宏门控 socket 段落：`XNETWORK_WIN_SOCKET_SELECT`。
 *          本宏（WSAASYNC=1）只在就绪获取层的内部 `#if` 里区分。 */
#if XNETWORK_WIN_BACKEND == XNETWORK_WIN_BACKEND_WSAASYNC
#define XNETWORK_WIN_SOCKET_WSAASYNC 1
#else
#define XNETWORK_WIN_SOCKET_WSAASYNC 0
#endif

/** @brief 是否使用"同步非阻塞 select 模型"的 socket 后端（WSAEVENT 或 WSAASYNC）。 */
#if (XNETWORK_WIN_SOCKET_WSAEVENT || XNETWORK_WIN_SOCKET_WSAASYNC)
#define XNETWORK_WIN_SOCKET_SELECT 1
#else
#define XNETWORK_WIN_SOCKET_SELECT 0
#endif

/* ========================================================================== */
/*                        自动模式检测                                         */
/* ========================================================================== */

/**
 * @brief 自动选择默认模式
 *
 * 如果用户未指定任何模式，根据平台自动选择：
 *   - Windows/Linux/macOS -> XNETWORK_USE_PLATFORM_API
 *   - 其他平台            -> XNETWORK_USE_LWIP
 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)

#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XNETWORK_USE_PLATFORM_API
#else
#define XNETWORK_USE_LWIP
#endif

#endif

/* ========================================================================== */
/*                        模式验证                                             */
/* ========================================================================== */

/* 检查是否同时启用了多个模式 */
#if defined(XNETWORK_USE_PLATFORM_API) && defined(XNETWORK_USE_LWIP)
#error "XNetwork: 不能同时启用多个网络后端，请只选择一种模式"
#endif

/* 检查是否未启用任何模式 */
#if !defined(XNETWORK_USE_PLATFORM_API) && !defined(XNETWORK_USE_LWIP)
#error "XNetwork: 必须启用至少一个网络后端模式"
#endif

/* ========================================================================== */
/*                        API可用性宏定义                                       */
/* ========================================================================== */

/**
 * @brief API功能可用性宏
 *
 * 用于编译时判断特定API是否可用，便于条件编译
 */

/* 平台初始化 - 两种模式均支持 */
#define XNETWORK_API_ENSURE_INIT       1
#define XNETWORK_API_CLEANUP           1
#define XNETWORK_API_LAST_ERROR        1
#define XNETWORK_API_IS_EAGAIN         1
#define XNETWORK_API_ERROR_STRING      1

/* 私有数据 - 两种模式均支持 */
#define XNETWORK_API_CREATE_PRIVATE    1
#define XNETWORK_API_DELETE_PRIVATE    1
#define XNETWORK_API_SOCKET_FD         1
#define XNETWORK_API_IS_CONNECTED      1

/* 核心操作 - 两种模式均支持 */
#define XNETWORK_API_BIND              1
#define XNETWORK_API_CONNECT           1
#define XNETWORK_API_DISCONNECT        1
#define XNETWORK_API_READ              1
#define XNETWORK_API_WRITE             1
#define XNETWORK_API_HANDLE_EVENT      1
#define XNETWORK_API_SET_DESCRIPTOR    1
#define XNETWORK_API_SET_OPTION        1
#define XNETWORK_API_GET_OPTION        1
#define XNETWORK_API_SET_READ_BUFFER   1

/* 异步读取状态 - 两种模式均支持 */
#define XNETWORK_API_READ_BUFFER       1
#define XNETWORK_API_READ_FINISHED     1
#define XNETWORK_API_WRITE_FINISHED    1
#define XNETWORK_API_CONTINUE_READ     1

/* TCP 服务器 - 两种模式均支持 */
#define XNETWORK_API_SERVER_CREATE     1
#define XNETWORK_API_SERVER_PORT       1
#define XNETWORK_API_SERVER_CLOSE      1
#define XNETWORK_API_GET_ACCEPTED      1
#define XNETWORK_API_CONTINUE_ACCEPT   1

/* DNS 查询 - 两种模式均支持 */
#define XNETWORK_API_LOOKUP_NAME       1
#define XNETWORK_API_LOCAL_HOSTNAME    1

/* ICMP Echo - 平台套接字后端和 lwIP Raw API 均提供 IPv4 能力。 */
#define XNETWORK_API_ICMP_ECHO         1

/* 网络接口枚举 - 仅平台API模式完整支持 */
#if defined(XNETWORK_USE_PLATFORM_API)
#define XNETWORK_API_ENUM_INTERFACES   1
#define XNETWORK_API_GET_IF_ADDRESSES  1
#elif defined(XNETWORK_USE_LWIP)
#define XNETWORK_API_ENUM_INTERFACES   1  /* lwIP: 通过 netif 枚举 */
#define XNETWORK_API_GET_IF_ADDRESSES  1
#endif

/* 多播 - 两种模式均支持 */
#define XNETWORK_API_MULTICAST_GROUP   1
#define XNETWORK_API_MULTICAST_OP      1

/* UDP 特有 - 两种模式均支持 */
#define XNETWORK_API_LAST_SENDER       1
#define XNETWORK_API_SEND_DATAGRAM     1

/* 系统代理与GSSAPI - 仅平台API模式支持 */
#if defined(XNETWORK_USE_PLATFORM_API)
#define XNETWORK_API_SYSTEM_PROXY      1
#define XNETWORK_API_GSSAPI_AUTH       1
#else
#define XNETWORK_API_SYSTEM_PROXY      0
#define XNETWORK_API_GSSAPI_AUTH       0
#endif

#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif

#endif /* XNETWORK_CONFIG_H */
