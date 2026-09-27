# night-3 第二波终测（GPU 直通 vs 软件渲染：增量三态对照 + 慢页 page 5 + 空闲 CPU + PBO/慢页修复落地复测）

日期：2026-09-26 02:08–02:2x（夜）
协议脚本：`Tools/night3_bench.ps1`（第二波口径：对第一波加厚三态增量对照、
page 5 入矩阵、空闲 CPU 口径；历史格名与第一波兼容可纵比）
被测程序：`bin/XGuiWindowDemo_Test.exe`（**2026-09-26 02:08:13 构建**，含第二波
四道源码改动：G/PBO 异步读回 `Drive/windows/Graphics/XGpuRenderDriver_gl.c`
（mtime 02:03）、I/会话创建重试 `Src/XGui/Graphics/XGpuRenderBackend.c`（02:03）、
H/page5 快照路径修复 `Test/XGuiDemo/xgui_demo_page_views.c`（01:38）+ 图例瓦片
`Src/XGui/Charts/XChartView.c/.h`（01:41/01:44）；二进制含 `gl-pbo` /
`XGPU_PBO_READBACK` / `XGUI_DEMO_VIEWS_SNAPSHOT` / `XGPU_SESSION_RETRY` 字串，
`findstr /m` 实测）。F 归因道零代码（`docs/xgui/night3-attribution.md`）。
参照：`docs/xgui/night3-final.md`（第一波终测）、`docs/xgui/night3-wave2-plan.md`
（协议与判据，预演 01:18 于 00:40 exe）、`docs/xgui/night3-attribution.md`。
原始产物：`Tools/bench-night3-results.txt`（40 格×2 遍=80 跑汇总，失败清单空）、
`Tools/bench-night3-logs/*.log`（80 份，含 `env:` 取证行；抽查 gpu_incr_p4 三态
def/dr1/pbo0 日志互斥正确）、`Tools/caps-night3/f*.png`（闪烁帧）。

## 1. 协议（第二波口径）

- 矩阵 = 后端 {`--software`, `--gpu`} × 页面 {0,2,4,5,6}（5=条目视图慢页）
  × 口径 {增量三态 def / dr1(`XGPU_DIRTY_READBACK=1` 回退全窗读回) /
  pbo0(`XGPU_PBO_READBACK=0` 关 PBO 整链回退同步读回——本波起该开关已落地，
  不再是 plan 预演时的空操作) + 整帧 def 单态} = 40 格 × 2 遍取第 2 遍
  （5s 栅），exe 复制 `%TEMP%\xgui_bench3.exe` 运行、每遍前后 taskkill。
- 每态子进程显式净化环境（def 移除两键、单态键互斥），日志写 `env:` 取证行。
- 空闲 CPU：`--gpu` 普通启动落窗稳定 3s 后 TotalProcessorTime 差分采 5s，
  load=ΔCpu/ΔWall×100（单核口径，可 >100%），两态 {gate-def,
  gate0(`XGUI_DEMO_IDLE_GATE=0` 回退帧泵口径)}。
- 闪烁：`--gpu --page 4` 普通启动抓 30 帧 + 每 3 像素采样差分，阈值 changed>500。
- 命令：`powershell -NoProfile -ExecutionPolicy Bypass -File Tools\night3_bench.ps1`。
- 环境实查：`quser` = jxy / **console 会话运行中**（登录 9/25 23:37，与前两波
  同会话）；本次汇编复核时 tasklist 无 xgui/bench 残留进程。

## 2. 终测矩阵（800×600，5s 栅 ×2 取第 2 遍；括号内为第一波终测值）

| 口径 | 页面 | SW fps | GPU fps | GPU/SW 比值 | GPU 第一波 | GPU 提升 |
|---|---|---|---|---|---|---|
| 增量 `--benchmark` | page 0 | 10831 (10924.5) | 787.4 (737.1) | 0.073 | 0.067 | ×1.068 |
| 整帧 `--benchmark-full` | page 0 | 1079.8 (1101.2) | 75.9 (59.7) | 0.070 | 0.054 | **×1.27** |
| 增量 | page 2 | 10966 (10898.0) | 780.3 (742.6) | 0.071 | 0.068 | ×1.051 |
| 整帧 | page 2 | 1241.6 (1158.7) | 79.1 (66.0) | 0.064 | 0.057 | **×1.20** |
| 增量 | page 4 | 9299.3 (9433.2) | 543.7 (517.1) | 0.058 | 0.055 | ×1.051 |
| 整帧 | page 4 | 1031.9 (1000.0) | 79.4 (64.7) | 0.077 | 0.065 | **×1.23** |
| 增量 | page 5 | 8085.3（新增格） | 491.8（新增格） | 0.061 | — | — |
| 整帧 | page 5 | 763.9（新增格） | 52.7（新增格） | 0.069 | — | — |
| 增量 | page 6 | 6978.3 (7101.6) | 454.5 (433.2) | 0.065 | 0.061 | ×1.049 |
| 整帧 | page 6 | 431.8 (418.4) | 41.3 (38.1) | 0.096 | 0.091 | ×1.08 |

