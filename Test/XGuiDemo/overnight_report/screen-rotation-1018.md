# 屏幕四方位旋转交付报告（2026-10-18）

主题：窗口内容四方位旋转（0/90/180/270）——呈现层旋转 + 输入逆变换 + 平台能力查询 + demo 接线 + 门禁与目验。

**核验口径**：本报告区分三类事实来源——
- 「实读」= 报告撰写轮用 sed/grep/ls/wc/comm 等只读命令在当前树上实跑的结果（命令与输出见各处标注）；
- 「材料」= 实现 lane 的实现材料与自检、目验 lane 的目验记录、审计 lane 的审计结论（本 ask 随附），撰写轮未重跑的部分如实标注；
- 门禁的构建/autotest/apitest 实际执行发生在实现/门 lane（分片日志落盘于 /tmp），撰写轮实读其落盘证据并重算差集，未重跑门脚本。

---

## 一、实现

### 1.1 呈现层旋转

**单一换算实现点**：`Src/XGui/Window/XWindowRotation.h`（实读存在，146 行）——
- `XWindow_rotationNormalize`（:37，任意整数折算到 0/90/180/270）；
- `XWindow_rotationSwapsAxesInline`（:55）；
- `XWindow_rotationUnmapPoint`（:88，设备坐标→逻辑坐标）；
- `XWindow_rotationMapRect`（:105，逻辑矩形→设备矩形）。
公开薄封装 `XWindow_rotationSwapsAxes` 在 `Src/XGui/Window/XWindow.c:1307-1310`（实读），转调本头文件 inline。

**X11（Drive/Posix/Graphics/XPlatformNativeWindow_posix.c，实读行号）**：
- footprint 对调：`xpwn_applyRotationFootprint`（:1390），四处调用 :3497 / :5222 / :5560 / :5691；
- present 旋转拷贝：present 路径内 `XWindow_rotationMapRect`（:6984，实读调用点）。「四步裁剪、180° 走反序快路径」的算法细节来自实现材料 summary，撰写轮未逐行核验该内层循环；
- 输入侧 R⁻¹：指针/XDND 命中经 `XWindow_rotationUnmapPoint`（:2775 / :2816 / :2853，实读），换算注释声明与 `XWindow_rotationMapRect/UnmapPoint` 严格互逆（:2722）。

**fbdev（Drive/Posix/Graphics/，实读行号）**：
- `XPlatformBackingStore_posix.c`：`xpbs_rotationCoversPanel`（:147，调用 :529）、`xpbs_presentRectRowsRot`（:168，调用 :866 / :880，逐像素旋转落笔）、脏区→设备矩形映射 :223、局部逆变换 :243；「同步账本按实际写入范围重算」为材料 summary 表述；
- `XPlatformFbInput_posix.c`：include `XWindowRotation.h`（:76，注释言「命中 footprint 与窗口局部坐标的 R⁻¹ 逆变换」），触摸/指针局部坐标逆变换 :312-319 与 :538-549（实读）。

**整帧脏闩**：`Src/XGui/Widget/XWidget.c` 的 per-window 指针 `g_xwidgetForceFullFrameFor`（:5898，实读），:5927 赋值为目标顶层，:9025 消费时校验 `== top`，:9027 消费后置空（实读）。

### 1.2 输入逆变换

- X11 指针/XDND：设备坐标 → 逻辑坐标 R⁻¹（上文 :2775/:2816/:2853）；
- fbdev 触摸/指针：同上（`XPlatformFbInput_posix.c:312-319/:538-549` 实读）；
- 控件坐标链：`XWidget.c` mapToGlobal/mapFromGlobal 偏移链按窗口旋转角换算（include 注 :80；`xwidget_windowRotation` :5761；:5784 / :5818 / :5859 / :5872 四处使用，实读）；跨顶层单点改道三处为材料 summary 表述。

### 1.3 API

