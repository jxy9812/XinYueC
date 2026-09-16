# XGui XLayout 模块 Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计模块：`XLayout`（`Src/XGui/XLayout/`，6 个公共头 + 4 个 .c + 1 个内部头 + 1 个保护头；跳过 `*_Protected.h` / `*_Internal.h` 的正式审计，仅作参考）
- 审计方式：只读审计；未修改 `Src/`、`Test/` 任何源码，未 commit/push（`git status` 仅新增 `docs/xgui-audit/`）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/{qlayout.h,qlayoutitem.h,qboxlayout.h,qgridlayout.h,qstackedlayout.h}`，行为对照 `qlayout.cpp / qboxlayout.cpp / qstackedlayout.cpp / qlayoutitem.cpp`
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`（3.2/4/5/6/10.117 章节）
- 统计口径：API 缺口 = Qt 6.8 公开方法名 vs X 头文件函数/宏归一比对（剔除构造/析构、Qt 保护成员与宏名；C 无法表达默认参数的重载单独计 1 类缺口）；功能缺口 = 与 Qt .cpp 行为不一致或未实现的算法/语义点；对齐度为人工综合评分。

## 结论摘要

XLayout 是 XGui 中完成度最高的模块之一：七个类全部有真实实现，**无空函数体占位**，几何分配（qGeomCalc/Fixed64、setupGeom、网格只扩不减、StackOne/StackAll 含 Qt 6.8 的 idx==0 怪癖）均按 Qt 6.8.3 源码复刻并有回归测试背书（XGui.md 第 4~7 节）。主要问题集中在三处：① **XStackedLayout 信号发射路径存在非法类型双关（P0 级 UB/崩溃风险）**；② 基类级行为偏差——`XLayout_alignmentRect` 与 Qt 默认居中/visualAlignment/maximum-hack 不一致、`XBoxLayout::setGeometry` 未实现父控件 RTL 时 visualDir 互换；③ 样式相关默认值（内容边距 0 vs Qt 样式 11px、间距 0 vs 样式约 6px）与 QObject/XObject 中间层缺失，均属文档已声明的架构近似。硬约束 1/6 全绿（无字符串 API、无 memcpy 复制对象、无直接 malloc/free/strdup、init/deinit 与 copy/move 成对）；约束 7/8 各有一处 P2（兼容旧名别名保留、typedef 重复定义）。

---

## 一、模块概览

| X 类 | Qt 类 | X 继承链 | Qt 继承链 | 一比一 | API 缺口 | 功能缺口 | 对齐度 |
|---|---|---|---|---|---|---|---|
| XLayoutItem | QLayoutItem | XLayoutItem→XClass | QLayoutItem（无基类） | ✓* | 0 | 0 | 95 |
| XWidgetItem（内部） | QWidgetItem | XWidgetItem→XLayoutItem | QWidgetItem→QLayoutItem | ✓ | 0 | 1 | 88 |
| XSpacerItem | QSpacerItem | XSpacerItem→XLayoutItem | QSpacerItem→QLayoutItem | ✓ | 1 | 0 | 92 |
| XLayout | QLayout | XLayout→XLayoutItem→XClass | QLayout→QObject+QLayoutItem | × | 3 | 5 | 80 |
| XBoxLayout | QBoxLayout | XBoxLayout→XLayout | QBoxLayout→QLayout | ✓ | 2 | 2 | 85 |
| XGridLayout | QGridLayout | XGridLayout→XLayout | QGridLayout→QLayout | ✓ | 2 | 2 | 85 |
| XStackedLayout | QStackedLayout | XStackedLayout→XLayout | QStackedLayout→QLayout | ✓ | 1 | 4 | 78 |

\* XLayoutItem 侧 XClass 为工程根类，Qt 无对应中间类；与 Widget 模块审计口径一致（XClass 不计为缺口）。

> 说明：XHBoxLayout/XVBoxLayout 为 `XBoxLayout` 的 typedef（Qt 中 QHBoxLayout/QVBoxLayout 是独立类），计入 XBoxLayout 功能缺口与“缺失类清单”。

## 二、跨类系统性问题（按硬约束编号）

### 2.1 V1【P0·约束5/代码正确性】XStackedLayout 信号发射的类型双关