判读：

- **gpuBeatsSw = 否：10 格 GPU/SW 全部 <1（0/10）**。增量口径差距
  **13.7–17.1×**（第一波 14.7–18.2×），整帧口径 **10.5–15.7×**（第一波
  11.0–18.4×）——同协议微收敛。最接近格：整帧 page 6 比值 0.096
  （41.3 vs 431.8）；增量最佳 page 0 比值 0.073（787.4 vs 10831）。
- SW 侧两波持平（增量 −1.7%~+0.6%，整帧 −1.9%~+7.2%）→ 本波 GPU 提升是
  真实改善，非分母效应（与第一波「SW +13–24% 混入」相反）。
- **整帧格脱离第一波 60Hz 限频带**（59.7–66.0 → 75.9–79.4，page 6
  38.1→41.3），+8.4%~+27%。归因为推断（整帧格协议单态无 pbo0 对照，
  排除法：本波四道中只有 G/PBO 触碰读回路径，整帧=全窗失效下 readbackRect
  退化为全帧读回、同步 glReadPixels 正是其大头；H 只影响 page 5 与默认关的
  图表静态层，I 只作用于会话创建失败路径）。定谳办法：整帧格加跑 pbo0 态。
- **page 5 慢页大幅缓解**：增量 168.9 fps（H 于 00:40 exe 实测，
  `--benchmark 4` 口径）→ **491.8 fps（×2.9）**，longest 152ms→26.0ms
  （离屏会话建立尖峰消失，与 H 移除 views_paintOffsetSafe 离屏快照路径的
  机制吻合）；预演（00:40 exe，2s 缩尺）整帧 4.5 fps → 终测 52.7 fps。
  但未回到 p0/p2 同级（H 预期 ~1.4ms/帧，实际 2.03ms/帧），残余 ~+0.75ms/帧
  与归因道「多余派发源未定位」疑题一致，p5 仍是全矩阵最重页。
- 增量四页 +4.9%~+6.8% 中，开关差实测只有 +1.7%~+3.6%（§3 PBO 列），
  其余 +1.3%~+3.5% 为两波间非 PBO 漂移（旁证：pbo0 态纵比第一波默认态
  +1.3%~+3.4%），未单独归因。

## 3. 增量口径三态对照（def / dr1 / pbo0，各 2 遍取第 2 遍）

| 后端 | 页面 | def | dr1 | pbo0 | 脏区读回收益 (def vs dr1) | PBO 收益 (def vs pbo0) |
|---|---|---|---|---|---|---|
| sw | page 0 | 10831 | 10860 | 10799.6 | -0.3% | +0.3% |
| sw | page 2 | 10966 | 10801.9 | 10789 | +1.5% | +1.6% |
| sw | page 4 | 9299.3 | 9305.7 | 9296.5 | -0.1% | 0% |
| sw | page 5 | 8085.3 | 8180.7 | 8196.9 | -1.2% | -1.4% |
| sw | page 6 | 6978.3 | 7009.8 | 7018.9 | -0.4% | -0.6% |
| gpu | page 0 | 787.4 | 682.6 | 762.1 | **+15.4%** | +3.3% |
| gpu | page 2 | 780.3 | 686.1 | 767.2 | **+13.7%** | +1.7% |
| gpu | page 4 | 543.7 | 477.3 | 526.1 | **+13.9%** | +3.3% |
| gpu | page 5 | 491.8 | 427.4 | 479.6 | **+15.1%** | +2.5% |
| gpu | page 6 | 454.5 | 393.7 | 438.7 | **+15.4%** | +3.6% |

判读：

- **脏区读回收益（判据 ≥+8%）：GPU 五页 +13.7%~+15.4% 全过**，第一波
  +10.8–13.2% 复现且更稳（5s×2 口径压噪）；SW 同列 −1.2%~+1.5%，判据
  |Δ|≤3% 过——天关对照干净，开关不触碰软件路径。dr1 拉开的帧成本
  +0.195~+0.256ms/帧（p0 1.270→1.465ms、p4 1.839→2.095ms），与归因道
  读回面积摊销模型 +0.18ms/迭代同量级。
