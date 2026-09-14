# XGui 全模块 Qt 对齐 + XString 化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 整个 XGui 模块（Widgets 56 控件 + Graphics + Charts）与 Qt 6.8.3 的 API/行为/继承关系一一对应，所有拥有型字符串字段一律改用 XString*（不再用 char[N]/char(*)[N]），先完成 XGui Widgets 全量对齐，再继续 Charts 模块继承对齐。

**Architecture:** 分两大阶段。Phase 1 审计并修正 Widgets 的继承层次（以 Qt 头文件为基准），把所有拥有型字符串成员从 char[N] 改为 XString*（生命周期配套：init 创建 / deinit 释放 / copy 深拷 / move 转移），全部修改在主构建 + 回归 + 25 裁剪 + demo 验证后进入 Phase 2。Phase 2 继续 Charts 的 QAbstractSeries→QXYSeries/QAbstractBarSeries/QPieSeries/QAreaSeries 继承重构与剩余 209 API 缺口。

**Tech Stack:** C99 / CMake / XObject/XClass 虚表 / XString（Src/XContainer/XString）/ XPainter

**Spec:** Qt 6.8.3 源码基准 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/` 与 `/home/xinyue/Qt/6.8.3/Src/qtcharts/src/charts/`；仓库《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》

## Global Constraints

- 只改代码不提交（用户明确政策；所有 commit 步骤替换为验证步骤）。
- 字符串成员一律 `XString*`（拥有型）；对外 API 参数/返回值保持 `const char*`（UTF-8 外部输入输出）。
- 生命周期配套：`_init` 创建、`V*_deinit` 释放、`V*_copy` 用 `XString_assign`/`XString_create_copy` 深拷、`V*_move` 转移指针并置空源。
- 继承层次以 Qt 6.8.3 为准（QWidget 派生树、QAbstractSeries 派生树）。
- 旧 API 不保留；与 Qt 冲突者改名不并存。
- 新文件 UTF-8 BOM、中文 Doxygen（@brief/@param/@return 全覆盖）、_base 只查表分派。
- 修改共享源后必须重新 configure 并编译全部 25 个裁剪构建 + build-crop-gpu。
- 每阶段结束验证：主构建 EXIT=0、`./bin/XGuiRegression_Test` 输出 "XGui regression tests passed"、`ctest --test-dir build` 3/3、25 裁剪全 PASS、demo 截图交互验证。

---

## Phase 1：XGui Widgets 全量对齐 + XString 化（已完成）

> 执行记录（2026-09-10 单代理执行）：Task 1-4（XLineEdit/XComboBox/XProgressBar/
> XGroupBox/XDockWidget/XMdiArea/XToolBar/XErrorMessage/XSplashScreen/XStatusBar/
> XPlainTextEdit/XDateTimeEdit/XMessageBox/XTextBrowser/XTextDocument/XWizard/
> XTableWidget 全部字符串字段 XString 化）+ Task 5（继承审计：51 类一致，补建
> XAbstractItemView/XTableView 中间层，XTableWidget 改继承 XTableView）+ 继承
> 契约测试 + Charts 的 XBarSeries/XCategoryAxis categories XString 化全部完成。
> 例外：XColorSpace.m_description/m_userDescription 保持 char[64]（值语义结构，
> 头文件明确"不持有堆资源、可安全按值复制嵌入 XImage"，改 XString* 会破坏
> 拷贝语义；Qt QColorSpace 用隐式共享 QString，XString 无此机制）。
> 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 个裁剪构建
> 全 PASS、demo 截图正常 + 交互点击稳定。Phase 1 验收完成。

### Task 1: XString 化基础改造样板（XLineEdit）

**Files:**
- Modify: `Src/XGui/Widget/XLineEdit.h:149`（m_placeholder 字段）
- Modify: `Src/XGui/Widget/XLineEdit.c`（生命周期 + 访问器）
- Test: `xgui_regression_test.c`（test_widgets_strings_contract 首个用例）

**Interfaces:**
- Produces: `void XLineEdit_setPlaceholderText(XLineEdit*, const char*)`（签名不变）、`const char* XLineEdit_placeholderText(const XLineEdit*)`（签名不变）、内部 `m_placeholder` 改 `XString*`

- [ ] **Step 1: 写失败测试**

在 `xgui_regression_test.c` 新建 `test_widgets_strings_contract()`：
```c
static void test_widgets_strings_contract(void)
{
    XLineEdit* edit = XLineEdit_create();
    XString* s;
    const char* p;
    if (!edit) { XTest_Fail("XLineEdit_create"); return; }
    XLineEdit_setPlaceholderText(edit, "请输入内容");
    p = XLineEdit_placeholderText(edit);
    if (!p || strcmp(p, "请输入内容") != 0) XTest_Fail("placeholder roundtrip");
    /* 超长内容（>64 字节）不再截断 */
    {
        char buf[256];
        memset(buf, 'x', 200);
        buf[200] = 0;
        XLineEdit_setPlaceholderText(edit, buf);
        if (strlen(XLineEdit_placeholderText(edit)) != 200)
            XTest_Fail("placeholder no truncation");
    }
    XLineEdit_delete_base(edit);
}
```
在 `main()` 中调用并断言运行。

- [ ] **Step 2: 运行测试验证失败**

Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected: 超长截断断言 FAIL（旧 char[64] 行为）。

- [ ] **Step 3: XLineEdit.h 字段改 XString***

`XLineEdit.h`：
```c
XString* m_placeholder;       /**< 占位提示（对象拥有）。 */
```
确保 include `"XString.h"`（XLineEdit.h 顶部）。

- [ ] **Step 4: XLineEdit.c 生命周期配套**

- `XLineEdit_init`（或 create 路径）：`self->m_placeholder = XString_create();`
- `VXLineEdit_deinit`：释放 `m_placeholder` 并置 NULL
- `VXLineEdit_copy`：`XString_assign(self->m_placeholder, other->m_placeholder)`
- `VXLineEdit_move`：转移指针、源置 NULL
- `XLineEdit_setPlaceholderText`：`XString_assign_utf8(self->m_placeholder, text ? text : "")`
- `XLineEdit_placeholderText`：`XString_toUtf8(self->m_placeholder)` 返回（空则 ""）

- [ ] **Step 5: 运行测试验证通过**

Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected: "XGui regression tests passed"（超长用例通过）。

### Task 2: 单值字符串字段批量 XString 化（10 个控件）

**Files:**
- Modify: `Src/XGui/Widget/XComboBox.h/.c`（m_placeholderText[64]）
- Modify: `Src/XGui/Widget/XDateTimeEdit.h/.c`（m_displayFormat[64]）
- Modify: `Src/XGui/Widget/XDockWidget.h/.c`（m_title[128]）
- Modify: `Src/XGui/Widget/XErrorMessage.h/.c`（m_message[512]）
- Modify: `Src/XGui/Widget/XGroupBox.h/.c`（m_title[64]）
- Modify: `Src/XGui/Widget/XMdiArea.h/.c`（m_title[128]）
- Modify: `Src/XGui/Widget/XMessageBox.h/.c`（m_title[128]、m_text[512]）
- Modify: `Src/XGui/Widget/XPlainTextEdit.h/.c`（m_placeholder[256]）
- Modify: `Src/XGui/Widget/XProgressBar.h/.c`（m_format[32]）
- Modify: `Src/XGui/Widget/XSplashScreen.h/.c`（m_message[256]）
- Modify: `Src/XGui/Widget/XStatusBar.h/.c`（m_currentMessage[512]）
- Modify: `Src/XGui/Widget/XToolBar.h/.c`（m_title[128]）
- Test: `xgui_regression_test.c`（test_widgets_strings_contract 扩展）

**Interfaces:**
- Produces: 每个控件 `m_*` 字段 → `XString*`；对外 setter/getter 签名不变

- [ ] **Step 1: 逐控件字段替换 + 生命周期配套**

对每个控件重复 Task 1 Step 3-4 模式：
- 头：`char m_xxx[N]` → `XString* m_xxx`，加 include `XString.h`
- 源：init 创建 / deinit 释放 / copy 深拷 / move 转移 / setter 用 `XString_assign_utf8` / getter 用 `XString_toUtf8`

注意 XMessageBox 的 m_title/m_text 是两个字段；XDockWidget 与 XMdiArea 的 title 有默认文本（如 "窗口"），init 时 `XString_create_utf8("窗口")`。

- [ ] **Step 2: 回归测试扩展**

在 test_widgets_strings_contract 中为每个控件加 roundtrip + 超长用例（沿用 Task 1 模式，超长长度取各自旧容量+100）。

- [ ] **Step 3: 主构建 + 回归**

Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected: EXIT=0 且 "XGui regression tests passed"。

- [ ] **Step 4: 25 裁剪 + gpu 全量验证**

Run: 对所有 build-crop-* 目录 `cmake -S . -B <dir> && cmake --build <dir> -j4`，逐目录检查输出
Expected: 25 个 canonical + build-crop-gpu 全部编译通过（无 error）。

### Task 3: XTextBrowser / XTextDocument / XWizard 复合字符串 XString 化

**Files:**
- Modify: `Src/XGui/Widget/XTextBrowser.h/.c`（m_source[256]、m_history[32][256]）
- Modify: `Src/XGui/Widget/XTextDocument.h/.c`（fontFamily[64]、anchorHref[256]、text[256]、blockFormat[64]、m_title[256]、m_url[256]）
- Modify: `Src/XGui/Widget/XWizard.h/.c`（m_title[128]、m_subTitle[128]）
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: `XTextBrowser_m_history` → `XString**`（堆数组，含 m_historyCount 配套）；XTextDocument 的片段字符格式结构体（XTDCharFormat）内 `char text[256]` → `XString*`（若该结构体是值语义则需先确认是否有 copy 语义——若无 copy 需求可保留内部缓冲，但用户要求"字符串使用 XString"，统一改）

- [ ] **Step 1: XTextBrowser 历史栈改造**

`m_history[32][256]` → `XString** m_history` + `int m_historyCapacity`；push/pop 用 XString_create/assign；`backward/forward` 用 XString_toUtf8 输出。历史容量从固定 32 改动态扩容（2 倍增长）。

- [ ] **Step 2: XTextBrowser source 与导航测试**

setSource 触发 sourceChanged 信号（信号签名 `void* XTextBrowser_sourceChanged_signal(XTextBrowser*, const char*)` 不变，发射时传 XString_toUtf8 临时指针）。backward/forward 用超长 URL 验证不截断。

- [ ] **Step 3: XTextDocument 字段改造**

`XTDCharFormat.text[256]` 与 XTextDocument 的 fontFamily/anchorHref/text/blockFormat/m_title/m_url 全部 XString*。检查 XTextDocument 是否 XObject 派生：若是，生命周期走 deinit/copy/move；若为纯 struct（栈上使用），需在 setter 内部创建 XString 并注意文档对象析构——按仓库既有模式处理（参考 XTDCharFormat 现有用法，若 XTDCharFormat 在 XTextDocument 内作为片段列表元素存储，则改为 `XString*` 并在文档析构时统一释放）。

- [ ] **Step 4: XWizard 标题改造**

m_title/m_subTitle → XString*；setTitle/setSubTitle/setButtonText 配套；回归测试 roundtrip。

- [ ] **Step 5: 主构建 + 回归 + 裁剪**

Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected: 通过。随后全部 build-crop-* 重新 configure + build。

### Task 4: XTableWidget 表头数组 XString 化

**Files:**
- Modify: `Src/XGui/Widget/XTableWidget.h/.c`（m_hHeaders[64] 二维数组、m_vHeaders[64] 二维数组）
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: `XString** m_hHeaders`、`XString** m_vHeaders`（动态数组，容量配套 m_hHeaderCount/m_vHeaderCount）；`XTableWidget_horizontalHeaderItem`/`verticalHeaderItem` 返回 `const char*` 不变

- [ ] **Step 1: 字段与 setHeaderLabel 改造**

`setHorizontalHeaderLabels(const char* const* labels, int count)` 内部逐项 XString_create_copy；`horizontalHeaderItem(col)` 返回 XString_toUtf8。

- [ ] **Step 2: 测试**

超长表头（>64）+ 多列（>32 列，验证动态扩容）。

- [ ] **Step 3: 构建 + 回归 + 裁剪**

### Task 5: Widgets 继承关系全量审计

**Files:**
- Modify: 审计结果涉及的 `Src/XGui/Widget/*.h`
- Test: `xgui_regression_test.c`（test_widgets_inheritance_contract）

**Interfaces:**
- Produces: 修正后的继承层次表（以 Qt 6.8.3 为准）

- [ ] **Step 1: 生成 Qt 继承基准表**

用脚本扫描 `/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/**/*.h` 的 `class X : public Y`，输出 50 控件对应 Qt 类的父类。

- [ ] **Step 2: 生成 XGui 继承现状表**

扫描 `Src/XGui/Widget/*.h` 的 `XCLASS_DEFINE_EXTEND_END(Xxx, Yyy)` 与 `typedef struct Xxx { Xyy m_base; }`。

- [ ] **Step 3: 差异修正**

逐类比对：父类不一致的（例如 XSpinBox 应为 XAbstractSpinBox 派生、XDateTimeEdit 应为 XAbstractSpinBox 派生、XComboBox 应为 QWidget 派生但若有 Qt 中间类则对齐）按 Qt 层次调整；涉及字段提升的同步改 .c 与调用点。

- [ ] **Step 4: 继承契约测试**

为每个控件断言 `XClassIsKindOf((XClass*)w, XWidget)` 成立、以及关键中间基类断言（如 XSpinBox → XAbstractSpinBox → XAbstractSpinBox → XWidget）。

- [ ] **Step 5: 构建 + 回归 + 裁剪**

### Task 6: Widgets API/行为对齐抽查（对照 Qt 头文件）

**Files:**
- Modify: 抽查发现的缺口 `Src/XGui/Widget/*.h/.c`
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: 与 Qt 6.8.3 头文件逐函数对齐的公共 API

- [ ] **Step 1: 生成 API 差异报告**

复用 `tools/scan_widget_signals.py` 思路写 `tools/scan_widget_api.py`：对每个控件类，从 Qt 头文件提取公共函数名集合，与 XGui 头文件提取的 `X<Class>_*` 函数集合比对，输出缺失清单。

- [ ] **Step 2: 按缺失清单逐类补 API（含中文 Doxygen）**

优先补齐：getter/setter 对、枚举、信号。每补一类跑一次回归。

- [ ] **Step 3: 全量验证**

主构建 + 回归 + 25 裁剪 + demo 截图。

### Task 7: XColorSpace 描述字段 XString 化（Graphics 收尾）

**Files:**
- Modify: `Src/XGui/Graphics/XColorSpace.h/.c`（m_description[64]、m_userDescription[64]）
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: `XString* m_description`、`XString* m_userDescription`

- [ ] **Step 1: 字段改造 + 生命周期 + setUserDescription/description 配套**
- [ ] **Step 2: 回归 + 裁剪验证**

### Task 8: Phase 1 全量验收

- [ ] **Step 1: 字符串残留扫描清零**

Run: `grep -rnE "char\s+\*?m_[a-zA-Z]+(\[[0-9]+\])?\s*;" Src/XGui/Widget/*.h Src/XGui/Graphics/*.h`
Expected: 无输出（除 XTextDocument 等已确认改造的）。

- [ ] **Step 2: 全量验证**

Run: 主构建 + `./bin/XGuiRegression_Test` + `ctest --test-dir build` + 26 裁剪全 PASS + `xgui_window_demo` 启动 + 截图（ffmpeg x11grab）确认无异常。

---

## Phase 2：Charts 模块继承对齐（Qt Charts 6.8.3）（进行中）

> 执行记录（2026-09-10）：Task 9 完成（XAbstractSeries 基类）；Task 10 完成
> （XXYSeries 基类 + 数据操作/外观/点选择/10 信号，XLineSeries/XScatterSeries/
> XSplineSeries 改继承，旧 API 不保留）；Task 11 完成（XAbstractBarSeries 基类
> + 数据/标签/信号，XBarSeries 改继承）；XPieSeries 改继承 XAbstractSeries；
> 点击/悬停命中测试与信号（pressed/clicked/released/hovered）已实现并入回归。
> 继承链：XY 三序列 → XXYSeries → XAbstractSeries；XBarSeries →
> XAbstractBarSeries → XAbstractSeries；XPieSeries/XAreaSeries → XAbstractSeries。
> 点击/悬停命中已实现：XChartView 鼠标事件 → 10px 阈值最近点命中 →
> pressed/clicked/released/hovered 信号 + 回归契约测试。
> 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
> demo 截图 + 交互点击稳定。
> Task 13 完成（2026-09-10 本轮）：209 API 缺口全部补齐并清零。新增：
> XAbstractSeries attachAxis/detachAxis/attachedAxes/type/axisCount/axisAt；
> XXYSeries pen/brush（C 参数化）/selectedColor/pointLabelsClipping/Font/
> points/pointsVector/bestFitLine 全系列/selectPoints/deselectPoints/
> toggleSelection/selectedPoints/lightMarker/pointConfiguration 批量与
> 单点/sizeBy/colorBy；XAreaSeries setUpperSeries/setLowerSeries/borderColor/
> pointLabels 全系列/color/pen/brush；XAbstractBarSeries insert/take/barSets/
> labelsPosition/labelsPrecision；XPieSeries slices；XPieSlice pen/brush/
> labelBrush/labelFont；XChart addAxis/removeAxis/axes/mapToPosition/
> titleFont/series。修复：XAbstractBarSeries remove 后残留槽指针双重释放。
> 验证：API 扫描缺口 206→0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
> demo 截图 + 交互稳定。

### Task 9: XAbstractSeries 基类落地（已完成大部分，收尾）

**Files:**
- Modify: `Src/XGui/Charts/XAbstractSeries.h/.c`（已创建，检查与 XChart.h 枚举去重）
- Modify: `Src/XGui/Charts/XLineSeries/XScatterSeries/XSplineSeries/XAreaSeries/XBarSeries/XPieSeries.h/.c`（已部分完成继承，检查 copy/move 虚表继承完整性与 XString 生命周期）

**Interfaces:**
- Produces: 完整可用的 XAbstractSeries 基类（name/visible/opacity/useOpenGL/chart + type 枚举唯一来源）

- [ ] **Step 1: 检查 copy/move 虚表继承**
- [ ] **Step 2: XBarSeries/XCategoryAxis m_categories → XString** 改造**
- [ ] **Step 3: 全量验证**

### Task 10: XXYSeries 中间基类（QXYSeries 对齐）

**Files:**
- Create: `Src/XGui/Charts/XXYSeries.h/.c`
- Modify: `Src/XGui/Charts/XLineSeries/XScatterSeries/XSplineSeries.h/.c`（改继承 XXYSeries）
- Test: `xgui_regression_test.c`（test_chart_xy_contract）

**Interfaces:**
- Produces: `XXYSeries`（XAbstractSeries 派生）：points/pointsVector/append/insert/replace/remove/removePoints/clear/count/at、color/pen 系列（仓库笔宽 m_width 对应 pen width）、pointLabelsVisible/pointLabelsFormat/pointLabelsColor、setPointsVisible、信号 clicked/hovered/pressed/released/doubleClicked/pointAdded/pointRemoved/pointReplaced/pointsReplaced

- [ ] **Step 1: 写失败测试（XXYSeries API 契约）**
- [ ] **Step 2: 实现 XXYSeries 基类（XObject 派生 + 虚表 + 数据操作 + 信号）**
- [ ] **Step 3: 三个序列改继承 XXYSeries，删除重复 API**
- [ ] **Step 4: 回归 + 裁剪**

### Task 11: XAbstractBarSeries + XBarSeries 对齐（QAbstractBarSeries 派生）

**Files:**
- Create: `Src/XGui/Charts/XAbstractBarSeries.h/.c`
- Modify: `Src/XGui/Charts/XBarSeries.h/.c`（继承 XAbstractBarSeries）
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: `XAbstractBarSeries`（XAbstractSeries 派生）：barWidth、count、at、append(label,value)、remove、clear、labelsVisible、labelsFormat、signals countChanged/labelsVisibleChanged

- [ ] **Step 1: 失败测试 → 实现基类 → 改造 XBarSeries → 验证**

### Task 12: XAreaSeries 对齐（QAreaSeries）

**Files:**
- Modify: `Src/XGui/Charts/XAreaSeries.h/.c`
- Test: `xgui_regression_test.c`

**Interfaces:**
- Produces: `setUpperSeries/lowerSeries/upperSeries/lowerSeries`、`setBorderColor/borderColor`、`pointLabelsVisible/Format`、signals clicked/hovered

- [ ] **Step 1: 失败测试 → 实现 → 验证**

### Task 13: Charts 209 API 缺口扫尾 + 全量验收

- [ ] **Step 1: tools/scan_charts_api.py 生成缺口清单并逐项补齐**
- [ ] **Step 2: 点击/悬停命中测试（pie slice、xy point）**
- [ ] **Step 3: Phase 2 全量验收**（主构建 + 回归 + CTest + 26 裁剪 + demo 截图）

---

## 风险与备注

- XTextDocument 的 XTDCharFormat 是值语义结构体（片段列表元素），改 XString* 后需确保所有赋值路径（insertFragment 等）深拷，否则悬垂。若改造风险过高，可先与用户确认该结构体保留内部缓冲的例外（其余全部 XString）。
- XWizard 的 m_buttonTexts 若为 `char[4][64]` 之类数组需一并处理（本次扫描未列出，实施时以扫描为准）。
- Charts 改造涉及 XChartView 渲染代码直接访问序列字段（m_points/m_count 等），基类化后需改用公共访问器。
- 每次共享源改动后 26 个裁剪构建必须重 configure（GLOB 缓存）。


---

## Phase 3：Fusion 风格引擎（批次 F1 已完成，2026-09-10）

> 执行记录：
> - **XStyleOption**（对标 QStyleOption）：State/PE/CE/CC/PM 枚举全对齐
>   Qt 6.8（State_Enabled/Sunken/MouseOver/HasFocus 等 25 位、PE 30 项、
>   CE 35 项、CC 7 项、PM 21 项）+ 通用控件字段（文本/进度/页签/滑块/菜单）。
> - **XStyle**（对标 QStyle）：XObject 派生 + 7 个虚表槽位
>   （DrawPrimitive/DrawControl/DrawComplexControl/PixelMetric/
>   SizeFromContents/Polish/Unpolish）+ 全局默认样式（懒创建 XCommonStyle）。
> - **XCommonStyle**（对标 QCommonStyle）：PE_Frame/PanelButtonCommand/
>   IndicatorCheckBox/IndicatorRadioButton/IndicatorArrow*/FrameFocusRect/
>   PanelLineEdit + CE_PushButton/CheckBox/RadioButton/ProgressBar 系列/
>   TabBarTabShape + 21 项 pixelMetric + sizeFromContents。
> - **XFusionStyle**（对标 QFusionStyle）：按钮垂直渐变（亮顶→暗底）、
>   按下反转渐变、悬停高亮顶边、焦点高亮内框、Fusion 复选/单选指示器
>   （悬停/选中高亮边框）、高亮色默认 #2A82DA、installDefault 入口。
> - **控件接入（首个）**：XPushButton_drawContents 面板+焦点框改走
>   XStyle_drawPrimitive（文本/图标保留原路径，避免回归）。
> - **回归契约**：test_fusion_style_contract（枚举/度量/分派/全状态绘制/
>   installDefault）。demo 已安装 Fusion 并验证按钮渐变 + 蓝色高亮生效。
> - **修复**：XStyle 枚举结尾误用 EXTEND_END（覆盖槽位数）→ XCLASS_DEFINE_END；
>   XPainter_drawTextRect 在 TEXTLAYOUT 裁剪下的守卫回退。
> - **验证**：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析确认渐变 + 高亮生效、交互稳定。
>
> 批次 F2（已完成，2026-09-10）：
> - XCheckBox：指示器走 PE_IndicatorCheckBox（style 分支独立计算 checkState，
>   修复 state 未初始化引用）；图标/文本保留原路径。
> - XProgressBar：槽（CE_ProgressBarGroove）+ 内容块（CE_ProgressBarContents）
>   走 style；文本绘制提取 xprogressbar_drawTextSegments 共享函数
>   （垂直旋转 + 水平分段裁剪完整复刻，两分支共用）。
> - XLineEdit：m_frame 时面板走 PE_PanelLineEdit + 焦点 PE_FrameFocusRect；
>   文本/占位/光标原路径。
> - **去精简修正（用户要求零近似）**：
>   XCommonStyle 复选对勾像素循环 → drawLine 折线（原 XCheckBox 坐标）；
>   单选逐行近似 → drawEllipse 外圆+内点；箭头阶梯 → drawPolygon 三角形；
>   焦点框 fillRect 段 → drawLine 虚线；进度槽四边 dark → 凹陷描边
>   （上左 dark 下右 light）；进度内容补 1px 内缩；drawCheckable 13/18
>   常量 → PM_IndicatorWidth/CheckBoxLabelSpacing 动态；
>   XFusionStyle 复选/单选同步去近似；XChart_mapToPosition 硬编码 400x300
>   → m_plotArea 映射（对标 QChart::mapToPosition）。
> - 裁剪守卫：drawEllipse（XPAINTER_SHAPE_ON）/drawPolygon
>   （XPAINTER_POLYGON_ON）裁剪关闭时回退逐行填充（与既有裁剪行为一致），
>   修复 min/painter/painter-off/shape/polygon/path/plugin-off-debug 7 个
>   裁剪构建；最终 26 裁剪全 PASS。
> - 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析（渐变列 15、蓝色高亮采样 170）+ 交互稳定。
>
> 批次 F3（进行中，2026-09-10 首批）：
> - XTabBar：页签形状（CE_TabBarTabShape）+ 标签（CE_TabBarTabLabel，选中
>   HighlightedText/未选中 WindowText + 焦点框）走 style。
> - XSpinBox：走 CC_SpinBox（frame PanelLineEdit + 上/下按钮
>   PanelButtonBevel + PE_IndicatorSpinUp/Down/Plus/Minus）；补
>   m_activeUp/m_activeDown 活动子控件跟踪（press 置位/release 复位，
>   对标 activeSubControls）与 XStyleOption spin 字段。
> - **XString 漏网修复**：XSpinBox m_prefix/m_suffix、XAbstractSpinBox
>   m_specialValueText 从 char* 转 XString*（此前扫描漏掉 char* 紧跟
>   格式；全套 getter/copy/move/deinit/字符剥离逻辑适配）。
> - XCommonStyle 新增：PE_IndicatorSpinUp/Down/Plus/Minus、
>   CE_TabBarTabLabel、CC_SpinBox（完整对齐 QCommonStyle）。
> - 已知技术债：XLineEdit 的 m_text/m_displayBuf/m_inputMask/
>   m_clipboardText 为编辑器字符级工作缓冲（undo/redo/掩码/光标基于
>   char* 索引），改 XString 需重写编辑器核心，单独评估。
> - XSlider 现有绘制已对齐 Fusion（sub-page 高亮），CC_Slider 接入待评估。
> - 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析（渐变列 15/蓝色高亮 170/纯黑 0%）+ 交互稳定。
>
> 批次 F3 二批（2026-09-10 完成）：
> - **XDial**：整体走 CC_Dial（XCommonStyle 完整迁移：刻度线区分大线/
>   小线（对标 calcLines 的 pageStep 倍数判定）、表盘圆 drawEllipse
>   （SHAPE 裁剪退化扫描线）、凹槽双弧 drawArc、calcArrow 三点箭头
>   drawConvexPolygon、**按箭头角度四分区选 light/dark 描边**（完整对齐
>   QCommonStyle 3D 高光/阴影方向）、焦点框）；XStyleOption 补 dial 字段
>   （dialWrapping/notchesVisible/notchSize/pageStep）。
> - **XGroupBox**：整体走 CC_GroupBox（镂空边框绕开标题区 + 标题文本 +
>   可勾选时 PE_IndicatorCheckBox，完整对齐 QCommonStyle）；XStyleOption
>   补 m_checkable/m_checked。
> - 原路径同步修复：XDial 刻度线大小区分（此前忽略 smallLine）。
> - 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析（渐变列 16/蓝色高亮 170/纯黑 0%）+ 交互稳定。
>
> 批次 F3 三批（2026-09-10 完成）：
> - **XSlider**：整体走 CC_Slider（XCommonStyle 完整迁移：凹槽 Base+Dark
>   1px 描边、handle 中心前 Highlight 子页高亮、刻度（tickInterval 0=自动
>   pageStep→singleStep→1，位置与把手一致，上/下单双侧）、把手 Button 底 +
>   Light 顶左/Dark 底右凸起边、水平与垂直两方向）；XStyleOption 补
>   sliderTickInterval/sliderTickPosition。
> - 至此 **Fusion 已接入 9 控件**：PushButton/CheckBox/ProgressBar/LineEdit/
>   TabBar/SpinBox/Dial/GroupBox/Slider。
> - 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析（渐变列 15/蓝色高亮 170/纯黑 0%）+ 交互稳定。
>
> 批次 F3 四批（2026-09-10 完成）：
> - **XScrollBar**：整体走 CC_ScrollBar——完整对标 QFusionStyle 非
>   transient 路径：groove 垂直渐变（buttonColor.darker 107/105/105/107
>   四点插值）+ alphaOutline（window.darker(140) alpha 180）顶/左边线 +
>   subtleEdge（alpha 40）内框；滑块 gradientStart(buttonColor.lighter
>   124)→gradientStop(lighter 102) 渐变、hover highlightedGradient
>   （start.darker(102)/stop.lighter(102)）、sunken midColor2
>   （merged 40）、alphaOutline 外框 + innerContrastLine（白 alpha 30）
>   内框；滑块几何由 min/max/pageStep/value 推算（同 XScrollBar 公式）。
>   新增 Fusion 色彩公式 helper：xcs_lighter/xcs_darker/xcs_gray/
>   xcs_merged/xcs_buttonColor（含 RGB→HSV→饱和度 0.75→RGB 完整复刻）。
> - 至此 **Fusion 已接入 10 控件**：PushButton/CheckBox/ProgressBar/
>   LineEdit/TabBar/SpinBox/Dial/GroupBox/Slider/ScrollBar。
> - 验证：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全 PASS、
>   demo 像素分析（渐变列 16/蓝色高亮 170/纯黑 0%）+ 交互稳定。
>
> 批次 F3 收尾（2026-09-10 完成）：
> - **XComboBox**：CC_ComboBox（PE_PanelLineEdit 面板 + 右端 16px Button
>   下拉区 + PE_IndicatorArrowDown 三角箭头）；弹出态 Sunken|On。
> - **XMenuBar**：PE_PanelMenuBar 面板（Button 色填充+底部暗线）+ 每项
>   CE_MenuBarItem（选中按下 highlight 填充 + highlight.darker(125) 边框
>   + HighlightedText；未选中 mergedColors(window.darker(120),
>   outline.lighter(140), 60) 底部阴影线——完整复刻 QFusionStyle）。
> - **XMenu**：PE_PanelMenu 面板 + 每条目 CE_MenuItem（选中 highlight +
>   HighlightedText，禁用 Mid 色，分隔线保留）。
> - **XToolBar**：PE_PanelToolBar 面板。
> - 至此 **Fusion 引擎接入全部 14 控件**：PushButton/CheckBox/ProgressBar/
>   LineEdit/TabBar/SpinBox/Dial/GroupBox/Slider/ScrollBar/ComboBox/
>   MenuBar/Menu/ToolBar。
>
> ## Phase 4：QSS 样式表（已完成，2026-09-10）
>
> **继承链 1:1**：XStyleSheetStyle : XWindowsStyle : XCommonStyle : XStyle
> （= QStyleSheetStyle : QWindowsStyle : QCommonStyle : QStyle）；
> XFusionStyle 已改继承 XWindowsStyle（= QFusionStyle : QWindowsStyle）。
>
> - **QSS-0 XWindowsStyle**：薄中间层（polish 钩子 + styleHint 分派点）。
> - **QSS-1 XCssStyleSheet 解析器**：XCssProperty 枚举（24 属性）、
>   XCssDeclaration（propertyId/important/value）、XCssBasicSelector
>   （elementName/#id/伪类位）、XCssSelector（specificity = id*100 +
>   伪类*10 + 元素）、XCssStyleRule/StyleSheet；解析支持 `selector { prop:
>   value; }`、逗号组合、`#id`、`:hover/:pressed/:focus/:disabled/
>   :enabled/:checked/:selected/:readonly`、`/* */` 注释、`!important`、
>   大小写不敏感属性名。
> - **QSS-2 XStyleSheetStyle**：规则匹配（元素名=控件类名
>   XVTABLE_GET_NAME、#id=objectName、伪类=XStyleState 位映射）、
>   specificity 排序、级联（后声明覆盖/!important 优先）；**色值解析**
>   XCssParseColor（#RGB/#RRGGBB/#AARRGGBB/rgb()/rgba()/具名色）+ 长度
>   XCssParseLength；绘制分派：底层样式先画（文本色在 option 预覆盖），
>   QSS 背景色绘制后覆盖（对标 QSS 级联优先级）；未命中完全回落底层。
> - **QSS-3 接入**：`XStyle_installStyleSheet(css)` 全局安装 API
>   （包装当前默认样式/复用已装包装更新规则表）；demo 安装示例 QSS。
> - **回归**：test_qss_contract（解析/规则数/颜色 5 形态/长度/属性名/
>   空表清零/端到端像素级背景覆盖断言）。
> - **验证**：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全
>   PASS、demo 像素分析（Fusion 渐变列 15 保留 + **QSS 黄底 #FFFFE0
>   采样 151 生效** + 纯黑 0%）+ 交互稳定。
>
> ### Phase 4 补强（2026-09-10 本轮）
>
> - **QSS 盒模型闭环**：padding（四值/单值简写 + 四向独立声明）内容区
>   内缩、border-width/color/radius 覆盖绘制（drawLine 多层直角框 +
>   drawRoundedRect 圆角分支）、border 简写颜色 token 提取、无色回落
>   currentColor；**修复三处根因**：①虚表槽位 size 推进缺失（XStyle/
>   XWindowsStyle 显式 ADD_FUNC_LIST，否则子类继承时槽位为 NULL 调用
>   崩溃——完整根因链：XVTABLE_OVERLOAD 下标写不推进 size，INHERIT
>   只复制 size 个槽）；②QSS 背景覆盖顺序（底层先画、QSS 背景后覆盖，
>   对标级联优先级）；③border 画线前未 setPen。
> - **全状态矩阵测试**：test_fusion_state_matrix（Raised/Sunken 渐变
>   反转像素断言、CheckBox On/Off、禁用灰化不崩）+ QSS 盒模型像素级
>   断言（中心绿背景 + (1,1) 红边框）。
> - **验证**：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全
>   PASS、demo 像素分析（渐变列 15/蓝色 314/QSS 黄底 151/纯黑 0%）+
>   交互稳定。
>
> ### Phase 4 补强二（2026-09-10 本轮）
>
> - **QSS font 覆盖**：font-family/font-size 命中时 XPainter_setFont
>   （基于画家当前字体副本改族/像素尺寸）。
> - **:hover 动态生效闭环**：XWidget Enter/Leave 事件翻转 UnderMouse
>   属性后补 XWidget_update（对标 Qt hover 样式刷新）；回归断言
>   hover 状态翻转 → QSS 橙色 #FFA500 背景命中/未命中像素差异。
> - **demo 实测**：QSS 橙色 hover 采样 10（鼠标悬停控件生效）。
> - **验证**：主构建 EXIT=0、软件/GPU 回归全绿、CTest 3/3、26 裁剪全
>   PASS、demo 像素分析（渐变列 15/蓝色 314/QSS 黄底 151/QSS 橙
>   hover 10/纯黑 0%）+ 交互稳定。
>
> ### 已知边界（记录，非阻塞）
>
> - XScrollBar 按钮（SubLine/AddLine）：XScrollBar 控件本身无按钮语义
>   （Fusion transient 同构），SC_ScrollBarSubLine/AddLine 子控件不绘制。
> - XLineEdit 编辑缓冲（m_text/m_displayBuf/m_inputMask/m_clipboardText）
>   为字符级工作区，XString 化需重写编辑器核心，单独评估。
> - XColorSpace 值语义 char[64]（拷贝契约）。


