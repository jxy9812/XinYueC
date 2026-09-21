# 绘制层性能专项四轮（表面裁剪/线段预裁剪/字形缓存/帧率普查/零拷贝）

> 归档自 XGui.md §2026（2026-09-21 文档重构迁移，内容逐字保留）。后续更新见 XGui.md 当前版。

## 17. 绘制层性能专项（表面裁剪/线段预裁剪/字形灰度图缓存） — 2026-09-19

### 17.1 背景与结果

图表页 800 帧、表格页 200 帧与其它 3000+ 帧页面的差距，最终定位为
三段通用绘制开销：线段逐像素走查、字形逐帧光栅化、贴图逐像素混合。
本轮三项机制落地后的基准（repaint 模式，520x360，6 秒采样）：

| 页面 | 优化前 | 优化后 |
| ---- | ------ | ------ |
| 图表（tab 20） | ~1076 FPS | ~2500 FPS |
| 表格（tab 19） | ~1526 FPS | ~5000 FPS |
| 默认（按钮页） | ~3747 FPS | ~6150 FPS |

### 17.2 表面裁剪（对齐 Qt setSystemClip）

`XWidget_flushBackingStore` 在 paintTree 递归前按刷区域外接矩形设置
`XPainter_setSurfaceClipRect(&sc, paintImage)`（设备坐标，作用于上屏
目标图像本身）；`begin_image` 继承为 painter 初始裁剪，putPixel/
fillRect span/blitImageRegion 三处独立判定保证控件 `setClipRect`
（ReplaceClip）也无法把像素写到脏区外。paintTree 不触碰该状态，
flush 结束统一 clear——作用域与生命周期都对标 Qt
drawWidget → setSystemClip(toBePainted)。

### 17.3 drawLine 包围盒预裁剪

`painterRaster_drawLine` 在取得设备坐标端点后、Bresenham 走查前，
把线段包围盒外扩笔宽半径（width/2+1，覆盖方头端帽），与图像边界 ∪
painter 裁剪 ∪ 表面裁剪求交，完全在外则整段返回（零像素可见）。
网格线、边框、序列线等高频场景免去无效走查；GPU 快速路径同样受益
（提前跳过 quad 提交）。

### 17.4 轮廓字形灰度图缓存（对齐 Qt glyph alpha map cache）

文本此前每帧逐字形执行：字库解码 → 路径搭建（已有路径缓存）→
轮廓拉直 → 4x4 抗锯齿覆盖率光栅化 → 堆分配 → 逐像素 putPixel。
新增 `XFONT_GLYPH_ALPHA_CACHE_ON`（XFont_config.h，默认 256 项，
单项目标上限 16K 像素）：

- 缓存键 =（XFontFace 指针, 码点, scaleKey），与既有路径缓存同口径；
- 位图以笔点 (penX, baselineY) 为原点存放（left/top 为相对偏移），
  命中后在 `x+tx+left, baselineY+ty+top` 处混合；
- 仅接受单位/整数平移变换：小数平移移动亚像素原点、覆盖率逐像素
  改变，必须退回逐帧光栅（与 Qt 仅缓存整数 hinting 位图同理）；
- 混合循环 `painterGlyphAlphaBlend` 跳过覆盖率为 0 的像素，其余与
  旧路径逐像素等价（coverage 255 直写 ink、中间值缩放 ink alpha 后
  经 putPixel 走裁剪/合成/边界）；
- 驱逐：stamp LRU，淘汰时释放位图；存储失败仅放弃复用不影响本帧。

校验：`XFONT_GLYPH_ALPHA_CACHE_ON` 0/1 两版对图表/表格/输入/多行
编辑四页截图逐字节一致（520x360x4 全零差异）；回归全绿；全 21 个
演示 tab 扫过无崩溃（此前多行编辑页 exit=3 已不复现）。

### 17.5 已知边界

