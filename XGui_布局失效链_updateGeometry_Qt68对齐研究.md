# XGui 布局失效链 / updateGeometry —— Qt 6.8 逐条对齐研究

> 源码基线：qtbase **v6.8.0**（tag），取自 code.qt.io cgit 与 GitHub 镜像原始文件。
> 行号锚点均按 v6.8.0 原文件。写作日期：2026-10-31。
> 本文是「隐藏控件 updateGeometry 真 no-op」实施批次的研究底稿：先逐条列出 Qt
> 的事实，再对照 XGui 现状，最后给出实施映射。

---

## 1. 研究范围与材料

| Qt 文件 | 用途 |
| --- | --- |
| `src/widgets/kernel/qlayout.cpp` | QLayout::update / invalidate / activate / widgetEvent / isEmpty |
| `src/widgets/kernel/qlayout_p.h` | QLayoutPrivate 字段（activated/topLevel/enabled/rect） |
| `src/widgets/kernel/qlayoutitem.h/.cpp` | QLayoutItem / QWidgetItem / QSpacerItem / QWidgetItemV2 |
| `src/widgets/kernel/qboxlayout.cpp`、`qgridlayout.cpp`、`qstackedlayout.cpp` | 子类 isEmpty/invalidate 覆盖情况（Qt 6.8 均在 kernel/ 下） |
| `src/widgets/kernel/qwidget.cpp` | QWidgetPrivate::updateGeometry_helper、setVisible show/hide 分支、setSizePolicy、hideChildren/showChildren |

注意：Qt 6.8 **没有** `src/widgets/kernel/qwidgetitem.cpp` / `qspaceritem.cpp`
（Qt 5 的布局），QWidgetItem/QSpacerItem 全部实现在 `qlayoutitem.cpp`。

---

## 2. 逐条事实（Qt 6.8 原文）

### 2.1 QLayout::update() —— 失效沿**布局链**自动上推（qlayout.cpp:971）

```cpp
void QLayout::update()
{
    QLayout *layout = this;
    while (layout && layout->d_func()->activated) {
        layout->d_func()->activated = false;
        if (layout->d_func()->topLevel) {
            Q_ASSERT(layout->parent()->isWidgetType());
            QWidget *mw = static_cast<QWidget*>(layout->parent());
            QCoreApplication::postEvent(mw, new QEvent(QEvent::LayoutRequest));
            break;
        }
        layout = static_cast<QLayout*>(layout->parent());
    }
}
```

语义拆解：

1. 沿 **QLayout 对象链**（子布局的 QObject parent 是父布局，不是控件）逐级把
   `d->activated` 清 false；
2. **门控早退**：链上任何一级 `activated==false`（装配期或此前已失效）→
   循环不进/即停，**不再投递**——一次失效风暴只投一个 LayoutRequest；
3. 到 `topLevel` 布局（其 QObject parent 是控件）→ 向**宿主控件** post
   `QEvent::LayoutRequest`（队列事件，不压缩，但下游 activate 幂等）；
4. **只清 activated 标志，不清任何尺寸缓存**——Qt 的 QLayout 本身没有
   sizeHint 缓存（QBoxLayout 有 `d->dirty`/hfw 缓存，由 QBoxLayout::invalidate
   自己清），祖先控件的缓存 QWidgetItemV2 由 updateGeometry_helper 的
   `widgetItem->invalidateSizeCache()` 清（见 2.6）。

### 2.2 QLayout::invalidate()（qlayout.cpp:469）

```cpp
void QLayout::invalidate()
{
    Q_D(QLayout);
    d->rect = QRect();
    update();
}
```

要点：**不向子条目级联 invalidate**（`QLayoutItem::invalidate` 本身是空实现，
见 2.5），只复位自身几何并触发 update() 上推。子类 QBoxLayout/QGridLayout 的
覆盖是 `d->setDirty(); QLayout::invalidate();`（qboxlayout.cpp:670、
qgridlayout.cpp:1647），清**各自**的度量缓存；QStackedLayout 无覆盖。

### 2.3 QLayout::activate()（qlayout.cpp:995）

```cpp
bool QLayout::activate()
{
    Q_D(QLayout);
    if (!d->enabled || !parent())
        return false;
    if (!d->topLevel)
        return static_cast<QLayout*>(parent())->activate();
    if (d->activated)
        return false;
    ...
    activateRecursiveHelper(this);   // 对整树逐条 item->invalidate()，并置各级 activated=true
    ...按 constraint 写回控件 min/max...
    d->doResize();                   // setGeometry(contentsRect)
    ...
}
```

