# XGuiDemo 夜间全量测试修复 · 最终报告（2026-09-23 23:40 ~ 2026-09-24 07:40）

## 总览
- **循环**：3 轮完整「全量扫描→统一修复→复扫验证」+ 1 轮定点收口，全程免费窗口内，零空转。
- **发现问题**：76+ 条（第一轮 77 原始/68 去重 + 复扫新发现 8+）
- **修复落地**：79 条次全部 Qt 6.8.3 源码对照口径（本机 /home/xinyue/Qt/6.8.3/Src），每条过 compile_commands 同口径 -fsyntax-only
- **验证转绿**：76 项次复扫实测（截图/像素/gdb 证据，全部亲验）——首轮复扫 38、二轮复扫 19、三轮复扫 16（含多项回归抽查维持）
- **全程验证门**：每轮重建 0 error + 三套件（回归/验收/GPU）+ autotest 135 PASS；最终态全绿
- **git**：零写操作（暂存区用户批次原样未动）

## 三轮循环明细
| 轮次 | 修复 | 复扫验证 | 证据 |
|---|---|---|---|
| 第一轮 | 48+6 条（五开扫描 77 原始/68 去重） | 38 转绿 / 19 broken→第二轮 | rescan_r1_lane_*.md |
| 第二轮 | 14 条（按复扫钉死根因） | 19 转绿 / 4 partial+4 broken→第三轮 | rescan_r2_lane*.md |
| 第三轮 | 11 条 + 主线定点 3 处 | 16 转绿 / 2 partial+3 broken→日间清单 | rescan_r3_lane*.md |

## 已验证转绿的代表性修复（全部对照 Qt 真实现）
- **弹层族**：XComboBox/XMenu/ToolTip 上屏（跨深度遮挡根因：弹层 depth-32 被同深度主窗排除合成，改屏幕默认 visual，对标 qxcbwindow createVisual）
- **键盘族**：平台 Ctrl/Space/Tab 键三段修复（直映键双通道分工、Ctrl 守卫、Tab 白名单），Tab 焦点链启动+生效可见性过滤（对标 qwidget.cpp focusNextPrevChild）
- **选择/焦点族**：ClearAndSelect、视图 StrongFocus+点击聚焦、树子条目高亮、树滚轮/表头钉顶
- **对话框族**：消息框分级图标+标题、模态拦截（isWindowBlocked 等价）、Tab 闭环+Return 聚焦钮、输入框所见即所回+初始聚焦、文件/颜色框居中公式根修
- **布局族**：XStackedLayout 切页几何、XScrollArea ShowEvent/WheelEvent、XToolBox 页高、XSplitter 拖拽+off-by-one、XStatusBar 排版、XLcdNumber qlcdnumber 几何、MDI exposedRect 脏区
- **效果管线**：透明/模糊/投影首启正确（插桩定案+真机像素终验：50% 公式逐点核对）
- **图表**：样条 Catmull-Rom 控制点、图例同色、折线 plotArea 裁剪、轴标签钳位
- **杂项**：XToolButton DelayPopup、XToolTip 全链（唤起/上屏/自动隐藏）、XShortcut、KSE 修饰键、XSizeGrip、XMainWindow 背景、XLabel autoFillBackground、字形回退定位、菜单栏高亮复位、XWidget_setParent 保几何（Qt 源码核实）、按钮按压态/hover、FPS 浮层移位

## 遗留（日间清单）
1. **#32 裸字母键**：平台大写归一已落（Ctrl+字母快捷键通）；裸字母仍被 IME 提交分支吞——需控件层文本推导改 Shift 派生（XLineControl/XTextControl.c），涉及文本插入语义，建议日间处理
2. **#41 残**：页3 Tab 链 SpinBox→Slider 一跳仍断（点击聚焦+Right 已通；冷启动 Tab 首停=导航钮属创建序正常）。锚定修复已验 Tab① LE→SpinBox ✓；SpinBox→Slider 一跳根因收窄=复合控件焦点策略链审计（SpinBox 容器/内部编辑器/Slider 的 TabFocus 位），日间按策略位逐个排查
3. **#40 残→已修待回归**：Tab 从文本控件出发的续链锚定（XWidget_focusChainTarget 最近可聚焦祖先为锚，对标 Qt）已落，Tab① LE→SpinBox+Up 步进真机验证通过
4. **#8 残**：投影效果下点击交互擦除（首启已正确；效果激活态交互重绘路径，gdb 可复现）
5. **#38 字形（设计裁定）**：内置字库=Latin+GB2312 一级 3755 字，「渲」等二级字缺失；方案 A=挂全字库 XFO provider（XFontOutlineFace_registerProvider 已支持）、方案 B=系统字形后端（fontconfig/freetype）
6. **#50 附加**：MDI 子窗拖拽位移未实现（库层无鼠标处理，功能性新增）；#31 伴随 KSE 捕获后 kseStatus 置空（demo 侧）；#28 低危颜色框标题遮半
7. **终验通过（07:50 主线亲验）**：#35 ToolTip 浅底黑字横排正常、#37 SizeGrip 拖拽 800x600→950x730、#40 Tab 进 SpinBox+Up 步进

## 覆盖增量
基线逐控件 → 键盘/焦点链 → 边界值/滚轮/拖拽 → 样式矩阵 → autotest/apitest → 软件 vs GPU_SYNC PSNR 对照 → 效果挂摘循环 → 交互后回归抽查（8 项抽查零退化）。覆盖台账见 coverage_ledger.md。

## 验证方法沉淀（本轮新增）
- 跨文件调用只许头文件公开声明（静态函数链接失败教训×1）
- find=bfs：-newermt 相对时间无效用 -mmin
- 多路并存禁 pkill -x demo（互杀），只杀自己 PID
- 像素级复核撤误报 5 条（坐标口径/放大误读），「自动全绿」与手测缺陷并存的根因=autotest 覆盖面不足
- xtrace/最小客户端矩阵/独立探针三件套定位 X Server 跨深度遮挡

## 遗留更正（2026-09-24 07:45，复扫-3 后定点收口的最终口径）
以上「遗留」节的 #37/#35/#40 状态已被后续工作更新，以此节为准：
1. **#37 SizeGrip：已终验通过**——真凶=VDemoWin_resizeEvent 每次 resize 重锚 BottomRight 覆盖 init TopRight（gdb 双证：浮层移走后拖拽 800x600→950x730 完全正常）；主线已把 resizeEvent 同改 TopRight
2. **#35 ToolTip：已终验通过**——浅底黑字横排提示框在光标旁正常显示（XToolTip 补 XWindowType_ToolTip 进默认 visual 族 + autoFillBackground 底填充）
3. **#40 焦点锚定已落**：focusChainTarget 非候选控件以最近可聚焦祖先为锚（Tab① LE→SpinBox+Up 步进真机验证 ✓）
4. **残点（日间，需 gdb 跟 focusChainTarget 候选收集）**：Tab 从文本控件出发仍落文档序首候选（LE 未入候选集原因待查：m_visible/策略位逐项审计）；SpinBox→Slider 一跳为其下游
5. **#8 投影效果激活态点击擦除**（首启已正确；效果重绘路径，gdb 可复现）——日间
6. **#50 附加** MDI 子窗拖拽（功能新增）、**#31 伴随** KSE 捕获后 kseStatus 置空（demo 侧）、**#28** 颜色框标题遮半（低危）——日间
7. **设计裁定待用户**：#32 裸字母键（控件层文本推导配套）、#38 字形（全字库 provider 或系统字形后端）