- **PBO 收益（判据 ≥+5% 计生效）：GPU 五页 +1.7%~+3.6%（5/5 正向，均值
  ≈+2.9%），未达判据，本波不计生效**。机制本身经 §5 取证健康（命中 76%、
  stall=0、mapfail=0）——收益小与归因道「读回仅占增量帧成本 ~5.7%」一致，
  池子只有这么大；PBO 排在派发数杠杆之后是归因道既定排序，未被推翻。
  注意 SW 侧 pbo0 列 ±1.6% 漂移同源存在，GPU +1.7~3.6% 中未单独剥离漂移
  成分（方向五页一致为主要佐证）。

## 4. 闪烁量化复测（--gpu，page 4，普通模式非基准，30 帧）

| 指标 | 第一波终测（00:42） | 第二波终测（02:08 构建） |
|---|---|---|
| changed>500 帧对 | **0 / 29** | **0 / 29** |
| 峰值 changed | 0 | **0**（29 对全部 changed=0） |

闪烁根治保持：连续两波全零（基线 8/29、峰值 3826 → 两波 0/29、峰值 0）。

## 5. GPU/PBO 激活取证（矩阵外单跑，沿 night3-final §5 惯例；本次汇编实跑）

命令：taskkill 清残留 → `copy /y bin\XGuiWindowDemo_Test.exe %TEMP%` →
CWD=bin 执行 `set XGPU_PROF=1 && set XGPU_PBO_STATS=1 &&
%TEMP%\xgui_bench3.exe --gpu --benchmark 8 --page 4`，输出原文：

```
[gl-info] vendor=ATI Technologies Inc. renderer=AMD Radeon (TM) Graphics version=4.6.0 Compatibility Profile Context 22.20.27.09.230330
[xgpu-prof] driver=opengl window=1
[gl-pbo] calls=300 pbo=228 seed=46 stall=0 mapfail=0
[xgpu-prof] 5.0s frames=8855 readback=301 (0.561ms/次) drawImage=0 (0.000ms/次) fillRect=29526 solidQuad=8930 present=0 (0.000ms/次)
XGuiWindowDemo: benchmark mode=repaint size=800x600 frames=4414 elapsed=8.001s fps=551.7 avg=1.813ms longest=19.935ms
```

判读：

- driver=opengl 且 fps=551.7 落终测 GPU 增量带内 → **GPU 路径真实激活**。
  矩阵行尾 `backend=sw-or-fallback` 系脚本取证位（矩阵未设 XGPU_PROF，
  同第一波 night3-final §7 口径），非回退。
- **`[gl-pbo] calls=300 pbo=228 seed=46 stall=0 mapfail=0`：G 道 PBO 双缓冲
  轮转在终测构建真实生效**——76% 读回命中 1 帧滞后异步路径、46 次供链
  （种子/回退帧保温）、零 fence 停等、零 map 失败。pbo0 态即关闭此路径，
  故 §3 的 +1.7%~3.6% 是真实开关差而非空操作噪声（该「空操作」标注只适用
  于 plan 预演的 00:40 exe）。
- readback=301 次 / 5.0s ≈ 60 次/s → 读回按呈现节拍（60Hz 限频）摊销，与
  归因道一致；prof 窗口 frames=8855 ÷ 同期基准帧数（≈5.0s×551.7≈2759，
  按 fps 均速估算）≈ **3.2 派发/帧**，与归因道 p4 模型值 3.26 派发/迭代
  吻合——「GPU 增量帧成本 ∝ 派发数×每派发固定成本」模型的派发计数在本
  构建交叉验证成立。

## 6. 空闲 CPU（--gpu 普通启动闲置，TotalProcessorTime 法，单核口径）

| 态 | cpu 秒 | 墙钟秒 | load |
|---|---|---|---|
| gate-def | 3.516 | 5.012 | 70.1% |
| gate0 | 10.25 | 5.014 | 204.4% |

判读：**未达 plan §4.4「gate-def ≤5%（近零）」判据**——预演 64–68% →
终测 70.1%，三次同法采样稳定不达标，第一波 C 路空闲治理欠账未清，如实
入档。残留负载定位（候选：呈现链 60Hz 空转、事件循环轮询、残留定时器）
需改 Src/Test 代码，超出本波 Tools/docs 与各实现道权限，移交下一波。
gate0 ~204% 与第一波 163% 同量级，闸门方向收益（~×2.9 降幅）仍在。

