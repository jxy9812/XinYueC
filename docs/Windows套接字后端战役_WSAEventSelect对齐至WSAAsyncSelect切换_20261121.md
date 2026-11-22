# Windows 套接字后端战役总文档：WSAEventSelect 对齐 → 未修项闭环 → WSAAsyncSelect 切换（2026-11-21）

> 本文档为 2026-11-21 Windows 套接字后端战役的**合并总记录**（原三份文档并入，
> 内容原样保留、按时间序分 Part）：
>   Part I  —— WSAEventSelect 后端按 Qt 6.8 口径全面整改 + 覆盖扩充（四轮复审）
>   Part II —— 未修项清单（T-1~T-13）与 Wave A/B/C 闭环计划
>   Part III —— 窗口消息事件源收敛进 XNetIoRingWin32 + 缺省后端切 WSAAsyncSelect
>                + 双源等待 + IOCP err=64 分类修复 + 跨进程联调复查
>
> 各 Part 内部的「§n」引用均指**该 Part 内**的节号。最终状态速览见 Part III 附录三。
> 原三份独立文档已删除（git 历史可查对应提交）。

# Part I：WSAEventSelect 后端按 Qt 6.8 口径全面整改 + 覆盖扩充

# Windows WSAEventSelect 套接字后端：Qt 6.8 语义对齐 + 回归覆盖扩充

> 日期：2026-11-21
> 范围：`Drive/windows/Device/Network/XDeviceNetwork_win32_select.c`（WSAEventSelect 后端）、
> `Src/XCore/XCode/XNetwork/XAbstractSocket/XAbstractSocket.c`（套接字框架层）、
> `Drive/windows/Core/XNetIoRingWin32.c`（就绪轮询表）、
> `Test/XIOTest/XSocketStallTest.c` / `Test/XIOTest/XSocketPipeTest.c`（新增套件）、
> `verify/run_socket_gates.bat`（新增门禁脚本）。
> 后端宏：`Src/XCore/XCode/XNetwork/XNetwork_config.h:294`
> （`XNETWORK_WIN_BACKEND` = `XNETWORK_WIN_BACKEND_WSAEVENT`）。

---

## 0. 起因与基线

用户提问「现在的 WSAEventSelect 好像也有问题吧」→ 开工作流全面修。

**动手前的实测基线**（不是推测）：

| 门禁 | 改前 | 改后 |
|---|---|---|
| `xsocket-stall` | 15 / 0 | **23 / 0**（新增 8 条用例） |
| `xlink-client` | 92 / 0 | 92 / 0 |
| `xmqtt-tcp-api` | 49 / 0 | 49 / 0 |
| `xconsole-shell-backend` | 通过 | 通过 |
| `udp-directed` / `udp-bind-family` | **不在门禁里** | **14 / 0、10 / 0（进门禁）** |
| `xsocket-pipe` | **不存在** | **4 / 0（新建套件）** |
| C 总回归 `verify\run_regression.bat c` | 1099 / 0 | 1099 / 0 |

**改前配置头里的口径已经过期**：`XNetwork_config.h` 记载「select 后端对端断开检测慢、
实测 1510ms 未察觉、四套 socket 断言红」——那是被 2026-11-18 的 FD_CLOSE 计数根修
（随 `3ae1ff49` 合入）修掉的老行为；本轮开工前四套门禁实测**已全绿**。
**过期注释本身就是债**：下一个人会照着它去查一条已经不存在的问题。

---

## 1. 三路并行调查（结论摘要）

### 1.1 Qt 6.8 契约（从 code.qt.io 取的实源）

关键事实（`src/network/socket/qnativesocketengine_win.cpp` 等，tag `6.8.0`）：

- 引擎层**不用 overlapped I/O**：`WSARecv`/`WSASend` 的 OVERLAPPED 传 0/nullptr。
- **一次通知一次读**（引擎层）；但上层 `QAbstractSocketPrivate::readFromSocket()`
  **循环读直到 would-block** ⇒ 「末批数据 + FIN」在**同一轮**里就看到 EOF。
- 读侧 `WSAECONNRESET`/`WSAECONNABORTED` **归一成 0 字节**（=EOF，与真 FIN 不可区分）。
- `WSAEWOULDBLOCK` 是独立哨兵（Qt 用 -2），**不是错误**。
- 写侧 `WSASend` 循环；遇 `WSAEWOULDBLOCK` 跳出并返回**已写字节数**；
  `WSAENOBUFS` 用 ≤49152 的小块重试，**不判死**。
- `setErrorFromWSAError()` 给出 WSA→`SocketError` 的固定映射，
  且**同一 WSA 码在 bind 与 connect 路径映射不同**（`WSAEADDRINUSE` 是反例）。
- 异步 connect 靠 `getsockopt(SO_ERROR)` 判定，且**读出后必须显式清零**。
- Qt 的 Windows **事件循环**侧其实用 `WSAAsyncSelect`（隐藏窗口消息），
  `select()` 只出现在**同步等待 API** 里；全库 `WSAEventSelect` 命中 0。
  ⇒ 本仓用 WSAEventSelect 替代是**换壳**，要对齐的是**位集合 + 水平触发 + 事件原子取走**。
- Qt 网络层**没有半关闭契约**（全文无 `shutdown`）⇒ 不能援引 Qt 作半关闭的依据。

### 1.2 本仓差距（A/B 两种形态，详见 §2）

### 1.3 覆盖盘点

- 「软」的：就绪事件模型（等待窗 ≫ 事件环 20ms 轮询片 ⇒ 唤醒链全断也过）、
  写背压（数据量远小于 Windows 发送缓冲 ⇒ 背压从未发生）、
  读缓冲暂停（断言恒真）、就绪唤醒时延（无时延断言）。
- 零覆盖：**优雅 FIN**、命名管道、>128 并发、Qt 错误码映射、半关闭（框架无 API）。

---

## 2. 修复清单

### 2.1 后端 A 级真 bug（会静默卡死 / 丢 EOF / 静默失联）

| 编号 | 问题 | 位置 | 修法 |
|---|---|---|---|
| A1 | **写侧致命错误被吞成「已排队」** ⇒ 写通道**静默永久卡死**（不报错、不报连接死、ring 无限增长） | `XDeviceNetwork_win32_select.c` 写提交/写续发两处 | 照抄 IOCP 后端的 `w32_isFatalWriteError` 分类表：致命错误 ⇒ 投递关闭事件 + 置连接死 + 如实返回失败；`WSAENOBUFS` 按 Qt 口径用 ≤49152 小块重试（不判死） |
| A2 | **直取路径吞 EOF**（`recv` 返 0 只 `return 0`，不置 closed、不投递关闭）⇒ FD_CLOSE 位已被 Enum 取走时 **EOF 永久丢失** | 同上，`XDeviceNetwork_socketRead` | 走与 `xw32_armRead` 0 字节分支**完全相同**的收口 |
| A3 | **一次就绪只读一次** ⇒ EOF 被推迟到上层 ContinueRead 之后 | `xw32_armRead` | 改成「一次就绪读到 would-block 为止，但**只投递一次**读完成」（Qt `readFromSocket` 口径）；EOF 与末批数据同轮可见 |
| A4 | **槽表满静默失联**（`addWsaEvent` 返回 false 被丢弃） | `xw32_armSocket` | 检查注册结果，失败必须显式冒泡，不得静默 |
| A5 | **同一轮两次 Enum** ⇒ 事件位「取走但丢弃」（connectPending 分支 `outRead==NULL`） | `xw32_pollEvents` / 派发处 | 一次 Enum 取回全部位并缓存进 `p->pendingEvents`，各分支读缓存 |
| A6 | **异常分支是死代码**（FD_CLOSE 同时进可读与异常掩码 ⇒ `x==true` 蕴含 `r==true` ⇒ 异常分支永不可达） | 派发处 | 关闭成为**独立于可读**的事件；关闭前先取走内核残余数据（Qt `canCloseNotification`） |
| A7 | 换 socket 时**未复位 `probeArmed`** ⇒ 新 socket 永不就绪 | `socketConnectLocal`/`socketConnect` | 凡替换 `p->socket` 处一律复位 |
| A8 | connect 失败**不关 socket、不摘表** ⇒ 槽位与 fd 泄漏 | connectPending 分支 | 失败路径统一摘表 + `closesocket` |
| A9 | 命名管道 `HANDLE` 强转 `int` 进 `CQEntry.m_osFd` ⇒ 截断后可能 ≤0，被框架的 `m_osFd <= 0` 守卫跳过 | `xw32_postReady` | 管道填 -1（不适用关闭判定），EOF 由后端直投关闭事件 |

### 2.2 Qt 语义对齐

| 项 | 口径 | 位置 |
|---|---|---|
| 读侧 RST/ABORT | 归一成 0 字节（=EOF），与 Qt `nativeRead` 一致 | `xw32_recvOnceInto` |
| `FD_CONNECT` | 显式映射（旧注释说「select 没有该位」是 select 时代的理由） | `xw32_selectOne` / 派发 |
| `SO_ERROR` | 读出后**显式清零**（Qt 注释：MSDN 说会清，实际没清，会污染后续 connect） | connect 判定分支 |
| `WSAEISCONN` | 按「已连接」处理，不再当硬失败 | `socketConnect` |
| 写侧 `WSAENOBUFS` | ≤49152 小块重试，不判死 | `xw32_sendOnce` |
| **错误码映射** | WSA/errno → `XAbstractSocket_SocketError`，逐条照 Qt `setErrorFromWSAError`（含 bind/connect 分路径差异） | `XAbstractSocket.c` 新增 `socketErrorFromPlatform` |
| 对端关闭 | 报 `RemoteHostClosedError`（Qt `readFromSocket`），此前**只转断开不报错误** | `XAbstractSocket.c` 关闭事件分支 |

新增跨平台钩子 `XDeviceNetwork_socketLastError(XFd)`（按套接字记录原始 OS 错误码），
Windows 两套后端 + POSIX 后端各一份实现。

### 2.3 框架层两处真缺陷（被新用例逼出来的）

1. **读缓冲上限暂停后恢复死等**
   `VXAbstractSocket_readData` 的「恢复读通知」判定排在 `maxlen == 0` 早退**之后**，
   而设备层的读是「缓冲优先」（`XIODevice.c` 的 `xioRead`）⇒ 应用把缓冲读空时
   只会回调一次 `readData(data, 0)` 的「缓冲已空」钩子 ⇒ 恢复判定永远够不着
   ⇒ 平台侧不再投递新批次 ⇒ **剩余数据永远收不到**（实测：只收到首批 8192/65536）。
   修法：恢复判定移到早退之前。
2. **背压被当成错误**
   后端把 `WSAEWOULDBLOCK` 记进「最近一次错误」⇒ 框架按它映射出
   `TemporaryError`，把真正的关闭原因顶掉（实测：对端优雅关闭后 errorOccurred
   报成 22=TemporaryError 而不是 1=RemoteHostClosedError）。
   修法：背压**不进错误通道**（读、写、管道三处）。

### 2.4 命名管道：整条通道从「死代码」到可用

改前管道在本后端**从来就没通过**（三处叠加）：
① 派发入口 `if (p->socket == INVALID_SOCKET) return;` 把管道整个跳过；
② 管道连通走 `socketConnectLocal`，**不经过**置 `autoRead` 的两条路径 ⇒ 读推送的闸门恒关；
③ 读环空直取路径把管道排除在外 ⇒ 应用主动读也拿不到数据。

修法：派发入口分流到 `PeekNamedPipe` 探数据；管道连通置 `autoRead`；
管道句柄置 `PIPE_NOWAIT`（非阻塞，否则 `ReadFile` 会卡死事件循环）⇒ 直取路径可用；
写侧 `ERROR_BROKEN_PIPE` 判死（不再当「瞬时错误」入队）；
管道没有写就绪事件 ⇒ 记住写环指针，在就绪派发里主动 drain（背压续发）。

---

## 3. 测试覆盖扩充

### 3.1 `xsocket-stall` 15 → 23 条

| 用例 | 锁的契约 | 改前状态 |
|---|---|---|
| 优雅 FIN 及时察觉 | 对端 FIN ⇒ ≤1500ms 收敛到 Unconnected | **零覆盖**（原先只覆盖 RST） |
| read 不超请求量 | 一次 `read(n)` 返回 ≤ n | **红**（真 bug：recv 按 8192 读、忽略调用方请求长度 ⇒ 越界） |
| 1MB 大数据完整性 | 大块逐字节一致 | 新增（背压触达在 TCP 上不可确定性构造 ⇒ 降级为信息输出） |
| 就绪唤醒时延 ≤50ms | 数据到达 → 可读的时延上界 | **无时延断言**（旧窗口 ≫ 事件环 20ms 片 ⇒ 恒真） |
| 读缓冲上限真的生效 | 暂停期可用字节 ≤ 上限；恢复后收齐 | 旧断言**恒真**（上界取的是发送总量） |
| 断链信号与状态顺序 | `disconnected` 恰好一次 + 发信号时状态已收敛 + 错误码 = `RemoteHostClosedError` | 零覆盖 |
| 对端关闭后写干净失败 | 写必须失败，不得静默成功 | 新增 |
| 并发上限显式失败不静默失联 | 上限内全通；超出必须**显式失败**，不得静默失联 | 零覆盖 |

