# Night3 第九波·交互内容丢失组件二分取证报告

日期：2026-09-27　执行：二分定位工程师（现有 exe + 环境开关矩阵，零源码改动）
被测物：`bin\XGuiWindowDemo_Test.exe`（2026-09-27 05:25 产物，未重构建），`--gpu --page 0`。
上游证据：`docs/xgui/night3-interaction-sweep.md`（页 0 拖动 0.5–1s 内整窗大面积丢失、松开 f006/f007 不自愈）。

## 1. 结论（先说）

**锁定病灶组件：GL 渲染驱动的 PBO 异步读回路径（`XGPU_PBO_READBACK=1`，默认开）。**

唯一让拖动全程 clean 的开关是 `XGPU_PBO_READBACK=0`（PBO 关、回退同步直读本帧）：2 遍 × 拖动中 4 帧 + 松开后 1 帧，共 10 帧全部干净，FPS 面板全程可读且绘制循环活跃（拖动中 94.4–335.0 FPS）。其余 8 个开关无一达到全程 clean。

代码归属：`Drive/windows/Graphics/XGpuRenderDriver_gl.c` 的 `xgld_readback_region()` 读回统一内核（`Drive/windows/Graphics/XGpuRenderDriver_gl.c:2609`）中 allowPboLag=true 的呈现链 PBO 拷出分支（命中条件见 `:2602`–`:2607` 注释：双 PBO 槽 + fence 0 超时探测 + bbox 并回，**契约上允许吃 1 帧滞后**）。交互拖动时脏区 bbox 逐帧随光标移动，正是该滞后链路的压力态。

## 2. 方法

沿 `Tools/drag_matrix.ps1` + `Tools/sweep_drag/sweep_drag.ps1` 口径，驱动脚本 `Tools/bisect9_run.ps1`（本波新增，只测量不改码）：

1. 判崩协议：`copy bin\XGuiWindowDemo_Test.exe %TEMP%\xgui_b9_<态>_r<遍>_<guid8>.exe`，CWD=源 bin，只按自身 PID 结束，TEMP 副本用后即删。
2. 环境开关经进程环境注入（每态独立 powershell 进程，态间天然隔离）；逐一单开（取默认值以外的那个值），基线不开开关。
3. 起动 `--gpu --page 0`（整窗白最重页）等 4s 置前台 → 抓静置参考帧 `pre` → 在内容区 (400,300) 按下左键横扫 3s（sweep 同款 53/31 伪随机轨迹）→ 拖动中每 0.5s 抓 1 帧 × 4（`f000..f003`）→ 松开 +0.5s 补 1 帧（`f004`，验"松开不自愈"）。抓帧 PrintWindow(PW_RENDERFULLCONTENT)，与既有各波同口径。
4. 每态 2 遍；目检分级 clean/fragment/blank/partial（sweep §1 口径）。先看每态 3×4 对照表 `sheet.png`，所有疑似 clean 的态再逐帧全分辨率复核后才判 clean。
5. stdout/stderr 异步收进 `<态>\r<遍>_stdout.log`（防缓冲反压卡出假症状）；10 态 20 遍日志全部无输出（demo 不走 stdout），无 GL 报错证据。

## 3. 每态结果表（每态 2 遍 × 拖动中 f000–f003 / 松开 f004）

