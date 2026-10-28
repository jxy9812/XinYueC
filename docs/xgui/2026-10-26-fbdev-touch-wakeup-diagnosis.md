# 2026-10-26 fbdev 触摸无响应 / 渲染停滞：诊断、方案与实施

> **结论先行**：昆仑通态屏（192.168.1.252，全志 A33，内核 5.2.9）上「触摸后程序无反馈、画面整体不动」，**不是死锁**（进程 CPU 持续增长、事件循环在转），而是**触摸 fd 不在框架的阻塞等待集里**：`/dev/input/event0`（fd 6）没有注册进 Linux 异步后端（`XNetIoRingPosix.c` 的 epoll 实例，fd 3），触摸事件因此**无法唤醒阻塞等待**，只能等下一圈边界；渲染重绘、FPS 浮层、触摸泵全挂在同一圈里 ⇒ 一起"卡住"。
>
> **已实施并验证（2026-10-26 晚）**：按 §三 方案 A 补齐「被动可读 fd」通道，触摸 fd 入 epoll 等待集。真机验证：触摸 fd 出现在 `fdinfo/3` 的 `tfd: 6`；点击根背景 → `mousePress` 立即产生（坐标精确）；点击导航按钮 → **页面成功切换 + 画面刷新**（交互态 FPS 80.4）；黑区占比 2.32%（无黑块回归）。桌面 `wf_all_gate.sh` RC=0、`wf_regression_gate.sh` 失败集与基线**逐条相同**（零新增失败）。

---

## 〇、症状与实测证据

### 0.1 用户报告

1. 「触摸后程序无反馈了」
2. 「画面整体也不动了，整个渲染都被卡住了」

### 0.2 设备实测（`mcgs` = 192.168.1.252）

| 观测项 | 结果 | 判读 |
| --- | --- | --- |
| 进程状态 | `State: S (sleeping)`，单线程 | 未死锁（死锁会是 R 或持续 D） |
| `/proc/<pid>/wchan` | `do_epoll_wait` | 阻塞在 epoll 等待 |
| CPU 时间增量 | 3 秒涨 10 tick（≈0.1s CPU） | **事件循环在转**，不是挂死 |
| 两帧 md5（间隔 1s） | 完全相同 | 画面确实静止 |
| `/proc/<pid>/fdinfo/3`（epoll 等待集） | `tfd: 8` / `tfd: 7` / `tfd: 4` —— **无 tfd: 6** | **触摸 fd 不在等待集**（核心论断） |
| 触摸 fd | `fd 6 -> /dev/input/event0` | demo 确实持有并打开了设备 |
| 设备事件产出 | 注入一次触摸，`/dev/input/event0` 可读出 96 字节（4 个 `input_event`） | **驱动正常产出事件** |
| demo 日志 | 无 `mousePress button=1 ...` 记录 | **事件未转成框架点击**（正常时应有） |
| 帧缓冲独占 | 仅 `PID 118` 持有 `/dev/fb0`，无 `demo_app` 竞争 | 排除双应用抢帧 |

> 关键否定性证据：**触摸设备在产出事件、demo 在正常消耗 CPU、帧缓冲无人竞争**。三者同时成立 ⇒ 问题在"事件从设备到框架"的**搬运/唤醒**环节，不在驱动、不在渲染实现。

### 0.3 与渲染黑块战役的关系（两件事，别混）

同一晚的**渲染黑块**（阶梯状黑带）与本次**触摸无响应**是**两个独立根因**：

| | 渲染黑块 | 本次触摸/渲染停滞 |
| --- | --- | --- |
| 根因 | CMake 布尔转发把枚举开关 `RENDER_MODE=DIRECT` 透传成未定义标识符 ⇒ 实际编出 **PARTIAL** | 触摸 fd **未注册进 epoll 等待集** |
| 症状 | 面板阶梯状黑块（`\|x−2y\|≤320` 斜带） | 触摸无响应 + 画面静止 |
| 状态 | ✅ 已修（CMakeLists 枚举映射，见 [cross-compile-mcgs-armel.md](../cross-compile-mcgs-armel.md) §2.1.1） | ✅ 已修（本文 §三 方案 A） |

