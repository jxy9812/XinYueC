# XGuiDemo 全页面双形态视觉走查清单（2026-10-27）

> 桌面 Xvfb 自动化截图：横屏 1000x700（--screenshot 直出）+ 竖屏 400x760（活窗
> resize 抓帧），页 0..21 全量 44 张（/tmp/ui_audit/h_NN.png、v_NN.png）。
> 四个子代理按「控件重叠/文字重叠/裁字缺失/溢出/间距/对比度」判据独立审查。
> 修复进度标记：[ ] 待修 [x] 已修 [~] 部分修。

## P1 明显破损（6 条）

- [~] P1-1 **输入演示（h_03/v_03，两形态都有）**：字体选择下拉框闭态文本叠印。
  **已定性（探针证据链）**：非残影（点击/全量重绘后仍在）、非弹层视图
  （POP 探针零绘制）、非几何重叠（combobox 探针仅两实例并排）、非
  drawText/drawTextRect/drawGlyph 任何调用（三 API 全程零「Noto Sans」
  非 Gurmukhi 调用）→ **drawText(「Noto Sans Gurmukhi」) 单调用产出双份
  字形**（绘制引擎内部，疑 XFontFt/painter 对混合族名的分段/回退渲染
  错位），需 XFontFt/painter 渲染专项。探针已撤（XGUI_COMBO_TRACE 定位
  法见 git 历史）。
- [x] P1-2 **软件更新（h_16/v_16）**：「策略」行两行状态文字（策略：未检查 /
  处理：未判定）行距小于字高叠印；「当前版本」行两行近乎贴住。页面布局行距
  问题。
- [x] P1-3（最小高缓解；scroll 壳方案已验证待收尾） **条目视图（v_05）**：XTreeWidget 底部第二行只露上半截且被控件内水
  平滚动条叠压——树高不足（窄屏 400 高度分配）。
- [x] P1-4 **ATV61 变频器（v_21）**：「串口」行站号控件整体溢出右缘不可见。
- [x] P1-5 **ATV61（v_21）**：控制按钮行第 5 个按钮（自由停车后）溢出右缘。
- [x] P1-6 **ATV61（v_21）**：「频率给定」行轮询输入框溢出右缘。→ ATV61 页
  未做窄屏适配，需按 window 页先例加窄档 adapt（行翻竖排/滚动）。

## P2 影响观感（8 条）

- [x] P2-1 **窗口设置（v_19）**：「唤醒方式」行第三个复选框「触摸唤醒」被裁
  断（400 宽行尾溢出；横屏正常）。
- [ ] P2-2 **容器与窗口（h_04）**：页签栏最后页签「向导」被裁只剩「向」（页签
  超宽出現 › 翻页箭头——可接受或调页签宽度策略）。
- [x] P2-3（复核定性：按钮在编辑器右缘+12px 设计位，非错位） **高级控件（h_07）**：「键入文本」按钮悬空错位（富文本框右缘与右
  侧按钮列之间孤立，与任何分组不对齐）。
- [x] P2-4（复核定性：LCD+竖直滚动条并排=演示原设计，非变形） **输入演示（v_03）**：数字框右侧被压缩到 ~20px 的窄竖条控件（滑
  块?）形态异常。
- [x] P2-5（页 3 窄档顶行翻竖排，demo_layout_content 漏斗收口） **输入演示（v_03）**：表盘（Dial）左缘被容器左缘裁成直线。
- [ ] P2-6 **条目视图（v_05）**：XTableWidget 表格右缘溢出窗口右缘被裁无收口。
- [x] P2-7（placeholder 缩短：点击第二框 → 键盘跟随重绑（组串复位）） **键盘演示（v_09）**：第 4 个说明框行尾裁字（「拼音组串复」疑缺
  「绑）」，400 宽单行放不下）。
- [x] P2-8（ATV61 hint 按 HUD 几何换行让位） **ATV61（h_21/v_21）**：「频率给定(Hz)」标签右括号被输入框压住；
  横屏底部说明行被 FPS 悬浮窗遮盖。

## P3 打磨项（跨页共性优先）

