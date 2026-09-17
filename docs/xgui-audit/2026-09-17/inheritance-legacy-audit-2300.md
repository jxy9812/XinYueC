# XGui 审计报告（audit-2300）

- 审计日期：2026-09-17
- 审计对象：`/home/xinyue/Code/XinYueC/Src/XGui`（只读，未修改任何源码）
- Qt 参照源码：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets`、`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui`、`/home/xinyue/Qt/6.8.3/Src/qtbase/src/core`（Qt 6.8.3）
- 本文件为唯一交付物，写入路径：`.tmpdbg/audit-2300.md`

---

## 一、继承链一致性审计

### 1.1 提取方法与判定规则

XGui 侧类定义使用两种宏形式（等价）：

1. `XCLASS_DEFINE_EXTEND_END(类名, 基类)` —— 显式给出基类（如 `XFusionStyle.h:15`）；
2. `XCLASS_DEFINE_BEGING(类名) … XCLASS_DEFINE_END(类名)` —— 基类由首个虚槽的
   `XCLASS_DEFINE_ENUM(类名, 槽) = XCLASS_VTABLE_GET_SIZE(基类)` 隐式给出
   （如 `XAbstractButton.h:56` 指向 `XWidget`）。

两种形式均已提取。XGui 类总数（`XCLASS_DEFINE_BEGING` 计）：**144 个**。

判定规则（按任务约定）：项目以 `XObject`/`XClass` 为根，Qt 的 `QObject`/`QWidget`
层级在 XGui 中映射为 `XObject`/`XWidget`；对比时按"Qt 基类链去掉
QObject/QWidget 层级后的形状"判定。已知正确锚点
`XFusionStyle→XCommonStyle→XStyle→XObject` 复核无误。

XGui 继承主干（审计建立，供对照）：

```
XClass（根，Src/XClass/XClass.h:14）
└─ XObject（XObject.h:19，VTABLE_GET_SIZE(XClass)）
   ├─ XWidget（Widget/XWidget.h:439，VTABLE_GET_SIZE(XObject)）
   │  ├─ XFrame（Widget/XFrame.h:101）
   │  │  ├─ XAbstractScrollArea（XAbstractScrollArea.h:51）、XLabel、XSplitter、
   │  │  │  XStackedWidget、XToolBox、XLcdNumber
   │  │  │  └─ XAbstractItemView（XAbstractItemView.h:53）
   │  │  │     ├─ XTableView、XTreeView、XListView（→ XListWidget、XTableWidget、XTreeWidget）
   │  │  ├─ XDialog（XDialog.h:21）→ XMessageBox/XFileDialog/XWizard/…
   │  ├─ XAbstractButton（XAbstractButton.h:56）→ XPushButton/XCheckBox/XRadioButton/XToolButton
   │  ├─ XAbstractSlider（XAbstractSlider.h:95）→ XSlider/XScrollBar/XDial
   │  ├─ XAbstractSpinBox（XAbstractSpinBox.h:86）→ XSpinBox/XDateTimeEdit
   │  └─ XComboBox、XLineEdit、XTabBar、XMenu、XMenuBar、XToolBar、XStatusBar、
   │     XMainWindow、XDockWidget、XCalendarWidget、XHeaderView、…
   └─ XStyle（Style/XStyle.h:24）→ XCommonStyle → XFusionStyle / XWindowsStyle
                                                       └→ XStyleSheetStyle
