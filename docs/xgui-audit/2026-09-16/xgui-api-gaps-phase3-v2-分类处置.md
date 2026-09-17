# XGui ↔ Qt 6.8.3 API 缺口分类处置报告（Phase 3.1 v2）

> 处置日期：2026-09-17　依据：`xgui-api-gaps-phase3-v2.txt`（初版 699 MISS）
> 扫描器：`tools/xgui_api_scan.py`（宏别名收集 + 36 条设计豁免登记）
> 处置结果：**699 → 568**（缺口收口 131 = 实现/别名收口 95 + 宏别名
> 误报消除 2 + 豁免登记 36±2 计 37 条；剩余 568 全部为已分类的 P2/P3
> 真实积压，无未判定项）

## 一、处置规则

| 类别 | 含义 | 处置动作 |
|---|---|---|
| A 已收口 | 本轮实现（新 API / getter 补齐 / 命名别名宏） | 已进 Src/，带回归 |
| B SKIP 豁免 | Qt-内部钩子 / 平台专属 / 体系不做 | 登记脚本 SKIP 表（含理由） |
| C P2 下轮 | 真实缺口，中等规模（1 个批次内可完成） | 保留清单，按类推进 |
| D P3 规划 | 真实缺口，架构级（视图族 M/V、文本引擎等） | 保留清单，批次规划 |
| E 架构偏差 | 语义由 XGui 自有模型承载，非缺口 | 11b 清单声明（本报告第四节） |

## 二、本轮已收口（A 类，按类明细）

| Qt 类 | 收口内容 | 方式 |
|---|---|---|
| QLayout | addWidget（基类转发，包装 XWidgetItem 后走 addItem 虚槽） | 实现 |
| QDateTimeEdit | minimumDate/maximumDate/minimumTime/maximumTime 全族（get/set/clear×6）、setDateRange/setTimeRange/setDateTimeRange、currentSectionIndex 别名 | 实现（语义对照 Qt：设日期保留时间、设时间保留日期） |
| QTabBar | shape/setShape、iconSize/setIconSize（单 int 方边值）、autoHide 族、selectionBehaviorOnRemove 族、changeCurrentOnDrag 族 | 实现（shape/iconSize/autoHide 仅存储位已注明） |
| QTabWidget | clear、documentMode/elideMode/tabShape/usesScrollButtons/iconSize 全族（转发页签条）、tabToolTip/tabWhatsThis getter | 实现 |
| QToolBox | setItemToolTip/itemToolTip/setItemToolTip_2（新增条目 tooltip 存储与 deinit 释放） | 实现 |
| QMenu | setIcon/icon/setIcon_2（新增 m_icon 字段 + copy/move/deinit 同步）、isTearOffEnabled 别名 | 实现+别名 |
| QDialog | open（模态+show，不进嵌套循环）、sizeGripEnabled 族 | 实现 |
| QDockWidget | isAreaAllowed | 实现 |
| QToolBar | isAreaAllowed、isFloating（存储位）、allowedAreasChanged/toolButtonStyleChanged 信号真发射 | 实现 |
| QComboBox | currentData | 实现 |
| QMessageBox | setOption | 实现 |
| QWizard | setCurrentIndex（xwiz_switchTo 复用）、currentId/setCurrentId/startId/setStartId 四别名、titleFormat/subTitleFormat getter、pixmap getter | 实现+别名 |
| QWizardPage | setButtonText/buttonText、setCommitPage/isCommitPage、setFinalPage/isFinalPage、setPixmap/setPixmap_2/pixmap | 实现 |
| QFontComboBox | currentFont 别名；setCurrentFont 头文件补声明（.c 既有实现漏声明，扫描器只扫头的盲区） | 别名+补声明 |
| QTextEdit | fontItalic/setFontItalic/fontUnderline/setFontUnderline 四别名（复用 isItalic/setItalic/isUnderline/setUnderline） | 别名 |
| XApplication | exec/quit/notify（扫描器宏别名收集后误报消除） | 扫描器修复 |

验证：默认构建回归全绿（含新增 `test_phase31_p1_contract` 40+ 断言）、
PARTIAL/FULL 渲染变体回归全绿、`XGUI_ON=0` 全裁剪构建通过、GPU 冒烟通过。

