# XGui Widget 模块 Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计模块：`Widget`（`Src/XGui/Widget/`，58 个公共头文件 + 56 个 .c；跳过 `*_Protected.h` 审计，仅作参考）
- 审计方式：只读审计；未修改 `Src/`、`Test/` 任何源码，未 commit/push
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/{kernel,widgets,itemviews,dialogs,util,effects}`（行为对照另参考同目录 .cpp；XTextDocument 对照 `src/gui/text/qtextdocument.h`）
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`（10.4xx Widget 相关章节）
- 统计口径：API 缺口 = 自动化名称差（Qt 公开方法名 vs X 头文件 `X类_方法[_N]` 归一比对，剔除 Qt 保护虚函数名/宏名），并人工剔除可由 XWidget 继承覆盖的项目（move/resize/setGeometry/setVisible/addAction 等）；功能缺口 = 空函数体计数（`{ (void)self; }` / `{ (void)self; (void)x; }`，33 个 .c 共 421 处）+ 源码注释明示未实现项；信号缺口 = Qt `Q_SIGNALS` 名 vs X `*_signal` 声明/宏比对。

## 结论摘要

Widget 模块是 XGui 中类数量最多的模块，58 个 X 类覆盖 Qt 5 个目录的主要控件，**继承链整体一比一**（仅 XWidget 缺 QPaintDevice 分支，见 2.1）。但存在三类系统性问题：

1. **P0（约束3/7）：约 421 个空函数体占位**（33 个 .c）。大部分是头文件主守卫 `#endif` 之外、`extern "C"` 之外的遗留 `_2/_3/_4/_5` 单参接口（旧 API 未删除），另一部分直接是**真实 Qt 对应 API 的空实现**（XMessageBox_setDetailedText、XTextEdit_setFontFamily、XMainWindow_setTabPosition、XCalendarWidget_showTodayPage、XComboBox_setItemIcon、XMdiArea_setBackground 等），并伴随机翻 Doxygen（约束4）。
2. **P0（约束4）：4 个公共头缺 UTF-8 BOM**（XDialogButtonBox.h、XMainWindow.h、XMenu.h、XTabWidget.h）。
3. **P1（约束1/5）：拥有型字符串违规**（XComboBox.m_items、XTabBar.m_titles 为 char**）；7 个 Qt 信号完全缺失（XAbstractItemView 7 个全缺、XMessageBox::buttonClicked、XTextEdit::currentCharFormatChanged、XTextDocument 7 个）与大量信号参数缺失（XComboBox 7 个、XAbstractSlider 4 个、XTabBar 4 个等）。

高对齐类：XLabel/XPushButton/XRadioButton/XStackedWidget/XFrame/XDialogButtonBox/XCheckBox/XKeySequenceEdit/XSplashScreen（85~90 分）；中低对齐类：XTextDocument（约 15%）、XAbstractItemView（约 12%）、XMdiSubWindow（约 25%）、XMessageBox/XTextEdit/XTableView/XTableWidget/XTabBar/XMainWindow（30~60%）。最严重缺口集中在 **item view 家族（无 model/view/delegate/selection 架构，QListView/QTreeView/QHeaderView 全缺）与文本编辑类（XTextDocument 仅块/片段子集，XTextEdit/XPlainTextEdit 大量格式化 API 为空实现）**。

---

## 一、模块概览

| X 类 | Qt 类 | X 继承链 | Qt 继承链 | 一比一 | API 缺口 | 空 stub | 对齐度 |
|---|---|---|---|---|---|---|---|
| XWidget | QWidget | XWidget→XObject | QWidget→QObject(+QPaintDevice) | × | 24 | 35 | 60 |
| XAbstractButton | QAbstractButton | XAbstractButton→XWidget | QAbstractButton→QWidget | ✓ | 1 | 2 | 88 |
| XAbstractScrollArea | QAbstractScrollArea | XAbstractScrollArea→XFrame→XWidget | QAbstractScrollArea→QFrame→QWidget | ✓ | 6 | 3 | 75 |
| XAbstractSlider | QAbstractSlider | XAbstractSlider→XWidget | QAbstractSlider→QWidget | ✓ | 0 | 6 | 82 |
| XAbstractSpinBox | QAbstractSpinBox | XAbstractSpinBox→XWidget | QAbstractSpinBox→QWidget | ✓ | 2 | 27 | 62 |
| XAbstractItemView | QAbstractItemView | XAbstractItemView→XAbstractScrollArea | QAbstractItemView→QAbstractScrollArea | ✓ | 55 | 0 | 12 |
| XButtonGroup | QButtonGroup | XButtonGroup→XObject | QButtonGroup→QObject | ✓ | 0 | 8 | 85 |
| XCalendarWidget | QCalendarWidget | XCalendarWidget→XWidget | QCalendarWidget→QWidget | ✓ | 15 | 21 | 62 |
| XCheckBox | QCheckBox | XCheckBox→XAbstractButton | QCheckBox→QAbstractButton | ✓ | 0 | 3 | 88 |
| XComboBox | QComboBox | XComboBox→XWidget | QComboBox→QWidget | ✓ | 16 | 10 | 70 |
| XCommandLinkButton | QCommandLinkButton | XCommandLinkButton→XPushButton | QCommandLinkButton→QPushButton | ✓ | 1 | 0 | 85 |
| XDateTimeEdit | QDateTimeEdit | XDateTimeEdit→XAbstractSpinBox | QDateTimeEdit→QAbstractSpinBox | ✓ | 18 | 12 | 68 |
| XDial | QDial | XDial→XAbstractSlider | QDial→QAbstractSlider | ✓ | 2 | 0 | 84 |
| XDialogButtonBox | QDialogButtonBox | XDialogButtonBox→XWidget | QDialogButtonBox→QWidget | ✓ | 0 | 3 | 85 |
| XDialog | QDialog | XDialog→XWidget | QDialog→QWidget | ✓ | 2 | 7 | 78 |
| XDockWidget | QDockWidget | XDockWidget→XWidget | QDockWidget→QWidget | ✓ | 1 | 10 | 80 |
| XErrorMessage | QErrorMessage | XErrorMessage→XDialog | QErrorMessage→QDialog | ✓ | 0 | 4 | 80 |
| XFocusFrame | QFocusFrame | XFocusFrame→XWidget | QFocusFrame→QWidget | ✓ | 0 | 0 | 88 |
| XFontComboBox | QFontComboBox | XFontComboBox→XComboBox | QFontComboBox→QComboBox | ✓ | 6 | 6 | 70 |
| XFrame | QFrame | XFrame→XWidget | QFrame→QWidget | ✓ | 0 | 11 | 85 |
| XGroupBox | QGroupBox | XGroupBox→XWidget | QGroupBox→QWidget | ✓ | 1 | 9 | 78 |
| XKeySequenceEdit | QKeySequenceEdit | XKeySequenceEdit→XWidget | QKeySequenceEdit→QWidget | ✓ | 0 | 3 | 85 |
| XLabel | QLabel | XLabel→XFrame→XWidget | QLabel→QFrame→QWidget | ✓ | 0 | 0 | 90 |
| XLcdNumber | QLCDNumber | XLcdNumber→XFrame | QLCDNumber→QFrame | ✓ | 1 | 0 | 85 |
| XLineEdit | QLineEdit | XLineEdit→XWidget | QLineEdit→QWidget | ✓ | 1 | 9 | 80 |
| XMainWindow | QMainWindow | XMainWindow→XWidget | QMainWindow→QWidget | ✓ | 9 | 27 | 55 |
| XMdiArea | QMdiArea | XMdiArea→XAbstractScrollArea | QMdiArea→QAbstractScrollArea | ✓ | 5 | 22 | 60 |
| XMdiSubWindow | QMdiSubWindow | XMdiSubWindow→XWidget | QMdiSubWindow→QWidget | ✓ | 12 | 0 | 25 |
| XMenuBar | QMenuBar | XMenuBar→XWidget | QMenuBar→QWidget | ✓ | 9 | 0 | 75 |
| XMenu | QMenu | XMenu→XWidget | QMenu→QWidget | ✓ | 12 | 17 | 70 |
| XMessageBox | QMessageBox | XMessageBox→XDialog | QMessageBox→QDialog | ✓ | 8 | 25 | 45 |
| XPlainTextEdit | QPlainTextEdit | XPlainTextEdit→XAbstractScrollArea | QPlainTextEdit→QAbstractScrollArea | ✓ | 14 | 18 | 62 |
| XProgressBar | QProgressBar | XProgressBar→XWidget | QProgressBar→QWidget | ✓ | 2 | 9 | 78 |
| XPushButton | QPushButton | XPushButton→XAbstractButton | QPushButton→QAbstractButton | ✓ | 0 | 0 | 88 |
| XRadioButton | QRadioButton | XRadioButton→XAbstractButton | QRadioButton→QAbstractButton | ✓ | 0 | 0 | 85 |
| XRubberBand | QRubberBand | XRubberBand→XWidget | QRubberBand→QWidget | ✓ | 0 | 0 | 80 |
| XScrollArea | QScrollArea | XScrollArea→XAbstractScrollArea | QScrollArea→QAbstractScrollArea | ✓ | 1 | 10 | 75 |
| XScrollBar | QScrollBar | XScrollBar→XAbstractSlider | QScrollBar→QAbstractSlider | ✓ | 1 | 0 | 82 |
| XSizeGrip | QSizeGrip | XSizeGrip→XWidget | QSizeGrip→QWidget | ✓ | 1 | 0 | 75 |
| XSlider | QSlider | XSlider→XAbstractSlider | QSlider→QAbstractSlider | ✓ | 2 | 0 | 80 |
| XSpinBox | QSpinBox | XSpinBox→XAbstractSpinBox | QSpinBox→QAbstractSpinBox | ✓ | 0 | 14 | 78 |
| XSplashScreen | QSplashScreen | XSplashScreen→XWidget | QSplashScreen→QWidget | ✓ | 0 | 0 | 85 |
| XSplitter | QSplitter | XSplitter→XFrame | QSplitter→QFrame | ✓ | 3 | 9 | 75 |
| XStackedWidget | QStackedWidget | XStackedWidget→XFrame | QStackedWidget→QFrame | ✓ | 0 | 0 | 88 |
| XStatusBar | QStatusBar | XStatusBar→XWidget | QStatusBar→QWidget | ✓ | 0 | 8 | 82 |
| XTabBar | QTabBar | XTabBar→XWidget | QTabBar→QWidget | ✓ | 6 | 30 | 55 |
| XTableView | QTableView | XTableView→XAbstractItemView | QTableView→QAbstractItemView | ✓ | 35 | 0 | 35 |
| XTableWidget | QTableWidget | XTableWidget→XTableView | QTableWidget→QTableView | ✓ | 33 | 16 | 50 |
| XTabWidget | QTabWidget | XTabWidget→XWidget | QTabWidget→QWidget | ✓ | 12 | 5 | 75 |
| XTextBrowser | QTextBrowser | XTextBrowser→XTextEdit | QTextBrowser→QTextEdit | ✓ | 3 | 12 | 70 |
| XTextEdit | QTextEdit | XTextEdit→XAbstractScrollArea | QTextEdit→QAbstractScrollArea | ✓ | 19 | 22 | 50 |
| XToolBar | QToolBar | XToolBar→XWidget | QToolBar→QWidget | ✓ | 6 | 9 | 75 |
| XToolBox | QToolBox | XToolBox→XFrame | QToolBox→QFrame | ✓ | 1 | 3 | 82 |
| XToolButton | QToolButton | XToolButton→XAbstractButton | QToolButton→QAbstractButton | ✓ | 0 | 6 | 85 |
| XWizard | QWizard | XWizard→XDialog | QWizard→QDialog | ✓ | 5 | 18 | 68 |
| XWizardPage | QWizardPage | XWizardPage→XWidget | QWizardPage→QWidget | ✓ | 12 | 0 | 45 |
| XTextDocument | QTextDocument | XTextDocument→XObject | QTextDocument→QObject | ✓ | 65 | 6 | 15 |
| XPerformanceOverlay | （X 专有，无 Qt 对应） | XPerformanceOverlay→XLabel | — | — | 0 | 0 | 90 |

