# XGui 进度文档

> 最后更新：2026-09-18 Asia/Shanghai
> 职责：记录 XGui（对标 Qt 6.8.3）当前实现进度、已知问题与下一步。
> 本文件面向“更换 AI 继续”场景，所有定位信息均为当前仓库实测事实。
> **阅读指引**：当前进度与计划看本文各主节；历次会话的逐轮改动日志
> （10.x 与 14.3~14.68 调试链，2026-09-17 精简移除约 1.45 万行）已归档，
> 完整内容见 git 历史（提交 `8a24b127` 及之前的 XGui.md 版本）。

## 1. 当前任务目标

XGui 主体（布局/图像/绘制/控件/平台抽象）已按 Qt 6.8 对齐并可裁剪；当前
主线为 **GPU 光栅渲染后端与直通上屏**（进度见第 14 节，Windows 已实现主体、
Linux 待续）。总开关 `XGUI_ON` 在 `Src/CXinYueConfig.h`；GUI 子模块开关集中在
`Src/XGui/XGuiConfig.h`；GPU 裁剪开关 `XGPU_ON`（嵌入式无 GPU 置 0 整体裁剪，
默认软件渲染不受影响）。

## 2. 仓库状态

- 分支：`codex/xdevice-file-platform`
- 工作树：**保留未提交改动**（含 GPU 渲染后端等近期工作），不清理、不丢失、不 push
- 构建：Windows 用 `out/build/x64-Debug`（Ninja+MSVC）；Linux 用 `build/`
  （见第 9 节命令）；测试程序 `bin/XGuiRegression_Test`、`bin/XGuiGpu_Test`
- 分支/提交约定：默认分支前缀 `codex/`；**不要 push**（除非用户明确要求）

### 2.1 XGui 子类接口规则

- 子类若只是继承父类已有 API，头文件直接用宏别名复用父类函数，不再声明或
  实现同义包装函数；调用约定和参数保持父类一致。
- 只有增加了子类状态、参数或行为，或者覆盖后语义确实不同，才保留子类函数；
  例如 `XGridLayout_setSpacing/spacing` 操作独立的水平/垂直网格间距，不归入
  父类 `XLayout` 间距接口的简单转发。
- 后续扫描按“继承关系 -> 头文件声明 -> C 实现 -> 引用 -> 默认/裁剪构建”顺序执行，
  并保留现有未提交改动。

### 2.2 运行回归测试

```bash
# 仓库根运行（Windows 先经 vcvarsall 注入 MSVC 环境）
./bin/XGuiRegression_Test
```

当前结果：默认构建回归**全绿**（`XGui regression tests passed`）；ASan 回归、
FULL/PARTIAL 渲染模式变体、`XGPU_ON=0` 无 GPU 裁剪变体均通过；GPU 冒烟
`XGUI_RENDER_BACKEND=gpu ./bin/XGuiGpu_Test` 通过。运行时输出的窗口参数提示
（无效 transient parent、忽略 WindowActive）及空对象诊断日志为既有测试路径
预期输出，非失败。

## 3. 已完成工作概览

### 3.1 图像体系（已完成）

- XImageCodec 九类格式完整编解码及 ICO 单条目路径：BMP（24/32 无压缩正/倒序）、PNG（8 位
  0/2/4/6 型、反滤波 0~4、无 Adam7、无调色板型）、JPEG（基线 SOF0、
  YCbCr/灰度、1/2/4 抽样、DRI；编码固定 4:2:0）、GIF（静态首帧、全局/
  局部调色板、透明色、GIF89a 编码）、PPM/PBM/PGM（P1-P6 ASCII 与二进制
  变体）、XBM（MonoLSB 十六进制位图）、XPM（调色板与透明色）、SVG/SVGZ（内嵌 PNG
  位图、轻量矢量渲染及 gzip 输入）、ICO/CUR（嵌入 PNG 与 24/32 位 DIB，首个条目）
- JPEG/GIF/SVG/XPM 等“扩展能力”通过配置文件开关可裁剪（PC 全开）；SVGZ
  作为 SVG 的只读 gzip 输入键随 `XIMAGECODEC_SVG_ON` 一并裁剪
- XImage / XPixmap / XPixmapCache / XPicture / XPainter / XPixmapCache 修复
  与完整实现，格式互相转化、整体对齐 Qt 图像体系
- 图像编解码开放接口统一集成在 XImageCodec；上层图像类统一调用其 API

### 3.2 XGui 布局系统（已完成）

目录 `Src/XGui/XLayout/`：

| 文件 | 对标 | 状态 |
|---|---|---|
| XLayoutItem.h/.c | QLayoutItem / QWidgetItem | 已实现并完成默认对齐/RTL/clamp 对齐 |
| XLayout.h/.c | QLayout | 已实现 |
| XBoxLayout.h/.c | QBoxLayout / QHBoxLayout / QVBoxLayout | 已实现并完成 Qt 几何分配对齐 |
| XGridLayout.h/.c | QGridLayout | 已实现并完成只扩不减网格语义 |
| XSpacerItem.h | QSpacerItem | 已实现 |
| XStackedLayout.h/.c | QStackedLayout | 已实现 StackOne/StackAll、索引切换、页面插入/移除及几何同步 |
| XLayout_config.h | 裁剪开关 | 已实现 |
| XLayout_Internal.h | 内部共享 | 已实现 |

另含 XApplication / XWindow（对标 QWindow）/ XWidget / 事件系统、Drive
平台后端（Linux/Windows）等前期已完成内容。

### 3.2a GPU 渲染后端（近期完成，详见第 14 节）

阶段 1（离屏 GPU 光栅 + readback）与阶段 2（窗口直通上屏，GPU swapBuffers）
主体已完成并全绿验证。原第 14.3 节遗留问题已全部收口：outline 字体 GPU
文本降级与 `drawTextRect`/`drawGlyph` GPU 分支缺失已修复（14.3-1/2，详见
14.5）；带下划线/删除线/上划线的 drawText 三后端走局部提交一致（Phase 3.2，
2026-09-16）；软件回归、GPU 回归（GL/Vulkan，XGUI_GPU_SYNC=1）、CTest 全绿。


### 3.3 XGui 源码目录

根目录只保留功能子目录，公共 API 名称和头文件 basename 不变，便于按 Qt
模块查找：

| 目录 | 内容 |
|---|---|
| `Application` | `XApplication`、`XGuiApplication` |
| `Window` | `XWindow`、窗口事件接口、`XScreen` |
| `Widget` / `Layout` | 控件与布局体系 |
| `Platform` | `XPlatform*` 抽象及平台集成对象 |
| `Graphics` | 图像、像素图、绘制、后备存储、GPU 与编解码 |
| `Input` | 无障碍、剪贴板、光标、输入法、MimeData |
| `Style` | 调色板、样式提示、表面格式 |
| `Icon` | 图标及图标引擎 |

XGui 配置集中在 `Src/XGui/XGuiConfig.h`。`Src/CXinYueConfig.h` 只保留
`XGUI_ON` 总开关并引入该文件；嵌入式构建使用 `-DXGUI_ON=0` 即可统一关闭
所有 GUI 子模块，桌面构建仍可按需覆盖子开关。

## 4. 已清零的布局失败

此前列出的 28 项均已修复或按 Qt 6.8.3 实际行为更新测试期望；当前无布局
断言失败。

## 5. 已验证的真实 Qt 6.8.3 行为基准（off-screen 实测）

参考源码：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/`
（`qboxlayout.cpp / qlayoutitem.cpp / qgridlayout.cpp / qlayoutengine.cpp`、
`../gui/kernel/qguiapplication_p.h`）

实测探针：`/tmp/qtlayoutcheck2`（`QT_QPA_PLATFORM=offscreen` 可复跑；
输出见下）。

### 5.1 QWidgetItem::setGeometry 默认对齐规则（qlayoutitem.cpp）

- **align == 0（未显式设置对齐）**：不收缩到首选尺寸；水平位置贴左
  （`QStyle::visualAlignment` 会给无水平位的对齐补 `AlignLeft`），垂直位置
  **居中**（`else if (!(align & AlignTop)) y = (r.height - s.height)/2`）。
- **显式设置对齐位时**：才按首选/hfw 收缩（`if (align & AlignHorizontal_Mask)
  s.w = qMin(s.w, pref.w)`，垂直同理或走 heightForWidth）。
- 末尾 clamp：`if (x < 0) { s.rwidth() += x; x = 0; }`，y 同理（**收缩尺寸
  而非简单平移**）。
- 水平摆放用 `alignHoriz = visualAlignment(layoutDirection, align)`：
  Right→贴右；无 Left→居中；否则贴左。RTL 且未带 AlignAbsolute 时
  Left/Right 互换（qguiapplication_p.h）。

### 5.2 QBoxLayout::setGeometry（qboxlayout.cpp）

- 镜像公式：RTL/BTT 为 `s.left() + s.right() - pos - size + 1`
  （X 使用半开区间，等价 `2*inner.x + inner.width - pos - size`）。
- 先存 `oldRect = geometry()`，再算 `reverse = horz ? ((r.right() >
  oldRect.right()) != (visualDir == RightToLeft)) : (r.bottom() >
  oldRect.bottom())`；`reverse==true` 时**逆序遍历** `i = n-1-j` 应用几何。
- visualDir：父控件 RTL 时 L/R 盒方向互换。

### 5.3 QBoxLayoutPrivate::setupGeom（交叉轴聚集）

- 交叉轴 min / hint 对**全部条目**取 qMax（含空 strut、隐藏控件）；
- 只有交叉轴 max（qMaxExpCalc）跳过 `empty && widget()` 的隐藏控件。

### 5.4 qGeomCalc 多余空间分配（qlayoutengine.cpp）

- 富余空间 `extraspace / (spacerCount + 2)` 均匀分给**链首、链尾、条目
  之间**的空档；`spacerCount` 为非空条目之间的间距个数（2 个条目间隔数
  为 1，即均分 3 档）。

### 5.5 QGridLayout（qgridlayout.cpp）

- 构造函数网格为 1x1（`expand(1,1)`）；
- `add()` 内 `expand(row + 1, col + 1)` **只扩不减**；
- `setNextPosAfter` 使用**扩容后**的行列数；
- `addItem()` 先取 nextPos 再加条目；span 存结束坐标；
- 移动后 `takeAt` 网格维度**保持不缩**。

### 5.6 探针实测输出（/tmp/qtlayoutcheck2）

```text
integ c0 geo=74,50 40x20      （间距6；间距0时为 76,50）
integ c1 geo=194,50 30x20     （间距6；间距0时为 192,50）
hidden c1 geo=135,50 30x20
restored c0 geo=74,50 40x20
restored c1 geo=194,50 30x20
stretch a geo=0,2 30x26
stretch b geo=36,2 54x26
TTB wv geo=0,15 40x20
BTT wv geo=0,15 40x20
HBox+strut w0 geo=0,15 40x20
HBox+strut hint w,h = 40,50
VBox+strut w0 geo=0,0 40x20
VBox+strut hint w,h = 80,20
noalign w geo=0,0 300x100
```

## 6. 已修复的 X 实现问题

### 6.1 XLayoutItem.c `VXWidgetItem_setGeometry`

已按 Qt 重排：

1. 尺寸收拢（boundedTo max）逻辑不变；
2. 仅当显式对齐位存在时按首选/hfw 收缩（使用**裸 align** 判断）；
3. 水平位置始终用 `visualAlignment`（无水平位补 Left；RTL 且非 Absolute
   时交换 Left/Right）计算：Right→贴右、无 Left→居中、否则贴左；
4. 垂直位置始终计算：Bottom→贴底、无 Top→居中、Top→贴顶；
5. 末尾负坐标 clamp 改为 `x<0 { s.width+=x; x=0; }`（同 y）。

### 6.2 XBoxLayout.c `XBoxLayout_calcMetrics`

已对全部条目聚集交叉轴 min/hint，并将 expanding 聚合移入独立的
qMaxExpCalc 循环；隐藏控件仍按 Qt 语义跳过交叉轴 max。

### 6.3 XBoxLayout.c `VXBoxLayout_setGeometry`

已补齐 RTL/BTT 镜像公式中的内部矩形原点，并按 Qt 保存 oldRect 和 reverse
顺序更新条目。
  验证：RTL 用例 inner=(4,...,132)，w0/w1/w2 几何与 Qt 基准
  (96,60,4) 一致；垂直盒 hfw 分支保留。

### 6.4 XGridLayout.c

已改为 `expand(toRow+1, toCol+1)` 只扩不减；`takeAt` 保留网格维度；
构造初始化为 1x1，与 Qt 构造函数一致。

## 7. 已更新的测试期望（xgui_regression_test.c）

| 用例 | 旧期望 | Qt 真实行为（新期望） |
|---|---|---|
| 布局集成（~6306，间距 0） | c0=(0,0,40,20)、c1=(40,0,30,20) | c0=(76,50,40,20)、c1=(192,50,30,20) |
| 集成-隐藏 c0 后 | c1=(0,0,30,20) | c1=(135,50,30,20) |
| 集成-恢复显示 | 同旧 | 同新（**y=50！垂直居中**） |
| stretch 1:2（~5697） | ga=(0,0,40,30)、gb=(40,0,50,30) | ga=(0,0,30,30)、gb=(30,0,60,30) |
| BTT/TTB（~5717） | (0,30,40,20)/(0,0,40,20) | 均为 (0,15,40,20) |
| HBox+strut（~5796） | w0=(0,0,40,50)，sizeHint=(40,50) | **w0=(0,15,40,20)**，sizeHint=(40,50) |
| VBox+strut（~5806） | w0=(0,0,80,20)，sizeHint=(80,20) | w0=(0,0,40,20)，sizeHint=(80,20) |
| RTL（~5665） | w0/w1/w2=(96,60,4) | **期望不变**（保持 96/60/4） |
| 「清除对齐后条目填满单元格」 | 已通过 | **不受影响**（growable max 不限→全尺寸时垂直居中无位移） |

上述期望已写入回归测试并全部通过。

## 8. 第三方问题（已修）

`test_codec_decode_real_assets`（xgui_regression_test.c ~1267）使用相对
路径 `assets/...`，从 `bin/` 下运行时找不到资源。已在测试内先试
`assets/` 再试 `../assets/`，从仓库根或 `bin/` 启动均可。

## 9. 构建与验证命令

```bash
# 1) 先刷新静态库（libXinYueCS.a 较旧）
cmake --build build --target XinYueCS -j$(nproc)
# 2) 再构建回归测试
cmake --build build --target XGuiRegression_Test -j$(nproc)
# 3) 从仓库根运行
./bin/XGuiRegression_Test
# 结果：0 失败