## #8 效果激活态交互擦除——攻坚告捷（2026-09-24 08:00，两轮未破终破案）
**双缺陷叠加**（缺任一证据链都拼不全，M6b 插桩副本复现+修复验证）：
- 根因A：XWidget_repaintRegion 表面裁剪外接框计算错误——多矩形脏区 {FPS浮层, 按钮, 复选框} 被算成 (32,384 236x50) 而非 (32,384 742x190)（原循环以"远边越过当前 x+width"增长，后续矩形拉走近角后右/下边界被静默收窄）→ 按钮重绘整段被裁、flush 回贴 stale = 永久消失。已修（独立 maxRx1/maxRy1 追踪）。
- 根因B：XPainter 表面裁剪兜底（:3426）不按目标图像过滤，设备坐标裁剪错施到效果管线 230x36 离屏画布 → 回贴全透明画布 = 复选框消失。按 Qt 语义修在效果管线自身（离屏段摘除表面裁剪+回贴自限，对标 QGraphicsEffectSource 语义；XPainter.c 统一修法留档建议日间评估——grab/保留层/内容缓存等离屏路径有同类风险）。
验证：插桩副本真机复现→修复→点击后勾选/文字/投影/按钮全完好（上轮 ≥2.5s 不自愈问题消失）；首启序列逐字节无回归；-fsyntax-only 过。

## 第四轮残项清零 + #32 闭环（08:50 最终态）
第四轮 4/4 fixed：#32 裸字母键全链（平台直映+大写归一+控件层 Shift 派生+夹具契约，回归一度现形已闭环）/#50 附加 MDI 子窗拖拽/#56 ErrorMessage 警示样式/#28 颜色框标题遮半。**#8 效果激活态交互擦除攻坚告捷**（双缺陷：repaintRegion 外接框收窄 + 表面裁剪错施离屏画布，插桩副本验证）。
**最终验证态（08:45）**：重建 0 error / 回归 0 FAIL / 验收 0 FAIL / GPU 0 FAIL / autotest 135 PASS——累计修复 83+ 条次，全部 Qt 源码对照。
**真残点（量少而清晰）**：①#8 主树重建后活体复验一次（插桩副本已验）②#50 MDI 拖拽、#28 标题、#56 样式活体复验（重建已含，未逐项截图）③#38 字形方案裁定（全字库 provider / 系统字形后端）

---

# 附录A：问题台账全量流水（夜间过程记录，83 条次状态流转）

# 夜间全量测试问题台账（2026-09-23 夜）

状态定义：new=待修复 / fixing=修复中 / fixed=已修复待复扫 / verified=已修复且复扫+回归通过 / false-alarm=复核为环境问题或误报
标注：加粗「主线亲验✓」= 主线亲自 Read 截图确认；其余为路内子代理亲验（报告含证据链）。

## lane0（页0/1/2 + autotest 对照）——5 条

| # | 位置 | 控件 | 症状 | 类别 | 置信度 | 截图 | 疑似位置 | 状态 | 备注 |
|---|------|------|------|------|--------|------|----------|------|------|
| 1 | 页0 按钮演示 | XToolButton「工具按钮」 | 单击/长按/二次点击共7次、3个独立实例，弹出菜单从不出现、无按压态、联动不变；同页他钮正常 | functional | high | /tmp/sweep_r1_lane0/p0_tool_held.png | Src/XGui/Widget/XToolButton.c:661-680（弹层分支 accept+return 吞掉整条点击链且无回退） | new | **主线亲验✓** |
| 2 | 页2 堆叠演示 | m_stackPageTwo | 切内层第2页文本错位到容器左上角，应为(52,122,320,110)内居中；状态栏正确；复现2/2 | visual | high | /tmp/sweep_r1_lane0/p2_prev1.png | Src/XGui/XLayout/XStackedLayout.c:151-154 + xgui_window_demo.c:1340-1396 | new | **主线亲验✓** |
| 3 | 全局键盘 | 全体可聚焦按钮 | Tab 键到达应用但焦点链不启动：5轮 Tab+Space 零反应、无焦点框 | functional | high | /tmp/sweep_r1_lane0/p0_tab1_space.png | Src/XGui/Widget/XWidget.c:2492-2505（对标 Qt 窗口级 focusNextChild） | new | **主线亲验✓**；与 #13 疑同根，一并修 |
| 4 | 页0（全局样式） | m_button | XPushButton:hover 高亮不生效（enter 事件已到，#3D8BFD 未出现） | visual | medium | /tmp/sweep_r1_lane0/p0_hover_on.png | XCssStyleSheet.c:263-264 → XCommonStyle.c:1774 链路 | new | 先复核 MouseOver 状态置位 |
| 5 | 页0 | m_button | 按压态视觉过弱：difference 亮度均值 2.2/255≈1%，肉眼不可辨 | visual | medium | /tmp/sweep_r1_lane0/zoom_pressed.png | XPushButton.c:427-436 | new | 缺陷 vs 风格取舍，修复阶段裁定 |

## lane3（页5 条目视图 + 页8 图形效果）——13 条

| # | 位置 | 控件 | 症状 | 类别 | 置信度 | 截图 | 疑似位置 | 状态 | 备注 |
|---|------|------|------|------|--------|------|----------|------|------|
| 6 | 页8 效果·组① | 透明按钮+OpacityEffect(0.5) | 首次启用按钮近乎消失仅余 1px 细线；禁用→再启用后 50% 正确；他组启用引发重绘再打碎 | visual | high | /tmp/sweep_r1_lane3/e02_g0_on.png | XWidget.c:5684-5691/:6150 效果管线快照 + XGraphicsOpacityEffect.c:25 | new | **主线亲验✓** |
| 7 | 页8 效果·组② | 模糊标签+BlurEffect | 启用后 220x52 标签必塌成横贯粗线+墨渍，三次启用无一帧正确；禁用即恢复 | visual | high | /tmp/sweep_r1_lane3/zoom_e16_blur2.png | XGraphicsBlurEffect.c:53-64（source 快照绘入尺寸/步距） | new | **主线亲验✓** |
| 8 | 页8 效果·组③ | 投影复选框+DropShadow(6,6) | 首次启用整个消失；off→on 后投影正确；效果下点击触发重绘又消失（第二实例复现） | visual | high | /tmp/sweep_r1_lane3/zoom_e07_g2_on_cb.png、zoom_e14_g2_check_undereffect_cb.png | 同 #6 效果管线 | new | 与 #6 同根 |
| 9 | 页8 效果 | 模糊/基线标签 | 代码设钢蓝 Window 底色+autoFillBackground，实际恒白底，基线对照语义失效 | visual | medium | /tmp/sweep_r1_lane3/e01_baseline.png | xgui_demo_page_effects.c:336-347 + XLabel.c 绘制（:745 仅前景角色） | new | 按 Qt 语义应显蓝 |
| 10 | 页5 条目视图 | XListWidget | 单选残留：点樱桃→榴莲后双行同蓝（第二实例复现）；期望 ClearAndSelect | functional | high | /tmp/sweep_r1_lane3/v03_list_row3.png、v17_list_double_confirm.png | XAbstractItemView.c:311-328（setCurrentIndex 不 clear）+:1696-1700 | new | 对照：表格互斥正常 |
| 11 | 页5 条目视图 | XListView | 同款单选残留：A2→A1 后双蓝 | functional | high | /tmp/sweep_r1_lane3/v05_view_row0.png | 同 #10 | new | 同 #10 一并修 |
| 12 | 页5 条目视图 | XListWidget | 选中后 Down/Up 方向键当前项不动（对照鼠标点击正常，排除通路问题） | functional | medium | /tmp/sweep_r1_lane3/v27_arrow_down.png | XAbstractItemView.c:25 keyPressEvent；疑焦点未交给视图 | new | 与 #3/#13 焦点链关联 |
| 13 | 页5 条目视图 | 全页 | 键盘焦点完全不可见：Tab×1..3 与基线逐像素一致（PSNR 差异全来自 FPS 浮层） | visual | medium | /tmp/sweep_r1_lane3/v22_tab1.png | 未定位（焦点框绘制/装配缺失） | new | 疑与 #3 同根，一并修 |
| 14 | 页5 条目视图 | XTreeWidget | 节点点击无任何选中高亮/当前项描边，与列表/表格蓝条反馈不一致 | visual | high | /tmp/sweep_r1_lane3/v06_tree_dev.png、v13_tree_child2.png | XTreeWidget.c:1038 附近绘制段（isSelected 仅用于勾选框） | new | |
| 15 | 页5 条目视图 | XTreeWidget | 全展开内容 140px>控件 130px，末行「键盘」下半硬裁且无滚动条 | visual | medium | /tmp/sweep_r1_lane3/zoom_v01_tree.png | XTreeWidget.c:1652-1665（vbar range 有计算无呈现） | new | |
| 16 | 页5 条目视图 | XTreeWidget+状态行 | 点子节点「网卡」状态行报顶层「设备」，子节点层级在反馈中不可分辨 | functional | medium | /tmp/sweep_r1_lane3/v13_tree_child2.png | XTreeWidget.c:107 xtw_topLevelRowOf 只发顶层行号 | new | 信号契约 vs 页面槽，修复阶段裁定 |
| 17 | 页5 条目视图 | XListWidget | 悬停行无高亮反馈 | visual | low | /tmp/sweep_r1_lane3/v21_hover_liulian.png | XListView.c 悬停绘制未定位 | new | 可能是样式取舍 |
| 18 | 页5 条目视图 | XHeaderView | setSectionsClickable(true) 后点击两段均无任何反应（信号无发射点） | functional | low | /tmp/sweep_r1_lane3/v18_header_click.png | xgui_demo_page_views.c:595-601/:757-759（注释自认无发射点） | new | 行为确凿、定级 low（声明边界） |