---

## 一、根因链路

### 1.1 触摸事件的通路（改造前）

```
/dev/input/event0 (fd 6)
   │  ← 驱动产出事件（实测 96 字节/次，可用）
   ▼
xpfi_pump()                        Drive/Posix/Graphics/XPlatformFbInput_posix.c:1145
   │  非阻塞 read 抽干，解析成多点触摸状态机
   ▼
XAbstractEventDispatcher_addPollCallback(xpfi_pump, NULL)   ← ① 挂在「轮询回调链」
   │  回调链只在**事件循环圈边界**执行，不参与阻塞等待
   ▼
XWindowSystemInterface_handleTouchPoints_ex → XWidget_dispatchTouchEvent → 合成鼠标
   │
   ▼
demo 日志「mousePress button=1」（本次缺失）
```

### 1.2 事件循环的阻塞等待

```
XEventLoop_exec                                        Src/XCode/XEvent/XEventLoop.c:117
   while (!exit) XEventLoop_processEvents(loop, WaitForMoreEvents | EventLoopExec)
      │
      ▼
VXAbstractEventDispatcher_processEvents                 Src/XCode/XEvent/XAbstractEventDispatcher.c:613
   1. XDeviceTimer_process(...)        ← 定时器兑现
   2. xPollDispatch(...)                ← ★ 轮询回调链（触摸泵在此）
   3. 投递事件派发
   4. 阻塞等待：
        timeoutMs = min(最近定时器剩余, 20ms)
        XAbstractNetIoRing_waitForEvents_base(ioRing, timeoutMs)
           │
           ▼
        VXNetIoRingPosix_waitForEvents                 Drive/Posix/Core/XNetIoRingPosix.c:880
           ├─ io_uring 引擎：poll({ringFd, wakeFd}, timeoutMs)
           └─ epoll 引擎（★ 本机 5.2.9 走这条）：
                epoll_wait(m_epollFd, events, MAX, timeoutMs)
                等待集 = { wakeFd(eventfd) } ∪ { 网络 socket fd }
                ★ 触摸 fd 不在其中
```

### 1.3 两处断裂

**断裂 A（已修）：触摸 fd 不在 epoll 等待集 ⇒ 触摸无法唤醒阻塞**

- `epSetup()`（`XNetIoRingPosix.c:792`）只注册了 `m_wakeFd`；
- 其余 fd 是**提交网络 SQE 时按需 ADD**（`:1289`：`epoll_ctl(EPOLL_CTL_ADD, sqe->fd, ...)`）；
- `VXNetIoRingPosix_registerEvent()`（`:874`）是**空桩**（`(void)fd; return true;`）——"被动 fd 注册通道"在 Posix 后端**没有实现**；
- `XAbstractNetIoRing_registerEvent` 的基类实现（`XAbstractNetIoRing.c:113`）同样空桩。

⇒ 触摸泵**只能靠圈边界**被驱动。

**断裂 B（已定位，非阻塞源）："FPS 2.5" 不是病态指标**

实测 FPS 2.5 曾被当作「圈长 400ms」的证据。实施后复测发现：**空闲态 FPS 1.1～2.5 是正常值**——空闲闸门（`XGUI_DEMO_IDLE_GATE`，默认开）下 demo 不逐帧重绘，只由 1Hz 悬浮层定时器驱动局部重绘，FPS 浮层测的是**绘制活动率**而非面板刷新率。**交互态**（点导航按钮切页）实测 **FPS 80.4** ⇒ 事件循环圈长并未异常。

⇒ 真正的病态指标是「**触摸注入后无 mousePress / 画面不变**」，而非 FPS 数值本身。**教训：HUD 上的 FPS 是活动率指标，不能直接当刷新率指标读**（同 [[repaint-max-fps-1025]] 的定论）。

> ⚠️ 已排除：死锁（CPU 在涨）、驱动无事件（实测有事件）、帧缓冲竞争（无竞争）、渲染实现错误（画面内容正确，静止而已）、圈长异常（交互态 FPS 80 反证）。