- [x] P3-A（**框架根修已落地 2026-10-27**：步进列几何四方分叉→单源收口。此前**复核降级为「视觉紧凑非压框」是误判**——像素取证证实确有 1px 分隔线压在输入框右边框上） **数字增减框（SpinBox）上下箭头压输入框右边框**——Modbus 主/从站、
  ATV61 多页共性，框架绘制级修一处跨页生效。
  - 真根因：同一份步进列几何被**推导四次、得出三个不同答案**——① 样式几何
    `VXCommonStyle_subControlRect`（Qt 公式，但 `bw` 夹取顺序与 Qt 相反）；
    ② 底层绘制 `xcs_drawSpinBox`（写死 16px/右缘齐平，无视 fw）；③ QSS 子件镜像
    `xsss2_subControlRect`（写死 16px）；④ 命中检测 `XSpinBox.c`（写死 `w-16`）。
  - 跨层断裂：QSS 代理解 `PM_SpinBoxFrameWidth`=2（CommonStyle）而实绘走
    Fusion=3 ——「预留几何」与「实绘几何」不同源。
  - 修复：新增 `xcs_spinBoxColumn()` 作为**唯一真值源**，四处全部经它派生；
    `bw` 夹取改为 Qt 的 `qMax(16, qMin(bh*8/5, w/4))`（先按 w/4 封顶再保底 16）；
    QSS 代理补 `PM_SpinBoxFrameWidth` 向源样式回落（对标 qstylesheetstyle.cpp:4989）。
  - 像素验证：修复前 x=250 输入框边框 / x=251 分隔线 / x=252 按钮边框三层紧贴；
    修复后分隔线消失，输入框边框与按钮边框直接相邻，箭头随列居中。
  - 门禁：`wf_all_gate` RC=0、`wf_orient_gate` PASS（no new failures）。
  - **证伪记录**：排队根因「Qt 的 rx 是右边界坐标，XGui 需再减一个 fw」是**错的**
    ——Qt `QRect(lx, fw, rx, h-2fw)` 里 rx 是**宽度**，XGui 原式已与 Qt 逐字节等价；
    照原计划改会凭空造出 2px 假间隙。证据=新增探针
    `Test/XGuiTest/xspinbox_geometry_probe.c`（`XSpinBoxGeometryProbe` 目标）。
- [x] P3-B（wn_buildCaption Mid→Dark） **分组小标题浅灰对比度不足**（窗口设置 h_19/v_19「皮肤/窗口框架/
  方向/屏幕休眠」）——Mid 色角色过浅。
- [x] P3-C（mb_buildLabel 统一 Dark 前景） **Modbus 主站 DI/DO 标签浅灰对比度低**（h_17/v_17；从站页用红色可
  读性好——统一口径）。
- [x] P3-D（CJK 标签行 0 间距→4px 与 IP 行统一） **标签与输入框零间距**（远程客户端 h_11/v_11 端口/档位/认证/口令
  行）——标签列宽或间距。
- [~] P3-E（会话状态 4~9 行动态，拆单行需 label 池——列多行动态文本专项） **远程窗口服务状态 4 行行距过密**（h_10）。
- [x] P3-F（源码 typo 实锤：「点击置不」→「点击置位」） **Modbus 从站「输入点 DI(点数置零不)」文案疑截断**（h_18）——核对
  源文案。
- [x] P3-G（复核定性：源文案「主布局锁定拉丁」完整，截图缩放误读） **键盘演示说明段「主布局锁定丁」疑漏「拉」字**（h_09）——核对源文
  案。
- [x] P3-H（服务器标题列 96→76 与进度条列对齐） **软件更新进度条与文本值列左缘参差**（v_16）。
- [x] P3-I（rowsRow 窄档翻竖排） **悬浮窗设置「CPU/GPU」与「网络」复选框零间距**（v_14）。
- [x] P3-J（复核定性：文案即「多行编辑/第二行/第三行」，无缺失） **输入演示「多行编辑」框首行不可见疑滚动偏移**（v_03）。
- [ ] P3-K **ATV61 串口下拉框/波特率框高度异常大**（v_21）。

## 无问题页面

横屏：h_00 按钮、h_01 选择、h_02 堆叠、h_05 条目、h_06 对话框、h_08 图形效
果、h_12 图表、h_13 网络、h_14 悬浮窗、h_15 二维码、h_20 时钟DDNS。
竖屏：v_00 按钮、v_01 选择、v_02 堆叠、v_04 容器、v_06 对话框、v_07 高级、
v_08 图形效果、v_10 远程窗口、v_12 图表、v_13 网络、v_15 二维码、v_20 时钟。


