# Windows IOCP 读完成丢失 —— 交接文档（2026-11-04）

> **状态：未完成，待续。** 用户要求：「Windows 网络后端实现一套对齐 Qt 的，
> 现有 IOCP 后端也保留，用宏切换，以后有空再继续查。」

## 一、问题现象

GUI 客户端（`HsfClient.exe`）+ 服务端（`HsfServer.exe`）环回通信：

- 两端正常收发几秒后，**在同一时刻双双停止接收**，之后各自继续发送 5~10 秒
- 6 秒静默判线后断开重连
- 服务端只收到连接期最初 3 个订阅，切页产生的订阅帧全部丢失
- `netstat` 全程 ESTABLISHED

## 二、根因（已定性）

**Windows 上 `WSARecv` 的 IOCP 完成通知在本机环回链路上不可靠。**

故障用两个互相独立的官方 API 拆成三态（实现在 `XDeviceNetwork_socketRead`
的读停摆看门狗）：

| 形态 | 判定手段 | 实测日志 |
|---|---|---|
| 完成包丢失 | `GetOverlappedResult(FALSE)` == TRUE | `读停摆=完成包丢失（已结束 385 字节），补投递` |
| 读请求挂死 | `ioctlsocket(FIONREAD)` > 0 但操作不完成 | `读停摆=数据在内核但读未完成（内核待读=385 字节）` |
| 对端未发 | 两者都不成立 | `读停摆=内核无数据(对端未发)` |

**决定性证据**：同一批 385 字节，先表现为「内核有数据但读完不成」，取消重挂后
立刻变成「操作已完成、完成包丢失」⇒ **挂起的读请求本身被平台挂死**。

### 已排除项（均实测）

| 排除对象 | 方法 | 结果 |
|---|---|---|
| 485 / 模拟量等其它 IO 干扰 | 关掉后复测 | 故障完全相同 |
| 写路径 | 写槽看门狗 | 零触发 |
| 协议层 + 服务端应用层 | Python 客户端同时序 | **11/11 全过** |
| 框架层（XTcpServer 等） | 18 + 9 项专项测试 | 全过 |
| 同进程环回 | 27 项测试 | 不复发（跨进程 GUI 客户端 100% 复现） |

## 三、Qt 的做法（根治方向依据）

Qt 6.8 `src/network/socket/qnativesocketengine_win.cpp`（1544 行）实源确认：

```cpp
::WSARecv(socketDescriptor, buf.data(), DWORD(buf.size()), &bytesRead, &flags, nullptr, nullptr);
::WSASend(socketDescriptor, &buf, 1, &bytesWritten, flags, 0, 0);
```

**OVERLAPPED 参数全部传 `nullptr`** —— Qt 完全不用 overlapped I/O / IOCP，
走 `QSocketNotifier` → `WSAEventSelect` + 窗口消息，**不依赖完成包投递**。

拉取命令：
```bash
curl -s "https://code.qt.io/cgit/qt/qtbase.git/plain/src/network/socket/qnativesocketengine_win.cpp?h=6.8" -o qns_win.cpp
```

### 微软官方立场

两轮检索（Learn 搜索 API、KB、Raymond Chen、WS2019 release-health）：
**没有任何微软文档承认「IOCP 完成包丢失」是缺陷。** 官方只承认三种合法不投递：

- `FILE_SKIP_COMPLETION_PORT_ON_SUCCESS`
- `CancelIoEx` 同步取消成功
- `closesocket` 后的竞态

## 四、本轮已落地的代码修复（未提交）

### 真缺陷

| # | 缺陷 | 位置 |
|---|---|---|
| 1 | Win32 IOCP 后端**从不填 `CQEntry.m_osFd`**，假 EOF 复核 PEEK 打到 **fd 0（stdin）**，健康链路被误判「对端 FIN」 | `XNetIoRingWin32.c` |
| 2 | 假 EOF 判据 `m_osFd < 0` 漏掉 0 值 | `XAbstractNetIoRing.c` |
| 3 | **`registerEvent` 把 `XDeviceContext*` 当 HANDLE** 传给 `CreateIoCompletionPort` | `XNetIoRingWin32.c` |
| 4 | `WSARecv` 的 `lpNumberOfBytesRecvd` 与 overlapped 同时非 NULL（官方要求传 NULL） | `XDeviceNetwork_win32.c` |
| 5 | **`XLinkServer` 从未接 socket 的 `disconnected` 信号**（只有客户端接了）⇒ 僵尸会话 | `XLinkServer.c` |
| 6 | 写路径单 OVERLAPPED + 单缓冲复用（违反 MSDN 禁令）⇒ 改为**独立写槽池**（4 槽） | `XDeviceNetwork_win32.c` |
| 7 | `socketContinueWrite` 的 `if (writePending) return` 门 ⇒ 单个完成包丢失即永久挡死续发；且先读走再提交会丢数据 | `XDeviceNetwork_win32.c` |
| 8 | `HsfLinkClient` 按需订阅缺 `m_topicSent` 位（与引用计数分离） | `HsfLinkClient.c` |

### 已修正的误改（教训）