并发上限的**真实值**是 63 条而不是拍脑袋的 130：`XFD_TABLE_SIZE=128` 个设备，
一条连接占 2 个（客户端 + 服务端 accept 出来的），外加监听套接字 ⇒ 1+2N ≤ 128。

### 3.2 新套件 `xsocket-pipe`（4 条）

命名管道与 TCP 在本仓共用一套上下文但收发实现完全不同（`CreateFileA` +
同步 `ReadFile`/`WriteFile`），**不走** WSAEventSelect/recv/send ⇒ TCP 那 20 多条
回归锁一条都覆盖不到它。覆盖：双向收发逐字节一致 / 对端关管及时察觉 /
64KB 大块不截断 / **写背压触达 + 1MB 续发完整**。

背压的**确定性覆盖放在管道**而不是 TCP：Windows 的 TCP 发送缓冲与接收窗口都带
自动调节，loopback 上一次 `send` 就能把 1MB 全收下（实测 `SO_SNDBUF` 已压到 4096
仍不返回 `WSAEWOULDBLOCK`）；管道的缓冲是固定的 ⇒ 写满必然触发背压。

### 3.3 门禁

新增 `verify\run_socket_gates.bat`（4 套 → 7 套，含 UDP 两套与新管道套件）、
`verify\build_static.bat`（增量）与 `verify\rebuild_full.bat`（改公共头宏后必须全量重编）。

---

## 4. 验证

### 4.1 门禁（复审修正后重跑）

> 复审修正涉及框架层 `XAbstractSocket.c` 与三个消费方（MQTT/S7/Modbus），
> 故门禁与总回归在修正后**重跑一遍**，结论不变。

```
verify\run_socket_gates.bat  → SOCKET GATES ALL GREEN - ran 7 suites
  xsocket-stall 23/0 · xsocket-pipe 4/0 · udp-directed 14/0 · udp-bind-family 10/0
  xlink-client 92/0 · xmqtt-tcp-api 49/0 · xconsole-shell-backend 通过
verify\run_regression.bat c → ALL GREEN - ran 3 steps（C 总回归 1099/0）
```

### 4.2 A/B 实证：两套后端跑同一批新测试

见 §6。

### 4.3 定位手法留档

- **A/B 定位「新引入 vs 既有」**：把后端改动单独 stash 回原版重编，跑同一批新用例
  ⇒ 原版 4 红 / 新版 3 红 ⇒ 新版**修好了 `read不超请求量` 这个真 bug**，
  且**没引入新失败**；剩下 3 红是**既有潜伏缺陷**。
- **「必须触达背压」这条断言本身是环境相关的**（Windows TCP 缓冲自动调节）⇒
  改成「TCP 侧只锁大数据完整性 + 管道侧锁确定性背压」，避免假绿与假红两头。

---

## 5. 遗留与建议

1. **管道修复只落在 WSAEventSelect 后端**（`XDeviceNetwork_win32.c` 的 IOCP pipe 分支
   仍是老口径）⇒ A/B 里 IOCP 后端下 `xsocket-pipe` 有 2 条红，且**会挂住**。
   若要把 IOCP 也扶正，需照搬同一套（派发分流 + `autoRead` + NOWAIT + 断链判死 + 写环续发）。
2. **半关闭无契约**：Qt 网络层没有 `shutdown` 语义，本仓框架也没有
   ⇒ 「对端半关后本端仍可读」这条要么补 API + 补测，要么在台账里显式写明不覆盖。
3. **`bytesToWrite()` 恒 0**：框架的写直接下发（不排队），`XIODevice_bytesToWrite_base`
   对套接字恒返回 0 ⇒ 与 Qt 的 `bytesToWrite()`（返回排队字节数）不等价。
   本轮靠「管道背压 + 写环」间接覆盖，真正的 Qt 等价实现需要框架层接管写缓冲。
4. **事件环轮询节拍**：只要有一个套接字登记，`waitForEvents` 必然在第一个 10ms 片后返回
   ⇒ 主循环节拍被钉在 ~100Hz，套接字就绪的最坏发现延迟 = 一个片 + 一圈循环。
   这是现状，不是本轮引入；如需改善要改事件环的等待策略。
5. **SO_ERROR 归零**只在 connect 判定分支做了；其它读 `SO_ERROR` 的地方需自查。
6. **复审带出的其余遗留**（详见 §8.4）：`readBufferSize` 与 Qt `canReadNotification()`
   的交错语义、`FD_READ` 边沿/水平语义的回归锁、「直取 vs 就绪派发」的并发互斥、
   命名管道「对端仍阻塞模式」的混用（MySQL over named pipe 典型场景）、
   连接失败错误码的端到端断言、**POSIX 侧没有对应门禁**（板端 Linux 那条链
   目前无自动化守护）。

---

## 6. A/B 实证结果

同一棵工作树，只翻 `XNETWORK_WIN_BACKEND` 一个宏 + 全量重编（1412 个目标），
跑**同一批**新用例：

| 套件 | WSAEventSelect（缺省） | IOCP（对照） | 判读 |
|---|---|---|---|
| `xsocket-stall`（**27 条**，含本轮新增 12 条） | **27 / 0** | **26 / 1** | ✅ 新用例锁的是**跨后端契约**：27 条里 26 条在 IOCP 下同样通过；唯一差异是「bind 占用端口报 AddressInUseError」——**IOCP 后端当时没记 bind 的原始错误码**（本轮已补：bind 失败时 `p->lastError = WSAGetLastError()`） |
| `xsocket-pipe`（4 条，本轮新建） | **4 / 0** | **挂死 → 超时判失败（180s）** | ⚠ 管道修复只落在 WSAEventSelect 后端（IOCP 的 pipe 分支是另一套实现）；顺带**实测了门禁的超时保护有效**（挂死被抓住，没有把门禁拖死） |
| 其余 5 套（UDP×2 / xlink / mqtt / console） | 全绿 | 全绿 | — |

⇒ **socket 层的新覆盖与后端无关**（两套后端都全绿）；**管道层尚未覆盖 IOCP 后端**
（§5 遗留项 1）。

### 6.1 A/B 过程本身的两个坑（都实踩）

1. **脚本里 `set -e` 会把树留在错误的后端态**：门禁在 IOCP 下非零退出 ⇒ `set -e`
   让脚本在「恢复 WSAEVENT」之前就中止 ⇒ **宏停在 IOCP、产物也是 IOCP 版的**
   ⇒ 后续增量编译会编出「宏说 IOCP、头文件已改回」的混合体。
   修法：A/B 脚本**不用 `set -e`**，恢复步骤必须无条件执行
   （`.zcode/tmp/ab_backend.sh` 已改）。
2. **门禁失败会把测试进程留在后台**：管道套件在 IOCP 后端下会**挂住**（不是失败退出）
   ⇒ `XinYueC_Static.exe` 残留 ⇒ 下一次构建 **LNK1168（产物被占用）**
   ⇒ 而且残留进程会被反复 respawn（后台任务还在跑门禁）。
   纪律：跑门禁前先 `taskkill /F /IM XinYueC_Static.exe`；构建失败先查 LNK1168，
   再查是不是有残留进程占着产物。

---

## 7. 过程留档：本轮怎么定位的

| 手法 | 收获 |
|---|---|
| **A/B 定位「新引入 vs 既有」** | 把后端改动单独 stash 回原版重编，跑同一批新用例 ⇒ 原版 4 红 / 新版 3 红 ⇒ 新版**修好了「read 不超请求量」这个真 bug**（原版 `recv` 按 8192 读、忽略调用方请求长度 ⇒ 越界写调用方缓冲），且**没引入新失败**；剩下 3 红是**既有潜伏缺陷**。没有这一步就会把既有缺陷算到本轮头上（或反之）。 |
| **「必须触达背压」是环境相关断言** | Windows TCP 发送缓冲与接收窗口都带自动调节 ⇒ loopback 上一次 `send` 收下 1MB（`SO_SNDBUF` 压到 4096 也一样）⇒ TCP 上**无法确定性构造背压** ⇒ 该断言只能改口径（TCP 侧锁大数据完整性、背压锁到管道）。 |
| **失败信息带诊断** | 新用例失败时打印「写环待发 = N」「SO_SNDBUF 回读」等 ⇒ 一步定位到「缓冲自动调节」而不是猜。 |
| **门禁脚本要能单独跑** | `verify\run_socket_gates.bat` 逐套判退出码（崩溃的负码不被当绿）+ **每套件带挂钟超时**（挂死的套件要**判失败**而不是把门禁一起挂住）⇒ 任何人都能用一条命令复现本轮结论。 |

---

## 8. 独立复审（新增上下文、只读、逐条核实）

改动完成后另起一个**没看过实现说明**的上下文做复审（只读 + 静态核实，不跑构建），
拿到 **12 条必修 / 8 条测试质量 / 8 条遗漏 / 6 条风险**。处置如下（**全部已修**）：

### 8.1 必修项

| 编号 | 复审发现 | 处置 |
|---|---|---|
| **P1** | **TCP 直写分支仍把 `WSAEWOULDBLOCK` 写进 `lastError`**（UDP 分支/读侧/管道两侧都改对了，唯独 TCP 直写漏了）⇒ 「对端正常关闭」会被报成 `TemporaryError` | 已修：TCP 直写分支的 `WSAEWOULDBLOCK`/`WSAENOBUFS` 一律**不进错误通道** |
| **P2** | 框架层把「按套接字记录的码 = 0」当成「未知」，回退线程级 `WSAGetLastError()`（**成功调用不会把它清零**）⇒ 线程上任何历史失败都会污染判定 | 已修：有设备时**只用**按套接字记录的码；0 的语义改成「本代连接没有失败过」；线程级码只在设备尚未打开时用 |
| **P3** | POSIX 后端返回线程级 `errno` ⇒ 非阻塞 socket 的 `EAGAIN` 恒为 11 ⇒ **板端（Linux）每次对端正常断开都被报成「临时错误」**，而门禁只在 Windows 跑 | 已修：POSIX 后端新增**按套接字**的 `lastError`（读完成时更新；瞬态错误与 0 字节读都不算错误） |
| **P4** | `lastError` 跨连接代不清理 ⇒ 上一代的错误码会被当成本代的 | 已修：新增 `xw32_resetConnState()`，换代/断开统一清零（连接失败路径**保留** `failCode`） |
| **P5** | Win32 管道错误码（109/232/233）落进 POSIX errno 表解释（Linux 的 errno.h 里 109 = `EADDRINUSE`） | 已修：后端对**管道**错误用**负值**标记来源，框架层按 Win32 管道码解释（109/233 ⇒ 对端已关管；232 ⇒ 背压） |
| **P6** | `socketSetDescriptor` / `serverSetDescriptor` 换 socket 时没清事件位缓存 ⇒ 上一代残留的 `FD_CLOSE` 位会让**新连接一上来就被判死** | 已修：两处补 `xw32_resetConnState()` |
| **P7** | `FD_READ` 位从不被消费 ⇒ `pendingEvents` 对收过数据的连接永不归零 ⇒ 每轮 poll 都白跑一次 `recv` | **保留现状并写进注释**：这条「每轮白读一次」正是内核数据滞留时的兜底；`FD_READ` 的边沿/水平语义在本仓仍是**未验证假设**，已在代码里降级表述 |
| **P8** | 管道写侧背压依赖「`PIPE_NOWAIT` 句柄上 `WriteFile` 缓冲满时立刻失败」这一 Win32 语义假设 | **已被实测证实**：管道套件的「写背压触达 + 1MB 续发完整」用例通过 ⇒ 假设成立（写满确实返回 `ERROR_NO_DATA` 而不是阻塞） |
| **P9** | 两套 Windows 后端对 `ERROR_NO_DATA(232)` 的解释相反（IOCP 判「对端已关闭」，select 判「背压」） | 记为遗留（IOCP 的管道路径整体未修，见 §5.1） |
| **P10** | **UDP 把「零长数据报」当 EOF** ⇒ 会把 UDP 连接误判为死 | 已修：UDP 的 0 字节按「一帧空数据报」处理（对齐 Qt：`nativeRead` 只在 TCP/SCTP 上把 0 当 EOF） |
| **P11** | `socketWrite` 忽略写环的**部分写入** ⇒ 数据静默丢失却上报「全部受理」 | 已修：如实上报「实际受理量」 |
| **P12** | 注释与代码事实相反（管道 `autoRead` 的必要性论证） | 已修：订正为「建上下文时已置 true ⇒ 这一行冗余；真正原因是派发入口跳过 + 直取路径排除管道」 |

