# XTableWidget 与 Qt 6.8 对齐说明

> **文档状态**：实施计划已批准，先行登记（2026-11-06）。各节随实现推进补齐，
> 未落地的小节会标注「（待实现）」。
> **协调备注（重要）**：另一台电脑上存在 `iso/table-align` 分支
> （commit `ad61e0a1`"XTableView/XTableWidget 全量对齐 Qt 6.8.3"，基于
> `7cbeda5b`，大改 XTableWidget.c/.h 与 XAbstractItemView/TableView，**未动
> QSS 引擎**）。为避免撞车，本分支**暂缓 §3.3 表格控件侧的实现**，待该分支
> 合入后在合入结果上对齐；QSS 引擎侧（§3.1/§3.2）与之无文件交集，可先行。
> **Qt 参考源码**：`D:\Qt\6.8.3\Src\qtbase\src\widgets\styles\qstylesheetstyle.cpp`
> （CE 表头拦截 :4088-4158）、`itemviews\qheaderview.cpp`（paintSection :2958）、
> `itemviews\qtableview.cpp`（SH_Table_GridLineColor 消费 :1507）、
> `gui\text\qcssparser.cpp`（子控件名表）。
> **适用控件**：`Src/XGui/Widget/XTableWidget.c/.h`（表头由表格 paintEvent 内联
> 自绘；`XHeaderView` 是挂接承载的模型类，不参与绘制——见 `XTableView.h:369-373`）。

## 1. 背景与动机

客户端各页表格（主界面「设备状态」、历史数据、报警、日志）的表头一直是引擎默认灰
（调色板 Button 角色 #efefef + WindowText 黑字）。根因是 **XGui 的 QSS 引擎没有
表头子控件支持**：Qt 6.8 一条 `QHeaderView::section {}` 就能美化表头，XGui 写不出
等价规则——即「没对齐 Qt」。此外主界面表格还有两个观感缺陷同源：

- **表头右侧无名字空白段**：各页列宽总和 < 控件宽，段带止于末列、余宽铺 Window 色
  （`XTableWidget.c` paintEvent :3139-3144），读作一段灰列；它也是「选中单元格比
  列边界宽」错觉的来源（选中格止于列宽总和处，灰段却延伸到控件右缘）。
- **选中色被条目背景盖画**：paintEvent :3218-3227 先铺选中色再铺条目 BackgroundRole，
  隔行行被选中时颜色混杂。Qt 的顺序是 背景层 → 选中层 → 文字层。

本次任务：**把表格控件完整对齐 Qt 6.8**（QSS 表头子控件 + 表格属性族 + 几何行为），
并在本文档登记对照关系与用法。

## 2. 现状对照（探查结论，2026-11-06）

### 2.1 绘制链路（对齐前）

```
XTableWidget paintEvent (:3070-3353)
 ├─ 铺 Base 底色
 ├─ 表头段带底色 + 空区铺 Window 色 (:3126-3148)      ← CE_HeaderEmptyArea 语义，自绘
 ├─ 逐列：XStyleOption(基类) → CE_HeaderSection → CE_HeaderLabel (:3156-3185)
 │      └─ 样式引擎（装 QSS 后 = XStyleSheetStyle）
 │            └─ VXStyleSheetStyle_drawControl (:2520-2554)
 │                 先按控件自身规则改写选项/画盒 → 源样式(Fusion) → 再铺控件背景边框
 │                 【缺陷】widget 参数是表本身 ⇒ 控件背景规则会盖掉表头段
 ├─ 数据区：选中色 → 条目背景(盖在选中上✗) → 网格线(:3304-3331, 色=局部 Dark)
 └─ 末行角区收口 (:3340-3348)
```

### 2.2 Qt ↔ XGui 对照表

