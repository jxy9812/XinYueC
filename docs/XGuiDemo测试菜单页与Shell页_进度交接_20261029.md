# XGuiDemo「测试菜单页 / Shell 页」进度与剩余任务交接（2026-10-29）

> 用途：换机/换人接手。本文记录本轮 XGuiDemo 新增两页（控制台 test 菜单 GUI 化 +
> 页面内 Shell 控制台）的**已完成部分、已定位根因、未收口问题**，以及并行会话
> 在同一工作区的在途改动（提交前必须先收口）。
> 阅读顺序：本文 → `Test/XGuiDemo/xgui_demo_page_testmenu.c` /
> `Test/XGuiDemo/xgui_demo_page_shell.c` / `Src/XGui/Widget/XWidget.c`（框架修复三处）。

---

## 0. 一句话状态

- **两页已建好并渲染正确**：页 22「测试菜单」、页 23「Shell」，导航归入**新增的第四
  一级分类「测试」**（原先误放在「远程」下已迁出）；横竖屏双档截图目验通过。
- **点击崩溃已修复**（三处 use-after-free，valgrind 铁证定位）。
- **点击后空白**：可见性维度已修（框架自动可见，vis=1）；**几何维度未收口**——
  第三层两个按钮 rect 都是 (0,0 404x40)，即叠在原点（详见 §4）。
- 两页 autotest 均为 **PASS**（含编程式点击回归锁），但页 22 因探针残留断言 FAIL。

---

## 1. 已交付内容

### 1.1 页面与导航

| 项 | 位置 | 说明 |
| --- | --- | --- |
| 测试菜单页 | `Test/XGuiDemo/xgui_demo_page_testmenu.c` | 页索引 22（exti17）；控制台 `test` 菜单树 GUI 化 |
| Shell 页 | `Test/XGuiDemo/xgui_demo_page_shell.c` | 页索引 23（exti18）；页面内 XConsoleShell |
| 页面声明 | `Test/XGuiDemo/xgui_demo_pages.h` | build/autotest/adapt 三个契约 |
| 导航接线 | `Test/XGuiDemo/xgui_window_demo.c` | 扩到 24 页/19 扩展槽；**新增第四分类「测试」**（`kNavTestItems`）；`DEMO_NAV_CAT_N` 3→4 |
| CMake | `CMakeLists.txt` | demo 目标加两个页源文件；非嵌入式构建把 `TEST_FILE` 整棵测试树链进 demo（菜单树来源） |

功能要点：
- 测试菜单页：子菜单进/返回上级导航；动作点击即运行（`XPrintf_outputPush` 线程级
  捕获打印 → XLabel 展示）；横屏 ≥600px 双栏（菜单|输出）、竖屏 <600px 单列。
- Shell 页：「开启/关闭」按需创建销毁 Shell 实例（内存只在开启期间占用）；输入框
  回车/发送执行命令（挂 `XTestCommand`，`Test <name>` 可用）；`clear`/`cls` 清屏
  并释放缓冲；输出区 XLabel 展示。

### 1.2 按需内存（用户要求）

- 两页都走扩展页懒分配（首显才建树与控件）。
- Shell 实例仅在「开启」期间存在，关闭即销毁。
- 输出缓冲 64KB 起按需分配、256KB 封顶，`clear` 时释放回系统。

### 1.3 横竖屏适配（用户要求）

- 测试菜单页：根控件**子类**（`DemoTestMenuRootWidget`）挂 `resizeEvent` →
  `tm_applyWidthTier()` 按根宽翻中栏方向（宽≥600 双栏 / 窄<600 单列），翻档后
  `XLayout_invalidate` + `XLayout_activate` 立即落几何。
  依据：views 页同款结论——堆叠几何自动重激活的路径不经过 adapt 契约漏斗，
  翻档必须由根 resizeEvent 兜住（`Test/XGuiDemo/xgui_demo_page_views.c`）。