> 说明：对齐度为人工综合评分（API 覆盖、信号、空实现、绘制/交互完整性），非机械公式。

## 二、跨类系统性问题（按硬约束编号）

### 2.1 继承链总体判定

- 58 个 X 类中 57 个与 Qt 继承链**一比一**（含中间基类：XAbstractScrollArea↔QAbstractScrollArea 前有 XFrame↔QFrame，XAbstractItemView↔QAbstractItemView 前有 XAbstractScrollArea，XLabel/XLcdNumber/XSplitter/XStackedWidget/XToolBox↔QLabel/QLCDNumber/QSplitter/QStackedWidget/QToolBox 前有 XFrame↔QFrame，XCommandLinkButton↔QCommandLinkButton 前有 XPushButton↔QPushButton 等）。
- **唯一偏差**：`XWidget` 的 Qt 侧为 `QWidget : QObject, QPaintDevice`（双基类），X 侧第一成员为 `XObject`（`XCLASS_DEFINE_ENUM(XWidget, PaintEvent) = XCLASS_VTABLE_GET_SIZE(XObject)`），**无 XPaintDevice 等价层**。绘制能力由 XPainter 外部注入（paintEvent 虚槽自绘），功能上等价但结构上不构成严格 1:1；建议记为已知偏差并在文档声明（补 XPaintDevice 基类成本高、收益低）。
- 模块开关守卫（`XWIDGET_ON && XXXX_ON`）下各派生类 `XCLASS_DEFINE_EXTEND_END` 均指向正确基类，`class_init` 中 `XVTABLE_INHERIT_*` 与之一致（抽查 XAbstractButton/XProgressBar/XMenu/XWizard 均正确）。

### 2.2 V1【P0·约束3/7】约 421 个空函数体占位，含大量头文件守卫外遗留旧接口

- 全模块 33 个 .c 中共 **421** 处函数体仅为 `{ (void)self; }`（或加 `(void)参数;`）的空实现（按文件：XWidget 35、XTabBar 30、XMainWindow 27、XAbstractSpinBox 27、XMessageBox 25、XMdiArea 22、XTextEdit 22、XCalendarWidget 21、XPlainTextEdit 18、XWizard 18、XMenu 17、XTableWidget 16、XSpinBox 14、XDateTimeEdit 12、XTextBrowser 12、XFrame 11 等）。
- **类型 A（遗留旧 API，违反约束 7“旧 API 不保留”）**：几乎所有公共头文件在主 `#endif /* XWIDGET_ON && XXXX_ON */` **之后**、`extern "C"` 关闭之后，还有一段“旧接口”声明区，如：
  - `XAbstractSlider.h:343-388`：`XAbstractSlider_invertedAppearance_2(self)`、`setTracking_2(self)`、`triggerAction_2(self)` 等单参声明（无对应 Qt 原型）；
  - `XDialog.h:106-140`：`XDialog_open_2`、`XDialog_setSizeGripEnabled(self)`（**缺 bool 参数**）、`XDialog_isSizeGripEnabled(self)`（**返回 void**）、`XDialog_result_2` 等；
  - `XMainWindow.h:325-410`：`insertToolBarBreak_2`、`setCentralWidget_2`、`iconSizeChanged_signal_2` 等；
  - `XAbstractSpinBox.h:320-447`：`clear_3`、`isSelected_2`、`setKeyboardStep_3`、`setReadOnly_4`、`setAlignment_5` 等 25 个。
  - 这些声明位于模块开关与 `extern "C"` 之外，且 .c 中实现全部为空函数体（如 `XWidget.c:4809-4825`、`XComboBox.c:785`、`XMainWindow.c:452`），调用即无操作，属悬挂式旧 API 保留。
- **类型 B（真实 Qt 对应 API 空实现，违反约束 3“样式/绘制/行为不得精简近似”）**，代表性例子：
  - `XMessageBox.c:289-299`：`setDetailedText`/`setInformativeText`/`addButton`/`setDefaultButton`/`setEscapeButton` 空实现，getter 返回 ""/0；
  - `XTextEdit.c:279-311`：`setAcceptRichText`/`setFontFamily`/`setFontWeight`/`setFontPointSize`/`zoomIn`/`zoomOut`/`setTabStopDistance`/`setAutoFormatting`/`setDocumentTitle`/`setLineWrapMode`/`setWordWrapMode` 空实现（getter 返回常量）；
  - `XPlainTextEdit.c:827-848`：`setTextBackgroundColor_2`/`setFontFamily_2`/`setCursorWidth_2`/`setTabStopDistance_2`/`anchorAt` 空实现；
  - `XMainWindow.c:342-363`：`toolBarArea`/`dockWidgetArea`/`addToolBarBreak`/`setDocumentMode`/`setCorner`/`setTabPosition`/`setTabShape`/`setAnimated`/`setDockNestingEnabled`/`setSeparator`/`insertToolBar`/`removeToolBar` 空实现（getter 返回 0/false）；
  - `XTabBar.c:535-567`：`setDocumentMode`/`setElideMode`/`setExpanding`/`setUsesScrollButtons`/`setTabButton`/`setTabTextColor`/`setTabToolTip`/`setTabIcon`/`setDrawBase`/`tabRect`/`tabAt`/`tabWidth`/`tabHeight`/`tabIndexAt`/`isTabVisible`/`isEmpty`/`moveTab` 空实现；
  - `XCalendarWidget.c:520-539`：`setDateEditEnabled`/`weekNumber`/`setHeaderTextFormat`/`setWeekdayTextFormat`/`isDateSelected`/`setShowTodayDate`/`setVerticalHeaderFormat`/`showTodayPage` 空实现（showTodayPage 是 Qt showToday() 的唯一导航实现，仍为空）；
  - `XComboBox.c:777-784`：`findData`/`setItemIcon`/`setItemData`/`itemData`/`setCompleter` 空实现；
  - `XDateTimeEdit.c:388-393`：`calendarPopup`/`setCalendarPopup`/`setTimeSpec`/`setCurrentSectionIndex` 空实现；
  - `XMdiArea.c:387-411`：`setBackground`/`background`/`setTabPosition`/`tabPosition`/`setTabsClosable_2`/`setActivationOrder` 空实现；
  - `XWizard.c:606-624`：`setPixmap`/`field`/`setSideWidget`/`setButtonLayout`/`setTitleFormat`/`setSubTitleFormat`/`cleanupPage`/`initializePage` 空实现；
  - `XAbstractScrollArea.c:173`：`scrollContentsBy` 明确注释“第一版为空操作占位”，直接影响 XScrollArea/XTableView/XTableWidget 的内容滚动。
