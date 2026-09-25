# GPU 第二夜战报（2026-09-24 夜 ~ 09-25 凌晨）：白屏/闪烁根修、Vulkan 激活与基线重锚

> 整理：战报整理员（2026-09-25 03:2x 重写版，**取代本文件 02:15 旧稿**）。
> 通道纪律：本报告为只读整理产物——整理过程未构建、未运行任何 demo/测试
> 程序、未 commit；实测数据一律转引自 `Tools/` 留档（逐条注明文件），
> 代码结论均带 文件:行号。证据等级：**[A]**=本通道本次实跑命令/实读文件
> 复核；**[B]**=在档文档或 Tools 留档（本通道实读了该文件，未复跑其测量）；
> **[M]**=任务给定材料/背景（无文件留档可考，如实标注）。
>
> **对 02:15 旧稿的三处实质修正**：①`git log --oneline 8a174def..e366ceef`
> 实测 **7 条**（5 条本地主线 + 2 条随合并进入的远端线，旧稿已记 7 但未列
> 全哈希，本版补全）；②旧稿「工作区恰等于 `433a5818` 的树（diff 为空）」
> **已失效**——并行攻坚流在 02:49–03:10 对 gl.c/XPainter.c 追加了未暂存
> WIP（本版 §〇 如实记录时间线）；③旧稿 P0-1 建议「cherry-pick/重放
> e366ceef」**不可执行且危险**——本版树取证（§〇.3）证明 e366ceef 的树
> 不含其提交信息所述修复、其 diff 本身是对合并线源码修复的回退。

---

## 〇、提交与工作区状态（如实记录，动工前必读）

### 〇.1 提交区间 [A]

`Tools\git.bat log --oneline 8a174def..e366ceef` 实跑返回 **7 条**：

| 提交 | 时间(%ci 实测) | 主题 |
|---|---|---|
| `e366ceef` | 09-25 00:02:17 | fix：GPU 交互白屏/闪烁根修——上屏默认改回读+BitBlt（RDP 换链帧不可达实测）+ 双通道 XGPU_PRESENT 开关；回归 0 新增失败/验收 68/68/autotest 0 FAIL |
| `034e0617` | 09-24 19:48:06 | docs：远端合并记录+归因切割（worktree 对照实验）入册 §8.0g26；对照构建脚本入库 |
| `be2f24b2` | 09-24 19:45:37 | fix：帧开整幅清零批量画布（跨帧字形残留污染 QSS 盒模型中心像素）+ 像素诊断临时探针；遗留 2 项 GPU 测试失败登记今晚攻坚 |
| `f6d295a1` | 09-24 19:11:55 | merge：远端夜测闭环(83+ 缺陷修复/框架九项根修/GL 诊断) 并入本地 GPU 二阶段（首父 diff 实测 **67 文件 +5220/−380**） |
| `433a5818` | 09-24 19:01:37 | feat：GPU 直通二阶段攻坚——脏区批量提交(105x)+统一顶点批+嵌套帧+Vulkan Windows 激活与 7 项渲染修复 |
| `77758746` | 09-24 18:54:22 | （远端线，随 f6d295a1 并入）夜间全量测试四轮闭环 83+ 条缺陷修复 + 框架九项根修 + GL 路径诊断入册（完整信息存 `Tools\sl.txt` [A]） |
| `87213b92` | 09-24 00:07:56 | （远端线，随 f6d295a1 并入）交互启动 AV/回归堆损坏三缺陷根修 + GPU 子矩形批量化 WIP 存档 |

任务书口径「5 个提交」=上表前 5 条本地主线；后 2 条为远端线。

### 〇.2 reset 后状态 [A]

- 分支 `codex/xdevice-file-platform` = `8a174def`（`.git\refs\heads` 实读），
  reflog 尾行 `reset: moving to 8a174def`——**5 条本地提交已从分支撤回**，
  以悬空对象存在（`ORIG_HEAD=e366ceef`）。
- **暂存区恰等于 `433a5818` 的树**：`git diff --cached --stat 433a5818`
  输出为空 [A]。即本地 GPU 二阶段成果（§1 #3~#7、XGui.md §8.0g25/g26、
  勘察报告）以**已暂存未提交**状态在场。
