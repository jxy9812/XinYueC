# Windows IOCP 读完成丢失 —— 根修记录（2026-11-06）

> **状态：根修完成，跨进程联调已给出前后对照证据。**
> 三个叠加的根因已全部修掉：**在飞 OVERLAPPED 复用**、**完成端口唤醒包饥饿**、
> **完成包归属按句柄数值判定**。IOCP 与默认 select 两套后端回归全绿，
> HSF 客户端/服务端跨进程环回联调从「3 次读停摆 + 6.1s 静默判线 + 自动重连」
> 变为 **0 次**。
>
> 前作《Windows_IOCP读完成丢失_交接_20261104.md》把现象定性为「平台丢完成包」。
> 该定性**不成立**：包从来没丢，是后端自己把在飞 OVERLAPPED 复用了，并且
> 让真实完成包排在唤醒包后面饿死。

## 一、根因一：在飞 OVERLAPPED 被复用（机制级复现）

`verify/iocp_overlapped_alias_repro.c`（独立于框架，纯 Win32 + Winsock，
本机环回，三个用例各自独立 socket 对 + 独立 IOCP）：

```
=== A 别名重投（同一 OVERLAPPED 在飞时再投一次） ===
  投递1: ret=-1 err=997   投递2(同一 OVERLAPPED): ret=-1 err=997
  第一批 1000 字节 → 完成包=1 字节=1000        （两笔在飞只回来 1 个包）
=== B 取消即重投（CancelIoEx 后立刻复用同一 OVERLAPPED） ===
  第一批 1000 字节 → 完成包=2 字节=1000 失败笔=1(ABORTED=1)
  第二批  500 字节 → 完成包=0 字节=0 内核待读=500
  判定：完成包丢失：数据在内核但读不再完成（读通道停摆）
=== C 对照（只在完成包回收后才重投）===
  第二批  500 字节 → 完成包=1 字节=500 内核待读=0   → 预期行为
```

旧实现用布尔 `readPending` 表达「有读在飞」，并在四处把它清掉后重投：

| # | 位置 | 行为 |
|---|---|---|
| 1 | `socketRead` 读看门狗 | `CancelIoEx` 后**立刻**用同一 `readContext` 重投 `WSARecv` |
| 2 | `socketContinueRead` | 无条件清 `readPending` 后重投（判据是愿望位，不是内核状态） |
| 3 | `socketDisconnect` → 重连 | 上一连接「已取消但未回收」的读被新连接复用 |
| 4 | 写槽看门狗 | 直接把槽标空闲，把**内核仍持有**的 OVERLAPPED 放回池子 |

## 二、根因二：完成端口被唤醒包灌满，真实完成包「排队饥饿」（联调才暴露）

修掉根因一后，跨进程联调**仍然**每 20 秒出现 5~6 次「读停摆=完成包丢失」，
随后 6.0s 静默判线。定向探针给出决定性数据：

```
[D4] arm fd=1 slot=1 seq=1086 …              ← 挂读
[D1] LOST fd=1 slot=1 bytes=750 deltaPolls=1321   ← 端口已被检查 1321 次仍无包
[D7] polls=7000 wakeTaken=6989               ← 每 1000 次轮询取到 989 个唤醒包
[D2] LATE fd=1 slot=1 …                      ← 该「丢失」的包随后到达（迟到而非丢失）
```

机制：

1. `wakeUp()` 每次唤醒都无条件 `PostQueuedCompletionStatus(..., NULL)`；
   任意线程 `postEvent` 都走它 ⇒ 端口里几乎全是唤醒包（实测 98.9%）。
2. `pollPlatform` 的排空循环遇到 `overlapped == NULL` 就 `break`：
   **每轮只能前进一个包位**。真实完成包排在唤醒包后面 ⇒ 被饿住数秒。
3. 内核早已完成（`GetOverlappedResult` 为真、状态块写好 750 字节），
   读看门狗据此判「完成包丢失」，退役读槽；3 个读槽全退役后读通道停摆
   ⇒ 6s 无入站 ⇒ 静默判线 ⇒ 重连。

修法：

- `wakeUp` 幂等（`m_wakePending` 0/1，端口里已有唤醒包就不再投）；
- `pollPlatform` 把唤醒包**取走并继续取**（最多一批），不再 `break`；
  分片等待路径同样清标记；
- 读看门狗判「丢包」增加前置条件：**自挂读起端口至少被检查 4 次**
  （`XNETWORK_READ_LOST_POLLS`），且退役槽在隔离 64 次检查后可回收
  （`XNETWORK_READ_QUARANTINE_POLLS`），避免「误判一次少一个槽」累积成停摆。

## 三、根因三：完成包归属用句柄数值判定（句柄复用即串代）

