/* HsfThemeCss.h —— XGuiWindowDemo 缺省主题样式表（亮色 · 现代蓝）。
 *
 * 供 xgui_window_demo.c 的 XStyle_installStyleSheet 调用处直传
 * （hsf_theme_css；fusion-css 缺省样式口径的现代化替换，取代
 * 旧版两行内嵌演示规则 XPushButton:hover=#3D8BFD / XLineEdit=#FFFFE0）。
 *
 * 设计口径（引擎消费面实测裁定，出处见 XGui.md §14.130 及其评审修订）：
 *  - 风格=扁平+描边+圆角（不用渐变：全控件一致性与栅格化开销取舍）；
 *  - 本引擎绘制序=底层样式先画 → QSS 背景填充/描边后置 → 控件自绘
 *    文本最后，且 (对象,状态) 级联只取单条胜出规则（无声明合并）——
 *    因此每个状态规则必须自包含整套盒外观（底色+描边+圆角），
 *    否则该态背景/描边整体消失；
 *  - 伪类不参与特异度权重，胜出仅由规则在表内先后决定：书写顺序
 *    即优先级（常态 < :hover < :focus < :pressed/:checked < :disabled）；
 *  - 文本色一律走调色板（引擎控件自绘文本不消费 QSS color，正文
 *    黑/禁用灰随 Fusion 调色板），主题只着色盒外观——正文对比度
 *    由「浅底黑字」结构保证；
 *  - 复选框/单选钮/滑块凹槽的「选中标记与已填充段」由底层样式先绘
 *    （xcs_drawIndicatorCheckBox 对勾、xcs_drawIndicatorRadioButton
 *    圆点、xcs_drawSlider 子页高亮段）——任何不透明的 QSS 后置填充
 *    都会抹掉它们，故指示器一律只描边不填充、凹槽完全不写规则
 *    （2026-10-01 评审第 1 轮实证：checked 蓝底填充抹对勾成纯色块、
 *    groove 平轨填充抹高亮段成无填充细线）；
 *  - 能力边界（本头内不写规则或仅描边，详见 XGui.md 登记）：
 *    XTextEdit/XPlainTextEdit 无样式绘制调用点；XMenu 逐条目后置
 *    填充会抹菜单项；XGroupBox 的 CC_GroupBox 背景覆盖会抹标题；
 *    XScrollBar 整条填充会抹把手（仅整框描边覆盖四缘，评审第 2 轮
 *    用其遮蔽右缘引擎暖色残线）；XTabWidget 窗格自身无样式绘制
 *    调用点（pane 框主题层不可达，见 XGui.md 登记）；XRadioButton
 *    原生即圆环+圆点（QSS 圆角矩形描边会与原生椭圆双圈）——以上
 *    保持原生观感。
 *
 * 色板（WCAG 2.1 相对亮度公式核算，正文 ≥4.5:1 / 非文本件 ≥3:1）：
 *  | 角色     | 色值    | 用途               | 关键对比            |
 *  |---------|---------|--------------------|---------------------|
 *  | 主色锚  | #3D8BFD | 任务给定主色系锚点 | —（仅色系锚定）     |
 *  | 强调填充| #2E7CE8 | 输入类悬停描边     | vs 白 4.06 / 窗底 3.53（≥3）|
 *  | 强调线  | #1A66D0 | 焦点/悬停/按下/选中描边 | vs 白 5.45 / 悬停底 4.83（≥3）|
 *  | 悬停底  | #EAF2FF | 悬停浅蓝底         | 正文黑 13.9（≥4.5） |
 *  | 按压底  | #DBE7FD | 按下浅蓝底         | 正文黑 16.8（≥4.5） |
 *  | 控件底  | #FFFFFF | 常态按钮/输入/页签 | 正文黑 15.5（≥4.5） |
 *  | 控件描边| #5A6572 | 主控件轮廓（评审第 1 轮自 #8B98A5 加深两档） | vs 白 5.94 / 页底 #F4F6F8 5.47（≥3）|
 *  | 次级描边| #8B98A5 | 微调框步进按钮框等次级分区 | vs 白 2.94（非文本件 ≈3）|
 *  | 软描边  | #D5DBE2 | 禁用/页签次级边界  | 禁用态对比豁免      |
 *  | 禁用底  | #F0F2F5 | 禁用控件底         | 禁用态对比豁免      |
 *  | 正文    | 调色板黑| 随 Fusion 调色板   | 浅底黑字结构达标    |
 *  滑块已填充段/进度条高亮块同取调色板 Highlight（引擎原生同源蓝）。
 *
 * [2026-11-11 双主题定稿] 亮色盒外观层维持上表全部口径——本轮
 * lightRefinements 的改①（四态语义色收进 HsfTheme.h 单一事实源）与改②
 * （页面状态色字面量归一）都不落在本头（页面字面量归一属页面工单）；
 * 改③（2026-11-11 晚场走查）：XSlider::handle 三条规则**撤除**（QSS
 * chrome 平涂盖掉引擎把手先绘的整圈四边描边、自身 AA 残线呈开口括号，
 * 根因与机制锚点见下方把手段注释）——这是上表之外唯一的亮色实改，
 * 把手可见性回到引擎 Fusion 口径并作 WCAG 豁免登记。其余亮色页底/
 * 表头/滚动条灰阶经实跑复核均达标且观感已对齐 ISA-101 浅灰底口径，
 * 不为改而改。旗舰深色主题（深色·控制室 墨青）样式表=下方
 * hsf_theme_dark_css，色板登记表与机制口径见其段头注释。
 */

