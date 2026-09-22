# XGui Qt 6.8.3 二次全量对齐复扫 —— 修复队列

日期：2026-09-22 ｜ 分支：codex/xdevice-file-platform ｜ 仓库：/home/xinyue/Code/XinYueC

## 一、背景与方法

XGui 于 2026-09-20 前后完成首轮 Qt 6.8.3 全量对齐（见 `2026-09-19-21-qt-alignment-campaign.md` 及 XGui.md §8）。本次为**二次全量复扫**：9 个域并行走读（Graphics / Widget编辑器 / Widget条目视图 / Widget按钮菜单 / Widget对话框主窗口 / Style与Layout / Text与Input / Platform与Window / Icon与Charts），产出 115 条原始发现；再经**独立复核**逐条核实——静态逐行验证、/tmp 探针运行复现（用后已删）、Qt 6.8.3 源码（qtbase v6.8.3 抓取逐字对照）与文档对照、§8.0/§8.1/§8.2/§8.3 已声明边界与已修项排除——确认 113 条、驳回 2 条。

本文为汇总整理层：剔除已驳回项、跨域去重（113 条合并 2 组同根因 → **111 项**）、按 P0→P1→P2 排队列。判级一律取复核 finalSeverity（9 条经复核校准，见第五节）。运行复现均由复核阶段完成，本汇总未重复运行任何构建/探针/测试（纯只读整理，未改动任何代码文件）。

去重合并记录（2 组）：

1. **paintOffset 缺失**（条目视图域两笔同根因）：XTreeView.c:1012-1020 + XTableView.c:1052-1060 → 合并为 R-14。
2. **对话框默认按钮机制缺失**（按钮菜单域 + 对话框域两笔互补半边）：XPushButton.c:270 + XDialog.c:99 → 合并为 R-76。

## 二、覆盖统计

| 域 | 原始 | 确认 | 驳回 | 覆盖要点 |
|---|---|---|---|---|
| Graphics | 2 | 2 | 0 | 20 文件全文（渲染内核表五内核/图像缓存/ImageIOHandler/BMP/GIF 等）+ 大头文件定点：XPainter.c(13644 行混合填充/blit/字形三路径/clipPath/begin_device)、XImage.c、XPicture.c、XPixmap.c、XImageReader.c、Png/Xpm/Jpeg 解码关键段。仅 grep 未逐行：Svg/Ico/Ppm/Xbm 编解码全文、XBitmap/XMovie/XColorSpace/XGpu*/Writer/PluginRegistry/BuiltinPlugin。 |
| Widget编辑器 | 21 | 21 | 0 | 17 文件全文（Shortcut/ToolTip/ErrorMessage/KeySequenceEdit/Calendar/Completer/SpinBox 系/LineEdit/PlainTextEdit/TextEdit/TextBrowser/TextDocument/ComboBox/FontComboBox）+ XLineControl.c 关键段交叉验证 + 死信号全库 grep。未逐行读各控件 .h；XErrorMessage 无 _Protected.h。 |
| Widget条目视图 | 19 | 19 | 0 | 19 个 .c 全文（AbstractItemView/Model/SelectionModel/Delegate/ListView/ListWidget/TreeView/TreeWidget/TableView/TableWidget/HeaderView/ScrollArea 系/Slider 系/ProgressBar/RubberBand）+ AbstractItemView.h 全文 + paintOffset 覆盖矩阵 grep + XWidget.c 根修语义核对 + git diff 状态核实。其余 18 个 .h 未通读（API 声明层）。 |
| Widget按钮菜单 | 11 | 11 | 0 | 17 个 .c 全文（AbstractButton 1230 行全/CheckBox/PushButton/RadioButton/ToolButton/CommandLinkButton/ButtonGroup/ActionGroup/Frame/GroupBox/Label 3108 行全/LcdNumber/MenuBar 末尾缺约 4 行/Menu/ToolBar/ToolBox/StatusBar）+ AbstractButton.h。XWidget.c(6480 行) 仅语义区切片；各控件 .h 与 _Protected.h 未逐行读。三笔任务书点名新修复均在位验证。 |
| Widget对话框主窗口 | 18 | 18 | 0 | 域内全部 24 个 .c 全文（Dialog/DialogButtonBox/MessageBox/InputDialog/FileDialog/ColorDialog/ProgressDialog/Wizard/FocusFrame/SizeGrip/MainWindow/MdiArea/Dock/Splitter/Stacked/TabBar/TabWidget/Splash/Offscreen/PerfOverlay/GraphicsEffect 四件）+ 4 个 .h；XWidget.c/XEventLoop/XObject/XStringList 核实性片段。一处初判疑点（XTabWidget_removeTab 悬垂）经 VXObject_deinit 级联删子证据自行撤销，未计入。 |
| Style与Layout | 11 | 10 | 1 | 34/34 文件全文（Style 20 + Layout 14）+ XGui.md；交叉 grep：drawItemText/addWidget/standardPalette/StepModifier 全库消费方。本机无 Qt 源码树，Qt 语义取已知行为保守报告。 |
| Text与Input | 7 | 7 | 0 | Text/ 10 文件 + Input/ 10 文件全量（LineControl 4297 行、TextControl 5449 行全文）+ XLineEdit.c 绘制段、XTextDocument.c UTF-8 修复周边；Qt 对照经 WebFetch 抓 qtbase v6.8.3 qwidgetlinecontrol.cpp/qlineedit.cpp 原文核实。 |
| Platform与Window | 9 | 8 | 1 | 40/40 全文（Platform 23 + Window 9 + Application 4）+ XFont/XMap/XObject emit/XSignalSlot emit/XWidget nativeWindow/posix 窗口 FocusIn 段辅助核对；fbdev 四项修复与 cacheSync 契约通读未发现超出 §8.0c4 已登记状态的新偏差。win32 对应段仅 grep 定位。 |
| Icon与Charts | 17 | 17 | 0 | Charts 13 个 .c 逐行 + Icon 9 个 .c 逐行；.h 精读 XChart.h/XChartView.h/XValueAxis.h/XIcon.h 等，其余声明层快扫；行号锚点 grep 复核。未运行构建/ASan。 |
| **合计** | **115** | **113** | **2** | — |

## 三、修复队列（P0 → P1 → P2，共 111 项）

### P0（8 项）——内存安全 / 核心交互破坏

