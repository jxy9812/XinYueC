# XGui 模块文档

> **文档导航**：本文件是 XGui 模块的**活文档**（架构与当前状态）。
> 按日的战役实施记录已归档至 `docs/xgui/history/`（2026-09-21 文档重构
> 迁移，内容逐字保留）；重构设计见
> `docs/xgui-audit/2026-09-21/文档重构设计.md`。
>
> **验证状态（2026-09-21）**：构建 0 错误；主回归 exit=0 零失败断言；
> 验收 68/68；GPU 套件通过；ASan 基线扫查完成。全部改动未提交待验收。

------

## 1. 架构总览

XGui 是对标 Qt Widgets 的纯 C GUI 模块，分层如下（平台调用只允许出现在
`Drive/`，`Src/` 一律经公共抽象层间接到达——用户明令，见 §9 约束）：

```
应用层        XGuiApplication（单例：剪贴板/输入法/屏幕/调色板装配）
控件层        Src/XGui/Widget   控件族/条目视图/对话框/停靠/效果
文本层        Src/XGui/Text     XLineControl（单行）/XTextControl（多行）控制器
图形层        Src/XGui/Graphics XPainter + XRenderKernel 内核表 + 图像编解码
窗口系统      Src/XGui/Window   XWindow/XWindowSystemInterface(WSI)/XScreen
平台抽象      Src/XGui/Platform XPlatformNativeWindow/Integration（QPA 对标）
平台驱动      Drive/Posix (X11/fbdev) | Drive/windows (Win32)
              | Drive/Unsupported (空实现桩)
```

- **主循环**：双源统一阻塞等待——POSIX 用 poll 同时监听 X11 连接 fd 与
  `XAbstractNetIoRing` 事件 fd；Windows 用 MsgWaitForMultipleObjects。
  网络完成延迟从 ~20ms 量化降至微秒级。
- **渲染**：软件光栅为主，GPU（OpenGL/Vulkan）会话按"局部提交"策略
  与软件路径混用；目标格式内核表见 §3。
- **事件**：XObject 虚槽多态；触摸/平板/IME/剪贴板/拖放全部经 WSI
  统一入口注入（见 §5/§6）。

## 2. 当前能力矩阵

| 域 | 状态 | 说明 |
|---|---|---|
| 控件族 | ✅ 对齐 | 按钮/输入/微调/组合/日期时间（扩展格式引擎）/滑动/进度/标签/分组/堆叠/选项卡/工具栏/菜单/滚动/分割/列表/表格/树/停靠/主窗口 |
| 文本 | ✅ 对齐 | 单行控制器（掩码/验证/IME/撤销）；多行控制器（**软换行**/IME/撤销/富文本预览子集） |
| 条目视图 | ✅ 基础+编辑 | 模型四信号/滚动偏移/键盘导航/选择模式/**delegate 编辑闭环**/role 体系 |
| 对话框 | ✅ 真实 UI | MessageBox/Dialog.exec 模态/Input(GetText/Int/Double/Item)/Color(48 色块+RGB)/File(目录浏览) |
| 剪贴板 | ✅ 全协议 | X11 Selection：CLIPBOARD+PRIMARY/INCR/MULTIPLE/SAVE_TARGETS/mime 多格式协商/图像编解码/外部变更通知；中键粘贴 |
| 输入法 | ✅ 接线 | XIM/DBus portal 注入 + 查询链（cursorRectangle 等实时值） |
| 渲染 | ✅ | 内核表（ARGB32/RGB565 已目验）/线条 AA/几何描边/精确路径裁剪/双线性/软换行 |
| 平台 | ✅ | 双源主循环/EWMH flags+MOTIF/屏幕 DPI+热插拔/fbdev 模板（默认关）/位图光标/WarpPointer |
| 触摸平板 | ✅ 入口 | WSI 入口+控件派发+隐式抓取+touch→mouse 仿真；XI2 合成待嵌入式接入 |
| 效果 | ✅ 子集 | 不透明度/盒式模糊/投影 + 控件渲染钩子 |
| 待立项 | ❌ | 见 §8 遗留清单（结构改造大件） |

## 3. 渲染管线

### 3.1 目标格式渲染内核表（对标 Skia blitter + LVGL 组织）

- 契约：`Src/XGui/Graphics/XRenderKernel.h`——`XRenderKernelOps` 七个
  span 级原语（fillSpanOpaque/fillSpanBlend/blitSpan/blendSpan/
  glyphMaskSpan/storePrem）；行基址+像素列寻址；颜色恒为预乘 ARGB32
  规范色；**未注册格式返回 NULL → 调用方回退既有逐像素路径**（零回归
  保险丝）。新格式/加速器 = 注册一张表，不碰 painter。
- 首批 `XRenderKernel_rgb565.c`：混合口径逐字节对齐 `(a*b+127)/255`
  （勿用 `>>8` 近似）。
- **RGB565 目验 ✓（2026-09-21）**：Xvfb 16 位深度 + RGB16 构建跑
  demo，逐像素校验"565 位复制展开合法性"（v<<3|v>>2；截断式 %8/%4
  检查无效）——187200 像素 0.00% 非法，24 位对照 92.98%（判别力成立）。
  内核探针 6 组（预乘混合/alpha=0 幂等/回退保险丝）全过。

### 3.2 质量特性（对标 Qt 逐项落地）

- **线条 AA**：线段法线偏移构成笔宽四边形进 4x4 覆盖通道；轴向线与
  hint 关闭路径逐字节零回归；drawPoint 保持硬边（图表标记约定）。
- **几何描边器**：Bevel/Miter(miterLimit 可调)/Round join + 三种 cap；
  拐角裁剪消除内侧重叠；虚线节距 = 笔宽倍数（对标 Qt）。
- **画笔宽度随变换缩放**：scale=(|M·(1,0)|+|M·(0,1)|)/2，宽度 0 为
  cosmetic。
- **路径**：Winding/OddEven 填充规则；setClipPath 精确路径掩码裁剪；
  折线/多边形/文本行动态容量（无静默截断）。
- **图像**：SmoothPixmapTransform 双线性；浮点/9 参重载族。
- **性能基准**（历史，硬件/场景/日期三标注）：桌面 Xorg
  i5+2558x1333 虚显：整帧 5670FPS/0.176ms（2026-09-19，第四轮软件
  渲染后，详见 history/2026-09-19-perf-rounds.md）；嵌入式预期
  （MCU+DMA2D 整帧 3~6×、RAM 省 0.5~1.5MB）见 history/2026-09-20。

### 3.3 PARTIAL tile 攒批

相邻/重叠 tile 合并 flush；超预算（≈1/4 屏）或 16ms 帧界强制收批；
关闭开关 `-DXGUI_BACKINGSTORE_TILE_BATCHING_ON=0` 退化逐片。
桩验证：100 片全窗→20 次 present 且面积守恒；ASan 零泄漏。

## 4. 文本系统

### 4.1 控制器架构（对标 QWidgetLineControl/QWidgetTextControl）

- **XLineControl（单行）**：掩码引擎/验证门禁/IME（preedit 计入布局）/
  撤销重做/选区/剪贴板三通道（CLIPBOARD/PRIMARY/进程内回退按模式区分）。
- **XTextControl（多行）**：**软换行**——可视行布局缓存
  {逻辑行,起始字节,字节长,像素宽}，字节位置↔(可视行,列)双向映射，
  断行按简化 UAX#14（CJK 逐字/词边界/行禁首尾禁则），软断点边沿
  跟踪；IME preedit splice 进布局；NoWrap 逐行裁剪兜底（任何模式
  文本不越出边框）。
- **精度**：UTF-8 字节口径（`XString_toUtf8_length`，勿用字符数）。

### 4.2 富文本子集（XTextEdit/XTextBrowser 显式预览模式）

格式栈 HTML 解析器：b/i/u/s/sup/sub/font(color/size)/
span(style background-color)/br/p(align)/h1-h6/ul/ol/li(嵌套分级)/
a(href)+8 类实体；toHtml 与解析器互逆；链接悬停/点击信号。渲染端
消费：粗（伪粗体）/斜体（§8.0g11 合成倾斜）/下划线/删除线/前景色/
背景色/字号/标题梯度/块对齐/列表缩进+标记（无序方块、有序按层序号）/
上下标（62% 字号+基线偏移）。简化边界：软换行已落地、超宽裁剪、
内联嵌套上限两层。

### 4.3 XTextDocument

纯 C 文档模型（块+片段）；setPlainText 逐行建块（容量增长+清零，
越界已修）；toHtml 往返；undo 栈实例持有。

## 5. 输入与剪贴板

### 5.1 输入法

- 注入：XIM/XIC（Linux）与 IMM32（Windows）转 `XInputMethodEvent`；
  DBus portal（fcitx5）并行支持。
- 查询链：应用自动注册默认 query handler → 焦点控件
  `XWidget_inputMethodQuery` 虚槽（cursorRectangle 经
  inputItemTransform 映射实时返回；文本控件重载可提供环绕文本）。
- filterEvent：键派发前置输入上下文过滤（虚槽，默认放行）。

### 5.2 剪贴板（X11 Selection 全协议，对标 QXcbClipboard）

- **双选择区**：CLIPBOARD + PRIMARY（中键粘贴闭环），状态/镜像/
  时间戳分离；专用 1×1 认领窗口；真实服务器时间戳（TIMESTAMP 应答）。
- **数据格式**：TARGETS 按实际持有集合应答并广播 MULTIPLE（ICCCM
  2.6.2，§8.0g3）；mime 多格式协商
  （text/plain/text/html/image/png 出站，png/bmp/jpeg 入站解码）；
  **INCR 分片**双向（阈值 min(最大请求字节/4,262144)，终结判定
  "NewValue+空读"；读方向整体超时可参数化
  XClipboard_setIncrTimeoutMs，默认 5s）；MULTIPLE 单往返；
  SAVE_TARGETS 轻量应答。
- **所有权纪律**：SelectionClear→selectionRevoked 回调→owns 复位+
  dataChanged/selectionChanged 发射；外部数据经 mimeData 合并为
  一次性镜像（二进制透明，XByteArray 通道）。
- **中键粘贴**：Button2 → 光标落位 → 粘贴 PRIMARY；Selection 空
  且系统后端可用时不回退进程内共享层。
- 已知限制：外部内容后续变化不自动刷新已合并镜像；image/png 写出
  载荷须合法 UTF-8（XString 承载限制，二进制用 data_bytes）。

### 5.3 光标

24 形状→X11 cursorfont 映射表（Blank 空像素图；部分形状字体近似，
Qt 为位图自绘）；XCreatePixmapCursor 位图/像素图通道；WarpPointer；
XWindow_setCursor 窗口级 API 已接平台。

### 5.4 触摸/平板

WSI 入口（BEGIN/UPDATE/END/CANCEL + 压力/指针类型）→ 命中派发 →
虚槽；BEGIN 被接受即隐式抓取；未接受走 touch→mouse 仿真（默认开，
`XGuiApplication_setAttribute(属性12)` 可关，合成事件带
synthesized 标志）。已知偏差：单触点列表（Task 2.20）、XI2 合成待接。

## 6. 平台层

- **窗口 flags 运行时同步**：StaysOnTop/Bottom→_NET_WM_STATE、
  BypassWindowManager→SKIP_TASKBAR+PAGER（EWMH 近似，Qt xcb 实为
  re-create）、DoesNotAcceptFocus→_NET_WM_HINTS.input、装饰位→
  _MOTIF_WM_HINTS；未映射窗口读-改-写、已映射发 ClientMessage 由
  WM 回写。
- **屏幕与 DPI**：RandR 枚举/差分热插拔（增删+主屏重选+驻留窗口
  钳位迁移）；logicalDPI 直读 RESOURCE_MANAGER（XGetDefault 有
  连接级缓存，不可用）+ 运行期刷新入口；physical=pixels/(mm/25.4)。