## 第二轮复审补充（2026-10-27 修复后重截 44 张再审）

**框架级真缺陷（本轮修复）**：
- [x] **P1 XSpinBox 窄档整体不可见**（ATV61 竖屏「站号」「轮询」两个数字框消失）=
  XSpinBox 是唯一未设 sizeHint 的输入类控件 → 布局度量回落 0 → 竖排按
  hint 高给尺寸时高度塌 0。**修=框架层补 sizeHint**（文本宽+步进区+
  风格边距，init/setValue 刷新）——一处跨页生效。
- [x] **P2 下拉箭头针状变形**（跨页通用）= `XCommonStyle.c` `xcs_drawArrow`
  的 hw=r.width/3、hh=r.height/3 分别取半宽半高 → 16x26 窄条区拉成
  10x16 针状。**修=同基准取小边（等边三角）**。

**页面级**：
- [x] P2-5 页 3 窄档顶行翻竖排（表盘左缘被裁/字体下拉被压扁）
- [x] P2-7 键盘页 placeholder 缩短
- [x] P2-8 ATV61 hint 按 HUD 几何换行让位
- [x] P3-B 组标题 Mid→Dark（**注：QSS 主题覆盖调色板角色，仍浅灰**——见下）
- [x] P3-C Modbus 标签统一 Dark 前景（**同样被 QSS 覆盖，见下**）
- [x] P3-D CJK 标签行 0→4px 间距
- [x] P3-F 源码 typo「点击置不」→「点击置位」
- [x] P3-H 服务器标题列 96→76 对齐进度条列
- [x] P3-I 悬浮窗设置五开关翻竖排

**待办（未修）**：
- [x] **P3-B/P3-C 对比度不足 → 已按「语义角色」修正**（归因更正：此前
  判断「QSS 覆盖调色板角色」**有误**——QSS 全文只有 background-color/
  border/border-radius，**无任何 color: 规则、无 XLabel 规则**，文字色
  100% 来自调色板角色。真实根因=**demo 拿装饰色角色当文字色**：
  Qt Fusion 的 Dark(159)/Mid(184) 是 background.darker() 出来的**边框/
  分隔线装饰色**，白底上作文字只有 ~2:1；框架调色板值与 Qt 逐位一致、
  **框架无错**。修=字段标签用 Text(黑，21:1)、组标题用 PlaceholderText
  （Qt「次要文字」语义角色，约 4:1），弱化层次靠字号/留白而非装饰色。
- [ ] P1-1 字体下拉叠印（绘制引擎专项；子代理提「可编辑模式双绘」假设与
  探针证据矛盾——探针证明仅一次 drawText 调用）
- [ ] P3-E 会话状态多行动态拆行（需 label 池）
- [ ] P2-2 页签溢出翻页（QTabWidget 正常行为）
- [ ] v_20 时钟页两条 83/93px 空白带；v_14 零间隙；v_03 双层边框；
      v_04/v_05/v_07 间距类 P3；h_17 端口缺标签；h_21 底部说明裁切
- [ ] 页级滚动专项（ATV61/v_21 说明行被裁无滚动条）

**教训**：①改码前必须对源码/放大截图复核审图结论（两轮共 8 条误读）；
②**框架级 sizeHint 缺失会在窄档布局塌成 0**——新增控件务必设 sizeHint；
③调色板角色可能被 QSS 覆盖，改 role 前先查样式表。


## 框架对齐补齐（2026-10-27 第二轮：用户令「框架对齐先修完」）

**归因方法**：逐控件对照 Qt 6.8.3 源码（本机 `/home/xinyue/Qt-src/6.8.3`），
确认哪些控件**确实应有** sizeHint / minimumSizeHint 而框架缺失——补在框架层，
不在 demo 打补丁。审计结论：**视图族不需要单独补**（Qt 的 QListView/QTreeView/
QTableView/QPlainTextEdit/QTextEdit 都不 override sizeHint，继承
QAbstractScrollArea 的 256×192 口径，XGui 同构已对齐），真正缺失的是
输入类与容器类。

