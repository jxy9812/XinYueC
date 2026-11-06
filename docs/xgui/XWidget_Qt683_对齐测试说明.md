# XWidget × Qt 6.8.3 对齐专项测试说明

- 套件：`Test/XGuiTest/XWidgetQtAlignTest.c` + `XWidgetQtAlignTest.h`
- 接线：`Test/CMakeLists.txt` 的 `XGuiRegression_Test` 源清单；
  仓库根 `xgui_regression_test.c` 的 `test_xgui_widgets()` 以
  `expect_true(XWidgetQtAlignTest_runAll(), "XWidget Qt6.8 对齐专项")` 入门。
- 运行：`cmake --build build -j 8 --target XGuiRegression_Test`
  后在仓库 `bin/` 目录执行 `./XGuiRegression_Test`；失败断言以
  `[XWQ-FAIL]` 前缀输出，组内任一失败即该入口返回 false。
- 定位：**增量回归锁**。不与 `xgui_regression_test.c` 的
  `test_widget_contract()`（XWidget 通用契约：几何钳位/属性位/焦点进出/
  close/copy-move）重复，也不与 `Test/XGuiTest/XWidgetApiTest.c`
  （独立 ctest 专项：新 API NULL 冒烟、rect/contentsRect 不变式、
  focusNextPrevChild/setVisible 虚槽覆写点、ensurePolished 链、winId
  转发）重复。本套件只锁本轮 Qt 6.8.3 对齐战役**新增/修正**的语义面。
- 断言总数：**174 条**（`qta_expect` 计数：几何 30 / 可见性与窗口状态 29 /
  焦点与可用性 27 / 树与坐标 12 / 绘制闭环 17 / 外观与动作 38 / 命名契约 21）。

## 一、语义分组与断言条数

### ① 几何与尺寸约束（30 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| baseSize 默认 (0,0)，"未设置"与"设置为 0"同义 | qwidget.cpp:3896-3901（behavior_diff#18） | 默认值、setBaseSize 往返、setSizeIncrementSize 重载等 5 条 |
| normalGeometry 三分支：非窗口空矩形 / 普通态返当前几何 / 特殊态返记忆值 | qwidget.cpp:3693-3699（behavior_diff#13） | 3 条（分支①②③各一） |
| setGeometry 真变化解除最大化/全屏并失效 normalGeometry | qwidget.cpp:7283-7287（behavior_diff#14） | 2 条（解除最大化 + 记忆值失效后不再返回陈旧值） |
| restoreGeometry「几何先、状态后」恢复次序；损坏数据拒收 | qwidget.cpp:7487 / 7572-7595（behavior_diff#23/#25） | save→restore 普通态往返 2 条、最大化态往返 3 条、拒收面 5 条（NULL/错魔数/截断/非窗口 restore+save） |
| setContentsMargins 派发 Resize + ContentsRectChange | qwidget.cpp:7664-7683（behavior_diff#19） | 2 条 |
| setSizePolicy 两参重载只改两轴、保留 hfw/stretch/retain（登记偏差护栏） | qwidget.h:908-909 对照（behavior_diff#21） | 3 条 |
| sizeIncrement 默认 (0,0) 与重载往返 | qwidget.cpp:3896-3901 | 2 条 |

### ② 可见性与窗口状态（29 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| 构造期 Hidden 规则（X 登记口径：可见父下新建子直接可见） | qwidget.cpp:11018-11019 对照（behavior_diff#26） | 3 条 |
| ExplicitShowHide 属性位记录（**登记偏离**：只置位不复刻同值早退——早退会卡死布局联动，见 `XWidget_setVisibleImpl` 注释） | qwidget.cpp:8300-8301（behavior_diff#36） | 3 条 |
| setWindowState(Minimized) 清 Active；同值早退不重复派发 | qwidget.cpp:2963-2966（behavior_diff#35） | 2 条 |
| showNormal/showMaximized/showFullScreen/showMinimized 位运算保留其余状态位；showMinimized 清 Active | qwidget.cpp:2869-2880 / 3052 / 3080-3088（behavior_diff#34） | 4 条 |
| activateWindow 只请求激活、从不改 windowState | qwidget.cpp:12965-12971（behavior_diff#33） | 1 条 |
| isModal 读自身 windowModality；isWindowModal 读顶层 | qwidget.h:818-819（behavior_diff#31） | 4 条 |
| setWindowModality 同步置 WA_ShowModal / WA_SetWindowModality | qwidget.cpp:2825-2826（behavior_diff#32） | 2 条 |
| childrenRect/childrenRegion 按显式隐藏位（isHidden）过滤：父不可见但子未显式隐藏仍计入 | qwidget.cpp:3721（behavior_diff#24） | 3 条 |
| setUpdatesEnabled 递归非窗口子控件；窗口型子控件不随父级冻结 | qwidget.cpp:1824-1839（behavior_diff#83） | 4 条 |