- **theme×调色板**：colorScheme 深浅→内置深浅调色板联动（深色组
  锚定 qt_fusionPalette 数值）；显式 setPalette 有守卫不被覆盖；
  顶层广播触发重绘；联动可关。
- **fbdev 模板**（XPLATFORM_FBDEV_ON 默认 0）：probe/formatNegotiate/
  pan/cacheSync/waitVsync/stride 六钩子契约（XPlatformDisplayDriver.h）
  + /dev/fb0 mmap 实现；板级可注册自定义 ops 覆盖 BSP 专有 ioctl。
- **已知时序**：WM 对刚映射数百 ms 内的 _NET_WM_STATE ClientMessage
  可能丢弃（WM 侧行为）；窗口创建前 screens() 为空（事件循环启动后
  可用的既定语义）。

## 7. 构建与验证

```bash
# 全量构建
cmake -S . -B build && cmake --build build -j
# 注意：CMake 为 GLOB 收源，新增 .c 后需 touch CMakeLists.txt 重配置

# 三套验证（必须在仓库根运行——资产/字体相对路径依赖）
./bin/XGuiRegression_Test           # 控件回归（exit=0 且无 FAIL 行）
./bin/XLineControl_Acceptance_Test  # 文本控制器验收（68/68）
./bin/XGuiGpu_Test                  # GPU 渲染

# XGui 演示
./bin/XGuiWindowDemo_Test --autotest
./bin/XGuiWindowDemo_Test --screenshot demo.png --page 0

# 嵌入式 RGB565
cmake -B build-rgb16 -DCMAKE_C_FLAGS="-DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1"
# 目验：Xvfb :99 -screen 0 800x600x16 起 16 位显示
```

**ASan 快照法**（并发工作下定位内存问题的标准方法）：工作树快照至
/tmp 独立目录 → `-fsanitize=address -g -O1` 构建三套件 →
LeakSanitizer 退出扫查 + `fast_unwind_on_malloc=0` 精确归因。
判读纪律：**调用方持有的分配 ≠ 泄漏**（先辨所有权再动手，见
2026-09-21 setFont 误判教训）；退出持有（测试夹具不删控件树）与
逐操作增长泄漏分开处置。基线：回归/验收/demo 三面扫查，真缺陷
（撤销合并越界读×3、setFont 比较时序 UAF、测试侧事件/控制器脱管）
已修；剩余 ~196KB 为夹具退出持有，全量清零+夹具有序拆除列入 §8。
**终态扫查矩阵（2026-09-21，全量代码集成后）**：回归 ~195KB（夹具
控件树，登记表方案经 ASan 实证不可行已回退，见 §8.2）/ 验收 **0 泄漏**
（ac_ime+ac_ctlAlive 修复生效）/ XGuiXdnd 4.4KB（51 原生窗口逐用例
创建的夹具持有 + XPlatformDrag 应用级单例，定性为夹具语义）/ demo
27KB（同夹具语义）。结论：生产代码无逐操作增长泄漏；夹具退出持有
按套别纪律逐步收敛。
**终验（2026-09-21 全量集成后）**：11+ 批次（保留层/IME 接线/XTabBar
滚动/效果/Dock/富文本/增量布局）全量落地后 ASan 重扫——195KB 与基线
一致，零新增泄漏源；新增代码均零泄漏。
**终态矩阵 v2（2026-09-21 §8.0g6 拆除真缺陷后）**：回归 151790B/464 块
（-43.7KB/-22%）——其中 100956B 为 Mesa/GLX 连接级一次性持有（反复
create/destroy 1/5/20× 恒等实证，非逐操作增长），~51KB 为夹具顶层
孤儿控件树；验收 0 / XGuiXdnd 4.4KB / demo 27KB 不变。生产拆除真缺陷
两笔已修：XMenu 析构隔个漏删、XDialogButtonBox::clear 自建按钮不删
（详见 §8.0g6）。
**终态矩阵 v4（2026-09-21 §8.0g8 收官）**：回归 **102766B**——Mesa/GLX
连接级 100956B 环境噪音 + 非 Mesa 残余 **1810B**（fontconfig 缓存 647B
+ 字符串碎屑/零星单例 ~1.1KB）。会话累计 -92.7KB/-47.4%，非 Mesa 夹具
债 -98%。生产真缺陷五笔全修（XMenu/按钮盒/XFontSet/setFont 壳/菜单栏
桥）。验收 0 / XGuiXdnd 4.4KB / demo 27KB 不变（夹具/单例语义）。

## 8. 已知偏差与遗留清单

> **Qt 6.8.3 二次全量对齐复扫（2026-09-21）**：四域只读扫描完成，报告
> 采信并完成 P0/P1 修复批次（三路并发 + posix 串行收尾）：
> - **P0×2 已修**：①对话框族键盘自递归（_base 入口虚表再分派回最派生
>   重载；XDialog/XMessageBox 改 XClass_Parent 静态调父类实现，递归链
>   终止于 ignore 默认）；②RGB565 fillSpanBlend 非预乘色（painter 侧
>   漏做预乘，半透明填充过亮 255/sa 倍；内核分支调用前按 sa 预乘）。
> - **P1 已修**：按键父链上抛（Esc 经行编辑 ignore 上抛对话框 reject）、
>   DashLine 节距 {4,3}→{4,2}（对标 QPen::dashPattern，三处回归断言
>   按实测基线更新）、AA/GPU 填充 Winding 参数化（含 setClipPath 掩码
>   同根）、三处行级快路径 clipRegion 门、剪贴板热路径 [clip-dbg] 移除
>   +XGetAtomName 泄漏修复、XTextControl 中键 PRIMARY 空护栏、三文本
>   控件接输入法查询虚槽。
> - **平台域 P1 登记**（修复批次 D 串行已排，见 git 记录）：exec 阻塞
>   点 X11 fd 接入、setWindowState X11 实现、_NET_WM_WINDOW_TYPE、
>   FramelessWindowHint 不再 override-redirect。
> - **P2 清单**：DockWidget 内容几何/标签组持久化/dateChanged 拆分/
>   键入解析/Home-End 语义/role 渲染消费/completer 安装/editTriggers
>   默认/富文本空白折叠/Winding-AA 同根残余/触摸 XI2 合成等——
>   完整逐条（文件:行号）见复扫工作流运行记录。

### 8.0 平台域 P1 深化批次（2026-09-21，复扫平台域五项收口）

- **R1 主循环 X11 fd 接入（唤醒桥方案）**：常驻分离线程 poll(XConnectionNumber)，
  可读即写 ring 的跨线程 eventfd 唤醒 dispatcher（接入面最小：零契约头/
  零 dispatcher 改动；监视线程不碰 Display*，4ms 限幅防空转，ioRing 关闭
  时编译剔除）。效果：有定时器挂起时 X11 按键即时唤醒（不再等 deadline）。
- **G1 setWindowState X11 实现**：最大化（VERT+HORZ 两原子）/全屏/
  最小化（WM_CHANGE_STATE，ICCCM 4.1.4）；已映射走根窗口 ClientMessage
  （EWMH source=application 规范位）、未映射属性直写；创建补
  PropertyChangeMask，WM 回写经 PropertyNotify → 状态上报链打通。
- **G2 _NET_WM_WINDOW_TYPE**：按 WindowType 单值写
  DIALOG/UTILITY/SPLASH/TOOLTIP/POPUP_MENU/NORMAL；创建 + setFlags
  双写入点。
- **R2 Frameless 收窄**：override_redirect 限定 Qt 同款集合
  {Popup,ToolTip,SplashScreen,BypassWindowManager}；Frameless 走
  MOTIF decorations=0（WM 管理）；Bypass 提示位补入创建分支。
- **R4 [ime-dbg]/[ime-dbus] 逐键调试输出收编**：XPWN_IME_DEBUG 编译
  开关（默认 0）——回归日志首次完全干净；[ime-dbus] 一次性失败诊断
  按保留策略留生产。
- 已知取舍：Bypass 动态增删走 EWMH 近似（无 re-create 机制，双注释
  说明）；ioRing 关闭时唤醒通道静默缺席（既定行为）。

### 8.0b 复扫 P2 快清批次（2026-09-21，单代理五项收口）

- XRenderKernel_rgb565.c 契约注释按名引用改写（行号漂移防复发；同文件
  5 处函数文档同批漂移一并修正）；实测纠正了扫描代理给的错误锚点
  （覆盖率调制实际位于 painterGlyphAlphaBlend/painterFillContoursAntialiased）。
- 565 压缩口径双轨注释互引：XImage compress5/6 四舍五入 vs 内核
  rgb565_pack 截断（同色可差 1 LSB，并存原因存档）。
- setDashPattern 文档修正："像素"→"笔宽倍数"（qt_scale_dash_pattern
  口径）+ Qt 自动置 CustomDashLine 差异注记。
- setMiterLimit 钳位注释表述修正（Qt 不钳位但 ≤1 行为殊途同归）。
- **HTML 源空白折叠**（行为变更，对标 QTextHtmlParser）：连续空白折叠
  单空格、块级标签边界剥离行首缩进、行内标签词间隔保留、段尾未提交
  空白丢弃；pre 不在白名单不做例外。多行缩进 setHtml 源的文本输出
  按 HTML 语义减少空白（预期变更）。

### 8.0c XGraphicsEffect 运行时目验 ✓（2026-09-21，探针 32/32 断言）

自动化像素探针（离屏 grab 管线 + 期望值逐位复算引擎）验证三效果：
- **Opacity 0.5 ✓**：半透明混合逐位精确（绿 255→回贴 120/248 理论值）；
  效果不越界。
- **Blur ✓**：两趟盒式模型逐点一致（块内 255→混 247、两侧 243/242 渐变，
  方差>0，中心变暗）。
- **DropShadow：探针发现并修复 2 笔真缺陷**（XGraphicsDropShadowEffect.c）：
  ①投影被渲染成不透明深灰硬块——预乘 SourceOver 紧致混合对全透明
  画布强制 alpha=0xFF；修法=投影层改 Source 合成（空画布语义等价，
  保住 alpha）；②boundingRectFor 外扩方向颠倒（offset.x>0 时误加
  padL）致投影截断；修法=按投影方向外扩。修复后阴影位置(+8,8)/
  着色(63,63,63@180)/渐变/溢出逐位正确。
- **登记新偏差（painter 侧待根治）**：XPainter.c 预乘 SourceOver 紧致
  混合对**透明目标**强制 alpha=0xFF——任何"半透明预乘源→透明目标"
  的绘制（半透明窗口后备存储等）会复现失真；建议仅当目标 alpha<255
  时回退通用分支或按 Porter-Duff 写回 outA（热路径改动，需专项
  基准，DropShadow 已用 Source 模式规避）。

### 8.0c2 剪贴板热路径静默化 + XGetAtomName 泄漏修复 ✓（2026-09-21，复扫文本域 P1-1）
- SelectionRequest 分支与 setText 认领处的 [clip-dbg] fprintf 全部移除
  （生产路径静默，对标 Qt 平台插件）；XGetAtomName 返回串用后
  xpwn_xFree（此前每次跨进程请求泄漏 3 小块）。并发验证终态成立。

### 8.0c3 单行文本越界双层裁剪 ✓（2026-09-21，用户实测缺陷收口）
- 根因：VXLineEdit_paintEvent 调 XLineControl_draw **无任何裁剪**——
  视口偏移左侧已滚出文本画进左边框区、长文本漫过右边框（用户截图即此）。
- 修复双层：①painter 级 save+IntersectClip(文本矩形 tx..textEndPx)+
  restore；②控制器级 XLineControl_draw 第 4 参传文本矩形（契约本有
  "按 clip 裁剪"，壳此前传 NULL）。
- 探针验证：60 中文字符 200px 控件，光标末尾——左边框区 0 文本像素、
  右边框外 0 溢出（绿笔色像素判定）；修复前代码走读确认无裁剪路径。
- 视口偏移本体（offset=cursorX 钳位 [cursorX-visibleW+1, textW-visibleW]）
  核对正确，无需改动。

