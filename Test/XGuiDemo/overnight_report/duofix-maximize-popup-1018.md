# 双缺陷修复交付报告：B 弹窗随转 + A 最大化跟随延迟（1018 夜间）

- 日期：2026-10-18/19 夜间；报告落盘 2026-10-19
- 分支：`codex/xdevice-file-platform`（XinYueC 工作区）
- 被测产物：`bin/XGuiWindowDemo_Test`（10月19日 01:45 构建，21874424 字节）
- 结论速览：
  - **B 弹窗随转**：已落地并验证。继承/footprint/present/输入四件套全通（门 + 探针 + 目验B 对应项），坏态序列 B 弹窗 footprint 170x82 → 82x170、有效旋转角 0 → 270。
  - **A 最大化跟随延迟**：已根修并验证。逐帧差分两次 resize 各单帧收敛（原迟滞 0.25~1.25s 归零），目验A 五项全过。
  - **最终门 = true**（B/A 两泳道 gateReady 均 true）：autotest rc=0；apitest rc=1 与 qtalign_1018 基线一致、25 个既有失败逐行同基线、无新失败。
  - **目验A passed=true；目验B passed=false**——未过项仅「命中区错位」一条，属遗留独立缺口，本轮实现侧无任何源码改动（见三.4 与四.1）。
- 证据等级标注（贯穿第三节，按审计口径）：
  - **〔证实〕**＝复核员独立证实的事实（日志值、PID、文件存在性、崩溃零命中、二进制同源性）；标 **〔落盘复核〕** 者为本报告撰写时实测复跑。
  - **〔目验实测〕**＝目验员会话内实测、复核员未复现的像素级断言（frame 区间、cmp IDENTICAL、pamsumm=0、xwininfo 几何、弹层蓝高亮列）。
  - **〔泳道自证〕**＝实现泳道会话内自测、复核员未复现（探针、帧差分等实现侧验证）。

---

## 一、B 弹窗随转（继承/footprint/present/输入四件套落地）

### 1.1 缺陷与修法
坏态序列 B：0° 下首开弹窗建柄 → 收起 → 主窗转 270° → 重开同一弹窗。弹层柄一次建、隐藏不销毁，重开不再经过 `XWidget_createWindow→setTransientParent`，owner 收起期间换角后无人补继承，弹窗按旧角（恒首开时刻值）显影。

方案 B 只做两处最小改动恢复弹窗的**有效旋转角**；旋转角一旦恢复，footprint / present / 输入三链按既有「每帧按窗现查」机制自动随转，无需各自改动。

### 1.2 改动一：公共层 show 路径旋转重继承（Src/XGui/Window/XWindow.c）
- 抽私有函数 `xwin_inheritTransientRotation`（XWindow.c:1541-1562；前置注释块 1532-1540）：原 `setTransientParent` 内「继承 + 已建柄重跑 resize 块」逻辑抽出复用。
- `XWindow_setTransientParent`（函数体 1564-1596）改调该函数（调用点 1586），并把「以重开生效」注释同步改写为与事实一致（1576-1585）。
- `XWindow_setVisible` show 路径注入旋转重继承（XWindow.c:2278-2289）：仅 Popup/ToolTip 且 `m_transientParent` 非空时补跑；首开路径 `setTransientParent` 已在建柄前继承过同一角，角等值短路，零扰动。
- 覆盖面：注入点为公共 show 路径，XMenu/ToolTip/XDateTimeEdit/虚拟键盘等 Popup 转换机制同源覆盖（未逐一探针，见四.3）。
- 行号〔落盘复核〕：以上五处均已按当前工作区 `grep`/`sed` 核实（1541/1564/1586/2289 命中，2278-2289 注释与注入块原文在位）。