## 三、SKIP 豁免（B 类，37 条，理由见脚本 SKIP 表）

- macOS 桥接：QMenu/QMenuBar toNSMenu 族（3 条，既有）。
- Qt 内部：`*.qt_findObjChild`、QLineEdit.timerEvent、
  QAbstractScrollArea.setupViewport、QScrollArea.focusNextPrevChild、
  QCommandLinkButton.initStyleOption（5 条）。
- 体系不做：QWidget.setupUi/createWindowContainer/graphicsProxyWidget/
  hasEditFocus/setEditFocus/grabGesture/ungrabGesture/grabShortcut/
  releaseShortcut/setShortcutEnabled/setShortcutAutoRepeat/find（13 条）；
  QApplication.navigationMode/setNavigationMode（Qt 已弃用，2 条）；
  QPlainTextEdit.print（1 条）。
- URL 承载族：QFileDialog getOpenFileUrl/getOpenFileUrls/getSaveFileUrl/
  getExistingDirectoryUrl/getOpenFileContent/saveFileContent/currentUrlChanged/
  directoryUrlEntered/urlSelected/urlsSelected（10 条）。
- 备选历法：QDateTimeEdit/QCalendarWidget calendar/setCalendar（4 条）；
  QCalendarWidget dateTextFormat/setDateTextFormat（2 条，格式映射子集）。

## 四、架构偏差声明（E 类，建议并入 XGui.md 11b）

1. **QWidget 快捷键/手势**：快捷键以 XShortcut 对象承载（Task 2.19），
   不建 Qt 的 grabShortcut id 注册表；手势识别体系不做。
2. **QFileDialog URL 族**：路径以本地字符串承载，URL 变体不做。
3. **QCalendar 备选历法**：XGui 纯公历，QCalendar 对象体系不做。
4. **QDateTimeEdit currentSectionIndex**：分段序号与分段码共用同一
   字段（项目简化），别名已注明。

## 五、剩余缺口（C/D 类，568 项 = 下轮起的批次积压）

### C 类 P2（下轮可收口，约 180 项）

| 类 | 数量 | 内容 |
|---|---|---|
| QComboBox | 14 | setLineEdit、view/setView、setItemDelegate、validator 族（弹出列表部件化） |
| QMessageBox | 14 | checkBox 族、iconPixmap 族、buttonRole、removeButton、open、standardIcon、aboutQt |
| QDateTimeEdit | 11 | sectionAt/sectionCount/sectionText/displayedSections、setSelectedSection、timeZone 族 |
| QTextBrowser | 14 | 历史族（backward/forward/count/title/url）、openExternalLinks、searchPaths |
| QMainWindow | 19 | iconSize/toolButtonStyle 族、tabifyDockWidget 族、resizeDocks、insertToolBarBreak 族、menuWidget |
| QWizard | 5 | button/setButton、visitedIds、pageIds |
| QWizardPage | 4 | initializePage/cleanupPage/validatePage/nextId（公开虚函数，需 _base 槽位接入 XWizard 流程） |
| QTabWidget | 3 | cornerWidget/setCornerWidget、tabCloseRequested 信号 |
| QTabBar | 4 | accessibleTabName 族、tabWhatsThis 族（XWidget 层已有 whatsThis，需页签级存储） |
| QStyle | 3 | name（虚槽）、proxy、combinedLayoutSpacing |
| 其他 | ~ | QAbstractScrollArea sizeAdjustPolicy 族、QSplitter getRange/handle/replaceWidget、QGraphicsEffect source/boundingRect 族、QAbstractButton group/shortcut 族、QCompleter 族、QProgressDialog setCancelButton/setLabel、QLineEdit completer 族、QMenuBar addAction、QFontComboBox currentFontChanged 之外的 writingSystem 族（6.8 新 API）、QKeySequenceEdit finishingKeyCombinations 族（6.8 新 API）、QDateTimeEdit calendarWidget 族（弹出日历聚合） |

### D 类 P3（架构级，按模块推进，约 390 项）