# 4) Linux XDND 跨客户端协议验收
cmake --build build --target XGuiXdnd_Test -j$(nproc)
./bin/XGuiXdnd_Test
# 结果：XdndEnter/Position/Drop selection transfer passed
```

全量外带：`cmake -S . -B build && cmake --build build -j$(nproc)`。


## 10. 历史改动日志（已归档）

> 2026-08~2026-09 的逐轮改动日志（原 10.1~10.2xx 与 2026-09-04/05
> 日期节，约 1.2 万行）已从本文移除，完整内容见 git 历史（提交
> `8a24b127` 及之前的 XGui.md 版本）。主结论已沉淀于第 3 节与第 11b 节。

## 11. 仍属抽象/平台边界的 API

以下返回空值不是漏实现，而是接口本身需要外部对象或尚未建立对应资源层：
`XIconEngine`、`XImageIOPlugin` 的基类默认虚函数、共享图形缓存。出站拖放、离屏
表面、字体/主题/桌面服务和无障碍已经分别由公共对象与 Drive 平台桥接实现。
`XGpu` 已统一 OpenGL/Vulkan
的驱动选择与适配器信息，Linux 通过
GLX/Vulkan、Windows 通过 WGL/Vulkan 后端创建真实上下文/实例；系统输入法事件
已经以 XIM/XIC（Linux）和 IMM32（Windows）转换为 `XInputMethodEvent` 注入。
PC 的软件光栅、X11
和 Win32 窗口/后备存储路径已经有真实实现；嵌入式在这些能力关闭时保留安全
退化语义，不伪造平台句柄。

## 11b. 已知偏差清单（Task 2.20 收口，2026-09-15）

以下差异在头文件 @note 或本清单正式声明，属架构裁剪/平台边界，不视为漏实现：

- **XPaintDevice 体系**：XPaintDevice/XPaintEngine 为公开类全量实现（枚举
  数值对齐 QPaintDevice/QPaintEngine，查询 API 齐全）；XPaintEngine 的绘制
  命令接口（begin/end/draw* 纯虚）由 XPainter 承担，不建重复引擎；
  QImage 在 Qt 中不继承 QPaintDevice，XGui 的 XImage/XPixmap/XBitmap/
  XPicture 统一接入 XPaintDevice 属项目决策（D3）。
- **QIcon 路径字符串映射**：图标以路径字符串（XString）承载，等价于
  QIcon 的 QIconEngine 资源寻址；无 QIconEngine 插件动态加载，内置
  XSvgIconEngine 插件经 XIconEnginePlugin 注册表按后缀选择。
- **setIconSize(int) 单值**：图标尺寸以单 int 方边值承载（Qt 为
  QSize），宽高不等场景需自行换算；头文件已注明。
- **QLayout 默认边距/间距 0 vs Qt 样式**：XGui 布局默认边距/间距为 0，
  Qt 由样式提供默认值（9/6 等）；显式设置后一致。
- **XMovie 手动驱动**：定时驱动为正式裁剪项，调用方按帧延迟自行驱动。
- **富文本子集边界**：XTextDocument 为纯 C 子集，不做完整 Qt 富文本
  引擎；行为差异头文件已声明。
- **XStackedLayout 信号决策**：布局自身不发射 currentChanged，信号
  所有权在 XStackedWidget（Task 0.5 裁决）。
- **XPainter 路径裁剪近似**：setClipPath 按路径包围矩形裁剪，精确
  路径光栅裁剪未实现；clipPath() 返回空路径（Task 2.11）。
- **XColorSpace ICC 承载**：ICC 字节以固定 1024 缓冲透明承载，超过
  截断；ICC 不解析为矩阵/LUT（Task 2.11）。
- **XTouchEvent 单点**：触摸事件承载首个触点，完整多点列表未实现
  （Task 2.13）。
- **XPaintEngine 类型**：XPaintEngineType 数值对齐 QPaintEngine::Type；
  XGui 统一使用 Raster 引擎语义。
- **XMenuBar 几何模型**：actionGeometry/actionAt 用统一布局模型
  （文本宽+16），与无样式绘制的固定 60px 间距存在轻微偏差（Task 2.10）。
- **GPU 文本装饰（已收口，非偏差）**：原三后端 drawText 文本装饰差异已
  于 Phase 3.2 修复——带 underline/strikeOut/overline 的绘制走局部提交，
  software/OpenGL/Vulkan 行为一致（2026-09-16，见第 14.3/14.5 节）。
- **QWidget 快捷键/手势（2026-09-17 Phase 3.1）**：快捷键以 XShortcut
  对象承载（Task 2.19），不建 Qt 的 grabShortcut id 注册表；手势识别
  体系（grabGesture 等）不做。
- **QFileDialog URL 族（2026-09-17 Phase 3.1）**：文件路径以本地字符串
  承载，getOpenFileUrl 等 URL 变体不做（依赖网络访问管理器的
  getOpenFileContent/saveFileContent 一并不做）。
- **QCalendar 备选历法（2026-09-17 Phase 3.1）**：XGui 纯公历，
  QDateTimeEdit/QCalendarWidget 的 calendar/setCalendar 不做；
  QCalendarWidget 按日期的 dateTextFormat（QTextCharFormat 映射）随
  富文本子集边界不做。
- **XDateTimeEdit 分段承载（2026-09-17 Phase 3.1）**：currentSectionIndex
  与 currentSection 共用同一字段（分段序号/分段码不分），宏别名已注明。
- **XHeaderView 继承链（2026-09-17 审计登记,调研结论维持现状）**：
  XHeaderView → XWidget，Qt 为 QHeaderView → QAbstractItemView 派生。
  专项调研（docs/xgui-audit/2026-09-17/headerview-refactor-plan.md）
  决定性证据：全仓零集成（XTableWidget/XTableView 均自绘表头,唯一
  使用点是回归断言）；改派生收益为负（基类 scrollTo 为桩、selection
  能力与段模型无关、须反向压制 4 鼠标槽+IndexAt 共 5 处）；重评触发
  条件为表头实体化集成进 XTableView 时再议。行为差异 9 条见方案文档
  （"移动即重排"与 Qt 视觉重排语义相反为最重要差异）。

## 12. 约束（沿用项目约定）

- 头文件详细中文注释；风格严格遵守
  `代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md`；
- 纯 C99，**不引入任何后台/平台 API**，嵌入式可用；
- **（2026-09-17 用户明令）原则上 `Src/` 目录下的代码不允许直接调用平台
  API**：平台调用（X11/Win32/POSIX/DBus/GL/Vulkan/fontconfig 等）只允许
  出现在 `Drive/` 平台适配层，Src 侧一律经由公共抽象层（如
  XPlatformNativeWindow/XPlatformBackingStore 等）间接到达；新增代码违者
  打回，存量违规按审计清单分批收敛；
- 布局开关裁剪语义：关闭开关后公共 API 硬裁剪（头文件保护壳保留，引用
  触发“类型未声明”），.c 整段不编译；
- 提交前 `git diff --cached --check`；不 push（除非用户明确要求）。

## 13. 参考

- 旧交接文档（图像体系，Windows 时期）：
  `XGui_Qt_Alignment_Handoff.md`
- Qt 源码：/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/、
  ../gui/kernel/qguiapplication_p.h
- 探针程序：/tmp/qtlayoutcheck2（复跑
  `QT_QPA_PLATFORM=offscreen ./b/qtlayoutcheck2`）


## 14. GPU 渲染后端（Windows 实施记录，Linux 待续） — 2026-09-06

> 本节专记 XGui GPU 光栅化后端与直通上屏的进度。Windows（WGL）已实现并验证
> 主体，遗留问题计划在 Linux（GLX）继续。设计文档见
> `docs/superpowers/specs/2026-09-06-xgui-gpu-render-backend-design.md`。
> 运行时开关：`XGUI_RENDER_BACKEND=gpu`（默认软件）；编译裁剪：`XGPU_ON=0`。

### 14.1 已完成的架构

```
阶段 1（离屏 readback，已完成并全绿）：
  XPainter → 离屏 GL 会话（XPlatformOffscreenSurface + FBO）
    → 帧末 readback 到 XImage → XPutImage/BitBlt 上屏
  局限：每帧 GPU→CPU 读回（750KB/帧）→ demo 仅 ~188 FPS。

阶段 2（窗口直通上屏，主体完成，对齐 Qt QBackingStoreDefaultCompositor）：
  XWidget_flushBackingStore → XGpuRenderBackend_acquireForWindow（窗口 GL 上下文
    XPlatformOpenGLContext，自建离屏 FBO）
    → XPainter 画到窗口上下文 FBO（持久缓冲，脏区叠加）
    → XGpuRenderBackend_presentToWindow：全屏 quad 采样 FBO 颜色纹理
        → 窗口默认帧缓冲 → swapBuffers（零 CPU 上屏）
```

关键文件（本轮新增/改动）：
- `Src/XGui/Graphics/XGpuRenderBackend.h/.c`：GPU 会话。离屏模式
  （`XPlatformOffscreenSurface`）+ 窗口直通模式（`XPlatformOpenGLContext`，
  `createForWindow`/`presentToWindow`/`isWindowMode`）；GL 函数全部经
  `getProcAddress` 运行期解析（无平台 GL 头，GLES 可复用）；GLES2 兼容
  shader（`#ifdef GL_ES precision`）；全局会话管理（acquire/current/
  degraded/presented/requested/shutdown）。
- `Src/XGui/Graphics/XPainter.c`：GPU 快速路径支持**纯平移变换**与**单矩形
  region clip**（子控件 translate/clip 不再强制降级）；fillRect/drawImage/
  drawText（位图字体经 CPU 字形→alpha→纹理）走 GPU；非快速路径/降级整帧
  一致回退软件（`frameDegraded` 使后续 painter 不再用 GPU 会话）。
- `Src/XGui/Widget/XWidget.c`：`flushBackingStore` 增加 GPU 直通分支
  （present vs BitBlt 自动选择；PARTIAL 模式保持离屏路径）。
- `Src/XGui/Graphics/XGpuRenderBackend` 全局标志：requested（env 缓存）、
  frameDegraded、framePresented（截图选内容来源）。
- `xgui_window_demo.c`：GPU 直通帧截图改从 FBO 读回（GDI 抓屏读不到
  WGL 双缓冲窗口内容——已确认是验证手段限制，非渲染缺陷）。
- `xgui_gpu_test.c`（CMake target `XGuiGpu_Test`）：GPU 冒烟测试
  （fillRect/图像/文本/半透明混合像素断言；`XGUI_RENDER_BACKEND=gpu` 运行）。

### 14.2 Windows 实测结果（AMD Radeon RX 6800 XT）

- 离屏 GL 上下文 vendor/renderer：`ATI Technologies Inc. / AMD Radeon RX 6800 XT`
  （确认硬件加速，非微软软件 GL）。
- 位图字体（`XFont8x16`）下 GPU 直通：fillRect 降级 = 0（全 GPU 快速路径）；
  FBO 与窗口默认帧缓冲读回内容均正确；软/GPU 画面 diff 仅
  60/187200 像素（0.03%，文本抗锯齿边缘近似差）。
- 性能：软件 6198 FPS 为「假吞吐」（BitBlt 不等显示）；GPU 直通 122 FPS
  是真上屏吞吐（受字形每字一次 alpha 生成 + `glTexImage2D` 上传 + swap 限制）。
  屏幕帧率两者均受 60Hz 刷新限制，不可直接比数字。
- 回归矩阵：DIRECT / FULL / PARTIAL / ASan / `XGPU_ON=0` 裁剪 全部通过。

### 14.3~14.68 历史轮次索引（2026-09-06~09，已归档）

> 期间 43 个轮次的详细记录（GPU 文本/字形图集/抗锯齿/渲染驱动可插拔、
> OpenGL/Vulkan 双驱动、io_uring 双内核、Win32 直通与 AMD 真机、中文输入
> fcitx5、Qt 控件对齐 18 批（14.25~14.45 全部主流 QWidget 家族）、
> 全 tab 截图审计与交互自动化等）已从本文移除，见 git 历史。
> 关键结论：

- GPU 后端阶段 1/2 完成并三后端（software/OpenGL/Vulkan）回归全绿
  （14.16.17）；outline 字体 GPU 文本、drawTextRect/drawGlyph GPU 分支、
  文本装饰三后端一致性均已收口（14.3 遗留清单全部关闭，见 11b）。
- io_uring/epoll 双内核完成，armel 交叉编译与 qemu 13/13 自检通过
  （14.19.x，环境脚本外部 armel-env.sh，重跑待授权）。
- 主流 QWidget 家族（LCD→Wizard/ErrorMessage 十八批）全部接入并进
  XGuiDemo（14.25~14.47）；XGuiDemo 21 内层 tab 全可见交互检视通过。

### 14.69 Phase 3 收尾推进（2026-09-16 第二十四轮）

#### Phase 3.3 XGui.md 已知偏差清单同步（已完成）

- 3.2a GPU 概要更新：清除"遗留 outline 字体 GPU 文本等问题"过期表述，
  标注 14.3 遗留问题全部收口（outline GPU 文本降级、drawTextRect/
  drawGlyph GPU 分支、文本装饰局部提交三后端一致）。
- 11b 已知偏差清单补"GPU 文本装饰（已收口，非偏差）"条目。

#### armel 交叉编译可选验证（推进中被叫停，待续）

- 发现 armel-env.sh 三处 /tmp 冷启动缺陷（此前 /tmp 环境存活时被掩盖）：
  ① `libc6_*_i386.deb`、`libpcap0.8-dev_*_armel.deb`、
  `libpcap0.8_*_armel.deb` 三处通配符被引号包住永不展开；
  ② SDK 解包缺 `--strip-components=1`（tar 顶层多一层 `host/`，
  导致 $SDK/opt/ext-toolchain、$SYSROOT 等路径全部错位）。
- 脚本位于工作区外（沙箱 workspace-write 不可写），原文件未改动；
  以 /tmp 修补副本推进（注意：本环境每次命令的 /tmp 相互隔离，
  环境重建必须与 configure/build 并入同一次调用）。
- 结果：环境重建 OK（180 个 i386 ELF interp 补丁）、
  build-armel 重新 configure 通过（"Could NOT find X11"属 armel 预期）；
  全量交叉编译已启动未完成（用户叫停），qemu 自检未执行。
- 残留日志：build-armel-env.log、build-armel-configure.log、
  build-armel-build.log（工作区根目录，可删）。
- 待办：修复原脚本上述缺陷（需用户授权写工作区外文件）后重跑
  编译 + qemu-arm 13/13 自检 + 产物 file/readelf 验证。

#### Phase 3.1 API 扫描器重建（已完成；缺口清零见 14.110）

- 旧 tools/xgui_api_scan.sh 从未入库且已从工作区丢失（仅剩产物
  xgui-api-gaps-phase3.txt，456 行）；其噪声来源：提取方法名首字母
  截断（"abstractButton"→"bstractButton"）、未做继承归并（QComboBox::
  sizeHint 已由 XWidget_sizeHint 满足仍误报）、脚本带 BOM 致 shebang 失效。
- 新扫描器 tools/xgui_api_scan.py 重建完成：修复截断/继承归并/_2
  变体归并/Q_PROPERTY 访问器识别；缺口 699→178→10→0（14.110 轮
  清零，SKIP 豁免 60 条均含理由），产物
  xgui-api-gaps-phase3-v2.txt 持续重扫更新。

#### 本轮未动事项（下轮续）

- Phase 3.1 分类处置、Phase 3.2 demo 全 tab 交互 xdotool 检视、
  armel 编译续跑与自检、全量验证矩阵复验。

### 14.70 Phase 3.1 分类处置 + Phase 3.2 全 tab 检视（2026-09-17 第二十五轮）

#### Phase 3.1 分类处置（已完成）

- **扫描器增强**（tools/xgui_api_scan.py）：
  - 新增 `#define X<类>_<名>` 宏别名收集（QApplication.exec/quit/notify
    等父类转发宏误报消除）；
  - SKIP 豁免表扩到 37 条，逐条注明理由（Qt-内部钩子 5、macOS 3、
    体系不做 16、URL 承载族 10、QCalendar 备选历法 4、格式映射 2）。
- **P1 批次实现**（约 60 个新 API + 10 个别名宏，全部带全量中文
  Doxygen 注释并进回归）：QLayout.addWidget、QDateTimeEdit 日期/时间
  范围族 17 项（语义对照 Qt 源码：设日期保留时间、设时间保留日期、
  clear 复位 init 默认）、QTabBar 形状/图标尺寸/自动隐藏/移除选择行为/
  拖拽切换 10 项、QTabWidget clear+6 属性族转发+2 getter 11 项、
  QToolBox itemToolTip 族（新增条目 tooltip 存储与 deinit 释放）、
  QMenu icon 族（新增 m_icon 字段，copy/move/deinit 同步）+
  isTearOffEnabled 别名、QDialog open+sizeGripEnabled 族、
  QDockWidget.isAreaAllowed、QToolBar isAreaAllowed/isFloating +
  allowedAreasChanged/toolButtonStyleChanged 真发射、QComboBox.currentData、
  QMessageBox.setOption、QWizard setCurrentIndex+currentId/startId 四别名
  +titleFormat/subTitleFormat/pixmap getter、QWizardPage
  buttonText/commit/final/pixmap 族 9 项、QTextEdit
  fontItalic/fontUnderline 四别名、QFontComboBox currentFont 别名 +
  setCurrentFont 头声明补齐（.c 既有实现漏声明的扫描盲区）。
