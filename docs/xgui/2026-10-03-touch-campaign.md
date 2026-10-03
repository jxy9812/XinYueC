# 2026-10-03 触摸全平台接入战役总结

> 2026-10-03 战役（框架层手势状态机 + Win32/X11/Android/fbdev 四平台真触摸接线）+ 独立复审与修复 + 门禁收口 · 交付总结。
> **结论先行**：四平台触摸已全部改走真 XTouchEvent 管线并统一手势语义（单击=左键、双击=DBL_CLICK、长按=右键/CONTEXT_MENU、拖动=滚轮）；已知问题 K1–K7 收口（K7 复用既有零改动）；独立复审 8 条 P1/P2 发现当日全部确认并修复，修复后门禁复验通过；**x64 构建、x86 构建、demo autotest、回归套件全部通过**。
> **行号基线**：本文所有代码锚点为 2026-10-04 文档会话实读（复审修复收口后）状态，逐一用 findstr/Read 重定位核验；复审发现的"位置"列保留**修复前**行号（战役记录原文），"整改"列为修复后实读锚点。同日二轮复审（§4.3）再改动五个涉改文件（文件 mtime：android.c 03:37 / posix.c 03:40 / win32.c 03:46 / XWidget.c 与 fbdev.c 04:24），涉二轮条目的描述与锚点以 §4.3 实读为准。

## 〇、结论速览

1. **架构铁律落地**：平台层只做翻译（帧聚合/状态映射/id 透传/坐标逻辑化），手势判定全部收敛在 `Src/XGui/Widget/XWidget.c` 的模块级手势状态机；四平台注入统一走 `XWindowSystemInterface_handleTouchPoints_ex`（Src/XGui/Window/XWindowSystemInterface.h:432）。
2. **平台缺口清零**：Win32 零 WM_POINTER（触摸被 OS 提升成鼠标，K1）、X11 丢 tracking id/坐标截断（K2）、Android 只取 index 0（K3）、fbdev 无 ABS_MT_SLOT（K4）——四路全部改写为多点真触摸注入。
3. **手势语义四件套**（K5/K6 一并收口）：合成滚轮带 phase/source（XWidget.c:2160/:2163）；`pressAndHoldInterval` 迎来首个消费者（XWidget.c:1951-1958）。
4. **独立复审 8 条 P1/P2 全部确认并当日修复**：含 3 条 P1（X11 XI2 事件镜像 LP64 错位、fbdev 纯 ST 屏误判 MT、fbdev 混合帧丢抬起沿），修复落点已逐条读码核实（见 §4.2），修复后门禁全量复验通过。
5. **门禁全绿**：`quiet_build_x64.bat` / `quiet_build_x86.bat` / `quiet_autotest.bat` / `quiet_regression.bat` / 终门 `diag/touch_baseline.bat` 全部通过（退出码门 BUILD_EXIT=0 / AUTOTEST_EXIT=0 / REGRESSION_EXIT=0 / X64BUILD_EXIT=0）。
6. **边界照实登记**：真触摸硬件未实测（本机 RDP 无触屏，平台翻译层靠静态评审+复审）；posix/android/fbdev 三个平台文件在 Windows 无法编译，仅静态核；X11 协议无原生触摸 CANCEL 源（§5）。

---

## 一、背景与目标

### 1.1 背景（战役前侦察结论，已逐一核实）

- **框架层管线完备但只有"仿真"**：XTouchEvent/XTouchPoint 负载（Src/XGui/Window/XWindowEvent.h:775-852）、WSI 注入入口 `handleTouchPoints_ex`（Src/XGui/Window/XWindowSystemInterface.c:789-817）、控件层 per-id 抓取表均已就绪；控件层 `XWidget_dispatchTouchEvent` 旧逻辑对未接受序列做无条件 touch→mouse 仿真（press 随 BEGIN、move 随 UPDATE、release 随 END），无双击/长按/拖动手势。
- **四平台各有缺口**：Win32 完全没有 WM_POINTER 接线（触摸依赖 OS 提升鼠标消息）；X11 XI2 路丢 detail（tracking id）、坐标截 short、自补的 `XIPointerEmulated` 过滤是死代码；Android 只取 pointer index 0、丢 POINTER_DOWN/UP/CANCEL；fbdev 无 ABS_MT_SLOT 多槽、按压来源"同帧最后写入者"覆盖混乱。
- **框架小缺口**：XWheelEvent 的 phase/source 字段零填充（K5）；XStyleHints 的 `mousePressAndHoldInterval`（默认 800ms）零消费者（K6）。
- **远端协议通道已就绪**：K7 触摸通道复用既有（Src/XGui/Remote/XGuiServer.c:644 `xgs_injectTouch`，:1380 消费），本战役零改动。

### 1.2 目标

1. 四平台（Win32/X11/Android/fbdev）触摸输入全部改走真 XTouchEvent 管线，平台层只翻译、不判定。
2. 框架层统一手势语义：**单击=左键**（press@BEGIN/release@END）、**快速双击=DBL_CLICK**（替代两组 press/release）、**长按=右键**（未被接受时框架自动弹 CONTEXT_MENU，再补右键 release 配对）、**拖动=滚轮滚动**（ScrollBegin/Update/End 相位）。
3. 收口已知问题 K1–K7；门禁（x64 构建 + x86 构建 + demo autotest + 回归套件）全绿。

---

## 二、架构

### 2.1 分层与数据流

