# night-3 改前基线（GPU 直通 vs 软件渲染，全矩阵实测）

日期：2026-09-25 23:47–23:58（夜）
协议脚本：`Tools/night3_bench.ps1`（本次新增并固化，终测直接复跑）
原始日志：`Tools/bench-night3-logs/*.log`（32 份，16 格 × 2 遍）
闪烁帧图：`Tools/caps-night3/f000.png..f029.png`
汇总输出：`Tools/bench-night3-results.txt`

## 1. 协议（冻结口径，终测同此复跑）

- 被测程序：`bin/XGuiWindowDemo_Test.exe`（2026-09-25 23:41:57 构建）。
- 锁定防护沿 night-2：复制到 `%TEMP%\xgui_bench3.exe` 运行（CWD=源 bin 解析
  DLL/assets），构建目录 exe 永不被锁；每遍前后 `taskkill` 清残留。
- 矩阵 = 后端 {`--software` 显式软件，`--gpu`} × 页面 {0, 2, 4, 6} ×
  口径 {增量 `--benchmark 5`，整帧 `--benchmark-full --benchmark 5`} = 16 格；
  每格 2 遍取第 2 遍（预热口径，沿 night-2）。
- 命令形如：`XGuiWindowDemo_Test.exe --gpu --benchmark 5 --page 4`；
  整帧口径为 `--benchmark-full --benchmark 5 --page P`。
  依据 `Test/XGuiDemo/xgui_window_demo.c:2957-2970`：`--benchmark N` 定时长，
  `--benchmark-full` 是不带参数的整帧开关，二者须连用。
- SW 口径用显式 `--software`（`:2980-2986`，`--gpu` 的对称反向开关）而非
  裸启动：不依赖桌面默认态（`XGPU_RUNTIME_DEFAULT_ON`）。实证冒烟：裸启动
  `--benchmark 3 --page 0` 得 fps=7183.7 且无 `[gl-info]`/`[xgpu-prof]` 行
  （GPU 行为带为 40–68 fps），确认裸启动本机当前即软件后端。
- 后端取证：`XGPU_PROF=1` 下 `--gpu` 打印 `[gl-info] renderer=AMD Radeon (TM)
  Graphics` + `[xgpu-prof] driver=opengl window=1`（矩阵日志未设该 env，
  取证为矩阵外单跑 `--gpu --benchmark 3 --page 4`：driver=opengl，fps=66.0，
  落在矩阵 GPU 带内 → GPU 路径真实激活，非软件回退）。

## 2. 环境注记（运行前后实查）

- `quser`（23:39 实查）：用户 jxy，**console 会话，运行中**，登录 2026/9/25
  23:37 —— 与 night-2（会话断开、空闲 1:32）不同，本夜为控制台活动会话；
  但显示适配器仍为 **OrayIddDriver Device（虚拟显示 17.1.58.818）+ AMD
  Radeon (TM) Graphics（APU 核显 31.0.12027.9001）**，远程显示栈在位，
  与"RDP/虚拟显示态"注记一致。
- 无残留 demo/test/xgui 进程（tasklist 实查）；协议自身每遍前后 taskkill。
- GPU 四格 fps 40.1–67.9，紧贴 60Hz 呈现限频带（`XGPU_PRESENT_MAX_FPS`
  止血后的预期钳制），页 6 略低（40.1–43.7）。

## 3. 基线表（800×600 窗口，5s 栅 ×2 取第 2 遍；GPU/SW 比值 <1 = GPU 落后）

| 口径 | 页面 | SW fps | GPU fps | GPU/SW 比值 |
|---|---|---|---|---|
| 增量 `--benchmark` | page 0 | 8788.8 | 63.5 | 0.007 |
| 整帧 `--benchmark-full` | page 0 | 1080.6 | 65.6 | 0.061 |
| 增量 | page 2 | 8846.2 | 67.9 | 0.008 |
| 整帧 | page 2 | 948.5 | 62.5 | 0.066 |
| 增量 | page 4 | 7746.9 | 65.3 | 0.008 |
| 整帧 | page 4 | 621.1 | 66.3 | 0.107 |
| 增量 | page 6 | 6259.4 | 43.7 | 0.007 |
| 整帧 | page 6 | 630.0 | 40.1 | 0.064 |

结论：**GPU 当前全面落后 SW**——增量口径落后 93–149 倍（比值 0.007–0.008），
整帧口径落后 9–17 倍（比值 0.061–0.107）；16 格中 GPU 无一格反超。
GPU 各格被呈现限频钳在 40–68 fps 一带，故反超的关键在减负至限频带以上后
仍能把帧成本压过 SW 的脏区 BitBlt 路径——终测判据：同协议下任一格
GPU/SW 比值 ≥1（fps 数字为准）。

## 4. 闪烁量化（--gpu，page 4，普通模式非基准）

- 方法：`--gpu --page 4` 普通启动（pid 7260），`Tools/capture_frames.ps1`
  PrintWindow(PW_RENDERFULLCONTENT) 抓 30 帧（窗口 816×639，间隔 120ms），
  `Tools/diff_frames.ps1` 每 3 像素采样差分（阈值 d>30）。
- 结果：29 个帧对中 **changed>500 的帧对 = 8 个**（全窗级翻动指标），
  峰值 changed=3826（≈6.6% 采样网格）。
  翻动帧对：2→3(3798)、3→4(3726)、5→6(3807)、6→7(3726)、19→20(3821)、
  20→21(3826)、22→23(3784)、23→24(3726)；其余帧对 0–419。
- 分布：翻动集中在底部条带（y-cell 6：x=1,2,4,6,7 各 ~300+ 采样点）成簇
  突发、间隔出现 —— 普通闲置模式下仍有整片区域级翻动，与"GPU 呈现路径在
  远程显示栈上闪烁"的定性一致；本数值即改前基线，终测同法复测对比。

## 5. 诚实性声明

- 表中 16 格数值与闪烁计数全部出自本次实跑（命令：
  `powershell -NoProfile -ExecutionPolicy Bypass -File Tools\night3_bench.ps1`，
  输出见 `Tools/bench-night3-results.txt`）。
- 矩阵日志内 `backend=sw-or-fallback` 字样系脚本取证位：矩阵运行未设
  `XGPU_PROF=1`，故无 driver 行可解析；GPU 真实激活由矩阵外单跑取证
  （§1 末条）+ GPU 格 fps 与 SW 格相差两个数量级的行为差异双重佐证。
- night-2 的 20s 栅/`--tab 20` 口径与本夜 5s 栅/缺省 tab 口径不可直接
  对比；本表即 night-3 基线，终测须用同一脚本复跑。
- `sw_full_p4`（847.1→621.1，−27%）与 `sw_full_p6`（465.4→630.0，+35%）
  两遍波动较大，按既定口径取第 2 遍，未做平滑。
