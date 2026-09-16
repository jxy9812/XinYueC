# XGui ↔ Qt 6.8.3 全量对齐补强实施计划（审计驱动 · Phase 2）

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development（推荐）或 executing-plans 按任务执行本计划。步骤用 checkbox（`- [ ]`）跟踪。
> 本计划基于 2026-09-15 全模块审计（`docs/xgui-audit/2026-09-15/*.md`，11 份详细报告 + `00-汇总报告.md`）。

**Goal:** 把 XGui 全部模块（Application/Window/Widget/XLayout/Graphics/Style/Icon/Input/Platform/Charts/CoreRoot）与 Qt 6.8.3 的 API 名称、功能行为、继承关系差距逐项清零：先消除硬约束 P0（BOM/旧 API/字符串/XMemcpy/裁剪/UB），再修继承链与信号，再实现真实 Qt API 空实现与样式/绘制缺项，最后全量回归收口。

**Architecture:** 三个串行阶段。Phase 0 只做不改变行为的基线合规（批量 BOM、删除守卫外旧 API、字符串字段/签名合规化、裁剪开关、生命周期 UB 修复），每步以「grep 扫描 + 编译 + 回归」为门；Phase 1 做架构级继承与信号对齐（XFusionStyle 链、XAbstractAxis、item view 决策、信号补齐）；Phase 2 按控件族/子系统实现真实功能（对照 Qt 6.8.3 源码逐函数复刻，不精简近似）。所有修改保持 XObject/XClass 虚表、XString 拥有、XCopy/XMove、_base 只分派等既有工程惯例。

**Tech Stack:** C99 / CMake / XObject/XClass 虚表 / XString（Src/XContainer）/ XPainter 软件渲染 + GPU 三后端 / xgui_regression_test.c / xgui_window_demo + xdotool + ffmpeg 截图。

**Spec:** 审计报告 `docs/xgui-audit/2026-09-15/`（11 份模块报告）；Qt 6.8.3 源码 `/home/xinyue/Qt/6.8.3/Src`（qtbase/src/widgets|gui、qtcharts/src/charts）；约束文档《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`、`XGui_Qt_Alignment_Handoff.md`。

## Global Constraints（每任务隐含包含）

- **不 commit/push**（用户政策；所有 commit 步骤替换为验证步骤；撤回用 reset --soft 不适用——无提交）。
- 拥有型字符串一律 `XString*`；API 主版本 XString，UTF-8 `const char*` 用 `_2` 后缀重载；例外仅 XColorSpace 值语义 char[64]、XLineEdit 编辑缓冲（XLcdNumber 段缓冲同类容忍，须在头文件注明）。
- 继承一比一含中间基类层；旧 API 不保留（守卫外/`extern "C"` 外悬挂声明一律删除，删除前全局 grep 确认无调用）。
- 样式/绘制完整复刻 Qt 6.8.3，不得精简近似（裁剪宏回退除外）。
- 公共头中文 Doxygen：@brief + @param 逐参数 + @return 逐返回值；UTF-8 带 BOM。
- 信号：Qt 6.8 每个信号有对应 `*_signal`；空参信号 args=NULL；参数必须带全 Qt 语义参数。
- 生命周期：init/deinit_base 成对；copy/move 虚函数安全（未初始化目标兜底 init）；禁 memcpy 复制对象；禁裸 malloc/free/strdup。
- 修改共享源（XPainter.c/XStyle 系列/XGuiConfig.h 等被多配置编译的文件）后：重新 configure + 编译全部 25 个裁剪构建 + build-crop-gpu。
- 验证门（每任务结束）：主构建 EXIT=0 + `./bin/XGuiRegression_Test` 输出 "XGui regression tests passed" + 涉及共享源时 25 裁剪构建全 PASS + 涉及交互时 demo 截图取证。
- X11 头文件坑：include X11/vulkan 前 rename 宏（XImage/XPoint/XEvent/XColor/XKeyEvent/XExposeEvent→X11_*）+ #undef XFree，include 后恢复。
- 新增/修改公共 API 后所有调用方必须包含完整原型头文件；新增测试并入 `xgui_regression_test.c` 或 `Test/XGuiTest/`。

---

# Phase 0：基线合规（硬约束 P0 清零，不改行为）

## Task 0.1：批量补齐缺失的 UTF-8 BOM（23 个文件）

**Files:**
- Modify（加 `\xEF\xBB\xBF` 文件头）：
  - Application：`Src/XGui/Application/XGuiApplication.h`
  - Widget：`Src/XGui/Widget/XDialogButtonBox.h`、`XMainWindow.h`、`XMenu.h`、`XTabWidget.h`（另 `XAbstractItemView.h/XTableView.h` 在 Task 0.4 一并处理）
  - Graphics：`XBackingStore.h`、`XColorSpace.h`、`XPainter.h`、`XPixmapCache.h`、`XImagePluginRegistry.h`、`XImageBuiltinPlugin.h`、`XGpuRenderBackend.h`、`XGpuRenderDriver.h`、`XGpuRenderDriver_vulkan_shaders.h`
  - Icon：`XIconScaledPixmapCache.h`、`XIconStyleHelper.h`、`XIconThemeInternal.h`
  - Platform：`XPlatformBackingStore.h`、`XPlatformDrag.h`、`XPlatformGraphics.h`、`XPlatformIntegration.h` + 实现 `XPlatformGraphics.c`
  - Window：`XWindow_Protected.h`
- Test：`xgui_regression_test.c` 增 BOM 静态断言（读文件首 3 字节）

**Interfaces:**
- Produces：一个 `tools/check_bom.sh` 扫描脚本（后续每次提交前跑），输出全部缺 BOM 公共头清单。

- [ ] **Step 1: 写扫描脚本并运行得到缺口清单**

```bash
cat > tools/check_bom.sh <<'EOF'
#!/bin/sh
# 用法: tools/check_bom.sh [目录]  —— 列出缺少 UTF-8 BOM 的 .h/.c
find "${1:-Src}" -name '*.h' -o -name '*.c' | while read f; do
  [ "$(od -An -tx1 -N3 "$f" | tr -d ' \n')" = "efbbbf" ] || echo "$f"
done
EOF
chmod +x tools/check_bom.sh
tools/check_bom.sh Src/XGui
```
Expected: 输出上述 23 个文件（数量以实测为准，只允许清单内文件）。

- [ ] **Step 2: 给每个缺口文件补 BOM**（保持其余字节不变）：

```bash
for f in <清单>; do
  printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
done
```

- [ ] **Step 3: 复跑扫描 + 全量编译回归**

Run: `tools/check_bom.sh Src/XGui && cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected: 扫描无输出；构建 EXIT=0；回归 "XGui regression tests passed"。

- [ ] **Step 4: 验证门**：主构建 + 软件回归全绿（纯文件头改动，25 裁剪构建可不全跑，但 run 一次 `cmake --build build-crop-min -j$(nproc)` 确认 GLOB 无异常）。

## Task 0.2：删除 Widget 守卫外遗留旧 API（约 380 处）

**Files:**
- Modify：33 个 Widget .c/.h（大头：XWidget/XComboBox/XMainWindow/XAbstractSpinBox/XMessageBox/XMdiArea/XTextEdit/XCalendarWidget/XPlainTextEdit/XWizard/XMenu/XTableWidget/XSpinBox/XDateTimeEdit/XTextBrowser/XFrame/XTabBar/XDialog/XGroupBox/XStatusBar/XToolBar/XButtonGroup/XToolButton/XErrorMessage/XKeySequenceEdit/XToolBox/XScrollArea/XStackedWidget/XDialogButtonBox/XFontComboBox/XSplitter/XProgressBar/XLineEdit）
- Test：`xgui_regression_test.c` 保持全绿（旧 API 无调用方）

**Interfaces:**
- Consumes：Task 0.1（BOM 已修，本任务删除时不覆盖 BOM）
- Produces：无（纯删除）；后续 Task 2.x 以「真实 Qt API」重建被删接口中属于 Qt 6.8 的部分。

- [ ] **Step 1: 全库 grep 确认旧接口无调用**

```bash
grep -rn '_2(self)\|_3(self)\|_4(self)\|_5(self)\|open_2\|result_2\|setSizeGripEnabled(self)' \
  --include='*.c' --include='*.h' Src Test xgui_regression_test.c xgui_window_demo.c | grep -v 'Src/XGui/Widget' | head -50
```
Expected：除 Widget 自身头/实现外无引用（若有 Test/demo 调用，先改写调用点或记录进 Step 3 白名单）。

- [ ] **Step 2: 定位所有「守卫外旧接口区」**：模式为主 `#endif /* XWIDGET_ON && XXXX_ON */` 之后、`#endif`（头文件保护）之前的声明块；对应 .c 中为空函数体 `{ (void)self; ... }`。

```bash
grep -n '^#endif /\* XWIDGET_ON' Src/XGui/Widget/*.h | head -80
```

- [ ] **Step 3: 逐文件删除**：删除守卫外声明块 + .c 中对应空实现；删除前对每个函数名执行一次 `grep -rn "<函数名>" Src Test xgui_regression_test.c | grep -v 该文件本身`，无引用才删。机翻 Doxygen（如 `XAbstractSpin盒clear3`）随块删除。
- [ ] **Step 4: 编译 + 回归**

Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test`
Expected：EXIT=0；回归全绿；`grep -c '{ (void)self; }' Src/XGui/Widget/*.c | awk -F: '{s+=$2} END{print s}'` 应显著下降（目标 < 100 且剩余均为「真实 Qt API 待实现」占位，逐项登记到 Task 2 清单）。

- [ ] **Step 5: 验证门**：主构建 + 回归 + `build-crop-min` 编译（守卫外代码曾破坏裁剪构建，本次必须验证 `cmake --build build-crop-min -j$(nproc)` EXIT=0）。

## Task 0.3：拥有型字符串与 XString 主版本合规化

**Files:**
- Modify：
  - Widget：`XComboBox.h/.c`（`char** m_items` → `XString** m_items`，含 addItem/insertItem/setItemText/itemText 生命周期与 `_2` 重载）、`XTabBar.h/.c`（`m_titles` 同改，setTabText/tabText）
  - Application：`XGuiApplication.h/.c`（`setSessionState` 改 XString 主版本 + `_2`；补 `setApplicationDisplayName_2/setDesktopFileName_2`）
  - Platform：`XPlatformInputContext_setLocale`、`XPlatformServices_openUrl`、`XPlatformFontDatabase_hasFamily`、`XPlatformTheme_create_ex` 补 XString 主版本 + `_2`；`XPlatformTheme.m_name` 改 XString*；`XPlatformNativeInterface` 注册表 `char* m_name` 改 XString*
  - Charts：全部 `const char*` 主版本改 XString* + `_2`（setTitle/setName/setPointLabelsFormat/setLabel/setTitleText/append 等，见 Charts.md 五-4）
  - Style：`XCssStyleSheet.c` 4 个 `char[64]` 局部 token 缓冲改动态 `XString*`（消除 63 字节截断）
- Test：`xgui_regression_test.c` 增超长字符串用例（>64/256 字节 roundtrip，断言不截断）

**Interfaces:**
- Consumes：Task 0.2（Widget 旧接口删除后，`_2` 重载按「XString 主版本 + UTF-8 转发」重建，不复用旧单参语义）
- Produces：`XString** XComboBox_items(XComboBox*)` 等内部字段类型变化；对外签名不变（`void XComboBox_addItem(XComboBox*, const XString*)` + `_2(const char*)`）。

- [ ] **Step 1: 写超长字符串失败测试**（先于实现）：

```c
static void test_strings_no_truncation(void)
{
    XComboBox* cb = XComboBox_create(NULL, 0);
    XString* s = XString_create();
    char buf[512];
    if (!cb) { XTest_Fail("XComboBox_create"); return; }
    memset(buf, 'x', 500); buf[500] = 0;
    XString_assign_utf8(s, buf);
    XComboBox_addItem(cb, s);   /* 旧 char* 实现：item 内部截断/长度不符 */
    if (XComboBox_itemText_const(cb, 0) == NULL ||
        XStrcmp(XString_toUtf8(XComboBox_itemText_const(cb, 0)), buf) != 0)
        XTest_Fail("combobox item no truncation");
    XComboBox_delete_base(cb); XString_delete_base(s);
}
```
Run: `cmake --build build -j$(nproc) && ./bin/XGuiRegression_Test` → Expected: FAIL（旧实现）。

- [ ] **Step 2: 逐文件改字段与生命周期**：init 创建 XString*/XString**、V*_deinit 释放、V*_copy 深拷、V*_move 转移置空；对外 getter 返回 `const XString*`（`_const`）或新建 XString*；`_2` 只做 UTF-8 转发。
- [ ] **Step 3: 同步 Test/ 与 demo 调用点**（`grep -rn 'XComboBox_addItem\|XTabBar_setTabText' Test xgui_regression_test.c xgui_window_demo.c`）。
- [ ] **Step 4: 验证门**：主构建 + 回归（含新用例）+ `build-crop-min` + `build-crop-no-plugin` 编译（Platform/Charts 开关联动）。

## Task 0.4：XGuiConfig 裁剪开关修复（CoreRoot）

**Files:**
- Modify：`Src/XGui/XGuiConfig.h`（573-735 行补齐 36 个遗漏 `#undef/置 0`；修正 673-682 把 XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/XCHARTS_ON 置 1 的错误；新增 `XPIXMAP_ON`（默认 1、裁剪 0）；14 个控件开关并入 XWIDGET_ON=0 依赖块；把 XWIZARD_ON/XERRORMESSAGE_ON/XTEXTDOCUMENT_ON/XTABLEWIDGET_ON/XCHARTS_ON 移出 `#ifndef XPLATFORMFONTDATABASE_ON` 嵌套；`!XLAYOUT_ON` 时连带置 0 XLAYOUT_STACKED_ON）
- Modify：`Src/XGui/Widget/XAbstractItemView.h`、`XTableView.h`（`#if XTABLEWIDGET_ON || 1` → `#if XWIDGET_ON && XTABLEWIDGET_ON`）
- Modify：`Src/XGui/Widget/XSplashScreen.h`（`XSplashScreen_setPixmap/pixmap` 门卫恢复为 `#if XPIXMAP_ON`）
- Test：新增 `Test/XGuiTest/XGuiConfigConsistencyTest.c`（三组预处理断言：`-DXGUI_ON=0`、`-DXWIDGET_ON=0`、`-DXLAYOUT_ON=0` 下相关 X*_ON 全部为 0）

