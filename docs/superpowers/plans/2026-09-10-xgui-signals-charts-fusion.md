# XGui 三大工作流实施计划：Widgets 信号补齐 + Charts 批次 A-F + Fusion 风格引擎

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans (inline) 按批次执行本计划。步骤用 checkbox（`- [ ]`）跟踪。

**Goal:** 依次完成 Widgets 信号补齐（~30 个）、Charts 批次 A-F（209 个 API）、Fusion 风格引擎完整复刻，每批次全量验证。

**Architecture:** 全部对齐 Qt 6.8.3 源码（`/home/xinyue/Qt/6.8.3/Src`）：Widgets 信号沿用 `X*_signal(self)` 宏 + XObject 信号槽；Charts 在 `Src/XGui/Charts/` 现有 11 类上按缺口文档批次补齐；Fusion 新建 `Src/XGui/Style/` 下 XStyleOption 系列结构 + XCommonStyle/XFusionStyle 虚表架构，逐步接管 50 控件绘制。

**Tech Stack:** C99 + CMake + XObject/XClass 虚表 + XGui 软件渲染；验证：xgui_regression_test.c + xgui_window_demo + xdotool + ffmpeg 截图 + 25 裁剪构建。

**Spec:** 工作区文档《XGui Charts 模块 Qt 对齐缺口扫描与后续批次计划》（mnemon caca0980）+ `XGui.md` + 用户 2026-09-10 指示（Fusion 完整复刻；本轮只改代码不提交）。

## Global Constraints

- 严格遵循《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》：UTF-8 BOM、XObject/XClass 虚表、复制/移动统一 XCopy/XMove、_base 只查表分派、中文 Doxygen 全覆盖（@brief/@param 逐参数/@return 逐返回值）。
- API 完全对齐 Qt 6.8 不省略；旧 API 不保留（与 Qt 冲突者改名不并存）。
- 空参信号 args=NULL；新增跨头文件 API 后所有调用方必须包含完整原型头文件。
- 修改共享源（XPainter.c / XStyle 系列等被多配置编译的文件）后，必须重新 configure + 编译全部 25 个裁剪构建验证（GLOB 缓存）。
- 本轮**禁止 commit/push**（用户明确授权只改代码）；撤回用 reset --soft 不适用（无提交）。
- 每批次验证门：主构建编译通过 + xgui_regression_test 软件回归全绿 +（共享源改动时）25 裁剪构建全绿 + demo 截图交互验证。
- X11 头文件坑：include X11/vulkan 前先 rename 宏 + #undef XFree，include 后恢复。

---

## 工作流一：Widgets 信号补齐（~30 个，1 批次）