## lane4（页6 对话框 + 页7 高级控件）——21 条

| # | 位置 | 控件 | 症状 | 类别 | 置信度 | 截图 | 疑似位置 | 状态 | 备注 |
|---|------|------|------|------|--------|------|----------|------|------|
| 19 | 页6 消息框×4 | XMessageBox | 信息/警告/错误/询问四种级别图标全部不渲染 | visual | high | /tmp/sweep_r1_lane4/dlg_info_open.png | XMessageBox.c:294-303（setIcon 仅存 m_icon 无绘制调用） | new | |
| 20 | 页6 对话框 | XMessageBox/XDialog | setTitle 标题不可见（子控件形态无标题栏） | visual | low | /tmp/sweep_r1_lane4/dlg_info_zoom.png | XGui.md:776 设计边界；XMessageBox.c:273-281 | new | Qt 对标性存疑，修复阶段裁定 |
| 21 | 页6 自定义对话框 | XDialogButtonBox | 按钮绘制位置与命中区错位约 28px：点视觉中心无效，点下方 28px 生效 | visual | high | /tmp/sweep_r1_lane4/a5_cust_click_visual.png、a5_cust_click_layout.png | xgui_demo_page_dialogs.c:551 + XDialogButtonBox.c:166-182 relayout 与面板绘制原点不一致 | new | 伴随功能性误点击 |
| 22 | 页6 模态 | XDialog 模态 | 应用模态不拦鼠标：模态框开着仍可叠开其他对话框 | functional | high | /tmp/sweep_r1_lane4/a4_msg_tab1.png | XDialog.c:421-435（setApplicationModalWidget 后鼠标分发未拦截） | new | |
| 23 | 页6 消息框 | 模态键盘路由 | 框内 Tab×2 后焦点逃出模态子树，Esc 随即失效、对话框滞留 | functional | high | /tmp/sweep_r1_lane4/a6_tab2_esc.png | 未定位（XDialog 键盘路由）；与 #3 同根链 | new | Tab×1→Esc 正常（对照） |
| 24 | 页6 消息框 | 确定/取消 | 框内 Tab 焦点框从不移动到「取消」，Return 恒触发确定 | functional | medium | /tmp/sweep_r1_lane4/a5_tab1.png、a5_tab2.png | 未定位（与 #23 同根） | new | |
| 25 | 页6 输入对话框 | XInputDialog | 输入框实见「预置文本 123」，确认后回传 文本=""（所见非所回） | functional | high | /tmp/sweep_r1_lane4/b1b_input_typed.png、b1b_input_enter.png | xgui_demo_page_dialogs.c:376-388 getText_2 返回值链路 | new | |
| 26 | 页6 输入对话框 | 输入框焦点 | 初始焦点在「确定」而非输入框，直接打字无效；Qt 应聚焦输入框 | functional | medium | /tmp/sweep_r1_lane4/b1_input_open.png | XInputDialog 初始焦点逻辑 | new | |
| 27 | 页6 文件对话框 | XFileDialog | 对话框溢出主窗底缘被裁：文件名框切成黄边、确定/取消完全不可见，只能 Esc | visual | high | /tmp/sweep_r1_lane4/b2_file_open.png | XFileDialog.c 尺寸/居中计算 | new | **主线亲验✓**；功能受损 |
| 28 | 页6 颜色对话框 | XColorDialog | 同款溢出底缘：预览色块下半被裁、按钮不可见 | visual | high | /tmp/sweep_r1_lane4/b3_color_open.png | XColorDialog.c 尺寸/居中计算 | new | 与 #27 一并修 |
| 29 | 页6 颜色对话框 | getColor_2 | Esc 取消后回传 RGB(0,0,0)（初值 70,130,180），取消语义无 ok 出参 | functional | low | /tmp/sweep_r1_lane4/b3_color_esc.png | xgui_demo_page_dialogs.c:423-438 | new | 便捷函数契约，修复阶段裁定 |
| 30 | 页7 补全 | XLineEdit+XCompleter | 键入 Ope 状态行候选数正确但无弹层；Down 不换候选；Return 不回填（整条弹层链路缺失） | functional | high | /tmp/sweep_r1_lane4/c1_completer_down.png、c1_completer_enter.png | xgui_demo_page_advanced.c:706-708；XLineControl/XCompleter 弹层落地 | new | ⚠记忆：g20 默认弹层已落地且 apitest 过（断言先 show 顶层）——真实页面路径没弹，修复前先按记忆口径复核差异 |
| 31 | 页7 快捷键捕获 | XKeySequenceEdit | 点击不聚焦（焦点留补全框），Ctrl+O 不被捕获，完全不可用；两次复现 | functional | high | /tmp/sweep_r1_lane4/c3_kse_focus.png、c3_kse_capture.png | XKeySequenceEdit.c 鼠标按下不请求焦点 | new | |
| 32 | 页7 全局快捷键 | XShortcut(T) | 恒不触发（空白处按 T 无反应；编辑框内 T 被当字符插入） | functional | high | /tmp/sweep_r1_lane4/c5_shortcut_t.png | xgui_demo_page_advanced.c:779-787 + XShortcut 真实按键接入边界 | new | 页面文案承诺「按 T 触发」：改文案或接真键路由，修复阶段裁定 |
| 33 | 页7 主窗口 | XMainWindow | 顶层窗口除菜单栏与停靠标题条外整片黑块，中央/左右停靠内容全不可见；三次复现 | visual | high | /tmp/sweep_r1_lane4/c2_mainwin.png | xgui_demo_page_advanced.c:434-512 + XMainWindow 顶层窗口背景未填充 | new | **主线亲验✓** |
| 34 | 页7 主窗口菜单 | 文件(&F) 菜单 | 点击无下拉弹层（窗口枚举无新增）；三次复现 | functional | medium | /tmp/sweep_r1_lane4/c3_mw_menu.png | XMenuBar/XMenu 弹出路径（顶层窗口形态） | new | 接线与主文件一致，疑弹出层在顶层窗口形态的落地 |
| 35 | 页7 悬停提示 | XToolTip | 渲染成约 30px 宽 375px 高的蓝色竖条贯穿页面，文字不可读（疑宽高颠倒/逐字竖排） | visual | high | /tmp/sweep_r1_lane4/c3_tooltip.png | XToolTip.c showText 尺寸计算 | new | |
| 36 | 页7 橡皮筋 | XRubberBand | 真实拖拽仅出现 1x1 小点不更新矩形；释放后小点残留 | functional | medium | /tmp/sweep_r1_lane4/c3_rubber_drag.png、c3_rubber_released.png | 事件直发 autotest 过但真实 MOVE 分发未达页面根控件 | new | 与真实鼠标 MOVE 分发链相关 |
| 37 | 页7 尺寸手柄 | XSizeGrip | 拖出 145x120px 窗口尺寸不变；手柄悬空于页面角而非窗口角、被 FPS 浮层遮盖 | functional | medium | /tmp/sweep_r1_lane4/c5_grip_after.png、z_grip.png | xgui_demo_page_advanced.c:893-902 + XSizeGrip.c | new | |
| 38 | 页7 字形 | 字体回退 | 「渲」字空白；「§」→S；「——」→--；「→」→> | visual | high | /tmp/sweep_r1_lane4/z_textedit.png、z_hint.png | 字体缺字形回退（XFont/XFontMerge 或内嵌字库覆盖） | new | |
| 39 | 页7 启动画面 | XSplashScreen | 黑底画深灰 0xFF202020 文字，对比度过低近乎不可读（生命周期本身正常） | visual | low | /tmp/sweep_r1_lane4/c2_splash_show.png | xgui_demo_page_advanced.c:399-403 | new | |