| 编号 | 严重度 | 位置 | 偏差 | Qt 对标 | 修复思路 | 规模 |
|---|---|---|---|---|---|---|
| R-01 | P0 | Src/XGui/Widget/XSpinBox.c:921（validate :286-302、textFromValue :257-276） | 内嵌编辑框数字校验器按整段显示文本判 Invalid，设置前缀/后缀/特殊值文本后任何键入被拒并回滚 | QSpinBoxPrivate::validateAndInterpret 先剥离前后缀再校验 | 校验器按前后缀边界剥离后仅验数值段 | 中 |
| R-02 | P0 | Src/XGui/Widget/XComboBox.c:1653（clear :1687 同病） | removeItem 删当前项之前条目时 m_currentIndex 不左移、clear 置 -1，均不发射信号、可编辑编辑框不同步，当前项静默指错 | removeItem/clear 经模型行删除，持久索引随行左移并发射 currentIndexChanged/currentTextChanged | 删除/清空后收敛索引并复用 setCurrentIndex 的发射+编辑框回填路径 | 中 |
| R-03 | P0 | Src/XGui/Widget/XCalendarWidget.c:272（绘制 :174-186、回退字符 :197-200） | 导航栏四按钮命中区与绘制按钮两两错位（◀上一年→上一月、<上一月→下一年、>下一月→上一年、▶下一年→下一月），点击即错误导航 | QCalendarWidget 导航条 ◀=上一年、<=上一月、>=下一月、▶=下一年 | 按绘制侧 xs[] 四区间重排命中映射并统一回退绘制字符序 | 小 |
| R-04 | P0 | Src/XGui/Widget/XTabWidget.c:518 | setTabEnabled 的 index 无界检即索引 m_clients[]，-1/越界读堆垃圾；removeTab 尾槽悬垂指针穿透非空判定经 XWidget_setEnabled 构成 UAF 写（indexOf 未命中返回 -1 即触发） | QTabWidget::setTabEnabled 对无效 index 无操作 | 比照 XTabBar_setTabEnabled(:1116-1121) 补 index 界检 | 小 |
| R-05 | P0 | Src/XGui/Widget/XTextDocument.c:621-637 | appendSpan 片段池满(fi==64)且尾片段为图片时漏钳位，&blk->fragments[64] 越界写覆写 XTDBlock 标量区并置 fragmentCount=65 级联野指针；经公开 API（setHtml 256 段→insertImage×64→appendHtml，256 块上限拒 advance）可达 | QTextFragment 片段表为堆容器动态增长，无定容池满分支 | 比照同文件 addFragment(:1287)/insertImage(:1361) 钳位样例，池满改丢弃/另起块 | 小 |
| R-06 | P0 | Src/XGui/Text/XTextControl.c:1153-1228 | undo 后任何新编辑不清空重做栈，redo() 按旧位置重放过期命令，Ctrl+Z→输入→Ctrl+Y 静默损坏文档内容 | QTextDocument/QUndoStack 单历史表+游标：新命令截断 redo 段 | xtc_recordCommand 入栈前清空重做栈 | 小 |
| R-07 | P0 | Src/XGui/Charts/XXYSeries.c:648（append :337-351、removeAt :416-418） | m_selected 选中数组不随 append 扩容，点数增长后 isPointSelected 越界读、setPointSelected 越界写（堆溢出），渲染路径逐点调用即触发 | QXYSeries 选中集合为动态容器，合法点序号即安全 | append/removeAt 同步扩缩 m_selected | 小 |
| R-08 | P0 | Src/XGui/Charts/XChart.c:711（setPieSeries :1472-1474、removeSeries :781-793） | 泛型 addSeries 对 Pie 的注册与 setPieSeries「删除旧饼图」语义冲突，旧饼图删除后泛型表残留悬垂指针，removeSeries/removeAllSeries 双重释放/UAF | addSeries/removeSeries 对全部序列统一接管所有权，remove 恰好释放一次 | setPieSeries 删旧饼图前先摘除泛型表登记（所有权统一收口注册表） | 中 |

### P1（32 项）——功能硬缺失 / 渲染正确性 / 泄漏与 UAF 风险

