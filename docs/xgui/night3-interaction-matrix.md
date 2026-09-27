# night-3 第七波 J 道：交互质量矩阵——当前 exe（未含 A 路字形修复）交互态损坏特征图谱

日期：2026-09-27 02:2x–03:1x（夜）
被测程序：`bin/XGuiWindowDemo_Test.exe`（**2026-09-27 01:14:19 构建**，第七波
GPU 增量 p0 4750 FPS 那份二进制；`findstr /m XGPU_GLYPH_HASH` 实测含该开关串）。
A 路字形修复**不在其中**：`Src/XGui/Graphics/XPainter.c` mtime 2026-09-27
01:57:37 晚于 exe 43 分钟（八波「六波默认关→八波修复默认开」块注
`XPainter.c:10957` 已在源树、未入此构建），与 ask「未含 A 路字形修复」口径一致。
本道性质：**全测量、零源码**——未改任何源码/构建/仓库文件（本文档与
`Tools/matrix/` 截图及脚本除外）；无 git、无构建；exe 一律 %TEMP% 改名副本运行；
TEMP 副本用后即删（§8）。

## 0. 结论速览

1. **GPU 交互态灾难性损坏实证（本道核心产出）**：连续按住拖动开始后
   **0.8 秒内整窗内容丢失**——导航排、页内控件、状态栏、应用标题文本全部
   消失，只剩空蓝条 + 拖动横带上的控件碎片 + 右下 FPS 叠层被涂抹成黑色
   横向条带。三页（0/2/4）无一幸免。GPU 臂 27 帧分级：**clean 9 /
   text-noise 3 / blank 15**。
2. **SW 对照 27/27 全 clean**（符合 ask 预期）：同坐标同步骤同节奏，
   SW 臂每一帧导航排/控件/状态栏/叠层文字完整可读，拖动悬停高亮都正常
   出画（图 `sheet_sw_p*.png`）。损坏只在 GPU 后端发生。
3. **双通道仲裁：损坏是真实出画，非抓取伪影**。拖动中同一时刻
   PrintWindow 与 CopyFromScreen（屏幕真实内容）逐像素级一致地呈现同一场
   blank（`blankp_f2_pw.png` vs `blankp_f2_scr.png`）——用户在屏幕上真实
   看到界面消失。PrintWindow 通道对本构建忠实，矩阵分级口径有效。
4. **最早损坏**：交互类型 = **连续按住拖动（sliderAnalog 横带拖拽）**；
   页面 = **0/2/4 三页全部**（每页第一交互步第一帧即 blank 级）。
   页签点击与空白区拖拽在内容未丢时（p0/p2）**不产生**损坏。
5. **零崩溃**：6 个矩阵跑 + 3 个探针跑全程无 0xCxxxxxxx；矩阵 6 跑全部
   `exit=0x0` 干净退出。损坏是绘制/呈现问题，不是稳定性问题。
6. 机制线索（只读源码，未定谳）：`XPainter.c:10950-10976` 块注记载六波
   XGPU_GLYPH_HASH 键不完备时代「交互拖动下……整串回退软件，与已提交的
   GPU 字形叠加出**整窗横向碎片**（capture 实证）」——与本道抓到的
   「横向碎片/内容丢失」特征同族；本 exe 该开关默认关（六波口径，线性扫
   回退态），说明**哈希关态下交互损坏依旧存在**，损坏面在字形/呈现链更
   深处，供 A 路修复后对照定位。

## 1. 方法学

### 1.1 运行纪律

- 每格流程：`copy bin\XGuiWindowDemo_Test.exe %TEMP%\xgui_n3m_<be>.exe` →
  CWD=bin 启动 `--<backend> --page <N>`（stdout/stderr 重定向到管道，顺带
  消除子进程控制台窗口对点击的遮挡，沿 night3-interactive §1.2 教训）→
  落窗 4s → SetForegroundWindow → 三步交互 → WM_CLOSE 优雅退（读 stdout
  与退出码；3.5s 不退才 taskkill 兜底并如实标注）。
