/**
 * @file       XDeviceNetwork_fault.h
 * @brief      Windows 套接字后端的**平台错误码注入**接口（**仅测试用**）。
 * @details    框架层把 Windows 的 WSA 错误码映射成 Qt 口径的
 *             `XAbstractSocket_SocketError`（见
 *             `Src/XCore/XCode/XNetwork/XAbstractSocket/XAbstractSocket.c`
 *             的 `socketErrorFromPlatform`）。其中有几条契约在测试里
 *             **造不出来**：`WSAECONNREFUSED` / `WSAETIMEDOUT` /
 *             `WSAEHOSTUNREACH` / `WSAEACCES` / `WSAENOBUFS` —— 真机上
 *             要么得「连一个真会失败的地址」（慢、不确定，还看当时的
 *             网络状态），要么根本撞不出来。本接口让回归测试能
 *             **确定性地**制造指定的平台错误码。
 *
 *             ## 核心原则：注入只改「结果」，不改「流程」
 *
 *             注入点**不是另写一条假路径**，而是把某次平台调用的
 *             **结果**替换掉（读路径把 `recv` 的结果伪造成
 *             `SOCKET_ERROR` + 注入码，写路径把 `send` 同理，connect/bind
 *             同理）→ 之后**原有的收口代码一字不改**地处理它 →
 *             **注入错误与真实错误走的是同一条收口路径**（返回值、
 *             `lastError` 的处理、是否进错误通道、是否判死，全部一致）。
 *
 *             这条是硬要求：若测试走的是假路径（另写一个「假装失败」的
 *             分支），测出来的行为**不能代表真实行为** —— 例如真实
 *             `WSAEWOULDBLOCK` **不进 `lastError`**（否则框架会把
 *             「对端正常关闭」误报成 TemporaryError），假路径若顺手写了
 *             `lastError`，测出来的是假的。
 *
 *             ## 零成本
 *
 *             未武装注入时，热路径上只有一句
 *             `if ((g_faultArmedMask & (1L << step)) == 0) return 0;`
 *             —— 一个**分支预测为假的比较**（读一个全局位掩码 + 与 + 判零），
 *             不引入锁、不引入系统调用、不写内存，正常路径的性能特征不变。
 *
 *             ## 线程安全
 *
 *             注入状态是**进程级**的（不是线程级）：套接字收发跑在**事件
 *             循环线程**上，而测试可能从另一个线程下发注入；若用 TLS
 *             （`__declspec(thread)`），跨线程注入会**静默失效** → 测试
 *             「什么都没发生」→ **假绿**。故用「全局位掩码（快速路径，
 *             volatile LONG）+ SRWLOCK 保护的状态（慢路径）」：任何线程
 *             都能武装/清除，事件循环线程都能看到。
 *
 * @note       本头只声明接口；实现位于
 *             `Drive/windows/Device/Network/XDeviceNetwork_win32_select.c`
 *             的「平台错误码注入」段。
 * @note       **非 Windows 平台本头为空**：POSIX/Android 构建里没有这套
 *             注入点，头文件里不声明任何符号，误包含不会引发链接错误。
 */

#ifndef XDEVICENETWORK_FAULT_H
#define XDEVICENETWORK_FAULT_H

#if defined(_WIN32)

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注入点：**哪一步**被注入。
 * @details 用枚举而不是裸魔数，调用方一眼能看懂（`Read` / `Write` /
 *          `Connect` / `Bind`）。
 */
typedef enum XDeviceNetworkFaultStep {
    XDeviceNetworkFaultStep_None    = 0, /**< 不注入（保留值；`Clear` 传它表示「全清」）。 */
    XDeviceNetworkFaultStep_Read    = 1, /**< 读路径（`recv` / `recvfrom`）。 */
    XDeviceNetworkFaultStep_Write   = 2, /**< 写路径（`send` / `sendto`）。 */
    XDeviceNetworkFaultStep_Connect = 3, /**< 连接路径（`connect`）。 */
    XDeviceNetworkFaultStep_Bind    = 4  /**< 绑定路径（`bind`）。 */
} XDeviceNetworkFaultStep;