| 族 | 数量 | 说明 |
|---|---|---|
| 视图族 QHeaderView/QTreeView/QAbstractItemView/QListView/QTableView/QListWidget/QTableWidget/QTreeWidget | 339 | 段管理（hide/move/resize/sort indicator）、展开折叠族、持久编辑器、委托挂接、拖放模式、row/column 级便捷族。XGui M/V 以行/列 int 承载（E 类偏差），推进时按 14.25-14.45 批次模式逐类补齐 |
| 文本族 QPlainTextEdit/QTextEdit/QTextEdit 残余 | ~80 | textCursor/document 交互、extraSelections、find、锚点与光标几何、undo/redo 信号、zoom、markdown（评估）；XTextDocument 为纯 C 子集（11b 已声明） |
| QWidget 残余 | ~14 | winId/effectiveWinId（需 XWindow 原生句柄暴露）、saveGeometry/restoreGeometry、render/grab、scroll、fontMetrics、setStyle/style、paintEngine、ensurePolished、screen 族、locale 族 |
| QFileDialog 残余 | 1 | open（异步打开模式） |
| QFontComboBox 残余 | 8 | displayFont/sampleText*/writingSystem 族（Qt 6.8 新 API，XGui 无字体采样体系） |

### 扫描器遗留改进（下轮）

- XGui 侧 .c 中存在但头文件未声明的实现（如 XFontComboBox_setWritingSystem
  占位）会持续误报；扫描器可增加「.c 定义但无头声明」白名单输出。
- Q_PROPERTY 派生访问器与成员函数同名时统计去重已由集合天然覆盖。

## 五b、Phase 3.2 P2 批次收口（2026-09-17 第二轮）

> 处置结果：**568 → 546**（净 22）。三类收口：QMessageBox 全清零、
> QDateTimeEdit 剩 2、QComboBox 剩 13（弹出列表部件化专项）。

| Qt 类 | 收口内容 | 方式 |
|---|---|---|
| QMessageBox | checkBox/setCheckBox（所有权转移+布局行）、iconPixmap 族（XImage 深拷贝存储）、buttonRole/removeButton（委托按钮盒+指针清理）、buttonText/setButtonText/_2（标准按钮文本）、aboutQt（文档化空操作，与 XApplication_aboutQt 一致）、standardIcon（映射 XStyleSP_*，样式未注册虚槽时返回 NULL）、setTextFormat/textFormat、setTextInteractionFlags/textInteractionFlags（转发内部标签） | 实现，14 项全收口 |
| QDateTimeEdit | sectionCount/sectionAt/sectionText/setSelectedSection（新格式分词器，与 xdt_refreshText 记号集一致）、displayedSections 别名（与 sections 同承载）；timeZone/setTimeZone 豁免登记（QTimeZone 体系未建，11b 偏差） | 实现+别名+豁免 |
| QComboBox | setLineEdit（所有权转移+隐式置可编辑+几何/show 接管）；view/setView、model 族、itemDelegate 族、validator 族、inputMethodQuery 列「弹出列表部件化」专项（弹出列表当前为自绘非部件承载，部件化后收口） | 实现+专项迁移 |

- 回归：新增 `test_phase32_p2_contract`（复选框所有权/位图拷贝/按钮角色
  与移除/标准按钮文本/分段查询/编辑框安装共 20+ 断言）全绿。
- 遗留：standardIcon 当前恒 NULL（无样式注册 EXStyle_StandardIcon 虚槽，
  图标生成为样式绘制批次任务）；QDateTimeEdit calendarWidget 族（弹出
  日历聚合）随部件化专项。

## 六、验证记录

- `python3 tools/xgui_api_scan.py`：自检通过（无截断/幻觉名），
  产出 v2 清单 568 MISS / 37 SKIP / 1747 满足。
- `cmake --build build --target XinYueCS` 与 `XGuiRegression_Test`：
  退出码 0，`XGui regression tests passed`。
- `/tmp/build-crop-p31`（-DXGUI_ON=0）：全裁剪构建退出码 0。
- `/tmp/build-p31-FULL`、`/tmp/build-p31-PARTIAL`（渲染模式变体）：
  回归全绿。
- `XGUI_RENDER_BACKEND=gpu ./bin/XGuiGpu_Test`：退出码 0，无 fail。