## 7. t218c 图例瓦片回归复跑（H 道遗留验证；本次汇编实跑）

命令：taskkill 清残留 → CWD=bin 执行
`set XGUI_CHART_STATIC_CACHE=1 && bin\XGuiRegression_Test.exe`（SW 口径）。

- **t218c 通过**：全轮无 `->FAIL: t218` 行、无 `t218c: mismatch=` 打印
  （断言点 `xgui_regression_test.c:32347`「静态层开/关逐位一致」通过）——
  00:40 exe 上实测 FAIL（mismatch=12 pixels）在 H 道修复（瓦片顶侧
  XCV_LEGEND_TILE_TOP_SLACK=16 + 半透明图例整体回退直画 gate）落地后消失，
  「待统一构建复跑」裁决完成。
- 同轮 `->FAIL` 恰两条：`XLineEdit 控件功能`（2 断言，即 setParentPlain
  断言组 `xgui_regression_test.c:22217-22231`——H 已判定的既有失败，与本波
  改动面无交集）；`多点触摸 per-id 路由与生命周期（XI2-B B1）`
  （`xgui_regression_test.c:31316`）——**是否既有无法定谳**（00:40 exe 已被
  02:08 构建覆盖，无对照物；本波四道均不触碰触摸/事件路由代码面，初步判断
  与本波无关），留验证道复核。汇总行照旧输出 `XGui regression tests passed`
  （与 ->FAIL 并存为该测试架既有形态）。

## 8. 第二波四道落地清单与回退开关

| 道 | 落地物 | 终测状态 | 回退开关 |
|---|---|---|---|
| F 归因 | `docs/xgui/night3-attribution.md`（零代码） | 核心结论成立并经 §5 交叉验证：GPU 增量帧成本 91–94% = 每派发会话级固定成本 ~0.59ms × 每迭代 2.18–3.62 次派发（线性模型残差 <3%）；读回仅 5.7%；上传/冲批/scissor≈0。遗留：基准循环只提交 1 次 demo_repaint 但实测 2.18+ 派发，多余派发源未定位——下一波首探针 | 无（文档） |
| G PBO 异步读回 | `Drive/windows/Graphics/XGpuRenderDriver_gl.c`：双 PBO 轮转 + fence 探测 + 读回统一内核（:242-257/:1506-1547/:1640-1657/:1888-1940/:1988-2100） | 已入 02:08 构建并实测（§3/§5）：机制健康（命中 76%、stall=0）、增量收益 +1.7~3.6% 未达 +5% 判据（池子小，符合归因）；整帧 +8.4~27% 推断主因（§2） | `XGPU_PBO_READBACK=0`（逐位旧行为）；诊断 `XGPU_PBO_STATS=1` |
| H page5 快照路径 + t218c 瓦片 | `xgui_demo_page_views.c`（非零偏移默认直派基类，旧快照需 `XGUI_DEMO_VIEWS_SNAPSHOT=1` 重编回退）+ `XChartView.c/.h`（TOP_SLACK=16 + `xcv_legendTileContentOpaque()`，仅 `XGUI_CHART_STATIC_CACHE=1` 运行域，默认关零影响） | page5 增量 ×2.9、longest 152→26ms（§2）；t218c 复跑通过（§7）。page5 画面目检与三态复测仍属首次构建后待办 | `XGUI_CHART_STATIC_CACHE=0`（默认）整体停用瓦片域；快照回退需重编 |
| I 会话创建瞬时失败重试 | `Src/XGui/Graphics/XGpuRenderBackend.c`（:306-368/:443/:667-685/:751-789）：`XGPU_SESSION_RETRY` 默认开，50ms 内联 + 500ms 冷却 + 3 次预算，probe 不再永久锁存 | 已入构建（二进制含开关串）；本机 14 种强杀序列全 exit 0 未能复现原始失败——修复有效性仅代码审查背书，参数待真实失败样本校准 | `XGPU_SESSION_RETRY=0`（与改动前行为逐字节一致） |
| J 协议 | `Tools/night3_bench.ps1` 第二波口径 + `docs/xgui/night3-wave2-plan.md` | 80 跑全采、失败清单空；预演格式校验 13/13；`env:` 取证行抽查互斥正确 | 脚本 `-SkipIdle` 即回第一波矩阵语义；产物均独立路径未覆盖前波 |