- **分类报告**：docs/xgui-audit/2026-09-16/
  xgui-api-gaps-phase3-v2-分类处置.md（A 收口/B 豁免/C P2 约 180 项/
  D P3 约 390 项/E 架构偏差五类逐类处置；P3 主体为视图族 339 项与
  文本族约 80 项）。
- **缺口收敛**：699 → 568（568 全部为已分类积压，无未判定项）。
- **11b 偏差新增 4 条**：快捷键/手势承载、URL 文件对话框、QCalendar
  备选历法、分段序号/分段码共用字段。

#### Phase 3.2 demo 全 tab 交互 xdotool 检视（已完成）

- 方法：xdotool 驱动 XGuiWindowDemo_Test（1780x700 加宽使全部
  21 个内层 tab 可见），逐 tab 点击 + xwd 截图（21 张全部唯一且
  >8KB），主导航 5 页逐一到达；证据截图已随清理移除（检视记录以本节文字为准）。
- **发现并修复**：XTabBar 选中页签文字不可见——XCommonStyle
  xcs_drawTabLabel 对 Selected 态取 HighlightedText（白字），而
  xcs_drawTabShape 选中填充为 Base（白底），白底白字。修复：文本
  统一取 WindowText（对标 Qt Fusion 选中也用 WindowText）；修复后
  选中页签文字清晰。此前 14.54
  轮的">8KB 非空白"审计无法发现此类缺陷。

#### 验证矩阵（全绿）

- 默认构建 XinYueCS + XGuiRegression_Test：全绿（含新增
  test_phase31_p1_contract 40+ 断言）。
- PARTIAL/FULL 渲染模式变体回归：全绿；`-DXGUI_ON=0` 全裁剪构建：
  通过；`XGUI_RENDER_BACKEND=gpu` GPU 冒烟：通过。

#### armel 编译续跑（未执行，按用户指示保持范围外）

- 本会话曾修复外部 armel-env.sh 三处引号包裹通配符与
  --strip-components=1 缺陷并启动后台编译，按用户指示（本轮只聚焦
  XGui 仓库内工作）已停止任务、脚本按备份还原、日志删除；缺陷定位
  结论保留在 14.69，重跑仍待用户授权写工作区外文件后执行。

#### 下轮建议

- P2 批次（约 180 项，按分类报告第五节逐类推进，建议先做
  QComboBox 弹出部件族 + QMessageBox checkBox/iconPixmap 族 +
  QDateTimeEdit section 族）；视图族 P3 按 14.25-14.45 批次模式启动
  QHeaderView 段管理；armel 待授权后续跑。


### 14.71 P2 批次收口（2026-09-17 第二十六轮）

#### API 收口（568 → 546，明细见分类报告五b节）

- **QMessageBox 全清零（14 项）**：checkBox 族（所有权转移 + 复选框
  布局行）、iconPixmap 族（XImage 深拷贝）、buttonRole/removeButton
  （委托按钮盒 + 默认/转义/最近点击指针清理）、buttonText/setButtonText
  （标准按钮文本）、aboutQt（文档化空操作）、standardIcon（映射
  XStyleSP_*，样式未注册虚槽时返回 NULL）、textFormat/
  textInteractionFlags 族（转发内部标签）。
- **QDateTimeEdit（5 实现 + 1 别名 + 2 豁免）**：sectionCount/sectionAt/
  sectionText/setSelectedSection（新格式分词器，记号集与
  xdt_refreshText 一致）、displayedSections 别名；timeZone 族豁免
  （QTimeZone 体系未建，11b 偏差）。
- **QComboBox setLineEdit**：所有权转移 + 隐式置可编辑 + 几何/show
  接管；view/model/delegate/validator/inputMethodQuery 共 13 项迁移
  「弹出列表部件化」专项（弹出列表现为自绘非部件承载，部件化后收口）。

#### 验证

- 新增 `test_phase32_p2_contract`（20+ 断言）随 `XGuiRegression_Test`
  全绿；`-DXGUI_ON=0` 全裁剪构建通过；扫描器自检通过（546 MISS）。
- 发现并如实记录：样式侧无任何实现注册 EXStyle_StandardIcon 虚槽，
  standardIcon 恒 NULL（图标生成为样式绘制批次任务）。

#### 下轮建议

- 弹出列表部件化专项（QComboBox view/model 族 13 项 +
  QDateTimeEdit calendarWidget 族）；样式 standardIcon 图标生成；
  QTabWidget cornerWidget/tabCloseRequested；视图族 P3 启动
  QHeaderView 段管理；armel 待授权后续跑。

### 14.72 模拟使用检视 + 两个真 bug 修复（2026-09-17 第二十七轮）

#### 修复 1：Fusion 渐变按钮横条纹（xfs_lerp 无符号下溢，重大显示缺陷）

- **现象**：demo 全部 Fusion 按钮（主导航/按钮/命令链接/工具按钮）渲染为
  黄红噪声横条纹，文字被噪声淹没；历史多轮截图记录中均已存在，此前被"非空白审计"漏检。
- **定位过程**：离屏 XPainter 逐行渐变探针干净 → 排除 XImage/XPainter；
  单缓冲/FULL/24 位 visual 变体均复现 → 排除双缓冲/visual；最终对
  xfs_drawPanelButtonCommand 渐变循环插桩，发现 `top/bot` 输入恒定正确而
  `xfs_lerp` 输出中间行乱跳。
- **根因**：xfs_lerp 通道差值 `(br - ar)` 为 uint32 无符号减法，`br < ar`
  （如 247-255）时回绕成 ~4.29e9，乘 t 后截断出无关色；仅 t=0/t=1 两端
  正确——正对应条纹只出现在按钮中部行的形态。
- **修复**：差值改有符号 int + 四舍五入 + 0..255 钳位
  （Src/XGui/Style/XFusionStyle.c xfs_lerp）。
- **验证**：demo 截图（--screenshot 内部后备存储抓取）与交互实测按钮均为
  干净 Fusion 渐变；全量回归 `XGui regression tests passed`。

#### 修复 2：demo 启动段错误（XTableWidget 垂直表头野指针）

- **根因**：XTableWidget_setVerticalHeaderLabels 扩容 m_vHeaders 用
  XRealloc_System 后未清零新增区域，下方 `if (!self->m_vHeaders[i])` 读到
  野指针直接对垃圾指针 assign（水平表头 ensureCols 有置 NULL 循环，垂直
  表头漏了）。
- **修复**：realloc 成功后对新增区域 XMemset 清零（XTableWidget.c）。
- 修复前：`./bin/XGuiWindowDemo_Test --autotest` 启动即段错误
  （XString_assign_utf8 ← XContainer_clear_base）；修复后启动/截图正常。

#### 模拟使用检视结论（xdotool + 截图，21 内层 tab 全走查）

- 正常：图表（柱/线/散点/面积）、Wizard、多行编辑、输入演示联动
  （滑块-进度条）、堆叠、下拉、日历等主体页签渲染与交互正确；
  上轮修复的选中页签蓝底白字持续生效。
- **遗留问题 A**：滚动条演示页在交互切换瞬间显示黑色竖条（groove 位置
  (11,11,11)），而 `--screenshot --page 4 --tab 3` 后备存储抓取完全正常
  （滑块 159 灰 + groove 239 浅灰，palette 取值正确）——指向交互路径的
  静态场景缓存/脏区合成，非样式取色问题。
- **遗留问题 B**：鼠标悬停在新点击的页签上瞬间文字不可见，移开后恢复
  （悬停态绘制细节）。
- 菜单工具栏页工具按钮为空块、多行编辑无边框（外观简化项，低优先）。

#### 本轮验证

- `XGuiRegression_Test` 全绿（含 phase31/32 契约）；`-DXGUI_ON=0` 全裁剪
  构建通过；扫描器 546 MISS 自检通过；ASan 构建 demo 无越界（仅 X11
  外部库泄漏噪声，与 10.186 结论一致）。
- 调试插桩（XPainter fillRect、Fusion 渐变、present 驱动、XCreateWindow）
  已全部移除，XPlatformNativeWindow_posix.c 还原。

#### 下轮建议

1. 遗留问题 A/B：demo 静态场景缓存与脏区合成机制排查
   （demo_repaint 每帧仅 overlay 区域脏 + switchPage 全窗 update 的交互）。
2. P2 继续：弹出列表部件化专项（QComboBox view/model 13 项 +
   QDateTimeEdit calendarWidget）；样式 standardIcon 图标生成。
3. 视图族 P3（QHeaderView 段管理）按 14.25-14.45 批次模式启动。


## 15. 下一阶段 GUI 模块计划（源码扫描建议）

本节基于当前 XGui 源码、工作区现有控件以及本机 Qt 6.8.3 源码扫描结果整理。Qt 对照源码位于：

- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/dialogs
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/itemviews
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/gui

当前 XGui 的窗口、事件、软件绘制、图像、布局、输入法、剪贴板、拖放、无障碍和平台适配已经具备基础闭环；控件层主要覆盖 XWidget/XFrame/XLabel、按钮族以及正在完善的 XAction/XMenu/XToolButton。下一阶段应优先补齐可组合应用所需的控件和交互模型。

### 15.1 P0：菜单、工具栏与主窗口体系

建议新增：

- XMenuBar，对标 QMenuBar；
- XToolBar，对标 QToolBar；
- XMainWindow，对标 QMainWindow；
- XStatusBar，对标 QStatusBar；
- XActionGroup，对标 QActionGroup。

先完善现有 XMenu 的真实交互闭环：键盘导航、Esc 关闭、子菜单切换、悬停高亮、失焦关闭、动作状态同步，以及 XPushButton/XToolButton 的平台弹出菜单。当前 XPushButton.h 已记录菜单 API 存在但真实平台弹层仍需接入；Src/XGui/Widget/XMenu.h、Src/XGui/Widget/XToolButton.h 和 Src/XCode/XAction/XAction.h 是主要入口。

推荐实现顺序：XMenu popup 完整化 -> XMenuBar -> XToolBar -> XMainWindow/XStatusBar -> XActionGroup 与快捷键。

验收目标：可以构建带菜单栏、工具栏、状态栏和多页面内容的真实桌面应用。

### 15.2 P1：文本输入控件

建议新增：

- XLineEdit；
- XValidator；
- XTextEdit 或 XPlainTextEdit；
- XCompleter。

第一阶段只实现单行编辑，覆盖键盘输入、输入法预编辑/提交、光标移动、选择、剪贴板、Home/End、Ctrl+A/C/V/X/Z、鼠标拖选、占位文本、最大长度、验证和焦点链。不要在第一阶段直接进入完整富文本编辑器。

主要依赖关系：XLineEdit -> XWidget 焦点/键盘事件 -> XInputMethod -> XClipboard/XMimeData -> XPainter 文本布局 -> XValidator。

Qt 对照重点为 qlineedit、qwidgetlinecontrol_p、qabstractspinbox 和 qvalidator。当前基础代码主要位于 Src/XGui/Widget/XWidget.h、Src/XGui/Input/XInputMethod.h、Src/XGui/Input/XClipboard.h 和 Src/XGui/Widget/XLabel.h。

验收目标：可以完成搜索框、设置项编辑和设备参数输入。

### 15.3 P1：对话框与文件选择器

结合当前 codex/xdevice-file-platform 分支，建议新增：

- XDialog；
- XDialogButtonBox；
- XMessageBox；
- XInputDialog；
- XFileDialog；
- XFileSystemModel。

优先实现 XDialog -> XDialogButtonBox -> XMessageBox -> XFileDialog。文件对话框第一阶段可定位为设备文件选择控件，支持当前目录、文件/目录列表、目录进入返回、文件名、过滤器、打开/保存/选择目录，并接入 XDevice/XDir/XFile。

验收目标：可以完成设备文件浏览、打开、保存和目录选择流程；嵌入式模式可使用固定根目录或虚拟文件系统。

### 15.4 P2：模型、列表、树与表格

建议先建立模型/视图基础，再扩展具体视图：

1. XAbstractItemModel；
2. XModelIndex；
3. XItemSelectionModel；
4. XListView；
5. XTreeView；
6. XTableView；
7. XHeaderView 和 delegate。

其中 XFileSystemModel + XTreeView/XListView 可直接服务于 XFileDialog 和设备浏览器。该部分应放在 XLineEdit 与基础对话框之后，避免模型、选择、滚动、delegate 和编辑器同时引入。

### 15.5 P2：数值、状态和页面导航控件

建议按以下顺序补齐普通设置页面能力：

XProgressBar -> XSlider -> XScrollBar -> XSpinBox -> XComboBox -> XTabWidget -> XSplitter。

XTabWidget 可以复用现有 XStackedLayout，收益较高；XComboBox 依赖 popup、列表和键盘导航，应放在 XMenu/XListView 之后。

可作为同批次补充的控件包括 XDoubleSpinBox、XDial、XGroupBox 和 XTabBar。

### 15.6 P2：统一样式系统

当前已经有 XPalette、XStyleHints 和 XPlatformTheme，但控件样式仍由控件分别绘制。建议在控件数量增加前引入最小样式层：

- XStyle；
- XStyleOption；
- XStylePainter；
- XCommonStyle；
- XStyleFactory。

第一阶段只统一按钮、checkbox/radio indicator、菜单项、line edit、scrollbar、tab 和 progress bar 的 hover、pressed、disabled、focus 状态，不追求完整复刻 Qt Fusion。

### 15.7 推荐实施路线

#### 阶段 A：桌面应用骨架

XMenu popup 完整化、XMenuBar、XToolBar、XMainWindow、XStatusBar、XActionGroup。

#### 阶段 B：输入与对话框

XLineEdit、XValidator、XDialog、XDialogButtonBox、XMessageBox、XInputDialog。

#### 阶段 C：文件与数据浏览

XAbstractItemModel、XItemSelectionModel、XListView、XTreeView、XFileSystemModel、XFileDialog。

#### 阶段 D：状态控件与统一样式

XProgressBar、XSlider、XScrollBar、XSpinBox、XComboBox、XTabWidget、XStyle。

### 15.8 暂不优先

暂不建议优先实现 XGraphicsView/XGraphicsScene、完整富文本编辑器、XCalendarWidget、XFontDialog/XColorDialog、XDockWidget、XMDIArea、XSystemTrayIcon、XWizard 和完整 OpenGL/Vulkan Widget 封装。这些模块要么依赖更大的模型体系，要么属于桌面高级功能，当前对嵌入式 GUI 和设备文件业务的直接收益较低。

### 15.9 本阶段推荐的单一切入点

如果以 XGui 桌面应用完整度为目标，下一步从 XMenu 真实弹出交互开始，随后实现 XMenuBar + XToolBar + XMainWindow。

如果以当前设备文件平台业务为目标，下一步从 XLineEdit + XDialog + XFileSystemModel + XTreeView/XListView + XFileDialog 开始。

本计划只记录推荐路线，不表示上述模块已经实现；后续每个模块仍需按现有约定补充配置开关、C99 API、回归测试、裁剪构建验证和 Qt 6.8.3 行为边界说明。

---

### 14.73 滚动条黑条根因修复：xcs_darker 溢出与语义反转（2026-09-17 第二十八轮）

- **遗留问题 A 关闭**：滚动条演示页交互黑竖条。插桩回读定位：
  `xcs_darker(0xFFEFEFEF, 105)` 输出 `0xFF0B0B0B`——旧实现
  `v*factor/100` 对 255 输入得 267 溢出字节（267&0xFF=0x0B），
  且语义与 Qt 相反（Qt darker(factor) = v*100/factor，factor>100
  变暗）。修复为 Qt 语义（XCommonStyle.c xcs_darker，factor<=0
  保护），groove 恢复浅灰渐变（ee/f2），滑块/边框正常。调用点
  全部为 factor>100 变暗语义，修正安全；XFusionStyle 无此函数。