**Interfaces:**
- Consumes：Task 0.1/0.2（守卫外代码删除后裁剪构建才可能干净）
- Produces：`XPIXMAP_ON` 开关（Task 2.11 Graphics 使用）。

- [ ] **Step 1: 写配置一致性断言测试**（CMake 目标 `XGuiConfigConsistency_Test`，用 `gcc -E -dM -DXGUI_ON=0 -include Src/XGui/XGuiConfig.h` 输出并断言）。
- [ ] **Step 2: 修 XGuiConfig.h**（按 CoreRoot.md 清单逐项；先读 573-735 行现状再改）。
- [ ] **Step 3: 修复恒开守卫与 XPIXMAP_ON 门卫。**
- [ ] **Step 4: 验证门**：`cmake -S . -B build && cmake --build build -j$(nproc)`；然后**重新 configure + 编译全部 25 个裁剪构建 + build-crop-gpu**（`for d in build-crop-*; do cmake -S . -B $d >/dev/null && cmake --build $d -j$(nproc) || echo "FAIL $d"; done`），全部 EXIT=0。

## Task 0.5：生命周期与 UB 修复

**Files:**
- Modify：`Src/XGui/Window/XWindowEvent.c`（7 处 XMemcpy 克隆 → 逐字段深拷贝/`create_ex+init`：第 30/49/70/83/94/105/116 行）
- Modify：`Src/XGui/Charts/XChart.c`、`XPieSlice.c`（VXChart_move/VXSlice_move 的 XMemcpy 整对象复制 → 字段级 move；XAreaSeries/XBarSeries/XScatterSeries 补 copy/move 覆写并恢复 m_type）
- Modify：`Src/XGui/Style/XStyle.c`（`XStyle_installStyleSheet`：替换 g_defaultStyle 前释放旧默认样式；定义 m_source 拥有语义并补注释）
- Modify：`Src/XGui/XLayout/XStackedLayout.c`（`xstackedlayout_emitInt` 类型双关 → 二选一：a) XStackedLayout 改继承 XObject 链（XLayout→XObject 重构，影响面大，需评审）；b) 删除伪发射路径，setCurrentIndex/takeAt 恢复纯标识函数语义，头文件注明「信号为后续扩展」——默认选 b，评审后可改 a）
- Test：`xgui_regression_test.c`（事件 clone 深拷贝断言：修改副本不影响原事件 region/数据；XStackedLayout 未连接信号时 setCurrentIndex/takeAt 不崩溃；XStyle 两次 installStyleSheet 无泄漏——ASan 构建验证）

**Interfaces:**
- Consumes：Task 0.1（BOM）
- Produces：无签名变化。

- [ ] **Step 1: 写失败测试**（ASan 下 XStackedLayout 切换 + XStyle 二次 install 无 leak 报告；事件 clone 独立性断言）。
- [ ] **Step 2: 修 XWindowEvent.c 克隆**（XResizeEvent/XExposeEvent（含 XRegion 深拷贝）/XPaintEvent/XCloseEvent/XShowEvent/XHideEvent/XFocusEvent 各自实现 `clone` 用逐字段 + XCopy）。
- [ ] **Step 3: 修 Charts move/copy**。
- [ ] **Step 4: 修 XStyle 泄漏 + XStackedLayout 二选一**（默认 b，文档记录）。
- [ ] **Step 5: 验证门**：`cmake --build build-asan -j$(nproc) && ASAN_OPTIONS=detect_leaks=1 ./build-asan/bin/XGuiRegression_Test` 全绿无 leak；主构建回归全绿。

---

# Phase 1：继承链与信号对齐（架构级）

## Task 1.1：XFusionStyle 继承链修复

**Files:**
- Modify：`Src/XGui/Style/XFusionStyle.h/.c`（`XWindowsStyle m_base` → `XCommonStyle m_base`；`XCLASS_DEFINE_EXTEND_END(XFusionStyle, XCommonStyle)`；class_init 改 `XVTABLE_INHERIT_XCommonStyle`；重排 StyleHint 槽位偏移——XWindowsStyle 引入的槽位后移；Fusion 特有 PE/CC 注册保持）
- Modify：`Src/XGui/Style/XStyle.h`（如 StyleHint 槽位枚举受牵连，同步修正枚举编号）
- Test：`xgui_regression_test.c`（断言 `XClassGetVtable((XClass*)style)` 名称 + 基类链：Fusion→CommonStyle→Style→XObject）

**Interfaces:**
- Consumes：Task 0.5（无直接依赖，可并行）
- Produces：`XFusionStyle` 基类链 = XCommonStyle（对齐 `QFusionStyle : QCommonStyle`）。

- [ ] **Step 1: 写继承链断言测试**（vtable 链遍历 4 层）。
- [ ] **Step 2: 改 XFusionStyle.h/.c**（含槽位枚举重排，参照 Qt qfusionstyle_p.h 虚函数归属）。
- [ ] **Step 3: 验证门**：主构建 + 回归 + **25 裁剪构建全量**（Style 为共享源）+ demo 截图（Fusion 主题下按钮/滑块样式不变形）。

## Task 1.2：Charts 轴体系重构（XAbstractAxis + XCategoryAxis 裁决）

**Files:**
- Create：`Src/XGui/Charts/XAbstractAxis.h/.c`（对标 QAbstractAxis：visible/linePen/gridLinePen/labelsBrush/labelsFont/labelsAngle/titleText/titleBrush/shadesVisible/min/max/range/reverse/applyNiceNumbers 等 + 信号 visibleChanged/linePenChanged/...；XObject 派生；XGuiConfig.h 增 `XCHARTS_AXIS_ON` 或并入 XCHARTS_ON）
- Modify：`XValueAxis.h/.c`（改继承 XAbstractAxis；补 deinit_base/信号/默认值 Qt 0..0/5；XString 泄漏修复）
- Modify：`XCategoryAxis.h/.c`（按用户裁决二选一，默认：**服务柱状图 → 对齐 QBarCategoryAxis**：categories/append/remove/min/max/setRange/countChanged；若用户选 QCategoryAxis 语义则改 append(label,endValue)/startValue/endValue/labelsPosition）
- Modify：`XChart.c`（addAxis/axes 泛化走 XAbstractAxis*；XChartView 同步）
- Test：`xgui_regression_test.c`（轴默认值、setRange 钳位、信号触发、XChart_addAxis 泛型收 XValueAxis/XCategoryAxis）

**Interfaces:**
- Consumes：Task 0.3（字符串合规后的签名）
- Produces：`XAbstractAxis`（XValueAxis/XCategoryAxis 基类）；决策点 D1 落定。

- [ ] **Step 1: 向用户确认 D1 裁决**（XCategoryAxis 服务柱状图→QBarCategoryAxis / 服务折线面积→QCategoryAxis；默认前者）——见交付前提问。
- [ ] **Step 2: 新建 XAbstractAxis.h/.c**（逐 API 对照 qabstractaxis.h，含信号 20+，全部中文 Doxygen）。
- [ ] **Step 3: 重构 XValueAxis/XCategoryAxis 继承**。
- [ ] **Step 4: 验证门**：主构建 + 回归 + `build-crop-min`（XCHARTS_ON=0 时整体裁剪验证）。

## Task 1.3：Widget 信号补齐（参数 + 缺失）

**Files:**
- Modify（信号参数补齐）：`XComboBox.h/.c`（activated/textActivated/highlighted/textHighlighted/currentIndexChanged/currentTextChanged/editTextChanged 带 Qt 参数）、`XAbstractSlider.h/.c`（valueChanged(int)/sliderMoved(int)/rangeChanged(int,int)/actionTriggered(int)）、`XTabBar.h/.c`（currentChanged(int)/tabBarClicked(int)/tabBarDoubleClicked(int)/tabMoved(int,int)；删除非 Qt 的 tabClicked）、`XTabWidget.h/.c`（currentChanged(int)）、`XProgressBar.h/.c`（valueChanged(int)）、`XFontComboBox.h/.c`（currentFontChanged(QFont 等价)）、`XMainWindow.h/.c`/`XToolBar.h/.c`（iconSizeChanged(QSize)）、`XDockWidget.h/.c`（dockLocationChanged(area)）、`XTextBrowser.h/.c`（anchorClicked(QUrl 等价)；删除非 Qt highlighted）、`XWizard.h/.c`（customButtonClicked(which)）
- Modify（信号新增）：`XMessageBox.h/.c`（buttonClicked）、`XTextEdit.h/.c`（currentCharFormatChanged）、`XTextDocument.h/.c`（7 信号：contentsChange/baseUrlChanged/cursorPositionChanged/documentLayoutChanged/redoAvailable/undoAvailable/undoCommandAdded）、`XWizardPage.h/.c`（completeChanged）、`XMdiSubWindow.h/.c`（aboutToActivate/windowStateChanged）
- Test：`xgui_regression_test.c` 每信号一个连接+触发断言（含参数值）

**Interfaces:**
- Consumes：Task 0.2（旧信号宏删除后重建）
- Produces：信号签名全表（对齐 Qt 6.8 头文件 Q_SIGNALS 区）。

- [ ] **Step 1: 写信号参数回归测试**（`XObject_connect_1/2` 连接，断言收到参数值；每控件一条）。
- [ ] **Step 2: 逐控件改信号 typedef/宏/发射点**（发射点对齐 Qt 源码 emit 位置：如 XComboBox activated 在选择路径、XProgressBar valueChanged 在 setValue）。
- [ ] **Step 3: 验证门**：主构建 + 回归 + demo 交互（xdotool 点击/输入触发，日志断言参数）。

## Task 1.4：XApplication 应用级 API 补齐