| 编号 | 严重度 | 位置 | 偏差 | Qt 对标 | 修复思路 | 规模 |
|---|---|---|---|---|---|---|
| R-09 | P1 | Src/XGui/Graphics/XPainter.c:7709-7720（复核校准 P0→P1） | italic+位图字形 AA 路径 x 扫描外扩为死代码：lastX 在 originX 未初始化时先读（UB 死存储）后被 :7720 无条件覆盖，顶部右侧墨迹被裁（scale=2 实测缺 4 列，随行高收敛） | Qt 合成斜体对整字形盒 shear，AA 覆盖按逆 shear 映射外扩扫描范围（§8.0g11 自述应生效） | 先赋 originX，lastX 统一按含 shear 外扩计算并以其钳位 | 中 |
| R-10 | P1 | Src/XGui/Graphics/XPainter.c:8698-8711（内核版 :8853-8864 同构） | 字形灰度混合两条快路径门槛不含 m_hasClipPath，非矩形 setClipPath 后文本只裁到路径包围盒（实测 110/201 像素越界落墨） | clipPath 对 drawText 按路径形状精确裁剪 | 两快路径门槛补 !painterClipPathActive（与 fillRect :2639/drawImage :3142 同款） | 小 |
| R-11 | P1 | Src/XGui/Widget/XTextDocument.c:1470（复核校准 P0→P1） | 撤销栈从不入栈（xtd_saveSnapshot 全库零调用点），undo/redo/isUndoAvailable 恒死；XTextEdit 走自有栈不受累，影响限于直接使用文档模型的消费方 | QTextDocument 每次编辑经 contentsChange 入撤销栈 | setPlainText 等变更路径接线快照入栈 | 中 |
| R-12 | P1 | Src/XGui/Widget/XDateTimeEdit.c:833 | 无 MousePress 覆载，点击分段不更新 m_currentSection，Up/Down 恒作用于上次分段（初始恒年段） | mousePressEvent 按光标位置选中分段 | 覆载 MousePressEvent 按 x 命中段边界换算 setCurrentSection | 中 |
| R-13 | P1 | Src/XGui/Widget/XFontComboBox.c:535 | currentFontChanged 仅程序化 setCurrentFamily 且族名命中时发射，用户弹层/键盘改选不发射 | currentFontChanged 任何当前字体变化（含用户选择）都发射 | init 连接基类 currentIndexChanged/activated 转发发射 | 小 |
| R-14 | P1 | Src/XGui/Widget/XTreeView.c:1012-1020 + Src/XGui/Widget/XTableView.c:1052-1060（合并 2 条，复核各校准 P0→P1） | 两控件 paintEvent 缺 XWidget_paintOffset 平移，非零偏移嵌入时全部内容直绘窗口 (0,0)；与 §8.0g12 已根修的 XListView/XTreeWidget 同款，派生类 XTableWidget.c:1398 已修而基类漏 | 子控件绘制须定位到控件自身位置 | 比照 XListView.c:317-321 补 translate(paintOffset) | 小 |
| R-15 | P1 | Src/XGui/Widget/XTreeView.c:967-983 | indexAt 不校验模型行数、不跳隐藏行，表尾空白点击返回越界行号并写当前项、以越界载荷发射 pressed/clicked/activated | indexAt 越界返回 invalid，pressed/clicked 仅对有效索引发射 | 比照 XListView.c:399/XTableView.c:877 补 rows 上界与隐藏行跳过 | 小 |
| R-16 | P1 | Src/XGui/Widget/XTreeWidget.c:1236-1293 | 表头 API（setHeaderLabels/setHeaderItem）只存储不渲染，paintEvent 不画表头带；demo apitest 实际调用该 API | QTreeWidget 默认 headerVisible，表头由 QHeaderView 渲染于视口顶部 | 挂接 XHeaderView 或 paintEvent 顶部按列宽画表头带 | 中 |
| R-17 | P1 | Src/XGui/Widget/XTreeWidget.c:519-536 | 键盘导航整体失效：条目存自有链表而基类导航以 m_model 为界（rows=cols=0 一律 ignore） | 方向键/翻页/Home/End 经 moveCursor 移动当前项，无需用户模型 | 覆载 KeyPressEvent 按自有存储实现当前项移动（或桥接内建模型） | 大 |
| R-18 | P1 | Src/XGui/Widget/XMenu.c:801（误发 triggered :66-76/:105-107/:174-183） | 弹出菜单中子菜单条目完全无法打开：点击即触发动作并关菜单，悬停只高亮，键盘无 Left/Right；点击子菜单条目还误发菜单 triggered(容器动作) | hover 延时/点击立即打开子菜单（不触发容器动作、不关父菜单），Right 打开/Left 关闭 | mouseMove/press/keyboard 对 addMenu 产物走子菜单弹出分支并抑制容器动作转发 | 大 |
| R-19 | P1 | Src/XGui/Widget/XToolBar.c:686（clear :693-707、deinit :277-292） | removeAction/clear/析构对 addAction(XAction*) 注入的借用动作无条件 XAction_delete_base，产生悬垂/双重释放可达路径（XMenuBar 已按借用语义实现可对照） | addAction(QAction*) 不取得所有权，removeAction/clear 只摘除不删除 | 引入 m_actionOwned 记账（比照 XMenuBar.c:553），借用动作仅摘除 | 中 |
| R-20 | P1 | Src/XGui/Widget/XToolButton.c:107（popupMode 死存储 :279-682、箭头 :556-568 仅绘制） | 带菜单按钮点击弹层不接线：popupMode 不消费、无箭头区命中，点击整钮一律触发动作，标准 Qt 用法下菜单永远打不开 | MenuButtonPopup 点箭头弹菜单且不触发动作、DelayedPopup 点击弹菜单 | 覆载 MousePress/Release 按 popupMode 与箭头区分流弹层与触发 | 中 |
| R-21 | P1 | Src/XGui/Widget/XMessageBox.c:273（复核校准 P0→P1） | setTitle 每次调用把 XString_create_utf8 堆结果传给深拷贝语义的 XWidget_setWindowTitle 后无人释放，逐调用泄漏（xmsg_runStatic 每便捷弹窗至少一次） | setWindowTitle 值语义持有不泄漏 | 改用 XWidget_setWindowTitle_2（XWidget.c:6431-6440 正确范式） | 小 |
| R-22 | P1 | Src/XGui/Widget/XMessageBox.c:341 | exec 全程未登记应用模态（从不调 XWidget_setApplicationModalWidget），模态循环期间其它控件仍可交互；与 XDialog_exec(:170-178) 不对称 | QMessageBox::exec 为应用模态，阻塞期间禁用其余窗口输入 | 比照 XDialog_exec 首轮+每轮重登记应用模态 | 小 |
| R-23 | P1 | Src/XGui/Widget/XMessageBox.c:341 | exec 模态循环忙等空转：processEvents 不带 WaitForMoreEvents，无事件时立即返回烧满 CPU | exec 嵌套事件循环，无事件时阻塞等待 | 标志补 XEventLoop_WaitForMoreEvents（XDialog.c:174 同款） | 小 |
| R-24 | P1 | Src/XGui/Widget/XMdiArea.c:207 | resizeEvent 无条件重铺全部子窗为 2 列网格，默认 SubWindowView 下用户摆位被摧毁；m_viewMode 死属性 | 仅 TiledView 随 resize 重排，SubWindowView 保留用户摆位 | resizeEvent 按 m_viewMode==TiledView 门禁 | 小 |
| R-25 | P1 | Src/XGui/Widget/XWizard.c:829 | addPage 挂载首页以 y=0 摆页与 xwiz_switchTo 的 y=bannerH 口径不一致，首屏页内容与横幅叠印（已修症状经此路径复发；m_currentIndex 初值致 restart 早退无自然自愈） | 首页内容区位于横幅之下 | addPage 复用 xwiz_layoutCurrentPage 统一口径 | 小 |
| R-26 | P1 | Src/XGui/Widget/XInputDialog.c:1028（交叉误发 :1024-1044） | *ValueSelected 三信号全库无发射点（accept 路径从不发）；且手动调 ValueSelected 信号函数连带真发射 *Changed，两独立信号交叉误发 | done(Accepted) 后发射 valueSelected 族；*Changed 仅实际值变化时发射 | accept 槽结算后发射对应 ValueSelected；信号 getter 解耦 *Changed 联动 | 中 |
| R-27 | P1 | Src/XGui/Widget/XSplitter.c:242 | 把手拖动交互整体缺失（press 仅 accept，无 Move/Release 覆载），splitterMoved 为死信号，QSplitter 主体功能不可用 | 拖动把手实时改页尺寸并发射 splitterMoved(pos,index) | 覆载 MouseMove/Release 实现拖拽分派与信号发射 | 大 |
| R-28 | P1 | Src/XGui/Style/XStyleSheetStyle.c:604-618（margin :628-665 同病且 n==4/n==2 映射错位 :659-663） | QSS padding/margin 多值简写被前缀解析吞成单值四边，2/4 值展开为死代码且映射上下/左右互换 | 盒模型简写支持 1/2/4 值（上 右 下 左）展开（ValueExtractor::lengthValues） | 补多值解析展开，margin 展开分支按 CSS 顺序修正映射 | 中 |
| R-29 | P1 | Src/XGui/Style/XStyleSheetStyle.c:344-353（回填 :375-379） | 渲染规则单槽缓存命中绕过 !important 级联：回填只比特异度不比 m_important，高特异度非 important 规则首绘后遮蔽低特异度 !important 同属性声明，两次绘制取值不一致（违反本文件 :320 自声明契约；勘误：Qt 实际不消费 !important） | 级联契约 !important > 特异度 > 后定义（文件 :320 自述） | 缓存命中判定补 importance 比较（或缓存键纳入 important 位） | 小 |
| R-30 | P1 | Src/XGui/Widget/XSpinBox.c:496-497 | setButtonSymbols(NoButtons=2) 在样式路径被折叠映射为 0 照画按钮列+箭头，公共 API 静默失效（样式侧 XCommonStyle.c:3493 本已支持 2；非样式回退 :512-542 同样画按钮） | CC_SpinBox 对 NoButtons 不画步进按钮、EditField 全宽 | m_spinSymbols 原值透传，回退路径同判 NoButtons | 小 |
| R-31 | P1 | Src/XGui/Text/XLineControl.c:3894-3910 | 掩码反选格宽度计算像素/字节混用：layoutCursorToX 返回的像素 X 被当字节偏移喂给 seqLenAt/textWidthRange，等宽字库光标过 len/8 即现 0/垃圾宽+潜在越界读 | 反选 FormatRange 为纯文本坐标，无像素值充当文本索引路径 | 像素→字节经 mapTextToLayout/字符边界映射后再取段宽 | 中 |
| R-32 | P1 | Src/XGui/Text/XLineControl.c:3894-3910 + Src/XGui/Widget/XLineEdit.c:1013-1024（与 R-31 同函数相邻，可一并修） | 光标处字符反选格未以掩码为门禁且与细光标并存（壳层恒传 Selections+Cursor 双旗标），普通行编辑 blink 亮相时反相格+细光标同屏 | DrawSelections 仅在有选区或（掩码+光标亮）；DrawCursor 仅在无掩码时，两者互斥 | 壳层按 m_maskData 分流双旗标，反选格与细光标互斥 | 中 |
| R-33 | P1 | Src/XGui/Text/XTextControl.c:2489-2541 + 2733-2747 | 中键粘贴双路径叠加：press 侧 PRIMARY 与 release 侧共享层对同一次中键各插一次，两份文本均入文档 | 中键粘贴仅 press 侧一处（Selection 模式） | 删除 release 侧遗留中键分支，保留 press 侧单路径 | 小 |
| R-34 | P1 | Src/XGui/Window/XWindowSystemInterface.c:92-101 | handleFocusWindowChanged 只向新窗口自发投递 FocusIn，从不更新 XGuiApplication 焦点窗口——focusWindow() 脱钩、focusWindowChanged 不发、输入上下文聚焦链不触发 | handleFocusWindowChanged→processFocusWindowChanged：设置 focusWindow、发信号、联动激活 | WSI 入口内接通 XGuiApplication_setFocusWindow 及变更信号 | 小 |
| R-35 | P1 | Src/XGui/Window/XWindow.c:2003-2019 | raise 置 m_active=true、lower 置 false，activeChanged 全库零生产发射，setFocusWindow 不联动新旧窗口 active——isActive 与真实焦点永久脱钩（A 激活后 B 激活，A 恒 true） | isActive=本窗口是 focusWindow 或其祖先；raise/lower 仅改 Z 序；activeChanged 随激活变化发射 | 激活切换收口 setFocusWindow，联动新旧 m_active 并发射 activeChanged | 中 |
| R-36 | P1 | Src/XGui/Platform/XPlatformNativeInterface.c:505-528 | windowPropertyChanged 以借用 XString* 直传且 del=NULL，违反头文件「新建 XString，由信号系统释放」契约；queued 连接下 setWindowProperty_2 的临时属性名 emit 返回即删，接收者 UAF | 信号系统持有参数副本；框架正确范式为 objectNameChanged_signal（拷贝+del） | emit 前拷贝属性名并挂专属 del 回调 | 小 |
| R-37 | P1 | Src/XGui/Charts/XChartView.c:1386（复核校准 P0→P1） | xcv_paintLegend 把 XFont_deinit_base 误放折线图例循环体内，m_lineCount==0（纯柱/饼）永不执行，每次重绘泄漏一份深拷贝字体，动画逐帧累积 | XWidget_font 契约：返回独立副本，调用方必须 deinit | deinit 移出循环体至函数尾恒执行 | 小 |
| R-38 | P1 | Src/XGui/Charts/XPieSlice.c:207（:209-215） | setValue 把 init_ex 初始化体复制进 setter：每次改值重置 pen/brush/labelBrush 颜色为 0 并将 m_labelFontFamily 置 NULL（旧串泄漏），用户外观被静默清空 | QPieSlice::setValue 只更新 value 并触发角度重算 | 函数体收敛为 m_value 赋值+valueChanged 发射 | 小 |
| R-39 | P1 | Src/XGui/Charts/XChartView.c:1242 | 面积序列填充用定长 XPoint poly[128] 并钳 126 点，>126 点面积图填充静默截断而描边画全量，图形残缺（与 §3.2「无静默截断」声明相悖） | 面积图用完整上边界+基线闭合路径填充全部点 | 填充与描边同源，改动态容量 | 中 |
| R-40 | P1 | Src/XGui/Charts/XChartView.c:1190（:905/:1189/:1224/:1327/:1378 等多点） | 视图侧主题色回退按「类型内下标」取色，与 setTheme 烘焙的全局序不一致，混合类型未 setTheme 时跨类型同色 | 序列加入图表即按全局序分配主题色 | 回退路径统一走 xchart_seriesGlobalIndex | 中 |