- **工作区在 02:15 之后被并行攻坚流持续追加未暂存 WIP**（时间线 [A]：
  02:15 旧稿记录 diff 为空 → 02:52 实测 gl.c +74/XPainter.c +32 →
  03:10 实测 gl.c **+106**/XPainter.c **+51**，`git diff --stat`）：
  - `XGpuRenderDriver_gl.c`：**P0-2 scissor 同矩形缓存**（`XGPU_SCISSOR_CACHE`
    开关，现位于 gl.c:1564）+ `p2Site="PRES"` 探针（gl.c:1401）；
  - `XPainter.c`：**P0-1 脏列清**（冲刷后只清本批脏列 [x0,x1)，`XGPU_CLEAR_ROWS_FULL=1`
    回退整宽清，XPainter.c:170-171）。
  两文件 mtime 03:10、**仍在被并行编辑**；本报告行号引用对该二文件标注
  「@03:10」。工作区另有 **110 个未跟踪文件**（证据 txt/帧抓目录/探针副本/
  本报告等，`git status --porcelain` 实数）。

### 〇.3 树取证：e366ceef 的树与其提交信息不符（本版新增，P0 前置）[A]

逐对 `git diff --stat` 实测：

- `tree(e366ceef) − tree(433a5818) = 恰 10 个非源码文件、+626 行`：
  Test/XGuiDemo/overnight_report/final_report.md(+280)、Test/XGuiTest/
  XTouchMultiPointTest.c/.h(+239/+12)、Tools/{capture_frames,diff_frames,
  find_prev}.ps1+{git,pm_build,rc_build,rc_demo_build}.bat(+88)。
  **无任何 Src/Drive 源码差异。**
- `e366ceef` 自身 diff（034e0617→e366ceef）= **70 文件 +461/−4780**：
  删除的正是合并线带入的源码修复（XWidget.c −767、XPainter.c −65、
  gl.c −113、xgui_regression_test.c −481、XGui.md −248 等）+ 新增 5 个
  Tools 脚本。`XGui.md` 的 −248 = 远端 +225 与 034e0617 记录 +23 之和
  （逐项对账吻合）。
- `be2f24b2` 自身 diff（f6d295a1→be2f24b2）= **XPainter.c +65、
  xgui_regression_test.c +5/−2**（帧开清零+探针）。
- `034e0617` 自身 diff = XGui.md +23（远端合并冲突裁决/归因切割记录，
  已从其树恢复，见 §4.2）+ Tools/find_prev.ps1、rc_build.bat。
- **`XGPU_PRESENT` 在 e366ceef/034e0617/be2f24b2/433a5818 四棵树中
  `git grep` 全部零命中**。