### 8.2 风险面：**最高危的一条**（D1）

新加的「对端关闭 ⇒ `RemoteHostClosedError`」会打到**六个既有消费方**。
逐个核实后：

| 消费方 | 行为 | 处置 |
|---|---|---|
| `XMqttClient` | 正常断开被报成 **`XMqttProtocol_fail(TransportInvalid)`（协议失败）** | **已修**：对端关闭按「连接已断」收尾（与 `mqtt_on_disconnected` 同款），不报协议失败（对齐 `QMqttClient`：transport 错误 ⇒ client 错误 ⇒ DisconnectedState） |
| `XS7TcpClient` | 正常断开多报一条 `XPlcDevice_ConnectionError` | **已修**：仅在「非对端关闭」或「连接未建立/握手中」时报错 |
| `XModbusTcpClient` | 正常断开多报一条 `XModbusDevice_ConnectionError` | **已修**：同上 |
| `XNetworkAccessManager`（HTTP） | 把 socket 错误映射成自己的错误空间 | **不动**：Qt 的 `QNetworkAccessManager` 同样报 `RemoteHostClosedError` ⇒ 本仓与 Qt 同口径 |
| `XFtp` | 同上 | **不动**（同上） |
| `XGuiClient` | 同上 | **不动**（同上） |

⇒ 修完这三处后，「对端正常关闭」在 MQTT/PLC/Modbus 上**不再被误报为故障**，
而 HTTP/FTP/GUI 保持与 Qt 一致。

### 8.3 测试质量项

| 编号 | 发现 | 处置 |
|---|---|---|
| Q1 | T6 的错误码断言锁在最脆弱的一环（依赖 P1/P2 先修好） | 随 P1/P2 一并解决；另加「同对象重连不继承上一代错误码」的语义到 `xw32_resetConnState` |
| Q2 | T8（并发上限）**锁不住它声称要锁的 A4**：触发失败的可能是 fd 表满而不是 WSA 表满 | 保留用例但**改口径**：断言改成「上限内全通 + 超出必须**显式失败**（不得静默失联）」，并在注释里写明「WSA 表与 fd 表同在 128 ⇒ 两条上限同时到达，**无法在集成层单独区分 A4**」 |
| Q3 | T4 的 50ms 时延窗口太紧（事件环轮询片 10ms + 主循环 ≤20ms） | 已修：放宽到 **200ms**（仍远小于旧的 1500ms，判别力保留） |
| Q4 | 旧用例 `avail <= kTotal` 恒真 | 已修（此前已改为 `avail <= 1024 + 8192`） |
| Q5 | T3 名字叫「1MB真背压完整性」但已不测背压 | 已修：改名「1MB大数据完整性(背压未触达)」 |
| Q6 | 管道套件的反向读取不抽事件 ⇒ 只覆盖直取路径 | 记为遗留（直取路径与派发路径**两条都已被覆盖**：用例 1 走直取、用例 4 的续发走派发） |
| Q7 | 管道套件缺整体超时；`WriteFile` 若阻塞会挂死而非失败 | 已修：**门禁脚本每套件带挂钟超时**（180s，超时判失败，退出码 124） |
| Q8 | 文件头说「三组」实际四组 | 已修 |

### 8.4 遗漏项（记录为遗留，本轮不展开）

- **M1** 映射表的注释宣称「逐条照 Qt」但不准确（比 Qt 多 8 项、缺 `WSAEISCONN`）⇒ **已订正注释**（列出 Qt 实际覆盖的 11 项 + 本仓扩展项）。
- **M2** Qt 每次 `send` 封顶 49152 字节、`WSAENOBUFS` **不在同一轮忙重试** ⇒ **已修**：直写路径封顶 49152；`WSAENOBUFS` 直接按背压返回（缩块交给下一轮续发）。
- **M3** `readBufferSize` 语义与 Qt `canReadNotification()` 的差异（暂停期内内核又到数据的交错）未覆盖 ⇒ 遗留。
- **M4** `FD_READ` 的边沿/水平语义仍无回归锁 ⇒ 遗留（已在代码里降级表述为「未验证假设」）。
- **M5** 「直取」与「就绪派发」并发取数的互斥只靠一个标志 ⇒ 遗留（单线程事件循环下窗口极小；多线程部署是真实竞态）。
- **M6** 命名管道「对端仍在阻塞模式」的混用未覆盖（MySQL over named pipe 的典型场景）⇒ 遗留。
- **M7** 连接失败的错误码分类没有端到端断言（refused / host-unreachable / timeout）⇒ 遗留。
- **M8** POSIX 侧没有对应门禁 ⇒ 遗留（板端 Linux 的那条链无自动化守护）。

### 8.5 复审的最终确认

复审员逐条核实了本轮声称的 12 条改动，结论：**第 3/4/5/6/7/8/10/12 条完全正确**；
第 1/9 条「部分对（漏了 TCP 直写分支）」⇒ 已修；第 11 条「接口加了但语义不成立」
⇒ 已修（P2/P3/P4/P5）。复审员另行核实并确认了这些事实：
- 「全仓唯一调用 `WSAWaitForMultipleEvents` 的地方」成立；
- 「0 字节读完成 ⇒ 框架 PEEK 复核」链路成立；
- 「本端主动 close **不会**报 `RemoteHostClosedError`」（SOCK_CLOSE 只由两个后端的
  `postSockClose` 产生）；
- 新测试的注册链路完整（新 `.c` 由 `GLOB` 自动收录，构建脚本每次重跑 `cmake`）；
- 新增公共符号 `XDeviceNetwork_socketLastError` **当前不会断链**
  （lwIP 后端不提供 socket API；ESP8266 因 `XNETWORK_ON=0` 被编译掉；
  两套 Windows 后端互斥编译）—— 已在头文件里写明「每个平台后端必须实现」。

---

## 9. 第二轮：三轮复跑 +「各种异常形态」覆盖补齐

### 9.1 三轮复跑（找偶发红）

同一棵工作树、**全量重编**后的产物，连跑三轮：

```
round 1: 7 OK / 0 FAIL
round 2: 7 OK / 0 FAIL
round 3: 7 OK / 0 FAIL
verify\run_regression.bat c → ALL GREEN（C 总回归 1099/0）
```

⇒ 无偶发红（此前的 1098/1 是批跑环境下的偶发项，本轮三轮 + 总回归均未复现）。

### 9.2 覆盖缺口复查：找出「**修了但没锁**」的洞

把 Qt 6.8 契约清单逐条对照「本仓实现 × 测试锁」，发现上一轮**修了几处真 bug 却没写回归锁**：
背压不进错误通道（P1）、UDP 零长数据报（P10）、写环部分写入（P11）、
`SO_ERROR` 读后清零（B2）、错误码映射的 refused/unreachable 分支（M7）、
bind 失败的原因（**原先一个错误码都不报**）。

### 9.3 新增覆盖：「突然断网」等异常形态（4 条）

| 用例 | 锁的契约 | 为什么重要 |
|---|---|---|
| **对端活着但不读不关**（`sst_peer_alive_but_silent`） | 写**成功**、**无 errorOccurred**、state 仍 Connected | **「突然断网」在传输层根本查不出来**：拔网线/对端假死后，TCP 只表现为「写成功、读无数据、无错误」，要等重传耗尽（分钟级）才报 ETIMEDOUT ⇒ 兜底**只能靠应用层心跳**（XLink 静默判线 / HSF 心跳）。这条把事实钉死，免得日后有人以为「平台层应该能发现对端假死」而去加无效逻辑 |
| **RST 被察觉后写必失败**（`sst_write_after_peer_reset_fails`） | 写侧 RST 是硬错误，不得「报告成功但数据进黑洞」 | 锁 Qt `nativeWrite` 的写侧口径（`ret=-1` + `NetworkError` + 关 socket） |
| **bind 已占用端口 ⇒ 报 `AddressInUseError`**（`sst_bind_address_in_use_reports_error`） | bind 失败要报**具体原因** | 本仓原先 bind 失败**一个错误码都不报**（只 `return false`）⇒ 上层分不清「地址被占用/权限不足/本地地址不可用」。**顺带修了框架层**：`VXAbstractSocket_Bind` 的失败路径现在按 Qt `nativeBind` 的映射报具体错误码 |
| **UDP 零长数据报不是 EOF**（`sst_udp_zero_length_datagram_not_eof`） | UDP 的 0 字节是「一帧空数据报」，不是关闭 | Qt 只在 TCP/SCTP 上把 0 字节当 EOF；本仓曾把 UDP 的 0 字节走 EOF 分支 ⇒ **UDP 连接被误判为死**（现场：设备发空帧后链路莫名断开）。用例用**原始套接字**发 0 长度数据报（框架的 `writeDatagram` 明确拒绝 `size<=0` ⇒ 只能从外面发） |

### 9.4 本轮踩的坑：批量替换把 helper 自己的函数体也换掉 ⇒ 无限递归

给后端加 `xw32_resetConnState()`（统一清「代内状态」）时，用**按文本匹配的批量替换**
把各处 `p->pendingEvents=0; p->eofPending=false; p->errPending=false;` 换成调用它 ——
**匹配器把新函数自己的函数体也换了** ⇒ 函数**调用自己** ⇒ 无限递归 ⇒ 栈溢出。

- **表征**：三个 socket 套件都在**第一条用例**就退出，退出码 **127**；
  `hsf-regression` 却全绿（1099/0）⇒ 不是全局问题，第一直觉会误判成「某个新用例崩了」。
- **定位**：`cdb` 抓栈（`D:\Windows Kits\10\Debuggers\x64\cdb.exe`）⇒ 栈上全是
  `xw32_resetConnState+0x1e` 自递归 ⇒ 一眼定位。
- **修法**：恢复 helper 函数体；并做一次「全量自查：函数体内是否出现自身函数名调用」。
- **教训**：① 批量文本替换**必须排除被新增的函数本身**，或改完立刻 review 差异；
  ② 改完**必须重编 + 重跑门禁**（本轮一度出现「改了但没被编进去」的错觉）；
  ③ 退出码 127 + 只输出了头 ⇒ 优先怀疑栈溢出/加载失败，不是业务断言失败。

### 9.5 覆盖完整性的最终结论

**已锁**（有回归、跨后端一致）：就绪事件模型、一次一投递、EOF/FIN、RST、写侧致命错误、
写不超请求量、背压（管道侧确定性）、读缓冲上限暂停/恢复、就绪唤醒时延、断链信号与状态顺序、
错误码（对端关闭 / bind 占用）、并发上限显式失败、对端静默（突然断网形态）、UDP 零长数据报。

**仍未锁（明确列为遗留，不假装覆盖）**：
- `FD_READ` 的边沿/水平语义（代码里已降级表述为「未验证假设」）
- 「直取」与「就绪派发」并发取数的互斥（单线程下窗口极小；多线程是真实竞态）
- 命名管道「对端仍阻塞模式」的混用（MySQL over named pipe 典型场景）
- `SO_ERROR` 读后清零、ENOBUFS 小块重试（实现已改，无独立断言）
- `bytesAvailable` 的「引擎缓冲 + FIONREAD」口径
- **POSIX 侧没有对应门禁**（板端 Linux 那条链目前无自动化守护）

---

## 10. 第三轮：门禁自查 + 覆盖补齐（含两个「自己挖的坑」）

### 10.1 ⚠ 先说结论：上一轮的三轮「全绿」是**假绿**

给门禁脚本加超时保护时用了 PowerShell 的 `Start-Process -PassThru` ⇒ 那个
`Process` 对象的 **`ExitCode` 恒为 NULL**（除非先取一次 `$p.Handle` 或对它调
`WaitForExit()`）⇒ `exit $null` ⇒ **退出码 0** ⇒ **每个套件都被判成「通过」**，
哪怕它红着。

⇒ 上一轮「三轮全绿 + 总回归 ALL GREEN」的结论**不成立**，必须重做。
⇒ 修法（`verify\run_one_suite.ps1` + `verify\run_socket_gates.bat`）：
改用 .NET `System.Diagnostics.Process` 直接起进程（`ExitCode` 可靠），并且
**在门禁里加一条负对照自检**：

```
---- [step 1] SELFCHECK-negative-control ----
[FAIL] SELFCHECK-negative-control exit=1
[OK] negative control behaved as expected (gate can detect failures)
```

即：**先跑一条必然失败的命令，门禁必须判它失败**；判不出来就直接 `exit 3`
中止整轮（后面的结果没有意义）。⇒ 门禁自己也要有「门禁能不能抓漏」的证据。

### 10.2 ⚠ 批量替换把 helper 自己的函数体也换掉 ⇒ 无限递归 ⇒ 栈溢出

给后端加 `xw32_resetConnState()`（统一清「代内状态」）时，我用**按文本匹配的批量
替换**把各处

```
p->pendingEvents = 0; p->eofPending = false; p->errPending = false;
```

