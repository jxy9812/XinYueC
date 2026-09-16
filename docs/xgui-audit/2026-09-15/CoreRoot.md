# XGui CoreRoot 模块 Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计模块：`CoreRoot`（`Src/XGui/` 顶层，即模块根公共单元）
- 审计对象：`Src/XGui/XAlignment.h`、`Src/XGui/XGuiConfig.h`（只读审计，未改动 Src/、Test/ 任何源码，未 commit/push）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/corelib/global/qnamespace.h`
  （Qt::AlignmentFlag 144-165、Qt::Orientation 99-104、Qt::TextFlag 170-186）、
  `qtbase/src/corelib/global/qflags.h`（Q_DECLARE_FLAGS/QFlags）
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》
  （字符串 API 主次 769-795、公共头注释格式 840-905、强制内存分配规则 951-995）、
  `XGui.md`（10.35 对齐头文件 BOM、10.97 对齐掩码 0x001f 修正、14.61 配置开关修复、
  XMenu/XToolButton 开关依赖章节 12320-12360）
- 验证手段：`gcc -E -dM`（-DXGUI_ON=0 / -DXWIDGET_ON=0 / -DXLAYOUT_ON=0 三组预处理
  实测开关终值）、全 Src 门卫宏使用/定义交叉比对、文件首字节 BOM 检查（od）
- 结论摘要：`XAlignment.h` 与 Qt 6.8 `Qt::AlignmentFlag` 的 14 项枚举数值、掩码与
  快捷组合**完全一致（1:1）**，无缺失 API，BOM/Doxygen 合规；`XGuiConfig.h` 结构
  完整（131 个开关宏、依赖裁剪块与总开关联动齐全），但裁剪联动存在明显漏洞：
  `XGUI_ON=0` 时实测仍有 **36 个开关保持 1**（含 XPAINTER_ON 全族、XSTYLE_ON、
  XMENU_ON、XTOOLBUTTON_ON 与 13 个控件开关），且 XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/
  XCHARTS_ON 被**显式强制置 1**；`XPIXMAP_ON` 从未定义导致 XSplashScreen 的
  setPixmap/pixmap 永久裁剪。无 P0 级违规（8 条硬约束本模块均不涉及类/字符串/
  信号/生命周期内容），P1 级配置缺陷 3 项、P2 级 4 项。

---

## 一、模块概览

| X 单元 | 文件 | 对标 Qt 单元 | 类型 | 对齐度 |
|---|---|---|---|---|
| XAlignment | Src/XGui/XAlignment.h | Qt::AlignmentFlag / Qt::Alignment（qnamespace.h:144-167） | 位标志枚举 + uint32_t 组合类型 | 高（数值/掩码 1:1，约 98%） |
| XGuiConfig | Src/XGui/XGuiConfig.h | 无 Qt 对应（内部裁剪配置） | 配置头（131 个开关宏） | 结构完整但裁剪联动有漏洞（约 85%） |

- 模块开关：XAlignment 无独立开关（随 XGUI 公共头文件提供，头文件自述）；XGuiConfig.h
  集中定义 XGui 全部子开关（含 XImageCodec 子开关经末尾 include 引入）。
- 依赖与分层：XGuiConfig.h 仅依赖 `CXinYueConfig.h`（顶层总配置）与
  `Graphics/XImageCodec/XImageCodec_config.h`；XAlignment.h 仅依赖 `<stdint.h>`，
  不依赖布局/控件模块，可在 XWIDGET_ON/XLAYOUT_ON 关闭时独立使用（合规）。
- 测试覆盖：`xgui_regression_test.c` 多处引用 `XAlignment_*` 常量（XGui.md 10.97
  记录 `Right|Bottom|Absolute` 掩码读回归）；**无**针对 XGuiConfig 裁剪终值的
  预处理断言测试（建议补，见任务建议 7）。

---

## 二、逐单元对比

### 2.1 XAlignment ↔ Qt::AlignmentFlag（qnamespace.h:144-165）

#### 2.1.1 枚举数值对照表（14/14 全覆盖）

| Qt 枚举值（qnamespace.h） | 数值 | X 枚举值（XAlignment.h） | 数值 | 状态 |
|---|---|---|---|---|
| Qt::AlignLeft | 0x0001 | XAlignment_Left | 0x0001 | ✓ |
| Qt::AlignLeading（=AlignLeft） | 0x0001 | XAlignment_Leading | 0x0001 | ✓ |
| Qt::AlignRight | 0x0002 | XAlignment_Right | 0x0002 | ✓ |
| Qt::AlignTrailing（=AlignRight） | 0x0002 | XAlignment_Trailing | 0x0002 | ✓ |
| Qt::AlignHCenter | 0x0004 | XAlignment_HCenter | 0x0004 | ✓ |
| Qt::AlignJustify | 0x0008 | XAlignment_Justify | 0x0008 | ✓ |
| Qt::AlignAbsolute | 0x0010 | XAlignment_Absolute | 0x0010 | ✓ |
| Qt::AlignHorizontal_Mask（L|R|HC|J|A） | 0x001f | XAlignment_HorizontalMask | 0x001f | ✓ |
| Qt::AlignTop | 0x0020 | XAlignment_Top | 0x0020 | ✓ |
| Qt::AlignBottom | 0x0040 | XAlignment_Bottom | 0x0040 | ✓ |
| Qt::AlignVCenter | 0x0080 | XAlignment_VCenter | 0x0080 | ✓ |
| Qt::AlignBaseline | 0x0100 | XAlignment_Baseline | 0x0100 | ✓ |
| Qt::AlignVertical_Mask（T|B|VC|BL） | 0x01e0 | XAlignment_VerticalMask | 0x01e0 | ✓ |
| Qt::AlignCenter（VC|HC） | 0x0084 | XAlignment_Center | 0x0084 | ✓ |

判定：**枚举项、数值、水平/垂直掩码、Center 快捷组合全部一比一**，无缺失、无错值。
（XGui.md 10.97 已确认 HorizontalMask 含 AlignAbsolute=0x0010 与 Qt 一致。）

#### 2.1.2 类型与位运算

- Qt：`Q_DECLARE_FLAGS(Alignment, AlignmentFlag)` + `Q_DECLARE_OPERATORS_FOR_FLAGS`
  （qflags.h），`Qt::Alignment` 为带 `|`/`&`/`~` 运算符的强类型标志类。
- X：`typedef uint32_t XAlignments;`（XAlignment.h:43），C 语言下按位或/与/取反为
  内建运算符，语义与 QFlags 等价；缺“类型安全”（无法阻止不同枚举互或），属 C 语言
  的合理近似，可接受。

#### 2.1.3 API/功能差异清单

- G1（缺口）：无。14 项枚举与组合类型全部有对应，无 Qt API 缺失。
- D1（命名不一致，可接受）：Qt 用 `AlignXxx` 短名，X 统一 `XAlignment_Xxx` 前缀；
  `AlignHorizontal_Mask`/`AlignVertical_Mask` 在 X 侧为 `HorizontalMask`/`VerticalMask`
  （值一致）。属既定命名风格（X*_ 前缀），不构成冲突。
- D2（P2）：`XAlignment_Baseline=0x0100` 与 `Qt::TextFlag::TextSingleLine=0x0100`
  数值重叠——Qt 源码 qnamespace.h:158-161 有专门注释说明该重叠只影响
  QPainter::drawText 混用场景；X 头文件未注明。`XPainter.h:389-391` 的
  `XPainterTextFlags` 已按 Qt“AlignmentFlag/TextFlag 全集合”混用同一 uint32_t，
  建议在 XAlignment.h 补一行 @note 说明，防止后续误判。
- D3（P2）：类型混用——组合类型定义为 `uint32_t XAlignments`，但消费方有的用
  `int` 接收（如 XSplashScreen.m_alignment 为 int、多控件 setAlignment 用 int）；
  位值不受影响，建议统一为 XAlignments。
- D4：无 `AlignNone` 枚举（Qt 亦无；默认 0 语义一致，无需补）。

#### 2.1.4 约束核查（8 条）

| # | 约束 | 核查结果 |
|---|---|---|
| 1 | 字符串 XString*/_2 重载 | N/A（无字符串 API） |
| 2 | 继承一比一含中间基类 | N/A（非类，纯枚举头） |
| 3 | 样式/绘制不得精简 | N/A（无绘制代码） |
| 4 | 中文 Doxygen + UTF-8 BOM | ✓ 文件头 @file/@brief/@details/@note 齐全；枚举类型 @brief、14 项枚举逐项行尾注释；无函数故无 @param/@return 需求；首字节 `ef bb bf` BOM 存在（XGui.md 10.35 记录单 BOM 处理） |
| 5 | 信号 *_signal | N/A |
| 6 | 生命周期/memcpy/malloc | N/A（无对象） |
| 7 | 旧 API 不保留 | ✓ 无历史 API |
| 8 | 新代码 C99 | ✓ 仅 typedef enum + stdint.h，无 C11/C++ 语法 |

### 2.2 XGuiConfig.h（内部，结构完整性）

#### 2.2.1 结构概览

- 分组（按注释分区）：图像与窗口基础 9 个（XIMAGECODEC_ON…XACCESSIBLE_ON）；
  应用/样式/剪贴板/MIME 8 个；平台集成与平台资源 11 个 + 后备存储渲染模式参数
  （PARTIAL/DIRECT/FULL、缓冲数/尺寸/帧缓冲地址）；绘图 18 个（XPAINTER_ON + 17 个子开关）；
  控件/布局/输入法/窗口事件约 39 个；性能悬浮层 6 个；原生窗口与无障碍后端 5 个。
- 默认值策略：全部 `#ifndef` 可覆盖，支持编译选项 `-DXxx_ON=0` 精细裁剪（合规）。
- 依赖裁剪块：`!XWIDGET_ON || !XGUIAPPLICATION_ON || !XWINDOW_ON` 连带裁剪
  布局与控件开关（456-537）；`!XABSTRACTSLIDER_ON→XSLIDER_ON`、`!XABSTRACTBUTTON_ON→
  按钮族`、`!XPUSHBUTTON_ON→XCOMMANDLINKBUTTON_ON`、`!XMENU_ON→XPUSHBUTTON/XTOOLBUTTON`
  （553-563）、`!XFRAME_ON→XLABEL_ON`、`!XGUIAPPLICATION_ON→XAPPLICATION_ON`（564-571）。