**结论**：#1（回读+BitBlt+XGPU_PRESENT）与 #2（帧开清零）**不在任何提交
树中**；e366ceef 的树相对其父提交是一次「回退合并线源码修复」的快照，
**对 e366ceef 执行 cherry-pick 会把 −4780 行的回退带进工作区**。恢复
素材只能来自：be2f24b2 树（#2 的 +65）、f6d295a1/77758746 树（远端线）、
`Tools\merge-backup\`（=433a5818 态的 10 个源码快照 [A]，实测其 gl.c 与
f6d295a1 树 gl.c 恰差合并的 +113 行）、以及重新实现。

### 〇.4 二进制留档与环境变量矩阵 [A]

`Tools\lane_env_scan.ps1`（ASCII 子串扫描）实跑结果：

| 字符串 | merged(02:59) | probe(01:55) | premerge(23:02) |
|---|---|---|---|
| XGUI_RENDER_BACKEND / XGPU_BACKEND / XGPU_PROF / XGPU_PROFILE / XGPU_QUAD_BATCH / XGPU_REGION_DISABLE / XGUI_GPU_SYNC | Y | Y | Y |
| **XGPU_PRESENT** | **.** | **.** | **.** |
| XGPU_SCISSOR_CACHE / XGPU_CLEAR_ROWS_FULL（03:10 WIP 开关） | . | . | . |
| present-path / batch-flush / gpu-fallback / fbo-probe（夜战临时探针） | . | . | . |
| [xgpu-prof] / [gl-info] | Y | Y | Y |

- merged=`bin\XGuiWindowDemo_Test.exe` 已被并行流于 **02:59 重建**（02:15
  旧稿的「00:08 构建」口径失效）；premerge=`Container-premerge\bin-pm`（23:02）；
  `Tools\probe_demo.exe`（01:55，按纪律复制的探针副本）。本通道未运行三者。
- 三个留档二进制**均不含 XGPU_PRESENT 与四个夜战探针字符串**——夜战后段
  的回读/换链对照实验（flick_base_*，00:30–01:55 窗）跑在**未留档的瞬态
  构建上**，其源码与 exe 均已不在（`Tools\flick.txt:1` 的
  `[present-path] -> GL swap` [A] 是该瞬态构建的唯一痕迹）。
- 注意：merged(02:59) 不含 03:10 工作区 WIP 的两个新开关字符串——
  **bin\ 落后于工作区源码，验证 WIP 行为必须重新构建**。
- 环境口径 [B/材料]：Windows 10 / AMD Radeon (TM) Graphics（`prof20_stderr.txt`
  实读 `[gl-info] vendor=ATI Technologies Inc. renderer=AMD Radeon (TM)
  Graphics version=4.6.0 Compatibility Profile Context 22.20.27.09.230330`，
  真硬件非 llvmpipe）/ Debug / RDP+OrayIdd 虚拟显示 / 800×600 图表页
  （`--page 4 --tab 20`）。

---

## §1 修复清单（7 项；「在场」= 当前工作区可编译载体）

| # | 修复 | 提交 | 在场 |
|---|---|---|---|
| 1 | 白屏/闪烁根修：上屏默认改回读+BitBlt + 双通道 XGPU_PRESENT | e366ceef（仅提交信息） | ❌ 不在场（树/二进制均无，§〇.3/〇.4） |
| 2 | QSS 画布污染根修：帧开整幅清零批量画布 | be2f24b2（树内可取 +65） | ❌ 不在场（工作区无，恢复素材在 be2f24b2 树） |
| 3 | 统一顶点批（跨字形存续） | 433a5818 | ✅ 在场 |
| 4 | 嵌套 GPU 帧 | 433a5818 | ✅ 在场 |
| 5 | Vulkan Windows 激活（三件套） | 433a5818 | ✅ 在场 |
| 6 | Vulkan 渲染修复 7 项 | 433a5818 | ✅ 在场 |
| 7 | 脏区批量提交（105×）+ demo 启动 AV 根修 | 433a5818 | ✅ 在场 |

**#1 白屏/闪烁根修（❌ 不在场）**[A+B]
内容（按提交信息 [A] 与任务材料 [M]）：RDP 会话实测换链（swap）帧不可达
（白屏/闪烁直接来源），上屏默认改「FBO 回读→后备 XImage→GDI BitBlt」
（与软件路径同一上屏管道），保留 `XGPU_PRESENT=swap/readback` 双通道开关；
提交时回归 0 新增失败/验收 68/68/autotest 0 FAIL（信息自称，门禁跑在当时的
工作区而非其提交树，见 §〇.3）。
不在场证据链 [A]：①四棵树 `git grep XGPU_PRESENT` 零命中；②三个留档
二进制 ASCII 扫描零命中（§〇.4）；③工作区 `xgld_present_to_window`
@03:10 仍为 FBO blit+SwapBuffers（gl.c:1383-1428 快路径、:1429-1450
整屏 quad 回退，`XPlatformOpenGLContext_swapBuffers` 收口），无 BitBlt
分支；④`Tools\merge-backup\XGpuRenderDriver_gl.c:1336-1424`（433a5818 态）
同为 blit+SwapBuffers；⑤sw_baseline_recheck 的 run_one.bat 设置
`XGPU_PRESENT=readback` 但被测 probe 二进制不含该字符串——实验中它是惰性
变量（`Tools\sw_baseline_recheck\REPORT_sw_fullframe_baseline_recheck.md`
§3 亦独立判其「Src 内无读取者」[B]）。
闪烁根因侧写：四路诊断之三——demo GPU 分支全场景重画为闪烁源头——
实锚 `Test/XGuiDemo/xgui_window_demo.c:762-766`：GPU 分支注释自认
「CPU memcpy 的静态场景拷贝不生效」而 `demo_drawStaticScene` 整场景重画
[A 实读]；瞬态构建曾打出 `[present-path] -> GL swap`（flick.txt:1 [A]）
与 `[gpu-fallback]` 降级探针（pdiff 时代，字符串未见于任何留档二进制 [A]）。
另注 [A]：无法仅凭字符串取证排除 01:55/02:59 留档二进制内含「无开关的
回读默认路径」实现（readback 通道本身有 glReadPixels 字符串，属既有
离屏/降级共用代码）；但 03:10 工作区源码态确证为 blit+swap。

**#2 QSS 画布污染帧开清零（❌ 不在场，恢复素材明确）**[A+B]
内容：批量暂存画布跨帧复用，冲刷后残行依赖脏区覆盖全部写入；有命令写入
逃过脏区记录（字形边缘/内部直写）时残留污染后续同尺寸批——实锤为 QSS
盒模型中心像素污染（034e0617 入册记录（从其树恢复 [A]）：worktree 对照
实验定位「跨帧字形残留」，上帧 red "ab" 残留进下帧 QSS 提交，
**canvas(20,10)=红@12%**；修复后仍余 2 项 GPU 测试失败登记今晚首位）。
修法=帧真正开启（深度 0→1，每帧一次）时 `XImage_fillRect(&g_gpuBatchCanvas,
NULL, 0u)` 整幅清零+脏区复位，代价每帧一次 1.9MB memset 换确定性正确。
不在场证据 [A]：工作区 XPainter.c 仅 :626 一处 `XImage_fillRect(&g_gpuBatchCanvas)`
（目标依赖命令路径的画布清底，非帧开清零）；帧开清零的 +65 行在 be2f24b2
树中（§〇.3），`git show be2f24b2:Src/XGui/Graphics/XPainter.c` 可直接取回。
配套验证 [B]：`Tools\fbo_run.txt` 六样 `[fbo-probe] center=20 9f df` 逐位
恒定输出（本通道实读复核六行全同 [A]）；`Tools\batchcanvas.ppm` 像素 dump。

**#3 统一顶点批（✅ 在场）**[A+B]
单程序管线（frag=texture2D(u_texture,v_texcoord)×v_color，pos2+uv2+color4
=32B/顶点，退化三角带连接 48 float/quad）：纯色 quad 走 1×1 白纹理+uv 中心
（白×色=色逐位精确）、字形图集走子矩形 UV+v_color=modulate；冲批触发收窄到
「采样纹理切换/混合切换/scissor/帧界」，同图集字形长跑批得以存续（此前每
字形冲批=碎片化反噬 128 vs 180 FPS，XGui.md:1266-1270/1302-1312 在档 [B]）。
锚点 [A 实读]：`XGpuRenderDriver_gl.c:144`（m_whiteTexture）、`:848`
（批状态 xgld_batch_set_state）、`:1053-1067`（白纹理创建；NEAREST/CLAMP
教训 XGui.md:1311-1312）。

**#4 嵌套 GPU 帧（✅ 在场）**[A+B]
软件路径每控件 painter begin/end 零成本在 GPU 直通下放大为每帧 ~45 对 FBO
绑定+状态复位+离屏 initialImage 重复上传；修=同会话同目标复用已开帧（深度
计数，内层结束只递减，readback/endFrame 由外层收口）。P0 按钮页 445→490
FPS（+10%）（XGui.md:1313-1318 在档 [B]）。
锚点 [A 实读，XPainter.c @03:10]：`:307-308`（g_gpuFrameActive* 跟踪）、
`:6913-6933`（begin 深度计数命中判定）、`:6918`（~45 对/帧注释）、
`:414/:973`（sync readback 钩子）、`:7085-7149`（endFrame 六出口收口）。

**#5 Vulkan Windows 激活（✅ 在场）**[A+B]
无 SDK 环境三件套：vendored Vulkan-Headers 1.3.290（Tools/vulkan-headers-tmp/，
staged 在场 [A]）+ System32\vulkan-1.dll dumpbin 导出生成导入库
（Tools/make_vulkan_lib.bat → vulkan-importlib/vulkan-1.lib，246 符号 [A]）+
CMakeLists 无 SDK 回退分支（SDK 优先→vendored 兜底→都无则不启用）。
锚点 [A 实读]：`CMakeLists.txt:271-286`（WIN32 分支三处
`add_compile_definitions(XINYUE_C_HAS_VULKAN=1)`）、`:166`（UNIX 分支）。
实测会话创建成功无回退：`Tools\vulkan_stderr.txt` 打印
`[xgpu-prof] driver=vulkan window=1`（真 AMD ICD，vulkaninfo 无 lavapipe，
勘察 §5.4 [B]）；`Tools\rb.txt` 的 cl 命令行含 `-DXINYUE_C_HAS_VULKAN=1`
[A]（23:37 构建实际启用）。

**#6 Vulkan 渲染修复 7 项（✅ 在场）**[A+B]
①glyph_atlas_upload/readback 帧中 reset m_cmd 丢弃全部已录绘制→改走
transferCmd；②m_atlasSet 从未写入即绑定采样（lavapipe SIGSEGV 最强候选）；
③draw_image 内联打断改 suspend→transfer→resume 三段式；④上传 CPU 侧残留
GL 式 R/B 交换（BGRA 应直拷）；⑤record_quad 顶点 UV 硬编码全幅→字形图集
子矩形；⑥fillRect 半透明色预乘修正；⑦drawImageUv 接入 procs 表
（XGui.md:1239-1251 在档 [B]；勘察 #13-#18 归因 [B]）。
终态：XGuiGpu_Test 双后端（GL/Vulkan）同套断言全绿；XGPU_BACKEND=vulkan
全量 demo autotest 0 FAIL（XGui.md:1331-1334 在档 [B]）。
锚点 [A 实读]：`XGpuRenderDriver_vulkan.c:57`（m_transferCmd 字段）、
`:917`（创建）、`:1121-1140`（submit/reset/begin 三钩子）、`:1934`
（帧中 reset 丢弃绘制根因注释）、`:2013`（readback 同款注释）。

**#7 脏区批量提交（105×）+ demo 启动 AV 根修（✅ 在场）**[A+B]
批量暂存画布改「透明画布、零快照，失效点一次 `drawImageRegion` 脏区
SourceOver 提交」（语义依据 Over 结合律）；效果（图表页 full 重绘）：
readback 986→21 次/5s、drawImage 全帧提交归零、**1.8 FPS→223 FPS（约 120×，
g25 收官累计 ~105× 口径）**，同口径软件 255 FPS（XGui.md:1216-1223/1275-1282
在档 [B]）。demo 启动确定性 AV 根修：`XAbstractItemModel_setDimension` 行
数组按 capCols 整块分配（高列号写越界堆，demo 全功能总闸，XGui.md:1210-1215
[B]）。锚点 [A 实读]：`XPainter.c:187`（drawImageRegion 提交点）、
`XAbstractItemModel.c:255-263`（容量根修注释与分配）。

**附注（随 reset 退出工作区）**：`f6d295a1` 首父 diff 67 文件 +5220/−380
[A]——远端夜测线（键盘/弹层/焦点/对话框/布局/效果/图表全族 83+ 缺陷修复+
框架九项根修，`Tools\sl.txt` 完整信息 [A]，含
Test/XGuiDemo/overnight_report/final_report.md 全量档案）。冲突裁决 [B，
034e0617 记录恢复]：XPainter 批量子系统两套不兼容设计取本地已验证实现，
远端 readbackRect/drawImageRegion 原语休眠保留；XAbstractItemModel 同一 P0
取远端版。

---

## §2 性能对照表（800×600 图表页 --page 4 --tab 20，Debug，RDP 会话）

### 2.1 主表：四格 + 对照（口径逐条注明）

| 口径 | SW | GPU(GL) | 出处 |
|---|---|---|---|
| **增量**（mode=repaint） | **2241.0**（0.446ms，静态场景缓存命中态，frames=11206）[B]；**211.3**（4.732ms，6s 样，带键盘释放迹）[A]；**2717** [M 任务给定，无留档] | **169.6**（5.897ms，5s 样）[B]；**167.4**（5.975ms，20s 样 frames=3349，**重锚值**）[A]；163.7/164.6/168.4 三样 [A] | 设计稿 §1.1；Tools\sw_stdout.txt:13；Tools\prof20_stdout.txt:3；Tools\prof1/base2/base3_stdout.txt |
| **整帧**（--benchmark-full） | 136.7（7.318ms）[B] = **慢速/受扰态读数**；**空闲稳态 214.6~223.4**（8s×6 轮，merged 均值 220.3 / premerge 217.5，差 ≤1.3%）[B]；**252** [M] | **166.7**（5.999ms）[B] | 设计稿 §1.1；Tools\sw_baseline_recheck\REPORT_*.md 主表+§3 |
| 交互直通（FPS 浮层） | — | **403/400** [M 任务给定；Tools 下无数值留档，本通道未复跑] | 材料/背景 |

- **GPU 脏区零红利（四路诊断①）**：GPU 增量 169.6 ≈ GPU 整帧 166.7，差
  1.7% [B]——脏区裁剪只省 fragment 光栅，省不掉原语提交与纹理上传；
  同口径 SW 增量/整帧 = 2241/136.7 ≈ 16.4×（设计稿 §1.2 [B]）。
- **合并前后对照 [A]**：GPU 增量 合并前 139.8（`Tools\pm_stdout.txt`，6s，
  839 帧，premerge 23:02 二进制）→ 合并后 167.4（prof20），**+19.7%**
  （本地 GPU 二阶段贡献；远端线对 SW 整帧无回归——§2.3 recheck 结论 4）。
- **基线重锚（四路诊断④）**：早前「交互 ~80FPS」前提未复现；稳态以
  prof20 重锚（20s、3349 帧、fps=167.4 [A]）。设计稿「GPU 整帧 166.7 vs
  SW 整帧 136.7 已反超」的说法**须修正**：recheck 证明 136.7 是慢速态，
  SW 空闲整帧 214.6~223.4 [B]——**GPU 整帧当前仍落后 SW 空闲整帧约
  25%~30%**，反超尚未发生，P0-4（场景保留）仍是唯一量级路径。
- SW 增量三数（2717[M]/2241[B]/211.3[A]）相差数倍，指向静态场景缓存命中态
  与失效态、CPU 时钟态的场景/环境敏感性（§2.3 recheck 机制学），历史引用
  必须带口径与环境标注。

### 2.2 瓶颈剖析（XGPU_PROF 5s 窗，20s 样）[A]

`Tools\prof20_stderr.txt` 实读：四个 5s 窗 fillRect=173487/179019/179718/
179154、solidQuad=161943/165454/165974/166023、readback=21/20/19/20
（1.114~2.101ms/次）、present=795/850/854/832（0.098~0.126ms/次）。摊到
基准帧 **~454 原语/帧 × ~15µs/原语**（GL 函数指针→AMD ICD→RDP 栈固定
开销；设计稿 §1.1/§6 同口径 [B]；XGui.md:1345-1352 三方对照 385 原语/
5-10µs 在档 [B]）。**四路诊断②由此锚定：瓶颈在 CPU 侧原语提交+上传，
非 GPU 光栅**——present 仅 ~0.1ms、readback 已被脏区批量压到 ~20 次/5s，
剩余成本=每原语 GL 调用链 + 每帧 ≥2 张整幅纹理重传（demo 静态场景+chart
静态层各 ~1.83MB，设计稿 §1.2.4/§⑥ [B]）。

### 2.3 实验组与专项复核 [A/B]

| 项 | 数值 | 出处 | 含义 |
|---|---|---|---|
| Vulkan 窗口会话（GL 同页） | **2.0 FPS**（avg 510.280ms，12 帧/6.1s）[A] | Tools\vulkan_stdout.txt | 激活成功但性能病态：readback 26.036ms/次×8、drawImage 887 次/5s×0.189ms（vulkan_stderr.txt [A]）——recon #20 全同步+#22 全队列往返的预期兑现 |
| SW 整帧基线矛盾复核（P0-3，01:54–02:20 专项） | **136.7 归因闭合**：并发帧泵进程（遗留 `--gpu` 窗口实测 2.2 核/41% load）+ 平衡电源计划 CPU 双态（2000MHz 基频 vs boost，136.9/136.9/136.1/133.6 四次复现）[B] | Tools\sw_baseline_recheck\REPORT_*.md §3 + 18 份 log | **非代码回归**：merged/premerge 空闲 8s 稳态差 ≤1.3%；「合并致 SW 回退」证据不成立 |
| expC 关批对照（nobatch） | 172.5 FPS [A] | Tools\expC_nobatch_stdout.txt | 与 167.4 同带（批合并在本页中性） |
| expB region 实验两样 | **4.7 FPS**（avg ≈212ms）[A] | Tools\expB_region/expB20_stdout.txt | drawImage 2353-2413 次/5s×1.9ms 恶化主因——增量通道退化为整幅重传的反面教材 |
| flick 闪烁场景剖析 | readback **40** 次/5s（稳态 19-21）[A] | Tools\flick_prof.txt:3-4 | 闪烁帧 readback 双化，与上屏/降级路径抖动假设一致；帧抓差分目录 caps_flick_*（60/90/60 帧法）在档 [A] |
| 像素探针稳定性 | [fbo-probe] 六样 center=20 9f df 逐位恒定 [A] | Tools\fbo_run.txt:1-6 | 探针法判稳实例（§4.5） |

---

## §3 未决清单（P0-1..4 / P1-5..6 / P2-7..9）

**P0（阻断/正确性）**

- **P0-1 工作区修复态恢复（本版重写，含树取证红线）**：#1（回读+BitBlt+
  XGPU_PRESENT）**不在任何提交树与任何留档二进制中**（§〇.3/〇.4 五重
  实证）；#2 的 +65 行在 be2f24b2 树、远端线在 f6d295a1/77758746 树、
  `Tools\merge-backup\` 为 433a5818 态快照（三者可取）。**红线：禁止
  cherry-pick e366ceef**——其树相对父提交是 −4780 行的合并线回退 [A]。
  重放 #1 时勿夹带未门控 TEMP-PROBE（`[batch-flush]`/`[gpu-fallback]`/
  `[present-path]` 探针随修走、环境变量门控或移除 [A]）。
- **P0-2 上屏双通道修活与白屏/闪烁终验**：XGPU_PRESENT 当前是死开关
  （树零命中+二进制零命中+sw_baseline_recheck 惰性使用，三重独立 [A/B]）；
  恢复 #1 后须以 `XGPU_PRESENT=swap/readback` 双路抓帧对照
  （capture_frames/diff_frames 口径，caps_flick_* 三目录 60/90/60 帧方法
  在档 [A]）确认默认 readback 路径零白屏/零闪烁，并消 demo GPU 分支
  全场景重画源头（xgui_window_demo.c:762-766 接入设计稿 §4.4 demo 行）。
- **P0-3 SW 基线口径收敛（大半已闭合，转固化）**：136.7 vs 211/220 矛盾
  已由 sw_baseline_recheck 归因闭合（环境态，非代码回归）[B]；残留动作：
  ①基线协议入规约（跑前后记 load%/CurrentClockSpeed、清理遗留 probe_demo
  进程、单窗 ≥8s、同口径 3 轮取中位——recheck §5 建议 [B]）；②历史数字
  （2241/211.3/2717/252/136.7）引用时强制带「口径+环境态」标注；③基准输出
  增内建分段（爬升可见，需持构建权通道改 xgui_window_demo.c:856-875 循环）。
- **P0-4 GPU 场景保留落地 P-A**（唯一能打破零红利的路径）：按设计稿 ⑦
  分期（驱动纹理缓存→图表层接入→保留层/demo 接线）；验收锚=原语
  454→<30（15×）、整幅纹理重传 3.7MB/帧→0、sceneCache hit≈quad 数
  （设计稿 §⑥ [B]）；80FPS 旧前提作废，以 167.4 重锚基线为准 [A]；
  「反超」判定须对 SW 空闲稳态（整帧 214.6~223.4 [B]）而非 136.7。

**P1（性能大件）**

- **P1-5 Vulkan 窗口会话性能**：现状 2.0 FPS（§2.3）；按 recon P1-1/P1-2
  消 QueueWaitIdle 五处与每次 drawImage 全队列往返（勘察报告 §八 [B]），
  readback 26ms/次离屏拷回链路一并入账（vulkan_stderr [A]）。
- **P1-6 嵌套帧残余成本与帧计数语义**：prof 5s 窗 frames=12028~13184
  （flick_prof）vs 基准 167fps——`[xgpu-prof] frames` 是后端 begin/end
  计数，≠基准 fps，语义待文档化；~45 对 FBO 绑定项为保留后下一台阶
  （XPainter.c:6918 注释在档 [A]，设计稿 §⑥ 列独立项 [B]）。

**P2（卫生/观测）**

- **P2-7 XGPU_PROF 口径补全**：四原语（drawImageUv/drawGradientAlpha/
  drawAlphaBitmap/drawGlyphAlpha）无计数（勘察 #37 [B]）；sceneCache 上线
  前预留 hit/miss/upload 计数位；`frames` 语义写入文档（连 P1-6）。
- **P2-8 上传/纹理格式优化与 drawImage 身份缓存**：ARGB32 BGRA 直传消
  R/B 交换、coverage 单通道化（勘察 #5/#10 [B]）；drawImage 按图像身份缓存
  （勘察 #4 [B]，与 P0-4 驱动缓存同构可并轨）。
- **P2-9 卫生批（数字已更新）**：未跟踪文件 110 个 [A]（旧稿记 10）——
  证据 txt/log×80+、caps_* 帧抓目录×16、probe_*.exe×6、sw_baseline_recheck/、
  merge-backup/、本报告与设计稿等，需甄别入库（脚本类）/归档（测量类）/
  清理（临时类）；`XPainter.c:2389-2390` 未引用局部变量与 `:8552` 未初始化
  originX 告警（`Tools\flb.txt` [A]，行号为该构建快照）；XTouch WIP 在
  撤回态编译不过（`Tools\rb.txt`：XWidget.c:1793 `XTouchEvent_points`
  未定义等 7 error [A]），恢复远端线时连带处理；两工作区文件 BOM 被并行
  WIP 剥离（lane_ws diff [A]），提交时注意编码一致性。

---

## §4 归因方法记录（本轮沉淀）

1. **勘察→复核→落点三段式**：五路只读勘察 39 条隐患，逐条复核成立 36
   （4 条含更正）/部分成立 1/驳回 2（勘察报告 §六统计 [B]）；行号漂移强制
   「动手前重新定位」纪律（勘察卷首与附录 A.5 [B]；本报告对 gl.c/XPainter.c
   全部行号标注「@03:10」即此纪律的执行）。
2. **worktree 对照实验（归因切割）**：merged（bin）/ 合并前（bin-pm，
   独立 worktree 构建）/ 纯远端树（Container-remote-check）三二进制隔离
   变量，把「合并收益」切成远端修复线与本地 GPU 线两份（034e0617 入册
   记录已从其树恢复 [A]）；实测切面：GPU 增量 139.8→167.4（+19.7%，
   本地线贡献，§2.1 [A]）；同一方法 02:15 后被 sw_baseline_recheck 复用于
   SW 整帧矛盾（merged/premerge 差 ≤1.3% → 排除代码回归 [B]）。
3. **基准口径纪律（+环境态维度，本轮新增教训）**：`--benchmark`（增量
   repaint）与 `--benchmark-full`（整帧）分开记录；时长 5s/6s/20s 多样；
   RDP 噪声带宽 ±25~40%（g25/g26 在档 [B]）；**任何单一 FPS 数字不得脱离
   口径与环境态引用**——SW「136.7 vs 211」35% 矛盾 100% 由环境解释
   （并发帧泵进程 +2.2 核、平衡电源 CPU 双态 2000MHz↔boost，recheck §3
   四次独立复现 [B]）；SW「211.3」实为增量口径被误读整帧的活例
   （sw_stdout.txt:13 [A]）。协议：load<10%、无残留 probe_demo、单窗 ≥8s、
   3 轮中位、时钟态入元数据。
4. **剖析指纹**：`XGPU_PROF` 5s 窗聚合（原语计数/readback/present，本通道
   实读 prof20_stderr 四窗数值 [A]）+ `[gl-info]` 驱动指纹（确认真 AMD
   硬件非 llvmpipe，prof20_stderr 首行 [A]）+ 驱动内 `XGPU_PROFILE` present
   分段计时（gl.c:1391-1396/1456-1468 @03:10，每 300 帧聚合 [A]）。注意
   `XGPU_PROF` 的 `frames` 为后端 begin/end 计数，≠基准 fps（§3 P1-6）。
5. **像素探针法**：`[fbo-probe]` 关键点位多样恒定判稳定（fbo_run.txt 六样
   逐位同 [A]）；`[batch-flush]` 脏区/画布 BGRA 采样+PPM dump 定位跨帧
   残留（canvas(20,10)=红@12% 实锤 [B，034e0617 记录]）；
   `XChartView_setStaticLayerBypass` A/B 逐位 diff 作正确性门（设计稿 §2.4
   [B]）。
6. **帧抓取差分（闪烁归因）**：capture_frames.ps1 PrintWindow
   PW_RENDERFULLCONTENT 定期抓窗 + diff_frames.ps1 逐帧像素差+区域直方图
   （caps_flick_scr 90 帧/g1 60/swap 60 [A]）；`[present-path] -> GL swap`
   （flick.txt:1 [A]）证明 RDP 下 swap 链路帧不可达 → 回读+BitBlt 归因。
   该方法依赖瞬态探针构建——**探针二进制未留档是本轮取证唯一断点**（§〇.4）。
7. **三重取证法（提交/源码/二进制互证，本版新增）**：对「某开关/修复是否
   存在于某形态」用三层独立证据收口——①树取证：`git diff --stat <A> <B>`
   逐对测树差 + `git grep <pat> <tree>`（XGPU_PRESENT 四树零命中 [A]）；
   ②源码检索：findstr/rg（present 路径实读为 blit+swap [A]）；③二进制
   ASCII 串扫描（lane_env_scan.ps1 三个二进制×17 串矩阵 [A]）。本轮以此
   发现 **e366ceef 树≠提交信息** 的实质风险，并推翻「cherry-pick 恢复」
   的直觉方案。局限：无专有字符串的实现无法由③区分（§1 #1 另注）。
8. **本通道复核声明**：整理过程实跑 = `Tools\git.bat`（log/status/diff/
   show/grep）全树只读查询、findstr 源码检索、`Tools\lane_env_scan.ps1`
   二进制扫描、Tools 留档通读、两份材料文档与 XGui.md §8.0g25/g26 实读；
   **未构建、未运行任何 demo/测试/基准程序**（纪律①④；避免与并行攻坚流
   抢锁/干扰其测量，其 02:14 起构建与 01:54-02:20 基线复核期间本通道保持
   只读）。FPS 浮层 403/400、「软件增量 2717/整帧 252」两项为任务给定
   [M]，无文件留档、本通道未复验，引用须带 [M] 标注。