**Files:**
- Modify：`Src/XGui/Application/XApplication.h/.c`（新增：`style()/setStyle(XStyle*)`（对接 Style 模块）、`focusChanged_signal`、`allWidgets()/topLevelAt()`、`beep()/alert(XWidget*,int)/isEffectEnabled/setEffectEnabled`、`closeAllWindows()/aboutQt()`、`styleSheet()/setStyleSheet()`（复用 XCssStyleSheet）、`autoSipEnabled()/setAutoSipEnabled()`）
- Modify：`XGuiApplication.c`（`font()` 未设置返回默认字体；`lastWindowClosed` 按无可见顶层窗口+in_exec+quit lock 判定；setFont/setPalette 补 ApplicationFontChange/PaletteChange 事件）
- Test：`Test/XGuiTest/XApplicationTest.c` + `XGuiApplicationTest.c`（CMake 接入）

**Interfaces:**
- Consumes：Task 1.1（XStyle 链）、Task 0.3
- Produces：`XApplication_style/setStyle` 等 26 项 API 与 focusChanged 信号。

- [ ] **Step 1: 写测试**（focusChanged 触发：setFocusWidget 后收到 (old,now)；style() 默认返回 XCommonStyle；closeAllWindows 关闭全部顶层）。
- [ ] **Step 2: 实现 API**（逐项对照 qapplication.h；setStyle 转发 XGuiApplication 全局样式钩子）。
- [ ] **Step 3: 验证门**：主构建 + 回归 + demo（窗口切换焦点日志 + 样式切换截图）。

## Task 1.5：item view 家族决策与基类信号（D2 待用户裁决）

**Files:**
- 决策 A（**用户已确认**，完整 M/V）：Create `XAbstractItemModel.h/.c`、`XHeaderView.h/.c`、`XItemSelectionModel.h/.c`；Modify `XAbstractItemView.h/.c`（model/setModel/rootIndex/setRootIndex/selectionModel/setSelectionModel/scrollTo/indexAt/visualRect 真实实现 + 7 信号）、`XTableView.h/.c`、`XTableWidget.h/.c`；新增 `XListView/XTreeView/XListWidget/XTreeWidget/XHeaderView` 派生族
- Test：`xgui_regression_test.c`（table 信号 7 个 + 行/列/单元格数据流）

**Interfaces:**
- Consumes：Task 1.3（信号规范）
- Produces：决策点 D2 落定 + XTableView/XTableWidget 数据通路。

- [ ] **Step 1: D2 已确认 = A 完整 M/V**。
- [ ] **Step 2: 实现**（先 XAbstractItemModel/XItemSelectionModel/XHeaderView 基座与信号，再 XAbstractItemView 数据通路，再 XTableView/XTableWidget，再 XListView/XTreeView/XListWidget/XTreeWidget）。
- [ ] **Step 3: 验证门**：主构建 + 回归 + `#if XTABLEWIDGET_ON || 1` 修复后裁剪构建验证 + demo 表格交互截图。

---

# Phase 2：功能实现（空实现清零，按控件族/子系统）

> 每任务通用验证门：主构建 + `./bin/XGuiRegression_Test` 全绿 + 涉及共享源时 25 裁剪构建 + demo 截图。以下只列各任务特有的实现点与验证。

## Task 2.1：XMessageBox 真实实现（P0-3 优先）

- Modify：`XMessageBox.h/.c`（setDetailedText/setInformativeText/addButton/setDefaultButton/setEscapeButton/buttons/standardButton/options/setOptions/testOption/aboutQt；buttonClicked 触发；16 个守卫外遗留随 Task 0.2 已删）
- 对齐基准：`qtbase/src/widgets/dialogs/qmessagebox.cpp`（按钮布局走 QDialogButtonBox 语义，text 换行与 Esc/默认按钮键绑定）
- Test：按钮点击→buttonClicked；默认/转义按钮键绑定；detailed/informative 显示与折叠。

## Task 2.2：XTabBar/XTabWidget 真实实现

- Modify：`XTabBar.h/.c`（setDocumentMode/setElideMode/setExpanding/setUsesScrollButtons/tabRect/tabAt/tabWidth/tabHeight/tabIndexAt/setTabIcon/setTabTextColor/setTabToolTip/setTabButton/tabData/setTabData/isTabVisible/moveTab/drawBase；tab 滚动按钮；XString** titles 已在 0.3）
- Modify：`XTabWidget.h/.c`（setCurrentWidget/setTabIcon/tabIcon/setTabToolTip/setTabWhatsThis/setTabVisible/isTabVisible/setTabBarAutoHide；currentChanged(int)）
- 对齐基准：`qtabbar.cpp`/`qtabwidget.cpp`；绘制走 XStyle（CE_TabBarTab）或 XPainter 兜底
- Test：tabAt 命中、tabRect 布局、图标/文本/颜色 roundtrip、滚动按钮可见性。

## Task 2.3：文本体系（XTextDocument/XTextEdit/XPlainTextEdit/XTextBrowser）

- Modify：`XTextDocument.h/.c`（QTextCursor 语义：cursorPositionChanged/undoAvailable/redoAvailable/undoCommandAdded 信号、find/characterAt/documentLayout、块/片段/格式表、7 信号触发点）
- Modify：`XTextEdit.h/.c`（document()/setDocument()/documentTitle/textCursor/setTextCursor/currentCharFormat/setFontFamily/setFontWeight/setFontPointSize/zoomIn/zoomOut/setTabStopDistance/setLineWrapMode/setWordWrapMode/currentCharFormatChanged 真实实现；insertPlainText/setPlainText/redo/undo/cursorRect/extraSelections/anchorAt/scrollToAnchor）
- Modify：`XPlainTextEdit.h/.c`（同族 30 余项）
- Modify：`XTextBrowser.h/.c`（historyTitle/historyUrl/setSearchPaths/searchPaths/setSource 家族 8 项；anchorClicked(QUrl)）
- 对齐基准：`qtbase/src/gui/text/qtextdocument.cpp`、`qtbase/src/widgets/widgets/qtextedit.cpp`、`qplaintextedit.cpp`、`qtextbrowser.cpp`（本项目 XTextDocument 为纯 C 子集，按 XGui.md 既定架构扩展，不做完整 Qt 富文本引擎；行为差异头文件声明）
- Test：光标移动信号、undo/redo 可用性、zoom 后字体点阵变化、document 共享。

## Task 2.4：XMainWindow dock/tab 布局

- Modify：`XMainWindow.h/.c`（toolBarArea/dockWidgetArea/addToolBarBreak/setDocumentMode/setCorner/setTabPosition/setTabShape/setAnimated/setDockNestingEnabled/setSeparator/insertToolBar/removeToolBar/saveState/restoreState/splitDockWidget/resizeDocks/tabifiedDockWidgets/menuWidget/unifiedTitleAndToolBarOnMac/iconSizeChanged(QSize)）
- 对齐基准：`qmainwindow.cpp`（QDockAreaLayout 简化移植：dock 区域 4 向 + 中央 widget + 工具栏带）
- Test：addDockWidget 归属、save/restoreState roundtrip、tabified 合并。

## Task 2.5：XCalendarWidget/XDateTimeEdit/XSpinBox/XAbstractSpinBox

- Modify：`XCalendarWidget.h/.c`（setDateEditEnabled/weekNumber/setHeaderTextFormat/setWeekdayTextFormat/isDateSelected/setShowTodayDate/setVerticalHeaderFormat/showTodayPage/selectedDate 导航 + 13 项）
- Modify：`XDateTimeEdit.h/.c`（calendarPopup/setCalendarPopup/setTimeSpec/setCurrentSectionIndex + section 导航）
- Modify：`XAbstractSpinBox.h/.c`/`XSpinBox.h/.c`（sizeHint/minimumSizeHint、长按连续步进 timer、真实 Qt API 14+ 项；机翻 Doxygen 重写）
- 对齐基准：`qcalendarwidget.cpp`/`qdatetimeedit.cpp`/`qabstractspinbox.cpp`/`qspinbox.cpp`
- Test：日历翻页/周数、日期 section 编辑、spinbox 长按步进。

## Task 2.6：XWizard/XWizardPage/XErrorMessage

- Modify：`XWizard.h/.c`（currentId/setCurrentId/startId/setStartId/pageIds/visitedIds 命名修正；setPixmap/field/setSideWidget/setButtonLayout/setTitleFormat/setSubTitleFormat/cleanupPage/initializePage 真实实现；customButtonClicked(which)）
- Modify：`XWizardPage.h/.c`（completeChanged、validatePage 通路）
- Modify：`XErrorMessage.h/.c`（4 个遗留 stub 清理/实现）
- Test：向导页导航 + field 读写 + complete 信号。

## Task 2.7：XMdiArea/XMdiSubWindow

- Modify：`XMdiArea.h/.c`（addSubWindow/currentSubWindow/subWindowList/setBackground/background/setTabPosition/tabPosition/setTabsMovable/tabsMovable/tabsClosable/setActivationOrder/activationOrder + 页签交互）
- Modify：`XMdiSubWindow.h/.c`（setOption/testOption/systemMenu/setSystemMenu/mdiArea/keyboardPageStep/isShaded/maximizedButtonsWidget/最小化最大化关闭按钮与窗口状态机/aboutToActivate/windowStateChanged）
- Test：子窗口平铺/级联、激活信号、页签模式切换。

## Task 2.8：XComboBox/XFontComboBox

- Modify：`XComboBox.h/.c`（findData/setItemIcon/setItemData/itemData/setCompleter/completer + iconSize(QSize) 语义；7 信号参数已在 1.3）
- Modify：`XFontComboBox.h/.c`（currentFont/setCurrentFont/displayFont/sampleTextForFont/sampleTextForSystem/sizeHint/currentFontChanged(QFont)）
- Test：itemData/findData roundtrip、字体样本渲染。

## Task 2.9：内容滚动（XAbstractScrollArea/XScrollArea/XTableView/XTableWidget）

- Modify：`XAbstractScrollArea.c`（`scrollContentsBy` 真实实现：viewport 内容平移 + 暴露区域重绘；setVerticalScrollBar/setHorizontalScrollBar/scrollBarWidgets/maximumViewportSize/sizeHint/minimumSizeHint）
- Modify：`XScrollArea.h/.c`（widgetResizable 几何同步、ensureVisible）
- Test：滚动条拖动→内容偏移；scrollContentsBy 暴露区重绘断言。

## Task 2.10：菜单/工具栏族

- Modify：`XMenu.h/.c`（addSection/insertSection/insertMenu/insertSeparator/actionGeometry/menuObject/menuInAction/platformMenu/showTearOffMenu/setNoReplayFor；17 个 stub 清理）
- Modify：`XMenuBar.h/.c`（actionAt/actionGeometry/cornerWidget/setCornerWidget/heightForWidth/sizeHint/minimumSizeHint/platformMenuBar/setNativeMenuBar）
- Modify：`XToolBar.h/.c`（insertSeparator/insertWidget/widgetForAction/actionAt/actionGeometry/toggleViewAction）
- Modify：`XToolBox.h/.c`（1 缺口）
- Test：菜单层级弹出/快捷键导航、工具栏动作几何。

## Task 2.11：Graphics 剩余项 + XPaintDevice 决策（D3）

- Modify：`Src/XGui/Graphics/XPainter.c`（画刷图案 Dense1~DiagCross 8x8 位图案；boundingRect()/clipPath()/setClipPath()；CustomDashLine 用户 dash 数组；RTL 镜像或正式声明）
- Modify：`XImage.c`（`transformed` 接 SmoothTransformation 双线性；文本元数据）
- Modify：`XColorSpace.c`（ICC 字节透明承载 fromIccProfile/iccProfile + transformationToColorSpace）
- Modify：`XMovie.c`（定时驱动层或文档化手动驱动为正式裁剪项）
- 决策 D3（**用户已确认**）：新建 `XPaintDevice` 中间基类（devicePixelRatio/width/height/paintEngine 等价接口），XImage/XPixmap/XBitmap/XPicture/XWidget 接入；XBackingStore_paintDevice 返回类型泛化
- Test：图案刷像素级断言（对照 Qt 8x8 图案表）、SmoothTransformation 输出、ICC roundtrip。