| Qt 6.8 机制 | Qt 位置 | XGui 现状 | 本次动作 |
|---|---|---|---|
| `QHeaderView::section {}` 伪元素规则 | qstylesheetstyle :4094-4152 | 解析层已支持 `::name`（XCssStyleSheet.c:820-834，字符串存储），**无消费端** | ✅ 新增消费端（计划 §3.1） |
| `CE_HeaderSection` 拦截（规则带背景/边框 → subRule.drawRule 盒绘制 + return） | :4103-4120 | 无拦截；且控件背景规则会盖表头（管线 :2541-2549） | ✅ 新增（§3.1） |
| `CE_HeaderLabel` 拦截（configurePalette 文字色/字体 → 源样式画文字） | :4122-4152 | 无；且 `xcs_drawHeaderLabel` 直接读调色板 WindowText，不读 `option->m_textColor`（XCommonStyle.c:2863-2883） | ✅ 新增 + 源样式小改（§3.1/§3.4） |
| `CE_HeaderEmptyArea`（widget 自身规则 hasDrawable 时空区不画） | :4154-4158 | XCommonStyle 侧空操作（:3131-3143）；XTableWidget 自绘空区无条件铺 Window 色 | ✅ 有 section 规则时不铺（§3.3） |
| `SH_Table_GridLineColor` 样式提示 | qtableview :1507 | 枚举已有（XStyleOption.h:454），**无人实现/无人消费**；网格线色=局部 Dark | ✅ 引擎实现 + 表格消费（§3.1/§3.3） |
| `alternate-background-color` 属性 | 文档属性 → AlternateBase 角色 | 枚举+名表已有（XCssStyleSheet.h:125 / .c:48），**无消费**；框架隔行开关 `XAbstractItemView_setAlternatingRowColors` 只有 XListView 消费（XListView.c:408） | ✅ XTableWidget 消费（§3.3）；应用页暂保留逐格铺底（同色叠加无害） |
| `selection-background-color` / `selection-color` 属性 | → Highlight / HighlightedText | 枚举+名表已有（:120-121/:132-133），**无消费** | ✅ 消费（§3.3） |
| `gridline-color` 属性 | → SH_Table_GridLineColor | 枚举已有（:119），**名表缺 "gridline-color" 项** | ✅ 补名表 + 消费（§3.2） |
| `QHeaderView::stretchLastSection` 属性 | qheaderview | `XHeaderView.m_stretchLast` 字段已有（XHeaderView.h:65/:87-94）**从未被消费**；demo apitest 只做存取往返 | ✅ 落地消费（§3.3） |
| `QHeaderView::setSectionResizeMode / defaultSectionSize` 等几何 API | qheaderview | XHeaderView 已有模式字段；XTableWidget 走自身 `m_colWidths`（Fixed 手工列宽） | 已够用，不动 |
| `QStyleOptionHeader` 选项结构 | qstyleoption | **结构已存在但从未使用**（XStyleOption.h:1156-1168，init :220-234）；表头绘制传的是基类 XStyleOption | ✅ 启用（§3.3） |
| 选中格绘制顺序（背景→选中→文字） | QTableView paint | 顺序反了（选中被条目背景盖） | ✅ 修正（§3.3） |
| 表头高度（defaultSectionSize 语义） | qheaderview | `m_headerHeight` 无公共 setter（默认 24，仅 init/copy 赋值） | ✅ 新增 setter（§3.3） |

**已知偏差（本文档登记，本次不做）**：见 §7。

## 3. 实施计划（已批准）

### 3.1 QSS 引擎 `Src/XGui/Style/XStyleSheetStyle.c/.h`

- `VXStyleSheetStyle_drawControl`（:2520）顶部新增三个 CE 拦截（对齐 Qt 语义）：
  - `CE_HeaderSection`：`xsss_widgetRule(…, "section")`（:1458）命中且规则带
    背景/边框 → `xsss2_drawRuleChrome`（:3342，与 Qt `subRule.drawRule` 同构）
    + **return**。提前 return 同时消除「控件自身背景规则盖表头文字」的过绘
    （管线 :2541-2549 对 widget=表对象无条件铺背景边框的机制）。规则只带
    color/font 时写 `option->m_textColor` / 画家字体后转发源样式；未命中走原管线。
  - `CE_HeaderLabel`：规则文字色经 `xsss_applyTextColor`（:1541，configurePalette
    等价）写 `m_textColor`、`xsss_applyFont`（:1843）后转发源样式画文字。
  - `CE_HeaderEmptyArea`：widget 自身规则带背景/边框（hasDrawable 判定 =
    `xsss_lookup`(:1497) 对 BackgroundColor/Background/BorderWidth/Border 逐一试）
    → 不画 return（对齐 Qt :4154-4158）。
- **styleHint 实现** `XStyleSH_Table_GridLineColor`（枚举 XStyleOption.h:454）：
  读 widget 规则 `gridline-color`（`XCssParseColor`）打包色值返回；无规则回落
  源样式/调色板 Dark。消费见 §3.3。