### ③ 焦点与可用性（27 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| **setFocus 对 NoFocus 策略控件仍然生效**（只检查 isEnabled） | qwidget.cpp:6537 | 1 条 |
| setFocus_2 焦点原因经 FocusIn 事件载荷透传（无参版恒 Other） | qwidget.cpp:6537 起 | 2 条 |
| **setTabOrder 对 NoFocus 任一方直接不建链** | qwidget.cpp:7021-7024 | 2 条 |
| setTabOrder remove-then-insert 链卫生：反向重挂清理旧链另一半、防 2 环（全局 take/insert 归专项，此处锁单跳不变量） | qwidget.cpp:6996-6998（behavior_diff#55 落地面） | 4 条（读公开结构字段 m_focusNext/m_focusPrev） |
| **焦点链遍历跳过 NoFocus 与显式隐藏控件** | qapplication.cpp:1970-2025 | 2 条 |
| 禁用持焦控件先顺延到下一可聚焦控件、失败才 clearFocus | qwidget.cpp:3400-3404（behavior_diff#60） | 1 条 |
| 禁用祖先递归处理后代焦点：焦点不残留在已禁用后代 | qwidget.cpp:3400-3411（behavior_diff#68） | 1 条（含 `XWidget_appFocusWidget()==NULL`） |
| setFocusPolicy 沿代理链同步策略位 | qwidget.cpp:7864-7865（behavior_diff#51） | 3 条 |
| 换父后按新父 isEnabled/updatesEnabled 传播（尊重 ForceDisabled） | qwidget.cpp:10826-10831（behavior_diff#81） | 2 条 |
| 换父跨窗口：焦点控件是被移控件后代时显式 clearFocus | qwidget.cpp:10775-10776（behavior_diff#88） | 2 条（本套件抓出该实现为死代码，已修，见「三」） |
| 换父可见性：已生效可见控件换父先不可见、调用方显式 show 恢复；不可见父下 show 不生效可见 | qwidget.cpp:11018-11019 对照（setParent 头注释契约） | 3 条 |

### ④ 控件树与坐标映射（12 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| childAt 跳过 WA_TransparentForMouseEvents 控件（命中其下方兄弟） | qwidget.cpp:10491（behavior_diff#70） | 2 条 |
| 子控件遮罩排除命中点时 continue 试下一个兄弟（不再误返遮罩外控件） | qwidget.cpp:10501-10502（behavior_diff#71） | 2 条 |
| WA_MouseNoMask：带遮罩控件按整矩形命中 | qwidget_p.h:898-900（missing_api#26） | 1 条 |
| isAncestorOf 不跨顶层边界、控件不是自身祖先 | QWidget::isAncestorOf 契约 | 3 条 |
| mapToGlobal 沿父链累加原点 + 顶层原点；mapFromGlobal 互逆；mapTo/mapFrom 互逆（无句柄路径，确定性口径） | QWidget::mapToGlobal 族 | 4 条 |

### ⑤ 绘制闭环（17 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| visibleRegion 可见性闸：不可见控件返回空区域 | qwidget.cpp:1889-1890 / 8720-8722（behavior_diff#76） | 2 条 |
| visibleRegion/update 脏区锚点为控件**全幅** rect()（含内容边距时边距带必须入区域/脏区） | qwidget.cpp:8716-8722 / 11257-11260（behavior_diff#12/#84 根修面） | 4 条（visibleRegion 全幅锚点 1 + update 脏区角点 2 + 顶层入脏 1） |
| setMask_2(XBitmap)：置位像素构成遮罩；空位图等价清除；遮罩随 childAt 生效 | qwidget.cpp:13193 + qregion.cpp:3851-3858（missing_api#20/#29） | 6 条 |
| grab_2 按矩形子区抓图；越界/负原点源矩形拒绝 | qwidget.cpp:5404-5405 对照（missing_api#22） | 3 条 |