### P2（71 项）——行为偏差 / 死信号 / 死属性 / 潜伏缺陷

| 编号 | 严重度 | 位置 | 偏差 | Qt 对标 | 修复思路 | 规模 |
|---|---|---|---|---|---|---|
| R-41 | P2 | Src/XGui/Widget/XComboBox.c:1904 | highlighted/textHighlighted 高亮信号死信号：声明存在全文件零发射点 | 弹层高亮条目变化即发射 | 键盘导航与 hover 路径补发射 | 中 |
| R-42 | P2 | Src/XGui/Widget/XCompleter.c:616 | activated/textActivated/activatedIndex 从未自动发射：补全采纳只发宿主控件信号 | 用户采纳补全时由 completer 发射 | 采纳路径（XLineControl.c:3380/XComboBox.c:1811）回发 completer | 中 |
| R-43 | P2 | Src/XGui/Widget/XCalendarWidget.c:511 | activated(XDate) 头文件标「真发射」实为零发射点，控件无任何键盘处理 | activated 由 Enter/激活发射，日期可键盘导航 | 补 KeyPressEvent 导航+Enter 发射 | 中 |
| R-44 | P2 | Src/XGui/Widget/XCalendarWidget.c:209 | 星期表头恒绘「一..日」不随 setFirstDayOfWeek 旋转，列标签与日期列错位 | 表头随 firstDayOfWeek 旋转 | 绘制起点按 m_firstDayOfWeek 偏移 | 小 |
| R-45 | P2 | Src/XGui/Widget/XCalendarWidget.c:497 | selectionMode 存而不消费：NoSelection 下点击仍选中并发 clicked | NoSelection 点击不改 selectedDate 也不发 clicked | mousePress 入口补模式门禁 | 小 |
| R-46 | P2 | Src/XGui/Widget/XKeySequenceEdit.c:330 | setClearButtonEnabled(true) 无任何实现：不绘清除按钮、无鼠标交互 | 显示清除按钮，点击清空序列 | paint 绘按钮+MousePress 命中清空 | 中 |
| R-47 | P2 | Src/XGui/Widget/XTextEdit.c:1626 | zoomIn/zoomOut 只增减 m_fontPointSize 属性，不改字体不触发重绘，缩放无视觉效应 | zoomIn/Out 逐级改变编辑器字体立即生效 | 比照 XPlainTextEdit xpe_zoomApply(:1936) 真改 XWidget_setFont | 小 |
| R-48 | P2 | Src/XGui/Widget/XLineEdit.c:1601 | alignment 属性存而不消费：Right/Center 后正文绘制恒左对齐（XAbstractSpinBox_setAlignment 转发同失效） | 绘制按 alignment 布局文本/光标 | 绘制起笔 x 计算补对齐分支 | 中 |
| R-49 | P2 | Src/XGui/Widget/XAbstractSpinBox.c:574 | setLineEdit 安装外部编辑框不接管：不 reparent/不设几何/不 show/不接 editingFinished，提交链断裂 | reparent+同步几何+更新连接 | 比照 spinbox_createDefaultLineEdit(:90-103) 补四件套 | 中 |
| R-50 | P2 | Src/XGui/Widget/XPlainTextEdit.c:601 | updateRequest 公共信号在控制器局部重绘路径不外发（约 10 处控制器发射被槽吞），仅滚动路径发射 | 视口任何失效（含光标闪烁）都发射 | 槽内转发发射公共信号 | 小 |
| R-51 | P2 | Src/XGui/Widget/XPlainTextEdit.c:1556 | ensureCursorVisible 只滚垂直且光标行顶对齐视口顶，不处理水平，NoWrap 长行光标滚不进来 | 最小滚动量使光标矩形双向可见 | 对齐 xpe_ctlVisibilitySlot(:625-652) 双向最小滚动 | 小 |
| R-52 | P2 | Src/XGui/Widget/XTextDocument.c:1189（解析 :942-973） | toHtml 对列表块只输出裸 <li>（无 ul/ol 包裹），setHtml→toHtml→setHtml 往返丢列表标记与缩进 | toHtml 输出 <ul><li> 结构且与解析互逆 | 块级输出按 listDepth/ordered 补 ul/ol 包裹 | 中 |
| R-53 | P2 | Src/XGui/Widget/XToolTip.c:89 | showText 记录 msecShowTime 但不启动隐藏定时器，提示停留至显式 hide；空串（非 NULL）仍显示空提示层 | 超时自动隐藏；文本为空等价隐藏 | 启动时长定时器+空串走 hideText | 小 |
| R-54 | P2 | Src/XGui/Widget/XSpinBox.c:1067 | AdaptiveDecimalStepType 存而不消费：setStepType 接受后 stepBy 恒按 singleStep 步进 | 按当前值数量级取 10 的幂步进 | stepBy 按 m_stepType 计算步长 | 小 |
| R-55 | P2 | Src/XGui/Widget/XCalendarWidget.c:337 | 默认选中日期硬编码 2026-09-09 且 shownYear/Month 硬编码 2026/9，跨日即现（XDate_currentDate 本文件 :613 已在用） | 构造 selectedDate=currentDate 并显示当月 | init 改用 XDate_currentDate | 小 |
| R-56 | P2 | Src/XGui/Widget/XAbstractItemView.c:1699-1716 | entered/viewportEntered 每次鼠标移动重复发射而非悬停进入新条目的边沿一次；viewportEntered 语义反转 | entered 仅 hover 索引变化时发射，viewportEntered 仅进入视口时发射 | 按前后索引差分判重（对照 XListWidget.c:822 范式） | 小 |
| R-57 | P2 | Src/XGui/Widget/XListWidget.c:786-790（XTreeWidget.c:1317-1321、XTableWidget.c:1583-1597 同款） | itemClicked/cellClicked 族在鼠标按下时发射（与 itemPressed 同次按压连发），与基类 release 语义（XAbstractItemView.c:1643-1662）互相矛盾 | clicked 须等释放发射，pressed 在 press 发射 | 三 Widget 层点击族迁至 Release 路径 | 中 |
| R-58 | P2 | Src/XGui/Widget/XListWidget.c:719-724（XTreeWidget.c:829-835 同款） | editItem 空操作桩不打开编辑器，而编辑闭环已建（XAbstractItemView_edit :403-455）且 XTableWidget_editItem 已接通，三控件行为分裂 | editItem 经 view->edit(index) 打开编辑器 | 转发 XAbstractItemView_edit（比照 XTableWidget.c:1161） | 小 |
| R-59 | P2 | Src/XGui/Widget/XScrollBar.c:454-473 | 右键标准菜单动作未连接任何触发处理，弹出后选择无效果；右击位置亦被丢弃（:368） | contextMenuEvent 标准菜单动作真实执行滚动 | connect 各动作并保留「滚动到此处」目标坐标 | 中 |
| R-60 | P2 | Src/XGui/Widget/XAbstractScrollArea.c:23-45 | 滚轮 Shift 横滚未实现：注释声明与实现矛盾，恒取垂直条从不读修饰键 | 按住 Shift 时滚动水平条 | wheelEvent 读修饰键分流水平条 | 小 |
| R-61 | P2 | Src/XGui/Widget/XAbstractScrollArea.c:409-424 | setVerticalScrollBar(NULL) 未按 Qt 拒绝：删旧条后接受 NULL，视口恒 w-16 留 16px 死列且滚动静默失效 | setVerticalScrollBar 首行拒绝 NULL | 入参 NULL 直接 return | 小 |
| R-62 | P2 | Src/XGui/Widget/XScrollArea.c:195-213 | ensureVisible/ensureWidgetVisible 无可见性判定恒把目标滚到视口原点+margin，已可见也无条件跳动；widget 版还丢尺寸维度 | 仅越出当前视口时按 margin 做最小滚动 | 比照 xlw_scrollRowVisible（XListWidget.c:80-101）最小滚动判定 | 中 |
| R-63 | P2 | Src/XGui/Widget/XHeaderView.c:934-1048 | restoreState 生产路径残留 25 处 fprintf(stderr,"[RS-fail]")（HEAD 与工作树一致），校验失败即刷 stderr，违反 §8.0c2/R4 静默化纪律 | 无此输出 | 删除或收编为编译开关 | 小 |
| R-64 | P2 | Src/XGui/Widget/XTableWidget.c:432-448 | setRowCount 不同步内建模型维度（setColumnCount/insertRow/removeRow/clear 均同步），先列后行调用序下模型 0 行致编辑/键盘搜索/role 通路失真 | 模型行列数即唯一事实源 | 补 setDimension 同步 | 小 |
| R-65 | P2 | Src/XGui/Widget/XListView.c:354-359（XTreeView.c:1060-1064、XTableView.c:1149-1153、XTreeWidget.c:1200-1206） | 条目视图族自绘配色硬编码（选中/当前/交替/文本），不读调色板，非默认调色板下仍白底黑字；XTreeWidget 注释与实现自相矛盾 | 视图绘制消费 palette Highlight/HighlightedText/WindowText/Base | 四处改走 XPalette 取色（对照 XTableWidget.c:1401-1407） | 小 |
| R-66 | P2 | Src/XGui/Widget/XTableWidget.c:598-623 | setCurrentCell 不做越界收敛且无条件发射 currentCellChanged（同格重设也重发），越界当前格永不命中高亮成不可见脏状态 | 越界 index 清除当前项；信号仅实际变化时发射 | 补界检收敛+变化门禁 | 小 |
| R-67 | P2 | Src/XGui/Widget/XListWidget.c:850-868 | Return 在基类已消费并开启条目编辑后仍无条件补发 itemActivated，编辑与激活同键并发 | Return 先经 edit(EditKeyPressed) 拦截，开启成功即 return | 父类返回后检查 XEvent_isAccepted 门禁 | 小 |
| R-68 | P2 | Src/XGui/Widget/XAbstractItemView.c:1782-1788 | PageUp/PageDown 页行数按整控件高度计算而非视口高度，含表头带/边框时翻页步幅偏大 | 键盘翻页以 viewport 高度参与计算 | 改用 XAbstractScrollArea_viewport 宽高（与 scrollToHint :1226 同口径） | 小 |
| R-69 | P2 | Src/XGui/Widget/XTreeWidget.c:1139-1146 | 条目数据承载为单文本、无 role/多列/checkState 维度，多列设置后第 0 列外无数据通路；itemEntered 无发射点 | QTreeWidgetItem 逐列 role 承载；itemEntered hover 进入新条目时发射 | 扩展 XTreeWidgetItem 列/role 存储并补 hover 路径 | 大 |
| R-70 | P2 | Src/XGui/Widget/XMenuBar.c:186 | hovered 桥接槽已实现但从未连接，菜单栏无 hover/移动事件与悬停高亮（XToolBar 已接同款桥可对照） | 动作被悬停高亮时发 hovered(QAction*) | 连接桥接槽+补 MouseMove/HoverMove 高亮 | 中 |
| R-71 | P2 | Src/XGui/Widget/XGroupBox.c:100 | 勾选状态切换强制覆盖子控件启用位：重新勾选把应用显式禁用的子控件一并强制启用 | _q_setChildrenEnabled 维护 disabledChildren 恢复表 | 切换时记账/恢复 ForceDisabled | 中 |
| R-72 | P2 | Src/XGui/Widget/XLabel.c:1669（label_posToUtf16 :1795 同款） | 链接命中/文本定位的字形循环内错放 XFont_deinit_base：首迭代后字体析构，后续字形按默认字库度量漂移（非内存错误） | hitTest 全程以控件当前字体度量 | deinit 移出循环体 | 小 |
| R-73 | P2 | Src/XGui/Widget/XToolButton.c:168 | setDefaultAction 不镜像动作图标：仅同步 text/checkable/checked/enabled，带图标动作在按钮上无图标且布局不留位 | defaultAction 的 icon 随动作镜像显示 | 镜像槽补 icon 拷贝+sizeHint 计入 | 小 |
| R-74 | P2 | Src/XGui/Widget/XToolBox.c:345 | removeItem 移除当前页后旧页不隐藏不摘父链，叠在新当前页上方成残影 | removeItem 摘除并隐藏 widget（widget 本身不删除） | 移除时 hide+摘父链 | 小 |
| R-75 | P2 | Src/XGui/Widget/XRadioButton.c:155 | 完全无图标渲染/度量路径：setIcon 不显示、sizeHint 不计入（同族 QCheckBox/QPushButton/XToolButton 均已支持），hasIconSize 为死代码 | QRadioButton 继承 QAbstractButton 消费 icon | drawContents/sizeHint 接入 XIcon_paint | 中 |
| R-76 | P2 | Src/XGui/Widget/XPushButton.c:270 + Src/XGui/Widget/XDialog.c:99（合并 2 条；对话框域+按钮菜单域同根因） | 默认按钮机制整体缺失：setDefault 纯存储位（不解除前一默认，可多个同真），XDialog 键盘仅 Escape→reject、无 Enter→默认按钮派发；XDialog/XDialogButtonBox 均无默认按钮机制（XMessageBox 私有实现不覆盖普通 QDialog 族） | setDefault 联动对话框解除前默认；QDialog 对 Enter 查找 default button 并点击（勘误：焦点在 autoDefault 按钮时按 Enter 点自身与 Qt 一致，非偏差） | 对话框级默认按钮注册表+互斥解除+Enter 派发一体实现 | 中 |
| R-77 | P2 | Src/XGui/Widget/XLcdNumber.c:580 | display(QString) 的 value 解析口径为前缀解析（"12.5px"→12.5），Qt 为整串解析失败置 0，value()/intValue() 可观察结果不同 | QString::toDouble 整串解析 | 改整串解析或于 §8.1 声明边界 | 小 |
| R-78 | P2 | Src/XGui/Widget/XWizard.c:198（复核校准 P1→P2） | setTitle/setSubTitle 运行期变更不上屏：setSubTitle 不触发横幅重排，setTitle 只脏页面不含横幅带；构造期静态文本正常 | setTitle/setSubTitle 立即更新横幅（随内容伸缩） | setter 触发横幅重排+向导脏区失效 | 小 |
| R-79 | P2 | Src/XGui/Widget/XTabBar.c:1062（XTabWidget.c:448-449 委托同路径） | removeTab 删当前页之前页时 m_currentIndex 不回退（同下标改指后一页签，选中右移一位）且不发射 currentChanged；selectionBehaviorOnRemove 仅存储 | index<currentIndex 时 --currentIndex，当前页变化必发 currentChanged | 补左移分支+发射 | 小 |
| R-80 | P2 | Src/XGui/Widget/XMessageBox.c:83 | exec 按钮点击路径不发射 finished/accepted/rejected（槽只置 m_inExec=false），与 Esc 路径（真发射 rejected）不对称 | 按钮点击经 QDialog::done(r) 统一发射 finished/accepted/rejected | 槽改走 XDialog_accept/reject/done | 小 |
| R-81 | P2 | Src/XGui/Widget/XDialog.c:142 | m_modal 默认 true 与 Qt QDialog 默认 false 相反；show() 从不消费该属性，查询/行为不自洽 | modal 默认 false，仅 exec/open 模态化 | 默认改 false | 小 |
| R-82 | P2 | Src/XGui/Widget/XProgressDialog.c:224 | minimumDuration 从未被消费：setValue 不会按最小时长自动显示对话框，标准 Qt 用法下永不自动出现 | setValue 首调起按 minimumDuration 计时超时自动显示 | setValue 起接时长定时器 | 中 |
| R-83 | P2 | Src/XGui/Widget/XDockWidget.c:519 | setTitleBarWidget 纯存储：不 reparent、不布局、paintEvent 不消费，自定义标题栏永不可见 | 接管控件并置于标题条区呈现 | 挂布局+绘制分流 | 中 |
| R-84 | P2 | Src/XGui/Widget/XWizard.c:1109 | setButton 登记的自定义按钮不 reparent/不入按钮布局/点击无转发；customButtonClicked 死信号 | setButton 替换标准按钮入布局，点击发 customButtonClicked | 入布局+点击转发+信号发射 | 中 |
| R-85 | P2 | Src/XGui/Widget/XTabWidget.c:748 | setCornerWidget 角部件不 show（框架显式 show 语义下 m_explicitShow=0 恒不可见），下两角不参与布局；clients 已补 show 而角部件漏 | setCornerWidget 即刻显示角部件 | setter 补 XWidget_show（比照 xtabwidget_showCurrent :97-99） | 小 |
| R-86 | P2 | Src/XGui/Widget/XTabBar.c:712 | 失能不终止滚动按钮连发定时器：§8.0g11 自述「释放/失能/析构终止」中失能分支未接线（changeEvent 无条件 ignore） | 自述行为：控件失能应停止连发 | changeEvent 失能分支调 xtabbar_scrollRepeatStop | 小 |
| R-87 | P2 | Src/XGui/Style/XStyle.c:436-453 | EtchDisabledText 分支画完 light 色 (+1,+1) 蚀刻影后主文本用 penColor 取色（已是 light），禁用文本整体画成 light；alignment 参数在基类路径被完全忽略 | etch=light 偏移影+dark 主体按 alignment 对齐绘制 | 主体显式用 dark 色+按 alignment 绘制 | 小 |
| R-88 | P2 | Src/XGui/Style/XFusionStyle.c:405-481 | standardPalette 只填 Active 全角色+Disabled 子集，Inactive 组保持 XMemset 全零无效色（勘误：Current 组经 palette_normalize_group 映射 Active 无恙；当前无消费方，潜伏） | 调色板 Active/Inactive/Disabled 三组完整有效配色 | Inactive 组复制 Active 填充 | 小 |
| R-89 | P2 | Src/XGui/Style/XCommonStyle.c:1641-1662 | CC_Slider 刻度循环 v+=interval 无溢出护栏，sliderMax 近 INT_MAX 时有符号溢出（UB；回绕通常致退出而非挂死） | 同位置循环带显式护栏（nextInterval < v 即 break） | 补回绕护栏 | 小 |
| R-90 | P2 | Src/XGui/Style/XStyleSheetStyle.c:284-315（复核定性修正） | QSS 关系选择器祖先/父段一律以 state=0 匹配伪类：:enabled/:hover/:focus 永不命中使整条规则失效、:disabled 恒命中（勘误：Qt 忽略祖先段伪类恒应用规则，偏差方向与初判相反但真实） | 伪类仅末段生效（Qt 6.8 源码核实） | 祖先段忽略伪类（对齐 Qt）或实现逐段状态求值 | 中 |
| R-91 | P2 | Src/XGui/Style/XCssStyleSheet.c:211-212 | 类选择器 .Foo 被当元素名存储并计特异度 1，与类型选择器同权重同匹配路径，class>type 级联序丢失可错色 | Qt 中 class 权重 16 显著高于 element 1 | 类选择器独立字段+特异度分级 | 中 |
| R-92 | P2 | Src/XGui/XLayout/XLayout.c:1159-1166（appendItem :470-481、releaseItems :292-308） | 基类 XLayout_addWidget 创建堆条目后以 owned=false 插入，布局 deinit 不释放，与 XLayout.h:27-31 所有权文档相悖（全库零调用方，潜伏泄漏） | addWidget 条目归布局所有并在析构时删除 | 基类入口置 owned=true（比照子类 insert* 路径） | 小 |
| R-93 | P2 | Src/XGui/Style/XCommonStyle.c:3064-3066 | SH_SpinBox_StepModifier 返回 0，Qt 默认 ControlModifier=0x04000000；注释误记「Qt6 为 0x02000000」（那是 ShiftModifier） | styleHint 默认返回 Qt::ControlModifier | 返回值与注释订正 | 小 |
| R-94 | P2 | Src/XGui/Text/XTextControl.c:4325-4346 + 4353-4367 | insertHtml/appendHtml 产物尾部恒带段落换行且仅 appendHtml 剥首换行，富文本粘贴/追加各多一个空段落 | fromHtml("<p>a</p>") 插入为一个块，块末分隔符不产生额外空块 | 插入前统一剥尾换行 | 小 |
| R-95 | P2 | Src/XGui/Text/XTextClipboard.h:15-18 + 52-54 | 头文件契约与实现相反：声明「剪贴板无文本返回内部缓冲」，实现仅整体不可用才回退、无文本返回 ""（实现侧与 Qt 一致，纯 API 文档错误） | QClipboard::text() 无文本返回空 QString | 订正头文件 @details | 小 |
| R-96 | P2 | Src/XGui/Window/XWindow.c:1902-1930 | 无平台句柄窗口重复 setVisible(同值) 重发 Show/HideEvent 并重设 exposed；有句柄路径去重，两路径语义不一致 | visible==this->visible 即 return，不重复派发 | 已建窗同值重入早退（保留父窗补建合法重入） | 小 |
| R-97 | P2 | Src/XGui/Application/XGuiApplication.c:692-735 | lastWindowClosed 仅在窗口注册表完全清空时发射，不检查剩余窗口可见性，隐藏顶层阻止信号与退出策略 | 最后一个可见主窗口关闭即发射（按 visible 判定） | removeWindow 时检查剩余顶层 isVisible | 小 |
| R-98 | P2 | Src/XGui/Platform/XPlatformIntegration.c:625-630 | styleHint 两处偏离：SetFocusOnTouchRelease 无单例回退 true（Qt false）；MousePressAndHoldInterval 恒 500（Qt 800）且映射 switch 不含该项、注入 XStyleHints 单例也不读 | defaultThemeHints：SetFocusOnTouchRelease=false、MousePressAndHoldInterval=800 | 接通 XStyleHints 单例回读+订正默认值 | 小 |
| R-99 | P2 | Src/XGui/Window/XWindowEvent.c:686-688 | pixelDelta 伪造为 angleDelta/120（每刻度 1 像素非 0），Qt 惯用法「pixelDelta 优先」的外部代码会 1px/格步进（仓库无 pixelDelta 消费者、头文件已自述取舍） | 平台无像素增量时 pixelDelta 恒 (0,0) | 置 (0,0) 对齐 Qt 语义 | 小 |
| R-100 | P2 | Src/XGui/Platform/XPlatformIntegration.c:730-735 | createPlatformOffscreenSurface 忽略传入 surface 请求尺寸恒建 1×1 且 XPlatformOffscreenSurface 无 resize API（驱动级定尺寸路径在用、工厂零调用方） | 按传入表面 size()/requestedFormat() 建立对应尺寸离屏表面 | 透传尺寸建面+补 resize 协议 | 中 |
| R-101 | P2 | Src/XGui/Charts/XChart.c:1463（xchart_registerSeries :682-690） | addXxxSeries/addSeries 均无去重守卫，同序列重复添加在按类数组与泛型表双重登记，析构双重释放；注册表函数头注释自称「去重」与实现不符 | 对已挂载序列 qWarning("Series is already added") 并忽略 | 注册表补 contains 守卫 | 小 |
| R-102 | P2 | Src/XGui/Charts/XChartView.c:1950 | 悬停离开以硬编码 (0.0,0.0) 发射 hovered(point,false)，接收方无从得知离开点；同序列 A→B 换点不给旧点发 false | 离开事件携带实际离开点的数据坐标 | 缓存上一悬停点用于离开载荷 | 小 |
| R-103 | P2 | Src/XGui/Charts/XChartView.c:1943 | m_hoverSeries 跨事件借用指针，序列被 removeSeries/removeAllSeries/setChart 删除后无失效联动，下次 mouseMove 在已释放对象上发射信号（UAF，触发窗口窄） | 交互项随序列删除即时销毁悬停状态 | 序列删除路径清悬停状态 | 小 |
| R-104 | P2 | Src/XGui/Charts/XPieSeries.c:163 | VXPieSeries_move 在父类 move（已把 other 的 name 移入 self）之后才 deinit(self)，基类析构把刚移入的 name/axes 释放——move 后序列名称/图表链丢失（状态损毁非 double free） | 基类资源先释放再转移，不得二次析构已接管内容 | deinit 提前至 move 前（对照 VXXYSeries_move :295-333 正确序） | 小 |
| R-105 | P2 | Src/XGui/Charts/XBarSet.c:389 | remove 在 index≥m_count 时 removeCount 计算为负，m_count 反向膨胀出未初始化数据当有效柱值，更极端时越容量访问 | 越界 pos 忽略不改任何状态 | 补 index>=m_count 拒绝分支 | 小 |
| R-106 | P2 | Src/XGui/Charts/XChartView.c:1076 | 轴属性 setReverse/setVisible 只存储不消费：翻转映射/隐藏轴线与网格均无视觉效果 | setReverse 翻转映射方向、setVisible(false) 隐藏轴 | mapPoint 与轴绘制消费两属性 | 中 |
| R-107 | P2 | Src/XGui/Charts/XPieSeries.c:96-97 | 饼图默认起始角两处口径矛盾：init 置 0.0，空指针回退 getter 返回 90.0，必有一处与真实默认不符 | getter 默认应与构造默认一致（文档化确定值） | 统一默认值 | 小 |
| R-108 | P2 | Src/XGui/Icon/XSvgIconEngine.c:29 | 引擎 pixmap 虚槽显式 (void)size 忽略请求尺寸按固有尺寸出图，引擎路径无事后缩放（当前全仓无注册调用，休眠偏差） | pixmap 按请求尺寸渲染 SVG（矢量按目标 size 光栅化） | 按 requested 缩放出图 | 小 |
| R-109 | P2 | Src/XGui/Charts/XXYSeries.c:1125 | setPointConfiguration 双数组扩容 OOM 时颜色块成功而尺寸块失败直接 return：m_pointColors 可能悬挂（realloc 已搬迁）或新块泄漏（仅 OOM 路径） | 容器异常安全由分配器语义保证，不产生悬垂指针 | 失败回滚/回写已扩块与容量 | 小 |
| R-110 | P2 | Src/XGui/Charts/XChart.c:572 | VXChart_copy 的 init 兜底分支与无条件 init 连用：裸内存目标首次 init 分配的 3 个 XString+2 个轴块被第二次 init 的 XMemset 覆盖泄漏（一次性 5 个小分配） | 拷贝语义对目标资源先释放后重建、恰好一次 | 收敛为条件 init 或第二次 init 前先 deinit | 小 |
| R-111 | P2 | Src/XGui/Charts/XChart.c:1580 | removeAxis 仅把 m_axisX/m_axisY 置 NULL 不释放：init 内建轴（XMalloc_System 所有权在图表）被移除后成孤儿泄漏；与同文件 setAxisX(:824) 对旧轴 free 的口径相反 | removeAxis 返还所有权不 delete（而 setAxisX 语义相反——本库内部口径先统一） | 统一所有权口径：内建轴释放、外接轴归还 | 小 |