换成调用它 —— **匹配器把新函数自己的函数体也换了** ⇒ 函数**调用自己** ⇒ 无限递归。

| 现象 | 说明 |
|---|---|
| 表征 | 三个 socket 套件都在**第一条用例**就退出，退出码 **127**；而 `hsf-regression` 全绿（1099/0）⇒ 第一直觉会误判成「某个新用例崩了」 |
| 定位 | `cdb` 抓栈（`D:\Windows Kits\10\Debuggers\x64\cdb.exe`）⇒ 栈上全是 `xw32_resetConnState+0x1e` 自递归 ⇒ 一眼定位 |
| 修法 | 恢复 helper 函数体；并加一次「全量自查：函数体内是否出现自身函数名调用」 |
| 教训 | ① 批量文本替换**必须排除被新增的函数本身**，改完立刻 review 差异；② 改完**必须重编 + 重跑门禁**；③ 退出码 127 + 只输出头部 ⇒ 优先怀疑栈溢出/加载失败，不是业务断言失败 |

### 10.3 又挖出一个**从未工作过**的路径：写环续发

把 `send` 按 Qt 口径封顶 49152 字节（`nativeWrite` 的 `qMin(49152, …)`）之后，
1MB 写入立刻暴露出：**只发出 49152 字节，其余 999424 卡在写环里**，
服务端只收到 49152。

⇒ **根因**：`WSAEventSelect` 的 `FD_WRITE` 是**边沿**语义 —— 投递一次之后，
若「可写」这个**条件**仍成立，AFD **不一定**再报一次 ⇒ 只靠事件驱动的写环，
数据一旦进队列就**永远推不出去**。
⇒ 这条路径此前**从未被触发过**：旧实现一次 `send` 就把整个缓冲发完了，
短写/续发是**死代码**，缺陷被彻底掩盖。
⇒ **修法**（Qt 口径：「写缓冲非空 ⇒ 使能写通知；写完 ⇒ 关掉」⇒ 等价于
**由持有写缓冲的一方自保活**）：后端每轮 poll **无条件**推一次写环
（且必须放在「本轮有无事件位」的早退**之前**，否则无事件位时续发被一并跳过）。

### 10.4 bind 失败**一个错误码都不报**（Qt 的 `nativeBind` 是有映射的）

- 本仓 `VXAbstractSocket_Bind` 失败只 `return false` ⇒ 上层分不清
  「地址被占用 / 权限不足 / 本地地址不可用」。
- 修：按 Qt `nativeBind` 的映射报具体错误码。
- **附带一个机制问题**：bind 失败时**设备随后就被销毁** ⇒ 框架层拿不到
  「按套接字记录的码」；而线程级的 `WSAGetLastError()` 在这条路径上**已被冲掉**
  （实测读到 0）⇒ 新增**线程级留存槽**
  （`XDeviceNetwork_setLastOperationError` / `..._lastOperationError`，
  TLS 存储，多线程互不串味），在设备销毁**之前**把原始码留下来。

### 10.5 新增覆盖（4 条「异常网络形态」）

| 用例 | 锁的契约 |
|---|---|
| **对端活着但不读不关** | 「突然断网」在传输层**查不出来**：写成功、无错误、仍 Connected ⇒ 兜底只能靠**应用层心跳** |
| **RST 被察觉后写必失败** | 写侧 RST 是硬错误，不得「报告成功但数据进黑洞」 |
| **bind 已占用端口 ⇒ `AddressInUseError`** | bind 失败要报具体原因（顺带修掉了「什么都不报」） |
| **UDP 零长数据报不是 EOF** | UDP 的 0 字节是「一帧空数据报」，不是关闭（用原始套接字发 0 长度帧） |

并发上限用例也改成**自适应**：上限内 60 条必须全通（断言不变），「超出」那一半
一直加到真的失败为止，并断言那次失败是**显式**的。

### 10.6 这一轮的最终状态

```
verify\run_socket_gates.bat（带负对照自检）→ ALL GREEN - ran 7 suites
  自检：负对照被判 FAIL ✓（门禁能抓漏）
verify\run_regression.bat c → ALL GREEN（C 总回归 1099/0）
```

---

## 11. 覆盖面是否全了（逐条 Qt 6.8 契约 × 本仓 × 回归锁）

「已锁」= 有**能在旧实现上真的红**的回归用例；「间接」= 被别的用例覆盖到但没有专门断言。

| Qt 6.8 契约 | 本仓实现 | 回归锁 |
|---|---|---|
| A1 socket 建出来即非阻塞；accept 后也非阻塞 | ✓ | 间接 |
| A2 就绪模型（位集合 / 水平触发 / 事件原子取走） | WSAEventSelect「关联一次 + 每轮 Wait(0)+Enum」 | ✓（就绪唤醒时延 ≤200ms）＋ ⚠ 位集合**逐位对齐**无锁 |
| A3 可读 / 可写 / 异常三类**并列**上报 | ✓（FD_CLOSE 独立，不再被可读吞掉） | ✓（优雅 FIN 用例） |
| B1 一次就绪一次读（**只投递一次**读完成） | ✓（读到 would-block 为止，但只投递一次） | ⚠「只投递一次」本身无直接锁 |
| B2 读到 0 字节 = EOF（对端 FIN） | ✓ | ✓ 优雅 FIN 及时察觉 |
| B3 读侧 RST/ABORT 归一成 0 字节 | ✓（Qt `nativeRead` 口径） | ✓ `sst_peer_reset_detected` |
| B4 would-block 是独立哨兵、**不进错误通道** | ✓（本轮修：TCP 直写分支原来漏了） | ✓（T6 的 `RemoteHostClosedError` 断言间接锁） |
| B5 「就绪但读不到」要复核（Qt 用 4096 字节重读） | ✓（框架 MSG_PEEK 复核） | ⚠ 无直接锁 |
| C1 写返回**实际写出**字节数（可短写） | ✓ | ✓ 1MB 完整性 |
| C2 写关注开关不变量（缓冲非空⇒保持关注） | ✓（本轮补「写环自保活」） | ✓ 1MB 续发完整 |
| C3 写侧 RST 是**硬错误**（关 socket + 报错） | ✓ | ✓ RST 被察觉后写必失败 |
| C4 每次 send 封顶 49152；`ENOBUFS` 不忙重试、按背压返回 | ✓（本轮补） | ⚠ `ENOBUFS` 分支本身无直接锁 |
| C5 可写事件到来时循环排空写缓冲 | ✓ | ✓（1MB 续发） |
| D1 WSA/errno → `SocketError` 映射（**同码在 bind/connect 映射不同**） | ✓（本轮补，含线程级留存槽） | 部分：✓ 对端关闭、✓ bind 占用；⚠ refused / host-unreachable / timeout **无端到端断言** |
| D2 状态机（bind/listen/connect/失败归零） | ✓ | 间接 |
| D3 异步 connect 用 `SO_ERROR` 判定，且**读后清零** | ✓（本轮补） | ⚠ 无直接锁 |
| E1 `bytesAvailable` = 引擎缓冲 + FIONREAD | ✓ | ⚠ 无直接锁 |
| E2 FIONREAD 是**下界**语义；UDP 的「1 字节」特例 | 下界 ✓；UDP 特例未实现 | ⚠ |
| E3 读缓冲上限：暂停 / 恢复 | ✓（本轮修「恢复死等」） | ✓ 两条（新旧各一） |
| E4 `readyRead` 递归保护 | 框架层（非本后端） | ⚠ |
| F1 `FD_CLOSE` 是独立事件类型；关闭前先把残余数据取走 | ✓（本轮补） | ✓ 优雅 FIN |
| F2 读关注位 / 写关注位与 Qt 一致 | 全集掩码（**有意偏离**：本后端不能重发 `WSAEventSelect`） | ⚠ |
| F3 每事件取消重挂 vs 关联一次 | 关联一次（有意偏离，有实锅：重发会挂在 AFD 端点锁） | ⚠ 边沿语义已在代码里**降级表述为「未验证假设」** |
| F4 accept 后覆盖继承的事件关联 | ✓ | ✓ 并发上限用例 |

### 11.1 异常网络形态（第二轮补）

| 形态 | 锁的行为 | 回归锁 |
|---|---|---|
| **拔网线 / 对端假死**（活着但不读不回） | 写成功、无错误、仍 Connected ⇒ 传输层查不出来 | ✓ 对端活着但不读 |
| **对端进程被杀**（RST） | 写必须失败；错误码可辨 | ✓ RST 察觉后写必失败 |
| **对端优雅关闭**（FIN） | ≤1500ms 收敛到 Unconnected + `RemoteHostClosedError` | ✓ 优雅 FIN |
| **地址被占用** | bind 报 `AddressInUseError` | ✓ bind 占用端口 |
| **设备发空帧**（UDP 零长数据报） | 不是 EOF、不判死 | ✓ UDP 零长数据报 |
| **写入量超过单次发送上限** | 续发完整、逐字节一致 | ✓ 1MB 完整性 |
| **并发超过容量** | 显式失败，不静默失联 | ✓ 并发上限 |
| **写侧链路死**（管道关管） | 报「对端已关闭」，不是静默成功 | ✓ xsocket-pipe |

### 11.2 结论

**核心可观测契约已全部有锁**；剩下 9 条未锁的，分三类：

1. **需要单元级夹具才能测**（D1 的 refused/unreachable/timeout、D3 的 SO_ERROR 清零、
   E1 的 bytesAvailable 口径、B5 的假 EOF 复核）—— 需要能**制造**特定平台错误码的夹具
   （本轮为 bind 做的「线程级留存槽」已经是这条路的雏形）。
2. **属于框架层而非本后端**（E4 readyRead 递归保护）。
3. **本仓有意偏离 Qt**（F2 位集合、F3 关联一次）—— 偏离理由有实锅背书（重发
   `WSAEventSelect` 会在 AFD 端点锁上永久悬挂），已在代码注释里写明并降级为
   「未验证假设」。

**仍无自动化守护的**：POSIX 侧（板端 Linux）**没有对应门禁** —— 本轮所有门禁都在
Windows 上跑；板端那条链目前靠人工联调。

---

## 12. 第四轮：对抗式复审（换新上下文、只读、逐条核实）后的必修

用户追问「确定没问题了吗」⇒ 换**全新上下文**做对抗式复审（不给结论、要它找问题），
拿到 **4 条必修 + 15 条建议**。逐条核实后处置如下（**全部已修**）：

### 12.1 必修

| 编号 | 问题 | 根因 | 处置 |
|---|---|---|---|
| **M1** | **写环续发在「短写 + 后续直写整包成功」时永久搁浅** | `socketWrite` 直写整包成功时**无条件** `writePending = false`，而**直写之前**的 `drainWrite` 可能因背压停下、环里还剩数据 ⇒ 出现「**环非空但标志假**」⇒ 自保活条件 `writePending && writeRing` 不成立、`FD_WRITE` 分支也不成立 ⇒ **尾巴数据永远发不出去**（不报错、不推进） | 写关注标志一律按**写环的实际占用**重算（与 `xw32_drainWrite` 同口径） |
| **M2** | **线程级留存槽跨操作串味** | 槽只有 bind 失败会写、**从不清零**；connect 失败却**优先**读槽 ⇒ 同线程「先 bind 失败 → 再 connect 失败」时，connect 会**报上一次 bind 的原因**（真因主机不可达，却报成权限不足） | 每次 open **之前**先清零；connect 失败也写自己的码 |
| **M3** | **管道错误码符号不一致** | `xw32_dispatchPipe`（Peek 路径）写**正数** 109/233，而读/写路径写**负数** ⇒ Peek 路径的「对端关管」落进 errno 表 ⇒ 报成 `NetworkError` 而非 `RemoteHostClosedError` ⇒ **MQTT/S7/Modbus 三个守卫在管道场景全部失效** | 管道错误码一律用**负值**标记来源 |
| **M4** | **lwIP / STM32 目标链接失败** | `XDeviceNetwork_socketLastError` 在跨平台头里**无条件声明**、被框架无条件调用，但 lwIP 与 STM32 后端**没有实现** ⇒ 编嵌入式目标 ⇒ `unresolved external symbol` | 给 `XDeviceNetwork_lwip.c` 与 `board_stubs.c` 各补一个 `return 0` 的实现 |

### 12.2 值得改但不阻塞（已挑有实质影响的做掉）

- **W11** 测试槽 `sst_on_error` **漏了 `XVarList_start`**：同一份 XVarList 是**所有槽共享**的（框架逐槽传同一个 args，读取会推进游标）⇒ 别的槽先读过就会读到**错位**的值 ⇒ 已补（本仓其它消费方都老老实实调了，唯独测试漏了）。
- **W4** MQTT 守卫缺「**连接中**」例外：代理在 CONNECT 握手期间关连接属于**失败**（不是正常断开）⇒ 原写法会静默吞掉握手失败、客户端既不报错也不重连 ⇒ 已补例外。
- **W8** 门禁**不校验二进制新鲜度**：改了源码没重编 ⇒ 跑的是旧二进制 ⇒ 假绿 ⇒ 门禁开头加「exe 必须比所有源码新」检查（否则 `exit 2`）。
- **W6 / W7 / W13 / W14** 注释与实现相反、函数名与实际行为不符 ⇒ 已逐条订正
  （POSIX `lastError` 的语义、ENOBUFS「下一轮用更小的块」的真实口径、1MB 用例名、
  两处残留注释）。
