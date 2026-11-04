/**
 * @file XDeviceNetwork_win32_select.c
 * @brief Windows 套接字后端：WSAEventSelect 就绪模型（口径对齐 Qt 6.8）。
 * @details 本文件与 `XDeviceNetwork_win32.c`（IOCP 重叠 I/O 后端）**并存**，
 *          由 `XNETWORK_WIN_BACKEND` 编译期二选一（见 XNetwork_config.h）：
 *
 *            - `XNETWORK_WIN_BACKEND_WSAEVENT`（本文件）——默认；
 *            - `XNETWORK_WIN_BACKEND_IOCP`——历史实现，保留可回退。
 *
 *          ## 为什么需要这一套
 *
 *          本机环回链路上 `WSARecv` 的 IOCP 完成通知被实测证实不可靠：
 *          两端同时停止接收、之后各自继续发送 5~10 秒、最终静默判线。
 *          `GetOverlappedResult(FALSE)` + `ioctlsocket(FIONREAD)` 三态判定
 *          给出的结论是「挂起的读请求本身被平台挂死」，证据与排除项见
 *          `docs/Windows_IOCP读完成丢失_交接_20261104.md`。
 *
 *          Qt 6.8 `qnativesocketengine_win.cpp` 完全不用 overlapped I/O：
 *          `WSARecv/WSASend` 的 OVERLAPPED 参数一律传 `nullptr`，就绪检测
 *          由窗口消息驱动。**它不依赖完成包投递，所以这个坑对它不存在。**
 *          本后端照此实现，只把就绪通知从「窗口消息」换成
 *          `WSAEventSelect` 的事件对象——因为框架主循环已有一个自有事件环
 *          （`XAbstractNetIoRing`），把事件对象并进它的等待集，比新开窗口
 *          更贴合本框架，也保住「不新增线程」这条既有约束。
 *
 *          ## 与框架异步读契约的对接
 *
 *          框架的套接字读是**单缓冲 + 续发**模型（与 IOCP 后端逐项一致）：
 *
 *          1. `readPending == false` 时后端才可 `recv` 进 `readBuffer`；
 *          2. 取到数据后投递 `Read` 完成，`readPending = true`（缓冲被占用）；
 *          3. 上层把 `readBuffer` 拷进自己的 ring buffer 后发
 *             `ContinueRead`，后端清 `readPending` 并再次 `recv`。
 *
 *          这条链路保证「上一批数据未消费前不会被下一批覆盖」。本后端完全
 *          复用该契约，只是把「提交 WSARecv」换成「就绪事件到了再同步
 *          recv」——**也就没有在途请求，从而没有完成包丢失的可能**。
 *
 *          ## 就绪事件的唤醒路径
 *
 *          `WSAEventSelect` 的事件对象由 `XNetIoRingWin32` 并入其等待集
 *          （与 IOCP 端口、GUI 消息一起构成单一等待点）。事件环只负责
 *          「发现就绪」，实际收发与状态迁移仍由本文件完成：
 *          `XDeviceNetwork_win32_selectDispatchReady` 是事件环回调进来的唯一
 *          入口，它内部 `WSAEnumNetworkEvents` 原子取走并复位事件——这是
 *          官方指定的唯一正确复位方式。
 *
 * @note 本文件不使用任何 OVERLAPPED / AcceptEx / ConnectEx。命名管道
 *       （`socketConnectLocal`）在 Windows 上本来就不是套接字语义，这里用
 *       同名 `CreateFile` + 同步读写实现，与 IOCP 后端的 pipe 分支等价。
 */

#include "XNetwork_config.h"

#if defined(XNETWORK_USE_PLATFORM_API) && defined(_WIN32) && \
    XNETWORK_WIN_SOCKET_WSAEVENT

/* ====== Windows 宏定义必须在所有头文件之前 ====== */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* ====== 项目头文件 ====== */
#include "XDeviceNetwork.h"
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include "XEvent.h"
#include "XHostAddress.h"
#include "XAbstractSocket.h"
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingWin32.h"
#include "XDateTime.h"

/* ====== Windows SDK 头文件 ====== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/* =========================================================================
 * 内部定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192   /**< 单次 recv 的落地缓冲容量（与 IOCP 后端同值）。 */
#define XNETWORK_WRITE_BUFFER_SIZE 8192   /**< 单次 send 的分片上限（与 IOCP 后端同值）。 */

/** @brief 命名管道单次写分片：同步写要一次性拷入内核，避免大块栈缓冲。 */
#define XNETWORK_PIPE_WRITE_CHUNK  4096

/**
 * @brief 发送失败的三态返回（与业务层的「0 = 背压」口径区分开）。
 * @details 框架的 `XDeviceNetwork_socketWrite` 用 `< 0` 表示错误、`0` 表示
 *          本轮不可写、`> 0` 表示写出字节数。内部用独立常量表达「内核
 *          暂满」，避免与「真的出错了」混为一谈。
 */
#define XNETWORK_IO_WOULD_BLOCK  (-3)
#define XNETWORK_IO_ERROR        (-1)

/**
 * @brief 就绪探测超时（毫秒）：`select()` 在事件环 poll 里的等待上限。
 * @details Qt 的 `waitForReadOrWrite` 用 `deadline.remainingTime()` 作
 *          `timeval`；本框架的 poll 是**非阻塞**语义，故取 0（只做一次
 *          状态查询）。真正需要阻塞时由事件环的等待路径分片调用。
 */
#define XNETWORK_SELECT_POLL_TIMEOUT_MS  0

/**
 * @brief 多播操作码。
 * @details 与 `XDeviceNetwork_win32.c`、`Src/XCore/XDevice/XDeviceNetwork.c`
 *          中的同名枚举逐项一致（三处各自本地定义，值必须保持同步——
 *          `XDeviceNetwork_multicastOp` 的实现放在共享段，调用方按值传递）。
 */
typedef enum XMulticastOp {
    XMC_Join, XMC_Leave, XMC_SetIf, XMC_GetIf,
    XMC_SetTtl, XMC_GetTtl, XMC_SetLoop, XMC_GetLoop
} XMulticastOp;

/* 共享段（不受后端宏门控）里实现的多播入口。 */
int XDeviceNetwork_multicastOp(XDeviceNetworkSocketHandle socketHandle,
                               XMulticastOp operation, void* argument);

/* 本文件内后置实现的钩子（不属于公共头文件契约）。 */
void XDeviceNetwork_socketDisconnect(XFd fd);

/* =========================================================================
 * 事件环集成：把套接字交给框架的就绪轮询表
 *
 * 事件环侧的等待集由 XNetIoRingWin32 提供；本文件只做「登记/注销」的薄
 * 转发。**不新增线程、不新增等待集**：套接字就绪最终汇聚到框架主循环
 * 同一个等待点上（见 XNetIoRingWin32_addWsaEvent 的说明）。
 * ========================================================================= */

#if XAbstractNetIoRing_ON

/**
 * @brief 把套接字上下文登记进框架就绪轮询表。
 * @param event 保留参数（`select` 模型不持有事件对象），恒传 NULL。
 * @param fd    框架句柄（仅诊断用）。
 * @param owner 属主上下文指针（就绪时原样回传）。**不能用 fd 反查**：
 *              `XFd` 是池槽位号，会随连接关闭被下一个连接复用。
 */
static bool xw32_ringAddSockEvent(WSAEVENT event, XFd fd, void* owner)
{
    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    (void)event;
    if (!ring) return false;
    return XNetIoRingWin32_addWsaEvent((XNetIoRingWin32*)ring, fd, owner);
}