```
原生输入（WM_POINTER / XI2 Touch / AMotionEvent / evdev SYN_REPORT）
   │  平台层只翻译：帧聚合、状态映射、id 透传、坐标逻辑化、时间戳透传
   ▼
XWindowSystemInterface_handleTouchPoints_ex(window, type, XTouchPoint*, count, timestamp)
   │  （XWindowSystemInterface.h:432；触摸四态 PRESSED/UPDATED/STATIONARY/RELEASED）
   ▼
XWidget_dispatchTouchEvent（Src/XGui/Widget/XWidget.c:2436）
   │  逐点状态过滤 → childAt 命中 / per-id 抓取路由 → 按靶分组派发
   ▼
主点手势状态机 xwidget_touchGestureStep（XWidget.c:2356，进入门 :2554-2556）
   │  单击/双击/长按/拖动 → XWidget_synthesizeMouseFromTouch 合成鼠标/滚轮
   ▼
既有鼠标管线（dispatchPointerEvent 命中派发；右键未接受自动弹 CONTEXT_MENU）
```

- **平台层零手势判定**：四平台注入的都是"整帧触点列表 + 四态"，双击窗口、长按间隔、拖动阈值全部在框架层。
- **多点状态过滤**（任务 B）：BEGIN 只派发 PRESSED 点、UPDATE 只派发 UPDATED 点、END 只派发 RELEASED 点（XWidget.c:2473-2485）——真帧聚合负载里未变化的伴点以 STATIONARY 随行，不过滤会被二次投递；旧单点负载（无触点列表）不过滤，与旧单指针模型逐位一致（:2486-2498）。
- **抓取与收口**：END 按事件携带 id 逐 id 摘抓取表（:2559-2566）；**CANCEL 无条件收口**（手势状态机 `xwidget_touchGestureAbort` + 抓取表全清，:2567-2573）——即便主点已被他人抓取导致状态机带门跳过，长按定时器/挂起 press 也必须随序列异常终止回收。

### 2.2 手势状态机（XWidget.c 模块级静态，主点驱动）

- **状态**：`XWidgetTouchGesture`（单序列 primaryId/begin/last/beginMs/synthPressSent/dragging/longFired/armedDouble/wheelSent/accX/accY/active/m_top 等 + 跨序列 lastTapEndMs/X/Y，XWidget.c:244-266，单例 :269）。
- **参数三读取器**（风格抄 XLineEdit.c:597-625，StyleHints 优先、常量回退）：双击 `XApplication_doubleClickInterval()`（:1943）、长按 `XStyleHints_mousePressAndHoldInterval()` 默认 800（:1951-1958，**首个消费者**）、拖动 `XApplication_startDragDistance()` 默认 10。**手势时钟**优先 `XWindowSystemInterface_touchTimestamp()` 域、ts=0 回退墙钟（:1985-1991）；跨域混合由 `now<beginMs` 防御跳过（:2108）。
- **单击=左键**：BEGIN 合成 LeftButton press（:2217-2225），END 同点 release；tap 收口记录 lastTapEnd 供双击布防。
- **双击=DBL_CLICK**：上次 tap 收口后 `doubleClickInterval` 窗口内、起点与收口点 manhattan 距离 ≤`startDragDistance` 且时间戳单调（`now>=lastTapEndMs`，防跨组回退误布防）→ 布防 armedDouble（:2208-2216），本序列 BEGIN 不合成 press；tap 收口改发 DBL_CLICK+RELEASE（`xwidget_touchGestureStep` 内）。
- **长按=右键**：BEGIN 布防定时器（:2226-2235）+ UPDATE/END 惰性兜底（:2101-2113，注入式测试无需真等 800ms）；触发 `xwidget_touchGestureFireLongPress`（:2068-2096）：已合成左键 press 时以**远偏移点**（begin+(4096,4096)，32767 钳位防 short 回绕，:2049-2061）release 关断左键序列（防按住中的按钮被误判 click），再合成 RightButton PRESS（右键未接受时框架自动合成 CONTEXT_MENU，XWidget.c:1669-1710 契约）+ RightButton RELEASE；longFired 后本序列 UPDATE/END 不再合成鼠标事件。
- **长按定时器专用宿主**（复审整改 §4.2 #1；**二轮重构，现行以 §4.3 ⑥ 为准**）：模块自持 **XObject 派生静态宿主** `xwidget_touchTimerHost`（XWidget.c:2013-2096，is_widget=0/无几何/静态存储零堆分配，惰性构造一次），其自有虚表槽即 `xwidget_touchTimerHost_timerEvent`（:2050-2060），到期事件必达；布防 :2301-2306，序列真实顶层另记 m_top（:264-265）。初稿记载的「裸 XWidget 宿主 + VXWidget_timerEvent」方案已整体移除。
- **拖动=滚轮**：超 `startDragDistance` 转拖（撤长按表、远偏移释放关断左键序列）；此后位移累积合成滚轮 `xwidget_touchGestureEmitWheel`（:2124-2167）：k=2 角度换算（60px=120 角度）、纵向取反/横向不取反、主导轴、整格发送余数保留、phase=ScrollBegin/Update/End（:2160）、source=SynthesizedByQt（:2163）。
- **接受门**：BEGIN 被控件接受（primaryAccepted）→ 序列归接受控件所有，状态机**整批不跟进**（不合成 press/滚轮、不布防长按，:2185-2191）——"同一 BEGIN 只走 touch 或合成一条路"；否则被接受序列拖动会向应用注入幻影滚轮。

