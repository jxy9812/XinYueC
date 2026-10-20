# XNtp SNTP 客户端

> 版本：v1.0　|　协议依据：RFC 5905 (NTPv4/SNTP)　|　日期：2026-10-15
> 落点：`Src/XProtocol/XNtp/`　|　开关：`XNTP_ON`

---

## 目录

- [1. 协议定位](#1-协议定位)
- [2. 总体架构](#2-总体架构)
- [3. 线上格式](#3-线上格式)
- [4. 时间戳与校时公式](#4-时间戳与校时公式)
- [5. 客户端状态机](#5-客户端状态机)
- [6. 功能使用指南](#6-功能使用指南)
- [7. 完整示例](#7-完整示例)
- [8. 配置项](#8-配置项)
- [9. API 速查表](#9-api-速查表)
- [10. 平台差异与权限](#10-平台差异与权限)
- [11. 测试](#11-测试)
- [12. 设计与实现要点](#12-设计与实现要点)
- [13. 常见问题](#13-常见问题)

---

## 1. 协议定位

XNtp 是 RFC 5905 的**最小客户端实现（SNTP 口径）**：单次请求-应答校时，
不做时钟滤波、斜率补偿或服务器协同（那些是完整 ntpd 的职责）。

**为什么自研而不是用系统 ntpd**：目标运行环境是最小化嵌入式 Linux 固件
（buildroot 裁剪），没有 ntpd/chrony，且应用需要**可控的校时策略**——
何时校、校到哪、权限不足怎么退化。XNtp 把这些决策点全部暴露为 API。

| 需求 | XNtp 的回应 |
|------|-------------|
| 板子固件无 ntpd | 应用内自带，一个类 + 一个 UDP 端口即可工作 |
| 默认不乱改系统时钟 | `syncNow` 只算偏移；`applyToSystem` 由应用显式调用 |
| 权限不足优雅降级 | `XDateTime_canSetSystemTime` 探测；无权限时照常同步、只读偏移 |
| 服务器可指定 IP 或域名 | `setServer_2("ntp.aliyun.com", 123)` 或 `"192.168.1.100"` |
| 周期自动校时 | `setAutoSync(true, intervalMs)`，XObject 定时器驱动 |

**不适用场景**：需要亚秒级时钟驯服、多服务器 swarm 合成的场景请部署 ntpd/chrony。

---

## 2. 总体架构

```
┌────────────────────────────────────────────────┐
│ 应用层：配置服务器 / syncNow / applyToSystem      │
└──────────────────┬─────────────────────────────┘
                   │ XNtpClient_xxx
┌──────────────────▼─────────────────────────────┐
│ XNtpClient（XObject 派生）                        │
│  · 请求发送 + 应答校验 + offset/delay 计算         │
│  · 超时定时器（重试/判失败）+ 自动同步定时器         │
│  · 信号：synchronized / syncFailed / offsetChanged│
└──────────────────┬─────────────────────────────┘
                   │ XUdpSocket（UDP，writeDatagram 定向发送）
┌──────────────────▼─────────────────────────────┐
│ 传输层：Posix io_uring（SENDTO）/ Win32 IOCP      │
│  （UDP 定向收发依赖 2026-10 驱动修复，见 §10）      │
└────────────────────────────────────────────────┘
```

### 模块文件

| 文件 | 职责 |
|------|------|
| `XNtp_config.h` | 开关与协议常量（端口/包长/超时/重试/纪元偏移） |
| `XNtpProto.h` | RFC 5905 报文结构、LI/Mode/Stratum 常量 |
| `XNtpPacket.h/.c` | 48 字节大端编解码、时间戳纪元换算、应答合法性闸门 |
| `XNtpClient.h/.c` | 客户端类（XObject 派生，信号槽 + 定时器） |
| `XNtpClient_Protected.h` | 私有状态结构（仅供实现使用） |

---

## 3. 线上格式

48 字节定长报文，**全部大端**（RFC 5905 第 6 节）：

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 1 | LI(2b)\|VN(3b)\|Mode(3b) | 闰秒指示\|版本\|模式 |
| 1 | 1 | Stratum | 0=KoD，1=主钟，2..15 有效，16=未同步 |
| 2 | 1 | Poll | 轮询间隔（2 的幂秒） |
| 3 | 1 | Precision | 时钟精度（2 的幂秒） |
| 4 | 4 | Root Delay | 到主钟时延 |
| 8 | 4 | Root Dispersion | 到主钟离散 |
| 12 | 4 | Reference ID | 参考源标识 |
| 16 | 8 | Reference Timestamp | 上次校时时刻 |
| 24 | 8 | Originate Timestamp | **应答中=T1 回显**（防错包/重放的关键） |
| 32 | 8 | Receive Timestamp | T2：服务器收到请求时刻 |
| 40 | 8 | Transmit Timestamp | 请求中=T1；应答中=T3 |

**T1 的位置（易错点）**：客户端把发出时刻填在 **Transmit** 字段；服务器
应答时把请求的 Transmit 原样拷入应答的 **Originate**。客户端校验应答的
Originate 必须等于自己发出的 T1。

64 位时间戳 = 32 位秒（自 **1900-01-01**）+ 32 位小数（2^-32 秒）。
换算 Unix 纪元减去 `XNTP_ERA_OFFSET`(2208988800)。

---

## 4. 时间戳与校时公式

四个时间戳（全换算到同一时钟域后）：

```
T1 = 客户端发出请求时刻        （本机墙钟，XDateTime_currentWallMSecsSinceEpoch）
T2 = 服务器收到请求时刻        （应答 Receive 字段）
T3 = 服务器发出应答时刻        （应答 Transmit 字段）
T4 = 客户端收到应答时刻        （本机墙钟）

offset(毫秒) = ((T2 - T1) + (T3 - T4)) / 2     ← 服务器比本地快多少
delay (毫秒) = (T4 - T1) - (T3 - T2)           ← 网络往返时延
```

**为什么需要 `XDateTime_currentWallMSecsSinceEpoch`（亚秒墙钟）**：
`currentSecsSinceEpoch` 是秒级截断（误差可达 1 秒，NTP 不可用）；
`currentMSecsSinceEpoch` 是单调钟（校时会跳变，时间戳无意义）。
墙钟毫秒 = 真实 UTC + 亚秒精度，是唯一正确的时间源。

**delay 的可信度**：delay 大（如 >200ms）说明网络抖动大，offset 估计的
误差上界约为 delay/2。应用可据此决定丢弃本次结果或换更近的服务器。

**应答校验矩阵**（`XNtpPacket_isValidServerReply`）：

| 校验项 | 拒绝时的错误码 |
|--------|---------------|
| Mode 必须为 Server(4) | 忽略该包（继续等待，超时兜底） |
| Stratum ∈ 1..15 | 0 → `KissOfDeath`；16+ → `ServerUnsynchronized` |
| LI ≠ 3（服务器已同步） | `ServerUnsynchronized` |
| Originate = 我发出的 T1 | 错包忽略；零回显拒绝 |
| Receive/Transmit 非零 | 忽略该包 |

---

## 5. 客户端状态机

```
setServer 失败/未配置        syncNow 成功发出
   ┌─────┐  ┌─────────┐     ┌─────────────────┐
   ▼     │  ▼         │     ▼                 │
┌──────┐ ┌──────────┐   ┌──────────┐            │
│ Idle │ │ Syncing  │──►│Synchronized           │
└──────┘ └────┬─────┘   └────┬─────┘            │
              │超时重试耗尽    │再次 syncNow       │
              ▼              └──────────────────┘
        ┌──────────┐
        │  Failed  │ ← KoD / 未同步 / 套接字失败（立即）
        └──────────┘
```

- **Syncing 期间拒绝并发 syncNow**（返回 false）
- 应答中**非法报文被忽略**而非判失败（继续等正确应答，超时兜底）——
  防止杂散 UDP 包伪造失败；唯 KoD/未同步立即判失败（服务器明确表态）
- 换服务器（`setServer`）重置同步状态：旧偏移对新服务器无意义

---

## 6. 功能使用指南

### 6.1 生命周期

```c
#include "XNtpClient.h"

/* 堆对象 */
XNtpClient* client = XNtpClient_create();
if (client) {
    /* ... */
    XClassDelete((XClass*)client);
}
```

### 6.2 最小校时流程

```c
/* 1. 配置（启动阶段做，DNS 解析是阻塞的） */
XNtpClient_setServer_2(client, "ntp.aliyun.com", 123);  /* 或 "192.168.1.100" */
XNtpClient_setTimeout(client, 5000);
XNtpClient_setRetries(client, 2);

/* 2. 发起异步同步（立即返回） */
if (XNtpClient_syncNow(client)) {
    /* 已发出，等待信号 */
}

/* 3. 事件循环里泵（驱动 readyRead 与定时器） */
for (;;) {
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    /* ... */
}

/* 4. 结果 */
/* 同步成功信号里 / 之后查询： */
int64_t offset = XNtpClient_offsetMs(client);     /* 服务器 - 本地，毫秒 */
int64_t delay  = XNtpClient_roundTripMs(client);
uint64_t secs  = XNtpClient_serverSecs(client);   /* 服务器 UTC 纪元秒 */
```

### 6.3 周期自动校时

```c
/* 每 60 分钟自动 syncNow（XObject 定时器驱动，校时跳变不影响它） */
XNtpClient_setAutoSync(client, true, 60 * 60 * 1000);
```

### 6.4 写系统时钟（默认不自动）

```c
/* 只在应用策略决定要校时时调用；需要 root/CAP_SYS_TIME/SE_SYSTEMTIME */
if (XNtpClient_isSynchronized(client)
    && XDateTime_canSetSystemTime()) {
    if (XNtpClient_applyToSystem(client)) {
        /* 系统时钟已对齐到估算的真实时刻（秒级） */
    } else {
        /* 权限中途丧失等，退化为只读模式 */
    }
}
```

### 6.5 信号订阅

```c
static void on_synced(XObject* receiver, XVarList* args) {
    XNtpClient* c = (XNtpClient*)receiver;
    XPrintf("校时成功: offset=%lldms delay=%lldms\n",
            (long long)XNtpClient_offsetMs(c),
            (long long)XNtpClient_roundTripMs(c));
}
static void on_failed(XObject* receiver, XVarList* args) {
    XNtpClient* c = (XNtpClient*)receiver;
    XPrintf("校时失败: err=%d\n", (int)XNtpClient_lastError(c));
}

XObject_connect_1((XObject*)client, XSignal(XNtpClient_synchronized_signal),
                  (XObject*)client, on_synced, XConnectionType_Auto);
XObject_connect_1((XObject*)client, XSignal(XNtpClient_syncFailed_signal),
                  (XObject*)client, on_failed, XConnectionType_Auto);
```

---

## 7. 完整示例

嵌入式设备开机后校时 + 每小时自动保持：

```c
#include "XNtpClient.h"
#include "XDateTime.h"
#include "XCoreApplication.h"
#include "XPrintf.h"

static void on_synced(XObject* receiver, XVarList* args)
{
    XNtpClient* c = (XNtpClient*)receiver;
    int64_t offset = XNtpClient_offsetMs(c);
    XPrintf("[NTP] 校时成功，offset=%lldms delay=%lldms\n",
            (long long)offset, (long long)XNtpClient_roundTripMs(c));

    /* 策略：偏移超过 2 秒且有权限时才落盘到系统时钟 */
    if ((offset > 2000 || offset < -2000) && XDateTime_canSetSystemTime()) {
        if (XNtpClient_applyToSystem(c))
            XPrintf("[NTP] 系统时钟已更新\n");
        else
            XPrintf("[NTP] 写时钟失败（权限不足），保持只读模式\n");
    }
}

int main(void)
{
    XNtpClient* ntp = XNtpClient_create();

    XNtpClient_setServer_2(ntp, "192.168.1.100", 123); /* 内网自建 NTP 源 */
    XNtpClient_setTimeout(ntp, 3000);
    XNtpClient_setRetries(ntp, 3);
    XNtpClient_setAutoSync(ntp, true, 60 * 60 * 1000); /* 每小时保持 */

    XObject_connect_1((XObject*)ntp, XSignal(XNtpClient_synchronized_signal),
                      (XObject*)ntp, on_synced, XConnectionType_Auto);
    XNtpClient_syncNow(ntp); /* 开机立即校一次 */

    for (;;)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
}
```

---

## 8. 配置项

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XNTP_ON` | `XPROTOCOL_ON` | 协议总开关（`XProtocol_config.h` 登记） |
| `XNTP_CLIENT_ON` | 1 | 客户端核心 |
| `XNTP_DEFAULT_PORT` | 123 | NTP 标准端口 |
| `XNTP_PACKET_SIZE` | 48 | 基本报文长度 |
| `XNTP_ERA_OFFSET` | 2208988800 | 1900→1970 纪元偏移（秒） |
| `XNTP_FRAC_PER_MS` | 4294967 | 32 位小数中 1ms 的刻度数（2^32/1000） |
| `XNTP_DEFAULT_TIMEOUT_MS` | 5000 | 单次同步应答超时 |
| `XNTP_DEFAULT_RETRIES` | 2 | 超时重试次数 |
| `XNTP_DEFAULT_SYNC_INTERVAL_MS` | 3600000 | 自动同步周期（60 分钟） |
| `XNTP_VERSION` | 4 | 请求中的协议版本 |

---

## 9. API 速查表

### 9.1 生命周期

| API | 说明 |
|-----|------|
| `XNtpClient_init(obj)` | 栈上初始化 |
| `XNtpClient_create()` | 堆创建（`XClassDelete` 释放） |

### 9.2 配置

| API | 说明 |
|-----|------|
| `XNtpClient_setServer(s, name, port)` | 配置服务器（XString 版） |
| `XNtpClient_setServer_2(s, utf8, port)` | 配置服务器（UTF-8 便捷版） |
| `XNtpClient_setTimeout(s, ms)` | 应答超时（0=默认 5000） |
| `XNtpClient_setAutoSync(s, on, ms)` | 周期自动同步（0=默认 60 分钟） |
| `XNtpClient_setRetries(s, n)` | 超时重试次数 |

### 9.3 同步与查询

| API | 说明 |
|-----|------|
| `XNtpClient_syncNow(s)` | 发起异步同步（Syncing 中返回 false） |
| `XNtpClient_state(s)` | Idle/Syncing/Synchronized/Failed |
| `XNtpClient_lastError(s)` | 最近失败原因 |
| `XNtpClient_isSynchronized(s)` | 是否至少成功同步过 |
| `XNtpClient_offsetMs(s)` | 服务器-本地偏移（毫秒） |
| `XNtpClient_roundTripMs(s)` | 往返时延（毫秒） |
| `XNtpClient_serverSecs(s)` | 服务器 UTC 纪元秒 |
| `XNtpClient_stratum(s)` | 服务器层级 |

### 9.4 校时落地

| API | 说明 |
|-----|------|
| `XNtpClient_applyToSystem(s)` | 把估算的真实时刻写入系统时钟（秒级；需权限） |
| `XDateTime_canSetSystemTime()` | 探测当前进程是否有写时钟权限 |
| `XDateTime_setSystemSecsSinceEpoch(secs)` | 底层写时钟接口 |
| `XDateTime_currentWallMSecsSinceEpoch()` | 亚秒墙钟（NTP 时戳的时间源） |

### 9.5 信号

| 信号 | 时机 |
|------|------|
| `XNtpClient_synchronized_signal` | 同步成功（含重试成功） |
| `XNtpClient_syncFailed_signal` | 重试耗尽或服务器明确拒绝 |
| `XNtpClient_offsetChanged_signal` | 每次成功，参数为新偏移毫秒 |

### 9.6 编解码

| API | 说明 |
|-----|------|
| `XNtpPacket_encode / decode` | 48 字节大端线序互转 |
| `XNtpPacket_makeClientRequest(p, t1)` | 构造客户端请求（T1 填 Transmit） |
| `XNtpPacket_isValidServerReply(p, t1)` | 应答合法性闸门 |
| `XNtpTimestamp_toUnixMs / fromUnixMs` | NTP 时戳 ↔ Unix 毫秒 |
| `XNtpTimestamp_isZero(ts)` | 零值判断 |

---

## 10. 平台差异与权限

### 10.1 UDP 定向收发（2026-10 驱动修复的前置依赖）

XNtp 依赖「向指定地址发 UDP 包 + connect 过滤来源」的能力。此能力由
`Drive/*/Device/Network/` 驱动层提供，**历史缺陷已于 2026-10 修复**：
- POSIX：UDP 目的地址曾被丢弃（`IORING_OP_SEND`），已改 `IORING_OP_SENDTO`；
  UDP `connectToHost` 曾不真正 connect，已落实 `connect()`（内核按对端过滤
  来源，兼作防欺骗）；`platformGetLastDatagramSender` 曾返回未初始化内存，
  已改为不可用时返回 false。
- Win32：UDP connect 同步修复；WSASendTo/WSARecvFrom 路径本就完整。
- 回归红线：`--test udp-directed`（14 项）。

**注意**：未连接 UDP socket 的「对端来源地址」读取在 POSIX 上依赖
`IORING_OP_RECVMSG`（内核 ≥ 5.12）；RK3566/68 的 5.10 BSP 内核不可用。
XNtp 的对策是**已连接 UDP**（connect 过滤），不依赖 RECVMSG，5.10 可用。

### 10.2 设置系统时钟的权限

| 平台 | 要求 | `canSetSystemTime` 实现 |
|------|------|------------------------|
| POSIX | root 或 `CAP_SYS_TIME`(25) | Linux 下读 `/proc/self/status` 的 CapEff 位图 |
| Windows | `SE_SYSTEMTIME_NAME` 特权 | `AdjustTokenPrivileges` 试启用并查 `GetLastError` |

无权限时：同步照常、偏移照算，仅 `applyToSystem` 返回 false。
开发机（Windows 桌面用户）默认无该特权，属正常现象。

### 10.3 时钟跳变的影响面

| 设施 | 校时后是否受影响 | 原因 |
|------|------------------|------|
| XTimer / XObject 定时器 | 不受影响 | 基于 `CLOCK_MONOTONIC`/`GetTickCount64` |
| XLink 心跳与超时 | 不受影响 | 同上（nowMs 由调用方传入单调毫秒） |
| 日志/文件时间戳/证书校验 | **会跳变** | 依赖墙钟（这正是校时的目的） |

---

## 11. 测试

| 入口 | 命令 / 路径 |
|------|------------|
| 命令行 | `XinYueC_Static.exe --test ntp` |
| Shell 命令 | `test ntp` |
| 图形菜单 | Test 菜单「协议栈 → XNtp(ntp校时)」 |

**54 项断言，全离线**（进程内 XUdpSocket 模拟 NTP 服务器，不依赖真实网络）：

| 分组 | 覆盖 |
|------|------|
| 报文编解码 | 大端往返、0x23 首字节位打包、T1 在 Transmit 字段、纪元换算、早于 1970 钳制 |
| 应答合法性闸门 | Mode/Stratum=0/16/LI=3/T1 回显/零回显/零 Transmit 的拒绝矩阵 |
| 端到端校时 | 模拟服务器加 **+60 秒人工偏移**，断言 offset≈+60000ms（±100ms）与白盒等值 |
| 超时与重试 | 黑洞端口 → 重试耗尽 → Failed/Timeout |
| 协议拒绝路径 | KoD / 未同步 / 错包忽略后按超时判失败（防重放） |
| applyToSystem 门控 | 未同步拒绝；无权限如实 false；有权限环境跳过实写（防破坏构建机时钟） |
| 内存泄漏 | 100 次创建/销毁（含定时器路径），分配器回基线 |

端到端核心断言的设计：模拟服务器给时间戳加已知人工偏移，客户端算出的
offset 必须复现该值 —— 这一条同时验证了报文编解码、四时间戳公式、端序
处理与应答校验的联合正确性。

---

## 12. 设计与实现要点

| 决策 | 理由 |
|------|------|
| SNTP 而非完整 NTP | 目标是「无 ntpd 固件的应用内校时」，滤波/驯服越界了 |
| 校时落地与应用分离 | 是否写系统时钟是策略决定（权限/时机/幅度阈值），归应用 |
| 时间戳用墙钟毫秒 | 秒级截断误差 1s 不可接受；单调钟随校时跳变不可用 |
| connect 过滤代替 RECVMSG | 源地址校验在 5.10 内核（无 IORING_OP_RECVMSG）也可用 |
| 非法报文忽略不判败 | 杂散 UDP 不应杀死同步；超时是最终兜底 |
| T1 回显强制校验 | 防错包/重放——offset 用了过期的 T1 就是假偏移 |
| 定容私有状态（无容器） | 客户端状态可完全放入单个 calloc 块，零动态扩容 |
| 库内不取时（除墙钟时戳） | 超时走 XObject 定时器（框架时钟），协议时间戳必须用墙钟 |

**线程模型**：单线程事件循环口径，全类不加锁；`setServer` 含阻塞 DNS，
请在启动阶段调用。

---

## 13. 常见问题

### Q1：syncNow 返回 false？

- 未配置服务器（`lastError == NoServer`）
- 正在 Syncing 中（并发被拒，等上一轮结束）

### Q2：同步一直 Timeout？

1. 服务器地址/端口是否正确（内网自建源先用 `ping`/`ntpdate` 验证）
2. UDP 123 出网是否被防火墙拦（公共 NTP 常只允许 UDP 123 源端口）
3. 超时 5s 对跨公网可能偏短，`setRetries` 增加 2~3 次
4. 域名解析失败也归为 NoServer——先确认 DNS 可用，或直接填 IP

### Q3：offset 是负数正常吗？

正常。负值=本地时钟比服务器快。`applyToSystem` 对正负偏移同等处理。

### Q4：applyToSystem 总是 false？

先查 `XDateTime_canSetSystemTime()`：Linux 非 root 且无 CAP_SYS_TIME、
Windows 未启用 SE_SYSTEMTIME 特权都会 false。嵌入式设备通常整体以 root
运行，若仍失败检查内核是否禁用了 `settimeofday`（seccomp）。

### Q5：delay 很大（>200ms）还能信 offset 吗？

误差上界约 delay/2。内网 <5ms 随便信；跨公网 100ms 时 offset 误差可达
±50ms——对秒级校时足够，对毫秒级需求请换更近的服务器或多源合成。

### Q6：为什么校时后定时器没有乱？

XObject 定时器与 XLink 心跳都基于单调钟，墙钟跳变对它们不可见——这是
仓库时间源裁定（2026-09-27）的直接收益。受影响的只有墙钟消费者（日志、
文件时间戳）。

### Q7：能对着 Windows W32Time / 域控校时吗？

能。Windows 域控同时是 NTP 服务器（UDP 123），`setServer_2(s,
"dc.corp.local", 123)` 即可。

---

## 附：源码索引

| 内容 | 位置 |
|------|------|
| 协议常量 | `XNtp_config.h` |
| 报文结构与枚举 | `XNtpProto.h` |
| 编解码实现 | `XNtpPacket.h/.c` |
| 客户端类 | `XNtpClient.h/.c` |
| 私有状态 | `XNtpClient_Protected.h` |
| 功能测试 | `Test/XProtocolTest/XNtpTest.c` |
| UDP 驱动红线 | `Test/XIOTest/XUdpDirectedTest.c` |
| 写时钟/亚秒墙钟 | `Src/XData/XDateTime/XDateTime.h` + `Drive/*/Core/XDateTime_*.c` |