/** @brief 从框架就绪轮询表注销套接字上下文。 */
static void xw32_ringRemoveSockEvent(WSAEVENT event, void* owner)
{
    XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
    (void)event;
    if (!ring) return;
    XNetIoRingWin32_removeWsaEvent((XNetIoRingWin32*)ring, owner);
}

#else  /* !XAbstractNetIoRing_ON —— 无事件环（裁剪构建）：退化为无人驱动 */

static bool xw32_ringAddSockEvent(WSAEVENT event, XFd fd, void* owner)
{
    (void)event;
    (void)fd;
    (void)owner;
    return false;
}

static void xw32_ringRemoveSockEvent(WSAEVENT event, void* owner)
{
    (void)event;
    (void)owner;
}

#endif /* XAbstractNetIoRing_ON */

/* =========================================================================
 * 套接字私有数据
 * ========================================================================= */

/**
 * @brief 本后端的套接字上下文。
 * @details 首成员必须是 `XDeviceNetworkContext`：框架按平台无关基类解释该
 *          指针（`XDevice_handle` 返回的就是它），后端扩展只能追加在其后，
 *          与 POSIX 的 `XDeviceNetworkContextPosix` 同构。
 *
 *          与 IOCP 后端的结构性差异：这里**没有** OVERLAPPED、没有写槽池、
 *          没有在途计数——同步非阻塞收发不存在「在途操作生命周期」问题，
 *          因而不需要完成回调来做延迟释放。
 */
typedef struct XDeviceNetworkContextWin32Select {
    XDeviceNetworkContext base;      /**< 第一位：平台无关基类（owner/xfd/notifiers）。 */
    uint64_t magic;                  /**< 存活魔数（XW32_SELECT_MAGIC）；deinit 置 0。 */
    SOCKET   socket;                 /**< Windows SOCKET；无效为 INVALID_SOCKET。 */
    HANDLE   namedPipe;              /**< 命名管道句柄；无效为 INVALID_HANDLE_VALUE。 */

    bool readPending;                /**< 已投递读完成、上层尚未取走（诊断/防重复投递用）。 */
    bool writePending;               /**< 是否有未发完的写（仅用于属性查询）。 */
    bool connectPending;             /**< 异步 connect 在途（FD_CONNECT 尚未到达）。 */
    bool autoRead;                   /**< 连接建立后自动挂读（框架既有语义）。 */
    bool isServer;                   /**< 监听套接字。 */
    bool closed;                     /**< 已收到 FD_CLOSE（避免重复投递关闭事件）。 */

    uint32_t readReadyCount;         /**< 累计读完成投递次数（诊断用）。 */
    uint32_t writeReadyCount;        /**< 累计写就绪投递次数（诊断用）。 */

    size_t readFinishedBytes;        /**< 最近一次交付给上层的字节数。 */
    size_t writeFinishedBytes;       /**< 最近一次写出字节数。 */

    /* UDP 来源地址（recvfrom 回写）。 */
    struct sockaddr_in6 fromAddr;
    int fromAddrLen;

    /* 连接目标暂存（connect 前记录，供诊断与后续同步端点复用）。 */
    XHostAddress pendingPeerAddr;
    uint16_t pendingPeerPort;

    /* 缓冲区联合体：监听用 accept 地址，客户端用 readBuffer。 */
    union {
        struct {
            char readBuffer[XNETWORK_READ_BUFFER_SIZE]; /**< 最近一次 recv 的落地缓冲。 */
        };
        struct {
            struct sockaddr_storage acceptAddr;         /**< 最近一次 accept 的对端地址。 */
            int acceptAddrLen;                          /**< 该地址的有效长度。 */
        };
    };
} XDeviceNetworkContextWin32Select;

/** @brief 便捷转换宏（与 IOCP 后端的 W32 对应）。 */
#define W32S(p) ((XDeviceNetworkContextWin32Select*)(p))

/**
 * @brief 上下文存活魔数。
 * @details `XFd` 是池槽位号、上下文内存也由系统堆复用——连接关闭后
 *          新建的连接可能拿到**同一个地址**。事件环里若还留着指向该地址
 *          的旧槽（漏注销），就绪回调就会打到一个「地址相同但内容是别的
 *          连接」的上下文上，表现为串包。`deinit` 时把魔数清零，dispatch
 *          首行校验，可把这一类残留访问变成可观测的拒绝而非静默错乱。
 */
#define XW32_SELECT_MAGIC UINT64_C(0x584E5753454C4543)  /* "XNWSELEC" */

/** @brief 上下文是否仍处于存活状态（魔数匹配）。 */
static bool xw32_isLive(const XDeviceNetworkContextWin32Select* p)
{
    return p && p->magic == XW32_SELECT_MAGIC;
}

/* =========================================================================
 * 地址转换（与 IOCP 后端同源：sockaddr_storage <-> XHostAddress）
 * ========================================================================= */

static void addr2sa(const XHostAddress* addr, uint16_t port,
    struct sockaddr_storage* ss, int* ssLen)
{
    XMemset(ss, 0, sizeof(*ss));

    if (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)ss;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons(port);
        XHostAddress_toIPv6Address(addr, &s6->sin6_addr);
        *ssLen = (int)sizeof(struct sockaddr_in6);
    }
    else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)ss;
        s4->sin_family = AF_INET;
        s4->sin_port = htons(port);
        s4->sin_addr.s_addr = htonl(XHostAddress_toIPv4Address(addr));
        *ssLen = (int)sizeof(struct sockaddr_in);
    }
}

