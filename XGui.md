# XGui 进度文档

> 最后更新：2026-08-26 Asia/Shanghai
> 职责：记录 XGui（对标 Qt 6.8.3）当前实现进度、已知问题与下一步。
> 本文件面向“更换 AI 继续”场景，所有定位信息均为当前仓库实测事实。

## 1. 当前任务目标

XGui 布局系统 **100% 对齐 Qt 6.8**：PC 端全功能可用，嵌入式通过
`Src/XGui/XLayout/XLayout_config.h` 的宏开关裁剪扩展功能。

- 总开关：`XLAYOUT_ON`（在 `Src/CXinYueConfig.h` 统一定义）
- 子开关：`XLAYOUT_BOX_ON` / `XLAYOUT_GRID_ON` / `XLAYOUT_SPACER_ON` /
  `XLAYOUT_TOTAL_ON`（PC 桌面扩展 API，关闭可裁剪嵌入式体积）

## 2. 仓库状态

- HEAD：`73329dbd` feat(usb): 统一 USB Host/Gadget 设备层并接入 Windows/Linux/TinyUSB 平台后端
- 工作树：**保留全部改动（约 25 项）**，不清理、不丢失、不 push
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

### 3.1 图像体系（已完整提交 HEAD `d73aeb26`）

- XImageCodec 五格式完整编解码：BMP（24/32 无压缩正/倒序）、PNG（8 位
  0/2/4/6 型、反滤波 0~4、无 Adam7、无调色板型）、JPEG（基线 SOF0、
  YCbCr/灰度、1/2/4 抽样、DRI；编码固定 4:2:0）、GIF（静态首帧、全局/
  局部调色板、透明色、GIF89a 编码）、SVG（内嵌 PNG 位图 + 纯色矩形）
- JPEG/GIF/SVG 等“扩展能力”通过配置文件开关可裁剪（PC 全开）
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
`QTextControl`，且 `XFocusReason` 没有 Qt 的 `PopupFocusReason`，因此文本选区保留、快捷键导航与上下文菜单仍属嵌入式近似。默认与裁剪构建、`XGuiRegression_Test`、CTest 均通过；构建输出仍保留仓库既有警告，LSan 受受控环境 ptrace 限制，不宣称零警告或零泄漏。未提交、未推送。

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