## lane1+lane2（页3 + 页4 全 21 页签，两路去重后）——29 条
说明：两路重叠的 9 条（tabStatus 死代码、页签2 数码管、页签3 滚动条、页签4 滚动区、页签13 浏览器、页签14 MDI、页签16 堆叠组B、页签18 Error、按压态过弱）已并入下列合并条目；按压态过弱并入 #5。

| # | 位置 | 控件 | 症状 | 类别 | 置信度 | 截图 | 疑似位置 | 状态 | 备注 |
|---|------|------|------|------|--------|------|----------|------|------|
| 40 | 页3 输入演示 | XSpinBox 内部编辑框 | Tab 进入后编辑框整白、值文本完全不可见；Tab 离开恢复；鼠标点击进入正常 | visual | high | /tmp/sweep_r1_lane1/p3_focus_spin.png | XAbstractSpinBox.c:338 focusInEvent 仅转发父类，内部编辑框获焦重绘缺失 | new | |
| 41 | 页3 输入演示 | XSlider | Tab 焦点链跳过滑块且无焦点指示，Right 方向键值不变；Qt QSlider 默认 StrongFocus+方向键步进 | functional | high | /tmp/sweep_r1_lane1/p3c_slidekey.png | XSlider.c 无 setFocusPolicy 调用（默认 NoFocus），XAbstractSlider.c:121 键盘处理永不触发 | new | |
| 42 | 页3（机制影响所有文本控件） | XLineEdit 键翻译 | Ctrl+Z 不撤销反插入一个空格（11→Ctrl+Z→22 得「11 22」）；Ctrl+A 全选 5 次全无效；Shift+End 正常，Ctrl 特异失败 | functional | high | /tmp/sweep_r1_lane1/p3d_s2.png、p3c_ctrla_sel.png | 平台层按键翻译（仓库未检索到 XLookupString，后端未定位）；应用层 XLineControl.c:3460-3467 从未命中 | new | 重大：影响全部文本控件的 Ctrl 组合键 |
| 43 | 页3 状态行 | m_inputStatus | 点 SpinBox 箭头路径滑块/进度条同步但状态行绘制空白（初始「就绪」也消失）；键入/凹槽路径正常 | visual | medium | /tmp/sweep_r1_lane1/p3c_up.png、zoom_p3c_up_status.png | xgui_window_demo.c:1610-1626 写入链 + XSpinBox.c:588 触发，标签重绘丢失 | new | |
| 44 | 全页右下角 | FPS 调试浮层 | 文本超宽被窗口右缘裁剪（「上传 11.9 M」）；Wizard 页签下盖住「取消/完成」按钮右半 | visual | medium | /tmp/sweep_r1_lane1/p4_scroll2.png、p4r5_wiz_retry2.png | xgui_window_demo.c:2079-2090（编译开关浮层几何） | new | demo 侧，主线统一改 |
| 45 | 页4 底部状态行 | m_tabStatus | 切任意页签恒「就绪」，应显「页签: N」；demo_tab_changedSlot 已定义从未 connect（死代码）——两路独立确认 | functional | high | /tmp/sweep_r1_lane1/p4_t1.png、/tmp/sweep_r1_lane2/p4_t01_click.png | xgui_window_demo.c:1667（定义）与 :2390-2825（装配无 connect） | new | demo 侧一行接线，主线改 |
| 46 | 页4 页签2 数码管 | XLcdNumber | 无数字显示+黑块越出右缘（lane1）；被拉伸整页 752x430、段码 26px 粗黑条占右半（lane2）；两次启动复现 | visual | high | /tmp/sweep_r1_lane1/p4_t2.png、/tmp/sweep_r1_lane2/p4_t02_lcd_a.png | XTabWidget.c:59（直插 client 几何覆盖整页）+ 绘制缓存分段偏移旧病（xgui_window_demo.c:2428-2432 注释自述） | new | 两路口径合并 |
| 47 | 页4 页签3 滚动条 | XScrollBar | 几何错位：编程 (10,80,24,180) 实测渲染约 (30,108)-(42,290) 宽仅 13px 贴顶、无凹槽无端钮（lane1：7px 发丝线） | visual | high | /tmp/sweep_r1_lane1/p4_t3.png、/tmp/sweep_r1_lane2/p4_t03_sb_base.png | xgui_window_demo.c:1511 + XTabWidget.c:54-59（页/客户双重布局） | new | 两路口径合并 |
| 48 | 页4 页签4 滚动区 | XScrollArea | 六行内容不可见：全白（lane1）/裁成左缘 ~3px 细条（lane2）；无滚动条、滚轮 3 次无响应 | visual | high | /tmp/sweep_r1_lane1/p4_t4.png、/tmp/sweep_r1_lane2/p4_t04_scroll_base.png | XScrollArea.c:26-96（viewport 布局）+ XTabWidget.c:59；demo 侧 XLabel 未 show（xgui_window_demo.c:2456-2461） | new | 两路口径合并 |
| 49 | 页4 页签13 浏览器 | XTextBrowser | 内容区全白，「帮助内容/第二段/第三段」不渲染——两路独立确认 | visual | high | /tmp/sweep_r1_lane1/p4r4_t13.png、/tmp/sweep_r1_lane2/p4_t13_browser.png | xgui_window_demo.c:2565-2571 + XTextBrowser 内部 editor 显示链 | new | |
| 50 | 页4 页签14 MDI | XMdiArea | 子窗口「文档1/文档2」均不可见，仅左上角蓝色细线/空盒残迹——两路独立确认 | visual | high | /tmp/sweep_r1_lane1/p4r4_t14.png、/tmp/sweep_r1_lane2/p4_t14_mdi_base.png | XMdiArea.c 子窗布局/绘制 | new | |
| 51 | 页4 页签栏 | XTabBar | 白底「当前页签」样式恒滞留 index0 不随切换迁移（--tab 3 直启 index0 从未选中亦复现）；真实当前页签仅描边难辨认 | visual | high | /tmp/sweep_r1_lane1/zoom_p4t1_bar.png、zoom_p4t3_bar.png | XTabBar.c:433-456 isCur 经 XStyleCE_TabBarTabShape 绘制，疑 Selected 判定/缓存 | new | |
| 52 | 页4 页签20 图表 | 网格按钮/axisY gridVisible | 关网格→随后仅点「标题」→网格线自行恢复显示 | functional | medium | /tmp/sweep_r1_lane1/p4r4_t20_grid.png → p4r4_t20_title.png | XChartView_updateChart 重建未保留轴网格开关 | new | |
| 53 | 页4 页签16 堆叠组 | 选项B XCheckBox | 勾选 B 瞬间 B 视觉消失（lane2）；点两行间空白后 B 消失（lane1）；再点可恢复、互斥逻辑正常 | visual | medium | /tmp/sweep_r1_lane2/p4_t16_clickB.png、/tmp/sweep_r1_lane1/p4r4_t16_b.png | XButtonGroup.c 互斥链 / XCheckBox.c 重绘 | new | 两路触发口径合并 |
| 54 | 页4 页签1 旋钮 | XDial | 点击旋钮 6 点方向得值 0；Qt QDial 270° 弧 6 点应约 50 | functional | low | /tmp/sweep_r1_lane1/p4r3_t1_dial.png | XDial 角度-值映射未定位 | new | |
| 55 | 页4 页签6 工具箱 | XToolBox | 页项内容文本出现在内容区中部（y≈318）而非激活页头正下方 | visual | low | /tmp/sweep_r1_lane1/p4_t6.png | XToolBox 内容区布局未定位 | new | |
| 56 | 页4 页签18 Error | XErrorMessage | 「Test error message」白底黑字普通文本，无图标/警示色，错误语义弱——两路独立确认 | visual | low | /tmp/sweep_r1_lane1/p4r4_t18.png、/tmp/sweep_r1_lane2/p4_t18_error.png | XErrorMessage.c 样式 | new | |
| 57 | 页4 页签19 表格 | XTableWidget | 单击第 3 行「类型」列后同行「名称」列一并高亮，选区视觉跨 2 列 | visual | low | /tmp/sweep_r1_lane1/p4r4_t19.png | XTableWidget 选区绘制未定位 | new | |
| 58 | 页4 页签0 下拉 | XComboBox 弹层 | 弹层窗口已映射(150x62)且缓冲有 Option 1/2/3，但屏幕完全不可见；盲点第二行可选中且联动正常 | visual | high | /tmp/sweep_r1_lane2/p4_combo_open_root2.png | XComboBox.c:1732-1744（popupShow/flushBackingStore 不上屏） | new | 重大：所有弹层族 |
| 59 | 页4 页签8 菜单+页签3 右键 | XMenu 弹层 | 菜单弹层映射而不可见(56x20，缓冲有「退出」)；Esc 不关闭、点击外部不关闭，文件项保持高亮 | functional | high | /tmp/sweep_r1_lane2/p4_t08_menu_open_root.png | XMenu.c 弹层上屏/关闭路径（与 #58 同类） | new | 与 #58 同根一并修 |
| 60 | 页4 页签5 分割 | XSplitter 把手 | 把手纯装饰不可拖：两次按住拖动分隔条不动；源码无鼠标事件处理 | functional | high | /tmp/sweep_r1_lane2/p4_t05_split_base.png | XSplitter.c 全文件无 MousePress/Move 覆盖，m_dragIndex :308 死状态 | new | |
| 61 | 页4 页签5 分割 | XSplitter 绘制 | 页面右缘（页右界之外 x≈776-780）多出一条与把手同款灰竖线+凹槽白点，疑把手循环 off-by-one | visual | medium | /tmp/sweep_r1_lane2/p4_t05_rightedge.png | XSplitter.c:191-211 绘制循环 | new | |
| 62 | 页4 页签10 日期时间 | XDateTimeEdit | Up 步进生效但文本重绘滞后一拍：连按 3 次 Up 仍显 2026，再交互一次才显 2030 | functional | medium | /tmp/sweep_r1_lane2/p4_t10_dt_up.png | XDateTimeEdit.c 按键步进后 update 链 | new | |
| 63 | 页4 页签20 图表 | XSplineSeries | 样条在 (3,48) 数据点附近自交成小环（单调 x 三点样条不应自交） | visual | medium | /tmp/sweep_r1_lane2/p4_t20_spline.png | XSplineSeries.c 插值过冲 | new | |
| 64 | 页4 页签20 图表 | 图例/系列颜色 | 图例「月销」色板蓝紫，实际柱形与折线同为青色（图例色≠实际色），区分度低 | visual | medium | /tmp/sweep_r1_lane2/p4_t20_legend.png | XChart.c/XBarSeries.c 系列默认色与图例不同源 | new | |
| 65 | 页4 页签20 图表 | 折线裁剪 | 「范围」切 0-30 后 >30 的折线点不裁剪，线段越出绘图区顶部画到按钮行（柱形正确裁剪） | visual | high | /tmp/sweep_r1_lane2/p4_t20_btn_range.png | XChartView.c/XLineSeries.c 折线无 plotArea 裁剪 | new | |
| 66 | 页4 页签20 图表 | X 轴标签 | 最右刻度标签「10」一半被窗口右缘裁掉 | visual | low | /tmp/sweep_r1_lane2/p4_t20_xaxis.png | XChartView.c 轴标签边距 | new | |
| 67 | 页4 页签12 日历 | XCalendarWidget | 默认高亮 9 日、16 日带下划线，均非今日 23 日，今日/选中语义可疑 | visual | low | /tmp/sweep_r1_lane2/p4_t12_cal_base.png | XCalendarWidget.c 初始选中/今日标记 | new | |
| 68 | 页4 页签15 状态栏 | XStatusBar | 控件被拉伸整页后仅见左上「普通区标签」文字，无状态条底色/边框 | visual | low | /tmp/sweep_r1_lane2/p4_t15_statusbar.png | XStatusBar.c 绘制 + XTabWidget.c:59 | new | |