static void sa2addr(const struct sockaddr_storage* ss, XHostAddress* addr, uint16_t* port)
{
    if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6* s6 = (const struct sockaddr_in6*)ss;
        XHostAddress_setAddressIPv6(addr, (const uint8_t*)&s6->sin6_addr);
        if (port) *port = ntohs(s6->sin6_port);
    }
    else if (ss->ss_family == AF_INET) {
        const struct sockaddr_in* s4 = (const struct sockaddr_in*)ss;
        XHostAddress_setAddressIPv4(addr, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/* =========================================================================
 * 就绪事件 → 框架 CQ
 * ========================================================================= */

/**
 * @brief 把一次套接字完成投进框架完成队列。
 * @param p      本方上下文（提供 XFd 与真实 SOCKET）。
 * @param events `XSocketActType` 位掩码。
 * @param bytes  伴随字节数（读路径填实际读入量，其余填 0）。
 * @details **`m_osFd` 必须填真实 SOCKET**：框架在「0 字节读完成」时会拿它
 *          做 `MSG_PEEK` 复核，判定是真 FIN 还是假 EOF。IOCP 后端历史上
 *          漏填该字段（memset 恒 0）导致 PEEK 打到 stdin、健康链路被误断
 *          ——这里沿用同一字段的正确口径，不可省。
 */
static void xw32_postReady(XDeviceNetworkContextWin32Select* p,
                           uint32_t events, uint32_t bytes)
{
#if XAbstractNetIoRing_ON
    XAbstractNetIoRing* ring;
    XAbstractNetIoRing_CQEntry entry;

    if (!p) return;
    ring = XAbstractNetIoRing_global();
    if (!ring || !XAbstractNetIoRing_isEnabled(ring)) return;

    XMemset(&entry, 0, sizeof(entry));
    entry.m_fd = p->base.m_base.m_fd;
    entry.m_osFd = (int)(p->namedPipe != INVALID_HANDLE_VALUE
                             ? (intptr_t)p->namedPipe
                             : (intptr_t)p->socket);
    entry.m_bytes = bytes;
    entry.m_events = events;
    entry.m_sourceType = XAbstractNetIoRing_Source_NativeIO;
    entry.m_fdType = XFd_type(p->base.m_base.m_fd);
    entry.m_error = 0;


    if (XAbstractNetIoRing_pushCompletion(ring, &entry))
        XAbstractNetIoRing_wakeUp_base(ring);
#else
    (void)p;
    (void)events;
    (void)bytes;
#endif
}

/* =========================================================================
 * 就绪探测（`select()`，与 Qt `QNativeSocketEnginePrivate::nativeSelect` 同源）
 * ========================================================================= */

/**
 * @brief 用 `select()` 查询套接字当前的可读/可写/异常状态。
 * @param p          上下文。
 * @param wantRead   是否关心可读。
 * @param wantWrite  是否关心可写。
 * @param outRead    输出可读；可为 NULL。
 * @param outWrite   输出可写；可为 NULL。
 * @param outError   输出异常（`select` 的 exceptfds 命中）；可为 NULL。
 * @return 就绪项数（>=0）；`SOCKET_ERROR` 表示查询失败。
 *
 * @details **为什么用 `select` 而不是 `WSAEventSelect` 事件对象**
 *
 *          Qt 6.8 在 Windows 上的实际做法是两条腿：
 *          - 事件循环内：`WSAAsyncSelect` + 窗口消息（`qeventdispatcher_win.cpp`）；
 *          - 同步等待时：`select()`（`qns_win.cpp:1495 nativeSelect`）。
 *
 *          本框架主循环已有自有事件环、没有独立窗口消息泵，且任务要求
 *          「不新增线程/等待集」。起初把 `WSAEventSelect` 的事件对象并进
 *          事件环等待集，实测事件对象**从不置位**（`slot not signaled`
 *          持续出现，而内核里确有数据）——该组合在本进程的套接字创建顺序
 *          下不可靠。改用 `select()` 后与 Qt 的同步路径完全一致：
 *          每次 poll 直接问内核对每个已登记套接字做就绪查询，**没有
 *          需要维护/复位的事件状态，也就没有「漏置位」这一故障面**。
 *
 * @note 只在 poll 里以零超时调用（非阻塞查询）；`select` 的 fd_set 在
 *       Winsock 里是**数组**（`fd_count`/`fd_array`），与 BSD 的位图不同，
 *       故这里按 Qt 的写法直接赋值 `fd_array[0]`。
 */
static int xw32_selectOne(XDeviceNetworkContextWin32Select* p,
                          bool wantRead, bool wantWrite,
                          bool* outRead, bool* outWrite, bool* outError)
{
    fd_set fdRead, fdWrite, fdExcept;
    struct timeval tv;
    SOCKET s;
    int ret;

    if (outRead) *outRead = false;
    if (outWrite) *outWrite = false;
    if (outError) *outError = false;
    if (!p || p->socket == INVALID_SOCKET) return SOCKET_ERROR;

    s = p->socket;
    FD_ZERO(&fdRead);
    FD_ZERO(&fdWrite);
    FD_ZERO(&fdExcept);
    if (wantRead) FD_SET(s, &fdRead);
    if (wantWrite) {
        FD_SET(s, &fdWrite);
        /* Windows 需要 exceptfds 才能报告连接失败（Qt 同款注释）。 */
        FD_SET(s, &fdExcept);
    }
    if (!wantRead && !wantWrite) return 0;

    tv.tv_sec = 0;
    tv.tv_usec = 0;      /* 非阻塞查询：事件环 poll 语义。 */

    ret = select(0, &fdRead, &fdWrite, &fdExcept, &tv);
    if (ret == SOCKET_ERROR) return SOCKET_ERROR;

    if (outRead && FD_ISSET(s, &fdRead)) *outRead = true;
    if (outWrite && FD_ISSET(s, &fdWrite)) *outWrite = true;
    if (outError && FD_ISSET(s, &fdExcept)) {
        *outError = true;
        /* 连接失败/异常只减不增就绪计数（Qt：`if (FD_ISSET(...)) ret--;`）。 */
        if (ret > 0) --ret;
    }
    return ret;
}

/**
 * @brief 把套接字置为非阻塞并登记进框架就绪轮询表。
 * @param p 上下文（`p->socket` 必须有效）。
 * @return 成功 true；失败 false（调用方负责关 socket）。
 * @details 与 Qt 的 `setReadNotificationEnabled(true)` 等价：把本上下文
 *          交给事件环，由它在每次 poll 里做一次 `select` 就绪查询。
 *          非阻塞由 `ioctlsocket(FIONBIO)` 显式设置（不依赖
 *          `WSAEventSelect` 的副作用）。
 */
static bool xw32_armSocket(XDeviceNetworkContextWin32Select* p)
{
    u_long nonBlocking = 1;

    if (!p || p->socket == INVALID_SOCKET) return false;

    (void)ioctlsocket(p->socket, FIONBIO, &nonBlocking);
    /* 登记进框架就绪轮询表（不预建/不持任何事件对象）。 */
    (void)xw32_ringAddSockEvent(NULL, p->base.m_base.m_fd, (void*)p);
    return true;
}

/** @brief 注销就绪轮询并复位状态（关 socket 前必须调用）。 */
static void xw32_disarmSocket(XDeviceNetworkContextWin32Select* p)
{
    if (!p) return;
    xw32_ringRemoveSockEvent(NULL, (void*)p);
}

/* =========================================================================
 * 同步收发核心
 * ========================================================================= */

/**
 * @brief 一次同步非阻塞读，把数据搬进指定缓冲。
 * @param p     上下文。
 * @param dest  落地缓冲（容量必须 >= XNETWORK_READ_BUFFER_SIZE）。
 * @param isUdp 是否数据报套接字（决定用 recvfrom 并记录来源）。
 * @return 实际读到的字节数；0 表示对端已优雅关闭；`XNETWORK_IO_WOULD_BLOCK`
 *         表示暂不可读；`XNETWORK_IO_ERROR` 表示出错。
 * @details 语义与 Qt `nativeRead` 一致：一次调用读一次，短读是常态，由
 *          `FD_READ` 水平触发驱动下一次——**不在一次事件里循环抽干**，
 *          避免大流量下长占主循环。
 */
static int xw32_recvOnceInto(XDeviceNetworkContextWin32Select* p, char* dest, bool isUdp)
{
    int got;

    if (!p || !dest) return XNETWORK_IO_ERROR;

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesRead = 0;
        if (!ReadFile(p->namedPipe, dest, XNETWORK_READ_BUFFER_SIZE,
                      &bytesRead, NULL)) {
            DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                return 0;                        /* 对端关管：与 TCP FIN 同语义。 */
            if (error == ERROR_NO_DATA) return XNETWORK_IO_WOULD_BLOCK;
            return XNETWORK_IO_ERROR;
        }
        return (int)bytesRead;
    }

    if (p->socket == INVALID_SOCKET) return XNETWORK_IO_ERROR;

    if (isUdp) {
        p->fromAddrLen = (int)sizeof(p->fromAddr);
        got = recvfrom(p->socket, dest, XNETWORK_READ_BUFFER_SIZE, 0,
                       (struct sockaddr*)&p->fromAddr, &p->fromAddrLen);
    }
    else {
        got = recv(p->socket, dest, XNETWORK_READ_BUFFER_SIZE, 0);
    }

    if (got == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK) return XNETWORK_IO_WOULD_BLOCK;
        return XNETWORK_IO_ERROR;
    }
    return got;
}

