# XHeaderView 继承链改造可行性与成本调研（11b 评估）

> 日期：2026-09-17。性质：只读调研，唯一写入本文件；未修改任何源码，未做 git 操作。
> 目标：评估"XHeaderView 改为 XAbstractItemView 派生"的可行性与成本，产出方案供决策
> （对应审计报告 docs/xgui-audit/2026-09-17/inheritance-legacy-audit-2300.md 表 #27、
> XGui.md 14.77–14.79"下轮建议"与 11b 登记）。

---

## 0. 结论速览

**推荐：保持现状（XWidget 派生 + 登记 11b 偏差），不实施改派生。**

核心理由（详见 §3、§4）：

1. **全仓零集成**：XHeaderView 在 Src/ 内无任何使用方——XTableWidget.c、XTableView.c
   均未引用（各自内联处理表头），唯一构造点在 xgui_regression_test.c 一段测试。
   Qt 让 QHeaderView 继承 QAbstractItemView 的动机（模型驱动段、随视图框架渲染/交互）
   在 XGui 当前架构下不存在：表头文本由 XTableView 直接经 headerData 读取并绘制，
   XHeaderView 只是纯几何/状态表。
2. **基类能力收益为负**：scrollTo 基类实现是空操作桩；selectionModel 在 init 中
   强制堆分配但表头无选择语义；indexAt/visualRect 默认按 80×24 网格命中，与段模型
   无关；基类 4 个鼠标事件虚槽会把表头点击解释为"选中单元格"并发射 pressed/clicked/
   activated——改派生后必须**主动压制**这些继承行为（重载 4 个鼠标槽 + 重载 IndexAt），
   属"负复用"。
3. **改派生的唯一实质收益是审计形状对齐**（消除 83 项对照中唯一硬偏差），
   成本是引入视口/滚动条/选择模型死重 + 语义压制代码 + 裁剪依赖加深，
   且现有回归覆盖（约 10 条断言、无事件用例）不足以拦截行为回退。
4. 若后续启动"表头渲染集成进 XTableView"（视图族 P3 深化），可随该任务重评本结论
   （触发条件见 §4.3）。

---

## 1. 现状盘点

### 1.1 继承链

- XGui：`XHeaderView → XWidget`（XHeaderView.h:25 `XCLASS_DEFINE_EXTEND_END(XHeaderView, XWidget)`）。
- Qt 6.8.3：`QHeaderView → QAbstractItemView → QAbstractScrollArea → QFrame → QWidget`。
- 文件守卫：`#if XWIDGET_ON && XTABLEWIDGET_ON`（借用 XTABLEWIDGET_ON，无专属
  XHEADERVIEW_ON 开关——审计报告已另记一条）。

### 1.2 数据结构与 API 面（XHeaderView.h/.c 全文核对）

结构体字段（XHeaderView.h:28-44）：`m_orientation`（0=水平/1=垂直）、`m_count`、
`m_defaultSize`（默认 30）、`m_sections`（XVector(int) 尺寸表，对象拥有）、
`m_stretchLast`、`m_sectionMovedFrom`（预留，仅写不读）、`m_hidden`（bool* 平行隐藏表）、
`m_hiddenCount`、`m_sectionsClickable`、`m_sectionsMovable`、`m_sortIndicatorShown/
Section/Order`。

公共 API 共 **39 个函数**（另含 create 宏与 deinit_base/delete_base 宏），分组：

| 分组 | API |
|---|---|
| 生命周期 | init(parent,flags,orientation)、create_ex(memory,parent,flags,orientation) |
| 几何 | orientation、count、setCount、sectionSize、setSectionSize、defaultSectionSize、setDefaultSectionSize、sectionPosition、setStretchLastSection、isStretchLastSection |
| 段管理 | hideSection、showSection、isSectionHidden、hiddenSectionCount、setSectionsClickable、sectionsClickable、setSectionsMovable、sectionsMovable、moveSection、swapSections、setSortIndicator、sortIndicatorSection、sortIndicatorOrder、setSortIndicatorShown、isSortIndicatorShown |
| 视觉序/反查 | visualIndex、visualIndexAt、logicalIndexAt、sectionSizeHint、length、offset |
| 信号 | sectionClicked_signal、sortIndicatorChanged_signal |

### 1.3 实现要点与自认限制（头文件 @note 与实现核对）

