/**
 * @file XDeviceNetwork_win32_select.c
 * @brief Windows 套接字后端：WSAAsyncSelect 消息模型（Qt 6.8 同款）。
 * @details 本文件与 `XDeviceNetwork_win32.c`（IOCP 重叠 I/O 后端，**当前
 *          缺省**）**并存**，由 `XNETWORK_WIN_BACKEND` 编译期二选一
 *          （见 XNetwork_config.h）：
 *
 *            - `XNETWORK_WIN_BACKEND_IOCP`（`XDeviceNetwork_win32.c`）——缺省；
 *            - `XNETWORK_WIN_BACKEND_WSAASYNC`（本文件）——Qt 6.8 同款，
 *              隐藏窗口 + WM_APP 消息投递；保留可回退。
 *            - `XNETWORK_WIN_BACKEND_WSAEVENT`——**实现已移除**（2026-11-21
 *              晚；曾按「一次关联 + 每轮 Wait(0)+Enum」实现，选它编译期报错）。
 *
 *          ## 为什么需要这一套
 *
 *          本机环回链路上 `WSARecv` 的 IOCP 完成通知被实测证实不可靠：
 *          两端同时停止接收、之后各自继续发送 5~10 秒、最终静默判线。
 *          `GetOverlappedResult(FALSE)` + `ioctlsocket(FIONREAD)` 三态判定
 *          曾把结论写成「平台丢完成包 / 挂起的读请求被平台挂死」——
 *          **该定性已被推翻**（2026-11-06 根修）：完成包从来没丢，真因是
 *          IOCP 后端**复用了在飞的 OVERLAPPED**、完成端口唤醒包饥饿、
 *          完成包归属按句柄数值判定，三者叠加才造成读停摆。
 *          既然重叠 I/O 的完成包语义在本框架里本就难做对（复用/归属/饥饿
 *          三个坑），不如照 Qt 走**不用重叠 I/O**的路子。
 *
 *          Qt 6.8 `qnativesocketengine_win.cpp` 完全不用 overlapped I/O：
 *          `WSARecv/WSASend` 的 OVERLAPPED 参数一律传 `nullptr`，就绪检测
 *          由窗口消息驱动。**它不依赖完成包投递，所以这个坑对它不存在。**
 *          本后端（WSAASYNC）照此实现：就绪通知走 `WSAAsyncSelect` 窗口
 *          消息——框架主循环已有自有事件环（`XAbstractNetIoRing`），其
 *          Windows 实现持有一个 message-only 隐藏窗口，正好做这个消息的
 *          落点，不新增线程、不新增等待集。（历史上的 WSAEVENT 变体曾把
 *          就绪通知换成 `WSAEventSelect` 事件对象，其实现已移除。）
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
 *          ## 就绪事件的真实形态（**2026-11-21 订正**）
 *
 *          本文件现状是「**一次 WSAAsyncSelect 关联 + ring 消息窗缓存**」：
 *
 *          1. 每个上下文把自己的 socket 做**一次性** `WSAAsyncSelect` 关联
 *             到 ring 的隐藏窗口（掩码取全集，见 `xw32_probeArm`）。
 *             **绝不重发关联**：重注册在 AFD 里是「取消旧 poll IRP + 重挂」
 *             复合操作，病态（对端已 FIN）套接字上会永久悬挂。
 *          2. FD_* 事件以 WM_APP 消息到达，由窗口 WndProc 缓存进 ring 的
 *             消息槽；事件环每轮 `pollPlatform` 泵送消息（WSAASYNC 构建下
 *             阻塞等待用 `MsgWaitForMultipleObjectsEx` 把 IOCP 端口与消息
 *             队列放进同一等待点，事件到达**立即唤醒**，见
 *             `XNetIoRingWin32.c` 的 `waitForEvents`）。
 *          3. 属主回调（本文件的 `XDeviceNetwork_win32_selectDispatchReady`）
 *             从消息槽取走该 socket 累计的事件位并入 `pendingEvents` 缓存，
 *             按角色派发。
 *
 *          （WSAEVENT 时代的「事件对象 + 每轮 Wait(0)+Enum」形态及其
 *          ~100Hz 轮询节拍随实现一并移除。）
 *
 * @note 本文件不使用任何 OVERLAPPED / AcceptEx / ConnectEx。命名管道
 *       （`socketConnectLocal`）在 Windows 上本来就不是套接字语义，这里用
 *       同名 `CreateFile` + 同步读写实现，与 IOCP 后端的 pipe 分支等价。
 */

#include "XNetwork_config.h"

#if defined(XNETWORK_USE_PLATFORM_API) && defined(_WIN32)

/* ====== Windows 宏定义必须在所有头文件之前 ====== */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

/* ====== 项目头文件 ====== */
#include "XDeviceNetwork.h"
#include "XDeviceNetwork_fault.h"  /* 平台错误码注入接口（**仅测试用**）。 */
#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include "XEvent.h"
#include "XCoreApplication.h"   /* 读失败时直接把关闭事件投递给属主对象 */
#include "XHostAddress.h"
#include "XAbstractSocket.h"
#include "XFileDescriptor.h"
#include "XAbstractNetIoRing.h"
#include "XNetIoRingWin32.h"
#include "XDateTime.h"

/* ====== Windows SDK 头文件 ====== */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>      /* SRWLOCK / InterlockedExchange（注入段用）。 */
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

/* =========================================================================
 * 平台错误码注入（**仅测试用**；生产路径恒不触发）
 * =========================================================================
 *
 * ## 为什么需要它
 *
 * 框架层把 Windows 的 WSA 错误码映射成 Qt 口径的
 * `XAbstractSocket_SocketError`（见
 * `Src/XCore/XCode/XNetwork/XAbstractSocket/XAbstractSocket.c` 的
 * `socketErrorFromPlatform`）。但其中有几条契约在测试里**造不出来**：
 * `WSAECONNREFUSED` / `WSAETIMEDOUT` / `WSAEHOSTUNREACH` / `WSAEACCES` /
 * `WSAENOBUFS` —— 真机上要么得「连一个真会失败的地址」（慢、不确定，还
 * 看当时的网络状态），要么根本撞不出来。
 *
 * ## 设计原则：**注入只改「结果」，不改「流程」**
 *
 * 注入点**不是另写一条假路径**，而是把某次平台调用的**结果**替换掉：
 * 读路径把 `recv` 的结果伪造成 `SOCKET_ERROR` + 注入的错误码；写路径把
 * `send` 的结果伪造成 `SOCKET_ERROR` + 注入的错误码；connect / bind 同理。
 * → 之后**原有的收口代码一字不改**地处理它 → **注入错误与真实错误走
 * 的是同一条收口路径**（返回值、`lastError` 的处理、是否进错误通道、
 * 是否判死，全部一致）。
 *
 * 这条原则是**硬要求**：若测试走的是假路径（另写一个「假装失败」的分支），
 * 测出来的行为**不能代表真实行为** —— 例如真实 `WSAEWOULDBLOCK`
 * **不进 `lastError`**（否则框架会把「对端正常关闭」误报成
 * TemporaryError，把真正的关闭原因顶掉）；假路径若顺手写了 `lastError`，
 * 测出来的是假的。
 *
 * ## 零成本
 *
 * 未武装注入时，热路径上只有一句
 * `if ((g_faultArmedMask & (1L << step)) == 0) return 0;`
 * —— 一个**分支预测为假的比较**（读一个全局位掩码 + 与 + 判零）：
 * 不引入锁、不引入系统调用、不写内存，正常路径的性能特征不变。
 * 真正的判定逻辑在慢路径（SRWLOCK 保护）里，只有「本步骤已武装」时
 * 才会走到。
 *
 * ## 线程安全
 *
 * 注入状态是**进程级**的（不是线程级）：套接字收发跑在**事件循环线程**
 * 上，而测试可能从另一个线程下发注入；若用 TLS（`__declspec(thread)`），
 * 跨线程注入会**静默失效** → 测试「什么都没发生」→ **假绿**。
 * 故这里用「全局位掩码（快速路径，volatile LONG）+ SRWLOCK 保护的状态
 * （慢路径）」：任何线程都能武装/清除，事件循环线程都能看到。
 *
 * @note 与 `Src/XCore/XDevice/XDeviceNetwork.c` 的线程级「最近一次操作失败
 *       错误码」（TLS 槽，`XDeviceNetwork_setLastOperationError`）取舍不同：
 *       那个是**运行期数据**（按线程归属天然正确），这里是**测试控制项**
 *       （必须跨线程可见）。
 * ========================================================================= */

/**
 * @brief 每个注入点的状态。
 */
typedef struct XDeviceNetworkFaultSlot {
    int errorCode;    /**< 待注入的平台错误码（WSA 码）；0 = 本槽未武装。 */
    int skipCount;    /**< 还要放行多少次同一步骤的操作，之后才开始注入。 */
    int repeatCount;  /**< 还要注入多少次；0 = 不限次（持续注入）。 */
} XDeviceNetworkFaultSlot;

/** @brief 注入点总数（枚举上界 + 1）。 */
#define XNETWORK_FAULT_STEP_COUNT  ((int)XDeviceNetworkFaultStep_Bind + 1)

/**
 * @brief 快速路径的「已武装」位掩码（每个注入点一位）。
 * @details 未武装时恒为 0 → 热路径的检查是「一个分支预测为假的比较」。
 *          写侧用 `InterlockedExchange`（自带全屏障）保证对**其它线程**
 *          立即可见。
 */
static volatile LONG g_faultArmedMask = 0;

/** @brief 慢路径的状态锁（SRWLOCK：静态初始化即可；Windows Vista+）。 */
static SRWLOCK g_faultLock = SRWLOCK_INIT;

/** @brief 各注入点的状态（只在 `g_faultLock` 保护下访问）。 */
static XDeviceNetworkFaultSlot g_faultSlots[XNETWORK_FAULT_STEP_COUNT];

/**
 * @brief 更新「已武装」位掩码（**必须在 `g_faultLock` 保护下调用**）。
 * @param step 注入点。
 * @param on   true = 置位；false = 清位。
 */
static void xw32_faultSetMask(int step, bool on)
{
    LONG mask = g_faultArmedMask;
    if (on) mask |= (1L << step);
    else mask &= ~(1L << step);
    /* 全屏障：保证对**其它线程**（事件循环线程）立即可见。 */
    InterlockedExchange(&g_faultArmedMask, mask);
}

#if XNETWORK_WIN_SOCKET_SELECT

/**
 * @brief 取一次注入（慢路径：加锁 + 推进计数器）。
 * @param step 注入点。
 * @return 本轮要注入的错误码；本轮不注入时返回 0。
 * @details 计数语义：
 *          - `skipCount > 0` ⇒ 本次**放行**（照常执行真实操作），计数减一；
 *          - 否则 ⇒ 返回注入的错误码，并把 `repeatCount` 减一；
 *            减到 0 时**解除武装**（快速路径重新变回「一个假分支」）。
 *          - `repeatCount == 0` ⇒ 不限次：持续注入，直到 Clear。
 */
