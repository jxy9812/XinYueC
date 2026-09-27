# night3-commit-plan.md — 夜三/四工作区「建议提交划分」（终测升级员，第四波 23:4x 刷新）

> **性质**：建议文档，非提交执行。本车道全程禁 git 写操作（status/diff/show 只读），
> 以下划分供终测后统一执行提交时参照。
>
> **快照时效警示**：本工作区是**活的工作区**。本文为 2026-09-26 23:3x–23:5x
> 快照（较 03:2x 旧版全量刷新：行数、hunk 头、exe 键串、**分支拓扑**均已变），
> 执行提交前必须重取快照核对。快照工具：`Tools\git.bat`（VS 自带 git 包装，
> 本机 PATH 无 git），全部命令带 `--no-optional-locks` 防索引写入。

## 0. 快照基线（status / diff --stat / exe 键串 / 分支拓扑）

### 0.1 `git --no-optional-locks status --porcelain`（快照 23:3x）

```
 M .gitignore
 M Drive/windows/Graphics/XGpuRenderDriver_gl.c
 M Drive/windows/Graphics/XPlatformNativeWindow_win32.c
 M Src/XGui/Charts/XChartView.c
 M Src/XGui/Charts/XChartView.h
 M Src/XGui/Graphics/XGpuRenderBackend.c
 M Src/XGui/Graphics/XGpuRenderBackend.h
 M Src/XGui/Graphics/XPainter.c
 M Src/XGui/Widget/XFileDialog.c            ← 03:2x 快照后新增（跨平台时钟）
 M Src/XGui/Widget/XWidget.c
 M Test/XGuiDemo/xgui_demo_page_views.c
 M Test/XGuiDemo/xgui_window_demo.c
?? Tools/bench-night4-smoketest.txt         ?? Tools/bench-pbo-lane-baseline.txt
?? Tools/capture_frames.ps1                 ?? Tools/diff_frames.ps1
?? Tools/git.bat                            ?? Tools/night3_bench.ps1
?? Tools/night_build_x64.bat                ?? Tools/tear_p4t20.png
?? docs/xgui/night3-attribution.md          ?? docs/xgui/night3-attribution2.md
?? docs/xgui/night3-baseline.md             ?? docs/xgui/night3-commit-plan.md
?? docs/xgui/night3-final.md                ?? docs/xgui/night3-interactive.md
?? docs/xgui/night3-wave2-final.md          ?? docs/xgui/night3-wave2-plan.md
```

（03:2x 旧快照中的 `bench-night3-idle-recheck.txt`、`bench-night3-rehearsal3*`、
`caps-night3-rehearsal/` 已不再现身——被 `.gitignore` 本次新增的
`Tools/bench-night3-*`、`Tools/caps-night3-rehearsal/` 段覆盖。）

**快照后增量（23:5x 复查 status 实测，工作区仍在被追写）**：撕裂归因
lane 在途落盘了一批新未跟踪文件——`Tools/tear_p4t20_{gpu_base,nofastpath,
nofullbatch,nokeepopen,noquadb,regionoff,rowsfull}.png`、
`Tools/tear_sw_p4t20.png`、`Tools/zoom_tear_*.png`、`Tools/tear_diff_map.ps1`、
`Tools/caps_n6_{0,4,ix}/`、`Tools/sweep/`、`docs/xgui/night3-sweep.md`——
外加本车道预演产物 `Tools/caps-night3-rehearsal4/`、
`Tools/tear-night3-rehearsal4/`（未被现有 gitignore 行覆盖）。这些归属
（夹具入库/产物 ignore）在 §5-4 一并裁决；12 个 `M` 文件集合与 §0.2 未变。

### 0.2 `git --no-optional-locks diff --stat`（快照 23:3x，vs HEAD）

