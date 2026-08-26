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
  默认方向为 `LayoutDirectionAuto`，setter/getter 直接读写 painter state。
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
- `Src/XGui/Graphics/XPainter.c`：默认状态为 Auto；setter 对非法枚举值回退
  Auto；`drawTextRect` 按 ForceLTR/ForceRTL、绘制器方向或首个强 RTL 码点
  决定方向，再执行视觉左右交换和 Qt 条件制表符展开。
- `xgui_regression_test.c`：增加默认 Auto、RTL 左对齐翻转、
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
  引入对应 XRegion/XPainterPath 裁剪接口。
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
