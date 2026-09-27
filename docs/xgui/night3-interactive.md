# night-3 第四波 S 道：keep-open 落地后交互质量目检 + 多窗冒烟 + PBO 命中率核查

日期：2026-09-26 04:05–04:5x（夜）
被测程序：`bin/XGuiWindowDemo_Test.exe`（**2026-09-26 03:38:00 构建**，即三波
K 路 keep-open + L 路四开关已落地的那份二进制；对照二进制：
`bin-release/XGuiWindowDemo_Test.exe` + `XinYueC.dll`，2026-09-24 12:26:54 构建）。
本道性质：**全测量、零源码**——未改任何源码/构建/仓库文件（本文档除外），
无 git 操作；exe 一律改名副本运行（`%TEMP%\xgui_n3i_s7.exe` / `%TEMP%\xgui_n3i_old.exe`），
taskkill 只作用于本道副本名/已核 pid，杜绝跨 lane 互杀假象（M 道判崩协议沿用）。
原始产物（截图/日志/驱动脚本）全部为 %TEMP% 临时件，用后即删（§6 清单）；
本 markdown 为唯一仓库产物。

## 0. 结论速览

1. **【红线·新发现】03:38 构建在屏幕上零渲染**：`--gpu` 与 `--software` 两后端
   一致——客户区在桌面合成器上是"透底"的（看到的是窗口后面的桌面），PrintWindow
   抓到的客户区是纯黑。连 `--benchmark`（570+fps 连续呈现）进行中也一样。
   同环境同显示器上，09/24 12:26 旧构建 `--gpu` 正常在屏出画（应用标题条、
   灰底、FPS 叠层），历史基线截图 `Tools/caps_base/f000.png` 更是完整内容
   （导航排+选项卡条+状态栏全在）。**环境已排除（同环境旧构建正常），指向
   三波未提交改动（或其构建配置）在共享的窗口呈现/落屏路径上引入回归**——
   两条后端同病，病不在 GL 驱动 keep-open 本身（离线通道一切正常：fps、
   readback、输入全活，见下）。
2. **交互输入链路全通（脚本实证）**：PowerShell `SetCursorPos+mouse_event`
   注入点击，页签 0→4→6→0 与 nav3 全部生效——应用 stdout 逐条打出
   `switch page=4 (选项卡演示)` / `switch page=6 (对话框)` / `switch page=0`
   / `switch page=3` / `switch page=0`，WM_CLOSE 关闭路径 `关闭=是 返回=0`
   干净退出。窗口消息 → 控件命中 → 槽分发 → 页面栈状态全链路活着。
3. **多实例冒烟通过**：双 `--gpu` 实例先后起动共存，各自页状态独立、无串扰；
   关一（WM_CLOSE 干净退）留一，留者继续响应切页并干净退出。keep-open 的
   会话身份在多实例下无跨会话错乱迹象（像素级内容完整性因发现 1 不可验证，
   如实记）。
4. **PBO 命中率：命中偏低（增量口径 77.0%），损失全部归因 bbox 漂移，
   fence 排除**：`stall=0`（五跑全零——containment 命中时 0 超时 fence 探测
   从未失手）、`mapfail=0`；seed=44/300（14.7%）为"请求 bbox 未被上一次
   读回 bbox 包含"（`XGpuRenderDriver_gl.c:2303`），与页面内容无关
   （page4 44 vs page0 42 / 300），且 `--benchmark-full`（恒定全窗 bbox）
   下 seed 塌缩到 1——漂移由基准的失效矩形摆动驱动，非 GPU 拖帧。
   另有恒定 25/300（8.3%）是全帧 `readback` 调用，设计上就不走 PBO
   （离屏像素契约，`xgld_readback` 传 `allowPboLag=false`）。

## 1. 交互脚本化目检（ask 项 1）

### 1.1 方法

- 起点：`copy bin\XGuiWindowDemo_Test.exe %TEMP%\xgui_n3i_s7.exe`，起 `--gpu`
  实例（CWD=源 bin 解析 DLL/assets），`Start-Process -PassThru` 取 pid。