### 1.3 改动二：posix setGeometry 去重加「已应用旋转角」维度（Drive/Posix/Graphics/XPlatformNativeWindow_posix.c）
- `XWNPendingEntry` 增 `m_appliedRotation`（声明区间 :310-320）：「逻辑几何不变而角变」（弹层锚点不变的重开场景）若仍按纯逻辑去重会短路掉 `applyRotationFootprint` 的 footprint 重对调，弹窗停在旧角宽高。
- `setGeometry` 去重判定加第二维（:5733-5746，比对逻辑 5741-5746：四字段相等**且角相等**才短路）；落地/挂起记账同步记录本次实际采用角（:5761-5764）。
- create 记账初始化（:5374）；entry 释放归零，防上一窗口已应用角经槽位复用污染下一窗口的首轮去重比对（:5670-5672）。
- 行号〔落盘复核〕：以上四处均已 `sed` 核实原文在位（含 `[弹窗随转·方案B 2026-10-18]` 标记注释）。

### 1.4 修复后四件套实测〔泳道自证〕（Xvfb :99 800x480x24，链含修复重建的 build/libXinYueCd.so.0.1）
- **继承**：坏态序列 B 重开后探针直读 `XWidget_contentRotationDegrees(弹窗)=270`（坏态=0），输出 `seqB: reopen after rotate win=0x200004 geom=82x170+90+379 or=1 map=2`、`seqB popup effective rot = 270 (top=270)`；序列 A（先转后首开）保持 82x170 无回归。
- **footprint**：弹窗 X 窗 170x82 → 82x170（override_redirect=1）；锚位 X 窗 +90+379 与逻辑几何原点逐位一致——原点不随旋转变，不飘不移。
- **输入**：xdotool 真实点击弹窗两点分别选中 idx=0 与 idx=3（CLICK1 (105,419)→idx=0、选中收起 hidden=1、重开 CLICK2 (155,419)→idx=3，VERDICT clickable=yes distinct=yes），两点逻辑落点 (129,15)/(129,65) 与 `XWindowRotation.h:88` UnmapPoint 270° 公式 `(nx=W-1-y, ny=x)` 逐位一致；选中即收起、可重开。
- **present**：ffmpeg 抓帧对比——shot0.png（0°）弹窗 170x82 四条目横排正置可读；shot270b.png（270°，82x170）逻辑行带 y∈[8,24) 显示为设备左缘竖条 dx∈[8,24)，dx=y 正变换逐位对应。弹窗文字未入可见区系文字带 dy∈[124,159] 落在 480 高屏幕裁剪区之外，非 present 缺陷。

---

## 二、A 最大化跟随延迟根修

### 2.1 根因
平台 ConfigureNotify 路径的 `XWidget_applyWindowGeometry` 只写矩形+派发事件、从不激活布局也零失效——布局激活寄生于 HUD 悬浮层 setText→updateGeometry 偶发链（HUD 空闲 1s + 统计窗 250ms ⇒ 迟滞 0.25~1.25s；HUD 裁剪/冻结时更久，10-18 DDE 真机实测 1~2s）。外部 resize/最大化/复原后根布局不激活，首个全窗 PAINT 画「旧子几何+新窗尺寸」，修正帧只能等 HUD 偶发激活。

### 2.2 主修：平台路径补齐（Src/XGui/Widget/XWidget.c）
- `XWidget_applyWindowGeometry`（:9989-10069）RESIZE 派发前补 `#if XLAYOUT_ON` 布局激活（:10035-10051，`if (self->m_layout) XLayout_activate(self->m_layout)`，镜像程序化路径 recomputeGeometry :4724-4733，对标 QWidget::event(Resize) 先 activate 再派发）。
- 函数尾补仅 `sizeChanged && m_visible` 的顶层 old∪new 失效（:10056-10068，`XRect_united(&old, &m_windowRect)`→`XWidget_updateRect`，镜像 :4753-4762 顶层分支；move-only 不失效，保持拖移免重绘直提优化；隐藏顶层几何变化不产生脏区，与镜像口径一致）。
- 生效路径：平台 ConfigureNotify→handleGeometryChange→RESIZE→`VXWidgetWindow_event` 唯一实调点（XWidget.c:3290）全路径生效。主修使平台路径与已修的程序化路径（recomputeGeometry）对称：布局激活先于 RESIZE 派发 + 顶层 old∪new 失效，激活链由 HUD 寄生变为几何同步链自足。
- 行号〔落盘复核〕：9989/10035-10051/10056-10068/3290 均已核实原文在位（含 `[平台路径布局激活 2026-10-18]`、`[顶层跟随失效 2026-10-18]` 标记注释）。