- **附带违规（约束4）**：旧接口区注释为机翻 Doxygen，如 `XAbstractSpinBox.h:326`“XAbstractSpin盒clear3（对标 Qt 同名接口）”、`XMessageBox.h:195`“X消息盒setDetailed文本”、`XMainWindow.h:326`“XMainWindowinsert工具条Break2”。
- **修复建议**：一次性删除所有守卫外遗留声明与对应空实现（约 380 处），再逐类补齐类型 B 真实 API；删除前用 `grep -rn` 确认无调用（本次审计未对 Test/ 做全面调用扫描，实施时需确认）。

### 2.3 V2【P0·约束4】4 个公共头缺 UTF-8 BOM

- `Src/XGui/Widget/XDialogButtonBox.h`、`XMainWindow.h`、`XMenu.h`、`XTabWidget.h` 首字节非 EF BB BF（`*_Protected.h` 另有 2 个，不在审计范围但建议一并修复）。

### 2.4 V3【P1·约束1】拥有型字符串违规（char**）

- `XComboBox.h:85`：`char** m_items;`（项文本数组，每项拥有）——拥有型字符串应为 `XString*`；`XComboBox.c`/`XTabBar.c` 目前以 `XFree_System`/`XMemory_strdup` 直接管理 char*。例外清单仅允许 XColorSpace char[64] 与 XLineEdit 编辑缓冲，**不覆盖 XComboBox/XTabBar**。
- `XTabBar.h`（m_titles 同款 `char**`）。XLcdNumber 的 `m_digitStr/m_points` 为定长 LCD 段显示缓冲，性质同 XLineEdit 编辑缓冲，记为 P2 容忍项。
- 修复建议：改为 `XString*`/`XString**` 并保留 `_2` UTF-8 重载入口；属 API 不变量变更，需同步 Test/ 调用点。

### 2.5 V4【P1·约束5】信号缺失与信号参数缺失

完全缺失（Qt 6.8 有、X 无 `*_signal`）：

- `XAbstractItemView`：pressed/clicked/doubleClicked/activated/entered/viewportEntered/iconSizeChanged 7 个全缺（XTableView/XTableWidget 亦未在基类补齐；XTableWidget 只有自身的 15 个 item/cell 信号）；
- `XMessageBox::buttonClicked(QAbstractButton*)`；`XTextEdit::currentCharFormatChanged(QTextFormat)`（X 多出的 modificationChanged 不是 Qt 信号）；
- `XTextDocument`：contentsChange/baseUrlChanged/cursorPositionChanged/documentLayoutChanged/redoAvailable/undoAvailable/undoCommandAdded 7 个全缺；`XWizardPage::completeChanged`；`XMdiSubWindow::aboutToActivate/windowStateChanged`。

有信号但参数缺失（连接方拿不到 Qt 语义参数）：

- `XComboBox`：activated/textActivated/highlighted/textHighlighted/currentIndexChanged/currentTextChanged/editTextChanged 7 个信号均只带 self（XComboBox.h:342-372）；
- `XAbstractSlider`：valueChanged/sliderMoved 缺 int，rangeChanged 缺 (int,int)，actionTriggered 缺 int（XSlider/XScrollBar/XDial 宏继承同缺）；
- `XTabBar`：currentChanged 缺 int，tabBarClicked/tabBarDoubleClicked 缺 int，tabMoved 缺 (int,int)，且 `tabClicked` 非 Qt 信号名；
- `XTabWidget::currentChanged` 缺 int；`XProgressBar::valueChanged` 缺 int；`XFontComboBox::currentFontChanged` 缺 QFont；`XMainWindow/XToolBar::iconSizeChanged` 缺 QSize；`XDockWidget::dockLocationChanged` 缺 area；`XTextBrowser::anchorClicked` 缺 QUrl（且多出非 Qt 的 highlighted）；`XWizard::customButtonClicked` 缺 which。

### 2.6 V5【P1·约束2a】命名/类型映射不一致

- `XWizard`：Qt 6.8 为 `currentId()/setCurrentId(int)/startId()/setStartId(int)/pageIds()/visitedIds()`，X 用 `currentIndex/setStartIndex/startIndex/visitedIds_count`（XWizard.h:229-259），命名不一致；信号 `currentIdChanged_signal(self, int index)` 语义对但名字错位。
- `XFontComboBox`：Qt `currentFont()/setCurrentFont(QFont)`，X 为 `currentFamily()/setCurrentFamily(const char*)`（只暴露家族名）。
- QIcon/QPixmap 类型映射简化：`XComboBox_setItemIcon(int,const char*)`、`XTabBar_setTabIcon(int,const char*)`、`XWizard_setPixmap(int,const char*)`、`XMessageBox_setIcon(枚举)` 等以路径字符串/枚举替代 QIcon/QPixmap 对象，属嵌入式裁剪型映射，应在头文件声明为已知差异（部分已注明，部分未注明）。
- `XMainWindow/XToolBar/XTabWidget/XMdiArea::setIconSize(int)` 以整数方边替代 Qt QSize（单值近似）。
- QVariant 缺失连锁：QComboBox 的 itemData/currentData/findData、QTableView 系 setItemData、QWizard::setField/field 均退化或空实现（XWizard.c:608-609 直接空实现）。

### 2.7 V6【P2】裁剪开关失效

- `XAbstractItemView.h:12`、`XTableView.h:12` 使用 `#if XTABLEWIDGET_ON || 1`，恒为真，模块开关失去裁剪能力（其它文件均用 `XWIDGET_ON && XXXX_ON`）。

### 2.8 V7【P1·约束6】生命周期与内存

- 未发现 `malloc/free/strdup/memcpy` 直接调用（0 处）；复制/移动统一走 `XCopy/XMove`（抽查 XAbstractButton.c:404/941/978、XLabel.c:1928/1985、XMenu.c:1062），符合约束 6。
- 头文件未再声明 `*_copy_base/*_move_base`（XProgressBar.h:164 仅为注释），符合风格文档 2026-09-08 变更。
- 遗留风险：旧接口区声明位于 `extern "C"` 之外（C++ 链接名错误风险），且位于模块开关之外（裁剪构建下仍可见，可能破坏 `XGUI_ON=0` 裁剪构建），建议立即删除。

## 三、逐类对比
### 3.1 XWidget ↔ QWidget（kernel/qwidget.h）

- 继承：X：`XWidget→XObject`；Qt：`QWidget→QObject(+QPaintDevice)`；判定：**不完全（见 2.1）**。
- API 缺口（24 项）：connect(QObject*,QObject*)/winId()/createWinId()/effectiveWinId()/internalWinId()/ensurePolished()/fontInfo()/fontMetrics()/grab()/render()/saveGeometry()/restoreGeometry()/screen()/setScreen()/locale()/setStyle()/style()/setShortcut*()/releaseShortcut()/createWindowContainer()/setEditFocus()/ungrabGesture()/insertActions()/find()
- 功能缺口（空 stub 35 处）：35 个空 stub（多为遗留 _2/_3，含 addAction_3/setLocale_2/focusPolicy_2/setMask_2 等）；平台窗口句柄族 API 整体缺失（嵌入式裁剪可接受，需声明）；updateRegion/repaintRegion/scroll_2 为简化路径。
- 违规/注意：继承缺 QPaintDevice 分支（2.1）；空 stub 35；

### 3.2 XAbstractButton ↔ QAbstractButton（widgets/qabstractbutton.h）

- 继承：X：`XAbstractButton→XWidget`；Qt：`QAbstractButton→QWidget`；判定：**一比一 ✓**。
- API 缺口（1 项）：shortcut()/setShortcut(const QKeySequence&)
- 功能缺口（空 stub 2 处）：2 个空 stub（遗留）；hitButton/事件分发与 Qt 对齐（XGui.md 多轮对齐）；显式 QButtonGroup 登记未实现，自动互斥为父控件兄弟遍历近似（XPushButton.c:24）。
- 违规/注意：—