- 鼠标注入：临时脚本 `%TEMP%\n3i_mouse.ps1`（`GetClientRect+ClientToScreen`
  客户坐标换算 → `SetCursorPos` → `mouse_event` LEFTDOWN/UP；drag 为 12 步
  插值）。客户坐标依据（只读源码定位，未改源码）：
  - 导航按钮行：`x = 12+nav*86, y=44, w=84, h=26`（`Test/XGuiDemo/xgui_window_demo.c:2256`，
    800×600 客户区来自 `:3171`）。取点：nav0=(54,57)、nav3=(312,57)、
    nav4=(398,57)、nav5=(484,57)、nav6=(570,57)。
  - page 0 按钮：容器内 (40,48,180,36)（`:2270`）+ 内容区原点 (12,78)
    （`:1440`）→ 客户中心 ≈(142,144)。
  - page 3 滑条：groupBox 内 `inner.y+92, h=28`（`:1461`）→ 客户 y≈205，
    轨道 x≈26..750，拖拽 (250,205)→(650,205)。
- 抓帧/差分：按 ask 指定用 `Tools/capture_frames.ps1`（PrintWindow
  PW_RENDERFULLCONTENT，120ms×6 帧）+ `Tools/diff_frames.ps1`（每 3 像素
  采样，阈值 30/通道；判据 changed>3000=全窗翻动异常）。
- 目检：PNG 由本道真实读图（Read 工具）逐张看，不是只看差分数。

### 1.2 首帧内容完整性目检 —— **FAIL（发现 1 的直接证据）**

`--gpu` 普通启动（默认 page 0）首帧：原生标题栏（"XinYueC 控件可视化测试" +
min/max/close）正常，**客户区 100% 纯黑**：无导航按钮排、无页内容、无状态栏。
末帧同黑。换成屏幕区域抓屏（`CopyFromScreen`）后客户区显示的是**窗口背后
的桌面内容（透底）**，两者互证：应用自三波改动构建起，客户区从未向屏幕
呈现过任何像素。对照：同法看 09/24 旧构建 `--gpu`，屏幕与 PrintWindow 一致
出画（蓝标题条/灰底/FPS 叠层"FPS 1408.7 帧耗时 0.11/1.05ms"清晰可见；
该旧构建未见导航排，疑与其 CWD=bin-release 的 assets 路径有关，与本题无关
——出画本身即对照成立）。更早历史截图 `Tools/caps_base/f000.png` 为完整
内容（导航排/选项卡条/Option 1/状态栏"就绪"全在），证明该链路曾完整工作。

环境佐证：本会话 `quser` 同前三波（console 交互会话），桌面抓屏对 ZCode
前台 UI 出画正常——显示器/会话无"抓不到 GL"的通病；同环境新旧构建差异
= 构建差异。

方法学注意（顺带发现）：本 demo 是 console 子系统，启动后同 pid 会带出一个
标题为 exe 路径的**控制台窗口**（实测 rect (26,26)-(1019,545)），恰好压在
demo 窗口上沿。抓屏目检前必须 `ShowWindow(console, SW_HIDE)` 并把 demo
窗口置顶，否则看到的是控制台（黑底）——此前 lane 的闪烁抓帧走 PrintWindow
不受此扰，但"黑"依旧（见下条）。

### 1.3 交互步骤 × 抓帧差分（全部六步）

每步：鼠标动作 → `capture_frames.ps1 -Count 6` → `diff_frames.ps1`。
每步窗口 816×639（客户 800×600，DPI 100%），进程全程存活，无 0xCxxxxxxx、
无 cdb dump（本道不涉及判崩：全部干净存活/退出）。

| 步骤 | 鼠标动作 | 差分 changed（5 对帧） | >3000 判定 |
|---|---|---|---|
| 基线 | 落窗后静置 | 0,0,0,0,0 | 无异常 |
| 1 | 点 nav4 (398,57) | 0,0,0,0,0 | 无异常 |
| 2 | 点 nav6 (570,57) | 0,0,0,0,0 | 无异常 |
| 3 | 点 nav0 (54,57) | 0,0,0,0,0 | 无异常 |
| 4 | 点 nav3 (312,57) + 拖滑条 (250,205)→(650,205) | 0,0,0,0,0 | 无异常 |
| 5 | 点 nav0 (54,57) + 悬停按钮 (142,144) 1.2s | 0,0,0,0,0 | 无异常 |

**"无 >3000 全窗翻动"在本构建上不构成及格证据**：底层是零渲染，黑面差分
恒 0，闪烁判据在此构建上完全失明。由此上溯：**既有"闪烁 0/29 保持"口径
对 03:38 构建同样是黑帧自比，不能当无闪烁证据引用**（caps-night3 历史帧
实查即黑，与本道新采一致）。