---

## Phase 4：QSS 样式表解析（对标 Qt QStyleSheetStyle，排队中）

> 背景：Qt 里样式表是独立一层。**继承链 1:1**（已核对 Qt 6.8.3 源码）：
> QStyleSheetStyle : QWindowsStyle : QCommonStyle : QStyle
> （qstylesheetstyle_p.h:39 / qwindowsstyle_p.h:28 / qcommonstyle.h:14）。
> 解析器在 QtGui：qcssparser.cpp（private），核心结构对齐目标：
> Declaration（Property 枚举 ~100 项：BackgroundColor/Color/Font/Border/
> Padding/Margin/...）、BasicSelector（elementName/ids/pseudos/
> attributeSelectors/relationToNext）、Selector（specificity()/pseudoClass()/
> pseudoElement()）、StyleRule、StyleSheet。
> XGui 现状：XWidget.m_styleSheet 只存储不解释；Fusion 引擎是代码级绘制。
> 分层原则：QStyleSheetStyle 包装（而非替代）XStyle 底层引擎。
> 注：XGui 当前继承链为 XFusionStyle : XCommonStyle : XStyle（Fusion 已对齐
> QFusionStyle : QCommonStyle）；QWindowsStyle 需作为中间层补建。

### Task QSS-0: XWindowsStyle 中间层（继承链 1:1 补齐）