### 3.3 XAbstractScrollArea ↔ QAbstractScrollArea（widgets/qabstractscrollarea.h）

- 继承：X：`XAbstractScrollArea→XFrame→XWidget`；Qt：`QAbstractScrollArea→QFrame→QWidget`；判定：**一比一 ✓**。
- API 缺口（6 项）：setVerticalScrollBar(QScrollBar*)/verticalScrollBar()/setHorizontalScrollBar()/horizontalScrollBar()/scrollBarWidgets()/maximumViewportSize()/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 3 处）：scrollContentsBy 首版为空操作占位（XAbstractScrollArea.c:173），内容平移未实现，直接影响 XScrollArea/XTableView/XTableWidget；3 个遗留空 stub。
- 违规/注意：—

### 3.4 XAbstractSlider ↔ QAbstractSlider（widgets/qabstractslider.h）

- 继承：X：`XAbstractSlider→XWidget`；Qt：`QAbstractSlider→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（24 个 Qt 公开 API 全覆盖）
- 功能缺口（空 stub 6 处）：6 个遗留空 stub；长按自动重复 timer 为后续扩展（头文件注明）；信号参数缺失见 2.5。
- 违规/注意：信号参数缺失：valueChanged/sliderMoved/rangeChanged/actionTriggered

### 3.5 XAbstractSpinBox ↔ QAbstractSpinBox（widgets/qabstractspinbox.h）

- 继承：X：`XAbstractSpinBox→XWidget`；Qt：`QAbstractSpinBox→QWidget`；判定：**一比一 ✓**。
- API 缺口（2 项）：sizeHint()/minimumSizeHint()（validate/fixup/stepBy 为 Qt 保护虚函数，X 已有 _base 虚槽，不计缺口）
- 功能缺口（空 stub 27 处）：27 个空 stub（25 个为守卫外遗留 _3/_4/_5）；长按连续步进 timer 未实现；按钮符号/修正模式等核心已实现。
- 违规/注意：守卫外遗留声明 25 处；机翻 Doxygen（“XAbstractSpin盒…”）

### 3.6 XAbstractItemView ↔ QAbstractItemView（itemviews/qabstractitemview.h）

- 继承：X：`XAbstractItemView→XAbstractScrollArea`；Qt：`QAbstractItemView→QAbstractScrollArea`；判定：**一比一 ✓**。
- API 缺口（55 项）：model()/setModel()/selectionModel()/setSelectionModel()/itemDelegate()/setItemDelegate()/indexAt()/visualRect()/scrollTo()/edit()/openPersistentEditor()/closePersistentEditor()/isPersistentEditorOpen()/setIndexWidget()/indexWidget()/keyboardSearch()/selectAll()/clearSelection()/reset()/scrollToTop()/scrollToBottom()/setRootIndex()/rootIndex()/setTextElideMode()/setTabKeyNavigation()/dragDropMode()/setDragEnabled()/showDropIndicator()/sizeHintForIndex() 等（共 55）
- 功能缺口（空 stub 0 处）：实现仅承载 currentRow/currentColumn/selectionMode/selectionBehavior/editTriggers/alternatingRowColors/autoScroll 属性存储；无模型、无选择集合、无委托、无拖放、无编辑交互；7 个 Qt 信号全缺。
- 违规/注意：信号 7 个全缺；`#if XTABLEWIDGET_ON || 1` 恒开

### 3.7 XButtonGroup ↔ QButtonGroup（widgets/qbuttongroup.h）

- 继承：X：`XButtonGroup→XObject`；Qt：`QButtonGroup→QObject`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（10/10）
- 功能缺口（空 stub 8 处）：8 个遗留空 stub；exclusive 互斥联动与 Qt 一致。
- 违规/注意：—

### 3.8 XCalendarWidget ↔ QCalendarWidget（widgets/qcalendarwidget.h）

- 继承：X：`XCalendarWidget→XWidget`；Qt：`QCalendarWidget→QWidget`；判定：**一比一 ✓**。
- API 缺口（15 项）：setCalendar()/calendar()/setDateTextFormat()/dateTextFormat()/horizontalHeaderFormat()/verticalHeaderFormat()/showNextMonth()/showNextYear()/showPreviousMonth()/showPreviousYear()/showSelectedDate()/showToday()/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 21 处）：21 个空 stub 中 13 个为真实 API（setDateEditEnabled/weekNumber/setHeaderTextFormat/setWeekdayTextFormat/isDateSelected/setShowTodayDate/setVerticalHeaderFormat/showTodayPage 等）；导航只能 setCurrentPage 翻页；日期格式仅 int 枚举，无 QTextCharFormat 级格式。
- 违规/注意：真实 API 空实现 13 处；无 QCalendar 后端

### 3.9 XCheckBox ↔ QCheckBox（widgets/qcheckbox.h）

- 继承：X：`XCheckBox→XAbstractButton`；Qt：`QCheckBox→QAbstractButton`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（6/6）
- 功能缺口（空 stub 3 处）：3 个遗留空 stub；stateChanged(int) 未实现（Qt 6.9 起废弃，可接受）；hitButton 按 indicator 矩形。
- 违规/注意：—

### 3.10 XComboBox ↔ QComboBox（widgets/qcombobox.h）

- 继承：X：`XComboBox→XWidget`；Qt：`QComboBox→QWidget`；判定：**一比一 ✓**。
- API 缺口（16 项）：model()/setModel()/modelColumn()/setModelColumn()/rootModelIndex()/setRootModelIndex()/view()/setView()/setLineEdit()/lineEdit()/setItemDelegate()/completer()/setCompleter()/validator()/currentData()/itemIcon()/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 10 处）：10 个空 stub 中 5 个为真实 API（findData/setItemIcon/setItemData/itemData/setCompleter）；弹出列表为自绘 XWidget 而非 QAbstractItemView；icon 用路径字符串。
- 违规/注意：m_items 为拥有型 char**（约束1）；7 个信号全部缺参数（约束5）

### 3.11 XCommandLinkButton ↔ QCommandLinkButton（widgets/qcommandlinkbutton.h）

- 继承：X：`XCommandLinkButton→XPushButton`；Qt：`QCommandLinkButton→QPushButton`；判定：**一比一 ✓**。
- API 缺口（1 项）：heightForWidth(int)
- 功能缺口（空 stub 0 处）：绘制按 QCommandLinkButton 近似（箭头为三角近似）；描述按宽度换行计算高度未实现（XCommandLinkButton.c:18）。
- 违规/注意：—

### 3.12 XDateTimeEdit ↔ QDateTimeEdit（widgets/qdatetimeedit.h）

- 继承：X：`XDateTimeEdit→XAbstractSpinBox`；Qt：`QDateTimeEdit→QAbstractSpinBox`；判定：**一比一 ✓**。
- API 缺口（18 项）：setCalendar()/calendar()/setCalendarWidget()/calendarWidget()/setTimeZone()/sectionAt()/sectionCount()/sectionText()/setSelectedSection()/displayedSections()/setMinimumDate()/minimumDate()/setMaximumDate()/maximumDate()/setMinimumTime()/minimumTime()/setMaximumTime()/maximumTime()/sizeHint()（stepBy 为保护虚槽，X 已有 _base）
- 功能缺口（空 stub 12 处）：12 个空 stub 中 6 个为真实 API（calendarPopup/setCalendarPopup/setTimeSpec/timeSpec/setCurrentSectionIndex/currentSectionIndex）；无日历弹窗；时分秒段编辑按索引实现。
- 违规/注意：—

### 3.13 XDial ↔ QDial（widgets/qdial.h）

- 继承：X：`XDial→XAbstractSlider`；Qt：`QDial→QAbstractSlider`；判定：**一比一 ✓**。
- API 缺口（2 项）：sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 0 处）：无空 stub；notch 算法为近似（XDial.c:530）；刻度/圆盘绘制完整。
- 违规/注意：—

### 3.14 XDialogButtonBox ↔ QDialogButtonBox（widgets/qdialogbuttonbox.h）