### ⑥ 外观与动作（38 条）

| Qt 语义 | Qt 6.8.3 位置 | 断言 |
| --- | --- | --- |
| cursor() 沿父链上溯、unsetCursor 后回落默认箭头；setCursor/unsetCursor 各派发 CursorChange | qwidget.cpp:4980-4982 / 5013-5018（behavior_diff#113） | 4 条 |
| setToolTip 派发 ToolTipChange | qwidget.cpp:11722（behavior_diff#107） | 1 条 |
| setWindowModified 派发 ModifiedChange 并翻转标志 | qwidget.cpp:11678（behavior_diff#30） | 2 条 |
| setWindowIconText 派发 IconTextChange | qwidget.cpp:6115（behavior_diff#93） | 1 条 |
| setWindowIcon **无条件**发射 windowIconChanged（同值/置空也发射）+ 每次派发 WindowIconChange | qwidget.cpp:6228（behavior_diff#39） | 3 条 |
| setWindowTitle：变化发射；同值非空早退；空/null 每次都发 | qwidget.cpp:6132-6149（behavior_diff#46） | 5 条（事件+信号成对断言） |
| setBackgroundRole 派发 PaletteChange 并向子控件传播；backgroundRole 沿父链上溯、自身角色优先 | qwidget.cpp:4388-4397 / 4421 / 4486（behavior_diff#96/#98） | 4 条 |
| 动作列表：addAction 入列 + ActionAdded；同一动作去重；insertAction(before) 次序；**动作销毁自动摘除（destroyed 连接，UAF 防护）**；removeAction + ActionRemoved；addAction_2 新建动作以控件为父对象 | qwidget.cpp:3139-3215（behavior_diff#90/#91） | 9 条 |
| contextMenuPolicy 四策略分派：Default 走 contextMenuEvent / Prevent 抑制 / Custom 发信号 / Actions 无动作忽略、有动作发信号；构造默认 DefaultContextMenu | qwidget.cpp:9150-9173（behavior_diff#92） | 7 条 |
| graphicsEffect 借用契约：set/_ref 语义一致、重复设置替换旧效果、置空清除 | qwidget.cpp 对照（naming#11/#12） | 4 条 |

### ⑦ 命名契约（21 条）

| 契约 | 依据 | 断言 |
| --- | --- | --- |
| `_const` 借用版与拷贝版成对、值随 set 变化、零释放：mask_const / visibleRegion_const（在⑤内）/ childrenRegion_const / layout_const | 《代码风格…》获取函数所有权契约（naming#10/#13） | 5 条 |
| setLayout 借用版摘挂后所有权仍归调用方；**setLayout_ref 接管的布局随控件销毁释放**（系统分配器统计口径，8 宿主放大窗口，泄漏残差≈11KB 判据） | qwidget.cpp:10284/10322（behavior_diff#78） | 4 条（本套件抓出原实现无效，已修，见「三」） |
| `_utf8` 版与 XString 版等价（setWindowTitle_2 内容一致；naming#15 临时口径） | 字符串 API 后缀契约 | 2 条 |
| `_2` 重载族：setParent_2 换父 / setParent_2(NULL) 脱靠补 Window 位；grabMouse_2 抓取 + 抓取期套光标 + releaseMouse 复位（WA_SetCursor 位）；setFocus_2 原因透传（在③内）；setContentsMargins_2（ApiTest 已锁）；setMask_2（在⑤内） | naming#1/#4/#17 | 5 条 |
| updateMicroFocus 冒烟（无输入法实例环境安全；IME 生效路径不可观测） | missing_api#8 | 冒烟调用（不计断言） |

## 二、断言有效性（负向验证）

- 纪律要求断言必须真的能失败，两条途径均已执行：
  1. **真实反例**：开发过程中 15 条断言在实现缺陷真实存在时报红（首轮
     运行 `[XWQ-FAIL]` 15 条），随缺陷修复逐条转绿——失败检测能力已被
     真实反例验证（非恒真断言）。
  2. **故意反例**：将「baseSize 默认 (0,0)」的期望值改为 1 后复跑，该断言
     如期报 `[XWQ-FAIL] baseSize 默认 (0,0)…`、套件入口返回 false；随即
     恢复正确期望并复跑转绿（最终入库版本为正确期望值）。