- **W15** `writeRing` 跨代不清理 ⇒ 已并入 `xw32_resetConnState` 的「代内状态」。

### 12.3 复审员**核实过、确认没问题的**（28 条要点摘录）

- 无限递归事故确实修好了（`xw32_resetConnState` 无自调用，调用点语义正确）
- 读侧 RST 归一成 0 字节与 Qt 一致，且 `lastError` 在归一**之前**写入 10054 ⇒ 关闭事件仍能区分 RST
- 背压确实不进错误通道（读侧/TCP 直写/UDP/管道两处都查过）
- `recv` 长度确实尊重调用方请求量 ⇒ **T2「read 不超请求量」是真锁**（改前是越界写）
- 事件位「一次 Enum」落实（connectPending 分支不再单独 Enum）⇒ A5 修好
- `FD_CLOSE` 已独立、旧的「异常分支永不可达」消除
- `SO_ERROR` 读出后显式清零、`WSAEISCONN` 按「已连接」收口
- 凡替换 `p->socket` 处都复位了 `probeArmed` 并清代内状态
- connect 失败会摘表 + closesocket 且保留 `lastError` 作证据
- 管道 `m_osFd` 不再塞 64 位 HANDLE；管道真的进了就绪轮询表且注册失败会冒泡
- `XIODevice_bytesAvailable`（非虚）与 `_base`（虚）**命名相反容易看错**，但 T4/T2 的判据**用对了函数**
- 读缓冲「恢复死等」真修好（判定移到 `maxlen==0` 早退之前 + 读后二次判定）
- 框架 0 字节读完成 → PEEK 复核 → 只发 SOCK_CLOSE、**不再发 0 字节读 act** ⇒ 关闭事件不重复投递
- 本地 close **不产生** SOCK_CLOSE ⇒ 新增的「关闭报 RemoteHostClosedError」只对端关闭生效
- 门禁假绿根因已消除（.NET Process + 两次 WaitForExit + 字符串比较 errorlevel）
- `XRingBuffer_write` 会**自动扩容** ⇒ 1MB 不会被静默截断，P11 的「如实上报实际受理量」成立
- 命名管道的套件注册链路完整、`PIPE_NOWAIT` 用法正确、管道一轮只读一次（不卡事件循环）

### 12.4 复审指出的**仍然存在**、本轮明确不修的

- **W2** `FD_READ` 位永不清除 ⇒ `pendingEvents` 常驻 ⇒ 每轮 poll 都进读分支做一次 `recv`
  尝试（~100Hz/套接字）。不是正确性问题，但与「事件位一次取回并缓存」的设计意图不符。
  **这是刻意保留的**：它正是内核数据滞留时的兜底（`FD_READ` 的边沿/水平语义在本仓
  仍是**未验证假设**，已在代码里降级表述）。
- **W9** 负对照自检是**空壳**：它只验证「任意非零退出码能被识别」，不验证「套件内部失败
  会被转成非零退出码」。⇒ 真实价值在**首次**：任何「套件失败 ⇒ 退出码非零」的回归
  都会被立刻抓到。局限已在此写明。
- **W10** 单套件 180s 超时对 `xsocket-stall` 偏紧（新用例里有 20s/30s 的等待窗口）。
- **W12** 「并发上限」用例**锁不住 A4**：fd 表（128）与 WSA 就绪表（128）容量相同，
  且 **fd 分配先于 `addWsaEvent`** ⇒ 溢出时先炸 fd 表，`addWsaEvent` 返回 false 的
  分支在集成层根本走不到 ⇒ **把 A4 回退，该用例仍然绿**。要单独锁 A4 需要能「只填满
  WSA 表而不填满 fd 表」的夹具。已在用例注释与本文档里写明这条限制。
- **W5** 本地 close 之后仍可能补发 `errorOccurred`（设备已销毁 ⇒ 退回线程级
  `WSAGetLastError()`，可能是陈旧值）。

---

## 13. 第五轮：补齐「WSAEventSelect 侧」最后两处 + 功能缺口归零

回答「WSAEventSelect 端是否全部修复」时，把复审里**属于本侧**的条目重新过了一遍，
又补了两处 + 一个功能缺口：

| 项 | 内容 | 处置 |
|---|---|---|
| **W1** | **`WSAEventSelect` 关联失败后会每轮重试关联** —— 而「重发关联」正是本仓记录过的那个 hang（对端已 FIN 的病态句柄上重注册会**永久挂**在 AFD 端点锁上，cdb 栈：NtDeviceIoControlFile ← WSAEventSelect）。`probeArmed` 只在成功时置 true ⇒ 一次失败就进入「每轮重发」的形态 | 关联失败**即记死**：记原始错误码 + 关 socket + 摘表，由上层走「连接已死」的既有路径收口，**绝不让它进入每轮重试** |
| **W5** | 关闭事件路径在设备已失效时**退回线程级陈旧码**（可能是上一次别的操作留下的 `WSAEWOULDBLOCK`）⇒ 把「对端正常关闭」误报成 `TemporaryError` | 设备已失效时按「没有失败过」处理（pcode=0 ⇒ `RemoteHostClosedError`） |
| **E2** | Qt 的 **UDP `FIONREAD` 特例**本仓没实现：UDP 上 `FIONREAD` 报 1 时可能是「一个 **0 长度**数据报」正在等待 ⇒ 上层 `bytesAvailable()` 会报 1 而实际读不到字节 | 照 Qt `nativeBytesAvailable()` 补上：`pending==1 && UDP` 时用 `recvfrom(MSG_PEEK)` 探一下（能 peek 到 ⇒ 真数据；peek 失败 ⇒ 是空数据报 ⇒ 待读 0） |

⇒ 至此，**WSAEventSelect 侧已知的功能缺口归零**。

### 13.1 最终验收（全量重编后）

```
verifyebuild_full.bat            → 1412 个目标，0 错
verifyun_socket_gates.bat  ×3    → ALL GREEN - ran 7 suites（每轮都含
                                     「二进制比源码新」+ 负对照判 FAIL）
verifyun_regression.bat c        → ALL GREEN（C 总回归 1099/0）
A/B（换 IOCP 再换回）             → IOCP：socket 层 27/0 全绿（仅管道套件红）；
                                     WSAEVENT：7 套全绿；树已恢复到 WSAEVENT
```

### 13.2 仍然不能说「100% 无遗留」的三类（如实列出）

1. **`FD_READ` 语义：由「未验证假设」升级为「已实测的 EDGE 约束」**（见 §14）。
   `verify\fd_read_edge_probe.c` 在本机实测确认：`FD_READ` 是**边沿**语义，数据滞留时
   AFD **不会重报** ⇒ 位**必须常驻**，不能消费。本仓**不能**重发 `WSAEventSelect`
   （重发会挂 AFD 端点锁）⇒ 只能靠位常驻 + 「读环空时直取内核」+「写环自保活」兜底。
2. **一处刻意保留、但现已证明是必需的机制**：`FD_READ` 位永不清除 ⇒ `pendingEvents`
   常驻 ⇒ 每轮 poll 多一次 `recv` 尝试（~100Hz/套接字）。**不是冗余**——探针证实
   数据滞留时 AFD 不重报，清掉位就会永久丢就绪；这正是正确性兜底。
3. **覆盖缺口**（产品行为已对，但**没有专锁**）：11 条 Qt 契约无专门回归 ⇒ 其中
   5 条需要「能制造特定平台错误码」的夹具（错误码分类、SO_ERROR 清零、bytesAvailable
   口径、假 EOF 复核、ENOBUFS 分支）、2 条属有意偏离（位集合、关联一次）、
   4 条是次要面（只投递一次、位集合逐位对齐等）；另 **A4 单独锁不住**（fd 表与 WSA 表
   同容量且 fd 先分配 ⇒ 集成层走不到 `addWsaEvent` 失败分支）。

### 13.3 跨端仍开着（本仓后端之外）

- **管道只在 WSAEventSelect 后端可用**（IOCP 的 pipe 分支仍是老实现 ⇒ A/B 下它会挂死，
  被门禁的超时抓住）。— **Wave A T-11 已移植，Gate 3 A/B 待验**。
- **POSIX 侧没有门禁**：板端 Linux 那条链仍靠人工联调；且 POSIX 的 `lastError`
  只在**读完成**时更新 ⇒ **写侧致命错误不记录**。— **Wave A T-12 已补写侧记录，
  T-13 门禁脚本待做**。

---

## 14. Wave A 闭环（承接未修项清单，2026-11-21）

用户「未修复的整理下直接开工作流闭环修复」⇒ 把 §13.2 / §13.3 的遗留拆成 T-1~T-13
（见 `docs/Windows_WSAEventSelect后端_未修项清单与闭环计划_20261121.md`），用
Wave A/B/C 分波闭环（工作流工具本会话不可用，改用多 Agent + 确定性门禁等价）。

### 14.1 Wave A 交付与 Gate 1

| 项 | 交付 | 状态 |
|---|---|---|
| T-1 `FD_READ` 语义探针 | `verify\fd_read_edge_probe.c` + `verify\run_fd_read_edge_probe.bat`；实测 **FD_READ = EDGE**（位必须常驻） | ✅ 完成；结论已写回两处代码注释 + §13.2 + 清单 T-1 |
| T-2 错误码注入夹具 | `Drive\windows\Device\Network\XDeviceNetwork_fault.h` + 读/写/connect/bind 注入点（SRWLOCK、默认关闭零成本、走真实收口路径） | ✅ 完成；Wave B 使用 |
| T-11 IOCP 管道移植 | `XDeviceNetwork_win32.c`（PIPE_NOWAIT / PeekNamedPipe 派发 / autoRead / 断链判死 / 写环续发 / 错误码负值标记） | ✅ 完成；Gate 3 A/B 待验 |
| T-12 POSIX lastError 写侧 | `XDeviceNetwork_posix.c` 写失败记录 errno + 背压不进错误通道 + 注释订正 | ✅ 完成；未本机编译 POSIX |

**Gate 1（全量重编 + 三轮门禁 + C 总回归）全绿**：
```
verify\rebuild_full.bat          → build=0 / ninja exit=0
verify\run_socket_gates.bat ×3   → 每轮 7 套 ALL GREEN（含二进制新鲜度 + 负对照自检）
verify\run_regression.bat c      → PASS 1099 / FAIL 0
```

### 14.2 因 T-1 结论取消的优化

原 Wave C C1「消费 `FD_READ` 位省一次 `recv`」**取消**：实测 EDGE 语义下位常驻是
正确性必需，消费会永久丢就绪。这条从「未验证假设」升级为「已验证约束」，
`run_fd_read_edge_probe.bat` 的退出码判断也已修（`echo` 里未转义的 `)` 会提前
闭合 `if` 块，使 `exit /b 3` 无条件执行 —— 这是本轮 bat 恒返回 3 的真根因）。

### 14.3 后续（Wave B / C）

- **Wave B**：T-3~T-10 用 T-2 夹具补专锁 + T-13 POSIX 门禁脚本 ⇒ Gate 2。
- **Wave C**：A/B 验 T-11（IOCP 管道）+ 独立复审 + 最终门禁 ⇒ Gate 3。

### 14.4 Wave B 闭环 + Gate 2（2026-11-21）

用 T-2 注入夹具给「行为已对但无专锁」的契约补上直接回归（`XSocketStallTest.c`，
socket 套件 27 → 33）：
| 项 | 契约 | 锁法 |
|---|---|---|
| T-3 | connect 错误码分类 | 注入 refused/host-unreachable/timeout ⇒ 断言各归 `ConnectionRefusedError`/`NetworkError`/`NetworkError`，且都不是 Unknown |
| T-4 | 错误码不串味 | 连续两次 connect 注入不同码 ⇒ 第二次不被第一次污染（框架 `lastOperationError` 槽语义；注意真实异步 connect 的 SO_ERROR 清零分支当前夹具无法注入，已诚实标注） |
| T-6 | 假 EOF 复核 | 注入读路径 would-block ⇒ 不断链、不进错误通道，解除后恢复 |
| T-7 | ENOBUFS 不判死 | 注入写路径 `WSAENOBUFS` ⇒ 连接不判死、不进错误通道，续发完整 |
| T-5 | bytesAvailable 口径 | 断言虚口径（读环+引擎/内核待读）≥ 仅读环口径 |
| T-9/T-10 | 夹具自检 | 武装/查询/清除、对照后端显式不支持 —— 门能抓漏的自证 |