```

### 1.2 对照表：控件类（任务清单全覆盖）

Qt 链均取自 Qt 6.8.3 实际头文件的 `class X : public Y` 声明（`class` 声明行，
不含 Private 前置类）。

| # | 类名 | XGui 链（至根） | Qt 6.8.3 链（至根） | 判定 |
|---|------|----------------|---------------------|------|
| 1 | XPushButton | XAbstractButton → XWidget → XObject → XClass | QAbstractButton → QWidget → QObject（→QPaintDevice） | 一致 |
| 2 | XCheckBox | XAbstractButton → … | QAbstractButton → … | 一致 |
| 3 | XRadioButton | XAbstractButton → …（XRadioButton.h:50） | QAbstractButton（qradiobutton.h:18） | 一致 |
| 4 | XToolButton | XAbstractButton → …（XToolButton.h:86） | QAbstractButton | 一致 |
| 5 | XCommandLinkButton | XPushButton → XAbstractButton → …（XCommandLinkButton.h:49） | QPushButton → QAbstractButton | 一致 |
| 6 | XComboBox | XWidget → XObject（XComboBox.h:68） | QWidget → QObject | 一致 |
| 7 | XLineEdit | XWidget → XObject（XLineEdit.h:125） | QWidget → QObject | 一致 |
| 8 | XTextEdit | XAbstractScrollArea → XFrame → XWidget（XTextEdit.h:25） | QAbstractScrollArea → QFrame → QWidget | 一致 |
| 9 | XPlainTextEdit | XAbstractScrollArea → XFrame → …（XPlainTextEdit.h:51） | QAbstractScrollArea → QFrame → … | 一致 |
| 10 | XLabel | XFrame → XWidget（XLabel.h:70） | QFrame → QWidget | 一致 |
| 11 | XTabWidget | XWidget（XTabWidget.h:27） | QWidget | 一致 |
| 12 | XTabBar | XWidget（XTabBar.h:28） | QWidget | 一致 |
| 13 | XMenu | XWidget（XMenu.h:51） | QWidget | 一致 |
| 14 | XMenuBar | XWidget（XMenuBar.h:36） | QWidget | 一致 |
| 15 | XToolBar | XWidget（XToolBar.h:55） | QWidget | 一致 |
| 16 | XStatusBar | XWidget（XStatusBar.h:37） | QWidget | 一致 |
| 17 | XDialog | XWidget（XDialog.h:21） | QWidget | 一致 |
| 18 | XMessageBox | XDialog → XWidget（XMessageBox.h:84） | QDialog → QWidget | 一致 |
| 19 | XFileDialog | XDialog → …（XFileDialog.h:89） | QDialog | 一致 |
| 20 | XMainWindow | XWidget（XMainWindow.h:62） | QWidget | 一致 |
| 21 | XDockWidget | XWidget（XDockWidget.h:44） | QWidget | 一致 |
| 22 | XScrollArea | XAbstractScrollArea → XFrame → …（XScrollArea.h:37） | QAbstractScrollArea → QFrame → … | 一致 |
| 23 | XAbstractScrollArea | XFrame → XWidget（VTABLE_GET_SIZE(XFrame)，XAbstractScrollArea.h:51） | QFrame → QWidget | 一致 |
| 24 | XTableView | XAbstractItemView → XAbstractScrollArea → XFrame（XTableView.h:15） | QAbstractItemView → QAbstractScrollArea → QFrame | 一致 |
| 25 | XTreeView | XAbstractItemView → …（XTreeView.h:25） | QAbstractItemView → … | 一致 |
| 26 | XListView | XAbstractItemView → …（XListView.h:27） | QAbstractItemView → … | 一致 |
| 27 | XHeaderView | **XWidget**（XHeaderView.h:25） | **QAbstractItemView → QAbstractScrollArea → QFrame**（qheaderview.h:17） | **偏差**：XGui 将 QHeaderView 简化为直接继承 XWidget，丢失 item-view 中间层（数据模型联动、section 复用 QAbstractItemView 语义）。且该头文件守卫为 `#if XWIDGET_ON && XTABLEWIDGET_ON`，无专属 `XHEADERVIEW_ON` 开关 |
| 28 | XAbstractItemView | XAbstractScrollArea → …（XAbstractItemView.h:53） | QAbstractScrollArea → … | 一致 |
| 29 | XWizard | XDialog → …（XWizard.h:231） | QDialog | 一致 |
| 30 | XWizardPage | XWidget（VTABLE_GET_SIZE(XWidget)，XWizard.h:88） | QWidget（qwizard.h:176） | 一致 |
| 31 | XCalendarWidget | XWidget（XCalendarWidget.h:55） | QWidget | 一致 |
| 32 | XDateTimeEdit | XAbstractSpinBox → XWidget（XDateTimeEdit.h:50） | QAbstractSpinBox → QWidget | 一致 |
| 33 | XFontComboBox | XComboBox → XWidget（XFontComboBox.h:37） | QComboBox → QWidget | 一致 |
| 34 | XProgressBar | XWidget（XProgressBar.h:59） | QWidget | 一致 |
| 35 | XSlider | XAbstractSlider → XWidget（XSlider.h:86） | QAbstractSlider → QWidget | 一致 |
| 36 | XSpinBox | XAbstractSpinBox（VTABLE_GET_SIZE(XAbstractSpinBox)，XSpinBox.h:36） | QAbstractSpinBox | 一致 |
| 37 | XSplitter | XFrame（XSplitter.h:38） | QFrame | 一致 |
| 38 | XDial | XAbstractSlider（XDial.h:28） | QAbstractSlider | 一致 |
| 39 | XScrollBar | XAbstractSlider（XScrollBar.h:38） | QAbstractSlider | 一致 |
| 40 | XGroupBox | XWidget（XGroupBox.h:39） | QWidget | 一致 |
| 41 | XStackedWidget | XFrame（XStackedWidget.h:37） | QFrame | 一致 |
| 42 | XToolBox | XFrame（XToolBox.h:36） | QFrame | 一致 |
| 43 | XLcdNumber | XFrame（XLcdNumber.h:67） | QFrame（qlcdnumber.h:15，注意 Qt 类名为 QLCDNumber） | 一致 |
| 44 | XMdiArea | XAbstractScrollArea（XMdiArea.h:230） | QAbstractScrollArea | 一致 |
| 45 | XMdiSubWindow | XWidget（XMdiArea.h:81） | QWidget | 一致 |
| 46 | XDialogButtonBox | XWidget（XDialogButtonBox.h:97） | QWidget | 一致 |
| 47 | XKeySequenceEdit | XWidget（XKeySequenceEdit.h:54） | QWidget | 一致 |
| 48 | XFocusFrame | XWidget（XFocusFrame.h:25） | QWidget | 一致 |
| 49 | XRubberBand | XWidget（XRubberBand.h:33） | QWidget | 一致 |
| 50 | XSizeGrip | XWidget（XSizeGrip.h:24） | QWidget（QSizeGrip） | 一致 |
| 51 | XSplashScreen | XWidget（XSplashScreen.h:38） | QWidget | 一致 |
| 52 | XTextBrowser | XTextEdit → XAbstractScrollArea → …（XTextBrowser.h:27） | QTextEdit → QAbstractScrollArea → … | 一致 |
| 53 | XListWidget / XTableWidget / XTreeWidget | XListView / XTableView / XTreeView | QListView / QTableView / QTreeView | 一致 |
| 54 | XColorDialog / XInputDialog / XProgressDialog / XErrorMessage | XDialog | QDialog | 一致 |
| 55 | XPerformanceOverlay | XLabel（XPerformanceOverlay.h:42） | 无 Qt 对应物（XGui 自有扩展，对标对象不存在） | 不适用（扩展类，不计偏差） |