## 扫描进度（第一轮已完成）
- lane0（页0/1/2 + autotest）：✅ 41 交互/43 截图/0 崩溃；--autotest 135 PASS 0 FAIL
- lane1（页3+页4 前段）：✅ 146 交互/85 截图/0 崩溃
- lane2（页4 全 21 页签）：✅ 95 交互/92 截图/0 崩溃（含一次误报撤销：combo 坐标漏加 +40 偏移，复核后改为真缺陷「弹层不上屏」）
- lane3（页5+页8）：✅ 50 交互/70 截图/0 崩溃（含防误报复核 6 项）
- lane4（页6+页7）：✅ 110 交互/75 截图/0 崩溃
- **去重后问题总数：68 = high 36 / medium 20 / low 12**（原始 77，两路页4 重叠 9 条已合并）
- 环境警示（下轮复扫适用）：多路并存时 `pkill -9 -x XGuiWindowDemo_Test` 会互杀他路实例（lane1/lane2 各被杀 3 次）——复扫路只杀自己记录的 PID，或用 /tmp 二进制副本改名运行

## 第一轮修复批次结果（2026-09-24 01:55，工作流 dwfrun-86215cd7，Qt 源码对照口径）
**fixed 42**：#1(工具按钮 Qt DelayedPopup 等价) #2(切页回贴几何) #3(Tab 链窗口级启动) #4(ENTER/LEAVE 派发链，:hover 通) #9(Label autoFillBackground) #10/11(选择 ClearAndSelect) #12(视图 StrongFocus+点击聚焦) #13(focusIn/Out 默认 update) #14(树选中 Highlight 行) #15(树滚动条按需呈现) #16(树点击发真实条目双参载荷) #18(表头 sectionClicked 真发射) #19(消息框分级图标) #20(子控件形态标题) #23/24(对话框 Tab 闭环+Return 聚焦钮) #25(输入框 xid_setName 五处) #26(初始聚焦输入框) #27/28(居中公式双计页偏移根修) #30(IME 提交后补 complete) #31(KSE StrongFocus+点击聚焦) #34(随 #59) #35(ToolTip sizeHint) #40(SpinBox 焦点转发内嵌编辑框) #41(Slider StrongFocus) #46(LCD qlcdnumber 几何) #48(ScrollArea resizeEvent) #49(TextBrowser 补 show) #50(MDI 子窗挂视口+级联) #55(ToolBox 页贴激活头) #58/59(弹层 activateWindow 置顶+键盘抓取——根因=raise 是 no-op 且 X11 MapWindow 不改堆叠序) #60/61(Splitter 鼠标拖拽+把手 off-by-one) #63(Catmull-Rom 控制点镜像) #64(图例与柱体同刷) #54(旋钮死角按 qdial 口径) #66(轴标签居中钳位) #67(日历选中=今天+今日下划线) #68(状态栏面板底)
**blocked 8（附主线协调方案）**：#6/7/8 效果管线需运行时插桩（复扫阶段做）；#22 模态不拦鼠标——组C 已给 8 行补丁方案（XWidget.c:1212 目标解析后加模态子树门禁，dispatchInputAt 同口径），主线落地；#42/32 平台 IME 提交分支吞 Ctrl 键（XPlatformNativeWindow_posix.c:2864-2901，Ctrl+字母走 lookupString 提交致 KEY_PRESS 不产出）——主线落地平台补丁；#47 滚动条几何需运行时 trace；#65 折线越界疑 GPU 直通批量 scissor（复扫用 XGUI_GPU_SYNC=1 对照）
**复核为误报/非缺陷 5**：#17 列表 hover（Qt 默认样式也不画行 hover，语义一致）；#21 按钮 28px 错位（像素复核=测试点漏加窗口原点 (40,40)）；#51 TabBar 白底（像素实测白底随切换正确迁移）；#52 网格复活（指纹含 axisY，复活的是 demo 从未切过的 axisX 竖线=demo 半接线）；#57 表格选区跨列（像素实测恰一格宽）；#29 颜色取消回黑（库返回 Invalid spec 符合 Qt，观感问题在 demo 打印）；#62 日期滞后（证据截图显示 2029 全选=3 次 Up 一致终态，疑误读，复扫复核）
**design-call**：#38 字形（内置字库=Latin+GB2312 一级 3755 字，「渲」二级字缺失是字库覆盖问题；真修复=挂全字库 provider 或系统字形后端，留用户裁定）；#5 已修 XCommonStyle 路径，XFusionStyle sunken 需同口径 darker(110)（主线补）
**漏派补做**：#33 XMainWindow 全黑 / #36 橡皮筋 / #37 SizeGrip（补漏工作流）；#44 FPS 浮层 / #45 tabStatus / #39 splash 色（主线 demo 侧）