`XStackedLayout.c:21-32` `xstackedlayout_emitInt()`：

```c
if (self && ((XObject*)self)->m_signalSlot) {
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
```

- XStackedLayout 的继承链是 XLayout→XLayoutItem→XClass，**不是 XObject**（头文件 8-10 行与 XGui.md 10.117 均明确声明）。实测内存布局：`offsetof(XObject, m_signalSlot)=40`，`offsetof(XLayoutItem, m_alignment)=40` —— 强转后读到的“m_signalSlot”实际是 `m_base.m_base.m_alignment`。
- 后果：布局条目未设对齐（默认 0）时信号**静默不触发**（此前“连接的槽从未被触发”的问题并未修好）；一旦堆叠布局作为条目被设过对齐（如挂入其他布局后 setAlignmentLayout），m_signalSlot 读到非零垃圾指针，`XObject_emitSignal` 会解引用非法内存 → UB/崩溃。
- 且 XStackedLayout 非 XObject，`XObject_connect_*` 本就无法合法连接这两个信号，该伪发射路径既不可达又危险。与 XGui.md 10.117“currentChanged/widgetRemoved 目前是稳定标识函数，不能使用 XObject_connect_* 连接”的声明矛盾。
- 修复建议：二选一——(a) 让 XStackedLayout 真正继承 XObject（改动大，需评估虚表/布局影响）；(b) 删除该发射路径，恢复纯标识函数语义并在头文件明确“不支持连接”，或引入独立的回调注册机制。

### 2.2 V2【P1·约束3】XLayout_alignmentRect 与 Qt 6.8 不一致

`XLayout.c:741-772` 对照 `qlayout.cpp:1265-1305`：

| Qt 6.8 行为 | X 实现 | 结论 |
|---|---|---|
| 水平无 Left/Right 位时 **居中**（`x += (r.width()-s.width())/2`） | 无水平位时保持 `out.x = r.x`（贴左） | ✗ |
| 垂直无 Top/Bottom 位时 **居中** | 无垂直位时保持 `out.y = r.y`（贴顶） | ✗ |
| 尺寸先按 expanding/无对齐位时取 maximumSize（对齐时 maximum 会返回 MAX 的 hack）再 `boundedTo(r.size())` | 仅用 sizeHint 且只在 `pref < r` 时收缩 | ✗（缺 maximum-hack 与 expanding 分支） |
| 有 hfw 且无垂直位时用 `heightForWidth(s.width())` 限高 | 无 hfw 分支 | ✗ |
| `QStyle::visualAlignment(parent->layoutDirection(), a)`：RTL 时 Left/Right 互换 | 完全未做布局方向处理 | ✗ |

该函数是 QLayout 的 protected 对齐矩形算法，X 中为公开函数并被 `VXBoxLayout_setGeometry` / `VXGridLayout_setGeometry` 在“布局自身有对齐”时调用，因此盒式/网格布局设置 `AlignTop` 等单一方向对齐时，摆放位置与 Qt 不同（X 贴左上，Qt 水平居中）。

### 2.3 V3【P1·约束3】XBoxLayout::setGeometry 未实现父控件 RTL 的 visualDir

`XBoxLayout.c:931-1028` 对照 `qboxlayout.cpp:1130-1180`：Qt 在 setGeometry 中先计算

```cpp
Direction visualDir = d->dir;
if (parent && parent->isRightToLeft()) {
    if (d->dir == LeftToRight) visualDir = RightToLeft;
    else if (d->dir == RightToLeft) visualDir = LeftToRight;
}
```

随后**镜像公式与 reverse 逆序更新都使用 visualDir**。X 实现只用 `box->m_direction`（993-996 行 reverse、1008-1010 行镜像），未查询 `XWidget_layoutDirection`。XGui.md 5.2 已记录“父控件 RTL 时 L/R 盒方向互换”为 Qt 行为基准，但代码未落实 → 父控件 RTL 场景盒式布局摆放与 Qt 不一致。另缺 Qt 的 `if (d->dirty || r != geometry())` 守卫（纯性能差异）。

### 2.4 V4【P2·约束7/8】兼容旧名别名与 typedef 重复定义

`XLayoutItem.h:55-80`：