- 六格**串行**执行；每步后 `HasExited` 活性检查；判崩只认 0xCxxxxxxx。
- 驱动脚本：`Tools/matrix/matrix_run.ps1`（矩阵）、`probe_dual.ps1`/
  `probe_blank.ps1`（双通道仲裁）、`matrix_grade.ps1`（拼图+指标）。

### 1.2 三步交互定义与坐标（客户坐标 800×600 口径，按实测窗口 816×639 缩放）

源码定位（只读）：导航按钮行 `x=12+nav*86, y=44, 84×26`
（`Test/XGuiDemo/xgui_window_demo.c:2355`）；page4 页签控件
`:2589-2669`；`--page`/`--gpu`/`--software` 开关 `:3156/:3164/:3177`。
本应用唯一的 XSlider 在 page 3（`:2528-2585`，几何 `:1510`）——
**ask 点名的 0/2/4 三页均无滑块**，故「滑块拖动」按 bisect_drag.ps1
的 3 秒按住横扫模式适配到各页交互/文字横带（偏差如实记）：

| 步骤 | page 0（按钮演示） | page 2（堆叠演示） | page 4（选项卡演示） |
|---|---|---|---|
| ① sliderAnalog：按住横扫 3s，1s/2s/3s 抓帧 | y=292「就绪」label 文字横带 x 60..500 | y=262 上一页/下一页按钮行 x 60..420 | y=200 页签内容区（下拉框/LCD 行）x 100..700 |
| ② 页签点击 ×4，第 2/3/4 击后各抓 1 帧 | 导航行「按钮演示」(54,57) ×4 | 导航行「堆叠演示」(226,57) ×4 | TabWidget 真页签 (54/120/190/265, y=100) 依序 4 页签 |
| ③ 空白区按住横扫 3s，同样 3 帧 | y=450 x 250..650 | y=470 x 250..650 | y=430 x 250..700 |

- 抬键前先把光标移到空白中性点（740,y）再 mouse UP——防误点击污染后续帧。
- 点击生效证据（stdout 原文）：p0 四击 `switch page=0 (按钮演示)` ×4、
  p2 四击 `switch page=2 (堆叠演示)` ×4；p4 页签点击经画面状态变化证实
  （激活页签从「下拉」推进到「数码管」、SW 状态行出现「页签: 2」）。

### 1.3 分级口径

- **clean**：内容完整——应用标题文本、9 键导航排、本页全部控件、状态栏
  在位，文字可读；
- **text-noise**：布局主体在位，但文字区碎片/涂抹（含 FPS 叠层文字区
  黑条化这类局部文字损坏）；
- **blank**：整窗内容丢失——导航排/状态栏/页内容缺失（可残留拖动横带上
  的零星控件碎片）。
- 附注类：GPU clean 帧的 FPS 叠层（调试叠层，非页面内容）右下恒有一处
  白块伪影盖住下行部分文字，分级记 clean 但在 §2 表中标注。

## 2. GPU 矩阵（27 帧分级；★=含叠层白块伪影附注）

### page 0（gpu_p0_*）

| 步骤 | f1 | f2 | f3 |
|---|---|---|---|
| sliderAnalog | **blank**（拖带「就绪」+横向碎片残留，余全失） | **blank**（仅拖带虚线碎片+空叠层框） | **blank**（同 f2） |
| 页签点击×4 | clean★ | clean★ | clean★（导航/按钮/就绪/状态栏全在） |
| 空白拖拽 | clean★ | clean★ | clean★ |

截图：`Tools/matrix/gpu_p0_slider_f1..f3.png`、`gpu_p0_tabs_f1..f3.png`、
`gpu_p0_blank_f1..f3.png`；拼图 `sheet_gpu_p0.png`。

### page 2（gpu_p2_*）

