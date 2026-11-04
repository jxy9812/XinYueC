# Windows 套接字后端：对齐 Qt 6.8 的 select 后端 —— 进度交接（2026-11-04）

> **状态：收口完成。DDNS/XLink 全绿，六项回归 0 失败。**
> 两套后端并存、宏切换可用；TCP 服务端与套接字读挂起回归全绿。
>
> | 回归 | 结果 |
> |---|---|
> | `--test ddns` | **112 通过 / 0 失败**（原 97/15） |
> | `--test xlink` | **820 通过 / 0 失败**（原 803/17） |
> | `--test xtcpserver` | **18 通过 / 0 失败** |
> | `--test xsocket-stall` | **9 通过 / 0 失败** |
> | `--test ntp` | **83 通过 / 0 失败** |
> | `--test xssl` | **PASS** |

## 一、任务目标（用户原话）

> 「保留原来的 IOCP 但是用宏切换到 qt 版本，XFd 还是我之前说的用法，同时遵守约束文档」
> 「select 也这么难吗，不是有 qt 可以抄吗」
> 「XAbstractSocket 的行为语义都对齐 qt 吧」
> 「read_1 要求修改为对齐 qt 吧，看 qt 是什么语义就修改为什么」

## 二、落地结构

### 2.1 宏切换（`Src/XCore/XCode/XNetwork/XNetwork_config.h`）

新增后端选择宏，两套实现**同时保留**、编译期择一：

```c
#define XNETWORK_WIN_BACKEND_IOCP       1   /* IOCP 重叠 I/O（历史实现，可回退） */
#define XNETWORK_WIN_BACKEND_WSAEVENT   2   /* select 模型（对齐 Qt，默认）     */

#ifndef XNETWORK_WIN_BACKEND
#define XNETWORK_WIN_BACKEND  XNETWORK_WIN_BACKEND_WSAEVENT
#endif

/* 派生的 0/1 两态判据，供源文件 #if 使用（避免未定义标识符静默取 0） */
#define XNETWORK_WIN_SOCKET_WSAEVENT  (0 或 1)
#define XNETWORK_WIN_SOCKET_IOCP      (0 或 1)
```

**切回 IOCP**：`cmake -DCMAKE_C_FLAGS="... /DXNETWORK_WIN_BACKEND=1"`（或改默认值 +
全量重编）。已验证该变体可独立编译、导出完整 18 个 `XDeviceNetwork_socket*` 符号，
而 select 目标文件被裁到 0 符号。

### 2.2 文件布局

| 文件 | 职责 |
|---|---|
| `Drive/windows/Device/Network/XDeviceNetwork_win32.c` | **保留原 IOCP 实现**。整段套接字/服务器实现（原 430~1776 行区间）用 `#if XNETWORK_WIN_SOCKET_IOCP` 门控；平台无关部分（ICMP/初始化/错误/网卡计数/DNS/网卡枚举与配置/多播/系统代理/GSSAPI）留在开关外，两套后端共用。 |
| `Drive/windows/Device/Network/XDeviceNetwork_win32_select.c` | **新增** select 后端。WSAEventSelect 曾尝试，后改 `select()`。 |
| `Drive/windows/Core/XNetIoRingWin32.c/.h` | 事件环增加「套接字就绪轮询表」：只存属主上下文指针，每次 poll 对每个套接字做一次零超时 `select`，就绪者回调 `XDeviceNetwork_win32_selectDispatchReady(owner)`。 |
| `Src/XCore/XIO/XIODevice/XIODevice.c` | `XIODevice_read_1` 全量对齐 Qt `QIODevice::read`。 |
| `Src/XCore/XCode/XNetwork/XAbstractSocket/XAbstractSocket.c` | 多个虚函数按 Qt 语义对齐（见 §4）。 |

### 2.3 关键设计决策：为什么最终不是 `WSAEventSelect`