---

## 三、各平台改动点

### 3.1 框架层（K5/K6 + 状态机 + 测试）

- `Src/XGui/Widget/XWidget.c`（最大件）：移除序列级 `g_touchMouseSynthActive`，旧无条件仿真块替换为带门手势步骤（进入门 :2554-2556、CANCEL 尾部无条件收口 :2567-2573）；新增手势实现块（KillTimer/Reset/专用定时器宿主/FarPoint/FireLongPress/LazyLongPress/EmitWheel/Begin/Update/End/Abort/Step，初稿锚点 :1994-2363；**二轮宿主重构后漂移**：EmitWheel :2194 起、Abort :2408、Step :2427 起）与宿主定时器槽（初稿 `VXWidget_timerEvent` :2394-2410 + vtable TimerEvent 重载 :3039，二轮已移除、改为 XObject 派生宿主自有槽，§4.3 ⑥）。`XWidget_Protected.h` 未改（无新增对外契约）。
- `Test/XGuiTest/XTouchMultiPointTest.c`：新增手势 sink `TpGestureSink`（XWidget 子类，覆写鼠标按/放/双击/CONTEXT_MENU/滚轮五虚槽计数，:113-177）与注入助手 `tp_gestureStep`（:182）；run() **新增第 4 节**「主点手势状态机回归」（:106）：tap（press/release 各 1）、double-tap（dblClick=1、第二序列不合成 press）、drag→wheel（wheel≥1、angleDelta.y=-120<0、phase=ScrollBegin、END 补 ScrollEnd、远偏移释放不落回靶）、长按惰性路径（contextMenu=1、rightPress=1、END 不再合成）；组间注入 CANCEL 清状态（:351/:383/:400）。既有第 1-3 节（多点交错/单点回归/生命周期）一字未动。

### 3.2 Win32（K1）—— `Drive/windows/Graphics/XPlatformNativeWindow_win32.c`

- **新增 WM_POINTER 独立节**：消息号 `#ifndef` 本地兜底（WM_POINTERUPDATE=0x0245/DOWN=0x0246/UP=0x0247，:1027-1034；WM_POINTERCANCELED 0x0248 未分配，:1023-1026）、`POINTER_FLAG_CANCELED` 本地定义（:1055）、SDK 结构本地镜像（XPwnPointerInfo/TouchInfo/PenInfo）、GetProcAddress 惰性装载 `xpwn_pointerApisInit`（:1113-1151，Win7 缺 API 整节旁路）。
- **主 handler `xpwn_handlePointerTouch`**（:1218）：两段式帧聚合（:1260-1266）、`SkipPointerFrameMessages` 丢弃同帧其余排队消息防 N 倍重复注入（:1267-1270）、帧内任一成员带 CANCEL 位即**整帧按 TOUCH_CANCEL/RELEASED 注入**（:1271-1281；笔同口径 :1317-1320）、未接触悬停不注入（:1284-1287；笔 :1321-1323）、UPDATE 帧非本消息 id 标 STATIONARY（:1301-1307）、PT_PEN 单点压力 pressure/1024 归一、0 压回落 1.0（:1328-1329）；BEGIN 注入成功 SetCapture、END/CANCEL/CAPTURECHANGED 释放（:1212-1213、:1229-1233）；只消费 PT_TOUCH/PT_PEN，PT_MOUSE/PT_TOUCHPAD 落回 DefWindowProc（:1236-1238）。
- **wndProc 接线**：五 case 消费即 return 0 抑制 OS 提升（:1662-1672）；**防双投第二保险**：`xpwn_mouseExtraInfoFromPointer`（MI_WP_SIGNATURE 0xFF515700 签名过滤，:1154-1166）在鼠标按键/MOUSEMOVE/滚轮入口吞掉提升的合成鼠标消息（:1689/:1724/:1762），真鼠标无签名不受影响。

### 3.3 X11（K2）—— `Drive/Posix/Graphics/XPlatformNativeWindow_posix.c`

- 守卫内新增 `#include <math.h>`（:136，lround 坐标四舍五入）；本地镜像 `XGuiXIDeviceEvent` 布局修正（:164-196，见 §4.2 #4）。
- `xpwn_dispatchXi2TouchEvent`（:3056）：Touch 三类映射四态（**TouchEnd 只映射 TOUCH_END**——X11 协议无原生触摸 CANCEL 报文，:3097-3101）；`detail → m_id` per-id 透传（:3103-3104，多点抓取的匹配依据）；坐标先 `(int)lround(event_x/root_x)` 再经 `xpwn_eventPosToLogical` ÷dpr，对齐鼠标路 ButtonPress 口径（:3105-3114）；`pressure=1.0f`；注入 `handleTouchPoints_ex(win, type, &point, 1, dev->time)`（:3115-3117）。
- **模拟报文问题收口为「不按 emulated 标志过滤」**（:3073-3083，见 §4.2 #5）：XI2 触摸选中生效后服务器已对该窗口抑制核心指针模拟，不存在双投；掩码选择与分派入口未动。

### 3.4 Android（K3）—— `Drive/Android/Graphics/XPlatformWindowAndroid.c`