- **类名直查薄封装**：现规则匹配按 widget vtable 类名（`xsss_className` :828，
  `xsss_basicMatches` :1025）；表头不是独立 widget（绘制传表对象），Qt 同名
  选择器 `XHeaderView::section` 无法命中。新增按类名字符串查规则的入口
  （级联源收集与裁决复用 `xsss_scanRules` 既有口径，仅元素名改查入参），
  让 `XHeaderView::section` 与 `XTableWidget::section` 两种写法都命中（详见 §6）。

### 3.2 属性解析 `Src/XGui/Style/XCssStyleSheet.c`

`k_propNames`（:27-148）按字母序补 `"gridline-color"` 一行（枚举
`XCssProperty_GridlineColor` 已在 .h:119；alternate/selection 三个已有枚举+名表，
本次只补消费端）。

### 3.3 表格控件 `Src/XGui/Widget/XTableWidget.c/.h` + `XHeaderView.c/.h`

- 表头逐列绘制（:3156-3185）改用 `XStyleOptionHeader`（填 m_section /
  m_textAlignment / m_orientation），CE 连发结构保持。
- 空区（:3139-3144）：查询到 `section` 规则时不再铺 Window 色（表格底色透出，
  对齐 CE_HeaderEmptyArea 语义）；无规则维持现状。
- 网格线色（:3304-3331）：改走 `XStyle_styleHint(XStyleSH_Table_GridLineColor)`
  取色，回落实色=现 Dark（无 QSS 时观感不变）。
- 隔行色：消费 `XAbstractItemView_setAlternatingRowColors` + AlternateBase 角色
  （对齐 QTableView；应用现有逐格铺底暂不动，同色叠加无害）。
- 选中绘制顺序（:3218-3227）：条目背景 → 选中色 → 文字；选中格文字色支持
  `selection-color`（规则命中时优先，否则 HighlightedText）。
- **末列拉伸**：`XHeaderView_setStretchLastSection` 公共 setter（字段已有）+
  XTableWidget 几何助手统一消费——末列有效宽 = max(设定宽, 控件宽 − 其余列合计
  − 行号列宽)，paint / cellAt / updateContentSize 走同一助手（几何同源不错位）；
  列宽合计超出控件宽时末列维持设定宽（横向滚动不受影响）。
- 新增 `XTableWidget_setHeaderHeight` 公共 setter（默认仍 24）。

### 3.4 源样式 `Src/XGui/Style/XCommonStyle.c`

`xcs_drawHeaderLabel`（:2863-2883）优先读 `option->m_textColor`（非 0），
否则调色板 WindowText——QSS 文字色经选项生效的通道（对标 Qt configurePalette
把规则色写进 option palette 的做法）。

### 3.5 应用侧 `HighSpeedFilter/client/ui/`

- `HsfThemeCss.h` 新增规则（蓝底白字加粗表头 + 表格属性，色值复用主题 #1A66D0 系）：

  ```css
  XHeaderView::section {
      background-color: #1A66D0;
      color: #FFFFFF;
      font-weight: bold;
      padding: 5px 8px;
      border-right: 1px solid #16409B;
  }
  XTableWidget {
      gridline-color: #E2E6EC;
      selection-background-color: #1A66D0;
      selection-color: #FFFFFF;
  }
  ```

- 四页建表处各加两行：`XHeaderView_setStretchLastSection(hh, true)` +
  `XTableWidget_setHeaderHeight(tw, 32)`（HsfHomePage.c :701 区 / HsfDataHistory.c /
  HsfDataAlarm.c / HsfDataTable.c 通用助手，覆盖 DataTable/DataLog）。

### 3.6 验证口径

1. 新框架测试 `Test/XGuiTest/XTableQssTest.c`（照 XTableAlignTest 挂法 include 进
   仓库根 xgui_regression_test.c；像素断言复用 `w1_renderCC` 模板 :29675）：
   - `::section` 规则下 CE_HeaderSection 离屏中心像素 = 规则底色；
   - CE_HeaderLabel 文字色 = 规则 color；
   - `gridline-color` / `selection-background-color` / `selection-color` 像素命中；
   - 末列拉伸几何（columnWidth / itemAt 跨拉伸区）；列宽合计超宽时末列不缩；
   - 选中隔行行不混色（像素=选中色）；有 section 规则时空区不铺灰。
2. 构建：`build_hsf.bat` + 测试宿主；`XinYueC_Static --test xmodbus-virtual`、
   `--test hsf-sim-mode` 无连带回归；ctest XGuiRegression 三变体；
   `XGuiWindowDemo_Test --apitest=views`（stretchLastSection 从存标志变真行为）。
