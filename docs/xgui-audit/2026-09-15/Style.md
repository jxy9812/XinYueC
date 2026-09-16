# XGui ↔ Qt 6.8.3 Style 模块对齐审计报告

- 审计日期：2026-09-15
- 审计范围：`Src/XGui/Style/`（10 个头文件 + 10 个实现文件）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/styles`（qstyle.h、qcommonstyle.h、qwindowsstyle_p.h、qfusionstyle_p.h、qstylesheetstyle_p.h、qstyleoption.h、qstylefactory.h、qproxystyle.h、qstylepainter.h、qstyleplugin.h）+ `src/gui/kernel/qpalette.h`、`qstylehints.h`、`qsurfaceformat.h`
- 审计方式：只读；未修改 `Src/`、`Test/` 任何源码；未 commit/push
- 基准说明（重要）：Qt 6.8.3 的 `QWindowsStyle`、`QFusionStyle`、`QStyleSheetStyle` 均**没有公共头文件**，分别声明在 `qwindowsstyle_p.h`、`qfusionstyle_p.h`、`qstylesheetstyle_p.h`（私有/自动测试导出）；`qstylesheet.h`（Qt5 的 QStyleSheet 类）在 Qt 6.8.3 qtbase 中**已不存在**（CSS 解析器为 `qcssparser_p.h` 私有头）。本次审计以这些实际存在的头文件为基准，并把"公共 API 对齐"理解为 XGui 对外提供的 C API 与 Qt 类公开/受保护行为对齐。

---

## 1. 模块概览

| X 类 | Qt 对应类 | Qt 头文件 | 继承链是否 1:1 | API 缺口 | 功能缺口 | 完整度 |
|---|---|---|---|---|---|---|
| XStyle | QStyle | widgets/styles/qstyle.h | ✅ | 25 | 8 | 35% |
| XCommonStyle | QCommonStyle | widgets/styles/qcommonstyle.h | ✅ | 11 | 12 | 45% |
| XWindowsStyle | QWindowsStyle | widgets/styles/qwindowsstyle_p.h（私有） | ✅ | 14 | 3 | 20% |
| XFusionStyle | QFusionStyle | widgets/styles/qfusionstyle_p.h（私有） | ❌ | 18 | 9 | 25% |
| XStyleSheetStyle | QStyleSheetStyle | widgets/styles/qstylesheetstyle_p.h（私有） | ✅ | 20 | 10 | 30% |
| XCssStyleSheet | （Qt6 无 QStyleSheet；对标 QCss 私有解析器子集） | 无 | —（值类型） | 3 | 4 | 50% |
| XStyleOption | QStyleOption 系列（20 个类） | widgets/styles/qstyleoption.h | ❌（扁平化） | 20 | 3 | 30% |
| XPalette | QPalette | gui/kernel/qpalette.h | ✅（值类型） | 15 | 6 | 55% |
| XStyleHints | QStyleHints | gui/kernel/qstylehints.h | ✅ | 1 | 2 | 90% |
| XSurfaceFormat | QSurfaceFormat | gui/kernel/qsurfaceformat.h | ✅（值类型） | 2 | 1 | 92% |

模块总体结论：**XStyleHints / XSurfaceFormat 两个值/单例类对齐度高（90%+）；样式引擎主体（XStyle/XCommonStyle/XFusionStyle/XStyleSheetStyle）是"嵌入式近似边界"，距 Qt 6.8.3 行为完整复刻差距很大**。最严重的问题：① XFusionStyle 继承链错误（XWindowsStyle 而非 XCommonStyle，违反硬约束 2）；② XStyle 基类虚表只有 7 个槽位，QStyle 的 styleHint/subElementRect/subControlRect/hitTestComplexControl/standardIcon/standardPixmap/layoutSpacing 等核心虚函数完全没有；③ 样式绘制大面积缺 case（枚举声明了但 switch 静默 no-op）；④ XPalette 多个颜色值与 qt_fusionPalette 不一致且缺 Accent 角色；⑤ `XStyle_installStyleSheet` 替换默认样式时存在所有权泄漏。

---

## 2. 硬约束逐条核查（Style 模块）

| # | 约束 | 核查结果 | 结论 |
|---|---|---|---|
| 1 | 拥有型字符串一律 XString*；UTF-8 const char* 用 `_2` 后缀 | XCssStyleSheet 的声明值/选择器/属性值全部为 XString*；XStyleOption::m_text 为借用 const char*；未发现 char[N] 长期持有。XCssStyleSheet.c 解析用 4 个 `char[64]` 局部临时缓冲（瞬时，非长期持有），但选择器名/属性名/属性值超过 63 字节会被**截断** | ✅（附注：64 字节 token 截断风险） |
| 2 | 继承 1:1 含中间基类层 | XStyle→XObject ✅（QStyle→QObject）；XCommonStyle→XStyle ✅；XWindowsStyle→XCommonStyle ✅；XStyleSheetStyle→XWindowsStyle ✅；**XFusionStyle→XWindowsStyle ❌（Qt 6.8.3 为 QFusionStyle : QCommonStyle，中间层多了一级 XWindowsStyle）** | ❌ 违规 1 处 |
| 3 | 样式/绘制不得精简近似 | XCommonStyle 的 drawPrimitive/drawControl 大量枚举缺 case 静默 no-op；pixelMetric 仅 21 项且多项数值与 Qt 不同；sizeFromContents 仅 3 种 CT 且公式与 Qt 不同；XFusionStyle 仅覆盖 3 个 PE 且用逐行渐变近似圆角；XStyleSheetStyle 仅实现 9 个可用属性（Qt 约 100 个）；QStyle 静态工具（visualRect/alignedRect/sliderPositionFromValue 等）全部缺失 | ❌ 违规 |
| 4 | 公共头中文 Doxygen + UTF-8 BOM | 10 个 .h 全部带 BOM（EF BB BF）✅；XPalette/XStyleHints/XSurfaceFormat 头文件 Doxygen 完整；XStyle.h/XCommonStyle.h/XWindowsStyle.h/XFusionStyle.h/XStyleSheetStyle.h/XCssStyleSheet.h/XStyleOption.h 均有 @brief/@param/@return。**XStyleHints.h 的 20 余个属性 getter/setter 与 13 个信号只有单行 `/** @brief ... */`，无逐参数 @param/@return**（XStyleHints.h:103-212） | ⚠️ 部分不合规（文档不完整） |
| 5 | 信号：空参 args=NULL；Qt 6.8 每信号有 *_signal | XStyleHints 13 个信号与 Qt 6.8 一一对应且参数齐全，经 XVarList 传递 ✅；样式类本身无 Qt 信号 ✅ | ✅ |
| 6 | 生命周期：init/deinit_base 成对；copy/move 安全；禁 memcpy 复制对象；禁直接 malloc/free/strdup | 未发现直接 malloc/free/strdup/memcpy 复制对象（XPalette/XSurfaceFormat 用结构体赋值；XMemcpy 仅用于解析局部缓冲）✅；**XStyle_installStyleSheet（XStyle.c:170-192）替换 g_defaultStyle 时未删除旧默认样式：旧指针被 `m_source` 借用后无人拥有 → 泄漏；若先 setDefaultStyle 再 installStyleSheet 同样泄漏旧默认样式** | ❌ 违规 1 处（所有权泄漏） |
| 7 | 旧 API 不保留（与 Qt 冲突者改名不并存） | XStyleHints_keyboardAutoRepeatRate（int）保留——Qt 6.8 中该接口仍在（6.5 起弃用），属对齐保留 ✅；未发现新旧双轨接口 | ✅ |
| 8 | 新代码 C99 | 未发现 C++/C11 语法；复合字面量（`&(XRect){...}`）为 C99 特性；声明位置符合 C99 | ✅ |

---

## 3. 逐类对比

### 3.1 XStyle ↔ QStyle（qstyle.h）

**继承**：X: `XStyle → XObject`；Qt: `QStyle → QObject`。✅ 1:1（QStyle 无 QPaintDevice 中间层，仅 QObject）。

**API 缺口表**（Qt 原型 → X 现状）：

| 缺失 Qt API（Qt 6.8.3 原型） | X 现状 |
|---|---|
| `QString name() const` | 无 |
| `virtual void polish(QApplication*)` / `unpolish(QApplication*)` | 无（只有 XWidget* 版） |
| `virtual void polish(QPalette&)` | 无 |
| `virtual QRect itemTextRect(const QFontMetrics&, const QRect&, int, bool, const QString&) const` | 无 |
| `virtual QRect itemPixmapRect(const QRect&, int, const QPixmap&) const` | 无 |
| `virtual void drawItemText(QPainter*, const QRect&, int, const QPalette&, bool, const QString&, QPalette::ColorRole)` | 无 |
| `virtual void drawItemPixmap(QPainter*, const QRect&, int, const QPixmap&) const` | 无 |
| `virtual QPalette standardPalette() const` | 无 |
| `virtual QRect subElementRect(SubElement, const QStyleOption*, const QWidget*) const` | 无（枚举 SubElement 整个缺失） |
| `virtual SubControl hitTestComplexControl(ComplexControl, const QStyleOptionComplex*, const QPoint&, const QWidget*) const` | 无 |
| `virtual QRect subControlRect(ComplexControl, const QStyleOptionComplex*, SubControl, const QWidget*) const` | 无（SubControl 枚举缺失，X 用零散 bool 代替） |
| `virtual int styleHint(StyleHint, const QStyleOption*, const QWidget*, QStyleHintReturn*) const` | XStyle 虚表无该槽；StyleHint 枚举缺失；XWindowsStyle_styleHint 未挂表 |
| `virtual QPixmap standardPixmap(...)` / `virtual QIcon standardIcon(...)` / `virtual QPixmap generatedIconPixmap(...)` | 全部缺失 |
| `static QRect visualRect/visualPos/visualAlignment/alignedRect(...)`、`static int sliderPositionFromValue/sliderValueFromPosition(...)` | 全部缺失 |
| `virtual int layoutSpacing(...)` / `int combinedLayoutSpacing(...)` | 缺失 |
| `const QStyle* proxy() const` | 无（无代理概念） |
| 枚举 State（25 位） | 缺失 HasEditFocus/ReadOnly/Small/Mini；**Editing=0x200000、KeyboardFocusChange=0x400000、Sibling=0x800000 与 Qt（0x400000/0x800000/0x200000）位值不一致** |
| 枚举 PrimitiveElement（44 项） | 30 项；缺 FrameDefaultButton、FrameDockWidget、FrameMenu、FrameStatusBarItem、FrameWindow、FrameButtonTool、IndicatorButtonDropDown、IndicatorDockWidgetResizeHandle、IndicatorHeaderArrow、IndicatorMenuCheckMark、IndicatorTabTear、PanelScrollAreaCorner、PE_Widget、IndicatorColumnViewArrow、IndicatorItemViewItemDrop、PanelItemViewRow、PanelStatusBar、IndicatorTabClose 等 |
| 枚举 ControlElement（46 项） | 33 项；缺 CE_MenuScroller、CE_MenuVMargin/HMargin、CE_MenuTearoff、CE_ScrollBarFirst/Last、CE_FocusFrame、CE_ToolBoxTabShape/Label、CE_HeaderEmptyArea、CE_ColumnViewGrip 等 |
| 枚举 ComplexControl（9 项） | 7 项；缺 CC_TitleBar、CC_MdiControls；且 X 的 Dial=5/GroupBox=6 与 Qt（Dial=6/GroupBox=7）数值错位 |
| 枚举 PixelMetric（约 100 项） | 21 项，且**数值整体重排**（自洽子集） |
| 枚举 ContentsType（24 项 CT_*） | **完全没有枚举**，sizeFromContents 用裸 int 0-5 |
| 枚举 StyleHint（约 120 项 SH_*） | 完全没有枚举 |
| 枚举 StandardPixmap（SP_*） | 完全没有 |
| QStyleHintReturn 系列 | 无 |

**功能缺口**：
1. 虚表槽位仅 7 个（DrawPrimitive/DrawControl/DrawComplexControl/PixelMetric/SizeFromContents/Polish/Unpolish），核心查询/几何/图标能力无分派点，控件无法走 style 查询尺寸/子矩形/命中；
2. `XStyle_drawPrimitive/drawControl` 等对空 option/painter 直接 return（合理防御），但槽位为 NULL 时静默忽略——枚举已声明而未实现的分支全部"静默不画"；
3. `XStyle_installStyleSheet`（见约束 6）替换默认样式泄漏旧对象；
4. `XStyle_defaultStyle` 惰性创建 XCommonStyle，与 Qt QApplication::style 的工厂/主题语义不同（无 QStyleFactory keys/create）。

**违规/问题**：继承 ✅；枚举位值不一致（State 三位）；无 SubElement/SubControl/CT/SH/SP 枚举；无 styleHint 槽位。

### 3.2 XCommonStyle ↔ QCommonStyle（qcommonstyle.h）

**继承**：X: `XCommonStyle → XStyle`；Qt: `QCommonStyle → QStyle`。✅

**API 缺口**（QCommonStyle 覆写/Qt 公共行为，X 未实现）：
- `subElementRect`、`hitTestComplexControl`、`subControlRect`（3 个几何/命中虚函数无槽位）
- `styleHint`、`standardIcon`、`standardPixmap`、`generatedIconPixmap`、`layoutSpacing`（5 个查询虚函数）
- `polish(QPalette&)`（Qt 6.8 为 QStyle::polish 空转发，缺口影响小）、`polish(QApplication*)`、`unpolish(QApplication*)`
- 静态/工具类 API 随 XStyle 缺失

**功能缺口**：
1. **drawPrimitive 只实现 15/30 个已声明 PE**：FrameGroupBox、FrameTabWidget、FrameTabBarBase、PanelMenu、PanelTipLabel、IndicatorProgressChunk、IndicatorToolBarHandle、IndicatorToolBarSeparator、IndicatorBranch、PanelItemViewItem、IndicatorItemViewItemCheck 等声明了枚举但 switch 无 case（XCommonStyle.c:587-646）；
2. **drawControl 只实现 14/33 个已声明 CE**：CE_PushButtonLabel、CE_CheckBoxLabel、CE_RadioButtonLabel、CE_TabBarTab、CE_ProgressBar、CE_ProgressBarLabel、CE_MenuBarEmptyArea、CE_ToolButtonLabel、CE_Header、CE_ScrollBarSlider/AddLine/SubLine/AddPage/SubPage、CE_ComboBoxLabel、CE_ToolBar、CE_ShapedFrame、CE_ItemViewItem 全部 no-op（XCommonStyle.c:1909-1967）；
3. **pixelMetric 数值与 Qt 6.8 不一致**（XCommonStyle.c:1969-1998）：

| PM | X | Qt 6.8 QCommonStyle | 差异 |
|---|---|---|---|
| PM_ButtonShiftHorizontal/Vertical | 0 | 2（落入 PM_DefaultFrameWidth 分支） | ❌ |
| PM_TabBarTabOverlap | 0 | 3 | ❌ |
| PM_TabBarTabHSpace | 12 | dpiScaled(24) | ❌ |
| PM_TabBarTabVSpace | 6 | Rounded 8 / Triangular 3 / 2 | ❌ |
| PM_ToolBarItemSpacing | 1 | dpiScaled(4) | ❌ |
| PM_ToolBarHandleExtent | 10 | dpiScaled(8) | ❌ |
| PM_DockWidgetTitleBarButtonMargin | 4 | dpiScaled(2) | ❌ |
| PM_SplitterWidth | 5（应在 Windows 层） | 4（QWindowsStyle） | ❌ |
| 其余 | — | 均经 QStyleHelper::dpiScaled | ⚠️ 无 DPI 缩放（嵌入式固定 DPI 可接受，需文档化） |

4. **sizeFromContents 仅处理 CT 0/1/2 且公式错误**：Qt CT_PushButton 为 `w += PM_ButtonMargin + 2*PM_DefaultFrameWidth`（+10），X 为 `w += 2*margin`（+12）、高仅 +margin（+6，Qt 为 +10）；CT_CheckBox/RadioButton Qt 为 `size + (indicatorWidth + 4 + labelSpacing, 4)`，X 为 `(w+12, h+6)`（XCommonStyle.c:2000-2023）；CT_ToolButton/ComboBox/Splitter/ProgressBar/MenuItem/MenuBarItem/Slider/ScrollBar/LineEdit/SpinBox/SizeGrip/TabWidget/HeaderSection/GroupBox 等全部无处理；
5. **polish(QWidget*)/unpolish(QWidget*) 槽位未覆写**（Qt 6.8 QCommonStyle 对 QWidget 有实际 polish 行为，如设置默认字体前滚）；
6. CC_Dial（xcs_drawDial）算法较完整复刻 Qt（刻度/凹槽/箭头/描边），CC_ScrollBar 渐变与 Qt Fusion 公式接近——这两处是模块内质量最高的绘制；但 CC_ComboBox 无 popupRect/editable/frame 语义、CC_ToolButton 用 `m_checkState==1`+`m_progressMin` 复用字段传箭头方向（XCommonStyle.c:1495-1515），属 hack，且不绘制图标/文本/菜单箭头。

### 3.3 XWindowsStyle ↔ QWindowsStyle（qwindowsstyle_p.h，Qt 私有头）

**继承**：X: `XWindowsStyle → XCommonStyle`；Qt: `QWindowsStyle → QCommonStyle`。✅

**API 缺口**（QWindowsStyle 覆写，X 全部未挂表）：
- `polish(QApplication*)/unpolish(QApplication*)`、`polish(QWidget*)/unpolish(QWidget*)`、`polish(QPalette&)`
- `drawPrimitive`、`drawControl`、`subElementRect`、`drawComplexControl`、`sizeFromContents`、`pixelMetric`、`standardPixmap`、`standardIcon`
- `eventFilter(QObject*, QEvent*)`（受保护）

**功能缺口**：
1. XWindowsStyle 虚表新增的 StyleHint 槽位为 **NULL 占位**（XWindowsStyle.c:15），独立函数 `XWindowsStyle_styleHint`（XWindowsStyle.c:47-55）**未挂入虚表**，且实现错误：返回 `XStyle_pixelMetric(..., XStylePM_DefaultFrameWidth, ...)`——即"styleHint 查询返回 2"；Qt 的 QWindowsStyle::styleHint 至少处理 SH_EtchDisabledText/SH_ItemView_ShowDecorationSelected 等；
2. Qt 的 QWindowsStyle::pixelMetric 有系统度量（PM_SplitterWidth=4 等），X 全部走基类；
3. X 头文件声明"StyleHint 槽：当前返回基类值"与实现不符（实现返回的是 pixelMetric）。

### 3.4 XFusionStyle ↔ QFusionStyle（qfusionstyle_p.h，Qt 私有头）

**继承**：X: `XFusionStyle → XWindowsStyle → XCommonStyle → XStyle`；Qt 6.8.3: `QFusionStyle → QCommonStyle → QStyle → QObject`。**❌ 不 1:1**——X 多了一级 XWindowsStyle 中间基类（硬约束 2 违规；QWindowsStyle 的 StyleHint 槽位偏移还会污染 XFusionStyle 的虚表布局，将来修正时需重排）。

**API 缺口**（QFusionStyle 覆写/新增，X 未实现）：
- `QPalette standardPalette() const`（Fusion 调色板入口——X 完全没有，XPalette 是独立值类型）
- `pixelMetric`（Fusion 有大量专用 PM）
- `subElementRect`、`hitTestComplexControl`、`subControlRect`
- `generatedIconPixmap`、`iconFromTheme`、`standardIcon`、`standardPixmap`、`drawItemPixmap`、`drawItemText`
- `styleHint`（含 Fusion 的 SH_* 特殊值）
- `itemPixmapRect`
- `polish(QWidget*)/polish(QApplication*)/polish(QPalette&)`、`unpolish(QWidget*)/unpolish(QApplication*)`
- `drawPrimitive` 覆盖范围：Qt Fusion 覆盖 PE_FrameFocusRect、PE_PanelButtonCommand/Bevel/Tool、PE_IndicatorCheckBox、PE_IndicatorRadioButton、PE_IndicatorItemViewItemCheck、PE_IndicatorHeaderArrow、PE_FrameDefaultButton 等；X 只覆盖 PanelButton*、IndicatorCheckBox、IndicatorRadioButton 3 组
- `drawControl`：Qt Fusion 覆写（CE_ShapedFrame 等）；X 是纯透传（XFusionStyle.c:274-284）

**功能缺口**：
1. Fusion 主题仅实现"圆角按钮面板渐变 + 复选/单选"，**CC_Dial/CC_Slider/CC_ScrollBar/CC_SpinBox/CC_ComboBox/CC_ToolButton/CC_GroupBox 全部透传公共样式**——Qt Fusion 对滚动条/滑块/微调框有完全不同的渐变与描边（XCommonStyle 的 scrollbar 反而是按 Fusion 公式写的，但 XFusionStyle 未接管）;
2. 按钮面板"圆角 2px"被矩形近似（XFusionStyle.c:114-120 注释自认"矩形近似"）；渐变用逐行 lerp 近似 QLinearGradient（Qt 还有 dpi 缩放与 disabled/hover 细分）；
3. 颜色常量与 Qt 不符：`XFS_HIGHLIGHT_DEFAULT=0xFF2A82DA`（Qt 为 #308CC6）、`XFS_BORDER_DARK=0xFF9E9E9E`（Qt outline 算法为 window.darker(140) 等）;
4. 复选对勾坐标硬编码（r.x+3..r.x+10）与 Qt 的相对缩放算法不同;
5. `XFusionStyle_installDefault` 是 X 新增便捷 API（Qt 无对应，合理）。

### 3.5 XStyleSheetStyle ↔ QStyleSheetStyle（qstylesheetstyle_p.h，Qt 私有头）

**继承**：X: `XStyleSheetStyle → XWindowsStyle`；Qt: `QStyleSheetStyle → QWindowsStyle`。✅

**API 缺口**：
- `QStyle* baseStyle() const`、`repolish(QWidget*)`、`repolish(QApplication*)`
- `updateStyleSheetFont/saveWidgetFont/clearWidgetFont/unsetStyleSheetFont`、`styleSheetPalette`、`setPalette/unsetPalette`、`setProperties`、`setGeometry`
- 覆写缺口：`pixelMetric`、`styleHint`、`subElementRect`、`subControlRect`、`sizeFromContents`、`standardPalette`、`standardIcon`、`standardPixmap`、`layoutSpacing`、`generatedIconPixmap`、`itemTextRect`、`itemPixmapRect`、`hitTestComplexControl`、`event(QEvent*)`
- `renderRule`/`positionRect`/`defaultSize`/`titleBarLayout` 等私有渲染规则体系（对应 X 的 xsss_lookup 简化版）

**功能缺口**：
1. **属性子集**：X 仅 25 个属性 ID 且实际生效 9 个（color/background-color/background/border 系列/padding 系列/font-family/font-size/text-decoration）；Qt qcssparser 约 100 个属性（border-style、background-image/position/repeat、image、icon、min/max/width/height、qproperty-*、spacing、selection-* 等）。**XCssProperty_Margin/Font/FontWeight/FontStyle/MinWidth/MinHeight/MaxWidth/MaxHeight/Width/Height 被解析但从不消费（死属性）**；
2. 无 QRenderRule 缓存与继承语义：Qt 按规则缓存渲染规则并处理 border-box/content-box、子控件几何（positionRect 的 4 盒语义）；X 每次绘制线性查找，无缓存；
3. 无伪元素（::section、::drop-down、::handle 等）与子控件规则；
4. 无背景图片/渐变、无 border 线型（solid/dashed/dotted）、无圆角裁剪（radius>0 仅画圆角框）；
5. 无 widget palette/font 篡改与恢复（Qt 的 Tampered<QPalette>/<QFont> 机制）、无样式字体更新钩子（updateStyleSheetFont）；
6. 匹配语义简化：Qt 用专用 StyleSelector（specificity 排序含 !important 全局优先）；X 用"同规则内 important 覆盖 + 跨规则 specificity 比较"（xsss_findDecl/xsss_lookup），**!important 只在一规则内部生效，跨规则 !important 语义与 Qt 不同**；
7. drawPrimitive/Control/ComplexControl 三层包装相同（文本色/字体/盒模型前置 + 背景/边框后置），对 CE 绘制"覆盖后置背景"的叠层顺序与 Qt 的 render rule 组合顺序存在偏差风险。

### 3.6 XCssStyleSheet ↔ QStyleSheet（Qt 6.8.3 无此类；对标 Qt5 qstylesheet.h / Qt6 qcssparser_p.h 子集）

**说明**：Qt 6.8.3 qtbase 无 `qstylesheet.h`、无 QStyleSheet 类（Qt5 才有，Qt6 内联进 QStyleSheetStyle + QCss 私有解析器）。X 的头文件自称"对标 qcssparser_p.h 的 Property 枚举子集"，定位准确。

**API/功能**：
- XCssProperty 25 项 vs Qt QCss::Property 约 100 项；XCssPseudoClass 8 位 vs Qt 完整伪类（:first/:last/:only/:checked/:indeterminate/:default/:on/:off 等缺失）；
- 解析器功能正常：注释、!important、逗号选择器、后代/子代关系、属性选择器 6 种匹配、伪类均实现；**选择器名/属性名/值用 char[64] 截断**（XCssStyleSheet.c:289-297），超长 token 静默截断无诊断；
- `XCssStyleSheet_parse` 语法错误不报告（Qt 会输出 parser warning），返回 true/false 仅反映分配失败；
- 生命周期正确：clear 释放 XString 与堆数组；无泄漏迹象；
- 该类型是 XStyleSheetStyle 的持有数据对象（m_sheet），职责清晰，但作为"公开 API"暴露了解析细节（Qt 中 QCss 是私有的）——设计取舍，可接受。

### 3.7 XStyleOption ↔ QStyleOption 系列（qstyleoption.h）

**结构**：Qt 是 1 个基类 + 3 个 hint 类 + 16 个派生选项类（QStyleOptionFocusRect/Frame/TabWidgetFrame/TabBarBase/Header/HeaderV2/Button/Tab/ToolBar/ProgressBar/MenuItem/DockWidget/ViewItem/ToolBox/RubberBand/Complex/Slider/SpinBox/ToolButton/ComboBox/TitleBar/GroupBox/SizeGrip/GraphicsItem）；X 是**单个扁平结构体**（版本/类型/状态/矩形/调色板 + 通用字段 + 各控件专属字段拼装）。

**API 缺口**：
- 基类字段缺失：`Qt::LayoutDirection direction`、`QFontMetrics fontMetrics`、`QObject* styleObject`；`void initFrom(const QWidget*)` 缺失（XStyleOption_init 只置默认值）；
- QStyleOption 系列 20 个类全部缺失（无类型安全，用 int m_type + 位/布尔字段模拟）；
- 无 `qstyleoption_cast` 等价物；
- QStyleHintReturn / QStyleHintReturnMask / QStyleHintReturnVariant 三个类缺失（与 XStyle 无 styleHint 槽位呼应）；
- X 的 State 枚举位值（Editing/KeyboardFocusChange/Sibling）与 Qt 不一致（见 3.1），跨模块填 state 的控件需注意。

**功能缺口**：
1. 扁平结构使"复合控件子控件命中/几何"（hitTestComplexControl/subControlRect 的输入输出）无法表达，是 XStyle 缺失这些虚函数的直接结构性原因；
2. `m_text` 借用 const char*（生命周期由调用方保证，文档已注明），合规但易悬挂；
3. XStyleOption_init 的默认值（version=1、进度 0-100、滑块 0-100/单步 1/页步 10）与 Qt 各选项类默认值大体一致。

### 3.8 XPalette ↔ QPalette（gui/kernel/qpalette.h）

**结构**：QPalette 为隐式共享类（QPalettePrivate* + currentGroup）；XPalette 为 POD 值类型（4×21 XColor 数组）。值语义对标成立。

**API 缺口**（Qt 原型 → X）：
- `const QBrush& brush(ColorGroup, ColorRole) const` / `void setBrush(...)` / `bool isBrushSet(...)` / `void setColorGroup(...)` / `bool isEqual(ColorGroup, ColorGroup)` / `bool isCopyOf(...)` / `qint64 cacheKey()` / `QPalette resolve(const QPalette&) const` / `resolveMask()` / `setResolveMask(...)` / `currentColorGroup()` / `setCurrentColorGroup(...)` / `operator==` / 构造族（QColor button、brush 组、windowText/window 等）/ 18 个便捷 getter（windowText()/button()/...）/ `operator QVariant`
- **ColorRole 缺 Accent**：Qt 6.8 有 22 个角色（PlaceholderText 之后为 Accent）；X 枚举止于 PlaceholderText（21 个），与头文件"取值与排序一致"声明不符；
- ColorGroup 顺序与 Qt 不同：Qt {Active, Disabled, Inactive, **NColorGroups, Current, All, Normal**}；X {Active, Disabled, Inactive, Current, NColorGroups}（无 All/Normal）；
- 无 resolve 掩码 → 无 Qt 的 palette 继承/部分解析语义（XWidget 层需自证替代方案）。

**功能缺口（颜色值 vs Qt 6.8 qt_fusionPalette，qplatformtheme.cpp:356-405）**：

| 角色 | XPalette.c | Qt 6.8 | 差异 |
|---|---|---|---|
| Light | #FFFFFF | backGround.lighter(150)=#F7F7F7 | ❌ |
| Midlight | #F7F7F7（注释写 qt_mix_colors） | mid.lighter(110)=#BFBFBF | ❌ |
| Highlight | #3D8EC9（注释写 #308cc6） | #308CC6 | ❌（且注释与代码不一致） |
| Disabled Base | #F0F0F0 | backGround=#EFEFEF | ❌ |
| Disabled Shadow | #B1B1B1 | shadow.lighter(150)=#BABABA | ❌ |
| Accent | 无角色 | = Highlight / disabledHighlight | ❌ |
| 深色主题 | 无 | darkAppearance 分支（windowText/背景 #323232 等） | ⚠️ 未实现（嵌入式可豁免但需文档化） |
| 其余（Window/Button/Base/Text/Dark/Mid/Shadow/disabledText/darkDisabled/placeholder 等） | ✅ | 一致 | ✅ |

- `XPalette_color` 的 Current→Active 归一化与 Qt 的 currentGroup 语义不同（Qt Current 组由 currentColorGroup 决定，可能是 Inactive）；X 固定映射 Active，头文件已明示简化。

### 3.9 XStyleHints ↔ QStyleHints（gui/kernel/qstylehints.h）— 模块内对齐度最高

**继承**：X: `XStyleHints → XObject`；Qt: `QStyleHints → QObject`。✅

**API**：13 个可写属性 + 13 个通知信号与 Qt 6.8 一一对应；只读常量（fontSmoothingGamma、passwordMaskCharacter/Delay、setFocusOnTouchRelease、showIsFullScreen/Maximized、startDragVelocity、useRtlExtensions、singleClickActivation、mouseDoubleClickDistance、touchDoubleTapDistance）齐全；`keyboardAutoRepeatRate`（int，Qt 6.5 弃用）与 `keyboardAutoRepeatRateF`（float）并存，与 Qt 6.8 完全一致；`unsetColorScheme` 存在；TabFocusBehavior/ContextMenuTrigger/ColorScheme 枚举值与 Qt 一致。`wheelScrollLines` 的零/负回退 3 已按 qstylehints.cpp:618-624 对齐（XGui.md 10.286 有记录）。

**功能缺口**：
1. 无平台主题刷新：Qt 的只读值来自 QPlatformTheme/平台集成，X 为固定默认 + 显式 setter（头文件已声明，属嵌入式边界）；
2. `styleHints_emit` 用"函数指针转 size_t"当信号 ID（XStyleHints.c:110-116），依赖实现细节；信号发射路径与 XObject 事件队列绑定，若 XStyleHints 单例先于事件系统初始化可能丢失信号（需确认 XGuiApplication_styleHints 的惰性初始化时序）。

### 3.10 XSurfaceFormat ↔ QSurfaceFormat（gui/kernel/qsurfaceformat.h）— 对齐度最高

**结构**：值类型；字段与 QSurfaceFormatPrivate 一一对应（含 C 兼容缓存 m_stereo）。

**API**：全量 getter/setter（depth/stencil/RGB/alpha/samples/swapBehavior/hasAlpha/profile/renderableType/version/setVersion/stereo/options/setOption/testOption/swapInterval/colorSpace/setDefaultFormat/defaultFormat/equals）与 Qt 6.8 对齐；`XSurfaceFormat_equals` 字段集合与 qsurfaceformat.cpp:827-842 的 operator== 完全一致（不含 renderableType/colorSpace/m_stereo，含 options/swapBehavior/profile/版本/swapInterval）；`stereo()` 以 StereoBuffers 选项位为唯一真源（对齐 qsurfaceformat.h:147-150）；`XSurfaceFormat_create` 独立于进程默认格式（对齐 qsurfaceformat.cpp 构造语义）；hasAlpha 为 `alphaBufferSize>0`（对齐 :428-437）。

**缺口**：仅 Qt 6.0 弃用的 `ColorSpace` 枚举与 `setColorSpace(ColorSpace)` 重载、QDebug 流运算符未提供（弃用 API 不实现符合约束 7）；无 `operator!=` 显式接口（equals 可表达）。

---

## 4. 缺失 Qt 类清单（Qt 基准范围内，XGui 完全没有）

| Qt 类 | Qt 头文件（相对 qtbase） | 建议 |
|---|---|---|
| QProxyStyle | src/widgets/styles/qproxystyle.h | 建议实现（样式表/主题叠加常用；XStyleSheetStyle 已承担部分代理职责，可后续补） |
| QStyleFactory | src/widgets/styles/qstylefactory.h | 建议实现轻量版（keys()/create("Fusion"/"Windows")，X 现有 XFusionStyle_installDefault 可并入） |
| QStylePainter | src/widgets/styles/qstylepainter.h | 建议实现（QPainter 便捷包装；X 的 XPainter+widget 参数可封装） |
| QStylePlugin | src/widgets/styles/qstyleplugin.h | 不建议实现（XGui 无插件体系需求；XImageIOPlugin 模式可参考） |
| QStyleHintReturn | src/widgets/styles/qstyleoption.h | 建议实现（styleHint 的 returnData 参数需要） |
| QStyleHintReturnMask | 同上 | 同上（SH_FocusFrame_Mask/SH_RubberBand_Mask 依赖） |
| QStyleHintReturnVariant | 同上 | 可暂缓（SH_* variant 查询少用） |
| QStyleOptionComplex | 同上 | 建议实现（复合控件子控件位掩码） |
| QStyleOptionFocusRect/Frame/TabWidgetFrame/TabBarBase/Header/HeaderV2/Button/Tab/ToolBar/ProgressBar/MenuItem/DockWidget/ViewItem/ToolBox/RubberBand/Slider/SpinBox/ToolButton/ComboBox/TitleBar/GroupBox/SizeGrip/GraphicsItem | 同上 | 建议按"常用优先"分批实现（Button/ProgressBar/Slider/SpinBox/ComboBox/ToolButton/ViewItem/Header 优先），或至少提供类型安全枚举 + 专用结构体 |
| QStyleSheetStyleCaches | src/widgets/styles/qstylesheetstyle_p.h | 不实现（私有缓存；X 可用简单规则缓存替代） |
| QPixmapStyle | src/widgets/styles/qpixmapstyle_p.h | 不实现（私有、内部像素图样式） |

另外说明：QWindowsStyle/QFusionStyle/QStyleSheetStyle 在 Qt 6.8.3 中无公共头文件（私有），XGui 已对应实现，不列入缺失。

---

## 5. 优先任务建议（按优先级）

1. **P0 修 XFusionStyle 继承链**：`m_base` 改为 XCommonStyle（Qt 6.8.3 QFusionStyle : QCommonStyle），同步重排 XWindowsStyle 引入的 StyleHint 槽位偏移，修正后跑全部裁剪构建回归（硬约束 2）。
2. **P0 修 XWindowsStyle_styleHint**：将 StyleHint 槽挂入虚表并实现 Qt QWindowsStyle 的 SH_EtchDisabledText/SH_ItemView_ShowDecorationSelected 等行为；删除"返回 PM_DefaultFrameWidth"的错误实现。
3. **P0 修 XStyle_installStyleSheet 泄漏**：替换 g_defaultStyle 前先释放旧默认样式（或把旧样式所有权转移给 m_source 并定义 m_source 拥有语义），补回归用例。
4. **P1 XStyle 虚表扩容**：新增 styleHint/subElementRect/subControlRect/hitTestComplexControl/standardPixmap/standardIcon/generatedIconPixmap/layoutSpacing/drawItemText/drawItemPixmap/itemTextRect/itemPixmapRect/standardPalette/polish(QApplication)/unpolish(QApplication)/polish(QPalette) 槽位与枚举（SubElement/SubControl/ContentsType/StyleHint/StandardPixmap），补齐静态工具（visualRect/alignedRect/sliderPositionFromValue 等）。
5. **P1 XPalette 对齐 qt_fusionPalette**：Light=#F7F7F7、Midlight=#BFBFBF、Highlight=#308CC6、Disabled Base=#EFEFEF、Disabled Shadow=#BABABA，新增 Accent 角色，修正注释与代码不一致（#308cc6 vs #3D8EC9）。
6. **P1 XCommonStyle 数值与覆盖补全**：pixelMetric 按 Qt 修正（TabBarTabOverlap=3、TabBarTabHSpace=24、TabBarTabVSpace=8/3/2、ToolBarItemSpacing=4、ToolBarHandleExtent=8、DockWidgetTitleBarButtonMargin=2、ButtonShift=2）；补 drawPrimitive/drawControl 缺失 case（ProgressChunk/ToolBarHandle/Separator/Branch/PanelItemViewItem/IndicatorItemViewItemCheck/FrameGroupBox/CE_ProgressBar/CE_Header/CE_ScrollBar*/CE_ToolBar/CE_ItemViewItem 等）；sizeFromContents 按 Qt 公式重写并扩展 CT。
7. **P1 XFusionStyle 主题化**：补 Fusion 的 drawComplexControl（Slider/ScrollBar/SpinBox/ComboBox/GroupBox/Dial）、standardPalette、pixelMetric、polish 系列；颜色常量改 #308CC6；圆角用真实圆角（XPAINTER_SHAPE_ON 分支）。
8. **P2 XStyleSheetStyle 属性与规则**：消费已解析的 margin/min/max/width/height/font 系列；补 border-style 与 !important 跨规则语义；引入简单渲染规则缓存；必要时补 sizeFromContents/pixelMetric 覆写。
9. **P2 XStyleOption 类型化**：至少实现 XStyleOptionButton/ProgressBar/Slider/SpinBox/ComboBox/ToolButton 专用结构体与 initFrom 等价物，替换 m_checkState/m_progressMin 复用 hack。
10. **P3 头文件 Doxygen 补全**：XStyleHints.h 全部 getter/setter/信号补齐逐参数 @param/@return（硬约束 4 合规收尾）。

---

## 6. 附注

- 数据来源：X 头文件/实现逐行阅读 + Qt 6.8.3 头文件与关键 .cpp（qcommonstyle.cpp 的 pixelMetric/sizeFromContents、qfusionstyle.cpp、qwindowsstyle.cpp、qplatformtheme.cpp 的 qt_fusionPalette、qsurfaceformat.cpp、qstylehints.cpp）对照；未修改任何 Src/Test 文件。
- 本报告与 XGui.md §10.99/§15.6 的历史记录一致：样式引擎此前被定位为"嵌入式近似边界"，本次审计按当前硬约束（完整复刻 Qt 行为）判定为违规项，实施优先级见第 5 节。