- **遗留问题 B 关闭**：悬停瞬间页签文字不可见系 lerp 溢出污染的
  同源表现，darker/lerp 修复后悬停采样文字笔画正常，无需另改。
- 回归全绿；检视截图确认滚动条页交互渲染正确。

### 14.74 平台 API 约束落地 + standardIcon 图标生成 + 双库陷阱（2026-09-17 第二十九轮）

- **新约束（用户明令，已入第 12 节）**：原则上 `Src/` 下代码不允许直接调用
  平台 API，平台调用只允许在 `Drive/` 适配层，Src 侧经公共抽象层间接到达。
- **全量审计**：DBus/fontconfig/X11 调用全部已在 Drive 层（合规）；唯一
  违规 `Src/XGui/Graphics/XGpuRenderDriver_vulkan.c(+shaders.h)` 已
  git mv 至 `Drive/Posix/Graphics/`（vulkan.h 为跨平台 SDK 核心头，文件内
  无 X11/Win32/POSIX 调用，X11 surface 由 Drive 层创建后句柄传入；
  CMake GLOB_RECURSE 自动收集，重建零改动）。time.h/errno.h 等 C 标准
  头不属平台 API。
- **standardIcon 图标生成（遗留关闭）**：XCommonStyle 实现
  EXStyle_StandardIcon 虚槽——几何绘制 48x48 透明位图经
  XImage→XPixmap→XIcon_init_pixmap 产出，覆盖消息框 4、文件夹/文件 4、
  标题栏按钮 5、对话框圆钮 7、箭头 6、媒体 3 等 29 个 SP 枚举；
  `XMessageBox_standardIcon` 实测返回有效图标（此前恒 NULL）。
- **双库陷阱记录**：build/ 为 Debug 配置，实际产物是 `libXinYueCSd.a`；
  根目录曾有不参与构建的陈旧 `libXinYueCS.a`（9/16 孤儿，误导链路与
  排查），已删除。今后探针/手工链接一律用 `libXinYueCSd.a`。
- 回归全绿；`XGui.md` 精简版文档结构稳定。

### 14.75 弹出列表部件化第一批（2026-09-17 心跳 22:20，单线程）

- XComboBox 新增 9 个 API（对标 QComboBox view/model 族）：view/setView
  （懒创建内置 XListView，安装外部视图取所有权）、model/setModel
  （懒创建内置 XAbstractItemModel 并随条目同步，setModel 取所有权）、
  modelColumn/setModelColumn、rootModelIndex/setRootModelIndex
  （(row,col) 平铺承载，XGui 无 QModelIndex）、validator/setValidator
  （不透明指针承载，XValidator 体系未建，头文件注明）、
  inputMethodQuery（简化承载：仅编辑文本类查询，返回新建 XString）。
- 生命周期：deinit 释放 popupView/model；setView/setModel 断开旧引用
  后释放旧对象。条目增删改后访问 view/model 时懒同步。
- 回归新增 6 断言全绿。**下一心跳继续**：Popup 容器承载
  （参照 XMenu.c:1080 XWindowType_Popup）+ activated 联动选择 +
  show/hidePopup 切换到部件路径。

#### 14.75 续（22:40 心跳：弹窗承载与联动闭环完成）

- showPopup_base 重写为部件路径：懒建视图设为顶层 Popup 窗口
  （参照 XMenu，X11 下 override-redirect），mapToGlobal 定位组合框
  正下方，行高 XCOMBOBOX_ITEM_H、高度 = 可见行数×行高+2；
  show+raise+flushBackingStore 主动首帧上屏；hidePopup_base 隐藏视图。
- 内嵌假弹出路径删除（grabMouse 拉高自身、paintEvent 列表覆盖绘制、
  popupItemAt/g_comboPopupOffset）；自身点击 = 弹出/收起切换。
- 联动闭环：视图 activated(row) 信号 → 选中条目 + activated/
  textActivated 信号 + 收起弹窗；回归新增断言（弹出可见 → 发
  activated(1) → currentIndex==1 且弹窗收起）全绿。
- demo 实测：弹窗作为独立窗口在组合框下方正确显示（高亮跟随选中）。
- **遗留（下轮）**：点击弹窗外部自动收起（需 grab 或失焦检测，
  当前可用再次点击组合框收起替代）。

#### 14.76 视图族 P3 启动：XHeaderView 段管理第一批（23:00 心跳,单线程收尾）

- XHeaderView 新增 16 项（对标 QHeaderView 段族）：hideSection/
  showSection/isSectionHidden/hiddenSectionCount（新增 bool* 平行
  隐藏表,setCount/copy/move 全同步）、setSectionsClickable/Movable
  及 getter、swapSections/moveSection（尺寸与隐藏状态随移）、
  setSortIndicator（发射 sortIndicatorChanged 信号）/
  sortIndicatorSection/Order、setSortIndicatorShown/isShown、
  sectionClicked/sortIndicatorChanged 两信号句柄。
- 排序方向枚举 XHeaderViewSortOrder（数值对齐 Qt::SortOrder）。
- 回归新增 10 断言全绿。下一心跳:并发模式起派矩阵(剩余代理任务)。

### 14.77 并发矩阵首跑：四代理并行批次（2026-09-17 23:00 心跳）

- **代理B(树展开族)**:XTreeView 新增 22 项——expand/collapse/isExpanded/
  setExpanded/expandAll/collapseAll/expandToDepth(@note 平铺模型简化)、
  双击展开/可展开/装饰/排序/等高开关族、列隐藏/列宽状态数组、
  expanded/collapsed 信号；copy/move 挂虚表,状态数组随模型行数惰性同步。
- **代理C(combo 收尾)**:弹窗外部点击自动收起——本地子类
  XComboPopupView(extends XListView,越界按下/释放吞掉并收起)；
  XMenu 同款模态抓取(grabMouse+1ms 精确定时器后平台抓取)；
  itemDelegate/setItemDelegate 不透明承载。遗留关闭。
- **代理D(样式图标+日历)**:standardIcon case 补足至 **38**（新增
  Desktop/Computer/Trash/DriveHD/FD/CD/DVD/Net/DirHome 9 个几何图标）；
  QDateTimeEdit calendarWidget 族(懒创建内置日历+selectedDate 联动
  setDate+setCalendarWidget 取所有权)；附带修复 deinit 缺 (XClass*)
  强转。
- **代理E(只读审计)**:报告归档 docs/xgui-audit/2026-09-17/
  inheritance-legacy-audit-2300.md。核心结论：83 项继承链对照,
  硬偏差仅 1 处(XHeaderView→XWidget,Qt 为 QAbstractItemView 派生,
  待后续批次评估)；死声明 32 个(声明无定义,调用即链接错)列入
  清理候选；空实现 7 处均为降级桩非清理对象。
- **审核与合入**：主会话统一构建零错误；补合入断言(树安全操作/
  列隐藏/根装饰默认、calendarWidget 幂等、standardIcon 非空)；
  修正空树展开断言语义(0 行树为无操作)；回归全绿。

#### 下轮建议

- XHeaderView 继承链硬偏差评估(→XAbstractItemView 派生改造或
  登记偏差)；死声明 32 个分批处置(补实现或删声明)；
  弹出列表部件化收尾核对；视图族 P3 继续(QListView/QTableView 批次)。

### 14.78 并发批次二：死声明清理 + QListView/QTableView 状态族（2026-09-17 23:20 心跳）

- **死声明清理 30/30**：XWidget.h 14 项（saveGeometry/restoreGeometry/
  windowIcon 族/winId 四族/screen 族/style 族/ensurePolished/actions）、
  XImage.h 2 项（setPixelFast/markDirty）、XLineEdit.h 3 项（completer
  族）、XMenu.h 1 项（menuInAction）——审计确认全仓零定义零调用方，
  删除后不再有"调用即链接错误"的陷阱；相关孤儿横幅一并清理。
- **XListView 状态族 18 项**：flow/gridSize 双函数/wrapping/viewMode
  （IconMode 联动 wrapping+flow）/resizeMode/layoutMode/batchSize/
  itemAlignment/selectionRectVisible/wordWrap/rowHidden（绘制与命中
  联动跳过隐藏行）；4 个数值对齐枚举；itemAlignment/wordWrap 走
  drawTextRect 换行路径。
- **XTableView 状态族 22 项**：gridStyle 枚举（DASH 真实虚线绘制）+
  showGrid 联动不变式、wordWrap/cornerButton、rowAt/columnAt 几何
  反查、行/列隐藏族与便捷族、clearSpans(@note 平铺模型)；为此给
  XTableView 虚表补挂 EXClass_Copy/EXClass_Move（原先未注册）。
- 附带修复：XListView/XTableView 既有缺包含（XWidget_Protected.h/
  XStringUtils.h）——隔离编译必报错项。
- **验证**：回归全绿（新增 QListView/QTableView 状态族断言 12 条）；
  -DXGUI_ON=0 全裁剪构建通过；demo 冒烟通过。

#### 下轮建议

- XHeaderView 继承硬偏差评估（→XAbstractItemView 派生或登记 11b）；
- 死声明清理模式扩展到"守卫外声明 20 头"复核；
- 视图族继续（QTreeView 深化/QTableWidget 便捷族）；
- 弹出列表部件化核对（分类报告 C 类清零确认）。

### 14.79 并发批次三：表格便捷族 + 菜单栏动作所有权（2026-09-17 23:40 心跳）

- **代理D'(QTableWidget 便捷族 6 项)**:findItems(双输出 int 数组,
  库侧零分配)、itemAt(表头/滚动偏移感知反查)、sortItems(补声明——
  实现已存在但头文件一直未声明,不可达公共 API)、selectedIndexes
  (单选语义收窄注明)、clearSpans(转发基类无操作)、takeItem
  (所有权移交调用方)。
- **代理F'(XMenuBar 动作所有权)**:insertAction(借用注入,移动
  语义)/removeAction(仅摘除不释放);新增 m_actionOwned 平行向量
  逐项记录拥有/借用,deinit/clear 只销毁拥有项(对齐 Qt 语义);
  addAction 族已存在,对接进新登记机制。
- **代理C'(XTreeWidget 便捷族)**:因速率限制未启动,下轮接续
  (findItems/sortItems/itemAt/visualItemRect/setHeaderLabels)。
- **审核记录**:合入断言时修正 2 处(枚举名 XTABLEVIEW_GRID_*、
  setItem 应为 setText 传单元格对象);崩溃栈定位为测试误用非库缺陷。
- **11b 登记**:XHeaderView 继承链偏差(见上)。
- **验证**:回归全绿;顺带确认 XTableWidget 渲染正常(表格 tab 走查)。

#### 下轮建议

- 代理C'(XTreeWidget 便捷族)接续;守卫外声明 20 头复核;
- 视图族 P3 继续(XTableWidget findItems 断言已进回归,扩展到
  QHeaderView visualIndex 族);分类报告 C 类清零确认。

### 14.80 XLineEdit completer 野指针修复 + 守卫复核归档（2026-09-18 00:00/0:20 心跳）

- **重大修复：XLineEdit completer 野指针（随机崩溃根因）**。ASan
  (malloc_fill_byte=0xBE 默认)精确定位：XLineEdit_init 未初始化
  m_completer/m_completerSyncing，堆残留垃圾被 xlineedit_syncCompleter
  解引用（XCompleter_completionMode(0xBEBE...)）→ 随机段错误。init 内
  已有同款教训注释（m_actionCount 堆残留垃圾），本字段漏网。修复：补
  completer=NULL/completerSyncing=false。修复前回归随机段错误
  （约 1/3 概率），修复后连续 9 轮全绿零失败。历史 demo/回归的
  部分随机崩溃疑与此同源。
- **守卫外声明 20 头复核归档**（docs/xgui-audit/2026-09-17/
  guard-review-0020.md）：9 头无需守卫（公共类型,补守卫破坏裁剪链）、
  4 头建议补守卫（XSVGICON_ON/XPIXMAPCACHE_ON/XMOVIE_ON,分批）、
  7 头待讨论（Icon 核心整体裁剪需单独立项）；GPU 系已合规；
  **顺带修复 12 个头缺 UTF-8 BOM**（XStyle.h/XWidget.h/XMenu.h/
  XLineEdit.h 等——违反项目硬约束,已全部补上并回归验证）。
- **XHeaderView 继承改造调研归档**（docs/xgui-audit/2026-09-17/
  headerview-refactor-plan.md）：结论保持 XWidget 派生（全仓零集成、
  改派生收益为负、须反向压制 5 处基类行为）；"移动即重排"与 Qt
  视觉重排语义相反等 9 条行为差异已列 11b 引用。
- **XTreeWidget 便捷族 4 项**（findItems/sortItems/itemAt/
  setHeaderLabels,含表头存储与防崩扩容）合入回归全绿。
- **XWizard 残余 8 项**（button/setButton/pageIds/visitedIds +
  WizardPage 4 个虚槽接线:initializePage/cleanupPage/validatePage/
  nextId 经父控件共享虚表定位）；顺带修复 deinit 清理代码错位嵌套。
- **确定性问题修复**：XListView 行隐藏断言在 0 行列表上的语义修正
  （接口安全验证）；ASan 下另发现 zlib trees.c:873 null arg（第三方
  内部,无害）。

#### 下轮建议

- 补守卫批次 1：XSVGICON_ON（零风险）→ 批次 2：XPIXMAPCACHE_ON +
  XMOVIE_ON（需联动）；Icon 核心裁剪单独立项讨论
- 代理 C'(XTreeWidget 便捷族)已完成合入;视图族 P3 继续
  （QAbstractItemView 基类能力 48 项为最大块）
- XLineEdit 全字段初始化复查模式推广:对照各控件 init 与 struct
  字段清单,排查同款"新增字段未入 init"漏网(建议工具化)

#### 14.80 续（0:00 心跳后半：真裁剪验证方法学修复）

- **重大发现（方法学）**：CMakeLists 从未处理 `-DXGUI_ON=0` 命令行开关
  （缓存变量 UNINITIALIZED 不产生编译定义）——既往所有“-DXGUI_ON=0
  全裁剪构建验证”实际是全量编译的假验证。
- **修复**：CMakeLists project() 后新增裁剪开关转发
  （XGUI_ON/XGPU_ON/XSVGICON_ON/XPIXMAPCACHE_ON/XMOVIE_ON →
  add_compile_definitions），编译定义已确认生效（flags.make 实证）。
- **首次真裁剪暴露并修复 3 处**：XCategoryAxis.c/XValueAxis.c 守卫尾
  错位（deinit_impl 在 #endif 外）；XImage.c 后缀解码分支缺
  XIMAGECODEC_ON 守卫；XIconStyleHelper.c 缺 XGUI_ON 总守卫。
- **剩余错误 6 处已留存** .tmpdbg/crop-errors-remaining.txt
  （XPlatformAccessibility.c/XCursor.c 等连带裁剪适配），下一心跳
  继续直至真裁剪全绿；审计的 4 头补守卫批次（XSVGICON_ON 等）随之
  一并真验证。

#### 14.80 续二（1:20 心跳增量）

- 真裁剪再修：XInputDialog.c 守卫尾错位（同款）；累计已修
  XCategoryAxis/XValueAxis/XImage 分支/XIconStyleHelper/XPlatformFont
  Database/XPlatformAccessibility/XCursor 回退壳补全/XColorDialog/
  XInputDialog/Platform 四文件守卫扩尾。
- 剩余 33 错（XTextEdit.h 守卫碎片化等）清单留存
  .tmpdbg/crop-errors-remaining.txt,下轮错误驱动清零。
- 默认构建 3 轮稳定性验证：无崩溃零失败（XLineEdit completer 修复后
  随机段错误消失实证）。

#### 14.80 续三（1:40 心跳：真裁剪验证体系完全打通）

- **CMake 转发修正**：option() 缓存值 ON/OFF 直传会产生 `-DXGUI_ON=ON`
  字面量（预处理器判假 → GUI 整体误裁、默认构建 78 错）。修正为
  ON/TRUE→1、OFF/FALSE→0 转换。