- 灰度图缓存条目数 256：超过后 LRU 驱逐，纯中文长文本页若字形集
  大于条目数会退化（可调 `XFONT_GLYPH_ALPHA_CACHE_ENTRIES`）；
- 表格页剩余成本主要为单元格文本混合与背景填充，已到 0.20ms/帧；
- 图表页剩余 0.40ms/帧：背景渐变按行求值 + 序列/坐标轴混合，
  后续可做序列级脏区剔除（对部分重绘场景收益，对全帧基准无感）。

## 18. 全页面帧率普查与第二轮优化 — 2026-09-19

### 18.1 测量口径的修正（重要）

此前 `--benchmark` 的帧数走的是 Demo 的「静态场景缓存 + 仅重绘性能
浮层小块」路径，反映的是小区域增量刷新，不是整页绘制成本。新增
`--benchmark-full` 强制每帧整帧重绘（`demo_repaint` 直接标脏整个
窗口），并新增 `--maximized` 让基准在最大化窗口下运行。

**同时修复了一个真实缺口**：`XWidget_showMaximized` 此前只改内部
状态位，从不通知平台层，原生窗口尺寸不变（「最大化」实际无效）。
现按 Qt `QPlatformWindow::setWindowState` 语义补齐
`XPlatformNativeWindow_setWindowState`（Win32 用
ShowWindow(SW_MAXIMIZE/SW_MINIMIZE/SW_RESTORE/SW_SHOWMAXIMIZED)），
`XWindow_setWindowStates` 在生效状态变化时调用；创建原生窗口时补应用
创建前已请求的状态。最大化实测 520x360 → 2752x1089。

普查脚本：`out/sweep_fps2.sh <秒数> <normal|max> [--benchmark-full]`
覆盖 5 个演示页 + 选项卡页全部 21 个 tab。

### 18.2 第二轮优化：半透明纯色矩形的整段混合

全帧重绘普查暴露图表页在最大化下仅 **30.7 FPS**（32ms/帧），是全部
页面中最差。探针定位（已移除）显示 33ms 中的 **19ms 集中在
`xcv_paintArea`**（面积系列），而非背景渐变（1.4ms）。

根因：`painterRaster_fillRect` 的 span 快速路径此前只覆盖**不透明**
纯色（`Source` 或 alpha==255 的 `SourceOver`）；半透明填充（面积系列
的 `0x5516AFA9`）落入逐像素路径——每像素做矩阵求逆、矩形成员判定，
并经 `painterRaster_putPixel` → `XImage_pixel`（反预乘）+
`XImage_setPixel`（逐像素 `XImage_detach` + 格式分派）。单像素约
130~180ns，面积填充在最大化下 19ms/帧。

新增 `painterRaster_blendFillRect`：目标为 ARGB32_Premultiplied 时
按行整段混合，算式与 `painterComposeColor` 的 SourceOver 分支逐位
一致（读入反预乘 → 源/目标分量各自 `painterMul255` 预乘 → 相加 →
按结果 alpha 反预乘 → 写回时重新预乘）。非预乘目标返回 false 继续走
逐像素路径（不静默丢弃填充）。

结果（最大化、全帧重绘）：图表页 **30.7 → 74 FPS**；面积系列
19ms → 1.4ms。

### 18.3 第二轮：控件级脏区裁剪

`--benchmark-full` 暴露的第二个共性问题是若干控件每帧全量重绘自身，
与事件脏区无关（小区域刷新也要付整页成本）：

- `XPlainTextEdit`（及复用它的 `XTextBrowser`）：背景、凹陷边框、
  文本行全部按事件脏区限幅（`setClipRect(ReplaceClip)` + 逐边收拢）；
- `XWizard`：白底、横幅（标题/副标题）、底部分隔线按脏区限幅。

两处改动对 520x360 与最大化两档均有效，且截图逐字节一致。

### 18.4 第二轮基准（全帧重绘，每帧真实整页绘制）