### 2.3 demo 兜底与编译健全性（Test/XGuiDemo/xgui_window_demo.c）
- `VDemoWin_resizeEvent` 开头（`m_staticSceneDirty` 置位后）补 `XWidget_updateGeometry(self)`（`#if XWIDGET_ON && XLAYOUT_ON` 护栏），demo 导航面板/内容区读到本帧激活几何，不靠帧泵晚一拍。
- :3789-3794（赋值 3793）`self->m_overlayPinned=false` 补 `#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON` 护栏（成员声明 :462 同宏域），修 overlay=0 裁剪构建编不过（`m_overlayPinned` 未声明）；另两处使用（:834/:2713）核实已在宏域内无需动。
- 行号〔落盘复核〕：3789-3794 已核实（含 `[编译健全性 2026-10-18]` 注释、赋值在 3793）。

### 2.4 次级防线：挂起防滞留（Drive/Posix/Graphics/XPlatformNativeWindow_posix.c）
- `XWNPendingEntry` 增 `m_pendingGeomStampMs`（挂起几何记账时刻，单调毫秒）；新增 `xpwn_monotonicMs` + `xpwn_applyPendingGeometry` 前置声明（ConfigureNotify 段在定义之前，修复首轮编译暴露的静态声明次序错误）。
- setGeometry 挂起记账处刷新时间戳；ConfigureNotify 入口滞留检查：`m_hasPendingGeom` 且距记账 >100ms 强制 `xpwn_applyPendingGeometry` 批内落地——只落地挂起值，不解 `m_deferGeometry` 臂（臂生死归装饰手势层）；entry 释放归零同步。

### 2.5 fixPlan 3 处置：如实放弃
「XWindow_setWindowStates 前置 xwd_endInteractions」经核实不可直接落地：XWindowDecoration.h 全部导出无强制解臂接口、XWindow.c 零引用装饰层，XWindow 低层不可回调高层（XWindow.c:1321 层级不倒置原则）。防线改为既有兜底（装饰层新按压清残留 :1905-1925、toggleMaximize endInteractions :1577）+ 平台侧滞留超时落地（2.4）。

---

## 三、验证（门 + 目验逐项 + 证据）

### 3.1 门（最终门=true）
- **判据〔证实〕**：newfail 空 + 构建成功 + 差集独立复算 0（B/A 两泳道 gateReady 均 true）。注：**门 stdout 与测试二进制退出码未存档**，证据为拆分日志文件组 `/tmp/wf_duofix_final.log_{autotest,apitest_containers,build,newfail}.log`（均为 02:20）。
  〔落盘复核〕四件日志均在、mtime 02:20；`newfail` 文件 **0 字节（空）**；`build` 日志尾部 `[100%] Built target XGuiWindowDemo_Test`。