| # | 误改 | 后果 | 最终处理 |
|---|---|---|---|
| 9 | accept 完成分支清 `acceptPending` | 两个 AcceptEx 同时在飞 | **回退为不清** |
| 10 | `GetAcceptedSocket` 重复调 `abandonSocketIocpOperation` | 段错误（cdb: `XDevice_dispatchControl` 0xFEEEFEEE） | 已回退 |
| 11 | `reap` 里 `close` socket | 同一 UAF 段错误 | 改为**只摘信号不关 socket** |
| 12 | 空壳会话闸用 `XIODevice_fd < 0` 判连接有效性 | 误杀真实连接（订阅归零） | 已撤，改按 `ConnectedState` 判 |

### 新增能力

| # | 能力 | 位置 |
|---|---|---|
| 13 | 读挂死看门狗（三态判定 + `GetOverlappedResult` 补投递 + `CancelIoEx` 重挂） | `XDeviceNetwork_socketRead` |
| 14 | 写槽超时看门狗 | `XDeviceNetwork_win32.c` |
| 15 | 静默判线可观测日志 + 帧级 trace 开关（`XLINK_TRACE_FRAMES`，默认 0） | `XLinkSession.c` / `XLink_config.h` |
| 16 | `XIODevice.m_fd` 统一句柄登记（socket 也应存 XFd，用户指出） | `XAbstractSocket.c` |
| 17 | 会话 pump 循环抽干（最多 64 轮） | `XLinkSession.c` |

### 测试

- `Test/XIOTest/XTcpServerTest.c`：新增 3 个用例（同一连接多帧收发 /
  连接 socket 所有权 / pending 队列单次投递）+ `XTcpServerTest_run()` 入口
- `Test/XIOTest/XSocketStallTest.c`（**新文件**）：9 个用例（长空闲恢复 /
  高频小帧 / 背压完整性 / 双向高频 / 反复重连 / 无串包 / 双连接竞争 /
  多连接隔离 / 推送压力下上行小帧）+ `XSocketStallTest_run()` 入口
- 命令行入口：`--test xtcpserver`、`--test xsocket-stall`（`main.c` + `XTestCommand.c`）

## 五、下一步任务

### 任务 A：实现 Qt 对齐的 Windows 网络后端（主任务）

- **保留现有 IOCP 后端**（`XDeviceNetwork_win32.c`）
- **新增 `WSAEventSelect`/`select` 后端**，建议新文件
  `Drive/windows/Device/Network/XDeviceNetwork_win32_select.c`
- **用宏切换**，建议：
  ```c
  /* XNetwork_config.h 或 CMake 选项 */
  #ifndef XNETWORK_WIN_BACKEND
  #define XNETWORK_WIN_BACKEND 1   /* 1=IOCP(现状) 2=WSAEventSelect(Qt 对齐) */
  #endif
  ```
- 现有后端已由 `#if defined(XNETWORK_USE_PLATFORM_API) && defined(_WIN32)`
  门控（7~2747 行），新后端沿用同门控再加一层后端选择
- CMake 按宏择一编译（参考 `Drive/windows/CMakeLists.txt`）

**语义契约必须完全一致**（`XDeviceNetwork_*` 全部 API）：

- `socketRead` / `socketWrite` / `socketContinueRead` / `socketContinueWrite`
- `serverCreate` / `serverAccept` / `serverGetAcceptedSocket` / `serverClose`
- `socketHandleEvent`（事件掩码 → `XSocketActType` 映射）
- `socketDescriptor` / `socketSetDescriptor` / `socketDisconnect`

**WSAEventSelect 要点**（官方文档）：
- 自动把 socket 设为非阻塞
- 每 socket 一个事件对象，无法按事件类型分开
- `FD_READ`/`FD_OOB`/`FD_ACCEPT` 是**水平触发**
- 用 `WSAEnumNetworkEvents` 重置事件对象状态（这是唯一的正确重置方式）
- 监听 socket 的 `WSAEventSelect` 关联会被 accept 出的 socket 继承
- 等待集上限 `WSA_MAXIMUM_WAIT_EVENTS`（通常 64），需按需分组或改用 `select`

### 任务 B：回归与收口

1. `--test xtcpserver`（18 项）全绿
2. `--test xsocket-stall`（9 项）全绿
3. HSF 端到端：服务端日志出现 **>3 个**「会话订阅主题」（切页订阅全部到达）
4. 零 `读停摆` / 零 `静默超时` 告警

### 任务 C：提交

按分支惯例（每战役合并为单一提交 + `--force-with-lease`）。
**注意**：工作区还有并发会话的改动（`HighSpeedFilter/` 多文件、`Src/Industry/`），
提交前先确认归属，勿误提交。

## 六、关键教训

1. **判「完成包丢失」用 `GetOverlappedResult(FALSE)`，判「数据有没有到」用 `FIONREAD`**
   —— 两个独立证据缺一不可，否则会误判方向。
2. **`OVERLAPPED` 每个在途操作必须独立一份**。用布尔标志互斥是错的方向——
   标志一旦与真实状态脱节就永久自锁。
3. **`WSARecv`/`WSASend` 在 `lpOverlapped` 非 NULL 时，`lpNumberOfBytesRecvd`
   必须传 NULL**。完成字节数取 `GetQueuedCompletionStatus` 的输出参数。
4. **`m_deviceCtx` 是 `XDeviceContext*`，不是句柄**——当 HANDLE 用即 bug。
5. **同进程测试全过 ≠ 跨进程正确**。本轮 27 项同进程测试无一复现。
6. **加日志前先想清楚「这条日志要区分哪几种情况」**——读停摆三态日志一加上，
   五分钟就定性了，此前几小时都在猜。
