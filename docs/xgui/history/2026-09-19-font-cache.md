# 字体缓存对齐 Qt 分层设计（嵌入式取向）

> 归档自 XGui.md §2026（2026-09-21 文档重构迁移，内容逐字保留）。后续更新见 XGui.md 当前版。

## 22. 字体缓存对齐 Qt 分层设计（嵌入式取向） — 2026-09-19

### 22.1 与 Qt 的分层对照

| 层 | Qt 设计 | 本框架现状（本轮完成后） |
| -- | ------- | --------- |
| 字体引擎实例 | QFontCache 按 QFont 请求 LRU 缓存 QFontEngine，度量创建时算一次 | **已对齐**：度量表 4 槽记忆（键=face+标量字段，命中免 info_base
  虚调用与 'M' 字形解码）+ XFontFace_register 注册表即引擎表 |
| 单字形度量 | QFontEngine 内 per-glyph glyph_metrics_t 哈希，查过即缓存 | **已对齐**：字形步进缓存 2 路组相联 512 项（键=face+码点+scaleKey），
  同时挂钩轮廓与点阵两条路径——textWidth/cursorRect 纯度量路径此前
  每字符每帧整字形解码，现已 O(1) 命中；空白字形（空格）以 1×1 零
  覆盖位图入灰度图缓存，度量随行返回 |
| 字形位图 | QFontEngineGlyphCache（raster）/ 纹理图集（GL），键=字形+格式+变换 | **已对齐**：路径缓存 24 项 + 灰度图缓存 256 项（键=face+码点+
  scaleKey），查找经 1024 桶哈希索引 O(1) 命中（陈旧索引由键复核
  兜底，线性扫描保留为回退）。与 Qt 一致仅缓存整数平移变换，小数
  平移退回逐帧光栅 |
| 排版/整形 | QTextEngine 缓存整形结果、QStaticText 预烘焙 | **沿用差异**：drawText 每帧重走 UTF-8 解码+步进（QPainter::drawText
  同档）；步进缓存已消除其中的字库解码成本。整形缓存随 §16 文本
  控制器化一并设计；QStaticText 等价物为新公共 API，需独立设计 |

### 22.2 本轮落地（两项，均像素级一致）

1. **字体度量表记忆**（XPainter `painterBitmapFont`）：原实现每次
   drawText/textWidth 都虚调用 `info_base` 并为取代表宽度**解码一次
   'M' 字形**。现按（face 指针 + pointSizeF/pixelSize/weight/style/
   stretch/hintingPreference 标量字段）记忆最近 4 个度量表，命中免
   重复解码——对齐 Qt「度量算一次」层。字库 face 为静态注册
   （指针稳定），绘制单线程，文件级 4 槽即可。
2. **空白字形入缓存**（`painterDrawOutlineGlyphSoftwareAA` 空白分支）：
   空格等无覆盖字形此前每帧重走字库解码+轮廓拉直。现以 1×1 零覆盖
   位图入灰度图缓存（度量一并返回），后续帧直接命中——对齐 Qt 对
   空白字形也缓存的行为。

3. **字形步进缓存**（XPainter）：2 路组相联 512 项，键
   （face, 码点, scaleKey）→ 像素步进，同时挂钩轮廓与点阵两条度量
   路径；解码失败（-1）也缓存避免反复空探。
4. **灰度图缓存哈希索引**（XPainter）：1024 桶哈希索引直指槽位，
   O(256) 线性扫描降为 O(1) 命中；键复核保证陈旧索引无害。

验证：回归全绿（Debug）；9 个代表页截图与基线逐字节一致；
屏幕校验 PASS；全 21 tab 稳定。

落地后 Release 基准（最大化全帧，4 秒采样，机器负载波动 ±20%）：
图表 421 / 表格 724 / 多行编辑 715 / Wizard 716 / 按钮页 720 FPS——
各页收敛到同一水平（此前图表是最差项），残余差异为各页填充面积的
物理差。

### 22.3 嵌入式调优说明（XFont_config.h 可配）

- `XFONT_GLYPH_ALPHA_CACHE_ENTRIES`（默认 256）：条目数直接决定
  常驻内存（每项约 40B 头 + 位图堆块）。RAM 受限目标可降到 64/128；
- `XFONT_GLYPH_ALPHA_CACHE_MAX_PIXELS`（默认 16K）：单字形位图上限，
  限制大字号下的突发占用；