### 1.3 对照表：Style 系

| # | 类名 | XGui 链 | Qt 6.8.3 链 | 判定 |
|---|------|---------|-------------|------|
| 56 | XFusionStyle | XCommonStyle → XStyle → XObject（XFusionStyle.h:15） | QCommonStyle → QStyle → QObject（qfusionstyle_p.h） | 一致（任务锚点，复核通过） |
| 57 | XCommonStyle | XStyle → XObject（XCommonStyle.h:15） | QStyle → QObject | 一致 |
| 58 | XStyle | XObject（VTABLE_GET_SIZE(XObject)，XStyle.h:24） | QObject | 一致 |
| 59 | XWindowsStyle | XCommonStyle（XWindowsStyle.h:15） | QCommonStyle（widgets/styles/qwindowsstyle_p.h:28；Qt 6 中 QWindowsStyle 位于 styles 私有头） | 一致 |
| 60 | XStyleSheetStyle | XWindowsStyle → XCommonStyle（XStyleSheetStyle.h:16） | QWindowsStyle → QCommonStyle（qstylesheetstyle_p.h） | 一致 |
| 61 | XStyleHints | XObject（XStyleHints.h:34） | QObject（qstylehints.h:16） | 一致 |

### 1.4 对照表：非 QWidget 系（Application/Graphics/Window/Icon/布局，摘要）