- 保留 `XLayoutAlignment / XLayoutAlignments` typedef 与 `XLayoutAlignment_*` 宏作为“兼容旧名称”，与新名 `XAlignment/XAlignments` 并存 —— 约束 7“旧 API 不保留”的 P2 违例（无 Qt 冲突，属工程内改名并存；建议迁移完成后删除）。
- 第 63 行 `typedef XAlignments XLayoutAlignments;` 与第 80 行 `typedef uint32_t XLayoutAlignments;` 为**同名 typedef 重复定义**：C11 才允许同型重定义（C11 6.7p3），项目 `CMAKE_C_STANDARD 99` 严格 C99 下属约束违规（GCC 以扩展接受，`-pedantic-errors` 会报错）——约束 8 的 P2 违例。

### 2.5 V5【P2】文档/代码小问题汇总

- `XLayout.h:306-311`：`XLayout_deleteAllItems` 标注“对标 QLayout::deleteAllItems”，但 **Qt 6.8 的 QLayout 无此 API**（Qt4 遗留名，Qt5/6 已删除），实为 X 扩展，对标引用错误。
- `XLayout.c:735-739`：`XLayout_spacing(NULL)` 返回 0，头文件 377 行写“失败返回 -1”，文档与实现不符。
- `XLayout.h:283-303`：`setAlignmentWidget/setAlignmentLayout` 返回 void，Qt `setAlignment` 两重载返回 bool（找到返回 true），签名语义不一致（计入 API 缺口）。
- `XGridLayout.h:82-86`：在公共头定义通用名 `XOrientation`（对标 Qt::Orientation），XGui 其他模块未来若引入同名枚举有冲突风险，建议改名 `XGridLayoutOrientation`。
- `XLayoutItem_Protected.h` 无 UTF-8 BOM（保护头，豁免正式审计，提示性）。
- 全部公共头均有 BOM ✓；公共 API 中文 Doxygen @brief/@param/@return 基本完整，仅少数 void 函数未写 `@return 无`（风格文档示例要求，提示性，未计违规）。

### 2.6 硬约束逐条核查

| 约束 | 结论 |
|---|---|
| 1 拥有型字符串一律 XString* | ✅ 不适用：本模块无任何字符串 API/字段 |
| 2 继承一比一含中间基类 | ⚠️ XLayout 缺 QObject(XObject) 中间层（双基类无法在 C 结构体表达，文档已声明）；XHBoxLayout/XVBoxLayout 为 typedef 而非独立类；其余链（XBoxLayout→XLayout、XGridLayout→XLayout、XStackedLayout→XLayout、XSpacerItem/XWidgetItem→XLayoutItem）一比一 |
| 3 样式/绘制不得精简近似 | ⚠️ 无绘制代码；几何算法主体按 Qt 复刻，但 alignmentRect（V2）、visualDir（V3）、默认边距 0 vs Qt 样式 11px、默认间距 0 vs 样式约 6px（XLayout.c:58-70、XLayout_effectiveSpacing，架构性近似、头文件已声明）为偏差 |
| 4 公共头 Doxygen+BOM | ✅ 6 个公共头 BOM 齐全、注释完整（个别 void 缺 @return 无，提示性） |
| 5 信号 *_signal 宏/回调 | ⚠️ XStackedLayout 两信号均有 *_signal 标识函数，但真实发射路径类型双关损坏（V1，P0） |
| 6 生命周期 | ✅ init/deinit 成对；copy/move 虚槽全部实现且带 `XClassIsVtableNull` 保护；对象复制走字段赋值/虚槽，未用 memcpy 复制对象（XMemcpy 仅用于 int 数组）；无直接 malloc/free/strdup（全部 XMalloc_System/XRealloc_System/XFree_System/XMalloc_Hybrid/XFree_Hybrid） |
| 7 旧 API 不保留 | ⚠️ XLayoutItem.h 兼容旧名别名并存（V4，P2） |
| 8 新代码 C99 | ⚠️ typedef 重复定义为 C11 特性（V4，P2）；其余为 C99（位域、long long、stdbool 均合规） |

---

## 三、逐类对比

### 3.1 XLayoutItem ↔ QLayoutItem（qtbase/src/widgets/kernel/qlayoutitem.h）