- 查找已改为 1024 桶哈希索引 + 键复核（本轮落地），线性扫描仅作回退。


### 22.4 增量模式正式 A/B（修正一次测量事故）

用户反馈「帧数好像降了」。交错 A/B（同一构建类型、head 与当前二进制
逐轮交替运行抵消机器漂移，3 轮取中位）复核发现：**此前一轮测量中
出现的"回退"是陈旧二进制假象**（测量所用 demo 可执行文件诞生于
多次 stash/构建交织的中间状态，并非当前源码的产物）。以干净重建的
二进制重测，增量模式（性能浮层所显示的口径）全面大幅领先：

| 页面（增量刷新，浮层口径） | 会话起点 HEAD | 当前 | 提升 |
| ---- | ---- | ---- | ---- |
| 按钮页 | 7,788 | **12,692** | 1.63x |
| 图表 (tab 20) | 2,938 | **8,018** | 2.73x |
| 表格 (tab 19) | 4,349 | **12,168** | 2.80x |
| 多行编辑 (tab 9) | 3,506 | **12,234** | 3.49x |

教训入库：性能对比必须用「刚从当前源码链接的二进制」+ 交错运行；
任何一次复制/覆盖 bin 目录的操作之后都要重新核对产物来源。


### 22.5 Debug 构建交错 A/B（增量口径）

| 页面 | HEAD Debug | 当前 Debug | 提升 |
| ---- | ---------- | ---------- | ---- |
| 按钮页 | 3,632 | 6,353 | 1.75x |
| 表格 (tab 19) | 1,782 | 5,596 | 3.14x |
| 图表 (tab 20) | 1,032 | 2,663 | 2.58x |
| 多行编辑 (tab 9) | 1,442 | 5,622 | 3.90x |

### 22.6 顺带发现的既有问题：resize 基准模式挂起

`--benchmark-resize`（每帧交替 SetWindowPos 窗口尺寸）在 HEAD 与当前
源码、Debug 与 Release 四种组合下均会长时间停滞（本次复现）；
而当日早些时候同构建曾正常运行——环境敏感（疑与桌面忙时
SetWindowPos → WM_SIZE 内同步整页重绘的嵌套链相关），**非本轮引入**。
根因方向：把 WM_SIZE 处理内的同步整页重绘改为异步/限流，归入平台
层专项。


### 22.7 第二轮图表专项（LUT + 不透明源早退）

并发代码分析（两个子代理分别核查斜线宽线路径与面积/柱状填充成本）
修正了两个预设：斜线宽线并非 O(包围盒) 距离测试（实为平行偏移
Bresenham，O(长度×宽度)），quad 填充改造收益/风险比不划算；真正的
热点是面积系列的半透明混合内循环。

- `painterRaster_blendFillRect` 内循环重写：源预乘分量（循环不变量）
  提升出循环，目标分量衰减改 256 项查表（与内联 `(v*ia+127)/255`
  逐位等价），不透明目标（da==255，outA 恒为 255、写回预乘恒等）
  直写——每像素从 ~10 乘 10 除降到 3 查 3 加 1 写；
- `painterComposeColor` 增加 `SourceOver && sa==255 → return source`
  早退（factorDestination=0 可证 out 恒等于源），斜线 Bresenham/
  散点弦线等逐像素路径全部受益。

落地后（Release，最大化全帧）：图表 **324→491~580 FPS**（series
0.65~0.99→0.43ms），与其他页差距从 2~3 倍收窄到 1.2~1.8 倍；
图表内部 bg 0.15 / axes 0.23 / series 0.43 ms——三项均为填充
带宽所限（实测 fill 17~18 GB/s、copy 34 GB/s），无剩余软件优化空间；
页面间残余差异即各页填充面积的物理差。

### 22.8 尝试并撤回：面积序列真实多边形填充

尝试将面积系列的逐段矩形近似改为 Qt 同款闭合多边形一次填充
（XPainter_drawPolygon，修复下降段过填/上升段欠填的视觉偏差）。
回归套件出现**非确定性段错误**（同源码两次构建一次崩一次绿，
崩溃点位于 test_painter_shape_contract 附近；交错验证确认触发条件
为 XChartView.c 的 drawPolygon 改动）。已撤回该改动，保留已提交的
矩形近似版本（确定全绿）。根因方向：painterFillPolygonShape 在
「多点 + 半透明画刷 + 行常量渐变旁路」组合下的某个越界写——
注意 HEAD 的 Release 回归本就存在非确定崩溃（22.6/21.7 节），
两者可能同源，需独立专项用应用验证器（Application Verifier）定位。