#ifndef XGUI_DEMO_THEME_H
#define XGUI_DEMO_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 缺省主题样式表（静态存储期字面量；XStyle_installStyleSheet 直传）。 */
static const char* const hsf_theme_css =

    /* ==================== XPushButton：常态/悬停/焦点/按下/禁用 ==================== */
    /* 三态全自包含（见文件头「单胜出规则」口径）；焦点态描边随
     * PE_FrameFocusRect 二次绘制落在 3px 内缩环上，与外描边构成
     * 双环焦点指示。 */
    "XPushButton {"
    " background-color: #FFFFFF;"
    " border: 1px solid #5A6572;"
    " border-radius: 5px; } "
    "XPushButton:hover {"
    " background-color: #EAF2FF;"
    " border: 1px solid #1A66D0;"
    " border-radius: 5px; } "
    "XPushButton:focus {"
    " background-color: #FFFFFF;"
    " border: 1px solid #1A66D0;"
    " border-radius: 5px; } "
    "XPushButton:pressed {"
    " background-color: #DBE7FD;"
    " border: 1px solid #1A66D0;"
    " border-radius: 5px; } "
    /* :checked=选中保持态（2026-10-03 双行分组导航的当前页常亮，按压底
     * 同色系）：规则置于 :pressed 之后、:disabled 之前（书写序即优先级
     * 口径，常态<hover<focus<pressed<checked<disabled），且必须自包含
     * 整套盒外观（单胜出规则，无声明合并）。 */
    "XPushButton:checked {"
    " background-color: #DBE7FD;"
    " border: 1px solid #1A66D0;"
    " border-radius: 5px; } "
    "XPushButton:disabled {"
    " background-color: #F0F2F5;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 5px; } "

    /* ==================== XLineEdit：底色+边框+悬停/聚焦 ==================== */
    /* 聚焦态同时覆盖 PE_PanelLineEdit 与 PE_FrameFocusRect 两次绘制
     * （同矩形幂等）；占位文本仍由引擎按调色板渲染。 */
    "XLineEdit {"
    " background-color: #FFFFFF;"
    " border: 1px solid #5A6572;"
    " border-radius: 4px; } "
    "XLineEdit:hover {"
    " background-color: #FFFFFF;"
    " border: 1px solid #2E7CE8;"
    " border-radius: 4px; } "
    "XLineEdit:focus {"
    " background-color: #FFFFFF;"
    " border: 1px solid #1A66D0;"
    " border-radius: 4px; } "
    "XLineEdit:disabled {"
    " background-color: #F0F2F5;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "

    /* ==================== XItemLineEditor：委托内嵌行编辑器 ==================== */
    /* [10-19 框架编辑器对齐] XAbstractItemView 缺省委托编辑器（XLineEdit
     * 派生，见 XItemDelegate.c）。QSS 元素名按 vtable 名严格匹配
     * （xsss_className），派生类命中不了 XLineEdit 规则 ⇒ 编辑器退化成
     * 「仅 1px 边线、无底色」的透明框（单元格选中色/原文本透出）。
     * 对标 Qt：QStyledItemDelegate 的编辑器本就是 QLineEdit，
     * QLineEdit{...} 规则天然命中——此处补同款四态规则即还原该语义；
     * 「样式表引擎基类链匹配」记为框架对齐项另案。 */
    "XItemLineEditor {"
    " background-color: #FFFFFF;"
    " border: 1px solid #5A6572;"
    " border-radius: 4px; } "
    "XItemLineEditor:hover {"
    " background-color: #FFFFFF;"
    " border: 1px solid #2E7CE8;"
    " border-radius: 4px; } "
    "XItemLineEditor:focus {"
    " background-color: #FFFFFF;"
    " border: 1px solid #1A66D0;"
    " border-radius: 4px; } "
    "XItemLineEditor:disabled {"
    " background-color: #F0F2F5;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "

    /* ==================== XComboBox / XSpinBox：描边（不设底色）+ 步进按钮框 ==================== */
    /* 复杂控件的箭头/步进按钮列由底层样式先绘：任何不透明底色的
     * 后置填充都会把它们抹掉——故容器只覆描边与圆角；步进按钮用
     * 子控件规则补框线（同样不设底色护箭头字形），上按钮底边+下
     * 按钮顶边构成上下分隔线。 */
    "XComboBox {"
    " border: 1px solid #5A6572;"
    " border-radius: 4px; } "
    "XComboBox:hover {"
    " border: 1px solid #2E7CE8;"
    " border-radius: 4px; } "
    "XComboBox:focus {"
    " border: 1px solid #1A66D0;"
    " border-radius: 4px; } "
    "XComboBox:disabled {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "
    "XSpinBox {"
    " border: 1px solid #5A6572;"
    " border-radius: 3px; } "
    "XSpinBox:hover {"
    " border: 1px solid #2E7CE8;"
    " border-radius: 3px; } "
    "XSpinBox:focus {"
    " border: 1px solid #1A66D0;"
    " border-radius: 3px; } "
    "XSpinBox:disabled {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 3px; } "
    "XSpinBox::up-button {"
    " border: 1px solid #8B98A5; } "
    "XSpinBox::down-button {"
    " border: 1px solid #8B98A5; } "

    /* ==================== XTabBar：页签描边/选中/禁用（不设底色） ==================== */
    /* 页签文字由底层在 CE_TabBarTabLabel 内先绘（xcs_drawTabLabel，
     * WindowText 黑）——规则的 background-color 后置填充会把文字整片
     * 抹掉（评审第 2 轮①实锚：9 个标签全空白、暗像素 0）。故只覆
     * 描边+圆角做状态区分，底色回落原生形状（未选中 Button 灰、
     * 选中 Base 白）。 */
    "XTabBar {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "
    "XTabBar:selected {"
    " border: 1px solid #1A66D0;"
    " border-radius: 4px; } "
    "XTabBar:disabled {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "

    /* ==================== XScrollBar：::handle/::sub-page/::add-page 中性化 ==================== */
    /* 引擎滑块（thumb）与滑块下方轨道末列存在 1px 暖色残线（#DF5F17
     * 系，主题/参考同源，Src 侧无该色常量——评审第 2 轮④）。按评审
     * 「改中性灰」方向：::handle 平涂滑块、::sub-page/::add-page 平涂
     * 上下轨道（矩形覆盖含残线所在列），整框描边收边；凹槽/箭头保
     * 持原生。 */
    "XScrollBar {"
    " border: 1px solid #8B98A5; } "
    "XScrollBar::handle {"
    " background-color: #8B98A5;"
    " border-radius: 3px; } "
    "XScrollBar::handle:hover {"
    " background-color: #5A6572;"
    " border-radius: 3px; } "
    "XScrollBar::sub-page {"
    " background-color: #E4E7EC; } "
    "XScrollBar::add-page {"
    " background-color: #E4E7EC; } "

    /* ==================== XSlider：::handle 不写规则（引擎完整把手上屏） ==================== */
    /* 凹槽不写规则：原生 groove（Base 底+Dark 描边）自带从 min 到
     * 把手的 Highlight 已填充段（xcs_drawSlider 子页高亮），与进度条
     * 高亮块同源同色——评审第 1 轮实锚。把手矩形/命中区由引擎几何固定
     * （xsss2_subControlRect 16px，XSlider 命中测试同源），QSS 不可
     * 扩大，如实登记。
     *
     * [2026-11-11 走查实锅：::handle 规则撤除] 原先这里写「白底圆角矩
     * 形+灰描边」三条规则（常态/hover/disabled），亮色下把手呈开口括
     * 号状：白填充落白凹槽、可见描边只剩左+下（像素级验证非裁切）。
     * 根因在 QSS 子件 chrome 与引擎把手先绘的叠加序（机制锚点：
     * VXStyleSheetStyle_drawComplexControl = 底层样式先画整只滑块 →
     * xsss2_dispatchSubControls 后置命中子件的 chrome）：
     *  ① 引擎 xcs_drawSlider 的把手段（XCommonStyle.c「draw handle」
     *     段）本就画**完整把手**：buttonColor 本体 + 四边 outline 显式
     *     fillRect 描边（上/下/左/右各一条，无 AA）+ 白 alpha 30 内衬
     *     线 + 焦点描边翻 Highlight.darker(125)；
     *  ② QSS ::handle 命中后 xsss2_drawRuleChrome 先 fillRect 平涂
     *     （矩形、无圆角）——把引擎四边描边在裁剪域内的区段整条盖掉；
     *  ③ 再画自身的 drawRoundedRect AA 圆角描边——1px 线在 16px 把手
     *     几何上四边各损失半像素覆盖（亮色白底上不可辨），呈现「上/右
     *     缺失、左/下残线」；深色把手（亮块 #D5DBE2）同序但可见性由
     *     亮块自身承担（见深色表同段登记），故两主题观感分叉。
     * 处置：本表三条 ::handle 规则撤除——::handle 不命中规则 ⇒ 子件
     * 分派零附加绘制（xsss2_dispatchSubControls 无规则短路，与转发
     * 底层样式逐字节一致），引擎完整把手（四边显式描边，无 AA 损失、
     * 无覆盖损失）直接上屏；焦点/悬停/禁用全态由引擎把手段自洽。
     * WCAG 登记（豁免）：引擎 outline=Window.darker(140)，标准浅盘下
     * ≈#ABABAB vs 凹槽白底 ≈2.3:1（非文本件 <3）——引擎 Fusion 全局
     * 观感（XGuiDemo 同源），把手可见性=buttonColor 本体+整圈描边+
     * 内衬线组合承担；本轮以「描边完整性（四边全画）」优先，对比豁免
     * 与引擎口径一并登记，深色表 ::handle 保留不受本段影响。 */

    /* ==================== XProgressBar：描边+圆角（不设底色，护高亮块） ==================== */
    "XProgressBar {"
    " border: 1px solid #5A6572;"
    " border-radius: 4px; } "
    "XProgressBar:disabled {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 4px; } "

    /* ==================== XCheckBox：指示器描边（不设底色护对勾） ==================== */
    /* 原生指示器=白底+黑边框+选中黑对勾（xcs_drawIndicatorCheckBox），
     * 与 XTreeWidget 条目复选框「白底框+深色对勾」同语义（评审第 1 轮
     * 裁定统一）；主题只覆描边色做状态强调，任何填充都会抹勾。
     * XRadioButton 不写规则：原生圆环+选中圆点已是最终形态（圆角矩
     * 形描边会双圈）。 */
    "XCheckBox {"
    " border: 1px solid #5A6572;"
    " border-radius: 3px; } "
    "XCheckBox:hover {"
    " border: 1px solid #1A66D0;"
    " border-radius: 3px; } "
    "XCheckBox:checked {"
    " border: 1px solid #1A66D0;"
    " border-radius: 3px; } "
    "XCheckBox:indeterminate {"
    " border: 1px solid #1A66D0;"
    " border-radius: 3px; } "
    "XCheckBox:disabled {"
    " border: 1px solid #D5DBE2;"
    " border-radius: 3px; } "

    /* ==================== XTableWidget / XTableView：边框 + 圆角 + 斑马纹 ==================== */
    /* 对齐 Python 版 theme.py:416-423 的 QTableView/QTableWidget 规则。
     * [为什么能命中] 本引擎 QSS 元素名按 vtable 名严格匹配
     * （xsss_className → XVTABLE_GET_NAME），XTableWidget 的 vtable 名
     * 就是 "XTableWidget"、基类 "XTableView"；两条都写（表控件本身 +
     * 只标注基类名的场景），参照 XItemLineEditor 段同款理由。
     * [消费边界·如实登记]
     *  - border/border-radius：由控件级盒路径（VXStyleSheetStyle_
     *    drawControl → xsss_drawBorder）在表体绘制之后覆盖四缘，圆角
     *    仅描边视觉（表体仍是矩形填充，四角不会真的被裁）；
     *  - alternate-background-color / gridline-color：本引擎**未接入**
     *    表格交替行与网格线的取色（XTableWidget 自绘按调色板
     *    AlternateBase/Dark，XTableView 同），写了不生效——C 端隔行
     *    浅底现由 HsfHomePage 逐格 BackgroundRole 承载。规则在此保留
     *    以便引擎接入后自动生效，不参与本次像素产出。 */
    "XTableWidget {"
    " background-color: #FFFFFF;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 6px; } "
    "XTableView {"
    " background-color: #FFFFFF;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 6px; } "

    /* ==================== 表头段 ::section（对齐 Python theme.py:425-440） ==================== */
    /* [引擎能力对齐] Python 侧 QHeaderView::section / :last /
     * QTableCornerButton::section 三条规则，由本次框架补的
     * 「绘制点向 QSS 代理声明伪元素」通道消费：
     *  - XTableWidget 列头绘制以 XStyleOptionHeader（m_optionType =
     *    XStyleOptionType_Header）传段号/段位置，代理据此按
     *    `XTableWidget::section`（默认伪元素名 "section"）查规则；
     *    段位置再派生 :first/:middle/:last/:only-one
     *    （对齐 Qt qstylesheetstyle.cpp:1995-2003）；
     *  - 四边独立边框由框架新增的表头段绘制路径消费
     *    （border:none + border-right + border-bottom 三声明共存，
     *    这是本规则能画成「右侧竖线 + 底部横线、无其余边框」的前提）；
     *  - padding:6px 8px 参与 SE_HeaderLabel 矩形内缩（文本不贴边）；
     *  - font-weight:bold 由标签趟落画家字体（对齐 Qt
     *    CE_HeaderLabel 的 p->setFont(subRule.font.resolve())）。
     * [元素名] 用 "XTableWidget" 而非 "XHeaderView"：C 端列头绘制点
     * 传的 widget 是表控件本身（XTableWidget.c paintEvent 内联绘制），
     * XHeaderView 在本库只承载段尺寸状态、不上屏（XTableView.c:207-224
     * 注释），写 XHeaderView 会永不命中。两者同时写出，兼顾后续把列头
     * 迁到 XHeaderView 自绘的情形。 */
    "XTableWidget::section {"
    " background-color: #EFF3F7;"
    " border: none;"
    " border-right: 1px solid #E4E8EC;"
    " border-bottom: 1px solid #D5DBE2;"
    " padding: 6px 8px;"
    " color: #1A1A1A;"
    " font-weight: bold; } "
    "XHeaderView::section {"
    " background-color: #EFF3F7;"
    " border: none;"
    " border-right: 1px solid #E4E8EC;"
    " border-bottom: 1px solid #D5DBE2;"
    " padding: 6px 8px;"
    " color: #1A1A1A;"
    " font-weight: bold; } "
    /* ::section:last —— 末段去掉右边线（否则末列右侧多一条与表框
     * 相邻的竖线，视觉上成双线）。对齐 Python theme.py:434-436。 */
    "XTableWidget::section:last {"
    " border-right: none; } "
    "XHeaderView::section:last {"
    " border-right: none; } "
    /* 角按钮（左上角格）：同底色、无边框。C 端当前不单独绘制角格
     * （表头带由整段 fillRect 铺底，见 XTableWidget.c:3135-3153），
     * 规则为后续接入预留；底色与 ::section 同值保证接入前后一致。 */
    "XTableCornerButton::section {"
    " background-color: #EFF3F7;"
    " border: none; } "

    ; /* hsf_theme_css 结束 */