- **无渲染、无事件**：XHeaderView.c 没有任何 PaintEvent/鼠标事件处理；头文件 @details
  明确"渲染由 XTableView 统一完成（本类只负责几何）"。因此它从不是交互控件。
- **移动即重排**：moveSection/swapSections 直接物理重排 m_sections 与 m_hidden
  （XHeaderView.c:237-287），故 visualIndex/visualIndexAt 恒等映射（头文件 @note
  自认，"若后续引入独立视觉映射表则替换"）。
- **隐藏段仍占位**：logicalIndexAt/sectionPosition 逐段累加尺寸、不跳过隐藏段
  （与 length() 跳过隐藏段自相矛盾，见 §5）；Qt 中隐藏段按 0 计。
- **sectionSizeHint 无内容感知**：恒返回 defaultSectionSize。
- **offset 恒 0**：无滚动偏移承载，刻意不提供 setOffset（避免死存储）。
- **sectionClicked 从不发射**：信号句柄存在，但无事件路径，且 sectionsClickable/
  sectionsMovable 目前是纯状态位，无交互实现。
- **setSortIndicator**：写状态 + 置 Shown=true + 发射 sortIndicatorChanged（.c:289-301）。

---

## 2. XAbstractItemView 继承链与能力

### 2.1 链条（已追到底）

```
XAbstractItemView → XAbstractScrollArea → XFrame → XWidget → XObject
```

证据：XAbstractItemView.h:64 `XAbstractScrollArea m_base`；XAbstractScrollArea.h:59
`XFrame m_base`；XFrame.h:101 `XCLASS_DEFINE_EXTEND_END(XFrame, XWidget)`；
XWidget.h:439 虚表继承自 XObject。

### 2.2 init 签名与构造开销

签名：`void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent, XWidgetFlags flags)`
（XAbstractItemView.h:90）。其初始化链（XAbstractItemView.c:41-64 →
XAbstractScrollArea.c:287-313）实际动作：

1. `XAbstractScrollArea_init`：创建 **viewport 子控件**、**垂直/水平两条 XScrollBar**、
   连接两根滚动条 valueChanged→scrollContentsBy 信号槽、`XWidget_resize(self,200,150)`、
   setSizeHint(256×192)；
2. **懒创建实为立即创建** `m_selectionModel = XItemSelectionModel_create()`（堆分配）；
3. 置默认：ExtendedSelection / SelectItems / 编辑触发组合 / autoScroll=true / 图标 16×16 等。

即改派生后，每个 XHeaderView 构造将多出 **3 个子控件分配 + 1 个选择模型分配 + 2 组
信号连接 + 一次强制 resize**，全部为表头用不到的死重（表头不渲染、不滚动、无选择）。

### 2.3 已有能力清单（与表头相关性标注）

| 能力 | 实现 | 表头相关性 |
|---|---|---|
| setModel/model | 借用指针；仅夹取 current 到模型行列并 update | **无关**（表头段尺寸是本地状态，不消费模型数据） |
| selectionModel/setSelectionModel | init 即建，替换接管所有权 | **无关**（表头无选择语义） |
| scrollTo(row,col) | **空操作桩**（.c:129-135，注释"派生视图可提升"） | **无收益** |
| setCurrentIndex/currentRow/Column | 纯存储 | 无关 |
| indexAt（虚）/visualRect | 默认 80×24 网格硬编码（.c:190-229） | **语义错位**（段≠网格单元） |
| 4 个鼠标事件虚槽 | indexAt 命中→设当前项+选择+发 pressed/clicked/activated/entered | **有害**（需压制，见 §3.3） |
| selectionMode/Behavior、editTriggers、alternatingRowColors、autoScroll、rootIndex、iconSize | 状态存储 | 无关 |
| pressed/clicked/doubleClicked/activated/entered/viewportEntered/iconSizeChanged 信号 | 事件驱动发射 | 理论上可桥接 sectionClicked，但前提是表头能收到事件（当前收不到） |

### 2.4 既有派生范式（改派生可参照的样板）

XTableView/XListView/XTreeView 直接派生；XTableWidget→XTableView、XListWidget→XListView、
XTreeWidget→XTreeView。范式（XTableView.c:76-135）：

- 头文件：`XCLASS_DEFINE_EXTEND_END(XHeaderView, XAbstractItemView)`；首成员换
  `XAbstractItemView m_base`；`#include "XAbstractItemView.h"`；
- class_init：`XVTABLE_INHERIT_XCLASS(XAbstractItemView)`，按需重载
  EXWidget_MousePressEvent/Release/DoubleClick/Move、EXAbstractItemView_IndexAt；