`sameSocket` 原本比较 `SOCKET`/`HANDLE` 的**数值**，而 Windows 会复用句柄值：
重连后新 socket 很可能拿到与旧 socket 相同的数值 ⇒ 旧连接残留的 ABORTED 包被
当成「当前连接」的完成包 ⇒ 触发一次重挂，而旧操作可能仍在内核里 ⇒ 又回到
根因一的二次投递。

修法：引入**连接代次** `p->ioGeneration`（换句柄/拆连接时 +1），读写槽在投递时
记录 `slot->ioGen`，归属判定改为 `slot->ioGen == p->ioGeneration`，与句柄数值无关。

## 四、四条不变量 + 完成包驱动的槽状态机

- **A** 一份 OVERLAPPED 同一时刻只能在飞一笔操作；
- **B** 只有该笔操作的完成包**出队**时，才允许它参与下一次投递；
- **C** `CancelIoEx` 不是完成：重投只能发生在 ABORTED 完成包出队之后；
- **D** ABORTED 完成包是内部控制流，不是语义事件。

落地：

| 项 | 设计 |
|---|---|
| 读槽 | `readSlots[3]`，每槽独占 OVERLAPPED + 8KB 缓冲；`armed`/`ready`/`retired` 三态互斥 |
| 投递入口 | 唯一：`w32_armRead`（守门人 `w32_readWanted`：在飞即拒、上批未消费即拒） |
| 回收出口 | 唯一：`w32_reapIocpCompletion`（按 OVERLAPPED 指针身份归属） |
| 取消/重挂 | 看门狗只发 `CancelIoEx`；重投在该笔操作的 ABORTED 完成包分支里执行 |
| 完成包真丢失 | `GetOverlappedResult(FALSE)==TRUE` **且**端口已检查 N 次 ⇒ 接管结果、隔离该槽、补投递 |
| 写槽 | `writeSlots[8]`，`inUse`/`armed`/`zombie` 三态；看门狗只取消不抢槽；`bytesWritten` 按 FIFO 逐笔呈现 |
| 写保序 | `writeStash`（提交失败的那一块）+ ring buffer，绝不「写回 ring 尾部」打乱字节流 |

## 五、连带修掉的三处真缺陷

| # | 缺陷 | 后果 | 处理 |
|---|---|---|---|
| 1 | 读操作**失败**（RST / 64 / 10054）被当作「0 字节读完成」走假 EOF 复核 | 复核用 `MSG_PEEK` 判对端是否关闭，而对端消失后 PEEK 拿不到 0 ⇒ 关闭事件被丢弃 ⇒ 踢线后本端永不重连（实测 XLink 踢线 10 项失败） | 读失败＝连接已死的直接证据：后端用**属主指针**直接投递 `XEventSockClose` |
| 2 | `platformPeerReallyClosed` 把所有 `recv` 错误都判成「还活着」 | 与上一条互为成因；RST 后 `recv` 恒返 `WSAECONNRESET`/64 | 按错误码分类：只有 `WSAEWOULDBLOCK` 才是「活着但无数据」 |
| 3 | 假 EOF 复核结束后把 `FIONBIO` 恢复为 **0（阻塞）**，注释却写「与创建时一致」 | 复核一次就把套接字留成阻塞态，后续非重叠 `recv/send` 可能阻塞事件循环 | 恢复为 1（非阻塞） |

## 六、跨进程联调证据（HSF 客户端 + 服务端，本机环回，`--sim` 模拟从站）

### 6.1 读方向停滞（同一命令、同一场景，仅切换后端代码版本）

单客户端 25 秒，登录 `admin:admin`：

| 观测量 | 修复前（HEAD 基线） | 修复后 |
|---|---|---|
| 客户端「读停摆」 | **1 次**（`完成包丢失（已结束 1482 字节）`） | **0** |
| 客户端「静默超时判线」 | **1 次**（`空闲 6139ms > 6000ms`） | **0** |
| 客户端自动重连 | **发生** | **未发生** |
| 服务端读停摆/判线 | 0 | 0 |

### 6.2 切页订阅是否真的到达（`--page` 必须落在会订阅的页面）

**口径修正**：`--goto` 只用于截图模式（`HsfUi.c` 里 `m_thenPage` 在出图前生效）；
联调必须用 `--page` 直接落在监测页，否则停在 `HSF_PAGE_HOME=0`（主界面，
不订阅任何页面主题），只能看到 2 个常驻主题——那种跑法既不能证明也不能否证
「切页订阅丢失」。

页面 → 主题（`hsf_pageTopicMask`）：

| 页 | 索引 | 订阅主题 |
|---|---|---|
| 监视-数字量 | 3 | `0x00010006` |
| 监视-模拟量 | 4 | `0x00010002` |
| 监视-变频器 | 5 | `0x00010001` |
| 监视-流量计 | 6 | `0x00010004` + `0x00010009` + `0x0001000A`（水质链 3 主题） |