/* ==================== 旗舰深色主题样式表（深色·控制室 墨青） ====================
 * 2026-11-11 设计师定稿落地（「主题名：深色·控制室（墨青）」，QSS 盒外观
 * 层 + 调色板层两层登记）。与亮色表 hsf_theme_css 逐段同构，全部机制口径
 * 照抄文件头、不在此重复：级联单条胜出（每态规则自包含整套盒外观：
 * 底色+描边+圆角）、书写顺序即优先级（常态<hover<focus<pressed/checked
 * <disabled）、文本色一律走调色板、指示器/凹槽只描边不填充、能力边界
 * （XTextEdit/XPlainTextEdit 无样式绘制调用点、XMenu 逐条目后置填充抹
 * 菜单项、XGroupBox 背景覆盖抹标题、XScrollBar 只整框描边、XTabWidget
 * 窗格不可达、XRadioButton 保持原生圆环）。两点深色特有口径：
 *  - 表头 ::section 的 color: 是「文本不走 QSS」的唯一例外（声明经
 *    xsssHdr_declText 消费，对标 Qt CE_HeaderLabel 的 subRule 配色覆盖），
 *    深色必须换 #DDE3E9——亮色硬编码 #1A1A1A 在深色表头底上是 1.2:1 级
 *    灾难；
 *  - 窗底不写窗口规则：autoFillBackground 按调色板 Window 色填充，
 *    深色盘（HsfTheme.c 装载）挂上即全窗翻 #1B1F24。
 *
 * 深色色板登记表（WCAG 2.1 相对亮度公式实算复核，正文 ≥4.5:1 /
 * 非文本件 ≥3:1；「层次参考值」为感知参考不作硬指标——亮色控件底白
 * vs 页底 #F4F6F8 同为 1.08 量级）：
 *  | 角色       | 色值    | 用途                       | 关键对比                        |
 *  |-----------|---------|----------------------------|---------------------------------|
 *  | 窗底      | #1B1F24 | 调色板 Window（非纯黑控暗室残影）| 正文 #E8EAED 13.74（≥4.5）  |
 *  | 面板/控件底| #23282F | 按钮/输入/页签回落/表体底  | 正文 12.30（≥4.5）；vs 窗底 1.12 层次参考 |
 *  | 控件描边  | #707B89 | 主控件轮廓（镜像亮色 #5A6572 的角色）| vs 控件底 3.45 / 窗底 3.85（≥3）|
 *  | 次级描边  | #6A7583 | 步进按钮框/滚动条整框      | vs 控件底 3.17 / 窗底 3.54（≥3；候选 #5E6976 仅 2.65 不达故提亮定稿）|
 *  | 软描边    | #39414C | 禁用/页签次级边界/表框线   | 禁用态对比豁免（镜像亮色 #D5DBE2 vs 白 1.19 同类豁免）|
 *  | 悬停底    | #2C343D | 悬停底                     | 正文 10.46（≥4.5）；vs 面板 1.18 层次参考 |
 *  | 强调填充  | #3686D9 | 输入类悬停描边             | vs 输入底 3.93（≥3；镜像亮色 #2E7CE8 的次级强调角色）|
 *  | 强调线    | #4DA3FF | 焦点/按下/选中/checked 描边（兼主色/链接）| vs 面板 5.65 / 悬停底 4.80 / 窗底 6.31（≥3；作链接/强调文字 6.31 ≥4.5）|
 *  | 按压/选中底| #1E3456 | pressed/checked 常亮底     | 正文 10.37（≥4.5）；状态识别由同框 #4DA3FF 描边承担（vs 选中底 4.76/vs 面板 5.65）——与亮色按压底只登记正文对比同口径 |
 *  | 禁用底/描边| #1E242B / #343B44 | 禁用控件          | 禁用态对比豁免；禁用文本 #8E96A0 vs 禁用底 5.23（≥4.5）|
 *  | 滚动条    | 轨道 #262C33 / 把手 #6B7684 / 把手悬停 #8A95A3 / 整框 #6A7583 | 把手 vs 轨道 3.05 / 悬停 4.64（≥3）；只整框描边+把手平涂（整条填充抹把手）|
 *  | 滑块把手  | 底 #D5DBE2 + 描边 #1B1F24 | 亮把手浮出暗轨（对齐 Fusion 深色把手形态）| vs 窗底/描边 11.88、vs 凹槽底 #23282F 10.64（均 ≥3）；把手 16px 引擎几何固定 QSS 不可扩大；悬停描边换 #4DA3FF——把手边界对暗轨由亮底承担（10.64），状态信号=描边翻强调线（vs 凹槽底 5.65 ≥3），描边对把手底 1.88 属组件内装饰（按压/选中底同款豁免口径）；亮色表同段规则已撤（QSS chrome 覆盖引擎四边描边的叠加代价两主题同在，深色由亮块可见性兜底故保留，见深色表把手段登记）|
 *  | 表头      | 底 #262D35 / 文字 #DDE3E9 / 右缘线 #323A44 / 底线 #39414C | 文字 vs 表头底 10.76（≥4.5）；vs 表体底 1.07 层次参考；bold + padding:6px 8px 照抄 |
 *  调色板配套深色盘（HsfTheme.c 装载，角色登记见 HsfTheme.h）：正文
 *  Text/WindowText/ButtonText=#E8EAED；次级文本=#A9B2BC（vs 窗底 7.71 /
 *  面板 6.91 / 选中底 5.82 全 ≥4.5；层级令牌，亮色主题无此层）；
 *  Highlight=#4DA3FF、HighlightedText=#101820（6.82 ≥4.5——修正内置深色
 *  盘 #F0F0F0 on #308CC6 仅 3.24 的不达项）、Link=#4DA3FF（6.31 ≥4.5）；
 *  ToolTipBase=#2A313A、ToolTipText=#E8EAED（10.90 ≥4.5——修正内置深色盘
 *  沿用浅色 ToolTipBase 的 1.12:1 缺陷）；AlternateBase=#2A313A（斑马纹，
 *  正文 10.90 ≥4.5）；禁用组文本=#8E96A0（vs 禁用底 5.23 ≥4.5，禁用态
 *  豁免但较内置深色盘 #828282 提亮一档保可读）。滑块已填充段/进度条
 *  高亮块/表格选中行同取 Highlight（引擎原生同源蓝口径继承）。
 */