- init：`XAbstractItemView_init(&self->m_base, parent, flags)` 后 `XClassSetVtable(self, XHeaderView)`；
- deinit：`XClass_Deinit_Parent(XAbstractItemView, (XAbstractItemView*)self)`；
- copy/move：`XClass_Parent(XAbstractItemView, EXClass_Copy/Move, ...)`。

---

## 3. 改造影响面

### 3.1 构造签名差异

`XHeaderView_init(parent, flags, orientation)` 比 XAbstractItemView 需要的
`(parent, flags)` **多一个 orientation 尾参**——不构成冲突：init 内部改为先调
`XAbstractItemView_init` 再写 orientation 即可，公共签名可保持不变（回归测试
`XHeaderView_create(NULL, 0, 0)` 无需改动）。真正的差异在**构造副作用**（§2.2 的
死重与强制 resize），不在签名。

### 3.2 使用方盘点（grep 全仓证据）

| 使用方 | 结论 |
|---|---|
| XTableWidget.c/.h | **零调用**。grep "header" 仅命中自有实现：`m_hHeaders/m_vHeaders`（XString* 标签数组，XTableWidget.h:61-63）、常量 `XTW_HEADER_H 24`/`XTW_HEADER_W 40`（.c:17-18）、标签转发到模型 headerData（.c:443）。表头几何由 XTableWidget 自绘自理 |
| XTableView.c | **零调用**。表头用常量 `XTV_HEADER_H 20`（.c:24），绘制时直接 `XAbstractItemModel_headerData_2(model, col, 0)` 取文本（.c:542） |
| XTreeView.c | 仅注释提及"参照 XHeaderView m_hidden 模式"（.c:65），非代码依赖 |
| xgui_regression_test.c | **唯一构造点**：.c:129 包含头，28532-28568 一段测试（create→setCount→hide/show/clickable/movable/swap/move/sortIndicator→delete_base，10 条断言） |
| Test/XGuiTest、xgui_window_demo.c 等其他 demo/Test | 零命中 |
| CMakeLists.txt | 递归 GLOB（CMakeLists.txt:61），改基类不涉构建脚本 |

**推论**：改派生的源码兼容性风险面≈0（公共 API 不变、唯一调用方是测试）；但也正因
零集成，改派生**不能带来任何现实调用路径上的能力提升**——收益只能来自未来集成，
而未来集成尚未决定走"XHeaderView 实体化"路线（XTableView 现在自己画表头）。

### 3.3 段模型 vs 行列模型：39 个段 API 的语义兼容性

- **段≈列（行数恒 1）**这一映射本身可成立：水平头 = 1×N 网格，垂直头 = N×1。
- 但 XAbstractItemView 的行列模型以 **XAbstractItemModel 的 rows/cols 数据单元**
  为中心（indexAt/visualRect/selection/current/编辑触发全部围绕单元格）；XHeaderView
  的段是**纯几何条目**（尺寸+隐藏位），无数据单元、无委托、无编辑。
- 逐 API 影响：
  - 36 个纯状态/几何 API（尺寸、隐藏、移动、排序指示器等）**不受基类影响**，照常工作；
  - indexAt/visualRect 需重载为"1×N 段条"语义，否则默认 80×24 网格命中是错的；
  - 4 个鼠标事件槽必须重载压制（基类会把手势变成"选中单元格 + pressed/clicked/
    activated"），压制后还要自行实现 sectionClicked 语义——等于继承来的交互逻辑
    **一行用不上、还得写反向代码**；
  - scrollTo/selectionModel/model/rootIndex/iconSize 等成为永久死字段/死接口。
- 结论：**语义兼容但不兼容"复用"**——39 个 API 与基类的交集复用度为零，
  负复用（需压制）面为 5 处。

### 3.4 收益逐项评估