| # | 类名 | XGui 链 | Qt 6.8.3 链 | 判定 |
|---|------|---------|-------------|------|
| 62 | XWidget | XObject → XClass（XWidget.h:439） | QObject + QPaintDevice（双继承） | 适配性一致：C 无多继承，取 QObject 分支为主干，QPaintDevice 分支由 `XClass` 根承担 |
| 63 | XBoxLayout / XGridLayout / XStackedLayout | XLayout → XLayoutItem → XClass | QLayout = QObject + QLayoutItem（双继承）；QStackedLayout : QLayout | 适配性一致：单继承取 QLayoutItem 分支，丢弃 QObject 分支（XLayout 不参与信号，可接受）；逐类形状与 Qt 对应 |
| 64 | XLayoutItem | XClass（XLayoutItem.h:109） | QLayoutItem（无基类普通类） | 一致（普通类 → XClass 根） |
| 65 | XApplication → XGuiApplication | XGuiApplication → XCoreApplication → XObject（XApplication.h:59 / XGuiApplication.h:73） | QApplication → QGuiApplication → QCoreApplication → QObject | 一致 |
| 66 | XWindow | XObject（XWindow.h:111） | QObject + QSurface（双继承） | 适配性一致（丢弃 QSurface 分支） |
| 67 | XScreen | XObject（XScreen.h:36） | QObject（qscreen.h:31） | 一致 |
| 68 | XClipboard | XObject（XClipboard.h:39） | QObject（qclipboard.h:19） | 一致 |
| 69 | XCursor | XObject（XCursor.h:61） | **QCursor 无基类（普通类，qcursor.h:23）** | 轻微偏差：普通类被映射为 XObject 而非 XClass，与 #64 的"普通类→XClass"约定不一致（见 2.5 待复核） |
| 70 | XBackingStore | XObject（XBackingStore.h:62） | **QBackingStore 无基类（普通类，qbackingstore.h:23）** | 轻微偏差：同上 |
| 71 | XOffscreenSurface | XObject（XOffscreenSurface.h:43） | QObject + QSurface | 适配性一致（丢 QSurface） |
| 72 | XPixmap / XImage / XPicture / XBitmap | XClass（XPixmap.h:25 / XImage.h:34 / XPicture.h:79 / XBitmap.h:31） | QPixmap / QImage / QPicture : QPaintDevice；QBitmap : QPixmap | 一致（QPaintDevice 层由 XClass 根承担；XBitmap : XPixmap 与 QBitmap : QPixmap 对齐） |
| 73 | XImageWriter / XImageReader | XClass（XImageWriter.h:25 / XImageReader.h:27） | QImageWriter / QImageReader 无基类（普通类） | 一致（普通类 → XClass） |
| 74 | XImageIOPlugin | XObject（VTABLE_GET_SIZE(XObject)，XImageIOPlugin.h:17） | QObject | 一致 |
| 75 | XImageIOHandler | XClass（XImageIOHandler.h:98） | QImageIOHandler 无基类（普通类） | 一致 |
| 76 | XIcon / XIconEngine | XClass（XIcon.h:23 / XIconEngine.h:24） | QIcon / QIconEngine 无基类（普通类） | 一致 |
| 77 | XMovie | XObject（XMovie.h:35） | QObject（qmovie.h:27） | 一致 |
| 78 | XTextDocument | XObject（XTextDocument.h:85） | QObject（qtextdocument.h:55） | 一致 |
| 79 | XCompleter | XObject（XCompleter.h:114） | QObject | 一致 |
| 80 | XActionGroup / XButtonGroup | XObject（XActionGroup.h:38 / XButtonGroup.h:38） | QObject | 一致 |
| 81 | XAbstractItemModel / XItemSelectionModel | XObject（XAbstractItemModel.h:28 / XItemSelectionModel.h:26） | QObject | 一致 |
| 82 | XShortcut / XGraphicsEffect | XObject（XShortcut.h:69 / XGraphicsEffect.h:35） | QShortcut : QObject（gui/kernel/qshortcut.h:18）/ QGraphicsEffect : QObject | 一致 |
| 83 | XChartView / XChart（Charts，附注） | XWidget / XObject（XChartView.h:21 / XChart.h:129） | QChartView : QGraphicsView；QChart : QGraphicsWidget（QtCharts 模块） | 偏差（简化映射）：XGui 未实现 GraphicsView 场景体系，Charts 以 XWidget/XObject 承载；属模块级映射决策，见 2.5 待复核 |

### 1.5 审计结论（继承链）

- 任务清单 38 类 + 扩展样本共计 **83 项**，其中 **硬偏差仅 1 项**：
  **XHeaderView**（XGui 直接继承 XWidget，Qt 的 QHeaderView 继承
  QAbstractItemView → QAbstractScrollArea → QFrame）。