实测（`HsfClient 20 --login admin:admin@127.0.0.1:61430 --no-settings --page 6`，
服务端 `HsfServer --sim 61430`）：

```
HsfServerApp: 会话订阅主题 0x00010003   ← 温湿度（常驻）
HsfServerApp: 会话订阅主题 0x00010005   ← 设备健康（常驻）
HsfServerApp: 会话订阅主题 0x00010004   ← 流量计（页面主题）
HsfServerApp: 会话订阅主题 0x00010009   ← 浊度（页面主题）
HsfServerApp: 会话订阅主题 0x0001000A   ← 泵（页面主题）
```

**5/5 全部到达**（旧交接的验收线是「>3」），同一次运行客户端
`读停摆/静默/自动重连 = 0`。说明链路已能承载切页订阅帧；现场「只收到最初
3 个订阅」的成因是读方向停摆期间订阅帧整段收不到，而不是订阅逻辑本身丢帧。

### 6.3 长跑

两会话（`admin` + `root`）连续 50 秒：服务端 **0 次读停摆/判线**，两客户端只有
退出时的 `连接断开（已收手）`，无重连。


## 七、回归（两套后端都跑）

| 回归 | IOCP 变体 | select 变体（默认） |
|---|---|---|
| `--test xsocket-stall` | **15 / 0** | **15 / 0** |
| `--test xtcpserver` | **18 / 0** | **18 / 0** |
| `--test xlink` | **820 / 0** | **820 / 0** |
| `--test xlink-quality` | **72 / 0** | — |
| `--test ntp`（UDP） | **83 / 0** | **83 / 0** |
| `--test ddns` | **112 / 0** | **112 / 0** |
| `--test xssl` | **PASS**（本地 TLS 1.2） | — |
| `--test xmqtt`（双进程） | **全部通过** | — |

构建：

```bat
out\build\build_default.bat         :: 默认变体（无宏 = IOCP，2026-11-06 起）
out\build\build_iocp.bat            :: IOCP 变体（显式 /DXNETWORK_WIN_BACKEND=1，A/B 用）
out\build\build_select.bat          :: select 回退变体（显式 =2，输出到 select-check）
out\build\build_hsf.bat <构建目录>   :: 联调二进制 HsfServer/HsfClient
```

### 7.1 新增用例（针对本轮三个真因，`XSocketStallTest.c` 第 11~15 项）

| # | 用例 | 针对的缺陷 |
|---|---|---|
| 11 | 唤醒包风暴下读通量（120 轮 × 64B，工作线程高频 `wakeUp`） | 完成端口唤醒包饥饿 |
| 12 | 立刻重连 20 轮无串代（断开不等待、不排空，直接下一轮） | 完成包归属按句柄数值 → 句柄复用 |
| 13 | 对端异常断开 1500ms 内被察觉（未读数据 + close ⇒ RST/FIN） | 读失败被假 EOF 复核吞掉 |
| 14 | 64KB 大块逐字节一致 | 写槽分片 + `writeStash` 保序 |
| 15 | 读缓冲暂停/恢复（`setReadBufferSize=1KB`，消费后 8KB 全额到达） | `readPaused` 恢复路径 |

### 7.2 循环复验（同一二进制连跑多次，查抖动）

| 序列 | IOCP 变体 | select 变体 |
|---|---|---|
| `xsocket-stall` | **6 轮 × 15/0**（90 项全过） | **4 轮 × 15/0**（60 项全过） |
| `xtcpserver` | **3 轮 × 18/0** | **3 轮 × 18/0** |
| `xlink` | **2 轮 × 820/0** | **2 轮 × 820/0** |

> 覆盖边界：以上均为**同进程**回归；本轮真因中「唤醒包饥饿」必须靠跨进程联调
> （GUI 客户端高频 postEvent）才会显形，单进程用例不能替代 §六 的联调。

### 7.3 默认后端切换（2026-11-06）

既然三处根因已定性、可复现、可消除，且两套后端在同一批用例上均全绿，
把默认值切回 IOCP：

| 项 | 切换前 | 切换后 |
|---|---|---|
| `XNETWORK_WIN_BACKEND` 默认值 | `XNETWORK_WIN_BACKEND_WSAEVENT`(2) | **`XNETWORK_WIN_BACKEND_IOCP`(1)** |
| select 后端定位 | 默认实现 | 可回退/对照（`-DXNETWORK_WIN_BACKEND=2`） |
| 构建脚本 | `build_select.bat` 出默认 | `build_default.bat`（无宏）/ `build_select.bat`（显式 2） |

> 宏在公共头（`XNetwork_config.h`）里，切换后**必须全量重编**；已重编默认
> 变体（`out\build\x64-Debug`）并在**新默认构建**上重跑全部回归与联调。