**继承**：X `XLayoutItem→XClass`；Qt `QLayoutItem`（无基类）。XClass 为工程根类（含虚表/内存/堆标志），Qt 无对应中间类 —— 判定 ✓（口径与 Widget 审计一致）。

**API 缺口**：0。Qt 全部 17 个公开方法（sizeHint/minimumSize/maximumSize/expandingDirections/setGeometry/geometry/isEmpty/hasHeightForWidth/heightForWidth/minimumHeightForWidth/invalidate/alignment/setAlignment）与 4 个保护虚函数（widget/layout/spacerItem/controlTypes）均有对应：15 个虚槽 + 2 个公开直调，保护入口在 `XLayoutItem_Protected.h`。构造/析构由 `XLayoutItem_init` + `deinit_base` 宏承担。

**功能缺口**：
1. `VXWidgetItem_sizeHint/minimumSize/maximumSize` 未实现 Qt QWidgetItemV2 的三元尺寸缓存（q_cachedMinimumSize/sizeHint/maximumSize + HfwCacheSize=3）——纯性能差异，见 3.2。

**违规**：无（兼容旧名别名问题见 2.4）。

### 3.2 XWidgetItem（内部）↔ QWidgetItem（qlayoutitem.h）

**继承**：X `XWidgetItem→XLayoutItem`；Qt `QWidgetItem→QLayoutItem` ✓（内部实现类，随 `XLayout_Internal.h` 编译，不对外公开）。

**API 缺口**：0（sizeHint/minimumSize/maximumSize/expandingDirections/isEmpty/setGeometry/geometry/widget/hasHeightForWidth/heightForWidth/minimumHeightForWidth/controlTypes 全部实现并注册虚表）。

**功能缺口**：
1. 无 QWidgetItemV2 等价类（尺寸缓存缺失，高度计算每次直查控件；Qt 自 4.4 起默认用 V2 且带 Dirty 标记与 3 项 hfw 缓存）。
2. `VXWidgetItem_copy/move` 不复制 `m_ownedByLayout`（拷贝后目标保持自身拥有状态，合理；Qt 不可拷贝，X 提供虚槽属工程扩展）。

**违规**：无。

### 3.3 XSpacerItem ↔ QSpacerItem（qlayoutitem.h）

**继承**：X `XSpacerItem→XLayoutItem`；Qt `QSpacerItem→QLayoutItem` ✓。

**API 缺口**：
| Qt 原型 | X | 说明 |
|---|---|---|
| `QSpacerItem(int w,int h, Policy hData=Minimum, Policy vData=Minimum)` / `changeSize(w,h,hData=Minimum,vData=Minimum)` | `XSpacerItem_create(w,h,hPolicy,vPolicy)` / `XSpacerItem_changeSize(w,h,hPolicy,vPolicy)` | 缺默认参数便捷形态（C 无默认参数），需显式传 4 参；`create(w,h)` 便捷重载可考虑 |

sizePolicy()、spacerItem()、尺寸/几何虚槽全部对齐；位语义 GrowFlag=1/ExpandFlag=2/ShrinkFlag=4/IgnoreFlag=8 与 Qt 6.8 qsizepolicy.h 一致（XLayoutItem.c:438-476）。

**功能缺口**：0。`m_isMagic` 为 QBoxLayoutItem::magic 的等价内部标志，setDirection 翻转逻辑与 qboxlayout.cpp:1130 一致。

**违规**：无。

### 3.4 XLayout ↔ QLayout（qlayout.h）

**继承**：X `XLayout→XLayoutItem→XClass`；Qt `QLayout→QObject, QLayoutItem`（双基类）。**缺 QObject(XObject) 中间层**：布局无对象名/属性/事件/子对象树/信号能力；头文件与 XGui.md 10.117 已声明。判定 ×（结构性偏差，非可轻易补齐项）。