- **队列单元整帧化**：`XPadTouchEvent {action(未掩码含 INDEX 位), count<=8, pts[8]{id,x,y}, timestampMs}`（:86-99，`XPAD_MAX_TOUCH_POINTS=8`）；`XPad_onInputEvent`（UI 线程）逐点 `getPointerId/getRawX/getRawY` + `getEventTime` ns→ms 整帧入队（:1488-1527）；半帧写入锁内重置队列（§4.2 #6，:1507-1515）。
- **`processPendingEvents` 按帧映射**（初稿锚点 :1028-1117；二轮后映射主体 :1018-1170）：DOWN→BEGIN（全 PRESSED）、POINTER_DOWN→BEGIN（actionIndex 点 PRESSED/其余 STATIONARY）、MOVE→UPDATE（**二轮改逐点判定**：坐标较 m_lastPts 缓存变化者 UPDATED/未变者 STATIONARY，§4.3 ⑤；初稿记"全 UPDATED"为修复前状态）、POINTER_UP→END（该点 RELEASED/其余 STATIONARY）、UP→END（全 RELEASED）、CANCEL→TOUCH_CANCEL（全 RELEASED），经 `handleTouchPoints_ex` 注入（:1160-1162）；HOVER_MOVE 保留单点鼠标 MOVE（buttons 恒 NoButton，:1106-1122）；其余动作静默丢弃。
- **平台双击检测整链删除**（producer 侧 400ms/12px 判定、m_lastUp* 状态、DBL_CLICK 合成）——双击语义改由框架层手势状态机统一承担。

### 3.5 fbdev/evdev（K4）—— `Drive/Posix/Graphics/XPlatformFbInput_posix.c/.h`

- **8 槽协议 B**：`XpfiTouchSlot[8]{trackingId,active,injected,framePressed,frameReleased,havePoint,rawX/Y,lastX/Y}` + `g_xpfiMtProtocol`（:163-165）；旧单点状态机与平台双击合成（XPFI_DOUBLE_CLICK_*）删除。
- **`xpfi_feedEvent` 槽位状态机**（:476 起）：判据分工=BTN_TOUCH 全局接触 / TRACKING_ID 身份（>=0/-1 置按下/抬起沿）/ PRESSURE 仅坐标有效性；ABS_MT_SLOT 切槽、MT 设备忽略 ABS_X/ABS_Y（ST 仿真回显）。
- **`xpfi_commitFrame`**（:318）：逐槽沿判定收集触点数组，无变化不注入（:383-384）；类型选择 有按沿→BEGIN/有抬沿→END/否则 UPDATE（:455-457）；主点命中 + `XWidget_mouseGrabber()` 抓取兜底（按住滑出窗界跟抓取窗，:386-397）；**混合帧按状态拆分 END→UPDATE→BEGIN 三段注入**（§4.2 #8，:405-453）；注入失败按下沿保留重试、抬起沿消费即终（:458-473）。
- `xpfi_normalizePoint` 参数化 (rawX,rawY,outX,outY) 逐槽复用（:368），归一基准补 ABS_MT_POSITION 范围；注册期 `EVIOCGBIT(EV_ABS)` + `EVIOCGABS` 双信源协议探测（:700-731，纯 ST 屏防误判见 §4.2 #7）；unregister 复位槽表/协议标志（:776）。

---

## 四、已知问题收口表

### 4.1 K1–K7

| 编号 | 内容 | 收口落点 | 状态 |
|---|---|---|---|
| K1 | Win32 WM_POINTER 接线（触摸不再被提升成鼠标，synthesized 区分真触摸） | §3.2：WM_POINTER 独立节 + wndProc 消费即抑制（:1662-1672）+ MI_WP_SIGNATURE 双保险（:1689/:1724/:1762） | 已收口 |
| K2 | X11 detail→id / 模拟报文 / 坐标精度 | §3.3：detail per-id 透传（posix.c:3103-3104）、lround+÷dpr（:3105-3114）；模拟报文经复审改收口为「不按 emulated 标志过滤」（:3073-3083） | 已收口（含复审改道） |
| K3 | Android 多点帧注入（POINTER_DOWN/UP/CANCEL 全收） | §3.4：整帧队列单元 + 六动作→四事件映射（android.c:1028-1117） | 已收口 |
| K4 | fbdev ABS_MT_SLOT 多槽 + 按压优先级 | §3.5：8 槽协议 B + 判据分工 + 混合帧拆分（fbdev.c:318-473） | 已收口 |
| K5 | 合成滚轮 phase/source | XWidget.c `xwidget_touchGestureEmitWheel`：phase=ScrollBegin/Update/End（:2160）、source=SynthesizedByQt（:2163） | 已收口 |
| K6 | 长按判定（pressAndHoldInterval 首个消费者） | XWidget.c :1951-1958（默认 800ms）+ 定时器布防/惰性兜底（:2226-2235/:2101-2113） | 已收口 |
| K7 | 远端协议触摸通道 | 复用既有：Src/XGui/Remote/XGuiServer.c:644 `xgs_injectTouch`（:1380 消费），本战役**零改动** | 已就绪（无改动） |

### 4.2 复审确认问题与整改（8 条，全部当日修复，整改落点为本会话逐一读码核实）

独立复审按改动文件逐位评审 → P1/P2 逐条独立复核确认 → 修复 → 门禁全量复验。下表"位置"为复审时（修复前）行号，"整改"为修复后实读锚点：