**框架层已补（18 处）**：
- sizeHint 缺失（布局里首选尺寸 0 → 竖排塌 0 不可见）：
  **XSlider**（qslider.cpp:424-445，槽长 84+厚度）、**XProgressBar**
  （qprogressbar.cpp:388-403）、**XDial**（qdial.cpp:425-428，100×100）、
  **XDateTimeEdit**（qdatetimeedit.cpp:1074-1100，委托内嵌编辑器；XDateEdit/
  XTimeEdit 继承自动受益）。
- minimumSizeHint 缺失（min=0 → 竖排可被压塌）：
  **XComboBox**（qcombobox.cpp:2505-2509，与 sizeHint 同源）、
  **XSpinBox**（此前 sizeHint/min 都缺，一并补）、
  **XAbstractScrollArea**（qabstractscrollarea.cpp:1405-1419，滚动条+边框）、
  **XScrollBar**、**XPlainTextEdit**、**XCalendarWidget**、**XMenuBar**、
  **XToolBar**、**XToolBox**、**XSizeGrip**、**XKeySequenceEdit**、
  **XLcdNumber**、**XGroupBox**（qgroupbox.cpp:456-480：Qt 不 override
  sizeHint、只 override minimumSizeHint → 按 Qt 口径只补 min）。
- 容器类 sizeHint：**XTabBar**（qtabbar.cpp:1487-1498，页签矩形并集）、
  **XTabWidget**（qtabwidget.cpp:837-870，页签条+当前页）、**XSplitter**
  （qsplitter.cpp:1490-1507，子项 sizeHint 累加）、**XHeaderView**（段尺寸累加）。

**回归**：all 门 RC=0、orient 门零新增失败；44 张双形态截图对比——第二批容器
hint 前后横屏平均差异 0.08%、竖屏 0.50%（仅 FPS 浮层/时间戳），**布局零劣化**；
第一轮 vs 基线差异仅落在预期页（输入页窄档翻排、软件更新拆单行、ATV61 滚动壳）。

**遗留**：字体下拉叠印（绘制引擎专项，P1-1）；页级滚动专项（ATV61 已落地，
views/窗口设置等页待推广）；QDockWidget/QMenu/QStatusBar/QToolTip/XStackedWidget
的 sizeHint 仍未补（Qt 有，XGui 缺；当前无布局暴露面——demo 内均定尺寸使用），
登记为下一批。


## 框架根因补修（2026-10-27 第三轮：XLabel 多行残影）

**归因**：2026-10-07 立案的「XLabel 多行动态文本渲染残影」此前只在 demo 侧
用「拆单行 + 盒距」缓解（属绕行，非框架修）。本轮对照 Qt 6.8.3 定位到框架
缺口：**Qt 的残影防线在顶层而非子控件**——
`QWidgetPrivate::paintBackground`（qwidget.cpp:2281-2315）的 **asRoot 分支**：
顶层控件上屏绘制前**总是**先用 Window 角色色回填本次绘制区域；Qt 的重画从
顶层开始（顶层先填底 → 再遍历子树画子控件），故任何不透明白底的子控件
（QLabel 缺省 autoFillBackground=false）其旧像素都由顶层这次回填覆盖。

**XGui 此前无此环节**：`XWidget_paintTree` 只派发 paintEvent、背景由各控件
自行决定是否填（`XWidget` 默认槽仅在 autoFillBackground 时填）⇒ 不透明白底
子控件局部重绘必留旧像素。

**修**：框架层在 `flushBackingStore` 的上屏绘制段（顶层）按 **Window 角色
色**预填本次绘制区域（Qt fillRegion 同款语义：只填本次区域，不整页铺底——
整页铺底的像素成本由各控件自己的背景负责）。

**回归**：all 门 RC=0、orient 门零新增失败；44 张双形态像素对比——横屏平均
0.09%、竖屏 1.12%（仅 FPS 浮层/时间戳与灯格状态），**布局零劣化**。

**注**：demo 侧「拆单行 + 盒距」**不是绕行框架缺陷、而是排版适配**——XGui
用位图点阵字库（ascent+descent=字高，无行间留白），相邻行笔画相接是位图
字库的固有形态（Qt 用 TrueType 时 lineSpacing≈1.2×height 才有留白）。框架
残影修好后，「行贴行」的观感仍需拆行/加距，属排布口径而非缺陷。

## 仍遗留（已定性，未修）