- **门规格**（.zcode/wf-gates/wf_duofix_gate.sh，〔落盘复核〕脚本在位，5912 字节）：等 make 空闲 → `flock /tmp/xinYueC_duofix.lock` 包住构建+测试段 → build-direct `make -j8 XGuiWindowDemo_Test` → Xvfb :91 未起自启（关 fd 9 防 Xvfb 继承锁描述符）→ DISPLAY=:91 跑 `--autotest` 与 `--apitest=containers` → 失败行 `grep -a -E 'FAIL|失败' | sort -u` 与 qtalign_1018 基线做「新失败⊆基线」，过则输出 DUOFIX_GATE_PASS exit 0。
- **结果**：autotest rc=0；apitest rc=1 与基线采集时一致——25 个既有失败逐行同基线，**无新失败**；DUOFIX_GATE_PASS exit 0。
- **口径规范化（首轮误报复盘）**：比对前对汇总行计数做规范化（失败=N/失败=25→失败=#）并剔除「全部通过」成功行。首轮 run1 报 3 行「新失败」，逐一核对为：基线冻结时占位符「失败=N」与现役实数「失败=25」的措辞漂移、成功行误捕（对话框页「全部通过 失败=0」）——属上游格式演进，非真实失败；规范化后两套件逐族/逐用例失败行与基线 `comm -12` 完全一致。
- **编译**〔证实〕：`while pgrep -x make; do sleep 15; done` + `flock /tmp/xinYueC_duofix.lock bash -c 'cd build-direct && make -j8 XGuiWindowDemo_Test'` → `[100%] Built target XGuiWindowDemo_Test`（尾部仅 :5476 既有 XIM 警告，非本次引入）。A 泳道首轮曾暴露 `xpwn_applyPendingGeometry` 静态声明次序错误，补前置声明后过。另重建 build/ 目标 XinYueC_Dynamic 供 B 探针链接。

### 3.2 A 泳道验证
- **demo 逐帧差分**〔泳道自证〕：Xvfb :99 1280x900 + xdotool 900x600→1280x760→900x600 + ffmpeg x11grab 10fps 共 111 帧（/tmp/verify_follow.sh，T_RESIZE_UP/DOWN 时间戳入脚本日志）；numpy 帧差（>24 灰度阈、>1200 ds-px 事件阈）event frames 仅 (10, 28223) 与 (60, 28222)，gaps=[50]——间隔恰 50 帧=5.0s 与操作节奏逐位吻合；两次 resize 各**单帧收敛**、零寄生差分帧；修正帧落在首帧全窗 PAINT、当帧收敛（≤100ms 采样分辨率），HUD 第二拍消失。
- **270° 协同探针**〔泳道自证〕：外部 xdotool resize 后 top 逻辑 700x500、X 窗 footprint 500x700 对调、rot=270 保持；画面满幅内容无黑带、CSD 图标竖排旋转不互吞（rr_b_crop.png）。排障记录：裸探针无帧泵致画面不更新（B/C 帧字节全同 5200），补 `XWidget_repaint` 驱动后正确——demo 帧泵场景无此问题，非框架缺陷。
- **overlay=0 裁剪编译**〔泳道自证〕：从 build/compile_commands.json 提取 xgui_window_demo.c 全部编译旗标 + `-DXGUI_PERFORMANCE_OVERLAY_ON=0` + `-fsyntax-only` → 0 error（NOOVL_SYNTAX_PASS；修复前该宏域 `m_overlayPinned` 未声明编不过；现存 incompatible-pointer 警告为既有单继承指针惯用法，与本次无关）。
- **0° 直通**〔论证+全量门回归佐证，未做微观基准〕：主修仅在 sizeChanged（低频）路径新增一次幂等 activate（XLayout.c:1140-1144 早退）+ 一次 updateRect；非 resize 路径零新增。

### 3.3 目验A（passed=true，五项全过）
环境〔证实〕：被测二进制为本轮 01:45 新构建（晚于上轮 01:23）；自有 Xvfb :92 2400x1200x24（PID 454668，/tmp/xvfb92.pid）；demo PID 454674（DISPLAY=:92 过滤核定），窗口 0x200002 1000x700+40+40；三连点击进「窗口设置」页（最大化复选框 (228,252)、窗口方向 combo (375,322)、复位默认 (262,367)）。逐项像素断言为〔目验实测〕，复核员未复现。