| 步骤 | f1 | f2 | f3 |
|---|---|---|---|
| sliderAnalog | **blank**（残留「上一页」按钮描边框） | **blank**（残留「下一页」蓝底高亮块+左侧碎片；叠层尚可读 FPS 578.5） | **blank**（残留「上一页」框） |
| 页签点击×4 | clean★ | clean★ | clean★（内层页面 1/上一页/下一页全在） |
| 空白拖拽 | **text-noise**（页面主体完整，FPS 叠层文字涂抹成黑色横条） | **text-noise**（同） | **text-noise**（同） |

截图：`gpu_p2_slider_f1..f3.png`、`gpu_p2_tabs_f1..f3.png`、
`gpu_p2_blank_f1..f3.png`；拼图 `sheet_gpu_p2.png`。

### page 4（gpu_p4_*，最重灾：9/9 无一 clean）

| 步骤 | f1 | f2 | f3 |
|---|---|---|---|
| sliderAnalog | **blank**（残留左上「Option 1」下拉框；标题文本/导航排全失） | **blank**（同） | **blank**（同） |
| 页签点击×4 | **blank**（仅页签行残留+Option 1；标题/导航排/状态栏失） | **blank**（页签行亦失，残留旋钮圆+「20%」进度条） | **blank**（残留 LCD 框，数字呈黑色粗块） |
| 空白拖拽 | **blank**（页签行+LCD 竖条碎片） | **blank**（同） | **blank**（LCD 呈「2」但全窗余部仍失） |

截图：`gpu_p4_slider_f1..f3.png`、`gpu_p4_tabs_f1..f3.png`、
`gpu_p4_blank_f1..f3.png`；拼图 `sheet_gpu_p4.png`。

**page 4 附加事实**：从拖动步内容丢失后，**页签点击只带来局部重绘
（残留物随激活页签换样），直到本格结束再未恢复整窗内容**——只有导航级
整页切换（p0/p2 的页签步）才能把内容画回来。真实用户路径「拖一下再点页签」
会停留在丢内容状态。

## 3. SW 对照矩阵（27/27 clean，符合 ask「应全 clean」预期）

| 格 | sliderAnalog | 页签点击×4 | 空白拖拽 |
|---|---|---|---|
| sw_p0 | clean / clean / clean | clean ×3 | clean ×3 |
| sw_p2 | clean ×3（f1 还正确画出「上一页」按下描边+「下一页」悬停蓝底） | clean ×3 | clean ×3 |
| sw_p4 | clean ×3（页签行/Option 1/状态栏全程在位） | clean ×3（页签正确轮转：下拉→按钮(旋钮 20%)→数码管(LCD「0」)，状态行「页签: N」更新） | clean ×3（LCD「2」+全要素在位） |

截图：`sw_p0/p2/p4_slider_tabs_blank_f1..f3.png`；拼图
`sheet_sw_p0.png`、`sheet_sw_p2.png`、`sheet_sw_p4.png`。

## 4. 最早损坏与损坏演化

- **最早损坏 = 每页第一交互（sliderAnalog 按住拖动）的第一抓帧（0.8s）**，
  三页皆然，p0 为全矩阵最早一格（gpu_p0_slider_f1）。
- 演化（probe_blank.ps1 同模式复现，0.8/1.8/2.8s 双通道六图）：
  - 早期中间态（1.5s，`probe_gpu_drag_pw/scr.png`）：内容大体在位，
    「就绪」字形被咬缺+拖带 220px 横向碎片尾——典型 **text-noise**；
  - 深化态（1.8s，`blankp_f2_pw/scr.png`）：导航排/状态栏/标题文本全失，
    FPS 叠层涂抹成黑色横条，只剩拖带「就绪」——**blank**；
  - 即损坏随拖动持续**渐进加深**（局部文字碎片 → 整窗内容丢失），
    与「每帧增量呈现未把失效区外内容带回」的形态一致（形态学描述，
    机制归因不在本道权限）。
- 后端行为佐证：交互中 GPU 叠层自报 268–606 FPS，SW 7–15 FPS——两臂
  确实走在不同后端路径上（本道按「当前 exe 默认态」口径未设 XGPU_PROF，
  stdout 无 gl-info 行属预期，后端激活为行为级证据，如实记）。