- 总开关联动：`!XGUI_ON` 块（573-735）逐个 #undef 并置 0（含后备存储参数复位），
  末尾 include XImageCodec_config.h。
- 内部文件性质：无公共 API 声明，无 Doxygen @param/@return 要求；BOM 存在
  （首字节 `ef bb bf` ✓）。

#### 2.2.2 模块覆盖核查（开关是否存在）

| 模块 | 开关 | 状态 |
|---|---|---|
| Application | XGUIAPPLICATION_ON / XAPPLICATION_ON | ✓ |
| Widget | XWIDGET_ON + 各控件开关 | ✓（覆盖不全，见 V2） |
| Charts | XCHARTS_ON | ✓（但嵌套定义，见 V4） |
| Graphics | XIMAGECODEC_ON、XBACKINGSTORE_ON、XPAINTER_ON 等 | ✓（XPixmap/XMovie/XImageReader/XPicture/XPixmapCache 无独立开关，由 Graphics 模块报告处理；XPIXMAP_ON 缺失，见 V3） |
| Icon | 无 XICON_ON | 部分（XIcon.h/.c 以 XGUIAPPLICATION_ON/XPLATFORMINTEGRATION_ON 守卫，无独立裁剪粒度，P2 可接受） |
| Input | XCLIPBOARD_ON/XMIMEDATA_ON/XCURSOR_ON/XACCESSIBLE_ON/XINPUTMETHOD_ON | ✓ |
| Platform | XPLATFORMINTEGRATION_ON/XPLATFORMWINDOW_ON/XPLATFORMNATIVEWINDOW_ON/XPLATFORMINPUTCTX_ON/XPLATFORMBACKINGSTORE_ON/XPLATFORMBACKINGSTORE_SOFTWARE_ON/XPLATFORMNATIVEINTERFACE_ON/XPLATFORMFONTDATABASE_ON | ✓（XPLATFORMFONTDATABASE_ON 于 14.61 已补） |
| Style | XSTYLE_ON/XSTYLEHINTS_ON/XPALETTE_ON | ✓ |
| Window | XWINDOW_ON/XSCREEN_ON/XWINDOWEVENT_ON/XWINDOWSYSTEMINTERFACE_ON/XSURFACEFORMAT_ON | ✓ |
| XLayout | XLAYOUT_ON/XLAYOUT_STACKED_ON | ✓（联动缺口，见 V5） |
| 裁剪参数 | 渲染模式/缓冲数/缓冲尺寸/帧缓冲地址 | ✓（含越界收敛 148-175） |