/**
 * @brief 一次同步非阻塞写。
 * @param p        上下文。
 * @param data     数据。
 * @param len      字节数。
 * @param destAddr UDP 目标地址（NULL 表示走已 connect 的对端）。
 * @param destPort 目标端口。
 * @param isUdp    是否数据报。
 * @return 已写出字节数；`XNETWORK_IO_WOULD_BLOCK` 表示背压；`XNETWORK_IO_ERROR`
 *         表示参数或链路错误。
 * @note UDP 数据报要求整包发出：`sendto` 不会短写，返回少于 len 视为错误。
 */
static int64_t xw32_sendOnce(XDeviceNetworkContextWin32Select* p,
                             const void* data, int64_t len,
                             const XHostAddress* destAddr, uint16_t destPort,
                             bool isUdp)
{
    int64_t total = 0;

    if (!p || !data || len <= 0) return XNETWORK_IO_ERROR;

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        const char* cursor = (const char*)data;
        while (total < len) {
            int64_t chunk = len - total;
            DWORD written = 0;
            if (chunk > XNETWORK_PIPE_WRITE_CHUNK) chunk = XNETWORK_PIPE_WRITE_CHUNK;
            if (!WriteFile(p->namedPipe, cursor + total, (DWORD)chunk, &written, NULL)) {
                if (total > 0) return total;   /* 部分写：把已写的如实上报。 */
                return XNETWORK_IO_ERROR;
            }
            total += (int64_t)written;
            if (written == 0) break;           /* 管道满：交给上层续发。 */
        }
        return total;
    }

    if (p->socket == INVALID_SOCKET) return XNETWORK_IO_ERROR;

    if (isUdp) {
        int sent;
        if (len > XNETWORK_WRITE_BUFFER_SIZE) return XNETWORK_IO_ERROR;
        if (destAddr) {
            struct sockaddr_storage dest;
            int destLen = 0;
            addr2sa(destAddr, destPort, &dest, &destLen);
            sent = sendto(p->socket, (const char*)data, (int)len, 0,
                          (const struct sockaddr*)&dest, destLen);
        }
        else {
            sent = send(p->socket, (const char*)data, (int)len, 0);
        }
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) return XNETWORK_IO_WOULD_BLOCK;
            return XNETWORK_IO_ERROR;
        }
        return (int64_t)sent;
    }

    /* TCP / 命名管道：可能短写；把已写的量如实返回，剩余由 ring buffer 续发。 */
    {
        int sent = send(p->socket, (const char*)data, (int)len, 0);
        if (sent == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) return XNETWORK_IO_WOULD_BLOCK;
            return XNETWORK_IO_ERROR;
        }
        return (int64_t)sent;
    }
}

/* =========================================================================
 * 读 / 写推进（单缓冲 + 续发契约）
 * ========================================================================= */

/**
 * @brief 从内核读一次并投递读完成（对标 Qt `QAbstractSocketPrivate::readFromSocket`）。
 * @param p     上下文。
 * @param isUdp 是否数据报。
 * @return 本次是否产生了给上层的读完成投递。
 * @details **与 Qt 完全同构的一次读**：
 *          - 直接把数据读进 `readBuffer`（框架的设备层读缓冲），投递 `Read(N)`；
 *            上层在事件处理里把数据拷进自己的 ring buffer 后发 `ContinueRead`。
 *          - **不做「缓冲区被占用」的门控**：Qt 的引擎同样无此概念——就绪就
 *            `recv`，短读合法，下一轮再来。之前那套 scratch/挂账设计是自己
 *            发明的补丁，反而让「有数据但没投递」的时序变多。
 *          - 读到 0 字节 ⇒ 投递 `Read(0)`，由框架的 PEEK 复核裁定真 FIN/假 EOF。
 *          - `EWOULDBLOCK` ⇒ 本轮无数据，直接返回（**仍已调用 recv**，满足
 *            MSDN 的 FD_READ 再使能要求）。
 * @note 每次通知只读一次、不循环抽干（Qt 同款），避免大流量长占主循环。
 */
static bool xw32_armRead(XDeviceNetworkContextWin32Select* p, bool isUdp)
{
    int got;

    if (!p || !p->base.m_connected || !p->autoRead) return false;

    got = xw32_recvOnceInto(p, p->readBuffer, isUdp);

    if (got > 0) {
        p->readFinishedBytes = (size_t)got;
        p->readReadyCount++;
        xw32_postReady(p, XSocketActType_Read, (uint32_t)got);
        return true;
    }
    if (got == 0) {
        /* 对端关闭：投递 0 字节读完成，让框架的 PEEK 复核下结论。 */
        p->readFinishedBytes = 0;
        p->closed = true;
        xw32_postReady(p, XSocketActType_Read, 0);
        return true;
    }
    if (got == XNETWORK_IO_ERROR) {
        /* 硬错误（连接被重置等）：同样走 0 字节完成，由 PEEK 复核判定；
         * 对端确已断开时 PEEK 返回 0 ⇒ 框架下发关闭事件。 */
        p->closed = true;
        xw32_postReady(p, XSocketActType_Read, 0);
        return true;
    }
    return false;   /* EWOULDBLOCK：本轮无数据，等下次水平触发。 */
}

/** @brief 把 ring buffer 里积压的数据尽量推出去（写就绪 / ContinueWrite 共用）。 */
static void xw32_drainWrite(XDeviceNetworkContextWin32Select* p,
                            XRingBuffer* rb, bool isUdp)
{
    if (!p || !rb) return;

    while (XRingBuffer_available(rb) > 0) {
        char tempBuf[XNETWORK_WRITE_BUFFER_SIZE];
        size_t chunk = XRingBuffer_peek(rb, tempBuf, sizeof(tempBuf));
        int64_t sent;
        if (chunk == 0) break;
        sent = xw32_sendOnce(p, tempBuf, (int64_t)chunk, NULL, 0, isUdp);
        if (sent <= 0) break;                    /* 背压：下一轮再来（数据留在队列）。 */
        XRingBuffer_skip(rb, (size_t)sent);
        p->writeFinishedBytes = (size_t)sent;
        if (isUdp || (size_t)sent < chunk) break; /* 数据报不拼包；短写即停。 */
    }
    p->writePending = (XRingBuffer_available(rb) > 0);
}

/* =========================================================================
 * 端点属性同步（connect / accept 后回填 socket 对象的地址与端口）
 * ========================================================================= */

static void xw32_syncSocketEndpoints(XDeviceNetworkContext* priv)
{
    XDeviceNetworkContextWin32Select* p;
    XAbstractSocket* socket;
    struct sockaddr_storage address;
    int addressLength;
    XHostAddress endpoint;
    uint16_t port = 0;

    if (!priv || !priv->m_owner) return;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET) return;

    socket = (XAbstractSocket*)priv->m_owner;
    XHostAddress_init(&endpoint);

    addressLength = (int)sizeof(address);
    if (getsockname(p->socket, (struct sockaddr*)&address, &addressLength) == 0) {
        sa2addr(&address, &endpoint, &port);
        XAbstractSocket_setLocalAddress(socket, &endpoint);
        XAbstractSocket_setLocalPort(socket, port);
    }

    addressLength = (int)sizeof(address);
    if (getpeername(p->socket, (struct sockaddr*)&address, &addressLength) == 0) {
        sa2addr(&address, &endpoint, &port);
        XAbstractSocket_setPeerAddress(socket, &endpoint);
        XAbstractSocket_setPeerPort(socket, port);
    }

    XClassDeinit(&endpoint);
}

/* =========================================================================
 * 上下文生命周期
 * ========================================================================= */