```cpp
void QLayout::activateRecursiveHelper(QLayoutItem *item)
{
    item->invalidate();
    QLayout *layout = item->layout();
    if (layout) {
        ...逐子条目递归...
        layout->d_func()->activated = true;
    }
}
```

要点：

1. **activate 自身没有可见性守卫**——「顶层不可见就不激活」的闸不在 activate
   里，而在 **widgetEvent 的 LayoutRequest 分支**（2.4）与 **setVisible show
   分支的可见父链 walk**（2.7）；
2. 幂等早退：`if (d->activated) return false;`；
3. 非顶层布局 → 上溯到顶层布局的 activate（子布局从不独立解算）；
4. 激活即**整树失效重测**（activateRecursiveHelper 对每条 item 调 invalidate；
   QWidgetItem 的 invalidate 是 no-op，所以重测成本由 QWidgetItemV2 缓存吸收）。

### 2.4 QLayout::widgetEvent()（qlayout.cpp:520，LayoutRequest 分支在 548）

```cpp
    case QEvent::LayoutRequest:
        if (static_cast<QWidget *>(parent())->isVisible())
            activate();
        break;
```

**「顶层不可见 ⇒ 不激活」守卫的真正落点**。配合 2.1 的门控：隐藏宿主的布局
一旦失效（activated=false），后续失效在 update() 处即停，不再投递——
隐藏页内的反复 setText 从第二次起接近零成本。

### 2.5 QLayoutItem::isEmpty() 的全部实现

| 类 | 覆盖? | 实现 | 锚点 |
| --- | --- | --- | --- |
| QLayoutItem | 纯虚 | `virtual bool isEmpty() const = 0;` | qlayoutitem.h:35 |
| QWidgetItem | ✅ | `(wid->isHidden() && !wid->sizePolicy().retainSizeWhenHidden()) \|\| wid->isWindow()` | qlayoutitem.cpp:696 |
| QSpacerItem | ✅ | `return true;`（**空白条目始终“空”，但仍占空间**——isEmpty 只表达“不参与内容度量”） | qlayoutitem.cpp:686 |
| QLayout | ✅ | itemAt 扫描：**所有条目都空才为空** | qlayout.cpp:422 |
| QBoxLayout / QGridLayout / QStackedLayout | ❌ 无覆盖 | 继承 QLayout::isEmpty | — |

```cpp
bool QLayout::isEmpty() const          // qlayout.cpp:422
{
    int i = 0;
    QLayoutItem *item = itemAt(i);
    while (item) {
        if (!item->isEmpty())
            return false;
        ++i;
        item = itemAt(i);
    }
    return true;
}
```

注意 QLayout::isEmpty 是**递归有效**的：子布局条目的 isEmpty 又走 QLayout::isEmpty。
QWidgetItemV2 的 sizeHint/minimumSize/maximumSize 同样先查 `isEmpty()` 返回 (0,0)
（qlayoutitem.cpp:786-822），QWidgetItem::sizeHint/minimumSize/maximumSize 同
（qlayoutitem.cpp:624-682）。

### 2.6 invalidate() 的全部实现与 QWidgetItemV2 缓存

| 类 | 覆盖? | 实现 |
| --- | --- | --- |
| QLayoutItem | ✅ | **空函数** `void QLayoutItem::invalidate() { }`（qlayoutitem.cpp:268） |
| QWidgetItem | ❌ 无覆盖 | 继承空实现（qlayoutitem.h 的 QWidgetItem 段没有 invalidate 声明） |
| QSpacerItem | ❌ 无覆盖 | 继承空实现（文档 `\sa QSpacerItem::invalidate()` 指的是继承版本） |
| QWidgetItemV2 | ❌ 无 invalidate 覆盖 | 缓存清除走**另一条路**：`invalidateSizeCache()`（inline 公有，qlayoutitem.h:126），由 QWidgetPrivate 调用 |
| QLayout | ✅ | `d->rect = QRect(); update();` |
| QBoxLayout / QGridLayout | ✅ | `d->setDirty(); QLayout::invalidate();` |
| QStackedLayout | ❌ | 继承 QLayout::invalidate |

结论：**QLayoutItem 体系的 invalidate 是空操作**；Qt 的失效语义全部由
「QLayout::invalidate→update() 上推」+「QWidgetPrivate 侧清 QWidgetItemV2 缓存」
两件套承担。