| # | 位置（修复前） | 问题 | 级别 | 整改（修复后实读核实） |
|---|---|---|---|---|
| 1 | Src/XGui/Widget/XWidget.c:2188 | 长按定时器宿主=序列顶层，约 15 个叶子类（XLineEdit/XTabBar/XMenu/XAbstractButton 等）覆写 `EXObject_TimerEvent` 后链回 XObject 默认甚至不链回（XTabBar），到期事件被吞，长按在 XMenu 弹出窗/XProgressDialog 顶层等现实宿主上静默失效 | P2 | 改为模块自持**专用裸 XWidget 宿主** `g_touchGestureTimerHost`（懒构造、零尺寸+输入透明，XWidget.c:2014-2046），布防改挂宿主、真实顶层记 m_top（:2226-2235）；其 vtable 槽即 VXWidget_timerEvent，到期必达（**二轮重构**：方案整体替换为 XObject 派生静态宿主，见 §4.3 ⑥） |
| 2 | Drive/windows/…/XPlatformNativeWindow_win32.c:1031-1033 | `WM_POINTERCANCELED=0x0248` 消息不存在（SDK 消息族 0x0247 后直接 0x0249），CANCEL 分支是死代码，真实取消（拒掌）以 `POINTER_FLAG_CANCELED` 随常规 UP 消息到达，会被注入成干净 tap | P2 | 删除 0x0248 兜底（注释改记「0x0248 未分配」，:1023-1026、:1203-1207、:1669-1671）；帧内扫描 `POINTER_FLAG_CANCELED`（本地定义 :1055），任一成员置位整帧按 TOUCH_CANCEL/RELEASED 注入（:1271-1281、:1292-1293；笔 :1317-1320） |
| 3 | 同上 :1265-1266 | WM_POINTERUPDATE 帧内全部成员恒标 UPDATED 且不调 SkipPointerFrameMessages：并行 digitizer 每帧重报全部触点，静止伴指被重复派发、整帧重复注入 N 倍，架空控件层 STATIONARY 过滤 | P2 | UPDATE 帧仅本消息 id 标 UPDATED、其余标 STATIONARY（:1301-1307）；装载并对每帧调 `SkipPointerFrameMessages`（:1143-1145、:1267-1270；**二轮补齐 Android 半边**：m_lastPts 逐点缓存，见 §4.3 ⑤） |
| 4 | Drive/Posix/…/XPlatformNativeWindow_posix.c:171 | 本地镜像 XGuiXIDeviceEvent 多插了不属于 XIDeviceEvent 的 `XID cookie` 成员，LP64 下自 time 起全部字段错位 +8 字节（detail 读到 root、坐标互串、event 读到 child）——K2 全部改动建立在错位读取上，真机 XI2 触摸必坏 | P1 | 删除 cookie 成员，镜像与 libXi XInput2.h 逐字段同序同宽，并留布局契约注释（:164-196）；detail→id/lround 坐标/flags 读取在正确布局上成立 |
| 5 | 同上 :193 | `#define XIPointerEmulated (1<<2)` 与守卫内已含的 xorgproto XI2.h 同名宏（1<<16）异值重定义，且位值不命中任何服务器置位（指针族 bit16、触摸族对应位是 XITouchPendingEnd/XITouchEmulatingPointer bit16/bit17），过滤恒假死代码 | P2 | 删除自补宏，**不按 emulated 标志过滤**：触摸报文一律注入，注释记录三点依据（异值重定义/标志分族误吞/本后端 XI2 选中后服务器已抑制核心模拟无双投）（:3073-3083） |
| 6 | Drive/Android/…/XPlatformWindowAndroid.c:1507 | `XRingBuffer_write` 返回值被忽略：半帧写入使 108 字节定长读契约永久错位，此后每"帧"都是两帧拼接的垃圾，可向框架注入幻影触摸且永不自愈 | P2 | 校验返回值，≠sizeof(frame) 时**锁内重置队列**丢弃本帧，下一帧从块边界重新开始（:1507-1515） |
| 7 | Drive/Posix/…/XPlatformFbInput_posix.c:630-642 | 注册期 `EVIOCGABS(ABS_MT_POSITION_X/Y)` 调用成功即判 MT 屏：内核 evdev 对缺席轴也返回 0（只查 absinfo 存在），纯 ST 屏（TSC2007 类）被误判 MT → 忽略 ABS_X/ABS_Y 且无 TRACKING_ID → 永无按下沿，ST 屏触摸全死（旧鼠标路可用，属功能回归） | P1 | 仅当 `abs.maximum>0` 才置 haveMtAxis/MtProtocol（真实 MT 位置轴 max 恒>0，缺席轴读回 0），EVIOCGBIT 位测 + 轴存在性双信源（:700-731，判据注释 :710-715） |
| 8 | 同上 :374-376 | 混合帧（同帧一指抬一指落）取 pressedCount 优先整帧投 BEGIN，控件层 BEGIN 只派发 PRESSED 点 → RELEASED 点被丢弃且驱动侧已消费不重放：抬起沿永久丢失、该 id 抓取泄漏、控件停留按压态 | P1 | 混合帧按状态拆分三段注入：先 TOUCH_END（关旧序列）→ TOUCH_UPDATE → TOUCH_BEGIN（开新序列），各段簿记独立（抬起沿消费即终、按下沿失败保留重试）（:405-453） |

### 4.3 复审缺陷修复（2 条已修）

> **锚点基线**：本节为 2026-10-04 二轮文档会话实读（初稿 02:42 落笔后，五个涉改文件又经二轮改动：android.c 03:37、posix.c 03:40、win32.c 03:46、XWidget.c 与 fbdev.c 04:24，mtime 实查）。核对方式=Read/findstr 逐条读码；X11 两项另以 curl 实取上游头文件逐字段核对（方法见 ①，本会话实跑）。本会话未重跑构建/测试（同 §6.1 口径），二轮两处修复的门禁复验状态未在本会话验证。