## 三、本套件抓出并已修复的实现缺陷

1. **behavior_diff#88 换父清焦为死代码**（`Src/XGui/Widget/XWidget.c`
   `XWidget_setParent`）：旧顶层在 `XObject_setParent` 落账**之后**才取
   `XWidget_topLevel(g_focusWidget)`，此时焦点控件的顶层已是新顶层，
   `oldTop != newTop` 永假，清焦分支从不触发。修复：重挂父前先捕获
   `focusOldTop`，判据改用该旧值。抓出断言：「换父跨窗口：被移子树内的
   焦点被显式清除」。
2. **behavior_diff#78 setLayout_ref 所有权接管无效且不安全**
   （`XWidget_setLayout_ref`）：原实现对布局对象调
   `XObject_setParent(layout, self)`，但 `XLayoutItem` 仅内嵌 `XClass`
   （非完整 XObject：无 m_parent/m_children/m_threadData 字段），该调用
   在线程亲和守卫处静默早退（实测独立探针 parent 恒 nil）→ 接管从未
   生效（布局泄漏）；且守卫一旦变化即向 XLayout 自有字段越界写。修复：
   撤销该调用，改为模块内「接管布局登记表」（`xwidget_ownedLayoutRegister/
   Remove/TakeFor`），`VXWidget_deinit` 析构时释放登记的布局。抓出断言：
   「setLayout_ref 接管的布局随控件销毁释放（deinit 置旗）」——断言以
   派生守门布局（QtaCanaryLayout）的 deinit 置旗为观测点（系统分配器
   字节统计受 postEvent 队列等惰性初始化干扰，不可靠），并带「借用布局
   不随宿主销毁」对照组。

## 四、仍未覆盖的已知缺口

| 缺口 | 原因 | 归属 |
| --- | --- | --- |
| setMask 缩小后「让出区域」并入**父级** update（behavior_diff#75） | 需 PAINT 级区域捕获（父子各自脏区在顶层汇合后不可分辨），本轮未建绘制捕获夹具 | 后续绘制专项 |
| grab 的 IgnoreMask **像素级**验证（behavior_diff#86） | 需 paintEvent 填充探针 + XImage 像素读回；本套件只锁 grab_2 尺寸/拒绝契约 | 后续绘制专项 |
| adjustSize 的 hfw 高度改写与 expanding 托底 200×100（behavior_diff#16） | 需布局夹具；demo apitest（XGuiWindowDemo_Test）已覆盖，回归套件不重复 | 已由 demo 覆盖 |
| 焦点链全局 take/insert 语义、per-window focus_child 记忆（behavior_diff#55 全局链 / #57） | 定裁留专项；本套件仅锁单跳链卫生不变量（读 m_focusNext/m_focusPrev，专项改造后此组断言需同步修订） | 焦点专项（XGui.md §8.2.2） |
| ExplicitShowHide 同值早退（behavior_diff#36） | **故意偏离**（早退卡死布局联动，代码注释已登记）；本套件只断言属性位记录语义 | 已登记偏差（XGui.md §8.1.1） |
| updateMicroFocus 的 IME 实效路径 | 无头环境无输入法实例，只做签名/安全冒烟 | 输入法专项 |
| XAccessible_help 未接 AT-SPI D-Bus 桥 | 批次3 遗留（平台层文件属并行会话脏区） | 后续批次 |
| restoreGeometry 平台窗口真实回写、多屏 2/3 上限（behavior_diff#16 屏幕分支） | 无头环境无 WM/多屏；屏幕上限依赖控件级 screen（定裁 C，随 screen 专项） | screen 专项 |
| 仓库既存红 2 条（[XKB-FAIL] 紧凑几何 / XKeyboard 控件功能） | 各批次对照基线均相同，既存红非本轮引入 | XKeyboard 专项 |

## 五、复现命令

```bash
cd <repo>
cmake --build build -j 8 --target XGuiRegression_Test
cd bin && ./XGuiRegression_Test        # 工作目录必须是仓库 bin/
# 本套件入口输出：XWidgetQtAlign test: PASS（或 N assertion(s) failed）
```