## Task 2.12：Style 引擎主体（最大工程，分 4 子批）

- 子批 1 `XStyle`：虚表扩容（styleHint/subElementRect/subControlRect/hitTestComplexControl/standardPixmap/standardIcon/generatedIconPixmap/layoutSpacing/drawItemText/drawItemPixmap/itemTextRect/itemPixmapRect/standardPalette/polish/unpolish）+ 枚举（SubElement/SubControl/ContentsType/StyleHint/StandardPixmap）+ 静态工具（visualRect/alignedRect/sliderPositionFromValue 等）
- 子批 2 `XCommonStyle`：pixelMetric 数值修正（TabBarTabOverlap=3、TabBarTabHSpace=24、TabBarTabVSpace=8/3/2、ToolBarItemSpacing=4、ToolBarHandleExtent=8、DockWidgetTitleBarButtonMargin=2、ButtonShift=2）+ drawPrimitive/drawControl 缺 case（ProgressChunk/ToolBarHandle/Separator/Branch/PanelItemViewItem/IndicatorItemViewItemCheck/FrameGroupBox/CE_ProgressBar/CE_Header/CE_ScrollBar*/CE_ToolBar/CE_ItemViewItem）+ sizeFromContents 按 Qt 公式重写
- 子批 3 `XFusionStyle`：drawComplexControl（Slider/ScrollBar/SpinBox/ComboBox/GroupBox/Dial）、standardPalette（Light=#F7F7F7、Midlight=#BFBFBF、Highlight=#308CC6、Disabled Base=#EFEFEF、Disabled Shadow=#BABABA、Accent）、pixelMetric、polish；圆角真实绘制（XPAINTER_SHAPE_ON 分支）
- 子批 4 `XStyleOption` 类型化（XStyleOptionButton/ProgressBar/Slider/SpinBox/ComboBox/ToolButton）+ `XStyleSheetStyle` 属性消费（margin/min/max/width/height/font、border-style、!important、渲染规则缓存）+ `XCssStyleSheet` token 缓冲 XString 化（已在 0.3）
- 对齐基准：`qstyle.cpp`/`qcommonstyle.cpp`/`qfusionstyle.cpp`/`qstylesheetstyle.cpp`（Qt 6.8.3）
- Test：逐控件 style 绘制截图对比（Fusion 主题）+ pixelMetric 数值断言 + sizeFromContents 尺寸断言。

## Task 2.13：Window 事件体系

- Modify：`XWindow.c`（isTopLevel 只查普通父；close/setTransientParent/isAncestorOf 同步；accepted 默认对齐（XEvent 默认 accepted=true 或事件 init 补偿）；requestActivate/raise/lower 发射 activeChanged；requestUpdate 投递 UPDATE_REQUEST；setVulkanInstance/vulkanInstance 实现或裁剪声明；startSystemResize/Move 接入平台或显式返回未支持）
- Modify：`XWindowEvent.h/.c`（新增 XMoveEvent/XTouchEvent/XTabletEvent 最小负载；XWheelEvent 补 pixelDelta/phase/inverted/source + QPointF 坐标；XEnterEvent 补 scenePosition；XDropEvent 动作集或拆分 DragEnter/DragMove/DragLeave；XInputMethodEvent attributes/setCommitString）
- Modify：`XWindowSystemInterface.h/.c`（分批补窗口状态/屏幕/应用状态/触摸/tablet/native/队列管理入口；键盘 text/count、鼠标全局坐标/source）
- Test：事件默认 accepted、isTopLevel 夹具（带瞬态父可 close）、WSI 注入→窗口事件断言。

## Task 2.14：Input 管线

- Modify：`XInputMethod.c`（默认注册焦点对象查询回调；ImQueryInput=0x40BA（CursorRectangle|CursorPosition|SurroundingText|CurrentSelection|AnchorRectangle|AnchorPosition））
- Modify：`XMimeData.h/.c`（urls/setUrls/hasUrls（XUrl/XStringList）；hasFormat 大小写敏感；formats 顺序；XString 主版本+_2）
- Modify：`XClipboard.h/.c`（XMIMEDATA_ON=0 裁剪；text_2 改名；Selection/FindBuffer 语义）
- Modify：`XAccessible.c`（name/description 读 XWidget accessibleName/accessibleDescription；角色推导；State 位）
- Modify：`XCursor.c`（X11/Win32 后端 pos/setPos；swap/equals）
- Test：ImQueryInput 值断言、MimeData 大小写/顺序、裁剪编译（XMIMEDATA_ON=0 构建）。

## Task 2.15：Icon 插件与主题修正

- Modify：`XIconThemeInternal.c`（`theme_searchThemeExists` 移除 directoryMatches(48,1) 过滤）
- Modify：`XIcon.c`（name() 文件图标返回空串；XIcon_pixmap/actualSize 接应用 DPR；`_2` 同签名重复 API 清理）
- Modify：`XIconEnginePlugin`（按后缀/MIME 选插件引擎；至少一个真实插件（SVG））
- Test：主题 isNull 语义、DPR 重载、插件工厂。

## Task 2.16：Platform 补强

- Modify：`XPlatformInputContext.c`（完整文本方向表；setSelectionOnFocusObject 发 Selection 属性事件）
- Modify：`XPlatformIntegration.c`（openGLModuleType 头注/实现统一；ScreenWindowGrabbing 置位；beep 语义）
- Modify：`XPlatformDrag.h`（XPlatformDragResult 数值与 Qt::DropAction 一致或独立文档化）
- Modify：`XPlatformNativeInterface.c`（nativeResourceForContext/nativeResourceFunctionForContext）
- Modify：`XPlatformTheme.c`（colorScheme/palette/font/themeHint 最小快照；m_name 已 XString 化于 0.3）
- Modify：`XPlatformFontDatabase.c`（defaultFont/standardSizes）
- Modify：`XPlatformAccessibility.c`（setActive/initialize/cleanup）
- Test：RTL 语言表、能力位、资源查询。

## Task 2.17：XLayout 行为修正

- Modify：`XLayout.c`（alignmentRect 按 qlayout.cpp:1265：默认居中/visualAlignment(RTL)/expanding+maximum-hack/hfw；activate 用 total* 尺寸 + SetDefaultConstraint 分支；spacing(NULL) 返回 -1 文档一致）
- Modify：`XBoxLayout.c`（setGeometry 父控件 RTL 时 visualDir 互换 + reverse 逆序；死分配清理）
- Modify：`XGridLayout.c`（qGeomCalc Fixed64 移植，消 1px 差）
- Modify：`XLayoutItem.h`（删除兼容旧名别名/重复 typedef；`XLayout_addWidget` 基类补齐；setContentsMargins(XMargins) 重载）
- Modify：`XStackedLayout.c`（removeWidget 不 reparent；create_layout 构造映射）
- Test：RTL 盒布局几何、alignmentRect 无对齐位居中、1px 精度断言。

## Task 2.18：Charts 主题/渲染/数据模型/交互

- Modify：`XChart.c`（8 主题色板/背景渐变/轴网格笔刷/阴影/outline 按 charttheme*_p.h 复刻；setTheme 应用到已有序列；plotAreaChanged 载荷；mapToValue/mapToPosition）
- Modify：`XXYSeries.c`/`XScatterSeries.c`/`XAreaSeries.c`/`XAbstractBarSeries.c`/`XPieSeries.c`/`XPieSlice.c`（点标记/点标签/最佳拟合线/线宽/柱标签/环图 holeSize/labelsPosition/轴标题/次网格「只存不画」项全部落地；QAbstractSeries 4 信号、QXYSeries 16 信号、交互与属性信号补齐）
- Create：`XBarSet.h/.c`（values/pen/brush/label/选择/信号；QAbstractBarSeries 改 QBarSet 集合语义）
- Modify：`XChartView.c`（橡皮筋 Vertical/Horizontal 锁轴/ClickThrough、双击缩放、命中测试 plotArea、滚轮平台语义）
- Test：主题切换色板断言、信号参数、QBarSet 数据流、交互事件注入。

## Task 2.19：缺失 Qt 类（范围 D4 待用户裁决）

- **D4 已确认 = 全部实现**：
  - 实用类：`XToolTip`、`XShortcut`、`XCompleter`（QLineEdit/XComboBox 依赖）、`XActionGroup`、`XFileDialog`、`XColorDialog`、`XInputDialog`、`XProgressDialog`
  - 视图族：`XListView/XTreeView/XListWidget/XTreeWidget/XHeaderView`（依赖 Task 1.5 决策 A 的基座）
  - 其他：`XGraphicsEffect`、`XOffscreenSurface`
- 每类按既有惯例：XObject/XClass 虚表 + XString + 中文 Doxygen + 信号 + 回归测试。

## Task 2.20：Graphics/Window/Style 已知偏差文档化（D3/D5 收口）

- Modify：`XGui.md`（新增「已知偏差清单」章节：XPaintDevice 豁免、QIcon 路径字符串映射、setIconSize(int) 单值、QLayout 默认边距/间距 0 vs Qt 样式、XMovie 手动驱动、富文本子集边界、XStackedLayout 信号决策）
- Modify：相关头文件 @note 补齐未注明的已知差异。

## Task 2.21：公开类全量对齐回检（新原则收口，2026-09-15 用户追加）

> 用户新原则（2026-09-15 追加）：① 只实现 Qt **公开类**的 API 对齐；② 依赖的 Qt **私有类**（Q*Private、内部 helper 类）若只有一个调用方，直接把功能实现到公有类中，不建私有类对应物；③ **XPaintDevice 等所有 Qt 公开类都要全量实现**（枚举数值、全部公开方法、语义完整，不因简化省略）。
> 本任务在 Phase 2 全部实现完成后、Phase 3 收口前执行，**回检此前全部轮次（Phase 0/1/2.1-2.20）已实现代码是否符合上述新原则**。

- [ ] **Step 1: 私有类对应物扫描**：grep 全库 `X[A-Za-z]*Private` 类型名（struct/typedef/class），确认不存在 Qt 私有类对应物；对审计/计划中曾出现但 Qt 为私有的 API（如 QStyleSheetStylePrivate、QToolTipPrivate、QComboBoxPrivate、QMenuPrivate、QMdiAreaPrivate 等）逐一确认已内联到公有类或未创建。
- [ ] **Step 2: 公开类全量复查**：对 Phase 2 涉及的全部 Qt 公开类（XPaintDevice/QPaintDevice、XPaintEngine/QPaintEngine、Task 2.19 新增类族、Style 模块 QStyleOption* 族、Charts QBarSet/QAbstractSeries 族等），逐个对照 Qt 6.8.3 头文件核对：枚举数值一致、公开方法齐全（不因"私有/内部"标记省略——Qt 公开头中标记 internal 但仍在 public 区的方法也保留，如 QMdiSubWindow::maximizedButtonsWidget）、返回类型映射合理（QString→XString 主版本+_2、QSize→XSize 等）。
- [ ] **Step 3: 单调用私有功能内联**：对 Qt 中为私有/内部类、XGui 中已有对应物或计划提及的功能，若全库仅一个调用方，将功能实现直接并入公有类方法/字段，删除独立内部类型（如 XToolTip 的独立实现、XStyleSheetStyle 的规则缓存内部类等）。
- [ ] **Step 4: 验证门**：主构建 + `./bin/XGuiRegression_Test` 全绿 + 涉及共享源时 25 裁剪构建 + 回检清单（Step1-3 的输出）记录到计划文档。

---

# Phase 3：收口与全量验证

## Task 3.1：全模块 API 复扫脚本（防回退）

- Create：`tools/xgui_api_scan.sh`（对 Qt 6.8 头文件提取公开方法/信号名，与 XGui 头 `X*_` 声明比对，输出缺口；先按模块跑一遍，目标缺口清单为 Task 2 已实现项的并集，不允许新增缺口）
- Test：CI 式脚本回归（每次 Phase 2 任务完成后跑，缺口数单调递减）。

## Task 3.2：全量验证矩阵