规模分布：P0 小 6 / 中 2；P1 小 14 / 中 13 / 大 3（R-17/R-18/R-27）；P2 小 47 / 中 21 / 大 1（R-69）。建议开工顺序：P0 八项先行（七项为小改），P1 按 R-14/R-10/R-21~R-23 等小改项快速清库、R-17/R-18/R-27 三个大项单独立批。

## 四、被驳回发现附录

复核阶段共驳回 **2 条**原始发现，均未计入本队列：

- Style与Layout 域 1 条；
- Platform与Window 域 1 条。

另有一处**扫描阶段自查撤销**未计入原始/确认数：XTabWidget_removeTab「客户控件父指针悬垂」疑点，经 VXObject_deinit（XObject.c:556-575）级联删除子对象证据撤销，与 Qt removeTab 删除页面语义一致，非缺陷。

> 说明：两条驳回项的明细（位置与驳回理由）未随本次汇总的输入条目下发，故本附录仅能记录计数；如需归档驳回理由，需向复核阶段补取原始记录。

## 五、复核校准记录（9 条 adjusted，判级以本队列为准）

| 队列号 | 原判 | 校准 | 理由摘要 |
|---|---|---|---|
| R-09 | P0 | P1 | 7712 未初始化读为被 7720 无条件覆盖的死存储，无可观察行为影响；实际收敛为 italic+位图 AA 顶部墨迹裁切 |
| R-11 | P0 | P1 | 死实现成立，但 XTextEdit 用户级 undo 走自有栈、全库无生产代码调用 XTextDocument_undo，影响限于文档模型直接消费方 |
| R-14 | P0×2 | P1×2 | 无崩溃/数据损坏、控件置于原点时正确，错位渲染与 §8.0g12 同款批次按 P1 根修 |
| R-21 | P0 | P1 | 纯泄漏不伴内存破坏，按仓库判级先例（XGetAtomName 泄漏记 P1）归 P1 |
| R-76 | P2 | P2 | 证据含一处 Qt 行为误述已勘误（焦点在 autoDefault 时 Enter 点自身与 Qt 一致），偏差本体成立 |
| R-78 | P1 | P2 | 影响限于运行期动态改标题/副标题场景，构造期首绘正确，纯视觉陈旧 |
| R-90 | P2 | P2 | qtRef 误述已修正：Qt 6.8 忽略祖先段伪类（非按祖先真实状态求值），偏差方向反转但真实存在 |
| R-37 | P0 | P1 | 纯泄漏无内存不安全，但逐帧累积无上界，高于 P2，按本仓泄漏判级口径记 P1 |