---

## 二、框架侧既有可复用机制

### 2.1 Linux 异步后端的双引擎回退（`Drive/Posix/Core/XNetIoRingPosix.c`）

| 引擎 | 启用条件 | 本机 |
| --- | --- | --- |
| `XNET_MODE_IOURING` | 内核版本 ≥ **5.10**（用户裁定 2026-10-16 的版本门）；`XNET_FORCE_IOURING=1` 可越门 | ✗ 内核 5.2.9 |
| `XNET_MODE_EPOLL` | io_uring 不可用时**自动回退**（`XNetIoRingPosix.c:1100` 附近 `if (!enabled && epSetup(ring))`）；`XNET_FORCE_EPOLL=1` 可强制 | ✓ **本机走这条** |

初始化链（`XNetIoRingPosix_init`，`:1045` 起）：`ioSetupRing` 成功 → IOURING；否则 `epSetup` → EPOLL；都不成 → `enabled=false`（此时框架不阻塞，退化为纯忙轮）。

### 2.2 已有的跨线程唤醒通道

- `m_wakeFd`：`eventfd(0, EFD_NONBLOCK|EFD_CLOEXEC)`，注册进 epoll 等待集（`:809`）；
- `VXNetIoRingPosix_wakeUp()`（`:1000`）：向 wakeFd 写 1 ⇒ **epoll_wait 立即返回**；
- epoll 引擎的 wait 循环里对 wakeFd 事件**只读不派发**。

⇒ wakeFd 是一条现成的"任意来源唤醒阻塞"通道，且不受引擎差异影响。

### 2.3 缺口（已补）

`registerEvent` 在**两个层级都是空桩**，没有"把一个被动可读 fd 纳入等待集"的 API。

---

## 三、方案 A（已实施）：epoll 引擎补「被动 fd 注册」通道，触摸 fd 入等待集

### 3.1 改动清单

| 文件 | 改动 |
| --- | --- |
| `Src/XCode/XEvent/XAbstractNetIoRing/XAbstractNetIoRing.h` | 新增两个**追加式**虚函数（`RegisterPassiveOsFd` / `UnregisterPassiveOsFd`，追加在表末，既有 9 项索引不变 ⇒ **ABI 面零破坏**）+ 两个 `_base` 包装声明 |
| `Src/XCode/XEvent/XAbstractNetIoRing/XAbstractNetIoRing.c` | 基类默认实现（注册返回 false = 不支持；注销幂等 true）+ `_base` 包装实现 + 虚函数表追加两项 |
| `Drive/Posix/Core/XNetIoRingPosix.h/.c` | **SQE/CQE 类型统一（2026-10-26 晚）**：原文件内有一份「与 io_uring 同名同布局」的伪 `struct io_uring_sqe/cqe`，但**与内核真实 ABI 不一致**（sqe 72 字节 vs 内核 64；`addr`/`off` 位置互换；缺 `poll_events`/`flags`），且与真头两套定义并存 ⇒ 同一份源码在两种工具链下得到不同字节布局。统一为：<br>· **有真头**（桌面/armel 工具链现状）：用内核真实结构体，并以 `_Static_assert` 锁死 ABI 偏移（sizeof(sqe)=64、poll_events@28、user_data@32、cqe.flags@12 等）；<br>· **无真头**：io_uring 引擎本就不编译（`XNET_BUILD_IO_URING=0`），该结构体只被 epoll 引擎当「操作描述载体」用，故改名 `XNetPseudoSqe/XNetPseudoCqe`（自有类型，与内核 ABI 解耦，字段补齐 flags/ioprio/cancel_flags 与内核同名对齐）；<br>· C 文件与外部调用点（`XDeviceNetwork_posix.c`/`XDeviceFile_posix_api.c`）一律改用 `XNetSqe`/`XNetCqe`，**不再直接写 `struct io_uring_*`** ⇒ 消除「两套同名不同布局」隐患。<br>验证：桌面（有真头）与强制无头两条路径均编译通过；双引擎探针全绿。 |
| `Drive/Posix/Core/XNetIoRingPosix.c` | **双引擎落实**。epoll 引擎：`epoll_ctl(ADD)` 纳入等待集 + 被动 fd 记录表（`m_passive[]`，容量 8）+ wait 循环识别被动 fd 就绪 → 回调属主；注销走 `EPOLL_CTL_DEL`。io_uring 引擎：**multishot POLL_ADD**（`IORING_POLL_ADD_MULTI`，一次挂载持续触发、无需重挂），完成识别靠专属 `XEventContext`（`type=Type_Custom`，**按设备类型标注区分、不用哨兵**）；multishot 被内核拒绝（5.10~5.12）时登记 `m_multiFailed` 退回轮询兜底 |
| `Drive/windows/Core/XNetIoRingWin32.c` | **显式**实现（恒 false + 原因说明）：IOCP 只接纳与 OVERLAPPED I/O 绑定的 HANDLE，无「只读设备 fd 挂进完成端口」的原语；Windows 输入本就由窗口消息驱动，不需要本通道 |
| `Drive/Posix/Graphics/XPlatformFbInput_posix.c` | 注册时把 evdev fd 经新 API 纳入等待集（回调 `xpfi_onPassiveReady` → 立即 `xpfi_pump`）；`unregister` 时**先注销再关 fd**（避免等待集留失效 fd）；日志增印「唤醒通道=事件环等待集 / 轮询回调兜底」 |

