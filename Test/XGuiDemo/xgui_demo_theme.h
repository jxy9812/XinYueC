/* xgui_demo_theme.h —— XGuiWindowDemo 缺省主题样式表（亮色 · 现代蓝）。
 *
 * 供 xgui_window_demo.c 的 XStyle_installStyleSheet 调用处直传
 * （xgui_demo_theme_css；fusion-css 缺省样式口径的现代化替换，取代
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
 */

#ifndef XGUI_DEMO_THEME_H
#define XGUI_DEMO_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 缺省主题样式表（静态存储期字面量；XStyle_installStyleSheet 直传）。 */
static const char* const xgui_demo_theme_css =

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

    /* ==================== XSlider：::handle（白底矩形把手） ==================== */
    /* 凹槽不写规则：原生 groove（Base 底+Dark 描边）自带从 min 到
     * 把手的 Highlight 已填充段（xcs_drawSlider 子页高亮），与进度条
     * 高亮块同源同色——评审第 1 轮实锚。把手=白底圆角矩形+灰描边
     * （对齐参考 Fusion 把手形态）；把手矩形/命中区由引擎几何固定
     * （xsss2_subControlRect 16px，XSlider 命中测试同源），QSS 不可
     * 扩大，如实登记。 */
    "XSlider::handle {"
    " background-color: #FFFFFF;"
    " border: 1px solid #5A6572;"
    " border-radius: 3px; } "
    "XSlider::handle:hover {"
    " background-color: #EAF2FF;"
    " border: 1px solid #1A66D0;"
    " border-radius: 3px; } "
    "XSlider::handle:disabled {"
    " background-color: #F0F2F5;"
    " border: 1px solid #D5DBE2;"
    " border-radius: 3px; } "

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

    ; /* xgui_demo_theme_css 结束 */

#ifdef __cplusplus
}
#endif
#endif /* XGUI_DEMO_THEME_H */