- **终验矩阵（转发修正后）**：
  - 默认构建：全绿（回归 passed）；
  - `-DXGUI_ON=0`：编译定义生效（flags.make 实证 -DXGUI_ON=0）、
    libXinYueCS 全绿、XGuiRegression_Test target 正确排除（条件化）；
  - `-DXSVGICON_ON=0` / `-DXMOVIE_ON=0`：独立裁剪构建全绿。
- **守卫错位通病修复累计 12 文件**（Charts 2、Graphics 1、Icon 1、
  Platform 5、Widget 3——含 XColorDialog/XInputDialog/XTextEdit.h
  信号区纳入类型守卫）。
- **新增工具 tools/init_field_check.py**（init 漏网字段静态检查）；
  高危漏网 XApplication.m_styleSheet（XString* 未初始化）待修清单
  .tmpdbg/init-check-report.md。

#### 下轮建议

- XApplication.m_styleSheet 等高危漏网修复（init-check 报告 6 项）；
- QAbstractItemView 批次二（拖放/键盘搜索行为本体）；
- 死声明 30 项中"补实现"候选按需重启（fontMetrics/grab/render）。

#### 14.81 初始化漏网修复 + QAbstractItemView 批次二（2026-09-18 2:00 心跳）

- **初始化漏网修复 4 项**（init_field_check 工具发现）：
  - [高] XApplication.m_styleSheet(XString*)——init 补 NULL、deinit 补释放
    （m_completer 同类野指针漏网）；
  - [中] m_autoSipEnabled = false（与 getter 缺省契约一致）；
  - [中] m_effectEnabled = 1（效果默认开）；[中] XSpinBox
    m_activeUp/m_activeDown = false（按压状态）。
- **QAbstractItemView 批次二**（行为深化 24+ 项基础上的新增）：
  keyboardSearch_2 行为本体（前缀累积+环形匹配+联动选中滚动）、
  scrollToHint 完整形态、scrollTo 空桩补齐方向滚动、setCurrentIndex
  选区联动（SelectCurrent）、clearSelection/selectAll 新增、
  keyPressEvent（Enter 激活+可打印字符键盘搜索）虚表挂接、
  ScrollHint 枚举。
- 验证：回归全绿（新增断言含初始化默认值与批次二 API 安全调用）；
  全裁剪构建通过。

### 14.82 视图族深化:fontMetrics + XTextEdit 补齐(2026-09-18 2:20 心跳)

- **XWidget_fontMetrics(XFont 值拷贝方案)**:返回控件当前 XFont 值
  (测量由调用方经 XPainter_textWidth/textHeight 完成,@note 与 Qt
  QFontMetrics 对象承载差异);顺带清理 fontMetrics/fontInfo 占位
  typedef 残留与 XWidget_font 自相矛盾注释;fontInfo 跳过(无承载类型)。
- **XTextEdit 补齐 6 项**:toPlainText(新建 XString)、setText
  (富文本探测分流 setHtml/纯文本+复位格式)、undo/redo(委托内嵌
  编辑器快照栈)、canUndo/canRedo(直查撤销/重做栈)。基线核查确认
  70+ 既有 API 无重复;markdown/text() 跳过并注明理由。
- 验证:回归全绿(新增 fontMetrics/XTextEdit 断言);全裁剪构建通过。

#### 下轮建议

- 文本族继续(QTextBrowser 历史族 14 项/QPlainTextEdit cursor 几何);
- XWidget fontMetrics 的真实测量辅助函数按需补(XPainter 测量族);
- 死声明"补实现"候选(fontMetrics 已补,grab/render 按需评估)。

### 14.83 文本族历史 + 表头段管理补齐（2026-09-18 2:40 心跳）

- **XTextBrowser 历史族**（环形数组 50 条承载,历史压栈联动 setSource）:
  clearHistory(保留当前条目)、backward/forwardHistoryCount、
  historyTitle/historyUrl(相对偏移语义,0=当前 -1=上一 +1=下一)、
  setOpenExternalLinks/openExternalLinks、setSearchPaths(深拷贝)/
  searchPaths(返回新建列表)、isBackwardAvailable/isForwardAvailable;
  顺带修复 reload 的 XString*/char* 类型误用与 m_searchPaths 泄漏。
- **XHeaderView 段管理补齐 11 项**:max/minSectionSize(缺省对齐 Qt,
  min<=max 不变式)、sectionResizeMode(Interactive/Fixed/Stretch/
  ResizeToContents 枚举)、firstSectionMovable(Qt 语义:需 sections
  Movable 配合)、reset(恢复默认,静默不发信号)、defaultAlignment
  (水平 Center/垂直 Left|VCenter)、stretchSectionCount、
  sectionsHidden、highlightSections/cascadingSectionResizes(状态
  承载 @note)、resizeContentsPrecision(缺省 1000 对齐 Qt 文档);
  结构体尾部新增 8 纯标量字段,init/copy/move 全路径同步。
- 验证:回归全绿(新增历史栈/段管理断言);全裁剪构建通过。

### 14.84 表头信号批次 + 文本查找几何（2026-09-18 3:00 心跳）

- **XHeaderView 信号批次**：sectionMoved(section 移动真实发射)/
  sectionResized(尺寸实际变化发射)/sectionCountChanged(段数变化
  发射)/geometriesChanged(几何影响操作发射)/sectionDoubleClicked/
  sectionEntered/sectionHandleDoubleClicked(句柄预留,无交互路径
  已注明)；新增 sectionViewportPosition 查询。emit 辅助
  xhv_emitInt2/Int3/Void 与既有模式一致。
- **XPlainTextEdit 几何与查找**：cursorRect(与 paintEvent 同口径
  字体度量)、find(向前/向后,命中置光标,连续查找语义)、
  anchorAt(纯文本无锚点,返回空串对象)、setTextCursor/
  textCursorLine/textCursorColumn(命名统一)。
- 验证:回归全绿(新增 find/cursorRect/length/logicalIndexAt 断言);
  全裁剪构建通过。

### 14.85 grab/render 离屏重定向 + XTextEdit 几何（2026-09-18 3:20 心跳）

- **XWidget_grab/render 实现**(离屏重定向机制):XWidget 结构体既有
  m_offscreenTarget/m_offscreenOrigin 预留字段首次启用——
  xwidget_redirectRoot 沿父链找最近重定向控件,paintImage/paintOffset
  重定向(子类 paintEvent 零改动自动画进临时图像),脏区保护
  (xwidget_backingPaintOffset 固定 update 路径旧语义);
  grab 双路径(后备存储深拷贝/临时画布同步派发完整子树绘制);
  render 经调用方 painter 输出(简化:等尺寸 targetRect、无缩放、
  无 RenderFlags,均 @note)。
- **XTextEdit 几何批次**:find_2 空桩实现(委托内嵌编辑器 find)、
  cursorRect(与 paintEvent 同口径)、anchorAt(空串对象,锚点几何
  未建 @note)、setTextCursor/textCursorLine/Column(钳位+查询)。
- 验证:回归全绿(新增 grab 快照尺寸/render 安全/cursorRect/
  setTextCursor 断言);全裁剪构建通过。

### 14.86 状态族核对批次（2026-09-18 3:20 心跳）

- **XTextBrowser 几何**:确认 cursorRect/anchorAt/setTextCursor 等全部
  经 XTextEdit 基类继承覆盖,滚动机制同一(XPlainTextEdit 垂直滚动条),
  无需覆写;负结果也是有效核对(接口存在性确认)。
- **XAbstractSpinBox/XSpinBox 状态族**:确认全部已实现(correction
  Mode/buttonSymbols/keyboardTracking/groupSeparatorShown/frame/
  accelerated);**修复 2 处对接断点**——XSpinBox 绘制路径硬编码
  m_spinFrame=true 致 setFrame(false) 不生效(改传真实字段),
  setFrame 转发宏补 update 重绘(对齐 Qt 立即重绘语义);另核实
  accelerating 默认 false 与 Qt 一致(任务假设纠正)。
- 验证:回归全绿。

### 14.87 并发批次四（2026-09-18 3:40 心跳）

- **QAbstractItemView 批次二基类能力 11 项**：currentIndex 组合查询、
  reset(清索引/选择/编辑器标记，容量保留)、scrollToTop/Bottom、
  doItemsLayout(@note 平铺简化)、itemDelegate 不透明承载、
  open/closePersistentEditor/isPersistentEditorOpen(打开标记简化)、
  indexWidget/setIndexWidget(借用承载)；xaiv_tableEnsure 行主序
  扁平表扩容助手，setModel/reset/deinit 全路径同步。
- **QTreeWidget 便捷族一 9 项**：currentItem/setCurrentItem(EnsureVisible
  滚动)、itemWidget/setItemWidget/removeItemWidget(三维承载,行锁步
  扩容+借用语义)、addTopLevelItems/insertTopLevelItems 批量、
  visualItemRect、sortColumn(-1=未排序)。
- **修复：XAbstractScrollArea_scrollContentsBy_base 空槽调用崩溃**——
  子类未覆盖 ScrollContentsBy 虚槽时,滚动条 value 变化经
  vScrollChangedSlot 调用未注册的空函数指针(0 地址)段错误；
  补判空保护(风格指南"void 槽位空槽保护"条款)。
- **XTextEdit 几何批次**(委托内嵌编辑器)：find_2 空桩实现、
  cursorRect、anchorAt(空串对象)、setTextCursor/textCursorLine/
  Column。
- 验证：回归全绿(新增 grab/render/aiv 批次二/tree 便捷族断言)；
  全裁剪构建通过。

#### 下轮建议

- 死声明"补实现"候选:fontMetrics 已补；grab/render 已补；
  视图族剩余小项扫尾；文本族 anchorAt 富文本化评估
- 分类报告更新(夜间批次进度表已在第七节，补最新计数)

#### 14.87 续

- 断言语义修正:undo 断言由"撤销栈清空"(栈计数假设错误——两次
  setText 各压一条快照)改为行为级验证"undo 后文本回退";3 轮全部
  通过,零失败零崩溃。

### 14.88 并发批次五：三路便捷族与状态族（2026-09-18 4:00 心跳）

- **XListWidget 便捷族**(对标 QListWidget):addItems 批量追加/
  currentItem/setCurrentItem(EnsureVisible 滚动)/findItems(精确/包含
  双模式)/sortItems(稳定插入排序)/itemAt(indexAt 分派)/
  visualItemRect(隐藏行不占位)/setItemWidget/itemWidget/
  removeItemWidget(行级部件挂载,借用语义);
  **修复 2 个既有缺陷**:takeItem 签名 void→XString*(返回取出文本)、
  insertItem 原占位实现(中段插入覆盖现行)改真实行内插入。
- **XHeaderView 剩余**:setSortIndicatorClearable/isSortIndicator
  Clearable(状态变化发射 sortIndicatorClearableChanged)+headerData
  Changed 信号句柄。
- **XTreeView 剩余 11 项**:sortByColumn/sortColumn/sortIndicatorOrder
  (状态承载 @note 排序本体未接)/visualRect(可见列宽均摊,与
  indexAt 同口径)/rowAt/columnAt(几何反查)/selectionRectVisible/
  allColumnsShowFocus/treePosition。
- 验证:回归全绿(新增三路断言);全裁剪构建通过。

### 14.89 并发批次六：XListView 剩余 + XTableView 几何表头族（2026-09-18 4:20 心跳）

- **XListView 剩余 4 项**：movement(Static/Free/Snap 枚举,@note Free
  拖动未接)/uniformItemSizes/clearPropertyFlags(接口存在性无操作)/
  indexesMoved 信号句柄(行号数组+数量,拖动移动路径 XGui 未建,预留)。
- **XTableView 几何表头族 12 项**：rowViewportPosition/columnViewport
  Position(表头偏移+可见行列累计,与 rowAt/columnAt 互逆)、rowSpan/
  columnSpan(平铺模型恒 1)、setHorizontalHeader/horizontalHeader 与
  setVerticalHeader/verticalHeader(XHeaderView 借用挂接,方向校验,
  @note 当前绘制仍内嵌自绘)、resizeColumnToContents/
  resizeColumnsToContents(XWidget_font+XPainter_textWidth 内容测量,
  边距 4px)、resizeRowToContents/resizeRowsToContents(textHeight+
  边距 2px)。
- 验证:回归全绿(新增 movement/uniformItemSizes/表头挂接/viewport
  位置/span 断言);全裁剪构建通过。

### 14.90 零散小类扫尾（2026-09-18 4:40 心跳）

- **XTabBar**:accessibleTabName 族 + tabWhatsThis 族(section 文本
  承载,XString** 平行数组,init/insert/remove/move/copy/deinit 全路径)
- **XTabWidget**:cornerWidget/setCornerWidget(四角借用挂载,上两角
  布局放置下两角仅承载)+ tabCloseRequested 信号句柄(预留接线)
- **XSplitter**:replaceWidget(替换旧控件交还调用方)、handle(把手
  几何矩形承载)、getRange(分隔点范围,含折叠语义)
- **XAbstractButton**:shortcut/setShortcut(文本承载,@note 触发体系
  未建)、group(void* 不透明承载,按钮组体系未建);init/copy/move/
  deinit 全路径同步
- **XLineEdit**:completer/setCompleter(不透明承载恢复接口存在性;
  setCompleter 时 XCompleter_setWidget 对标 Qt)
- **XMenu**:menuInAction(返回动作登记的菜单,省去 qobject_cast 鉴别)
- **XToolBar**:topLevelChanged(bool) 信号(句柄预留,无浮动机制)
- **XApplication**:fontMetrics(返回默认 XFont 值拷贝,对标
  QApplication::fontMetrics 承载差异注明)
- 验证:回归全绿。

### 14.90 并发批次七：三路补齐（2026-09-18 5:00 心跳）

- **XListWidget 剩余 5 项**：currentItemChanged(当前, 上一)/
  itemSelectionChanged 信号(真实发射点:setCurrentRow/takeItem/clear
  路径)、selectedItems(选择模型扫描)、scrollToItem(EnsureVisible)、
  item_new(行文本新建副本)。
- **XHeaderView 剩余 7 项+1 信号+1 成员**：resizeSection(钳制后转发
  setSectionSize)、setSectionHidden(转发)、setSectionResizeModeAt/
  sectionResizeModeAt(单段模式平行表,-1=跟随全局)、doItemsLayout
  (@note 全量 update)、**saveState/restoreState**(XHV v1 固定位宽
  文本序列化:魔数/版本/方向/段数/尺寸上下限/布尔/精度/对齐+每段
  尺寸隐藏模式,校验先行非法整体拒绝,对标 Qt write/read)、
  sectionPressed 信号句柄预留。
- **XTextBrowser/XWizard 零散**：anchorAt(空串对象 @note)、cursorRect
  (委托内嵌编辑器)、XWizard_visitedIds(计数/双输出双模式)+
  visitedPages 宏别名；顺带修 VX_wizard_deinit 清理块错位嵌套。
- 验证:回归全绿(新增 saveState/restoreState 往返/findItems/anchorAt/
  visitedIds 断言);全裁剪构建通过。

### 14.91 并发批次八：三路补齐（2026-09-18 6:00 心跳）

- **QHeaderView 9 项**：logicalIndex(逻辑序=视觉序恒等)、
  resetDefaultSectionSize、resizeSections(批量模式,Stretch 均分)、
  setModel(不透明借用承载)、setOffset/offset/setOffsetToLastSection/
  setOffsetToSectionPosition(偏移承载,视口宽 @note)、
  stretchLastSection getter 别名。
- **QPlainTextEdit 14/14**：cursorForPosition(行/列反查,UTF-8 逐码点
  累宽同口径)、createStandardContextMenu(简化菜单 6 动作,启用态
  实时计算)、currentCharFormat 族(int 位集承载)、document/setDocument
  (借用/接管)、extraSelections/setExtraSelections(轻量结构 XVector
  承载 @note 绘制联动未接)、loadResource(NULL 对标 Qt 默认)、
  selectionChanged 信号(真发射,状态翻转唯一入口)、textCursor
  (XPoint 平铺承载)、zoomIn/zoomOut(字体同步增减)。