**XWindow 层（Src/XGui/Window/，实读）**：
- `XWindow_setContentRotation`（XWindow.h:668）：只落请求角，角度变化时发 `contentRotationChanged`（:1418 信号访问器），不发同步重绘指令（:659 注释）；
- `XWindow_contentRotationDegrees`（XWindow.h:678）：有效角**纯算、无回写**——实现 `XWindow.c:1299-1303`，末行 `return XWindow_rotationSupported(self) ? req : 0;`（实读）；
- `XWindow_rotationSupported`：声明 XWindow_Protected.h:155，实现 XWindow.c:1282-1292（实读）；dpr>1 恒不支持（:1288 实读），条件回落后自动恢复（注释明言）；
- 存储字段 `m_contentRotation` 为 **int16_t**（XWindow.c:130，实读；注释言「本字段只存请求值、无回写」）。实现材料自检记录：评审稿原写 uint8_t，270 装不下会被截成 14 且静默失效，靠临时 fprintf（req=14）定位后改为 int16_t，调试打印已全部移除——撰写轮实跑 `grep -rn "ROTDBG\|ORIENTDBG" Src/ Test/ Drive/` **无命中**，确认无残留。

**平台能力查询（评审要求的两个查询 API，实读确认四平台齐全）**：
- `XPlatformNativeWindow_rotationSupported`：声明 `Src/XGui/Platform/XPlatformNativeWindow.h:370`；posix 真判据实现 :6763；win32 :4169 / android :1254 / unsupported :228 恒 false 并注明日期（实读 grep 命中）；
- posix 判据 `xpwn_canRotateDirect`（:6740，实读）：与 `xpwn_preparePresentImage`（:6774）直拷判据同源但**不依赖已准备的描述符**，未建连按乐观 true；`#if XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16` 分支恒 false（:6745-6747 实读）；注释含「2026-10-18 新增」；
- fbdev 走 `XPlatformBackingStore_rotationCapable`（XPlatformBackingStore.h:427；实现 XPlatformBackingStore.c:470-476，按全局计数 `g_xpbsNativeBufferUsers == 0` 判定，计数器 :461、增减 :463-467，实读）；
- `m_presentDirect` 为 XWNPendingEntry 私有字段（:274，初值 :597，运行期唯一赋值 :6861，实读）——查询 API 正是为绕开「拿不到私有运行期状态」而设。

**XWidget 层（实读）**：`XWidget_setContentRotation`（XWidget.h:1198）、`XWidget_contentRotationDegrees`（XWidget.h:1207）；实现 XWidget.c:5900（转调 XWindow）、:5937（转调有效角 getter）。

### 1.4 demo 接线（Test/XGuiDemo/xgui_demo_page_window.c，实读）

- **平台探测/置灰**：:434-435 试探性 `XWidget_setContentRotation(top, 90)` 记录 `m_rotationSupported` 后回设原角；:437 不支持时置灰方向下拉；应用路径失败时返回 0，槽据此给说明文案（:478-486）；
- **应用路径**：:478 `XWidget_setContentRotation`（失败即整体跳过）→ :486 `XWidget_setGeometry`（宽高按换轴对调并按主屏可用几何钳位）→ :496 同角二次置闩；:470-477 注释以「平台层 footprint 对调读执行时刻旋转角」论证先旋转后几何的顺序（该论证属于评审保留项，见第四节 4.5）；
- **页脚标签**：提示/状态两标签并入根布局器统一排布（:786-789 `addStretch(1)` + `addWidget(m_hintLabel)` + `addSpacing(6)` + `addWidget(m_statusLabel)`，实读）；`demo_page_window_settings_adapt` 改为空实现（:802-806，注明「页内排布全部交给根布局器……不再做任何手写几何」，实读）；
- **autotest 断言**：`demo_page_window_settings_autotest` 自 :812 起（实读）；方向四方位段（约 :891-908，实读）对 90/180/270 逐档断言 `XWidget_contentRotationDegrees(top) == deg`，180° 断言宽>高、90/270 断言高>宽（不断言精确对调值，注释言按主屏可用几何钳位），回 0° 断言有效角归零且基准宽高精确还原。材料自检记录该四条实测 PASS。

### 1.5 fbdev 落地状态（如实）

fbdev 旋转分支（自持 footprint 命中遍历、局部逆变换、逐像素旋转落笔、换角帧账本按首帧处置）**只做了静态编写**：`XPLATFORM_FBDEV_ON=1` 配置在实现轮**未编译**、真机**未验证**（材料自检原文声明）。撰写轮同样未编译 fbdev，仅实读确认上述符号在树且行号相符。「fbdev 有可编译实现」目前纸面成立、未经实测。