#### 2.2.3 结构问题清单（按严重度）

- **V1（P1，总开关契约违背）**：`XGUI_ON=0` 裁剪不彻底。预处理实测（
  `gcc -E -dM -DXGUI_ON=0 -include Src/XGui/XGuiConfig.h`）仍有 **36 个开关保持 1**：
  `XPAINTER_ON` 及 17 个子开关（SHAPE/POLYGON/PENSTYLE/BRUSH/BRUSH_ORIGIN/BACKGROUND/
  PIXMAP/IMAGE_RECT/TILED_PIXMAP/PATH/TEXTLAYOUT/LAYOUT_DIRECTION/RENDERHINT/
  WORLD_MATRIX/VIEW_TRANSFORM/CLIP/CLIP_REGION）、`XSTYLE_ON`、`XMENU_ON`、
  `XTOOLBUTTON_ON`、`XPROGRESSBAR_ON`、`XGROUPBOX_ON`、`XABSTRACTSLIDER_ON`、
  `XSLIDER_ON`、`XDIAL_ON`、`XCOMBOBOX_ON`、`XTABBAR_ON`、`XTABWIDGET_ON`、
  `XABSTRACTSPINBOX_ON`、`XSPINBOX_ON`、`XLINEEDIT_ON`、`XLAYOUT_STACKED_ON`、
  `XTEXTDOCUMENT_ON`、`XTABLEWIDGET_ON`、`XCHARTS_ON`。
  其中 673-682 行把 XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/XCHARTS_ON **显式置 1**，
  与 573 行“XGui 总开关关闭时，所有 GUI 子模块统一裁剪”的声明直接矛盾；
  实测 `XGUI_ON=0` 后仍定义 110 个开关宏（36 个=1、74 个=0）。
  附带影响：`XPainter.c`/`XPixmap.c` 等源文件本身不以 XPAINTER_ON/XGUI_ON 为
  整文件门卫（XPainter.h/.c 全文无 XPAINTER_ON），`XSTYLE_ON=1` 使 XStyle.c
  （13 行门卫）继续编译——嵌入式 `-DXGUI_ON=0` 无法真正裁剪整个 GUI。