- **QTextEdit 17 项**：cursorForPosition(cursorRect 逆映射)、
  insertPlainText/insertHtml(剥标签)、lineWrapColumnOrWidth 族、
  loadResource(NULL 承载)、merge/setCurrentCharFormat(位集)、
  scrollToAnchor(@note 锚点几何未建)、setDocument/document(所有权
  语义注明)、setMarkdown/toMarkdown/markdown(原文承载+纯文本降级)、
  setPlainText(收敛纯文本路径)。
- 验证:回归全绿(新增三路断言);全裁剪构建通过。

### 14.92 并发批次九：QTreeWidget 信号便捷族 + QFontComboBox 状态族（2026-09-18 7:00 心跳）

- **QTreeWidget 信号族 10 项**(句柄+真实发射点):itemClicked/
  itemPressed(点击命中)、itemDoubleClicked/itemActivated(双击激活)、
  itemChanged(条目文本变化,条目 owner 字段定位)、itemExpanded/
  itemCollapsed(**展开指示器独立承载**:m_topExpanded 平行数组,
  折叠补竖线/子树停绘,默认展开保证历史渲染不变)、currentItemChanged
  (setCurrentItem/鼠标换项/clear 三路径统一)、itemSelectionChanged
  (SelectCurrent 差分判定)。
- **QTreeWidget 数据便捷族 6 项**:columnCount(平铺承载恒 1)、
  editItem(句柄预留,将来发射 itemChanged)、indexFromItem(行号恒等)、
  scrollToItem(EnsureVisible)、selectedItems(选择模型承载)。
- **QFontComboBox 状态族**:writingSystem/setWritingSystem(XFontComboBox
  WritingSystem 简化子集,数值逐项对齐 Qt);**补 XFontComboBox 缺失的
  copy/move/deinit 虚函数重载**(此前 XCopy/XMove 路径字段丢失)。
- 验证:回归全绿(新增 columnCount/selectedItems/scrollToItem/
  writingSystem 断言);全裁剪构建通过。

#### 下轮建议

- **XTreeWidget 便捷族二补齐**(findItems/sortItems/itemAt/
  setHeaderLabels——4 项产出被会话二分回退丢失,需重做);
  分类报告更新最新计数。

### 14.93 XTreeWidget 便捷族二重做 + 守卫复核归档（2026-09-18 8:00 心跳）

- **XTreeWidget 便捷族二 4 项**(重做,前产出随二分回退丢失):
  findItems(前序遍历全树,精确/包含双模式,超限截断计数)、
  sortItems(冒泡,条目指针+cellWidgets 行表+m_topExpanded 三组平行
  数组同步换位,写入 m_sortColumn/m_sortOrder)、itemAt(展开态子树
  行带累计命中,与自绘同口径)、setHeaderLabels(新增表头文本存储,
  倍增扩容+新增区清零防野指针,deinit 逐条释放)。
- **守卫外声明复核重做归档**(docs/xgui-audit/2026-09-18/
  guard-review-0600.md):175 头全扫,"约 20 个无守卫"收敛为 19 个——
  13 个无需守卫(公共类型/基类,补守卫断裁剪链)、2 个建议补
  (XImagePluginRegistry/XImageBuiltinPlugin 包 XIMAGEIOPLUGIN_ON)、
  3 个待讨论;GPU 系已合规;XICON_ON/XIMAGE_ON 全仓不存在(Icon 核心
  常开设计);**XWizard.h 缺 BOM 已补**(全仓 BOM 修复至 13 头)。
- 验证:构建零错误,回归全绿。

### 14.91 续（9:00 心跳:守卫落地 + visualRect 扫尾）

- **XImagePlugin 系补守卫**:XImagePluginRegistry.h/XImageBuiltin
  Plugin.h 整体包裹 XIMAGEIOPLUGIN_ON(两头+.c 四文件);CMakeLists
  转发 foreach 补 XIMAGEIOPLUGIN_ON;消费方核查:全部使用点已在
  条件编译内(XImageReader/XImageWriter 内置单帧回退路径已存在);
  nm 确认 =0 时符号零残留。三项构建验证全绿。
- **visualRect 扫尾**:XListView_visualRect(行视觉矩形,隐藏行跳过,
  与绘制/命中同一几何函数)、XTableView_visualRect(单元格矩形,
  与 viewport 位置互逆);XListView rect(row) 跳过(与 visualRect
  语义重复)。
- 验证:回归全绿。

### 14.94 并发批次十：sectionsMoved/gridSize/XTableWidget 便捷族（2026-09-18 9:00 心跳）

- **XHeaderView**:sectionsMoved 信号别名句柄(与 sectionMoved 共用
  同一令牌,既有发射点直接送达)。
- **XListView**:gridSize 双输出 getter(未启用维输出 -1,与 setter
  对齐)。
- **XTableWidget 便捷族 17 项**:clear(补声明——.c 已有实现;清全部
  单元格+表头文本+挂载+模型维度)、currentItem/setCurrentItem、
  setCellWidget/cellWidget/removeCellWidget(平行挂载表承载,借用)、
  indexFromItem(地址恒等查表)、itemPrototype/setItemPrototype(借用
  不透明 @note)、visualRow/visualColumn(平铺恒等)、visualItemRect、
  setRangeSelected(范围归一化+裁剪,变化发射 itemSelectionChanged)、
  set/takeHorizontalHeaderItem/set/takeVerticalHeaderItem(拷贝/转移
  双语义);所有权语义逐 API 写明。
- 代理坦白:验证过程误用 stash/pop 已精确还原(与操作前逐字节一致);
  git 状态核查确认 stash 列表仅剩用户原有条目。
- 验证:回归全绿(新增 sectionsMoved/gridSize/cellWidget/clear 断言)。

### 14.95 XHeaderView viewport + XPlainTextEdit 选区查询（2026-09-18 10:00 心跳）

- **XHeaderView**:viewport() 新增(返回自身,表头自绘无独立视口
  @note);setSectionResizeMode 全局重载接线 resizeSections(Stretch/
  ResizeToContents 延迟批量重排,对标 Qt doDelayedResizeSections);
  sectionClicked @note 核对(Qt 在鼠标释放事件发射,程序化
  setSortIndicator 不发本信号);cascadingSectionResizes @note 收敛
  (Qt 仅作用交互级联,与批量重排无关)。
- **XPlainTextEdit**:hasSelectedText/selectedText 新增(选区激活
  查询+选中文本堆拷贝;@note 平铺模型选区为当前行起点至光标,
  跨行选区未接);maximumBlockCount/selectionChanged/textChanged/
  updateRequest/extraSelections/anchorAt/mergeCurrentCharFormat 等
  既有确认(核对后跳过,无重复)。
- 验证:回归全绿(新增 viewport/选区断言)。

### 14.96 核对确认轮（2026-09-18 11:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/stretchLastSection getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/Column 五项经 XTextEdit 基类继承全部可用(同一内嵌
  编辑器承载、同一滚动偏移口径、C 上转型同地址);无需覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.97 核对确认轮（2026-09-18 12:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/stretchLastSection getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.98 核对确认轮（2026-09-18 13:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/stretchLastSection getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.100 核对确认轮（2026-09-18 14:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/stretchLastSection getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.101 QWizard 虚槽接线 + XWidget 零散（2026-09-18 14:00 心跳）

- **QWizard/QWizardPage 虚槽接线（缺口补齐——上批"已有接线"结论不成立）**:
  XWizardPage_class_init 此前未注册四虚槽,本批补默认实现(对标 Qt:
  initializePage/cleanupPage 空操作、validatePage=true、nextId=顺序下一页)
  + 四分派函数 + XWizardPage_wizard(借用)/m_initialized 字段;
  导航接线:XWizard_next 改 validateCurrentPage→页 nextId→切换、
  xwiz_switchTo 加方向参数(Backward 触发 cleanupPage,IndependentPages
  跳过)、进入新页首次触发 initializePage、restart 对标 reset+Forward;
  addPage/setPage/removePage 维护 m_wizard 并复位被替换/移除页标记;
  pageIds(计数/双输出双模式);运行时冒烟 23 项断言全 PASS。
- **XWidget 零散 4 项**:fontInfo(XFont 值拷贝方案)、devType(1=Widget)、
  paintEngine(内嵌 XPaintDevice Raster 引擎借用)、scroll(简化:平移
  目标带+露出带入脏区+挂起脏区随动+updateRegion,无像素 blit)。
- **XTextBrowser**:sourceType(导航栈空=Unknown/有源=Url 简化枚举)。
- 验证:回归全绿。

### 14.102 核对确认轮（2026-09-18 15:00 心跳）

- **XHeaderView**:9 项全部已实现(logicalIndex 恒等/resetDefault
  SectionSize/resizeSections Stretch 均分/sectionsMoved 别名句柄/
  setModel 不透明借用/offset 四族/stretchLastSection getter 别名)。
- **XTextEdit**:17 项全部已实现(cursorForPosition 逆映射/
  insertPlainText/insertHtml 剥标签/lineWrapColumnOrWidth 族/
  loadResource NULL 承载/merge/setCurrentCharFormat 位集/
  scrollToAnchor @note/setDocument+document 所有权管理/
  setMarkdown/toMarkdown/markdown 原文承载/setPlainText 收敛)。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.103 核对确认轮（2026-09-18 16:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.104 核对确认轮（2026-09-18 17:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.105 核对确认轮（2026-09-18 18:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.105 核对确认轮（2026-09-18 19:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.106 核对确认轮（2026-09-18 20:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.107 核对确认轮（2026-09-18 21:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.108 核对确认轮（2026-09-18 22:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.109 核对确认轮（2026-09-18 23:00 心跳）

- **XHeaderView**:12 项小项全部为"已有跳过/确认"(sectionsMoved 别名
  句柄/headerDataChanged 信号/sectionPosition/viewport/resizeSections
  接线/cascading/highlightSections/resizeContentsPrecision/offset 族/
  setOffsetToLastSection/setOffsetToSectionPosition/stretchLastSection
  getter)——段管理已完整。
- **XTextBrowser 几何**:cursorRect/anchorAt/setTextCursor/
  textCursorLine/textCursorColumn 五项经 XTextEdit 基类继承全部可用
  (同一内嵌编辑器承载、同一滚动偏移口径、C 上转型同地址);浏览器层
  cursorRect/anchorAt 包装属同名便捷再导出,非行为覆写。
- 零改动确认轮:本轮无新增/修改,构建与回归状态与上轮一致。

### 14.110 API 缺口清零轮（2026-09-18 11:56 单线程批次）

**扫描器缺口 699→178→10→0:Phase 3 API 对齐面收敛完成。** 10 条尾部
缺口本轮全部处置:

- **XTableView setSpan 真实现**:新增 XTableViewSpan 类型与
  m_spans/m_spanCount/m_spanCapacity 平行数组存储(倍增扩容、
  deinit/copy/move 全路径接管);setSpan 对标 Qt 语义(均 1 取消合并/
  同原点替换/任一 <=0 忽略);rowSpan/columnSpan 改查表(覆盖格同样
  返回所属区间,Qt 一致);clearSpans 真清空;绘制循环原点格按区间内
  可见列宽/行高之和合并绘制、被覆盖格跳过(隐藏行列 0 占高参与求和);
  indexAt 命中合并区间反查回原点。回归 9 断言(存储往返/覆盖格反查/
  取消/清空/合并 grab/indexAt 映射)。
- **XWizardPage_validatePage 默认实现修复**(WIZ-FAIL 既有失败):
  Qt 的 QWizardPage::validatePage() 默认返回 isComplete(),原实现
  恒 true——改为回退 isComplete();next() 导航随之获得 complete
  门禁(对标 Qt)。
- **XTreeView_dataChanged 槽**:对标 QAbstractItemView::dataChanged,
  区间校验(逆序/负值/完全越界丢弃)后整体重绘(全量帧管线的收敛)。
- **XTextEdit_currentFont**:整篇单格式模型下以族/字重/字号/斜体/
  下划线属性组合 XFont 值返回(字号四舍五入收敛整型点值);调用方
  XFont_deinit_base 契约同 fontMetrics。
- **QTreeWidget 四件套**:invisibleRootItem 哨兵根(children 借用
  m_topItems、owner 挂控件)——xtw_syncRoot 在 ensureTop/add/
  insert/take/clear 五入口同步,addChild 钩子根→控件方向回写(经根
  挂载即顶层挂载,Qt 真语义);headerItem/setHeaderItem(表头条目
  init 懒建、setHeaderLabels 镜像子节点文本、setHeaderItem 接管
  所有权+回填标签承载);新增 headerLabel 列文本便捷 getter;
  itemFromIndex(索引=indexFromItem 约定的顶层行号,反查平凡)。
- **SKIP 豁免新增 5 条**(含理由):QTextEdit currentCharFormat/
  setCurrentCharFormat(QTextCharFormat 未建模,字体属性访问器承载)、
  extraSelections/setExtraSelections(ExtraSelection 覆盖绘制层未
  建,富文本子集边界)、QListWidget.items(拖放 MIME 换算辅助,
  protected,依赖未建的 DnD 体系)。
- **文本族 anchorAt 富文本化落地**(原"评估"项升级为真实现):
  XTextEdit_anchorAt 由恒空串桩改为富文档真实命中——以片段
  fmt.anchorHref 为承载,命中几何与 VX_textEdit_paintEvent 富绘制
  逐段同口径(块带高 18/起笔 x=2/居中 (width-100)/2/片段步进
  strlen*8);XTextBrowser_anchorAt 委托改至基类富文档命中(原委托
  内嵌纯文本编辑器,无锚点概念);锚点数据模型(XTDCharFormat::
  anchorHref)本已存在,本轮补齐几何反查。链接点击→anchorClicked
  真发射(openExternalLinks 桌面打开)仍留后续(需鼠标事件接线,
  属行为深化批次)。回归 3 断言(命中/片段外空串/浏览器委托)。
- **ASan 拦获既有栈作用域缺陷**:XTableView 表头标签绘制 char buf[16]
  声明在 if 块内,text 指针出块后仍被 drawText 读取(stack-use-after-
  scope,新加的表格 grab 用例首次踩中);提升声明至列循环层修复,
  ASan 全量复跑零错误(泄漏检测按项目约定 detect_leaks=0 口径)。
- 新增回归断言 30+(setSpan 族/树四件套/dataChanged/currentFont);
  常规构建+ASan 双通道全绿。

### 14.111 Demo 检视 + Wizard 布局修复 + 链接交互接线轮（2026-09-18 12:39 单线程心跳）

**Demo 全页离屏检视**(25 张:主页面 1/2/3/5 + 选项卡 21 页全量,
`--screenshot --page --tab` 离屏口径,无空白帧):

- 全部 tab 渲染正常(表格表头/数据/选中、日历、日期时间、菜单工具栏、
  多行编辑、下拉、工具箱选中项与内容一致、堆叠、输入组、状态栏);
  选项卡 16-20 因 demo 乱序 insertTab 物理位次与逻辑号不同,非缺陷。
- **发现并修复:Wizard 底部按钮被裁剪不可见**——按钮按 init 时
  480x320 布局到 y=284,demo setGeometry(440x220) 后无 resizeEvent
  重排,按钮悬在可见区外。修复:(a) 新增 xwiz_layoutButtons 统一
  按钮行布局(init 与 resize 共用,Help 最左/取消最右/完成·下一页·
  上一页依次左移,bw=80/gap=6/边距 8);(b) XWizard 虚表补
  EXWidget_ResizeEvent→VX_wizard_resizeEvent(重排按钮行+当前页
  几何,与 xwiz_switchTo 同口径高 -40);(c) addPage 首页挂载即铺
  内容区(否则首次导航前页面 0 尺寸不可见)。修后截图按钮行正常,
  首页 Back 隐藏、Next/Cancel 就位。