1. **放大 1000x700→1980x1000**〔目验实测〕【通过】：frame1=[15,259]ms（盖 0.1s 刻度）、frame2=[262,500]（盖 0.3s）、frame3=[503,731]（骑 0.6s）、frame4=[734,972]（盖 1.0s）；frame1~3 与 1.2s 后终态帧在导航列全高裁剪(178x1000)与主区裁剪(1980x900，避开 FPS 悬浮层)均 `cmp` 逐字节 IDENTICAL——**跟随 ≤0.1s ≪ 0.6s 口径**；目验 frame1：导航列满高、控件新位、钉底行落新底、FPS 悬浮层重锚右下。
2. **复原 1980x1000→1000x700**〔目验实测〕【通过】：frame1=[8,237]ms 即与终态逐字节一致（同口径双裁剪，frame2~3 同）；窗外露出区 (abs 1100,300)+500x300 `pamsumm -mean`=0.000000 纯黑无残影；X11 几何复原 1000x700。
3. **「最大化显示」复选框路径**〔目验实测〕【通过，按 ask 预案口径】：点击 (228,252) 后 frame1=[106,335]ms 内呈勾选态，X11 几何保持 1000x700+40+40——本构建无 WM 下 showMaximized 无几何效果（源码口径：XWindow.c:1490-1493 原生窗挂接时仅发 EWMH ClientMessage、XPlatformNativeWindow_posix.c:4197 由 WM 执行）；按预案点「复位默认」（☐回落、状态栏「已复位默认」）+ 放大组合：frame1=[10,288]ms 即与终态逐字节一致。
4. **四方位切换后回 0° 再放大**〔目验实测〕【通过】：经「窗口方向」弹层逐档切换，90°/180°/270°/回 0° 均生效（状态栏文案：竖屏/倒置横屏/倒置竖屏/横屏，尺寸随档切换，180° 整页点对称倒置）；弹层开态伪影检查三朝向全干净（/tmp/k_pop90.ppm 弹层窗四列+蓝高亮列=当前值、kpop180m.png、kpop270m.png 无撕裂无副本）——上上轮 failure 三子项持续未复现；关弹层残迹差分 4155 像素全部集中于左上 FPS 悬浮层秒刷区（FPS 4.2→6.7 数字变化），无重影无灰条；回 0° 后放大 frame1=[7,267]ms 与终态逐字节一致、正置无旋转残迹。弹层项定位方法论：弹层为独立 X 窗 0x200009（90°=82x170+703+330、180°=170x82+749+403、270°=82x170+376+449），`xwd -id` 单抓窗内容 + python 逐行/列统计蓝高亮（当前值）位置校准项序后，**单条命令内连贯完成开层+点项**（弹层在工具调用间隔会自关）。
5. **无崩溃无残影**〔证实（日志/进程）+ 目验实测（像素）〕【通过】：demo 存活 08:06 完成全部项（`ps -p 454674`→`454674 08:06 Sl`）；/tmp/duoA.log `grep -inE "segv|crash|assert|abort|fatal"` 无命中（仅启动期 1 条既有 XClass.c:11 诊断行，早于全部交互）；放大态窗外两区 pamsumm -mean 均 0.000000；收尾按 PID 收讫 demo(454674)、自有 Xvfb(454668)，ps 双确认消失，未动业务代码。

### 3.4 目验B（passed=false：命中区错位一项未过，其余过）
环境〔证实〕：Xvfb :93 1280x900x24（真身 PID 456514，`/proc/456514/cmdline` 核对）；demo PID 456536（`./bin/XGuiWindowDemo_Test --page 19`，日志 "switch page=19 (窗口设置)"），主窗 0x200002 1000x700+40+40。

**关键客观事实〔证实〕**：被测二进制与上一轮**完全相同**（`ls -la bin/XGuiWindowDemo_Test` → 10月19日 01:45、21874424 字节，与上轮同 mtime 同 size，无新编译）。〔落盘复核〕A/B 修复面四个源文件 mtime 均早于该构建：XWindow.c 与 posix 00:26:15、XWidget.c 01:10:21、xgui_window_demo.c 10-18 22:14:29——**A/B 修复确已编入 01:45 产物**；01:45 后无 A/B 修复面源码改动（详见四.1）。