- **V2（P1，依赖裁剪不完整）**：`!XWIDGET_ON` 依赖块（456-537）遗漏 14 个控件
  开关：XPROGRESSBAR_ON、XGROUPBOX_ON、XABSTRACTSLIDER_ON、XSLIDER_ON、XDIAL_ON、
  XCOMBOBOX_ON、XTABBAR_ON、XTABWIDGET_ON、XABSTRACTSPINBOX_ON、XSPINBOX_ON、
  XLINEEDIT_ON、XMENU_ON、XTOOLBUTTON_ON、XERRORMESSAGE_ON（XCHARTS_ON/
  XTEXTDOCUMENT_ON/XTABLEWIDGET_ON 亦未联动）。实测 `XWIDGET_ON=0` 时这些宏仍为 1。
  多数头文件用双门卫（如 XStackedWidget.h:30/34 `XLAYOUT_ON && XLAYOUT_STACKED_ON`、
  XMenu.h `XWIDGET_ON && XMENU_ON`）所以不产生编译错误，但开关语义自相矛盾，
  XGui.md 12329-12332 记录的“XMENU_ON 关闭连带裁剪菜单能力”约定在 XWIDGET_ON=0
  路径失效。
- **V3（P1，功能永久裁剪）**：`XPIXMAP_ON` 全仓库无定义（grep Src 无 `#define XPIXMAP_ON`）。
  `XSplashScreen.h:31/58-66` 用 `#if XPIXMAP_ON` 守卫 setPixmap/pixmap（头文件 16 行
  自述依赖 XWIDGET_ON、XPIXMAP_ON），导致 XSplashScreen 位图能力**永久裁剪**：
  XSplashScreen.c 以 `XWIDGET_ON && XSPLASHSCREEN_ON`（21 行）整体编译，而头文件
  的位图 API 恒不声明——公开 API 与 Qt QSplashScreen 的 setPixmap/pixmap 长期缺失。