### 1.4 输入链路的应用侧实证（stdout，进程退出后冲刷）

关闭实例（WM_CLOSE）后读取重定向 stdout，六步动作被应用逐一确认：

```
XGuiWindowDemo: switch page=4 (选项卡演示)     <- 步骤1 点击生效
XGuiWindowDemo: switch page=6 (对话框)         <- 步骤2
XGuiWindowDemo: switch page=0 (按钮演示)       <- 步骤3
XGuiWindowDemo: switch page=3 (输入演示)       <- 步骤4 前置切页
XGuiWindowDemo: switch page=0 (按钮演示)       <- 步骤5
XGuiWindowDemo: 退出事件循环（关闭=是 自动退出=0 返回=0）
```

加上抓屏悬停期间的 `enter/leave` 事件行：鼠标消息→控件树→槽→页面栈
全通，关闭路径干净（返回=0）。**限制如实记**：滑条 valueChanged 与按钮
hover 高亮不产生 stdout 日志、像素又不可见，故这两项只有"输入已按坐标
送达+链路同构"的间接证据，无直接效果证据。

## 2. 多窗冒烟（ask 项 2）

demo 为单窗程序，按 ask 改为双实例先后起动（同一 %TEMP% 改名副本，
各自独立进程/GL 上下文/控制台）：

- 实例 B：`--gpu --page 0`，pid 7904；3s 后实例 C：`--gpu --page 4`，pid 8168。
  两实例同窗并存 ≥3s，均存活，各自 MainWindowHandle 独立。
- C 存活期间对 B 注入 nav4、nav0 两次点击（B 的 stdout 事后确认
  `switch page=4`、`switch page=0`——**兄弟实例在跑，B 的输入/会话照常**）。
- `WM_CLOSE` 关 C：`关闭=是 返回=0` 干净退出；**B 存活不受影响**。
- C 退出后对 B 注入 nav5 点击：`switch page=5 (条目视图)` 生效；抓帧 4 张；
  随后 B 亦 WM_CLOSE 干净退出。
- 双窗 PrintWindow/差分：与 §1.2 同因全零（零渲染），**像素级"两窗内容
  完整"在 03:38 构建上不可验证**；状态级完整性以上述各实例独立页序列
  （B: 0→4→0→5；C: 4）与双干净退出作证，**未见任何跨实例状态串扰或
  keep-open 会话身份错配征兆**。
- 附：`bin/XGuiGpu_Test.exe --gpu`（03:38 同期构建）双击即退（秒退，无窗口），
  本道未展开（非 ask 范围）。

## 3. PBO 命中率核查（ask 项 3）

协议：`set XGPU_PBO_STATS=1` + `%TEMP%\xgui_n3i_s7.exe --gpu --benchmark 5
--page 4`（CWD=bin），stderr 抓 `[gl-pbo]` 行（每 300 calls 打一条，
`XGpuRenderDriver_gl.c:2323-2326`）。五跑：

| 跑 | 口径 | calls | pbo(命中) | seed | stall | mapfail | 命中率/call |
|---|---|---|---|---|---|---|---|
| t1 | 增量 5s p4 | 300 | 231 | 44 | 0 | 0 | 77.0% |
| t2 | 增量 5s p4（复测，与 t1 逐位一致级） | 300 | 230 | 44 | 0 | 0 | 76.7% |
| 10s | 增量 10s p4 | 600 | 466 | 89 | 0 | 0 | 77.7%（次窗 78.3%） |
| p0 | 增量 10s page0（对照） | 600 | 464 | 86 | 0 | 0 | 77.3%（次窗 79.0%） |
| full | `--benchmark-full` 5s p4（对照） | 300 | 274 | 1 | 0 | 0 | 91.3% |

同跑 fps：p4 增量 570.3–577.3；p0 增量 821.2（与主会话锚点 p0 786–816 同量级）；
full 83.1（整帧口径已知退化，自洽）。

### 3.1 字段语义（只读源码核实，`Drive/windows/Graphics/XGpuRenderDriver_gl.c`）