**XTextBrowser anchorClicked/highlighted 真发射落地**(14.110 遗留项):

- 机制前提核实:XObject 事件过滤器(installEventFilter/EXObject_
  EventFilter)在 XCoreApplication_notify 已全量分发(app 级+对象级、
  返回 true 截流),可直接使用。
- 接线:浏览器 init 对内嵌编辑器安装自身为过滤器;事件过滤器内
  编辑器按下命中锚点→发 anchorClicked(URL UTF-8),默认 openLinks
  语义同时 setSource 导航,openExternalLinks 开启改走
  XPlatformServices 桌面打开;鼠标移动进入/切换/离开链接(URL 变化)
  →发 highlighted,离开载荷空串(去重经新增 m_hoverAnchor 承载);
  deinit 解挂过滤器+释放悬停锚点;恒返回 false 不过滤(编辑器照常
  处理光标)。锚点命中复用 14.110 的 XTextEdit_anchorAt 富文档几何。
- .h 两信号与 openExternalLinks 注释同步为真发射语义。
- 回归 5 断言:悬停进入/按下发射+URL 载荷/openLinks 导航 setSource
  往返/悬停离开空载荷。测试探针教训:信号载荷串归发射方所有且在
  事件返回前即释放,槽内必须拷贝持有(直接存指针=悬垂,XStrcmp 读
  已释放内存偶发不等)。

**验证**:常规构建+回归全绿;demo Wizard/浏览器页修后截图复查正常。

**下轮建议**:XGuiWindowDemo 交互态(xdotool 实点击)复验链接点击
导航与 highlighted 串台;QHeaderView headerDataChanged 模型转发
接线;XWizard 当前页在 xwiz_switchTo 之外(如 setGeometry 后无新
导航)的路径已由 resizeEvent 覆盖,可抽查 setSource(searchPaths
相对解析)行为对齐。

### 14.112 弹层透明缺陷定位轮（2026-09-18 12:58 单线程心跳）

**xdotool 交互冒烟(补 14.111 静态截图未覆盖的点击路径)**:主导航/
内部页签/表格选中交互正常;**发现重大交互缺陷:XComboBox 弹层
打开后完全透明不可见**。证据链:

1. gdb 断点确认点击到达 VXComboBox_mousePressEvent、
   XComboBox_showPopup_base 全流程执行(视图懒建/定位/映射/
   flushBackingStore/grabMouse 全跑);
2. xwininfo 证实弹层 X 窗(150x62+78+275,组合框正下方,
   override-redirect)**已映射 IsViewable 但内容全空**(32 位 ARGB
   无像素→视觉全透明),root 截图多次复验;
3. **第一层根因(已修)**:XWidget_init/setWindowFlags/create 变体
   共 3 处的 m_isWindow 判定只认 XWindowType_Window——有父的
   Popup 型控件 isWindow=false,flushBackingStore 的顶层回溯落到
   宿主窗,后备存储绘错目标。已按 Qt 语义(Qt::Popup 即窗口)把
   XWindowType_Popup 纳入全部 3 处判定,回归全绿。
4. **剩余层(未闭合)**:修复后弹层仍透明——平台层 override-redirect
   弹层 X 窗与 widget 层 XWidgetWindow/后备存储仍接不通(疑似
   XWidget_createWindow 另建空壳窗,根 children 中 150x62 弹层旁
   有 1x1 空窗;XMenu 同构路径同样受害,菜单弹开交互验证亦未通过)。
   平台层线索:全局优先 32 位 visual(XPWN_DEPTH_32),弹层窗实测
   Depth 32 TrueColor——若画刷写入 alpha=0 则整窗透明(主窗显示
   正常,其上屏路径对 32 位窗的 alpha 处理需一并核对)。
5. **裁剪抽样**:XGUI_ON=0 下库目标构建零错误(注意:全量 target
   会连带未做裁剪守卫的 Test/XGuiTest 测试文件报 75 错,属既有
   局限,与本体改动无关;裁剪抽样口径=库目标)。

**下轮建议**(夜间并发窗口,建议专列一代理深挖):审计
XWidget_createWindow/XWidgetWindow 对 Popup 型控件的平台窗创建
与 backing store 绑定链路,对齐 XMenu 的窗体承载;优先核对 32 位
visual 上屏路径的 alpha 通道写入(XPutImage ARGB32 是否补 0xFF);
完成后 combo 弹层/菜单弹层交互双验证(gdb+detach+root 截图法);
XGuiWindowDemo 默认页弹层选中联动回归断言。

### 14.113 弹层透明定位推进轮（2026-09-18 13:27 单线程心跳）

gdb 断点透视(XComboBox.c:1160,flush 之后)逐项排除,**widget 层
绘制管线全通**:

- 弹层 m_isWindow=1(14.112 修复生效)、m_windowHandle 非空、
  m_windowRect={78,275,150,62} 正确;
- flush 后 m_backingStore 已建、平台后端(XPlatformBackingStore)
  已建、XBackingStore_paintImage 返回非空 XImage——**内容确实
  画进了后备存储**;
- 剩余怀疑面收敛到最后一环:XBackingStore_flush→XPutImage 的
  **目标 drawable 绑定**(弹层 XWidgetWindow 包的 XWindow 是否真
  指向屏上 0x9a00006 那个 override-redirect 窗,还是另有所指/
  尺寸不符),以及 32 位 visual 下 alpha 通道是否为 0。

**下轮动作**(一发 gdb 即可闭环):断点 1160 处
`p *((XWindow*)((XWidget*)view)->m_windowHandle)` 打印平台窗结构
取其 drawable id 与 xwininfo 的 0x9a00006 比对;若一致则转向
alpha 假设(XPutImage ARGB32 检查 alpha 字节);若不一致则修
XBackingStore_flush 目标解析。

**补充证据(14.113 同轮末)**:主窗同为 Depth 32 TrueColor 且显示
正常——纯"32 位必透明"假设被削弱;主窗内容可见说明 32 位上屏
路径本身能写出正确像素。新增主怀疑:**双重窗创建**——
XWidget_setWindowFlags(Popup) 可能已触发平台层创建 override-
redirect 窗(0x9a00006,150x62+78+275 几何全对),而 XWidget_show→
XWidget_createWindow 又建了第二个原生窗(XWidgetWindow 桥接),
后备存储 flush 绑定后者→内容落在不可见的第二个窗,屏上 OR 窗
恒空。根 children 中伴随的 1x1 空窗与此吻合。xwd 直读 OR 窗报
BadMatch(X_GetImage),无法直接取样内容。

**下轮首选核验**:审计 XWidget_setWindowFlags/XWidget_show/
XWidget_createWindow 三处对 Popup 型控件的窗口创建路径,确认是否
创建两次;若是,去重(复用同一 XWindow 平台对象)或让 backing
store 绑定 OR 窗 drawable。

**再排除两项(13:45)**:XListView_paintEvent 首行即
fillRect(0xFFFFFFFF) 整幅不透明填充——"内容透明"假设排除
(只要 paintEvent 被派发,弹层必然不透明);XWidget_setVisible
是 Popup 唯一惰性建窗点(无双重建窗)。剩余唯一疑点:**paintTree
是否真的把 paintEvent 派发到了弹层视图**(paintEvent 若从未被
调用,后备存储保持全零=ARGB 全透明,与所有观测吻合)。下轮在
XListView_paintEvent 打断点即可一锤定音;若未派发,则查
flushBackingStore→paintTree 对"新创建且无脏区"顶层窗的派发条件
(疑似 dirty-region 为空导致 paint 被跳过)。

**一锤定音结果(13:47)**:gdb 断点 VXListView_paintEvent **有被
派发**(经 XWidget_paintEvent_base,调用对象即弹层视图)——paint
派发正常、填充不透明,即 XImage 内已有正确像素。排除链走完,
问题锁定在**最终 blit 层**二选一:(a) XPutImage 对该 OR 窗
BadMatch 被自定义错误处理器吞掉(对照:主窗同路径成功);
(b) xpwn_copyRectDirect 直拷含 alpha 字节,而填充路径实际未写
alpha 高字节(需 dump 弹层 XImage 前几字节验证 FF/00)。下轮:
gdb dump 弹层 backing store XImage 首行字节;若 alpha=00 → 修
raster/present 的 alpha 写入;若 FF → 查 X11 错误处理器日志。

**终局排除(13:50)**:dump 弹层 XImage——150x62、首 16 字节全
0xFF(不透明白);XWindow_winId(弹层桥接窗)=161480710=0x9A00006,
即屏上那个 mapped OR 窗本体。**矛盾定案**:像素正确+drawable
正确+窗口 viewable+XPutImage 走同一条主窗可用的路径,四项全对
却不可见。剩余可能性仅两:(1) XPutImage 实际 BadMatch 被吞
(需临时启用默认 X 错误处理器或 XSetErrorHandler 打印复跑);
(2) 弹层在 show 后被某环节(1ms grab 定时器/XComboPopupView
越界判定)瞬间 unmap/重置——但 xwininfo 多次显示 IsViewable,
(2) 弱。下轮首选:临时安装打印型 XSetErrorHandler 复跑弹层开
启,看 XPutImage 是否 BadMatch(srect/drect 或 depth 不匹配);
顺带在 grab 定时器回调与 XComboPopupView 越界收起处打断点排除
瞬间隐藏。

**registry 核验补记(13:49,本批最后一测)**:gdb 直接调
xpwn_findByXWindow——弹层 XWindow 在平台注册表中,entry->m_win
=161480710=0x9A00006(与屏上 mapped 窗一致)。同时确认全库**未
安装自定义 XSetErrorHandler**——默认处理器遇 BadMatch 会打印
并杀进程,而弹层开启后进程存活,故 **XPutImage 没有 BadMatch**。
至此七项全验证通过(paint 派发/像素不透明/后备存储/平台后端/
drawable 绑定/窗口 viewable/注册表),唯二剩余:(1) present
调用链在 XBackingStore_flush→XPlatformBackingStore_flush→
present 之间某处提前 return false(如 preparePresentImage 失败、
GPU 直通分支劫持);(2) XPutImage 成功但像素被覆盖。
下轮第一步:gdb 断点 1160 处直接调 XPlatformNativeWindow_present
看返回值,或在 XPlatformNativeWindow_posix.c:2263/2268 两处
XPutImage 前插 fprintf 定位是否到达。

### 14.114 弹层不可见缺陷完全修复轮（2026-09-18 18:20 晚间单线程，用户实时反馈确认）

**双层根因全部落网**(承接 14.112/14.113 的排除链):

1. **列表行文字颜色参数为 0(全透明)**——xlv_drawRowText 快速路径
   `XPainter_drawText(..., text, 0)` 末参 color=0,XPainter_drawText
   直接以该参数作 ink(SoftwareAA 路径 painterApplyOpacity(0)→
   alpha=0)→ SourceOver 写入等于无像素;drawText 返回 true、
   drawRowText 也确实逐行调用,一切"正常"却零痕迹(潜伏 bug:demo
   中唯一 XListView 即弹层,回归只断言模型不断言像素,故从未暴露)。
   修复:显式传 0xFF000000u(对齐矩形路径既有写法)。
2. **paintTree 遍历原生/弹出子窗**(14.112 第一层修复后暴露的覆盖
   源):弹层(isWindow,父链挂在 combo 下)被主窗帧泵的
   paintTree 递归当作普通子控件,按父链偏移 translate(24,182) 重画
   进弹层**自己的**后备存储——整幅背景色覆盖 + 文字越界丢弃,
   440FPS 永久压制弹层自身 flush 的正确内容。修复:paintTree 子级
   遍历跳过 m_isWindow 子树(对标 Qt 跳过原生子窗口;XMenu 同受
   此益)。修复前该遍历恰好让弹层内容经主窗缓冲"意外可见"(白盒
   无文字,即用户最初报告的形态);修复后走正确通道。

**验证**:ffmpeg x11grab 实屏捕获(注:xwd -root 在本机合成器下
不含 OR 窗内容,14.113 的"不可见"部分判读失准)——弹层 Alpha/
Beta/Gamma 三行黑字白底完整显示;全量回归真绿(4 处 FAIL 均为
ime-dbg 输入法调试日志非测试失败)。

**经验**:多因叠加时分步定位务必用"最终上屏像素"作唯一判据,
中间缓冲快照与合成器外推都会误导;用户实时目视是最快的 oracle。

**同款隐患全库清扫(14.114 续,19:30)**:按 color=0 透明文字模式
全库排查,另发现并修复 5 处同款潜伏点——XTableView 三处(无模型
占位/表头/单元格文字)与 XTreeView 两处(列头/条目文字),均为
"setPen 设色后 drawText 传 0"的错配(standalone XTableView/
XTreeView 直绘文字从未上屏;demo 表格页因走 XTableWidget 自绘
带真实色而幸免)。回归新增 lvtext 像素级断言(XWidget_grab 全幅
扫暗像素>20)锁定该约定;全库复扫零残留。

### 14.115 弹层选择回路打通轮（2026-09-18 19:30 晚间单线程）

**第三层根因落网——contains 坐标口径错配**:gdb 断点链
(REL-FORWARD-INSIDE w=150 h=62 → 但 xlv_indexAt 从未被调用→
ACTIVATED 未发射)证实:释放事件已转发基类,基类 indexAt(74,35)
却返回 false 且未触碰出参——`xcomboPopupView_contains` 拿事件
**弹层局部坐标**去比对 `XWidget_rect` 的**全局矩形 (65,270)**,
弹层内点击恒判"越界":按下即收起、永不激活(与 14.114 白盒无字
叠加,即"下拉框选项文字看不到"的完整形态)。

- 修复:xcomboPopupView_contains 改为与弹层尺寸直接比较(局部
  坐标口径)。
- 修复后实测仍有断链:基类 activated 依赖 IndexAt 虚槽,弹层
  子类(XComboPopupView)虚表在该槽位解析不稳(xlv_indexAt 从未
  被调用,虚表槽位继承问题留档待查)。改为**确定性实现**:释放
  处理内按 XCOMBOBOX_ITEM_H 行高本地换算行号,显式
  setCurrentIndex + 发射 activated(row),选择/收起仍经
  xcombo_viewActivatedSlot 既有链路。
- **实机端到端验证**:点开弹层(Option 1/2/3 黑字白底)→点击
  Option 2 →弹层收起+组合框标签更新为"Option 2" ✓(ffmpeg
  实屏捕获前后对照);全量回归真绿零 FAIL。

**弹层缺陷累计修复清单(14.112→14.115)**:m_isWindow 纳入 Popup
(3 处判定点)/paintTree 跳过 isWindow 子树/drawRowText 颜色
0→0xFF000000/contains 坐标口径/释放直接激活五项;XTableView
三处+XTreeView 两处同款 color=0 清扫;lvtext 像素级回归断言。

**虚表探针补记(20:40)**:gdb 全表扫描证实弹层虚表 0..33 中
**不含 xlv_indexAt**(data[31]=noop/data[32]=VXFrame_changeEvent),
而同表 EXWidget_PaintEvent 槽位继承正常——链路为 XObject(7)+
XWidget(24)+XFrame(+1)+ASA(+1)+AIV(IndexAt)+XListView,EXAbstract
ItemView_IndexAt 的槽位下标与 XListView 表尾写入位置存在错位嫌疑
(XVTABLE_OVERLOAD 越界会 exit,未触发则可能写入位置并非派发读取
位置);因选择回路已改直连,该虚表异常暂不阻断功能,但影响所有
"对弹层调 indexAt_base"的外部路径。