```
 .gitignore                                         |  16 +
 Drive/windows/Graphics/XGpuRenderDriver_gl.c       | 821 +++++++++++++++++++--
 Drive/windows/Graphics/XPlatformNativeWindow_win32.c |  35 +-
 Src/XGui/Charts/XChartView.c                       | 123 ++-
 Src/XGui/Charts/XChartView.h                       |   5 +-
 Src/XGui/Graphics/XGpuRenderBackend.c              | 225 +++++-
 Src/XGui/Graphics/XGpuRenderBackend.h              |  21 +
 Src/XGui/Graphics/XPainter.c                       | 613 ++++++++++++++-
 Src/XGui/Widget/XFileDialog.c                      |  10 +-
 Src/XGui/Widget/XWidget.c                          | 128 ++--
 Test/XGuiDemo/xgui_demo_page_views.c               |  72 +-
 Test/XGuiDemo/xgui_window_demo.c                   | 128 +++-
 12 files changed, 1998 insertions(+), 199 deletions(-)
```

### 0.3 exe 状态（实测，23:03:46 构建——已非旧文档的 02:08 构建）

`bin\XGuiWindowDemo_Test.exe`（23:03:46）。二进制键串扫描
（PowerShell `ReadAllBytes`+ASCII `IndexOf` 逐键计数）：

- **已含**：`XGPU_DIRTY_READBACK`、`XGPU_PBO_READBACK`、`XGPU_SESSION_RETRY`、
  `XGPU_WS_COMPOSITED`、`XGPU_FASTPATH_EXT`、`XGUI_DEMO_IDLE_GATE`、
  `XGPU_FBO_PERSIST`、`XGPU_STATE_CACHE`、`XGPU_MAKECURRENT_ONCE`、
  `XGPU_PRESENT_LEAN`、`XGPU_PBO_STATS`、`XGPU_PRESENT_MAX_FPS`、
  `XGUI_CHART_STATIC_CACHE`、`XGUI_DEMO_VIEWS_SNAPSHOT`、
  `XGUI_FLUSH_FULLFALLBACK`——**L 道四键全部已编入**（fbo0 态为真对照）。
- **未含**：`XGPU_DISPATCH_MERGE`（dsp0 默认键名空操作=基线，终测须以
  `-KDispatchSwitch` 覆盖为在册键重跑或按 §4 抽测口径标注）、
  `XGUI_SETPARENT_RESET_GEOMETRY`（见 §3：用户落库的 setParent 实现未保留
  回退键）、`XGPU_RUNTIME_DEFAULT_ON`（本来就不在变更面）。

### 0.4 分支拓扑（本车道新发现，影响整个执行方案）

- 当前分支 `codex/xdevice-file-platform`，HEAD = `e728ca64`（19:26 用户质量
  收官提交）。
- **波 1 提交 `b1416e2a`（01:04「第三夜第一波」）不在 HEAD 祖先链上**：
  `merge-base --is-ancestor b1416e2a HEAD` 退出码 1；
  `merge-base b1416e2a HEAD` = `bf5757fb`；`branch -a --contains b1416e2a`
  **零输出**（任何分支都不含它）——它是一条悬空支线。
- **推论**：工作区 diff（vs HEAD=e728ca64）= 波 1+2+3+4/5 全部内容。
  旧文档按「波 1 已入库」把 A/C/D/E 组当已落库描述，对 HEAD 而言**不成立**
  ——这些组的内容此刻只存在于悬空的 `b1416e2a` 里，不提交就会丢。
- `git diff --stat b1416e2a`（对悬空支线的基线差）混入 e728ca64 自身内容
  （XFileDialog +1759 等 26 文件 +4387/−432），**不可**当作分组依据；分组
  一律以 §0.2 diff vs HEAD 为准。