### 1.6 门脚本（.zcode/wf-gates/wf_orient_gate.sh，实读）

流程（脚本头注释 :10-19）：等 `pgrep -x make` 为空 → `flock /tmp/xinYueC_orient.lock`（超时退 75）→ build-direct 构建 `XGuiWindowDemo_Test` → Xvfb :96 自启跑 `--autotest` 与 `--apitest=containers` → 失败行与基线做 `comm -23` 「新失败 ⊆ 基线失败」判定。实现轮自检记录并修复了脚本自身两个缺陷：① BIN 路径写错导致 rc=127 却判 PASS——已改双路径探测 + 退出码 ≥126 一律退 1（实读脚本 ~:110-139 确认在位）；② 后台 Xvfb 继承 flock 的 fd 9 造成跨进程死锁——已加 `9>&-`（实读脚本 ~:92 确认在位）。

---

## 二、验证

### 2.1 构建门（orient 门）

最终轮 stdout 已落盘 `/tmp/orient_gate_final.out`，末行 **`ORIENT_GATE_PASS`**（撰写轮实读）。分片证据（/tmp/wf_orient_gate_final.log_*，撰写轮实读）：
- `_build.log` 末行 `[100%] Built target XGuiWindowDemo_Test`（构建通过）；
- `_newfail.log` **0 字节**（wc -c 实测）；
- `_autotest.log` 末尾正常退出（「已退出」，无 FAIL 增项）；
- `_apitest_containers.log` 末行「总计失败=25 HAS FAILURES」——该 rc=1 是基线既有状态，门按「新失败⊆基线失败」判通过（判据见脚本 :145-153 区段，实读）。

**门表述澄清**（沿审计结论 + 撰写轮实读订正）：本轮可验证的门就是 orient 门（内含构建+autotest+apitest+新失败差集闭环）；.zcode/wf-gates 下不存在本轮独立的 wf_build_gate 轮次日志。审计称「最终轮 ORIENT_GATE_PASS 行未落盘（只有分片）」——撰写轮实读发现该行实际落盘于 `/tmp/orient_gate_final.out`，以实读为准。另：`orient_gate_final*` 前缀的分片现位于 /tmp（文件名 `wf_orient_gate_final.log_*`），.zcode/wf-gates 目录下无此前缀文件（ls 实读）；更早轮次分片 `/tmp/wf_orient_gate1.log_*` 亦在盘。材料称门共运行四轮、逐轮 PASS，撰写轮未重跑门脚本（避免与共享构建树/锁冲突），以落盘证据为准。

### 2.2 autotest/apitest 基线对比

判定方式 = 当前失败行集合与基线失败行集合做 `comm -23` 差集。撰写轮实跑重算：

```
comm -23 <(sort -u /tmp/wf_orient_gate_final.log_autotest.log.fails) \
         <(sort -u .zcode/wf-gates/autotest_baseline_1018.log.fails) | wc -l   → 0
comm -23 <(sort -u /tmp/wf_orient_gate_final.log_apitest_containers.log.fails) \
         <(sort -u .zcode/wf-gates/apitest_containers_baseline_1018.log.fails) | wc -l   → 0
```

行数实读（wc -l）：autotest 基线 .fails **10 行**、本轮 **9 行**；apitest 基线 **26 行**、本轮 **25 行**。即 0 条新失败，且本轮失败比基线各少 1 条（部分基线失败未复现）。材料自检「基线 10 条全在」与实数略有出入：准确表述是「本轮失败 ⊆ 基线失败（差集 0），本轮 autotest 9 条 / apitest 25 条，基线 10 条 / 26 条」。

新增的 demo 旋转断言（第一节 1.4）随 `--autotest` 执行，材料自检记录 90/180/270 三档 + 回 0° 共四条实测 PASS；窗口设置页 27 条全 PASS（材料）。撰写轮未单独重跑 autotest。

### 2.3 Xvfb 目验逐项与证据