| 态 | 开关（值=单开值） | r1 分级 | r2 分级 | 松开自愈 | 判定 |
|---|---|---|---|---|---|
| base | （无，验证复现） | **blank**：f000–f003 页签行/左列/文字大面积丢失+残丝，FPS 面板噪带；f004 依旧 | **blank**：同 r1，f000–f003 全程丢 | 否（f004 同烂） | 复现 ✓ |
| nopbo | `XGPU_PBO_READBACK=0` | **clean**：10 帧全净，面板读数 105.4/335.0/139.1/99.6/105.6 | **clean**：10 帧全净，94.4/112.9/106.3/99.2/109.0 | 是（本就无恙） | **全程 clean ×2 → 锁定** |
| ko0 | `XGPU_FRAME_KEEPOPEN=0` | **fragment**：f000 子菜单行闪失+FPS 面板横噪带+就绪后残丝；f001–f003 近全；f004 面板底部噪带 | **fragment**：主体保住，面板噪带/微残丝 | 部分 | 大幅改善但不 clean |
| fullrb | `XGPU_DIRTY_READBACK=1` | **partial+fragment**：f001/f002 单个选择按钮行失，f003/f004 恢复 | **partial**：中段行闪失；f004 面板噪带 | 部分 | 闪烁型，不 clean |
| nofullbatch | `XGPU_FULLBATCH=0` | **fragment**：f000 子菜单区残丝带，余帧近全 | **fragment**：f000 顶部残丝带 | 部分 | 不 clean |
| beginclear | `XGPU_CANVAS_BEGIN_CLEAR=1` | **fragment**：f000 子菜单残丝+底部噪带；f002 行失 | **fragment/partial**：f000 残丝、f003 行失 | 部分 | 整幅清安全网无效 |
| noidlegate | `XGUI_DEMO_IDLE_GATE=0` | **fragment+partial**：页签行前段失、子菜单行失、底部粗噪带，全程 | **同 r1** | 否 | 不 clean |
| nobatchlean | `XGPU_BATCH_LEAN=0` | **fragment**：f000 残丝，中后段右中残丝带 | **fragment**：f001/f003/f004 残丝+底部噪带 | 部分 | 不 clean |
| noendlean | `XGPU_END_LEAN=0` | **fragment**：f000 残丝 | **fragment**：f000/f001 残丝+底部噪带 | 部分 | 不 clean |
| noflushlean | `XGPU_FLUSH_LEAN=0` | **fragment**：f000 子菜单残丝带 | **fragment**：f000 残丝带；f004 底部噪带 | 部分 | 不 clean |

白度快查指标（grade_blank 口径）全场 1.2–8% 无区分度——本页症状是"控件/文字消失留底色+残丝"，非纯白帧，分级以目检为准。

## 4. 判崩协议执行

10 态 × 2 遍 = 20 遍，每遍结束 `alive=true exit=running`，**无任何 0xCxxxxxxx 崩溃**；TEMP 副本每遍用后即删，进程只按自身 PID 结束。

## 5. 证据与产物

- `Tools/bisect9_run.ps1` — 二分驱动（TEMP 副本+开关注入+3s 拖动+0.5s 抓帧+对照表）
- `Tools/bisect9/<态>/sheet.png` — 每态 2 遍 × [pre+f000–f004] 3×4 对照表（10 张）
- `Tools/bisect9/<态>/r<1,2>_pre/f000..f004.png` — 全分辨率帧（nopbo 的 clean 判定逐帧全分辨率复核）
- `Tools/bisect9/<态>/r<1,2>_stdout.log` — 运行日志（全空，无报错）
- 关键单帧：`Tools/bisect9/base/r1_f002.png`（基线整窗丢失）对照 `Tools/bisect9/nopbo/r1_f000.png`（同轨迹同刻 PBO 关全程干净、面板 105.4）

## 6. 解读与移交（下一波假设，非本波结论）

- 同一确定性轨迹（53/31 步进，坐标逐帧一致）下，仅 PBO 开关翻转即从"整窗丢"变"全程 clean"，因果为开关本身，非轨迹/时机抖动。
- `XGPU_FRAME_KEEPOPEN=0`（帧不复用）大幅收窄症状但不消除（面板噪带、行闪失仍在）——帧复用会放大陈旧帧暴露面，但根因在 PBO 读回链；两开关同属读回/上屏路径，与锁定结论一致。
- `XGPU_DIRTY_READBACK=1`（退全帧读回）只除脏区、不除面板噪带，说明症状不全来自脏区 bbox 分辨，PBO 滞后拷出本身即可产出陈旧/半成品帧。
- 下一波可在 PBO 链内部细分：fence 0 超时探测通过但 GPU 实际未完成、bbox 并回条件在移动脏区下的误命中、以及 `XGPU_PBO_READBACK_FULL=1`（诊断开关，`XGpuRenderDriver_gl.c:2465`）下全帧也走 PBO 的对照复测。