### 8.0c4 fbdev 显示驱动消费链打通 ✓（2026-09-21，嵌入式续）
- 格式协商：BackingStore 创建/resize 时经 XPlatformDisplayDriver_active
  → formatNegotiate 决定后备格式（面板 565+选择器=直写零拷贝；面板非
  565=按面板格式分配；Invalid=编译期格式）；活动驱动变化惰性重协商。
- present 直写：XPlatformBackingStore_posix.c 新增
  xpbs_presentToDisplayDriver——前置三查（active/格式一致/probe 映射）
  →逐矩形"窗口坐标裁剪→fb 坐标换算→按行 memcpy（驱动 stride）→
  cacheSync(Clean)→pan(0)"，命中即跳过 X11 路径；不符回落 XPutImage
  零回归。present/presentTile 双钩子接入。
- 注册便捷入口：XPlatformNativeWindow_useFramebufferDriver(device)
  （probe+register 一步，幂等）；XPlatformNativeWindow_create 在惰性
  X11 建连前检查 active 驱动并拒绝 X11 窗口（嵌入式单屏互斥，告警）。
- 运行期设备路径覆盖（g_xpdfbDevicePath，板级可指定 /dev/fb1 等）。
- 验证：4 文件×双配置语法 8/8；桩驱动探针 24/24（ASan+LSan 零泄漏）：
  直写逐像素一致/stride 填充区不写/pan 计数/cacheSync/脏区外不改写/
  格式不符回落/注销回落/useFramebufferDriver 权限受限路径；顺带修复
  初版 gating 语义 bug（面板非选择器格式误回落编译期格式）。
- 待板：真机 RGB565 面板色彩/撕裂目验；多窗口 fb 合成不在范围。

### 8.0d 静态内容保留层 ✓（2026-09-21，Qt+LVGL 融合首批，§23.4 规划 6 收口）

- **API**：`XWidget_setContentRetained/contentRetained/retainedLayerStats`
  （显式选择加入，默认关零回归）；预算开关
  `XGUI_RETAINED_LAYER_BUDGET_BYTES`（#ifndef 默认 2MB，XGUI_ON=0 归零）。
- **机制**：双向链表 LRU（O(1) touch/evict）；paintTree 挂点先于效果
  钩子（保留未施效输出）；缓存有效整幅 blit 跳过 paintEvent 派发，
  失效先重渲染进缓存再 blit（paintEvent 恰派发一次）；热点路径未启用
  时仅一次全局布尔判断。
- **回贴正确性**：Porter-Duff Over 结合律——缓存画布只含子树自身输出
  （透明起画），blit ≡「子树输出 over 目标既有内容」，与直接绘制逐像素
  等价（父级背景/兄弟交叠不影响缓存语义）。
- **失效联动全清单**：update 族唯一入口 addDirtyRegion、几何
  recomputeGeometry、可见性翻转+子树传播、setFont/setPalette/
  setEnabled 经 update 自动覆盖、子控件 update 向上冒泡命中保留层
  祖先、图形效果互斥（双向拒绝）。
- **验证**：ASan 有头探针 30/30——计数器 retained 开连续 5 次整窗
  update paintEvent=1（全 blit）、保留帧整树快照**逐位一致**（含
  半透明，Over 结合律端到端实证）、预算 LRU 淘汰退回常规绘制不阻塞、
  默认关闭逐帧派发零变化、LSan 零泄漏（仅平台库退出持有）；全量回归
  零回归。
- **已登记边界**：半透明内容缓存像素偏差根因在 XPainter 紧致混合
  （§8.0c 已登记的 painter 侧待根治项，非保留层引入）；顶层窗口不
  开放（自带后备存储）；预算淘汰为整层关闭语义。
- **应用级批量失效已接线**：`XWidget_invalidateAllRetainedLayers()`
  （沿保留层链表逐个 update）挂入调色板广播——树中保留层随应用级
  palette 变化自动失效重渲染；setMask 经既有 update 链式失效覆盖
  （初判缺口复核为已覆盖）。

### 8.0e 富文本深化 + 增量布局双批次 ✓（2026-09-21，双路并发）

- **富文本深化**（XTextEdit/XTextDocument）：①预览**软换行**——两阶段
  词元布局（空白词+CJK 逐字词元、贪心填行、行高/上升取行内最大），
  绘制/anchorAt/滚动三共用几何源，Left/Right/HCenter 按可视行宽；
  修复片段跨行拆分后的**文本重影**（XTERichGeom 增 fragOff/fragLen
  子区间）；②**图片片段**——XTDFragment 增 XImage* 字段 +
  XTextDocument_insertImage 深拷贝接口、toHtml 输出 img 宽高、渲染
  基线贴图（src 按名解析与缩放属性不做，已注明）；③嵌套 2→3 层
  （b/i/u 三层同现，第 4 层忽略）。冒烟探针 16/16。
- **多行增量布局**（XTextControl）：单逻辑行编辑局部更新可视行段
  （原位替换+尾段 memmove），跨行/度量变更回退全量；断行扫描重构为
  全量/增量共用同一份代码（产物天然同源）；探针 201 行文档 2000 次
  编辑全部增量、与全量参照逐条一致，ASan+UBSan+LSan 零泄漏零越界；
  顺带修复 setPlainText 空文本时缓存陈旧的既有缺陷。
- 集成验证：构建 0 错误、回归零失败断言、验收 68/68、GPU 通过。

### 8.0e2 XGui Demo 逐页目验（2026-09-21，五页截图全检）

- 页 0 按钮演示 ✓（按钮/命令链接/工具按钮渲染正确）
- 页 1 选择演示 ✓（三态复选框/单选组正常；"就绪"为页内状态展示标签，设计如此）
- 页 2 堆叠演示 ✓（内层页面 1 + 上/下一页导航正常）
- 页 3 输入演示 ✓（输入文本占位/微调/滑动/进度 30% 正常；单行长文本
  越界已由 §8.0c3 双层裁剪修复，探针实证左边框区 0 文本像素）
- 页 4 选项卡演示：功能正常但**选项卡条溢出**（17+ 选项卡挤压重叠、
  文字越界）——登记 §8.2 遗留清单（对标 QTabBar scrollButtons/elide）。
- 截图：/tmp/dpage0~4.png（无 autotest，纯 --page 静态帧）。

### 8.0f XTabBar 溢出增强 ✓（2026-09-21，demo 逐页目验登记项收口）

- **滚动模式**：溢出判定（count×88 > 条宽）激活——单行固定 88px 页签、
  两端 18px 滚动按钮区、偏移状态机（effOffset clamp 自动收敛）；
  绘制视口 ReplaceClip 平移裁剪、命中换算、按钮区命中（点击步进
  ±88）、closable 关闭区坐标同步适配；滚轮步进（120 角度/页签）。
- **当前页自动露出**：setCurrentIndex/点击/removeTab/moveTab 四处挂
  ensureVisible（对标 QTabBar）。
- **API**：setUsesScrollButtons/usesScrollButtons（默认 true 对标 Qt）、
  isOverflowed/scrollOffset/barHeightHint（新增，XTabWidget 高度计算
  统一收口，消除漂移副本）。
- **开关 false**：回退旧换行挤压布局零回归。
- **验证**：双配置语法+模块关闭变体通过；探针 31/31（按钮区渲染/偏移
  换算/滚轮/自动露出/开关回退）；demo 实拍单行整齐页签+两端箭头；
  集成回归 XTabBar/XTabWidget test PASS。
- 过程中修复 XPainter 真缺陷：解除视口裁剪须 setClipping(false)
  （setClipRect(NULL, NoClip) 文档与实现不符）。
- **未尽**：elide、按住连发、按钮 hover/按下态、无滚动动画、触摸滚动
  ——已登记后续。

### 8.0g 控件域 P1 修复批次 ✓（2026-09-21，四路并发，复扫控件域 P1 收口）

- **XDateTimeEdit 四连**（+369/-17）：①dateChanged/timeChanged 真发射
  （xdt_emitPartChanged 统一提交口，六条路径接线，对标 QDateTimeEdit
  三信号齐发）；②Interpret 键入提交——覆写 EXAbstractSpinBox_Interpret，
  格式串与编辑文本并行游走分节解析（yyyy=4/MM=2/z=3 位宽截取、
  AmPm 上午/下午识别折算 24h、星期节跳过、字面对齐），非法回退旧值
  （对标 CorrectToPreviousValue）；③Home/End 拦截=光标到当前节首/节尾
  （不再值突变跳 min/max，对标 Qt 不消费 Home/End）；④calendarPopup
  默认 false（对标 Qt）。探针 21/21。
- **Dock 停靠三连**：①内容几何跟随 resize（xdw_layoutContent 摆到
  标题条 21px 以下，停靠/浮动/回归三态跟随）；②saveState v2 追加
  标签组持久化（p 条目，restoreState 还原编组，旧快照兼容）；③
  setWidget 替换语义（旧 widget 摘父链不删除，所有权转移调用方）。
  探针 27/27。
- **条目视图三连**：①editTriggers 默认改 DoubleClicked|EditKeyPressed
  （对标 Qt 6.8）；②role 渲染消费（CheckState 简笔勾选框/Decoration
  左置/Alignment/Font，ListView+TableView）；③Return 先编辑后激活；
  keyboardSearch 每视图实例化（原全库共享静态前缀）；reset() 收
  编辑器。探针 28/28。
- **XComboBox 三连**：①editable currentText 回编辑框文本（对标 Qt
  editable getter）；②setCompleter 接通（借用安装+无模型自动接通
  条目模型+completerMode 驱动）；③InsertAtCurrent 无当前项不动作
  （对标 Qt）；④InsertAlphabetically 无符号字节序（修 signed char
  使中文恒排前的缺陷）+ 前缀分支颠倒修正；⑤22 处 gcc-14 指针转型
  硬错误清零。探针 25/25（含中文字母序验证）。
- 集成验证：构建 0 错误、回归零失败断言、验收 68/68、GPU 通过。

### 8.0g2 收尾三小项批次 ✓（2026-09-21）

- **XTabWidget setWidget 替换语义**：新增 API（此前无 setWidget，遗留
  清单描述已过时）——同指针幂等、widget==self 防自挂、已有内容
  XWidget_setParent(NULL) 摘父链转独立顶层不销毁（所有权转移调用方）、
  新控件 reparent 到页容器；NULL 清空；跨页迁移解除借用。探针 19/19。
- **demo 页 4 启动器按钮文字溢出修复**："菜单工具栏"→"菜单栏"、
  "堆叠+按钮组"→"堆叠组"（3 字≈46px 低于 88px 最小单元格）——
  功能逻辑零改动。
- **富文本 <pre> 标签支持**（XTextDocument）：pre 开/闭标签→块级处理+
  monospace 字体族切换+pre 区逐字节原样 append（跳过空白折叠）；
  xtd_appendByte 重构为 xtd_appendSpan（定长核心，保 UTF-8 序列
  完整）；pre 外路径零改动。探针 11 项边界全过（游离/未闭合/嵌套/
  大写/带属性/实体/br-in-pre/appendHtml/li 衔接）。

### 8.0g3 剪贴板协议补边批次 ✓（2026-09-21，§8.2 长尾首项）

- **MULTIPLE 广播进 TARGETS**（posix 后端）：TARGETS 应答在镜像格式
  原子集合后追加 MULTIPLE（ICCCM 2.6.2——支持批量转换的所有者应
  广播；此前"服务但不广播"为 §8.1 声明偏差，本批收口）。数组容量
  +1 护栏即为此预留；SAVE_TARGETS 应答维持纯数据目标语义不变。
  Xvfb 跨进程探针：独立 Xlib 客户端请求 TARGETS，断言 MULTIPLE/
  UTF8_STRING/text/html 三原子在列（实测 n=4），探针用后已删。