- 5 项为 **C 单继承下的适配性一致**（QWidget 的 QPaintDevice 分支、QLayout 的
  QObject 分支、QWindow/QOffscreenSurface 的 QSurface 分支、普通类 → XClass），
  形状判定按"去 QObject/QWidget 前缀"规则视为一致，但应在设计文档中固化。
- 2 项轻微偏差（XCursor、XBackingStore 把 Qt 普通类映射为 XObject，与
  QImageWriter/XIcon 等映射为 XClass 的约定不统一）。
- Charts 模块整体为简化映射（无 QGraphicsView/QGraphicsWidget 层）。

---

## 二、旧 API 清理候选清单

### 2.1 扫描方法

- 工具：Python 脚本做预处理分支深度追踪（正确处理块注释中的 `#if/#endif`）、
  函数体大括号配对；全部候选再用 grep 全仓人工复核。
- 调用方统计：`grep` 全仓 `*.c`/`*.h`，范围含 `Src/`、`Test/`、`Drive/`、
  `Tools/`、`tools/` 与根目录 `*.c`（main.c、xgui_*.c、ftp_e2e_test.c 等）。
- 原则：只列证据确凿项；疑似项归入"待人工复核"。

### 2.2 守卫外声明

**块级守卫外声明（特性开关 `#endif` 之后仍有声明）：0 处。**
对 `Src/XGui` 全部头文件做了预处理深度追踪，所有公共 API 均包在
`#if X*_ON` 特性守卫内（如 XLabel.h:50 `#if XWIDGET_ON && XFRAME_ON && XLABEL_ON`）。

**整文件无特性守卫的头文件：20 个**（XGuiConfig 已有或应有对应开关但头文件未包裹）：

```
Src/XGui/Graphics/XColorSpace.h        Src/XGui/Graphics/XImageIOPlugin.h
Src/XGui/Graphics/XImageBuiltinPlugin.h Src/XGui/Graphics/XImagePluginRegistry.h
Src/XGui/Graphics/XImageFormat.h       Src/XGui/Graphics/XImageReader.h
Src/XGui/Graphics/XImageIOHandler.h    Src/XGui/Graphics/XImageWriter.h
Src/XGui/Graphics/XMovie.h             Src/XGui/Graphics/XPixmapCache.h
Src/XGui/Icon/XIcon.h                  Src/XGui/Icon/XIconEngine.h
Src/XGui/Icon/XIconEnginePlugin.h      Src/XGui/Icon/XIconScaledPixmapCache.h
Src/XGui/Icon/XIconStyleHelper.h       Src/XGui/Icon/XIconThemeEngine.h
Src/XGui/Icon/XIconThemeInternal.h     Src/XGui/Icon/XSvgIconEngine.h
Src/XGui/Icon/XSvgIconEnginePlugin.h   Src/XGui/XAlignment.h（无函数声明，不计）
```

说明：XGuiConfig.h 为其中多数模块定义了开关（如 `XIMAGECODEC_ON`、
`XIMAGEIOPLUGIN_ON`），但对应头文件未使用；XMovie/XIcon 等甚至没有专属开关。
效果是这些模块无法被 `XGUI_ON=0`/子开关裁剪声明。因涉及"新增开关"属功能
变更，列入待人工复核（2.5-1），不直接列为删除候选。

### 2.3 重复/被别名取代的旧签名

**同头文件重复声明：0 处（真重复）。** 扫描命中的 XWindow.h `screen/setScreen/
cursor/setCursor/unsetCursor`（XWindow.h:1009/1022、1019/1024、1072/1091、
1080/1093、1086/1095）经核实为 `#if XSCREEN_ON / #else` 与
`#if XCURSOR_ON / #else` 的降级回退声明（注释明确"开关关闭时退化为空实现"），
属设计内双声明，非重复旧签名。XGpuRenderDriver.h 的命中为结构体函数指针成员，误报。

**宏别名（`#define X*_create(p,f) X*_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,p,f)`）**：
全库统一约定、配对使用，非旧 API，保留。

**核心发现：32 个"有声明、无实现、无调用"的死声明。**
以下函数在头文件中带完整 doxygen 文档（语气为"已实现"），但全仓
（Src/Test/Drive/Tools/根目录，含 .c 与 .h）中除声明所在头文件外 **出现次数为 0
（调用方数量 = 0，定义 = 无）**。任何用户代码调用都会产生链接错误。