环境（材料记录，撰写轮未能复验进程——已按 PID 清理）：目验用 Xvfb **:97**（`Xvfb :97 -screen 0 1280x900x24`，PID 338568），因 :91 被另一 lane 占用（PID 314220）；验证完按 PID 收掉，`ls /tmp/.X11-unix/` 确认其它 lane 的 Xvfb 与 app 未被波及；app PID 338824。证据文件在盘（撰写轮 ls 实读 mtime 18:30-18:32）：`/tmp/orient/R3_base.png`、`R3_90.png`、`R3_after90.png`、`R3_270.png`、`R3_after270.png`、`R3_180.png`、`R3_after180.png`、`s_base.png`、`s_90.png`、`r3_main.py`、`r3_main.out`、`seto.py`。

判定脚本输出 `/tmp/orient/r3_main.out` 为撰写轮**全文实读**，逐项如下：

1. **上轮失败项（90°/270°→0° 页脚标签丢失）已修复**：0° 基线提示标签墨行 12 行、状态标签墨行 15 行；90°→0°、270°→0°、180°→0° 三条回程后均同为 12 行 / 15 行，提示区 diff=0.00，品牌条行区 (30,67,38) 与基线一致（r3_main.out 实读）。状态区 diff=28.46 属预期文案变化——基线状态标签「就绪」，回程后为「已切换： 横屏（0°）(1000x700)」（材料裁图 s_base.png / s_90.png，裁自行 640-666，标签均在原位）。
2. **回 0° 剖面三路径一致**：90→0、270→0、180→0 对 0° 基线均为 MAE=0.4481、exact=95.71%、±20 内 98.71%、全帧遮罩后 0.298（r3_main.out 实读，三段数字逐位相同）。
3. **真旋转判定**：180° 帧对「真旋转 (x,y)→(W-1-x,H-1-y)」MAE=0.5415，对「上下镜像」MAE=15.5481，r3_main.out 原文「2D: 对真旋转 MAE=0.5415 | 对上下镜像 MAE=15.5481 => 真旋转✓」（实读；排除镜像误判）。
4. **方向与几何**：0° 品牌条 rows (30,67)；90° 品牌竖条 cols (932,969) 右缘（顺时针）；270° cols (30,67) 左缘（逆时针）；180° rows (632,669) 底部。四态抓帧均 1000x700（r3_main.out 实读；材料称与 `xwininfo` 的 1000x700 一致）。
5. **切换路径**：r3_main.out 记录 goto90 / goto270 / goto180 及各自回程均 `try0 ok=True`（下拉一次点开）。**订正**（沿审计）：0° 基线为直接抓帧，无下拉记录，「四方位均一次点开成功」应表述为「三个非 0 方位」；「四条切换往返」实为**三条**（0→90→0、0→270→0、0→180→0）。
6. **无崩溃**：r3_main.out 末两行「⑥崩溃标记: 0」「存活: 338824」（实读）；材料另记 `grep -a -icE 'segmentation fault|assert|abort|core dump|Fatal' /tmp/orient_ui.log` → 0。
7. **修复落点**：页脚两标签并入根布局器（xgui_demo_page_window.c:786-789）与 adapt 空实现（:802-806），撰写轮实读确认与目验材料所述一致。
8. **二进制时效**（沿审计订正）：`ls -la` 实读——源 Test/XGuiDemo/xgui_demo_page_window.c mtime **18:18**，bin/XGuiWindowDemo_Test mtime **18:30**（18:18 后重链一次；材料称 18:34 门禁复验时 make 判 up-to-date 未再触碰）。结论「含本轮修复」成立（18:30 晚于最后源码修改）。
9. **墨行计数的括号说明**（沿审计订正）：「提示标签墨行 12 行（622-633）」「状态标签墨行 15 行（647-654）」中括号内 647-654 是 r3_main.py 打印的**前 8 行样本**（`[:8]`），非全部 15 行的连续区间；r3_main.out 原文样式即 `[647, 648, …, 654] (15 行)`（实读）。

**未做**：像素级逐位比对、可视环境下的人眼目视（含 180° 倒置观感）——材料与审计一致声明未做，撰写轮亦未做。

---

## 三、已知限制