**Files:**
- Create: `Src/XGui/Style/XWindowsStyle.h/.c`

**Interfaces:**
- Produces: `XWindowsStyle`（XCommonStyle 派生，对标 QWindowsStyle : QCommonStyle；
  Qt 6 中该层很薄——NativeTheme 接入与少量 PM 覆盖，绘制逻辑继承 QCommonStyle）；
  XFusionStyle 改继承 XWindowsStyle（对齐 QFusionStyle : QWindowsStyle）
- [ ] 创建 + XFusionStyle 基类切换 + 回归

### Task QSS-1: CSS 子集解析器（对齐 qcssparser 结构）

**Files:**
- Create: `Src/XGui/Style/XStyleSheetParser.h/.c`

**Interfaces:**
- Produces（结构 1:1 对齐 qcssparser_p.h）:
  - `XCssDeclaration`（对标 Declaration）：`XCssProperty` 枚举
    （UnknownProperty/BackgroundColor/Color/Font/FontFamily/FontSize/FontStyle/
    FontWeight/Margin 系列/TextDecoration/Border 系列/Padding 系列/...）+
    `m_propertyId`/`m_important`/`m_inheritable` + 值提取
  - `XCssBasicSelector`（对标 BasicSelector）：`m_elementName`/`m_ids`/
    `m_pseudos`/`m_attributeSelectors`/`m_relationToNext`
    （NoRelation/Ancestor/Parent/DirectAdjacent/IndirectAdjacent）
  - `XCssSelector`（对标 Selector）：`m_basicSelectors` +
    `XCssSelector_specificity()`/`XCssSelector_pseudoClass()`/
    `XCssSelector_pseudoElement()`
  - `XCssStyleRule`（对标 StyleRule）；`XCssStyleSheet`（对标 StyleSheet）
  - `XCssParser_parse(const char* css, XCssStyleSheet* out)`（UTF-8）
  - 支持语法：`selector { prop: value; }`、组合选择器（逗号分隔）、
    后代/子代关系（空格/>）、伪类 `:hover`/`:pressed`/`:focus`/`:disabled`、
    ID 选择器 `#name`、属性选择器 `[attr=val]`、注释 `/* */`、`!important`