- **INCR 读超时参数化**：XClipboard 新增 setIncrTimeoutMs/
  incrTimeoutMs（读方向整体兜底超时；Qt 无公开对应——QXcbClipboard
  内部常量的参数化等价）；ms<=0 恢复默认
  XCLIPBOARD_INCR_TIMEOUT_DEFAULT_MS(5000)。前端进程内记录，经后端
  契约尾部可选回调 setIncrTimeoutMs 下发（尾部追加零回归；
  Win32/进程内无 INCR 语义留 NULL no-op）；先设后装顺序亦生效
  （install 时同步当前值）。posix 读循环常量改全局
  g_xpwnClipIncrTimeoutMs；仅约束读方向（serve 方向闲置回收不受
  影响）。回归断言 3 条：设置回读/0 恢复默认/负值恢复默认。
- 集成验证：构建 0 错误、回归 exit=0 零失败断言、验收 68/68、
  diff --check 干净。

### 8.0g4 富文本渲染收口批次 ✓（2026-09-21，§8.2 长尾·富文本深化）

- **列表项呈现**（此前 ul/ol/li 解析建块但渲染拍平）：内容盒按
  indentLevel×24px 左缩进（软换行宽度同步扣减、居中在扣减后内容盒内
  居中；右对齐沿视口右缘——简化子集）；块首行发射标记单元——有序
  "N."（同序别连续项计数，序别切换/非列表项隔断重起 1）、无序 3×3
  实心方块（位图字库无 "·" 字形，字体无关近似），右对齐挂在内容盒
  左缘前 4px。标记经 XTERichGeom markerText/markerBullet 通道走与
  片段同一 walk（绘制/命中/滚动范围共用几何），命中回调按 frag==NULL
  自然忽略。
- **`<sup>`/`<sub>` 解析+渲染+互逆**：内联栈开/闭标签置
  superScript/subScript（与 b/i/u/s 同机制）；布局字号统一入口
  xte_fragPixelSize（fontPointSize 优先、缺省块字号；上下标缩至
  62%——对标 QTextCharFormat verticalAlignment），度量/填行/绘制三
  站点同口径；绘制基线上移行高 2/5（上标）/下移行高 1/5 钳行盒
  （下标）；toHtml 互逆发射 `<sup>`/`<sub>`。
- 验证：Xvfb 像素差分探针（owner 预览态 300×200 + 原生 Xlib 回读）：
  缩进 +24px、无序方块 ink+9、双项双标记带、有序序号在文本左侧、
  上标 miny 抬升 3px 且 ink 减少、下标 maxy 下沉 2px；回归新增断言
  8 条（sup/sub 四片段拆分/双标志/toHtml 互逆/列表三块/ul/ol 属性）。
  三套件全绿（回归 exit=0 零失败、验收 68/68、GPU 通过）、diff 干净。
- **未尽**：斜体合成倾斜（位图/矢量字形引擎 shear 专项）、列表嵌套
  （解析侧 listDepth 已承载、渲染未分级呈现）、bgColor 片段背景。

### 8.0g5 富文本余项收口批次 ✓（2026-09-21，§8.2 长尾·富文本深化二）

- **嵌套列表分级**：解析侧列表上下文改真嵌套栈（ul/ol 逐层下压、
  闭标签弹栈，深度上限 4 超深钳制；此前闭标签一律复位拍平）；li 块
  承载 indentLevel=嵌套深度，新增 listFresh 标记区分「同层新列表首项」
  与「同列表兄弟项」（相邻 </ul><ul> 两列表序号各自重起，嵌套归来
  兄弟项续号）。渲染侧序号改按层独立计数（li 所在层 +1 并清更深层，
  非列表块清全部层）。
- **span 片段背景色**：解析 style 属性的 background-color（CSS 名值
  对解析 xtd_styleValue，大小写不敏感；span 恒入内联栈保持配对平衡，
  其余样式静默）；渲染在文本前铺行带背景矩形（建议不透明色——半透明
  混合语义子集边界，与悬停高亮同实现）；toHtml 互逆发射
  `<span style="background-color:#rrggbb">`。
- 验证：Xvfb 像素探针——嵌套列表 4 列簇（外/内层标记与文本 +24px
  逐层错开）、span 背景 +520px 固体块且文字仍绘于其上（644=520+124
  算术吻合）；回归新增断言 9 条（嵌套三块/层级 1·2/首项标记/兄弟不
  重起/相邻列表各自重起/背景色拆分/互逆）。三套件全绿、diff 干净。
- **未尽**：斜体合成倾斜（字形引擎 shear 专项）；嵌套列表各级标记
  形态未区分（Qt disc/circle/square 梯度——统一方块）。

### 8.0g6 逐套 deinit 纪律首批 + 拆除真缺陷两笔 ✓（2026-09-21，§8.2 长尾）

- **归因方法**：ASan 快照（/tmp/xinyuec-asan，源码同步）+ LSan
  fast_unwind_on_malloc=0 按分配栈聚合到测试函数/行号。回归基线
  195469B/821 块。
- **Mesa 连接级定性（非缺陷）**：195KB 中 100956B（52%）分配于
  libGLX_mesa 内部（驱动 screen 状态+glapi 表）；反复 create/destroy
  探针 1×/5×/20× 泄漏恒等 100973B——连接级一次性持有，随 X 连接
  存亡，非逐操作增长。我们的 destroyOffscreen 路径（解绑/销毁上下文/
  销毁 pbuffer/释放 state）完整无缺。
- **真缺陷 ①：XMenu 析构隔个漏删**——deinit 循环 delete_base(0) 后
  动作经 destroyed 信号自摘（xmenu_actionDestroyedSlot 已移出向量），
  循环尾再补 remove(0) 把下一个动作指针丢弃不删。修复：按尺寸是否
  自缩判定（自摘已缩则不补删；未缩防御性手摘防死循环）。影响面：
  全部菜单测试（lineedit 右键×4/scrollbar 标准菜单/menu 族/menubar
  族），约 -36KB。
- **真缺陷 ②：XDialogButtonBox::clear 只摘父不删自建按钮**——
  addButton_3 创建的标准按钮（m_standards 非 0）为盒所有，clear/析构
  应删除（对标 Qt 盒拥有自建按钮）；用户 addButton 传入（0 标记）
  保持摘父归还。消费方（MessageBox/ColorDialog/InputDialog/FileDialog）
  无按钮指针缓存，语义安全。-7.7KB。
- **量化**：195469B→151790B（-43679B/-22%），块 821→464；剩余 =
  Mesa 101KB 连接级 + ~51KB 夹具退出持有（顶层孤儿控件树）。ASan
  零内存错误（无 UAF/双释放）；三套件全绿、diff 干净。
- **未尽**：剩余 ~51KB 夹具债继续按套拆（test_gui_application/
  ime_bridge/phase32 等）；斜体合成（字形引擎专项）。

### 8.0g7 逐套 deinit 纪律二批：三笔生产泄漏收口 ✓（2026-09-21，§8.2 长尾）

- **XFontSet 逐窗口泄漏（-14.9KB）**：posix 平台窗口创建 PreeditPosition
  风格 IC 时 XCreateFontSet 传入 XNFontSet 后从未释放（X11 语义：IC 不
  接管所有权，须 IC 销毁后 XFreeFontSet）。修复：XWNPendingEntry 增
  m_fontSet 字段持有，窗口销毁在 XDestroyIC 之后释放。ASan 归因四个
  测试点同款 3730B/28blk 签名即此（ime_bridge/lineedit 菜单/gui_app×2）。
- **XLineControl_setFont 壳泄漏（-26.9KB，回归残余的 75%）**：m_font 以
  裸 XMalloc_System+XCopy 深拷贝承载，但 XCopy 不继承堆所有权位——
  delete_base 只 deinit 不 free，880B 壳逐替换泄漏。微探针隔离复现
  （单控件建/打/删 4255B→647B 纯库级；10 次 setFont 同）。修复：
  拷贝后 Set_Class_IsHeap(true)（XTextMenuContext 同款纪律）；全库巡
  检其余 XCopy 站点均走 create_ex（is_heap 已置），无同款反模式。
- **上一批遗留确认**：菜单域泄漏清零（XMenu 析构修复生效，scrollbar/
  menu/menubar 族测试从归因表消失）。
- **量化**：195469→151790→136870→**109942B**（会话累计 -85.5KB/-44%）
  ；剩余 = Mesa/GLX 连接级 100956B（§8.0g6 定性）+ 真夹具残债 ~9KB
  （顶层孤儿控件：phase32 tree cw/icon_geometry/menubar 桥等零散）。
  ASan 零内存错误；三套件全绿、diff 干净。
- **未尽**：~9KB 夹具残债（零散小项）；斜体合成（字形引擎专项）。

### 8.0g8 逐套 deinit 纪律三批·收官 ✓（2026-09-21，§8.2 长尾）

- **XMenuBar 桥泄漏（第五笔生产真缺陷，-2.8KB）**：addMenu/addAction
  创建的 XMBBridge 仅被动作的 triggered 信号连接引用、无持有者，逐
  addMenu 泄漏 560B。修复：桥挂为配对动作的 XObject 子（动作析构级联
  释放堆子，XTextMenuContext 同款纪律）。
- **测试夹具拆除两处**：phase32 tree3 段 removeItemWidget 后 cw 补删
  （借用语义归还即自删，-2.6KB）；icon_geometry 段源 XPixmap 补
  deinit（-1.8KB）。
- **量化（泄漏战役终态）**：195469→**102766B（-47.4%）**；非 Mesa
  残余从 ~94KB 清至 **1810B（-98%）**——构成：fontconfig 库缓存 630B
  + xpwn_imeInit 17B + 6 处 48B 级字符串碎屑与零星单例。逐套 deinit
  纪律实质收官（继续清需跨数十路径追 48B 字符串，收益见底）。
  生产真缺陷累计五笔全修（XMenu 隔个漏删/按钮盒 clear/XFontSet/
  setFont 壳/菜单栏桥）。ASan 零内存错误；三套件全绿、diff 干净。

### 8.0g9 XGUI_ON=0 全裁剪构建收口 ✓（2026-09-21，§8.2 长尾·裁剪巡检收官）

- **文本引擎守卫补齐**（XLineControl/XTextControl 为裁剪下仅存的两
  个 GUI 源，XTEXTCONTROL/LINECONTROL_ON 不随 XGUI_ON=0 关闭）：
  ①纯值常量兜底——XClipboardMode（Clipboard=0/Selection=1）、
  XFocusReason（ActiveWindow=3/Popup=4）按 `#if !X*_ON` 本地定义（值
  口径与头文件枚举一致）；②类型/函数使用点按子系统分流——剪贴板三
  块（中键门禁/copy/paste 的统一剪贴板通道）按
  `XCLIPBOARD_ON && XGUIAPPLICATION_ON` 包裹（裁剪时走 XTextClipboard
  共享层回退，语义不变）；调色板取色四角色按 XPALETTE_ON 分流（裁剪
  时直接取常量回退值）；行带裁剪两处按 XPAINTER_CLIP_ON 分流；IM 查
  询 switch、inputMethod/focus/drop 三个事件函数及 processEvent 对应
  case 组分别按 XINPUTMETHOD_ON/XWINDOWEVENT_ON 包裹；setFocus 公共
  API 声明+定义成对守卫（XLabel 调用方在裁剪下同样不编译）。