### 2.7 QWidgetPrivate::updateGeometry_helper（qwidget.cpp:10506）——本批核心

```cpp
void QWidgetPrivate::updateGeometry_helper(bool forceUpdate)
{
    Q_Q(QWidget);
    if (widgetItem)
        widgetItem->invalidateSizeCache();
    QWidget *parent;
    if (forceUpdate || !extra || extra->minw != extra->maxw || extra->minh != extra->maxh) {
        const int isHidden = q->isHidden() && !size_policy.retainSizeWhenHidden()
                             && !retainSizeWhenHiddenChanged;

        if (!q->isWindow() && !isHidden && (parent = q->parentWidget())) {
            if (parent->d_func()->layout)
                parent->d_func()->layout->invalidate();
            else if (parent->isVisible())
                QCoreApplication::postEvent(parent, new QEvent(QEvent::LayoutRequest));
        }
    }
}

void QWidget::updateGeometry()
{
    Q_D(QWidget);
    d->updateGeometry_helper(false);
}
```

公开文档原话（qwidget.cpp:10523-10530）：

> Notifies the layout system that this widget has changed and may need to
> change geometry. … **For explicitly hidden widgets, updateGeometry() is a
> no-op. The layout system will be notified as soon as the widget is shown.**

逐条拆解：

1. `widgetItem->invalidateSizeCache()`：**无条件**先清控件条目缓存（隐藏与否
   无关）；XGui 的 XWidgetItem 不缓存度量（直读控件），对应**空操作**；
2. isHidden 守卫是**三条件合取**：`isHidden() && !retainSizeWhenHidden() &&
   !retainSizeWhenHiddenChanged`。隐藏但 retainSizeWhenHidden 的控件**仍占布局
   空间**，提示变化必须通知；`retainSizeWhenHiddenChanged` 见 2.9；
3. 守卫只拦**父通知**：顶层窗口（isWindow）本来就无父可通知；
4. 父通知两分支：父挂布局 → `parent->layout->invalidate()`（激活交给
   LayoutRequest 消费端的可见性闸）；父无布局且**可见** → post LayoutRequest；
5. **不激活任何布局**：updateGeometry 自己不同步 activate——激活统一走
   invalidate→update()→LayoutRequest→widgetEvent(可见性闸)→activate 的事件链；
6. forceUpdate 的三个调用点：setVisible show 分支（Hidden 位**先清后调**，
   qwidget.cpp:8340）、setFixedSize（qwidget.cpp:4123）、prepareToRender
   （渲染前补偿，qwidget.cpp:5363）。setMinimumSize/setMaximumSize 传
   `min==max && min==max`（定尺控件 forceUpdate=true，qwidget.cpp:3984/4043）。

### 2.8 show / hide 的布局补偿（qwidget.cpp:8335-8361 / 8405-8411）

show 分支（「隐藏时通知欠账，show 时补」的机制）：

```cpp
        // whether we need to inform the parent widget immediately
        bool needUpdateGeometry = !q->isWindow() && q->testAttribute(Qt::WA_WState_Hidden);
        // we are no longer hidden
        q->setAttribute(Qt::WA_WState_Hidden, false);

        if (needUpdateGeometry)
            updateGeometry_helper(true);      // Hidden 位已清 ⇒ 守卫必放行

        // activate our layout before we and our children become visible
        if (layout)
            layout->activate();

        if (!q->isWindow()) {
            QWidget *parent = q->parentWidget();
            while (parent && parent->isVisible() && parent->d_func()->layout
                   && !parent->data->in_show) {
                parent->d_func()->layout->activate();
                if (parent->isWindow())
                    break;
                parent = parent->parentWidget();
            }
            ...
        }
```

hide 分支：

```cpp
        // invalidate layout similar to updateGeometry()
        if (!q->isWindow() && q->parentWidget()) {
            if (q->parentWidget()->d_func()->layout)
                q->parentWidget()->d_func()->layout->invalidate();
            else if (q->parentWidget()->isVisible())
                QCoreApplication::postEvent(q->parentWidget(), new QEvent(QEvent::LayoutRequest));
        }
```

**关键事实——hideChildren 不给子孙打 WA_WState_Hidden**（qwidget.cpp
hideChildren：只清 `WA_WState_Visible` + 发 QHideEvent）。因此隐藏页的**子孙
控件 isHidden()==false**：它们调用 updateGeometry 时守卫**不拦**，照样通知
直接父布局；真正把成本拦下来的是 2.1 的 activated 门控 + 2.4 的可见性闸——
隐藏页宿主布局被失效一次后 activated=false，后续失效在 update() 处即停，
LayoutRequest 消费端也因宿主不可见而不激活。