- 继承：X：`XDialogButtonBox→XWidget`；Qt：`QDialogButtonBox→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（13/13）
- 功能缺口（空 stub 3 处）：3 个遗留空 stub；标准按钮/角色/布局完整。
- 违规/注意：缺 BOM（XDialogButtonBox.h）

### 3.15 XDialog ↔ QDialog（dialogs/qdialog.h）

- 继承：X：`XDialog→XWidget`；Qt：`QDialog→QWidget`；判定：**一比一 ✓**。
- API 缺口（2 项）：sizeHint()/minimumSizeHint()（setVisible 由 XWidget 提供）
- 功能缺口（空 stub 7 处）：7 个空 stub（open_2/setSizeGripEnabled 等遗留；setSizeGripEnabled 签名缺 bool、isSizeGripEnabled 返回 void）；exec 事件循环已实现。
- 违规/注意：守卫外遗留 5 处

### 3.16 XDockWidget ↔ QDockWidget（widgets/qdockwidget.h）

- 继承：X：`XDockWidget→XWidget`；Qt：`QDockWidget→QWidget`；判定：**一比一 ✓**。
- API 缺口（1 项）：isAreaAllowed(Qt::DockWidgetArea)
- 功能缺口（空 stub 10 处）：10 个空 stub 全为遗留 _2；dockLocationChanged 信号缺 area 参数；标题栏动作占位（XDockWidget.c:240）。
- 违规/注意：—

### 3.17 XErrorMessage ↔ QErrorMessage（dialogs/qerrormessage.h）

- 继承：X：`XErrorMessage→XDialog`；Qt：`QErrorMessage→QDialog`；判定：**一比一 ✓**。
- API 缺口（0 项）：—
- 功能缺口（空 stub 4 处）：4 个遗留空 stub；showMessage 已实现。
- 违规/注意：—

### 3.18 XFocusFrame ↔ QFocusFrame（widgets/qfocusframe.h）

- 继承：X：`XFocusFrame→XWidget`；Qt：`QFocusFrame→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—
- 功能缺口（空 stub 0 处）：—
- 违规/注意：—

### 3.19 XFontComboBox ↔ QFontComboBox（widgets/qfontcombobox.h）

- 继承：X：`XFontComboBox→XComboBox`；Qt：`QFontComboBox→QComboBox`；判定：**一比一 ✓**。
- API 缺口（6 项）：currentFont()/setCurrentFont(QFont)/displayFont()/setDisplayFont()/sampleTextForFont()/sampleTextForSystem()/setSampleTextForFont()/setSampleTextForSystem()/sizeHint()
- 功能缺口（空 stub 6 处）：6 个空 stub（含 displayFont/sampleText 族真实 API）；currentFamily 替代 currentFont（命名+类型偏差）。
- 违规/注意：currentFontChanged 信号缺 QFont 参数

### 3.20 XFrame ↔ QFrame（widgets/qframe.h）

- 继承：X：`XFrame→XWidget`；Qt：`QFrame→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（14/14）
- 功能缺口（空 stub 11 处）：11 个空 stub 全为遗留 _2；frameStyle/绘制完整。
- 违规/注意：—

### 3.21 XGroupBox ↔ QGroupBox（widgets/qgroupbox.h）

- 继承：X：`XGroupBox→XWidget`；Qt：`QGroupBox→QWidget`；判定：**一比一 ✓**。
- API 缺口（1 项）：minimumSizeHint()
- 功能缺口（空 stub 9 处）：9 个空 stub 全为遗留；勾选标记为两段斜线近似（XGroupBox.c:268）；标题区点击判定收窄。
- 违规/注意：—

### 3.22 XKeySequenceEdit ↔ QKeySequenceEdit（widgets/qkeysequenceedit.h）

- 继承：X：`XKeySequenceEdit→XWidget`；Qt：`QKeySequenceEdit→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（9/9）
- 功能缺口（空 stub 3 处）：3 个空 stub 遗留；组合键捕获已实现。
- 违规/注意：—

### 3.23 XLabel ↔ QLabel（widgets/qlabel.h）

- 继承：X：`XLabel→XFrame→XWidget`；Qt：`QLabel→QFrame→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（31/31）
- 功能缺口（空 stub 0 处）：无空 stub；富文本为实用近似（mightBeRichText 简化），图片/电影/选区/链接信号齐全。
- 违规/注意：—

### 3.24 XLcdNumber ↔ QLCDNumber（widgets/qlcdnumber.h）

- 继承：X：`XLcdNumber→XFrame`；Qt：`QLCDNumber→QFrame`；判定：**一比一 ✓**。
- API 缺口（1 项）：sizeHint()
- 功能缺口（空 stub 0 处）：无空 stub；段显完整。
- 违规/注意：m_digitStr/m_points 定长 char[]（P2 容忍项）

### 3.25 XLineEdit ↔ QLineEdit（widgets/qlineedit.h）

- 继承：X：`XLineEdit→XWidget`；Qt：`QLineEdit→QWidget`；判定：**一比一 ✓**。
- API 缺口（1 项）：completer()/setCompleter(QCompleter*)
- 功能缺口（空 stub 9 处）：9 个空 stub（遗留）；inputMask 占位符显示；光标 1px 常显、闪烁为后续扩展；undo/redo 栈为内部 char* 缓冲（编辑缓冲例外，合规）。
- 违规/注意：—

### 3.26 XMainWindow ↔ QMainWindow（widgets/qmainwindow.h）

- 继承：X：`XMainWindow→XWidget`；Qt：`QMainWindow→QWidget`；判定：**一比一 ✓**。
- API 缺口（9 项）：resizeDocks()/splitDockWidget()/tabifiedDockWidgets()/restoreState()/saveState()/restoreDockWidget()/menuWidget()/setMenuWidget()/toolBarBreak()/unifiedTitleAndToolBarOnMac()
- 功能缺口（空 stub 27 处）：27 个空 stub 中 12 个为真实 API（toolBarArea/dockWidgetArea/addToolBarBreak/setDocumentMode/setCorner/setTabPosition/setTabShape/setUnifiedTitleAndToolBarOnMac/setAnimated/setDockNestingEnabled/setSeparator/insertToolBar/removeToolBar）；核心（centralWidget/menuBar/statusBar/addToolBar/addDockWidget/setDockOptions/iconSize/toolButtonStyle）已实现。
- 违规/注意：缺 BOM；真实 API 空实现 12 处；iconSizeChanged 信号缺 QSize

### 3.27 XMdiArea ↔ QMdiArea（widgets/qmdiarea.h）

- 继承：X：`XMdiArea→XAbstractScrollArea`；Qt：`QMdiArea→QAbstractScrollArea`；判定：**一比一 ✓**。
- API 缺口（5 项）：addSubWindow()/currentSubWindow()/subWindowList()/setTabsMovable()/tabsMovable()/tabsClosable()/setActivationOrder()/activationOrder()/setOption()/testOption()/background()/setBackground()/setDocumentMode()/tabPosition()/tabShape()
- 功能缺口（空 stub 22 处）：22 个空 stub 中 10 个为真实 API（setBackground/background/setTabPosition/tabPosition/setTabsClosable_2/setActivationOrder 等）；子窗口管理核心（removeSubWindow/setActiveSubWindow/closeAll/cascade/tile/subWindowCount）已实现；TabbedView 仅为存储。
- 违规/注意：真实 API 空实现 10 处

### 3.28 XMdiSubWindow ↔ QMdiSubWindow（widgets/qmdisubwindow.h）

- 继承：X：`XMdiSubWindow→XWidget`；Qt：`QMdiSubWindow→QWidget`；判定：**一比一 ✓**。
- API 缺口（12 项）：setOption()/testOption()/systemMenu()/setSystemMenu()/mdiArea()/keyboardPageStep()/setKeyboardPageStep()/keyboardSingleStep()/setKeyboardSingleStep()/isShaded()/maximizedButtonsWidget()/maximizedSystemMenuIconWidget()/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 0 处）：仅实现 setWidget/widget/标题；无系统菜单、无最小化/最大化/关闭按钮、无窗口状态机；aboutToActivate/windowStateChanged 信号缺失。
- 违规/注意：信号 2 个缺失

### 3.29 XMenuBar ↔ QMenuBar（widgets/qmenubar.h）

- 继承：X：`XMenuBar→XWidget`；Qt：`QMenuBar→QWidget`；判定：**一比一 ✓**。
- API 缺口（9 项）：actionAt()/actionGeometry()/cornerWidget()/setCornerWidget()/heightForWidth()/sizeHint()/minimumSizeHint()/platformMenuBar()/setNativeMenuBar()（addAction/setVisible 由 XWidget 覆盖）
- 功能缺口（空 stub 0 处）：无空 stub；addMenu/addSeparator/insertMenu/insertSeparator/clear/activeAction/defaultUp 已实现。
- 违规/注意：—

### 3.30 XMenu ↔ QMenu（widgets/qmenu.h）

- 继承：X：`XMenu→XWidget`；Qt：`QMenu→QWidget`；判定：**一比一 ✓**。
- API 缺口（12 项）：addSection()/insertSection()/insertMenu()/insertSeparator()/actionGeometry()/menuObject()/menuInAction()/platformMenu()/setPlatformMenu()/setAsDockMenu()/setNoReplayFor()/showTearOffMenu()
- 功能缺口（空 stub 17 处）：17 个空 stub 全为遗留 _2；exec() 阻塞事件循环、addAction/addMenu/popup/信号均实现；tearOff 为字段存储。
- 违规/注意：缺 BOM；17 处遗留空 stub

### 3.31 XMessageBox ↔ QMessageBox（dialogs/qmessagebox.h）

- 继承：X：`XMessageBox→XDialog`；Qt：`QMessageBox→QDialog`；判定：**一比一 ✓**。
- API 缺口（8 项）：aboutQt()/options()/setOptions()/setOption()/testOption()/buttons()/setWindowModality()/standardButton()
- 功能缺口（空 stub 25 处）：25 个空 stub 中 7 个为真实 API（setDetailedText/detailedText/setInformativeText/informativeText/addButton/setDefaultButton/setEscapeButton，getter 返回 0/""）；静态 information/warning/critical/question/about 已实现；buttonClicked 信号缺失。
- 违规/注意：buttonClicked 信号缺失；真实 API 空实现 7 处；守卫外遗留 16 处

### 3.32 XPlainTextEdit ↔ QPlainTextEdit（widgets/qplaintextedit.h）

- 继承：X：`XPlainTextEdit→XAbstractScrollArea`；Qt：`QPlainTextEdit→QAbstractScrollArea`；判定：**一比一 ✓**。
- API 缺口（14 项）：document()/setDocument()/documentTitle()/setDocumentTitle()/textCursor()/setTextCursor()/currentCharFormat()/setCurrentCharFormat()/mergeCurrentCharFormat()/extraSelections()/setExtraSelections()/cursorRect()/canPaste()/appendHtml()/setCenterOnScroll()（loadResource 为保护虚函数）
- 功能缺口（空 stub 18 处）：18 个空 stub 中多数为真实 API（字体/背景/缩放/制表符/光标宽度/行距等空实现，getter 返回常量）；块模型（blockCount/光标行/可见块等）已实现；XTextDocument 为文档后端。
- 违规/注意：真实 API 空实现约 15 处

### 3.33 XProgressBar ↔ QProgressBar（widgets/qprogressbar.h）

- 继承：X：`XProgressBar→XWidget`；Qt：`QProgressBar→QWidget`；判定：**一比一 ✓**。
- API 缺口（2 项）：sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 9 处）：9 个空 stub 全为遗留；绘制为单色居中，分段裁剪/动态属性未实现（头文件注明）；valueChanged 信号缺 int。
- 违规/注意：valueChanged 缺 int 参数

### 3.34 XPushButton ↔ QPushButton（widgets/qpushbutton.h）

- 继承：X：`XPushButton→XAbstractButton`；Qt：`QPushButton→QAbstractButton`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（8/8）
- 功能缺口（空 stub 0 处）：无空 stub；QButtonGroup 显式登记未实现、菜单弹层未实现（XPushButton.c:24-26）；animateClick 定时器已实现。
- 违规/注意：—

### 3.35 XRadioButton ↔ QRadioButton（widgets/qradiobutton.h）

- 继承：X：`XRadioButton→XAbstractButton`；Qt：`QRadioButton→QAbstractButton`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（2/2）
- 功能缺口（空 stub 0 处）：无空 stub；选中圆点用中心方块近似（XRadioButton.c:254，点阵后端无实心椭圆）；快捷键/bevel/悬停未实现。
- 违规/注意：—

### 3.36 XRubberBand ↔ QRubberBand（widgets/qrubberband.h）

- 继承：X：`XRubberBand→XWidget`；Qt：`QRubberBand→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（move/resize/setGeometry 由 XWidget 继承提供，未重载）
- 功能缺口（空 stub 0 处）：shape() 已实现，绘制为 XOR 矩形。
- 违规/注意：—