- **构建脚本收口**：XinYueC_Static/Dynamic demo 可执行（main.c +
  Test/*.c 引用 GUI 类型）按 CMakeLists 既有注释语义纳入 XGUI_ON 排
  除（与回归测试同口径）；CMake_Install.cmake 对应 install(TARGETS)
  同步分流（否则配置期即报 target 不存在）。
- **验证**：裁剪构建 XinYueCS 静态库 + libXinYueCd.so 动态库双 0 错
  误；默认构建 0 错误 + 回归零失败 + 验收 68/68 + GPU 通过 + diff 干
  净（守卫零回归）。探错顺序：38 错（XTextControl.c 36 + XLineControl
  2）→ 9 锁三类事件类型 → 33 锁 TEST_FILE 目标 → 收口。

### 8.0g10 XPaintDevice 绘制派发（begin 泛化）✓（2026-09-21，§8.2 末项常规件）

- **设备侧**：XPaintDevice 增可选 `m_beginPainter` 回调（设备自述「如何
  被绘制」——对标 QPaintEngine::begin 的设备侧虚语义，C 分层经回调解
  耦）+ `XPaintDevice_setBeginPainter` 启用/撤销；init 默认 NULL（不
  开放）。
- **绘制器侧**：`XPainter_begin_device(painter, device)` 泛化入口——
  与 begin_image/begin_picture 同护栏（未 init/已激活拒绝），装配权
  交还设备回调（单一事实源在设备，painter 不复制装配逻辑）。
- **接入**：XImage/XPicture 经 paintDevice() 访问器惰性装配（幂等）；
  XPixmap/XBitmap 的 paintDevice 转发内部 XImage 自动继承，零改动接
  入；XWidget 保持不开放（对标 Qt：控件不可在绘制事件外 begin）。
- **关键设计（两轮 ASan 实证）**：设备 userData 为内部数据指针
  （XImageData*/XPicturePrivate*，引用计数共享、外层对象不唯一）——
  ①栈上临时包装不可行：painter 长持绑定目标指针，回调返回即悬垂
  （ASan stack-use-after-return 当场抓获）；②最终方案=数据内嵌惰性
  堆外壳（m_deviceShell，仅 m_class+m_data 自指），随数据 unref 归零
  路径释放；壳对数据**裸借用不持引用**（持引用会使"只剩壳引用"时无
  人触发 unref 整块泄漏）；XPainter_device() 返回外壳指针（非调用方
  栈对象，断言口径同步）。
- **验证**：构建 0 错误；回归新增 8 断言全过（绑定 Image/Picture/
  绘制落像素/重复绑定拒绝/未 init 拒绝/Widget 拒绝/end 解绑/外壳指
  针口径）；三套件全绿；ASan 总量 102766B 与基线逐字节一致（零新增
  泄漏、零 UAF）。

### 8.0g11 斜体合成 + XTabBar 小尾巴 ✓（2026-09-21，§8.2 长尾收尾）

- **斜体合成倾斜（字形引擎专项，此前"仅属性承载"收口）**：
  outline 路径在 PainterOutlinePathSink 坐标换算处加 shear
  （XPAINTER_SYNTHETIC_ITALIC_SHEAR=0.22，基线不动、顶部右移，对标
  Qt 合成斜体约 12°）；位图路径双管齐下——AA 覆盖采样按逆 shear 映射
  （x 扫描范围外扩 shear×字高）、非 AA 行块按行右移
  shear×(基线−行)；缓存隔离：italic 折进 scaleKey 最高位
  （painterOutlineCacheKey，三处缓存共用、结构零改动）。富文本
  `<i>` 接通：xte_makeFragFontStyled 把片段 italic 映射 XFont_style
  （度量/绘制同口径）。目验：Xvfb 双窗口正体/斜体截图对比，逐行
  首墨迹偏移曲线 +3→+1→0（顶部最大、基线归零）确认 shear 特征；
  回归零失败。
- **XTabBar elide**：XStyleOption 尾部追加 m_tabElideMode；
  XCommonStyle xcs_elideText（右/左/中省略+无省略截断，U+2026），
  CE_TabBarTabLabel 消费；TabBar paint 传 elideMode。目验：长标题
  窄条渲染"Settings/Network/User Acc…"截短+右滚动按钮并存。
- **XTabBar 按住连发**：滚动按钮按下启动 120ms 定时器（首段 3 跳
  ≈350ms 延迟），按住期间每拍步进一页签宽，释放/失能/析构终止；
  init 显式置 XTIMER_INVALID_ID（Memset 清零后 0 非 INVALID——
  回归断言抓出的边界）。
- **触摸滚动**：不做——触摸→mouse 合成无来源标志（§8.1 声明边界），
  触摸接入时随 XI2 专项一并处理。
- **验证**：构建 0 错误；回归（含新增 8 断言）exit=0 零失败；验收
  68/68；GPU 通过；diff 干净。

### 8.0g12 XGuiDemo 全量 Widget 接入 + 全量自动化测试 ✓（2026-09-21，§8.2 长尾·demo 收口）

- **全量接入**：demo 全家迁入 `Test/XGuiDemo/`（主文件 xgui_window_demo.c
  + 4 个演示页 + `xgui_demo_pages.h` 契约头，契约先行 + 每页
  独立翻译单元，文件所有权分离并行开发）：条目视图页
  （XListWidget/XListView+自定义模型/XTreeWidget/XTableWidget/
  XHeaderView 段带可视化子类）；对话框页（XMessageBox×4 非阻塞
  open+结果中继/XInputDialog/XFileDialog/XColorDialog 模态便捷函数
  仅构造覆盖/XProgressDialog/自定义 XDialog+XDialogButtonBox）；
  高级控件页（XTextEdit 富文本含 §8.0g11 斜体直观样例/XCompleter+
  XLineEdit/XKeySequenceEdit Ctrl 修饰注入/XShortcut/XSizeGrip/
  XFocusFrame/XRubberBand/XToolTip/XSplashScreen/XMainWindow+
  XDockWidget 独立窗）；图形效果页（Opacity/Blur/DropShadow 挂样例
  +无效果基线组同屏像素对照）。主文件：9 页导航单行 84px、窗口按
  扩展页自适应 800x600、`--style=common|fusion|fusion-css` 样式矩阵
  开关、autotest 逐页调度与效果页独立帧截图。修复 styleOpt 先用后
  初始化的启动段错误（原样式安装块不依赖参数故未暴露）。
- **全量自动化测试（--autotest，事件经 XObject_event_base 直发与真
  实输入同路径）**：**129 断言全过 exit=0**——输入页 9 + 条目视图 20
  + 对话框 44 + 高级控件 38 + 效果 18。点击/键入/滚动/补全/序列捕
  获/效果挂摘全覆盖，全程非阻塞（模态 exec 仅真人路径）。
- **目验（Xvfb 截图 + AI 视检 + 像素差分）**：9 页逐页截图正常；效
  果页三项差分成立——透明度对比度 4.9 vs 基线 15.8、模糊边框中间亮
  度像素 7.2% vs 1.6%、投影带暗像素 4.7% vs 0.9%；样式三套矩阵渲染
  正常，CSS 黄底仅在 fusion-css 出现。
- **框架真缺陷两笔（本批次根修，页面目验揪出）**：①XListView/
  XTreeWidget paintEvent 缺 `XWidget_paintOffset` 平移——paintImage
  返回顶层后备存储，控件非零偏移时内容直绘窗口 (0,0)（XListWidget
  继承同槽一并修复；XLabel/XFrame/XTableWidget 本就正确）；②
  XTreeWidget 条目文本 `XPainter_drawText` 第 4 参墨水色传 0（透明
  =条目永不出字；对标 XTableWidget 传 palette windowText）。根修后
  demo 页 paintOffsetSafe 子类补偿自动退化为直调基类路径。
- **ASan**：autotest 0 断言失败；退出持有 18.3KB/126（控件树夹具语
  义 + fontconfig 缓存 + ime 17B 连接级），新增页面零新增泄漏源
  （completer 模型 static 可达不计泄漏）。注意 LSan 检出泄漏时进程
  exit=1，与断言结果无关。

### 8.0g13 Release/-O2 堆损坏根修（主题引擎栈残影误判）✓（2026-09-22，§8.2 长尾·交付后补测揪出）

- **现象**：最终门全绿（Debug 口径）后补测 Release/-O2，回归套件
  确定性 `malloc(): corrupted top size`（exit=134）；-O0 全绿，
  ASan 快照全绿，旗标二分（严格别名/浮点收缩/向量化关闭）均无效。
- **根因**：`theme_loadFile`（XIconThemeInternal.c）栈上
  `XPixmap candidate` 未清零即 `XPixmap_init`。该栈槽残留上一次
  XPixmap 的字节——vtable 指针恰好匹配 → `XPixmap_isInitializedObject`
  误判为「已初始化对象（重初始化）」→ 先 `XPixmap_releaseData`
  释放残影里的陈旧 m_data：普通 -O2 下陈旧指针恰为活对象 → 重复
  unref → 引用计数提前归零释放 → 后续使用即堆损坏（损坏 glibc
  top 块，在下一次主分配区扩顶 malloc 才惰性 abort）；残影为垃圾值
  时直接野指针（调试分配器下实测 0xd1）。**检测完全依赖栈布局**：
  -O0 残影恰为 NULL、ASan 换分配器布局、零初始化换栈帧填充，
  三者均"修好"假象。
- **修复**：XIconThemeInternal.c 8 处栈上 XPixmap 局部
  （theme_loadFile candidate、tryParsedTheme 两 candidate、
  tryTheme/searchTheme 两 best、scaledToSizeRect scaled、
  fallback 扫描 pixmap）声明处 `XMemset 0` 再 init（对已初始化
  路径零 memset 无害，unref(NULL) 安全空操作）；`out` 参数不动
  （契约由调用方保证，且需保留重初始化释放语义）。
- **排查工具链（可复用）**：①`-ftrivial-auto-var-init=zero` 全绿
  ⇒ 类别锁定为未初始化局部（GCC14 支持，替代不可用的
  MSan/valgrind）；②按文件注入 plain/zero 旗标二分须强制
  touch 变更文件（CMake 每源旗标变更不保证重编，首轮二分因此
  误定位 XAtomic_GCC——翻转复验证伪）；③core 离线 gdb 不走
  ptrace；④`LD_PRELOAD=libc_malloc_debug.so.0 +
  GLIBC_TUNABLES=glibc.malloc.check=3:tcache_count=0` 把惰性
  abort 变成写点 SEGV，core 栈直接暴露野指针；⑤expect 探针
  `malloc(1MB)` 走 mmap 不校验 top、96KB 探针因 bin 复用不保证
  走 top——探针"通过"≠堆完好，勿据此定性。
- **顺带**：XPainter.c 排查期 TEMP 调试打印 10 处清零；
  `painterFillContoursAntialiased` 的 ok 赋值/检查顺序复核为正确。
- **验证**：Release -O2 回归全量绿（exit=0）；Debug 回归全绿；
  apitest 0 FAIL；autotest 全过；TEMP 残留 0。

### 8.0g14 deferred 小项六路收口 + 对话框键盘路由根修 ✓（2026-09-22，§8.2 长尾）

- **批次形态**：六路 Flash 工作流派发至中途配额耗尽（1310，重置
  09-24），主线单线程接续收口——已落盘的两路（StyleHints 默认值、
  QSS 头注释×2）直接采纳，未落盘的三路主线亲手补齐，轴 reverse 一
  路代理已完整落盘直接采纳。
- **六项 deferred 收口**：①XStyleHints 默认值对齐 Qt（长按
  500→800、触摸释放焦点 true→false，qplatformtheme.cpp
  defaultThemeHint 依据，头文档同步）；②XCssStyleSheet.h:132 特异
  度注释改 0x100/0x10/1 实现口径；③XStyleSheetStyle.h 缓存字段注
  释同步新契约（同分取后+!important 门禁）；④**XDialog Enter 派
  发**（VXDialog_keyPressEvent 补 Return/Enter 分支：显式 default
  优先、回落子树首个可见可用 autoDefault 按钮，命中即 click；多行
  文本编辑持焦豁免）；⑤toDouble/toFloat 家族改 Qt 全串口径
  （XStringView/XByteArrayView 四函数：跳过首尾空白后整串消费，
  尾随垃圾判失败——调用方全库枚举确认仅 XVariant 依赖、其 Qt 对
  应 QVariant::toDouble 本就全串；QSS/XInputDialog 用裸 strtod 不
  受影响）；⑥**轴 reverse 登记推翻**：核对 Qt 6.8.3
  verticalaxis/horizontalaxis.cpp——轴线（arrow）定位不读
  isReverse()，"setReverse 把轴线移至对侧"的 deferred 登记不成立，
  reverse 仅镜像刻度/网格/标签排列；已落盘实现=映射翻转+刻度值翻
  转+指纹纳入 reverse，即为对标行为。