命中 PBO 需同时过四关（`:2265-2303`）：① 本调用是呈现链 readbackRect
（`allowPboLag=true`）且 PBO 链路开且资源就绪；② 请求 bbox **被上一次
读回的 bbox 包含**（双槽乒乓，`:2267-2273`）；③ fence **0 超时**探测通过
（`:2274`，失败计 `stall`="GPU 拖帧" `:2302`）；④ glMapBuffer 成功（失败计
`mapfail` `:2298`）。不满足 ② 计 `seed`="首帧或 bbox 漂移出界" `:2303`；
回退帧仍照发异步读保温链路（`:2321-2322`）。全帧 `readback`（离屏像素契约）
设计上就不吃 PBO 滞后（`xgld_readback` 传 `allowPboLag=false`，`:2334-2338`），
但同样计入 calls——这就是与 300 对不上的那 25。

### 3.2 归因结论（ask 问：bbox 漂移还是 fence 未就绪）

- **fence 未就绪：排除。** 五跑 stall 恒 0——凡 bbox 包含成立的调用，
  0 超时探测全部通过（读回节拍 60Hz、渲染 570–820fps，两帧间隔足够 GPU
  清账，符合预期）。
- **bbox 漂移：成立，且是全部损失来源。** 增量口径 seed=44–45/300
  （≈15%），每 5s 恒定（10s 跑 seed 翻倍到 89 ⇒ 稳态持续再播种 ~4.5/s，
  非冷启动一次性）；`--benchmark-full`（恒定全窗 bbox ⇒ 恒被上一窗包含）
  下 seed 从 44 塌缩到 1（纯首帧）——算术闭合：274 = 300 − 25(全帧设计)
  − 1(首帧)。**页无关**（p4 44 vs p0 42、次窗 45 vs 44）⇒ 漂移由基准协议
  自身的失效矩形摆动驱动，不是页面内容/真实交互的 bbox 抖动；真实交互下
  演化未知（本道不外推）。
- **mapfail：排除**（0）。**全帧同步 25/300（8.3%）：设计行为**，非损失。
- 对主会话 1.011ms/readback 的分布含义：命中路径（拷出+unmap）远便宜于
  回退路径（同步直读+再播种）；增量口径约 15% 调用落在贵的回退路径上。
  若要抬命中率，方向是"上一帧 bbox 并回/扩大保留"（源码注释 `:2231`
  自述已有并回语义）或基准失效模式整形——**本道零改码，仅记录**。

## 4. 判崩/存活核查（M 道协议沿用）

本道全程 8 个被测进程（GPU/SW/双实例/基准×5/旧构建对照）无一个
0xCxxxxxxx、无 cdb dump；所有退出均为 WM_CLOSE 或基准自然到栅
（`返回=0`）。收尾 `tasklist` 核查无 xgui_n3i_* 残留。

## 5. 对主会话的移交建议（本道不改码、不立项）

1. **先于一切 fps 数字处理"零渲染"回归**：反超的前提是 GPU 路径最终要把
   画面放上屏；当前 03:38 构建上屏通道死（两后端同病 ⇒ 优先排查共享的
   窗口/present/后备存储路径，K/FBO_PERSIST 与 L 开关的交互是首要嫌疑，
   二分可用 `XGPU_FBO_PERSIST=0` 等现成开关、无需改码）。旧构建对照法
   （bin-release 09/24）本道已验证可用。
2. 闪烁 0/29 口径在 03:38 构建上失明（黑帧自比），修复呈现前不得引用；
   修复后建议抓屏（CopyFromScreen）+PrintWindow 双通道复核。
3. PBO 无需动：stall/mapfail 干净、seed 归因基准失效模式而非 GPU；若后续
   真实交互实测 seed 仍高，再按 §3.2 方向处理。

## 6. 纪律与产物清单

- 改名副本：`%TEMP%\xgui_n3i_s7.exe`（bin 03:38）、`%TEMP%\xgui_n3i_old.exe`
  （bin-release 09/24）、`%TEMP%\xgui_n3i_g.exe`（XGuiGpu_Test，仅秒退探测）。
- taskkill 范围：本道副本名 + 已核 pid，未碰 `XGuiWindowDemo_Test.exe` 同名
  与其他 lane 资源。
- 临时件用后即删：驱动脚本 5 份（n3i_mouse/n3i_cap_screen/n3i_enum/
  n3i_front_cap/n3i_probe_paint.ps1）、截图目录 n3i_caps\（含 60+ PNG 与
  fullscr.png）、日志 n3i_*.log 8 份、副本 exe 3 份。
- 仓库新增：仅 `docs/xgui/night3-interactive.md`（本文件）。零源码改动、
  零构建、零 git。