### 3.37 XScrollArea ↔ QScrollArea（widgets/qscrollarea.h）

- 继承：X：`XScrollArea→XAbstractScrollArea`；Qt：`QScrollArea→QAbstractScrollArea`；判定：**一比一 ✓**。
- API 缺口（1 项）：sizeHint()
- 功能缺口（空 stub 10 处）：10 个空 stub 全为遗留；setWidget/widgetResizable/ensureVisible 已实现；依赖 XAbstractScrollArea 内容滚动占位。
- 违规/注意：—

### 3.38 XScrollBar ↔ QScrollBar（widgets/qscrollbar.h）

- 继承：X：`XScrollBar→XAbstractSlider`；Qt：`QScrollBar→QAbstractSlider`；判定：**一比一 ✓**。
- API 缺口（1 项）：sizeHint()
- 功能缺口（空 stub 0 处）：无空 stub；滑块绘制/拖拽完整。
- 违规/注意：—

### 3.39 XSizeGrip ↔ QSizeGrip（widgets/qsizegrip.h）

- 继承：X：`XSizeGrip→XWidget`；Qt：`QSizeGrip→QWidget`；判定：**一比一 ✓**。
- API 缺口（1 项）：sizeHint()（setVisible 继承自 XWidget）
- 功能缺口（空 stub 0 处）：无空 stub；拖拽缩放已实现。
- 违规/注意：—

### 3.40 XSlider ↔ QSlider（widgets/qslider.h）

- 继承：X：`XSlider→XAbstractSlider`；Qt：`QSlider→QAbstractSlider`；判定：**一比一 ✓**。
- API 缺口（2 项）：sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 0 处）：无空 stub；刻度/绘制完整。
- 违规/注意：信号参数缺失继承自 XAbstractSlider

### 3.41 XSpinBox ↔ QSpinBox（widgets/qspinbox.h）

- 继承：X：`XSpinBox→XAbstractSpinBox`；Qt：`QSpinBox→QAbstractSpinBox`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（18/18）
- 功能缺口（空 stub 14 处）：14 个空 stub 全为遗留；valueFromText/textFromValue 虚槽已实现；decimals/displayIntegerBase 已实现。
- 违规/注意：—

### 3.42 XSplashScreen ↔ QSplashScreen（widgets/qsplashscreen.h）

- 继承：X：`XSplashScreen→XWidget`；Qt：`QSplashScreen→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（7/7）
- 功能缺口（空 stub 0 处）：无空 stub；pixmap/消息/finish 已实现。
- 违规/注意：—

### 3.43 XSplitter ↔ QSplitter（widgets/qsplitter.h）

- 继承：X：`XSplitter→XFrame`；Qt：`QSplitter→QFrame`；判定：**一比一 ✓**。
- API 缺口（3 项）：handle(int)/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 9 处）：9 个空 stub 全为遗留；restoreState/setSizes/橡皮筋已实现；insertWidget 顺序近似（XSplitter.c:346）；getRange/closestLegalPosition 为遗留空实现。
- 违规/注意：—

### 3.44 XStackedWidget ↔ QStackedWidget（widgets/qstackedwidget.h）

- 继承：X：`XStackedWidget→XFrame`；Qt：`QStackedWidget→QFrame`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（10/10）
- 功能缺口（空 stub 0 处）：无空 stub；currentChanged/widgetRemoved 信号参数完整。
- 违规/注意：—

### 3.45 XStatusBar ↔ QStatusBar（widgets/qstatusbar.h）

- 继承：X：`XStatusBar→XWidget`；Qt：`QStatusBar→QWidget`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（10/10）
- 功能缺口（空 stub 8 处）：8 个空 stub 全为遗留；showMessage 定时器已实现。
- 违规/注意：—

### 3.46 XTabBar ↔ QTabBar（widgets/qtabbar.h）

- 继承：X：`XTabBar→XWidget`；Qt：`QTabBar→QWidget`；判定：**一比一 ✓**。
- API 缺口（6 项）：setTabVisible()/isTabVisible()/tabData()/setTabData()/tabIcon()/setAccessibleTabName()/sizeHint()/minimumSizeHint()
- 功能缺口（空 stub 30 处）：30 个空 stub 中 19 个为真实 API（setDocumentMode/setElideMode/setExpanding/setUsesScrollButtons/setTabButton/tabButton/setTabTextColor/setTabToolTip/setTabWhatsThis/setTabIcon/drawBase/setDrawBase/tabRect/tabWidth/tabHeight/tabAt/tabIndexAt/isTabVisible/isEmpty/moveTab 等）；核心增删页/当前页/文本/使能/可关闭/可移动已实现；shape/iconSize 为遗留空实现。
- 违规/注意：真实 API 空实现约 19 处；m_titles 拥有型 char**（约束1）；4 个信号缺参数

### 3.47 XTableView ↔ QTableView（itemviews/qtableview.h）

- 继承：X：`XTableView→XAbstractItemView`；Qt：`QTableView→QAbstractItemView`；判定：**一比一 ✓**。
- API 缺口（35 项）：horizontalHeader()/verticalHeader()/setHorizontalHeader()/setVerticalHeader()/setModel()/setRootIndex()/setSelectionModel()/indexAt()/rowAt()/columnAt()/rowViewportPosition()/columnViewportPosition()/rowSpan()/columnSpan()/setSpan()/clearSpans()/isRowHidden()/setRowHidden()/hideRow()/showRow()/hideColumn()/showColumn()/resizeRowToContents()/resizeColumnToContents()/gridStyle()/setGridStyle()/wordWrap()/setWordWrap()/isCornerButtonEnabled()/scrollTo()/doItemsLayout() 等（共 35）
- 功能缺口（空 stub 0 处）：仅实现列宽/行高（统一行高）/网格开关/排序开关/sortByColumn/selectRow/selectColumn；无表头控件、无模型、无行/列隐藏、无 span、无尺寸自适应；QAbstractItemView 7 个信号缺。
- 违规/注意：信号 7 个全缺；`#if XTABLEWIDGET_ON || 1` 恒开