- [ ] 词法/解析实现 + 回归测试（规则数/属性值/伪类/specificity 断言）

### Task QSS-2: XStyleSheetStyle 风格包装

**Files:**
- Create: `Src/XGui/Style/XStyleSheetStyle.h/.c`

**Interfaces:**
- Produces: `XStyleSheetStyle`（**XWindowsStyle 派生**，1:1 对齐
  QStyleSheetStyle : QWindowsStyle；包装 XFusionStyle 或 XCommonStyle）：
  - `setStyleSheet(const char* css)`（对标 QStyleSheetStyle::setStyleSheet
    语义）解析并重建 XCssStyleSheet 规则表
  - drawPrimitive/drawControl：先按控件类名/对象名/伪类状态匹配 StyleRule
    （specificity 排序），命中 Declaration 覆盖绘制
    （BackgroundColor/Color/Border/Padding/Font），未命中回落底层 style
  - StyleRule 按 Qt 规则：elementName 匹配类名、id 匹配 objectName、
    pseudoClass 匹配 XStyleState 位
- [ ] 规则匹配 + 属性覆盖绘制 + specificity 排序 + 回落路径测试

### Task QSS-3: XWidget 接入

**Files:**
- Modify: `Src/XGui/Widget/XWidget.c`（setStyleSheet 触发解析与重绘）
- Modify: `Src/XGui/Style/XStyle.c`（全局默认样式可替换为 XStyleSheetStyle）