### 2.9 setSizePolicy 与 retainSizeWhenHiddenChanged（qwidget.cpp:10373-10393）

```cpp
void QWidget::setSizePolicy(QSizePolicy policy)
{
    ...
    if (d->size_policy.retainSizeWhenHidden() != policy.retainSizeWhenHidden())
        d->retainSizeWhenHiddenChanged = 1;

    d->size_policy = policy;
    ...
    updateGeometry();
    d->retainSizeWhenHiddenChanged = 0;
    ...
}
```

`retainSizeWhenHiddenChanged` 是 QWidgetPrivate 的 1 位标志（qwidget.cpp:141）：
仅在 setSizePolicy 翻转 retain 位期间置位，让**隐藏**控件的策略翻转也能穿过
isHidden 守卫（2.7 第 2 条的第三合取项）。

---

## 3. XGui 现状对照表

| # | Qt 事实（锚点） | XGui 现状（锚点） | 对齐状态 |
| --- | --- | --- | --- |
| 1 | update 沿布局链清 activated + 门控 + 顶层 post LayoutRequest（qlayout.cpp:971） | `VXLayout_invalidate` 的 activated-walk + post（XLayout.c:251-270，2026-10-20 已落） | ✅ 已对齐 |
| 2 | QLayout::invalidate 不级联子条目、子类各自清缓存 | XGui 反向级联子条目 + 清父布局链缓存（XLayout.c:226-245）——XGui 三级尺寸缓存需要（Qt 无缓存），**有意增强** | ✅ 语义等价（缓存补偿） |
| 3 | activate 有 enabled/parent 早退 + activated 幂等早退（qlayout.cpp:1002） | `XLayout_activate` 幂等早退（XLayout.c:1174） | ✅（XGui 更严：还比对矩形） |
| 4 | **activate 无可见性守卫**；闸在 widgetEvent LayoutRequest（qlayout.cpp:548） | `VXWidget_event` LayoutRequest 分支已有 `m_layout && XWidget_isVisible`（XWidget.c:5381-5388） | ✅ 已对齐（**更正旧记忆②**：闸一直都在，无需给 activate 加守卫） |
| 5 | QWidgetItem::isEmpty 公式（qlayoutitem.cpp:696） | `VXWidgetItem_isEmpty`（XLayoutItem.c:233-242）同公式 | ✅ 已对齐 |
| 6 | QSpacerItem::isEmpty()==true（qlayoutitem.cpp:686） | `VXSpacerItem_isEmpty`==true（XLayoutItem.c:491） | ✅ 已对齐 |
| 7 | QLayout::isEmpty：itemAt 扫描，全空才空；box/grid/stacked 不覆盖（qlayout.cpp:422） | `VXLayout_isEmpty`=`itemCount==0`（XLayout.c:200），box/grid 各自覆盖同式（XBoxLayout.c:753、XGridLayout.c:779） | ❌ **偏离**：含全隐藏条目的布局被判非空 |
| 8 | QLayoutItem/QWidgetItem/QSpacerItem invalidate 全空操作（qlayoutitem.cpp:268） | 基类/控件条目/空白条目 invalidate 全 no-op（XLayoutItem.c:128、XWidgetItem 继承基类） | ✅ 已对齐 |
| 9 | updateGeometry_helper 三合取隐藏守卫（qwidget.cpp:10513） | **无守卫**：`XWidget_updateGeometry` 无条件沿父控件链逐级失效 + 同步激活（XWidget.c:5821-5856，2026-10-20 有意为之） | ❌ **本批主缺口** |
| 10 | 父通知两分支：父布局 invalidate / 无布局且父可见 post（qwidget.cpp:10515-10518） | 直接 `XLayout_activate(parent->m_layout)` 同步激活，无闸（XWidget.c:5852-5854） | ❌ 偏离：隐藏宿主也被同步激活（隐藏页全量重算的执行点） |
| 11 | show 分支补通知 + 可见父链激活（qwidget.cpp:8335-8361） | `XWidget_setVisible` show 分支同构（XWidget.c:7155-7181） | ✅ 已对齐 |
| 12 | hide 分支父布局 invalidate / post（qwidget.cpp:8405-8411） | 同构（XWidget.c:7182-7187） | ✅ 已对齐 |
| 13 | setSizePolicy→updateGeometry + retainChanged 位（qwidget.cpp:10373-10393） | `XWidget_setSizePolicy/Full` **不调 updateGeometry**、无 retainChanged 位（XWidget.c:6356-6372） | ❌ 偏离 |
| 14 | hideChildren 不打 Hidden 位 ⇒ 页内子孙 isHidden()==false | XGui 同口径（`XWidget_isHidden`=显式位，XWidget.c:7057） | ✅ 已对齐 |
| 15 | takeAt 后条目缓存：Qt 无缓存不适用 | `VXLayout_takeAt` 只置 `m_isDirty`，**不清三级缓存**（XLayout.c:322）→ 读旧缓存路径（XLayoutItem_sizeHint_base 命中 -1 哨兵前的旧值） | ❌ 顺手修（置脏同时清缓存） |