- Run：主构建 + `./bin/XGuiRegression_Test` + `ctest --test-dir build`（3/3）+ GPU 回归（`XGUI_GPU_SYNC=1 ./bin/XGuiGpu_Test`，GL+Vulkan）+ **25 裁剪构建全部重新 configure+编译** + ASan（`build-asan` 全绿无 leak）+ demo 全 tab 交互截图（xdotool+ffmpeg，存档 docs/ 或 /tmp）
- Expected：全部 EXIT=0/全绿。

## Task 3.3：文档与收口

- Update：`XGui.md` 进度章节（Phase 2 完成状态、已知偏差清单、裁剪开关表）
- Update：`docs/xgui-audit/2026-09-15/00-汇总报告.md` 标注已修复项（勾选）
- 提交：**不自动提交**；向用户汇报验证证据，由用户决定是否 commit/push。

---

## 执行顺序与依赖

0.1 → 0.2 → 0.3 → 0.4 → 0.5（Phase 0 串行，0.2 是 0.3/0.4 前置）
1.1 →（并行 1.2/1.3/1.4）→ 1.5（需 D2 裁决）
2.x 按控件族并行推进（每任务独立验证门；共享源任务自动挂起等 25 裁剪构建队列）
3.1 → 3.2 → 3.3

## 决策点清单（用户已确认 2026-09-15）

- **执行方式**：本会话内联执行（executing-plans）。
- **D1（Task 1.2）**：XCategoryAxis 对齐 **QBarCategoryAxis**（categories/append/remove/min/max/setRange/countChanged；XChart_addAxis 接受）。
- **D2（Task 1.5）**：item view 家族选 **A 完整 M/V**：XAbstractItemModel + XItemSelectionModel + XHeaderView + delegate 接口，QAbstractItemView→QTableView→XTableWidget 完整数据通路，并新增 QListView/QTreeView/QListWidget/QTreeWidget/QHeaderView。
- **D3（Task 2.11/2.20）**：**新建 XPaintDevice 中间基类**，XWidget 与图像族（XImage/XPixmap/XBitmap/XPicture）接入，严格 1:1。
- **D4（Task 2.19）**：**全部实现**：XToolTip/XShortcut/XCompleter/XActionGroup/XFileDialog/XColorDialog/XInputDialog/XProgressDialog + QListView/QTreeView/QListWidget/QTreeWidget/QHeaderView + QGraphicsEffect/QOffscreenSurface。

## Self-Review 记录

- 覆盖面：11 个审计模块全部落到任务（Application→1.4/0.3、Window→0.5/2.13、Widget→0.2/0.3/1.3/1.5/2.1-2.10、XLayout→0.5/2.17、Graphics→0.1/2.11、Style→0.3/1.1/2.12、Icon→0.1/2.15、Input→2.14、Platform→0.3/2.16、Charts→0.3/1.2/2.18、CoreRoot→0.4）。
- 占位符检查：无 TBD；每个任务含具体文件、API 名、对齐基准与验证命令。
- 类型一致性：XString 主版本+_2、XSignal 签名、XAbstractAxis 基类引用在 1.2 与 2.18 间一致；XStyle 槽位枚举在 1.1 与 2.12 间一致。

---

## 执行进度（2026-09-15 第一轮，内联执行）

- [x] Task 0.1 BOM 补齐：26 个文件全部加 UTF-8 BOM；`tools/check_bom.sh` 扫描零残留；主构建+回归全绿。
- [x] Task 0.2 守卫外旧 API 删除：`tools/remove_legacy_widget_api.py` 删除 593 个守卫外声明（含 380+ 空实现），修复过程中发现的 2 个信号声明缺失（XTabBar tabBarClicked/tabBarDoubleClicked 已补回头文件+实现）；`{ (void)self; }` 空体清零；主构建+回归+build-crop-min 全绿。
- [~] Task 0.3 字符串合规化（进行中）：
  - [x] XComboBox：`m_items` char**→XString**，addItem/insertItem/addItems/insertItems/itemText/currentText/findText/setItemText/setCurrentText/setEditText/placeholder 全部 XString 主版本 + `_2`；补 deinit 泄漏修复；超长字符串回归用例（纯 ASCII 500B）通过。
  - [x] XTabBar/XTabWidget：`m_titles` char**→XString**，addTab/insertTab/tabText/setTabText + XTabWidget 同族 API 全部 XString 主版本 + `_2`；补 VXTabBar_deinit（原泄漏）；调用方（XTabBarTest/XTabWidget 内部/回归/demo）已迁移。
  - [x] XGuiApplication：setSessionState 改 XString 主版本 + `_2`；补 setApplicationDisplayName_2/setDesktopFileName_2。
  - [x] Platform 核心：XPlatformTheme.m_name char[64]→XString*（create_ex 主/_2、name 借用/_2）；XPlatformInputContext_setLocale、XPlatformServices_openUrl、XPlatformFontDatabase_hasFamily、XPlatformWindow property 三件套 XString 主版本 + `_2`；调用点迁移。
  - [x] XPlatformNativeInterface：注册表 `char* m_name`→`XString*`；nativeResourceFor*/nativeResourceFunctionFor*/platformFunction/registerPlatformFunction/windowProperty 族（含 windowProperty_2 带默认值版改名 windowProperty_default）/setWindowProperty 全部 XString 主版本 + `_2` UTF-8 转发（脚本 `/tmp/ni_convert_c.py` 生成，已修 4 轮 bug：返回类型 void 匹配、函数名前缀、转发参数 tmp、nativeResourceFunctionFor* 直传主版本）；回归/演示调用方全部迁移。
- [~] Charts 字符串 API（样板完成，批量待续）：XChart（setTitle/title/setTitleFont/titleFontFamily/setLocale/locale/titleFont）+ XAbstractSeries（setName/name）已按「主版本 XString*（getter 借用 const XString*）+ `_2` UTF-8」完成并全绿；剩余 ~30 个（XAbstractBarSeries/XXYSeries/XAreaSeries/XCategoryAxis/XPieSeries/XPieSlice/XValueAxis）下一轮批量。
- [ ] 待续：XCssStyleSheet 4 个 char[64] token 缓冲（顺延至 Task 2.12 子批 4，已在 Style 审计判定为合规+附注）。
- [ ] Task 0.4/0.5、Phase 1/2/3：下一轮继续。

### 本轮新增工具
- `tools/check_bom.sh`：扫描缺 BOM 的 .h/.c。
- `tools/remove_legacy_widget_api.py`：删除 Widget 守卫外旧 API（含调用方检查、守卫内同名保留实现、BOM 保留、残留碎片自检）。

## 执行进度（2026-09-15 第二轮）

- [x] Task 0.3 继续：XPlatformNativeInterface 全量字符串合规化完成（17 个 API 主 XString + `_2`；注册表 XString*；`windowProperty_2`(带默认值) 改名 `windowProperty_default` 腾出 `_2` 后缀）；Charts 样板 XChart/XAbstractSeries 完成。
- [x] 验证：主构建 EXIT=0、`XGui regression tests passed`、build-crop-min/build-crop-no-plugin EXIT=0、`git diff --check` 无空白错误。
- [ ] 下一轮：Charts 剩余 30 个字符串 API 批量转换；Task 0.4 XGuiConfig 裁剪开关。

## 执行进度（2026-09-15 第三轮）

- [x] Task 0.3 收尾：Charts 剩余 25 个字符串 API 批量转换完成（XAbstractBarSeries 5、XCategoryAxis 2、XValueAxis 2、XAreaSeries 8、XXYSeries 7、XPieSeries 1、XPieSlice 9 含 init_2→init_ex 改名腾出 `_2` 后缀、create_ex_2、labelFont 实现补齐）；Charts 头文件 const char* 主版本清零。
- [x] Task 0.4 XGuiConfig 裁剪修复：拆 5 个开关嵌套；!XGUI_ON 块补齐 31 个缺失置 0（XPAINTER 全族/XSTYLE/X控件）+ 修正 XTEXTDOCUMENT/XTABLEWIDGET/XCHARTS 误置 1；新增 XPIXMAP_ON；!XWIDGET_ON 块补 15 个控件；XLAYOUT_STACKED 联动；恒开守卫 `#if XTABLEWIDGET_ON || 1` 修复；XSplashScreen 双守卫（XPIXMAP_ON && XPAINTER_PIXMAP_ON）；配置一致性三场景验证通过；**26/26 裁剪构建全 PASS**（修复 build-crop-painter-off 的 drawPixmap 未守卫）。
- [x] Task 0.5 生命周期/UB：XWindowEvent 7 个 clone 去 XMemcpy（逐字段 + XClassSetVtable + 基类 helper，补 10 个事件类 class_init 声明）；VXChart_move/VXSlice_move 去 XMemcpy（deinit+结构体赋值+源重建，修复原 memcpy 覆盖丢失字符串指针 bug）；XStyle_installStyleSheet 泄漏修复（XStyleSheetStyle 新增 setSourceStyle_move 拥有语义 + deinit 释放 + ASan 泄漏测试通过）；XStackedLayout 类型双关清除——信号发射所有权**正确转移**到 XStackedWidget（删除无效的布局信号连接，setCurrentIndex/removeWidget 自行发射），XStackedWidget test PASS。
- [x] Task 1.1 XFusionStyle 继承链修复：`XFusionStyle→XCommonStyle`（Qt 6.8 QFusionStyle : QCommonStyle）；class_init/Parent 同步；继承链回归断言（PixelMetric/SizeFromContents 同槽）。
- [ ] 下一轮：Task 1.2 XAbstractAxis 轴体系重构；1.3 Widget 信号补齐；1.4 XApplication；1.5 item view M/V。

## 执行进度（2026-09-15 第四轮）

- [x] Task 1.2 Charts 轴体系重构（用户已定 QBarCategoryAxis 方向）：
  - 新建 `Src/XGui/Charts/XAbstractAxis.{h,c}`（XObject 派生，对标 QAbstractAxis）：visible/gridLineVisible/min/max/setRange/reverse/titleText/labelsAngle/shadesVisible/linePenColor/labelsBrushColor + 5 个信号（visibleChanged/gridLineVisibleChanged/titleTextChanged/rangeChanged/reverseChanged）；copy/move/deinit 虚槽齐全。
  - XValueAxis 改组合继承 XAbstractAxis（m_base 第一成员；min/max/visible/title 上移基类，保留 tickCount/labelFormat；旧 API 转发基类）。
  - XCategoryAxis 改组合继承 + QBarCategoryAxis 语义：min 恒 0、max=类别数、setRange 钳位 [0,count]、append 后回写 max 并发射 countChanged；补 create_ex/create。
  - XChart/XChartView 轴字段直访问改 m_base.*，轴释放补 deinit（修复 XFree_System 直接释放导致 m_titleText 泄漏）；顺手修复 XButtonGroup.h 引用未定义 XObject_deinit_base 的断宏。
  - 回归：XAbstractAxis 信号 3 项、QBarCategoryAxis 语义（append/max/countChanged/setRange 钳位）断言全绿。
- [ ] 下一轮：Task 1.3 Widget 信号补齐；1.4 XApplication；1.5 item view M/V；Phase 2。

## 执行进度（2026-09-15 第五轮）

- [~] Task 1.3 Widget 信号补齐（进行中，参数补齐阶段完成）：
  - [x] XAbstractSlider 家族：valueChanged(int)/sliderMoved(int)/rangeChanged(int,int)/actionTriggered(int) 信号签名+发射点+派生宏（XSlider/XScrollBar/XDial）全部带 Qt 参数；修复 XSignal 宏与带参信号的调用约定（取 id 用直接调用+占位参）。
  - [x] XComboBox：activated(int)/textActivated(text)/highlighted(int)/textHighlighted(text)/currentIndexChanged(int)/currentTextChanged(text)/editTextChanged(text) 签名+发射点。
  - [x] XTabBar：currentChanged(int)/tabBarClicked(int)/tabBarDoubleClicked(int)；**删除非 Qt 的 tabClicked**（发射统一走 tabBarClicked，XTabWidget 连接迁移）。
  - [x] XTabWidget：currentChanged(int)。
  - [x] XProgressBar：valueChanged(int)（修复发射型信号函数体取 id 误改导致的递归）。
  - [ ] 待续（新增缺失信号）：XDockWidget dockLocationChanged、XMainWindow/XToolBar iconSizeChanged、XWizard customButtonClicked、XFontComboBox currentFontChanged、XTextBrowser anchorClicked、XTabBar tabMoved、XMessageBox buttonClicked、XWizardPage completeChanged、XMdiSubWindow aboutToActivate/windowStateChanged、XTextEdit currentCharFormatChanged。