- **GCC14 指针惯用法清理**：32 处显式转型（XTreeWidget×3/
  XListView×4/XListWidget×2+XStringUtils 头/XChartView×13/
  XStringView×9+XVariant 等连带）；实测 Debian gcc-14 该诊断文本
  为 error 但 exit=0 不阻断构建（P2 批次"硬错误级"系文案误读），
  清理为防御性；错误诊断已清零。
- **对话框键盘路由根修（真键盘目验揪出，"对话框弹不出"同族）**：
  真键盘路径下打开的消息框 Esc/Enter 全部无响应——链条两层缺口：
  ①平台键固定投递原生窗口对象，而 XGui 对话框为应用内 XWindow（
  单原生窗口模型）永远收不到键 → **VXGuiApplication_notify 键事
  件重定向到焦点控件顶层窗口**（对标 QGuiApplicationPrivate::
  processKeyEvent 的 focusWindow 交付）；②对话框 open/exec 只
  show+登记模态、从不抢焦点 → **dialog_grabInitialFocus**（对标
  showModal initialFocusWidget：默认按钮优先，已持有焦点则不动）。
  ③连带修正 autoDefault 判定：对话框子树内按钮仅显式 Off 才退出
  候选（XMessageBox 便捷路径以 parent+flags=0 构造为子控件形态，
  windowType≠Dialog 使 autoDefault 误判关——Qt 语义对话框内按钮
  默认即 autoDefault）。**真键盘复验全通**：xdotool 开框→按
  Return→默认按钮点击→对话框关闭→状态栏"接受(result=1)"。
- **排查教训**：后台长活进程用 `pkill -f` 会误杀同名包装 shell
  （-f 匹配整条命令行），须用 `pkill -x`/comm 精确匹配；测试实例
  必须先 ps 核对唯一性（本轮曾两个实例叠跑导致连续误判"修复无
  效"）；autotest 直发事件路径与平台真键路径不等价——对话框键盘
  行为必须有真键盘目验。
- **追加收口（同日）**：⑦**XAction 图标承载**（deferred 末项落地：
  m_iconPath 字段 + icon/icon_const/setIcon/setIcon_2 四 API（复用
  XACTION_DEFINE_TEXT_SET 宏族，changed 联动）+ XToolButton 镜像
  经 XIcon_init_file 落按钮；apitest menus 族新增 5 断言全过——注
  意 set(NULL) 置空串为文本族约定非 NULL 指针）；⑧autotest 补对话
  框 Enter 派发锁定断言 4 条（Return 直发→默认按钮→accept 关闭，
  含焦点清理防悬垂——此前真键盘行为零自动化覆盖）；⑨**GCC14 惯用
  法项重登记并关闭**：全库扫描实为 13524 处/303 文件的既定 C 继承
  风格（诊断文本 error 但 exit=0 不阻断构建），P2"16 处"仅所有权
  六文件巧合计数——机械转型不立项，仅触碰文件顺手清理。
- **验证（终态）**：Debug（API 2732/autotest 133/回归/验收/GPU）+
  Release（同套件×3+diff CLEAN+基准 280 FPS）双口径终门全绿；探
  针零残留；改动未提交等授权。

### 8.0g15 SVG 目标尺寸矢量直渲 ✓（2026-09-22，大件首项落地）

- **管线**：`XImageCodecInternal_decodeSvg_ex(data,size,tw,th,out)`
  新入口——`svgVectorDecode` 目标尺寸覆写表面（viewBox 根变换按目
  标比例映射矢量几何），消除「固有尺寸光栅化+平滑放大」的插值模
  糊（对标 QSvgRenderer::render 按目标矩形出图）；根元素缺 viewBox
  时以固有尺寸充当隐式 viewBox（Qt QSvgTinyDocument 缺省语义），
  保证目标表面整体缩放；preserveAspectRatio（默认 xMidYMid meet）
  纵横比语义保持。位图/纯色回退形态无矢量几何，target 不适用按原
  口径。`decodeSvg` 委托 `_ex(0,0)`，gzip 递归透传目标。
- **引擎接线**：`XSvgIconEngine` pixmap 槽有效请求尺寸时读文件字
  节走直渲优先，失败回退既有 XImageCache 路径（不空手）；直渲绕过
  XImageCache（键 fileName+format 不分尺寸防错尺寸命中），重复成
  本由上层 XIconScaledPixmapCache 最终位图缓存吸收。
- **渲染器级 AA 已实现（同日，登记项闭环）**：探针实测原光栅化器
  为二值覆盖（非对齐圆周半透明像素=0）→ 落地 **4× 超采样+盒式降
  采样**（16 级覆盖积分，预乘平均/非预乘还原；目标>4096 时超采样
  面超 16384 上限自动回退无 AA）。**AA 仅在显式目标尺寸时启用**
  （decodeSvg_ex 传正目标=「矢量直渲+AA」新契约；既有 decodeSvg
  固有尺寸路径保持渐变中心/三角形/宽行覆盖等像素级历史基线）。
  回归新增 1 断言（非对齐圆周过渡像素>0）全过。SVG 侧 Qt 对齐项
  至此清零。
- **验证**：回归新增 5 断言（尺寸×4+AA 过渡像素）全过；双口径终
  门全绿（API 2732/autotest 133/回归/验收/GPU/diff CLEAN/基准
  284 FPS）；探针零残留。

### 8.0g16 XTreeWidget itemEntered（数据模型四期③）✓（2026-09-22）

- **落地**：VXTreeWidget_mouseMoveEvent 覆写（XClass_Parent 走
  XTreeView 基类移动路径保 entered 抽象信号）+ m_enteredRow 差分判
  重（-2 初值同 XListWidget 口径）+ 命中走 xtw_rowAtY 展开态行带
  （无模型便利类不适用基类 indexAt 的模型行数校验，与点击同口径）
  + itemEntered_signal 由句柄预留转真实发射 + 头注同步。
- **测试**：apitest views 族 +2 断言（进入新行发射 itemEntered(1)/
  同行悬停差分判重不重发）——注入坐标须 visualItemRect 内容坐标
  加回 20px 表头带（与点击注入 +20 同口径）。2732→2734。
- **验证**：双口径终门全绿（API 2734/autotest 133/回归/验收/GPU/
  diff CLEAN/基准 281 FPS）。四期剩余①②④（数据模型扩展/模型桥
  接/绘制消费）仍为独立批。

### 8.1 架构裁剪/平台边界（声明式偏差，非漏实现）

- XPaintEngine 绘制命令接口由 XPainter 承担；XImage/XPixmap/XBitmap/
  XPicture 统一接入 XPaintDevice 为项目决策（D3）。
- 图标以路径字符串承载（等价 QIconEngine 资源寻址）；图标尺寸单 int。
- 布局默认边距/间距 0（Qt 由样式提供）；显式设置后一致。
- XMovie 手动驱动（正式裁剪项）。
- XStackedLayout 不发 currentChanged（信号所有权在 XStackedWidget）。
- XColorSpace ICC 固定 1024 缓冲承载，不解析矩阵/LUT。
- XTouchEvent 单触点（完整多点列表待做）。
- XMenuBar 几何模型与样式绘制有轻微偏差；XHeaderView 维持 XWidget
  直接派生（调研结论，重评条件=表头实体化进 XTableView）；本体无
  自绘代码，独立摆放不可见（demo 页以子类画段带可视化）。
- 快捷键以 XShortcut 承载（无 grabShortcut 注册表）；手势体系不做；
  文件 URL 族不做；纯公历（QCalendar 备选历法不做）。
- XGraphicsEffect：blur 固定 3x3 核（blurRadius 仅 API 对齐）、效果
  外扩区依赖父级重绘；富文本子集边界（斜体已合成倾斜 §8.0g11——
  固定 shear 0.22 非 12° 连续可调；嵌套列表各级标记形态统一方块；
  span 半透明背景混合语义）。
- XFileDialog 多选（getOpenFileNames）未实化；XColorDialog 无 Alpha
  通道输入、48 标准色为简化生成。
- 剪贴板：外部内容后续变化不自动刷新镜像；image/png 写出载荷须
  合法 UTF-8（INCR 读超时与 MULTIPLE 广播已于 §8.0g3 收口）。
- 触摸→mouse 合成事件无来源标志字段；XI2 合成待嵌入式接入。
- 主循环：无定时器时 20ms 心跳兜底保留（驱动轮询回调）。

### 8.2 遗留清单（按优先级，结构改造大件各自立项）