> 更正记忆文件 `xgui-layout-updategeometry-qt-deviation` 的两处旧结论：
> ①「XLayout_activate 需补可见性守卫」——闸已在 LayoutRequest 消费端
> （XWidget.c:5381），与 Qt 落点一致，activate 本身不该加；
> ②「XLayout_invalidate 缺失效沿链自动上推」——XLayout.c:251-270 已实现
> activated-walk + 门控投递（2026-10-20），与 qlayout.cpp:971 同构。
> 真缺口是 **XWidget_updateGeometry 的三合取守卫**与**同步激活无可见性闸**。

---

## 4. 本批实施映射（行为目标：Qt 可观察行为一致，缓存不变量自洽）

1. **`XWidget_updateGeometry` 对齐 `updateGeometry_helper`**（XWidget.c）：
   - 拆内部 `xwidget_updateGeometry_helper(self, forceUpdate)`；
   - 三合取守卫：`isHidden && !retainSizeWhenHidden && !RetainSizeWhenHiddenChanged`
     → **真 no-op**（新增内部属性位 `XWidgetAttribute_RetainSizeWhenHiddenChanged=132`，
     192 位位集有空位，130/131 已有非 Qt 内部位先例）；
   - 父通知对齐两分支：父挂布局 → `XLayout_invalidate`（激活交给事件链；
     XGui 保留**同步激活扩展**但加 `XWidget_isVisible(宿主)` 闸，与 LayoutRequest
     消费端同口径——隐藏页宿主不再被同步激活，可见宿主保持既有同步语义）；
   - 自身布局激活同样加可见性闸（原逻辑对 `self->m_layout` 早退且跳过父通知，
     对齐后父通知不再被早退吞掉）；
   - 删除沿父控件链的逐级失效 walk：跨控件缓存一致性改由第 3 条承担。
2. **`VXLayout_invalidate` 上推收口**（XLayout.c）：activated-walk 合并为
   「缓存清 + activated 清 + 跨控件边界沿父控件链继续」的单一门控循环；
   LayoutRequest 仍只以**失效源链顶宿主**为接收者投递一次（消费端闸不变）。
   效果：隐藏页 setText → 页宿主布局失效一次即被 activated 门拦住，祖先控件
   布局只清缓存不触发重算，重算留给 show 补偿 / resize / 下次 activate。
3. **`XWidget_setSizePolicy/Full` 对齐 Qt**：策略变化调 `XWidget_updateGeometry`；
   retain 位翻转期间置 `RetainSizeWhenHiddenChanged` 位，过后清位。
4. **布局 isEmpty 对齐**：`VXLayout_isEmpty` 改 itemAt 扫描（全空才空），
   删除 XBoxLayout/XGridLayout 的重复覆盖（Qt 结构：三子类都不覆盖）。
5. **`VXLayout_takeAt`**：置脏同时清三级缓存（堵读旧缓存路径）。
6. **show 补偿不新增**：XWidget_setVisible show 分支已同构（对照表 #11）。

### 风险与回归面

- updateGeometry 是全类控件共享热路径：setText/setMinimumSize/setSizePolicy/
  各控件内部尺寸推送（XLabel 推送式 sizeHint 等）全部经过。可见宿主的行为
  保持（同步激活保留+闸只拦不可见宿主），预期差异集中在隐藏宿主——这正是
  本批目标；构造期（父未 show）控件此前会被同步激活、现在不会，几何改由
  show 补偿——若有控件依赖「构造期即有几何」将在此暴露。
- 失效传播语义变更 + isEmpty 语义变更：布局/几何相关用例需全量重跑
  （XGuiRegression_Test 全量门禁）；Windows/Ninja 公共头依赖缺失，
  **验证前必须全量重编**。

