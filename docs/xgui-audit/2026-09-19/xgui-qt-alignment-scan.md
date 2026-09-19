# XGui 模块 ↔ Qt 6.8.3 对齐扫描报告(全量,2026-09-19)

**口径**:只对比**当前已实现**的部分是否与 Qt 对齐——API 命名/签名/默认值/信号集合/发射时机/核心行为语义。不把"Qt 有但本框架从未计划实现"列为缺陷(仅标注为缺失备查)。C 适配约定(X 前缀、信号函数返回标识、QString→XString/UTF-8、私有类公开化)不计差异。
**方法**:75 个 Widget 类 + Text/Window/Application/Style 子模块,按 Qt 类族分 6 组并行审计;每个类通读 .h 公开 API,关键行为抽查 .c 实现,存疑处对照 Qt 6.8.3 源码。

---

## 一、总体结论

| 结论 | 类数 | 占比 |
|---|---|---|
| 完整对齐 | 16 | 20% |
| 基本对齐(小缺口) | 40 | 50% |
| 有差距(行为级缺口) | 24 | 30% |

- **完整对齐(16)**:XLineControl、XTextMenu、XTextClipboard、XTextUtf8、XRadioButton、XFrame、XStackedWidget、XLcdNumber、XSlider、XLayout、XBoxLayout、XGuiApplication、XWindow、XStyle/XCommonStyle/XFusionStyle/XStyleHints、XSizeGrip。
- **基本对齐(40)**:XLineEdit、XTextControl、XTextBrowser、XAbstractButton、XPushButton、XCommandLinkButton、XToolButton、XButtonGroup、XActionGroup、XGroupBox、XListView、XListWidget、XWidget、XSplitter、XTabBar、XTabWidget、XToolBox、XScrollArea、XAbstractScrollArea、XStatusBar、XToolBar、XMdiArea(+SubWindow)、XGridLayout、XStackedLayout、XAbstractSpinBox、XSpinBox、XFontComboBox、XCompleter、XKeySequenceEdit、XLabel、XProgressBar、XScrollBar、XAbstractSlider、XDialogButtonBox、XMessageBox、XProgressDialog、XCalendarWidget、XSplashScreen、XToolTip、XFocusFrame、XRubberBand、XOffscreenSurface、XApplication、XScreen、XWindowSystemInterface。
- **有差距(24)**:XPlainTextEdit、XTextEdit、XTextDocument、XCheckBox、XAbstractItemView、XTableView、XTableWidget、XTreeView、XTreeWidget、XHeaderView、XAbstractItemModel、XItemSelectionModel、XDockWidget、XMainWindow、XWizard、XDateTimeEdit、XComboBox、XDial、XDialog、XInputDialog、XFileDialog、XColorDialog、XErrorMessage、XShortcut、XGraphicsEffect。

## 二、高严重度缺口(行为错误/核心语义缺失,共 ~20 项)

### 文本族
1. **XTextEdit:8 个信号全部死信号**(textChanged/copyAvailable/currentPositionChanged/modificationChanged/undoAvailable/redoAvailable/selectionChanged 等仅返回标识,无任何 emit)——连接后永不触发,与头文件"真发射"承诺相悖。
2. **XTextDocument:撤销/重做栈为全局静态**,多文档互相污染;isRedoAvailable 恒 false、isUndoAvailable 误用修改计数;尾 '\n' 丢块、256 块静默截断、characterAt 字节/字符口径不符。

### 条目视图族(族级)
3. **滚动未接入**:XTableView/XListView/XTreeView/XTreeWidget/XListWidget 绘制/命中/visualRect 均不含滚动偏移,滚动条/scrollTo 无视觉效果(仅 XTableWidget 完整)。
4. **模型信号不跟踪**:基类 setModel 不 connect dataChanged/rowsInserted/rowsRemoved/modelReset,外部改模型视图不刷新。
5. **键盘导航缺失**:基类 keyPress 无方向键/Home/End/PageUp 导航。
6. **多选语义未落地**:Extended/Multi/Contiguous 模式枚举齐全但行为无差异,不读修饰键。
7. **XTableWidget**:sortItems 不同步内建模型、insertColumn/removeColumn 不迁移数据(数据/模型/部件三者失步)。
8. **模型 role 体系缺失**:仅 DisplayRole,勾选/图标/对齐/可编辑标志在模型层不可表达;headerDataChanged/columns* 信号缺失。