### 3.1 fbdev 硬件加速 / 构建覆盖

- fbdev 旋转分支未经编译与真机验证（见 1.5）。零拷贝模式（缓冲落在 fb 映射内）按设计 `rotationSupported=false`，经 `XPlatformBackingStore_rotationCapable` 的 `g_xpbsNativeBufferUsers==0` 判据挡住（XPlatformBackingStore.c:470-476 实读），未实测；
- RGB16(565) 构建（XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1）未编译；该配置下 `xpwn_canRotateDirect` 恒 false（posix :6745-6747 实读）；
- Win32 / Android / unsupported 三后端 `rotationSupported` 恒 false 桩（:4169 / :1254 / :228 实读），三后端未编译；demo 据返回 false 置灰（1.4），此路径在 X11 下由探测代码覆盖、其余平台仅静态。

### 3.2 光标 / 触摸细节

- 「画面真的转了、点击位置对准」在无头 Xvfb 下仅由两类**间接**证据覆盖：autotest 的有效角逐档命中断言，与目验的 2D 真旋转/方向剖面判定；无像素级比对，无真机目视；
- 输入逆变换的正确性无独立的自动化断言（autotest 断的是有效角与几何，不是命中点映射）；fbdev 输入逆变换（XPlatformFbInput_posix.c:312-319/:538-549）连编译都未过；
- X11 侧评审保留项【中】仍未裁定解决（见 4.4）：`XWidget_replayPressAtGlobal`（XWidget.c:3429 实读）与远程服务命中（Src/XGui/Remote/XGuiServer.c:882 实读）仍经 `XGuiApplication_topLevelAt` 以逻辑矩形做 `XRect_contains`——若全局坐标口径在旋转后含设备语义，这两处需要同步换算；本轮材料无解决记录。

### 3.3 性能

- 本轮**未做任何性能基准**（帧率、切换延迟、逐像素旋转拷贝代价均无数据）；
- 设计稿撤回「零开销」时「getter 两跳比 xpwn_dprForGeometry(:1361) 更轻」的量级论证不成立（评审保留【低】，审计核实 :1361 引用无误、该结论仍成立）：现 getter 链为 `xpwn_findByXWindow` 线性查找 + 判据计算；它确是纯读、无副作用；
- X11 present 旋转拷贝（含四步裁剪、180° 反序快路径）与 fbdev 逐像素落笔的实测开销未知。

### 3.4 查无出处 / 未复核项（沿审计如实登记）

- 「上轮 90→0/270→0 为 MAE=3.8837、exact=65.43%」的历史出处，审计在 /tmp/orient 现存 .out/.py 中检索不到，撰写轮同样未检索到；仅作历史背景，不构成本轮证据；
- 评审保留【低】「demoWiring 第 0 处修正的推理错误」属设计文档层面评判，设计文档不在本轮材料中，保留项**未裁定**；
- 目验环境项（:91 占用 PID、Xvfb :97 PID、清理后 X sockets 清单）为目验材料记录，撰写轮无法复验已被清理的进程状态。

---

## 四、复核修正

对评审保留项的逐条裁定（结合实读证据更新至当前树）：

### 4.1 评审【高】1「XWindow_rotationSupported 拿不到输入、首帧静默降级」→ **已按评审要求补齐，不再是未解决问题**

两个平台查询 API 已存在且四平台实现齐全（实读）：`XPlatformNativeWindow_rotationSupported`（XPlatformNativeWindow.h:370；posix :6763 / win32 :4169 / android :1254 / unsupported :228）；posix 判据 `xpwn_canRotateDirect`（:6740）不依赖已准备的描述符、未建连乐观 true；fbdev 走 `XPlatformBackingStore_rotationCapable` + 全局计数。原【高】所述「把运行期初始化状态当纯函数查询」的前提已被消除——判据不读 `m_presentDirect`（:274/:597/:6861）与 `m_nativeBufferMode`（:1162/:1193/:1200）的运行期落盘值。

### 4.2 评审【高】2「g_xwidgetForceFullFrame 文件级单值被别的窗口消费」→ **已解决**