（I 道交接，B 路归因矛盾）`XGuiApplication_create_ex` 返回 NULL 不经任何
GPU 代码（`Src/XGui/Application/XGuiApplication.c:276-297`）；`taskkill /f`
本身把被杀进程退出码置 1（**假 code=1 陷阱**）；`DemoWin_create` 失败路径缺
return（`Test/XGuiDemo/xgui_window_demo.c:3145-3148`，真触发是 0xC0000005）
——再查 B 路先索原始 stdout/stderr，勿以杀进程取码。

## 9. 结论

1. **GPU 反超 SW：仍未达成（0/10 格 ≥1）**。两波合并轨迹：增量 GPU
   63.5（基线）→433–743（第一波）→454–787 fps（本波），GPU/SW 比值
   0.007→0.055–0.068→0.058–0.073，差距 93–149×→14.7–18.2×→**13.7–17.1×**；
   整帧差距 11.0–18.4×→**10.5–15.7×**。
2. 本波三个已证结论：**脏区读回收益 +13.7~15.4% 稳定复现**（判据过、天关
   对照干净）；**page5 慢页 ×2.9 缓解 + t218c 修复通过复跑**；**PBO 机制
   健康但增量池子小**（+1.7~3.6% 未计生效，与归因道「读回仅 5.7%」互洽），
   整帧带改善 +8.4~27%（推断主因，待整帧 pbo0 对照定谳）。
3. 未达标两项如实入档：主判据 GPU/SW≥1 为 0 格；空闲 gate-def 70.1%
   （≤5% 判据未达，欠账未清）。
4. 下一波杠杆排序（沿归因道，只认数字）：①定位多余派发源 + 每派发固定
   成本压减（91–94% 池子；归因道估两步兑现 p0 ~2700 fps，差距 15×→~4×）；
   ②空闲 70% 残留负载定位（独立于帧率判据的欠账）；③整帧格加 pbo0 对照态
   （协议小改，定谳 §2 整帧归因）；④PBO 深化仅在①后重估。另：归因道移交
   的 `XGPU_QUAD_BATCH=0`/`XGPU_SCISSOR_CACHE=0` 在 page 0 疑似崩溃
   （173 字节日志）待修复道优先核实，影响后续一切用此二开关的对照。

## 10. 诚实性声明

- 终测数值全部引自 `Tools/bench-night3-results.txt`（2026-09-26 02:08:24 起，
  80 跑、失败清单空）；第一波括号值引自 `docs/xgui/night3-final.md` §2
  （同脚本同格可纵比；页面集合 4→5 页，p5 为新增格无纵比值）。
- §5 与 §7 两条命令为本次汇编实跑，输出为原文引用；跑前 taskkill 清残留、
  跑后 tasklist 复核无残留，%TEMP% 副本已删；未跑构建、未 git、除本报告
  文件外未改任何仓库文件。
- 归因推断均带标签：整帧 +8.4~27% →PBO 为排除法（整帧格无开关对照）；
  page5 ×2.9 →H 修复为机制吻合+排除法，且 H 旧值 168.9 为 `--benchmark 4`
  单遍口径、预演 4.5 fps 为 2s 缩尺口径，与终测 5s×2 不同口径，只在量级
  上可比；§5 派发数 3.2/帧 的分母为按 fps 均速估算，非直接计数。
- 增量四页两波纵比 +4.9~6.8% ≠ PBO 开关差 +1.7~3.6%，差额 +1.3~3.5% 为
  两波间未归因漂移（两构建差四道改动+测量时刻不同），未单独归因。
- pbo0 收益列含漂移成分（SW 侧同列 −1.4~+1.6% 同源存在）；`sw_full_p4`
  两遍差 7.2%（1105.8→1031.9），整帧格 SW 波动大于增量格，整帧判读按格间
  带而非单格绝对值。
- 环境同前两波：jxy/console 会话（9/25 23:37 登录）、OrayIdd 虚拟显示 +
  AMD Radeon iGPU（GL 4.6.0 Compatibility 22.20.27）远程栈；每派发固定成本
  等绝对数值外推到物理显示/其他驱动栈可能不同，杠杆排序（成本∝派发数）
  预计仍成立。
- `XGPU_PBO_READBACK` 在 00:40 exe 无定义（plan 预演 pbo0 为空操作）、在
  02:08 exe 有定义（`findstr /m` 二进制实测）——plan §5.3/§6 的「pbo0=噪声」
  标注只适用于预演，本文终测口径已按真实开关态改写。