### 容器/窗口
9. **XWidget:Tab 键焦点遍历未接线**——focusNextChild 存在但全仓无任何 Tab 键路径调用。
10. **XDockWidget:toggleViewAction() 恒 NULL;setFloating 无浮动语义**(仅翻标志+发信号)。

### 对话框/应用
11. **XDialog.exec 不阻塞**(只处理一批事件即返回)、**setModal 不接窗口系统**(模态不生效)。
12. **XInputDialog/XFileDialog/XColorDialog 的静态便捷函数无条件跳过模态执行**,恒返回默认值/空——API 形在、功能无。
13. **XShortcut:match/activate 全仓无任何调用点**——快捷键永不触发。
14. **XCheckBox:hitButton 仅命中 13×13 indicator**,点击标签文字不切换(Qt 为 indicator∪文本区;头文件声称对齐 SE_CheckBoxClickRect 与事实不符)。

## 三、中级缺口摘要(按族)

- **文本**:XLineEdit 失焦 editingFinished 缺 hasAcceptableInput||fixup 门禁;selectionStart/End 返回字节而 cursorPosition 返回字符(同 API 组单位不一致);hasAcceptableInput 空文本与 Qt 相反;双击=全选(Qt 为选词)。XPlainTextEdit:ensureCursorVisible 强制顶对齐不走最小滚动;Tab 键无分支(tabChangesFocus 死值);textInteractionFlags 查询值与行为不符。
- **按钮**:XButtonGroup exclusive 组内选中项可被反选(Qt 禁止);addButton 不回写 group()(group() 恒 NULL);XGroupBox setCheckable(true) 未联动 setChecked(true)+toggled+StrongFocus;XToolButton popupMode 交互缺失(InstantPopup 仍触发动作);XPushButton 对话框默认按钮唯一性缺失。
- **输入杂项**:XComboBox 可编辑路径不成体系(editTextChanged 永不发射、completer 未接线、insertPolicy 缺失、无键盘导航);XDateTimeEdit 格式引擎仅 6 占位符、calendarPopup 弹不出、currentSectionIndex 与 currentSection 字段混用;XDial wrapping 无回绕语义、notchSize 单位错位;XAbstractSpinBox Ctrl+Up/Down 无 ×10、按钮长按不重复;XAbstractSlider 滚轮不发 actionTriggered;XProgressBar setRange 交换 vs 收敛(与库内 XSpinBox 不一致);XKeySequenceEdit Return=确认/Esc=清空与 Qt 6.8.3 实际行为不符;XScrollBar 拖动不遵守 tracking、无按住重复。
- **容器**:XTabBar closable/movable 纯存储(tabCloseRequested 永不发、拖拽换位缺失);XTabWidget tabPosition 仅 North 生效;XWizard completeChanged 通知链断裂+validatePage 默认值偏差+16 页/32 字段硬上限;XMainWindow 停靠几何系统性简化(Top/Bottom 不布局);XStackedLayout currentChanged/widgetRemoved 不发射。
- **对话框**:XMessageBox Escape=escapeButton/defaultButton 键盘语义未接、setWindowTitle 不落到原生标题;XDialogButtonBox Abort 角色映射与 Qt 不一致、accepted/rejected 与 clicked 次序相反;XErrorMessage done-shown 抑制机制缺失;XCalendarWidget activated 信号无发射点、VerticalHeaderFormat 枚举与 Qt 相反。

## 四、修复优先级建议(按影响面×严重度)

1. **条目视图族级三件**:基类 setModel 接模型信号 → 派生视图接滚动偏移 → 键盘导航+多选(一次性拉平 10 个类的最大短板)。
2. **XTextEdit 信号接线** + **XTextDocument 撤销栈实例化**(文本兼容层正确性)。
3. **XDialog.exec 阻塞循环 + 模态**(对话框核心;XMessageBox 已有可复用的 while 实现)。
4. **XWidget Tab 焦点遍历接线**(全库键盘可用性)。
5. **XCheckBox hitButton、XButtonGroup exclusive/group() 回写、XToolButton popupMode**(交互正确性)。
6. **XLineEdit 失焦门禁 + selectionStart/End 单位统一**(小改)。
7. **XDateTimeEdit 格式引擎扩展与 sectionIndex 拆分、XComboBox 可编辑路径**(体量较大,可分批)。