### 3.48 XTableWidget ↔ QTableWidget（itemviews/qtablewidget.h）

- 继承：X：`XTableWidget→XTableView`；Qt：`QTableWidget→QTableView`；判定：**一比一 ✓**。
- API 缺口（33 项）：setItem()/item()/takeItem()/clear()/setCurrentItem()/currentItem()/itemAt()/itemFromIndex()/indexFromItem()/findItems()/sortItems()/setSortingEnabled()/isSortingEnabled()/setCellWidget()/cellWidget()/removeCellWidget()/setItemPrototype()/itemPrototype()/items()/selectedItems()/selectedRanges()/setRangeSelected()/visualItemRect()/visualRow()/visualColumn()/row()/column()/openPersistentEditor()/closePersistentEditor()/setHorizontalHeaderItem()/takeHorizontalHeaderItem()/setVerticalHeaderItem()/takeVerticalHeaderItem()（共 33）
- 功能缺口（空 stub 16 处）：16 个空 stub（含 item 相关真实 API 部分为空）；实现为行×列 XTableWidgetItem 内嵌数组 + 文本/前景/背景/勾选态子集；15 个 cell/item 信号齐全但基类 7 个信号缺失；无 item widget、无持久编辑器、无选中范围。
- 违规/注意：基类信号 7 个缺失；真实 API 空实现多处

### 3.49 XTabWidget ↔ QTabWidget（widgets/qtabwidget.h）

- 继承：X：`XTabWidget→XWidget`；Qt：`QTabWidget→QWidget`；判定：**一比一 ✓**。
- API 缺口（12 项）：setCurrentWidget()/setTabIcon()/tabIcon()/setTabToolTip()/tabToolTip()/setTabWhatsThis()/tabWhatsThis()/setTabVisible()/isTabVisible()/setTabBarAutoHide()/tabBarAutoHide()/sizeHint()/minimumSizeHint()（heightForWidth 为保护虚函数）
- 功能缺口（空 stub 5 处）：5 个空 stub（遗留）；tabBar 复用 XTabBar 并转发 currentChanged/tabBarClicked；currentChanged 信号缺 int 参数；多出非 Qt 的 tabClicked。
- 违规/注意：缺 BOM；currentChanged 缺 int

### 3.50 XTextBrowser ↔ QTextBrowser（widgets/qtextbrowser.h）

- 继承：X：`XTextBrowser→XTextEdit`；Qt：`QTextBrowser→QTextEdit`；判定：**一比一 ✓**。
- API 缺口（3 项）：historyTitle(int)/historyUrl(int)/setSearchPaths()/searchPaths()（sourceType 为保护虚函数）
- 功能缺口（空 stub 12 处）：12 个空 stub（含 setSource_3/doSetSource 等真实路径 8 个为空）；anchorClicked 缺 QUrl 参数；highlighted 非 Qt 信号（Qt 为 highlightRequested）；backward/forward/home/reload 已实现。
- 违规/注意：anchorClicked 缺 QUrl；多出 highlighted

### 3.51 XTextEdit ↔ QTextEdit（widgets/qtextedit.h）

- 继承：X：`XTextEdit→XAbstractScrollArea`；Qt：`QTextEdit→QAbstractScrollArea`；判定：**一比一 ✓**。
- API 缺口（19 项）：document()/setDocument()/documentTitle()/setDocumentTitle()/textCursor()/setTextCursor()/currentCharFormat()/setCurrentCharFormat()/mergeCurrentCharFormat()/cursorRect()/extraSelections()/setExtraSelections()/insertPlainText()/setPlainText()/currentFont()/setCurrentFont()/fontItalic()/setFontItalic()/fontUnderline()/setFontUnderline()/lineWrapColumnOrWidth()/setLineWrapColumnOrWidth()/scrollToAnchor()/anchorAt()/redo()（loadResource 为保护虚函数）
- 功能缺口（空 stub 22 处）：22 个空 stub 中 17 个为真实 API（setAcceptRichText/setFontFamily/setFontWeight/setFontPointSize/setCurrentFont/zoomIn/zoomOut/setTabStopDistance/setAutoFormatting/setTabChangesFocus/setDocumentTitle/setLineWrapMode/setWordWrapMode/setCursorWidth/find/print 等，getter 返回常量）；currentCharFormatChanged 信号缺失；基础文本/富文本显示与编辑已实现。
- 违规/注意：currentCharFormatChanged 缺失；真实 API 空实现 17 处

### 3.52 XToolBar ↔ QToolBar（widgets/qtoolbar.h）

- 继承：X：`XToolBar→XWidget`；Qt：`QToolBar→QWidget`；判定：**一比一 ✓**。
- API 缺口（6 项）：insertSeparator()/insertWidget()/widgetForAction()/actionAt()/actionGeometry()/toggleViewAction()
- 功能缺口（空 stub 9 处）：9 个空 stub（遗留为主，含 insertSeparator_2 等）；movable/floatable/orientation/allowedAreas/iconSize/toolButtonStyle 已实现；iconSizeChanged 信号缺 QSize。
- 违规/注意：—

### 3.53 XToolBox ↔ QToolBox（widgets/qtoolbox.h）

- 继承：X：`XToolBox→XFrame`；Qt：`QToolBox→QFrame`；判定：**一比一 ✓**。
- API 缺口（1 项）：itemIcon(int)（X 有 setItemIcon(int,const char*) 空实现）
- 功能缺口（空 stub 3 处）：3 个空 stub（setItemIcon 族真实 API 为空）；页项/索引/文本/使能已实现。
- 违规/注意：—

### 3.54 XToolButton ↔ QToolButton（widgets/qtoolbutton.h）

- 继承：X：`XToolButton→XAbstractButton`；Qt：`QToolButton→QAbstractButton`；判定：**一比一 ✓**。
- API 缺口（0 项）：—（11/11）
- 功能缺口（空 stub 6 处）：6 个空 stub 全为遗留；defaultAction 镜像、arrowType/popupMode/sizeHint 已实现。
- 违规/注意：—

### 3.55 XWizard ↔ QWizard（dialogs/qwizard.h）

- 继承：X：`XWizard→XDialog`；Qt：`QWizard→QDialog`；判定：**一比一 ✓**。
- API 缺口（5 项）：setCurrentId(int)/currentId()（X 用 currentIndex 命名）/pageIds()/visitedIds()（X 仅 visitedIds_count）/button(WizardButton)/sizeHint()
- 功能缺口（空 stub 18 处）：18 个空 stub 中 9 个为真实 API（setPixmap/pixmap/setField/field/setSideWidget/sideWidget/setButtonLayout/setTitleFormat/setSubTitleFormat/cleanupPage/initializePage 空实现）；页面数组/next/back/restart/按钮/信号已实现；customButtonClicked 缺 which 参数。
- 违规/注意：命名不一致（currentId→currentIndex）；真实 API 空实现 9 处

### 3.56 XWizardPage ↔ QWizardPage（dialogs/qwizard.h）

- 继承：X：`XWizardPage→XWidget`；Qt：`QWizardPage→QWidget`；判定：**一比一 ✓**。
- API 缺口（12 项）：nextId()/validatePage()/cleanupPage()/initializePage()/setFinalPage()/isFinalPage()/setCommitPage()/isCommitPage()/setButtonText()/buttonText()/setPixmap()/pixmap()
- 功能缺口（空 stub 0 处）：仅标题/副标题/complete；completeChanged 信号缺失；setComplete 已实现。
- 违规/注意：completeChanged 信号缺失

### 3.57 XTextDocument ↔ QTextDocument（src/gui/text/qtextdocument.h）

- 继承：X：`XTextDocument→XObject`；Qt：`QTextDocument→QObject`；判定：**一比一 ✓**。
- API 缺口（65 项）：begin()/end()/firstBlock()/lastBlock()/findBlock()/find()/characterAt()/documentLayout()/setDocumentLayout()/pageSize()/setPageSize()/pageCount()/lineCount()/idealWidth()/textWidth()/setTextWidth()/defaultFont()/setDefaultFont()/documentMargin()/setDocumentMargin()/indentWidth()/setIndentWidth()/baseUrl()/setBaseUrl()/defaultStyleSheet()/availableUndoSteps()/clearUndoRedoStacks()/clone()/drawContents()/setModified()/isModified()/size()/objectForFormat()/addResource() 等（共 65）
- 功能缺口（空 stub 6 处）：实现为块/片段子集（setPlainText/toPlainText/setHtml/toHtml/block/append/片段格式/undo-redo 开关）；无 QTextCursor 对象模型、无 documentLayout、无 pageSize 分页、无 find、无资源；7 个 Qt 信号缺失。
- 违规/注意：信号 7 个缺失；API 覆盖约 19%（65/80 缺）