**起初按任务书写的 `WSAEventSelect` 事件对象实现**（每 socket 一个 `WSAEVENT`，
并进事件环等待集，`WSAEnumNetworkEvents` 复位）。**实测该组合在本进程下不可靠**：

- 事件环侧日志持续出现 `slot not signaled`（事件对象从不置位）；
- 同一时刻 `select` 显示内核确有数据、`recv` 也能取到；
- 结果：读路径只能靠上层主动 `ContinueRead` 兜底，`waitForReadyRead` 必然超时。

**Qt 6.8 的实际做法是两条腿**（拉取实源确认）：
- 事件循环内：`WSAAsyncSelect` + 窗口消息（`qeventdispatcher_win.cpp`）；
- 同步等待：`select()`（`qns_win.cpp:1495 QNativeSocketEnginePrivate::nativeSelect`）。

本框架没有独立窗口消息泵，故**统一采用 `select`**：每次 poll 现问现判，
**没有需要维护和复位的事件对象状态，也就没有「漏置位」这一故障面**。

> 教训：`WSAEventSelect` 与 `WSA_FLAG_OVERLAPPED` 套接字、以及本进程的
> 套接字创建顺序组合下行为不确定；Qt 不用它做事件循环通知，值得信任这个信号。

## 三、Qt 语义对齐清单（已完成）

### 3.1 `XIODevice_read_1`（`XIODevice.c`）

对照 `QIODevice::read`（qiodevice.cpp:988）与 `QIODevicePrivate::read`（:1046）：

| # | 原实现 | Qt 语义（已对齐） |
|---|---|---|
| 1 | `maxlen <= 0` 返回 -1 | `maxSize < 0` 才报错（`CHECK_MAXLEN`）；**`0` 返回 0** |
| 2 | `!m_d` 与读权限混判 | 分开：参数守卫 → `CHECK_READABLE`（只要 `ReadOnly` 位） |
| 3 | 缓冲区不足才补读 | 同 Qt：先吃缓冲区、剩余额度交给 `readData`（短读合法） |
| 4 | 底层出错**无条件**返回 -1 | `readFromDevice < 0 && readSoFar == 0` 才 -1；否则返回已取到的量 |
| 5 | — | `readData == 0` 视为 EOF，返回已取到的量 |

### 3.2 `XAbstractSocket`（`XAbstractSocket.c`）

| 函数 | 对齐要点 |
|---|---|
| `connectToHost` | **补 `XIODevice_open_base(self, mode)`**（对齐 `QIODevice::open(openMode)`，qabsock.cpp:1679）。此前只写进 `options`，设备自身 `m_openMode` 恒 `NotOpen` ⇒ `read_1`/`write_1` 全在守卫处被拒（实测 `read_1 NOT READABLE mode=0x0`） |
| `VXAbstractSocket_readData` | 未连接 ⇒ `maxSize ? -1 : 0`；设备层「暂无数据」折算为 **0**（Qt 引擎 -2 → 0），不是错误 |
| `waitForReadyRead` | ①未连接 ⇒ false；②连接中先 `waitForConnected`；③只有 Connected/Bound 才继续等；④`msecs < 0` 不超时 |
| `waitForBytesWritten` | ①未连接 ⇒ false；②连接中先等连接；③**写缓冲为空 ⇒ false**（原实现返回 true，语义相反） |
| `WaitForConnected` | 超时 ⇒ 置 `SocketTimeoutError` + 转 Unconnected（不再静默返回 false） |
| `atEnd` | `NotOpen || (缓冲区空 && bytesAvailable==0)`，不再按 state 判定 |
| `abort` | 先 close 再置 Unconnected（对齐 Qt 的「先丢弃待写、再走统一 close 收尾」顺序） |
| `readLineData` | `maxlen < 2` 拒绝（`CHECK_LINEMAXLEN`） |

### 3.3 一处**故意不重载**