| 文件:行 | 符号名 | 建议 | 调用方数量 |
|---------|--------|------|-----------|
| Src/XGui/Widget/XWidget.h:854 | `XWidget_saveGeometry` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:870 | `XWidget_restoreGeometry` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1193 | `XWidget_windowIcon` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1195 | `XWidget_setWindowIcon` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1228 | `XWidget_winId` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1240 | `XWidget_createWinId` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1253 | `XWidget_internalWinId` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1264 | `XWidget_effectiveWinId` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1277 | `XWidget_screen` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1291 | `XWidget_setScreen` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1564 | `XWidget_style` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1577 | `XWidget_setStyle` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1590 | `XWidget_ensurePolished` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1604 | `XWidget_actions` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1616 | `XWidget_addAction` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1630 | `XWidget_addAction_2` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1639 | `XWidget_addActions` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1656 | `XWidget_insertAction` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1667 | `XWidget_insertActions` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1678 | `XWidget_removeAction` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1703 | `XWidget_inputMethodQuery` | 补实现或删声明（位于 `#if XINPUTMETHOD_ON` 内） | 0 |
| Src/XGui/Widget/XWidget.h:1718 | `XWidget_locale` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1730 | `XWidget_setLocale` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XWidget.h:1740 | `XWidget_unsetLocale` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XComboBox.h:370 | `XComboBox_setItemDelegate` | 补实现或删声明 | 0 |
| Src/XGui/Widget/XComboBox.h:378 | `XComboBox_itemDelegate` | 补实现或删声明 | 0 |
| Src/XGui/Graphics/XImage.h:604 | `XImage_setPixelFast` | 补实现（内部已有 `XImageData_markDirty`，包装成本低）或删声明 | 0 |
| Src/XGui/Graphics/XImage.h:607 | `XImage_markDirty` | 同上 | 0 |
| Src/XGui/Widget/XLineEdit.h:725 | `XLineEdit_setCompleter` | 补实现（XLineEdit.c 已有 `m_completer` 与内部同步逻辑 `xlineedit_syncCompleter`，公开存取器缺失）或删声明 | 0 |
| Src/XGui/Widget/XLineEdit.h:732 | `XLineEdit_completer` | 同上 | 0 |
| Src/XGui/Widget/XLineEdit.h:747 | `XLineEdit_inputMethodQuery` | 文档化退化实现（恒返回 0）——但**无定义体**；补一个 3 行定义或删声明 | 0 |
| Src/XGui/Widget/XMenu.h:356 | `XMenu_menuInAction` | 补实现（一行，等价 `XAction_menu`）或删声明 | 0 |

补充说明：

- 这批声明的 doxygen 注释详尽描述了实现行为（如 XWidget.h:1228 winId 的
  "惰性创建桥接窗口"细节），但 `XWidget.c` 中连函数名都不存在，判断为
  "头文件先行、实现未落地"的预留 API。**不应以"删"为唯一建议**：其中
  action/locale/style/winId 族是 Qt 高频 API，建议优先补实现；若短期不实现，
  应删除声明，避免"文档承诺≠实际可链接"的 API 面膨胀。
- 抽查排除项：`XLineEditValidatorState`（XLineEdit.h:105）为枚举类型，被
  `XSpinBox.c:286-301` 使用，非死代码；`XWidget_windowIconText`（对标 Qt 已废弃
  API）有实现和调用，不在本清单。

### 2.4 空实现函数（.c 中函数体只有 return 且无注释说明）

以严格模式扫描 `Src/XGui/**/*.c`（函数体仅含 `(void)x;` 强转与/或
`return;`/`return <字面量>;`，体内无注释）：**命中 7 处**，逐一复核后
**全部为特性开关关闭时的 `#else` 回退桩或有注释说明的占位桩**，不构成清理候选：