二轮复审对一轮 8 项修复逐条复核（下述 ①–⑧ 即一轮 #4/#8/#7/#2/#3/#1/#5/#6 按文件重排），另新增复核点 ⑨（EVIOCGBIT 位图缓冲定容，初稿整改表未单列）。**其中 2 条发现残留缺陷并于本轮动码修复**：⑤ 的 Android 半边（一轮只修 Win32，Android MOVE 全帧 UPDATED 的同类缺陷未同步，架空控件层 STATIONARY 过滤——初稿 §3.4 亦按"全 UPDATED"记载）与 ⑥（一轮"裸 XWidget 宿主"方案二轮整体重构为 XObject 派生静态宿主）；其余 7 条复核确认一轮修复在当前代码中成立。

#### ① X11 镜像删 cookie 对齐上游 —— 一轮已修（§4.2 #4），本轮 curl 实证复核

- **现行代码**：本地镜像 `XGuiXIDeviceEvent`（Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:164-186）无 cookie 成员；布局契约注释逐字段声明与 libXi XInput2.h 同序同宽、LP64/ILP32 布局等价（:187-196）；辅助镜像 XIValuatorState/XIEventMask（:151-162）与上游同构。
- **curl 核对方法（本会话已实跑）**：

  ```
  :: XIDeviceEvent 布局核对（属 libXi 仓库；路径须含 include/X11/extensions/，
  :: 直链仓库根或 src/ 下无此文件，会 404）
  curl -sS "https://gitlab.freedesktop.org/xorg/lib/libxi/-/raw/master/include/X11/extensions/XInput2.h"
  :: emulated 标志位核对（⑦；这些宏属 xorgproto 的 XI2.h，不在 libXi 内）
  curl -sS "https://gitlab.freedesktop.org/xorg/proto/xorgproto/-/raw/master/include/X11/extensions/XI2.h"
  ```

  实跑取回 XInput2.h 21,969 字节 / XI2.h 11,151 字节。上游 `XIDeviceEvent` 字段序为 type/serial/send_event/display/extension/evtype/time/deviceid/sourceid/detail/root/event/child/root_x/root_y/event_x/event_y/flags/buttons/valuators/mods/group——本地镜像前 21 字段逐一同序同宽，仅省略末尾未读的 `group`（:194-196 注明其位于全部已读字段之后，不影响布局前缀），LP64 下自 time 起无错位。**对齐上游核对通过。**

#### ② fbdev 混合帧拆分注入 —— 一轮已修（§4.2 #8），复核确认

- **现行代码**：Drive/Posix/Graphics/XPlatformFbInput_posix.c `xpfi_commitFrame`（:320）：收集态非单一即拆——判定 `(pressedCount>0)+(releasedCount>0)+(updatedCount>0) > 1`（:411），三段注入 TOUCH_END（:428-434）→ TOUCH_UPDATE（:435-448，成功才落位）→ TOUCH_BEGIN（:449-465，成功簿记），各子集经 `xpfi_collectState` 独立收集；抬起沿消费即终不重放（:338-342）、按下沿失败保留沿重试；单一态帧走原路径按沿选型 BEGIN/END/UPDATE（:467-469）。

#### ③ fbdev EVIOCGABS maximum>0 判定 —— 一轮已修（§4.2 #7），复核确认

- **现行代码**：ABS_MT_POSITION_X/Y 仅在 `ioctl(...)==0 && abs.maximum > 0` 时置 haveMtAxis（fbdev.c:732-745），haveMtAxis 强制协议 B（:746-747）；判据注释记录内核行为依据——EVIOCGABS 对任意轴码只查 absinfo 是否存在，纯 ST 屏对缺席 MT 轴也 ioctl 成功并读回全零，仅凭调用成功会误判 MT 致触摸全死（:723-731）。

#### ④ Win32 POINTER_FLAG_CANCELED 读位 —— 一轮已修（§4.2 #2），复核确认

- **现行代码**：本地定义 `POINTER_FLAG_CANCELED 0x00008000`（Drive/windows/Graphics/XPlatformNativeWindow_win32.c:1058-1060）；消息族注释确认 0x0248 未分配、取消经该标志位随常规消息（典型 WM_POINTERUP）到达（:1021-1026）；触帧扫描任一成员置位即整帧 TOUCH_CANCEL（:1377-1384）、CANCEL 帧全点 RELEASED（:1395-1396）、悬停过滤对取消帧放行（:1387-1390）；笔分支同口径（:1423-1426）。

#### ⑤ Win32/Android STATIONARY + SkipPointerFrameMessages —— **本轮已修（Android 半边）**；Win32 半边=一轮 #3 复核确认