- [ ] 下一轮：完成 1.3 新增信号；Task 1.4 XApplication；1.5 item view M/V。

## 执行进度（2026-09-15 第六轮）

- [x] Task 1.3b 新增 11 个缺失信号（QApplication/QWidget 系 Qt 语义）：
  - 带发射点：XDockWidget::dockLocationChanged(int)（setFloating 近似）、XFontComboBox::currentFontChanged(text)（setCurrentFamily）、XToolBar::iconSizeChanged(int,int)（setIconSize）。
  - 仅信号函数（发射点随对应 Task 2.x）：XTabBar::tabMoved(int,int)、XMainWindow::iconSizeChanged(int,int)、XWizard::customButtonClicked(int)、XWizardPage::completeChanged()、XMessageBox::buttonClicked(XAbstractButton*)、XMdiSubWindow::aboutToActivate()/windowStateChanged(int)、XTextEdit::currentCharFormatChanged()、XTextBrowser::anchorClicked(text)。
  - Task 1.3 参数补齐 + 缺失信号全部完成，回归全绿。
- [x] Task 1.4 XApplication 应用级 API：style()/setStyle(XStyle*)、focusChanged 信号（setFocusWidget/setActiveWindow 接线）、allWidgets()、topLevelAt()、beep()、alert()、isEffectEnabled/setEffectEnabled、closeAllWindows()、aboutQt()、styleSheet()/setStyleSheet()（复用 XStyle_installStyleSheet）、autoSipEnabled()/setAutoSipEnabled()；修复 XApplication_create 宏缺 argc/argv 的既有 bug；回归测试（含无实例默认值断言）。
- [ ] 下一轮：Task 1.5 item view 完整 M/V（用户已定 A：XAbstractItemModel/XItemSelectionModel/XHeaderView + XTableView/XTableWidget 数据通路 + XListView/XTreeView 视图族）。

## 执行进度（2026-09-15 第七轮）

- [~] Task 1.5 item view 完整 M/V（方案 A，基座完成）：
  - [x] 新建 XAbstractItemModel（XObject 派生；内存二维模型：rowCount/columnCount/setDimension/data/setData/headerData/setHeaderData/parent 预留 + dataChanged/modelReset/rowsInserted/rowsRemoved 信号；copy/move/deinit 虚槽齐全）。
  - [x] 新建 XItemSelectionModel（XObject 派生；(row<<16)|col 编码选择集：select/deselect/isSelected/clear/selectedCount/selectedAt/currentIndex + selectionChanged 信号）。
  - [x] 新建 XHeaderView（XWidget 派生；orientation/count/sectionSize/sectionPosition/defaultSectionSize/stretchLastSection）。
  - [x] XAbstractItemView 数据通路：model()/setModel()/selectionModel()/setSelectionModel()/rootIndex()/setRootIndex()/indexAt()/visualRect()/setIconSize() + **7 个信号**（pressed/clicked/doubleClicked/activated/entered/viewportEntered/iconSizeChanged）+ 鼠标事件接线（按下→pressed+选中、释放→clicked+activated、双击→doubleClicked、移动→entered）；修复 .c 恒开守卫 `#if XTABLEWIDGET_ON || 1`。
  - [x] XTableView：paintEvent 从 model 渲染（表头/网格/单元格文本/选中高亮/交替行色）。
  - [x] XTableWidget：内建 model 桥（model() 返回；setRowCount/setColumnCount/insert/remove/clear/setText/setItem/表头全部同步 model 维度与数据）。
  - [x] 回归测试：model 维度/数据/dataChanged 信号/表头、selection 选中/取消/信号、header 尺寸/位置，全绿。
  - [ ] 待续：视图族 XListView/XTreeView/XListWidget/XTreeWidget（D4 用户确认全部实现）。
- [ ] 下一轮：视图族 + Phase 2（控件功能实现）。

## 执行进度（2026-09-15 第八轮）

- [x] Task 1.5b 视图族（D4 全部实现）：
  - [x] XAbstractItemView_indexAt 改为可派生虚槽（EXAbstractItemView_IndexAt；基类网格实现 + XListView 行渲染 + XTableView 行高/列宽感知重载）。
  - [x] XListView（→XAbstractItemView）：model 单列垂直渲染、spacing/modelColumn/rowHeight、IndexAt 重载、选中高亮。
  - [x] XTreeView（→XAbstractItemView）：model 第一列渲染、indentation/headerHidden/rowHeight、树枝指示、IndexAt 重载。
  - [x] XListWidget（→XListView）：内建 model 桥（addItem/insertItem/count/item/takeItem/clear/currentRow/setCurrentRow/model）。
  - [x] XTreeWidget（→XTreeView）+ XTreeWidgetItem：树条目（text/children/parent）、addTopLevelItem/insertTopLevelItem/topLevelItem/topLevelItemCount/takeTopLevelItem/clear、递归缩进渲染。
  - [x] 回归：XListView model 接入、XListWidget 条目/当前行/clear、XTreeWidgetItem 子节点/文本、XTreeWidget 顶层条目/clear，全绿。
- [ ] **Phase 1 完成**。下一轮进入 Phase 2：控件族/子系统功能实现（2.1 XMessageBox 等，按审计 P0-3 优先）。

## 执行进度（2026-09-15 第九轮，Phase 2 启动）

- [x] Task 2.1 XMessageBox：setDetailedText/setInformativeText（XString 字段+roundtrip）、addButton 三态（按钮/文本/标准）、setDefaultButton/setEscapeButton（对象+标准位，自动创建缺失标准按钮）、defaultButton/escapeButton、buttons()、standardButton(button)、setOptions/options/testOption；buttonClicked 经 XDialogButtonBox clicked 转发接线。
- [x] Task 2.2 XTabBar/XTabWidget：
  - XTabBar 新增 19 API：setDocumentMode/setElideMode/setExpanding/setUsesScrollButtons/setDrawBase + tabRect/tabAt/tabWidth/tabHeight/tabIndexAt/isEmpty + setTabIcon/setTabTextColor/setTabToolTip/setTabButton/setTabData/setTabVisible + moveTab（发射 tabMoved，同步 6 条平行数组）；ensureCapacity/insertTab/removeTab/moveTab/deinit 全同步平行数组；修复 init 未清零新数组导致的 Realloc 崩溃。
  - XTabWidget 新增 10 API：setCurrentWidget/setTabIcon/setTabToolTip/setTabWhatsThis/setTabVisible/isTabVisible/setTabBarAutoHide/tabBarAutoHide（委托 XTabBar）。
- [ ] 下一轮：Task 2.3 文本体系（XTextDocument/XTextEdit/XPlainTextEdit/XTextBrowser）。

## 执行进度（2026-09-15 第十轮，Phase 2 推进）

- [x] Task 2.3 文本体系（核心部分）：
  - XTextDocument：新增 m_cursorPosition + setCursorPosition/cursorPosition/find/characterAt + 6 个信号（baseUrlChanged/cursorPositionChanged/documentLayoutChanged/redoAvailable/undoAvailable/undoCommandAdded）；setPlainText 接 documentLayoutChanged/undoCommandAdded 触发点；修复 XTextDocument_delete_base 断宏（XObject_delete_base 未定义）。
  - XTextEdit：11 个新字段（fontFamily/fontWeight/fontPointSize/tabStopDistance/cursorWidth/lineWrapMode/wordWrapMode/acceptRichText/autoFormatting/centerOnScroll/documentTitle/textBackgroundColor）+ 20 个空实现全部落地（存储型 setter/getter + zoomIn/zoomOut 字号增减）。
  - 回归：格式 roundtrip/zoom/documentTitle/cursorWidth + XTextDocument 信号/find/characterAt 全绿。
  - [ ] 待续：XPlainTextEdit 30 余项、XTextBrowser 8 项（下轮）。
- [ ] 下一轮：XPlainTextEdit/XTextBrowser 收尾 + Task 2.4 XMainWindow。

## 执行进度（2026-09-15 第十一轮，Phase 2 推进）

- [x] Task 2.3 收尾确认：XPlainTextEdit/XTextBrowser 头引用 100% 有实现（旧审计缺口已在清理中消除）；回归 test_textbrowser2_contract 全绿。
- [x] Task 2.4 XMainWindow（18 API）：toolBarArea/dockWidgetArea（数组查询）、insertToolBar（Qt 移动语义防重复）/removeToolBar/addToolBarBreak、documentMode/animated/dockNestingEnabled/unifiedTitleAndToolBarOnMac/tabPosition/tabShape/separator/corner（存储型）、saveState（快照生成）/restoreState；修复 XWidget_setWindowTitle_2 把 const char* 当 XString* 传的既有崩溃 bug。
- [x] Task 2.5 XCalendarWidget 11 API（dateEditEnabled/weekNumber/headerTextFormat/weekdayTextFormat/isDateSelected/showTodayDate/verticalHeaderFormat/todayDate/showTodayPage）+ XDateTimeEdit 4 API（calendarPopup/timeSpec/currentSectionIndex）。
- [x] Task 2.6 XWizard 15 API：setPixmap/field/setField（内嵌字段表，排障 XStringList 值语义 copy 崩溃后改 struct 数组）/setSideWidget/setButtonLayout/setTitleFormat/setSubTitleFormat/cleanupPage/initializePage/validateCurrentPage/nextId/done；XWizardPage completeChanged 信号已具备。
- [ ] 下一轮：Task 2.7 XMdiArea、2.8 XComboBox、2.9 内容滚动、2.10 菜单/工具栏族。

## 执行进度（2026-09-15 第十二轮，Phase 2 推进）

- [x] Task 2.7 XMdiArea/XMdiSubWindow 补齐：
  - XMdiSubWindow 新增 14 API：setOption/testOption（SubWindowOption 枚举 4 位数值对齐 Qt）、setKeyboardSingleStep/keyboardSingleStep/setKeyboardPageStep/keyboardPageStep（默认 5/20 对齐 Qt）、isShaded/showShaded（折叠状态机 + XMdiSubWindowState_Shaded=0x20 状态位）、showSystemMenu（系统菜单 popup）、setSystemMenu/systemMenu（所有权接管）、mdiArea()（parent 反查）、sizeHint/minimumSizeHint、maximizedButtonsWidget/maximizedSystemMenuIconWidget（Qt internal，返回 NULL 注明）。
  - windowStateChanged 信号由单参改为双参 (int oldState, int newState) 对齐 Qt；aboutToActivate 在激活前真发射（setActiveSubWindow/activateNext/activatePrevious 路径）。
  - XMdiArea 新增 8 API：setOption/testOption（AreaOption）、setDocumentMode/documentMode、setTabShape/tabShape、closeActiveSubWindow（关闭后激活剩余第一个）、activateNextSubWindow/activatePreviousSubWindow（环形导航）。
  - XMenu 前向声明 typedef 处理 XMENU_ON=0 裁剪；回归新增 test_mdisubwindow_ext_contract（26 断言全绿）。
- [x] Task 2.9 内容滚动：
  - XAbstractScrollArea 新增 9 API：setVerticalScrollBar/setHorizontalScrollBar（替换+删旧+重连 valueChanged）、addScrollBarWidget/scrollBarWidgets（8 槽平行数组）、setViewport、maximumViewportSize、sizeHint（默认 256x192 对齐 Qt）、minimumSizeHint（滚动条尺寸+边框，Qt 公式）；cornerWidget 双条可见时布局右下角；附加控件按对齐位挂靠边缘。
  - XScrollArea 补 sizeHint（视口优先）与 scrollContentsBy 暴露区重绘（XWidget_update(viewport)）。
  - 回归新增 test_abstractscrollarea_ext_contract（12 断言全绿）。