- **执行方案二选一（须用户裁决，lane 不得代决）**：
  1. **按域重提交（推荐）**：以 §1 分组从工作区直接提交；`b1416e2a` 弃用
     不合并（其树已过时，内容被工作区超集覆盖）。历史引用该哈希的文档
     （night3-baseline.md 等）注明「内容改由后续分组提交落库」。
  2. 先 `merge`/`cherry-pick b1416e2a` 再提交余量：与 e728ca64 在
     XWidget.c（+687）等同域文件高概率冲突，且收益仅是哈希连续性——
     **不推荐**，列出仅备查。

## 1. 建议提交划分（按功能域，12 组 + 工具文档组）

> 顺序即建议提交顺序：先零行为风险（工具/文档/时钟源），再各功能域。
> 域内标记 = 本车道在 diff 新增行中实际检索到的注释标记/开关键
> （`「P-PBO」`、`L-固定成本（2026-09-26 夜三）`、`夜三 5a`、`夜四 FULLBATCH`）。
> 任务面提到的其余字母（K/M/Q/S/U/V）**未在 diff 注释中现身**，本划分不猜
> 字母、一律按功能域命名；与各 lane 报告的字母对应关系由执行者按 §5-6 核对。
> 「波1 已入 b1416e2a」= 该域内容已在悬空支线提交过一次（§0.4），按方案 1
> 重提交时提交信息标注来源即可。