**Interfaces:**
- Produces: `XWidget_setStyleSheet` 后按规则重绘；样式优先级：
  QSS 命中规则 > 全局默认 style > 控件内置绘制
- [ ] 接入 + 回归 + demo 截图验证（QSS 覆盖背景色/文字色效果）


---

## Phase 5：剩余 36 控件 style 接管（已提交 d7034750 后继续）

> 基线：d7034750 全绿（主构建/软件+GPU 回归/CTest 3/3/26 裁剪）。
> 已接入 14 控件，本阶段补齐其余控件绘制接管（分 4 批）。

### 批次 G1：按钮家族（RadioButton/ToolButton/CommandLinkButton）
- XRadioButton：CE_RadioButton（XCommonStyle 已实现单选指示器+标签）
- XToolButton：CE_ToolButtonLabel + PE_PanelButtonTool（AutoRaise 悬停凸起）
- XCommandLinkButton：CE_PushButton 变体（图标+两行文本）
- 验证：回归 + demo

### 批次 G2：输入/容器（FontComboBox/ScrollArea/TextEdit 家族/DockWidget）
- XFontComboBox：CC_ComboBox（继承 XComboBox 路径）
- XTextEdit/XPlainTextEdit/XTextBrowser：PE_PanelLineEdit 帧接入（基类
  XAbstractScrollArea 视口）
- XDockWidget：CE_DockWidgetTitle
- 验证：回归 + demo

### 批次 G3：导航/容器（TabWidget/MainWindow/ToolBox/Splitter/SizeGrip/RubberBand）
- XTabWidget：PE_FrameTabWidget + 页签容器
- XMainWindow：菜单/工具栏/停靠区组合（子控件已接）
- XToolBox：CE_ToolBoxTab
- XSplitter：CE_Splitter；XSizeGrip：CE_SizeGrip；XRubberBand：CE_RubberBand
- 验证：回归 + demo

### 批次 G4：表格/日历/对话框杂项（TableWidget/CalendarWidget/Dialog 家族/
- XTableWidget：表头走 CE_HeaderSection/CE_HeaderLabel
- XCalendarWidget：导航条/日期格
- XDialog/XDialogButtonBox/XErrorMessage/XMessageBox：按钮已接（基类组合）
- XStatusBar：已接；XFocusFrame：PE_FrameFocusRect
- XKeySequenceEdit/XLCDNumber/XStackedWidget/XLabel：评估接入点
- 验证：回归 + 26 裁剪 + demo + 收尾汇总