切换后实测（拉取远端 `83ba560e` 之后的树，默认构建 = IOCP）：

| 项 | 结果 |
|---|---|
| `--test xsocket-stall` | **15 / 0** |
| `--test xtcpserver` | **18 / 0** |
| `--test xlink` / `xlink-quality` | **820 / 0** · **72 / 0** |
| `--test ntp` / `ddns` / `xssl` / `xmqtt` | **83 / 0** · **112 / 0** · **PASS** · **全部通过** |
| HSF 联调（默认构建，`--page 6`） | **5 条订阅全到、0 读停摆/判线/重连** |

## 八、给下一次的判据

1. **同进程测试全过 ≠ 跨进程正确**。本轮所有单进程回归在修复前后都全绿，
   故障只在跨进程联调复现；判定必须包含一次真实客户端/服务端联调。
2. 看到「内核有数据但读不完成 / 完成包丢失」，先查**在飞 OVERLAPPED 是否被复用**，
   再查**完成端口是否被唤醒包灌满**（`pollPlatform` 遇 `overlapped==NULL`
   是否 `break`）。
3. `GetOverlappedResult(TRUE)` 只证明「操作完成」，不证明「包丢了」；
   必须叠加「端口已被检查 N 次」才能判丢包。
4. 布尔标志（`readPending`/`writePending`）不得作为「内核在飞」的判据；
   在飞关系只能由完成包出队来结束。
5. 完成包归属**不得**用句柄数值比较（句柄会复用），必须用连接代次。

## 九、写方向补修：提交失败不再静默（2026-11-08）

前八节都在读方向。2026-11-08 联调暴露出**写方向的对称缺陷**：
`WSASend` 的**提交**（不是完成包）失败时，旧实现只回滚写槽、打印一行日志，
**从不判定连接已死**。

### 现象（现场口径）

服务端日志刷出成百上千条 `[XNetwork-W32] 写提交失败 err=10053/10054
（无完成包，已回滚槽 N）`，但会话仍停在 `Ready`：tx 队列持续积压、推送被
静默丢弃（队列满即丢），而心跳与收方向仍然通。客户端表现为**界面数据冻结、
连接显示正常、切页订阅也发得出去**——最容易误判成「客户端渲染卡住」或
「服务端推错主题」。

### 根因

`WSASend` 返回失败时**不会**产生完成包，因此「靠完成包判死」的两条路径
（读完成失败、写完成失败）都不会触发。而提交失败的错误码分两类：

| 类别 | 错误码 | 语义 |
|---|---|---|
| 致命（连接已死） | 10053 WSAECONNABORTED、10054 WSAECONNRESET、10057、10058、10059、10060、10061、ERROR_NETNAME_DELETED、ERROR_CONNECTION_ABORTED、ERROR_BROKEN_PIPE、ERROR_NO_DATA、ERROR_PIPE_NOT_CONNECTED | 再重试也不会好 |
| 瞬时（背压） | 10035 WSAEWOULDBLOCK、997 ERROR_IO_PENDING 及未知码 | 下一拍继续 |

旧实现把两类一律当背压滚回槽位 ⇒ 死链路上无限重试，永远不会判死。

### 修法（`Drive/windows/Device/Network/XDeviceNetwork_win32.c`）

1. `w32_isFatalWriteError(err)`：按上表分类。
2. `w32_postFatalClose(p, err, where)`：投递一次**关闭事件**走正常拆除路径
   （不在提交栈上直接拆，避免重入），并用 `closePosted` 按**连接代次**去重
   ——同一代连接无论读写各报多少次致命错误，只投递一次。
3. `w32_rollbackWrite(p, index, err, fatalAllowed)`：回滚槽 + 关判定。
   调用侧显式区分：命名管道 `fatalAllowed=true`；TCP `fatalAllowed=!isUdp`
   （UDP 无「连接」语义，一律按瞬时处理）。
4. `closePosted` 在 `w32_newIoGeneration()` 里复位（覆盖 connect/bind/
   setDescriptor/ConnectLocal/socketDisconnect 全部换句柄路径），保证重连后
   仍能正常判死，又不会让旧代的重复致命写错误刷屏。
5. 异步写**完成**失败分支同样补 `w32_isFatalWriteError` 判定，且 `isUdp`
   必须在 `w32_releaseWriteSlot` 清槽**之前**取值（清槽会清掉该位）。

### 判据（给下一次）

- 看到「数据不更新但连接正常」，先数日志里 `写提交失败` 的条数与错误码：
  出现 10053/10054 且会话仍是 `Ready` ⇒ 写方向判死缺失。
- 「提交失败」与「完成失败」是两条独立路径，**都必须**判死；只补一条仍会漏。
- 判死去重必须按**连接代次**，不能按布尔（`closePosted` 就是为此存在）。