**API 缺口**：
| Qt 原型 | X | 说明 |
|---|---|---|
| `void setContentsMargins(const QMargins&)` | 无 | 缺 XMargins 重载（仅 4-int 版本） |
| `void addWidget(QWidget*)` | 无基类入口 | 仅子类提供（XBoxLayout_addWidget / XGridLayout_addWidgetAuto），基类缺通用入口 |
| `bool setAlignment(QWidget*, Qt::Alignment)` / `bool setAlignment(QLayout*, ...)` | `void XLayout_setAlignmentWidget/Layout` | 返回类型不一致（Qt bool：找到返回 true） |
| `QRect alignmentRect(const QRect&) const`（protected） | `XRect XLayout_alignmentRect(...)`（public） | X 暴露为公开且语义偏差（见 2.2） |
| Q_PROPERTY spacing/contentsMargins/sizeConstraint | 无属性系统 | 架构性（XGui 无 Q_PROPERTY 等价物） |

X 扩展（Qt 无对应）：`replaceItemAt_base`（对应 QLayoutPrivate::replaceAt，正确）、`setAlignmentItem`、`itemForWidget`、`deleteAllItems`（**对标引用错误，Qt 6.8 无此 API**）、`indexOfItem` 公开化。

**功能缺口**：
1. 默认内容边距解析为 0（XLayout.c:58-70），Qt 缺省取样式值（多数平台 11px，qlayout.cpp getContentsMargins → PM_LayoutLeftMargin）；默认间距 0 vs Qt 样式约 6px。已文档声明，属“无 QStyle”架构近似。
2. `XLayout_alignmentRect` 与 qlayout.cpp:1265 行为不一致（2.2 详表）。
3. `XLayout_activate`（XLayout.c:1083-1126）与 qlayout.cpp activate 差异：Qt 用 total* 尺寸（含菜单栏/边框）、SetDefaultConstraint 区分 isWindow 与 explicitMin 保存/恢复、enabled=false 或已激活时返回 false、子布局委托顶层；X 直接用 minimumSize、无 explicitMin 跟踪、不检查 enabled/activated、子布局直接以控件几何解算。
4. 基类默认 `expandingDirections` 返回 None，Qt QLayout 默认返回 `Qt::Horizontal|Qt::Vertical`（qlayout.cpp:944）——仅影响不自实现该虚槽的自定义 XLayout 子类（盒式/网格/堆叠均覆盖，无实际影响）。
5. 非 XObject：无信号/事件/属性/子对象语义（与 QLayout 的 QObject 半边不对齐，文档声明）。
6. `XLayout_indexOf(QWidget*)`（XLayout.c:593-603）直读内部数组，Qt 经 itemAt() 虚函数扫描（X 的 indexOfItem 版本则正确走了虚函数）。

**违规**：见 2.1~2.5（本类相关：alignmentRect/activate/deleteAllItems 引用错误/spacing NULL 返回）。

### 3.5 XBoxLayout ↔ QBoxLayout / QHBoxLayout / QVBoxLayout（qboxlayout.h）

**继承**：X `XBoxLayout→XLayout`；Qt `QBoxLayout→QLayout` ✓。XHBoxLayout/XVBoxLayout 为 typedef（非独立类），Qt 为独立类。

**API 缺口**：
| Qt | X | 说明 |
|---|---|---|
| `enum Direction { LeftToRight, RightToLeft, TopToBottom, BottomToTop, Down=TopToBottom, Up=BottomToTop }` | 无 Down/Up 别名 | 枚举值 0-3 一致，仅缺两个别名 |
| `addStretch(int=0)` / `insertStretch(int,int=0)` | 需显式传 stretch | C 无默认参数；`addStretch(self, 0)` 等价 |
| `addWidget(w,int=0,Qt::Alignment=0)` / `insertWidget(...,int=0,Qt::Alignment=0)` / `addLayout(l,int=0)` | `addWidget`+`addWidgetEx` / `insertWidget`+`insertWidgetEx` / `addLayout`+`addLayoutEx` | 重载拆分合理，命名可接受 |

addSpacing/addSpacerItem/addStrut/addItem/insert* 全套、setStretchFactor 两重载、stretch/setStretch、direction/setDirection、sizeHint~count 虚槽全覆盖。

**功能缺口**：
1. setGeometry 未实现父控件 RTL 的 visualDir 互换（2.3，P1）。
2. XHBoxLayout/XVBoxLayout 为 typedef：无法区分类型做类型安全判断，也无独立 class_init（Qt 可 `qobject_cast` 判型）；建议维持 typedef 并在文档声明，或补独立结构体。
3. 次要：`XBoxLayout_doHeightForWidth` 水平分支分配了未使用的 sizes 数组（XBoxLayout.c:797/816，死分配）；`XBoxLayout_collectGeom` 有 5 个未使用局部变量（hExp/vExp/dummy/maxW/maxH，334-338 行）——残留死代码，建议清理。