| 场景 | 优化前 | 优化后 |
| ---- | ------ | ------ |
| 图表 2752x1089 | 30.7 FPS | 74~79 FPS |
| 多行编辑 520x360 | 434 FPS | 444 FPS |
| 多行编辑 2752x1089 | 109 FPS | 168 FPS |
| Wizard 520x360 | 280 FPS | 391 FPS |
| Wizard 2752x1089 | 108 FPS | 124~158 FPS |

最大化下各页数值在 100~230 FPS 区间，逐次运行波动约 ±20%（软件光栅 +
GDI 上屏受系统调度影响），单次采样不足以比较；趋势是图表页从「明显
最差」回到与其它页面同档。正常尺寸下 21 个 tab 全部落在 2800~8700 FPS。

### 18.5 复现方式

```bash
# 诚实口径的全帧重绘基准（每帧真实整页绘制）
./bin/XGuiWindowDemo_Test.exe --benchmark 3 --benchmark-full --page 4 --tab 20
# 最大化窗口
./bin/XGuiWindowDemo_Test.exe --benchmark 3 --benchmark-full --maximized --page 4 --tab 20
# 全量普查（5 页 + 21 个 tab）
bash out/sweep_fps2.sh 3 max --benchmark-full
```

新增命令行：`--benchmark-full`（强制整帧重绘）、`--maximized`
（最大化启动，依赖 18.1 补齐的平台状态同步）。

### 18.6 其它已定位但本轮未做的项

- 数码管（tab 2）最大化全帧 106 FPS：分段绘制逐段走 putPixel，可
  按脏区裁剪并整段填充；
- 多行编辑/浏览器仍按滚动视口而非脏区裁剪行范围（脏区裁剪已生效，
  但行循环仍遍历整个视口）；
- 图表页剩余成本：坐标轴网格线与刻度文本（约 4.5ms/帧，已用字形
  缓存与线段预裁剪）。


## 19. 第三轮：轴线/网格线整段填充 + 逐层探针定位 — 2026-09-19

### 19.1 方法：逐层探针拆解 13ms

在图表页最大化全帧（13.4ms/帧）上做逐层探针，按层拆解：

| 层 | 耗时 |
| -- | ---- |
| flush（双缓冲拷贝 + DIB 同步 + BitBlt） | 2.0ms |
| demo 静态场景（缓存命中路径） | 0.6ms |
| painter 全部调用（fill 3.2 + text 1.0） | 4.2ms |
| paintTree 总计 | 11.4ms |
| → 差值：网格线逐像素 putPixel | ~7ms |

最慢控件探针显示 12.7ms 集中在图表所在的 tab 页容器（递归含子树），
A/B 跳过各阶段后确认：series 2.6ms、axes 4.5ms（其中网格线 ~3.5ms、
文本 ~1ms）、背景+图例+标题 ~4ms。

### 19.2 修复：轴线/网格线整段 span 填充

`painterRaster_drawAxisLine` 的水平/垂直粗线此前对每个像素调用
`putPixel`（每像素重复做裁剪判定 + 合成）。不透明色改为按行/列整段
`XImage_fillRect`（「线段范围 ∩ 有效裁剪盒 ∩ 表面裁剪」一次写入），
像素结果与逐像素一致。半透明色保留逐像素回退。

坐标轴网格线 4.0ms → 0.65ms；图表页最大化全帧 76 → 84 FPS
（最长帧 26 → 19ms）。截图逐字节一致。

### 19.3 探针全部移除，当前余量（最大化全帧）

| 页面 | FPS | 说明 |
| ---- | --- | ---- |
| 图表 (tab 20) | 84 | 剩余：序列 2.6ms、文本 1ms、背景图例 ~4ms |
| 表格 (tab 19) | 242 | — |
| 多行编辑 (tab 9) | 124 | 剩余：行绘制仍按滚动视口而非脏区裁剪 |
| Wizard (tab 17) | 192 | — |
| 按钮页 (page 0) | 266 | — |