- **Win32（复核确认）**：`SkipPointerFrameMessages` 装载（win32.c:1145-1147）并于每帧取走后调用（触 :1373-1374、笔 :1421-1422）；UPDATE 帧仅本消息 id 标 UPDATED、帧内其余 id 标 STATIONARY（:1404-1408），DOWN/UP 伴点 STATIONARY（:1409-1410）。
- **Android（二轮修复）**：缺陷=motion 事件不指明变化触点，MOVE 全帧 UPDATED 使静止伴指逐帧重投抓取靶、架空控件层 STATIONARY 过滤（现行设计注释自述"对标 Win32 WM_POINTER 修复"，android.c:132-141）。修复=按触点 id 的上帧坐标缓存 `m_lastPts`（:140-141）：MOVE 帧逐点对照，坐标未变标 STATIONARY、变化标 UPDATED（:1072-1086），缓存无此 id（队列重置后首帧等）保守标 UPDATED 维持旧行为下限（:1070-1071）；POINTER_DOWN/POINTER_UP 伴点照旧 STATIONARY（:1058-1064/:1089-1095）；缓存刷新——UP/CANCEL 清空防残快照泄入下一序列、其余动作整体替换为当前帧快照（:1146-1158）。

#### ⑥ 长按专用定时器宿主 —— **本轮已修（整体重构）**；一轮"裸 XWidget 宿主"方案被替换

- **现行实现**：模块内部 **XObject 派生类** `xwidget_touchTimerHost`（Src/XGui/Widget/XWidget.c:2015-2017）——静态存储单例（:2042-2045，`Set_Class_IsHeap` 钉 false 零堆分配 :2080-2082）、非 widget（is_widget=0）、无几何，不进控件枚举/命中路径（自述 :2027-2033）；到期事件槽为宿主自有虚表槽 `xwidget_touchTimerHost_timerEvent`（:2050-2060，比对 m_longPressTimerId 命中则触发 `xwidget_touchGestureTimerExpired`），vtable `EXObject_TimerEvent` 重载（:2063-2070）；惰性构造一次（:2088-2096）。布防=对宿主 `XObject_startTimer_ms`（:2301-2306；序列真实顶层记 m_top，:2301/结构体字段 :264-265），撤销 KillTimer :1996-2000；异常收口注释"宿主永不析构"（:3372）。
- **与一轮方案的差异（照实记录）**：初稿 §2.2/§3.1/§4.2 #1 记载的"裸 XWidget 宿主（零尺寸+TransparentForMouseEvents 双保险）+ `VXWidget_timerEvent`（:2394-2410）+ XWidget 类级 vtable TimerEvent 重载（:3039）"已整体移除（`VXWidget_timerEvent` 在 XWidget.c 现零命中，findstr 核实）——宿主不再具有控件身份：无需双保险避命中面、不占堆账（内存 Soak 门禁不记账）、到期必达由宿主自有槽保证，分发器经 XObject_eventDispatcher 应用级回退取得（性质自述 :2030-2036）；手势结构体对应改 `m_longPressHost`/`m_longPressTimerId`（:260-263）。

#### ⑦ X11 emulated 过滤移除依据 —— 一轮已修（§4.2 #5），本轮上游位值实证复核

- **现行代码**：不按 emulated 标志过滤、触摸报文一律注入；三点依据注释（自补宏 `1<<2` 与 xorgproto 同名宏异值重定义且不命中任何服务器置位 / 标志位按事件类分族、指针族测试用在触摸事件会误吞 / XI2 触摸选中生效后服务器已对该窗口抑制核心指针模拟无双投）posix.c:3073-3084，并自记"真机语义需硬件验证"（:3083-3084，与 §5 第 2 条同口径）。
- **curl 实证（同 ① 第二条命令，本会话已跑）**：xorgproto XI2.h 中 `XIPointerEmulated (1<<16)`（:161）、`XITouchPendingEnd (1<<16)`（:163）、`XITouchEmulatingPointer (1<<17)`（:164）——与注释口径逐位一致：`1<<16` 在触摸事件是 XITouchPendingEnd，拿它当模拟标志过滤会误吞合法报文；触摸的模拟标志实为 bit17。**移除依据成立。**

#### ⑧ Android 队列短写 reset —— 一轮已修（§4.2 #6），复核确认

- **现行代码**：`XRingBuffer_write` 返回值校验（Drive/Android/Graphics/XPlatformWindowAndroid.c:1553-1554），≠sizeof(frame) 时锁内 `XRingBuffer_reset` 丢弃本帧、下一帧从块边界重新开始（:1556-1563）。108 字节定长读契约=XPadTouchEvent（:93-99：action 4 + count 4 + pts[8]×12 + timestampMs 4；XPadTouchPoint 为 id/x/y 三个 int32，:78-83；XPAD_MAX_TOUCH_POINTS=8，:86）；`XRingBuffer_reset` 契约在 Src/XContainer/XRingBuffer/XRingBuffer.h:109（实现 .c:333）。

#### ⑨ EVIOCGBIT 定容 —— 复核确认（初稿整改表未单列，二轮清单补录）

- **现行代码**：位图缓冲按被测位定容 `unsigned char absBits[(ABS_MT_TRACKING_ID + 7) / 8]`（恒 8 字节，覆盖全部 MT 位；fbdev.c:679-683）。不按 `ABS_MAX` 定容的理由（注释自述）：ABS_MAX 随内核头版本漂移（旧头 ABS_MAX=0x2f 时 (ABS_MAX+7)/8 仅 6 字节），EVIOCGBIT 按请求截短返回 → MT 协议探测静默失效，且 `absBits[ABS_MT_POSITION_X>>3]` 越界读；位测带返回字节数守卫 `absBitsLen > ABS_MT_POSITION_X/8`（:715-722）。

---

## 五、已知限制