static int xw32_faultTakeSlow(int step)
{
    XDeviceNetworkFaultSlot* slot;
    int result = 0;

    if (step <= (int)XDeviceNetworkFaultStep_None ||
        step >= XNETWORK_FAULT_STEP_COUNT) return 0;
    slot = &g_faultSlots[step];

    AcquireSRWLockExclusive(&g_faultLock);
    if (slot->errorCode != 0) {
        if (slot->skipCount > 0) {
            slot->skipCount--;
        }
        else {
            result = slot->errorCode;
            if (slot->repeatCount > 0) {
                slot->repeatCount--;
                if (slot->repeatCount == 0) {
                    slot->errorCode = 0;
                    xw32_faultSetMask(step, false);
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_faultLock);
    return result;
}

/**
 * @brief 取一次注入（**热路径**：一个分支预测为假的比较）。
 * @param step 注入点。
 * @return 本轮要注入的错误码；本轮不注入时返回 0。
 */
static int xw32_faultTake(int step)
{
    if ((g_faultArmedMask & (1L << step)) == 0) return 0;
    return xw32_faultTakeSlow(step);
}

#endif /* XNETWORK_WIN_SOCKET_SELECT */

/**
 * @brief 当前构建的后端是否支持错误码注入。
 */
bool XDeviceNetwork_socketFaultSupported(void)
{
#if XNETWORK_WIN_SOCKET_SELECT
    return true;
#else
    /* 对照后端（IOCP 重叠 I/O）没有注入点：显式报「不支持」，免得调用方
     * 以为注入生效了（→ 假绿）。 */
    return false;
#endif
}

/**
 * @brief 武装一个可重复的平台错误码注入。
 */
bool XDeviceNetwork_socketFaultInjectRepeat(XDeviceNetworkFaultStep step, int errorCode,
                                            int skipCount, int repeatCount)
{
    int s = (int)step;
    if (!XDeviceNetwork_socketFaultSupported()) return false;
    if (s <= (int)XDeviceNetworkFaultStep_None || s >= XNETWORK_FAULT_STEP_COUNT)
        return false;
    if (errorCode == 0) return false;   /* 0 = 「没有错误」，不能注入 */
    if (skipCount < 0) skipCount = 0;
    if (repeatCount < 0) repeatCount = 0;   /* 负数按「不限次」处理 */

    AcquireSRWLockExclusive(&g_faultLock);
    g_faultSlots[s].errorCode = errorCode;
    g_faultSlots[s].skipCount = skipCount;
    g_faultSlots[s].repeatCount = repeatCount;
    xw32_faultSetMask(s, true);
    ReleaseSRWLockExclusive(&g_faultLock);
    return true;
}

/**
 * @brief 武装一个一次性的平台错误码注入（打完自动解除武装）。
 */
bool XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep step, int errorCode,
                                      int skipCount)
{
    return XDeviceNetwork_socketFaultInjectRepeat(step, errorCode, skipCount, 1);
}

/**
 * @brief 解除某个注入点的武装。
 */
void XDeviceNetwork_socketFaultClear(XDeviceNetworkFaultStep step)
{
    int s = (int)step;
    if (s == (int)XDeviceNetworkFaultStep_None) {
        XDeviceNetwork_socketFaultClearAll();
        return;
    }
    if (s < 0 || s >= XNETWORK_FAULT_STEP_COUNT) return;
    AcquireSRWLockExclusive(&g_faultLock);
    g_faultSlots[s].errorCode = 0;
    g_faultSlots[s].skipCount = 0;
    g_faultSlots[s].repeatCount = 0;
    xw32_faultSetMask(s, false);
    ReleaseSRWLockExclusive(&g_faultLock);
}

/**
 * @brief 解除所有注入点的武装。
 */
void XDeviceNetwork_socketFaultClearAll(void)
{
    int i;
    AcquireSRWLockExclusive(&g_faultLock);
    for (i = 0; i < XNETWORK_FAULT_STEP_COUNT; i++) {
        g_faultSlots[i].errorCode = 0;
        g_faultSlots[i].skipCount = 0;
        g_faultSlots[i].repeatCount = 0;
        xw32_faultSetMask(i, false);
    }
    ReleaseSRWLockExclusive(&g_faultLock);
}

/**
 * @brief 查询某个注入点的当前状态（供测试自检）。
 */
bool XDeviceNetwork_socketFaultQuery(XDeviceNetworkFaultStep step, int* errorCode,
                                     int* skipRemaining, int* repeatRemaining)
{
    int s = (int)step;
    XDeviceNetworkFaultSlot* slot;
    int code = 0;
    int skip = 0;
    int repeat = 0;
    bool armed = false;

    if (s <= (int)XDeviceNetworkFaultStep_None || s >= XNETWORK_FAULT_STEP_COUNT)
        return false;
    slot = &g_faultSlots[s];
    AcquireSRWLockExclusive(&g_faultLock);
    code = slot->errorCode;
    skip = slot->skipCount;
    repeat = slot->repeatCount;
    armed = (code != 0);
    ReleaseSRWLockExclusive(&g_faultLock);

    if (errorCode) *errorCode = code;
    if (skipRemaining) *skipRemaining = skip;
    if (repeatRemaining) *repeatRemaining = repeat;
    return armed;
}

#endif /* XNETWORK_USE_PLATFORM_API && _WIN32 */

#if defined(XNETWORK_USE_PLATFORM_API) && defined(_WIN32) && \
    XNETWORK_WIN_SOCKET_SELECT

/* =========================================================================
 * 内部定义
 * ========================================================================= */

#define XNETWORK_READ_BUFFER_SIZE  8192   /**< 单次 recv 的落地缓冲容量（与 IOCP 后端同值）。 */
#define XNETWORK_WRITE_BUFFER_SIZE 8192   /**< 单次 send 的分片上限（与 IOCP 后端同值）。 */

/** @brief 命名管道单次写分片：同步写要一次性拷入内核，避免大块栈缓冲。 */
#define XNETWORK_PIPE_WRITE_CHUNK  4096

/**
 * @brief 单次 `send` 的块上限（字节，Qt `nativeWrite` 口径 49152）。
 * @details Qt 的 `qnativesocketengine_win.cpp` `nativeWrite` 把一次 send
 *          钳到 49152 字节（避免长时间独占线程）。本后端同款：每次 send
 *          最多 49152；更大数据由上层写环分块续发。
 *          遇到 `WSAENOBUFS` 时**不在本轮忙等重试**（烧 CPU、放大尾延迟），
 *          直接按背压返回，由写环在下一轮以更小的块（≤ 写环槽）续发——
 *          这就是「下一轮用更小的块」的真实口径，不是本轮减半重试。
 */
#define XNETWORK_SEND_NOBUFS_MAX_CHUNK  49152

/**
 * @brief 内部收发原语的三态返回（与业务层的「0 = 背压」口径区分开）。
 * @details **订正（2026-11-21）**：旧注释写的是「`< 0` 错误、`0` 不可写、
 *          `> 0` 字节」，**与代码矛盾**。真实口径是：
 *          - `XNETWORK_IO_WOULD_BLOCK`（-3）= 内核暂不可读写（背压）；
 *          - `XNETWORK_IO_ERROR`（-1）= 真的出错；
 *          - `0` = **对端已优雅关闭（EOF）**，不是「不可写」；
 *          - `> 0` = 实际字节数。
 *          注意 `0` 与「EOF」同义这一点在读路径上被大量使用（recv 返 0
 *          ⇒ 对端关闭），写路径上不会出现（send 返 0 只可能是 0 长度写）。
 */
#define XNETWORK_IO_WOULD_BLOCK  (-3)
#define XNETWORK_IO_ERROR        (-1)

/* =========================================================================
 * 前置声明（内部函数）
 * ========================================================================= */

/* 注：结构体全定义在下方「套接字私有数据」段；此处只用 `struct` 前置
 * 声明（不重复 typedef，避免 C99 下的 typedef 重定义）。 */

/** @brief 注销就绪轮询并复位状态（关 socket 前必须调用）。 */
static void xw32_disarmSocket(struct XDeviceNetworkContextWin32Select* p);
/** @brief 把「连接已死」以关闭事件的形式投递给属主对象。 */
static void xw32_postSockClose(struct XDeviceNetworkContextWin32Select* p);
/** @brief 写侧致命错误的唯一收口（去重 + 日志 + 关闭事件）。 */
static void xw32_postFatalClose(struct XDeviceNetworkContextWin32Select* p,
                                int err, const char* where);

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
    bool autoRead;                   /**< 连接建立后自动挂读（框架既有语义）。
                                          **死字段**：全仓只有置 true 的地方
                                          （armSocket 路径与 socketSetDescriptor），
                                          没有任何地方置 false —— 框架层的
                                          `readPaused` 走的是「不 ContinueRead」
                                          而不是关这个标志。保留字段仅为兼容，
                                          不要依赖它做「暂停读」。 */
    bool isServer;                   /**< 监听套接字。 */
    bool closed;                     /**< 已判定连接已死（避免重复投递关闭事件）。 */
    bool probeArmed;                 /**< 当前 socket 已做**一次性** WSAAsyncSelect
                                          关联（关联只发一次；换 socket/
                                          断开时复位）。 */

    /* [A5 2026-11-21] 事件位缓存：本轮从 ring 消息槽取回的全部
     * 事件位。**每轮只取位一次**，各分支读这份缓存而不是各自再取
     * （takeMsgEvents 取走即清，第二次取是空的 ⇒ 那一轮到达的读事件被吞）。
     * 被消费掉的位从这里清除；条件型事件（FD_READ/FD_WRITE）在条件仍成立时
     * 由内核/续发路径兜底（FD_READ 常驻语义见 xw32_consumeEvents 注）。 */
    uint32_t pendingEvents;

    /* [A3 2026-11-21] 已观察到 EOF（recv 返 0）但**同一轮还交付了数据**
     * ⇒ 关闭事件必须推迟到「上一批被上层取走」之后（Qt 的
     * `canCloseNotification()`：有数据就先发 readyRead、把关闭推到下一回合，
     * 否则会丢最后一帧数据）。 */
    bool eofPending;
    /* [A3 2026-11-21] 同上，但观察到的是**硬错误**（非 RST/ABORT 的
     * 读侧错误）⇒ 推迟投递「连接已死」的关闭事件。 */
    bool errPending;

    /* [接口冻结 2026-11-21] 最近一次失败的原始 OS 错误码（Windows 上是
     * WSA 错误码；命名管道路径是 Win32 错误码）。成功路径**不**更新，
     * 保持上一次的值直到下一次失败覆盖它。
     *
     * [复审 P4 2026-11-21] **按连接代**：换代（换 socket / disconnect）必须
     * 清零，否则上一代的错误码会被当成本代的 ⇒「对端正常关闭」会被误报成
     * 上一代的错误（框架层据此映射 `XAbstractSocket_SocketError`）。
     * 清零统一走 `xw32_resetConnState`。 */
    int lastError;

    uint32_t readReadyCount;         /**< 累计读完成投递次数（诊断用）。 */
    uint32_t writeReadyCount;        /**< 累计写就绪投递次数（诊断用）。 */

    size_t readFinishedBytes;        /**< 最近一次交付给上层的字节数。 */
    size_t writeFinishedBytes;       /**< 最近一次写出字节数。 */

    /* [管道 2026-11-21] 上层传进来的**写环**（设备层写缓冲）。
     *
     * 套接字路径不需要它：写就绪事件（FD_WRITE）会驱动 `xw32_drainWrite`，
     * 而 drain 的实参由框架在 `socketContinueWrite` 时传进来。
     * 命名管道**没有写就绪事件**（没有 WSA 事件可等）⇒ 写环里的数据
     * 没人推 ⇒ 必须记住这个指针，在管道的就绪派发里主动 drain。
     * 指针由框架持有（设备层写缓冲），生命周期与套接字一致。 */
    XRingBuffer* writeRing;

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

/**
 * @brief 清零**一代连接**的全部状态（换代时调用）。
 * @details [复审 P4/P6 2026-11-21] 凡替换 `p->socket` 或断开连接之处，都必须
 *          把「代内状态」一并清零，否则上一代的残留会污染新一代：
 *            · `pendingEvents` —— 上一代残留的 `FD_CLOSE` 位会让**新连接**
 *              一上来就被判死；
 *            · `eofPending` / `errPending` —— 上一代推迟的关闭会被当成本代的；
 *            · `lastError` —— 上一代的错误码会被当成本代的 ⇒ 框架按它映射出
 *              错误码时会把「对端正常关闭」误报成上一代的错误。
 * @note `probeArmed` 由调用方单独复位（它与事件对象绑定，语义不同）。
 */
static void xw32_resetConnState(XDeviceNetworkContextWin32Select* p)
{
    if (!p) return;
    /* [自修复 2026-11-21] 这里原本被批量替换误改成「调用自己」⇒ 无限递归
     * ⇒ 栈溢出（实测：三个 socket 套件第一条用例即崩，退出码 127，
     * cdb 栈全是 xw32_resetConnState 自递归）。批量替换脚本按「三行状态清零」
     * 文本匹配，把**本函数自己的函数体**也换了 ⇒ 教训：批量改代码必须
     * 排除被新增的函数本身（或改完立刻 review 差异）。 */
    p->pendingEvents = 0;
    p->eofPending = false;
    p->errPending = false;
    p->lastError = 0;
    /* [W15 2026-11-21 复审] 写环指针也是**代内状态**：指向设备层的写缓冲，
     * 换代后不再有效 ⇒ 必须清掉（否则可能对着别的连接的写缓冲推数据）。 */
    p->writeRing = NULL;
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
        /* [Qt 6.8 对齐] 双栈 socket 收到的 IPv4 对端会以 v4-mapped 形式出现
         * （::ffff:a.b.c.d）。Qt 的 QHostAddress 会还原为 IPv4，这里同样
         * 还原，否则上层会拿到 "::ffff:127.0.0.1" 而匹配不上 IPv4 地址。 */
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
            uint32_t v4;
            memcpy(&v4, &s6->sin6_addr.s6_addr[12], sizeof(v4));
            XHostAddress_setAddressIPv4(addr, ntohl(v4));
        } else {
            XHostAddress_setAddressIPv6(addr, (const uint8_t*)&s6->sin6_addr);
        }
        if (port) *port = ntohs(s6->sin6_port);
    }
    else if (ss->ss_family == AF_INET) {
        const struct sockaddr_in* s4 = (const struct sockaddr_in*)ss;
        XHostAddress_setAddressIPv4(addr, ntohl(s4->sin_addr.s_addr));
        if (port) *port = ntohs(s4->sin_port);
    }
}

/** @brief 套接字是否为 IPv6（双栈）socket。 */
static bool xw32_socketIsIpv6(SOCKET s)
{
    struct sockaddr_storage ss;
    int len = (int)sizeof(ss);
    if (s == INVALID_SOCKET) return false;
    if (getsockname(s, (struct sockaddr*)&ss, &len) != 0) return false;
    return ss.ss_family == AF_INET6;
}

/**
 * @brief 构造目标地址；IPv4 目标发给双栈 IPv6 socket 时转成 v4-mapped。
 * @details 对齐 Qt 引擎：`nativeSendDatagram` 会把 IPv4 目标映射为
 *          `::ffff:a.b.c.d` 再交给 sendto，双栈 socket 才能正常发出。
 */
