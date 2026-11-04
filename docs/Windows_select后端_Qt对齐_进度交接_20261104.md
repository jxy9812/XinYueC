# Windows 套接字后端：对齐 Qt 6.8 的 select 后端 —— 进度交接（2026-11-04）

> **状态：主体已落地，DDNS/HTTP 端到端仍有 15 项待收口。**
> 两套后端并存、宏切换可用；TCP 服务端与套接字读挂起回归全绿。

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
| `--test ntp`（UDP 路径） | **82 通过 / 0 失败** |
| `--test xssl` | **PASS**（含本地 TLS 1.2 握手） |
| `--test ddns` | **97 通过 / 15 失败**（见 §5） |

回退验证：`XNETWORK_WIN_BACKEND=IOCP` 变体独立编译通过；该变体下
`XDeviceNetwork_win32_select.c.obj` 被完全裁空（0 符号），
`XDeviceNetwork_win32.c.obj` 导出完整 18 个套接字 API。

## 五、未收口项：DDNS 端到端 15 项

### 5.1 现象（已精确定位到一环）

```
[XNET-SOCK] waitReady sock=...5E0 state=3 avail=0 msecs=150
[XNET-SOCK] waitReady loop sock=...5E0 state=3 avail=0
[XNET-SOCK] toRing sock=...5E0 rb=...C8620 n=64 avail=64   ← 数据确实进了 ring
[XDNS] probe fail url=http://127.0.0.1:.../ip body=[]        ← 但收包为 0
```

**矛盾点**：`toRing` 成功把 64 字节写进该 socket 的 ring buffer（`avail=64`），
但 `waitForReadyRead` 的循环日志里 `avail` 恒为 0，且 `probe fail` 的 body 为空。

### 5.2 已排除

| 排除项 | 证据 |
|---|---|
| 后端 recv 失败 | `[XNET-WSA] dispatch ... readable` + `toRing n=64 avail=64` |
| `read_1` 参数/权限被拒 | `read_1 NOT READABLE mode=0x0` 已消失（`connectToHost` 补 openMode 后） |
| `bytesAvailable` 虚槽重载问题 | 基类实现即 ring buffer 可用量，无需重载（见 §3.3） |
| fd 复用串扰 | 事件槽已改为按 **owner 指针** 寻址，`toRing` 的 socket 指针与 `waitReady` 的指针一致 |
| 数据被别的 socket 消费 | `toRing` 的 `sock` 与 `waitReady` 的 `sock` 是**同一个指针** |

### 5.3 下一步建议（按优先级）

1. **测单次 `processEvents` 耗时**：日志显示 `waitReady` 循环只转了一次就退出，
   怀疑第一次 `processEvents` 内部接近吃满 150ms。在循环里打印每次迭代的
   `XDateTime_currentMSecsSinceEpoch()` 差值即可判定。
2. **`XDdnsHttp_get` 的收包循环**：`XIODevice_waitForReadyRead_base(sock, 150)`
   返回 true 后立刻 `read_1`；若 `read_1` 返回 0 就 `break`。检查
   `read_1` 里 `XRingBuffer_read` 与 `VXIODevice_bytesAvailable` 是否读同一
   通道（两者都用 `XIODevice_currentReadChannel`，理论上一致，但值得实测打印
   ring 指针）。
3. **对比 IOCP 后端的时序**：IOCP 下该测试 112/112 全绿。差异在于 IOCP 是
   **常驻异步读**（连接建立即挂读），数据一进内核就进 `readBuffer`；select 后端
   要等 poll 时机。可考虑在 `socketSetDescriptor`/`connect` 完成后立即
   摆一次 `xw32_armRead`（已有类似逻辑，确认是否被 `readPending` 挡住）。

## 六、临时诊断开关（当前仍在代码里，收口时须清理）

`XNET_WSA_DEBUG=1` 环境变量打开，输出前缀：

- `[XNET-WSA]` — 后端 dispatch/recv/write/post
- `[XNET-SOCK]` — `XAbstractSocket` 的 `waitReady`/`toRing`/`evRead`
- `[XNET-RING]` — 事件环 poll
- `[XDNS]` / `[XDNS-HTTP]` — DDNS 探测与收包

**收口时必须全部删除**（这些是排查期临时打点，不符合「诊断打点须被编译开关门
包裹」的规范；若确需保留，应改为 `XNETWORK_WIN_SELECT_TRACE` 之类的
默认 0 专属开关）。

## 七、其他待清理项（本次工作区遗留）

- `docs/reg_m512_*.txt`、`docs/rtu_probe_*.txt` — 其他会话的调试输出
- `bfull.bat` — 根目录临时构建脚本
- `verify/ld_meter/`、`verify/temphum_probe/`、`verify/verify_hsf_flowmeter.py` — 其他会话产物

## 八、约束遵守情况

- ✅ 保留 IOCP 后端，宏可切回
- ✅ 不新增线程：select 查询在既有事件环 poll 内完成
- ✅ XFd 口径不变：`m_fd` 仍由 `XFd_alloc` 分配，事件仍经 CQ 投递
- ✅ 平台差异下沉 `Drive/`：`Src/` 无平台分支
- ✅ 命名/注释规范：UTF-8 BOM、`X` 前缀、`m_` 成员、Doxygen 中文注释
- ⚠️ 待清理：§6 的临时诊断打点