XDeviceNetworkContext* XDeviceNetwork_createContext(void)
{
    XDeviceNetworkContextWin32Select* p =
        (XDeviceNetworkContextWin32Select*)XCalloc_System(
            1, sizeof(XDeviceNetworkContextWin32Select));
    if (!p) return NULL;

    p->socket = INVALID_SOCKET;
    p->namedPipe = INVALID_HANDLE_VALUE;
    p->base.m_base.m_fd = XFD_INVALID;
    p->magic = XW32_SELECT_MAGIC;
    p->autoRead = true;

    XHostAddress_init(&p->pendingPeerAddr);
    XHostAddress_setAddressSpecial(&p->pendingPeerAddr, XHostAddress_NullSpecial);

    return (XDeviceNetworkContext*)p;
}

void XDeviceNetwork_deleteContext(XDeviceNetworkContext* priv)
{
    XDeviceNetworkContextWin32Select* p;
    if (!priv) return;
    p = W32S(priv);

    priv->m_owner = NULL;
    /* 先注销就绪轮询项再清魔数：注销后事件环不会再回调本上下文，此后的
     * 任何残留访问都会因魔数不匹配被 dispatch 拒绝（见 xw32_isLive）。 */
    xw32_disarmSocket(p);
    p->magic = 0;
    if (p->socket != INVALID_SOCKET) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
    }
    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(p->namedPipe);
        p->namedPipe = INVALID_HANDLE_VALUE;
    }
    XClassDeinit(&p->pendingPeerAddr);
    XFree_System(p);
}

intptr_t XDeviceNetwork_socketDescriptor(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return -1;
    return W32S(priv)->namedPipe != INVALID_HANDLE_VALUE
        ? (intptr_t)W32S(priv)->namedPipe : (intptr_t)W32S(priv)->socket;
}

/* =========================================================================
 * 绑定 / 连接 / 断开
 * ========================================================================= */

uint16_t XDeviceNetwork_socketBind(XFd xfd, const XHostAddress* address,
    uint16_t port, bool reuseAddr, bool shareAddr,
    XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    int af, type, proto;
    struct sockaddr_storage addrStorage;
    int addrLen = 0;
    uint16_t actualPort = 0;

    if (!priv || !address) return 0;
    p = W32S(priv);
    (void)shareAddr;

    XDeviceNetwork_ensureInit();

    af = (XHostAddress_protocol(address) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;
    type = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    proto = (sockType == XDeviceNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;

    /* WSA_FLAG_OVERLAPPED：与 Qt 一致（MSDN KB179942，非阻塞 socket 的历史
     * 约束）。虽然本后端不用 overlapped I/O，保留该标志无副作用。 */
    p->socket = WSASocket(af, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (p->socket == INVALID_SOCKET) return 0;

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }

    addr2sa(address, port, &addrStorage, &addrLen);
    if (bind(p->socket, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        return 0;
    }

    {
        struct sockaddr_storage boundAddr;
        int boundAddrLen = (int)sizeof(boundAddr);
        if (getsockname(p->socket, (struct sockaddr*)&boundAddr, &boundAddrLen) == 0) {
            actualPort = (boundAddr.ss_family == AF_INET6)
                ? ntohs(((struct sockaddr_in6*)&boundAddr)->sin6_port)
                : ntohs(((struct sockaddr_in*)&boundAddr)->sin_port);
        }
    }

    /* 事件绑定同时完成两件事：置非阻塞 + 建就绪事件并登记进 ring。
     * 这里是 bind 之后的套接字（已绑定本地端点，尚未 connect）：既可能
     * 之后被 connect（UDP 数据报用 connect 固化对端），也可能只是被动
     * 收数据。**先不给 FD_CONNECT**——UDP 的 connect 不产生 FD_CONNECT
     * 通知，而 TCP 走到这里是 Bind 操作（不是 Connect），后续若真发起
     * connect 会在 socketConnect 里重新绑定带 FD_CONNECT 的掩码。 */
    if (!xw32_armSocket(p)) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        return 0;
    }

    p->base.m_connected = true;
    return actualPort;
}

bool XDeviceNetwork_socketConnect(XFd xfd, const XString* hostName,
    uint16_t port, XDeviceNetworkProtocol protocol,
    XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    const char* hostStr;
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    struct addrinfo* ai;
    struct sockaddr_storage destAddr;
    int destLen = 0;
    int af, type, proto;
    int rc;

    if (!priv || !hostName) return false;
    hostStr = XString_toUtf8(hostName);
    if (!hostStr || !hostStr[0]) return false;
    p = W32S(priv);

    XDeviceNetwork_ensureInit();

    XMemset(&hints, 0, sizeof(hints));
    hints.ai_family = (protocol == XDeviceNetwork_IPv6) ? AF_INET6 :
        (protocol == XDeviceNetwork_IPv4) ? AF_INET : AF_UNSPEC;
    hints.ai_socktype = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;

    if (getaddrinfo(hostStr, NULL, &hints, &result) != 0) {
        return false;
    }

    ai = result;
    while (ai && ai->ai_family != hints.ai_family) ai = ai->ai_next;
    if (!ai) ai = result;
    if (!ai) { freeaddrinfo(result); return false; }

    af = ai->ai_family;
    type = (sockType == XDeviceNetwork_Tcp) ? SOCK_STREAM : SOCK_DGRAM;
    proto = (sockType == XDeviceNetwork_Tcp) ? IPPROTO_TCP : IPPROTO_UDP;

    /* 目标地址先在本地备好，freeaddrinfo 之后仍然可用。 */
    XMemset(&destAddr, 0, sizeof(destAddr));
    if (af == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&destAddr;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons(port);
        XMemcpy(&s6->sin6_addr, &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr,
                sizeof(struct in6_addr));
        destLen = (int)sizeof(struct sockaddr_in6);
    }
    else {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&destAddr;
        s4->sin_family = AF_INET;
        s4->sin_port = htons(port);
        s4->sin_addr = ((struct sockaddr_in*)ai->ai_addr)->sin_addr;
        destLen = (int)sizeof(struct sockaddr_in);
    }
    freeaddrinfo(result);

    p->socket = WSASocket(af, type, proto, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (p->socket == INVALID_SOCKET) return false;

    /* 先绑本地任意端口：与 IOCP 后端一致（先 bind 让本地端口在 FD_CONNECT
     * 到达前就可查，且与 UDP 分支共用同一套端点语义）。 */
    {
        struct sockaddr_storage localAddr;
        int localLen = (af == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
                                        : (int)sizeof(struct sockaddr_in);
        XMemset(&localAddr, 0, sizeof(localAddr));
        ((struct sockaddr*)&localAddr)->sa_family = (unsigned short)af;
        if (bind(p->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
            closesocket(p->socket);
            p->socket = INVALID_SOCKET;
            return false;
        }
    }

    /* 事件绑定必须在 connect 之前完成：否则「立即成功」的 connect 没有事件
     * 对象可置位，FD_CONNECT 会丢。
     * 掩码要**包含 FD_CONNECT**（TCP 异步连接靠它通知完成）；TCP 连接建立
     * 后该事件不会再触发（一次性），不影响后续收发。 */
    if (!xw32_armSocket(p)) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        return false;
    }

    sa2addr(&destAddr, &p->pendingPeerAddr, NULL);
    p->pendingPeerPort = port;

    rc = connect(p->socket, (struct sockaddr*)&destAddr, destLen);
    if (rc == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK) {
            xw32_disarmSocket(p);
            closesocket(p->socket);
            p->socket = INVALID_SOCKET;
            return false;
        }
        /* 连接在途：等 FD_CONNECT（事件环唤醒 → DispatchReady）。 */
        p->connectPending = true;
        return true;
    }

    /* 立即成功（本机环回的常态）。这里**不**直接改 socket 状态：统一由
     * DispatchReady 分发的 FD_CONNECT 走同一条路径，让「立即成功」与
     * 「稍后成功」两类时序在上层看来完全一致。 */
    p->connectPending = true;
    return true;
}

bool XDeviceNetwork_socketConnectLocal(XFd xfd, const XString* pipePath,
                                 XDeviceNetworkLocalStreamType streamType,
                                 int timeoutMs,
                                 XDeviceNetworkSocketType sockType)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    const char* pathText;
    char fullPath[512];
    HANDLE handle;
    DWORD mode = PIPE_READMODE_BYTE;

    if (!priv || !pipePath || streamType != XDeviceNetwork_LocalStream_NamedPipe
        || sockType != XDeviceNetwork_Tcp) return false;
    pathText = XString_toUtf8(pipePath);
    if (!pathText || !pathText[0]) return false;
    if (strncmp(pathText, "\\\\.\\pipe\\", 9) == 0) {
        if (snprintf(fullPath, sizeof(fullPath), "%s", pathText) >= (int)sizeof(fullPath))
            return false;
    } else if (snprintf(fullPath, sizeof(fullPath), "\\\\.\\pipe\\%s", pathText)
               >= (int)sizeof(fullPath)) {
        return false;
    }

    p = W32S(priv);
    XDeviceNetwork_socketDisconnect(xfd);

    /* 命名管道不是套接字：用同步句柄（不带 FILE_FLAG_OVERLAPPED）。读路径
     * 由服务端主动写触发，读写都走同步调用——与 IOCP 后端的 pipe 分支在
     * 「上层可观察行为」上等价（该通道只服务 MySQL 等本地传输）。 */
    handle = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY) {
        if (!WaitNamedPipeA(fullPath, timeoutMs < 0 ? NMPWAIT_WAIT_FOREVER
                                                    : (DWORD)timeoutMs)) return false;
        handle = CreateFileA(fullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (handle == INVALID_HANDLE_VALUE) return false;
    if (!SetNamedPipeHandleState(handle, &mode, NULL, NULL)) {
        CloseHandle(handle);
        return false;
    }

    p->namedPipe = handle;
    p->base.m_connected = true;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
    p->closed = false;
    return true;
}

void XDeviceNetwork_socketDisconnect(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv) return;
    p = W32S(priv);

    xw32_disarmSocket(p);

    if (p->socket != INVALID_SOCKET) {
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
    }
    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(p->namedPipe);
        p->namedPipe = INVALID_HANDLE_VALUE;
    }

    p->base.m_connected = false;
    p->connectPending = false;
    p->readPending = false;
    p->writePending = false;
    p->closed = false;
    p->readFinishedBytes = 0;
}

/* =========================================================================
 * 读 / 写
 * ========================================================================= */

/**
 * @brief 读接口（异步契约）。
 * @details 数据搬运发生在就绪分派里（`DispatchReady` 收到 `FD_READ` 时同步
 *          recv 进 `readBuffer`），所以本函数与 IOCP 后端一样：只把上层
 *          ring buffer 里已到的数据交给调用方，返回 0 表示本轮无新增。
 *          **不在这里发起 I/O**，避免把「事件驱动」退化成「调用驱动」。
 * @note 与 IOCP 后端的刻意差异：没有「读挂死看门狗」。本后端不存在挂起的
 *       读请求，`FD_READ` 是水平触发，内核里还有数据就会重新置位。
 */
int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    (void)sockType;


    if (!priv || !buf || len <= 0) return -1;

    if (ringBuffer) {
        XRingBuffer* rb = (XRingBuffer*)ringBuffer;
        size_t available = XRingBuffer_available(rb);
        if (available > 0) {
            size_t toRead = (len < (int64_t)available) ? (size_t)len : available;
            return (int64_t)XRingBuffer_read(rb, buf, toRead);
        }
    }
    return 0;
}

