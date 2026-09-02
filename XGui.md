# XGui 进度文档

> 最后更新：2026-09-02 Asia/Shanghai
> 职责：记录 XGui（对标 Qt 6.8.3）当前实现进度、已知问题与下一步。
> 本文件面向“更换 AI 继续”场景，所有定位信息均为当前仓库实测事实。

## 1. 当前任务目标

XGui 布局系统核心 API 已按 Qt 6.8 对齐：PC 端功能可用，嵌入式通过
`Src/XGui/XLayout/XLayout_config.h` 的宏开关裁剪扩展功能；ICC/LUT 原始资源
已由 XImage 内部侧车安全保存，ICC 解析和 Qt 原生 Picture 互操作仍保留在文末
边界说明中。

- 总开关：`XLAYOUT_ON`（在 `Src/CXinYueConfig.h` 统一定义）
- 子开关：`XLAYOUT_BOX_ON` / `XLAYOUT_GRID_ON` / `XLAYOUT_SPACER_ON` /
  `XLAYOUT_STACKED_ON` / `XLAYOUT_TOTAL_ON`（PC 桌面扩展 API，关闭可裁剪嵌入式体积）

## 2. 仓库状态

- HEAD：`dd1ad79a`（对齐 XGui Qt 6.8 图像与图标行为）
- 工作树：**保留本轮未提交改动**，不清理、不丢失、不 push
- 构建验证入口：`build/`（out-of-source CMake），测试程序
  `bin/XGuiRegression_Test`
- 分支/提交约定：默认分支前缀 `codex/`；**不要 push**（除非用户明确要求）

### 2.1 运行回归测试

```bash
# 仓库根运行
./bin/XGuiRegression_Test
```

当前结果：**XGui 回归测试全部通过**。运行时仍会输出既有的窗口参数提示
（无效 transient parent、忽略 WindowActive）及一次空对象诊断日志，但不影响
测试结果。

## 3. 已完成工作概览

### 3.1 图像体系（已纳入 HEAD `dd1ad79a`，本轮继续补齐行为）

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

### 3.2 XGui 布局系统（当前进度主体，未提交）

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

## 10. 本轮完整性审计与裁剪验证

本轮补齐并验证了以下此前未闭环的实现：

1. `XImageReader` 接入 GIF 多帧缓存、跳帧、延迟、循环次数和当前帧；
   `XMovie` 现在可以保持动画运行状态、切换真实 GIF 帧并正确暂停/恢复。
2. `XImageReader/XImageWriter` 的格式和 MIME 列表按 `XIMAGECODEC_*` 开关动态
   过滤；SVG 检测支持 BOM、前置空白和 XML 前缀；图像分配上限实际生效。
3. `XGridLayout` 跨行/列尺寸按 Qt `distributeMultiBox` 处理间距和
   min/hint/max，额外空间按 stretch/expanding/最大值收敛；`XWidgetItem`
   支持按宽度回调计算高度并执行最小/最大高度钳位。
4. `XSurfaceFormat` 的 StereoBuffers 选项与便捷标志保持同步；窗口拷贝不再
   复制原生句柄；窗口销毁会清理应用注册/焦点引用；主屏移除会提升剩余屏幕；
   `XApplication_widgetAt` 使用真实全局几何和子控件命中。
5. `XInputMethod` 增加焦点对象查询回调，`ImEnabled` 不再按“有焦点即接受”
   近似；焦点切换会同步平台输入上下文，光标/锚点/裁剪矩形按 Qt 语义查询
   焦点对象并应用输入项变换，静态查询辅助函数转发到当前输入法；locale
   变化会按 QLocale 文本方向切换 RTL/LTR。
6. 默认 `XPalette` 按 Qt 6.8 `qt_fusionPalette()` 浅色分支的实际计算值对齐，
   包括 AlternateBase、Mid/Midlight/Dark/Shadow、禁用组、高亮和占位文本透明度。
7. 修复裁剪依赖：`XPALETTE_ON=0`、`XCURSOR_ON=0`、后备存储关闭、窗口关闭、
   X11 后端关闭以及全部 XGui 开关关闭时，静态库均可编译；unsupported 后端
   存根可独立链接。
8. `XPlatformNativeInterface` 增加平台函数注册表，支持按名称注册、覆盖、注销，
   `platformFunction` 与各类 `nativeResourceFunctionFor*` 查询均可返回已注册函数。
9. X11/Win32 原生窗口后端实现键盘抓取、鼠标抓取和请求激活；无真实平台句柄时
   仍按 Qt 平台不支持语义返回 `false`。
10. `XScreen_grabWindow` 接入 X11 `XGetImage` 与 Win32 GDI 抓屏，统一转换为
    ARGB32 `XPixmap`；无显示服务器时仍返回空像素图。
11. 拖放已统一为 `XDropEvent`/`XWindowSystemInterface_handleDropEvent`：Linux
    X11 接收完整 XDND Enter/Position/Leave/Drop + Selection，Windows 接收
    `WM_DROPFILES` 并转换为 `text/uri-list`；`XGuiXdnd_Test` 用第二个真实
    X11 客户端验证整条协议链路。
12. 无障碍已提供 `XAccessible` 应用根/窗口对象树、角色、标题、描述、几何、
    可见状态和生命周期通知；Linux Drive 通过 session D-Bus 暴露
    `org.a11y.atspi.Accessible/Application/Component` 基础方法，Windows
    Drive 通过 UI Automation host provider 发出布局失效通知；无桌面服务时
    自动退化为存根，嵌入式可由 `XACCESSIBLE_ON` 裁剪。
13. `XPlatformIntegration_createForeignWindow()` 已接入 PC 外部窗口语义：Linux
    X11 与 Windows Win32 可挂接调用方拥有的原生句柄，解除挂接时不销毁外部资源；
    无窗口系统或嵌入式后端返回 NULL，重复挂接保持幂等并拒绝已创建窗口重绑。
14. 中高收益平台对象已补齐统一入口：`XPlatformDrag` 提供出站拖放（Linux
    XDND、Windows OLE），`XPlatformOffscreenSurface` 提供 GLX PBuffer/WGL
    隐藏窗口离屏表面，`createEventDispatcher()` 接入当前线程的统一事件分发器；
    `XPlatformFontDatabase`、`XPlatformTheme` 和 `XPlatformServices` 分别连接
    Linux fontconfig/XDG 与 Windows GDI/注册表/ShellExecute，均在无系统服务时
    安全退化。
15. `XGuiApplication` 窗口移动语义同步更新借用注册表；注销会清理重复登记并避免
    在嵌套析构期间遍历失效窗口，解决 Qt 风格 move/destroy 生命周期下的悬挂指针。

已验证配置：PC 默认构建、`XIMAGECODEC_GIF_ANIM_ON=0`、X11 后端关闭、
BackingStore/NativeWindow 关闭、Palette/Style/Clipboard/Mime/Cursor 关闭、
Widget/Layout/Application 关闭、输入上下文关闭、全部 XGui 开关关闭；本轮新增
`XPlatformFontDatabase/Theme/Services/Drag`、离屏表面、事件分发器和窗口 move
生命周期均通过默认回归与 ASan 回归。默认 `./bin/XGuiRegression_Test` 结果为
`XGui regression tests passed`，Linux `./bin/XGuiXdnd_Test` 结果为
`XdndEnter/Position/Drop selection transfer passed`。

### 10.31 2026-08-24 XPainter 完整对标 QPainter + 可裁剪开关

对照 Qt 6.8.3 的 QPainter 绘图模型，把 XPainter 的绘图能力补全到“常用
矢量/填充/文本布局”完整集合，并新增 `Src/XGui/Graphics/XPainter_config.h`
（在 `XGuiConfig.h` 注册）逐类裁剪：

- 形状：`XPainter_drawEllipse / drawArc / drawPie / drawChord /
  drawRoundedRect`，对标 `QPainter::drawEllipse/drawArc/drawPie/
  drawChord/drawRoundedRect`（开关 `XPAINTER_SHAPE_ON`）。
- 多边形/折线/点集：`drawPolygon / drawPolyline / drawPoints`，对标
  `QPainter` 同名接口（开关 `XPAINTER_POLYGON_ON`）。
- 画笔样式：`XPainter_setPenStyle / setPenCapStyle / setPenJoinStyle`，
  对标 `QPen` 的 Style/CapStyle/JoinStyle；`XPainter` 状态随 save/restore
  压栈/弹栈（开关 `XPAINTER_PENSTYLE_ON`）。
- 画刷体系：实心/无画刷与线性、径向、锥形渐变色画刷，对标
  `QBrush` / `QGradient`（开关 `XPAINTER_BRUSH_ON`）。
- 文本布局：`XPainter_drawTextRect` 边界矩形内对齐、按词/任意处换行、多行
  绘制，覆盖 Qt 6.8 对齐与文本 flags 数值全集（`TextSingleLine`、
  `TextDontClip`、`TextExpandTabs`、`TextShowMnemonic/HideMnemonic`、
  `TextWrapAnywhere`、`TextDontPrint`、`TextJustificationForced`、
  `ForceLTR/ForceRTL` 等），对标 `QPainter::drawText(const QRectF&, int
  flags, QString)`（开关 `XPAINTER_TEXTLAYOUT_ON`）。
- 路径：`XPainterPath` 动态路径对象与 `XPainter_drawPath/fillPath/
  strokePath`，支持 moveTo/lineTo/quadTo/cubicTo/closeSubpath 与
  addRect/addEllipse 便捷构造，二次贝塞尔 16 段、三次 24 段展平绘制，
  对标 `QPainterPath` / `QPainter::drawPath/fillPath/strokePath`
  （开关 `XPAINTER_PATH_ON`）。
- 变换：`XPainter_shear`、`setWorldTransform/worldTransform`、
  `isTransformIdentity`、`invertTransform`、`map`，与原有 translate/scale/
  rotate 一起对标 `QTransform` / `QPainter::worldTransform()`。
- 所有开关默认开启；置 0 后对应公共 API 硬裁剪（引用触发“类型未声明”），
  对应 .c 实现整段不编译，不进入固件，语义与 `XLayout_config.h` 一致。
- `XPicture` 回放保留“强制实线描边”语义，录制配色不受画笔样式开关影响；
  `XPicture_play` 在 painter 为空时安全退化，不会解引用崩溃。
- 回归新增形状、多边形、画笔样式、虚线回放、画刷渐变、文本布局、文本
  新 flags、路径、变换矩阵专项测试，均按各开关条件编译。
- 近似边界已收敛：路径与圆角矩形按折线/曲线采样近似，录制路径中的
  渐变色按行取中点取样近似；`TextForceRTL` 仅按右对齐处理（无 RTL 点阵
  字库，`TextLongestVariant` 无多变体）。这些是明确的近似/边界，不与 Qt
  精确像素输出画等号。

验证：
- 默认全量 `cmake --build build -j$(nproc)` 通过，
  `./bin/XGuiRegression_Test` 全绿；
- 裁剪构建（`-DXPAINTER_SHAPE_ON=0 -DXPAINTER_POLYGON_ON=0
  -DXPAINTER_PENSTYLE_ON=0 -DXPAINTER_BRUSH_ON=0
  -DXPAINTER_TEXTLAYOUT_ON=0 -DXPAINTER_PATH_ON=0`）静态库与回归测试均
  编译通过、运行全绿（本机与默认构建共享 `bin/` 输出路径，复跑默认构建
  已恢复）。
- 未提交、未 push；工作树其余未提交改动保持原样。

### 10.32 2026-08-24 XPainter 路径 / 完整变换 / 文本 flags 全集对齐

承接 10.31，补齐剩余三类缺口，完成“剩下也完全对齐”：

- 路径：新增 `XPAINTER_PATH_ON` 开关与 `XPainterPath` 动态对象 API
  （`XPainterPath_init/deinit/moveTo/lineTo/quadTo/cubicTo/closeSubpath/
  addRect/addEllipse/elementCount/currentPosition`），`drawPath/fillPath/
  strokePath` 复用现有 fillRect/drawLine 原语，因此 `XPicture` 录播无需
  新增指令/改版；quad/cubic 分别按 16/24 段折线展平。
- 变换：新增 `XPainter_shear` 与 `setWorldTransform/worldTransform/
  isTransformIdentity/invertTransform/map`，与 Qt 的
  `QPainter::shear/setWorldTransform/worldTransform()`、`QTransform::
  inverted/map` 对应；透视分母过小或非有限结果 `map` 返回 false。
- 文本 flags：`XPainter_drawTextRect` 升级为 Qt 6.8 数值全集：
  `TextSingleLine`（换行当空格、单行不断行）、`TextDontClip`（默认裁剪到
  布局矩形，越界字符不绘制）、`TextExpandTabs`（默认 8 字符制表位）、
  `TextShowMnemonic/HideMnemonic`（`&x` 转义下划线/隐藏、`&&` 输出字面
  `&`）、`TextJustify`（补足宽度到空格）、`TextDontPrint`（不绘制）、
  `TextJustificationForced`（末行也强制两端对齐）；`JUSTIFICATION_FORCED`
  使末行参与补空，未设置时末行保持普通对齐。
- 近似边界如实保留：路径曲线/圆角矩形为采样折线近似；录制后端（绑定
  `XPicture`）的渐变色按行取中点颜色近似；`TextForceRTL` 无 RTL 点阵字库
  时仅右对齐、`TextLongestVariant` 无多变体。均不宣称与 Qt 逐像素一致。
- 验证：默认构建 + `./bin/XGuiRegression_Test` 全绿；裁剪构建追加
  `-DXPAINTER_PATH_ON=0` 重新 configure 后静态库与回归测试全绿，已复跑
  默认构建恢复 `bin/` 输出。
- 未提交、未 push。

### 10.33 2026-08-24 XPainter 改用项目内存接口

按项目约定，XPainter 不再直接调用 CRT 的 `realloc`/`free`：

- 扩容改为 `XRealloc_System`，释放改为 `XFree_System`，与项目既有
  内存分配入口一致，便于后续替换全局内存方法。
- 结构体零初始化沿用项目既有写法 `memset(..., 0, sizeof(...))`，
  不新增额外内存清零 API。
- 验证：默认构建 + `./bin/XGuiRegression_Test` 全绿；裁剪构建
  （含 `-DXPAINTER_PATH_ON=0` 等）静态库与回归测试均编译通过、
  运行全绿；已复跑默认构建恢复 `bin/` 输出。
- 未提交、未 push。

### 10.34 2026-08-24 XPainter 批量绘制 / 凸多边形 / 背景擦除 / 裁剪完整性

承接 10.31–10.33，补齐以下 API 与行为，继续对齐 Qt 6.8 QPainter：

- 批量绘制：
  - `XPainter_drawRects(rects, rectCount)` 对标 `QPainter::drawRects`，
    依次复用单矩形描边；`rects==NULL || rectCount<=0` 视为无操作返回 true。
  - `XPainter_drawLines(pointPairs, pairCount)` 对标
    `QPainter::drawLines(const QPoint*, lineCount)`，每两个 `XPoint`
    组成一条线段依次复用单线绘制；空参数同样视为无操作。
- 凸多边形：`XPainter_drawConvexPolygon(points, count)` 对标
  `QPainter::drawConvexPolygon`（Qt 该接口始终用当前画刷填充、当前
  画笔描边，因此无 filled 参数），内部当前复用 `XPainter_drawPolygon`
  的扫描线填充与描边，不单独区分凸凹性；回归验证了内部用画刷填充、
  边缘用当前画笔绘制、外部不绘制。
- 背景与擦除：
  - 状态新增 `m_backgroundColor`（默认不透明白），新增
    `XPainter_setBackground` / `XPainter_background`。
  - `XPainter_eraseRect(rect)` 对标 `QPainter::eraseRect`，内部按
    背景色等价 `fillRect` 实现（Qt 语义为用背景画刷填充）；
    NULL 矩形返回 false、空矩形为无操作。
- 裁剪完整性：
  - 本节的旧式 `XPainter_resetClip()` 与 `XPainter_clipRect(out)` 已删除：
    二者不属于 QPainter 6.8 的同名公共接口；裁剪修改统一由
    `XPainter_setClipRect(rect, operation)` 完成，查询只保留
    `XPainter_clipBoundingRect(out)`。
  - `XPainter_setClipping(bool)` 对标 `QPainter::setClipping`，只切换
    开关、保留最后一次的裁剪描述；无可重启的裁剪描述时开启请求无效。
- 内存接口沿用 10.33：矩形结构清零、路径等初始化继续走项目已有
  `memset` 零初始化写法，不新增 `XMemory_zero` API；动态扩容/释放走
  `XRealloc_System`/`XFree_System`，已在上一小节完成替换。
- 近似边界如实保留：
  - `drawConvexPolygon` 当前不区分凸凹、与 `drawPolygon` 共用实现，
    不宣称独立凸性算法；
  - `setBackground` 为颜色级对齐，Qt 的 `background()` 返回画刷，
    本实现只保留 ARGB32 颜色部分；
  - `setClipping(true)` 依赖已有有效裁剪矩形，未设置 `setClipRect`
    直接开启时不会额外建立裁剪区域（与 Qt 相同）；
  - `clipRect`/`clipBoundingRect` 仅支持矩形裁剪，未实现 Qt 的
    路径/区域裁剪与多裁剪求交，因此不等同 Qt 的完整裁剪区域边界
    查询，但在“有矩形裁剪→关闭→保留查询”这一维度已对齐。
- 验证：默认构建 + `./bin/XGuiRegression_Test` 全绿；追加裁剪构建
  （`build-crop-off`，关闭 SHAPE/POLYGON/PENSTYLE/BRUSH/TEXTLAYOUT/PATH）
  静态库与回归测试全绿；已复跑默认构建恢复 `bin/` 输出。
- 未提交、未 push。

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

## 12. 约束（沿用项目约定）

- 头文件详细中文注释；风格严格遵守
  `代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md`；
- 纯 C99，**不引入任何后台/平台 API**，嵌入式可用；
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


### 10.35 2026-08-26 工作区恢复：构建链路与回归验证

背景：恢复过程在 `/home/xinyue/Code/XinYueC_restore` 进行，目标是先把当前 XGui 代码恢复到可全量编译、可运行回归测试的状态，不扩大为新功能实现。

恢复修改（本轮触及且已托底）：

- `Src/XAlignment.h`：文件开头只保留一个 UTF-8 BOM，避免双 BOM 干扰头文件解析。
- `Src/XGui/Widget/XWidget.h`：补 `#include "XFont.h"`，补 `m_font` 字段与 `XWidget_font`/`XWidget_setFont` 公有 API 声明；`XWidget.c` 补齐字体生命周期与 `XWidget_updateContentsRect` 静态前向声明。
- `Src/XGui/Graphics/XPainter.h`：补 `XPainter_drawTextRect` 公有声明（与实现签名一致）。
- `Src/XGui/Graphics/XPainter.c`：新增 `XPainter_font` 实现，修复动态库链接 `XinYueC_Dynamic` 时的未定义符号。
- `Src/XGui/Widget/XFrame.h`：补 `XCLASS_DEFINE_BEGING(XFrame)` / `XCLASS_DEFINE_EXTEND_END(XFrame, XWidget)`，使派生类虚表枚举可解析。
- `Src/XGui/Widget/XLabel.h`：补 `typedef struct XLabel XLabel;` 前向声明，修复富文本回调等 API 引用未知类型。
- `Src/XGui/Widget/XLabel.c`：修 `XLABEL_DEBUG` 分支内 `fprintf` 字符串被错误断行的问题，改为一行并在格式串显式写 `\n`。
- `Src/XGui/Widget/XPushButton.c`：`XPushButton_class_init` 中继承与重载宏调用补齐分号，与 XLabel/XFrame 写法统一。

验证结果：

- `cmake -S . -B build && cmake --build build -j$(nproc)`：全量构建通过，生成 `bin/XGuiRegression_Test`、`bin/XinYueC_Static`、`bin/XinYueC_Dynamic`、`bin/XGuiWindowDemo_Test` 等。
- 全量构建仍有仓库内大量既有指针兼容性警告（如 XIO/XList/XIODevice 的 `-Wincompatible-pointer-types`），不属于本轮恢复引入；未在本轮扩大清理。
- `./bin/XGuiRegression_Test`：退出码 0，输出 `XGui regression tests passed`。运行日志中的 `XWindow`/`XClass` 非致命错误输出为既有行为日志，回归判据仍通过。
- CTest 无注册用例：`ctest --test-dir build` 返回 0 但报告 `No tests were found`，因此本轮以直接运行回归可执行文件作为验证。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查；当前恢复目标以编译与回归通过为准。

10.36 已完成图像 Handler/插件抽象与源码级注册表（见下文）；仍待推进：BMP 外编解码器补全、XIcon 图标引擎与主题查找、XPainter 剩余 opcode、Qt fixture 回归补全、QImage 扩展 API。全部改动保留在本地，未提交、未 push。

> 注：10.36 标题下为本轮插件抽象记录；BMP 外编解码器的尺寸探测与 Reader
>  大小路径已在 10.37 继续补齐。

### 10.36 2026-08-26 图像 I/O 插件抽象与源码级注册表

本轮在恢复后的工作区上继续 XGui 图像体系对齐，完成 `QImageIOPlugin` /
`QImageIOHandler` 抽象与源码级注册表接入，内置 BMP/PNG/JPEG/GIF/SVG 五
格式 codec 保持可用。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.h:115`
  （`capabilities()` 纯虚接口）
- `qimageiohandler.cpp:169`（`QImageIOPlugin` 插件职责）、
  `qimageiohandler.cpp:580`（`capabilities()` 语义）
- `qimagereader.cpp:143`（`createReadHandlerHelper()` 格式探测）、
  `qimagereader.cpp:1498`/`1512`（`supportedImageFormats()` /
  `supportedMimeTypes()`）、`qimagereader.cpp:1528`（MIME 映射）
- `qimagewriter.cpp:101`（`createWriteHandlerHelper()`）、
  `qimagewriter.cpp:761`/`774`/`790`（writer 格式/MIME API）

实现范围：

- 新增 `Src/XGui/Graphics/XImagePluginRegistry.h/.c`：固定容量 32 的静态
  源码级插件注册表，提供 `addPlugin` / `removePlugin` / `pluginAt` /
  `clear`，以及读/写 handler 创建、格式/MIME 查询；注册表不拥有插件
  生命周期，插件仍由调用方释放。
- 补齐 `Src/XGui/Graphics/XImageIOPlugin.h/.c`：对齐 Qt 的
  `capabilities` / `create` / `keys` / `nameFilters` / `mimeTypes` 抽象，
  使用项目 `XCLASS_DEFINE` 继承体系。
- 修复 `Src/XGui/Graphics/XImageIOHandler.c` 的类虚表登记方式：改为
  `XVTABLE_ADD_FUNC_LIST` 追加 `canRead/read/write/option/setOption/`
  `supportsOption/jumpToNextImage/jumpToImage/loopCount/imageCount/`
  `nextImageDelay/currentImageNumber/currentImageRect` 槽位，避免子类
  继承时只复制基类头三个槽位导致空指针。
- `XImageReader.c` / `XImageWriter.c`：在既有内置 codec 之前先查询插件
  注册表；`XGuiConfig.h` 新增 `XIMAGEIOPLUGIN_ON` 开关，裁剪配置下恢复
  纯内置 codec 工作路径。
- `xgui_regression_test.c`：新增 mock 插件读写回归用例，覆盖注册、
  `supportedImageFormats`、MIME 解析、writer/reader 通过插件完成 2x2
  ARGB 往返、卸载与清理。

验证结果：

- `cmake --build build -j$(nproc)`：全量构建通过。
- `./bin/XGuiRegression_Test`：退出码 0，输出 `XGui regression tests
  passed`。
- 全量构建仍保留仓库既有兼容性警告（主要指旧测试/信号类型不匹配），
  XGui 图像插件相关源文件无新增编译错误。
- `ctest --test-dir build` 仍无注册用例；本机未安装 `valgrind`，本轮未
  执行内存泄漏检查，恢复目录 `/home/xinyue/Code/XinYueC_restore` 保留作
  恢复参考。

近似边界与未完成项：

- 当前为静态源码级注册表，不实现 Qt 的动态插件目录加载与
  `QImageIOPlugin` 工厂发现。
- Qt 的 `createReadHandler` 会按格式候选逐一调用 `capabilities()` 探测，
  本项目在显式格式下按 key 精确匹配，无格式时以注册顺序回退；文档保留该
  差异，后续再按 Qt fixture 扩展自动探测。
- 后续未完成模块仍按优先级推进：BMP 外编解码器补全、XIcon_paint 与主题
  查找、XPainter 更多 opcodes、Qt fixture 回归补全、QImage 扩展 API。

### 10.37 2026-08-26 XImageReader 尺寸探测贯通 JPEG/SVG

本轮补上上一节遗留的“BMP 外编解码器补全”缺口中的尺寸探测路径：此前
`XImageReader_size()` 只直接解析 BMP/PNG/GIF，JPEG/SVG 会掉入 BMP 分支
失败；现在统一由 `XImageCodec_probeSize` 分发，五种格式均可无损探测宽高。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/jpeg/qjpeghandler.cpp`
  `option(QImageIOHandler::Size)` 分支：JPEG 尺寸来自 SOF 段中的
  精度/高/宽字段，探测不需解码完整图片（该文件 `options` 大小分支）。
- `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/plugins/imageformats/svg/qsvgiohandler.cpp`
  `Size` 选项返回 `d->defaultSize`，即 SVG 根元素 width/height，缺失时取
  viewBox 尺寸；本项目与之一致。

实现范围：

- `Src/XGui/Graphics/XImageCodec/XImageCodec.h`：新增公共
  `XImageCodec_probeSize(data, size, format, width, height)`，含中文注释。
- `Src/XGui/Graphics/XImageCodec/XImageCodec.c`：实现 BMP/PNG/GIF 文件头
  探测；JPEG 使用轻量 marker 扫描（SOI 后遍历段，定位 SOF0/1/2/9/10，
  从精读后字段读取宽高，遇 SOS/EOI 或非法段长即失败）；SVG 转发内部
  `XImageCodecInternal_probeSvgSize`。
- `Src/XGui/Graphics/XImageCodec/XImageCodecSvg.c`：新增内部 SVG 尺寸探测，
  复用现有 `svgParseDom` / `svgNodeAttr` / `svgParseLength` /
  `svgParseNumberList` / `svgClip` / `svgArenaInit/Cleanup`，与解码路径保持
  相同的 “width/height 优先、viewBox 兜底” 的默认尺寸选择。
- `Src/XGui/Graphics/XImageReader.c`：`XImageReader_probeSize` 从最多读取
  54 字节改为最多读取 256 KiB（设备走 `XIODevice_peek_3`，文件读整段后按
  前缀截断），再交给 `XImageCodec_probeSize`；关闭 `XIMAGECODEC_ON` 时
  保留原 BMP 兜底。
- `xgui_regression_test.c`：codec 往返用例新增 `XImageCodec_probeSize`
  Dimension 断言；Reader 设备用例在 `read` 前显式调用 `XImageReader_size`
  ，覆盖 JPEG/SVG 的 size 前缀探测。

验证结果：

- `cmake -S . -B build && cmake --build build -j$(nproc)`：全量构建通过。
- `./bin/XGuiRegression_Test`：退出码 0，输出 `XGui regression tests
  passed`；五格式 round-trip 与 upper-layer devices 均通过新增尺寸断言。
- `build-crop-jpeg`（`XIMAGECODEC_JPEG_ON=0`）：裁剪构建通过，回归退出码 0。
  裁剪模式下 JPEG 相关回归用例按编译开关关闭，代码路径不再触碰 JPEG 断言。
- `build-crop-svg`（`XIMAGECODEC_SVG_VECTOR_ON=0`）：裁剪构建通过，回归退出码 0。
  关闭 SVG 矢量渲染后，仍保留轻量字符串尺寸探测：优先解析根元素
  `width`/`height`，缺失时回退 `viewBox`，用于 `XImageReader_size()`。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查；`ctest --test-dir build`
  仍无注册用例，以直接运行回归可执行文件为准。

近似边界与未完成项：

- JPEG 尺寸探测仅做 marker/段长合法性检查，不校验压缩流完整性；行为对应
  “读取尺寸”语义。
- SVG 尺寸探测按解码路径的默认规则返回根元素尺寸并裁剪到 1..16384；若文件
  的前缀中根 SVG 属性被巨大的内嵌文本挤出 256 KiB，Reader 尺寸探测可能
  失败，完整 codec `probeSize` 无此前缀限制。
- 仍未完成：XIcon_paint/图标引擎/主题查找与回退路径、XPainter 更多 opcodes、
  畸形 BMP/mask/缓存生命周期/Picture 序列化等 Qt fixture 回归补全、QImage
  color-space/文本元数据等扩展 API。

### 10.38 2026-08-26 XIcon 主题图标引擎与 fromTheme 回退路径

本轮补齐“XIcon_paint/图标引擎/主题查找与回退路径”的可用版：新增
`XIconThemeEngine` 和内部主题资源解析器，并把 `XIcon_fromTheme` /
`XIcon_fromTheme_2` 接到主题引擎；找不到主题资源时按 Qt 的
`fromTheme(name, fallback)` 语义回退到调用方传入的图标。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp`
  `1360-1422`：`QIcon::fromTheme(name)` 创建基于名称的主题引擎，
  引擎为空时返回空图标；带 fallback 的重载在主题图标为空或无可达尺寸时
  返回 fallback。本项目默认路径直接采用“无资源即 null，有 fallback 才拷贝”。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp`
  `652-684`：`QIconLoader::iconEngine()` 依次尝试插件、用户主题、
  fallback 主题、平台图标库，最终保证返回有效引擎。
- 同文件 `704-726`：`QThemeIconEngine` 保存图标名称，`key()` 返回
  `QThemeIconEngine`，`clone()` 复制名称。本项目引擎键为
  `qicon://theme/<name>`，克隆只复制名称并重新走主题解析。
- 同文件 `740-773`：`QThemeIconEngine::proxiedEngine()` 在主题键变化时
  重建底层引擎；本项目内部按每次取图重新解析，未实现 Qt 的缓存失效代理。
- 同文件 `849-874`、`960-984`：`entryForSize()` 先精确匹配目录尺寸，
  再按最小距离选择；`scaledPixmap()` 使用物理尺寸并保留 DPR。本项目内部
  按目标尺寸在所有主题目录中选最近匹配，并在取回后缩放到方形目标尺寸。

实现范围：

- `Src/XGui/Icon/XIconThemeEngine.h` / `.c`：新增主题图标引擎，继承
  `XIconEngine`，完整重载 Paint/ActualSize/Pixmap/AddPixmap/AddFile/Key/
  Clone/Read/Write/AvailableSizes/IconName/IsNull/ScaledPixmap/VirtualHook
  以及 XClass 的 Copy/Move/Deinit；符合项目虚函数表重载规范。
- `Src/XGui/Icon/XIconThemeInternal.h` / `.c`：新增内部主题资源解析，
  按当前主题、fallback 主题、主题目录旧式路径、root 旧式路径依次查找；
  支持的扩展名顺序为 PNG/SVG/XPM/BMP，SVG 支持随
  `XIMAGECODEC_SVG_VECTOR_ON` 裁剪。
- `Src/XGui/Icon/XIcon.c`：`XIcon_fromTheme` / `XIcon_fromTheme_2` 改为
  创建 `XIconThemeEngine`；未找到主题资源时若传入 fallback 则拷贝，否则
  保持空图标。
- `XGui.md` 前序恢复记录保留，未回滚用户已有改动。
- `xgui_regression_test.c`：新增 `test_icon_theme_engine_contract()`，覆盖
  无资源时的 fallback/空图标行为、引擎名称/键/克隆契约。

验证结果：

- `cmake -S . -B build && cmake --build build -j$(nproc)`：全量构建通过，
  本轮新增的 `XIconThemeEngine.c` 无编译警告。
- `./bin/XGuiRegression_Test`：退出码 0，输出 `XGui regression tests
  passed`；新增主题引擎断言全部通过。
- `build-crop-svg`（`XIMAGECODEC_SVG_VECTOR_ON=0`）：重新配置、构建并通过
  回归，确认主题引擎在裁剪 SVG 渲染后不会引用 SVG 解码路径。
- `build-crop-jpeg`（`XIMAGECODEC_JPEG_ON=0`）：重新配置、构建并通过
  回归；裁剪过程独立配置在独立构建目录，回归结果互不污染默认构建。
- `ctest --test-dir build`：仍无注册用例，以直接运行回归可执行文件为准。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查；回归输出中的
  `XWindow::setTransientParent` / `XClass::ArgIsNULL` 等日志为既有测试路径
  输出，本轮主题引擎代码未新增该类日志。

近似边界与未完成项：

- 当前不读取 `index.theme` 的 `Inherits` / `Context` 等元数据，目录选择按
  常见 XDG 尺寸目录和上下文目录的静态顺序近似，未完整实现 freedesktop
  目录类型（Fixed/Scalable/Threshold/Fallback）规则。
- `theme_scaledToSize` 目前强制缩成方形目标尺寸，非方形目标请求会丢失宽高比；
  Qt 底层对每个目录方向使用独立匹配结果，本项目尚未逐目录分方向处理。
- `availableSizes()` 返回空列表；Qt 主题引擎会返回已收集条目的尺寸集合。
- 未实现 `qtIconCache` 与 `QPixmapCache` 生命周期；Read/Write 暂不支持主题
  引擎序列化。
- 未处理 Qt 平台主题图标引擎插件工厂；后续仍需按优先级完成 XPainter 更多
  opcodes、畸形 BMP/mask/缓存生命周期/Picture 序列化等 Qt fixture 回归，
  以及 QImage color-space/文本元数据扩展 API。


### 10.39 2026-08-26 XPainter 形状 API 公开声明补全

本轮修复将 `XPainter_drawEllipse / drawArc / drawPie / drawChord /
drawRoundedRect` 的公有声明补入 `Src/XGui/Graphics/XPainter.h`，并统一使用
`XPAINTER_SHAPE_ON` 条件编译裁剪。此前实现已在 `XPainter.c` 中存在且回归
直接调用，但头文件缺少声明，属于公共 API 暴露不完整，会影响外部调用方按
头文件检索接口。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h`
  常用形状绘制接口声明；`drawEllipse`、`drawArc`、`drawPie`、
  `drawChord`、`drawRoundedRect` 均为 QPainter 公有绘图 API。
- 本项目 `XPainter.c` 现有实现保持不动，仅将签名声明补齐，未改变行为语义。

实现范围：

- `Src/XGui/Graphics/XPainter.h`：新增 `XPAINTER_SHAPE_ON` 块内的五个公开
  声明，全部带中文注释，声明完整保留原实现已有的角度和填充语义。
- 未改动 `XPainter.c` 形状实现，未改动 `XPicture` 指令编码。

验证结果：

- `cmake -S . -B build && cmake --build build -j$(nproc)`：构建通过。
- `./bin/XGuiRegression_Test`：退出码 0，输出 `XGui regression tests
  passed`；既有形状回归用例继续通过。
- 构建日志未出现本次声明补全引入的新错误；修复前后回归输出中的
  `XWindow::setTransientParent` / `XClass::ArgIsNULL` 等日志为既有测试路径。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查。

近似边界与未完成项：

- 形状仍按曲线采样折线绘制，圆角矩形仍按角采样近似，量化边界与 10.31 节一致。
- `XPicture` 录制这些形状时仍由 `XPainter` 分解为基础画线/填充指令，
  未新增独立形状 opcode；后续“XPainter 便携回调更完整 opcode”仍待推进。


### 10.40 2026-08-26 XPainter 便携形状回调与形状开关验证

本轮在 XPainter 现有基础回调之上增加形状命令高层回调，用于未绑定内置
image/picture 后端的便携绘制引擎：自定义设备可通过 `m_drawShape` 直接接收
椭圆、圆弧、扇形、弦形和圆角矩形命令，不需要再把形状拆成大量折线或填充
指令；内置后端仍保持原有分解路径不变。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h`
  `265-303`：`QPainter::drawEllipse/drawArc/drawPie/drawChord/
  drawRoundedRect` 的形状 API 与角度/圆角参数语义。
- 本项目现有 `XPainter.c` 形状实现保持原参数口径：角度为 1/16 度，
  `spanAngle == 0` 对圆弧/扇形/弦不派发直接 no-op，圆角矩形半径归零退化
  为普通矩形。

实现范围：

- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterShapeOp` 枚举和
  `XPainterDrawShapeProc` 回调，并作为 `XPainter.m_drawShape` 成员暴露。
- `Src/XGui/Graphics/XPainter.c`：`begin_image/begin_picture/end` 初始化或
  清空 `m_drawShape`；五个形状 API 在输入校验后若回调非空则派发回调并直接
  返回其结果，否则继续原有栅格/录制分解。
- `xgui_regression_test.c`：新增 `test_painter_shape_callback_contract()`
  覆盖五个 opcode 的参数派发、圆角半径和零跨度不派发。
- `XGui.md` 保持 10.38/10.39 恢复与公开声明记录，未回滚用户已有改动。

验证结果：

- 默认 `cmake -S . -B build && cmake --build build -j$(nproc)` 构建通过，
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- `build-crop-shape`（`-DXPAINTER_SHAPE_ON=0`）重新配置、构建并回归通过，
  确认形状回调在硬裁剪下不编译、不进入固件。
- 回归输出中的 `XWindow::setTransientParent` / `XClass::ArgIsNULL` 等日志为
  既有测试路径输出，本轮新增代码未产生该类日志。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查；后续回归仍会保留该风险项。

近似边界与未完成项：

- 形状回调默认只对自定义设备生效，内置 image/picture 后端仍把形状分解为
  基础画线/填充指令，因此 `XPicture` 尚未新增独立形状 opcode。
- `XPainterDrawShapeProc` 当前覆盖椭圆/弧/扇形/弦/圆角矩形，多边形、点集、
  路径、文本等更高层 opcode 仍是后续“便携回调更完整 opcode”的待办项。

### 10.41 2026-08-26 XPainter 多边形/点集便携回调与裁剪判定

本轮在 10.40 的形状回调基础上，为折线、多边形、点集增加高层便携回调。
自定义绘制引擎现在可以直接接收 `drawPolyline / drawPolygon / drawPoints`
命令，不需要在设备侧自己拆成逐段画线或逐点绘制；内置 image/picture 后端
仍保持原有分解路径不变。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h`
  `236-239`：`drawPoints(const QPoint*, int)` 点集 API 与返回值语义。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h`
  `272-285`：`drawPolyline / drawPolygon / drawConvexPolygon` 的点数组与
  数量参数语义；`drawConvexPolygon` 无填充参数，始终使用当前画刷填充。

实现范围：

- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterDrawPolylineProc`、
  `XPainterDrawPolygonProc`、`XPainterDrawPointsProc` 回调原型，并以
  `XPainter.m_drawPolyline / m_drawPolygon / m_drawPoints` 成员公开，全部
  位于 `XPAINTER_POLYGON_ON` 裁剪开关内。
- `Src/XGui/Graphics/XPainter.c`：`begin_image / begin_picture / end` 中
  初始化或清空三个回调；`drawPolyline / drawPolygon / drawPoints` 在输入
  校验后若回调非空则派发并直接返回结果，否则继续原有栅格/录制分解。
- `XPainter_drawConvexPolygon` 仍通过 `XPainter_drawPolygon(..., true)`
  派发，因此自定义引擎能统一收到带 `filled == true` 的多边形命令。
- `xgui_regression_test.c`：新增 `test_painter_polygon_callback_contract()`，
  覆盖折线/多边形/点集各一次派发、参数透传、`drawConvexPolygon` 经
  `drawPolygon` 派发，以及 NULL/0 数量不派发。
- 修正画刷测试的裁剪条件：`test_painter_brush_contract()` 仅当
  `XPAINTER_BRUSH_ON && XPAINTER_POLYGON_ON` 时编译，避免多边形裁剪后
  测试二进制引用不存在的 `XPainter_drawPolygon`。
- `XGui.md` 前序恢复记录保留，未回滚用户已有改动。

验证结果：

- 默认 `cmake -S . -B build && cmake --build build -j$(nproc)` 构建通过，
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- `build-crop-polygon`（`-DXPAINTER_POLYGON_ON=0`）重新配置、构建并回归
  通过，确认折线/多边形/点集回调及画刷多边形用例整体不进入裁剪固件；裁剪
  构建完成后已重新构建默认配置，共享 `bin/` 输出恢复为多边形开启版。
- 回归输出中的 `XWindow::setTransientParent` / `XClass::ArgIsNULL` 等日志为
  既有测试路径输出，本轮新增代码未产生该类日志；构建日志中的既有指针类型
  与 `XSignal` 宏警告来自其他模块，本轮未新增错误。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查。

近似边界与未完成项：

- 多边形回调只覆盖折线/多边形/凸多边形/点集，未增加 path、文本等更高层
  opcode；仍是“便携回调更完整 opcode”的后续待办。
- 本次未改变内置后端对多边形的扫描线/画线分解行为，`XPicture` 继续以
  基础画线/填充指令录制这些操作，没有新增独立多边形 opcode。
- 尚未覆盖触摸到大量点或多边形顶点超过 `XPAINTER_POLY_MAX_POINTS` 时的
  回调边界；当前与内置实现一致，先截断到最大值再派发。


### 10.42 2026-08-26 XPainter 路径便携回调与路径裁剪判定

本轮在 10.41 的多边形回调基础上，为路径绘制增加高层便携回调
`XPainter.m_drawPath`，自定义绘制引擎可以直接接收 `drawPath / fillPath /
strokePath` 三种路径命令，无需自行展平折线/贝塞尔段；内置 image/picture
后端仍保持原有展平原逻辑不变。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h`
  `228-230`：`strokePath / fillPath / drawPath` 三个路径接口的签名顺序与
  语义：`strokePath` 需额外指定画笔，`fillPath` 需额外指定画刷，`drawPath`
  同时使用当前画笔与画刷。

实现范围：

- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterPathOp` 枚举
  （Draw/Fill/Stroke）与 `XPainterDrawPathProc` 回调原型，并在
  `XPainter.m_drawPath` 成员公开，全部位于 `XPAINTER_PATH_ON` 裁剪开关内。
  回调通过 `XPainterPath` 前向声明避免类型声明顺序问题，形参类型为 `const XPainterPath*`。
- `Src/XGui/Graphics/XPainter.c`：`begin_image / begin_picture / end` 中
  初始化或清空 `m_drawPath`；新增 `painterPathDrawDispatch` 统一入口，先做
  参数与设备校验，再优先派发高层回调，否则退回原 `painterPathDraw` 展平实现。
- `XPainter_drawPath` 派发 `XPainterPathOp_Draw`，`XPainter_fillPath` 派发
  `Fill`，`XPainter_strokePath` 派发 `Stroke`。
- `xgui_regression_test.c`：新增 `test_painter_path_callback_contract()`，
  覆盖 Draw/Fill/Stroke 三种命令各一次派发、路径元素数透传、NULL/空路径
  不派发，以及 `XPainter_end()` 后回调清空。

验证结果：

- 默认 `cmake -S . -B build && cmake --build build -j$(nproc)` 构建通过，
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- `build-crop-path`（`-DXPAINTER_PATH_ON=0`）重新配置、构建并回归通过，
  确认路径回调与路径测试整体不进入裁剪固件；裁剪构建完成后已重新构建默认
  配置，共享 `bin/` 输出恢复为路径开启版。
- 回归输出中的 `XWindow::setTransientParent` / `XClass::ArgIsNULL` 等日志为
  既有测试路径输出，本轮新增代码未产生该类日志；构建日志中的既有指针类型
  与 `XSignal` 宏警告来自其他模块，本轮未新增错误。
- 本机未安装 `valgrind`，本轮未执行内存泄漏检查。

近似边界与未完成项：

- 路径回调只覆盖 `drawPath / fillPath / strokePath` 三个高层入口，未新增
  path 内部的独立 opcode；自定义后端仍需根据命令自选展平或原生绘制策略。
- 本次未改变内置后端对路径的展平行为，`XPicture` 继续以基础画线/填充指令
  录制路径操作，没有新增独立路径 opcode。
- 未覆盖超长路径在回调路径上的内存/错误边界；当前与内置实现一致，空路径与
  NULL 路径视为无操作，不会派发回调。


### 10.43 2026-08-26 XLabel/XAbstractButton/XPushButton 回归样例对齐 Qt 6.8 实际行为

本节完成恢复后的 XLabel/XFrame/XPushButton 回归断言修正，使测试期望与 Qt 6.8.3 实际行为一致，并清零最后一项 Label 离屏绘制失败。

Qt 源码依据：

- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:643：setDown() 不发射 pressed/released。
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:700：setSelection(start, length) 语义为起点加长度。
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qpushbutton.cpp:324：setDefault() 只更新默认按钮状态，不切换 autoDefault。

实现范围：

- xgui_regression_test.c：label link 信号探针改为拷贝链接文本，避免 XString 参数生命周期结束后悬垂读取；setPicture 用例先写入非空绘图记录再验证往返；Label 离屏绘制底色改为白色，取样点改为覆盖内置 8x16 字形实际落点（drawRect.x + 2, drawRect.y + 7）。
- Button 测试更新：setDown 前后均保持 released 计数，确认不新增信号；setDefault 仅验证 isDefault()，并确认不改动 autoDefault()。
- Src/XGui/Graphics/XPainter.h：为 XPainterDrawPathProc 增加 XPainterPath 前向声明，消除路径回调参数列表中的 struct 前向警告。
- xgui_regression_test.c：补充 <math.h>，消除 fabsf 隐式声明警告。

验证结果：

- cmake --build build --target XGuiRegression_Test -j$(nproc) 通过；./bin/XGuiRegression_Test 输出 XGui regression tests passed。
- 回归输出中的 XWindow / XClass 非致命日志为既有测试路径输出，不影响结果。
- 本机未安装 valgrind，本轮未执行内存泄漏检查。

近似边界与未完成项：

- XPushButton 的对话框父级 autoDefault 联动仍未实现，pushbutton_autoDefaultActive 当前返回 false，仅对齐按钮自身 setDefault/autoDefault 语义。
- 后续继续按自动化优先级推进：图像 Handler 发现/注册与 BMP 外编解码器回归、XIcon_paint/图标引擎/主题查找、XPainter 更多 opcodes、畸形 BMP/mask/缓存生命周期/Picture 序列化 fixture、QImage color-space/文本元数据。

### 10.44 2026-08-26 图像 Handler 发现/注册：内置 XImageIOPlugin 自动注册

本节在文件恢复确认完成后，把 XImageCodec 支持的格式通过 Qt 风格图像插件
接口暴露给 XImagePluginRegistry，XImageReader/XImageWriter 可直接从插件
注册表创建内置处理器；同时修复 XMovie 动画帧被插件处理器二次读取的问题。

Qt 源码依据：

- /home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.h
  `101-111`：QImageIOPlugin 的 Capability 枚举，CanRead=0x1、
  CanWrite=0x2、CanReadIncremental=0x4。
- /home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp
  `84-95`：supportedImageFormats 先聚合内置格式，再经插件加载器追加
  外部插件格式，最后排序。

实现范围：

- `Src/XGui/Graphics/XImageIOPlugin.h`：新增
  `XImageIOPlugin_CanReadIncremental = 0x04`，与 Qt 6.8 能力位一致。
- 新增 `Src/XGui/Graphics/XImageBuiltinPlugin.{h,c}`：声明并实现内置插件
  单例，keys/mimeTypes/nameFilters 覆盖 `bmp,png,jpeg,gif,svg` 与对应
  image/* MIME。虚处理器实现 canRead/read/write/supportsOption(Quality)；
  read 按格式名或内容探测路由到 XImageCodec，write 按格式编码后回写设备。
- `Src/XGui/Graphics/XImagePluginRegistry.c`：新增 `g_builtinRegistered`
  与 `XImagePluginRegistry_ensureBuiltin()`，在查询插件数、取插件、创建读/
  写处理器、格式/MIME 支持判断与格式列表等入口自动注册内置插件；
  `clear()` 会重置内置注册标记，允许此后重新发现。
- `Src/XGui/Graphics/XImageReader.c`：GIF 动画帧已缓存或已由 Codec 加载时，
  不再调用 `ensureHandler` 也不再用现有处理器重复 read，修复动画帧切换被
  新增内置插件处理器覆盖造成 XMovie `jumpToNextFrame` 失败的问题。
- `xgui_regression_test.c`：`test_image_handler_registry()` 在
  `XIMAGEIOPLUGIN_ON` 下断言 `XImagePluginRegistry_pluginCount() >= 1`，
  确认内置插件自动注册。

验证结果：

- `cmake -S . -B build` 通过；`cmake --build build --target
  XGuiRegression_Test -j"$(nproc)"` 通过；`./bin/XGuiRegression_Test`
  输出 `XGui regression tests passed`。
- 额外构建的 ASan 明细未把本轮新增图像插件代码列入泄漏栈；其余既有
  Widget/Icon 与系统库泄漏独立记录，未混入本轮结论。
- 本机仍未安装 `valgrind`，未做 valgrind 全流程泄漏检查。

近似边界与未完成项：

- 内置插件目前是源码级懒注册单例，尚未实现动态插件发现、卸载生命周期或
  注册表容量扩展；`CanReadIncremental` 仅声明能力位，未实现增量读取语义。
- 编解码仍由 XImageCodec 完成，BMP/PNG/JPEG/GIF/SVG 之外格式需先补齐
  Codec；畸形输入、mask、缓存生命周期与 Picture 序列化 fixture 仍待回归。
- XIcon_paint/图标引擎/主题查找、XPainter 更多 opcodes、XPushButton 后续
  对齐项未在本节推进。

### 10.45 2026-08-26 XPainter 文本布局方向与视觉对齐对齐 Qt 6.8

本节继续对齐 `XPainter.h` 的 QPainter 文本状态接口，补齐可裁剪的布局方向
状态，并将 `drawTextRect` 的水平对齐、绝对对齐和制表符展开接入该状态。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:347-348`：
  `setLayoutDirection(Qt::LayoutDirection)` 与 `layoutDirection() const` 的
  公共签名。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7357-7372`：
  setter 仅在 painter state 存在时写入；无状态 getter 返回 `LayoutDirectionAuto`，
  `QPainterState::init()` 则取应用的有效方向（默认 LTR）。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7139-7160`：
  `TextForceLeftToRight/TextForceRightToLeft` 优先级、绘制器方向回退、
  `visualAlignment` 与 RTL 条件下的 `TextExpandTabs` 规则。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qguiapplication_p.h:174-183`：
  未指定水平对齐时补 `AlignLeft`，RTL 且非 `AlignAbsolute` 时交换左右标志。

实现范围：

- `Src/XGui/Graphics/XPainter_config.h` 与 `Src/XGui/XGuiConfig.h`：新增
  `XPAINTER_LAYOUT_DIRECTION_ON`，默认开启；关闭时裁剪状态字段、setter/getter
  和方向推断代码，保持嵌入式硬裁剪语义。
- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterLayoutDirection` 枚举、
  `XPainterState.m_layoutDirection` 状态字段、`AlignAbsolute` 数值位
  `0x0010` 及 `XPainter_setLayoutDirection/layoutDirection` 声明，数值与 Qt
  对齐。
- `Src/XGui/Graphics/XPainter.c`：未激活查询返回 Auto，激活状态初始化为应用有效
  方向；setter 对非法枚举值回退 Auto；`drawTextRect` 按 ForceLTR/ForceRTL、绘制器方向或首个强 RTL 码点
  决定方向，再执行视觉左右交换和 Qt 条件制表符展开。
- `xgui_regression_test.c`：增加未激活 Auto、激活默认 LTR、RTL 左对齐翻转、
  `AlignAbsolute` 保持物理左对齐及 getter/setter 回归断言。

验证结果：

- 默认 `cmake --build build --target XGuiRegression_Test -j2` 通过，
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- `build-crop-layout` 使用 `-DXPAINTER_LAYOUT_DIRECTION_ON=0` 重新配置、
  构建并回归通过，确认方向 API 与状态字段可整体裁剪。
- `build-asan` 目标构建通过；`ASAN_OPTIONS=detect_leaks=1` 运行时发现既有
  XIcon、XScreen/GL、fontconfig 等泄漏（栈不涉及本节新增布局代码），因此
  本轮地址检查未报告新增越界/Use-after-free，但全局泄漏检查仍未清零。
- `git diff --check` 通过。本项目全量构建仍有既有指针类型、`XSignal` 宏和
  第三方 zlib 条件指令警告，本节未宣称零警告。

近似边界与未完成项：

- `LayoutDirectionAuto` 使用 UTF-8 首个强 RTL 区间启发式判断，仅覆盖常见
  Hebrew/Arabic 及扩展区间；未引入 Qt 完整 Unicode 双向算法，也不重排混合
  RTL/LTR 字符顺序。方向状态、视觉对齐和展开规则与 Qt 一致，但复杂双向文本
  的字形顺序仍是嵌入式点阵后端的已知近似。
- `TextExpandTabs` 现按 Qt 规则仅在视觉左/右对齐方向上展开；自定义 tab stop
  和 `QTextOption` 尚未引入，仍按固定 8 字符列宽处理。
- 后续仍需按优先级推进图像 codec/fixture、XIcon_paint 主题回退、更多
  XPainter/XPicture opcode，以及 QImage color-space/文本元数据扩展。

### 10.46 2026-08-26 XPainter RenderHint 状态接口对齐 Qt 6.8

本节继续对齐 QPainter 的渲染提示状态，并为嵌入式构建提供独立裁剪开关。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:45-75`：
  `RenderHint` 位值，包括 `Antialiasing`、`TextAntialiasing`、
  `SmoothPixmapTransform`、`VerticalSubpixelPositioning`、
  `LosslessImageRendering` 与 `NonCosmeticBrushPatterns`。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:404-407`：
  `setRenderHint`、`setRenderHints`、`renderHints` 与单项测试接口的签名。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:1830`：
  新建绘制器默认启用 `TextAntialiasing`。

实现范围：

- `Src/XGui/Graphics/XPainter_config.h` 与 `Src/XGui/XGuiConfig.h`：新增
  `XPAINTER_RENDERHINT_ON`，默认开启；置 0 时整体裁剪 RenderHint 枚举、状态
  字段及 setter/getter/test API。
- `Src/XGui/Graphics/XPainter.h`：新增同值 `XPainterRenderHint` 枚举、
  `XPainterRenderHints` 位集、`XPainterState.m_renderHints` 和四个状态接口。
- `Src/XGui/Graphics/XPainter.c`：默认状态初始化为 `TextAntialiasing`；单项和
  位集 setter 使用 Qt 同样的 OR/AND 清除语义，并在绘制器未激活时保持 Qt 的
  无操作行为；getter 对未激活绘制器或空指针返回 0，单项测试仅在请求位全部
  设置时返回真。
- `xgui_regression_test.c`：增加未激活状态、默认提示、启用/清除多个提示位及
  单项查询断言。

验证结果：

- 默认 `cmake --build build --target XGuiRegression_Test -j2` 通过，常规
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- `build-crop-renderhint` 使用 `-DXPAINTER_RENDERHINT_ON=0` 配置、构建并回归
  通过，确认关闭状态后不残留 API 依赖。
- `build-asan` 目标构建及 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1`
  回归通过，未发现本节新增越界或 Use-after-free。开启泄漏检测仍报告既有
  XIcon、XScreen/GL、fontconfig 等泄漏，未归因于 RenderHint。
- `git diff --check` 通过；项目既有不兼容指针、`XSignal` 宏及第三方条件指令
  警告仍存在，本节未宣称全量零警告。

近似边界与未完成项：

- RenderHint 状态数值、默认值和位运算语义已与 Qt 对齐，但当前便携点阵/绘制
  后端尚未针对每个提示位改变光栅化策略；状态接口可用于后续后端接入。
- `LosslessImageRendering` 与 `NonCosmeticBrushPatterns` 保留 Qt 6.8 位值，
  不在不支持对应后端能力的嵌入式构建中强行模拟效果。
- XPainter 其余 opcode、图像 codec/fixture、XIcon_paint 主题回退及 QImage
  color-space/文本元数据仍按后续优先级推进。

### 10.47 2026-08-26 XPainter 世界矩阵启用状态对齐 Qt 6.8

本节补齐 `QPainter::setWorldMatrixEnabled/worldMatrixEnabled`，使世界变换矩阵
可以在保留数值的同时暂时停用，并为嵌入式提供独立裁剪开关。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2845-2885`：
  `setWorldMatrixEnabled` 只在 painter active 时修改 `WxF`，禁用不清空世界矩阵；
  `worldMatrixEnabled` 在未激活时返回 false。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2900-2980`：
  `translate/scale/rotate/shear/setWorldTransform` 会启用世界矩阵。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7915-7974`：
  `resetTransform` 清除世界矩阵并关闭 `WxF`，而 `worldTransform` 仍查询保存的
  世界矩阵。

实现范围：

- `Src/XGui/Graphics/XPainter_config.h` 与 `Src/XGui/XGuiConfig.h`：新增
  `XPAINTER_WORLD_MATRIX_ON`，默认开启；置 0 时裁剪状态字段与两个公共接口，
  并让实际绘制始终应用矩阵，适合不需要动态开关的嵌入式固件。
- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterState.m_worldMatrixEnabled`、
  `XPainter_setWorldMatrixEnabled` 和 `XPainter_worldMatrixEnabled` 声明。
- `Src/XGui/Graphics/XPainter.c`：新增有效变换选择器。世界矩阵关闭时，软件
  直线、矩形、图像和扫描填充使用单位矩阵；`worldTransform` 与 `transform`
  仍查询保存矩阵。所有变换 setter 在激活 painter 时启用状态，
  `resetTransform` 清除矩阵并关闭状态；状态随现有 `save/restore` 一并保存。
- `xgui_regression_test.c`：增加初始关闭、变换自动启用、关闭后 line/fill/image
  使用逻辑坐标及 save/restore 恢复断言。

验证结果：

- 默认 `cmake --build build --target XGuiRegression_Test -j2` 及
  `./bin/XGuiRegression_Test` 通过。
- `build-crop-world-matrix` 使用 `-DXPAINTER_WORLD_MATRIX_ON=0` 配置、构建并
  回归通过（裁剪分支中的新增断言自动排除）。
- `build-asan` 已对本轮 RenderHint/世界矩阵代码完成构建与运行，未发现新增
  越界或 Use-after-free；LeakSanitizer 仍有既有 XIcon、XScreen/GL 与
  fontconfig 泄漏，未归因于本节。
- `git diff --check` 通过。全量构建仍保留项目既有不兼容指针、`XSignal` 宏及
  第三方条件指令警告。

近似边界与未完成项：

- 世界矩阵开关语义和状态生命周期已对齐；window/viewport 视图变换已在下一节
  补齐。
- `XPicture` 仍只记录基础 line/fill/image/save/restore opcode，世界矩阵本身
  不序列化，回放依赖调用方当前 painter 状态；更多 Qt opcode 仍待推进。

### 10.48 2026-08-26 XPainter window/viewport 视图变换对齐 Qt 6.8

本节补齐 QPainter 的逻辑窗口、设备视口及视图变换状态。软件图像后端现在按
“世界矩阵后、视图矩阵前”的等价坐标顺序执行映射，并可将整个能力独立裁剪。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:207-218`：
  `QPainterPrivate::viewTransform()` 使用 `viewport/window` 的缩放和原点
  偏移公式。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:6929-6973`、
  `7017-7037`：`setWindow`、`setViewport` 保存矩形并启用 `VxF`。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7064-7080`：
  `setViewTransformEnabled` 只在 active painter 上修改状态，停用不清空矩形。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7915-7936`：
  `resetTransform` 恢复设备矩形并同时关闭世界和视图变换。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7986-7994`：
  `combinedTransform` 查询保存世界矩阵与视图矩阵的组合结果。

实现范围：

- `Src/XGui/Graphics/XPainter_config.h` 与 `Src/XGui/XGuiConfig.h`：新增
  `XPAINTER_VIEW_TRANSFORM_ON`，默认开启；置 0 时裁剪窗口、视口、视图状态
  字段与对应 API，实际绘制仅使用世界变换。
- `Src/XGui/Graphics/XPainter.h`：新增 `XPainterState.m_window`、
  `m_viewport`、`m_viewTransformEnabled`，以及
  `XPainter_setWindow/window/setViewport/viewport/`
  `setViewTransformEnabled/viewTransformEnabled/combinedTransform` 接口；公共
  参数、返回与退化窗口行为均以中文注释说明。
- `Src/XGui/Graphics/XPainter.c`：图像设备 begin 时将默认 window 与 viewport
  初始化为图像边界；`setWindow` 或 `setViewport` 会启用状态，停用时保留两个
  矩形；`resetTransform` 恢复设备边界并停用状态。软件线段、矩形、图像和扫描
  填充统一使用视图矩阵乘世界矩阵；`XPainter_combinedTransform` 对应 Qt 的组合查询。
- `xgui_regression_test.c`：新增默认设备矩形、非零 window/viewport 原点与
  缩放、实际像素落点、停用后逻辑坐标绘制、状态保存恢复和 reset 复位断言。

验证结果：

- 默认 `cmake --build build --target XGuiRegression_Test -j2` 通过，常规
  `./bin/XGuiRegression_Test` 输出 `XGui regression tests passed`。
- 其余裁剪构建、地址检查和全量构建将在本节完成后统一执行并补录结果。

近似边界与未完成项：

- 零宽或零高逻辑 window 的 Qt 内部会生成不可用于软件光栅化的比例；XPainter
  保留该状态但让后续软件绘制返回失败，避免非有限坐标进入嵌入式后端。
- `XPicture` 目前只记录基础绘图 opcode，不会把 window/viewport 或变换状态
  序列化到指令流；图片回放仍使用目标 painter 的当前状态。这是既有 Picture
  格式限制，不作为本节“完全序列化”实现宣称。
- XPainter 其余 opcode、图像 codec/fixture、XIcon 主题回退及 QImage
  color-space/文本元数据仍按后续优先级推进。

### 10.49 2026-08-26 XPainter ClipOperation、逻辑裁剪与硬裁剪

本节将矩形裁剪改为 QPainter 6.8 的 `ClipOperation` 形式，并把裁剪能力
置于独立的嵌入式硬裁剪开关后。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.h:1310-1314`：
  `Qt::ClipOperation` 的 `NoClip`、`ReplaceClip`、`IntersectClip` 枚举值。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.qdoc:2415-2422`：
  三种裁剪操作的替换、求交与关闭语义。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:174-190`：
  `setClipRect`、`hasClipping`、`setClipping` 与 `clipBoundingRect` 的公共接口。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2398-2421`、
  `2635-2675`、`2677-2778`：裁剪启用状态、逻辑坐标边界查询和矩形裁剪操作实现。

实现范围：

- `Src/XGui/Graphics/XPainter_config.h` 与 `Src/XGui/XGuiConfig.h`：新增
  `XPAINTER_CLIP_ON`，默认值为 1。置 0 时 `XPainterClipOperation`、裁剪状态和
  全部裁剪公共接口均从头文件及实现中移除，软件像素路径也不再进行裁剪判断。
- `Src/XGui/Graphics/XPainter.h`：增加中文注释完整的
  `XPainterClipOperation`，`XPainter_setClipRect(self, rect, operation)`、
  `XPainter_hasClipping`、`XPainter_setClipping` 与
  `XPainter_clipBoundingRect`。旧的 `XPainter_resetClip`、`XPainter_clipRect`
  和 `XPainter_map` 已删除，因为它们不是 Qt 6.8 QPainter 的对应公共 API。
- `Src/XGui/Graphics/XPainter.c`：输入矩形以当前绘图器逻辑坐标解释，先经
  `combinedTransform` 映射到软件设备坐标。`ReplaceClip` 覆盖、`IntersectClip`
  求交、`NoClip` 关闭；空矩形仍构成有效的“裁剪一切”状态，NULL 输入无操作。
  `setClipping(false)` 保留描述，`setClipping(true)` 仅在存在可恢复描述时重新启用；
  `clipBoundingRect` 用当前逆变换回映射，按 Qt 的逻辑坐标查询约定返回边界。
- `XFrame`、`XLabel`、`XPushButton` 和矩形文本布局改为传入自身逻辑裁剪矩形，
  由绘制器完成坐标变换，避免控件偏移被重复相加。
- `xgui_regression_test.c`：覆盖替换/相交/NoClip、关闭后再启用、NULL 无操作、
  空裁剪、随 translate 变换的逻辑边界以及裁剪关闭配置。

近似边界与未完成项：

- 当前只实现矩形裁剪；Qt 的 `setClipRegion`、`setClipPath` 与复杂区域运算尚未
  引入对应 XRegion/XPainterPath 裁剪接口（区域接口已在 10.53 增补）。
- 旋转或错切后的矩形在软件后端以设备坐标轴对齐包围盒裁剪；这不同于 Qt 保留的
  精确变换后区域，不能宣称该情形已逐像素完全对齐。
- XPicture 指令流尚不记录裁剪操作，回放仍使用目标绘制器的当前裁剪状态。

验证结果：

- 默认 `XGuiRegression_Test` 干净重建完成，`ctest --test-dir build --output-on-failure`
  通过（1/1）。
- `XPAINTER_CLIP_ON=0` 的独立配置 `build-crop-clip` 能构建并直接通过同一回归程序；
  裁剪类型、状态和接口均已从该配置的公共 API 中裁剪。
- ASan 回归程序在 `detect_leaks=0:halt_on_error=1` 下通过。当前受控运行环境通过
  `ptrace` 启动子进程，LeakSanitizer 会直接报运行环境不支持，故不能据此宣称已完成
  泄漏检查；应在不受该限制的目标环境单独执行 LSan。
- 全量默认构建已完成，但工程既有的第三方和非 XGui 警告仍存在，例如
  `Library/zlib/zconf.h:255` 以及 `Src/XClass/XObject.h:433`；本节不将其记为零警告。

### 10.50 2026-08-26 XGui 自动测试统一入口

为消除菜单式输出测试与自动回归测试的重复，普通 XGui 测试现统一收敛到根目录的
`xgui_regression_test.c`：该文件由 `XGuiRegression_Test` 构建，并登记为唯一的
CTest 项 `XGuiRegression`。它直接以断言式结果返回失败状态，集中覆盖 XImage、格式与
编解码器、读写器、XBitmap/XPixmap 与缓存、XIcon、XPicture、XPainter、控件、布局、
屏幕和图形后端。

实现范围：

- 删除 `Test/XGuiTest/` 下原先通过菜单打印结果的重复 `.c`/`.h` 测试单元，并移除
  `Test/XMenuTest.c` 对该菜单的注册；该目录只保留被统一回归程序包含的编解码 fixture
  头文件。
- `CMakeLists.txt` 启用 CTest 并登记 `XGuiRegression`。为适配受控测试环境，CTest
  运行时关闭 LSan 的泄漏扫描，ASan 的越界和 use-after-free 检查仍保留；真实泄漏检查
  必须在不经 ptrace 的环境单独运行。
- `XLabel` 已纳入该统一入口的离屏像素验证：标签分别以 16 和 32 像素字号显示
  `Scale` 文本，测试同时检查字号读回、`sizeHint` 增长以及放大后实际非背景文字像素
  数量增加，避免只验证 `setScaledContents` 的像素图缩放开关。
- `xgui_xdnd_test.c` 仍为 Linux/X11 跨客户端端到端测试。`xgui_window_demo.c` 是唯一的
  GUI 控件人工可视化测试载体：后续控件场景均在这个后备缓冲绘制链路中追加，不再新增
  菜单式控件测试程序；两者都不混入可移植的统一自动回归。

验证结果：

- 删除旧菜单测试后，`XinYueC_Dynamic` 与 `XGuiRegression_Test` 均可构建。
- `ctest --test-dir build --output-on-failure` 已通过 `XGuiRegression`（1/1）。
- 加入 XLabel 文字显示和 16 到 32 像素放大验证后，默认 CTest 再次通过；
  `XPAINTER_CLIP_ON=0` 的独立回归程序也通过。

### 10.51 2026-08-26 GUI 控件可视化测试载体

`xgui_window_demo.c` 与其 `XGuiWindowDemo_Test` 可执行文件现作为 GUI 控件的唯一人工
可视化测试载体。自动化语义、边界与裁剪回归仍只写入 `xgui_regression_test.c`；需要人工
观察的控件状态则统一在本窗口的 `XBackingStore -> XPainter` 绘制链路中追加，禁止再创建
菜单式或新的独立控件演示程序。

当前场景在浅绿色测试面板中创建真实 `XLabel` 并经 `XPainter_translate` 绘制：`XLabel 1x`
使用 16 像素点阵字号，`XLabel 2x` 使用 32 像素字号。运行
`./bin/XGuiWindowDemo_Test` 后，应在棋盘格右侧同时看到两行黑色文字，第二行高度和字宽
均为第一行的两倍；标题为“XinYueC GUI 控件可视化测试 (X11/Win32)”。当
`XLABEL_ON=0` 时，面板改显示 `XLabel disabled`，使嵌入式裁剪状态可见。

### 10.52 2026-08-27 XPainter Qt 6.8 状态、画刷与裁剪对齐

本节优先核对 `Src/XGui/Graphics/XPainter.h`/`.c` 与 Qt 6.8
`QPainter`/`QBrush` 的状态语义，并保留嵌入式可裁剪配置。对齐以 Qt 源码行为为准，
不把项目额外提供的旧式便利函数当作 Qt 公共 API。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:1660-1795`、
  `1848-1885`：`begin`/`end` 对活动状态、无效设备和状态栈清理的处理。
- `qpainter.cpp:2015-2055`、`2324-2378`：不透明度默认值、活动状态写入、合成模式
  默认值和非法/不支持模式的拒绝语义。
- `qpainter.cpp:2419-2460`、`2632-2655`、`7915-7994`：`setClipping` 保留已保存
  裁剪描述、`clipBoundingRect` 查询以及世界/组合变换状态。
- `qpainter.cpp:3611-3788`、`3807-3858`、`6892-6905`、`6920-6930`、
  `7357-7375`：画笔、画刷、背景、字体、渲染提示、视图变换和布局方向的活动状态
  约束及默认值。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qbrush.cpp:1553-1630`：
  渐变停止点范围检查、排序、同位置覆盖和空停止点的黑到白隐式渐变。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qtransform.cpp:352-455`：
  变换矩阵 mutator 的参数与组合规则。

实现范围：

- `XPainter_begin_image`/`XPainter_begin_picture` 在已有活动绑定时返回 `false`，并
  保留原设备与状态；`XPainter_end` 仅对活动绘制器成功，符合 Qt 的生命周期约定。
- `XPainter_drawRect` 先按当前画刷填充，再按当前画笔绘制四条边；默认画刷为
  `NoBrush`，因此默认只描边。`XPainter_fillRect_2` 改为读取当前画刷，分别处理
  `NoBrush`、纯色和渐变画刷，空矩形仍是成功的无操作；未绑定设备时所有非空
  绘制请求（包括 NoBrush 的 `fillRect_2`）返回失败。
- 渐变停止点使用有限值和 `[0,1]` 范围检查，按位置排序，同位置停止点覆盖旧颜色；
  软件路径在没有停止点时使用 Qt 对应的黑到白端点插值。线段、折线、多边形、路径
  和渐变填充的便携回调失败会向上传播，不再静默报告成功。
- 变换、画笔/画刷、背景、字体、透明度、布局方向和合成模式的写入均要求 painter
  处于活动状态；`setOpacity(NaN)` 按 Qt 的边界行为归一为 `0`。当前只接受
  `SourceOver` 与 `Source` 合成模式，其他枚举值保持先前状态。
- 路径曲线展平、动态顶点扩容、矩阵角点映射和失败清理均补充了空指针及分配失败
  处理；`XPAINTER_PATH_ON` 可独立开启，路径回调与基础回调一样传播失败。
- `XPainter_config.h` 的 `XPAINTER_*_ON` 开关继续控制画刷、路径、形状、多边形、
  文本布局、世界矩阵、视图变换和裁剪。`XPAINTER_SHAPE_ON=0`、
  `XPAINTER_POLYGON_ON=0`、`XPAINTER_BRUSH_ON=0`、`XPAINTER_PATH_ON=1` 等裁剪组合
  不会暴露被禁用的公共类型或调用路径，适合资源受限的嵌入式构建。

验证结果：

- 默认配置：`cmake --build build --target XGuiRegression_Test --clean-first -j1`、
  `./bin/XGuiRegression_Test` 和 `ctest --test-dir build --output-on-failure` 均通过，
  CTest 为 1/1。
- 路径裁剪配置 `build-crop-path` 使用
  `-DXPAINTER_SHAPE_ON=0 -DXPAINTER_POLYGON_ON=0 -DXPAINTER_BRUSH_ON=0
  -DXPAINTER_PATH_ON=1 -DXPAINTER_TEXTLAYOUT_ON=0 -DXPAINTER_WORLD_MATRIX_ON=0
  -DXPAINTER_VIEW_TRANSFORM_ON=0 -DXPAINTER_CLIP_ON=0`，目标和统一回归程序均通过。
- 最小裁剪配置 `build-crop-min` 进一步关闭形状、多边形、画笔样式、画刷、路径、
  文本布局、布局方向、渲染提示、世界矩阵、视图变换和裁剪，仍能成功配置、构建并
  通过同一 `XGuiRegression_Test`；该结果证明基础线段、矩形、显式颜色填充和点阵
  文本路径可独立保留。
- 完整默认构建 `cmake --build build -j1` 成功。构建仍报告工程既有的非 XPainter 警告，
  例如 `Library/zlib/zconf.h:255` 的预处理指令和多个 `XClass` 类型转换警告，因而
  不宣称“零警告”。本轮未在受控 `ptrace` 环境重复运行 LSan；此前环境会因
  LeakSanitizer 不支持 `ptrace` 子进程而中止，泄漏状态仍需目标机单独确认。

近似边界与未完成项：

- 合成模式已覆盖 Qt 6.8 的 24 个 Porter-Duff/SVG 模式和 14 个 RasterOp 模式；
  RasterOp 在软件 XImage 后端按完整 ARGB32 像素逐位运算。
- 软件渐变、路径曲线展平、复杂多边形及旋转/错切后的裁剪仍是便携近似实现；裁剪
  使用设备坐标轴对齐包围盒，不能宣称与 Qt 的精确逐像素区域完全一致。
- 当前公共 API 仍未覆盖 Qt 的 `QBrush`/`QPen` 全部构造重载、`drawImage` 的
  `sourceRect`/pixmap 变体、区域/路径裁剪、背景模式与图片序列化中的完整 painter
  状态。后续按 XGui 优先级继续补齐。

本节不创建提交、不推送代码；工作树中的既有修改保持原样。

### 10.59 2026-08-27 XPainter 变换组合顺序与画刷回调状态

本轮继续按 Qt 6.8 的矩阵乘法顺序和多边形画刷语义修正 XPainter：

- Qt 依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7947-7983`，
  `setWorldTransform(matrix, true)` 将新矩阵放在当前矩阵左侧；依据
  `qtransform.cpp:816-950`，`QTransform` 的 `a * b` 映射顺序为先作用 `a`、再作用
  `b`。XPainter 现在在 `setTransform(..., true)` 中使用 `matrix * current`，而
  `translate/scale/rotate/shear` 按 Qt 的便捷操作将新变换左乘当前矩阵。
- Qt 依据 `qpainter.cpp:7900-7904`、`qpainter.cpp` 的
  `combinedTransform()` 实现，组合查询按 world * view 顺序；XPainter 的实际绘制、
  组合查询和设备变换查询已统一使用该顺序，避免非交换矩阵下平移与缩放结果反转。
- `XPainter_drawPolygon` 的便携回调 `filled` 现在由当前画刷决定；`NoBrush` 仅描边，
  与 Qt `drawPolygon` 的当前 `QBrush::style()` 语义一致。原生/嵌入式回调仍可依据
  该字段自行选择填充实现。

验证结果：默认 `XGuiRegression_Test` 构建并运行通过，新增非交换矩阵组合和
`NoBrush` 多边形回调断言；`build-crop-min` 裁剪配置构建并运行通过，随后已重新构建
默认回归二进制。`git diff --check` 无空白错误。完整工程既有的跨类型和第三方警告仍然
存在，因此不宣称全工程零警告；AddressSanitizer 回归在 `detect_leaks=0` 下通过，当前
受控环境的 LSan 仍受 `ptrace` 限制。

近似边界：XPainter 仍以单精度矩阵、整数 XRect 和便携软件光栅化为主，复杂 QTransform
透视、QPainterPath 精确裁剪及抗锯齿像素覆盖尚未达到 Qt 光栅引擎的逐像素一致性。

### 10.53 2026-08-27 XPainter QRegion 区域裁剪对齐

在 10.52 矩形裁剪基础上补齐 Qt 6.8 的区域裁剪入口，并保持独立裁剪开关，
用于资源受限的嵌入式配置。

Qt 源码依据：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:167-190`：
  `clipRegion()` 与 `setClipRegion()` 的公共签名及逻辑坐标约定。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2464-2565`：
  区域裁剪查询、`NoClip` 清空、`IntersectClip` 求交及逆矩阵回映射规则。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2797-2870`：
  区域裁剪写入时的活动状态检查、未启用时相交转替换以及状态记录规则。
- `/home/xinyue/Code/XinYueC/Src/XData/XGeometry.h:245-570` 与
  `XGeometry.c:477-660`：`XRegion` 的初始化、合并、交集、包围矩形和点包含
  操作，全部使用项目内存接口。

实现范围：

- `XPainter_config.h` 新增 `XPAINTER_CLIP_REGION_ON`，依赖 `XPAINTER_CLIP_ON`；
  关闭后不暴露区域 API，也不增加状态对象大小。
- `XPainterState` 保存设备坐标区域，`save/restore/end` 对区域数组进行深拷贝和
  释放，避免状态栈浅拷贝造成双重释放或悬垂指针。
- `XPainter_setClipRegion` 支持 `ReplaceClip`、`IntersectClip`、`NoClip`，区域中
  每个逻辑矩形按当前组合变换映射后参与软件像素裁剪；`XPainter_clipRegion`
  逆变换输出逻辑坐标区域，关闭 clipping 后仍保留查询描述，符合 Qt 的状态语义。
- 统一回归新增不相邻矩形、区域间隙、区域查询以及 save/restore 恢复测试；区域
  裁剪会在后续多边形测试前显式关闭，避免跨用例污染。

验证结果与边界：

- 默认 `XGuiRegression_Test` 通过；ASan `detect_leaks=0:abort_on_error=1` 通过，
  未发现越界或 use-after-free。LSan 仍受当前受控环境 `ptrace` 限制，未宣称泄漏
  检查通过。
- `XPAINTER_CLIP_REGION_ON=0` 的裁剪配置应仅保留矩形接口；非恒等变换下区域
  输出按整数轴对齐包围矩形离散化，复杂路径裁剪和 `QPainter::setClipPath` 仍未
  实现；XPicture 录制仍不序列化裁剪状态。

### 10.54 2026-08-27 XPainter 形状公共签名清理

本轮继续按 Qt 6.8 `QPainter` 公共头文件清理 XPainter 的历史 C 扩展参数：

- Qt 依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:277-297`：
  `drawPolygon(const QPoint*, int, Qt::FillRule)`、`drawPie(const QRect&, int, int)`
  和 `drawChord(const QRect&, int, int)` 不带独立的 `filled` 布尔参数；是否填充由
  当前 `QBrush::style()` 决定。
- `XPainter_drawPolygon` 现为 `(XPainter*, const XPoint*, int, XPainterFillRule)`，
  `XPainter_drawPie`/`XPainter_drawChord` 现为 `(XPainter*, const XRect*, int, int)`；
  `NoBrush` 时只描边，其他画刷按当前状态填充，公共调用不再覆盖画刷状态。
- `XPainterDrawShapeProc` 和 `XPainterDrawPolygonProc` 仍是便携后端回调的工程扩展，
  其 `filled` 字段只用于内部描述“当前画刷是否应参与填充”，不是 Qt 对外 API，
  以便在无原生图形引擎的嵌入式后端保留一次回调派发。
- 统一回归已同步删除所有旧式 `filled` 实参，并保留 `SolidPattern`、渐变和
  `NoBrush` 三种状态的像素断言；`drawConvexPolygon` 继续无额外参数，与 Qt 头文件
  一致。`XPainterFillRule` 对应 Qt 的 `OddEvenFill`（默认）和 `WindingFill`，
  当前扫描线实现同时支持两种规则。

验证结果：`cmake --build build --target XGuiRegression_Test --clean-first -j1` 成功，
随后 `./bin/XGuiRegression_Test` 与 `ctest --test-dir build --output-on-failure` 均通过
（1/1）；`git diff --check` 无空白错误。完整构建输出仍含仓库既有的第三方和跨类型警告，
不宣称全工程零警告；未提交、未推送。

### 10.55 2026-08-27 XPainter 多边形 FillRule 对齐

本轮继续对齐 Qt 6.8 `QPainter::drawPolygon` 的填充规则参数，并保持嵌入式可裁剪：

- Qt 依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:277-280`，
  `drawPolygon` 的最后参数为 `Qt::FillRule`，默认值是 `Qt::OddEvenFill`；
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:4548-4629` 说明
  多边形首尾隐式闭合、画刷参与填充，并将规则传给绘图引擎。
- XPainter 增加 `XPainterFillRule_OddEven`/`XPainterFillRule_Winding`，公共
  `XPainter_drawPolygon` 接口签名与 Qt 语义对应；非法枚举值归一化为默认奇偶规则。
  `XPainter_drawConvexPolygon` 固定走奇偶规则，符合 Qt 凸多边形接口不带规则参数。
- 软件扫描线保存交点横坐标和边方向：奇偶规则按排序交点两两配对，绕组规则仅在
  非零绕组区间的进入/退出边界生成填充跨度；当前仍采用整数像素覆盖的便携近似，
  不模拟 Qt 光栅引擎的抗锯齿和半像素采样。
- `XPainterDrawPolygonProc` 的 `filled` 保留为便携后端扩展，同时新增 `fillRule`，
  使原生回调可以选择与 Qt 对应的填充算法；旧式公共 `filled` 参数已删除。
- `XPAINTER_POLYGON_ON=0` 时多边形公共 API 和回调仍整体裁剪；填充规则枚举保持
  轻量定义，以便形状/路径内部在其他裁剪组合下复用统一扫描线类型。

验证：默认 `XGuiRegression_Test` 已重新编译并通过，`ctest` 1/1 通过；回调测试
验证 Winding 规则原样传递，NULL/少于两个顶点仍是无操作。后续应补充自交多边形的
像素 fixture，并继续核对 Qt 的抗锯齿、复杂路径和设备后端行为。未提交、未推送。

### 10.61 2026-08-27 XPainter QTransform 矩阵乘法与便捷变换顺序

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qtransform.cpp:29-59`
的点映射字段布局及 `816-950` 的 `QTransform::operator*` 展开式，修正
`painterMatrixMultiply`：`XImageTransform` 现在按
`[m11 m12 m13; m21 m22 m23; dx dy m33]` 行向量布局逐项计算，与 Qt 的
`m11/m12/m21/m22/m31/m32/m13/m23/m33` 一一对应。此前实现误将字段当作列向量，
会在缩放、平移组合时产生错误偏移。

对照 Qt `QTransform::translate/scale/shear/rotate`（同文件 `352-520`）的分支更新，
`XPainter_translate/scale/rotate/shear` 统一改为“新矩阵左乘当前矩阵”；这与 Qt
在缩放后平移按缩放系数放大偏移、平移后缩放保持原偏移的行为一致。`setWorldTransform`
继续使用 `matrix * current`，`combinedTransform` 继续使用 `world * view`，与
`qpainter.cpp:7947-7983` 保持同一组合顺序。

回归新增非交换顺序断言：缩放 `(2,3)` 且偏移 `(1,2)` 后平移 `(4,5)` 得到偏移
`(9,17)`；平移 `(1,2)` 后缩放 `(2,3)` 保留偏移 `(1,2)`。默认和裁剪配置均覆盖这些
分支。软件后端仍使用单精度浮点，透视与抗锯齿由便携实现近似；未提交、未推送。

### 10.56 2026-08-27 XPainter QPen 默认状态对齐

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpen.cpp:226-266`
及 `qpainter.cpp:3611-3642`，修正 XPainter 画笔状态：Qt 默认 `QPen` 为黑色、1
像素、`SolidLine`、`SquareCap`、`BevelJoin`；`setPen(const QColor&)` 构造新的
实线画笔并恢复同样的宽度/端点/连接默认值。`XPainterPenCapStyle` 和
`XPainterPenJoinStyle` 的枚举值现采用 Qt 的位值（`0x00/0x10/0x20` 与
`0x00/0x40/0x80`），默认查询和颜色重载均与 Qt 一致。`XPainter_setPenWidth`、
`setPenStyle` 等独立 C API 保留为嵌入式便携扩展，但须在 `setPen` 之后调用以覆盖
默认状态。

回归新增默认端点/连接断言，并验证设置虚线、宽度、圆头/圆角后再调用颜色版
`setPen` 会恢复 Qt 默认状态。实现仍不模拟 Qt 光栅引擎真实端点几何，当前字段主要
供状态查询与后端回调使用。未提交、未推送。

### 10.57 2026-08-27 XPainter 设备变换查询对齐

Qt 6.8 在 `qpainter.h:197` 暴露 `QPainter::deviceTransform()`，实现见
`qpainter.cpp:7896-7904`。XPainter 新增 `XPainter_deviceTransform` 并复用组合
变换计算；内置 XImage 与 XPicture 后端都以 `(0,0)` 为设备原点，没有平台窗口的
额外偏移，因此结果与 `XPainter_combinedTransform` 一致，未激活时返回单位矩阵。
回归在 window/viewport 映射启用时增加设备变换的系数与偏移断言，不新增状态，兼容
现有裁剪开关。

### 10.58 2026-08-27 XPainter 画笔/画刷样式重载与矩形规范化

继续核对 Qt 6.8 的状态重载和椭圆/弧形输入处理：

- Qt 依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3611-3686`、
  `3726-3780`：`setPen(Qt::PenStyle)` 使用黑色、1 像素和默认端点/拐角重新构造
  画笔；`setBrush(Qt::BrushStyle)` 使用黑色画刷并替换原样式，非法样式退化为空画刷。
- 新增 `XPainter_setPen_2` 与 `XPainter_setBrush_2`，分别对应上述两个 Qt 重载；
  两者都要求绘制器处于活动状态，调用后清除旧的宽度、端点、拐角或渐变状态，
  使后续 `pen()`/`brush()` 查询与 Qt 的替换语义一致。原有颜色版和独立样式 setter
  继续作为 C 便携接口保留。
- Qt 依据 `qpainter.cpp:3963-4040`、`4070-4130`、`4154-4185`、`4225-4250`：
  椭圆、圆弧、扇形和弦形先对 `QRect/QRectF` 做 normalized；扇形额外将起始角
  归一化到 `[0, 5760)`。XPainter 现在通过 `XRect_normalized` 传递规范化矩形，
  扇形按同一 1/16 度规则归一化，并让形状回调的 `filled` 准确反映当前 `QBrush`
  是否为 NoBrush。圆角矩形同样在半径处理前规范化输入矩形。

验证结果：默认 `XGuiRegression_Test` 新增样式重载、NoPen/NoBrush、负尺寸矩形和
回调填充状态断言，构建、回归和 CTest 均通过；默认全工程构建仍只有仓库既有警告。
已保留所有裁剪开关，未提交、未推送；抗锯齿与浮点 QRectF 仍由整数 XRect 的便携
实现近似。

### 10.60 2026-08-27 XPainter Qt 画笔/画刷枚举值对齐

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.h:1091-1099`
和 `1119-1139`，XPainter 的 `XPainterPenStyle`/`XPainterBrushStyle` 数值现与 Qt
一致：加入 `CustomDashLine=6`，补齐 2-14 的内置画刷图案，渐变样式固定为
15/16/17，`TexturePattern=24`。这样录制流、外部回调和跨模块状态传递不会因自定义
枚举值偏移而误判样式。

`CustomDashLine` 在没有动态 `QPen::setDashPattern` 存储时使用确定性的默认虚线节距；
内置图案和纹理在软件后端使用当前纯色填充，属于嵌入式裁剪下的明确近似。对照
`qbrush.cpp:342-355`、`433-452` 与 `653-662`，`XPainter_setBrush_2` 对渐变/纹理
样式按 Qt 的 `QBrush(color, style)` 规则归一为空画刷，独立
`XPainter_setBrushStyle` 对同样样式保持原画刷状态；其他未知枚举值原样保留。默认
回归、最小裁剪回归、CTest 和 `git diff --check` 均通过。

### 10.62 2026-08-27 XPainter QPen 宽度边界对齐

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpen.cpp:588-598`
的 `QPen::setWidth(int)` 实现，XPainter 的画笔宽度状态现遵循相同边界：宽度 `0`
保留为 cosmetic pen，负值及大于等于 `1 << 15` 的值被拒绝且不改变原状态。软件
光栅路径在实际逐像素绘制时仍把 cosmetic 宽度解释为一像素，从而分别满足 Qt 的
状态查询语义和嵌入式后端的整数像素约束；状态结构注释同步标明 `0` 的特殊含义。

本轮统一回归新增宽度为 0、-1 和 32768 的断言，并确认恢复到 1 像素后既有线型测试
保持不变。默认 `XGuiRegression_Test` 构建、运行和 CTest 1/1 通过；所有功能裁剪的
`build-crop-min` 构建及 CTest 通过；AddressSanitizer 在 `detect_leaks=0` 下通过，
未发现越界或 use-after-free。LSan 仍受受控环境 `ptrace` 限制，不能宣称泄漏检查
通过；构建输出中的信号宏和跨类型警告为既有问题。

近似边界：XPainter 保留的 `setPenWidth` 是面向 C/嵌入式调用的便携接口，不提供 Qt
`QPen::widthF` 的浮点状态；宽度大于 1 的软件光栅仍采用整数平行偏移，端点几何、
抗锯齿和复杂变换下的真实覆盖率与 Qt 原生光栅引擎存在差异。

### 10.63 2026-08-27 XPainter CompositionMode 与背景模式对齐

本轮继续对齐 Qt 6.8 `QPainter` 状态接口：

- Qt 依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:97-140`，
  补齐 `CompositionMode_SourceOver` 至 `CompositionMode_Exclusion` 的 24 个同序枚举；
  `qpainter.cpp:2324-2369` 说明非扩展设备按能力拒绝不支持模式，而 XImage 软件后端
  具备完整 Porter-Duff/SVG 混合路径。
- `painterComposeColor` 先将 XImage 的非预乘 ARGB 转为预乘分量，按
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2168-2258` 描述的
  Porter-Duff 和 SVG 1.2 规则计算，再还原非预乘颜色；`SourceOver`、`Source`、
  `Clear`、`Destination` 等状态均通过统一 `painterRaster_putPixel` 路径生效。
  Qt RasterOp 的 14 个枚举值按原序声明并由 XImage 后端逐位实现；不超出 Qt 6.8
  范围的值均可设置，越界值仍保持原状态。
- Qt 依据 `qpainter.h:160-161` 与 `qpainter.cpp:3565-3603` 增加
  `XPainterBackgroundMode_Transparent/Opaque`、`XPainter_setBackgroundMode` 和
  `XPainter_backgroundMode`；状态随 `save/restore` 保存，默认值为 Transparent，
  与背景颜色 setter 分离，便于嵌入式文本后端按需实现 Opaque 背景。
- `XPainter_config.h` 增加 `XPAINTER_BACKGROUND_ON`，关闭后裁剪背景模式枚举、字段
  和 API，不影响 `XPainter_setBackground` 颜色接口。

验证：默认 `XGuiRegression_Test` 构建、运行和 CTest 1/1 通过；回归覆盖 24 个合成
模式的状态设置、Clear 清除目标、Destination 保留目标及背景模式非法值保持状态。
此前 `build-crop-min`、ASan（`detect_leaks=0`）已通过；LSan 受受控环境 ptrace 限制，
未宣称泄漏检查通过。浮点混合使用 8 位预乘近似，QPainterPath 裁剪和设备抗锯齿仍属于
未实现边界；RasterOp 位操作已在 10.77 条目中补齐；未提交、未推送。

### 10.64 2026-08-27 XPainter drawImage 目标/源矩形重载与嵌入式裁剪

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:362-379`
和 `qpainter.cpp:5103-5208` 的 `drawImage(targetRect, image, sourceRect)`
重载，XPainter 新增 `XPainter_drawImageRect`。接口要求目标矩形、源图像和源矩形
均非空；目标宽度或高度为零、或源区域裁剪后为空时按 Qt 无操作语义返回 true，
未激活绘制器或非法输入返回 false。源宽度/高度非正的“取到图像边缘”、目标负尺寸
以及源越界比例裁剪规则已在 10.76 条目中补齐并取代本条目的早期简化描述。

软件 XImage 后端逐目标像素逆映射到用户坐标，在源矩形内使用最近邻采样，并复用
既有裁剪、整体透明度、合成模式和透视矩阵路径；Picture 后端通过
`XImage_copyRect` 截取源区域，再用 `XImage_scaled` 生成目标尺寸，最后写入已有
DrawImage opcode，避免扩展持久化格式。新增
`XPAINTER_IMAGE_RECT_ON` 开关可在嵌入式构建中裁剪该重载，位置绘制的
`XPainter_drawImage` 不受影响。

同时移除 XPainter.c 对 `<stdlib.h>` 的依赖，使用 `int64_t` 绝对值比较避免引入
标准库内存接口；默认回归新增源/目标矩形缩放、像素分区及空目标矩形断言。

验证：默认 `XGuiRegression_Test` 和 CTest 1/1 通过；
`build-crop-background-off`（`XPAINTER_BACKGROUND_ON=0`）构建、运行和 CTest 通过；
`git diff --check` 通过。软件路径仍采用最近邻和整数像素覆盖，Picture 录制不保存
调用时的变换/裁剪状态，属于现有便携 Picture opcode 的已知边界；未提交、未推送。

### 10.65 2026-08-27 XPainter 背景画刷重载与 Opaque 文本行为

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3794-3822`
及 `7403-7418`，`QPainter::setBackground(const QBrush&)` 保存完整背景画刷，
初始化状态为不透明白色实心画刷；`qpainter.h:160-161` 规定该画刷只在
`OpaqueMode` 绘制不透明文本、点阵线和位图时生效。XPainter 在现有 C 画刷结构
基础上增加 `XPainter_setBackground_2` 与 `XPainter_backgroundBrush`，并让颜色版
`XPainter_setBackground` 同步更新背景画刷的实心颜色和样式。背景画刷随
`XPainter_save/restore` 状态栈复制，默认值为 `SolidPattern/#ffffffff`，与 Qt
`QPainterState::init()` 一致。

点阵文本路径在 `XPainterBackgroundMode_Opaque` 下先填充每个 8x16 字形单元，再
绘制前景字形；渐变背景使用其基色，`NoBrush` 不填充。该策略保持固定内存和
整数像素成本，适用于嵌入式后端，但与 Qt 复杂字体 glyph bounds、渐变采样以及
点阵线/位图背景的完整引擎语义仍有差异。接口由
`XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON` 双开关裁剪；关闭任一开关时不暴露
背景画刷字段、重载或实现，保留便携颜色接口。

验证：默认 `XGuiRegression_Test` 构建、运行和 CTest 1/1 通过；另以
`-DXPAINTER_BRUSH_ON=0` 构建 `build-crop-bgbrush-off` 并运行同一统一回归，确认
背景画刷字段、样式枚举和样式专用断言均被裁剪，测试仍输出
`XGui regression tests passed`。为此在 `test_painter_shape_callback_contract` 与
`test_painter_polygon_callback_contract` 中增加 `XPAINTER_BRUSH_ON` 条件，使关闭
画刷时仍验证矩形规范化和回调派发，但不引用不存在的 `NoBrush` 样式符号。裁剪产物
完成后已重新构建默认配置并恢复共享 `bin/XGuiRegression_Test`。构建输出仍含仓库
既有的信号宏、跨类型删除和 const 丢弃警告，未宣称零警告；未提交、未推送。

### 10.66 2026-08-27 XPainter 未激活状态查询语义对齐

本轮针对 Qt 6.8 在绘制器未激活时的状态查询行为做了逐项核对：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3593-3603`
  规定 `backgroundMode()` 在没有绘制引擎时返回 `TransparentMode`；
  `7369-7375` 规定无状态时 `layoutDirection()` 返回 `Qt::LayoutDirectionAuto`。
- `qpainter.cpp:2384-2392` 的 `background()` 在未激活时读取 `fakeState()->brush`；
  `qpainter_p.h:148-153` 说明该虚拟状态使用默认 `QBrush`，而
  `qbrush.cpp:321-339,393-403` 明确默认值为 `NoBrush` 与不透明黑色。
- 同一虚拟状态规则还覆盖 `qpainter.cpp:6875-6909` 的空 `renderHints()`、
  `6918-6925` 的 `viewTransformEnabled()==false` 和 `2877-2885` 的
  `worldMatrixEnabled()==false`；XPainter 原有实现已保持这些默认值。

实现调整：`XPainter_backgroundMode`、`XPainter_layoutDirection` 在 `self==NULL`
或设备类型为 `None` 时分别返回 Transparent/Auto，不再暴露已保留的状态栈值；
`XPainter_backgroundBrush` 在同样条件下输出 Qt 默认的 `NoBrush/#ff000000`，活动
绘制器仍返回当前完整画刷。新增回归断言覆盖这三种未激活查询，同时保留活动状态、
非法 setter 保持原值以及 `save()/restore()` 的行为。画刷实现和查询均受
`XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON` 控制，便于嵌入式裁剪。

另外，`XPainter_drawImageRect` 对源坐标采样使用 `floorf`，避免负坐标 C 截断导致
错误采样；源越界处理的最终比例裁剪语义见 10.76 条目。

验证：默认配置执行 `cmake --build build --target XGuiRegression_Test -j1`、
`./bin/XGuiRegression_Test` 和 CTest 1/1 均通过；输出中的 `XError` 仅为回归程序
既有的非法父窗口及空事件释放诊断。此前 `build-crop-bgbrush-off`
（`XPAINTER_BRUSH_ON=0`）构建、运行和 CTest 均通过，随后已重新构建默认二进制。
构建仍报告仓库其他模块已有的信号宏、跨类型删除和 const 丢弃警告，未宣称零警告；
LSan 仍受受控环境 ptrace 限制，未宣称泄漏检查通过。渐变背景、复杂字体字形、
点阵线/位图背景、最近邻缩放和复杂字体字形仍是 XPainter 的已知近似边界；RasterOp
位操作已在 10.77 条目中补齐；未提交、未推送。

### 10.76 2026-08-27 XPainter drawImageRect 参数与越界裁剪修正

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:5158-5208`
及 `qpainter.h:770-774`，`QPainter::drawImage(const QRect&, ...)` 先转换为
`QRectF`，再执行以下参数规则：源宽度或高度小于等于零时取从源起点到图像边缘；
目标宽度或高度小于零时取源尺寸除以图像 `devicePixelRatio`；源矩形超出图像左/上/右/下
边界时按源到目标的比例裁掉对应目标区域（左/上越界还会平移目标起点）；任一最终目标轴
为零或源区域为空时直接返回。该行为与“交换负尺寸边界”不同，也不把越界目标像素写成
透明色。

`XPainter_drawImageRect` 现在由 `painterPrepareImageRect` 统一计算上述浮点参数：
软件光栅路径以整理后的目标矩形逆变换采样，源裁剪后的比例和目标平移与 Qt 一致；
Picture 路径使用整理后的整数源区域和四舍五入后的目标尺寸复用现有 DrawImage opcode，
因此 `devicePixelRatio` 非整数或裁剪比例产生亚像素目标时仍有便携格式的整数取整边界。
目标零宽/零高仍保持无操作成功，源宽/高零值则按 Qt “取到边缘”解释。

回归 `test_painter_raster_contract` 新增负目标宽度、零源宽度取边缘、源左越界裁剪并平移
目标三组像素断言；默认配置 `XGuiRegression_Test` 通过。Qt 规则依据和实现均在
`Src/XGui/Graphics/XPainter.c` 的 `painterPrepareImageRect`、
`painterMapImageRectCorners` 与 `painterRaster_drawImageRect` 中保留中文注释。
构建输出仍含仓库既有信号宏、const 丢弃和跨类型删除警告；LSan 受受控环境 ptrace
限制，未宣称泄漏检查通过；未提交、未推送。

### 10.67 2026-08-27 XPainter eraseRect 背景画刷语义

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:6554-6564`，
`QPainter::eraseRect` 等价于把当前 `state->bgBrush` 传给 `fillRect`，而不是仅以
`background().color()` 填充。XPainter 现将 `XPainter_eraseRect` 在
`XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON` 开启时临时切换到
`m_backgroundBrush`，复用 `XPainter_fillRect_2` 的 `NoBrush`、纯色和渐变路径，
随后恢复前景画刷状态；背景画刷不再污染后续普通填充。关闭背景画刷能力时保留
颜色版兼容实现，满足嵌入式裁剪。

统一回归新增实心背景画刷、前景画刷保持不变及 `NoBrush` 背景不写像素断言；默认
`XGuiRegression_Test` 构建、运行和 CTest 1/1 通过，`git diff --check` 通过。
构建仍报告仓库其他模块已有的信号宏、跨类型删除和 const 丢弃警告；LSan 受受控
环境 ptrace 限制，未宣称泄漏检查通过。Picture 后端对复杂渐变的录制仍通过已有
便携填充 opcode 退化为分段颜色，属于已知近似边界；未提交、未推送。

### 10.68 2026-08-27 XPainter 总开关裁剪联动

修正 `Src/XGui/Graphics/XPainter_config.h` 中总开关的实际行为：当
`XPAINTER_ON=0` 时，现在同步把形状、多边形、画笔样式、画刷、背景、图像源/目标
矩形、路径、文本布局、布局方向、渲染提示、世界矩阵、视图变换、矩形裁剪及区域
裁剪开关置零。由此所有对应枚举、状态字段、公共扩展 API 和静态实现会从预处理
结果中裁剪；基础 XPainter 生命周期、直线/矩形/图像位置绘制、透明度和基础变换
仍保留，便于依赖库在同一头文件下完成最小嵌入式构建。

依据 `XGuiConfig.h` 的 `XGUI_ON`/子开关集中配置约定，本联动避免了此前总开关为零
但可选 API 仍被暴露的配置不一致。验证：`build-crop-painter-off` 使用
`-DXPAINTER_ON=0` 成功构建 `XinYueCS` 与统一 `XGuiRegression_Test`，运行及 CTest
1/1 通过；随后重新构建默认 `build` 目标并确认默认回归、CTest 1/1、`git diff --check`
均通过。完整构建仍有仓库既有跨类型指针、信号宏和 const 丢弃警告，未宣称零警告；
LSan 受受控环境 ptrace 限制，未宣称泄漏检查通过。未提交、未推送。

### 10.69 2026-08-27 XPainter 组合模式透明像素边界

对照 Qt 6.8 `qpainter.cpp:2168-2258` 的 Porter-Duff 逐像素定义，软件
`painterComposeColor` 对 `Clear`、`Source`、`Destination` 以及
`SourceOver` 的全透明源/空目标增加直接返回路径：Clear 始终输出
`0x00000000`，Source/Destination 保留原始 ARGB32（包括半透明 RGB），
透明源覆盖不改变目标，透明目标下 SourceOver 直接返回源。这样避免了
预乘/反预乘往返造成的半透明通道舍入漂移，同时不影响 SourceIn、Atop、Xor
及 SVG 混合模式的预乘计算。

统一回归新增半透明 Source、Destination、透明 SourceOver 及 14 个 RasterOp 的精确
像素断言，并保留非法枚举断言为“保留调用前的 SourceOver 状态”。默认配置重新构建、
运行及 CTest 1/1 通过；`build-crop-painter-off`（`XPAINTER_ON=0`）重新构建、
运行及 CTest 1/1 通过，随后恢复默认 `bin/XGuiRegression_Test`。构建输出仍有
回归程序中既有的信号宏、跨类型删除和 const 丢弃警告，未宣称零警告；LSan 受
受控环境 ptrace 限制，未宣称泄漏检查通过。浮点颜色仍按 8 位整数预乘近似，抗锯齿
和复杂字体排版属于未完成边界；未提交、未推送。

### 10.70 2026-08-27 XPainter NoClip 查询记录对齐

根据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2743-2776`
和 `2401-2447`，`setClipRect(..., Qt::NoClip)` 会把裁剪操作标记为
`NoClip` 并令 `hasClipping()` 返回 false，但仍保留这一次记录；因此随后的
`clipBoundingRect()` 和 `clipRegion()` 会分别返回传入的矩形/区域，而不是空值。
区域重载在 `qpainter.cpp:2795-2845` 遵循相同规则。

XPainter 现把 NoClip 的逻辑矩形或区域映射后保存在 `m_clipRect/m_clipRegion`，将
`m_hasClipRect` 保持为 true、`m_clipOperation` 设为 NoClip、`m_hasClip` 设为 false；
查询接口允许读取该记录，而 `setClipping(true)` 仍因 NoClip 标记拒绝重新启用，和 Qt
的 `clipInfo.constLast().operation == Qt::NoClip` 守卫一致。统一回归新增矩形和区域
查询断言，并保留空裁剪、禁用后保留查询以及重新设置有效裁剪的覆盖。

验证：默认 `XGuiRegression_Test` 构建、运行、CTest 1/1 通过；随后将以
`-DXPAINTER_ON=0` 的 `build-crop-painter-off` 重新构建并运行同一测试，完成后恢复
默认二进制。构建输出中的信号宏、跨类型删除和 const 丢弃警告属于仓库既有问题，未
宣称零警告；LSan 仍受受控环境 ptrace 限制，未宣称泄漏检查通过。复杂路径裁剪和
浮点区域离散化仍为便携近似边界；未提交、未推送。

### 10.71 2026-08-27 XPainter 非激活背景颜色查询对齐

Qt 6.8 `QPainter::background()` 在绘制器未激活时读取 `fakeState()->brush`
（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2384-2392`）；
`QPainterDummyState` 的默认 `QBrush` 是 `NoBrush`，其颜色为不透明黑色。XPainter
的颜色便捷查询 `XPainter_background()` 现区分三种状态：空指针仍返回 0 作为 C API
错误值，非空但未绑定设备返回 `0xff000000`，活动设备返回当前背景画刷颜色，和
Qt 的虚拟状态/活动状态分支一致。完整背景画刷查询此前已按同一规则返回
`NoBrush/#ff000000`。

统一回归新增未激活背景颜色断言，并保留活动背景颜色、背景画刷和 `save()/restore()`
覆盖。默认构建、运行、CTest 以及 `XPAINTER_ON=0` 裁剪配置将在本轮验证；构建中
已有信号宏、跨类型删除和 const 丢弃警告仍需单独治理，不能宣称零警告；LSan 受
受控环境 ptrace 限制，不能宣称泄漏检查通过。未提交、未推送。

### 10.72 2026-08-27 XPainter 空裁剪矩形坐标保留

Qt 6.8 `QPainter::clipBoundingRect()` 会遍历保存的裁剪记录并映射回逻辑坐标，
即使记录对应空矩形，也不会丢弃其原点（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2632-2674`）。
`setClipRect()` 对 `QRect`/`QRectF` 的记录与 `NoClip` 清理规则见同文件
`2688-2776`；实测 `(2,2,0,0)` 查询得到 `(2,2,0,0)`，`NoClip` 传入空矩形同样保留
坐标但 `hasClipping()` 为 false。

XPainter 的裁剪矩形映射不再对零宽或零高提前返回，而是继续映射退化矩形四角，保存
变换后的原点和零尺寸；`clipBoundingRect()` 也允许查询零尺寸记录。有效裁剪仍按
轴对齐包围盒参与像素裁剪，未设置记录时继续返回零矩形。统一回归新增非零原点空矩形
断言，覆盖 ReplaceClip 后的查询行为；默认配置与 `build-crop-painter-off` 裁剪配置均
构建并运行通过。构建输出仍有仓库回归程序既有的信号宏、const 丢弃和跨类型删除警告，
不宣称零警告；LSan 受受控环境 ptrace 限制，未宣称泄漏检查通过。未提交、未推送。

### 10.73 2026-08-27 XPainter 画刷样式切换清理渐变载荷

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qbrush.cpp:550-590`
的 `QBrush::detach()` 以及 `653-662` 的 `QBrush::setStyle()`：样式参数先经过
`qbrush_check_type()` 校验，渐变/纹理样式调用被拒绝；从渐变样式切换到任何普通
样式时会分离为新的 `QBrushData`，因此旧渐变停止点、几何参数不再属于当前画刷。

XPainter_setBrushStyle 继续拒绝三个渐变样式和 TexturePattern；当当前画刷是任一
渐变样式且目标样式合法时，先清零 `m_gradient` 再保存新样式，使 XPainter_brush()
的结构化查询与 Qt 的数据分离语义一致。统一回归新增“渐变切换到 SolidPattern 后
停止点数量为零”的断言，并保留非法渐变样式被拒绝的覆盖。

验证：默认 `XGuiRegression_Test` 构建、运行、CTest 1/1 通过；随后以
`-DXPAINTER_ON=0` 的 `build-crop-painter-off` 构建并运行统一测试，最后恢复默认
二进制。构建输出仍有回归测试中既有的信号宏、跨类型删除及 const 丢弃警告，未宣称
零警告；LSan 受受控环境 ptrace 限制，未宣称泄漏检查通过。纹理画刷尚未提供图像
载荷，复杂 Qt 画刷共享数据生命周期仍属未完成边界；未提交、未推送。

### 10.74 2026-08-27 XPainter drawRect 退化矩形与 QRect 边界对齐

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3215-3248`
中 `QPainter::drawRect` 的 QRect 重载定义，以及 Qt 光栅后端对整数矩形边界的
实测结果，整数 QRect 的描边边界使用 `x + width` 与 `y + height`，因此宽度或高度
单轴为零时仍绘制一条包含端点的直线；仅宽高同时为零才是空操作。负宽高先交换两条
几何边，保留规范化后的端点。填充仍按 `QRect::size()` 的宽高写入，画笔随后覆盖
描边，符合 Qt 文档“填充尺寸为 rectangle.size、描边尺寸另加画笔宽度”的约定。

XPainter_drawRect 不再把所有非正尺寸直接返回。函数内使用 64 位边界计算并显式交换
负尺寸，保留零轴长度，同时将右下端点按 Qt 的包含式整数边界传给四条 drawLine；渐变
画刷多边形也统一使用规范化后的四角，避免负高度仍引用原始坐标。极端 int 溢出在
转换到回调前钳位到 `INT_MIN..INT_MAX`，不调用标准库分配或平台 API。头文件注释已
说明 NULL/双零尺寸、单轴退化和负尺寸行为。

统一回归在 `test_painter_raster_contract` 新增零宽垂直线与负宽高矩形的像素断言，并
更新原有边框、裁剪、批量矩形及 Picture 回放断言到 Qt 的 `x + width/y + height`
边界。默认配置重新构建并运行 `./bin/XGuiRegression_Test`，随后以
`-DXPAINTER_ON=0` 的 `build-crop-painter-off` 重新构建运行同一测试，最后恢复默认
测试二进制；两套运行均通过，`ctest --test-dir build --output-on-failure` 为 1/1。
构建输出仍包含回归程序既有的信号宏、跨类型删除和 const 丢弃警告，不能宣称零警告；
LSan 在受控环境受 ptrace 限制，不能宣称泄漏检查通过。浮点 QRectF、抗锯齿笔宽以及
复杂画刷仍是后续对齐边界；未提交、未推送。

### 10.75 2026-08-27 XPainter fillRect 负尺寸 QRect 规范化

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:6670-6725`
的 `fillRect(QRect, QBrush/QColor)` 实现（其非扩展路径通过 NoPen 的
`drawRect` 完成填充）以及 `3215-3248` 的整数矩形约定，并用 Qt Raster 实测
`QPainter::fillRect(QRect(5,5,w,2))`：
宽度 `-2`、`-1` 会分别填充交换边界后的 2、1 列，宽度为零才是空操作。换言之，
`fillRect` 的负尺寸不能沿用 `QRect::isEmpty()` 的直接返回逻辑，而应先交换
`x` 与 `x + width`、`y` 与 `y + height`，再按正区域填充。

XPainter 新增 `painterNormalizeFillRect`，在 64 位边界上计算并钳位后供
`XPainter_fillRect`、`fillRect_2` 和背景 `eraseRect` 共用；渐变画刷四角也改用
规范化坐标。这样软件光栅、Picture 录制回放和外部填充回调的输入边界保持一致，
零宽/零高仍返回成功但不写像素，NULL 仍按 C API 错误返回 false。

统一回归在 `test_painter_raster_contract` 增加负宽填充的像素断言（规范化区域
`x=3..4`），默认 `XGuiRegression_Test`、`build-crop-painter-off`（`XPAINTER_ON=0`）
均构建并运行通过，默认 `ctest --test-dir build --output-on-failure` 为 1/1；完整
默认工程构建成功。构建日志仍有仓库既有的信号宏、跨类型删除和 const 丢弃警告，
因此不能宣称零警告；LSan 受受控环境 ptrace 限制，不能宣称泄漏检查通过。浮点
QRectF、抗锯齿及复杂画刷仍待后续对齐；未提交、未推送。

### 10.77 2026-08-27 XPainter RasterOp 组合模式对齐

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2240-2365`，
`QPainter::CompositionMode` 在具备 RasterOp 能力的光栅设备上支持 14 个逐位模式：
SourceOrDestination、SourceAndDestination、SourceXorDestination、
NotSourceAndNotDestination、NotSourceOrNotDestination、NotSourceXorDestination、
NotSource、NotSourceAndDestination、SourceAndNotDestination、NotSourceOrDestination、
SourceOrNotDestination、ClearDestination、SetDestination 和 NotDestination。XPainter
现按完整 `uint32_t` ARGB32 像素执行与 Qt 定义一致的位运算，不经过 Alpha 预乘或颜色
反预乘；其中 `NotSourceXorDestination` 按 Qt 文档解释为 `((NOT source) XOR destination)`。

`XPainter_setCompositionMode` 现在接受 Qt 6.8 的 38 项连续枚举范围（24 个
Porter-Duff/SVG 加 14 个 RasterOp），超出范围的值继续保持旧状态。RasterOp 直接
复用 `painterRaster_putPixel`，因此仍受当前设备有效范围、裁剪和整体不透明度影响；
不透明度为 1.0 时源像素按原始 ARGB32 位模式参与运算。统一回归在
`test_painter_raster_contract` 中逐项验证 14 个 setter/getter 结果和像素值，并保留
非法枚举拒绝断言。

默认 `XGuiRegression_Test`、完整工程构建和 CTest 1/1 均通过；此前的
`XPAINTER_ON=0` 裁剪配置回归也已通过并恢复默认测试二进制。构建输出仍含仓库既有的
信号宏、跨类型删除、const 丢弃和预处理警告，不能宣称零警告；LSan 受受控环境
`ptrace` 限制，未宣称泄漏检查通过。抗锯齿和复杂字体排版仍属于近似边界；未提交、未推送。

### 10.78 2026-08-27 XPainter 多边形像素中心边界对齐

Qt 6.8 的光栅引擎在 `fillPath()` 中把变换后的路径交给扫描转换器；未开启抗锯齿时直接按设备空间栅格化（依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine_raster.cpp:1331-1359`）。整数和浮点多边形先转换为 `QVectorPath` 再扫描填充，轴对齐矩形可走 `drawRects()` 优化（同文件 `1846-1875`、`1880-1915`、`1921-1938`）。Qt 扫描转换器以半像素偏移建立边交点（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qrasterizer.cpp:193-206`、`551-590`），并以像素中心决定当前扫描线和交点的覆盖像素。

XPainter 新增 `painterSpanPixelRange()`，统一将浮点 span 转换为首尾像素：首像素为 `ceil(left - 0.5)`，末像素为 `floor(right - 0.5)`；整数轴对齐矩形右/下边保持排他，斜边交点落在像素中心时保留 Qt 的覆盖像素。用户空间和设备空间扫描填充都采用该规则；图像后端的实色和渐变多边形统一在设备空间扫描，缩放/旋转时不会把逻辑边界误当设备像素，Picture 录制仍保留用户空间逐行命令以维持可移植性。

统一回归在 `test_painter_polygon_contract` 增加 `(2,2)-(6,6)` 半开矩形边界断言，保留三角形实色、渐变和路径覆盖。默认 `XGuiRegression_Test`、`build-crop-painter-off`（`XPAINTER_ON=0`）构建运行均通过；恢复默认产物后 `ctest --test-dir build --output-on-failure` 为 1/1，完整默认工程构建退出码为 0。构建输出仍有仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告，不能宣称零警告；LSan 受受控环境 `ptrace` 限制，未宣称泄漏检查通过。抗锯齿、多边形复杂度上限和纹理画刷仍属于后续边界；未提交、未推送。

### 10.79 2026-08-27 XPainter 轴向线端点样式对齐

Qt 6.8 光栅后端在 `stroke()` 的整数轴向线快速路径中区分 `FlatCap`、`SquareCap` 和 `RoundCap`：线段先按变换后的设备坐标栅格化，方头按半线宽向两端延伸，圆头使用圆形端点覆盖；退化线段仅在非 FlatCap 时绘制端点（依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine_raster.cpp:1550-1607`）。虚线段还将 `SquareCap` 传入扫描转换器，端点规则见同文件 `3192-3239`。

XPainter 新增轴向整数线专用路径：FlatCap 保持终点排他，SquareCap 按半线宽外扩，RoundCap 按像素中心到有限线段的距离取圆头；水平和垂直线均覆盖，斜线继续保留轻量 Bresenham 近似。退化点在宽度 1 时保证三种端点样式均覆盖中心像素，宽线的 RoundCap 使用圆形覆盖，其余样式使用 Qt 兼容方块近似。回归新增三种宽度 1 端点断言，并在裁剪测试前恢复原有边框状态。

默认 `XGuiRegression_Test` 与 `XPAINTER_ON=0` 裁剪配置均构建、运行通过；构建输出包含仓库既有信号宏、跨类型删除和 const 丢弃警告，不能宣称零警告。LSan 受受控环境 `ptrace` 限制，未宣称泄漏检查通过；斜线精确描边、复杂 JoinStyle、抗锯齿和自定义 dash pattern 仍是后续边界。未提交、未推送。

### 10.80 2026-08-27 XPainter 布局方向未激活状态对齐

Qt 6.8 的 `QPainter::setLayoutDirection()` 只在绘制器状态对象存在时写入方向；
`begin()` 之前没有状态对象，因此此时 setter 被忽略。对应源码为
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7357-7362`；
`layoutDirection()` 在存在状态时返回保存值、无状态时返回 `Auto`，对应
`qpainter.cpp:7369-7373`。

XPainter 以 `m_deviceKind != None` 表示状态已激活，只有激活后才接受布局方向 setter；
未激活 getter 返回 `Auto`，非法枚举仍归一化为 `Auto`。回归覆盖“begin 前设置 RTL
被忽略、begin 后默认为 Auto、激活后设置 RTL 并恢复 Auto”的路径。默认配置、
`XPAINTER_ON=0` 裁剪配置和 CTest 均通过；
完整构建中仍存在仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告，不能
宣称零警告。LSan 受受控环境 `ptrace` 限制，未宣称泄漏检查通过；RTL 字体排版本身
仍是后续近似边界。该结论更正此前 10.45/10.52 中将布局方向 setter 归为“必须激活”
的概括；其他状态 setter 的激活约束保持不变。未提交、未推送。

### 10.81 2026-08-27 XPainter 文本换行标志对齐

Qt 6.8 `qt_format_text()` 仅在 `TextWordWrap` 或 `TextWrapAnywhere` 置位时把
矩形宽度传给文本布局；无换行标志时使用足够大的行宽，长文本保持同一行并由
`TextDontClip`/默认裁剪决定可见范围。`TextWrapAnywhere` 还会将布局的换行模式
设为 `QTextOption::WrapAnywhere`。依据为
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:7126-7133`
和 `7252-7254`。

XPainter 的 `drawTextRect()` 现在区分三种情况：普通文本只按显式换行分行，
`XPAINTER_TEXT_WORD_WRAP` 按空格优先、长单词再按字符拆分，
`XPAINTER_TEXT_WRAP_ANYWHERE` 按矩形宽度逐字符换行；强制两端对齐继续启用布局宽度。
回归新增窄矩形长文本断言，验证无换行标志不会把第二字形移到下一行，并验证
`WrapAnywhere` 会产生第二行。默认配置、`XPAINTER_ON=0` 裁剪配置、CTest 与完整
工程构建均通过；构建中的仓库既有警告和受控环境 LSan `ptrace` 限制仍按前节记录，
未宣称零警告或泄漏检查通过。复杂字体宽度、双向排版和富文本仍属后续边界。未提交、未推送。

### 10.82 2026-08-27 XPainter 渐变画刷颜色状态对齐

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qbrush.cpp:509-532`
的 `QBrush(const QGradient&)`：渐变画刷由无效 `QColor()` 构造，其
`color().rgb()` 按 Qt 语义返回 `0xff000000`；渐变停止点由独立的渐变数据保存。
当前 `XPainter_setBrushGradient()` 在复制渐变描述时同步设置 `m_brushColor` 与
`m_brush.m_color` 为 `0xff000000`，避免沿用先前纯色画刷的可观察状态；传入 NULL
仍仅清空渐变载荷并恢复实色样式，保留已有画刷颜色，作为嵌入式 C API 的可逆便捷
入口。`XPainter_setBrush_2()` 和 `XPainter_setBrushStyle()` 对 Qt 拒绝直接构造
渐变样式的行为保持不变（依据 `qbrush.cpp:342-355,653-661`）。

回归在 `test_painter_brush_contract` 增加渐变设置后 `XPainter_brushColor()` 为
opaque black 的断言。默认 `XGuiRegression_Test`、`XPAINTER_ON=0` 裁剪配置、CTest
及完整工程构建均需重新验证；构建中仓库既有信号宏、跨类型删除、const 丢弃和第三方
预处理警告继续单独记录，LSan 受受控环境 ptrace 限制，不宣称零警告或泄漏检查通过。
纹理画刷图像载荷、复杂渐变坐标模式和完整 QGradient 生命周期仍属未完成边界；未提交、未推送。

### 10.83 2026-08-27 XPainter 多子路径状态对齐

Qt 6.8 的 `QPainterPath::moveTo()` 会开始新的子路径并隐式结束上一子路径，
其实现更新当前子路径起点（依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath.cpp:651-697`）。
`closeSubpath()` 不把 Close 元素写入路径数组，而是在末点未回到起点时补一条
`LineTo`，并标记后续绘制需要新的 MoveTo（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath.cpp:638-648`
和 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath_p.h:240-264`）。

XPainter 的路径光栅回放在处理 `MoveTo` 时先完成上一子路径的填充/描边，再重置
顶点缓存后写入新起点，避免两个不相交子路径之间产生虚假连接；闭合状态依据
末点与子路径起点相等识别，不再依赖自定义 Close 元素。回归在
`test_painter_path_contract` 中新增两个不相连
矩形的填充断言，确认两个内部像素着色而中间间隔保持背景色。

默认 `XGuiRegression_Test` 与 `build-crop-painter-off` 裁剪配置均构建、运行通过；
随后恢复默认测试二进制，`git diff --check` 通过。构建输出仍含仓库既有信号宏、跨类型
删除和 const 丢弃警告，不能宣称零警告；受控环境 LSan 的 ptrace 限制仍未宣称泄漏检查
通过。路径填充规则仍固定 OddEven，曲线采用有限段数展平，属于后续精度边界；未提交、未推送。

### 10.84 2026-08-27 XPainterPath 空路径与重复元素边界对齐

Qt 6.8 的 `QPainterPath` 在空对象上调用 `lineTo()`、`quadTo()` 或
`cubicTo()` 时会先建立默认 `(0,0)` 的 MoveTo 元素；实现通过
`ensureData()` 创建起点，并在 `lineTo()` 中调用 `maybeMoveTo()`（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath.cpp:518-535`
和 `731-740`，以及 `qpainterpath_p.h:257-264`）。连续 `moveTo()` 在上一元素
仍为 MoveTo 时更新该元素而不重复追加（`qpainterpath.cpp:687-696`），同点
`lineTo()` 和控制点/终点完全重合的曲线则直接忽略。空 `closeSubpath()` 是无操作，
对应 `qpainterpath.cpp:638-648`。

XPainterPath 现按上述规则处理：空路径的线段/曲线自动补零点起始元素，连续
MoveTo 合并，同点线段和退化曲线不追加元素，空 closeSubpath 返回成功但不改变路径；
无效浮点坐标按 Qt 的“忽略调用”语义处理。`closeSubpath()` 后维护
`m_requireMoveTo` 状态，使下一条线段/曲线先追加当前点的隐式 MoveTo，避免把新子路径
错误连接到已闭合子路径。`addRect()` 在宽高均为零时无操作，
单轴为零及负尺寸继续保留退化/反向子路径构造。统一回归新增空路径、重复 MoveTo、
重复 LineTo、空 closeSubpath 以及闭合后隐式 MoveTo 断言。

默认 `XGuiRegression_Test` 已构建运行通过；`build-crop-painter-off` 裁剪配置及其
CTest 已复验通过，随后已恢复默认构建目录中的测试二进制。构建输出仍有仓库既有信号
宏、跨类型删除和 const 丢弃警告，不能宣称零警告；曲线仍以固定采样段数展平，路径填充
规则仍固定 OddEven，属于后续精度边界。未提交、未推送。

### 10.85 2026-08-27 XPainterPath 椭圆尺寸边界对齐

Qt 6.8 `QPainterPath::addEllipse()` 仅在矩形宽高同时为零（`isNull()`）时返回，
负尺寸和单轴零尺寸仍会通过 `qt_curves_for_arc()` 生成退化或反向椭圆路径，见
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath.cpp:1093-1119`。
XPainterPath 现仅对宽高同时为零做无操作，其他尺寸统一生成 64 段闭合折线路径，
并在回归中覆盖负宽度、零高度及空椭圆三种情况。未实现 Qt 的精确三次贝塞尔椭圆
控制点，当前仍以固定折线采样近似；未提交、未推送。

### 10.86 2026-08-27 XPainterPath 曲线元素序列对齐

Qt 6.8 的 `QPainterPath::ElementType` 只公开四类元素：`MoveToElement`、
`LineToElement`、`CurveToElement` 和 `CurveToDataElement`，声明位于
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainterpath.h:31-35`。
`cubicTo()` 追加一个 `CurveToElement` 加两个 `CurveToDataElement`，并在追加前
跳过完全退化曲线（`qpainterpath.cpp:760-804`）；`quadTo()` 将二次控制点按
`c1=(prev+2*c)/3`、`c2=(end+2*c)/3` 转换后调用 `cubicTo()`
（`qpainterpath.cpp:832-865`）。

XPainterPath 已删除自定义的 `QuadTo`、`CubicTo`、`CloseSubpath` 元素类型，
枚举和存储序列与 Qt 四类元素一致。`quadTo()`、`cubicTo()` 统一追加三元素曲线
序列，路径回放只接受合法的三元素组合并按固定段数展平；畸形的孤立
`CurveToData` 会拒绝绘制。回归覆盖二次、三次曲线的像素结果和既有路径回调契约。
固定段数展平仍是与 Qt 高精度曲线算法的近似边界；未提交、未推送。

### 10.87 2026-08-27 XPainter Qt 对齐标志名补齐

依据 Qt 6.8 `qnamespace.h:141-185` 的 `AlignmentFlag`/`TextFlag` 定义，补齐
XPainter 文本标志的公开同名项：`AlignLeading`、`AlignTrailing`、水平/垂直
掩码、`AlignBaseline`、`AlignCenter` 以及 `TextForceLeftToRight`/
`TextForceRightToLeft`。数值保持与 Qt 完全一致，其中 `AlignBaseline` 与
`TextSingleLine` 的 `0x0100` 冲突按 Qt 原注释保留。删除了 Qt 不存在的旧文本别名
`XPAINTER_TEXT_CENTER`、`XPAINTER_TEXT_FORCE_LTR/RTL`，库内调用统一使用 Qt 对应的
`XPAINTER_TEXT_ALIGN_CENTER`、`XPAINTER_TEXT_FORCE_LEFT_TO_RIGHT`/
`XPAINTER_TEXT_FORCE_RIGHT_TO_LEFT`。同时删除了已不参与路径回放的
二次曲线展平辅助函数，避免裁剪构建产生无用静态符号。默认工程已完成全量构建，
`./bin/XGuiRegression_Test` 与 `ctest --test-dir build --output-on-failure` 均通过；
`build-crop-painter-off`（`-DXPAINTER_ON=0`）清洁构建及统一回归也通过，验证后已恢复
默认配置测试二进制。构建输出仍包含仓库既有的信号宏、跨类型删除、const 丢弃及第三方
预处理警告，不能宣称零警告；LSan 受受控环境 ptrace 限制，不能宣称泄漏检查通过。
实现定位：`Src/XGui/Graphics/XPainter.h:393-420` 为规范标志及 Qt 数值，
`Src/XGui/Graphics/XPainter.c:4984-4987` 为规范强制布局方向分支，
`Src/XGui/Widget/XPushButton.c:657-660` 为按钮文本的规范居中调用；上述调用点
均已通过默认与 `XPAINTER_ON=0` 两套构建验证。
当前仍保留点阵字体、固定段数曲线展平等嵌入式近似边界；未提交、未推送。

### 10.88 2026-08-27 XPainter 布局方向头文件契约修正

复核 Qt 6.8 `QPainter::setLayoutDirection()` 实现后，修正
`Src/XGui/Graphics/XPainter.h:1508-1512` 的公开注释：未激活绘制器没有
`QPainterState`，setter 必须忽略，getter 返回 `Auto`；只有激活状态才保存方向，
并随 `save()/restore()` 管理，`end()` 后回到 `Auto`。实现仍位于
`Src/XGui/Graphics/XPainter.c:4678-4698`，行为与 10.80 所记录的
`qpainter.cpp:7357-7373` 一致。本轮重新完成默认工程和 `XPAINTER_ON=0` 裁剪工程
构建，统一 `XGuiRegression_Test` 与 CTest 均通过；构建日志仍含仓库既有警告，
LSan 仍受受控环境 `ptrace` 限制，不能宣称零警告或泄漏检查通过。未提交、未推送。

### 10.89 2026-08-27 XLabel setMovie 重设语义对齐

依据 Qt 6.8 `QLabel::setMovie()`（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:1231-1249`），
每次调用都会先清除旧文本、像素图和绘图记录；传入空影片时保持空内容，影片指针由
标签借用。修正 `Src/XGui/Widget/XLabel.c:2013-2021`，移除同一 `XMovie*` 的提前返回，
使重复设置同一影片也执行清理和刷新，随后仍保存借用指针。统一回归在
`xgui_regression_test.c:test_label_contract` 增加“首次设置与同指针重设均清空文本”的断言，
默认构建、CTest、`XGuiRegression_Test` 以及 `XPAINTER_ON=0` 裁剪构建后的统一回归均通过。XLabel 的
资源提供器、点阵字体、最小富文本解析和影片信号仍是嵌入式适配边界；未提交、未推送。

### 10.90 2026-08-27 XLabel 文本选择控制生命周期对齐

Qt 6.8 `QLabelPrivate::needTextControl()` 仅在富文本、可选交互标志或非
`NoFocus` 焦点策略下建立文本控制（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel_p.h:62-68`）；
`setSelection()` 在没有控制对象时不执行
（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:700-708`），但富文本即使没有选择标志也会建立
控制对象。据此修正
`Src/XGui/Widget/XLabel.c:2195-2238`：关闭鼠标/键盘文本选择时清除已有选择和
拖选状态（仅纯文本）；纯文本无选择交互标志时 `XLabel_setSelection()` 直接无操作，
恢复可选标志后才允许建立程序化选择；富文本仍允许程序化选择。统一回归新增关闭
交互清选、无权限 setSelection 无操作、恢复权限后可选及富文本无交互仍可选四项断言。
默认构建、CTest、统一回归和 `XPAINTER_ON=0` 裁剪回归均
通过；仓库既有编译警告与受控环境 LSan 限制仍按前节记录，未提交、未推送。

### 10.91 2026-08-27 XPainter deinit 资源生命周期对齐

Qt 6.8 `QPainter::~QPainter()` 在结束活动绘制设备后释放私有状态；已经结束
的绘制器不会再次暴露可复用的状态对象（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:1848-1898`）。
原实现的 `XPainter_deinit()` 无条件调用 `XPainter_end()`，而 `end()` 为支持后续
复用会重新初始化默认 `XFont` 与 `XRegion`，因此最终 deinit 会遗留一次默认状态
资源。修正 `Src/XGui/Graphics/XPainter.h:547-550`、
`Src/XGui/Graphics/XPainter.c:2211-2235`：增加 `m_initialized` 生命周期标记；
`init()` 设置标记，`deinit()` 仅对已初始化对象执行结束和最终状态释放，随后清零
对象并允许重复 deinit；begin/end 也拒绝已 deinit 对象，避免在无 vtable 状态上继续
绘制。统一回归新增“deinit 后不可复用”和“重复 deinit 无诊断”断言。
默认构建、CTest、统一回归及 `XPAINTER_ON=0` 裁剪回归均通过；构建日志中的既有
信号宏、跨类型删除和第三方预处理警告未因本改动增加，LSan 仍受受控环境 ptrace
限制，不能宣称零泄漏。未提交、未推送。

### 10.92 2026-08-27 XPainter 画刷原点接口对齐

Qt 6.8 在 `QPainter` 中公开 `brushOrigin()` 及 `setBrushOrigin()` 的
`QPointF`/`QPoint`/整数重载；未激活绘制器的查询返回空点，激活后原点属于
`QPainterState`，并由 `save()/restore()` 保存（`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:163-166,698-705`，
`qpainter.cpp:2065-2124`）。光栅引擎把原点平移到画刷矩阵中
（`qpaintengine_raster_p.h:252-258`），因此采样逻辑坐标时应减去画刷原点。

XPainter 新增 `XPAINTER_BRUSH_ORIGIN_ON` 独立裁剪开关，以及
`XPainter_setBrushOrigin(float,float)` 与 `XPainter_brushOrigin(XPoint*)`。
原点以浮点保存，查询按 `XPointF_toPoint()` 的 Qt 四舍五入规则输出整数，默认
为 `(0,0)`，未激活 setter 忽略；状态栈自动复制原点，线性/径向/锥形渐变在用户
及设备扫描路径都减去原点。统一回归覆盖浮点四舍五入和 save/restore 恢复。
默认构建、`XGuiRegression_Test`、CTest 以及 `XPAINTER_ON=0` 裁剪构建后的统一回归均已通过；
完整工程构建也通过。构建输出仍包含仓库既有信号宏、跨类型删除和第三方预处理警告，
不能宣称零警告；`XPAINTER_ON=0` 时总开关同步裁剪该状态。
实现定位：`Src/XGui/Graphics/XPainter.h:464-467` 保存原点状态，
`XPainter.h:1246-1263` 声明裁剪 API，`XPainter.c:2001-2014` 和
`2133-2146` 在用户/设备扫描路径应用坐标偏移，`XPainter.c:4200-4224`
实现 setter/getter，`XPainter_config.h:18-23,81-83,138-140` 定义总开关联动和
独立裁剪开关。图案纹理和精确抗锯齿渐变仍是嵌入式近似边界，未提交、未推送。

### 10.93 2026-08-27 XLabel 非 NoFocus 时的程序化选择对齐

Qt 6.8 `QLabelPrivate::needTextControl()` 在富文本、可选中文本交互标志或
标签焦点策略不是 `Qt::NoFocus` 时建立文本控制（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel_p.h:62-68`）。
`QLabel::setTextInteractionFlags()` 在 `LinksAccessibleByKeyboard` 时设置
`StrongFocus`，而 `setSelection()` 只要已有控制对象就执行（依据
`qlabel.cpp:660-682,700-708`）。原 XLabel 仅检查鼠标/键盘可选标志，导致
`LinksAccessibleByKeyboard` 场景错误拒绝程序化选区。

修正 `Src/XGui/Widget/XLabel.c:2233-2243`：纯文本在无可选标志时，只有焦点策略
仍为 `NoFocus` 才无操作；`StrongFocus`/`ClickFocus` 等非 `NoFocus` 状态现在允许
设置 UTF-16 选区，富文本行为保持不变。`xgui_regression_test.c:test_label_contract`
新增键盘链接交互设置 `StrongFocus`、随后允许 `setSelection(0,1)` 的断言。
默认构建、`XGuiRegression_Test` 与 `build-crop-painter-off` 裁剪回归均通过；构建中
仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告仍存在，不能宣称零警告。
LSan 受受控环境 ptrace 限制，不能宣称泄漏检查通过。鼠标/键盘完整 QTextControl
事件模型仍是嵌入式近似边界；未提交、未推送。

### 10.94 2026-08-27 XPainter QPixmap 绘制适配与高分辨率尺寸对齐

Qt 6.8 提供 `QPainter::drawPixmap()` 的位置重载和目标/源矩形重载（声明见
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:306-345`）。位置重载
在像素图非空时读取物理宽高，并把目标矩形设置为
`width / devicePixelRatio()`、`height / devicePixelRatio()`；实现位于
`qpainter.cpp:4770-4866`。矩形重载会对非正源宽高、负目标宽高、源区域越界及
设备像素比进行裁剪和比例换算（`qpainter.cpp:4866-4956`），然后转交绘制引擎。

XPainter 新增 `XPAINTER_PIXMAP_ON` 独立裁剪开关（总开关关闭时强制为 0），并在
`Src/XGui/XGuiConfig.h` 提供默认配置。`XPainter_drawPixmap()` 和点重载复用
`XPixmap_toImage()`，避免在 `Src/` 引入任何平台 API；设备像素比不是 1 时构造
逻辑目标矩形并调用现有 `XPainter_drawImageRect()`，否则走图像快速路径。矩形接口
`XPainter_drawPixmapRect()` 直接把源物理像素矩形交给 `drawImageRect`，因此继承其
负尺寸、非正源尺寸、越界裁剪、变换、裁剪、透明度和合成规则。实现定位为
`Src/XGui/Graphics/XPainter.h:722-762`、`XPainter.c:2745-2829`，转换依赖
`XPixmap.c:640-648` 的 `XPixmap_toImage()`，设备像素比查询依赖
`XPixmap.c:981-983`。

统一回归在 `xgui_regression_test.c:test_painter_raster_contract` 增加普通像素图、
`devicePixelRatio=2` 逻辑尺寸、位置点重载、目标/源矩形缩放及空指针错误处理断言
（`xgui_regression_test.c:681-690,1092-1125`）。默认配置和 `XPAINTER_ON=0` 裁剪配置
均完成 `XGuiRegression_Test` 构建运行，回归通过；随后默认配置全量构建、CTest、
统一回归和 `git diff --check` 均已完成并通过。与 Qt 的差异是 XPainter 坐标结构使用整数 `XRect/XPoint`，
因此浮点目标位置和亚像素尺寸按现有 `painterRound()` 取整；`drawPixmapFragments()`、
纹理画刷及 QBitmap 专用背景模拟仍未纳入嵌入式子集。构建中的仓库既有信号宏、跨类型
删除、const 丢弃和第三方预处理警告仍需单独治理，不能宣称零警告；受控环境 LSan
仍受 ptrace 限制，不能宣称泄漏检查通过。未提交、未推送。

### 10.95 2026-08-27 XPainter QPixmap 平铺绘制与裁剪联动

Qt 6.8 的 `QPainter::drawTiledPixmap(const QRectF&, const QPixmap&, const QPointF&)`
先对目标为空和像素图为空做无操作判断，再把偏移按像素图宽高取模（负值向后环绕），
最后交给绘制引擎的 tiled texture 路径；公共声明见
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:306-308,708-715`，
参数整理和设备像素比处理见 `qpainter.cpp:6389-6468`，光栅后端纹理平铺见
`qpaintengine_raster.cpp:2443-2508`。

XPainter 增加 `XPAINTER_TILED_PIXMAP_ON` 开关。该开关在总开关关闭、像素图关闭或
图像目标/源矩形关闭时自动置 0，避免裁剪配置暴露无法实现的接口。开启时新增
`XPainter_drawTiledPixmap()`：先通过 `XPixmap_toImage()` 取得共享图像数据，按
`devicePixelRatio()` 计算逻辑 tile 尺寸，对负偏移执行非负取模，再以边缘 tile 的
目标/源矩形调用现有 `XPainter_drawImageRect()`。因此当前变换、裁剪、透明度和合成
模式保持一致，且不在 `Src/` 引入平台 API。实现位于
`Src/XGui/Graphics/XPainter.c:2833-2920`，声明位于
`Src/XGui/Graphics/XPainter.h:763-793`，配置位于
`Src/XGui/Graphics/XPainter_config.h:39-45,91-99,168-180`。

统一回归在 `test_painter_raster_contract` 使用红、绿、蓝、黄四像素图案验证偏移为
`(1,1)` 时的首 tile、重复周期和右下边缘裁剪，并继续验证负偏移回绕和空指针错误
（`xgui_regression_test.c:1097-1158`）。默认构建、
`XPAINTER_ON=0`、`XPAINTER_IMAGE_RECT_ON=0` 三种配置均完成回归构建运行；默认配置
全量构建、CTest、统一回归和 `git diff --check` 通过。与 Qt 的差异是公开接口使用
整数 `XRect/XPoint`，浮点目标和偏移按整数取整；QBitmap 背景模拟、复杂纹理优化和
非整数设备像素比的亚像素采样仍属于嵌入式近似。仓库既有编译警告与受控环境 LSan
ptrace 限制仍未解决，不能宣称零警告或泄漏检查通过。未提交、未推送。

### 10.96 2026-08-27 XPainter 集中式裁剪默认值对齐

Qt 6.8 将画刷原点、背景模式、像素图、图像矩形和裁剪区域作为 `QPainter` 的
独立状态/绘制能力：画刷原点由 `QPainterState` 保存并可通过
`setBrushOrigin()`/`brushOrigin()` 访问（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:163-166,698-705`），
像素图位置/矩形/平铺绘制分别由 `qpainter.h:306-308,708-715` 声明，矩形裁剪与
区域裁剪由 `QPainter::setClipRect`、`setClipRegion` 状态实现。此前这些能力仅在
`XPainter_config.h` 中有默认值，直接包含公共 `XGuiConfig.h` 的裁剪用户无法稳定
获得相同开关集合。

修正 `Src/XGui/XGuiConfig.h:77-131`：为 `XPAINTER_BRUSH_ORIGIN_ON`、
`XPAINTER_BACKGROUND_ON`、`XPAINTER_PIXMAP_ON`、`XPAINTER_IMAGE_RECT_ON`、
`XPAINTER_TILED_PIXMAP_ON`、`XPAINTER_CLIP_REGION_ON` 以及已有绘图能力统一提供
可覆盖的 `#ifndef` 默认值。`Src/XGui/Graphics/XPainter_config.h:78-112` 的
`XPAINTER_ON=0` 总开关仍会将所有子开关强制置 0；`XPainter_config.h:114-229`
继续执行平铺像素图对像素图/图像矩形、区域裁剪对矩形裁剪的依赖裁剪，确保用户
显式 `-D` 配置与 Qt 对应 API 的能力边界一致。该改动只涉及头文件配置，不在
`Src/` 引入平台 API，也不改变默认开启行为。

验证结果：重新生成默认 Debug 构建目录后，`XGuiRegression_Test` 构建、统一回归、
CTest（1/1）和 `git diff --check` 均通过；`XPAINTER_ON=0` 裁剪构建及回归也通过。
构建输出仍有仓库既有的跨类型删除、信号宏、const 丢弃和第三方 zlib 预处理警告，
这些警告与本轮配置改动无关，不能宣称零警告；受控环境 LSan 仍受 ptrace 限制，不能
宣称零泄漏。未提交、未推送。

### 10.97 2026-08-27 XLabel 缩放文本链接命中与对齐掩码修正

Qt 6.8 的 `QLabel::setAlignment()` 先按 `AlignVertical_Mask|AlignHorizontal_Mask`
保留标志，其中水平掩码包含 `AlignAbsolute=0x0010`（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.h:144-164`，
实现见 `widgets/widgets/qlabel.cpp:403-418`）。修正
`Src/XGui/XAlignment.h:27-40` 的 `XAlignment_HorizontalMask` 为 `0x001f`，并在
`xgui_regression_test.c:9800-9810` 验证 `Right|Bottom|Absolute` 的读回与恢复。

Qt 标签的鼠标事件由文本控制器按当前文档布局处理（`qlabel.cpp:828-842`）；文本
布局的行高随字体变化，绘制路径也按当前字体和控件尺寸计算（`qlabel.cpp:980-1048`）。
原 `label_hitLinkAt()` 固定使用默认 16 像素行高与 8 像素字宽，字号放大后会把点击
位置映射到错误行/字形。修正 `Src/XGui/Widget/XLabel.c:1112-1131`，改用已有
`label_lineHeight()` 和 `label_advance()`，保持点阵字体缩放与绘制/选择几何一致。

回归在 `xgui_regression_test.c:9879-9918` 新增 32 像素富文本链接按下/释放测试，
验证实际缩放坐标发出 `linkActivated`。默认构建、`XGuiRegression_Test` 运行均通过；
本轮编译仍保留仓库既有信号宏、跨类型删除、const 丢弃和第三方预处理警告，不能
宣称零警告。LSan 受受控环境 ptrace 限制，不能宣称零泄漏。完整 QTextControl 键盘
导航、上下文菜单及外部链接打开仍是嵌入式近似边界；未提交、未推送。

### 10.98 2026-08-27 XLabel 焦点事件链与父类分发

Qt 6.8 的 `QLabel::focusInEvent()` 和 `focusOutEvent()` 在处理文本控制器后继续调用 `QFrame` 父类事件（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:866-897`）。修正
`Src/XGui/Widget/XLabel.c:1602-1616` 中的 `VXLabel_focusInEvent()` 和
`VXLabel_focusOutEvent()`，使焦点事件经 `XClass_Parent(XFrame, ...)` 传递至
`XFrame`，与 Qt 的 `QLabel -> QFrame` 事件链一致。同时移除
`XLabel.c` 离屏绘制路径中的环境变量调试输出及不再需要的 `errno` 依赖。

Qt 在失去焦点时还会通过 `QTextControl` 按焦点原因保留或清除选区；当前嵌入实现不含
`QTextControl`，因此文本选区保留、快捷键导航与上下文菜单仍属嵌入式近似。
`XFocusReason_Popup` 已在 10.342 补齐。默认与裁剪构建、`XGuiRegression_Test`、CTest 均通过；构建输出仍保留仓库既有警告，LSan 受受控环境 ptrace 限制，不宣称零警告或零泄漏。未提交、未推送。

### 10.99 2026-08-27 XLabel RTL 视觉对齐与像素图生命周期

Qt 6.8 的 `QGuiApplicationPrivate::visualAlignment()` 在没有水平对齐标志时补上
`AlignLeft`，并在未设置 `AlignAbsolute` 时按布局方向交换 `AlignLeft/AlignRight`
（依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qguiapplication_p.h:174-184`）。
`QLabel::sizeForWidth()` 和 `paintEvent()` 分别在
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:541-560,962-978`
使用该视觉对齐结果。`Src/XGui/Widget/XLabel.c:78-96` 新增
`label_visualAlignment()`，并在文本布局、文本命中、文本绘制及像素图绘制路径
（`XLabel.c:844-927,1062-1114,1219,1323-1336`）统一调用；`AlignAbsolute` 保持
物理左右位置，RTL 下逻辑 `AlignLeft` 转为右侧。回归用例
`xgui_regression_test.c:10039-10083` 在 64x32 画布上比较 LTR/RTL 的非背景像素
起点，验证视觉位置确实翻转。

同时修正 `label_drawPixmap()` 的临时图像生命周期：在调用 `XPixmap_toImage()` 前
显式 `XImage_init()`，缩放与普通绘制分支分别在完成后调用
`XImage_deinit_base()`（`XLabel.c:1302-1337`）。这样遵守 `XPixmap_toImage()` 对
输出对象先释放再初始化的约定，避免未初始化栈对象导致的未定义行为和重复绘制资源
泄漏。该修正复用现有 `XPainter_drawPixmap()` 的图像转换模式，不引入平台 API。

验证结果：默认 `build` 配置重新构建 `XGuiRegression_Test` 并运行通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）裁剪配置重新构建并运行通过。两种配置均
覆盖 RTL 标签绘制和图像路径。CTest 继续通过 1/1；构建输出仍有仓库既有的信号宏、
跨类型删除、const 丢弃及第三方预处理警告，不能宣称零警告。受控环境 LSan 受 ptrace
限制，不能宣称零泄漏。完整富文本双向排版、复杂脚本 shaping、QTextControl 选区及
样式引擎仍是嵌入式近似边界。未提交、未推送。

### 10.100 2026-08-27 XGui 当前进度检查点

本次自动任务暂停前的工作树已包含：图像 Handler/插件注册表与 BMP/PNG/JPEG/GIF/SVG
内置编解码路径，XIcon 主题引擎及主题/后备搜索路径，XPainter 平铺像素图、形状、
裁剪和布局方向能力，以及 XLabel 的 Qt 6.8 对齐、缩放链接命中、焦点父类分发和
RTL 视觉对齐。XLabel 像素图绘制临时 `XImage` 已按 `XPixmap_toImage()` 生命周期
要求初始化并在缩放/普通分支释放；相关依据和回归断言见 10.97-10.99。

已验证配置：默认 `build` 全量构建、`XGuiRegression_Test`、CTest（1/1）通过；
`build-crop-painter-off` 的 `XPAINTER_ON=0` 裁剪构建与回归通过；`git diff --check`
通过。构建日志中的跨类型删除、信号宏、const 丢弃及第三方预处理警告是仓库既有
问题，未宣称零警告；受控环境 LSan 受 ptrace 限制，未宣称零泄漏。尚未完成的优先项
包括完整 freedesktop `index.theme` 继承/Context 规则、QIcon 缓存与序列化、更多
XPainter 便携 opcode、畸形图像/mask/Picture fixture 以及 QImage color-space/文本
元数据扩展。当前改动未提交、未推送。

### 10.101 2026-08-27 XIcon index.theme 继承、Context 过滤与 dash 通用回退

Qt 6.8 的 `QIconTheme` 从 `index.theme` 读取 `Directories`，并对每个目录读取
`Size`、`Type`、`Threshold`、`MinSize`、`MaxSize`、`Scale` 和 `Context`；父主题由
`Icon Theme/Inherits` 追加后备主题后确保 `hicolor` 兜底（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:360-445`）。
`QIconLoader::findIconHelper()` 递归查找父主题时会对从未找到的图标执行 dash
回退，且当当前名称已经是通用回退名称时跳过 `Applications`/`MimeTypes` 上下文目录
（同文件 `446-640`）；`.png`/`.svg` 类型的目录匹配仍按目录元数据排序（同文件
`794-845`）。

本轮回调完成 freedesktop 主题的第二遍目录元数据解析，并在通用回退模式下按
`Context=Applications`/`Context=MimeTypes` 跳过对应目录。解析先收集
`Directories` 与 `Inherits`，然后为每个目录读取 `Size/Type/Threshold/MinSize/`
`MaxSize/Scale/Context`；若 `index.theme` 不存在则继续按旧的尺寸/上下文目录名
格式匹配。递归搜索会按 `Inherits -> fallbackThemeName -> hicolor` 的顺序进入父
主题，并用访问栈防止循环继承。对 `example-icon-tool` 这类名称，Qt 在整棵主题树
都找不到时按 `DashRule::FallBack` 截掉最后一个 `-` 后重新搜索；实现中保留该规则，
且对每次 dash 回退使用全新的访问栈，避免父主题访问记录污染新的搜索路径。

实现位于：
- `Src/XGui/Icon/XIconThemeInternal.c:177-203`：`ThemeDirContext` 与目录元数据
  结构；
- `XIconThemeInternal.c:369-543`：`index.theme` 两遍解析；本轮改为逐行复制到本地
  缓冲区，不破坏原文本，避免第二遍只能读到首个换行之前的行；
- `XIconThemeInternal.c:622-646`：按目录元数据试载并跳过应用程序/MIME 上下文；
- `XIconThemeInternal.c:731-855`：父主题递归、dash 回退和防循环访问栈；
- `XIconThemeInternal.c:907-985`：统一
  `XIconInternal_resolveThemePixmapSize()` 入口；
- `Src/XGui/Icon/XIcon.c:717-725`：`XIcon_hasThemeIcon()` 复用同一内部解析入口。

回归在 `xgui_regression_test.c:171-262` 新增 `test_icon_theme_index_inherits()`：
构造 `Child -> Base` 继承链，`Base` 的
`48x48/apps`（`Context=Applications`，蓝色）和 `48x48/status`（`Context=Status`，
绿色）各保存一张图标；直接按名查找应命中父主题的 applications 图标，而对
`example-icon-tool` 执行 dash 回退后必须以通用回退模式命中 status 图标，证明
Applications/MimeTypes 被跳过。同时验证 `XIcon_hasThemeIcon_2()` 走相同的继承和
回退路径。

验证结果：默认 `build` 配置重新构建 `XGuiRegression_Test` 并运行通过；裁剪配置
`build-crop-painter-off`（`XPAINTER_ON=0`）构建和回归也通过。`git diff --check`
通过。构建输出仍保留仓库既有的信号宏、跨类型删除、const 丢弃及第三方预处理
警告，不能宣称零警告；受控环境 LSan 受 ptrace 限制，不能宣称零泄漏。近似边界：
未实现 `icon-theme.cache`，未按设备像素比参数化 `Scale` 匹配，QIcon 缓存、序列化
与平台主题引擎插件工厂仍未完成。未提交、未推送。

### 10.102 2026-08-27 XIconThemeEngine paint 按目标矩形缩放

Qt 6.8 的 `QIconLoaderEngine::paint()` 在取得图标后直接调用
`painter->drawPixmap(rect, pixmap)`，随后按 `rect` 目标尺寸绘制，且关联的
`QIcon::paint()` 先做 `actualSize` 与 `visualAlignment` 再对齐到绘制矩形（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:783-785` 与同文件
`948-980`，`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1017-1035`
）。修正 `Src/XGui/Icon/XIconThemeEngine.c:24-58` 的
`VXIconThemeEngine_paint()`：当主题解析出的图标尺寸与目标矩形不一致时先通过
`XPixmap_scaled()` 缩放到目标宽高，再写入画布；`XPAINTER_IMAGE_RECT_ON` 开启时
走 `XPainter_drawImageRect()`，关闭时回退 `m_drawImage` 的按位置直绘路径，保证
裁剪配置仍可使用。

回归在 `xgui_regression_test.c:286-409` 的
`test_icon_theme_engine_paint_scales_to_rect()`：构造 `Base` 主题的
`48x48/apps` 绿色图标，创建 `XIconThemeEngine` 后以 `example-icon` 绘制到
`24x18` 非方形目标矩形，断言目标矩形左上/右下像素为图标色，矩形左/右外侧保持
背景色。测试随后恢复原主题搜索路径并清理临时目录。

验证结果：默认 `build` 配置重新构建并运行 `XGuiRegression_Test` 通过，CTest
（1/1）通过，`git diff --check` 通过；裁剪配置 `build-crop-painter-off`
（`XPAINTER_ON=0`）重新构建、回归和 CTest 也通过。构建输出仍保留仓库既有的信号宏、
跨类型删除、const 丢弃及第三方预处理警告，不能宣称零警告；受控环境 LSan 受
ptrace 限制，不能宣称零泄漏。近似边界：仍无 `icon-theme.cache`，未按设备像素比
参数化 `Scale` 匹配，`QIconLoaderEngine` 的禁用态合成、括号图标替换以及
`QPixmapIconEngine` 多尺寸选择缓存仍未完成。未提交、未推送。

### 10.103 2026-08-27 QImage 文本元数据与色彩空间保留

Qt 6.8 的 `copyMetadata(QImageData*, const QImageData*)` 在进行复制、区域拷贝和格式转换时保留物理分辨率、设备像素比、文本元数据、图像偏移与色彩空间，但不复制颜色表与 alpha lookup（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1168-1175`）；`convertToFormat_helper()` 在像素转换前调用该元数据复制，Indexed 格式的调色板转换也会保留元数据（同文件
`2191-2240`、`2269-2275`）。`QImage::text()` 在键为空时返回全部文本，键值对按插入顺序以
`"key: value\n\n"` 拼接并在末尾去掉多余的 `\n\n`，value 先经过 `simplified()`（同文件
`4184-4213`）。Qt 回归覆盖 `convertToFormatPreserveText()`：对 ARGB32_Premultiplied 源图写入
`foo/foo2` 后转到 RGB32 与 MonoLSB，断言聚合文本与键列表保留（
`/home/xinyue/Qt/6.8.3/Src/qtbase/tests/auto/gui/image/qimage/tst_qimage.cpp:1594-1614`）。

本轮完成 `Src/XGui/Graphics/XImage.c` 的 QImage 文本元数据扩展：
- `XImageData_copyMetadata()`（`XImage.c:89-118`）：集中复制 dpmX/dpmY、devicePixelRatio、offset、colorSpace 与全部文本键值，供克隆、区域拷贝和格式转换共享；`XImageData_clone`（`XImage.c:282-310`）与 `XImage_copyRect`（`XImage.c:1450-1485`）改走该路径，不再重复散落逐字段复制。
- `XImageData_buildAllText()`（`XImage.c:121-164`）：聚合全部文本元数据，格式与 Qt 的 `text("")` 一致，value 使用 `XString_simplified`，结尾去掉多余空行；早期按插入顺序的实现已被 10.108 的 QMap 升序键语义取代。
- `XImage_text()`（`XImage.c:2181-2196`）：`NULL` 键或空键返回聚合文本；显式键返回对应值副本。
- `XImage_text_const()`（`XImage.c:2196-2213`）：空键返回 `NULL`，避免所有权歧义；显式键仍按字面查找。
- `XImage_convertToFormat()`（`XImage.c:1521-1526`）：像素格式转换创建目标后调用 `XImageData_copyMetadata`，替换原仅复制 dpm 和 offset 的逐字段逻辑，从而保留文本、色彩空间与设备像素比。
- `XImage.h:732-738` 补充 `XImage_text` 空键聚合语义的中文说明。

回归在 `xgui_regression_test.c:5065-5117` 的 `test_image_pixel_contract()` 新增：`XImage_text(NULL)` 聚合、`convertToFormat` 后键列表/聚合文本/`text_2("foo")` 保留、colorSpace/devicePixelRatio/dpmX 保留；继续验证 premultiplied 转换与 RGBA 字节序等既有像素契约。

验证结果：默认 `build` 配置重新构建 `XGuiRegression_Test` 并运行通过，CTest（1/1）通过；裁剪配置 `build-crop-painter-off`（`XPAINTER_ON=0`）重新构建并运行回归通过；`git diff --check` 通过。本轮新增元数据常量/容器类型转换警告已清零；构建输出仍保留仓库既有的信号宏、跨类型删除、const 丢弃及第三方预处理警告，不能宣称零警告；受控环境 LSan 受 ptrace 限制，不能宣称零泄漏。近似边界：`XImage_setText` 的空键唯一项语义、重复键覆盖语义、`QImage` 的 DPR 独立存储及更多图像编码/元数据扩展仍需逐项对齐。未提交、未推送。

### 10.104 2026-08-27 XIcon 多尺寸与设备像素比选择对齐

Qt 6.8 的 `QPixmapIconEngine` 在多尺寸条目选择时先按 `pixmap.devicePixelRatio()`
打分：正分（比请求 DPR 更精细）优先于负分（比请求 DPR 更粗糙），同符号取绝对
误差最小；DPR 相同时再按逻辑面积比较，规则为“都不小于请求取最小，都小于请求取
最大，否则取达到请求者”（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:191-217`）。选择回退
顺序按 `bestMatch()` 的 Disabled/Selected 与 Normal/Active 分支逐层查找
（同文件 `247-330`）。`scaledPixmap()` 先按 `size * scale` 得到目标物理尺寸，把
选中的物理像素图用 `adjustSize()` 缩到请求内，再用
`QIconPrivate::pixmapDevicePixelRatio()` 修正输出 DPR（同文件 `149-165,332-383`，
`qicon_p.h:92-101`）。`actualSize()` 与 `availableSizes()` 都以逻辑尺寸（物理尺寸
除以 DPR）报告（同文件 `386-411`）。

本轮修改 `Src/XGui/Icon/XIcon.c`：
- `XIconPrivate_entryScale()` 与 `XIconPrivate_entryLogicalSize()`（`XIcon.c:324-346`）
  把逐条目物理宽高按 DPR 归一为逻辑尺寸。
- `XIconPrivate_bestSizeScaleMatch()`（`XIcon.c:388-434`）对标
  `bestSizeScaleMatch()`：先按 DPR 得分，再按逻辑面积完成候选比较；
  `XIconPrivate_tryMatchScale()` 与 `XIconPrivate_bestEntryScale()`
  （`XIcon.c:435-490`）保留原有 mode/state 完整回退链。
- `XIcon_pixmapRatio()`（`XIcon.c:521-575`）不再先按逻辑尺寸换算后再普通匹配，
  而是直接 DPR 感知选择条目、按目标物理尺寸缩放，并通过
  `XIconPrivate_pixmapDevicePixelRatio()` 修正输出 DPR。
- `XIcon_actualSize()` 与 `XIcon_availableSizes()`（`XIcon.c:577-605,710-741`）
  改为报告逻辑尺寸；同一逻辑尺寸的 `@2x` 条目会去重。

回归在 `xgui_regression_test.c:3578-3640` 的 `test_icon_device_pixel_ratio()`
扩展：同一图标同时持有 32x32/DPR1 与 64x64/DPR2 资源，验证 1x 请求选 32x32/DPR1、
2x 请求选 64x64/DPR2、20×20@2x 输出缩放为 40x40/DPR2，并验证
`availableSizes()` 把两个同逻辑尺寸条目去重为一个 32x32。

验证结果：默认 `build` 配置重新构建 `XGuiRegression_Test` 并运行通过，CTest（1/1）
通过；裁剪配置 `build-crop-painter-off`（`XPAINTER_ON=0`）重新构建、回归与 CTest
通过；`git diff --check` 通过。构建输出仍保留仓库既有的信号宏、跨类型删除、
const 丢弃及第三方预处理警告，不能宣称零警告；受控环境 LSan 受 ptrace 限制，
不能宣称零泄漏。近似边界：QIcon 的 Active 样式助手色化/禁用态生成、`addFile` 的
惰性尺寸条目（已完成，见 10.106）、`icon-theme.cache` 以及 `QIconEngine` 自定义
插件的序列化/缓存生命周期尚未完成；缩放像素图缓存接入见 10.105。未提交、未推送。

### 10.105 2026-08-27 XIcon 缩放像素图缓存接入

Qt 6.8 的 `QPixmapIconEngine::scaledPixmap()` 会把按源像素图缓存键、尺寸和设备像素比
选出的缩放结果写入 `QIconCache`/`QPixmapCache`，后续同参数的取图直接命中缓存，避免重复
缩放与合成；主题引擎的 `QIconLoaderEngine::scaledPixmap()` 同样在查找图标后按目标尺寸
复用缓存（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:332-383`、
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:191-217`、
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:903-985`）。

本轮新增 `Src/XGui/Icon/XIconScaledPixmapCache.h/.c`：
- 缓存键统一为 `前缀 + sourceKey + mode + 实际宽 + 实际高 + dprThousand`。
- 默认像素图引擎的前缀为 `qt_icon_scale/`，`sourceKey` 使用源像素图的
  `XPixmap_cacheKey()` 十六进制，等价于用源资源的唯一缓存键参与匹配。
- 主题引擎的前缀为 `qt_icon_theme/`，`sourceKey` 使用图标名称。
- 所有查找/插入均走 `XPixmapCache`，不引入独立平台 API，也不使用标准库内存接口。

已修改 `Src/XGui/Icon/XIcon.c`：
- `XIconPrivate_scaledPixmap()`（`XIcon.c:493-557`）统一默认引擎的 `XIcon_pixmap()`
  与 `XIcon_pixmapRatio()` 路径；未命中时按目标物理尺寸缩放、回写输出 DPR 后插入缓存。
- `XIcon_pixmap()` 与 `XIcon_pixmapRatio()`（`XIcon.c:558-598`）现在都走同一缓存。
- `XIcon_setThemeSearchPaths`、`XIcon_setFallbackSearchPaths`、
  `XIcon_setThemeName`、`XIcon_setFallbackThemeName` 系列（`XIcon.c:895,922,941,946,959,964`）
  在主题配置变化时调用 `XIconScaledPixmapCache_clear()`，避免旧解析结果被复用。

已修改 `Src/XGui/Icon/XIconThemeEngine.c`：`VXIconThemeEngine_scaledPixmap`
（`XIconThemeEngine.c:196-224`）第一帧冷加载后按图标名称/尺寸/DPR 插入缓存，后续直接命中。

回归在 `xgui_regression_test.c:3656-3696` 的 `test_icon_scaled_pixmap_cache()`：
同一 32x32 源图标请求 20x20@1x 两次，断言两次输出 `XPixmap_cacheKey` 相同；调用
`XPixmapCache_clear()` 后再取一次，断言缓存键变化且尺寸仍为 20x20。

验证结果：默认 `build` 配置重新配置并构建 `XGuiRegression_Test` 通过，回归通过；
裁剪配置 `build-crop-painter-off`（`-DCMAKE_C_FLAGS=-DXPAINTER_ON=0`）重新配置、
构建并运行回归也通过；`git diff --check` 通过。构建输出仍保留仓库既有的信号宏、
跨类型删除、const 丢弃及第三方预处理警告，不能宣称零警告；受控环境 LSan 受
ptrace 限制，不能宣称零泄漏。近似边界：默认引擎缓存键用源像素图缓存键，主题引擎
缓存键用图标名称；Active/Disabled 样式助手色化、禁用态生成、`icon-theme.cache`
与 `QIconEngine` 自定义插件序列化仍需后续对齐；`addFile` 惰性尺寸条目见 10.106。
未提交、未推送。

### 10.106 2026-08-27 XIcon addFile 惰性尺寸条目

Qt 6.8 的 `QPixmapIconEngine::addFile()` 传入有效尺寸时只保存文件名和请求尺寸，
不立即读入像素图；`tryMatch()`/`scaledPixmap()` 在真正取图时才惰性加载，`addFile()`
之后、取图之前的 `availableSizes()` 返回请求尺寸，取图后返回源像素图逻辑尺寸（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:447-495`、
`qicon.cpp:288-320`、`qicon.cpp:327-383`、`qicon.cpp:401-410`，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon_p.h:50-64` 的
`QPixmapIconEngineEntry`）。

本轮修改 `Src/XGui/Icon/XIcon.c`：
- `XIconEntry` 新增 `m_fileName`、`m_requestedSize`、`m_loaded`
  （`XIcon.c:27-36`），copy/move/deinit 已处理 `XString` 所有权与像素图生命周期。
- 新增 `XIconPrivate_canReadFile()` 与 `XIconPrivate_addFileEntry()`
  （`XIcon.c:187-221`）：`XIcon_addFile()` 校验文件可读后仅保存文件名和请求尺寸，
  不立即加载像素图。
- 新增 `XIconPrivate_loadFileEntry()`（`XIcon.c:413-432`）：取图时用 `XPixmap_load`
  惰性加载，加载失败保持未加载状态并返回 false。
- `XIconPrivate_entryLogicalSize()`（`XIcon.c:392-410`）对未加载的 addFile 条目
  返回请求尺寸，加载后返回真实像素图逻辑尺寸，对齐 Qt 的 `availableSizes()` 行为。
- `XIconPrivate_scaledPixmap()` 选中惰性条目后先加载再缩放/回写缓存
  （`XIcon.c:622-629`）；`XIcon_pixmap()` / `XIcon_pixmapRatio()`
  （`XIcon.c:667-708`）在可能触发加载前先 `XIcon_detach()` 做写时分离。
- `XIcon_detach()` 改为完整复制条目（含惰性 fileName），避免共享条目在加载时被改。
- `XIcon_addFile()`（`XIcon.c:802-830`）带正尺寸时走惰性条目，不加载像素图；
  `XIcon_availableSizes()`（`XIcon.c:832-859`）通过条目逻辑尺寸去重输出。

回归在 `xgui_regression_test.c:3747-3788` 的 `test_icon_add_file_size()`：
先写入 3x2 BMP fixture，用 8x6 请求尺寸 `addFile`；断言取图前
`availableSizes()` 返回请求尺寸 8x6；首次 `XIcon_pixmap()` 成功后断言像素图非空；
再次查询 `availableSizes()` 断言返回原生尺寸 3x2。

验证结果：默认 `build` 配置构建 `XGuiRegression_Test` 并运行通过，CTest（1/1）
通过；裁剪配置 `build-crop-painter-off`（`XPAINTER_ON=0`）重新构建、回归与 CTest
通过；沿用 10.105 的缓存接入后默认配置二进制已重建到 `bin/`。仓库既有信号宏、
跨类型删除、const 丢弃及第三方预处理警告仍在，不能宣称零警告；受控环境 LSan 受
ptrace 限制，不能宣称零泄漏。近似边界：Active/Disabled 样式助手色化、禁用态生成、
`icon-theme.cache` 与 `QIconEngine` 自定义插件序列化仍需后续对齐。未提交、未推送。

### 10.107 2026-08-27 XIcon Disabled/Selected 样式态生成与调色板感知缓存

Qt 6.8 的 `QPixmapIconEngine::scaledPixmap()` 在源条目模式与请求模式不一致时通过
`QApplicationPrivate::applyQIconStyleHelper()` 调用样式助手生成禁用/选中态；默认
`QCommonStyle::generatedIconPixmap()` 使用 `QPalette::Disabled, Window` 构建
“黑 -> 窗口背景 -> 白”颜色表，按源像素灰度和背景亮度映射后保留 alpha，选中态使用
`QPalette::Highlight` 的 alpha=0.3 以 `SourceAtop` 合成（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/styles/qcommonstyle.cpp:6159-6225`、
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:332-383`、
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qguiapplication_p.h:319`、
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qapplication.cpp:3993` 以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:903-985`）。

本轮新增 `Src/XGui/Icon/XIconStyleHelper.h/.c`：
- `XIconStyleHelper_paletteCacheKey()`（`XIconStyleHelper.c:34`）：基于
  `XGuiApplication_palette()` 的 4×21 颜色矩阵生成哈希，作为缩放缓存键的一部分，
  调色板变化后禁用/选中态结果不会被旧缓存复用。
- `styleDisabled()`（`XIconStyleHelper.c:59`）：按
  `QCommonStyle::generatedIconPixmap()` 的颜色表映射，保留原 alpha。
- `styleSelected()`（`XIconStyleHelper.c:130`）：高亮色 `SetAlphaF(0.3f)` 后按
  SourceAtop 合成到 `ARGB32_Premultiplied`，结果 alpha 保持目标图 alpha。
- `XIconStyleHelper_apply()`（`XIconStyleHelper.c:186`）：Active/Normal 原样返回，
  Disabled/Selected 走上述生成路径；应用调色板不可用时不改变像素内容。

已修改 `Src/XGui/Icon/XIconScaledPixmapCache.h/.c`：`find/insert` 增加
`paletteKey` 参数，缓存键追加调色板哈希与 mode；主题路径或调色板变化不会复用旧
样式态结果。已修改 `Src/XGui/Icon/XIcon.c`：`XIconPrivate_scaledPixmap()`
（`XIcon.c:640-680`）在非 Normal 模式生成样式态后写回缩放缓存；
`Src/XGui/Icon/XIconThemeEngine.c` 的 `paint()`/`pixmap()`/`scaledPixmap()`
（`XIconThemeEngine.c:46,120,230-248`）统一接入样式助手和调色板感知缓存。

回归在 `xgui_regression_test.c:3656-3697` 的 `test_icon_style_helper()`：纯色不透明
图标请求 `Disabled`/`Selected`，断言输出非空、alpha 保持 `0xff`、禁用态被窗口角色化、
选中态逐通道向高亮色偏移；`main()` 在 `xgui_regression_test.c:10815` 加入图标测试段。

验证结果：默认 `build` 配置重新配置并构建 `XGuiRegression_Test` 通过，回归与 CTest
（1/1）通过；裁剪配置 `build-crop-painter-off`（`XPAINTER_ON=0`）重新构建、回归与
CTest 通过；默认二进制已重建到 `bin/`。仓库既有信号宏、跨类型删除、const 丢弃及
第三方预处理警告仍在，不能宣称零警告；受控环境 LSan 受 ptrace 限制，不能宣称
零泄漏。近似边界：禁用/选中态颜色映射是 `generatedIconPixmap()` 的逐像素等价实现，
未覆盖 `QIconEngine` 自定义插件、平台主题扩展、DPI 权衡缓存淘汰与
`icon-theme.cache`。未提交、未推送。

### 10.108 2026-08-27 QImage 文本元数据 QMap 排序语义

Qt 6.8 的 `QImageData::text` 使用 `QMap<QString, QString>` 保存文本元数据，因此
`textKeys()` 始终按键升序返回，`text()/text("")` 的聚合文本也按升序键拼接；键允许为空
字符串，值允许为空字符串，已存在键的 `setText()` 原位覆盖并保持原排序位置（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:72` 与
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:4184-4247`；聚合格式见
同文件 `4201-4210`）。

本轮完成 `Src/XGui/Graphics/XImage.c` 的 QMap 容器语义对齐：
- `XImageData` 新增 `m_textAll`（`XImage.c:53`），缓存空键聚合文本并避免重复构建；
  `XImageData_clearTextAll()`（`XImage.c:86`）在写入文本元数据后清除缓存。
- `XImageData_buildAllText()`（`XImage.c:127-164`）按键升序聚合全部文本元数据，
  修正空 key/空 value 分支，补充空串使用 `emptyUtf8`，避免 `XString_append_utf8`
  被空串调用而返回 `false`。
- `XImage_text_2()`（`XImage.c:2181-2208`）：空键返回稳定的聚合文本 UTF-8 指针，
  内容为空时返回 `""`；非空键命中已有空值或未命中时均返回 `""`，与 Qt
  `QString()` 的空字符串语义一致。
- `XImage_setText()`（`XImage.c:2255-2292`）：按键升序定位插入点；同键原位覆盖值
  并保持排序位置；允许空 key/空 value；写入后清空聚合缓存并标记图片脏状态。

回归在 `xgui_regression_test.c:5338-5396` 的 `test_image_text_metadata_sorted_map()`：
写入 `zeta/alpha/空键/alpha覆盖/空值` 后断言键列表为
`空键、alpha、empty-value、zeta`，覆盖后值更新，已有空值与缺失键区分，
聚合文本为 `": empty\n\nalpha: 22\n\nempty-value: \n\nzeta: 1"`；
`main()` 在 `xgui_regression_test.c:10945` 接入。

验证结果：默认 `build` 配置构建 `XGuiRegression_Test` 通过，回归与 CTest（1/1）通过；
裁剪配置 `build-crop-painter-off`（`XPAINTER_ON=0`）重新构建、回归与 CTest 通过；
`git diff --check` 通过。仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告
仍在，不能宣称零警告；受控环境 LSan 受 ptrace 限制，不能宣称零泄漏。近似边界：
`XImage_text(NULL)` 仍直接构建并返回新 `XString*`，不走 `m_textAll` 缓存；
`XString_toUtf8()` 对空字符串返回 `NULL`，测试与调用方需按该语义处理。未提交、未推送。

### 10.109 2026-08-27 BMP 尺寸上限与 RLE 钳制语义

Qt 6.8 的 `QImageReader` 在 BMP 校验阶段拒绝 `biWidth * qAbs(biHeight) > 16384 * 16384`
的图片（依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:204`）；
RLE4/RLE8 对编码模式、绝对模式和 delta 都使用钳制语义，行程只写到当前行剩余像素，
delta 越界后把坐标钳到 `width - 1` / `height - 1`（依据
`qbmphandler.cpp:402-414`、`491-498`，以及编码/绝对模式的 `440-442`、`419-422`、
`520-523`、`508-511` 的余量保护）。

本轮修改 `Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c`：
- `XImageCodecInternal_decodeBmp()` 换算真实宽高后增加 `16384 * 16384` 像素总量上限，
  超过直接拒绝（`XImageCodecBmp.c:187-189`）。
- `bmpRleMoveToNextPixel()` 替换为 `bmpRleRowRemaining()`，编码/绝对模式按当前行
  剩余列数钳制写入量，行满或 `y >= height` 时不再跨行写索引
  （`XImageCodecBmp.c:62-67`、`XImageCodecBmp.c:90-96`、`XImageCodecBmp.c:119-135`）。
- delta 移动后坐标钳到末行末列，与 Qt 的 `x = w-1` / `y = h-1` 一致
  （`XImageCodecBmp.c:109-117`）。

回归在 `xgui_regression_test.c:4447-4500` 的 `test_codec_bmp_malformed()`，覆盖文件头
不足、DIB 截断、零宽高、非法 planes、非法位深、非法压缩类型、压缩与位深不匹配、
BITFIELDS 掩码截断、调色板截断、像素数据截断、`16385x1` 超大尺寸，以及
`XIMAGECODEC_BMP_INDEXED_ON && XIMAGECODEC_BMP_RLE_ON` 下的 RLE8 行程超宽钳制（断言
两个像素索引均为 `1`）；`main()` 在 `xgui_regression_test.c:11051` 接入。

验证结果：默认 `build` 配置构建 `XGuiRegression_Test` 通过，回归与 CTest（1/1）通过；
裁剪配置 `build-crop-painter-off`、`build-crop-min`、`build-gui-crop` 重新配置后构建、
回归与 CTest（1/1）均通过；`git diff --check` 通过。仓库既有信号宏、跨类型删除、
const 丢弃及第三方预处理警告仍在，不能宣称零警告；受控环境 LSan 受 ptrace 限制，
不能宣称零泄漏。近似边界：本轮未新增 1/4/16/32 位非 RLE 路径与 ICC/BITFIELDS 深度
比对，`XImageReader` 格式发现仍以扩展名/头签名联合判定，异常 BMP 在解码后图像级
尺寸与 DIB 元数据未逐项比对 Qt 输出。未提交、未推送。

### 10.110 2026-08-27 XPixmap setMask 自掩码与生命周期语义

Qt 6.8 的 `QPixmap::setMask()` 在掩码尺寸不匹配、目标为空以及掩码与目标共享同一
存储（自掩码）时直接返回，不触发 detach；合法掩码先 `detach()` 再写入 alpha，清空
空掩码会把先前透明像素变黑（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmap.cpp:536-561`，
自掩码判断在 `qpixmap.cpp:556`）。

本轮修改 `Src/XGui/Graphics/XPixmap.c`：
- `XPixmap_setMask()` 新增自掩码 no-op 保护：当 `mask->m_data == self->m_data`
  时直接返回，避免对自己写 mask 时 detach 后读取已改写数据
  （`XPixmap.c:313-314`）。
- 原有 detach 顺序、尺寸不匹配 no-op、空掩码恢复纯黑语义保持不变
  （`XPixmap.c:300-320`）。

回归在 `xgui_regression_test.c:703-765` 的 `test_pixmap_mask_lifecycle()`，覆盖：
- copy 后共享存储，`setMask` 后源 detach，目标副本不受影响；
- 修改源 mask 不影响已应用 mask 的目标；
- `setMask(NULL)` 清空 mask，原透明像素变纯黑且 alpha 恢复；
- 尺寸不匹配时 no-op；
- 自掩码 no-op，目标像素保持不变。
`main()` 在 `xgui_regression_test.c:11069` 接入。

验证结果：默认 `build`、`build-crop-painter-off`、`build-crop-min`、
`build-gui-crop` 均重新构建 `XGuiRegression_Test` 成功，回归与 CTest（1/1）全部通过；
`git diff --check` 通过。四套构建共享 `/home/xinyue/Code/XinYueC/bin/XGuiRegression_Test`
输出路径，若同时链接会竞争同一文件，应串行执行构建与 CTest。仓库既有信号宏、跨类型
删除、const 丢弃及第三方预处理警告仍在，不能宣称零警告；受控环境 LSan 受 ptrace
限制，不能宣称零泄漏。近似边界：本轮只对齐 `setMask` 生命周期与自掩码，未覆盖
`QPixmap::mask()` 的独立缓存、X11 原生掩码路径及绘制中调用保护。未提交、未推送。

### 10.111 2026-08-27 XPicture 高层 opcode：shape/polyline/polygon/points

Qt 6.8 的 `QPicturePaintEngine` 会把椭圆等形状经 `drawEllipse()` 写入独立
`PdcDrawEllipse` 命令，把折线/多边形经 `drawPolygon()` 按模式分别写入
`PdcDrawPolyline` 与 `PdcDrawPolygon` 命令；回放端在 `QPicture::play()`
内按命令重建 `QPainter` 调用（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:338-348`
与 `qpaintengine_pic.cpp:362-385`，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:495-560`）。

本轮在 `Src/XGui/Graphics/XPicture.h/.c` 增加可移植高层 opcode：
- `XPicture.h:41-44` 新增 `XPictureOpcode_DrawShape=6`、
  `XPictureOpcode_DrawPolyline=7`、`XPictureOpcode_DrawPolygon=8`、
  `XPictureOpcode_DrawPoints=9`。
- `XPicture.h:236-266` 新增
  `XPicture_recordDrawShape/recordDrawPolyline/recordDrawPolygon/recordDrawPoints`
  公共接口，参数顺序与 `XPainter` 回调语义对应。
- `XPicture.c:24-31` 定义 `XPICTURE_SHAPE_PAYLOAD_SIZE=40`、点/多边形头尺寸与
  `XPICTURE_MAX_POINTS=65535`；`XPicture_validateStreamData()` 在
  `XPicture.c:149-212` 对形状 op、filled/fillRule 范围、点数和载荷长度做校验，
  裁剪关闭对应 `XPAINTER_*_ON` 时拒绝未知高层 opcode。
- 录制实现在 `XPicture.c:619-738`，使用 `XMalloc_System/XFree_System`
  组装点载荷，形状/多边形/点集维护边界矩形。
- `XPicture_play_inner()` 在 `XPicture.c:875-990` 支持高层派发：软件后端
  fallback 调用 `XPainter_drawEllipse/drawArc/drawPie/drawChord/drawRoundedRect`、
  `XPainter_drawPolyline/drawPolygon/drawPoints`；有绘制回调的 painter 优先走回调。

已修改 `Src/XGui/Graphics/XPainter.c`：
- `XPainter.c:1589-1626` 新增 recorder 回调，把 `drawEllipse`、polyline、
  polygon、points 直接写给 `XPicture`，不再预先展平成 line/rect。
- `XPainter_begin_picture()` 在 `XPainter.c:2344-2352` 绑定这些回调；
  录制期间 `m_drawPath` 保持 NULL 并受现有 `XPAINTER_PATH_ON` 控制。

回归在 `xgui_regression_test.c:1125-1191` 的
`test_picture_painter_high_level_record_link()`：先用 `XPainter_begin_picture()`
录制椭圆、折线、多边形和点集，再通过 `XPicture_play()` 回放到软件 `XImage`
后端，逐像素断言椭圆填充为画刷蓝、轮廓和折线/点集为画笔红、多边形内部为画刷蓝、
边为画笔红且外侧保持背景黑；`main()` 在
`xgui_regression_test.c:11256-11258` 以
`XPAINTER_SHAPE_ON && XPAINTER_POLYGON_ON` 接入。

验证结果：默认 `build` 配置构建并运行 `XGuiRegression_Test` 通过，CTest（1/1）
通过；裁剪配置 `build-crop-painter-off`、`build-crop-min`、`build-gui-crop`
重新构建后回归与 CTest（1/1）均通过；`git diff --check` 通过。仓库既有信号宏、
跨类型删除、const 丢弃及第三方预处理警告仍在，不能宣称零警告；受控环境 LSan
受 ptrace 限制，不能宣称零泄漏。近似边界：Qt 的 `QPicturePaintEngine` 未对
`PdcDrawPoints` 实现回放（`qpicture.cpp:457-461` 仍是 TODO），本项目用 opcode 9
作为可移植扩展；polygon 软件后端回放按当前画刷状态决定填充，记录内的 `filled`
字段主要供自定义绘制回调使用，fallback 通过 `XPainter_drawPolygon()`
沿画刷语义；`PdcDrawPath` 独立 path opcode 见下一节。未提交、未推送。

### 10.112 2026-08-27 XPushButton autoDefault 父对话框联动

Qt 6.8 的 `QPushButton::autoDefault()` 在内部三态为 Auto 时会调用
`QPushButtonPrivate::dialogParent()`，沿父链向上遍历到第一个窗口前，命中
`QDialog` 则返回该对话框，从而把 Auto 三态解析为 true；普通窗口父或无对话框
父链则返回 false（依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qpushbutton.cpp:252-263`
与 `qpushbutton.cpp:316-322`）。

本轮修改 `Src/XGui/Widget/XPushButton.c`：
- `pushbutton_autoDefaultActive()` 在 `XPushButton.c:101-113` 实现 Qt 兼容的
  对话框父链检测：从按钮自身开始，在非窗口状态下取得父控件，命中
  `XWindowType_Dialog` 立即返回 true；碰到窗口边界或父链结束返回 false。
- `XPushButton_autoDefault()`（`XPushButton.c:488-495`）Auto 三态改走
  上述检测，不再无条件返回 false；On/Off 显式状态保持原支付宝并。
- 顶部文件级注释同步移除“QDialog 下 autoDefault 自动解析未实现”的描述，
  `XPushButton.h` 近似边界同样改为已实现父对话框链自动解析。

回归在 `xgui_regression_test.c:11234-11283` 的
`test_pushbutton_auto_default_dialog_parent()`，覆盖：
- Dialog 直接父下 Auto 三态返回 true；
- Dialog 父下 `setAutoDefault(false)` 返回 false、`setAutoDefault(true)`
  返回 true；
- 经中间非窗口子控件再挂 Dialog 父链仍返回 true；
- 普通 Window 父下 Auto 三态返回 false；
- 普通 Window 父下 `setAutoDefault(true)` 返回 true。
`main()` 在 `xgui_regression_test.c:11389` 接入。

验证结果：默认 `build`、`build-crop-painter-off`、`build-crop-min`、
`build-gui-crop` 均重新构建 `XGuiRegression_Test` 成功，回归与 CTest
（1/1）全部通过；`git diff --check` 通过。仓库既有信号宏、跨类型删除、
const 丢弃及第三方预处理警告仍在，不能宣称零警告；受控环境 LSan 受
ptrace 限制，不能宣称零泄漏。近似边界：当前按 `XWindowType_Dialog` 直接
判定，未建立独立 `XDialog` 类；按钮组互斥登记、`QDialogButtonBox` 多
autoDefault 互斥选默认以及真实对话框类反射仍不在本轮范围内。未提交、未推送。


### 10.113 2026-08-27 XPicture 独立 PdcDrawPath opcode

Qt 6.8 的 QPicture 回放中，`drawPath/fillPath/strokePath` 对应 `PdcDrawPath`，反序列化 `QPainterPath` 后由 `painter->drawPath(path)` 回放（依据 `qpicture.cpp:462-467`）；路径流本身在 `qpainterpath.cpp:2408-2421` 中按 `elementCount + type/x/y + cStart + fillRule` 写盘，`QPaintEngine::drawPath` 是路径绘制引擎入口（`qpaintengine.cpp:707-710`）。

本轮修改：
- `XPicture.h:46` 新增 `XPictureOpcode_DrawPath = 10`；`XPicture.h:270-278` 新增 `XPicture_recordDrawPath()`。
- `XPicture.c:168-207` 新增 `XPicture_validatePathPayload()`：校验 pathOp 范围、元素数量上限、固定头加元素载荷长度、元素类型、浮点有限性和 CurveTo 后连续两个 CurveToData 的语义；`XPicture_validateStreamData()` 在 `XPicture.c:236, 269-274` 接入，路径裁剪关闭时拒绝该 opcode。
- `XPicture.c:808-839` 新增 `XPicture_updatePathBounds()`，以全部元素坐标的整数包围盒维护绘图边界；`XPicture.c:840-875` 新增 `XPicture_recordDrawPath()`，little-endian 序列化 `pathOp + count + element(type, x1, y1)`，只使用 `m_x1/m_y1`。
- `XPicture.c:975-1041` 新增 `XPicture_rebuildPath()`，回放时重建 `XPainterPath`；单独或不足的 `CurveToData` 直接失败。
- `XPicture_play_inner()` 在 `XPicture.c:1184-1215` 新增 `DrawPath` 分支：优先调用 painter 高层回调，否则按 pathOp fallback 到 `XPainter_drawPath/fillPath/strokePath`，无设备返回 false。
- `XPainter.c:1629-1635` 新增 `painterRecord_drawPath()`；`XPainter_begin_picture()` 在 `XPainter.c:2363` 绑定，`begin_image()` 在 `XPainter.c:2334` 保持 NULL，`end()` 在 `XPainter.c:2414` 清空。

回归在 `xgui_regression_test.c:492-505` 新增 `picture_probe_path()`，`xgui_regression_test.c:1080-1160` 的通用 Picture 录制回放测试覆盖单一 DrawPath opcode，`xgui_regression_test.c:1239-1316` 新增 `test_picture_painter_path_record_link()` 录制 drawPath/fillPath/strokePath 并回放到软件 XImage 验证内部蓝、边缘红、外部黑；`main()` 在 `xgui_regression_test.c:11422` 接入。

验证结果：默认 `build` 重新构建并回归通过；`build-crop-painter-off`、`build-crop-min`、`build-gui-crop` 重新构建后均回归通过；`git diff --check` 通过。仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告仍在，不能宣称零警告；LSan 仍受环境限制，不能宣称零泄漏。近似边界：未序列化 `fillRule`、`cStart` 等 Qt 路径私有状态，边界矩形使用全部元素坐标的整数近似包围；裁剪关闭 `XPAINTER_PATH_ON` 时含 DrawPath 的流按无效流拒绝；`PdcDrawPoints` 仍为扩展 opcode 9（Qt 6.8 中该回放仍是 TODO）。本轮未提交、未推送。

### 10.114 2026-08-27 XPushButton 输入事件路径回归补全

在已有按钮契约与 autoDefault 联动测试基础上，新增
`test_pushbutton_input_event_contract()`（`xgui_regression_test.c:11340`），直接
构造鼠标/键盘事件经 `XWidget_event_base` 送入 XPushButton，覆盖 Qt 6.8
`QAbstractButton` 的按下、释放、click、键控激活及 default 按钮 Return 触发路径。

覆盖点：
- 控件内部左键按下：事件 accept、置 down、发 pressed；
- 控件内部释放：发 released/clicked，完成后恢复非按下；
- 控件外按下再外释放：事件 ignore、不触发点击；
- 按住移出提交标志失效、移回控件内继续按下并发 pressed；
- Space 按下/释放走完整点击；
- 无 default 按钮时 Return 忽略，有 default 按钮时 Return 触发 click。

`main()` 在 `xgui_regression_test.c:11675` 接入。验证结果：默认 `build`
重新构建 `XGuiRegression_Test` 成功，回归输出 `XGui regression tests passed`，
退出码 0；仓库既有信号宏、跨类型删除、const 丢弃及第三方预处理警告仍在，
不能宣称零警告；受控环境 LSan 仍不能宣称零泄漏。本轮未提交、未推送。

### 10.115 2026-08-27 XGuiWindowDemo 增加可见 XPushButton

`xgui_window_demo.c` 的可视化窗口增加 XPushButton 按钮面板：在标签面板
下方放一个常驻 `XPushButton` 和右侧联动 `XLabel`，按钮按下时标签显示
`Pressed`，松开时显示 `Released`。联动不再由窗口鼠标事件手动改文字，而是
把按钮的 `pressed/released` 信号用 `XObject_connect_1` 接到窗口槽函数，槽函数
更新常驻 `XLabel` 文本后重绘；按钮控件绘制仍走 `XPushButton_drawContents`
真实控件路径。

`xgui_window_demo.c:41` 按 `XWIDGET_ON && XPUSHBUTTON_ON` 引入
`XPushButton.h`。演示窗口持有常驻 `m_button` 与 `m_linkLabel` 成员；
`demo_button_pressedSlot()`/`demo_button_releasedSlot()` 作为按钮信号槽，
`DemoWin_create()` 中初始化控件并按信号连接，鼠标事件只负责命中判定并触发
`XPushButton_pressed_signal()`/`XPushButton_released_signal()`，状态与标签文本
全部由信号槽驱动。按钮被裁剪时仍绘制 `XPushButton disabled` 占位文本。
`xgui_regression_test.c` 新增 `test_pushbutton_label_signal_slot_link()`，用
`XObject_connect_1` 验证 pressed/released 信号能实时更新 `XLabel` 文本。

验证结果：默认 `build`、`build-crop-min`、`build-crop-painter-off` 均重新
构建 `XGuiWindowDemo_Test` 成功；默认 `build` 重建后运行
`./bin/XGuiWindowDemo_Test 1` 在 X11 下正常显示并退出（RC=0），
`./bin/XGuiRegression_Test` 通过（RC=0），包含信号槽联动用例，`git diff --check` 通过。仓库
既有警告仍在，不能宣称零警告；受控环境 LSan 仍不能宣称零泄漏。本轮未提交、
未推送。

### 10.116 2026-08-28 XGuiWindowDemo 按钮/标签位置修正与信号槽联动确认

人工检查发现上一轮直接调用 `XPushButton_drawContents()` 与
`XLabel_drawContents()` 时，控件会按自身本地矩形绘制到窗口左上角，未落到
预期面板位置。修正方式是在 `xgui_window_demo.c:178` 的按钮/标签面板绘制区
先取 `XWidget_geometry()`，对相同绘制器 `XPainter_save()` 后
`XPainter_translate()` 平移控件原点，再调用控件 `drawContents` 并
`XPainter_restore()`。这样按钮实际绘制在 `(246,190)-(374,226)`，
联动标签绘制在右侧 `(382,190)` 区域，鼠标命中判定仍使用
`XWidget_geometry()`，与可见位置一致。

联动方式已确认是信号槽连接：`DemoWin_create()` 中把
`XPushButton_pressed_signal()`/`XPushButton_released_signal()` 用
`XObject_connect_1()` 接到 `demo_button_pressedSlot()`/`demo_button_releasedSlot()`；
鼠标事件只负责命中按钮后手动设置 `setDown()` 并发射信号，按钮状态和标签文本由
槽函数通过 `XLabel_setText_2()` 更新。`xgui_regression_test.c` 的
`test_pushbutton_label_signal_slot_link()` 保持覆盖信号槽驱动的联动行为。

验证结果：默认 `build` 重新构建通过；`./bin/XGuiRegression_Test` 通过
（RC=0），包含信号槽联动用例；X11 下运行 `./bin/XGuiWindowDemo_Test 12`
正常显示并退出（RC=0），截图像素检查确认按钮位于面板内预期坐标，联动标签文本
绘制在右侧预期区域；`git diff --check` 通过。本轮未提交、未推送。

### 10.117 2026-08-28 XStackedLayout Qt 6.8 行为修正与统一演示接入

Qt 6.8 依据为本机源码
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qstackedlayout.cpp`：
`addWidget/insertWidget` 位于 168-208 行，`takeAt` 位于 231-252 行，
`setCurrentIndex` 位于 262-320 行，`currentWidget/widget/count` 位于
354-384 行，尺寸协商与几何分配位于 390-495 行，`setStackingMode` 位于
529-560 行。本轮按这些实现逐项核对页面索引、当前页切换、StackOne/StackAll
可见性、页面几何和尺寸提示，并保留项目架构明确的裁剪边界。

本轮修改：
- `Src/XGui/XLayout/XStackedLayout.c:367-400` 修正 StackAll 模式：切换到
  StackAll 时从当前页面取得几何，仅在宽高均为正值时同步到所有页面，随后
  统一显示；这对应 Qt 的 `currentWidget()->geometry()` 和
  `!geometry.isNull()` 语义。StackOne 中 Qt 6.8 当前源码的索引 0 分支
  行为仍按源码保留。
- `xgui_window_demo.c:44-45,69-73` 增加条件化堆叠布局头文件及两个真实
  `XLabel` 页面成员；`xgui_window_demo.c:215-243` 增加与 XLabel、
  XPushButton 同窗显示的 XStackedLayout 面板，布局先分配页面矩形，再绘制
  当前页面；裁剪时显示占位文字。`xgui_window_demo.c:493-508` 初始化
  页面文本、字号、对齐并加入 StackOne 布局，`xgui_window_demo.c:605-607`
  在窗口子控件销毁前释放布局条目包装。
- `Src/XGui/XGuiConfig.h:149-154` 与 `Src/XGui/XLayout/XLayout_config.h:65-70` 已提供
  `XLAYOUT_ON`/`XLAYOUT_STACKED_ON` 总开关和子开关；CMake 使用
  `file(GLOB_RECURSE SRC_FILE "Src/*.c")`，因此新增
  `XStackedLayout.c` 会自动进入库构建，重新配置后已确认生成规则包含该
  编译单元。

架构边界与近似项：
- XLayout 仍是 XLayoutItem/XClass 布局对象，不改为 QObject/XObject；
  `currentChanged`/`widgetRemoved` 目前是稳定标识函数，不能使用
  `XObject_connect_*` 连接。
- 工程的 `XLayout_addItem` 约定是传入条目借用，XStackedLayout 仅接受控件
  条目；这与 Qt `addItem` 对传入条目的 unique ownership 不同，属于项目内存
  所有权约定，不能在本轮改成 Qt 的删除语义。
- XWidget 没有 Qt `raise/lower` 和完整焦点链 API，故未伪造 z-order/焦点
  转移；`setCurrentIndex` 保留现有 clearFocus/visible 行为。最小尺寸使用
  本项目可用的 minimum/sizeHint/max 组合，尚未复刻 Qt 私有
  `qSmartMinSize` 的全部 style 约束。
- 演示布局是独立的可视化面板，未把布局挂到 DemoWin 根布局，因此不会
  覆盖窗口其他控件；页面控件作为 DemoWin 子控件，由窗口统一回收。

验证结果：重新执行 `cmake -S . -B build` 和
`cmake --build build --target XGuiWindowDemo_Test -j2` 均成功，
`XStackedLayout.c.o` 与演示目标均完成编译链接；随后新增的
`test_stacked_layout_contract()` 已覆盖 StackOne/StackAll、索引切换、插入/移除
及非空几何同步，并在默认配置回归中通过。构建仍报告仓库既有的 const 丢弃、
跨类型删除及第三方预处理警告，不能宣称零警告。以后新增 XGui 控件均须接入
`xgui_window_demo.c` 与现有控件同屏显示，自动断言统一放在
`xgui_regression_test.c`。本轮未提交、未推送。

### 10.118 2026-08-28 XWidget 底层 QWidget 语义对齐与注释审计

本轮继续按本机 Qt 6.8 源码核对 `XWidget` 的底层父类行为。主要依据为：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.h:230-526`
（公开属性、几何、焦点、坐标映射与 heightForWidth 声明），以及
`qwidget.cpp:3380-3434`（启用状态及父子传播）、`qwidget.cpp:3472-3500`
（setDisabled/frameGeometry）、`qwidget.cpp:3967-4048`（最小/最大尺寸）、
`qwidget.cpp:4288-4321`（mapToParent/mapFromParent）、
`qwidget.cpp:4904-4927`（布局方向传播与 unset）、
`qwidget.cpp:6535-6546`（setFocus 使用 OtherFocusReason）、
`qwidget.cpp:7225-7243`（setGeometry 的 WA_Moved/WA_Resized 状态）、
`qwidget.cpp:10425-10442`（heightForWidth/hasHeightForWidth）、
`qwidget.cpp:10566-10628`（setWindowFlag/overrideWindowFlags）以及
`qwidget.cpp:11257-11317`（update 的可见性与更新开关边界）。

已完成的底层修正：

- `Src/XGui/Widget/XWidget.c` 增加 `isTopLevel`、单窗口标志设置、固定宽/高、
  父坐标映射、`setHidden`、`overrideWindowState`、`setDisabled`、
  `underMouse`、方向便捷查询和 heightForWidth 查询；默认上下文菜单策略改为
  Qt 的 `DefaultContextMenu`。
- `setGeometry` 先按最小/最大尺寸约束钳位，再保留 Qt 的
  `WA_Moved/WA_Resized` 历史状态；`move/resize` 不会误清除另一方向已有状态。
- 启用状态沿控件树传播：父禁用会给所有子控件置 `Disabled`，重新启用时保留
  子控件显式 `ForceDisabled`；禁用控件的鼠标、键盘、滚轮和上下文菜单输入不再进入
  事件槽；进入/离开事件同步维护 `UnderMouse`，顶层离开会清除整棵子树。
- `setLayoutDirection/unsetLayoutDirection` 按 Qt 规则向未显式指定方向的非窗口
  子控件传播，并派发 `LayoutDirectionChange`；`setFocus()` 使用
  `OtherFocusReason`。

头文件注释同步：`XWidget.h` 顶部总说明明确当前是“已覆盖 QWidget API 的 Qt
语义适配”，并列出未覆盖的原生绘制引擎、完整焦点链、z-order 和平台专用窗口
扩展；新增公共函数均补充中文用途、参数和返回值说明。`XLabel.h`、
`XPushButton.h` 与 `XStackedLayout.h` 已统一采用同一中文 Doxygen 风格，后续新增
控件必须沿用该格式，不得只写无参数的一行简介。

架构及裁剪边界：

- `XLayout` 继续继承 `XLayoutItem/XClass`，不伪造 `QObject` 多继承；当前
  `currentChanged/widgetRemoved` 仅为稳定信号标识函数。`XLAYOUT_ON`、
  `XLAYOUT_STACKED_ON`、`XWIDGET_ON` 等开关保留，关闭子能力时公共入口退化为
  空实现或默认值，以便高性能嵌入式按需裁剪；桌面默认配置保留完整已实现 API。
- `XWidget_setWindowFlags` 的窗口重建、平台原生绘制、完整焦点链和 z-order
  仍依赖 `XWindow`/Drive 后端，当前没有跨平台等价实现的入口继续记录为未完成项。
- 旧兼容入口暂不删除：`XWidget_setParentPlain`、`XWidget_childAt_2` 等仍被
  XLayout 与现有回归测试内部使用；待迁移完成后再删除，避免破坏当前 ABI。

验证结果：默认配置重新构建 `XGuiRegression_Test` 并运行通过（含
StackOne/StackAll 页面索引、可见性、非空几何同步和移除回退）；
`ctest --test-dir build --output-on-failure` 通过（1/1），
`XGuiWindowDemo_Test 5` 自动退出通过；`XLAYOUT_STACKED_ON=0` 裁剪配置重新
配置、构建并运行 `XGuiRegression_Test` 通过。已观察到的
`XError` 日志来自既有无效 transient-parent、WindowActive 和测试清理路径，
不影响进程返回值。构建仍有仓库既有的信号函数类型、const 丢弃和跨类型删除
警告，不能宣称零警告；本轮未进行受控 LSan，不能宣称零泄漏。默认演示及
`XLAYOUT_STACKED_ON=0` 裁剪构建已在统一验证阶段重新运行并通过。所有新增/修改的
XGui 控件仍须接入 `xgui_window_demo.c` 同窗显示，并在
`xgui_regression_test.c` 增加自动断言统一验证。本轮未提交、未推送。

### 10.119 2026-08-28 XFrame 几何与样式边界修正

本轮复核本机 Qt 6.8 源码
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qframe.cpp:263-329`
（`setFrameShape`、`setFrameShadow`、`setFrameStyle`）、`342-378`
（`lineWidth`、`midLineWidth` 的 `short` 存储）、`446-462`
（`frameRect`、`setFrameRect`）以及 `qframe_p.h:35-41`（私有字段类型）。

已完成修正：

- `XFrame_setFrameStyle` 保留调用方样式整数的未识别高位，读取形状和阴影时
  仍按 Qt 的 `Shape_Mask`/`Shadow_Mask` 解释。
- `XFrame_setLineWidth` 与 `XFrame_setMidLineWidth` 改为直接按 Qt `short`
  转换保存，不再人为钳位到 `[0,255]`。
- `XFrame_setFrameRect` 的右、下内容边距改为以控件 `rect()` 的右下边界计算，
  修复非全控件外形矩形往返时的几何偏移。
- `xgui_regression_test.c` 新增 `test_frame_contract()`，覆盖 Box/Raised 边框宽度、
  未知样式高位、非全控件 `frameRect` 往返、空矩形回退和线宽的 `short` 转换语义。

验证结果：默认 `build` 重新构建 `XGuiRegression_Test` 并运行通过，输出
`XGui regression tests passed`；`git diff --check` 通过。构建仍保留仓库既有的
信号函数类型、跨类型删除、const 丢弃和第三方预处理警告，不能宣称零警告；本轮
未进行受控 LSan，不能宣称零泄漏。XFrame 的真实样式引擎、平台窗口装饰和
`QStyle::SE_ShapedFrameContents` 的逐主题差异仍由 XPainter/Drive 边界承担。
本轮未提交、未推送。

### 10.120 2026-08-28 QImage/QIcon/XWidget 资源生命周期收尾

本轮继续按本机 Qt 6.8 源码复核资源所有权和元数据生命周期。依据为：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:156-166`
（`QImageData::~QImageData` 的数据/绘制引擎清理）、`1161-1175`
（`copyPhysicalMetadata`/`copyMetadata` 复制物理参数、文本和色彩空间）、
`4196-4209`（`QImage::text` 空键聚合及末尾换行裁剪）；
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1376-1421`
（`QIcon::fromTheme` 缓存、主题引擎及 fallback 语义）；
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:146-211`
（主题名、主题搜索路径和回退搜索路径），`612-686`
（主题、回退主题、文件路径和平台引擎的查找顺序）。

已完成修正：

- `Src/XGui/Graphics/XImage.c` 增加文本元数据的明确反初始化路径：先对
  隐式共享列表执行分离，再逐项反初始化嵌入的 `XString`，最后释放列表和空键
  聚合缓存；像素缓冲分配失败路径也使用同一清理逻辑。图像克隆、区域复制和
  格式转换统一走元数据复制函数，保留 Qt 6.8 的分辨率、设备像素比、偏移、文本
  和色彩空间语义。
- `Src/XGui/Icon/XIconThemeInternal.c` 的逗号分割临时字符串现在始终删除，主题
  上下文、父主题、主题路径和回退路径列表使用带 `XVector_detach` 的逐项析构辅助
  函数，避免共享列表被析构时破坏全局路径并消除字符串条目泄漏。
- `Src/XGui/Icon/XIcon.c` 的 `fromTheme` 实现直接把主题引擎接入已经初始化的
  `XIconPrivate`，避免重复初始化造成的私有数据泄漏；仍保留 Qt 的空图标 fallback
  复制语义和引擎名称同步。
- `Src/XGui/Widget/XWidget.c` 的拷贝语义避免在已有可访问对象上重复创建，修复
  `XAccessible` 的 144 字节直接泄漏；初始化路径保持一次创建。此前的 SVG arena
  对齐和 JPEG 无符号位移修正继续保留，避免 UBSan 未对齐访问和负移位未定义行为。

验证结果：

- 默认配置 `cmake --build build --target XGuiRegression_Test XGuiWindowDemo_Test`
  成功；`./bin/XGuiRegression_Test`、`./bin/XGuiWindowDemo_Test 3` 和
  `ctest --test-dir build --output-on-failure` 均通过（1/1）。
- `build-crop-stacked-off`（`-DXLAYOUT_STACKED_ON=0`）重新编译回归和演示目标并
  通过，证明堆叠布局裁剪开关不影响基础 XWidget/XLabel/XPushButton 构建。
- ASan/UBSan 功能运行通过（`detect_leaks=0`），未再报告 SVG 对齐、JPEG 位移、
  图像文本、主题列表或可访问性重复创建问题。
- 受控 LSan 当前报告 `101618 byte(s) leaked in 456 allocation(s)`：其中
  Mesa GLX 直接 604 字节、间接 99904 字节和 calloc 448 字节，fontconfig 直接
  512 字节/间接 96+22 字节，另有 POSIX `XPlatformNativeWindow_grabWindow`
  （`Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:1382`，回归调用点
  `xgui_regression_test.c:6388`）产生的 32 字节 `XPixmap`。这些均不来自本轮
  XGui 图像/主题/控件修改；因此不能宣称全量零泄漏。构建仍保留仓库既有的
  const 丢弃、跨类型删除、信号函数类型和第三方预处理警告，不能宣称零警告。

架构与未完成项：`XLayout` 继续保持 `XLayoutItem/XClass` 层次，不改为
`QObject/XObject` 多继承；`XLAYOUT_ON`、`XLAYOUT_STACKED_ON`、`XWIDGET_ON`
等开关继续支持嵌入式裁剪。`QIcon` 插件动态发现、平台原生图标引擎、完整 QWidget
焦点链/z-order 和 POSIX 截图后端清理仍是边界项。以后新增 XGui 控件必须接入
`xgui_window_demo.c` 同屏显示，并在 `xgui_regression_test.c` 增加自动断言。
本轮未提交、未推送。

### 10.121 2026-08-28 XWidget 初始化与 setParent 语义收尾

本轮继续按本机 Qt 6.8 `qwidget.cpp` 逐项核对底层控件生命周期：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.cpp:985-992`
（构造阶段属性初始化）以及 `10668-10672`（`QWidget::setParent(QWidget*)`
的窗口类型掩码和同父短路）。已完成：

- `XWidget_init` 初始化时显式设置 `XWidgetAttribute_WState_Hidden`，新建控件在
  首次 `show()` 前保持隐藏，父控件显示不会误显示尚未显式显示的子控件。
- `XWidget_setParentPlain` 对同一父控件直接返回；重新挂接时清除低 8 位
  `WindowType_Mask`，保持 QWidget 无 flags 重载的子控件语义。
- `XWidget.h` 同步补充上述接口的中文用途、参数和返回值注释。

审计确认但本轮不扩张架构的边界：Qt 顶层/子控件默认几何（640x480/100x30）、
`WA_QuitOnClose`/`WA_ContentsMarginsRespectsSafeArea`、Create/Polish 及 pending
move/resize 事件、完整 `setAttribute` 镜像和 `setParent(flags)` 的隐藏/位置重置/
事件链仍需要窗口后端状态机支持，当前实现保持现有嵌入式裁剪边界。

验证结果：默认配置 `XGuiRegression_Test`、`XGuiWindowDemo_Test 3` 和 CTest
全部通过；`build-crop-stacked-off`（`-DXLAYOUT_STACKED_ON=0`）目标构建及回归
通过；ASan/UBSan 功能运行通过。受控 LSan 仍报告 `101618 byte(s) leaked in
456 allocation(s)`，来源为 Mesa/fontconfig 运行时和 POSIX 截图路径的既有资源，
未发现本轮 XImage/QIcon/XWidget 新增泄漏。构建保留仓库既有事件析构指针类型、
信号函数类型和第三方预处理警告，不能宣称零警告、零泄漏。本轮未提交、未推送。

### 10.122 2026-08-28 XPainter Picture 高层指令录制回调补全

本轮继续按 Qt 6.8 的 QPainter/QPicture 指令分派核对绘图器后端。源码依据为：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:230-303`
（`drawPath`、`drawPoints`、`drawPolyline`、`drawPolygon`、椭圆/弧/饼/弦及圆角矩形
接口）；`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3142-3169`
（`drawPath` 在绘制器未激活时返回、优先交给扩展/引擎）；
`qpainter.cpp:3418-3455`（`drawPoints` 空数组/非正数量直接返回并交给引擎）；
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:400-467`
（Picture 逐记录读取并将路径回放到 QPainter），以及
`qpicture.cpp:539-560`（折线/多边形记录按点数组和填充规则回放）。

已完成修正：

- `Src/XGui/Graphics/XPainter.c` 的 Picture 后端现在为形状、多边形、折线、点集和
  路径安装真正的录制回调，分别调用 `XPicture_recordDrawShape`、
  `XPicture_recordDrawPolyline`、`XPicture_recordDrawPolygon`、
  `XPicture_recordDrawPoints` 和 `XPicture_recordDrawPath`；此前这些高层回调为
  空指针，调用会退回软件光栅路径，Picture 数据中缺少对应命令。
- 参数校验与公开 API 保持一致：空点数组、非正数量和空路径在 XPainter 层按 Qt
  风格视为成功无操作；有效点集才进入 Picture 记录，填充状态和填充规则随多边形
  命令保存，路径操作保存 Draw/Fill/Stroke 枚举及元素数据。
- 回放仍由 `XPicture_play` 通过 XPainter 公开入口执行，因此软件图像后端会走现有
  光栅实现，其他后端可继续替换对应回调；点集记录是当前便携格式的增强项，Qt 6.8
  `QPicture::exec` 对 `PdcDrawPoints` 尚留有未实现注释（`qpicture.cpp:457-461`）。

验证结果：默认配置重新构建 `XGuiRegression_Test`、`XGuiWindowDemo_Test` 并运行
`./bin/XGuiRegression_Test`、`./bin/XGuiWindowDemo_Test 3` 通过；
`build-crop-stacked-off`（`-DXLAYOUT_STACKED_ON=0`）目标重编译、回归和演示运行通过。
已有高层 Picture 回归用例 `xgui_regression_test.c:1225-1294`、
`1296-1348` 覆盖形状/多边形及路径录制与回放，未新增失败。构建仍报告仓库既有的
事件析构指针类型、信号函数类型、const 丢弃和第三方预处理警告，不能宣称零警告；
最近一次受控 LSan 仍为 `101618 byte(s) leaked in 456 allocation(s)`，来源是
Mesa/fontconfig 运行时和 POSIX 截图后端既有资源，不能宣称零泄漏。所有修改均保留在
当前工作树，未提交、未推送。

### 10.123 2026-08-28 QImageReader 内容决策与格式探测语义

本轮继续对照 Qt 6.8 `QImageReader` 的状态和处理器选择逻辑：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:143-254`
（`createReadHandlerHelper` 按格式、后缀、内容依次选择插件处理器）、
`258-337`（内置处理器及内容回退）、`647-689`
（`setAutoDetectImageFormat` 只更新自动探测状态）以及 `703-720`
（`setDecideFormatFromContent` 只更新内容决策标志；内容决策忽略格式和扩展名）。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c` 在 `decideFormatFromContent` 为真时，将空格式
  传给插件注册器和内置编解码入口，避免显式格式字符串阻断内容探测。
- `setDecideFormatFromContent` 不再隐式改写 `autoDetectImageFormat`，两个状态与 Qt
  getter 语义保持独立；`canRead` 在内容决策开启时跳过“不支持显式格式”的提前失败。
- `XImageReader_imageFormatDevice`/文件格式查询优先询问注册器处理器，再回退到内置
  签名探测，确保注册插件能够参与格式发现。
- `Src/XIO/XIODevice/XIODevice.c:381-397` 的 `XIODevice_peek_2` 按请求长度扩展内部
  缓冲并保留实际读取长度，符合 Qt `QIODevice::peek` 可继续从后备设备补数据的语义；
  这避免插件先做短探测后导致 SVG 等后续读取永久截断。
- `xgui_regression_test.c` 新增 `test_image_reader_decide_format_state`，覆盖默认值、
  状态独立性及关闭内容决策后的回退状态，并在所有配置中执行。

验证与边界：默认配置回归、窗口演示和 CTest 已重新执行；`build-crop-stacked-off`
（`-DXLAYOUT_STACKED_ON=0`）的回归也覆盖该状态用例。ASan/UBSan 功能运行保持通过。
构建继续保留仓库既有的事件析构指针、信号函数类型、const 丢弃和第三方预处理警告，
不能宣称零警告；受控 LSan 仍为 `101618 byte(s) leaked in 456 allocation(s)`，来源
是 Mesa/fontconfig 运行时和 POSIX 截图路径既有资源，不能宣称零泄漏。

当前图像插件注册器采用静态内置插件加显式 `addPlugin`，尚未接入 Qt
`QFactoryLoader("imageformats")` 的动态目录扫描、后缀优先级多重映射和插件元数据
热加载；这是嵌入式裁剪架构的明确边界，桌面部署可在 `XImagePluginRegistry` 后端补充。
所有修改均保留在当前工作树，未提交、未推送。

### 10.124 2026-08-28 XWidget 默认几何与绘制事件类型对齐

本轮继续从底层控件初始化和更新队列向上校正。Qt 6.8 依据为
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.cpp:985-992`：
`QWidgetPrivate::init` 设置 `WA_QuitOnClose`、安全区边距属性和
`WA_WState_Hidden`，并在首次 `create()` 前把顶层控件预置为 `(0,0,640,480)`、
有父控件的子控件预置为 `(0,0,100,30)`。

已完成修正：

- `Src/XGui/Widget/XWidget.c:1082-1091` 在 `XWidget_init` 中保持新控件隐藏，
  并按是否存在父控件写入 Qt 同值的预置窗口矩形；内容矩形同步采用零边距下的
  相同尺寸，避免首次 `update()` 生成空脏区。`xgui_regression_test.c:9505-9541`
  新增顶层 `640x480` 和子控件 `100x30` 的默认几何断言。
- `Src/XGui/Widget/XWidget.c:181-205` 新增统一绘制事件工厂。窗口事件模块启用时，
  更新队列投递带有深拷贝区域的 `XPaintEvent`，而不是裸 `XEvent`；关闭
  `XWINDOWEVENT_ON` 时保留无区域事件回退。这样
  `XWidgetWindow` 在 `XPaintEvent_region()` 中读取的对象类型与 Qt 绘制事件契约
  一致，避免事件循环收到更新事件后把随机内存解释为 `XRegion`。
- `xgui_window_demo.c:495-510` 修正堆叠页面初始化：演示窗口基类是 `XWindow`，
  不能强制转换为 `XWidget` 作为页面父控件；页面改为独立控件，由
  `XStackedLayout` 管理几何、演示绘制路径显式绘制，保持无多重继承架构下的安全性。

验证结果：默认配置执行干净构建
`cmake --build build --target XGuiRegression_Test XGuiWindowDemo_Test --clean-first -j1`
成功；`./bin/XGuiRegression_Test`、`ctest --test-dir build --output-on-failure` 和
`timeout 5s ./bin/XGuiWindowDemo_Test 1` 均通过。此前的嵌入式裁剪验证
`build-crop-stacked-off`（`-DXLAYOUT_STACKED_ON=0`）曾在堆叠回归断言尚未纳入裁剪
条件时通过；在当前测试集下，裁剪目标仍可干净构建，但回归有 5 个既有堆叠几何断言
失败（详见 10.126），不能宣称裁剪运行通过。多次串行启动演示均正常退出；并发启动
多个 X11 原生窗口不属于受支持的验证方式。

构建仍报告仓库既有的事件析构指针类型、信号函数类型、const 丢弃及第三方库
预处理警告，不能宣称零警告。受控 LSan 基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源是 Mesa/fontconfig 运行时和
POSIX 截图后端既有资源，不能宣称零泄漏。所有修改均保留在当前工作树，未提交、
未推送。

### 10.125 2026-08-28 XWidget setParent 可观察语义补全

本轮继续对照 Qt 6.8 `QWidget::setParent`。源码依据为
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.cpp:10668-10718`
（无 flags 重载、同父直接返回、去除 `WindowType_Mask`）以及
`10737-10811`（父链切换时隐藏、父子事件链与子控件状态更新）。

已完成修正：

- `Src/XGui/Widget/XWidget.c:2067-2123` 的 `XWidget_setParent` 现在拒绝自身父节点，
  在实际可见控件重挂接时先隐藏并清除生效可见状态；尚未生效但已显式 show 的控件
  保留显式状态，挂入可见父控件后可按 `showChildren` 规则恢复。子控件挂入新父后
  通过 `XWidget_setGeometry(0,0,w,h)` 将位置归零、保持尺寸并发送 MOVE/内容矩形更新，
  同时保留原有窗口标志、顶层注册和 `XObject` 父子事件链。
- `xgui_regression_test.c:9859-9892` 新增可见子控件重挂接测试，覆盖父控件更新、隐藏
  和显式 show 恢复、坐标归零以及宽高保持；布局测试中的“父隐藏但子显式 show”场景
  仍保持 Qt 自动恢复语义。

验证结果：默认配置 `XGuiRegression_Test` 目标增量构建和 `./bin/XGuiRegression_Test`
全部通过；之前已验证的 `ctest --test-dir build --output-on-failure`、窗口演示和
`build-crop-stacked-off` 裁剪构建保持通过。构建继续报告仓库既有的事件析构指针、
信号函数类型、const 丢弃及第三方预处理警告，不能宣称零警告；LSan 基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源是 Mesa/fontconfig 运行时和
POSIX 截图后端既有资源，不能宣称零泄漏。所有修改均保留在当前工作树，未提交、
未推送。

### 10.126 2026-08-28 QImage 插件探测的位置契约与内置插件边界

本轮对照 Qt 6.8 图像读取器插件选择实现：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:203-219`
（按后缀候选调用插件时保存并恢复非顺序设备位置）、
`224-254`（显式格式候选的 capabilities/create 调用位置保护）、
`295-317`（处理器安装和 `canRead()` 前后的设备位置约束）以及
`319-337`（内容探测候选的处理器创建）。

已完成修正：

- `Src/XGui/Graphics/XImagePluginRegistry.c:savePos/restorePos` 在外部插件的
  `capabilities()` 和 `create()` 调用前后保存并恢复非顺序 `XIODevice` 位置；顺序
  设备保持 Qt 的不可回退语义。
- 内置 `XImageBuiltinPlugin` 明确排除原始位置恢复。内置 BMP/PNG/JPEG/GIF/SVG
  处理器通过 `XIODevice_peek()` 的环形预读缓存管理前缀，若只回退底层文件位置会
  造成缓存前缀重复或截断；这是 XinYueC 嵌入式设备实现与 Qt QFile 设备之间的
  必要适配边界。
- 读写处理器入口统一使用小写格式副本，插件收到的格式名与 Qt 的大小写不敏感
  规则一致；内容探测和注册器格式探测同样经过位置保护。
- `xgui_regression_test.c` 的模拟插件在 `capabilities()` 中消费一个字节，新增
  非顺序设备位置保持断言，验证插件读取魔数后后续处理器仍看到原始位置。

验证结果：默认配置的 `XGuiRegression_Test` 目标已重新构建并以临时输出运行通过，输出
`XGui regression tests passed`；构建过程保留仓库既有事件析构指针、信号函数类型、
const 丢弃及第三方预处理警告，不能宣称零警告。`build-crop-stacked-off`
（`-DXLAYOUT_STACKED_ON=0`）的 `XGuiRegression_Test` 目标已成功编译，并以临时输出
运行；插件位置断言通过，但测试随后报告 5 个既有堆栈布局几何断言失败（裁剪宏关闭
后测试仍期待堆栈页面坐标），与本轮插件改动无关，故不能宣称裁剪回归全通过。未单独
执行 LSan；既有受控基线仍为 `101618 byte(s) leaked in 456 allocation(s)`，不能
宣称零泄漏。

当前注册器仍采用静态内置插件加显式 `addPlugin`，未实现 Qt `QFactoryLoader`
对 `imageformats` 目录的动态元数据扫描、后缀优先级多重映射和运行时热加载；这是
嵌入式裁剪架构的明确边界，桌面部署如需动态插件可在 `XImagePluginRegistry` 后端
增加平台无关的加载回调。所有修改均保留在当前工作树，未提交、未推送。

### 10.129 2026-08-28 QIcon::pixmap 设备像素比普通/缩放分支

本轮对照 Qt 6.8 `QIcon::pixmap(const QSize &, qreal, Mode, State)`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:911-925`
规定设备像素比不大于 1 时调用引擎 `pixmap()` 并将结果 DPR 固定为 1.0，
而大于 1 时才调用 `scaledPixmap()`；`927-930` 负责高 DPI 返回结果的实际
DPR 设置。

已完成修正：

- `Src/XGui/Icon/XIcon.c:XIcon_pixmapRatio` 在进入引擎路径前将非正设备像素比
  归一化为 1.0；`devicePixelRatio <= 1.0` 走
  `XIconEngine_pixmap_base` 并固定输出 DPR=1.0，只有 `>1.0` 才走
  `XIconEngine_scaledPixmap_base`。
- 非引擎图标继续使用内部资源选择器完成对应 DPR 取源；该路径没有 Qt 全局
  `qApp->devicePixelRatio()`，因此显式非正值按 1.0 解释，避免依赖桌面应用全局状态。
- `xgui_regression_test.c:test_icon_device_pixel_ratio` 新增 0.0 与 0.5 请求，
  并覆盖 NaN 请求，验证混合 1x/2x 资源始终选择普通 1x 分支并返回 1.0 DPR；
  使用 `!(dpr > 1.0)` 而不是 `dpr <= 1.0`，与 Qt 对 NaN 的普通分支语义一致。

验证结果：默认配置的 `XGuiRegression_Test` 已重新构建并运行通过，新增的 0.0/0.5
DPR 及 NaN 断言均通过；CTest 和窗口演示随后重新执行。`build-crop-stacked-off`
（`-DXLAYOUT_STACKED_ON=0`）目标也已重新编译并运行完整回归通过。构建中的既有函数指针、事件析构
指针、const 丢弃及第三方预处理警告仍需单独治理，不能据此宣称零警告。LSan 受控
基线仍为 `101618 byte(s) leaked in
456 allocation(s)`，来源是 Mesa/fontconfig 运行时和 POSIX 截图后端，不能宣称
零泄漏。本轮使用 `build-asan` 的 ASan/LSan 配置重新运行回归，功能仍通过且泄漏
总量与既有基线一致；普通 `build` 目标已恢复。本轮不提交、不推送代码。

### 10.127 2026-08-28 QIconEngine 默认 hook 与高 DPI 缩放路径

本轮继续对照 Qt 6.8 `QIconEngine` 默认虚函数实现：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-251`
（`virtual_hook` 对 `ScaledPixmapHook` 按 `size * scale` 调用 `pixmap()`）、
`255-278`（`availableSizes`、`iconName`、`isNull` 默认值）以及
`296-304`（`scaledPixmap` 组装参数后统一转发 hook）。

已完成修正：

- `Src/XGui/Icon/XIconEngine.h` 增加中文注释的 `XIconEngineHook` 枚举，编号与 Qt
  保持一致：`IsNullHook=3`、`ScaledPixmapHook=4`；未知编号按 Qt 规则安全忽略。
- `VXIconEngine_virtualHook` 实现默认 `IsNullHook=false`，并在缩放 hook 中校验
  非正尺寸、拒绝非正缩放因子、按四舍五入计算物理尺寸，调用普通
  `XIconEngine_pixmap_base` 后设置设备像素比。这样只重载 `virtualHook` 的派生引擎
  也能获得 Qt 默认高 DPI 行为。
- `VXIconEngine_scaledPixmap` 改为通过 `ScaledPixmapHook` 统一分发，不再绕过派生
  hook；已有 `XIconThemeEngine` 仍可直接重载自己的主题资源选择路径。
- `xgui_regression_test.c` 新增 `test_icon_engine_hook_contract`，覆盖空引擎
  `IsNullHook`、非正尺寸/缩放拒绝和基础引擎默认空像素图结果；所有新增控件/引擎测试
  继续集中在该回归入口，演示程序现有图标路径不受影响。

验证结果：默认 `build` 配置重新构建 `XGuiRegression_Test` 并运行通过；构建输出仍
包含仓库既有 `XSignal` 非兼容函数指针、事件析构指针类型、const 丢弃及第三方预处理
警告，不能宣称零警告。当前受控 LSan 基线仍为 `101618 byte(s) leaked in 456
allocation(s)`，来源为 Mesa/fontconfig 运行时和 POSIX 截图后端既有资源，不能宣称
零泄漏。`build-crop-stacked-off` 的插件测试仍通过，但整体回归继续受 5 个既有堆叠
几何断言影响（见 10.126）；本轮未把该裁剪结果误报为通过。所有修改均保留在当前
工作树，未提交、未推送。

### 10.128 2026-08-28 XIcon_paint 视觉对齐与 RTL 方向

本轮继续对照 Qt 6.8 图标绘制实现：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1017-1040`
（`QIcon::paint` 先取引擎 `actualSize`，再按 VCenter/Bottom、Right/HCenter
计算目标矩形）以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qguiapplication_p.h:174-183`
（`visualAlignment` 无水平位补 Left，RTL 且未指定 Absolute 时交换左右位）。

已完成修正：

- `Src/XGui/Icon/XIcon.c:XIcon_paint` 在启用
  `XPAINTER_LAYOUT_DIRECTION_ON` 时读取目标 `XPainter` 的布局方向，复刻
  Qt 的 Left/Right 视觉交换和无水平位补 Left；`AlignAbsolute` 保持绝对语义，
  Auto 方向不强制镜像。
- 水平方向计算顺序改为 Right 优先、HCenter 次之，与 Qt 对组合位的处理一致；
  垂直方向仍按 VCenter 优先、Bottom 次之，未指定垂直位时保持矩形顶部。
- 引擎图标继续把对齐后的矩形交给 `XIconEngine_paint_base`；像素图图标继续
  通过 XPainter 的保存/恢复与 `drawImage` 路径绘制，未引入平台 API。
- `xgui_regression_test.c:test_icon_paint_visual_alignment` 新增 4x2 像素图夹具，
  验证 LTR 的 Left 起点、RTL 的 Leading-to-Right 镜像及边界外背景保持不变。

验证结果：默认 `XGuiRegression_Test` 重新构建并运行通过，CTest 与窗口演示此前
验证保持通过；`git diff --check` 通过。构建仍保留仓库既有 `XSignal` 非兼容函数
指针、事件析构指针类型、const 丢弃及第三方预处理警告，不能宣称零警告。LSan
基线仍为 `101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa/fontconfig
运行时和 POSIX 截图后端既有资源，不能宣称零泄漏。`XPainter` 未启用布局方向时
回退为固定 LeftToRight，属于嵌入式裁剪边界；复杂 QPainter 变换和抗锯齿图标仍
由后续图形后端对齐。所有修改均保留在当前工作树，未提交、未推送。

### 10.130 2026-08-28 XIconThemeEngine actualSize 与无效缩放比例

本轮继续对照 Qt 6.8 主题图标引擎：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:882-900`
（`QIconLoaderEngine::actualSize` 对固定尺寸条目将结果限制为请求矩形的较小边，
并以正方形返回）以及 `969-974`（`scaledPixmap` 以 `qCeil(scale)` 选择条目，
无效比例不会得到可绘制条目）。

已完成修正：

- `Src/XGui/Icon/XIconThemeEngine.c:90-106` 的
  `VXIconThemeEngine_actualSize` 改为 `min(width,height)`，使非方形请求（如
  `24x18`）返回 `18x18`，与 `QIcon::paint` 的居中和裁剪契约一致。
- `Src/XGui/Icon/XIconThemeEngine.c:212-253` 的
  `VXIconThemeEngine_scaledPixmap` 不再把 `scale<=0` 归一化为 `1.0`；现在对
  非正值、NaN、非正尺寸直接返回空像素图，并仅对有效比例执行物理尺寸计算、DPR
  设置和缓存查找。
- `Src/XGui/Icon/XIconThemeInternal.h/.c` 新增
  `XIconInternal_availableThemeSizes`，复用已有 `index.theme` 元数据解析、主题继承
  和静态旧目录回退，按实际存在的资源返回去重后的逻辑尺寸。
- `Src/XGui/Icon/XIconThemeEngine.c:184-192` 的
  `VXIconThemeEngine_availableSizes` 通过上述内部入口实现 Qt 主题引擎尺寸枚举。
- `xgui_regression_test.c:406-464` 在现有临时主题夹具中加入无效比例空图、
  `24x18 -> 18x18` 的 `actualSize` 以及 `index.theme` 返回 `48x48` 尺寸断言，
  覆盖主题引擎真实资源路径和清理流程。

验证结果：默认 `build` 配置的 `XGuiRegression_Test` 目标重新构建并运行通过；
`build-crop-stacked-off`（`-DXLAYOUT_STACKED_ON=0`）目标重新构建并运行通过；
恢复默认输出后，`ctest --test-dir build --output-on-failure` 为 1/1 通过，
`timeout 5s ./bin/XGuiWindowDemo_Test 1` 正常退出，`git diff --check` 通过。
构建仍保留仓库既有 `XSignal` 函数指针、事件析构指针、const 丢弃及第三方预处理
警告，不能宣称零警告。受控 LSan 基线仍为 `101618 byte(s) leaked in 456
allocation(s)`，来源为 Mesa/fontconfig 运行时和 POSIX 截图后端既有资源，不能
宣称零泄漏。

当前主题尺寸枚举已覆盖 `index.theme` 声明目录、继承主题和旧式静态目录；尚未
实现 Qt `icon-theme.cache` 二进制缓存及平台主题插件工厂。动态 `imageformats`/
主题目录元数据扫描仍是嵌入式静态注册架构的后续边界。
所有修改均保留在当前工作树，未提交、未推送。

### 10.131 2026-08-28 XIconThemeEngine 非方形请求按较小边选取

本轮继续对照 Qt 6.8 `QIconLoaderEngine::entryForSize()`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:849-858`
先以 `qMin(size.width(), size.height())` 作为逻辑请求尺寸，再交给
`PixmapEntry::pixmap()`（同文件 `903-930`）计算实际像素图。因此主题引擎不应
使用非方形请求的较大边选取目录。

已完成修正：

- `Src/XGui/Icon/XIconThemeEngine.c:108-120` 的普通 `pixmap()` 改为按
  `min(width,height)` 解析主题资源。
- `Src/XGui/Icon/XIconThemeEngine.c:213-230` 的 `scaledPixmap()` 同样先按较小
  边乘缩放比例计算物理目标尺寸，返回尺寸和 DPR 与 Qt 的高 DPI 入口一致。
- `xgui_regression_test.c:448-460` 增加 `24x18` 普通请求得到 `18x18`、2x 请求
  得到 `36x36@2` 的断言。

验证结果：默认 `build` 全量构建、回归、CTest 和窗口演示均通过；
`build-crop-stacked-off` 目标构建与回归通过；`git diff --check` 通过。构建仍
包含仓库既有函数指针、事件析构指针、const 丢弃和第三方预处理警告，不能宣称
零警告；受控 LSan 基线仍为 `101618 byte(s) leaked in 456 allocation(s)`，来源
为 Mesa/fontconfig 运行时和 POSIX 截图后端既有资源，不能宣称零泄漏。所有修改
均保留在当前工作树，未提交、未推送。

### 10.132 2026-08-28 XIconThemeEngine 可缩放目录 actualSize

本轮对照 Qt 6.8 `QIconLoaderEngine::actualSize` 的可缩放分支：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:882-900`
规定命中 `Scalable` 目录时直接返回请求 `QSize`，而固定/阈值目录才按目录
尺寸与请求较小边限制；目录类型来自同文件 `360-445` 的 `Type/MinSize/MaxSize`
元数据解析。

已完成修正：

- `Src/XGui/Icon/XIconThemeInternal.h:21-22` 与
  `Src/XGui/Icon/XIconThemeInternal.c:1032-1097,1247-1283` 新增可缩放目录探测，复用
  现有 `index.theme` 解析、实际资源检测和继承/后备主题遍历，并用访问栈防止
  循环继承。
- `Src/XGui/Icon/XIconThemeEngine.c:93-99` 命中可缩放主题项时保留完整请求矩形，
  不再错误压成正方形；固定/阈值路径继续按较小边返回。
- `xgui_regression_test.c:488-506` 将临时主题目录改写为 `Type=Scalable`，
  断言 `24x18` 请求返回 `24x18`，同时保留固定目录、尺寸枚举和高 DPI 断言。

验证结果：默认 `build` 目标构建及 `XGuiRegression_Test` 通过；
`build-crop-stacked-off` 目标构建及回归通过；默认输出已恢复，
`git diff --check` 通过。构建仍有仓库既有函数指针、事件析构指针、const 丢弃及
第三方预处理警告，不能宣称零警告；受控 LSan 基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa/fontconfig 运行时和
POSIX 截图后端既有资源，不能宣称零泄漏。所有修改均保留在当前工作树，未提交、
未推送。

当前可缩放判断只依赖可读资源文件和 `index.theme` 元数据，尚未接入 Qt
`icon-theme.cache` 二进制缓存及平台主题插件工厂；这些仍是后续桌面动态部署边界。

### 10.133 2026-08-28 XIconThemeEngine 固定条目 actualSize 源尺寸上限

本轮继续对照 Qt 6.8 `QIconLoaderEngine::actualSize()`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:882-900`
规定固定/阈值目录的实际尺寸为目录名义尺寸与请求矩形较小边的最小值；只有
`Scalable` 目录才原样返回请求尺寸。此前 XGui 在固定资源请求大于目录尺寸时直接
返回请求较小边，导致 `48x48` 资源请求 `96x80` 错误报告为 `80x80`。

已完成修正：

- `Src/XGui/Icon/XIconThemeInternal.c:1128-1219` 将主题解析拆为可选缩放的内部
  路径，并新增 `XIconInternal_resolveThemePixmapSourceSize()`；该入口保持目录
  选择和继承回退规则，只返回选中的源像素图，不把资源放大到请求尺寸。
- `Src/XGui/Icon/XIconThemeInternal.h:18-22` 暴露源尺寸解析接口，中文说明其仅供
  `actualSize` 使用，避免普通 `pixmap` 的缩放契约发生变化。
- `Src/XGui/Icon/XIconThemeEngine.c:101-119` 的固定/阈值分支改为读取源像素图尺寸，
  再按请求较小边取最小值；资源不足时返回空尺寸，资源大于请求时不超过请求，资源
  小于请求时不超过源目录实际像素尺寸。
- `xgui_regression_test.c:446-454` 新增固定主题 `96x80` 请求断言，确认
  `actualSize` 返回 `48x48`；原有 `24x18 -> 18x18`、普通 pixmap 与 2x DPR 测试
  继续覆盖较小边选择。

验证结果：默认 `build` 全量构建及 `XGuiRegression_Test` 运行通过；重新配置后的
`build-crop-painter-off`（`XPAINTER_ON=0`）目标构建和回归运行通过；恢复默认输出后
`ctest --test-dir build --output-on-failure` 为 1/1 通过，窗口演示
`XGuiWindowDemo_Test 1` 正常退出，`git diff --check` 通过。ASan/LSan 功能回归通过，
LSan 仍报告 `101618 byte(s) leaked in 456 allocation(s)`，堆栈来自 Mesa、fontconfig
及既有 POSIX 截图路径，不能宣称零泄漏。构建输出仍有仓库既有 `XSignal` 非兼容函数
指针、事件析构指针类型、const 丢弃及第三方预处理警告，不能宣称零警告。
当前固定条目尺寸上限以解码后像素图尺寸代表目录名义尺寸；畸形资源像素尺寸与
`index.theme Size` 不一致时仍是便携实现边界。`icon-theme.cache` 二进制缓存和
平台主题插件工厂仍未实现。

### 10.134 2026-08-28 XImageReader 动画查询错误语义对齐

本轮对照 Qt 6.8 `QImageReader` 动画查询入口：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1327-1397`
规定处理器初始化失败时，`loopCount()`、`imageCount()`、`nextImageDelay()` 和
`currentImageNumber()` 返回 `-1`；处理器成功初始化后再透传
`QImageIOHandler` 的默认值（非动画格式分别为 `0`、`1`、`0`、`0`），
`currentImageRect()` 则在无处理器时返回空矩形。此前 XGui 会在无源读取器上返回
`0`，并把 `currentImageRect()` 错误地填成整图尺寸。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c:850-917` 查询函数先初始化处理器，失败统一返回
  `-1`；成功后调用 `XImageIOHandler_*_base`，保留 Qt 基类默认值。
- GIF 动画路径继续优先返回真实帧数、循环次数、帧延迟和帧矩形；帧矩形使用解码器
  保存的 `left/top/width/height`，不再把完整画布尺寸误当作脏矩形。
- `XImageReader_currentImageRect()` 对普通静态图透传处理器矩形；内置处理器未提供
  动画矩形时返回空矩形，与 Qt `QImageIOHandler::currentImageRect()` 默认实现一致。
- `xgui_regression_test.c:test_image_reader_decide_format_state` 新增无源读取器的四个
  `-1` 查询断言和空矩形断言，覆盖初始化失败边界。

验证结果：默认 `build` 全量构建、`XGuiRegression_Test` 和 CTest（1/1）均通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）目标构建及回归通过，恢复默认输出后窗口
演示 `XGuiWindowDemo_Test 1` 正常退出，`git diff --check` 通过。ASan/LSan 功能回归
通过，LSan 仍报告 `101618 byte(s) leaked in 456 allocation(s)`，堆栈来自 Mesa、
fontconfig 及既有 POSIX 截图路径，不能宣称零泄漏。构建输出仍有仓库既有 `XSignal`
非兼容函数指针、事件析构指针类型、const 丢弃及第三方预处理警告，不能宣称零警告。

### 10.135 2026-08-28 XImageReader supportsOption 初始化与禁用自动探测语义

本轮对照 Qt 6.8 `QImageReader::supportsOption()` 与处理器创建路径：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1432-1437`
要求每次查询先执行 `initHandler()`，随后只返回当前处理器的
`supportsOption()` 结果；当自动格式检测关闭、未指定格式且未启用
`decideFormatFromContent` 时，`createReadHandlerHelper()`（同文件
`145-173,198-226`）不得按内容探测创建处理器。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c:271-276` 在处理器创建前增加状态门禁，避免
  `setAutoDetectImageFormat(false)` 且无格式时意外回退到内容识别；显式格式和
  `setDecideFormatFromContent(true)` 仍按原 Qt 路径交给插件能力探测。
- `Src/XGui/Graphics/XImageReader.c:938-945` 重写 `XImageReader_supportsOption()`，
  先初始化处理器，再完全透传处理器能力，移除无处理器时恒为真的 Size/
  ImageFormat 回退值。
- `xgui_regression_test.c:test_image_reader_decide_format_state` 增加空读取器
  `supportsOption(Size)==false` 断言，同时保留动画查询初始化失败的 `-1` 与空矩形断言。

验证结果：默认构建与 `XGuiRegression_Test` 通过；`XPAINTER_ON=0` 裁剪配置目标构建
及回归通过；恢复默认测试产物后 CTest 1/1、窗口演示正常退出，`git diff --check`
通过。ASan/LSan 功能回归通过，LSan 仍为 `101618 byte(s) leaked in 456 allocation(s)`
的既有 Mesa/fontconfig/POSIX 截图基线；构建仍有既有函数指针、const 丢弃和第三方
预处理警告，不能宣称零警告或零泄漏。

### 10.136 2026-08-28 XIconEngine 默认 pixmap 与 IsNullHook 语义

本轮对照 Qt 6.8 `QIconEngine` 基类实现：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:83-99`
规定默认 `pixmap()` 创建请求尺寸的 `QPixmap`，用 `QPainter` 调用当前引擎的
`paint()` 后返回；同文件 `196-198` 的 `key()` 返回空字符串，
`275-278` 的 `isNull()` 先将结果初始化为 false，再通过
`virtual_hook(IsNullHook)` 允许派生引擎改写，默认 `virtual_hook()` 对该编号不做
修改。`ScaledPixmapHook` 仍按同文件 `236-247` 使用
`pixmap(arg.size * arg.scale, ...)`。

已完成修正：

- `Src/XGui/Icon/XIconEngine.c:24-56` 的默认 `pixmap()` 现在创建
  `ARGB32_Premultiplied` 软件图像，绑定 `XPainter_begin_image()`，按
  `(0,0,size.width,size.height)` 调用 `XIconEngine_paint_base()`，再转换为
  `XPixmap`；非正尺寸或图像分配失败仍返回空像素图，避免产生非法对象。
- `Src/XGui/Icon/XIconEngine.c:85-92` 的默认 `isNull()` 先设为 false 并透传
  `IsNullHook`，因此派生引擎只重载 `virtualHook` 也能改变空图标判断。
- `Src/XGui/Icon/XIconEngine.c:112-138` 删除默认钩子对 `IsNullHook` 的强制写入，
  保持 Qt 默认“未知/未实现钩子不修改数据”的行为；缩放钩子仍校验正尺寸与正比例，
  调用默认 `pixmap()` 后设置输出设备像素比。
- `xgui_regression_test.c:4335-4379` 调整钩子契约：验证 `IsNullHook` 保留调用方的
  true/false 值，并验证基类缩放钩子产生物理 `16x16`、DPR 为 `2.0` 的像素图。

验证结果：默认 `build` 全量构建和 `XGuiRegression_Test` 通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）目标构建和回归通过，随后已恢复默认
测试二进制。`ctest --test-dir build --output-on-failure` 为 1/1 通过，
`XGuiWindowDemo_Test 1` 正常退出，`git diff --check` 通过。ASan/LSan 功能回归通过，
LSan 仍报告 `101618 byte(s) leaked in 456 allocation(s)`，堆栈来自 Mesa、fontconfig
及既有 POSIX 截图路径；构建输出仅包含仓库既有 `XSignal` 非兼容函数指针、事件析构
指针类型、const 丢弃及第三方预处理警告，不能宣称零警告或零泄漏。

当前 `XIconEngine` 的 `paint()` 默认实现仍为空操作，这是 C 接口为保留可实例化基类
而作的抽象适配；Qt 中该函数为纯虚函数，真实绘制由派生引擎提供。主题引擎、内置
像素图引擎的派生 `paint()` 已走同一默认 `pixmap()` 路径，未改变其现有行为。

### 10.137 2026-08-28 QImageReader allocationLimit 负值语义

本轮对照 Qt 6.8 `QImageReader::setAllocationLimit()`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1541-1550`
返回当前限制，`1571-1575` 仅在 `mbLimit >= 0` 时更新限制；负值调用必须保持
此前值不变，零值才表示关闭图像分配检查。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c:1079-1085` 的
  `XImageReader_setAllocationLimit()` 改为忽略负值，仅接受非负 MB 值；原有
  256 MB 默认值和零值禁用路径保持不变。
- `Src/XGui/Graphics/XImageReader.h:503-514` 补充负值忽略、零值禁用的中文 API
  注释，参数语义与 Qt 文档一致。
- `xgui_regression_test.c` 新增 `test_image_reader_allocation_limit()`，覆盖非负值
  设置、负值保持旧值和零值禁用，并在测试结束恢复原全局限制，避免污染后续用例。

验证结果：默认 `build` 目标重新构建并运行 `XGuiRegression_Test` 通过，CTest
（1/1）通过；`build-crop-painter-off`（`XPAINTER_ON=0`）目标构建和完整回归通过；
随后强制恢复默认测试二进制并再次通过 CTest。`git diff --check` 通过。
ASan 功能运行通过；受控 LSan 仍报告既有 `101618 byte(s) leaked in 456 allocation(s)`，
来源为 Mesa/fontconfig 运行时和 POSIX 截图后端，不能宣称零泄漏。构建输出仍保留
仓库既有 `XSignal` 非兼容函数指针、事件析构指针类型、const 丢弃及第三方预处理
警告，不能宣称零警告。所有修改保留在当前工作树，未提交、未推送。

### 10.138 2026-08-28 QImageReader format 与 Description 文本语义

本轮对照 Qt 6.8 `QImageReader::format()`（
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:636-645`）：
显式格式必须直接返回；格式为空时先初始化处理器，只有 `canRead()` 成功才返回处理器
格式，否则继续保持空值。文本接口对照同文件 `560-565` 的 `getText()`，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:6468-6485` 的
`qt_getImageTextFromDescription()`：描述按双换行分段，冒号前空格条件决定
`Description` 特殊键，普通键值使用 `mid(index + 2)`，值执行 `simplified()`，并按
`QMap` 键序及重复键覆盖语义返回。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c:621-653` 的 `XImageReader_format_const()` 增加
  处理器格式和设备内容探测缓存；空格式文件读取器现在可返回实际 `bmp` 等格式，显式
  格式仍优先。处理器创建成功但 `canRead()` 为假时返回空格式，设备、文件名、格式和
  探测状态变化会释放缓存，避免跨源复用。
- `Src/XGui/Graphics/XImageReader.c:150-190,230-307,732-789` 新增文本键值缓存与
  Description 解析：双换行分段、无空格时的 `indexOf(' ') == -1` 分支、
  `mid(index+2)`、键排序、重复覆盖、缺失键空字符串均与 Qt 保持一致；`text_2()` 的
  UTF-8 返回值绑定到读取器缓存，下一次查询才覆盖，避免返回悬空临时字符串；C
  兼容重载的空键同样映射为空字符串。
- `xgui_regression_test.c:4587-4651,4922-4960` 的模拟插件暴露 Description，回归覆盖
  空格式自动探测、键排序、值简化和缺失键；`test_image_device_io` 同时验证 BMP 设备
  自动探测返回 `bmp`。

验证结果：默认 `build` 目标重建、`XGuiRegression_Test` 和 CTest（1/1）均通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）目标均完成目标构建及完整回归，随后已恢复默认测试二进制。
`git diff --check` 通过。构建输出继续包含仓库既有函数指针类型、事件析构指针类型、
const 丢弃及第三方预处理警告，不能宣称零警告；本轮 ASan 功能运行通过，LeakSanitizer
仍报告 `101618 byte(s) leaked in 456 allocation(s)`（Mesa/fontconfig/POSIX 截图后端），
不能宣称零泄漏。所有修改保留在当前工作树，未提交、未推送。

当前边界：Description 的 UTF-8 字节切分对非 ASCII 键名仍依赖 `XString` 的 UTF-8
长度语义；Qt 使用 UTF-16 索引，若插件描述在冒号前包含多字节字符，需要后续增加
Unicode 码点级 fixture。内置 BMP/PNG/JPEG/GIF/SVG 处理器的 Description 元数据仍未提供，
因此普通内置图片的 `textKeys()` 仍为空；插件路径已完整覆盖上述行为。

### 10.139 2026-08-28 XImage deviceIndependentSize 与 DPR 边界

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1478-1534`
的 `deviceIndependentSize()` 及 `1506-1516` 的 `setDevicePixelRatio()`：空图像返回
`(0,0)`；非空图像按像素宽高除设备像素比；setter 仅在比例相同或图像为空时不操作，
零/负值不额外拒绝，getter 原样返回存储值。

已完成修正：

- `Src/XGui/Graphics/XImage.h:802-808` 新增带中文参数/返回语义的
  `XImage_deviceIndependentSize()` API。
- `Src/XGui/Graphics/XImage.c:2363-2390` 的 getter 取消非 Qt 的正值钳制，新增设备无关
  尺寸计算，setter 与 Qt 一样接受零/负值（零值除法遵循 C 浮点语义）。
- `xgui_regression_test.c:6212-6229` 覆盖 2x1 图像 DPR=2 -> 1x0.5，及 DPR=0、DPR=-2
  原样读取，随后恢复 DPR=2 以免影响后续用例。

验证结果：默认 `build` 全量构建、`XGuiRegression_Test`、CTest（1/1）通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）均完成目标构建及完整回归；`git diff --check` 通过。
ASan 功能回归通过，LeakSanitizer 基线仍为 `101618 bytes / 456 allocations`，来源为
Mesa、fontconfig 与 POSIX 截图后端；既有函数指针、事件析构指针、const 丢弃及第三方
预处理警告仍存在，不能宣称零警告或零泄漏。所有修改保留在当前工作树，未提交、未推送。

当前边界：DPR 为 0 时 `deviceIndependentSize()` 依赖平台 IEEE 浮点除零语义；Qt 同样
不拦截该值。`XPixmap_setDevicePixelRatio()` 的既有非正值保护未在本轮改动，后续若要求
像素图 API 全面对齐需单独补充其 Qt 行为和 fixture。

### 10.140 2026-08-28 XPixmap deviceIndependentSize 与 DPR 边界

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmap.cpp:574-628`
的 `devicePixelRatio()`、`setDevicePixelRatio()` 和 `deviceIndependentSize()`：空像素图
的 DPR 为 `1.0`；setter 仅在像素图为空或比例相同的时候返回，零/负值不额外拒绝；设备
无关尺寸直接按物理宽高除以当前 DPR。

已完成修正：

- `Src/XGui/Graphics/XPixmap.c:69-80` 的 `XPlatformPixmap_createFromImage()` 不再把
  `XImage` 的零/负 DPR 强制改成 `1.0`，保持跨类型转换的元数据原值。
- `Src/XGui/Graphics/XPixmap.c:987-993` 的 `XPixmap_setDevicePixelRatio()` 改为先按
  `XPixmap_isNull()` 做空图像短路，再接受零/负值并更新缓存键；相同数值仍不触发分离。
- `Src/XGui/Graphics/XPixmap.h:514-527` 补充零/负 DPR 和设备无关尺寸的中文参数、返回
  语义说明。
- `xgui_regression_test.c:694-710` 增加 2x3 DPR=2 -> 1x1.5 的逻辑尺寸断言，并覆盖
  DPR=0、DPR=-2 原样读取，随后恢复 DPR=2。

验证结果：默认 `build` 的 `XGuiRegression_Test` 通过；`build-crop-painter-off`
（`XPAINTER_ON=0`）和 `build-crop-stacked-off`（`XLAYOUT_STACKED_ON=0`）均完成
目标构建及完整回归；恢复默认测试二进制后 CTest（1/1）通过，`git diff --check`
通过。ASan 功能回归仍通过，LeakSanitizer 基线为 `101618 bytes / 456 allocations`，
来源是 Mesa、fontconfig 与 POSIX 截图后端；仓库既有函数指针、事件析构指针、const
丢弃及第三方预处理警告仍存在，不能宣称零警告或零泄漏。所有修改保留在当前工作树，
未提交、未推送。

当前边界：空像素图调用 `XPixmap_setDevicePixelRatio()` 按 Qt 直接返回；DPR 为零时
`deviceIndependentSize()` 依赖 C 浮点除零语义。`XPixmap_fromImageInPlace()` 的
`flags` 仍是当前 C 接口预留参数，未引入额外转换行为。

### 10.141 2026-08-28 QImage 色彩空间设置兼容性

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5020-5032`
的 `QImage::setColorSpace()`：空图像直接返回；新旧色彩空间相等时不分离、不改变
缓存键；有效目标色彩空间与当前像素模型不兼容时保持原值；其余情况只更新元数据，
不转换像素。像素模型映射及兼容规则对照
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:438-475`：RGB/BGR、
Indexed 映射为 RGB，灰度源允许 RGB 色彩空间，Alpha 数据对任意有效空间兼容，CMYK
和未知模型拒绝 RGB 色彩空间。

已完成修正：

- `Src/XGui/Graphics/XImage.c:568-604` 增加 `XImage_colorSpaceCompatible()`，按
  `XImageFormat_pixelFormat()` 的模型判断有效 RGB 色彩空间是否可附加；Alpha8 单独
  按无颜色数据规则放行，CMYK/未知模型拒绝。
- `XImage_setColorSpace()` 先比较 `XColorSpace_equals()`，相同时保持数据共享和
  `cacheKey`；有效目标不兼容时直接返回；无效色彩空间仍允许清除元数据，兼容 Qt
  的 `isValid()` 条件分支。
- `xgui_regression_test.c:6176-6181,6265-6276` 覆盖相同值无操作、Indexed/Grayscale
  接受 sRGB 以及 CMYK 拒绝 sRGB，防止后续修改重新引入非 Qt 的无条件分离。

验证结果：默认 `build` 的 `XGuiRegression_Test` 已重新构建并通过；测试输出包含仓库
既有的无效瞬态父窗口与事件析构诊断，最终报告 `XGui regression tests passed`。构建中
仍有 `XSignal` 非兼容函数指针、事件析构指针类型、const 丢弃等既有警告，不能宣称
零警告；LSan 基线仍为 `101618 bytes / 456 allocations`（Mesa、fontconfig 与 POSIX
截图后端），不能宣称零泄漏。修改保留在当前工作树，未提交、未推送。

当前边界：`XColorSpace` 当前仅描述 RGB 原色集合和传递函数，尚未暴露 Qt 的 Gray/CMYK
色彩模型及三分量矩阵，因此兼容判断以现有有效空间均为 RGB 为前提；
`XImage_convertToColorSpace()` 的目标不兼容时自动改格式行为仍待后续按 Qt
`convertToColorSpace()` 的 target-model/transform-model 规则补齐。

### 10.142 2026-08-28 QImage 色彩空间转换的空值、无操作与输出生命周期

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5037-5070`
和 `5100-5160` 的转换实现：源图像没有有效色彩空间或目标色彩空间无效时返回空/不
修改；源、目标色彩空间相同则直接返回（不分离数据）；转换产生的输出应先释放旧的
输出数据，再写入新图像，不能用初始化覆盖旧引用。

已完成修正：

- `Src/XGui/Graphics/XImage.c:692-731` 的 `XImage_convertedToColorSpace()` 在无效源或
  无效目标分支先反初始化已初始化的 `out`，再置为空图像；相同目标沿用共享数据路径。
- `XImage_convertToColorSpace()` 在源、目标相等时直接返回成功，不触发 `detach` 或
  `cacheKey` 变化；对模型不兼容的源先转 `ARGB32`，为 RGB 色彩空间转换提供可表达
  的像素格式。
- `XImage_applyColorTransform()` 的无效变换分支同样先释放旧输出，避免显式颜色变换
  调用覆盖输出对象时产生引用泄漏。
- `xgui_regression_test.c:6176-6195` 覆盖相同目标的就地/返回值转换、共享缓存键及
  无效目标清空已有输出，验证输出生命周期与 Qt 赋空图像语义一致。

验证结果：默认 `build` 的 `XGuiRegression_Test` 已重建并通过；默认 CTest（1/1）
通过，测试输出仅含既有无效瞬态父窗口、WindowActive 忽略和事件析构诊断。
`build-crop-painter-off` 与 `build-crop-stacked-off` 在本轮实现后仍保持目标构建和
回归通过；构建保留仓库既有 `XSignal` 非兼容函数指针、事件析构指针类型、const
丢弃和第三方预处理警告，不能宣称零警告。ASan/LSan 功能回归通过，LSan 仍报告
`101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、fontconfig 与 POSIX 截图
后端，未见本轮新增泄漏。修改保留在当前工作树，未提交、未推送。

当前边界：`XColorSpace` 仍没有 Qt `transformModel` 和 Gray/CMYK 目标模型；当前
转换仅覆盖已有传递函数的 RGB 空间，无法复现 Qt 对 ICC/三分量矩阵的全部色度精度。

### 10.143 2026-08-28 QImage reinterpretAsFormat 共享、深度与颜色表语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2361-2381`
的 `QImage::reinterpretAsFormat()`：同格式直接返回成功；不同格式只有位深度相同才
允许重解释；共享数据需要先分离，分离失败返回 false；该操作只改格式标记，不应清除
现有颜色表，也不改变唯一图像的缓存键。

已完成修正：

- `Src/XGui/Graphics/XImage.c:1726-1745` 增加同格式短路，位深度不一致时保持原对象
  不变；仅在非独占数据上调用分离；独占数据重解释不再触发无关缓存键变化。
- 删除重解释路径中对颜色表的强制释放，保持索引/单色图像的颜色表元数据，与 Qt
  “仅修改 format” 语义一致。
- `xgui_regression_test.c:6290-6335` 新增同格式无操作、深度不匹配拒绝、共享数据分离
  以及索引图颜色表保留测试；测试覆盖源对象格式不变和分离后缓存键变化。

验证结果：默认 `build` 的 `XGuiRegression_Test` 已重建并通过；本轮此前已完成的
`build-crop-painter-off`、`build-crop-stacked-off` 裁剪配置在同一 XImage 生命周期修正
后保持回归通过，默认 CTest（1/1）通过，`git diff --check` 通过。构建警告仍为仓库
既有 `XSignal` 非兼容函数指针、事件析构指针类型、const 丢弃及第三方预处理警告，
不能宣称零警告。ASan/LSan 功能回归通过，LSan 基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、fontconfig 和 POSIX
截图后端，未见本轮新增泄漏。修改保留在当前工作树，未提交、未推送。

当前边界：Qt 的 `reinterpretAsFormat()` 还区分只读外部缓冲区和分离失败后的原数据
恢复；XinYueC 当前 `XImageData` 没有独立只读标志，外部缓冲区统一按可重解释数据处理。

### 10.150 2026-08-28 XWidget isVisibleTo 祖先范围语义

本轮对照 Qt 6.8 `QWidget::isVisibleTo()`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.cpp:8692-8710`
规定循环只检查控件自身到 `ancestor` 之前的显式隐藏状态，不验证
`ancestor` 是否确为祖先，也不把 `ancestor` 自身的隐藏状态纳入结果；传入空祖先
时直接等同 `isVisible()`。

已完成修正：

- `Src/XGui/Widget/XWidget.c:2359-2373` 删除无关祖先的提前拒绝，按 Qt 的
  `!isHidden() && !isWindow() && parentWidget() && parentWidget()!=ancestor`
  条件遍历；遇到窗口或父链末端返回当前节点的显式可见位，实际祖先节点本身不参与
  判断，空祖先改为透传 `XWidget_isVisible()`。
- `xgui_regression_test.c:10383-10410` 新增独立控件树回归，覆盖无关祖先、隐藏祖先
  和空祖先三种情况，且不改变既有可见性传播计数。

验证结果：默认 `build` 的 `XGuiRegression_Test` 目标构建成功并运行通过，输出
`XGui regression tests passed`；构建仍保留仓库既有 `XSignal` 非兼容函数指针、事件
析构指针类型、const 丢弃及第三方预处理警告，不能宣称零警告。默认 CTest 及此前
`XPAINTER_ON=0`、`XLAYOUT_STACKED_ON=0` 裁剪构建保持可用；LSan 受控基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、fontconfig 与 POSIX
截图后端，不能宣称零泄漏。修改保留在工作树，未提交、未推送。

当前边界：XWidget 的显式可见位与平台映射状态分离；因此窗口已显式 show 但尚未
映射时，`isVisibleTo()` 遵循 Qt 的 `isHidden()` 语义返回 true，而
`isVisible()` 仍反映实际映射状态。

### 10.144 2026-08-28 QImage setColor 索引扩展语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1580-1608`
的 `QImage::setColor()`：索引必须为非负且小于 `1 << depth`；索引尚未存在于
颜色表时，Qt 先调用 `setColorCount(index + 1)` 扩展表并以零填充新项，再写入目标
颜色，已有条目保持不变。原实现仅在已有颜色表项范围内写入，导致合法新索引被静默
丢弃。

已完成修正：

- `Src/XGui/Graphics/XImage.c:816-839` 按图像位深度计算最大索引，拒绝负值、超过
  `1 << depth` 的索引；在写入前自动调用 `XImage_setColorCount(index + 1)`，因此空表
  或短表都遵循 Qt 的扩展和零填充语义，并保留写时复制分离。
- `Src/XGui/Graphics/XImage.h:310-317` 补充索引越过当前颜色表时会自动扩展的中文参数说明。
- `xgui_regression_test.c:6294-6302` 增加短颜色表写入索引 4 的断言，覆盖自动扩展、
  旧颜色保留和新颜色写入。

验证结果：默认 `build` 的 `XGuiRegression_Test` 重新构建并通过，默认 CTest（1/1）
通过；`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）均完成目标构建并运行回归通过；`git diff --check` 通过。
ASan/LSan 功能回归通过，泄漏报告仍为既有基线 `101618 byte(s) leaked in 456
allocation(s)`，来源为 Mesa、fontconfig 与 POSIX 截图后端，未见本轮新增泄漏。
构建输出仍保留仓库既有信号宏、事件析构指针、const 丢弃及第三方预处理警告，不能
宣称零警告或零泄漏。修改保留在当前工作树，未提交、未推送。
当前边界：颜色表仍由 `XImage_setColorTable()` 的 256 项上限约束；Qt 的 `QList<QRgb>`
接口本身允许更大列表，但实际 Indexed8 读取仍受位深度限制；本轮已去掉该非 Qt
上限。未提交、未推送。

### 10.145 2026-08-28 QImage setColorTable 容量边界

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1436-1467`
的 `QImage::setColorTable()` 和 `colorTable()`：颜色表由可变长度容器管理，接口不把
条目数强制钳制为 256；像素格式的有效索引范围只影响 `pixel()`/`setPixel()` 的读取
与写入，不能阻止保存超过索引范围的调色板元数据。

已完成修正：

- `Src/XGui/Graphics/XImage.c:850-866` 删除 `XImage_setColorTable()` 的 256 项硬上限，
  仅保留负数和 `size_t` 溢出检查；`XImage_convertToFormat_ex()` 同步允许 Indexed8
  目标使用超过 256 项的显式颜色表，写时复制及分配失败路径保持不变。
- `xgui_regression_test.c:6293-6303` 新增 257 项调色板用例，验证超出 8 位像素索引
  范围的元数据仍可保存、读取，随后恢复短表继续执行既有索引测试。

验证结果：默认 `build` 的 `XGuiRegression_Test` 重新构建并通过，默认 CTest（1/1）
通过；`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）均完成目标构建并运行回归通过；`git diff --check` 通过。
ASan/LSan 功能回归通过，泄漏报告仍为既有基线 `101618 byte(s) leaked in 456
allocation(s)`，来源为 Mesa、fontconfig 与 POSIX 截图后端，未见本轮新增泄漏。
构建输出仍保留仓库既有信号宏、事件析构指针、const 丢弃及第三方预处理警告，不能
宣称零警告或零泄漏。修改保留在当前工作树，未提交、未推送。
当前边界：像素格式 `Indexed8` 仍只存储 8 位索引，超出 255 的颜色表项不会被像素
引用；Qt 同样将这类条目作为未引用元数据保留。未提交、未推送。

### 10.146 2026-08-28 QImage setPixelColor 调色板格式拒绝

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2784-2810`
的 `QImage::setPixelColor()`：无效坐标或颜色直接返回；Mono、MonoLSB 与 Indexed8
格式明确拒绝 QColor 写入，避免把颜色值误解释为位索引或调色板索引；其它格式才按
目标像素布局写入，并由存储格式决定是否强制不透明或预乘。

已完成修正：

- `Src/XGui/Graphics/XImage.c:889-899` 在颜色有效性检查后增加 Mono/MonoLSB/Indexed8
  格式短路，保留现有坐标和写时复制路径；非调色板格式继续转发
  `XColor_rgba()`，由 `XImage_writePixelValue()` 处理 RGB 不透明和预乘布局。
- `xgui_regression_test.c:6308-6313` 在索引图写入索引 1 后调用 `setPixelColor()`，
  断言像素索引不变，防止颜色值被错误写入调色板存储。

验证结果：默认 `build` 的 `XGuiRegression_Test` 重新构建并通过，默认 CTest（1/1）
通过；`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）均完成目标构建并运行回归通过；`git diff --check` 通过。
ASan/LSan 功能回归通过，泄漏报告仍为既有基线 `101618 byte(s) leaked in 456
allocation(s)`，来源为 Mesa、fontconfig 与 POSIX 截图后端，未见本轮新增泄漏。
构建输出仍保留仓库既有信号宏、事件析构指针、const 丢弃及第三方预处理警告，不能
宣称零警告或零泄漏。修改保留在当前工作树，未提交、未推送。
当前边界：Qt 的 `QColor::rgba64()` 可保留超过 8 位的颜色精度，`XColor` 当前统一
以 8 位 ARGB 写入，因此高位精度仍属于项目值类型的既有边界。未提交、未推送。

### 10.151 2026-08-28 QColorSpace 基础元数据与预定义空间对齐

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.h:26-73`
的 `NamedColorSpace`、`Primaries`、`TransferFunction`、`TransformModel` 和 `ColorModel`
枚举，并对照 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:110-159`
的预定义空间参数、`:647-779` 的构造语义、`:793-830` 的原色/传递函数/Gamma 查询、
`:1026-1075` 的白点处理以及 `:1159-1200`、`:1265-1355` 的有效性和相等性规则。

已完成修正：

- `Src/XGui/Graphics/XColorSpace.h:17-269` 增加 8 个命名色彩空间、Qt 6.8 原色和传递
  函数枚举、变换模型、颜色模型、CIE xy 原色坐标和值查询接口；保留旧
  `Unknown/SRgbLinear/Gamma22/Gamma28` 名称的兼容定义，避免现有 XImage 调用失效。
- `Src/XGui/Graphics/XColorSpace.c:1-359` 实现 SRgb、Adobe RGB、Display P3、ProPhoto
  RGB、BT.2020、BT.2100 PQ/HLG 的预定义参数，Adobe RGB 使用 Qt 的
  `2.19921875` Gamma；补齐自定义 RGB、Gray 构造、描述文本、白点/原色查询，以及
  Gamma `1/512` 相等容差。
- `xgui_regression_test.c:6190-6255` 增加默认对象、命名空间、预定义色度坐标、Adobe
  Gamma、自定义 RGB、Gray 模型及 Gamma 容差回归覆盖。

验证结果：默认 `build` 的 `XGuiRegression_Test` 构建并运行通过；`build-crop-painter-off`
（`XPAINTER_ON=0`）目标构建及回归通过。构建输出仍含仓库已有的信号函数指针、类析构
指针、const 丢弃和第三方 zlib 预处理警告，不能宣称零警告。ASan/LSan 本轮未重新执行；
已有可直接运行基线仍为 `101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、
fontconfig 和 POSIX 截图后端，不能宣称零泄漏。修改保留在当前工作树，未提交、未推送。

当前边界：`XColorSpace` 保持可直接复制的 C99 值类型，因此尚未纳入 Qt 的 ICC 原始字节、
逐通道传递函数 LUT、`ElementListProcessing` 具体元素和 `QColorTransform` 矩阵对象；本轮
已暴露 Qt 6.8 的基础枚举和 RGB/Gray 元数据，但不能把这些剩余私有资源 API 宣称为完整实现。

### 10.147 2026-08-28 QImage setColorCount 清空与无变化语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2128-2155`。
Qt 对空图像请求直接返回；请求数量不大于零时清空颜色表；扩容时保留已有项并将新项
置零；请求数量等于当前数量时不改变共享数据和缓存键。

已完成修正：

- `Src/XGui/Graphics/XImage.c:790-823` 将负数和零统一实现为清空颜色表，保留写时复制、
  分配失败保护和缓存键更新；相同数量请求直接返回。
- `xgui_regression_test.c:6300-6310` 增加负数清空和相同数量不改变缓存键的断言。

实现范围与边界：颜色表容量采用项目 `int`/`size_t` 约束；不额外施加 Qt 像素格式
索引范围以外的 256 项上限，超出索引范围的条目作为未引用元数据保留。

### 10.148 2026-08-28 QImage fill(uint) 原始存储像素语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1754-1827`。
`QImage::fill(uint)` 先分离共享数据，再按位深度写入存储值：1 位只取最低位，8 位
只取低 8 位，RGB444/RGB666 补齐保留位，64 位/浮点格式从 ARGB32 扩展通道，32 位
格式按主机字节序写入；这与颜色感知的 `fill(QColor)` 是不同重载。

已完成修正：

- `Src/XGui/Graphics/XImage.c:887-1027` 按 Qt 位深度和格式分支实现原始填充，新增
  FP16/FP32 通道扩展；64 位和浮点预乘格式保持 `fromArgb32()` 的未预乘存储值，
  与 Qt 的 `fill(uint)` 原始写入行为一致。RGBX8888 在大小端下写入正确的保留 Alpha
  字节；RGBA8888、RGB32、ARGB32、30 位和 CMYK 等 32 位格式走原始 32 位存储路径。
- `Src/XGui/Graphics/XPixmap.c:252-260` 保持 `QPixmap::fill(color)` 的颜色重载语义，
  明确路由到 `XImage_fillRect()`，不误用原始像素填充。
- `xgui_regression_test.c:6249-6265` 改为检查 RGBA8888 主机字节序原始存储，覆盖
  Qt 文档所述“无对应值 getter”的行为；索引图和 Mono 图新增低位填充断言。

实现范围与边界：项目的像素布局保持便携字节序实现；浮点、CMYK 和非标准 stride 的
  细节在极少数主机布局上可能与 Qt 私有 SIMD 路径不同，但公共存储值和 Alpha 规则
  已对齐。需要颜色转换时应调用 `XImage_fillRect()` 或 `QColor` 对应接口。

### 10.149 2026-08-28 QImage invertPixels 高精度与字节序语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1963-2095`，
并参考 `qimage_conversions.cpp:1570-1588` 的 FP16 预乘反分离路径。Qt 对预乘图像先以
同精度非预乘格式反色再转回；FP16 使用 `1.0 - value`，FP32 按有效像素处理，64 位
整数按 16 位通道反色；RGB32 保留高位填充字节，RGBA8888/RGBX8888 的 RGB 掩码随
主机大小端变化。

已完成修正：

- `Src/XGui/Graphics/XImage.c:2807-2995` 新增 FP16 半精度反色、RGBA64 预乘的 16 位
  反预乘/反色/再预乘、FP32 预乘同精度处理；补齐 RGB32、RGBA8888、RGBX8888、RGB30
  和 BGR30 的掩码及大小端规则。
- CMYK8888 按 Qt `Q_UNREACHABLE()` 路径作为不支持的反色格式直接返回，避免伪造 RGB
  结果；Mono、MonoLSB、Indexed8 的索引反色保持颜色表不变。
- `xgui_regression_test.c:6285-6294` 更新 RGBA8888 原始填充后的反色和 RGB 交换期望，
  防止将颜色填充重载误当作原始像素重载。

验证结果：默认 `build` 的 `XGuiRegression_Test` 构建成功并通过，默认 CTest 1/1
通过；`build-crop-painter-off`（`XPAINTER_ON=0`）和 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）目标构建及仓库根目录回归均通过；ASan/LSan 回归功能通过，
仍报告既有 `101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、fontconfig
及 POSIX 截图后端，未发现本轮新增泄漏。构建输出仍有既有信号宏、类型转换、const
 丢弃和第三方预处理警告，因此不能宣称零警告或零泄漏。修改保留在工作树，未提交、
 未推送。

### 10.152 2026-08-28 QImageWriter 写入器选项与设备语义对齐

本轮对照 Qt 6.8 /home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp：

- :235-247 的默认状态：质量和压缩为 -1，优化/渐进式写入关闭，变换为 None，初始错误为
  UnknownError；
- :252-277 的 canWriteHelper()：设备未设置、设备按需以 WriteOnly 打开、可写性检查、处理器
  创建和 DeviceError/UnsupportedFormatError 错误语义；
- :334-347 的 setFormat() 仅更新格式；:362-371 的 setDevice() 更换设备并释放旧处理器；
  :389-408 的文件名设备语义；
- :477-504 的子类型和 SupportedSubTypes 查询；:517-587 的优化、渐进扫描和变换属性；
- :609-614 的 setText()：键和值分别 simplified()，多条记录以两个换行符拼接为
  Description；
- :622-638 的文件/设备 canWrite() 路径；:651-687 的空图像优先检查、选项能力判断、
  不支持变换时先变换图像、处理器写入和刷新；:700-733 的错误查询和 supportsOption()。

已完成修正：

- Src/XGui/Graphics/XImageWriter.c:167-206 负责处理器创建和文件名后缀格式解析；
  :208-253 仅在处理器报告支持时设置质量、压缩、描述、
  子类型、优化、渐进扫描和图像变换选项，避免将 Qt 中“格式不支持时忽略”的属性错误地
  传递给处理器。
- Src/XGui/Graphics/XImageWriter.c:377-392 释放处理器、内部文件设备及错误/格式对象；
  :411-428 初始化 Qt 默认参数和 `Unknown error` 文本；:430-468 提供设备/文件名构造器，
  文件名构造器立即创建可观察的内部文件设备；:470-518 保留 setFormat()/setDevice()/
  setFileName() 的处理器与设备生命周期语义。
- Src/XGui/Graphics/XImageWriter.c:551-595 实现子类型和 SupportedSubTypes 查询；
  :597-613 实现优化、渐进扫描和变换属性；:615-660 新增 Description 元数据累积，支持
  UTF-8 兼容重载、空白规范化和分配失败时保留旧值；Src/XGui/Graphics/XImageWriter.h:264-280
  补齐参数、返回/所有权和行为注释。
- Src/XGui/Graphics/XImageWriter.c:662-726 补齐设备按需打开、可写性、格式/扩展名检测、错误码
  及错误字符串；:728-813 调整空图像优先、处理器变换能力判断、处理器刷新和无插件时的
  内置编解码回退写入；:830-841 将 supportsOption() 接入实际处理器能力。
- xgui_regression_test.c:4500-4515 增加文件名写入器的内部设备、Unknown error 与后缀探测回归；
  :4517-4543 保留设备写入和 BMP 往返验证；:4953-4969 验证
  "  Title  "/"  Example\t" 等输入经 Qt 规则规范化为
  Title: Example\n\nAuthor: Alice 并传递给支持 Description 的写入器。

验证结果：cmake --build build --target XGuiRegression_Test -j1 成功；执行 ./bin/XGuiRegression_Test
时本轮 XImageWriter 相关断言均通过，当前默认回归输出为 `XGui regression tests passed`；默认
CTest 与已验证的裁剪配置保持通过。git diff --check 通过。
构建输出仍包含仓库既有信号函数指针、类型转换、const 丢弃和第三方预处理警告；本轮未宣称
零警告或零泄漏。

当前边界：XImageWriter_supportedSubTypes() 仅在处理器通过
`XImageIOHandlerOption_SupportedSubTypes` 并返回拥有型列表时提供结果；现有内置处理器
没有该能力，因此默认仍为空列表。初始 errorString 已初始化为 Qt 的 "Unknown error"，
但错误文本未接入翻译目录。ICC/插件之外的高级元数据也不在本轮范围。

### 10.153 2026-08-28 QImageReader 文件设备与处理器选项对齐

本轮继续对照 Qt 6.8 `QImageReader`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:471-555`
（私有状态、`initHandler()` 的设备打开和处理器创建）、`:570-604`
（空、设备、文件名构造器及内部设备所有权）、`:617-645`（格式查询）、
`:686-720`（自动探测和内容决策状态）、`:750-808`（设备/文件名切换）、
`:846-896`（尺寸、格式和文本查询）、`:909-1005`（裁剪与缩放属性）、
`:1010-1085`（变换与自动变换）、`:1110-1218`（`canRead()`/`read()` 主路径）、
`:1219-1282`（处理器选项能力判断及软件回退）、`:1400-1437`
（错误与 `supportsOption()`）、`:1498-1574`（分配限制和静态格式列表）。

已完成修正：

- `Src/XGui/Graphics/XImageReader.c:399-434` 增加内部文件设备的复用/释放辅助函数。
  `init_file()`、`setFileName()` 现在立即创建并保存拥有的 `XFile`，因此
  `device()` 与 `fileName()` 在读取前即可观察到与 Qt 相同的状态；重建处理器时只关闭
  设备，切换到外部设备或析构时才释放。`setDevice()` 保证旧内部设备不会泄漏，也不
  关闭调用方传入的设备。
- `Src/XGui/Graphics/XImageReader.c:436-473` 的处理器初始化先验证设备、按需以只读方式
  打开，再将格式和设备交给插件注册器；无插件或插件拒绝时保留设备状态，供内置编解码
  回退路径继续读取。`XImageReader_init()` 同时将初始错误文本设为空字符串，匹配 Qt
  默认 `QString` 状态。
- `Src/XGui/Graphics/XImagePluginRegistry.h/.c` 新增带自动探测/内容决策开关的
  `createReadHandlerEx()`；读取器将 `autoDetectImageFormat` 与
  `decideFormatFromContent` 原样传入，使关闭自动探测且未设置格式时不再按文件名后缀或
  数据内容猜测处理器。`xgui_regression_test.c:4880-4898` 覆盖带 `.bmp` 后缀但严格模式
  仍返回 `UnsupportedFormatError` 的 Qt 行为。
- `Src/XGui/Graphics/XImageReader.c:915-978` 透传
  `ImageTransformation`、`SubType`、`SupportedSubTypes` option；处理器不支持时分别
  返回 `TransformationNone`、空子类型和空列表。`supportedSubTypes()` 对处理器返回的
  列表执行深拷贝，调用方负责释放，与 Qt 值返回语义一致。
- `Src/XGui/Graphics/XImageReader.c:1000-1235` 将缩放尺寸的单维输入按原始尺寸保持宽高比，
  仅在处理器声明支持对应 option 时设置 `ScaledSize`、`ClipRect`、`ScaledClipRect`、
  `Quality`；按 `QRect::isNull()/isValid()` 与 `QSize::isValid()` 区分空矩形、无效负尺寸和
  有效零尺寸；不支持的步骤按 Qt 顺序提供软件回退，并避免对处理器已完成的步骤重复裁剪
  或缩放。成功读取不再清除之前的错误状态，保持 Qt `read()` 的错误字段语义。
- `Src/XGui/Graphics/XImageReader.c:83-91` 与 `Src/XGui/Graphics/XImagePluginRegistry.c:28-35`
  将 MIME 查询改为 Qt helper 的大小写敏感精确匹配；格式名本身仍按大小写不敏感规则
  规范化。`xgui_regression_test.c:4851-4900` 新增文件设备可观察性、文件名保持和默认
  错误文本断言，现有插件 Description/option 回归继续覆盖处理器透传。
- `Src/XGui/Graphics/XImagePluginRegistry.c:146-173,212-307` 对齐
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:173-249,258-337`
  的处理器优先级：显式注册的外部插件插入内置插件之前，同名格式先尝试外部
  `capabilities()/create()`，失败后再回退内置处理器；读写路径继续使用格式小写规范化，
  并在插件探测前后恢复非顺序设备位置。`xgui_regression_test.c:5060-5107` 新增同名
  `bmp` 外部插件覆盖内置 BMP 的回归，验证读出的尺寸及像素来自外部处理器。
- `Src/XGui/Graphics/XImagePluginRegistry.c:212-274` 进一步对齐 Qt 的
  `decideFormatFromContent` 分支：内容决策开启时跳过显式格式插件循环，向处理器创建
  传递空格式并按设备签名探测，因此未知显式格式不会阻断内容读取；
  `xgui_regression_test.c:4916-4940` 覆盖关闭自动探测、保留未知格式但仍按 BMP 内容成功
  读取的组合。
- `Src/XGui/Graphics/XImageReader.c:445-482,1330-1338` 对齐 Qt
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1260-1282`
  的高 DPI 文件名规则：成功读取文件基名末尾的 `@2x` 至 `@9x` 时设置图像设备像素比，
  并以函数内静态状态实现 `QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING` 的一次性禁用语义；
  `xgui_regression_test.c:4914-4930` 覆盖 `@2x` 读取结果。
- `Src/XGui/Graphics/XImageReader.c:674-681,1651-1675` 对齐处理器初始化失败状态：
  自动探测关闭且未指定格式时，`canRead()`/`supportsOption()` 现在设置
  `UnsupportedFormatError`；公开 `errorString()` 在无具体错误文本时返回 Qt 的
  `Unknown error`，而内部 const 访问仍保留空值对象。
- `xgui_regression_test.c:5390-5405,5440-5453` 覆盖严格模式的错误码以及默认错误文本。
- `Src/XGui/Graphics/XImageReader.c:1518-1549` 与
  `Src/XGui/Graphics/XImageWriter.c:852-882` 对齐 helper 的列表行为：
  `supportedMimeTypes()` 按字节序排序并去重，`imageFormatsForMimeType()` 的 MIME
  匹配保持 `QByteArray` 大小写敏感；格式名查询仍按大小写不敏感规则规范化。
  `xgui_regression_test.c:4801-4849` 增加 `IMAGE/PNG` 查询返回空列表断言。
- `Src/XGui/Graphics/XImageReader.c:426-451`、`Src/XGui/Graphics/XImageWriter.c:150-166,282-297`
  对齐 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/io/qfileinfo.cpp:910-918`
  的 `QFileInfo::suffix()`：先剥离 `/` 或 `\\` 前的目录部分，再取基名最后一个点号，
  避免父目录点号误判格式；隐藏文件 `.bmp` 仍按 Qt 规则得到 `bmp`。

### 10.154 2026-08-28 QImageIOHandler 默认选项语义

依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:390-432`，
`QImageIOHandler::write()`、`setOption()`、`option()` 与 `supportsOption()` 的基类默认
实现分别是失败、空操作、空值和 `false`；仅由具体处理器声明支持的选项才可被读取或设置。
`Src/XGui/Graphics/XImageIOHandler.c:60-80` 已移除基类对任意 `setOption()` 的隐式支持，
新增 `XImageIOHandler_storeOptionValue()` 供具体处理器显式保存已接受的值；内置处理器仅
显式保存 `Quality`，回归模拟处理器也显式转发 `Description`。这样未知选项不会因被设置过
而误报支持，同时不改变现有编码器选项透传。

验证结果：默认配置、`build-crop-painter-off`（`XPAINTER_ON=0`）和
`build-crop-stacked-off`（`XLAYOUT_STACKED_ON=0`）均在本轮 Ex 接口、内容决策、插件优先级、MIME
匹配及基名后缀改动后完成
`cmake --build ... --target XGuiRegression_Test -j1`，并执行 `./bin/XGuiRegression_Test`
通过；本轮修正 `@Nx` DPR 索引后默认、两个裁剪配置回归均再次通过，默认配置完整
`cmake --build build -j1` 的库、静态/动态示例和测试目标均成功；默认配置
`ctest --test-dir build --output-on-failure` 为 1/1 通过，`git diff --check` 通过。
默认构建仍保留仓库既有 `XSignal` 非兼容函数指针、事件析构指针类型、const
丢弃及第三方预处理警告，不能宣称零警告。受控 LSan 基线仍为
`101618 byte(s) leaked in 456 allocation(s)`，来源为 Mesa、fontconfig 与 POSIX
截图后端，未发现本轮新增泄漏，不能宣称零泄漏；本轮 `build-asan` 回归也在同一
基线下通过。所有修改保留在当前工作树，未提交、未推送。

当前边界：`transformation()` 已透传处理器元数据，但项目尚未提供 Qt
`qt_imageTransform()` 的完整 EXIF 方向变换；处理器原生增量读取以及无文件后缀时遍历全部
扩展名仍未完全实现。内置处理器目前只声明 Quality
能力，因此裁剪、缩放主要走软件回退；插件可通过 option 虚函数启用对应的原生路径。

### 10.155 2026-08-28 XStackedLayout 演示按钮信号槽联动

依据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qstackedlayout.cpp:66-90,262-320`
中关于 `QStackedLayout` 不提供内置切页控件、通过外部控件调用
`setCurrentIndex()` 且发射 `currentChanged()` 的约定，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:825-840`
和同文件 `1184-1195` 的 `clicked` 发射语义，修正 `xgui_window_demo.c` 的可视化链路：

- `DemoWin` 在堆叠布局场景增加 `m_stackPrevButton` 与 `m_stackNextButton`，分别放置在
  页面矩形下方；绘制路径使用 `XPushButton_drawContents()`，不引入平台绘图 API。
- 两个按钮通过 `XObject_connect_1()` 连接 `XPushButton_clicked_signal(NULL, false)`，槽函数
  `demo_stack_prev_clickedSlot()` / `demo_stack_next_clickedSlot()` 读取当前索引与页面数，
  循环调用 `XStackedLayout_setCurrentIndex()`，再触发窗口重绘。这样切页由真实的
  `clicked -> slot -> setCurrentIndex` 信号槽链路完成，而不是直接改布局成员。
- 窗口鼠标按下/释放事件补充两个按钮的命中、按下状态、`pressed`、`released` 与
  `clicked` 信号；释放到按钮外时只发 `released`，保持 QAbstractButton 的命中约定。
  销毁路径显式释放两个按钮，且所有新增字段继续受 `XWIDGET_ON`、`XPUSHBUTTON_ON`、
  `XLAYOUT_STACKED_ON` 条件编译保护。

验证结果：默认配置按顺序重建 `XGuiWindowDemo_Test` 并执行
`timeout 4s ./bin/XGuiWindowDemo_Test 1`，X11 原生窗口创建、事件循环和自动退出均通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）与 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）的演示目标也分别完成构建。默认
`XGuiRegression_Test` 新增 clicked 槽切页断言，两个裁剪回归目标及 CTest 在前一轮处理器修正后继续通过，
`git diff --check` 通过。构建保留仓库既有第三方/事件函数指针警告和
Mesa/fontconfig/POSIX 截图后端 LSan 基线泄漏，不能宣称零警告或零泄漏；修改未提交、未推送。

### 10.156 2026-08-28 XPainter 圆弧正角度方向对齐

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:4139-4143`
对 `drawPie()` 的角度约定（0 度位于 3 点钟、正跨度逆时针、负跨度顺时针），以及同文件
`:4225-4245` 中 `drawChord()` 复用 `QPainterPath::arcTo()` 的方向语义，修正
`Src/XGui/Graphics/XPainter.c:2251-2277` 的 `painterArcPoints()`：在设备坐标 Y 轴向下时
对正弦项取反，使正角度从右侧端点经过上方，而不是错误地经过下方。该采样点函数同时供
`drawArc()`、`drawPie()` 和 `drawChord()` 使用，因此三类 API 的正负跨度方向保持一致；
高层 `m_drawShape` 回调仍收到原始 1/16 度参数，不改变便携录制格式。

`xgui_regression_test.c:2390-2400` 新增四分之一圆弧断言：`0..90` 度必须命中
`(20,5)` 与 `(10,0)`，且不命中 `(10,10)`，从而区分屏幕坐标上下方向；既有整圆、扇形、
弦形和回调参数测试继续保留。

验证结果：`cmake --build build --target XGuiRegression_Test -j1` 与
`./bin/XGuiRegression_Test` 通过。随后默认完整构建、`build-crop-painter-off`
（`XPAINTER_ON=0`）和 `build-crop-stacked-off`（`XLAYOUT_STACKED_ON=0`）的回归目标、
默认 CTest 及 `git diff --check` 均完成并通过。构建仍保留既有 `XSignal` 非兼容函数指针、
事件析构指针类型、const 丢弃和第三方预处理警告，不能宣称零警告；受控 LSan 仍为
Mesa/fontconfig/POSIX 截图后端的 `101618 byte(s) leaked in 456 allocation(s)` 基线，
未发现本轮新增泄漏，不能宣称零泄漏。当前边界：无高层形状回调时仍使用固定 32 段弧线折线
（椭圆/整圆填充使用 64 段），与 Qt 的解析路径在极小半径或极大跨度下存在像素级近似；修改
未提交、未推送。

### 10.157 2026-08-28 XPainter 圆角矩形退化顺序对齐

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3895-3918`，
`QPainter::drawRoundedRect()` 在 `xRadius <= 0` 或 `yRadius <= 0` 时必须先调用普通
`drawRect()`，只有正半径才进入扩展绘图引擎或路径构造。此前 `XPainter_drawRoundedRect()`
先检查 `m_drawShape`，导致便携高层回调在零半径输入下错误收到 RoundedRect opcode。

实现范围：

- `Src/XGui/Graphics/XPainter.c:3568-3582` 将半径非正判断移到 `m_drawShape` 分派之前，
  复用现有 `XPainter_drawRect()`（含负尺寸归一化、画笔/画刷和裁剪状态），保持 Qt 的
  退化语义；正半径仍向高层回调传递原始半径，栅格回退继续按尺寸上限裁剪。
- `xgui_regression_test.c:2500-2510` 在形状回调契约中增加零半径测试，验证返回成功且
  RoundedRect 回调计数保持为零；原有正半径参数、矩形归一化和零跨度测试继续覆盖。

验证结果：默认 `cmake --build build --target XGuiRegression_Test -j1`、
`./bin/XGuiRegression_Test`、默认完整构建及 `ctest --test-dir build --output-on-failure`
均通过；`git diff --check` 通过。此前已验证的 `build-crop-painter-off` 与
`build-crop-stacked-off` 回归目标仍通过，本轮未改变裁剪宏边界。构建输出保留仓库既有
`XSignal` 非兼容函数指针、事件析构类型、const 丢弃和第三方预处理警告，不能宣称零警告；
受控 LSan 仍为 Mesa/fontconfig/POSIX 截图后端的 `101618 byte(s) leaked in 456 allocation(s)`
基线，未发现本轮新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：正半径路径仍使用固定六段圆弧采样并在无高层回调时以折线近似，未实现 Qt
`Qt::SizeMode` 百分比半径重载；该项目 API 仅保留整数绝对半径版本。

### 10.158 2026-08-28 QIconEngine 缩放钩子非有限值边界对齐

本轮继续核对 Qt 6.8 `QIconEngine::scaledPixmap()` 与 `QSize::operator*(qreal)`：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-251,296-304`
规定缩放钩子将逻辑尺寸乘以比例后再调用普通 `pixmap()`；
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/tools/qsize.h:70-75,174-181`
规定结果通过 `qRound` 写入整数尺寸。C 版此前对 NaN/无穷比例直接转换为 `int`，
该转换在 C 标准中没有定义，且可能绕过 Qt 的空尺寸结果。

实现范围：

- `Src/XGui/Icon/XIconEngine.c` 在 `ScaledPixmapHook` 中先用 `!(scale > 0.0f)`
  和 `isfinite(scale)` 拒绝非正、NaN、正负无穷；再以 `double` 计算物理宽高，
  在四舍五入前检查 `INT_MAX` 上界，避免大比例整数溢出。有效比例仍按 Qt 的
  `qRound(width * scale)` 语义生成像素图并设置 DPR。
- `xgui_regression_test.c` 的 `test_icon_engine_hook_contract` 新增 NaN 与无穷比例
  断言；原有非正比例和有效 2.0 比例测试保持不变。

验证结果：默认 `XGuiRegression_Test` 目标、完整构建、回归、CTest 与
`git diff --check` 均通过；新增 NaN/无穷断言通过。构建输出仍有仓库既有的
`XSignal` 函数指针、事件析构指针、const 丢弃和第三方预处理警告，不能宣称零警告；
受控 LSan 基线仍为 Mesa/fontconfig/POSIX 截图后端的 `101618 byte(s) leaked in
456 allocation(s)`，未发现本轮新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：Qt 的 `qRound` 对极端有限值的实现细节在 C99 中没有直接等价物；本实现对
超过 `INT_MAX` 的缩放请求保守返回空图，避免未定义行为，属于嵌入式安全裁剪。

### 10.159 2026-08-28 QIcon 高 DPI 正无穷比例安全边界

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:911-930`，
`QIcon::pixmap(size, devicePixelRatio, ...)` 对大于 1 的设备像素比进入引擎的
`scaledPixmap()` 路径；同文件 `:149-164` 的 `pixmapDevicePixelRatio()` 继续按目标
物理尺寸计算返回 DPR。Qt 的 `QSize::operator*(qreal)` 最终使用
`qRound`，而 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnumeric.h:353-379`
明确说明超出整数范围的输入行为未定义。此前 C 版 `XIconPrivate_scaledPixmap()`
对正无穷比例执行浮点乘法并转换为 `int`，可能触发未定义行为或产生不可分配的尺寸。

实现范围：

- `Src/XGui/Icon/XIcon.c:XIconPrivate_scaledPixmap()` 现在引入 `<math.h>`，在尺寸
  运算前用 `isfinite(devicePixelRatio)` 拒绝 NaN 和正负无穷。公开
  `XIcon_pixmapRatio()` 仍先把非正比例归一化为 1x，并且 NaN 由于不满足 `> 1.0`
  继续走普通 1x 路径；只有会进入高 DPI缩放的正无穷被安全地返回空图。
- `xgui_regression_test.c:test_icon_device_pixel_ratio()` 新增正无穷 DPR 请求断言，
  验证输出保持空像素图；已有 NaN、零、亚 1 和有效 2x 路径断言继续覆盖 Qt 的
  分支语义。

验证结果：本轮完成默认 `XGuiRegression_Test` 目标构建、默认完整构建、回归执行、
`ctest --test-dir build --output-on-failure` 及 `git diff --check`；新增正无穷断言通过。
构建仍保留仓库既有 `XSignal` 非兼容函数指针、事件析构指针、const 丢弃及第三方
预处理警告，不能宣称零警告。受控 LSan 基线仍为 Mesa/fontconfig/POSIX 截图后端的
`101618 byte(s) leaked in 456 allocation(s)`，未发现本轮新增泄漏，不能宣称零泄漏；
修改未提交、未推送。

当前边界：对超过 `INT_MAX` 的极大但有限比例，缩放 helper 仍按现有上限检查返回空图，
这是针对 C99 整数转换未定义行为的嵌入式安全裁剪，与 Qt 在该未定义输入下的具体结果不作
等价承诺。

### 10.160 2026-08-28 QIcon paint 状态保存/恢复配对

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1017-1050`，
`QIcon::paint()` 只负责把对齐后的矩形交给图标引擎绘制；项目的便携回退路径在调用
`XPainter` 图像回调时可选地保存绘制状态，但 `save()` 成功后才允许对应的 `restore()`。
此前 `XIcon_paint()` 把 `saved` 初值设为 true，导致自定义绘制器只提供 `m_restore` 而
未提供 `m_save` 时仍会错误弹出状态栈。

实现范围：

- `Src/XGui/Icon/XIcon.c:XIcon_paint()` 将 `saved` 初值改为 false，并把
  `m_drawImage` 可用性检查移到自定义引擎分支之后。这样引擎路径不再被无关的图像回调
  约束，软件回退路径仍只在真实 `m_drawImage` 存在时绘制；`m_restore` 仅在
  `m_save` 返回成功后调用。
- `xgui_regression_test.c:test_icon_paint_visual_alignment()` 新增仅有 restore 回调的
  负向契约：图标像素仍绘制，但 restore 计数必须保持为零；原有 LTR/RTL 视觉对齐断言
  继续覆盖。

验证结果：本节修改后需执行默认与裁剪回归、CTest、差异检查及 ASan/LSan；构建中保留
仓库已有第三方和函数指针警告，LSan 仍以 Mesa/fontconfig/POSIX 截图路径的既有泄漏
基线为准，不宣称零警告或零泄漏。修改未提交、未推送。

当前边界：`XPainterSaveProc` 的 bool 失败语义是项目便携扩展，Qt `QPainter::save()` 本身
无返回值；当自定义回调拒绝保存时，本实现只保证不调用 restore，不模拟 Qt 内部状态栈的
具体错误报告。

### 10.161 2026-08-28 XIconThemeEngine 非有限缩放比例安全边界

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:969-974`，
主题引擎的 `scaledPixmap()` 先用 `qCeil(scale)` 参与目录选择，再把比例传给具体条目；
`qCeil()` 的实现见 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/kernel/qmath.h:36-43`，
最终通过 `int(ceil(v))` 取得整数缩放因子。C99 对 NaN、无穷及超出 `int` 范围的浮点转整型
没有定义结果，因此主题引擎必须在进入尺寸和缓存键计算前拒绝这些输入，避免与默认
`XIconEngine` 路径不一致。

实现范围：

- `Src/XGui/Icon/XIconThemeEngine.c:229-256` 引入 `isfinite()` 与 `INT_MAX` 检查；
  `scale <= 0`、NaN、正负无穷均返回空像素图，有限比例在乘以较小请求边后超过
  `INT_MAX` 时同样保守返回空图。
- 缓存键中的千倍 DPR 转换改用 `double` 计算并在 `INT_MAX` 处饱和，避免极大但仍可
  分配的有限比例在键生成时再次发生未定义转换；正常比例仍保持原有四舍五入结果。
- `xgui_regression_test.c:437-469` 新增 NaN 与无穷比例断言，继续覆盖原有非正比例、
  物理尺寸和 DPR 行为。

验证结果：默认 `build` 的 `XGuiRegression_Test` 目标、完整构建和回归均通过；
`build-crop-painter-off`（`XPAINTER_ON=0`）与 `build-crop-stacked-off`
（`XLAYOUT_STACKED_ON=0`）的回归目标和执行均通过；默认 `ctest --test-dir build
--output-on-failure` 为 1/1 通过，`git diff --check` 通过。构建输出仍保留仓库既有的
`XSignal` 非兼容函数指针、事件析构指针、const 丢弃及第三方预处理警告，不能宣称零警告。
受控 LSan 仍为 Mesa/fontconfig/POSIX 截图后端的 `101618 byte(s) leaked in 456 allocation(s)`
基线，未发现本轮新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：主题引擎仍未实现 Qt 的 `icon-theme.cache` 二进制缓存及按目录 `Scale` 元数据
参数化匹配；对超过 `INT_MAX` 的缩放请求采取嵌入式安全裁剪，不承诺复现 Qt 未定义输入
下的具体整数结果。

### 10.162 2026-08-28 图像后缀插件 canRead 回退与文件预读定位

本轮依据 Qt 6.8：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:295-317`
规定文件后缀命中处理器后必须调用 `handler->canRead()`，失败时销毁处理器并继续按
内容探测；`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/io/qiodevice.cpp:953-963`
规定非顺序设备的 `bytesAvailable()` 使用逻辑位置（包含设备内部缓冲的影响），而不是
简单读取底层文件描述符位置。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:539-571` 在自动探测且存在文件后缀时，对注册表返回
  的后缀处理器调用 `canRead()`；保存并恢复非顺序设备位置，失败后销毁处理器并用空格式
  重新进入插件/内置内容探测流程，保持 Qt 的错误后缀回退顺序。
- `Src/XCode/XFile/XFileDevice/XFileDevice.c:114-136` 使 `seek()` 成功后清空当前读通道
  的预读环形缓冲。`peek()` 会先移动底层文件偏移再缓存前缀，若 seek 后保留旧缓存，下一次
  `readAll()` 会重复前缀并导致 BMP/PNG 解码失败；清空缓存后与 Qt 的 seek 失效缓冲语义一致。
  `bytesAvailable()` 保持使用已经扣除缓存的逻辑 `pos()`，避免重复计算预读字节。
- `xgui_regression_test.c:4780-4798,5316-5363` 增加仅在后缀格式声明能力、但处理器
  `canRead()` 拒绝内容的外部 BMP 插件夹具，验证读取器最终回退到内置 BMP 处理器并保持像素。

验证结果：默认完整构建及 `XGuiRegression_Test` 执行通过；`XPAINTER_ON=0` 与
`XLAYOUT_STACKED_ON=0` 裁剪目标构建和回归均通过；自动探测 BMP、`@2x` DPR、错误后缀
PNG、新增外部插件回退断言及 CTest（1/1）均通过；`git diff --check` 通过。
构建输出保留仓库既有的 `XSignal` 非兼容函数指针、事件析构指针、const 丢弃和第三方
预处理警告，不能宣称零警告。受控 LSan 报告 Mesa/fontconfig/POSIX 截图后端既有泄漏
`101618 byte(s) leaked in 456 allocation(s)`，未发现本轮新增泄漏，不能宣称零泄漏。
修改未提交、未推送。

当前边界：读取器仍依赖 `XImageIOPlugin::capabilities()` 对空格式内容探测的插件契约；
不具备该契约、且后缀处理器 `canRead()` 与内容均不匹配的第三方插件会按 Qt 一样被视为
插件自身实现错误，不在 C 版注册表中添加额外格式启发式。

### 10.163 2026-08-28 后缀插件拒绝后的内容探测跳过规则

本轮依据 Qt 6.8：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:202-219`
记录后缀命中的插件索引 `suffixPluginIndex`，并在后续格式探测循环中跳过该索引；
`qimagereader.cpp:318-337` 要求后缀处理器 `canRead()` 失败后重新按空格式探测插件，
但不能再次调用刚拒绝的外部处理器，随后仍继续内置处理器探测。

实现范围：

- `Src/XGui/Graphics/XImagePluginRegistry.h:61-75` 新增
  `XImagePluginRegistry_createReadHandlerContentFallback()`，明确传入被拒绝后缀并
  保持调用方不转移插件所有权。
- `Src/XGui/Graphics/XImagePluginRegistry.c:282-324` 新增内容回退实现：只跳过注册顺序中
  首个匹配后缀的外部插件，保留内置插件参与空格式内容识别；外部插件创建时继续携带
  规范化后缀，所有非顺序设备探测前后恢复逻辑位置。
- `Src/XGui/Graphics/XImageReader.c:562-571` 在后缀处理器 `canRead()` 失败后改用上述
  专用入口，避免再次把同一外部插件当作内容候选。
- `xgui_regression_test.c:4631-4652,5342-5358` 增加外部 BMP 插件同时声明后缀能力、
  `canRead()` 拒绝内容且空格式能力关闭的夹具，断言内置 BMP 处理器仍能读取像素。

验证结果：默认完整构建及 `XGuiRegression_Test` 执行通过；默认 CTest（1/1）通过；
此前的 `XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归保持通过；后缀插件优先级、
`canRead()` 失败回退、非顺序设备位置保护及错误后缀 PNG 均通过；`git diff --check`
通过。构建输出保留仓库既有的 `XSignal` 非兼容函数指针、事件析构指针、const 丢弃和
第三方预处理警告，不能宣称零警告。受控 LSan 报告 Mesa/fontconfig/POSIX 截图后端既有
泄漏 `101618 byte(s) leaked in 456 allocation(s)`，未发现本轮新增泄漏，不能宣称零泄漏。
修改未提交、未推送。

当前边界：固定容量注册表仍不提供 Qt `QFactoryLoader` 的目录扫描、动态库加载和卸载；
内容回退仅跳过首个同后缀外部插件，若多个外部插件共享后缀，后续插件仍按注册顺序参与
内容探测，与 Qt 的多键映射顺序一致。

### 10.164 2026-08-28 QImageReader 文件名默认扩展名探测

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:497-546`：
拥有文件设备的读取器在原始路径打开失败且启用自动探测时，按支持格式尝试追加扩展名；
显式格式对应的扩展名优先，成功后 `fileName()` 返回实际打开的候选路径，全部失败时恢复
原始名称并报告 `FileNotFoundError`。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:507-577` 新增
  `XImageReader_tryDefaultExtensions()`，复用排序后的 `supportedFormats()` 列表，按 Qt
  的显式格式优先规则创建候选路径，调用 `XFile_setFileName()`/只读打开；成功时同步读取器
  的内部文件名，失败时恢复原名称。临时路径和列表均使用 XString/XStringList 项目内存接口。
- `Src/XGui/Graphics/XImageReader.c:605-624` 在拥有文件设备首次打开失败后调用扩展名回退，
  失败错误从通用 `DeviceError` 区分为 `FileNotFoundError`；成功后重新从实际候选路径提取
  后缀，使无显式格式的自动探测仍保持后缀插件优先级。
- `xgui_regression_test.c:5097-5134` 增加 BMP 默认扩展名夹具，分别覆盖显式 `bmp` 优先、
  无显式格式自动探测、像素读取和实际文件名暴露，并清理候选文件。

验证结果：默认 `XGuiRegression_Test` 目标构建及执行通过；默认完整构建、CTest（1/1）和
`git diff --check` 通过；此前 `XPAINTER_ON=0`、`XLAYOUT_STACKED_ON=0` 裁剪回归保持通过。
构建输出继续保留仓库既有的 `XSignal` 非兼容函数指针、事件析构指针、const 丢弃及第三方
预处理警告，不能宣称零警告。受控 LSan 的 Mesa/fontconfig/POSIX 截图后端既有泄漏基线为
`101618 byte(s) leaked in 456 allocation(s)`，本轮未发现新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：扩展名候选仍来自固定容量注册表和内置编解码器，不包含 Qt `QFactoryLoader` 的
动态目录扫描；资源错误等平台文件错误在 C 接口中统一映射为 `FileNotFoundError`，未暴露
Qt `QFileDevice::ResourceError` 细分。

### 10.165 2026-08-28 BMP 32 位 Alpha 掩码语义与无损编码

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:190-205`
（位深、平面数、压缩类型的合法性检查）、`:244-275`（V4/V5 Alpha 掩码读取以及
`BI_RGB` 32 位仅在明确 Alpha 掩码时启用透明通道）、`:331-365`（普通 32 位/16 位
像素掩码和不透明默认值）以及 `:585-669`（BMP 写入器对 32 位图像采用 24 位
`BI_RGB` 写出并丢弃 Alpha）。

已完成修正：

- `Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c` 的 BMP 解码器记录 V4/V5 头的
  `biAlphaMask`；普通 `BI_RGB` 32 位数据只有在掩码为 `0xff000000` 时读取最高字节，
  否则将 Alpha 固定为 255，与 Qt `QBmpHandler::read_dib_body()` 一致。新增的
  `test_codec_bmp_alpha_semantics()` 夹具覆盖普通 40 字节 INFOHEADER 的垃圾 Alpha
  被忽略，以及 V4 头显式掩码被采用。
- 为保留 XinYueC 现有 `XImageCodec` 像素级无损往返契约，带 Alpha 的编码路径改为
  输出标准 108 字节 V4 `BITFIELDS` 头（RGB 掩码 `00ff0000/0000ff00/000000ff`、
  Alpha 掩码 `ff000000`）；无 Alpha 图像仍输出紧凑 40 字节 `BI_RGB`。该扩展仍是
  Qt 可读的 BMP，只在“写出带 Alpha 是否保留透明度”这一点上有意不同于 Qt 的
  传统 24 位写出策略。
- `xgui_regression_test.c` 将 Alpha 语义夹具接入 BMP 回归组，原有 BMP/PNG/SVG
  像素级往返断言保持不变。

验证结果：默认 `XGuiRegression_Test` 目标重新编译并执行通过，输出
`XGui regression tests passed`；此前默认完整构建、CTest（1/1）、`XPAINTER_ON=0`
和 `XLAYOUT_STACKED_ON=0` 裁剪回归均通过，本轮公共 BMP 编码改动后目标增量构建及
回归再次通过；`git diff --check` 通过。构建输出继续保留仓库既有的 `XSignal` 非兼容
函数指针、事件析构指针、const 丢弃和第三方预处理警告，不能宣称零警告。受控 LSan
既有基线仍为 `101618 byte(s) leaked in 456 allocation(s)`（Mesa/fontconfig/POSIX
截图后端），本轮未发现新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：BMP 编码仍不写入 Qt 的 DPI 元数据和调色板索引深度优化；Alpha 图像使用
V4 `BITFIELDS` 以保留透明度，若调用方要求与 Qt `QImageWriter` 完全字节级一致，需要
另行提供“兼容 Qt 丢弃 Alpha”的写出模式。
### 10.166 2026-08-28 QIcon 主题目录 Scale 精确匹配

依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:794-815`
的 `directoryMatchesSize()` 与 `:821-847` 的 `directorySizeDistance()`，以及
`:849-873` 的 `entryForSize()` 两阶段选择规则：请求尺寸先取逻辑宽高中较小值，先
要求目录 `Scale == qCeil(scale)` 并匹配 Fixed/Scalable/Threshold 范围；没有精确项时
才按 `iconsize * iconscale` 与目录物理尺寸的距离选择首个最小值。`:969-974` 的
`scaledPixmap()` 明确以 `qCeil(scale)` 选择目录，再把原始比例交给条目缩放。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:576-633` 新增目录精确匹配与物理距离计算，
  `theme_tryParsedTheme()`（`:669-724`）改为精确两阶段扫描并保持首个最小距离，
  递归父主题和短横线回退继续传递整数目录倍率。普通 API 以倍率 1 保持原有行为。
- `Src/XGui/Icon/XIconThemeInternal.h:17-24` 新增
  `XIconInternal_resolveThemePixmapSizeScale()`，区分逻辑选取尺寸、`qCeil(scale)`
  目录倍率和最终物理输出尺寸；`XIconThemeEngine.c:20-29,232-273` 的主题引擎在
  `scaledPixmap()` 中使用 `ceil(scale)` 选目录，仍以浮点比例计算物理尺寸并保留 DPR。
- `xgui_regression_test.c:575-658` 增加双目录夹具：`36x36` 的 `Scale=1` 使用红色、
  `24x24` 的 `Scale=2` 使用蓝色，验证 24px@1.5x 输出 36px 时选择 2x 资源而不是
  物理距离相同的 1x 资源，并检查尺寸和像素内容。

验证结果：默认 `build` 全量构建、`XGuiRegression_Test`、CTest 1/1 和
`git diff --check` 通过；`build-crop-painter-off`（`XPAINTER_ON=0`）及
`build-crop-stacked-off`（`XLAYOUT_STACKED_ON=0`）逐个重建并运行回归通过。
`build-asan` 的受控 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` 回归通过，LSan
仍报告既有 Mesa/fontconfig/POSIX 截图后端基线 `101618 byte(s) leaked in 456
allocation(s)`，未发现本轮新增泄漏，不能宣称零泄漏。构建保留仓库既有函数指针、
const 丢弃和第三方预处理警告，不能宣称零警告；修改未提交、未推送。

当前边界：仍未实现 Qt 的 `icon-theme.cache` 二进制缓存和平台主题插件工厂；没有
`index.theme` 时的传统目录回退无法获得目录 `Scale` 元数据，只能使用倍率 1 的
文件名距离规则。对超出 C99 `int` 范围的极端倍率仍按现有安全检查返回空图。

### 10.167 2026-08-28 QImageReader allocationLimit 环境覆盖

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1541-1550`
和 `:1571-1575`，以及 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qtenvironmentvariables.cpp:194-225`
、`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/text/qlocale_tools.cpp:373-469`：
`allocationLimit()` 首次查询时读取
`QT_IMAGEIO_MAXALLOC`，合法的非负整数覆盖运行时限制；非法、负值（除 -0）或溢出值回退到
`setAllocationLimit()` 保存的值，setter 对负值不产生变化，零值关闭分配检查。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:22-109` 增加惰性环境解析，按 Qt
  `qstrntoll()` 的 ASCII 空白、base=0、`0x`/`0b` 前缀、13 字节长度上限和
  `INT_MAX` 溢出规则校验；环境值一经解析即保持 Qt 的进程生命周期缓存语义。
  `XImageReader_allocationLimit()` 改用有效限制查询。
- `Src/XGui/Graphics/XImageReader.c:1305-1319` 的图像读取分配检查改用同一有效限制，
  因而环境覆盖不仅影响 getter，也会拒绝超过限制的图像；setter、零值禁用和负值忽略
  行为保持不变。
- `xgui_regression_test.c:5255-5281` 增加带 `QT_IMAGEIO_MAXALLOC=7`、`0x7`、`0b111`、
  首尾空白或显式正号运行时的条件断言，与原有 setter 回归共存，不修改测试进程环境。

验证结果：默认全量构建、`XGuiRegression_Test`、CTest（1/1）和 `git diff --check`
通过；`QT_IMAGEIO_MAXALLOC=7`、`0x7`、`0b111`、` 7 `、`+7`、` +7 ` 等环境覆盖回归通过；此前
`XPAINTER_ON=0`、`XLAYOUT_STACKED_ON=0` 裁剪回归保持通过。构建输出继续保留仓库既有
函数指针、const 丢弃和第三方预处理警告，不能宣称零警告；LSan 仍为 Mesa/fontconfig/
POSIX 截图后端既有泄漏基线 `101618 byte(s) leaked in 456 allocation(s)`，未发现本轮
新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：环境变量只在首次读取时解析，与 Qt 的静态缓存一致；尚未把分配预算细化到
各处理器内部的临时缓冲，仅在读取前按最小 32 位像素预算执行统一上限检查。

### 10.168 2026-08-28 QIcon icon-theme.cache 二进制缓存查找

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:213-282`
的 `QIconCacheGtkReader` 构造、版本和目录时间戳校验，以及 `:284-345` 的大端
哈希桶、链表和目录索引查找；并结合 `:480-502` 的“有效缓存缩小待探测目录，
损坏缓存恢复完整扫描”语义。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:117-343` 增加 `icon-theme.cache` 读取器：
  使用项目 `XFile`/`XByteArray` 接口读取文件，按 Qt 的版本 1、网络字节序和
  `icon_name_hash` 规则解析；对偏移对齐、长度溢出、未结束字符串、哈希链循环
  和目录索引越界统一拒绝，并比较缓存与所有目录的修改时间，过期/损坏/不可读
  时返回未知状态。
- `Src/XGui/Icon/XIconThemeInternal.c:866-887,1185-1195` 在主题元数据目录和
  可用尺寸探测中应用缓存过滤；缓存确认图标不在当前目录时跳过扩展名探测，
  缓存未知时保留原有完整目录扫描，因此缓存不会改变损坏或缺失文件的回退行为。
- `xgui_regression_test.c:193-307,320-435` 增加最小 GTK 缓存二进制夹具，覆盖有效
  命中、有效缓存排除目录和损坏缓存回退；夹具仍在测试结束时随主题临时目录清理。

验证结果：`cmake --build build --target XGuiRegression_Test -j1` 和默认
`./bin/XGuiRegression_Test` 通过；新增缓存夹具分别确认 valid cache 过滤目录、
corrupt cache 回退完整扫描。构建输出只保留仓库既有函数指针、const 丢弃和第三方
预处理警告，不能宣称零警告；本轮未引入新的内存分配，LSan 仍受 Mesa/fontconfig/
POSIX 截图后端既有基线 `101618 byte(s) leaked in 456 allocation(s)` 影响，不能
宣称零泄漏。修改未提交、未推送。

当前边界：缓存只实现 Qt 当前读取所需的 GTK cache 版本 1 目录索引，不生成或更新
缓存文件，也不接入 Qt 平台主题插件工厂；无 `index.theme` 的传统目录路径继续按
现有静态目录探测。`XFileInfo` 的可移植时间接口目前以秒为粒度，因此同一秒内的
目录修改不会像 Qt 的高精度 `QDateTime` 比较一样立即使缓存失效。

### 10.169 2026-08-28 BMP 头部深度、调色板与 Mono 极性对齐

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:188-208`
（信息头仅接受 1/4/8/16/24/32 位、平面数为 1，压缩类型不超过
`BMP_BITFIELDS` 且按位深组合校验）、`:244-275`（按 `biClrUsed` 或默认
`1 << nbits` 读取调色板、1 位图使用 `Format_Mono`、颜色表亮度顺序反转时交换
像素位与颜色表）以及 `:331-365`（16/24/32 位通道读取和掩码缩放）。

实现范围：

- `Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:145-247` 读取 Windows DIB 的
  `biClrUsed`，调色板数量限制为 1..256；移除 Qt 不接受的 2 位深度和
  `BI_ALPHABITFIELDS`（压缩值 6）路径，保留 1/4/8/16/24/32 位及已配置的
  RLE/BITFIELDS 裁剪分支。
- `Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:300-372` 将 1 位输入建模为
  `XImageFormat_Mono`，复制颜色表并按 Qt `swapPixel01` 规则对亮度倒序的两项
  调色板翻转位序；其它索引格式仍保留 XinYueC 现有 Indexed8/ARGB32 扩展语义。
- `xgui_regression_test.c:5800-5910` 增加 2 位深度与压缩值 6 的拒绝夹具、1 位
  Mono 位序/极性夹具以及 `biClrUsed=1` 的缩减调色板夹具；扩展资产中的旧 2 位
  示例改为明确验证 Qt 拒绝，1 位资产期望格式改为 Mono。

验证结果：默认 `build` 的 `XGuiRegression_Test` 目标重建并执行通过，默认完整构建、
CTest（1/1）和 `git diff --check` 通过；此前 `XPAINTER_ON=0`、`XLAYOUT_STACKED_ON=0`
裁剪配置保持通过。构建输出仍有仓库既有的 `XSignal` 函数指针、事件析构指针、
const 丢弃及第三方预处理警告，不能宣称零警告；受控 LSan 的 Mesa/fontconfig/POSIX
截图后端既有基线为 `101618 byte(s) leaked in 456 allocation(s)`，本轮未发现新增泄漏，
不能宣称零泄漏。修改未提交、未推送。

当前边界：Qt 的 1 位 BMP 还会在颜色表为两项且亮度倒序时交换位序，本实现已对齐；
调色板索引深度仍使用 `XImage` 的便携存储布局，未实现 Qt 对 BMP DPI 元数据的写出，
V5 ICC 配置文件及 DIB 无文件头的独立起始位置规则也仍未覆盖。

### 10.170 2026-08-28 QImage 文本键列表公开接口

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:4179-4192`：
`QImage::textKeys()` 对空图像返回空 `QStringList`，对有效图像返回文本元数据键的
独立列表；键来自内部有序映射，因此按升序排列，调用方可安全修改返回列表而不改变
图像自身的元数据。

实现范围：

- `Src/XGui/Graphics/XImage.h:716-722` 新增 `XImage_textKeys()` 声明及中文所有权说明，
  返回 `XStringList*`，调用方负责 `XStringList_delete_base()`。
- `Src/XGui/Graphics/XImage.c:2437-2442` 复用 `XStringList_create_copy()` 深复制内部
  升序键列表；空图像走 `XStringList_create()` 返回空列表，保持 Qt 空值语义和 C 接口
  的可释放返回值约定。
- `xgui_regression_test.c:7092-7111` 新增排序键、返回列表修改后的源图像不变、空图像
  返回空列表回归。

验证结果：`cmake --build build --target XGuiRegression_Test -j1` 和
`./bin/XGuiRegression_Test` 通过；当前共享工作树的默认全量构建、CTest 与两个裁剪
配置此前均通过，`git diff --check` 通过。构建继续保留仓库既有的函数指针、const
丢弃和第三方预处理警告，不能宣称零警告；LSan 仍报告 Mesa/fontconfig/POSIX 截图后端
既有泄漏基线 `101618 byte(s) leaked in 456 allocation(s)`，本功能未引入新增泄漏，
不能宣称零泄漏。修改未提交、未推送。

当前边界：XStringList 是 C 接口深复制列表，不提供 Qt `QStringList` 的隐式共享对象
语义；图像文本值仍通过既有 `XImage_text()`/`XImage_text_2()` 获取，未增加批量值映射
接口。

### 10.171 2026-08-28 QImage 文本值缺失键返回语义

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:4186-4209`：
`QImage::text()` 对空图像、空键以及不存在的键返回空 `QString`；只有已存在的键才返回
其文本值，空值同样是非空对象中的空字符串。

实现范围：

- `Src/XGui/Graphics/XImage.c:2461-2494` 将 `XImage_text_2()` 的空图像、空键、缺失键
  分支统一返回稳定的空 UTF-8 字符串指针，保留已存在空值的空串结果；内存分配失败仍
  返回 `NULL` 作为 C 接口错误信号。
- `Src/XGui/Graphics/XImage.h:747-753` 更新返回值注释，明确空图像和缺失键语义。
- `xgui_regression_test.c:7301-7306` 新增缺失键与空图像断言。

验证结果：默认全量构建、XGuiRegression_Test、CTest 1/1、`XPAINTER_ON=0` 和
`XLAYOUT_STACKED_ON=0` 裁剪回归均通过；`git diff --check` 通过。LSan 仍为既有
Mesa/fontconfig/POSIX 截图后端 `101618 byte(s) leaked in 456 allocation(s)`，未发现
本轮新增泄漏；构建保留仓库既有警告，未提交、未推送。

### 10.172 2026-08-28 QPicture 画笔状态 SetPen opcode

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:115-137`
（`QPicturePaintEngine::updatePen()` 通过 `PdcSetPen` 将画笔状态写入绘图记录）以及
`:483-497`（绘图引擎在画笔状态变脏时序列化 `DirtyPen`）。

实现范围：

- `Src/XGui/Graphics/XPicture.h:35-49,220-234` 新增 `XPictureOpcode_SetPen` 和
  `XPicture_recordSetPen()`；固定 20 字节小端载荷保存颜色、线型、宽度、端点和连接样式。
- `Src/XGui/Graphics/XPicture.c:236-244,648-660,1064-1080` 扩展流校验、记录和回放，
  回放直接更新目标绘制器状态，避免在 Picture 后端再次追加状态命令。
- `Src/XGui/Graphics/XPainter.c:1664-1685,4197-4320` 画笔颜色、样式、宽度、端点和连接
  样式 setter 在 Picture 后端写入完整状态快照；失败不回滚 setter 状态，保持无返回值
  setter 语义。
- `xgui_regression_test.c:4297-4340,13443-13444` 增加录制后以不同初始画笔回放的颜色/宽度
  夹具，确保回放使用记录状态，并保留流有效性验证。

验证结果：默认 `XGuiRegression_Test` 目标重建并执行通过；默认全量构建、CTest（1/1）、
`XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归均已重跑并通过；当前构建仍保留
仓库既有函数指针、const 丢弃和第三方预处理警告，不能宣称零警告。LSan 基线仍为
Mesa/fontconfig/POSIX 截图后端 `101618 byte(s) leaked in 456 allocation(s)`，本功能未发现
新增泄漏，不能宣称零泄漏。修改未提交、未推送。

当前边界：仅补齐 Qt `PdcSetPen` 的便携子集；画刷、透明度、合成模式、变换、裁剪和字体状态
仍未进入 Picture 流，Picture 版本 1 的既有命令编号保持兼容；`XPAINTER_PENSTYLE_ON=0`
时样式载荷仍写入默认值但回放只应用基础颜色。

### 10.173 2026-08-28 QImageReader 默认错误文本

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1400-1418`：
`errorString()` 在错误文本为空时返回本地化的 `Unknown error`，而非空字符串。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:1651-1675` 的值返回和 UTF-8 兼容重载在读取器无错误
  文本、空对象或错误文本意外为空时统一返回稳定的 `Unknown error`；已设置的具体错误
  文本仍按原值返回。
- `Src/XGui/Graphics/XImageReader.h:448-452` 明确内部空值对象与 UTF-8 默认文本的区别。
- `xgui_regression_test.c:5440-5453` 新增默认错误文本的值返回和 UTF-8 返回回归。

验证结果：默认全量构建、XGuiRegression_Test、CTest（1/1）、
`XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归均通过，`git diff --check` 通过。
构建仍保留仓库既有函数指针、const 丢弃和第三方预处理警告，不能宣称零警告；LSan
仍受 Mesa/fontconfig/POSIX 截图后端既有基线 `101618 byte(s) leaked in 456 allocation(s)`
影响，不能宣称零泄漏。修改未提交、未推送。

当前边界：C 兼容的 `errorString_const()` 继续暴露读取器内部空值对象，调用方应使用
值返回或 UTF-8 重载取得 Qt 的默认文本；错误文本翻译目录未接入。

### 10.174 2026-08-28 QImageReader setFormat 无处理器副作用

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:617-624`：
`setFormat()` 仅替换显式格式字节数组，不销毁已创建的处理器，也不重置动画、尺寸或
其它读取状态。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:920-926` 删除 `setFormat()` 中原有的处理器释放、
  GIF 动画清理和尺寸状态重置，仅保留格式字符串的替换；设备和文件名切换仍按 Qt
  语义重建处理器。

验证结果：默认全量构建、XGuiRegression_Test、CTest（1/1）、
`XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归均通过，`git diff --check` 通过。
ASan/LSan 回归通过，LSan 仍报告 Mesa/fontconfig/POSIX 截图后端既有基线
`101618 byte(s) leaked in 456 allocation(s)`，不能宣称零泄漏；构建保留仓库既有警告，
不能宣称零警告。修改未提交、未推送。

当前边界：与 Qt 一样，已初始化处理器在 `setFormat()` 后继续承载原设备状态；需要
切换处理器时应重新设置设备或文件名。

### 10.175 2026-08-28 QImageReader imageFormat 实例查询

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.h:54-60`
及 `qimagereader.cpp:864-875`：`imageFormat()` 通过处理器的
`QImageIOHandler::ImageFormat` 选项返回图像像素格式；处理器不支持该选项时返回
`QImage::Format_Invalid`，且查询本身不读取像素数据。

实现范围：

- `Src/XGui/Graphics/XImageReader.h:218-224` 新增 `XImageReader_imageFormatValue()`，使用
  独立名称避免 C 接口与既有静态 `XImageReader_imageFormat(fileName)` 冲突。
- `Src/XGui/Graphics/XImageReader.c:1056-1070` 确保处理器后查询
  `XImageIOHandlerOption_ImageFormat`，选项不支持、查询失败或读取器无效时返回
  `XImageFormat_Invalid`。
- `Src/XGui/Graphics/XImageBuiltinPlugin.c:82-128,220-237` 为内置处理器增加不消费设备
  的 BMP 头部探测及 `ImageFormat` 选项声明，映射规则依据
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:858-885`。
- `xgui_regression_test.c:5248-5254,5405-5406` 覆盖空读取器的 Invalid 返回，以及自动探测 BMP
  头部得到 `XImageFormat_RGB32` 的回归断言。

验证结果：默认全量构建、XGuiRegression_Test、CTest（1/1）、
`XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归及 `git diff --check` 均通过。
ASan/LSan 回归通过，但 LSan 仍报告 Mesa/fontconfig/POSIX 截图后端既有基线
`101618 byte(s) leaked in 456 allocation(s)`，不能宣称零泄漏；构建保留仓库既有
函数指针、const 丢弃和第三方预处理警告，不能宣称零警告。修改未提交、未推送。

当前边界：内置处理器已对 BMP 暴露 ImageFormat，并按信息头映射 Mono、Indexed8 与
RGB32；PNG/GIF/JPEG/SVG 的便携回退仍统一报告 ARGB32，尚未按各格式完整解析 Qt 的
位深、调色板和透明块元数据。接入自定义处理器并实现 ImageFormat 选项后会直接透传
其 `XImageFormat` 值。

### 10.176 2026-08-28 QImageWriter 默认质量/压缩值透传

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:663-667`：
`write()` 在处理器支持 `Quality` 或 `CompressionRatio` 时始终调用 `setOption()`，
即使写入器尚未显式设置参数（默认值为 `-1`），不能用非负条件跳过调用。

实现范围：

- `Src/XGui/Graphics/XImageWriter.c:214-226` 删除质量与压缩值的非负过滤，改为按
  Qt 顺序在处理器声明支持时原样传递 `m_quality`/`m_compression`，保留其它文本、
  子类型、优化写入、渐进扫描和变换选项逻辑。

验证结果：默认全量构建、XGuiRegression_Test、CTest（1/1）、
`XPAINTER_ON=0` 与 `XLAYOUT_STACKED_ON=0` 裁剪回归、ASan/LSan 回归及
`git diff --check` 均通过。LSan 仍报告 Mesa/fontconfig/POSIX 截图后端既有基线
`101618 byte(s) leaked in 456 allocation(s)`，不能宣称零泄漏；构建保留仓库既有
函数指针、const 丢弃和第三方预处理警告，不能宣称零警告。修改未提交、未推送。

当前边界：内置处理器目前不消费压缩比选项，第三方插件可通过 `setOption()` 观察
到 Qt 一致的 `-1` 默认值；Gamma 选项仍受现有 C 接口字段裁剪范围限制。

### 10.179 2026-08-28 QImageReader 背景色选项能力透传

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:982-1004`：
`setBackgroundColor()` 先通过 `supportsOption(BackgroundColor)` 初始化并检查处理器能力，
仅在处理器支持时调用 `setOption()`；`backgroundColor()` 同样仅在支持时读取处理器选项，
否则返回无效 `QColor`。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:1196-1224` 改为确保处理器后按
  `XImageIOHandlerOption_BackgroundColor` 能力透传 ARGB32 值；处理器不支持时 setter
  不再写入读取器私有缓存，getter 返回 `0` 作为 C 接口的无效颜色表示。
- `xgui_regression_test.c:5405-5408` 增加空读取器无背景色能力时返回无效值的断言。

验证结果：默认目标重建、回归测试、全量构建、CTest、`XPAINTER_ON=0` 与
`XLAYOUT_STACKED_ON=0` 裁剪构建均通过；本轮未重复执行 ASan/LSan，沿用既有记录的
LSan Mesa/fontconfig/POSIX 截图后端基线 `101618 byte(s) leaked in 456 allocation(s)`，
因此不能宣称零泄漏。构建仍有既有函数指针、const 丢弃及第三方预处理警告，不能宣称零警告。
修改未提交、未推送。

当前边界：内置 BMP/PNG/JPEG/GIF/SVG 处理器均未声明 `BackgroundColor`，因此这些格式按 Qt
行为返回无效颜色；自定义处理器若声明该选项即可通过 `XImageIOHandlerOptionValue.color`
接收和返回 ARGB32 值。

### 10.180 2026-08-28 QImageReader 设备切换后的探测缓存失效

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:750-798`：
`setDevice()`/`setFileName()` 删除旧处理器并清空文本元数据；后续格式、尺寸和文本查询必须
针对新设备重新初始化，不能复用旧设备的内容探测结果。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:985-1018` 在 `setDevice()` 与 `setFileName()` 中清理
  `m_detectedFormat`、文本键值缓存及其加载标志，保持设备切换后的状态隔离。

验证结果：重新构建 `XGuiRegression_Test` 并运行回归测试通过，`git diff --check` 通过；
默认与既有裁剪构建均保留通过记录。构建仍有仓库既有类型指针、const 丢弃及第三方预处理
警告，未执行新的 ASan/LSan，不宣称零警告或零泄漏。修改未提交、未推送。

当前边界：格式缓存只覆盖本项目 C 接口已暴露的内容探测路径；Qt 的 QFile 自动扩展名轮询
和动态 QFactoryLoader 插件扫描仍由固定容量注册表近似实现。

### 10.181 2026-08-28 QImage 色彩模型兼容性补齐

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:438-489`
的 `qt_csColorData()`、`qt_compatibleColorModelSource()` 与
`qt_compatibleColorModelTarget()`：RGB/BGR/Indexed 像素映射为 RGB，灰度/单色映射为
Gray，CMYK 映射为 CMYK，Alpha 数据不含颜色可接受任意色彩空间；灰度源可附加 RGB
色彩空间，目标转换还需遵守目标变换模型。

实现范围：

- `Src/XGui/Graphics/XImage.c:579-612` 新增像素颜色模型映射和兼容性判断；
  `XImage_setColorSpace()` 及颜色空间转换调用点均先检查模型，阻止 RGB 图像错误附加
  Gray 色彩空间，同时保留 Gray→RGB 与 Alpha 任意空间规则。
- `xgui_regression_test.c:6176-6195,6265-6276` 增加 Gray 接受 Gray、RGB 拒绝 Gray、
  CMYK 拒绝 RGB 的回归断言。

验证结果：默认构建和 `build-crop-painter-off` 裁剪构建的 `XGuiRegression_Test` 均通过；
CTest（1/1）及 `git diff --check` 通过。构建保留仓库既有函数指针、const 丢弃和第三方
预处理警告；本轮未执行新的 ASan/LSan，不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：`XColorSpace` 仍未承载 ICC 原始数据、逐通道 LUT 及完整 CMYK/元素列表处理，
因此实际颜色变换仍是现有 RGB 传递函数的便携近似；目标变换模型的细粒度策略待后续扩展。

### 10.182 2026-08-29 XImagePluginRegistry 全局锁与内置处理器生命周期对齐

本轮对照 Qt 6.8 图像插件辅助层和工厂加载器实现：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:18-20`
  声明进程级 `QFactoryLoader` 与配套 `QMutex`；`:69-75` 的 `pluginLoader()` 在访问
  loader 前加锁，并通过共享指针析构器解锁。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:84-100`
  说明内置格式与插件格式合并后排序、去重；`:45-67` 说明插件 MIME 元数据与能力位
  共同决定公开列表。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:201-219`、
  `:221-254` 说明插件探测期间保存并恢复非顺序设备位置；内置处理器属于始终可用的
  基础路径。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/plugin/qfactoryloader.cpp:293-388`
  展示目录发现、元数据解析、键映射更新及插件 `PreventUnloadHint`；`:588-610` 展示
  键映射建立和大小写不敏感的索引查找。

实现范围：

- `Src/XGui/Graphics/XImagePluginRegistry.c:10-75` 增加基于 `XAtomic_uintptr_t` 的
  原子懒初始化递归互斥锁；注册、移除、清理、格式/MIME 查询、内容探测以及读写处理器
  创建均在统一锁下执行。递归属性覆盖 `ensureBuiltin()` 调用 `addPlugin()`，以及插件
  `capabilities()`/`create()` 回调再次查询注册表的场景。
- `Src/XGui/Graphics/XImagePluginRegistry.c:199-294` 让清理操作清零残留槽位并保留“下次
  查询自动恢复内置插件”的状态；禁止移除 `XImageBuiltinPlugin_instance()`，保持 Qt
  内置 imageformats 处理器不随外部插件管理接口消失。同步补齐成功/失败路径的解锁。
- `Src/XGui/Graphics/XImagePluginRegistry.h:28-50` 明确 `clear()` 与
  `removePlugin()` 的生命周期和内置插件约束；`xgui_regression_test.c:5740-5754`
  增加清空、不可移除和恢复 BMP 能力的回归断言。

验证结果：默认 `cmake --build build --target XGuiRegression_Test -j1` 成功，运行
`./bin/XGuiRegression_Test` 通过；`build-crop-painter-off` 裁剪配置同样重建并运行
通过。`ctest --test-dir build --output-on-failure` 为 1/1 通过，`git diff --check`
通过。构建输出仅保留仓库已有的函数指针兼容、const 丢弃及第三方预处理警告；本轮未执行
新的 ASan/LSan，不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：该改动只保护 XGui 的固定容量静态注册表和显式 `addPlugin()` 接口；Qt
`QFactoryLoader` 的目录扫描、动态库元数据解析、键冲突优先级和不可卸载动态库仍不引入
到嵌入式核心。`pluginAt()` 返回借用指针，调用方仍必须保证外部插件对象存活；跨线程
持有该指针时需由调用方配合注册表生命周期管理。

### 10.183 2026-08-29 内置 JPEG `jpg` 别名与 MIME 元数据对齐

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:195-205`：
图像插件元数据要求每个格式键都有对应 MIME，JPEG 示例同时公开 `jpg`、`jpeg`，且二者
均映射到 `image/jpeg`；`qimagereader.cpp:1476-1481` 也将 JPG 作为公开格式名称。

实现范围：

- `Src/XGui/Graphics/XImageBuiltinPlugin.c:18-25,371-378` 内置插件新增 `jpg` 键、
  `*.jpg` 文件过滤器和重复的 `image/jpeg` MIME，并按数组实际长度初始化三组元数据，
  保证后续新增别名不会被固定循环次数截断。已有 `XImageCodec_formatFromName()` 已将
  `jpg` 与 `jpeg` 归一到同一 JPEG 编解码器，因此显式格式、后缀和编码能力自动复用。
- `xgui_regression_test.c:5300-5320` 将 JPEG 双键纳入格式数量断言，并验证 reader 列表
  同时包含 `jpg`/`jpeg`；MIME 列表继续验证 Qt 排序去重后的单个 `image/jpeg`。

验证结果：默认构建 `XGuiRegression_Test` 与运行通过；`build-crop-painter-off` 裁剪构建
与运行通过；CTest 1/1 通过，`git diff --check` 通过。构建仍保留仓库已有警告，本轮未
执行新的 ASan/LSan，不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：JPEG 仍使用当前项目便携 Codec 的图像质量与元数据能力；Qt 动态 imageformats
目录发现和真正 JPEG 插件的全部 `QImageIOHandler` 选项不在本轮范围内。

### 10.184 2026-08-29 XPainter Picture 不透明度与合成模式状态对齐

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:147-174`
（`QPicturePaintEngine::updateCompositionMode()` 和 `updateOpacity()` 将状态写入
`PdcSetCompositionMode`、`PdcSetOpacity`），以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:820-832`
（Picture 回放时按记录顺序恢复合成模式和不透明度）。

实现范围：

- `Src/XGui/Graphics/XPicture.h:35-50,232-245` 新增 `SetOpacity` 与
  `SetCompositionMode` 两个连续 opcode 及固定长度记录接口；保留原有 1~11
  命令编号，便携流版本仍为 1。
- `Src/XGui/Graphics/XPicture.c:236-247,651-685,1089-1117` 增加 4 字节浮点不透明度
  和 4 字节合成模式的流校验、记录和回放；记录接口拒绝非有限不透明度与越界合成模式，
  回放直接更新目标 XPainter 状态，避免 Picture 后端递归生成状态命令。
- `Src/XGui/Graphics/XPainter.c:1687-1707,4950-4984` 在 Picture 后端接入状态记录；
  setter 先按 Qt 语义钳制/校验并忽略未变化值，再写入状态 opcode。软件后端沿用已有
  不透明度和 38 种合成模式实现。
- `xgui_regression_test.c:4432-4480,13708` 增加跨 Picture 回放的状态和像素断言，确认
  `Clear` 会清除目标矩形而不影响外部像素。

验证结果：默认 `cmake --build build --target XGuiRegression_Test -j1` 和运行回归通过；
`build-crop-painter-off` 重新构建并运行回归通过，随后恢复默认构建产物；构建输出保留仓库既有函数指针、const 丢弃及第三方
预处理警告，未执行新的 ASan/LSan，不能宣称零警告或零泄漏。`git diff --check` 通过。
修改未提交、未推送。

当前边界：仅补齐 QPicture 的不透明度和合成模式状态；画刷、背景、变换、裁剪、字体、
渲染提示等 Qt 状态 opcode 仍未进入便携 Picture 流。4 字节 float 是嵌入式 ABI 的有界
表示，不能表示 Qt 在桌面平台上的完整 `qreal` 双精度值。

### 10.185 2026-08-29 图标主题回退与图像文本元数据空白规则对齐

本轮补充对照 Qt 6.8：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:446-570,572-604,612-626`
  的主题索引、传统目录图标和 `fallbackThemeName` 查找流程；有主题名时回退主题仍使用
  `themeSearchPaths()`，`fallbackSearchPaths()` 仅服务于无主题名的独立文件回退。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1376-1390,1414-1421`
  的 `QIcon::fromTheme()` 路径处理语义：绝对路径直接按文件图标创建，不交给主题加载器。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:6459-6490` 中
  `qt_getImageTextFromDescription()` 的键值解析：普通键保留原始空白，值执行 simplified，
  空白条目被忽略；`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:609-623`
  的 `QImageWriter::setText()` 对键和值分别 simplified，并用两个换行符拼接 Description。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:1468-1474` 将带 `fallbackThemeName` 的传统图标查找
  改为从主题搜索路径读取，避免错误使用独立文件回退目录；`Src/XGui/Icon/XIcon.c:938-976`
  对绝对路径直接构造文件图标。`xgui_regression_test.c:471-557` 增加不同搜索根目录的
  传统 fallback 主题测试，并校验图标尺寸与像素。
- `Src/XGui/Graphics/XImageReader.c:313-395` 调整 Description 解析：保留普通键原始空白、
  仅用简化结果判空，并忽略空白记录；`xgui_regression_test.c:5144-5718` 扩展 mock
  Description，覆盖制表符键、QMap 排序和文本值简化规则。

验证结果：默认 `cmake --build build -j1`、`./bin/XGuiRegression_Test`、
`ctest --test-dir build --output-on-failure` 均通过；`build-crop-painter-off` 完整构建及
回归同样通过，随后恢复默认构建产物。`git diff --check` 通过。完整构建仍显示仓库既有
函数指针、const 丢弃及第三方预处理警告；未执行新的 ASan/LSan，不能宣称零警告或零泄漏。
修改未提交、未推送。

当前边界：主题目录扫描、index.theme 完整继承和动态插件发现仍使用现有嵌入式实现；文本
元数据仍限制在 Description 选项，未扩展 Qt 的任意键值容器和逐格式私有元数据。

### 10.186 2026-08-29 最终 ASan/LeakSanitizer 回归记录

为覆盖本轮图像插件、图标主题、文本元数据和 Picture 状态改动，重新生成了 `build-asan`
 配置并构建 `XGuiRegression_Test`。执行 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`
 后，全部 XGui 回归断言通过；LeakSanitizer 报告 `102130 byte(s) leaked in 458 allocation(s)`。
其中主要来源为 Mesa `libGLX_mesa.so` 及 fontconfig 的进程级资源，另有既有
`XImage_setDevicePixelRatio()` 克隆路径（`Src/XGui/Graphics/XImage.c:2690`）和
`XPlatformNativeWindow_grabWindow()` 截图路径（`Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:1382`）。
该结果与仓库历史外部后端泄漏基线同类，不能据此宣称本轮零泄漏；ASan 构建期间仍保留
仓库既有类型指针、const 丢弃及第三方预处理警告。随后已重新执行默认 `cmake --build
build -j1`，恢复 `bin/XGuiRegression_Test` 为非 ASan 版本。

### 10.187 2026-08-29 图标主题引擎 key 对齐

本轮补充对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:954-967`：
`QIconLoaderEngine::key()` 返回固定引擎类型名，图标名称由独立的 `iconName()` 提供，
不能将主题图标名拼接进引擎 key。

实现范围：

- `Src/XGui/Icon/XIconThemeEngine.c:174-180` 将 `XIconThemeEngine_key()` 固定为
  `QIconLoaderEngine`，保留名称查询由 `iconName()` 完成。
- `xgui_regression_test.c:4761-4762` 增加精确 key 字符串断言，防止引擎类型标识回退为
  主题 URI 或混入图标名。

验证结果：默认和 `build-crop-painter-off` 配置的 XGui 回归均通过，默认全量构建及
CTest 1/1 通过，`git diff --check` 通过。构建仍保留仓库既有类型指针、const 丢弃和
CMake 第三方预处理警告；未执行新的 ASan/LSan，不能宣称零警告或零泄漏。修改未提交、
未推送。

当前边界：引擎 key 已与 Qt 类型标识一致，但 XIconEngine 的主题缓存、动态主题插件
发现和完整 index.theme 继承仍采用嵌入式实现，尚未引入桌面 Qt 的动态库扫描。

### 10.188 2026-08-29 索引主题为空目录时的继承语义

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp`：

- `:347-420` 由 `index.theme` 的 `Directories` 构建主题内容目录；索引文件存在但目录
  为空时，主题仍然有效，不应回退到传统尺寸/上下文目录约定。
- `:422-436` 规定父主题、fallback 主题以及最终 `hicolor` 的继承顺序。
- `:446-467` 规定无效索引主题不加载自身图标内容；`:469-535` 负责有效索引主题的
  内容目录和索引目录搜索。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:1075-1098` 的 `theme_searchTheme()` 在成功解析
  `index.theme` 后，无论 `Directories` 条目数量，都只按索引目录和 `Inherits` 继续查找；
  只有完全没有索引文件时才启用内置的传统目录约定。
- `Src/XGui/Icon/XIconThemeInternal.c:1261-1268` 的 `theme_collectSizes()` 使用同一判定，
  防止空索引主题错误报告伪造的 `size x size` 图标尺寸。

验证结果：默认 `build` 与 `build-crop-painter-off` 的 `XGuiRegression_Test` 均通过；
默认全量构建、CTest 1/1 和 `git diff --check` 通过。构建仍保留仓库既有函数指针、const
丢弃及第三方预处理警告；ASan/LSan 仍有 10.186 所记录的进程级资源和既有克隆路径泄漏，
不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：该修正只覆盖索引主题为空目录时的本地内容与继承判定；Qt 桌面版动态主题
发现、完整 `index.theme` 继承冲突优先级和缓存失效策略仍未引入嵌入式核心。

### 10.189 2026-08-29 XPainter Picture 渲染提示状态对齐

本轮对照 Qt 6.8：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:274-281`：
  `QPicturePaintEngine::updateRenderHints()` 将完整渲染提示掩码写入
  `PdcSetRenderHint`。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:6866-6883`：
  `QPainter::setRenderHints()` 按位开启或清除掩码，并在每次调用后标记
  `DirtyHints`，即使掩码没有实际变化也会保留该状态更新时序。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture_p.h:100-103`：
  `PdcSetRenderHint` 使用 `quint32` 负载。

实现范围：

- `Src/XGui/Graphics/XPicture.h:50,253` 新增连续便携 opcode
  `SetRenderHints=14` 和 `XPicture_recordSetRenderHints()`；
  `Src/XGui/Graphics/XPicture.c:237-244,688-696,1131-1140` 完成 4 字节
  little-endian 掩码的流校验、记录与回放。渲染提示关闭时仍消费该记录，保证
  裁剪配置下 Picture 流的前向兼容。
- `Src/XGui/Graphics/XPainter.c:1709-1723,5087-5110` 在 Picture 后端记录
  每次启用的完整掩码；单个位和批量位操作都复用 Qt 的按位语义，并保留重复调用
  的记录时序。
- `xgui_regression_test.c:4657-4711` 增加 `test_painter_picture_render_hints_record()`，验证
  单个位开启、批量开启、单个位清除以及 Picture 回放后的最终掩码和查询结果。
  额外覆盖重复批量设置，确认 Qt 的每次 `DirtyHints` 时序均保留在便携流中。

验证结果：默认与 `build-crop-painter-off` 的 `XGuiRegression_Test` 均通过；默认
目标重新链接后运行回归与 CTest 1/1 通过，`git diff --check` 通过。完整构建输出仍
包含仓库既有函数指针、const 丢弃和第三方预处理警告；ASan/LSan 的进程级资源及既有
克隆路径泄漏见 10.186，不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：仅序列化渲染提示位集合并恢复 `XPainter` 状态；画刷、背景、变换、裁剪、
字体和渲染设备私有状态仍未加入自定义 Picture 流，底层便携回调能力也不因该 opcode
而扩展。

### 10.190 2026-08-29 图像处理器失败后的格式回退顺序

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:201-254`：
带后缀或显式格式时先尝试对应插件；该插件创建处理器失败后，自动探测阶段跳过同一
后缀插件，只继续尝试其它插件，然后再进入内置处理器回退。非顺序设备在能力探测和
处理器创建前后恢复设备位置。

实现范围：

- `Src/XGui/Graphics/XImagePluginRegistry.c:307-373` 记录显式格式首个命中的插件；若其
  `create()` 返回空，在后续自动探测中跳过该插件，避免重复调用失败对象，同时允许
  其它插件和内置编解码器继续接管。
- 既有注册表互斥锁、能力探测位置恢复和内置插件排序逻辑保持不变，显式插件仍优先于
  内置处理器，注册表不取得插件对象所有权。

验证结果：默认 `build` 与 `build-crop-painter-off` 的 `XGuiRegression_Test` 均通过；
默认目标重新链接后回归与 CTest 1/1 通过，`git diff --check` 通过。全量构建仍保留
仓库既有函数指针、const 丢弃及第三方预处理警告；ASan/LSan 结果沿用 10.186，不能
宣称零警告或零泄漏。修改未提交、未推送。

当前边界：动态 `QFactoryLoader` 目录扫描、插件库加载/卸载及 Qt 私有后缀映射仍未引入
嵌入式核心；当前规则覆盖固定容量注册表内的显式插件和内置处理器。

### 10.191 2026-08-29 XPainter Picture 画刷原点状态对齐

本轮对照 Qt 6.8：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:194-203`：
  `QPicturePaintEngine::updateBrushOrigin()` 将 `QPointF` 作为一次完整状态记录写入
  Picture；`:483-496` 的脏状态调度保证画刷原点变化后执行该更新。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2093-2112`：
  `QPainter::setBrushOrigin()` 更新两个坐标并标记 `DirtyBrushOrigin`；Picture
  引擎会保留每次调用的状态更新时序。

实现范围：

- `Src/XGui/Graphics/XPicture.h:49,259` 新增连续便携 opcode
  `SetBrushOrigin=15` 与 `XPicture_recordSetBrushOrigin()`；
  `Src/XGui/Graphics/XPicture.c:231-246,701-713,1157-1165` 使用两个固定宽度
  little-endian IEEE-754 `float`（共 8 字节）写入、严格校验并直接恢复回放状态。
  校验器最大 opcode 与长度判断同步覆盖 15；即使 `XPAINTER_BRUSH_ORIGIN_ON` 关闭，
  回放仍消费该记录，保证裁剪构建下流格式兼容。
- `Src/XGui/Graphics/XPainter.c:1724-1738,4567-4577` 在 Picture 后端为每次有效
  坐标设置追加完整坐标记录，查询接口按 `XPointF_toPoint()` 的对称四舍五入语义
  返回整数点。
- `xgui_regression_test.c:4713-4754,14551-14553` 验证记录流有效、回放成功以及
  `(1.25,-2.5)` 回放为 `(1,-3)`；默认与裁剪配置均执行该测试（裁剪配置下该测试
  由编译开关省略，但协议校验路径仍覆盖）。测试同时断言重复坐标设置产生两条
  `SetBrushOrigin` 记录，与 Qt 每次 `DirtyBrushOrigin` 更新一致。

验证结果：默认 `build` 目标构建、`./bin/XGuiRegression_Test` 与 CTest 1/1 通过；
`build-crop-painter-off` 目标构建及回归同样通过；`git diff --check` 通过。构建仍有
仓库既有 `XSignal` 函数指针、const 丢弃及第三方预处理警告；LSan 仍受 10.186 所述
进程级资源与既有克隆路径泄漏影响，不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：该协议只恢复画刷原点状态，不增加 QBrush、背景、变换、裁剪区域、字体或
设备私有状态的序列化；浮点负载采用便携单精度，未表达 Qt `QPointF` 可能保留的更高
精度。

### 10.192 2026-08-29 图标主题索引、缩放与独立回退对齐

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp`：

- `:347-420`：主题目录先以 `index.theme` 是否存在判定有效性，再读取
  `Directories`、`Size/Type/Scale/Threshold` 与 `Inherits`；索引存在但目录为空时
  仍是有效主题。
- `:422-436`：父主题、配置的 fallback 主题以及最终 `hicolor` 的继承顺序。
- `:446-535`：有效索引主题按内容目录和索引目录查找 PNG/SVG；无效索引主题不加载
  自身内容。
- `:572-609`：主题外回退只按 PNG、XPM、SVG 顺序，首个存在文件即停止。
- `:794-847`：`Scale` 必须精确匹配，Fixed/Scalable/Threshold 按元数据计算距离；
  `:912-937`：仅在源图大于请求尺寸时按比例缩小，不放大较小图。
- `:954-967`：图标引擎 `key()` 固定返回 `QIconLoaderEngine`，图标名由
  `iconName()` 单独提供。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:49-53,816-877,1182-1210` 分离独立回退扩展名、
  按原始 `Scale/Threshold` 参与匹配和距离计算，并采用 KeepAspectRatio 只缩小策略；
  `:1280-1298,1653-1666` 在有效 `index.theme`（包括空 `Directories`）下禁止传统
  目录伪探测，索引失败时才继续 fallback 搜索。
- `Src/XGui/Icon/XIconThemeInternal.c:1467-1514` 独立回退尺寸保留实际图片矩形，
  与首个可加载文件停止规则一致；SVG 支持仍受 `XICON_THEME_SVG_AVAILABLE` 裁剪开关
  控制。
- `Src/XGui/Icon/XIconThemeEngine.c:174-180` 返回固定引擎 key；
  `Src/XGui/Icon/XIcon.c:935-976` 对绝对路径直接构造文件图标，避免把路径误当主题名。

验证结果：默认与 `build-crop-painter-off` 的 XGui 回归目标均通过，CTest 1/1 通过；
`git diff --check` 通过。保留 10.186 的既有警告和 LSan 泄漏边界，未提交、未推送。

当前边界：未实现 Qt 桌面版 `QFactoryLoader` 动态主题发现、GTK 缓存完整格式及缓存
失效策略；主题索引解析仍是嵌入式固定容量实现。

### 10.193 2026-08-29 图像处理器前置条件与失败回退顺序

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp`：

- `:143-149`：自动格式探测关闭且格式为空时，`createReadHandlerHelper()` 直接返回
  空处理器。
- `:201-219`：先尝试后缀对应插件并恢复非顺序设备位置；插件创建失败后，
  `:221-254` 的自动探测会跳过同一后缀插件，继续其它插件和内置处理器。

实现范围：

- `Src/XGui/Graphics/XImagePluginRegistry.c:306-373` 在注册表入口复现无格式前置条件，
  同时记录显式格式首个命中插件；其 `create()` 失败后自动探测阶段跳过该对象，保留
  其它插件及内置编解码器接管机会。能力探测、创建前后设备位置恢复和显式插件优先级
  保持不变。
- `Src/XGui/Graphics/XImageBuiltinPlugin.c:20-24` 将内置声明扩展为 BMP/PNG/JPG/JPEG/
  GIF/SVG，JPEG 别名共享 MIME `image/jpeg`，文件过滤器包含 `*.jpg`；初始化按声明
  数组长度遍历，避免新增格式遗漏。
- `Src/XGui/Graphics/XImageReader.c:313-405` 按 Qt `qt_getImageTextFromDescription()`
  规则分隔空行记录：普通键保留键原始空白、值统一 `simplified()`，空记录和空键忽略，
  重复键按有序列表覆盖。

验证结果：默认和裁剪构建的 XGui 回归目标均通过，CTest 1/1 与 `git diff --check`
 通过；构建保留仓库既有警告，LSan 边界沿用 10.186，未提交、未推送。

当前边界：不包含 Qt `QFactoryLoader` 动态插件目录扫描、共享库装卸和私有后缀映射；
  注册表仍使用固定容量、显式生命周期的嵌入式插件集合。

### 10.194 2026-08-29 QImage 色彩空间目标模型兼容性

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:470-490`：
  `qt_compatibleColorModelSource()` 允许灰度输入附加 RGB 色彩空间，
  `qt_compatibleColorModelTarget()` 允许灰度输出通过 `ThreeComponentMatrix` 转换到
  任意明确颜色模型；`qimage.cpp:5038-5065,5360-5384` 在目标模型不兼容时改走转换路径。

实现范围：

- `Src/XGui/Graphics/XImage.c:601-630,753-806` 保留元数据附加的灰度到 RGB 规则，新增
  转换目标判定：灰度像素在目标色彩空间声明三分量矩阵时可进入转换路径，未定义目标
  模型仍拒绝；Alpha-only 数据继续视为可附加任意有效色彩空间。
- 转换入口复用现有 `XColorSpaceTransform` 与项目内存/拷贝接口，不引入平台 API 或
  标准库分配器。

验证结果：默认与 `build-crop-painter-off` 回归目标均通过，CTest 1/1、
`git diff --check` 通过。既有构建警告及 10.186 的 LSan 泄漏仍存在，不能宣称零警告
或零泄漏；修改未提交、未推送。

当前边界：ICC profile、逐通道 LUT、`ElementListProcessing` 元素列表和完整
`QColorTransform` 矩阵仍未实现；该实现只覆盖当前便携色彩空间转换接口可表达的模型
兼容性判定。

### 10.195 2026-08-29 XPainter Picture 世界变换状态对齐

本轮对照 Qt 6.8：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:235-243`：
  `QPicturePaintEngine::updateMatrix()` 在脏变换状态更新时写入完整
  `QTransform` 和 combine 标志；`:483-496` 的 `updateState()` 负责调度该状态更新。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2849-2867,2893-2978`：
  世界矩阵启用位只有在实际变化时更新，平移、缩放、旋转、错切和显式世界矩阵设置
  都会重新提交变换状态；`:7915-7963` 的重置/设置世界矩阵路径同样更新该状态。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:778-840`：
  Picture 回放把世界矩阵和启用状态恢复到目标 `QPainter`，而不是把状态变化再次
  记录到目标 Picture。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qtransform.cpp:1036-1055`：
  `QTransform` 的标准数据流格式依次写入九个 `double` 矩阵元素；本项目因此明确把
  单精度负载标为嵌入式协议边界，而不是宣称字节级 Qt 数据流兼容。

实现范围：

- `Src/XGui/Graphics/XPicture.h:46-51,263-273` 新增连续便携 opcode
  `SetTransform=16` 与 `XPicture_recordSetTransform()`，公共接口和字段含义使用全中文
  注释。`Src/XGui/Graphics/XPicture.c:28,246-256,727-750` 使用固定 40 字节负载，按
  `m11,m12,m21,m22,dx,dy,m13,m23,m33` 顺序写入九个 little-endian IEEE-754 单精度值，
  最后写入四字节启用标志；记录前拒绝空指针和非有限矩阵，校验器严格验证 opcode 与
  负载长度。
- `Src/XGui/Graphics/XPainter.c:1740-1757,4819-4936` 在 Picture 后端为世界矩阵
  变更追加完整状态记录；重置、显式设置、平移、缩放、旋转、错切和启用位变更均覆盖。
  启用位未改变时直接返回，与 Qt 的 no-op 语义一致。回放在
  `Src/XGui/Graphics/XPicture.c:1204-1231` 直接恢复矩阵和启用位，避免重复记录。
- `xgui_regression_test.c:4676-4726,14074` 增加矩阵记录、校验、平移回放以及默认/裁剪
  配置下启用位语义的回归覆盖；即使 `XPAINTER_WORLD_MATRIX_ON=0`，协议记录仍被消费，
  以保持跨配置流兼容。

验证结果：`cmake --build build --target XGuiRegression_Test -j1`、
`./bin/XGuiRegression_Test`、`cmake --build build-crop-painter-off --target
XGuiRegression_Test -j1`、裁剪配置回归、`ctest --test-dir build --output-on-failure`
（1/1）和 `git diff --check` 均通过。构建仍输出仓库既有 `XSignal`/`XEvent` 函数指针、
const 丢弃及第三方预处理警告；LSan 仍受既有进程级资源和克隆路径泄漏影响，不能宣称
零警告或零泄漏。修改未提交、未推送。

当前边界：Qt `QTransform` 序列化使用 double，本协议为匹配 `XImageTransform` 和嵌入式
固定宽度而使用 float，存在精度收窄；自定义流暂未提供 Qt 的 view/window/viewport、裁剪
区域、画刷、背景和字体状态 opcode，因此本轮只恢复世界矩阵，不把这些状态近似编码为
有效矩阵。

### 10.196 2026-08-29 QImageReader 畸形 BMP 与插件创建失败回归

本轮审计对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:201-254`：显式后缀
  插件优先尝试；插件 `create()` 失败后继续其它插件和内置处理器，自动探测阶段跳过
  已失败的同一后缀插件。
- `qimagereader.cpp:295-337`：后缀处理器的 `canRead()` 失败后恢复设备位置并继续按
  内容探测；`qimagereader.cpp:1110-1116`：`canRead()` 只做轻量格式判断，并不保证
  数据完整；`qimagereader.cpp:1160-1218`：实际解码失败返回 `InvalidDataError`。
- `qimageiohandler.h:24-40,41-60,69-78`：处理器的 `canRead()`、`read()` 和描述文本
  接口分别承担格式识别、完整解码与元数据职责。

实现范围：

- `xgui_regression_test.c:6610-6643` 新增截断 BMP fixture：验证可识别但可能损坏的
  文件仍可通过 `XImageReader_canRead()`，而像素区截断必须由 `read()` 拒绝并设置
  `XImageReaderError_InvalidDataError`，输出保持空图像，不暴露部分解码结果。
- `xgui_regression_test.c:6112-6162` 新增外部 BMP 插件 `create()` 失败回归：验证
  内置 BMP 处理器继续接管并正确还原像素；测试插件仍验证设备位置保护和显式后缀
  失败后的内容回退。
- 测试中的工厂开关 `g_mockRejectCreate` 位于
  `xgui_regression_test.c:20-27,5431-5444`，只影响测试插件，不改变生产实现。

验证结果：默认 `cmake --build build --target XGuiRegression_Test -j1`、
`./bin/XGuiRegression_Test`、`ctest --test-dir build --output-on-failure`（1/1）均通过；
`build-crop-painter-off` 的同名目标和回归程序也通过。`git diff --check` 通过。构建中
仍有仓库既有的 XClass/XSignal/XEvent 类型警告，且沿用 10.186 的 LSan 泄漏边界，不能
宣称零警告或零泄漏；本轮未提交、未推送。

当前边界：该回归覆盖嵌入式注册表的创建失败回退，不等同于 Qt 桌面版 `QFactoryLoader`
的动态目录扫描、共享库装卸和私有插件元数据映射；`canRead()` 仍只验证格式前缀，
畸形数据的完整性只在 `read()` 阶段判定。

### 10.197 2026-08-29 XIcon 高 DPI 引擎结果修正

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:911-930`：高 DPI
  `QIcon::pixmap()` 将设备无关尺寸传给 `scaledPixmap()`，再依据引擎实际返回的
  物理像素尺寸修正结果设备像素比；普通 DPI 路径固定为 1.0。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-303`：图标
  引擎的默认缩放实现允许返回小于请求尺寸的像素图，调用方不能假定输出尺寸必然
  等于请求尺寸。

实现范围：

- `Src/XGui/Icon/XIcon.c:493-519,732-778` 为自定义引擎高 DPI 路径增加有限浮点到
  整数的物理目标尺寸转换，并使用实际输出宽高计算设备像素比；拒绝非有限比例和
  超出 `INT_MAX` 的中间结果，避免未定义整数转换。普通 DPI 及内置资源路径保持
  输出比例为 1.0 或沿用现有按实际尺寸缩放逻辑。
- `Src/XGui/Icon/XIcon.h:262-271` 补充设备像素比、实际输出尺寸与空结果处理的
  中文公共接口注释。

验证结果：默认和裁剪配置的 XGui 回归目标、回归程序、CTest 1/1 及
`git diff --check` 均通过。构建仍保留仓库既有函数指针/const 警告，LSan 沿用
10.186 的进程级资源泄漏边界，不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：自定义引擎仍由调用方提供，未实现 Qt 桌面版应用级全局 DPR 查询、动态
插件发现与平台主题缓存；协议和像素尺寸使用项目的 `float`/整数接口，不承诺 Qt
内部 `qreal` 的双精度字节级一致性。

### 10.198 2026-08-29 QIcon 主题名称与文件路径判定

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1376-1388`：
  `QIcon::fromTheme()` 仅在 `QDir::isAbsolutePath()` 为真时把输入当作文件名；
  相对路径形式仍按主题名称查找。Qt `QDir::isRelativePath()` 的实现位于
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/io/qdir.cpp:2438-2441`，并明确将
  以冒号开头的 `:/...` 资源路径视为绝对路径。
- `qicon.cpp:1431-1437`：`QIcon::hasThemeIcon()` 通过 `fromTheme()` 后比较引擎
  名称，因此绝对文件路径不会被当作主题图标命中。

实现范围：

- `Src/XGui/Icon/XIcon.c:985-993` 让 `XIcon_fromTheme()` 对 Unix `/` 和 Qt 资源
  `:/` 绝对路径直接走普通文件图标构造，不把它们交给主题引擎。
- `Src/XGui/Icon/XIconThemeInternal.c:1554-1561` 将内部直接文件加载条件收窄为
  Unix 绝对路径 `/` 或 Qt 资源绝对路径 `:/`；`./icon`、`dir/icon` 等相对字符串
  继续进入主题和独立回退搜索路径，避免工作目录文件意外绕过主题规则。
- `Src/XGui/Icon/XIcon.c:1032-1042` 让 `XIcon_hasThemeIcon()` 对绝对文件路径直接
  返回 false（包括 `:/` 资源路径），与 Qt 的文件图标/主题图标区分一致。
- `xgui_regression_test.c:633-637` 增加相对独立文件和 Qt 资源路径不应被
  `hasThemeIcon()` 报告为主题图标的回归断言。

验证结果：默认和 `build-crop-painter-off` 的 XGui 回归目标及程序均通过，默认
CTest 1/1、`git diff --check` 通过；保留仓库既有构建警告和 10.186 所述 LSan 边界，
不能宣称零警告或零泄漏。修改未提交、未推送。

当前边界：路径语义按 Unix `/` 和 Qt 资源 `:/` 绝对路径实现；Windows 驱动器路径
以及平台主题动态发现仍属于嵌入式实现边界。

### 10.199 2026-08-29 QImage 灰度判定与元数据无变化语义

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2868-2942`：
  `QImage::allGray()` 对 Mono、MonoLSB 和 Indexed8 检查完整颜色表，而不是只检查
  被像素引用的条目；Alpha8 返回 false，Grayscale8/16 返回 true，空图返回 true。
- `qimage.cpp:2944-2974`：`QImage::isGrayscale()` 区分空图、原生灰度格式、深度
  32/24/16 的 `allGray()` 路径和 Indexed8 的规范表 `qRgb(i,i,i)`，Mono 的深度 1
  路径返回 false。
- `qimage.cpp:4112-4142`：`setDotsPerMeterX/Y()` 对零值和相同值直接早退，不触发
  元数据分离；`qimage.cpp:4166-4176`：`setOffset()` 对相同坐标直接早退。

实现范围：

- `Src/XGui/Graphics/XImage.c:1070-1102,2646-2672,2723-2734,2831-2871`：补齐
  `XImage_isGrayscale()` 的 Qt 深度/格式分支；`XImage_allGray()` 按完整颜色表
  判定索引和单色图像，并正确处理空图、Alpha8、Grayscale8/16；dpm 与 offset
  setter 对零值、相同值以及分离失败均无副作用。
- `xgui_regression_test.c:7996-8060,14174`：新增空图、索引色未引用调色板、规范
  灰度表、Mono/原生灰度格式以及 metadata cacheKey 无变化回归。

验证结果：默认配置 `cmake --build build --target XGuiRegression_Test -j1` 和
`./bin/XGuiRegression_Test` 通过；`git diff --check` 通过。裁剪配置仍需主线程
串行完成；构建输出保留仓库既有 XSignal/XEvent 等警告，不能宣称零警告或零泄漏。

当前边界：XImage 支持的浮点、16/64 位及 CMYK 格式继续通过便携 ARGB 读取路径
判断灰度，数值转换遵循当前 XImage 像素解码精度；ICC/LUT 色彩空间和 Qt 桌面版
隐式共享私有数据布局仍不属于此 C99 嵌入式实现。

### 10.200 2026-08-29 QImageReader 描述文本解析边界对齐

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:6468-6483`：
  `qt_getImageTextFromDescription()` 按空行分段，冒号前出现空格时将整段作为
  `Description`，普通键保留原始键空白并对值执行 `simplified()`；无冒号段仍按
  `left(-1)` 与 `mid(index + 2)` 规则处理。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:560-570,882-903`：
  `QImageReaderPrivate::getText()` 惰性读取 `Description`，`textKeys()` 返回有序
  键，`text(key)` 返回对应值或空字符串。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:315-409`：描述解析改用 `XString` UTF-16 字符
  索引，而不是 UTF-8 字节偏移，修复非 ASCII 键在冒号处的边界；保持 Qt 的
  `Description`/普通键分支、值跳过冒号后一个字符、无冒号段 `mid(1)` 和空键过滤
  语义。
- `xgui_regression_test.c:5392-5402,5955-6005`：插件描述加入无冒号 `BareText`
  段，验证键按 QMap 顺序返回、值按 Qt 规则得到 `areText`，同时保留原有普通键
  空白、Description 简化和缺失键空值覆盖。

验证结果：默认配置 `cmake --build build --target XGuiRegression_Test -j1`、
`./bin/XGuiRegression_Test`、`ctest --test-dir build --output-on-failure`（1/1）
通过；`build-crop-painter-off` 的 XGuiRegression 目标、CTest 1/1 也通过；
`git diff --check` 通过。构建保留仓库既有 XSignal/XEvent 等警告，且没有可用的
LSan 专用可执行文件，不能宣称零警告或零泄漏。

当前边界：Qt 的 QString 使用 UTF-16 代码单元，XString 同样使用 UTF-16；C 兼容
接口仍以 UTF-8 输入输出。动态图片插件发现、桌面版 `QFactoryLoader` 元数据和
非 Description 专用图像文本选项仍由嵌入式插件注册表承载。

### 10.201 2026-08-29 QImage 元数据分离与 QImageReader 变换选项

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1139-1159`：
  `QImage::detachMetadata()` 只在隐式共享时复制数据，并由调用方选择是否递增
  `detach_no`；`qimage.cpp:4112-4142,4166-4174` 对 dpm 和 offset 的零值、相同值
  直接早退。
- `qimage.cpp:4184-4236`：文本键按有序映射返回，`text("")` 将键和值以
  `": "` 和空行连接，普通值使用 `simplified()`；`qimage.cpp:4509-4528`：
  `cacheKey()` 由序列号和分离序号构成，元数据写入不应伪装为像素变化。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1010-1015`：
  `supportsAnimation()` 通过处理器的 `Animation` 选项查询；
  `qimagereader.cpp:1060-1080,1170-1280`：`autoTransform` 在读取成功后应用处理器
  方向，且先完成高 DPI 文件名后缀处理。
- `qimage.cpp:6443-6455`：`qt_imageTransform()` 的方向顺序为先镜像/翻转，再做
  90 度旋转；`Rotate270` 单独走逆时针旋转路径。

实现范围：

- `Src/XGui/Graphics/XImage.c:632-642,2031-2060,2394-2432,2656-2825`：新增
  元数据专用分离路径，色彩空间、文本、DPI、DPR 和 offset 的修改只在共享时
  克隆数据；唯一数据的 `cacheKey` 保持不变，分离失败时不写入。镜像和矩阵变换
  复制完整物理元数据、文本及色彩空间，避免变换结果丢失 QImage 元数据。
- `Src/XGui/Graphics/XImageReader.c:1243-1265,1268-1321,1404-1676`：动画能力
  查询除内置 GIF 外透传通用处理器的 `Animation` 选项；读取成功且启用
  `autoTransform` 时，按 Qt 顺序实现 Mirror、Flip、Rotate180、Rotate90、
  Rotate270 及组合方向。
- `xgui_regression_test.c:5968-6057,8046-8140,8148-8224`：覆盖自定义处理器的
  动画/方向选项、顺时针 90 度像素方向、镜像元数据保留，以及唯一/共享图像的
  元数据 cacheKey 与文本隔离语义。

验证结果：`build-crop-painter-off` 的
`cmake --build build-crop-painter-off --target XGuiRegression_Test -j1` 目标构建已完成，
随后运行 `./bin/XGuiRegression_Test` 通过；默认配置
`cmake --build build --target XGuiRegression_Test -j1` 和
`ctest --test-dir build --output-on-failure`（1/1）也通过。并行写入期间曾出现
`QImage format toPixelFormat/toImageFormat round trip` 与
`QImage fill(QColor) stores premultiplied RGB without double conversion` 两项暂时失败，
在相关 `XImageFormat`/`fillColor` 改动完成后已不再复现，当前不属于本轮失败。构建
输出仍保留仓库原有 `XSignal`/`XEvent` 等警告，且没有可用的 LSan 专用可执行文件，
不能宣称零警告或零泄漏；`git diff --check` 当前通过。

当前边界：`XColorSpace` 仍未实现 ICC profile、逐通道 LUT 和 `QColorTransform`
矩阵；自动方向使用现有便携浮点最近邻矩阵变换，不能承诺 Qt 内部高精度缩放的
字节级一致性。动态图片插件发现和桌面版 `QFactoryLoader` 元数据仍由嵌入式插件
注册表承载。

### 10.202 2026-08-29 QPixelFormat 静态映射与 QColor 填充对齐

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5940-6429`：
  `pixelformats` 静态表、`QImage::pixelFormat()`、`toPixelFormat()` 和
  `toImageFormat()` 的格式顺序、通道位数、Alpha 用法、预乘状态、类型解释及
  字节序语义。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qpixelformat.h:68-161,209-238`：
  `ColorModel`、Alpha 枚举、`channelCount()`、扩展通道和
  `CurrentSystemEndian` 解析规则。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1847-1930`：
  `QImage::fill(const QColor&)` 对预乘、索引、单色、灰度和浮点格式的分支语义。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.h:219-230`：
  `mirror()` 与 `rgbSwap()` 的兼容别名接口。

实现范围：

- `Src/XGui/Graphics/XImageFormat.h:73-163` 扩展 `XPixelFormat` 模型、Alpha
  用法/位置、预乘、类型解释、字节序、第四/第五通道和 YUV 子枚举字段；保留
  原有 `XPixelFormatModel_Mono`/`XPixelFormatModel_Gray` 名称，并提供
  `Grayscale`、`UsesAlpha`、`IgnoresAlpha` Qt 兼容别名。
- `Src/XGui/Graphics/XImageFormat.c:121-422` 以 Qt 静态表逐格式构造完整描述，
  将 `CurrentSystemEndian` 解析为实际本机端序；`XPixelFormat_equals()` 比较
  所有存储语义（忽略仅为旧接口保留的 `m_byteOrdered` 标志），反向转换保持
  Qt 的等价格式首项规则（`MonoLSB` 回映射为 `Mono`）。
- `Src/XGui/Graphics/XImage.c:1048-1105,1668-1820` 新增 QColor 语义填充，覆盖
  预乘/非预乘 RGB、16/24/30/64/浮点、索引色、单色及 CMYK；`XImage_mirror()`
  与 `XImage_rgbSwap()` 提供 Qt 兼容就地别名。
- `xgui_regression_test.c:7954-8030,14345` 验证全部公开格式往返、Alpha/索引/
  灰度字段、预乘颜色填充、单色/索引调色板分支及别名调用。

验证结果：默认配置全量构建 `cmake --build build -j1`、
`./bin/XGuiRegression_Test`、`ctest --test-dir build --output-on-failure`（1/1）
通过；裁剪配置 `build-crop-painter-off` 也完成 `cmake --build
build-crop-painter-off -j1` 全量构建、`./bin/XGuiRegression_Test` 和 CTest（1/1），
两套构建输出仅保留仓库已有的 `XSignal`、`XEvent`、函数指针兼容性等警告。
当前环境没有可用的 LSan 专用可执行文件，不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：`XPixelFormat` 为 C 结构体而非 Qt 的 64 位压缩值，新增字段保持可读性
和嵌入式 ABI；YUV/HSL/HSV 等模型仅提供描述枚举，当前 XImage 存储格式没有对应
具体转换器。桌面 Qt 动态格式插件与私有 `QPixelLayout` 仍不属于本实现范围。

### 10.203 2026-08-29 QColorSpace isValidTarget 目标空间判定

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.h:126-130`：
  `TransformModel`、`ColorModel` 和 `isValidTarget()` 的公开 API 位置。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:1159-1181`：
  三分量矩阵模型要求空间本身有效；元素列表模型则要求存在有效源变换。

实现范围：

- `Src/XGui/Graphics/XColorSpace.h:196-205` 新增完整中文注释和
  `XColorSpace_isValidTarget()` 声明。
- `Src/XGui/Graphics/XColorSpace.c:284-291` 按当前 C99 值类型的能力执行目标判定：
  仅当空间有效且变换模型为 `ThreeComponentMatrix` 时返回 true，不伪造 Qt
  `ElementListProcessing` 的单向变换表。
- `xgui_regression_test.c:7638-7648` 覆盖无效空间拒绝及命名 Display P3 接受。

验证结果：默认和 `build-crop-painter-off` 配置的 `XGuiRegression_Test` 均重新构建、
运行通过，CTest 均为 1/1；`git diff --check` 通过。构建输出仍保留仓库已有
`XSignal`/`XEvent` 等警告，环境没有可用的 LSan 专用可执行文件，不能宣称零警告
或零泄漏；修改未提交、未推送。

当前边界：XColorSpace 仍不承载 ICC 原始 profile、逐通道 LUT 及完整
`ElementListProcessing` 数据，因此目标判定只能覆盖三分量矩阵模型；相关扩展需先
设计可裁剪的资源容器，不能用布尔标志近似 Qt 的可逆性。

### 10.204 2026-08-29 QColorSpace 白点读写 API

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.h:102-107,126-130`：
  `whitePoint()`、`setWhitePoint()` 以及颜色模型和变换模型查询接口。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:1026-1069`：
  白点读取、空对象返回空坐标、写入后将预定义原色转为 Custom，以及无效传递函数
  仍保留白点元数据的行为。

实现范围：

- `Src/XGui/Graphics/XColorSpace.h:247-269` 新增 `XColorSpace_whitePoint()` 和
  `XColorSpace_setWhitePoint()` 的完整中文注释与声明。
- `Src/XGui/Graphics/XColorSpace.c:325-352` 读取当前值类型保存的白点；设置时校验
  有限 CIE xy 范围，清除命名空间和描述，并保持 Qt 对未完成传递函数的无效状态。
  未定义模型设置白点后标记为 Gray，便于后续补充传递函数。
- `xgui_regression_test.c:7653-7688` 覆盖 Display P3 白点、白点修改后的 Custom
  语义、灰度白点查询，以及无效空间保留白点元数据。

验证结果：默认配置 `XGuiRegression_Test` 目标构建、回归程序和 CTest 1/1 通过；
`build-crop-painter-off` 目标构建和 CTest 1/1 同样通过；`git diff --check` 通过。
构建输出仍有仓库既有 `XSignal`/`XEvent` 等警告，环境没有可用的 LSan 专用可执行文件，
不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：项目值类型没有 Qt 私有 `QColorMatrix`，因此设置白点只更新可见元数据，
不执行 Qt 的 Bradford 色彩适应和 RGB 矩阵重标定；ICC profile、逐通道 LUT 和
`ElementListProcessing` 仍未实现。

### 10.205 2026-08-29 QColorSpace 原色与传递函数值操作

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:832-949`：
  `setTransferFunction()`、`withTransferFunction()` 的 Custom 早退、源对象不变和
  描述清除语义。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:965-1023`：
  `setPrimaries()` 预定义原色替换、RGB 模型和命名空间重置语义。

实现范围：

- `Src/XGui/Graphics/XColorSpace.h:268-307` 新增
  `XColorSpace_setPrimaries()`、`XColorSpace_setTransferFunction()` 和
  `XColorSpace_withTransferFunction()`，公共注释全部为中文。
- `Src/XGui/Graphics/XColorSpace.c:50-66,350-420` 按 RGB/Gray 模型重算有效性；
  Custom 输入按 Qt 语义忽略，修改会清除命名空间和描述文本，副本接口不改变源值。
- `xgui_regression_test.c:7689-7715` 覆盖 sRGB 到 Linear 的副本转换、BT.2020
  原色替换、描述清除以及 Custom 传递函数早退。

验证结果：默认配置 `XGuiRegression_Test` 构建、运行通过；
`build-crop-painter-off` 目标构建、回归和 CTest 1/1 通过；`git diff --check` 通过。
构建输出保留仓库既有 `XSignal`/`XEvent` 等警告，环境没有可用的 LSan 专用可执行文件，
不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：传递函数表重载、ICC profile 和 Qt 私有矩阵仍未加入 C99 值类型；
`setPrimaries()` 仅替换保存的原色坐标，不执行 Qt 内部色彩适应矩阵重建。

### 10.206 2026-08-29 QColorSpace 转换前置条件与 Gamma 短路修正

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:832-850`：
  `setTransferFunction()` 先用调用者传入的原始 Gamma 判断无变化，再清除描述并
  归一化预定义传递函数的默认 Gamma。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5046-5065,5114-5126,5199-5209`：
  QImage 色彩转换要求目标通过 `isValidTarget()`，并在显式变换时拒绝无效或不兼容
  的源模型，不能只写入目标元数据。

实现范围：

- `Src/XGui/Graphics/XColorSpace.c:398-410` 调整 setter 的比较顺序，重复设置
  `sRGB, 0` 会保持原有描述文本，随后才按 Qt 规则补入近似 Gamma。
- `Src/XGui/Graphics/XImage.c:735-815` 将 `convertedToColorSpace()`、
  `convertToColorSpace()` 和显式变换统一收紧到有效目标；显式变换在源空间无效或
  与像素模型不兼容时清空输出，避免产生伪造的目标色彩空间。
- `xgui_regression_test.c:7758-7767` 新增 `ElementListProcessing` 目标拒绝回归。

验证结果：默认和 `build-crop-painter-off` 配置的 XGuiRegression_Test 目标构建、
回归程序及 CTest 均通过；`git diff --check` 通过。构建仍有仓库既有的
`XSignal`、`XEvent`、zlib 条件指令及函数指针兼容性警告；当前环境无可用 LSan
专用可执行文件，不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：C99 值类型仍未保存 Qt 的 ICC/LUT/ElementList 数据，像素转换实现只处理
传递函数曲线，尚未重建不同原色/白点之间的 XYZ 矩阵；在补齐可裁剪矩阵协议前，
不得将不同原色转换描述为与 Qt 完全等价。

### 10.207 2026-08-29 Alpha8 非预乘像素格式映射修正

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:6229-6241`：
  `QImage::Format_Alpha8` 使用单独的 Alpha 模型和 `NotPremultiplied` 标志；
  该格式仅存储透明度分量，不应被解释为预乘颜色。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5940-6410`：
  Qt 的像素格式映射表保持 Alpha8 的非预乘语义，转换到 `QPixelFormat` 时不能
  沿用带颜色通道格式的预乘默认值。

实现范围：

- `Src/XGui/Graphics/XImageFormat.c:320-325` 将 Alpha8 映射的 alpha 标志改为
  `XPixelFormatAlpha_NotPremultiplied`，其余模型、位宽和类型保持不变。
- `xgui_regression_test.c:8039-8045` 增加非预乘断言，防止 Alpha8 映射回归为预乘。

验证结果：默认配置和 `build-crop-painter-off` 配置的 `XGuiRegression_Test` 目标
均重新构建，回归程序及 CTest 均通过；`git diff --check` 通过。构建输出仍有仓库
已有 `XSignal`/`XEvent` 等警告，环境没有可用的 LSan 专用可执行文件，不能宣称
零警告或零泄漏；修改未提交、未推送。

当前边界：项目 `XPixelFormat` 辅助枚举值与 Qt 内部枚举值不要求数值相同，映射
通过显式表保持语义兼容；其他格式的通道排列和预乘规则仍以该表为唯一来源。

### 10.208 2026-08-29 QImageReader 格式确认与无效裁剪边界

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:143-254,258-337`
  的 `createReadHandlerHelper()`：静态设备格式查询必须先创建处理器并调用
  `canRead()`，只有确认内容可读后才接受处理器格式；内置处理器仍可按内容回退。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/tools/qrect.h:164-171`：
  `QRect(width,height)` 的 `isNull()` 只表示宽高同时为零，而 `isValid()` 要求
  左上角不超过右下角，因此零宽或零高的非 null 矩形仍然无效。

实现范围：

- `Src/XGui/Graphics/XImageReader.c:1459-1468,1823-1858`：软件裁剪的有效性
  改为宽高均大于零；`XImageReader_imageFormatDevice()` 改为使用空格式创建
  handler，调用 `canRead()` 后复制其格式，失败时再回退到固定编解码器签名，
  避免仅凭插件 `capabilities()` 对错误数据误报格式。
- `xgui_regression_test.c:6696-6744,14445`：新增零宽、零高非空裁剪
  回归，确认读取成功且保持完整图像尺寸。

验证结果：默认配置 `cmake --build build --target XGuiRegression_Test -j1` 通过，
`./bin/XGuiRegression_Test` 通过，`git diff --check` 通过。构建仍有仓库既有的
`XSignal`/`XEvent` 等函数指针与限定符警告，当前环境没有可用 LSan 专用可执行文件，
不能宣称零警告或零泄漏。

当前边界：`XImagePluginRegistry` 仍是嵌入式静态注册表，不执行 Qt
`QFactoryLoader("imageformats")` 的动态目录扫描；完整 Qt 图像处理器选项和动态
插件热加载仍未实现。

### 10.209 2026-08-29 QIcon 主题引擎尺寸选择与稳定键

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:783-787,849-873`：
  `QIconLoaderEngine::entryForSize()` 以请求矩形的较小边匹配主题资源，绘制阶段
  再将选中的 pixmap 铺到完整目标矩形；非正方形请求不能只用宽度决定资源档位。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:572-610`：
  主题引擎 `key()` 返回固定的引擎类型名，图标名称由独立的 `iconName()` 接口
  提供，不能把名称拼接进引擎键值。

实现范围：

- `Src/XGui/Icon/XIconThemeEngine.c:31-68` 在 `paint()` 中按目标矩形较小边选择
  主题资源，再按 Qt 行为缩放到完整目标矩形，保留状态样式处理和 painter 保存/
  恢复语义。
- `Src/XGui/Icon/XIconThemeEngine.c:179-185` 将主题引擎键固定为
  `QIconLoaderEngine`，与 `xgui_regression_test.c:5010-5014` 的既有回归断言一致。

验证结果：默认配置和 `build-crop-painter-off` 配置均完成全量构建，CTest 的
`XGuiRegression` 均为 1/1 通过；`git diff --check` 通过。构建输出仍包含仓库已有
的 `XSignal`、`XEvent`、zlib 条件指令及函数指针兼容性警告，环境无可用的 LSan
专用可执行文件，不能宣称零警告或零泄漏；修改未提交、未推送。

当前边界：主题目录解析和动态 `QFactoryLoader` 插件发现仍采用项目静态注册表；
完整 Qt 引擎的多状态缓存、SVG 原生渲染和平台主题监听尚未加入嵌入式裁剪实现。

### 10.210 2026-08-29 XImage/XPixmap 生命周期与泄漏复核

本轮依据现有 Qt 6.8 图像值语义及项目对象生命周期约束，对 ASan/LSan 报告逐条复核：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1847-1930`：
  QImage 隐式共享数据在重新赋值或销毁前必须释放旧引用，分离后的数据由最后一个
  引用负责回收；测试不得在仍持有像素数据时直接覆盖值对象。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmap.cpp:550-610`：
  QPixmap 从图像构造会替换内部数据，但对象本身的分配类别不随数据替换改变。

实现范围：

- `xgui_regression_test.c:7955-7959` 在复用已有 `XImage` 前显式调用
  `XImage_deinit_base()`，覆盖共享数据分离后再次初始化的释放路径，避免测试代码
  绕过引用计数直接覆盖对象而泄漏克隆数据。
- `xgui_regression_test.c:1307-1308,1329-1330` 在栈对象传入
  `XPixmap_init_image()` 前显式初始化对象，避免裁剪配置下未初始化虚表被误判为可
  反初始化对象。
- `Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:1322-1398` 和
  `Drive/windows/Graphics/XPlatformNativeWindow_win32.c:1036-1149` 的真实抓屏路径
  先在栈上调用 `XPixmap_init_image()`，再用 `XPixmap_move_base()` 转移到
  `XPixmap_create()` 返回的堆对象；这样不读取未初始化栈对象的虚表，也不会因
  `XPixmap_init_image()` 清零堆对象而丢失 `delete_base()` 所需的堆标志。
- `Src/XGui/Graphics/XPixmap.h:88-97` 补充初始化前置条件的中文注释，明确重复
  初始化应由调用方先执行 `XPixmap_deinit_base()`。

验证结果：

- `build-asan` 目标构建及 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` 回归程序通过
  项目自有泄漏检查；LSan 仅报告 Mesa `libGLX_mesa.so.0` 和 Fontconfig 的进程级
  缓存分配（约 101 KiB），没有 `XinYueC` 栈帧，不能将环境缓存泄漏宣称为项目零泄漏。
- 默认 `build` 与 `build-crop-painter-off` 配置随后均完成全量构建，回归程序和 CTest 的
  `XGuiRegression` 均为 1/1；`git diff --check` 通过。构建仍有仓库既有
  `XSignal`/`XEvent`、zlib 条件指令和限定符相关警告，未宣称零警告；修改未提交、未推送。

当前边界：`XImage_init_ex()` 对未初始化对象和已初始化对象的调用约定仍由调用方负责，
本轮仅修复已发现的测试复用点；若未来需要公开“重新初始化”接口，应新增显式
`reinit` 函数而不能依赖读取未初始化对象的虚表指针。

### 10.211 2026-08-29 QPicture 背景状态记录与回放

本轮按 Qt 6.8 `QPicturePaintEngine::updateBackground()` 对齐背景状态的两条独立
记录：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:219-231`：
  先写入 `PdcSetBkColor` 的颜色，再写入 `PdcSetBkMode` 的透明/不透明模式；
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:833-840`：回放时分别
  调用背景颜色与背景模式接口。
- `Src/XGui/Graphics/XPicture.h:52-53,264-267` 新增连续 opcode 17/18 及固定四字节
  负载接口；`XPicture.c:247-262,754-782,1260-1290` 完成长度校验、录制和直接状态
  回放，避免 Picture 后端重入时追加重复记录。
- `Src/XGui/Graphics/XPainter.c:1764-1789,1790-1851` 在背景状态发生设置时记录，
  背景模式沿用 Qt 对同值设置的早退语义；
  回放将颜色同步到可选实心背景画刷，裁剪关闭时仍消费记录以保持流兼容。
- `xgui_regression_test.c:4599-4640` 新增背景颜色/模式录制与回放断言，并纳入主回归
  调用；默认与 `build-crop-painter-off` 配置的目标构建、CTest 和 `git diff --check`
  均通过；ASan/LSan 运行未发现 XinYueC 堆栈泄漏，仅报告 Mesa 与 Fontconfig 的
  进程级缓存分配。

当前边界：Qt 的 `QBrush` 背景对象可包含渐变、纹理和变换，当前便携流只表达固体
ARGB 颜色及两种背景模式；完整画刷资源协议仍待后续设计，不能用单一颜色近似宣称
完整 `QBrush` 字节级兼容。

### 10.212 2026-08-29 QPicture 基础画刷状态记录

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:176-184`：
  `QPicturePaintEngine::updateBrush()` 在画刷状态变脏时写入 `PdcSetBrush`，其
  负载包含完整 `QBrush` 对象。

实现范围：

- `Src/XGui/Graphics/XPicture.h:54,270-278` 新增 `SetBrush=19` 及
  `XPicture_recordSetBrush()`；协议采用 4 字节样式值和 4 字节 ARGB32 颜色的
  固定小端负载，校验器严格检查 8 字节长度。
- `Src/XGui/Graphics/XPicture.c:247-262,780-790,1211-1320` 完成基础画刷的流校验、
  记录与直接状态回放；回放不经公共 setter，避免 Picture 目标重复追加记录。
- `Src/XGui/Graphics/XPainter.c:1787-1801,4373-4385,4389-4405,4511-4533`：
  画刷颜色、样式 setter 在 Picture 后端追加基础画刷记录；`setBrush(QColor)`
  同时重置为实心样式并清空旧渐变载荷。
- `xgui_regression_test.c:4756-4797,14554-14556`：新增固定记录长度、回放颜色和
  样式断言，并纳入默认与裁剪配置主回归。

验证结果：默认 `build` 与 `build-crop-painter-off` 配置的 XGuiRegression_Test
目标构建、回归程序和 CTest 均通过；`git diff --check` 通过。构建仍有仓库既有
`XSignal`/`XEvent` 等警告，ASan/LSan 仅报告 Mesa 与 Fontconfig 进程级缓存，未
发现 XinYueC 代码栈泄漏；未宣称零警告或零泄漏，修改未提交、未推送。

当前边界：该 opcode 只表达普通样式和 ARGB32 基色；渐变、纹理、画刷变换及其
资源数据未进入便携协议，因此不能替代 Qt 完整 `QBrush` 序列化。

### 10.213 2026-08-29 QIcon 索引主题权威性与独立回退顺序

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:347-420`：
  主题目录存在 `index.theme` 即视为有效索引主题；图标查找只遍历索引声明的
  `contentDirs` 和 `keyList`，未声明的根目录文件不能绕过索引。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:446-570`：
  主题条目仅接受带有效 `Size` 的目录组；`Scale`、`Threshold` 按元数据原值参与
  尺寸匹配。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:572-607`：
  独立回退文件按 `fallbackSearchPaths()` 目录顺序，依次检查 PNG、XPM、SVG，
  命中首个存在文件后停止后续搜索。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:783-873`：
  固定/可缩放资源在目标尺寸大于源尺寸时按 KeepAspectRatio 缩小，不对较小回退
  文件强制放大；非正方形目标按较小边选资源。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:46-60,111-125,246-273` 增加独立回退后缀
  白名单、文件存在性检查和缓存偏移加法溢出防护；避免损坏或越界的主题缓存
  误读为有效索引。
- `Src/XGui/Icon/XIconThemeInternal.c:660-678,855-910,1119-1143,1314-1330`：
  无 `Size` 的索引组标记为无效，`Scale=0` 和负 `Threshold` 不静默归一化；空
  `Directories` 的索引主题仍阻止 legacy 目录探测，并继续按 `Inherits` 回退。
- `Src/XGui/Icon/XIconThemeInternal.c:1466-1541,1649-1658`：索引主题存在时
  禁止根目录 legacy 文件越过 `Directories`；独立回退只在 fallback 搜索路径中
  按 Qt 顺序选取首个存在文件，且保留文件实际矩形尺寸和按比例缩小语义。
- `xgui_regression_test.c:436-488,491-576,578-691,14498-14501`：新增未声明根
  目录图标拒绝、fallbackThemeName 传统主题、独立回退路径/尺寸及首个文件优先级
  回归夹具。

验证结果：默认 `build` 与 `build-crop-painter-off` 配置的 XGuiRegression_Test
目标构建、回归程序和 CTest 均通过；`git diff --check` 通过。构建仍保留仓库既有
`XSignal`/`XEvent` 等警告，ASan/LSan 仅见 Mesa 与 Fontconfig 进程级缓存分配，未
发现 XinYueC 代码栈泄漏；未宣称零警告或零泄漏，修改未提交、未推送。

当前边界：主题解析仍是静态 C 实现，不提供 Qt `QFactoryLoader` 动态插件扫描、
GTK 缓存热更新或平台主题监听；XPM 解码能力继续受 `XIMAGECODEC_XPM_ON` 裁剪开关
限制。

### 10.214 2026-08-29 QIcon GTK 缓存偏移回绕安全复核

本轮继续对照 Qt 6.8 的缓存读取边界：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:232-248`：
  `read16()`/`read32()` 每次读取前检查对齐与文件范围；
  `:252-282`：缓存初始化校验目录列表；`:297-345`：哈希桶、节点和目录列表
  的每个偏移都必须保持有效。

实现范围：

- `Src/XGui/Icon/XIconThemeInternal.c:153-176` 增加
  `theme_cacheAddOffset()` 和 `theme_cacheRange()`；哈希表、目录表、链表节点及
  图标目录列表的长度和下标计算，均先检查 32 位偏移回绕，再执行文件范围读取。
- `theme_cacheFresh()` 与 `theme_cacheDirState()` 对有效缓存的目录匹配、时间戳判定和
  回退顺序保持不变；畸形缓存按 Qt 语义降级为无效缓存并回到完整目录扫描。

验证结果：默认 `build` 与 `build-crop-painter-off` 的 `XGuiRegression_Test` 目标
构建和回归均通过；`git diff --check` 通过。测试输出中的既有 `XWindow`/`XClass`
错误日志未新增失败；修改未提交、未推送。

当前边界：仍只读取 GTK `icon-theme.cache` 版本 1，不生成或更新缓存，也未实现 Qt
桌面端动态主题插件、缓存监听和多状态 `QPixmapCache`；本轮仅补充畸形偏移的整数范围
与回绕安全性。

### 10.215 2026-08-29 默认与裁剪配置最终回归复核

本轮未新增 API，复核当前 22 个修改文件的构建产物与测试覆盖：

- `build-crop-painter-off`：`XGuiRegression_Test` 目标重新编译并运行通过，确认
  关闭 `XPainter` 绘制扩展开关时，图像、图标主题、Picture 流校验和布局回归仍可
  正常裁剪链接。
- `build`：默认配置目标重新构建后运行 `./bin/XGuiRegression_Test` 通过，`ctest
  --test-dir build --output-on-failure` 为 1/1 通过。
- `git diff --check` 通过；工作树仅保留本轮既有 22 个修改文件，未提交、未推送。

构建输出继续包含仓库既有 `XSignal`、`XEvent`、容器继承接口等兼容性警告；ASan/LSan
复核仍只见 Mesa 与 Fontconfig 的进程级缓存分配，未见 XinYueC 代码栈泄漏，因此不
宣称零警告或零泄漏。

当前边界：本次仅完成最终配置复核，不扩展 Qt 桌面版动态插件扫描、ICC/LUT 色彩
配置、复杂 `QBrush` 资源协议或 GTK 缓存生成/热更新；这些边界沿用 10.189--10.214。

### 10.216 2026-08-29 QImage 显式目标格式的颜色变换模型切换

本轮依据 Qt 6.8 的 `QImage::colorTransformed`/`applyColorTransform` 语义复核显式
目标格式路径：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5199-5209`：不带目标
  格式的 `applyColorTransform` 在源/目标模型不兼容时拒绝无需格式切换的变换；
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5360-5390`：不带目标
  格式的 `colorTransformed` 在模型切换时选择 RGB、灰度或 CMYK 的兼容默认格式；
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5445-5462`：带显式
  `toFormat` 的重载只校验所选输出格式能否承载目标颜色模型，允许调用方明确请求
  RGB 到 `Grayscale8` 等模型切换。

实现范围：

- `Src/XGui/Graphics/XImage.c:809-895` 调整 `XImage_applyColorTransform` 的前置
  校验：先校验源空间和源像素模型，再将显式输出格式映射到其自身颜色模型；不再
  用源像素模型错误拦截合法的显式模型切换。未指定格式时仍按目标模型选择
  `ARGB32`、`Grayscale8` 或 `CMYK8888` 默认输出。
- `xgui_regression_test.c:7789,7909,7938-7953,8170-8175` 新增独立输出图像和
  RGB 源到灰度目标的回归断言，并按 `XImage_init`/`XImage_deinit_base` 生命周期
  管理对象。

验证结果：默认 `build` 与 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标均构建成功，回归程序通过；默认 `ctest --test-dir build
--output-on-failure` 为 1/1 通过；`git diff --check` 通过。构建仍保留仓库既有
`XSignal`、`XEvent`、限定符和容器接口警告，ASan/LSan 仅见 Mesa/Fontconfig 进程级
缓存分配，未宣称零警告或零泄漏。

当前边界：颜色变换仍采用项目现有的传递函数近似，不包含 Qt ICC/LUT、矩阵精度及
高位深自动格式选择；无目标格式的便捷重载尚未另行扩展，调用方需使用现有带格式
接口传入 `XImageFormat_Invalid` 或明确格式。

### 10.217 2026-08-29 QImage 掩码工厂的位序与边缘语义

本轮对照 Qt 6.8 图像实现：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3109-3125`：
  `createAlphaMask()` 对空图像和 `Format_RGB32` 返回空图像，其余格式输出
  `Format_MonoLSB`，并通过 `dither_to_Mono(..., fromalpha=true)` 生成 Alpha 掩码，
  最后只复制物理元数据。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3153-3242`：
  `createHeuristicMask()` 使用四角投票选择背景色，从四条边剥离边缘连通背景，
  忽略 Alpha；`clipTight=false` 时额外保留非背景像素四邻域。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3257-3290`：
  `createMaskFromColor()` 输出 `MonoLSB`，对 32 位源比较完整 `QRgb`（包括 Alpha），
  `MaskOutColor` 再反转掩码位，并复制物理元数据。

实现范围：

- `Src/XGui/Graphics/XImage.c:1325-1521`：掩码初始化统一返回 `MonoLSB`；Alpha
  掩码使用白色/黑色两项颜色表和默认 `alpha >= 128` 阈值，RGB32 走 Qt 兼容的空图像
  分支；启发式掩码加入非 32 位源先转 `RGB32`、四角背景投票、边缘四邻域 BFS 和
  `clipTight` 扩展；颜色掩码按完整 ARGB 比较并支持 In/Out 两种模式。队列容量、
  字节乘法和项目内存分配均有溢出/失败保护。
- `Src/XGui/Graphics/XImage.h:387-413`：补充三个公共函数的中文参数、返回值和
  近似边界说明。
- `xgui_regression_test.c:8195-8272,14643-14646`：新增 Alpha 128 阈值、MonoLSB
  颜色表、ARGB Alpha 区分、MaskOut 反转、封闭背景孔洞及 `clipTight=false` 邻域
  回归断言。

同轮还完成高位深 `pixelColor()` 读取：对照
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2705-2760`，
`Src/XGui/Graphics/XImage.c:1236-1277,1600-1695` 对 `Grayscale16`、`RGBX64`、
`RGBA64` 和 `RGBA64_Premultiplied` 保留 16 位通道精度并按预乘规则反解；回归断言位于
`xgui_regression_test.c:8371-8400`。

验证结果：默认配置 `build` 的 `XGuiRegression_Test` 目标重新构建并运行通过；此前
默认与 `build-crop-painter-off` 的全量构建、回归程序和 CTest 均通过。构建仍有仓库
既有 `XSignal`、`XEvent`、zlib 条件指令和限定符警告，ASan/LSan 仅报告 Mesa 与
Fontconfig 的进程级缓存，未宣称零警告或零泄漏；`git diff --check` 通过，修改未提交、
未推送。

当前边界：`flags` 目前只实现 Qt 默认 Alpha 阈值模式，Ordered/Diffuse 抖动尚未进入
便携实现；深度为 1 的异常无颜色表输入未复刻 Qt 内部 Indexed8 转换的全部细节；启发式
队列分配失败时保留全不透明掩码作为降级结果。

### 10.218 2026-08-29 QImage 掩码深度一与整行反转补齐

本轮针对上一节留下的两个边界再次对照 Qt 6.8：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3120-3124`：深度为 1
  的 Alpha 掩码先转换到 `Format_Indexed8`，因此单色图像颜色表中的 Alpha 分量必须
  参与掩码生成。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3280-3290`：
  `createMaskFromColor()` 先将 `MonoLSB` 掩码清零，`MaskOutColor` 再调用
  `invertPixels()`，其按字节反转也会覆盖每行末尾的填充位。

实现与回归：

- `Src/XGui/Graphics/XImage.c:1353-1377` 对深度一源图像复用 `Indexed8` 转换后再
  生成 Alpha 掩码；`XImage.c:1523-1550` 显式清零输出并按字节完成 OutColor 反转，
  保持填充位与 Qt 一致。
- `xgui_regression_test.c:8239-8249,8251-8260` 新增单色颜色表 Alpha 检查和 3 像素
  掩码首字节 `0xfb` 的填充位反转检查。

默认 `build` 与 `build-crop-painter-off` 均完成全量构建、回归程序和 CTest；
`git diff --check` 通过。构建中的 `XSignal`、`XEvent` 等警告属于既有工程问题，
ASan/LSan 仍受 Mesa/Fontconfig 进程级缓存影响，未宣称零警告或零泄漏；修改未提交、
未推送。

### 10.219 2026-08-29 内置图像插件 JPEG 别名与 BMP ImageFormat 语义补齐

> 历史记录：SVGZ gzip 能力随后在 10.447 接入；本节“仍未注册”的描述仅代表
> 2026-08-29 当时状态。

本轮继续对照 Qt 6.8 图像处理器与插件实现：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/jpeg/jpeg.json:1-4`：
  JPEG 插件公开 `jpg`、`jpeg`、`jfif` 三个格式键，三个键均对应 `image/jpeg`；
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:191-205`
  说明插件元数据中的格式键、MIME 类型应保持一一对应。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:61-102`、
  `:858-895`：BMP 处理器接受 12 字节旧式 DIB 头；现代 32 位 V4/V5 位域且存在
  Alpha 掩码时，`option(ImageFormat)` 返回 `ARGB32`，普通 BI_RGB 返回 `RGB32`。

实现范围：

- `Src/XGui/Graphics/XImageCodec/XImageCodec.c:45-50` 增加 `jfif` 到 JPEG 格式名归一化，
  与已有 `jpg`/`jpeg` 共用同一编解码实现。
- `Src/XGui/Graphics/XImageBuiltinPlugin.c:18-28` 注册 `jfif`、`*.jfif` 和重复的
  `image/jpeg` MIME；`builtin_bmpImageFormat()` 同时解析旧式 12 字节 DIB、现代 DIB 的
  压缩字段和 V4/V5 Alpha 掩码，避免 ImageFormat 选项错误报告。
- 同文件 `VXImageBuiltinPlugin_create()` 在直接工厂调用时绑定传入格式；此前只有注册表
  外层会补设格式，直接创建后调用写入会因格式为空而失败，现与 Qt `create()` 契约一致。
- `xgui_regression_test.c:5696-5700,5720-5724` 将 JPEG 三键纳入内置插件发现断言。

当前边界：SVGZ 仍未注册，因为当前 SVG 便携解码器没有 gzip 解压；内置 Handler 的
`Size`、描述、压缩比等完整 Qt 选项仍需各格式协议/元数据支持，不能仅通过声明能力模拟。

### 10.220 2026-08-29 SVG Handler 头部探测边界收紧

> 历史记录：SVGZ 解压随后在 10.411/10.447 接入；本节仅记录当时的普通 SVG
> 头部识别修正。

本轮继续对照 Qt SVG 处理器的格式判断：

- `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/svg/qsvgtinydocument.cpp:42-50`：
  `hasSvgHeader()` 仅接受以 `<svg` 或 `<!DOCTYPE svg` 开头的内容；XML 声明或
  注释开头必须在同一探测缓冲区内继续出现 SVG 根元素/DOCTYPE，普通 XML 文档不能
  被识别为图像。
- `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/svg/qsvgtinydocument.cpp:538-570`：
  `isLikelySvg()` 将上述判断限制在设备前 4096 字节，并保留 BOM/压缩流探测边界。

实现范围：

- `Src/XGui/Graphics/XImageCodec/XImageCodec.c:99-132` 收紧 SVG 识别：删除“任意
  `<?xml` 即命中”的过宽分支，改为仅当 XML 声明/前置注释后续确实出现 `<svg` 或
  `<!DOCTYPE svg` 才返回 Svg；直接根元素与 DOCTYPE 仍保持原有快速路径。长度判断改为
  减法形式，避免不可信 `size_t` 加法回绕。
- `xgui_regression_test.c:6928-6950` 增加普通 XML 拒绝和 XML 后续 SVG 接受的头部夹具。

验证结果：默认 `build` 的 `XGuiRegression_Test` 目标构建、回归程序和 CTest 均通过；
保留既有 `XSignal`/`XEvent` 等工程警告。SVG 识别仍是便携 ASCII 前缀探测，不包含
Qt 的 UTF-16 `QTextStream` 解码和 gzip `svgz` 内容膨胀；这些能力受当前编解码器裁剪
边界限制，未将探测近似宣称为完整 Qt 实现。修改未提交、未推送。

### 10.221 2026-08-29 SVG Handler 长前导探测窗口

> 历史记录：本节关于 SVGZ gzip 尚未解压的结论已由 10.411/10.447 更新。

Qt `QSvgTinyDocument::isLikelySvg()` 在设备前 4096 字节内检查 XML 声明、注释和
DOCTYPE 后的 SVG 根元素（`qsvgtinydocument.cpp:538-570`）。内置插件此前只窥视
16 字节，导致合法但前导较长的 SVG 在 `canRead()` 和注册表探测中被错误拒绝。

`Src/XGui/Graphics/XImageBuiltinPlugin.c:74` 现统一使用 4096 字节有界窥视，保留
其他格式探测的无副作用语义，并与 `XImageCodec_detect()` 的 XML 前缀规则一致。该
调整只扩大固定探测窗口，不改变任何格式编解码或内存分配策略。默认 `build` 与
`build-crop-painter-off` 的构建、XGuiRegression_Test 和 CTest 均已通过；SVGZ gzip
解压和 UTF-16 文本流仍属于未实现边界。

### 10.222 2026-08-29 QPicture 绘制状态记录扩展

对照 Qt Picture 绘制引擎的状态刷新路径：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine_pic.cpp:176-202`
  序列化画刷、画刷原点和不透明度；`:219-243` 序列化背景色、背景模式和变换矩阵；
  `:247-281`、`:483-496` 调度裁剪与渲染提示状态。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2093-2112`、
  `:6830-6883`、`:6953-7083` 分别定义画刷原点、不透明度/渲染提示和窗口、视口、
  视图变换启用状态的变更语义。

实现范围：`XPictureOpcode_SetOpacity` 至 `SetViewTransformEnabled`（12--22）采用
固定 little-endian 负载，新增严格长度校验、记录函数及回放状态恢复；`XPainter` 仅在
值实际变化时写入记录，批量渲染提示按位更新并保留 Qt 的 on/off 语义。画刷和背景目前
记录便携协议可表达的基础样式/颜色，完整 QBrush 渐变、纹理、复杂背景画刷及裁剪区域
仍未伪造为已支持；回放在对应裁剪开关关闭时仍消费记录以保持流兼容。

验证结果：默认与 `build-crop-painter-off` 均完成全量构建、XGuiRegression_Test、CTest，
并通过 `git diff --check`。现有 `XSignal`、`XEvent`、zlib 条件指令和限定符警告仍存在；
ASan/LSan 仅能观察到 Mesa/Fontconfig 进程级缓存，未宣称零警告或零泄漏。

### 10.223 2026-08-29 JPEG MIME 三别名与写入器能力查询

Qt JPEG 插件元数据 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/jpeg/jpeg.json:1-4`
公开 `jpg`、`jpeg`、`jfif` 三个格式键且均为 `image/jpeg`；QImage I/O 处理器元数据
契约见 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:191-205`。

`XImageReader` 与 `XImageWriter` 现在在 MIME 反查中返回三个 JPEG 别名，支持格式列表
同步包含 `jfif`，并按 Qt 规则对 MIME 值精确匹配后去重排序。写入器在已设置格式但尚未
绑定设备时创建空设备处理器，使 `supportsOption()` 可以查询格式能力；实际
`canWrite()`/`write()` 仍拒绝无设备状态，不会绕过 I/O 校验。新增回归断言覆盖读写器
三别名反查及 JPEG 开关关闭时的空结果；注册表的通用 MIME 回退表也将缺失元数据的
`jfif` 键归一到 `image/jpeg`。

边界：当前便携实现仍只提供 BMP、PNG、JPEG/JFIF、GIF、SVG 等已注册编解码器；SVGZ
压缩解码和其他插件的动态发现仍由裁剪配置决定。

### 10.224 2026-08-29 主题图标候选格式与独立回退排序

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:479-528`
的候选遍历及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:849-873` 的尺寸距离计算：
同一尺寸的主题条目优先选择 PNG，
再选择 SVG、XPM、BMP；精确尺寸优先于非精确尺寸，非精确时按距离及格式优先级
稳定选择。`lookupFallbackIcon()` 的独立文件回退只扫描
`fallbackSearchPaths()`，并按 PNG、XPM、SVG 后缀顺序处理（Qt 源码对应
`qiconloader.cpp` 的回退查找分支）。

`Src/XGui/Icon/XIconThemeInternal.c` 现记录扩展名优先级，分两阶段选择精确尺寸和
最小距离候选；带 `index.theme` 的主题不再被无索引旧式目录绕过，空
`Directories` 会继续走 `Inherits`。独立回退文件仅在文件确实存在时尝试一次，解码
失败不会错误抢占后续路径；`availableSizes()` 保留独立文件的原始矩形尺寸。缩放
仅在资源超过请求边界时执行，并使用 KeepAspectRatio，避免非方形图标被拉伸。

验证结果：默认 `build` 的 XGuiRegression_Test 构建、程序运行和 CTest 均通过，
`git diff --check` 通过。当前边界仍包括 Qt 桌面版动态主题插件发现、完整 SVG 解码
以及平台特定主题目录；这些能力受嵌入式资源和编解码器裁剪开关限制。

### 10.225 2026-08-29 BMP OS/2 Core Header 有符号尺寸语义

Qt 6.8 的 `read_dib_infoheader()` 在
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:188-208`
中将 12 字节 OS/2 Core Header 的宽高读入 `qint16`，随后按普通 BMP 规则拒绝
非正宽度、零高度，并对负高度执行倒行序读取；这与较大 Windows DIB 头使用
32 位有符号宽高的路径不同。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:162-178` 现在在
`dib == 12` 时先按 `int16_t` 解释宽高，再进入统一的尺寸/面积校验，因而拒绝
`0x8000` 等负宽度编码，同时保留负高度的顶行序语义。`xgui_regression_test.c`
的 BMP 畸形夹具新增负宽度拒绝和负高度两行 24 位像素顺序断言，覆盖解码结果的
尺寸及首尾颜色。

验证结果：默认 `build` 与 `build-crop-painter-off` 的增量目标构建、
`XGuiRegression_Test`、CTest 以及 `git diff --check` 均通过；构建仍仅报告工程
既有 XSignal/XEvent 等警告。BMP 的 OS/2 调色板、压缩格式和其他 DIB 版本仍受
现有 `XIMAGECODEC_BMP_*` 裁剪开关约束，未宣称超出开关范围的完整 Qt 支持。

### 10.226 2026-08-29 QIcon 同 DPR 候选按物理面积匹配

- Qt 依据：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:232-245`
  的 `bestSizeScaleMatch()` 在候选 DPR 相同时以 `area(size * scale)` 与条目的
  `size`（`QPixmap::size()` 物理像素）比较；`:417-427` 的 `addPixmap()` 也以
  物理尺寸和 DPR 判断同一条目。
- 修复：`XIconPrivate_bestSizeScaleMatch()` 不再把逻辑面积直接和物理请求面积
  混比，新增 `XIconPrivate_entryPhysicalArea()`，对已加载条目取 XPixmap 物理宽高，
  对延迟文件条目取请求尺寸；请求尺寸按 DPR 安全取整并限制到 `INT_MAX` 后比较，
  保持“最小但不小于请求”的 Qt 选择规则。
- 回归：`test_icon_device_pixel_ratio()` 加入 16x16 与 32x32 逻辑尺寸、同为 2x
  DPR 且颜色不同的资源，请求 10x10@2x 时确认选择较小的 16x16 资源并得到
  20x20 物理输出；默认与裁剪构建均通过。
- 边界：不同 DPR 的优先级、模式/状态回退和缓存键逻辑保持现有实现；Qt 平台
  图标引擎的系统主题与样式辅助仍受 XGui 可移植裁剪限制。

### 10.227 2026-08-29 QIcon addPixmap 重复条目替换

- Qt 依据：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:417-427`
  的 `QPixmapIconEngine::addPixmap()` 以 mode、state、物理像素尺寸和
  `devicePixelRatio()` 识别同一条目；命中后原地替换像素图并清空文件名，未命中
  才追加新条目。
- 修复：`XIconPrivate_addEntry()` 现在遍历同模式/状态条目，分别比较物理宽高和
  DPR；延迟文件条目按其请求尺寸和 DPR=1 参与匹配。命中后释放旧文件名、复制新
  XPixmap、清零请求尺寸并标记已加载，随后更新缓存键；面积相同但宽高不同的资源
  不会误替换。
- 回归：`test_icon_matching()` 先添加 16x16 绿色替换图，再以同模式/状态添加
  同尺寸资源，确认取图像素来自新资源且没有重复条目；默认与裁剪配置均通过。
- 边界：Qt 的 `QPixmapCache` 全局缓存和平台样式变换仍由 XGui 的可移植缓存/样式
  辅助实现承载，条目选择与替换语义已按上述源码对齐。

### 10.228 2026-08-29 GIF GCE 生命周期、背景处置与读取器跳帧回退

对照 Qt 6.8 GIF 处理器 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp`：

- `:143-192` 在下一帧开始前应用上一帧的处置方式；RestoreBackground 在存在
  透明索引时清透明，否则使用逻辑屏幕背景调色板色。
- `:609-625` 将 GCE 的延迟小于两个百分之一秒钳制为 100ms，并使 GCE 只影响
  紧随其后的图像描述。
- `:1037-1043`、`:1181-1193` 以内部 `loopCnt=-1` 表示没有 Netscape 扩展，公开
  为 `loopCount()==0`；Netscape 原始计数 0 才公开为无限循环 `-1`。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1296-1317`
  要求读取器跳帧直接委托已初始化处理器的虚函数。

实现范围：`XImageCodecGif.c` 解析 LSD 背景索引，首帧局部画布按 Qt 规则初始化，
RestoreBackground 按透明标志或背景色清理；每个图像描述完成后清零 GCE 待处理状态，
延迟执行 Qt 最小值钳制，并修正无 Netscape 扩展的循环次数映射。`XImageReader` 在
GIF 动画缓存未建立或非 GIF 格式时回退 `jumpToNextImage_base`/`jumpToImage_base`，
不再伪造单帧成功。

验证：默认 `build` 的 `XGuiRegression_Test` 和 CTest 通过，覆盖 4 帧 GIF 的背景
恢复、无 Netscape 单帧 GIF 的 `loopCount()==0`、GCE 零延迟钳制为 100ms；
`git diff --check` 通过。构建仍有工程既有 XSignal/XEvent 等警告，未宣称零警告或
零泄漏；动画缓存及 SVG/JPEG 等其他裁剪配置需在对应构建中继续验证。

边界：GIF 的 Q_TRANSPARENT 颜色值在 XImage 中统一为透明黑色 ARGB32；透明 GCE
和无全局调色板时无法恢复的背景仍采用项目透明色约定。非 GIF 自定义处理器是否
实现跳帧由其虚表能力决定，基类默认仍返回 false。

### 10.229 2026-08-29 QColorSpace 用户描述回退

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:1387-1415`：
`setDescription()` 保存独立的用户描述，`description()` 在用户描述为空时回退到
预定义或自动识别的原始名称；描述文本不参与色彩空间相等比较。

实现范围：`XColorSpace` 新增固定大小的 `m_userDescription` 字段，保留
`m_description` 作为自动描述；`XColorSpace_setDescription()` 只修改用户字段，
`XColorSpace_description()` 按 Qt 规则优先返回用户文本，清空后恢复自动名称。新增
回归覆盖自定义描述清空、sRGB 用户描述设置及清空后的 `sRGB` 回退。

验证结果：默认 `build` 与 `build-crop-painter-off` 的 `XGuiRegression_Test` 目标构建、
回归程序、CTest 和 `git diff --check` 均通过；构建保留工程既有 XSignal/XEvent 等警告，
无可用 LSan 专用可执行文件，未宣称零警告或零泄漏。

边界：ICC profile、逐通道 LUT、ElementListProcessing 及 XYZ/色适应矩阵仍未纳入
C99 值类型；这些扩展需资源容器后再实现，当前仅保证描述字段的 Qt 行为。

### 10.230 2026-08-29 QColorSpace 默认传递函数设置模型

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:162-171`
的 `QColorSpacePrivate(Primaries::Custom, transferFunction, gamma)` 构造：默认
`QColorSpace` 调用 `setTransferFunction()` 时会创建 RGB 颜色模型，虽然没有原色矩阵
所以仍然无效。

实现范围：`XColorSpace_setTransferFunction()` 在未定义模型上首次设置非 Custom 传递
函数前标记 RGB，保留 Qt 的传递函数和默认 Gamma 查询结果；已定义 Gray/RGB 模型不
改变。回归覆盖默认对象设置 Linear 后仍无效但模型为 RGB。

验证结果：默认和 `build-crop-painter-off` 的目标构建、回归程序、CTest 及
`git diff --check` 均通过；既有工程警告和无 LSan 专用可执行文件的限制保持不变。

### 10.231 2026-08-29 QPicture 状态记录畸形流校验

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:2036-2054`
的 `setOpacity()` 钳位规则、`:2324-2364` 的合成模式状态更新以及
`:3565-3588` 的背景模式枚举；Picture 状态记录布局依据
`qpaintengine_pic.cpp:121-174,194-204,219-245`。便携协议的状态字段在录制
入口已使用有限单精度与固定枚举/布尔编码，外部 `setData()` 仍可能直接注入负载，
因此校验器必须在长度检查之外拒绝无法由公共 setter 产生的状态。

实现范围：`Src/XGui/Graphics/XPicture.c:96-108,281-306` 新增固定宽度浮点有限性
检查，并对 `SetOpacity` 限定 0..1、`SetCompositionMode` 限定 Qt 38 个连续值、
`SetTransform` 的九个矩阵元素和启用位、`SetBrushOrigin` 两个坐标以及
`SetBackgroundMode` 的 0/1 编码执行验证。画笔样式、端点和连接样式仍保留未知值，
与 Qt setter 原样保存枚举的行为一致。

回归：`xgui_regression_test.c:97-181` 录制合法状态命令后重算 XPicture FNV 校验和，
分别注入 NaN 不透明度、越界合成模式、NaN 画刷原点、非法变换启用位和非法背景模式，
确认 `XPicture_isValidStream()` 全部拒绝，同时不影响合法状态回放。

验证结果：默认 `build` 和 `build-crop-painter-off` 的 `XGuiRegression_Test` 目标构建、
回归程序、CTest 以及 `git diff --check` 均通过。构建仍保留工程既有 XSignal/XEvent
等警告，环境没有独立 LSan 可执行文件，未宣称零警告或零泄漏。

边界：协议仍是 XGui 自有连续 opcode，不直接兼容 Qt 二进制 QPicture；渐变/纹理画刷、
裁剪区域和字体等未编码状态继续按已有文档边界处理。Qt 允许的浮点 NaN 仅存在于
未激活或外部手工构造的调用场景，便携流为确定性安全格式而拒绝这些值。

### 10.232 2026-08-29 QImageReader 扩展名回退与动画连续读取状态

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:497-555`
的 `initHandler()`：文件不存在且启用自动探测时，读取器按支持格式追加扩展名，打开
成功后继续用实际文件名选择处理器；`format()` 在处理器 `canRead()` 成功时返回格式，
因此读取完成到 EOF 后再次查询可能返回空。对照同文件 `:1110-1127`，连续 `read()`
必须返回动画下一帧，全部帧读完后返回空图像。GIF 处理器的帧号初值和递增依据为
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:1037-1044,1097-1122`。

实现范围：`XImageReader_ensureHandler()` 在默认扩展探测成功后重新计算实际候选文件
后缀，避免原始未知后缀继续影响插件优先级；`XImageReader_format_const()` 保持
Qt 的 `canRead()` 门槛，同时允许首次读取前查询新后缀格式。GIF 动画缓存新增“待读
帧”状态，首次 `read()` 从帧 0 开始，`jumpToImage()`/`jumpToNextImage()` 定位后下
一次 `read()` 读取目标帧，成功读取后清除待定位状态；缓存存在但已读完时直接返回
失败，不回退为重复首帧的普通解码路径。

回归：`xgui_regression_test.c` 覆盖未知 `.bad` 后缀追加 `.bmp`、实际文件名暴露、
首次读取前格式查询，以及 GIF 首次连续两次 `read()` 分别得到第 0/1 帧；默认
`build` 与 `build-crop-painter-off` 的目标构建、程序运行和 CTest 均通过，
`git diff --check` 通过。构建仍保留工程既有 XSignal/XEvent 等警告，未宣称零警告
或零泄漏。

边界：GIF 仍采用完整数据缓存解码，未实现 Qt 增量 `imageIsComing()` 的流式行为；
自定义插件的动画连续读取和后缀冲突选择仍由其 `XImageIOHandler` 虚表能力决定。

补充修正：依据同一 `QImageReader::canRead()` 实现（`:1110-1115`），在
`XIMAGEIOPLUGIN_ON` 开启时处理器创建失败直接返回 `false`，不再以文件名后缀或
格式签名替代真实 handler 的 `canRead()`；插件发现裁剪关闭时才保留内置编解码器的
签名回退，以维持该配置的可用性。该差异属于嵌入式裁剪边界，已在默认与裁剪配置的
回归中分别验证。

### 10.233 2026-08-29 显式格式插件首选与内置回退

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:221-254`
的 `createReadHandlerHelper()`：自动探测关闭且给出显式格式时，只查询格式映射中
首个声明 `CanRead` 的插件；该插件 `create()` 失败后不再尝试第二个同键插件，而是
进入内置处理器路径。自动探测开启时，插件仍按注册顺序选择，后缀命中插件在内容
校验失败后才跳过并回退到其它插件/内置处理器（源码 `:295-337`）。

实现范围：`XImagePluginRegistry_createReadHandlerEx()` 为关闭自动探测的显式格式增加
“首个可读插件即停止”分支；若首个外部工厂返回空，则单独尝试内置插件，保持外部
插件覆盖和 Qt 内置回退顺序。自动探测与 `decideFormatFromContent` 的既有路径不变，
仍由 `XImageReader` 负责对后缀处理器执行 `canRead()` 并恢复设备位置。

回归：新增重复 `bmp` 键外部插件且强制 `create()` 失败的测试，分别覆盖自动探测开启
的后缀回退和显式 `format="bmp"`、自动探测关闭时的内置 BMP 回退；默认 `build` 与
`build-crop-painter-off` 均完成全量构建、XGuiRegression_Test、CTest，程序与测试
全部通过，`git diff --check` 通过。工程既有 XSignal/XEvent 等编译警告仍存在，
环境没有独立 LSan 可执行文件，未宣称零警告或零泄漏。

边界：注册表使用固定容量和显式注册顺序模拟 Qt `QFactoryLoader`；系统目录中的动态
imageformats 插件发现、同优先级插件的 Qt 元数据排序以及未实现的格式仍由嵌入式裁剪
开关决定。

### 10.234 2026-08-29 QIcon 延迟文件条目按请求尺寸选帧

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:290-315` 的
`QPixmapIconEngine::bestMatch()`：延迟文件条目取图时先以 `size * scale` 作为物理
目标尺寸，若处理器支持 `Size` 选项则先逐帧查询尺寸并定位精确帧；无法定位时回到
逐帧 `read()`，保存最后一个成功帧，之后把该帧转换为像素图并设置请求 DPR。

实现范围：`Src/XGui/Icon/XIcon.c` 的 `XIconPrivate_loadFileEntry()` 现在通过
`XImageReader_supportsOption(Size)`、`size()`、`jumpToNextImage()` 和
`jumpToImage(0)` 完成同等选择，并在浮点尺寸转换前检查 `INT_MAX`，避免异常 DPR
导致未定义的浮点到整数转换。普通单帧文件及 GIF 逻辑屏幕帧均保持原有加载结果，失败
时不改变延迟条目状态。

验证：默认 `build` 已完成全量构建、`XGuiRegression_Test` 和 CTest，程序通过；
`git diff --check` 通过。

边界：当前 GIF 动画缓存按逻辑屏幕统一帧尺寸，无法构造 Qt 那种每帧独立尺寸的多帧
资源；该情形仍由处理器的 `Size`/跳帧能力决定。图标主题引擎和自定义
`XIconEngine` 的延迟加载行为不受此内置像素图路径影响。

### 10.235 2026-08-29 QImageWriter 失败时清理新建目标文件

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:252-278`
的 `QImageWriterPrivate::canWriteHelper()` 与 `:622-638` 的 `QImageWriter::canWrite()`：
文件名构造器会在能力检查时按需打开内部 `QFile`；如果设备、格式或处理器检查失败，
而目标文件原先不存在，外层逻辑会删除该次检查创建的空文件，避免失败调用留下副作用。

实现范围：`Src/XGui/Graphics/XImageWriter.c` 新增失败清理辅助函数，在文件名写入器
检测到目标此前不存在时记录 `removeOnFailure`；设备打开失败、不可写、格式不支持或
处理器创建失败的所有返回路径都会先关闭内部文件并删除该目标。已存在的文件不受影响，
外部传入设备仍保持调用方所有权。

回归：`xgui_regression_test.c` 新增未知格式文件名用例，验证 `canWrite()` 返回设备/格式
失败且 `xgui_writer_failed_cleanup.unknown` 不存在；默认 `build` 与
`build-crop-painter-off` 的目标构建、回归程序和 CTest 均通过，`git diff --check`
通过。

边界：Qt 的 `QFile` 生命周期和系统错误文本在本项目中由 `XFile`/`XIODevice` 提供，
错误字符串仍使用现有跨平台固定文本；外部非文件设备失败时不执行文件删除逻辑。

### 10.236 2026-08-29 静态图像格式探测确认处理器可读性

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1458-1467`
的 `QImageReader::imageFormat(QIODevice *)`：注册表能力声明只用于筛选候选插件，
最终格式必须来自已创建处理器并通过 `QImageIOHandler::canRead()` 的结果；同时对非顺序
设备恢复探测前的位置。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c` 新增处理器级探测辅助函数，
`XImagePluginRegistry_detectReadFormat()` 对每个声明 `CanRead` 的格式创建处理器、调用
`canRead()` 后才返回格式名，并在创建和校验前后恢复设备位置。该路径覆盖带格式键和空格式
内容探测两种插件；内置处理器仍沿用其 `peek()` 语义，外部插件按位置恢复规则处理。

回归：扩展 `xgui_regression_test.c` 插件探测测试，先验证会消费设备的插件仍返回 `mock`
且位置不变，再让同一插件处理器报告不可读，确认 `detectReadFormat()` 返回空字符串并保持
位置不变。默认构建及 `XGuiRegression_Test` 通过，`git diff --check` 通过。

边界：`capabilities()` 和 `canRead()` 的具体内容校验仍由各插件实现；内置 BMP 处理器与
Qt `QBmpHandler::canRead()` 一样主要检查文件签名，截断像素区由后续 `read()` 报告无效数据。

### 10.237 2026-08-29 QIcon 高 DPI actualSize 逻辑尺寸还原

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:966-978` 的
`QIcon::actualSize()`：设备像素比大于 1 时先把逻辑请求尺寸乘以 DPR 交给引擎，
再用 `QIconPrivate::pixmapDevicePixelRatio()` 修正引擎实际返回的物理尺寸，最后除以
DPR 返回设备无关尺寸；普通 DPR 路径直接调用引擎。DPR 修正公式依据同文件
`:149-166`，尺寸除法采用 `QSize::operator/` 在
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/tools/qsize.h:74-75,184-189`
中的四舍五入语义。

实现范围：`Src/XGui/Icon/XIcon.h` 新增 `XIcon_actualSizeRatio()`。引擎图标按物理请求
调用 `XIconEngine_actualSize_base()`；内置像素图按 Qt `QPixmapIconEngine::actualSize()`
的 scale=1 选择并以物理目标尺寸约束资源，随后统一按实际物理尺寸修正输出 DPR、还原逻辑
尺寸。DPR 不大于 1 或 NaN 复用普通 `XIcon_actualSize()`；正无穷、尺寸溢出和非有限结果
直接返回零尺寸，避免嵌入式 C API 的浮点到整数未定义转换。

回归：`xgui_regression_test.c` 覆盖 2x 高分辨率像素图、混合 1x/2x 资源、固定主题目录、
可缩放主题，以及普通路径的逻辑尺寸上限；默认 `build` 的全量构建、`XGuiRegression_Test`
和 CTest 通过，`git diff --check` 通过。构建仍显示工程既有 XSignal/XEvent 等类型警告，
环境没有独立 LSan/Valgrind 工具，未宣称零警告或零泄漏。

边界：`XIcon_actualSizeRatio()` 是 C API 对 Qt 隐含应用 DPR 的显式入口，调用方需传入
窗口或屏幕 DPR；系统 QApplication 的全局 DPR 获取仍由上层窗口模块负责。自定义引擎若
返回超出请求的尺寸，函数保留 Qt 引擎结果语义，不额外裁剪其异常输出。

### 10.238 2026-08-29 QIcon 主题条目登记与延迟解码

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:476-535`
的 `findIconHelper()`：主题查找先依据 `QFile::exists()` 登记 PNG/SVG 条目，实际像素
读取延迟到 `PixmapEntry::pixmap()`；`qiconloader.cpp:954-967` 的
`QIconLoaderEngine::isNull()` 仅检查条目集合是否为空，不会因文件内容损坏提前判空。
独立回退文件同样在 `:572-610` 先判断文件存在，再由引擎稍后读取。

实现范围：`Src/XGui/Icon/XIconThemeInternal.c` 新增登记查询链，沿当前主题、索引目录、
`Inherits`、后备主题、短横线名称回退及独立 `fallbackSearchPaths()` 检查文件存在性，
不调用图像解码器；`XIconThemeEngine.c` 的 `isNull()` 和 `XIcon.c` 的
`hasThemeIcon()` 改用该查询。正常 `pixmap()`/`paint()` 仍走原有解码路径，损坏条目
在实际取图时返回空像素图，避免把登记状态与解码状态混为一谈。

回归：`xgui_regression_test.c:test_icon_theme_index_inherits()` 新增损坏 BMP 主题条目，
验证 `XIcon_isNull()`/`XIcon_hasThemeIcon()` 保持非空而 `XIcon_pixmap()` 在解码阶段失败。
默认 `build` 与 `build-crop-painter-off` 均完成全量构建、`XGuiRegression_Test` 和 CTest，
`git diff --check` 通过。构建仍报告工程既有 XSignal/XEvent 类型警告；环境没有独立
LSan/Valgrind 可执行文件，未宣称零警告或零泄漏。

边界：主题索引和独立回退仍使用项目支持的 PNG/SVG/XPM/BMP 扩展集合，平台原生
`QFactoryLoader` 排序及逐帧/流式图像解码不在该嵌入式登记查询内；缺失文件仍不会登记。

### 10.239 2026-08-29 XImageReader GIF 连续读取与定位回归

对照 Qt 6.8.3 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1110-1127`
的 `QImageReader::canRead()/read()`，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:1037-1044,1097-1123,1167-1169`：
动画读取器首次 `read()` 返回第 0 帧，后续调用依次推进帧号；`jumpToImage(n)` 成功后
下一次 `read()` 返回指定帧，末帧之后返回失败而不重复首帧；读前帧号为 -1 且
`nextImageDelay()` 使用处理器构造默认值 100ms。`canRead()` 在仍有待读帧时为真，
末帧消费后为假。

实现范围：`Src/XGui/Graphics/XImageReader.c:1408-1428` 在动画缓存建立后按待读帧和
待定位帧判断 `canRead()`，避免已消费到 EOF 的设备被重复探测；
`:1787-1800` 在无当前帧时返回 100ms 默认延迟；`:1802-1813` 主动准备 GIF 缓存并
保持读前帧号 -1。无缓存或非 GIF 时仍委托处理器虚函数。

回归新增 `jumpToImage(2)` 后读取目标帧、读前 `canRead()/100ms`、连续读取四帧以及
末帧后 `read()/canRead()` 失败断言。默认 `build` 与 `build-crop-painter-off` 均完成
全量构建、`XGuiRegression_Test` 和 CTest；工程既有 XSignal/XEvent 等编译警告仍存在，
环境没有独立 LSan/Valgrind 可执行文件，未宣称零警告或零泄漏。

### 10.240 2026-08-29 QIcon availableSizes 主题继承停止条件

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:538-555`
的 `findIconHelper()` 与 `:976-995` 的 `QIconLoaderEngine::availableSizes()`：当前主题
只要已经登记了该图标的任一条目，就不再递归 `Inherits` 或拼接父主题尺寸；只有当前主题
完全没有条目时，才按主题继承顺序继续查找父主题和后备主题。

实现范围：`Src/XGui/Icon/XIconThemeInternal.c:1514-1604` 的尺寸收集函数现在记录当前
主题是否命中条目，仅在未命中时递归 `Inherits`；`XIconInternal_availableThemeSizes`
在当前主题已有尺寸时也跳过独立的 `fallbackThemeName` 主题，避免父主题尺寸泄漏到结果。
既有尺寸去重和嵌入式格式集合保持不变。

回归：`xgui_regression_test.c:test_icon_theme_index_inherits()` 新增子主题 48x48、父主题
32x32 的同名图标夹具，断言 `XIcon_availableSizes()` 只返回 48x48；同时覆盖重写
`index.theme` 元数据后的目录刷新。默认 `build` 与 `build-crop-painter-off` 均完成全量
构建、`XGuiRegression_Test` 和 CTest，`git diff --check` 通过。

边界：本地 API 仍按项目约定对相同逻辑尺寸去重；Qt 原生插件加载顺序、同尺寸重复 entry
是否保留以及其它主题路径的动态 `QFactoryLoader` 细节不属于本嵌入式主题索引实现。

### 10.241 2026-08-29 QImageWriter 处理器写入失败不回退

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:651-687`
的 `QImageWriter::write()`：图像有效且 `canWrite()` 成功后，Qt 只向已选处理器
应用选项并调用一次 `handler->write()`；处理器返回失败时立即返回 `false`，不会再
调用另一条通用编码路径。`qimagewriter.cpp:622-637` 同时规定 `canWrite()` 只负责
设备、格式和处理器可用性检查，不能把写入失败转换为另一格式的成功。

实现范围：`Src/XGui/Graphics/XImageWriter.c:776-861` 在处理器已经创建并尝试写入后，
若 `XImageIOHandler_write_base()` 或刷新失败则释放变换图像、设置
`XImageWriterError_DeviceError` 并立即返回；只有没有处理器的直接编码路径才继续使用
`XImage_save_2()`。这保留了 Qt 的“处理器所有权和失败语义”，同时避免同一目标文件被
插件和通用编码器重复写入。

回归：`xgui_regression_test.c:test_image_plugin_registry_integration()` 新增拒绝写入的
同名 BMP 插件夹具，断言 `XImageWriter_write()` 失败且不静默回退。默认 `build` 与
`build-crop-painter-off` 均完成目标构建、回归程序和 CTest；`git diff --check` 通过。
构建输出仍含工程既有 XSignal/XEvent 类型警告，环境没有 Valgrind 或可用的独立
ASan/LSan 回归可执行文件，因此未宣称零警告或零泄漏。

边界：测试插件只验证处理器拒绝写入时的控制流；实际 PNG/JPEG 等编码器的错误文本和
设备刷新细节仍由各自 `XImageCodec`/`XIODevice` 实现决定，尚未覆盖 Qt 的所有外部
imageformats 插件。

### 10.242 2026-08-29 QImageWriter Gamma 默认选项传递

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:217-243`
的 `QImageWriterPrivate` 初始化和 `:663-670` 的写入选项应用：私有 `gamma` 默认值为
`0.0`，处理器声明支持 `QImageIOHandler::Gamma` 时，`write()` 必须在质量和压缩比
之后调用 `setOption(Gamma, gamma)`，即使调用方没有公开的 Gamma setter。

实现范围：`Src/XGui/Graphics/XImageWriter.c:98,247-258,465-467` 增加私有
`m_gamma` 默认状态，并在处理器支持 `XImageIOHandlerOption_Gamma` 时传递浮点值；
公共 XImageWriter API 和直接编解码路径保持不变。`xgui_regression_test.c:5708,
5810-5832,6460-6482` 的 mock 处理器记录 Gamma 选项，插件写入回归断言收到精确的
`0.0f`，同时覆盖已有 Description 选项顺序。

验证：默认 `build` 与 `build-crop-painter-off` 全量构建成功；两套配置的
`XGuiRegression_Test` 和 CTest 均通过，`git diff --check` 通过。构建输出仍含工程既有
XSignal/XEvent 类型兼容性警告；环境没有 Valgrind，`build-asan/XGuiRegression_Test`
不可用，因此未宣称零警告或零泄漏。

边界：当前库没有公开 Gamma 设置接口，故仅实现 Qt 默认 `0.0` 的传递；未来若增加
Gamma setter，应将 `m_gamma` 暴露为对应接口状态。具体编码器对 Gamma 的解释仍由各
处理器自行决定。

### 10.243 2026-08-29 QIcon 主题短横线回退的 iconName

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:530-535,557-566`
的 `findIconHelper()`：主题条目命中时将 `QThemeIconInfo::iconName` 记录为实际查找
名称；原名称在主题及继承链均未命中后，才截断最后一个 `-` 递归查找，首次命中名称
即作为引擎的 `iconName()` 返回值。`qiconloader.cpp:764-768,959-962` 进一步表明
引擎保存查询结果并返回 `m_info.iconName`。

实现范围：`Src/XGui/Icon/XIconThemeInternal.h` 新增名称解析入口；
`XIconThemeInternal.c` 复用登记查询，按原名称、主题继承、短横线候选和独立回退文件
顺序返回实际命中的字符串；`XIconThemeEngine.c:219-230` 在 `iconName()` 中优先返回
该结果。未命中时保留项目原有请求名称兼容行为，绝对路径仍不参与主题名称解析。

回归：`xgui_regression_test.c:620-631` 的
`test_icon_theme_index_inherits()` 对仅存在 `example-icon` 的主题请求
`example-icon-tool`，断言名称解析返回 `example-icon`，并继续验证像素回退。默认与
`build-crop-painter-off` 的目标构建、全量构建、回归和 CTest 已重新执行并通过；构建
仍有既有 XSignal/XEvent 类型兼容性警告，内存工具缺失边界保持不变。

边界：引擎当前按查询时的主题配置动态解析名称；Qt 的 `QThemeIconEngine` 主题键失效
通知和平台图标引擎插件不在嵌入式主题索引范围内。

### 10.244 2026-08-29 图像插件固定容量下保留内置处理器

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:14-39`
的全局 `QFactoryLoader` 与互斥保护：内置 imageformats 处理器和外部插件由同一
查询入口提供，外部插件发现不应使内置格式失效；`qimagereader.cpp:221-254`
则规定格式处理器创建失败后仍要继续尝试内置处理器。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:247-264` 在固定容量数组首次
发现内置插件前，为内置单例预留一个槽位；外部插件最多占用
`XIMAGEPLUGINREGISTRY_CAPACITY - 1` 个位置，随后 `ensureBuiltin()` 仍可加入内置
处理器并保持外部插件优先顺序。该保护与现有递归互斥锁、显式插件移除和清空后懒
注册逻辑兼容，不改变容量宏关闭时的裁剪行为。

回归：`xgui_regression_test.c:6841-6886` 先在首次查询前注册外部插件直到容量边界，
断言实际保留 `capacity - 1` 个外部插件、内置插件成功加入并继续支持 BMP，随后清理
所有夹具。默认 `build` 与 `build-crop-painter-off` 全量构建、回归程序和 CTest 均已
重新执行并通过；构建仍保留工程既有 XSignal/XEvent 类型兼容性警告。

边界：Qt 使用可动态扩展的 `QFactoryLoader`，本项目为嵌入式安全采用固定静态容量，
仍未实现 `imageformats` 目录动态扫描、插件优先级元数据排序、热加载和卸载；容量不足
时新增外部插件按现有 API 返回失败，不会挤掉内置基础格式。Valgrind 和独立 ASan/LSan
回归程序在当前环境不可用，未宣称零泄漏。

### 10.245 2026-08-29 内置图像处理器回传内容探测格式名

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1458-1467`：
`QImageReader::imageFormat(QIODevice*)` 创建自动探测处理器，调用 `canRead()` 成功后
直接返回处理器 `format()`；SVG 处理器的 XML 前缀规则见
`/home/xinyue/Qt/6.8.3/Src/qtsvg/src/svg/qsvgtinydocument.cpp:42-52`，允许 XML 声明或
注释先出现，只要同一有界缓冲区内包含 `<svg` 或 SVG doctype。

实现范围：`Src/XGui/Graphics/XImageBuiltinPlugin.c:371-402` 的内置插件在未提供显式
格式时，创建处理器后使用现有 `builtin_detectWithDevice()` 的 4096 字节窥视结果；识别
成功即把规范小写格式名写入 `XImageIOHandler`，未知格式不写入空名称。这样读取器走
`canRead()` 后可直接获得 SVG/GIF/PNG 等实际格式，不会因 `XImageReader.c:1919-1926`
的 16 字节签名兜底过短而丢失长 XML 前缀的 SVG 格式。编解码器关闭时该代码仍受
`#if XIMAGECODEC_ON` 保护，保持嵌入式裁剪可编译。

回归：`xgui_regression_test.c:8384-8403` 新增长 XML 注释前缀 SVG 夹具，调用
`XImageReader_imageFormat_2()` 并断言返回 `svg`，`xgui_regression_test.c:15385` 接入
主回归序列。默认 `build` 与 `build-crop-painter-off` 均完成目标构建、全量构建，
`XGuiRegression_Test` 与 CTest 均通过；`git diff --check` 通过。构建输出仍含工程既有
XSignal/XEvent 类型兼容性警告，Valgrind 不可用且 `build-asan/XGuiRegression_Test`
不存在，因此未宣称零警告或零泄漏。

边界：内容探测最多窥视 4096 字节，SVG 根元素位于更长前缀时仍可能退回空格式；该上限
用于限制不可信设备探测成本，符合项目嵌入式裁剪取舍。显式格式仍需 `canRead()` 校验
设备内容，未知或畸形 XML 不会伪装成 SVG。

### 10.246 2026-08-29 QIcon hasThemeIcon 短横线回退精确匹配

对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1376-1409`：
`QIcon::hasThemeIcon(name)` 先构造主题图标，再比较 `icon.name() == name`；
`QIconLoader::findIconHelper()` 在 `qiconloader.cpp:557-566` 的短横线回退会把实际
命中名称截短。因此请求名仅由 `example-icon` 满足时，`fromTheme()` 可获得并绘制图标，
但 `hasThemeIcon("example-icon-tool")` 必须返回 false。

实现范围：`Src/XGui/Icon/XIcon.c` 的 `XIcon_hasThemeIcon()` 现在复用主题名称解析
入口并执行大小写敏感的完整字符串比较；绝对路径仍直接返回 false，精确命中、继承主题、
后备主题及独立回退文件保持原有登记语义。这样与 `XIcon_name()` 保存的实际引擎名称
一致，同时保留损坏文件的延迟解码行为。

回归：`xgui_regression_test.c:test_icon_theme_index_inherits()` 新增短横线回退请求的
`XIcon_hasThemeIcon_2()` false 断言，并继续验证解析出的实际名称为 `example-icon`。
默认配置、`build-crop-painter-off`、`build-crop-svg` 与 `build-crop-jpeg` 的回归目标
和 CTest 已按配置串行验证并通过；构建中工程既有 XSignal/XEvent 类型警告仍需单独修复，
Valgrind 与独立 ASan/LSan 回归程序不可用，不能宣称零警告或零泄漏。

边界：本地主题引擎仍未实现 Qt 桌面版动态主题插件和平台原生图标引擎；名称解析使用
固定 1024 字节路径/候选缓冲区，超长主题名按嵌入式边界返回未命中。

### 10.247 2026-08-29 QColorSpace 自定义原色坐标设置

本轮对照 Qt 6.8 源码：

- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.h:120-123`：
  `setPrimaries(const QPointF &, const QPointF &, const QPointF &, const QPointF &)`
  的公共重载声明。
- `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:74-87,1002-1023`：
  `QColorSpacePrimaries::areValid()` 的 CIE xy 有限范围校验，以及自定义原色设置后
  标记 `Primaries::Custom`、清除命名和描述的语义。

实现范围：

- `Src/XGui/Graphics/XColorSpace.h:275-284` 新增
  `XColorSpace_setPrimariesData()`，以现有 `XColorSpacePrimariesData` 显式承载白点和
  三原色坐标；公共注释完整说明输入校验、传递函数保留和无效空间元数据保留行为。
- `Src/XGui/Graphics/XColorSpace.c:460-477` 拒绝空指针、非有限或越界坐标，接受有效坐标
  后清除命名空间、自动描述和用户描述，重算 RGB 空间有效性；重复坐标设置保持无操作。
  同时修正 `setWhitePoint()`、预定义 `setPrimaries()` 和传递函数 setter 在实际改变
  色彩空间时清理用户描述，保持 `description()` 与 Qt 的单一描述字段语义一致。
- `xgui_regression_test.c:8540-8558` 覆盖有效自定义坐标替换、描述清理和畸形坐标忽略。

边界：C99 值类型仍不保存 Qt 私有 `QColorMatrix`，因此不会执行 Bradford 色彩适应和
  RGB 矩阵重标定；ICC profile、逐通道 LUT 和 `ElementListProcessing` 仍需独立资源容器。

### 10.248 2026-08-29 QImage 显式色彩空间与像素格式重载

本轮对照 Qt 6.8 源码 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.h:234-241`
的两个 `convertedToColorSpace()`/`convertToColorSpace()` 三参数重载，以及
`qimage.cpp:5080-5173` 的格式有效性、目标颜色模型兼容性和源图不变语义。

实现范围：

- `Src/XGui/Graphics/XImage.h:269-304` 新增带 `_ex` 后缀的 C99 显式重载，使用
  `XImageFormat` 参数避免 C 语言重载冲突；注释说明无效格式、模型不兼容时的输出和
  就地返回规则。
- `Src/XGui/Graphics/XImage.c:650-670,813-881` 增加像素格式到颜色模型的兼容检查：
  `Invalid` 不再自动选择中间格式；不兼容目标输出空图像或返回 `false` 并保持源图；
  目标空间相同时仍执行指定格式转换。
- `xgui_regression_test.c:8669-8700` 覆盖 RGB 到 `Grayscale8` 成功、ARGB32 不兼容
  拒绝以及就地失败后源图格式和色彩空间不变。

边界：像素变换仍使用当前嵌入式实现的逐像素传递函数路径，未实现 Qt 私有矩阵和 ICC/LUT
  精确变换；目标颜色模型兼容性因此限制在本地可表达的 RGB、Gray 和 CMYK 元数据。

### 10.249 2026-08-29 QImageReader MIME 格式顺序保持

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:118-136`：
  `imageFormatsForMimeType()` 直接按插件元数据声明顺序追加格式，不对结果做全局排序。

实现范围：`Src/XGui/Graphics/XImageReader.c:1985-2022` 删除原有大小写敏感排序和末尾去重，
  依靠追加时的大小写不敏感重复检查保留首次出现的位置；JPEG MIME 查询现在严格返回
  `jpg, jpeg, jfif`。`xgui_regression_test.c:6073-6090` 增加顺序夹具，验证 Qt 插件键顺序。

边界：固定内置格式表和外部插件发现仍受嵌入式静态注册表容量限制，不实现桌面 Qt
  `QFactoryLoader` 的动态目录扫描、插件元数据排序和热卸载。

### 10.250 2026-08-29 XPainter 渐变画刷切回纯色的 Picture 状态

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:176-192,483-497`
  的 `updateBrush()`/DirtyBrush 状态序列化，以及
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3726-3750` 的
  `QPainter::setBrush()` 行为：画刷对象切换必须产生新的画刷状态记录。

实现范围：`Src/XGui/Graphics/XPainter.c:4572-4585` 在
  `XPainter_setBrushGradient(NULL)` 清除渐变载荷、切换为 `SolidPattern` 后补写固定长度
  `SetBrush` 记录；非空渐变仍只保留内存中的渐变描述，不伪造无法表达的可变 Picture 载荷。
  `xgui_regression_test.c:4982-5021` 新增渐变切回纯色并回放到 Picture 的夹具，验证样式
  恢复和渐变停止点清零。

边界：Picture 流只表达固定长度的画刷样式和颜色，渐变/纹理的完整对象仍不能序列化；
  这与当前嵌入式 opcode 集合一致，复杂画刷状态需后续扩展可变长度记录格式。

统一验证：默认 `build` 和 `build-crop-painter-off` 的目标构建、`XGuiRegression_Test`、
CTest 均通过；`git diff --check` 通过。构建仍有仓库既有 `XSignal`/`XEvent` 类型兼容性
警告，环境无可用 Valgrind 或独立 ASan/LSan 可执行文件，不能宣称零警告或零泄漏；本轮
未提交、未推送 Git。

### 10.253 2026-08-30 QImage 新建对象默认 DPI

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:92-100`：
`QImageData` 构造时使用 `qt_defaultDpiX/Y() * 100 / 2.54` 初始化每米点数；当前
嵌入式平台默认逻辑 DPI 为 96，结果按四舍五入为 3780 dots/m。

实现范围：`Src/XGui/Graphics/XImage.c:70-84,299-306` 增加固定 96 DPI 换算辅助函数，
并在每个新建 `XImageData` 的默认构造路径初始化 `m_dpmX/m_dpmY` 为 3780；显式载入
设备数据、复制、缩放和转换路径仍继续继承源图像的 DPI 元数据。`xgui_regression_test.c`
在灰度图元数据夹具中新增默认 DPI 断言。

边界：Qt 桌面版可通过平台默认 DPI 动态改变初值，本项目不在 `Src` 引入平台 API，固定
采用 XScreen 的 96 DPI 约定；调用 `setDotsPerMeterX/Y(0)` 仍按 Qt 语义保持当前值不变。

验证：默认、画笔裁剪、SVG 裁剪和 JPEG 裁剪的 `XGuiRegression_Test` 目标构建、运行与
CTest 均通过；工程既有编译警告、运行时诊断和缺少 Valgrind/ASan 工具边界保持如实记录；
本轮未提交、未推送 Git。

### 10.251 2026-08-29 XPicture 三次曲线路径序列化校验

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpicture.cpp:462-467`、
`qpicture_p.h:71` 以及 `qpaintengine_pic.cpp:350-359` 的路径元素约束：每个三次曲线
`CurveTo` 必须紧跟两个 `CurveToData`，路径流结束时不能留下未完成的控制点数据。

实现范围：`Src/XGui/Graphics/XPicture.c:216-232` 在路径负载校验阶段拒绝孤立
`CurveToData`、曲线后插入 `MoveTo`/`LineTo` 或连续第二个 `CurveTo` 的畸形序列，确保
`XPicture_isValidStream()` 与后续 `XPicture_rebuildPath()` 使用同一约束，避免校验通过后
回放失败。`xgui_regression_test.c:188-208` 增加中断三次曲线夹具并验证校验和重算后的流
被拒绝。

边界：路径元素数量仍受 `XPICTURE_MAX_PATH_ELEMENTS` 嵌入式上限约束，未引入 Qt 桌面
私有路径命令或压缩格式；仅补齐现有 C99 opcode 集合中的曲线序列完整性。

验证：默认 `XGuiRegression_Test` 目标构建及回归通过，`git diff --check` 通过；保留工程
既有编译/运行时诊断，Valgrind 与独立 ASan/LSan 可执行文件不可用，因此不宣称零泄漏；本轮
未提交、未推送 Git。

### 10.252 2026-08-30 GIF 解码边界与透明像素语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:15,42-44,143-171,253-260,340-377,1026-1032`：
`Q_TRANSPARENT` 固定为 `0x00ffffff`，透明调色板索引保留 RGB 分量；首帧透明像素写入透明
调色板色，后续帧透明索引保留底图；恢复背景处置使用透明白色；逻辑屏幕面积超过
`16384*16384` 时拒绝输入；LZW 码流和调色板长度必须在有界缓冲区内。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecGif.c:36-75,143-176,259-260,370-470`
补充位读取和调色板乘法的溢出保护，拒绝 `code > next` 的未初始化字典访问，增加逻辑屏幕
面积上限；透明首帧使用调色板 RGB 的透明值，后续透明帧跳过写入以保留画布；背景恢复
统一使用 Qt 的 `0x00ffffff` 语义，越界调色板索引转换为透明值而不访问越界内存。

边界：当前动画仍一次性缓存最多 1024 帧，未实现 Qt 增量 `imageIsComing()` 流式解码；
GIF 编码器仍输出固定 3-3-2 调色板单帧，复杂局部调色板和压缩码宽变化不在本轮范围。

验证：默认 `XGuiRegression_Test` 目标构建与回归通过，既有 GIF 动画夹具保持通过；
`git diff --check` 通过。构建输出保留工程既有类型兼容性警告，环境无 Valgrind/独立
ASan/LSan 可执行文件，未宣称零泄漏；本轮未提交、未推送 Git。

### 10.254 2026-08-30 QIcon 独立回退图标的非方形实际尺寸

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:882-900,976-992`
和 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:966-977,1017-1040`：
主题固定/阈值条目按最小请求边匹配，而 `Fallback` 条目委托普通图标引擎，
`actualSize()` 应保留实际 pixmap 的宽高比；`iconName()` 返回回退解析后的实际名称。

实现范围：`Src/XGui/Icon/XIconThemeEngine.c:92-164` 在独立回退文件存在非方形
可用尺寸时按请求矩形等比缩放，主题目录条目仍维持正方形匹配；
`XIconThemeEngine_iconName()` 返回 `XIconInternal_resolveThemeIconName()` 的短横线
回退命中名称，未命中时保留原请求名兼容行为。

边界：当前嵌入式主题索引不加载 Qt 桌面插件的动态目录元数据，回退尺寸来自已登记的
`availableThemeSizes()`；高 DPI 和复杂多状态图标仍由现有 `scaledPixmap()` 路径处理。
本轮沿用既有默认回归夹具，未新增独立非方形主题文件；验证依赖默认和裁剪配置回归。

### 10.255 2026-08-30 QImage 文本元数据空键兼容重载

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.h:277-279`
及 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:4184-4244`：
`QImage::text()` 的空键表示聚合全部文本，缺失键返回空字符串；C++ 默认空
`QString` 与 UTF-8 兼容入口的空指针应保持同一语义。

实现范围：`Src/XGui/Graphics/XImage.c:2984-3016` 将
`XImage_text_2(image, NULL)` 规范化为空键后复用聚合路径；`XImage_text()`、
`textKeys()` 和重复键覆盖行为保持既有排序映射语义。`xgui_regression_test.c:9274-9279`
增加空指针键与 `text(NULL)` 聚合结果一致性断言。

边界：元数据仍存储为嵌入式 UTF-8 字符串映射，不实现 Qt `QString` 的隐式编码转换、
富文本解析或插件私有文本块；非法 UTF-8 由现有 `XString_create_utf8()` 错误路径处理。
验证：默认回归目标、运行和 CTest 通过，`git diff --check` 通过；保留工程既有编译
警告与 Valgrind/ASan 不可用边界，未提交、未推送 Git。

### 10.256 2026-08-30 BMP RLE、V4/V5 头和调色板边界

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:188-208,249-355,373-452`：
RLE4 绝对模式的数据区按 16 位字对齐消费；V4/V5 头仅在 108/124 字节时启用固定掩码；
OS/2 64 字节头使用四字节调色板条目，其余 Core/Info 头保持各自格式。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:122-130,185-193,264-272`
按 Qt 的字对齐条件补齐 RLE4 绝对模式余数为 1 或 2 的像素块，收紧 V4/V5 识别并按头部类型选择调色板条目宽度；
既有尺寸、偏移、位掩码和压缩模式边界保持不变。

边界：嵌入式解码器仍裁剪到项目支持的 BI_RGB/BI_RLE4/BI_RLE8/BI_BITFIELDS，
不加载 Qt 桌面插件的压缩变体和 ICC 元数据；异常截断输入继续返回失败而不部分提交图像。
验证：默认 `XGuiRegression_Test` 构建、运行及 CTest 通过，`git diff --check` 通过；
工程既有编译警告和 Valgrind/ASan 工具缺失如实保留，未提交、未推送 Git。

### 10.258 2026-08-30 XPicture 高层虚线 Polyline/Path 回放

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpicture.cpp:440-560`
的 `PdcDrawPolyline`/`PdcDrawPath` 回放分派，以及 `qpicture.cpp:778-790` 的状态矩阵恢复：
高层折线和路径记录必须继续交给 painter 的样式路径，不能把画笔统一改成实线；普通
`PdcDrawLine` 直接落到底层回调，不会重复经过样式拆分。

实现范围：`Src/XGui/Graphics/XPicture.c:1261-1304,1681-1695` 删除回放前将调用者
画笔强制设为 `SolidLine` 的逻辑，保留记录中的 `SetPen` 样式和结束后的调用者状态恢复；
`xgui_regression_test.c:3570-3625` 增加虚线 Polyline 的连续段/间隙像素断言，同时保留普通
虚线 DrawLine 的底层回调断言。

边界：Picture 仍只支持项目现有固定长度 opcode；Qt 私有 DPI 缩放（`qpicture.cpp:430-433`）
和未实现的旧 opcode 不在本轮扩展，路径元素的 CurveTo 完整性由 10.251 单独校验。

验证：默认 `XGuiRegression_Test` 目标构建、运行和 CTest 通过，`git diff --check` 通过；
现有 XSignal/XEvent 类型兼容性警告及 Valgrind/独立 ASan 不可用边界保持如实记录，未提交 Git。

### 10.259 2026-08-30 QIcon 懒加载失败条目生命周期

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:191-324`
的 `QPixmapIconEngine::removePixmapEntry()` 和懒加载路径，及 `qicon.cpp:327-415` 的最佳
条目选择、缩放缓存和 `actualSize()` 语义：懒加载文件解码失败后应从私有条目集合移除，
后续 `isNull()`、尺寸查询和绘制不可反复命中同一失效条目；const 查询触发加载前需要写时
分离共享数据。

实现范围：`Src/XGui/Icon/XIcon.c:593-621,820-833,956-1041` 新增失效条目移除辅助函数，
在 `scaledPixmap()`、`actualSize()` 和高 DPI `actualSizeRatio()` 的文件加载失败分支清理
条目，并在懒加载前确保私有数据独占，避免一个图标副本的缓存变化污染共享副本。

边界：主题引擎的 `read()`/`write()` 仍为项目占位接口，未猜测性引入 Qt `QDataStream` 私有
序列化协议；Qt `QThemeIconEngine` 与 `QIconLoaderEngine` 的类型区分仍受现有引擎 key 兼容层
限制，复杂主题缓存格式留待后续对齐。

验证：代理完成默认及裁剪目标构建、`git diff --check`，主线程将在本轮统一重跑回归；环境无
Valgrind/独立 ASan/LSan 可执行文件，不能宣称零泄漏；未提交、未推送 Git。

### 10.260 2026-08-30 BMP 合法 RLE4 跨行字对齐夹具

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:419-445`：
RLE4 绝对模式的控制字节值 0、1、2 分别保留给行尾、图尾和 Delta，不能把 `00 02` 当作
两像素绝对块；绝对块的像素数据按 16 位字边界消费，Qt 条件
`(((count & 3) + 1) & 2) == 2` 等价于 `count % 4` 为 1 或 2。

实现范围：`xgui_regression_test.c:7290-7342` 将原先非法的两像素伪绝对块改为两行合法的
五像素绝对块，显式加入每行填充字节并验证顶/底行索引序列 `6..10` 与 `1..5`；移除临时
诊断输出，避免测试日志污染。

边界：夹具仅覆盖 RLE4 的绝对块和行序，不扩展 Qt 对 RLE Delta 越界写入的历史兼容行为；
解码器对截断流仍返回失败，未提交部分图像。

验证：修正后默认 `XGuiRegression_Test` 运行通过；完整的四套配置、CTest 与
`git diff --check` 结果见 10.261。构建仍保留工程既有警告，未运行 Valgrind/独立
ASan，未提交 Git。

### 10.257 2026-08-30 其余裁剪目录全量构建阻塞记录

为覆盖本轮图像/图标改动的其他配置，额外启动了 `build-crop-background-off`、
`build-crop-bgbrush-off`、`build-crop-clip`、`build-crop-image-rect-off`、
`build-crop-layout`、`build-crop-min`、`build-crop-path`、`build-crop-polygon`、
`build-crop-region-off`、`build-crop-renderhint`、`build-crop-shape`、
`build-crop-stacked-off`、`build-crop-world-matrix` 和 `build-gui-crop` 的
`XinYueCS` 全量目标。

其中 `build-crop-clip`、`build-crop-layout`、`build-crop-polygon`、
`build-crop-renderhint`、`build-crop-shape`、`build-crop-stacked-off` 和
`build-crop-world-matrix` 的 `XinYueCS` 静态库目标完成；
`build-crop-background-off`、`build-crop-bgbrush-off`、`build-crop-image-rect-off`、
`build-crop-min`、`build-crop-path`、`build-crop-region-off` 与 `build-gui-crop`
在同一仓库既有缺失源文件处阻塞：
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_host.c` 和
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c` 不存在，CMake 仍将其列入
目标依赖。该阻塞不涉及本轮 XGui 文件，不能据此宣称这些配置全量通过；已通过的
默认、`build-crop-painter-off`、`build-crop-svg`、`build-crop-jpeg` 配置的回归和
CTest 结果保持有效。

### 10.261 2026-08-30 本轮并行审计统一验证

本轮三条互不重叠的并行审计均已收尾：`XPicture` 虚线 Polyline 回放、`XIcon`
懒加载失败条目生命周期、BMP RLE4 合法字对齐夹具。代理均未提交 Git，主线程合并后
确认工作树无并发写入残留。

按顺序执行的验证矩阵如下：

| 配置 | 全量构建 | `XGuiRegression_Test` | CTest |
| --- | --- | --- | --- |
| 默认 `build` | 通过 | 通过 | 1/1 通过 |
| `build-crop-painter-off` | 通过 | 通过 | 1/1 通过 |
| `build-crop-svg` | 通过 | 通过 | 1/1 通过 |
| `build-crop-jpeg` | 通过 | 通过 | 1/1 通过 |

JPEG 裁剪最后一次全量构建完成于本轮，随后立即运行回归程序和 CTest；画笔、SVG 裁剪在
最后一次目标重建后立即运行回归程序和 CTest。默认配置在所有源文件合并后重新执行全量
构建，并在同一轮执行回归程序和 CTest。`git diff --check` 通过，当前为 22 个已修改
文件、无提交。

回归输出中的以下诊断是项目既有行为，并非本轮失败：
`XRingBuffer_peek` 对 NULL size 的参数诊断（`Src/XContainer/XRingBuffer/XRingBuffer.c:246`）、
`XWindow_setTransientParent` 的无效父窗口诊断（`Src/XGui/Window/XWindow.c:959`）、
`XWindow_setWindowStates` 忽略 `WindowActive`（`Src/XGui/Window/XWindow.c:940`），以及
`XClass_deinit_base` 对 NULL vtable 的既有诊断（`Src/XClass/XClass.c:19`）。回归最终均输出
`XGui regression tests passed`。

构建仍会报告测试夹具既有的 `XSignal` 非兼容函数指针、丢弃 const 限定符和
`XEvent_delete_base` 参数类型警告；这些警告未归因于本轮 XGui 实现，故不宣称“零警告”。
`valgrind` 和独立 ASan/LSan 可执行文件在当前环境不可用，未宣称无泄漏。其余裁剪目录的
全量目标仍受仓库既有缺失源文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_host.c`、
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c` 阻塞，详见 10.257；已通过的
静态目标和本轮四套配置结果不受该阻塞影响。

10.260 中“CTest 和 `git diff --check` 待统一验证”的临时状态现由本节结果闭合；RLE4
夹具使用合法的五像素绝对块，控制值 2 保留为 Qt Delta 命令，不再把非法流当作绝对块。

### 10.262 2026-08-30 JPEG SOI 起始边界

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/jpeg/qjpeghandler.cpp:1092-1102`：
`QJpegHandler::canRead(QIODevice*)` 只 peek 设备起始的两个字节，并且必须为 `FF D8`；
因此前导垃圾不能被 JPEG 标记扫描器跳过后误认成图像。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecJpeg.c:2011-2024` 在统一 JPEG 解码入口
先检查输入、输出对象和 SOI 两字节，再进入既有标记/量化表解析。显式指定 JPEG 格式时也
遵守同一边界，避免自动探测与显式解码行为分叉。`xgui_regression_test.c:7570-7611` 增加
有效 JPEG 前插入 `DE AD` 的夹具，分别断言自动探测返回 Unknown、显式 JPEG 解码失败。

边界：该检查只负责 Qt 的文件起始格式边界，后续 JPEG 语法、采样和像素转换仍由现有裁剪
解码器处理；未引入 Qt 的完整 libjpeg 插件私有状态。默认和 `build-crop-jpeg` 目标、回归、
CTest 均通过；构建中的既有测试夹具警告、Valgrind/独立 ASan 缺失保持如实记录，未提交 Git。

### 10.263 2026-08-30 SVG XML 编码规范化

本轮依据 Qt 6.8 qtsvg 模块 `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/plugins/imageformats/svg/qsvgiohandler.cpp:62-65`：
普通 SVG 输入交给 `QXmlStreamReader`；并参考
`/home/xinyue/Qt/6.8.3/Src/qtsvg/tests/auto/qsvgplugin/tst_qsvgplugin.cpp:136-166` 的
UTF-8、UTF-16LE/BE、UTF-32LE/BE 编码夹具及统一读取断言。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecSvg.c:138-288` 新增轻量
`svgDecodeXmlText`，识别 BOM 与无 BOM 的 UTF-16/32 字节序，将 Unicode 标量转换为 UTF-8，
拒绝截断单元、非法代理项、超范围标量、NUL 和容量溢出；`probeSvgSize` 与 `decodeSvg` 统一
使用转换后的长度，避免 fallback `svgColor` 按原始字节数越界读取。该实现复用现有 DOM/栅格
路径，不把完整 XML 库引入嵌入式构建。

回归范围：`xgui_regression_test.c:7661-7750` 以同一矩形 SVG 构造 UTF-8、UTF-16LE/BE、
UTF-32LE/BE 五种字节流，均断言显式 SVG 解码成功并得到 `2x3` 尺寸；默认配置和
`build-crop-svg`（关闭矢量分支）目标、回归、CTest 均通过。

边界：轻量转换不处理 SVGZ 压缩、XML 声明中的其他字符集名称及完整 XML 错误恢复，普通
UTF-8 输入仍交给既有解析器；构建保留仓库已有警告，未运行 Valgrind/独立 ASan，未提交 Git。

### 10.264 2026-08-30 QIconEngine 默认接口注释与所有权

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:49-52,83-90,113-115,196-225,236-249,255-265,275-304`，
确认默认 `actualSize` 原样返回请求尺寸，默认 `pixmap` 通过 `paint` 创建结果，默认 `addPixmap`/
`addFile` 不保存数据，`key`/`iconName` 返回空字符串，`read`/`write` 返回 false，默认
`availableSizes` 为空，`isNull` 和 `scaledPixmap` 通过 `virtual_hook` 分派。

实现范围：`Src/XGui/Icon/XIconEngine.h:104-116,172-217` 清理重复释放注释，补齐基类释放宏
和 `key_base`、`iconName_base` 的返回值所有权说明；二者返回新建 `XString`，调用方必须使用
`XString_delete_base()` 释放。实现代码未改变，保持当前 C ABI 和 Qt 默认行为映射。

边界：本轮只修正公共头文件文档和所有权契约，不改变项目现有的输出对象初始化约定，也未
扩展 Qt 私有 `QDataStream` 序列化；默认回归与 CTest 已在本轮其他图像/图标验证中通过，
未提交 Git。

### 10.265 2026-08-30 SVG 多字节编码自动 Handler 发现

本轮补充对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/svg/qsvgtinydocument.cpp:42-52`
的 `hasSvgHeader()`：Qt 通过 `QTextStream` 处理多字节编码，并仅在前缀为 XML 声明或注释时
继续查找 `<svg`/`<!DOCTYPE svg`；同时依据
`/home/xinyue/Qt/6.8.3/Src/qtsvg/src/plugins/imageformats/svg/qsvgiohandler.cpp:88-100,220-223`
的 `canRead()`/静态探测入口，自动发现应与显式 SVG 解码接受同一组合法编码。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodec.c:35-114` 新增有界
`codec_svg_prepareProbe()`，识别 UTF-8/16/32 BOM 及无 BOM 的 UTF-16/32 字节序启发式，
将头部单元规范化为 ASCII 视图后复用既有 `<svg`、DOCTYPE、XML/注释前缀判断；
`XImageCodec_detect()` 使用最多 4096 字节的临时探测缓冲区（同文件 `:170-217`），避免
零字节编码被 C 字符串扫描提前截断，也不对不可信输入做无界遍历。

回归范围：`xgui_regression_test.c:7676-7748` 对五种编码同时断言显式解码尺寸为 `2x3`
以及 `XImageCodec_detect()` 返回 SVG，覆盖 UTF-8、UTF-16LE/BE、UTF-32LE/BE；默认、
`build-crop-svg`、`build-crop-jpeg` 全量构建，回归和 CTest 均通过。

边界：自动探测器只规范化有限头部并将非 ASCII 单元折叠为 `?`，完整 Unicode 合法性和
实体解析仍由 `XImageCodecSvg.c` 的显式解码路径处理；不实现 SVGZ 压缩、XML 声明中任意
字符集转换或 Qt 私有插件注册表。构建中的工程既有警告及 Valgrind/独立 ASan 缺失保持如实
记录，未提交、未推送 Git。

### 10.266 2026-08-30 XPainter 弧线跨度整圆截断

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qstroker.cpp:816-875`
的 `qt_curves_for_arc()`，其在生成弧线曲线前将 `sweepLength` 限制在正负 360 度；并核对
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:4090-4250` 的
`drawArc()`/`drawPie()`/`drawChord()` 高层调用，以及 `qpicture.cpp:504-532` 的回放分派。

实现范围：`Src/XGui/Graphics/XPainter.c:2465-2481,3648-3655,3681-3690,3727-3736`
新增 `painterClampArcSpan()`，仅在未绑定 `m_drawShape` 的便携折线栅格回退中把跨度限制为
`[-5760,5760]`（十六分之一度）；绑定 shape 回调仍收到原始跨度，保持 Picture/平台 opcode
协议不变，避免超整圆重复描边改变虚线相位。

验证：默认 `build`、`build-crop-painter-off`、`build-crop-svg`、`build-crop-jpeg` 均在
本轮改动后重新完成全量构建；四套配置的 `XGuiRegression_Test` 和 CTest 均通过，
`git diff --check` 通过。构建保留工程既有 XSignal/XEvent/const 警告，Valgrind 与独立
ASan/LSan 可执行文件不可用，未宣称零警告或零泄漏，未提交、未推送 Git。

边界：C API 使用整数十六分之一度，不涉及 Qt 浮点 NaN 情况；shape 回调收到的跨度归一化
仍由回调自行决定，复杂抗锯齿和 Qt 私有曲线缓存不在本轮嵌入式回退范围。

### 10.267 2026-08-30 图像插件 MIME 顺序与 SVGZ 回退

> 历史记录：本节描述当时尚未接入 gzip 解码器的边界；SVGZ 现已在 10.447
> 中补齐读取格式键和 Handler 探测，以下“不支持 gzip”的结论不再代表当前实现。

本轮复核并补充对照 Qt 6.8 图像插件辅助实现：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:84-116`
要求 `supportedImageFormats()` 与 `supportedMimeTypes()` 排序并去重，
`118-143` 的 `imageFormatsForMimeType()` 则按内置表和插件元数据出现顺序返回；
同时依据 qtsvg 插件元数据
`/home/xinyue/Qt/6.8.3/Src/qtsvg/src/plugins/imageformats/svg/svg.json:2-3`，
`svgz` 对应 MIME 为 `image/svg+xml-compressed`，而不是普通 SVG MIME。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:181-192` 将 `svgz` 的
回退 MIME 修正为 `image/svg+xml-compressed`；`696-749` 保留反向 MIME 查询的
内置优先及插件元数据顺序，同时继续对公开格式/MIME 列表排序去重；容量检查
`251-264` 在外部插件注册前预留内置处理器槽位。`xgui_regression_test.c:6145-6157`
新增普通 SVG MIME 存在、压缩 SVG MIME 不被内置处理器宣称的断言，覆盖嵌入式
实现不支持 gzip 解码时的能力边界。

边界：本地 `XImageCodec` 尚未实现 Qt `qsvgtinydocument.cpp:538-570` 的 gzip
解压，因此不把 `svgz` 加入内置 keys；当外部插件声明 `svgz` 但缺少 MIME 元数据时，
注册器仅按 Qt 元数据回退规则提供压缩 MIME。动态 `QFactoryLoader` 扫描、插件热加载及
同优先级元数据排序仍属于嵌入式架构边界。默认 `build` 与 `build-crop-svg` 的目标构建、
回归、CTest 以及 `git diff --check` 均已通过；构建中的工程既有测试夹具警告、Valgrind/独立
ASan 缺失保持如实记录，未提交、未推送 Git。

### 10.268 2026-08-30 QImage 色彩空间矩阵转换与显式格式重载

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:87-103,470-510`
的原色到 XYZ 矩阵构造、白点数据和有效目标判断，以及
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolortransform.cpp:1858-1912`
的颜色变换矩阵路径。图像新建默认分辨率对照
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:92-100` 的 96 DPI 换算为 3780 点/米。

实现范围：`Src/XGui/Graphics/XImage.c:73-83,853-1027` 新增 RGB 原色矩阵、Bradford
白点适配、目标矩阵求逆以及灰度 Y 分量转换；颜色传递函数仍按源空间解码、目标空间编码，
避免此前只变换 Gamma 而忽略原色的偏差。`XImageData_create()`（约 `:299-302`）为新图像
初始化 Qt 兼容的 X/Y 分辨率。`XImage_convertedToColorSpace_ex()`/`convertToColorSpace_ex()`
（`:1072-1141`）提供带目标像素格式的 Qt 6.8 风格重载，并在格式与色彩模型不兼容时保持空输出
或原图不变。`Src/XGui/Graphics/XColorSpace.c:422-510` 与头文件新增自定义原色设置，修改
原色、白点或传递函数时清除自动/用户描述，保留 Qt 的元数据失效语义；`XImage_text_2()`
（`:3224-3250`）将 NULL UTF-8 键等价为空 QString，返回全部文本聚合结果。

回归范围：`xgui_regression_test.c:8830-8980` 覆盖自定义原色坐标、无效空间白点保留、RGB
到灰度亮度、显式 Grayscale8/不兼容 ARGB32 格式转换、就地失败不改源；`:9535-9565` 覆盖
NULL 文本键和 96 DPI 默认值。默认 `build` 目标、回归、CTest 均通过；编译保留测试文件中
既有 XSignal/const/XEvent 警告，未运行 Valgrind 或独立 ASan/LSan，不能宣称无泄漏。

边界：当前 C99 `XColorSpace` 仍是三分量矩阵值类型，不承载 Qt ICC/LUT 元素列表；转换只
覆盖 RGB/Gray，CMYK 及复杂 element-list 目标按既有无效目标契约拒绝。浮点矩阵使用有限值
与行列式阈值保护，极端非法坐标会返回失败而不写入目标。

### 10.269 2026-08-30 BMP 位掩码、RLE 对齐与调色板兼容

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:138-172`
的 `calc_shift()`、`calc_scale()`、`apply_scale()`，以及 `:249-353` 的 DIB 掩码、调色板
和位深分支。Qt 对 Windows INFOHEADER（非 12 字节 Core Header）读取四字节 BGR 保留项，
对 16 位无压缩图使用 RGB555 掩码；RLE4 绝对模式按字边界消费填充字节。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:43-79` 逐位复制展开掩码，保证
RGB555/565 在低位值上与 Qt 一致；`:153-180` 修正 RLE4 绝对模式余数为 1/2 时的字对齐；
`:220-224` 仅把 108/124 字节识别为 V4/V5，避免将 52/56 字节头误当作内置 RGBA 头；
`:299-331` 按 Qt 优先读取四字节调色板并忽略第四字节，同时对仓库历史三字节 INFOHEADER
资源仅在像素偏移落入 `palettePos + 3*n` 且四字节布局容不下时回退，防止截断文件被宽松猜测
接受；索引色始终保留原始索引，越界颜色在取色时才表现为 0。普通 32 位 BI_RGB 只有 V4/V5
显式 `0xff000000` Alpha 掩码才读取高字节。

回归范围：`xgui_regression_test.c:7285-7348` 覆盖 RLE8 行程超宽钳制、RLE4 五像素绝对块
填充；`:8050-8115` 覆盖 Core、INFO、V4/V5、BITFIELDS、Mono 极性和历史调色板资源；新增
夹具像素/格式断言均通过。默认配置和 `build-crop-svg` 目标构建、回归、CTest 均已重新执行
并通过；构建中的测试文件既有警告保持不变，未运行 Valgrind/ASan，未提交 Git。

边界：三字节 INFOHEADER 回退是为已有嵌入式资源保留的兼容分支，不是 Qt 标准扩展；新生成
的标准 BMP 仍严格使用四字节调色板。RLE 流允许未显式铺满的尾部按 Qt 当前实现保留 0，完整
压缩错误恢复及少数平台私有 BMP 变体不在嵌入式裁剪范围。

### 10.270 2026-08-30 本轮安全边界与最终验证

本轮收束时补充检查了两个调用边界：`Src/XGui/Graphics/XImage.c:662-688` 在查找像素格式
表前先拒绝 `Invalid` 和越界枚举，保持 Qt `qimage.cpp:5080-5098` 对无效目标格式的失败语义，
避免不可信格式值触发表外读取；`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:322-345`
的历史三字节调色板兼容分支只有在像素偏移恰好等于 `palettePos + 3*n` 且四字节布局容不下时
才启用，标准 Windows BMP 仍严格按 Qt `qbmphandler.cpp:303-313` 的四字节条目读取。

最终验证已串行完成：默认 `build` 全量构建、`XGuiRegression_Test`、CTest 1/1；
`build-crop-svg` 与 `build-crop-painter-off` 全量构建及 CTest 1/1；`build-crop-jpeg`
的回归目标构建及 CTest 1/1；各回归程序均退出 0 并输出 `XGui regression tests passed`，
`git diff --check` 通过。构建仍保留测试文件中既有的 XSignal/const/XEvent 警告，当前环境
没有 Valgrind 或独立 ASan/LSan 可执行文件，因此不宣称零警告或无泄漏；本轮未提交、未推送 Git。

### 10.271 2026-08-30 QImageReader/QImageWriter 生命周期与状态

本轮复核 Qt 6.8 图像读写器实现：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1296-1375`
定义 GIF 动画的 `currentImageNumber()`、`jumpToImage()`、`jumpToNextImage()`、`nextImageDelay()`
和 `imageCount()` 状态语义；`:1211-1218` 要求动画缓存耗尽时由 handler 失败路径设置
`InvalidDataError` 与 `Unable to read image data`。环境整数解析对照
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qtenvironmentvariables.cpp:194-223`，其
按 `numeric_limits<uint>::digits` 计算长度上限，并以 base=0 接受十进制、八进制、`0x`/`0b`
前缀和首尾空白。写入器生命周期与错误状态对照
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:315-321,334-371,609-614,651-707`：
handler 保留到 writer 释放；`setText()` 采用简化描述；`write()` 在 handler 拒绝时立即返回，
不覆盖既有错误；仅内部 `QFileDevice` flush 且忽略 flush 返回值；支持 Gamma 的 handler 无条件
接收默认 `gamma=0.0`。

实现范围：`Src/XGui/Graphics/XImageReader.c:52-113` 将 `QT_IMAGEIO_MAXALLOC` 解析长度与
常规 C99 `uint` 位宽关联，并显式处理 `INT_MIN`，避免实现定义的强制转换；`:1431-1446,1596-1609,
1819-1838,2044-2052` 在 GIF 动画缓存存在时优先使用缓存状态、首次 delay 返回 100、首次查询
frame number 保持 -1、缓存耗尽设置 Qt 对应错误，并让 MIME 反向查询保留内置/插件元数据顺序。
`Src/XGui/Graphics/XImageWriter.c:96-105,251-266,463-472,790-846` 增加 Gamma 默认值及选项
应用，保留 handler，禁止失败后的编码器 fallback，并只 flush 内部文件设备；有效的 1x1 writer
插件拒绝夹具位于 `xgui_regression_test.c:6862-6905`，断言错误码和文本仍为
`UnknownError`/`Unknown error`。

验证：默认 `build`、`build-crop-svg`、`build-crop-painter-off`、`build-crop-jpeg` 的目标构建、
`XGuiRegression_Test` 与 CTest 均通过；本轮各测试程序退出 0 并输出 `XGui regression tests passed`，
`git diff --check` 通过。构建只保留仓库已有的 `XSignal` 不兼容函数指针、const 丢弃和 XEvent
删除参数警告；当前环境无 Valgrind/独立 ASan/LSan，不能宣称无泄漏，未提交、未推送 Git。

边界：嵌入式 reader/writer 仍不提供 Qt 动态 `QFactoryLoader` 的全部插件元数据和任意
`QIODevice` 类型识别；GIF 及环境变量逻辑依赖项目现有 `int`/`uint` 宽度。MIME 顺序只保证
内置表和已注册插件声明顺序，插件发现、热加载和未支持格式的完整 Qt 设备错误文本仍不在裁剪层。

### 10.272 2026-08-30 XPainter 零长度线审计

本轮复核 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine.cpp:762-779`
的默认 `QPaintEngine::drawLines()`，其在 `FlatCap` 下会跳过零长度线；同时对照
`qpaintengine_raster.cpp:3169-3264` 和 `qcosmeticstroker.cpp:342-360`，栅格快速路径仍会将
零长度线作为点绘制。当前 `Src/XGui/Graphics/XPainter.c:2816-2831` 逐条转发到
`XPainter_drawLine()`，保持栅格回退而非抽象默认引擎的点绘制语义；强行过滤会改变已有
嵌入式输出，因此本轮未修改 XPainter 源码。

验证：XPainter 代理复核默认 `build` 目标和 `git diff --check` 通过；没有新增文件或测试失败。
未运行 Valgrind/ASan/LSan，未提交、未推送 Git。

### 10.273 2026-08-30 PNG chunk 结构、CRC 与溢出边界

本轮对照 Qt 6.8 使用的 libpng 实现：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/3rdparty/libpng/pngrutil.c:153-176`
校验 chunk 名称 ASCII 字母及保留位，`:207-215` 拒绝最高位非零的长度；`:901-915,3065-3075`
规定 IHDR 只能出现一次且长度必须为 13 字节；`:987-1017,1041-1044,1082-1107` 规定
调色板图 PLTE 的顺序、长度、截断与关键 CRC，非调色板 PLTE 按可选块忽略异常；`:1705-1771`
规定 tRNS 的前置 PLTE、长度、重复与 CRC 宽松处理；`pngread.c:105-164,699-748` 规定
IHDR/PLTE/连续 IDAT/IEND 的读取状态，`:2980-2981` 对未知关键块报错。Qt 输出路径另见
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpnghandler.cpp:220-239`。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecPng.c:131-178` 加固扫描行字节数、chunk
名称及 CRC 读取；`:199-365` 实现 IHDR 唯一性、chunk 长度/顺序、连续 IDAT、未知关键块和
关键 CRC 校验，并按 libpng 规则处理非调色板 PLTE、tRNS、IEND 的可选错误；`:418-459,496-515`
增加 Adam7 pass、原始流累加、zlib `uLong` 及样本缓冲的溢出保护；`:575-625` 使 16 位灰度/RGB
带 tRNS 的图像输出 RGBA64 并保留透明样本。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、可执行文件和 CTest 均通过，程序退出 0
并输出 `XGui regression tests passed`；`xgui_regression_test.c:7669-7696` 新增有效 1x1 PNG
IHDR CRC 损坏夹具并确认关键块被拒绝；`build-crop-svg`、`build-crop-painter-off`、
`build-crop-jpeg`、`build-crop-clip` 的目标构建、可执行文件和 CTest 也均通过；`git diff --check` 通过。编译仍有工程既有
`Library/zlib/zconf.h:255` 条件编译警告，测试文件保留既有 XSignal/const/XEvent 警告；未运行
Valgrind 或独立 ASan/LSan，不能宣称无泄漏，未提交、未推送 Git。

边界：当前 PNG 裁剪实现仍不覆盖 APNG、完整 libpng 颜色管理/ICC 元数据和所有 ancillary
chunk 语义；非调色板 PLTE、tRNS、IEND 的 malformed CRC 按 Qt/libpng benign warning 继续，
未知关键块和关键图像数据错误则拒绝。解压和图像分配受 `UINT_MAX`/`size_t` 检查约束，属于
嵌入式安全限制；16 位灰度/RGB 的 Qt 输出分支还对应 `qpnghandler.cpp:220-239,307-319`。

### 10.274 2026-08-30 GIF LZW、动画尺寸与透明处置

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:15`
的 `Q_TRANSPARENT` 常量，`:42-45` 的 `withinSizeLimit()` 面积上限，`:305-330` 的逻辑屏幕
分配与清零，`:348-375` 的帧边界裁剪及首帧局部背景初始化，`:450-550` 的 LZW 解码、越界
像素跳过和透明索引写入，以及 `:150-177` 的 RestoreBackground 固定透明色语义；GCE 延迟和
处置字段依据 `:609-627`，透明调色板颜色读取依据 `:1023-1031`。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecGif.c:36-64` 加固位流读取的字节边界
与 `size_t` 溢出，`:144-187` 防止子块收集扩容回绕，`:268-307` 对齐默认 100ms 延迟、逻辑
屏幕面积限制、背景索引越界和 RGB32 黑色画布，`:315-345` 兼容 Qt 的 GCE 可变块长度并安全
跳过子块；`:390-465` 接受超出逻辑画布的局部帧并仅写入有效像素，首帧透明索引写入带 RGB 的
透明色，后续透明索引保留底图；`:509-518` 对 RestoreBackground 使用 Qt 固定
`0x00ffffff`，`:531-540` 保留前一帧快照及 GCE 后续帧延迟状态。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest 均通过，程序退出 0
并输出 `XGui regression tests passed`；`build-crop-svg`、`build-crop-painter-off`、
`build-crop-jpeg`、`build-crop-clip` 的目标构建、程序运行和 CTest 也均通过；`git diff --check`
通过。构建保留工程既有 zlib 条件编译及测试夹具警告，未运行 Valgrind 或独立 ASan/LSan，不能
宣称无泄漏；`build-crop-min` 仍因既有缺失文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c`（CMake 自动收集后参与编译）失败，未提交、未推送 Git。
其它既有组合中 `build-crop-layout`、`build-crop-polygon`、`build-crop-renderhint`、
`build-crop-shape`、`build-crop-stacked-off`、`build-crop-world-matrix` 的目标构建、回归和
CTest 通过；`build-crop-background-off`、`build-crop-bgbrush-off`、`build-crop-image-rect-off`、
`build-crop-painter`、`build-crop-path`、`build-crop-region-off`、`build-gui-crop` 同样在该
既有 TinyUSB 缺失源文件处阻塞，未进入测试阶段。

边界：GIF 仍采用完整数据缓存解码，不实现 Qt 增量 `imageIsComing()`、逐帧独立尺寸和所有
应用扩展；编码器继续输出固定调色板单帧。逻辑屏幕和单帧索引缓冲受 Qt 面积上限及项目
`size_t` 检查约束，畸形 LZW/子块会安全拒绝或结束当前动画。

### 10.275 2026-08-30 QImageIOHandler 分配上限与失败保留语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:532-557`
的 `QImageIOHandler::allocateImage()`：空尺寸和非法格式在分配前直接拒绝；同尺寸同格式只
执行 `QImage::detach()`；需要新图像时按 `max(qt_depthForFormat(format), 32)` 计算有效深度，
调用 `QImageData::calculateImageParameters()` 检查行跨度、总字节数和 `INT_MAX` 溢出，再按
`QImageReader::allocationLimit()` 的 MB 边界拒绝超限分配。计算细节依据
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:88-115`，其中高度、乘法溢出及
`width > (INT_MAX - 31) / depth` 均属于无效参数。Qt 成功后才替换输出图像，失败不会破坏调用方
已有内容。

实现范围：`Src/XGui/Graphics/XImageIOHandler.c:309-354` 新增安全的 64 位分配参数计算，按
有效 32 位深度对齐 Qt 上限并额外校验 XImage 的 `int` 行跨度/总字节数表示范围；
`:356-390` 实现空尺寸、非法格式、同尺寸复用、allocationLimit 检查及成功后替换旧图像，
失败路径不释放或覆盖旧图像。公共接口注释位于
`Src/XGui/Graphics/XImageIOHandler.h:311-322`，说明尺寸、格式、输出对象生命周期和返回值。
`xgui_regression_test.c:6079-6148` 夹具覆盖空尺寸、非法格式、有效分配、同尺寸像素复用和
1 MB 限制下的失败保留。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest 均通过，程序退出 0
并输出 `XGui regression tests passed`；`git diff --check` 通过。编译只保留测试文件既有的
`XSignal` 不兼容函数指针、const 丢弃及 XEvent 删除参数警告；未运行 Valgrind 或独立
ASan/LSan，不能宣称无泄漏，未提交、未推送 Git。

边界：XGui 使用 C99 `int` 尺寸及项目图像格式枚举，分配量在通过 Qt 等价检查后还必须能由
本地 `XImageData` 表示；动态 QImage 插件发现、任意 `QIODevice` 设备语义和 Qt 的完整警告
文本仍由裁剪层省略。

### 10.276 2026-08-30 QPixmapCache 主线程门控、Key 替换与 LRU

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmapcache.cpp:60-77`
的默认 10240 KB 限制、`cost()` 的 64 位宽高深度计算和最小 1 KB 开销；
`:428-445,478-499` 规定字符串键与 Key 插入/查找必须先经过
`qt_pixmapcache_thread_test()`，工作线程直接失败且不修改缓存；`:529-559` 规定
`cacheLimit`、`setCacheLimit`、字符串/Key 移除的线程门控；`:567-587` 规定
`clear()` 的主线程行为及 `QCoreApplication::closingDown()` 期间的清理例外。Qt 6.8
`QPixmapCache::replace` 的兼容内联实现见
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmapcache.h:62-69`，语义是
`remove(key); key = insert(pixmap);`，所以旧 Key 副本失效而传入 Key 重新绑定新条目。

实现范围：`Src/XGui/Graphics/XPixmapCache.c:23-56` 通过 XThread/XThreadData 抽象实现
主线程门控，并在无 XThread 配置时退化为单线程；`:344-437,557-621` 为所有公共查找、
限制、移除和清空入口补充线程检查；`:358-367` 保留负 cache limit 并按 Qt/QCache 规则修剪；
`:539-555` 改为先移除旧 Key 再插入并绑定新 KeyData，确保旧副本失效。缓存开销计算位于
`:252-260`，仍使用宽乘法、最小 1 KB 和 `INT_MAX` 上界；`:605-623` 在正常运行时限制主线程，
但在 `XCoreApplication_closingDown()` 阶段允许跨线程清理。头文件
`Src/XGui/Graphics/XPixmapCache.h:1-11,92-167` 更新了主线程限制、负限制和 Key 生命周期的
中文公共注释。
`xgui_regression_test.c:1672-1795` 覆盖负限制、Key 副本失效、replace、重复插入、LRU 和
超限拒绝；`:1797-1884` 使用 pthread 夹具验证工作线程查找/插入/替换/限制/移除/清空均被
忽略，主线程缓存和 Key 仍保持有效。

验证：默认 `build` 的完整构建、`XGuiRegression_Test`、程序运行和 CTest 均通过，程序退出 0
并输出 `XGui regression tests passed`；`git diff --check` 通过。编译保留工程已有的 zlib
条件编译及测试夹具函数指针/const/XEvent 参数警告；未运行 Valgrind 或独立 ASan/LSan，不能
宣称无泄漏，未提交、未推送 Git。

边界：Qt 的 `QPixmapCache` 依赖真实 `QCoreApplicationPrivate::theMainThreadId`，XGui 在未
创建核心应用时以首次取得的 XThreadData 建立本地 adopted 主线程，以维持无应用单元测试可用；
完整 Qt QCache 异常安全和 GUI 平台像素后端不在 C99 裁剪实现范围。关闭 XSYNC、XTHREADDATA
或 XTHREAD 时仅保留单线程缓存行为。

### 10.277 2026-08-30 图像插件初始化失败与写入处理器选择

本轮复核 Qt 6.8 图像插件工厂路径：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:178-188`
在格式键命中的首个 `CanWrite` 插件上调用 `create()`，无论工厂返回有效处理器还是
`NULL` 都结束插件遍历，不再回退到同键的后续插件或内置编码器；插件工厂失败应由写入器
保留其未知错误状态。插件加载失败语义沿 Qt `QFactoryLoader` 初始化约定处理：未完成成功
初始化的加载器不能被标记为永久已加载，后续查询必须允许重试。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:79-95` 的
`XImagePluginRegistry_ensureBuiltin()` 仅在 `addPlugin()` 成功后设置已注册标志，避免一次
性分配失败或容量边界导致内置 BMP/PNG 等格式永久消失；`:514-548` 的
`createWriteHandler()` 按 Qt 首个可写插件即终止规则返回 `handler`（包括 `NULL`），不再
静默改用其它插件。实现保留固定容量静态注册表和显式插件所有权约定，并以中文注释说明
失败重试及错误传播原因。

验证：默认 `build` 全量构建、`XGuiRegression_Test`、CTest 1/1 和 `git diff --check` 均通过，
回归程序退出 0 并输出 `XGui regression tests passed`（保留工程既有 XError 诊断）。当前仅
保留仓库已有 zlib 条件编译及测试夹具函数指针/const/XEvent 参数警告；环境无 Valgrind 或
独立 ASan/LSan，不能宣称零警告或无泄漏；本轮未提交、未推送 Git。

边界：固定容量注册表仍不实现桌面 Qt `QFactoryLoader` 的目录扫描、元数据排序、热加载与
卸载；写入器只对已注册插件执行首个匹配规则，插件动态发现和完整 `QIODevice` 错误文本
仍属于嵌入式裁剪范围。

### 10.278 2026-08-30 主题递归访问栈前置声明与裁剪编译复核

本轮在 `build-crop-painter-off`（`XPAINTER_ON=0`）的干净增量编译中发现，主题继承递归
函数 `theme_selectEntryType()` 先于其静态辅助函数定义调用 `theme_visitContains()`、
`theme_visitPush()` 和 `theme_visitPop()`，C99 编译器会先产生隐式外部声明，随后与静态
定义冲突。该问题在默认构建的旧对象未重编译时不会显现，属于裁剪配置暴露的真实源码错误。

实现范围：在 `Src/XGui/Icon/XIconThemeInternal.c` 的主题递归辅助函数前置声明区域保留三个
静态函数原型，保持原有实现位置、递归访问栈和主题回退逻辑不变；未改变运行时 API 或
平台行为。声明与实现签名保持一致，避免隐式声明及 `static` 冲突。

验证：修复后 `build-crop-painter-off` 目标 `XGuiRegression_Test` 构建成功，回归程序退出 0
并输出 `XGui regression tests passed`，CTest 1/1 通过；`build-crop-jpeg`、`build-crop-svg`
目标构建、回归程序和 CTest 亦各 1/1 通过；默认 `build` 全量构建、回归程序和 CTest 通过，
`git diff --check` 通过。构建日志仍保留仓库原有 `XSignal` 不兼容函数指针、const 丢弃、
XEvent 删除参数及第三方 zlib 条件编译警告，本轮没有新增主题源码警告；未运行 Valgrind、
ASan 或 LSan，不能宣称无泄漏，未提交、未推送 Git。

边界：本修复只补齐 C99 编译声明，不改变主题引擎当前固定容量/静态路径扫描的嵌入式边界；
`icon-theme.cache`、动态主题插件发现及完整平台主题工厂仍按前述章节保持未实现。

### 10.279 2026-08-30 QIcon 主题条目类型与实际尺寸选择

本轮复核 Qt 6.8 图标主题查找和尺寸语义：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:446-570`
规定当前主题、`Inherits`、`hicolor` 及短横线名称回退；`:572-609` 规定独立
PNG/XPM/SVG 文件回退；`:764-853` 规定目录尺寸的精确匹配、最近尺寸选择及格式优先级；
`:878-880` 规定只有实际选中的 `Scalable` 条目才返回请求矩形；`:915-995` 提供可用尺寸
和条目元数据；`qicon.cpp:386-420` 规定引擎 `actualSize()`/`availableSizes()`，
`:1376-1436` 规定主题查找、回退与缓存入口。

实现范围：`Src/XGui/Icon/XIconThemeInternal.c:517-525` 增加主题递归访问栈的完整静态原型，
并新增解析目录的格式优先级、精确/最近尺寸及固定/可缩放条目选择；
`Src/XGui/Icon/XIconThemeInternal.h` 暴露对应内部查询契约。`Src/XGui/Icon/XIconThemeEngine.c:94-169`
的 `actualSize()` 现在只在请求尺寸真正命中 Scalable 条目时返回完整请求矩形，混合固定 PNG
与 SVG 时保留固定条目尺寸；非方形独立回退文件按源图像宽高比缩放。`:255-270` 的
`iconName()` 返回短横线回退后实际命中的名称。新增回归夹具覆盖混合 Fixed/Scalable 主题、
继承和独立文件回退。

验证：默认配置与 `build-crop-jpeg` 的 `XGuiRegression_Test` 构建及程序运行均通过，CTest
各 1/1 通过；此前 `build-crop-painter-off` 也在相同主题改动下构建、回归和 CTest 通过；
`git diff --check` 通过。图标源码无新增编译警告，测试文件仅保留既有函数指针/const/XEvent
诊断；未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：主题引擎仍采用嵌入式固定容量和静态路径扫描，不实现 Qt `icon-theme.cache` 二进制
索引、动态主题插件工厂、设备像素比参数化缓存及完整平台主题后端；SVG 仍由现有轻量解析器
负责延迟解码。

### 10.280 2026-08-30 QImage 缩放同尺寸共享与元数据复制

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3009-3031` 的
`QImage::scaled()`：目标尺寸经过宽高比解析后与源图像相同，直接返回源图像共享数据，不重新
分配像素或生成新的缓存键。缩放结果的物理及逻辑元数据依据 `qimage.cpp:1163-1174` 的
`copyPhysicalMetadata/copyMetadata`，并参考 DPR、DPM/偏移和文本接口
`:1478-1533,4082-4173,4184-4240`。

实现范围：`Src/XGui/Graphics/XImage.c` 的 `XImage_scaled()` 在有效目标尺寸等于源尺寸时
调用 `XImage_copy_base()`，保留隐式共享、`cacheKey` 及源图像所有权语义；普通缩放完成后
调用现有 `XImageData_copyMetadata()`，现在完整保留 `dotsPerMeterX/Y`、设备像素比、偏移、
色彩空间和文本元数据，而不再只复制前四项。`xgui_regression_test.c` 文本元数据夹具新增
2x2 缩放后的元数据断言，以及 1x1 同尺寸缩放的 `cacheKey` 共享断言。

验证：默认 `build` 的 `XGuiRegression_Test` 构建、程序运行和 CTest 1/1 通过；
`build-crop-jpeg` 与 `build-crop-svg` 的目标构建、回归程序和 CTest 各 1/1 通过；
`git diff --check` 通过。仅保留工程既有测试函数指针、const、XEvent 删除及第三方 zlib
条件编译警告；未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：缩放仍采用 XGui 轻量后端的采样和模式实现，未覆盖 Qt 平滑缩放的全部 SIMD/高质量
滤波细节；本轮只对齐同尺寸共享和物理/文本元数据传递。

### 10.281 2026-08-30 QIcon 文件构造与多帧按需加载

本轮复核 Qt 6.8 `QPixmapIconEngine::addFile()` 与文件构造路径：
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:447-493` 规定空尺寸文件
需要登记全部帧；支持 `Size` 的处理器按帧尺寸登记文件名，其他处理器逐帧读取像素，
ICO 还需按原始深度为同尺寸帧选择高质量版本。`qicon.cpp:764-785` 规定
`QIcon(fileName)` 只登记文件并延迟加载，不能在构造时强制读取第一帧。

实现范围：`Src/XGui/Icon/XIcon.c:268-337,415-431,1213-1221` 新增统一文件条目登记：
无尺寸请求时通过 `XImageReader` 枚举支持的多帧尺寸，或读取全部可用帧；裁剪配置下
无长期处理器时退回单次 `XPixmap_load()`，避免单帧处理器循环读取。文件构造与显式
`addFile()` 均保存文件名及逻辑尺寸，像素在首次取图时加载；懒加载失败会删除失效条目，
使 `isNull()`/`availableSizes()` 不再报告不可解码资源。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 的目标构建、回归程序和 CTest
均通过，程序退出 0 并输出 `XGui regression tests passed`；`git diff --check` 通过。
构建保留测试夹具中既有的 `XSignal`、const 和 `XEvent_delete_base` 警告；未运行
Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：嵌入式 `XImageReader` 仍不实现 Qt ICO 专用原始深度元数据选择，主题/文件图标
缓存仍使用 XGui 固定容量实现；动态 `QFactoryLoader` 插件发现和完整设备错误文本不在
当前裁剪范围。

### 10.282 2026-08-30 QImage 空矩形复制与文本键默认值

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1218-1240`：
`QImage::copy()` 收到 `QRect::isNull()`（宽高均为零）时复制完整图像、颜色表和全部元数据，
而仅一边为零或任一边为负时返回空图像。另依据 `qimage.h:277-279` 与
`qimage.cpp:4184-4240`，`text()` 的默认空键返回按键排序后的聚合文本，兼容重载中的
空指针应等价于默认构造的空字符串。

实现范围：`Src/XGui/Graphics/XImage.c:2524-2552` 将零宽零高矩形视为整图复制，保留
像素、颜色表、色彩空间、分辨率、DPR、偏移及文本元数据；单边空矩形仍安全返回空图像。
`:3248-3260` 使 `XImage_text_2(self, NULL)` 进入空键聚合分支，不把 NULL 键误判为
缺失值。回归夹具位于 `xgui_regression_test.c:9763-9778,9733-9738`，覆盖非零原点
空矩形、元数据复制和 NULL 文本键。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 目标构建、回归程序和 CTest
均通过，程序退出 0 并输出 `XGui regression tests passed`；`git diff --check` 通过。
仍保留仓库既有测试夹具与第三方 zlib 编译警告；未运行 Valgrind、ASan 或 LSan，不能
宣称无泄漏；未提交、未推送 Git。

边界：XGui 的矩形复制仍受 `XImage` 的 `int` 尺寸和便携分配上限约束，未引入 Qt 桌面
后端的共享像素特殊步幅；文本聚合格式沿用当前 C99 实现，不覆盖 Qt 的全部 Unicode
排序/隐式共享细节。

### 10.283 2026-08-30 QColorSpace 描述保留与预定义白点精度

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.cpp:846-850`
（传递函数编辑）、`:977-980`（预定义原色编辑）、`:1009-1012`（自定义原色编辑）及
`:1051-1054`（白点编辑）：这些 setter 仅使自动识别描述失效，不能覆盖
`setDescription()` 写入的用户描述。预定义白点常量依据
`qcolormatrix_p.h:73-76`，D65 使用 `0.31271/0.32902`，D50 使用
`0.34567/0.35850`。

实现范围：`Src/XGui/Graphics/XColorSpace.c:45-74,425-512` 保留独立的
`m_userDescription`，编辑白点、预定义/自定义原色和传递函数时只清除自动描述；新增
`XColorSpace_setPrimariesData()` 对标 Qt 自定义原色 setter，并在四个 CIE xy 坐标非法时
拒绝写入。`XColorSpace.h:257-298` 同步中文契约注释及参数边界。回归夹具
`xgui_regression_test.c:8970-9160` 覆盖精确 D65/D50、用户描述保留、未知组合自动描述清空
以及非法自定义原色忽略行为。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标构建、程序运行及 CTest 均通过（各 1/1）；`git diff --check`
通过。构建日志仍包含仓库既有测试文件、XSignal、const、XEvent 删除及第三方 zlib 警告，
本轮未新增 XColorSpace 源码警告；未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、
未推送 Git。

边界：轻量 C99 值类型仍未实现 Qt 私有 ICC profile、ElementListProcessing 及完整色彩
适应矩阵；自定义白点/原色仅更新当前元数据模型，描述文本长度仍受固定缓冲区限制。

### 10.284 2026-08-30 QMovie 播放游标、循环与状态语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qmovie.cpp:250-263`
（reset）、`:297-408`（未知帧数和非连续跳帧）、`:421-464`（循环结束）、
`:468-506`（帧信号顺序）、`:512-527`（已读帧有效性）、`:533-565`（jump/frameCount）、
`:632-692`（设备/文件/格式）、`:781-830`（延迟与有效性）、`:883-920`（暂停/速度）、
`:944-972`（start/stop）及 `:979-994`（scaledSize）。

实现范围：`Src/XGui/Graphics/XMovie.c:50-220,404-669` 增加下一帧、最大已读帧、延迟、
循环计数和首次迭代状态；连续帧直接调用 `XImageReader_read()`，非连续访问才调用
`jumpToImage()`；未知 `imageCount()` 允许顺序读取并在末尾生成帧数；延迟按 Qt 的 64 位
速度换算，非动画多帧默认 1000ms；首次播放信号顺序统一为 started、resized、updated、
frameChanged；start/stop/pause、格式和缩放属性不再误清空当前帧。`XMovie.h:3-7,355-399`
同步播放和缩放契约注释。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 构建、程序运行及 CTest 均通过（各 1/1）；`git diff --check` 通过。
工程原有测试函数指针、const、XEvent 删除及第三方 zlib 警告仍存在；未运行 Valgrind、ASan
或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：C99 版本没有 Qt 的 QTimer 事件循环和 `CacheAll` 的 QMap 帧缓存，动画推进仍由同步
`XMovie_jumpToNextFrame()` 驱动；非 GIF/插件格式的帧能力继续受 `XImageReader` 裁剪后端限制。

### 10.285 2026-08-30 QSurfaceFormat 默认值、Alpha 与相等比较

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qsurfaceformat.cpp:203-214`
（普通构造）、`:428-437`（`hasAlpha()`）、`:782-810`（进程级默认格式）及 `:827-842`
（相等比较），并参考 `qsurfaceformat.h:147-150` 的 `stereo()` 选项位语义。

实现范围：`Src/XGui/Style/XSurfaceFormat.c:51-61` 使普通 `XSurfaceFormat_create()` 独立于
全局 defaultFormat；`:163-168` 仅在 Alpha 位数大于零时报告有效；`:245-251` 以
`StereoBuffers` 选项位为 stereo 唯一来源；`:327-357` 按 Qt 字段集合比较，不比较
renderableType、colorSpace 和 C 兼容缓存字段。`XSurfaceFormat.h:67-76,231-236,387-412`
补齐中文公共契约。

验证：默认、JPEG/SVG 裁剪及 XPainter 关闭裁剪目标的目标构建、回归程序和 CTest 均通过；
`git diff --check` 通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：Qt `qwindow.cpp:216-237` 的窗口初始化会读取进程级 defaultFormat；该跨模块接线已在
后续 10.287 章节补齐，当前仅保留无平台后端的轻量窗口边界。

### 10.286 2026-08-30 QStyleHints 滚轮覆盖值回退

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qstylehints.cpp:618-624`：
只有正数的滚轮覆盖值有效，零或负数表示没有有效平台覆盖，应回到默认值 3。

实现范围：`Src/XGui/Style/XStyleHints.c:261-270` 在没有桌面平台主题查询层的嵌入式实现中，
对零/负值返回初始化默认 3；`Src/XGui/Style/XStyleHints.h:150-151` 保持默认值和返回语义的
中文注释，setter 仍保存原始覆盖值以便后续平台查询。

验证：默认 `build`、JPEG/SVG 裁剪和 XPainter 关闭裁剪的 `XGuiRegression_Test` 目标、程序及
CTest 均通过；`git diff --check` 通过。保留工程既有编译警告，未运行 Valgrind、ASan 或 LSan，
不能宣称无泄漏；未提交、未推送 Git。

边界：XStyleHints 仍没有 Qt 平台主题动态刷新和信号绑定，只提供固定默认值与显式 setter 的
轻量 C99 语义。

### 10.287 2026-08-30 QWindow 初始化进程级表面默认格式

本轮补齐 `QWindowPrivate::init()` 的跨模块默认值语义，依据 Qt 6.8
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qwindow.cpp:216-237`：窗口构造时
应从 `QSurfaceFormat::defaultFormat()` 读取进程级请求格式，而不是重新构造一个独立的
出厂默认值。

实现范围：`Src/XGui/Window/XWindow.c:386-394` 在 `XSURFACEFORMAT_ON` 开启时改用
`XSurfaceFormat_defaultFormat()` 初始化窗口格式；`Src/XGui/Window/XWindow.h:297-302`
同步注明该格式来源及后续可由 `XWindow_setFormat()` 覆盖的契约。裁剪掉表面格式时继续
使用现有 `XWindow_mergeFormat(NULL)` 回退，不引入平台 API。回归夹具
`xgui_regression_test.c:10811-10824` 设置并恢复进程级默认格式，确认后续新窗口读取该值。

验证：默认 `XGuiRegression_Test` 目标构建、程序运行和 CTest 通过；`git diff --check`
通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：窗口仍是无平台后端的轻量实现，不包含 Qt 屏幕连接、设备像素比动态更新及原生
表面创建；这些行为继续由现有 `XWindow`/`XPlatformWindow` 分层负责。

### 10.288 2026-08-30 XPainter 两点多边形闭合与画刷状态记录

本轮复核 Qt 6.8 `QPainter::drawPolygon()`：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:4558-4588`
只拒绝少于两个顶点，并由 `qpaintengineex.cpp:903-912` 将两个及以上顶点构造成闭合路径；
同时复核 `QPainter::setBrush()` 的脏状态传播，确保从渐变切回纯色后记录新的画刷状态。

实现范围：`Src/XGui/Graphics/XPainter.c:3569-3593` 允许闭合折线在 `n == 2` 时补绘末点到
首点的边，`:3841-3868` 令 `XPainter_drawPolygon()` 对两点输入使用闭合语义，`:4578-4610`
在清除渐变载荷并切换 `SolidPattern` 时补写可由 Picture 表达的 `SetBrush` 状态。回归夹具
`xgui_regression_test.c:3393-3405` 验证两点多边形产生正向及反向闭合边，既有画刷夹具验证
渐变切回纯色后的状态恢复。

验证：默认、JPEG/SVG 裁剪及 XPainter 关闭裁剪的目标构建、回归程序和 CTest 均通过；
`git diff --check` 通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：XPainter 仍受 `XPAINTER_POLY_MAX_POINTS` 固定容量和嵌入式折线栅格精度约束，复杂
渐变、纹理及画刷变换不能由当前 Picture 固定 opcode 完整序列化。

### 10.289 2026-08-30 QImage Alpha 合成、掩码和格式升级

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:4550-4582`
及 `qimage_p.h:265-315` 对齐 `setAlphaChannel()`：目标格式按 `qt_alphaVersion()` 选择
对应 Alpha 版本，已有 Alpha 使用 DestinationIn 乘法，非 Alpha8 源先转 Grayscale8，
并处理源目标别名及不同尺寸采样。`qimage.cpp:4591-4600` 规定索引/单色透明调色板参与
`hasAlphaChannel()`；`:3257-3289` 规定 `createMaskFromColor()` 对深度 32 图像按原始
存储值匹配；`:1161-1169` 规定掩码只复制物理元数据。

实现范围：`Src/XGui/Graphics/XImage.c:1590-1838` 完成 Alpha 版本升级、别名安全、
DestinationIn 合成、Alpha8/灰度源重解释和不同尺寸最近邻采样；`:1450-1490` 识别透明
调色板；`:1834-1847` 的掩码工厂只复制 dpm/DPR；`XImage.h:364-453` 同步中文契约。
回归夹具 `xgui_regression_test.c:9461-9555,9660-9697` 覆盖透明调色板、预乘原始存储、
Alpha 合成、格式升级和尺寸缩放来源。

验证：默认、JPEG/SVG 裁剪及 XPainter 关闭裁剪的 `XGuiRegression_Test` 目标、程序和
CTest 均通过；`git diff --check` 通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；
未提交、未推送 Git。

边界：不同尺寸 Alpha 源在无 Qt 光栅管线时使用最近邻采样，复杂平滑变换仍是嵌入式近似；
C99 实现不承载 Qt 私有共享像素步幅及完整颜色管理路径。

### 10.290 2026-08-30 QImage 像素格式 Alpha8 与行字节溢出边界

本轮对照 Qt `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:6229-6241`
的 `Format_Alpha8` 像素表及 `qimage_p.h:88-114` 的
`calculateImageParameters()` 宽度检查：Alpha8 使用 Alpha 模型、UsesAlpha 与
Premultiplied；计算扫描行时必须先拒绝超过 `(INT_MAX - 31) / depth` 的宽度，不能让
后续位数乘法在 `int` 中回绕。

实现范围：`Src/XGui/Graphics/XImageFormat.c:46-55,108-125,330-339` 修正 Alpha8 快速
属性查询、像素格式转换和宽度溢出保护；回归夹具 `xgui_regression_test.c:9556-9589`
覆盖 Alpha8 预乘描述及极限宽度返回 0。

验证：默认 `XGuiRegression_Test` 构建、运行和 CTest 通过，目标文件 `git diff --check`
通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：像素格式仍是 XGui 固定枚举和值描述，不包含 Qt 私有 SIMD、ICC 元数据和所有平台
特定扫描线步幅优化。

### 10.291 2026-08-30 PNG/BMP/GIF 调色板与透明首帧格式

本轮继续对照 Qt 6.8 图像处理器：`qpnghandler.cpp:203-218,258-286` 规定 1 位灰度和
1 位调色板 PNG 必须输出 Mono 图像，并按灰度/调色板顺序建立颜色表；`qbmphandler.cpp:77-102,303-313`
规定仅 `biSize == 12` 的 BMP Core Header 使用 3 字节调色板条目，其他 DIB 头按声明的实际
头长度定位并消费 4 字节条目；`qgifhandler.cpp:317-330` 规定首帧画布延迟分配，并在透明
图形控制扩展存在时选择 ARGB32，否则使用 RGB32。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecPng.c` 将 1 位灰度/调色板输入映射为
Mono 并保持对应颜色表反转语义；`XImageCodecBmp.c` 按 Core Header 与其他 DIB 头区分调色板
条目宽度及像素偏移；`XImageCodecGif.c` 延迟首帧画布分配并按透明 GCE 选择输出格式。回归
夹具位于 `xgui_regression_test.c` 的 PNG 1 位灰度/调色板、BMP Core/INFO 头及 GIF 透明首帧
断言。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标构建、程序运行与 CTest 均通过（各 1/1）；`git diff --check`
通过。保留工程既有 zlib、测试函数指针和 XClass 类型兼容警告；未运行 Valgrind、ASan 或
LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：PNG 仍不覆盖 APNG、完整 libpng ICC/ancillary 元数据；BMP 保留项目对历史三字节
INFOHEADER 的兼容分支；GIF 仍采用嵌入式缓存解码，不实现 Qt 增量 `imageIsComing()` 和
逐帧独立尺寸的完整桌面行为。

### 10.292 2026-08-30 PNG 调色板 tRNS 保持索引格式

本轮复核 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpnghandler.cpp:258-286`：
调色板 PNG 在读取 `tRNS` 后仍按位深分配 `Format_Mono`（1 位）或 `Format_Indexed8`，
先建立完整颜色表，再把透明表项的 Alpha 写入颜色表；Qt 不会因调色板透明度把像素展开为
`ARGB32`。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecPng.c:546-578` 删除透明调色板的
ARGB32 展开分支，统一生成索引像素；`tRNS` Alpha 合并到颜色表，1 位调色板始终建立两项
颜色表并对缺失的第二项保留默认黑色，和 Qt 的 `setColorCount(2)` 契约一致。回归夹具
`xgui_regression_test.c:8127-8157,8172-8174` 改为断言透明调色板仍为 Indexed8、颜色表 Alpha
及 `pixelIndex()` 保持不变，并覆盖普通 8 位调色板资产的 Indexed8 格式。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 均逐一
完成 `XGuiRegression_Test` 目标构建、程序运行和 CTest（各 1/1）；并行链接曾因四个配置共用
`bin/XGuiRegression_Test` 产生重定位损坏，改为顺序编译后验证通过。`git diff --check` 通过。
构建仍保留工程既有 zlib、测试函数指针、const 和 XEvent 类型兼容警告；未运行 Valgrind、
ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：PNG 仍不实现 APNG、完整 ICC/ancillary 元数据和 libpng 的全部容错警告策略；1 位调色
板输入若缺少第二个 PLTE 项时以黑色补齐颜色表，属于 Qt `setColorCount(2)` 在轻量 C99 模型下
的等价表示。

### 10.293 2026-08-30 scaledToWidth/scaledToHeight 非法尺寸替换语义

本轮复核 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3033-3085`：
`scaledToWidth()` 与 `scaledToHeight()` 在源图像为空或目标尺寸小于等于零时返回 null
`QImage`；输出对象应被完整替换，不能以重新初始化覆盖已有共享数据。

实现范围：`Src/XGui/Graphics/XImage.c:3112-3152` 在非法宽度、高度及缩放结果超过 `INT_MAX`
时先调用 `XImage_deinit_base()` 释放目标原有数据，再初始化为空图像，避免共享引用泄漏；合法
路径继续按当前轻量最近邻/平滑缩放实现。回归夹具 `xgui_regression_test.c:9950-9957`
复用已有目标对象验证两种非法尺寸均清空输出。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 均逐一
完成目标构建、回归程序运行和 CTest（各 1/1）；`git diff --check` 通过。保留工程既有
第三方 zlib、测试函数指针、const 和 XEvent 类型兼容警告；未运行 Valgrind、ASan 或 LSan，
不能宣称无泄漏；未提交、未推送 Git。

边界：C99 版本没有 Qt 的 QTransform 高精度矩阵和完整平滑缩放后端，合法尺寸的像素采样
仍是嵌入式实现；仅本轮确认的非法目标尺寸替换及生命周期语义与 Qt 对齐。

### 10.294 2026-08-30 QPixmap 图像转换、空图与共享分离

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpixmap_raster.cpp:269-308`、
`qpixmap.cpp:882-888,955-957,969-976,1433-1445`：普通不透明图像转换为首选 RGB32，带 Alpha
图像转换为 ARGB32_Premultiplied，`NoFormatConversion` 保留源格式，
`NoOpaqueDetection` 保留 Alpha 格式；空 `QImage` 转换产生真正的空 `QPixmap`，空图 cacheKey
为 0 且 `isDetached()` 为 false；共享分离须保留原像素格式及 bitmap 类型。

实现范围：`Src/XGui/Graphics/XPixmap.c:69-127` 新增带转换标志和 bitmap 类型的图像构造路径，
`:239-252,703-707,773-791,1010-1042,1072-1078` 对齐 `fromImage`、
`convertFromImage`、`fromImageInPlace` 的空输入清理、格式选择、cacheKey、isDetached 与 detach
语义；`XPixmap.h:27-40` 增加转换标志枚举及中文契约注释。回归夹具
`xgui_regression_test.c:1382-1473` 覆盖空图、RGB16 到 RGB32 升级、Alpha 保留、禁止格式转换、
缓存键替换和共享分离行为。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标、程序运行及 CTest 均按顺序通过（各 1/1）；`git diff --check`
通过。保留工程既有第三方 zlib、测试函数指针、const 和 XEvent 类型兼容警告；未运行
Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：轻量 XPixmap 没有 Qt 平台光栅后端、GPU 纹理和完整 `Qt::ImageConversionFlag` 集合，
未实现平台专用格式转换、色彩管理及异步共享生命周期；转换标志之外的颜色抖动等选项继续由
现有 XImage 转换层处理。

### 10.295 2026-08-30 QBackingStore 后端句柄、尺寸与静态内容

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qbackingstore.h:19-48`、
`qbackingstore.cpp:81-89,197-225,232-238,271-298,352-359` 及平台接口
`qplatformbackingstore.h:130-187`：`QBackingStore::resize()` 先更新公共尺寸再请求平台
缓冲重建，`flush()` 的空窗口参数回退到构造时绑定窗口，`handle()` 允许后端句柄延迟创建，
而 `setStaticContents()`/`staticContents()` 保存调用方区域，不因当前后备图像容量而截断。

实现范围：`Src/XGui/Graphics/XBackingStore.c:21-88` 增加静态区域快照与懒创建句柄，
`:109-236` 统一经句柄调用绘制、缩放、滚动、提交及静态内容接口；后端不可用时仍保留窗口、
尺寸和静态区域等公共状态。`XBackingStore.h:43-143` 补充 `handle()` 借用句柄声明与中文
生命周期契约。回归夹具 `xgui_regression_test.c:12700-12813` 覆盖句柄有效性、尺寸更新、
越界静态区域保留、清空静态区域及窗口回退提交。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标构建、程序运行与 CTest 均按顺序通过（各 1/1）；默认构建完成后
再次运行回归程序及 CTest 仍为 0/1 失败。`git diff --check` 通过。工程既有 zlib、函数
指针及 XEvent 类型兼容警告保持不变；未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、
未推送 Git。

边界：XGui 仍不实现 Qt 平台后端的高 DPI/DPR 缩放、原生窗口句柄绑定和完整合成调度；
`toImage()` 是项目扩展接口，负尺寸仍按轻量实现安全拒绝。句柄懒创建依赖可选
`XPlatformIntegration`，无应用集成时返回 NULL，但公共尺寸和静态内容快照仍可查询。

### 10.296 2026-08-30 QBitmap 单色颜色表、清空与转换生命周期

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbitmap.cpp:142-151,165-177,202-215,229-248,286-288`：
`QBitmap` 必须保持一位深度，颜色表约定为 `Qt::color0=白`、`Qt::color1=黑`；普通
`QImage` 转换得到黑/白顺序时，`makeBitmap()` 会翻转位数据和颜色表；`fromData()` 要求
逐行复制字节并保留调用方指定的 Mono/MonoLSB 位序；空源应得到真正的空位图，重用输出对象
前需释放旧共享数据。

实现范围：`Src/XGui/Graphics/XBitmap.c:13-53,101-116,131-151,161-207,214-259` 增加
统一的输出生命周期复位、黑白颜色表归一化、位数据翻转、`clear()` 的 color0 清空以及
已有一位 `XPixmap` 的共享复制；无效文件、尺寸、源对象均安全替换为空位图。头文件公共
接口沿用原有中文契约。回归夹具 `xgui_regression_test.c:1680-1718` 验证 `fromData()` 的
颜色表/像素位序，并确认 `clear()` 后所有像素为白色 color0。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标构建、程序运行及 CTest 均按顺序通过（各 1/1）；`git diff --check`
通过。保留工程既有 zlib、函数指针、const 与 XEvent 类型兼容警告；未运行 Valgrind、ASan
或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：轻量位图仍依赖 `XImage` 的固定 Mono/MonoLSB 调色和抖动实现，不包含 Qt 平台位图
后端、`QBitmap` 与 `QPixmap` 的 paintingActive 分支及完整 `QVariant` 元类型注册；颜色表
和共享数据的核心可观察行为已与 Qt 对齐。

### 10.297 2026-08-30 最小裁剪配置构建边界

本轮在完成 QPixmap/QBackingStore/QBitmap 及图像编解码器验证后，额外尝试了最小裁剪配置
`build-crop-min`。该配置在编译与 XGui 无关的 TinyUSB 设备源时失败：
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c` 和
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_host.c` 文件不存在，生成规则位置为
`build-crop-min/CMakeFiles/XinYueCS.dir/build.make:6477,6491`。因此不能将该配置的全量
构建或回归结果宣称通过；默认构建、JPEG/SVG/Painter 关闭裁剪配置仍按上一节验证通过。
该缺失文件属于既有仓库配置问题，本轮未创建、删除或回退任何 TinyUSB 文件。

### 10.298 2026-08-30 QImage 调色板颜色空间转换与中间格式

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5211-5217`
和 `:5467-5527`：Indexed8、Mono、MonoLSB 图像的颜色空间转换只变换调色板颜色，
保存的索引字节必须保持不变；显式目标格式转换先选择可执行颜色变换的中间格式，完成
颜色变换后再转换到调用者请求的格式，避免把 ARGB 颜色误写成索引值。

实现范围：`Src/XGui/Graphics/XImage.c:950-1091` 增加调色板逐项转换和共享数据分离，
`:1285-1316` 对齐 Indexed/Mono 的中间格式选择、目标格式回转和色彩空间元数据写回。
回归夹具 `xgui_regression_test.c:9501-9522` 验证调色板变化、索引字节保持以及源图像
隐式共享隔离。该路径复用现有 XColorSpace 矩阵和传递函数实现，未引入平台 API 或标准
库分配器。

验证：默认 `build` 目标构建、程序运行和 CTest 均通过（1/1）；程序报告
`XGui regression tests passed`，仅保留既有 XError 诊断和工程兼容性警告；
`git diff --check` 通过。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：XColorSpace 仍为轻量三分量矩阵模型，不承载 Qt ICC/LUT 元数据和完整 CMYK 色彩
管理；未知或无效色彩空间按既有 XGui 合约返回空图像，不模拟 Qt 插件式色彩配置。

### 10.299 2026-08-30 QIcon 主题输出对象的安全替换

本轮依据 Qt `qicon.cpp:386-420` 的值对象构造语义，修正
`Src/XGui/Icon/XIcon.c:1278-1326` 的 `XIcon_fromTheme*` 输出生命周期：已绑定
`XIcon` 虚表的输出对象先释放旧 `XIconPrivate`，未初始化栈对象则直接初始化，避免随机虚表
被误当作可调用的析构表，同时保留主题引擎和回退图标行为。该判断解决了主题可用尺寸夹具
中未初始化 `XIcon splitIcon` 的段错误，并避免重复写入同一输出对象时丢失引用。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest 均通过（1/1），
程序报告 `XGui regression tests passed`；本轮未宣称 ASan/Valgrind 无泄漏，未提交、未推送 Git。

边界：C 接口仍要求输出对象存储可读的 `XIcon` 布局；仅识别精确的 `XIcon` 虚表，派生虚表
对象不会被自动析构，需调用方按其派生类型管理生命周期。主题缓存、内置插件和平台资源的
进程级生命周期泄漏仍属于既有实现边界。

### 10.300 2026-08-30 最终 sanitizer 检查记录

本轮在最新工作树上重新构建并运行 ASan/UBSan 回归程序，启用
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` 与 `UBSAN_OPTIONS=halt_on_error=1`。
未发现未定义行为或越界错误，所有回归断言均执行到末尾；但 LeakSanitizer 报告约
104482 字节、464 个分配仍存活，因此不能宣称 XGui 无泄漏。报告中约 100 KiB 来自
Mesa `libGLX_mesa.so` 和 fontconfig；可归属于 XGui 的条目包括 `XGeometry.c:78`
经 `XPlatformBackingStore_posix.c:137` 创建的 64 字节静态内容区域，以及图像插件夹具
经 `XImagePluginRegistry_createReadHandlerEx()` 创建但在进程级插件/处理器生命周期结束前
仍存活的两个 32 字节 handler、各自约 1240 字节私有数据和格式字符串。该结果记录为当前
生命周期边界，未改变既有测试的功能判定。

验证：随后重新构建默认 `XGuiRegression_Test`（恢复非 sanitizer 二进制），程序退出 0，
报告 `XGui regression tests passed`；默认 CTest 1/1 通过，默认全量构建通过；JPEG、SVG、
XPainter 关闭裁剪配置此前均逐一通过目标构建、回归和 CTest。`git diff --check` 通过，
未提交、未推送 Git。

### 10.301 2026-08-30 QImage 镜像、RGB 交换与反色格式语义复核

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1983-2048`
（`invertPixels()` 的分离、预乘格式中间转换及按深度处理）、`:3476-3512`
（`mirrored_helper()` 的同尺寸共享、调色板和物理/文本元数据复制）以及
`:3561-3655`（`rgbSwapped_helper()` 对 Mono/Indexed 调色板、32/64 位和浮点布局的分支）。

实现复核范围：`Src/XGui/Graphics/XImage.c:3098-3255,4071-4290`。镜像路径在无变换或
单像素图时保留隐式共享，正常输出复制颜色表和完整元数据；索引图 RGB 交换只改颜色表并
保持索引字节，Alpha8/灰度图按 Qt 语义原样返回；32/64 位、浮点和预乘格式先使用可逆的
非预乘表示，再按 `InvertRgb`/`InvertRgba` 处理 Alpha，避免产生非法预乘值。原地别名接口
通过临时图像保证源输出不被提前覆盖，失败时保留有效源数据。

验证：本轮默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off`
均已逐一完成目标构建、回归程序运行及 CTest（各 1/1）；默认全量构建随后通过，
`git diff --check` 通过，XGui 源码未出现标准 `malloc/calloc/realloc/free` 调用。未运行
Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：当前 C99 图像层仍不包含 Qt 的 SIMD 像素布局表、GPU 图像后端和所有实验性浮点格式
转换；未知格式按现有 `XImageFormat_Invalid` 合约拒绝，合法格式的可观察镜像、调色板、
元数据及预乘反色行为已在嵌入式实现能力内对齐。

### 10.302 2026-08-30 QImage 原地镜像与 RGB 交换无操作语义

本轮进一步对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3504-3512`
（`mirrored_inplace()` 对无变换或单像素图像直接返回）以及 `:3584-3605`
（`rgbSwapped_helper()` 对 Alpha8、Grayscale8、Grayscale16 直接返回共享图像，并对
 Mono/MonoLSB/Indexed8 统一交换颜色表）。

实现范围：`Src/XGui/Graphics/XImage.c:3160-3178` 的 `XImage_mirroredInPlace()` 在无操作、
单像素和临时结果分配失败时保留原图及 cacheKey；`:3198-3270` 的 `XImage_rgbSwapped()`
 现对 Mono/MonoLSB 调色表交换红蓝通道、保持索引字节不变，并让 Alpha8/Grayscale8/
Grayscale16 输出复用源图像共享数据而非克隆。回归夹具
`xgui_regression_test.c:9540-9598` 覆盖 Mono 调色表、原地无操作 cacheKey，以及灰度和
Alpha8 的共享与像素保持。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和 `build-crop-painter-off` 的
`XGuiRegression_Test` 目标构建、程序运行及 CTest 均按序通过（各 1/1，程序报告
`XGui regression tests passed`，仅保留既有 XError 诊断和工程警告）；默认配置随后恢复并
完成全量构建。未运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：C99 图像层仍不含 Qt SIMD/GPU 后端和所有浮点像素布局，Mono 调色表和无操作共享语义
仅覆盖当前 XImage 支持的格式集合；其它格式仍沿用现有轻量像素交换实现。

### 10.303 2026-08-30 QImage AlphaDither 转换标志

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3109-3127`
及 `qimage_conversions.cpp:1611-1634,1745-1848`：`createAlphaMask()` 在默认
`flags=0` 时使用 128 阈值，但显式 `OrderedAlphaDither` 必须使用 16x16 Bayer
矩阵，`DiffuseAlphaDither` 必须执行 Floyd-Steinberg 风格的误差传播；深度一图像
仍先转换为 Indexed8，使调色板 Alpha 参与相同抖动路径。

实现范围：`Src/XGui/Graphics/XImage.c:2082-2118` 现在统一调用已有的
`XImage_alphaMaskDither()`，保留 MonoLSB、物理元数据和 RGB32 空图像语义，并覆盖
Alpha8、Indexed8 调色板及深度 32 的 Alpha 读取。回归夹具
`xgui_regression_test.c:9751-9767` 新增 Ordered（Alpha=1 的 Bayer 首格）和
Diffuse（两个 Alpha=127 像素的相邻误差扩散）断言。

验证：默认 `build` 及 JPEG、SVG、XPainter 关闭裁剪配置均已依次完成目标构建、回归程序
和 CTest（各 1/1）；默认配置随后恢复并完成全量构建。保留工程既有兼容性警告，未运行
Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：当前抖动实现覆盖 Qt 三种 AlphaDither 模式的可观察位结果，但不包含桌面 Qt
SIMD 优化和平台特定调色板后端；未知图像格式仍按 XImage 既有拒绝约定处理。

### 10.304 2026-08-30 QPicture 裁剪状态记录与回放

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:152-161`
（`updateClipEnabled`）、`:247-258`（`updateClipRegion`）和 `:260-271`
（`updateClipPath`），补齐 `XPicture` 便携流中的裁剪启用、矩形裁剪及区域裁剪命令。
Qt 的 `QPicture::play()` 高层回放路径见 `qpicture.cpp:435-466,539-558`；回放时状态
命令必须按原始顺序作用于目标 painter，裁剪区域随后再参与绘制。

实现范围：`Src/XGui/Graphics/XPicture.h:34-61,320-348` 新增三个 opcode 及中文契约；
`XPicture.c:260-381` 增加定长/变长负载校验、操作枚举和数量上限，`:878-938` 以小端
固定宽度编码矩形/区域数组并检查整数溢出，`:1510-1556` 在回放中恢复裁剪状态；
`Src/XGui/Graphics/XPainter.c:1792-1825,4751-4813,4885-4931` 在 Picture 后端记录
逻辑坐标和 ClipOperation，并按目标 painter 当前变换重新映射。区域记录采用矩形数组，
避免主机 ABI/指针进入序列化数据；`XPAINTER_CLIP_REGION_ON` 关闭时仍拒绝该 opcode，
支持极小固件裁剪。

回归夹具 `xgui_regression_test.c:5441-5543` 覆盖矩形裁剪启用/禁用后的绘制结果、
边界矩形恢复，以及两个不相邻矩形组成的区域裁剪；主函数调用见 `:16533-16538`。
验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 与 `build-crop-painter-off` 均
完成 `XGuiRegression_Test` 目标构建、程序运行和 CTest（各 1/1，程序报告
`XGui regression tests passed`）；默认全量构建通过，`git diff --check` 通过。工程中
原有兼容性警告和预期 XError 诊断保持不变；未运行本轮专属 ASan/Valgrind，不能宣称无泄漏。

边界：当前便携流不编码 Qt 的 `PdcSetClipPath`/复杂 QPainterPath 裁剪（对应
`qpaintengine_pic.cpp:260-271`），区域只保留整数矩形集合；同一 `XPicture` 作为源和目标
的递归回放仍不在轻量 C99 合约内。版本号保持 `XPICTURE_STREAM_VERSION=1`，新增 opcode
仅对当前实现及同配置构建可读，旧裁剪配置会按特性开关拒绝这些记录。

### 10.305 2026-08-30 QImage 启发式掩码的预乘 RGB 读取

本轮复核 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3153-3250`：
`createHeuristicMask()` 的 `PIX` 宏直接读取深度 32 扫描行中的原始 `QRgb`，仅比较
`0x00ffffff` 的 RGB 位并忽略 Alpha；因此对 `Format_ARGB32_Premultiplied` 不得先用
`pixel()` 反预乘，否则半透明背景可能被错误识别为前景。非 32 位源仍按
`:3160-3163` 转换为 RGB32，非紧致模式的四邻域扩展按 `:3228-3248` 保持边界裁剪。

实现范围：`Src/XGui/Graphics/XImage.c:1959-1974,2178-2238` 的启发式背景投票、边缘队列
和宽松邻域判断统一调用 `XImage_heuristicRgb()`，深度 32 时保留原始扫描值，其他格式
继续使用公开像素值；转换或掩码队列分配失败时现在复位为空图像，匹配 Qt 构造失败语义；
没有引入平台 API 或标准库分配器。回归夹具
`xgui_regression_test.c:9915-9932` 构造两个原始 RGB 相同但 Alpha/公开反预乘值不同的
合法预乘像素，确认中心背景不会被误标为不透明。

验证：默认 `build`、`build-crop-jpeg`、`build-crop-svg` 和
`build-crop-painter-off` 的 `XGuiRegression_Test` 目标均成功构建，程序退出 0 并报告
`XGui regression tests passed`，各自 CTest 均为 1/1；默认配置随后恢复并完成全量构建，
本轮 `git diff --check` 通过。工程既有 XError 诊断、函数指针/const/XEvent 兼容性警告保持
不变；未重新运行 Valgrind、ASan 或 LSan，不能宣称无泄漏；未提交、未推送 Git。

边界：启发式算法仍使用轻量 C99 的队列实现，不包含 Qt 的 SIMD 优化和平台后端；复杂
浮点图像会沿现有 `XImage_pixel()` 转换路径处理，便携层仅保证当前支持格式的可观察
背景连通、Alpha 忽略和物理元数据语义。

### 10.306 2026-08-30 掩码修复后的 sanitizer 复核

本轮在包含 10.305 修复的完整工作树上重新构建并运行
`XGuiRegression_Test`，启用 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` 与
`UBSAN_OPTIONS=halt_on_error=1`。未发现未定义行为、越界或失败断言，程序仍报告
`XGui regression tests passed`；LeakSanitizer 继续报告约 104482 字节、464 个分配存活，
与 10.300 中记录的 Mesa/fontconfig、后备静态内容区域及进程级图像插件处理器生命周期
条目一致。因此本轮仍不能宣称无泄漏，未将第三方或进程级缓存分配误算为已修复。

验证：sanitizer 目标构建和运行退出 0；随后重新构建默认非 sanitizer
`XGuiRegression_Test` 并完成默认全量构建，默认回归程序退出 0、CTest 1/1 通过，
`git diff --check` 通过。未提交、未推送 Git。

### 10.307 2026-08-30 setAlphaChannel 的 Alpha8 格式升级

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage_p.h:279-310` 的
`qt_alphaVersion()`：`Format_Alpha8` 与 Mono、Indexed8 等格式一样没有可绘制的同深度
Alpha 版本，必须升级为 `Format_ARGB32_Premultiplied`。此前
`XImage_alphaVersionForPainting()` 将 `XImageFormat_Alpha8` 原样返回，导致
`XImage_setAlphaChannel()` 在 Alpha8 目标上保留错误的格式。

实现范围：`Src/XGui/Graphics/XImage.c:1734-1777` 移除 Alpha8 的原格式分支，使其走
ARGB32_Premultiplied 默认路径；`xgui_regression_test.c:10060-10066` 新增 1x1 Alpha8
目标夹具，验证目标格式升级以及与 Alpha8 来源合成后的 0x20 Alpha 值。现有
`XImage_setAlphaChannel()` 的同尺寸路径仍按 DestinationIn 饱和乘法，非同尺寸路径继续
使用最近邻采样，后者是无 QPainter 光栅管线时的已记录轻量实现边界。

验证：默认 `build` 与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标均构建成功，程序
均退出 0 并报告 `XGui regression tests passed`，对应 CTest 均为 1/1；随后执行默认
clean-first 目标重建并完成默认全量构建，默认回归及 CTest 仍为 1/1，`git diff --check`
通过。补充运行 `build-asan` 的 ASan/UBSan 目标后无 UB、越界或失败断言，但 LSan 仍
报告约 104482 字节、464 个分配存活（Mesa/fontconfig、静态后备内容和进程级图像处理器
生命周期条目），因此不能宣称无泄漏。编译器仍报告工程既有的函数指针/const/XEvent、
zlib 预处理等警告，未新增 XImage.c 警告。未提交、未推送 Git。

### 10.308 2026-08-30 QImageReader 插件工厂首个能力命中语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:205-224`
（格式插件按注册表顺序检查 `CanRead`，首个命中后调用 `create()` 并立即结束该阶段）及
`:226-254`（插件阶段结束后才进入内置处理器和内容探测）。此前
`XImagePluginRegistry_createReadHandlerEx()` 在首个外部插件 `create()` 返回 `NULL` 时，
自动探测路径仍会继续调用同格式的后续外部插件；即使内置处理器已经创建成功，后续内容
探测循环也可能再次覆盖已选处理器。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:372-443` 现在在首个声明 `CanRead`
的格式插件上无条件停止外部格式遍历；格式工厂失败后按 Qt 顺序尝试内置处理器，并以
`!handler` 门控内容探测，避免第二次替换或泄漏已创建的处理器。显式格式和自动探测两条
路径均保留同一选择规则，既有后缀回退接口仍负责跳过被拒绝的后缀插件。
回归夹具 `xgui_regression_test.c:7267-7346` 注册两个同键 `bmp` 插件，令首个工厂一次性
返回 `NULL`，验证第二个插件不会被调用；同时覆盖注册成功、设备创建和工厂调用次数。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建和程序运行通过，程序报告
`XGui regression tests passed`；默认 CTest 为 1/1，`git diff --check` 通过。构建输出保留
工程既有函数指针/const/XEvent 兼容性警告，未新增本模块警告；未运行本轮专属 ASan/Valgrind，
不能宣称无泄漏。插件对象和内置处理器仍由进程级注册表持有，生命周期释放边界与
10.300/10.306 一致；未提交、未推送 Git。

### 10.309 2026-08-30 QImageReader 关闭自动探测时的首个格式插件

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:240-249`：
`autoDetectImageFormat=false` 时，格式插件查询使用 `keyMap.key(testFormat)`，只检查该
格式键映射到的首个插件；若其 `capabilities()` 不含 `CanRead`，也不会越过同键的后续插件。
此前 `XImagePluginRegistry_createReadHandlerEx()` 在显式格式路径中统一遍历所有插件，可能
跳过首个能力拒绝者并错误选中第二个同键插件。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:374-446` 为关闭自动探测单独实现首个
格式键命中分支；首个外部插件无 `CanRead` 或 `create()` 失败后立即结束外部阶段，再按 Qt
顺序尝试内置处理器。自动探测开启的格式插件阶段继续遍历能力命中项，但首个命中后
`create()` 失败也立即停止。回归夹具 `xgui_regression_test.c:7313-7356` 让两个同键插件
分别拒绝/允许能力，统计能力查询次数，确认关闭自动探测不会调用第二个插件；同时保留
首个工厂返回 `NULL` 的既有夹具。

验证：默认 `build`、`build-crop-jpeg` 与 `build-crop-svg` 的 `XGuiRegression_Test` 目标、
程序运行和 CTest 均通过（各 1/1，程序报告 `XGui regression tests passed`）；默认配置已
恢复并完成全量构建，`git diff --check` 通过。工程既有函数指针/const/XEvent 兼容性警告
及预期 XError 诊断保持不变；未运行本轮专属 ASan/Valgrind，不能宣称无泄漏。固定容量
注册表和静态内置插件生命周期边界不变，未提交、未推送 Git。

### 10.310 2026-08-30 QImageReader 内容探测首个工厂失败语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:258-275`：
按内容探测时，插件按注册顺序检查空格式 `capabilities()`；首个声明 `CanRead` 的插件
调用 `create()` 后立即结束循环，即使工厂返回 `NULL` 也不会继续尝试后续插件。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:449-475` 在内容探测分支中对首个能力
命中插件无条件 `break`，保留成功处理器的正常返回，并阻止失败工厂被后续插件覆盖。
回归夹具 `xgui_regression_test.c:7357-7378` 注册两个同键插件，令首个内容探测工厂失败，
统计能力查询与工厂调用次数，确认第二个插件不会被调用。

验证：默认 `build`、`build-crop-jpeg` 与 `build-crop-svg` 的 `XGuiRegression_Test` 目标和
串行程序运行均成功，程序报告 `XGui regression tests passed`，各自 CTest 均为 1/1；默认配置
随后恢复并完成全量构建。工程既有警告及预期 XError 诊断保持不变。
本轮未运行 ASan/Valgrind，不能宣称无泄漏；工程既有警告和预期 XError 诊断保持不变，未提交、
未推送 Git。

### 10.311 2026-08-30 QImageReader 后缀回退首个外部插件失败语义

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:332-355`：
后缀处理器拒绝文件内容后，内容探测阶段只遍历外部 imageformats 插件，跳过后缀映射选中的
插件；首个声明 `CanRead` 的剩余插件调用 `create()` 后立即结束外部阶段，即使工厂返回
`NULL` 也不得继续尝试后续插件，随后才进入内置处理器内容探测（`:357-421`）。

实现范围：`Src/XGui/Graphics/XImagePluginRegistry.c:490-555` 将
`createReadHandlerContentFallback()` 拆为外部插件和内置处理器两个阶段。外部阶段跳过首个
匹配被拒绝后缀的插件，遇到首个 `CanRead` 命中后无条件停止；内置插件随后独立按空格式
探测并创建处理器。回归夹具 `xgui_regression_test.c:7379-7399` 注册两个同键插件，跳过
首个后缀插件并令第二个工厂失败，确认不会查询或调用更晚的外部插件。

验证：默认 `build`、`build-crop-jpeg` 与 `build-crop-svg` 全量构建均成功，随后各配置的
`XGuiRegression_Test` 串行运行均退出 0 并报告 `XGui regression tests passed`，对应 CTest
均为 1/1；默认产物已恢复。`git diff --check` 通过。工程既有函数指针/const/XEvent 警告
及预期 XError 诊断保持不变。本轮未运行 ASan/Valgrind，不能宣称无泄漏；固定容量注册表、
静态内置插件生命周期和非 Qt 动态目录发现仍是已记录的嵌入式边界。未提交、未推送 Git。

### 10.312 2026-08-30 QIcon 默认主题资源搜索路径

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:180-196`：
`QIconLoader::themeSearchPaths()` 在用户路径为空时懒加载平台图标路径，并始终追加
`:/icons` 资源目录；显式设置空 `QStringList` 仍会在下一次查询恢复该默认项。平台回退
路径的系统提示见同文件 `:62-78,198-210`，由平台主题提供，不应在公共层伪造固定目录。

实现范围：`Src/XGui/Icon/XIcon.c:1404-1419` 的 `XIcon_themeSearchPaths()` 在全局用户列表
为空或未初始化时返回包含 `:/icons` 的新副本；非空用户列表保持原顺序和内容，默认项不会
写回全局列表。`XIcon_setThemeSearchPaths()` 的复制和缓存失效语义不变，调用方仍可通过
空列表恢复懒加载默认路径。回归夹具 `xgui_regression_test.c:436-461` 设置空列表后验证
资源目录存在，再恢复原列表，覆盖 getter 不污染用户状态的契约。

验证：默认 `build` 及 `build-crop-jpeg` 均完成全量构建、`XGuiRegression_Test` 运行和
CTest（各 1/1，程序报告 `XGui regression tests passed`）；默认目标随后恢复并再次通过
回归与 CTest，`git diff --check` 通过。输出中仍有工程既有信号函数指针、const/XEvent
兼容性警告及预期 XError 诊断，本轮未新增 XIcon 警告。未运行 ASan、Valgrind 或 LSan，不能
宣称无泄漏；未提交、未推送 Git。

边界：XGui 当前没有 Qt `QPlatformTheme::IconThemeSearchPaths`/`IconFallbackSearchPaths`
提示的统一公共接口，平台路径仍由调用方显式设置；`:/icons` 资源解析依赖现有 XFile/XPixmap
后端，未引入动态资源系统或主题插件工厂。

### 10.313 2026-08-30 QImage setPixelColor 原生高精度写入

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2784-2864`：
`QImage::setPixelColor()` 先取得 `QColor::rgba64()`，对无 Alpha 格式强制不透明，
对预乘布局调用 `QRgba64::premultiplied()`；RGBA64 直接保存 16 位通道，
RGBX/RGBA 16/32 位浮点格式分别保存原生浮点值，30 位格式通过
`qpixellayout_p.h:162-181` 的 `qConvertRgb64ToRgb30()` 和
`qRepremultiply<14>()` 降位。此前 XGui 统一转成 8 位 ARGB，导致高位分量丢失。

实现范围：`Src/XGui/Graphics/XImage.c:1709-1754,2506-2640` 新增原生分量写入路径，
保留 `XColor` RGB 规格中的 16 位 `m_comp1..3/m_alpha`；RGBA64 直接写入，
RGBA64_Premultiplied 使用 Qt 等价的 16 位预乘公式，RGBX/RGBA 16/32 位浮点格式
写入对应半精度/单精度通道，BGR30/RGB30 与 A2*30 使用 10/2 位降位和通道顺序。
其余低位格式继续复用原有 ARGB32 路径，Mono/MonoLSB/Indexed8 仍按 Qt 拒绝写入。
回归夹具 `xgui_regression_test.c:10355-10395` 验证 RGBA64 原值保留及预乘通道结果。

验证：默认 `build` 的 `XGuiRegression_Test` 构建和运行通过，CTest 1/1；随后
`build-crop-jpeg` 目标构建、运行和 CTest 亦通过，默认配置已恢复并完成全量构建；
`git diff --check` 通过。随后使用 `build-asan` 运行 ASan/UBSan 回归，未发现 UB、越界或
测试失败；LSan 报告约 105898 bytes/468 allocations，来源主要是 Mesa/fontconfig、静态
区域内容和内置图像插件生命周期，因此不能宣称无泄漏。本轮未运行 Valgrind。输出中的既有
XError 诊断保持不变；未提交、未推送 Git。

边界：非 RGB 的 `XColor` 色彩空间转换仍受 XColor 现有 8 位转换模型约束；浮点写入仅覆盖
当前 XImageFormat 枚举，不包含 Qt 私有 SIMD/平台像素布局。30 位通道的
`qRepremultiply<14>()` 已按 Qt 精度实现，但底层 XImage 仍是固定 C99 字节布局。

### 10.314 2026-08-30 QImage 掩码默认调色表与转换失败语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:126-135`、
`:3158-3161` 和 `:3261-3289`：新建 `Format_MonoLSB` 图像时默认调色表为
`color0=black`、`color1=white`；`createHeuristicMask()` 对非 32 位图像先转换为
`RGB32`，转换失败时直接返回空图，不能继续使用原格式生成掩码。

实现范围：`Src/XGui/Graphics/XImage.c:2178-2205` 在非 32 位启发式掩码转换失败时清理并
返回空图；`Src/XGui/Graphics/XImage.c:2300-2314` 为颜色掩码的 `MonoLSB` 输出设置 Qt
默认黑白调色表，同时保留原有按存储深度比较像素、`MaskOutColor` 反转位图和元数据复制。
回归夹具 `xgui_regression_test.c:10080-10095` 额外断言颜色掩码的两项默认调色表。

验证：默认 `build` 的 `XGuiRegression_Test` 构建、运行和 CTest 均通过（1/1）；
`build-crop-jpeg` 目标构建、运行和 CTest 亦通过（1/1），程序报告
`XGui regression tests passed`。`git diff --check` 通过；随后使用 `build-asan` 运行
ASan/UBSan 回归，未发现 UB、越界或测试失败；LSan 报告约 105898 bytes/468 allocations，
来源主要是 Mesa/fontconfig、静态区域内容和内置图像插件生命周期，因此不能宣称无泄漏。
本轮未运行 Valgrind。输出中的既有 XError/编译警告保持不变；未提交、未推送 Git。

边界：当前 XImage 的固定 C99 `MonoLSB` 存储仍不承载 Qt 私有的共享位图优化；资源分配失败
按现有轻量实现返回空图，平台后端和动态插件生命周期不在本次掩码修复范围内。

### 10.315 2026-08-30 QImage fill(QColor) 高精度格式写入

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1847-1960`：
`QImage::fill(const QColor&)` 对 BGR30/RGB30、RGBA64 及 16/32 位浮点格式分别使用
`QRgba64` 或 `getRgbF()` 写入；RGBX 变体强制 Alpha 为不透明，预乘变体在写入时进行
对应精度的预乘。此前 XGui 所有非索引格式统一经过 8 位 ARGB，导致低 8 位颜色分量
丢失。

实现范围：`Src/XGui/Graphics/XImage.c:1555-1629` 在 `XImage_fillColor()` 中保留
`XColor` 的 16 位 RGB 分量，并复用 `XImage_writePixelColor16()` 为 30 位、RGBA64
及浮点格式写入原生通道；Mono/MonoLSB/Indexed8 仍按 Qt 的调色表与索引语义处理，
其余 8 位格式继续沿用 ARGB32 路径。回归夹具 `xgui_regression_test.c:10298-10320`
使用非整字节对齐的 16 位 RGBA64 颜色，断言四个通道完整保留。

验证：默认 `build` 与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标构建和程序运行均
通过，程序报告 `XGui regression tests passed`；默认与裁剪配置的 CTest 均为 1/1。默认
目标随后已恢复，并完成默认全量构建；`git diff --check` 通过。工程既有的函数指针、
const/XEvent 编译警告及预期 XError 诊断保持不变。使用现有 `build-asan` 配置运行
ASan/UBSan/LSan 时未发现 UB、越界或测试失败；LSan 仍报告约 105898 bytes/468 allocations，
来源主要为 Mesa/fontconfig、静态区域和内置插件生命周期分配，不能宣称无泄漏；本轮未
运行 Valgrind。未提交、未推送 Git。

边界：非 RGB 的 `XColor` 仍受现有轻量 8 位色彩空间转换模型限制；浮点和 30 位写入仅
覆盖当前 `XImageFormat` 枚举，不包含 Qt 私有 SIMD/平台像素布局。

### 10.316 2026-08-30 QImage Grayscale16 填充保留灰度精度

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qdrawhelper.cpp:758-779`：
`destStore64Gray16()` 在输入 `QRgba64` 的红、绿、蓝分量相等时直接保存该 16 位
分量；只有非灰色输入才执行色彩空间到 XYZ 的灰度转换。此前 XGui 的
`XImage_fillColor()` 将 `Grayscale16` 落入 8 位 `XImage_luma()`，会把例如 `0x1234`
窄化为 `0x1212`。

实现范围：`Src/XGui/Graphics/XImage.c:2610-2636` 增加 `Grayscale16` 的原生写入分支，
灰度输入逐像素保留完整 16 位值，彩色输入沿用现有轻量 sRGB 299/587/114 近似并直接
扩展到 16 位；`XImage_fillColor()` 高精度格式列表同步包含 `Grayscale16`。回归夹具
`xgui_regression_test.c:10363-10384` 使用 `0x1234` 灰度值验证填充后低位不丢失。

验证：默认 `build` 与 `build-crop-jpeg` 的 `XGuiRegression_Test` 构建、运行及 CTest
均通过（各 1/1，程序报告 `XGui regression tests passed`）；默认产物已恢复。
`git diff --check` 通过。既有回归文件编译警告和预期 XError 诊断保持不变；本轮未运行
ASan/Valgrind，不能宣称无泄漏。未提交、未推送 Git。

边界：非灰色 `Grayscale16` 输入未实现 Qt 私有 `QColorSpacePrivate::transformationToXYZ()`
及 ICC/LUT 处理，仍使用项目现有 sRGB 灰度近似；其它浮点/高位格式行为不受本轮修改影响。

### 10.317 2026-08-30 QImage fill(uint) 的 RGBX 高精度 Alpha 语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1788-1807`：
`QImage::fill(uint)` 对 RGBX64、RGBX16FPx4 和 RGBX32FPx4 均直接使用
`QRgba64::fromArgb32(pixel)` 或对应浮点 `fromArgb32(pixel)`，保留输入 ARGB32
的 Alpha 存储值；只有 `setPixel()` 的独立路径才对 RGBX 格式补 `0xff000000`。
此前 XGui 的 `XImage_fill()` 在这些格式中强制 Alpha 为满值，导致原始存储与 Qt 不符。

实现范围：`Src/XGui/Graphics/XImage.c:1469-1533` 移除 RGBX64、RGBX16FPx4、
RGBX32FPx4 `fill(uint)` 分支中的不透明 Alpha 强制，改为按传入像素扩展到 16 位或
浮点通道；`fill(QColor)` 的 RGBX 不透明规则保持不变。回归夹具
`xgui_regression_test.c:10325-10347` 直接检查 RGBX64 四个原生 16 位通道，覆盖
半透明输入 `0x80112233`。

验证：默认 `build` 与 `build-crop-jpeg` 的回归目标构建、运行及 CTest 均通过（各 1/1）；
默认二进制已恢复并完成全量构建，程序报告 `XGui regression tests passed`。
`git diff --check` 通过；既有编译警告和预期 XError 诊断保持不变。本轮未运行
ASan/Valgrind，不能宣称无泄漏。未提交、未推送 Git。

边界：RGBX 格式的公共 `pixelColor()` 仍按 Qt 语义忽略 X/Alpha 存储通道；本轮只修正
`fill(uint)` 原始内存值，浮点格式未增加额外色彩空间转换或平台 SIMD 优化。

### 10.318 2026-08-30 QImage setPixel 调色板索引越界语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2584-2614`：
`QImage::setPixel()` 对 Mono/MonoLSB 仅接受索引 0 或 1，对 Indexed8 仅接受小于
当前颜色表数量的索引；越界请求只发出诊断并保持原像素，不应按存储宽度截断写入。

实现范围：`Src/XGui/Graphics/XImage.c:3124-3140` 在公开 `XImage_setPixel()` 入口增加
Mono/MonoLSB 与 Indexed8 的范围拒绝检查，匹配 Qt 的无副作用行为。BMP 私有解码路径
`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:400-445` 改为直接写入 Mono/Indexed8
扫描线，使 RLE/调色板像素即使超出 `biClrUsed` 仍保留原始索引；这是 Qt 解码器内部
直接填充数据的语义，避免与公开 setter 的调用方校验冲突。回归夹具
`xgui_regression_test.c:10340-10370` 验证 Indexed8 越界索引及单色索引 2 均保持原像素，
并继续覆盖 RLE8 越行/绝对模式截断夹具。

验证：默认 `build` 的回归目标构建、程序运行及 CTest 均通过（1/1）；`build-crop-jpeg`
目标构建、运行及 CTest 同样通过（1/1）。随后默认目标已恢复并完成全量构建，`git diff
--check` 通过。工程既有编译警告、预期 XError 诊断和 ASan/LSan 已知进程生命周期泄漏
保持不变；本轮未新增 ASan/Valgrind 运行。未提交、未推送 Git。

边界：越界 setter 仍按 Qt 仅拒绝并输出轻量实现的无额外警告路径；BMP 解码器直接写入
内部索引时依赖已分配并对齐的 XImage 扫描线，未引入动态 QImage 私有数据结构。

### 10.319 2026-08-30 QImage pixel/pixelIndex 越界哨兵

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:2448-2460`
和 `:2489-2495`：`QImage::pixelIndex()` 在空图像或坐标越界时返回 `-12345`，
`QImage::pixel()` 在同类边界返回 `12345`，同时保留诊断输出；合法坐标继续按像素格式
读取，不影响调色板索引越界时的 `0` 返回语义。

实现范围：`Src/XGui/Graphics/XImage.c:2419-2433` 将公开 `XImage_pixelIndex()` 的空图、
越界和扫描线失败结果统一为 `-12345`；`XImage_pixel()` 在调用读取前检查
`XImage_valid()`，无效坐标返回 `12345u`。`Src/XGui/Graphics/XImage.h:532-548` 补充
中文返回值契约。回归夹具 `xgui_regression_test.c:10353-10362` 覆盖普通图像的负坐标
及 `NULL` 图像调用，验证两个哨兵值；其余有效索引/颜色夹具保持原样。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest 均通过（1/1，
程序报告 `XGui regression tests passed`）；`build-crop-jpeg` 目标构建、程序运行和 CTest
同样通过（1/1）。随后默认目标已重新链接并恢复到共享 `bin/XGuiRegression_Test`，默认
程序和 CTest 再次通过；`git diff --check` 通过。全量构建中的既有函数指针、const/XEvent
兼容性警告及预期 XError 诊断保持不变。本轮未运行 ASan、UBSan 或 Valgrind，不能宣称
无泄漏；未提交、未推送 Git。

边界：Qt 对 `pixelIndex()`/`pixel()` 的越界返回值属于实现级诊断哨兵，XGui 仍不输出
完整 Qt `qWarning` 文本；内部所有采样调用均在合法坐标下进行，因此不会把哨兵带入
缩放、掩码或绘图运算。

### 10.320 2026-08-30 QImageReader 内容决策状态独立性

本轮复核 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:703-720`：
`QImageReader::setDecideFormatFromContent()` 的实现只赋值
`ignoresFormatAndExtension`，不会改写 `autoDetectImageFormat`；文档中的“禁用自动探测”
描述由内部处理器选择逻辑实现，而不是公开状态 setter 的副作用。

实现范围：`Src/XGui/Graphics/XImageReader.c:1031-1036` 仅更新
`m_decideFromContent`，保留 `m_autoDetectFormat` 原值；`Src/XGui/Graphics/XImageReader.h:156-163`
同步说明两个状态彼此独立。回归夹具 `xgui_regression_test.c:6652-6687` 验证启用和关闭
内容决策前后自动探测状态均保持开启。

验证：默认与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest
均通过（各 1/1）；随后恢复默认目标并完成 `cmake --build build -j2` 全量构建，默认
程序和 CTest 再次通过。既有函数指针/const/XEvent 编译警告和预期 XError 诊断保持
不变；本轮未运行 ASan、UBSan 或 Valgrind，不能宣称无泄漏。未提交、未推送 Git。

边界：该修复只校正公开状态变量的 setter 语义；内容决策模式下的处理器筛选仍依赖
当前轻量注册表，不引入 Qt 的动态 QFactoryLoader 元数据热加载。

### 10.321 2026-08-30 QImageReader 失败读取保留调用方输出

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1199-1218`：`QImageReader::read(QImage*)` 在设置处理器选项后直接调用 `handler->read(image)`，读取器不预先销毁或初始化调用方的 `QImage`。仅当处理器成功分配/替换时输出变化，处理器返回失败且未修改输出时原图保持不变。

实现范围：`Src/XGui/Graphics/XImageReader.c` 的 `XImageReader_read()` 删除读取前的 `XImage_deinit_base()/XImage_init()`，交由处理器或内置编解码器在成功读取时管理输出；失败路径仍设置 `InvalidDataError` 与 `Unable to read image data`。回归夹具 `xgui_regression_test.c` 插件读取测试令处理器尺寸超过其可读上限并返回失败，断言 `XImageReader_read()` 返回 false 且输出尺寸/像素保持。

验证：默认与 `build-crop-jpeg` 的目标构建、回归和 CTest 均通过；默认全量构建通过；`git diff --check` 通过。保留既有编译警告与预期 `XError` 诊断；本轮未运行 ASan/UBSan/Valgrind，不能宣称无泄漏。

边界：处理器自身若在失败前修改了输出，轻量 C API 与 Qt 一样不回滚该副作用；内置编解码器仍按各自实现负责输出图像分配。

### 10.322 2026-08-30 QImageReader read 成功标志直接返回

本轮继续对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1199-1218`：`QImageReader::read(QImage*)` 在 `handler->read(image)` 返回 `true` 后直接沿成功路径返回，不依据输出 `QImage` 是否为空再次判定。处理器若错误地返回 `true` 但未填充图像，Qt 仍报告成功；无效输出由处理器自身契约负责。

实现范围：`Src/XGui/Graphics/XImageReader.c` 删除末尾 `!XImage_isNull(out)` 二次判定，所有源读取成功后直接返回 `true`；失败仍在各分支设置对应错误并返回 `false`。回归夹具扩展 `TestImageHandler`，验证处理器报告成功但保留空输出时读取器返回 `true` 且图像为空。

验证：默认与 `build-crop-jpeg` 的目标构建、回归和 CTest 均通过；默认全量构建通过；`git diff --check` 通过。保留既有编译警告与预期 `XError` 诊断；本轮未运行 ASan/UBSan/Valgrind，不能宣称无泄漏。

边界：该行为依赖处理器遵守 Qt `QImageIOHandler::read()` 契约；内置编解码器成功时总会产生非空图像，空图成功仅由自定义处理器测试覆盖。

### 10.323 2026-08-30 QIcon::fromTheme 备用图标双条件回退

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1414-1422`：
`QIcon::fromTheme(name, fallback)` 先创建主题图标，随后在 `isNull()` 或
`availableSizes().isEmpty()` 任一条件成立时返回备用图标。第二个条件覆盖已登记但
延迟解码失败的主题条目，不能只检查引擎是否为空。

实现范围：`Src/XGui/Icon/XIcon.c:1325-1343` 在存在 fallback 时查询默认
`Normal/Off` 可用尺寸；主题引擎为空或尺寸列表为空均复制 fallback，保留无 fallback
重载原有结果。回归夹具 `xgui_regression_test.c:666-716` 登记损坏 BMP 主题文件，验证
`isNull()/hasThemeIcon()` 仍按 Qt 的“文件已登记、解码延迟”语义保持非空，同时带 fallback
的 `fromTheme` 返回备用 2x2 像素图及其像素值。

验证：默认 `build` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest 均通过（1/1）；
`build-crop-jpeg` 目标构建、程序运行和 CTest 同样通过（1/1）。随后已恢复默认目标并完成
全量构建、程序和 CTest 复核，`git diff --check` 通过。保留工程既有函数指针、const/XEvent
编译警告和预期 XError 诊断；本轮未运行 ASan、UBSan 或 Valgrind，不能宣称无泄漏。未提交、
未推送 Git。

边界：轻量主题实现仍使用固定容量解析和现有格式注册表，不提供 Qt 动态插件扫描；若主题
引擎的尺寸查询因资源损坏而为空，现已与 Qt 一样优先返回调用方 fallback。

### 10.324 2026-08-30 QImage 掩码工厂的默认调色表与转换失败语义

本轮依据 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:110-145`：
`QImageData::create()` 为 `Format_Mono/Format_MonoLSB` 初始化两项默认调色表，
`color0=black`、`color1=white`；`qimage.cpp:3153-3161` 规定
`createHeuristicMask()` 对非 32 位源先转换为 `RGB32`，转换失败时临时图像为空，
结果也必须为空；`qimage.cpp:3257-3290` 规定颜色掩码在深度 32 时直接比较扫描线
存储值，并仅复制物理元数据。

实现范围：`Src/XGui/Graphics/XImage.c:2217-2235` 在非 32 位启发式掩码路径检查
`RGB32` 转换结果，失败时清理调用方输出并返回空图；
`Src/XGui/Graphics/XImage.c:2330-2377` 为 `MonoLSB` 颜色掩码设置黑白默认调色表，
深度 32 按原始存储值比较，`MaskOutColor` 反转整行字节并保留物理元数据。回归夹具
`xgui_regression_test.c:10131-10163` 覆盖颜色掩码调色表、完整 ARGB 匹配和反转位序，
`xgui_regression_test.c:10177-10209` 覆盖预乘存储比较及启发式掩码边缘/孔洞语义。

验证：本轮完成后重新执行默认 `build` 与 `build-crop-jpeg` 的回归目标构建、程序运行
和 CTest，并恢复默认目标后完成全量构建；同时执行 `git diff --check`。既有编译警告、
预期 `XError` 诊断保持不变。本轮未运行 ASan、UBSan 或 Valgrind，不能宣称无泄漏。

边界：轻量 C99 实现不引入 Qt 私有 `QImageData`/动态插件对象；掩码算法仍使用项目
现有软件队列和像素访问接口，未增加平台 SIMD 优化。

### 10.325 2026-08-30 QIcon::addFile 有效零尺寸请求

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:447-469`：
`QPixmapIconEngine::addFile()` 通过 `QSize::isValid()` 判断是否指定尺寸，而
`QSize::isValid()` 对宽高均为非负值返回 true。因此 `QSize(0,0)` 或单个分量为零的
尺寸仍属于显式条目；只有负分量才进入“加入文件中的全部图像”分支。显式零尺寸条目
在取图时可作为空占位，不应被改写成全部帧加载。

实现范围：`Src/XGui/Icon/XIcon.c:251-266` 的惰性文件条目校验改为拒绝负尺寸、保留
零尺寸；`Src/XGui/Icon/XIcon.c:1210-1224` 的 `XIcon_addFile()` 分支改用宽高非负
判断，与 Qt `QSize::isValid()` 一致。`Src/XGui/Icon/XIcon.h:324-357` 更新公共契约，
明确负值代表无效尺寸/全部帧而零值仍是指定请求。回归夹具
`xgui_regression_test.c:6128-6148` 添加 `0x0` 请求并断言 `availableSizes()` 保留
零尺寸显式占位。

验证：默认 `build` 与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标构建、程序运行
和 CTest 均通过；随后默认 `build` 全量构建通过，`git diff --check` 通过。保留工程既有
函数指针、const/XEvent 和信号宏编译警告及预期 `XError` 诊断。本轮未运行 ASan、UBSan
或 Valgrind，不能宣称无泄漏。

边界：轻量图标引擎仍不实现 Qt 的 ICO 专用高质量帧筛选和动态图标引擎插件扫描；负尺寸
的无尺寸路径继续依赖现有 `XImageReader` 多帧能力与裁剪配置策略。

### 10.326 2026-08-30 QPicture::save(QIODevice*) 写入结果语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:279-289`：
`QPicture::save(QIODevice*)` 在绘制未激活且记录状态有效时直接调用
`QIODevice::write()`，不检查写入字节数或错误返回，也不额外调用 `flush()`，随后返回
`true`。设备指针为空仍返回 `false`；保存过程不因记录字节流尚未验证而拒绝。

实现范围：`Src/XGui/Graphics/XPicture.c:1887-1915` 移除保存前便携流校验，并让文件名重载
复用设备重载；对非空记录仅调用一次 `XIODevice_write_1()` 并忽略短写/错误结果，空记录
直接成功，移除原有的全量写入比较和 `XIODevice_flush()`。`Src/XGui/Graphics/XPicture.h:483-490`
补充该 Qt 兼容契约，明确不会刷新设备。回归夹具
`xgui_regression_test.c:118-144` 构造独立 `XIODevice` 虚表并替换写槽模拟短写，
`xgui_regression_test.c:2200-2247` 分别断言无效 `setData` 和有效记录的
`XPicture_save_device()` 均返回 true 且只委托一次写入。

验证：默认与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标构建、程序运行和 CTest
均通过（各 1/1）；默认 `build` 全量构建通过，`git diff --check` 通过。既有信号宏、
const/XEvent 兼容性警告和预期 `XError` 诊断保持不变。现有 `build-asan` 目标构建并运行
通过，未发现越界或未定义行为；LSan 报告约 105898 字节、468 个进程结束时残留，来源包括
既有 Mesa/fontconfig、区域后备存储和图像插件生命周期分配，因此不能宣称无泄漏。

边界：XPicture 仍不包含 Qt 绘制激活状态字段，因此无法在 C99 层复现
`paintingActive()` 的拒绝分支；文件名重载仍使用项目 `XFile` 的打开/关闭语义。

### 10.327 2026-08-30 BMP 未压缩像素行截断的 Qt 兼容读取

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp`：
`read_dib_body()` 在 1 位未压缩分支（373-377）、4/8 位未压缩分支（532-535）以及
16/24/32 位分支（548-552）对 `QIODevice::read()` 短读只退出行循环，仍返回已经
分配的图像；RLE/调色板读取失败仍保持失败语义。Qt 同时在约 77-102 行以
`biWidth * abs(biHeight) > 16384 * 16384` 拒绝超面积图像，宽度 16385、高度 1
本身仍是合法请求。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:356-365` 将非 RLE 像素
校验从完整 `stride * height` 改为仅校验像素偏移不超过输入长度；索引色解码
`XImageCodecBmp.c:380-400` 与 RGB 解码 `:468-481` 均按文件行顺序计算安全行偏移，
每行检查剩余字节，遇到短读即停止而不访问越界。`XImage` 新分配像素由现有工厂
清零，因此未读行确定为黑色/索引 0；这比 Qt 对其未初始化存储的未指定值更适合嵌入式
环境，同时保留成功标志、已读行方向与尺寸语义。偏移落在文件末尾之后仍立即失败，
RLE 流仍由 `bmpRleDecode()` 逐字节校验。

回归夹具位于 `xgui_regression_test.c:8019-8042`，覆盖 24 位底部文件行已读、上部
行截断时仍成功返回及面积上限；`xgui_regression_test.c:8319-8329` 通过
`XImageReader` 验证单行短读返回 1x1 图像且未读像素安全为 0。旧的宽度 16385x1
错误断言已改为超面积 16385x16384，旧的“截断必失败”断言已改为 Qt 的部分图像语义。

验证：默认 `build` 与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标构建、程序运行
和 CTest 各 1/1 通过；随后恢复默认目标并完成默认全量构建与 CTest，`git diff --check`
通过。既有信号宏、const/XEvent 函数指针兼容性警告与预期 `XError` 诊断保持不变。
本轮另执行 `build-asan` 目标与回归，未发现 BMP 越界或未定义行为；LSan 仍报告约
105898 字节、468 个进程结束时残留，来源为既有 Mesa/fontconfig、区域后备存储及
图像插件生命周期分配，不能宣称全局无泄漏。

边界：轻量 C99 解码器仍不复现 Qt `QDataStream` 的设备状态对象和未初始化像素值；
BMP RLE 的部分 `getChar()` 容错仍按现有安全实现返回失败，ICO/动态插件扫描也不在
本模块范围内。

### 10.328 2026-08-30 QIcon::actualSize 引擎请求尺寸透传

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:966-975`
及 `qiconengine.cpp:49-52`：`QIcon::actualSize()` 在存在图标引擎时将请求的
`QSize` 原样交给引擎，默认引擎实现返回原请求，因此零或负分量不能在外层提前改成
空尺寸。仅文件条目匹配路径要求正的查找矩形。

实现范围：`Src/XGui/Icon/XIcon.c:1019-1045` 将引擎路径移到尺寸有效性检查之前，
保留零/负请求及引擎返回值；文件条目路径继续对非正尺寸返回空结果。回归夹具
`xgui_regression_test.c:6051-6073` 通过基础引擎验证 `0x16` 与 `-8x16` 请求均
原样可见，并通过 `XIcon_deinit_base()` 释放引擎所有权。

验证：默认与 `build-crop-jpeg` 的回归目标、程序运行和 CTest 均通过，`git diff --check`
通过。保留既有测试警告与预期 `XError` 诊断；本轮未运行 ASan/UBSan/Valgrind，不能
宣称无泄漏。

边界：轻量 C99 引擎仍不提供 Qt 动态图标插件扫描及完整高质量帧筛选；非引擎图标的
尺寸匹配继续依赖现有文件条目缓存。

### 10.329 2026-08-30 QPainter::drawImage 高 DPI 逻辑尺寸

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:5103-5155`：
点重载绘制带有效 `devicePixelRatio` 的图像时，目标逻辑宽高为物理宽高除以 DPR，
源矩形仍覆盖完整物理图像；图像记录到 `QPicture` 时保留原始调用参数，回放到光栅
设备后再应用该尺寸语义。

实现范围：`Src/XGui/Graphics/XPainter.c:2933-2975` 在绑定 `XImage` 且
`XPAINTER_IMAGE_RECT_ON` 开启时，把 DPR 非 1 的点重载转发为完整源矩形和逻辑目标
尺寸；`XPainter.h:705-716` 补充中文契约。绑定 `XPicture` 的记录路径保持原始图像
与坐标。回归夹具 `xgui_regression_test.c:2895-2915` 使用 2x2、DPR=2 图像断言
默认配置只覆盖一个逻辑像素，并为裁剪关闭配置保留物理绘制断言。

验证：默认回归目标、程序和 CTest 通过，`git diff --check` 通过。`build-crop-image-rect-off`
已编译到 XPainter 对象，但全目标被无关缺失文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c`（`build.make:6449`）阻塞，
未运行该配置的旧二进制；默认与 `build-crop-jpeg` 验证不受影响。保留既有警告；本轮
未运行 ASan/Valgrind，不能宣称无泄漏。

边界：裁剪关闭时仅保留现有物理绘制协议；C99 层不包含 Qt 光栅引擎的完整变换矩阵、
平滑采样和浮点位置舍入策略。

### 10.330 2026-08-30 QImageReader 处理器空格式不回退签名探测

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:636-645`
及 `1458-1467`：处理器已经创建且 `canRead()` 成功时，`format()` 和静态
`imageFormat(QIODevice*)` 直接采用处理器的格式结果；格式为空时返回空字符串，不能
再次用设备签名探测出的其它格式填充。

实现范围：`Src/XGui/Graphics/XImageReader.c:987-1018` 在处理器格式为空时直接返回空，
`XImageReader_imageFormatDevice():1930-1957` 在已创建处理器但空格式时返回新的空字符串。
回归夹具 `xgui_regression_test.c:7172-7230` 注入 `canRead()` 成功但 `format()` 为空的
模拟处理器，分别验证静态和实例查询均保持空格式。

验证：默认与 `build-crop-jpeg` 的回归目标、程序运行和 CTest 通过，默认全量构建通过，
`git diff --check` 通过。保留既有测试警告及预期 `XError` 诊断；本轮未运行
ASan/UBSan/Valgrind，不能宣称无泄漏。

边界：固定容量轻量注册表仍不实现 Qt `QFactoryLoader` 的动态目录扫描、元数据热加载；
无插件时才继续使用内置签名探测路径。

### 10.331 2026-08-30 QImage load/loadFromData 失败失效与显式格式优先

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3775-3779`
及 `3795-3799`：`QImage::load(QIODevice*)` 和 `QImage::loadFromData()` 都先取得
临时读取结果，再赋值给目标对象；读取失败时目标被赋为空图像。静态
`QImage::fromData()` 在 `qimage.cpp:3837-3844` 通过 `QImageReader` 读取，显式格式名
由读取器直接选择，不能把未知格式改作内容探测。

实现范围：`Src/XGui/Graphics/XImage.c:3839-3878` 现在先在临时 `XImage` 中解码，成功
后移动替换目标，失败则释放旧数据并置为空；提供非空格式名且注册表无法识别时立即失败，
仅省略格式名才调用内容探测。`Src/XGui/Graphics/XImageReader.c:1636-1680` 的文件和设备
软件回退改用临时图像，保持 Qt `QImageReader::read(QImage*)` 在失败时不触碰调用方输出的
语义。`Src/XGui/Graphics/XImage.h:729-750` 补充失败失效和初始化契约注释。

回归夹具位于 `xgui_regression_test.c:7898-7946`，覆盖畸形数据失败后旧像素被清空、有效
BMP 在显式未知格式下拒绝且不探测，以及显式 `bmp` 成功替换目标。

验证：默认与 `build-crop-jpeg` 的 `XGuiRegression_Test` 目标均构建通过；默认和裁剪配置的
CTest 均为 1/1 通过，`./bin/XGuiRegression_Test` 返回 0 并报告 `XGui regression tests
passed`；默认与 `build-crop-jpeg` 全量构建均返回 0。构建输出保留既有信号宏、跨类型删除、
const 丢弃和第三方预处理警告；`build-crop-min` 仍被无关缺失文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c`（`build.make:6477`）阻塞；
`git diff --check` 通过。未运行 ASan/Valgrind，不能宣称无泄漏。

边界：`XImage_load_2()` 仍以读取完整文件后交给轻量编解码器为实现，未引入 Qt 的
`QIODevice` 懒加载、扩展名逐个尝试和动态 `QFactoryLoader` 插件扫描。

### 10.332 2026-08-30 QIcon 主题固定条目的声明尺寸

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:849-900`：
`entryForSize()` 以请求矩形较小边选择条目，`QIconLoaderEngine::actualSize()` 对
`Fixed/Threshold` 条目使用 `QIconDirInfo::size * scale` 的目录元数据并限制在请求
较小边以内；只有 `Scalable` 条目直接返回请求矩形，`Fallback` 条目才委托独立图标
重新计算宽高比。解码后的文件像素尺寸不参与固定主题条目的逻辑尺寸计算。

实现范围：`Src/XGui/Icon/XIconThemeEngine.c:92-188` 记录最接近的声明方形目录尺寸，
固定条目按该元数据返回方形逻辑尺寸；非方形回退文件继续保留宽高比路径。回归夹具
`xgui_regression_test.c:1098-1119` 先把声明为 `48x48` 的主题文件写成 `7x5`，再断言
请求 `32x32` 仍返回 `32x32`，随后恢复原始资源。

验证：默认 `build` 与 `build-crop-jpeg` 的全量构建、回归程序和 CTest 均通过（各
1/1）；`build-asan` 的 `XGuiRegression_Test` 构建及 `ASAN_OPTIONS=detect_leaks=1`
运行退出 0，未发现越界或未定义行为。LSan 仍报告约 105898 字节、468 个进程结束时
残留，栈来自既有 Mesa/fontconfig、区域后备存储及图像插件注册缓存，不能宣称全局
无泄漏。`build-crop-min` 仍被无关缺失文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c`（`build.make:6477`）阻塞；
`git diff --check` 通过。

边界：轻量主题引擎仍不扫描 Qt 动态插件目录，也不实现 SVG 渲染和完整设备像素比
缓存；声明目录仅承载当前 C99 主题索引可表达的固定尺寸/缩放信息。

### 10.333 2026-08-30 QPicture load 格式失败时保留已读数据

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:227-250`：
`QPicture::load(QString)` 仅在文件无法打开时赋空对象；打开成功后委托设备重载，
`load(QIODevice*)` 先把 `readAll()` 的完整字节写入内部缓冲，再执行 `checkFormat()`。
因此非空但格式错误的流返回 `false`，同时仍可通过 `size()/data()` 观察；空流或读取
失败才得到空图片。

实现范围：`Src/XGui/Graphics/XPicture.c:1846-1897` 在文件或设备读取成功且长度不超过
`UINT32_MAX` 时先调用 `XPicture_setData()`，再用 `XPicture_isValidStream()` 返回格式
结果；打开/读取失败及空数据走 `XPicture_reset()`。`Src/XGui/Graphics/XPicture.h:446-470`
同步文件与设备重载契约。回归夹具 `xgui_regression_test.c:2362-2398` 验证四字节畸形
流返回失败但保留非空数据，随后验证缺失文件会清空对象。

验证：默认 `build` 与 `build-crop-jpeg` 的目标构建、全量构建、回归程序和 CTest 均
通过（各 1/1）；`build-asan` 目标及 sanitizer 回归退出 0，LSan 保持上述既有
105898 字节/468 分配残留；`git diff --check` 通过。构建输出仍有原有跨类型删除、
信号函数指针、const 丢弃及第三方预处理警告，未宣称零警告。

边界：XPicture 使用 XinYueC 自有便携流格式，并不解析 Qt 二进制 `QPicture` 版本和
设备状态；超出 `UINT32_MAX` 的输入仍拒绝并置空，内存分配失败时的回退由基础容器
实现决定。

### 10.334 2026-08-30 QIconEngine 缩放钩子的设备像素比责任边界

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-249`
及 `qicon.cpp:911-930`：默认 `QIconEngine::virtual_hook(ScaledPixmapHook)` 只将
`pixmap(arg.size * arg.scale, ...)` 写回参数，不在钩子中设置 DPR；`QIcon::pixmap()`
在钩子返回后按实际像素尺寸统一调用 `setDevicePixelRatio()`。因此自定义引擎的 hook
返回值必须保留其自身 DPR，不能提前用请求 scale 覆盖。

实现范围：`Src/XGui/Icon/XIconEngine.c:114-151` 保留物理尺寸换算和溢出/非有限值
检查，移除 hook 内对输出 `XPixmap` 的强制 DPR 写入；`Src/XGui/Icon/XIcon.c:1021-1170`
允许引擎路径透传非正尺寸给 `actualSize()`/`paint()`，无引擎像素回退路径仍拒绝无效
尺寸。回归夹具 `xgui_regression_test.c:6090-6190` 验证基础引擎的
`ScaledPixmapHook` 得到物理尺寸图且 DPR 保持默认值，并覆盖零/负尺寸、NaN/无穷比例
拒绝语义。

验证：默认与 `build-crop-jpeg` 的回归目标、回归程序、CTest 和全量构建均通过；
`build-asan` 目标及 sanitizer 回归无越界/未定义行为，但 LSan 保持既有约 105898
字节、468 个进程结束时残留（Mesa/fontconfig、区域后备存储及图像插件注册缓存），
不能宣称全局无泄漏。`build-crop-min` 仍被无关缺失文件
`Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c`（`build.make:6477`）阻塞；
`git diff --check` 通过。
边界：C99 引擎仍不实现完整 QIcon pixmap DPR 计算、动态插件和平台样式；DPR 的最终
逻辑由 `XIcon.c` 现有 scaledPixmap/actualSize 路径近似承载。

### 10.335 2026-08-30 XPainter 批量矩形在无画笔后端的填充语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3258-3309`、
`qpaintengineex.cpp:700-730` 及 `qpaintengine_raster.cpp:1436-1485`：
`QPainter::drawRects()` 将矩形批量交给引擎，填充由画刷路径独立完成，不要求画笔回调
存在；空矩形列表保持成功空操作。后端未绑定设备时才报告失败。

实现范围：`Src/XGui/Graphics/XPainter.c:2840-2852` 移除对 `m_drawLine` 的强制要求，
仅提供画刷填充的后端也可逐矩形完成 `drawRects()`；`xgui_regression_test.c:3171-3187`
新增仅画刷回调夹具，验证矩形区域写入和无输入时成功返回。

验证：默认与 `build-crop-jpeg` 的目标构建、回归程序、CTest 和全量构建均通过；
`build-asan` 目标及 sanitizer 回归退出 0，未发现越界或未定义行为，LSan 保持上述
既有进程结束残留；`build-crop-min` 仍受同一无关 TinyUSB 缺失文件阻塞；
`git diff --check` 通过。构建输出保留仓库原有函数指针、事件类型、const 丢弃及第三方
预处理警告，未宣称零警告。

边界：`XPainter_drawRects()` 仍按当前便携回调逐项执行，不实现 Qt 光栅引擎内部的批量
路径优化；仅画刷后端的填充颜色及合成规则由现有 `m_fillRect` 回调决定。

### 10.336 2026-08-30 远端 XFont 合并后的无字库文本保护与路径回退

本轮先从远端同名分支抓取并以快进方式合并 `1568c348`；合并前的本地未提交改动
经临时保存后完整恢复，无冲突且未产生新的合并提交。该提交默认关闭内置位图
provider，并把外挂字库目录配置为 `../Library/XFont`。因此从仓库根目录直接执行
回归程序时，外部字库的相对路径与 CTest 在 `build/` 目录执行时不同；同时在所有
provider 被裁剪掉的固件配置下，位图度量可能为零。

实现范围：

- `Src/XData/XFont/XFont.c:590-611` 首次打开外挂字库失败且路径以 `../` 开头时，
  释放当前文件对象后重试去掉前导 `../` 的相对路径；自定义绝对路径、盘符路径和
  其它目录配置不改变。
- `Src/XGui/Graphics/XPainter.c:6177-6184` 在 `XPainter_drawTextRect()` 计算布局
  宽度前检查位图宽度、行高和缩放后的字符宽度；无有效 provider 时按 Qt 绘制无效
  引擎的成功空操作返回，避免 `rect->width / charW` 除零并保持 painter 状态。

Qt 6.8 依据：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:5651-5668`
在 painter 无绘制引擎、文本为空或画笔为 `NoPen` 时直接返回；
`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/text/qfontmetrics.cpp:502-517` 对空文本的
`horizontalAdvance()` 明确定义为 0。XFont 无 provider 的零度量保护沿用该“安全空
操作/零度量”边界，避免将不可用字库伪装成固定宽度。

验证：重新配置后的 `build`、`build-crop-jpeg` 与 `build-crop-min` 目标构建、回归
程序、CTest 和全量构建均通过（各 CTest `1/1`）；根目录直接运行
`./bin/XGuiRegression_Test` 与三个 CTest 工作目录运行均通过。`build-asan` 目标及
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1` 回归退出
0，未发现越界或未定义行为；本轮未启用 LSan，既有 LSan 记录的约 105898 字节/468
分配进程结束残留仍不能宣称全局无泄漏。构建输出仍包含远端提交引入的 XClass 类型
指针和第三方预处理警告，以及既有 XError 诊断；未宣称零警告。

边界：路径回退只解决当前默认 `../Library/XFont` 的两个运行目录；嵌入式文件系统
和自定义 `XFONT_EXTERNAL_FONT_DIR` 仍由配置提供可访问路径。无 provider 时文本不会
绘制字形，但其它文本 API 的零度量结果保持现有实现；不实现 Qt 完整字体引擎、字形
整形或动态字体插件。

### 10.337 2026-08-30 裁剪构建目录重新配置后的验证复核

此前复用的 `build-crop-painter-off` 构建目录仍保留已删除源文件
`Src/XGui/Graphics/XFont8x16.c` 的旧 CMake 依赖，目标构建曾在该文件的编译步骤报
`cc1: fatal error: .../XFont8x16.c: No such file or directory`；这不是当前源码或
便携回调实现错误。为避免把旧生成文件当作当前裁剪能力，本轮执行
`cmake -S . -B build-crop-painter-off` 重新生成构建图。

重新配置后，`build-crop-painter-off` 的 `XGuiRegression_Test` 目标构建与 CTest
均通过（`1/1`），默认 `build`、`build-crop-jpeg`、`build-crop-min` 的既有验证
结果保持不变。构建输出仍含仓库原有 XClass 函数指针、const 丢弃和第三方预处理
警告，不能宣称零警告；本轮只验证越界/未定义行为之外的常规回归，未启用 LSan。

### 10.338 2026-08-30 XImageIOPlugin 基类元数据的空值生命周期

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.h:101-117`：
`QImageIOPlugin` 基类只要求 `capabilities()` 与 `create()` 两个纯虚函数，格式键、
文件过滤器和 MIME 列表属于插件元数据，不存在“基类默认空列表”对象。XGui 为便于
嵌入式注册表增加的三个元数据虚函数，在派生插件未覆盖时应以 `NULL` 表示未提供，
而不应每次查询都分配一个调用方无法释放的空 `XStringList`。

实现范围：`Src/XGui/Graphics/XImageIOPlugin.c:15-20` 的默认 `keys/nameFilters/mimeTypes`
改为返回 `NULL`；`XImageIOPlugin_*_base()` 对无效对象的回退值同步改为 `NULL`。
`Src/XGui/Graphics/XImageIOPlugin.h:98-123` 明确文档化“插件管理；未提供时返回
NULL”。注册表现有 `XImagePluginRegistry.c:161-169`、`:652-733` 已对 NULL 列表按空
集合处理，因此不改变格式发现和 MIME 查询结果。回归夹具
`xgui_regression_test.c:7483-7496` 验证基类对象三个接口均返回 NULL 并可正常销毁，
派生测试插件仍返回自身管理的列表。

验证：默认、`build-crop-jpeg`、`build-crop-min` 与 `build-crop-painter-off` 的回归
目标和 CTest 均通过；`build-asan` 目标及 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1` 回归退出 0，未发现越界或未定义行为。随后已重新构建
默认 `build` 目标恢复非 sanitizer 回归程序。
构建输出仍有既有 XClass 函数指针、const 丢弃及第三方预处理警告，未宣称零警告；
本轮不宣称全局无泄漏，LSan 的历史进程结束残留仍需单独归因。

边界：该修正只影响未覆盖元数据虚函数的 C99 插件基类；真实插件返回的列表仍由插件
拥有并保持原有生命周期。Qt `QFactoryLoader` 的动态目录发现和插件 JSON 元数据仍是
固定容量注册表的明确嵌入式裁剪边界。

### 10.339 2026-08-30 BMP 空像素区的 atEnd 拒绝

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:210-214`
和 `:373-377/532-535/548-552`：`read_dib_body()` 在逐行读取前先检查设备是否已到
末尾；像素偏移恰好等于文件长度时返回失败，而像素区只有部分行字节时仍保留已分配
图像并成功返回。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:359-369` 将像素偏移边界从
`offset > size` 收紧为 `offset >= size`，拒绝无任何像素字节的 BMP，同时保留逐行
短读的零填充结果。回归夹具 `xgui_regression_test.c:8561-8562` 新增完整 DIB 头但
空像素区的拒绝断言；此前的截断 24 位行夹具继续验证部分读取成功。

验证：默认、JPEG/最小/Painter-off 裁剪目标已分别串行重链并通过 CTest；ASan/UBSan
回归退出 0，未发现越界或未定义行为。构建输出中的既有 XClass、const 丢弃和第三方
预处理警告继续单独记录，不宣称零警告或全局无泄漏。

边界：该收紧只针对像素起始位置在文件末尾的输入；Qt 对某些带额外尾部字节但像素区
仍为空的非标准偏移存在历史宽松路径，XGui 仍以 `offset < size` 作为统一安全边界。

### 10.340 2026-08-30 BMP 32 位零位域掩码语义

本轮对照 Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:244-265`
及 `:314-330/540-565`：对 `BI_BITFIELDS`，Qt 无条件按声明的 RGB 掩码提取通道；
掩码全零时三个通道均为 0，透明掩码为 0 则 alpha 保持 255。只有 `BI_RGB` 才使用
默认 `0x00RRGGBB` 掩码。

实现范围：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:510-529` 将 32 位位域分支
从“掩码非零才启用”改为只要 `hasMasks` 就调用 `bmpMaskTo8()`，确保全零掩码输出
不透明黑色。回归夹具 `xgui_regression_test.c` 的 `test_codec_bmp_malformed()` 中
构造 32 位 INFOHEADER `BI_BITFIELDS` 全零掩码并写入非零原始像素，断言结果为
`0xff000000`。

验证：修改后默认、JPEG/最小/Painter-off 裁剪及 ASan/UBSan 回归均通过；构建输出
继续保留既有警告，未宣称零警告或全局无泄漏。

### 10.341 2026-08-31 本轮 XGui Qt 6.8 对齐收束

本轮完成并复核了图像 Handler 注册入口、BMP 边界、图标引擎钩子和
XPainter 绘制路径的最后一组行为修正：

- `XImagePluginRegistry` 的内置处理器懒注册、显式格式/后缀/内容探测和
  外部插件回退保持 Qt `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:276-352`
  的 `createReadHandlerHelper()` 顺序；
- BMP 像素区到达设备末尾时拒绝，部分行短读保留已解码像素；32 位
  `BI_BITFIELDS` 全零 RGB 掩码按 Qt
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:244-265,540-565`
  结果解码为不透明黑色；
- `XIconEngine` scaled hook 仅生成物理像素，DPR 由外层图标 API 修正；
  主题引擎按声明目录尺寸执行 `actualSize()`，并保留主题/回退搜索顺序，
  对应 Qt `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-247`
  和 `qiconloader.cpp:783-976`；
- `XPicture`、`XImageIOPlugin` 和图像文本元数据入口的失败/空值生命周期均
  由回归夹具覆盖。

验证结果：默认 `build` 全量构建退出 0；默认、JPEG 关闭、最小裁剪和
Painter 关闭配置的 `XGuiRegression_Test` 均串行构建并通过 CTest（各 `1/1`）；
`build-asan` 回归在 `ASAN_OPTIONS=detect_leaks=0`、`UBSAN_OPTIONS=halt_on_error=1`
下退出 0；`git diff --check` 通过。构建输出仍有仓库既有的 XClass 函数指针、
const 丢弃和第三方预处理警告，因此不宣称零警告；Valgrind 不可用，且未以
LSan 结果宣称全局无泄漏。

当前明确边界：`XImagePluginRegistry` 采用固定容量静态注册表，不执行 Qt
`QFactoryLoader("imageformats")` 的动态目录扫描和插件 JSON 热加载；主题图标
缓存、平台主题插件及复杂 `QIconEngine` 序列化仍为嵌入式裁剪项。其余
QImage 色彩空间/文本元数据已覆盖当前轻量 C API，完整 ICC、浮点色彩管理和
桌面 Qt 私有元数据格式不在本库范围。

### 10.342 2026-08-31 FocusReason 枚举补齐 PopupFocusReason

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.h:1340-1351` 的 `Qt::FocusReason` 顺序包含 `PopupFocusReason`，其数值为 4，后续为 `ShortcutFocusReason=5`、`MenuBarFocusReason=6`、`OtherFocusReason=7`、`NoFocusReason=8`。
- **实现范围**：在 `Src/XGui/Window/XWindowEvent.h` 以及 `XWINDOWEVENT_ON=0` 时的 `Src/XGui/Widget/XWidget.h` 回退枚举中加入 `XFocusReason_Popup`，同步补充中文枚举说明；`xgui_regression_test.c` 增加 0~8 数值顺序断言。
- **验证结果**：默认、JPEG 关闭、最小裁剪和 Painter 关闭配置均重编译 `XGuiRegression_Test`，对应 CTest 各 `1/1` 通过；默认配置直接回归亦通过。构建输出仍保留仓库既有的 XClass 函数指针兼容等警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：本修正只补齐枚举声明和值；XLabel 对 `PopupFocusReason` 的失焦选区保留规则已在 10.343 对齐，完整 `QTextControl` 选区模型仍不在嵌入式范围。

### 10.343 2026-08-31 XLabel focusOut 选区保留规则

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:885-897` 在 `QLabel::focusOutEvent()` 中仅对非 `ActiveWindowFocusReason`、非 `PopupFocusReason` 清除 `QTextControl` 选区，随后调用 `QFrame::focusOutEvent()`。
- **实现范围**：`Src/XGui/Widget/XLabel.c:1760-1788` 的 `VXLabel_focusOutEvent()` 读取 `XFocusEvent` 原因；普通失焦清除程序化选择并刷新控件，活动窗口和弹出窗口切换保留选区，最后始终向 `XFrame` 父类分发。`xgui_regression_test.c` 新增普通失焦清除及 Popup 保留夹具（`XWINDOWEVENT_ON` 开启时）。
- **验证结果**：默认配置完整构建退出码为 0，默认、JPEG 关闭、最小裁剪和 Painter 关闭配置均重编译 `XGuiRegression_Test`，对应 CTest 各 `1/1` 通过，默认配置直接回归亦通过。构建输出仍保留仓库既有的 XClass 函数指针兼容等警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：无 `XWINDOWEVENT_ON` 负载时无法读取焦点原因，按 `Other` 处理并清除选区；完整 `QTextControl` 的光标、复杂焦点原因和链接导航仍不在嵌入式范围。

### 10.344 2026-08-31 XPushButton autoExclusive 兄弟互斥

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:150-208` 按钮查询同一父控件下的自动互斥按钮并在新按钮选中时通知旧按钮取消；`:584-624` 禁止独占组唯一已选中按钮被主动取消，`:759-763` 保存 `autoExclusive` 标志。
- **实现范围**：`Src/XGui/Widget/XPushButton.c` 复用 `XObject_children()` 遍历同一父控件的 XPushButton 兄弟；选中当前按钮时清除其它可选中且启用自动互斥的兄弟，取消选中时若没有其它已选中兄弟则保持原状态。新增 `xgui_regression_test.c` 自动互斥组夹具，覆盖首个选中、切换清除和唯一选中不可取消。
- **验证结果**：默认配置目标构建、直接回归和 CTest 均通过；JPEG 关闭、最小裁剪和 Painter 关闭配置在本项改动后均串行重链并通过 CTest（各 `1/1`）。构建输出继续保留仓库既有 XClass 函数指针兼容、const 丢弃及第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：仅覆盖同一父控件的 XPushButton 自动互斥组；Qt `QButtonGroup` 显式登记、跨父控件互斥和快捷键仍为嵌入式裁剪项。

### 10.345 2026-08-31 XPushButton autoRepeat 定时器时序

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:604-624`
  在 `setDown()` 时按 `autoRepeatDelay` 启动/停止重复定时器；`:659-685` 的
  `setAutoRepeat()` 在按下状态同步启动或停止；`:1099-1117` 的 `timerEvent()` 每次
  超时先切换为 `autoRepeatInterval`，再按 `released() -> clicked() -> pressed()`
  顺序发射，并保持 `down=true`。
- **实现范围**：`Src/XGui/Widget/XPushButton.h:101` 增加定时器 ID 字段；
  `Src/XGui/Widget/XPushButton.c:62-124` 统一处理定时器启动/停止和重复点击，
  `setDown()`、`setAutoRepeat()`、鼠标释放、失焦、禁用、移动及析构路径均清理活动
  定时器；新增 `EXObject_TimerEvent` 虚函数重载，按 Qt 时序重启间隔定时器并发射
  信号。由于当前 `XAbstractEventDispatcher` 拒绝零间隔，调用方设置的零/负值仍
  原样保存，真正注册时钳制为 1ms。
- **验证结果**：`xgui_regression_test.c:17225-17251` 增加定时器 ID、直接定时器
  事件和信号顺序夹具；默认目标构建、直接回归与 CTest 均通过，保留既有
  `XFont_deinit_base`、XClass 函数指针及第三方预处理警告，未宣称零警告；本轮未运行
  Valgrind/LSan，不宣称全局无泄漏。
- **边界**：事件调度器当前无 Qt `QBasicTimer` 的原生零毫秒定时器能力，非正间隔按
  1ms 近似；系统快捷键自动重复、QButtonGroup 登记以及 `QAbstractButton` 的
  `QPointer` 删除保护仍未接入。

### 10.346 2026-08-31 XPushButton focusOut PopupFocusReason 保持按下

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:1125-1138`
  的 `QAbstractButton::focusOutEvent()` 仅在焦点原因不是 `PopupFocusReason` 时清除
  `down` 状态、停止自动重复并发射 `released()`；弹出窗口切换时保持按下状态。
- **实现范围**：`Src/XGui/Widget/XPushButton.c:1056-1077` 读取
  `XFocusEvent` 原因，在 `XFocusReason_Popup` 下保留 `m_down` 和重复定时器，其他
  原因执行停止定时器、清除按下状态、重绘及 `released` 信号，然后继续向
  `XWidget` 父类分发。`xgui_regression_test.c:17290-17315` 增加 Popup 保留和普通
  失焦释放的事件夹具。
- **验证结果**：默认配置全量构建、直接回归及 CTest 通过；`build-crop-min` 和
  `build-crop-painter-off` 配置串行重链并通过 CTest。构建输出仍有仓库既有的
  XClass 函数指针兼容、const 丢弃及第三方预处理警告，未宣称零警告；本轮未运行
  Valgrind/LSan，不宣称全局无泄漏。
- **边界**：`XWINDOWEVENT_ON=0` 时事件不携带焦点原因，按 `Other` 处理并释放；
  系统快捷键自动重复、`QButtonGroup` 登记和 Qt `QPointer` 删除保护仍为嵌入式
  裁剪项。

### 10.347 2026-08-31 XPushButton Escape 取消按下状态

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:1007-1080`
  的 `QAbstractButton::keyPressEvent()` 在按下状态收到取消键（默认
  `QKeySequence::Cancel`，对应 `Escape`）时调用 `setDown(false)`、重绘并发射
  `released()`，不调用 `click()`，因此不会发射 `clicked()` 或改变选中状态。
- **实现范围**：`Src/XGui/Widget/XPushButton.c:1015-1027` 增加
  `XKey_Escape` 分支；按下时经 `pushbutton_setDownInternal(false)` 停止重复定时器、
  重绘并发射 released，然后接受事件；未按下时继续交给父类。`xgui_regression_test.c`
  在键盘夹具中设置按下状态并验证 Escape 事件已接受、down 清除、released 增加且
  clicked 保持不变。
- **验证结果**：默认配置全量构建、直接回归和 CTest 通过；随后将串行验证最小裁剪及
  Painter 关闭配置。构建输出仍有仓库既有 XClass 函数指针兼容、const 丢弃和第三方
  预处理警告，未宣称零警告；本轮不运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：仅映射 `XKey_Escape` 为 Qt 取消键，不实现平台主题自定义
  `ButtonPressKeys`/快捷键序列；按键事件不可见或按钮未按下时保持父类分发，
  `QPointer` 删除保护仍为嵌入式裁剪项。

### 10.348 2026-08-31 XPushButton animateClick 100ms 定时点击

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qabstractbutton.cpp:799-812`
  的 `QAbstractButton::animateClick()` 在按钮启用时立即置 `down=true`、重绘并仅在
  动画定时器未活动时发射一次 `pressed()`，随后以 100ms 定时器安排释放；
  `:1101-1119` 的 `timerEvent()` 停止动画定时器并调用私有 `click()`；
  `:345-375` 的 `QAbstractButtonPrivate::click()` 清除 `down`、执行
  `nextCheckState()`、发射 `released()` 与 `clicked()`，不重复发射 `pressed()`。
- **实现范围**：`Src/XGui/Widget/XPushButton.h:103` 增加
  `m_animateTimer`；`Src/XGui/Widget/XPushButton.c` 增加动画定时器启动/停止辅助函数，
  `XPushButton_animateClick()` 立即按下并按 Qt 规则抑制重复 `pressed()`，
  `VXPushButton_timerEvent()` 增加动画定时器分支，定时到期后清除按下状态、切换可选
  状态并发射 `released()/clicked()`；复制、移动和析构路径清理两个定时器，手动
  `setDown(false)` 仅按 Qt 规则停止重复定时器。回归夹具覆盖首次按下、重复调用重置
  及定时到期信号顺序。
- **验证结果**：默认配置目标构建、直接回归与 CTest 通过；随后串行重链最小裁剪和
  Painter 关闭配置并通过各自 CTest，最后恢复默认回归二进制再次通过；
  `git diff --check` 通过。构建输出仍包含仓库既有 XClass 函数指针、const 丢弃和
  第三方预处理警告，未宣称零警告；未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：固定使用 Qt 的 100ms 释放延迟；焦点策略、快捷键触发和
  `QPointer` 删除保护仍由嵌入式现有实现决定。若底层事件调度器不可用，定时器 ID
  为无效值，但立即按下及 `pressed()` 语义仍保留；`setDown(false)` 与 Qt 一样只停止
  重复定时器，动画定时器仍可在后续事件中完成点击。

### 10.349 2026-08-31 QImageIOHandler const setFormat 重载

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.h:31-34`
  同时声明非常量和常量 `setFormat(const QByteArray&)`；
  `qimageiohandler.cpp:322-352` 两个重载都直接写入私有 `format` 缓存。
  内置 BMP/PNG 处理器的 `canRead() const`（`qbmphandler.cpp:704-714`、
  `qpnghandler.cpp:1042-1052`）在内容确认后回写规范格式名。
- **实现范围**：`Src/XGui/Graphics/XImageIOHandler.h:180-190` 新增
  `XImageIOHandler_setFormat_const()`，实现层通过可变私有数据替换旧格式并深拷贝
  新值；非常量 setter 复用该路径避免语义分叉。`Src/XGui/Graphics/XImageBuiltinPlugin.c`
  的 `VXImageBuiltinHandler_canRead()` 在魔数与显式格式匹配后调用常量 setter，
  将 BMP/PNG/JPEG/GIF/SVG 的规范小写名称暴露给读取器的 `format()` 查询。
  `xgui_regression_test.c` 的图像处理器夹具验证常量上下文可更新格式状态。
- **验证结果**：默认 `XGuiRegression_Test` 目标、直接回归和 CTest 均通过；默认全量
  构建退出 0。构建输出仍有仓库已有的 `XFont_deinit_base` 指针类型、XSignal 函数指针
  及 const 丢弃警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：C99 以 `_const` 后缀表达 Qt 的 C++ 常量重载；动态
  `QFactoryLoader` 插件发现、增量 `CanReadIncremental` 读取和完整私有处理器状态机
  仍按 10.341 的嵌入式边界处理。

### 10.350 QImageReader::size 处理器失败路径

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:846-852`
  的 `QImageReader::size()` 仅在 `supportsOption(Size)` 成功时读取处理器
  选项；`:497-548` 的 `QImageReaderPrivate::initHandler()` 在处理器创建失败
  时返回 false，`:1432-1436` 的 `supportsOption()` 随后返回 false，不执行
  独立文件头探测。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1119-1128` 在
  `XIMAGEIOPLUGIN_ON=1` 时，处理器创建失败直接保持 `(0,0)`；仅在插件裁剪
  关闭时保留项目 `XImageCodec` 的裸头尺寸探测，以支持无插件嵌入式构建。
  `xgui_regression_test.c:7190-7215` 新增关闭自动探测且未设置格式的 BMP
  夹具，验证默认插件配置下尺寸保持无效。
- **验证结果**：默认配置、最小裁剪和 Painter 关闭配置的目标构建与回归均通过；
  CTest 各为 1/1。保留仓库既有 XClass 函数指针、const 丢弃及第三方预处理警告，
  未宣称零警告；未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：插件裁剪关闭时的裸头探测是 XinYueC 为无插件构建保留的扩展，
  与 Qt 在插件启用配置下的处理器失败语义不同。

### 10.351 QImageReader/QImageWriter 处理器失败不回退直接编解码

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:497-548`
  的 `QImageReaderPrivate::initHandler()` 在没有可用处理器时直接返回 false，
  `:1160-1218` 的 `QImageReader::read()` 随即失败；`QImageWriter` 同样在
  `qimagewriter.cpp:252-281` 的 `canWriteHelper()` 无法创建处理器时返回 false，
  `:651-687` 的 `write()` 不会改走 `QImage::save()`。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1631-1697` 在
  `XIMAGEIOPLUGIN_ON=1` 时，`ensureHandler()` 失败后直接保留已有错误并返回，
  屏蔽文件/设备的 `XImage_load*()` 回退；`XIMAGEIOPLUGIN_ON=0` 仍保留便携
  编解码器路径以支持无插件嵌入式裁剪。写入器的直接编码分支已由
  `canWrite()` 的插件处理器前置校验限制在插件裁剪配置。
  `Src/XGui/Graphics/XImagePluginRegistry.c:434-485` 同时修正内置处理器严格
  格式回退在成功创建后被末尾清零的路径，确保首个外部插件 `create()` 失败时仍
  返回 Qt 风格的内置 BMP 处理器。`xgui_regression_test.c:test_image_reader_decide_format_state()`
  新增关闭自动探测的未知显式格式读取合法 BMP 失败夹具；插件集成夹具继续验证
  外部工厂失败后的内置处理器回退。
- **验证结果**：默认配置目标构建、直接回归和 CTest 通过；最小裁剪及 Painter
  关闭配置也串行构建并通过 CTest，随后恢复默认二进制再次验证。构建仍有仓库
  既有 XClass 函数指针、const 丢弃和第三方预处理警告，未宣称零警告；本轮未运行
  Valgrind/LSan，不宣称全局无泄漏。
- **边界**：无插件裁剪下的直接 codec 回退是嵌入式扩展；动态 Qt
  `QFactoryLoader` 插件元数据、增量读取及平台专属处理器仍由现有注册表近似实现。

### 10.352 QImagePluginRegistry 内置处理器成功返回保持

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:226-345`
  在外部插件格式命中或内容探测失败后继续尝试内置处理器；只要内置
  `QImageIOPlugin::create()` 成功，读取器必须保留该处理器并进入后续
  `canRead()/read()`，不能在清理标签处把结果丢弃。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:434-485` 删除成功创建
  内置处理器后无条件 `handler = NULL` 的错误语句，使显式 BMP 且自动探测关闭时，
  外部同键插件工厂返回空仍能稳定回退到内置处理器。该修正同时恢复
  `QImageReader::size()/read()` 的处理器路径，避免依赖已屏蔽的直接 codec 回退。
  `xgui_regression_test.c:7910-7935` 的严格显式 BMP 夹具覆盖该回退，测试确认
  读取像素与源图一致。
- **验证结果**：默认 `build` 的回归目标、直接运行和 CTest 均通过；最小裁剪与
  Painter 关闭配置随后串行重链并通过 CTest，最后恢复默认目标并再次运行通过。
  构建仍有仓库既有 XClass 函数指针、const 丢弃及第三方预处理警告，未宣称零警告；
  本轮未运行 Valgrind/LSan，不宣称全局无泄漏。额外以
  `-DXIMAGEIOPLUGIN_ON=0` 进行的嵌入式探索构建可成功编译，但现有测试中有三项
  依赖插件发现的历史夹具（图标主题 BMP 写入、V4 头图像格式查询、长 SVG 处理器
  格式名）失败；这些是裁剪配置的既有测试前提，未将该配置宣称为通过。
- **边界**：内置插件仍由静态格式表驱动，未实现 Qt 动态 `QFactoryLoader` 的
  元数据优先级、运行时目录扫描与插件卸载通知；这些属于现有嵌入式注册表边界。

### 10.353 2026-08-31 QPicture 平铺像素图命令序列化与回放

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:406-421`
  的 `QPicturePaintEngine::drawTiledPixmap()` 使用独立的
  `PdcDrawTiledPixmap` 命令，按顺序保存目标矩形、像素图（或内存列表索引）和
  源偏移；该命令不会在录制阶段展开成多个普通图像绘制。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:34-62,420-430` 新增
  `XPictureOpcode_DrawTiledPixmap` 及公开录制接口；`XPicture.c:260-405` 扩展流
  上限、图像负载和正整数目标尺寸校验，`:1305-1373` 将源图像元数据、像素、目标
  矩形与偏移编码为一个便携命令，`:1913-1973` 在回放时重建 `XImage/XPixmap` 并
  调用现有 `XPainter_drawTiledPixmap()`；裁剪关闭 `XPAINTER_PIXMAP_ON` 或
  `XPAINTER_TILED_PIXMAP_ON` 时拒绝该 opcode。`XPainter.c:3172-3210` 在 Picture
  设备录制时保留单条命令，光栅设备继续使用现有负偏移取模和目标区域分块路径。
- **回归与验证**：`xgui_regression_test.c:5433-5523` 增加 2x1 红绿源图、逻辑偏移
  `(1,0)` 和 4x2 目标矩形的文件往返及像素夹具，确认录制保持单条语义且回放图案
  正确。默认构建目标、直接回归、CTest 及最小/Painter 关闭裁剪配置均串行通过；
  随后重新构建默认目标恢复默认回归二进制，`git diff --check` 通过。构建输出仍有
  仓库既有 XClass 函数指针兼容、const 丢弃和第三方预处理警告，未宣称零警告；本轮
  未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：XinYueC 便携流只保存展开后的像素数据，不复用 Qt `QPicture` 的内存
  `pixmap_list` 索引，也不记录浮点 `QRectF/QPointF` 的小数部分；当前 C99 API 采用
  整数 `XRect/XPoint`。绘制器的世界变换、DPR 和裁剪状态仍由既有状态 opcode
  单独记录；平台专属像素图引擎和 Qt 动态插件发现不在本子功能范围内。

### 10.354 2026-08-31 QPicture 矩形像素图命令序列化与回放

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:387-404`
  的 `QPicturePaintEngine::drawPixmap()` 使用独立的 `PdcDrawPixmap` 命令，按顺序
  保存目标矩形、像素图和源矩形；`qpicture.cpp:634-657` 回放该命令时仍调用
  `QPainter::drawPixmap(target, pixmap, source)`，不会改写成 `PdcDrawImage`。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:34-63,431-447` 新增
  `XPictureOpcode_DrawPixmap` 与 `XPicture_recordDrawPixmap()`；`XPicture.c:39-40,276-418`
  扩展 opcode 上限、载荷长度验证及自包含图像编码，额外保存目标宽高和源矩形，
  并在 pixmap/矩形功能裁剪关闭时拒绝该命令。`XPainter.c:3099-3130,3184-3200`
  在 Picture 设备中分别为点位置重载和矩形重载保留单条专用命令；光栅设备仍
  复用既有 `drawImageRect` 采样路径。回放分支重建 `XImage/XPixmap` 后调用
  `XPainter_drawPixmapRect()`，保留 Qt 的源裁剪与目标缩放语义。
- **回归与验证**：`xgui_regression_test.c:5578-5645` 增加 2x2 四色像素图、源列
  裁剪到 2x2 目标矩形的记录/回放夹具，验证流有效、源颜色复制、目标缩放和边界
  像素保持不变。默认 `build` 目标、直接回归、CTest，以及最小和 Painter 关闭
  裁剪配置均串行通过；最后恢复默认二进制并再次运行，`git diff --check` 通过。
  构建输出继续包含仓库既有 XClass 函数指针兼容、const 丢弃和第三方预处理警告，
  未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：便携流保存展开后的像素数据，不复用 Qt `pixmap_list` 索引，也不记录
  `QRectF` 小数部分；当前 C99 接口使用整数 `XRect`。像素图设备像素比只影响
  点位置重载生成的逻辑目标尺寸；矩形重载按调用方目标矩形原样记录。平台专属
  pixmap 引擎和 Qt 动态插件发现不在本子功能范围内。

### 10.355 2026-08-31 QIcon 清除用户主题时恢复搜索路径

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:174-186`
  的 `QIconLoader::setThemeName()` 在非空用户主题被清除时调用
  `setThemeSearchPath(systemIconSearchPaths())`，不继续沿用用户自定义路径；
  `:188-196` 的 `themeSearchPaths()` 随后懒加载平台路径并追加 `:/icons`。
- **实现范围**：`Src/XGui/Icon/XIcon.c:1508-1538` 记录清除前是否存在非空用户主题；
  当 `XIcon_setThemeName[_2]()` 将其置空时清空显式主题路径，让公共层 getter 回到
  便携默认 `:/icons`。设置新主题、重复清除或 fallback 主题不触碰主题路径。
  `xgui_regression_test.c:test_icon_default_theme_search_path()` 新增显式路径、用户主题
  设置再清除夹具，确认默认资源路径恢复且旧路径不残留。
- **验证结果**：修改后将串行重编译默认回归目标、运行程序和 CTest；随后验证最小
  裁剪与 Painter 关闭配置并恢复默认二进制。构建中的既有 XClass 函数指针、const
  丢弃和第三方预处理警告继续保留，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称
  全局无泄漏。
- **边界**：嵌入式公共层无法查询平台 `systemIconSearchPaths()`，清除后仅恢复
  `:/icons`；平台主题目录、动态主题插件和桌面通知仍由 Drive/调用方提供。

### 10.356 2026-08-31 图像处理器内容回退与文件名推导生命周期

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:318-337`
  在后缀处理器无法读取内容后，按内容尝试其它插件并恢复非顺序设备位置；
  `qimagewriter.cpp:101-143,147-198` 规定无显式格式的文件设备先由后缀得到
  `testFormat`，再按内置/插件顺序创建处理器；`:622-637` 的 `canWrite()` 失败时
  删除检查阶段新建且原先不存在的文件；`:651-687` 的 `write()` 在已选处理器
  成功后刷新文件设备并直接返回处理结果。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:489-553` 的内容回退在
  外部插件成功返回时只释放一次规范化格式对象，失败时保留“首个能力命中即停止”
  规则，并在随后按内置处理器顺序回退；此前该成功分支存在重复释放风险。
  `xgui_regression_test.c:6730-6741` 新增无格式文件名推导后实际写入夹具，
  `:8192-8212` 新增内容回退成功命中夹具，覆盖处理器创建、文件存在性和清理路径。
- **验证结果**：默认回归目标构建、直接运行和 CTest 通过；随后最小裁剪及
  Painter 关闭配置的目标构建、直接回归和 CTest 均通过，并恢复默认构建产物。
  `git diff --check` 通过。构建输出继续包含仓库既有 XClass 函数指针、const
  丢弃和第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局
  无泄漏。
- **边界**：注册表仍为固定容量静态数组，不实现 Qt `QFactoryLoader` 的动态目录
  扫描、插件 JSON 元数据热加载和卸载通知；无插件裁剪继续使用便携直接 codec，
  与 Qt 插件启用路径的处理器失败语义保持配置隔离。

### 10.357 2026-08-31 QPicture 字体状态命令序列化与回放

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:206-216`
  的 `QPicturePaintEngine::updateFont()` 在字体状态变化时写入独立的
  `PdcSetFont` 命令；`qpicture.cpp:726-729` 回放该命令时调用
  `QPainter::setFont()`，因此字体状态不能只保存在绘制器内存而遗漏在 Picture 流中。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:64-65,247-255` 新增
  `XPictureOpcode_SetFont` 和 `XPicture_recordSetFont()`；`XPicture.c:41-42,279-323`
  将其纳入 opcode 上限并校验载荷为 `[u32 UTF-8 长度][i32 像素字号][UTF-8 快照]`，
  限制快照不超过 4096 字节、长度精确匹配且禁止内嵌 NUL。`:812-851` 复用
  `XFont_toString()/XString_toUtf8()` 记录家族、点字号、字重、样式和像素字号，
  NULL 字体写入默认状态标记；`:1639-1671` 回放时通过 `XFont_fromString()` 恢复快照，
  再应用像素字号并直接移动到绘制器状态，避免在 Picture 回放期间递归追加命令。
  `Src/XGui/Graphics/XPainter.c:3326-3340` 在 Picture 后端的 `XPainter_setFont()`
  更新本地状态后追加该命令。`xgui_regression_test.c:5602-5652` 覆盖流有效性、
  opcode、家族/点字号/字重/样式/像素字号回放一致性。
- **验证结果**：默认 `build`、最小裁剪和 Painter 关闭配置的回归目标、直接运行与
  CTest 均通过（各 1/1）；最后恢复默认构建产物并再次通过直接回归和 CTest。
  运行时仅有仓库既有的空参数、无效瞬态父窗口、WindowActive 忽略和空虚表诊断。
  `git diff --check` 通过。构建输出仍含仓库已有 XClass 函数指针兼容、const 丢弃及
  第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：便携快照只覆盖当前 `XFont` 字符串和点阵后端所需像素字号，不实现 Qt
  `QFont` 的字体数据库匹配、hinting、variable-font 特性或 `QTextItem`/复杂脚本排版；
  文字字形仍由现有 `XFont` 点阵绘制路径处理。字体快照异常时 Picture 校验或回放失败，
  不回退到未记录的当前字体。

### 10.358 2026-08-31 QPicture 单点命令序列化与回放

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpaintengine_pic.cpp:448-455`
  的 Picture 回放对 `PdcDrawPoint` 保持独立分支，按一个点调用
  `QPainter::drawPoint()`；`qpicture_p.h:45-49` 将该绘图命令列为独立保留 opcode，
  与 `PdcDrawLine` 的双点载荷不同。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:65-66,236-243` 新增
  `XPictureOpcode_DrawPoint` 和 `XPicture_recordDrawPoint()`；`XPicture.c:279-284`
  将 opcode 上限和固定 8 字节坐标载荷纳入流校验，`:799-815` 写入坐标并更新单像素
  边界，`:1882-1887` 回放时调用 `XPainter_drawPoint()`。`Src/XGui/Graphics/XPainter.c:2697-2707`
  在 Picture 后端直接追加专用点命令，回放阶段仍走现有画笔、设备回调和样式逻辑，
  其它后端继续沿用零长度直线的像素实现。`xgui_regression_test.c:5654-5707`
  验证 `SetPen` 后的命令字节为 `DrawPoint`、流校验成功及目标像素颜色一致。
- **验证结果**：默认 `build` 的回归目标构建、直接运行和 CTest 均通过；最小裁剪与
  Painter 关闭配置此前已完成全量构建，本项目标构建与回归边界保持通过；最终恢复
  默认目标并再次运行回归与 CTest 通过。运行时仅出现仓库既有 XClass 空虚表、参数
  和窗口状态诊断。`git diff --check` 通过。构建输出继续有既有 XClass 函数指针、
  const 丢弃及第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称
  全局无泄漏。
- **边界**：便携流保留整数坐标和当前画笔状态，不编码 Qt `QPointF` 浮点坐标、DPR
  专用点大小或 `QPicture` 旧版本格式兼容；复杂画笔端点栅格化仍由现有 C99 点阵
  后端近似。无有效绘制器或损坏载荷时回放失败，不静默改写为其它绘图命令。

### 10.359 2026-08-31 QIcon 引擎尺寸与非正请求透传

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:966-979`
  的 `QIcon::actualSize()` 在普通 DPI 路径直接把请求交给引擎，未在公共层过滤
  零或负尺寸；`:1017-1040` 的 `QIcon::paint()` 同样使用引擎返回尺寸计算对齐后
  直接调用 `paint()`。文件条目路径仍由具体图像加载逻辑决定是否能产生有效像素。
- **实现范围**：`Src/XGui/Icon/XIcon.c:1019-1066,1134-1185` 将尺寸正值检查移到
  无引擎的便携文件条目分支；引擎分支保留原始请求和返回尺寸，即使请求含非正分量
  也继续执行 `actualSize()/paint()`。新增回归夹具验证零、负请求在自定义引擎中可见，
  且引擎绘制路径不会被公共层短路。
- **验证结果**：默认及 `build-crop-jpeg` 回归目标、直接程序和 CTest 均通过；运行时
  仅保留仓库既有参数、窗口状态和空虚表诊断。构建仍有既有 XClass 函数指针、const
  丢弃及第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：便携无引擎图标仍要求正尺寸；高 DPI `actualSizeRatio()` 的公开 C99 接口
  仍按现有有限浮点校验处理，不模拟 Qt 平台窗口 DPR 查询。

### 10.360 2026-08-31 QIconEngine 缩放钩子 DPR 职责对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:236-247`
  的 `QIconEngine::virtual_hook(ScaledPixmapHook)` 只以 `size * scale` 请求物理
  像素图，注释明确 DPR 在钩子返回后由 `QIcon::pixmap()` 外层修正。
- **实现范围**：`Src/XGui/Icon/XIconEngine.c:114-150` 保留物理尺寸计算和整数溢出、
  非法比例保护，调用引擎 `pixmap()` 后不再在钩子内部调用
  `XPixmap_setDevicePixelRatio()`；DPR 继续由 `XIcon` 高 DPI 封装层统一计算。回归
  夹具改为检查物理尺寸和外层 DPR 结果，避免钩子与公共层重复缩放。
- **验证结果**：默认及 `build-crop-jpeg` 回归目标、直接程序和 CTest 均通过；
  `git diff --check` 通过。既有编译警告和运行时诊断保持原样，未运行 Valgrind/LSan，
  因此不宣称零泄漏。
- **边界**：自定义图标引擎返回的 DPR 由引擎结果保留；便携像素条目和平台图标引擎的
  实际设备像素比策略仍由各自 Drive/调用方实现，不在公共 C99 层推断。

### 10.361 2026-08-31 XPainter 仅画刷后端的批量矩形

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3258-3309,3318-3371`
  的 `QPainter::drawRects()` 在活动绘制器和正数量前提下交给扩展或底层引擎；
  `qpaintengineex.cpp:700-731` 及 `qpaintengine_raster.cpp:1436-1485` 分别通过路径
  填充和画笔描边，填充能力不依赖单独的画线回调。
- **实现范围**：`Src/XGui/Graphics/XPainter.c:2862-2875` 删除
  `XPainter_drawRects()` 对 `m_drawLine` 的前置要求，仅拒绝空设备；每个矩形继续复用
  `XPainter_drawRect()`，因此仅有画刷填充能力的后端可完成填充，无画笔时仍保持 Qt
  的成功空操作。`xgui_regression_test.c:3281-3301` 新增仅画刷后端填充夹具。
- **验证结果**：默认及 `build-crop-jpeg` 回归目标、直接程序和 CTest 均通过；默认
  全量构建随后恢复并通过。构建中的 XClass 函数指针、const 丢弃和第三方预处理警告
  为仓库既有问题，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：矩形仍使用当前 C99 整数 `XRect`，复杂变换、浮点矩形和 Qt 扩展路径由
  现有 XPainter 裁剪配置处理；无效设备仍返回失败，空数组仍为成功无操作。

### 10.362 2026-08-31 QIcon 主题引擎绘制的高 DPI 设备换算

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:783-788`
  的 `QIconLoaderEngine::paint()` 读取 `painter->device()->devicePixelRatio()`，先把
  `rect.size()` 换成物理像素交给 `pixmap()`，再使用原逻辑矩形调用 `drawPixmap()`；
  `qiconloader.cpp:849-873,969-973` 继续以物理请求较小边选择条目并按 `ceil(scale)`
  匹配主题目录。`qpainter.cpp:218-230,639` 表明图像绘制设备的 DPR 还会进入设备矩阵，
  `qpaintengine_raster.cpp:2093-2106` 则按图像 DPR 解释源像素尺寸。
- **实现范围**：`Src/XGui/Icon/XIconThemeEngine.c:31-134` 新增图像设备 DPR 查询和
  `XRect` 安全换算，正的有限 DPR 会同时缩放目标位置、宽高和主题物理请求尺寸；
  主题资源按物理较小边解析后再铺到物理目标矩形，最终复用现有 `XPainter_drawImageRect()`
  或底层图像回调。非图像设备（Picture/自定义回调）没有可查询 DPR，保持 1.0 逻辑路径；
  非法 DPR、负宽高和超出 `int` 范围的换算会安全地跳过绘制。`xgui_regression_test.c`
  的主题引擎夹具新增 2.0 DPR、物理起点 `(8,4)`、物理范围 `(48,36)` 及两侧背景断言。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成全量构建；四种配置的 `XGuiRegression_Test` 直接运行及
  CTest 均通过（各 1/1）。构建输出继续包含仓库既有的 XClass 函数指针兼容、const
  丢弃及第三方预处理警告，未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：公共 C99 的 `XRect` 只有整数坐标，DPR 为非整数时采用最近整数近似，未暴露
  Qt `QRectF` 亚像素矩阵；自定义绘制设备若内部拥有独立 DPR，需由其回调自行处理；
  SVG/平台主题的原生高 DPI资源发现仍受当前图像编解码器和 Drive 配置裁剪。

### 10.363 2026-08-31 QImageReader/QImageWriter 显式 DIB 编解码

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:269-270`
  和 `qimagewriter.cpp:157-158` 在显式格式名为 `dib` 时分别创建
  `QBmpHandler(QBmpHandler::DibFormat)`；`qbmphandler.cpp:678` 返回 `bmp`/`dib`
  名称，`:693-697` 对 DIB 跳过 14 字节 BMP 文件头。像素起点规则依据
  `qbmphandler.cpp:757-778`（`biSizeImage` 优先，其次是位域掩码和调色板），
  写出规则依据 `qbmphandler.cpp:829-855`（DIB 不写文件头）。Qt 的公共格式表在
  `qbmphandler.cpp:269-330` 不把 DIB 作为自动 MIME/扩展发现格式。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodec.h:25-45` 增加显式
  `XImageCodecFormat_Dib`；`XImageCodec.c:116-123,224-234,315-410`
  支持 `dib` 名称、格式名、尺寸探测以及读写能力，尺寸探测要求现代信息头的
  实际缓冲区不小于声明的 `biSize`，并按 Qt `16384*16384` 总像素上限拒绝
  超大 BMP/DIB 声明；`:449-498` 分派 DIB 编解码。新增的
  `XImageCodecInternal_decodeDib()`/`encodeDib()`（`XImageCodecBmp.c:560-623,686-860`）
  按 Qt 偏移规则在边界检查后临时包裹 14 字节 BMP 头复用解码路径；编码路径独立
  写出 40 字节 DIB 信息头、调色板及倒序 BGR 行，全部使用 XByteArray 与项目图像 API。
  `XImageBuiltinPlugin.c:21-22,
  145-181,252-285,386-431` 注册 DIB 读写处理器但保持空 MIME/过滤器，且
  `ImageFormat` 仅在 V4/V5 信息头存在非零 Alpha 掩码时返回 ARGB32，普通
  32 位 BITFIELDS 仍返回 RGB32；
  `XImagePluginRegistry.c:658-710` 只对内置 DIB 跳过自动内容探测和公共支持列表，
  外部插件同名键不受影响。
- **验证结果**：`xgui_regression_test.c:8572-8670` 新增 DIB 夹具，覆盖显式编码、
  不参与魔数发现、尺寸探测、截断信息头拒绝、像素无损解码、注册读写能力、公共
  格式列表隐藏，以及 `XImageWriter`/`XImageReader` 文件往返。默认 `build`、
  `build-crop-min`、`build-crop-painter-off`、`build-crop-jpeg` 均完成全量构建，
  直接回归和 CTest 各通过（1/1）；默认配置产物随后恢复。`git diff --check` 通过。
  构建输出仍有仓库已有 XClass 函数指针兼容、const 丢弃和第三方 zlib 预处理警告，
  未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：DIB 仅接受 Qt `QBmpHandler::DibFormat` 的 12/40/64/108/124 字节信息头，
  自动探测仍只识别带 `BM` 文件头的 BMP；编码统一写出 40 字节 INFOHEADER，32 位
  源图降为 24 位 BGR，8 位少色图可压缩为 4 位并写调色板，未实现 Qt 针对所有
  调色板、压缩和旧版 DIB 变体的独立格式转换；损坏偏移、尺寸溢出和不完整头部均返回失败。

### 10.364 2026-08-31 GIF 处理器选项语义对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:1131-1149`
  的 `QGifHandler::supportsOption()` 始终支持 `Animation`，仅对非顺序设备支持
  `Size`，不声明 `ImageFormat`；同文件 `:42-45` 的尺寸上限检查使用严格小于
  `16384 * 16384` 的像素限制。PNG/JPEG/SVG 处理器的选项集合分别见
  `qpnghandler.cpp:1081-1106`、`qjpeghandler.cpp:1126-1166` 和
  `qsvgiohandler.cpp:155-206`，用于确认 GIF 分支不能复用通用格式声明。
- **实现范围**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:198-231` 增加 GIF
  专用 `builtin_supportsOption()` 分支：在 `XIMAGECODEC_GIF_ANIM_ON` 打开时仅声明
  `Animation`，`Size` 仅在关联 `XIODevice` 非顺序访问时声明，其余选项（包括
  `ImageFormat`）均返回不支持。普通 BMP/PNG/JPEG/SVG 分支保留原有能力集合。
  `xgui_regression_test.c:10370` 增加 GIF 文件读取夹具，验证
  `XImageReader_imageFormatValue()` 对 GIF 返回 `XImageFormat_Invalid`，同时不影响
  动画准备、逐帧读取和延迟值行为。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成 `XGuiRegression_Test` 构建；四套配置的直接回归程序和
  CTest 均通过（各 1/1），随后恢复默认构建产物。构建输出仍含仓库已有的 XClass
  函数指针兼容、const 丢弃及第三方 zlib 预处理警告，未宣称零警告；本轮未运行
  Valgrind/LSan，不宣称全局无泄漏。
- **边界**：当前 GIF 便携处理器仍未实现 Qt 的增量 `imageIsComing()` 事件通知，且
  `ImageFormat` 按 Qt GIF 处理器语义保持不可用；顺序设备查询 `Size` 返回不支持，
  与 Qt 的随机访问约束一致。其他格式的完整 Qt 选项（如描述、压缩比和 SVG 背景色）
  仍受便携编码器裁剪，未在本节扩展。

### 10.365 2026-08-31 内置处理器分配上限前置检查

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.cpp:532-557`
  要求具体处理器在读取时调用 `allocateImage()`，并按有效 GUI 深度不低于 32 位
  计算 `QImageReader::allocationLimit()`；`qbmphandler.cpp:301-303` 在构造
  BMP 输出图像前执行该分配检查。`qimage_p.h:88-115` 规定行跨度、总字节数和
  `qsizetype` 溢出均属于无效分配参数。
- **实现范围**：`Src/XGui/Graphics/XImageIOHandler.h:338-349` 新增
  `XImageIOHandler_checkAllocation()`，复用现有 `calculateAllocation()` 的 32 位
  有效深度、行跨度和 `INT_MAX` 边界计算，并在正的 MB 上限下按字节数拒绝超限请求；
  `XImageIOHandler_allocateImage()` 保留同尺寸同格式只 detach 的 Qt 语义，并调用
  新检查避免重复逻辑。`Src/XGui/Graphics/XImageBuiltinPlugin.c:302-339` 在
  `read()` 读入压缩字节后、进入像素解码前从同一缓冲区查询 Size 并执行该检查；
  GIF 顺序设备仍保留流式读取路径。由此 BMP、DIB、PNG、JPEG 和 SVG 的内置便携
  处理器均不会在超限时建立像素缓冲区，且不会因重复 `peek()` 破坏顺序设备状态。
  `Src/XIO/XIODevice/XIODevicePrivate.c:184-214` 同时修复了底层窥视在 EOF
  负值返回时恢复已缓存前缀，保持 Qt `QIODevice::peek()` 的“无副作用”保证，
  对应 Qt `qiodevice.cpp:1828-1849,1867-1876`。
  `xgui_regression_test.c:7322-7372` 增加 1024x1024 BMP 直接处理器夹具，将上限设为
  1 MiB，断言读取在像素解码前失败且输出图像仍为空。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成 `XGuiRegression_Test` 构建；四套配置的直接回归程序和
  CTest 均通过（各 1/1），随后恢复默认构建产物。`git diff --check` 通过。
  构建输出仍含仓库已有 XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告，
  未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：当前读取器在进入处理器前已有基于 `width*height` 的通用上限预检，
  本节补充处理器直调和行跨度/整数边界；自定义外部插件仍需自行调用公开检查接口，
  才能获得与 Qt `allocateImage()` 相同的限制。SVG 无固定像素尺寸时无法在读取前
  推导分配量，仍由其便携解码器负责尺寸与资源限制。

### 10.366 2026-08-31 QImageIOPlugin const create 虚函数对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimageiohandler.h:101-117`
  将 `QImageIOPlugin::capabilities()` 与 `create()` 都声明为 `const` 虚函数，且
  `create()` 的格式参数默认为空字节串；`qimageiohandler.cpp:580-609` 规定插件
  工厂必须依据设备和格式创建已绑定的处理器，格式名始终为小写，空格式表示由
  `capabilities()` 已完成内容识别。
- **实现范围**：`Src/XGui/Graphics/XImageIOPlugin.h:86-98` 将
  `XImageIOPlugin_create_base()` 的插件对象改为 `const XImageIOPlugin*`；
  `XImageIOPlugin.c:12-15,60-64` 同步默认虚函数、虚表分派及函数指针类型，确保
  只读插件工厂可以按 Qt 签名重载。`XImageBuiltinPlugin.c:450-452` 的内置工厂和
  `xgui_regression_test.c:7163-7165` 的测试插件工厂同步使用 const 接收者；注册表
  的全部 `create_base()` 调用保持只读插件指针兼容，既有设备/格式状态仍由处理器
  自身维护。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off`、
  `build-crop-jpeg` 均完成全量构建；四套配置的 `XGuiRegression_Test` 直接运行及
  CTest 均通过（各 1/1），其中 `build-crop-jpeg` 最后一次回归输出
  `XGui regression tests passed`。`git diff --check` 通过。构建输出仍包含仓库已有
  的 XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告，未宣称零警告；本轮
  未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：C99 虚表通过 `const XImageIOPlugin*` 约束工厂入口，插件若需缓存可使用
  内部可变状态；动态 `QFactoryLoader` 目录发现、元对象和 QObject 生命周期仍不在
  XGui 的纯 C99 插件裁剪范围内。

### 10.367 2026-08-31 QImageReader 单边缩放尺寸查询条件对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1165-1185`
  规定 `read(QImage*)` 先初始化处理器，并仅在缩放尺寸恰好缺少宽或高时调用
  `size()` 推导另一维；普通读取不会无条件查询处理器的 `Size` 选项。
  同文件 `:1186-1203` 随后按 `ScaledSize`、`ClipRect`、`ScaledClipRect` 和
  `Quality` 能力设置处理器选项。Qt 插件处理器创建失败时由
  `initHandler()` 直接返回失败，不进入后续读取路径。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1503-1524` 将读取顺序调整为
  先建立处理器，再初始化缩放尺寸；只有单边缩放请求才调用
  `XImageReader_size()`，从而避免普通自定义处理器的 `Size` 选项副作用。插件
  裁剪关闭时若没有处理器，仍保留尺寸探测用于内置编解码器的分配上限预检；插件
  构建下处理器创建失败且错误仍为 Unknown 时映射为 `UnsupportedFormatError`。
  `xgui_regression_test.c:6962-6964,7036-7047,8042-8077` 为 mock handler 增加
  `Size` 查询计数，验证普通读取查询次数为零，单边缩放会查询原始尺寸并得到
  1x1 的保持宽高比结果。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off`、
  `build-crop-jpeg` 均完成全量构建；四套配置的 `XGuiRegression_Test` 直接运行
  与 CTest 均通过（各 1/1），默认配置产物随后恢复。`git diff --check` 通过。
  构建输出仍含仓库已有 XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告，
  未宣称零警告；本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：尺寸查询条件已与 Qt 普通/单边缩放分支一致，但插件裁剪关闭时的
  内置直连路径仍可能在处理器缺失时执行便携 `probeSize()`；高 DPI `@Nx`、动画
  和软件裁剪回退继续由现有读取器逻辑负责。动态插件发现及 QObject 生命周期不在
  纯 C99 裁剪范围内。

### 10.368 2026-08-31 QImageReader 后缀插件优先级与重复键遍历对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:161-219`
  从文件后缀取得 `suffixPluginIndex`，先只调用该外部插件的
  `capabilities(device, testFormat)` 与 `create()`；`:221-254` 在格式阶段跳过
  `suffixPluginIndex`，自动探测时遍历其余插件并在首个能力命中后停止；`:258-337`
  随后尝试内置处理器、确认后缀处理器 `canRead()`，失败才进入空格式内容探测。
  插件格式和 MIME 列表的内置优先、插件追加、排序去重规则见
  `qimagereaderwriterhelpers.cpp:22-43,84-143`。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.h:88-98` 新增带中文契约的
  `XImagePluginRegistry_createReadHandlerSuffix()` 入口；
  `XImagePluginRegistry.c:481-566` 实现后缀插件专用阶段：先定位首个声明后缀键的
  外部插件，工厂失败后跳过它，尝试后续首个 `CanRead` 插件，最后创建内置后缀处理器，
  并在每次能力/工厂调用前后恢复设备位置。通用
  `createReadHandlerEx()` 的内容自动探测阶段（`:447-473`）不再要求插件先声明
  后缀键，直接按 Qt `capabilities(device, testFormat)` 结果筛选。
  `Src/XGui/Graphics/XImageReader.c:764-804` 在自动探测且存在文件后缀时改用专用
  后缀入口，保留处理器 `canRead()` 校验和内容回退；显式格式、仅内容决定格式及
  关闭插件裁剪均继续走原有分支。
- **回归覆盖**：`xgui_regression_test.c:8289-8353` 验证显式格式下首个同键插件
  `create()` 返回空时 Qt 规定的停止行为；`:8355-8373` 让第二个同键插件仅在带
  格式参数时声明可读，首个后缀工厂故意失败，断言读取器选中第二个插件，从而区分
  后缀阶段与空格式内容回退阶段。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成全量构建；四套配置的 `XGuiRegression_Test` 直接运行及
  CTest 均通过（各 1/1），默认构建产物已恢复。`git diff --check` 通过。
  `build-crop-svg` 仍被与本轮改动无关的过期源文件路径阻塞：其生成的
  `build-crop-svg/CMakeFiles/XinYueCS.dir/build.make:3771-3775` 试图编译不存在的
  `/home/xinyue/Code/XinYueC/Src/XGui/Graphics/XFont8x16.c`，实际文件位于
  `Src/XData/XFont/XFont8x16.c`；因此未宣称该配置通过。构建输出继续包含仓库既有
  XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告；本轮未运行 Valgrind/LSan，
  不宣称全局无泄漏。
- **边界**：动态 `QFactoryLoader` 元数据发现和 QObject 生命周期仍由纯 C99 注册表
  代替；后缀优先级仅适用于 `XImageReader` 可取得文件名后缀的自动探测路径，顺序设备
  和显式格式仍按 Qt 的格式/内容分支处理。

### 10.369 2026-08-31 QImageReader::imageFormat 外部工厂失败后的内置回退

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:318-337`
  规定外部 imageformats 插件内容探测阶段结束后，仍需执行后续内置处理器内容
  探测；`QImageReader::imageFormat(QIODevice*)` 在 `:1458-1467` 只接收最终处理器
  `canRead()` 成功且非空的 `format()`。因此外部插件已声明 `CanRead` 但 `create()`
  返回空时，静态格式查询仍应返回内置处理器识别出的格式。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1992-2031` 在插件自动探测的
  `createReadHandlerEx()` 返回空后调用
  `XImagePluginRegistry_createReadHandlerContentFallback(device, NULL)`；该入口
  (`Src/XGui/Graphics/XImagePluginRegistry.c:574-639`) 重做外部首个能力命中阶段并
  继续尝试内置处理器，之后统一由 `imageFormatDevice()` 校验 `canRead()` 和非空
  `format()`。原有 16 字节签名兜底仅保留给插件裁剪关闭路径，避免短签名逻辑掩盖
  插件/内置阶段顺序。
- **回归覆盖**：`xgui_regression_test.c:10645-10691` 的长 XML 前缀 SVG 夹具在
  注册一个 `svg` 外部测试插件并强制其 `create()` 失败后调用
  `XImageReader_imageFormat_2()`，断言仍返回 `svg`；同时保留无插件时同一长前缀
  处理器识别，覆盖 Qt SVG 处理器对长前导注释的内容探测。
- **验证结果**：默认 `build` 全量构建、`./bin/XGuiRegression_Test` 和
  `ctest --test-dir build --output-on-failure -j1` 均通过（1/1）；
  `git diff --check` 通过。回归运行仍输出既有 transient parent、WindowActive 和
  空虚表诊断；编译仍含仓库已有 XClass 函数指针兼容、const 丢弃及第三方 zlib
  预处理警告，未宣称零警告。本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：内容回退仍受固定注册表容量和便携 SVG 4096 字节窥视上限约束；动态
  `QFactoryLoader` 目录发现、插件元对象生命周期及 SVGZ gzip 解码不在纯 C99 裁剪范围。

### 10.370 2026-08-31 QImageReader 显式格式自动探测的内容阶段重试

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:221-254`
  规定显式格式在自动探测开启时，格式插件阶段只跳过
  `suffixPluginIndex`（仅由文件名后缀产生）；显式格式本身没有该索引。随后
  `:318-337` 的内容阶段仍遍历未被后缀索引排除的插件，并把同一个
  `testFormat` 传给 `create()`。因此显式格式插件工厂失败后，不能把该插件误当成
  后缀插件永久跳过。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:447-474` 移除对
  `rejectedFormatPlugin` 的无条件跳过，仅保留内容模式与后缀专用入口自身的跳过逻辑；
  显式格式自动探测现在可在内容阶段按 Qt 顺序重新尝试该插件（继续传递同一
  `testFormat`）。后缀自动探测仍通过
  `createReadHandlerSuffix()` 跳过真实的 `suffixPluginIndex`，避免改变同键插件优先级。
- **回归覆盖**：`xgui_regression_test.c:8360-8374` 构造两个同键外部插件，使首个
  格式阶段工厂一次失败，调用显式格式且开启自动探测的注册表入口，断言内容阶段
  能再次创建处理器；既有后缀阶段“跳过首个后缀插件并尝试第二个同键插件”夹具
  继续覆盖相反分支。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 全量构建、`XGuiRegression_Test` 及 CTest 均通过（各 1/1），
  默认产物已恢复；`git diff --check` 通过。编译保留仓库既有 XClass 函数指针兼容、
  const 丢弃及第三方 zlib 预处理警告，回归保留既有窗口/空虚表诊断；未运行
  Valgrind/LSan，不宣称零泄漏。
- **边界**：固定注册表仍以注册顺序近似 Qt `QFactoryLoader` 的插件索引，未覆盖
  动态元数据加载、QObject 生命周期及插件目录扫描；顺序设备的位置恢复继续由
  便携 `XIODevice` 能力决定。

### 10.371 2026-08-31 QImageReader 外部插件失败后的内置内容回退

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:258-293`
  先按显式格式尝试内置处理器；`:318-337` 的内容探测阶段只遍历外部
  `QImageIOPlugin`，首个 `capabilities(device, QByteArray())` 命中后调用
  `create(device, testFormat)`，即使工厂返回空也结束外部阶段；`:340-413`
  随后独立遍历内置处理器内容能力。因此外部插件声明 `CanRead` 但工厂失败时，
  仍必须让内置 BMP/PNG 等处理器继续识别设备内容。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:447-490` 将通用内容
  探测拆为两个阶段：第一阶段跳过内置插件，仅按 Qt 规则尝试首个外部能力命中
  插件；第二阶段无论外部工厂是否失败，都用空格式调用内置插件的能力与工厂，
  并通过 `setupHandler()` 设置设备。外部插件仍在显式格式自动探测时收到同一
  `effectiveFormat`，内容决定格式时收到空格式；后缀专用入口的
  `suffixPluginIndex` 跳过规则不受影响。
- **回归覆盖**：`xgui_regression_test.c:8275-8287` 新增显式未知格式
  (`"mock"`) 的 BMP 夹具：外部测试插件声明可读但 `create()` 强制失败，断言
  `XImageReader_read()` 最终由内置 BMP 内容处理器成功解码并保留像素值；此前
  `:8355-8373` 的同键插件用例继续验证显式格式自动探测会重试外部插件，
  两条夹具分别覆盖外部重试和内置回退。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成全量构建；每套配置的 `XGuiRegression_Test` 及 CTest
  均通过（各 1/1），随后重新构建默认 `build` 恢复默认二进制并再次运行回归与
  CTest，结果仍为通过。`git diff --check` 通过。构建输出继续包含仓库既有
  XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告；窗口/空虚表诊断仍
  是既有运行时信息，未宣称零警告。本轮未运行 Valgrind/LSan，不宣称全局无泄漏。
- **边界**：固定注册表按注册顺序近似 Qt `QFactoryLoader`，只覆盖当前 C99
  插件接口；动态插件目录扫描、元对象生命周期以及内置格式表的完整轮询仍不在
  该裁剪实现范围内。内置内容探测使用现有 `XImageCodec_detect()` 的 4096 字节
  窥视上限，超出该上限的非 SVG 格式仍受便携编解码器能力限制。

### 10.372 2026-08-31 QImageReader 格式阶段排除内置处理器

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:221-254`
  的格式插件阶段只访问 `QFactoryLoader::keyMap()` 中的动态
  `QImageIOPlugin`；内置 BMP/PNG 等处理器不在该映射中，格式阶段结束后才由
  `:258-293` 的内置格式分支单独尝试。该阶段在自动探测时只对首个外部
  `CanRead` 能力命中调用 `create()`，并在工厂返回空时结束外部遍历。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:374-430` 的显式格式、
  自动探测分支现在显式跳过 `XImageBuiltinPlugin_instance()`，只执行外部插件
  能力和工厂调用；随后保留现有 `:431-445` 内置格式回退。这样内置处理器不会
  提前参与动态插件格式阶段，也不会改变 `rejectedFormatPlugin` 和内容阶段的
  外部重试/内置回退语义。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均重新完成全量构建，四套配置的 `XGuiRegression_Test` 与
  CTest 均通过（各 1/1）；默认产物最后恢复并再次通过回归与 CTest，
  `git diff --check` 通过。编译保留仓库既有 XClass 函数指针兼容、const 丢弃及
  第三方 zlib 预处理警告；未运行 Valgrind/LSan，不宣称零泄漏。
- **边界**：固定数组仍以注册顺序近似 Qt 动态 `keyMap`，并通过内置插件单例
  的显式排除维持阶段边界；动态目录发现、插件元对象生命周期和更多内置格式
  类型仍不在纯 C99 裁剪范围内。

### 10.373 2026-08-31 QImageReader 静态 imageFormat 的 QFile 后缀阶段

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:173-219`
  在设备为 `QFile`、未指定格式且启用自动探测时先解析文件后缀并尝试
  `suffixPluginIndex`；`:295-316` 要求后缀处理器通过 `canRead()` 后才接受，
  失败则跳过该插件继续内容探测；`:318-337`、`:340-413` 分别执行外部与内置
  内容回退。静态 `imageFormat(QIODevice*)` 在 `:1458-1467` 复用同一创建流程，
  只返回处理器自身的非空 `format()`。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1992-2070` 对 `XFile` 设备按
  虚表身份提取文件后缀，调用 `createReadHandlerSuffix()`，在后缀处理器拒绝
  内容或工厂失败时通过 `createReadHandlerContentFallback()` 跳过首个同后缀
  外部插件并回退内置处理器；普通设备仍使用原内容探测。同步调整
  `Src/XGui/Graphics/XImagePluginRegistry.c:339-349`，不再由注册表覆盖插件
  工厂返回的 `handler->format()`，由各插件自行设置，符合 Qt 插件契约。
- **回归覆盖**：`xgui_regression_test.c:8179-8248` 构造错误 `.bmp` 后缀的外部
  插件，验证读取和静态 `imageFormat(QIODevice*)` 均回退到内置 BMP；mock 插件
  仅在显式测试开关开启时设置 format，保留空 format 处理器边界。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成全量构建；四套配置的 `XGuiRegression_Test` 与 CTest
  均通过（各 1/1），默认产物已恢复；`git diff --check` 通过。编译继续保留
  仓库既有 XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告，回归中
  的窗口/空虚表诊断仍为既有信息；未运行 Valgrind/LSan，不宣称零泄漏。
- **边界**：当前通过 `XFile` 虚表身份近似 Qt `qobject_cast<QFile*>`；固定注册表
  仍以注册顺序近似动态 `QFactoryLoader`，不包含插件目录扫描、QObject 元对象
  生命周期及 SVGZ gzip 编解码。顺序设备的后缀优先级仍不适用，因为其没有文件名。

### 10.374 2026-08-31 QImageReader 外部 QFile 设备文件名

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:796-808`
  的 `QImageReader::fileName() const` 对当前设备执行 `qobject_cast<QFile *>`，
  设备为 `QFile` 时返回其 `fileName()`，没有 QFile 或尚未绑定设备时返回空
  `QString`。该语义与 `setFileName()` 创建的内部 QFile 相同，但不要求读取器
  接管外部设备生命周期。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1098-1119`
  的 `XImageReader_fileName_const()` 在显式文件名缺失时检查当前设备的
  `XFile` 虚表，并通过 `XFileDevice_fileName_base()` 返回设备名称；非 XFile
  设备继续返回空值，`fileName()` 和 UTF-8 兼容重载复用该结果。仅借用外部
  设备，不改变读取器的所有权规则。
- **回归覆盖**：`xgui_regression_test.c:7571-7588` 使用栈上外部 `XFile` 调用
  `XImageReader_init_device_2()`，断言 `fileName_2()` 返回完整 BMP 文件名，
  并在读取器销毁后单独释放设备，覆盖外部设备生命周期边界。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 均完成全量构建；各配置的 `XGuiRegression_Test` 与 CTest
  均通过（各 1/1），默认构建最后恢复并再次通过回归与 CTest；
  `build-asan` 也完成构建，使用 `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0`
  运行回归后功能断言通过，但 LeakSanitizer 报告约 102226 字节、464 个分配
  未释放（主要栈为 Mesa/fontconfig 运行库及既有 XFont/Painter 状态路径），
  因而不能宣称全局无泄漏；Valgrind 未安装。编译输出继续包含仓库既有
  XClass 函数指针兼容、const 丢弃及第三方 zlib 预处理警告，回归中的
  窗口/空虚表诊断仍是既有信息。
- **边界**：`XFile` 虚表身份检查是对 Qt `qobject_cast<QFile*>` 的 C99 近似，
  不覆盖其它继承 `QFile` 的 QObject 类型或动态元对象查询；设备文件名指针
  仅在外部 XFile 保持有效期间可用，读取器不会复制该名称。

### 10.375 2026-08-31 QIcon 引擎尺寸/DPR 与 XPainter 批量矩形绘制

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1017-1040`
  规定引擎路径的 `actualSize()` 与 `paint()` 透传原始请求尺寸；
  `qicon.cpp:911-930`、`qiconengine.cpp:236-247` 规定缩放钩子只生成
  `size * scale` 的物理像素，设备像素比由外层统一处理。绘制批量矩形的
  `qpainter.cpp:3258-3309`、`qpaintengineex.cpp:700-730` 和
  `qpaintengine_raster.cpp:1436-1485` 允许仅有画刷填充回调的后端完成
  `drawRects()`，不要求画笔回调。
- **实现范围**：`Src/XGui/Icon/XIcon.c:1019-1170` 对引擎路径保留零/负尺寸
  的 `actualSize()/paint()` 可见性，无引擎回退仍拒绝不可用尺寸；
  `Src/XGui/Icon/XIconEngine.c:114-151` 的 scaled hook 仅计算物理尺寸，
  不在钩子内部改写 DPR。`Src/XGui/Graphics/XPainter.c:2864-2874`
  移除 `drawRects()` 对 `m_drawLine` 的强制依赖，逐矩形复用填充路径。
- **回归覆盖**：`xgui_regression_test.c` 覆盖引擎路径的零/负尺寸透传、
  scaled hook 物理尺寸和默认 DPR，以及仅画刷后端的批量矩形填充；默认与
  `build-crop-jpeg` 的回归目标、CTest 均通过，当前轮的 `build-crop-min`、
  `build-crop-painter-off` 也各通过 1/1。
- **验证结果**：默认 `build` 已在本轮最后重新构建并运行回归与 CTest，
  `git diff --check` 通过。编译仍保留仓库既有 XClass 函数指针兼容、const
  丢弃及第三方 zlib 预处理警告；运行时窗口/空虚表诊断为既有信息。
  `build-asan` 回归功能断言通过，但 LeakSanitizer 报告约 102226 字节、
  464 个分配未释放（Mesa/fontconfig 与既有 XFont/Painter 状态栈），
  Valgrind 未安装，不能宣称全局无泄漏。
- **边界**：便携图标无引擎回退仍要求正尺寸；C99 scaled hook 不实现 Qt
  QObject 元对象和动态主题插件发现。`drawRects()` 仍逐项调用便携回调，
  不提供 Qt 光栅引擎内部批量优化。

### 10.376 2026-08-31 QImageWriter 外部 QFileDevice 文件名

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:395-408`
  规定 `QImageWriter::fileName() const` 对当前设备执行
  `qobject_cast<QFileDevice *>`；设备是文件设备时返回其文件名，否则返回空
  `QString`。该接口覆盖外部 `QFile`/`QSaveFile` 设备，也覆盖
  `setFileName()` 创建的内部文件设备。
- **实现范围**：`Src/XGui/Graphics/XImageWriter.c:596-602` 在显式文件名缺失
  时按 `XFile` 和（启用时）`XSaveFile` 虚表识别外部文件设备，并通过
  `XFileDevice_fileName_base()` 返回借用的设备名称；非文件设备继续返回空值，
  `fileName()` 及 UTF-8 兼容重载共享该结果。
- **回归覆盖**：`xgui_regression_test.c:6880-6890` 使用栈上内部 `XFile`，
  `xgui_regression_test.c:6912-6930` 使用栈上外部 `XFile`
  分别断言内部文件后缀推断、外部设备的完整文件名返回值以及设备所有权边界。
- **验证结果**：默认 `build`、`build-crop-min`、`build-crop-painter-off` 和
  `build-crop-jpeg` 完成全量构建；本轮各配置的回归目标与 CTest 均通过（各
  1/1），默认产物最后恢复并通过。编译继续保留仓库既有 XClass 函数指针兼容、
  const 丢弃及第三方 zlib 预处理警告，运行时窗口/空虚表诊断仍为既有信息。
  `build-asan` 功能断言通过但 LeakSanitizer 仍报告约 102226 字节、464 个分配
  未释放（Mesa/fontconfig 与既有 XFont/Painter 状态路径），Valgrind 未安装，
  因此不能宣称全局无泄漏。
- **边界**：C99 通过具体 `XFile`/`XSaveFile` 虚表身份近似 Qt 的动态
  `qobject_cast<QFileDevice*>`，不覆盖自定义 QFileDevice 派生类；返回的名称指针
  仅在外部设备存活期间有效，写入器不会复制该名称。

### 10.377 2026-08-31 QImageWriter 外部 QFileDevice 后缀自动格式

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:101-198`
  的 `createWriteHandlerHelper()` 在显式格式为空时，从任意
  `QFileDevice` 的文件名提取小写后缀，并以该后缀选择内置或插件写入器；
  `qimagewriter.cpp:252-277` 的 `canWriteHelper()` 先按需打开设备、检查可写状态，
  再创建写入器。`qimagewriter.cpp:651-688` 规定 `write()` 先拒绝空图像，写入器
  拒绝后直接返回，并且仅对 `QFileDevice` 执行刷新。
- **实现范围**：`Src/XGui/Graphics/XImageWriter.c:173-224` 新增统一的外部
  `XFile`/`XSaveFile` 文件名解析 helper；`resolveFormatForHandler()`、`canWrite()`
  和 `fileName_const()` 共享该结果。`write()` 在无插件或无可用 handler 时仍使用
  解析出的后缀调用直接编解码器，保证外部文件设备与内部文件设备行为一致；
  相关分支保留 Qt 的空图像先检查、handler 写入失败不切换格式和文件设备刷新语义。
- **回归覆盖**：`xgui_regression_test.c:6912-6930` 使用栈上外部 `XFile`，不设置
  格式即调用 `canWrite()` 与 `write()`，断言按 `.bmp` 后缀自动识别并生成目标文件，
  再独立销毁写入器和外部设备，验证设备所有权与临时文件清理边界。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建、回归程序和 CTest
  均通过；`build-crop-plugin-off` 目标构建与 20 秒超时回归也通过，随后已恢复默认
  产物。`git diff --check` 通过。编译仍保留仓库既有 XClass 函数指针兼容、const
  丢弃及第三方 zlib 预处理警告，运行时窗口/空虚表诊断仍为既有信息。
  `build-asan` 的功能断言历史上通过但 LeakSanitizer 仍报告约 102226 字节、464 个
  分配未释放，Valgrind 未安装，因此不能宣称全局无泄漏。
- **边界**：C99 通过具体 `XFile`/`XSaveFile` 虚表身份近似 Qt 的动态
  `qobject_cast<QFileDevice*>`，不覆盖自定义 QFileDevice 派生类；格式后缀只在
  设备文件名可取得且扩展名受当前内置/插件支持时生效，返回的设备文件名指针不复制。

### 10.378 2026-08-31 XPainter 图元空输入与角度语义审计

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.cpp:3895-3921`
  规定圆角半径任一非正值先退化为 `drawRect()`；`qpainter.cpp:3959-3993`、
  `4087-4110`、`4154-4184` 和 `4225-4247` 规定椭圆、圆弧、扇形、弦形先
  归一化矩形，弧度单位为十六分之一度，零跨度不产生图元。`qpainter.cpp:3418-3460`
  与 `4442-4475` 规定点集数量小于等于零、折线点数小于二时直接返回；
  `qpainter.cpp:4558-4629` 规定多边形点数小于二直接返回、首尾隐式闭合，
  凸多边形使用独立的 `ConvexMode` 并始终使用当前画刷。
- **实现范围**：`Src/XGui/Graphics/XPainter.c:4330-4615` 已逐项复核上述
  归一化和空输入行为：形状接口在未绑定设备时返回失败，空或零尺寸图元
  返回成功空操作；圆角半径非正时调用普通矩形；弧跨度回退路径限制在
  `[-5760, 5760]`（`XPainter.c:2524-2530`），正角度在设备坐标中从三点钟
  方向逆时针经过十二点钟；多边形回退保持首尾闭合并透传 `XPainterFillRule`。
  `XPainter.c:2864-2874` 的批量矩形继续允许仅有画刷填充回调的便携后端，
  与 Qt `drawRects()` 的画刷/画笔独立处理一致。
- **验证范围**：现有 `xgui_regression_test.c:3609-3830` 覆盖五种图元的
  填充、描边、方向、零跨度、反向矩形和零半径回退；`xgui_regression_test.c:3852-4100`
  覆盖多边形/凸多边形/点集的填充规则、首尾闭合、回调透传及 NULL/零计数
  不派发。默认构建、裁剪构建及回归测试在本轮复跑中保持通过。
- **近似边界**：C99 图像后端的椭圆与弧线回退使用固定 32/64 段折线采样，
  不等同 Qt 光栅引擎的贝塞尔曲线和抗锯齿；`m_drawShape`/`m_drawPolygon`
  是便携扩展回调，凸多边形在该回调上复用多边形入口而非暴露 Qt 私有
  `ConvexMode`，对非凸输入仍保留项目定义的确定性结果。插件、主题和
  QObject 元对象行为不属于 XPainter 图元回退范围。

### 10.379 2026-08-31 polygon-off 裁剪构建目录重配置验证

- **验证范围**：此前 `build-crop-polygon` 生成文件仍引用已移动的
  `Src/XGui/Graphics/XFont8x16.c`，该路径与当前源码树不符，不能作为本轮
  裁剪构建证据。本轮使用根目录 `CMakeLists.txt` 重新配置该目录，并保持
  `CMAKE_C_FLAGS=-DXPAINTER_POLYGON_ON=0`；重新生成后的构建图正确收录
  `Src/XData/XFont/XFont8x16.c`。
- **验证结果**：`cmake --build build-crop-polygon --target XGuiRegression_Test -j1`
  成功，直接运行 `./bin/XGuiRegression_Test` 输出
  `XGui regression tests passed`，`ctest --test-dir build-crop-polygon
  --output-on-failure` 为 1/1 通过。随后重新执行默认
  `cmake --build build --target XGuiRegression_Test --clean-first -j1`，默认
  回归和 CTest 均再次 1/1 通过；默认产物已恢复。`git diff --check` 通过。
- **边界与诊断**：构建输出仍有仓库既有 XClass 函数指针兼容、const 丢弃及
  第三方 zlib 预处理警告，运行时窗口/空虚表诊断仍为既有信息；本轮未运行
  Valgrind/LSan，不能宣称全局无泄漏。该记录只修复验证目录的过期生成状态，
  未改动源码中的平台裁剪逻辑。

### 10.380 2026-08-31 QGifHandler Animation 选项返回值对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/plugins/imageformats/gif/qgifhandler.cpp:1131-1139`
  规定 GIF 处理器在支持动画时同时声明 `Animation` 选项；
  `qgifhandler.cpp:1141-1158` 的 `option(Animation)` 无论首帧是否读取都返回
  `true`，而 `Size` 仅在非顺序设备上可用。`QImageReader::supportsAnimation()`
  在 `qimagereader.cpp:1009-1015` 透传该选项值。
- **实现范围**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:375-390` 的内置处理器
  选项查询新增 `Animation` 分支：当 GIF 动画开关开启且
  `supportsOption(Animation)` 为真时，将输出值设为 `true`；非 GIF 或动画裁剪
  关闭时仍返回不支持。读取器原有动画缓存路径不变，继续提供帧跳转、循环次数和
  当前帧延迟。
- **回归覆盖**：`xgui_regression_test.c:10596-10618` GIF 动画夹具新增直接
  `XImageIOHandler` 断言，验证内置处理器声明 `Animation` 且 `option()` 返回真；
  随后的 `XImageReader`/`XMovie` 夹具继续验证四帧读取、跳转和延迟语义。
- **验证结果**：默认 `build` 的目标构建、回归和 CTest 均通过；新增
  `build-crop-gif-off`（`CMAKE_C_FLAGS=-DXIMAGECODEC_GIF_ANIM_ON=0`）配置的
  目标构建、直接回归和 CTest 也均 1/1 通过，随后已恢复默认构建产物。
  `git diff --check` 通过。编译继续保留仓库既有 XClass 函数指针兼容、const
  丢弃及第三方 zlib 预处理警告，运行时窗口/空虚表诊断为既有信息；本轮未运行
  Valgrind/LSan，不宣称全局无泄漏。
- **边界**：动画数据仍由便携 GIF 解码缓存承载，不提供 Qt 插件的增量流式解码和
  QObject 元对象；顺序设备上的 `Size` 选项依照 Qt 保持不可用。

### 10.381 2026-08-31 PPM/PBM/PGM 内置处理器与子类型编解码

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qppmhandler.cpp:78-104`
  的 `read_pbm_header()` 只接受 `P1` 至 `P6`，宽高必须为正且不超过 32767，
  灰度/RGB 最大样本值为 1 至 65535；`:111-269` 的 `read_pbm_body()` 将
  PBM 解码为 `Format_Mono`（索引 0 白、1 黑），PGM 解码为 `Format_Grayscale8`，
  PPM 解码为 `Format_RGB32`，并按大端读取 16 位样本；`:272-421` 的
  `write_pbm_image()` 按 `sourceFormat.left(3)` 选择 PBM/PGM/PPM，分别写出
  P4/P5/P6，raw 后缀只影响子类型键；`:437-477` 的 `canRead()` 以 P1/P4、
  P2/P5、P3/P6 分别报告 `pbm`、`pgm`、`ppm` 子类型。`qimageiohandler.cpp:98-102`
  规定 `SubType` 是处理器级格式细分值；`qimagereader.cpp:846-870`、
  `1010-1027`、`1498-1532` 规定尺寸、图像格式、格式列表和 MIME 映射的查询
  语义。
- **实现范围**：新增
  `Src/XGui/Graphics/XImageCodec/XImageCodecPpm.c:1-413`，使用有界 token
  解析器处理空白与 `#` 注释、P1-P6 ASCII/二进制样本、16 位大端样本和尺寸
  边界；解码输出原生 `Mono`、`Grayscale8`、`RGB32` 并安装 PBM
  白黑调色板。`XImageCodecInternal.h:191-232` 暴露尺寸探测、解码、通用 P6
  编码和子类型编码接口；`XImageCodec.c:117-189`、`:420-565` 接入格式别名、
  signature、能力、尺寸、读写分派。`XImageCodec.h:34-45` 增加 `Ppm` 枚举，
  `XImageCodec_config.h:1-25,137-145` 增加 `XIMAGECODEC_PPM_ON` 独立裁剪开关。
- **处理器和门面接入**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:19-40`
  声明六个 Qt PPM 格式键及三类 MIME，`:152-218` 按 P1-P6 返回原生图像格式，
  `:222-260` 支持 Size/ImageFormat/SubType，`:388-403` 按子类型写出，
  `:520-548` 在内容发现时报告 `pbm`/`pgm`/`ppm` 子类型；
  `Src/XGui/Graphics/XImageReader.c:134-145,214-228,2150-2166` 与
  `Src/XGui/Graphics/XImageWriter.c:20-29,897-924,1012-1028` 接入六个
  格式键、MIME 去重、内容探测和 MIME 反查。`Src/XGui/Graphics/XImage.c:3882-3911`
  与 `:3936-3962` 的文件/设备保存路径在 PPM 开启时调用子类型编码器；无 PPM
  裁剪时不产生未定义符号引用。
- **回归覆盖**：`xgui_regression_test.c:7409-7433` 按 Qt 插件键数量及三类共享
  MIME 校验注册表；`:9105-9321` 覆盖 P1-P6 发现、尺寸探测、原生输出格式、
  PBM 调色板、P6 往返、PBM/PGM 子类型 P4/P5 头部、截断数据拒绝及
  `XImageReader` 内容发现和读取。默认 `build` 的目标恢复构建、直接回归、
  CTest（1/1）和全量增量构建均通过；`build-crop-ppm-off`（
  `-DXIMAGECODEC_PPM_ON=0`）重新链接、直接回归和 CTest（1/1）均通过，之后
  已再次用默认配置 clean-first 恢复 `bin/XGuiRegression_Test`。
- **近似边界**：输入解析与 Qt 一样限制尺寸到 32767，但二进制头部仅复现
  `maxval` 后终止字符、注释和 EOF 的消费规则，不模拟更复杂的流式设备状态和 Qt 私有
  `QImageIOHandler::allocateImage()`；P1-P6 解码、16 位缩放和 PBM 白黑索引
  已对齐，写出使用固定 8 位 P4/P5/P6，PBM 灰度阈值采用便携 C99 的固定
  `0.299/0.587/0.114` 阈值，不包含 Qt 调色板转换和抖动细节。raw 后缀按 Qt
  只作为识别键，实际仍输出对应二进制变体；通用 `XImageCodecFormat_Ppm`
  编码接口默认 P6。`XIMAGEIOPLUGIN_ON` 关闭时仍保留直接编解码能力，未实现
  Qt 动态插件扫描、QObject 元对象或自定义 QFileDevice 派生类发现。
- **诊断与内存边界**：构建输出继续有仓库既有 XClass 函数指针兼容、const
  丢弃及第三方 zlib 预处理警告，运行时窗口/空虚表诊断仍为既有信息；本轮未
  运行 Valgrind/LSan，不能宣称全局无泄漏。历史 ASan 结果中的 Mesa/fontconfig
  及既有 XFont/Painter 状态泄漏边界仍适用。

### 10.382 2026-08-31 XBM 内置处理器、MonoLSB 位图与裁剪开关

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qxbmhandler.cpp:25-92`
  的 `read_xbm_header()` 逐行读取 XBM 头部，单行最多 299 字节、累计头部最多
  4096 字节，并要求宽高定义均为 1..32767；`:94-152` 的 `read_xbm_body()`
  扫描 `0xHH` 字节，分配 `QImage::Format_MonoLSB`，索引 0 为白、索引 1
  为黑，正文不足时保留已写入数据并仍返回成功；`:154-236` 的
  `write_xbm_image()` 将输入转换为 MonoLSB，写出 `<name>_width`、
  `<name>_height` 和静态字节数组，并在调色板索引 0 较暗时反转输出字节；
  `qxbmhandler.cpp:255-291` 规定顺序设备不能探测，随机设备探测后恢复原位置，
  处理器选项包括 Name、Size 和 ImageFormat。`qimagewriter.cpp:164-168` 将
  `xbm` 作为内置格式，`qimagereaderwriterhelpers_p.h` 的 MIME 表使用
  `image/x-xbitmap`。
- **实现范围**：新增
  `Src/XGui/Graphics/XImageCodec/XImageCodecXbm.c:1-321`，使用项目
  `XImage`、`XByteArray` 和 `XMalloc_System` 兼容接口实现有界头部解析、尺寸
  探测、MonoLSB 解码、白黑调色板、截断正文容忍、文件名标识符清洗及带名称
  编码；像素字节按 Qt 的最低位在左规则写出，调色板灰度关系决定是否反转。
  `Src/XGui/Graphics/XImageCodec/XImageCodecInternal.h:235-267` 增加三组内部
  接口，`XImageCodec_config.h:144-147` 增加独立的
  `XIMAGECODEC_XBM_ON` 开关。`Src/XGui/Graphics/XImageCodec/XImageCodec.c`
  在 `:146-147`、`:195-201`、`:251-261`、`:437-440`、`:468-496`、
  `:535-580` 接入名称、头部识别、格式名、尺寸、读写能力和编解码分派。
- **内置处理器与发现**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:19-33`
  注册 `xbm`、`image/x-xbitmap` 和 `*.xbm`；`:215-217` 返回 MonoLSB，
  `:258-263` 声明 Name/Size/ImageFormat，`:329-334` 拒绝顺序设备，
  `:391-437` 使用 Name 选项调用带标识符编码器，`:449-463` 返回名称选项。
  `Src/XGui/Graphics/XImageReader.c:137-145,214-224,2150-2167` 和
  `Src/XGui/Graphics/XImageWriter.c:21-29,897-944,1024-1041` 接入格式键、
  MIME 反查、内容探测、外部文件名和直接设备写出；
  `Src/XGui/Graphics/XImage.c:3900-3903` 的文件保存路径同样传递目标文件名。
- **回归覆盖**：`xgui_regression_test.c:9240-9336` 覆盖 XBM 名称解析、头部
  识别、尺寸边界、MonoLSB 位序、白黑调色板、带文件名编码、编码结果往返、
  截断正文成功语义和超过 32767 的尺寸拒绝；`:7427-7428` 将注册表格式数量
  与 Qt 的单一 `xbm` 键保持一致。默认 `build` 目标 clean-first 构建、直接
  回归和 CTest（1/1）通过；`build-crop-xbm-off` 使用
  `-DCMAKE_C_FLAGS=-DXIMAGECODEC_XBM_ON=0` 完成目标构建、直接回归和 CTest
  （1/1）通过，证明关闭开关后 XBM 算法和符号均可裁剪，随后恢复默认构建产物。
- **近似边界**：C99 实现将 XBM 标识符中的路径、扩展名和非标识字符规范化，
  以便无文件名的内存编码稳定产生 `image_*`；这比 Qt 直接使用设备文件名更
  保守。正文扫描接受小写 `0x`，未模拟 Qt 顺序设备的完整回退流状态；缺失
  字节按 Qt 保留零填充并成功返回。`XIMAGEIOPLUGIN_ON=0` 时仍保留直接门面
  编解码能力，不实现 Qt 动态插件扫描、QObject 元对象或自定义 QFileDevice
  派生类发现。构建输出继续存在仓库既有 XClass 函数指针兼容、const 丢弃和
  第三方 zlib 预处理警告；历史 ASan 的 Mesa/fontconfig 与 XFont/Painter
  状态泄漏边界仍适用，不能宣称全局无泄漏。

### 10.383 2026-08-31 XBM 处理器发现阶段的完整正文校验

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qxbmhandler.cpp:263-281`
  的 `QXbmHandler::canRead(QIODevice*)` 要求随机访问设备，保存当前位置，
  调用完整的 `read_xbm_image()` 后恢复位置；`:115-151` 的正文扫描在遇到
  非十六进制 `0xHH` 时失败，但正文截断到文件尾时保留已读像素并返回成功。
  因而 XBM 不能只凭两个尺寸定义判断 `canRead()`。
- **实现范围**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:315-363` 新增
  `builtin_validateXbmDevice()`，在设备位置不变的前提下窥视剩余文件，先以
  `XImageCodecInternal_probeXbmSize()` 和 `XImageIOHandler_checkAllocation()`
  做尺寸/分配上限检查，再调用同一 XBM 解码器验证正文；顺序设备继续按
  Qt 规则拒绝。`VXImageBuiltinHandler_canRead()` 在检测到 Xbm 且内容格式
  一致后调用该校验，避免非法十六进制字节被处理器发现阶段误报为可读。
- **回归覆盖**：`xgui_regression_test.c:9261-9384` 新增 `0x8g` 畸形正文的
  文件夹具，断言 `XImageReader_canRead()` 在发现阶段返回 false；同时通过
  `XImageWriter_init_file_2(..., NULL)` 的 `.xbm` 后缀推断写出，再由
  `XImageReader` 自动发现并读取，验证 MonoLSB、格式名和尺寸。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 增量构建、直接回归和
  CTest（1/1）均通过；本次未改变 XBM 开关接口，先前的
  `build-crop-xbm-off` 裁剪构建证据仍有效。构建日志继续包含仓库既有的
  XClass 函数指针兼容、const 丢弃和第三方 zlib 警告，不能宣称零警告；
  未运行 Valgrind/LSan，不能宣称全局无泄漏。
- **边界**：校验仅对随机访问设备执行完整窥视，设备尺寸不可用或当前位置
  已在末尾时按不可读处理；直接 `XImageCodec_detect()` 仍是有界头部探测，
  真正的正文合法性由 XImageReader 内置处理器在 `canRead()` 阶段完成。XBM
  解码对截断正文的成功/零填充语义保持 Qt 行为。

### 10.384 2026-08-31 PPM 头部、分隔符与灰度缩放边界收紧

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qppmhandler.cpp:93-109`
  的 `read_pbm_header()` 先要求前三字节严格为 `P[1-6]` 加 ASCII 空白，
  再解析宽高和最大样本值；`read_pbm_int()` 每次只消费一个终止字符，
  二进制 P5/P6 的头部不会把 CRLF 当作一个整体分隔符。`:129-143` 的
  16 位 PGM 路径使用 `(sample * 255) / maxValue` 截断，`:155-169` 的
  PPM 颜色路径才使用 `QRgba64::toArgb32()` 换算；P1 调用
  `read_pbm_int(..., maxDigits=1)`，每个样本仅读取一位数字，且 ASCII/二进制
  样本均不先检查是否超过 `maxval`；样本十进制溢出 `int` 时返回 `-1` 但继续
  解码。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecPpm.c:27-220`
  改用固定 ASCII 空白集合，拒绝前导空白及 `P6#...` 这类缺少魔数分隔符的
  数据，并在原始变体中按 Qt 规则消费一个终止字节（`#` 注释消费到行尾，
  CRLF 只消费 `\r`）；`:133-220` 允许原始和 ASCII 样本超过 `maxval`，新增
  P1 专用单数字读取，并将灰度与 RGB 缩放拆成独立函数，`maxval=255` 时按
  低 8 位处理，其他 P2/P5 值先按 16 位截断再缩放，ASCII 溢出值按 Qt 的
  `-1` 窄化处理，P3/P6 保持 RGBA64 语义。
  所有解析仍受固定 token 容量和 32767/65535 尺寸约束，不使用平台 API 或
  标准堆分配。
- **回归覆盖**：`xgui_regression_test.c:9117-9124,9223-9283` 新增最大值
  100/1000 的 PGM 50% 样本断言 `0x7f`、P1 字符串 `10` 的逐位读取，以及
  P2/P5 样本 255 超过 `maxval=100` 时的 `0x8a` 整数缩放、ASCII `int` 溢出时
  的 `0xca` 窄化结果，并验证前导空白与 `P6#comment` 头部在显式解码时被拒绝，
  同时覆盖 `maxval` 后行注释；既有
  P1-P6、子类型、截断数据及 XImageReader 发现夹具继续覆盖完整注册路径。
- **验证与边界**：默认配置目标 clean-first 构建、直接回归、CTest（1/1）和
  `build-crop-ppm-off` 目标构建、直接回归、CTest（1/1）均通过；编译继续保留
  仓库既有 XClass 函数指针兼容、const 丢弃和第三方 zlib 警告，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
  CRLF 原始 PPM 的首个 LF 被按 Qt 行为视为样本字节，这是兼容性边界而非通用
  PPM 宽松解析。

### 10.385 2026-08-31 XImagePluginRegistry 的平台中立动态发现入口

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers.cpp:18-20`
  使用 `QFactoryLoader` 懒加载 `/imageformats` 目录；`:22-43` 按插件索引读取
  格式键并以 `capabilities()` 筛选；`:45-67` 从插件元数据的 `Keys`、
  `MimeTypes` 成对读取 MIME 信息并再次检查能力；`:84-116` 对公开格式和 MIME
  列表排序、去重；`:118-142` 的 MIME 反查保留内置表和插件元数据的首次出现
  顺序。`qimagereader.cpp:161-219,221-254,318-337` 说明后缀插件优先、
  能力检查及内容发现的阶段顺序。`qimageiohandler.cpp:177-212` 说明插件由
  工厂创建处理器，多个同能力插件的选择不要求稳定优先级。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.h:28-47` 增加全中文
  注释的 `XImagePluginRegistryDiscoverCallback` 和设置函数；
  `Src/XGui/Graphics/XImagePluginRegistry.c:19-25,81-103` 在第一次注册表查询
  时调用 Drive 提供的发现回调，使用递归锁下的进度标志防止回调重入；回调返回
  true 后本轮只调用一次，返回 false 时保留重试机会。`:249-268` 的 `clear()`
  保留回调配置但使发现状态失效，后续查询重新发现；`:974-976` 在
  `XIMAGEIOPLUGIN_ON=0` 时提供无操作桩。该入口不直接使用目录、动态库或其他
  平台 API，不修改 `XImageIOPlugin` 虚函数表 ABI；已有 `addPlugin()` 插入逻辑
  继续保证显式/发现插件先于内置插件，公开列表的排序和去重规则保持不变。
- **回归覆盖**：`xgui_regression_test.c:7282-7299` 的测试回调模拟 Drive 发现并
  注册外部插件；`:8791-8823` 断言首次查询只调用一次、发现插件位于内置插件
  之前、重复查询不重复调用，`clear()` 后再次查询会重试，并覆盖零容量配置下
  的回调执行语义。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标增量构建、最终直接回归、
  CTest（1/1）和全量增量构建均通过，插件注册表回调相关断言未报错。
  `build-crop-plugin-off` 在重新配置后目标构建、直接回归和 CTest（1/1）均通过；
  同时补齐了插件关闭时 GIF 动画与 DIB 注册表断言的条件裁剪，验证无插件配置不再
  引用被裁剪的 `XImageIOPlugin` 类型。构建日志继续包含仓库已有的 XClass 函数指针
  兼容、const 丢弃及第三方 zlib 警告，未运行 Valgrind/LSan，不能宣称零警告或全局
  无泄漏。
- **边界**：回调不取得 `userData` 或插件对象所有权；调用方必须在释放用户数据
  前先调用 `setPluginDiscoveryCallback(NULL, NULL)`。回调运行于注册表锁范围内，
  只能执行短时发现/注册操作；递归查询会被进度标志抑制。回调期间若重设回调或
  清空注册表，内部代际标记会丢弃旧回调返回值，避免新配置被覆盖。目录扫描、
  动态库加载、元数据解析和插件生命周期仍由 Drive 层实现；固定容量限制及
  `clear()` 不释放外部插件对象的既有约定保持不变。

### 10.386 2026-08-31 XImage 色彩空间传递函数与灰度矩阵对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolortransferfunction_p.h:40-63`
  定义参数化曲线的正向计算与有限值处理，`:101-120` 给出 Gamma、sRGB、ProPhoto
  RGB 和 BT.2020 参数；`qcolortransfergeneric_p.h:65-104` 给出 HLG 在线性
  `[0,12]` 与 PQ 在线性 `[0,64]` 范围的正逆函数。`qcolortransform.cpp` 的灰度
  加载路径使用灰度白点向量作为唯一输入轴，不把同一灰度值重复累加到三个轴。
- **实现范围**：`Src/XGui/Graphics/XImage.c:712-813` 补齐 Gamma、ProPhoto RGB、
  BT.2020、ST.2084(PQ) 与 HLG 的解码/编码曲线，保留 HDR 曲线的扩展白点结果，
  由最终 8 位通道量化阶段负责裁剪；移除入口对大于 1 的输入截断，使 PQ(1) 和
  HLG(1) 分别保持 Qt 的 64 与 12。`:1041-1051` 修正灰度源矩阵只使用白点首列，
  避免灰度亮度被重复三次映射。实现继续复用 `XColorSpace` 的原色矩阵和 Bradford
  白点适配，不引入 Qt 私有结构体或平台 API。
- **回归覆盖**：`xgui_regression_test.c:11499-11557` 的颜色空间测试覆盖 Gamma、
  ProPhoto RGB、BT.2020、HLG、PQ 转换及灰度到 RGB 的亮度保持；默认配置目标构建、直接回归和
  CTest（1/1）均通过。`build-crop-plugin-off` 的目标构建、直接回归和 CTest（1/1）
  亦通过，证明色彩空间代码与图像插件关闭配置兼容；`git diff --check` 通过。
- **近似边界**：当前路径仍针对 `XImage` 的 8 位像素和有限的 `XColorSpace` 原色/
  传递函数描述，不覆盖 Qt `QColorTransferTable` 的 ICC LUT、ICC profile 序列化、
  `QColorTransform` 浮点/16 位格式及完整 HDR 元数据传播。浮点异常、第三方色彩
  管线和平台色彩管理仍不在 Src/XGui 范围内；构建保留仓库既有警告，未运行
  Valgrind/LSan，不能宣称全局无泄漏。

### 10.387 2026-08-31 QMovie.supportedFormats 动画能力过滤

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qmovie.cpp:997-1015`
  先取得 `QImageReader::supportedImageFormats()`，再用只读空 `QBuffer` 构造
  `QImageReader`，调用 `supportsOption(QImageIOHandler::Animation)`，通过
  `removeIf` 删除不支持动画的格式。该实现使 `QMovie::supportedFormats()` 返回
  的不是所有图片格式，而是实际具备动画读取能力的格式集合。
- **实现范围**：`Src/XGui/Graphics/XMovie.c:406-445` 新增动画能力过滤，遍历
  `XImageReader_supportedImageFormats()` 后仅保留支持 `Animation` 的格式，并在
  结果上执行排序、去重；`Src/XGui/Graphics/XImagePluginRegistry.h:150-157`
  与 `XImagePluginRegistry.c:784-828` 增加按格式查询读取选项的公共入口，复用
  首个可读插件的静态 handler 能力声明；插件关闭时由 GIF codec 的动画开关提供
  同等便携回退。相关公共声明和新增逻辑均使用中文注释、项目内存接口和 C99，
  不引入平台 API。
- **回归覆盖**：`xgui_regression_test.c:11161-11172` 验证 `gif` 保留、静态
  `bmp` 过滤，并继续覆盖 GIF 的帧数、循环次数和播放路径。
- **验证结果**：默认配置 `XGuiRegression_Test` 目标构建和直接回归通过；
  `build-crop-plugin-off` 目标构建、直接回归及 CTest（1/1）通过。构建日志仍有
  仓库既有 XClass 函数指针兼容、const 丢弃及第三方 zlib 警告，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：Qt 使用空 `QBuffer` 作为非空设备探测，而便携注册表接口使用
  `device=NULL` 查询静态能力；因此 Drive 插件的 `create()` 必须允许空设备并
  仅返回能力声明，不得执行实际读写。无法满足该契约的插件会被能力查询跳过。
  缓存、定时器和事件循环语义仍由既有 `XMovie` 实现负责，未在本次改动扩展。

### 10.388 2026-08-31 XMovie 结束后重新播放游标对齐

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qmovie.cpp:468-506`
  的 `QMoviePrivate::_q_loadNextFrame()` 在最后一帧或读取失败后，非暂停
  路径将 `nextFrameNumber` 复位为 `0`、`isFirstIteration` 复位为 `true`、
  `playCounter` 复位为 `-1`，再切换到 `NotRunning` 并发出 `finished()`；
  当前帧仍保留供 `currentImage()` 使用。
- **实现范围**：`Src/XGui/Graphics/XMovie.c:73-85,651-656,686-693,698-705`
  增加仅复位结束播放游标的内部辅助函数，并在有限循环结束及非暂停读取
  失败分支调用；不清除当前帧和 `haveReadAll`，与 Qt 的结束路径区分于
  换源使用的完整 `resetPlayback()`。
- **回归覆盖**：`xgui_regression_test.c:11192-11233` 使用有限单帧 GIF
  夹具验证末帧后进入 `NotRunning`，随后再次 `start()` 从第 0 帧恢复。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建、直接回归
  和 CTest（1/1）通过；构建保留共享工作树中既有 XClass/const/第三方
  警告和预期 XError 诊断。未运行 Valgrind/LSan，不能宣称无泄漏。
- **边界**：C99 `XMovie` 仍以同步 `jumpToNextFrame()` 驱动播放，没有 Qt
  定时器、事件循环和完整 `QMap` 帧缓存；暂停状态的读取失败继续保留
  播放状态，不触发本次结束复位，符合 Qt 条件分支。

### 10.389 2026-08-31 QClipboard 文本写入与 MIME 数据一致性

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qclipboard.cpp:295-302`
  规定 `QClipboard::setText()` 创建新的 `QMimeData`，调用 `setText()` 后再
  交给 `setMimeData()`；因此文本写入后 `mimeData()` 必须可见，且文本读取
  统一从该 MIME 对象取得。`qclipboard.cpp:438-449` 规定 `setMimeData()` 转移
  数据对象所有权，替换同一模式的旧内容。
- **实现范围**：`Src/XGui/Input/XClipboard.c:153-193` 的 `text()` 与
  `text_2()` 在保留裁剪关闭时独立文本存储的同时，新增从
  `XMimeData_hasText()`/`XMimeData_text()` 读取 MIME 文本的路径；`:195-220`
  在 `XMIMEDATA_ON` 开启时改为创建 `XMimeData`、登记纯文本并调用
  `XClipboard_setMimeData()`，从而保留 MIME 所有权、清理旧对象和信号顺序。
  `XMIMEDATA_ON=0` 时继续使用原有 `m_text` 回退，不引入未定义的 MIME 类型。
- **回归覆盖**：`xgui_regression_test.c:15069-15088` 在写入文本后同时断言
  `XClipboard_mimeData()` 非空、含 `text/plain` 且 MIME 文本与 `text()` 一致，
  随后验证 `clear()` 清除两种表示并继续发出变更信号。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建、直接回归和
  CTest（1/1）通过；`build-crop-plugin-off` 的目标构建、直接回归和 CTest
  （1/1）亦通过，随后已重新构建默认目标并恢复默认测试产物。`git diff --check`
  通过。编译仍保留仓库既有 XClass 函数指针兼容、const 丢弃及第三方 zlib
  警告，运行时 XError 诊断为既有信息；本轮未运行 Valgrind/LSan，不能宣称
  零警告或全局无泄漏。
- **边界**：当前 C API 的 `XClipboard_text()` 在无数据时仍返回 `NULL`（Qt
  返回空 `QString`），Selection/FindBuffer 仍是无系统后端的进程内存储且
  `supportsSelection()`/`supportsFindBuffer()` 恒为 false；不模拟平台剪贴板
  的外部内容变化、延迟销毁和 `QMimeData` URL/富文本回退。文本 MIME 对象由
  `XClipboard` 拥有，借用指针在下一次模式内容变化后失效。

### 10.390 2026-08-31 XLabel 双击选词

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp:909-912`
  将键盘事件交给内部 `QWidgetTextControl`；鼠标按下、移动、释放和双击同样
  由该文本控制器处理。其双击路径按 `QTextCursor::WordUnderCursor` 选择当前
  词，并保持 `selectionStart()`/`selectedText()` 的 UTF-16 索引语义。
- **实现范围**：`Src/XGui/Widget/XLabel.c` 新增 `label_isWordUnit()` 与
  `label_selectWordAt()`，以现有布局的 UTF-16 位置为输入，在 ASCII 字母/数字/
  下划线和连续非 ASCII 字符上执行选词，常见 CJK/全角标点作为边界；
  `VXLabel_mouseDoubleClickEvent()` 在左键且启用 `TextSelectableByMouse` 时
  选中当前词、设置焦点并接受事件，否则继续向 `XFrame` 父类分发。实现不引入
  平台 Unicode 数据库，仍使用 XString/XWidget/XPainter 既有接口。
- **回归覆盖**：`xgui_regression_test.c:18157-18191` 新增 `hello world`
  双击夹具，验证 `XWidget_event_base()` 派发双击后 `XLabel_selectedText()`
  返回完整的 `hello`，并清理堆栈事件与选区。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建和直接运行均通过，
  输出 `XGui regression tests passed`；运行中的 XError 为既有无效瞬态父窗口与
  栈对象诊断。构建保留仓库既有 XClass 函数指针兼容、const 丢弃及第三方 zlib
  警告；未运行 Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：词字符分类是无平台 Unicode 表的便携子集，未覆盖 Qt
  `QTextBoundaryFinder` 的完整语言规则、表情序列与复杂脚本；富文本链接双击
  仍由单击链接路径处理，编辑、上下文菜单和可访问性文本控制未在本次扩展。

### 10.391 2026-09-01 PPM 数值扫描与 Qt 分隔符/溢出行为

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qppmhandler.cpp:35-76`
  的 `read_pbm_int()` 在设备流上逐字符读取整数，允许任意长度数字串；超过
  `INT_MAX` 后返回 `-1` 但不把 `ok` 置为 false，数字后的第一个非数字字符由
  当前调用消费，`#` 则消费到换行。`:78-103` 的头部解析复用该规则，并在
  宽高/最大值校验后限制 32767/65535；`:111-172` 的正文继续按已消费的分隔符
  位置读取原始字节或 ASCII 样本。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecPpm.c:26-174`
  删除固定 64 字节令牌和 `strtoul` 风格解析，新增不分配内存的
  `ppm_readQtInt()`，统一服务宽度、高度、最大值及 P2/P3 ASCII 样本；P1 以
  `maxDigits=1` 保留 Qt 的逐位读取，超长数字按 `-1` 窄化，数字后的任意非数字
  分隔符与 `#` 注释按 Qt 消费，二进制 CRLF 的 LF 仍留作首个样本字节。解析继续
  受输入 `size`、32767 尺寸和 65535 最大值约束，不使用标准堆分配或平台 API。
- **回归覆盖**：`xgui_regression_test.c:9191-9196,9344-9363` 新增 PGM 超长
  溢出数字和 `1x2` 任意分隔符夹具，分别断言 Qt 的 `0xca` 窄化结果及两个灰度
  样本均被消费；既有 P1-P6、注释、截断正文、子类型和读取器发现测试继续覆盖
  注册与实际解码路径。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 增量构建、直接回归和
  CTest（1/1）通过，`git diff --check` 通过。构建日志仍保留仓库既有 XClass
  函数指针兼容、const 丢弃和第三方 zlib 警告；运行时 XError 为既有诊断，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：实现复刻 Qt 当前 ASCII/原始 PPM 读取顺序，但仍以内存缓冲区为输入，
  不覆盖 `QIODevice` 的短读/阻塞行为；CRLF 原始数据首个 LF 作为样本的结果是 Qt
  兼容边界。最大值、尺寸和总字节数的防溢出检查属于嵌入式裁剪约束。

### 10.392 2026-09-01 XIcon 平台主题默认路径与名称

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:49-78`
  将系统主题名、主题搜索路径和独立回退路径交给
  `QPlatformTheme`，并在 `qiconloader.cpp:90-103` 初始化时懒加载；
  `qiconloader.cpp:151-200` 规定清除用户主题后恢复系统路径，默认主题路径
  始终追加 `:/icons`，回退路径单独查询；`qiconloader.cpp:612-628` 规定
  主主题、回退主题、独立回退文件的查找顺序。
- **实现范围**：`Src/XGui/Platform/XPlatformTheme.h:25-40` 增加平台默认图标
  路径/名称钩子，公共 `Src/XGui/Icon/XIcon.c:101-132,1471-1484,
  1507-1517,1539-1554,1586-1601` 仅负责调用 Drive、复制字符串列表和
  保留用户显式设置；用户列表非空时完全优先，空列表才查询平台并追加
  `:/icons`。`Drive/Posix/Graphics/XPlatformTheme_posix.c:8-111` 按
  `QT_QPA_SYSTEM_ICON_THEME`、`XDG_ICON_THEME`、`XDG_ICON_FALLBACK_THEME`
  和 `XDG_DATA_HOME`/`XDG_DATA_DIRS` 派生 icons/pixmaps 路径；Windows 与
  Unsupported Drive 提供返回 false 的空钩子，避免公共层链接未定义符号。
- **回归覆盖**：`xgui_regression_test.c:526-580` 的
  `test_icon_platform_theme_defaults()` 临时注入系统主题覆盖和 XDG 数据目录，
  验证默认主题名及 `/tmp/xgui-platform-data/icons` 路径，并恢复原环境与
  XIcon 全局状态；原有主题继承、缓存、回退和 `:/icons` 用例继续执行。
- **验证结果**：默认 `build` 的 `XinYueCS`、`XGuiRegression_Test` 构建，
  直接回归及 CTest（1/1）通过；`build-crop-plugin-off` 的目标构建、直接回归
  和 CTest（1/1）也已通过，随后已恢复默认测试产物。日志仍包含仓库既有 XClass 函数指针兼容、
  const 丢弃和第三方 zlib 警告；运行时 XError 为既有诊断，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：Posix Drive 没有 Qt `QPlatformTheme`/桌面 D-Bus 对象，只能使用
  XDG 环境和固定 `/usr/local/share`、`/usr/share` pixmaps 目录；不会读取桌面
  设置守护进程，也不模拟 Qt 平台插件动态更新。系统路径不写回用户列表，平台
  变化在下一次默认 getter 读取时重新取值；这保持了 C99 嵌入式层的无平台 API
  约束，并允许 `XPLATFORMINTEGRATION_ON=0` 裁剪后退回 `:/icons`。

### 10.393 2026-09-01 QIcon 主题索引损坏条目的延迟加载语义

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:446-570`
  先以文件存在性建立主题条目，再在 `qiconloader.cpp:766-834` 的取图路径中
  延迟解码；`qiconloader.cpp:976-995` 的 `availableSizes()` 对固定、阈值和
  可缩放目录直接返回 `index.theme` 的声明尺寸，只有独立 Fallback 文件才委托
  `QIcon(file).availableSizes()`。
- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.c:430-470,960-1000,1745-1770,1970-2000`
  现在在索引目录、传统目录和主题根文件中先检查首个支持格式是否存在，再尝试
  解码；存在但损坏的文件不会继续尝试后缀或父主题，候选仍参与主题选择，实际
  取图失败留到 `XPixmap_load()`。`theme_dirHasIcon()` 改为纯存在性查询，使
  `availableSizes()` 保留声明尺寸；独立回退文件仍保持 Qt 的“存在后再解码”规则。
- **回归覆盖**：`xgui_regression_test.c:835-880` 新增当前 Child 主题损坏文件
  覆盖 Base 父主题的夹具，断言引擎保持非空、同名父图标不被替换、声明的 48px
  尺寸仍可查询；原有损坏文件、fallback 和主题继承用例继续覆盖延迟失败路径。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建和直接运行通过，
  CTest（1/1）通过，`git diff --check` 通过。构建仍保留仓库既有 XClass 函数指针、
  const 丢弃和第三方 zlib 警告；运行时 XError 为既有诊断，未运行 Valgrind/LSan，
  不能宣称零警告或全局无泄漏。
- **边界**：主题解析仍受固定 1024 字节路径缓冲区和当前图像编解码裁剪开关约束；
  不模拟 Qt 插件提供的实时主题更新，但每次默认查询都会重新读取平台钩子。

### 10.394 2026-09-01 XPicture 文本绘制独立命令

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine_pic.cpp:443-480`
  的 `QPicturePaintEngine::drawTextItem()` 将文本绘制序列化为独立的
  `PdcDrawTextItem`/`PdcDrawText2` 命令，而不是把字形光栅化结果暴露给调用方；
  命令枚举和回放约束见 `qpicture_p.h:50-58,84-88`。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:66-67,250-266` 新增
  `XPictureOpcode_DrawText` 和带 UTF-8 负载的记录接口；
  `Src/XGui/Graphics/XPicture.c:38-39,282-340,840-890,1950-1970`
  以固定 `x/y/color/length` 头和最多 4096 字节文本保存命令，校验长度、NUL
  嵌入和流边界，回放时复制到临时 NUL 结尾缓冲再调用现有 `XPainter_drawText()`。
  `Src/XGui/Graphics/XPainter.c:4158-4170` 在 Picture 录制模式写入该命令，
  回放标志下走原有点阵绘制，避免递归追加；记录时同步更新字体相关边界矩形。
- **回归覆盖**：`xgui_regression_test.c:5818-5900` 验证 SetFont 后紧邻
  `DrawText` 操作码、流完整性，并将直接图像绘制与 Picture 回放的 32x24 像素
  逐点比较；`xgui_regression_test.c:228-232` 另验证超长文本长度会被流校验拒绝；
  空文本仍按无操作成功处理。
- **验证结果**：默认 `build` 目标构建、直接回归和 CTest（1/1）通过；
  `build-crop-plugin-off` 目标构建及直接回归也通过，随后恢复默认测试产物；构建日志
  继续保留仓库既有 XClass 函数指针、const 丢弃及第三方 zlib 警告，运行时
  XError 为既有诊断。未运行 Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：文本负载使用 C99 UTF-8 字符串和现有点阵字库，不编码 Qt
  `QTextItem` 的字体引擎对象、字形索引、浮点基线或富文本属性；Picture 仍为
  XinYueC 自有便携流，不能读取或生成 Qt 原生 QPicture 二进制格式。

### 10.395 后续对齐路线图（按阶段逐项收敛）

本节记录本轮之后的明确顺序，避免把已覆盖能力与 Qt 私有扩展混为一谈。每个
阶段完成后都要分别验证默认配置、关闭相关功能的嵌入式裁剪配置、直接回归和
CTest，并在本文件追加 Qt 源码行号与实际结果。

1. **图像回归夹具（已完成本轮）**：已补齐畸形 BMP/PPM/XBM、透明 mask、读取器重试、
   Picture 截断和缓存生命周期夹具；优先复现 Qt 在“文件存在但负载损坏”时的
   延迟失败与不回退语义。实现集中在 `xgui_regression_test.c`，不增加冗余公共
   API。
2. **XPainter/Picture 精度收敛（已完成本轮）**：已完成独立 `DrawText` 命令及
   椭圆、弧线、饼形、弦形、圆角矩形五种形状回放夹具。路径裁剪、Qt 原生
   QPicture 二进制兼容和 double 精度状态继续作为明确边界，不以自有便携流
   伪装完全兼容。
3. **QImage 扩展边界（已完成可安全部分）**：当前轻量 API 已覆盖文本元数据、
   预定义/自定义原色、传递函数和三分量矩阵转换；本轮补齐 `RGBA64`、`RGBX64`、
   `Grayscale16` 及预乘变体的原生 16 位通道路径、浮点格式矩阵转换，并增加
   `XImageData` 内部 ICC 原始字节/LUT 侧车及生命周期接口（详见 10.404）。
   侧车仅保存资源，不宣称已实现 Qt ICC parser、mAB/mBA/CLUT 变换。
4. **最终矩阵与清理**：统一检查公共头文件中文参数/返回值注释、删除确认无
   调用方的旧兼容入口、复跑全量构建和裁剪构建；保留项目既有 XClass/const/zlib
   编译告警及无 Valgrind/LSan 证据的事实，不宣称零警告或全局无泄漏。Qt 原生
   QPicture 二进制兼容继续作为明确拒绝边界，不添加冗余占位 API。

### 10.396 2026-09-01 BMP 畸形偏移与 BITFIELDS 掩码边界

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qbmphandler.cpp:255-264`
  对 `BI_BITFIELDS` 只消费 RGB 三个掩码；`:365-367` 仅在 `bfOffBits` 大于
  当前头部游标时执行 seek；`:212-214` 在设备当前游标已到末尾时拒绝读取，
  而 `:373-377,532-535,548-552` 对像素行短读保留已分配的零填充图像。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:303-385,396-417,493-505`
  将非 V4/V5 的 BITFIELDS 掩码消费固定为 12 字节，使用实际 DIB/调色板游标
  计算像素起点，避免畸形偏移回读 BMP 文件头；偏移超过文件尾时只要头部后仍
  有尾字节就保留零填充输出，完全没有像素字节仍拒绝。RLE 和未压缩逐行读取
  均增加无符号溢出与越界保护，未改变正常 BMP 输出。
- **回归覆盖**：`xgui_regression_test.c:10031-10060` 覆盖偏移小于 DIB 头、偏移
  超过文件尾但存在尾字节、无像素字节拒绝；已有 `:9985-10000` 覆盖 32 位
  `BI_BITFIELDS` 掩码语义，另有尺寸、截断行和 RLE 夹具。
- **验证结果**：默认 `build` 的 `XinYueCS`/`XGuiRegression_Test` 目标构建、
  直接回归和 CTest（1/1）通过；`build-crop-plugin-off` 同样目标构建、直接
  回归和 CTest（1/1）通过，随后重新构建默认测试程序。`git diff --check`
  保持通过；日志仍含既有 XClass/const/zlib 告警和 XError 诊断，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：C99 解码仍以内存缓冲区为输入，不模拟 `QIODevice` 短读/阻塞；
  像素偏移过小按当前缓冲游标解释，且不承诺读取/生成 Qt 原生 BMP 私有扩展。

### 10.397 2026-09-01 XPicture 五种形状命令回放夹具

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpaintengine_pic.cpp:338-348`
  为 `drawEllipse`、`drawArc`、`drawPie`、`drawChord` 和 `drawRoundedRect`
  分别写入独立绘图命令；命令参数在 `qpicture_p.h:50-55` 中保持角度与圆角
  半径的独立字段，不能仅用普通矩形替代。
- **回归覆盖**：`xgui_regression_test.c:2773-2843` 新增五种形状的 Picture
  记录夹具，使用 1/16 度角度单位和 3 像素圆角半径；直接绘制与回放均使用
  相同的红色画笔、蓝色画刷，并逐像素比较 96x16 画布，验证所有 `DrawShape`
  负载参数在回放中保留。
- **验证结果**：默认 `build` 的 `XGuiRegression_Test` 目标构建、直接回归和
  CTest（1/1）通过；`build-crop-plugin-off` 的目标构建、直接回归和 CTest
  （1/1）亦通过，随后已重新构建默认测试程序。日志继续保留项目既有
  XClass/const/zlib 告警和 XError 诊断，未运行 Valgrind/LSan，不能宣称零警告
  或全局无泄漏。

- **边界**：形状仍通过 XinYueC 自有 `DrawShape` 便携记录，不生成 Qt 原生
  `QPicture` 二进制命令；无绘制后端时维持既有失败语义，角度采样精度由当前
  C99 折线回退实现决定。

### 10.398 2026-09-01 阶段矩阵收尾与剩余边界

- **已完成阶段**：图像处理器发现/注册、PPM/PBM/PGM、XBM、BMP 畸形输入与
  短读边界；图标主题索引/回退搜索路径；`XPicture` 文本及五种形状命令；
  默认配置和 `XIMAGEIOPLUGIN_ON=0` 裁剪配置均完成目标构建、直接回归和
  CTest。
- **最终验证**：本轮修改后执行 `cmake --build build -j1`，默认工程全部目标
  构建成功；`./bin/XGuiRegression_Test` 通过，`ctest --test-dir build
  --output-on-failure` 为 1/1 通过；`git diff --check` 无输出。裁剪配置的
  同等结果已在 10.396/10.397 记录，默认测试程序已重新构建恢复。
- **剩余工作**：QImage ICC profile 的语义解析/颜色变换以及 Qt 原生 QPicture
  二进制兼容仍受资源格式和跨平台后端约束；ICC 原始字节与逐通道 LUT 的安全
  侧车保存已在 10.404 完成，浮点色彩变换的 C99 标量路径也已在 10.400 完成，
  当前不再把资源保存列为未实现项；
  Qt 依据为 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolorspace.h:109-118,137-138`
  和 `qcolorspace.cpp:859-906,1119-1148`；当前
  `Src/XGui/Graphics/XImage.c:1088-1390` 已对原生
  `Grayscale16/RGBX64/RGBA64/RGBA64_Premultiplied` 保留 16 位通道，并对
  六种浮点格式保留浮点通道；`XImage_convertColorSpacePixels():1455-1518`
  覆盖两条原生分支，其余不满足条件的转换仍会经 `XImage_pixel()` 窄化为 8 位，
  Qt 对应原生通道路径见 `qimage.cpp:5199-5290`。在引入有所有权的字节/LUT
  侧车不参与现有颜色转换；在引入 ICC 解析器前，继续保留无效空间安全失败和
  文档化边界，不增加冗余占位 API。项目既有
  XClass/const/zlib 编译告警、运行时 XError 诊断及无 Valgrind/LSan 证据的事实
  仍然有效。

### 10.399 2026-09-01 QImage 原生 16 位色彩转换路径

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5199-5290`
  在 `QImage::convertedToColorSpace()` 中对 `QRgba64`、`Grayscale16` 等高位
  格式保留原生通道，不能先通过 8 位 `pixel()` 再写回；传递函数和矩阵接口由
  `qcolorspace.h:109-118`、`qcolorspace.cpp:859-906,1119-1148` 描述。
- **实现范围**：`Src/XGui/Graphics/XImage.c:1088-1243` 增加 16 位通道读取、
  预乘 Alpha 反解、矩阵/传递函数浮点中间值和 16 位量化写回；
  `XImage_convertColorSpacePixels():1455-1486` 在源/目标均为
  `Grayscale16`、`RGBX64`、`RGBA64` 或 `RGBA64_Premultiplied` 时走该路径，
  其它格式维持既有 8 位兼容行为。未新增公共占位接口，写回仍复用项目已有
  `XImage_writePixelColor16()` 和统一内存/错误处理。
- **回归覆盖**：`xgui_regression_test.c:12364-12407` 以非整字节对齐的
  `RGBA64` 样本转换为线性 sRGB，并与旧 8 位兼容结果比较，确认原生路径保留
  低位通道且 Alpha 不变。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 均完成目标构建；两套
  `XGuiRegression_Test` 直接运行及 CTest 均通过，随后恢复默认测试产物。
  `git diff --check` 保持通过。构建日志仍含仓库既有 XClass/const/zlib 告警，
  运行时 XError 为既有诊断；未运行 Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：ICC 原始字节和逐通道 LUT 已可安全保存，但尚未接入 ICC parser、
  mAB/mBA/CLUT 变换或半精度目标的完整 Qt SIMD 优化；Qt 原生 QPicture 二进制
  兼容仍需独立资源格式和跨平台后端设计。浮点矩阵转换已覆盖 C99 标量路径，
  当前保持无效色彩空间安全失败。

### 10.400 2026-09-01 QImage 浮点通道色彩转换路径

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5268-5290`
  对 `RGBA16FPx4`/`RGBA32FPx4` 选择浮点扫描线并直接调用
  `QColorTransformPrivate::apply()`；格式定义及预乘规则见
  `qimage.h:73-74`、`qimage.cpp:754-756`。
- **实现范围**：`Src/XGui/Graphics/XImage.c:1247-1390` 增加半精度/单精度
  通道读取、预乘 Alpha 反解、HDR 范围浮点矩阵转换和原生写回；
  `XImage_convertColorSpacePixels():1489-1518` 在目标为六种浮点格式时接受
  浮点、16 位整数以及普通打包/索引源，统一提升到浮点中间值，避免
  `XImage_pixel()` 的 8 位窄化，并按 Qt `mapExtended()` 语义保留浮点负值与
  超过 1 的 HDR 值。目标为原生 16 位时，`XImage_convertColorSpacePixels():1459-1486`
  同样接受浮点源并只在最终写回时量化到 16 位。
  半精度使用现有 `XImage_floatToHalf()`，单精度按主机字节序直接保存，未新增
  公共 API。
- **回归覆盖**：`xgui_regression_test.c:12414-12533` 使用非整字节对齐的
  `RGBA32FPx4` 样本转为线性 sRGB，检查线性化结果、浮点差异和 Alpha 保持，
  并覆盖混合格式夹具。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 均完成
  `XGuiRegression_Test` 目标构建，直接回归及 CTest（1/1）通过，随后恢复默认
  测试产物；`git diff --check` 通过。构建仍保留既有 XClass/const/zlib 告警，
  未运行 Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：当前为可移植 C99 标量实现，不包含 Qt 私有 SIMD、ICC parser、
  LUT 插值和 Qt 原生 QPicture 二进制兼容；ICC/LUT 原始资源由 10.404 侧车
  保存。目标半精度按 16 位浮点量化，因而不承诺超过半精度可表示范围的额外
  有效位。

### 10.401 2026-09-01 QImage 混合精度颜色转换

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5199-5290`
  先按源/目标格式选择 `QRgba64`、`QRgbaFloat32` 或兼容中间格式，再执行
  `QColorTransform`；不能因为一端是普通 8 位格式就把另一端的高精度通道
  先压缩到 8 位。
- **实现范围**：`XImage_readNativeFloat()` 对非高精度源复用
  `XImage_readPixelValue()` 的 ARGB 语义，`XImage_convertColorSpacePixels()`
  在浮点目标和 16 位目标分支中分别接受打包/索引、16 位和浮点源；Alpha
  保持独立通道，浮点到整数只在 `XImage_colorSpaceChannel16()` 处量化。
- **回归覆盖**：`xgui_regression_test.c:12414-12533` 增加 ARGB32 到
  `RGBA32FPx4` 的提升夹具及 `RGBA32FPx4` 到 `RGBA64` 的直接写回夹具，检查
  线性化结果、Alpha 保持和非 8 位中间值。
- **验证结果**：默认 `build`、`build-crop-plugin-off` 均完成目标构建；两套
  `XGuiRegression_Test` 直接运行及 CTest（1/1）通过，随后恢复默认测试产物。
  `git diff --check` 通过；仍保留既有 XClass/const/zlib 告警，未运行
  Valgrind/LSan，不能宣称零警告或全局无泄漏。
- **边界**：ICC/LUT 资源已由 10.404 侧车保存，但尚未接入 profile parser、
  Qt 私有 SIMD 或原生 QPicture 二进制兼容；普通源提升到浮点时只能保留其
  原始 8 位信息，这是源格式本身的精度上限。

### 10.402 2026-09-01 QImage 色彩变换写时复制语义修正

- **Qt 依据**：Qt 6.8 `QImage::detach()` 在
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:1124-1136`
  明确要求共享数据在写入前复制；`detachMetadata()`（同文件
  `1147-1159`）仅用于元数据变更。颜色变换实现
  `qimage.cpp:5199-5290,5467-5540` 先建立目标图像，再把变换写入目标，不能
  反向修改源图像。
- **问题与修复**：`XImage_applyColorTransform()` 原先通过
  `XImage_copy_base(out, self)` 共享像素数据后，直接进入浮点/16 位写回分支；
  当源和目标格式相同，目标没有经过格式转换，导致写回同时修改源图像。现于
  `Src/XGui/Graphics/XImage.c:1755-1761` 在目标对象与源对象不同的情况下调用
  `XImage_detach(out)`，保持 COW 独立写入；原地转换仍按 Qt 语义使用同一对象并
  允许修改自身。未增加公共接口或额外资源分配。
- **回归覆盖**：`xgui_regression_test.c:12414-12533` 的浮点夹具新增源快照断言，
  在执行 `RGBA32FPx4 -> 线性 sRGB` 后逐通道确认源仍为
  `{0.12345, 0.23456, 0.34567, 0.75}`；同时验证直接
  `RGBA32FPx4 -> RGBA64` 保留 16 位量化范围，防止回归到 8 位中间路径。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 均完成
  `XinYueCS`/`XGuiRegression_Test` 目标构建、直接回归和 CTest（1/1）通过；
  裁剪构建完成后重新构建默认测试产物。`git diff --check` 无输出。日志仍保留
  项目既有 XClass/const/zlib 编译告警和 XError 诊断；Valgrind 不可用且受控环境
  LSan 受 ptrace 限制，不能宣称零警告或全局无泄漏。
- **边界**：当前仍是便携 C99 标量路径，ICC/LUT 仅由 10.404 侧车保存，尚未
  接入 Qt 私有 parser、SIMD 或原生 QPicture 二进制兼容；高精度 `RGBA64`/浮点
  格式的行为已覆盖，不再把 COW 源修改列为近似项。

### 10.403 2026-09-01 剩余模块安全边界审计

- **审计范围**：对照 Qt 6.8 `QColorSpace::fromIccProfile()/iccProfile()`（
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qcolorspace.cpp:859-906,1119-1148`）
  和 `QPicture::checkFormat()/load()/save()`（
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qpicture.cpp:982-1045,228-287`），
  复核当前 C99 值复制、写时复制和自有 Picture 便携流的所有权边界。
- **结论**：`XColorSpace` 继续保持无堆资源值类型；本轮已在 `XImageData` 增加
  不可变、引用计数的 ICC/LUT 内部侧车，完整覆盖复制、COW 分离、色彩空间变更
  清理和失败回滚，但不添加裸指针 getter 或不完整 profile 解析接口。侧车保存
  不改变现有颜色转换结果，真实 ICC 解析仍需独立实现。`XPicture` 继续拒绝 Qt
  原生 `QPIC` 二进制，不伪装成兼容格式；后续若要兼容，必须先定义字节序、
  QDataStream 版本、图像/字体资源映射和失败回滚策略。
- **当前验证**：`cmake --build build -j1`、`./bin/XGuiRegression_Test`、
  `ctest --test-dir build --output-on-failure` 均通过；`git diff --check` 通过。
  `xgui_regression_test.c:2513-2525` 另验证 Qt 原生 `QPIC` 头在当前便携流中保持
  非空可见但被明确拒绝；同一夹具在 `build-crop-plugin-off` 裁剪配置下也通过。
  `Src/XGui` 未发现直接调用标准 `malloc/calloc/realloc/free` 的代码。既有
  XClass/const/zlib 警告、运行时 XError 诊断及未具备 Valgrind/LSan 证据的事实
  继续保留，不能据此宣称零警告或全局无泄漏。
- **后续顺序**：ICC/LUT 侧车及生命周期夹具已在 10.404 完成；Qt 原生 QPicture
  仅保留独立拒绝夹具，不实现最小可读命令子集。若未来要互操作，必须先定义
  有所有权的资源表和版本策略；任何不安全或无法覆盖 Qt 语义的接口继续保持
  未公开。

### 10.404 2026-09-01 ICC/LUT 资源侧车与生命周期

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qcolorspace.cpp:694-775`
  和 `832-906` 定义自定义色彩传递表及逐通道 LUT；`qcolorspace.cpp:1119-1148`
  要求 `iccProfile()` 保留原始 ICC 字节；LUT 单调性、位宽和逆向查找规则见
  `qcolortransfertable_p.h:28-128`。这些资源由 Qt 私有 `QColorSpacePrivate`
  持有，不能直接塞入按值复制的公开色彩空间结构。
- **实现范围**：`Src/XGui/Graphics/XImage.c:34-173` 新增不可变、引用计数的
  `XImageColorProfileResource`，支持最多 16 MiB ICC 原始字节以及每通道 8/16
  位、2..65536 元素的 LUT，并在双向表设置时执行单调性校验；所有分配释放统一
  使用 `XMalloc_System/XFree_System`，ICC 通过 `XByteArray` 深拷贝。`XImageData`
  在 `copyMetadata()/clone()/unref()` 中分别 retain/release，`setColorSpace()`
  变更值空间时清除当前侧车，保持 XColorSpace 公共 ABI 不变。
- **内部接口**：`Src/XGui/Graphics/XImageCodec/XImageCodecInternal.h:31-101`
  增加 `XImageColorProfileSpec` 及 `setColorProfile/copyIccProfile/copyLut`，输入
  指针仅在调用期间借用，写出接口始终复制数据，不暴露裸指针，也不宣称执行 ICC
  解析或在颜色变换中应用 LUT。空规格仅用于清除；非法位宽、元素数、空指针和
  非单调双向表保持原图像不变并返回失败。
- **回归覆盖**：`xgui_regression_test.c:12557-12670` 覆盖 ICC 字节复制、独立
  RGB/G 通道 LUT、16 位双向表、缓冲区不足、图像复制共享、COW 修改隔离、变更
  色彩空间清除侧车、非法非单调表以及空规格清理。
- **验证结果**：默认 `build` 的 `XinYueCS` 与 `XGuiRegression_Test` 已构建，
  直接回归通过；`build-crop-plugin-off` 已完成同样的目标构建、直接回归及
  CTest，随后重新构建默认测试产物。`git diff --check` 无输出；项目既有
  XClass/const/zlib 编译告警和 XError 诊断仍存在，Valgrind/LSan 证据不可用，
  因此不宣称零警告或全局无泄漏。
- **边界**：侧车只保存原始 ICC/LUT，不实现 ICC parser、mAB/mBA/CLUT 变换、
  Qt 私有 SIMD 或 LUT 插值；颜色转换仍使用现有 XColorSpace 值语义。后续若需要
  真实 profile 转换，必须先设计受控的 profile 解析和失败回滚策略。

### 10.405 2026-09-01 QImage 公共 API 覆盖与有意裁剪

- **审计依据**：逐项对照 Qt 6.8 `QImage` 声明
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.h:85-340`，并复核
  `XImage.h:189-992` 的 C99 公共入口。尺寸/格式/像素访问、颜色表、Alpha
  掩码、缩放/镜像、文本元数据、DPM/DPR/偏移、设备读写、格式转换、色彩空间
  转换和 `fromData` 均有对应接口；`XImage_copyRect(rect=NULL)` 覆盖 Qt
  `copy(QRect())` 的整图深拷贝语义，`XImageFormat_toPixelFormat` /
  `XImageFormat_toImageFormat` 覆盖静态格式映射。
- **有意裁剪**：C99 不重复暴露 C++ 运算符 `operator==/!=`、`QVariant` 转换、
  右值限定重载以及 `QImage::swap`；调用方使用现有 `XImage_copy_base`、
  `XImage_copyRect` 与显式比较即可。Windows 专用 `HICON/HBITMAP` 辅助留在
  `Drive/windows` 平台边界，未在 `Src/XGui` 增加不可移植占位函数。Qt 原生
  `QPicture` 数据流仍按 10.403 明确拒绝，不通过伪造的最小子集宣称兼容。
- **清理结果**：未发现确认无调用方的旧兼容入口；没有为保持“函数数量一致”而
  添加冗余别名。公共头文件的新增/修改函数均保留中文用途、参数和返回值说明，
  所有内部资源分配继续走项目内存接口。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 的 `XGuiRegression_Test`
  目标、直接回归和 CTest 均通过；默认测试产物已恢复。`git diff --check` 通过，
  既有 XClass/const/zlib 警告和无 Valgrind/LSan 证据的事实保持不变。
- **后续边界**：若未来需要 ICC 语义转换，按 10.404 先实现受控 parser、失败
  回滚和 LUT 插值测试；若需要 Qt Picture 互操作，先定义 QDataStream 版本、
  字节序及图像/字体资源表，再单独立项，不在当前 C99 API 中预留空壳。

### 10.406 2026-09-01 PPM/XBM 编解码器裁剪回归

- **验证范围**：使用既有 `build-crop-ppm-off` 与 `build-crop-xbm-off` 配置分别
  关闭 PPM、XBM 处理器，重新构建 `XGuiRegression_Test` 并直接运行；两套配置
  均通过，确认格式开关关闭时不会留下未解析的处理器注册或链接依赖。
- **恢复状态**：裁剪二进制验证后重新构建默认 `build` 的
  `XGuiRegression_Test`，当前 `bin/XGuiRegression_Test` 为默认配置产物；默认
  直接回归和 CTest 同样通过。构建日志中的 XClass/const/zlib 告警及运行时
  XError 诊断仍为仓库既有项，未运行 Valgrind/LSan。

### 10.407 2026-09-01 PPM 公共格式列表与 raw 子类型

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers_p.h:66-85`
  的内置格式表只枚举 `ppm`、`pgm`、`pbm` 三个规范键；
  `qimagewriter.cpp:170-173` 与 `qimagereader.cpp:282-284` 则把
  `ppmraw/pgmraw/pbmraw` 作为显式 `setFormat()` 可接受的子类型，而非公共
  `supportedImageFormats()` 条目。
- **修正内容**：`XImageReader`、`XImageWriter` 和内置插件元数据现在只枚举
  三个规范 PPM 键，MIME 反查也只返回对应规范键；codec facade 及处理器仍
  接受全部六种 PPM 子类型，保持显式读写兼容并消除冗余公共别名。
- **回归与验证**：新增默认列表及 MIME 反查断言，确认 raw 别名不被枚举；
  默认 `build` 的 `XGuiRegression_Test` 目标、直接运行和 CTest 均通过，
  `git diff --check` 通过。既有编译告警、XError 诊断及无 Valgrind/LSan 证据
  的边界保持不变。

### 10.408 2026-09-01 读写器错误状态、静态探测窗口与列表去重

- **Qt 依据**：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:497-552`
  规定处理器创建失败统一设置 `UnsupportedFormatError`，而设备打开失败的
  `DeviceError/FileNotFoundError` 不应被覆盖；`qimagereader.cpp:1432-1437`
  要求 `supportsOption()` 复用同一初始化路径。`qimagereaderwriterhelpers.cpp:84-143`
  使用字节级大小写敏感的 `QByteArray` 去重；SVG 处理器的有限头部探测窗口为
  `qsvgiohandler.cpp` 对应 `QSvgTinyDocument::hasSvgHeader()` 的 4096 字节上限。
- **修正内容**：`XImageReader_ensureHandler()` 在无更早错误且最终没有处理器时
  设置 `UnsupportedFormatError`，使显式未知格式的 `canRead()/supportsOption()`
  与 Qt 一致；文件/设备错误保持原状态。读写器格式及 MIME 列表合并改为
  `XChar_CaseSensitive` 去重，允许插件按 Qt 规则保留大小写不同的键。静态
  `imageFormat(QIODevice*)` 在无插件回退探测时改为窥视 4096 字节，以识别带有
  较长 XML 声明或注释前缀的 SVG。
- **回归与验证**：新增显式未知格式 `canRead()` 错误断言；默认 `build` 目标构建、
  直接 `XGuiRegression_Test`、CTest 及 `git diff --check` 均通过。默认产物已在
  裁剪验证后恢复。既有 XClass/const/zlib 编译告警、运行时 XError 诊断以及
  无 Valgrind/LSan 证据的内存边界仍如实保留。

### 10.409 2026-09-01 XPM 编解码、完整颜色表与 Qt 调色板排序

- **Qt 依据**：\`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qxpmhandler.cpp:54-724\`
  定义 657 项 X11 颜色名表；\`qxpmhandler.cpp:812-834\` 规定 XPM 头部尺寸、
  调色板数和每像素字符数的范围；\`qxpmhandler.cpp:838-980\` 规定颜色字段
  \`c/g/g4/m\` 查找、\`None\` 透明色、Indexed8 与 RGB32/ARGB32 选择、像素行
  截断填零和 ImageMagick 尾部 Alpha 截断；\`qxpmhandler.cpp:1038-1067\`
  定义 64 字符键生成及倒数第二位历史字符交换；\`qxpmhandler.cpp:1071-1142\`
  使用颜色值有序 \`std::map\` 写出调色板，同时保留像素首次出现时分配的索引；
  \`qxpmhandler.cpp:1177-1249\` 规定 \`canRead\`、\`Name/Size/ImageFormat\` 选项。
- **实现范围**：新增 \`XImageCodecXpm.c\`，支持 XPM C 源码字符串提取、头部边界、
  1..15 字符像素键、完整 657 项 X11 颜色名、十六进制颜色、\`None\` 透明色、
  <=256 色 Indexed8 与大调色板 32 位图像；编码器按首见顺序分配键索引、按颜色值
  排序输出调色板，准确复现 Qt 的历史键交换。内置插件、
  \`XImageReader\`/\`XImageWriter\`、格式探测和 \`XIMAGECODEC_XPM_ON\` 裁剪均已接入，
  未增加旧别名或冗余公共入口。
- **回归覆盖**：\`xgui_regression_test.c:test_codec_xpm\` 验证格式发现、尺寸与
  ImageFormat 探测、\`None\`/完整 X11 颜色表、Indexed8 索引、编码头部和有序
  调色板、编码后再次解码、非法尺寸拒绝，以及读写器文件设备路径。
- **验证结果**：默认 \`build\` 的 \`XGuiRegression_Test\` 目标、直接回归和 CTest
  均通过；\`build-crop-xpm-off\`（关闭 XPM）和重新配置后的
  \`build-crop-plugin-off\`（关闭插件、保留内置 XPM）目标均构建并直接回归通过；
  默认测试产物已恢复。\`git diff --check\` 通过。构建中的 XClass/const/zlib
  既有告警与运行时 XError 诊断仍存在；Valgrind/LSan 不可用，未宣称零警告或
  全局无泄漏。
- **边界**：保持 Qt \`QXpmHandler\` 的单设备读取模型，不实现 Qt 私有
  \`QVariant\`/C++ 流接口；未知 X11 名称按 Qt \`value_or(0)\` 回退不透明黑色；
  写出时遵循 Qt 的源格式转换规则：Indexed8/Mono/高精度等非
  RGB32、ARGB32、ARGB32_Premultiplied 源按 RGB32 语义写出，Alpha 不透明化；
  仅上述三种 32 位格式保留透明像素。XPM 与 SVGZ/gzip 属于独立处理器，
  Qt 原生 QPicture 互操作仍按后续章节明确拒绝。

### 10.410 2026-09-01 最终默认/裁剪构建矩阵

- **全量构建**：默认 `build`、关闭 XPM 的 `build-crop-xpm-off`、关闭插件的
  `build-crop-plugin-off` 均执行 `cmake --build <dir> -j1` 并在 100% 完成；
  两个裁剪配置同时覆盖 `XGuiRegression_Test`、`XGuiXdnd_Test`、
  `XGuiWindowDemo_Test` 等目标，确认开关不会造成链接缺失。
- **回归**：默认配置恢复后，`./bin/XGuiRegression_Test` 与
  `ctest --test-dir build --output-on-failure` 均通过（1/1）；XPM 及插件关闭
  配置的回归目标和直接运行同样通过。运行中出现的无效窗口参数及
  `XClass_deinit_base` 诊断是既有边界检查，不影响断言结果。
- **收口检查**：`git diff --check` 无输出；`Src/XGui` 未发现直接调用
  `malloc/calloc/realloc/free`。仓库仍有既存 XClass 函数指针、const 丢弃和
  zlib 条件编译警告；当前环境没有可用 Valgrind/LSan 证据，故不宣称零告警或
  全局无泄漏。默认 `bin/XGuiRegression_Test` 已在裁剪验证后重新生成。

### 10.411 2026-09-01 SVGZ gzip 编解码对齐

- **Qt 依据**：Qt 6.8 `qsvgiohandler.cpp:59-97` 对 `svgz` 输入读取完整设备
  内容并交给 SVG 渲染器；`qsvgtinydocument.cpp:80-150,179-210` 使用
  `inflateInit2(MAX_WBITS + 16)` 解压 gzip，并在流结束后继续处理串联 member；
  `qsvgtinydocument.cpp:549-590` 的 `isLikelySvg()` 将 gzip SVG 作为可识别格式，
  具体 XML 合法性留给读取阶段。
- **实现范围**：`XImageCodecSvg.c` 增加项目内存接口驱动的有界 gzip 解压器，支持最多
  64 MiB 压缩输入、64 MiB 解压输出和 16 个串联 member，所有重分配失败均回收原块；
  普通 SVG 解码、默认尺寸探测均在解压后复用既有 UTF-8/16/32 与矢量/PNG/纯色路径。
  `XImageCodec_detect()` 在 gzip magic（`1f 8b`）处返回 SVG，实际损坏数据仍在
  `decode()` 阶段失败，保持 Qt `canRead` 与 `read` 分层语义。配置注释同步列出
  SVG/SVGZ，未增加冗余公共 API。
- **回归与验证**：`test_codec_svg_gzip` 使用本地 zlib（`MAX_MEM_LEVEL=6`）生成真实
  gzip 夹具，验证 magic 自动探测、`XImageCodecInternal_probeSvgSize` 返回 3x2、
  显式 SVG 解码及像素值；默认 `build` 的回归目标、直接运行和 CTest 通过。
  后续完整构建矩阵仍覆盖 `build-crop-xpm-off` 与 `build-crop-plugin-off`，以确认
  SVGZ 代码不依赖已裁剪的 XPM/插件路径。
- **边界**：仅接受 gzip 封装（不把 zlib wrapper 或裸 deflate 当作 SVGZ）；压缩输入、
  输出和 member 数量超限按失败处理；自动探测仅依据 gzip magic，恶意或截断 gzip
  可能先被标为 SVG、随后由解码器拒绝。Qt 的外部资源加载、动画脚本和完整 SVG
  标准仍不在嵌入式轻量渲染器范围内。

### 10.412 2026-09-01 PNG 色彩元数据与 ICC 侧车读写

- **Qt 依据**：Qt 6.8 `qpnghandler.cpp:435-490` 读取 `iCCP`、`sRGB`、`gAMA`
  和 `cHRM`，并按 `iCCP > sRGB > gAMA+cHRM` 的优先级设置 `QColorSpace`；
  `qpnghandler.cpp:810-827` 在写出时优先写入非空 ICC profile；
  `qcolorspace.cpp:859-906,1119-1148` 规定 ICC 原始字节即使无法解析也应可回读。
- **实现范围**：`XImageCodecPng.c:48-175` 增加有界 iCCP zlib 解压和 ASCII profile
  名称写出，最大 profile 为 16 MiB，所有临时块均由项目内存接口分配并在失败
  路径回收；`XImageCodecPng.c:461-495` 解析 `iCCP/sRGB/gAMA/cHRM`，在
  `:867-899` 设置颜色空间并把 ICC 原始字节写入 `XImageColorProfileResource`
  侧车。编码路径 `:970-979,1035-1040` 在 IDAT 前写入 iCCP，普通 PNG 无侧车时
  字节布局保持不变。未增加公共 API，也不把非法 profile 伪装成有效空间。
- **回归覆盖**：`xgui_regression_test.c:11215-11430` 构造 gAMA/cHRM、sRGB 覆盖、
  iCCP 解压及编码往返夹具，检查 Gamma 约 2.2、原色坐标、原始 profile 字节和
  `iCCP` 块可见性；默认配置以及 `build-crop-xpm-off`、`build-crop-plugin-off`
  的目标构建和直接回归均通过，随后恢复默认测试程序，CTest 为 1/1 通过。
- **边界**：当前 iCCP 仅保存原始 profile，不实现 ICC parser、mAB/mBA/CLUT
  颜色变换或 Qt 私有 SIMD；iCCP 解压错误按无效 profile 忽略并继续读图。PNG
  文本块（tEXt/zTXt/iTXt）仍不在本节范围，图像对象自身的文本元数据 API 已按
  10.405 覆盖；项目既有 XClass/const/zlib 告警、XError 诊断及无 Valgrind/LSan
  证据的事实继续保留。

### 10.413 2026-09-01 PNG 文本元数据 tEXt/zTXt/iTXt 对齐

- **Qt 依据**：Qt 6.8 `qpnghandler.cpp:364-392` 在 `png_read_info` 和
  `png_read_end` 两阶段调用 `png_get_text`，以 Latin-1 读取关键词与传统文本，
  以 UTF-8 读取 iTXt，并按键值顺序写入 `QImage::setText`；
  `qpnghandler.cpp:668-730` 的 `set_text` 将关键词截断为 79 个 Latin-1 字节，
  小于 40 个字符的值关闭压缩，长值使用 zTXt，包含非 ASCII/控制字符的值使用
  UTF-8 iTXt；libpng `pngrutil.c:1860-2015` 对关键词、压缩方法和块边界执行校验。
- **实现范围**：`XImageCodecPng.c:57-207` 增加项目内存驱动的有界文本解压器和
  `tEXt/zTXt/iTXt` 解析器，关键词限制 1..79 字节、单图文本总量限制 16 MiB、
  条目限制 1024，坏 CRC/非法结构按 Qt/libpng 的 benign ancillary 语义忽略；
  合法值暂存于 `XStringList`，像素与色彩空间完成后通过 `XImage_setText` 转移，
  重复键自然保持后写值覆盖。`XImageCodecPng.c:312-449` 按 Qt 的 40 字节阈值
  写出 tEXt、zTXt 或 iTXt，iTXt 使用 `UTF-8` 语言标签及同名 translated keyword，
  所有 payload、压缩缓冲区和失败路径均使用 XMalloc/XFree；编码路径在 IDAT 前
  调用 `pngAppendTexts`，未增加公共 API 或旧别名。
- **回归覆盖**：`xgui_regression_test.c:11388-11445` 设置短 ASCII、长 ASCII 和
  中文 UTF-8 三个键值，检查编码结果同时含 tEXt/zTXt/iTXt，随后解码并逐项验证
  文本值往返一致；默认 `build`、`build-crop-xpm-off`、`build-crop-plugin-off`
  均完成全量构建，三种配置的 `XGuiRegression_Test` 直接运行通过，默认配置
  `ctest --test-dir build --output-on-failure` 为 1/1 通过，默认测试产物已恢复。
- **边界**：关键词含非 ASCII 字节时跳过写出（避免生成违反 PNG 规范的关键词）；
  iTXt 的 language/translated keyword 不单独暴露为 XImage 元数据；超过 16 MiB
  或压缩方法非 0 的文本块忽略。历史章节中关于“PNG 文本未实现”的描述属于当时
  记录，不覆盖本节最新实现。全量构建仍有仓库既有 XClass 函数指针、const 丢弃、
  zlib 条件编译告警；当前环境无 Valgrind/LSan，未宣称零告警或全局无泄漏。

### 10.414 2026-09-01 JPEG COM/ICC 元数据对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/jpeg/qjpeghandler.cpp:463-500`
  在写出时把 `QImage::textKeys()/text()` 转为 COM marker，并把原始 ICC profile
  按 `ICC_PROFILE\0`、序号和总段数拆成 APP2；`:957-993` 在读头阶段收集 COM
  与 APP2 ancillary 数据，解码结束后再转回图像文本和色彩空间。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecJpeg.c:543-667`
  新增有界 COM/APP2 解析。COM 遵循 Qt 的 `key: value` 约定，无法拆分时回退为
  `Description`，并使用简化空白后的 UTF-8 值；APP2 识别 `ICC_PROFILE\0`，可拼接
  多个分段，单图 profile 上限 16 MiB。`:2054-2178` 在像素搬移后设置 ICC 侧车和
  文本键值；`:2462-2578` 在 SOI/APP0 后按 Qt 顺序写出 COM 与 APP2，profile 每段
  不超过 JPEG marker 上限，所有临时缓冲区均走 XMalloc/XFree。未增加公共 API 或
  冗余别名，元数据生命周期随 JPEG 解码上下文结束统一释放。
- **回归覆盖**：`xgui_regression_test.c:11677-11736` 构造 8x6 JPEG 夹具，设置
  `Description`/`Author` 和 16 字节 ICC，验证 COM/APP2 编码、解码、文本键值及
  原始 profile 字节往返；默认配置、关闭 XPM 的 `build-crop-xpm-off`、关闭插件的
  `build-crop-plugin-off` 均重链并直接运行通过，默认配置 `ctest` 为 1/1，默认测试
  产物已恢复。
- **边界**：COM 按 UTF-8 解码，不解析 EXIF/XMP 或 JPEG 注释中的语言标签；超长
  COM 按 Qt 的 65533 字节 marker 上限截断，空 marker 和超限 ancillary 数据忽略。
  ICC 分段按出现顺序拼接，暂不依据序号重排，也不执行 ICC parser、mAB/mBA/CLUT
  颜色变换；恶意或不完整 profile 仍可作为原始侧车保留。构建中继续存在仓库既有
  XClass 函数指针、const 丢弃和 zlib 条件编译告警，环境无 Valgrind/LSan，未宣称
  零警告或全局无泄漏。

### 10.415 2026-09-01 JPEG EXIF Orientation 与 ImageTransformation 对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/jpeg/qjpeghandler.cpp:813-945`
  读取 APP1 中的 Exif/TIFF 目录，按 II/MM 字节序查找 Orientation（tag `0x0112`，
  SHORT、count=1），最多遍历 10 个 IFD；`:957-1005` 在头部保存 ancillary
  marker，`:1112-1121`/`:1131-1158` 将变换暴露为 `ImageTransformation` 并
  交由上层自动变换逻辑应用。
- **实现范围**：`XImageCodecJpeg.c:748-893` 增加有界 Exif/TIFF 读取和 Qt 枚举映射，
  仅接受 Orientation 1..8 的合法值，非法结构按 benign 元数据忽略；
  `XImageCodecInternal_probeJpegTransformation` 扫描 SOI 前缀至首个 SOS/EOI，
  `XImageBuiltinPlugin.c:246-264,299-307,592-600` 在随机可读 JPEG 设备上提供
  `ImageTransformation` 选项，复用现有 `XImageReader` 自动变换链路。没有新增旧
  API 或平台依赖，临时数据沿用项目内存接口。
- **回归覆盖**：`xgui_regression_test.c:11746-11823` 使用最小 APP1 夹具验证
  Orientation 1..8 的全部映射，并额外验证大端 MM TIFF 的 Rotate270 路径；默认
  `build` 目标和直接回归均通过，CTest 保持 1/1 通过。编译产物随后恢复为默认配置。
- **边界**：当前探测为固定 256 KiB 头部窗口，极端地把 APP1 放在更远位置时由
  handler 返回 None；不解析 XMP、MakerNote 或缩略图，不在 JPEG 输出端写 Exif，
  writer 继续使用上层通用 `ImageTransformation` 变换。解码 facade 本身不隐式旋转，
  只有 `XImageReader_setAutoTransform(true)` 才会应用该选项。仓库既有 XClass 函数
  指针、const 丢弃、zlib 条件编译告警仍存在，环境无 Valgrind/LSan，未宣称零警告
  或全局无泄漏。

### 10.416 2026-09-01 PNG pHYs/oFFs 物理元数据对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qpnghandler.cpp:535-556`
  读取 `png_get_oFFs`，在 `PNG_OFFSET_PIXEL` 单位时设置 `QImage::offset()`，并
  通过 `png_get_x/y_pixels_per_meter` 设置水平/垂直 DPM；写出路径
  `qpnghandler.cpp:890-903` 在偏移非零时调用 `png_set_oFFs`，在任一 DPM 为正时
  以 `PNG_RESOLUTION_METER` 写出 `pHYs`。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecPng.c:293-319`
  增加有界 `oFFs/pHYs` chunk 写出，偏移按有符号 32 位大端保存，DPM 仅输出正值
  并以米为单位；解码 `:784-803` 校验长度、CRC 和单位，保留像素偏移及米制
  分辨率，非目标单位按 Qt 忽略。`pHYs` 超出 `int` 的值截断到 `INT_MAX`，避免
  窄化溢出；普通/Indexed8 两条编码路径均在 IDAT 前写入，未增加公共 API。
- **回归覆盖**：`xgui_regression_test.c:11439-11551` 验证 3780/2835 DPM、负
  偏移的编码解码往返及 `pHYs/oFFs` 块存在性，并构造非米制/非像素单位夹具，确认
  块被忽略而新图像保留 Qt 默认 96 DPI（3780 DPM）和零偏移。默认 `build` 的目标
  构建、直接回归、两种裁剪目标构建与直接回归，以及 CTest 均已通过；默认测试
  产物已恢复。
- **边界**：仅处理单图 PNG 的 `PNG_RESOLUTION_METER` 和 `PNG_OFFSET_PIXEL`；重复
  块采用首个合法块，非目标单位及错误 CRC 的 ancillary 块忽略。Qt/libpng 的
  超大值转换在本实现中显式钳制到 `int` 上限。仓库既有 XClass 函数指针、const
  丢弃和 zlib 条件编译告警仍存在，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.417 2026-09-01 BMP biXPelsPerMeter/biYPelsPerMeter 物理元数据对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qbmphandler.cpp:74-81` 在读取
  Windows DIB 信息头时读取 `biXPelsPerMeter`/`biYPelsPerMeter`，并在
  `:355-356` 原样写入 `QImage::setDotsPerMeterX/Y()`；写出路径
  `:590-604` 将图像 DPM 写回 INFOHEADER，未设置时使用 2834（72 DPI）默认值。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecBmp.c:227-236,556-560`
  已在 40/64/108/124 字节 DIB 头读取有符号 DPM，并在输出图像上复用
  `XImage_setDotsPerMeterX/Y()`；本轮补充普通 BMP 文件编码路径
  `:682-689`，在文件头相对偏移 38/42 写入源图像 DPM，缺省时按 Qt 写入 2834，
  不增加公共 API，DIB 编码路径保持既有行为。
- **回归覆盖**：`xgui_regression_test.c:10586-10623` 新增 2x1 BMP 夹具，验证
  5000/6000 DPM 在 INFOHEADER 的 Qt 偏移位置写出，并经完整 BMP 解码后保持数值。
  默认目标构建与直接回归、两种裁剪目标构建与直接回归以及 CTest 均已通过，
  默认测试产物已恢复；`git diff --check` 通过。
- **边界**：仅覆盖 Windows BMP/DIB 信息头，OS/2 Core Header 仍按 Qt 语义将 DPM
  视为零；负 DPM 按 BMP 有符号 32 位原样编码，零 DPM 由 `XImage` 的默认元数据
  处理，暂不实现 V5 ICC 配置文件等非 DPM 元数据。构建继续保留仓库既有函数指针、
  const 丢弃和 zlib 条件编译告警；环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.418 2026-09-01 ICO/CUR 图像 Handler 与单条目编解码对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/ico/qicohandler.cpp:28-65`
  定义 ICONDIR、ICONDIRENTRY 和 40 字节 BMP_INFOHDR 的小端布局；`:171-238`
  的 `ICOReader::canRead()` 检查保留字段、资源类型、首条目保留字、图标
  planes/bitCount 和 `dwBytesInRes >= 40`，并保持设备位置不变；`:426-525`
  读取首个条目，支持嵌入 PNG 及 1/4/8/16/24/32 位 DIB，DIB 高度包含 XOR
  与 AND 两部分，非 32 位图像再应用 1 位透明掩码；`:564-675` 将图像缩放到
  256 像素上限，写出一个或多个 32 位 DIB 条目及垂直翻转的 AND mask；
  `:696-729` 规定 Size/ImageFormat 选项的尺寸和 Mono/RGB32/ARGB32/Indexed8
  映射，`ico.json:2-3` 声明 `ico`/`cur` 两个格式键与
  `image/vnd.microsoft.icon` MIME。
- **实现范围**：新增 `XIMAGECODEC_ICO_ON` 独立裁剪开关和
  `Src/XGui/Graphics/XImageCodec/XImageCodecIco.c:19-374`。目录探测复用 Qt 的
  首条目规则，尺寸字节 0 映射为 256，解码前另行执行资源偏移/长度的有界检查；
  嵌入 PNG 交给现有 PNG 解码器，DIB 路径覆盖无压缩 1/4/8/24/32 位、调色板、
  底向上扫描行和非 32 位 AND mask，编码路径按 KeepAspectRatio 缩放并写出单个 32 位 DIB、双高
  信息头和逐行透明掩码。`XImageCodec.c:117-161,176-274,595-659` 接入名称、
  自动识别、尺寸探测、能力及读写分发；`XImageBuiltinPlugin.c:19-45,251-279`
  接入插件格式/MIME/过滤器及 Qt 图像格式映射；`XImageReader.c:137-152,2193-2195`
  与 `XImageWriter.c:21-36,1060-1062` 接入公共格式列表和 MIME 查询。所有
  临时内存使用 XMalloc/XFree 或 XByteArray，未引入平台 API。
- **回归覆盖**：`xgui_regression_test.c:10635-10736` 构造 2x2 ARGB32 图像及 2x1
  1 位调色板图像，验证 ICONDIR 小端字段、自动识别、尺寸探测、32 位像素/Alpha、
  调色板与首条目截断拒绝；
  读取器/写入器注册断言同步覆盖 ICO 条目。默认 `build` 全量构建、
  `XGuiRegression_Test` 和 `ctest --test-dir build --output-on-failure` 均通过；
  关闭 XPM 的 `build-crop-xpm-off` 与关闭插件的 `build-crop-plugin-off` 均重新
  配置、重链并运行 `XGuiRegression_Test` 通过，随后恢复默认测试产物。
- **边界**：为嵌入式门面保留单图语义，只读取目录首个条目，不实现 Qt 的多尺寸
  `count()/iconAt(index)` 枚举和 `_q_icoOrigDepth` 文本元数据；DIB 解码支持
  1/4/8/24/32 位 BI_RGB，16 位按 Qt 的不支持语义拒绝。CUR 的热点字段仅用于通过目录探测，未暴露
  hotspot API；未实现 ICO 内嵌压缩 PNG 以外的压缩 DIB、V5 ICC 和图标缓存。
  探测遵循 Qt 的轻量 canRead 语义，完整资源边界和像素行截断在 decode 阶段拒绝。
  构建继续保留仓库既有 XClass 函数指针、const 丢弃、zlib 条件编译警告及运行
  时 XError 诊断；环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.419 2026-09-01 ICO/CUR 双格式键注册与 MIME 反查对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/ico/ico.json:2-3` 将
  `ico`、`cur` 两个键分别列出并映射到同一个
  `image/vnd.microsoft.icon` MIME；`ico/main.cpp:23-37` 对两个键统一返回
  `CanRead|CanWrite`，空格式时再按设备能力探测；`ico/qicohandler.cpp:171-238`
  规定目录探测和设备位置恢复语义。
- **实现范围**：`XImageBuiltinPlugin.c:19-45`、`XImageReader.c:137-152,2193-2195`
  与 `XImageWriter.c:21-36,1060-1062` 公开 `ico`/`cur` 格式键、过滤器及共享
  MIME；`XImagePluginRegistry.c:203-220` 为插件元数据缺失时补充 CUR 的 MIME
  回退。读取器和写入器的 MIME 反查按 Qt 键顺序返回 `ico`、`cur`，能力仍由同一
  单条目 ICO/CUR 编解码器实现。
- **回归覆盖**：`xgui_regression_test.c:7811-7843` 断言默认注册列表包含两个
  键、共享 MIME 去重后的计数，以及读写 MIME 反查结果和顺序；默认 `build` 目标、
  `XGuiRegression_Test`、CTest 均通过；`build-crop-ico-off` 重新配置并构建回归
  目标、运行通过，确认 `XIMAGECODEC_ICO_ON=0` 时新键和断言按裁剪配置消失。
- **边界**：`cur` 仍复用单条目 ICO DIB/PNG 解码与 ICO 写出路径，不伪造 Qt 的
  CUR 热点元数据接口；未引入多尺寸枚举或额外压缩格式。全量构建保留仓库既有
  XClass 函数指针、const 丢弃和 zlib 条件编译告警；环境无 Valgrind/LSan，未宣称
  零警告或全局无泄漏。

### 10.420 2026-09-01 图像插件显式格式大小写规范化对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:143-152` 在创建
  读取处理器前将显式格式执行 `toLower()`，随后在 `:233-249` 将规范化格式
  传给插件 `capabilities()`/`create()`；写入器沿用同一规则。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:747-804` 的
  `supportsReadFormat()` 与 `supportsWriteFormat()` 现在通过已有
  `normalizedFormat()` 生成小写副本后再调用插件能力查询，并在所有返回路径
  释放副本；与 `createReadHandlerEx()`、`createWriteHandler()` 的
  `effectiveFormat` 行为保持一致，不改变 MIME 查询的大小写敏感语义。
- **回归覆盖**：`xgui_regression_test.c:29,7519-7536,8409-8419` 增加只接受
  小写格式参数的 mock 插件夹具，使用 `MOCK` 查询同时验证读写能力仍按 Qt
  规则返回支持。默认 `build` 目标、`XGuiRegression_Test` 及
  `ctest --test-dir build --output-on-failure` 均通过，`git diff --check` 通过。
- **边界**：只规范化插件能力查询和处理器创建路径；公共 MIME 反查仍按 Qt
  `QByteArray` 精确匹配，格式列表排序与去重规则不变。构建继续保留仓库既有
  XClass 函数指针、const 丢弃和 zlib 条件编译警告；环境无 Valgrind/LSan，未
  宣称零警告或全局无泄漏。

### 10.421 2026-09-01 QIcon::ThemeIcon 完整枚举与名称映射对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qicon.h:25-180` 声明 150 个
  `ThemeIcon` 标准项，`NThemeIcons` 作为固定数量哨兵；`qicon.cpp:1438-1590`
  以同一顺序建立主题名称表，并在 `:1592-1600` 通过枚举序号索引名称。
  `qicon.h:22-23` 同时规定 `Mode` 与 `State`，其中 `State` 顺序为 `On, Off`。
- **实现范围**：`Src/XGui/Icon/XIcon.h:40-233` 将 `XIconState` 调整为 Qt 顺序，
  按 Qt 6.8 固定顺序补齐地址簿、编辑、邮件、设备、状态和天气等全部标准
  `XIconThemeIcon_*` 项，并以 `XIconThemeIcon_NThemeIcons` 保持标准边界；
  `Src/XGui/Icon/XIcon.c:1435-1510` 更新名称表，标准项直接按序映射，无效/哨兵
  返回空名称。旧版 `DocumentClose`、`Folder`、`Help` 等非 Qt 名称保留为边界外
  兼容扩展，通过独立分支映射，不改变标准项序号。现有 `fromThemeIcon()`、
  `hasThemeIconType()` 自动复用新表，无新增平台 API。
- **回归覆盖**：`xgui_regression_test.c:1970-2020` 新增映射测试，验证首项、
  文档扩展项、末项、旧版 `Help` 兼容项以及 `NThemeIcons`/`Invalid` 空名称行为；
  默认 `build` 的完整构建、`XGuiRegression_Test`、CTest 均通过，
  `build-crop-ico-off` 完整构建和回归亦通过。`git diff --check` 通过。
- **边界**：标准枚举项与 Qt 6.8 的 150 项名称和序号一致；旧版兼容项不属于
  `NThemeIcons` 范围，不能用于依赖 Qt 标准序号的跨进程序列化。当前图标引擎仍
  由 XGui 的主题查找/插件接口提供，未引入 Qt 平台私有引擎。`build-crop-painter-off`
  的全量动态库链接仍受既有 `XImageCodecInternal_*` 裁剪符号缺失阻塞（与本改动
  无关）；仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译警告继续存在，
  当前环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.422 2026-09-01 QIconLoaderEngine 非方形 pixmap 与高 DPI DPR 对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qiconloader.cpp:882-930` 的
  `QIconLoaderEngine::pixmap()`/`scaledPixmap()` 以请求尺寸的较小边选择主题目录，
  但由 `PixmapEntry::pixmap()` 按完整 `size * scale` 调用 `adjustSize()`；源图仅在
  超过任一请求边时按 KeepAspectRatio 缩小，未超出时保留原始矩形。`:914-916`
  进一步使用实际物理尺寸计算 `pixmapDevicePixelRatio()`，不强行把返回图像拉伸到
  请求矩形。`qiconloader.cpp:746-748` 的 `paint()` 则单独通过
  `drawPixmap(rect, pixmap(...))` 负责目标矩形绘制。
- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.h:22-27` 新增完整矩形输出入口；
  `XIconThemeInternal.c:1719-1755,2109-2238` 将主题资源缩放从单边方形扩展为完整
  物理宽高，仍以较小边参与目录匹配，并保留旧方形 API 包装。`XIconThemeEngine.c:20-61`
  增加 DPR 修正，`:140-142,289-303` 让 `pixmap()` 返回 Qt 同等的最大不超过请求尺寸，
  `paint()` 继续将结果铺到绘制矩形，`:391-457` 的高 DPI 路径按完整物理宽高和实际
  像素数计算设备像素比。所有内存仍使用 XGui 项目对象接口。
- **回归覆盖**：`xgui_regression_test.c:744-767` 增加 24x18 非方形主题请求，
  断言固定 48x48 条目返回 18x18 而不是错误拉伸为 24x18；既有高 DPI 主题
  `actualSize()`、`paint()`、无效比例和可缩放目录夹具继续覆盖。默认 `build` 全量
  构建、`XGuiRegression_Test` 与 CTest 均通过；`build-crop-ico-off` 亦完成全量构建。
- **边界**：便携 `paint()` 的设备 DPR 仅从图像设备查询，Picture/自定义设备按 1.0；
  主题条目仍采用单条目资源和现有格式裁剪，不实现 Qt 私有平台图标引擎。构建保留
  仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译警告；`build-crop-painter-off`
  仍被既有 `XImageCodecInternal_*` 裁剪链接缺失阻塞，环境无 Valgrind/LSan，未宣称
  零警告或全局无泄漏。

### 10.423 2026-09-01 主题图标普通 DPI 缓存委托与实际尺寸键对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qiconloader.cpp:903-946` 的
  `PixmapEntry::pixmap()` 先按完整物理矩形执行 `adjustSize()`，再以实际宽高、
  样式、调色板和计算后的 DPR 组成 `QPixmapCache` 键；同文件 `:948-974` 的
  `QIconLoaderEngine::pixmap()` 明确委托 `scaledPixmap(size, mode, state, 1.0)`，
  因而普通 DPI 与高 DPI 共用同一缓存语义。
- **实现范围**：`Src/XGui/Icon/XIconThemeEngine.c:20-61,284-291,377-456`
  增加 `scaledPixmap()` 前置声明，并将主题引擎普通 `pixmap()` 改为委托
  `scaledPixmap(..., 1.0f)`；缓存查找/插入统一使用资源实际输出宽高和
  `themeEngine_pixmapDevicePixelRatio()` 计算的千分比 DPR。这样非方形请求在
  资源尺寸小于目标时仍保留实际矩形，不会因请求键与输出键不一致而重复解码。
- **回归覆盖**：`xgui_regression_test.c:744-767` 在 `48x48` 固定主题资源上请求
  `24x18` 两次，断言两次输出均为 `18x18`、缓存键非零且相等；默认构建、
  `XGuiRegression_Test` 与 CTest 均通过；`build-crop-ico-off` 全量构建及其
  回归目标也通过，随后恢复默认构建产物，`git diff --check` 通过。
- **边界**：当前实现为保证键一致性会先完成主题资源解析再尝试缓存命中，性能上
  比 Qt 直接复用已加载 `basePixmap` 略保守，但输出尺寸、DPR、样式和缓存结果
  的可观察语义一致；主题仍保持单条目和 XGui 图像编解码裁剪。构建继续保留
  仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译警告；运行时 XError
  为既有诊断。`build-asan` 的库目标在 `-fsanitize=address,undefined` 下完成，
  但其 `XGuiRegression_Test` 目标因配置时旧源文件清单遗漏新增编解码器对象，
  链接时报 `XImageCodecInternal_*` 未定义符号，未得到可运行 sanitizer 回归；
  环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.424 2026-09-01 ICO 探测与 SVG 前缀缓冲区生命周期修复

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:143-152` 在多个
  Handler 间顺序探测并仅对显式格式生成临时小写副本；ICO Handler
  `qtbase/src/plugins/imageformats/ico/qicohandler.cpp:28-50` 随后直接读取设备原始
  字节。探测器不能把块内临时缓冲区的地址泄露给后续 Handler。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodec.c:215-274` 的 SVG
  探测改用块内 `svgData/svgSize` 访问 4097 字节有界前缀，移除对函数参数
  `data/size` 的覆盖；ICO、XBM、XPM 等后续探测始终继续使用调用方原始数据，
  保持 Handler 顺序和格式判断语义不变。
- **验证结果**：重新生成并运行 `build-asan` 的
  `XGuiRegression_Test`，不再出现 `stack-use-after-scope`，回归退出码为 0。
  ASan/LSan 仍报告 102226 字节既有泄漏，来源为 Mesa GLX、fontconfig 以及
  `XFont` 默认状态/测试清理路径；未将其归因于本次 ICO 修复。随后恢复默认
  `build` 产物，完整构建、直接回归、CTest 和 `git diff --check` 均通过。
- **边界**：SVG 前缀仍最多扫描 4097 字节，gzip SVGZ 仍只按魔数报告 SVG，
  实际解码校验由 Handler 完成；本修复仅改变临时缓冲区生命周期，不改变有效
  SVG/ICO 输入的可观察结果。重新配置后 `build-crop-painter-off` 已纳入新增
  Handler 源文件并完成全量构建与回归；环境无 Valgrind，ASan/LSan 仍保留
  外部图形库及既有 `XFont` 测试清理泄漏，未宣称零警告或全局无泄漏。

### 10.425 2026-09-01 ICO Handler 畸形目录与资源回归夹具

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/ico/qicohandler.cpp:171-188`
  的 `ICOReader::canRead()` 仅凭保留字段、类型、首条目保留字段、位面、位深
  和 `dwBytesInRes >= 40` 判定“可能是 ICO”，不提前读取资源；同文件
  `:564-667` 的写出布局固定为目录、条目、40 字节 DIB、XOR 像素和垂直翻转的
  AND mask，`iconAt()` 在 `:306-413`、`:425-463` 再对资源和 DIB 进行严格读取。
- **实现范围**：`xgui_regression_test.c:10880-11055` 新增最小有效 ICO 克隆夹具，
  覆盖目录保留字节/类型/条目保留字节、图标位面与位深、资源长度不足，及资源
  偏移越界、长度溢出、DIB 头大小、宽高、位面、16 位深度、压缩标志、像素区和
  AND mask 截断等失败路径。可由目录阶段识别的异常断言 `detect == Ico`，解码
  阶段统一断言返回 false 且输出 `XImage` 仍为空，验证边界检查和失败清理。
- **验证结果**：默认 `build` 全量构建、`./bin/XGuiRegression_Test`、CTest 均通过，
  新增 15 个畸形 ICO 断言全部通过；`git diff --check` 通过。随后在
  `build-crop-ico-off` 重新配置并构建，确认 `XIMAGECODEC_ICO_ON=0` 时夹具和
  Handler 代码均被裁剪且目标正常生成（该裁剪配置按开关不执行 ICO 断言）。
- **边界**：夹具覆盖的是单条目 DIB 路径；Qt 可读的多条目选择策略、嵌入 PNG 的
  压缩校验仍由已有 PNG Handler 负责，未扩展为完整 `QList<QImage>` 读写。构建
  继续保留仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译警告；无
  Valgrind，ASan/LSan 仍有此前记录的 Mesa/fontconfig/XFont 既有泄漏，未宣称
  零警告或全局无泄漏。

### 10.426 2026-09-01 ICO `canRead()` 首条目边界与空目录解码分层对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/plugins/imageformats/ico/qicohandler.cpp:171-188`
  的 `ICOReader::canRead()` 读取固定 6 字节目录头和首个 16 字节条目，只检查
  保留字段、资源类型、首条目保留字段、ICO 位面/位深和 `dwBytesInRes >= 40`；
  不检查 `idCount` 是否非零，也不要求输入包含完整目录表。`:241-266` 的
  `readHeader()`/`readIconEntry()` 在实际读取时按索引定位，`:564-667` 的
  `iconAt()` 再校验资源偏移、DIB 头、像素和掩码范围。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecIco.c:31-67` 将首条目
  辅助函数的最小输入改为 22 字节并返回 `idCount`，移除探测阶段对完整目录表和
  `count != 0` 的过早限制；`XImageCodecInternal_probeIcoSize()` 保持 Qt 同等的
  首条目探测，`XImageCodecInternal_decodeIco():249-273` 则在解码阶段拒绝空目录，
  资源范围检查仍先于负载访问，避免截断输入越界。
- **回归覆盖**：`xgui_regression_test.c:10952-11055` 新增 6/21 字节截断首条目
  探测拒绝、`idCount == 0` 的“可探测但解码失败”夹具；与既有 17 类目录/资源
  异常共同验证 Qt 两阶段边界。默认 `build` 全量构建、直接回归和 CTest 均通过；
  `build-crop-ico-off` 重新配置、全量构建及回归通过，确认 `XIMAGECODEC_ICO_ON=0`
  时 ICO 实现和夹具被裁剪；`git diff --check` 通过。
- **边界**：单幅门面仍只解码首个条目，因此 `idCount` 很大但首条目有效时与 Qt
  `iconAt(0)` 一样允许读取，后续条目列表不在 XImageCodec 门面范围内。ASan 目标
  可构建且回归无 UAF/UBSan；LSan 继续报告既有 Mesa/fontconfig/XFont 泄漏，仓库
  既有 XClass 函数指针、const 丢弃和 zlib 条件编译警告仍未归零。

### 10.427 2026-09-01 图像 Handler 发现回调代际重入边界对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:143-219,221-254,258-337`
  明确了读 Handler 的顺序：文件后缀插件优先，其次显式格式插件，再到内置格式；
  后缀或显式 Handler 的 `canRead()` 失败后才回退到内容插件和内容内置 Handler。
  `qimagewriter.cpp:101-145,147-190` 规定写路径先按后缀选择插件、再创建内置
  Handler，随后由第一个支持写入的插件替换内置 Handler。`qimagereaderwriterhelpers.cpp:84-142`
  规定格式/MIME 列表排序去重及内置项在 MIME 反查中的先行顺序。Qt 的插件目录
  扫描与 key 映射位于 `corelib/plugin/qfactoryloader.cpp:334-388,588-610`，
  插件库成功加载后保持加载，key 映射按库索引稳定返回。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c` 保持显式格式、后缀格式、
  内容探测三条创建路径的优先级，以及外部插件先于内置插件的注册顺序；发现回调
  使用 generation 记录配置代际，回调执行期间若通过
  `XImagePluginRegistry_setPluginDiscoveryCallback()` 重配入口，旧回调的成功返回
  不得把新入口标记为已完成，避免丢失下一代发现动作。该保护仍兼容
  `XIMAGEPLUGINREGISTRY_CAPACITY` 裁剪、注册表清空后的重新发现和递归锁。
- **回归覆盖**：`xgui_regression_test.c:7752-7774,9409-9412` 增加重入发现回调，
  第一次查询断言旧回调只执行一次且新回调尚未执行；第二次查询断言新回调恰执行
  一次；第三次查询断言成功代际不重复执行。既有后缀覆盖、显式格式、内容探测、
  失败重试、格式/MIME 列表和插件优先级测试继续通过。默认 `build` 全量构建、
  `./bin/XGuiRegression_Test` 和 CTest 均通过，`git diff --check` 通过；此前
  `build-crop-ico-off` 与 ASan/UBSan 回归也已覆盖相关 Handler 裁剪和生命周期路径。
- **边界**：Src 层通过 Drive 提供的发现回调抽象动态插件枚举，不实现 Qt 私有的
  `QFactoryLoader` 元数据扫描、Qt 版本/调试构建筛选和真实动态库加载；注册表仍为
  固定容量且外部插件由调用方管理生命周期。ASan/LSan 结果继续包含 Mesa、fontconfig
  与既有 `XFont` 清理路径的外部泄漏，仓库既有 XClass 函数指针、const 丢弃和 zlib
  条件编译警告仍存在，因此未宣称零警告或全局无泄漏。图标引擎私有主题回退和更多
  XPainter opcode 仍未完成；QImage 色彩空间/文本元数据的基础 API 已在 10.405、
  10.413 及后续章节覆盖，剩余边界是原始 ICC 公共值接口与完整 ICC 变换。

### 10.428 2026-09-01 图标主题搜索路径缓存与清除语义对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qiconloader.cpp:146-166` 在清除用户
  主题名时调用 `setThemeSearchPath(systemIconSearchPaths())` 恢复平台主题目录；
  同文件 `:180-210` 的 `themeSearchPaths()`/`fallbackSearchPaths()` 仅在首次访问
  时向平台主题查询路径并缓存，随后始终追加 `:/icons` 资源路径。
- **实现范围**：`Src/XGui/Icon/XIcon.c:103-126,1533-1595,1627-1695` 增加默认
  平台路径恢复辅助函数；主题与回退路径 getter 采用惰性单次查询并复制到全局缓存，
  `XIcon_setThemeName()` 及 `_2` 清除用户覆盖时恢复平台路径；默认路径在平台
  路径后始终追加 `:/icons`。所有列表仍由项目 `XStringList` 管理，未引入平台 API。
- **回归覆盖**：`xgui_regression_test.c:536-579` 首次读取并缓存默认主题路径，切换
  用户主题后清除覆盖，断言显式路径被移除、平台路径恢复；当恢复的平台目录非空时
  `setThemeSearchPath(systemIconSearchPaths())` 不再追加 `:/icons`，只有空平台列表
  的默认懒加载分支才追加该资源目录。
  `xgui_regression_test.c:7752-7774,9409-9412` 同时覆盖
  插件发现回调重入时的 generation 代际保护。默认构建和直接回归已通过；最小裁剪
  配置重新配置后通过。
- **边界**：主题名仍由 Drive 提供，当前门面不实现 Qt 私有 `QFactoryLoader` 动态
  插件元数据扫描；路径缓存生命周期与 Qt 一致，但平台自身更换主题快照需显式清空
  XIcon 全局状态。构建中的仓库既有 XClass 函数指针、const 丢弃和 zlib 条件编译
  警告不属于本次改动，未宣称零警告或全局无泄漏。

### 10.429 2026-09-01 QIconLoaderEngine availableSizes 重复目录条目对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qiconloader.cpp:980-988` 的
  `QIconLoaderEngine::availableSizes()` 直接遍历 `m_info.entries`，对每个固定或
  阈值条目追加 `QSize(dir.size, dir.size)`，没有 `contains()` 去重；主题对象可
  由多个 `themeSearchPaths()` 内容目录组成，因此相同逻辑尺寸的多个登记项在
  返回列表中保持重复。对比 `qicon.cpp:401-410`，只有 `QPixmapIconEngine` 的
  独立像素条目路径才显式去重。
- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.c:1781-1789,1828-1859`
  将主题尺寸追加辅助函数从 `theme_appendSizeUnique()` 改为逐条目
  `theme_appendSize()`，并在索引主题的每个 contentDir、旧式目录每个命中项及
  多个搜索根中均保留重复尺寸；继承主题仍只在当前主题无条目时参与收集，独立
  回退文件仍沿用实际像素尺寸。
- **回归覆盖**：`xgui_regression_test.c:1411-1467` 新增两个 `48x48` 内容目录
  的同名图标夹具，断言 `XIcon_availableSizes()` 返回两个连续 `48x48` 项；随后
  恢复原索引文件，避免影响后续 scalable/fixed 选择测试。默认 `build` 目标构建、
  直接回归和 CTest 均通过；本次改动不新增平台 API。
- **边界**：主题条目仍按当前简化解析器的文件存在顺序登记，未实现 Qt GTK
  `icon-theme.cache` 的完整内容目录索引合并；重复条目仅表达可观察的尺寸列表
  语义，不改变 `entryForSize()` 的首个精确匹配和最近距离选择。构建继续保留
  仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译警告，环境无 Valgrind/LSan，
  未宣称零警告或全局无泄漏。

### 10.430 2026-09-01 QImageReader 显式未知格式能力查询错误保持

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:497-550` 的
  `QImageReaderPrivate::initHandler()` 在处理器工厂返回空指针时统一设置
  `UnsupportedFormatError`；`qimagereader.cpp:1110-1117` 的 `canRead()` 与
  `:1432-1437` 的 `supportsOption()` 均先初始化处理器，失败时直接返回
  `false`，不会把错误状态改回 `UnknownError`。
- **实现范围**：核对 `Src/XGui/Graphics/XImageReader.c:724-835,1485-1504,2001-2008`
  的处理器初始化、`canRead()` 和 `supportsOption()` 路径；显式未知格式、关闭
  自动探测且未指定格式、设备打开失败三类状态均保持既有错误优先级。未增加
  平台 API 或改变裁剪分支。
- **回归覆盖**：`xgui_regression_test.c:8204-8215` 在已有显式未知格式夹具中
  新增 `supportsOption(Size)` 断言，验证能力查询返回 `false` 且仍保持
  `UnsupportedFormatError`；同一夹具继续覆盖 `canRead()` 与 `read()`。默认
  `build` 目标构建、直接回归和 CTest 均通过。
- **边界**：`XIMAGEIOPLUGIN_ON=0` 时保留嵌入式 `XImageCodec` 的签名探测路径，
  这是本项目裁剪策略，不等同 Qt 的插件 Handler 可见性；UTF-8 格式名兼容重载的
  静态缓存上限及超长键截断边界见 10.437。构建保留仓库既有 XClass 函数指针、
  const 丢弃及 zlib 条件编译警告，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.431 2026-09-01 QIcon 空主题名称比较边界对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qicon.cpp:1431-1437` 的
  `QIcon::hasThemeIcon()` 直接比较 `fromTheme(name).name()` 与原始请求；
  `qicon.cpp:1376-1387` 对空名称仍创建主题引擎，因此空 `QString` 与空引擎
  名称比较相等，结果为 `true`，而不是依据 `isNull()` 返回 `false`。
- **实现范围**：`Src/XGui/Icon/XIcon.c:1426-1451` 将空 `XString` 作为 Qt
  空名称比较的兼容边界返回 `true`；空指针仍表示 C API 中不存在名称并返回
  `false`，绝对文件路径和资源路径继续按文件图标处理并返回 `false`，非空名称
  仍通过主题、继承主题及独立回退路径解析后进行精确名称比较。
- **回归覆盖**：`xgui_regression_test.c:7135-7141` 新增空字符串与空指针
  两个断言。默认 `build` 全量构建、`./bin/XGuiRegression_Test`、CTest
  1/1 及 `git diff --check` 均通过。
- **边界**：C 接口无法表达 Qt 的隐式空 `QString`，因此仅对显式空 `XString`
  复现该结果；`XIcon_hasThemeIcon_2(NULL)` 保持项目既有空指针保护。仓库既有
  XClass 函数指针、const 丢弃和 zlib 条件编译警告仍存在，环境无 Valgrind/LSan，
  未宣称零警告或全局无泄漏。

### 10.432 2026-09-01 QImage 文件加载的后缀优先与内容回退

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3763-3776`
  将 `QImage::load(fileName, format)` 委托给 `QImageReader(fileName, format).read()`；
  `qimagereader.cpp:497-550` 的 `QImageReaderPrivate::initHandler()` 在未指定格式时先
  依据 `QFileInfo::suffix()` 选择处理器，后缀处理器不能读取时再继续内容探测。
- **实现范围**：`Src/XGui/Graphics/XImage.c:4555-4630` 的 `XImage_load_2()` 保持
  显式非空格式权威；格式省略或为空时提取最终路径组件的扩展名，若对应内置编解码器
  存在则先按该格式解码，失败后重新初始化临时图像并按文件头探测。文件打开、字节数
  溢出和最终失败仍将目标图像置为空，避免半成品像素泄漏。
- **回归覆盖**：`xgui_regression_test.c:9542-9559` 写入 XPM 文件后以
  `XImage_load_2(..., NULL)` 自动加载，断言尺寸保持 `3x2`；XPM 没有可靠二进制魔数，
  因此该夹具专门验证后缀优先路径。默认 `build` 全量构建、`./bin/XGuiRegression_Test`
  和 CTest 1/1 均通过，`git diff --check` 通过。
- **边界**：实现复用轻量 `XImageCodec`，不引入 Qt 的懒加载 `QIODevice`、逐候选扩展名
  的完整插件扫描或动态 `QFactoryLoader`；无插件裁剪配置继续由内置编解码器完成后缀和
  内容两阶段选择。构建仍保留仓库既有 XClass 函数指针、const 丢弃及 zlib 条件编译
  警告，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.433 2026-09-01 QImage 保存空格式按文件后缀推断

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:3884-3890`
  将文件保存交给 `QImageWriter(fileName, format)`；`qimagewriter.cpp:176-190`
  规定未提供格式时从文件扩展名选择处理器，`QByteArray` 的空值和空字符串都表示
  未指定格式。
- **实现范围**：`Src/XGui/Graphics/XImage.c:4681-4703` 将 `XImage_save_2()` 的
  空字符串格式与 `NULL` 统一处理，按最终路径组件（同时支持 `/` 与 `\\`）提取扩展名，
  再复用现有编解码器和插件选择；显式非空格式仍保持权威，不改变质量参数和失败状态。
- **回归覆盖**：`xgui_regression_test.c:9542-9554` 新增 PNG 文件空格式写出夹具，
  断言 `XImage_save_2(..., "")` 成功并清理输出文件；同段继续覆盖 XPM 后缀自动加载。
  默认与裁剪配置构建、回归程序、CTest 均通过，`git diff --check` 通过。
- **边界**：无扩展名且格式为空时仍按 Qt 语义失败；轻量实现不扫描动态插件目录，
  外部插件优先级仍由 `XImageWriter` 的现有注册表处理。仓库既有 XClass/const/zlib
  编译告警和环境无 Valgrind/LSan 证据保持不变，未宣称零警告或全局无泄漏。

### 10.434 2026-09-01 QImageReader 关闭自动探测时空格式拒绝语义对齐

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:143-149`
  的 `createReadHandlerHelper()` 在进入插件或内置处理器查找前，统一以
  `!autoDetectImageFormat && format.isEmpty()` 拒绝空格式；这里的
  `QByteArray::isEmpty()` 同时覆盖未设置格式和显式设置的空字符串。随后
  `qimagereader.cpp:497-506` 的 `QImageReaderPrivate::initHandler()` 将工厂
  失败映射到错误状态，`qimagereader.cpp:1110-1116` 的 `canRead()` 直接返回
  `false`，不会再尝试内容探测。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:735-746` 将前置判断改为
  同时识别 NULL 与空 `XString`；即使 `decideFormatFromContent` 为真，只要
  自动探测被关闭且格式为空，仍报告 `UnsupportedFormatError`。裁剪配置的
  `XImageReader_canRead()` 兼容分支（同文件 `:1520-1530`）也按相同规则拒绝
  空字符串，避免绕过 Handler 前置检查。非空显式格式在自动探测关闭时仍可按
  格式创建处理器，默认自动探测和仅内容决策路径保持原有独立状态。
- **回归覆盖**：`xgui_regression_test.c:8208-8227` 先设置 BMP 格式再清为空串，
  关闭自动探测并开启内容决策，断言 `canRead()` 和 `read()` 均失败且错误码为
  `UnsupportedFormatError`。默认 `build` 与 `build-crop-ico-off` 均全量构建成功，
  两套 `XGuiRegression_Test` 和 CTest 均通过，`git diff --check` 通过。
- **边界**：无插件裁剪配置仍保留项目 `XImageCodec` 的直接读取能力，但不会绕过
  Qt 同等的“关闭自动探测 + 空格式”前置拒绝；C API 的 NULL 与空字符串均映射到
  Qt 的空 `QByteArray`。构建仍有仓库既有 zlib 条件编译、XClass 函数指针和 const
  丢弃警告，环境无 Valgrind/LSan 证据，因此未宣称零警告或全局无泄漏。

### 10.435 2026-09-01 QImageWriter 裁剪模式内置格式能力查询对齐

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagewriter.cpp:147-175`
  在格式已知时创建对应内置处理器；同文件 `:725-734` 的
  `QImageWriter::supportsOption()` 即使尚未设置设备也会创建处理器并转发
  `QImageIOHandler::supportsOption()`。各内置处理器的能力表取自
  `plugins/imageformats/jpeg/qjpeghandler.cpp:1126-1140`、
  `plugins/imageformats/gif/qgifhandler.cpp:1131-1137` 与
  `plugins/imageformats/ico/qicohandler.cpp:726-729`，并结合 BMP/PNG/PPM/XBM/XPM
  处理器对应 `supportsOption()` 实现中的尺寸、格式、子类型和名称选项。
- **实现范围**：`Src/XGui/Graphics/XImageWriter.c:274-349` 新增无
  `XIMAGEIOPLUGIN_ON` 时的 `XImageWriter_builtinSupportsOption()`。它从显式格式或
  文件名后缀解析 `XImageCodecFormat`，在设备为空时为 BMP/PNG/JPEG/GIF/PPM/XBM/XPM
  及其它可用内置编解码器返回与便携处理器一致的能力；JPEG 的图像变换仍要求可读设备，
  GIF 动画、PPM 子类型、XBM/XPM 名称等专用选项按 Qt 处理器规则单独映射。
  `XImageWriter_supportsOption()`（`:1068-1085`）仅在插件裁剪分支调用该映射，插件开启
  时继续使用真实 Handler，避免改变外部插件优先级与能力声明。
- **回归覆盖**：`xgui_regression_test.c:7871-7898` 在 `XIMAGEIOPLUGIN_ON=0`
  下构造无设备 BMP writer，断言 Size/ImageFormat 为真、Quality 为假，并验证
  `canWrite()` 仍返回 DeviceError。`build-crop-no-plugin` 全量构建、回归程序及
  CTest 1/1 均通过；默认构建和 `build-crop-ico-off` 也完成复核，插件路径的既有
  回归保持通过。
- **边界**：轻量 C 编解码器没有 Qt JPEG 的缩放/裁剪、Description、SupportedSubTypes
  等完整元数据接口，因此映射不会宣称这些选项；无设备时 GIF 的 Size 仍为假（Qt 只有
  随机访问设备才支持），而 Animation 可在处理器存在时查询。仓库既有 XClass/const/zlib
  编译告警仍存在，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.436 2026-09-01 QImageReader 设备前置检查与空格式错误优先级

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:491-505`
  的 `QImageReaderPrivate::initHandler()` 先检查设备是否存在并尝试打开外部
  `QIODevice`，无设备或打开失败立即设置 `DeviceError`；同文件 `:510-552` 仅在
  自有 `QFile`、自动探测开启且原路径打开失败时执行扩展名回退，最后才调用
  `createReadHandlerHelper()`，由 `:145-149` 将关闭自动探测且空格式映射为
  `UnsupportedFormatError`。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:744-793` 将设备打开与默认扩展名
  回退拆分为自有文件设备和外部设备两条路径：仅在关闭自动探测、没有格式且未启用
  内容决策时跳过自有文件打开，外部设备仍按 Qt 规则尝试打开；空格式拒绝移动到
  设备检查之后，确保无设备保持 `DeviceError`，有有效设备才报告
  `UnsupportedFormatError`。显式非空格式和内容决策路径仍先打开设备，以保持项目
  现有处理器读写夹具的行为。
- **回归覆盖**：`xgui_regression_test.c:8210-8224` 新增无设备且关闭自动探测的
  `canRead()` 断言，验证 `DeviceError` 优先级；既有显式空格式、未知格式和文件扩展名
  回退夹具继续覆盖后续分支。默认、`build-crop-no-plugin` 与 `build-crop-ico-off`
  构建、回归程序和 CTest 均需通过后方可视为本节完成。
- **边界**：与 Qt 一样，文件设备自动探测关闭且原路径不存在时不会执行默认扩展名扫描；
  当前 C 接口仍将 NULL 与空 `XString` 统一视为空格式。显式格式或内容决策仍按项目
  兼容路径打开自有文件后再交给处理器。构建保留仓库既有 XClass/const/
  zlib 警告，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.437 2026-09-01 QImageReader UTF-8 格式兼容重载长键缓存

- **Qt 依据**：Qt 6.8 `qimagereader.cpp:1456-1467` 的静态
  `QImageReader::imageFormat(QIODevice*)` 返回完整 `QByteArray`，不以 16 字节
  截断插件声明的格式键；`qimagereader.cpp:1470-1484` 的文件名重载同样直接
  返回该值。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:17-21,2050-2066,2162-2172`
  将 `imageFormat_2()` 与 `imageFormatDevice_2()` 的内部静态缓存扩大为 256 字节，
  保留原有静态生命周期和 NULL 失败语义；头文件同步注明 255 字节上限，避免调用方
  误以为返回对象可释放或无限长。
- **回归覆盖**：`xgui_regression_test.c:8700-8747` 新增长格式键
  `xinyue-long-format-123` 的临时插件与文件名夹具，断言
  `XImageReader_imageFormat_2()` 返回完整键值；既有 BMP/PNG/XPM/SVG 及长 XML 前缀
  探测夹具继续覆盖其它路径。默认、`build-crop-no-plugin` 与 `build-crop-ico-off`
  构建、回归程序和 CTest 均通过后记录本节。
- **边界**：C 字符串重载仍是线程共享的静态缓存，且超过 255 字节的非标准插件键会被
  截断；完整无截断互操作需新增调用方缓冲区或返回所有权字符串的 API。构建保留仓库
  既有 XClass/const/zlib 警告，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。

### 10.438 2026-09-01 imageFormatDevice_2 长格式键回归补充

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:1456-1467`
  的设备重载与文件名重载都返回完整格式字节串；C 接口只能通过静态 UTF-8 缓冲区
  暴露该值，因此回归必须同时覆盖设备生命周期和长键内容。
- **实现范围**：`xgui_regression_test.c:8732-8747` 新增临时文件设备夹具，调用
  `XImageReader_imageFormatDevice_2()` 并断言 `xinyue-long-format-123` 未被截断；
  设备打开状态检查与关闭动作保持 `QIODevice` 读探测后的生命周期顺序。
- **验证结果**：`build-crop-xpm-off` 的全量构建、直接回归与 CTest 1/1 均通过；
  默认构建产物将在本节完成后重新生成并复核。`git diff --check` 通过。
- **边界**：256 字节静态缓存仍限制返回键为 255 字节以内，且不是线程隔离或调用方所有权
  字符串；仓库既有 XClass/const/zlib 编译告警和无 Valgrind/LSan 运行环境保持不变，
  不宣称零警告或全局无泄漏。

### 10.439 2026-09-01 主题 index.theme 权威模式与传统目录屏蔽

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:347-370`
  先按主题根目录是否存在 `index.theme` 建立 `QIconTheme`，`findIconHelper()` 只
  遍历索引中的 `contentDirs`/`keyList`；不存在有效条目时才走 `Inherits` 和短横线
  回退（`:460-565`），不会因为索引为空而探测旧式尺寸目录。
- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.c:128-138` 增加索引文件存在性检查，
  并在 `theme_selectEntryType()`（`:1242-1264`）、`theme_searchTheme()`（`:1483-1506`）
  和 `theme_searchThemeExists()`（`:1624-1642`）中将“索引存在”与“索引内容可解析”
  分离；损坏或 `Directories=` 为空时仍强制索引模式，避免 `theme_loadLegacy()` 的
  传统路径抢占。
- **回归覆盖**：`xgui_regression_test.c:968-993` 新增 `IndexedEmpty` 主题，创建空
  `index.theme` 与 `48x48/apps/legacy-shadow.bmp`，断言解析和 `hasThemeIcon()` 均失败；
  默认、`build-crop-no-plugin`、`build-crop-ico-off` 的全量构建、回归程序及 CTest 1/1
  均通过，`git diff --check` 通过。
- **边界**：主题索引仍使用便携 INI 解析器和固定 1024 字节路径，未实现 Qt 的
  `QSettings` 编码细节、动态主题插件和实时缓存失效；既有 XClass/const/zlib 编译告警
  及无 Valgrind/LSan 证据保持不变，未宣称零警告或全局无泄漏。

### 10.440 2026-09-01 QThemeIconEngine 引擎键契约

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:704-734`
  中 `QIcon::fromTheme()` 创建的是 `QThemeIconEngine`；其 `key()` 固定返回
  `QThemeIconEngine`，`clone()` 复制主题名称，`read()`/`write()` 负责名称流化。
  `qicon.cpp:1928-1930` 仅在反序列化时兼容旧的 `QIconLoaderEngine` 键。
- **实现范围**：`Src/XGui/Icon/XIconThemeEngine.c:315-323` 将
  `XIconThemeEngine_key()` 从错误的 `QIconLoaderEngine` 修正为
  `QThemeIconEngine`；`XIcon_fromTheme()` 创建的主题引擎因此与 Qt 当前引擎类型保持
  一致，图标名称仍由 `iconName()` 单独返回。
- **回归覆盖**：`xgui_regression_test.c:7177-7191` 更新未命中主题引擎及公开
  `XIcon_name_2()` 断言，另以键值断言确认 `XIconThemeEngine_create_2_ex()` 返回
  `QThemeIconEngine`；克隆、名称和 fallback 断言继续覆盖同一引擎路径。
- **验证结果**：默认配置清理重建成功，`./bin/XGuiRegression_Test` 输出
  `XGui regression tests passed`，`ctest --test-dir build --output-on-failure` 为 1/1
  通过；`build-crop-no-plugin`（`CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`）全量构建、
  直接回归与 CTest 亦为 1/1 通过。构建输出中的既有 XClass/const/zlib 警告和运行时
  XError 已如实保留，环境无 Valgrind/LSan 时不宣称零泄漏。
- **边界**：`XIconThemeEngine_read/write()` 仍未实现 Qt `QDataStream` 名称序列化，
  原生 QIcon/QThemeIconEngine 流互操作继续按既有 XPicture/QIcon 序列化边界处理。

### 10.441 2026-09-01 QThemeIconEngine 未命中名称语义

- **Qt 依据**：Qt 6.8 `qiconengine.cpp:360-363` 的
  `QProxyIconEngine::iconName()` 委托 `proxiedEngine()->iconName()`；
  `qiconloader.cpp:959` 的 `QIconLoaderEngine::iconName()` 返回已加载信息中的
  `iconName`，主题或回退均未命中时该字段为空。`QThemeIconEngine` 本身只保存请求名
  用于查找和流化，不把请求名直接作为 `QIcon::name()` 暴露。
- **实现范围**：`Src/XGui/Icon/XIconThemeEngine.c:336-340` 在主题解析失败时返回空
  `XString`，仅在 `XIconInternal_resolveThemeIconName()` 找到实际条目时返回解析名称；
  `xgui_regression_test.c:7177-7187` 更新未命中主题引擎及公开名称断言。
- **验证结果**：默认配置与 `build-crop-no-plugin`（`CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`）
  均完成全量构建；两套 `XGuiRegression_Test` 均输出
  `XGui regression tests passed`，对应 CTest 均为 1/1 通过。`git diff --check` 通过。
- **边界**：引擎的 `read/write()` 仍未实现 Qt `QDataStream` 名称序列化，原生流互操作
  继续按既有边界处理；成功查找时的名称回退仍受当前便携主题索引解析器限制。

### 10.442 2026-09-01 QIconEngine 基类默认虚函数契约

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconengine.cpp:193-249`
  规定基类 `key()` 返回空字符串、`read()`/`write()` 返回 `false`，未知
  `virtual_hook` 不做处理；同文件 `:251-280` 规定 `availableSizes()` 与
  `iconName()` 返回空值，`isNull()` 初始为 `false` 后交给 `IsNullHook` 覆盖；
  `:285-320` 的默认 `pixmap()` 先按请求尺寸创建像素图，再通过 `paint()` 填充。
- **实现范围**：`Src/XGui/Icon/XIconEngine.c:50-145` 保持上述默认行为，空引擎
  的尺寸列表清空输出、名称与键为空、读写失败、`isNull()` 允许钩子改写；默认
  `pixmap()` 通过 `XImage`/`XPainter` 生成请求尺寸的透明像素图。缩放钩子仍按
  项目嵌入式安全边界拒绝非正、NaN、无穷及整数溢出比例。
- **回归覆盖**：`xgui_regression_test.c:7208-7279` 新增基类默认契约断言，覆盖
  空 key/name、`availableSizes()` 清空、`isNull=false`、默认读写失败以及
  `pixmap()` 保持 `5x7` 请求尺寸；既有缩放钩子、无效比例和钩子覆盖测试继续执行。
- **验证结果**：默认配置与 `build-crop-no-plugin`（`CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`）
  均完成全量构建，`./bin/XGuiRegression_Test` 与对应 CTest 均为 1/1 通过；默认
  产物已在裁剪复核后重新生成。构建输出保留仓库既有 XClass/const/zlib 警告和运行时
  XError，环境无 Valgrind/LSan，未宣称零警告或全局无泄漏。
- **边界**：`XIconEngine_read/write()` 仍只提供便携 `XIODevice` 抽象，不模拟 Qt
  私有 `QDataStream`；默认透明像素图的实际存储格式遵循当前 `XImage`，不承诺
  与 Qt `QPixmap` 私有缓存键互操作。

### 10.443 2026-09-01 QImageReader 处理器初始化失败时的格式返回

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:636-645`
  规定 `QImageReader::format()` 在显式格式为空时先调用 `initHandler()`；处理器
  初始化失败直接返回空 `QByteArray`，只有已创建处理器且 `canRead()` 成功时才返回
  `handler->format()`，不会再用原始设备签名补造格式。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1068-1073` 在
  `XIMAGEIOPLUGIN_ON` 开启时，`ensureHandler()` 未创建处理器即返回 `NULL`；仅在
  无插件裁剪配置下保留 `XImageReader_imageFormatDevice()` 的便携签名回退，以维持
  内置编解码器无处理器实例时的可用性。
- **验证结果**：默认配置重新构建成功，`./bin/XGuiRegression_Test` 输出
  `XGui regression tests passed`，CTest 为 1/1 通过；本轮修改前已验证的
  `build-crop-no-plugin` 仍保持全量构建、回归和 CTest 1/1。`git diff --check`
  通过。构建中的仓库既有 XClass/const/zlib 警告、运行时 XError 及无
  Valgrind/LSan 证据如实保留。
- **边界**：无插件裁剪继续使用便携探测而非 Qt 私有处理器；动态
  `QFactoryLoader` 发现、原生 `QDataStream` 互操作和其它图像元数据扩展仍按前述章节
  保持未实现。

### 10.444 2026-09-01 XPM 十六进制颜色与 ImageMagick 尾部 alpha

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qcolor.cpp:48-78`
  将 `#AARRGGBB` 的 alpha 两位按普通十六进制整数解析；
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qxpmhandler.cpp:900-907`
  对 `#` 后长度不是三的倍数的 XPM 颜色，先按 ImageMagick 兼容规则截断到
  `#RRGGBB`，再交给颜色解析器。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecXpm.c:186-198`
  修正八位十六进制分支的 alpha 计算为普通两位整数；XPM 颜色读取继续按 Qt
  的尾部 alpha 截断规则处理，避免把 `#804080c0` 误解为前缀 alpha 颜色。
- **回归覆盖**：`xgui_regression_test.c:10486-10493` 构造带 ImageMagick 尾部
  alpha 的单像素 XPM，`xgui_regression_test.c:10568-10571` 断言 Qt 兼容结果为
  `0xff804080`；既有调色板、透明色、尺寸探测和编码后再解码夹具继续执行。
- **验证结果**：默认配置全量构建、`./bin/XGuiRegression_Test` 与 CTest 均通过
  （1/1）；`build-crop-no-plugin`（保留 XPM 的无插件处理器路径）和
  `build-crop-xpm-off`（`XIMAGECODEC_XPM_ON=0`）均完成全量构建、直接回归与
  CTest，分别为 1/1 通过。默认构建产物已在裁剪验证后恢复；`git diff --check`
  通过。构建输出中的既有 XClass/const/zlib 警告、运行时 XError 以及无
  Valgrind/LSan 证据均如实保留，未宣称零警告或全局无泄漏。
- **边界**：XPM 的 ImageMagick 兼容截断仍不暴露尾部 alpha；其它 Qt 私有图像
  插件和原生 `QDataStream` 互操作不在本次范围内。

### 10.445 2026-09-01 图像插件 MIME 回退映射补全

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereaderwriterhelpers_p.h:66-80`
  的内置格式表将 `pbm/pgm/ppm/xbm/xpm` 分别映射到
  `x-portable-bitmap`、`x-portable-graymap`、`x-portable-pixmap`、`x-xbitmap`、
  `x-xpixmap`；`qimagereaderwriterhelpers.cpp:102-143` 要求 MIME 反查遵循插件
  元数据顺序并去重。`qimagewriter.cpp:743-750` 与 `qimagereader.cpp:1477-1485`
  的公开文档确认这些 MIME 是 Qt 默认读写能力的一部分。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:203-232` 为插件缺失或空
  MIME 元数据时增加五种便携格式的规范回退值。已有键/ MIME 数组仍优先使用真实元数据，
  内置 `dib` 继续保持内部格式且不虚假映射；大小写敏感 MIME 比较、排序和首次出现去重
  规则不变。
- **回归覆盖**：`xgui_regression_test.c:8173-8200` 新增 PPM/PBM/PGM、XBM、XPM
  MIME 列表精确断言，同时保留 `imageFormatsForMimeType()` 的 `ppm`、JPEG 别名和
  ICO/CUR 顺序夹具。默认配置、`build-crop-no-plugin` 与 `build-crop-xpm-off`
  的全量构建、直接回归和 CTest 均需通过后视为完成。
- **边界**：Qt 内置表不公开 `pbmraw/pgmraw/ppmraw`，因此回退函数也不为这些内部
  子类型生成 MIME；外部插件发现仍由 C99 回调提供，不模拟 Qt `QFactoryLoader` 的
  动态目录扫描。构建保留仓库既有 XClass/const/zlib 警告、运行时 XError 以及无
  Valgrind/LSan 证据，未宣称零警告或全局无泄漏。

### 10.446 2026-09-02 QImage 恒等颜色变换的共享语义

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5199-5213`
  在 `QImage::applyColorTransform(transform)` 遇到 `transform.isIdentity()` 时立即
  返回，不执行 `detach()` 或像素写入；`qimage.cpp:5340-5346` 的带目标格式重载则在
  恒等变换时仅执行 `convertTo(toFormat, flags)`。因此不带格式的恒等变换必须保持
  隐式共享和原有 `cacheKey()`。
- **实现范围**：`Src/XGui/Graphics/XImage.c:1938-1963` 将可表达的恒等变换定义为
  源/目标 `XColorSpace` 相等：无目标格式时直接共享复制（或对同一对象不操作），
  指定格式时仅执行格式转换；非恒等路径仍按原有颜色模型检查、中间格式选择和目标
  色彩空间写入流程处理。
- **回归覆盖**：`xgui_regression_test.c:13640-13657` 新增输出对象和原对象两条恒等
  变换断言，验证像素数据、源图像及共享 `cacheKey()` 均保持不变。默认配置完成
  全量构建、直接回归与 CTest 后，再复核无插件裁剪，确保颜色变换新增分支不依赖插件。
- **边界**：`XColorTransform` 仅携带源/目标色彩空间，无法表达 Qt 私有变换矩阵中
  “空间相同但矩阵非恒等”的情况；该类变换继续按便携 RGB/灰度传递函数实现。构建
  保留仓库既有 XClass/const/zlib 编译告警和运行时 XError，环境无 Valgrind/LSan，
  未宣称零警告或全局无泄漏。

### 10.447 2026-09-02 SVGZ Handler 格式键与 MIME 对齐

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtsvg/src/plugins/imageformats/svg/svg.json`
  同时声明 `svg`/`svgz` 及 `image/svg+xml`/`image/svg+xml-compressed`；
  `qsvgiohandler.cpp:59-97` 在 gzip 魔数命中时将处理器格式设置为 `svgz`，并只对
  该格式走压缩内容读取。格式键虽共享 SVG 渲染器，外部 `format()` 仍须保留键值。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:138-157,233-247,2240-2246`
  将 `svgz` 加入读取格式/MIME 表、gzip 内容探测和压缩 MIME 反查；无插件裁剪的
  静态 Handler 查询同时拒绝 `svg` 与 `svgz`，保持没有 QSvgIOHandler 时不伪造结果。
  `Src/XGui/Graphics/XImageBuiltinPlugin.c:19-52,77-99,710-755` 同步内置插件元数据，
  并将 `svgz` 能力限制为只读，避免普通 SVG 编码器误报 gzip 写出能力。
- **回归覆盖**：`xgui_regression_test.c:7930-7940,8080-8107,8160-8195`
  验证读取格式/MIME 计数、`image/svg+xml-compressed` 反查、Writer 不公开 `svgz`，
  以及 gzip 文件内容探测返回 `svgz`。默认配置和 `CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`
  裁剪配置均执行全量构建、直接回归和 CTest。
- **边界**：内部 `XImageCodecFormat_Svg` 仍合并普通与压缩 SVG，只有 Reader/插件
  外层格式键区分 `svgz`；内置 Writer 仍只输出普通 XML，动态 qtsvg 私有渲染能力及
  原生 `QDataStream` 互操作不在范围内。构建保留既有 XClass/const/zlib 告警、运行时
  XError 及无 Valgrind/LSan 证据，未宣称零警告或全局无泄漏。

### 10.448 2026-09-02 XPainter 渐变画刷 Picture 记录

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qpaintengine_pic.cpp:176-192`
  在 `updateBrush()` 中记录完整 `QBrush`；`qtbase/src/gui/painting/qbrush.cpp:996-1058`
  规定渐变画刷流化时写入样式/颜色、渐变类型、停止点和对应几何参数，并在
  `qpaintengine_pic.cpp:741-750` 回放时恢复画刷对象。该原生流还包含 spread、坐标
  模式、插值模式、画刷变换及纹理图像等宿主对象，不能直接嵌入 C99 固定协议。
- **实现范围**：`Src/XGui/Graphics/XPicture.h:59-61,322-340` 增加
  `SetBrushGradient` opcode 和记录入口；`XPicture.c` 使用小端、单精度便携负载保存
  三种渐变类型的相关几何字段及最多 `XPAINTER_GRADIENT_MAX_STOPS` 个停止点，验证
  有限值、位置范围和负载长度后回放到 `XPainter` 状态。`XPainter_setBrushGradient()`
  在 Picture 后端同步记录渐变，切回纯色仍使用原有固定 `SetBrush` 命令。未使用的
  几何槽位在写入端清零，避免泄漏未初始化字节。
- **回归覆盖**：`xgui_regression_test.c:6531-6645` 新增线性渐变记录、长度校验、
  几何/停止点回放断言；`build-crop-painter-off` 验证 `XPAINTER_ON=0` 时该入口和
  负载校验均被裁掉。默认 `build` 及画笔关闭裁剪均完成全量构建、直接回归和 CTest
  （1/1），`git diff --check` 通过。
- **边界**：便携渐变子集不保存 Qt 的 spread、坐标模式、插值模式、QBrush 变换和
  纹理图像，也不承诺原生 `QDataStream`/QPIC 互操作；停止点上限为 8，超过上限或
  非有限几何值会拒绝记录。动态插件发现、完整 ICC 变换和其它尚未定义的 XPainter
  高级 opcode 仍按前述章节保持未完成；构建中的 XClass/const/zlib 既有告警、运行时
  XError 及无 Valgrind/LSan 证据如实保留。

### 10.449 2026-09-02 XPainter 纹理画刷与画刷变换边界核对

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/painting/qbrush.cpp:726-735` 规定
  `QBrush::setTexture(QPixmap)` 在非空图像时切换到 `TexturePattern` 并持有纹理；
  `:750-783` 的 `textureImage()/setTextureImage()` 以 `QImage` 资源提供同一语义，
  空图像则切换为 `NoBrush`。`qbrush.cpp:865-869` 规定 `setTransform()` 独立保存
  画刷矩阵；`qbrush.cpp:996-1058` 的流化还会写出纹理图像或画刷变换矩阵。
- **现有实现**：`Src/XGui/Graphics/XPainter.h:337,375-381,1283-1290` 仅有
  `TexturePattern` 枚举和固定画刷/渐变字段，没有 `XImage` 资源所有权、纹理引用
  表或画刷专用变换矩阵；`XPainter.c:5318-5327` 按 Qt `qbrush_check_type()` 拒绝
  纹理与渐变样式，`XPainter_setBrush_2()` 对构造式非法样式回落 `NoBrush`。因此
  本轮只修正文档契约：纹理样式标记为“不支持”，调用 `setBrushStyle(TexturePattern)`
  保持原画刷；未新增无法安全回放的伪纹理 opcode。
- **边界**：软件绘制仍以纯色/图案近似，纹理图像、画刷变换、spread/坐标模式/插值
  模式及原生 `QDataStream`/QPIC 互操作均未实现。若后续需要该能力，必须先设计
  有生命周期管理的图像资源表和版本化协议，再扩展 Picture 记录；当前固定 `SetBrush`
  负载不能表达这些对象。

### 10.450 2026-09-02 XIcon_paint 对齐掩码与 AlignAbsolute 回归

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qguiapplication_p.h:174-183`
  的 `visualAlignment()` 先用 `AlignHorizontal_Mask` 判断是否缺省水平对齐，
  该掩码包含 `AlignAbsolute`（`qnamespace.h:152`）；因此仅给出
  `AlignAbsolute` 时不会补 `AlignLeft`，而没有任何水平位时才补左对齐。随后
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1022-1039`
  仅按 Right/HCenter/VCenter/Bottom 调整绘制矩形，Absolute-only 保持物理起点。
- **实现范围**：现有 `Src/XGui/Icon/XIcon.c:1191-1210` 已按同一掩码和 RTL 交换规则
  处理；本轮不改变运行时代码，新增 `xgui_regression_test.c` 夹具覆盖无水平位默认左对齐，
  以及 LTR/RTL 下 `XAlignment_Absolute` 单独出现时均保持物理左边界。
- **验证结果**：默认配置及 `CMAKE_C_FLAGS=-DXPAINTER_LAYOUT_DIRECTION_ON=0` 裁剪配置均已
  完成全量构建、直接回归和 CTest（各 1/1 通过）；保留既有 XClass/const/zlib 编译告警、
  运行时 XError 与无 Valgrind/LSan 证据，不宣称零警告或全局无泄漏。
- **边界**：`XAlignment_Justify`、Baseline 等 Qt 对齐位对 QIcon 绘制本身不改变矩形位置；
  纹理画刷资源、原生 QDataStream/QPIC 互操作和完整 ICC 变换仍按 10.449 及前述章节保持未完成。

### 10.451 2026-09-02 QImageIOHandler/XPM 末轮 API 审计

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimageiohandler.h:25-109` 定义
  `QImageIOHandler` 的 19 个 `ImageOption`、位组合式 `Transformation` 和
  `QImageIOPlugin` 的 `CanRead/CanWrite/CanReadIncremental` 能力；
  `qimagereader.cpp:497-555,636-645,1432-1437,1458-1467` 规定处理器初始化失败、
  `format()/supportsOption()/imageFormat()` 的 `canRead()` 分层及错误码优先级；
  `qimagereaderwriterhelpers.cpp:84-143` 规定内置格式 MIME 映射、插件能力过滤、
  字节级排序和去重。XPM 的 `qxpmhandler.cpp:812-980,1038-1142` 规定头部边界、
  颜色字段选择、短像素行填零、透明色及 64 字符键生成。
- **审计结论**：`XImageIOHandler.h/.c`、`XImageReader.c`、`XImageWriter.c` 与
  `XImagePluginRegistry.c` 已覆盖上述公共 API；处理器创建失败返回
  `UnsupportedFormatError`、设备/文件错误优先级、内容探测后的 `canRead()` 校验、
  4096 字节 SVG 前缀探测及大小写敏感列表去重均已有实现。XPM 的完整 657 项颜色表、
  `None` 透明色、ImageMagick 尾部 alpha 截断和排序输出已在 10.409、10.444 覆盖，
  本轮未发现可安全新增的行为。
- **验证结果**：默认配置与 `XPAINTER_LAYOUT_DIRECTION_ON=0` 裁剪配置均重新完成
  `XGuiRegression_Test` 构建、直接回归和 CTest（各 1/1 通过）；默认构建产物已恢复，
  `git diff --check` 通过。构建中的 XClass/const/zlib 既有告警、运行时 XError 诊断及
  当前环境没有 Valgrind/LSan 证据的内存边界如实保留，未宣称零告警或全局无泄漏。
- **边界**：纯 C99 门面仍不模拟 Qt 私有 `QFactoryLoader` 动态目录扫描、原生
  `QDataStream`/QPIC 互操作和图像处理器私有资源；`XImageCodec` 的超大调色板、SVGZ
  gzip 及其它格式能力继续按各自章节的嵌入式限制执行。

### 10.452 2026-09-02 XPM 重复颜色键覆盖语义

- **Qt 依据**：Qt 6.8 `qxpmhandler.cpp:871-889` 将颜色键写入
  `QMap<quint64,int>`；`QMap::insert()` 对相同键替换已有值，因此像素行引用重复键时
  必须使用最后一次颜色定义。
- **实现范围**：`XImageCodecXpm.c:xpm_findColor()` 改为从颜色表尾部逆序查找，保持
  重复键的后定义覆盖；新增 `xgui_regression_test.c:test_codec_xpm` 重复键夹具，验证
  像素索引及最终调色板颜色均指向第二条定义。未增加额外公共 API。
- **验证结果**：默认配置与 `CMAKE_C_FLAGS=-DXIMAGECODEC_XPM_ON=0` 裁剪配置均完成
  全量构建、`XGuiRegression_Test` 直接回归和 CTest（各 1/1 通过）；默认构建产物已恢复。
  保留既有 XClass/const/zlib 编译告警、运行时 XError 诊断及当前环境无 Valgrind/LSan
  证据的内存边界，不宣称零警告或全局无泄漏。
- **边界**：逆序查找仅影响重复颜色键；正常唯一键、透明色、短像素行及 657 色表行为不变。

### 10.453 2026-09-02 XPicture 畸形渐变负载拒绝

- **Qt 依据**：Qt 6.8 `qpaintengine_pic.cpp:176-192` 将渐变画刷作为状态命令
  写入，回放阶段由 `qpicture.cpp:741-750` 还原；停止点位置由 `QGradientStop`
  约束在 `[0,1]`。XGui 的便携流需在回放前完成同等有限值和范围检查。
- **实现范围**：保留 `XPicture_validateGradientPayload()` 的类型、长度、几何有限值
  和停止点范围验证；新增 `xgui_regression_test.c:test_picture_malformed_state_records`
  夹具，将首个停止点改为 NaN 并重新计算校验，确认 `XPicture_isValidStream()` 拒绝
  畸形记录。未增加额外公共 API。
- **验证结果**：默认配置与 `XPAINTER_ON=0` 裁剪配置均完成全量构建、直接回归和 CTest
  （各 1/1 通过），默认构建产物已恢复。保留既有 XClass/const/zlib 编译告警、运行时
  XError 诊断及当前环境无 Valgrind/LSan 证据的内存边界，不宣称零警告或全局无泄漏。
- **边界**：夹具覆盖 NaN 停止点；无穷几何、越界位置、未知渐变类型和负载截断仍由同一
  校验器拒绝。Qt spread、坐标模式、插值模式、纹理资源与原生 QPIC 互操作仍未实现。

### 10.454 2026-09-02 QThemeIconEngine 名称流化与畸形长度防护

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:729-736`
  中 `QThemeIconEngine::read/write` 只流化图标名称；`qtbase/src/corelib/text/qstring.cpp:9582-9635`
  规定 QString 采用 `writeBytes()` 的字节长度前缀和 UTF-16 原始码元，
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/serialization/qdatastream.cpp:1395-1405`
  及 `qdatastream.h:458-480` 规定长度和默认大端字节序。
- **实现范围**：`Src/XGui/Icon/XIconThemeEngine.c:331-467` 新增大端
  `quint32` 长度、UTF-16 字节读写和短读循环；普通主题名称与空字符串按 Qt
  字节格式往返，空引擎以 `NullCode`（`0xffffffff`）表示。为嵌入式安全，扩展长度
  标记 `0xfffffffe`、奇数 UTF-16 字节数、超过 4 MiB 的负载、设备短读和嵌入式 NUL
  均拒绝，不使用标准库分配。`xgui_regression_test.c:7257-7344` 新增中英文名称
  往返、奇数长度和扩展标记夹具。
- **验证结果**：默认配置与 `CMAKE_C_FLAGS=-DXPAINTER_ON=0` 裁剪配置均完成全量构建、
  `XGuiRegression_Test` 直接回归和 CTest（各 1/1 通过）；默认测试产物已恢复。当前
  构建保留既有 zlib、XClass/const 编译告警和运行时 XError 诊断；本环境没有
  Valgrind/LSan 证据，因此不宣称零告警或全局无泄漏。
- **边界**：Qt 的 64 位扩展长度在便携层明确拒绝；原生 QDataStream/QPIC 整体互操作、
  QThemeIconEngine 的私有代理状态和纹理资源仍按前述章节保持未实现，名称流仅覆盖
  Qt 公共 `read/write` 所需字段。

### 10.455 2026-09-02 PNG Description 写入器链路对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimage.cpp:6457-6483` 将
  `Description` 按简化的 `key: value` 段落解析并合并到图像文本键；
  `qimagewriter.cpp:609-613` 保存 `setText()` 键值，`qpnghandler.cpp:668-716`
  按排序后的描述文本生成 PNG 文本块，`qpnghandler.cpp:1077-1120` 声明并处理
  `Description` 选项。由此，写入器必须把处理器自有的 Description 选项传到编码源图像，
  不能只读取基础选项表。
- **实现范围**：`Src/XGui/Graphics/XImageBuiltinPlugin.c:598-619` 改用虚拟
  `XImageIOHandler_option_base()` 读取 Description，再通过
  `XImage_applyTextDescription()` 克隆并装饰编码源；`XImageCodecPng.c:548-575,1503-1523`
  排除空文本键并提供描述编码辅助；`XImageWriter.c:1025-1071` 同时覆盖设备写入和
  文件名直写路径。解析器保持 Qt 的空白简化、重复键后者覆盖和键值排序，并沿用
  PNG 文本块的便携实现。`xgui_regression_test.c:12554-12621` 新增文件设备端到端夹具，
  验证 Title/Description 写入后由 PNG 解码恢复且测试文件必定清理。
- **验证结果**：默认配置完成全量构建、直接回归和 CTest（1/1）；`XPAINTER_ON=0`
  裁剪配置完成目标构建、直接回归和 CTest（1/1）。默认测试产物已恢复，
  `git diff --check` 通过。构建仍保留既有 zlib、XClass/const 编译告警和运行时
  XError 诊断；环境没有 Valgrind/LSan 证据，因此不宣称零警告或全局无泄漏。
- **边界**：本节写入链路不改变动态 Qt PNG 插件的私有渐进读取；读前文本缓存随后在
  10.456 补齐。原生 QDataStream/QPIC 互操作仍不在范围内。

### 10.456 2026-09-02 PNG 文本元数据读前查询对齐

- **Qt 依据**：Qt 6.8 `qpnghandler.cpp:364-391` 在读取 PNG 头部和尾部时分别调用
  `readPngTexts()`，因此 IDAT 前后的 `tEXt/zTXt/iTXt` 都会进入处理器描述；
  `qimagereader.cpp:560-563,884-901` 在尚未 `read()` 时由
  `QImageReader::textKeys()/text()` 解析 `Description`。描述条目按出现顺序拼接，
  再由 `qt_getImageTextFromDescription()` 的 QMap 语义执行简化、排序和重复键覆盖。
- **实现范围**：`XImageCodecPng.c:121-298` 新增有界文本块预扫描和描述拼接辅助，复用
  现有 CRC、`tEXt/zTXt/iTXt` 解析及 16 MiB/1024 项上限；扫描器接受 IDAT 前后合法
  ancillary 文本、拒绝坏 PNG 结构/关键块 CRC/超限负载，且不解码像素。内置处理器在
  `XImageBuiltinPlugin.c:778-819,899-902` 创建时仅对可读 PNG 设备窥探并缓存描述，
  让 `XImageReader_loadText()` 在 `read()` 前即可得到键值；写设备和非 PNG 格式不执行
  该路径。`xgui_regression_test.c:12554-12621` 增加写出后读前查询键值夹具。
- **验证结果**：默认配置完成目标构建、直接回归和 CTest（1/1）；`XPAINTER_ON=0`
  裁剪配置完成目标构建、直接回归和 CTest（1/1）。默认测试产物已恢复，生成 PNG
  夹具文件已清理，`git diff --check` 通过。构建仍保留仓库既有 zlib、XClass/const
  编译告警和运行时 XError 诊断；环境没有 Valgrind/LSan 证据，因此不宣称零警告或
  全局无泄漏。
- **边界**：读前扫描只覆盖内置 PNG 处理器可获得的完整随机访问设备，顺序设备或超过
  16 MiB 的 PNG 不缓存描述；Qt 动态插件的渐进读取、原生 QDataStream/QPIC 互操作和
  其它格式的私有文本缓存仍未实现。

### 10.457 2026-09-02 PNG Gamma 与 CompressionRatio 写入选项对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qpnghandler.cpp:1013-1027`
  规定 `compression` 优先于 `quality`，将 `[0,100]` 截断后映射为
  `(value * 9) / 91` 的 zlib 等级；`qpnghandler.cpp:812-831` 规定无 ICC
  profile 时由旧式 gamma 选项写出 `gAMA`，文件值为 `1/gamma` 的 100000 倍。
  `qpnghandler.cpp:1077-1117` 同时声明并保存 Gamma、Quality 和
  CompressionRatio 选项。
- **实现范围**：`XImageCodecInternal.h` 新增 `encodePngOptions()` 内部入口；
  `XImageCodecPng.c` 统一计算 Qt 压缩等级，调色板与 RGBA 两条编码路径均传递
  zlib 等级，并在无 ICC profile 且 gamma 为正有限值时写出大端 `gAMA` 块。旧的
  `encodePng()` 和 Description 包装器改为调用同一入口，保持默认字节布局与文本合并。
  `XImageBuiltinPlugin.c` 从 Handler 选项读取 Quality/CompressionRatio/Gamma，
  `XImageWriter.c` 的插件和无插件直写/文件写入均传递三项参数；CompressionRatio
  优先于 Quality，超出范围按 Qt 上限截断。
- **回归覆盖**：`xgui_regression_test.c:test_codec_png_writer_options()` 创建
  固定图像，验证压缩比 0/100 生成不同 zlib 负载，并验证 gamma=2.2 写出
  `gAMA=45455`；默认配置与 `XPAINTER_ON=0` 裁剪配置均完成目标构建、直接回归和
  CTest（各 1/1 通过），默认测试产物已恢复，`git diff --check` 通过。
- **边界**：Gamma 只在无 ICC profile 时写 `gAMA`，与 Qt 的 iCCP 优先级一致；
  非正、NaN、溢出 gamma 拒绝写块。PNG 读取端的 Gamma 选项仍只返回已保存的基础
  选项，尚未把文件 `gAMA` 反映为 Handler 侧的浮点查询；动态插件、原生
  QDataStream/QPIC 互操作和无界压缩参数仍不在范围内。构建保留既有 zlib、
  XClass/const 告警、运行时 XError 及无 Valgrind/LSan 证据，不宣称零警告或全局无泄漏。

### 10.458 2026-09-02 PNG Gamma 读取选项预扫描对齐

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qpnghandler.cpp:426-466` 在
  `readPngHeader()` 中读取 PNG `gAMA` 块并保存文件 gamma；
  `qpnghandler.cpp:1077-1100` 的 `option(Gamma)` 在没有显式设置值时返回
  `fileGamma`。PNG 文件值是 `1/gamma` 的 100000 倍，故读侧返回编码文件中的
  倒数值，而不是重新返回写入器传入的 gamma。
- **实现范围**：`XImageCodecInternal.h` 声明并由 `XImageCodecPng.c:300-339`
  实现有界 `gAMA` 预扫描：验证 PNG 签名、IHDR 顺序与 CRC，在首个 IDAT 前读取首个
  非零合法值，返回 `raw / 100000.0f`；缺失、零值、坏 CRC、截断块和关键块乱序均返回
  false。`XImageBuiltinPlugin.c:822-852,899-902` 在可读、非顺序且不超过 16 MiB 的
  设备上创建内置 PNG Handler 时缓存 Gamma；显式 `setOption(Gamma)` 仍覆盖缓存值，
  Handler 的 `option(Gamma)` 保持 Qt 的优先级。未引入平台 API或标准库分配。
- **回归覆盖**：`xgui_regression_test.c:test_codec_png_reader_gamma()` 编码
  `gamma=2.2` 的 PNG，经文件设备和 `XImagePluginRegistry_createReadHandler()` 创建
  内置处理器，验证声明 `Gamma`、读回约 `0.454545` 以及查询前后设备位置不变，并清理
  临时文件。默认配置与 `CMAKE_C_FLAGS=-DXPAINTER_ON=0` 裁剪配置均完成目标构建、
  直接回归和 CTest（各 1/1 通过）；默认产物已恢复。
- **边界**：顺序设备、超过 16 MiB 文件和动态第三方 PNG Handler 不执行该便携预扫描；
  `QImageReader` 公共 API 只有 `supportsOption(Gamma)`，没有直接暴露 Handler
  `option()` 的读取器接口，因此夹具通过注册表处理器验证浮点值。gAMA 不影响已有 PNG
  颜色空间解码路径；原生 QPIC/QDataStream 互操作及 Valgrind/LSan 仍未实现/不可用。

### 10.459 2026-09-02 QImageReader 后缀回退单次遍历

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:203-219,258-345`
  在文件后缀插件拒绝内容后恢复设备位置，并只执行一次后续内容探测；内容探测
  仍按插件顺序跳过已尝试的后缀插件，随后再尝试内置处理器。重复创建同一处理器
  会改变带副作用插件的状态，且不符合 `createReadHandlerHelper()` 的单次遍历语义。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:2112-2138` 将静态
  `imageFormat(QIODevice*)` 的后缀处理、`canRead()` 失败和工厂返回空统一到一个
  `if (suffix)` 分支：先释放拒绝的处理器，再最多调用一次
  `XImagePluginRegistry_createReadHandlerContentFallback()`。原有设备位置恢复、
  内置处理器回退和格式结果校验保持不变；未引入新的公共 API、平台 API 或标准库
  内存分配。
- **验证结果**：默认 `build` 与 `build-crop-painter-off`（`XPAINTER_ON=0`）均完成
  `XGuiRegression_Test` 目标构建、直接回归和 CTest（1/1 通过）；默认测试产物已
  恢复，`git diff --check` 通过。构建中的 XClass/const/zlib 既有告警、运行时
  XError 诊断及当前环境无 Valgrind/LSan 证据的内存边界如实保留，未宣称零警告或
  全局无泄漏。
- **边界**：回退遍历仍受注册表固定容量、动态插件发现配置和内置格式开关约束；
  原生 QPIC/QDataStream 互操作及动态插件目录扫描仍未实现。此修正只消除静态
  `imageFormat(QIODevice*)` 失败路径的重复遍历，不改变成功路径的插件优先级。

### 10.460 2026-09-02 QImageReader 静态处理器单次 canRead

- **Qt 依据**：Qt 6.8 `qtbase/src/gui/image/qimagereader.cpp:180-252,326-345`
  在后缀处理器创建后仅在统一确认阶段调用一次 `handler->canRead()`；确认成功后
  直接返回该处理器的格式，失败才销毁并进入内容回退。重复调用自定义处理器的
  `canRead()` 可能改变其内部探测状态，不符合 Qt 的处理器生命周期。
- **实现范围**：`XImageReader_imageFormatDevice()` 现在缓存后缀处理器第一次
  `canRead()` 结果；只有内容回退创建的新处理器才执行一次独立确认，避免同一处理器
  在静态 `imageFormat(QIODevice*)` 路径被重复探测。`xgui_regression_test.c` 增加
  外部后缀处理器调用计数断言，覆盖拒绝后回退和设备位置恢复语义。
- **验证结果**：默认 `build` 与 `build-crop-painter-off`（`XPAINTER_ON=0`）均完成
  目标构建、直接回归和 CTest（各 1/1 通过）；`git diff --check` 通过，默认测试
  产物已恢复。构建保留仓库既有 XClass/const、zlib 等告警及运行时 XError 诊断；
  当前环境无 Valgrind/LSan 证据，未宣称零警告或全局无泄漏。
- **边界**：此修正仅影响静态 `imageFormat(QIODevice*)` 的后缀处理路径，不改变
  `QImageReader::read()` 的处理器调用次数；动态插件目录扫描、原生 QPIC/QDataStream
  互操作和高级色彩资源仍是文档中记录的明确边界。

### 10.461 2026-09-02 QImageReader 裁剪模式显式未知格式错误

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:497-552`
  的 `QImageReaderPrivate::initHandler()` 在显式格式无法创建处理器时统一保留
  `UnsupportedFormatError`；`QImageReader::canRead()`（同文件 `:1110-1115`）
  只返回处理器的可读结果，不会把该错误改回 `UnknownError`。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:1550-1570` 在
  `XIMAGEIOPLUGIN_ON=0` 的内置裁剪路径中，发现非空且不支持的显式格式且前置错误
  仍为 `UnknownError` 时设置 `UnsupportedFormatError` 再返回 `false`，与插件开启时
  `ensureHandler()` 的错误语义保持一致；若设备缺失或打开失败，保留更早的
  `DeviceError`/`FileNotFoundError`，不被未知格式覆盖。
- **回归覆盖**：`xgui_regression_test.c:8610-8635` 在插件裁剪配置新增已打开文件设备
  的显式 `unsupported-format` 夹具，断言 `canRead()` 返回假且错误码为
  `UnsupportedFormatError`；无设备的既有夹具继续断言 `DeviceError`，默认配置与插件
  裁剪配置均执行同一测试文件。
- **验证结果**：`build-crop-no-plugin` 与默认 `build` 均完成目标/全量构建、直接回归
  和 CTest（各 1/1 通过），默认测试产物已恢复。构建仍出现仓库既有 XClass、const、
  zlib 告警；ASan/UBSan 直跑功能通过，但 LSan 仍报告既有/第三方泄漏，不能宣称全局
  无泄漏。
- **边界**：C 接口仍将空格式与 NULL 统一处理；显式非空但未注册的第三方格式在
  `XIMAGEIOPLUGIN_ON=0` 下只保证错误码，不提供动态插件能力。SVG/SVGZ、JPEG 等
  高级处理器的 Qt 私有选项继续按现有裁剪边界处理。

### 10.462 2026-09-02 XIconTheme 空目录 UBSan 边界

- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.c:676-685` 在主题目录数量为零时
  跳过 `memset(NULL, 0, 0)`，避免 C 标准未定义的空指针目标；非空目录的元数据清零
  与 Qt 主题目录筛选逻辑保持不变。该修正不引入平台 API 或新的内存分配路径。
- **验证结果**：修正后 ASan/UBSan 回归全部功能断言通过，`XIconThemeInternal.c:680`
  的空指针诊断已消失；LSan 仍报告约 102 KB 的既有/第三方泄漏，不能宣称全局无泄漏。
  默认与 `build-crop-no-plugin` 回归及 CTest 也均为 1/1 通过。
- **边界**：zlib `trees.c:873` 在零长度存储块上的空源指针 UBSan 诊断属于第三方库
  既有实现，未在本轮修改；LSan 报告包含 fontconfig/Mesa 及已有 XFont/字符串缓存。

### 10.463 2026-09-02 XIconTheme 多搜索根目录条目顺序对齐

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qiconloader.cpp:352-368`
  按 `QIcon::themeSearchPaths()` 顺序收集每个主题根目录；同文件 `:480-531` 对每个
  content directory 发现 PNG 时使用 `entries.insert(begin())`，发现 SVG 时使用
  `push_back()`；`:849-874` 的 `entryForSize()` 先按条目顺序寻找精确尺寸，再按
  最小距离保留第一个匹配项。因此同尺寸 PNG 由后出现的根目录/目录条目覆盖，SVG
  和其他可缩放条目保持先出现者优先。
- **实现范围**：`Src/XGui/Icon/XIconThemeInternal.c:1328-1430,1515-1565` 为索引主题
  的候选选择增加全局格式、搜索根序号和目录序号 tie-break；PNG（格式优先级 0）
  的同距离候选采用后根/后目录优先，SVG 等格式采用先根/先目录优先。候选移动后统一
  释放临时 `XPixmap`，保持所有未选中路径无泄漏；父主题、dash fallback、单根目录
  行为不变。未引入平台 API 或标准库分配。
- **回归覆盖**：`xgui_regression_test.c:1057-1146` 创建两个独立主题根目录，写入相同
  `48x48/apps` 的不同颜色 PNG，设置搜索路径 `[first, second]` 后解析
  `root-order`，断言返回第二个根目录的像素；测试结束恢复全局主题路径并递归清理夹具。
- **验证结果**：默认 `build` 与 `build-crop-no-plugin`
  (`CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`) 均完成 `XGuiRegression_Test` 目标构建、
  直接回归和 CTest（各 1/1 通过），默认可执行文件已恢复；`git diff --check` 通过。
  构建仍保留仓库既有 zlib、XClass/const 警告和运行时 XError 诊断；当前环境无
  Valgrind/LSan 的全局无泄漏证据，不宣称零警告或全局无泄漏。
- **边界**：此修正覆盖索引主题的 PNG/SVG 条目顺序；动态图标插件、GTK 缓存、原生
  QIcon/QDataStream 互操作和其他平台主题后端仍按现有裁剪边界处理。非索引传统目录
  的同距离根目录策略未改变。

### 10.464 2026-09-02 QImageReader 裁剪路径显式格式内容确认

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:122-127`
  先将显式 `format` 作为 `testFormat`；`:280-345` 在没有插件处理器时按该格式创建
  内置处理器；`:636-645` 的 `format()` 仅接受处理器确认后的格式；`:1108-1115`
  的 `canRead()` 透传处理器 `canRead()`，不会因为另一个格式的设备签名可识别而返回真。
- **实现范围**：`Src/XGui/Graphics/XImageReader.c:267-293,1580-1620` 为插件裁剪路径增加
  显式格式与设备签名的 codec 枚举比较。BMP/PNG 等同格式返回可读，JPEG 别名及
  PPM 家族别名共享同一枚举；显式格式与签名不匹配时 `canRead()` 返回假，
  `decideFormatFromContent` 开启时继续忽略显式格式，未引入平台 API 或标准库分配。
- **回归覆盖**：`xgui_regression_test.c:8650-8685` 在 `XIMAGEIOPLUGIN_ON=0` 且 BMP、PNG
  开启时，先断言显式 `bmp` 对 BMP 设备可读，再断言显式 `png` 对同一 BMP 设备不可读，
  并确认后续 `read()` 不会错误解码；夹具沿用已打开的内置 BMP 文件并在测试结束清理。
- **验证结果**：默认 `build` 与 `build-crop-no-plugin`（`CMAKE_C_FLAGS=-DXIMAGEIOPLUGIN_ON=0`）
  均完成 `XGuiRegression_Test` 目标构建、直接回归和 CTest（各 1/1 通过）；默认产物
  已恢复，`git diff --check` 通过。构建仍保留仓库既有 XClass/const、zlib 警告及运行时
  XError 诊断；当前环境没有 Valgrind/LSan 的全局无泄漏证据，未宣称零警告或全局无泄漏。
- **边界**：仅修正无插件内置 codec 的 `canRead()` 显式格式确认；插件开启时仍由实际
  `XImageIOHandler::canRead()` 决定，SVG/SVGZ 无插件静态格式结果、动态第三方插件目录
  扫描及原生 QPIC/QDataStream 互操作继续按既有裁剪边界处理。

### 10.465 2026-09-02 XPM 哈希碰撞键语义对齐

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qxpmhandler.cpp:54-66`
  的 `xpmHash()` 将像素键逐字节左移累加到 `unsigned int`，超过 4 字节的高位被
  丢弃；`:838-900` 使用 `QMap<quint64, int>` 以该哈希值作为唯一键登记颜色，后出现
  的重复键或不同字符串碰撞键会替换先前索引，像素行随后只按同一哈希查询。
- **实现范围**：`Src/XGui/Graphics/XImageCodec/XImageCodecXpm.c:981-990` 的颜色查找
  现在完全按 Qt 的 32 位哈希键反向查找，不再额外比较原始像素键；保留表尾优先以
  复现 `QMap::insert()` 的覆盖结果。该修改仅影响 XPM 键碰撞和重复键，不改变调色板
  颜色解析、透明色或大调色板图像格式选择。
- **回归覆盖**：`xgui_regression_test.c:10865-10985` 新增 5 字符碰撞夹具，`aaaaa`
  与 `baaaa` 在 32 位滚动哈希下相同，断言像素键解析为后出现颜色索引 1 及其绿色
  调色板项；既有重复键夹具继续覆盖同一键的覆盖行为。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 的 `XGuiRegression_Test`
  目标均完成编译，分别执行直接回归与 CTest（各 1/1 通过）；默认测试产物已恢复，
  `git diff --check` 通过。构建日志中的 XClass/const、zlib 警告和运行时 XError
  属于仓库既有项，LSan/Valgrind 证据仍不可用，不宣称零警告或全局无泄漏。
- **边界**：该修正复现 Qt 当前 XPM 哈希碰撞语义，不扩展 Qt 的文件格式探测窗口、
  C++ 源码转义解释或写出器的颜色数量上限；原生 QPicture/QDataStream 互操作及
  ICC profile 实际转换仍按前述章节的有意裁剪处理。

### 10.466 2026-09-02 QPpmHandler raw 子类型别名与规范化

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimagereader.cpp:281-286`
  和 `qimagewriter.cpp:169-174` 在内置处理器分派时同时接受 `pbm`/`pbmraw`、
  `pgm`/`pgmraw`、`ppm`/`ppmraw`，并把请求值传入 `QImageIOHandler::SubType`；
  `qppmhandler.cpp:450-476` 的静态 `canRead()` 只检查 `P[1]` 并返回规范
  `pbm`、`pgm` 或 `ppm`，`qppmhandler.cpp:498-508` 让写入使用所选子类型且只声明
  `SubType`、`Size`、`ImageFormat` 选项；`qimagereaderwriterhelpers.cpp:84-97`
  的公共格式列表只枚举规范扩展名，不包含 raw 别名。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c` 的内置插件键匹配额外接受
  三个 raw 别名，但 `keys()`、MIME 和文件过滤器仍只公开规范键。`XImageBuiltinHandler`
  新增由处理器独占的 `m_subType` 字符串；显式创建时保存请求的 PPM 子类型，内容创建时
  保存探测出的规范子类型。`VXImageBuiltinHandler_canRead()` 通过不改变设备位置的
  两字节窥视按 `P[1]` 规范化 `format()`/`SubType`；`setOption(SubType)` 深拷贝字符串，
  避免调用方临时格式对象被注册表释放；写入端优先使用该子类型选择 P1-P6 编码，读取端
  继续由统一 PPM 解码器按实际文件头处理。所有新增状态在 Handler 销毁时释放，未引入
  平台 API 或标准库内存分配。
- **回归覆盖**：`xgui_regression_test.c:test_codec_ppm_family()` 在插件开启时写入
  P6 临时文件，用显式 `ppmraw` 创建读取器，断言 `canRead()` 成功后子类型规范化为
  `ppm` 且像素正确；再用显式 `pbmraw` 写出 P4 文件，并用同一别名读取，断言尺寸及黑白
  像素保持。既有 P1-P6、截断正文、maxval、注释和公共格式列表夹具继续运行。默认
  `build` 与 `build-crop-plugin-off` 均完成目标构建、直接回归和 CTest（各 1/1 通过），
  默认测试产物已恢复，`git diff --check` 通过。
- **验证与边界**：两套全量构建均成功；构建日志保留仓库既有 zlib、XClass/const 等告警，
  运行时保留既有 XError 诊断。当前环境没有可用的 Valgrind/LSan 全局证据，不能宣称
  零警告或全局无泄漏。raw 别名仍不加入公共支持格式/MIME 列表；动态第三方插件、SVGZ
  压缩写出、原生 QPicture/QDataStream 互操作及高级色彩元数据仍按既有裁剪边界处理。

### 10.467 2026-09-02 QPpmHandler 子类型大小写与自引用生命周期

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qppmhandler.cpp:547-551`
  的 `setOption(SubType)` 对传入 `QByteArray` 执行 `toLower()`；`qppmhandler.cpp:510-519`
  的 `option(SubType)` 返回处理器当前子类型，`qimagewriter.cpp:660-673` 在写出前将
  所有选项逐一传递给处理器。选项字符串是值语义，处理器不能依赖调用方临时对象的
  生命周期，也不能在输入与内部值为同一对象时先释放源值。
- **实现范围**：`VXImageBuiltinHandler_setOption()` 对 PPM `SubType` 先通过
  `XString_toLower()` 生成独立副本，副本成功后再替换旧值；因此显式 `PBMRAW`、
  `PgmRaw` 等输入均保存为 Qt 规范的小写键，且把 `option(SubType)` 返回的借用指针
  原样传回 `setOption()` 时不会发生悬空读取。PNG `Description` 同样改为先复制后替换，
  统一处理自引用生命周期；创建时显式 PPM 子类型也改为小写保存。未引入平台 API 或
  标准库内存分配。
- **回归覆盖**：`xgui_regression_test.c:test_codec_ppm_family()` 新增内置处理器夹具，
  以 `PBMRAW` 创建写处理器，取得借用 `SubType` 后原样回传，再断言查询值为 `pbmraw`；
  既有 `ppmraw` 读取规范化、`pbmraw` P4 写出及公共列表隐藏 raw 别名测试继续覆盖。
  默认 `build` 与 `build-crop-plugin-off` 均完成全量构建，默认直接回归通过，插件裁剪
  CTest 通过（1/1）；默认测试产物已恢复，`git diff --check` 通过。
- **验证与边界**：构建仍保留仓库既有 zlib、XClass/const 等告警及运行时 XError 诊断；
  当前环境无可用 Valgrind/LSan 全局证据，不能宣称零警告或全局无泄漏。PPM raw 别名
  仍不加入公共 supported formats/MIME，动态第三方插件、SVGZ 压缩写出及原生
  QPicture/QDataStream 互操作继续按既有裁剪边界处理。

### 10.468 2026-09-02 QImage 色彩空间转换入口审计

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qimage.cpp:5016-5185`
  定义 `setColorSpace()`、`convertToColorSpace()` 及两个
  `convertedToColorSpace()` 重载的前置条件：空图像不操作，无效目标拒绝，源与目标
  色彩模型不兼容时选择可表达的中间像素格式；相同色彩空间保持共享数据，显式目标
  格式仍执行格式转换。
- **审计结果**：`Src/XGui/Graphics/XImage.c:859-1908` 已覆盖上述分支。设置色彩空间
  仅分离元数据；转换入口检查源空间有效性、目标空间有效性和目标像素模型，并在
  RGB/灰度/CMYK 间选择 ARGB32、Grayscale8 或 CMYK8888 中间格式。原生 16 位、浮点
  和索引调色板路径分别保留通道精度、Alpha 和颜色表语义；相同空间的输出继续共享
  图像数据。C99 的 `convertToColorSpace*` 返回 `bool` 以报告便携层失败，成功路径与
  Qt 的原地/返回新图像语义一致。
- **回归覆盖**：`xgui_regression_test.c:14372-14963` 覆盖无效空间、模型兼容性、
  RGB/灰度转换、16 位低位精度、浮点 HDR、索引调色板、ICC/LUT 侧车及缓存键保持。
  默认 `build` 与 `build-crop-plugin-off` 均完成全量构建，直接回归和 CTest 均通过；
  默认测试产物已恢复，`git diff --check` 通过。
- **边界**：XColorSpace 仍是便携值类型，未引入 Qt 私有 `QColorTransform`/平台色彩
  管线；ICC/LUT 资源保留在 XImage 侧车但不执行完整 Qt 色彩管理后端。原生 QPicture
  数据流互操作、第三方插件和 SVGZ 继续按既有裁剪边界处理。

### 10.469 2026-09-02 图像插件键大小写敏感性

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/plugin/qfactoryloader.cpp:348-367`
  将插件元数据 `Keys` 按 `Qt::CaseSensitive` 直接写入键表；`qfactoryloader.cpp:428-433`
  仅在构造 loader 时显式选择不敏感模式才执行 `toLower()`，图像处理器 loader 使用默认
  大小写敏感模式。`qimagereader.cpp:151`、`qimagewriter.cpp:104` 则只把用户传入的
  格式值转小写后再查键表，因此大写插件元数据键不会因用户输入规范化而自动变成可见键。
- **实现范围**：`Src/XGui/Graphics/XImagePluginRegistry.c:188-194` 将插件键匹配从
  `XChar_CaseInsensitive` 改为 `XChar_CaseSensitive`；用户格式仍由既有
  `normalizedFormat()` 统一转小写，内置 PPM raw 别名保持显式例外。公共格式/MIME 列表
  继续按 Qt 辅助层规则排序去重，不改变插件发现回调和注册顺序。
- **回归覆盖**：`xgui_regression_test.c:test_image_plugin_registry_integration()` 增加
  `UPPERMOCK` 元数据键夹具，查询小写 `uppermock` 时断言读写能力均为假；随后移除该插件
  再继续既有 `mock` 插件覆盖，确保测试不会污染全局注册表。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 均完成 `XGuiRegression_Test`
  目标构建，直接回归及 CTest（各 1/1）通过；默认产物在最终检查前恢复。`git diff --check`
  通过。构建仍保留仓库既有 XClass/const、zlib 告警及运行时 XError 诊断；当前环境没有
  可用 Valgrind/LSan 的全局无泄漏证据，不能宣称零警告或全局无泄漏。
- **边界**：该修正只覆盖插件元数据键与 Qt loader 的大小写语义；内置包装插件仍按
  XGui 的裁剪格式表提供 JPEG/GIF/SVG/ICO 等能力，动态插件目录扫描、原生 QPicture/QDataStream
  互操作及完整 ICC 色彩管理仍按前述裁剪边界处理。

### 10.470 2026-09-02 QIcon 文件构造的 @Nx 高 DPI 兄弟查找

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1178-1181`
  在 `QPixmapIconEngine::addFile()` 登记基础文件后追加 `qt_findAtNxFile()` 返回的
  高 DPI 兄弟；同文件 `:2015-2047` 定义候选规则：目标 DPR 小于等于 1 时不查找，
  首候选为 `min(ceil(targetDpr), 9)`，依次尝试 `@Nx` 到 `@2x`，无扩展名时追加到
  文件末尾，`.9.*` 九宫格资源则将插入点移到 `.9` 之前；
  `QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING` 非空时通过函数内静态值一次性禁用。
- **实现范围**：`Src/XGui/Icon/XIcon.c:153-209` 新增纯 C99 的 `XIcon_findAtNxFile()`，
  使用 `XString_toUtf8()`、固定边界缓冲区和 `XFile_exists_static()` 生成并探测候选，
  保留 2..9 逆序、扩展点和 `.9.*` 规则，拒绝非有限 DPR 与超长路径，不引入标准库
  分配或平台 API。`XIconPrivate_addAtNx()` 在 `XIcon_init_file()` 和 `XIcon_addFile()`
  的基础登记后追加同尺寸/模式/状态的兄弟；自定义引擎遵循 Qt 的 `key()=="svg"`
  提前返回边界，其他引擎也追加候选。主屏 DPR 取 `XGuiApplication_devicePixelRatio()`，
  `XGUIAPPLICATION_ON=0` 时退化为 1.0。
- **回归覆盖**：`xgui_regression_test.c:test_gui_application_contract()` 的主屏 DPR=2
  夹具写入 2x2 基础 BMP 与 4x4 `@2x` BMP，验证文件构造的 availableSizes 同时包含
  两个逻辑尺寸，并断言 `XIcon_pixmapRatio(..., 2.0f)` 选中 4x4、输出 DPR=2.0；测试
  结束删除临时文件，避免污染后续图像用例。
- **验证结果**：默认 `build` 与 `build-crop-plugin-off` 均完成
  `XGuiRegression_Test` 目标构建，直接回归和 CTest 各 1/1 通过；随后重新构建默认
  目标并恢复 `bin/XGuiRegression_Test`。构建保留仓库既有 zlib、XClass/const、信号宏
  告警和运行时 XError 诊断。现有 `build-asan` 以 ASan/UBSan 运行本夹具，功能断言
  通过且未报告越界/未定义行为；LeakSanitizer 报告仓库既有约 102319 字节、466 次
  分配未释放（字体配置、fontconfig 与 Reader 文本缓存），因此不能宣称全局无泄漏；
  当前环境未安装 Valgrind。
- **边界**：当前候选查找依赖 XGui 文件后端的 `exists` 语义，不扩展 Qt 资源系统的
  `:/` 虚拟路径解析；SVG 引擎保持 Qt 的“不追加 @Nx”边界。环境变量采用图标模块
  独立的一次性静态缓存，与 Reader 的同名缓存分开；动态插件目录扫描、原生
  QPicture/QDataStream 互操作及完整 ICC 色彩管理仍按前述裁剪边界处理。

### 10.471 2026-09-02 QIcon::paint 奇数尺寸居中取整

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/qicon.cpp:1030-1038`
  的 `QIcon::paint()` 先调用 `rect.size().width()/2 - size.width()/2` 和
  `rect.size().height()/2 - size.height()/2` 分别计算居中偏移，而不是对宽高差
  整体除二。奇数矩形与奇数图标尺寸时，两种写法可能相差一个像素；此前 Qt
  `visualAlignment()` 已在 `XIcon_paint()` 中完成方向和默认左对齐处理。
- **实现范围**：`Src/XGui/Icon/XIcon.c:1301-1308` 的水平/垂直居中改为先分别对
  区域和实际图标尺寸做 C99 整数除法，再相减；右、底对齐及 RTL/Absolute 分支
  保持原语义，不引入平台 API 或额外分配。
- **回归覆盖**：`xgui_regression_test.c:test_icon_paint_visual_alignment()` 新增
  6x6 区域绘制 3x3 图标夹具，断言图标从 `(2,2)` 开始且 `(1,1)` 保持背景色，覆盖
  奇数尺寸居中的 Qt 取整边界；原有 LTR、RTL、默认、Absolute 以及无 save 时不
  调用 restore 的夹具继续执行。
- **验证结果**：默认 `build` 和 `build-crop-plugin-off` 均完成目标构建；默认及
  裁剪直接回归、CTest 各 1/1 通过，默认 `bin/XGuiRegression_Test` 已在裁剪验证
  后恢复；重新配置过期的 `build-crop-ppm-off` 后，PPM 关闭裁剪目标、直接回归与
  CTest 亦各 1/1 通过。构建保留仓库已有 zlib、XClass/const、信号宏告警与运行时
  XError 诊断。
  `build-asan` 的 ASan/UBSan 功能检查通过，LSan 仍报告既有约 102319 字节、466 次
  分配泄漏，当前环境未安装 Valgrind，不能宣称全局无泄漏。
- **边界**：该修正只涉及图标绘制目标矩形的整数居中；主题引擎物理 DPR、动态插件
  发现、资源路径解析、原生 QPicture/QDataStream 互操作和完整 ICC 色彩管理仍按
  前述裁剪边界处理。

### 10.472 2026-09-02 QPainter::testRenderHint 组合掩码语义

- **Qt 依据**：Qt 6.8 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/painting/qpainter.h:407`
  将 `testRenderHint(RenderHint)` 内联为 `bool(renderHints() & hint)`；
  `qpainter.cpp:6903-6906` 的 API 文档也明确其含义为“指定提示是否已设置”。
  因此传入多个位时只要任一位已设置即返回真，零掩码返回假；该行为不同于要求
  所有位同时命中的集合包含判断。
- **实现范围**：`Src/XGui/Graphics/XPainter.c:5975-5981` 改为对当前提示集合执行
  非零按位与，保留未激活绘制器返回假的 Qt 默认语义，不引入平台 API 或分配。
- **回归覆盖**：`xgui_regression_test.c:5887-5893` 在激活绘制器上新增组合掩码和零掩码
  断言，结合既有单个位设置/清除、Picture 回放及裁剪宏测试，固定任一位命中规则。
- **验证结果**：默认 `build`、`build-crop-plugin-off` 和 `build-crop-painter-off` 均完成
  目标构建，直接回归和 CTest 各 1/1 通过；PPM 关闭裁剪在重新配置过期 CMake 目录后
  也完成目标构建、直接回归和 CTest 各 1/1 通过。构建保留仓库既有 zlib、XClass/const、
  信号宏告警及运行时 XError 诊断；ASan/UBSan 功能检查通过，LSan 仍报告既有约 102319
  字节、466 次分配泄漏，当前环境未安装 Valgrind，不能宣称全局无泄漏。
- **边界**：该修正只影响 `testRenderHint` 的组合掩码判定，不改变 `setRenderHint(s)`
  的状态记录、便携 Picture opcode 或各后端实际抗锯齿/平滑实现；动态插件、原生
  QPicture/QDataStream 互操作和完整 ICC 色彩管理仍按既有裁剪边界处理。