| 预期收益 | 实际 |
|---|---|
| 获得基类 scrollTo | **无**。基类 scrollTo 是空操作桩，连 XTableView 都自行实现滚动；表头当前 offset 恒 0、无滚动设计 |
| 获得 selectionModel | **负收益**。init 强制堆分配，表头无选择语义，永不读取；copy/move 还需随基类深拷贝它 |
| 模型联动（Qt 的核心动机） | **不成立**。Qt 中 QHeaderView 继承视图是为了随 QAbstractItemModel 的 headerData/布局信号驱动段；XGui 设计上把文本留给 XTableView 直读 headerData，XHeaderView 明确"只管几何"（头文件 @details）。setModel 对表头是无意义的死字段 |
| clicked→sectionClicked 桥接 | **理论可行、现实不可用**：前提是表头作为交互控件接收鼠标事件，而它不渲染、不在控件树内交互，XTableView/XTableWidget 自绘表头并各自处理（现状连 XTableWidget 的表头点击都没走 XHeaderView） |
| 审计形状对齐 | **真实但唯一**的收益：消除 83 项对照中唯一硬偏差，类图与 Qt 同构，未来若做"表头实体化+视觉映射表"时结构更顺 |

**净判断：收益为负或近似为零。**

---

## 4. 方案对比与推荐

### 4.1 方案 A：保持 XWidget 派生 + 登记 11b（推荐）

- 动作：在 11b 登记中补充本调研差异清单（§5），并把"无专属 XHEADERVIEW_ON 开关"
  一并注明；文档（XGui.md §11 偏差节）已登记的表述可引用本文件。
- 成本：0 行源码改动；回归零风险。
- 代价：类图与 Qt 存在一处硬偏差（已被 11b 覆盖）。

### 4.2 方案 B：改派生 XAbstractItemView（不推荐，留作 P3 集成时重评）

改动清单（机械部分约 0.5 人天，语义压制部分另计 0.5–1 人天）：

1. XHeaderView.h：首成员 `XWidget m_base` → `XAbstractItemView m_base`；
   `XCLASS_DEFINE_EXTEND_END(XHeaderView, XAbstractItemView)`；include 换
   XAbstractItemView.h；守卫可维持 XWIDGET_ON && XTABLEWIDGET_ON（XTableWidget
   经 XTableView 已隐含依赖 XAbstractScrollArea，改派生不新增现实裁剪约束）。
2. XHeaderView.c：
   - class_init：`XVTABLE_INHERIT_XCLASS(XAbstractItemView)`；**必须**重载
     EXWidget_MousePressEvent/Release/DoubleClick/Move（压制基类选中行为，
     可选实现 sectionClicked 发射）与 EXAbstractItemView_IndexAt（1×N 段条命中）；
   - init：改调 `XAbstractItemView_init`（注意其内部强制 resize(200,150) 与
     sizeHint——对无渲染表头是错误尺寸语义，需评估是否还原尺寸）；
   - deinit/copy/move：父类链改挂 XAbstractItemView（copy/move 将随基类深拷贝
     viewport/滚动条/选择模型，开销与正确性都需新增断言）。
3. 回归测试：现 10 条断言应全数兼容；建议补 copy/move 深拷贝与"基类事件不泄漏
   pressed/clicked"断言。

风险分级：

| 风险 | 级别 | 说明 |
|---|---|---|
| 基类鼠标事件语义泄漏（表头点击被解释为选单元格） | 高 | 必须重载 4 槽压制；现有回归无事件用例，回退不易被发现 |
| 构造副作用（viewport/双滚动条/选择模型死重、强制 resize） | 中 | 资源浪费 + 尺寸语义变化；无渲染路径可立即暴露 |
| copy/move 深拷贝链变长引入回归 | 中 | 基类 copy 需复制选择模型等，新增隐藏耦合 |
| 公共 API 破坏 | 低 | 签名可保持不变，唯一调用方是回归测试 |
| 构建破坏 | 低 | CMake 递归 GLOB，无脚本改动 |

### 4.3 触发重评条件（任一满足时再议方案 B）

1. XTableView/XTableWidget 启动"表头实体化"集成（XHeaderView 成为真实子控件、
   承担渲染与交互，替代 XTV_HEADER_H/XTW_HEADER_H 两套内联表头）；
2. 引入独立"逻辑序↔视觉序映射表"（替换现"移动即重排"，届时 moveSection 语义
   对齐 Qt，段模型与视图数据不再同序移动）；
3. 需要表头消费模型 headerData 变更信号（Qt 式模型驱动段）。

---

## 5. 表头与 Qt 行为差异清单（供 11b 引用）

除继承链本身（QHeaderView→QAbstractItemView vs XHeaderView→XWidget）外，
XHeaderView 与 Qt 6.8 QHeaderView 的行为差异：