int64_t XDeviceNetwork_socketWrite(XFd xfd, const void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, const XHostAddress* destAddr,
    uint16_t destPort, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    bool isUdp;
    int64_t written;

    if (!priv || !buf || len <= 0) return -1;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET && p->namedPipe == INVALID_HANDLE_VALUE)
        return -1;

    isUdp = (sockType == XDeviceNetwork_Udp);

    /* 先把上一轮积压推出去，避免「新写直接把旧积压饿死」。 */
    if (ringBuffer)
        xw32_drainWrite(p, (XRingBuffer*)ringBuffer, isUdp);

    written = xw32_sendOnce(p, buf, len, destAddr, destPort, isUdp);

    if (written == XNETWORK_IO_WOULD_BLOCK) {
        /* 背压：有 ring buffer 就整包入队并回报「受理」；否则如实失败。 */
        if (!ringBuffer || isUdp) return -1;
        return (int64_t)XRingBuffer_write((XRingBuffer*)ringBuffer, buf, (size_t)len);
    }
    if (written == XNETWORK_IO_ERROR) {
        if (ringBuffer && !isUdp)
            return (int64_t)XRingBuffer_write((XRingBuffer*)ringBuffer, buf, (size_t)len);
        return -1;
    }

    p->writeFinishedBytes = (size_t)written;

    /* 短写：剩余部分入 ring buffer，等 FD_WRITE 续发（不丢半帧）。 */
    if (written < len) {
        p->writePending = true;
        if (!ringBuffer) return written;        /* 无缓冲：如实上报短写 */
        XRingBuffer_write((XRingBuffer*)ringBuffer, (const char*)buf + written,
                          (size_t)(len - written));
    }
    else {
        p->writePending = false;
    }
    return len;
}

void XDeviceNetwork_socketContinueWrite(XFd xfd, XRingBuffer* ringBuffer, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv || !ringBuffer) return;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET && p->namedPipe == INVALID_HANDLE_VALUE) return;

    xw32_drainWrite(p, ringBuffer, isUdp);
}

void XDeviceNetwork_socketContinueRead(XFd xfd, bool isUdp)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv) return;
    p = W32S(priv);

    /* 上层已把 readBuffer 拷进自己的 ring buffer ⇒ 清占用标记，并立刻
     * 发起下一次读。这是本后端唯一的「续读」入口。
     *
     * Qt 对照：`QAbstractSocketPrivate::readFromSocket` 每轮只读一次，
     * 读完立即 `setReadNotificationEnabled(true)` 由事件循环驱动下一轮。
     * 本框架把「通知重新使能」表达为 ContinueRead，语义等价。 */
    p->readPending = false;
    if (!p->base.m_connected || !p->autoRead) return;
    (void)xw32_armRead(p, isUdp);
}

/* =========================================================================
 * 就绪分派（事件环回调入口，与 XNetIoRingWin32 的唯一接缝）
 * ========================================================================= */

/** 
 * @brief 事件环每轮 poll 时对单个套接字做一次就绪查询与推进。
 * @param owner 该套接字的属主上下文（`XDeviceNetworkContextWin32Select*`）。
 * @details 与 Qt `QNativeSocketEnginePrivate::nativeSelect`
 *          （qns_win.cpp:1495）同机制：**直接 `select` 问内核**，再按
 *          就绪结果推进。做四件事：
 *            1. `select` 查询可读/可写/异常；
 *            2. 可读 ⇒ `recv` 一次并投递 `Read(N)`（0 字节照投，由框架
 *               PEEK 复核裁定真 FIN/假 EOF）；
 *            3. 可写 ⇒ 清写背压标记并投递 `Write`；
 *            4. 异常 ⇒ 走 0 字节读完成，交框架复核。
 *
 *          **没有事件对象、没有需要复位的状态**：每次都是现问现判，因此
 *          不存在 `WSAEventSelect` 那套「漏置位/漏复位」的故障面。实测该
 *          故障面确实发生过（事件对象从不置位，而内核里确有数据）。
 *
 *          connect 完成判定：`select` 没有 `FD_CONNECT` 对应位，改用
 *          `SO_ERROR` 轮询（Qt 的 `_q_testConnection` 同样查 `SO_ERROR`）。
 * @note **不用 XFd 反查属主**：XFd 是 `XFixedPool` 的池槽位号，连接关闭
 *       后会被下一个连接复用。按 fd 反查会把就绪派给刚复用该槽位的新连接
 *       （串包/丢包）；Qt 同样是用 notifier 的 `wp` 直接寻址。
 */