static void xw32_mapV4Dest(SOCKET s, const XHostAddress* destAddr, uint16_t port,
                           struct sockaddr_storage* ss, int* ssLen)
{
    addr2sa(destAddr, port, ss, ssLen);
    if (ss->ss_family == AF_INET && xw32_socketIsIpv6(s)) {
        struct sockaddr_in6 mapped;
        XMemset(&mapped, 0, sizeof(mapped));
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = htons(port);
        mapped.sin6_addr.s6_addr[10] = 0xFF;
        mapped.sin6_addr.s6_addr[11] = 0xFF;
        memcpy(&mapped.sin6_addr.s6_addr[12],
               &((struct sockaddr_in*)ss)->sin_addr.s_addr, 4);
        XMemset(ss, 0, sizeof(*ss));
        memcpy(ss, &mapped, sizeof(mapped));
        *ssLen = (int)sizeof(mapped);
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
 *
 *          [A9 2026-11-21] **命名管道不再把 64 位 HANDLE 塞进这个 int 字段**：
 *          `m_osFd` 是 `int`（`XAbstractNetIoRing.h`），把 `HANDLE` 强转
 *          会截断成可能 ≤0 的值 ⇒ 框架的「0 字节读完成」分支会因
 *          `m_osFd <= 0` **整条丢弃**（连关闭事件都不会投递）。命名管道
 *          根本不支持 `MSG_PEEK` 复核（`ioctlsocket` 对管道句柄必失败），
 *          故这里填 -1（「不适用关闭判定」），并由后端在管道 EOF 时
 *          **直接投递关闭事件**（见 `xw32_armRead` 的 EOF 分支），
 *          完全绕开那条复核路径。
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
    /* [A9] 命名管道：m_osFd 不适用（-1），不要截断 64 位 HANDLE 进去。 */
    entry.m_osFd = (p->namedPipe != INVALID_HANDLE_VALUE)
                       ? -1
                       : (int)(intptr_t)p->socket;
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

/**
 * @brief 把「连接已死」以关闭事件的形式投递给属主对象。
 * @param p 本方上下文（提供 XFd 与属主指针）。
 * @details 与 IOCP 后端（`XDeviceNetwork_win32.c: w32_postSockClose`）同款：
 *          读侧**硬错误**（非 RST/ABORT）直接投递关闭事件，不借「0 字节读
 *          完成」那条路。
 *
 *          [理由订正 2026-11-21] 旧理由写的是「Windows 侧的假 EOF 复核会
 *          把错误冲掉 ⇒ 关闭事件被丢弃」——**该理由已过期**：框架层的
 *          `XAbstractNetIoRing_platformPeerReallyClosed`
 *          （`Drive/windows/Core/XNetIoRingWin32.c:760-814`）在 2026-11-18
 *          改成**先取错误码再复原**，并把 `WSAECONNRESET`/`WSAECONNABORTED`
 *          显式映射为「真关闭」。所以读侧 RST 现在走 0 字节路径也能正确
 *          落到关闭事件（见 B1）。
 *
 *          仍然保留「硬错误直投关闭事件」的理由是**另一条**：那条复核对
 *          **未知错误码**保守按「对端健在」处理（`default: return false`）
 *          ⇒ 0 字节完成会被整条丢弃 ⇒ 关闭事件永远不来。而读侧报错本身
 *          就是连接死亡的直接证据，不需要也不应该再复核。
 * @note 用属主指针直接投递，不经 XFd 反查：XFd 是池槽位号，连接拆除后
 *       可能已被新对象复用（历史串包/误杀事故的同源原因）。
 */
static void xw32_postSockClose(XDeviceNetworkContextWin32Select* p)
{
    XEventSockClose* closeEvent;
    if (!p || !p->base.m_owner) return;
    closeEvent = XEventSockClose_create(p->base.m_base.m_fd);
    if (!closeEvent) return;
    ((XEvent*)closeEvent)->m_posted = true;
    ((XEvent*)closeEvent)->m_spontaneous = true;
    XCoreApplication_postEvent((XObject*)p->base.m_owner, (XEvent*)closeEvent,
                               XEVENT_PRIORITY_NORMAL);
}

/**
 * @brief 判别写侧错误是否意味着「连接已死」。
 * @param err 原始 OS 错误码（WSA）。
 * @return 致命（连接已死）返回 true；瞬时（背压/可重试）返回 false。
 * @details [A1 2026-11-21] 照抄 IOCP 后端的分类表
 *          （`Drive/windows/Device/Network/XDeviceNetwork_win32.c` 的
 *          `w32_isFatalWriteError`），并按本轮任务口径收敛到**流连接**
 *          写侧可达的那一组：RST / ABORT / 未连接 / 已 shutdown /
 *          超时 / 对端不可达 / 网络不可达（另加 IOCP 表里的
 *          `WSAENETDOWN` / `WSAENETRESET` / `WSAENOTSOCK`——同族的
 *          「连接不可用」错误）。
 *
 *          **绝不判死**的：`WSAEWOULDBLOCK`（发送缓冲满的瞬时背压）、
 *          `WSAENOBUFS`（系统缓冲区暂满 ⇒ 已按 Qt 口径改用小块重试，
 *          见 `xw32_sendOnce`）、以及一切未知错误（保守按瞬时处理，
 *          避免误杀正常链路与正常重连）。
 *
 * @note UDP 的数据报错误**不等于**流关闭 ⇒ 调用方对 UDP 不走判死
 *       （与 IOCP 后端的 `fatalAllowed` 语义一致）。
 */
static bool xw32_isFatalWriteError(int err)
{
    switch (err) {
    case WSAECONNRESET:              /* 10054 对端 RST */
    case WSAECONNABORTED:            /* 10053 软件导致连接中止 */
    case WSAENOTCONN:                /* 10057 套接字未连接 */
    case WSAESHUTDOWN:               /* 10058 已 shutdown 后仍发送 */
    case WSAETIMEDOUT:               /* 10060 连接超时 */
    case WSAEHOSTUNREACH:            /* 10065 对端不可达 */
    case WSAENETUNREACH:             /* 10051 网络不可达 */
    case WSAENETDOWN:                /* 10050 网络子系统失效 */
    case WSAENETRESET:               /* 10052 连接被重置 */
    case WSAENOTSOCK:                /* 10038 句柄已不是套接字 */
        return true;
    default:
        /* 10035 WSAEWOULDBLOCK / 10055 WSAENOBUFS 及一切未知错误：
         * 保持「只回滚、不判死」的瞬时背压语义。 */
        return false;
    }
}

/**
 * @brief 写侧致命错误的唯一收口：去重 + 一行可观测日志 + 投递关闭事件。
 * @param p     本方上下文。
 * @param err   原始 OS 错误码（WSA）。
 * @param where 现场描述（用于日志）。
 * @details 与 IOCP 后端的 `w32_postFatalClose`（`XDeviceNetwork_win32.c`
 *          的同名函数）同款：**同代连接只投递一次关闭事件、只留一行日志**。
 *          关闭事件是**异步** post 的，在它被框架处理（→ 关闭设备 →
 *          `socketDisconnect`）之前，上层可能继续调 `socketWrite`；没有去重
 *          就会刷出成千上万条无用事件与日志。
 *          本函数同时把上下文置为 `closed` ⇒ 派发处不再轮询它、读侧不再
 *          recv、写侧不再续发（写通道随之静默收口，而不是无限囤积）。
 */
static void xw32_postFatalClose(XDeviceNetworkContextWin32Select* p, int err,
                                const char* where)
{
    if (!p) return;
    if (p->closed) return;                 /* 同代已判死：不重复投递 */
    p->closed = true;
    p->writePending = false;
    p->lastError = err;
    XPrintf("[XNetwork-WSA] %s 致命错误 err=%d，判定连接已死，投递关闭事件\n",
            where, err);
    xw32_disarmSocket(p);                  /* 死句柄摘出轮询表 */
    xw32_postSockClose(p);
}

/* =========================================================================
 * 就绪探测（一次性 WSAAsyncSelect 关联 + ring 消息窗缓存位）
 * ========================================================================= */

/**
 * @brief 把当前 socket 与 ring 的隐藏窗口做**一次性** WSAAsyncSelect 关联。
 * @param p 上下文（`p->socket` 必须有效）。
 * @return 成功 true；关联失败 false（调用方记死关 socket）。
 * @details 关联位图一次到位（FD_READ|FD_ACCEPT|
 *          FD_WRITE|FD_CONNECT|FD_CLOSE|FD_OOB 全集，读/写/连接/监听各
 *          角色通用——多出的位在派发处按角色取舍）。**绝不能每轮重发
 *          WSAAsyncSelect**：重注册在 AFD 里是「取消旧 poll IRP+重挂」
 *          复合操作，病态（对端已 FIN）套接字上会永久悬挂（与已移除的
 *          WSAEventSelect 同一 hang 类）。换 socket/断开时由调用方把
 *          probeArmed 复位（disconnect / SetDescriptor / bind / connect /
 *          serverCreate ——**凡替换 `p->socket` 之处都必须复位**）。
 *
 *          [B4 掩码说明] Qt 的 `QEventDispatcherWin32::registerSocketNotifier`
 *          （qeventdispatcher_win.cpp:592-598）按关注位**动态**订阅：
 *          读关注 = `FD_READ|FD_CLOSE|FD_ACCEPT`，写关注 = `FD_WRITE|FD_CONNECT`，
 *          异常关注 = `FD_OOB`。本后端**刻意用全集**、不按关注位重发
 *          关联——因为重发关联会挂在 AFD 端点锁上永久悬挂
 *          （见上），动态改掩码在此不可行。全集的代价是「多出来的位」，
 *          由派发处按角色取舍（见 `XDeviceNetwork_win32_selectDispatchReady`）。
 *          全集的**正确性**由两件事保证：① 每次就绪只做**一次**取位
 *          （`xw32_pollEvents` 从 ring 消息槽 take），取到的位进缓存，
 *          各分支读缓存；
 *          ② `FD_CLOSE` 是**独立于可读**的事件（不再靠「可读掩码里含
 *          FD_CLOSE」顺带捎上），因此不会因为「可读分支先 return」被吞。
 */
/** @brief 关闭套接字并注销 ring 消息槽（本文件所有 closesocket 的统一收口）。
 *  [2026-11-21 槽泄漏根治] accept 会继承 WSAAsyncSelect 关联 ⇒ WndProc 对
 *  accepted socket **自动建消息槽**；若其关闭路径不经过 disarmSocket
 *  （拒收句柄 / 关联失败 / 建连失败等），槽就滞留——并发洪流下槽表(128)
 *  被前序用例的泄漏耗尽，新 socket 的消息位被 WndProc 丢弃 ⇒ 建连永不
 *  完成（实测：并发上限用例 3/5 复现，每次运行均打「slot table FULL」）。
 *  removeMsgSlot 幂等 ⇒ 统一在此收口，不依赖每个关闭点自行记得。 */
static void xw32_closeSocketTracked(SOCKET s)
{
    /* 本文件只剩 WSAASYNC 一种 select 模型实现（WSAEVENT 已移除）：
     * 关闭一律先注销 ring 消息槽再 closesocket。 */
    {
        XAbstractNetIoRing* iring = XAbstractNetIoRing_global();
        XNetIoRingWin32* wring = iring ? (XNetIoRingWin32*)iring : NULL;
        if (wring && s != INVALID_SOCKET)
            XNetIoRingWin32_removeMsgSlot(wring, s);
    }
    if (s != INVALID_SOCKET)
        closesocket(s);
}

static bool xw32_probeArm(XDeviceNetworkContextWin32Select* p)
{
    long mask = FD_READ | FD_ACCEPT | FD_WRITE | FD_CONNECT |
                FD_CLOSE | FD_OOB;
    if (!p || p->socket == INVALID_SOCKET) return false;

    /* [WSAASYNC 2026-11-21] Qt 6.8 同款：WSAAsyncSelect 把 FD_* 事件以
     * WM_APP 消息投递到 ring 的隐藏窗口，由 ring 缓存、pollPlatform 泵送。
     * **关联只发一次**（probeArmed 守卫），失败即记死，
     * 绝不每轮重发（重注册在 AFD 端点锁上的 hang 同样适用于 AsyncSelect）。 */
    if (!p->probeArmed) {
        XAbstractNetIoRing* iring = XAbstractNetIoRing_global();
        XNetIoRingWin32* wring = iring ? (XNetIoRingWin32*)iring : NULL;
        HWND msgHwnd;
        if (!wring) {
            p->lastError = WSAGetLastError();
            p->closed = true;
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            return false;
        }
        msgHwnd = XNetIoRingWin32_msgWindow(wring);
        if (msgHwnd == NULL) {
            p->lastError = WSAGetLastError();
            p->closed = true;
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            return false;
        }
        if (WSAAsyncSelect(p->socket, msgHwnd,
                           XNETIORING_WM_SOCKET, mask) == SOCKET_ERROR) {
            p->lastError = WSAGetLastError();
            p->closed = true;
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            p->probeArmed = false;
            return false;
        }
        /* [句柄复用 2026-11-21] 注册即清残留：SOCKET 句柄被内核复用后，
         * 旧槽里未消费的位（如 FD_CLOSE）会被新 socket 读到 => 监听口被
         * 误判死（双服务端用例实测）。见 XNetIoRingWin32_resetMsgSlot。 */
        XNetIoRingWin32_resetMsgSlot(wring, p->socket);
        p->probeArmed = true;
    }
    return true;
}

/**
 * @brief 取回该 socket 在 ring 消息槽累计的全部网络事件位，并入上下文缓存。
 * @param p 上下文（`p->socket` 必须有效）。
 * @return 本轮可用的全部事件位（`FD_*` 的按位或；无事件返回 0）。
 *
 * @details **就绪探测的形态演进**（四段，均有实锅背书）：
 *
 *          ① 原始形态零超时 `select()`。Qt 6.8 同步等待也用 `select()`
 *          （`qns_win.cpp:1495 nativeSelect`）。病态句柄（对端 FIN 后的
 *          死句柄）上会**永久悬挂**：主线程冻在 pollWsaEvents、全局时间
 *          轮停摆 → 全部 Coarse 定时器死（候选1 诊断文档）。
 *
 *          ② [候选2] 改 WSAEventSelect+Enum，但**每轮重发关联**。对端已
 *          FIN 的 CLOSE_WAIT 套接字上，重注册（取消旧 poll IRP+重挂）会
 *          悬挂在 AFD 端点锁上（cdb 栈：NtDeviceIoControlFile ←
 *          WSAEventSelect）——同一 hang 类换了皮。
 *
 *          ③ [候选3] 关联**一次**（`xw32_probeArm`，probeArmed 守卫，
 *          换 socket/断开复位），此后每轮只 Wait(0)+Enum。事件位累积、
 *          Enum 取走，无「漏置位」故障面，也无每轮重注册的 IRP 序列化
 *          （WSAEVENT 路径，随 WSAEventSelect 实现于 2026-11-21 一并移除）。
 *
 *          ④ [WSAASYNC·现行] Qt 6.8 同款：FD_* 位由 ring 隐藏窗口的
 *          WndProc 缓存，本函数只做「从消息槽取走并入缓存」——没有
 *          事件对象，①②③那一族 hang/漏置位故障面不复存在。
 *
 *          [A5 2026-11-21] **每轮只取位一次**：取回的**全部**位一次存进
 *          `p->pendingEvents`，各分支读缓存而不是各自再取
 *          （takeMsgEvents 取走即清，第二次取是空的 ⇒ 该轮到达的读事件
 *          会被吞）。
 */
static uint32_t xw32_pollEvents(XDeviceNetworkContextWin32Select* p)
{
    if (!p) return 0;
    if (p->socket == INVALID_SOCKET) return p->pendingEvents;

    /* [WSAASYNC] Qt 6.8 同款：事件位由 ring 隐藏窗口的 WndProc 缓存，
     * pollPlatform 每轮 xnetio_pumpMsgOnce 泵送消息后，这里把该 socket
     * 累计的 FD_* 位取走并入缓存。**FD_READ 常驻语义不变**：takeMsgEvents
     * 会把位取走，但本后端靠「每轮补一次 recv + ContinueRead」兜底
     * （见 xw32_consumeEvents 的 FD_READ 注），不依赖 FD_READ 重报。
     *
     * [事件位语义 2026-11-21 订正：FD_READ=EDGE 已由探针实测]
     *   FD_READ   —— 有数据可读；**边沿（EDGE）语义，已实测**：
     *                verify\fd_read_edge_probe.c 在本机（Win10 x64 / Winsock
     *                2.2）用纯 WinSock 复现三种数据滞留形态，结论为
     *                "FD_READ reported once per arrival, not re-armed while
     *                data is queued"。⇒ **FD_READ 位必须常驻，不能消费**：
     *                数据滞留时不会重报，若把位清掉就会永久丢就绪。
     *                pendingEvents 常驻 + 每轮补一次 recv 尝试正是
     *                正确性依据（不是纯冗余）。
     *   FD_WRITE  —— 发送缓冲有空（条件型；但投递一次后不一定重报
     *                ⇒ 写环续发每轮无条件服务，见 DispatchReady）。
     *   FD_CONNECT—— 异步 connect 完成（**一次性**）。
     *   FD_ACCEPT —— 有连接到达（WSAAsyncSelect 下是**边沿**消息 ⇒
     *                serverAccept 成功后要主动补位，见该函数）。
     *   FD_CLOSE  —— 对端关闭（**一次性**，独立于可读）。
     */
    if (!xw32_probeArm(p)) return p->pendingEvents;
    {
        XAbstractNetIoRing* iring = XAbstractNetIoRing_global();
        XNetIoRingWin32* wring = iring ? (XNetIoRingWin32*)iring : NULL;
        if (wring) {
            long bits = 0;
            if (XNetIoRingWin32_takeMsgEvents(wring, p->socket, &bits, NULL) > 0)
                p->pendingEvents |= (uint32_t)bits;
        }
    }
    return p->pendingEvents;
}

/**
 * @brief 消费（清除）缓存中的某些事件位。
 * @param p    上下文。
 * @param mask 要清除的 `FD_*` 位。
 * @details **[FD_READ=EDGE 2026-11-21 探针实测] 绝不要用本函数清 FD_READ**：
 *          verify\fd_read_edge_probe.c 证实 FD_READ 是**边沿**语义——数据滞留时
 *          AFD **不会**重报，清掉该位就会永久丢就绪。故本函数的调用点**仅**
 *          传一次性事件（FD_CONNECT/FD_ACCEPT/FD_CLOSE/FD_WRITE），FD_READ 位
 *          恒常驻（见 xw32_pollEvents 的 FD_READ 注释）。
 *
 *          一次性事件（FD_CONNECT/FD_ACCEPT/FD_CLOSE）清除后**不会**再出现 ⇒
 *          「清了但还没处理完」的场景必须**保留**该位（见派发处的关闭分支）。
 *          旧注释称「FD_READ/FD_WRITE 条件仍成立会被 AFD 重置、清除安全」是
 *          **错的**（与 EDGE 结论直接相反），已订正。
 */
static void xw32_consumeEvents(XDeviceNetworkContextWin32Select* p, uint32_t mask)
{
    if (p) p->pendingEvents &= ~mask;
}

/**
 * @brief 把套接字置为非阻塞并登记进框架就绪轮询表。
 * @param p 上下文（`p->socket` 必须有效）。
 * @return 成功 true；失败 false（调用方负责关 socket）。
 * @details 与 Qt 的 `setReadNotificationEnabled(true)` 等价：把本上下文
 *          交给事件环，由它在每次 poll 里回调一次本上下文的
 *          `XDeviceNetwork_win32_selectDispatchReady`（内部做消息槽取位
 *          与派发）。
 *          非阻塞由 `ioctlsocket(FIONBIO)` 显式设置。
 *
 *          [A4 2026-11-21] **登记失败必须显式失败**：轮询表只有 128 槽
 *          （`XNETIORING_WIN32_MAX_WSA_EVENTS`），表满时
 *          `XNetIoRingWin32_addWsaEvent` 返回 false。旧实现把返回值
 *          **丢弃**（`(void)xw32_ringAddSockEvent(...)`）⇒ 第 129 条
 *          套接字**永远不会被轮询**：连接永远不完成，**无日志无错误**。
 *          现在登记失败即返回 false ⇒ 上层关掉 socket 并报错。
 */
static bool xw32_armSocket(XDeviceNetworkContextWin32Select* p)
{
    u_long nonBlocking = 1;

    if (!p || p->socket == INVALID_SOCKET) return false;

    (void)ioctlsocket(p->socket, FIONBIO, &nonBlocking);
    /* socket 与 ring 隐藏窗口的 WSAAsyncSelect 关联在这里**一次**做完
     * （xw32_probeArm 内 probeArmed 守卫），绝不重发关联（重发会悬挂，
     * 见 xw32_probeArm 注）。 */
    if (!xw32_probeArm(p))
        return false;
    /* 登记进框架就绪轮询表（不预建/不持任何事件对象）。
     * [A4] 登记失败 ⇒ 该套接字永远不会被轮询 ⇒ 必须显式失败。 */
    if (!xw32_ringAddSockEvent(NULL, p->base.m_base.m_fd, (void*)p)) {
        XPrintf("[XNetwork-WSA] 就绪轮询表登记失败（表满或事件环未就绪），"
                "该套接字不会被轮询，连接无法建立\n");
        return false;
    }
    return true;
}

/** @brief 注销就绪轮询并复位状态（关 socket 前必须调用）。 */
static void xw32_disarmSocket(XDeviceNetworkContextWin32Select* p)
{
    if (!p) return;
    if (p->socket != INVALID_SOCKET) {
        XAbstractNetIoRing* iring = XAbstractNetIoRing_global();
        XNetIoRingWin32* wring = iring ? (XNetIoRingWin32*)iring : NULL;
        if (wring)
            XNetIoRingWin32_removeMsgSlot(wring, p->socket);
    }
    xw32_ringRemoveSockEvent(NULL, (void*)p);
}

/* =========================================================================
 * 同步收发核心
 * ========================================================================= */

/**
 * @brief 读侧失败的**唯一收口**：把一个 WSA 错误码翻译成三态返回值。
 * @param p     上下文（要写 `lastError`）。
 * @param error 原始 WSA 错误码（真实失败时来自 `WSAGetLastError()`，
 *              注入失败时来自注入的码）。
 * @return 0 = 对端已关闭（EOF）；`XNETWORK_IO_WOULD_BLOCK` = 背压；
 *         `XNETWORK_IO_ERROR` = 出错。
 * @details **真实失败与注入失败都走这里**（这是「注入走真实收口路径」的
 *          落点）：本函数是读侧唯一的错误判据，注入的错误码被喂进同一个
 *          函数 ⇒ 返回值、`lastError` 的处理完全一致：
 *          - `WSAEWOULDBLOCK` ⇒ 背压，**不进 `lastError`**（否则框架会把
 *            「对端正常关闭」误报成 TemporaryError）；
 *          - `WSAECONNRESET` / `WSAECONNABORTED` ⇒ 归一成 0 字节（Qt 口径，
 *            与真 FIN 不可区分）；
 *          - 其它 ⇒ 记 `lastError`（原始 OS 错误码）+ 出错。
 */
static int xw32_recvFail(XDeviceNetworkContextWin32Select* p, int error)
{
    /* [2026-11-21] **背压不是错误**：`WSAEWOULDBLOCK` 只是「暂时读不到」，
     * **不得**进错误通道——否则它会留在 `lastError` 里，框架按它映射出
     * `TemporaryError`，把真正的关闭原因顶掉（实测：对端优雅关闭后
     * errorOccurred 报成 22=TemporaryError 而不是
     * 1=RemoteHostClosedError）。 */
    if (error == WSAEWOULDBLOCK) return XNETWORK_IO_WOULD_BLOCK;
    p->lastError = error;              /* 接口冻结：记录原始 OS 错误码 */
    /* [B1] Qt 口径：读侧 RST/ABORT ⇒ 0 字节（与真 FIN 不可区分）。 */
    if (error == WSAECONNRESET || error == WSAECONNABORTED) return 0;
    return XNETWORK_IO_ERROR;
}

/**
 * @brief 一次同步非阻塞读，把数据搬进指定缓冲。
 * @param p     上下文。
 * @param dest  落地缓冲。
 * @param len   落地缓冲的可写容量（字节）。
 * @param isUdp 是否数据报套接字（决定用 recvfrom 并记录来源）。
 * @return 实际读到的字节数；**0 表示对端已关闭（EOF）**；
 *         `XNETWORK_IO_WOULD_BLOCK` 表示暂不可读；
 *         `XNETWORK_IO_ERROR` 表示出错。
 * @details [B1 2026-11-21] **读侧 RST/ABORT 归一成 0 字节（EOF）**：
 *          Qt 的 `qnativesocketengine_win.cpp` `nativeRead` 里
 *          `case WSAECONNRESET: case WSAECONNABORTED:` → `ret = 0;`
 *          （注释："for tcp sockets this will be handled in
 *          QNativeSocketEngine::read"）⇒ 读侧 RST 与真 FIN 在**字节数上
 *          不可区分**，上层统一报 `RemoteHostClosedError`。
 *          本后端原先把除 `WSAEWOULDBLOCK` 外的所有错误一律报成
 *          `XNETWORK_IO_ERROR` ⇒ RST 落到「硬错误」分支（直接投递关闭
 *          事件），与 Qt 口径不一致。现在按 Qt 口径归一。
 */
static int xw32_recvOnceInto(XDeviceNetworkContextWin32Select* p, char* dest,
                              int len, bool isUdp)
{
    int got;

    if (!p || !dest || len <= 0) return XNETWORK_IO_ERROR;

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        DWORD bytesRead = 0;
        if (!ReadFile(p->namedPipe, dest, (DWORD)len, &bytesRead, NULL)) {
            DWORD error = GetLastError();
            /* [2026-11-21] **背压不是错误**：管道没有数据时返回
             * `ERROR_NO_DATA`，那是「暂时读不到」，**不得**进错误通道
             * （否则框架会把错误码映射成 TemporaryError/未知错误，
             * 覆盖掉真正的关闭原因）。 */
            if (error == ERROR_NO_DATA) return XNETWORK_IO_WOULD_BLOCK;
            /* 命名管道的错误码是 **Win32 码**（109=ERROR_BROKEN_PIPE、
             * 232=ERROR_NO_DATA、233=ERROR_PIPE_NOT_CONNECTED），与 errno
             * 空间**重叠**（Linux 的 errno.h 里 109 就是 EADDRINUSE）⇒
             * 用**负值**标记来源，框架层据此按 Win32 管道码解释。 */
            p->lastError = -((int)error);
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
                return 0;                        /* 对端关管：与 TCP FIN 同语义。 */
            return XNETWORK_IO_ERROR;
        }
        return (int)bytesRead;
    }

    if (p->socket == INVALID_SOCKET) return XNETWORK_IO_ERROR;

    /* [故障注入 2026-11-21] **读路径注入点**。
     * @details 注入在**发起 recv 之前**判定：真实读失败时内核数据**不会**
     *          被消费，注入必须保持同一语义（否则会把「本该失败」的那次
     *          读变成「读到了数据又报错」→ 上层拿到脏数据）。
     *          注入码被喂进**与真实失败完全相同的收口**（`xw32_recvFail`）
     *          → 返回值、`lastError` 的处理、是否进错误通道全部一致：
     *          注入 `WSAEWOULDBLOCK` 同样**不进** `lastError`，
     *          注入 `WSAECONNRESET/ABORTED` 同样归一成 0 字节（EOF）。 */
    {
        int injected = xw32_faultTake(XDeviceNetworkFaultStep_Read);
        if (injected != 0) return xw32_recvFail(p, injected);
    }

    if (isUdp) {
        p->fromAddrLen = (int)sizeof(p->fromAddr);
        got = recvfrom(p->socket, dest, len, 0,
                       (struct sockaddr*)&p->fromAddr, &p->fromAddrLen);
    }
    else {
        got = recv(p->socket, dest, len, 0);
    }

    if (got == SOCKET_ERROR) {
        return xw32_recvFail(p, WSAGetLastError());
    }
    return got;
}

/**
 * @brief UDP 写失败的**唯一收口**：把一个 WSA 错误码翻译成返回值。
 * @param p     上下文（要写 `lastError`）。
 * @param error 原始 WSA 错误码（真实失败时来自 `WSAGetLastError()`，
 *              注入失败时来自注入的码）。
 * @param outErr 输出失败时的原始 OS 错误码；可为 NULL。
 * @return `XNETWORK_IO_WOULD_BLOCK`（背压）或 `XNETWORK_IO_ERROR`（出错）。
 * @details **真实失败与注入失败都走这里**（这是「注入走真实收口路径」的
 *          落点之一）：`WSAEWOULDBLOCK` ⇒ 背压且**不进 `lastError`**；
 *          其它 ⇒ 记 `lastError` + 出错。
 */
static int64_t xw32_sendFailUdp(XDeviceNetworkContextWin32Select* p, int error,
                                int* outErr)
{
    /* [2026-11-21] 背压不是错误（同读侧）：`WSAEWOULDBLOCK` 不进
     * `lastError` ⇒ 不会把框架的错误码映射顶成 TemporaryError。 */
    if (error == WSAEWOULDBLOCK) {
        if (outErr) *outErr = error;
        return XNETWORK_IO_WOULD_BLOCK;
    }
    p->lastError = error;
    if (outErr) *outErr = error;
    return XNETWORK_IO_ERROR;
}

/**
 * @brief 一次同步非阻塞写。
 * @param p        上下文。
 * @param data     数据。
 * @param len      字节数。
 * @param destAddr UDP 目标地址（NULL 表示走已 connect 的对端）。
 * @param destPort 目标端口。
 * @param isUdp    是否数据报。
 * @param outErr   输出失败时的原始 OS 错误码；可为 NULL。
 * @return 已写出字节数；`XNETWORK_IO_WOULD_BLOCK` 表示背压；`XNETWORK_IO_ERROR`
 *         表示参数或链路错误（`*outErr` 给出原始错误码）。
 * @note UDP 数据报要求整包发出：`sendto` 不会短写，返回少于 len 视为错误。
 *
 * @details [A1 2026-11-21] **`WSAENOBUFS` 不判死**：Qt 的
 *          `qnativesocketengine_win.cpp` `nativeWrite` 注释
 *          ("this function used to not send more than 49152 per call …
 *          We now handle the WSAENOBUFS") ⇒ 口径是「**下一轮**用更小的块
 *          再试」（Qt 原文："just go on, the next loop run we will try a smaller
 *          number"）。
 *          [W7 2026-11-21 复审订正] 本实现**不在同一轮里忙重试**（忙等既
 *          烧 CPU 又放大尾延迟）⇒ 遇 `WSAENOBUFS` 直接按**背压**返回，
 *          由上层写环在下一轮续发（续发块 ≤ 写环槽大小 8192，比 49152
 *          更小 ⇒ 与 Qt「下一轮用更小的块」一致），绝不判死。
 *
 *          [故障注入 2026-11-21] **写路径注入点**：注入时**不发起 send**
 *          （真实写失败时数据不会上线，注入必须保持同一语义），把结果
 *          伪造成「send 返回 SOCKET_ERROR + 注入的错误码」→ 随后的收口
 *          判据**一字不改**地处理它 → **注入错误与真实错误同路**。
 */
static int64_t xw32_sendOnce(XDeviceNetworkContextWin32Select* p,
                             const void* data, int64_t len,
                             const XHostAddress* destAddr, uint16_t destPort,
                             bool isUdp, int* outErr)
{
    int64_t total = 0;

    if (outErr) *outErr = 0;
    if (!p || !data || len <= 0) return XNETWORK_IO_ERROR;

    if (p->namedPipe != INVALID_HANDLE_VALUE) {
        const char* cursor = (const char*)data;
        while (total < len) {
            int64_t chunk = len - total;
            DWORD written = 0;
            if (chunk > XNETWORK_PIPE_WRITE_CHUNK) chunk = XNETWORK_PIPE_WRITE_CHUNK;
            if (!WriteFile(p->namedPipe, cursor + total, (DWORD)chunk, &written, NULL)) {
                DWORD error = GetLastError();
                /* [2026-11-21] 管道满（`ERROR_NO_DATA`）是**背压**不是错误 ⇒
                 * 不进 `lastError`（同读侧口径）。 */
                /* 同上：管道错误码用负值标记来源（Win32 码 vs errno）。 */
                if (error != ERROR_NO_DATA) p->lastError = -((int)error);
                if (outErr) *outErr = (int)error;
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
        /* [故障注入 2026-11-21] **写路径注入点（UDP 分支）**：注入时
         * **不发起 send**（真实写失败时数据不会上线）→ 结果被伪造成
         * 「send 失败 + 注入的错误码」→ 与真实失败走**同一个收口**
         * （`xw32_sendFailUdp`）→ `WSAEWOULDBLOCK` 同样按背压返回
         * （不进 `lastError`），其它码同样记 `lastError` + 出错。 */
        {
            int injected = xw32_faultTake(XDeviceNetworkFaultStep_Write);
            if (injected != 0) return xw32_sendFailUdp(p, injected, outErr);
        }
        if (destAddr) {
            struct sockaddr_storage dest;
            int destLen = 0;
            xw32_mapV4Dest(p->socket, destAddr, destPort, &dest, &destLen);
            sent = sendto(p->socket, (const char*)data, (int)len, 0,
                          (const struct sockaddr*)&dest, destLen);
        }
        else {
            sent = send(p->socket, (const char*)data, (int)len, 0);
        }
        if (sent == SOCKET_ERROR) {
            return xw32_sendFailUdp(p, WSAGetLastError(), outErr);
        }
        return (int64_t)sent;
    }

    /* TCP：可能短写；把已写的量如实返回，剩余由 ring buffer 续发。 */
    {
        int sent = SOCKET_ERROR;
        int error = 0;
        int64_t chunk = len;

        /* [M2 2026-11-21 复审] **按 Qt 口径给每次 send 封顶 49152 字节**
         * （`qnativesocketengine_win.cpp` 的 `nativeWrite`：
         *  `bytesToSend = qMin<qint64>(49152, len - ret);`）。
         * 本仓原先一次 `send` 整个 len（可能 MB 级）⇒ 单次系统调用要搬上兆
         * 数据，尾延迟不可控，且大块更容易撞 `WSAENOBUFS`。 */
        if (chunk > XNETWORK_SEND_NOBUFS_MAX_CHUNK)
            chunk = XNETWORK_SEND_NOBUFS_MAX_CHUNK;

        /* [故障注入 2026-11-21] **写路径注入点（TCP 分支）**：注入时
         * **不发起 send**（真实写失败时数据不会上线），把结果伪造成
         * 「send 返回 SOCKET_ERROR + 注入的错误码」→ 下面两段判据
         * **一字不改**地处理它 → `WSAEWOULDBLOCK` / `WSAENOBUFS` 同样按
         * **背压**返回（不进 `lastError`），其它码同样记 `lastError` +
         * 出错 ⇒ **注入错误与真实错误同路**。 */
        {
            int injected = xw32_faultTake(XDeviceNetworkFaultStep_Write);
            if (injected != 0) {
                sent = SOCKET_ERROR;
                error = injected;
            }
            else {
                sent = send(p->socket, (const char*)data, (int)chunk, 0);
            }
        }
        if (sent == SOCKET_ERROR) {
            /* `error` 非 0 ⇒ 是注入的码（不再覆盖）；为 0 才是真实错误。 */
            if (error == 0) error = WSAGetLastError();
            /* [A1/M2] `WSAENOBUFS`：Qt 口径是「**下一轮**用更小的块再试」
             * （Qt 注释："just go on, the next loop run we will try a smaller
             * number"）⇒ **不在同一轮里忙等重试**（忙等既烧 CPU 又放大尾
             * 延迟）⇒ 这里按**背压**返回，由上层写环在下一轮续发
             * （续发块本来就 ≤ 写环槽大小，比 49152 更小）。 */
            if (error == WSAENOBUFS) {
                if (outErr) *outErr = error;
                return XNETWORK_IO_WOULD_BLOCK;
            }
        }

        if (sent == SOCKET_ERROR) {
            /* [2026-11-21 复审 P1] **背压不进错误通道**：`WSAEWOULDBLOCK` 与
             * `WSAENOBUFS` 都是「暂时写不出去」，不是错误 ⇒ 不得写进
             * `lastError`（否则它会一直留着，框架按它映射出
             * TemporaryError，把真正的关闭原因顶掉——实测：对端优雅关闭后
             * errorOccurred 报成 22=TemporaryError 而不是
             * 1=RemoteHostClosedError）。UDP 分支与读侧已按此口径处理，
             * 这里（TCP 直写分支）原先漏了。 */
            if (error == WSAEWOULDBLOCK) {
                if (outErr) *outErr = error;
                return XNETWORK_IO_WOULD_BLOCK;
            }
            /* ENOBUFS 已在上面第一处判据按背压返回；此处不会再到（保留
             * 注释仅为说明：ENOBUFS 与 WOULDBLOCK 同属背压，都不判死、
             * 都不进错误通道）。 */
            p->lastError = error;
            if (outErr) *outErr = error;
            return XNETWORK_IO_ERROR;
        }
        return (int64_t)sent;
    }
}

/* =========================================================================
 * 读 / 写推进（单缓冲 + 续发契约）
 * ========================================================================= */

/**
 * @brief 从内核读一批并投递读完成（对标 Qt `QAbstractSocketPrivate::readFromSocket`）。
 * @param p     上下文。
 * @param isUdp 是否数据报。
 * @return 本次是否产生了给上层的读完成投递。
 * @details **一次就绪事件内的读推进**（[A3 2026-11-21] 订正）：
 *
 *          Qt 的**引擎层**（`QNativeSocketEngine::read`）一次一读，但 Qt 的
 *          **上层**（`QAbstractSocketPrivate::readFromSocket()`，
 *          qabstractsocket.cpp:1176-1180）**循环读直到返回 -2（EAGAIN）**
 *          ⇒ 「末批数据 + FIN」在**同一轮**里就看到 EOF。
 *          本仓后端把引擎层与投递合在一起 ⇒ 旧实现一次就绪只 recv 一次，
 *          EOF 只能等上层 `ContinueRead` 触发的下一次 recv 才暴露
 *          ⇒ **对端断开的察觉依赖上层是否继续读**。
 *          现在：**在一次就绪事件内把内核数据读到 `WSAEWOULDBLOCK` 为止**
 *          （或读到读缓冲满），使 EOF 与末批数据同轮可见。
 *
 *          **但只投递一次读完成**（本仓契约：`readBuffer` / `readFinishedBytes`
 *          各只有一份，见下面的单缓冲契约注）。因此这里的实现形态是：
 *          「**把内核数据尽量读进内部读缓冲，但只投递一次读完成**」。
 *
 *          - 直接把数据读进 `readBuffer`（框架的设备层读缓冲），投递 `Read(N)`；
 *            上层在事件处理里把数据拷进自己的 ring buffer 后发 `ContinueRead`。
 *          - 读到 0 字节（EOF）⇒ 投递 `Read(0)`，由框架的 PEEK 复核裁定
 *            真 FIN/假 EOF。
 *          - `EWOULDBLOCK` ⇒ 本轮无数据，直接返回（**仍已调用 recv**）。
 *          - **同一轮既读到数据又读到 EOF** ⇒ 数据照常投递，EOF **推迟**：
 *            置 `eofPending`，等上层把这一批取走（ContinueRead）后再收口
 *            关闭事件（Qt 的 `canCloseNotification()`：有数据就先发
 *            readyRead、把关闭推到下一回合，否则会丢最后一帧数据）。
 *
 * @note [单缓冲契约 2026-11-04 恢复] 上一批数据已投递、上层尚未取走时，
 *       **不得**再次 recv：`p->readBuffer` 与 `p->readFinishedBytes` 都只有
 *       一份，再读一次会把未消费的数据覆盖掉，同时产生第二个 Read 完成
 *       ——上层会按同一份缓冲重复拷贝（实测 XLink 回显 17 字节被交付两次、
 *       请求帧被重复解析）。`ContinueRead` 是唯一的解除点（上层拷进自己的
 *       ring buffer 之后才调用），与 IOCP 后端的 readPending 契约逐位一致。
 * @note UDP **一轮只读一个数据报**：多个数据报拼进同一块缓冲会丢掉报边界
 *       （来源地址也只保留最后一个）。
 */
static bool xw32_armRead(XDeviceNetworkContextWin32Select* p, bool isUdp)
{
    size_t total = 0;
    bool eof = false;
    bool hardError = false;

    if (!p || !p->base.m_connected || !p->autoRead) return false;
    /* [2026-11-04 单缓冲契约] 上一批尚未被上层取走 ⇒ 本轮不读。 */
    if (p->readPending) return false;
    /* 已判定对端关闭/硬错误：不再重复 recv，也不再重复投递 0 字节完成。 */
    if (p->closed) return false;

    /* [A3] 上一轮已观察到 EOF、但同轮还交付了数据 ⇒ 本轮直接收口。
     * （旧实现没有这个状态：EOF 只在「下一次 recv 恰好返 0」时才被看见，
     *   而那一轮可能因为上层不再读而永远不来。） */
    if (p->eofPending || p->errPending) {
        bool wasErr = p->errPending;
        p->eofPending = false;
        p->errPending = false;
        p->readFinishedBytes = 0;
        p->closed = true;
        xw32_disarmSocket(p);
        if (wasErr) {
            xw32_postSockClose(p);       /* 硬错误：直投关闭事件 */
        }
        else if (p->namedPipe != INVALID_HANDLE_VALUE) {
            /* [A9] 命名管道无法 MSG_PEEK 复核 ⇒ 直投关闭事件。 */
            xw32_postSockClose(p);
        }
        else {
            xw32_postReady(p, XSocketActType_Read, 0);  /* 0 字节完成 → 框架 PEEK 复核 */
        }
        return true;
    }

    /* [A3] 一次就绪事件内读到 would-block 为止（或读缓冲满）。 */
    while (total < (size_t)XNETWORK_READ_BUFFER_SIZE) {
        int got = xw32_recvOnceInto(p, p->readBuffer + total,
                                    (int)((size_t)XNETWORK_READ_BUFFER_SIZE - total),
                                    isUdp);
        if (got > 0) {
            total += (size_t)got;
            if (isUdp) break;      /* UDP：一轮只读一个数据报（保报边界） */
            /* [管道 2026-11-21] 命名管道是**同步阻塞**句柄：再读一次会在
             * 没有数据时**卡住整个事件循环** ⇒ 管道一轮只读一次。 */
            if (p->namedPipe != INVALID_HANDLE_VALUE) break;
            continue;
        }
        if (got == 0) {
            /* [P10 2026-11-21 复审] **UDP 的 0 字节不是 EOF**：UDP 上
             * `recv` 返 0 表示「收到一个**零长数据报**」，不是对端关闭
             * （Qt 的 `nativeRead` 只在 TCP/SCTP 上把 0 当 EOF：
             *  `if (readBytes == 0 && (socketType == TcpSocket || ...))`）。
             * 旧实现把 UDP 的零长数据报当成 EOF ⇒ 会把 UDP 连接误判为死。 */
            if (isUdp) break;
            eof = true;
            break;
        }
        if (got == XNETWORK_IO_WOULD_BLOCK) break;
        hardError = true;
        break;
    }

    if (total > 0) {
        p->readFinishedBytes = total;
        p->readReadyCount++;
        p->readPending = true;   /* 缓冲被占用，等 ContinueRead 解除。 */
        xw32_postReady(p, XSocketActType_Read, (uint32_t)total);
        /* 同一轮既交付了数据又看到了 EOF/硬错误 ⇒ **关闭推迟**到这一批
         * 被上层取走之后（Qt 的 canCloseNotification 口径）。 */
        if (eof) p->eofPending = true;
        if (hardError) p->errPending = true;
        return true;
    }
    if (eof) {
        /* 对端关闭：投递 0 字节读完成，让框架的 PEEK 复核下结论。 */
        p->readFinishedBytes = 0;
        p->closed = true;
        /* [2026-11-06 根修：死句柄即刻摘出轮询表] EOF 是水平触发的，句柄
         * 留在 WSA 轮询表里 ⇒ 之后每圈仍对它 select 并重投 Read(0)（事件
         * 洪水），且实测病态死句柄上的 select 会**永久悬挂**——主线程冻结
         * 在 pollWsaEvents 里，全局时间轮停摆 ⇒ 全部 Coarse 定时器死
         * （XLink pump/reap/静默判线/485 总线轮询全冻结：38 路僵尸会话、
         * 界面 "--"、登录全拒，详见 Src/XCore/XProtocol/XLink/
         * XLink服务端会话泄漏与事件环挂死诊断_20261106.md）。
         * closed 是终态（全库无"假 EOF 复活"路径），此刻摘表只动环形表
         * 项、不关 socket（生命周期仍归持有者），安全；后续正式 close 里
         * 的 disarm 重复摘为幂等空操作。 */
        xw32_disarmSocket(p);
        if (p->namedPipe != INVALID_HANDLE_VALUE) {
            /* [A9] 命名管道：没有 MSG_PEEK 复核 ⇒ 直投关闭事件。 */
            xw32_postSockClose(p);
        }
        else {
            xw32_postReady(p, XSocketActType_Read, 0);
        }
        return true;
    }
    if (hardError) {
        /* 硬错误（非 RST/ABORT）：**直接投递关闭事件**，不借「0 字节读完成」
         * 那条路——框架的假 EOF 复核对**未知错误码**保守按「对端健在」处理
         * ⇒ 0 字节完成会被整条丢弃（见 xw32_postSockClose 的注）。
         * [B1] 读侧 RST/ABORT 已在 `xw32_recvOnceInto` 里归一成 0 字节，
         *       走上面的 EOF 分支（Qt 口径）。 */
        p->closed = true;
        xw32_disarmSocket(p);   /* [2026-11-06 根修] 同上：死句柄摘出轮询表。 */
        p->readFinishedBytes = 0;
        xw32_postSockClose(p);
        return true;
    }
    return false;   /* EWOULDBLOCK：本轮无数据，等下次就绪。 */
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
        int err = 0;
        if (chunk == 0) break;
        sent = xw32_sendOnce(p, tempBuf, (int64_t)chunk, NULL, 0, isUdp, &err);
        if (sent == XNETWORK_IO_ERROR) {
            /* [A1] 写侧致命错误 ⇒ 判定连接已死（投递关闭事件），**绝不**把
             * 数据留在队列里假装「已排队」——那会让写通道静默永久卡死
             * （ring 无限增长、不报错、不报连接死）。 */
            if (!isUdp && xw32_isFatalWriteError(err)) {
                xw32_postFatalClose(p, err, "写续发");
                return;
            }
            /* [管道 2026-11-21] 管道错误码是 Win32 码：对端关管 ⇒ 判死。 */
            if (p->namedPipe != INVALID_HANDLE_VALUE &&
                (err == (int)ERROR_BROKEN_PIPE || err == (int)ERROR_PIPE_NOT_CONNECTED)) {
                xw32_postFatalClose(p, err, "管道写续发");
                return;
            }
            break;                    /* 瞬时：下一轮再来（数据留在队列）。 */
        }
        if (sent <= 0) break;          /* 背压：下一轮再来（数据留在队列）。 */
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
    p->probeArmed = false;
    if (p->socket != INVALID_SOCKET) {
        xw32_closeSocketTracked(p->socket);
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
    if (p->socket == INVALID_SOCKET) {
        p->lastError = WSAGetLastError();
        return 0;
    }
    /* [A7] 新 socket ⇒ 旧的事件关联作废，必须复位 probeArmed，否则本
     * 上下文「先 bind 再 connect」时新 socket 不会被重新关联事件对象 ⇒
     * 永不就绪。 */
    p->probeArmed = false;
    xw32_resetConnState(p);   /* [复审 P4/P6] 代内状态一并清零 */

    if (reuseAddr) {
        int opt = 1;
        setsockopt(p->socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    }
    /* [Qt 6.8 对齐] bind(Any) 走双栈：AF_INET6 + IPV6_V6ONLY=0 才能同时
     * 接收 IPv4/IPv6（Qt nativeBind 对 AnyIPProtocol 显式清 V6ONLY）。 */
    if (af == AF_INET6) {
        int ipv6only = 0;
        setsockopt(p->socket, IPPROTO_IPV6, IPV6_V6ONLY,
                   (char*)&ipv6only, sizeof(ipv6only));
    }

    addr2sa(address, port, &addrStorage, &addrLen);
    /* [故障注入 2026-11-21] **bind 路径注入点**：注入时**不发起真实 bind**
     * （真实 bind 失败时端口并没有被占住，注入必须保持同一语义），
     * 把结果伪造成「bind 失败 + 注入的错误码」→ 随后的收口**一字不改**
     * 地处理它（记 `lastError` + 关 socket + 返回 0）⇒ 框架层据此翻译出
     * AddressInUseError / SocketAddressNotAvailableError / SocketAccessError
     * （这几个映射**只在 bind 路径**上与 connect 路径不同，见
     * `socketErrorFromPlatform` 的 `bindPath` 参数）。 */
    {
        int injected = xw32_faultTake(XDeviceNetworkFaultStep_Bind);
        int bindFailed;
        int bindErr = injected;
        if (injected != 0) {
            bindFailed = 1;              /* 注入：绑定「失败」 */
        }
        else {
            bindFailed = (bind(p->socket, (struct sockaddr*)&addrStorage,
                               addrLen) == SOCKET_ERROR);
            if (bindFailed) bindErr = WSAGetLastError();
        }
        if (bindFailed) {
            p->lastError = bindErr;
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            return 0;
        }
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
     * 收数据。
     * [掩码订正 2026-11-21] 旧注释写「**先不给 FD_CONNECT**…后续若真发起
     * connect 会在 socketConnect 里重新绑定带 FD_CONNECT 的掩码」——
     * **与代码矛盾**：本后端的关联掩码是**全集**（`FD_READ|FD_ACCEPT|
     * FD_WRITE|FD_CONNECT|FD_CLOSE|FD_OOB`，见 `xw32_probeArm`），
     * 任何时候都不按角色裁剪（Qt 能按关注位动态改掩码是因为它可以重发
     * 关联；本后端**不能**——重发会挂在 AFD 端点锁上永久
     * 悬挂）。多出来的 FD_CONNECT 位在派发处按角色取舍：没有 connect
     * 在途时它不会置位。 */
    if (!xw32_armSocket(p)) {
        xw32_closeSocketTracked(p->socket);
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
    if (p->socket == INVALID_SOCKET) {
        p->lastError = WSAGetLastError();
        return false;
    }
    /* [A7] **凡替换 `p->socket` 之处一律复位 `probeArmed`**（并作废旧事件
     * 对象关联）：旧实现这里**没有**复位（对比 socketSetDescriptor /
     * serverSetDescriptor 都有）⇒ 若同一上下文先 bind 再 connect，新 socket
     * 不会被重新关联事件对象 ⇒ **永不就绪**（连接永远不完成）。 */
    p->probeArmed = false;
    xw32_resetConnState(p);   /* [复审 P4/P6] 代内状态一并清零 */

    /* 先绑本地任意端口：与 IOCP 后端一致（先 bind 让本地端口在 FD_CONNECT
     * 到达前就可查，且与 UDP 分支共用同一套端点语义）。 */
    {
        struct sockaddr_storage localAddr;
        int localLen = (af == AF_INET6) ? (int)sizeof(struct sockaddr_in6)
                                        : (int)sizeof(struct sockaddr_in);
        XMemset(&localAddr, 0, sizeof(localAddr));
        ((struct sockaddr*)&localAddr)->sa_family = (unsigned short)af;
        if (bind(p->socket, (struct sockaddr*)&localAddr, localLen) == SOCKET_ERROR) {
            p->lastError = WSAGetLastError();
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            return false;
        }
    }

    /* 事件绑定必须在 connect 之前完成：否则「立即成功」的 connect 没有事件
     * 源可投递，FD_CONNECT 会丢。
     * 掩码是**全集**（含 FD_CONNECT）：本后端不按角色裁剪（重发
     * 关联会挂在 AFD 端点锁上永久悬挂）。TCP 连接建立后
     * FD_CONNECT 不会再触发（一次性），不影响后续收发。 */
    if (!xw32_armSocket(p)) {
        xw32_closeSocketTracked(p->socket);
        p->socket = INVALID_SOCKET;
        return false;
    }

    sa2addr(&destAddr, &p->pendingPeerAddr, NULL);
    p->pendingPeerPort = port;

    /* [故障注入 2026-11-21] **connect 路径注入点**。
     * @details 与读/写不同：connect **不是原子操作**（有「在途」语义），
     *          所以处理方式分两种：
     *          · 注入 `WSAEWOULDBLOCK` ⇒ **照常发起真实 connect**（否则
     *            FD_CONNECT 永远不会来，连接会挂到超时），只把结果按
     *            「在途」处理 → 与真实 would-block 的时序一致（真实 connect
     *            若已成功，FD_CONNECT 随后到达 → 连接最终成立）；
     *          · 注入其它码 ⇒ **不发起真实 connect**（真实 connect 失败时
     *            对端同样看不到任何连接），把结果伪造成「connect 失败 +
     *            注入的错误码」。
     *          两种情况最终都交给**下面那段一字未改的收口**处理
     *          → `WSAEISCONN` 特判、`WSAEWOULDBLOCK` 在途、其它 ⇒
     *          记 `lastError` + 关 socket + 返回 false → **注入错误与真实
     *          失败同路**（框架层据此翻译出 ConnectionRefusedError /
     *          NetworkError / SocketAccessError / SocketResourceError）。 */
    {
        int injected = xw32_faultTake(XDeviceNetworkFaultStep_Connect);
        int connErr;
        if (injected != 0 && injected != WSAEWOULDBLOCK) {
            rc = SOCKET_ERROR;
            connErr = injected;
        }
        else {
            rc = connect(p->socket, (struct sockaddr*)&destAddr, destLen);
            connErr = (rc == SOCKET_ERROR) ? WSAGetLastError() : 0;
            if (injected == WSAEWOULDBLOCK) {
                /* 注入「在途」：真实 connect 保持发起（FD_CONNECT 需要它）。 */
                connErr = WSAEWOULDBLOCK;
                rc = SOCKET_ERROR;
            }
        }
        if (rc == SOCKET_ERROR) {
            /* [B3] Qt 口径：`WSAEISCONN` ⇒ **按「已连接」处理**
             * （`qnativesocketengine_win.cpp` 的 `setErrorFromWSAError`：
             * "connection already established" ⇒ `setConnectedState()`）。
             * 旧实现把「除 WSAEWOULDBLOCK 外的所有 connect 错误」当硬失败
             * ⇒ 关 socket、返回 false。 */
            if (connErr == WSAEISCONN) {
                p->base.m_connected = true;
                p->connectPending = false;
                xw32_syncSocketEndpoints(&p->base);
                xw32_postReady(p, XSocketActType_Connect, 0);
                return true;
            }
            if (connErr != WSAEWOULDBLOCK) {
                p->lastError = connErr;
                xw32_disarmSocket(p);
                xw32_closeSocketTracked(p->socket);
                p->socket = INVALID_SOCKET;
                return false;
            }
            /* 连接在途：等 FD_CONNECT（事件环唤醒 → DispatchReady）。 */
            p->connectPending = true;
            return true;
        }
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
    DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;

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
     * 「上层可观察行为」上等价（该通道只服务 MySQL 等本地传输）。
     *
     * [2026-11-21] 等待模式取 **PIPE_NOWAIT**（非阻塞）：
     *   阻塞模式下 `ReadFile` 在没有数据时会**卡住整个事件循环** ⇒ 管道
     *   根本不能进「读环空时直取内核」那条路（TCP 那边是非阻塞套接字，
     *   直取不会卡）。改成 NOWAIT 后：没有数据时 `ReadFile` 立刻失败并给
     *   `ERROR_NO_DATA` ⇒ 后端翻译成「暂时读不到」（与套接字的
     *   `WSAEWOULDBLOCK` 同义）⇒ 直取路径可用，且不卡事件循环。 */
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
    xw32_resetConnState(p);   /* [复审 P4/P6] 代内状态一并清零 */
    /* [管道 2026-11-21] 显式置 `autoRead`（读推进入口 `xw32_armRead` 的闸门
     * 是 `!p->autoRead ⇒ 直接返回 false`）。
     * [复审 P12 2026-11-21 订正] 旧注释写「管道连通不经过置 autoRead 的两条
     * 路径 ⇒ autoRead 一直是 false」——**这个前提是错的**：`createContext`
     * 就已置 `autoRead = true`（:1271）⇒ 这一行本身是**冗余**（保留是为了
     * 不依赖「建上下文时的缺省」这一隐含前提）。
     * 管道真正读不到数据的真实原因是两条：**①** 派发入口对
     * `p->socket == INVALID_SOCKET` 直接 return（管道没有 socket）⇒ 管道
     * 永远进不了轮询表、永远不被派发；**②** 读环空直取路径把管道排除在外。 */
    p->autoRead = true;
    /* [管道 2026-11-21] 登记进就绪轮询表：管道没有 WSA 事件，靠事件环每轮
     * 回调 `XDeviceNetwork_win32_selectDispatchReady` → `xw32_dispatchPipe`
     * 用 PeekNamedPipe 探数据与断链。不登记 ⇒ 管道永远不会被派发（死通道）。 */
    if (!xw32_ringAddSockEvent(NULL, p->base.m_base.m_fd, (void*)p)) {
        CloseHandle(p->namedPipe);
        p->namedPipe = INVALID_HANDLE_VALUE;
        p->base.m_connected = false;
        return false;
    }
    return true;
}

void XDeviceNetwork_socketDisconnect(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;

    if (!priv) return;
    p = W32S(priv);

    xw32_disarmSocket(p);

    p->probeArmed = false;   /* 关联随 socket 一并作废。 */

    if (p->socket != INVALID_SOCKET) {
        xw32_closeSocketTracked(p->socket);
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
    /* [A5/A3] 事件位缓存与「推迟的关闭」标志一并清零：新的一代连接不能
     * 继承上一代的 FD_CLOSE / EOF 状态。 */
    xw32_resetConnState(p);   /* [复审 P4/P6] 代内状态一并清零 */
}

/* =========================================================================
 * 读 / 写
 * ========================================================================= */

/**
 * @brief 读接口（异步契约）。
 * @details 数据搬运主要发生在就绪分派里（`DispatchReady` 收到 `FD_READ` 时
 *          同步 recv 进 `readBuffer`），所以本函数与 IOCP 后端一样：优先把
 *          上层 ring buffer 里已到的数据交给调用方，返回 0 表示本轮无新增。
 * @note 与 IOCP 后端的刻意差异：没有「读挂死看门狗」。本后端不存在挂起的
 *       读请求。**`FD_READ` 是边沿（EDGE）语义，已由探针实测**
 *       （verify\fd_read_edge_probe.c：数据滞留时 rounds 2..N 不再报
 *       FD_READ）⇒ 本后端不依赖重报：`xw32_armRead` 一次就绪把内核抽干，
 *       下一批由 `ContinueRead` 触发（见 A3 注），且 FD_READ 位必须常驻。
 *
 * [2026-11-18 根修] 读环空时**允许直接从内核取一次**（见下）。
 *          本后端是纯轮询模型：没有「在飞的读请求」，数据在被轮询到之前
 *          一直躺在内核缓冲里；而框架的 bytesAvailable() 会把 FIONREAD
 *          计入（§B15 口径：内核待读、尚未被任何一次异步读交付的字节），
 *          上层据此 read() 时读环里还没有这些字节 ⇒ 读回 0（伪可读）。
 *          IOCP 后端没有这个问题：它的重叠读一直在飞，内核一收到数据就
 *          拷进用户缓冲，FIONREAD 随即归零 ⇒ 两端「可读」的观测口径一致。
 *          直取的前提是「读环空 **且** 平台侧没有已投递、尚未被上层取走
 *          的那一批」，保证：① 不会抢在平台侧那一批前面（字节序不乱）；
 *          ② 不会与平台侧那一批重复投递；③ 命名管道（同步 ReadFile 会
 *          阻塞事件循环）与监听套接字不参与。
 */
int64_t XDeviceNetwork_socketRead(XFd xfd, void* buf, int64_t len,
    XDeviceNetworkSocketType sockType, void* ringBuffer)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    int64_t got;

    if (!priv || !buf || len <= 0) return -1;
    p = W32S(priv);

    if (ringBuffer) {
        XRingBuffer* rb = (XRingBuffer*)ringBuffer;
        size_t available = XRingBuffer_available(rb);
        if (available > 0) {
            size_t toRead = (len < (int64_t)available) ? (size_t)len : available;
            return (int64_t)XRingBuffer_read(rb, buf, toRead);
        }
    }

    /* 读环空：直接从内核取一次（见函数头注）。平台侧还有一批已投递但上层
     * 尚未取走时**不能**直取，否则会抢在那一批前面（字节序乱）。
     *
     * [管道 2026-11-21] 命名管道**也**走这条直取路径：管道句柄已置
     * `PIPE_NOWAIT`（非阻塞），没有数据时 `ReadFile` 立刻返回
     * `ERROR_NO_DATA` ⇒ 不会卡住事件循环。旧实现把管道排除在这条路之外，
     * 加上「派发入口对 p->socket==INVALID_SOCKET 直接 return」，管道这条
     * 通道**从来就收不到任何数据**（死代码）。 */
    if (!p->isServer && !p->closed &&
        !p->readPending && p->base.m_connected && p->autoRead) {
        got = xw32_recvOnceInto(p, (char*)buf, (int)len,
                                 sockType == XDeviceNetwork_Udp);
        if (got > 0) return got;
        /* [A2 2026-11-21] **EOF（recv 返 0）不能吞**：旧实现在这里只
         * `return 0`，既不置 `closed`、也不投递关闭事件 ⇒ 若此时 FD_CLOSE
         * 位已被上一轮取位取走，该 EOF **永久丢失**，
         * 连接停留在「已连接」（直到 6 秒静默超时才拆链）。
         * 现在走**与 `xw32_armRead` 的 0 字节分支完全相同**的收口：
         * 置 closed + 摘表 + 投递 0 字节读完成（框架 PEEK 复核 ⇒ 关闭事件）。 */
        if (got == 0) {
            p->readFinishedBytes = 0;
            p->closed = true;
            xw32_disarmSocket(p);
            if (p->namedPipe != INVALID_HANDLE_VALUE) {
                /* [管道 2026-11-21] 管道**不能**走「0 字节读完成 → 框架
                 * MSG_PEEK 复核」那条路（复核用 ioctlsocket，对管道句柄必
                 * 失败 ⇒ 框架保守按「对端健在」⇒ 关闭事件被整条丢弃）。
                 * 与 `xw32_armRead` 的 EOF 分支同款：直投关闭事件。 */
                xw32_postSockClose(p);
            }
            else {
                xw32_postReady(p, XSocketActType_Read, 0);
            }
            return 0;
        }
        /* EWOULDBLOCK / 硬错误都按「本轮无数据」上报：硬错误由就绪派发
         * 那条路负责（读侧 RST/ABORT 已按 Qt 口径归一成 0 字节 ⇒ 也走
         * 上面那条收口）。 */
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
    int err = 0;

    if (!priv || !buf || len <= 0) return -1;
    p = W32S(priv);
    if (p->socket == INVALID_SOCKET && p->namedPipe == INVALID_HANDLE_VALUE)
        return -1;

    isUdp = (sockType == XDeviceNetwork_Udp);

    /* [管道 2026-11-21] 记住上层写环：管道没有写就绪事件，续发要靠它。 */
    if (ringBuffer) p->writeRing = (XRingBuffer*)ringBuffer;

    /* 先把上一轮积压推出去，避免「新写直接把旧积压饿死」。 */
    if (ringBuffer)
        xw32_drainWrite(p, (XRingBuffer*)ringBuffer, isUdp);

    written = xw32_sendOnce(p, buf, len, destAddr, destPort, isUdp, &err);

    if (written == XNETWORK_IO_WOULD_BLOCK) {
        /* 背压：有 ring buffer 就整包入队并回报「受理」；否则如实失败。 */
        if (!ringBuffer || isUdp) return -1;
        /* [Qt 对齐] 入队即代表「写缓冲非空」：必须置 writePending，否则
         * DispatchReady 不会投递 Write 完成，队列里的数据永远等不到续发
         * （Qt 由 writeToSocket 在缓冲非空时使能写通知，语义等价）。 */
        p->writePending = true;
        return (int64_t)XRingBuffer_write((XRingBuffer*)ringBuffer, buf, (size_t)len);
    }
    if (written == XNETWORK_IO_ERROR) {
        /* [A1 2026-11-21] **致命写错误绝不能吞成「已排队」**。
         * 旧实现在这里把错误吞掉、把数据留在 ring 里并回报「受理」⇒
         * 下一轮 `xw32_drainWrite` 又撞同一个错误、在 `sent <= 0` 处停下
         * ⇒ **不报错、不报连接死、ring 无限增长** ⇒ 写通道静默永久卡死。
         * 现在：致命错误 ⇒ 投递关闭事件 + 置连接死 + 如实返回 -1。 */
        if (!isUdp && xw32_isFatalWriteError(err)) {
            xw32_postFatalClose(p, err, "写提交");
            return -1;
        }
        /* [管道 2026-11-21] 命名管道的错误码是 **Win32 码**（不是 WSA 码）：
         * 对端关管 ⇒ `ERROR_BROKEN_PIPE`(109) / `ERROR_PIPE_NOT_CONNECTED`(233)
         * ⇒ 与 TCP 的 RST 同语义 ⇒ **判死**。旧实现把它当「瞬时错误」入队
         * 回报「受理」⇒ 数据烂在队列里、对端已关管却毫无痕迹（静默失败）。 */
        if (p->namedPipe != INVALID_HANDLE_VALUE &&
            (err == (int)ERROR_BROKEN_PIPE || err == (int)ERROR_PIPE_NOT_CONNECTED)) {
            xw32_postFatalClose(p, err, "管道写提交");
            return -1;
        }
        if (ringBuffer && !isUdp) {
            /* 瞬时错误：照旧入队（背压语义）。 */
            p->writePending = true;
            return (int64_t)XRingBuffer_write((XRingBuffer*)ringBuffer, buf, (size_t)len);
        }
        return -1;
    }

    p->writeFinishedBytes = (size_t)written;

    /* 短写：剩余部分入 ring buffer，等 FD_WRITE 续发（不丢半帧）。 */
    if (written < len) {
        p->writePending = true;
        if (!ringBuffer) return written;        /* 无缓冲：如实上报短写 */
        {
            size_t queued = XRingBuffer_write((XRingBuffer*)ringBuffer,
                                              (const char*)buf + written,
                                              (size_t)(len - written));
            /* [P11 2026-11-21 复审] 写环可能**只装下一部分**（内存不足）
             * ⇒ 如实上报「实际受理量」：宣称全部受理会让没入队的那部分
             * **静默丢失**，而调用方以为写成功（与本轮「写侧不许吞错」的主张
             * 自相矛盾）。 */
            return written + (int64_t)queued;
        }
    }
    else if (ringBuffer) {
        /* [M1 2026-11-21 复审] **写关注标志一律以写环的实际占用为准**。
         *
         * 旧写法：直写整包成功就无条件 `writePending = false`。但本次直写
         * **之前**先做了一次 `drainWrite`（背压时会停下、环里还剩数据）⇒
         * 于是出现「**环非空但 writePending=false**」的状态：
         *   · 自保活 drain 的条件是 `writePending && writeRing` ⇒ 不成立；
         *   · `FD_WRITE` 分支的 `if (p->writePending)` ⇒ 不成立；
         *   · `ContinueWrite` 只在 Write 事件处理器里发 ⇒ 没人再触发 drain；
         * ⇒ **尾巴数据永远发不出去**：不报错、不推进、只能等上层超时
         *   （现场表现：大帧的尾巴永远收不到回应）。
         * ⇒ 收尾统一按环的实际占用重算，与 `xw32_drainWrite` 的口径一致。 */
        p->writePending = (XRingBuffer_available((XRingBuffer*)ringBuffer) > 0);
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

    /* [管道 2026-11-21] 同上：记住写环，管道侧的就绪派发要用它续发。 */
    p->writeRing = (XRingBuffer*)ringBuffer;

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
 * @details 真实形态（**2026-11-21 订正**）：不是 `select`，而是
 *          「ring 消息槽取位」（见 `xw32_pollEvents`）。本函数做四件事：
 *            1. **一次**取回该 socket 累计的全部事件位并缓存进
 *               `p->pendingEvents`；
 *            2. 连接完成判定（`FD_CONNECT` / `FD_WRITE` / `FD_CLOSE` ⇒ 查
 *               `SO_ERROR`）；
 *            3. 可写 ⇒ 投递 `Write`（仅当有待发数据）；
 *            4. 可读 / 关闭 ⇒ `xw32_armRead` 一次读到 would-block 为止，
 *               **只投递一次**读完成；关闭是**独立于可读**的事件。
 *
 *          **没有需要手工复位的状态**：事件位由 ring 的 WndProc 累积进
 *          消息槽、`xw32_pollEvents` 取走；probeArmed 只在换 socket/断开时
 *          复位（一次关联）。历史教训仍在：早期把事件对象并进等待集
 *          出现过「从不置位」；候选2 的每轮重发关联又暴露「重注册悬挂」
 *          ——两头都踩过，最终形态是「一次关联 + 消息槽取位」。
 *
 *          connect 完成判定：**显式映射 `FD_CONNECT`**（旧注释说
 *          「`select` 没有 `FD_CONNECT` 对应位」——那是 select 时代的理由），
 *          并按 Qt 口径（`nativeConnect`
 *          L605-642）用 `getsockopt(SO_ERROR)` 取真实结果；**读出后必须
 *          显式清零**（Qt 注释：MSDN 说 getsockopt 会清，实际上没清，
 *          会污染后续所有 `WSAConnect`）。
 * @note **不用 XFd 反查属主**：XFd 是 `XFixedPool` 的池槽位号，连接关闭
 *       后会被下一个连接复用。按 fd 反查会把就绪派给刚复用该槽位的新连接
 *       （串包/丢包）；Qt 同样是用 notifier 的 `wp` 直接寻址。
 */
/**
 * @brief 命名管道通道的就绪推进（**没有 WSA 事件**，改用 PeekNamedPipe 探）。
 * @param p 上下文（`p->namedPipe` 必须有效）。
 * @details [管道 2026-11-21] 命名管道不是套接字 ⇒ 没有 WSA 事件
 *          可等 ⇒ 本后端原先在派发入口就 `if (p->socket == INVALID_SOCKET)
 *          return;` 把管道**整个跳过** ⇒ 管道永远进不了读环 ⇒
 *          「命名管道」这条通道在本后端上**从来就没通过**（死代码）。
 *
 *          现在改成：管道也登记进就绪轮询表（`xw32_ringAddSockEvent`），
 *          事件环每轮回调一次本函数 ⇒ 用 `PeekNamedPipe` 探一次：
 *            ① 有数据 ⇒ 读一次（管道是同步阻塞句柄，**一轮只读一次**，
 *               再读会卡住事件循环）；
 *            ② `ERROR_BROKEN_PIPE` / `ERROR_PIPE_NOT_CONNECTED`
 *               ⇒ 对端已关管 ⇒ 与 TCP FIN 同语义 ⇒ 投递关闭事件。
 */
static void xw32_dispatchPipe(XDeviceNetworkContextWin32Select* p)
{
    DWORD avail = 0;

    if (!p || p->namedPipe == INVALID_HANDLE_VALUE) return;
    if (p->closed) return;

    if (!PeekNamedPipe(p->namedPipe, NULL, 0, NULL, &avail, NULL)) {
        DWORD err = GetLastError();
        /* [M3 2026-11-21 复审] 管道错误码一律用**负值**标记来源（Win32 码，
         * 不是 WSA/errno）——与 `xw32_recvOnceInto` / `xw32_sendOnce` 一致。
         * 旧写法写的是正数 ⇒ 框架按 errno 表解释（109 不在表内）⇒ Unknown
         * ⇒ 对端关管被报成 `NetworkError` 而不是 `RemoteHostClosedError`
         * ⇒ MQTT/S7/Modbus 三个「对端关闭」守卫在**管道**场景全部失效。 */
        p->lastError = -((int)err);
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
            /* 对端关管 ⇒ EOF ⇒ 关闭事件（管道没有 MSG_PEEK 复核路径）。 */
            p->readFinishedBytes = 0;
            p->closed = true;
            xw32_disarmSocket(p);
            xw32_postSockClose(p);
        }
        return;   /* 其它错误：本轮无数据，等下一轮再探。 */
    }

    if (avail > 0) {
        /* 有数据可读：读一次。`xw32_armRead` 内部对管道只读一次。 */
        (void)xw32_armRead(p, false);
    }

    /* [管道 2026-11-21] 写侧续发：管道**没有写就绪事件** ⇒ 写环里的数据
     * 必须在这里主动推（套接字那边由 FD_WRITE 驱动）。
     * 管道缓冲写满时 `WriteFile` 返回 `ERROR_NO_DATA` ⇒ 后端把它当
     * **背压**（数据留在写环）⇒ 对端读走一些后，本函数下一轮再推。 */
    if (p->writePending && p->writeRing) {
        xw32_drainWrite(p, p->writeRing, false);
    }
}

void XDeviceNetwork_win32_selectDispatchReady(void* owner)
{
    XDeviceNetworkContextWin32Select* p = (XDeviceNetworkContextWin32Select*)owner;
    uint32_t ev;
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
    if (p->socket == INVALID_SOCKET) {
        /* [管道 2026-11-21] 命名管道没有 WSA 事件，走 PeekNamedPipe 探。 */
        if (p->namedPipe != INVALID_HANDLE_VALUE) {
            xw32_dispatchPipe(p);
        }
        return;
    }
    /* [2026-11-06 根修] 已判 closed（对端关闭/硬错误）的套接字**不再参与
     * 轮询**：EOF 水平触发，每圈仍会对死句柄 select 并经 x 分支重投
     * Read(0)（事件洪水）；病态死句柄上的 select 更会永久悬挂拖死整个
     * 事件循环（机制见 xw32_armRead 根修注与 XLink 诊断报告）。armRead/
     * x 分支的 closed 处已同步摘表，此处是冗余护栏（摘表幂等）。 */
    if (p->closed) return;

    isUdp = (p->base.m_socketType == XDeviceNetwork_Udp);

    /* [A5] 本轮**只**取一次位（xw32_pollEvents 从 ring 消息槽取）：取回全部
     * 事件位并缓存。各分支读缓存，绝不各自再取一次（takeMsgEvents 取走即清，
     * 第二次取是空的 ⇒ 那一轮到达的读事件被吞）。 */
    /* [写侧续发自保活 2026-11-21] **写环的续发必须每轮无条件服务** ⇒ 放在
     * 「本轮有没有事件位」的判定**之前**。
     * 放后面是错的：`FD_WRITE` 是边沿语义（投递一次后 AFD 不一定重报），
     * 于是「无事件位 → 直接 return」会把写环的续发一并跳过 ⇒ 队列里的
     * 数据永远推不出去（实测：1MB 只发出 49152，其余 999424 卡死 30s）。
     */
    /* [写侧续发自保活 2026-11-21] **写环的续发不依赖 FD_WRITE 事件**。
     *
     * 根因：WSA 的 `FD_WRITE` 是**边沿**语义 —— 投递一次之后，
     * 若「可写」这个**条件**仍然成立，AFD **不一定**再报一次 ⇒ 只靠事件
     * 驱动的写环，数据一旦进了队列就**永远推不出去**。
     * 实测（1MB 写入）：直写发出 49152 字节，其余 **999424 字节卡在写环里**，
     * 服务端只收到 49152 ⇒ 传输中断。
     * 这条路径此前从未被触发过 —— 旧实现一次 `send` 就把整个缓冲发完了，
     * 短写/续发是**死代码**，缺陷被掩盖。
     *
     * Qt 的口径（`QAbstractSocketPrivate::writeToSocket`）：**写缓冲非空 ⇒
     * 使能写通知；写完 ⇒ 关掉写通知** ⇒ 等价于「**由持有写缓冲的一方自保活**」。
     * 本后端持有写环指针（`p->writeRing`，由 `socketWrite`/`socketContinueWrite`
     * 记下），每轮 poll 直接推一次 ⇒ 不依赖事件边沿。
     */
    if (p->writePending && p->writeRing) {
        size_t before = XRingBuffer_available(p->writeRing);
        xw32_drainWrite(p, p->writeRing, isUdp);
        {
            size_t after = XRingBuffer_available(p->writeRing);
            if (after < before) {
                /* 真的写出去了 ⇒ 上报一次写完成（上层据此做 bytesWritten 对账）。
                 * 事件的「字节数」用**本轮写出的量**，避免上层读到陈旧的
                 * `writeFinishedBytes`（历史 bug：17 项文件传输进度对账失败）。 */
                p->writeFinishedBytes = (size_t)(before - after);
                xw32_postReady(p, XSocketActType_Write, (uint32_t)(before - after));
            }
        }
    }

    ev = xw32_pollEvents(p);
    if (ev == 0) return;

    /* --- 连接完成判定（FD_CONNECT / FD_WRITE / FD_CLOSE ⇒ 查 SO_ERROR）--- */
    if (p->connectPending && (ev & (FD_CONNECT | FD_WRITE | FD_CLOSE))) {
        int soError = 0;
        int soLen = (int)sizeof(soError);
        bool ok = false;
        int failCode = 0;

        xw32_consumeEvents(p, FD_CONNECT);
        if (getsockopt(p->socket, SOL_SOCKET, SO_ERROR,
                       (char*)&soError, &soLen) == 0) {
            ok = (soError == 0);
            if (!ok) failCode = soError;
        }
        else {
            failCode = WSAGetLastError();
            if (failCode == 0) failCode = WSAECONNABORTED;
        }
        /* [B2] SO_ERROR 读出后必须**显式清零**：MSDN 说 getsockopt 会清，
         * 实际上没清 ⇒ 会污染后续所有 WSAConnect（Qt 口径）。 */
        {
            int zero = 0;
            (void)setsockopt(p->socket, SOL_SOCKET, SO_ERROR,
                             (const char*)&zero, sizeof(zero));
        }

        if (ok) {
            p->base.m_connected = true;
            xw32_syncSocketEndpoints(&p->base);
            p->connectPending = false;
            xw32_postReady(p, XSocketActType_Connect, 0);
            /* 连接已成立：本轮继续走读写分支（缓存里的 FD_WRITE 仍有效）。 */
        }
        else {
            /* [A8] 连接失败：**摘表 + 关 socket**（与其它失败路径一致）。
             * 旧实现只改标志，不 disarm、不 closesocket ⇒ 该套接字继续占着
             * 环表槽位与一个 fd，且每轮仍被 `pollWsaEvents` 扫到（每轮两次
             * 系统调用），连接失败**无任何痕迹**。 */
            p->base.m_connected = false;
            p->connectPending = false;
            p->lastError = failCode;
            /* [复审 P6] socket 已被替换 ⇒ 代内的事件位/推迟关闭一并清零
             * （**保留 `lastError`**：它正是本次失败的证据，框架层要靠它
             * 映射出「连接被拒 / 主机不可达 / 超时」等具体错误码）。 */
            p->pendingEvents = 0;
            p->eofPending = false;
            p->errPending = false;
            xw32_disarmSocket(p);
            xw32_closeSocketTracked(p->socket);
            p->socket = INVALID_SOCKET;
            p->probeArmed = false;   /* [A7] socket 已换 ⇒ 旧关联作废 */
            xw32_postReady(p, XSocketActType_Connect, 0);
            return;
        }
    }

    if (!p->base.m_connected) return;

    if (p->isServer) {
        /* 监听套接字：只看「新连接到达」。 */
        if (ev & FD_ACCEPT) {
            xw32_consumeEvents(p, FD_ACCEPT);
            /* WSAAsyncSelect 的 FD_ACCEPT 是**边沿**消息：只在队列从空变
             * 非空那一刻投一次；accept 成功后由 serverAccept 主动补位
             * （见该函数）。 */
            xw32_postReady(p, XSocketActType_Accept, 0);
        }
        if (ev & FD_CLOSE) {
            /* [A6] 关闭是**独立**事件（不再是「不可读却报了异常」的兜底）。
             * [句柄复用 2026-11-21] **listen 忽略 FD_CLOSE**：closesocket 时
             * 内核投的最后一条 FD_CLOSE 消息会滞留窗口队列，SOCKET 句柄被
             * 新 socket 复用后毒消息落进新监听口的槽 => 监听口被误判死
             * （双服务端用例实测；mask 裁剪治不了已入队的旧消息）。listen
             * 的"死"只来自本地 serverClose 主动关闭，对端关闭对它无语义
             * => consume 后直接忽略，不判死。客户端 socket 仍走判死路径
             * （其 FD_CLOSE 有 recv(0)+PEEK 复核兜底）。 */
            xw32_consumeEvents(p, FD_CLOSE);
            return;
        }
        return;
    }

    if (ev & FD_WRITE) {
        xw32_consumeEvents(p, FD_WRITE);
        p->writeReadyCount++;
        /* [2026-11-04 Qt 6.8 对齐] 只有**存在待发数据**时才投递 Write 完成。
         * Qt 的写通知按需使能：`QAbstractSocketPrivate::writeToSocket` 仅在
         * writeBuffer 非空时 `setWriteNotificationEnabled(true)`
         * （qabsock.cpp:2443/2476），写完即关。原实现只要「报可写」就
         * 投递——TCP 连接建立后几乎恒可写，于是每次 poll 都产生一个 Write
         * 完成：①事件环被写事件淹没（实测单次 ddns 跑出 20 万条 poll/投递）；
         * ②`XAbstractSocket_event` 的 Write 分支用陈旧的 `writeFinishedBytes`
         * 反复 `bytesWritten`，XLink 大文件传输的进度对账据此错乱（17 项失败
         * 全在文件上传/下载）。`writePending` 由 `socketWrite` 短写与
         * `xw32_drainWrite` 共同维护，正是 Qt「writeBuffer 非空」的等价物。
         * 复位交给 `xw32_drainWrite`（排空后自然转 false），不在这里抢改。 */
        if (p->writePending)
            xw32_postReady(p, XSocketActType_Write, 0);
    }

    /* [A6 2026-11-21] **关闭是独立于「可读」的事件**（Qt 口径：
     * `QEvent::SockClose` 是独立类型；旧实现把 FD_CLOSE 同时塞进可读掩码
     * 与异常掩码 ⇒ `x == true` 必然蕴含 `r == true` ⇒ 异常分支**永不可达**，
     * 关闭只能靠「可读」顺带捎上）。现在 FD_CLOSE 自己就能触发一次
     * 「把内核残余数据取走」的读推进：
     *   - 残余数据 ⇒ 先投递 `Read(N)`，把关闭**推迟**到下一回合
     *     （Qt 的 `canCloseNotification()`：有数据就先发 readyRead、
     *     关闭推迟，否则会丢最后一帧数据——Windows 与 Unix 的行为差异）；
     *   - 没有残余数据 ⇒ recv 返 0 ⇒ 0 字节读完成 ⇒ 框架 PEEK 复核 ⇒
     *     关闭事件（这才是「对端断开立刻察觉」的机制，不依赖 6 秒静默超时）。
     */
    if (ev & (FD_READ | FD_CLOSE)) {
        if (xw32_armRead(p, isUdp)) {
            /* 已投递完成（数据 / EOF / 硬错误）。若本轮同时看到了 FD_CLOSE
             * 而又交付了数据，关闭已被 `xw32_armRead` 记在 `eofPending`/
             * `errPending` 上，等这一批被上层取走（ContinueRead）后收口。 */
            xw32_consumeEvents(p, FD_CLOSE);
            return;
        }
        /* 本轮取不到数据（平台侧还有一批已投递、尚未被上层取走）：此时
         * **不能**盲投 0 字节完成（框架的 PEEK 复核会看到内核里还有数据
         * ⇒ 判成假 EOF ⇒ 整条丢弃），也**不能**置 closed（那会让后续
         * armRead 直接返回、再也等不到 recv==0）。正确收口：等上层把在途
         * 那一批取走（ContinueRead）后，由 ContinueRead 触发的 armRead
         * 收到 recv==0 ⇒ 真 EOF ⇒ 0 字节完成 ⇒ 框架下发关闭事件。
         * ⇒ **FD_CLOSE 位保留在缓存里**，下一轮继续尝试。 */
        return;
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
    /* [候选3] 接手的是新句柄：旧关联（若有）属于旧 socket，作废重挂。 */
    p->probeArmed = false;
    /* [复审 P6] 上一代残留的事件位/推迟关闭/错误码会污染新一代
     * （残留的 FD_CLOSE 位会让新连接一上来就被判死）。 */
    xw32_resetConnState(p);

    /* 接管已有连接：绑事件对象（同时把 socket 置非阻塞）。accept 出来的
     * 套接字会继承监听套接字的事件关联，这里重新绑定会覆盖掉该继承关
     * 系，避免多连接共用一个事件对象。
     * 掩码用「已连接」口径（**不含 FD_CONNECT**）：这是已建好的连接，
     * 注册 FD_CONNECT 会因事件持久性立刻报一次假的「连接完成」。 */
    if (!xw32_armSocket(p)) {
        p->socket = INVALID_SOCKET;
        return false;
    }

    /* `state` 是调用方传入的 `XAbstractSocket_SocketState`。用枚举而非魔数
     * 判定（Qt：`setSocketDescriptor` 直接采用传入的 socketState）。 */
    p->base.m_connected = ((XAbstractSocket_SocketState)state
                           == XAbstractSocket_ConnectedState);
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
    /* [候选3] 新句柄接管：旧关联作废（同 socketSetDescriptor）。 */
    p->probeArmed = false;
    /* [复审 P6] 同上：代内状态一并清零。 */
    xw32_resetConnState(p);
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

/**
 * @brief 取该套接字最近一次失败的原始 OS 错误码。
 * @details [2026-11-21] 框架层用它把平台错误翻译成 Qt 口径的
 *          `XAbstractSocket_SocketError`。本后端在**每一个失败返回点**
 *          都更新了 `p->lastError`（读/写/connect/accept/bind），
 *          成功路径不更新 ⇒ 读到的是「最近一次失败」而不是「最近一次调用」。
 * @note 命名管道路径存的是 **Win32 错误码**（如 `ERROR_BROKEN_PIPE`=109），
 *       不是 WSA 码；框架层的映射函数按量级区分（≥10000 才是 WSA）。
 */
int XDeviceNetwork_socketLastError(XFd xfd)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    if (!priv) return 0;
    return W32S(priv)->lastError;
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

/**
 * @brief 内核待读字节数（Win32 WSAAsyncSelect 后端）。
 * @details 与 IOCP 后端（XDeviceNetwork_win32.c）逐行同义：Qt 6.8 Windows
 *          原生引擎以 `ioctlsocket(socket, FIONREAD, &bytesToRead)` 回答
 *          「引擎还能读到多少」；本文件的就绪判定本就依赖同一原语
 *          （文件头 :14 / :527 起的「GetOverlappedResult + FIONREAD 三态
 *          判定」注释），此处只把它开放成只读查询。
 * @note 命名管道不是套接字 ⇒ false；未建 socket ⇒ false。
 */
bool XDeviceNetwork_socketPendingReadBytes(XFd xfd, size_t* value)
{
    XDeviceNetworkContext* priv = (XDeviceNetworkContext*)XDevice_handle(xfd);
    XDeviceNetworkContextWin32Select* p;
    u_long pending = 0;
    if (!priv || !value) return false;
    p = W32S(priv);
    if (p->namedPipe != INVALID_HANDLE_VALUE) return false;
    if (p->socket == INVALID_SOCKET) return false;
    if (ioctlsocket(p->socket, FIONREAD, &pending) != 0) return false;
    /* [Qt 6.8 对齐·UDP 特例] `FIONREAD` 在 UDP 上报 1 时，可能是「一个
     * **0 长度**数据报」正在等待（Windows 对 UDP 的 FIONREAD 语义）⇒
     * 探一下：能 peek 到字节 ⇒ 是真数据（1 字节）；peek 失败（连接被重置）
     * ⇒ 那个数据报是空的 ⇒ 待读 0。
     * Qt 口径见 `qnativesocketengine_win.cpp` 的 `nativeBytesAvailable()`
     * （nbytes == 1 && socketType == UdpSocket 分支）。
     * 差异虽小（只影响「0 长度 UDP 数据报」这一种），但会让上层
     * `bytesAvailable()` 报 1 而实际读不到字节 ⇒ 归一掉。 */
    if (pending == 1 && p->base.m_socketType == XDeviceNetwork_Udp) {
        char probe = 0;
        struct sockaddr_storage peerAddr;
        int peerLen = (int)sizeof(peerAddr);
        int got = recvfrom(p->socket, &probe, 1, MSG_PEEK,
                           (struct sockaddr*)&peerAddr, &peerLen);
        if (got == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAECONNRESET && err != WSAENETRESET)
                return false;
            got = 0;
        }
        pending = (u_long)got;
    }
    *value = (pending > 0) ? (size_t)pending : (size_t)0;
    return true;
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
        xw32_closeSocketTracked(s);
        return -1;
    }

    if (listen(s, backlog > 0 ? backlog : SOMAXCONN) == SOCKET_ERROR) {
        xw32_closeSocketTracked(s);
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
        xw32_closeSocketTracked(s);
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
        xw32_closeSocketTracked(p->socket);
        p->socket = INVALID_SOCKET;
        p->isServer = false;
        p->base.m_connected = false;
    }
    else {
        /* 上层拒收的已接受句柄：所有权已转给它，这里只负责关。 */
        xw32_closeSocketTracked(socketToClose);
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

    p = W32S(priv);
    if (!p->isServer || p->socket == INVALID_SOCKET) return -1;

    /* 监听套接字在 xw32_armSocket 里已置非阻塞（ioctlsocket FIONBIO），
     * 故 accept 无连接时立即返回 WSAEWOULDBLOCK，不会在主循环里阻塞。 */
    client = accept(p->socket, NULL, NULL);
    if (client == INVALID_SOCKET) return -1;

    /* [WSAASYNC 2026-11-21] **FD_ACCEPT 的水平语义要自己补回来**。
     * WSAAsyncSelect 的 FD_ACCEPT 是**边沿**消息——只在队列
     * 从空变非空的那一刻投一次；上面 dispatch 分支 consume 清位后，若
     * 队列里还剩 pending 连接，就再也不会有新消息 ⇒ 后续连接全部饿死
     * （实测：60 并发用例第 60 条建不起来、SSH/Telnet 双服务端只出一路
     * 会话）。⇒ accept 成功后把位重新置回缓存：队列还有 pending ⇒ 下一
     * 轮再投一次 Accept；队列已空 ⇒ 多投一轮空 Accept（accept 返
     * WSAEWOULDBLOCK，无副作用）且位已被 consume ⇒ 自终止。 */
    p->pendingEvents |= (uint32_t)FD_ACCEPT;

    /* 显式把新连接置为非阻塞。
     * 新连接的首轮探测（xw32_probeArm）会把本 socket 与 ring 隐藏窗口做
     * 一次性关联，覆盖 accept 继承自监听套接字的旧关联；但 socket 的阻塞
     * 属性**不保证**随 accept 继承（不同 Winsock 实现行为不一），而本
     * 后端全部收发路径都建立在「非阻塞 + WSAEWOULDBLOCK 背压」之上——
     * 阻塞 recv 会把主循环钉死，所以这里显式设置。 */
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

#endif /* XNETWORK_USE_PLATFORM_API && _WIN32 && XNETWORK_WIN_SOCKET_SELECT */