- **P1-1 字体下拉叠印**：探针定性为绘制引擎单次 `drawText` 产出双份字形
  （三个文本 API 全程零「Noto Sans」非 Gurmukhi 调用）——需 XFontFt/painter
  渲染专项，根因未定位到具体代码行。
- **QDockWidget/QMenu/QStatusBar/QToolTip/XStackedWidget 的 sizeHint**（Qt
  有、XGui 缺；当前无布局暴露面——demo 内均定尺寸使用）。
- **页级滚动推广**：ATV61 已落地，views/窗口设置/键盘演示等页待推广。
- **Xvfb 无 WM 下弹层点击路由异常**（诊断期间遇到：下拉项点击不触发）——
  未查清是否框架问题，疑似无 WM 环境的输入路由限制。
- **安卓真机未验证本轮全部框架改动**：设备离线（192.168.10.58 No route to
  host / Connection refused），仅编译+出包完成，APK 在
  platforms/android/out/XGuiDemo-debug.apk（13.1 MB）。


## 第四轮：P1 字体下拉叠印的框架根修（2026-10-27）

**归因（探针逐级收敛）**：
①XPainter 文本 API 探针（drawText/drawTextRect/drawGlyph 全量打印）在
输入页**只抓到 XLabel 的文本、抓不到任何 combobox 文本**——闭态文本走
XComboBox.paintEvent 的 drawText 调用，但探针没命中；
②改在 XComboBox.paintEvent 挂探针 → **抓到**：`idx=0 text=Noto Sans
Gurmukhi`，同一控件被 paint 3 次，**当前项文本无冒号**；而画面上另一串
带冒号（"Noto Sans: Gurmukhi"）⇒ 两串不同源；
③对照 Qt 6.8.3 **QCommonStyle::drawControl(CE_ComboBoxLabel)**
（qcommonstyle.cpp:2250）：闭态文本**仅非可编辑时**绘制——
`if (!cb->currentText.isEmpty() && !cb->editable)`；可编辑态文本由
**内嵌 lineEdit** 承载（lineEdit 子控件占据编辑区并自绘）。

**根因**：XFontComboBox 构造即 `setEditable(true)`（对齐
qfontcombobox.cpp:403），而 XGui 的 `VXComboBox_paintEvent` **无条件**画
闭态当前项文本 ⇒ 与内嵌 XLineEdit **在同一位置双绘**，两串字形互相叠印、
互不可读。

**修（框架层）**：`VXComboBox_paintEvent` 在 `m_editable` 为真时**不画**
闭态文本（非 editable 路径逐位不变）。验证：输入演示页字体下拉叠印消失
（截图确认只剩单串 "Gurmukhi"——内嵌 edit 框光标在末尾故显示族名尾部，
与 Qt editable 语义一致）。

**回归**：all 门 RC=0、orient 门零新增；44 张双形态像素对比——editable
修前后横屏各页 ≤0.27%，XMenu sizeHint 前后平均 0.08%，**布局零劣化**。

**顺带澄清的「欠账」**：对照 Qt 源码确认 **QStatusBar / QToolTip /
QStackedWidget / QDockWidget 在 Qt 里都没有自己的 sizeHint**（继承
QWidget 缺省空尺寸）⇒ XGui 缺它们**不是对齐欠账**，不必补（避免为
不该补的控件瞎改）。真正欠账的只有 **QMenu**（qmenu.cpp:2260-2276
sizeHint = 动作矩形并集）——已补（XMenu_updateSizeHint）。

## 仍遗留（已定性/已判非缺陷）

- **Xvfb 无 WM 下弹层点击不生效**：弹层点击的框架逻辑本身正确（14.115
  已修为本地坐标判界），且**真机（Android 触摸）与 Windows 桌面均未报过
  「下拉点不中」**——只在无 WM 的自动化环境复现，判为**测试环境限制**，
  未改框架（贸然改有回归风险）。若真机复现再立项。
- **页级滚动推广**：ATV61 页已落地 scroll 壳；views 页因**紧凑/宽档双
  装配互移**（compact/wide 互搬布局条目）与 scroll 内容根交互未收尾，
  推广有回归风险，本轮未做——留待专项（受益页：条目视图/窗口设置/键盘
  演示等窄档内容超高页）。
- **安卓真机未验证本轮全部框架改动**：设备离线，仅编译+出包（APK 13.1MB
  在 platforms/android/out/）。