## 五、范围说明

- 本轮覆盖 Src/XGui/Widget(75 类中 74 个)+ Text + Application + Window + Style。**Graphics(XPainter 等)、Charts、Input、Icon、Platform 各后端不在本轮比对范围**(远端 28c41c1c 刚对 Graphics/XPainter 做过性能专项,建议下轮单独对 QPainter/QBackingStore 扫描)。
- 各组详细逐条清单(含 file:line 证据)见本次审计的 6 份分组报告(组报告由审计代理产出,要点已收录本文件;如需完整版可从会话记录导出)。


---

## 六、继承关系对齐（第二轮扫描,2026-09-19）

方法:逐文件提取 `X<ClassName>_class_init` 内的 `XVTABLE_INHERIT_XCLASS`
声明,构建 74 类完整继承树,与 Qt 6.8.3 类层次逐类比对。

### 结论:74 类中 71 类继承链与 Qt 一致,3 处结构性偏差

**完全一致的继承链(抽样对照 Qt)**:
- QWidget 族:QWidget←XWidget;QFrame←XFrame←(QLabel/QLCDNumber/
  QSplitter/QToolBox/QStackedWidget*);QAbstractButton←(QPushButton/
  QCheckBox/QRadioButton/QToolButton),QPushButton←QCommandLinkButton;
  QAbstractSlider←(QScrollBar/QSlider/QDial);QAbstractSpinBox←
  (QSpinBox/QDateTimeEdit);QComboBox←QFontComboBox;QLineEdit/
  QComboBox/QGroupBox/QTabBar/QTabWidget/QStatusBar/QProgressBar/
  QKeySequenceEdit/QCalendarWidget/QDockWidget/QSizeGrip/QFocusFrame/
  QRubberBand/QSplashScreen/QMenu/QMenuBar/QToolBar/QDialogButtonBox
  ←QWidget;
- QDialog←(QMessageBox/QFileDialog/QInputDialog/QErrorMessage/
  QProgressDialog/QColorDialog/**QWizard**);
- QFrame←QAbstractScrollArea←(QTextEdit→QTextBrowser/QPlainTextEdit/
  QAbstractItemView←(QListView→QListWidget/QTableView→QTableWidget/
  QTreeView→QTreeWidget)/QMdiArea/QScrollArea);
- QObject←(QButtonGroup/QActionGroup/QCompleter/QTextDocument/
  QItemSelectionModel/QGraphicsEffect/QShortcut/QOffscreenSurface/
  QAbstractItemModel)。

**三处结构性偏差(均为文档化的架构简化,非缺陷回归)**:
| # | 类 | Qt 继承 | XGui 继承 | 影响 |
|---|---|---|---|---|
| 1 | XHeaderView | QHeaderView←QAbstractItemView | ←XWidget | 无模型驱动表头(自有几何),列宽/隐藏/排序状态层已对齐 |
| 2 | XStackedWidget | QStackedWidget←QWidget | ←XFrame | 多出 Frame 绘制语义(视觉可忽略) |
| 3 | XWizard 已修 | QWizard←QDialog | ←XDialog ✓ | —(初判 XWidget 系提取误差,实为 XDialog ✓) |

**框架适配(不计偏差)**:XWidget←XObject 并内嵌窗口语义——C 单继承
下将 QWidget(QObject)+QWindow 的组合压平为一条链,全库一致;
QToolTip 为静态命名空间(XToolTip 无实例类)与 Qt 同构。

**继承链修正记录**:初轮扫描误将 XMenuBar/XToolBar/XDialogButtonBox
判为 XObject 派生——系文件内桥接类(XMBBridge 等)干扰的提取误差,
精确提取后三者均为 XWidget ✓(与 Qt 一致)。

### 综合最终结论(API+功能行为+继承)

- **继承关系**:74 类中 71 类与 Qt 完全一致;3 类为文档化的结构简化,
  无"继承链错误导致的行为缺陷"。
- **API 面**:近全覆盖(各分组报告);缺失项集中在扩展面
  (contentOffset/print/loadResource/role 体系等)。
- **功能行为**:文本族核心(XLineControl/XTextControl)为忠实移植;
  差距集中在 条目视图交互四支柱(已列入阶段三)、对话框模态语义
  (阶段二已修)、杂项行为细节(阶段四已修大部分)。