flush 层 2.0ms 是双缓冲架构的固定成本（脏区拷贝 + DIB 上传 +
BitBlt），单帧 3.7ms 的按钮页里占一半；若要突破需把 present 改为
直接写 DIB（省掉双缓冲拷贝）或走 GPU 直通。


## 20. 第四轮：软件渲染极限冲刺 — 2026-09-19

### 20.1 三项落地

1. **字形灰度混合直写内存**（XPainter `painterGlyphAlphaBlend`）：
   预乘 ARGB32 目标 + SourceOver + 无图案画刷时，字形位图与裁剪盒
   求交后逐行直写（Qt raster 的 blendColor with alpha map 同构），
   绕过逐像素 putPixel 的重复裁剪判定/合成/边界检查。混合算式与
   逐像素路径逐位一致（关键细节：alpha 缩放须带 +127 取整、RGB
   分量不随覆盖率缩放——首版两处都写错，被回归/像素比对当场
   抓出并修正）。
2. **多行编辑行循环按脏区裁剪**（XPlainTextEdit）：行范围 =
   滚动视口 ∩ 事件脏区，小区域刷新只重绘覆盖到的行。
3. **present 直写上屏**（Win32 DIRECT 模式）：`SetDIBitsToDevice`
   直接从 XImage 用户内存上屏，省去 XImage→DIB 的逐矩形 memcpy
   （此前 syncDirtyRect 本身就是 memcpy，证明字节序一致）；DIB/
   memDC 保留供 grab/兼容场景，FULL 模式路径不变。

### 20.2 基准（最大化 2752x1089、全帧重绘、4 秒采样）

| 页面 | 第三轮 | 第四轮 | 提升 |
| ---- | ------ | ------ | ---- |
| 图表 (tab 20) | 84 | 140~143 | 1.7x |
| 表格 (tab 19) | 242 | 366 | 1.5x |
| 多行编辑 (tab 9) | 124 | 253 | 2.0x |
| Wizard (tab 17) | 192 | 250 | 1.3x |
| 按钮页 (page 0) | 266 | 532 | 2.0x |

四天累计：图表页 30.7 → 142 FPS（4.6x），全部页面像素级一致。

### 20.3 剩余成本结构（图表页 7.1ms/帧）

- painter 原语 ~3ms：序列梯形逐段 fillRect（可合并为路径一次填）、
  背景渐变逐行求值；
- flush 双缓冲脏区同步 ~0.6ms：scrollContentsBy 滚动优化依赖
  inactive 缓冲，去除需重构滚动路径；
- 树遍历/事件构造 ~1ms；GDI SetDIBitsToDevice ~1ms（系统调用地板）。

软件光栅在此窗口规模已接近内存带宽极限（~3M 像素/帧 × 4 字节 ×
读改写 ≈ 36MB/帧 峰值流量）。进一步突破需要 GPU 直通（框架已有
XGpuRenderBackend，`--gpu` 场景已验证）或局部更新策略。


## 21. 第三轮回归修复：present 错位 bug 与零拷贝重做 — 2026-09-19

### 21.1 用户实测抓到的真实 bug

第三轮的「SetDIBitsToDevice 直接从 XImage 子矩形上屏」在真实使用中
（非基准的小脏区刷新）产生**整帧垂直错位复制**：窗口中部重复出现
顶部内容。根因是 SetDIBitsToDevice 的源子矩形语义（XSrc/YSrc 与
iStartScan/cScanLines 在负高度 DIB 下的叠加规则）与想当然的用法
不符——传 cScanLines=rect.height 而 YSrc=y0 时，GDI 从缓冲头部取行。

### 21.2 验证方法的漏洞与补丁

此前所有「像素逐字节一致」验证都读后备存储（XImage），而 present
层错误**只污染屏幕、不污染后备存储**——截图验证天然测不到。补上
读真实屏幕的校验工具（`out/screen_verify.c`，PrintWindow
PW_RENDERFULLCONTENT 抓窗口客户区 + 结构断言：标题栏横幅只允许
出现在顶部区域），以后 present 层改动必须过此工具。