3. 实机：`HsfClient --login root:root@127.0.0.1:6100 --no-settings --shot <png>
   --page <索引>` 离屏截主界面/历史/报警/日志四页——表头蓝底白字加粗、右侧空白
   段消失、末列吸收余宽、选中格与列边界对齐；通过后拉起正式客户端屏上确认。

## 4. QSS 用法（对齐后可用）

```css
/* 表头段：Qt 写 QHeaderView::section，XGui 两种选择器等价支持 */
XHeaderView::section {
    background-color: #1A66D0;
    color: #FFFFFF;
    font-weight: bold;
    padding: 5px 8px;
    border-right: 1px solid #16409B;
}
XTableWidget::section { /* 同上，按实际绘制控件类名匹配 */ }

/* 表格属性族（对齐 Qt QTableView 的 QSS 属性） */
XTableWidget {
    alternate-background-color: #F5F7FA;  /* 隔行色（配 alternatingRowColors） */
    gridline-color: #E2E6EC;              /* 网格线 */
    selection-background-color: #1A66D0;  /* 选中底色 */
    selection-color: #FFFFFF;             /* 选中文字色 */
}
```

（本节随实现补充：支持的声明子集、优先级与回退链、与控件级 setStyleSheet 的级联。）

## 5. 几何模型（待实现后补充细节）

- 列宽单一数据源 `m_colWidths[]`；表头段、单元格、`cellAt` 命中全部读同一份
  （paint :3152/:3211、命中 :3380-3392、列起点 `xtw_colX` :2950）——几何同源。
- 末列拉伸在「列宽计算助手」一层生效（末列有效宽 = max(设定宽, 剩余可用宽)），
  三处消费点自动一致；内容尺寸/横向滚动范围随之收敛。
- 行号列隐藏时占宽 0（`xtw_vHeaderW`，最近一次提交新增）。

## 6. 选择器说明

- Qt 里表头是独立子控件 `QHeaderView`，规则写 `QHeaderView::section`。
- XGui 表头由 `XTableWidget` paintEvent 内联绘制（`XHeaderView` 仅为模型类），
  绘制时传给样式引擎的 widget 是表本身。为对齐 Qt 书写习惯，引擎提供按类名
  直查入口：`XHeaderView::section`（规范写法，与 Qt 同名）与
  `XTableWidget::section`（按实际绘制控件匹配）**两者等价命中**同一条
  `section` 规则；文档与主题 QSS 统一用前者。

## 7. 已知偏差（登记，不做）

| 偏差 | 说明 |
|---|---|
| 表头排序指示器 | XGui 无列排序模型，`::section` 不含指示器区域（Qt CE_HeaderLabel :4131-4140 的箭头让位逻辑不适用） |
| `::section :hover` | Qt 悬停段有 State_MouseOver；XGui 表格无悬停重绘机制，暂不接 |
| span 绘制 | `setSpan` 在 XTableWidget paintEvent 不被消费（仅 clear 语义） |
| corner button | 左上角按钮（`QTableCornerButton::section`）无对应物 |
| 垂直表头 QSS | 行号列不参与 QSS（`::section` 仅水平表头） |
| 伪元素特异度 | 伪元素不计入级联特异度（XCssStyleSheet.h:306-309 已登记，与 Qt 偏差保持现状） |

## 8. 测试与回归入口

- 框架回归：仓库根 `xgui_regression_test.c`（无 golden 截图机制，断言=离屏软位图
  像素取样：`xqss_centerPixel` :28662、`w1_renderCC` :29675、`w1_imagesEqual`
  :29706）；QSS wave1 子控件分派用例 `:29931-30019` 是 `::section` 测试的模板。
- 表格几何专项：`Test/XGuiTest/XTableAlignTest.c`（include 挂入回归 :36157）。
- header API 往返：`bin/XGuiWindowDemo_Test --apitest=views`
  （Test/XGuiDemo/xgui_demo_apitest_views.c:3512-3543，无 ctest 注册，直接跑）。
- ctest 变体：`Test/CMakeLists.txt:776-802`（XGuiRegression / Gpu / Auto，
  env `XGUI_RENDER_BACKEND=software`）。
- 本次新增：`Test/XGuiTest/XTableQssTest.c`（§3.6 口径，挂入回归主流程）。