## 第一轮复扫结果（lane2/lane3 已交付，lane0/1/4 进行中；2026-09-24 03:35）
**lane2（页4 后段）8 fixed / 2 broken**：#52/53(确认项意外转绿——连带治愈)/57/63/64/65(软件与 XGUI_GPU_SYNC=1 逐位同)/66/67 fixed；#49/#50 broken——根因锁定=页签13/14 直插型页签未包 demo_wrapTabPage 且无显式 show，页面本体隐藏（库修复已落但被遮住）。
**lane3（页5+页8）5 fixed / 1 partial / 5 broken**：#10/11/12/15/9 fixed；#14 partial（顶级行高亮✓子行不随动=模型粒度，第二轮补子条目高亮）；#16 broken（库已发真实条目双参，页面槽仍 args_1 读顶层行——主线已修槽）；#18 broken（库已发 sectionClicked，页面没连——主线已连）；#6/7/8 broken（效果管线原症状仍在，待插桩构建定位）。
**复扫新发现（第二轮清单）**：①页5 树滚轮滚动无响应（滚动条只能拖）；②树表头随内容上滚不钉顶。
**主线已落修复（待重建复验）**：#16 views_treeClickedSlot 读 item 载荷（XVarList_args_2+回退）；#18 页面连 sectionClicked→状态行；#49/50 直插页签显式 XWidget_show。

## 第一轮复扫总结果（2026-09-24 04:10，5 路全部交付：61 项 = 38 fixed / 4 partial / 19 broken）
**注意：复扫用 02:50 二进制，不含主线随后落的 4 处 demo 修复（#16 槽载荷/#18 表头连接/#49/50 直插 show）——#16/18/49/50 的 broken 属预期，待重建复验。**
**复扫钉死的新根因（第二轮主攻）**：
- #3/40：Space/可打印键 keyPress 被平台 IME 提交分支吞掉（日志仅 keyRelease）——P1 的 Ctrl 守卫只救了 Ctrl 组合键；Qt 对 Space 等是「文本提交+按键事件」双发，平台需补发 KEY_PRESS
- #32：平台 KEY_PRESS 只取 keysym 第 0 列（小写 0x74），XShortcut 注册大写 0x54——真实按键永不匹配（XPlatformNativeWindow_posix.c:2939 vs XEvent.h:214）
- #5：按压态被样式表 :hover 实底遮盖（demo 无 :pressed 规则；Qt pressed 优先于 hover）
- #58/59：弹层 activateWindow+XRaiseWindow 后仍映射不上屏（IsViewable、缓冲完整、盲点可选）——需 xtrace 抓 MapWindow/CopyArea 链
- #60：Splitter 新鼠标处理有破坏性 bug——首次 MOVE 整页擦空、把手+右页永久消失
- #35：全库无 hover→XToolTip_showText 接线（ToolTip 仅静态 API，无延迟计时器）
- #37：SizeGrip 实现读码正确但真机不生效，手柄整块落在 FPS 悬浮层矩形内+页面角（demo 锚定未改）
- #46：LCD 渲染修复生效但控件仍被直插拉伸整页（需 sizeHint 布局/demo 几何）
- #68 伴随新问题：面板底铺上后「普通区标签」文字不可见（绘制顺序）；#28 小瑕疵：颜色框标题被色板遮半
- #31 partial：KSE 捕获 Ctrl+O 成功但修饰键按下被记为额外「?」组
第二轮清单汇总：平台键双发+大写归一（K1）、弹层上屏 xtrace（K2）、页4 布局族 #46/47/48/55/60/68（K3）、页6/7 族 #31/35/37 + #5 pressed 优先（K4）、#14 子行高亮 + #16/18/49/50 复验（K5）。