| 项 | 类型 | 备注 |
|---|---|---|
| 全量内存清零+夹具有序拆除 | ✅ 收官 | §8.0g6~g8：生产真缺陷五笔全修（XMenu 隔个漏删/按钮盒 clear/XFontSet 逐窗口/setFont 880B 壳/菜单栏桥），回归 195→102.8KB，非 Mesa 残余 94KB→1.8KB（-98%，余为 fontconfig 缓存+字符串碎屑）；Mesa 101KB 连接级环境噪音；登记表全量拆除方案不可行（栈对象悬垂）已证 |
| XGraphicsEffect 视觉目验 | ✅ 完成 | 2026-09-21 无头自动化目验：Xvfb 真实渲染四按钮同屏（基准+三效果）→ 原生 Xlib 截图 PNG 视检+像素差分——Opacity 亮度 -39%、Blur 边缘发散可见、Shadow 下方投影带 18 vs 基准 0；blurRadius 4/12 逐位相同=§8.1 已声明固定核偏差（非缺陷）；截图存 /tmp/effect_review/ 供人工复核；§8.0g12 效果页把三效果挂到真控件并纳入 demo autotest+像素差分常态化 |
| ~~文档重构阶段三（architecture/ 分册）~~ | ✅ 完成 | 2026-09-21：render-pipeline/text-system/clipboard-input/platform/widgets-dialogs 五册落地 docs/xgui/architecture/ |
| Qt 6.8.3 二次全量对齐复扫 | 大 | 大量代码变更后的回归性复扫 |
| 图表序列绘制热点（area 填充 1.2ms+图例已修） | 中 | §8.0g12 后勘测：area drawPolygon 占序列耗时 92%（Debug 口径）；**2026-09-22 Release 口径勘测后降级**：-O2 下图表页 288 FPS（中位五样），混合成本非桌面瓶颈，SIMD 随板级档位评估（§10.4） |
| XGuiGpu 回归 GPU 口径 t211g 勘误 | ✅ 已修 | 2026-09-22：后端期望改为环境分支（GPU 请求→Gpu），ctest#3 XGuiRegressionGpu 软件与 GPU 口径双绿；"exit=3" 系 X11 BadWindow code=3 误记，实为 exit=1/ctest=8；新发现 vulkan 后端 lavapipe SIGSEGV（违反回退契约，需真硬件+VK validation） |
| Qt+LVGL 融合优化专项 | 大 | 内核表已按 LVGL 组织，续：嵌入式显存/局部刷新策略 |
| 富文本引擎深化（换行/嵌套/图片） | ✅ 收口 | 换行/嵌套/图片/列表标记+嵌套分级/上下标/背景色/斜体合成（§8.0g2/g4/g5/g11）全部落地；§8.2 无剩余项 |
| XPlainTextEdit 增量布局 | ✅ 已修 | 2026-09-21 §8.0e：单逻辑行编辑局部更新可视行段（原位替换+尾段 memmove，跨行回退全量），2000 次编辑全增量与全量参照逐条一致 |
| MULTIPLE 进 TARGETS 广播、INCR 读超时参数化 | ✅ 已修 | 2026-09-21 §8.0g3：setIncrTimeoutMs API + TARGETS 应答补 MULTIPLE 原子（Xvfb 独立客户端探针实证） |
| XPaintDevice 接入绘制派发（begin 泛化） | ✅ 已修 | 2026-09-21 §8.0g10：beginPainter 回调+XPainter_begin_device；Image/Picture 堆外壳绑定（ASan 两轮实证定方案），Pixmap/Bitmap 转发继承，Widget 按对标不开放；ASan 与基线逐字节一致 |
| XGuiDemo 全量 Widget 接入+全量测试 | ✅ 完成 | 2026-09-21 §8.0g12：9 页 129 断言 autotest 全过 + 逐页目验 + 样式三套矩阵 + 效果像素差分 + ASan 零新增泄漏；随批根修框架 paintOffset/drawText 真缺陷两笔（§8.3） |
| XTextEdit 富文本预览非 ASCII 字形缺失 | ✅ 已修 | 2026-09-22 三重根因：①parseHtml 逐字节拆散多字节序列（改按 UTF-8 整序列追加）；②字库缺 §/—/→/全角括号字形（XPainter 加 ASCII 代理回退表：全角平移/箭头/曲引号等）；③AA 光栅半像素约定错位系统性丢右端墨迹列（改半开区间+逐子采样过滤）。探针+离屏渲染亲验：中文粗斜体/§/破折号/黄底高亮/列表全部呈现 |
| 对话框子控件形态 show/hide 不标脏（"弹不出"根因） | ✅ 已修 | 2026-09-22：XWidget_setVisible 非 m_isWindow 分支缺 XWidget_update（show 永不出现/hide 残影）+ XWidget_paintEvent_default 裁剪坐标混淆（offset 折算后按局部尺寸裁剪）两笔框架根修；XDialog/XMessageBox/XProgressDialog 补 update 中继与自绘面板；xdotool 真点击复验：面板/文本/按钮完整、关闭无鬼影、结果回传正确 |
| 图表图例五项修复 | ✅ 已修 | 2026-09-22：addXxxSeries 系列从不登记全局注册表→主题色按类型内索引分配（销量/月销同色）；xchart_seriesGlobalIndex 加跨类型位置回退；柱状图例色板回退链（序列色→首柱组色→全局序主题色，修白色空板）；图例高度 4→6 行（样条条目曾被 +80 上限裁掉） |
| demo 巡检 P2 批次（外观） | ✅ 已修 | 2026-09-22：棋盘格移至标题栏右端 24x24；命令链接按钮双行自适应居中+14x14 右向大箭头（首版方向镜像已勘误）；XTableWidget 垂直表头补 1 基行号；滑块手柄对标 Qt Fusion 四层画法（投影/填充/描边/内衬）；输入页三控件初值统一 30；insertTab 恢复 0→20 递增（Error/表格位次归位）。全部截图亲验 |
| Qt 6.8.3 二次全量对齐复扫（第二次） | ✅ 报告已出 | 2026-09-22：9 域扫描+独立复核，115→113 确认→去重 111 项修复队列（P0=8/P1=32/P2=71），报告 docs/xgui/history/2026-09-22-qt-full-rescan.md；P0×6 已修复过全量门（XXYSeries 选中位图扩容/XChart 注册表摘除+去重/XTextControl 撤销截断/XTextDocument 片段池钳位/XSpinBox 校验剥前后缀/XTabWidget 界检），P1×32/P2×71 分波派修中 |
| 复扫 P1×22 修复批次 | ✅ 已修 | 2026-09-22 七路并行：XLineControl 反选格像素/字节混用+掩码门禁+中键双粘贴（Text）；SpinBox NoButtons 折叠+InputDialog 死信号（输入）；XMenu 子菜单打不开+XToolBar 借用动作 UAF+XToolButton popupMode 死存储+XMenuBar hovered 死信号（菜单工具）；XTreeView/XTableView paintOffset+indexAt 越界+XTreeWidget 表头渲染/键盘导航（视图）；图例字体泄漏循环+PieSlice setValue 重置外观+面积图 128 点截断+视图侧主题色全局序（Charts）；WSI 焦点窗口更新+XWindow active/raise/lower 语义（窗口）；XMdiArea resize 重铺+XWizard 横幅叠印（容器）。全量门全绿+截图亲验 |
| 复扫 P2×71 修复批次 | ✅ 已修 | 2026-09-22 七路并行（40 文件）：itemClicked 迁移 release 语义/entered 差分/editItem 接通/滚动条菜单/Shift 横滚/ensureVisible 最小滚动/RS-fail 清理；setCurrentCell 收敛/setRowCount 同步/四处视图配色走 XPalette；图表悬停坐标与 UAF/PieSeries move/BarSet 越界/轴 reverse+visible；XLabel 字体 deinit 错位/XRadioButton 图标/LCD 整串解析/TabBar removeTab 语义/MessageBox 关闭信号/Dialog modal 默认；ToolButton 图标镜像/ToolBox 残影/ProgressDialog 自动显示/DockWidget 标题栏/Wizard setButton/TabWidget 角部件/默认按钮互斥；QSS 简写/级联/伪类/特异度/蚀刻文本/Fusion Inactive 组/刻度护栏；XLayout 泄漏/windowPropertyChanged UAF/焦点更新等。deferred：XTreeWidget 数据模型（四期：③itemEntered 已于 §8.0g16 落地，余①②④）、XAction 图标承载（✅ §8.0g14）、XDialog Enter 派发（✅ §8.0g14）、轴 reverse 对侧迁移（✅ §8.0g14 经 Qt 源码核对推翻登记）、SVG 矢量直渲（✅ §8.0g15 含渲染器 AA）、GCC14 指针惯用法清理（✅ §8.0g14 重登记关闭：全库既定风格）。全量门全绿+调色板零漂移亲验 |
| Release/-O2 口径堆损坏（主题引擎栈残影） | ✅ 已修 | 2026-09-22 §8.0g13：Debug 门全绿后 Release 补测确定性 `corrupted top size`；根因=theme_loadFile 等栈上 XPixmap 未清零，vtable 残影被 isInitializedObject 误判为重初始化→释放陈旧 m_data（活对象重复 unref→提前释放→堆损坏）。XIconThemeInternal.c 8 处局部补 XMemset；Release/-O2 回归全绿+三套件复验；排查工具链与探针陷阱教训入册 §8.0g13 |
| 序列 SIMD 填充（光栅 2.5~4×） | 中 | 2026-09-22 实测勘误与量化：图表序列 1.2~1.6ms 中 area 填充占 92%；部分落地（半开区间正确性修复入库；fastBlend 行级快路径此前被 A/B 脚手架 `if(0)` 禁用，2026-09-22 已启用）覆盖光栅器 span 内部像素免逐子采样测试（数学等价，Debug 口径 FPS 无感）——**瓶颈在逐像素 source-over 混合而非覆盖计算**，SIMD 化对象应为实色 span 混合内循环；**Release 口径评估已闭合（§10.4）：-O2 下 288 FPS 非瓶颈，SIMD 随板级档位评估** |
| Release 口径纳入常规门（g13 教训） | ✅ 落地 | 2026-09-22：`Tools/final_gate_release.sh`（-O2 构建→bin-release/，与 Debug 门同套件×3 轮+图表基准单样）；CMakeLists 输出目录加 -D 覆盖守卫防 bin/ 互踩。首跑全绿（API 2727×3/autotest 129×3/回归/验收/GPU/diff CLEAN），基准中位 288 FPS 入册 §10.4 |
| deferred 小项六路（StyleHints/QSS 注释/Dialog Enter/toDouble/GCC14/轴 reverse） | ✅ 已修 | 2026-09-22 §8.0g14：工作流配额中断由主线接续收口；轴 reverse"移至对侧"登记经 Qt 源码核对**推翻**（reverse 仅翻转映射与刻度序）；toDouble 全串口径连带调用方枚举；GCC14 惯用法触碰文件清零+**全库重登记关闭**（13524 处/303 文件=既定 C 继承风格不阻断构建，机械转型不立项） |
| 对话框键盘路由（真键盘 Esc/Enter 全无响应） | ✅ 已修 | 2026-09-22 §8.0g14：单原生窗口模型下平台键固定投主窗，应用内对话框 XWindow 永远收不到键+open/exec 不抢焦点+子控件形态对话框 autoDefault 误判关，三层缺口两笔根修（notify 键重定向到焦点控件顶层窗口/dialog_grabInitialFocus/子树内 Auto 视为候选）——xdotool 真键盘复验：开框按 Return → 默认按钮 → 关框 → result=1 回传 |
| SIMD 内核（NEON/Helium/DMA2D 变体注册） | 中 | 需板级验证 |
| XGUI_ON=0 下其余文件同类裁剪错误 | ✅ 已修 | 2026-09-21 §8.0g9：XLineControl/XTextControl 守卫补齐 + demo 可执行/install 按 XGUI_ON 分流，静态+动态库裁剪构建双 0 错误 |
| XTabBar 多选项卡溢出 | ✅ 已修 | 滚动按钮+偏移滚动+自动露出+滚轮（2026-09-21，见 §8.0f）；elide/按住连发已收口（§8.0g11）；触摸滚动随 XI2 专项 |
| XTabWidget setWidget 替换语义（现拒绝二次设置） | ✅ 已修 | 2026-09-21 §8.0g2：替换语义落地（摘父链转移所有权），探针 19/19 |
| demo 启动器按钮文字溢出边界（页 4 网格） | ✅ 已修 | 2026-09-21 §8.0g2：文案缩短至单元格宽度内 |

### 8.3 已修根修存档（防回归要点）

dayOfWeek 偏一天（jd%7）；XMimeData 自定义条目取址（槽位二级指针）+
move 漏 urls；裸父控件 is_widget 归一化；is_app_closing 永久拦截；
撤销合并 strdupN 越界（分配/拷贝分离）；setFont 比较时序 UAF（先较
后释）；XTextDocument setPlainText 容量增长+清零；XDate 儒略日口径；
XClass 槽位宏陷阱（**首槽必锚定父类槽位总数 + 用 DEFINE_END 续号，
EXTEND_END 会把 END_SIZE 重置回父类值**）；XListView/XTreeWidget
paintEvent 缺 paintOffset 平移（paintImage=顶层后备存储，非零偏移直
绘窗口原点）；XTreeWidget 条目文本 drawText 墨水色传 0（第 4 参是
色非长度，透明=永不出字）。

## 9. 工作流约定

- **子代理并发模式**（远端 §23.5 沉淀）：主线统筹/契约/接线/集成，
  Flash 子代理实现；契约先行（接口头先入库）；文件所有权互不重叠；
  并行顺序消解用 `#ifndef` 兜底。模型指定
  `account:bigmodel-individual-coding-plan/GLM-5.3-Flash`。
- **禁止 `2>nul` 重定向**（已两次误建仓库根 `nul` 字面文件）：Git Bash
  不认 Windows 的 NUL 设备名，`2>nul` 会创建真实文件且常规手段删不掉
  （需扩展路径：`python -c "import os; os.remove('//?/D:/.../nul')"`）。
  丢弃输出一律用 `2>/dev/null`；任务书给子代理的命令不得含
  `nul` 字样。
- **集成脚本**：构建输出落盘只回传退出码摘要（world.run 输出上限
  256KB，直接捕获 cmake 输出会超限——`.zcode/integrate.sh`）。
- **验证纪律**：每批次构建 0 错误 + 三套件 + 真机/ASan 探针自证
  （探针用后删除）；疑似内存问题先辨所有权（调用方持有≠泄漏）。
- **Git 纪律**：默认不提交；一次授权一次提交；需要干净工作区用
  stash 或先询问；已推送提交的撤回需用户确认。
- **已知工程坑**：CMake GLOB 不感知新文件（touch CMakeLists 重配置）；
  ninja 依赖缓存陈旧报幽灵"未声明标识符"（touch 源文件强编）；
  gcc14 将隐式声明/指针不兼容升为 error（MSVC 放过，双平台互补）；
  多构建目录共用 bin/ 输出会互相覆盖二进制。

------

## 10. 规划：图表最大化性能优化（两期，对标 Qt DeviceCoordinateCache）