- **④ 0° 基线**〔目验实测〕【通过】：combo 弹窗 0x200009 = 170x82+330+376、IsViewable；D_popup_0deg.png 四条目文字正立、横屏(0°)高亮。
- **①附带·绘制**〔目验实测〕【维持修复】：270° combo 本体清晰绘制——D_270_row.png 竖条「倒置竖屏(270°)」+顶部▽（screen ≈[307,337]x[240,407]）；180° 同——D_180_row.png 倒置 combo+左端▽（screen ≈[542,707]x[360,390]）；帧面干净无残影。
- **①附带·命中区**【**未过·遗留独立缺口**，与上轮逐位一致〕〔目验实测〕：旋转态点击「看得见的 combo」仍不开层，命中带与绘制带错位约 40px——270°：可见位 (325,365)/(323,284) miss（日志「顶层未消费 press pos=(374,285)/(455,283)」），盲位 (364,364) 一击开层；180°：可见位 (624,375)/(560,372)/(690,378) 三点全 miss（pos=(415,364)/(479,367)/(349,361)），盲位 (650,415) 一击开层；miss 日志值与上轮逐位相同。**失败定论：命中区错位未修复，且本轮实现侧无任何源码改动（find -newermt 01:45 证实）；修复前提是先落码再编译，重编译无效果。**
- **① 弹窗随转 + footprint**〔目验实测〕【本轮修复项，过】：270° 盲位开层后 `xwininfo -id 0x200009` → Width: 82 / Height: 170 / Absolute +376+449 / IsViewable（宽<高）；D_popup_270.png 四条目随主窗 270° 旋转（文字自下而上、高亮列=倒置竖屏270° 当前项）。
- **② 270° 旋转弹窗内点选**〔目验实测〕【通过】：点最左列「横屏(0°)」行 (386,534) → 弹窗 IsUnMapped、主窗回横屏 1000x700 正立、combo 显示「横屏 (0°)」、状态栏「已切换：横屏 (0°) (1000x700)」（D_after_sel0.png）。
- **③ 180° 弹窗倒置 + 命中**〔目验实测〕【通过】：170x82+749+403 IsViewable（180° 不换轴正确）；D_popup_180.png 四条目全部倒置、蓝底高亮行=倒置横屏(180°) 当前项；点倒置弹窗底行（逻辑行1 横屏0°，(834,475)）→ 弹窗收起、回横屏正立（D_after180back.png）。
- **④' 回 0° 不回归**〔目验实测〕【通过】：历经 270°→0°→180°→0° 往返后重开弹窗 = 170x82+330+376 与基线同值；D_popup_0deg_return.png 与基线内容一致，点行1 正常收层。
- **⑤ 全程无崩溃**〔证实〕【通过】：demo 456536 存活至手动 kill（`Rl, etime=03:07`，R=运行态非僵尸）；/tmp/duoB.log 全文 37 行，`grep -inE "segv|crash|abort|assert|double free|X Error|BadMatch|BadWindow|BadValue"` 零命中；期间含 2 次换向、4 次开层、10+ 次探针点击；收尾核对 `/proc/456514/cmdline` 后按 PID kill Xvfb(456514)（X93 socket 清除）与 demo(456536)，未用 pkill -f，未改业务代码。