void XDeviceNetwork_win32_selectDispatchReady(void* owner)
{
    XDeviceNetworkContextWin32Select* p = (XDeviceNetworkContextWin32Select*)owner;
    bool r = false, w = false, x = false;
    int n;
    bool isUdp;

    if (!p) return;
    if (!xw32_isLive(p)) {
        /* 残留项指向已析构的上下文（地址被复用）：直接拒绝。
         * 这不该发生（deleteContext 会先注销），出现即说明有路径漏了
         * 注销，用这条日志把它暴露出来而不是静默串包。 */
        XPrintf("[XNetwork-WSA] 就绪回调命中已失效上下文 %p（轮询项未注销）\n",
                (void*)p);
        return;
    }
    if (p->socket == INVALID_SOCKET) return;

    isUdp = (p->base.m_socketType == XDeviceNetwork_Udp);

    /* --- 连接完成判定（select 无 FD_CONNECT 位，用 SO_ERROR）--- */
    if (p->connectPending) {
        int soError = 0;
        int soLen = (int)sizeof(soError);
        n = xw32_selectOne(p, false, true, NULL, &w, &x);
        if (n >= 0 && (w || x)) {
            if (getsockopt(p->socket, SOL_SOCKET, SO_ERROR,
                           (char*)&soError, &soLen) == 0 && soError == 0) {
                p->base.m_connected = true;
                xw32_syncSocketEndpoints(&p->base);
            }
            else {
                p->base.m_connected = false;
            }
            p->connectPending = false;
            xw32_postReady(p, XSocketActType_Connect, 0);
        }
    }

    if (!p->base.m_connected) return;

    if (p->isServer) {
        /* 监听套接字：只看可读（新连接到达）与异常。 */
        n = xw32_selectOne(p, true, false, &r, NULL, &x);
        if (n > 0 && r) {
            /* 不在这里 accept：消费方在上层（serverGetAcceptedSocket）。
             * select 是水平触发，未 accept 前每次 poll 都会再报可读。 */
            xw32_postReady(p, XSocketActType_Accept, 0);
        }
        return;
    }

    n = xw32_selectOne(p, true, true, &r, &w, &x);

    if (n > 0 && w) {
        p->writeReadyCount++;
        p->writePending = false;
        xw32_postReady(p, XSocketActType_Write, 0);
    }

    if (n > 0 && x) {
        /* 异常/连接被重置：投递 0 字节读完成，由框架的 PEEK 复核裁定。 */
        p->closed = true;
        p->readFinishedBytes = 0;
        xw32_postReady(p, XSocketActType_Read, 0);
        return;
    }

    if (n > 0 && r) {
        (void)xw32_armRead(p, isUdp);
    }
}

/* =========================================================================
 * 事件处理（框架把 CQ 分派回来后的状态收敛）
 * ========================================================================= */

bool XDeviceNetwork_socketHandleEvent(XFd xfd, void* event)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    XEvent* e;
    XEventSockAct* sockAct;

    if (!priv || !event) return false;
    p = W32S(priv);
    e = (XEvent*)event;
    if (e->m_type != XEVENT_TYPE_SOCK_ACT) return false;
    sockAct = (XEventSockAct*)e;

    if (sockAct->actType & XSocketActType_Read) {
        /* 不改 readPending：清占用与发起下一次读都归 ContinueRead 一处处理，
         * 避免「事件处理」与「续发」两条路径各自改同一个状态位（那会让
         * readBuffer 在两次 recv 之间被并发覆盖）。这里只回报本次读量。 */
        return p->readFinishedBytes > 0;
    }
    if (sockAct->actType & XSocketActType_Write) {
        return true;
    }
    if (sockAct->actType & XSocketActType_Connect) {
        return true;
    }
    if (sockAct->actType & XSocketActType_Accept) {
        return true;
    }
    return false;
}

/* =========================================================================
 * 描述符接管 / 选项
 * ========================================================================= */

bool XDeviceNetwork_socketSetDescriptor(XFd deviceFd, intptr_t fd, int state, int openMode)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv || fd == -1) return false;
    (void)openMode;
    p = W32S(priv);

    p->socket = (SOCKET)fd;
    p->closed = false;

    /* 接管已有连接：绑事件对象（同时把 socket 置非阻塞）。accept 出来的
     * 套接字会继承监听套接字的事件关联，这里重新绑定会覆盖掉该继承关
     * 系，避免多连接共用一个事件对象。
     * 掩码用「已连接」口径（**不含 FD_CONNECT**）：这是已建好的连接，
     * 注册 FD_CONNECT 会因事件持久性立刻报一次假的「连接完成」。 */
    if (!xw32_armSocket(p)) {
        p->socket = INVALID_SOCKET;
        return false;
    }

    p->base.m_connected = (state == 3);
    p->autoRead = true;

    if (p->base.m_connected) {
        /* 接管时可能已经有数据在等：立刻取一次（水平触发保证之后仍会
         * 被唤醒，不依赖这次是否成功）。 */
        (void)xw32_armRead(p, priv->m_socketType == XDeviceNetwork_Udp);
    }
    return true;
}

bool XDeviceNetwork_serverSetDescriptor(XFd deviceFd, intptr_t fd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv || fd == -1) return false;
    p = W32S(priv);
    if (p->socket != INVALID_SOCKET || p->isServer) return false;

    p->socket = (SOCKET)fd;
    p->isServer = true;
    p->base.m_connected = true;
    if (!xw32_armSocket(p)) {
        p->socket = INVALID_SOCKET;
        p->isServer = false;
        p->base.m_connected = false;
        return false;
    }
    return true;
}

bool XDeviceNetwork_socketSetOption(XFd xfd, int option, const void* value)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    int* intVal;

    if (!priv || !value) return false;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET) return false;
    intVal = (int*)value;

    switch (option) {
    case 0: return setsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, (char*)intVal, sizeof(int)) == 0;
    case 1: return setsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, (char*)intVal, sizeof(int)) == 0;
    case 2: return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_SetTtl, intVal) == 0;
    case 3: {
        bool enabled = (*intVal != 0);
        return XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket,
                                          XMC_SetLoop, &enabled) == 0;
    }
    case 4: return setsockopt(p->socket, IPPROTO_IP, IP_TOS, (char*)intVal, sizeof(int)) == 0;
    case 5: return setsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, (char*)intVal, sizeof(int)) == 0;
    case 6: return setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)intVal, sizeof(int)) == 0;
    case 8: return setsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, (char*)intVal, sizeof(int)) == 0;
    default: return false;
    }
}