/** @brief 旗舰深色主题样式表（静态存储期字面量；HsfTheme_applyFlagshipSkin
 *          经 XApplication_setStyleSheet_2 直装）。 */
static const char* const hsf_theme_dark_css =

    /* ==================== XPushButton：常态/悬停/焦点/按下/选中/禁用 ==================== */
    /* 与亮色表同构：六态全自包含；按压/选中同底 #1E3456（常亮底），
     * 状态识别由同框 #4DA3FF 描边承担（登记表口径）。 */
    "XPushButton {"
    " background-color: #23282F;"
    " border: 1px solid #707B89;"
    " border-radius: 5px; } "
    "XPushButton:hover {"
    " background-color: #2C343D;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 5px; } "
    "XPushButton:focus {"
    " background-color: #23282F;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 5px; } "
    "XPushButton:pressed {"
    " background-color: #1E3456;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 5px; } "
    "XPushButton:checked {"
    " background-color: #1E3456;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 5px; } "
    "XPushButton:disabled {"
    " background-color: #1E242B;"
    " border: 1px solid #343B44;"
    " border-radius: 5px; } "

    /* ==================== XLineEdit：底色+边框+悬停/聚焦/禁用 ==================== */
    /* 悬停描边=强调填充 #3686D9（vs 输入底 3.93 ≥3），聚焦升强调线
     * #4DA3FF——与亮色表「常态<悬停<聚焦」的强调分级同构。 */
    "XLineEdit {"
    " background-color: #23282F;"
    " border: 1px solid #707B89;"
    " border-radius: 4px; } "
    "XLineEdit:hover {"
    " background-color: #23282F;"
    " border: 1px solid #3686D9;"
    " border-radius: 4px; } "
    "XLineEdit:focus {"
    " background-color: #23282F;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 4px; } "
    "XLineEdit:disabled {"
    " background-color: #1E242B;"
    " border: 1px solid #343B44;"
    " border-radius: 4px; } "

    /* ==================== XItemLineEditor：委托内嵌行编辑器 ==================== */
    /* 同亮色表：XLineEdit 派生的缺省委托编辑器按 vtable 名严格匹配，
     * 必须单列同款四态规则（否则退化成透明框）。 */
    "XItemLineEditor {"
    " background-color: #23282F;"
    " border: 1px solid #707B89;"
    " border-radius: 4px; } "
    "XItemLineEditor:hover {"
    " background-color: #23282F;"
    " border: 1px solid #3686D9;"
    " border-radius: 4px; } "
    "XItemLineEditor:focus {"
    " background-color: #23282F;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 4px; } "
    "XItemLineEditor:disabled {"
    " background-color: #1E242B;"
    " border: 1px solid #343B44;"
    " border-radius: 4px; } "

    /* ==================== XComboBox / XSpinBox：描边（不设底色）+ 步进按钮框 ==================== */
    /* 同亮色表：容器只覆描边与圆角（不设底色护箭头字形/步进按钮列）；
     * 步进按钮框=次级描边 #6A7583（vs 控件底 3.17 ≥3）。 */
    "XComboBox {"
    " border: 1px solid #707B89;"
    " border-radius: 4px; } "
    "XComboBox:hover {"
    " border: 1px solid #3686D9;"
    " border-radius: 4px; } "
    "XComboBox:focus {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 4px; } "
    "XComboBox:disabled {"
    " border: 1px solid #343B44;"
    " border-radius: 4px; } "
    "XSpinBox {"
    " border: 1px solid #707B89;"
    " border-radius: 3px; } "
    "XSpinBox:hover {"
    " border: 1px solid #3686D9;"
    " border-radius: 3px; } "
    "XSpinBox:focus {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 3px; } "
    "XSpinBox:disabled {"
    " border: 1px solid #343B44;"
    " border-radius: 3px; } "
    "XSpinBox::up-button {"
    " border: 1px solid #6A7583; } "
    "XSpinBox::down-button {"
    " border: 1px solid #6A7583; } "

    /* ==================== XTabBar：页签描边/选中/禁用（不设底色） ==================== */
    /* 同亮色表：底色回落原生形状（未选中 Button 灰、选中 Base 白——深色盘
     * 里两者同为 #23282F，见 HsfTheme.c）；本表只覆描边，软描边 #39414C
     * 作页签次级边界，选中升强调线 #4DA3FF。 */
    "XTabBar {"
    " border: 1px solid #39414C;"
    " border-radius: 4px; } "
    "XTabBar:selected {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 4px; } "
    "XTabBar:disabled {"
    " border: 1px solid #39414C;"
    " border-radius: 4px; } "

    /* ==================== XScrollBar：::handle/::sub-page/::add-page 中性化 ==================== */
    /* 同亮色表「只整框描边+把手平涂」边界（整条填充抹把手）：整框
     * #6A7583，把手 #6B7684（vs 轨道 3.05 ≥3）、悬停 #8A95A3（4.64 ≥3），
     * 上下轨道平涂 #262C33 遮引擎暖色残线。 */
    "XScrollBar {"
    " border: 1px solid #6A7583; } "
    "XScrollBar::handle {"
    " background-color: #6B7684;"
    " border-radius: 3px; } "
    "XScrollBar::handle:hover {"
    " background-color: #8A95A3;"
    " border-radius: 3px; } "
    "XScrollBar::sub-page {"
    " background-color: #262C33; } "
    "XScrollBar::add-page {"
    " background-color: #262C33; } "

    /* ==================== XSlider：::handle（亮把手浮出暗轨） ==================== */
    /* 凹槽不写规则（原生 groove=Base 底+Dark 描边+Highlight 已填充段，
     * 深色盘 Base=#23282F/Dark=#14181D/Highlight=#4DA3FF 一并登记）；
     * 把手=亮底圆角矩形+窗底描边（对齐 Fusion 深色把手形态，vs 凹槽底
     * 10.64/vs 描边 11.88 双 ≥3）；悬停底保持亮把手、描边翻强调线；
     * 把手 16px 引擎几何固定（xsss2_subControlRect），QSS 不可扩大。
     * [2026-11-11 亮色同段撤规对照] 亮色表 ::handle 三条规则已撤（QSS
     * chrome 平涂盖引擎四边描边+自身 AA 残线，见亮色表同段根因）。
     * 本深色表**保留**：① 深色走查已合格（把手可见性=亮块自身 vs 凹槽
     * 底 10.64 承担，描边残缺不致命），不为改而改；② 撤规则后深色把手
     * 落回引擎 buttonColor（=#23282F）与凹槽底同色——**把手整块消失**
     * 的倒退不可接受。已知的 QSS chrome 叠加代价如实登记：把手描边
     * #1B1F24 的 AA 半像素损失在亮块边缘不显（亮块/描边 1.88 组件内
     * 装饰豁免同口径）。 */
    "XSlider::handle {"
    " background-color: #D5DBE2;"
    " border: 1px solid #1B1F24;"
    " border-radius: 3px; } "
    "XSlider::handle:hover {"
    " background-color: #D5DBE2;"
    " border: 1px solid #4DA3FF;"
    " border-radius: 3px; } "
    "XSlider::handle:disabled {"
    " background-color: #1E242B;"
    " border: 1px solid #343B44;"
    " border-radius: 3px; } "

    /* ==================== XProgressBar：描边+圆角（不设底色，护高亮块） ==================== */
    "XProgressBar {"
    " border: 1px solid #707B89;"
    " border-radius: 4px; } "
    "XProgressBar:disabled {"
    " border: 1px solid #343B44;"
    " border-radius: 4px; } "

    /* ==================== XCheckBox：指示器描边（不设底色护对勾） ==================== */
    /* 同亮色表：原生指示器先绘对勾/圆点，任何填充都会抹勾；XRadioButton
     * 不写规则（原生圆环已是最终形态）。 */
    "XCheckBox {"
    " border: 1px solid #707B89;"
    " border-radius: 3px; } "
    "XCheckBox:hover {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 3px; } "
    "XCheckBox:checked {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 3px; } "
    "XCheckBox:indeterminate {"
    " border: 1px solid #4DA3FF;"
    " border-radius: 3px; } "
    "XCheckBox:disabled {"
    " border: 1px solid #343B44;"
    " border-radius: 3px; } "

    /* ==================== XTableWidget / XTableView：边框 + 圆角 ==================== */
    /* 表体底=#23282F（面板/控件底，镜像亮色「表体=控件白」的角色）；
     * 表框线=软描边 #39414C。alternate-background-color/gridline-color
     * 引擎未接入（亮色表同款边界），斑马纹由调色板 AlternateBase
     * #2A313A 承载。 */
    "XTableWidget {"
    " background-color: #23282F;"
    " border: 1px solid #39414C;"
    " border-radius: 6px; } "
    "XTableView {"
    " background-color: #23282F;"
    " border: 1px solid #39414C;"
    " border-radius: 6px; } "

    /* ==================== 表头段 ::section（深色表头） ==================== */
    /* color: #DDE3E9 是必须换值的例外通道（xsssHdr_declText 消费）——
     * 亮色的 #1A1A1A 在深色表头底 #262D35 上仅 1.2:1 级。右缘线
     * #323A44 / 底线 #39414C 独立边框三声明共存同亮色口径；
     * font-weight:bold + padding:6px 8px 照抄。 */
    "XTableWidget::section {"
    " background-color: #262D35;"
    " border: none;"
    " border-right: 1px solid #323A44;"
    " border-bottom: 1px solid #39414C;"
    " padding: 6px 8px;"
    " color: #DDE3E9;"
    " font-weight: bold; } "
    "XHeaderView::section {"
    " background-color: #262D35;"
    " border: none;"
    " border-right: 1px solid #323A44;"
    " border-bottom: 1px solid #39414C;"
    " padding: 6px 8px;"
    " color: #DDE3E9;"
    " font-weight: bold; } "
    /* ::section:last —— 末段去掉右边线（否则与表框成双线），同亮色口径。 */
    "XTableWidget::section:last {"
    " border-right: none; } "
    "XHeaderView::section:last {"
    " border-right: none; } "
    /* 角按钮：同表头底色、无边框（规则为后续接入预留，同亮色口径）。 */
    "XTableCornerButton::section {"
    " background-color: #262D35;"
    " border: none; } "

    ; /* hsf_theme_dark_css 结束 */

#ifdef __cplusplus
}
#endif
#endif /* XGUI_DEMO_THEME_H */