/**
 * @brief 「注入次数不限」：持续注入，直到 `socketFaultClear`。
 * @note 作为 `XDeviceNetwork_socketFaultInjectRepeat` 的 `repeatCount` 传入。
 */
#define XDEVICENETWORK_FAULT_REPEAT_UNLIMITED  0

/**
 * @brief 武装一个**一次性**的平台错误码注入。
 * @param step      注入点（读 / 写 / connect / bind）。
 * @param errorCode 要注入的**平台原生**错误码。Windows 上是 **WSA 错误码**
 *                  （如 `WSAECONNREFUSED`=10061、`WSAETIMEDOUT`=10060、
 *                  `WSAEHOSTUNREACH`=10065、`WSAEACCES`=10013、
 *                  `WSAENOBUFS`=10055、`WSAEWOULDBLOCK`=10035）；
 *                  调用方需自行 `#include <winsock2.h>` 取得这些常量。
 * @param skipCount **先放行多少次**同一步骤的操作，之后才开始注入。
 *                  `0` = 下一次操作就注入；`1` = 第一次成功、第二次失败
 *                  （「先成功再失败」的时序契约用它）。
 * @return 成功 true；参数非法或当前后端不支持注入时 false。
 * @details 注入**只发生一次**（打完自动解除武装）⇒ 后续操作回到真实行为。
 * @note 注入的错误**走与真实错误完全相同的收口路径**（见文件头注）。
 */
bool XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep step, int errorCode,
                                      int skipCount);

/**
 * @brief 武装一个**可重复**的平台错误码注入。
 * @param step       注入点。
 * @param errorCode  要注入的平台原生错误码（WSA 码）。
 * @param skipCount  先放行多少次同一步骤的操作，之后才开始注入（<0 按 0 处理）。
 * @param repeatCount 连续注入多少次。
 *                   `1` = 只注入一次（与 `socketFaultInject` 等价）；
 *                   `XDEVICENETWORK_FAULT_REPEAT_UNLIMITED`(=0) = **不限次**，
 *                   持续注入直到 `socketFaultClear`；`<0` 也按「不限次」处理。
 * @return 成功 true；参数非法或当前后端不支持注入时 false。
 */
bool XDeviceNetwork_socketFaultInjectRepeat(XDeviceNetworkFaultStep step, int errorCode,
                                            int skipCount, int repeatCount);

/**
 * @brief 解除某个注入点的武装（恢复真实行为）。
 * @param step 注入点；传 `XDeviceNetworkFaultStep_None` 等价于「全清」。
 */
void XDeviceNetwork_socketFaultClear(XDeviceNetworkFaultStep step);

/**
 * @brief 解除**所有**注入点的武装（恢复真实行为）。
 */
void XDeviceNetwork_socketFaultClearAll(void);

/**
 * @brief 查询某个注入点的当前状态（供测试自检「注入是否真的还挂着」）。
 * @param step          注入点。
 * @param errorCode     输出：当前待注入的错误码（未武装时 0）；可为 NULL。
 * @param skipRemaining 输出：还要放行多少次；可为 NULL。
 * @param repeatRemaining 输出：还要注入多少次（0 = 不限次）；可为 NULL。
 * @return 该注入点当前**已武装**返回 true；未武装/参数非法返回 false。
 */
bool XDeviceNetwork_socketFaultQuery(XDeviceNetworkFaultStep step, int* errorCode,
                                     int* skipRemaining, int* repeatRemaining);

/**
 * @brief 当前构建的后端**是否支持**错误码注入。
 * @return 支持返回 true。
 * @details WSAEventSelect 后端支持；对照后端（IOCP 重叠 I/O）没有注入点，
 *          恒返回 false —— 显式告知，避免调用方以为注入生效了（→ 假绿）。
 */
bool XDeviceNetwork_socketFaultSupported(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIN32 */

#endif /* XDEVICENETWORK_FAULT_H */