T-8（A4「只填 WSA 表不填 fd 表」的夹具）经评估**未做**：WSA 表与 fd 表同容量、
fd 先分配，集成层走不到 `addWsaEvent` 返回 false 的分支，单独锁需要改事件环
暴露注入接口 —— 超出「测试代码扩展」范围，已在清单里保留限制说明。

**Gate 2（全量重编 + 三轮门禁 + C 总回归）全绿**：
```
全量重编        → build=0 / ninja exit=0（新用例无 C4133/错误）
run_socket_gates.bat ×3 → 每轮 7 套 ALL GREEN
run_regression.bat c    → PASS 1099 / FAIL 0
```

**A/B 实证（门能抓漏）**：临时把映射 `WSAECONNREFUSED → NetworkError` 打断，
全量重编后 socket 套件 **31 通过 / 2 失败**（T-3 refused 报 7 期望 0 当场变红，
证明新门有效）；恢复映射重编后回到 33/0。

**T-13**（POSIX 门禁脚本）：`verify/run_socket_gates.sh`（同一组 7 suite + 负对照
自检 + 新鲜度 + 超时，零 PowerShell 依赖）；`bash -n` 通过，`/bin/true` 对照触发
`exit 3` 证明能拦假绿。板端/WSL 上 `BIN=... bash verify/run_socket_gates.sh` 直接跑。

### 14.5 Wave C 独立复审（新上下文、只读）后的订正

换全新上下文做对抗式复审，8 项断言逐条核实**全部属实**（注入走同一收口、
A/B 门真能抓漏、ENOBUFS/would-block 不进错误通道等），未发现新的运行时逻辑
缺陷或内存问题。三处「注释/命名与实现不一致」已订正：

- **复审 A**：T-4 用例名 `SO_ERROR 清零` 名实不符（实际锁的是框架
  `lastOperationError` 槽清零，不是后端异步 `setsockopt(SO_ERROR,0)`）。
  已改名为 `connect 错误码不串味`（函数名/显示名/PASS 文案/文档同步），
  并在用例注释里明确「真实异步 SO_ERROR 清零分支当前夹具无法注入、未纳入
  自动回归」。
- **复审 B**：`xw32_consumeEvents` 的注释称「FD_READ 条件仍成立会被 AFD 重置、
  清除安全」——与 T-1 探针的 EDGE 结论**直接相反**。实际调用点从不传 FD_READ，
  无运行时 bug，但该注释若被人据以加 `consumeEvents(FD_READ)` 会立刻永久丢就绪。
  已订正为「绝不清 FD_READ，位必须常驻」。
- **复审 C**：ENOBUFS 注释称「本轮把块减半后重试」，但代码是第一次 ENOBUFS
  就按背压返回（由写环下一轮续发）；且第二个 ENOBUFS 分支不可达、
  `XNETWORK_SEND_NOBUFS_MAX_RETRY` 是死常量。已删死分支/死常量并订正注释为
  「下一轮用更小的块 = 写环续发，不是本轮减半重试」。

### 14.6 Wave C 收口：IOCP 管道移植 A/B + 最终门禁

T-11 的 IOCP 管道移植最初 A/B 时 case 3 挂死（超时 120s）。独立定位出**两处真根因**
（均在 `XDeviceNetwork_win32.c`，与 WSAEVENT 后端无关）：

1. **case 3 挂死 = 写槽收割死锁**：测试两次写之间不转事件环，而 IOCP 管道写是
   重叠 `WriteFile`，写槽归还只能靠完成包出队（`pollPlatform`）驱动；8 槽占满后
   第 9 写入写环，写环 drain 又等完成包 ⇒ 死锁。修法：`socketWrite` 管道分支写前
   就地调一次 `XAbstractNetIoRing_pollPlatform_base` 收割已完成写槽。
2. **case 4 丢数 = PIPE_NOWAIT 下 0 字节完成静默丢数**：管道缓冲满时重叠
   `WriteFile` 以 `ERROR_SUCCESS + 0 字节`完成，完成回调当成成功写出 0 字节 ⇒
   静默丢 64KB（实测完成总量恰少 64KB），1MB 永远收不齐。试过 `PIPE_WAIT`
   会破坏读路径（重叠读等满 8192 ⇒ err=996），已回退；改保留 `PIPE_NOWAIT`
   + 写槽记 `submittedLen`，完成回调把未写出尾部回退 `writeStash` 保序续发，
   并把管道写改**串行**（`pipeWritePending` 闸）避免回退次序错乱。

**A/B 实证**：IOCP 下 pipe 套件 4/4（连跑 4 次无抖动）；恢复 WSAEVENT 宏全量
重编后 pipe 4/4 无回归。当前树 = WSAEVENT 宏。

**最终门禁（Wave C Gate 3）**：全量重编 0 错误 + socket 门禁**三轮**全绿
（每轮 7 套 + 负对照 + 新鲜度）+ C 总回归 **1099/0**。

**结论**：用户「未修复的整理下直接开工作流闭环修复」的全部遗留（T-1~T-13，
其中 T-8/T-9b/T-10 因需改框架/事件环暴露接口、超出测试扩展范围而保留限制说明）
已按 Wave A/B/C 闭环；WSAEventSelect 侧功能缺陷归零，跨端 IOCP 管道与 POSIX
lastError 补齐，POSIX 门禁脚本就位。

---

# Part II：未修项清单（T-1~T-13）与闭环计划

# Windows 套接字后端：未修项清单与闭环计划

> 2026-11-21。承接 `docs/Windows_WSAEventSelect后端Qt对齐与覆盖扩充_20261121.md`
> §13.2 / §13.3（那里是「已修完什么 + 还开着什么」的完整叙述）。
> 本文件是**待办清单**：每条都有「为什么还开着 / 怎么验 / 完成判据」。

## 0. 一句话结论

**WSAEventSelect 侧的功能缺陷已归零**（§13 已补齐 W1 / W5 / UDP-FIONREAD）。
剩下的是三类：
① **验证类**（要先把「未验证假设」变成事实、要先有夹具）；
② **覆盖类**（行为已对，但没有能红的回归）；
③ **跨端类**（IOCP 侧管道未移植、POSIX 侧无门禁）。

---

## 1. 验证类（先做，结论会影响后续实现）

### T-1　探针：AFD 在「条件仍成立」时会不会**重报** `FD_READ`？　【已完成】
- **结论（2026-11-21 实测）**：**不会重报 ⇒ `FD_READ` = 边沿（EDGE）语义**。
  `verify\fd_read_edge_probe.c` 用纯 WinSock 在本机（Win10 x64 / Winsock 2.2）
  复现三种数据滞留形态，结论行：
  ```
  FD_READ still visible while data is UNREAD    : no
  FD_READ reported after the buffer was DRAINED  : no
  => FD_READ semantics : EDGE (FD_READ reported once per arrival, not re-armed while data is queued -> the bit must stay resident)
  ```
  case A（数据滞留不读）/ B（读走后无新数据）/ C（读走第一帧、第二帧仍排队）
  的 rounds 2..N **全部不再报** `FD_READ`；对照组 D（从未发数据）恒 no。
- **意义**：`FD_READ` 位**必须常驻**，不能消费；`pendingEvents` 常驻 + 每轮补一次
  `recv` 尝试是**正确性必需**，不是纯冗余。⇒ 原本设想的「消费掉 FD_READ 位省一次
  recv」优化（原 Wave C C1）**取消**。
- **落地**：结论已写进 `XDeviceNetwork_win32_select.c` 的两处注释（事件位语义块 +
  读接口 @note）；`verify\run_fd_read_edge_probe.bat` 已修好退出码判断
  （`echo ... (exit !CLRC!)` 里的 `)` 未转义会提前闭合 `if` 块，导致 `exit /b 3`
  无条件执行），现在 bat 与手动编译结论一致。
- **复跑**：`cmd /c verify\run_fd_read_edge_probe.bat`，退出码 0，读末尾
  `PROBE-SUMMARY` 行即可。

### T-2　夹具：让测试**能制造指定的平台错误码**
- **为什么开**：5 条 Qt 契约没有专锁（见 §2），根因是**没法在测试里制造**
  `WSAECONNREFUSED` / `WSAETIMEDOUT` / `WSAEHOSTUNREACH` / `WSAEACCES` /
  `WSAENOBUFS` 这类错误 ⇒ 只能靠「连一个真会失败的地址」，既慢又不确定。
- **怎么做**：给后端加一个**仅测试可用**的故障注入点（例如一个编译期/运行期开关，
  指定「下一次 socketWrite/socketRead/socketConnect 返回这个错误码」）。
  要点：① 生产构建默认关闭（宏或运行时开关，**不得**影响正常路径的性能）；
  ② 注入的错误必须走**与真实错误完全相同的收口路径**（否则测的是假路径）。
- **完成判据**：夹具可用 + 至少一条契约用上它（其余留给 §2）。

---

## 2. 覆盖类（行为已对，但没有能红的回归）

依赖 T-2 的夹具。

| 编号 | 契约 | 现在为什么没锁 | 完成判据 |
|---|---|---|---|
| T-3 | 错误码分类端到端（refused / host-unreachable / timeout 各归各的） | 真机上要靠「连一个真会失败的地址」，慢且不确定 | 用夹具注入三种错误 ⇒ 断言 `errorOccurred` 分别是 `ConnectionRefusedError` / `NetworkError` / `NetworkError`（Qt 口径），且**都不是** `UnknownSocketError` |
| T-4 | `SO_ERROR` 读出后**显式清零** | Qt 注释：「MSDN 说 getsockopt 会清，实际上没清，会污染后续所有 WSAConnect」 | 注入一次 connect 失败 ⇒ 断言**紧接着**的第二次 connect 不带上次残留（错误码不串味） |
| T-5 | `bytesAvailable` = 引擎缓冲 + FIONREAD | 现在只间接测过读环那一半 | 断言「引擎里已到但未搬进读环的字节」+「内核待读」之和与 `bytesAvailable()` 一致 |
| T-6 | 假 EOF 复核（就绪但读不到 ⇒ **不**误判断链） | 需要「已投递读完成但数据已被取走」的时序 | 注入该时序 ⇒ 断言连接**不**被误判为关闭 |
| T-7 | `ENOBUFS` 分支（不判死 + 下一轮用更小的块） | 真实环境几乎撞不出 `WSAENOBUFS` | 注入 `WSAENOBUFS` ⇒ 断言：① 连接**不**被判死；② 数据最终**完整送达**（续发用更小的块） |
| T-8 | A4「槽表满必须显式失败」**单独**锁住 | 现在只能靠「并发上限」用例间接带过 —— 而 fd 表（128）与 WSA 就绪表（128）**同容量**且 **fd 分配先于 `addWsaEvent`** ⇒ 集成层**根本走不到** `addWsaEvent` 返回 false 的分支 ⇒ **把 A4 回退，该用例仍然绿** | 做一个「只填满 WSA 表、不填满 fd 表」的夹具 ⇒ 断言注册失败时**显式失败**（不静默失联） |
| T-9 | 次要面：「一次就绪**只投递一次**读完成」 | 现在靠「重复投递会污染下一轮」间接带过 | 断言一次就绪**只**产生一次读完成投递（计数法） |
| T-10 | 次要面：位集合**逐位**对齐 Qt（读关注 `FD_READ\|FD_CLOSE\|FD_ACCEPT`、写关注 `FD_WRITE\|FD_CONNECT`、异常 `FD_OOB`） | 本仓用**全集**掩码（有意偏离：不能重发关联） | 断言「实际收到的位 ⊆ 订阅的位」，把「全集是有意的」这条契约钉住 |

---

## 3. 跨端类（本仓后端之外）

| 编号 | 内容 | 为什么开 | 完成判据 |
|---|---|---|---|
| T-11 | **管道移植到 IOCP 后端** | 管道修复只落在 WSAEventSelect 侧 ⇒ A/B 下 IOCP 的管道套件**挂死**（被门禁超时抓住） | IOCP 后端补齐 5 处：派发分流（PeekNamedPipe）/ `autoRead` / `PIPE_NOWAIT` / 断链判死 / 写环续发；且 Peek 路径的错误码符号与另两处**统一**（负值标记 Win32 码）⇒ A/B 下两套后端的管道套件**都绿** |
| T-12 | POSIX 侧 `lastError`：**写侧**致命错误也要记录 | POSIX 现在**只在读完成时**更新 `lastError` ⇒ 写侧致命错误读到的永远是 0 ⇒ 被报成「对端已关闭」 | POSIX 后端在写失败路径也记录 errno；并订正「POSIX 返回线程级 errno」的**错误注释**（代码返回的其实是 per-socket 字段） |
| T-13 | POSIX 侧**门禁**（板端 Linux 的自动化守护） | 本轮所有门禁都在 Windows 上跑 ⇒ 板端那条链目前**无自动化守护** | 一条能在 Linux/WSL 上跑 socket 门禁的命令（不要求本轮就接入 CI），并在文档里写明怎么跑 |