| 文件:行 | 符号名 | 函数体 | 复核结论 | 建议 |
|---------|--------|--------|----------|------|
| Src/XGui/Application/XGuiApplication.c:941 | `XGuiApplication_styleHints` | `return NULL;` | `#else /* !XSTYLEHINTS_ON */` 分支的降级实现 | 保留 |
| Src/XGui/Application/XGuiApplication.c:964 | `XGuiApplication_clipboard` | `return NULL;` | `#else /* !XCLIPBOARD_ON */` 降级实现 | 保留 |
| Src/XGui/Application/XGuiApplication.c:994 | `XGuiApplication_inputMethod` | `return NULL;` | `#else /* !XINPUTMETHOD_ON */` 降级实现 | 保留 |
| Src/XGui/Graphics/XImageBuiltinPlugin.c:1139 | `XImageBuiltinPlugin_instance` | `return NULL;` | `#else /* !XIMAGEIOPLUGIN_ON */` 降级实现 | 保留 |
| Src/XGui/Graphics/XImagePluginRegistry.c:1073 | `XImagePluginRegistry_clear` | 空 | `#else`（`XIMAGECODEC_ON`=0）降级桩组之一 | 保留 |
| Src/XGui/Graphics/XImagePluginRegistry.c:1076 | `XImagePluginRegistry_pluginCount` | `return 0;` | 同上降级桩组 | 保留 |
| Src/XGui/Widget/XDial.c:449 | `VXSliderBase_dialStub` | 空 | 注释明确："基类桩（占位避免空翻译单元告警）"，有说明 | 保留 |

另有大量"基类默认虚槽实现"（如 `XLayoutItem.c` 的 `VXLayoutItem_*` 系列、
`XLayout.c:282`）函数体仅 `(void)self;` 或 `return <默认值>;`，但均带
doxygen 说明注释（"基类默认……"），按任务定义（"无注释说明"）不属于候选。
真正"无注释的空实现"除上述 7 处外为 0。

### 2.5 待人工复核（证据不足以下清理结论的疑似项）

1. **20 个无特性守卫的头文件**（清单见 2.2）：是否补 `#if X*_ON` 需先决定
   XMovie/XIcon/XImageReader 等是否允许被裁剪（XLabel.h 等已把 XMovie 作为
   可选依赖处理，理论上可裁）。涉及新增开关，属功能变更。
2. **XHeaderView 继承偏差**（1.2 表 #27）：是否补齐
   `XAbstractItemView` 中间层，或维持"表头仅作 XTableWidget 附属"的简化定位；
   同时其守卫借用 `XTABLEWIDGET_ON`，建议增设专属开关或在文档中说明。
3. **普通类根映射约定不统一**：XCursor（XCursor.h:61）、XBackingStore
   （XBackingStore.h:62）把 Qt 普通类映射为 `XObject`，而 XImageWriter/XIcon/
   XLayoutItem 等普通类映射为 `XClass`。建议统一约定（普通类 → XClass）或
   在注释中说明选择 XObject 的理由（如需要信号/事件能力）。
4. **Charts 简化映射**（1.4 表 #83）：XChartView/XChart 与 QtCharts 的
   QGraphicsView/QGraphicsWidget 链形状不同，属模块级取舍，需设计层面确认。
5. **XIconThemeIcon 的 Legacy 枚举项**（XIcon.h:53 注释）：为兼容 XinYueC 旧
   版本名称保留、不参与 Qt 标准序号。有明确注释，属"保留"项；若旧版本兼容期
   已过可评估删除，需先统计外部使用。
6. **XWidget_windowIconText / setWindowIconText**（XWidget.h:1201/1203 附近）：
   对标 Qt **已废弃**的 `windowIconText`，但当前有实现且有文档声明"仅存储不
   推送"。保留与否取决于是否追求与 Qt 6.8 废弃策略对齐。
7. **2.3 表中 32 个死声明**的"补实现 vs 删声明"取舍：因涉及公共 API 面，
   建议按模块分批决策（本报告仅给出证据与两条路径）。

---

## 附：数据快照

- XGui 类总数：144（`XCLASS_DEFINE_BEGING` 计）
- 继承链对照样本：83 项（任务清单 38 类全覆盖）；一致 75、适配性一致 5、
  硬偏差 1（XHeaderView）、模块级偏差 1（Charts）、轻微偏差 2（XCursor/XBackingStore）、
  不适用 1（XPerformanceOverlay，XGui 自有）
- 守卫外声明：块级 0；整文件无守卫 20
- 死声明（声明无实现无调用）：32
- 无注释空实现：0（7 处命中均为有据可查的回退/占位桩）
- 审计过程未修改任何源码文件；本报告为唯一写入文件