- Shell 页：单列竖排天然双档通吃，adapt 为契约空操作。

---

## 2. 点击崩溃（已修复，三处 UAF）

valgrind 定位（`Invalid read of size 8` + free 栈），根因与修法：

| # | 根因 | 修法 |
| --- | --- | --- |
| 1 | 翻层用 `XScrollArea_setWidget` 换容器，其实现**同步 `XClassDelete` 旧树**（对标 QScrollArea），点击派发栈内的按钮被释放 | 按钮容器持久只建一次；翻层时旧按钮逐个「`XLayout_removeWidget` 摘出布局 + `XObject_deleteLater` 延迟释放」 |
| 2 | 用 `XLayout_removeItem` 传控件指针清理旧按钮——该 API 要的是**布局条目对象**，传控件等于没删，释放后的按钮仍挂布局 → 下一拍几何计算读已释放内存 | 改用 `XLayout_removeWidget`（仓库 `xgui_demo_page_network.c` 同款用法） |
| 3 | `XTestMenu_getActions` 返回菜单**内部向量的借用指针**（`const XVector*`），我却 `XClassDelete` 了它 → 下次进同一层读已释放内存 | 只删 `getMenus` 返回的副本（新建、调用方拥有），`getActions` 借用不释放 |

> 教训：`XTestMenu_getMenus` = 新建副本（可删）；`XTestMenu_getActions` = 借用
> 指针（**不可删**）。控制台老代码 `Test/XTestMenuTest.c` 本就只删 menus，是对的。

---

## 3. 框架修复（已落地，`Src/XGui/Widget/XWidget.c`）

对标 Qt 6.8.3 源码（`D:/Qt/6.8.3/Src/qtbase/src/widgets/kernel/`）：

1. **`XWidget_init` 挂父后自动可见**（对标 Qt `QWidget::setParent`：
   `if (parent && parent->isVisible() && !q->testAttribute(WA_WState_ExplicitShowHide)) q->show();`）
   —— 父已可见时创建的子控件立即可见。本框架 `m_explicitShow` 默认 0 且此前**无任何
   自动 show 路径**：父若早已 show 过（动态建控件的常态），新控件永远不可见。
2. **`XWidget_setParent` 换父后同样自动可见**（同款语义，显式隐藏位 `WA_WState_Hidden`
   对位 Qt 的 `WA_WState_ExplicitShowHide`）。
3. **show 后失效父布局**（对标 Qt `QWidgetPrivate::show_helper` 会 invalidate 父布局）
   —— 控件从「隐藏（布局条目 empty、不占主轴空间）」转可见后若不重排，几何仍按
   空条目分配。

> 三处修复已落在 Windows 侧工作区（与并行会话的手势改动叠加在同一文件的不同位置）。
> **页面层曾临时加过逐个 `XWidget_show`**（框架修复后已冗余但无害，与仓库其他页面
> 风格一致，可保留）。

---

## 4. 未收口：第三层按钮几何叠在原点

### 4.1 现象（探针实测，页 22 进入「库 → 查询算法」）

```
[TM3] level3: items=2 path='库 / 查询算法'
[TM3] layCount=2 hostGeom=(0,0 412x552) hostRect=(0,0 412x552)
[TM3] btn0 vis=1 rect=(0,0 404x40)
[TM3] btn1 vis=1 rect=(0,0 404x40)     ← 应为 (0,44 404x40)
```

- 布局本身有效：`layCount=2`、容器几何 412x552 正常；
- 按钮**可见**（框架修复生效，vis=1）；
- 但两条目主轴位置相同（y 都是 0）→ 视觉上重叠/只见其一。

### 4.2 已排除的假设

- 非 `setWidget` 换容器问题（已改持久容器）；
- 非 `removeItem` 误用（已改 `removeWidget`）；
- 非「条目被判 empty」——按钮几何已被应用（404x40 = 交叉轴铺满 + 固定高度 40），
  而 `VXWidgetItem_setGeometry` 对 empty 条目是**直接 return 不改几何**的；