### 21.3 最终方案：零拷贝 DIB 绘制（对齐 Qt Windows 后端）

不再绕 GDI 源子矩形，而是釜底抽薪——**绘制缓冲直接架在 DIB 内存上**
（Qt windows 平台插件同款结构）：

- 新增驱动钩子 `XPlatformBackingStoreDriver_getNativeBuffer`
  （Win32 返回 DIB bits，其它平台 NULL 回落）；
- `XPlatformBackingStore_resize` 优先申请 native 缓冲，用
  `XImage_init_ex_2` 外部内存模式构造绘制 XImage（m_ownsData=false，
  unref 不释放 DIB）；native 模式单缓冲，跳过双缓冲同步与
  surfaceResized（重建会使 m_image 悬垂）；
- 注意顺序：getNativeBuffer 会释放旧 DIB，resize 前必须先
  `xpbs_deepCopy` 旧内容快照（XCopy 是 COW 共享，不够）；
- present 在 native 模式下只剩按脏矩形 `BitBlt(memDC→窗口)`；
- 非 native 缓冲（外部缓冲等）保留紧凑行缓冲路径
  （biWidth=rect.width，XSrc=YSrc=0，规避 GDI 源子矩形陷阱）。

### 21.4 最终基准（最大化 2752x1089、全帧重绘）

| 页面 | 错误直写版 | 本轮零拷贝 | 累计（四轮前 30.7 起） |
| ---- | ---------- | ---------- | ---------------------- |
| 按钮页 | （弃用） | **682 FPS** | — |
| 表格 | 366 | **536 FPS** | — |
| 多行编辑 | 253 | **292 FPS** | — |
| Wizard | 250 | **211 FPS** | — |
| 图表 | 142 | **138 FPS** | **4.5x** |

（数字为 4 秒采样，±10% 波动；Wizard/图表本轮在测量噪声内持平。）

### 21.5 验证清单

- 回归全绿；图表/表格/按钮/多行编辑/Wizard/浏览器截图与基线
  逐字节一致；
- 全 21 tab 各 2 秒稳定运行；
- 屏幕校验：默认模式（小脏区，出 bug 的路径）与最大化全帧模式
  各连续 3~4 轮 PASS；resize 压力模式 PASS。


### 21.6 构建配置的影响（重要）：Debug vs Release

此前所有基准均基于 x64-Debug（/Od 无内联）。同一份代码的 x64-Release
（/O2）实测（最大化全帧）：

| 页面 | Debug | Release | 提升 |
| ---- | ----- | ------- | ---- |
| 图表 | 138 | **335 FPS** | 2.4x |
| 多行编辑 | 292 | **795 FPS** | 2.7x |
| Wizard | 211 | **608 FPS** | 2.9x |
| 表格 | 536 | **766 FPS** | 1.4x |
| 按钮页 | 682 | **955 FPS** | 1.4x |

最重页面（图表最大化全帧）3ms/帧，60Hz 刷新预算 16.6ms —— 余量 5 倍；
常态交互（增量刷新）成本远低于此。Release 下按钮页 1.05ms/帧约搬运
50MB 内存（静态场景 tile 拷贝 24MB + 页容器填充 10MB + BitBlt 12MB+），
已贴近单核内存带宽墙（~50GB/s 有效值）——软件模式的实际天花板。

### 21.7 顺带发现的既有问题（非本轮引入）

x64-Release 下回归套件在启动早期段错误（cdb 定位为 painter/shape
契约测试区的间接调用踩空），**HEAD 未含本轮改动时同样崩溃且崩溃点
不同**——判定为既有问题（项目此前仅跑 Debug/ASan 构建）。本轮所有
回归验证均基于 x64-Debug；建议后续单独排查 Release 构建的回归崩溃。