1. **X11 无原生触摸 CANCEL 源**（协议限制）：XI_TouchEnd 只映射 TOUCH_END（posix.c:3097-3101），序列异常终止在 X11 上无 CANCEL 语义；其余三平台 CANCEL 通道已接（Win32 POINTER_FLAG_CANCELED、Android ACTION_CANCEL、fbdev BTN_TOUCH=0）。
2. **真触摸硬件未实测**：本机为 RDP 会话无触屏/笔硬件，WM_POINTER/XI2/Android/fbdev 四条平台翻译路均未在真机运行过；门禁只覆盖 WSI 注入层（autotest/回归）与 Windows 编译面，平台翻译层正确性=逐行静态核 + 独立复审 + P1/P2 复核 + 二轮复审复核（§4.3：7 条复核确认、X11 两项另以 curl 实取上游头文件核对布局/位值——curl 核对亦非真机验证，posix.c:3083-3084「真机语义需硬件验证」同口径；2 条本轮修复 ⑤Android 半边/⑥宿主重构同样仅静态核）。触屏设备到位后建议做一轮真机回归（重点：win32 双指 STATIONARY 去重、Android MOVE 静止伴指去重（m_lastPts 缓存）、fbdev 纯 ST 屏、Android 真机多点、长按在叶子控件宿主上的定时器触发）。
3. **pen-as-touch**：Win32 上 PT_PEN 走单点触摸通道注入（不成帧、压力/1024 归一、悬停不注入，win32.c:1311-1330），无独立笔语义；X11/Android/fbdev 无笔通道。
4. **触摸拖动滑条行为变化（有意变更）**：拖动现在合成滚轮（ScrollBegin/Update/End）而非等价鼠标拖拽滑块——滑条/滑块类控件对触摸拖动的响应从"跟手拖块"变为"滚轮步进"；且被控件接受的触摸序列不再合成任何鼠标事件（XWidget.c:2185-2191）。这是本批次的设计意图（对齐移动端惯例），非缺陷。
5. **posix/android/fbdev 无法在 Windows 编译**（`__linux__`/`__ANDROID__` 守卫裁空，Windows 宿主编为空 TU）：三平台文件仅静态核 + 评审，x64/x86 门禁只验证 Windows 编译面与全库公共层。
6. **长按定时器依赖事件分发器**：无事件分发器的环境 startTimer 静默不布防（宿主构造仍安全；宿主现为模块内 XObject 派生静态单例，§4.3 ⑥），长按仅由惰性兜底覆盖（下一个 UPDATE/END 到达才触发；XWidget.c:2033-2036 注释口径）——真实 GUI 应用均有事件环，不受影响。

---

## 六、验证

### 6.1 门禁命令与结果（全部通过）

| 门禁 | 命令 | 内容 | 结果 |
|---|---|---|---|
| x64 构建 | `Tools\windows\quiet_build_x64.bat` | 包装 `night_build_x64.bat`，退出码门 BUILD_EXIT=0 | 通过 |
| x86 构建 | `Tools\windows\quiet_build_x86.bat` | 32 位双架构构建（截断/类型宽度面），BUILD_EXIT=0 | 通过 |
| demo autotest | `Tools\windows\quiet_autotest.bat` | `bin\XGuiWindowDemo_Test.exe --autotest`，AUTOTEST_EXIT=0 | 通过 |
| 回归套件 | `Tools\windows\quiet_regression.bat` | `bin\XGuiRegression_Test.exe`，REGRESSION_EXIT=0 | 通过 |
| 终门全量基线 | `diag\touch_baseline.bat` | x64 构建 + demo autotest + 回归套件串行复验（X64BUILD_EXIT=0 / DEMOAUTOTEST_EXIT=0 / REGRESSION_EXIT=0） | 通过 |

- 门禁由**战役主脚本统一串行执行**（实现各路纪律禁止自跑构建/测试，避免并发互踩）；构建失败/测试失败转门禁修复手循环（构建 ≤6 轮、测试 ≤5 轮；复审修复后复验 ≤4/≤3 轮），全程收敛。
- 流程：改动前基线体检（同 touch_baseline.bat 三件套）→ 五路实现 → 构建门禁 → autotest+回归门禁 → 独立复审（每改动文件一位评审员）→ P1/P2 逐条独立复核 → 修复 → 门禁复验 → 终门（x86 + 全量基线）。本文档会话未重跑构建/测试，门禁结果取自战役门禁记录（终门 finalOk=通过）。

### 6.2 功能覆盖面

- **XTouchMultiPointTest**：既有第 1-3 节（多点交错抓取/单点回归/事件生命周期）保持通过；新增第 4 节四组手势断言（tap / double-tap / drag→wheel 相位与换算 / 长按惰性路径，XTouchMultiPointTest.c:106-400）全过。
- **demo autotest XI2-A 回归锁**：窗口级 TOUCH_BEGIN/END → touch→mouse 仿真切页签（Test/XGuiDemo/xgui_window_demo.c:1282-1299）保持通过——手势状态机对旧单点负载的 tap 路径逐位兼容。
- **回归套件**：全量通过（含既有触摸/鼠标语义锚点）。

### 6.3 未覆盖（照实登记）

- 真触摸硬件实测（见 §5 第 2 条）；
- posix/android/fbdev 三平台文件的本地编译运行（Windows 无法编译，仅静态核 + 复审）；
- X11 触摸 CANCEL 语义（协议无源，见 §5 第 1 条）；
- K7 远端协议通道为复用既有（Src/XGui/Remote/XGuiServer.c:644），未在本战役新增针对性用例。