### 3.2 为什么能修好

触摸到达 ⇒ 内核直接唤醒 `epoll_wait` ⇒ 事件循环**同一次唤醒**里回到圈首 → `xPollDispatch` 立刻执行触摸泵 ⇒ **触摸延迟从「等一圈」降到亚毫秒**。轮询回调链**保留**（兜底），故注册失败时行为退化为改造前口径，无功能回退。

### 3.3 关键设计取舍

- **新增独立 API 而非改 `registerEvent` 语义**：`registerEvent` 收 **XFd**（fd 表槽位号），而 evdev fd 是**裸 OS fd**（内核编号空间）——两套编号空间（实测槽位 2 / OS fd 8）。套用会注册到错误槽位。故另立 API 并在头文件注明「勿混用」。
- **io_uring 侧用 multishot POLL_ADD**（2026-10-26 晚补齐）：初版用「一次性 POLL_ADD + 完成重挂」，桌面 6.18 真机实测踩到「同一 ring 第 3 次挂等同 fd 不再触发」的坑（SQE 字节级与裸程序一致、enter 消费正常、CQE 永不再来；同进程新建 ring 同序列正常、裸 ABI 复刻全序列正常——根因未定）。改用 multishot（5.13+，一次挂载持续触发）后桌面探针全绿，且天然规避「重挂时数据未读空 → CQ 风暴」（初版就地重挂实测 notifyCount=128=CQ 容量）。5.10~5.12 内核 multishot 不可用（-EINVAL）→ `m_multiFailed` 登记后回落轮询兜底。**教训：一次性 POLL_ADD 的重挂语义在真内核上有隐蔽坑，持续型监听优先 multishot**。
- **Windows 侧显式 false 而非"未重载"**：语义可读，行为等价。

### 3.4 真机验证结果

| 项 | 改造前 | 改造后 |
| --- | --- | --- |
| `fdinfo/3` 等待集 | `tfd: 8/7/4`（无 6） | **`tfd: 6`**（触摸 fd 在位） |
| 触摸→`mousePress` 延迟 | 无（事件不产生） | **亚毫秒**（注入间隔 1.21s ↔ 事件时间戳差 1.212s） |
| 导航按钮点击 | 无响应 | **页面切换成功**（`switch page=13`）+ 画面 md5 改变 |
| 交互态 FPS | — | **80.4** |
| 画面黑区 | — | 2.32%（无黑块回归） |
| 桌面 all 构建门 | — | RC=0 |
| 桌面回归门 | — | 失败集与基线**逐条相同**（零新增） |