| 组 | 功能域 | 波次/标记 | 文件（hunk 拆分点见 §2） | 建议提交信息标题 |
|---|---|---|---|---|
| G1 | PBO 双缓冲异步读回 | 二波，「P-PBO」 | `XGpuRenderDriver_gl.c` PBO 分节（`XGL_PIXEL_PACK_BUFFER` 定义族、`m_readbackPbo[2]`/fence 字段、initialize 建环、session_destroy 释放、读回统一内核） | `xgpu: P-PBO 双 PBO 异步读回——glMapBuffer+fence 双缓冲环消除同步 glReadPixels 占死 CPU（XGPU_PBO_READBACK=0 逐位回退；XGPU_PBO_READBACK_FULL=1 扩全帧滞后口径默认关；XGPU_PBO_STATS=1 诊断；资源创建失败只降级不反窄可用性）` |
| G2 | 脏区呈现链读回收敛 | 波 1 已入 b1416e2a+波内演进 | `XWidget.c` flushBackingStore 5 hunks、`XGpuRenderBackend.c` readbackRect 尾 hunk+`.h` 声明、`gl.c` 读回统一内核中 readbackRect 部分 | `xgui: 脏区呈现链读回收敛——flushBackingStore wholeBbox 一次收拢三处共用+readbackRect 只读本帧呈现区域（XGPU_DIRTY_READBACK=1 回退全窗；XGUI_FLUSH_FULLFALLBACK=1 回退整窗退化；内容源自 b1416e2a 支线，按 §0.4 方案 1 重落库）` |
| G3 | WS_EX_COMPOSITED 交互闪烁根治 | 波 1 已入 b1416e2a | `XPlatformNativeWindow_win32.c`（整文件一域） | `xplatform-win32: CreateWindowExW 默认附加 WS_EX_COMPOSITED——DWM 双缓冲合成使脏区批量提交对外原子整帧（XGPU_WS_COMPOSITED=0 回退；内容源自 b1416e2a 支线）` |
| G4 | 交互空闲闸门+悬浮层降频+静态场景缓存 | 波 1 重现+演进 | `xgui_window_demo.c` 空闲/悬浮层/静态缓存 hunks | `xgui-demo: 交互空闲闸门+悬浮层降频+静态场景缓存——帧泵不再逐轮强制重绘（XGUI_DEMO_IDLE_GATE=0 回退旧口径；XGUI_DEMO_IDLE_OVERLAY_MS 调悬浮层周期；XGUI_DEMO_STATIC_SCENE_CACHE_ON=0 关静态缓存；--benchmark/--screenshot 旁路；内容源自 b1416e2a 支线）` |
| G5 | 斜线 quad 快速路径 | 波 1 已入 b1416e2a | `XPainter.c` `@@ -2825` 斜线 hunk | `xpainter: 非轴对齐 SolidLine 走 drawSolidQuad 快速路径——消除椭圆描边/箭头族逐段画布冲批上传（XGPU_FASTPATH_EXT=0 回退；SYNC 像素契约保持画布路径；内容源自 b1416e2a 支线）` |
| G6 | 全批化（绑定推迟+追加只写批数组） | 四/五波，「夜四 FULLBATCH」 | `XPainter.c` 批面 hunks（`@@ -178/-185/-221/-254` 等）、`gl.c` 绑定/冲批 hunks（`@@ -812/-819/-834/-851/-870/-899`） | `xgpu+xpainter: 夜四 FULLBATCH 全批化——纹理绑定推迟到冲批+追加只写 CPU 侧批数组，入批面覆盖纯色 fillRect/纹理 quad 族（XGPU_FULLBATCH=0、XGPU_QUAD_BATCH=0 逐项回退）` |
| G7 | keep-open 帧保持开启 | 夜三 5a | `gl.c` present 重写主体 `@@ -1727,73 +2119,313`、`XPainter.c` begin/end frame hunks（`@@ -287/-321/-339`） | `xgpu+xpainter: 夜三5a keep-open——窗口直通帧保持开启，同会话同目标 begin_image 嵌套复用零 beginFrame（XGPU_FRAME_KEEPOPEN=0 回退；身份不符异常路径仍关闭与旧行为同价）` |
| G8 | L-固定成本削减+scissor 缓存 | 三波，「L-固定成本（2026-09-26 夜三）」 | `gl.c` L 分节字段/辅助+begin/end/present 镜像跳过 hunks、`@@ -1811/-1849` scissor 缓存 | `xgpu: L-固定成本削减——FBO 跨帧常驻/状态镜像跳过冗余重设/present blit 旁路混合/makeCurrent 一次化+scissor 缓存（XGPU_FBO_PERSIST=0、XGPU_STATE_CACHE=0、XGPU_MAKECURRENT_ONCE=0、XGPU_PRESENT_LEAN=0、XGPU_SCISSOR_CACHE=0 逐项回退）` |
| G9 | 会话/窗口创建瞬时失败重试扩展 | 二波 I 道扩展 | `XGpuRenderBackend.c` 重试门/acquire/shutdown/driver_type hunks+`.h` | `xgpu: 会话/窗口创建瞬时失败重试扩展——probe 上限+退避延迟+窗口级重试闸门（XGPU_SESSION_RETRY=0 关闭；XGPU_PROBE_MAX_FAILURES/XGPU_SESSION_RETRY_DELAY_MS/XGPU_WINDOW_RETRY_COOLDOWN_MS/XGPU_WINDOW_RETRY_MAX_FAILURES 调参）` |
| G10 | 图例瓦片字形修复+glyph 哈希缓存+painter prof | 二波 H 道后续+本波 | `XChartView.c/.h` 图例/渲染 hunks、`XGpuRenderBackend.c` `xgpu_text_equals` `@@ -295` 、`XPainter.c` glyph/绘制/prof hunks | `xgui-chart+xpainter: 图例瓦片字形修复+glyph 哈希缓存+逐原语 prof——（XGPU_GLYPH_HASH=0 回退；XGPU_GLYPH_ATLAS_MAX_ENTRIES 调参；XGPU_PAINTER_PROF=1 诊断）` |
| G11 | page5 四视图默认直绘 | 二波 H 道收尾 | `xgui_demo_page_views.c`（框架侧 paintOffset 缺陷修复已在用户 `e728ca64` 落库） | `xgui-demo: page5 四视图默认直绘——旧 C 虚表离屏快照规避路径降为回退开关（XGUI_DEMO_VIEWS_SNAPSHOT=1 回退；框架 paintOffset 修复见 e728ca64）` |
| G12 | XFileDialog 时钟源跨平台化 | 本波（Windows 可编译性） | `XFileDialog.c` 2 hunks | `xgui-filedialog: 激活去伪时钟换 XDateTime 单调毫秒源——POSIX clock_gettime Windows 不可编译（配对窗口 50ms 语义零变化）` |
| GT | 工具/文档/坏例夹具 | 本波 | `Tools/night3_bench.ps1`（本波增补：五态+p4t20 撕裂格+撕裂自动检测）、`Tools/capture_frames.ps1`/`diff_frames.ps1`/`git.bat`/`night_build_x64.bat`（b1416e2a 重现）、**`Tools/tear_p4t20.png`（40KB 坏例基线，night3_bench 撕裂检测默认依赖，建议随组入库）**、`docs/xgui/night3-*.md` 8 个、`.gitignore` | `tools+docs: 夜三/四终测协议升级——night3_bench 五态口径+撕裂页 p4t20 帧率格+撕裂自动检测（两帧 hash+SW diff 像素统计+坏例基线标定）+ 坏例夹具 + 归因/终测文档 + 测量产物 gitignore` |