- 非 `qGeomCalc` 缺 pos 赋值——本框架 `XBoxLayout.c:571-580` 有统一收尾：
  ```c
  int p = pos + extra;
  for (i = start; i < start + count; ++i) {
      data->pos = p;
      p += data->size;
      if (!data->empty) p += effectiveSpacer(...) + extra;
  }
  ```
  → **pos 会被累加**，两个条目 pos 应为 0 与 44。

### 4.3 下一步怀疑点（按优先级）

1. **`data->size` 是否为 0**：若 `XBoxLayout_smartSizeHint(g)`（`stretch>0 ? min : hint`）
   返回 0，则 `p += 0` → pos 不推进（两按钮同 y）而控件高度来自
   `maximumSize`/`minimumSize` 夹取（=40）。**建议探针打印按钮的
   `XWidget_sizeHint` / `XWidget_minimumSizeHint` / `XWidget_maximumSize` 与
   `XLayoutItem_sizeHint_base`**。
   候选解释：按钮 `sizeHint` 在「父可见但尚未布局/未 show」时序下返回 0
   （对照 `XWidget_sizeHint` 里 `self->m_layout` 链与 `XLayout_totalSizeHint` 的
   递归条件）。
2. **`XLayoutItem_sizeHint_base` 缓存**：条目若缓存了创建瞬间（控件未布局时）的
   sizeHint，后续不再刷新 → 需查 `XLayoutItem` 是否缓存及失效时机
   （对照 Qt `QWidgetItem::sizeHint` 每次问 widget，靠 `QWidget::updateGeometry`
   通知失效）。
3. **`XWidget_sizeHint` 的布局链**：按钮的 m_layout 指向 hostLay，`hostLay` 的
   parentWidget 是 menuHost……若这条链在 `XWidget_setLayout` 时序里未接好，
   `XLayout_totalSizeHint` 会返回 0。

### 4.4 复现与验证方式（WSL）

```sh
cd /root/xinyuec-src
cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug -DXINYUE_CROSS_COMPILE=OFF
cd build-native && make XGuiWindowDemo_Test -j8
export DISPLAY=:99
./bin/XGuiWindowDemo_Test --autotest-page 22     # 看 [TM3] 探针输出
```

> 探针在 `demo_page_testmenu_autotest` 里（`[TM3] level3/layCount/btnN` 三条
> XPrintf），**收口后必须删除**。

---

## 5. 测试与门禁现状

- `--autotest-page 22` / `23`：两页 PASS（页 22 在探针加入后因「返回上级」断言
  顺序被探针打乱而 FAIL，收口时需把探针移到断言之后或删除）。
- `--autotest-page` 专项模式已跳过内置页自测（避免 views 页大表格的在途崩溃）。
- **全量 autotest 目前崩溃**：`views` 页大表格 `XTableWidget_setHorizontalHeaderLabels`
  空指针（`XTableWidget.c:984`）——属并行会话「页级滚动壳」在途改造，**不是本轮改动
  引入**（已用 HEAD 纯净版隔离验证：HEAD 不崩）。
- 两页点击回归锁（永久防复发）：
  - 测试菜单页：进子菜单（面包屑压段/按钮重渲染）→ 点返回上级（弹段/回根层）；
  - Shell 页：开启 → `help`（输出入缓冲）→ `clear`（清屏还内存）→ 关闭（实例销毁）。
  - 手法：`XAbstractButton_click` 编程式注入（框架 autotest 同款，不依赖鼠标注入）。

---

## 6. 并行会话在途改动（提交前必须收口）

同一工作区存在另一条会话的在途改造（**我的改动已叠加在其上，提交前需整树复核**）：