`EXIODevice_BytesAvailable` **保持不重载**。基类 `VXIODevice_bytesAvailable`
（`XIODevice_virtual.c:139`）已经是「返回当前读通道 ring buffer 可用量」，
正是套接字需要的语义。若重载并回调 `XIODevice_bytesAvailable_base`，会经虚表
回到本类自身形成**无限递归**（实测栈溢出，cdb 栈里 `XIODevice_bytesAvailable_base`
↔ `VXAbstractSocket_bytesAvailable` 交替 250+ 帧）。

> Qt 里 `QIODevice::bytesAvailable()` 调的是非虚的基类实现，不存在该问题。

## 四、验证结果

| 测试 | 结果 |
|---|---|
| `--test xtcpserver` | **18 通过 / 0 失败** |
| `--test xsocket-stall` | **9 通过 / 0 失败** |
| `--test xlink` | **820 通过 / 0 失败** |
| `--test xlink-quality` | **72 通过 / 0 失败** |
| `--test ntp`（UDP 路径） | **83 通过 / 0 失败**（本轮实测；原记 82） |
| `--test xssl` | **PASS**（含本地 TLS 1.2 握手） |
| `--test ddns` | **112 通过 / 0 失败**（原 97/15，根因见 §5） |

回退验证：`XNETWORK_WIN_BACKEND=IOCP` 变体独立编译通过；该变体下
`XDeviceNetwork_win32_select.c.obj` 被完全裁空（0 符号），
`XDeviceNetwork_win32.c.obj` 导出完整 18 个套接字 API。

## 五、DDNS/XLINK 收口（2026-11-04 第二轮）

### 5.1 三个根因（按发现顺序）

| # | 缺陷 | 后果 | 修复 |
|---|---|---|---|
| 1 | `XAbstractSocket_setSocketState` 进入 UnconnectedState 时把 `m_openMode` 置 NotOpen | 「数据先到、FIN 后到」时（HTTP/1.0 Connection: close 的常态），已进 ring buffer 的字节被 `read_1` 的 CHECK_READABLE 拒掉 → 空应答 | **删除该赋值**，对齐 Qt：`disconnectFromHost` 不清 openMode，只有显式 `close()` 才清（qabsock.cpp:2593/2612） |
| 2 | select 后端每次 `select` 报可写就投递 Write 完成 | TCP 建连后几乎恒可写 ⇒ 每次 poll 一个写事件（实测单次 ddns 20 万条）；`XAbstractSocket_event` 用陈旧 `writeFinishedBytes` 反复发 `bytesWritten`，XLink 文件传输进度对账错乱 | 只在 `writePending`（等价 Qt「writeBuffer 非空」）时投递；`socketWrite` 入队时置位、`xw32_drainWrite` 排空后复位 |
| 3 | `xw32_armRead` 无「单缓冲占用」门控 | 上一批未消费时再次 recv，覆盖 `readBuffer` + `readFinishedBytes`，同一帧被重复投递（实测 17 字节回显交付两次） | 恢复 `readPending` 门控（与 IOCP 后端同契约），`ContinueRead` 是唯一解除点；`closed` 后不再重复投递 0 字节完成 |

### 5.2 定位方法（可复用）

- 临时追踪用 `XNETWORK_SELECT_TRACE=1` 环境变量（排查后已全部删除）。
- 判定链：`[XSEL] poll → armRead got=N → postReady → [XSOCK] evRead → [XHTTP] read_1`，
  一眼看出「数据到没到内核 / 有没有投递 / 上层读没读」三段各自的结果。
- 决定性证据：`waitRR=true avail=190` 紧跟 `read_1 NOT READABLE mode=0x0`，
  即「数据在、但设备已 NotOpen」——直接指向根因 1。

### 5.3 复验

```
ddns 112/0、xlink 820/0、xtcpserver 18/0、xsocket-stall 9/0、ntp 83/0、xssl PASS
```

## 八、约束遵守情况