---

## 6. 验证记录（2026-10-31）

- 新增用例（xgui_regression_test.c，注册于布局用例组）：
  - `test_layout_updategeometry_qt_align`：隐藏控件 updateGeometry 真 no-op、
    retainSizeWhenHidden 翻转开/关与保持三态通知、隐藏期 setMinimumSize
    不失效不位移、show 补偿后按新约束重排（父宽 110=c0 80+c1 30 精确断言）；
  - `test_layout_invalidate_propagation`：跨控件上推（叶子约束变化失效祖先
    控件布局缓存、可见宿主同步激活、祖先补激活后干净）、布局 isEmpty 的
    itemAt 扫描口径（全空才空 + 子布局递归）、takeAt 清三级缓存。
- **断言可观测性教训**：updateGeometry 的同步激活在父可见时立刻清脏位，
  「是否失效」的 dirty 位断言必须把父控件隐藏（dirty=1=通知过 / dirty=0=
  no-op，可区分）；盒布局 addWidget 后惰性置脏（Qt 同口径）。
- 门禁：`Tools/windows/quiet_regression.bat` → **REGRESSION_EXIT=0**（增量
  与 `ninja -t clean` 后全量 995 文件重编各跑一遍，均全绿，含既有全部用例
  与五个新断言）。
- 全测试矩阵（12 个 *_Test 全目标重建后逐个跑）：9 个 exit=0（含门禁
  XGuiRegression、RemoteInputView、GestureRecognizer、Industry×2、
  LineControl、ScrollTouchProbe、WiFiParsers、UpgradeE2E 复跑前）；2 个为
  **既有失败**（`git stash` A/B 实证与改动无关、输出逐字相同）：
  FtpE2E_Test（FTP prelogin 契约 error=3）、XUpgradeE2E_Test（22/23，
  「策略时机」契约）；2 个为调用形态问题（XGuiRemoteDemo_Test 需
  --server/--client 双进程参数、XGuiWindowDemo_Test 为常驻 demo 裸跑超时）。

---

## 7. 板端（RK3568）A/B 验证（2026-10-31）

为量化「隐藏页布局重算 elimination」，给库加了 **[lprof] 布局画像**（诊断，
`XGPU_W_FRAME_PROF=1` 同门控、挂在既有 3 秒画像窗：`act`=activate 真重解算
次数 / `skip`=幂等早退 / `inval`=失效事件数 / `solveAvg`=单次真重解算平均
耗时；门关零计数，随本批改动一起编译，交叉部署 `--worktree --deploy` 免提交）。

场景（与 2026-10-30 可见性闸批次同配方）：`--page 17`（主站页构建即连
192.168.0.12:502 轮询）→ 3s 后 `--switch-page 18`（前台从站页，主站页转
隐藏）+ CPU 采样（top -b -d 3）。基线 = 部署旧二进制
`XGuiWindowDemo_Test.bak-prelayoutfix-1031`（md5 846edb10，含可见性闸、
不含本批布局修复）。

| 场景 | act（真重解算） | solveAvg | CPU 稳态 |
| --- | --- | --- | --- |
| 新·隐藏页·轮询 1Hz | 3Hz（HUD 基线） | 0.06ms | 3.3% |
| 旧·隐藏页·轮询 1Hz | （无计数） | — | 3.3% |
| **新·隐藏页·轮询 20ms（50Hz）** | **3Hz——与轮询完全解耦** | 0.05ms | **13.9%** |
| **旧·隐藏页·轮询 20ms（50Hz）** | — | — | **17.3%** |
| 新·**前台页**·轮询 20ms | **58Hz ≈ 轮询率** | **0.34ms** | 19.5% |

结论：

1. **机制证明（同二进制自对照）**：50Hz 轮询下隐藏页 `inval≈500-650/3s`
   （每拍 12-13 笔失效）但 `act` 恒 3Hz（HUD 基线）——失效全部被
   activated 门控+可见性闸吞掉；同二进制切回前台页 act 立即 ≈58Hz 且
   solveAvg 跳到 0.34ms（页级布局全量重解算在 RK3568 上的单价）。
2. **收益**：50Hz 轮询隐藏页 CPU 17.3%→13.9%（**-3.4pp ≈ -20% 相对**）；
   节省量与理论值（58Hz×0.34ms≈2%+跨控件链）同量级。1Hz 档两侧无差
   （0.34ms×1Hz 不可见，符合「不是大头」预期）。
