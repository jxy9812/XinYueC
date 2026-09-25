# XGui 基准协议固化与 night-2 基线采集（bench-night2-table）

日期：2026-09-25 04:12–04:18（本机时段：后半夜）
协议脚本：`Tools/bench_protocol.bat`（本次新增并冻结）
原始日志：`Tools/bench-night2-logs/*.log`（16 份，8 口径 × 2 遍）
汇总输出：`Tools/bench-night2-results.txt`

## 1. 协议（冻结口径）

- 被测程序：`XGuiWindowDemo_Test.exe`。协议规范源 = `build-night/bin/`（汇合统一构建后生效）；
  本夜 `build-night/` 尚未生成，脚本自动回退 `bin/XGuiWindowDemo_Test.exe`（2026-09-25 03:44 构建，
  含 P0-1..P0-4 修复；脚本已打印 WARN，见 results.txt 头部）。
- 锁定防护：一律复制到 `%TEMP%\xgui_bench.exe` 运行（CWD=源 bin 目录解析 DLL/assets），
  构建目录 exe 永不被锁；结束与重跑前均 `del` 副本。
- 残留清理：每遍运行前后 `taskkill /F /IM xgui_bench.exe`（起始还清 `XGuiWindowDemo_Test.exe`）。
- 矩阵 = 后端 {`--gpu`，默认软件} × 口径 {`--benchmark-full` 整帧，增量（缺省脏区）} × 页面
  {`--page 4 --tab 20` 图表页，`--page 2` 堆叠演示页} = 8 格；每格跑 2 遍取第 2 遍；
  每遍 `--benchmark 20`（20 秒栅，与 167.4 重锚基线同长，prof20 口径）。
- 后端取证：`XGPU_PROF=1`（`Src/XGui/Graphics/XGpuRenderBackend.c:31-42` 环境开关，`:94` 打印
  `[xgpu-prof] driver=...`）。本次全部 `--gpu` 格均出现 `driver=opengl window=1`，且
  `[gl-info] renderer=AMD Radeon (TM) Graphics version=4.6.0 Compatibility Profile Context` ——
  GPU 路径真实激活，非软件回退。
- CLI 依据：`Test/XGuiDemo/xgui_window_demo.c:2947-2966`（`--benchmark-full`/`--gpu`/`--page`/`--tab`），
  FPS 行格式 `:881`（`benchmark mode=... fps=%.1f`）。图表页=tab 20 依据：`:2810-2812`
  （`XTabWidget_insertTab_2(..., 20, m_chartView, "图表")`）；"图表页 --page 4 --tab 20" 亦为
  `docs/xgui/gpu-night2-report.md` §2 的既定称呼。

## 2. 环境状态（运行前 tasklist 实查）

- `quser`：用户 jxy，会话 2，状态**断开**，空闲 1:32，登录于 2026/9/24 17:59 —— 显示栈处于
  断连状态（OrayIddDriver 虚拟显示 + RDP 传输路径），与既知"后半夜 64–71 FPS 环境性天花板"
  成因一致（kill-switch 位等价对照已证明非代码，见主工作流交接）。
- `tasklist /v`：无任何残留 demo/test/xgui 进程；仅 dwm（Console + 会话 2 双实例）与
  VS ServiceHub.IntellicodeModelService 等后台服务。
- 显示适配器：OrayIddDriver Device（虚拟显示，17.1.58.818）+ AMD Radeon (TM) Graphics
  （APU 核显，驱动 31.0.12027.9001），状态均 OK。
- 本夜天花板表现：图表页两格 GPU 均被压在 68.3–70.7 FPS（落在 64–71 带内）；
  注意 page 2 的 GPU 格达 102–103 FPS，**高于**该带 —— 本夜的天花板绑定在图表页 present
  路径上，而非整机全局 present 限速。此差异如实记录，供明日复测核对。

## 3. 基线表（800×600，20s 栅，2 遍取第 2 遍；括号=第 1 遍）

| 口径 | 页面 | GPU（OpenGL 直通） | 软件路径 | GPU/SW 比值 |
|---|---|---|---|---|
| 整帧 `--benchmark-full` | 图表页 `--page 4 --tab 20` | **70.7 FPS**（69.2）| **251.9 FPS**（288.3）| 0.281 |
| 增量（脏区，缺省） | 图表页 `--page 4 --tab 20` | **68.3 FPS**（68.7）| **3236.9 FPS**（3211.6）| 0.021 |
| 整帧 `--benchmark-full` | 堆叠演示 `--page 2` | **102.1 FPS**（103.6）| **1430.5 FPS**（1259.3）| 0.071 |
| 增量（脏区，缺省） | 堆叠演示 `--page 2` | **103.5 FPS**（105.8）| **13146.8 FPS**（13016.9）| 0.008 |

图表页增量（GPU，锚点口径续列）：**68.3 FPS** —— 被本夜 64–71 环境带封顶，与
P0 系列锚点（167.4 → 183.6 → 198.7–206.0 → 181–193，环境带未出现时段所测）**不可直接对比**。

观测备注：
- GPU 四格 take1≈take2（±2%，被天花板钳住故稳）；SW 两格波动较大（sw_full 图表页 288.3→251.9，
  −13%），协议取第 2 遍即为既定口径，未再做平滑。
- SW 整帧图表页 251.9 FPS 与在档历史"同口径软件 255 FPS"（XGui.md 口径）一致；
  SW 增量图表页 3236.9 与历史 2424.7/2544.7（3s/4s 栅）同量级。
- 增量口径的绝对值依赖脏区大小（图表页 tab 20 有图表重绘脏区，故 SW 增量 3236 而非
  tab 0 的 ~6500 量级，见 Tools/pm_inc_tab0.txt 历史样例）。

## 4. 与 P0 锚点的关系

本夜数值统一记录为 night-2 环境受制基线：图表页 GPU 两格 ~68–71 FPS 证明天花板仍在，
代码侧 P0-1..P0-4 的收益（增量口径 181–206 区间）需在天花板解除时段（白天/会话连接态）
用本协议重跑验证；协议、日志与汇总已固化，复测只需重跑 `Tools/bench_protocol.bat`。

## 5. 诚实性声明

- 表中全部数值出自本夜实跑（命令：`Tools\bench_protocol.bat`，BENCH_PROTOCOL_SECONDS 缺省 20）。
- `build-night/bin/XGuiWindowDemo_Test.exe` 不存在（未构建），实跑源为 `bin/` 回退 exe；
  该事实由脚本 WARN 与 results.txt 头部记录，非臆测。
- "64–71 环境带非代码"结论引自主工作流既有的 kill-switch 位等价对照，本夜未重复该对照实验。