### 22.9 根因结案：Picture 录制后端回放自递归导致栈溢出（已修复）

**真凶不是堆损坏，是栈溢出（STACK_OVERFLOW, c00000fd）**——通过 WER
LocalDumps 抓取崩溃转储 + `!analyze -v` 确认，栈扫描显示
`XGuiStyle` 单选按钮绘制帧递归 83 层。

根因：`XPainter_begin_picture` 把 8 个绘制回调（m_drawLine/fillRect/
drawImage/drawShape/drawPolyline/drawPolygon/drawPoints/drawPath）全部
绑定到 `painterRecord_*` 录制函数；这些录制函数**只检查 `m_picture`
非空、不检查 `m_replaying`**。于是 `XPicture_play` 回放时
（painter 仍处 Picture 后端、回调仍指向录制函数、m_picture 非空），
每条命令被再次追加回同一 Picture，形成无限自递归 → 栈溢出。
对比：所有 `painterRecord_clip*`/`painterRecord_penState` 等状态录制
函数都带 `m_replaying` 检查，唯独 8 个绘制录制函数漏了。

修复：8 个绘制录制函数全部加 `m_replaying` 分支——回放期间不再记录，
转为执行软件光栅实现（Qt `QPicture::play` 的语义：把命令画到目标
设备）。shape/polyline/polygon/points/path 通过临时清空对应回调后
调用公共 API，复用既有软件内联实现。

验证：Release 回归从**修复前 5/5 崩溃 → 修复后 8/8 全绿**；
Debug 回归 6/6 全绿；21 个演示 tab 稳定；屏幕校验 PASS；
除图表面积系列形状（预期修正，24 行差异）外像素逐字节一致。

这同时解释了此前所有异常现象：Release 才崩（栈帧更小更易触顶）、
Debug 侥幸不崩（帧大 + 调用深度不同）、页堆查不出（非堆问题）、
崩溃现场 m_drawPath 槽被写入代码指针（栈帧被覆盖）。

### 22.10 早期堆损坏 hunt 记录（工具链经验，已归档）

- gflags 页堆需管理员：经 UAC 提权已为两个测试 exe 启用 full 页堆
  （IFEO GlobalFlag=0x02000000 / PageHeapFlags=0x3）；
- 页堆下 Debug 回归完整跑过 1 轮**全绿**、demo 截图 3 轮无 AV——
  损坏不是 CRT 堆块的普通越界/UAF（页堆会当场拦截），指向
  **栈踩踏**（更深层帧的局部数组越界向上写）或 MultiPool 内部块
  越界（页堆对池内大块内部分配不可见）；
- 崩溃现场的 m_drawPath 槽被写入**代码指针**（与栈上返回地址被
  破坏的形态一致），佐证栈踩踏；
- 下一轮协议（工具已备）：重新应用 fillPath 复现（3/3 必现）+
  `bp xcv_paintLines "ba w8 @rdx+50; g"` 写断点（rdx=painter，
  +0x50=m_drawPath 槽）——损坏写必落在此区间，当场抓写入者栈；
- 页堆已为两个 exe 启用并保留（常规回归会变慢 ~10 倍；关闭命令：
  `gflags -p /disable XGuiRegression_Test.exe` / `... XGuiWindowDemo_Test.exe`，
  均需管理员 UAC）；
- 关键新事实：页堆 + 写断点（xcv_fillChartBackground 入口布防）
  运行显示崩溃发生在 **bg 填充之前**（fillChartBackground 断点从未
  命中，crash 栈却已到 paintArea）——且 fillChartBackground 疑似
  被 /Od 内联或符号绑定失败，断点是否真正生效存疑。下一轮必须先
  验证断点绑定（`bp xcv_renderToImage` 入口单步 + `x` 符号枚举），
  再布栈写断点；
- 教训：`bin/` 下同名二进制被多次构建覆盖，验证前必须 rm + 重链 +
  复核尺寸（Debug 7.3MB/10.8MB vs Release 4.0/5.3MB），否则会测到
  陈旧产物。本轮已再次踩坑（bin 混入 Release 回归导致误判 127 崩溃）。