3. **渲染链不变**：rprof 三场景逐字相同（frm=6，归因 DemoWin HUD）——
   本批与可见性闸批次正交，渲染侧零回归。
4. 板端收尾：run_test.service 已恢复 active，demo 正常运行。

---

## 8. 残留微偏差收口（2026-10-31 第二批，①②③ 全部落地）

研究期记录的三处微偏差已顺手收掉（门禁全绿 + 板端重部署冒烟通过）：

1. **定尺外门**（qwidget.cpp:10514）：`xwidget_updateGeometry_helper` 补
   `forceUpdate || 非定尺` 外门——定尺控件（min==max 两轴）的提示变化
   不通知父布局；`setMinimumSize/setMaximumSize` 按 Qt 口径传「改后是否
   定尺」作 forceUpdate（qwidget.cpp:3984/4043），setFixedSize 经 min/max
   路径自动获得同口径；show 分支改走 `helper(true)`（qwidget.cpp:8340
   同形，父通知两分支含「父无布局且可见 post LayoutRequest」此前缺失的口径）。
2. **enabled 早退**（qlayout.cpp:1000/523）：`XLayout_activate` 补
   `if (!m_enabled) return false`（先于 parent 检查）；LayoutRequest
   消费端补 enabled 拦截（widgetEvent 同位）。
3. **几何快照复位**（qlayout.cpp:471 `d->rect = QRect()`）：
   `VXLayout_invalidate` 复位 `m_base.m_geometry`——失效后 geometry()/
   contentsRect() 为空直到下一次 setGeometry/activate。既有测试
   `test_layout_total_extended` 的 unset-contentsRect 断言按 Qt 模型修正
   （失效后重设几何再读）；XStackedLayout 切页回贴经分析安全（宿主可见时
   show 链先重激活写回新鲜几何；宿主隐藏时回贴跳过、show 补齐）。

新增断言：定尺外门三态（定尺不通知/解除定尺通知/定尺 setMax 强制通知）、
enabled activate 早退、invalidate 几何复位与恢复。板端重部署后冒烟：
构建期 act=358（solveAvg 0.425ms）→ 切页后稳态 act=9/frm=6，切页几何无伪影。

---

## 9. 纯异步定稿（2026-10-31 第三批，同步激活扩展移除）

用户裁定「updateGeometry 出现同步是有问题的」——移除第一批保留的同步激活
扩展（旧 ④），updateGeometry 达成与 Qt 的**完全同口径**（含此前的有意偏离
一并消除）：

1. `xwidget_updateGeometry_helper` 删除同步激活段：只做隐藏守卫/定尺外门/
   父通知两分支，几何一律经 `invalidate→门控 LayoutRequest→消费端
   （VXWidget_event 可见性闸）→activate` 的事件链下一拍落地；
2. 窗口态自身布局重算由 Qt 自有的两个同步点承接：show 分支
   （qwidget.cpp:8349 layout->activate() + 可见父链 walk）与 resize 路径
   （widgetEvent Resize→activate）；
3. 当拍需要几何的调用方改**显式激活**（对标 QLayout::activate() 公开
   用法）：XDialog exec/open 的 CSD 避让收尾两处（XWidget_updateGeometry →
   `if (m_layout) XLayout_activate(...)`，行为不变）；XMenu 五处
   updateGeometry 经核实**无布局**（菜单自管条目几何），④ 对其本就空转，
   无需改；
4. 测试改纯异步模型并补**端到端异步契约**：可见控件约束变化当拍几何不变
   → `processEvents` 泵后落地（LayoutRequest 消费端激活）；跨控件用例同改
   （失效未激活 → 泵 → 消费端激活）。**测试基建教训**：LayoutRequest 是
   队列事件，`xwidget_postLayoutRequest` 在无应用实例时按既有契约丢弃——
   布局异步测试需先 `layout_test_ensure_app()`（惰性创建 XCoreApplication，
   收尾释放，先于其它创建 app 的用例避免单例冲突）。

板端复测（50Hz 隐藏页轮询）：act 9-11→**5-7**/3s、skip 8-10→**0**
（updateGeometry 零激活调用，剩余激活全来自消费端）、inval 不变、
CPU 13.9%→**13.2-13.5%**、rprof 逐字相同（渲染零回归）；全测试矩阵
8 目标复跑全绿。至此本课题在 XGui 侧无任何已知偏离（同步激活已除名）。

---

## 10. 冗余扫描与同名错标修复（2026-10-31 第四批）