- **V4（P2，嵌套守卫隐患）**：316-333 行把 XWIZARD_ON/XERRORMESSAGE_ON/
  XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/XCHARTS_ON 五个开关的定义嵌套在
  `#ifndef XPLATFORMFONTDATABASE_ON` 内。用户预置 `XPLATFORMFONTDATABASE_ON=0` 时，
  这五个开关不再获得默认 1（静默变为 0）；其中 XTABLEWIDGET_ON/XCHARTS_ON 与字体
  数据库无直接依赖，嵌套无依据（14.61 补 XPLATFORMFONTDATABASE_ON 时疑似误扩范围）。
- **V5（P2，开关语义矛盾）**：`XLAYOUT_ON=0` 时 `XLAYOUT_STACKED_ON` 仍为 1
  （405 行无条件定义；XLayout_config.h 的默认值在 `#if XLAYOUT_ON` 内，不会覆盖）。
  消费方双门卫（XStackedWidget.h:30/34）不编译错，但“布局系统整体裁剪”的语义
  不成立。
- 无 TODO/FIXME/未实现/占位标记（grep 确认）。

#### 2.2.4 约束核查（8 条）

| # | 约束 | 核查结果 |
|---|---|---|
| 1 | 字符串 XString*/_2 重载 | N/A（纯宏配置头） |
| 2 | 继承一比一 | N/A |
| 3 | 样式/绘制不得精简 | N/A（无绘制代码；开关未裁剪导致的“多编译”是配置问题，见 V1） |
| 4 | 中文 Doxygen + BOM | ✓ 文件头 @file/@brief/@details 齐全、分组注释清晰；内部文件无函数注释要求；BOM 存在 |
| 5 | 信号 *_signal | N/A |
| 6 | 生命周期/memcpy/malloc | N/A（无对象/分配代码） |
| 7 | 旧 API 不保留 | ✓ |
| 8 | 新代码 C99 | ✓ 纯预处理指令与宏，无 C11/C++ 语法 |

---

## 三、模块内硬约束逐条核查（汇总）

| # | 约束 | 核查结果 |
|---|---|---|
| 1 | 拥有型字符串一律 XString*；API 主版本 XString，UTF-8 用 `_2` 重载 | ✓ 两文件均无字符串 API/成员，N/A |
| 2 | 继承一比一含中间基类 | ✓ 两文件均非类，N/A |
| 3 | 样式/绘制不得精简近似 | ✓ 两文件均无绘制代码，N/A |
| 4 | 公共头中文 Doxygen（@brief/@param/@return）+ UTF-8 BOM | ✓ XAlignment.h（公共）逐项注释 + BOM；XGuiConfig.h（内部）分组注释 + BOM |
| 5 | 信号：空参 args=NULL；Qt 6.8 每个信号有对应 *_signal | ✓ N/A（无信号） |
| 6 | init/deinit_base 成对；copy/move 安全；禁 memcpy/malloc/free/strdup | ✓ N/A（无对象） |
| 7 | 旧 API 不保留 | ✓ 无新旧并存 |
| 8 | 新代码 C99 | ✓ 两文件均为 C99 语法 |

**本模块无 P0 违规**；P1 问题 3 项（V1/V2/V3）+ P1/P2 边界项 V4/V5（P2），全部集中在
XGuiConfig.h 的裁剪联动，XAlignment.h 干净。

---

## 四、缺失 Qt 类清单（CoreRoot 对应范围）