屏上现役二进制：`md5 0001018bc4a4cfc9677d5de3d8735e98`（含触摸唤醒通道 + io_uring multishot 实现；armel 5.2.9 运行时走 epoll 引擎）。独立探针 `XDeviceIoUPassiveProbe [epoll|iouring]` 可随时复验双引擎。

---

## 四、遗留与后续

1. **真实手指触摸未验**：真机验证用的是 `xinj2` 注入（走 evdev 同路径，坐标经 pointercal 变换），与手指路径一致但未人工确认；建议人工点一次确认。
2. ~~io_uring 引擎侧未实现被动 fd 通道~~ **已实现（multishot POLL_ADD）**：桌面 6.18 真机探针 `XDeviceIoUPassiveProbe iouring` 全绿（engine=IOURING，注册/唤醒/持续触发/注销全通，fdinfo 判定引擎身份）；armel 5.2.9 内核 <5.10 版本门走 epoll（多挂验证无回归），5.10~5.12 内核 multishot 不可用时自动回落轮询兜底（`m_multiFailed`）。
3. **并发 lane 冲突**：屏幕休眠功能（`XPlatformPower*`）由另一条 lane 开发中，本次落地时其 armel 全量构建曾因未定义的 `XPlatformPowerCap_ScreenOff` 失败（该符号其后已由对方修正）。按"绝不修别人的半成品"纪律，本次未触碰其代码。

---

## 五、验证口径（本次实施实测）

| 层 | 判据 | 实测 |
| --- | --- | --- |
| 设备·通道 | `fdinfo/3` 含 `tfd: 6` | ✅ |
| 设备·触摸 | 注入点击 → 日志出现 `mousePress button=1 pos=(...)` | ✅ 坐标精确 (500,400)→(500,399) |
| 设备·端到端 | 点导航按钮 → 页面切换 + 画面 md5 变化 | ✅ `switch page=13` |
| 设备·画面 | 黑区占比 ≈2%，不得回升到 52% | ✅ 2.32% |
| 设备·交互帧率 | 交互态 FPS 回到 ~80（空闲 1~2.5 是活动率正常值） | ✅ 80.4 |
| 桌面·构建 | `wf_all_gate.sh` RC=0 | ✅ |
| 桌面·回归 | `wf_regression_gate.sh` 失败集与基线一致 | ✅ 逐条相同 |
| 设备卫生 | 抓帧写 `/root` 不写 `/tmp`；诊断脚本不得 OOM 被测进程 | ✅ 清理后根分区 70% |

---

## 六、附：证据链溯源

| 结论 | 证据 |
| --- | --- |
| 非死锁 | `/proc/<pid>/status` = S；3 秒 CPU tick 增量 10 |
| 阻塞点 | `/proc/<pid>/wchan` = `do_epoll_wait`；`/proc/<pid>/syscall` nr=0 fd=3（eventpoll） |
| 触摸 fd 未注册 | `/proc/<pid>/fdinfo/3` 仅 tfd 8/7/4，无 tfd 6；触摸 fd=6 |
| 驱动有事件 | `dd if=/dev/input/event0` 读到 96 字节（4 个 input_event） |
| 事件未进框架 | demo.log 无 `mousePress button=1`（正常版本有） |
| 圈长非病态 | 交互态 FPS 80.4 ⇒ 圈长正常；空闲 1~2.5 是「绘制活动率」 |
| epoll 等待集语义 | `XNetIoRingPosix.c:792` epSetup 仅 ADD wakeFd；`:1289` 提交时按需 ADD 网络 fd；`:874` registerEvent 空桩 |
| 引擎选择 | `XNetIoRingPosix.c:1045` init：版本门 ≥5.10 → io_uring，否则 epoll 回退；本机 5.2.9 → epoll |
| 触摸泵挂载点 | `XPlatformFbInput_posix.c:1145` xpfi_pump；经 `XAbstractEventDispatcher_addPollCallback` 挂链 |
| fd 两套编号空间 | `XNetIoRingPosix.c:268-273` 注释（槽位 2 / OS fd 8 实测） |

---

*2026-10-26 · 昆仑通态屏触摸无响应诊断 → 方案 A 实施 → 真机验证通过*