**违规**：2.3（visualDir）。

### 3.6 XGridLayout ↔ QGridLayout（qgridlayout.h）

**继承**：X `XGridLayout→XLayout`；Qt `QGridLayout→QLayout` ✓。

**API 缺口**：
| Qt | X | 说明 |
|---|---|---|
| `inline void addWidget(QWidget*)` | `XGridLayout_addWidgetAuto` | 命名不一致（功能等价：走默认定位游标 nextR/nextC） |
| `addItem(item,row,col,rowSpan=1,colSpan=1,align)` | `XGridLayout_addItemAt(...,rowSpan,columnSpan,alignment)` | 缺默认参数便捷形态 |
| `addItem(QLayoutItem*)`（protected override） | `XGridLayout_addItem`（public） | X 把 Qt 保护 API 公开化（自动定位游标语义正确） |

其余：行列间距/伸缩/最小尺寸、rowCount/columnCount/cellRect/itemAtPosition/getItemPosition/setDefaultPositioning/setOriginCorner/originCorner、addWidget/addLayout 各 4 个重载、sizeHint~count 虚槽全部覆盖。

**功能缺口**：
1. 行列分配 `XGridLayout_shrinkHint/growExtra`（XGridLayout.c:270-384）为近似算法：按 (hint-min) 差值比例收缩、按剩余像素逐像素分摊，未逐像素复刻 Qt 的 qGeomCalc 三段式 + Fixed64 定点（盒式布局反而完整移植了 Fixed64，网格没有）——空间不足/有余的像素级结果可能与 Qt 差 1px。
2. heightForWidth 行高回填（XGridLayout.c:1086-1105）用“首选所需高度求和再分配”近似 Qt 的行高协商，未完全复刻 qgridlayout.cpp 的 hfw 分支细节。
3. 次要：`cellRect` 依赖最近一次激活的缓存（未激活返回 0 矩形），Qt 同理基于缓存列位置，可接受。

**违规**：无（近似算法计为功能缺口，不构成“空实现/占位”）。

### 3.7 XStackedLayout ↔ QStackedLayout（qstackedlayout.h）

**继承**：X `XStackedLayout→XLayout`；Qt `QStackedLayout→QLayout` ✓。

**API 缺口**：
| Qt 原型 | X | 说明 |
|---|---|---|
| `explicit QStackedLayout(QLayout *parentLayout)` | 无 | 缺“挂入父布局”构造映射（另有 QStackedLayout()/QStackedLayout(QWidget*) 两形态，X 用 create(parent) 覆盖） |
| `void removeWidget(QWidget*)` | `XStackedLayout_removeWidget` | **反向缺口**：Qt 6.8 无此公开 API（用 takeAt），X 为扩展 |

addWidget/insertWidget/currentWidget/currentIndex/widget/count/stackingMode/setStackingMode/addItem/sizeHint/minimumSize/itemAt/takeAt/setGeometry/hasHeightForWidth/heightForWidth、信号 currentChanged/widgetRemoved（*_signal 标识函数）、槽 setCurrentIndex/setCurrentWidget 全部覆盖。setStackingMode 的 `if (const int idx = currentIndex())` 索引 0 怪癖与 Qt 6.8 逐字一致（XStackedLayout.c:437-441）。

**功能缺口**：
1. **信号发射路径损坏（P0，2.1 详述）**：xstackedlayout_emitInt 类型双关读 m_signalSlot。
2. `XStackedLayout_removeWidget`（XStackedLayout.c:369-387）把页面控件 parent 置 NULL；Qt 的 takeAt（qstackedlayout.cpp:231-252）只 hide，**不 reparent**（页面仍是父控件子控件）。X 头文件 381-384 行注释“对标 Qt：…移出本容器”与 Qt 实际行为不符。
3. `XStackedLayout_addItem`（328-331 行）对非控件条目直接忽略；Qt addItem 会 `delete item`（qstackedlayout.cpp addItem）。所有权约定差异（X 借用指针约定），XGui.md 10.117 已声明。
4. 焦点链转移未实现：Qt setCurrentIndex 会尝试把焦点从旧页转移到新页的 focusWidget/焦点链首个可见可聚焦子控件（qstackedlayout.cpp:262-320），X 仅有 clearFocus（XStackedLayout.c:398）；X 无 raise/lower，z-order 亦未处理（文档声明）。
5. 继承 XLayout 的 expandingDirections=None / maximumSize=(MAX,MAX) 默认（Qt QStackedLayout 未覆盖，继承 QLayout 默认 Both / MAX+边框合计），自定义子类场景有差异。