| 范围 | 内容 | 冲突风险 |
| --- | --- | --- |
| `Src/XGui/Widget/XWidget.c` | 手势事件体系（新增 `XGestureEvent.h` 等，`Src/XGui/Gesture/` 整目录为**未跟踪新文件**） | **高**：与我的三处框架修复同文件；WSL 编译需同步该目录，否则报缺头 |
| `Src/XGui/Widget/XAbstractScrollArea.*`、`XScrollArea.c` | 页级滚动壳（`demo_ext_stackEntry` 包装器） | 中：影响扩展页堆叠条目 |
| `Test/XGuiDemo/xgui_window_demo.c` | 滚动壳接线（`g_extPageScrollReady` 豁免表等） | 中：与我的扩列/导航改动同文件 |
| `Test/XGuiTest/XGestureRecognizerTest.c`、`XScrollTouchProbe.c` | 未跟踪新测试 | 低 |
| `Tools/posix/cross-compile-rk3506.sh`、`docs/cross-compile-rk3506.md` | 未跟踪新文件 | 低 |

> **WSL 同步纪律**：本轮多次遇到 `/root/xinyuec-src` 被外部覆盖/缺文件。同步须用
> `git archive HEAD` + 工作区改动 + **未跟踪文件/目录**（`tar cf - <dir>`）三段齐全，
> 否则编译会报缺头（如 `XGestureEvent.h`）。

---

## 7. 剩余任务清单

| # | 任务 | 状态 |
| --- | --- | --- |
| 1 | 定位并修复第三层按钮几何叠原点（§4.3 三条怀疑点逐个验） | 待做 |
| 2 | 删除 `[TM3]` 探针与临时断言，恢复页 22 autotest PASS | 待做 |
| 3 | 桌面端交互复测（点击进层/返回/动作运行/Shell 开关与命令） | 待做 |
| 4 | MSVC 重建 `XGuiWindowDemo_Test.exe` 交付用户交互验证 | 待做 |
| 5 | 与并行会话合并在途改动（尤其 `XWidget.c` 同文件冲突）后跑全量 autotest | 待做 |
| 6 | 提交（当前工作区 51 项改动，含两条会话的内容，**不要拆错归属**） | 待做 |

---

## 8. 附：Qt 6.8.3 源码对照位置（本机）

`D:/Qt/6.8.3/Src/qtbase/src/widgets/kernel/`
- `qwidget.cpp`：`QWidget::setParent` 的自动 show 语义（`newParent->isVisible() &&
  !WA_WState_ExplicitShowHide → q->show()`）、`QWidgetPrivate::show_helper` 的
  父布局 invalidate。
- `qlayout.cpp`：`QLayout::removeWidget`（takeAt+delete item+invalidate）、
  `QLayout::activate`/`activateRecursiveHelper`。
- `qlayoutengine.cpp`：`qGeomCalc`（**只算 size 不算 pos**，pos 由摆放循环累加）。
- `qboxlayout.cpp`：`QBoxLayoutPrivate::setupGeom`（几何收集）、`setGeometry`（摆放）。

---

## 9. 收官补记（2026-10-30 换机续役闭环）

§4 第三层按钮几何叠原点与 §7 任务清单已随 35d601ec 收口（尾部可伸展空白
方案落地，autotest 含几何回归锁 g0.y/g1.y-g0.y 断言）；本日另补两件：

1. **[动作运行回归锁]** 原回归锁只覆盖导航（进子菜单/返回上级），
   「运行动作到结束」路径零覆盖（用户实测每项结束即崩的盲区）。现
   autotest 内补永久锁：沿子菜单下行（每层重算父节点判定返回钮）
   到首个叶动作、真实触发到结束、断言存活+输出捕获增长、回锁起始层。
   Linux 实证：二分查找「主测试」跑完 buf 0→34 捕获正常、进程存活、
   扩展页22 autotest PASS。
2. **[全量矩阵]** Test 目录 57 项独立测试+五套件+all 构建全量跑完，
   中断零发生；失败逐条 A/B 定责均为存量/环境（详见
   docs/第二段施工编排方案_1030.md 与提交说明）。