## 第二轮修复批次结果（2026-09-24 04:55，工作流 dwfrun-2ed532cf）
**fixed 14**：#3（Space/直映键改走按键路径，直映键集=单字节可打印非字母无 Shift，规避双写） #58/59（xtrace 实锤弹层不上屏=跨深度遮挡：depth-32 弹层被先上屏 depth-32 主窗排除在根合成外，移出主窗即显；修=Popup/ToolTip/Splash 族改屏幕默认 visual/depth，对标 qxcbwindow createVisual——XMenu/XComboBox/ToolTip/Splash 一处修好） #5（扩权 XStyleSheetStyle.c：Sunken 置位不发 Hover 伪类） #31（KSE 修饰键按 XKey_* 键码识别不再误记「?」组+键名大写） #35（ToolTip 全链：ENTER 唤起 700ms 单发+到期自动隐藏+LEAVE 宽限+首帧 flush） #46（渲染已愈，demo 改 wrap 紧凑） #48（ScrollArea 补 ShowEvent 重排） #55（ToolBox 页高按页自身现高顶置） #60（Splitter oldPos 取把手段带原点修破坏性擦页+grabMouse） #68（状态栏补 xsb_layoutItems 左普通右永久排版） #1=K5 树子条目高亮（三元组命中态+drawItem 递进） #2=树滚轮（行单位 3 行/格） #3=树表头钉顶（滚动平移前置 IntersectClip）
**blocked 1**：#47 滚动条几何——根因钉死=XWidget_setParent 换父重置几何 (0,0)（qwidget.cpp setParent_sys 全程不写 crect，原注释误标）；**主线已按 Qt 源码落 setParent 保几何修复** + 页签3 原有 wrap 保持
**design-call/deferred 2**：#32 平台大写归一已落（Ctrl+字母快捷键通），裸字母仍被 IME 提交分支吞——需控件层文本推导改 Shift 派生（XLineControl/XTextControl），留第三轮；#37 SizeGrip 库侧正确、真机失效疑 demo 根浮层路径消费按压——待插桩复验，demo 锚定主线落地
**主线本轮已落**：XWidget_setParent 保几何（Qt 源码核实）；页签2/4 改 wrapTabPage（LCD 紧凑/滚动区修视口）

## 第二轮复扫结果（2026-09-24 05:05，27 项 = 19 fixed / 4 partial / 4 broken）
**fixed 19**：#3(Space 激活) #5(按压三态可辨) #14(子行高亮) #16(树槽载荷) #17(树滚轮) #18×2(表头点击+钉顶) #19 #30 #31(KSE 无?组) #46(LCD 紧凑) #47(滚动条归位，8px 为样式设计常量) #49(浏览器) #52/53/57/62/63/64/65/66/67/68/9(抽查全维持)
**partial 4**：#48(滚动区内容可见+拖拽可滚，滚轮仍死) #50(MDI 子窗现但交互即擦除——XMdiArea 绘制/脏区深病) #55(工具箱切换态愈、初始态仍偏) #59(弹层上屏愈，「文件」高亮不复位残留)
**broken 4（根因全部钉死）**：#40/41① Tab(0x09) 不在直映键白名单仍被 IME 提交分支吞——文本控件=焦点链死岛（xpwn_imeCommitIsDirectKey 加 0x09 即可）；#41② Slider 点击聚焦后 Right 仍不步进（键盘链未达，疑点击未置焦点）；#35 ToolTip 双层根因（gdb 钉死）：①悬停目标是 XTextEdit 内部子控件、tooltip 在外层，无祖先回溯 ②ToolTip 窗仍 depth-32 未走弹层默认 visual 修复口径；#37 SizeGrip 按压被 demo 根控件 FPS 悬浮层拖拽分支消费（demo_performance_contains 命中+accept），手柄整块落在浮层矩形内（gdb+读码双证）
**第二轮复扫新问题**：#31 伴随（KSE 捕获后 kseStatus 标签被置空，demo 侧）；#28 颜色框标题被色板遮半（低危）
**第三轮清单**：M1 平台 Tab 白名单 / M2 Slider 点击聚焦+键盘链 / M3 ToolTip 祖先回溯+默认 visual 覆盖 / M4 XMdiArea 交互擦除+ToolBox 初始态 / M5 菜单高亮复位+ScrollArea 滚轮+KSE demo 态 / M6 效果管线 #6/7/8 插桩调查（/tmp 独立构建授权）+ #37 demo 锚定主线落

## 第三轮修复批次结果（2026-09-24 06:20，工作流 dwfrun-8929e06a）
**fixed 11**：#40/41①（平台 Tab 白名单一行根修：xpwn_imeCommitIsDirectKey 加 0x09，Shift+Tab 反向遍历可达） #41②（Slider 点击补 setFocus——根因=点击从未聚焦，Right 被行编辑消费） #35（ToolTip 双层：XWidget 悬停祖先回溯对标 qapplication.cpp notify 转交 + XToolTip 窗补 XWindowType_ToolTip 进默认 visual 族） #50（MDI 双根因 gdb 实证：resizeEvent 未链基类致视口 200x150 叠死 + ASA 整幅涂写多矩形派发互相抹白——改 exposedRect 脏区口径对标 qmdiarea.cpp:2669） #55（ToolBox 瞬态 6px 高度写坏页矩形：layout≤0 早退+sizeHint 推导+setCurrentIndex 标脏） #59（XMenuBar 连 aboutToHide 清 activeAction，对标 qmenu.cpp:2728 菜单栏回链） #48（ScrollArea 补 WheelEvent：基类方向反+单步 1px 双因，改 20px×3 格对标 qscrollarea.cpp:108） #6/7/8（**M6 插桩证明已愈**：快照 7200/7200 满幅、模糊/投影 backblit 对位正确、首启即正确——被第二轮批次连带治愈，插桩数据 trace_p2/p3/p5/p7.log 留档）
**deferred 1**：#37（FPS 浮层冲突主线已改右上角；M6 旁证浮层每帧独立 flush 无耦合）
**待复扫-3 验证**：以上 11 条 + 第一轮主线 4 处（#16/18/49/50 已在复扫-2 验过 3 条：#16/18 fixed、#49 fixed、#50 partial→本轮 #50 深修）

## 第三轮复扫结果（2026-09-24 07:30，24 项 = 16 fixed / 2 partial / 3 broken）
fixed：#3/5(键盘回归抽查) #46/47/48/55/68(页4 布局族全愈：滚轮 60px/格、工具箱初始态、状态栏) #59(菜单高亮复位) #31/30(KSE/补全回归) #63/67(图表/日历回归) #14/17(树回归) #49(浏览器) #50(MDI 交互擦除愈+子窗2 铬架愈) #6/7(透明/模糊真机像素终验：50% 公式逐点核对)
partial/broken（日间清单）：#40 焦点链落隐藏页钮（根因=XWidget_focusChainCandidate 用 explicitShow 非生效可见性——**主线已修**（m_visible 过滤，对标 Qt isVisible）待复验） #41 partial（点击聚焦+Right 已愈；Tab→Slider 一跳待复验同上） #35 partial（ToolTip 窗映射/几何/深度全对，缓冲黑条——**主线已补 autoFillBackground** 待复验） #8 partial→broken（投影首启像素终验 ✓；效果激活态点击擦除仍开放——效果重绘路径） #37 broken（resizeEvent BottomRight 重锚覆盖 init TopRight——**主线已修**（同改 TopRight）待复验；gdb 实证浮层移走后 SizeGrip 拖拽 800x600→950x730 完全正常）

## 定点收口（2026-09-24 07:30-08:05，主线）
**#40 焦点锚定修复已落**：XWidget_focusChainTarget 内嵌编辑器等非候选控件以最近可聚焦祖先为锚续链（对标 Qt focusNextPrevChild 锚定语义）。真机复验：Tab① LE→SpinBox ✓ Up 步进 30→31 ✓。
**#41② 残点精确收窄**：Tab② 从 SpinBox 出发仍未到 Slider——根因收窄为复合控件焦点策略链（SpinBox 容器/内部编辑器/Slider 的 TabFocus 位与显式 show 审计），页3 键盘链三段已通两段（LE→SpinBox、点击滑块+方向键），SpinBox→Slider 一跳留日间按策略位审计修。
**#37/#35 终验通过**：SizeGrip 拖拽 800x600→950x730 逐像素吻合；ToolTip 浅底黑字横排+光标偏移正常。