### 3.58 XPerformanceOverlay ↔ （X 专有）（—）

- 继承：X：`XPerformanceOverlay→XLabel`；Qt：`—`；判定：**不完全（见 2.1）**。
- API 缺口（0 项）：X 专有（性能悬浮层），无 Qt 对应类
- 功能缺口（空 stub 0 处）：FPS/帧耗时/网络统计绘制完整（6 个返回 false 为正常逻辑分支，非占位）。
- 违规/注意：—

## 四、Qt 对应范围内 XGui 缺失的类（missingQtClasses）

| Qt 类 | Qt 头文件（qtbase/src/widgets 下相对路径） | 建议 |
|---|---|---|
| QAbstractItemDelegate | `itemviews/qabstractitemdelegate.h` | 建议实现：item view 委托基础，先于具体视图补齐 |
| QStyledItemDelegate | `itemviews/qstyleditemdelegate.h` | 建议实现：默认样式委托（编辑控件工厂） |
| QItemDelegate | `itemviews/qitemdelegate.h` | 可选：与 QStyledItemDelegate 二选一即可 |
| QListView | `itemviews/qlistview.h` | 建议实现：列表视图（嵌入式列表常用） |
| QTreeView | `itemviews/qtreeview.h` | 建议实现：树视图 |
| QListWidget | `itemviews/qlistwidget.h` | 建议实现：列表控件（若做 ListWidget 应用） |
| QTreeWidget | `itemviews/qtreewidget.h` | 建议实现：树控件 |
| QHeaderView | `itemviews/qheaderview.h` | 建议实现：表格表头交互核心（XTableWidget 目前固定表头，排序/拖动/尺寸交互缺失） |
| QColumnView | `itemviews/qcolumnview.h` | 不实现（低优先级） |
| QDataWidgetMapper | `itemviews/qdatawidgetmapper.h` | 不实现（依赖 model 架构） |
| QItemEditorFactory | `itemviews/qitemeditorfactory.h` | 不实现（可随 QStyledItemDelegate 一并裁剪） |
| QFileIconProvider | `itemviews/qfileiconprovider.h` | 不实现（依赖 QFileInfo/QIcon 体系） |
| QTreeWidgetItemIterator | `itemviews/qtreewidgetitemiterator.h` | 不实现 |
| QTableWidgetSelectionRange | `itemviews/qtablewidget.h` | 建议实现：selectedRanges/setRangeSelected 的载体（XTableWidget 缺失） |
| QToolTip | `kernel/qtooltip.h` | 建议实现：工具提示系统（XWidget_setToolTip 已存文本但无弹层） |
| QShortcut | `kernel/qshortcut.h` | 建议实现：XAbstractButton/QAbstractSlider 的 shortcut 缺口直接依赖它 |
| QActionGroup | `kernel/qactiongroup.h` | 建议实现：按钮/动作互斥组（XPushButton 目前用父控件兄弟遍历近似） |
| QWhatsThis | `kernel/qwhatsthis.h` | 不实现（低优先级） |
| QWidgetAction | `kernel/qwidgetaction.h` | 不实现（低优先级） |
| QRhiWidget | `kernel/qrhiwidget.h` | 不实现（XGui 有独立 GPU 后端） |
| QCompleter | `util/qcompleter.h` | 建议实现：XLineEdit/XComboBox 的 completer 缺口依赖它 |
| QSystemTrayIcon | `util/qsystemtrayicon.h` | 不实现（嵌入式无托盘） |
| QScroller | `util/qscroller.h` | 不实现（低优先级） |
| QScrollerProperties | `util/qscrollerproperties.h` | 不实现 |
| QUndoStack | `util/qundostack.h` | 低优先（XTextEdit 用内部 undo 栈） |
| QUndoGroup | `util/qundogroup.h` | 不实现 |
| QUndoView | `util/qundoview.h` | 不实现 |
| QGraphicsEffect | `effects/qgraphicseffect.h` | 建议实现：XWidget_setGraphicsEffect 目前为 void* 占位（XWidget.h:1548） |
| QFileDialog | `dialogs/qfiledialog.h` | 建议实现：嵌入式文件选择常用 |
| QColorDialog | `dialogs/qcolordialog.h` | 建议实现：颜色选择常用 |
| QFontDialog | `dialogs/qfontdialog.h` | 可选（嵌入式字体选择不常用） |
| QInputDialog | `dialogs/qinputdialog.h` | 建议实现：文本/整数/下拉输入对话框 |
| QProgressDialog | `dialogs/qprogressdialog.h` | 建议实现：进度对话框（可基于 XProgressBar 组装） |
| QFileSystemModel | `dialogs/qfilesystemmodel.h` | 低优先（依赖 model 架构） |
| QPrintDialog | `qtprintsupport（不在 qtbase widgets 范围内）` | 不实现/另行评估（本审计范围外） |

已覆盖说明：QMdiSubWindow（XMdiSubWindow 内嵌于 XMdiArea.h，功能子集）、QCalendarWidget（XCalendarWidget 已有）、QAction（Src/XCode/XAction 已有）、QApplication/QGuiApplication（Application 模块已有）、QLayout/QBoxLayout/QFormLayout/QGridLayout/QStackedLayout/QSizePolicy（Layout 模块与 XWidgetSizePolicy 已有）；QXWidget 为 Qt 3 遗留类，Qt 6 不存在，无需实现。

## 五、优先任务建议（按优先级）

1. P0-1：删除全部头文件守卫外遗留声明区与对应空实现（约 380 处，33 个文件），恢复模块开关/`extern "C"` 完整性；先全局 grep 确认无调用（重点 XWidget.c/XComboBox.c/XMainWindow.c/XAbstractSpinBox.c）。
2. P0-2：补齐 4 个公共头 UTF-8 BOM（XDialogButtonBox.h/XMainWindow.h/XMenu.h/XTabWidget.h）。
3. P0-3：逐类落地“真实 Qt API 空实现”（约束3）：优先 XMessageBox（detailed/informative/默认与转义按钮）、XTabBar（外观/矩形/图标 19 处）、XTextEdit/XPlainTextEdit（字体/缩放/光标/换行 30 余处）、XMainWindow（dock/tab 布局 12 处）、XCalendarWidget（周数/格式/导航 13 处）、XWizard（pixmap/field/按钮布局 9 处）、XMdiArea（背景/页签 10 处）、XComboBox（findData/itemIcon/itemData/completer 5 处）。
4. P1-1：修复拥有型字符串违规：XComboBox.m_items、XTabBar.m_titles 改 XString*（约束1），同步 Test/ 调用点与 `_2` 重载。
5. P1-2：补齐信号与信号参数（约束5）：XAbstractItemView 7 信号（随 item view 重构）；XComboBox 7 信号参数；XAbstractSlider/XTabBar/XTabWidget/XProgressBar/XMainWindow/XToolBar/XFontComboBox/XDockWidget/XTextBrowser/XWizard 信号参数；XMessageBox::buttonClicked、XTextEdit::currentCharFormatChanged、XTextDocument 7 信号、XWizardPage::completeChanged、XMdiSubWindow 2 信号。
6. P1-3：item view 家族架构决策：选 A（引入 XAbstractItemModel+XHeaderView+delegate 完整 M/V 架构）或选 B（文档声明“固定网格子集”，删除 model/selectionModel 等 90 个空头声明）；若选 A，按 QAbstractItemView→QTableView→XTableWidget 顺序补 model/selection/delegate/scrollTo，再新增 QListView/QTreeView/QHeaderView。
7. P1-4：文本体系：先补 XTextDocument 的 QTextCursor 语义（cursorPositionChanged/undoAvailable/redoAvailable 信号、find/characterAt/documentLayout），再让 XTextEdit/XPlainTextEdit 的 document()/setDocument()/currentCharFormat 落到真实对象上，删除“getter 返回常量”类空实现。
8. P2-1：命名对齐：XWizard currentIndex/startIndex → currentId/startId（或文档声明为索引变体）；XFontComboBox currentFamily → currentFont 语义。
9. P2-2：修复 `#if XTABLEWIDGET_ON || 1` 恒开（XAbstractItemView.h/XTableView.h），恢复裁剪。
10. P2-3：补齐低优先缺失类：QToolTip/QShortcut/QActionGroup/QCompleter/QFileDialog/QColorDialog/QInputDialog/QProgressDialog/QGraphicsEffect；QListView/QTreeView/QListWidget/QTreeWidget 视 item view 决策而定。
11. P2-4：为 XWidget 缺 QPaintDevice 分支、QIcon→路径字符串、setIconSize(int) 等已知偏差补文档声明（未注明的补上）。

---
附注：本报告所有行号均为审计当日快照；stub 统计口径为函数体 `{ (void)self; }`/`{ (void)self; (void)x; }`；API 缺口数为名称级（含部分可由继承覆盖项已在逐类小节说明）。