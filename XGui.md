# XGui 进度文档

> 最后更新：2026-08-23 Asia/Shanghai
> 职责：记录 XGui（对标 Qt 6.8.3）当前实现进度、已知问题与下一步。
> 本文件面向“更换 AI 继续”场景，所有定位信息均为当前仓库实测事实。

## 1. 当前任务目标

XGui 布局系统 **100% 对齐 Qt 6.8**：PC 端全功能可用，嵌入式通过
`Src/XGui/XLayout/XLayout_config.h` 的宏开关裁剪扩展功能。

- 总开关：`XLAYOUT_ON`（在 `Src/CXinYueConfig.h` 统一定义）
- 子开关：`XLAYOUT_BOX_ON` / `XLAYOUT_GRID_ON` / `XLAYOUT_SPACER_ON` /
  `XLAYOUT_TOTAL_ON`（PC 桌面扩展 API，关闭可裁剪嵌入式体积）

## 2. 仓库状态

- HEAD：`d73aeb26` feat(XGui): 完整实现 XImageCodec 五格式编解码并整体对齐 Qt 图像体系
- 工作树：**保留全部改动（约 80 项）**，不清理、不丢失、不 push
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