### Task 1: 信号缺口精确扫描
- [ ] 写临时扫描脚本（tools/ 下不提交），对 Qt 6.8.3 widgets 头文件提取 Q_SIGNALS 声明，与 Src/XGui/Widget/*.h 的 `*_signal` 宏声明逐一对照，输出缺口清单（预计 ~30 个）。
- [ ] 人工复核清单（排除内嵌类/明确不做项），落定最终信号列表。

### Task 2: 信号实现（1 批次全部完成）
- [ ] 按清单逐控件补齐：信号函数 + `_signal` 访问宏 + 触发点接线（用户交互路径中 emit）+ 中文 Doxygen（含父类宏转发到派生类）。
- [ ] 新增信号的触发点：参照 Qt 源码 emit 位置（如 XLineEdit textEdited 在输入事件、XComboBox activated 在选择路径）。
- [ ] 编译 + 软件回归全绿；demo 补信号触发交互验证（xdotool 注入 + 截图/日志取证）。
- [ ] 若动到共享源：25 个裁剪构建重新 configure + 全绿。

## 工作流二：Charts 批次 A-F（209 个，3-4 批次）

基准：Qt 6.8.3 `/home/xinyue/Qt/6.8.3/Src/qtcharts/src/charts/`；对照 `Src/XGui/Charts/`（XCHARTS_ON）。

### 批次 C1（= 原批次 A+B）：XChart 50 + XChartView 7
- [ ] XChart：addSeries/removeSeries/series/axes/createDefaultAxes/setTheme/themeFont/titleBrush/backgroundBrush/animationOptions/zoomIn/zoomOut/zoom/zoomReset/scroll/setMargins/plotArea 背景/mapToValue/mapToPosition 等 50 个。
- [ ] XChartView：setChart/setRubberBand/鼠标拖拽缩放事件（grabMouse 拖拽语义）。
- [ ] 序列泛化：XChart 的序列集合改经公共基类/接口管理（对标 QAbstractSeries），支撑 addSeries 泛型入口。
- [ ] 验证门全跑；demo 图表 tab 补 zoom/scroll/主题按钮交互取证（md5 对比截图互异）。

### 批次 C2（= 原批次 C）：XPieSeries 28 + XPieSlice 独立类
- [ ] 新增 `XPieSlice.{h,c}` 独立类（setLabel/setValue/percentage/exploded/labelVisible/borderColor 等，对标 QPieSlice）。
- [ ] XPieSeries：clicked/hovered/insert/isEmpty/setPosition/holeSize 等剩余 API；slice 集合改 XPieSlice* 管理。
- [ ] 渲染：pie 渲染适配 slice 独立属性（explode 偏移、label 可见性、边框色）。
- [ ] 验证门全跑 + 截图取证。

### 批次 C3（= 原批次 D+E）：Scatter/Area/轴 + 命中信号
- [ ] XScatterSeries 12（marker 枚举/borderColor/pointsVector 等）+ XAreaSeries 34（setUpperSeries/setLowerSeries/pointsVector/upper/borderColor 等）。
- [ ] XValueAxis 20（tickType/tickAnchor/applyTo 等）+ XCategoryAxis 8（startValue/labelsAngle 等）。
- [ ] 系列点击/悬停信号（clicked/hovered 最近点命中检测）+ QAbstractSeries 公共 API（setName/attachAxis 等）。
- [ ] 验证门全跑 + 截图取证。

### 批次 C4（= 原批次 F）：主题与动画 + 收口
- [ ] ChartTheme 8 种主题枚举全量色板 + 动画选项枚举 + 渲染适配（动画时长生效于 series 入场插值，预留字段落地）。
- [ ] 全模块 API 复扫（对 qtcharts 头文件逐项核对本批新增），缺口清零。
- [ ] 验证门全跑（含 25 裁剪构建，Charts 宏参与裁剪）+ 截图取证。

## 工作流三：Fusion 风格引擎（独立大工程，完整复刻 Qt Fusion）

基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/styles/qfusionstyle.cpp` + `qcommonstyle.cpp` + `qstyleoption*.h`。

### Task F1: 风格基座
- [ ] 新建 `Src/XGui/Style/XStyleOption.h/.c`：XStyleOption（state: hover/pressed/disabled/focus/on 等 + rect/palette）、XStyleOptionButton/Slider/SpinBox/ComboBox/Tab/Header/MenuItem/ProgressBar/Frame/Dock/ToolButton 等系列结构（对标 qstyleoption.h）。
- [ ] 新建 `XStyle.{h,c}`：QStyle 对标抽象基类——枚举（ControlElement/SubControl/PrimitiveElement/PixelMetric/StandardPixmap/StyleHint）+ 纯虚 drawPrimitive/drawControl/drawSubControl/sizeFromContents/pixelMetric/subElementRect 查表分派。
- [ ] 新建 `XCommonStyle.{h,c}`（对标 qcommonstyle.cpp）：通用 CC_*/PE_* 实现（按钮框、菜单项、进度条块、tab 框、frame 等）。
- [ ] XGuiConfig.h 增风格模块开关；接入应用级单例（对标 QApplication::setStyle）+ 控件绘制分派入口（控件 paintEvent 逐步改走 style->drawControl）。
- [ ] XPalette 扩展：QPalette 全部 20 色角色 + 组（Active/Inactive/Disabled）。
- [ ] 验证门全跑（风格为共享源，25 裁剪构建必须全绿）。

### Task F2: 基础控件绘制接管（Fusion 绘制）
- [ ] XFusionStyle（对标 qfusionstyle.cpp）：按钮（含 Primary/raised 感）、checkbox/radio indicator、line edit、slider/dial groove+handle、scrollbar、progressbar、tab/tabbar、menu/menubar 菜单项、groupbox、combobox、spinbox 子控件。
- [ ] 上述控件 hover/pressed/disabled/focus 全状态渲染 + demo 交互截图逐控件取证。
- [ ] 验证门全跑。

### Task F3: 复杂控件绘制接管 + 全量收口
- [ ] 剩余控件接入：dock/mdi/splitter/toolbox/table 表头/tree 指示器/lcd/dialog button box/rubber band/focus frame/tooltip 等（对标 qfusionstyle CC 全集）。
- [ ] 50 控件绘制全部经 style 引擎；渲染审计（GPU 三后端一致性，XGUI_GPU_SYNC=1）。
- [ ] 全模块最终验证：软件回归 + GPU 回归（GL/Vulkan）+ CTest 3/3 + 25 裁剪构建全绿 + demo 全 tab 截图存档。

## 执行顺序与状态

1 → 2（C1→C2→C3→C4）→ 3（F1→F2→F3），严格依次，每批次验证门通过才进下一批次。