## 复扫-3 结果（2026-09-24 07:30，24 项 = 16 fixed / 2 partial / 3 broken）+ 定点收口（07:30-08:10）
**fixed 16**：#5(按压三态回归✓) #3(Space 回归✓) #46/47/48(LCD/滚动条/滚动区+滚轮 60px/格) #55(工具箱初始态贴头+无残影) #68(状态栏回归✓) #59(菜单高亮复位——aboutToHide 回链生效，像素实证) #31/30(KSE/补全回归✓) #63/67(图表/日历回归✓) #14/17(树子行高亮/滚轮回归✓) #49(浏览器回归✓) #50(MDI 交互擦除愈——四组交互 diff=0，exposedRect+resizeEvent 双修生效)
**#6/7 真机像素终验通过**（透明 50% 公式逐点、模糊笔画守恒），**#8 partial**：投影首启 ✓ 但效果激活态点击仍擦除（隔离在效果重绘路径，gdb 可复现）
**#37 broken→已修待复验**：真凶=VDemoWin_resizeEvent 每次 resize 把浮层重锚回 BottomRight 覆盖 init（gdb 双证：浮层移走后手柄拖拽 800x600→950x730 完全正常）——**主线已修**（resizeEvent 同改 TopRight）
**#40/41 焦点链（主线定点 + 残点收窄）**：①Tab 白名单后 keyPress 已进框架（gdb 实证 focusStep/setFocus 执行）；②新根因=focusChainCandidate 用 explicitShow 过滤，隐藏页控件成为候选——**主线已修**（m_visible 生效可见性过滤）+**焦点锚定修复**（focusChainTarget 内嵌编辑器等非候选控件以最近可聚焦祖先为锚，对标 Qt）；真机复验 Tab① LE→SpinBox+Up 步进 ✓
**残点（日间，gdb 跟 focusChainTarget 候选收集）**：Tab 从文本控件出发仍落文档序首候选（LE 未入候选集的原因待查——m_visible/策略位逐项审计）；SpinBox→Slider 一跳为其下游

## #41② 终验通过（2026-09-24 08:15，主线定点）
根因链补全：①XWidget_focusChainCandidate 用 explicitShow 过滤（隐藏页控件成候选）——已修（m_visible 生效可见性）；②focusChainTarget 非候选控件无锚重启——已修（最近可聚焦祖先为锚）；③**XAbstractSpinBox_init 从未设焦点策略**（默认 NoFocus，SpinBox 全家族不入 Tab 候选集）——已修（StrongFocus，对标 QAbstractSpinBoxPrivate::init）。
**真机终验**：页3 点击 LE → Tab① → SpinBox → Tab② → Slider → Right×2：SpinBox 32 / 滑块 32% / 进度 32% / 状态行「滑块: 32」四控件联动一致（截图 /tmp/verify_final/j41_tab_right.png）。页3 键盘链全线贯通。

## GL/OpenGL 路径实测（2026-09-24 07:40，用户问询专项；本机 GLX=llvmpipe 软件 GL，Xvfb :99）
**渲染正确性 ✓**：--gpu 页0/页4 图表页启动+空闲连拍 14 帧无一白屏（旧"白屏闪烁"在当前构建未复现），图表全元素渲染正确（含 #65 裁剪修复在 GL 下同样生效）。
**帧率问题仍在（未修，立专项）**：FPS 角标仅 1.0~2.2（页0/页4 同样低）；profiler 拆解——内部渲染 ~23 FPS、present 仅 ~1.5 次/秒（UI 呈现被节流）；单次开销 readback=3.5ms ×150次/s + drawImage=2.8ms ×150次/s ≈ 占 95% 帧预算。修复方向：①GL present 路径重构（readback 批量化/解耦 present 节流）②真机硬件 GL 复测（llvmpipe 软件 GL 数字仅为下界）。

## 第四轮残项清零结果（2026-09-24 08:35，工作流 dwfrun-e19672f8）
**fixed 4/4**：
- **#32 裸字母键全链收官**：①控件层 xlc_keyToText/xtc_keyPress 文本推导加 Shift 派生（字母大小写正确）②平台直映键集纳入拉丁字母（CapsLock 例外保留提交通道防锁存态插错）③XShortcut(T)/KSE 链静态核验全通（裸 t→KEY_PRESS 0x54→快捷键命中或插入 't'/'T' 按修饰）。真机复验待重建后。
- **#50 附加 MDI 子窗拖拽**：vtable 三挂点+标题条命中+grabMouse+视口钳位（对标 qmdisubwindow.cpp:3143/3271/1151），exposedRect 脏区口径保持。
- **#56 ErrorMessage 警示样式**：浅红底带+4px 深红强调条+深红文字（对比度 7.4:1 > WCAG AA）。
- **#28 颜色框标题遮半**：根布局顶边距按标题判定 28/12（对齐 #20 xmsg_contentTop 先例），空间预算核算 320≤340 无回退。
全部 -fsyntax-only 过；#50/#28 运行时验证待重建后复扫。

## #32 回归处置（2026-09-24 08:55，诚实记录）
第四轮 #32 落地后验收 2 FAIL（T=="aXc" 实得 "axc"——Shift+x 大小写丢失）+ autotest 1 FAIL（实机键入 abcXYde）。已回退平台字母直映扩展（字母恢复 IME 提交通道）但回归依旧→失败源锁定为 A 组控件层配套（xlc_keyToText Shift 派生 + 修饰位透传链）组合效应：KEY_PRESS 路径 modifiers 缺 Shift 位（translateModifiers 链或 fcitx DBus 重投递），推导得小写。
**处置**：字母直映保持回退态（文本插入回到既有通道，仅 XShortcut 裸字母触发回落原状）；keyToText Shift 派生代码保留（仅在直映路径触发，主路径零影响）；日间修复方向=①gdb/插桩核验 Shift+x 的 KEY_PRESS modifiers 是否含 Shift ②按缺位点补透传 ③再纳字母入直映集 ④XLC-ACC "aXc" 断言+autotest "abcXYde" 断言复验。A 组 harness（7/7）与完整分析在案。

## #32 回归闭环（2026-09-24 08:50）
根因定位：A 组 keyToText Shift 派生让"大小写由 Shift 位表达"（键值恒大写归一，对标 Qt），验收夹具 ac_type 注入大写字母未带 Shift 位 → 契约不一致。**修正=夹具跟随新契约**（ac_type 大写字母携带 XKeyboardModifier_ShiftModifier；autotest "XY" 注入同改）。
**最终全套验证全绿**：重建 0 error / 回归 0 FAIL / 验收 0 FAIL / GPU 0 FAIL / autotest 135 PASS。#32 全链（平台直映+大写归一+控件层 Shift 派生+夹具契约）闭环。

---

# 附录B：覆盖台账

# 夜间测试覆盖台账（2026-09-23 夜）

原则：已测路径不重复测，未覆盖路径优先。每轮结束更新。

## 页面清单（9 页 + 页面4 的 21+ 页签）
- 页面0 按钮演示 | 页面1 选择演示 | 页面2 堆叠演示 | 页面3 输入演示
- 页面4 选项卡演示（页签 0~20，页签 20=图表）
- 页面5 条目视图 | 页面6 对话框 | 页面7 高级控件 | 页面8 图形效果

## 第一轮（进行中）
- lane0 (:99)：页面0/1/2 逐控件 + --autotest 对照
- lane1 (:100)：页面3 + 页面4 页签0~7
- lane2 (:101)：页面4 页签8~20（图表重点）
- lane3 (:102)：页面5 条目视图 + 页面8 图形效果
- lane4 (:103)：页面6 对话框 + 页面7 高级控件
- 维度：逐控件「看得见+点得动」+ 联动读出 + 键盘 Tab/Enter/Esc