## 5. 特征图谱速查（供 A 路字形修复后同矩阵对照）

| 特征 | 出现格 | 参考截图 |
|---|---|---|
| 整窗内容丢失（导航/状态栏/标题文本全失） | GPU p0/p2 slider 6 帧 + p4 全 9 帧 | gpu_p0_slider_f2.png、gpu_p4_tabs_f3.png |
| 拖动横带控件碎片（描边框/蓝底高亮块/虚线文字尾） | 同上各 blank 格 | gpu_p2_slider_f1/f2.png、gpu_p0_slider_f1.png |
| 文字咬缺+拖带碎片（内容尚未全失的中间态） | probe 1.5s 态 | probe_gpu_drag_pw.png / probe_gpu_drag_scr.png |
| FPS 叠层文字涂抹成黑色横条 | GPU p2 blank 步 3 帧 + 各深化态 | gpu_p2_blank_f1..f3.png、blankp_f2_pw.png |
| 叠层右下白块伪影（clean 态恒存） | GPU 全部 clean 帧 | gpu_p0_tabs_f1.png |
| 内容丢失后局部点击不恢复整窗 | GPU p4 tabs/blank 步 | gpu_p4_tabs_f1..f3.png |
| SW 同操作零损坏（对照基线） | SW 全 27 帧 | sheet_sw_p0/p2/p4.png |

## 6. 判崩与存活

- 矩阵 6 跑：全部 `exit=0x0`（含 WM_CLOSE 确认路径「关闭=是 返回=0」，
  stdout 原文可见）；每步间 `HasExited` 检查全活。
- probe_dual 首跑收尾因脚本漏导入 PostMessage 走 taskkill 兜底
  （exit=0xFFFFFFFF，**强杀假码陷阱**，非崩溃——第二跑修复后 exit=0x0；
  抓图在收尾前已完成，不受影响）。
- **0xCxxxxxxx 计数：0**。无 cdb dump、无残留进程（tasklist 复核）。

## 7. 诚实性声明

- 27+27 帧分级全部由本道真实读图（Read 工具逐张/拼图目检）作出；
  `matrix_grade.ps1` 的均值亮度/近白占比仅为辅助指标（本应用底色即浅灰，
  该指标不能替代目检），每张拼图含逐格指标条。
- 「滑块拖动」在三页均为适配口径（三页无滑块控件，见 §1.2 源码定位），
  沿用上一波 bisect_drag 的按住横扫模式；p0/p2 的「页签」为导航行（应用
  自身以页签行方式呈现），p4 为 TabWidget 真页签——均如实标注。
- 后端激活证据为行为级（FPS 叠层 268–606 vs 7–15 + 损坏不对称），
  非驱动层取证（未设 XGPU_PROF，避免偏离「默认态」口径）。
- PrintWindow 通道经双通道仲裁对本构建忠实（§0.3），但「首次 PrintWindow
  落在拖动中会抓到 blank 态、先静置抓过一次后再拖则抓到中间态」的通道/
  呈现交互细节未定谳，不影响分级有效性（两态均被屏幕通道证实真出画）。
- 机制归因（呈现链/字形链哪一环丢内容）不做——本道只测量；§0.6 仅引
  源码既有块注作线索。
- 环境同前波：jxy/console 会话、OrayIdd 虚拟显示 + AMD Radeon iGPU
  （GL 4.6.0 Compatibility）；绝对结论外推到其他环境需复测。

## 8. 产物清单

- 仓库新增：本文档 + `Tools/matrix/`（matrix_run.ps1、probe_dual.ps1、
  probe_blank.ps1、matrix_grade.ps1、54 帧矩阵 PNG、8 张探针 PNG、6 张
  拼图 PNG、6 份 stdout 日志）。
- %TEMP% 改名副本 xgui_n3m_gpu/sw.exe、xgui_n3probe_gpu.exe、
  xgui_n3blank.exe 已全部删除（%TEMP%\xgui_n3_unified.txt 为其他 lane
  产物，未触碰）；无残留被测进程。
- 零源码改动、零构建、零 git。