void* XDeviceNetwork_socketGetOption(XFd xfd, int option)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    static int result;
    int optLen = (int)sizeof(int);

    if (!priv) return NULL;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET) return NULL;

    switch (option) {
    case 0: if (getsockopt(p->socket, IPPROTO_TCP, TCP_NODELAY, (char*)&result, &optLen) == 0) return &result; break;
    case 1: if (getsockopt(p->socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&result, &optLen) == 0) return &result; break;
    case 2: if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_GetTtl, &result) == 0) return &result; break;
    case 3: {
        bool e = false;
        if (XDeviceNetwork_multicastOp((XDeviceNetworkSocketHandle)p->socket, XMC_GetLoop, &e) == 0) {
            result = e ? 1 : 0;
            return &result;
        }
        break;
    }
    case 4: if (getsockopt(p->socket, IPPROTO_IP, IP_TOS, (char*)&result, &optLen) == 0) return &result; break;
    case 5: if (getsockopt(p->socket, SOL_SOCKET, SO_SNDBUF, (char*)&result, &optLen) == 0) return &result; break;
    case 6: if (getsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)&result, &optLen) == 0) return &result; break;
    case 7: if (getsockopt(p->socket, IPPROTO_IP, IP_MTU, (char*)&result, &optLen) == 0) return &result; break;
    case 8: if (getsockopt(p->socket, SOL_SOCKET, SO_BROADCAST, (char*)&result, &optLen) == 0) return &result; break;
    default: break;
    }
    return NULL;
}

void XDeviceNetwork_socketSetReadBufferSize(XFd xfd, int64_t size)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    int bufSize;

    if (!priv) return;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET || size <= 0) return;
    bufSize = (int)((size > INT_MAX) ? INT_MAX : size);
    setsockopt(p->socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize));
}

/* =========================================================================
 * 异步状态查询
 * ========================================================================= */

const char* XDeviceNetwork_socketReadBuffer(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32S(priv)->readBuffer : NULL;
}

size_t XDeviceNetwork_socketReadFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32S(priv)->readFinishedBytes : 0;
}

size_t XDeviceNetwork_socketWriteFinishedBytes(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32S(priv)->writeFinishedBytes : 0;
}

bool XDeviceNetwork_socketWritePending(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    return priv ? W32S(priv)->writePending : false;
}

/* =========================================================================
 * 监听 / 接受
 * ========================================================================= */

bool XDeviceNetwork_serverAccept(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv) return false;
    p = W32S(priv);
    if (!p->isServer || p->socket == INVALID_SOCKET) return false;

    /* 本后端不预挂 accept：连接到达由 FD_ACCEPT 通知，真正的 accept() 在
     * serverGetAcceptedSocket 里同步完成。这里返回 true 表示「接受通道
     * 就绪」，与 IOCP 后端的 ContinueAccept 语义对齐。 */
    return true;
}

XDeviceNetworkServerHandle XDeviceNetwork_serverCreate(XFd deviceFd, const XHostAddress* addr, uint16_t port,
    int backlog, bool reuseAddr)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(deviceFd);
    XDeviceNetworkContextWin32Select* p;
    int af;
    SOCKET s;
    struct sockaddr_storage addrStorage;
    int addrLen = 0;
    int ipv6only = 0;

    if (!priv) return -1;
    p = W32S(priv);
    XDeviceNetwork_ensureInit();

    af = (XHostAddress_protocol(addr) == XHostAddress_IPv6Protocol) ? AF_INET6 : AF_INET;

    s = WSASocket(af, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) return -1;

    if (reuseAddr) {
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    if (af == AF_INET6) {
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&ipv6only, sizeof(ipv6only));
    }

    addr2sa(addr, port, &addrStorage, &addrLen);
    if (bind(s, (struct sockaddr*)&addrStorage, addrLen) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    if (listen(s, backlog > 0 ? backlog : SOMAXCONN) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    p->socket = s;
    p->isServer = true;
    p->base.m_connected = true;
    p->closed = false;

    /* 监听套接字只关心 FD_ACCEPT / FD_CLOSE。accept 出来的连接会**继承**
     * 同一事件对象与掩码（官方明示）——每条连接在 socketSetDescriptor 里
     * 都会重新绑定自己的事件对象，继承关系随即被替换，不会串扰。 */
    if (!xw32_armSocket(p)) {
        closesocket(s);
        p->socket = INVALID_SOCKET;
        p->isServer = false;
        p->base.m_connected = false;
        return -1;
    }

    return (XDeviceNetworkServerHandle)s;
}

uint16_t XDeviceNetwork_serverPort(XDeviceNetworkServerHandle server)
{
    struct sockaddr_storage addr;
    int addrLen = (int)sizeof(addr);

    if (server == -1) return 0;
    if (getsockname((SOCKET)server, (struct sockaddr*)&addr, &addrLen) == 0) {
        if (addr.ss_family == AF_INET6)
            return ntohs(((struct sockaddr_in6*)&addr)->sin6_port);
        return ntohs(((struct sockaddr_in*)&addr)->sin_port);
    }
    return 0;
}

void XDeviceNetwork_serverClose(XFd xfd, XDeviceNetworkServerHandle server)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p = priv ? W32S(priv) : NULL;
    SOCKET socketToClose = (SOCKET)server;

    if (server == -1) return;

    if (p && p->socket == socketToClose) {
        xw32_disarmSocket(p);
        closesocket(p->socket);
        p->socket = INVALID_SOCKET;
        p->isServer = false;
        p->base.m_connected = false;
    }
    else {
        /* 上层拒收的已接受句柄：所有权已转给它，这里只负责关。 */
        closesocket(socketToClose);
    }
}

XDeviceNetworkSocketHandle XDeviceNetwork_serverGetAcceptedSocket(XFd xfd,
    XHostAddress* clientAddr, uint16_t* clientPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    SOCKET client;
    struct sockaddr_storage peer;
    int peerLen = (int)sizeof(peer);

    if (!priv) return -1;
    p = W32S(priv);
    if (!p->isServer || p->socket == INVALID_SOCKET) return -1;

    /* 监听套接字在 xw32_armSocket 里已置非阻塞（ioctlsocket FIONBIO），
     * 故 accept 无连接时立即返回 WSAEWOULDBLOCK，不会在主循环里阻塞。 */
    client = accept(p->socket, NULL, NULL);
    if (client == INVALID_SOCKET) return -1;

    /* 显式把新连接置为非阻塞。
     * 本后端已不再使用 `WSAEventSelect`（改用 `select` 查询就绪），因此
     * 没有「事件对象被 accept 继承」的问题；但 socket 的阻塞属性**不保证**
     * 随 accept 继承（不同 Winsock 实现行为不一），而本后端全部收发路径都
     * 建立在「非阻塞 + WSAEWOULDBLOCK 背压」之上——阻塞 recv 会把主循环
     * 钉死，所以这里显式设置。 */
    {
        u_long nonBlocking = 1;
        (void)ioctlsocket(client, FIONBIO, &nonBlocking);
    }

    if (clientAddr || clientPort) {
        XMemset(&peer, 0, sizeof(peer));
        if (getpeername(client, (struct sockaddr*)&peer, &peerLen) == 0)
            sa2addr(&peer, clientAddr, clientPort);
    }

    /* 新连接继承监听套接字的非阻塞属性；调用方随后会走
     * socketSetDescriptor 为它绑定专属事件对象。 */
    return (XDeviceNetworkSocketHandle)client;
}

/* =========================================================================
 * UDP 来源
 * ========================================================================= */

bool XDeviceNetwork_platformGetLastDatagramSender(XFd xfd,
                                     XHostAddress* srcAddr, uint16_t* srcPort)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv) return false;
    p = W32S(priv);
    if (p->fromAddrLen <= 0) return false;
    if (srcAddr || srcPort)
        sa2addr((const struct sockaddr_storage*)&p->fromAddr, srcAddr, srcPort);
    return true;
}

#endif /* XNETWORK_USE_PLATFORM_API && _WIN32 && XNETWORK_WIN_SOCKET_WSAEVENT */