现码为 per-window 指针 `g_xwidgetForceFullFrameFor`（XWidget.c:5898），:5927 赋值目标 top，:9025 消费时校验 `== top`，:9027 置空（实读）——正是评审要求的 per-window + 身份校验。保留原文会误导读者以为多窗口消费缺陷仍在。

### 4.3 评审【高】3「整帧闩解决的是不存在的问题（margins=0 下脏区本已覆盖条带）」→ **保留成立，行号订正**

内容仍对，引用按当前树订正（撰写轮实读）：CSD 条带保留量走 `XWindowDecoration_marginsFor`（XWindowDecoration.c:**1720**，非评审稿 1699-1705）→ `XWindow_setFrameMargins`（XWindow.c:**1709**，非 :1631）→ `m_frameMargins`（:106-108）；`updateContentsRect` 扣的是另一套 `m_contentsMargins`，二者不互通。`XWidget_addDirtyRegion` 将入队矩形与 contentsRect 求交后才入脏区（XWidget.c:**1285**，实读 `XRect_intersected(..., &contents)`）；`XWidget_rect` 返回 `m_contentsRect`（**4566-4572**，非 4562-4568）。今天顶层 margins 为零故无碍；若将来把 frameMargins 接进 contentsMargins，该问题重新成立——届时整帧闩与脏区裁剪两条机制须成对说明。

### 4.4 评审【中】两条 → **保留**

- D5-D 只改 fbdev 命中、漏 X11 同函数：`XGuiApplication_topLevelAt` 三个调用方中 XWidget.c:3429（重放按压）与 XGuiServer.c:882（远程命中）仍以逻辑矩形比对（实读行号，与评审一致）；fbdev 自持遍历只修了一半。未解决，见 3.2；
- R4b 的 `XWidget_updateRect(top, &top->m_windowRect)` 矩形语义与脏区求交裁剪须成对说明（同 :1285 实读）：今天 margins=0 无碍，margins 非零后闩是唯一出路、传什么都会被裁。

### 4.5 评审【低】两条

- demoWiring「第 0 处修正」推理质疑：**未裁定**（设计文档不在材料中）。代码事实（实读）：demo :478 setContentRotation 在 :486 setGeometry **之前**，:496 同角二次置闩；:470-477 注释以「footprint 对调读执行时刻旋转角」论证该顺序必要。注意：审计文字称「先 setGeometry 后 setContentRotation」，与实读代码顺序相反，以实读为准；
- R22 量级论证：结论仍成立（见 3.3）。

### 4.6 评审「未运行：本轮未执行任何构建或测试」→ **已过时**

orient 门（含 build → autotest → apitest → 新失败差集闭环，即评审要求的「先加 flock → build gate → autotest」）已运行至少四轮且通过：/tmp 下存有 wf_orient_gate1.log_* 与 wf_orient_gate_final.log_* 两套分片，最终轮 /tmp/orient_gate_final.out 末行 ORIENT_GATE_PASS（撰写轮实读）；demo autotest 段（xgui_demo_page_window.c:812 起，实读）随门执行。该「未运行」结论应删除，以第二节证据替代。

### 4.7 目验材料措辞订正（已在 2.3 落实）

「四条切换往返」→ 实为三条（90/270/180 回程；0° 为直接抓帧）；「四方位下拉一次点开」→ 三个非 0 方位；墨行括号区间为打印样本前 8 行起止，非总行区间；二进制 mtime 18:30 / 源 18:18 / 门禁 18:34 复验 up-to-date。

### 4.8 对审计材料的两处订正（撰写轮实读）

- 「ORIENT_GATE_PASS 行未落盘（只有分片）」——实际落盘于 /tmp/orient_gate_final.out 末行；
- 门分片实际文件名前缀为 /tmp/wf_orient_gate_final.log_*（.zcode/wf-gates 下无 orient_gate_final* 文件）；审计在 .zcode/wf-gates 与 /tmp 找不到「独立 build 门」的结论不受影响。

---

*报告撰写：方向报告撰写员，2026-10-18。所有「实读/实跑」条目均在撰写轮执行；其余标注为 lane 材料转述。未执行的操作（门脚本重跑、fbdev/RGB16/Windows/Android 编译、真机目视、性能基准、像素级比对）已如实列为未做。*