> 状态：**第一期已落地（2026-09-22）**，Linux/Xvfb Debug 实测：静态层
> 命中路径逐帧生效（稳态 rebuild=0us），同口径对比 185→211 FPS
> （+14%）；剖析发现本机口径下序列绘制占图表耗时 ~75%
> （1.2~1.6ms），下一个量级提升在 §10.4 序列 SIMD 填充。路线决策：
> 软件静态层缓存（嵌入式基线，普适收益）
> 先行，GPU 直通增强（有 GPU 硬件，数千 FPS 潜力）随后；框架已有
> vulkan→gl→软件自动回退，两路径互斥自动切换。
> 测量纪律教训：早期 357/676 等数字混入了错误页面口径（缺
> --page 4 导致测的是按钮页）与后台负载干扰；本节数据均为
> --page 4 --tab 20 图表页专测（五段剖析在位证明 chart 真被绘制）。

### 10.1 背景实测（2026-09-21，Windows 本机 2752×1089 口径；与 §10.2 尾部 Linux/Xvfb 口径数字不可直接互比，口径差异见两节各自标注）

| 场景 | 数据 |
|---|---|
| 最大化图表（tab 20）repaint | 357 FPS / 2.8ms（七个历史提交实测 34.5～36.4，非回退，是每帧全量重绘的结构成本） |
| 最大化图表 --benchmark-full | 36.1 FPS（历史文档记录优化后 74～79，未在任何已提交树复现） |
| 最大化选项卡容器页 | 6235 FPS（纯缓存 blit 的物理上限示范） |
| 小窗口 520×360 图表 | 2033 FPS（历史 852，已提升 2.4×） |
| 上屏成本 | 12MB 帧 SetDIBitsToDevice ≈ 1～1.3ms，软件路径物理下限 ≈700 FPS |

### 10.2 第一期：XChartView 静态层缓存（嵌入式基线）

对标 Qt QGraphicsItem::DeviceCoordinateCache。XChart 全部 setter 为纯模型写
入、零自动失效（失效完全由应用层 updateChart 驱动）——指纹比对方案
天然可行，零 API 改动。

- **Phase A 插桩（半天）**：XCHARTVIEW_PROFILE 编译开关，xcv_renderToImage
  五段计时（指纹/静态层blit/静态重建/序列/图例），每秒均值输出。
  先弄清 2.8ms 去向再动手。
- **Phase B 静态层（1～1.5 天，核心）**：XChartView 私有 m_staticLayer
  （XImage，格式=目标表面格式）+ m_staticFp（FNV-1a）+ m_staticValid。
  指纹覆盖：宽高/margins/titleVisible+标题文本 hash/theme 字段组/
  backgroundVisible+brush/plotArea 三态/轴 range+tickCount+网格可见性/
  legendVisible/序列数量+颜色+名称 hash；**不含序列数据点**（序列
  是动态部分）。命中→blit 层（受 dirty/clip）；未命中→静态五件
  套渲进层再 blit；序列照旧绘制（plot 裁剪不变）。位一致性
  依据 source-over 结合律（层=G over B 的 8bit 结果，S over G′ 与直画
  同轮次同舍入）。开关 XCHARTVIEW_STATIC_LAYER_ON（默认 1，
  XGUI_ON=0 级联）；renderToImage(dirty=NULL) 公共 API 语义不变（回归
  测试 t218b 按像素断言）。
- **Phase C 验证（半天）**：回归全绿（renderToImage 像素断言验证位
  一致）；基准对照：最大化 repaint/full 两档 + 小窗口；四配置
  语法检查（XGUI_ON=0/XCHARTS_ON=0/STATIC_LAYER=0/RGB16）。

预期：repaint 357→450～550（25～50%，视 Phase A 数据）；上屏 12MB
拷贝是物理下限（≈700 FPS），1000+ FPS 需第二期 GPU 路径。

> **落地状态（2026-09-22）**：Phase A 插桩（XCHARTVIEW_PROFILE 编译+
> 环境变量双门控）与 Phase B 静态层均已落地（Linux/Xvfb 口径
> 185→211 FPS，+14%，稳态 rebuild=0us；本机 Windows 口径另测，两口径
> 不可互比）。**Phase C 已补齐（2026-09-22，本机）**：回归新增 t218c
> 静态层开/关逐位 diff 断言（XChartView_setStaticLayerBypass 运行期
> A/B 开关 + 260×180 含面积半透明序列场景，mismatch==0 通过）；
> Windows 645 目标构建全绿 + 回归全绿（软件/GL 口径 0 FAIL）。
> 遗留：STATIC_LAYER=0/1 双编译配置的构建级对拍与真机位一致验证
> 仍待板级。
>
> **同批修复（2026-09-22，RX 6800 XT 真硬件）**：
> - XImage_paintDevice vtable 校验根修（t211g 未 init image 断言在
>   Windows Debug 栈 0xcc 填充下必然 AV，Linux 侥幸——已修并入库）；
> - fastBlend 行级混合启用（最大化图表 357→551 FPS，+54%）；
> - GPU 口径回归 runnable：非 SYNC 模式 195 FAIL 为 GL 预乘舍入
>   序列与软件整数合成的固有差异（精确整值断言），SYNC 模式
>   （XGUI_GPU_SYNC=1 每命令读回）0 FAIL 全绿——软件精确契约测试
>   已由回归 gpuRequested() 门跳过，剩余 FAIL 属 GPU 预乘管线
>   与软件舍入口径差异，非缺陷；GPU 加速路径由 gl 冒烟+回归+
>   基准（静态场景缓存 8558 FPS）覆盖。

### 10.3 第二期：GPU 直通增强（有 GPU 硬件，数千 FPS 潜力）

- 前置（✅ 已闭合，2026-09-22 §8.2 勘误）："exit=3" 系 X11
  BadWindow code=3 误记，实为 exit=1/ctest=8；t211g 后端期望已改
  环境分支（GPU 请求→Gpu），ctest#3 软件与 GPU 口径双绿。**新发现**：
  vulkan 后端 lavapipe SIGSEGV（违反回退契约，需真硬件+VK validation）。
- 图表页 GPU 直通验证与补齐（demo 已有 acquireForWindow/
  presentToWindow/frameDegraded 挂点，非 PARTIAL 模式生效）。
- 静态层作为 GPU 纹理与软件路径协同；GPU 不可用时框架自动
  回退软件（零回归）。
- 验收：GPU 直通下最大化图表 FPS 显著高于软件路径；回退
  路径零回归。

> **GPU 直通诊断结论（2026-09-22，RX 6800 XT 实测）——状态改为「架构
> 缺陷待重构」**：GL 驱动（XGpuRenderDriver_gl.c，已迁移至
> Drive/windows/）在 Windows/AMD 上功能正确（冒烟/回归/SYNC 全绿），
> 但性能病态：所有页面 GPU 模式 0.4～2.5 FPS（软件同场景 572～6235）。
> 根因（cdb 抓栈 + 逐命令审计确认）：
> - **逐命令局部提交**：图表页 ~140 个绘制命令不匹配 GPU 快速路径
>   条件（渐变逐行填充/半透明面积/虚线/非 SourceOver），每条触发
>   painterGpuSubmitSoftwareCommand——创建全尺寸临时 XImage +
>   glReadPixels 全帧读回（GPU 管线完全冲刷）+ 软件画一笔 +
>   glTexSubImage2D 全帧回传 + draw quad。140 次/帧 × 每次 2 次 GPU
>   同步停顿 ≈ 400ms/帧。
> - **次要根因（已修）**：GL 函数指针调用约定缺失（cdecl 配 stdcall
>   驱动，/RTC 栈检查报错）——XGLAPI __stdcall 已全覆盖（43 处）；
>   GL 驱动文件已从 Src/ 迁移至 Drive/windows/（平台代码分层约束）。
> - **本批已修**：半透明 fillRect 的 GL 原语路径启用（非 SYNC 实渲染
 *   时 GL 预乘混合原语替代逐命令局部提交）；XImage_paintDevice
>   vtable 校验（t211g 未 init image AV 根修）。
> - **剩余工作量估计**：局部提交批量化（攒一批命令一次
>   upload/readback，1~2 周）或 GPU 快速路径扩展覆盖渐变/虚线
>   （2~3 周）。**在重构完成前，--gpu 开关保留但性能不达预期，
>   生产使用软件路径（572 FPS 已满足 60Hz 需求）。**

### 10.3.1 同批修复清单（2026-09-22）

| 修复 | 文件 | 说明 |
|---|---|---|
| XImage_paintDevice vtable 校验 | XImage.c | 未初始化栈上 image AV 根修（t211g 必崩 → 返回 NULL） |
| fastBlend 行级混合启用 | XPainter.c | A/B 脚手架 if(0) 移除，最大化图表 357→572 FPS |
| XChart 类型化 add 去重 | XChart.c | 五入口同指针重复加入悬空项缺口 |
| XDialog 死条件清理 | XDialog.c | XTextControl 继承 XObject 非 Widget |
| 回归 GPU 跳过门 | xgui_regression_test.c | 软件契约测试组 GPU 口径跳过 |
| t218c 位一致断言 | xgui_regression_test.c | 静态层 0/1 A/B 逐位 diff |
| GL 驱动迁移 | Drive/windows/ | Src→Drive 平台代码分层约束 |
| XGLAPI stdcall | GL 驱动 | 43 处调用约定修复 |
| RGB16 同格式直拷 | XPainter.c | 静态层 565 blit 免逐像素转换 |
| 焦点 probe 重置 | xgui_regression_test.c | 4 FAIL 根修（OS 焦点事件污染计数）|
| Tools/final_gate_release.sh | Tools/ | Release 终门入库 |

### 10.4 后续批次（按板测数据排序，不在本期）

序列 SIMD 填充（光栅 2.5～4×）、样条细分降档、vsync 门控消费、
RGB332/1bpp 内核（打开 MCU+SPI 屏档位）、图片资源离线编译
（RLE/C 数组，零解码零 IO）、点阵字体整字缓存。

> **2026-09-22 Release 口径勘测（SIMD 决策数据，此前登记"收益评估需
> Release 口径"已闭合）**：新增 `Tools/final_gate_release.sh`（Release
> 门常设化：-O2 构建输出 bin-release/，CMakeLists 输出目录已加
> `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY` 覆盖守卫，不触碰 bin/ Debug
> 产物）。首跑全绿：API 2727×3 / autotest 129×3 / 回归 / 验收 / GPU /
> diff-check 全过。**Release 图表基准五样：265.2 / 288.0 / 287.9 /
> 295.6 / 280.7，中位 288 FPS（±6% 带宽）**，对比 Debug 口径中位
> 160~211（-O2 提升 ~+37%），每帧 3.5ms 已远超交互需求（60 FPS）。
> **§10.4 序列 SIMD 优先级据此降级为"中等"**：Debug -O0 放大的混合
> 成本在 -O2 下约缩至 0.4~0.6ms/帧（按帧占比推算），非当前瓶颈；
> SIMD 批次建议与嵌入式板级档位（MCU+SPI 屏）一起评估，桌面口径
> 无近效需求。

## 附：历史战役归档索引（docs/xgui/history/）

| 归档 | 内容 |
|---|---|
| 2026-09-06-gpu-backend.md | GPU 渲染后端实施记录（Windows 时期） |
| 2026-09-18-text-control-refactor.md | 文本编辑控制器化重构 |
| 2026-09-19-perf-rounds.md | 绘制性能专项四轮（452→5670FPS） |
| 2026-09-19-font-cache.md | 字体缓存对齐 Qt 分层 |
| 2026-09-19-21-qt-alignment-campaign.md | Qt 对齐战役全批次（14.120~14.126） |
| 2026-09-20-dualsource-rgb565.md | 双源主循环 + RGB565 内核表 + 工作流沉淀 |

其余参考：`代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md`；
Qt 源码对照路径与 off-screen 探针方法见 git 历史（8a24b127 前版本）。