分组备注：

- **G7+G8 present 侧纠缠**：两域同在 `xgld_present_to_*` 重写体内（keep-open
  深度规整与 L 镜像跳过交错），硬拆收益低于风险——**建议 G7+G8 合为一个
  提交**（提交信息可并列两域），或现场按 `rg -n "KEEPOPEN|固定成本"` 逐
  hunk 归属后分提。
- **产物归属（不入库）**：`Tools/bench-night4-smoketest.txt`、
  `Tools/bench-pbo-lane-baseline.txt` 未被现有 `.gitignore` 行覆盖（前者是
  夜四命名、后者无通配）——若按「测量产物不入库」惯例处理，需在 GT 组给
  `.gitignore` 补 `Tools/bench-night4-*` 与 `Tools/bench-pbo-lane-*`（本车道
  未代改）。`.gitignore` 新增段里有一行字面 `%T%`（疑为 `%TEMP%` 类意图的
  笔误），**执行提交前应修掉**，git 对该行按字面目录名处理。
- 撕裂验收三口径的观测基线：坏例 `tear_p4t20.png` + 本波预演数据（§4），
  GT 组入库后终测即可复现同一检测。

## 2. 跨域文件的 hunk 级拆分点（`git add -p` 参照，快照 23:3x）

四个文件承载多个功能域；hunk 头为本快照实测（§0.2 diff），执行时以现场
`git diff` 重验：

- **`Drive/windows/Graphics/XGpuRenderDriver_gl.c`**（32 hunks，≥4 域）：
  - G1（PBO）：`@@ -89`（GL 常量定义族）起、struct 字段族
    `@@ -188/-197/-211/-220/-238`、`@@ -354` 读回统一内核、initialize 建环
    `@@ -1388/-1458`、session_destroy 释放 `@@ -1551/-1576`。
  - G6（FULLBATCH）：`@@ -812/-819/-834/-851/-870/-899`（绑定/冲批/追加）。
  - G7+G8（keep-open+L）：`@@ -1628/-1645/-1652/-1682/-1700/-1715/-1727`
    （end_frame+present 重写，两域交错，建议合提）、L 字段/辅助分节
    （`「L-固定成本」` banner 附近 hunks）、scissor `@@ -1811/-1849`。
  - G10（glyph）：`@@ -2422/-2443`（glyph atlas）。
- **`Src/XGui/Graphics/XPainter.c`**（36 hunks，≥4 域）：
  - G10：`@@ -111,6 +111,177`（glyph 哈希缓存主体）、`@@ -10516/-10525`
    （drawText）、`@@ -10337..-10481`（glyph 绘制路径）、`@@ -10317`
    （prof 聚合）、`@@ -910/-933`（prof/状态）。
  - G6（FULLBATCH）：`@@ -178/-185/-221/-254`（画布批/sync 判定）。
  - G7（keep-open）：`@@ -287/-321/-339`（begin/end frame 嵌套复用）。
  - G5（斜线）：`@@ -2825,8 +3135,69`（XGPU_FASTPATH_EXT 门）。
  - 余量（`@@ -607/-744/-11904/-11939` 等）按现场内容就近归入 G6/G10。