- ✅ 保留 IOCP 后端，宏可切回
- ✅ 不新增线程：select 查询在既有事件环 poll 内完成
- ✅ XFd 口径不变：`m_fd` 仍由 `XFd_alloc` 分配，事件仍经 CQ 投递
- ✅ 平台差异下沉 `Drive/`：`Src/` 无平台分支
- ✅ 命名/注释规范：UTF-8 BOM、`X` 前缀、`m_` 成员、Doxygen 中文注释
- ✅ 临时诊断打点已全部删除（本轮排查用的 `XNETWORK_SELECT_TRACE` 亦已清理）

## 九、QAbstractSocket 行为对齐（2026-11-04 第三轮）

对照 `D:\Qt\6.8.3\Src\qtbase\src\network\socket\qabstractsocket.cpp` 逐项收口。
API 名称按本项目 C 规约映射（`XAbstractSocket_` 前缀 / `_base` 虚函数入口 /
`_signal` 信号），功能面已齐全；本轮修的是**行为**：

| # | 项目 | Qt 语义 | 修复 |
|---|---|---|---|
| 1 | `isValid()` | 引擎有效即 true（:1595） | 设备句柄有效即 true；`isValidFlag` 保留给描述符接管路径。原实现正常 `connectToHost` 后恒 false |
| 2 | `hostFound` | DNS 成功后、connect 前发一次（:975-979） | 原实现在 `HostLookup→Connected` 转换发射，而正常连接必经过 Connecting，**信号从未触发**；改由解析完成回调发射 |
| 3 | 异步 DNS | `connectToHost` 立即返回，HostLookup→解析→Connecting | **同线程事件驱动**：解析在调用线程的事件循环里执行，完成后用 `XEVENT_TYPE_HOST_LOOKUP` 事件回到 socket。**不新增线程、不使用线程池**（区别于 Qt 的 QHostInfo 工作线程）；代价是解析期间占用一次事件循环调用 |
| 4 | `close()` | `QIODevice::close()` + `disconnectFromHost()`（:2593） | 关闭后补状态收敛到 Unconnected（发 disconnected）。原实现只关设备，状态机停在 Connected |
| 5 | `disconnectFromHost()` | 有待写数据时延迟断开（:2612-2665） | 新增 `disconnectPending`：ClosingState 后等写缓冲排空，由写事件/等待循环收尾；不再丢弃未发完的写 |
| 6 | `setReadBufferSize()` | 限制用户态缓冲 + 满时关闭读通知（:2736-2747） | 新增 `readPaused`：ring 达到上限即不再 ContinueRead，应用读走后恢复。原实现只设 `SO_RCVBUF` |
| 7 | `bind(port)` / `Any` | `QHostAddress::Any` 双栈（:1580） | `bindAny`、`XTcpServer_listen(NULL)`、`setAnyAddress(Any)` 统一改双栈（AF_INET6 + V6ONLY=0）；两个 Win32 后端补 v4-mapped 发送与 `sa2addr` 还原。原实现 UDP 只绑 IPv4 |
| 8 | `setSocketError()` | 受保护 setter 只写字段（:2797） | 拆为 `setSocketError`（只写）+ `setSocketErrorAndEmit`（写+发信号）；内部调用点改用后者 |
| 9 | `connectToHost(QHostAddress)` | 无解析、无 hostFound | `connectToHostByAddress` 直接走 `socketStartConnect`，不再绕 hostName 重载 |

新增回归用例（`--test xsocket-stall` 第 10 项）：
`connectToHost("localhost")` 立即返回且处于 HostLookupState → 恰好一次
hostFound → 连接后 `isValid()==true` → `close()` 后状态 Unconnected 且
`isValid()==false`。

复验（select 与 IOCP 两套宏均跑）：

```
ddns 112/0、xlink 820/0、xtcpserver 18/0、xsocket-stall 10/0、
ntp 83/0、xssl PASS、xmqtt 全部通过（含双进程联调）
```