### 3.5 证据索引与存档状态
- 门日志：`/tmp/wf_duofix_final.log_{autotest,apitest_containers,build,newfail}.log`（02:20）。〔落盘复核〕四件均在，newfail 0 字节，build 尾 `[100%] Built target XGuiWindowDemo_Test`。门 stdout 与测试二进制退出码未存档（见 3.1 判据注）。
- A 目验：/tmp/duoA.log、/tmp/kup_times.txt+kup1~4 帧、/tmp/k_up_settled.ppm、/tmp/kdn_times.txt+k_dn_1.ppm、/tmp/kmx_times.txt+kmx1.png、/tmp/krst.png、/tmp/kcb_times.txt、/tmp/kr90_st.png、/tmp/kr180.png、/tmp/kr270.png、/tmp/kr0c.png、/tmp/k_pop90.ppm、/tmp/kpop180m.png、/tmp/kpop270m.png、/tmp/ks180_fps.png、/tmp/kc180_fps.png、/tmp/kr4_times.txt+kr4_1.png。
- B 目验：/tmp/duoB.log、/tmp/duoB_shots/（D_popup_0deg / D_270_row / D_180_row / D_popup_270 / D_after_sel0 / D_popup_180 / D_after180back / D_popup_0deg_return .png）。
- 泳道探针：/tmp/probe_rot（probe / probe_rr / probe_click、rr_b_crop.png、shot0.png、shot270b.png）、/tmp/verify_follow.sh。
- 〔落盘复核〕duoA.log、duoB.log、duoB_shots、probe_rot、verify_follow.sh 在位。上述均为验证会话临时产物，**未随报告归档**。
- 源码级证据以本报告所引 `文件:行号` 为准；关键行号已按当前工作区逐一落盘复核（见 1.2/1.3/2.2/2.3 标注）。

---

## 四、已知限制

1. **命中区错位（未修复，遗留独立缺口）**：旋转态 combo「看得见的位置」点击不开层、须盲点不可见区域，命中带与绘制带错位约 40px（270°/180° 各 miss 点坐标见 3.4）。本轮实现侧无任何源码改动（find -newermt 01:45 证实）；修复前提是先落码再编译，重编译无效果。〔落盘复核〕01:45 后工作区仅一处源文件有改动：`Test/XGuiDemo/xgui_demo_page_modbus.c`（mtime 01:45:50），属并行 modbus 泳道，不在 A/B 修复面；A/B 修复面四文件 mtime 均 ≤ 01:10:21，早于 01:45 构建。
2. **B·套件覆盖**：XGuiRegression 全量套件未跑（门规格只要求 autotest + apitest=containers，均已跑）。
3. **B·弹层覆盖**：XMenu/ToolTip/XDateTimeEdit/虚拟键盘弹层未逐一探针（机制同源，`XWindow_setVisible` 注入点为公共 show 路径，已覆盖其 Popup 转换）。
4. **B·角度覆盖**：90° 未单独探针（与 270° 同为对调角，同一代码路径）。
5. **B·语义边界**：真实鼠标点击链 focusWindow 为空场景（首开无 owner 则无继承）未修未展开，属既有独立缺口；打开态 owner 换角仍按「以重开生效」口径（`setTransientParent` 注释已改写为与事实一致，XWindow.c:1576-1585）。
6. **A·环境**：DDE 真机（WM 合成时序）回归未做——本环境无 DDE/WM，已验口径为 Xvfb 无 WM 直连。
7. **A·性能**：0° 直通未做微观性能基准（以论证 + 全量门回归佐证）。
8. **A·极端场景**：滞留防御的「卡死期无任何后续几何事件」无检查点（日常由 present 批内消费与 ConfigureNotify 滞留检查双口覆盖）。
9. **A·未落地项**：fixPlan 3 半条（`setWindowStates` 前置解臂）因层级倒置不可落地（见 2.5）。
10. **平台口径**：无 WM 下 showMaximized 仅发 EWMH ClientMessage、无几何效果（由 WM 执行；XWindow.c:1490-1493、XPlatformNativeWindow_posix.c:4197），已按预案以「复位默认+放大」组合验证。
11. **门基线口径**：qtalign_1018 基线含 25 个 apitest 既有失败，门口径为「新失败⊆基线」，既有失败不在本轮修复范围；基线汇总行措辞漂移（失败=N→失败=25）已在门内规范化（见 3.1）；门 stdout 与测试二进制退出码未存档。
12. **环境收尾**：探针 Xvfb :99 已清理；门 Xvfb :91 保留供门复用。
13. **证据归档**：第三节所引 /tmp 证据文件为验证会话临时产物，未随报告归档（在位情况见 3.5）。