- **`Test/XGuiDemo/xgui_window_demo.c`**（15 hunks，≥2 域）：
  - G4（空闲族）：`@@ -244/-321`（字段）、`@@ -813/-832/-841/-851`（绘制/
    布局）、`@@ -1173`（帧泵闸门）、`@@ -1294/-1310/-1320`（定时器）、
    `@@ -3117/-3191`（env 读取+首帧收敛）、`@@ -2189`（创建）。
  - G11 相关（若 demo 侧有 page5 联动行）按现场内容归入 G11。
- **`Src/XGui/Graphics/XGpuRenderBackend.c/.h`**（2 域）：
  - G9（重试）：`.c` 头部 prof 之外的 acquire/shutdown/driver_type/
    addRequestedOverride hunks（`@@ -368/-396/-426/-574/-596/-604/-639`）+
    `.h @@ -104`。
  - G2（readbackRect）：`.c @@ -987,6 +1157,39` + `.h @@ -320,6 +324,23`。
  - G10（text_equals）：`.c @@ -295,6 +332,70`。
  - prof 计时 hunks（`@@ -67/-106/-120/-132`）随 G10 的 painter prof 一致
    归属（或单独小提交）。

## 3. 用户 setParent 域（单列）——工作区残留 = 0，无事可提交

- **工作区残留**：对 §0.2 全量 diff 检索 `setParent|SETPARENT` = **0 行命中**
  （PowerShell 逐行 Contains 实测）——用户已在 `bf5757fb`/`e728ca64` 自行
  落库，工作区**没有任何 setParent 相关 hunk 需要标注或提交**。
- **落库形态核实**：HEAD 的 `XWidget.c` 含 `XWidget_setParent`/
  `XWidget_setParentPlain` 与「对标 Qt QWidgetPrivate::setParent_sys
  （qwidget.cpp:10925-11035）」注释（`git show HEAD:...` 检索实测）；
  但 02:08 工作区时代的回退键 `XGUI_SETPARENT_RESET_GEOMETRY` **既不在
  HEAD 也不在 23:03 exe**（§0.3 键串扫描）——用户落库版本未保留该回退键。
  是否有意弃用回退键属用户设计决策，本车道仅归档事实，不动其域。
- 旧文档 §3 的「3 组旧测试断言互斥」问题域已随用户提交终结于其自己的
  提交里，与本波工作区无关；任何 lane 仍禁碰 setParent 相关代码与测试。

## 4. 第四波终测口径预演（23:03 exe；fbo0=真对照、dsp0=空操作=基线）

- 脚本本波增补（`Tools/night3_bench.ps1`，语法 Parser 校验通过）：
  1. **撕裂页帧率格**：`--page 4 --tab 20` 入矩阵（主会话 23:2x 实锤残影
     复现参数），格名 `p4t20`（如 `gpu_incr_p4t20[-态]`），与普通页同一
     五态×两模式×SW 对照口径（单格执行收拢进 `Invoke-IncrCell`，保证两类
     格净化/栅长/日志名规则逐位一致）；`-SkipTearPage` 可关。
  2. **撕裂自动检测**（`-SkipTearScan` 可关）：三跑两比——`--screenshot`
     两遍 GPU（第 3 帧 FBO 读回后退出，`xgui_window_demo.c:1288-1337`）
     SHA256 对比；GPU 截图 vs SW 截图（paintImage 干净重渲）像素 diff
     （内嵌 C# LockBits+容差 8：变化数/占比/外接框/变化行带）；对坏例
     `Tools/tear_p4t20.png`（800x600）做同口径 diff=已知坏例基线幅度；
     机器判读三级：gpu_a≈坏例（<0.05% 逐位级）/仅局部差异（<1%，残差带=
     悬浮层等活区域）/不同帧，外加 gpu-vs-sw÷坏例-vs-sw 幅度比（≥0.7×=
     同带）。撕裂检测不注入新环境键，恒测默认路径。
  3. 附带：`-BenchTag` 参数（TEMP 副本名基，跨 lane 并跑防 taskkill 互杀，
     默认 `xgui_bench3` 不变=历史口径不变）。