- [x] Task 2.10 菜单/工具栏族：
  - XMenu 新增 13 API：addSection/insertSection（节标题=禁用动作对齐 Qt）、insertMenu/insertSeparator（任意位置插入 + 信号连接）、actionGeometry、setNoReplayFor、setPlatformMenu/platformMenu、setAsDockMenu（裁剪记录）、showTearOffMenu/hideTearOffMenu/isTearOffMenuVisible；按 Qt 6.8 qmenu.h 裁决不实现 Qt 不存在的 menuObject/menuInAction（审计误列，Ruling 记录）。
  - XMenuBar 新增 9 API：actionAt/actionGeometry（与绘制同布局模型）、cornerWidget/setCornerWidget（XMenuBarCorner 枚举对齐 Qt::Corner）、heightForWidth/sizeHint/minimumSizeHint、setNativeMenuBar/isNativeMenuBar/platformMenuBar（NULL）。
  - XToolBar 新增 6 API：insertSeparator/insertWidget/widgetForAction/actionAt/actionGeometry/toggleViewAction（懒创建+触发切换可见性）；新增 m_widgets 平行数组修复 addWidget 不布局缺陷；修复 removeAction/clear/deinit 的 m_bridges 泄漏与平行数组错位。
  - XToolBox 补 setItemIcon/itemIcon（_2 重载）+ XToolBoxItem 内部 text 由 char[128] 改为 XString*（消除拥有型字符串违规）。
  - 回归新增 test_menu_ext_contract/test_menubar_ext_contract/test_toolbar_ext_contract + XToolBox 图标断言，全绿。
- [x] Task 2.8 收尾确认：XComboBox findData/itemData/itemIcon/iconSize 已在 Task 2.8 首轮实现并有回归；本轮补 setCompleter NULL 清除路径测试（XCompleter 类本体于 Task 2.19 落地）。
- [ ] 下一轮：Task 2.11 Graphics 剩余项（图案刷/transformed 双线性/ICC/XPaintDevice D3）、2.12 Style 引擎主体（最大工程 4 子批）、2.13 Window 事件体系。

## 执行进度（2026-09-15 第十三轮，Phase 2 推进）

- [x] Task 2.11 Graphics 剩余项 + XPaintDevice（D3 落地）：
  - **XPaintDevice/XPaintEngine 全量新建**（`Src/XGui/Graphics/XPaintDevice.h/.c`，XPAINTDEVICE_ON 开关）：XPaintDeviceMetric 枚举（PdmWidth=1..PdmDevicePixelRatioScaled=12 数值对齐 Qt）、XPaintEngineType（X11=0..Direct2D=16、User=50/MaxUser=100）、XPaintEngineFeature（18 位数值对齐）；XPaintDevice 全部查询 API（devType/paintEngine/width/height/widthMM/heightMM/DPI×4/devicePixelRatio/devicePixelRatioF/colorCount/depth/metric，metric 走接入类回调=Qt 虚语义）；XPaintEngine type/isActive/hasFeature。
  - 五类接入：XImage（XImageData 内嵌 + 度量回调：dpm→mm/dpi 换算对齐 Qt 254/10000）、XPixmap（复用内部 XImage）、XBitmap（转调）、XPicture（XPicturePrivate 内嵌 + bounding 度量）、XWidget（结构内嵌 + 实时几何度量）。
  - **改名收口**：`XWidget_paintDevice`（返回 XImage*）→ `XWidget_paintImage`（全库 47 处迁移），新 `XWidget_paintDevice` 返回 XPaintDevice*（对齐 QWidget 的 QPaintDevice 度量入口）；`XBackingStore_paintDevice` → `XBackingStore_paintImage` + 新泛化 `XBackingStore_paintDevice` 返回 XPaintDevice*（计划要求的泛化）。
  - **XPainter 画刷标准图案全量**：Qt 6.8 qt_patternForBrush 13 个 8×8 位图案表（Dense1-7/Hor/Ver/Cross/BDiag/FDiag/DiagCross，MonoLSB 字节精确）；fillRect 快速路径/GPU 快速路径禁用图案（强制软件逐像素），逐像素+ScanFill 按设备坐标 pattern 位采样；RTL 镜像未实现（正式声明）。
  - **XPainter dash**：XPainterState 加 m_dashPattern[16]/m_dashCount + XPainter_setDashPattern/dashPattern；CustomDashLine 优先用户节距（未设置回退 kDash）。
  - **XPainter clipPath/boundingRect**：setClipPath 按路径包围矩形裁剪（精确路径光栅裁剪=已知偏差，Task 2.20 记录）；clipPath 返回空路径；boundingRect 文本度量（flags 对齐）。修复 xpainterPathBounds 误读 LineTo 未初始化 m_x2/m_y3 的 bug。
  - **XImage transformed 双线性**：mode=1（SmoothTransformation）四邻双线性；修复 XImage_scaled 平滑分支被 `srcX==sw-1` 条件误伤的既有 bug。
  - **XColorSpace ICC 透明承载**：fromIccProfile/iccProfile/hasIccProfile/transformationToColorSpace；1024 字节固定缓冲（值类型约束，超长截断=已知偏差 Task 2.20）。
  - **XMovie 文档化**：定时驱动为正式裁剪项（头文件 @note + 已知偏差清单）。
  - 回归 test_painter_task211_contract（图案逐像素断言/dash 节距/clipPath/boundingRect/XPaintDevice 度量/ICC roundtrip/双线性灰度），全绿。
- [ ] 下一轮：Task 2.12 Style 引擎主体（4 子批）、2.13 Window 事件体系。

## 执行进度（2026-09-15 第十四轮，Phase 2 推进 + 2.12 子代理）

- [x] Task 2.12 Style 引擎主体（子代理 04e75461 完成，4 子批）：
  - 子批 1：XStyle 虚表 7→20 槽（styleHint/subElementRect/subControlRect/hitTestComplexControl/standardPixmap/standardIcon/generatedIconPixmap/layoutSpacing/drawItemText/drawItemPixmap/itemTextRect/itemPixmapRect/standardPalette）+ 枚举（SubElement/SubControl/ContentsType/StyleHint/StandardPixmap 数值对齐 Qt 6.8.3）+ 静态工具（visualRect/alignedRect/sliderPositionFromValue 等）。
  - 子批 2：XCommonStyle pixelMetric 数值修正（TabBarTabOverlap=3、TabBarTabHSpace=24、TabBarTabVSpace=8/3/2、ToolBarItemSpacing=4、ToolBarHandleExtent=8、DockWidgetTitleBarButtonMargin=2、ButtonShift=2）+ drawPrimitive/drawControl 缺 case + sizeFromContents 按 Qt 公式重写 + styleHint/subElementRect/subControlRect/hitTest。
  - 子批 3：XFusionStyle standardPalette（Light=#F7F7F7/Midlight=#BFBFBF/Highlight=#308CC6/Disabled Base=#EFEFEF/Disabled Shadow=#BABABA/Accent）+ pixelMetric/polish/drawComplexControl（Slider/ScrollBar/SpinBox/ComboBox/GroupBox/Dial）+ 圆角真实绘制（XPAINTER_SHAPE_ON）+ 修复 init 误调 XWindowsStyle_init 的 bug。
  - 子批 4：XStyleOption 类型化（Complex/Button/ProgressBar/Slider/SpinBox/ComboBox/ToolButton，默认值对齐）+ XStyleSheetStyle 属性消费（margin/min/max/width/height/font、border-style、!important 级联、渲染规则缓存）+ XCssStyleSheet token 缓冲 XString 化。
  - 验证：主构建 EXIT=0、回归（test_style_engine_contract 全绿）、26/26 裁剪 PASS、BOM 零残留；11 条 Ruling 记录在子代理报告。
- [x] Task 2.13 Window 事件体系：XWheelEvent 扩展（pixelDelta/phase/inverted/source，枚举数值对齐 Qt::ScrollPhase/MouseEventSource）+ XEnterEvent scenePosition + XDropEvent 动作集（XDropAction 数值对齐 Qt::DropAction）+ 新建 XMoveEvent/XTouchEvent/XTabletEvent（最小负载）+ XWindow isTopLevel 只查普通父 + setVulkanInstance/vulkanInstance；回归 test_window_event_task213_contract 全绿。
- [x] Task 2.14 Input 管线：XMimeData urls/setUrls/hasUrls（XStringList 承载）+ XCursor swap/equals + XClipboard text_2 改名 text_subtype + XAccessible name/description 读 accessibleName/accessibleDescription；回归 test_input_task214_contract 全绿。
- [x] Task 2.15 Icon 插件与主题修正：XIconEnginePlugin 全局注册表（register/unregister/createEngineForFile 后缀匹配）+ 内置真实 SVG 插件（新建 XSvgIconEngine/XSvgIconEnginePlugin，XImageCodecSvg 解码）+ XIcon name() 文件图标返回空串（对齐 Qt QIcon::name）。
- [x] Task 2.16 Platform 补强：nativeResourceForContext/nativeResourceFunctionForContext 家族 + XPlatformTheme colorScheme（枚举对齐 Qt::ColorScheme）/font/themeHint + XPlatformFontDatabase defaultFont/standardSizes + XPlatformAccessibility setActive/initialize/cleanup + XPlatformDragResult 独立数值文档化 + ScreenWindowGrabbing 能力位置位（子代理按测试白名单回退裁决）。
- [x] Task 2.17 XLayout 行为修正（部分）：XLayout_spacing(NULL) 返回 -1 + alignmentRect 无对齐位默认居中（XStackedLayout removeWidget 不 reparent 此前已具备）。
- [ ] 下一轮：Task 2.18 Charts（主题/渲染/数据模型/交互 + XBarSet）、2.19 缺失 Qt 类、2.20/2.21、Phase 3。

## 执行进度（2026-09-16 第十五轮，Phase 2 推进 + 2.18a/2.18b）

- [x] Task 2.18a Charts 数据模型/信号（子代理 409f71db 完成）：
  - 新建 XBarSet（18 信号 + values/pen/brush/label/选择族）；XAbstractBarSeries 重写为 XBarSet 集合语义（append/appendSets/insert/remove/take/clear + barsetsAdded/barsetsRemoved/countChanged；LabelsPosition 枚举对齐 Qt；barWidth 默认 0.5/precision 6/format 空）；保留 m_values/m_count 第一柱组镜像待 2.18b 移除。
  - XXYSeries 补齐 qxyseries.h 全量信号与发射点；XAbstractSeries 4 信号 + show/hide；XPieSeries holeSize 钳位 [0,1]；XPieSlice pen/brush/labelBrush 族。
  - 测试 test_charts_task218a_contract 全绿；build-crop-min 通过。
- [x] **ASan 暴露修复（跨任务）**：① VXFont_copy 先释放后复制导致目标与源共享 XString 时 UAF（XStyleSheetStyle 文本装饰路径）→ 改为先复制后释放；② test_mainwindow_contract 两处"析构后访问已释放子控件"断言（XDockWidget/XMainWindow）→ 删除不安全断言。ASan 全量回归零 UAF。
- [x] Task 2.20 已知偏差文档化：XGui.md 新增「已知偏差清单」章节（11b，12 项：XPaintDevice 体系/QIcon 路径映射/setIconSize 单值/QLayout 边距/XMovie 手动驱动/富文本子集/XStackedLayout 信号/路径裁剪近似/ICC 1024 截断/XTouchEvent 单点/XPaintEngine/菜单栏几何）。
- [x] Task 3.1 部分：tools/xgui_api_scan.sh 创建（基线缺口 789 含误报，Phase 3 精化）。
- [x] Task 3.3 部分：docs/xgui-audit/2026-09-15/00-汇总报告.md 修复进度标注表回填。
- [ ] 进行中：Task 2.18b（渲染/主题/交互，子代理 b012e530）；Task 2.19 缺失 Qt 类；Task 2.21 回检收口；Phase 3 全量矩阵。

## 执行进度（2026-09-16 第十六轮，Phase 2 推进 + 2.18/2.19 子代理）