---

## 4. 闭环计划（工作流）

```
Wave A（可并行，文件不重叠）
  A1 = T-1 探针（写探针，不跑；主控跑）
  A2 = T-2 夹具（设计 + 实现）
  A3 = T-11 IOCP 管道移植（只改 XDeviceNetwork_win32.c）
  A4 = T-12 POSIX lastError（只改 XDeviceNetwork_posix.c）
  ⇒ Gate 1：全量重编 + 门禁 + 总回归（主控跑）

Wave B（依赖 A2 的夹具）
  B1 = T-3~T-8 + T-9/T-10（独占测试文件）
  B2 = T-13 POSIX 门禁脚本（只加 verify 脚本）
  ⇒ Gate 2：全量重编 + 门禁 + 总回归

Wave C
  C1 = （已取消）原「消费 FD_READ 位省一次 recv」优化 —— T-1 实测 FD_READ=EDGE，
       位常驻是正确性必需，不能消费；保留现状。
  C2 = 独立复审（新上下文、只读、逐条核实）
  ⇒ Gate 3：全量重编 + 门禁 + 总回归 + A/B
```

**门禁纪律**（沿用本轮已建立的）：
- 门禁自带**负对照自检**（判不出失败就 `exit 3` 中止）与**二进制新鲜度检查**；
- 每套件**挂钟超时**（挂死判失败而不是拖住门禁）；
- 每轮改动后**全量重编**（本仓 ninja 无头文件依赖，增量会留陈旧 obj）；
- 改完必须**重跑门禁 + 总回归**，不靠「应该没问题」。

**不做的事**（明确排除，避免范围蔓延）：
- 不动 POSIX 的**行为**（只补记录与注释），不重写 POSIX 后端的就绪模型；
- 不把 IOCP 后端也改成 Qt 的位集合（两边都保持本仓的既有形态，只做管道对齐）；
- 不引入新的第三方依赖、不引入新的线程/事件源。

---

## 5. T-13 POSIX 门禁脚本【已完成】

板端 Linux 的 socket 门禁自动化守护，已落地为一条独立 verify 脚本。

- **脚本路径**：`verify/run_socket_gates.sh`（POSIX shell，纯 ASCII，LF 行尾）。
- **怎么跑**：
  ```
  BIN=bin/XinYueC_Static bash verify/run_socket_gates.sh
  ```
  可用 `TIMEOUT_S=<秒>` 覆盖每套件的挂钟超时（缺省 180s，与 Windows 侧
  180000ms 对齐）。`BIN` 缺省即 `bin/XinYueC_Static`（Linux 原生 CMake
  构建把运行时产物落在仓库 `bin/`，ELF 宿主无 `.exe` 后缀）。
- **前置条件**：先在 Linux/WSL 上 ninja 构建出测试宿主，例如
  ```
  cmake -S . -B <build> -G Ninja
  cmake --build <build> --target XinYueC_Static
  ```
  再把 `BIN` 指向实际产物（若用私有构建目录则是 `<build>/bin/XinYueC_Static`
  或 `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY` 指定处）。本任务**不要求**在 Windows
  本机跑通 POSIX 构建，脚本面向板端/WSL 直接执行。
- **与 Windows 门禁（`verify/run_socket_gates.bat`）的相同点**：
  - 同一组 7 个 suite：`xsocket-stall` / `xsocket-pipe` / `udp-directed` /
    `udp-bind-family` / `xlink-client` / `xmqtt-tcp-api` /
    `xconsole-shell-backend`；
  - 同一负对照自检：先跑 `--test __gate_selfcheck_must_fail__` 并要求判 FAIL，
    判不出即 `exit 3` 中止；
  - 同一二进制新鲜度检查：`find -newer` 比对 `Src/` `Drive/` `Test/` 与
    顶层 `main.c` 的 `.c/.h`，发现更新则 `exit 2`；
  - 同一退出码纪律：`$?` 整数比较，124=超时判 FAIL，任一失败 `exit 1`、
    全绿 `exit 0`；
  - 每套件挂钟超时（Windows 侧经 `run_one_suite.ps1`，POSIX 侧用 coreutils
    `timeout`）。
- **与 Windows 门禁的不同点**：
  - **零 PowerShell 依赖**：超时用 `timeout` 命令，无 `Start-Process -PassThru`
    的 NULL ExitCode 坑；`timeout` 不可用时明确降级告警（无墙钟保护），不静默；
  - **负对照排在最前**：Windows 侧先查新鲜度再自检，POSIX 侧按要求先自检；
  - 不依赖 `vcvars64.bat` / vswhere / CRLF / OEM 码页等 Windows 构建约定。
- **退出码**：0=全绿 / 1=有 suite 失败 / 2=前置不满足（宿主缺失或陈旧）/
  3=门禁自损（负对照未判 FAIL）。

---

## 6. Wave B 收口状态（2026-11-21）

| 编号 | 状态 | 说明 |
|---|---|---|
| T-2 | ✅ | 注入夹具已落地并被 5 条用例使用；A/B 实证打断映射⇒用例变红 |
| T-3 | ✅ | `sst_fault_connect_error_classification`：refused/unreachable/timeout 各归各的 |
| T-4 | ✅（口径收缩） | `sst_fault_connect_error_not_stale`：锁**框架 lastOperationError 槽不串味**。真实异步 connect 的 SO_ERROR 清零分支（后端 line 2482）当前夹具无法注入，已在用例注释里诚实标注，未假装覆盖（复审 A：已把误导性的「SO_ERROR 清零」用例名/PASS 文案改成「connect 错误码不串味」） |
| T-5 | ✅（口径收缩） | `sst_bytes_available_includes_engine`：断言虚口径（读环+引擎/内核待读）≥ 仅读环口径。无法精确造出「引擎有 X 字节、内核有 Y 字节」的确定值，故用不等式而非等式 |
| T-6 | ✅ | `sst_fault_ready_but_no_data_not_eof`：注入读 would-block ⇒ 不断链不报错，解除后恢复 |
| T-7 | ✅ | `sst_fault_enobufs_then_full_delivery`：注入 ENOBUFS ⇒ 不判死，续发完整 |
| T-8 | ⚠ 保留限制 | **未做**：WSA 表与 fd 表同容量且 fd 先分配，集成层走不到 `addWsaEvent` 返回 false 分支；单独锁需改事件环暴露注入接口，超出测试扩展范围。原并发上限用例的间接覆盖保留 |
| T-9 | ⚠ 部分 | 夹具自检（`sst_fault_injector_selfcheck`）锁了「注入武装/查询/清除」；「一次就绪只投递一次读完成」的计数锁未单独做（需后端暴露计数，超范围） |
| T-10 | ⚠ 部分 | 同上：全集掩码是有意偏离 Qt 的契约，已在 T-1 探针 + 代码注释钉住 EDGE 语义；逐位对齐锁未单独做（超范围） |
| T-13 | ✅ | `verify/run_socket_gates.sh`，见 §5 |

**Gate 2**：全量重编 + 三轮 socket 门禁 + C 总回归 1099/0 全绿；A/B 实证门能抓漏
（打断 WSAECONNREFUSED 映射 ⇒ 31 通过/2 失败，恢复后 33/0）。

---

## 7. 全清单最终状态（Wave C 收口，2026-11-21）

- T-1 ✅ EDGE 探针（结论固化 + bat 退出码修复）
- T-2 ✅ 注入夹具
- T-3 / T-4 / T-5 / T-6 / T-7 ✅（T-4/T-5 口径收缩，已诚实标注；T-4 复审后改名）
- T-8 ⚠ 保留限制（需事件环暴露注入接口，超范围）
- T-9 / T-10 ⚠ 部分（次要点未单独计数锁；EDGE/全集偏离已由探针+注释钉住）
- T-11 ✅ IOCP 管道移植 A/B 4/4（修了写槽收割死锁 + PIPE_NOWAIT 0 字节丢数两处真根因）
- T-12 ✅ POSIX lastError 写侧
- T-13 ✅ POSIX 门禁脚本

**Gate 3（最终）**：全量重编 0 错误 + socket 门禁三轮全绿 + C 总回归 1099/0。
**独立复审**（新上下文只读）：8 项断言全属实，三处注释/命名与实现不一致已订正。

---

# Part III：窗口消息事件源收敛 + 缺省切 WSAAsyncSelect + IOCP 收口

# Windows 窗口事件源收敛 + WSAAsyncSelect 后端（ring 级事件源）

> 2026-11-21。用户拍板三件事：① 切换缺省套接字后端到 `WSAAsyncSelect`（Qt 6.8 同款）；
> ② 窗口消息循环直接在 `XNetIoRingWin32` 实现，**不在其它地方重复建窗**；
> ③ 盘点全仓窗口创建，能迁入 ring 的迁入。

## 1. 架构定案：ring = 平台等待集的两类内核源

`XAbstractNetIoRing` 的抽象是「平台事件等待集」。Windows 上这个等待集由两类内核源构成：

| 内核源 | 承载 | 驱动点 |
|---|---|---|
| 完成包（IOCP） | `m_iocp` + `GetQueuedCompletionStatus` | `waitForEvents`（阻塞） |
| **窗口消息**（新） | message-only 隐藏窗口 + WndProc + 消息槽表 | `pollPlatform` 每轮 `xnetio_pumpMsgOnce`（非阻塞泵） |

阻塞等待点**未改**（仍一次 GQCS）——窗口泵只挂在每轮 `processEvents` 必经的
`pollPlatform`（`xPollDispatch`）里，GUI/纯控制台（`--test` 宿主）都被驱动，
不引入 MsgWaitForMultipleObjects 双源，不新增线程。

### ring 新 API（`XNetIoRingWin32.h`）
- `XNetIoRingWin32_msgWindow(ring)` — 惰性创建/返回消息窗口（WSAAsyncSelect 的 hwnd）
- `XNetIoRingWin32_pumpMsgOnce(ring)` — 泵一帧 pending 消息
- `XNetIoRingWin32_takeMsgEvents(ring, sock, &bits, errSlot)` — 取走某 socket 累计 FD_* 位
- `XNetIoRingWin32_resetMsgSlot / removeMsgSlot` — **注册即清残留** / 注销
- `XNetIoRingWin32_registerWindowSource(msg, cb, ud)` — 通用窗口消息挂靠（未来
  `WM_DEVICECHANGE`/热键/电源通知用，**不得各自另建窗口**）

### 生命周期
窗口惰性创建于首次触达（事件循环线程，线程亲和）；`init` 清槽表；
`deinit` `DestroyWindow`。`GWLP_USERDATA` 存 ring 指针，WndProc 路由。

## 2. WSAAsyncSelect 后端（`XNETWORK_WIN_BACKEND_WSAASYNC`，新缺省）

**关键设计**：不复制 3000 行后端——`XDeviceNetwork_win32_select.c` 的 socket 语义段
门控从 `WSAEVENT` 放宽为 `XNETWORK_WIN_SOCKET_SELECT`（=WSAEVENT‖WSAASYNC），
仅「就绪获取层」按宏分叉：
- `probeArm`：`WSAAsyncSelect(sock, ring窗口, XNETIORING_WM_SOCKET, mask)`；
  **listen 只订阅 FD_ACCEPT**（FD_CLOSE 对 listen 无语义，订阅面最小化）；
- `pollEvents`：`takeMsgEvents` 取缓存位 OR 进 `pendingEvents`（FD_READ 常驻
  语义与 WSAEVENT 逐位一致）；
- 其余收发/背压/错误收口/管道/错误注入 2800 行**逐字复用**（注入夹具在
  WSAASYNC 下同样可用）。

三后端宏（`XNetwork_config.h`）：`IOCP=1 / WSAEVENT=2 / WSAASYNC=3`，
派生 `XNETWORK_WIN_SOCKET_WSAEVENT/IOCP/WSAASYNC`（互斥）+ `..._SELECT`（WSAEVENT‖WSAASYNC）。
**缺省 = WSAASYNC**（用户 2026-11-21 拍板切换，Qt 6.8 事件分发器同款通知机制）。