- 预演命令（独立产物路径，不覆盖终测产物；全协议）：
  `powershell -NoProfile -ExecutionPolicy Bypass -File Tools\night3_bench.ps1
  -Takes 1 -Seconds 2 -BenchTag xgui_bench3_r4
  -LogDir ...\Tools\bench-night3-rehearsal4-logs
  -CapsDir ...\Tools\caps-night3-rehearsal4
  -TearCapsDir ...\Tools\tear-night3-rehearsal4
  -SummaryPath ...\Tools\bench-night3-rehearsal4.txt`
  （产物与 `bench-night3-rehearsal4-stdout.txt` 均被 `.gitignore`
  `Tools/bench-night3-*` 覆盖，不入库。）
- 结果：**84/84 格全采**（2 后端×(5 页+p4t20)×(5 态增量+1 整帧)）+闪烁
  +撕裂检测+空闲全跑（RC=0）。
- **基线标注（本 exe 实测口径）**：
  - `fbo0`（XGPU_FBO_PERSIST=0）——键已编入 exe（§0.3），**真对照**：GPU
    def vs fbo0 = p0 −1.3% / p2 −1.0% / p4 +2.3% / p5 −2.3% / p6 −2.2% /
    p4t20 −0.4%，±2.3% 带内无一致方向（2s 短栅；5s 终测栅再判）。
  - `dsp0`（XGPU_DISPATCH_MERGE=0）——键**不在** exe，空操作=基线，其列
    差值是运行间噪声，不得当收益解读。需真对照时以
    `-KDispatchSwitch XGPU_STATE_CACHE`（或 MAKECURRENT_ONCE/PRESENT_LEAN，
    键已在 exe）覆盖重跑。
    **覆盖机制已实测可用**：`-Pages 0,4 -KDispatchSwitch XGPU_STATE_CACHE`
    抽测（`Tools/bench-night3-rehearsal4-sc.txt` / `*-sc-stdout.txt`，
    `-SkipFlicker -SkipIdle -SkipTearScan`），dsp0 日志 env 行恰
    `XGPU_STATE_CACHE=0`+其余键 `<unset>`（gpu_incr_p4-dsp0-take1.log 实读）。
    抽测读数：gpu p4 def 651.0 vs dsp0 636.0（−2.3%）、p4t20 487.1 vs
    477.3（−2.0%）；**SW 侧对照 p4 −3.4% / p4t20 +1.3%**（GPU 态键不可能
    影响 SW）——即 2s 栅运行间噪声地板约 ±3.4%，GPU 的 −2%/−2.3% 在噪声
    带内，无一致方向；终测 5s 栅+多遍再判。
- 主表（增量 def 列，2s 栅；GPU 仍全面低于 SW，p4t20 撕裂页同样）：

| 口径 | 页面 | SW fps | GPU fps | GPU/SW |
|---|---|---|---|---|
| 增量 | page 0 | 9167.5 | 922.0 | 0.101 |
| 增量 | page 2 | 9027.4 | 924.1 | 0.102 |
| 增量 | page 4 | 7964.3 | 630.4 | 0.079 |
| 增量 | page 5 | 6930.4 | 619.3 | 0.089 |
| 增量 | page 6 | 4910.2 | 410.5 | 0.084 |
| 增量 | **p4t20(撕裂页)** | 6794.8 | 483.5 | **0.071** |