对布局体系公开面做「Qt 无的 API」扫描，判据三层：**C 语言必然**（无重载→
`_ex` 后缀、无虚方法→`_base` 调度器、无 d 指针→字段直读）保留；**Qt 无但
有用**（内部基建/推送式 sizeHint 等）保留；**Qt 无且零调用**删除；**同名
不同义**修行为。

### 10.1 删除（Qt 无 + 全仓零调用，4 个）

| API | 说明 |
| --- | --- |
| `XLayout_deleteAllItems` | doc 曾错标「对标 QLayout::deleteAllItems」——Qt 无此接口；条目清理走 releaseItems/takeAt |
| `XLayout_itemForWidget` | Qt 无 itemForWidget；内部唯一调用（setAlignmentWidget）已内联 indexOf 查找 |
| `XLayout_setAlignmentItem` | Qt 无「任意条目」公开重载（qlayout.h 只有 QWidget/QLayout 两个） |
| `XLayout_layoutMargins` | 保护头内部 API，零调用；内容边距读取走 contentsMargins/resolveMargins |

### 10.2 同名错标行为修复（5 处，逐一以 Qt v6.8.0 源码为锚）

| API | 旧行为 | 修复后 | 锚点 |
| --- | --- | --- | --- |
| `XLayout_update` | 「重画所有条目」遍历 | **失效沿布局链门控上推**（置未激活+链顶投递 LayoutRequest）；实现抽出 `xlayout_update_impl` 供 invalidate 复用（对标 `invalidate = 复位 + update()` 结构） | qlayout.cpp:971 |
| ↑ 的两个调用方（XColorDialog/XInputDialog） | 依赖旧重画+清缓存混合语义 | 改 `XLayout_invalidate`——其真实意图是「失效度量缓存后重取 totalSizeHint」，Qt 惯用口令即 invalidate（清缓存+update） | qlayout.cpp:469 |
| `XLayout_activate`（子布局） | 子布局局部重解算 | **上推委托父布局**（子布局从不独立解算）；新增断言：顶层禁用时子布局 activate 经委托同拒 | qlayout.cpp:1002 |
| `XLayout::expandingDirections` 基类 | 继承 XLayoutItem 的 None | **Both**（盒/网格的本类覆盖不受影响） | qlayout.h:80 |
| `XLayout_spacing` | 未设置返回 -1 哨兵 | **解析生效值**（沿父布局链回退，X 无样式→0；Qt 从不返回 -1）；原始值走 m_spacing 字段 | qlayout.cpp:255 |
| `XLayout_setAlignmentWidget/Layout` | void | **bool**（是否命中条目，Qt 语义） | qlayout.h:58-59 |

### 10.3 保留清单（Qt 无但正当，备忘免重复扫描）

`_base` 虚调度器 / `_ex` 后缀重载变体（C 无默认参/重载）/ 结构体字段直读
（无 d 指针）；内部基建 appendItem/insertItemAt/attachWidget/linkItem/
contentsRectForRect/effectiveSpacing（均有调用方）；`XWidget_setSizeHint`
推送式提示（Qt 懒算的 C 框架等价物，XLabel 承重）；`RetainSizeWhenHidden
Changed` 属性位（Qt 私有成员的 C 等价）。

验证：门禁全绿（update 失效契约/委托 enabled 拒/spacing 解析/setAlignment
bool 新断言）+ 矩阵 6 目标全绿 + 板端重部署冒烟（稳态 act=5-7/frm=6 不变）。

---

## 5. 附：隐藏页成本模型（对齐后）

以堆叠布局隐藏页的标签 setText 为例（Qt 口径 = XGui 对齐后口径）：

1. 标签推送 sizeHint（XLabel 控件内文本度量，控件内部成本，与布局无关）；
2. `XWidget_setMinimumSizeHint` / `XWidget_updateGeometry`：标签非显式隐藏
   （页面隐藏不打子孙 Hidden 位）→ 守卫放行 → **直接父布局 invalidate 一次**
   （清页宿主布局缓存 + activated 链上推 + 跨控件清祖先缓存，O(链长)）；
3. 同步激活被宿主可见性闸拦下；LayoutRequest 投递到页宿主，消费端同样被闸拦下；
4. 页宿主布局 activated=false ⇒ **后续 setText 连 invalidate 上推都在门处即停**
   （Qt 2.1 门控语义），仅剩 O(1) 的缓存清与标志位；
5. 页面 show：wasHidden → 失效父布局 + 自身激活 + 可见父链激活（对照表 #11），
   一次性补齐全部陈旧度量。