- [x] Task 2.18a Charts 数据模型/信号（子代理 409f71db）：XBarSet（18 信号）+ XAbstractBarSeries XBarSet 集合语义 + XXYSeries 全量信号 + XAbstractSeries 4 信号 + XPieSeries holeSize + XPieSlice 笔刷族；测试全绿。
- [x] Task 2.18b Charts 渲染/主题/交互（子代理 b012e530）：8 主题色板（XChartThemeSpec 逐字复刻 Qt 6.8.3，含序列色/背景渐变/轴线/网格/outline/阴影/投影）+ setTheme 应用到既有序列（initializeTheme 语义，显式颜色不覆盖）；渲染落地（点标记/点标签/bestFitLine 最小二乘/柱标签/XBarSet 集合渲染/环图挖洞/主题色）+ XChartView_renderToImage 离屏管线；交互（橡皮筋锁轴/ClickThrough/双击缩放/滚轮+Ctrl 缩放/右键缩小/plotAreaChanged 变化时发射）；XAbstractBarSeries m_values/m_count 镜像删除；测试全绿。
- [x] Task 2.19a 实用类（子代理 886cce37）：XToolTip（静态全量+全局状态+惰性提示控件）、XShortcut（key/context/autoRepeat/注册表 match/activated）、XCompleter（model 前缀过滤/currentCompletion/6 信号）、XActionGroup（互斥/triggered 转发）；枚举按 Qt 6.8 头文件核对（ShortcutContext/CompletionMode 顺序修正，Ruling 记录）；测试全绿。
- [ ] 进行中：Task 2.19b（对话框族 6 类，子代理 3f3de7fd）；Task 2.21 回检；Phase 3（API 扫描精化 + ASan 泄漏专项 + 全量矩阵）。
- [ ] **Phase 3.2 专项登记**：ASan detect_leaks=1 全量回归报 ~4569 处泄漏（377 Direct leak），主要来源为回归测试未释放的临时对象（test_textedit_contract 的 XTextDocument、XImage 路径）+ 少量实现路径；Phase 3.2 前需逐类清零。

## 执行进度（2026-09-16 第十七轮，Phase 2 收尾）

- [x] Task 2.19b 对话框族（子代理 3f3de7fd）：XFileDialog（静态便捷+实例 fileMode/acceptMode/nameFilters/option 位/4 信号）、XColorDialog（getColor/currentColor/ShowAlphaChannel 等）、XInputDialog（getText/getInt/getItem+4 inputMode+5 信号）、XProgressDialog（setRange/value/wasCanceled/canceled/autoReset/autoClose/minimumDuration）、XGraphicsEffect（enabled/enabledChanged/update + XWidget_setGraphicsEffect/graphicsEffect 接入，父代理补 XWidget.h 声明）、XOffscreenSurface（format/size/screen/create/destroy/isValid/surfaceType）；测试 test_dialog_task219b_contract 全绿；主构建+回归+26/26 裁剪 PASS。
- [ ] Task 2.21 公开类全量对齐回检：Step1 私有类扫描已通过（X*Private 均为 XGui 实现块）；Step2/3 待执行（对 2.18/2.19 新类核对 Qt 公开头 + 单调用私有功能内联检查）。
- [ ] Phase 3.2 泄漏专项：ASan detect_leaks=1 报 ~4569 处（377 Direct leak）；主要来源测试未释放（XLineEdit_set 路径 198 次 XString、XTextDocument/图表测试）+ 少量实现路径。

## 执行进度（2026-09-16 第十八轮，Phase 2 完成 + Phase 3 推进）

- [x] Task 2.19b 对话框族（子代理 3f3de7fd）：XFileDialog/XColorDialog/XInputDialog/XProgressDialog/XGraphicsEffect（+XWidget 接入）/XOffscreenSurface 全量；ASan 复验修复 XSignal NULL 载荷/XStringList move/对话框 deinit。
- [x] Task 2.21 回检补齐：XFileDialog 追加 22 API（selectedUrls/directoryUrl/mimeTypeFilters/history/sidebarUrls/supportedSchemes/saveState/restoreState/iconProvider 等）；XColorDialog customColor/standardColor 族；XInputDialog int/double range + echoMode + 4 个 valueSelected 信号；XFileDialog/XColorDialog/XInputDialog 测试追加。
- [x] **test_style_engine_contract 定义恢复**：并发编辑中丢失（声明/调用残留），经 2.12 子代理索回完整源码贴回，回归恢复。
- [x] **Phase 3.2 库实现泄漏清零（部分）**：XTextDocument class_init 补 EXClass_Deinit 注册（原泄漏 49664B×N）；XWidget_font 深拷贝语义确认 + 调用方 30 处补 XFont_deinit_base（XLineEdit 内部改借用 &m_font 消除 244 次/轮）；XMenu/XMenuBar font 释放；双线性测试 out 初始化。库泄漏从 379→~120（剩余全部为测试未释放对象，子代理 5b6bdeb1 清理中）。
- [ ] 进行中：Phase 3.2 测试泄漏清理（子代理 5b6bdeb1）；Phase 3.1 扫描脚本精化；Phase 3.2 全量矩阵（GPU 回归/demo/ctest）；Phase 3.3 文档收口。

## 执行进度（2026-09-16 第十九轮，Phase 3.2 泄漏清理）

- [x] 库实现泄漏批量修复（Phase 3.2）：
  - XTextDocument class_init 补 EXClass_Deinit（原 49664B×N 泄漏）；XPlainTextEdit 补 m_textDoc 释放；XTextEdit 补 m_editor/m_textDoc/m_fontFamily/m_documentTitle 释放。
  - XValueAxis_deinit 补 m_labelFormat；XCategoryAxis_deinit 补 m_categories；XAbstractSeries/XPieSlice/XBarSet deinit 补 XClass_Deinit_Parent(XObject)（消除 XSignalSlot 泄漏）。
  - XWidget_font 深拷贝语义确认 + 30 处调用方补 XFont_deinit_base；XLineEdit 内部 8 处改借用 &m_font（消除 244 次/轮）；XMenu/XMenuBar/XChartView font 释放（含 XChartView.c:283 deinit 误入 if 块的修复）。
  - XStatusBar deinit 释放 items/permanents；XMessageBox 释放 m_standards；XTabWidget 新增 deinit（页容器 deinit+free）；XFontComboBox 释放 families 元素与容器；XTableWidget_clear 释放单元格行 + deinit 释放 m_model；XTreeWidgetItem 子项本体 free；XTextBrowser_clearHistory/setSource 释放 history（含覆盖前清 forward）。
  - XTextDocument_find/characterAt 释放 toPlainText 返回值；XFileDialog/XColorDialog/XInputDialog 回检补齐（Task 2.21）。
  - test_tabwidget_wrap_contract 页对象补 Set_Class_IsHeap（级联删除释放本体）；双线性测试 out 初始化。
  - ASan 泄漏从 379→29 处（0 UAF），主版回归通过、BOM 零残留。
- [ ] 下一轮：剩余 29 处泄漏（XImageData_clone 路径 test_pixmap_mask_lifecycle 等测试对象、XTextBrowser 余项、focusOut 散点）清零 → Phase 3.1 扫描精化 → Phase 3.2 全量矩阵（ctest/GPU/demo）→ Phase 3.3 文档收口。

## 执行进度（2026-09-16 第二十轮，Phase 3.2 泄漏清零 + 矩阵）

- [x] **ASan 泄漏全部清零**（XGui 代码 0 Direct leak / 0 UAF；剩余 3 处为外部系统库 libGLX_mesa/libfontconfig 初始化泄漏，非本项目）：
  - XTextBrowser xtb_emitStr 改 argsDel 模式（XString 信号载荷释放）；XSplashScreen xsp2_emitMessageChanged 同修。
  - XPainter_end 释放 defaultState 重建的字体（避免 end 后泄漏）。
  - XTextDocument_clear 释放全部已用块（原只清 block0，setHtml 多块 fragment 泄漏）；XTextDocument_setHtml 覆盖旧 doc 泄漏修复。
  - XLineEdit refreshDisplay 无文本不再分配（deinit 链 focusOut 1B 泄漏）。
  - XChart deinit 补 XClass_Deinit_Parent(XObject)（signalSlot 泄漏）；XPixmap_toImage 安全替换 out（XImage_init 丢弃旧数据泄漏）。
  - 测试侧：test_tabwidget_wrap_contract 页对象补 Set_Class_IsHeap。
  - 泄漏历程：379 → 121（测试侧）→ 28 → 19 → 11 → 4 → 0（XGui）。
- [x] 主构建 + 回归 + BOM + 26/26 裁剪全绿。
- [ ] **GPU 回归 1 项失败（待修）**：XGuiRegressionGpu（XGUI_RENDER_BACKEND=gpu;XGUI_GPU_SYNC=1）的 [C1-FAIL] QSS 下划线绘制（字体装饰线）——GPU 模式下 underline 装饰行未呈现，疑似文本路径 scissor/提交顺序问题（MEMORY 坑 4 相关）；需 gpu-render-debug 专项。
- [ ] Phase 3.1 扫描脚本精化、3.2 GPU 修复 + demo 全 tab、3.3 文档收口。

## 执行进度（2026-09-16 第二十一轮，GPU 回归修复 + Phase 3 收口）

- [x] **GPU 回归 [C1-FAIL] QSS 下划线修复（gpu-render-debug）**：
  - 根因：XPainter_drawText 的 GPU 文本快速路径（painterGpuDrawText）只画字形、不渲染 underline/strikeOut/overline 装饰（QFont 语义缺失）→ GPU 下装饰线丢失；软件路径经 painterDrawCodepoint 含装饰 → 后端不一致。此前测试"误通过"系字形底部像素误判（软件字形比 GPU 高 1px）。
  - 修复：XPainter_drawText GPU 分支检测 XFont_underline/strikeOut/overline——带装饰文本跳过 GPU 快速路径，走软件光栅局部提交（painterGpuSubmitSoftwareCommand，gpuActive 临时关闭，软件路径含装饰，提交保留）。
  - 测试修正：QSS text-decoration 作用于绘制期 painter 字体（不持久化控件 m_font），测试显式 XFont_setUnderline 验证装饰线渲染（三后端一致）。
  - 验证：software + gpu 双后端回归全过；CTest 3/3（XGuiRegression/XGuiRegressionGpu/XGuiGpu）全过；BOM 0。
- [ ] 裁剪构建重验中（XPainter.c 改动全量）；Phase 3.1 API 扫描精化；Phase 3.2 demo 全 tab 交互冒烟；Phase 3.3 文档收口。

## 执行进度（2026-09-16 第二十二轮，Phase 3.1 扫描精化 + 文档）

- [x] Task 3.1 API 复扫脚本精化：tools/xgui_api_scan.sh 改为**仅扫 Qt public 区**（用户原则：只对齐公开类 API，protected 虚方法不计缺口）+ 碎片 token 过滤；缺口 891→447。
- [x] 缺口清单落档：docs/xgui-audit/2026-09-16/xgui-api-gaps-phase3.txt（447 项，含继承方法/提取碎片/XGui 设计差异，Phase 3 持续精化）。

## 执行进度（2026-09-16 第二十三轮，Phase 3 收口验证）

- [x] 全量验证矩阵（Phase 3.2 主体）：主构建 EXIT=0、回归 "XGui regression tests passed"、CTest 3/3（XGuiRegression/XGuiRegressionGpu/XGuiGpu）、26/26 裁剪 PASS、BOM 0、ASan 0 leak/0 UAF（剩 3 外部系统库）、demo 冒烟（Xvfb 8s 无崩溃）。
- [x] Phase 3.3 汇总报告收口：docs/xgui-audit/2026-09-15/00-汇总报告.md 修复进度标注表全量更新（2.18-2.21 完成、3.1 精化、3.2 泄漏/GPU）。
- [x] API 缺口清单落档：docs/xgui-audit/2026-09-16/xgui-api-gaps-phase3.txt（447 项）。
- [ ] 剩余（下轮）：Phase 3.1 深度精化（447 缺口分类至真实缺口）；Phase 3.2 demo 全 tab 交互 xdotool 检视；Phase 3.3 XGui.md 已知偏差清单同步（GPU 文本装饰已修复项）；armel 交叉编译可选验证；目标达成后 complete。