- 五态对照（GPU 侧节选；完整见
  `Tools/bench-night3-rehearsal4-stdout.txt`）：

| 后端 | 页面 | def | dr1 | pbo0 | fbo0(真对照) | dsp0(=基线) |
|---|---|---|---|---|---|---|
| gpu | page 0 | 922.0 | 704.6 | 817.6 | 909.9 | 916.8 |
| gpu | page 4 | 630.4 | 478.8 | 579.6 | 644.7 | 642.4 |
| gpu | p4t20 | 483.5 | 372.7 | 444.1 | 481.7 | 484.6 |
| gpu | page 6 | 410.5 | 313.8 | 358.5 | 401.6 | 411.5 |

- **撕裂自动检测（本预演主结论）**：
  - 两遍 GPU 截图 SHA256 **相等**（7EE2A6BEABAF…，逐位确定）；gpu_a-vs-gpu_b
    像素 diff = 0。
  - **gpu_a vs 坏例 `tear_p4t20.png` = 0/480000 像素差（0.0000%）——当前
    23:03 exe 的 p4t20 第 3 帧 FBO 读回与已知坏例逐位一致** ⇒ 机器判读
    「撕裂特征仍在（与已知坏例几乎逐位一致）」。
  - gpu-vs-sw = 47673 px（9.9319%）= 坏例-vs-sw（比值 1.0）⇒「与坏例同带」。
  - 检测器对已知坏例状态的标定：**通过**（以最强信号复现主会话 23:2x
    「撕裂复现实锤」）。终测在修复构建上重跑时，同口径数字即为根治量证。
- 闪烁：`page=4 gpu frames=30 pairs=29 changed>500 : 0 maxChanged=21`——
  与主会话 0/29 记录同带（本预演复现）。
- 空闲 CPU（16 逻辑核单核口径）：`gate-def 55.6%`、`gate0 200.4%`——闸门
  收益方向成立（旧口径回 3.6 倍），但 gate-def 55.6% 未达「近零」预期，
  终测时留意（可能为悬浮层/背景活动，属测量观察，非协议失败）。
- 附注：`--screenshot` 与 2s 短栅下不打 `[xgpu-prof] driver=` 行，tear-scan
  的 driver 取证列恒 `n/a`（日志留档可人工核）；5s 终测栅是否恢复未验证。

## 5. 执行提交时的核对清单

1. **先决**：拿到 §0.4 b1416e2a 处置二选一裁决（推荐方案 1 按域重提交）。
2. 重取 `git status --porcelain` + `git diff --stat`，确认各 lane 已停止
   追写（driver/XPainter diff 行数连续两次采样一致）再执行分组。
3. 按 §2 拆分点 `git add -p`；G7+G8 按备注建议合提。
4. GT 组：`.gitignore` 修 `%T%` 笔误行 + 视裁决补
   `Tools/bench-night4-*`/`Tools/bench-pbo-lane-*`/`Tools/tear-night3*/`/
   `Tools/caps-night3-rehearsal*/`；`tear_p4t20.png` 随组入库（night3_bench
   默认依赖）；撕裂归因 lane 的 sweep 产物族（§0.1 增量清单）与其
   `night3-sweep.md` 的入库/忽略归属先与该 lane 对齐再动。
5. dsp0 口径：终测默认键名 `XGPU_DISPATCH_MERGE` 对本 exe 空操作——要么
   按 K/L 报告改用 `-KDispatchSwitch XGPU_STATE_CACHE`（本车道已验证覆盖
   可用），要么报告里明确标注该列为基线。
6. 域字母核对：任务面 K/M/Q/S/U/V 未在 diff 注释现身（§1 前言），执行者
   以各 lane 报告核对组号↔字母映射后再写提交信息正文。
7. 全程本波车道只做 `git commit`（提交执行者终测报告落版后再动）；
   U 组（setParent）本波无残留，无需单独立项（§3）。