1. **visualIndex / visualIndexAt 恒等映射**：Qt 维护独立的逻辑↔视觉映射，
   moveSection 只改视觉序、逻辑索引与模型数据不动；XGui"移动即重排"，段尺寸/隐藏
   表被物理重排，逻辑序==视觉序恒成立（XHeaderView.h:160-182 @note）。后果：
   未来接模型后"移动表头段"不会带动数据列移动，与 Qt 视觉重排语义相反。
2. **隐藏段的占位语义不一致**：`length()` 跳过隐藏段（同 Qt 隐藏段按 0 计），
   但 `logicalIndexAt()`/`sectionPosition()` 不跳过——隐藏段仍占位累加，
   同一对象内两套坐标基准；Qt 中隐藏段在所有几何查询中均为 0 宽跳过。
3. **offset 恒 0**：Qt 有 offset/setOffset/setOffsetToLastSection 及滚动跟随；
   XGui 无偏移承载，刻意不设写入接口（XHeaderView.h:216-225 @note）。
4. **sectionSizeHint 无内容感知**：Qt 经委托/模型数据按内容测算；
   XGui 恒返回 defaultSectionSize（XHeaderView.h:196-205 @note）。
5. **sectionClicked 信号不发射**：Qt 在 sectionsClickable 时点击段发射
   sectionClicked/sectionPressed 且支持拖动移动段（sectionsMovable 实交互）；
   XGui 两标志为纯状态位，无任何事件路径，sectionClicked 句柄从不发射。
6. **sortIndicator 无渲染**：Qt 由 style 绘制指示器并触发视口排序联动；
   XGui 仅存状态 + 发射 sortIndicatorChanged，绘制责任留给 XTableView（未接）。
7. **无 resizeMode 体系**：Qt 的 Interactive/Stretch/Fixed/ResizeToContents、
   stretchSectionCount、minimum/maximumSectionSize、cascadingSectionResizes、
   defaultAlignment、firstSection/lastSection 等整族缺失（按子集设计裁掉，
   非 bug，列出供完整性）。
8. **非控件化**：Qt QHeaderView 是真实控件（含视口/事件/调色板）；
   XHeaderView 不渲染不收事件，表头几何由使用方各自硬编码
   （XTableWidget XTW_HEADER_H=24 vs XTableView XTV_HEADER_H=20，两处表头高
   互不一致，也是一处内部差异）。
9. **默认值差异**：XGui sectionsClickable/sectionsMovable 默认 false、
   defaultSectionSize=30；Qt 中视图派生类通常默认开启段点击（且各视图默认段宽
   由样式/内容决定）。差异属默认策略层面。

---

## 6. 证据索引（文件:行）

- Src/XGui/Widget/XHeaderView.h:24-25（继承宏）、28-44（字段）、56-78（init/create）、
  158-182（visualIndex 族 @note）、216-225（offset @note）、246-256（length @note）
- Src/XGui/Widget/XHeaderView.c:118-149（init/create_ex）、237-287（swap/move 物理重排）、
  362-425（视觉序恒等/logicalIndexAt/hint/length/offset）、427-438（两信号句柄）
- Src/XGui/Widget/XAbstractItemView.h:52-54（虚表）、62-78（字段）、90-105（init/create）
- Src/XGui/Widget/XAbstractItemView.c:23-39（class_init 重载面）、41-64（init：
  滚动区 init + 选择模型立即创建）、129-135（scrollTo 空桩）、142-157（setModel）、
  190-229（默认 indexAt/visualRect 80×24）、280-371（基类鼠标事件→选中+信号）
- Src/XGui/Widget/XAbstractScrollArea.h:50-75（类定义/字段）、XAbstractScrollArea.c:287-313
  （init：viewport+双滚动条+连接+resize(200,150)+sizeHint）
- Src/XGui/Widget/XFrame.h:100-101（XFrame→XWidget）；Src/XGui/Widget/XWidget.h:438-439
  （XWidget→XObject）
- 使用方：Src/XGui/Widget/XTableWidget.c:17-18、61-67、431-446、220-221（自有表头）；
  Src/XGui/Widget/XTableView.c:24、533-557（自绘表头）；Src/XGui/Widget/XTreeView.c:65（注释）；
  xgui_regression_test.c:129、28532-28568（唯一构造点）；CMakeLists.txt:61（GLOB）
- 审计与登记：docs/xgui-audit/2026-09-17/inheritance-legacy-audit-2300.md:82（表 #27）、
  153-158、288-292（待复核第 2 条）；XGui.md:323-329（偏差节 11b 表述）、
  812-821（14.76 段管理第一批）、841-843、877、897（11b 登记）