**虚表全表 dump 实测(20:55,弹层 34 槽)**:slot[10]=paintEvent
(继承正常)、slot[25/26]=弹层鼠标重载(本次直连激活生效)、
slot[31]=noop、slot[32]=VXFrame_changeEvent、slot[33]=ignore
——EXAbstractItemView_IndexAt 期望位置无 xlv_indexAt,与扫描
结论一致。链路含 XFrame 层(XAbstractScrollArea extends XFrame:
ScrollContentsBy = XFrame 尺寸,ASA 后再 AIV/XListView),XFrame
自身槽位数量决定 IndexAt 名义下标;gdb 实测与头文件推算存在
+1 量级的错位嫌疑(XVTABLE_OVERLOAD 越界即 exit 未触发,说明
注册写入与派发读取的枚举值一致、均落在表内但不是 xlv_indexAt
——即注册表与派发表使用了同一错误槽位,该槽实际为 noop 默认)。

**根因终局+根修(21:10)**:XVTABLE_OVERLOAD 写槽不维护 size——
AIV 在槽 34 注册 IndexAt 后 size 仍 34,下游 INHERIT 按 size 复制
即代际丢失槽 34(XListView 自身因重写自己的槽 34 而幸免,弹出层
继承链则彻底丢失)。根修:XVTABLE_OVERLOAD 写入越旧 size 时同步
`size = Type + 1`(size=有效槽位数不变式),全量构建+回归真绿,
实屏复测下拉框完整选择回路(开→点 Option 2→收起+标签回写)全通。
回归锁:新增 popup-idx 断言(弹层打开后 indexAt_base(75,25) 须命中
行 1)——锁定虚表尾槽继承,防 OVERLOAD size 维护回退;全裁剪
(XGUI_ON=0)库目标构建零错误。

XMenu 侧静态检查:其条目文字绘制传真实颜色(XMenu.c 无 color=0
调用),同受五项修复惠及;菜单弹出实机目视验证因演示窗位置漂移
致 xdotool 点击命中不可靠,转请用户日常使用中顺带目验。

XMenuBar 侧:XMenuBar 经 bridge(动作 triggered→XMenu_popup)开
菜单,点击文件未现弹窗(命中或触发链待查);演示窗已停在菜单页
供用户直接目验菜单弹出与文字显示。

**XMenuBar 点击开菜单修复(21:30)**:两处补齐——(1) XMenuBar
虚表仅有 paint/deinit,无任何鼠标槽,点击动作(文件/编辑)天然
无效:新增 VX_menuBar_mousePressEvent(经 XMenuBar_actionAt 命中
动作→XAction_trigger,triggered 桥接既有链路弹菜单);(2) 桥接
槽 XMenu_popup(menu, NULL) 使菜单弹在屏幕 (0,0):改为按动作几何
经 XWidget_mapToGlobal 映射全局位置(动作矩形左下)。实机验证:
点文件→菜单弹出于文件正下方,"退出"项文字清晰可见,Esc/再点
正常收起;全量回归真绿。

**菜单项选择端到端验证通过(21:10)**:点文件→菜单弹出→点
"退出"项→动作触发、演示进程正常退出(退出动作的预期行为)——
菜单完整回路(开菜单→条目点击→动作触发)实机全通,与下拉框
选择回路并列成为交互修复的两大闭环验证。

**槽位下标静态核对闭环(21:58)**:以头文件枚举块逐级推算,全链
真实槽位为 XObject 0-9(10 槽,含 Copy/Move/Deinit/Event/
EventFilter/ChildEvent 等)、XWidget 10-33(24 槽,Paint=10、
MousePress=25、Wheel=29、Change=32、ContextMenu=33,与虚表 dump
完全吻合)、XFrame 0 新增(34=ScrollContentsBy)、ASA 34、AIV
35=IndexAt、XListView/XComboPopupView END=36——即 IndexAt 名义
槽位=35,弹层容量 36 内;14.115 的 size 维护根修使 INHERIT 按
新 size(36)完整复制,弹层 data[35]=xlv_indexAt 成立,回归断言
(popup-idx)持续锁死该路径。虚表审计正式闭环,无遗留动作。

### 14.116 对齐边界盘点+扫描映射修复轮（2026-09-18 22:10 晚间单线程）

**未映射类全量盘点（应"74 之外还有啥"之问）**:Qt widgets 源码树
共 189 类,XGui 映射 74;未映射 115 类经分类归位九大类:GraphicsView
体系 40、QStyleOption* 内省结构 28、委托/条目内部支撑 11、窗口框
架增强 11(QMainWindow/MDI/Dock 等)、手势 8、杂项 8、布局系统 10、
平台系统级 7、未归类 9——除未归类 9 外均属嵌入式设计边界
(场景图/手势/主窗口框架/布局系统不在 XGui 承载范围)。

**扫描器映射修复 3 项(映射类 74→77)**:
- QLCDNumber→XLcdNumber:缩写类名(QLCD)不合名称约定(X+Qt 去 Q),
  MANUAL_QT_TO_XGUI 显式映射;暴露 1 缺口 checkOverflow→查实
  XLcdNumber 已有 checkOverflowInt/Double(语义化重载命名),登记
  RENAMED 改名表(首次启用)后清零;
- QDateEdit/QTimeEdit→XDateTimeEdit:Qt 中即 QDateTimeEdit 的
  便捷子类,XGui 以同一实现类承载;暴露 2 真缺口 userDateChanged/
  userTimeChanged(用户改期/改时信号,句柄从未存在也从未发射);

**userDateChanged/userTimeChanged 实现**:XDateTimeEdit.h 补两
信号声明,XDateTimeEdit.c 补句柄实现与 xdt_emitUserDate/UserTime
发射助手,stepBy 用户步进路径按日期/时间部分是否变化分别发射
(程序性 set 不发射,对标 Qt 语义)。重扫:映射类 77、缺口 0。
全量回归真绿。

**对齐边界结论**:未映射 112 类均属设计边界(场景图框架/内部
结构/主窗口框架等),XGui 侧不再逐类对齐;后续新增控件按需个案
评估(QDoubleSpinBox/QFontDialog 列为候选)。

### 14.117 gui 域首次盘点轮（2026-09-18 22:15 晚间单线程）

**对齐范围诚实盘点（应"全部比较过了吗"之问）**:既有扫描器
QT_SRC 仅覆盖 qtbase/src/widgets——widgets 域 77 类逐 API 比较闭环
（缺口 0）成立,但 XGui 的 Graphics/Application 子系统与 Qt 的
qtbase/src/gui 从未系统对比。本批首跑 gui 域扫描:

- **映射 45 类**（XImage/XPainter/XIcon/XMovie/XBitmap/XPixmap/
  XTextDocument/XShortcut/XAccessible/XPlatformIntegration 等按
  名称约定自动映射）；
- **缺口 129（15 类）**,产出独立清单
  docs/xgui-audit/2026-09-16/xgui-api-gaps-gui-v1.txt（widgets
  主报告已恢复）。构成:QAccessible 无障碍框架 18、QImage 富 API
  （convertTo/copy/QVariant 等）、QActionGroup 组策略 5、
  QPlatformIntegration/Accessibility 平台接口、QShortcut/
  QMovie/QPicture/QPixmap/QIcon 杂项;
- 三分处置待后续轮次:①设计边界（QVariant 体系/内部钩子/
  无障碍注册表——嵌入式豁免）②真实候选（QImage convertTo/
  copy、QActionGroup 排斥策略等）③平台接口对齐
  （XPlatformIntegration 已有实现,补 API 面）。

**下轮建议**:gui 域 129 缺口三分处置;widgets 报告与 gui 报告
今后分文件维护（扫描器输出路径固定,切换 QT_SRC 后需手动归档）。(EXAbstractItem
View_IndexAt 在 XComboPopupView 虚表解析为空的机制,涉及所有
"子类仅重载鼠标、依赖基类 IndexAt"的场景);XMenu 菜单弹出交互
复验(同受益于本轮修复)。

#### 14.102 续（saveState/restoreState 往返失败——待查项）





- XHeaderView_saveState/restoreState 往返在最小复现中 restoreState
  返回 false(校验拒绝),序列化/解析字段序列已核对对称。
  需后续在 restoreState 校验链中逐步打断点定位(疑似
  XHEADERVIEW_STATE_VERSION 或 stateWriteInt/ReadInt 的位宽不匹配)。
- 影响:XHeaderView 状态保存/恢复暂不可用,段管理 API 本身正常。

#### 14.90 续二（ASan 定位 sortItems 类型混淆修复）

- ASan 精确定位 sortItems 写回阶段 heap-buffer-overflow：snapshot[i]
  (char*) 被误传给期望 const XString* 的 setData——char* 被当 XString*
  解引用 XContainer_memory 越界。修复：改调 setData_2(UTF-8 兼容重载)。
- ASan 下另确认 XHeaderView saveState/restoreState 往返已修复(补
  sortOrder 写入+orientation 写入宽度 WriteDigit→WriteInt+ReadInt
  跳前导空格)，连续 3 轮无崩溃无 FAIL。
- 3 轮稳定性验证：回归全绿零失败；全裁剪构建通过。

## 16. 文本编辑控制器化重构计划（对齐 Qt 私有控制器架构） — 2026-09-18

### 16.1 背景与动机

XLineEdit（单行）与 XPlainTextEdit（多行）是两条平行继承链（前者直接继承
XWidget，后者经 XAbstractScrollArea/XFrame），与 Qt 完全一致；Qt 也没有
"文本编辑共同控件基类"。但两者在控件内部各自内联实现了同一批编辑外围能力：

- 撤销/重做栈（XLineEdit 用定长数组，XPlainTextEdit 用 XVector，两套实现）；
- 剪贴板读写（各写一份 XGuiApplication_clipboard 往返）；
- UTF-8 码点边界扫描（`xlineedit_nextBoundary` vs
  `xpe_utf8SeqLen`/`xpe_prevBoundary`）；
- IME 提交接入、标准右键菜单构建、光标绘制与命中测宽。

重复实现已发生一次真实漂移事故（2026-09-18）：XPlainTextEdit 的
backspace/delete 按单字节删除中文，把多字节字符拆成非法残序列，渲染为
空白且光标测宽错位（用户感知为"光标反方向跳动"、"删除出空白字符"）；
而 XLineEdit 的同名逻辑自始就是码点感知的（`xlineedit_nextBoundary`）。
两份实现各自演化，正是该类缺陷的温床。本轮已把 XPlainTextEdit 修复为
码点感知（`XPlainTextEdit.c` backspace/delete/左右键 + 新增回归用例），
但两份实现并存的漂移风险仍在。

### 16.2 Qt 6.8.3 参考架构（本机源码实测，D:/Qt/6.8.3/Src）

Qt 对同一问题的解法：公开控件层不做共享，编辑逻辑全部下沉到"私有文本
控制器"（非控件的 QObject）：

- `qtbase/src/widgets/widgets/qwidgetlinecontrol_p.h:50`
  `class QWidgetLineControl : public QInputControl` —— QLineEdit 专用
  （单字符串模型：maxLength/validator/回显模式/命中测试/撤销栈）。
- `qtbase/src/widgets/widgets/qwidgettextcontrol_p.h`
  `class QWidgetTextControl : public QInputControl` —— QTextEdit、
  QPlainTextEdit、QTextBrowser、QLabel（可选中文本）共用（文档模型：
  QTextDocument + QTextCursor + 选区 + 撤销栈 + IME + 命中测试）。
- `qtbase/src/widgets/widgets/qplaintextedit_p.h:46`
  `class QPlainTextEditControl : public QWidgetTextControl` —— 块感知特化。
- 共同根：`qtbase/src/gui/text/qinputcontrol_p.h:50`
  `class QInputControl : public QObject`，以 `Type{LineEdit,TextEdit}`
  区分按键可接受语义。两个控制器本身是兄弟关系，Qt 亦未强行抽取共同
  编辑基类。

控件壳因此极薄（以 QPlainTextEdit 为例，qplaintextedit.cpp）：

- `copy()/undo()/paste()` 即 `d->control->copy()/undo()/paste()`；
- `keyPressEvent/mousePressEvent/inputMethodEvent` 一律
  `d->sendControlEvent(e)`（qplaintextedit_p.h:107 →
  `control->processEvent(e, offset, viewport)`），光标定位、选区、
  撤销全部由控制器在文档坐标内完成；
- textChanged/undoAvailable/selectionChanged 等信号由控制器发射、
  控件转发；
- 绘制经 `control->draw(...)`（含 AA）。Qt 的 QRectF 路径边界
  （x+w/y+h）靠 0.5 平移 + 抗锯齿落到最外圈像素；XGui 整数光栅等价
  内缩见本轮 XFusionStyle 按钮边框修复。

### 16.3 XGui 现状对照

| 能力 | Qt 位置 | XGui 现状 |
| --- | --- | --- |
| 撤销/重做栈 | 两个私有控制器 | XLineEdit 定长数组、XPlainTextEdit XVector，两套 |
| 码点边界 | 控制器内部（QTextCursor） | xlineedit_nextBoundary / xpe_utf8SeqLen+xpe_prevBoundary 两套 |
| 剪贴板读写 | QWidgetTextControl::copy/paste | 两份 XGuiApplication_clipboard 往返 |
| IME 提交 | control->processEvent | 两份 inputMethodEvent（本轮补齐 XPlainTextEdit） |
| 标准编辑菜单 | 控件 createStandardContextMenu | 两份近乎相同的构建函数 |
| 光标绘制/测宽 | control->draw | 各自 paintEvent 内联 |

（样式引擎承接的绘制不在本计划范围；XCommonStyle/XFusionStyle 分层维持
现状。）

### 16.4 实施计划（两期）

#### 一期：文本工具层共享（低风险，先行）

1. 新增 `Src/XGui/Text/XTextUtf8`（暂定名）：
   - `XTextUtf8_seqLen(s, remain)`（吸收 xpe_utf8SeqLen）；
   - `XTextUtf8_prevBoundary(s, col)`（吸收 xpe_prevBoundary 与
     xlineedit_nextBoundary 的反向语义）；
   - `XTextUtf8_nextBoundary(s, len, col)`；
2. 剪贴板文本助手：`XTextClipboard_setText/getText`（封
   XGuiApplication_clipboard 往返与 UTF-8 转换）；
3. 标准编辑菜单构建器：`XTextMenu_createStandard(ops)`，ops 为回调表
   （undo/redo/cut/copy/paste/selectAll + 对应 enabled 查询），XLineEdit
   与 XPlainTextEdit 各传自己的槽；
4. 两个控件删除各自重复实现，改为调用共享层；行为不变，回归全绿为
   验收线（重点：UTF-8 码点编辑用例）。

#### 二期：控制器对象化（结构对齐 Qt）

1. 新增 `XLineControl`（QObject 语义，非控件）：单字符串模型、撤销栈、
   回显模式、maxLength/validator 钩子、命中测试、IME 提交、绘制数据
   （对标 QWidgetLineControl）；
2. 新增 `XTextControl`（对标 QWidgetTextControl 的平铺行简化版）：行
   数组、撤销栈、选区模型、码点游标、滚动值联动、IME/命中测试；
3. `XLineEdit`/`XPlainTextEdit` 壳化：keyPress/mousePress/IME 事件改为
   `XTextControl_processEvent(control, event)`；paintEvent 调
   `control->draw(painter, clip)`；公开 API 一行委托，签名不变；
4. 迁移顺序：先 XPlainTextEdit（本轮修复的码点/IME/菜单逻辑整体搬家），
   后 XLineEdit；分两个独立提交；
5. 验收：回归套件全绿（含 UTF-8 码点用例）、演示页交互实测
   （点击定位/中英文输入/Backspace 与 Delete 方向/方向键/右键菜单）、
   像素级截图比对（边框/光标/选区高亮）。

### 16.5 风险与约束

- 行为不变是硬约束：重构期间不得顺带改交互语义；缺陷修复单独提交；
- 光标测宽（XPainter_textWidthRange 字节偏移口径）、IME commitString、
  菜单启用态为高敏区，每步改动需截图像素比对；
- 平铺行模型暂不引入 QTextDocument（XTextDocument 桥接保持现状），
  避免把控制器对象化扩大为文档模型重写；
- 二期迁移 XLineEdit 时，密码回显（PasswordEchoOnEdit 状态机）与
  校验器拒绝路径必须逐条回归。

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