| Qt 单元 | Qt 头文件（相对 qtbase） | X 现状 | 建议 |
|---|---|---|---|
| QFlag | `src/corelib/global/qflags.h` | 无（C 无模板机制） | **不实现**：维持 `XAlignments = uint32_t` 位组合模式，运算符由 C 内建支持 |
| QFlags\<Qt::AlignmentFlag\> | `src/corelib/global/qflags.h` | 无模板等价物；XAlignment.h:43 typedef uint32_t XAlignments | **不实现**（同上），如需类型安全可后续提供 XAlignments_has/intersects 辅助函数 |
| Qt::Orientation | `src/corelib/global/qnamespace.h:99-104` | XGui 无统一枚举；三处重复同值枚举：XGridLayout.h:82-86 `XOrientation`（Horizontal=1/Vertical=2）、XProgressBar.h:42-46 `XProgressBarOrientation`、XAbstractSlider.h:50-54 `XAbstractSliderOrientation` | **建议实现**：CoreRoot 新增统一 `XOrientation`（含 XOrientations 位组合），按“旧 API 不保留”原则将三处迁移/别名收敛 |
| Qt::Orientations | `src/corelib/global/qnamespace.h:104` | 无统一位组合类型；XLayoutItem.h:82-85 有“对标 Qt::Orientations”的伸展方向位标志 | 随 XOrientation 一并提供，或维持模块内局部标志 |
| Qt::TextFlag | `src/corelib/global/qnamespace.h:170-186` | XPainter.h:389-391 `XPainterTextFlags(uint32_t)` 已按“AlignmentFlag/TextFlag 全集合”覆盖（Graphics 模块） | **不实现**：CoreRoot 不重复定义；仅建议 XAlignment.h 补注 Baseline=0x0100 与 TextSingleLine 重叠 |

已确认**不缺失**：Qt::AlignmentFlag 全部 14 项（XAlignment.h 1:1）；Qt::GlobalColor、
WindowType、FocusPolicy、KeyboardModifier、MouseButton、LayoutDirection 等其它
qnamespace 全局枚举由 Graphics/Window/Widget/Application 模块各自覆盖，不属于
CoreRoot 范围。

---

## 五、优先任务建议（按优先级）

1. **P1 修 XGUI_ON=0 裁剪覆盖**：573-735 块补齐 36 个遗漏开关的 #undef/置 0（含
   XPAINTER_ON 全族、XSTYLE_ON、XMENU_ON、XTOOLBUTTON_ON、13 个控件开关），并修正
   673-682 行把 XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/XCHARTS_ON 置 1 的错误。
2. **P1 补 XPIXMAP_ON 开关**：XGuiConfig.h 定义（默认 1，裁剪分支 0），恢复
   XSplashScreen_setPixmap/pixmap（XSplashScreen.h:31/58 门卫即刻生效）。
3. **P1 补 XWIDGET_ON=0 依赖裁剪**：把 V2 列出的 14 个控件开关并入 456-537 依赖块，
   与既有 XLAYOUT_ON/XFRAME_ON 等联动保持一致。
4. **P2 拆嵌套守卫**：把 XWIZARD_ON/XERRORMESSAGE_ON/XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/
   XCHARTS_ON 移出 `#ifndef XPLATFORMFONTDATABASE_ON`（316-333）；若确实依赖字体
   数据库，改为显式依赖裁剪规则而非嵌套定义。
5. **P2 修 XLAYOUT_STACKED_ON 联动**：`!XLAYOUT_ON` 时连带置 0（与 XLayout_config.h
   的 `#if XLAYOUT_ON` 语义对齐）。
6. **P2 XAlignment.h 补注与类型统一**：注明 Baseline=0x0100 与 TextSingleLine 重叠；
   将消费方 int 对齐参数统一为 XAlignments（如 XSplashScreen.m_alignment）。
7. **P2 增加配置一致性回归**：Test 下新增 XGuiConfig 预处理断言（XGUI_ON=0 /
   XWIDGET_ON=0 / XLAYOUT_ON=0 三组，断言相关 X*_ON 全部为 0），用
   `gcc -E -dM` 或 CMake 编译期检查防回退。
8. **P2 CoreRoot 统一 XOrientation**：新增与 Qt::Orientation 数值一致（Horizontal=1/
   Vertical=2）的共享枚举，收敛 XGridLayout/XProgressBar/XAbstractSlider 三处重复定义，
   消除 Qt::Orientation 缺口。

---

*本报告为只读审计产物；除本文件外未修改 Src/、Test/ 任何内容，未执行 git commit/push。
验证命令（可复现）：`gcc -E -dM -I <stub> -I Src/XGui -DXGUI_ON=0 -include Src/XGui/XGuiConfig.h`，
其中 <stub> 为空的 CXinYueConfig.h 替身（XGuiConfig.h 裁剪逻辑不依赖其内容）。*