### 落地过程中抓出并修复的三个真根因（都有实测背书）

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 1 | 60 连接全失败、消息零到达 | 消息泵挂在 `waitForEvents` 分片——但测试宿主不走该路径；WSA 就绪真正驱动点是 `pollPlatform`（xPollDispatch 每轮调） | 泵移到 `pollPlatform`，`pollWsaEvents` 之前 |
| 2 | 并发 60 建连失败、双服务端只出一路会话 | **FD_ACCEPT 边沿 vs 水平**：WSAEventSelect 的 Enum 是水平累积（代码注释「未 accept 前 AFD 持续报」依赖它），WSAAsyncSelect 是边沿消息——consume 清位后队列剩余 pending 连接再无新消息 ⇒ 饿死 | `serverGetAcceptedSocket` accept 成功后把 FD_ACCEPT 位重置回缓存（水平语义补回；空投递一轮自终止） |
| 3 | 双服务端用例监听口被误杀（`CLOSE-path=dispatch-FD_CLOSE isServer=1`） | **SOCKET 句柄复用毒消息**：closesocket 时内核投的最后一条 FD_CLOSE 滞留窗口队列，新 socket 复用同句柄后毒位落进新监听口槽；listen 的 FD_CLOSE 消费路径无任何复核（客户端有 recv(0)+PEEK 兜底） | ① probeArm 注册即 `resetMsgSlot` 清残留；② listen 忽略 FD_CLOSE（dispatch consume 后 return，不判死）；③ listen mask 最小化只订 FD_ACCEPT |
| 4 | 并发上限用例偶发（3/5，第 59/60 条建连超时） | **FD_ACCEPT 边沿剩余面**：accept 与 socketSetDescriptor 之间继承关联的消息入队后，consume/重置位的覆盖仍有窗口间隙 | 双源等待（见 §2.1）即时泵送 + 洪流即时消化，实测 11 连绿 |
| 5 | 槽表满（MSGDIAG：FULL inUse=128 addCnt=253 rmCnt=125） | **消息槽泄漏**：accept 继承关联让 WndProc 对 accepted socket 自动建槽；socket 关闭后句柄值未必复用，滞留毒消息到达时 WndProc 又为死句柄建新槽 ⇒ 槽永久泄漏（128 个全表耗尽，新 socket 消息位被丢弃） | **槽只经显式注册存在**：probeArm 注册即 `resetMsgSlot`（无槽则建空槽）；WndProc 对无槽 socket 的消息**直接丢弃**（取走点都在注册点之后的 processEvents 轮，安全）；全文件 17 处 closesocket 统一收口 `xw32_closeSocketTracked`（remove+closesocket，幂等） |

### 2.1 双源等待（IOCP + 窗口消息一次等待、立即唤醒）

`waitForEvents` 的 WSAASYNC 分支改为 `MsgWaitForMultipleObjectsEx(1, {m_iocp},
slice, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`（Qt `qeventdispatcher_win` 同款）：

- 完成包/wakeUp → 句柄唤醒 → `GQCS(0)` 取包处理 → 返回；
- 窗口消息 → **立即**唤醒 → 全量排空本线程消息队列（DispatchMessage 路由到
  各 WndProc，FD_* 位缓存进消息槽）→ `pollWsaEvents` 派发 → 返回。

事件唤醒延迟从「≤10ms 分片」降为「立即」，分片循环消失。`MWMO_INPUTAVAILABLE`
+ 排空到空避免 busy-loop；PeekMessage 不带 hwnd 过滤（本线程其它窗口的消息
由 DispatchMessage 路由到各自 WndProc，与 XGui 泵并发安全）。**门控隔离**：
仅 WSAASYNC 构建走新路径，WSAEVENT/IOCP 逐位不变（切回实测 7 套 + 1099 全绿）。

## 3. 剪贴板窗口迁入 ring（消除第二个自建窗口）

- 迁移前：`XinYueCClipboard` 专用隐藏窗（`XPlatformNativeWindow_win32.c`，对标
  QWindowsClipboard）收 `WM_CLIPBOARDUPDATE`。
- 迁移后：监听 `AddClipboardFormatListener` 加在 **ring 窗口**上，
  `registerWindowSource(WM_CLIPBOARDUPDATE, xpwn_clipUpdateCb)` 路由到原处理函数；
  `OpenClipboard`/`GetClipboardOwner` 属主判断沿用同一句柄，语义不变。
- `xpwn_wndProc` 的 WM_CLIPBOARDUPDATE case 删除（消息不再到 GUI 窗口）。
- `!XAbstractNetIoRing_ON` 裁剪构建保留旧自建窗回退（桌面恒 ON 不触达）。
- Qt 对照：Qt 的剪贴板窗口（QWindowsClipboard）与 event dispatcher 的
  internalHwnd 也是两个窗口；本仓收敛为一个 ring 窗口后，比 Qt 更统一。

## 4. 验证（全绿）

```
WSAASYNC（缺省）+ ring 窗口源 + 剪贴板迁移：
  verify\run_socket_gates.bat   → 7 套 ALL GREEN（含负对照/新鲜度/超时）
  verify\run_regression.bat c   → PASS 1099 / FAIL 0
  XLineControl_Acceptance_Test  → PASS (68 checks)（含剪贴板读写往返 E12/E15）
  XGuiRegression_Test           → passed (s_failures=0)
WSAEVENT（切回对照）：
  同一套门禁 + 总回归           → ALL GREEN / 1099-0（旧后端无回归）
IOCP（此前战役）                → pipe 4/4（A/B 双后端）
双源等待 + 槽泄漏根治后：        → stall 33/0 ×11 连绿（原 3/5 偶发已根治）
                                  门禁 7 套 + 1099-0；WSAEVENT 切回同全绿
```

## 5. 已知边界（如实记录）

- **并发上限用例偶发已根治**（原 3/5 复现，见 §2 根因 4/5）：槽泄漏 +
  双源等待落地后 stall 33/0 连续 11 次全绿。
- **WM_CLIPBOARDUPDATE 反向通知（外部进程写剪贴板）无自动化覆盖**：
  现有 GUI 用例只覆盖本进程读写往返（OpenClipboard/Owner 路径=迁移面）；
  跨进程通知依赖窗口消息路由（迁移后走 ring 泵），延迟≤一轮心跳（20ms），
  剪贴板通知无实时性要求。
- 剪贴板在 `ring 未就绪`（进程极早期）时不可用——与旧实现「窗口创建失败
  即不可用」同语义；GUI 场景 ring 在 dispatcher init 即建，实际无窗口期差异。

## 附：IOCP「环回完成停滞」的复查（2026-11-21）

**结论：当前环境已不复现；skip 方案留档备查；IOCP 后端暂不实施该重构。**

- 复查手段：原复现程序（`verify/windows_loopback_stall_repro.c`，ConnectEx+
  AcceptEx 变体）编译运行 20 轮 × 4 组（spin 0/1）——**0 slow/0 lost**，
  2026-10-28 的停滞在当前系统状态不再自然发生（plain connect/accept 变体
  的 `r0 accept fail` 是脚手架限制：非阻塞 listen + plain accept =
  WSAEWOULDBLOCK，原程序只支持 AcceptEx 跑通，非违例）。
- **skip 方案留档**（`verify/iocp_skip_ab_probe.c`）：若未来环境再触发停滞，
  根治路径是官方开关
  `SetFileCompletionNotificationModes(sock, FILE_SKIP_COMPLETION_PORT_ON_SUCCESS)`
  ——同步完成的操作不投递完成包，应用以 `GetOverlappedResult(FALSE)` 收结果
  ⇒「完成包停滞」降级为非事件（没有包可丢）。探针实现含 skip/基线两种
  等待形态可直接复用。
- **暂不实施的理由**：触发条件已不复现 ⇒ skip 重构（读/写槽完成判定从
  「等包」改「查结果」，驱动模型级改动）没有可复现的故障做 A/B，只能回归
  证明「没改坏」——收益/风险比倒挂；缺省后端 WSAASYNC 不依赖完成包投递，
  停滞对缺省路径无影响。
- 当时「应用层无解」的结论订正：它只在「必须依赖完成包出队」的前提下成立；
  skip 模式移除该前提后，三态中的①（完成包丢失）完全可解，②（CancelIoEx
  返回 NOT_FOUND）恰是「操作已完成」的证据（即①的误判），真挂死可取消重挂。

## 附二：IOCP 跨进程联调复查（2026-11-21 晚）——「进程退出」是测试工件，非缺陷

用户拍板切 IOCP 缺省后做跨进程联调复查，抓到一个**测试方法工件**并证伪：

- **现象**：`bin/HsfServer.exe --sim &`（bash 后台）+ 单连接+断开后，服务端进程
  消失（alive 1→0，二次连接 10061 拒绝），3+ 次确定性复现——疑似"断开即退出"缺陷。
- **逐层排除**：cdb 附加（默认 Debug Heap）不退出；`_NO_DEBUG_HEAP=1` + cdb
  全异常拦截（av/0xc0000409/ibp/epr）也不退出——Debug Heap 与用户态异常全部
  排除；`bp XCoreApplication_quit / XEventLoop_exit` 未命中——退出不经 quit/exit。
- **根因**：**Git Bash 后台作业生命周期**。bash 命令行（含其后台原生子进程）
  结束时 MSYS 对后台作业做终止清理；所有"死亡"观察点都是**新的一条 bash
  命令**（前一条已退出并清理）。
- **铁证**：改用独立 `cmd start /b` 启动（脱离 bash 生命周期）后：
  单连接+断开（err=64 正确触发并处理）→ **服务端存活** → 二次连接成功。
- **err=64 分类修复在真实跨进程场景验证通过**：日志显示 64 被正确判死 +
  投递关闭事件 + 服务端继续接受新连接；XLink 登录成功（root）多次。
- **对照口径修正**：此前「WSAASYNC 构建从未死过」的印象同样是工件——历史
  验证从未在「bash 命令结束之后」检查过后台服务端存活；后端之间并无此差异。

**结论：IOCP 后端跨进程场景健康**；切缺省的验收补齐（联调层面）。工具教训：
**bash 后台原生子进程的存活检查，必须在同一条 bash 会话内做，或用独立
cmd start 启动被测进程**——否则"死没死"的结论是 MSYS 作业清理的工件。

## 附三：IOCP err=64 错误分类修复——当年弃用 IOCP 的核心问题已解决

### 当年问题（为什么 IOCP 被弃用、WSAEVENT 后端被造出来）

HSF 跨端联调（2026-11-04 起）实测：Windows 桌面客户端登录成功后**反复断开
重连循环**，服务端日志唯一线索是
`[XNetwork-W32] 读完成 致命错误 err=64，判定连接已死，投递关闭事件`。
当时无法定位根因，遂以「IOCP 后端不做规避就不可用」定性，新建
WSAEventSelect 后端（不依赖完成包投递，绕开整个故障面）作为缺省，
IOCP 保留为对照——**并挂账为「未完成交接」**（2026-11-04 战役）。

### 本轮定位与修复（2026-11-21，提交 4cd4a14b）

**机制**：IOCP 未决重叠读在**对端关闭**时以 **Win32 码 64**
（`ERROR_NETNAME_DELETED`）完成（优雅关闭也可能走此码，取决于内核时机；
RST 走 10054）。读侧判死语义本来就正确（64=连接已死）——**错在错误分类**：

- 框架 `socketErrorFromPlatform` 的区间只有三段：WSA（≥10000）/ 管道负值 /
  errno（0<code<10000）。64 落 errno 区间 → 不在表 → `UnknownSocketError`
  → 关闭事件兜底成 `NetworkError`（"Socket closed by error"）。
- **分类撒谎** ⇒ 上层 MQTT/S7/Modbus 的「对端正常关闭守卫」全部不命中
  ⇒ 正常断开被报成异常 ⇒ 联调表现为"登录成功 ↔ 断开循环、界面闪"。

**修复**（`XAbstractSocket.c`，读写共用一处收口）：
1. 关闭事件特判列表补 **64 / 1236**（ERROR_CONNECTION_ABORTED）→
   `RemoteHostClosedError`（与 10054/10053 同类）；
2. `socketErrorFromPlatform` 补 **`_WIN32` 门控的 Win32 码区间**
   （64/1236/109/233 → RemoteHostClosed，232 → Temporary；同值在 Linux
   errno 语义完全不同，必须门控）。

### 验证

- **IOCP 构建全套**：7 套门禁 ALL GREEN + C 回归 1099/0 + XGuiRegression；
- **真实跨进程联调**（本轮压测/探针）：python 客户端连/断后日志显示 64 被
  正确判死 + 投递关闭事件 + 服务端**继续接受新连接**（附录二的「断开后
  存活 + 二次连接 OK」即在此场景下测得）；
- WSAASYNC/WSAEVENT 构建：RST/FIN/断链用例同口径全绿（门控隔离，无回归）。

### IOCP 最终状态

| 项 | 状态 |
|---|---|
| err=64 断开循环（弃用核心原因） | ✅ 已解决（分类修复 + 跨进程验证） |
| 管道挂死（写槽收割死锁 / PIPE_NOWAIT 0 字节丢数） | ✅ 已解决（A/B 4/4） |
| 16 项交接缺陷清单 | ✅ 全部关闭 |
| 环回完成停滞 | 当前环境不复现；skip 方案留档（见附录一） |
| 错误注入夹具 | 仅 WSAASYNC；IOCP 走显式跳过口径 |

**三后端最终形态**：缺省 **WSAASYNC**（用户 2026-11-21 拍板；通知驱动、
ring 窗口源、双源唤醒）；**IOCP** 高性能可选（真正 overlapped I/O，多槽并发，
跨进程验证健康）；**WSAEVENT** 对照回退。三者均以同一套门禁与回归守护。