**违规**：
1. V1【P0】`XStackedLayout.c:22-32` 非法强转 XObject* 读 m_signalSlot（偏移与 m_alignment 重合）：未设对齐→信号静默丢失；设了对齐→读到垃圾指针→UB/崩溃。
2. `XStackedLayout.c:381-384` removeWidget 将页面 parent 置 NULL，与 Qt takeAt 不 reparent 不符（文档注释亦错误宣称“对标 Qt”）。

---

## 四、缺失类清单（Qt 对应范围内）

| Qt 类 | Qt 头文件 | 现状 | 建议 |
|---|---|---|---|
| QHBoxLayout | qtbase/src/widgets/kernel/qboxlayout.h | XHBoxLayout 为 typedef | 可维持 typedef（功能等价），建议在文档声明；如需类型安全判型再补独立类 |
| QVBoxLayout | qtbase/src/widgets/kernel/qboxlayout.h | XVBoxLayout 为 typedef | 同上 |
| QWidgetItemV2 | qtbase/src/widgets/kernel/qlayoutitem.h | 无 | 不实现亦可（仅性能缓存）；若做，内部实现 hfw/最小/首选三元缓存 + Dirty 标记 |

## 五、优先任务建议

1. 【P0】修复 `XStackedLayout.c:22-32` 信号发射类型双关：让 XStackedLayout 继承 XObject 走正规信号，或删除伪发射路径恢复纯标识函数语义（与 XGui.md 10.117 声明一致），二者必选其一；顺带补回归断言（未连接时 setCurrentIndex/takeAt 不得崩溃）。
2. 【P1】按 `qlayout.cpp:1265` 重写 `XLayout_alignmentRect`：补“无水平/垂直位默认居中”、expanding+maximum-hack、visualAlignment(RTL) 与 hfw 限高分支，并加 XBox/XGrid 自身对齐用例。
3. 【P1】按 `qboxlayout.cpp` setGeometry 补父控件 RTL 时 visualDir 互换（镜像公式与 reverse 逆序均需使用 visualDir），并加 `dirty/几何变化` 守卫。
4. 【P1】统一激活语义：`XLayout_activate` 改用 total* 尺寸、SetDefaultConstraint 按 isWindow/explicitMin 分支、enabled/activated 短路（对齐 qlayout.cpp activate）。
5. 【P2】补 `XLayout_setContentsMargins(XMargins)` 重载与基类 `XLayout_addWidget`；`setAlignment*` 返回 bool 或文档声明差异。
6. 【P2】修正 `XLayout.h` 对 `QLayout::deleteAllItems` 的错误对标引用（Qt 6.8 无此 API，标注为 X 扩展）；修正 `XLayout_spacing(NULL)` 返回 0 与文档“返回 -1”不一致。
7. 【P2】清理 `XLayoutItem.h` 兼容旧名别名与重复 typedef：删除 XLayoutAlignment* 别名/宏（迁移调用方），满足严格 C99（约束 7/8）。
8. 【P2】`XGridLayout` 行列分配换用与盒式一致的 Fixed64 qGeomCalc 移植，消除像素级差 1px 的近似；清理 `XBoxLayout` 死分配与未用变量。
9. 【P2】统一声明样式边界：默认边距 0（Qt 样式 11px）、间距 0（Qt 样式约 6px）写进公共头与 XGui.md 的已知偏差清单，防止后续被误报为 bug。
10. 【P2】`XStackedLayout_removeWidget` 移除 parent=NULL 的 reparent（对齐 Qt takeAt 不 reparent）；评估补 `XStackedLayout_create_layout(XLayout*)` 构造映射。
