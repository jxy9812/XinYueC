# Src/XGui/Widget 控件目录 · 详设与注意事项

> 本目录 82 个控件的单文档详设：每类一节，含**定位 / 生命周期 / API 面逐个签名（带 Qt 对标行号与所有权归属）/ 所有权与命名契约 / 可覆写点 / 注意事项与坑点 / 测试覆盖**七项。XWidget 节另含「Qt 6.8.3 对齐状态」与「本役修复记录」两节。
>
> 生成于 XWidget × Qt 6.8.3 全量对齐战役（一轮 179 差异修复 + 二轮 50 条新发现复核）。

**相关台账**：对齐矩阵 `out/xwidget_align/`（六域逐 API）｜测试说明 `docs/xgui/XWidget_Qt683_对齐测试说明.md`

---

## XWidget

**定位**：整个 XGui 控件体系的根基类——把 Qt 6.8 `QWidget` 的公开语义（属性位/窗口标志/几何/尺寸约束/控件树/可见性与窗口状态/焦点/外观/绘制闭环/布局挂接/图形效果/保留层）以 C 适配方式承载，并内嵌 `XWidgetWindow`（`XWindow` 子类，内部实现）桥接平台窗口与事件分发。继承链 `XObject ← XWidget`（`m_class` 为首成员，XWidget.h:616；`XCLASS_DEFINE_BEGING(XWidget)` XWidget.h:517）。头文件 `Src/XGui/Widget/XWidget.h`（**2701 行**，实测 `wc -l`），保护头 `Src/XGui/Widget/XWidget_Protected.h`（421 行），实现 `Src/XGui/Widget/XWidget.c`（**14542 行**）。模块开关 `XWIDGET_ON`；依赖 `XWINDOW_ON`，子能力 `XCURSOR_ON`/`XBACKINGSTORE_ON`/`XGUIAPPLICATION_ON`/`XAPPLICATION_ON`/`XLAYOUT_ON`/`XPALETTE_ON`/`XINPUTMETHOD_ON`/`XGESTURE_ON`/`XGUI_CUSTOM_TITLEBAR_ON` 关闭时对应接口按回退语义退化（XWidget.h:68-71、XWidget.c:51-58）。

**生命周期**：`XWidget_class_init()`（XWidget.c:4928，进程内单例虚表，`XVTABLE_INHERIT_XCLASS(XObject)` 后按序注册 26 个事件槽（PaintEvent…TabletEvent）+ 1 个条件槽（`InputMethodQuery`，`XINPUTMETHOD_ON`）+ 6 个扩展槽（`TouchDragEvent`/`GestureEvent`/`SizeHint`/`MinimumSizeHint`/`FocusNextPrevChild`/`SetVisible`），合计 **33** 槽（XWidget.h:518-572）；类初始化表实读 33 项，XWidget.c:4933-4966）→ `XWidget_init(self, parent, flags)`（XWidget.c:4982；`XMemset` 全零 + `XObject_init` + 置 `m_is_widget=1` + `XClassSetVtable`；无父且未带 Window 位时自动补 Window 位形成顶层；`parent` 非控件（`m_is_widget==0`）一律按无父处理，见 XWidget.c:4988-4996）→ `XWidget_create_ex(memory,parent,flags)`（XWidget.c:5126）／宏 `XWidget_create(parent, flags)`（XWidget.h:795，**两参宏，无零参形态**）。析构走 `VXWidget_deinit`（XWidget.c:5246）：入口先置 `m_deiniting`（抑制析构链重绘回表）、作废父控件占位像素、摘延迟重绘/待激活焦点/装饰注册表、摘焦点链与焦点代理、按接管登记表处理布局、摘应用模态门、销毁桥接窗口、无条件摘顶层登记表。栈对象须与 `XClassDeinit` 成对；堆对象 `XClassDelete`。

**API 面**（按头文件注释分区；签名逐字抄自 XWidget.h，行号为声明行；Qt 列给 `qwidget.h` 行号，无对应写「X 扩展」）

### 1. 生命周期与属性位（XWidget.h:775-871）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XWidget_class_init(void);` | 建/取共享虚表 | QWidget 无公开对应 | 共享表，禁释放 |
| `void XWidget_init(XWidget* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（parent 借用） | `QWidget(QWidget*,WindowFlags)` qwidget.h:213 | parent 借用 |
| `#define XWidget_create(parent, flags) XWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,(parent),(flags))` | 默认内存类型建堆对象 | — | 调用方拥有，`XClassDelete` |
| `XWidget* XWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，`XClassDelete` |
| `void XWidget_setAttribute(XWidget* self, XWidgetAttribute attribute, bool on);` | 置/清属性位并同步便捷字段 | `setAttribute` qwidget.h:629 | — |
| `bool XWidget_testAttribute(const XWidget* self, XWidgetAttribute attribute);` | 查属性位 | `testAttribute` qwidget.h:630 | — |
| `bool XWidget_isWindow(const XWidget* self);` | 窗口型判定（`m_isWindow`） | `isWindow` qwidget.h:232 | — |
| `bool XWidget_isTopLevel(const XWidget* self);` | 已废弃拼写，转发 `isWindow` | `isTopLevel` qwidget.h:230 | — |
| `XWindowType XWidget_windowType(const XWidget* self);` | 窗口类型（flags 低 8 位） | `windowType` qwidget.h:622 | — |
| `XWidgetFlags XWidget_windowFlags(const XWidget* self);` | 窗口标志 | `windowFlags` qwidget.h:618 | — |
| `void XWidget_setWindowFlags(XWidget* self, XWidgetFlags flags);` | 设标志并重算窗口/父子状态 | `setWindowFlags` qwidget.h:617 | — |
| `void XWidget_overrideWindowFlags(XWidget* self, XWidgetFlags flags);` | 仅改逻辑标志，不重建平台窗口 | `overrideWindowFlags` qwidget.h:620 | — |
| `void XWidget_setWindowFlag(XWidget* self, XWidgetFlags flag, bool on);` | 单标志置/清 | `setWindowFlag` qwidget.h:619 | — |

### 2. 几何（XWidget.h:873-1042）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XWidget_x(const XWidget* self);` / `int XWidget_y(...)` | 父坐标系左上角分量 | `x`/`y` qwidget.h:253-254 | — |
| `XPoint XWidget_pos(const XWidget* self);` | 父坐标系位置 | `pos` qwidget.h:255 | 值 |
| `int XWidget_width(...)` / `int XWidget_height(...)` | 宽/高 | `width`/`height` qwidget.h:258-259 | — |
| `XSize XWidget_size(const XWidget* self);` | 尺寸 | `size` qwidget.h:257 | 值 |
| `XRect XWidget_rect(const XWidget* self);` | **控件全幅** (0,0,w,h)，与内容边距无关 | `rect` qwidget.h:260（inline h:890-891） | 值 |
| `XRect XWidget_contentsRect(const XWidget* self);` | rect 扣四边内容边距 | `contentsRect` qwidget.h:534（impl qwidget.cpp:7705-7708） | 值 |
| `XMargins XWidget_contentsMargins(const XWidget* self);` | 内容边距 | `contentsMargins` qwidget.h:532 | 值 |
| `void XWidget_setContentsMargins(XWidget* self, int left, int top, int right, int bottom);` | 设边距；变更派发 RESIZE（可见时）/挂起（不可见）+ CONTENTS_RECT_CHANGE | `setContentsMargins` qwidget.h:530（qwidget.cpp:7628/7664-7686） | — |
| `void XWidget_setContentsMargins_2(XWidget* self, const XMargins* margins);` | 边距对象重载；NULL 等价四参全 0 | `setContentsMargins(const QMargins&)` qwidget.h:531（qwidget.cpp:7658） | margins 借用 |
| `void XWidget_unsetContentsMargins(XWidget* self);` | 清零边距 | X 扩展（Qt 仅 QLayout 有同名） | — |
| `XRect XWidget_geometry(const XWidget* self);` | 父坐标系几何 | `geometry` qwidget.h:250 | 值 |
| `XRect XWidget_frameGeometry(const XWidget* self);` | 框架几何（X 无系统边框，恒等 geometry） | `frameGeometry` qwidget.h:249 | 值 |
| `XSize XWidget_frameSize(const XWidget* self);` | 框架尺寸 | `frameSize` qwidget.h:256 | 值 |
| `XPoint XWidget_framePos(const XWidget* self);` | 框架位置 | X 扩展（Qt 6.8 无 `framePos`，`rg framePos` 在 qtbase/src/widgets 零命中） | 值 |
| `XRect XWidget_normalGeometry(const XWidget* self);` | 正常态几何三分支（非窗口→空；非最大化/全屏→当前几何；否则记忆值） | `normalGeometry` qwidget.h:251（qwidget.cpp:3690-3700） | 值 |
| `void XWidget_setGeometry(XWidget* self, int x, int y, int w, int h);` | 钳位后设几何；真变化时解除最大化/全屏并失效 normalGeometry | `setGeometry(int,int,int,int)` qwidget.h:502（`setGeometry_sys` qwidget.cpp:7249-7291） | — |
| `void XWidget_setGeometryRect(XWidget* self, const XRect* rect);` | 矩形重载 | `setGeometry(const QRect&)` qwidget.h:503 | rect 借用 |
| `void XWidget_updateGeometry(XWidget* self);` | 通知布局；显式隐藏且未保留尺寸时真 no-op | `updateGeometry` qwidget.h:539（`updateGeometry_helper` qwidget.cpp:10506-10520） | — |
| `void XWidget_move(XWidget* self, int x, int y);` / `void XWidget_move_2(XWidget*, const XPoint*)` | 移动（尺寸不变） | `move` qwidget.h:498-499 | — |
| `void XWidget_resize(XWidget* self, int w, int h);` / `void XWidget_resizeSize(XWidget*, const XSize*)` | 改尺寸（位置不变） | `resize` qwidget.h:500-501 | — |
| `void XWidget_setFixedSize(XWidget* self, int w, int h);` / `XWidget_setFixedSizeSize(XWidget*, const XSize*)` | min=max=size | `setFixedSize` qwidget.h:290-291 | — |
| `void XWidget_setFixedWidth(XWidget* self, int width);` / `XWidget_setFixedHeight(...)` | 单轴固定 | `setFixedWidth`/`setFixedHeight` qwidget.h:292-293 | — |
| `void XWidget_adjustSize(XWidget* self);` | 先 polish，再按 sizeHint/childrenRect/hfw 调尺寸 | `adjustSize` qwidget.h:506（qwidget.cpp:8792 起） | — |

### 3. 窗口几何序列化（XWidget.h:1044-1072）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XByteArray* XWidget_saveGeometry(const XWidget* self);` | 仅顶层；文本魔数 `XWG1` + 9 字段（frame、normal、Maximized\|FullScreen） | `saveGeometry` qwidget.h:504（Qt 为二进制） | **拷贝须释放**：调用方 `XClassDelete`（h:1053） |
| `bool XWidget_restoreGeometry(XWidget* self, const XByteArray* geometry);` | 校验后恢复；损坏/截断/非顶层返回 false | `restoreGeometry` qwidget.h:505 | geometry 借用 |

### 4. 尺寸约束与提示（XWidget.h:1074-1238）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XSize XWidget_minimumSize(...)` / `XWidget_maximumSize(...)` | 最小/最大尺寸（最大默认 `XWIDGET_MAX_SIZE`=16777215 = `QWIDGETSIZE_MAX` qwidget.h:918） | `minimumSize`/`maximumSize` qwidget.h:264-265 | 值 |
| `void XWidget_setMinimumSize(XWidget*, int w, int h);` / `XWidget_setMinimumSizeSize(XWidget*, const XSize*)` | 设最小尺寸（`XWIDGET_MAX_SIZE` 映射为 0） | `setMinimumSize` qwidget.h:270-271 | — |
| `void XWidget_setMaximumSize(XWidget*, int w, int h);` / `XWidget_setMaximumSizeSize(...)` | 设最大尺寸 | `setMaximumSize` qwidget.h:272-273 | — |
| `int XWidget_minimumWidth(...)` / `minimumHeight` / `maximumWidth` / `maximumHeight` | 单轴查询 | qwidget.h:266-269 | — |
| `void XWidget_setMinimumWidth(...)` / `setMinimumHeight` / `setMaximumWidth` / `setMaximumHeight` | 单轴设置（另一轴不受影响） | qwidget.h:274-277 | — |
| `XSize XWidget_baseSize(const XWidget* self);` | 基准尺寸，默认 (0,0) | `baseSize` qwidget.h:286（qwidget.cpp:3896-3901） | 值 |
| `void XWidget_setBaseSize(XWidget*, int w, int h);` / `XWidget_setBaseSizeSize(...)` | 设基准尺寸；顶层已建柄时转发 `XWindow_setBaseSize` | `setBaseSize` qwidget.h:287-288 | — |
| `XSize XWidget_sizeIncrement(...)`; `void XWidget_setSizeIncrement(...)` / `XWidget_setSizeIncrementSize(...)` | 尺寸步进；同样转发平台窗口 | `sizeIncrement`/`setSizeIncrement` qwidget.h:283-285 | — |
| `XSize XWidget_sizeHint(const XWidget* self);` | 经 `SizeHint` 虚槽；布局总提示优先 | `sizeHint` qwidget.h:519 | 值 |
| `XSize XWidget_sizeHint_base(const XWidget* self);` | **非虚**基类求值（覆写类回落用，防无限递归） | X 扩展（Qt 无显式基类入口） | 值 |
| `XSize XWidget_minimumSizeHint(const XWidget* self);` | 经 `MinimumSizeHint` 虚槽 | `minimumSizeHint` qwidget.h:520 | 值 |
| `XSize XWidget_minimumSizeHint_base(const XWidget* self);` | 非虚基类求值 | X 扩展 | 值 |
| `int XWidget_heightForWidth(const XWidget* self, int width);` | 布局/回调/策略求高，不支持返回 -1 | `heightForWidth` qwidget.h:525 | — |
| `bool XWidget_hasHeightForWidth(const XWidget* self);` | 是否支持 hfw | `hasHeightForWidth` qwidget.h:526 | — |
| `XWidgetSizePolicy XWidget_sizePolicy(const XWidget* self);` | 尺寸策略 | `sizePolicy` qwidget.h:522 | 值 |
| `void XWidget_setSizePolicy(XWidget*, XWidgetSizePolicyPolicy horizontal, XWidgetSizePolicyPolicy vertical);` | **只改两轴**，其余字段保留（登记偏差） | `setSizePolicy(Policy,Policy)` qwidget.h:524（Qt 会复位其余字段） | — |
| `void XWidget_setSizePolicyFull(XWidget* self, const XWidgetSizePolicy* policy);` | 整体替换策略；retain 位翻转期间置内部位放行 updateGeometry | `setSizePolicy(QSizePolicy)` qwidget.h:523 | policy 借用 |

### 5. 控件树、命中与遮罩（XWidget.h:1240-1363）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XWidget* XWidget_parentWidget(const XWidget* self);` | 最近控件祖先（非控件父→NULL） | `parentWidget` qwidget.h:615 | 借用禁释放 |
| `void XWidget_setParent(XWidget* self, XWidget* parent, XWidgetFlags flags);` | 换父：双向 enabled/updates 传播、按新父重算可见、真实换父派发 PARENT_CHANGE、持焦子树清焦 | `setParent(QWidget*,WindowFlags)` qwidget.h:542（qwidget.cpp:10702-10847） | parent 借用 |
| `void XWidget_setParent_2(XWidget* self, XWidget* parent);` | 换父并清除窗口类型位（`flags & ~0xff`）；同父早退 | `setParent(QWidget*)` qwidget.h:541（qwidget.cpp:10668-10673） | parent 借用 |
| `XWidget* XWidget_childAt(const XWidget* self, const XPoint* point);` | 逆序递归命中；跳过窗口型/显式隐藏/`TransparentForMouseEvents`；子遮罩排除时 continue | `childAt(const QPoint&)` qwidget.h:626（`childAt_helper` qwidget.cpp:10477-10502） | 借用禁释放 |
| `XWidget* XWidget_childAt_2(const XWidget* self, int x, int y);` | 整数坐标重载 | `childAt(int,int)` qwidget.h:625 | 借用禁释放 |
| `XWidget* XWidget_childAtGlobal(const XWidget* self, const XPoint* globalPoint);` | 全局坐标一步命中 | X 扩展 | 借用禁释放 |
| `XRegion XWidget_mask(const XWidget* self);` | 遮罩深拷贝 | `mask` qwidget.h:344 | **拷贝须释放**：`XRegion_deinit`（h:1286-1287） |
| `const XRegion* XWidget_mask_const(const XWidget* self);` | 内部遮罩只读借用 | X 扩展（Qt 无借用版） | 借用禁释放/禁改 |
| `bool XWidget_hasMask(const XWidget* self);` | `mask().count>0` 便利判定 | X 扩展（Qt 用 `mask().isEmpty()`） | — |
| `void XWidget_setMask(XWidget* self, const XRegion* region);` | 设/清遮罩；缩小后让出区入父脏区；顶层同步原生窗口遮罩 | `setMask(const QRegion&)` qwidget.h:343 | region 借用 |
| `void XWidget_setMask_2(XWidget* self, const XBitmap* bitmap);` | 位图置位像素构成遮罩；空位图等价清除 | `setMask(const QBitmap&)` qwidget.h:342（qwidget.cpp:13192-13195） | bitmap 借用 |
| `void XWidget_clearMask(XWidget* self);` | 清遮罩 | `clearMask` qwidget.h:345 | — |
| `XRect XWidget_childrenRect(const XWidget* self);` | 非窗口且未显式隐藏子控件的外接矩形 | `childrenRect` qwidget.h:261（qwidget.cpp:3715-3727 过滤 `isHidden()`） | 值 |
| `XRegion XWidget_childrenRegion(const XWidget* self);` | 子控件区域并集（按显式隐藏位过滤） | `childrenRegion` qwidget.h:262（qwidget.cpp:3739-3757） | **拷贝须释放**：`XRegion_deinit`（h:1343） |
| `const XRegion* XWidget_childrenRegion_const(const XWidget* self);` | 内部缓存借用 | X 扩展 | 借用，下次调用即失效 |
| `bool XWidget_isAncestorOf(const XWidget* self, const XWidget* child);` | 祖先或自身；不跨顶层边界 | `isAncestorOf` qwidget.h:636 | — |

### 6. Z 序与窗口/坐标映射（XWidget.h:1364-1459）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XWidget_raise(XWidget* self);` / `XWidget_lower(XWidget*)` | 父 children 序移末/移首并重绘 | `raise`/`lower` qwidget.h:493-494 | — |
| `void XWidget_stackUnder(XWidget* self, XWidget* other);` | 插到指定兄弟之下 | `stackUnder` qwidget.h:497 | other 借用 |
| `XWidget* XWidget_window(const XWidget* self);` | 所在顶层控件 | `window` qwidget.h:310 | 借用禁释放 |
| `XWindow* XWidget_nativeWindow(const XWidget* self);` | 顶层桥接窗口句柄 | X 扩展（`windowHandle` qwidget.h:648 返回 QWindow） | 借用禁释放 |
| `XWindow* XWidget_windowHandle(const XWidget* self);` | `nativeWindow` 别名 | `windowHandle` qwidget.h:648 | 借用禁释放 |
| `XWindowId XWidget_winId(const XWidget* self);` | 沿顶层句柄取原生 id；未建柄时 `XWindow_winId` 惰性建柄 | `winId` qwidget.h:218（qwidget.cpp:2377 强制建窗） | — |
| `XWindowId XWidget_effectiveWinId(const XWidget* self);` | 只读变体，不建窗；无已建柄返回 0 | `effectiveWinId` qwidget.h:221（qwidget.cpp:2482-2492） | — |
| `XWidget* XWidget_nativeParentWidget(const XWidget* self);` | 最近的已建柄祖先控件 | `nativeParentWidget` qwidget.h:311（qwidget.cpp:4358-4364） | 借用禁释放 |
| `XWidget* XWidget_topLevelWidget(const XWidget* self);` | `window()` 别名（已废弃拼写） | `topLevelWidget` qwidget.h:312 | 借用禁释放 |
| `XPoint XWidget_mapToGlobal(...)` / `mapFromGlobal(...)` / `mapToParent(...)` / `mapFromParent(...)` / `mapTo(...)` / `mapFrom(...)` | 六级坐标映射（顶层有句柄时以 `XWindow_mapToGlobal` 为准，含内容旋转折算） | qwidget.h:298/300/302/304/306/308（整数版） | 值 |

### 7. 窗口内容旋转（XWidget.h:1461-1488，X 扩展）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWidget_setContentRotation(XWidget* self, int deg);` | 转发 `XWindow_setContentRotation` 并置整帧脏闩（不同步 flush） | X 扩展 | — |
| `int XWidget_contentRotationDegrees(const XWidget* self);` | 读顶层有效旋转角 | X 扩展 | — |

### 8. 可见性与窗口状态（XWidget.h:1490-1582）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWidget_isVisible(const XWidget* self);` | 生效可见（含父链） | `isVisible` qwidget.h:507 | — |
| `bool XWidget_isHidden(const XWidget* self);` | 显式隐藏位 `WState_Hidden` | `isHidden` qwidget.h:509 | — |
| `bool XWidget_isVisibleTo(const XWidget* self, const XWidget* ancestor);` | 相对祖先可见 | `isVisibleTo` qwidget.h:508 | — |
| `void XWidget_setVisible(XWidget* self, bool visible);` | 经 `SetVisible` 虚槽分派 | `setVisible`（public virtual）qwidget.h:482 | — |
| `void XWidget_show(XWidget* self);` | 经虚槽；顶层首显按 `XPlatformIntegration_defaultWindowState` 分流 FullScreen/Maximized | `show` qwidget.h:484（qwidget.cpp:7913-7929） | — |
| `void XWidget_hide(XWidget* self);` | 经虚槽隐藏 | `hide` qwidget.h:485 | — |
| `void XWidget_setHidden(XWidget* self, bool hidden);` | `setVisible(!hidden)` | `setHidden` qwidget.h:483 | — |
| `void XWidget_showNormal(...)` / `showMinimized(...)` / `showMaximized(...)` / `showFullScreen(...)` | 位运算保留其余状态位后 show；minimized 清 Active；fullscreen 尾补 `activateWindow` | qwidget.h:487-490（qwidget.cpp:2869-2880 / 3043-3054 / 3066-3073 / 3082-3090） | — |
| `bool XWidget_isMinimized(...)` / `isMaximized(...)` / `isFullScreen(...)` | 特殊状态位查询（要求 `m_isWindow`） | qwidget.h:511-513 | — |
| `bool XWidget_isActiveWindow(const XWidget* self);` | 顶层活跃位/`XWindowState_Active` | `isActiveWindow` qwidget.h:426 | — |
| `bool XWidget_isModal(const XWidget* self);` | 读**自身** `m_windowModality`（普通子控件恒 false） | `isModal` qwidget.h:234（inline `window_modality != NonModal`） | — |
| `bool XWidget_isWindowModal(const XWidget* self);` | 顶层窗口的模态（含 `ShowModal` 兜底） | X 扩展（Qt 无窗口级变体） | — |
| `XWindowStates XWidget_windowState(const XWidget* self);` | 窗口状态 | `windowState` qwidget.h:515 | 位值 |
| `void XWidget_setWindowState(XWidget* self, XWindowStates state);` | 清 Active（最小化时）、同值早退、首次进入特殊态存 normalGeometry、派发 WINDOW_STATE_CHANGE | `setWindowState` qwidget.h:516（qwidget.cpp:2959-2985） | — |
| `void XWidget_overrideWindowState(XWidget* self, XWindowStates state);` | 直改状态位，不下发平台、不动 normalGeometry、不派发事件 | `overrideWindowState` qwidget.h:517 | — |
| `XWindowModality XWidget_windowModality(const XWidget* self);` | 窗口模态 | `windowModality` qwidget.h:235 | — |
| `void XWidget_setWindowModality(XWidget* self, XWindowModality modality);` | 存字段并同步 `ShowModal`/`SetWindowModality` 两位 + 下发桥接窗口 | `setWindowModality` qwidget.h:236（qwidget.cpp:2821-2827） | — |
| `void XWidget_activateWindow(XWidget* self);` | 只请求激活，**不改** `windowState` | `activateWindow` qwidget.h:427（qwidget.cpp:12965-12971） | — |
| `bool XWidget_close(XWidget* self);` | 派发 CLOSE；接受则隐藏并返回 true | `close` qwidget.h:492（qwidget.cpp:8611） | — |

### 9. 标题/图标/透明度/自定义标题条（XWidget.h:1584-1698）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `const XString* XWidget_windowTitle(const XWidget* self);` | 标题；未设但设了 filePath 时派生「文件名+`[*]`」并入缓存 | `windowTitle` qwidget.h:376（qwidget.cpp:5997-6003） | 借用禁释放（无 `_const`，见命名契约） |
| `void XWidget_setWindowTitle(XWidget* self, const XString* title);` | 设标题；`[*]` 按段计数替换；变化发信号 | `setWindowTitle` qwidget.h:368（qwidget.cpp:6017-6052/6132-6149） | title 借用 |
| `void XWidget_setWindowTitle_2(XWidget* self, const char* utf8);` | UTF-8 便捷重载 | 同上 | utf8 借用 |
| `const XString* XWidget_windowIconText(const XWidget* self);` | 图标文本（Qt 6.1+ 已废弃） | `windowIconText` qwidget.h:380 | 借用禁释放 |
| `void XWidget_setWindowIconText(XWidget* self, const XString* text);` | 仅存储不推送平台；派发 ICON_TEXT_CHANGE 并发信号 | `setWindowIconText` qwidget.h:379（qwidget.cpp:6115） | text 借用 |
| `XIcon XWidget_windowIcon(const XWidget* self);` | 自身图标→父链→应用图标三级解析 | `windowIcon` qwidget.h:378 | **拷贝须释放**：`XClassDeinit`（h:1617-1619） |
| `void XWidget_setWindowIcon(XWidget* self, const XIcon* icon);` | 顶层即时刷平台图标；变化**无条件**派发/发信号 | `setWindowIcon` qwidget.h:377（qwidget.cpp:6228） | icon 借用 |
| `const XString* XWidget_windowFilePath(const XWidget* self);` | 窗口文件路径 | `windowFilePath` qwidget.h:384 | 借用禁释放 |
| `void XWidget_setWindowFilePath(XWidget* self, const XString* path);` | 设文件路径（派生标题源） | `setWindowFilePath` qwidget.h:383 | path 借用 |
| `double XWidget_windowOpacity(const XWidget* self);` | 不透明度（控件级为 X 扩展语义） | `windowOpacity` qwidget.h:387 | — |
| `void XWidget_setWindowOpacity(XWidget* self, double opacity);` | 设不透明度并同步桥接窗口 | `setWindowOpacity` qwidget.h:386 | — |
| `bool XWidget_isWindowModified(const XWidget* self);` | 已修改标志 | `isWindowModified` qwidget.h:389 | — |
| `void XWidget_setWindowModified(XWidget* self, bool modified);` | 置位 + 派发 MODIFIED_CHANGE + 重跑标题 `[*]` | `setWindowModified` qwidget.h:244（qwidget.cpp:11671-11698） | — |
| `void XWidget_setTitleBarWidget(XWidget* self, XWidget* bar);` | 控件级标题条槽：挂为子控件并 show；旧条先隐藏再归还 | X 扩展（Qt 仅 QDockWidget 有） | **借用**：bar 归创建方，宿主析构前解挂不删除（h:1666-1680） |
| `XWidget* XWidget_titleBarWidget(const XWidget* self);` | 查当前标题条 | X 扩展 | 借用；调用方释放前须先清槽（h:1694-1695） |

### 10. 可用性与焦点（XWidget.h:1700-1819）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWidget_isEnabled(const XWidget* self);` | 只看 `Disabled` 位 | `isEnabled` qwidget.h:238 | — |
| `void XWidget_setEnabled(XWidget* self, bool enabled);` | 记 `ForceDisabled` 请求；禁用持焦控件先 `focusNextChild` 顺延，失败才清焦；递归传播 | `setEnabled` qwidget.h:242（`setEnabled_helper` qwidget.cpp:3387-3412） | — |
| `void XWidget_setDisabled(XWidget* self, bool disabled);` | `setEnabled(!disabled)` | `setDisabled` qwidget.h:243 | — |
| `bool XWidget_isEnabledTo(const XWidget* self, const XWidget* ancestor);` | 相对祖先启用判定 | `isEnabledTo` qwidget.h:239 | — |
| `XWidgetFocusPolicy XWidget_focusPolicy(const XWidget* self);` | 焦点策略（默认 NoFocus） | `focusPolicy` qwidget.h:431 | — |
| `void XWidget_setFocusPolicy(XWidget* self, XWidgetFocusPolicy policy);` | 赋值并沿代理链转发（限深 64 防环） | `setFocusPolicy` qwidget.h:432（qwidget.cpp:7860-7866） | — |
| `bool XWidget_hasFocus(const XWidget* self);` | 解析最深代理后比全局焦点 | `hasFocus` qwidget.h:433（qwidget.cpp:6485-6499） | — |
| `XWidget* XWidget_focusWidget(const XWidget* self);` | 本顶层窗口内的焦点控件（无则 NULL） | `focusWidget` qwidget.h:549（qwidget.cpp:6867-6870，Qt 为 per-window `focus_child` 记忆） | 借用禁释放 |
| `XWidget* XWidget_nextInFocusChain(const XWidget* self);` | 显式 Tab 链 next，失效则文档序下一可聚焦候选 | `nextInFocusChain` qwidget.h:550 | 借用禁释放 |
| `XWidget* XWidget_previousInFocusChain(const XWidget* self);` | 反向同上 | `previousInFocusChain` qwidget.h:551 | 借用禁释放 |
| `void XWidget_setFocus(XWidget* self);` | 以 `OtherFocusReason` 转发 `_2` | `setFocus()` qwidget.h:423（inline） | — |
| `void XWidget_setFocus_2(XWidget* self, XFocusReason reason);` | 落最深代理；窗口未激活时登记待激活焦点靶而不抢焦；换焦发 FOCUS_OUT/FOCUS_IN | `setFocus(Qt::FocusReason)` qwidget.h:430（qwidget.cpp:6535-6628） | — |
| `void XWidget_clearFocus(XWidget* self);` | 清全局焦点 | `clearFocus` qwidget.h:428（qwidget.cpp:6722-6770） | — |
| `XWidget* XWidget_focusProxy(const XWidget* self);` | 焦点代理 | `focusProxy` qwidget.h:437 | 借用禁释放（无 `_const`） |
| `void XWidget_setFocusProxy(XWidget* self, XWidget* proxy);` | 设代理；拒绝成环；代理销毁自动摘除 | `setFocusProxy` qwidget.h:436（qwidget.cpp:6407-6452） | proxy 借用 |
| `bool XWidget_focusNextChild(XWidget* self);` | `focusNextPrevChild(true)` 包装，经虚槽 | `focusNextChild` qwidget.h:728（inline） | — |
| `bool XWidget_focusPreviousChild(XWidget* self);` | `focusNextPrevChild(false)` 包装 | `focusPreviousChild` qwidget.h:729 | — |
| `void XWidget_setTabOrder(XWidget* first, XWidget* second);` | 同窗且非 NoFocus 才建链；second 先摘旧链再插 first 之后（防 2 环） | `setTabOrder` qwidget.h:434（qwidget.cpp:7020-7084） | 双方借用 |

### 11. 跟踪/抓取/菜单策略/布局方向（XWidget.h:1821-1937）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWidget_hasMouseTracking(...)` / `void XWidget_setMouseTracking(...)` | 鼠标跟踪；变更派发 MOUSE_TRACKING_CHANGE | qwidget.h:335-336 | — |
| `bool XWidget_underMouse(const XWidget* self);` | 悬停位 | `underMouse` qwidget.h:337 | — |
| `bool XWidget_hasTabletTracking(...)` / `void XWidget_setTabletTracking(...)` | 板绘跟踪；变更派发 TABLET_TRACKING_CHANGE | qwidget.h:339-340 | — |
| `bool XWidget_acceptDrops(...)` / `void XWidget_setAcceptDrops(...)` | 接受拖放（读写 `AcceptDrops` 属性位）；变更派发 ACCEPT_DROPS_CHANGE | qwidget.h:554-555 | — |
| `void XWidget_grabMouse(XWidget* self);` | 显式鼠标抓取（需可见；跨窗事件转投并换算坐标） | `grabMouse()` qwidget.h:442（qwidget.cpp:12807-12811） | — |
| `void XWidget_grabMouse_2(XWidget* self, const XCursor* cursor);` | 抓取期套光标；保存抓取前自有光标供 release 还原 | `grabMouse(const QCursor&)` qwidget.h:444 | cursor 借用 |
| `void XWidget_releaseMouse(XWidget* self);` | 仅当前抓取者生效；按保存值还原光标 | `releaseMouse` qwidget.h:446 | — |
| `XWidget* XWidget_mouseGrabber(void);` | 当前显式鼠标抓取者 | `mouseGrabber` qwidget.h:455（Qt 还含隐式按压抓取） | 借用禁释放 |
| `void XWidget_setTouchMouseSynthesisEnabled(bool on);` / `bool XWidget_touchMouseSynthesisEnabled(void);` | 未处理触摸序列的 touch→mouse 仿真开关（默认开） | X 扩展（对位 `AA_SynthesizeMouseForUnhandledTouchEvents`） | — |
| `void XWidget_grabKeyboard(XWidget* self);` / `releaseKeyboard` / `XWidget* XWidget_keyboardGrabber(void)` | 键盘抓取三件套 | qwidget.h:447-448/456 | 借用禁释放（getter） |
| `XWidgetContextMenuPolicy XWidget_contextMenuPolicy(...)` / `void XWidget_setContextMenuPolicy(...)` | 右键菜单策略（默认 `DefaultContextMenu`） | qwidget.h:438-439 | — |
| `XWidgetLayoutDirection XWidget_layoutDirection(...)` / `setLayoutDirection` / `unsetLayoutDirection` | 布局方向及其继承/传播 | qwidget.h:411-413 | — |
| `bool XWidget_isRightToLeft(...)` / `isLeftToRight(...)` | 方向便捷判定 | qwidget.h:419-420 | — |

### 12. 光标/提示/调色板/字体（XWidget.h:1939-2110）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XCursor XWidget_cursor(const XWidget* self);` | 沿父链上溯光标，链上全无回落 Arrow | `cursor` qwidget.h:330（qwidget.cpp:4980-4982） | 值返回（内部 `XClassCopy` 深拷；头注释未写释放契约，见坑点） |
| `void XWidget_setCursor(XWidget* self, const XCursor* cursor);` | 设光标并派发 CURSOR_CHANGE | `setCursor` qwidget.h:331 | cursor 借用 |
| `void XWidget_unsetCursor(XWidget* self);` | 清自定义光标并派发 CURSOR_CHANGE | `unsetCursor` qwidget.h:332 | — |
| `const XString* XWidget_toolTip(const XWidget* self);` | 工具提示 | `toolTip` qwidget.h:392 | 借用禁释放（无 `_const`） |
| `void XWidget_setToolTip(XWidget* self, const XString* tip);` | 设提示并派发 TOOL_TIP_CHANGE | `setToolTip` qwidget.h:391 | tip 借用 |
| `int XWidget_toolTipDuration(...)` / `void XWidget_setToolTipDuration(...)` | 提示时长（默认 -1 动态） | qwidget.h:393-394 | — |
| `const XString* XWidget_statusTip(...)` / `void XWidget_setStatusTip(...)` | 状态提示 | qwidget.h:397-398 | 借用禁释放（getter） |
| `const XString* XWidget_whatsThis(...)` / `void XWidget_setWhatsThis(...)` | What's This 文本 | qwidget.h:401-402 | 借用禁释放（getter） |
| `const XString* XWidget_accessibleName(...)` / `setAccessibleName` | 无障碍名称 | qwidget.h:405-406 | 借用禁释放（getter） |
| `const XString* XWidget_accessibleDescription(...)` / `setAccessibleDescription` | 无障碍描述 | qwidget.h:407-408 | 借用禁释放（getter） |
| `const XString* XWidget_windowRole(...)` / `setWindowRole` | 窗口角色（仅存储，不下发平台） | qwidget.h:381-382 | 借用禁释放（getter） |
| `XInputMethodHints XWidget_inputMethodHints(...)` / `setInputMethodHints` | 输入法提示位 | qwidget.h:714-715 | — |
| `const XString* XWidget_styleSheet(...)` / `void XWidget_setStyleSheet(...)` | 样式表文本；变更废缓存 + 重跑 qproperty + 调度重绘 | qwidget.h:374/370 | 借用禁释放（getter） |
| `void XWidget_applyStyleSheetProperties(XWidget* self);` | 把 qproperty-<名>:<值> 应用到动态属性（幂等账本） | X 扩展（对位 `QStyleSheetStyle::setProperties`） | — |
| `XFont XWidget_font(const XWidget* self);` | 字体深拷贝（父链/应用默认回落） | `font` qwidget.h:324 | **拷贝须释放**：`XClassDeinit`（h:2052-2054） |
| `void XWidget_setFont(XWidget* self, const XFont* font);` | 设字体并向未显式设字体的子控件传播 | `setFont` qwidget.h:325 | font 借用 |
| `XFont XWidget_fontMetrics(const XWidget* self);` | 测量凭据（XFont 值拷贝，非 QFontMetrics） | `fontMetrics` qwidget.h:326（承载差异） | **拷贝须释放**：`XClassDeinit`（h:2076-2077） |
| `XFont XWidget_fontInfo(const XWidget* self);` | 生效字体信息（XFont 值拷贝，非 QFontInfo） | `fontInfo` qwidget.h:327（承载差异） | **拷贝须释放**：`XClassDeinit`（h:2095-2096） |
| `XPalette XWidget_palette(const XWidget* self);` | 自身调色板或应用调色板 | `palette` qwidget.h:315（qwidget.cpp:4539-4553） | 值（POD 矩阵，无堆资源） |
| `void XWidget_setPalette(XWidget* self, const XPalette* palette);` | 整体覆盖全矩阵（无 resolve/传播） | `setPalette` qwidget.h:316 | palette 借用 |
| `XPaletteColorRole XWidget_backgroundRole(...)` / `setBackgroundRole(...)` | 背景角色 | qwidget.h:318-319 | — |
| `XPaletteColorRole XWidget_foregroundRole(...)` / `setForegroundRole(...)` | 前景角色（恒返回设置值，无对比色推导） | qwidget.h:321-322（Qt 永不返回 NoRole） | — |

### 13. 动作列表（XWidget.h:2112-2233）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XWidget_addAction(XWidget* self, XAction* action);` | 尾插；去重；连接 destroyed；派发 ACTION_ADDED | `addAction(QAction*)` qwidget.h:559 | action 借用，控件不释放 |
| `XAction* XWidget_addAction_2(XWidget* self, const XString* text);` | 新建动作（父为本控件）并加入 | `addAction(const QString&)` qwidget.h:567 | 控件（父链）拥有 |
| `void XWidget_addActions(XWidget* self, const XVector* actions);` | 批量尾插（元素 `XAction*`） | `addActions` qwidget.h:560 | actions 借用 |
| `void XWidget_insertAction(XWidget* self, XAction* before, XAction* action);` | before 前插入；before 为空/NULL 则尾插 | `insertAction` qwidget.h:562 | 双方借用 |
| `void XWidget_insertActions(XWidget* self, XAction* before, const XVector* actions);` | 批量插入（最终次序同入参） | `insertActions` qwidget.h:561 | 借用 |
| `void XWidget_removeAction(XWidget* self, XAction* action);` | 摘指针 + 断连接 + 派发 ACTION_REMOVED | `removeAction` qwidget.h:563 | 不释放动作 |
| `XVector* XWidget_actions(const XWidget* self);` | 动作列表值拷贝（元素借用） | `actions` qwidget.h:564（Qt 返回值列表） | **拷贝须释放**：`XClassDelete`（h:2231） |

### 14. 手势识别器订阅（XWidget.h:2177-2209，`XGESTURE_ON`）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XWidget_addGestureRecognizer(XWidget* self, XGestureRecognizer* recognizer);` | 订阅识别器（幂等，绑定 target=本控件） | `grabGesture` qwidget.h:363（承载不同：Qt 传 GestureType） | recognizer 借用 |
| `void XWidget_removeGestureRecognizer(XWidget* self, XGestureRecognizer* recognizer);` | 解除单条订阅 | `ungrabGesture` qwidget.h:364 | 借用 |
| `void XWidget_clearGestureRecognizers(XWidget* self);` | 清空全部订阅 | X 扩展 | 借用 |

### 15. 输入法查询与区域设置（XWidget.h:2235-2326）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVariant* XWidget_inputMethodQuery(const XWidget* self, XInputMethodQuery query);` | 经 `InputMethodQuery` 虚槽；ImCursorRectangle/ImInputItemClipRectangle/ImHints/ImEnabled 有值，其余 NULL | `inputMethodQuery` qwidget.h:712（qwidget.cpp:9064 口径） | **拷贝须释放**：`XClassDelete`（h:2263） |
| `void XWidget_updateMicroFocus(XWidget* self, XInputMethodQuery query);` | 请求输入法刷新微焦点 | `updateMicroFocus` qwidget.h:718（protected slot） | — |
| `const XString* XWidget_locale(const XWidget* self);` | 区域设置名（显式→父链） | `locale` qwidget.h:416 | 借用禁释放（无 `_const`） |
| `void XWidget_setLocale(XWidget* self, const XString* locale);` | 深拷存储 + 置 `SetLocale` 位 + 派发 LOCALE_CHANGE + 向子传播 | `setLocale` qwidget.h:415 | locale 借用 |
| `void XWidget_unsetLocale(XWidget* self);` | 清显式区域设置并重新按父链解析 | `unsetLocale` qwidget.h:417 | — |

### 16. 绘制闭环与快照（XWidget.h:2328-2521）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWidget_updatesEnabled(...)` / `void XWidget_setUpdatesEnabled(...)` | 更新使能（递归非窗口子控件） | qwidget.h:459-460（qwidget.cpp:1824-1839） | — |
| `bool XWidget_autoFillBackground(...)` / `setAutoFillBackground(...)` | 自动填背景 | qwidget.h:643-644 | — |
| `void XWidget_ensurePolished(XWidget* self);` | 幂等 polish：置位→自身 POLISH→递归子件→父 CHILD_POLISHED | `ensurePolished` qwidget.h:634（qwidget.cpp:10209-10235） | — |
| `void XWidget_update(XWidget* self);` / `updateRect` / `updateRegion` | 并入脏区并折算顶层，投递 PAINT（异步） | `update`/`update(QRect)`/`update(QRegion)` qwidget.h:467/472/473 | — |
| `void XWidget_repaint(XWidget* self);` / `repaintRect` / `repaintRegion` | 同步派发 PAINT | `repaint` 族 qwidget.h:468/476/477 | — |
| `XRegion XWidget_visibleRegion(const XWidget* self);` | 祖先裁剪后的可见区域（锚点=全幅 rect） | `visibleRegion` qwidget.h:528（qwidget.cpp:8716-8722） | **拷贝须释放**：`XRegion_deinit`（h:2377-2384） |
| `const XRegion* XWidget_visibleRegion_const(const XWidget* self);` | 内部缓存借用 | X 扩展 | 借用，下次调用即失效 |
| `XBackingStore* XWidget_backingStore(const XWidget* self);` | 顶层后备存储 | `backingStore` qwidget.h:646 | 借用禁释放 |
| `XWidget* XWidget_widgetForWindow(const XWindow* window);` | 桥接窗口→归属顶层控件反查（虚表同一性） | X 扩展 | 借用 |
| `void XWidget_scroll(XWidget* self, int dx, int dy);` | 平移裁剪区 + 调度重绘（无像素 blit、不移动子控件） | `scroll(int,int)` qwidget.h:544（无三参重载） | — |
| `int XWidget_devType(const XWidget* self);` | 设备类型（有效=1） | `devType` qwidget.h:216 | — |
| `void* XWidget_paintEngine(const XWidget* self);` | 内嵌 `XPaintEngine` 借用 | `paintEngine` qwidget.h:632 | 借用禁释放 |
| `XImage* XWidget_grab(const XWidget* self);` | 整控件快照（顶层优先后备存储深拷贝，否则同步重绘） | `grab(const QRect& = …)` qwidget.h:355 | **拷贝须释放**：`XClassDelete`（h:2469） |
| `XImage* XWidget_grab_2(const XWidget* self, const XRect* rect);` | 子矩形快照；与 rect() 求交钳位，交叠为空返回 NULL | `grab(const QRect&)` qwidget.h:355（qwidget.cpp:5257） | **拷贝须释放** |
| `bool XWidget_render(XWidget* self, XPainter* painter, const XRect* targetRect);` | 渲染到 painter；**仅支持等尺寸** targetRect | `render(QPainter*,…)` qwidget.h:351-353（承载简化） | painter/targetRect 借用 |

### 17. 布局挂接 / 图形效果 / 保留层（XWidget.h:2524-2669）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XLayout* XWidget_layout(const XWidget* self);` | 当前顶层布局 | `layout` qwidget.h:537（qwidget.cpp:10258-10261） | 借用（无 `_const` 形态，配对 `layout_const`） |
| `const XLayout* XWidget_layout_const(const XWidget* self);` | 同一指针的借用别名 | X 扩展（命名） | 借用禁释放 |
| `void XWidget_setLayout(XWidget* self, XLayout* layout);` | **借用版**挂接；重复设不同布局告警并保留首个；立即解算一次 | `setLayout` qwidget.h:538（qwidget.cpp:10289-10329） | **借用**，控件不释放 |
| `void XWidget_setLayout_ref(XWidget* self, XLayout* layout);` | **接管版**：登记入控件接管表，随控件析构释放 | `setLayout` 的所有权语义（qwidget.cpp:10284/10322） | **引用转移**：调用后不得再释放/使用 |
| `XGraphicsEffect* XWidget_graphicsEffect(const XWidget* self);` | 当前效果 | `graphicsEffect` qwidget.h:358 | 借用禁释放 |
| `void XWidget_setGraphicsEffect(XWidget* self, XGraphicsEffect* effect);` | 替换并释放旧效果（兼容入口，语义已是接管） | `setGraphicsEffect` qwidget.h:359 | **引用转移**（旧效果被删） |
| `void XWidget_setGraphicsEffect_ref(XWidget* self, XGraphicsEffect* effect);` | 三态契约显式命名的接管版（转发上者） | 同上 | **引用转移** |
| `void XWidget_invalidateAllRetainedLayers(void);` | 全量保留层失效 | X 扩展（LVGL 静态内容缓存思想） | — |
| `bool XWidget_setContentRetained(XWidget* self, bool on);` | 开/关静态内容保留层（默认关；预算 LRU） | X 扩展 | — |
| `bool XWidget_contentRetained(const XWidget* self);` | 查保留层开关 | X 扩展 | — |
| `void XWidget_retainedLayerStats(size_t* usedBytes, int* count);` | 保留层统计 | X 扩展 | 输出参数 |

### 18. 通知信号（XWidget.h:2671-2695）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XWidget_windowTitleChanged_signal(XWidget* self, const XString* title);` | 标题变化信号 | `windowTitleChanged` qwidget.h:655 | 信号标识；title 借用 |
| `void* XWidget_windowIconChanged_signal(XWidget* self, XIcon* icon);` | 图标变化信号（icon 为可变指针，h:2686） | `windowIconChanged` qwidget.h:656 | 信号标识 |
| `void* XWidget_windowIconTextChanged_signal(XWidget* self, const XString* text);` | 图标文本变化信号 | `windowIconTextChanged` qwidget.h:657 | 信号标识 |
| `void* XWidget_customContextMenuRequested_signal(XWidget* self, const XPoint* pos);` | 自定义右键菜单请求 | `customContextMenuRequested` qwidget.h:658 | 信号标识 |

**所有权与命名契约**

- **`_const` 借用族（4 个，新补）**：`XWidget_mask_const`（h:1298）、`XWidget_childrenRegion_const`（h:1356）、`XWidget_visibleRegion_const`（h:2400）、`XWidget_layout_const`（h:2546）——返回内部指针，调用方禁释放/禁改，下一次同名调用或控件销毁后失效。
- **无 `_const` 却返回借用的 getter（11 个，待全库改名）**：`windowTitle`/`windowIconText`/`windowFilePath`/`toolTip`/`statusTip`/`whatsThis`/`accessibleName`/`accessibleDescription`/`windowRole`/`styleSheet`/`locale`（XGui.md:1811 登记，头注释已用 `@warning` 补注）。另有 `parentWidget`/`childAt`/`childAt_2`/`childAtGlobal`/`focusWidget`/`focusProxy`/`nextInFocusChain`/`previousInFocusChain`/`window`/`nativeWindow`/`windowHandle`/`nativeParentWidget`/`topLevelWidget`/`mouseGrabber`/`keyboardGrabber`/`graphicsEffect`/`layout`/`backingStore`/`widgetForWindow`/`titleBarWidget` 亦为借用返回（头注释以 `@warning` 逐条写明）。
- **`_utf8` 设置侧**：仅 `XWidget_setWindowTitle_2(XWidget*, const char*)` 一个 char* 设置入口，当前用 `_2` 承载，**违反**「设置侧 char* 必须 `_utf8`」契约（XGui.md:1813 登记为全库性遗留）。
- **`_2` 重载族（9 个）**：`setContentsMargins_2`（h:926，`const XMargins*`）、`setParent_2`（h:1267，两参换父）、`childAt_2`（h:1277，整数坐标）、`setMask_2`（h:1327，`const XBitmap*`）、`setFocus_2`（h:1763，`XFocusReason`）、`grab_2`（h:2500，子矩形）、`grabMouse_2`（h:1859，带光标）、`addAction_2`（h:2138，文本建动作）、`setWindowTitle_2`（h:1598，char*——应改 `_utf8`）。描述性后缀族（`_Size`/`_Rect`/`_Point`/`_Full`）仍存在：`setMinimumSizeSize`/`setMaximumSizeSize`/`setBaseSizeSize`/`setSizeIncrementSize`/`setFixedSizeSize`/`setGeometryRect`/`resizeSize`/`setSizePolicyFull`，属 §8.2.1 登记的全库机械改名项（XGui.md:1812）。`movePoint` 已于 2026-11-07 改为 `move_2`（对齐 `QWidget::move(const QPoint&)`）。
- **三态 `set*`**：仅布局与图形效果两族有三态——`setLayout`（借用）/`setLayout_ref`（接管）；`setGraphicsEffect`（名义兼容，实际已接管）/`setGraphicsEffect_ref`（契约命名接管）。**无 `_move` 变体**。其余 setter 取 `const X*` 入参，按「拷贝」态处理（控件内部深拷/存值，调用方保留所有权）。
- **按值返回且含堆资源者必须释放**：`saveGeometry`（`XByteArray*`）、`windowIcon`（`XIcon`）、`font`/`fontMetrics`/`fontInfo`（`XFont`）、`actions`（`XVector*`）、`inputMethodQuery`（`XVariant*`）、`grab`/`grab_2`（`XImage*`）、`mask`/`childrenRegion`/`visibleRegion`（`XRegion`）——归还方式分别为 `XClassDelete` / `XClassDeinit` / `XRegion_deinit`，逐条见上表。

**可覆写点**（`XCLASS_DEFINE_ENUM(XWidget, …)` 共 **33** 槽，XWidget.h:518-572；`XObject` 的 Event/EventFilter/ChildEvent 等槽经 `XVTABLE_INHERIT_XCLASS(XObject)` 继承）

| 槽 | `_base` 入口 | 默认行为 |
|---|---|---|
| `PaintEvent` | `XWidget_paintEvent_base`（Protected.h:121） | `autoFillBackground` 时按背景角色填底，否则空操作（XWidget.c paintEvent_default） |
| `ResizeEvent` / `MoveEvent` / `ShowEvent` / `HideEvent` / `ChangeEvent` | 各自 `_base`（Protected.h:123/125/163/165/167） | `XWidget_noopEvent_default`：空操作 |
| `CloseEvent` | `XWidget_closeEvent_base`（:127） | `XWidget_closeEvent_default`：`XEvent_accept` |
| `FocusInEvent` / `FocusOutEvent` | `_base`（:129/131） | `XWidget_focusEvent_default`：`update()`（`FocusOwnerQuiet` 宿主与 NoFocus 窗口豁免） |
| `EnterEvent` / `LeaveEvent` / `KeyPressEvent` / `KeyReleaseEvent` / `InputMethodEvent` / `MousePressEvent` / `MouseReleaseEvent` / `MouseMoveEvent` / `WheelEvent` / `ContextMenuEvent` | 各自 `_base`（:133-161） | `XWidget_ignoreEvent_default`：`XEvent_ignore`（允许父链冒泡） |
| `DragEnterEvent` / `DragMoveEvent` / `DragLeaveEvent` / `DropEvent` | 各自 `_base`（:143-151） | `XWidget_dropEvent_default`：`m_acceptDrops` 为真则 accept，否则 ignore |
| `MouseDoubleClickEvent` | `_base`（:157） | `XWidget_mouseDoubleClickForward_default`：转发 `mousePressEvent`（Qt qwidget.cpp:9579-9582 同口径） |
| `TouchEvent` | `XWidget_touchEvent_base`（**仅 XWidget.c:452 的 TU 内声明**） | `XWidget_ignoreEvent_default` |
| `TabletEvent` | `XWidget_tabletEvent_base`（**仅 XWidget.c:458**） | `XWidget_ignoreEvent_default` |
| `InputMethodQuery`（`XINPUTMETHOD_ON`） | 无 `_base`（直调 `XWidget_inputMethodQuery`） | `XWidget_inputMethodQuery_default`：ImCursorRectangle=(w/2,0,1,h) 整数除、ImInputItemClipRectangle、ImHints、ImEnabled，其余 NULL |
| `TouchDragEvent`（X 扩展） | `XWidget_touchDragEvent_base`（**仅 XWidget.c:454**） | `XWidget_ignoreEvent_default`：不接受即沿父链 |
| `GestureEvent`（X 扩展） | `XWidget_gestureEvent_base`（**仅 XWidget.c:456**） | `XWidget_ignoreEvent_default` |
| `SizeHint` | `XWidget_sizeHint_base`（公开，h:1184） | 布局总提示优先，否则 `m_sizeHint` 缓存（默认 (-1,-1)） |
| `MinimumSizeHint` | `XWidget_minimumSizeHint_base`（公开，h:1191） | 布局总最小提示优先，否则 `m_minimumSizeHint` |
| `FocusNextPrevChild` | `XWidget_focusNextPrevChild_base`（Protected.h:182） | 非窗口→上交顶层；窗口→以窗口焦点控件（无则顶层自身）为起点续链 |
| `SetVisible` | `XWidget_setVisible_base`（Protected.h:193） | `XWidget_setVisibleImpl`：置 `ExplicitShowHide`、首显 `ensurePolished`、顶层惰性建窗、挂起 Resize 兑现、布局激活 |

注：`SizeHint`/`MinimumSizeHint` 两槽注册的默认实现**就是** `XWidget_sizeHint_base` / `XWidget_minimumSizeHint_base` 本体（XWidget.c:4964-4965），而 `FocusNextPrevChild`/`SetVisible` 注册的是 `xwidget_focusNextPrevChild_default` / `xwidget_setVisible_default`（:4966-4967）。`XWidget_sizeHint` 读槽时用 `fn != XWidget_sizeHint_base` 判自递归并回落（XWidget.c:7301-7313）；因此覆写 `SizeHint` 的类回落基类**必须**调 `XWidget_sizeHint_base`，不可调 `XWidget_sizeHint`（XWidget.h:1175-1183 已写明「调 XWidget_sizeHint 会再次分派到自身槽 → 无限递归」）。`FocusNextPrevChild`/`SetVisible` 的槽默认值本身不做分派，回落走 `XWidget_focusNextPrevChild_base` / `XWidget_setVisible_base`。

**注意事项与坑点**

1. **头注释与实现的恢复次序相反（`restoreGeometry`）**：头文件写「先恢复窗口状态标志…再恢复当前几何」（XWidget.h:1063-1065），实现按 Qt 6.8 口径是**几何先、状态后**（XWidget.c:7024-7069，注释自陈「本实现此前『先状态后几何』…往返断言失败」）。按头注释写调用方会误判副作用次序；且 Qt 侧 `restoreGeometry` 的对应次序在 qwidget.cpp:7572-7595。

2. **`ExplicitShowHide` 的早退承诺未实现，且登记出处失实**：属性注释称「置位后 setVisible 在目标状态与当前显式状态一致时早退」（XWidget.h:276-280），而实现**明确不复刻**该早退（XWidget.c:8448-8455，理由：早退会卡死布局联动）。`docs/xgui/XWidget_Qt683_对齐测试说明.md:146` 声称此偏离「已登记偏差（XGui.md §8.1.1）」，但 `grep -c "ExplicitShow\|显式显隐\|同值早退" XGui.md` 结果均为 **0**——登记并不存在，属文档链条断点。

3. **事件虚函数的「个数」在四处自述不一致，且都低于实际**：XWidget.h:35 写「23 个事件虚函数」、XWidget.h:509 写「26 个新槽位」、XWidget_Protected.h:113 写「23 个事件虚函数」、XWidget.c:13917 写「25 个控件事件虚函数」；实测 `rg -c "XCLASS_DEFINE_ENUM\(XWidget,"` = **33**（32 个恒有 = 26 事件槽 + `TouchDragEvent`/`GestureEvent`/`SizeHint`/`MinimumSizeHint`/`FocusNextPrevChild`/`SetVisible`，另加 `XINPUTMETHOD_ON` 条件的 `InputMethodQuery`），`XWIDGET_VT_DISPATCH` 展开 = **28**（24 事件 + touch/tablet/touchDrag/gesture）。

4. **`TouchEvent`/`GestureEvent` 的「对标 QWidget」失实**：槽注释写「对标 QWidget::touchEvent」（XWidget.h:542）与「对标 QWidget::gestureEvent」（XWidget.h:551-555），但 Qt 6.8.3 `qwidget.h`/`qwidget.cpp` 中 `grep -c touchEvent` = **0**、`grep -c gestureEvent` = **0**（`touchEvent` 的虚函数在 `QWindow`，qwindow.h:345）；`tabletEvent` 才是 QWidget 的保护虚函数（qwidget.h:684）。三者的 Qt 对标关系应按此改写。

5. **touch/tablet/touchDrag/gesture 的 `_base` 入口未进保护头**：`XWidget_Protected.h` 只声明了 24 个事件 `_base`（:121-167），而 `XWidget_touchEvent_base`/`_tabletEvent_base`/`_touchDragEvent_base`/`_gestureEvent_base` 在 XWidget.c 中仅以 TU 内前置声明出现（XWidget.c:451-458）并被 `XWIDGET_VT_DISPATCH` 展开定义（:13955-13958）。跨编译单元的派生类无法调用这四个基类入口，与「子类覆写后回落基类」的既有用法不一致。

6. **`out/xwidget_align/` 六域矩阵是本轮修复前的快照，不能当现状引用**：`api-surface.md` 首段称 `XWidget.h`「2256 行」，实测 `wc -l` = **2701**；`geometry.md` #11 称 `XWidget_rect` 返回 `m_contentsRect`（错），现实现已返回全幅（XWidget.c:6368-6380）；`geometry.md` #3 称 `normalGeometry` 无 `isWindow` 门，现已三分支齐全（XWidget.c:6503-6525）；`focus.md` 仍以 `XWidget_setFocusReason`（h:1491）为对位，该符号已更名为 `XWidget_setFocus_2`（h:1763，`git diff` 中可见 `-void XWidget_setFocusReason(...)` 删除行）。引用这些矩阵时须以行号重新核到当前工作区。

7. **`DeleteOnClose` 属性只有存储没有消费**：属性注释承诺「关闭后删除对象」（XWidget.h:271），但 `XWidget_close` 只做「派发 CLOSE → 接受则 `setVisible(false)` → 返回」（XWidget.c:8920-8935），全库无 `XWidgetAttribute_DeleteOnClose` 的读取点（`rg -n DeleteOnClose Src/XGui/Widget/XWidget.c` 零命中）。这是**有意偏离**：Qt 在 qwidget.cpp:8580-8583 走 `deleteLater`，而 X 侧该位只挂在自删菜单上，通用消费会与 `XMenu` 自删形成双删（XGui.md:1698-1702）。

8. **`focusWidget` 语义与 Qt 不同，且矩阵与现状已对齐但头注释未改口**：Qt 返回窗口内 `focus_child` 记忆（qwidget.cpp:6867-6870），X 返回「本顶层窗口内的全局焦点控件，否则 NULL」（XWidget.c:9496-9504）。XGui.md:1684-1687 已把它登记为声明式偏差，per-window 记忆列 §8.2.2 专项（XGui.md:1819）。调用方若依赖「隐藏后仍记得上次焦点」会拿到 NULL。

9. **`setVisible` 的同值重复调用有真实副作用**：因不复刻 Qt 早退（上条坑点 2），重复 `setVisible(true)` 会重跑布局激活与脏区补画；实现注释指出这是刻意取舍（XWidget.c:8448-8455，实测早退会让竖条范围不随文本增高放大）。依赖「重复 show 零开销」的代码需自行去抖。

10. **`ensurePolished` 的幂等锚是 `m_polished` 布尔而非 Qt 的 `metaObject` 指针**：Qt 在 qwidget.cpp:10209-10215 比较 `m == d->polished`（元对象变化会重跑），X 只置位 `m_polished`（XWidget.c:8664-8674），并且 copy/move 会把该位一并搬运（XWidget.c:5579、5778）。因此**拷贝/移动出来的控件不会再跑一次 POLISH**，依赖 POLISH 重算边框/页签登记的派生件（XFrame.c:462、XSplitter.c:774）在拷贝构造场景下不会重新初始化。

11. **`_2` 里有字符串契约违规项，改名前不要批量扩散**：`XWidget_setWindowTitle_2(self, const char*)`（h:1598）按《代码风格…》「字符串 API 后缀契约」第 4 条应为 `_utf8`（`_2` 只表示同语义不同参数类型/个数，不得用于编码差异）；XGui.md:1813 已登记为全库 213 个同类项之一。新增 char* 设置入口请直接命名 `_utf8`。

12. **11 个借用型 getter 缺少 `_const`，按契约会被误判为「须释放的拷贝」**：`windowTitle`/`windowIconText`/`windowFilePath`/`toolTip`/`statusTip`/`whatsThis`/`accessibleName`/`accessibleDescription`/`windowRole`/`styleSheet`/`locale`（XGui.md:1811）。头注释虽以 `@warning` 写明「归对象所有、不得释放」，但《获取函数所有权契约》按后缀机械判定，改名未完成前调用方须逐条读注释。

13. **`XWidget_cursor` 的释放契约头文件未写**：头注释只写「查询光标；未设置时返回 ArrowCursor」（h:1941-1942），实现是 `XCursor_init` 后对自身/祖先光标 `XClassCopy` 深拷（XWidget.c:10892-10911）。`XCursor` 是 `XObject` 派生（XCursor.h:61-63），自定义位图/像素图光标随对象持有资源，按值对象惯例应以 `XClassDeinit` 收尾——与同族 `XWidget_font`（h:2052-2054 明写）不一致，属文档缺口而非实现缺陷。

14. **`XWidget_create` 是两参宏，无法零参调用**：`#define XWidget_create(parent, flags)`（XWidget.h:795），与风格文档「`XType_create()` 默认创建」形态不符（全库 368 个头文件同款宏，属库级约定）；需要零参语义须写 `XWidget_create(NULL, 0)`。

**测试覆盖**

- **无专属套件**：`Test/XGuiTest/XWidgetTest.c` 不存在（`ls` 返回「没有那个文件或目录」）。
- **`Test/XGuiTest/XWidgetApiTest.c`**（591 行，自带 `main`，XWidgetApi_Test 目标，`Test/CMakeLists.txt:770-784`（目标声明 :776-778、`add_test` :784），ctest 名 `XWidgetApiTest`）：入口 `main`（XWidgetApiTest.c:568）顺序调用 6 组——`wapi_test_api_surface` / `wapi_test_geometry_invariant` / `wapi_test_virtual_slots` / `wapi_test_ensure_polished` / `wapi_test_window_id` / `wapi_test_parent_chain_guards`；断言宏 `wapi_expect`（:36）共 **85** 个调用点（`rg -n "wapi_expect\(" | grep -v 定义行 | wc -l` = 85）；失败前缀 `[WAPI-FAIL]`；覆盖 NULL 入参、`rect` 全幅不变式、`focusNextPrevChild`/`setVisible` 虚槽覆写、`ensurePolished` 链、`winId`/`effectiveWinId` 转发。
- **`Test/XGuiTest/XWidgetQtAlignTest.c`**（2536 行，无 `main`，由回归主程序驱动）：入口 `XWidgetQtAlignTest_runAll`（XWidgetQtAlignTest.c:2500，声明于 XWidgetQtAlignTest.h:22）顺序跑 **10** 组，`qta_expect`（:69）共 **261** 个调用点（`rg -n "qta_expect\(" … | grep -vcE "定义行"` = 261）；分组实测：几何 33、可见性/窗口状态 29、焦点/可用性 29、树与坐标 12、绘制闭环 18、外观/动作 38、命名契约 21、批6 契约 9、二轮生命周期 44、二轮遍历 28；失败前缀 `[XWQ-FAIL]`。注意 `docs/xgui/XWidget_Qt683_对齐测试说明.md:16-17` 写「断言总数 174 条」「七组」，与当前代码（261 条 / 10 组）不符，该文档为上一轮快照。
- **间接覆盖**：仓库根 `xgui_regression_test.c` 的 `test_xgui_widgets()`（:36219）以 `expect_true(XWidgetQtAlignTest_runAll(), "XWidget Qt6.8 对齐专项")` 入门（:36246）；通用契约 `test_widget_contract()`（:22925-23348）含 **96** 个 `expect_*` 断言（几何钳位/属性位/焦点进出/close/copy-move），与对齐专项分工不重复（说明文档 §定位 L10-15 已述）。
- **本次未执行的检查**：本轮只做源码与测试文件的静态阅读，**未运行** `cmake --build`、**未运行** `./XGuiRegression_Test`、**未运行** `XWidgetApi_Test`；上述断言数均为 `rg` 静态计数，不是运行结果。

---

### Qt 6.8.3 对齐状态

**对齐要点**（Qt 行号均为 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/kernel/qwidget.cpp`，除注明 `qwidget.h`）

| 对齐点 | Qt 依据 | X 侧落点 |
|---|---|---|
| `rect()` 恒为控件**全幅** `(0,0,crect.w,crect.h)`，与内容边距无关 | inline qwidget.h:890-891 | `XWidget_rect` 返回 `m_windowRect` 宽高（XWidget.c:6368-6380）；`contentsRect` 才扣边距（与 qwidget.cpp:7705-7708 同构） |
| `normalGeometry()` 三分支：非窗口→空；非最大化/全屏→当前 geometry；否则→记忆值 | qwidget.cpp:3690-3700 | XWidget.c:6503-6525（三分支逐条对应，含 `m_isWindow` 门） |
| `setGeometry` 真变化时清 `Maximized|FullScreen` 并失效 `normalGeometry`，且受 `in_set_window_state` 闸保护 | qwidget.cpp:7283-7289（清位 7284-7285、窗口失效 7286-7287） | XWidget.c:6659-6673（`g_xwidgetInSetWindowState` 等价闸，XWidget.c:338/6668/8834） |
| `focusNextChild()` = `focusNextPrevChild(true)`，窗口级以**当前焦点控件**为起点、无焦点时回退顶层 | inline qwidget.h:728；qwidget.cpp:6816-6820；`QApplicationPrivate::focusNextPrevChild_helper` qapplication.cpp:1970-1978（`f = toplevel->focusWidget(); if (!f) f = toplevel;`） | XWidget.c:10049-10056（`_base(self,true)`）+ 9992-10016（`g_focusWidget` 在本窗则从它续链，否则从顶层起）；Tab/Backtab 原因透传见 :9971-9978 |
| `showNormal/showMinimized/showMaximized/showFullScreen` 用**位运算保留其余状态位**；minimized 清 `WindowActive`；fullscreen 尾补 `activateWindow` | qwidget.cpp:2869-2880 / 3043-3054 / 3066-3073 / 3082-3090 | XWidget.c:8698-8752（showNormal 8698-8709、showMinimized 8711-8726、showMaximized 8728-8738、showFullScreen 8740-8752；含 showMinimized 的「已最小化且可见则早退」与 showFullScreen 尾补 `activateWindow`） |
| `setWindowState`：最小化时清 `WindowActive`、同值早退、首次进入特殊态存 `normalGeometry`、派发状态变更 | qwidget.cpp:2959-2985（清 Active 2963-2964、早退 2966-2967、存 normalGeometry 2978-2979） | XWidget.c:8809-8872（清位 8821-8822、早退 8823、存几何 8825-8827、WINDOW_STATE_CHANGE 派发 8865-8870） |
| `ensurePolished` 链：置位→自身 Polish→递归子件→父 ChildPolished，幂等 | qwidget.cpp:10209-10235 | XWidget.c:8664-8696（四步同序）；生产接入点＝首显路径（对位 `show_helper` qwidget.cpp:7943 → XWidget.c:8460-8464）与 `adjustSize`（qwidget.cpp:8792 → XWidget.c:6848） |
| `setContentsMargins` 变更派发 `Resize`（可见时，载荷 crect.size）与 `ContentsRectChange`（闸外无条件），不可见时置 `WA_PendingResizeEvent` | qwidget.cpp:7628/7658（两重载）、`updateContentsRect` 7664-7683（可见 Resize 7673-7679、ContentsRectChange 7681-7682） | XWidget.c:6398-6440（可见/挂起分支 6419-6432、ContentsRectChange 6433-6437）、`_2` 重载 6442-6456；挂起位在 show 兑现（:8567-8586，对位 qwidget.cpp:7968-7971） |
| `setMask(const QBitmap&)` ≡ `setMask(QRegion(bitmap))`，置位像素构成遮罩 | qwidget.cpp:13192-13195 | XWidget.c:7828-7886（逐像素置位判定 + 复用区域版路径；空位图等价 clear） |
| `grab(rect)` 仅拒绝**交叠为空**的矩形（`!r.intersects(rect())`），不做「越界即 NULL」 | qwidget.cpp:5250-5258 | XWidget.c:13871-13909（与 rect() 求交钳位，交叠为空才 NULL；唯一差别：源矩形 0 宽/高直接 NULL，XWidget.c:13886，Qt 仅在 `width<0` 时走 prepare 分支） |
| `childAt` 三过滤：跳过 `WA_TransparentForMouseEvents`、父自身「矩形+遮罩」判定、子遮罩排除时 `continue` 试下一兄弟 | qwidget.cpp:10477-10502（跳过 10491、父判定 10482、子判定 10501-10502） | XWidget.c:7642-7721（透明位 7675-7679、父遮罩门 7648-7651、子遮罩 continue 7709-7718） |
| `setFocus` 落最深焦点代理；**不激活窗口**；窗口未激活时只记焦点诉求、激活后兑现 | qwidget.cpp:6535-6628（6540 代理、6572 `isActiveWindow()` 分流、6668-6670 `updateFocusChild`） | XWidget.c:9586-9673（代理 9599-9600、登记待激活靶 9620-9630、`XWidget_pendingFocusRedeem` 兑现见 Protected.h:303-317（声明 :317）） |
| `setFocusPolicy` 同步转发给焦点代理 | qwidget.cpp:7864-7865 | XWidget.c:9466-9484（沿代理链限深 64） |
| 禁用持焦控件时先 `focusNextChild()` 顺延，失败才 `clearFocus()` | qwidget.cpp:3398-3402 | XWidget.c:9414-9422 |
| `setParent` 双向传播新父的 `isEnabled`/`updatesEnabled`；持焦后代清焦；真实换父派发 `ParentChange` | qwidget.cpp:10826-10831（传播）、10775-10776（清焦）、10845-10846（ParentChange） | XWidget.c:7552-7579（双向传播）、7586-7588（清焦）、7623-7627（ParentChange） |
| `setVisible` 置 `WA_WState_ExplicitShowHide`（Qt 另在同值早退，X 有意不复刻） | qwidget.cpp:8293-8301（置位 8301 区段、早退 8300-8301） | XWidget.c:8448-8455（置位、注释说明不复刻早退） |
| `show()` 顶层按平台默认窗口态分流 FullScreen/Maximized | qwidget.cpp:7913-7929（7921-7927 分流） | XWidget.c:8619-8650（`XPlatformIntegration_defaultWindowState` 分流并路由到 `showFullScreen`/`showMaximized`） |
| `winId()` 必要时惰性建窗；`effectiveWinId()` 不建窗、沿 native parent 取 | qwidget.cpp:2377-2388 / 2482-2492 | XWidget.c:8134-8141（`XWindow_winId` 惰性建柄）/ 8143-8149（`XWindow_winIdIfCreated`） |
| `setLayout` 接管布局所有权（`l->setParent(this)`）与「重复设置保留首个 + qWarning」 | qwidget.cpp:10284/10289-10329（所有权文档 10284、保留首个 10295-10301、`setParent(this)` 10322） | 借用版 `setLayout` 按 Qt 告警并保留首个（XWidget.c:10485-10499）；Qt 所有权语义由 `setLayout_ref` + 接管登记表承担（XWidget.c:10510-10538） |

**豁免清单**（平台专属 / 架构裁剪 / 有意偏离 / 承载差异；逐条出处已在 XGui.md 登记）

- **平台专属/架构裁剪（不补 API）**：控件级 `style`/`setStyle`（走全局 `XApplication_setStyle`）、控件级 `screen`/`setScreen`、静态 `find(WId)`（另有 `XWidget_widgetForWindow` + `XApplication_topLevelWidgets`）、`actionEvent`、控件级 `nativeEvent`、Keypad 导航（`hasEditFocus`/`setEditFocus`）、`createWindowContainer`、GraphicsView 代理族、控件级快捷键四件套（`grabShortcut`/`releaseShortcut`/`setShortcutEnabled`/`setShortcutAutoRepeat`，由 `XShortcut` 承担）、`setTabOrder(initializer_list)`、`render(QPaintDevice*)` 目标重载、`QPointF` 浮点坐标族、`takeLayout`、Qt 内部接口 `createWinId`/`internalWinId`——全部见 XGui.md:1614-1653。
- **有意偏离（行为口径差）**：「父可见即自动可见」Hidden 模型（XGui.md:1657-1662）、托管控件不自动伸缩（1663-1665）、`heightForWidth` 无布局时返回提示高度（1666-1667）、控件级透明度为扩展（1668-1670）、Tab 键先 `keyPressEvent` 后遍历（1671-1674）、Tab 候选要求 `m_explicitShow`（1675-1677）、复合控件过滤口径（1678-1680）、`nextInFocusChain` 返回下一可 Tab 候选（1681-1683）、`focusWidget` 返回全局焦点（1684-1687，per-window 记忆另立 §8.2.2）、`mouseGrabber` 不含隐式按压抓取（1688-1691）、特殊状态位判定更严（1692-1694）、图标父链上溯终止条件（1695-1697）、`close` 不消费 `WA_DeleteOnClose`（1698-1702）、`overrideWindowState` 不派发事件（1703-1705）、`setSizePolicy` 两参只改两轴（XWidget.h:1215-1228 自述）。
- **平台边界（Qt 有下发、X 无桥接入口）**：`setWindowRole` 仅存储（XGui.md:1709-1711）、`setWindowIconText` 不推送平台（1712-1714）。
- **承载差异**：`fontMetrics`/`fontInfo` 返回 `XFont` 值拷贝（1718-1720）、`render` 仅等尺寸且无 RenderFlags/sourceRegion（1721-1723，XWidget.c:13790-13822 实读为等尺寸校验）、`visibleRegion` 不扣不透明子控件（1724-1727）、`childrenRegion` 不并入子控件遮罩（1728-1731，XWidget.c:8051-8073 实读仍只加矩形）、`XRect` 半开区间边界（1732-1733）、调色板无 resolve/色组选择（1734-1738，XWidget.c:11736-11763 实读为「自身或应用」二选一）、`foregroundRole` 恒返回设置值（1740-1742）、样式表 qproperty 采集范围限自身文本（1743-1744，XWidget.c:11442 起）、手势体系机制差异（1745-1747）。
- **X 扩展（Qt 无对应，保留）**：`framePos`（Qt 6.8 无，`rg framePos` 在 qtbase/src/widgets 零命中）、`unsetContentsMargins`、`titleBarWidget`、`hasMask`、`clearGestureRecognizers`（XGui.md:1749-1761）；另有 `childAtGlobal`、`setContentRotation`/`contentRotationDegrees`、静态内容保留层四件套、`setTouchMouseSynthesisEnabled`/`touchMouseSynthesisEnabled`、`widgetForWindow`、三对 `_const` 借用 getter（`mask_const`/`childrenRegion_const`/`visibleRegion_const`）+ `layout_const`、`isWindowModal`、`XWidget_ownedLayoutUnregister`/`XWidget_ownedLayoutTransfer`（Protected.h:386/398）——见 out/xwidget_align/api-surface.md §19（X 侧扩展面）与 visibility.md / tree.md 对应行。
- **对齐矩阵索引**：API 面 `out/xwidget_align/api-surface.md`（504 行，§21 汇总：对齐 241）；几何 `geometry.md`（151 行）；可见性/窗口状态 `visibility.md`（161 行）；焦点/可用性 `focus.md`（192 行）；控件树/坐标/绘制 `tree.md`（126 行）；外观/动作/输入法/命名 `appearance.md`（230 行）。测试侧缺口与已修缺陷见 `docs/xgui/XWidget_Qt683_对齐测试说明.md` §三（:117-136，两处实现缺陷）与 §四（:138-150，仍未覆盖的九项缺口，含 setMask 让出区的 PAINT 级验证、grab 的 IgnoreMask 像素级验证、焦点链全局 take/insert、per-window focus_child、restoreGeometry 平台回写等）。构建/回归门结果：`out/xwidget_align/build.exit` = 0、`reg.exit` = 0，`reg.log` 末行「XGui regression tests passed」（本次仅为读取既有产物，未重跑）。

### 本役修复记录

**根修 1：换父清焦（`XWidget_setParent`）**

- 第一轮（behavior_diff#88，记于 `docs/xgui/XWidget_Qt683_对齐测试说明.md:119-124`）：原实现在 `XObject_setParent` **落账之后**才取 `XWidget_topLevel(g_focusWidget)`，此时焦点控件的顶层已是新顶层，`oldTop != newTop` 恒假 → 清焦分支是死代码。修法：重挂前先捕获旧顶层（`focusOldTop`），判据改用旧值；抓出断言「换父跨窗口：被移子树内的焦点被显式清除」。
- 第二轮（批3 new-code#11，2026-11-06）：按 Qt qwidget.cpp:10775-10776 的**原始判据**删除「跨窗门」——`newParent && isAncestorOf(focusWidget()) && !(f & Qt::Window)` 不含 `oldTop != newTop`，同窗口内重挂持焦子树同样要清焦（子链折进新父的焦点链）。当前代码：`if (g_focusWidget && parentChanged && parent && !self->m_isWindow && XWidget_isAncestor(self, g_focusWidget)) XWidget_clearFocusBase(g_focusWidget, XFocusReason_Other);`（XWidget.c:7586-7588），注释明确写「旧顶层变量…随之无用途，删除」（XWidget.c:7528-7533）。
- **文档与代码的偏差**：`focusOldTop` 在当前实现中已 **0 次出现**（`rg -c focusOldTop Src/XGui/Widget/XWidget.c` 无输出），但测试说明 §三.1 仍以「捕获 focusOldTop」描述修复；且第一轮只覆盖跨窗场景，第二轮才覆盖同窗重挂——引用该文档时应按本条订正。
- 配套：换父后按新父双向传播 enabled/updates（XWidget.c:7542-7579，对位 qwidget.cpp:10826-10831）、真实换父收尾直发 `XEvent(PARENT_CHANGE)`（XWidget.c:7613-7627，对位 qwidget.cpp:10845-10846；因 `XObject` 的 `m_receive_parent_events` 全仓零写点导致原发送不可达，故收敛在控件侧直发）。

**根修 2：`setLayout_ref` 接管登记表**

- 根因（登记于 `docs/xgui/XWidget_Qt683_对齐测试说明.md:125-136` 与 XWidget.c:10395-10399）：原实现用 `XObject_setParent(layout, self)` 模拟 Qt 的 `l->setParent(this)`，但 `XLayoutItem` 只内嵌 `XClass`，**不是完整 XObject**（无 `m_parent`/`m_children`/`m_threadData`），该调用在线程亲和守卫（`XObject.c:202-204` 读 `m_threadData`）处静默早退 → 接管从未生效（布局泄漏）；守卫一旦变化还会向 `XLayout` 自有字段越界写。
- 修法：模块内接管登记表 `XWidgetOwnedLayout {widget, layout, next}` + `xwidget_ownedLayoutRegister/Remove/TakeFor`（XWidget.c:10400-10453，Register :10425-10436、Remove :10409-10422、TakeFor :10439-10453）；`setLayout_ref` 挂接后登记（XWidget.c:10510-10538）；`VXWidget_deinit` 析构时 `TakeFor` 摘账并 `XClassDelete`（XWidget.c:5297-5322，TakeFor 摘账 :5307、`XClassDelete` 见 :5316-5318 区段）。
- 二轮加固（2026-11-06）：① `TakeFor` **移出** `m_layout` 门——布局曾被绕过 `setLayout` 的路径摘挂时也要摘账，否则登记表留「(控件, 已释放布局)」悬垂项，地址复用后可撞等式误删借用布局；② 释放仍以 `m_layout` 作**等式见证**（`owned == mounted`），且 `mounted` 必须在 `XLayout_detachWidget` **之前**捕获（detach 会反向清宿主槽位）；③ 登记随布局挂接全路径同步：`XLayout_attachWidget` 抢旧宿主时 `XWidget_ownedLayoutUnregister`（Src/XGui/XLayout/XLayout.c:751-787，摘账点 :772 与 :781），`VXLayout_move` 走 `XWidget_ownedLayoutTransfer`（Protected.h:398）。
- 观测口径（测试说明 :132-136）：以派生守门布局 `QtaCanaryLayout` 的 deinit 置旗断言「接管布局随宿主销毁释放」，并设「借用布局不随宿主销毁」对照组；不用系统分配器字节统计（受 postEvent 队列惰性初始化干扰）。
- 配套交互：借用版 `setLayout` 与接管版同样执行 Qt 的「重复设不同布局 → 告警 + 保留首个」（XWidget.c:10485-10499 / 10514-10519）。

**本役新增 API**（`git diff Src/XGui/Widget/XWidget.h` 中新增声明；行号为当前头文件）

| API | 头文件 | 实现 | 说明 |
|---|---|---|---|
| `XWidget_winId` | h:1414 | c:8134-8141 | 控件级原生 id；沿顶层句柄两级跳转，未建柄时惰性建窗（对位 qwidget.cpp:2377-2388） |
| `XWidget_effectiveWinId` | h:1423 | c:8143-8149 | 只读变体，不建窗（`XWindow_winIdIfCreated`，对位 qwidget.cpp:2482-2492） |
| `XWidget_ensurePolished` | h:2351 | c:8664-8696 | polish 链四步 + 幂等；生产接入首显（c:8464）与 `adjustSize`（c:6848） |
| `XWidget_grab_2` | h:2500 | c:13871-13909 | 子矩形快照；求交钳位替代原「越界即 NULL」（对位 qwidget.cpp:5257） |
| `XWidget_setContentsMargins_2` | h:926 | c:6442-6456 | `const XMargins*` 重载；NULL 等价四参全 0（对位 qwidget.cpp:7658） |
| `XWidget_setMask_2` | h:1327 | c:7828-7886 | `XBitmap` 位图遮罩（对位 qwidget.cpp:13192-13195） |
| `XWidget_setParent_2` | h:1267 | c:7630-7640 | 两参换父，清窗口类型位（`flags & ~0xffu`），同父早退（对位 qwidget.cpp:10668-10673） |
| `XWidget_setFocus_2` | h:1763 | c:9586-9673 | 带原因 `setFocus`；`XWidget_setFocus` 变为它的 `Other` 包装（c:9512-9516）。**更名自** `XWidget_setFocusReason`（`git diff` 删除行） |
| `XWidget_setLayout_ref` | h:2586 | c:10510-10538 | 布局所有权接管（见根修 2）；与 `XWidget_layout_const`（h:2546 / c:10306）成对 |
| `XWidget_mask_const` | h:1298 | c:7750-7756 | 遮罩只读借用（零拷贝） |
| `XWidget_childrenRegion_const` | h:1356 | c:8075-8100 | 子控件区域借用（内部缓存覆写） |
| `XWidget_visibleRegion_const` | h:2400 | c:12118-12128 | 可见区域借用（内部缓存覆写） |
| `XWidget_grabMouse_2` | h:1859 | c:10156-10191 | 抓取期套光标 + 保存/还原抓取前自有光标（对位 `grabMouse(const QCursor&)` qwidget.h:444） |
| `XWidget_isWindowModal` | h:1563 | c:8794-8802 | 窗口级模态；与 `XWidget_isModal`（c:8783-8792，读自身）分工 |
| `XWidget_setGraphicsEffect_ref` | h:2623 | c:14003-14012 | 三态契约命名的接管版（转发 `setGraphicsEffect`） |
| `XWidget_updateMicroFocus` | h:2288 | c:11133-11145 | 输入法微焦点刷新（`XINPUTMETHOD_ON`） |
| `XWidget_setFocusPolicy` 代理转发增强 | h:1731 | c:9466-9484 | 沿焦点代理链同步策略位（批3 修复，非新增签名） |
| `XWidget_sizeHint_base` / `XWidget_minimumSizeHint_base` | h:1184 / h:1191 | c:7283 / c:7315 | 非虚基类求值，供覆写 Slots 的类回落（防递归） |

**其它同批修正（可观测语义变化，均带源码注释）**：`rect` 回归全幅（c:6368-6380）；`normalGeometry` 三分支（c:6503-6525）；`baseSize` 默认 (0,0)（getter c:7214-7223，init c:5034）；`toolTipDuration` 默认 -1（c:5058）；`contextMenuPolicy` 默认 `DefaultContextMenu`（c:5006-5008；`m_focusPolicy` 默认 NoFocus 同在 :5006）；`isModal` 改读自身（c:8783-8792）；`activateWindow` 不再污染 `windowState`（c:8905-8918）；`setWindowModality` 同步两位属性（c:8889-8903，属性置位 :8895-8899、平台下发 :8901-8902）；`setEnabled` 焦点顺延（c:9414-9422）；`setTabOrder` 链卫生（c:9722-9763）；`childAt` 三过滤（c:7642-7721）；`childrenRect`/`childrenRegion` 改按显式隐藏位（c:8018-8049 / 8051-8073）；`setWindowModified` 派发 MODIFIED_CHANGE 并重跑 `[*]`（c:9313-9330，派发 :9322、标题落地 :9330）；`setBaseSize`/`setSizeIncrement` 补平台转发（消除 `XWindow_setBaseSize`/`XWindow_setSizeIncrement` 死代码）；`setWindowTitle` 的 `[*]` 段计数算法移植（对位 qwidget.cpp:6017-6052）+ filePath 派生标题（对位 qwidget.cpp:5997-6003）。
## XAbstractButton

- **定位**：按钮族公共骨架——文本/图标、checkable/checked、down、自动重复、自动互斥、快捷键、click/animateClick 与四个信号，具体绘制留子类。继承链 XObject ← XWidget（`m_base` 首成员）。`Src/XGui/Widget/XAbstractButton.h:495`（实现 `XAbstractButton.c`，保护面 `XAbstractButton_Protected.h:165`）。
- **生命周期**：`XAbstractButton_create(parent, flags)` 宏（h:140）→ `XAbstractButton_create_ex`（h:151）；`XAbstractButton_init`（h:130）置默认文本空、图标空、checkable/checked/down/pressed=false、autoRepeat/autoExclusive=false、重复延迟 300ms、间隔 100ms（h:118-121；实值 `XAbstractButton.c:1395-1396`）；堆对象 `XClassDelete`、栈对象 `XClassDeinit`（h:126-129）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractButton_class_init(void)` | 初始化并返回类共享虚表 | — | 返回静态借用，禁释放（h:111-112） |
| `void XAbstractButton_init(XAbstractButton* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化，建立父借用 | `QAbstractButton(QWidget*)` qabstractbutton.h:41 | 不分配 self、不释放 parent |
| `XAbstractButton* XAbstractButton_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 指定内存类型堆创建 | X 扩展（C 适配） | 调用方拥有，`XClassDelete` |
| `XAbstractButton_create(parent, flags)`（宏，h:140） | 默认内存类型创建，展开为 `_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,…)` | X 扩展 | 调用方拥有，`XClassDelete` |
| `const XString* XAbstractButton_text(const XAbstractButton* self)` | 取按钮文本 | `text()` qabstractbutton.h:45 | 借用，禁释放/禁改 |
| `void XAbstractButton_setText(XAbstractButton* self, const XString* text)` | 深拷贝设置文本；NULL 等价空串 | `setText(const QString&)` qabstractbutton.h:44 | 入参借用，不转移 |
| `void XAbstractButton_setText_2(XAbstractButton* self, const char* utf8)` | UTF-8 解码后设置文本 | 同上（C 适配重载） | 入参借用，函数内复制 |
| `XIcon XAbstractButton_icon(const XAbstractButton* self)` | 按 XIcon 值语义返回图标副本 | `icon()` qabstractbutton.h:48 | **调用方拥有**，用毕 `XClassDeinit`（h:193-196） |
| `void XAbstractButton_setIcon(XAbstractButton* self, const XIcon* icon)` | 复制图标值；NULL/空清图标 | `setIcon(const QIcon&)` qabstractbutton.h:47 | 入参借用 |
| `XSize XAbstractButton_iconSize(const XAbstractButton* self)` | 显式图标尺寸；(0,0)=未设置 | `iconSize()` qabstractbutton.h:50 | 值语义 |
| `void XAbstractButton_setIconSize(XAbstractButton* self, const XSize* size)` | 设显式图标尺寸；NULL/非正清回默认 | `setIconSize(const QSize&)` qabstractbutton.h:82 | 入参借用 |
| `bool XAbstractButton_isCheckable(const XAbstractButton* self)` | 是否可选中 | `isCheckable()` qabstractbutton.h:58 | 值语义 |
| `void XAbstractButton_setCheckable(XAbstractButton* self, bool checkable)` | 设可选中；转 false 清 checked 但**不发** toggled | `setCheckable(bool)` qabstractbutton.h:57 | 值语义 |
| `bool XAbstractButton_isChecked(const XAbstractButton* self)` | 当前选中态 | `isChecked()` qabstractbutton.h:60 | 值语义 |
| `void XAbstractButton_setChecked(XAbstractButton* self, bool checked)` | 设选中态；变化发 toggled 并按 autoExclusive 取消同组 | `setChecked(bool)` qabstractbutton.h:86 | 值语义 |
| `void XAbstractButton_toggle(XAbstractButton* self)` | 非虚，等价 `setChecked(!checked)` | `toggle()` qabstractbutton.h:85 | 值语义 |
| `bool XAbstractButton_isDown(const XAbstractButton* self)` | 当前 down 态 | `isDown()` qabstractbutton.h:63 | 值语义 |
| `void XAbstractButton_setDown(XAbstractButton* self, bool down)` | 只改视觉/内部 down，不发 pressed/released/clicked | `setDown(bool)` qabstractbutton.h:62 | 值语义 |
| `bool XAbstractButton_autoRepeat(const XAbstractButton* self)` | 是否自动重复 | `autoRepeat()` qabstractbutton.h:66 | 值语义 |
| `void XAbstractButton_setAutoRepeat(XAbstractButton* self, bool repeat)` | 开关自动重复；关闭时停表 | `setAutoRepeat(bool)` qabstractbutton.h:65 | 值语义 |
| `int XAbstractButton_autoRepeatDelay(const XAbstractButton* self)` | 首重复延迟 ms | `autoRepeatDelay()` qabstractbutton.h:69 | 值语义 |
| `void XAbstractButton_setAutoRepeatDelay(XAbstractButton* self, int delay)` | 设首重复延迟（原样保存） | `setAutoRepeatDelay(int)` qabstractbutton.h:68 | 值语义 |
| `int XAbstractButton_autoRepeatInterval(const XAbstractButton* self)` | 重复间隔 ms | `autoRepeatInterval()` qabstractbutton.h:72 | 值语义 |
| `void XAbstractButton_setAutoRepeatInterval(XAbstractButton* self, int interval)` | 设重复间隔（原样保存） | `setAutoRepeatInterval(int)` qabstractbutton.h:71 | 值语义 |
| `bool XAbstractButton_autoExclusive(const XAbstractButton* self)` | 是否自动互斥 | `autoExclusive()` qabstractbutton.h:75 | 值语义 |
| `void XAbstractButton_setAutoExclusive(XAbstractButton* self, bool exclusive)` | 设自动互斥（同父可选中按钮联动） | `setAutoExclusive(bool)` qabstractbutton.h:74 | 入参不含所有权 |
| `void XAbstractButton_setShortcut(XAbstractButton* self, const XString* shortcut)` | 存文本并登记单键 XShortcut 绑定；NULL 清除并注销 | `setShortcut(const QKeySequence&)` qabstractbutton.h:53 | 入参借用 |
| `void XAbstractButton_setShortcut_2(XAbstractButton* self, const char* utf8)` | UTF-8 版本，转发主版本 | 同上（C 适配重载） | 入参借用 |
| `const XString* XAbstractButton_shortcut(const XAbstractButton* self)` | 取快捷键文本 | `shortcut()` qabstractbutton.h:54 | 借用，禁释放 |
| `void* XAbstractButton_group(const XAbstractButton* self)` | 所属按钮组不透明位（仅可做指针比较） | `group()` qabstractbutton.h:78 | 借用，禁解引用 |
| `void XAbstractButton_click(XAbstractButton* self)` | 程序化点击：pressed→down→nextCheckState→released→clicked | `click()` qabstractbutton.h:84 | 值语义 |
| `void XAbstractButton_animateClick(XAbstractButton* self)` | 立即 down+pressed，约 100ms 后释放并 clicked | `animateClick()` qabstractbutton.h:83 | 值语义 |
| `void* XAbstractButton_pressed_signal(XAbstractButton* self)` | 发 pressed | `pressed()` qabstractbutton.h:89 | 返回不透明 id，禁解引用 |
| `void* XAbstractButton_released_signal(XAbstractButton* self)` | 发 released | `released()` qabstractbutton.h:90 | 同上 |
| `void* XAbstractButton_clicked_signal(XAbstractButton* self, bool checked)` | 发 clicked(checked) | `clicked(bool)` qabstractbutton.h:91 | 同上 |
| `void* XAbstractButton_toggled_signal(XAbstractButton* self, bool checked)` | 发 toggled(checked) | `toggled(bool)` qabstractbutton.h:92 | 同上 |

- **所有权与命名契约**：借用族 `XAbstractButton_text`/`XAbstractButton_shortcut` 返回内部 `const XString*` 却无 `_const` 后缀（与 `out/xwidget_align/api-surface.md:488` 记录的 188 个同类函数同族）；设置侧 `setText_2`/`setShortcut_2` 是 UTF-8 重载但用 `_2` 而非 `_utf8`（api-surface.md:488 逐字点名 `XAbstractButton_setText_2`）；三态/布尔 set*：`setCheckable`/`setChecked`/`setDown`/`setAutoRepeat`/`setAutoExclusive` 均只接受 bool，无枚举三态。
- **可覆写点**：虚表槽 `CheckStateSet`、`NextCheckState`、`HitButton`、`ContentChanged`（h:60-65，均排在 `XCLASS_VTABLE_GET_SIZE(XWidget)` 之后）。默认行为：`checkStateSet` 空操作、`nextCheckState` 对可选中按钮反转 checked（`XAbstractButton.c:844-849`）、`hitButton` 判局部矩形命中（`c:866-874`）、`contentChanged` 空操作；分派入口 `XAbstractButton_checkStateSet_base`（`XAbstractButton_Protected.h:102`）、`XAbstractButton_nextCheckState_base`（Protected.h:121）、`XAbstractButton_hitButton_base`（Protected.h:144）、`XAbstractButton_contentChanged_base`（Protected.h:155），对应实现 `XAbstractButton.c:829/851/877/896`。
- **注意事项与坑点**：
  1. **头注释与实现矛盾（按钮组）**：`XAbstractButton.h:411-413` 写「按钮组体系未建：XButtonGroup 目前未回写该承载位，按钮未经登记时本接口返回 NULL」，但 `XButtonGroup.c:498-501` 已执行 `button->m_group = self;`，且 `XAbstractButton.c:625-627` 已在 `setChecked` 里读该位判定 exclusive 组语义。
  2. `setShortcut` 只登记文本末段单键（`"Ctrl+S"`→`XKey_S`），组合键修饰位不入 XShortcut 单键模型；`'&'` 助记符与 `'&&'` 转义未实现（h:370-376）。
  3. `toggle` 是非虚槽、只做 `setChecked(!checked)`，三态循环 `(state+1)%3` 只在 `nextCheckState` 虚槽（点击/自动重复路径）发生（h:266-273）。
  4. 公共 `setDown` 只改视觉态、不发 pressed/released/clicked（h:287-290）；输入事件路径另走私有函数同步信号——`XAbstractButton.c:179-182` 注释明确「公共 setDown() 不能发射 pressed/released，因此单独保留该内部路径」。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractButtonTest.c` 不存在）。间接覆盖：`XButtonGroupTest.c`（17 处引用，调 `XAbstractButton_setChecked`/`click`/`isChecked`/`group`）、`hsf_vfd_probe.c`（7）、`XGuiRemoteInputViewTest.c`（3）、`XGuiMirrorFidelityTest.c`（2）、`XGuiRemoteTest.c`（1）；demo 侧 `xgui_demo_apitest_buttons.c`（165）。

## XAbstractItemModel

- **定位**：内存二维条目模型——行/列数、单元格文本读写、行列表头、结构增删与排序、变化信号；QModelIndex 以 (row,column) 平面索引承载。继承链 XObject（`m_class` 首成员，`XCLASS_DEFINE_EXTEND_END(XAbstractItemModel, XObject)` h:48）。`Src/XGui/Widget/XAbstractItemModel.h:369`。
- **生命周期**：`XAbstractItemModel_init(self)` 建 0 行 0 列空模型（h:76-77）；`XAbstractItemModel_create_ex(memory)`（h:80）与 `XAbstractItemModel_create()` 宏（h:81-82）；析构走 XClass 虚表，单元格/表头元素（`XString*`）由对象拥有（h:52-59）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractItemModel_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XAbstractItemModel_init(XAbstractItemModel* self)` | 初始化空模型 | X 扩展（C 适配构造） | 不分配 self |
| `XAbstractItemModel* XAbstractItemModel_create_ex(XMemoryType memory)` | 指定内存类型创建 | X 扩展 | 调用方拥有 |
| `XAbstractItemModel_create()`（宏，h:81-82） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `int XAbstractItemModel_rowCount(const XAbstractItemModel* self)` | 行数；NULL→0 | `rowCount()` qabstractitemmodel.h:276 | 值语义 |
| `int XAbstractItemModel_columnCount(const XAbstractItemModel* self)` | 列数；NULL→0 | `columnCount()` qabstractitemmodel.h:277 | 值语义 |
| `void XAbstractItemModel_setDimension(XAbstractItemModel* self, int rows, int cols)` | 设维度；扩容补空、缩容裁尾 | X 扩展（Qt 经 insert/removeRows） | 值语义 |
| `void XAbstractItemModel_setRowCount(XAbstractItemModel* self, int rows)` | 只改行数（补空行） | X 扩展 | 值语义 |
| `void XAbstractItemModel_setColumnCount(XAbstractItemModel* self, int cols)` | 只改列数（补空列） | X 扩展 | 值语义 |
| `const XString* XAbstractItemModel_data(const XAbstractItemModel* self, int row, int col)` | 取单元格文本；空/越界 NULL | `data(index, role)` qabstractitemmodel.h:280（仅文本维） | 借用，禁释放（h:119） |
| `const char* XAbstractItemModel_data_2(const XAbstractItemModel* self, int row, int col)` | UTF-8 借用；空/越界/self NULL→`""` | 同上（C 适配重载） | 内部转换缓存借用，随下次修改失效（h:131-132） |
| `bool XAbstractItemModel_setData(XAbstractItemModel* self, int row, int col, const XString* value)` | 写单元格；成功发 dataChanged | `setData()` qabstractitemmodel.h:281 | 入参借用，内部深拷贝 |
| `bool XAbstractItemModel_setData_2(XAbstractItemModel* self, int row, int col, const char* value)` | UTF-8 写单元格，转发主版本 | 同上（C 适配重载） | 入参借用 |
| `const XString* XAbstractItemModel_headerData(const XAbstractItemModel* self, int section, int orientation)` | 取表头文本（0=列头 1=行头） | `headerData()` qabstractitemmodel.h:283 | 借用 |
| `const char* XAbstractItemModel_headerData_2(const XAbstractItemModel* self, int section, int orientation)` | UTF-8 表头；未设/越界→`""` | 同上（C 适配重载） | 借用缓存 |
| `bool XAbstractItemModel_setHeaderData(XAbstractItemModel* self, int section, int orientation, const XString* value)` | 写表头；成功发 headerDataChanged | `setHeaderData()` qabstractitemmodel.h:285 | 入参借用 |
| `bool XAbstractItemModel_setHeaderData_2(XAbstractItemModel* self, int section, int orientation, const char* value)` | UTF-8 写表头，转发主版本 | 同上（C 适配重载） | 入参借用 |
| `int XAbstractItemModel_parent(const XAbstractItemModel* self, int row, int col)` | 父索引；扁平模型恒 -1 | `parent()` qabstractitemmodel.h:273 | 值语义 |
| `int XAbstractItemModel_flags(const XAbstractItemModel* self, int row, int col)` | 条目标志位；有效格 = Selectable\|Enabled | `flags()` qabstractitemmodel.h:321 | 值语义 |
| `bool XAbstractItemModel_hasChildren(const XAbstractItemModel* self)` | 行、列均 >0 即有子级 | `hasChildren()` qabstractitemmodel.h:278 | 值语义 |
| `bool XAbstractItemModel_insertRows(XAbstractItemModel* self, int row, int count)` | row 前插 count 空行；发 rowsInserted | `insertRows()` qabstractitemmodel.h:301 | 值语义 |
| `bool XAbstractItemModel_removeRows(XAbstractItemModel* self, int row, int count)` | 自 row 删 count 行；越界收敛至末尾 | `removeRows()` qabstractitemmodel.h:303 | 值语义 |
| `bool XAbstractItemModel_insertColumns(XAbstractItemModel* self, int column, int count)` | 插列；发 columnsInserted | `insertColumns()` qabstractitemmodel.h:302 | 值语义 |
| `bool XAbstractItemModel_removeColumns(XAbstractItemModel* self, int column, int count)` | 删列；发 columnsRemoved | `removeColumns()` qabstractitemmodel.h:304 | 值语义 |
| `void XAbstractItemModel_sort(XAbstractItemModel* self, int column, int order)` | 按列文本字典序稳定重排；发 layoutChanged | `sort()` qabstractitemmodel.h:322（Qt 基类空实现） | 值语义 |
| `void* XAbstractItemModel_dataChanged_signal(XAbstractItemModel* self, int row, int col)` | 发 dataChanged(row,col) | `dataChanged()` qabstractitemmodel.h:356 | 不透明 id，禁解引用 |
| `void* XAbstractItemModel_modelReset_signal(XAbstractItemModel* self)` | 发 modelReset | `modelReset()` qabstractitemmodel.h:375 | 同上 |
| `void* XAbstractItemModel_rowsInserted_signal(XAbstractItemModel* self, int row, int count)` | 发 rowsInserted | `rowsInserted()` qabstractitemmodel.h:363 | 同上 |
| `void* XAbstractItemModel_rowsRemoved_signal(XAbstractItemModel* self, int row, int count)` | 发 rowsRemoved | `rowsRemoved()` qabstractitemmodel.h:366 | 同上 |
| `void* XAbstractItemModel_headerDataChanged_signal(XAbstractItemModel* self, int orientation, int first, int last)` | 发表头变化（单段 first==last） | `headerDataChanged()` qabstractitemmodel.h:358 | 同上 |
| `void* XAbstractItemModel_columnsInserted_signal(XAbstractItemModel* self, int column, int count)` | 发 columnsInserted | `columnsInserted()` qabstractitemmodel.h:369 | 同上 |
| `void* XAbstractItemModel_columnsRemoved_signal(XAbstractItemModel* self, int column, int count)` | 发 columnsRemoved | `columnsRemoved()` qabstractitemmodel.h:372 | 同上 |
| `void* XAbstractItemModel_layoutChanged_signal(XAbstractItemModel* self)` | 发 layoutChanged | `layoutChanged()` qabstractitemmodel.h:359 | 同上 |

- **所有权与命名契约**：`data`/`headerData` 为借用 `const XString*`、`data_2`/`headerData_2` 为借用 `const char*`，四个 getter 均无 `_const` 后缀（同 `api-surface.md:488` 记录的全库族）；四个 `_2` 是 UTF-8 兼容重载而非 `_utf8`；`setData`/`setHeaderData` 深拷贝入参（h:136-144、176-185），无三态 set*。
- **可覆写点**：本类无新增虚表槽——`XCLASS_DEFINE_EXTEND_END(XAbstractItemModel, XObject)`（h:47-48）直挂 XObject，也没有 `*_base` 入口；派生模型只能整体替换同名函数（非虚分派）。
- **注意事项与坑点**：
  1. **无 modelReset 触发点**：`xaim_emitReset` 只被 `XAbstractItemModel_modelReset_signal` 自身调用（`XAbstractItemModel.c:95`、`:558`），模型内任何结构变更都不发 modelReset；只有显式调用信号函数才发。
  2. **改列不发信号**：`setDimension` 只在行数变化时发 rowsInserted/rowsRemoved（`XAbstractItemModel.c:267-272`），列数变化无 columns* 通知；h:100 的「维度变化时发射 rowsInserted/rowsRemoved」与实际一致，但列维度静默这一点未在头注释提示。
  3. **空值口径不一致**：`data()` 空单元格/越界返回 NULL（h:123），`data_2()` 同类情况返回 `""`（h:131-132）；`headerData_2` 亦返回 `""`（h:171-172）。
  4. **历史 P0 已修**：`XGui.md:1228-1233` 记 `XAbstractItemModel_setDimension` 曾按当前列数 calloc 行数组、与 `xaim_growCols` 的 capCols 假设冲突，导致高列号写越界堆并 `XClassDelete` 垃圾指针（demo 条目视图页首屏崩溃）；现行实现改为 `XVector_resize_base` 单块存储（`XAbstractItemModel.c:238-273`），头文件 h:58 亦声明「不保留裸数组与容量字段」。
  5. **结构变更的时序坑**：`XGui.md:963-967` 记 XTreeWidget `clear` 里 bridgeSync 原置于信号发射前，`setDimension` 的 rowsRemoved 先重置基类选择/当前状态，抢跑导致信号丢失——模型结构变更须在信号链之后同步桥接数据。
  6. `flags` 不承载 `ItemIsEditable`（h:213-217：本库模型全格可写，编辑门禁在视图侧 `XAbstractItemView_itemsEditable`）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractItemModelTest.c` 不存在）。间接覆盖：`XTableAlignTest.c`（5 处）；demo 侧 `xgui_demo_apitest_views.c`（128）、`xgui_demo_page_views.c`（18）、`xgui_demo_apitest_text.c`（13）。

## XAbstractItemView

- **定位**：条目视图通用状态机——当前索引、选择模式/行为、编辑触发与编辑闭环（委托/持久编辑器/条目控件）、role 叠加数据、键盘搜索、滚动与尺寸提示；数据呈现由派生视图实现。继承链 XObject ← XWidget ← XFrame ← XAbstractScrollArea（`m_base` 首成员 h:181）。`Src/XGui/Widget/XAbstractItemView.h:1281`。
- **生命周期**：`XAbstractItemView_init`（h:264）建内建选择模型（`XAbstractItemView.c:311`）、默认 selectionMode=ExtendedSelection、editTriggers=DoubleClicked\|EditKeyPressed（c 同段）、m_itemsEditable=false；`XAbstractItemView_create(parent, flags)` 宏（h:278）/`_create_ex`（h:275）；析构经 `EXClass_Deinit` 虚表槽（class_init 注册 `VXAbstractItemView_deinit`）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractItemView_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化 | `QAbstractItemView(QWidget*)`（构造在 qabstractitemview.h 类内） | 不分配 self |
| `XAbstractItemView* XAbstractItemView_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XAbstractItemView_create(parent, flags)`（宏，h:278-279） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row, int column)` | 设当前格并按模式分派选择命令 | `setCurrentIndex()` itemviews/qabstractitemview.h:211 | 值语义 |
| `int XAbstractItemView_currentRow(const XAbstractItemView* self)` | 当前行；无→-1 | `currentIndex()` qabstractitemview.h:112（拆行） | 值语义 |
| `int XAbstractItemView_currentColumn(const XAbstractItemView* self)` | 当前列；无→-1 | 同上（拆列） | 值语义 |
| `bool XAbstractItemView_currentIndex(const XAbstractItemView* self, int* outRow, int* outCol)` | 双输出当前索引；有效返回 true | `currentIndex()` qabstractitemview.h:112 | 输出参数 |
| `void XAbstractItemView_reset(XAbstractItemView* self)` | 清当前索引/选择/根索引/编辑器承载并全量重绘 | `reset()` qabstractitemview.h:205 | 值语义 |
| `void XAbstractItemView_doItemsLayout(XAbstractItemView* self)` | 触发条目全量布局/重绘（本实现仅 update） | `doItemsLayout()` qabstractitemview.h:207 | 值语义 |
| `void XAbstractItemView_update(XAbstractItemView* self, int row, int col)` | 按 visualRect 与视口相交请求局部重绘 | `update(const QModelIndex&)` qabstractitemview.h:214 | 值语义 |
| `void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode)` | 设选择模式 | `setSelectionMode()` qabstractitemview.h:106 | 值语义 |
| `int XAbstractItemView_selectionMode(const XAbstractItemView* self)` | 取选择模式 | `selectionMode()` qabstractitemview.h:107 | 值语义 |
| `void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self, int behavior)` | 设选择行为 | `setSelectionBehavior()` qabstractitemview.h:109 | 值语义 |
| `int XAbstractItemView_selectionBehavior(const XAbstractItemView* self)` | 取选择行为 | `selectionBehavior()` qabstractitemview.h:110 | 值语义 |
| `void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers)` | 设编辑触发位组合 | `setEditTriggers()` qabstractitemview.h:115 | 值语义 |
| `int XAbstractItemView_editTriggers(const XAbstractItemView* self)` | 取编辑触发位 | `editTriggers()` qabstractitemview.h:116 | 值语义 |
| `bool XAbstractItemView_edit(XAbstractItemView* self, int row, int column)` | 进入编辑闭环（委托→createEditor→setEditorData→几何→显示授焦） | `edit(const QModelIndex&)` qabstractitemview.h:209 | 值语义 |
| `void XAbstractItemView_setItemsEditable(XAbstractItemView* self, bool editable)` | 条目可编辑总闸（替代 model flags 门禁） | X 扩展（Qt 无同名） | 值语义 |
| `bool XAbstractItemView_itemsEditable(const XAbstractItemView* self)` | 取可编辑总闸 | X 扩展 | 值语义 |
| `bool XAbstractItemView_isEditing(const XAbstractItemView* self)` | 是否有打开的编辑器 | X 扩展（h:417 自注） | 值语义 |
| `void XAbstractItemView_commitData(XAbstractItemView* self, XWidget* editor)` | 提交编辑器数据落库（幂等） | `commitData(QWidget*)` qabstractitemview.h:231 | editor 借用 |
| `void XAbstractItemView_closeEditor(XAbstractItemView* self, XWidget* editor, int hint)` | 关闭编辑器；hint≠RevertModelCache 走提交分支 | `closeEditor()` qabstractitemview.h:230 | editor 借用 |
| `void XAbstractItemView_setItemDelegate(XAbstractItemView* self, void* delegate)` | 设条目委托；NULL 恢复默认 | `setItemDelegate()` qabstractitemview.h:103 | 借用，不析构（h:459-461） |
| `void* XAbstractItemView_itemDelegate(const XAbstractItemView* self)` | 取条目委托 | `itemDelegate()` qabstractitemview.h:104 | 借用 |
| `void XAbstractItemView_setItemDelegateForColumn(XAbstractItemView* self, int column, void* delegate)` | 设列级委托；NULL 清除 | `setItemDelegateForColumn()` qabstractitemview.h:190 | 借用，不析构 |
| `void* XAbstractItemView_itemDelegateForColumn(const XAbstractItemView* self, int column)` | 取列级委托 | `itemDelegateForColumn()` qabstractitemview.h:191 | 借用 |
| `void XAbstractItemView_setItemDelegateForRow(XAbstractItemView* self, int row, void* delegate)` | 设行级委托；NULL 清除 | `setItemDelegateForRow()` qabstractitemview.h:187 | 借用，不析构 |
| `void* XAbstractItemView_itemDelegateForRow(const XAbstractItemView* self, int row)` | 取行级委托 | `itemDelegateForRow()` qabstractitemview.h:188 | 借用 |
| `void* XAbstractItemView_itemDelegateForIndex(const XAbstractItemView* self, int row, int col)` | 按 行级→列级→默认 解析委托 | `itemDelegateForIndex()` qabstractitemview.h:198 | 借用 |
| `const char* XAbstractItemView_itemText(const XAbstractItemView* self, int row, int col, int role)` | 读 role 文本（Display/Edit 走模型） | `data(index, role)` qabstractitemmodel.h:280 | 借用 UTF-8 |
| `bool XAbstractItemView_setItemText(XAbstractItemView* self, int row, int col, int role, const char* text)` | 写 role 文本（仅 Display/Edit） | `setData(index,value,role)` qabstractitemmodel.h:281 | 入参借用 |
| `int XAbstractItemView_itemCheckState(const XAbstractItemView* self, int row, int col)` | 读复选态；未设/越界→-1 | `CheckStateRole`（Qt 无 -1 约定） | 值语义 |
| `void XAbstractItemView_setItemCheckState(XAbstractItemView* self, int row, int col, int state)` | 写复选态；<0 清除 | 同上 | 值语义 |
| `int XAbstractItemView_itemTextAlignment(const XAbstractItemView* self, int row, int col)` | 读文本对齐；未设→0 | `TextAlignmentRole` | 值语义 |
| `void XAbstractItemView_setItemTextAlignment(XAbstractItemView* self, int row, int col, int alignment)` | 写文本对齐；0 清除 | 同上 | 值语义 |
| `const XFont* XAbstractItemView_itemFont(const XAbstractItemView* self, int row, int col)` | 读条目字体 | `FontRole` | 借用，禁释放 |
| `void XAbstractItemView_setItemFont(XAbstractItemView* self, int row, int col, const XFont* font)` | 写条目字体；NULL 清除 | 同上 | 借用，生命周期归调用方（h:630） |
| `const void* XAbstractItemView_itemDecoration(const XAbstractItemView* self, int row, int col)` | 读装饰（通常 XImage*） | `DecorationRole` | 借用 |
| `void XAbstractItemView_setItemDecoration(XAbstractItemView* self, int row, int col, const void* decoration)` | 写装饰；NULL 清除 | 同上 | 借用，不转移 |
| `void XAbstractItemView_openPersistentEditor(XAbstractItemView* self, int row, int col)` | 打开持久编辑器（仅标记） | `openPersistentEditor()` qabstractitemview.h:180 | 值语义 |
| `void XAbstractItemView_closePersistentEditor(XAbstractItemView* self, int row, int col)` | 关闭持久编辑器标记 | `closePersistentEditor()` qabstractitemview.h:181 | 值语义 |
| `bool XAbstractItemView_isPersistentEditorOpen(const XAbstractItemView* self, int row, int col)` | 查询持久编辑器标记 | `isPersistentEditorOpen()` qabstractitemview.h:182 | 值语义 |
| `void XAbstractItemView_setIndexWidget(XAbstractItemView* self, int row, int col, void* widget)` | 设单元格条目控件；NULL 清除 | `setIndexWidget()` qabstractitemview.h:184 | 借用，不接管所有权（h:699-704） |
| `void* XAbstractItemView_indexWidget(const XAbstractItemView* self, int row, int col)` | 取条目控件 | `indexWidget()` qabstractitemview.h:185 | 借用 |
| `bool XAbstractItemView_keyboardSearch(XAbstractItemView* self, const char* text)` | 2000ms 前缀累积 + 环形前缀匹配并移动当前项 | `keyboardSearch()` qabstractitemview.h:170 | 入参借用 |
| `void XAbstractItemView_setTabKeyNavigation(XAbstractItemView* self, bool enable)` | 设 Tab 键条目导航（仅状态） | `setTabKeyNavigation()` qabstractitemview.h:132 | 值语义 |
| `bool XAbstractItemView_tabKeyNavigation(const XAbstractItemView* self)` | 取 Tab 导航开关 | `tabKeyNavigation()` qabstractitemview.h:133 | 值语义 |
| `void XAbstractItemView_setAlternatingRowColors(XAbstractItemView* self, bool enable)` | 设交替行色 | `setAlternatingRowColors()` qabstractitemview.h:161 | 值语义 |
| `bool XAbstractItemView_alternatingRowColors(const XAbstractItemView* self)` | 取交替行色 | `alternatingRowColors()` qabstractitemview.h:162 | 值语义 |
| `void XAbstractItemView_setAutoScroll(XAbstractItemView* self, bool enable)` | 设自动滚动 | `setAutoScroll()` qabstractitemview.h:126 | 值语义 |
| `bool XAbstractItemView_hasAutoScroll(const XAbstractItemView* self)` | 取自动滚动 | `hasAutoScroll()` qabstractitemview.h:127 | 值语义 |
| `void XAbstractItemView_setAutoScrollMargin(XAbstractItemView* self, int margin)` | 设边缘判定边距 | `setAutoScrollMargin()` qabstractitemview.h:129 | 值语义 |
| `int XAbstractItemView_autoScrollMargin(const XAbstractItemView* self)` | 取边距 | `autoScrollMargin()` qabstractitemview.h:130 | 值语义 |
| `void XAbstractItemView_setTextElideMode(XAbstractItemView* self, int mode)` | 设省略模式 | `setTextElideMode()` qabstractitemview.h:167 | 值语义 |
| `int XAbstractItemView_textElideMode(const XAbstractItemView* self)` | 取省略模式 | `textElideMode()` qabstractitemview.h:168 | 值语义 |
| `const char* XAbstractItemView_elidedText(const XAbstractItemView* self, char* buf, int bufCap, const char* text, int maxW, const XFont* font)` | 按 textElideMode 省略；够宽原样返回 text | X 扩展（Qt 用 QFontMetrics::elidedText） | buf 调用方缓冲；font 借用 |
| `bool XAbstractItemView_shouldAutoScroll(const XAbstractItemView* self, int x, int y)` | 指针是否落四边任一边缘带 | X 扩展（对齐私有 shouldAutoScroll） | 值语义 |
| `void XAbstractItemView_startAutoScroll(XAbstractItemView* self)` | 启动边缘自动滚动定时器 | X 扩展（对齐私有 startAutoScroll） | 值语义 |
| `void XAbstractItemView_stopAutoScroll(XAbstractItemView* self)` | 停止并清累计拍数 | X 扩展（对齐私有 stopAutoScroll） | 值语义 |
| `void XAbstractItemView_doAutoScroll(XAbstractItemView* self)` | 执行一拍边缘滚动；推不动时停表 | `doAutoScroll()` qabstractitemview.h:296 | 值语义 |
| `void XAbstractItemView_syncScrollSteps(XAbstractItemView* self)` | 按滚动模式同步滚动条 singleStep | X 扩展（对齐 itemviewChangeSingleStep） | 值语义 |
| `void XAbstractItemView_updateAutoScrollPointer(XAbstractItemView* self, int x, int y)` | 记录指针位置并按需启停自动滚动 | X 扩展（对齐私有 draggedPosition） | 值语义 |
| `void XAbstractItemView_setVerticalScrollMode(XAbstractItemView* self, int mode)` | 设垂直滚动模式 | `setVerticalScrollMode()` qabstractitemview.h:118 | 值语义 |
| `int XAbstractItemView_verticalScrollMode(const XAbstractItemView* self)` | 取垂直滚动模式 | `verticalScrollMode()` qabstractitemview.h:119 | 值语义 |
| `void XAbstractItemView_setHorizontalScrollMode(XAbstractItemView* self, int mode)` | 设水平滚动模式 | `setHorizontalScrollMode()` qabstractitemview.h:122 | 值语义 |
| `int XAbstractItemView_horizontalScrollMode(const XAbstractItemView* self)` | 取水平滚动模式 | `horizontalScrollMode()` qabstractitemview.h:123 | 值语义 |
| `void XAbstractItemView_resetVerticalScrollMode(XAbstractItemView* self)` | 复位为 ScrollPerItem | `resetVerticalScrollMode()` qabstractitemview.h:120 | 值语义 |
| `void XAbstractItemView_resetHorizontalScrollMode(XAbstractItemView* self)` | 复位为 ScrollPerItem | `resetHorizontalScrollMode()` qabstractitemview.h:124 | 值语义 |
| `void XAbstractItemView_setDragEnabled(XAbstractItemView* self, bool enable)` | 设允许拖出（仅状态） | `setDragEnabled()` qabstractitemview.h:139 | 值语义 |
| `bool XAbstractItemView_dragEnabled(const XAbstractItemView* self)` | 取允许拖出 | `dragEnabled()` qabstractitemview.h:140 | 值语义 |
| `void XAbstractItemView_setDragDropMode(XAbstractItemView* self, int mode)` | 设拖放模式（仅状态） | `setDragDropMode()` qabstractitemview.h:154 | 值语义 |
| `int XAbstractItemView_dragDropMode(const XAbstractItemView* self)` | 取拖放模式 | `dragDropMode()` qabstractitemview.h:155 | 值语义 |
| `void XAbstractItemView_setDragDropOverwriteMode(XAbstractItemView* self, bool overwrite)` | 设拖放覆盖模式（仅状态） | `setDragDropOverwriteMode()` qabstractitemview.h:142 | 值语义 |
| `bool XAbstractItemView_dragDropOverwriteMode(const XAbstractItemView* self)` | 取覆盖模式 | `dragDropOverwriteMode()` qabstractitemview.h:143 | 值语义 |
| `void XAbstractItemView_setDefaultDropAction(XAbstractItemView* self, int action)` | 设默认拖放动作（仅状态） | `setDefaultDropAction()` qabstractitemview.h:157 | 值语义 |
| `int XAbstractItemView_defaultDropAction(const XAbstractItemView* self)` | 取默认拖放动作 | `defaultDropAction()` qabstractitemview.h:158 | 值语义 |
| `void XAbstractItemView_setDropIndicatorShown(XAbstractItemView* self, bool enable)` | 设拖放指示器显示（仅状态） | `setDropIndicatorShown()` qabstractitemview.h:136 | 值语义 |
| `bool XAbstractItemView_showDropIndicator(const XAbstractItemView* self)` | 取指示器开关 | `showDropIndicator()` qabstractitemview.h:137 | 值语义 |
| `int XAbstractItemView_sizeHintForColumn(const XAbstractItemView* self, int column)` | 列尺寸提示（visualRect 最大宽）；空/越界 -1 | `sizeHintForColumn()` qabstractitemview.h:178 | 值语义 |
| `int XAbstractItemView_sizeHintForRow(const XAbstractItemView* self, int row)` | 行尺寸提示（最大高）；空/越界 -1 | `sizeHintForRow()` qabstractitemview.h:177 | 值语义 |
| `bool XAbstractItemView_sizeHintForIndex(const XAbstractItemView* self, int row, int column, int* outWidth, int* outHeight)` | 单元格尺寸提示（双 int 输出） | `sizeHintForIndex()` qabstractitemview.h:176 | 输出参数 |
| `void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column)` | 滚动到指定格并保证可见 | `scrollTo(index, EnsureVisible)` qabstractitemview.h:173 | 值语义 |
| `void XAbstractItemView_scrollToHint(XAbstractItemView* self, int row, int column, int hint)` | 按 ScrollHint 定位滚动 | `scrollTo(index, hint)` qabstractitemview.h:173 | 值语义 |
| `void XAbstractItemView_scrollToTop(XAbstractItemView* self)` | 垂直滚动条置 minimum | `scrollToTop()` qabstractitemview.h:212 | 值语义 |
| `void XAbstractItemView_scrollToBottom(XAbstractItemView* self)` | 垂直滚动条置 maximum | `scrollToBottom()` qabstractitemview.h:213 | 值语义 |
| `XAbstractItemModel* XAbstractItemView_model(const XAbstractItemView* self)` | 取数据模型 | `model()` qabstractitemview.h:98 | 借用（h:182） |
| `void XAbstractItemView_setModel(XAbstractItemView* self, XAbstractItemModel* model)` | 设模型并同步选择模型维度 | `setModel()` qabstractitemview.h:97 | 借用，不取得所有权 |
| `XItemSelectionModel* XAbstractItemView_selectionModel(const XAbstractItemView* self)` | 取选择模型 | `selectionModel()` qabstractitemview.h:101 | 借用 |
| `void XAbstractItemView_setSelectionModel(XAbstractItemView* self, XItemSelectionModel* selectionModel)` | 替换选择模型并**接管所有权**（删旧、NULL 重新懒建） | `setSelectionModel()` qabstractitemview.h:100 | 转移（h:1161-1163） |
| `void XAbstractItemView_clearSelection(XAbstractItemView* self)` | 清全部选中，当前索引不变 | `clearSelection()` qabstractitemview.h:210 | 值语义 |
| `void XAbstractItemView_selectAll(XAbstractItemView* self)` | 全选（按模式收敛） | `selectAll()` qabstractitemview.h:208 | 值语义 |
| `void XAbstractItemView_setRootIndex(XAbstractItemView* self, int row, int col)` | 存根索引偏移（树预留） | `setRootIndex()` qabstractitemview.h:206 | 值语义 |
| `bool XAbstractItemView_rootIndex(const XAbstractItemView* self, int* outRow, int* outCol)` | 取存储的根索引 | `rootIndex()` qabstractitemview.h:113 | 输出参数 |
| `bool XAbstractItemView_indexAt_base(const XAbstractItemView* self, int x, int y, int* outRow, int* outCol)` | 命中测试虚分派入口（基类默认网格） | `indexAt()` qabstractitemview.h:174 | 输出参数 |
| `bool XAbstractItemView_visualRect(const XAbstractItemView* self, int row, int col, XRect* out)` | 条目几何虚分派入口（默认 80×24 网格） | `visualRect()` qabstractitemview.h:172 | 输出参数 |
| `void XAbstractItemView_setIconSize(XAbstractItemView* self, int w, int h)` | 设图标尺寸并发 iconSizeChanged | `setIconSize()` qabstractitemview.h:164 | 值语义 |
| `void XAbstractItemView_iconSize(const XAbstractItemView* self, int* outWidth, int* outHeight)` | 双输出图标尺寸 | `iconSize()` qabstractitemview.h:165 | 输出参数 |
| `void* XAbstractItemView_pressed_signal(XAbstractItemView* self, int row, int col)` | 发 pressed(row,col) | `pressed()` qabstractitemview.h:235 | 不透明 id |
| `void* XAbstractItemView_clicked_signal(XAbstractItemView* self, int row, int col)` | 发 clicked(row,col) | `clicked()` qabstractitemview.h:236 | 不透明 id |
| `void* XAbstractItemView_doubleClicked_signal(XAbstractItemView* self, int row, int col)` | 发 doubleClicked | `doubleClicked()` qabstractitemview.h:237 | 不透明 id |
| `void* XAbstractItemView_activated_signal(XAbstractItemView* self, int row, int col)` | 发 activated | `activated()` qabstractitemview.h:239 | 不透明 id |
| `void* XAbstractItemView_entered_signal(XAbstractItemView* self, int row, int col)` | 发 entered | `entered()` qabstractitemview.h:240 | 不透明 id |
| `void* XAbstractItemView_viewportEntered_signal(XAbstractItemView* self)` | 发 viewportEntered | `viewportEntered()` qabstractitemview.h:241 | 不透明 id |
| `void* XAbstractItemView_iconSizeChanged_signal(XAbstractItemView* self, int width, int height)` | 发 iconSizeChanged | `iconSizeChanged()` qabstractitemview.h:243 | 不透明 id |

- **所有权与命名契约**：`m_model` 借用（h:182）、`m_selectionModel` 视图拥有（h:183）且 `setSelectionModel` 明写「接管所有权」（h:1161-1163，实现删旧 `XAbstractItemView.c:1822-1836`）；`m_editor`/`m_defaultDelegate` 拥有，列/行/条目委托与 indexWidget 一律借用不析构（h:459-467、486-489、699-704）；`itemFont`/`itemDecoration`/`itemText` 返回借用。命名侧本类无 `_const`/`_utf8` 后缀族，`_base` 仅 `indexAt_base` 一个；`_2` 重载无。
- **可覆写点**：虚表槽 `IndexAt`、`VisualRect`、`ShouldEdit`（h:167-171，排在 `XCLASS_VTABLE_GET_SIZE(XAbstractScrollArea)` 之后）。默认行为：`IndexAt` 按 `y/24,x/80` 网格命中（`XAbstractItemView.c:1899-1918`），入口 `XAbstractItemView_indexAt_base`（h:1220，c:1920-1931 查槽后调用）；`VisualRect` 默认 `(col*80,row*24,80,24)`（c:1933-1939），入口 `XAbstractItemView_visualRect`（h:1229，c:1952-1964 虚分派）；`ShouldEdit` 默认返回 `m_itemsEditable`（c:1944-1950），**无公开 `_base` 入口**，只在 `edit()` 内联取槽（c:632-638）。
- **注意事项与坑点**：
  1. **头注释与实现矛盾（visualRect 是否虚分派）**：`XAbstractItemView.h:1061`、`:1074`、`:1092` 三处 note 写「visualRect 当前非虚分派」，实际 `XAbstractItemView_visualRect` 已按 `EXAbstractItemView_VisualRect` 取槽分派（`XAbstractItemView.c:1952-1964`），派生视图覆写即生效。
  2. **头注释与实现矛盾（rootIndex）**：h:1196-1209 写「平铺模型无层级，根索引恒为 (0,0)（同 Qt）…self 为空置 0」，实现返回存储的 `m_rootRow/m_rootCol`（c:1885-1895），init/reset 置 -1（c:320-321、475-476）——空视图得到 (-1,-1)。
  3. **悬空注释**：h:281-283 两条 `/** @brief 析构入口…*/`、`/** @brief 删除堆上视图…*/` 之后没有任何声明（与 `out/xwidget_align/api-surface.md:489` 记录的 `XWidget.h:736-737` 同类悬空）。
  4. **注释未解码**：h:885-894 五段文档是字面 `\uXXXX` 转义序列（`sed -n '883,896p'` 可见 `\u6309\u5f53...`），不是中文。
  5. **`ShouldEdit` 缺 `_base` 入口**：`IndexAt`/`VisualRect` 都有公开入口（h:1220、h:1229），`ShouldEdit` 只能由虚表内联触发，派生类无法在自身实现里显式调基类默认判定。
  6. **行/列级委托设了不生效**：h:486-489、511-513 明写「不析构、不虚分派，也不参与条目绘制/编辑分派」——但 `xaiv_resolveDelegate` 会先取 `itemDelegateForIndex`（`XAbstractItemView.c:573-577`），即编辑闭环实际会消费该指针。
  7. **editTriggers 缺省曾偏差**：init 注释记缺省为 `DoubleClicked|EditKeyPressed`（对标 Qt，除 QTableView 外全部视图），此前多出 CurrentChanged；`XGui.md:438-439` 记同一批修复（§8 条目视图三连①）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractItemViewTest.c` 不存在）。间接覆盖：`XTableAlignTest.c`（24 处，含编辑/选择/role 断言）、`XKeyboardTest.c`（5）；demo 侧 `xgui_demo_apitest_views.c`（282）、`xgui_demo_page_modbus.c`（24）。

## XAbstractScrollArea

- **定位**：滚动区域骨架——viewport 子控件、垂直/水平滚动条、滚动条策略、cornerWidget、视口边距、内容尺寸驱动滚动，以及 X 扩展的触摸跟手/惯性滚动。继承链 XObject ← XWidget ← XFrame（`m_base` 首成员 h:61）。`Src/XGui/Widget/XAbstractScrollArea.h:301`（保护面 `XAbstractScrollArea_Protected.h:53`）。
- **生命周期**：`XAbstractScrollArea_init`（h:113）、`XAbstractScrollArea_create(parent, flags)` 宏（h:115）、`_create_ex`（h:116）；viewport 与两条滚动条由对象拥有（h:62-64），`setViewport`/`setVerticalScrollBar`/`setHorizontalScrollBar` 接管新件所有权并删旧（h:191-192、219-225）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractScrollArea_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化（建视口/双滚动条） | `QAbstractScrollArea(QWidget*)` qabstractscrollarea.h:30 | 不分配 self |
| `XAbstractScrollArea* XAbstractScrollArea_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XAbstractScrollArea_create(parent, flags)`（宏，h:115） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `XWidget* XAbstractScrollArea_viewport(const XAbstractScrollArea* self)` | 取内容承载视口 | `viewport()` qabstractscrollarea.h:56 | 借用（对象拥有，禁释放） |
| `XScrollBar* XAbstractScrollArea_verticalScrollBar(const XAbstractScrollArea* self)` | 取垂直滚动条 | `verticalScrollBar()` qabstractscrollarea.h:42 | 借用 |
| `XScrollBar* XAbstractScrollArea_horizontalScrollBar(const XAbstractScrollArea* self)` | 取水平滚动条 | `horizontalScrollBar()` qabstractscrollarea.h:47 | 借用 |
| `void XAbstractScrollArea_setVerticalScrollBarPolicy(XAbstractScrollArea* self, XScrollBarPolicy policy)` | 设垂直策略并重排 | `setVerticalScrollBarPolicy()` qabstractscrollarea.h:41 | 值语义 |
| `XScrollBarPolicy XAbstractScrollArea_verticalScrollBarPolicy(const XAbstractScrollArea* self)` | 取垂直策略 | `verticalScrollBarPolicy()` qabstractscrollarea.h:40 | 值语义 |
| `void XAbstractScrollArea_setHorizontalScrollBarPolicy(XAbstractScrollArea* self, XScrollBarPolicy policy)` | 设水平策略并重排 | `setHorizontalScrollBarPolicy()` qabstractscrollarea.h:46 | 值语义 |
| `XScrollBarPolicy XAbstractScrollArea_horizontalScrollBarPolicy(const XAbstractScrollArea* self)` | 取水平策略 | `horizontalScrollBarPolicy()` qabstractscrollarea.h:45 | 值语义 |
| `void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self, XWidget* widget)` | 设右下角件：新件 reparent+show，旧件隐藏不销毁 | `setCornerWidget()` qabstractscrollarea.h:51 | 借用（h:65） |
| `void XAbstractScrollArea_setViewportMargins(XAbstractScrollArea* self, int left, int top, int right, int bottom)` | 设视口四向边距；负值钳 0 | `setViewportMargins(int,...)` qabstractscrollarea.h:71（protected） | 值语义 |
| `XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self)` | 取角件 | `cornerWidget()` qabstractscrollarea.h:50 | 借用 |
| `void XAbstractScrollArea_setContentSize(XAbstractScrollArea* self, int width, int height)` | 设内容尺寸驱动滚动范围 | X 扩展（Qt 由子类 setupViewport 承担） | 值语义 |
| `void XAbstractScrollArea_setVerticalScrollBar(XAbstractScrollArea* self, XScrollBar* scrollbar)` | 替换垂直条（接管新条、删旧条） | `setVerticalScrollBar()` qabstractscrollarea.h:43 | 转移（接管所有权） |
| `void XAbstractScrollArea_setHorizontalScrollBar(XAbstractScrollArea* self, XScrollBar* scrollbar)` | 替换水平条 | `setHorizontalScrollBar()` qabstractscrollarea.h:48 | 转移 |
| `void XAbstractScrollArea_addScrollBarWidget(XAbstractScrollArea* self, XWidget* widget, int alignment)` | 滚动条旁附加控件（上限 8，去重） | `addScrollBarWidget()` qabstractscrollarea.h:53 | 注释称归调用方（h:204），实现改父链（c:1061） |
| `const XWidget** XAbstractScrollArea_scrollBarWidgets(const XAbstractScrollArea* self, int alignment)` | 返回附加控件借用数组 | `scrollBarWidgets()` qabstractscrollarea.h:54 | 借用数组，禁释放 |
| `void XAbstractScrollArea_setViewport(XAbstractScrollArea* self, XWidget* widget)` | 替换视口（接管所有权，旧视口 deleteLater） | `setViewport()` qabstractscrollarea.h:57 | 转移 |
| `XSize XAbstractScrollArea_maximumViewportSize(const XAbstractScrollArea* self)` | 视口最大可用尺寸 | `maximumViewportSize()` qabstractscrollarea.h:58 | 值语义 |
| `XSize XAbstractScrollArea_sizeHint(const XAbstractScrollArea* self)` | 尺寸提示 | `sizeHint()` qabstractscrollarea.h:62 | 值语义 |
| `XSize XAbstractScrollArea_minimumSizeHint(const XAbstractScrollArea* self)` | 最小尺寸提示 | `minimumSizeHint()` qabstractscrollarea.h:60 | 值语义 |
| `XAbstractScrollAreaSizeAdjustPolicy XAbstractScrollArea_sizeAdjustPolicy(const XAbstractScrollArea* self)` | 取尺寸自适应策略 | `sizeAdjustPolicy()` qabstractscrollarea.h:66 | 值语义 |
| `void XAbstractScrollArea_setSizeAdjustPolicy(XAbstractScrollArea* self, XAbstractScrollAreaSizeAdjustPolicy policy)` | 设策略并清尺寸提示缓存 | `setSizeAdjustPolicy()` qabstractscrollarea.h:67 | 值语义 |
| `void XAbstractScrollArea_setTouchScrollEnabled(XAbstractScrollArea* self, bool enabled)` | 触摸拖动滚动开关（默认 true） | X 扩展 | 值语义 |
| `bool XAbstractScrollArea_touchScrollEnabled(const XAbstractScrollArea* self)` | 取触摸滚动开关 | X 扩展 | 值语义 |

- **所有权与命名契约**：viewerport/滚动条拥有（h:62-64），`m_cornerWidget` 与 `m_sbWidgets[8]` 为借用（h:65-66）；`setViewport`/`setVerticalScrollBar`/`setHorizontalScrollBar` 是「引用转移」语义；`scrollBarWidgets` 返回 `const XWidget**` 借用数组。本类无 `_const`/`_utf8`/`_2` 命名族；无三态 set*。
- **可覆写点**：虚表槽仅 `ScrollContentsBy`（h:52-54，排在 `XCLASS_VTABLE_GET_SIZE(XFrame)` 之后）。默认实现 `VX_asa_scrollContentsBy`（`XAbstractScrollArea.c:743`，class_init 注册于 c:803），按 dx/dy 平移视口内容；分派入口 `XAbstractScrollArea_scrollContentsBy_base`（Protected.h:31；实现见 c：子类未覆盖时不调用，注释「避免空槽指针崩溃」）。另有 `XAbstractScrollArea_resizeEvent_base`（Protected.h:37，直调父类槽、不经虚表）与 `XAbstractScrollArea_stopTouchScroll`（Protected.h:42）。
- **注意事项与坑点**：
  1. **头注释与实现矛盾（NULL 删条）**：h:191-192 写「替换垂直滚动条…NULL 表示删除现有条」，实现首行判空直接返回——`XAbstractScrollArea.c:1014-1020` 注释：「对标 Qt 6.8 首行判空拒绝（qWarning 后 return）：此前接受 NULL——删旧条后视口恒 w-16 留 16px 死列且滚动静默失效」。
  2. **头注释与实现矛盾（附加控件所有权）**：h:204 写「在滚动条旁附加控件（对标 addScrollBarWidget；控件归调用方）」，实现 `c:1061` 调 `XWidget_setParent(widget, (XWidget*)self, 0)` 改父链——与「归调用方」的表述冲突。
  3. **数组无 NUL 终止保证**：h:215 说返回数组「最多 8 个，末尾 NULL」；`m_sbWidgets` 只有 8 槽、init 全零（`c:56` 的 `XMemset`），附加满 8 个后第 9 个位置不存在，末尾无终止符。
  4. **矩阵条目已过时**：`out/xwidget_align/focus.md:152`（R1）记 `XAbstractScrollArea.c:848` 与 `:852` 连续两次相同 `setFocusProxy`；现行源码只剩 `c:852` 一次，`c:848-851` 注释声明「【redundant#4 2026-11-05】…已删重复的那一份，零行为变化」。
  5. **触摸滚动与旧结论冲突**：`XGui.md:663-664` 记「触摸滚动：不做——触摸→mouse 合成无来源标志」；本类已加 `m_touchScrollEnabled`/惯性定时器（h:86-107，init `c:872` 默认 true）。边界：滚动条把手按下即 grabMouse，DragBegin 不会询问到本区（h:281-283）。
  6. **尺寸自适应策略仅承载状态**：h:248-249、263-264 两处 note 明写「AdjustToContents 的自适应布局…未接：策略当前仅承载状态，仅影响 sizeHint 查询」。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractScrollAreaTest.c` 不存在）。间接覆盖：`XTouchMultiPointTest.c`（12）、`XScrollTouchProbe.c`（10）、`xscroll_clip_probe.c`（3）、`XGuiRemoteInputViewTest.c`（3）、`XTableAlignTest.c`（2）。

## XAbstractSlider

- **定位**：滑块族公共骨架——范围/值/句柄位置、单步/翻页步进、跟踪与按下态、方向/翻转、triggerAction、长按重复、滚轮与键盘步进。继承链 XObject ← XWidget（`m_base` 首成员 h:126）。`Src/XGui/Widget/XAbstractSlider.h:365`。
- **生命周期**：`XAbstractSlider_init`（h:165）置 min=0/max=99/value=0/position=0/水平/pageStep=10/singleStep=1/tracking=true（`XAbstractSlider.c` init 段；头文件 h:108-116 同口径）；`XAbstractSlider_create(parent, flags)` 宏（h:168）、`_create_ex`（h:177）；另有 31 条 XWidget 转发宏（h:185-214）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractSlider_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XAbstractSlider_init(XAbstractSlider* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化 | `QAbstractSlider(QWidget*)` qabstractslider.h:34 | 不分配 self |
| `XAbstractSlider* XAbstractSlider_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XAbstractSlider_create(parent, flags)`（宏，h:168-169） | 默认内存类型创建，展开为 `_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,…)` | X 扩展 | 调用方拥有 |
| `XAbstractSlider_setEnabled(self, enabled)`（宏，h:185） | 1:1 转发到 `XWidget_setEnabled` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_isEnabled(self)`（宏，h:186） | 1:1 转发到 `XWidget_isEnabled` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setVisible(self, visible)`（宏，h:187） | 1:1 转发到 `XWidget_setVisible` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_isVisible(self)`（宏，h:188） | 1:1 转发到 `XWidget_isVisible` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_show(self)`（宏，h:189） | 1:1 转发到 `XWidget_show` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_hide(self)`（宏，h:190） | 1:1 转发到 `XWidget_hide` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_raise(self)`（宏，h:191） | 1:1 转发到 `XWidget_raise` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_lower(self)`（宏，h:192） | 1:1 转发到 `XWidget_lower` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setGeometry(self, x, y, w, h)`（宏，h:193） | 1:1 转发到 `XWidget_setGeometry` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setGeometryRect(self, rect)`（宏，h:194） | 1:1 转发到 `XWidget_setGeometryRect` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_x(self)`（宏，h:195） | 1:1 转发到 `XWidget_x` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_y(self)`（宏，h:196） | 1:1 转发到 `XWidget_y` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_width(self)`（宏，h:197） | 1:1 转发到 `XWidget_width` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_height(self)`（宏，h:198） | 1:1 转发到 `XWidget_height` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_resize(self, w, h)`（宏，h:199） | 1:1 转发到 `XWidget_resize` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_move(self, x, y)`（宏，h:200） | 1:1 转发到 `XWidget_move` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_update(self)`（宏，h:201） | 1:1 转发到 `XWidget_update` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_updateRect(self, rect)`（宏，h:202） | 1:1 转发到 `XWidget_updateRect` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setParent(self, parent, flags)`（宏，h:203） | 1:1 转发到 `XWidget_setParent` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_parentWidget(self)`（宏，h:204） | 1:1 转发到 `XWidget_parentWidget` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setFocus(self)`（宏，h:205） | 1:1 转发到 `XWidget_setFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_clearFocus(self)`（宏，h:206） | 1:1 转发到 `XWidget_clearFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_hasFocus(self)`（宏，h:207） | 1:1 转发到 `XWidget_hasFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setFocusPolicy(self, policy)`（宏，h:208） | 1:1 转发到 `XWidget_setFocusPolicy` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setAttribute(self, attribute, on)`（宏，h:209） | 1:1 转发到 `XWidget_setAttribute` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_testAttribute(self, attribute)`（宏，h:210） | 1:1 转发到 `XWidget_testAttribute` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setContentsMargins(self, l, t, r, b)`（宏，h:211） | 1:1 转发到 `XWidget_setContentsMargins` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_contentsMargins(self)`（宏，h:212） | 1:1 转发到 `XWidget_contentsMargins` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_setWindowFlags(self, flags)`（宏，h:213） | 1:1 转发到 `XWidget_setWindowFlags` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSlider_updateGeometry(self)`（宏，h:214） | 1:1 转发到 `XWidget_updateGeometry` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `int XAbstractSlider_orientation(const XAbstractSlider* self)` | 取方向 | `orientation()` qabstractslider.h:37 | 值语义 |
| `void XAbstractSlider_setOrientation(XAbstractSlider* self, int orientation)` | 设方向（校验 1/2）并 update+updateGeometry | `setOrientation()` qabstractslider.h:83 | 值语义 |
| `int XAbstractSlider_minimum(const XAbstractSlider* self)` | 取下限 | `minimum()` qabstractslider.h:40 | 值语义 |
| `void XAbstractSlider_setMinimum(XAbstractSlider* self, int min)` | 设下限；超过上限时上限收敛 | `setMinimum()` qabstractslider.h:39 | 值语义 |
| `int XAbstractSlider_maximum(const XAbstractSlider* self)` | 取上限 | `maximum()` qabstractslider.h:43 | 值语义 |
| `void XAbstractSlider_setMaximum(XAbstractSlider* self, int max)` | 设上限；低于下限时下限收敛 | `setMaximum()` qabstractslider.h:42 | 值语义 |
| `void XAbstractSlider_setRange(XAbstractSlider* self, int min, int max)` | 设范围（min>max 时 max 收敛为 min） | `setRange()` qabstractslider.h:84 | 值语义 |
| `int XAbstractSlider_singleStep(const XAbstractSlider* self)` | 取单步步进 | `singleStep()` qabstractslider.h:46 | 值语义 |
| `void XAbstractSlider_setSingleStep(XAbstractSlider* self, int step)` | 设单步并通知 SliderStepsChange | `setSingleStep()` qabstractslider.h:45 | 值语义 |
| `int XAbstractSlider_pageStep(const XAbstractSlider* self)` | 取翻页步进 | `pageStep()` qabstractslider.h:49 | 值语义 |
| `void XAbstractSlider_setPageStep(XAbstractSlider* self, int step)` | 设翻页步进并通知 SliderStepsChange | `setPageStep()` qabstractslider.h:48 | 值语义 |
| `bool XAbstractSlider_hasTracking(const XAbstractSlider* self)` | 取拖动跟踪 | `hasTracking()` qabstractslider.h:52 | 值语义 |
| `void XAbstractSlider_setTracking(XAbstractSlider* self, bool enable)` | 设拖动跟踪 | `setTracking()` qabstractslider.h:51 | 值语义 |
| `void XAbstractSlider_setSliderDown(XAbstractSlider* self, bool down)` | 按下/抬起发 sliderPressed/sliderReleased；抬起且位置≠值时提交 | `setSliderDown()` qabstractslider.h:54 | 值语义 |
| `bool XAbstractSlider_isSliderDown(const XAbstractSlider* self)` | 取按下态 | `isSliderDown()` qabstractslider.h:55 | 值语义 |
| `int XAbstractSlider_sliderPosition(const XAbstractSlider* self)` | 取句柄位置 | `sliderPosition()` qabstractslider.h:58 | 值语义 |
| `void XAbstractSlider_setSliderPosition(XAbstractSlider* self, int position)` | 设句柄位置；tracking 时提交到值，按下时发 sliderMoved | `setSliderPosition()` qabstractslider.h:57 | 值语义 |
| `bool XAbstractSlider_invertedAppearance(const XAbstractSlider* self)` | 取外观翻转 | `invertedAppearance()` qabstractslider.h:61 | 值语义 |
| `void XAbstractSlider_setInvertedAppearance(XAbstractSlider* self, bool inverted)` | 设外观翻转并重绘 | `setInvertedAppearance()` qabstractslider.h:60 | 值语义 |
| `bool XAbstractSlider_invertedControls(const XAbstractSlider* self)` | 取控制翻转 | `invertedControls()` qabstractslider.h:64 | 值语义 |
| `void XAbstractSlider_setInvertedControls(XAbstractSlider* self, bool inverted)` | 翻转键盘/滚轮方向 | `setInvertedControls()` qabstractslider.h:63 | 值语义 |
| `int XAbstractSlider_value(const XAbstractSlider* self)` | 取当前值 | `value()` qabstractslider.h:77 | 值语义 |
| `void XAbstractSlider_setValue(XAbstractSlider* self, int value)` | 经 bound 收敛、同步位置并视情发 valueChanged | `setValue()` qabstractslider.h:82 | 值语义 |
| `void XAbstractSlider_triggerAction(XAbstractSlider* self, int action)` | 按动作调位置，发 actionTriggered 后提交到值 | `triggerAction()` qabstractslider.h:79 | 值语义 |
| `void XAbstractSlider_setRepeatAction(XAbstractSlider* self, int action, int thresholdTime, int repeatTime)` | 设长按重复动作与阈值/间隔（含定时器布防） | `setRepeatAction()` qabstractslider.h:100（protected） | 值语义 |
| `int XAbstractSlider_repeatAction(const XAbstractSlider* self)` | 取重复动作 | `repeatAction()` qabstractslider.h:101（protected） | 值语义 |
| `void XAbstractSlider_stepBy_base(XAbstractSlider* self, int steps)` | 步进虚分派入口（基类默认钳位步进+发信号） | Qt 无 stepBy（X 扩展，对齐内部步进语义） | 值语义 |
| `void XAbstractSlider_sliderChange_base(XAbstractSlider* self, int change)` | 变化通知虚分派入口（基类仅 update） | `sliderChange()` qabstractslider.h:109（protected） | 值语义 |
| `int XAbstractSlider_stepEnabled_base(XAbstractSlider* self)` | 步进使能虚分派入口（按边界拼位） | Qt 无 stepEnabled（X 扩展） | 值语义 |
| `int XAbstractSlider_bound_base(XAbstractSlider* self, int val)` | 取值收敛虚分派入口（默认钳位 [min,max]） | Qt 私有 `QAbstractSliderPrivate::bound`（无公开声明） | 值语义 |
| `void* XAbstractSlider_valueChanged_signal(XAbstractSlider* self, int value)` | 发 valueChanged(新值) | `valueChanged(int)` qabstractslider.h:87 | 不透明 id |
| `void* XAbstractSlider_sliderPressed_signal(XAbstractSlider* self)` | 发 sliderPressed | `sliderPressed()` qabstractslider.h:89 | 不透明 id |
| `void* XAbstractSlider_sliderMoved_signal(XAbstractSlider* self, int position)` | 发 sliderMoved(位置) | `sliderMoved(int)` qabstractslider.h:90 | 不透明 id |
| `void* XAbstractSlider_sliderReleased_signal(XAbstractSlider* self)` | 发 sliderReleased | `sliderReleased()` qabstractslider.h:91 | 不透明 id |
| `void* XAbstractSlider_rangeChanged_signal(XAbstractSlider* self, int min, int max)` | 发 rangeChanged(min,max) | `rangeChanged(int,int)` qabstractslider.h:93 | 不透明 id |
| `void* XAbstractSlider_actionTriggered_signal(XAbstractSlider* self, int action)` | 发 actionTriggered(动作码) | `actionTriggered(int)` qabstractslider.h:95 | 不透明 id |

- **所有权与命名契约**：全 API 值语义（int/bool），无指针返回、无 `_const`/`_utf8`/`_2` 族；三态 set* 无（`setOrientation`/`setButtonSymbols` 式枚举校验写入在此类为 `setOrientation` 校验 1/2，非法忽略）。
- **可覆写点**：虚表槽 `StepBy`、`SliderChange`、`StepEnabled`、`Bound`（h:98-103，排在 `XCLASS_VTABLE_GET_SIZE(XWidget)` 之后）。默认行为：`stepBy` 经 `bound` 收敛、同步 position，按下时发 sliderMoved、变化发 valueChanged（`VXAbstractSlider_stepBy`）；`sliderChange` 忽略 change 仅 `XWidget_update`；`stepEnabled` 按 `value>min`/`value<max` 拼 StepDown/StepUp（`VXAbstractSlider_stepEnabled`）；`bound` 钳位到 [min,max]。分派入口 `stepBy_base`（h:310/c:832）、`sliderChange_base`（h:319/c:841）、`stepEnabled_base`（h:327/c:852）、`bound_base`（h:339/c:119）。
- **注意事项与坑点**：
  1. **头注释与实现矛盾（长按重复定时器）**：h:20-22 与 h:285-287 两处写「长按自动重复的 timer 为后续扩展项」，实现已落地——`timerEvent` 按阈值到期后改按间隔连发（`XAbstractSlider.c:209-220`），`setRepeatAction` 重新布防（`c:798-822`，注释「[长按连发 2026-10-07] 定时器落地」）。
  2. **新增虚槽必须走 ADD_FUNC_LIST**：`class_init` 顶部注释明写「XVTABLE_OVERLOAD 只写槽位不更新 size，派生类（XSlider）经 append_vtable 继承时会把新槽遗漏为 NULL」——四个槽用 `XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots)` 追加。
  3. **stepBy/stepEnabled 在 Qt 无对应公开 API**：`grep -n "stepBy\|stepEnabled" qabstractslider.h` 无命中；h:300-327 的「对标」措辞指「本项目约定的步进核心 / Qt 侧内部语义」，不是 Qt 公开面。
  4. **滚轮余数口径**：不足一格（120）的增量不入账（h:139-143 引 `xslider_scrollByDelta` 注，与 Qt `offset_accumulated` 的 qreal 累加不同）。
  5. `sliderChange` 的 change 参数基类忽略（`VXAbstractSlider_sliderChange` 仅 `(void)change; update`），头 h:312-314 描述一致。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractSliderTest.c` 不存在）。间接覆盖：`XSliderTest.c`（202 处，入口 `XSliderTest_runAll` :146）、`XScrollBarTest.c`（9）、`XDialTest.c`（2）、`hsf_vfd_probe.c`（3）、`XTableAlignTest.c`（5）。

## XAbstractSpinBox

- **定位**：数值微调框骨架——内嵌 XLineEdit、按钮符号/frame/keyboardTracking/correctionMode/wrapping/accelerated/千分位、specialValueText、步进与校验/修正/清空虚槽、键盘与滚轮步进、editingFinished。继承链 XObject ← XWidget（`m_base` 首成员 h:115）。`Src/XGui/Widget/XAbstractSpinBox.h:324`。
- **生命周期**：`XAbstractSpinBox_init`（h:140）建内嵌编辑框并接线（`m_lineEdit` 拥有，h:116）；`XAbstractSpinBox_create_ex`（h:147）——**本类没有 `XAbstractSpinBox_create(...)` 宏**（全头 `grep -n "XAbstractSpinBox_create"` 仅命中 h:147）；`setLineEdit` 换件时旧件 `deleteLater`。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XAbstractSpinBox_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XAbstractSpinBox_init(XAbstractSpinBox* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化 | `QAbstractSpinBox(QWidget*)` qabstractspinbox.h:37 | 不分配 self |
| `XAbstractSpinBox* XAbstractSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XAbstractSpinBox_setEnabled(self, enabled)`（宏，h:151） | 1:1 转发到 `XWidget_setEnabled` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_isEnabled(self)`（宏，h:152） | 1:1 转发到 `XWidget_isEnabled` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setVisible(self, visible)`（宏，h:153） | 1:1 转发到 `XWidget_setVisible` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_isVisible(self)`（宏，h:154） | 1:1 转发到 `XWidget_isVisible` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_show(self)`（宏，h:155） | 1:1 转发到 `XWidget_show` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_hide(self)`（宏，h:156） | 1:1 转发到 `XWidget_hide` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_raise(self)`（宏，h:157） | 1:1 转发到 `XWidget_raise` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_lower(self)`（宏，h:158） | 1:1 转发到 `XWidget_lower` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setGeometry(self, x, y, w, h)`（宏，h:159） | 1:1 转发到 `XWidget_setGeometry` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setGeometryRect(self, rect)`（宏，h:160） | 1:1 转发到 `XWidget_setGeometryRect` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_x(self)`（宏，h:161） | 1:1 转发到 `XWidget_x` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_y(self)`（宏，h:162） | 1:1 转发到 `XWidget_y` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_width(self)`（宏，h:163） | 1:1 转发到 `XWidget_width` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_height(self)`（宏，h:164） | 1:1 转发到 `XWidget_height` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_resize(self, w, h)`（宏，h:165） | 1:1 转发到 `XWidget_resize` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_move(self, x, y)`（宏，h:166） | 1:1 转发到 `XWidget_move` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_update(self)`（宏，h:167） | 1:1 转发到 `XWidget_update` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_updateRect(self, rect)`（宏，h:168） | 1:1 转发到 `XWidget_updateRect` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setParent(self, parent, flags)`（宏，h:169） | 1:1 转发到 `XWidget_setParent` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_parentWidget(self)`（宏，h:170） | 1:1 转发到 `XWidget_parentWidget` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setFocus(self)`（宏，h:171） | 1:1 转发到 `XWidget_setFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_clearFocus(self)`（宏，h:172） | 1:1 转发到 `XWidget_clearFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_hasFocus(self)`（宏，h:173） | 1:1 转发到 `XWidget_hasFocus` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setFocusPolicy(self, policy)`（宏，h:174） | 1:1 转发到 `XWidget_setFocusPolicy` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setAttribute(self, attribute, on)`（宏，h:175） | 1:1 转发到 `XWidget_setAttribute` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_testAttribute(self, attribute)`（宏，h:176） | 1:1 转发到 `XWidget_testAttribute` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setContentsMargins(self, l, t, r, b)`（宏，h:177） | 1:1 转发到 `XWidget_setContentsMargins` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_contentsMargins(self)`（宏，h:178） | 1:1 转发到 `XWidget_contentsMargins` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_setWindowFlags(self, flags)`（宏，h:179） | 1:1 转发到 `XWidget_setWindowFlags` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XAbstractSpinBox_updateGeometry(self)`（宏，h:180） | 1:1 转发到 `XWidget_updateGeometry` | 同 `QWidget` 对应方法 | 入参原样转发 |
| `XLineEdit* XAbstractSpinBox_lineEdit(const XAbstractSpinBox* self)` | 取内嵌编辑框 | `lineEdit()` qabstractspinbox.h:129 | 借用（对象拥有） |
| `void XAbstractSpinBox_setLineEdit(XAbstractSpinBox* self, XLineEdit* lineEdit)` | 替换编辑框；NULL 时内部新建；旧件延迟回收 | `setLineEdit()` qabstractspinbox.h:130 | 转移（接管新件） |
| `int XAbstractSpinBox_buttonSymbols(const XAbstractSpinBox* self)` | 取按钮符号 | `buttonSymbols()` qabstractspinbox.h:47 | 值语义 |
| `void XAbstractSpinBox_setButtonSymbols(XAbstractSpinBox* self, int symbols)` | 设按钮符号并重绘 | `setButtonSymbols()` qabstractspinbox.h:48 | 值语义 |
| `void XAbstractSpinBox_setCorrectionMode(XAbstractSpinBox* self, int mode)` | 设提交修正模式 | `setCorrectionMode()` qabstractspinbox.h:53 | 值语义 |
| `int XAbstractSpinBox_correctionMode(const XAbstractSpinBox* self)` | 取修正模式 | `correctionMode()` qabstractspinbox.h:54 | 值语义 |
| `bool XAbstractSpinBox_hasAcceptableInput(const XAbstractSpinBox* self)` | 当前文本是否通过校验 | `hasAcceptableInput()` qabstractspinbox.h:56 | 值语义 |
| `const char* XAbstractSpinBox_text(const XAbstractSpinBox* self)` | 取编辑框当前文本（含前后缀） | `text()` qabstractspinbox.h:57 | 借用 |
| `const char* XAbstractSpinBox_specialValueText(const XAbstractSpinBox* self)` | 取特殊值文本（未设→`""`） | `specialValueText()` qabstractspinbox.h:59 | 借用 |
| `void XAbstractSpinBox_setSpecialValueText(XAbstractSpinBox* self, const char* text)` | 设特殊值文本；空串关闭 | `setSpecialValueText()` qabstractspinbox.h:60 | 入参借用，内部复制 |
| `bool XAbstractSpinBox_wrapping(const XAbstractSpinBox* self)` | 取循环步进开关 | `wrapping()` qabstractspinbox.h:62 | 值语义 |
| `void XAbstractSpinBox_setWrapping(XAbstractSpinBox* self, bool wrapping)` | 设循环步进 | `setWrapping()` qabstractspinbox.h:63 | 值语义 |
| `bool XAbstractSpinBox_isReadOnly(const XAbstractSpinBox* self)` | 取只读（转发编辑框） | `isReadOnly()` qabstractspinbox.h:66 | 值语义 |
| `void XAbstractSpinBox_setReadOnly(XAbstractSpinBox* self, bool readOnly)` | 设只读（转发编辑框） | `setReadOnly()` qabstractspinbox.h:65 | 值语义 |
| `bool XAbstractSpinBox_keyboardTracking(const XAbstractSpinBox* self)` | 取键盘跟踪 | `keyboardTracking()` qabstractspinbox.h:69 | 值语义 |
| `void XAbstractSpinBox_setKeyboardTracking(XAbstractSpinBox* self, bool tracking)` | 设键盘跟踪（每次键入即提交） | `setKeyboardTracking()` qabstractspinbox.h:68 | 值语义 |
| `int XAbstractSpinBox_alignment(const XAbstractSpinBox* self)` | 取文本对齐（转发编辑框） | `alignment()` qabstractspinbox.h:72 | 值语义 |
| `void XAbstractSpinBox_setAlignment(XAbstractSpinBox* self, int alignment)` | 设文本对齐 | `setAlignment()` qabstractspinbox.h:71 | 值语义 |
| `bool XAbstractSpinBox_hasFrame(const XAbstractSpinBox* self)` | 取边框开关（默认 true） | `hasFrame()` qabstractspinbox.h:75 | 值语义 |
| `void XAbstractSpinBox_setFrame(XAbstractSpinBox* self, bool on)` | 设边框并重绘 | `setFrame()` qabstractspinbox.h:74 | 值语义 |
| `void XAbstractSpinBox_setAccelerated(XAbstractSpinBox* self, bool on)` | 设加速开关（字段级，行为未接） | `setAccelerated()` qabstractspinbox.h:77 | 值语义 |
| `bool XAbstractSpinBox_isAccelerated(const XAbstractSpinBox* self)` | 取加速开关 | `isAccelerated()` qabstractspinbox.h:78 | 值语义 |
| `void XAbstractSpinBox_setGroupSeparatorShown(XAbstractSpinBox* self, bool shown)` | 设千分位并刷新显示 | `setGroupSeparatorShown()` qabstractspinbox.h:80 | 值语义 |
| `bool XAbstractSpinBox_isGroupSeparatorShown(const XAbstractSpinBox* self)` | 取千分位开关 | `isGroupSeparatorShown()` qabstractspinbox.h:81 | 值语义 |
| `void XAbstractSpinBox_interpretText(XAbstractSpinBox* self)` | 解析/修正文本并刷新显示后发信号 | `interpretText()` qabstractspinbox.h:85 | 值语义 |
| `void XAbstractSpinBox_stepUp(XAbstractSpinBox* self)` | 上步进一次 | `stepUp()` qabstractspinbox.h:102 | 值语义 |
| `void XAbstractSpinBox_stepDown(XAbstractSpinBox* self)` | 下步进一次 | `stepDown()` qabstractspinbox.h:103 | 值语义 |
| `void XAbstractSpinBox_selectAll(XAbstractSpinBox* self)` | 全选编辑框文本 | `selectAll()` qabstractspinbox.h:104 | 值语义 |
| `void XAbstractSpinBox_stepBy_base(XAbstractSpinBox* self, int steps)` | 步进虚分派入口（基类空操作） | `stepBy()` qabstractspinbox.h:93 | 值语义 |
| `XValidatorState XAbstractSpinBox_validate_base(XAbstractSpinBox* self, const char* input, int* pos)` | 校验虚分派入口（基类恒 Acceptable） | `validate()` qabstractspinbox.h:90 | input 借用；pos 输入输出 |
| `void XAbstractSpinBox_fixup_base(XAbstractSpinBox* self, char* input, size_t capacity)` | 修正虚分派入口（基类空操作） | `fixup()` qabstractspinbox.h:91 | input 就地修改，调用方缓冲 |
| `void XAbstractSpinBox_clear_base(XAbstractSpinBox* self)` | 清空虚分派入口（清编辑框+置 cleared） | `clear()` qabstractspinbox.h:105 | 值语义 |
| `int XAbstractSpinBox_stepEnabled_base(XAbstractSpinBox* self)` | 步进使能虚分派入口（基类 StepNone） | `stepEnabled()` qabstractspinbox.h:132 | 值语义 |
| `void XAbstractSpinBox_interpret_base(XAbstractSpinBox* self)` | 内部提交钩子分派（基类空操作） | X 扩展（对齐私有 interpret） | 值语义 |
| `void XAbstractSpinBox_updateEdit_base(XAbstractSpinBox* self)` | 显示刷新钩子分派（基类仅 update） | X 扩展（对齐私有 updateEdit） | 值语义 |
| `void* XAbstractSpinBox_editingFinished_signal(XAbstractSpinBox* self)` | 发 editingFinished（由内嵌编辑框转发） | `editingFinished()` qabstractspinbox.h:134 | 不透明 id |

- **所有权与命名契约**：`lineEdit`/`text`/`specialValueText` 均返回借用（h:185-202），`setLineEdit` 为引用转移（h:187「旧对象释放…lineEdit 为 NULL 时内部新建」）；`m_specialValueText` 对象拥有（h:117）。本类无 `_const`/`_utf8`/`_2` 族；三态 set* 无。
- **可覆写点**：虚表槽 `StepBy`、`Validate`、`Fixup`、`Clear`、`StepEnabled`、`Interpret`、`UpdateEdit`（h:87-95，排在 `XCLASS_VTABLE_GET_SIZE(XWidget)` 之后）。默认行为：stepBy 空操作（`XAbstractSpinBox.c:211-216`）、validate 恒 `XValidatorState_Acceptable`（c:218-227）、fixup 空操作（c:229-236）、clear 清编辑框并置 `m_cleared`（c:238-245）、stepEnabled 恒 `StepNone`（c:255-259，注释引 qabstractspinbox.cpp:537-552 基类实测恒 0）、interpret 空操作（c:261-265）、updateEdit 仅 `XWidget_update`（c:267-271）。分派入口 `stepBy_base`（h:256/c:894）、`validate_base`（h:265/c:903）、`fixup_base`（h:276）、`clear_base`（h:285）、`stepEnabled_base`（h:293）、`interpret_base`（h:302）、`updateEdit_base`（h:310）。
- **注意事项与坑点**：
  1. **注释乱码**：h:134「XAbstractSpin盒init」、h:141「XAbstractSpin盒createex」——「Box」被替换为「盒」，且夹在中文里。
  2. **缺默认创建宏**：其余 8 个同类都以 `#define X*_create(parent, flags)` 提供默认内存类型创建，本头只有 `XAbstractSpinBox_create_ex`（h:147），调用方须自备 `XCLASS_DEFAULT_MEMORY_TYPE`。
  3. **内部钩子实际是公开槽**：h:16-17 称 `Interpret`/`UpdateEdit` 为「内部提交/显示刷新钩子」，但两者都在公共头声明（h:302、h:310）并在 `class_init` 注册进虚表——是可被外部调用的公开虚槽。
  4. **入参错误与默认语义相反**：`validate_base` 在 self/虚表无效时返回 `XValidatorState_Invalid`（c:903-908），而 Validate 槽默认返回 `Acceptable`（c:218-227）；空指针调用方会得到「非法」而非「可接受」。
  5. **accelerated 只有字段**：h:225 写「字段级」，实现 `setAccelerated` 仅赋值（c:844-847）——长按连续步进未接。
  6. **派生类必须自己接 Interpret**：`XGui.md:427` 记 XDateTimeEdit 覆写 `EXAbstractSpinBox_Interpret` 做格式串分节解析；本类默认空操作（c:261-265），不覆写则键入不提交。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XAbstractSpinBoxTest.c` 不存在）。间接覆盖：`XSpinBoxTest.c`（101 处，入口 `XSpinBoxTest_runAll` :77，测的是派生 XSpinBox）；`xspinbox_geometry_probe.c`；demo 侧 `xgui_demo_apitest_input.c`（136）。

## XActionGroup

- **定位**：动作逻辑分组——成员增删、ExclusionPolicy 三态互斥、checkedAction、enabled/visible 转发到成员、triggered/hovered 转发。继承链 XObject（`m_base` 首成员，`XCLASS_DEFINE_EXTEND_END(XActionGroup, XObject)` h:64-65）。`Src/XGui/Widget/XActionGroup.h:298`。
- **生命周期**：`XActionGroup_init(self, parent)`（h:101）、`XActionGroup_create(parent)` 宏（h:109）、`_create_ex`（h:111）；`addAction_2/_3` 新建的动作以组为父、随组级联释放（h:130-152），`addAction` 加入的既有动作只借用（h:120）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XActionGroup_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XActionGroup_init(XActionGroup* self, XObject* parent)` | 就地初始化 | `QActionGroup(QObject*)` gui/kernel/qactiongroup.h:33 | 不分配 self |
| `XActionGroup* XActionGroup_create_ex(XMemoryType memory, XObject* parent)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XActionGroup_create(parent)`（宏，h:109-110） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `XAction* XActionGroup_addAction(XActionGroup* self, XAction* action)` | 加成员（重复忽略），连 triggered/hovered/destroyed 并同步组状态 | `addAction(QAction*)` qactiongroup.h:36 | 借用，不取得所有权 |
| `XAction* XActionGroup_addAction_2(XActionGroup* self, const char* utf8Text)` | 新建带文本动作并入组 | `addAction(const QString&)` qactiongroup.h:37 | 新建归组（父对象） |
| `XAction* XActionGroup_addAction_3(XActionGroup* self, const char* iconUtf8, const char* utf8Text)` | 新建带图标+文本动作并入组 | `addAction(const QIcon&, const QString&)` qactiongroup.h:38 | 新建归组 |
| `void XActionGroup_removeAction(XActionGroup* self, XAction* action)` | 断开转发桥并从组移除，不销毁动作 | `removeAction()` qactiongroup.h:39 | 动作借用 |
| `XVector* XActionGroup_actions(const XActionGroup* self)` | 按加入顺序返回成员 | `actions()` qactiongroup.h:40 | **新建 XVector，调用方 `XClassDelete`**（h:164-171） |
| `XAction* XActionGroup_checkedAction(const XActionGroup* self)` | 取当前选中动作 | `checkedAction()` qactiongroup.h:41 | 借用 |
| `void XActionGroup_setCheckedAction(XActionGroup* self, XAction* action)` | 设选中并互斥、发 triggered | **X 扩展**（Qt 6.8.3 无此 API，h:184-186） | 借用 |
| `bool XActionGroup_isExclusive(const XActionGroup* self)` | 策略非 None 即互斥 | `isExclusive()` qactiongroup.h:43 | 值语义 |
| `void XActionGroup_setExclusive(XActionGroup* self, bool exclusive)` | 便捷映射 true→Exclusive / false→None | `setExclusive()` qactiongroup.h:53 | 值语义 |
| `XActionGroupExclusionPolicy XActionGroup_exclusionPolicy(const XActionGroup* self)` | 取互斥策略 | `exclusionPolicy()` qactiongroup.h:46 | 值语义 |
| `void XActionGroup_setExclusionPolicy(XActionGroup* self, XActionGroupExclusionPolicy policy)` | 设互斥策略 | `setExclusionPolicy()` qactiongroup.h:54 | 值语义 |
| `bool XActionGroup_isEnabled(const XActionGroup* self)` | 取分组启用态（默认 true） | `isEnabled()` qactiongroup.h:44 | 值语义 |
| `void XActionGroup_setEnabled(XActionGroup* self, bool enabled)` | 设启用态并转发全部成员 | `setEnabled()` qactiongroup.h:50 | 值语义 |
| `bool XActionGroup_isVisible(const XActionGroup* self)` | 取分组可见态（默认 true） | `isVisible()` qactiongroup.h:45 | 值语义 |
| `void XActionGroup_setVisible(XActionGroup* self, bool visible)` | 设可见态并转发全部成员 | `setVisible()` qactiongroup.h:52 | 值语义 |
| `void* XActionGroup_triggered_signal(XActionGroup* self, XAction* action)` | 发 triggered(action) | `triggered(QAction*)` qactiongroup.h:57 | 不透明 id |
| `void* XActionGroup_hovered_signal(XActionGroup* self, XAction* action)` | 发 hovered(action) | `hovered(QAction*)` qactiongroup.h:58 | 不透明 id |

- **所有权与命名契约**：`addAction` 借用（h:120「仅借用不取得所有权」）；`addAction_2/_3` 新建且父对象为组（引用转移给组）；`actions()` 返回**新建**容器（h:164-171，唯一须释放项）；`checkedAction`/`setCheckedAction` 借用。`addAction_2/_3` 是按 Qt 重载顺序编号的 `_2`/`_3`，不是 `_utf8` 后缀（但第二/三参确是 UTF-8 文本，与 `api-surface.md:488` 记的 `_2(const char*)` 命名问题同族）。
- **可覆写点**：无新增虚表槽——`XCLASS_DEFINE_EXTEND_END(XActionGroup, XObject)`（h:64-65）直挂 XObject，无 `_base` 入口；行为扩展只能靠成员动作侧（XAction 虚表）。
- **注意事项与坑点**：
  1. **两种互斥策略行为相同**：h:29-32 note 明写「Exclusive 与 ExclusiveOptional 的差别（"恰好一个"与"至多一个"）体现在对已选中动作的反选守卫；本库 XAction_setChecked 不感知所属组（Qt 的守卫位于 QAction::activate），故两策略在组内互斥联动上行为一致」。
  2. **setCheckedAction 是 X 扩展且改写了信号语义**：h:184-189 自注 Qt 6.8.3 无此公共 API（`gui/kernel/qactiongroup.h:25-58` 核对无 `setCheckedAction`），并会「发射 triggered(action)」——Qt 的 triggered 只由动作激活发出，直接设选中不发。
  3. **直接调用信号函数不发信号**：`XActionGroup_triggered_signal`/`_hovered_signal` 只返回信号 id（`XActionGroup.c:549-560`），真发射走内部 `xactiongroup_emitAction`（c:178、192、463）。
  4. **组状态只在「关」时下发**：`addAction` 尾部注释「组状态为关才下发，避免覆盖动作自身的显式开启」（对标 qactiongroup.cpp:135-137）；而 `setEnabled`/`setVisible` 会无条件转发全部成员（h:243-269）。
  5. **Qt 头位置**：`widgets/widgets/qactiongroup.h` 在 6.8.3 只是 7 行 include 转发；实体在 `gui/kernel/qactiongroup.h`。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XActionGroupTest.c` 不存在；`rg -c XActionGroup Test/XGuiTest/` 0 命中）。间接覆盖：demo 侧 `xgui_demo_apitest_menus.c`（35）、`xgui_demo_apitest_buttons.c`（28）。

## XButtonGroup

- **定位**：按钮逻辑分组——按加入顺序管理成员与 id、exclusive 互斥、checkedButton/checkedId、四组按钮维度与 id 维度信号；不负责成员按钮销毁。继承链 XObject（`m_base` 首成员，`XCLASS_DEFINE_EXTEND_END(XButtonGroup, XObject)` h:39-40）。`Src/XGui/Widget/XButtonGroup.h:155`。
- **生命周期**：`XButtonGroup_init(self, parent)`（h:59）、`XButtonGroup_create(parent)` 宏（h:60）、`_create_ex`（h:61）；成员按钮一律借用（h:48），组不销毁按钮（h:14-15）；组析构时桥接置 `m_groupDead` 只断连（`XButtonGroup.c:280-300`）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XButtonGroup_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XButtonGroup_init(XButtonGroup* self, XObject* parent)` | 就地初始化 | `QButtonGroup(QObject*)` qbuttongroup.h:24 | 不分配 self |
| `XButtonGroup* XButtonGroup_create_ex(XMemoryType memory, XObject* parent)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XButtonGroup_create(parent)`（宏，h:60） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `bool XButtonGroup_exclusive(const XButtonGroup* self)` | 取互斥标志（默认 true） | `exclusive()` qbuttongroup.h:28 | 值语义 |
| `void XButtonGroup_setExclusive(XButtonGroup* self, bool exclusive)` | 设互斥标志 | `setExclusive()` qbuttongroup.h:27 | 值语义 |
| `bool XButtonGroup_isExclusive(const XButtonGroup* self)` | 取互斥模式（`exclusive` 的同义查询） | **X 扩展**（Qt QButtonGroup 无 `isExclusive`） | 值语义 |
| `void XButtonGroup_addButton(XButtonGroup* self, XAbstractButton* button, int id)` | 加成员；id<0 自动分配并从 -2 递减 | `addButton(QAbstractButton*, int=-1)` qbuttongroup.h:30 | 借用，不取得所有权 |
| `void XButtonGroup_removeButton(XButtonGroup* self, XAbstractButton* button)` | 移除成员并清按钮的组指针 | `removeButton()` qbuttongroup.h:31 | 借用 |
| `const XVector* XButtonGroup_buttons(const XButtonGroup* self)` | 按加入顺序返回成员数组 | `buttons()` qbuttongroup.h:33 | 借用容器，禁释放（h:88-90） |
| `XAbstractButton* XButtonGroup_button(const XButtonGroup* self, int id)` | 按 id 反查成员；未找到 NULL | `button(int)` qbuttongroup.h:38 | 借用 |
| `XAbstractButton* XButtonGroup_checkedButton(const XButtonGroup* self)` | 取当前选中按钮 | `checkedButton()` qbuttongroup.h:35 | 借用 |
| `int XButtonGroup_checkedId(const XButtonGroup* self)` | 取选中按钮 id；无选中 -1 | `checkedId()` qbuttongroup.h:41 | 值语义 |
| `void XButtonGroup_setId(XButtonGroup* self, XAbstractButton* button, int id)` | 改成员 id；非成员忽略 | `setId()` qbuttongroup.h:39 | 借用 |
| `int XButtonGroup_id(const XButtonGroup* self, XAbstractButton* button)` | 查成员 id；非成员 -1 | `id()` qbuttongroup.h:40 | 值语义 |
| `void* XButtonGroup_buttonClicked_signal(XButtonGroup* self, XAbstractButton* button)` | 发 buttonClicked(button) | `buttonClicked(QAbstractButton*)` qbuttongroup.h:44 | 不透明 id |
| `void* XButtonGroup_buttonPressed_signal(XButtonGroup* self, XAbstractButton* button)` | 发 buttonPressed | `buttonPressed()` qbuttongroup.h:45 | 不透明 id |
| `void* XButtonGroup_buttonReleased_signal(XButtonGroup* self, XAbstractButton* button)` | 发 buttonReleased | `buttonReleased()` qbuttongroup.h:46 | 不透明 id |
| `void* XButtonGroup_buttonToggled_signal(XButtonGroup* self, XAbstractButton* button, bool checked)` | 发 buttonToggled(button,checked) | `buttonToggled()` qbuttongroup.h:47 | 不透明 id |
| `void* XButtonGroup_idClicked_signal(XButtonGroup* self, int id)` | 发 idClicked(id) | `idClicked(int)` qbuttongroup.h:48 | 不透明 id |
| `void* XButtonGroup_idPressed_signal(XButtonGroup* self, int id)` | 发 idPressed(id) | `idPressed(int)` qbuttongroup.h:49 | 不透明 id |
| `void* XButtonGroup_idReleased_signal(XButtonGroup* self, int id)` | 发 idReleased(id) | `idReleased(int)` qbuttongroup.h:50 | 不透明 id |
| `void* XButtonGroup_idToggled_signal(XButtonGroup* self, int id, bool checked)` | 发 idToggled(id,checked) | `idToggled(int,bool)` qbuttongroup.h:51 | 不透明 id |

- **所有权与命名契约**：成员按钮与 `m_buttons` 数组均借用（h:48-50）；`buttons()` 返回内部 `const XVector*` 借用（h:88-90）。本类无 `_const`/`_utf8`/`_2` 族；`exclusive`/`isExclusive` 是同一标志的双查询名（见坑点 1）。
- **可覆写点**：无新增虚表槽——`XCLASS_DEFINE_EXTEND_END(XButtonGroup, XObject)`（h:39-40），无 `_base` 入口；互斥联动靠基类 `XAbstractButton_setChecked`/`nextCheckState` 读回写的 `m_group` 位（`XAbstractButton.c:625-627`）。
- **注意事项与坑点**：
  1. **头注释与 Qt 面不符**：h:75 写「查询互斥模式（对标 isExclusive）」，但 Qt `QButtonGroup` 只有 `exclusive()`/`setExclusive()`（qbuttongroup.h:22-28），`isExclusive` 是 `QActionGroup` 的 API（qactiongroup.h:43）——本函数是 X 侧别名。
  2. **直接调用信号函数不发信号**：8 个 `XButtonGroup_*_signal` 全部只返回 id（`XButtonGroup.c:587-647`，函数体内 `XObject_emitSignal` 命中 0），真发射经内部 `xbgroup_emitButton`/`xbgroup_emitId`（c:231-233）。
  3. **addButton 会回写按钮的组位**：`c:498-501` `button->m_group = self;`（注释「使 XAbstractButton_group() 生效，并供基类在 setChecked/nextCheckState 里识别 exclusive 组语义」）——与 `XAbstractButton.h:411-413` 的「XButtonGroup 目前未回写」相反。
  4. **自动 id 从 -2 起递减**：`c:482-487` 按负序分配，测试断言首个 -2、次个 -3（`XButtonGroupTest.c:61-64`）。
  5. **Qt 无 `XButtonGroup` 的 `isExclusive` 与「组状态转发」概念**；本类只做互斥与信号转发，不处理按钮的 enabled/visible（对比 XActionGroup 会转发）。
- **测试覆盖**：**存在** `Test/XGuiTest/XButtonGroupTest.c`（141 行），入口 `bool XButtonGroupTest_runAll(void)`（c:46，声明 `XButtonGroupTest.h:7`），**21 处 `bg_expect(` 断言调用点**（`grep -c '^ *bg_expect(' Test/XGuiTest/XButtonGroupTest.c` → 21；另有 1 处函数定义行不计）。覆盖创建默认值、addButton 自动 id、checkedButton/checkedId 互斥、setId、buttonClicked/idClicked/buttonToggled 转发、removeButton 后 checkedId 复位。

## XCalendarWidget

- **定位**：日历控件——选中日期、显示年月、日期范围钳位、首日/表头格式、导航栏与 42 格（7×6）网格绘制与点击选择，年份就地编辑。继承链 XObject ← XWidget（`m_base` 首成员，`XCLASS_DEFINE_EXTEND_END(XCalendarWidget, XWidget)` h:56-57）。`Src/XGui/Widget/XCalendarWidget.h:315`。
- **生命周期**：`XCalendarWidget_init`（h:90）初始化选中=今日、显示年月=今日、firstDayOfWeek=1、网格不可见、导航栏可见、selectionMode=SingleSelection、horizontalHeaderFormat=ShortDayNames、dateEditAcceptDelay=1500（`XCalendarWidget.c` init 段）；`XCalendarWidget_create(parent, flags)` 宏（h:92）/`_create_ex`（h:93）；年份编辑器懒建、`deinit` 释放（`VXCalendarWidget_deinit`）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XCalendarWidget_class_init(void)` | 初始化类虚表 | — | 静态借用 |
| `void XCalendarWidget_init(XCalendarWidget* self, XWidget* parent, XWidgetFlags flags)` | 就地初始化 | `QCalendarWidget(QWidget*)` qcalendarwidget.h:58 | 不分配 self |
| `XCalendarWidget* XCalendarWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆创建 | X 扩展 | 调用方拥有 |
| `XCalendarWidget_create(parent, flags)`（宏，h:92） | 默认内存类型创建 | X 扩展 | 调用方拥有 |
| `XDate XCalendarWidget_selectedDate(const XCalendarWidget* self)` | 取选中日期 | `selectedDate()` qcalendarwidget.h:64 | 值语义 |
| `void XCalendarWidget_setSelectedDate(XCalendarWidget* self, const XDate* date)` | 设选中日期（同值早退），并同步显示页 | `setSelectedDate()` qcalendarwidget.h:123 | 入参借用 |
| `int XCalendarWidget_yearShown(const XCalendarWidget* self)` | 取显示年 | `yearShown()` qcalendarwidget.h:66 | 值语义 |
| `int XCalendarWidget_monthShown(const XCalendarWidget* self)` | 取显示月 | `monthShown()` qcalendarwidget.h:67 | 值语义 |
| `void XCalendarWidget_setCurrentPage(XCalendarWidget* self, int year, int month)` | 设显示年月；month 钳到 1..12，同页早退 | `setCurrentPage()` qcalendarwidget.h:125 | 值语义 |
| `void XCalendarWidget_endYearEdit(XCalendarWidget* self)` | 终结年份就地编辑会话（不提交、幂等） | X 扩展（对齐 yearEdit 的 Escape 语义） | 值语义 |
| `XDate XCalendarWidget_minimumDate(const XCalendarWidget* self)` | 取最小日期 | `minimumDate()` qcalendarwidget.h:69 | 值语义 |
| `void XCalendarWidget_setMinimumDate(XCalendarWidget* self, const XDate* date)` | 设最小日期 | `setMinimumDate()` qcalendarwidget.h:70 | 入参借用 |
| `void XCalendarWidget_clearMinimumDate(XCalendarWidget* self)` | 清最小日期限制 | `clearMinimumDate()` qcalendarwidget.h:71 | 值语义 |
| `XDate XCalendarWidget_maximumDate(const XCalendarWidget* self)` | 取最大日期 | `maximumDate()` qcalendarwidget.h:73 | 值语义 |
| `void XCalendarWidget_setMaximumDate(XCalendarWidget* self, const XDate* date)` | 设最大日期 | `setMaximumDate()` qcalendarwidget.h:74 | 入参借用 |
| `void XCalendarWidget_clearMaximumDate(XCalendarWidget* self)` | 清最大日期限制 | `clearMaximumDate()` qcalendarwidget.h:75 | 值语义 |
| `int XCalendarWidget_firstDayOfWeek(const XCalendarWidget* self)` | 取每周首日 | `firstDayOfWeek()` qcalendarwidget.h:77 | 值语义 |
| `void XCalendarWidget_setFirstDayOfWeek(XCalendarWidget* self, int dayOfWeek)` | 设每周首日（1=Mon..7=Sun） | `setFirstDayOfWeek()` qcalendarwidget.h:78 | 值语义 |
| `bool XCalendarWidget_isGridVisible(const XCalendarWidget* self)` | 取网格线开关（默认 false） | `isGridVisible()` qcalendarwidget.h:81 | 值语义 |
| `void XCalendarWidget_setGridVisible(XCalendarWidget* self, bool visible)` | 设网格线 | `setGridVisible()` qcalendarwidget.h:126 | 值语义 |
| `bool XCalendarWidget_isNavigationBarVisible(const XCalendarWidget* self)` | 取导航栏开关（默认 true） | `isNavigationBarVisible()` qcalendarwidget.h:80 | 值语义 |
| `void XCalendarWidget_setNavigationBarVisible(XCalendarWidget* self, bool visible)` | 设导航栏 | `setNavigationBarVisible()` qcalendarwidget.h:127 | 值语义 |
| `void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable)` | 设日期编辑框开关（仅存储） | `setDateEditEnabled()` qcalendarwidget.h:106 | 值语义 |
| `bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self)` | 取日期编辑框开关 | `isDateEditEnabled()` qcalendarwidget.h:105 | 值语义 |
| `void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int fmt)` | 设表头文本格式位（仅存储） | `setHeaderTextFormat(const QTextCharFormat&)` qcalendarwidget.h:96 | 值语义 |
| `void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int fmt)` | 设周几文本格式位（仅存储） | `setWeekdayTextFormat(Qt::DayOfWeek, const QTextCharFormat&)` qcalendarwidget.h:99 | 值语义 |
| `void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self, int format)` | 设垂直表头格式（仅存储+重绘） | `setVerticalHeaderFormat()` qcalendarwidget.h:93 | 值语义 |
| `int XCalendarWidget_verticalHeaderFormat(const XCalendarWidget* self)` | 取垂直表头格式 | `verticalHeaderFormat()` qcalendarwidget.h:92 | 值语义 |
| `int XCalendarWidget_headerTextFormat(const XCalendarWidget* self)` | 取表头文本格式位 | `headerTextFormat()` qcalendarwidget.h:95 | 值语义 |
| `int XCalendarWidget_weekdayTextFormat(const XCalendarWidget* self)` | 取周几文本格式位 | `weekdayTextFormat()` qcalendarwidget.h:98 | 值语义 |
| `void XCalendarWidget_setHorizontalHeaderFormat(XCalendarWidget* self, int format)` | 设水平表头格式（0 不显示/1 单字/2 短名/3 长名） | `setHorizontalHeaderFormat()` qcalendarwidget.h:90 | 值语义 |
| `int XCalendarWidget_horizontalHeaderFormat(const XCalendarWidget* self)` | 取水平表头格式（默认 2） | `horizontalHeaderFormat()` qcalendarwidget.h:89 | 值语义 |
| `void XCalendarWidget_setDateEditAcceptDelay(XCalendarWidget* self, int delay)` | 设日期编辑确认延迟（仅存储） | `setDateEditAcceptDelay()` qcalendarwidget.h:109 | 值语义 |
| `int XCalendarWidget_dateEditAcceptDelay(const XCalendarWidget* self)` | 取确认延迟（默认 1500） | `dateEditAcceptDelay()` qcalendarwidget.h:108 | 值语义 |
| `void XCalendarWidget_setDateRange(XCalendarWidget* self, const XDate* min, const XDate* max)` | 同时设上下界；NULL 忽略 | `setDateRange()` qcalendarwidget.h:124 | 入参借用 |
| `void XCalendarWidget_showNextMonth(XCalendarWidget* self)` | 显示下月（不改选中） | `showNextMonth()` qcalendarwidget.h:128 | 值语义 |
| `void XCalendarWidget_showPreviousMonth(XCalendarWidget* self)` | 显示上月（不改选中） | `showPreviousMonth()` qcalendarwidget.h:129 | 值语义 |
| `void XCalendarWidget_showNextYear(XCalendarWidget* self)` | 显示下年（不改选中） | `showNextYear()` qcalendarwidget.h:130 | 值语义 |
| `void XCalendarWidget_showPreviousYear(XCalendarWidget* self)` | 显示上年（不改选中） | `showPreviousYear()` qcalendarwidget.h:131 | 值语义 |
| `void XCalendarWidget_showSelectedDate(XCalendarWidget* self)` | 显示选中日期所在月 | `showSelectedDate()` qcalendarwidget.h:132 | 值语义 |
| `void XCalendarWidget_showToday(XCalendarWidget* self)` | 显示今日所在月（不改选中） | `showToday()` qcalendarwidget.h:133 | 值语义 |
| `int XCalendarWidget_selectionMode(const XCalendarWidget* self)` | 取选择模式 | `selectionMode()` qcalendarwidget.h:86 | 值语义 |
| `void XCalendarWidget_setSelectionMode(XCalendarWidget* self, int mode)` | 设选择模式（NoSelection/SingleSelection） | `setSelectionMode()` qcalendarwidget.h:87 | 值语义 |
| `void* XCalendarWidget_clicked_signal(XCalendarWidget* self, const XDate* date)` | 发 clicked(date) | `clicked(QDate)` qcalendarwidget.h:137 | 不透明 id |
| `void* XCalendarWidget_activated_signal(XCalendarWidget* self, const XDate* date)` | 发 activated(date) | `activated(QDate)` qcalendarwidget.h:138 | 不透明 id |
| `void* XCalendarWidget_selectionChanged_signal(XCalendarWidget* self)` | 发 selectionChanged | `selectionChanged()` qcalendarwidget.h:136 | 不透明 id |
| `void* XCalendarWidget_currentPageChanged_signal(XCalendarWidget* self, int year, int month)` | 发 currentPageChanged(year,month) | `currentPageChanged(int,int)` qcalendarwidget.h:139 | 不透明 id |

- **所有权与命名契约**：`XDate` 全值语义；`setSelectedDate`/`setMinimumDate`/`setMaximumDate`/`setDateRange` 的 `const XDate*` 为借用；`m_yearEdit` 归本控件（h:65-68，deinit 释放）。本类无 `_const`/`_utf8`/`_2` 族；三态 set* 无。
- **可覆写点**：无新增虚表槽——`XCLASS_DEFINE_EXTEND_END(XCalendarWidget, XWidget)`（h:56-57）直挂 XWidget，无 `_base` 入口；交互行为在自绘的鼠标/键盘处理里（同文件 static 函数）。
- **注意事项与坑点**：
  1. **垂直表头格式码与 Qt 枚举顺序相反**：本头 h:199 与 h:72 都写「0=ISO 周数，1=无」，而 Qt `VerticalHeaderFormat` 为 `NoVerticalHeader=0, ISOWeekNumbers=1`（qcalendarwidget.h:46-49）；本项目对该维只有 `int` 字段、没有枚举类型（h:72），无法靠编译期值对齐。
  2. **垂直表头只存不渲染**：`XCalendarWidget_setVerticalHeaderFormat` 仅赋值 + `XWidget_update`（`XCalendarWidget.c:1291-1299`），绘制路径不读该字段（`rg -n "ISOWeekNumbers|ISO 周" Src/XGui/Widget/XCalendarWidget.c` 无命中）。
  3. **悬空引用**：h:118 的 `@details` 说「终结 beginYearEdit 开启的会话」，但 `beginYearEdit` 是文件内 static（`XCalendarWidget.c:571`），公共头无声明；对外只有 `endYearEdit`（h:128）。
  4. **直接调用信号函数不发信号**：4 个 `_signal` 包装只返回 id（`XCalendarWidget.c:1220-1241`）；真发射经内部 `xcal_emitDate`（c:828、c:903）与 `xcal_emitPageChanged`/`xcal_emitSelectionChanged`（c:196-218）。
  5. **多处仅存储未接行为**：`m_dateEditEnabled`（h:83 注「存储」）、`m_dateEditAcceptDelay`（h:76 注「仅存储」）、`m_headerTextFormat`/`m_weekdayTextFormat`（h:73-74 注「预留」）。
  6. **纯公历边界**：`XGui.md:1596` 记「纯公历（QCalendar 备选历法不做）」——Qt 的 QCalendar 备选历法在本库无承载。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XCalendarWidgetTest.c` 不存在；`rg -c XCalendarWidget Test/XGuiTest/` 0 命中）。间接覆盖：demo 侧 `xgui_demo_apitest_dialogs.c`（17）、`xgui_demo_apitest_input.c`（7）、`xgui_window_demo.c`（5）。
# 控件文档片段 · 批次 02

本批覆盖 `Src/XGui/Widget/` 下 9 个控件类。所有签名逐字抄自头文件（行号可核对）；实现行号取自同目录 `.c`。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/`。**目录实况与任务书假设不同**（已 `find` 核实）：`qcolordialog.h`、`qdialog.h` 在 `dialogs/`，`qcompleter.h` 在 `util/`，`qdateedit.h` 不存在（`QDateEdit` 定义在 `widgets/qdatetimeedit.h:194-205`）——下文按真实路径标注。

---

## XCheckBox

**定位**：三态复选框（indicator 方块 + 勾/横线 + 文本/图标）。继承链 `XObject ← XWidget ← XAbstractButton ← XCheckBox`。头文件 `Src/XGui/Widget/XCheckBox.h`（273 行）。开关 `XCHECKBOX_ON`，另需 `XWIDGET_ON && XABSTRACTBUTTON_ON`（XCheckBox.h:44）。

**生命周期**：`XCheckBox_init`（XCheckBox.h:101）先是 `XAbstractButton_init` 再挂虚表，随后置 checkable=true、tristate/noChange/blockRefresh=false、publishedState=Unchecked（XCheckBox.c:585-598）；栈对象与 `XClassDeinit` 成对、堆对象经 `_create/_create_ex` 后 `XClassDelete`（XClass.h:226-231），类不重载 deinit（无自有资源，XCheckBox.c:21-22）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XCheckBox_class_init(void);` | 建表并登记为 XAbstractButton 派生类（自动互斥依赖） | X 扩展（Qt 元对象） | 共享表，禁释放 |
| `void XCheckBox_init(XCheckBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 checkable=true、三态关） | `QCheckBox(QWidget*)` qcheckbox.h:25 | — |
| `#define XCheckBox_create(parent, flags) XCheckBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,(parent),(flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XCheckBox* XCheckBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `#define XCheckBox_text(self)` | 读按钮文本 | `QAbstractButton::text` qabstractbutton.h:45 | 借用，禁释放 |
| `#define XCheckBox_setText(self, text)` | 设文本（XString 主版本） | `setText` qabstractbutton.h:44 | XString 借用 |
| `#define XCheckBox_setText_2(self, utf8)` | 设文本（UTF-8 方便版） | `setText` qabstractbutton.h:44 | UTF-8 借用 |
| `#define XCheckBox_icon(self)` / `XCheckBox_setIcon(self, icon)` | 读写图标 | `icon`/`setIcon` qabstractbutton.h:48/47 | XIcon 值语义 |
| `#define XCheckBox_iconSize(self)` / `XCheckBox_setIconSize(self, size)` | 读写图标渲染尺寸 | `iconSize`/`setIconSize` qabstractbutton.h:50/82 | XSize 值 |
| `#define XCheckBox_isCheckable(self)` / `XCheckBox_setCheckable(self, checkable)` | 读写可选中位（本类默认 true） | `isCheckable`/`setCheckable` qabstractbutton.h:58/57 | — |
| `#define XCheckBox_isChecked(self)` / `XCheckBox_setChecked(self, checked)` | 读写选中真值（三态下 PartiallyChecked 时 isChecked 为 true） | `isChecked`/`setChecked` qabstractbutton.h:60/86 | — |
| `#define XCheckBox_toggle(self)` | 非虚切换：`setChecked(!checked)`，三态下只在 U/C 间跳变 | `toggle` qabstractbutton.h:85 | — |
| `#define XCheckBox_isDown(self)` / `XCheckBox_setDown(self, down)` | 按下面板态 | `isDown`/`setDown` qabstractbutton.h:63/62 | — |
| `#define XCheckBox_autoRepeat(self)` / `XCheckBox_setAutoRepeat(self, repeat)` | 自动重复开关 | `autoRepeat`/`setAutoRepeat` qabstractbutton.h:66/65 | — |
| `#define XCheckBox_autoRepeatDelay(self)` / `XCheckBox_setAutoRepeatDelay(self, delay)` | 自动重复起始延迟 | `autoRepeatDelay`/`setAutoRepeatDelay` qabstractbutton.h:69/68 | — |
| `#define XCheckBox_autoRepeatInterval(self)` / `XCheckBox_setAutoRepeatInterval(self, interval)` | 自动重复间隔 | `autoRepeatInterval`/`setAutoRepeatInterval` qabstractbutton.h:72/71 | — |
| `#define XCheckBox_autoExclusive(self)` / `XCheckBox_setAutoExclusive(self, exclusive)` | 自动互斥位 | `autoExclusive`/`setAutoExclusive` qabstractbutton.h:75/74 | — |
| `#define XCheckBox_click(self)` / `XCheckBox_animateClick(self)` | 程序化点击 / 动画点击 | `click`/`animateClick` qabstractbutton.h:84/83 | — |
| `#define XCheckBox_pressed_signal(self)` | pressed 信号标识 | `pressed` qabstractbutton.h:89 | 信号标识地址 |
| `#define XCheckBox_released_signal(self)` | released 信号标识 | `released` qabstractbutton.h:90 | 信号标识地址 |
| `#define XCheckBox_clicked_signal(self, checked)` | clicked(bool) 信号标识 | `clicked(bool)` qabstractbutton.h:91 | 信号标识地址 |
| `#define XCheckBox_toggled_signal(self, checked)` | toggled(bool) 信号标识 | `toggled(bool)` qabstractbutton.h:92 | 信号标识地址 |
| `bool XCheckBox_hitButton(const XCheckBox* self, const XPoint* pos);` | 经 `XAbstractButton_hitButton_base` 多态命中 | `QCheckBox::hitButton` qcheckbox.h:47 | — |
| `void XCheckBox_setTristate(XCheckBox* self, bool tristate);` | 开关三态（只翻标志，不动 noChange/checked） | `setTristate` qcheckbox.h:32 | — |
| `bool XCheckBox_isTristate(const XCheckBox* self);` | 查三态位 | `isTristate` qcheckbox.h:33 | — |
| `XCheckState XCheckBox_checkState(const XCheckBox* self);` | 派生计算：三态且 noChange→PartiallyChecked，否则按 m_checked | `checkState` qcheckbox.h:35 | 值 |
| `void XCheckBox_setCheckState(XCheckBox* self, XCheckState state);` | 写状态（PartiallyChecked 自动开 tristate）；去重后发 checkStateChanged | `setCheckState` qcheckbox.h:36 | — |
| `void* XCheckBox_checkStateChanged_signal(XCheckBox* self, XCheckState state);` | 状态变化信号（state 为 int 载荷）；self==NULL 只返回标识 | `checkStateChanged` qcheckbox.h:43 | 信号标识地址 |
| `XSize XCheckBox_sizeHint(const XCheckBox* self);` | 13×13 indicator + 6 间距 + 文本/图标宽（XCheckBox.c:116-156） | `sizeHint` qcheckbox.h:29 | 值 |
| `XSize XCheckBox_minimumSizeHint(const XCheckBox* self);` | 同 sizeHint | `minimumSizeHint` qcheckbox.h:30 | 值 |
| `void XCheckBox_drawContents(XCheckBox* self, XPainter* painter);` | 离屏绘制入口（背景/indicator/勾横线/图标/文本） | `paintEvent` qcheckbox.h:50（仅内容） | painter 借用 |

枚举：`XCheckState`（Unchecked=0/PartiallyChecked=1/Checked=2，数值对齐 Qt::CheckState，XCheckBox.h:61-66）。

**所有权与命名契约**：`_const` 借用族=查询取 `const XCheckBox*`；`_2`=UTF-8 设置侧（`XCheckBox_setText_2`）；三态 `set*`= `setTristate`（纯开关）与 `setCheckState`（三值写入，值域 0..2 未做越界校验，负值落入 `state != Unchecked` 即按 Checked 记账，XCheckBox.c:207-224）；其余遵循 XWidget/XAbstractButton 基类契约。

**可覆写点**：`XCLASS_DEFINE_BEGING(XCheckBox)`+`XCLASS_DEFINE_EXTEND_END(XCheckBox, XAbstractButton)`（XCheckBox.h:55-56），不新增槽。重载（XCheckBox.c:563-583）：`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`→按 QCommonStyle 数值重算；`EXAbstractButton_CheckStateSet`→清 noChange、按 publishedState 去重发信号（XCheckBox.c:419-437）；`EXAbstractButton_NextCheckState`→三态 (s+1)%3 循环，否则交基类反转（XCheckBox.c:439-452）；`EXAbstractButton_HitButton`→整控件矩形；`EXAbstractButton_ContentChanged`→刷新 sizeHint；`EXWidget_PaintEvent`；`EXClass_Copy/Move`→先 `XClass_Parent` 再拷三态字段。`_base` 入口：`XAbstractButton_checkStateSet_base`（XAbstractButton_Protected.h:102）、`_nextCheckState_base`（:121）、`_hitButton_base`（:144）、`_contentChanged_base`（:155）。

**注意事项与坑点**

1. **头/实现矛盾（hitButton 命中域）**：同一头文件里两处说法相反——`@details` 称"XCheckBox 重载为 indicator 矩形包含（无主题时按 indicator 矩形近似）"（XCheckBox.h:192-194），而 `@note` 称"hitButton 按整控件矩形命中（对标 Qt SE_CheckBoxClickRect = indicator ∪ 文本区）"（XCheckBox.h:23-25）。实现在 `VXCheckBox_hitButton` 用 `XWidget_rect` 全矩形包含（XCheckBox.c:455-469，:463-466 注释自陈"文本未占满控件时右侧空档一并命中"），但该文件文件头 XCheckBox.c:16 与 :25 又写"indicator 矩形命中/按 indicator 矩形"。行为口径以代码为准=整控件矩形，`@details` 与 `.c` 文件头三处文字均过时。
2. **三态的真值源是基类 m_checked**：PartiallyChecked 由 `m_tristate && m_noChange` 组合表达（XCheckBox.c:190-199），`setCheckState(PartiallyChecked)` 会把 checked 置 true（XCheckBox.c:216-217）。因此对三态复选框调 `XCheckBox_toggle` 得到的是 U↔C 跳变而非 U→P→C（XCheckBox.h:144-147；XAbstractButton.c:649-661 同款注记），(s+1)%3 循环只发生在点击路径的 `nextCheckState`（XCheckBox.c:439-452）。
3. **setTristate 不清 noChange**：关闭三态后 `checkState()` 立即按真值报 Checked（此前实现"清 noChange 落到 Unchecked"被判偏离 Qt）；实现注详见 XCheckBox.c:174-181。
4. **信号去重双路**：`setChecked` 早退路径会回调 `checkStateSet` 虚槽（XAbstractButton.c:616-618），三态字段同步与去重都靠它；`setCheckState` 期间用 `m_blockRefresh` 抑制该槽重复发信号（XCheckBox.c:213-224）。外部若在信号槽里再调 `setCheckState`，发布状态已在发射前写回 `m_publishedState`，不会自激。
5. **无快捷键/悬停/bevel**：`stateChanged(int)`（Qt 6.9 起废弃）不提供，只对齐 Qt 6.7+ 的 `checkStateChanged`；`m_mouseTracking` 悬停效果未实现（XCheckBox.h:23-27）。
6. **两行悬空 doc 注释**：XCheckBox.h:115-116 的"通过 XClass 虚表释放…/删除堆上的…"后面没有对应声明（`rg "XCheckBox_delete\|XCheckBox_deinit"` 零命中），实际删除走宏 `XClassDelete`（XClass.h:229-230）。

**测试覆盖**：无 `Test/XGuiTest/XCheckBoxTest.c`（`ls` 核实不存在）。间接覆盖：`rg -c "XCheckBox_" xgui_regression_test.c`=17 处（27307-27333 的三态/命中/切换断言段、30923 虚表登记断言、33650 起的实例化）；`Test/XGuiDemo/xgui_demo_apitest_buttons.c:797-818` 起（该文件 63 处 API 引用，含 checkStateChanged 信号连接）；`Test/XGuiDemo/*.c` 合计 103 处。

---

## XColorDialog

**定位**：颜色选择对话框（标准色网格 + RGB 分量 + 预览 + 确定/取消，另有静态便捷 `getColor`）。继承链 `XObject ← XWidget ← XDialog ← XColorDialog`。头文件 `Src/XGui/Widget/XColorDialog.h`（252 行）。开关 `XDIALOG_ON`（XColorDialog.h:40）——**注意并无 `XCOLORDIALOG_ON` 宏**（`rg "XCOLORDIALOG" Src/` 仅命中头文件保护宏本身）。

**生命周期**：`XColorDialog_init` 先给 flags 叠 `XWindowType_MSWindowsFixedSizeDialogHint`（固定尺寸声明，XColorDialog.c:160-168）再 `XDialog_init`，写 currentColor=selectedColor=initial、options=0；`init_default` 固定传 `XColor_White`（:178-181）；堆对象 `_create/_create_ex` 后 `XClassDelete`。真正有资源的是 `getColor` 路径自建的布局与子控件，经 `xcd_pendingRegister` + `XObject_deleteLater` 延迟回收（取消崩溃根修，XColorDialog.c:75-81、:877-878）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XColorDialog_class_init(void);` | 建表（继承 XDialog，另重载 ShowEvent/Deinit） | X 扩展 | 共享表，禁释放 |
| `void XColorDialog_init(XColorDialog* self, XColor initial, XWidget* parent, XWidgetFlags flags);` | 初始化（固定尺寸提示位 + 初色） | `QColorDialog(const QColor&, QWidget*)` qcolordialog.h:37 | — |
| `void XColorDialog_init_default(XColorDialog* self, XWidget* parent);` | 以白色初始化 | `QColorDialog(QWidget*)` qcolordialog.h:36 | — |
| `#define XColorDialog_create(parent, flags) …XColor_White…` | 默认内存+白色建堆对象 | 同 init_default | 调用方拥有，须 `XClassDelete` |
| `XColorDialog* XColorDialog_create_ex(XMemoryType memory, XColor initial, XWidget* parent, XWidgetFlags flags);` | 指定内存+初色建堆对象 | X 扩展 | 调用方拥有 |
| `void XColorDialog_setCurrentColor(XColorDialog* self, XColor color);` | 设当前色；实变才发 currentColorChanged（XColorDialog.c:196-204） | `setCurrentColor` qcolordialog.h:40 | XColor 值 |
| `XColor XColorDialog_currentColor(const XColorDialog* self);` | 读当前色；NULL 返回无效色 | `currentColor` qcolordialog.h:41 | 值 |
| `XColor XColorDialog_selectedColor(const XColorDialog* self);` | 读最近接受色；NULL 返回无效色 | `selectedColor` qcolordialog.h:43 | 值 |
| `void XColorDialog_setOption(XColorDialog* self, XColorDialogOption option, bool on);` | 置/清单个选项位 | `setOption` qcolordialog.h:45 | — |
| `bool XColorDialog_testOption(const XColorDialog* self, XColorDialogOption option);` | 测选项位 | `testOption` qcolordialog.h:46 | — |
| `void XColorDialog_setOptions(XColorDialog* self, XColorDialogOptions options);` | 整体设选项位集 | `setOptions` qcolordialog.h:47 | — |
| `XColorDialogOptions XColorDialog_options(const XColorDialog* self);` | 读选项位集 | `options` qcolordialog.h:48 | 值 |
| `XColor XColorDialog_getColor(XColor initial, XWidget* parent, const XString* title, XColorDialogOptions options);` | 模态弹窗取色；无 GUI 环境返回 initial | `getColor` qcolordialog.h:55-58 | title 借用 |
| `XColor XColorDialog_getColor_2(XColor initial, XWidget* parent, const char* title, XColorDialogOptions options);` | 同上，title 走 UTF-8 | `getColor` qcolordialog.h:55-58 | title 借用 |
| `int XColorDialog_customCount(const XColorDialog* self);` | 已设自定义槽计数（本库口径） | `customCount` qcolordialog.h:60（Qt 恒 16） | — |
| `void XColorDialog_setCustomColor(XColorDialog* self, int index, XColor color);` | 写自定义色（index∈[0,16)） | `setCustomColor` qcolordialog.h:62 | 进程级共享表 |
| `XColor XColorDialog_customColor(const XColorDialog* self, int index);` | 读自定义色；越界返黑 | `customColor` qcolordialog.h:61 | 值 |
| `void XColorDialog_setStandardColor(XColorDialog* self, int index, XColor color);` | 写标准色（index∈[0,48)） | `setStandardColor` qcolordialog.h:64 | 进程级共享表 |
| `XColor XColorDialog_standardColor(const XColorDialog* self, int index);` | 读标准色；越界返黑 | `standardColor` qcolordialog.h:63 | 值 |
| `void XColorDialog_open(XColorDialog* self);` | 非模态显示（实现仅 `XWidget_show`） | `QDialog::open` qdialog.h:52（`using QDialog::open` qcolordialog.h:50） | — |
| `void XColorDialog_done(XColorDialog* self, int result);` | 收口：Accepted→selectedColor=currentColor 并发 colorSelected；否则 selectedColor 复位无效色；再断开 open_2 连接 | `done` qcolordialog.h:72（实现 qcolordialog.cpp:2303-2316） | — |
| `void XColorDialog_open_2(XColorDialog* self, XObject* receiver, XSlotFunc1 member);` | 窗口模态打开并连 colorSelected 到接收槽 | `open(QObject*,const char*)` qcolordialog.h:51（qcolordialog.cpp:2179-2186） | receiver 借用 |
| `void* XColorDialog_currentColorChanged_signal(XColorDialog* self, XColor color);` | 当前色变化信号 | `currentColorChanged` qcolordialog.h:67 | 信号标识地址 |
| `void* XColorDialog_colorSelected_signal(XColorDialog* self, XColor color);` | 选色信号 | `colorSelected` qcolordialog.h:68 | 信号标识地址 |

枚举 `XColorDialogOption`（XColorDialog.h:43-49，与 qcolordialog.h:26-31 逐值一致）：
- `XColorDialog_ShowAlphaChannel` = 0x00000001
- `XColorDialog_NoButtons` = 0x00000002
- `XColorDialog_DontUseNativeDialog` = 0x00000004
- `XColorDialog_NoEyeDropperButton` = 0x00000008

另 `typedef uint32_t XColorDialogOptions`（XColorDialog.h:50）。

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_2`=UTF-8 标题重载（`getColor_2`）与"带 receiver 的打开"（`open_2`，按数字后缀而非 UTF-8，见 XColorDialog.h:218-220 注记）；三态 `set*`：`setOption` 是 (option,on) 位对，`setOptions` 整体覆盖，两者均非三态取值；自定义/标准色表为**文件级静态存储**（XColorDialog.c:59-68），API 保留 per-instance 形态但全部实例共享。

**可覆写点**：`XCLASS_DEFINE_BEGING(XColorDialog)`+`XCLASS_DEFINE_EXTEND_END(XColorDialog, XDialog)`（XColorDialog.h:52-53），不新增槽。重载（XColorDialog.c:146-153）：`EXWidget_ShowEvent`→`VXColorDialog_showEvent`（按真实装饰条高精修布局）；`EXClass_Deinit`→`VXColorDialog_deinit`。无公开 `_base` 入口；`XColorDialog_done` 本身即"收口漏斗"（XColorDialog.h:203-207 称沿用 XWizard_done 惯例）。

**注意事项与坑点**

1. **`XColorDialog_open` 与头注释不符**：头注称"以非模态方式打开…（对标 QDialog::open；show + 置模态）"（XColorDialog.h:196-198），实现只有 `XWidget_show`（XColorDialog.c:955-959），未设任何窗口模态；真正带窗口模态语义的是 `open_2`→`XDialog_open`（XColorDialog.c:1010-1018，XDialog.c:1385-1388）。
2. **selectedColor 初值口径偏差**：Qt 的 `selectedColor` 在接受前是默认构造的无效 QColor（qcolordialog.cpp:142 成员、:2033-2036 返回、:2157 复位），而本库 init 直接写入 initial（XColorDialog.c:174），已由 apitest 断言并注明为"本库口径"（`Test/XGuiDemo/xgui_demo_apitest_dialogs.c:1682-1686`）。
3. **customCount 与 Qt 不同**：Qt 的 `QColorDialog::customCount()` 恒返回 16（qcolordialog.cpp:513-516；常量 `CustomColorCount = 16` 见 qplatformdialoghelper.cpp:242），本库返回"已设槽高水位"`g_xcdCustomCount`（XColorDialog.c:915-919、:921-929），头文件 XColorDialog.h:183-185 已自陈该差异。
4. **标准色表尺寸 48**：`setStandardColor`/`standardColor` 只接受 index<48（XColorDialog.c:937-955），与 8 列×6 行网格一致（XColorDialog.c:240-243），写 48 及以上静默丢弃。
5. **无 Alpha 通道输入**：XGui.md §8.1 登记"XColorDialog 无 Alpha 通道输入、48 标准色为简化生成"（XGui.md:1601-1602）。
6. **getColor 的无头路径仍建对象**：无 `XCoreApplication` 实例时也 create 一个对话框、设标题与选项、再 `XClassDelete` 后返回 initial（XColorDialog.c:669-679）——便于验证构造覆盖，但调用方不应假设"无头就零副作用"。
7. **取消路径的输入摘除**：确定/取消槽在 clicked 发射帧内运行，取消槽先 `xcd_detachInputRecursive` 再 done（XColorDialog.c:647-659），否则按压靶指向已释放按钮（UAF，ASan 实抓，XColorDialog.c:75-81 注）。自定义槽若在 colorSelected 里同步删除对话框，同理须走延迟回收。

**测试覆盖**：无 `Test/XGuiTest/XColorDialogTest.c`。间接覆盖：`rg -c "XColorDialog_" xgui_regression_test.c`=23 处（39432-39473：create/getColor/getColor_2/信号/选项位往返）；`Test/XGuiDemo/xgui_demo_apitest_dialogs.c:1661-1727`（currentColor/selectedColor/currentColorChanged/选项位）；`Test/XGuiDemo/*.c` 合计 53 处；另有 `Test/XGuiDemo/xgui_dialog_portrait_probe.c` 目验工具。

---

## XComboBox

**定位**：文本项下拉组合框（项管理/枚举策略/弹出列表/可编辑 + 内建前缀补全）。继承链 `XObject ← XWidget ← XComboBox`。头文件 `Src/XGui/Widget/XComboBox.h`（742 行）。开关 `XCOMBOBOX_ON`，另需 `XWIDGET_ON && XLINEEDIT_ON`（XComboBox.h:63）。

**生命周期**：`XComboBox_init` 置 m_currentIndex=-1、maxCount=2147483647、maxVisibleItems=10、iconSize=16、frame=true、insertPolicy=InsertAtBottom、sizeAdjustPolicy=AdjustToContents、grabTimer=XTIMER_INVALID_ID，并开 Hover 属性（XComboBox.c:626-666）；堆对象 `_create/_create_ex` 后 `XClassDelete`，`deinit` 由类重载释放项数组/模型/弹层（XComboBox.c:601-624）。

**API 面**

枚举：`XComboBoxInsertPolicy`（NoInsert=0…InsertAlphabetically=6，与 qcombobox.h:67-75 同序）、`XComboBoxSizeAdjustPolicy`（0/1/2，与 qcombobox.h:81-85 同序）（XComboBox.h:68-85）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XComboBox_class_init(void);` | 建表（继承 XWidget） | X 扩展 | 共享表，禁释放 |
| `void XComboBox_init(XComboBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化默认值 | `QComboBox(QWidget*)` qcombobox.h:45 | — |
| `#define XComboBox_create(parent, flags) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XComboBox* XComboBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `int XComboBox_count(const XComboBox* self);` | 项数 | `count` qcombobox.h:51 | — |
| `int XComboBox_maxCount(const XComboBox* self);` / `void XComboBox_setMaxCount(XComboBox* self, int max);` | 容量上限（setter 负值忽略、超限立即裁尾，XComboBox.c:683-688） | `maxCount`/`setMaxCount` qcombobox.h:53/52 | — |
| `int XComboBox_maxVisibleItems(const XComboBox* self);` / `void XComboBox_setMaxVisibleItems(XComboBox* self, int maxItems);` | 弹出可见行数（setter 只收 >0，XComboBox.c:694-697） | `maxVisibleItems`/`setMaxVisibleItems` qcombobox.h:48/49 | — |
| `bool XComboBox_duplicatesEnabled(const XComboBox* self);` / `void XComboBox_setDuplicatesEnabled(XComboBox* self, bool enable);` | 允许重复项 | `duplicatesEnabled`/`setDuplicatesEnabled` qcombobox.h:55/56 | — |
| `void XComboBox_setFrame(XComboBox* self, bool on);` / `bool XComboBox_hasFrame(const XComboBox* self);` | 边框开关（默认 true） | `setFrame`/`hasFrame` qcombobox.h:58/59 | — |
| `int XComboBox_insertPolicy(const XComboBox* self);` / `void XComboBox_setInsertPolicy(XComboBox* self, int policy);` | 插入策略（setter 只收 0..6，XComboBox.c:712-715；只读框无效果） | `insertPolicy`/`setInsertPolicy` qcombobox.h:78/79 | — |
| `int XComboBox_sizeAdjustPolicy(const XComboBox* self);` / `void XComboBox_setSizeAdjustPolicy(XComboBox* self, int policy);` | 尺寸策略（setter 只收 0..2 并刷新 sizeHint） | `sizeAdjustPolicy`/`setSizeAdjustPolicy` qcombobox.h:88/89 | — |
| `int XComboBox_minimumContentsLength(const XComboBox* self);` / `void XComboBox_setMinimumContentsLength(XComboBox* self, int characters);` | 最小内容字符数 | `minimumContentsLength`/`setMinimumContentsLength` qcombobox.h:90/91 | — |
| `XString* XComboBox_placeholderText(const XComboBox* self);` | 占位文本拷贝；未设返回 NULL | `placeholderText` qcombobox.h:96 | **新建，须 `XClassDelete`** |
| `const char* XComboBox_placeholderText_2(const XComboBox* self);` | 占位文本 UTF-8 借用；未设返回 "" | 同上 | 借用，禁释放/修改 |
| `void XComboBox_setPlaceholderText(XComboBox* self, const XString* placeholderText);` | 设占位文本（NULL 按空串） | `setPlaceholderText` qcombobox.h:95 | 借用 |
| `void XComboBox_setPlaceholderText_2(XComboBox* self, const char* placeholderText);` | UTF-8 重载转发主版本 | `setPlaceholderText` qcombobox.h:95 | 借用 |
| `bool XComboBox_isEditable(const XComboBox* self);` / `void XComboBox_setEditable(XComboBox* self, bool editable);` | 可编辑开关；打开时懒建内嵌 XComboEdit，关闭时 deleteLater 回收 | `isEditable`/`setEditable` qcombobox.h:98/99 | — |
| `XLineEdit* XComboBox_lineEdit(const XComboBox* self);` | 查内嵌编辑框 | `lineEdit` qcombobox.h:101 | 借用（属组合框） |
| `void XComboBox_setLineEdit(XComboBox* self, XLineEdit* edit);` | 安装自定义编辑框 | `setLineEdit` qcombobox.h:100 | **所有权转移给组合框**，旧框 deleteLater，调用方禁重复释放 |
| `XListView* XComboBox_view(XComboBox* self);` | 懒建/返回弹出视图 | `view` qcombobox.h:150 | 借用（属组合框） |
| `void XComboBox_setView(XComboBox* self, XListView* view);` | 接管自定义视图 | `setView` qcombobox.h:151 | **所有权转移**，旧视图释放 |
| `XAbstractItemModel* XComboBox_model(XComboBox* self);` | 懒建/返回条目模型 | `model` qcombobox.h:115 | 借用（属组合框） |
| `void XComboBox_setModel(XComboBox* self, XAbstractItemModel* model);` | 接管外部模型（NULL 仅清并释放当前） | `setModel` qcombobox.h:116 | **所有权转移** |
| `int XComboBox_modelColumn(const XComboBox* self);` / `void XComboBox_setModelColumn(XComboBox* self, int column);` | 模型显示列 | `modelColumn`/`setModelColumn` qcombobox.h:121/122 | — |
| `void XComboBox_setRootModelIndex(XComboBox* self, int row, int col);` / `void XComboBox_rootModelIndex(const XComboBox* self, int* row, int* col);` | 根索引（平铺 row/col 简化） | `setRootModelIndex`/`rootModelIndex` qcombobox.h:119/118 | — |
| `void XComboBox_setValidator(XComboBox* self, void* validator);` / `void* XComboBox_validator(const XComboBox* self);` | 校验器（不透明仅承载，不参与过滤） | `setValidator`/`validator` qcombobox.h:103/104 | 借用，不取得所有权 |
| `void XComboBox_setItemDelegate(XComboBox* self, void* delegate);` / `void* XComboBox_itemDelegate(const XComboBox* self);` | 条目委托（不透明仅承载） | `setItemDelegate`/`itemDelegate` qcombobox.h:113/112 | 借用 |
| `XString* XComboBox_inputMethodQuery(XComboBox* self, int query);` | 输入法查询（编辑文本/当前项文本） | `inputMethodQuery` qcombobox.h:160 | **新建，须 `XClassDelete`** |
| `int XComboBox_currentIndex(const XComboBox* self);` | 当前项（-1 无） | `currentIndex` qcombobox.h:124 | — |
| `XString* XComboBox_currentText(const XComboBox* self);` | 当前文本拷贝（可编辑分叉时取编辑框文本） | `currentText` qcombobox.h:125 | **新建，须 `XClassDelete`** |
| `const char* XComboBox_currentText_2(const XComboBox* self);` | 当前文本借用（分叉态借编辑框缓冲） | 同上 | 借用，禁释放 |
| `XString* XComboBox_itemText(const XComboBox* self, int index);` | 项文本拷贝；越界 NULL | `itemText` qcombobox.h:128 | **新建，须 `XClassDelete`** |
| `const char* XComboBox_itemText_2(const XComboBox* self, int index);` | 项文本借用；越界 "" | 同上 | 借用 |
| `int XComboBox_findText(const XComboBox* self, const XString* text);` / `int XComboBox_findText_2(const XComboBox* self, const char* text);` | 按文本查找（大小写敏感，首命中） | `findText` qcombobox.h:61-63 | text 借用 |
| `void XComboBox_insertItem(XComboBox* self, int index, const XString* text);` | 指定索引插入（负数插最前、超界追加） | `insertItem` qcombobox.h:138-139 | text 借用 |
| `void XComboBox_insertItem_2(XComboBox* self, int index, const char* text);` | 同上，UTF-8 重载 | `insertItem` qcombobox.h:138-139 | text 借用 |
| `void XComboBox_insertItems(XComboBox* self, int index, const XStringList* texts);` | 批量插入（元素 XString*） | `insertItems` qcombobox.h:141 | texts 借用 |
| `void XComboBox_insertItems_2(XComboBox* self, int index, const char* const* texts);` | 批量插入（NULL 结尾 UTF-8 数组） | `insertItems` qcombobox.h:141 | texts 借用 |
| `void XComboBox_addItem(XComboBox* self, const XString* text);` | 尾部追加 | `addItem` qcombobox.h:132-133 | text 借用 |
| `void XComboBox_addItem_2(XComboBox* self, const char* text);` | 同上，UTF-8 重载 | `addItem` qcombobox.h:132-133 | text 借用 |
| `void XComboBox_addItems(XComboBox* self, const XStringList* texts);` | 批量追加 | `addItems` qcombobox.h:135 | texts 借用 |
| `void XComboBox_addItems_2(XComboBox* self, const char* const* texts);` | 批量追加（NULL 结尾 UTF-8 数组） | `addItems` qcombobox.h:135 | texts 借用 |
| `void XComboBox_insertSeparator(XComboBox* self, int index);` | 插入分隔项（实现按文本 "---------" 插入，XComboBox.c:2132-2135） | `insertSeparator` qcombobox.h:142 | — |
| `void XComboBox_removeItem(XComboBox* self, int index);` | 删项并修正当前项 | `removeItem` qcombobox.h:144 | — |
| `void XComboBox_setItemText(XComboBox* self, int index, const XString* text);` / `void XComboBox_setItemText_2(XComboBox* self, int index, const char* text);` | 改项文本（XString 主版本 / UTF-8 重载） | `setItemText` qcombobox.h:146 | text 借用 |
| `void XComboBox_clear(XComboBox* self);` | 清空全部项 | `clear` qcombobox.h:164 | — |
| `void XComboBox_showPopup_base(XComboBox* self);` | 全量弹出项列表 | `showPopup` qcombobox.h:156 | — |
| `void XComboBox_hidePopup_base(XComboBox* self);` | 收起弹层、解抓取、焦点回交 | `hidePopup` qcombobox.h:157 | — |
| `bool XComboBox_popupVisible(const XComboBox* self);` | 弹层可见态 | X 扩展（Qt 无公开 getter） | — |
| `void XComboBox_setCurrentIndex(XComboBox* self, int index);` | 设当前项（联动编辑框/信号） | `setCurrentIndex` qcombobox.h:167 | — |
| `void XComboBox_setCurrentText(XComboBox* self, const XString* text);` / `void XComboBox_setCurrentText_2(XComboBox* self, const char* text);` | 按文本设当前项（XString 主版本 / UTF-8 重载） | `setCurrentText` qcombobox.h:168 | text 借用 |
| `void XComboBox_clearEditText(XComboBox* self);` | 清空编辑框 | `clearEditText` qcombobox.h:165 | — |
| `void XComboBox_setEditText(XComboBox* self, const XString* text);` / `void XComboBox_setEditText_2(XComboBox* self, const char* text);` | 设编辑框文本（XString 主版本 / UTF-8 重载） | `setEditText` qcombobox.h:166 | text 借用 |
| `void XComboBox_setItemIcon(XComboBox* self, int index, const XString* path);` / `void XComboBox_setItemIcon_2(XComboBox* self, int index, const char* path);` | 设项图标**路径**（Qt 为 QIcon），NULL 清除 | `setItemIcon` qcombobox.h:147（路径简化） | path 借用 |
| `const XString* XComboBox_itemIcon(const XComboBox* self, int index);` / `const char* XComboBox_itemIcon_2(const XComboBox* self, int index);` | 读项图标路径（内部 XString 借用 / UTF-8 借用） | `itemIcon` qcombobox.h:129 | 借用 |
| `void XComboBox_setItemData(XComboBox* self, int index, const XString* data);` / `void XComboBox_setItemData_2(XComboBox* self, int index, const char* data);` | 设项数据（字符串简化，Qt 为 QVariant+role），NULL 清除 | `setItemData` qcombobox.h:148（简化） | data 借用 |
| `const XString* XComboBox_itemData(const XComboBox* self, int index);` / `const char* XComboBox_itemData_2(const XComboBox* self, int index);` | 读项数据（内部 XString 借用 / UTF-8 借用） | `itemData` qcombobox.h:130 | 借用 |
| `const XString* XComboBox_currentData(const XComboBox* self);` | 当前项数据（等价 itemData(currentIndex)） | `currentData` qcombobox.h:126 | 借用，禁释放 |
| `int XComboBox_findData(const XComboBox* self, const XString* data);` / `int XComboBox_findData_2(const XComboBox* self, const char* data);` | 按数据查找（未命中 -1） | `findData` qcombobox.h:64 | data 借用 |
| `void XComboBox_setCompleter(XComboBox* self, XCompleter* completer);` | 安装补全器（借用；无模型时接通条目模型并置 completerMode） | `setCompleter` qcombobox.h:108 | 借用，不拥有 |
| `XCompleter* XComboBox_completer(const XComboBox* self);` | 查补全器 | `completer` qcombobox.h:109 | 借用 |
| `void XComboBox_setCompleterMode(XComboBox* self, bool enable);` | 内建前缀过滤补全开关（关时立即收层） | X 扩展（Qt 无对应开关） | — |
| `bool XComboBox_isCompleterMode(const XComboBox* self);` | 查前缀补全开关 | X 扩展 | — |
| `void XComboBox_setIconSize(XComboBox* self, int size);` | 设方边图标像素（只收 >0） | `setIconSize` qcombobox.h:93（方边简化） | — |
| `int XComboBox_iconSize(const XComboBox* self);` | 查方边像素 | `iconSize` qcombobox.h:92 | — |
| `void* XComboBox_activated_signal(XComboBox* self, int index);` | activated(int)，用户激活路径发射 | `activated` qcombobox.h:172 | 信号标识地址 |
| `void* XComboBox_textActivated_signal(XComboBox* self, const char* text);` | textActivated(text) | `textActivated` qcombobox.h:173 | 信号标识地址 |
| `void* XComboBox_highlighted_signal(XComboBox* self, int index);` | highlighted(int) | `highlighted` qcombobox.h:174 | 信号标识地址 |
| `void* XComboBox_textHighlighted_signal(XComboBox* self, const char* text);` | textHighlighted(text) | `textHighlighted` qcombobox.h:175 | 信号标识地址 |
| `void* XComboBox_currentIndexChanged_signal(XComboBox* self, int index);` | currentIndexChanged(int) | qcombobox.h:176 | 信号标识地址 |
| `void* XComboBox_currentTextChanged_signal(XComboBox* self, const char* text);` | currentTextChanged(text) | qcombobox.h:177 | 信号标识地址 |
| `void* XComboBox_editTextChanged_signal(XComboBox* self, const char* text);` | editTextChanged(text) | qcombobox.h:171 | 信号标识地址 |
| `void* XComboBox_popupShown_signal(XComboBox* self);` / `void* XComboBox_popupHidden_signal(XComboBox* self);` | 弹层显隐信号 | X 扩展 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_2` 在此类兼两义——UTF-8 文本重载（`addItem_2`/`itemText_2`/`setEditText_2` 等）与"UTF-8 字符串数组"批量重载（`addItems_2`/`insertItems_2`）；`_base` 后缀=弹层实现入口（`showPopup_base`/`hidePopup_base`），**不是** UTF-8 族。查询族分两轨：返回 `XString*` 的为新建拷贝须 `XClassDelete`（`placeholderText`/`currentText`/`itemText`/`inputMethodQuery`），返回 `const char*`/`const XString*` 的为借用禁释放（`*_2`、`itemIcon`、`itemData`、`currentData`）。三态 `set*`：`setEditable`、`setFrame`、`setDuplicatesEnabled`、`setCompleterMode` 均为二态 bool，无三态 setter。

**可覆写点**：`XCLASS_DEFINE_BEGING(XComboBox)`+`XCLASS_DEFINE_EXTEND_END(XComboBox, XWidget)`（XComboBox.h:88-89），不新增槽。重载（XComboBox.c:601-624）：`EXWidget_SizeHint`/`MinimumSizeHint`→按内容/策略算；`PaintEvent`→自绘框+文本+箭头；`MousePress/MouseMove/MouseRelease`→按压展开与弹层拖动；`ChangeEvent`→样式/使能变化刷新；`KeyPressEvent`→键盘换项；`WheelEvent`→非弹层非编辑态按 angleDelta 换项并发 activated（XComboBox.c:614-617）；`EXObject_TimerEvent`→弹层延迟抓取定时器；`EXClass_Copy/Move/Deinit`。无公开 `_base` 多态入口（`showPopup_base`/`hidePopup_base` 是普通函数，不经虚表分派）。

**注意事项与坑点**

1. **`showPopup/hidePopup` 并非虚槽**：头注释写"弹出：showPopup/hidePopup（虚槽…）"（XComboBox.h:17-18），但虚表定义只有 `XCLASS_DEFINE_EXTEND_END(XComboBox, XWidget)`（XComboBox.h:88-89），`class_init` 也未注册任何 popup 槽（XComboBox.c:601-624；`rg "ShowPopup|HidePopup" Src/` 零命中）。实际入口是普通函数 `XComboBox_showPopup_base`/`hidePopup_base`（XComboBox.h:547-552），名字里的 `_base` 会让人误以为可被虚表调度。
2. **`XCOMBOBOX_MAX_ITEMS` 不存在**：头注释称项数组"上限 XCOMBOBOX_MAX_ITEMS"（XComboBox.h:94-95），但全仓 `rg "XCOMBOBOX_MAX_ITEMS"` 只命中该注释行本身；实现按需倍增扩容（XComboBox.c:2010-2028），实际上限只由 `maxCount` 控制（默认 2147483647，XComboBox.c:639）。
3. **可编辑 `currentText` 的双源语义**：编辑框文本与当前项分叉时返回编辑框文本，同步态返回项文本（XComboBox.h:421-428；实现 `xcombo_editTextSource` 在文本相等时返回 NULL 以回落项文本，XComboBox.c:1883-1897）。`*_2` 借用版在分叉态借的是**行编辑内部缓冲**，任何编辑文本变更后即失效（XComboBox.h:430-435）。
4. **条目文本/图标/数据三组数组平行且各自拥有**：`m_items`/`m_itemData`/`m_itemIcons` 都是 `XString**` 拥有型（XComboBox.h:114-116），`setItemData`/`setItemIcon` 传 NULL 即清除对应槽（XComboBox.h:596、:609）。
5. **补全器是借用且析构不回调**：`setCompleter(NULL)` 只清指针、不关 `completerMode`；组合框析构不回调补全器，调用方须保证补全器不悬空使用失效模型（XComboBox.h:638-657；实现 XComboBox.c:2837-2873，替换时若旧补全器仍回指本框则清其 widget 关联）。
6. **安装编辑框/视图/模型均转移所有权**：`setLineEdit`/`setView`/`setModel` 会释放旧对象（编辑框走 `XObject_deleteLater` 防同帧 UAF，XComboBox.c:1271-1276），调用方之后不得再释放传入对象（XComboBox.h:293-303、:315-324、:333-342）。
7. **`insertSeparator` 是伪分隔**：实现插入字面量 `"---------"` 文本项（XComboBox.c:2132-2135），不是不可选项；头注释未声明这一点（XComboBox.h:513-518）。
8. **策略 setter 静默丢弃非法值**：`setMaxVisibleItems(<=0)`、`setInsertPolicy` 越界 0..6、`setSizeAdjustPolicy` 越界 0..2、`setIconSize(<=0)` 均无动作也无返回值（XComboBox.c:694-697、:712-715、:723-729、:2869-2875）。

**测试覆盖**：有 `Test/XGuiTest/XComboBoxTest.c`（153 行），入口 `bool XComboBoxTest_runAll(void)`（XComboBoxTest.c:26），**30 个断言调用点**（`grep "cb_expect(" 去定义行` 计数 30；含定义共 31）。覆盖：默认值/首插自动置 0 并发一次 currentIndexChanged（:44-48）、setCurrentIndex/findText/itemText、insert/remove/setItemText、showPopup/hidePopup 可见态、editable+placeholder、maxCount 钳位、图标/数据/findData/iconSize、completer NULL 清除、clear、500 字节超长文本不截断。注册于 `Test/CMakeLists.txt:560`，由 `xgui_regression_test.c:36232` 调用。

---

## XCommandLinkButton

**定位**：命令链接按钮（标题 + 描述双行 + 左侧大图标 + 右侧箭头，heightForWidth 参与布局）。继承链 `XObject ← XWidget ← XAbstractButton ← XPushButton ← XCommandLinkButton`。头文件 `Src/XGui/Widget/XCommandLinkButton.h`（262 行）。开关 `XCOMMANDLINKBUTTON_ON`，另需 `XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON`（XCommandLinkButton.h:40）。

**生命周期**：`XCommandLinkButton_init` 是 `XPushButton_init` → 建空 description → 尺寸策略 Preferred/Preferred+PushButton+heightForWidth 位 → 注册 heightForWidth 回调 → 图标尺寸 20×20（XCommandLinkButton.c:576-601）；析构重载 `EXClass_Deinit` 释放 description（XCommandLinkButton.c:558-574）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XCommandLinkButton_class_init(void);` | 建表并登记派生类 | X 扩展 | 共享表，禁释放 |
| `void XCommandLinkButton_init(XCommandLinkButton* self, XWidget* parent, XWidgetFlags flags);` | 初始化（策略/图标 20×20/空描述） | `QCommandLinkButton(QWidget*)` qcommandlinkbutton.h:25 | — |
| `#define XCommandLinkButton_create(parent, flags) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XCommandLinkButton* XCommandLinkButton_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `#define XCommandLinkButton_text(self)` / `XCommandLinkButton_setText(self, text)` / `XCommandLinkButton_setText_2(self, utf8)` | 标题文本读写 | `text`/`setText` qabstractbutton.h:45/44 | XString/UTF-8 借用 |
| `#define XCommandLinkButton_icon(self)` / `XCommandLinkButton_setIcon(self, icon)` | 图标读写 | qabstractbutton.h:48/47 | 值语义 |
| `#define XCommandLinkButton_iconSize(self)` / `XCommandLinkButton_setIconSize(self, size)` | 图标尺寸读写（默认 20×20） | qabstractbutton.h:50/82 | 值 |
| `#define XCommandLinkButton_isCheckable(self)` / `XCommandLinkButton_setCheckable(self, checkable)` | 可选中位读写 | `isCheckable`/`setCheckable` qabstractbutton.h:58/57 | — |
| `#define XCommandLinkButton_isChecked(self)` / `XCommandLinkButton_setChecked(self, checked)` | 选中态读写 | `isChecked`/`setChecked` qabstractbutton.h:60/86 | — |
| `#define XCommandLinkButton_toggle(self)` | 切换选中 | `toggle` qabstractbutton.h:85 | — |
| `#define XCommandLinkButton_isDown(self)` / `XCommandLinkButton_setDown(self, down)` | 按下面板态 | qabstractbutton.h:63/62 | — |
| `#define XCommandLinkButton_autoRepeat(self)` / `XCommandLinkButton_setAutoRepeat(self, repeat)` | 自动重复开关 | qabstractbutton.h:66/65 | — |
| `#define XCommandLinkButton_autoRepeatDelay(self)` / `XCommandLinkButton_setAutoRepeatDelay(self, delay)` | 自动重复起始延迟 | qabstractbutton.h:69/68 | — |
| `#define XCommandLinkButton_autoRepeatInterval(self)` / `XCommandLinkButton_setAutoRepeatInterval(self, interval)` | 自动重复间隔 | qabstractbutton.h:72/71 | — |
| `#define XCommandLinkButton_autoExclusive(self)` / `XCommandLinkButton_setAutoExclusive(self, exclusive)` | 自动互斥位 | qabstractbutton.h:75/74 | — |
| `#define XCommandLinkButton_click(self)` / `XCommandLinkButton_animateClick(self)` | 程序化/动画点击 | qabstractbutton.h:84/83 | — |
| `#define XCommandLinkButton_pressed_signal(self)` | pressed 信号标识 | `pressed` qabstractbutton.h:89 | 信号标识地址 |
| `#define XCommandLinkButton_released_signal(self)` | released 信号标识 | qabstractbutton.h:90 | 信号标识地址 |
| `#define XCommandLinkButton_clicked_signal(self, checked)` | clicked(bool) 信号标识 | qabstractbutton.h:91 | 信号标识地址 |
| `#define XCommandLinkButton_toggled_signal(self, checked)` | toggled(bool) 信号标识 | qabstractbutton.h:92 | 信号标识地址 |
| `#define XCommandLinkButton_autoDefault(self)` / `XCommandLinkButton_setAutoDefault(self, enable)` | 自动默认按钮转发 | `autoDefault`/`setAutoDefault` qpushbutton.h:36/35 | — |
| `#define XCommandLinkButton_isDefault(self)` / `XCommandLinkButton_setDefault(self, enable)` | 默认按钮转发 | `isDefault`/`setDefault` qpushbutton.h:38/39 | — |
| `#define XCommandLinkButton_isFlat(self)` / `XCommandLinkButton_setFlat(self, flat)` | 扁平按钮转发 | `isFlat`/`setFlat` qpushbutton.h:47/46 | — |
| `#define XCommandLinkButton_setMenu(self, menu)` / `XCommandLinkButton_menu(self)` / `XCommandLinkButton_showMenu(self)` | 关联菜单转发 | `setMenu`/`menu` qpushbutton.h:42/43、`showMenu` qpushbutton.h:51 | 菜单借用 |
| `#define XCommandLinkButton_hitButton(self, pos)` | 命中转发（XPushButton 实现） | `hitButton` qpushbutton.h:62 | — |
| `const XString* XCommandLinkButton_description(const XCommandLinkButton* self);` | 读描述（借用） | `description` qcommandlinkbutton.h:30 | 借用，禁释放/修改 |
| `void XCommandLinkButton_setDescription(XCommandLinkButton* self, const XString* description);` | 深拷贝描述，刷新 sizeHint+重绘；NULL 视为空 | `setDescription` qcommandlinkbutton.h:31 | 源借用，内部拥有副本 |
| `void XCommandLinkButton_setDescription_2(XCommandLinkButton* self, const char* utf8);` | UTF-8 重载（临时对象转发后释放） | `setDescription` qcommandlinkbutton.h:31 | UTF-8 借用 |
| `XSize XCommandLinkButton_sizeHint(const XCommandLinkButton* self);` | 标题+描述两行与左侧图标算宽高 | `sizeHint` qcommandlinkbutton.h:33 | 值 |
| `XSize XCommandLinkButton_minimumSizeHint(const XCommandLinkButton* self);` | 同 sizeHint（XCommandLinkButton.c:277-280） | `minimumSizeHint` qcommandlinkbutton.h:35 | 值 |
| `int XCommandLinkButton_heightForWidth(const XCommandLinkButton* self, int width);` | 按宽算高（转 `XWidget_heightForWidth` 走已注册回调） | `heightForWidth` qcommandlinkbutton.h:34 | 值 |
| `void XCommandLinkButton_drawContents(XCommandLinkButton* self, XPainter* painter);` | 离屏绘制（背景/大图标/双行文本/右箭头） | `paintEvent` qcommandlinkbutton.h:41 | painter 借用 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_2`=UTF-8 设置侧（`setText_2`/`setDescription_2`）；三态 `set*`：`setAutoDefault`/`setDefault`/`setFlat` 的公开形参均为 bool（XCommandLinkButton.h:168-179）；其中 **autoDefault 在 XPushButton 内部是三态** `XPushButtonAutoDefault`（Auto=0/Off=1/On=2，XPushButton.h:70-75），`setAutoDefault(bool)` 只写 On/Off（XPushButton.c:319-327）、读取时 Auto 按父对话框推断（XPushButton.c:309-317），因此宏转发无法把状态拨回 Auto。`description` 返回借用禁释放，与 `*_2` 设置形成"借用读/拷贝写"非对称。

**可覆写点**：`XCLASS_DEFINE_BEGING(XCommandLinkButton)`+`XCLASS_DEFINE_EXTEND_END(XCommandLinkButton, XPushButton)`（XCommandLinkButton.h:50-51）。重载（XCommandLinkButton.c:558-574）：`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`→两行+图标重算；`EXAbstractButton_ContentChanged`→文本/图标/描述变化刷新 sizeHint；`EXWidget_PaintEvent`；`EXClass_Copy/Move/Deinit`。heightForWidth 走回调而非虚槽（`XWidget_setHeightForWidthHandler`，XCommandLinkButton.c:595-597；机制见 XWidget.c:7360-7383）。`_base` 入口：`XAbstractButton_contentChanged_base`（XAbstractButton_Protected.h:155）。

**注意事项与坑点**

1. **`minimumSizeHint` 的 Qt 口径说法不准确**：头注释写"Qt 中直接返回 sizeHint()"（XCommandLinkButton.h:222），Qt 6.8.3 实际是 `sizeHint()` 再按"描述偏移+底边距 vs 图标高+顶边距"抬高度（qcommandlinkbutton.cpp:222-230）；X 侧确实直接返回 sizeHint（XCommandLinkButton.c:277-280），差异被头注释误述成"Qt 同款"。
2. **heightForWidth 是估算折行**：描述行数用 `ceil(描述宽/可用宽)`（XCommandLinkButton.c:165-190 `commandlink_descriptionHeight`），Qt 经 QTextLayout 逐行断词，行边界可能有出入（XCommandLinkButton.h:19-20、:228-231）。
3. **heightForWidth 依赖 init 里注册的回调**：直接 `XMemset` 后手工填字段而不走 `XCommandLinkButton_init`，`XCommandLinkButton_heightForWidth` 会因无回调而退化为 sizeHint 高度（XWidget.c:7370-7378）；`XWidget_hasHeightForWidth` 为假时直接返回 -1。
4. **描述变化才刷新几何**：`setDescription` 文本相等时早退（XCommandLinkButton.c:240-244），但 NULL→空串会重建对象并刷新（:245-253）；每次实变都 `updateGeometry`+`update`。
5. **SP_CommandLink 标准图标未接入**：无图标时只绘制文本（XCommandLinkButton.h:21）；快捷键与样式 bevel 未实现（同处）。
6. **`_2` 与主版本的所有权差异**：`setDescription_2` 内部建临时 XString 转发后 `XClassDelete`（XCommandLinkButton.c:256-268），调用方传栈/静态 UTF-8 即可，无需管理临时对象。

**测试覆盖**：无 `Test/XGuiTest/XCommandLinkButtonTest.c`。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_buttons.c:1226-1289`（默认 description 空、图标尺寸 20、sizeHint 随描述变高、minimumSizeHint、heightForWidth 宽窄对比、`setDescription(NULL)`、UTF-8 往返）；`rg -c "XCommandLinkButton_" xgui_regression_test.c`=8 处；`Test/XGuiDemo/*.c` 合计 41 处；`xgui_window_demo.c` 另有演示页使用。XGui.md §8.2 记录过其观感修复"命令链接按钮双行自适应居中+14x14 右向大箭头（首版方向镜像已勘误）"（XGui.md:1782）。

---

## XCompleter

**定位**：补全对象（模型前缀过滤 + 完成列表 + 内建/外接弹出列表 + 采纳信号）；`QCompleter : QObject` 对应 `XCompleter : XObject`，非控件。继承链 `XObject ← XCompleter`。头文件 `Src/XGui/Widget/XCompleter.h`（609 行）。门控 `XWIDGET_ON && XTABLEWIDGET_ON`（XCompleter.h:57，随 `XAbstractItemModel` 同门控）。

**生命周期**：`XCompleter_init` 置 PopupCompletion/StartsWith/UnsortedModel/column 0/role -1/CaseSensitive/maxVisibleItems 7/wrapAround true/currentRow -1，并建 `m_matches` 向量（XCompleter.c:316-338）；`deinit` 停守护定时器、释放 prefix/currentCompletion/matches（XCompleter.c:288-306）；`create`/`create_2`（带模型）为堆构造，须 `XClassDelete`。

**API 面**

枚举：`XCompleterCompletionMode`（PopupCompletion=0/UnfilteredPopupCompletion=1/InlineCompletion=2，与 qcompleter.h:37-41 同序）、`XCompleterFilterMode`（StartsWith=0/Contains=1/EndsWith=2，**自定义三值**，Qt 6.8 为 `Qt::MatchFlags` 位掩码 qcompleter.h:28/67-68）、`XCompleterModelSorting`（0/1/2，与 qcompleter.h:44-48 一致）（XCompleter.h:69-108）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XCompleter_class_init(void);` | 建表（继承 XObject，另重载 Deinit/TimerEvent） | X 扩展 | 共享表，禁释放 |
| `void XCompleter_init(XCompleter* self, XObject* parent);` | 默认初始化（嵌入式） | `QCompleter(QObject*)` qcompleter.h:51 | — |
| `#define XCompleter_create(parent) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XCompleter* XCompleter_create_ex(XMemoryType memory, XObject* parent);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `#define XCompleter_create_2(model, parent) …` | 带模型建堆对象 | `QCompleter(QAbstractItemModel*,QObject*)` qcompleter.h:52 | 模型借用 |
| `XCompleter* XCompleter_create_2_ex(XMemoryType memory, XAbstractItemModel* model, XObject* parent);` | 指定内存+模型建堆对象 | X 扩展 | 模型借用 |
| `void XCompleter_setModel(XCompleter* self, XAbstractItemModel* model);` | 设模型并重建完成列表 | `setModel` qcompleter.h:61 | 借用，不拥有 |
| `XAbstractItemModel* XCompleter_model(const XCompleter* self);` | 查模型 | `model` qcompleter.h:62 | 借用 |
| `void XCompleter_setCompletionMode(XCompleter* self, XCompleterCompletionMode mode);` | 设补全模式（仅存储，UI 由控件层接入） | `setCompletionMode` qcompleter.h:64 | — |
| `XCompleterCompletionMode XCompleter_completionMode(const XCompleter* self);` | 查模式；NULL 返 PopupCompletion | `completionMode` qcompleter.h:65 | 值 |
| `void XCompleter_setCompletionPrefix(XCompleter* self, const XString* prefix);` | 深拷贝前缀并重建；首项变化发 highlighted 对 | `setCompletionPrefix` qcompleter.h:102 | prefix 借用，内部拥有副本 |
| `void XCompleter_setCompletionPrefix_2(XCompleter* self, const char* utf8);` | UTF-8 重载 | 同上 | UTF-8 借用 |
| `XString* XCompleter_completionPrefix(const XCompleter* self);` | 前缀拷贝 | `completionPrefix` qcompleter.h:99 | **新建，须 `XClassDelete`** |
| `void XCompleter_setCompletionColumn(XCompleter* self, int column);` | 设补全列并重建 | `setCompletionColumn` qcompleter.h:79 | — |
| `int XCompleter_completionColumn(const XCompleter* self);` | 查补全列 | `completionColumn` qcompleter.h:80 | 值 |
| `void XCompleter_setCompletionRole(XCompleter* self, int role);` | 设角色（**仅存储**，模型 data 无 role 参数） | `setCompletionRole` qcompleter.h:82 | — |
| `int XCompleter_completionRole(const XCompleter* self);` | 查角色；NULL 返 -1 | `completionRole` qcompleter.h:83 | 值 |
| `void XCompleter_setCaseSensitivity(XCompleter* self, XChar_CaseSensitivity cs);` | 设大小写并重建 | `setCaseSensitivity` qcompleter.h:73 | — |
| `XChar_CaseSensitivity XCompleter_caseSensitivity(const XCompleter* self);` | 查大小写 | `caseSensitivity` qcompleter.h:74 | 值 |
| `void XCompleter_setFilterMode(XCompleter* self, XCompleterFilterMode mode);` | 设过滤模式并重建 | `setFilterMode` qcompleter.h:67（Qt::MatchFlags） | — |
| `XCompleterFilterMode XCompleter_filterMode(const XCompleter* self);` | 查过滤模式 | `filterMode` qcompleter.h:68 | 值 |
| `void XCompleter_setModelSorting(XCompleter* self, XCompleterModelSorting sorting);` | 设排序假设并重建（有序+StartsWith 走二分） | `setModelSorting` qcompleter.h:76 | — |
| `XCompleterModelSorting XCompleter_modelSorting(const XCompleter* self);` | 查排序假设 | `modelSorting` qcompleter.h:77 | 值 |
| `void XCompleter_setWrapAround(XCompleter* self, bool wrap);` | 设环绕开关（环绕由消费方导航层实现） | `setWrapAround` qcompleter.h:104 | — |
| `bool XCompleter_wrapAround(const XCompleter* self);` | 查环绕；NULL 返 true | `wrapAround` qcompleter.h:85 | 值 |
| `void XCompleter_setMaxVisibleItems(XCompleter* self, int maxItems);` | 设最大可见条目（负值归 0） | `setMaxVisibleItems` qcompleter.h:88 | — |
| `int XCompleter_maxVisibleItems(const XCompleter* self);` | 查最大可见条目；NULL 返 7 | `maxVisibleItems` qcompleter.h:87 | 值 |
| `void XCompleter_setWidget(XCompleter* self, XWidget* widget);` | 设关联编辑控件 | `setWidget` qcompleter.h:58 | 借用，不拥有 |
| `XWidget* XCompleter_widget(const XCompleter* self);` | 查关联控件 | `widget` qcompleter.h:59 | 借用 |
| `void XCompleter_setPopup(XCompleter* self, XWidget* popup);` | 记录外接弹层（不接管、不绘制） | `setPopup` qcompleter.h:71 | 借用，存活期须有效 |
| `void XCompleter_complete(XCompleter* self);` | 重建完成列表；Popup/Unfiltered 模式同步内建弹层 | `complete` qcompleter.h:103 | — |
| `void XCompleter_hidePopup(XCompleter* self);` | 隐藏外接/内建弹层并解抓取、停巡检（幂等） | X 扩展（Qt 经 popup()->hide()） | — |
| `XString* XCompleter_currentCompletion(const XCompleter* self);` | 当前补全文本拷贝 | `currentCompletion` qcompleter.h:95 | **新建，须 `XClassDelete`** |
| `int XCompleter_currentRow(const XCompleter* self);` | 完成列表行（0 基；无命中 -1） | `currentRow` qcompleter.h:92 | 值 |
| `int XCompleter_currentIndex(const XCompleter* self);` | 当前候选的**源模型行号**（列恒 completionColumn） | `currentIndex` qcompleter.h:94（签名差异：返 int 非 QModelIndex） | 值 |
| `int XCompleter_completionCount(const XCompleter* self);` | 候选数（重建后常数时间） | `completionCount` qcompleter.h:90 | 值 |
| `bool XCompleter_setCurrentRow(XCompleter* self, int row);` | 设完成列表行；越界返回 false 且不改状态 | `setCurrentRow` qcompleter.h:91 | — |
| `XAbstractItemModel* XCompleter_completionModel(const XCompleter* self);` | 返回**底层模型借用**（无代理模型） | `completionModel` qcompleter.h:97（Qt 返代理模型） | 借用 |
| `XString* XCompleter_pathFromIndex(const XCompleter* self, int row);` | 取源模型第 row 行补全文本拷贝 | `pathFromIndex` qcompleter.h:107（签名改为 int row） | **新建，须 `XClassDelete`** |
| `void XCompleter_splitPath(const XCompleter* self, const XString* path, XString** dirOut, XString** fileOut);` | 按 '/' 拆目录/文件名（双输出） | `splitPath` qcompleter.h:108（Qt 返 QStringList） | **两个输出均新建，调用方释放** |
| `void XCompleter_splitPath_2(const XCompleter* self, const char* path, XString** dirOut, XString** fileOut);` | UTF-8 重载 | 同上 | 同上 |
| `XWidget* XCompleter_popup(const XCompleter* self);` | 查弹层（外接优先，否则内建默认弹层） | `popup` qcompleter.h:70 | 借用 |
| `void* XCompleter_activated_signal(XCompleter* self, const XString* text);` | 候选采纳信号（text） | `activated(const QString&)` qcompleter.h:115 | 信号标识地址 |
| `void* XCompleter_textActivated_signal(XCompleter* self, const XString* text);` | 与 activated 等价的旧命名 | `activated` qcompleter.h:115 | 信号标识地址 |
| `void* XCompleter_highlighted_signal(XCompleter* self, const XString* text);` | 高亮变化信号 | `highlighted(const QString&)` qcompleter.h:117 | 信号标识地址 |
| `void* XCompleter_textHighlighted_signal(XCompleter* self, const XString* text);` | 与 highlighted 等价的旧命名 | qcompleter.h:117 | 信号标识地址 |
| `void* XCompleter_highlightedRow_signal(XCompleter* self, int row);` | 高亮行信号（行简化） | `highlighted(const QModelIndex&)` qcompleter.h:118 | 信号标识地址 |
| `void* XCompleter_activatedIndex_signal(XCompleter* self, int row, int col);` | 激活索引信号（row,col 简化） | `activated(const QModelIndex&)` qcompleter.h:116 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_2` 兼两义——UTF-8 重载（`setCompletionPrefix_2`/`splitPath_2`）与"带模型构造"（`create_2`，XCompleter.h:185-189）；无三态 `set*`，全部为二态/枚举写入。返回 `XString*` 的四个查询（`completionPrefix`/`currentCompletion`/`pathFromIndex`/`splitPath` 双输出）一律新建拷贝，须 `XClassDelete`；`model`/`widget`/`popup`/`completionModel` 一律借用。

**可覆写点**：`XCLASS_DEFINE_BEGING(XCompleter)`+`XCLASS_DEFINE_EXTEND_END(XCompleter, XObject)`（XCompleter.h:117-118），不新增槽。重载（XCompleter.c:307-314）：`EXClass_Deinit`→停巡检+释放 prefix/currentCompletion/matches；`EXObject_TimerEvent`→弹层守护巡检。`_base` 入口：`XObject_event_base`（经 `XObject_Protected.h`）。

**注意事项与坑点**

1. **`XCompleter_popup` 与头注释矛盾**：头注释称"返回 setPopup() 保存的借用指针（默认 NULL）…未调用 setPopup 时恒为 NULL，与既有回归断言一致"（XCompleter.h:533-542），实现却是"外接优先，否则返回内建默认弹层"（XCompleter.c:980-985）——键入产生候选后 `popup()` 非 NULL，`Test/XGuiDemo/xgui_demo_apitest_input.c:887` 正是断言"内建默认弹层自动创建"。
2. **`completionPrefix` 的"空前缀返回 NULL"只对 NULL 成立**：实现仅判断 `m_prefix == NULL`（XCompleter.c:406-410），而 `setCompletionPrefix_2(self,"")` 会建一个空 XString（XCompleter.c:396-404），此时返回的是**空串拷贝对象**而非 NULL；头注释"self 为 NULL 或空前缀返回 NULL"（XCompleter.h:247-249）在空串场景不成立。
3. **`completionModel` 不是代理模型**：Qt 返回过滤后的代理模型，本库直接返回 `setModel` 的底层模型（XCompleter.c:920-924，头注 XCompleter.h:473-482）；要看过滤结果必须用 `completionCount`/`currentCompletion`/`currentIndex`。
4. **`splitPath` 签名与语义双差**：Qt 返回逐级匹配段列表且文件系统模型按本机分隔符拆分，本库改为 (目录,文件名) 双输出且只识别 '/'（XCompleter.c:937-965，头注 XCompleter.h:500-515）；`self` 参数纯为保留成员函数形态而接收但未使用（XCompleter.c:946）。
5. **`setCurrentRow` 不环绕**：越界一律 false，环绕只在弹层按键导航层按 `wrapAround` 自行回绕（XCompleter.c:902-918 注，对齐 qcompleter.cpp:256-269）；`setWrapAround` 本身不改变该行为。
6. **模型/控件/弹层全是借用**：组合框/`XLineEdit` 析构不会通知补全器；内建默认弹层挂在编辑框顶层窗口、随顶层析构并自摘指针（XCompleter.c:804-833），但 `m_widget`/`m_model` 悬垂无防护——调用方须先 `setWidget(NULL)`/`setModel(NULL)`（`m_popupGuardTimer` 巡检只处理"编辑框隐藏/失焦"收层，XCompleter.h:138）。
7. **枚举非 Qt 逐值一致的只有 FilterMode**：头注释已声明 Qt 6.8.3 无 FilterMode 枚举（qcompleter.h:28 用的是 `Qt::MatchFlags`），本库三值 0/1/2 为自定义顺序（XCompleter.h:76-88）；CompletionMode/ModelSorting 与 qcompleter.h:37-48 逐项一致。
8. **`completionRole` 是死存储**：`setCompletionRole` 只写字段（XCompleter.c:424-428），`pathFromIndex` 走 `XAbstractItemModel_data(model,row,column)` 无 role 参数（XCompleter.c:926-935）。

**测试覆盖**：无 `Test/XGuiTest/XCompleterTest.c`。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_input.c:855-915`（create/model/widget/completionCount/内建弹层创建与条目数/currentRow/setCompletionPrefix_2/hidePopup）；`Test/XGuiDemo/xgui_demo_apitest_text.c:1631-1652`（默认值/信号连接/popup NULL）；`rg -c "XCompleter_" xgui_regression_test.c`=15 处；`Test/XGuiDemo/*.c` 合计 117 处。XGui.md §8.0g20「XCompleter 默认弹层落地」记录 apitest +6 断言（XGui.md:907、:1794）。

---

## XDateEdit

**定位**：日期编辑控件——QDateEdit 式的 XDateTimeEdit 纯别名子类（只定构造口径 + userDateChanged 信号，其余全部继承）。继承链 `XObject ← XWidget ← XAbstractSpinBox ← XDateTimeEdit ← XDateEdit`。头文件 `Src/XGui/Widget/XDateEdit.h`（110 行）。开关 `XDATEEDIT_ON`，另需 `XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON`（XDateEdit.h:39）。

**生命周期**：`XDateEdit_init` 先 `XDateTimeEdit_init`，再置 parserType=Date、初值 2000-01-01 00:00:00、范围 1752-09-14..9999-12-31、displayFormat="yyyy/M/d"，最后连 `dateChanged`→`userDateChanged` 转发槽（XDateEdit.c:62-107）；类不重载 deinit（无自有字段，XDateEdit.h:45-47）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDateEdit_class_init(void);` | 建表，仅继承 XDateTimeEdit 虚表（不重载任何槽） | X 扩展 | 共享表，禁释放 |
| `void XDateEdit_init(XDateEdit* self, XWidget* parent, XWidgetFlags flags);` | 构造口径：parserType=Date + 初值/范围/格式 + 信号转发 | `QDateEdit(QWidget*)` qdatetimeedit.h:199（构造语义 qdatetimeedit.cpp:1703-1707） | — |
| `#define XDateEdit_create(parent, flags) XDateEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,(parent),(flags))` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XDateEdit* XDateEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `void* XDateEdit_userDateChanged_signal(XDateEdit* self, const XDate* date);` | userDateChanged 信号（真发射：构造期连接 dateChanged 转发，程序性 setDate 也触发） | `userDateChanged(QDate)` qdatetimeedit.h:204（Q_PROPERTY NOTIFY :197） | date 借用（发射期有效） |

其余全部公共 API 继承自 XDateTimeEdit（日期值/范围/分段/格式/日历弹层），本类不新增字段也不新增函数（XDateEdit.h:16-20、:47-51）。

**所有权与命名契约**：遵循 XDateTimeEdit/XAbstractSpinBox 基类契约；本类唯一自有符号 `XDateEdit_userDateChanged_signal` 无 `_2`/`_const` 变体；`date` 载荷为借用指针，"发射期间有效"（XDateEdit.c:40-42 注）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDateEdit)`+`XCLASS_DEFINE_EXTEND_END(XDateEdit, XDateTimeEdit)`（XDateEdit.h:42-43），**零新增/零重载**——`XDateEdit_class_init` 只有 `XVTABLE_INHERIT_XCLASS(XDateTimeEdit)` 一行（XDateEdit.c:27-34），分段步进/键入提交/绘制/日历弹层全走 XDateTimeEdit 实现。无 `_base` 入口；如需基类行为直接用 `XDateTimeEdit_*`（如 apitest 即以 `&de.m_base` 调用，`Test/XGuiDemo/xgui_demo_apitest_input.c:1787-1815`）。

**注意事项与坑点**

1. **两行悬空 doc 注释**：XDateEdit.h:87-88 "析构调度入口宏（复用基类 XDateTimeEdit 的 deinit 链）/删除堆对象入口宏（XClass_delete_base 转发）"之后**没有对应宏**（`grep "XDateEdit_delete\|XDateEdit_deinit"` 零命中），删除只能走 `XClassDelete`（XClass.h:229-230）。
2. **范围与基类默认值不同**：XDateEdit 构造把范围收到 1752-09-14..9999-12-31（XDateEdit.c:88-97，对标 `QT_COMPAT_DATE_MIN`/`QT_DATE_MAX`），而 XDateTimeEdit 自身 init 的默认是 1900-01-01..2999-12-31（XDateTimeEdit.c:3468-3473），`clearMinimumDate` 也复位到 1900（XDateTimeEdit.c:3758-3764，头注 XDateTimeEdit.h:326-329 已声明"与 Qt 的 1752-09-14 下界不同"）——在 XDateEdit 上清范围会得到与构造不一致的下界。
3. **parserType 一旦置 Date 就滤掉时间记号**：`setDisplayFormat` 含时间域时按 parserType 拒绝/退化为字面（XDateEdit.h:8-12；基类守卫 XDateTimeEdit.c:3830-3842）；纯时间格式整体拒绝、原格式保持。
4. **`userDateChanged` 属本类而非基类**：与 Qt 一致（QDateTimeEdit 基类无 userDateChanged，qdatetimeedit.h:146-149 只有三信号），基类只发 dateChanged/timeChanged/dateTimeChanged（XDateTimeEdit.h:484-490 注）；把 `XDateEdit` 当 `XDateTimeEdit*` 用会收不到 user 变体。
5. **构造期直赋不发信号**：初值/范围/格式在 init 内直写字段后再 `setDisplayFormat`（XDateEdit.c:77-99），`Test/XGuiDemo/xgui_demo_apitest_input.c:1787-1800` 断言构造后默认值而信号计数为 0（转发连接在最后一行建立，XDateEdit.c:103-106）。

**测试覆盖**：无 `Test/XGuiTest/XDateEditTest.c`。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_input.c:1779-1815` 起（构造默认格式/分段掩码=年|月|日/sectionCount=3/初值 2000-01-01/时间部分清零/范围 1752-09-14，及 userDateChanged 全变更转发）；`rg "XDateEdit" xgui_regression_test.c`=**0**（该套件未覆盖本类）；`Test/XGuiDemo/*.c` 合计 3 处。

---

## XDateTimeEdit

**定位**：日期时间编辑控件（范围/分段编辑/步进/格式展开/日历弹层）。继承链 `XObject ← XWidget ← XAbstractSpinBox ← XDateTimeEdit`。头文件 `Src/XGui/Widget/XDateTimeEdit.h`（496 行）。开关 `XDATETIMEEDIT_ON`，另需 `XWIDGET_ON && XABSTRACTSPINBOX_ON`（XDateTimeEdit.h:53）。

**生命周期**：`XDateTimeEdit_init` 建默认格式 "yyyy-MM-dd HH:mm:ss"、当前值=墙钟 `XDateTime_currentDateTime()`、范围 1900-01-01..2999-12-31 23:59:59.999、currentSection=YearSection，并把内嵌行编辑置鼠标穿透+NoFocus、反向注册焦点代理、挂输入法属性（XDateTimeEdit.c:3450-3545 起）；析构重载 `EXClass_Deinit` 释放格式串/日历/弹层/时间行编辑（XDateTimeEdit.c:3411）。

**API 面**

枚举：`XDateTimeEditSection`（NoSection=0/AmPm=1/MSec=2/Second=4/Minute=8/Hour=0x10/Day=0x100/Month=0x200/Year=0x400，与 qdatetimeedit.h:47-59 逐值一致）、`XDateTimeEditParserType`（DateTime=0/Date=1/Time=2，X 扩展，对标 QDateTimeParser::parserType）（XDateTimeEdit.h:59-84）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDateTimeEdit_class_init(void);` | 建表（继承 XAbstractSpinBox） | X 扩展 | 共享表，禁释放 |
| `void XDateTimeEdit_init(XDateTimeEdit* self, XWidget* parent, XWidgetFlags flags);` | 初始化（当前时间/默认范围/默认格式） | `QDateTimeEdit(QWidget*)` qdatetimeedit.h:65（实现 qdatetimeedit.cpp:140-144） | — |
| `#define XDateTimeEdit_create(parent, flags) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XDateTimeEdit* XDateTimeEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `void XDateTimeEdit_setCalendarPopup(XDateTimeEdit* self, bool popup);` | 开关日历下拉（默认 false） | `setCalendarPopup` qdatetimeedit.h:129 | — |
| `bool XDateTimeEdit_calendarPopup(const XDateTimeEdit* self);` | 查日历下拉 | `calendarPopup` qdatetimeedit.h:128 | 值 |
| `XCalendarWidget* XDateTimeEdit_calendarWidget(const XDateTimeEdit* self);` | 懒建/取内置日历（const 接口含副作用） | `calendarWidget` qdatetimeedit.h:116 | 借用（属本控件），禁释放 |
| `void XDateTimeEdit_setCalendarWidget(XDateTimeEdit* self, XCalendarWidget* calendar);` | 接管外部日历（NULL 仅释放清空；同指针幂等） | `setCalendarWidget` qdatetimeedit.h:117 | **所有权转移给本控件** |
| `void XDateTimeEdit_setTimeSpec(XDateTimeEdit* self, int spec);` | 设时区规格（原样存取，不参与换算） | `setTimeSpec` qdatetimeedit.h:135（Qt 6.10 起废弃） | — |
| `int XDateTimeEdit_timeSpec(const XDateTimeEdit* self);` | 查时区规格；NULL 返 0 | `timeSpec` qdatetimeedit.h:133 | 值 |
| `void XDateTimeEdit_setCurrentSectionIndex(XDateTimeEdit* self, int index);` | 按格式内 0 基序号定位分段并整段选中 | `setCurrentSectionIndex` qdatetimeedit.h:114 | — |
| `int XDateTimeEdit_currentSectionIndex(const XDateTimeEdit* self);` | 当前分段的 0 基序号（与枚举码分离） | `currentSectionIndex` qdatetimeedit.h:113 | 值 |
| `void XDateTimeEdit_setDateTime(XDateTimeEdit* self, const XDateTime* dateTime);` | 设日期时间（钳位后按差异发信号） | `setDateTime` qdatetimeedit.h:152 | dateTime 借用 |
| `const XDateTime* XDateTimeEdit_dateTime(const XDateTimeEdit* self);` | 查当前值（内部指针） | `dateTime` qdatetimeedit.h:71 | **借用内部存储，禁释放** |
| `void XDateTimeEdit_setDate(XDateTimeEdit* self, const XDate* date);` | 设日期部分 | `setDate` qdatetimeedit.h:153 | date 借用 |
| `XDate XDateTimeEdit_date(const XDateTimeEdit* self);` | 取日期部分（按值） | `date` qdatetimeedit.h:72 | 值 |
| `void XDateTimeEdit_setTime(XDateTimeEdit* self, const XTime* time);` | 设时间部分 | `setTime` qdatetimeedit.h:154 | time 借用 |
| `XTime XDateTimeEdit_time(const XDateTimeEdit* self);` | 取时间部分（按值） | `time` qdatetimeedit.h:73 | 值 |
| `const XDateTime* XDateTimeEdit_minimumDateTime(const XDateTimeEdit* self);` | 查最小日期时间 | `minimumDateTime` qdatetimeedit.h:78 | 借用内部存储 |
| `void XDateTimeEdit_setMinimumDateTime(XDateTimeEdit* self, const XDateTime* dateTime);` | 设最小值并钳位当前值 | `setMinimumDateTime` qdatetimeedit.h:80 | dateTime 借用 |
| `const XDateTime* XDateTimeEdit_maximumDateTime(const XDateTimeEdit* self);` | 查最大日期时间 | `maximumDateTime` qdatetimeedit.h:82 | 借用内部存储 |
| `void XDateTimeEdit_setMaximumDateTime(XDateTimeEdit* self, const XDateTime* dateTime);` | 设最大值并钳位当前值 | `setMaximumDateTime` qdatetimeedit.h:84 | dateTime 借用 |
| `XDate XDateTimeEdit_minimumDate(const XDateTimeEdit* self);` | 取最小值的日期部分；NULL 返全零 | `minimumDate` qdatetimeedit.h:88 | 值 |
| `void XDateTimeEdit_setMinimumDate(XDateTimeEdit* self, const XDate* date);` | 保留最小时间、替换日期后钳位 | `setMinimumDate` qdatetimeedit.h:89 | date 借用 |
| `XDate XDateTimeEdit_maximumDate(const XDateTimeEdit* self);` | 取最大值的日期部分 | `maximumDate` qdatetimeedit.h:92 | 值 |
| `void XDateTimeEdit_setMaximumDate(XDateTimeEdit* self, const XDate* date);` | 保留最大时间、替换日期后钳位 | `setMaximumDate` qdatetimeedit.h:93 | date 借用 |
| `XTime XDateTimeEdit_minimumTime(const XDateTimeEdit* self);` | 取最小值的时间部分 | `minimumTime` qdatetimeedit.h:98 | 值 |
| `void XDateTimeEdit_setMinimumTime(XDateTimeEdit* self, const XTime* time);` | 保留最小日期、替换时间后钳位 | `setMinimumTime` qdatetimeedit.h:99 | time 借用 |
| `XTime XDateTimeEdit_maximumTime(const XDateTimeEdit* self);` | 取最大值的时间部分 | `maximumTime` qdatetimeedit.h:102 | 值 |
| `void XDateTimeEdit_setMaximumTime(XDateTimeEdit* self, const XTime* time);` | 保留最大日期、替换时间后钳位 | `setMaximumTime` qdatetimeedit.h:103 | time 借用 |
| `void XDateTimeEdit_clearMinimumDate(XDateTimeEdit* self);` | 复位最小日期为 1900-01-01 | `clearMinimumDate` qdatetimeedit.h:90 | — |
| `void XDateTimeEdit_clearMaximumDate(XDateTimeEdit* self);` | 复位最大日期为 2999-12-31 | `clearMaximumDate` qdatetimeedit.h:94 | — |
| `void XDateTimeEdit_clearMinimumTime(XDateTimeEdit* self);` | 复位最小时间为 00:00:00.000 | `clearMinimumTime` qdatetimeedit.h:100 | — |
| `void XDateTimeEdit_clearMaximumTime(XDateTimeEdit* self);` | 复位最大时间为 23:59:59.999 | `clearMaximumTime` qdatetimeedit.h:104 | — |
| `void XDateTimeEdit_clearMinimumDateTime(XDateTimeEdit* self);` | 复位最小值为 1900-01-01 00:00:00.000 并钳位 | `clearMinimumDateTime` qdatetimeedit.h:79 | — |
| `void XDateTimeEdit_clearMaximumDateTime(XDateTimeEdit* self);` | 复位最大值为 2999-12-31 23:59:59.999 并钳位 | `clearMaximumDateTime` qdatetimeedit.h:83 | — |
| `void XDateTimeEdit_setDateRange(XDateTimeEdit* self, const XDate* min, const XDate* max);` | 等价先后调 setMinimumDate/setMaximumDate | `setDateRange` qdatetimeedit.h:96 | min/max 借用 |
| `void XDateTimeEdit_setTimeRange(XDateTimeEdit* self, const XTime* min, const XTime* max);` | 等价先后调 setMinimumTime/setMaximumTime | `setTimeRange` qdatetimeedit.h:106 | min/max 借用 |
| `void XDateTimeEdit_setDateTimeRange(XDateTimeEdit* self, const XDateTime* min, const XDateTime* max);` | 等价先后调 min/max setter | `setDateTimeRange` qdatetimeedit.h:86 | min/max 借用 |
| `void XDateTimeEdit_setDisplayFormat(XDateTimeEdit* self, const char* utf8);` | 设显示格式（无可识别分段则拒绝、原格式保持） | `setDisplayFormat` qdatetimeedit.h:126 | UTF-8 借用 |
| `const char* XDateTimeEdit_displayFormat(const XDateTimeEdit* self);` | 查显示格式 | `displayFormat` qdatetimeedit.h:125 | 借用，禁释放 |
| `int XDateTimeEdit_currentSection(const XDateTimeEdit* self);` | 查当前分段枚举码 | `currentSection` qdatetimeedit.h:109 | 值 |
| `void XDateTimeEdit_setCurrentSection(XDateTimeEdit* self, int section);` | 设当前分段（NoSection 或不在格式中不动作） | `setCurrentSection` qdatetimeedit.h:111 | — |
| `int XDateTimeEdit_sections(const XDateTimeEdit* self);` | 显示格式的分段掩码 | `displayedSections` qdatetimeedit.h:108（Sections 位掩码） | 值 |
| `#define XDateTimeEdit_displayedSections(self) XDateTimeEdit_sections((self))` | displayedSections 别名 | `displayedSections` qdatetimeedit.h:108 | 值 |
| `int XDateTimeEdit_sectionCount(const XDateTimeEdit* self);` | 格式中分段记号个数（0~16） | `sectionCount` qdatetimeedit.h:119 | 值 |
| `int XDateTimeEdit_sectionAt(const XDateTimeEdit* self, int index);` | 第 index 个分段枚举；越界 NoSection | `sectionAt` qdatetimeedit.h:110 | 值 |
| `XString* XDateTimeEdit_sectionText(const XDateTimeEdit* self, int section);` | 分段显示文本（星期/上下午有中文文案） | `sectionText` qdatetimeedit.h:123 | **新建，须 `XClassDelete`** |
| `void XDateTimeEdit_setSelectedSection(XDateTimeEdit* self, int section);` | 选中分段（NoSection=反选全部；不在格式中不动作） | `setSelectedSection` qdatetimeedit.h:121 | — |
| `void* XDateTimeEdit_dateTimeChanged_signal(XDateTimeEdit* self, const XDateTime* dateTime);` | dateTimeChanged 信号（真发射） | `dateTimeChanged` qdatetimeedit.h:147 | dateTime 借用（发射期） |
| `void* XDateTimeEdit_dateChanged_signal(XDateTimeEdit* self, const XDate* date);` | dateChanged 信号（真发射，日期部分变化才发） | `dateChanged` qdatetimeedit.h:149 | date 借用 |
| `void* XDateTimeEdit_timeChanged_signal(XDateTimeEdit* self, const XTime* time);` | timeChanged 信号（真发射，时间部分变化才发） | `timeChanged` qdatetimeedit.h:148 | time 借用 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；无 `_2` 系列（格式串直接收 `const char* utf8`，全类仅此一处 UTF-8 接口，XDateTimeEdit.h:401-402）；`const XDateTime*` 返回内部存储借用（`dateTime`/`minimumDateTime`/`maximumDateTime` 三个），`XDate`/`XTime` 返回按值；三态 `set*`：无三态取值，`setCalendarPopup`/`setCalendarWidget` 为二态+NULL；`setTimeSpec` 原样存 int。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDateTimeEdit)`+`XCLASS_DEFINE_EXTEND_END(XDateTimeEdit, XAbstractSpinBox)`（XDateTimeEdit.h:86-87），不新增槽。重载（XDateTimeEdit.c:3378-3413）：`EXWidget_SizeHint`/`MinimumSizeHint`→按格式串量宽；`EXAbstractSpinBox_StepBy`→按当前分段增减（含进位、上下午 ±12h，只读拒绝，XDateTimeEdit.c:1018-1035 起）；`EXAbstractSpinBox_StepEnabled`→边界判定；`EXAbstractSpinBox_Interpret`→键入文本分节解析提交（非法回退旧值）；`EXAbstractSpinBox_Clear`→清当前分段（非全文本）；`EXObject_EventFilter`→时间行编辑器焦点弹数字键盘；`EXWidget_KeyPressEvent`→分段导航/键入/Home-End 段内语义；`EXWidget_MousePressEvent`→点选段+整段选中+焦点回收；`EXWidget_FocusInEvent`→进窗选首段；`EXWidget_PaintEvent`/`ResizeEvent`/`HideEvent`→日历弹层绘制/几何/收层；`EXObject_TimerEvent`→弹层延迟抓取；`EXClass_Deinit`。`_base` 入口：`XAbstractSpinBox_stepBy_base`（XAbstractSpinBox.h:256）、`_stepEnabled_base`（:293）、`_interpret_base`（:302）、`_clear_base`（:285）。

**注意事项与坑点**

1. **`stepBy` 只存在于虚槽，无公开函数**：头注释把"stepBy：按当前分段增减"列为功能（XDateTimeEdit.h:22-25），但头文件不声明 `XDateTimeEdit_stepBy`（`rg` 全仓零命中），实际入口是 `EXAbstractSpinBox_StepBy`→`VXDateTimeEdit_stepBy`（XDateTimeEdit.c:3384-3385）；调用方要用公共 API 只能走继承的 `XAbstractSpinBox_stepUp/stepDown`（XAbstractSpinBox.h:239/242 → XAbstractSpinBox.c:876-884，内部按 `stepBy_base` 虚派发）。
2. **基类默认范围与 Qt 不同**：init 落 1900-01-01..2999-12-31（XDateTimeEdit.c:3468-3473）、`clearMinimumDate` 复位 1900-01-01（XDateTimeEdit.c:3758-3764），Qt 的默认下界是 `QDATETIMEEDIT_COMPAT_DATE_MIN`（1752-09-14，qdatetimeedit.cpp:368/493）——头文件 XDateTimeEdit.h:326-329 已声明"与 Qt 的 1752-09-14 下界不同，属 XGui 裁剪默认值"。
3. **`dateTime()` 默认值是墙钟**：init 用 `XDateTime_currentDateTime()` 而非单调时钟（XDateTimeEdit.c:3462-3466 注），原因是 `XDateTime_currentMSecsSinceEpoch` 已改为开机时长语义；任何"默认值应为 1970"的假设都会错。
4. **信号只在"部分真变"时发**：三条信号统一走 `xdt_emitPartChanged`（XDateTimeEdit.c:499-509；`xdt_emitChanged` 见 :439-452）：先发 dateTimeChanged，再按 `XDate_compare`/`XTime_compare` 差异分别补 dateChanged/timeChanged；因此"设同值"不发、只改时间不发 dateChanged。用户在 `XDateEdit`/`XTimeEdit` 上还要的是 user 变体，基类不声明（XDateTimeEdit.h:484-490）。
5. **parserType 是隐藏的第三态**：`m_parserType` 字段在基类（XDateTimeEdit.h:145-149），XDateEdit/XTimeEdit 构造写 Date/Time（XDateEdit.c:77），但并**无公开 setter**——`setDisplayFormat` 能接受哪些分段完全取决于构造者。用基类实例想模拟"纯日期"只能靠子类。
6. **`setTimeSpec` 是死存储**：只写 `m_timeSpec`（XDateTimeEdit.c:4111-4112），不参与任何换算；头注称"setter 原样存取的契约字段"（XDateTimeEdit.h:123-124）。
7. **日历接口的所有权不对称**：`calendarWidget()` 懒建并**返回借用**（对象归本控件，XDateTimeEdit.h:182-184），而 `setCalendarWidget` **取得所有权**且旧日历直接 `XClassDelete`（XDateTimeEdit.c:4066-4075）；同指针重复传入为幂等（`calendar == self->m_calendar` 早退，XDateTimeEdit.c:4069），传 NULL 等于释放当前日历。
8. **格式串守卫会静默吞掉非法格式**：`setDisplayFormat` 在格式里找不到任何本 parserType 可识别分段时直接 return，原格式不变（XDateTimeEdit.c:3830-3842，对标 qdatetimeedit.cpp:929）；调用方无法从返回值判断是否生效，只能再读 `displayFormat()` 比对。
9. **`sectionText` 同码多档取先出现者**（如 "dd ddd"）：头注 XDateTimeEdit.h:444-451；`sectionCount`/`sections`/`currentSectionIndex` 共用 `XDT_SECTION_MAX`（=16，XDateTimeEdit.c:46）局部槽数组，超长格式的分段在计数与掩码中被截断。

**测试覆盖**：无 `Test/XGuiTest/XDateTimeEditTest.c`。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_input.c:1457-1600+`（默认格式/currentSection/calendarPopup/timeSpec/最小最大日期时间/sections 掩码/sectionCount=6/sectionAt 逐项/越界/setDateTime 钳位/setDate+setTime/范围 setter 往返，及三信号 connect）；`rg -c "XDateTimeEdit_" xgui_regression_test.c`=90 处；`Test/XGuiDemo/*.c` 合计 161 处。XGui.md §8.0g「XDateTimeEdit 四连」（XGui.md:425-433）记录 true-emit 信号、Interpret 提交、Home/End 段内语义、calendarPopup 默认 false 四项修复，探针 21/21。

---

## XDial

**定位**：圆形旋钮（凹槽圆弧 + 指示针 + notches 刻度 + 角度拖动调值）。继承链 `XObject ← XWidget ← XAbstractSlider ← XDial`。头文件 `Src/XGui/Widget/XDial.h`（123 行）。开关 `XDIAL_ON`，另需 `XWIDGET_ON && XABSTRACTSLIDER_ON`（XDial.h:28）。

**生命周期**：`XDial_init` 先 `XAbstractSlider_init`（范围 0..99、值 0、singleStep 1、pageStep 10 由基类给），再写 notchTarget=3.7、notchesVisible=false、wrapping=false、dragging=false，开 Hover 属性，最后 `xdial_updateSizeHint`（100×100 / 最小 50×50，XDial.c:532-543、:516-530）；堆对象 `_create/_create_ex` 后 `XClassDelete`。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDial_class_init(void);` | 建表（继承 XAbstractSlider） | X 扩展 | 共享表，禁释放 |
| `void XDial_init(XDial* self, XWidget* parent, XWidgetFlags flags);` | 初始化默认值 + sizeHint | `QDial(QWidget*)` qdial.h:28 | — |
| `#define XDial_create(parent, flags) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XDial* XDial_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `double XDial_notchTarget(const XDial* self);` | 查刻度间距目标像素（默认 3.7） | `notchTarget` qdial.h:37 | 值 |
| `void XDial_setNotchTarget(XDial* self, double target);` | 设刻度间距并重绘（不校验 0/负值） | `setNotchTarget` qdial.h:36 | — |
| `bool XDial_notchesVisible(const XDial* self);` | 查刻度可见 | `notchesVisible` qdial.h:38 | 值 |
| `void XDial_setNotchesVisible(XDial* self, bool visible);` | 设刻度可见并重绘 | `setNotchesVisible` qdial.h:44 | — |
| `bool XDial_wrapping(const XDial* self);` | 查环绕（本类自有位） | `wrapping` qdial.h:32 | 值 |
| `void XDial_setWrapping(XDial* self, bool on);` | 设环绕（经 bound 虚槽全链路生效） | `setWrapping` qdial.h:45 | — |
| `int XDial_notchSize(const XDial* self);` | 刻度间隔（**范围单位**，singleStep 的非零倍数） | `notchSize` qdial.h:34（实现 qdial.cpp:346-363） | 值 |
| `#define XDial_minimum(self)` | 查最小值 | `minimum` qabstractslider.h:40 | — |
| `#define XDial_maximum(self)` | 查最大值 | `maximum` qabstractslider.h:43 | — |
| `#define XDial_setMinimum(self, min)` | 设最小值 | `setMinimum` qabstractslider.h:39 | — |
| `#define XDial_setMaximum(self, max)` | 设最大值 | `setMaximum` qabstractslider.h:42 | — |
| `#define XDial_setRange(self, min, max)` | 设范围 | `setRange` qabstractslider.h:84 | — |
| `#define XDial_value(self)` | 查当前值 | `value` qabstractslider.h:77 | — |
| `#define XDial_setValue(self, v)` | 设当前值（经 bound 收敛） | `setValue` qabstractslider.h:82 | — |
| `#define XDial_singleStep(self)` / `#define XDial_setSingleStep(self, s)` | 单步步进读写 | `singleStep`/`setSingleStep` qabstractslider.h:46/45 | — |
| `#define XDial_pageStep(self)` / `#define XDial_setPageStep(self, s)` | 翻页步进读写 | `pageStep`/`setPageStep` qabstractslider.h:49/48 | — |
| `#define XDial_hasTracking(self)` / `#define XDial_setTracking(self, e)` | 拖动实时发信号开关 | `hasTracking`/`setTracking` qabstractslider.h:52/51 | — |
| `#define XDial_invertedAppearance(self)` / `#define XDial_setInvertedAppearance(self, i)` | 外观翻转读写 | `invertedAppearance`/`setInvertedAppearance` qabstractslider.h:61/60 | — |
| `#define XDial_invertedControls(self)` / `#define XDial_setInvertedControls(self, i)` | 控制翻转读写 | `invertedControls`/`setInvertedControls` qabstractslider.h:64/63 | — |
| `#define XDial_triggerAction(self, a)` | 触发滑块动作 | `triggerAction` qabstractslider.h:79 | — |
| `#define XDial_valueChanged_signal(self, value)` | valueChanged(int) 信号标识 | `valueChanged` qabstractslider.h:87 | 信号标识地址 |
| `#define XDial_sliderPressed_signal(self)` | sliderPressed() 信号标识 | `sliderPressed` qabstractslider.h:89 | 信号标识地址 |
| `#define XDial_sliderMoved_signal(self, position)` | sliderMoved(int) 信号标识 | `sliderMoved` qabstractslider.h:90 | 信号标识地址 |
| `#define XDial_sliderReleased_signal(self)` | sliderReleased() 信号标识 | `sliderReleased` qabstractslider.h:91 | 信号标识地址 |
| `#define XDial_rangeChanged_signal(self, min, max)` | rangeChanged(int,int) 信号标识 | `rangeChanged` qabstractslider.h:93 | 信号标识地址 |
| `#define XDial_actionTriggered_signal(self, action)` | actionTriggered(int) 信号标识 | `actionTriggered` qabstractslider.h:95 | 信号标识地址 |

**所有权与命名契约**：遵循 XAbstractSlider/XWidget 基类契约；本类无 `_2`/`_const` 变体，无返回新建对象的 API；三态 `set*`：`setNotchesVisible`/`setWrapping` 为二态 bool，`setNotchTarget` 为 double（无范围校验）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDial)`+`XCLASS_DEFINE_EXTEND_END(XDial, XAbstractSlider)`（XDial.h:31-32），不新增槽。重载（XDial.c:496-513）：`EXWidget_SizeHint`/`MinimumSizeHint`→恒 100×100 / 50×50（XDial.c:516-530，对标 qdial.cpp:416-428）；`EXWidget_PaintEvent`→刻度/表盘/指示针；`EXWidget_MousePress/MouseMove/MouseRelease`→角度拖动；`EXWidget_ChangeEvent`；`EXAbstractSlider_Bound`→wrapping 时按 `minimum + (val-minimum) % (maximum-minimum)` 回绕，否则纯钳位（XDial.c:676-688）；`EXClass_Copy/Move`。`_base` 入口：`XAbstractSlider_bound_base`（XAbstractSlider.h:339，默认纯钳位）、`XAbstractSlider_stepBy_base`（:310）、`_sliderChange_base`（:319）、`_stepEnabled_base`（:327）。

**注意事项与坑点**

1. **`XDial_wrapping` 不是"基类转发"**：头注释称"查询环绕（继承自基类 wrapping 语义的便捷转发）"（XDial.h:71），但 XAbstractSlider **没有** wrapping 字段或 API（`rg "wrapping" XAbstractSlider.h` 只命中 :332 的注释），实际读写的是 XDial 自己的位域 `m_wrapping`（XDial.h:49；实现 XDial.c:587-597）。
2. **`notchSize` 是间隔值不是刻度数**：量纲为范围单位，恒为 singleStep 的非零倍数，非 0 起点量程下不等于最大值；绘制端刻度数另按 `(maximum + ns - 1 - minimum) / ns` 算（XDial.h:75-83；XDial.c:649-658）。测试注释专门记录了这次量纲迁移（`Test/XGuiTest/XDialTest.c:49-52`）。
3. **`setNotchTarget` 接受 0/负值**：只赋值+update，不做校验；`setNotchTarget(0)` 使 notchSize 退化为 singleStep×1，负值由换算自行收敛（XDial.c:564-573；头注 XDial.h:64-66 亦声明）。旧实现 `target<=0` 直接 return 已废弃。
4. **wrapping 走 bound 虚槽才是全链路**：`setValue`/`setSliderPosition`/`triggerAction`/`stepBy`/滚轮全部经 `bound`；此前只有 stepBy 一条路回绕、键鼠仍钳位（XDial.c:672-675 注；XAbstractSlider.h:332-341）。
5. **sizeHint 曾缺失**：类此前无 sizeHint ⇒ 布局条目首选尺寸 0（竖排高度塌 0、控件不可见），现恒 100×100/50×50（XDial.c:516-520 注）；但 XDial.h 中没有公开的 `XDial_sizeHint` 函数，只能经虚槽/`XWidget_sizeHint` 取。
6. **无刻度时 notchTarget 仍参与绘制判断**：`notchSize` 依赖当前控件宽高（`pixels = min(w,h)`，XDial.c:628-630），初始化前（0×0）调用返回 singleStep（:632），因此"未设几何就取 notchSize"得到的值不可用于持久化。

**测试覆盖**：有 `Test/XGuiTest/XDialTest.c`（80 行），入口 `bool XDialTest_runAll(void)`（XDialTest.c:18），**13 个断言调用点**（`grep "dl_expect(" 去定义行` 计数 13；含定义共 14）。覆盖：默认范围 0..99/值 0/notchTarget 3.7/刻度隐藏/不环绕（:23-28）、值越界钳位与扩范围（:31-37）、专属属性往返（:40-47）、notchSize 随 notchTarget 单调（:53-59）、SingleStepAdd 步进（:62-66）。注册于 `Test/CMakeLists.txt:559`，由 `xgui_regression_test.c:36231` 调用。

---

## XDialog

**定位**：对话框基类（模态 exec、done/accept/reject 收口、结果码、模态开关、CSD 内容避让）。继承链 `XObject ← XWidget ← XDialog`。头文件 `Src/XGui/Widget/XDialog.h`（236 行）。开关 `XDIALOG_ON`（XDialog.h:26）。

**生命周期**：`XDialog_init` 在 flags 不含窗口类型位时自动叠 `XWindowType_Dialog`（对标 qdialog.cpp:374-378，XDialog.c:1076-1082），并把顶层默认几何预置 640×480（带父也不落成 100×30 空壳，XDialog.c:1084-1094），`m_resetModalityTo=-1`（:1110）；栈对象须与 `XClassDeinit` 成对（XDialog.h:72-73），堆对象经 `_create/_create_ex` 后 `XClassDelete`。

**API 面**

枚举：`XDialogCode`（Rejected=0/Accepted=1，数值对齐 QDialog::DialogCode qdialog.h:30）（XDialog.h:32-36）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDialog_class_init(void);` | 建表（进程内单例，继承 XWidget） | X 扩展 | 共享表，禁释放 |
| `void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags);` | 初始化（自动叠 Dialog 类型位、result=0、非模态） | `QDialog(QWidget*,Qt::WindowFlags)` qdialog.h:27 | — |
| `#define XDialog_create(parent, flags) …` | 默认内存建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存建堆对象 | X 扩展 | 调用方拥有 |
| `int XDialog_exec(XDialog* self);` | 阻塞模态事件循环，返回 result()；递归返回 -1；DeleteOnClose 时返回前登记延迟删除 | `exec` qdialog.h:53（实现 qdialog.cpp:553-587） | — |
| `void XDialog_done(XDialog* self, int result);` | 收口：隐藏+解模态+恢复 open 临时模态 → 发 accepted/rejected → 发 finished(result) | `done` qdialog.h:54（qdialog.cpp:608-626） | — |
| `void XDialog_accept(XDialog* self);` | done(Accepted)（信号由 done 内发，不重复） | `accept` qdialog.h:55 | — |
| `void XDialog_reject(XDialog* self);` | done(Rejected) | `reject` qdialog.h:56 | — |
| `int XDialog_result(const XDialog* self);` | 读结果码；NULL 返 0 | `result` qdialog.h:33 | 值 |
| `void XDialog_setResult(XDialog* self, int result);` | 写结果码（原样存取任意 int） | `setResult` qdialog.h:44 | — |
| `void XDialog_setModal(XDialog* self, bool modal);` | 设模态标志；置真且窗口模态仍 NonModal 时同步 ApplicationModal | `setModal` qdialog.h:43 | — |
| `bool XDialog_isModal(const XDialog* self);` | 查模态标志；NULL 返 false | `isModal`（Q_PROPERTY qdialog.h:24） | 值 |
| `void XDialog_open(XDialog* self);` | 窗口模态显示并立即返回：临时改 WindowModal（原值记 m_resetModalityTo）、setResult(0)、show、raise、登记应用模态门 | `open` qdialog.h:52（qdialog.cpp:509-526） | — |
| `void XDialog_setSizeGripEnabled(XDialog* self, bool enable);` | 尺寸手柄开关（**仅存位**，不嵌 XSizeGrip） | `setSizeGripEnabled` qdialog.h:40 | — |
| `bool XDialog_isSizeGripEnabled(const XDialog* self);` | 查尺寸手柄位 | `isSizeGripEnabled` qdialog.h:41 | 值 |
| `int XDialog_decorationTopOffset(const XDialog* self);` | CSD 装饰条高作布局避让量；非独立顶层/系统标题栏/CSD 抑制时返回 0 | **X 扩展**（Qt 无公开对应，见 XDialog.h:194-196） | 值 |
| `void* XDialog_accepted_signal(XDialog* self);` | accepted 信号（done(Accepted)/accept 内真发射） | `accepted` qdialog.h:48 | 信号标识地址 |
| `void* XDialog_rejected_signal(XDialog* self);` | rejected 信号（done(Rejected)/reject 内真发射） | `rejected` qdialog.h:49 | 信号标识地址 |
| `void* XDialog_finished_signal(XDialog* self, int result);` | finished(result)，在 accepted/rejected 之后发射 | `finished(int)` qdialog.h:47 | 信号标识地址 |

**所有权与命名契约**：遵循 XWidget 基类契约；无 `_2`/`_const` 变体；三态 `set*`：无三态取值（`setModal` 二态、`setResult` 任意 int、`setSizeGripEnabled` 二态）。`parent` 为借用，对话框自身恒为独立顶层窗口（带父也是），不作为父链子控件参与布局/绘制/命中（XDialog.c:1076-1080）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDialog)`+`XCLASS_DEFINE_EXTEND_END(XDialog, XWidget)`（XDialog.h:28-29），不新增槽。重载（XDialog.c:1053-1070）：`EXClass_Deinit`；`EXWidget_PaintEvent`→自绘面板+标题带；`EXWidget_KeyPressEvent`→Esc→reject、Return/Enter→默认按钮；`EXWidget_ShowEvent`→套用 CSD 避让+尺寸精修；`EXWidget_HideEvent`→解悬停/按压跟踪；`EXWidget_CloseEvent`；`EXWidget_MousePress/MouseRelease/MouseMove`→标题带拖动。`_base` 入口：`XWidget_closeEvent_base`（XWidget_Protected.h:127）、`_keyPressEvent_base`（:137）、`_paintEvent_base`（:121）、`_showEvent_base`/`_hideEvent_base`（:163/165）、`_mousePressEvent_base`/`_mouseReleaseEvent_base`/`_mouseMoveEvent_base`（:153/155/159）。**派生类须用 `XClass_Parent` 静态调父类实现**，不要在 `_base` 入口里再虚分派——否则递归回最派生重载；XGui.md:237-239 记录 ①对话框族键盘自递归 P0（XDialog/XMessageBox 改 XClass_Parent 静态调父类实现，递归链终止于 ignore 默认），实现内注见 XDialog.c:847-854。

**注意事项与坑点**

1. **exec 的 DeleteOnClose 不是"返回前删除"**：头注释称"设有 DeleteOnClose 属性时 exec 返回前删除对话框"（XDialog.h:98-99），实现是先取 result 再 `XObject_deleteLater`（XDialog.c:1313-1320），真正回收在事件循环归还后——注释自陈"同步删 self 即 UAF"。删除后不得再解引用 self。
2. **递归 exec 返回 -1 并打印警告**：`m_inExec` 置位期间再次 exec 走 `XPrintf("XDialog::exec: Recursive call detected")` 后返回 -1（XDialog.c:1259-1264；头注 XDialog.h:97-101）。exec 期间还临时清 `DeleteOnClose`、恢复 open 遗留模态（:1265-1271）。
3. **open 的模态是"临时的"**：open 前若窗口模态不是 WindowModal 则改 WindowModal 并记录原值，`done`/`exec` 经 `xdlg_resetModalitySetByOpen` 恢复（XDialog.c:1381-1388、:1180-1187；头注 XDialog.h:157-164）。因此 open 前后读 `windowModality` 应为 原值→WindowModal→原值。
4. **open 的模态门有例外**：XProgressDialog 的 open 不登记应用模态（vtable 向下识别，XDialog.c:1410-1421），因 Qt 6.8.3 qprogressdialog.cpp 全文无 setModal/setWindowModality；exec 路径不受影响（exec 无条件应用模态，XDialog.c:1279-1281）。
5. **accepted/rejected 由结果码映射，且消息盒有角色映射**：done 内先按 `xdlg_dialogCode` 决定发 accepted 还是 rejected，再发 finished（XDialog.c:1324-1338）；`xdlg_dialogCode` 对 XMessageBox 把标准按钮角色映射成 Accept/Reject（XDialog.c:1223-1250），所以消息盒 done(>1) 仍能发 accepted/rejected。
6. **setSizeGripEnabled 只是存位**：不自动嵌入 XSizeGrip 控件（XDialog.h:57-58、:169-174；实现 XDialog.c:1425-1428）。
7. **无 sizeHint/minimumSizeHint 覆写**：Qt 的 QDialog 覆写了二者（qdialog.h:37-38），X 侧 XDialog.h 未声明，走 XWidget 的 `EXWidget_SizeHint`/`EXWidget_MinimumSizeHint` 默认槽（XWidget.h:556-561）；需要内容驱动尺寸时要靠布局或派生类。
8. **CSD 避让是 X 独有机制**：`decorationTopOffset` 返回装饰条高，exec/open/show 时基类对布局挂载对话框增量改写根布局顶边距、对显式几何对话框整体下移直接子控件，消息盒/向导自管理除外（XDialog.h:183-203；XDialog.c:325-346）。增量为记账式（`m_csdAppliedTop`），重复调用幂等、CSD 关闭后 delta 为负亦安全。
9. **构造期的顶层几何预置**：带父创建的对话框若走默认尺寸会得到 100×30（XWidget_init 子控件分支），本类显式改 640×480（XDialog.c:1084-1094 注，账本 #3/#9 实证），派生类若自行 resize 不受影响。

**测试覆盖**：无 `Test/XGuiTest/XDialogTest.c`（`ls` 核实；`Test/XGuiTest/XGuiDialogMoveTest.c` 虽名含 Dialog，`rg "XDialog"` 零命中——它测的是 XWindowDecoration 标题带拖动）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c:446-580`（默认非模态/result 0/setResult/setModal/isModal/accepted+rejected+finished 信号/accept/reject/done(3)/open 前后模态往返/NULL 安全），该文件 `rg -c "XDialog_"`=44 处；`rg -c "XDialog_" xgui_regression_test.c`=48 处（含 decorationTopOffset 2 处）；`Test/XGuiDemo/*.c` 合计 93 处。
# 控件文档片段 · 批次 03

本批覆盖 Src/XGui/Widget/ 下 9 个控件/效果类。所有签名逐字抄自头文件；行号可核对。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/`（存在才写）。

---

## XDialogButtonBox

**定位**：对话框按钮排布控件（标准按钮按角色成组自动发射 accepted/rejected）。继承链 `XObject ← XWidget ← XDialogButtonBox`。头文件 `Src/XGui/Widget/XDialogButtonBox.h`（338 行）。模块开关 `XDIALOGBUTTONBOX_ON`，另需 `XWIDGET_ON && XPUSHBUTTON_ON`（XDialogButtonBox.h:42）。

**生命周期**：`XDialogButtonBox_init` 默认水平方向、右对齐、成员表空；栈对象须与 `XClassDeinit` 成对；堆对象经 `_create/_create_ex` 后须 `XClassDelete`（XDialogButtonBox.h:139-149；XDialogButtonBox.c:389-406）。内部桥接对象（`XDBBridge*`）由盒拥有并在 deinit 释放（XDialogButtonBox.c:353-362）。

**API 面**

枚举（XDialogButtonBox.h:46-108）：`XDialogButtonBoxRole`（-1..9，数值对齐 Qt::ButtonRole）、`XDialogButtonBoxStandardButton`（位值与 Qt 逐项一致，含 FirstButton/LastButton）、`XDialogButtonBoxLayout`（Win/Mac/Kde/Gnome/Android，数值 0..4）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDialogButtonBox_class_init(void);` | 建表（进程内单例，继承 XWidget 虚表） | 同名 `class_init` 惯用 | 共享表，禁释放 |
| `void XDialogButtonBox_init(XDialogButtonBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认水平/右对齐/空表） | QDialogButtonBox 构造 | — |
| `#define XDialogButtonBox_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XDialogButtonBox* XDialogButtonBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XDialogButtonBox_setOrientation(XDialogButtonBox* self, int orientation);` | 设排布方向（1=水平 2=垂直） | `setOrientation` qdialogbuttonbox.h:90 | — |
| `int XDialogButtonBox_orientation(const XDialogButtonBox* self);` | 查方向 | `orientation` qdialogbuttonbox.h:91 | — |
| `void XDialogButtonBox_setCenterButtons(XDialogButtonBox* self, bool center);` | 设按钮居中 | `setCenterButtons` qdialogbuttonbox.h:107 | — |
| `bool XDialogButtonBox_centerButtons(const XDialogButtonBox* self);` | 查居中位 | `centerButtons` qdialogbuttonbox.h:108 | — |
| `void XDialogButtonBox_addButton(XDialogButtonBox* self, XAbstractButton* button, XDialogButtonBoxRole role);` | 登记外部按钮并指定角色 | `addButton(QAbstractButton*,role)` qdialogbuttonbox.h:93 | 按钮借用，禁释放；移除/清空仅解登记 |
| `XPushButton* XDialogButtonBox_addButton_2(XDialogButtonBox* self, const char* utf8, XDialogButtonBoxRole role);` | 按 UTF-8 文本建按钮并指定角色 | `addButton(const QString&,role)` qdialogbuttonbox.h:94 | 新建按钮，父为本控件；调用方持有指针但归盒父链 |
| `XPushButton* XDialogButtonBox_addButton_3(XDialogButtonBox* self, XDialogButtonBoxStandardButton which);` | 建标准按钮（预置中文文案+预设角色） | `addButton(StandardButton)` qdialogbuttonbox.h:95 | 盒自建=盒拥有，clear/析构删除 |
| `void XDialogButtonBox_removeButton(XDialogButtonBox* self, XAbstractButton* button);` | 移除成员按钮（不删本体） | `removeButton` qdialogbuttonbox.h:96 | 仅解登记 |
| `void XDialogButtonBox_clear(XDialogButtonBox* self);` | 清空全部按钮 | `clear` qdialogbuttonbox.h:97 | 分级：自建按钮删、外来按钮摘父归还 |
| `const XVector* XDialogButtonBox_buttons(const XDialogButtonBox* self);` | 成员按钮数组（XAbstractButton*） | `buttons` qdialogbuttonbox.h:99 | 内部数组借用，禁释放/修改 |
| `XDialogButtonBoxRole XDialogButtonBox_buttonRole(const XDialogButtonBox* self, XAbstractButton* button);` | 查按钮角色 | `buttonRole` qdialogbuttonbox.h:100 | — |
| `void XDialogButtonBox_setStandardButtons(XDialogButtonBox* self, int buttons);` | 按位掩码批量建标准按钮 | `setStandardButtons` qdialogbuttonbox.h:102 | 同 addButton_3（盒拥有） |
| `int XDialogButtonBox_standardButtons(const XDialogButtonBox* self);` | 查标准按钮位掩码 | `standardButtons` qdialogbuttonbox.h:103 | — |
| `XDialogButtonBoxStandardButton XDialogButtonBox_standardButton(const XDialogButtonBox* self, XAbstractButton* button);` | 查按钮对应的标准枚举 | `standardButton` qdialogbuttonbox.h:104 | — |
| `XPushButton* XDialogButtonBox_button(const XDialogButtonBox* self, XDialogButtonBoxStandardButton which);` | 查标准按钮对应的成员 | `button(StandardButton)` qdialogbuttonbox.h:105 | 借用，不存在返回 NULL |
| `void* XDialogButtonBox_clicked_signal(XDialogButtonBox* self, XAbstractButton* button);` | 点击信号（真发射，载荷按钮） | `clicked(QAbstractButton*)` qdialogbuttonbox.h:111 | 信号标识地址 |
| `void* XDialogButtonBox_accepted_signal(XDialogButtonBox* self);` | 接受信号（Accept/YesRole 触达） | `accepted` qdialogbuttonbox.h:112 | 信号标识地址 |
| `void* XDialogButtonBox_helpRequested_signal(XDialogButtonBox* self);` | 帮助信号（HelpRole 触达） | `helpRequested` qdialogbuttonbox.h:113 | 信号标识地址 |
| `void* XDialogButtonBox_rejected_signal(XDialogButtonBox* self);` | 拒绝信号（Reject/NoRole 触达） | `rejected` qdialogbuttonbox.h:114 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_2`=UTF-8 文本重载（`addButton_2`）；`_3`=标准按钮重载（`addButton_3`）；无三态 `set*`（`setCenterButtons` 为二态 bool）。按钮所有权分级：盒自建（`m_standards` 非 0）归盒，外来按钮仅摘父归还（XDialogButtonBox.c:585-598）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDialogButtonBox)` + `XCLASS_DEFINE_EXTEND_END(XDialogButtonBox, XWidget)`（XDialogButtonBox.h:110-111）。虚表槽 `EXWidget_ResizeEvent`→`VX_dialogButtonBox_resizeEvent`（重排按钮）、`EXClass_Deinit`→`VX_dialogButtonBox_deinit`（析构桥接/向量）（XDialogButtonBox.c:382-386）。无 `_base` 公开入口。

**注意事项与坑点**

1. **clear 的所有权分级（真缺陷根修）**：`addButton_3` 创建的标准按钮（`m_standards` 非 0）为盒拥有，`clear`/析构须删除（经 `XObject_deleteLater` 异步回收，避免槽内同步删 UAF）；用户 `addButton` 传入的按钮（0 标记）仅摘父归还调用方（XDialogButtonBox.c:585-598）。此前一律摘父导致标准按钮成孤儿泄漏（ASan 归因 7.7KB/18 块），登记于 `XGui.md` §8.0g6「真缺陷 ②：XDialogButtonBox::clear 只摘父不删自建按钮」。
2. **焦点闭环必须在 `XFree_System(order)` 之前建立**：`order` 是本次布局临时序列，free 后再读即 use-after-free（回归实测 phase32 消息盒 `addButton_2` 路径 `xdb_relayout` 读已释放段段错误）；闭环把盒内按钮 `setTabOrder` 围成 i→i+1、末钮→首钮，使 Tab/Shift+Tab 不越出模态子树（XDialogButtonBox.c:326-333）。
3. **角色信号时序**：`clicked` 必须在发射前取定角色，且先发射 `clicked(button)` 再按角色发射 `accepted`/`rejected`/`helpRequested`（对标 Qt 注释 "Can't fetch this *after* emitting clicked"，qdialogbuttonbox.cpp:868-895；XDialogButtonBox.c:135-155）。
4. **头/实现矛盾**：XDialogButtonBox.h:185 注释称 orientation 无效时"返回 0 或 -1"，实现为 `self ? self->m_orientation : 1`（XDialogButtonBox.c:429-431）——NULL 实返 1（水平）。另 `XDialogButtonBox_init` 先写 `m_orientation = (int)XDialogButtonBoxLayout_WinLayout;`（=0）随即覆盖为 1（XDialogButtonBox.c:402-403），首写为死存储。
5. `XDialogButtonBox_buttons` 返回内部 `m_buttons` 数组借用指针，禁止释放或修改（XDialogButtonBox.h:252-256）。

**测试覆盖**：无 `Test/XGuiTest/XDialogButtonBoxTest.c` 专属套件。间接覆盖（`rg -c "\bXDialogButtonBox\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c`（9 处）、`xgui_demo_page_dialogs.c`（8 处）、`xgui_window_demo.c`（4 处）等。

---

## XDockWidget

**定位**：可停靠/可浮动面板控件（标题条拖动、浮动转独立顶层窗口）。继承链 `XObject ← XWidget ← XDockWidget`。头文件 `Src/XGui/Widget/XDockWidget.h`（299 行）。模块开关 `XDOCKWIDGET_ON`（XDockWidget.h:40）。

**生命周期**：`XDockWidget_init` 默认特性 Closable|Movable|Floatable、允许全部区域、非浮动，标题深拷贝为对象拥有的 XString；栈对象须与 `XClassDeinit` 成对；堆对象须 `XClassDelete`（XDockWidget.h:116-128）。`toggleViewAction` 惰性创建的动作归面板所有，随析构释放（XDockWidget.h:245-254）。

**API 面**

枚举（XDockWidget.h:43-63）：`XDockWidgetArea`（位掩码 0..0xf）、`XDockWidgetFeature`（位 0x1..0xf）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XDockWidget_class_init(void);` | 建表（单例，继承 XWidget） | 同名惯用 | 共享表，禁释放 |
| `void XDockWidget_init(XDockWidget* self, const char* utf8Title, XWidget* parent, XWidgetFlags flags);` | 初始化（默认全特性/容许全区/非浮动） | QDockWidget 构造 | — |
| `#define XDockWidget_create(title, parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XDockWidget* XDockWidget_create_ex(XMemoryType memory, const char* utf8Title, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XDockWidget_setWidget(XDockWidget* self, XWidget* widget);` | 设内容控件（二次调用先摘旧） | `setWidget` qdockwidget.h:36 | 内容归 dock；旧内容摘除后所有权回调用方 |
| `XWidget* XDockWidget_widget(const XDockWidget* self);` | 查内容控件 | `widget` qdockwidget.h:35 | 借用 |
| `void XDockWidget_setFeatures(XDockWidget* self, int features);` | 设特性位 | `setFeatures` qdockwidget.h:52 | — |
| `int XDockWidget_features(const XDockWidget* self);` | 查特性位 | `features` qdockwidget.h:53 | — |
| `void XDockWidget_setFloating(XDockWidget* self, bool floating);` | 浮/停切换（转独立顶层窗） | `setFloating` qdockwidget.h:55 | — |
| `bool XDockWidget_isFloating(const XDockWidget* self);` | 查浮动状态 | `isFloating` qdockwidget.h:56 | — |
| `void XDockWidget_setAllowedAreas(XDockWidget* self, int areas);` | 设允许停靠区（int 往返） | `setAllowedAreas` qdockwidget.h:58 | — |
| `int XDockWidget_allowedAreas(const XDockWidget* self);` | 查允许区 | `allowedAreas` qdockwidget.h:59 | — |
| `bool XDockWidget_isAreaAllowed(const XDockWidget* self, int area);` | 查指定区是否允许 | `isAreaAllowed` qdockwidget.h:64 | — |
| `void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget);` | 设自定义标题条（NULL 恢复内置） | `setTitleBarWidget` qdockwidget.h:61 | 借用，不转移所有权 |
| `#define XDockWidget_titleBarWidget(self) XWidget_titleBarWidget((const XWidget*)(self))` | 查自定义标题条（仅 `XGUI_CUSTOM_TITLEBAR_ON=1`） | `titleBarWidget` qdockwidget.h:62 | 借用 |
| `XWidget* XDockWidget_titleBarWidget(const XDockWidget* self);` | 查自定义标题条（仅 `XGUI_CUSTOM_TITLEBAR_ON=0`，自有实现） | `titleBarWidget` qdockwidget.h:62 | 借用 |
| `XAction* XDockWidget_toggleViewAction(XDockWidget* self);` | 显/隐切换动作（惰性创建，checked 随显隐同步） | `toggleViewAction` qdockwidget.h:72 | 归面板所有，禁释放 |
| `void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features);` | 特性变更信号 | `featuresChanged` qdockwidget.h:76 | 信号标识地址 |
| `void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel);` | 浮/停状态变更信号 | `topLevelChanged` qdockwidget.h:77 | 信号标识地址 |
| `void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas);` | 允许区变更信号 | `allowedAreasChanged` qdockwidget.h:78 | 信号标识地址 |
| `void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible);` | 可见性变更信号（show/hide 事件驱动） | `visibilityChanged` qdockwidget.h:79 | 信号标识地址 |
| `void* XDockWidget_dockLocationChanged_signal(XDockWidget* self, int area);` | 停靠区变更信号 | `dockLocationChanged` qdockwidget.h:80 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=所有查询取 const self；`_utf8` 设置侧=`init`/`create` 的 `utf8Title` 按 UTF-8 解码；无 `_2` 重载；无三态 `set*`（`setFeatures`/`setAllowedAreas` 为位集，`setFloating` 为 bool）。`m_allowedAreas`/`m_features` 声明为 int/uint8 原样往返存取外部值。

**可覆写点**：`XCLASS_DEFINE_BEGING(XDockWidget)` + `XCLASS_DEFINE_EXTEND_END(XDockWidget, XWidget)`（XDockWidget.h:65-66）。虚表槽（XDockWidget.c:1417-1431）：`EXWidget_PaintEvent`→标题条绘制、`MousePress/Move/Release/DoubleClickEvent`→拖动与浮/停手势、`ResizeEvent`→内容重摆+浮动几何记录、`Show/Hide/CloseEvent`→可见性广播、`EXClass_Deinit`→失活。无 `_base` 公开入口（`XDockWidget_setHost` 为 Protected 接口）。

**注意事项与坑点**

1. **无宿主不得早退**：`setFloating(false)` 在无宿主登记时仍须复位 `m_floating` 并发射 `topLevelChanged`（回归契约，apitest D 段口径）——此前「无宿主即早退」会同时吞掉状态翻转与信号（XDockWidget.c:1594-1598）。
2. **不可重停靠守卫**：面板已登记宿主、区域为 `NoDockWidgetArea` 且**当前非浮动**时不得强行挂回（显式排除浮动态，因 C 的浮动位与父链是两个账本，否则 `setFloating(false)` 与 `addDockWidget` 重停靠会被误拦）（XDockWidget.c:1575-1587）。
3. **状态切换须终止拖动并释放鼠标抓取**：对标 Qt `setFloating` 首行 `endDrag(Abort)`——否则双击切换后全局鼠标抓取泄漏，后续所有窗口鼠标事件被重定向进本面板（XDockWidget.c:1611-1615）。
4. **keepSize 无等价物（已知差异）**：本框架无 KeepSize 标志，停靠区尺寸由 XMainWindow 的 `m_dockHeights` 记账，隐藏面板时让出空间给同区其余面板——与 Qt keepSize「保留占位」不同（XDockWidget.c:440-447）。
5. **控件级 titleBarWidget 为 X 扩展**：Qt 仅 QDockWidget 有此 API（qdockwidget.h:61-62），X 把该槽提升为控件级通用槽，`XDockWidget_setTitleBarWidget` 为薄包装转发父类、查询用宏映射 `XWidget_titleBarWidget`，实现单一无重复（`XGui.md` §8.1.1「X 扩展」段；`out/xwidget_align/visibility.md:101` C1）。
6. `XGUI_CUSTOM_TITLEBAR_ON=1` 时自定义标题条由父类槽 `XWidget::m_titleBarWidget` 承载，本类自有 `m_titleBar` 成员删除（XDockWidget.h:73-79）。

**测试覆盖**：无 `Test/XGuiTest/XDockWidgetTest.c` 专属套件。间接覆盖（`rg -c "\bXDockWidget\b" Test/`）：`Test/XGuiTest/XGuiDockDragTest.c`（21 处）、`Test/XGuiDemo/xgui_demo_apitest_containers.c`（39 处）、`xgui_demo_page_advanced.c`（21 处）等。

---

## XErrorMessage

**定位**：错误消息对话框（带"再次显示"去重抑制表）。继承链 `XObject ← XWidget ← XDialog ← XErrorMessage`。头文件 `Src/XGui/Widget/XErrorMessage.h`（119 行）。模块开关 `XERRORMESSAGE_ON`，另需 `XWIDGET_ON && XDIALOG_ON`（XErrorMessage.h:27）。

**生命周期**：`XErrorMessage_init` 建对象拥有的消息/类型/两张抑制表；栈对象须与 `XClassDeinit` 成对；堆对象须 `XClassDelete`（XErrorMessage.h:32-45, 51-65）。`m_message`/`m_currentType`/`m_doNotShow`/`m_doNotShowType` 均对象拥有。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XErrorMessage_class_init(void);` | 建表（单例，继承 XDialog） | 同名惯用 | 共享表，禁释放 |
| `void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags);` | 初始化 | QErrorMessage 构造 | — |
| `#define XErrorMessage_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XErrorMessage* XErrorMessage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XErrorMessage_showMessage(XErrorMessage* self, const char* msg);` | 弹错误消息（UTF-8） | `showMessage(const QString&)` qerrormessage.h:28 | — |
| `void XErrorMessage_showMessage_2(XErrorMessage* self, const char* msg, const char* type);` | 弹带类型错误消息（受抑制表过滤） | `showMessage(msg,type)` qerrormessage.h:29 | — |
| `void XErrorMessage_done(XErrorMessage* self, int result);` | 收口（按复选框等价位记抑制表） | `done(int)` qerrormessage.h:32 | — |
| `const char* XErrorMessage_currentMessage(const XErrorMessage* self);` | 查当前消息文本 | X 扩展（Qt 6.8 qerrormessage.h 无公有 currentMessage） | 借用内部 UTF-8，禁释放 |
| `void XErrorMessage_setDoneShown(XErrorMessage* self, bool on);` | 设"再次显示"等价位（done 时是否记抑制表） | X 扩展（Qt 对应私有 againCheckBox） | — |
| `bool XErrorMessage_isDoneShown(const XErrorMessage* self);` | 查"再次显示"等价位 | X 扩展 | — |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`currentMessage`/`isDoneShown`）；`_2`=双参重载（`showMessage_2`，type 维度去重）；无 `_utf8` 后缀（入参即 UTF-8）；无三态 `set*`（`setDoneShown` 为 bool 二态）。`currentMessage` 返回内部借用 UTF-8 文本，无消息时返回空串。

**可覆写点**：`XCLASS_DEFINE_BEGING(XErrorMessage)` + `XCLASS_DEFINE_EXTEND_END(XErrorMessage, XDialog)`（XErrorMessage.h:29-30）。虚表槽（XErrorMessage.c:166-169）：`EXWidget_PaintEvent`→横幅绘制、`EXWidget_ShowEvent`→尺寸刷新、`EXClass_Deinit`→释放消息/表。无 `_base` 公开入口。

**注意事项与坑点**

1. **空串消息参与显示（与 Qt 的偏差，双侧测试锁共载）**：本库契约要求 `showMessage(空串)` 走应用路径（清当前消息并重算固定尺寸回落 300x40 下限），而 Qt 在 `isMessageToBeShown` 对空串直接短路；NULL 仍为无操作（XErrorMessage.c:242-248）。r1#5 缺陷锁与 apitest menus 族共载。
2. **`done` 收口时 `m_message` 保留不清**：Qt 收口后清 currentMessage/currentType 内部态；本库 `m_message` 兼作横幅显示文本与 `currentMessage()` 读数，收口后保留（XErrorMessage.c:341-345）。
3. **头/实现矛盾（Qt 对标标注）**：XErrorMessage.h:98/104 将 `currentMessage`/`setDoneShown`/`isDoneShown` 标为"对标 Qt 同名接口"，但 Qt 6.8 `qerrormessage.h` 公有区仅有 `qtHandler`(25)/`showMessage`(28,29)/`done`(32)，三者实为 X 扩展。
4. **无 pending 队列**：Qt 另有 pending 队列承载 `qtHandler` 的异步日志消息，本库无消息处理器设施，直接显示为轻量等价，队列未复刻（XErrorMessage.c:263-267）；`qtHandler` 依赖 `qInstallMessageHandler` 全局设施，未提供（XErrorMessage.h:11-13 依赖缺口登记）。

**测试覆盖**：无 `Test/XGuiTest/XErrorMessageTest.c` 专属套件。间接覆盖（`rg -c "\bXErrorMessage\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c`（7 处）、`xgui_demo_apitest_menus.c`（5 处）、`xgui_window_demo.c`（4 处）等。

---

## XFileDialog

**定位**：文件选择对话框（静态便捷函数 + 实例属性 + 信号）。继承链 `XObject ← XWidget ← XDialog ← XFileDialog`。头文件 `Src/XGui/Widget/XFileDialog.h`（889 行）。模块开关 `XDIALOG_ON`，另需 `XWIDGET_ON`（XFileDialog.h:54）。

**生命周期**：`XFileDialog_init` 默认 AnyFile/AcceptOpen/Detail/options=0；栈对象须与 `XClassDeinit` 成对；堆对象须 `XClassDelete`（XFileDialog.h:143-166）。全部字符串字段为拥有型 `XString*`、列表字段为拥有型 `XStringList*`，随析构释放（XFileDialog.h:107-135）。

**API 面**

枚举（XFileDialog.h:56-102）：`XFileDialogFileMode`（0..3）、`XFileDialogAcceptMode`（0..1）、`XFileDialogDialogLabel`（0..4）、`XFileDialogOption`（位 0x1..0x40）、`XFileDialogViewMode`（0..1），数值与 Qt 6.8.3 `qfiledialog.h` 一致；`XFileDialogOptions=uint32_t` 位集。

生命周期/通用：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XFileDialog_class_init(void);` | 建表（单例，继承 XDialog） | 同名惯用 | 共享表，禁释放 |
| `void XFileDialog_init(XFileDialog* self, XWidget* parent, XWidgetFlags flags);` | 初始化 | QFileDialog 构造 | — |
| `#define XFileDialog_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XFileDialog* XFileDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XFileDialog_setFileMode(XFileDialog* self, XFileDialogFileMode mode);` | 设文件模式 | `setFileMode` qfiledialog.h:100 | — |
| `XFileDialogFileMode XFileDialog_fileMode(const XFileDialog* self);` | 查文件模式 | `fileMode` qfiledialog.h:101 | — |
| `void XFileDialog_accept(XFileDialog* self);` | 按 fileMode 校验后收口 | `accept` qfiledialog.h:234 | — |
| `void XFileDialog_setAcceptMode(XFileDialog* self, XFileDialogAcceptMode mode);` | 设接受模式 | `setAcceptMode` qfiledialog.h:103 | — |
| `XFileDialogAcceptMode XFileDialog_acceptMode(const XFileDialog* self);` | 查接受模式 | `acceptMode` qfiledialog.h:104 | — |
| `void XFileDialog_setViewMode(XFileDialog* self, XFileDialogViewMode mode);` | 设视图模式 | `setViewMode` qfiledialog.h:97 | — |
| `XFileDialogViewMode XFileDialog_viewMode(const XFileDialog* self);` | 查视图模式 | `viewMode` qfiledialog.h:98 | — |

名称/目录/选中：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XFileDialog_setNameFilter(XFileDialog* self, const XString* filter);` | 设单一名称过滤器 | `setNameFilter` qfiledialog.h:81 | 借用入参，深拷贝存储 |
| `void XFileDialog_setNameFilters(XFileDialog* self, const XStringList* filters);` | 设过滤器列表 | `setNameFilters` qfiledialog.h:82 | 借用入参，深拷贝存储 |
| `XStringList* XFileDialog_nameFilters(const XFileDialog* self);` | 查过滤器列表副本 | `nameFilters` qfiledialog.h:83 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_selectNameFilter(XFileDialog* self, const XString* filter);` | 选中名称过滤器 | `selectNameFilter` qfiledialog.h:84 | — |
| `XString* XFileDialog_selectedNameFilter(const XFileDialog* self);` | 查当前选中过滤器 | `selectedNameFilter` qfiledialog.h:86 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setDirectory(XFileDialog* self, const XString* directory);` | 设当前目录 | `setDirectory` qfiledialog.h:68 | 借用入参，深拷贝存储 |
| `XString* XFileDialog_directory(const XFileDialog* self);` | 查当前目录副本 | `directory` qfiledialog.h:70 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_selectFile(XFileDialog* self, const XString* filename);` | 加入选中列表 | `selectFile` qfiledialog.h:75 | — |
| `XStringList* XFileDialog_selectedFiles(const XFileDialog* self);` | 查已选文件列表副本 | `selectedFiles` qfiledialog.h:76 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_selectedFile(const XFileDialog* self);` | 查第一个已选文件 | `selectedFiles` 首项 | 新建返回，须 `XClassDelete` |
| `XStringList* XFileDialog_selectedUrls(const XFileDialog* self);` | 查已选 URL（从 selectedFiles 派生） | `selectedUrls` qfiledialog.h:79 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setDirectoryUrl(XFileDialog* self, const XString* directory);` | 设目录 URL（路径字符串承载） | `setDirectoryUrl` qfiledialog.h:72 | 借用入参，深拷贝存储 |
| `XString* XFileDialog_directoryUrl(const XFileDialog* self);` | 查目录 URL | `directoryUrl` qfiledialog.h:73 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_selectUrl(XFileDialog* self, const XString* url);` | 选中 URL（记录为选中文件） | `selectUrl` qfiledialog.h:78 | — |
| `void XFileDialog_setFilter(XFileDialog* self, int filters);` | 设目录枚举过滤位集 | `setFilter(QDir::Filters)` qfiledialog.h:95 | — |
| `int XFileDialog_filter(const XFileDialog* self);` | 查目录枚举过滤位集 | `filter` qfiledialog.h:94 | — |
| `void XFileDialog_setMimeTypeFilters(XFileDialog* self, const XStringList* filters);` | 设 MIME 过滤器列表 | `setMimeTypeFilters` qfiledialog.h:89 | 借用入参，深拷贝存储 |
| `XStringList* XFileDialog_mimeTypeFilters(const XFileDialog* self);` | 查 MIME 过滤器列表 | `mimeTypeFilters` qfiledialog.h:90 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_selectMimeTypeFilter(XFileDialog* self, const XString* filter);` | 选择 MIME 过滤器 | `selectMimeTypeFilter` qfiledialog.h:91 | — |
| `XString* XFileDialog_selectedMimeTypeFilter(const XFileDialog* self);` | 查当前选中 MIME 过滤器 | `selectedMimeTypeFilter` qfiledialog.h:85 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setHistory(XFileDialog* self, const XStringList* history);` | 设历史目录列表 | `setHistory` qfiledialog.h:115 | 借用入参，深拷贝存储 |
| `XStringList* XFileDialog_history(const XFileDialog* self);` | 查历史目录列表 | `history` qfiledialog.h:116 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setSidebarUrls(XFileDialog* self, const XStringList* urls);` | 设侧栏 URL 列表 | `setSidebarUrls` qfiledialog.h:106 | 借用入参，深拷贝存储 |
| `XStringList* XFileDialog_sidebarUrls(const XFileDialog* self);` | 查侧栏 URL 列表 | `sidebarUrls` qfiledialog.h:107 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setSupportedSchemes(XFileDialog* self, const XStringList* schemes);` | 设支持协议列表 | `setSupportedSchemes` qfiledialog.h:127 | 借用入参，深拷贝存储 |
| `XStringList* XFileDialog_supportedSchemes(const XFileDialog* self);` | 查支持协议列表 | `supportedSchemes` qfiledialog.h:128 | 新建返回，须 `XClassDelete` |

状态/选项/标签/未映射记录：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XFileDialog_saveState(const XFileDialog* self, XByteArray* out);` | 保存状态（当前输出空字节数组，序列化未实现） | `saveState` qfiledialog.h:109 | out 为调用方提供的输出容器 |
| `bool XFileDialog_restoreState(XFileDialog* self, const XByteArray* state);` | 恢复状态（当前恒 false） | `restoreState` qfiledialog.h:110 | 借用，不读取 |
| `void XFileDialog_setIconProvider(XFileDialog* self, void* provider);` | 设图标提供者（类型不映射，仅记录） | `setIconProvider` qfiledialog.h:121 | 借用，不取得所有权 |
| `void* XFileDialog_iconProvider(const XFileDialog* self);` | 查图标提供者（恒 NULL） | `iconProvider` qfiledialog.h:122 | 借用 |
| `void XFileDialog_setItemDelegate(XFileDialog* self, void* delegate);` | 设条目代理（仅记录） | `setItemDelegate` qfiledialog.h:118 | 借用，不取得所有权 |
| `void* XFileDialog_itemDelegate(const XFileDialog* self);` | 查条目代理（恒 NULL） | `itemDelegate` qfiledialog.h:119 | 借用 |
| `void XFileDialog_setProxyModel(XFileDialog* self, void* model);` | 设代理模型（仅记录） | `setProxyModel` qfiledialog.h:131 | 借用，不取得所有权 |
| `void* XFileDialog_proxyModel(const XFileDialog* self);` | 查代理模型（恒 NULL） | `proxyModel` qfiledialog.h:132 | 借用 |
| `void XFileDialog_setDefaultSuffix(XFileDialog* self, const XString* suffix);` | 设默认后缀 | `setDefaultSuffix` qfiledialog.h:112 | 借用入参，深拷贝存储 |
| `XString* XFileDialog_defaultSuffix(const XFileDialog* self);` | 查默认后缀 | `defaultSuffix` qfiledialog.h:113 | 新建返回，须 `XClassDelete` |
| `void XFileDialog_setOption(XFileDialog* self, XFileDialogOption option, bool on);` | 设单个选项位 | `setOption` qfiledialog.h:135 | — |
| `bool XFileDialog_testOption(const XFileDialog* self, XFileDialogOption option);` | 测选项位 | `testOption` qfiledialog.h:136 | — |
| `void XFileDialog_setOptions(XFileDialog* self, XFileDialogOptions options);` | 设整个选项位集 | `setOptions` qfiledialog.h:137 | — |
| `XFileDialogOptions XFileDialog_options(const XFileDialog* self);` | 查选项位集 | `options` qfiledialog.h:138 | — |
| `void XFileDialog_setLabelText(XFileDialog* self, XFileDialogDialogLabel label, const XString* text);` | 设标签文本 | `setLabelText` qfiledialog.h:124 | — |
| `XString* XFileDialog_labelText(const XFileDialog* self, XFileDialogDialogLabel label);` | 查标签文本 | `labelText` qfiledialog.h:125 | 新建返回，须 `XClassDelete` |

静态便捷函数（XFileDialog.h:534-810，每个均带 `_2` UTF-8 重载）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XString* XFileDialog_getOpenFileName(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取单个打开文件名 | `getOpenFileName` qfiledialog.h:159 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getOpenFileName_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XStringList* XFileDialog_getOpenFileNames(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取多个打开文件名 | `getOpenFileNames` qfiledialog.h:200 | 新建返回，须 `XClassDelete` |
| `XStringList* XFileDialog_getOpenFileNames_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getSaveFileName(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取保存文件名 | `getSaveFileName` qfiledialog.h:174 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getSaveFileName_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getExistingDirectory(XWidget* parent, const XString* caption, const XString* dir);` | 取已存在目录 | `getExistingDirectory` qfiledialog.h:189 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getExistingDirectory_2(XWidget* parent, const char* caption, const char* dir);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getOpenFileUrl(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取打开文件 URL（路径承载） | `getOpenFileUrl` qfiledialog.h:166 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getOpenFileUrl_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getSaveFileUrl(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取保存文件 URL | `getSaveFileUrl` qfiledialog.h:181 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getSaveFileUrl_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getExistingDirectoryUrl(XWidget* parent, const XString* caption, const XString* dir);` | 取已存在目录 URL | `getExistingDirectoryUrl` qfiledialog.h:194 | 新建返回，须 `XClassDelete` |
| `XString* XFileDialog_getExistingDirectoryUrl_2(XWidget* parent, const char* caption, const char* dir);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |
| `XStringList* XFileDialog_getOpenFileUrls(XWidget* parent, const XString* caption, const XString* dir, const XString* filter, int* selectedFilterIndex);` | 取多个打开文件 URL | `getOpenFileUrls` qfiledialog.h:207 | 新建返回，须 `XClassDelete` |
| `XStringList* XFileDialog_getOpenFileUrls_2(XWidget* parent, const char* caption, const char* dir, const char* filter, int* selectedFilterIndex);` | 同上（UTF-8） | 同上 | 新建返回，须 `XClassDelete` |

内容便捷函数与回调类型（XFileDialog.h:749-810）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `typedef void (*XFileDialogFileContentReady)(void* userData, const XString* fileName, const XByteArray* content);` | 文件内容就绪回调（函数指针+userData 惯例） | Qt `getOpenFileContent` 回调 | fileName/content 仅回调期内有效 |
| `void XFileDialog_getOpenFileContent(const XString* nameFilter, XFileDialogFileContentReady ready, void* userData, XWidget* parent);` | 选文件并读全部字节 | `getOpenFileContent` qfiledialog.h:215 | — |
| `void XFileDialog_getOpenFileContent_2(const char* nameFilter, XFileDialogFileContentReady ready, void* userData, XWidget* parent);` | 同上（UTF-8） | 同上 | — |
| `void XFileDialog_saveFileContent(const XByteArray* content, const XString* fileNameHint, XWidget* parent);` | 选保存位置并写全部字节 | `saveFileContent` qfiledialog.h:219 | content 借用，写入即返回 |
| `void XFileDialog_saveFileContent_2(const XByteArray* content, const char* fileNameHint, XWidget* parent);` | 同上（UTF-8） | 同上 | 借用 |

信号（XFileDialog.h:812-879）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XFileDialog_fileSelected_signal(XFileDialog* self, const XString* file);` | 单文件选中 | `fileSelected` qfiledialog.h:145 | 载荷深拷贝 |
| `void* XFileDialog_filesSelected_signal(XFileDialog* self, const XStringList* files);` | 多文件选中 | `filesSelected` qfiledialog.h:146 | 载荷深拷贝 |
| `void* XFileDialog_currentChanged_signal(XFileDialog* self, const XString* path);` | 当前路径变化 | `currentChanged` qfiledialog.h:147 | 载荷深拷贝 |
| `void* XFileDialog_directoryEntered_signal(XFileDialog* self, const XString* directory);` | 目录进入 | `directoryEntered` qfiledialog.h:148 | 载荷深拷贝 |
| `void* XFileDialog_filterSelected_signal(XFileDialog* self, const XString* filter);` | 过滤器选中 | `filterSelected` qfiledialog.h:155 | 载荷深拷贝 |
| `void* XFileDialog_urlSelected_signal(XFileDialog* self, const XString* url);` | 单 URL 选中 | `urlSelected` qfiledialog.h:150 | 载荷深拷贝 |
| `void* XFileDialog_urlsSelected_signal(XFileDialog* self, const XStringList* urls);` | 多 URL 选中 | `urlsSelected` qfiledialog.h:151 | 载荷深拷贝 |
| `void* XFileDialog_currentUrlChanged_signal(XFileDialog* self, const XString* url);` | 当前 URL 变化 | `currentUrlChanged` qfiledialog.h:152 | 载荷深拷贝 |
| `void* XFileDialog_directoryUrlEntered_signal(XFileDialog* self, const XString* directory);` | 目录 URL 进入 | `directoryUrlEntered` qfiledialog.h:153 | 载荷深拷贝 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；`_utf8` 设置侧=带 `_2` 后缀的 UTF-8 重载（`getOpenFileName_2` 等）；`_2`=UTF-8/双参重载；无三态 `set*`（`setOption` 为 `bool on` 二态，`setFileMode`/`setAcceptMode`/`setViewMode` 单值枚举）。查询类 getter 统一"新建返回、调用方 `XClassDelete`"，设置类入参统一"借用、内部深拷贝存储"。

**可覆写点**：`XCLASS_DEFINE_BEGING(XFileDialog)` + `XCLASS_DEFINE_EXTEND_END(XFileDialog, XDialog)`（XFileDialog.h:104-105）。虚表槽（XFileDialog.c:311-312）：`EXClass_Deinit`→释放全部 XString/XStringList 字段。无 `_base` 公开入口。

**注意事项与坑点**

1. **无 GUI 环境静态函数返回默认值**：无 `XCoreApplication` 实例时静态函数创建临时实例、应用 setter，但不执行模态循环，一律返回默认值（字符串空串、列表空列表），`*selectedFilterIndex` 保持 0（XFileDialog.h:24-31）。
2. **多选未实化**：`getOpenFileNames` 多选未实化（`XGui.md` §8.2「XFileDialog 多选（getOpenFileNames）未实化」，XGui.md:1601）；`saveState`/`restoreState` 状态序列化未实现（已知偏差 Task 2.20，XFileDialog.h:419-433）。
3. **IconProvider/ItemDelegate/ProxyModel 类型不映射**：仅记录借用指针，getter 恒 NULL（XFileDialog.h:435-473）；XGui 无 QFileIconProvider 对应物。
4. **`accept` 为 C 同名漏斗**：C 无虚函数，GUI 路径 OK 槽结算文件名后转发到 `XFileDialog_accept`，实例路径可直接调用；校验通过恒发 `filesSelected`，单元素时补发 `fileSelected`（XFileDialog.h:183-198）。
5. **路径父目录平台根修**：旧实现只认 `/`，Windows CWD 形如 `C:\Users\x`（无正斜杠）→ 无分隔符分支无限递归栈溢出；现版 `/` 与 `\` 同等对待、识别盘符根、递归深度守卫（XFileDialog.c:1420-1430）。
6. **视图装配显式关水平滚动条**：详情列模型下关横条消除"幻影横条"（r1 猎捕 defect#13，XFileDialog.c:2908-2919）；文件列表键盘激活须接线 `activated` 信号，否则事件被消费不进目录/选文件（XFileDialog.c:3152-3158）。

**测试覆盖**：无 `Test/XGuiTest/XFileDialogTest.c` 专属套件。间接覆盖（`rg -c "\bXFileDialog\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c`（11 处）、`xgui_demo_page_dialogs.c`（3 处）、`xgui_dialog_portrait_probe.c`（1 处）等。

---

## XFocusFrame

**定位**：环绕目标控件绘制焦点指示框。继承链 `XObject ← XWidget ← XFocusFrame`。头文件 `Src/XGui/Widget/XFocusFrame.h`（76 行）。模块开关 `XFOCUSFRAME_ON`（XFocusFrame.h:24）。

**生命周期**：遵循基类 XWidget 约定（`XFocusFrame_init` 设 `NoFocus` 焦点策略，XFocusFrame.c:240）。`setWidget` 建立关联，目标为借用（XFocusFrame.h:5-6）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XFocusFrame_class_init(void);` | 建表（单例，继承 XWidget） | 同名惯用 | 共享表，禁释放 |
| `void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags);` | 初始化 | QFocusFrame 构造 | — |
| `#define XFocusFrame_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XFocusFrame* XFocusFrame_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XFocusFrame_setWidget(XFocusFrame* self, XWidget* widget);` | 建立焦点框与目标关联（NULL 解除并隐藏） | `setWidget` qfocusframe.h:23 | 目标借用，归调用方 |
| `XWidget* XFocusFrame_widget(const XFocusFrame* self);` | 查关联的目标控件 | `widget` qfocusframe.h:24 | 借用 |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`widget`）；无 `_utf8`/`_2`/三态 `set*`。`setWidget` 目标为借用，归调用方；`destroyed` 连接自动解除（XFocusFrame.h:49-57）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XFocusFrame)` + `XCLASS_DEFINE_EXTEND_END(XFocusFrame, XWidget)`（XFocusFrame.h:26-27）。虚表槽（XFocusFrame.c:220-223）：`EXWidget_PaintEvent`→2px 描边绘制、`EXObject_TimerEvent`→30ms 几何跟随轮询、`EXClass_Deinit`→停轮询。无 `_base` 公开入口。

**注意事项与坑点**

1. **目标为顶层窗口时不跟踪**：`setWidget` 传入 `XWidget_isWindow` 为真的目标时不跟踪（置空并隐藏，对标 Qt 同 `isWindow` 分支）（XFocusFrame.c:266-273）。
2. **30ms 轮询等价 Qt eventFilter**：框架内部几何事件经 `XWidget_event_base` 直发、不经 `XCoreApplication_notify` 分发，事件过滤器收不到，故以 30ms 轮询定时器（`XFOCUSFRAME_FOLLOW_MS`）等价承载（XFocusFrame.c:5-15；XFocusFrame.h:33-35）。
3. **外扩固定 2px**：`XFOCUSFRAME_MARGIN=2`，为 Qt 样式 `PM_FocusFrame*Margin` 的轻量等价，与 paintEvent 的 2px 描边一致（XFocusFrame.c:28-31, 42-55）。
4. **头文件重复 doc block**：XFocusFrame.h:49-57 与 :58-61 为两段并列的 `setWidget` `@brief`（前者带完整 @details，后者为简注），同一 API 文档重复，汇编时需择一。
5. 目标销毁经 `destroyed` 连接自动解除（源注释见 XFocusFrame.h:55-56）。

**测试覆盖**：无 `Test/XGuiTest/XFocusFrameTest.c` 专属套件。间接覆盖（`rg -c "\bXFocusFrame\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_containers.c`（12 处）、`xgui_demo_page_advanced.c`（9 处）、`Test/XGuiTest/XGuiRemoteTest.c`（1 处）等。

---

## XFontComboBox

**定位**：字体族选择下拉框（构造时按字体族列表填充条目）。继承链 `XObject ← XWidget ← XComboBox ← XFontComboBox`。头文件 `Src/XGui/Widget/XFontComboBox.h`（248 行）。模块开关 `XFONTCOMBOBOX_ON`，另需 `XWIDGET_ON && XCOMBOBOX_ON`（XFontComboBox.h:28）。

**生命周期**：`XFontComboBox_init` 经 `XPlatformFontDatabase` 字体族列表填充条目；`m_currentFont` 深拷贝/转移遵循 XClass Copy/Move 槽；栈对象须与 `XClassDeinit` 成对；堆对象须 `XClassDelete`（XFontComboBox.h:93-101；XFontComboBox.c:320-372）。

**API 面**

枚举（XFontComboBox.h:31-61）：`XFontComboBoxFilter`（位 0x0..0x8）、`XFontComboBoxWritingSystem`（简化子集，数值对齐 Qt::WritingSystem 对应项）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XFontComboBox_class_init(void);` | 建表（单例，继承 XComboBox） | 同名惯用 | 共享表，禁释放 |
| `void XFontComboBox_init(XFontComboBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化并填充字体族条目 | QFontComboBox 构造 | — |
| `#define XFontComboBox_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XFontComboBox* XFontComboBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XFontComboBox_setFontFilters(XFontComboBox* self, int filters);` | 设字体过滤器位 | `setFontFilters` qfontcombobox.h:41 | — |
| `int XFontComboBox_fontFilters(const XFontComboBox* self);` | 查字体过滤器位 | `fontFilters` qfontcombobox.h:42 | — |
| `void XFontComboBox_setWritingSystem(XFontComboBox* self, int writingSystem);` | 设书写系统（仅状态承载） | `setWritingSystem` qfontcombobox.h:28 | — |
| `int XFontComboBox_writingSystem(const XFontComboBox* self);` | 查书写系统 | `writingSystem` qfontcombobox.h:29 | — |
| `const char* XFontComboBox_currentFamily(const XFontComboBox* self);` | 查当前字体族名 | `currentFont().family()` qfontcombobox.h:44 | 借用内部缓存，禁释放 |
| `void XFontComboBox_setCurrentFamily(XFontComboBox* self, const char* family);` | 按族名选中条目 | `setCurrentFont(QFont(family))` qfontcombobox.h:57 | — |
| `#define XFontComboBox_currentFont(self) XFontComboBox_currentFamily((self))` | 查当前字体（宏别名，以 UTF-8 族名承载 QFont） | `currentFont` qfontcombobox.h:44 | 借用内部缓存，禁释放 |
| `void XFontComboBox_setCurrentFont(XFontComboBox* self, const char* family);` | 按族名选中条目（转发 setCurrentFamily） | `setCurrentFont` qfontcombobox.h:57 | — |
| `void XFontComboBox_setDisplayFont(XFontComboBox* self, const XFont* font);` | 设显示字体状态（值承载，不改条目/不发信号） | `setDisplayFont(QString,QFont)` qfontcombobox.h:53 | 字体入参借用、只读；内部深拷贝 |
| `XFont XFontComboBox_displayFont(const XFontComboBox* self);` | 查显示字体状态 | `displayFont` qfontcombobox.h:54 | 值拷贝，须 `XClassDeinit` 释放内部字符串 |
| `XString* XFontComboBox_sampleTextForFont(const XFontComboBox* self, const char* family);` | 查字体族样例文本（未自定义回退族名） | `sampleTextForFont` qfontcombobox.h:51 | 新建返回，须 `XClassDelete` |
| `XString* XFontComboBox_sampleTextForSystem(const XFontComboBox* self, int system);` | 查书写系统样例文本（未自定义回退当前族名） | `sampleTextForSystem` qfontcombobox.h:48 | 新建返回，须 `XClassDelete` |
| `void XFontComboBox_setSampleTextForFont(XFontComboBox* self, const char* family, const char* sample);` | 设字体族样例文本（状态承载，NULL 移除） | `setSampleTextForFont` qfontcombobox.h:50 | sample 深拷贝 |
| `void XFontComboBox_setSampleTextForSystem(XFontComboBox* self, int system, const char* sample);` | 设书写系统样例文本（NULL 移除） | `setSampleTextForSystem` qfontcombobox.h:47 | sample 深拷贝 |
| `void* XFontComboBox_currentFontChanged_signal(XFontComboBox* self, const char* family);` | 当前字体族变化信号（载荷族名 UTF-8） | `currentFontChanged` qfontcombobox.h:60 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`fontFilters`/`writingSystem`/`currentFamily`/`displayFont`/`sampleTextFor*`）；`_utf8` 设置侧未使用后缀（入参直接 `const char*`）；无 `_2` 重载；无三态 `set*`（`setFontFilters` 为位集）。getter 分级：`currentFamily` 借用内部缓存，`displayFont` 值拷贝须 `XClassDeinit`，`sampleTextFor*` 新建 XString 须 `XClassDelete`。

**可覆写点**：`XCLASS_DEFINE_BEGING(XFontComboBox)` + `XCLASS_DEFINE_EXTEND_END(XFontComboBox, XComboBox)`（XFontComboBox.h:63-64）。虚表槽（XFontComboBox.c:385-388）：`EXClass_Copy`→`VXFontComboBox_copy`（深拷贝 `m_currentFont`）、`EXClass_Move`→`VXFontComboBox_move`（转移 `m_currentFont`）、`EXClass_Deinit`→`VXFontComboBox_deinit`。无 `_base` 公开入口。

**注意事项与坑点**

1. **fontFilters 仅 AllFonts 生效**：其余过滤项（可缩放/等宽）依赖字体元数据，当前仅存储、待字体数据库能力补齐后重算条目（XFontComboBox.h:6-9；XFontComboBox.c:478-481）。
2. **setWritingSystem 仅状态承载**：字体族列表不按书写系统过滤，取值须为 `XFontComboBoxWritingSystem` 子集成员，非法值忽略（XFontComboBox.c:488-510）。
3. **`setCurrentFamily` 须跳过空串条目**：`XComboBox_itemText_2` 对空 XString 条目返回 NULL（如本地化字体族名枚举产生的空串），比较前必须跳过，否则 `strcmp(NULL)` 段错误（XFontComboBox.c:620-623）。
4. **`currentFont` 宏别名以族名承载 QFont**：非 XFont 值查询；`setDisplayFont` 只写字体状态、不改条目、不发信号，与 `setCurrentFont` 互不影响（XFontComboBox.h:135-160）。
5. **分区结构错位**：头文件 `/* ==================== 信号 ==================== */` 分区注释位于 `#endif /* XWIDGET_ON && XCOMBOBOX_ON && XFONTCOMBOBOX_ON */`（XFontComboBox.h:243）之后（XFontComboBox.h:244），信号分区标记落在守卫外。

**测试覆盖**：无 `Test/XGuiTest/XFontComboBoxTest.c` 专属套件。间接覆盖（`rg -c "\bXFontComboBox\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_text.c`（8 处）、`xgui_demo_apitest_input.c`（5 处）、`xgui_window_demo.c`（3 处）等。

---

## XFrame

**定位**：框架控件（形状+阴影组合绘制样式化边框）。继承链 `XObject ← XWidget ← XFrame`。头文件 `Src/XGui/Widget/XFrame.h`（312 行）。模块开关 `XFRAME_ON`，另需 `XWIDGET_ON && XPALETTE_ON`（XFrame.h:31-34）。

**生命周期**：`XFrame_init` 先初始化 XWidget 基类再挂 XFrame 虚表，默认 `frameStyle=16`（NoFrame|Plain）、`lineWidth=1`、`midLineWidth=0`、`frameWidth=0`；栈对象须与 `XClassDeinit` 成对；堆对象须 `XClassDelete`（XFrame.h:139-160）。

**API 面**

枚举/结构（XFrame.h:56-93）：`XFrameShape`（0..6）、`XFrameShadow`（0x10/0x20/0x30）、`XFrameStyleMask`（Shape=0x0f / Shadow=0xf0）、`XFrameStyleOption`（绘制选项结构）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XFrame_class_init(void);` | 建表（继承 XWidget） | 同名惯用 | 共享表，禁释放 |
| `void XFrame_init(XFrame* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 NoFrame\|Plain/1/0） | QFrame 构造 | — |
| `#define XFrame_create(parent, flags) …` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XFrame* XFrame_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `int XFrame_frameStyle(const XFrame* self);` | 查样式组合值 | `frameStyle` qframe.h:31 | — |
| `void XFrame_setFrameStyle(XFrame* self, int style);` | 设样式组合值（含尺寸策略接管） | `setFrameStyle` qframe.h:32 | — |
| `XFrameShape XFrame_frameShape(const XFrame* self);` | 查形状掩码 | `frameShape` qframe.h:60 | — |
| `void XFrame_setFrameShape(XFrame* self, XFrameShape shape);` | 设形状（保留阴影） | `setFrameShape` qframe.h:61 | — |
| `XFrameShadow XFrame_frameShadow(const XFrame* self);` | 查阴影掩码 | `frameShadow` qframe.h:62 | — |
| `void XFrame_setFrameShadow(XFrame* self, XFrameShadow shadow);` | 设阴影（保留形状） | `setFrameShadow` qframe.h:63 | — |
| `int XFrame_lineWidth(const XFrame* self);` | 查线宽 | `lineWidth` qframe.h:65 | — |
| `void XFrame_setLineWidth(XFrame* self, int width);` | 设线宽（按 short 转换） | `setLineWidth` qframe.h:66 | — |
| `int XFrame_midLineWidth(const XFrame* self);` | 查中间线宽 | `midLineWidth` qframe.h:68 | — |
| `void XFrame_setMidLineWidth(XFrame* self, int width);` | 设中间线宽 | `setMidLineWidth` qframe.h:69 | — |
| `int XFrame_frameWidth(const XFrame* self);` | 查综合边框宽度 | `frameWidth` qframe.h:34 | — |
| `XRect XFrame_frameRect(const XFrame* self);` | 查外形矩形 | `frameRect` qframe.h:71 | 值返回 |
| `void XFrame_setFrameRect(XFrame* self, const XRect* rect);` | 设外形矩形（重写 contentsMargins） | `setFrameRect` qframe.h:72 | rect 借用，可为 NULL |
| `XSize XFrame_sizeHint(const XFrame* self);` | 尺寸提示（HLine (-1,3)/VLine (3,-1)） | `sizeHint` qframe.h:36 | 值返回 |
| `void XFrame_drawFrame(XFrame* self, XPainter* painter);` | 用给定绘制器直接绘制框架 | `drawFrame(QPainter*)` qframe.h:78 | painter 借用 |
| `void XFrame_initStyleOption(XFrame* self, XFrameStyleOption* option);` | 填充框架样式选项 | `initStyleOption` qframe.h:83 | option 输出参数，调用方提供 |

**所有权与命名契约**：`_const` 借用族=所有查询取 const self；无 `_utf8`/`_2`/三态 `set*`（`setLineWidth`/`setMidLineWidth` 为 int 单值）。XFrame 无指针成员，全为标量字段（XFrame.h:118-129）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XFrame)` + `XCLASS_DEFINE_EXTEND_END(XFrame, XWidget)`（XFrame.h:102-103）。虚表槽（XFrame.c:580-587）：`EXWidget_SizeHint`→`VXFrame_sizeHintSlot`、`EXWidget_MinimumSizeHint`→`VXFrame_minimumSizeHintSlot`、`EXObject_Event`→`VXFrame_event`、`EXWidget_PaintEvent`→`VXFrame_paintEvent`、`EXWidget_ChangeEvent`→`VXFrame_changeEvent`、`EXClass_Copy`→`VXFrame_copy`、`EXClass_Move`→`VXFrame_move`。无 `_base` 公开入口。

**注意事项与坑点**

1. **`update()` 不刷新边距带**：`XWidget_update`（XWidget.h:2005；XWidget.c:10027-10033）以 `m_contentsRect` 为脏区，而 `m_contentsRect` 已按内容边距扣减 ⇒ 含 `contentsMargins` 的 XFrame `update()` 不刷新边距带，边距带像素残留旧值（`out/xwidget_align/tree.md:80` E3）。
2. **foregroundRole 恒返回 NoRole**：Qt 的 `foregroundRole()` 永不返回 NoRole（按 `backgroundRole()` 推导，qwidget.cpp:4440-4463）；X 恒返回 NoRole（XWidget.c:9984），推导逻辑在 XFrame.c:63-70 复制实现（`out/xwidget_align/appearance.md:27`；`XGui.md` §8.1.1「承载差异」段，XGui.md:1509）。
3. **sizeHint 回落必须用非虚读取**：`XFrame_sizeHint` 回落基类须调 `XWidget_sizeHint_base`，调 `XWidget_sizeHint` 会再次分派到本类槽 → 无限递归（XFrame.c:781-783）。
4. **头/实现矛盾（虚表重载清单）**：XFrame.h:99-101 声称"重载 XClass 的 Copy/Move/Deinit 与 XWidget 的 PaintEvent/ChangeEvent"，但 `XFrame_class_init`（XFrame.c:580-587）只注册 Copy/Move，**无 `EXClass_Deinit`**，且另注册了头文件未提及的 `EXObject_Event`/`SizeHint`/`MinimumSizeHint`。
5. **HLine/VLine 接管尺寸策略**：HLine→(Minimum,Fixed,Line)、VLine→(Fixed,Minimum,Line)、其余→(Preferred,Preferred,Frame)；用户显式 `setSizePolicy` 后经 `WState_OwnSizePolicy` 属性保留用户策略（XFrame.h:20-23；XFrame.h:181-192）。

**测试覆盖**：无 `Test/XGuiTest/XFrameTest.c` 专属套件。间接覆盖（`rg -c "\bXFrame\b" Test/`）：`Test/XGuiTest/XWidgetQtAlignTest.c`（20 处）、`Test/XGuiDemo/xgui_demo_apitest_labels.c`（28 处）、`xgui_demo_page_effects.c`（5 处）等。

---

## XGraphicsBlurEffect

**定位**：模糊图形效果（对源快照做简化盒式模糊）。继承链 `XObject ← XGraphicsEffect ← XGraphicsBlurEffect`。头文件 `Src/XGui/Widget/XGraphicsBlurEffect.h`（146 行）。模块开关 `XWIDGET_ON`（XGraphicsBlurEffect.h:27）。

**生命周期**：无 parent 参数（效果对象独立）；`XGraphicsBlurEffect_init` 默认 `blurRadius=1.0`；堆对象须 `XClassDelete`（XGraphicsBlurEffect.h:69-83）。

**API 面**

枚举（XGraphicsBlurEffect.h:36-44）：`XGraphicsBlurEffectBlurHint`（Performance=0x00/Quality=0x01/Animation=0x02，位值一致 qgraphicseffect.h:125-129）、`XGraphicsBlurEffectBlurHints=int` 位或组合。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XGraphicsBlurEffect_class_init(void);` | 建表（继承 XGraphicsEffect） | 同名惯用 | 共享表，禁释放 |
| `void XGraphicsBlurEffect_init(XGraphicsBlurEffect* self);` | 初始化（blurRadius=1.0） | QGraphicsBlurEffect 构造 | — |
| `#define XGraphicsBlurEffect_create() …` | 默认内存类型建对象 | — | 调用方拥有，须 `XClassDelete` |
| `XGraphicsBlurEffect* XGraphicsBlurEffect_create_ex(XMemoryType memory);` | 指定内存类型建对象 | — | 调用方拥有，须 `XClassDelete` |
| `float XGraphicsBlurEffect_blurRadius(const XGraphicsBlurEffect* self);` | 查模糊半径 | `blurRadius` qgraphicseffect.h:138 | 值返回 |
| `void XGraphicsBlurEffect_setBlurRadius(XGraphicsBlurEffect* self, float radius);` | 设模糊半径（仅对齐保存，钳非负） | `setBlurRadius` qgraphicseffect.h:142 | — |
| `XGraphicsBlurEffectBlurHints XGraphicsBlurEffect_blurHints(const XGraphicsBlurEffect* self);` | 查模糊质量提示 | `blurHints` qgraphicseffect.h:139 | 值返回 |
| `void XGraphicsBlurEffect_setBlurHints(XGraphicsBlurEffect* self, XGraphicsBlurEffectBlurHints hints);` | 设模糊质量提示（仅存储） | `setBlurHints` qgraphicseffect.h:143 | — |
| `void* XGraphicsBlurEffect_blurRadiusChanged_signal(XGraphicsBlurEffect* self, float radius);` | 模糊半径变化信号 | `blurRadiusChanged` qgraphicseffect.h:146 | 信号标识地址 |
| `void* XGraphicsBlurEffect_blurHintsChanged_signal(XGraphicsBlurEffect* self, XGraphicsBlurEffectBlurHints hints);` | 模糊提示变化信号 | `blurHintsChanged` qgraphicseffect.h:147 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；无 `_utf8`/`_2`/三态 `set*`。成员 `m_blurRadius`/`m_blurHints` 为值承载（XGraphicsBlurEffect.h:53-61）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XGraphicsBlurEffect)` + `XCLASS_DEFINE_EXTEND_END(XGraphicsBlurEffect, XGraphicsEffect)`（XGraphicsBlurEffect.h:29-30）。虚表槽（XGraphicsBlurEffect.c:116-119）：`EXGraphicsEffect_BoundingRectFor`→`VXGraphicsBlurEffect_boundingRectFor`（源矩形四周外扩 2px）、`EXGraphicsEffect_Draw`→`VXGraphicsBlurEffect_draw`（两趟 3 点盒式均值）。无 `_base` 公开入口。

**注意事项与坑点**

1. **固定 3x3 盒式核，属性仅 API 对齐**：`blurRadius`/`blurHints` 含变化信号但不改变核尺寸（等效模糊半径约 2px），与 Qt 6 按半径持高斯/指数近似核不同（XGraphicsBlurEffect.h:6-10；`XGui.md`:1597-1598）。
2. **boundingRectFor 两趟各外扩 1px（共 2px）**：供脏区放大采样源周边像素（XGraphicsBlurEffect.c:9-11；`XGRAPHICSBLUREFFECT_PAD 2.0f`，XGraphicsBlurEffect.c:26）。
3. **效果外扩区依赖父级重绘**（`XGui.md`:1597-1598）。
4. **setBlurHints 不触发重绘**：对标 Qt（qgraphicseffect.cpp:794-802）仅发射 `blurHintsChanged`，固定核下 hints 不改变渲染（XGraphicsBlurEffect.c:178-183）。
5. **setBlurRadius 实际变化才处理**：钳非负、同值早退，变化时 `XGraphicsEffect_update` + 发射信号（XGraphicsBlurEffect.c:150-167）。

**测试覆盖**：无 `Test/XGuiTest/XGraphicsBlurEffectTest.c` 专属套件。间接覆盖（`rg -c "\bXGraphicsBlurEffect\b" Test/`）：`Test/XGuiDemo/xgui_demo_page_effects.c`（8 处）、`xgui_demo_apitest_core.c`（4 处）、`xgui_demo_apitest_labels.c`（2 处）等。

---

## XGraphicsDropShadowEffect

**定位**：投影图形效果（按源快照 alpha 生成着色投影）。继承链 `XObject ← XGraphicsEffect ← XGraphicsDropShadowEffect`。头文件 `Src/XGui/Widget/XGraphicsDropShadowEffect.h`（155 行）。模块开关 `XWIDGET_ON`（XGraphicsDropShadowEffect.h:29）。

**生命周期**：无 parent 参数；`XGraphicsDropShadowEffect_init` 默认 offset=(8,8)、blurRadius=1.0、color=QColor(63,63,63,180)；堆对象须 `XClassDelete`（XGraphicsDropShadowEffect.h:55-71）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XGraphicsDropShadowEffect_class_init(void);` | 建表（继承 XGraphicsEffect） | 同名惯用 | 共享表，禁释放 |
| `void XGraphicsDropShadowEffect_init(XGraphicsDropShadowEffect* self);` | 初始化（默认 offset/blurRadius/color） | QGraphicsDropShadowEffect 构造 | — |
| `#define XGraphicsDropShadowEffect_create() …` | 默认内存类型建对象 | — | 调用方拥有，须 `XClassDelete` |
| `XGraphicsDropShadowEffect* XGraphicsDropShadowEffect_create_ex(XMemoryType memory);` | 指定内存类型建对象 | — | 调用方拥有，须 `XClassDelete` |
| `XPointF XGraphicsDropShadowEffect_offset(const XGraphicsDropShadowEffect* self);` | 查投影偏移 | `offset` qgraphicseffect.h:173 | 值返回 |
| `void XGraphicsDropShadowEffect_setOffset(XGraphicsDropShadowEffect* self, XPointF offset);` | 设投影偏移 | `setOffset` qgraphicseffect.h:185 | 值入参 |
| `float XGraphicsDropShadowEffect_blurRadius(const XGraphicsDropShadowEffect* self);` | 查模糊半径 | `blurRadius` qgraphicseffect.h:181 | 值返回 |
| `void XGraphicsDropShadowEffect_setBlurRadius(XGraphicsDropShadowEffect* self, float radius);` | 设模糊半径（仅对齐保存，钳非负） | `setBlurRadius` qgraphicseffect.h:199 | — |
| `XColor XGraphicsDropShadowEffect_color(const XGraphicsDropShadowEffect* self);` | 查投影颜色 | `color` qgraphicseffect.h:182 | 值拷贝 |
| `void XGraphicsDropShadowEffect_setColor(XGraphicsDropShadowEffect* self, XColor color);` | 设投影颜色 | `setColor` qgraphicseffect.h:200 | 值拷贝 |
| `void* XGraphicsDropShadowEffect_offsetChanged_signal(XGraphicsDropShadowEffect* self, XPointF offset);` | 偏移变化信号 | `offsetChanged` qgraphicseffect.h:203 | 信号标识地址 |
| `void* XGraphicsDropShadowEffect_blurRadiusChanged_signal(XGraphicsDropShadowEffect* self, float radius);` | 模糊半径变化信号 | `blurRadiusChanged` qgraphicseffect.h:204 | 信号标识地址 |
| `void* XGraphicsDropShadowEffect_colorChanged_signal(XGraphicsDropShadowEffect* self, XColor color);` | 颜色变化信号 | `colorChanged` qgraphicseffect.h:205 | 信号标识地址 |

**所有权与命名契约**：`_const` 借用族=查询取 const self；无 `_utf8`/`_2`/三态 `set*`。成员 `m_offset`（XPointF 值）、`m_blurRadius`（float 值）、`m_color`（XColor 值）均值承载（XGraphicsDropShadowEffect.h:38-46）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XGraphicsDropShadowEffect)` + `XCLASS_DEFINE_EXTEND_END(XGraphicsDropShadowEffect, XGraphicsEffect)`（XGraphicsDropShadowEffect.h:31-32）。虚表槽（XGraphicsDropShadowEffect.c:250-253）：`EXGraphicsEffect_BoundingRectFor`→`VXGraphicsDropShadowEffect_boundingRectFor`（源矩形按模糊扩散 2px+偏移外沿生长）、`EXGraphicsEffect_Draw`→`VXGraphicsDropShadowEffect_draw`（先画模糊着色投影、再叠源）。无 `_base` 公开入口。

**注意事项与坑点**

1. **投影层须用 Source 合成（真缺陷根修）**：预乘 SourceOver 紧致混合对全透明画布强制 `alpha=0xFF`，会把半透明投影渲染成不透明深灰硬块；改用 Source 合成按行直拷预乘值保住半透明 alpha（`XGui.md`:299-306；XGraphicsDropShadowEffect.c:222-231）。
2. **boundingRectFor 外扩方向须随 offset 符号**：offset.x>0 时外沿量加在 `padR`/`padB` 一侧，否则右/下方向投影被画布截断（像素探针实测源贴右缘时投影 8px 外沿整段丢失）（XGraphicsDropShadowEffect.c:100-107；`XGui.md`:299-306）。
3. **固定 3x3 盒式核，blurRadius 仅 API 对齐**：与 Qt 6 按半径持高斯/指数近似核不同（XGraphicsDropShadowEffect.h:8-11；`XGui.md`:1597-1598）。
4. **painter 侧偏差未根治（登记新偏差）**：`XPainter.c` 预乘 SourceOver 紧致混合对**透明目标**强制 `alpha=0xFF`，任何"半透明预乘源→透明目标"绘制会复现失真；DropShadow 已用 Source 模式规避（`XGui.md`:305-310）。
5. **默认值对标 Qt 6.8**：offset=(8,8)、blurRadius=1.0、color=QColor(63,63,63,180)（XGraphicsDropShadowEffect.h:9-11）。

**测试覆盖**：无 `Test/XGuiTest/XGraphicsDropShadowEffectTest.c` 专属套件。间接覆盖（`rg -c "\bXGraphicsDropShadowEffect\b" Test/`）：`Test/XGuiDemo/xgui_demo_page_effects.c`（8 处）、`xgui_demo_apitest_core.c`（4 处）、`xgui_demo_apitest_labels.c`（2 处）等。运行时目验：§8.0c 探针 32/32 断言（`XGui.md`:292-306，含 DropShadow 两笔真缺陷修复）。
# 控件文档片段 · 批次 4（9 类）

> 依据：逐行打开 `Src/XGui/Widget/*.h` 与对应 `.c`；Qt 行号查 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/`（路径见各条）；坑点来源限于源码注释、`XGui.md` §8、`out/xwidget_align/` 矩阵与本次实测的代码事实。

## XGraphicsEffect

**定位**：图形效果对象基类，承载启用开关、效果源借用指针与三个效果虚槽；不直接绘制控件，绘制由 `XGraphicsEffect_drawWidget` 离屏管线完成。继承链 XObject←XGraphicsEffect。头文件 `Src/XGui/Widget/XGraphicsEffect.h`（283 行）。

**生命周期**：`XGraphicsEffect_init` 清零后挂 `XGraphicsEffect` 虚表并置 `m_enabled=true`（`XGraphicsEffect.c:143-152`）；`XGraphicsEffect_create()` 宏转 `XGraphicsEffect_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)`（`.h:129`），`create_ex` 内先 `init` 再置 `Set_Class_Memory/IsHeap=true`（`.c:154-164`）；效果实例由 `XWidget_setGraphicsEffect` 接管并析构（`.c:8-10` 文件头注释 + `XWidget.c:13967-14001` 实现），自身无平台资源、无独立 `_delete`。

**API 面**

生命周期与启用：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XGraphicsEffect_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用（共享表） |
| `void XGraphicsEffect_init(XGraphicsEffect* self)` | 就地初始化（默认启用） | `QGraphicsEffect(QObject*)` qgraphicseffect.h:46 | 无参数所有权 |
| `#define XGraphicsEffect_create()` | 默认内存类型创建 | 同上 | 返回堆对象，调用方释放（或移交 `XWidget_setGraphicsEffect`） |
| `XGraphicsEffect* XGraphicsEffect_create_ex(XMemoryType memory)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `bool XGraphicsEffect_isEnabled(const XGraphicsEffect* self)` | 查询启用；NULL 返回 false | `isEnabled` qgraphicseffect.h:52 | 借用（无返回对象） |
| `void XGraphicsEffect_setEnabled(XGraphicsEffect* self, bool enable)` | 状态变化时发 `enabledChanged` 再 `update()` 标脏 | `setEnabled` qgraphicseffect.h:55 | 无 |
| `void XGraphicsEffect_update(XGraphicsEffect* self)` | 把效果包围盒经父控件标脏；无源无操作 | `update` qgraphicseffect.h:56 | 无 |
| `void* XGraphicsEffect_enabledChanged_signal(XGraphicsEffect* self, bool enabled)` | 已连接时真发射（`XGraphicsEffect.c:475-481`） | `enabledChanged` qgraphicseffect.h:59 | 返回信号令牌，不得解引用/释放 |

效果源与包围盒：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XWidget* XGraphicsEffect_source(const XGraphicsEffect* self)` | 取效果源控件；无源 NULL | `source()` 为内部 `QGraphicsEffectSource*`（qgraphicseffect.h:84），此处以控件承载 | 借用禁释放 |
| `void XGraphicsEffect_setSource(XGraphicsEffect* self, XWidget* source)` | 仅由 `XWidget_setGraphicsEffect` 挂接/摘除时调用 | X 扩展（`sourceChanged` 的内部配套） | 借用，不转移 |
| `void XGraphicsEffect_sourceChanged(XGraphicsEffect* self, int flags)` | 经 `SourceChanged` 槽分派；基类默认转 `update()`（`.c:88-92`） | `sourceChanged` qgraphicseffect.h:64 | 无 |
| `XRectF XGraphicsEffect_boundingRectFor(const XGraphicsEffect* self, const XRectF* sourceRect)` | 经 `BoundingRectFor` 槽分派；基类原样返回（`.c:95-108`） | `boundingRectFor` qgraphicseffect.h:49 | 借用入参，值返回 |
| `XRectF XGraphicsEffect_boundingRect(const XGraphicsEffect* self)` | 有源时以源控件几何求包围盒；无源空矩形（`.c:242-258`） | `boundingRect` qgraphicseffect.h:50 | 值返回 |

绘制管线（X 适配接口）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XGraphicsEffect_drawWidget(XGraphicsEffect* self, XWidget* widget, XImage* sourceImage, const XRegion* paintRegion)` | 脏区∩控件区→按 `boundingRectFor` 外扩→透明画布 `Draw` 槽→回贴；失败 false 回退常规路径（`.c:337-`） | 对标 `drawWidget` 的 `effect->draw(&p)` 分支（Qt 无同名公共 API） | 入参全借用；成功即已绘制、无返回对象 |
| `void XGraphicsEffect_blurBox3(XImage* image, const XRect* rect)` | 原地 3x3 盒式模糊，供 Blur/DropShadow 复用 | X 扩展（Qt `blurRadius` 语义，本项目固定核） | image 借用并原地修改 |

虚槽签名 typedef（供派生类赋值）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `typedef void (*XGraphicsEffect_DrawSlot)(XGraphicsEffect* self, XGraphicsEffectDrawContext* ctx)` | `Draw` 槽函数类型（`.h:108-109`） | `virtual void draw(QPainter*)` qgraphicseffect.h:63 | 参数借用 |
| `typedef XRectF (*XGraphicsEffect_BoundingRectForSlot)(const XGraphicsEffect* self, const XRectF* sourceRect)` | `BoundingRectFor` 槽函数类型（`.h:111-112`） | `boundingRectFor` qgraphicseffect.h:49 | 参数借用、值返回 |
| `typedef void (*XGraphicsEffect_SourceChangedSlot)(XGraphicsEffect* self, int flags)` | `SourceChanged` 槽函数类型（`.h:114-115`） | `sourceChanged` qgraphicseffect.h:64 | 参数借用 |

**所有权与命名契约**：本类所有 getter 均返回值或借用指针，唯一返回堆对象的是 `XGraphicsEffect_create_ex`（须 `XClassDelete`）；无 `_const`/`_utf8`/`_2`/三态 `set*` 家族。`XGraphicsEffect_setSource` 名为 set 实为借用登记（`.h:186-195`），不适用 `set*` 三态契约。

**可覆写点**：三个新槽由 `XCLASS_DEFINE_ENUM` 定义（`.h:43-53`；`BEGING` 在 `.h:43`、`END` 在 `.h:51`）：`SourceChanged`（默认转 `update()`，`.c:88-92`）、`BoundingRectFor`（默认原样返回，`.c:95-108`）、`Draw`（默认把源快照原样搬入输出画布，`.c:112-127`）。本类无 `_base` 入口（`rg 'XGraphicsEffect_[A-Za-z]+_base\('` 于 `.h` 0 命中）。派生示例：`XGraphicsOpacityEffect` 以 `XCLASS_DEFINE_EXTEND_END` 继承并只覆写 `Draw`（`XGraphicsOpacityEffect.c:71`）。

**注意事项与坑点**

1. **槽位编号必须锚定父类槽总数**：`.h:44-46` 明写「首槽必须锚定父类槽位总数，否则从 0 起编号会覆写根基类槽（EXClass_Deinit 等）」；`.h:51-53` 又警示「不可用 EXTEND_END——它会把 END_SIZE 重置回父类槽位数，容量被砍导致 OVERLOAD 索引越界」。新增槽的派生类必须用 `XCLASS_DEFINE_END`。
2. **`enabledChanged` 与重绘顺序与 Qt 相反**：本实现先发射 `enabledChanged` 再 `update()`（`.h:145-147`、`.c:170-179`）；Qt 是 `effectBoundingRectChanged()/invalidateCache()` 后 `emit enabledChanged(enable)`（qgraphicseffect.cpp:433-445）。依赖"收到信号时源已重绘"的槽会看到不同时序。
3. **`boundingRect` 的坐标系是父级相对**：实现直接取 `XWidget_x/y` 构造源矩形（`.c:252-257`），而 Qt 走 `d->source->boundingRect()`（qgraphicseffect.cpp:392-398）；`XGraphicsEffect_update` 因此必须经父控件标脏，注释给出残影链（`.c:187-193`），顶层源无父时退化为源本地坐标（`.c:202-206`）。
4. **基类 `Draw` 有默认实现、类可实例化**：Qt `draw` 为纯虚（qgraphicseffect.h:63 `= 0`），X 侧提供了"原样输出源"的默认实现（`.c:110-127`）——直接 `XGraphicsEffect_create()` 挂接不会崩溃，但也不会产生任何视觉效果。
5. **`XGraphicsEffectChange_ContentsChanged/GeometryChanged` 是同一别名**：二者都等于 `SourceAttached`（`.h:69-74`；注释解说起于 `.h:65-68`），挂接时位或后只单发一次 `SourceAttached`；按位分派的派生效果不会收到 `ContentsChanged`/`GeometryChanged` 这两个独立位。同处注释引用的挂接行号 `XWidget.c:9128` 已过期，实际定义在 `XWidget.c:13967`。
6. **头文件自述"简化"**：`XGui.md:1597-1598` 登记 blur 固定 3x3 核（`blurRadius` 仅 API 对齐）、效果外扩区依赖父级重绘；`XGui.md:292-300`（§8.0c）记录 Opacity/Blur/DropShadow 的像素级目验（探针 32/32）。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XGraphicsEffectTest.c`；`ls Test/XGuiTest/ | grep -iE '^(XLabel|XHeaderView|XInputDialog|XItemDelegate|XItemSelectionModel|XKeySequenceEdit|XGraphics)'` 返回空、rc=1）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_core.c:1007-1056` 共 10 条 `XAPI_EXPECT`（默认启用/信号/无源包围盒/`boundingRectFor` 原样返回/NULL 防护，入口 `xapi_core_run`，`:94`）；`Test/XGuiTest/XWidgetQtAlignTest.c:1413-1436` 4 条 `qta_expect`（`XWidget_graphicsEffect`/`setGraphicsEffect` 所有权契约）；演示页 `Test/XGuiDemo/xgui_demo_page_effects.c`。

## XGraphicsOpacityEffect

**定位**：不透明度效果，把源快照按 `m_opacity` 绘入输出画布。继承链 XObject←XGraphicsEffect←XGraphicsOpacityEffect。头文件 `Src/XGui/Widget/XGraphicsOpacityEffect.h`（101 行）。

**生命周期**：`XGraphicsOpacityEffect_init` 先 `XGraphicsEffect_init(&self->m_base)`、挂本类虚表，再置 `m_opacity = 0.7f`（`.c:75-87`）；`create()` 宏转 `create_ex`（`.h:59-60`）；析构沿用 `XGraphicsEffect`（无自有资源，`.c` 无 deinit 槽）；随挂接控件析构。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XGraphicsOpacityEffect_class_init(void)` | 创建虚表并覆写 `Draw` | 无（C 适配） | 借用（共享表） |
| `void XGraphicsOpacityEffect_init(XGraphicsOpacityEffect* self)` | 初始化，opacity 默认 0.7 | `QGraphicsOpacityEffect(QObject*)` qgraphicseffect.h:222 | 无 |
| `#define XGraphicsOpacityEffect_create()` | 默认内存类型创建 | 同上 | 堆对象须释放（或移交控件） |
| `XGraphicsOpacityEffect* XGraphicsOpacityEffect_create_ex(XMemoryType memory)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `float XGraphicsOpacityEffect_opacity(const XGraphicsOpacityEffect* self)` | 查询 0~1；NULL 返回 1.0 | `opacity` qgraphicseffect.h:225 | 借用（值） |
| `void XGraphicsOpacityEffect_setOpacity(XGraphicsOpacityEffect* self, float opacity)` | 钳制 0~1；变化时先 `update()` 再发 `opacityChanged`（`.c:105-119`） | `setOpacity` qgraphicseffect.h:229 | 无 |
| `void* XGraphicsOpacityEffect_opacityChanged_signal(XGraphicsOpacityEffect* self, float opacity)` | 信号句柄（本函数自身不发，发射在 `.c:117-118`） | `opacityChanged` qgraphicseffect.h:233 | 信号令牌，不得释放 |

**所有权与命名契约**：无 `_const`/`_utf8`/`_2`/三态 `set*`；`setOpacity` 为纯值 setter。Qt 的 `setOpacityMask`/`opacityMask`/`opacityMaskChanged`（qgraphicseffect.h:226/230/234）未建模（`.h:10-14`：缺笔刷/掩码抽象）。

**可覆写点**：不新增槽（`.h:33-34` 用 `XCLASS_DEFINE_EXTEND_END`），仅覆写继承的 `Draw` 槽（`.c:71`），默认行为 = 在透明画布上按 `m_opacity` 绘制源（`.c:41-61`）。无 `_base` 入口。

**注意事项与坑点**

1. **缺省值 0.7 不是 1.0**：`.h:8-9` 与 `.c:83-86` 明写对标 `qgraphicseffect_p.h:183` 的 `opacity(qreal(0.7))`；旧实现 1.0 被注释判为"挂接无视觉效果、语义失真"。使用方若假定"新效果不改变画面"会得到 30% 透明。
2. **NULL 兜底值 1.0 与缺省 0.7 不一致**：getter 对 NULL 返回 1.0（`.h:71`、`.c:102-103`），而正常初始化对象是 0.7——不能把 NULL 兜底值当作"效果默认值"。
3. **信号发射顺序与 Qt 一致（update 后 emit）**：`.c:113-118`；Qt `setOpacity` 同样 `update(); emit opacityChanged(opacity);`（qgraphicseffect.cpp:1099-1100）。注意 `opacityMask` 相关信号在 X 侧完全缺失。
4. **`opacityChanged` 句柄自身不发信号**：`XGraphicsOpacityEffect_opacityChanged_signal` 只返回函数地址（`.c:123-128`），与 `XInputDialog`/`XHeaderView` 那类"句柄即发射"的实现风格不同；手动"触发信号供测试"必须走 `setOpacity`。
5. **效果外扩区依赖父级重绘**：见 `XGui.md:1597-1598`（与基类共用登记）。

**测试覆盖**：无专属套件。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_core.c:1060-1095` 共 10 条 `XAPI_EXPECT`（缺省 0.7、0.3 设置、-1→0、2.0→1，入口 `xapi_core_run` `:94`）；`Test/XGuiDemo/xgui_demo_apitest_labels.c` 6 处引用、`Test/XGuiDemo/xgui_demo_page_effects.c` 13 处；`XGui.md:1896` 记录"OpacityEffect 三断言补连缺失的 opacityChanged 信号"为存量失败根因修复。

## XGroupBox

**定位**：带可选标题与可选勾选框的分组容器，1px 凹陷边框环绕，子控件排在 `contentsRect` 内。继承链 XObject←XWidget←XGroupBox。头文件 `Src/XGui/Widget/XGroupBox.h`（270 行）。模块开关 `XGROUPBOX_ON`（`.h:12-14`）。

**生命周期**：`XGroupBox_init` 先 `XWidget_init`、建 `m_title` 空串、置左对齐/非 flat/非 checkable，开 `XWidgetAttribute_Hover` 并刷新最小尺寸提示（`.c:552-574`）；`XGroupBox_create(parent, flags)` 宏（`.h:93`）；析构由虚表 `EXClass_Deinit`→`VXGroupBox_deinit`（`.c:486`、`.c:514`）释放 `m_title` 后交父类；无独立 `_delete` 声明（`.h:103-104` 仅剩两行悬空 `@brief`，见坑点 6）。

**API 面**

生命周期：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XGroupBox_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用 |
| `void XGroupBox_init(XGroupBox* self, XWidget* parent, XWidgetFlags flags)` | 初始化并设默认值 | `QGroupBox(QWidget*)` qgroupbox.h:26 | parent 借用 |
| `#define XGroupBox_create(parent, flags)` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `XGroupBox* XGroupBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |

标题与样式：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `const char* XGroupBox_title(const XGroupBox* self)` | 标题 UTF-8 借用视图；NULL 返回 `""` | `title()` qgroupbox.h:30 | 借用禁释放 |
| `void XGroupBox_setTitle(XGroupBox* self, const char* title)` | 赋值后刷 sizeHint + `updateGeometry` + 重绘（`.c:593-606`） | `setTitle` qgroupbox.h:31 | 内部另存，入参不转移 |
| `int XGroupBox_alignment(const XGroupBox* self)` | 标题水平对齐；NULL 返回 Left | `alignment()` qgroupbox.h:33 | 借用（值） |
| `void XGroupBox_setAlignment(XGroupBox* self, int alignment)` | 仅保留 Left/HCenter/Right，非法回退 Left（`.c:613-622`） | `setAlignment` qgroupbox.h:34 | 无 |
| `bool XGroupBox_isFlat(const XGroupBox* self)` | 扁平样式查询 | `isFlat` qgroupbox.h:38 | 借用（值） |
| `void XGroupBox_setFlat(XGroupBox* self, bool flat)` | 置位并重绘（`.c:629-634`） | `setFlat` qgroupbox.h:39 | 无 |
| `XRect XGroupBox_contentsRect(const XGroupBox* self)` | 内容区（去标题区与边框，负值钳 0，`.c:636-651`） | `contentsRect()` 走 `QWidget`（qwidget.h:534），Qt 用 `SC_GroupBoxContents` 换算内容边距（qgroupbox.cpp:434-436） | 值返回 |

勾选状态：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XGroupBox_isCheckable(const XGroupBox* self)` | 是否可勾选 | `isCheckable` qgroupbox.h:40 | 借用（值） |
| `void XGroupBox_setCheckable(XGroupBox* self, bool checkable)` | true 时初始勾选并加 StrongFocus；false 时清勾选、发 `toggled(false)`、恢复子控件（`.c:660-692`） | `setCheckable` qgroupbox.h:41 | 无 |
| `bool XGroupBox_isChecked(const XGroupBox* self)` | `checkable && checked`（`.c:694-698`） | `isChecked` qgroupbox.h:42 | 借用（值） |
| `void XGroupBox_setChecked(XGroupBox* self, bool checked)` | 仅 checkable 生效；变化时同步子控件可用性并发 `toggled`（`.c:700-710`） | `setChecked` qgroupbox.h:45 | 无 |

信号：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XGroupBox_clicked_signal(XGroupBox* self, bool checked)` | 已连接时发射；NULL 仅返回令牌（`.c:713-719`） | `clicked(bool)` qgroupbox.h:48 | 信号令牌，不得解引用 |
| `void* XGroupBox_toggled_signal(XGroupBox* self, bool checked)` | 已连接时发射；NULL 仅返回令牌（`.c:721-727`） | `toggled(bool)` qgroupbox.h:49 | 信号令牌，不得解引用 |

父类转发宏（`.h:108-137`，共 30 个）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XGroupBox_setEnabled(self, enabled)` / `XGroupBox_isEnabled(self)` | 转发 `XWidget_setEnabled/isEnabled` | `setEnabled` qwidget.h:242 / `isEnabled` qwidget.h:238 | 无 |
| `XGroupBox_setVisible(self, visible)` / `XGroupBox_isVisible(self)` / `XGroupBox_show(self)` / `XGroupBox_hide(self)` | 可见性转发 | qwidget.h:482/507/484/485 | 无 |
| `XGroupBox_raise(self)` / `XGroupBox_lower(self)` | 层序转发 | qwidget.h:493/494 | 无 |
| `XGroupBox_setGeometry(self, x, y, w, h)` / `XGroupBox_setGeometryRect(self, rect)` | 几何转发 | qwidget.h:502/503 | 无 |
| `XGroupBox_x(self)` / `XGroupBox_y(self)` | 位置读取转发 | qwidget.h:253/254 | 无 |
| `XGroupBox_width(self)` / `XGroupBox_height(self)` | 尺寸读取转发 | qwidget.h:258/259 | 无 |
| `XGroupBox_resize(self, w, h)` / `XGroupBox_move(self, x, y)` | 尺寸/位置转发 | qwidget.h:500/498 | 无 |
| `XGroupBox_update(self)` / `XGroupBox_updateRect(self, rect)` | 重绘转发 | qwidget.h:467/472 | 无 |
| `XGroupBox_setParent(self, parent, flags)` / `XGroupBox_parentWidget(self)` | 父控件转发 | qwidget.h:542/615 | parent 借用 |
| `XGroupBox_setFocus(self)` / `XGroupBox_clearFocus(self)` / `XGroupBox_hasFocus(self)` / `XGroupBox_setFocusPolicy(self, policy)` | 焦点转发 | qwidget.h:423/428/433/432 | 无 |
| `XGroupBox_setAttribute(self, attribute, on)` / `XGroupBox_testAttribute(self, attribute)` | 属性转发 | qwidget.h:629/630 | 无 |
| `XGroupBox_setContentsMargins(self, l, t, r, b)` / `XGroupBox_contentsMargins(self)` | 边距转发 | qwidget.h:530/532 | 无 |
| `XGroupBox_setWindowFlags(self, flags)` / `XGroupBox_updateGeometry(self)` | 窗口标志/几何刷新转发 | qwidget.h:617/539 | 无 |

**所有权与命名契约**：`XGroupBox_title` 是 `const char*` 借用视图（符合 `_toUtf8` 语义但未用该后缀）；`XGroupBox_setTitle(const char*)` 未用 `_utf8` 后缀——按风格文档《字符串 API 后缀契约》属待改名族（`XGui.md:1813` 已登记全库 213 个同类 `_2` 函数）；无 `_const` 拷贝版 `XGroupBox_title_const`，无三态 `set*`。

**可覆写点**：不新增槽（`.h:40-41` `XCLASS_DEFINE_EXTEND_END`）；覆写继承槽（`XGroupBox.c:501-514`）：`EXObject_Event`（`VXGroupBox_event`）、`EXWidget_PaintEvent`（平移裁剪后 `XGroupBox_drawControl`）、`EXWidget_ResizeEvent`、`EXWidget_FocusInEvent`、`EXWidget_ChangeEvent`（`EnabledChange` 时同步子控件可用性，`.c:347-360`）、`EXWidget_MousePressEvent`（仅记按压态，`.c:363-385`）、`EXWidget_MouseMoveEvent`（直接交父类，`.c:388-392`）、`EXWidget_MouseReleaseEvent`（标题区释放才切换，`.c:396-422`）、`EXClass_Copy/Move/Deinit`。无 `_base` 入口。

**注意事项与坑点**

1. **`clicked` 的发射时机注释与实现相反**：`.h:243-244` 写「第一版在标题区鼠标按下完成切换后立即发射（Qt 为按下+释放完整点击语义，后续扩展）」，而实现只在释放且按下起点在标题区时切换并发射（`.c:396-418`，`m_pressed` 于按下时置位、释放时判定）。实际行为已是按下+释放语义。
2. **`setCheckable(false)` 的清态/信号与注释相反**：`.h:212-213` 写「false 时…不清除已设置的 checked 状态，也不发射 toggled」，实现则「若此前已勾选则取消选中并补发 `toggled(false)`」（`.c:675-688`），且注释自陈这是本库口径（Qt 6.8.3 `setCheckable` else 支仅置 NoFocus、恢复子控件，qgroupbox.cpp:540-547）。
3. **标题"63 字符截断"已不成立**：`.h:151` 写「超长截断到内部缓冲上限（63 字符 + NUL）」，但 `setTitle` 全量存入 `XString`（`.c:593-606`），只有绘制栈缓冲 `char text[64]`（`.c:151`、`.c:204` `XSnprintf`）会截断可见文本；`Test/XGuiTest/XGroupBoxTest.c:54` 断言 99 字符标题不截断。
4. **勾选命中区是整个标题行，不是勾选框+标题**：`.h:16-18` 明写 Qt 响应区为 `SC_GroupBoxCheckBox|Label`、本实现命中整个标题区（`xgroupbox_titleRowHit` 仅判 `y < titleHeight`，`.c:95-98`），行为近似而非等价。
5. **`contentsRect` 不扣 `contentsMargins`**：实现用 `rect ±1` 减去标题高度（`.c:642-649`），Qt 由 `QStyle::SC_GroupBoxContents` 算出后 `setContentsMargins`（qgroupbox.cpp:434-436）；配 `XGroupBox_setContentsMargins` 后两者结果不同。
6. **两行悬空的 `@brief`**：`.h:103-104` 只有两行注释、无对应声明（`XGroupBox_delete`/`XGroupBox_deinit` 在 `.h` 中不存在，`rg` 仅命中 `.c:486`、`.c:514` 的内部静态函数），文档生成器会产出无主条目。
7. **样式接管后自绘分支不可达**：`XSTYLE_ON` 且 `XStyle_defaultStyle()!=NULL` 时整框走 `CC_GroupBox`（`.c:163-188`），自绘的勾选框/标题遮挡逻辑只作为回退；`XGui.md:2700-2701` 登记 QSS 后置填充会"抹标题与勾选框、整框描边横穿标题区"的能力边界。

**测试覆盖**：`Test/XGuiTest/XGroupBoxTest.c` 存在（197 行）。断言数：`grep -c 'gb_expect('` = 35，其中第 9 行为 `gb_expect` 函数定义本体，实际断言调用点 34 处。入口 `bool XGroupBoxTest_runAll(void)`（`.c:33`，声明 `XGroupBoxTest.h:15`），注册于 `Test/CMakeLists.txt:557`，由 `xgui_regression_test.c:36224` 以 `expect_true(XGroupBoxTest_runAll(), "XGroupBox 控件功能")` 调用。覆盖：默认值、标题与 99 字符长标题、对齐三档+非法回退、flat、`contentsRect` 有/无标题两态、checkable 初始勾选、非 checkable 时 `setChecked` 无效、子/孙控件递归禁用与恢复、标题区点击 `clicked`+`toggled` 计数。另有 `Test/XGuiDemo/xgui_demo_apitest_labels.c:487-` 的 `XAPI_EXPECT` 组（该文件 182 条断言，入口 `xapi_labels_run` `:1402`）。

## XHeaderView

**定位**：管理水平（列）/垂直（行）表头区间的尺寸与几何（count/sectionSize/sectionPosition），表头文本由关联模型提供、渲染由 XTableView 统一完成，本类不自绘。继承链 XObject←XWidget←XHeaderView。头文件 `Src/XGui/Widget/XHeaderView.h`（789 行）。模块开关 `XWIDGET_ON && XTABLEWIDGET_ON`（`.h:32`）。

**生命周期**：`XHeaderView_init` 先 `XWidget_init`，建三条平行 XVector（尺寸/单段模式/隐藏）并置默认：`m_defaultSize=30`、`min=20`、`max=1048575`、全局模式 Interactive、`firstSectionMovable=true`、水平头 Center/垂直头 Left|VCenter（`.c:529-568`；常量 `.c:25/27/29`）；`create(parent, flags, orientation)` 宏（`.h:117-119`）；析构 `VXHeaderView_deinit` 释放三表并清旁表键（`.c:360-381`）。

**API 面**

生命周期与几何：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XHeaderView_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用 |
| `void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags, int orientation)` | 初始化（orientation 钳 0/1） | `QHeaderView(Qt::Orientation, QWidget*)` qheaderview.h:48 | parent 借用 |
| `XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags, int orientation)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XHeaderView_create(parent, flags, orientation)` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `int XHeaderView_orientation(const XHeaderView* self)` | 0=水平 1=垂直 | `orientation()` qheaderview.h:53 | 借用（值） |
| `int XHeaderView_count(const XHeaderView* self)` | 区间数 | `count()` qheaderview.h:82 | 借用（值） |
| `void XHeaderView_setCount(XHeaderView* self, int count)` | 增用默认尺寸、缩编尾部；变化时发 `sectionCountChanged`+`geometriesChanged`（`.c:587-627`） | X 扩展（Qt 由模型驱动增删） | 无 |
| `int XHeaderView_sectionSize(const XHeaderView* self, int section)` | 区间尺寸；越界返回默认尺寸 | `sectionSize` qheaderview.h:66 | 借用（值） |
| `void XHeaderView_resizeSection(XHeaderView* self, int logicalIndex, int size)` | 先按 min/max 钳制再转 `setSectionSize`（`.c:637-649`） | `resizeSection` qheaderview.h:72 | 无 |
| `void XHeaderView_setSectionSize(XHeaderView* self, int section, int size)` | 不钳制，仅越界/非正值防护；变化时发 `sectionResized`+`geometriesChanged`（`.c:1443-1460`） | **X 扩展**（Qt 6.8 无 `setSectionSize`，`grep -rn 'setSectionSize' widgets/` 0 命中） | 无 |
| `int XHeaderView_defaultSectionSize(const XHeaderView* self)` | 默认段尺寸 | `defaultSectionSize` qheaderview.h:122 | 借用（值） |
| `void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size)` | 仅 `>0` 生效（`.c:1463-1466`） | `setDefaultSectionSize` qheaderview.h:123 | 无 |
| `void XHeaderView_resetDefaultSectionSize(XHeaderView* self)` | 复位为常量 30，不动既有段 | `resetDefaultSectionSize` qheaderview.h:124 | 无 |
| `int XHeaderView_sectionPosition(const XHeaderView* self, int section)` | 逐段累加求起点；越界 -1 | `sectionPosition` qheaderview.h:67 | 借用（值） |
| `int XHeaderView_sectionViewportPosition(const XHeaderView* self, int section)` | 恒等于 `sectionPosition`（offset 未接线） | `sectionViewportPosition` qheaderview.h:68 | 借用（值） |
| `void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch)` | 仅存状态（`.c:1493-1496`） | `setStretchLastSection` qheaderview.h:117 | 无 |
| `bool XHeaderView_stretchLastSection(const XHeaderView* self)` | 查询（`.c:1501-1505`） | `stretchLastSection` qheaderview.h:116 | 借用（值） |

段管理：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XHeaderView_setSectionHidden(XHeaderView* self, int section, bool hide)` | 转发 hide/show（与 Qt 原始入口方向相反，`.c:698-704`） | `setSectionHidden` qheaderview.h:76 | 无 |
| `void XHeaderView_hideSection(XHeaderView* self, int section)` | 隐藏并（变化时）发 `geometriesChanged` | `hideSection` qheaderview.h:79 | 无 |
| `void XHeaderView_showSection(XHeaderView* self, int section)` | 显示并（变化时）发 `geometriesChanged` | `showSection` qheaderview.h:80 | 无 |
| `bool XHeaderView_isSectionHidden(const XHeaderView* self, int section)` | 越界 false | `isSectionHidden` qheaderview.h:75 | 借用（值） |
| `int XHeaderView_hiddenSectionCount(const XHeaderView* self)` | 隐藏数 | `hiddenSectionCount` qheaderview.h:77 | 借用（值） |
| `void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable)` | 仅存状态 | `setSectionsClickable` qheaderview.h:91 | 无 |
| `bool XHeaderView_sectionsClickable(const XHeaderView* self)` | 查询 | `sectionsClickable` qheaderview.h:92 | 借用（值） |
| `void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable)` | 仅存状态 | `setSectionsMovable` qheaderview.h:86 | 无 |
| `bool XHeaderView_sectionsMovable(const XHeaderView* self)` | 查询 | `sectionsMovable` qheaderview.h:87 | 借用（值） |
| `void XHeaderView_moveSection(XHeaderView* self, int from, int to)` | 重排尺寸表并发一次 `sectionMoved`+`geometriesChanged`（`.c:802-805`） | `moveSection` qheaderview.h:70 | 无 |
| `void XHeaderView_swapSections(XHeaderView* self, int first, int second)` | 交换并发两次 `sectionMoved`+一次 `geometriesChanged`（`.c:745-750`） | `swapSections` qheaderview.h:71 | 无 |
| `void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order)` | 变化时发 `sortIndicatorChanged` 并置 shown（`.c:807-819`） | `setSortIndicator` qheaderview.h:109 | 无 |
| `int XHeaderView_sortIndicatorSection(const XHeaderView* self)` | -1=无 | `sortIndicatorSection` qheaderview.h:110 | 借用（值） |
| `int XHeaderView_sortIndicatorOrder(const XHeaderView* self)` | 0 升/1 降 | `sortIndicatorOrder` qheaderview.h:111 | 借用（值） |
| `void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown)` | 仅存状态 | `setSortIndicatorShown` qheaderview.h:106 | 无 |
| `bool XHeaderView_isSortIndicatorShown(const XHeaderView* self)` | 查询 | `isSortIndicatorShown` qheaderview.h:107 | 借用（值） |
| `void XHeaderView_setSortIndicatorClearable(XHeaderView* self, bool clearable)` | 变化时发 `sortIndicatorClearableChanged`（`.c:834-841`） | `setSortIndicatorClearable` qheaderview.h:113 | 无 |
| `bool XHeaderView_isSortIndicatorClearable(const XHeaderView* self)` | 缺省 false | `isSortIndicatorClearable` qheaderview.h:114 | 借用（值） |

尺寸上下限/模式与状态：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XHeaderView_setMaximumSectionSize(XHeaderView* self, int size)` | -1 重置硬上限；落库并把越限段钳制（`.c:847-871`） | `setMaximumSectionSize` qheaderview.h:129 | 无 |
| `int XHeaderView_maximumSectionSize(const XHeaderView* self)` | 缺省 1048575 | `maximumSectionSize` qheaderview.h:128 | 借用（值） |
| `void XHeaderView_setMinimumSectionSize(XHeaderView* self, int size)` | 抬升不足下限的段（**.c:876-891 未写回 `m_minimumSectionSize`**，见坑点 2） | `setMinimumSectionSize` qheaderview.h:127 | 无 |
| `int XHeaderView_minimumSectionSize(const XHeaderView* self)` | 缺省 20 | `minimumSectionSize` qheaderview.h:126 | 借用（值） |
| `void XHeaderView_setSectionResizeMode(XHeaderView* self, int mode)` | 全局模式：清全部单段覆写后同步 `resizeSections(mode)`（`.c:896-920`） | `setSectionResizeMode(ResizeMode)` qheaderview.h:98 | 无 |
| `int XHeaderView_sectionResizeMode(const XHeaderView* self)` | 全局模式；NULL 返回 Interactive | `sectionResizeMode(int)` 只有单段重载（qheaderview.h:97），全局读取为 X 扩展 | 借用（值） |
| `void XHeaderView_resizeSections(XHeaderView* self, int mode)` | Stretch 以当前 length 均分可见段；其余只 `update()`（`.c:928-978`） | `resizeSections(ResizeMode)` qheaderview.h:73 | 无 |
| `void XHeaderView_setSectionResizeModeAt(XHeaderView* self, int section, int mode)` | 单段模式；-1 清除覆写（`.c:980-994`） | `setSectionResizeMode(int, ResizeMode)` qheaderview.h:99 | 无 |
| `int XHeaderView_sectionResizeModeAt(const XHeaderView* self, int logicalIndex)` | 无覆写/越界回落全局模式（`.c:996-1014`） | `sectionResizeMode(int)` qheaderview.h:97 | 借用（值） |
| `void XHeaderView_setFirstSectionMovable(XHeaderView* self, bool movable)` | 仅存状态（`.c:1016-1017`） | `setFirstSectionMovable` qheaderview.h:88 | 无 |
| `bool XHeaderView_isFirstSectionMovable(const XHeaderView* self)` | 需 `sectionsMovable` 同时为真（`.c:1019-1023`） | `isFirstSectionMovable` qheaderview.h:89 | 借用（值） |
| `void XHeaderView_reset(XHeaderView* self)` | 保留段数，复位尺寸/隐藏/单段模式/指示器，有段时发 `geometriesChanged`（`.c:1025-1049`） | `reset()` qheaderview.h:143 | 无 |
| `void XHeaderView_setDefaultAlignment(XHeaderView* self, int alignment)` | 仅存状态 | `setDefaultAlignment` qheaderview.h:132 | 无 |
| `int XHeaderView_defaultAlignment(const XHeaderView* self)` | 查询；NULL 0 | `defaultAlignment` qheaderview.h:131 | 借用（值） |
| `int XHeaderView_stretchSectionCount(const XHeaderView* self)` | 只有 0/1（`stretchLast` 决定，`.c:1057-1058`） | `stretchSectionCount` qheaderview.h:104 | 借用（值） |
| `bool XHeaderView_sectionsHidden(const XHeaderView* self)` | 任一隐藏 | `sectionsHidden` qheaderview.h:136 | 借用（值） |
| `void XHeaderView_setHighlightSections(XHeaderView* self, bool highlight)` | 仅存状态 | `setHighlightSections` qheaderview.h:94 | 无 |
| `bool XHeaderView_highlightSections(const XHeaderView* self)` | 缺省 false | `highlightSections` qheaderview.h:95 | 借用（值） |
| `void XHeaderView_setCascadingSectionResizes(XHeaderView* self, bool enable)` | 仅存状态（级联未接，`.c:1069-1070`） | `setCascadingSectionResizes` qheaderview.h:120 | 无 |
| `bool XHeaderView_cascadingSectionResizes(const XHeaderView* self)` | 缺省 false | `cascadingSectionResizes` qheaderview.h:119 | 借用（值） |
| `void XHeaderView_setResizeContentsPrecision(XHeaderView* self, int precision)` | 原样存取（`.c:1075-1076`） | `setResizeContentsPrecision` qheaderview.h:101 | 无 |
| `int XHeaderView_resizeContentsPrecision(const XHeaderView* self)` | 缺省 1000 | `resizeContentsPrecision` qheaderview.h:102 | 借用（值） |

布局与持久化：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XHeaderView_doItemsLayout(XHeaderView* self)` | 仅整控件 `update()`（`.c:1083-1090`） | `doItemsLayout()` qheaderview.h:134 | 无 |
| `XByteArray* XHeaderView_saveState(const XHeaderView* self)` | XHV 文本格式 v1 序列化（`.c:1181-`） | `saveState()` qheaderview.h:139 | **拷贝，调用方须释放**（`.h:491-492`） |
| `bool XHeaderView_restoreState(XHeaderView* self, const XByteArray* state)` | 校验先行、失败整体拒绝（`.c:1236-`） | `restoreState()` qheaderview.h:140 | state 借用 |

视觉序/反查/视口：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex)` | 移动即重排，恒等返回；越界 -1 | `visualIndex` qheaderview.h:83 | 借用（值） |
| `int XHeaderView_visualIndexAt(const XHeaderView* self, int visualIndex)` | 恒等返回；越界 -1 | `visualIndexAt(position)` qheaderview.h:60 | 借用（值） |
| `int XHeaderView_logicalIndex(const XHeaderView* self, int visualIndex)` | 恒等返回；越界 -1 | `logicalIndex` qheaderview.h:84 | 借用（值） |
| `int XHeaderView_logicalIndexAt(const XHeaderView* self, int position)` | 逐段累加反查（隐藏段仍占位）；越界 -1 | `logicalIndexAt(position)` qheaderview.h:61 | 借用（值） |
| `int XHeaderView_resizeHandleSectionAt(const XHeaderView* self, int position)` | ±3px 热区命中段尾分隔线 | 对标 `QHeaderViewPrivate::sectionHandleAt`（qheaderview.cpp:3307） | 借用（值） |
| `bool XHeaderView_resizeGestureActive(const XHeaderView* self)` | 拖拽调宽手势进行中 | 对标 `d->state == ResizeSection`（私有状态） | 借用（值） |
| `int XHeaderView_resizeGestureSection(const XHeaderView* self)` | 手势命中段；无手势 -1 | X 扩展（Qt 无公共查询） | 借用（值） |
| `int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex)` | 恒返回默认段尺寸（无内容感知） | `sectionSizeHint` qheaderview.h:58 | 借用（值） |
| `int XHeaderView_length(const XHeaderView* self)` | 可见段尺寸和（跳过隐藏） | `length` qheaderview.h:55 | 借用（值） |
| `void XHeaderView_setOffset(XHeaderView* self, int offset)` | 仅存储（`.c:1627-1631`） | `setOffset` qheaderview.h:146 | 无 |
| `int XHeaderView_offset(const XHeaderView* self)` | 存储值，初值 0 | `offset` qheaderview.h:54 | 借用（值） |
| `void XHeaderView_setOffsetToLastSection(XHeaderView* self)` | 偏移退化为 length（视口宽未接） | `setOffsetToLastSection` qheaderview.h:148 | 无 |
| `void XHeaderView_setOffsetToSectionPosition(XHeaderView* self, int index)` | 取 `sectionPosition(index)` | `setOffsetToSectionPosition` qheaderview.h:147 | 无 |
| `void XHeaderView_setModel(XHeaderView* self, void* model)` | 借用登记 + 连模型 `headerDataChanged` 转发槽（`.c:1659-1683`） | `setModel(QAbstractItemModel*)` qheaderview.h:51 | 借用禁释放（`.h:662-663`） |
| `XWidget* XHeaderView_viewport(XHeaderView* self)` | 收敛为返回自身（无独立视口） | `viewport()` qabstractscrollarea.h:56（由 QAbstractScrollArea 持有） | 借用禁释放 |

信号：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section)` | 释放于按压段时发射（`.c:1692-1699`、发射点 `.c:356`） | `sectionClicked` qheaderview.h:155 | 信号令牌 |
| `void* XHeaderView_sectionPressed_signal(XHeaderView* self, int section)` | 按下且 clickable 时发射（`.c:261-262`） | `sectionPressed` qheaderview.h:154 | 信号令牌 |
| `void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self, int logicalIndex, int order)` | 句柄自身不发；发射点 `.c:817-818`（只带 section 一个载荷） | `sortIndicatorChanged(int, Qt::SortOrder)` qheaderview.h:161 | 信号令牌 |
| `void* XHeaderView_sortIndicatorClearableChanged_signal(XHeaderView* self, bool clearable)` | 发射点 `.c:838` | `sortIndicatorClearableChanged(bool)` qheaderview.h:162 | 信号令牌 |
| `void* XHeaderView_sectionDoubleClicked_signal(XHeaderView* self, int section)` | 句柄预留，无发射点（`.c:1723-1728`） | `sectionDoubleClicked` qheaderview.h:157 | 信号令牌 |
| `void* XHeaderView_sectionEntered_signal(XHeaderView* self, int section)` | 句柄预留，无发射点（`.c:1730-1735`） | `sectionEntered` qheaderview.h:156 | 信号令牌 |
| `void* XHeaderView_sectionHandleDoubleClicked_signal(XHeaderView* self, int section)` | 句柄预留，无发射点（`.c:1737-1743`） | `sectionHandleDoubleClicked` qheaderview.h:159 | 信号令牌 |
| `void* XHeaderView_sectionMoved_signal(XHeaderView* self, int logicalIndex, int oldVisualIndex, int newVisualIndex)` | 发射点 moveSection/swapSections（`.c:745-750`、`.c:802-803`） | `sectionMoved` qheaderview.h:152 | 信号令牌 |
| `void* XHeaderView_sectionsMoved_signal(XHeaderView* self, int logicalIndex, int oldVisualIndex, int newVisualIndex)` | `sectionMoved` 的别名令牌（同一函数地址，`.c:1749-1754`） | Qt 侧是 `bool sectionsMoved() const` 查询（qheaderview.h:135），非信号 | 信号令牌 |
| `void* XHeaderView_sectionResized_signal(XHeaderView* self, int logicalIndex, int oldSize, int newSize)` | 发射点 `setSectionSize`（`.c:1454-1455`） | `sectionResized` qheaderview.h:153 | 信号令牌 |
| `void* XHeaderView_sectionCountChanged_signal(XHeaderView* self, int oldCount, int newCount)` | 发射点 `setCount`（`.c:624-625`） | `sectionCountChanged` qheaderview.h:158 | 信号令牌 |
| `void* XHeaderView_geometriesChanged_signal(XHeaderView* self)` | 无载荷；发射点 `.c:626/675/691/749/804/975/1047/1428/1456` | `geometriesChanged` qheaderview.h:160 | 信号令牌 |
| `void* XHeaderView_headerDataChanged_signal(XHeaderView* self, int section)` | 模型转发槽的出口，顺带整控件重绘（`.c:1801-1810`） | `headerDataChanged(Qt::Orientation,int,int)` 为公开槽 qheaderview.h:149；本处载荷收敛为单 int | 信号令牌 |

**所有权与命名契约**：`saveState` 返回**新分配** `XByteArray*`（`.h:492` 明写"调用者负责释放"），其余 getter 均为值或借用；`XHeaderView_setModel(void*)` 是类型擦除的借用登记（`.h:658-663` 注释"借用、不拥有"）。无 `_const` 家族、无 `_utf8`、`_2` 未用（重载改用 `At` 后缀：`.h:357-359` 自述"C 无重载，以 At 后缀区分"）、无三态 `set*`。

**可覆写点**：不新增槽（`.h:35-36` `XCLASS_DEFINE_EXTEND_END`）；覆写继承槽（`.c:483-497`）：`EXClass_Deinit`（释放三表+旁表键）、`EXClass_Copy`（深拷贝三表并浅拷贝借用 `m_model`）、`EXClass_Move`（转移三表并清源模型）、`EXWidget_MousePressEvent`（手柄命中→起手势；否则发 `sectionPressed`，`.c:228-264`）、`EXWidget_MouseMoveEvent`（手势中实时 `resizeSection`；否则切换 SplitH/SplitV 光标，`.c:272-323`）、`EXWidget_MouseReleaseEvent`（落定手势或发 `sectionClicked`，`.c:329-358`）。无 `_base` 入口。

**注意事项与坑点**

1. **`setMinimumSectionSize` 不落库**：`.h:305-317` 声明"设置最小区间尺寸…对不足下限的既有段立即抬升"，实现只抬升既有段、**从不写 `self->m_minimumSectionSize`**（`.c:876-891`）。全文件 `m_minimumSectionSize` 的写入仅 4 处：copy `.c:427`、move `.c:468`、init `.c:555=20`、以及 `setMaximumSectionSize` 里的被动降值 `.c:863`。故 `XHeaderView_setMinimumSectionSize(h, 40)` 后 `XHeaderView_minimumSectionSize(h)` 仍返回 20，后续 `resizeSection` 也按 20 钳制。
2. **`setSectionSize` 标注的"Qt setSectionSize 同名语义"不存在**：`.h:159` 写"Qt setSectionSize 同名语义"，但 `grep -rn 'setSectionSize' /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/` 返回 0 命中（Qt 只有 `resizeSection`，qheaderview.cpp:822-834）。该接口应记为 X 扩展；且它与 `sectionSize` 的越界策略不对称——`sectionSize` 越界返回默认尺寸（`.h:137`），`setSectionSize` 越界静默忽略（`.c:1447-1449`）。
3. **`sortIndicatorChanged` 声明两载荷、实发一个**：`.h:707-708` 声明 `(int logicalIndex, int order)`，`setSortIndicator` 以 `xhv_emitInt(...)` 只塞入 `section`（`.c:817-818`）；连接方按两参数取 `order` 会拿到错位/缺失实参。
4. **`XHeaderView_isStretchLastSection` 无声明**：`.c:1498-1505` 定义并作为 `stretchLastSection` 的内层实现，但 `.h` 中只有 `XHeaderView_stretchLastSection`（`.h:211`，`rg -n 'isStretchLastSection' Src/XGui/Widget/XHeaderView.h` 0 命中）——外部无法调用该别名，且 `-Wmissing-prototypes` 类构建会告警。
5. **视觉序/逻辑序恒等是刻意简化**：`.h:518-520/529-531/540-542` 三处 @note 明写"移动即重排…若后续引入独立视觉映射表则替换此实现"；`visualIndexAt`/`logicalIndex` 直接用越界判断后原样返回（`.c:1521-1541`）。`sectionMoved` 的"逻辑号"因此取移动后的新位置（`.c:801-803` 注释）。
6. **offset 不参与任何位置计算**：`.h:617-619/628-630/639-641` 与 `.c:1488-1489` 一致声明；`setOffsetToLastSection` 因视口宽按 0 参与而退化为 `length`（`.c:1638-1647`）。
7. **隐藏段仍占几何位**：`.h:552-554` 明写 `logicalIndexAt` 不跳过隐藏段；`sectionViewportPosition` 恒等 `sectionPosition`（`.c:1486-1491`），而 `length` 会跳过隐藏段（`.c:1611-1626`）——两者口径不同，用 `sectionPosition(max)` 推总长会偏大。
8. **`resizeContentsPrecision` 存 `uint16_t`**：`.h:59-61` 自陈"setter 原样存取，>65535 值会被截断——实测调用方仅用默认值/小阈值"。
9. **`sectionDoubleClicked/sectionEntered/sectionHandleDoubleClicked` 无发射点**：`.c:1723-1743` 三处仅返回令牌；`sectionClicked/sectionPressed` 有真实发射点（`.c:356`、`.c:262`），头文件 `.h:690-705` 的"句柄预留"注释与实现一致。
10. **类不自绘**：`XGui.md:1592-1594` 登记"XHeaderView 维持 XWidget 直接派生…本体无自绘代码，独立摆放不可见（demo 页以子类画段带可视化）"；本类确实未覆写 `EXWidget_PaintEvent`（`.c:483-497` 仅三类槽）。
11. **`count` 的 getter 注释未标注越界/NULL 语义**：`.h:125-126` 仅一句 `@brief 查询区间数。@param self 目标表头。@return 区间数。`，未写 NULL 返回值（实现 `.c:584-585` 对 NULL 返回 0）；`setCount` 的信号语义另写在 `.h:131`。与 Qt"段数由模型驱动"不同，本类可独立设段（X 扩展）。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XHeaderViewTest.c`；`rg -c '\bXHeaderView\b' Test/XGuiTest/` 0 命中）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_views.c`（`XHeaderView` 216 行命中，第 9 组 `:3488-` 专测段数/尺寸/隐藏/移动/指示器/持久化；信号装配 `:624-641`；表头可视化子类 `:2823-2824`），入口 `xapi_views_run`（`:805`）；演示页 `Test/XGuiDemo/xgui_demo_page_views.c` 29 处。

## XInputDialog

**定位**：文本/整数/浮点/下拉四种模式的输入对话框，含静态阻塞便捷函数与非阻塞 `open_2`。继承链 XObject←XWidget←XDialog←XInputDialog（`XDialog.h:29` `XCLASS_DEFINE_EXTEND_END(XDialog, XWidget)`）。头文件 `Src/XGui/Widget/XInputDialog.h`（606 行）。开关 `XWIDGET_ON && XDIALOG_ON`（`.h:47`）。

**生命周期**：`XInputDialog_init` 先 `XDialog_init`，再按 Qt 缺省置范围：`int` 全范围、`double ±1e308`、步进 1、小数 2 位、`m_openSignal=OpenFinished`（`.c:236-269`）；`create(parent, flags)` 宏（`.h:138`）；析构 `VXInputDialog_deinit` 释放 6 个 `XString*` + `XStringList*`，并兑现静态便捷路径登记的延迟布局回收（`.c:204-224`）。

**枚举与选项（静态度量，非函数）**

| 符号 | 取值与语义 | Qt 对标 |
|---|---|---|
| `XInputDialog_NoButtons` / `XInputDialog_UseListViewForComboBoxItems` / `XInputDialog_UsePlainTextEditForTextInput` | `XInputDialogOption` 位值 0x1/0x2/0x4（`.h:52-54`） | `QInputDialog::InputDialogOption` qinputdialog.h:43-47（数值一致） |
| `XInputDialogOptions` | `uint32_t` 选项位集（`.h:56`） | `QInputDialog::InputDialogOptions` qinputdialog.h:49 |
| `XInputDialog_TextInput` / `XInputDialog_IntInput` / `XInputDialog_DoubleInput` / `XInputDialog_ComboBoxInput` | `XInputDialogInputMode` 0/1/2/3（`.h:61-64`） | `QInputDialog::InputMode` qinputdialog.h:51-55 仅前三项；`ComboBoxInput` 为 X 兼容扩展 |
| `XInputDialogEchoMode_Normal/NoEcho/Password/PasswordEchoOnEdit` | `XInputDialogEchoMode` 0~3（`.h:70-73`） | `QLineEdit::EchoMode` 数值一致（`.h:67` 注释） |
| `XInputDialog_OpenFinished` / `XInputDialog_OpenTextValueSelected` / `XInputDialog_OpenIntValueSelected` / `XInputDialog_OpenDoubleValueSelected` | `XInputDialogOpenSignal` 0~3，供 `open_2` 选信号（`.h:82-85`） | X 扩展（Qt 由 `signalForMember` 按槽签名反射，qinputdialog.cpp:46-55） |

**API 面**

生命周期与实例属性：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XInputDialog_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用 |
| `void XInputDialog_init(XInputDialog* self, XWidget* parent, XWidgetFlags flags)` | 初始化 + Qt 缺省范围 | `QInputDialog(QWidget*, Qt::WindowFlags)` qinputdialog.h:57 | parent 借用 |
| `XInputDialog* XInputDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XInputDialog_create(parent, flags)` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `void XInputDialog_setInputMode(XInputDialog* self, XInputDialogInputMode mode)` | 原样赋值（无校验，`.c:284-285`） | `setInputMode` qinputdialog.h:60 | 无 |
| `XInputDialogInputMode XInputDialog_inputMode(const XInputDialog* self)` | 缺省 TextInput | `inputMode` qinputdialog.h:61 | 借用（值） |
| `void XInputDialog_setLabelText(XInputDialog* self, const XString* text)` | 深拷贝存入 | `setLabelText` qinputdialog.h:63 | 拷贝（入参仍归调用方） |
| `XString* XInputDialog_labelText(const XInputDialog* self)` | 返回副本 | `labelText` qinputdialog.h:64 | **拷贝，须 `XClassDelete`** |
| `void XInputDialog_setTextValue(XInputDialog* self, const XString* text)` | 变化时发 `textValueChanged`（`.c:302-318`） | `setTextValue` qinputdialog.h:71 | 拷贝 |
| `XString* XInputDialog_textValue(const XInputDialog* self)` | 副本 | `textValue` qinputdialog.h:72 | 拷贝，须释放 |
| `void XInputDialog_setIntValue(XInputDialog* self, int value)` | 变化时发 `intValueChanged` | `setIntValue` qinputdialog.h:83 | 无 |
| `int XInputDialog_intValue(const XInputDialog* self)` | 当前值；NULL 0 | `intValue` qinputdialog.h:84 | 借用（值） |
| `void XInputDialog_setDoubleValue(XInputDialog* self, double value)` | 变化时发 `doubleValueChanged` | `setDoubleValue` qinputdialog.h:97 | 无 |
| `double XInputDialog_doubleValue(const XInputDialog* self)` | 当前值；NULL 0.0 | `doubleValue` qinputdialog.h:98 | 借用（值） |
| `void XInputDialog_setComboBoxItems(XInputDialog* self, const XStringList* items)` | 清空后逐项深拷贝（`.c:347-369`） | `setComboBoxItems` qinputdialog.h:80 | 深拷贝，入参不转移 |
| `XStringList* XInputDialog_comboBoxItems(const XInputDialog* self)` | 深拷贝列表 | `comboBoxItems` qinputdialog.h:81 | **拷贝，须释放** |
| `void XInputDialog_setComboBoxEditable(XInputDialog* self, bool editable)` | 仅存状态 | `setComboBoxEditable` qinputdialog.h:77 | 无 |
| `bool XInputDialog_isComboBoxEditable(const XInputDialog* self)` | 查询 | `isComboBoxEditable` qinputdialog.h:78 | 借用（值） |
| `void XInputDialog_setOkButtonText(XInputDialog* self, const XString* text)` | 深拷贝（构建按钮时未消费，见坑点 4） | `setOkButtonText` qinputdialog.h:111 | 拷贝 |
| `XString* XInputDialog_okButtonText(const XInputDialog* self)` | 副本 | `okButtonText` qinputdialog.h:112 | 拷贝，须释放 |
| `void XInputDialog_setCancelButtonText(XInputDialog* self, const XString* text)` | 深拷贝（同上） | `setCancelButtonText` qinputdialog.h:114 | 拷贝 |
| `XString* XInputDialog_cancelButtonText(const XInputDialog* self)` | 副本 | `cancelButtonText` qinputdialog.h:115 | 拷贝，须释放 |
| `void XInputDialog_setPlaceholderText(XInputDialog* self, const XString* text)` | 仅 TextInput 模式有意义；由 `getText_3` 转发给内部行编辑（`.c:424-429`、`:1015-1027`） | **X 扩展**（Qt 6.8 `QInputDialog` 无 placeholder API，`grep -c placeholder dialogs/qinputdialog.h` = 0） | 拷贝 |
| `XString* XInputDialog_placeholderText(const XInputDialog* self)` | 副本 | 同上（X 扩展） | 拷贝，须释放 |
| `void XInputDialog_setOption(XInputDialog* self, XInputDialogOption option, bool on)` | 位置/清位 | `setOption` qinputdialog.h:66 | 无 |
| `bool XInputDialog_testOption(const XInputDialog* self, XInputDialogOption option)` | 位测试 | `testOption` qinputdialog.h:67 | 借用（值） |
| `void XInputDialog_setOptions(XInputDialog* self, XInputDialogOptions options)` | 整集赋值 | `setOptions` qinputdialog.h:68 | 无 |
| `XInputDialogOptions XInputDialog_options(const XInputDialog* self)` | 查询；NULL 0 | `options` qinputdialog.h:69 | 借用（值） |

数值范围/步进/小数位/回显（`.h:552-588`，均为存取值）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XInputDialog_setIntRange(XInputDialog* self, int min, int max)` | 设范围并钳当前值（`.c:1386-1393`） | `setIntRange` qinputdialog.h:92 | 无 |
| `int XInputDialog_intMinimum(const XInputDialog* self)` / `void XInputDialog_setIntMinimum(XInputDialog* self, int min)` | 下限存取（NULL 返回 `INT_MIN`） | qinputdialog.h:87 / 86 | 借用（值）/无 |
| `int XInputDialog_intMaximum(const XInputDialog* self)` / `void XInputDialog_setIntMaximum(XInputDialog* self, int max)` | 上限存取 | qinputdialog.h:90 / 89 | 借用（值）/无 |
| `int XInputDialog_intStep(const XInputDialog* self)` / `void XInputDialog_setIntStep(XInputDialog* self, int step)` | 步进存取 | qinputdialog.h:95 / 94 | 借用（值）/无 |
| `void XInputDialog_setDoubleRange(XInputDialog* self, double min, double max)` | 设范围 | `setDoubleRange` qinputdialog.h:106 | 无 |
| `double XInputDialog_doubleMinimum(const XInputDialog* self)` / `void XInputDialog_setDoubleMinimum(XInputDialog* self, double min)` | 下限存取 | qinputdialog.h:101 / 100 | 借用（值）/无 |
| `double XInputDialog_doubleMaximum(const XInputDialog* self)` / `void XInputDialog_setDoubleMaximum(XInputDialog* self, double max)` | 上限存取 | qinputdialog.h:104 / 103 | 借用（值）/无 |
| `double XInputDialog_doubleStep(const XInputDialog* self)` / `void XInputDialog_setDoubleStep(XInputDialog* self, double step)` | 步进存取 | qinputdialog.h:149 / 148 | 借用（值）/无 |
| `int XInputDialog_doubleDecimals(const XInputDialog* self)` / `void XInputDialog_setDoubleDecimals(XInputDialog* self, int decimals)` | 小数位存取 | qinputdialog.h:109 / 108 | 借用（值）/无 |
| `XInputDialogEchoMode XInputDialog_textEchoMode(const XInputDialog* self)` / `void XInputDialog_setTextEchoMode(XInputDialog* self, XInputDialogEchoMode mode)` | 回显模式存取 | qinputdialog.h:75 / 74 | 借用（值）/无 |

静态便捷函数（无 GUI 环境走桩）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XString* XInputDialog_getText(XWidget* parent, const XString* title, const XString* label, XInputDialogEchoMode echo, const XString* text, bool* ok)` | 单行文本；无 GUI 返回空串且 `*ok=false` | `getText` qinputdialog.h:125 | 返回值拷贝，须释放 |
| `XString* XInputDialog_getText_2(XWidget* parent, const char* title, const char* label, XInputDialogEchoMode echo, const char* text, bool* ok)` | 同上（UTF-8 入参） | 同上 | 拷贝，须释放 |
| `XString* XInputDialog_getText_3(XWidget* parent, const char* title, const char* label, XInputDialogEchoMode echo, const char* text, const char* placeholder, bool* ok)` | 同上 + 占位提示 | X 扩展（Qt 6.8 无 placeholder） | 拷贝，须释放 |
| `XString* XInputDialog_getMultiLineText(XWidget* parent, const XString* title, const XString* label, const XString* text, bool* ok)` | 多行文本 | `getMultiLineText` qinputdialog.h:130 | 拷贝，须释放 |
| `XString* XInputDialog_getMultiLineText_2(XWidget* parent, const char* title, const char* label, const char* text, bool* ok)` | 同上（UTF-8） | 同上 | 拷贝，须释放 |
| `int XInputDialog_getInt(XWidget* parent, const XString* title, const XString* label, int value, int minValue, int maxValue, int step, bool* ok)` | 整数输入；无 GUI 返回 `value` | `getInt` qinputdialog.h:139 | 值返回 |
| `int XInputDialog_getInt_2(XWidget* parent, const char* title, const char* label, int value, int minValue, int maxValue, int step, bool* ok)` | 同上（UTF-8） | 同上 | 值返回 |
| `double XInputDialog_getDouble(XWidget* parent, const XString* title, const XString* label, double value, double minValue, double maxValue, int decimals, bool* ok)` | 浮点输入 | `getDouble` qinputdialog.h:143 | 值返回 |
| `double XInputDialog_getDouble_2(XWidget* parent, const char* title, const char* label, double value, double minValue, double maxValue, int decimals, bool* ok)` | 同上（UTF-8） | 同上 | 值返回 |
| `XString* XInputDialog_getItem(XWidget* parent, const XString* title, const XString* label, const XStringList* items, int current, bool editable, bool* ok)` | 下拉选择；无 GUI 返回 `items[current]` 副本 | `getItem` qinputdialog.h:134 | 拷贝，须释放 |
| `XString* XInputDialog_getItem_2(XWidget* parent, const char* title, const char* label, const char* const* items, int count, int current, bool editable, bool* ok)` | 同上（UTF-8 数组） | 同上 | 拷贝，须释放 |

非阻塞与信号：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XInputDialog_open_2(XInputDialog* self, XObject* receiver, XSlotFunc1 member, XInputDialogOpenSignal openSignal)` | 窗口模态显示 + 按 `openSignal` 连一路信号，关闭收口时自动断开；重复 open 先断旧连（`.c:1327-1345`） | `open(QObject*, const char*)` qinputdialog.h:118 | receiver 借用，不转移 |
| `void* XInputDialog_textValueChanged_signal(XInputDialog* self, const XString* text)` | 真发射（`.c:1349-1354`，载荷深拷贝） | `textValueChanged` qinputdialog.h:152 | 信号令牌 |
| `void* XInputDialog_intValueChanged_signal(XInputDialog* self, int value)` | 真发射 | `intValueChanged` qinputdialog.h:154 | 信号令牌 |
| `void* XInputDialog_doubleValueChanged_signal(XInputDialog* self, double value)` | 真发射 | `doubleValueChanged` qinputdialog.h:156 | 信号令牌 |
| `void* XInputDialog_comboBoxTextChanged_signal(XInputDialog* self, const XString* text)` | 手动触发（`.c:1370-1381`） | Qt 6.8 无此信号（Qt 5 遗留，`grep -c comboBoxTextChanged dialogs/qinputdialog.h` = 0） | 信号令牌 |
| `void* XInputDialog_textValueSelected_signal(XInputDialog* self, const XString* text)` | 确认时发射（accept 结算链 `.c:875`） | `textValueSelected` qinputdialog.h:153 | 信号令牌 |
| `void* XInputDialog_intValueSelected_signal(XInputDialog* self, int value)` | 确认时发射 | `intValueSelected` qinputdialog.h:155 | 信号令牌 |
| `void* XInputDialog_doubleValueSelected_signal(XInputDialog* self, double value)` | 确认时发射 | `doubleValueSelected` qinputdialog.h:157 | 信号令牌 |
| `void* XInputDialog_comboBoxTextValueSelected_signal(XInputDialog* self, const XString* text)` | 确认时发射 | Qt 6.8 无此信号（X 兼容扩展，`.h:597`） | 信号令牌 |

**所有权与命名契约**：字符串 getter 全部返回**新分配副本**（`.h:174-176/189-191` 等逐条写明"调用方拥有，须 XClassDelete"），符合"_const=借用、无 `_const`=拷贝"的分工，但**未提供 `_const` 借用版**（`XGui.md:1811` 已登记全库 284 处同类待改名）。`_2` 后缀在此承担"UTF-8 入参重载"职责（`getText_2`/`getInt_2`…），与风格文档《字符串 API 后缀契约》第 4 条（`_2` 不得表达 UTF-8 类型差异，应用 `_utf8`）冲突，属 `XGui.md:1813` 登记族。`open_2` 是"同功能不同参数"的合规 `_2` 用法（`.h:498-501` 自述）。无三态 `set*`。

**可覆写点**：不新增槽（`.h:88-89` `XCLASS_DEFINE_EXTEND_END`）；覆写 `EXWidget_ShowEvent`、`EXWidget_CloseEvent`、`EXClass_Deinit`（`.c:230-232`）。无 `_base` 入口。

**注意事项与坑点**

1. **无 GUI 环境一律返回默认值**：`.h:21-24` 与 `.c:459-465`（`xid_guiReady()` 以 `XCoreApplication_instance() != NULL` 判定）一致；文本类返回空串（`.c:952-960`）、数值类返回入参 `value`（`.c:1116-1123`、`.c:1176-1184`）、`getItem` 返回 `items[current]` 越界空串（`.c:1243-1259`），且 `*ok` 一律 false。
2. **`setPlaceholderText` 的"Qt 对标"不成立**：`.h:270` 写"对标 QInputDialog::setPlaceholderText"，但 Qt 6.8.3 的 `qinputdialog.h/.cpp` 中 `placeholder` 关键字 0 命中（`grep -c placeholder` 两文件均 0）；应记为 X 扩展。
3. **`ComboBoxInput` 与 `comboBoxTextValueSelected` 均为兼容扩展**：`.h:17-19` 自述"ComboBoxInput=3 为兼容扩展（Qt 5 同值，Qt 6 已移除该项）"；`qinputdialog.h:51-55` 的 `InputMode` 只有 `TextInput/IntInput/DoubleInput`。`comboBoxTextValueSelected` 同理（`.h:597`，Qt 6.8 无）。
4. **`okButtonText`/`cancelButtonText`/`options` 存而不消费**：`xid_addButtons` 硬编码中文"确定"/"取消"（`.c:586`、`.c:597`），未读 `m_okButtonText`/`m_cancelButtonText`；`m_options` 的全部出现点只有 init 置 0 与 setOption/testOption/setOptions/options 四个访问器（`.c:245`、`.c:437-453`），静态便捷函数的构建路径不读取——`XInputDialog_NoButtons` 等选项对实际弹窗无效。另 `m_comboBoxEditable` 也只有 setter/getter（`.c:394-397`），构建下拉时未消费。
5. **实例范围字段不影响静态便捷函数**：`getInt`/`getDouble` 用的是入参 `minValue/maxValue/step`（`.c:1130-1134`、`.c:1209-1212`），不是 `m_intMinimum/m_intMaximum`；`setIntRange` 只影响实例 `XInputDialog` 与后续取值钳制（`.c:1386-1393`）。
6. **`open_2` 的 member 为 NULL 时完全不动作**：`.c:1330` `if (!self || !member) return;` —— 连"仅显示"都不会发生；`.h:508` 写"NULL 或 member 空时不执行任何操作"与实现一致，但易被误读成"receiver 为空时仍显示"（receiver 为空的"仅显示"分支在 `.c:1339-1344`，前提是 member 非空）。
7. **默认信号选择决定回调时机**：`XInputDialog_OpenFinished` 连的是 `XDialog_finished_signal`（`.c:802-804`），而 Qt 的 `signalForMember` 无参兜底是 `accepted()`（qinputdialog.cpp:55）——取消关闭时本实现也会回调，Qt 不会。
8. **QSS/数值解析口径**：`XGui.md:757` 记录 "QSS/XInputDialog 用裸 strtod"，即浮点输入不经 `XStrtod` 全串校验，尾随垃圾的容错行为与 `XStringView::toDouble` 家族不同。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XInputDialogTest.c`；`rg -c '\bXInputDialog\b' Test/XGuiTest/` 0 命中）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c`（80 行命中，入口 `xapi_dialogs_run` `:437`；该文件 298 条 `XAPI_EXPECT`，含 XDialog 基础与 XInputDialog 便捷函数构造覆盖）；`Test/XGuiDemo/xgui_demo_page_dialogs.c` 19 处；另有手机竖屏探针 `Test/XGuiDemo/xgui_dialog_portrait_probe.c` 3 处。

## XItemDelegate

**定位**：条目编辑器生命周期四虚槽（createEditor/setEditorData/setModelData/updateEditorGeometry）与 commitData/closeEditor 两信号的承载者，默认实现为"内嵌 XLineEdit 编辑器"路径。继承链 XObject←XItemDelegate。头文件 `Src/XGui/Widget/XItemDelegate.h`（188 行）。开关 `XWIDGET_ON && XTABLEWIDGET_ON`（`.h:32`）。

**生命周期**：`XItemDelegate_init` 清零 + `XObject_init` + 挂虚表（`.c:263-269`）；`create()` 宏（`.h:94-95`）；`VXItemDelegate_deinit` 仅分派父类析构（定义 `.c:240-245`，委托自身无资源）。**所有权**：视图不析构用户委托，只析构自己懒创建的默认委托（`.h:13-14` 注释；`XAbstractItemView.c:406-408` 释放 `m_defaultDelegate`）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XItemDelegate_class_init(void)` | 创建虚表并装四个默认实现 | 无（C 适配） | 借用 |
| `void XItemDelegate_init(XItemDelegate* self)` | 就地初始化 | `QAbstractItemDelegate(QObject*)` qabstractitemdelegate.h:36 | 无 |
| `XItemDelegate* XItemDelegate_create_ex(XMemoryType memory)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XItemDelegate_create()` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `XWidget* XItemDelegate_createEditor(XItemDelegate* self, XAbstractItemView* view, int row, int col)` | 查表分派 `CreateEditor` 槽；默认建 `XItemLineEditor`（父=view，`.c:171-191`） | `createEditor(parent, option, index)` qabstractitemdelegate.h:48 | **返回的编辑器所有权移交调用方**（视图在 closeEditor 时 `deleteLater`，`.h:103-104`） |
| `void XItemDelegate_setEditorData(XItemDelegate* self, XWidget* editor, XAbstractItemView* view, int row, int col)` | 默认读 EditRole 文本写入并全选（`.c:195-209`） | `setEditorData` qabstractitemdelegate.h:54 | editor 借用 |
| `void XItemDelegate_setModelData(XItemDelegate* self, XWidget* editor, XAbstractItemView* view, int row, int col)` | 默认把编辑器文本经 EditRole 写回模型（`.c:213-224`） | `setModelData` qabstractitemdelegate.h:56 | editor/模型均借用 |
| `void XItemDelegate_updateEditorGeometry(XItemDelegate* self, XWidget* editor, const XRect* cellRect)` | 默认整格 `setGeometryRect`（`.c:228-234`） | `updateEditorGeometry` qabstractitemdelegate.h:60 | 借用 |
| `void* XItemDelegate_commitData_signal(XItemDelegate* self, XWidget* editor)` | 编辑器请求提交时发射（`.c:335-339`） | `commitData` qabstractitemdelegate.h:78 | 信号令牌 |
| `void* XItemDelegate_closeEditor_signal(XItemDelegate* self, XWidget* editor, int hint)` | 编辑器请求关闭时发射；`RevertModelCache` 为放弃分支（`.c:341-347`） | `closeEditor` qabstractitemdelegate.h:79 | 信号令牌 |

虚槽签名 typedef：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `typedef XWidget* (*XItemDelegate_CreateEditorFunc)(XItemDelegate* self, XAbstractItemView* view, int row, int col)` | `CreateEditor` 槽函数类型（`.h:70-71`） | `createEditor(parent, option, index)` qabstractitemdelegate.h:48 | 返回物所有权移交调用方 |
| `typedef void (*XItemDelegate_SetEditorDataFunc)(XItemDelegate* self, XWidget* editor, XAbstractItemView* view, int row, int col)` | `SetEditorData` 槽函数类型（`.h:73-75`） | `setEditorData` qabstractitemdelegate.h:54 | editor 借用 |
| `typedef void (*XItemDelegate_SetModelDataFunc)(XItemDelegate* self, XWidget* editor, XAbstractItemView* view, int row, int col)` | `SetModelData` 槽函数类型（`.h:78-80`） | `setModelData` qabstractitemdelegate.h:56 | editor/模型借用 |
| `typedef void (*XItemDelegate_UpdateEditorGeometryFunc)(XItemDelegate* self, XWidget* editor, const XRect* cellRect)` | `UpdateEditorGeometry` 槽函数类型（`.h:83-84`） | `updateEditorGeometry` qabstractitemdelegate.h:60 | 借用 |

**所有权与命名契约**：无 `_const`/`_utf8`/三态 `set*`；`createEditor` 是"返回新建对象、所有权移交"（`.h:103-104`）；`setModelData` 命名沿用 Qt 的"回写模型"语义，与本库 `set*` 三态契约不冲突（无指针移交）。枚举 `XItemDelegateEndEditHint` 数值与 Qt 逐项一致（`.h:38-45` 对 qabstractitemdelegate.h:28-34）。

**可覆写点**：四个新槽（`.h:49-52`，首槽锚定 `XCLASS_VTABLE_GET_SIZE(XObject)`）：`CreateEditor`（默认 `VXItemDelegate_defaultCreateEditor`，`.c:250-251`）、`SetEditorData`（默认读 EditRole+全选，`.c:252-253`）、`SetModelData`（默认写 EditRole，`.c:254-255`）、`UpdateEditorGeometry`（默认整格，`.c:256-257`）；另有 `EXClass_Deinit`（`.c:249`）。默认编辑器的按键/失焦语义由内部 `XItemLineEditor` 子类覆写（`.c:159-168`：Enter/Tab 提交、Esc 放弃、失焦提交）。无 `_base` 入口。

**注意事项与坑点**

1. **绘制面不在本类**：`.h:11-12` 明写"绘制（paint/sizeHint）不在本批范围：条目绘制仍由派生视图直绘"；Qt 的 `paint`/`sizeHint`（qabstractitemdelegate.h:40/44）在本类**没有对应槽**，自定义委托无法通过继承改绘制。
2. **编辑器销毁是延迟的**：视图侧 `closeEditor` 走 `XWidget_hide` + `XObject_deleteLater`（`XAbstractItemView.c:700-704`），注释说明"编辑器可能仍处于自身按键回调栈内，立即析构有悬空风险"；委托方不得在 `commitData` 槽里同步释放编辑器。
3. **默认编辑器是 `XLineEdit` 派生，自定义编辑器会被强转**：`setEditorData`/`setModelData` 直接 `XLineEdit_setText((XLineEdit*)editor, ...)` / `XLineEdit_text(...)`（`.c:207`、`.c:221`），注释 `.c:203-204` 声明"自定义编辑器应由自定义委托覆写本槽"——若只覆写 `CreateEditor` 而沿用默认数据槽，会按 `XLineEdit` 布局强解。
4. **`hint` 参数为 `int` 而非枚举**：`closeEditor_signal(..., int hint)`（`.h:178-179`），取值须用 `XItemDelegateEndEditHint`；`SubmitModelCache` 与 `NoHint` 同走提交分支（`.h:43` 枚举注释；提交判定在 `XAbstractItemView.c:696`），`RevertModelCache` 是唯一放弃分支。
5. **`class_init` 无文档块、`init` 只有单行注释**：`.h:88` 的 `XVtable* XItemDelegate_class_init(void)` 前无任何 `@brief`（对比 `.h:89` 的 `init` 有单行块），`create_ex` 的块（`.h:91-92`）无 `@return` 所有权说明；与本库"公开函数须完整块注释"的规范不符（同类问题在 `out/xwidget_align/tree.md:90` 对 `XWidget_graphicsEffect` 已有登记）。
6. **委托为借用承载**：`.h:13-14` 与 `XAbstractItemView.c:566-578`（`xaiv_resolveDelegate` 行级→列级→默认三级解析）一致；设置后视图不接管所有权，用户提前释放委托会导致视图悬垂。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XItemDelegateTest.c`）。间接覆盖：`Test/XGuiTest/XKeyboardTest.c`（3 处引用，`:4892`、`:4962` 涉及按下路径与 `XItemDelegateEndEditHint_NoHint`）；`Test/XGuiDemo/xgui_demo_apitest_views.c` 11 处（`:763-780` 定义 `VwCapDelegate` 派生并转发 `EXItemDelegate_CreateEditor`，作为"自定义委托覆写虚槽"的活体样例）；`Test/XGuiDemo/xgui_demo_page_modbus.c` 2 处。

## XItemSelectionModel

**定位**：维护 `(row,col)` 平面选中集合与当前索引，供 `XAbstractItemView`/`XTableView`/`XTableWidget` 使用；选中项以 `(row<<16)|col` 编码存入 `XVector<int>`。继承链 XObject←XItemSelectionModel。头文件 `Src/XGui/Widget/XItemSelectionModel.h`（260 行）。开关 `XWIDGET_ON && XTABLEWIDGET_ON`（`.h:25`）。

**生命周期**：`XItemSelectionModel_init` 建 `m_selected` 向量、当前索引置 -1/-1（`.c:203-212`）；`create()` 宏（`.h:84-85`）；`VXItemSelectionModel_deinit` 释放向量后分派父类（`.c:140-148`）；`Copy`/`Move` 一并覆写（`.c:197-199`）。

**API 面**

生命周期与选择操作：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XItemSelectionModel_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用 |
| `void XItemSelectionModel_init(XItemSelectionModel* self)` | 初始化（模型未绑定、当前索引 -1） | `QItemSelectionModel(QAbstractItemModel*)` qitemselectionmodel.h:126 | 无 |
| `XItemSelectionModel* XItemSelectionModel_create_ex(XMemoryType memory)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XItemSelectionModel_create()` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `bool XItemSelectionModel_select(XItemSelectionModel* self, int row, int col, int command)` | 单格施加命令；`Rows/Columns` 先展开整行/列；`NoUpdate` 空操作；集合变化返回 true 并发一次 `selectionChanged`（`.c:225-257`） | `select(QModelIndex, SelectionFlags)` qitemselectionmodel.h:153 | 无 |
| `bool XItemSelectionModel_selectSelection(XItemSelectionModel* self, int topRow, int leftCol, int bottomRow, int rightCol, int command)` | 矩形区间逐格施加，行列先归一（`.c:259-267`、`:85-138`） | `select(QItemSelection, SelectionFlags)` qitemselectionmodel.h:154 | 无 |
| `bool XItemSelectionModel_selectRow(XItemSelectionModel* self, int row)` | 需绑定模型取列数；越界/未绑定 false（`.c:285-296`） | **X 扩展**：Qt 的 `selectRow` 在 `QTableView`（qtableview.h:87），`QItemSelectionModel` 无此方法 | 无 |
| `bool XItemSelectionModel_selectColumn(XItemSelectionModel* self, int column)` | 需绑定模型取行数（`.c:298-309`） | 同上：`QTableView::selectColumn` qtableview.h:88 | 无 |
| `bool XItemSelectionModel_isSelected(const XItemSelectionModel* self, int row, int col)` | 线性查找编码值 | `isSelected` qitemselectionmodel.h:131 | 借用（值） |
| `bool XItemSelectionModel_clearSelection(XItemSelectionModel* self)` | 仅清集合、**当前索引不动**；有变化才发信号（`.c:311-320`） | `clearSelection` qitemselectionmodel.h:158 | 无 |
| `bool XItemSelectionModel_clear(XItemSelectionModel* self)` | `clearSelection()+clearCurrentIndex()`（`.c:322-329`） | `clear` qitemselectionmodel.h:155 | 无 |
| `bool XItemSelectionModel_hasSelection(const XItemSelectionModel* self)` | 集合非空 | `hasSelection` qitemselectionmodel.h:138 | 借用（值） |
| `int XItemSelectionModel_selectedCount(const XItemSelectionModel* self)` | 选中数 | **X 扩展**（Qt 无 `selectedCount`，`grep -c selectedCount qitemselectionmodel.h` = 0） | 借用（值） |

模型绑定与当前索引：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XItemSelectionModel_setModel(XItemSelectionModel* self, XAbstractItemModel* model)` | 仅登记借用指针（`.c:343-346`），不发 `modelChanged` | `setModel` qitemselectionmodel.h:149 | 借用禁释放 |
| `XAbstractItemModel* XItemSelectionModel_model(const XItemSelectionModel* self)` | 取绑定模型；未绑定 NULL | `model` qitemselectionmodel.h:145/146 | 借用禁释放 |
| `void XItemSelectionModel_setCurrentIndex(XItemSelectionModel* self, int row, int col, int command)` | 同格时按 command 改选择但不发 `currentChanged`；异格时先写索引→施加命令→发 `currentChanged`（及行/列变化信号）（`.c:352-385`） | `setCurrentIndex` qitemselectionmodel.h:152 | 无 |
| `void XItemSelectionModel_clearCurrentIndex(XItemSelectionModel* self)` | 复位 -1/-1 并发三条 current 信号；原本无当前项时空操作（`.c:387-406`） | `clearCurrentIndex` qitemselectionmodel.h:159 | 无 |
| `bool XItemSelectionModel_currentIndex(const XItemSelectionModel* self, int* outRow, int* outCol)` | 输出行/列，无当前项置 -1；行列均 ≥0 返回 true | `currentIndex` qitemselectionmodel.h:129 | 出参借用（调用方提供缓冲） |

信号：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XItemSelectionModel_selectionChanged_signal(XItemSelectionModel* self)` | 无载荷；有连接时真发射（`.c:416-421`） | `selectionChanged` qitemselectionmodel.h:162（Qt 带 selected/deselected 两区间载荷） | 信号令牌 |
| `void* XItemSelectionModel_currentChanged_signal(XItemSelectionModel* self, int row, int col, int prevRow, int prevCol)` | 四 int 承载 Qt 的 current/previous 索引对（`.c:423-428`） | `currentChanged` qitemselectionmodel.h:163 | 信号令牌 |
| `void* XItemSelectionModel_currentRowChanged_signal(XItemSelectionModel* self, int row, int prevRow)` | 当前行变化（`.c:431-437`） | `currentRowChanged` qitemselectionmodel.h:164 | 信号令牌 |
| `void* XItemSelectionModel_currentColumnChanged_signal(XItemSelectionModel* self, int col, int prevCol)` | 当前列变化（`.c:440-446`） | `currentColumnChanged` qitemselectionmodel.h:165 | 信号令牌 |

**所有权与命名契约**：全部 getter 为借用或值返回，无 `_const` 拷贝族（`model()` 返回借用模型指针，`.h:187` 明写"模型借用指针；未绑定 NULL"）；无 `_utf8`/三态 `set*`；`setCurrentIndex` 名为 set 但不持所有权，符合"值 setter"。

**可覆写点**：不新增槽（`.h:60-61` `XCLASS_DEFINE_EXTEND_END`）；仅覆写 `EXClass_Deinit`（释放集合）、`EXClass_Copy`、`EXClass_Move`（`.c:197-199`）。无 `_base` 入口；Qt 侧 `select`/`setCurrentIndex` 为 `virtual`（qitemselectionmodel.h:152-154），X 侧未做虚槽，派生无法拦截选择语义。

**注意事项与坑点**

1. **行/列空间只有 16 位**：头文件 `.h:5-6` 明写"选择以 `(row<<16)|col` 编码"，实现 `#define XISM_ENC(row, col) ((((int)(row)) << 16) | ((int)(col)))`（`.c:17-19`）；行或列 ≥ 65536 时编码互相串位。同时 `m_currentRow/m_currentCol` 为 `int16_t`（`.h:72-73` 注释"负值哨兵，故有符号"），而 `setCurrentIndex` 参数是 `int`（`.h:210-211`）——行号 > 32767 写入即截断。
2. **`selectRow`/`selectColumn` 的对标对象写错**：`.h:127`、`.h:134` 写"对标 selectRow(row)/selectColumn(column)"，但 Qt 这两个方法属于 `QTableView`（qtableview.h:87-88），`QItemSelectionModel` 只提供 `selectedRows/selectedColumns` 查询（qitemselectionmodel.h:141-142）；本库把视图能力下放到了选择模型。
3. **`clearSelection()` 与 `clear()` 的信号差异**：`.h:149-156`（`clearSelection` 文档块）注明前者"当前索引保持不变"（实现 `.c:311-320` 只清集合）；后者串联两者、各自发信号（`.c:322-329`）。`clear()` 的返回值只反映**集合**是否有变化，当前索引被清空时为 false。
4. **`clearCurrentIndex` 无条件发三条信号**：`.c:396-406` 只判"原本有无当前项"，不判行列是否各自变化，因此行列未变也会收到 `currentRowChanged/currentColumnChanged`；这与 `setCurrentIndex` 的行列差分（`.c:377-385`）不同。
5. **`setModel` 不发 `modelChanged`**：Qt 有 `modelChanged` 信号（qitemselectionmodel.h:166），本类无对应句柄且 `setModel` 仅赋值（`.c:343-346`）——视图侧无法通过信号感知换模型。
6. **`Current` 位不改变集合**：`.h:37-39` 明写"Current 不改变集合内容，仅作为命令修饰位透传"，实现里 `xism_selectRect` 只判 `Toggle/Select/Deselect/Clear`（`.c:115-132`），`Current` 位被忽略。
7. **`NoUpdate` 在入口即早退**：`select`/`selectSelection` 均 `if (command == NoUpdate) return false;`（`.c:233`、`.c:264`），因此 `select(r,c,NoUpdate)` 不会像 Qt 那样"仅校验"。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XItemSelectionModelTest.c`；`rg -c '\bXItemSelectionModel\b' Test/XGuiTest/` 0 命中）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_views.c` 61 处（信号装配 `:324-337` 连接 `currentChanged`/`currentRowChanged`/`currentColumnChanged`；槽 `:266-295`），入口 `xapi_views_run`（`:805`）。此外 `XAbstractItemView` 复位路径调用 `XItemSelectionModel_clear`（`Src/XGui/Widget/XAbstractItemView.c:477-481`）。

## XKeySequenceEdit

**定位**：快捷键捕获控件，聚焦即进入捕获，按修饰键+非修饰键组合记录最多 4 组序列。继承链 XObject←XWidget←XKeySequenceEdit。头文件 `Src/XGui/Widget/XKeySequenceEdit.h`（203 行）。开关 `XWIDGET_ON && XKEYSEQUENCEEDIT_ON`（`.h:40`）。

**生命周期**：`XKeySequenceEdit_init` 置 `m_maxLength=4`、空闲定时器无效、结束键默认 `{Tab, Backtab}`、焦点策略 `StrongFocus`、尺寸 120×26 并同步 sizeHint/minimumSizeHint（`.c:437-470`）；`create(parent, flags)` 宏（`.h:89`）；`VXKeySequenceEdit_deinit` 停空闲表后分派父类（`.c:416-421`），虚表装载于 `.c:433`。

**API 面**

生命周期与序列：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XKeySequenceEdit_class_init(void)` | 创建共享虚表 | 无（C 适配） | 借用 |
| `void XKeySequenceEdit_init(XKeySequenceEdit* self, XWidget* parent, XWidgetFlags flags)` | 初始化 + 默认结束键 | `QKeySequenceEdit(QWidget*)` qkeysequenceedit.h:26 | parent 借用 |
| `XKeySequenceEdit* XKeySequenceEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XKeySequenceEdit_create(parent, flags)` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `const XKeySequence* XKeySequenceEdit_keySequence(const XKeySequenceEdit* self)` | 返回内部序列借用指针 | `keySequence()` qkeysequenceedit.h:30 | 借用禁释放 |
| `void XKeySequenceEdit_setKeySequence(XKeySequenceEdit* self, const XKeySequence* sequence)` | 相同早退；超上限截断并告警；变化时发 `keySequenceChanged`（`.c:490-515`） | `setKeySequence` qkeysequenceedit.h:40 | 值拷贝，入参不转移 |
| `void XKeySequenceEdit_clear(XKeySequenceEdit* self)` | 清空序列并发信号（`.c:517-523`） | `clear` qkeysequenceedit.h:41 | 无 |
| `int XKeySequenceEdit_maximumSequenceLength(const XKeySequenceEdit* self)` | 当前上限 | `maximumSequenceLength` qkeysequenceedit.h:31 | 借用（值） |
| `void XKeySequenceEdit_setMaximumSequenceLength(XKeySequenceEdit* self, int count)` | `<1` 或 `>4` 直接拒绝；已超长序列截断且不发信号（`.c:530-548`） | `setMaximumSequenceLength` qkeysequenceedit.h:42 | 无 |
| `bool XKeySequenceEdit_isClearButtonEnabled(const XKeySequenceEdit* self)` | 查询清除按钮开关 | `isClearButtonEnabled` qkeysequenceedit.h:34 | 借用（值） |
| `void XKeySequenceEdit_setClearButtonEnabled(XKeySequenceEdit* self, bool enable)` | 仅存状态 + 重绘（`.c:556-561`，无渲染消费，见坑点 4） | `setClearButtonEnabled` qkeysequenceedit.h:33 | 无 |

捕获态（X 自造面）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XKeySequenceEdit_startCapturing(XKeySequenceEdit* self)` | 程序化重武装捕获（`.h:131-139` 自述"自造口"） | **X 扩展**（Qt 6.8 无 capturing 公开 API） | 无 |
| `bool XKeySequenceEdit_isCapturing(const XKeySequenceEdit* self)` | 捕获中返回 true；NULL false | X 扩展 | 借用（值） |

结束键组合：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XKeySequenceEdit_finishingKeyCombinationCount(const XKeySequenceEdit* self)` | 组合数；NULL 0 | `finishingKeyCombinations().size()` qkeysequenceedit.h:37 | 借用（值） |
| `const XKeyCombination* XKeySequenceEdit_finishingKeyCombinations(const XKeySequenceEdit* self)` | 内部数组借用指针；NULL 返回 NULL | `finishingKeyCombinations()` qkeysequenceedit.h:37（Qt 返回 QList 值） | 借用禁释放（`.h:167`） |
| `void XKeySequenceEdit_setFinishingKeyCombinations(XKeySequenceEdit* self, const XKeyCombination* combos, int count)` | 逐项拷贝，超出 `XKEYSEQUENCEEDIT_MAX_FINISHING` 截断；`count<=0` 或 NULL 清空（`.c:592-608`） | `setFinishingKeyCombinations` qkeysequenceedit.h:36 | 借用读取，不接管（`.h:180`） |

信号：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XKeySequenceEdit_keySequenceChanged_signal(XKeySequenceEdit* self, const XKeySequence* sequence)` | 句柄自身**不发**（`.c:611-616` 直接返回）；真发射在 `xkse_emitChanged`（`.c:85-98`） | `keySequenceChanged` qkeysequenceedit.h:46 | 信号令牌 |
| `void* XKeySequenceEdit_editingFinished_signal(XKeySequenceEdit* self)` | 句柄自身**不发**（`.c:618-622`）；真发射在 `xkse_emitFinished`（`.c:100-`） | `editingFinished` qkeysequenceedit.h:45 | 信号令牌 |

**所有权与命名契约**：`keySequence`/`finishingKeyCombinations` 为 `const` 借用（符合借用语义但缺 `_const` 后缀，属 `XGui.md:1811` 登记族）；`setFinishingKeyCombinations` 以 `(数组, 数量)` 取代 Qt 的 `QList`（`.h:175-177` 说明）；无 `_utf8`、无三态 `set*`。

**可覆写点**：不新增槽（`.h:61-62` `XCLASS_DEFINE_EXTEND_END`）；覆写 `EXWidget_KeyPressEvent`（捕获/结束/清空/回删/记录，`.c:238-327`）、`EXWidget_MousePressEvent`（左键自请焦点，`.c:179-193`）、`EXWidget_PaintEvent`（序列文本/焦点框/占位，`.c:330-`）、`EXWidget_FocusInEvent`（发起捕获）、`EXWidget_FocusOutEvent`（退出捕获）、`EXObject_TimerEvent`（空闲超时），以及 `EXClass_Deinit`（`.c:427-433`）。无 `_base` 入口。

**注意事项与坑点**

1. **非捕获态不消费任何按键**：`.h:11` 与 `.c:246-253` 一致（`XEvent_ignore` 后返回）；Tab 交窗口级 `focusNextPrevChild`（注释引 `XWidget.c:2791`）。捕获态有 5s 空闲超时（`.c:117` `XKEYSEQUENCEEDIT_CAPTURE_IDLE_MS 5000`、`.c:119-127` 重装、`.c:229-236` 超时退出），超时后需重新点击或 `startCapturing`。
2. **Esc 会清空且不记录 Esc**：`.h:14` 与 `.c:298-305` 一致；Return/Enter 只"确认"（发 `editingFinished`）且捕获态保持（`.c:290-297`），真正的退出只有 Esc/失焦/空闲超时（`.c:157-160` 注释）。
3. **`setMaximumSequenceLength` 的拒绝策略与 Qt 不同**：实现 `count < 1 || count > 4` 时**静默返回**（`.c:532`）；Qt 打印 `qWarning` 后返回（qkeysequenceedit.cpp:195-199）。且上限硬编码为 `XKEYSEQUENCEEDIT_MAX_LENGTH`（4，`.h:43`），与 Qt `QKeySequencePrivate::MaxKeyCount=4`（qkeysequence_p.h:38）一致但不可扩展。
4. **`setClearButtonEnabled` 无渲染效果**：`m_clearButton` 仅在 init/setter/getter 出现（`.c:448`、`.c:553`、`.c:559`），`VXKeySequenceEdit_paintEvent` 不读它（`.c:330-411` 只见序列文本、焦点框、"Press shortcut"占位）。头文件 `.h:120-126` 只声明"获取/设置清除按钮开关"，未承诺渲染，但使用方容易误判。
5. **信号句柄自身不发射**：`.h:189-197` 两处注释写"真发射"，实际 `XKeySequenceEdit_keySequenceChanged_signal`/`editingFinished_signal` 只返回地址（`.c:611-622`），真发射在内部 `xkse_emitChanged`（`.c:85-98`）与 `xkse_emitFinished`（`.c:100-`）。与 `XInputDialog`/`XHeaderView` 的"句柄即发射"风格不一致，直接调用句柄不会通知任何槽。
6. **`XKeySequence` 是扁平值类型**：`.h:48-59`（`XKeyCombination{modifiers,key}` + 定长 `combos[4]` + `count`），无引用计数、无多平台编码（Qt 的 `QKeySequence` 有 NativeKey/PortableText 等格式）；跨平台序列化需自行换算。
7. **两处文档注释为自动生成的残句**：`.h:83-84` "XKeySequenceEditclassinit（对标 Qt 同名接口）。@return 返回对象指针；无效时返回 NULL。"（实际返回 `XVtable*`）、`.h:90-91` "XKeySequenceEditcreateex"——与函数真实语义不符，属文档缺陷。
8. **默认结束键与 Qt 一致但"未配置即清空"语义不同**：默认 `{Tab, Backtab}` 无修饰键（`.c:451-456`；Qt `finishingKeyCombinations = {Qt::Key_Tab, Qt::Key_Backtab}`，qkeysequenceedit.cpp:32）；`setFinishingKeyCombinations(NULL, 0)` 后"仅 Return/Enter 结束编辑"（`.h:176-177`），Qt 则退化为空列表（同效但无该条注释承诺）。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XKeySequenceEditTest.c`；`rg -c '\bXKeySequenceEdit\b' Test/XGuiTest/` 0 命中）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_text.c` 100 处（专节 `:1924-` "XKeySequenceEdit：快捷键捕获"，入口 `xapi_text_run` `:2482`）；`Test/XGuiDemo/xgui_demo_page_advanced.c` 22 处（Ctrl 修饰注入样例，`XGui.md:678` 记载）。

## XLabel

**定位**：在控件矩形内显示文本/像素图/绘图记录/影片的单内容单元，并内持 `XTextControl` 承载选择与链接交互。继承链 XObject←XWidget←XFrame←XLabel（`XFrame.h:103` `XCLASS_DEFINE_EXTEND_END(XFrame, XWidget)`）。头文件 `Src/XGui/Widget/XLabel.h`（480 行）。开关 `XWIDGET_ON && XFRAME_ON && XLABEL_ON`（`.h:65`）。

**生命周期**：`XLabel_init` 先 `XFrame_init`，建 `m_text`/`m_displayText`（`XString`）、置 AutoText/PlainText、`Left|VCenter`、margin 0、indent -1、交互标志 `LinksAccessibleByMouse`、`XPixmap_init` 像素图、创建内持 `XTextControl` 并接线，最后设 Preferred/Preferred+ControlType=Label 与 `WindowText` 前景角色（`.c:2864-2902`）；`create(parent, flags)` 宏（`.h:225`）；析构 `VXLabel_deinit`（定义 `XLabel.c:2718`，虚表装载 `.c:2860`）释放文本/链接/控制器（含 buddy 自摘连接：槽 `XLabel.c:3229`、`setBuddy` 内连接 `.c:3245-3255`）。

**API 面**

文本：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XLabel_class_init(void)` | 创建虚表并覆写多槽 | 无（C 适配） | 借用 |
| `void XLabel_init(XLabel* self, XWidget* parent, XWidgetFlags flags)` | 初始化 + 默认属性/Qt 一致 | `QLabel(QWidget*, Qt::WindowFlags)` qlabel.h:37 | parent 借用 |
| `XLabel* XLabel_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 指定内存类型创建 | 同上 | 拷贝/新建，须释放 |
| `#define XLabel_create(parent, flags)` | 默认内存类型创建 | 同上 | 堆对象须释放 |
| `const XString* XLabel_text(const XLabel* self)` | 原始文本借用；NULL 返回 NULL | `text()` qlabel.h:41 | **借用禁释放**（无 `_const` 后缀，见坑点 1） |
| `void XLabel_setText(XLabel* self, const XString* text)` | 相同则无操作；否则清内容→按格式重解→刷提示/几何/重绘（`.c:2924-`） | `setText` qlabel.h:98 | 深拷贝 |
| `void XLabel_setText_2(XLabel* self, const char* utf8)` | UTF-8 便利版 | `setText` qlabel.h:98 | 深拷贝 |
| `XLabelTextFormat XLabel_textFormat(const XLabel* self)` | 用户格式（默认 AutoText） | `textFormat` qlabel.h:58 | 借用（值） |
| `void XLabel_setTextFormat(XLabel* self, XLabelTextFormat format)` | 仅变化时生效并重解已有文本 | `setTextFormat` qlabel.h:59 | 无 |
| `void XLabel_setNum(XLabel* self, int num)` | 十进制整数文本（`.c:2984-2993`） | `setNum(int)` qlabel.h:106 | 无 |
| `void XLabel_setNum_2(XLabel* self, double num)` | `%g` 浮点文本（`.c:2995-3004`） | `setNum(double)` qlabel.h:107 | 无 |
| `void XLabel_clear(XLabel* self)` | 释放文本/图/记录/影片/链接并退出文本模式 | `clear` qlabel.h:108 | 无 |

图像/记录/影片：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XPixmap XLabel_pixmap(const XLabel* self)` | 返回像素图**值**（内部 `XClassCopy`，共享数据） | `pixmap()` qlabel.h:46 | 值返回，**调用方须 `XClassDeinit`**（回归测试 `xgui_regression_test.c:25973-25979` 即如此） |
| `void XLabel_setPixmap(XLabel* self, const XPixmap* pixmap)` | 同源短路；否则清内容并存共享副本（`.c:3025-3039`） | `setPixmap` qlabel.h:99 | 共享拷贝，入参不转移 |
| `XPicture* XLabel_picture(const XLabel* self)` | 绘图记录借用；无则 NULL（`.c:3041-3044`） | `picture()` qlabel.h:52 | 借用禁释放 |
| `void XLabel_setPicture(XLabel* self, const XPicture* picture)` | 深拷贝由标签持有 | `setPicture` qlabel.h:101 | 深拷贝 |
| `XMovie* XLabel_movie(const XLabel* self)` | 影片借用；无则 NULL（`.c:3058-3061`） | `movie()` qlabel.h:55 | 借用禁释放 |
| `void XLabel_setMovie(XLabel* self, XMovie* movie)` | 借用登记，标签不持有 | `setMovie` qlabel.h:104 | **借用**（`.h:298`） |

布局属性与尺寸：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XAlignments XLabel_alignment(const XLabel* self)` | 仅水平/垂直掩码位；NULL 返回 `Left|VCenter`（`.c:3090-3094`） | `alignment()` qlabel.h:64 | 借用（值） |
| `void XLabel_setAlignment(XLabel* self, XAlignments alignment)` | 掩码后写回（`.c:3097-3109`） | `setAlignment` qlabel.h:65 | 无 |
| `bool XLabel_wordWrap(const XLabel* self)` | 默认 false | `wordWrap` qlabel.h:68 | 借用（值） |
| `void XLabel_setWordWrap(XLabel* self, bool on)` | 同步 hasHeightForWidth | `setWordWrap` qlabel.h:67 | 无 |
| `int XLabel_indent(const XLabel* self)` | 默认 -1（自动） | `indent` qlabel.h:70 | 借用（值） |
| `void XLabel_setIndent(XLabel* self, int indent)` | 钳 `[-1,255]`（`.c:3128-3137`） | `setIndent` qlabel.h:71 | 无 |
| `int XLabel_margin(const XLabel* self)` | 默认 0 | `margin` qlabel.h:73 | 借用（值） |
| `void XLabel_setMargin(XLabel* self, int margin)` | 钳 `[0,255]`（`.c:3144-3151`） | `setMargin` qlabel.h:74 | 无 |
| `int XLabel_textPixelSize(const XLabel* self)` | 当前字体像素高度；NULL 0 | **X 扩展**（`.h:335-339` 自述 Qt 无同名函数） | 借用（值） |
| `void XLabel_setTextPixelSize(XLabel* self, int pixelHeight)` | 改字体 pixelSize 并刷布局；`<=0` 回默认（`.c:3158-3172`） | **X 扩展** | 无 |
| `bool XLabel_hasScaledContents(const XLabel* self)` | 默认 false | `hasScaledContents` qlabel.h:76 | 借用（值） |
| `void XLabel_setScaledContents(XLabel* self, bool on)` | 置真后内容等比拉伸填满 | `setScaledContents` qlabel.h:77 | 无 |
| `XSize XLabel_sizeHint(const XLabel* self)` | 文本按 `sizeForWidth(-1)` | `sizeHint` qlabel.h:78 | 值返回 |
| `XSize XLabel_minimumSizeHint(const XLabel* self)` | 文本高=一行、宽=不换行/最大词 | `minimumSizeHint` qlabel.h:79 | 值返回 |
| `int XLabel_heightForWidth(const XLabel* self, int width)` | 文本走 `sizeForWidth(w).height()`；非文本回退基类 | `heightForWidth` qlabel.h:84 | 借用（值） |

伙伴与交互：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XWidget* XLabel_buddy(const XLabel* self)` | 伙伴借用；销毁自动置空（QPointer 等价） | `buddy()` qlabel.h:82 | 借用禁释放 |
| `void XLabel_setBuddy(XLabel* self, XWidget* buddy)` | 登记借用 + 连 `destroyed` 自摘（`.c:3240-3255`） | `setBuddy` qlabel.h:81 | 借用，不转移 |
| `bool XLabel_openExternalLinks(const XLabel* self)` | 默认 false | `openExternalLinks` qlabel.h:86 | 借用（值） |
| `void XLabel_setOpenExternalLinks(XLabel* self, bool open)` | 置真也不打开浏览器，仍只发信号（`.h:395-400`） | `setOpenExternalLinks` qlabel.h:87 | 无 |
| `XLabelTextInteractionFlags XLabel_textInteractionFlags(const XLabel* self)` | 默认 `LinksAccessibleByMouse` | `textInteractionFlags` qlabel.h:90 | 借用（值） |
| `void XLabel_setTextInteractionFlags(XLabel* self, XLabelTextInteractionFlags flags)` | 同步控制器 + 推导焦点策略（`.c:3274-3303`） | `setTextInteractionFlags` qlabel.h:89 | 无 |

选择：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XLabel_setSelection(XLabel* self, int start, int length)` | 入参为**显示文本 UTF-16 码元**偏移，换算成 UTF-8 字节写入控制器；任一 <0 清选择（`.c:3307-`） | `setSelection(int,int)` qlabel.h:92 | 无 |
| `bool XLabel_hasSelectedText(const XLabel* self)` | 读控制器光标判空 | `hasSelectedText` qlabel.h:93 | 借用（值） |
| `XString* XLabel_selectedText(const XLabel* self)` | 选区副本；跨行换行转 U+2029（`.c:3356-3362`） | `selectedText` qlabel.h:94 | **拷贝，须释放**（无选择也返回空串对象） |
| `int XLabel_selectionStart(const XLabel* self)` | UTF-16 码元起点；无选择 -1 | `selectionStart` qlabel.h:95 | 借用（值） |

信号与离屏绘制：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XLabel_linkActivated_signal(XLabel* self, const XString* link)` | 已连接时发射（`.c:3377-3382`） | `linkActivated` qlabel.h:111 | 信号令牌；link 借用仅发射期间有效（`.h:448-449`） |
| `void* XLabel_linkHovered_signal(XLabel* self, const XString* link)` | 移入/移出/换链时发射，空串表示离开 | `linkHovered` qlabel.h:112 | 信号令牌 |
| `void XLabel_drawContents(XLabel* self, XPainter* painter)` | 先 `XFrame_drawFrame` 再在客户区绘制内容（`.c:3393-`） | **X 扩展**（离屏渲染/回归测试入口） | painter 借用 |

**所有权与命名契约**：`XLabel_text`/`XLabel_picture`/`XLabel_movie`/`XLabel_buddy` 均返回内部借用指针却**未带 `_const` 后缀**（按风格文档《获取函数的所有权契约》应为拷贝或改名 `_const`），正是 `XGui.md:1811` 登记的全库 284 处之一；`XLabel_setText_2(const char*)` 属 `_2` 表达 UTF-8 的违规族（`XGui.md:1813`）。`XLabel_pixmap` 是唯一"值返回需 `XClassDeinit`"的 getter（无 `_const`/拷贝二分，属 XRD 特例）。无三态 `set*`。

**可覆写点**：不新增槽（`.h:91-92` `XCLASS_DEFINE_EXTEND_END`）；覆写清单（`XLabel.c:2837-2860`，虚表装载块）：`EXWidget_SizeHint`、`EXWidget_MinimumSizeHint`、`EXObject_Event`、`EXWidget_PaintEvent`、`EXWidget_ChangeEvent`、`EXWidget_KeyPressEvent`、`EXWidget_MousePressEvent`、`EXWidget_MouseReleaseEvent`、`EXWidget_MouseMoveEvent`、`EXWidget_MouseDoubleClickEvent`、`EXWidget_FocusInEvent`、`EXWidget_FocusOutEvent`、`EXWidget_ContextMenuEvent`、`EXWidget_InputMethodEvent`、`EXClass_Copy/Move/Deinit`。无 `_base` 入口。

**注意事项与坑点**

1. **头文件声称的 `XPainter_drawText8x16` 系列不存在**：`.h:12-13` 写"文本绘制使用内置 8x16 等宽点阵字体（`XPainter_drawText8x16` 系列）"，但全库 `rg -n 'XPainter_drawText8x16' Src/` 只命中该头注释本身（1 处）；真实绘制路径是逐字形 `XPainter_drawGlyph`（`XLabel.c:1650-1651`，声明于 `Src/XGui/Graphics/XPainter.h:1578`），字库度量回退由 `XFONT8X16_*` 常量给出（`XLabel.c:1605-1610`）。
2. **`XLabel_setSelection` 有前置门槛，头文件未写**：实现要求"富文本 或 可选中标志 或 焦点策略非 NoFocus"三者之一（`.c:3316-3321`，注释引 `QLabelPrivate::needTextControl()`），否则**直接 return 不设选区**（退出点即 `.c:3321` 的 `return;`）。纯文本 + 无交互标志 + NoFocus 的默认标签调用 `setSelection` 会静默无效。
3. **`selectedText` 的换行被替换为 U+2029**：`.c:3356-3362` 注释引 `qlabel.cpp:752-758`（`QTextCursor::paragraphSeparator`），`XGui.md:1871` 亦登记该契约；按 `\n` 切段落的移植代码会拿不到预期分隔符。
4. **`openExternalLinks`/`buddy` 助记符为受限项**：`.h:395-400`（置真仅发信号）与 `.h:383-390`（`&` 助记符生成快捷键"受限未实现"）一致；`XGui.md:1905` 将其与"buddy 助记符/openExternalLinks 跳浏览器"一并登记为不修遗留。
5. **`XLabel_pixmap` 的释放义务靠调用点约定**：`.h:275` 只写"按值返回共享数据，可再传入绘制"（`XLabel.h:275-276`），未写"须 `XClassDeinit`"；实测调用点 `xgui_regression_test.c:25973-25980` 取出后 `XClassDeinit(&got)`/`XClassDeinit(&empty)`；`Test/XGuiDemo/xgui_demo_apitest_labels.c:275-277` 取返回值后未调用 `XClassDeinit`（与前者写法不一致）。按值返回的是带 `m_data` 引用的 `XPixmap` 壳（`XPixmap.h:50-54`），不 deinit 会漏引用计数。
6. **可覆写点清单与头注释不符**：`.h:87-89` 写"仅重载 `XClass` 的 Copy/Move/Deinit 与 `XObject` 的 Event、`XWidget` 的 PaintEvent/ChangeEvent/Mouse*Event/Focus*Event"，实际还覆写了 `SizeHint`/`MinimumSizeHint`（`.c:2837-2838`）、`KeyPressEvent`（`.c:2843`）、`ContextMenuEvent`（`.c:2853`）、`InputMethodEvent`（`.c:2856`）。
7. **前景角色推导是派生层复制**：`label_foregroundRole` 自行按 `backgroundRole` 映射 Button/Base/Dark/Shadow/Highlight/ToolTipBase（`.c:732-752`），`out/xwidget_align/appearance.md:27` 已登记该重复实现与"上提到 `XWidget_foregroundRole`"的整改建议。
8. **文本绘制逐行做视口剔除**：`.c:1687-1698` 注释记录"长文本标签每帧对全部行逐字形发起 draw call…269 行 × ~30 字形 ≈ 60ms/帧"，现已用 painter 裁剪+变换换算剔除视口外行；自定义绘制路径若绕过该判断会退回慢路径。
9. **`XMovie` 受 `XMOVIE_ON` 独立裁剪**：`.h:43-45`、`.h:293-305` 与 `.c:3058-3061` 一致——裁剪后 `movie()/setMovie()` 仍保留借用指针登记语义，但影片不参与尺寸计算与绘制。
10. **`XGui.md:1842-1872`（§8.0g28）** 记录本类已修 11 项（含 hfw 控件级接入、sizeHint/minimumSizeHint 推送、长词硬切宽度模型、垂直 indent、富文本误检测收紧），属已完成批次，勿按旧实现推断。

**测试覆盖**：无专属套件（`Test/XGuiTest/` 无 `XLabelTest.c`；`rg -c '\bXLabel\b' Test/XGuiTest/` 命中仅 `XStackedWidgetTest.c:4`、`XGuiRemoteAdaptTest.c:4`、`XGuiRemoteInputViewTest.c:1`、`XGuiRemoteTest.c:5`、`xscroll_clip_probe.c:4`、`XGuiDockDragTest.c:45`、`hsf_vfd_probe.c:3`）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_labels.c` 148 处（专节 `:214-` "XLabel：标签（对标 QLabel）"，含默认值/文本往返/setNum/pixmap/选区/交互标志，入口 `xapi_labels_run` `:1402`，该文件 182 条 `XAPI_EXPECT`）；顶层回归 `xgui_regression_test.c` 524 处引用（含 `:25965-25990` 的 pixmap 往返与 clear 断言、`:21187-21189` QSS 选择器、`:25545` 信号槽接收者）；`Test/XGuiDemo/xgui_window_demo.c` 140 处、`xgui_demo_page_network.c` 98 处等演示页。
## XLcdNumber

- **定位**：LCD 七段数码管数字显示控件，覆盖进制/段风格/位数/小数点模式与 overflow 信号；继承链 `XObject←XWidget←XFrame←XLcdNumber`（`XLcdNumber.h:68-69`）；头文件 `Src/XGui/Widget/XLcdNumber.h`（342 行）。
- **生命周期**：`XLcdNumber_init`（默认 5 位/Dec/Filled/Box|Raised）与 `XLcdNumber_init_2`（指定位数，0..99 钳位）双构造；堆对象走 `XLcdNumber_create` 宏 / `XLcdNumber_create_ex` 与 `XLcdNumber_create_2` 宏 / `XLcdNumber_create_ex_2`，调用方 `XClassDelete` 释放（`XLcdNumber.h:109-150`）。
- **API 面**：
  - 生命周期：`XVtable* XLcdNumber_class_init(void)` | 首次调用的单例虚表，继承 XFrame 并重载 PaintEvent | `QLCDNumber`（qlcdnumber.h:15） | 借用禁释放
  - `void XLcdNumber_init(XLcdNumber* self, XWidget* parent, XWidgetFlags flags)` | 默认状态完整初始化 | `QLCDNumber(QWidget*)`（qlcdnumber.h:26） | 无
  - `void XLcdNumber_init_2(XLcdNumber* self, unsigned numDigits, XWidget* parent, XWidgetFlags flags)` | 指定位数初始化 | `QLCDNumber(uint,QWidget*)`（qlcdnumber.h:27） | 无
  - `XLcdNumber* XLcdNumber_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - `XLcdNumber* XLcdNumber_create_ex_2(XMemoryType memory, unsigned numDigits, XWidget* parent, XWidgetFlags flags)` | 指定位数堆对象 | 构造对标同上 | 拷贝须释放
  - 专属属性：`bool XLcdNumber_smallDecimalPoint(const XLcdNumber* self)` | 小数点是否并入数字位 | `smallDecimalPoint`（qlcdnumber.h:39） | 借用禁释放
  - `void XLcdNumber_setSmallDecimalPoint(XLcdNumber* self, bool on)` | 设置小点模式并重绘 | `setSmallDecimalPoint`（qlcdnumber.h:65） | 无
  - `int XLcdNumber_digitCount(const XLcdNumber* self)` | 当前位数（0..99） | `digitCount`（qlcdnumber.h:40） | 借用
  - `void XLcdNumber_setDigitCount(XLcdNumber* self, int numDigits)` | 设置位数，保留既有显示串，负数取 0、>99 取 99 | `setDigitCount`（qlcdnumber.h:41） | 无
  - `XLcdNumberMode XLcdNumber_mode(const XLcdNumber* self)` | 进制模式 | `mode`（qlcdnumber.h:46） | 借用
  - `void XLcdNumber_setMode(XLcdNumber* self, XLcdNumberMode mode)` | 设进制并按当前值重显 | `setMode`（qlcdnumber.h:47） | 无
  - `XLcdNumberSegmentStyle XLcdNumber_segmentStyle(const XLcdNumber* self)` | 段风格 | `segmentStyle`（qlcdnumber.h:49） | 借用
  - `void XLcdNumber_setSegmentStyle(XLcdNumber* self, XLcdNumberSegmentStyle style)` | 设段风格并重绘 | `setSegmentStyle`（qlcdnumber.h:50） | 无
  - `bool XLcdNumber_checkOverflowInt(const XLcdNumber* self, int num)` | 整数溢出判定 | `checkOverflow(int)`（qlcdnumber.h:44） | 借用
  - `bool XLcdNumber_checkOverflowDouble(const XLcdNumber* self, double num)` | 浮点溢出判定 | `checkOverflow(double)`（qlcdnumber.h:43） | 借用
  - `double XLcdNumber_value(const XLcdNumber* self)` | 当前数值 | `value`（qlcdnumber.h:52） | 借用
  - `int XLcdNumber_intValue(const XLcdNumber* self)` | 四舍五入整数 | `intValue`（qlcdnumber.h:53） | 借用
  - 显示槽：`void XLcdNumber_display(XLcdNumber* self, const char* utf8)` | 显示字符串，整串可解析则同步 value，不判溢出 | `display(const QString&)`（qlcdnumber.h:58） | utf8 借用
  - `void XLcdNumber_display_2(XLcdNumber* self, int num)` | 按进制格式化整数，溢出发 overflow 并保留旧显示 | `display(int)`（qlcdnumber.h:59） | 无
  - `void XLcdNumber_display_3(XLcdNumber* self, double num)` | 按进制格式化浮点（仅 Dec 显示小数） | `display(double)`（qlcdnumber.h:60） | 无
  - `void XLcdNumber_setHexMode(XLcdNumber* self)` | 便捷切 Hex | `setHexMode`（qlcdnumber.h:61） | 无
  - `void XLcdNumber_setDecMode(XLcdNumber* self)` | 便捷切 Dec | `setDecMode`（qlcdnumber.h:62） | 无
  - `void XLcdNumber_setOctMode(XLcdNumber* self)` | 便捷切 Oct | `setOctMode`（qlcdnumber.h:63） | 无
  - `void XLcdNumber_setBinMode(XLcdNumber* self)` | 便捷切 Bin | `setBinMode`（qlcdnumber.h:64） | 无
  - 信号：`void* XLcdNumber_overflow_signal(XLcdNumber* self)` | overflow 信号标识，仅作 connect 参数 | `overflow`（qlcdnumber.h:68） | 借用禁释放
  - 父类转发宏（`XLcdNumber.h:324-333`）：`XLcdNumber_sizeHint` / `frameStyle` / `setFrameStyle` / `frameShape` / `setFrameShape` / `frameShadow` / `setFrameShadow` / `lineWidth` / `setLineWidth` / `frameWidth` | 直转 XWidget/XFrame | `QFrame`/`QWidget` 同名（无独立行号，属转发） | 无
- **所有权与命名契约**：无 `_const` 借用族以外的返回堆对象；设置侧无 `_utf8` 后缀（`display` 直接收 `const char*`）；`display_2`（int）/`display_3`（double）与 `setHexMode` 等便捷槽构成 `_2`/`_3` 重载族；三态 set* 无（`setDigitCount`/`setMode`/`setSegmentStyle` 均为「未变化即返回」二态）。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XLcdNumber)` + `XCLASS_DEFINE_EXTEND_END(XLcdNumber, XFrame)`（`XLcdNumber.h:68-69`）；register 见 `XLcdNumber.c:484-486`：`EXWidget_PaintEvent`→七段数码管绘制、`EXWidget_SizeHint`→按位数与边框计算提示尺寸，其余槽继承 XFrame。
- **注意事项与坑点**：
  1. `XLcdNumber_display` 走整串解析口径：尾部存在其余字符即置 0（`XLcdNumber.c:682-700` 注释「此前为 XStrtod 前缀解析（"12.5px"→12.5），与 Qt 整串口径（失败置 0）不一致」），头文件 `XLcdNumber.h:255-256` 同口径注明「整串（允许首尾空白）可解析为数值时同步 value，否则置 0」。
  2. 绘制为工程简化：头注明言「Qt 以内部字体度量计算段长，本实现按控件几何均分数字位」（`XLcdNumber.h:24-25`）；`XGui.md:1904` 登记「XLcdNumber frameWidth 内容内缩（刻意改良偏离，默认边框差 1-2px）」。
  3. `setDigitCount` 不按数值重格式化：源码注释「display 设定过非数值内容（如 "12:34"）后改位数不再按 m_value 重格式化置 0。仅「原位数==0」时才 display(value())」（`XLcdNumber.c:585-587`，对标 qlcdnumber.cpp:379-405）。
  4. `intValue` 用 `+0.5/-0.5` 截断实现四舍五入（`XLcdNumber.c:669-675`），头文件描述为「四舍五入到最近整数」（`XLcdNumber.h:242-246`）——负数边界与 qRound 的 half-away-from-zero 一致，但非 `qRound` 直接调用。
  5. 模块开关三重门控 `XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON`（`XLcdNumber.h:42`），`=0` 时全部公共 API 被裁剪。
- **测试覆盖**：`Test/XGuiTest/XLcdNumberTest.c` 存在，入口 `bool XLcdNumberTest_runAll(void)`（第 27 行；模块关闭时第 143 行另有 skipped 桩），断言宏 `ln_expect` 定义于第 12 行；`ln_expect(` 全文件出现 34 次（`rg -o "ln_expect\(" | wc -l` = 34），其中 1 次为定义 → 调用点 33 处。已注册于 `Test/CMakeLists.txt:562`。

## XLineEdit

- **定位**：单行文本编辑控件（壳），编辑引擎下沉到私有控制器 `XLineControl`；继承链 `XObject←XWidget←XLineEdit`（`XLineEdit.h:138-139`）；头文件 `Src/XGui/Widget/XLineEdit.h`（876 行）。
- **生命周期**：`XLineEdit_init` 创建控制器与空占位文本，控制器分配失败时控件仍完成初始化（`XLineEdit.h:218-219`）；堆对象走 `XLineEdit_create` 宏 / `XLineEdit_create_ex`，`XClassDelete` 释放。
- **API 面**：
  - 生命周期：`XVtable* XLineEdit_class_init(void)` | 单例虚表，重载键盘/IME/鼠标/焦点/绘制/变更/定时器与 copy/move/deinit | `QLineEdit`（qlineedit.h:27） | 借用禁释放
  - `void XLineEdit_init(XLineEdit* self, XWidget* parent, XWidgetFlags flags)` | 初始化壳与控制器 | `QLineEdit(QWidget*)`（qlineedit.h:57） | 无
  - `XLineEdit* XLineEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 文本：`const char* XLineEdit_text(const XLineEdit* self)` | 内部文本借用，掩码时剥离占位 | `text`（qlineedit.h:61） | 借用禁释放
  - `const char* XLineEdit_displayText(const XLineEdit* self)` | 按回显模式处理的显示文本 | `displayText`（qlineedit.h:63） | 借用禁释放
  - `void XLineEdit_setText(XLineEdit* self, const char* text)` | 置文本、光标移末尾、清撤销历史 | `setText`（qlineedit.h:148） | 无
  - `void XLineEdit_clear(XLineEdit* self)` | 清空文本 | `clear`（qlineedit.h:149） | 无
  - `void XLineEdit_insert(XLineEdit* self, const char* utf8)` | 光标处插入（替换选区，不设 readOnly 门禁） | `insert`（qlineedit.h:161） | utf8 借用
  - `const char* XLineEdit_placeholderText(const XLineEdit* self)` | 占位提示借用 | `placeholderText`（qlineedit.h:65） | 借用禁释放
  - `void XLineEdit_setPlaceholderText(XLineEdit* self, const char* placeholder)` | 设占位提示 | `setPlaceholderText`（qlineedit.h:66） | 无
  - 编辑属性：`bool XLineEdit_isReadOnly(const XLineEdit* self)` | 只读查询 | `isReadOnly`（qlineedit.h:82） | 借用
  - `void XLineEdit_setReadOnly(XLineEdit* self, bool readOnly)` | 设只读 | `setReadOnly`（qlineedit.h:83） | 无
  - `int XLineEdit_echoMode(const XLineEdit* self)` | 回显模式 | `echoMode`（qlineedit.h:79） | 借用
  - `void XLineEdit_setEchoMode(XLineEdit* self, int echoMode)` | 设回显模式并自标注 IME 提示位 | `setEchoMode`（qlineedit.h:80） | 无
  - `int XLineEdit_maxLength(const XLineEdit* self)` | 最大字符数（0=不限） | `maxLength`（qlineedit.h:68） | 借用
  - `void XLineEdit_setMaxLength(XLineEdit* self, int maxLength)` | 设上限，超长截断并发 textChanged | `setMaxLength`（qlineedit.h:69） | 无
  - `int XLineEdit_alignment(const XLineEdit* self)` | 文本对齐 | `alignment`（qlineedit.h:103） | 借用
  - `void XLineEdit_setAlignment(XLineEdit* self, int alignment)` | 设对齐并重绘 | `setAlignment`（qlineedit.h:102） | 无
  - `bool XLineEdit_hasFrame(const XLineEdit* self)` | 边框开关 | `hasFrame`（qlineedit.h:72） | 借用
  - `void XLineEdit_setFrame(XLineEdit* self, bool on)` | 设边框 | `setFrame`（qlineedit.h:71） | 无
  - `bool XLineEdit_isClearButtonEnabled(const XLineEdit* self)` | 清除按钮开关 | `isClearButtonEnabled`（qlineedit.h:75） | 借用
  - `void XLineEdit_setClearButtonEnabled(XLineEdit* self, bool enable)` | 设清除按钮 | `setClearButtonEnabled`（qlineedit.h:74） | 无
  - `void XLineEdit_setFinishOnFocusOut(XLineEdit* self, bool enable)` | 失焦必发 editingFinished（delegate 编辑器语义） | X 扩展（Qt 由 QAbstractItemDelegate eventFilter 承载） | 无
  - `void XLineEdit_setValidator(XLineEdit* self, XLineEditValidatorFunc validator, void* userData)` | 设 C 适配校验回调 | `setValidator`（qlineedit.h:86） | validator/userData 借用
  - `XLineEditValidatorFunc XLineEdit_validator(const XLineEdit* self)` | 查询校验回调 | `validator`（qlineedit.h:87） | 借用
  - `XSize XLineEdit_sizeHint(const XLineEdit* self)` | 尺寸提示并同步 XWidget | `sizeHint`（qlineedit.h:95） | 无
  - `XSize XLineEdit_minimumSizeHint(const XLineEdit* self)` | 最小尺寸提示 | `minimumSizeHint`（qlineedit.h:96） | 无
  - `void XLineEdit_addAction(XLineEdit* self, XAction* action, int position)` | 注册内置 action（借用） | `addAction(QAction*,ActionPosition)`（qlineedit.h:143） | 借用禁释放
  - `XAction* XLineEdit_addActionIcon(XLineEdit* self, const char* iconPath, int position)` | 以图标路径新建并注册 | `addAction(const QIcon&,ActionPosition)`（qlineedit.h:144） | 新建归调用方，须释放
  - `XRect XLineEdit_cursorRect(const XLineEdit* self)` | 光标竖线矩形 | `cursorRect`（qlineedit.h:204） | 无
  - `int XLineEdit_cursorPosition(const XLineEdit* self)` | 光标字符位置 | `cursorPosition`（qlineedit.h:98） | 借用
  - `void XLineEdit_setCursorPosition(XLineEdit* self, int position)` | 设光标位置（钳位+清选区） | `setCursorPosition`（qlineedit.h:99） | 无
  - `int XLineEdit_cursorPositionAt(const XLineEdit* self, const XPoint* pos)` | 像素位置反查字符位置 | `cursorPositionAt`（qlineedit.h:100） | pos 借用
  - 光标移动与编辑键：`void XLineEdit_cursorForward(XLineEdit* self, bool mark, int steps)` | 右移 | `cursorForward`（qlineedit.h:105） | 无
  - `void XLineEdit_cursorBackward(XLineEdit* self, bool mark, int steps)` | 左移 | `cursorBackward`（qlineedit.h:106） | 无
  - `void XLineEdit_cursorWordForward(XLineEdit* self, bool mark)` | 按词右移 | `cursorWordForward`（qlineedit.h:107） | 无
  - `void XLineEdit_cursorWordBackward(XLineEdit* self, bool mark)` | 按词左移 | `cursorWordBackward`（qlineedit.h:108） | 无
  - `void XLineEdit_backspace(XLineEdit* self)` | 退格删除 | `backspace`（qlineedit.h:109） | 无
  - `void XLineEdit_del(XLineEdit* self)` | 删除光标处字符 | `del`（qlineedit.h:110） | 无
  - `void XLineEdit_home(XLineEdit* self, bool mark)` | 移到行首 | `home`（qlineedit.h:111） | 无
  - `void XLineEdit_end(XLineEdit* self, bool mark)` | 移到行尾 | `end`（qlineedit.h:112） | 无
  - 修改状态：`bool XLineEdit_isModified(const XLineEdit* self)` | 用户是否编辑过 | `isModified`（qlineedit.h:114） | 借用
  - `void XLineEdit_setModified(XLineEdit* self, bool modified)` | 设修改状态 | `setModified`（qlineedit.h:115） | 无
  - 选区：`void XLineEdit_setSelection(XLineEdit* self, int start, int length)` | 设选区（start 越界告警并忽略整个调用） | `setSelection`（qlineedit.h:117） | 无
  - `bool XLineEdit_hasSelectedText(const XLineEdit* self)` | 是否有选区 | `hasSelectedText`（qlineedit.h:118） | 借用
  - `char* XLineEdit_selectedText(const XLineEdit* self)` | 选中文本堆拷贝 | `selectedText`（qlineedit.h:119） | 拷贝须 `XFree_System`
  - `int XLineEdit_selectionStart(const XLineEdit* self)` | 选区起点 | `selectionStart`（qlineedit.h:120） | 借用
  - `int XLineEdit_selectionEnd(const XLineEdit* self)` | 选区终点 | `selectionEnd`（qlineedit.h:121） | 借用
  - `int XLineEdit_selectionLength(const XLineEdit* self)` | 选区长度 | `selectionLength`（qlineedit.h:122） | 借用
  - `void XLineEdit_deselect(XLineEdit* self)` | 清选区 | `deselect`（qlineedit.h:160） | 无
  - `void XLineEdit_selectAll(XLineEdit* self)` | 全选 | `selectAll`（qlineedit.h:150） | 无
  - 撤销/重做：`bool XLineEdit_isUndoAvailable(const XLineEdit* self)` | 可撤销 | `isUndoAvailable`（qlineedit.h:124） | 借用
  - `bool XLineEdit_isRedoAvailable(const XLineEdit* self)` | 可重做 | `isRedoAvailable`（qlineedit.h:125） | 借用
  - `void XLineEdit_undo(XLineEdit* self)` | 撤销 | `undo`（qlineedit.h:151） | 无
  - `void XLineEdit_redo(XLineEdit* self)` | 重做 | `redo`（qlineedit.h:152） | 无
  - 剪贴板：`void XLineEdit_cut(XLineEdit* self)` | 剪切选区 | `cut`（qlineedit.h:154） | 无
  - `void XLineEdit_copy(XLineEdit* self)` | 复制选区（密码类回显不写剪贴板） | `copy`（qlineedit.h:155） | 无
  - `void XLineEdit_paste(XLineEdit* self)` | 粘贴（按 maxLength/掩码过滤） | `paste`（qlineedit.h:156） | 无
  - `XMenu* XLineEdit_createStandardContextMenu(XLineEdit* self)` | 标准右键菜单 | `createStandardContextMenu`（qlineedit.h:163） | 所有权转移给调用方
  - 其他属性：`bool XLineEdit_dragEnabled(const XLineEdit* self)` | 拖拽开关（仅存储） | `dragEnabled`（qlineedit.h:128） | 借用
  - `void XLineEdit_setDragEnabled(XLineEdit* self, bool b)` | 设拖拽开关（仅存储） | `setDragEnabled`（qlineedit.h:127） | 无
  - `int XLineEdit_cursorMoveStyle(const XLineEdit* self)` | 光标移动风格 | `cursorMoveStyle`（qlineedit.h:131） | 借用
  - `void XLineEdit_setCursorMoveStyle(XLineEdit* self, int style)` | 设移动风格（两风格行为一致） | `setCursorMoveStyle`（qlineedit.h:130） | 无
  - `const char* XLineEdit_inputMask(const XLineEdit* self)` | 输入掩码借用 | `inputMask`（qlineedit.h:133） | 借用禁释放
  - `void XLineEdit_setInputMask(XLineEdit* self, const char* inputMask)` | 设输入掩码 | `setInputMask`（qlineedit.h:134） | 无
  - `bool XLineEdit_hasAcceptableInput(const XLineEdit* self)` | 掩码+校验均满足 | `hasAcceptableInput`（qlineedit.h:135） | 借用
  - `void XLineEdit_setTextMargins(XLineEdit* self, int left, int top, int right, int bottom)` | 设四边文本边距 | `setTextMargins(int,int,int,int)`（qlineedit.h:137） | 无
  - `void XLineEdit_setTextMargins_2(XLineEdit* self, const XMargins* margins)` | 边距对象版本 | `setTextMargins(const QMargins&)`（qlineedit.h:138） | margins 借用
  - `XMargins XLineEdit_textMargins(const XLineEdit* self)` | 查询边距 | `textMargins`（qlineedit.h:139） | 按值返回
  - 补全器：`void XLineEdit_setCompleter(XLineEdit* self, XCompleter* completer)` | 安装补全器（借用，不拥有） | `setCompleter`（qlineedit.h:91） | 借用禁释放
  - `XCompleter* XLineEdit_completer(const XLineEdit* self)` | 查询补全器 | `completer`（qlineedit.h:92） | 借用禁释放
  - 信号：`void* XLineEdit_textChanged_signal(XLineEdit* self)` | 文本任何原因变化 | `textChanged`（qlineedit.h:167） | 借用禁释放
  - `void* XLineEdit_textEdited_signal(XLineEdit* self)` | 仅用户编辑 | `textEdited`（qlineedit.h:168） | 借用禁释放
  - `void* XLineEdit_cursorPositionChanged_signal(XLineEdit* self, int oldPos, int newPos)` | 光标位置变化 | `cursorPositionChanged`（qlineedit.h:169） | 借用禁释放
  - `void* XLineEdit_returnPressed_signal(XLineEdit* self)` | Return 键 | `returnPressed`（qlineedit.h:170） | 借用禁释放
  - `void* XLineEdit_editingFinished_signal(XLineEdit* self)` | Return 或失焦且有编辑 | `editingFinished`（qlineedit.h:171） | 借用禁释放
  - `void* XLineEdit_selectionChanged_signal(XLineEdit* self)` | 选区变化 | `selectionChanged`（qlineedit.h:172） | 借用禁释放
  - `void* XLineEdit_inputRejected_signal(XLineEdit* self)` | 输入被掩码/长度/校验拒绝 | `inputRejected`（qlineedit.h:173） | 借用禁释放
  - 父类转发宏 `XLineEdit_setEnabled` … `XLineEdit_updateGeometry`（`XLineEdit.h:236-265`，共 30 条） | 直转 XWidget | 无独立 Qt 行号（QWidget 同名） | 无
- **所有权与命名契约**：`_const` 借用族未在此头文件出现性命名，但返回值约定为「借用禁释放」者：`XLineEdit_text`/`displayText`/`placeholderText`/`inputMask`/`completer` 及全部 `*_signal`；设置侧 UTF-8 重载无 `_utf8` 后缀（`setText`/`insert`/`setPlaceholderText`/`setInputMask` 均直接收 `const char*`）；`_2` 重载为 `XLineEdit_setTextMargins_2`；三态 set* 无（全部 setter 均为条件早返）。`selectedText` 用 `XFree_System` 释放而非 `XClassDelete`（`XLineEdit.h:590-591`）。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XLineEdit)` + `XCLASS_DEFINE_EXTEND_END(XLineEdit, XWidget)`（`XLineEdit.h:138-139`）；`XLineEdit.c:1849-1883` 重载 `EXWidget_SizeHint`/`MinimumSizeHint`/`KeyPressEvent`/`KeyReleaseEvent`/`InputMethodEvent`/`InputMethodQuery`/`ContextMenuEvent`/`MousePress`/`MouseRelease`/`MouseMove`/`MouseDoubleClick`/`FocusIn`/`FocusOut`/`PaintEvent`/`ChangeEvent`、`EXObject_TimerEvent`、`EXClass_Copy`/`Move`/`Deinit`；父类入口 `XWidget_*_base` 声明于 `XWidget_Protected.h:121-167`。
- **注意事项与坑点**：
  1. 壳-控制器分工是硬约束：头注「编辑逻辑（文本缓冲、光标/选区、撤销栈、回显状态机、输入掩码、校验、IME、剪贴板、命中测试、键盘分派、补全联动与绘制数据）全部由 m_control 指向的 XLineControl 持有」（`XLineEdit.h:143-148`）；`XLINECONTROL_ON=0` 时直接 `#error`（`XLineEdit.h:66-68`）。
  2. 结构体首成员名为 `m_class` 而非 `m_base`（`XLineEdit.h:167`），且实现文件全篇不出现 `m_base`/`m_class` 字样（`rg -o "m_base" Src/XGui/Widget/XLineEdit.c` = 0 行）；这与同批其他类统一的 `m_base` 命名不一致，跨类强转/取址代码需注意。
  3. 头文件宣称「超长截断到 63 字符」（`XLineEdit.h:319`），但 `XLineEdit_setPlaceholderText` 实现只做 `XString_assign_utf8` 全量赋值、无任何 63 字符截断（`XLineEdit.c:2004-2012`）——头注与实现矛盾。
  4. 长文本越界曾是用户实测缺陷：`XGui.md:316-322`「根因：VXLineEdit_paintEvent 调 XLineControl_draw **无任何裁剪**」，修复为 painter 级 IntersectClip + 控制器级 clip 两处（§8.0c3）。
  5. `setSelection` 的 start 越界不钳位而是「告警并忽略整个调用」（`XLineEdit.h:573-574`；`XLineEdit.c:2384-2396` 亦为 `if (start < 0) return;` 与 `(size_t)start > 字符数 → return` 两处直接返回），与同文件 `setCursorPosition` 的钳位语义不同（`XLineEdit.h:470-472`）。
  6. 程序化编辑不受 readOnly 限制：`insert`/`backspace`/`del`/`paste` 均「与 Qt 一致不设 readOnly 门禁」（`XLineEdit.h:309-310`、`522-523`、`531`、`680-681`）。
  7. `XLineControl_setFont` 曾以裸 `XMalloc_System+XClassCopy` 深拷贝却不继承堆所有权位，逐替换泄漏 880B（`XGui.md:564-570`）。
  8. `XLineEdit_cursorPositionChanged_signal` 的两个位置参数头注标为「仅占位，本实现不用于发射判定」（`XLineEdit.h:833-835`）。
- **测试覆盖**：`Test/XGuiTest/XLineEditTest.c` 存在，入口 `bool XLineEditTest_runAll(void)`（第 56 行），断言宏 `le_expect` 定义于第 19 行；`le_expect(` 全文件出现 31 次（`rg -o "le_expect\(" | wc -l` = 31），其中 1 次为定义 → 调用点 30 处。已注册于 `Test/CMakeLists.txt:554`。

## XListView

- **定位**：以 m_base 组合继承 XAbstractItemView 的列表视图，按单列垂直行模型渲染条目，状态族对标 QListView 同名属性；继承链 `XObject←XWidget←XFrame←XAbstractScrollArea←XAbstractItemView←XListView`（`XListView.h:114-115`，基链见 `XFrame.h:103`、`XAbstractScrollArea.h:52-54`、`XAbstractItemView.h:167-170`）；头文件 `Src/XGui/Widget/XListView.h`（422 行）。
- **生命周期**：`XListView_init` + `XListView_create` 宏 / `XListView_create_ex`，`XClassDelete` 释放；`deinit` 重载释放行隐藏状态表（`XListView.c:475`）。
- **API 面**：
  - 生命周期：`XVtable* XListView_class_init(void)` | 单例虚表，重载 deinit/PaintEvent/IndexAt/VisualRect/ScrollContentsBy | `QListView`（itemviews/qlistview.h:16） | 借用禁释放
  - `void XListView_init(XListView* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | `QListView(QWidget*)`（qlistview.h:46） | 无
  - `XListView* XListView_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 属性：`void XListView_setSpacing(XListView* self, int spacing)` | 设条目间距（>=0） | `setSpacing`（qlistview.h:64） | 无
  - `int XListView_spacing(const XListView* self)` | 查询间距 | `spacing`（qlistview.h:65） | 借用
  - `void XListView_setModelColumn(XListView* self, int column)` | 设渲染列（拒绝负值） | `setModelColumn`（qlistview.h:81） | 无
  - `int XListView_modelColumn(const XListView* self)` | 查询渲染列 | `modelColumn`（qlistview.h:82） | 借用
  - `void XListView_setRowHeight(XListView* self, int height)` | 设统一行高（项目库扩展，>0） | X 扩展 | 无
  - `int XListView_rowHeight(const XListView* self)` | 查询行高 | X 扩展 | 借用
  - 几何查询：`XRect XListView_visualRect(const XListView* self, int row)` | 行视觉矩形（越界/隐藏返回全零） | `visualRect`（qlistview.h:96） | 按值返回
  - 状态族：`void XListView_setFlow(XListView* self, int flow)` | 设排列方向（非法忽略） | `setFlow`（qlistview.h:52） | 无
  - `int XListView_flow(const XListView* self)` | 查询排列方向（无效返回 0=LeftToRight） | `flow`（qlistview.h:53） | 借用
  - `void XListView_setGridSize(XListView* self, int width, int height)` | 设网格尺寸（<=0 存 -1） | `setGridSize`（qlistview.h:70） | 无
  - `void XListView_gridSize(const XListView* self, int* width, int* height)` | 双输出查询（未启用输出 -1） | `gridSize`（qlistview.h:71） | 输出参数借用
  - `void XListView_setWrapping(XListView* self, bool enable)` | 设换行排布（仅存储） | `setWrapping`（qlistview.h:55） | 无
  - `bool XListView_isWrapping(const XListView* self)` | 查询换行 | `isWrapping`（qlistview.h:56） | 借用
  - `void XListView_setViewMode(XListView* self, int mode)` | 设视图模式并联动 wrapping/flow | `setViewMode`（qlistview.h:73） | 无
  - `int XListView_viewMode(const XListView* self)` | 查询视图模式（无效返回 0=ListMode） | `viewMode`（qlistview.h:74） | 借用
  - `void XListView_setResizeMode(XListView* self, int mode)` | 设尺寸调整模式（仅状态） | `setResizeMode`（qlistview.h:58） | 无
  - `int XListView_resizeMode(const XListView* self)` | 查询（无效返回 0=Fixed） | `resizeMode`（qlistview.h:59） | 借用
  - `void XListView_setLayoutMode(XListView* self, int mode)` | 设布局模式（仅状态） | `setLayoutMode`（qlistview.h:61） | 无
  - `int XListView_layoutMode(const XListView* self)` | 查询（无效返回 0=SinglePass） | `layoutMode`（qlistview.h:62） | 借用
  - `void XListView_setMovement(XListView* self, int movement)` | 设移动模式（仅存储+重绘） | `setMovement`（qlistview.h:49） | 无
  - `int XListView_movement(const XListView* self)` | 查询（无效返回 0=Static） | `movement`（qlistview.h:50） | 借用
  - `void XListView_setBatchSize(XListView* self, int batchSize)` | 设批式批量（<=0 忽略） | `setBatchSize`（qlistview.h:67） | 无
  - `int XListView_batchSize(const XListView* self)` | 查询（无效返回 0） | `batchSize`（qlistview.h:68） | 借用
  - `void XListView_setUniformItemSizes(XListView* self, bool enable)` | 均匀条目尺寸声明（仅存储） | `setUniformItemSizes`（qlistview.h:84） | 无
  - `bool XListView_uniformItemSizes(const XListView* self)` | 查询 | `uniformItemSizes`（qlistview.h:85） | 借用
  - `void XListView_clearPropertyFlags(XListView* self)` | 清零模式属性标志 | `clearPropertyFlags`（qlistview.h:76） | 无
  - `void XListView_setItemAlignment(XListView* self, int alignment)` | 设条目对齐位掩码 | `setItemAlignment`（qlistview.h:93） | 无
  - `int XListView_itemAlignment(const XListView* self)` | 查询对齐掩码 | `itemAlignment`（qlistview.h:94） | 借用
  - `void XListView_setSelectionRectVisible(XListView* self, bool show)` | 橡皮筋框可见（仅存储） | `setSelectionRectVisible`（qlistview.h:90） | 无
  - `bool XListView_isSelectionRectVisible(const XListView* self)` | 查询 | `isSelectionRectVisible`（qlistview.h:91） | 借用
  - `void XListView_setWordWrap(XListView* self, bool on)` | 按词换行（参与文本绘制） | `setWordWrap`（qlistview.h:87） | 无
  - `bool XListView_wordWrap(const XListView* self)` | 查询 | `wordWrap`（qlistview.h:88） | 借用
  - `void XListView_setRowHidden(XListView* self, int row, bool hide)` | 设行隐藏（平行数组随模型行数同步） | `setRowHidden`（qlistview.h:79） | 无
  - `bool XListView_isRowHidden(const XListView* self, int row)` | 查询行隐藏 | `isRowHidden`（qlistview.h:78） | 借用
  - 信号：`void* XListView_indexesMoved_signal(XListView* self, const int* rows, int count)` | indexesMoved 句柄（真实发射点缺失，可手工调用） | `indexesMoved`（qlistview.h:105） | rows 借用（仅发射期有效）
- **所有权与命名契约**：`m_rowHidden` 表「对象拥有」由 deinit 释放（`XListView.h:122`）；全部 getter 无 `_const` 后缀但收 `const XListView*`；设置侧无 `_utf8`；`gridSize` 为双指针输出替代 QSize（`XListView.h:250-260`）；无 `_2` 重载；三态 set* 无。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XListView)` + `XCLASS_DEFINE_EXTEND_END(XListView, XAbstractItemView)`（`XListView.h:114-115`）；`XListView.c:474-481` 重载 `EXClass_Deinit`、`EXWidget_PaintEvent`、`EXAbstractItemView_IndexAt`、`EXAbstractItemView_VisualRect`、`EXAbstractScrollArea_ScrollContentsBy`；父类入口 `XAbstractItemView_indexAt_base`（`XAbstractItemView.h:1220`）、`XAbstractScrollArea_scrollContentsBy_base`（`XAbstractScrollArea_Protected.h:31`）。
- **注意事项与坑点**：
  1. 头注称 `setMovement`「不实现 Qt 的 modeProperties 门控（clearPropertyFlags 为无操作，setViewMode 不联动 movement）」（`XListView.h:313-316`），但实现里 `XListView_clearPropertyFlags` 明确清 `m_modeProperties`（`XListView.c:754-759`），且 `setMovement` 置 `XLISTVIEW_PROP_MOVEMENT` 位（`XListView.c:713-722`）、`setViewMode` 按位门控 movement（`XListView.c:659`、`XListView.c:670`）——头注与实现矛盾，实现口径与源码注释自称的「对标 qlistview.cpp:422-471」一致。
  2. 头注称 `setViewMode` 切换时「spacing/gridSize 等其余模式属性 XGui 不自动复位」（`XListView.h:274-276`），实现只复位 wrap/flow/movement/resizeMode/selectionRectVisible 五项（`XListView.c:651-676`），与该注释一致。
  3. `visualRect` 的隐藏行语义：头注「隐藏行按默认可见口径查询（不触发状态表同步）」（`XListView.h:220-222`），但 `setRowHidden` 会主动 `xlv_refreshRowStates` 同步（`XListView.c:795-799`），两者对「何时同步」的表述不同。
  4. paintEvent 曾缺 `XWidget_paintOffset` 平移，「paintImage 返回顶层后备存储，控件非零偏移时内容直绘窗口 (0,0)」（`XGui.md:693-699`、`1830-1832`），XListWidget 继承同槽一并受影响。
  5. 横向流式（LeftToRight/wrapping）重排布局未接，`visualRect` 恒为 TopDown 槽位几何（`XListView.h:222-224`）。
  6. 几何口径：`visualRect` 的 y 按「max(行高, 网格高) + spacing」累计（`XListView.h:213-215`），网格宽仅在小于视口宽时收缩；默认行高 24、batchSize 100（`XListView.c:26-27`）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XListViewTest.c` 不存在，`ls` 报「没有那个文件或目录」）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_views.c` 引用 `XListView` 5 处（含 `#include "XListView.h"`，第 28 行），`Test/XGuiDemo/xgui_demo_page_views.c` 同页演示。

## XListWidget

- **定位**：在 XListView 之上持内建单列条目模型并封装 addItem/takeItem/findItems/sortItems/行级部件挂载与 10 个条目信号；继承链 `XObject←XWidget←XFrame←XAbstractScrollArea←XAbstractItemView←XListView←XListWidget`（`XListWidget.h:40-41`）；头文件 `Src/XGui/Widget/XListWidget.h`（442 行）。
- **生命周期**：`XListWidget_init` 建内建模型并连接 dataChanged 桥；`XListWidget_create` 宏 / `XListWidget_create_ex`，`XClassDelete` 释放；`deinit` 释放承载表与模型（`XListWidget.c:193`）。
- **API 面**：
  - 生命周期：`XVtable* XListWidget_class_init(void)` | 单例虚表，重载 deinit 与鼠标按下/释放/双击/移动、按键槽 | `QListWidget`（itemviews/qlistwidget.h:163） | 借用禁释放
  - `void XListWidget_init(XListWidget* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | `QListWidget(QWidget*)`（qlistwidget.h:174） | 无
  - `XListWidget* XListWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 条目：`int XListWidget_addItem(XListWidget* self, const XString* text)` | 追加条目 | `addItem(QString)`（qlistwidget.h:184） | text 借用
  - `int XListWidget_addItem_2(XListWidget* self, const char* text)` | UTF-8 重载 | 同上 | text 借用
  - `int XListWidget_addItems(XListWidget* self, const char* const* texts, int count)` | 批量追加 | `addItems`（qlistwidget.h:186） | texts 借用
  - `int XListWidget_insertItem(XListWidget* self, int row, const XString* text)` | 行内插入（负数最前、超出追加） | `insertItem`（qlistwidget.h:181） | text 借用
  - `int XListWidget_insertItem_2(XListWidget* self, int row, const char* text)` | UTF-8 重载 | 同上 | text 借用
  - `int XListWidget_insertItems(XListWidget* self, int index, const char* const* texts, int count)` | 批量插入 | `insertItems`（qlistwidget.h:183） | texts 借用
  - `int XListWidget_count(const XListWidget* self)` | 条目数 | `count`（qlistwidget.h:188） | 借用
  - `const XString* XListWidget_item(const XListWidget* self, int row)` | 条目文本借用 | `item`（qlistwidget.h:179） | 借用禁释放
  - `const char* XListWidget_item_2(const XListWidget* self, int row)` | 条目文本 UTF-8 借用 | 同上 | 借用禁释放
  - `XString* XListWidget_item_new(const XListWidget* self, int row)` | 条目文本副本 | X 扩展（`item` 的独立持有形态） | 拷贝须 `XClassDelete`
  - `XString* XListWidget_takeItem(XListWidget* self, int row)` | 取出条目并归还文本所有权 | `takeItem`（qlistwidget.h:187） | 拷贝须 `XClassDelete`
  - `void XListWidget_clear(XListWidget* self)` | 清空全部条目并释放承载表 | `clear`（qlistwidget.h:230） | 无
  - 当前条目：`int XListWidget_currentItem(const XListWidget* self)` | 当前行号（无则 -1） | `currentItem`（qlistwidget.h:190） | 借用
  - `void XListWidget_setCurrentItem(XListWidget* self, int row)` | 设当前条目并滚动 | `setCurrentItem`（qlistwidget.h:191） | 无
  - `int XListWidget_currentRow(const XListWidget* self)` | 当前行 | `currentRow`（qlistwidget.h:194） | 借用
  - `void XListWidget_setCurrentRow(XListWidget* self, int row)` | 设当前行（-1 清除） | `setCurrentRow`（qlistwidget.h:195） | 无
  - `void XListWidget_scrollToItem(XListWidget* self, int row)` | 滚动至可见 | `scrollToItem`（qlistwidget.h:229） | 无
  - 选中：`int XListWidget_selectedItems(const XListWidget* self, int* outRows, int maxCount)` | 选中行导出 | `selectedItems`（qlistwidget.h:216） | outRows 调用方分配
  - 查找/排序/几何：`int XListWidget_findItems(const XListWidget* self, const char* text, int flags, int* outRows, int maxCount)` | 按文本查找（1=精确、0=包含） | `findItems`（qlistwidget.h:217） | text 借用
  - `void XListWidget_sortItems(XListWidget* self, int order)` | 整行排序（0=升、1=降），隐式开启排序 | `sortItems`（qlistwidget.h:202） | 无
  - `bool XListWidget_isSortingEnabled(const XListWidget* self)` | 排序使能查询 | `isSortingEnabled`（qlistwidget.h:204） | 借用
  - `void XListWidget_setSortingEnabled(XListWidget* self, bool enable)` | 设排序使能（开启即排一次，此后插入自动排） | `setSortingEnabled`（qlistwidget.h:203） | 无
  - `int XListWidget_itemAt(const XListWidget* self, int x, int y)` | 位置反查行号 | `itemAt(int,int)`（qlistwidget.h:199） | 借用
  - `XRect XListWidget_visualItemRect(const XListWidget* self, int row)` | 条目可视矩形 | `visualItemRect`（qlistwidget.h:200） | 按值返回
  - 索引反查/编辑：`int XListWidget_indexFromItem(const XListWidget* self, int row)` | 行→索引（恒等映射） | `indexFromItem`（qlistwidget.h:221） | 借用
  - `const XString* XListWidget_itemFromIndex(const XListWidget* self, int row)` | 索引→行文本借用 | `itemFromIndex`（qlistwidget.h:222） | 借用禁释放
  - `int XListWidget_row(const XListWidget* self, const char* text)` | 文本反查行号（精确、区分大小写） | `row`（qlistwidget.h:180） | text 借用
  - `void XListWidget_editItem(XListWidget* self, int row)` | 编辑触发（无编辑器，当前为无操作） | `editItem`（qlistwidget.h:206） | 无
  - 行级部件挂载：`void XListWidget_setItemWidget(XListWidget* self, int row, XWidget* widget)` | 挂载行级部件（列表不取得所有权） | `setItemWidget`（qlistwidget.h:213） | widget 借用禁释放
  - `XWidget* XListWidget_itemWidget(const XListWidget* self, int row)` | 读取挂载部件 | `itemWidget`（qlistwidget.h:212） | 借用禁释放
  - `void XListWidget_removeItemWidget(XListWidget* self, int row)` | 摘除挂载（仅置 NULL） | `removeItemWidget`（qlistwidget.h:214） | 无
  - 信号：`void* XListWidget_currentItemChanged_signal(XListWidget* self, int current, int previous)` | 当前条目变化 | `currentItemChanged`（qlistwidget.h:240） | 借用禁释放
  - `void* XListWidget_itemSelectionChanged_signal(XListWidget* self)` | 选择集合变化 | `itemSelectionChanged`（qlistwidget.h:244） | 借用禁释放
  - `void* XListWidget_currentRowChanged_signal(XListWidget* self, int current, int previous)` | 当前行变化 | `currentRowChanged`（qlistwidget.h:242） | 借用禁释放
  - `void* XListWidget_currentTextChanged_signal(XListWidget* self, const char* text)` | 当前行文本变化 | `currentTextChanged`（qlistwidget.h:241） | text 借用
  - `void* XListWidget_itemActivated_signal(XListWidget* self, int row)` | 条目激活 | `itemActivated`（qlistwidget.h:236） | 借用禁释放
  - `void* XListWidget_itemChanged_signal(XListWidget* self, int row)` | 条目数据变化 | `itemChanged`（qlistwidget.h:238） | 借用禁释放
  - `void* XListWidget_itemClicked_signal(XListWidget* self, int row)` | 条目点击 | `itemClicked`（qlistwidget.h:234） | 借用禁释放
  - `void* XListWidget_itemDoubleClicked_signal(XListWidget* self, int row)` | 条目双击 | `itemDoubleClicked`（qlistwidget.h:235） | 借用禁释放
  - `void* XListWidget_itemEntered_signal(XListWidget* self, int row)` | 鼠标进入新行 | `itemEntered`（qlistwidget.h:237） | 借用禁释放
  - `void* XListWidget_itemPressed_signal(XListWidget* self, int row)` | 条目按下 | `itemPressed`（qlistwidget.h:233） | 借用禁释放
  - `XAbstractItemModel* XListWidget_model(const XListWidget* self)` | 内建条目模型 | `QListWidget::model`（继承自 QAbstractItemView；无独立行号） | 借用禁释放
- **所有权与命名契约**：`m_model` 与 `m_rowWidgets` 表本身对象拥有、表内部件借用（`XListWidget.h:48-53`）；`item`/`item_2`/`itemFromIndex` 借用，`item_new`/`takeItem` 拷贝须 `XClassDelete`；`_2` 重载为 `addItem_2`/`insertItem_2`/`item_2`；UTF-8 设置侧后缀沿用 `_2` 而非 `_utf8`；三态 set* 无。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XListWidget)` + `XCLASS_DEFINE_EXTEND_END(XListWidget, XListView)`（`XListWidget.h:40-41`）；`XListWidget.c:192-203` 重载 `EXClass_Deinit`、`EXWidget_MousePressEvent`、`MouseReleaseEvent`、`MouseDoubleClickEvent`、`MouseMoveEvent`、`KeyPressEvent`，命中经 `XAbstractItemView_indexAt_base` 回基类（`XListWidget.c:678/801/833/860/885`）。
- **注意事项与坑点**：
  1. `sortItems` 会隐式开启 sortingEnabled：源码注释「对标 Qt sortItems:显式排序隐式开启 sortingEnabled」（`XListWidget.c:592`），头文件 `XListWidget.h:247-256` 的 `setSortingEnabled` 注则把「开启」描述为显式调用的效果，两者需并读。
  2. 排序稳定但实现为插入排序（`XListWidget.c:656` 附近注释「稳定插入排序：升序右移较大者、降序右移较小者，相等不动」），大规模条目为 O(n²)。
  3. `m_dataGuard` 用于抑制内部批量改写触发 itemChanged（`XListWidget.h:67-70`），因此外部若依赖逐行 itemChanged 观察插入/排序写入会漏事件。
  4. `editItem` 是句柄预留：头注「本控件尚无编辑器/委托（editor/delegate）机制，编辑触发无从落地，当前为校验入参后的无操作」（`XListWidget.h:300-307`）。
  5. itemEntered 的差分判重沿用 `m_enteredRow`、初值 -2 哨兵，「-2 初值同 XListWidget 口径」（`XGui.md:829`）；事件投递依赖窗口层移动事件派发（`XListWidget.h:416-419`）。
  6. `takeItem`/`clear` 会发 currentItemChanged 与 itemSelectionChanged（`XListWidget.h:345-346`、`353-355`），宿主需容忍「条目被移除触发的当前项失效」信号。
  7. paintOffset 缺失缺陷同样命中本类（继承 XListView 同槽，`XGui.md:693-699` 明写「XListWidget 继承同槽一并修复」）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XListWidgetTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_views.c` 引用 6 处并在第 428-441 行装配「XListWidget 七信号」，`Test/XGuiDemo/xgui_demo_apitest_text.c`（补全器弹层用 XListWidget）与 `xgui_demo_apitest_input.c` 亦有引用。

## XMainWindow

- **定位**：主窗口容器，编排菜单栏/工具栏/中央控件/停靠区/状态栏布局并持久化停靠几何；继承链 `XObject←XWidget←XMainWindow`（`XMainWindow.h:65-66`）；头文件 `Src/XGui/Widget/XMainWindow.h`（628 行）。
- **生命周期**：`XMainWindow_init` + `XMainWindow_create` 宏 / `XMainWindow_create_ex`，`XClassDelete` 释放；`deinit` 重载（`XMainWindow.c:782`）；内部惰性创建的内置菜单栏/状态栏由 `m_menuBarOwned`/`m_statusBarOwned` 区分归属（`XMainWindow.h:140-141`）。
- **API 面**：
  - 生命周期：`XVtable* XMainWindow_class_init(void)` | 单例虚表，重载 ResizeEvent 与 Deinit | `QMainWindow`（qmainwindow.h:24） | 借用禁释放
  - `void XMainWindow_init(XMainWindow* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | `QMainWindow(QWidget*,WindowFlags)`（qmainwindow.h:59） | 无
  - `XMainWindow* XMainWindow_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 菜单栏与状态栏：`XWidget* XMainWindow_menuBar(XMainWindow* self)` | 返回内置菜单栏（不存在则惰性创建） | `menuBar`（qmainwindow.h:91） | 借用（内部创建者为拥有）
  - `void XMainWindow_setMenuBar(XMainWindow* self, XWidget* menuBar)` | 设外部菜单栏 | `setMenuBar`（qmainwindow.h:92） | 外部控件借用
  - `XWidget* XMainWindow_menuWidget(const XMainWindow* self)` | 查询菜单栏位置控件（不惰性创建） | `menuWidget`（qmainwindow.h:94） | 借用
  - `void XMainWindow_setMenuWidget(XMainWindow* self, XWidget* menuWidget)` | 设菜单栏位置控件 | `setMenuWidget`（qmainwindow.h:95） | 借用
  - `XMenu* XMainWindow_createPopupMenu(XMainWindow* self)` | 新建停靠/工具栏右键菜单 | `createPopupMenu`（qmainwindow.h:153） | 所有权归调用方，须 `XClassDelete`
  - `XWidget* XMainWindow_statusBar(XMainWindow* self)` | 返回内置状态栏（惰性创建） | `statusBar`（qmainwindow.h:99） | 借用（内部创建者为拥有）
  - `void XMainWindow_setStatusBar(XMainWindow* self, XWidget* statusBar)` | 设外部状态栏 | `setStatusBar`（qmainwindow.h:100） | 借用
  - 中央控件与工具栏：`void XMainWindow_setCentralWidget(XMainWindow* self, XWidget* widget)` | 设中央控件 | `setCentralWidget`（qmainwindow.h:104） | 借用
  - `XWidget* XMainWindow_centralWidget(const XMainWindow* self)` | 查询中央控件 | `centralWidget`（qmainwindow.h:103） | 借用
  - `XWidget* XMainWindow_takeCentralWidget(XMainWindow* self)` | 摘取中央控件 | `takeCentralWidget`（qmainwindow.h:106） | 引用转移（归还调用方）
  - `void XMainWindow_addToolBar(XMainWindow* self, int area, XWidget* toolbar)` | 按区域加工具栏 | `addToolBar(Qt::ToolBarArea,QToolBar*)`（qmainwindow.h:117） | 借用
  - `void XMainWindow_addToolBar_2(XMainWindow* self, XWidget* toolbar)` | 无区域重载（缺省 Top） | `addToolBar(QToolBar*)`（qmainwindow.h:118） | 借用
  - `XWidget* XMainWindow_addToolBar_3(XMainWindow* self, const char* utf8Title)` | 以标题创建工具栏 | `addToolBar(const QString&)`（qmainwindow.h:119） | 新建归主窗口
  - `int XMainWindow_iconSize(const XMainWindow* self)` | 全局图标方边像素（默认 16） | `iconSize`（qmainwindow.h:62） | 借用
  - `void XMainWindow_setIconSize(XMainWindow* self, int size)` | 设图标尺寸并同步全部工具栏 | `setIconSize`（qmainwindow.h:63） | 无
  - `void XMainWindow_insertToolBarBreak(XMainWindow* self, XWidget* before)` | 在 before 前插入断行 | `insertToolBarBreak`（qmainwindow.h:115） | before 借用
  - `void XMainWindow_removeToolBarBreak(XMainWindow* self, XWidget* before)` | 移除断行 | `removeToolBarBreak`（qmainwindow.h:122） | before 借用
  - `bool XMainWindow_toolBarBreak(const XMainWindow* self, const XWidget* toolbar)` | 查询断行 | `toolBarBreak`（qmainwindow.h:127） | toolbar 借用
  - `void XMainWindow_setToolButtonStyle(XMainWindow* self, int toolButtonStyle)` | 设全局工具按钮样式 | `setToolButtonStyle`（qmainwindow.h:66） | 无
  - `int XMainWindow_toolButtonStyle(const XMainWindow* self)` | 查询样式（默认 IconOnly） | `toolButtonStyle`（qmainwindow.h:65） | 借用
  - 停靠面板：`void XMainWindow_addDockWidget(XMainWindow* self, int area, XWidget* dock)` | 停靠到指定区域 | `addDockWidget`（qmainwindow.h:131） | dock 借用
  - `void XMainWindow_addDockWidget_2(XMainWindow* self, int area, XWidget* dock, int orientation)` | 带跨向拆分重载 | `addDockWidget(area,dock,orientation)`（qmainwindow.h:132-133） | dock 借用
  - `void XMainWindow_removeDockWidget(XMainWindow* self, XWidget* dock)` | 摘除停靠面板 | `removeDockWidget`（qmainwindow.h:140） | dock 借用
  - `void XMainWindow_setDockOptions(XMainWindow* self, int options)` | 设停靠选项位 | `setDockOptions`（qmainwindow.h:85） | 无
  - `int XMainWindow_dockOptions(const XMainWindow* self)` | 查询停靠选项 | `dockOptions`（qmainwindow.h:86） | 借用
  - `void XMainWindow_splitDockWidget(XMainWindow* self, XDockWidget* after, XDockWidget* dock, int orientation)` | 相邻切分（简化为登记顺序/区域调整） | `splitDockWidget`（qmainwindow.h:134-135） | 两者借用
  - `void XMainWindow_tabifyDockWidget(XMainWindow* self, XDockWidget* first, XDockWidget* second)` | 编入同一标签组 | `tabifyDockWidget`（qmainwindow.h:137） | 两者借用
  - `const XVector* XMainWindow_tabifiedDockWidgets(const XMainWindow* self, const XDockWidget* dock)` | 同组面板借用数组（含自身、仅组内 >=2 时非 NULL） | `tabifiedDockWidgets`（qmainwindow.h:138） | 借用禁释放、随登记变化失效
  - `bool XMainWindow_restoreDockWidget(XMainWindow* self, XDockWidget* dock)` | 恢复可见性/浮动状态 | `restoreDockWidget`（qmainwindow.h:141） | dock 借用
  - `void XMainWindow_resizeDocks(XMainWindow* self, XDockWidget** docks, const int* sizes, int count, int orientation)` | 按尺寸调整停靠 | `resizeDocks`（qmainwindow.h:145-146） | docks/sizes 借用
  - `bool XMainWindow_isSeparator(const XMainWindow* self, const XPoint* pos)` | 坐标是否落在停靠分隔带（边界 ±2px） | `isSeparator`（qmainwindow.h:88） | pos 借用
  - 布局 API：`int XMainWindow_toolBarArea(const XMainWindow* self, const XWidget* toolbar)` | 查询工具栏区域（未登记 0） | `toolBarArea`（qmainwindow.h:126） | 借用
  - `int XMainWindow_dockWidgetArea(const XMainWindow* self, const XWidget* dock)` | 查询停靠面板区域 | `dockWidgetArea`（qmainwindow.h:143） | 借用
  - `void XMainWindow_setDocumentMode(XMainWindow* self, bool enable)` | 文档模式 | `setDocumentMode`（qmainwindow.h:75） | 无
  - `bool XMainWindow_documentMode(const XMainWindow* self)` | 查询文档模式 | `documentMode`（qmainwindow.h:74） | 借用
  - `void XMainWindow_setAnimated(XMainWindow* self, bool enable)` | 停靠动画 | `setAnimated`（qmainwindow.h:158） | 无
  - `bool XMainWindow_isAnimated(const XMainWindow* self)` | 查询动画 | `isAnimated`（qmainwindow.h:69） | 借用
  - `void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enable)` | 停靠嵌套 | `setDockNestingEnabled`（qmainwindow.h:159） | 无
  - `bool XMainWindow_isDockNestingEnabled(const XMainWindow* self)` | 查询嵌套 | `isDockNestingEnabled`（qmainwindow.h:70） | 借用
  - `void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self, bool enable)` | 统一标题栏（属性存储） | `setUnifiedTitleAndToolBarOnMac`（qmainwindow.h:162） | 无
  - `bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self)` | 查询统一标题栏 | `unifiedTitleAndToolBarOnMac`（qmainwindow.h:124） | 借用
  - `void XMainWindow_setTabPosition(XMainWindow* self, int areas, int position)` | 按区域掩码写页签位置 | `setTabPosition`（qmainwindow.h:82） | 无
  - `int XMainWindow_tabPosition(const XMainWindow* self, int area)` | 按区读页签位置 | `tabPosition`（qmainwindow.h:81） | 借用
  - `void XMainWindow_setTabShape(XMainWindow* self, int shape)` | 页签形状 | `setTabShape`（qmainwindow.h:80） | 无
  - `int XMainWindow_tabShape(const XMainWindow* self)` | 查询页签形状 | `tabShape`（qmainwindow.h:79） | 借用
  - `void XMainWindow_setCorner(XMainWindow* self, int corner, int area)` | 角控件所属区域 | `setCorner`（qmainwindow.h:109） | 无
  - `int XMainWindow_corner(const XMainWindow* self, int corner)` | 查询角控件区域 | `corner`（qmainwindow.h:110） | 借用
  - `void XMainWindow_addToolBarBreak(XMainWindow* self, int area)` | 工具栏断点 | `addToolBarBreak`（qmainwindow.h:114） | 无
  - `void XMainWindow_insertToolBar(XMainWindow* self, XWidget* before, XWidget* toolbar)` | 插入工具栏 | `insertToolBar`（qmainwindow.h:120） | 两者借用
  - `void XMainWindow_removeToolBar(XMainWindow* self, XWidget* toolbar)` | 移除工具栏（不删除对象） | `removeToolBar`（qmainwindow.h:121） | 借用
  - `XString* XMainWindow_saveState(XMainWindow* self, int version)` | 布局快照 "XMWSTATE:2;" | `saveState`（qmainwindow.h:149） | 拷贝须 `delete_base`
  - `bool XMainWindow_restoreState(XMainWindow* self, const XString* state, int version)` | 回放快照 | `restoreState`（qmainwindow.h:150） | state 借用
  - 信号：`void* XMainWindow_iconSizeChanged_signal(XMainWindow* self, int width, int height)` | 图标尺寸变化并发射 | `iconSizeChanged`（qmainwindow.h:166） | 借用禁释放
  - `void* XMainWindow_toolButtonStyleChanged_signal(XMainWindow* self, int toolButtonStyle)` | 按钮样式变化并发射 | `toolButtonStyleChanged`（qmainwindow.h:167） | 借用禁释放
  - `void* XMainWindow_tabifiedDockWidgetActivated_signal(XMainWindow* self, XWidget* dockWidget)` | 标签面板激活并发射 | `tabifiedDockWidgetActivated`（qmainwindow.h:169） | 借用禁释放
  - 别名宏 `XMainWindow_unifiedTitleAndToolBarOnMac`（`XMainWindow.h:508`） | 等价 `XMainWindow_isUnifiedTitleAndToolBarOnMac` | `unifiedTitleAndToolBarOnMac`（qmainwindow.h:124） | 无
- **所有权与命名契约**：`m_menuBar`/`m_statusBar` 归属由 `m_menuBarOwned`/`m_statusBarOwned` 位标记（`XMainWindow.h:140-141`）；`m_central`/`m_toolBars`/`m_docks` 中元素均借用（`XMainWindow.h:74-78`）；`m_dockTabBarPool` 池中页签条仍是主窗口子控件、向量只持指针（`XMainWindow.h:99-105`）；`_2` 重载 `addToolBar_2`/`addDockWidget_2`；无 `_utf8`/`_const` 命名族；三态 set* 无。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XMainWindow)` + `XCLASS_DEFINE_EXTEND_END(XMainWindow, XWidget)`（`XMainWindow.h:65-66`）；`XMainWindow.c:776-783` 仅重载 `EXWidget_ResizeEvent`（触发重排）与 `EXClass_Deinit`；另有内部接口 `XMainWindow_updateDockLayout`/`hoverDrop`/结束落放见 `XMainWindow_Protected.h`（不属公开覆写点）。
- **注意事项与坑点**：
  1. 停靠体系为简化模型：头注「四个停靠区（左/右两条停靠列 + Top/Bottom 两条停靠行）……标签组只记录分组与活动面板，不绘制真实标签条」（`XMainWindow.h:22-28`），但同头又新增 `m_dockTabBars`（标签组页签条，`XMainWindow.h:87-90`）——两处对「是否绘制标签条」的描述不一致。
  2. `splitDockWidget` 头注明标与 Qt 差异：「XGui 停靠体系没有嵌套布局与分隔条，无法把区域真正二分，本接口只调整登记区域与顺序并显示面板」（`XMainWindow.h:363-365`），且同句称「Top/Bottom 区域不参与几何布局」，与文件头「Top/Bottom 两条停靠行参与几何布局」（`XMainWindow.h:22-26`）相抵。
  3. `m_tabShape`/`m_tabPositions[4]` 的页签位置是逐区而非全局单值（`XMainWindow.h:134-138`，对标 Qt `QMainWindowLayout::tabPositions[]`）。
  4. 断行表示为哨兵条目（NULL 工具栏 + 区域 0），头注明言「不额外维护平行标志数组，避免……形成双份事实来源」（`XMainWindow.h:265-268`）；同时 `toolBarBreak` 的差异注登记「简化布局把每个顶部工具栏各放一行，断行只影响登记顺序与 toolBarBreak 查询结果，不改变几何」（`XMainWindow.h:282-283`）。
  5. `saveState` 格式为自研 "XMWSTATE:2;"（`XMainWindow.h:561-573`），非 Qt QByteArray 流格式，不可与 Qt 快照互通；`restoreState` 对 NULL state「视为重置，当前无可重置字段，直接返回 true」（`XMainWindow.h:582-583`）。
  6. 拖放指示器为惰性创建的 XRubberBand 子控件，注释记录「创建一次后跨拖拽复用」（`XMainWindow.h:92-98`）；页签条池的存在理由是「拖放回调链内删除会经析构级联触发事件分发崩溃」（`XMainWindow.h:99-105`）。
  7. `XMainWindow.h:461-465` 有一条 `XMainWindow_setSeparator` 的悬空文档注释（其后无任何声明），且 `XMainWindow.h:153-154` 注释说明该位已删：「末位 m_separator 已随冗余 API setSeparator 删除——该位只写不读，无任何行为，纯冗余」——文档与实现不同步。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XMainWindowTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_containers.c` 引用 71 处、`Test/XGuiDemo/xgui_demo_page_window.c` 演示页。

## XMdiArea

- **定位**：多文档界面区域 `XMdiArea`（继承 XAbstractScrollArea）与子窗口 `XMdiSubWindow`（继承 XWidget）两个类同头定义，覆盖添加/激活/层叠/平铺/视图模式；继承链 `XObject←XWidget←XFrame←XAbstractScrollArea←XMdiArea`（`XMdiArea.h:233-234`）与 `XObject←XWidget←XMdiSubWindow`（`XMdiArea.h:82-83`）；头文件 `Src/XGui/Widget/XMdiArea.h`（419 行）。
- **生命周期**：`XMdiArea_init`/`XMdiSubWindow_init` + 各自 `_create` 宏与 `_create_ex`，`XClassDelete` 释放；`XMdiArea_addSubWindow` 创建的子窗口归区域（`m_subWindows` 拥有，`XMdiArea.h:240`），内容控件归子窗口（`XMdiArea.h:13-14`）。
- **API 面**：
  - XMdiSubWindow 生命周期：`XVtable* XMdiSubWindow_class_init(void)` | 单例虚表 | `QMdiSubWindow`（qmdisubwindow.h:33） | 借用禁释放
  - `void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | 构造对标同上 | 无
  - `XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - `void* XMdiSubWindow_aboutToActivate_signal(XMdiSubWindow* self)` | aboutToActivate 信号 | `aboutToActivate`（qmdisubwindow.h:65） | 借用禁释放
  - `void* XMdiSubWindow_windowStateChanged_signal(XMdiSubWindow* self, int oldState, int newState)` | 窗口状态变化信号 | `windowStateChanged`（qmdisubwindow.h:64） | 借用禁释放
  - `void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget)` | 设内容控件 | `setWidget`（qmdisubwindow.h:39） | 内容归 sub window
  - `XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self)` | 查询内容控件 | `widget`（qmdisubwindow.h:40） | 借用
  - `void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8)` | 设标题条文本 | X 扩展（QMdiSubWindow 无自有 setWindowTitle） | 无
  - `const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self)` | 查询标题（空则 ""） | X 扩展 | 借用禁释放
  - `void XMdiSubWindow_setOption(XMdiSubWindow* self, int option, bool on)` | 置/清选项位 | `setOption`（qmdisubwindow.h:47） | 无
  - `bool XMdiSubWindow_testOption(const XMdiSubWindow* self, int option)` | 测选项位 | `testOption`（qmdisubwindow.h:48） | 借用
  - `void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step)` | 设键盘单步像素（仅 >0 生效） | `setKeyboardSingleStep`（qmdisubwindow.h:50） | 无
  - `int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self)` | 查询单步 | `keyboardSingleStep`（qmdisubwindow.h:51） | 借用
  - `void XMdiSubWindow_setKeyboardPageStep(XMdiSubWindow* self, int step)` | 设键盘页步 | `setKeyboardPageStep`（qmdisubwindow.h:53） | 无
  - `int XMdiSubWindow_keyboardPageStep(const XMdiSubWindow* self)` | 查询页步 | `keyboardPageStep`（qmdisubwindow.h:54） | 借用
  - `bool XMdiSubWindow_isShaded(const XMdiSubWindow* self)` | 是否折叠 | `isShaded`（qmdisubwindow.h:45） | 借用
  - `void XMdiSubWindow_showShaded(XMdiSubWindow* self)` | 折叠/还原为仅标题条 | `showShaded`（qmdisubwindow.h:71） | 无
  - `void XMdiSubWindow_showSystemMenu(XMdiSubWindow* self)` | 标题条处弹系统菜单 | `showSystemMenu`（qmdisubwindow.h:69） | 无
  - `void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu)` | 设系统菜单（取得所有权） | `setSystemMenu`（qmdisubwindow.h:57） | 所有权转移（NULL 清除）
  - `XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self)` | 查询系统菜单 | `systemMenu`（qmdisubwindow.h:58） | 借用禁释放
  - `XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self)` | 反查所属区域 | `mdiArea`（qmdisubwindow.h:61） | 借用
  - `XSize XMdiSubWindow_sizeHint(const XMdiSubWindow* self)` | 尺寸提示 | `sizeHint`（qmdisubwindow.h:36） | 按值返回
  - `XSize XMdiSubWindow_minimumSizeHint(const XMdiSubWindow* self)` | 最小尺寸提示 | `minimumSizeHint`（qmdisubwindow.h:37） | 按值返回
  - `XWidget* XMdiSubWindow_maximizedButtonsWidget(const XMdiSubWindow* self)` | 恒返回 NULL | `maximizedButtonsWidget`（qmdisubwindow.h:42，internal） | 借用
  - `XWidget* XMdiSubWindow_maximizedSystemMenuIconWidget(const XMdiSubWindow* self)` | 恒返回 NULL | `maximizedSystemMenuIconWidget`（qmdisubwindow.h:43，internal） | 借用
  - XMdiArea 生命周期：`XVtable* XMdiArea_class_init(void)` | 单例虚表 | `QMdiArea`（qmdiarea.h:20） | 借用禁释放
  - `void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | `QMdiArea(QWidget*)`（qmdiarea.h:54） | 无
  - `XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - `XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget)` | 添加子窗口（挂视口、级联摆放） | `addSubWindow`（qmdiarea.h:64） | sub window 归区域
  - `void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget)` | 移除子窗口 | `removeSubWindow`（qmdiarea.h:65） | 无
  - `const XVector* XMdiArea_subWindowList(const XMdiArea* self)` | 子窗口数组借用 | `subWindowList`（qmdiarea.h:62） | 借用禁释放
  - `XMdiSubWindow* XMdiArea_currentSubWindow(const XMdiArea* self)` | 当前子窗口 | `currentSubWindow`（qmdiarea.h:60） | 借用
  - `void XMdiArea_setBackground(XMdiArea* self, uint32_t color)` | 背景色（ARGB，0=默认） | `setBackground`（qmdiarea.h:68） | 无
  - `uint32_t XMdiArea_background(const XMdiArea* self)` | 查询背景色 | `background`（qmdiarea.h:67） | 借用
  - `void XMdiArea_setTabPosition(XMdiArea* self, int position)` | 页签位置（Tabbed 模式） | `setTabPosition`（qmdiarea.h:93） | 无
  - `int XMdiArea_tabPosition(const XMdiArea* self)` | 查询页签位置 | `tabPosition`（qmdiarea.h:94） | 借用
  - `void XMdiArea_setTabShape(XMdiArea* self, int shape)` | 页签形状（0=Rounded/1=Triangular） | `setTabShape`（qmdiarea.h:90） | 无
  - `int XMdiArea_tabShape(const XMdiArea* self)` | 查询页签形状 | `tabShape`（qmdiarea.h:91） | 借用
  - `void XMdiArea_setTabsMovable(XMdiArea* self, bool movable)` | 页签可拖 | `setTabsMovable`（qmdiarea.h:86） | 无
  - `bool XMdiArea_tabsMovable(const XMdiArea* self)` | 查询可拖 | `tabsMovable`（qmdiarea.h:87） | 借用
  - `void XMdiArea_setTabsClosable(XMdiArea* self, bool closable)` | 页签可关 | `setTabsClosable`（qmdiarea.h:83） | 无
  - `bool XMdiArea_tabsClosable(const XMdiArea* self)` | 查询可关 | `tabsClosable`（qmdiarea.h:84） | 借用
  - `void XMdiArea_setActivationOrder(XMdiArea* self, int order)` | 设激活顺序 | `setActivationOrder`（qmdiarea.h:71） | 无
  - `int XMdiArea_activationOrder(const XMdiArea* self)` | 查询激活顺序 | `activationOrder`（qmdiarea.h:70） | 借用
  - `void XMdiArea_setOption(XMdiArea* self, int option, bool on)` | 置/清区域选项位 | `setOption`（qmdiarea.h:73） | 无
  - `bool XMdiArea_testOption(const XMdiArea* self, int option)` | 测区域选项位 | `testOption`（qmdiarea.h:74） | 借用
  - `void XMdiArea_setDocumentMode(XMdiArea* self, bool enabled)` | 文档模式 | `setDocumentMode`（qmdiarea.h:81） | 无
  - `bool XMdiArea_documentMode(const XMdiArea* self)` | 查询文档模式 | `documentMode`（qmdiarea.h:80） | 借用
  - `XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self)` | 活动子窗口 | `activeSubWindow`（qmdiarea.h:61） | 借用
  - `void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window)` | 设活动子窗口 | `setActiveSubWindow`（qmdiarea.h:101） | window 借用
  - `void XMdiArea_closeActiveSubWindow(XMdiArea* self)` | 关活动子窗口 | `closeActiveSubWindow`（qmdiarea.h:104） | 无
  - `void XMdiArea_activateNextSubWindow(XMdiArea* self)` | 激活下一个 | `activateNextSubWindow`（qmdiarea.h:106） | 无
  - `void XMdiArea_activatePreviousSubWindow(XMdiArea* self)` | 激活上一个 | `activatePreviousSubWindow`（qmdiarea.h:107） | 无
  - `void XMdiArea_closeAllSubWindows(XMdiArea* self)` | 关闭全部（销毁子窗口） | `closeAllSubWindows`（qmdiarea.h:105） | 无
  - `void XMdiArea_cascadeSubWindows(XMdiArea* self)` | 层叠 | `cascadeSubWindows`（qmdiarea.h:103） | 无
  - `void XMdiArea_tileSubWindows(XMdiArea* self)` | 平铺 | `tileSubWindows`（qmdiarea.h:102） | 无
  - `void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode)` | 设视图模式 | `setViewMode`（qmdiarea.h:76） | 无
  - `XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self)` | 查询视图模式 | `viewMode`（qmdiarea.h:77） | 借用
  - `int XMdiArea_subWindowCount(const XMdiArea* self)` | 子窗口数量 | X 扩展 | 借用
  - `void* XMdiArea_subWindowActivated_signal(XMdiArea* self, XMdiSubWindow* window)` | 子窗口激活信号（真发射） | `subWindowActivated`（qmdiarea.h:98） | 借用禁释放
- **所有权与命名契约**：`m_subWindows` 拥有子窗口、`m_active` 借用（`XMdiArea.h:240-241`）；`m_widget`「借用，归 sub window」、`m_title`/`m_systemMenu` 对象拥有（`XMdiArea.h:89-91`）；`setSystemMenu`「取得菜单所有权」（`XMdiArea.h:192`）；`_2` 重载为 `setWindowTitle_2`/`windowTitle_2`（UTF-8 后缀而非 `_utf8`）；无 `_const` 族；三态 set* 无。
- **可覆写点**：`XMdiSubWindow` 虚表 `XCLASS_DEFINE_BEGING(XMdiSubWindow)` + `XCLASS_DEFINE_EXTEND_END(XMdiSubWindow, XWidget)`（`XMdiArea.h:82-83`），`XMdiArea.c:511-524` 重载 `EXWidget_SizeHint`/`MinimumSizeHint`/`PaintEvent`/`ResizeEvent`/`MousePress`/`MouseMove`/`MouseRelease` 与 `EXClass_Deinit`；`XMdiArea` 虚表 `XCLASS_DEFINE_BEGING(XMdiArea)` + `XCLASS_DEFINE_EXTEND_END(XMdiArea, XAbstractScrollArea)`（`XMdiArea.h:233-234`），`XMdiArea.c:817-820` 重载 `EXWidget_ResizeEvent`/`PaintEvent` 与 `EXClass_Deinit`。
- **注意事项与坑点**：
  1. `XMdiAreaAreaOption_AllowTabbedView = 0x1`（`XMdiArea.h:51`）在 Qt 6.8.3 中并不存在：`qmdiarea.h:36-38` 的 `AreaOption` 只有 `DontMaximizeSubWindowOnActivation = 0x1`；本头把它排到 0x2（`XMdiArea.h:52`）并声称「数值一致」（`XMdiArea.h:48`）——枚举值与 Qt 不对齐。
  2. `XMdiSubWindowState_Shaded 0x20`（`XMdiArea.h:74`）头注称「对标 Qt::WindowStates 中与本类相关的位」，但 Qt 6.8.3 `qnamespace.h:250-257` 的 `WindowState` 只有 `WindowNoState=0x0/Minimized=0x1/Maximized=0x2/FullScreen=0x4/Active=0x8`，并用 `rg -rn "WindowShaded" src/` 在 Qt 源码中检索无结果——该位无 Qt 对应物。
  3. `XSize XMdiSubWindow_sizeHint` 头注称「对标 QSize sizeHint」（`XMdiArea.h:208-212`），而 `out/widget_qt683_diff_20261104.txt:116` 记 `XMdiArea 缺public(2): minimumSizeHint, sizeHint`——Qt 的 `sizeHint`/`minimumSizeHint` 属 `QMdiArea`（`qmdiarea.h:57-58`），本库给到了 `XMdiSubWindow` 而 `XMdiArea` 无对应声明。
  4. `XMdiArea_setViewMode` 仅赋值存储 `m_viewMode`（`XMdiArea.c` 中函数体为 `self->m_viewMode = (int)mode;`），不校验非法值也不触发重排；`viewMode` 取值 0/1 与 Qt `SubWindowView/TabbedView` 一致（`qmdiarea.h:48-51`）。
  5. `addSubWindow` 把子窗挂到视口而非区域本体，源码注释记录根因：「挂到区域本体时子窗与视口为兄弟、被视口绘制次序压住且几何/裁剪口径错位——表现为子窗不可见仅剩残迹」（`XMdiArea.c:861-864`，对标 qmdiarea.cpp:791-797）。
  6. `setKeyboardSingleStep`/`setKeyboardPageStep` 实现在 `step > 0` 时才写入（`XMdiArea.c:1134-1135`、`1140-1141`），头注仅写「像素数」（`XMdiArea.h:155-176`），未注明非正值被忽略；结构体初值 5/20（`XMdiArea.h:93-94`）。
  7. `XMdiArea` 的页签相关 setter（`setTabPosition`/`setTabShape`/`setTabsMovable`/`setTabsClosable`）在头注均标「Tabbed 模式」，但 `XMdiArea_setViewMode` 实现只写 `self->m_viewMode = (int)mode;`（`XMdiArea.c:1047`），且 `XMdiArea_class_init` 仅重载 Resize/Paint/Deinit（`XMdiArea.c:817-820`）——页签渲染分支在公开覆写点中无对应槽。
  8. 历史修复记录：`XGui.md:1784`「XMdiArea resize 重铺」列入复扫 P1×22 修复批次。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XMdiAreaTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_containers.c` 引用 44 处。

## XMenu

- **定位**：弹出菜单，动作容器 + 属性 + popup/exec + 四个信号；继承链 `XObject←XWidget←XMenu`（`XMenu.h:52-53`）；头文件 `Src/XGui/Widget/XMenu.h`（634 行）。
- **生命周期**：`XMenu_init`/`XMenu_init_2`（带标题）就地初始化；`XMenu_create()`（无参）/`XMenu_create_ex(memory,parent,utf8Title)`/`XMenu_create_copy`/`XMenu_create_move` 四个创建族，全部须 `XClassDelete` 释放；`deinit`/`copy`/`move` 三个虚表槽自管动作与子菜单资源（`XMenu.c:1608-1610`）。
- **API 面**：
  - 生命周期：`XVtable* XMenu_class_init(void)` | 单例虚表 | `QMenu`（qmenu.h:25） | 借用禁释放
  - `void XMenu_init(XMenu* self, XWidget* parent)` | 默认初始化 | `QMenu(QWidget*)`（qmenu.h:38） | 无
  - `void XMenu_init_2(XMenu* self, XWidget* parent, const char* utf8Title)` | 带标题初始化 | `QMenu(QString,QWidget*)`（qmenu.h:39） | utf8Title 借用
  - `XMenu* XMenu_create(void)` | 默认内存创建 | 构造对标 qmenu.h:38 | 拷贝须释放
  - `XMenu* XMenu_create_ex(XMemoryType memory, XWidget* parent, const char* utf8Title)` | 指定内存创建 | 构造对标 qmenu.h:39 | 拷贝须释放
  - `XMenu* XMenu_create_copy(const XMenu* other)` | 拷贝创建（只复制标题与动作，不复制连接） | X 扩展 | 拷贝须释放
  - `XMenu* XMenu_create_move(XMenu* other)` | 移动创建（资源所有权转移） | X 扩展 | 拷贝须释放，源字段复位
  - 动作容器：`XAction* XMenu_addAction(XMenu* self, const XString* text)` | 追加文本动作（菜单拥有） | `addAction(QString)`（qmenu.h:44） | 归菜单所有（禁释放）
  - `XAction* XMenu_addAction_2(XMenu* self, const char* utf8)` | UTF-8 重载 | 同上 | 归菜单所有
  - `bool XMenu_addMenu(XMenu* self, XMenu* menu)` | 把已有菜单作为子菜单加入 | `addMenu(QMenu*)`（qmenu.h:113） | 子菜单归本菜单
  - `XMenu* XMenu_addMenu_2(XMenu* self, const char* utf8Title)` | 创建并加入带标题子菜单 | `addMenu(QString)`（qmenu.h:114） | 归本菜单，禁释放
  - `XAction* XMenu_addSeparator(XMenu* self)` | 追加分隔条 | `addSeparator`（qmenu.h:117） | 归菜单所有
  - `XAction* XMenu_addSection(XMenu* self, const XString* text)` | 追加节标题（禁用不可选） | `addSection(QString)`（qmenu.h:119） | 归菜单所有
  - `XAction* XMenu_addSection_2(XMenu* self, const char* utf8)` | UTF-8 重载 | 同上 | 归菜单所有
  - `void XMenu_addActionAction(XMenu* self, XAction* action)` | 加入已有动作（借用） | `addAction(QAction*)`（qmenu.h:42 `using QWidget::addAction`；`qwidget.h:559`） | 借用禁释放
  - `void XMenu_insertAction(XMenu* self, XAction* before, XAction* action)` | 在 before 前插入已有动作 | `insertAction`（qmenu.h:42；`qwidget.h:562`） | 借用禁释放
  - `void XMenu_removeAction(XMenu* self, XAction* action)` | 摘除动作但不释放 | `removeAction`（qmenu.h:42；`qwidget.h:563`） | 所有权归还调用方
  - `XAction* XMenu_insertMenu(XMenu* self, XAction* before, XMenu* menu)` | 在 before 前插入子菜单 | `insertMenu`（qmenu.h:122） | 子菜单所有权转移给本菜单
  - `XAction* XMenu_insertSeparator(XMenu* self, XAction* before)` | 在 before 前插入分隔条 | `insertSeparator`（qmenu.h:123） | 归菜单所有
  - `XAction* XMenu_insertSection(XMenu* self, XAction* before, const XString* text)` | 在 before 前插入节标题 | `insertSection`（qmenu.h:124） | 归菜单所有
  - `XAction* XMenu_insertSection_2(XMenu* self, XAction* before, const char* utf8)` | UTF-8 重载 | 同上 | 归菜单所有
  - `XRect XMenu_actionGeometry(const XMenu* self, XAction* action)` | 动作局部矩形（未命中全零） | `actionGeometry`（qmenu.h:152） | 按值返回
  - `void XMenu_setNoReplayFor(XMenu* self, XWidget* widget)` | 设不重放点击的目标控件 | `setNoReplayFor`（qmenu.h:165） | widget 借用
  - `void XMenu_setPlatformMenu(XMenu* self, void* platformMenu)` | 设平台菜单句柄（不透明存储） | `setPlatformMenu`（qmenu.h:167） | 不透明句柄
  - `void* XMenu_platformMenu(const XMenu* self)` | 查询平台菜单句柄 | `platformMenu`（qmenu.h:166） | 借用
  - `void XMenu_setAsDockMenu(XMenu* self)` | macOS Dock 菜单（仅记录） | `setAsDockMenu`（qmenu.h:171） | 无
  - `void XMenu_showTearOffMenu(XMenu* self)` | 显示撕离菜单（仅记录可见位） | `showTearOffMenu()`（qmenu.h:134） | 无
  - `void XMenu_hideTearOffMenu(XMenu* self)` | 隐藏撕离菜单 | `hideTearOffMenu`（qmenu.h:136） | 无
  - `bool XMenu_isTearOffMenuVisible(const XMenu* self)` | 撕离菜单可见查询 | `isTearOffMenuVisible`（qmenu.h:133） | 借用
  - `void XMenu_clear(XMenu* self)` | 清空全部动作与子菜单 | `clear`（qmenu.h:128） | 无
  - `bool XMenu_isEmpty(const XMenu* self)` | 是否为空 | `isEmpty`（qmenu.h:127） | 借用
  - `const XVector* XMenu_actions(const XMenu* self)` | 动作列表借用 | `actions`（QWidget 继承，qwidget.h:565） | 借用禁释放
  - `XAction* XMenu_actionAt(const XMenu* self, const XPoint* pos)` | 局部坐标处动作（分隔条也返回） | `actionAt`（qmenu.h:153） | 借用
  - `XAction* XMenu_menuAction(XMenu* self)` | 代表本菜单的动作（懒创建） | `menuAction`（qmenu.h:155） | 归菜单所有，借用
  - `XMenu* XMenu_menuInAction(const XAction* action)` | 动作承载的子菜单 | `menuInAction`（qmenu.h:156-157） | 借用禁释放
  - 属性：`XString* XMenu_title(const XMenu* self)` | 标题拷贝 | `title`（qmenu.h:159） | 拷贝须 `XClassDelete`
  - `const XString* XMenu_title_const(const XMenu* self)` | 标题借用 | 同上 | 借用禁释放
  - `void XMenu_setTitle(XMenu* self, const XString* title)` | 设标题并更新 menuAction 文本 | `setTitle`（qmenu.h:160） | title 借用
  - `void XMenu_setTitle_2(XMenu* self, const char* utf8)` | UTF-8 重载 | 同上 | 借用
  - `void XMenu_setIcon(XMenu* self, const XString* icon)` | 设图标（路径承载，渲染未消费） | `setIcon`（qmenu.h:163） | icon 借用
  - `void XMenu_setIcon_2(XMenu* self, const char* utf8)` | UTF-8 重载 | 同上 | 借用
  - `const XString* XMenu_icon(const XMenu* self)` | 图标借用 | `icon`（qmenu.h:162） | 借用禁释放
  - `XAction* XMenu_defaultAction(const XMenu* self)` | 默认动作 | `defaultAction`（qmenu.h:139） | 借用
  - `void XMenu_setDefaultAction(XMenu* self, XAction* action)` | 设默认动作 | `setDefaultAction`（qmenu.h:138） | action 借用
  - `XAction* XMenu_activeAction(const XMenu* self)` | 当前高亮动作 | `activeAction`（qmenu.h:142） | 借用
  - `void XMenu_setActiveAction(XMenu* self, XAction* action)` | 设高亮并重绘 | `setActiveAction`（qmenu.h:141） | action 借用
  - `bool XMenu_separatorsCollapsible(const XMenu* self)` | 相邻分隔条是否合并 | `separatorsCollapsible`（qmenu.h:174） | 借用
  - `void XMenu_setSeparatorsCollapsible(XMenu* self, bool collapse)` | 设合并 | `setSeparatorsCollapsible`（qmenu.h:175） | 无
  - `bool XMenu_toolTipsVisible(const XMenu* self)` | 是否显示工具提示 | `toolTipsVisible`（qmenu.h:177） | 借用
  - `void XMenu_setToolTipsVisible(XMenu* self, bool visible)` | 设工具提示可见 | `setToolTipsVisible`（qmenu.h:178） | 无
  - `bool XMenu_tearOffEnabled(const XMenu* self)` | 是否允许撕离 | `isTearOffEnabled`（qmenu.h:131） | 借用
  - 宏 `XMenu_isTearOffEnabled(self)`（`XMenu.h:535`） | `tearOffEnabled` 属性访问器别名 | `isTearOffEnabled`（qmenu.h:131） | 无
  - `void XMenu_setTearOffEnabled(XMenu* self, bool enable)` | 设撕离使能（仅存储位） | `setTearOffEnabled`（qmenu.h:130） | 无
  - 弹出：`void XMenu_popup(XMenu* self, const XPoint* pos)` | 屏幕坐标弹出（pos NULL 用鼠标位置） | `popup`（qmenu.h:144） | pos 借用
  - `XAction* XMenu_exec(XMenu* self)` | 阻塞执行并返回被选动作 | `exec`（qmenu.h:145） | 借用
  - `XSize XMenu_sizeHint(const XMenu* self)` | 建议尺寸 | `sizeHint`（qmenu.h:150） | 按值返回
  - 离屏绘制：`void XMenu_drawContents(XMenu* self, XPainter* painter)` | 与 paintEvent 同体的离屏绘制 | X 扩展（Qt 为 protected paintEvent） | painter 借用
  - 信号：`void* XMenu_aboutToShow_signal(XMenu* self)` | aboutToShow | `aboutToShow`（qmenu.h:181） | 借用禁释放
  - `void* XMenu_aboutToHide_signal(XMenu* self)` | aboutToHide | `aboutToHide`（qmenu.h:182） | 借用禁释放
  - `void* XMenu_triggered_signal(XMenu* self, XAction* action)` | triggered(action) | `triggered`（qmenu.h:183） | 借用禁释放
  - `void* XMenu_hovered_signal(XMenu* self, XAction* action)` | hovered(action) | `hovered`（qmenu.h:184） | 借用禁释放
- **所有权与命名契约**：`m_actions` 与平行 `m_actionOwned` 记录逐项所有权（true=菜单自建拥有、false=insertAction 注入借用，`XMenu.h:71-78`）；`m_defaultAction`/`m_activeAction`/`m_noReplayFor` 借用（`XMenu.h:81-84`）；`XMenu_title` 返回拷贝须 `XClassDelete`，`XMenu_title_const` 借用；`_2` 重载 `addAction_2`/`addMenu_2`/`addSection_2`/`insertSection_2`/`setTitle_2`/`setIcon_2`；无 `_utf8` 后缀（UTF-8 参数直接收 `const char*`）；三态 set* 无。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XMenu)` + `XCLASS_DEFINE_EXTEND_END(XMenu, XWidget)`（`XMenu.h:52-53`）；`XMenu.c:1592-1611` 重载 `EXWidget_SizeHint`/`MinimumSizeHint`/`PaintEvent`/`MousePress`/`MouseRelease`/`MouseMove`/`KeyPress`/`Leave`/`Hide`、`EXObject_TimerEvent`、`EXClass_Copy`/`Move`/`Deinit`。
- **注意事项与坑点**：
  1. 两处无声明主体的悬空文档注释：`XMenu.h:155-159`（"通过当前 XClass 虚表释放菜单对象所拥有的资源"）与 `XMenu.h:161-165`（"释放菜单对象资源并按对象所有权删除其存储空间"），其后均无任何函数声明——头文件里没有 `XMenu_deinit`/`XMenu_delete` 的公开原型，释放走 `XClassDelete`。
  2. `XMenu_create_move` 头注「移动后其字段复位，仍由创建者释放」（`XMenu.h:148-151`），而 `XMenu_create_copy` 只复制标题与动作、不复制信号连接（`XMenu.h:143-144`）——两者都不覆盖平台句柄/定时器等内部字段，跨对象搬运需自行评估。
  3. 弹出菜单自删语义：实现按 `XWidgetAttribute_DeleteOnClose` 在交互关闭路径调 `XObject_deleteLater`，注释说明「同步删改为延迟：本函数由 mouseRelease/keyPress/triggered 槽路径进入，aboutToHide 已在本帧发射，事件分发帧返回途中仍会访问本控件——立即释放 UAF」（`XMenu.c:804-812`）；`XGui.md:1700` 亦登记「XMenu 已自行实现自删（XMenu.c:804-812 经 XObject_deleteLater）」，并警告通用化会形成双重删除风险。
  4. 析构曾隔个漏删：`XGui.md:540-547`「deinit 循环 XClassDelete(0) 后动作经 destroyed 信号自摘……循环尾再补 remove(0) 把下一个动作指针丢弃不删」，修复按尺寸是否自缩判定；自摘机制见 `XMenu.c:91`（`xmenu_actionDestroyedSlot`）与 `XMenu.c:165-166`、`212-213` 的 destroyed 连接，析构实现 `VXMenu_deinit` 在 `XMenu.c:1516`。
  5. `popup` 不对刚 show 的窗口立即设焦点：头注「X11 下对未完成映射的窗口调用 XSetInputFocus 会触发 BadMatch，焦点交由平台在用户与菜单窗口交互时自然授予」（`XMenu.h:553-556`）；实现侧另注「BadMatch 已由该挂起机制消除」（`XMenu.c:912`）。
  6. `exec` 必须在事件循环环境调用，否则阻塞（`XMenu.h:567-568`）。
  7. `menuInAction` 省去 `qobject_cast` 类型鉴别，直接返回动作登记的 XMenu（`XMenu.h:394-402`），无运行时类型校验。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XMenuTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_menus.c` 引用 14 处、`xgui_demo_apitest_containers.c` 14 处。

## XMenuBar

- **定位**：菜单栏控件，注册菜单并创建关联动作、便捷动作族、角落控件占位、triggered/hovered 转发；继承链 `XObject←XWidget←XMenuBar`（`XMenuBar.h:47-48`）；头文件 `Src/XGui/Widget/XMenuBar.h`（252 行）。
- **生命周期**：`XMenuBar_init` + `XMenuBar_create` 宏 / `XMenuBar_create_ex`，`XClassDelete` 释放；`deinit` 重载按 `m_actionOwned` 平行标记销毁自建动作、仅摘除借用动作（`XMenuBar.c:480-494`）。
- **API 面**：
  - 生命周期：`XVtable* XMenuBar_class_init(void)` | 单例虚表 | `QMenuBar`（qmenubar.h:19） | 借用禁释放
  - `void XMenuBar_init(XMenuBar* self, XWidget* parent, XWidgetFlags flags)` | 初始化 | `QMenuBar(QWidget*)`（qmenubar.h:27） | 无
  - `XMenuBar* XMenuBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 菜单管理：`XAction* XMenuBar_addMenu(XMenuBar* self, XMenu* menu)` | 注册菜单并创建关联动作（动作文本=菜单标题） | `addMenu(QMenu*)`（qmenubar.h:36） | 返回动作归菜单栏所有；menu 借用
  - `XMenu* XMenuBar_addMenu_2(XMenuBar* self, const char* utf8Title)` | 以标题创建空菜单并注册 | `addMenu(QString)`（qmenubar.h:37） | 返回菜单归调用方
  - `XAction* XMenuBar_addSeparator(XMenuBar* self)` | 追加分隔条动作 | `addSeparator`（qmenubar.h:41） | 归菜单栏所有
  - `XAction* XMenuBar_addAction(XMenuBar* self, const XString* text)` | 以文本创建动作并追加 | `addAction(QString)`（qmenubar.h:32，`QT_WIDGETS_REMOVED_SINCE(6,3)` 门控） | 归菜单栏所有，借用返回
  - `XAction* XMenuBar_addAction_2(XMenuBar* self, const char* utf8Text)` | UTF-8 重载 | `addAction(const QString&)` 重载 | 归菜单栏所有
  - `XAction* XMenuBar_insertMenu(XMenuBar* self, XAction* before, XMenu* menu)` | 在 before 前插入菜单（NULL 等价追加） | `insertMenu`（qmenubar.h:44） | 返回动作归菜单栏所有
  - `XAction* XMenuBar_insertSeparator(XMenuBar* self, XAction* before)` | 在 before 前插入分隔条 | `insertSeparator`（qmenubar.h:42） | 归菜单栏所有
  - `XAction* XMenuBar_insertAction(XMenuBar* self, XAction* before, XAction* action)` | 在 before 前插入既有动作（已在则先摘再插） | `insertAction`（QWidget 继承，qwidget.h:562） | action 借用禁释放
  - `void XMenuBar_removeAction(XMenuBar* self, XAction* action)` | 摘除动作但不释放 | `removeAction`（QWidget 继承，qwidget.h:563） | 所有权归还调用方
  - `void XMenuBar_clear(XMenuBar* self)` | 清空全部（不销毁外部菜单对象） | `clear`（qmenubar.h:46） | 无
  - `XAction* XMenuBar_activeAction(const XMenuBar* self)` | 查询激活动作 | `activeAction`（qmenubar.h:48） | 借用
  - `void XMenuBar_setActiveAction(XMenuBar* self, XAction* action)` | 设激活动作 | `setActiveAction`（qmenubar.h:49） | action 借用
  - `bool XMenuBar_isDefaultUp(const XMenuBar* self)` | 弹出方向默认向上 | `isDefaultUp`（qmenubar.h:52） | 借用
  - `void XMenuBar_setDefaultUp(XMenuBar* self, bool up)` | 设默认向上 | `setDefaultUp`（qmenubar.h:51） | 无
  - `int XMenuBar_actionCount(const XMenuBar* self)` | 动作总数（含分隔条） | X 扩展 | 借用
  - 几何/角落/尺寸：`XAction* XMenuBar_actionAt(const XMenuBar* self, const XPoint* pos)` | 局部坐标处动作 | `actionAt`（qmenubar.h:59） | pos 借用
  - `XRect XMenuBar_actionGeometry(const XMenuBar* self, XAction* action)` | 动作局部矩形（未命中全零） | `actionGeometry`（qmenubar.h:58） | 按值返回
  - `XWidget* XMenuBar_cornerWidget(const XMenuBar* self, int corner)` | 查询角落控件 | `cornerWidget`（qmenubar.h:62） | 借用
  - `void XMenuBar_setCornerWidget(XMenuBar* self, XWidget* widget, int corner)` | 设角落控件（仅 TopLeft/TopRight） | `setCornerWidget`（qmenubar.h:61） | widget 归调用方
  - `int XMenuBar_heightForWidth(const XMenuBar* self, int width)` | 给定宽度下期望高度（width 忽略） | `heightForWidth`（qmenubar.h:56） | 借用
  - `XSize XMenuBar_sizeHint(const XMenuBar* self)` | 尺寸提示 | `sizeHint`（qmenubar.h:54） | 按值返回
  - `XSize XMenuBar_minimumSizeHint(const XMenuBar* self)` | 最小尺寸提示 | `minimumSizeHint`（qmenubar.h:55） | 按值返回
  - `bool XMenuBar_isNativeMenuBar(const XMenuBar* self)` | 原生菜单栏开关 | `isNativeMenuBar`（qmenubar.h:68） | 借用
  - `void XMenuBar_setNativeMenuBar(XMenuBar* self, bool nativeMenuBar)` | 设原生（仅存储位） | `setNativeMenuBar`（qmenubar.h:69） | 无
  - `void* XMenuBar_platformMenuBar(const XMenuBar* self)` | 恒返回 NULL | `platformMenuBar`（qmenubar.h:70） | 借用
  - 信号：`void* XMenuBar_triggered_signal(XMenuBar* self, XAction* action)` | triggered(action) 转发 | `triggered`（qmenubar.h:75） | 借用禁释放
  - `void* XMenuBar_hovered_signal(XMenuBar* self, XAction* action)` | hovered(action) 转发 | `hovered`（qmenubar.h:76） | 借用禁释放
- **所有权与命名契约**：`m_actions`/`m_menus`/`m_actionOwned` 三平行向量（`XMenuBar.h:57-64`），addMenu/addSeparator/addAction/addAction_2/insertMenu/insertSeparator 创建者为拥有、insertAction 注入者借用（`XMenuBar.h:17-23`）；`m_cornerWidgetL/R` 借用；`_2` 重载 `addMenu_2`/`addAction_2`；无 `_utf8`/`_const` 命名族；三态 set* 无。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XMenuBar)` + `XCLASS_DEFINE_EXTEND_END(XMenuBar, XWidget)`（`XMenuBar.h:47-48`）；`XMenuBar.c:480-494` 重载 `EXWidget_SizeHint`/`MinimumSizeHint`/`PaintEvent`/`MousePress`/`MouseMove`/`Leave` 与 `EXClass_Deinit`。内部另有私有桥类 `XMBBridge`（继承 XObject，`XMenuBar.c:128-133`），非公开覆写点。
- **注意事项与坑点**：
  1. `XMenuBar_cornerWidget` 实现只判 TopLeft，其余一律返回右上槽（`XMenuBar.c:915-920`：`corner == (int)XMenuBarCorner_TopLeft ? self->m_cornerWidgetL : self->m_cornerWidgetR`），而 `setCornerWidget` 对 BottomLeft/BottomRight/非法值打印告警并直接返回（`XMenuBar.c:922-943`）——getter 对未设置的角位不返回 NULL 而是错位读到右上槽。
  2. `XMenuBarCorner` 枚举值（TopLeft=0x1/TopRight=0x2/BottomLeft=0x3/BottomRight=0x4，`XMenuBar.h:76-82`）头注称「对标 Qt::Corner，数值一致」，但 Qt 6.8.3 `qnamespace.h:1267-1272` 为 `TopLeftCorner=0x0/TopRightCorner=0x1/BottomLeftCorner=0x2/BottomRightCorner=0x3`——数值不对齐。
  3. 几何模型与绘制存在轻微偏差：头注「几何按统一布局模型（文本宽+16 或固定 60px 间距）计算，与无样式绘制的固定间距存在轻微视觉偏差」（`XMenuBar.h:181-183`）；`XGui.md:1592` 登记「XMenuBar 几何模型与样式绘制有轻微偏差」。
  4. 桥对象泄漏曾为生产真缺陷：`XGui.md:580-585`「addMenu/addAction 创建的 XMBBridge 仅被动作的 triggered 信号连接引用、无持有者，逐 addMenu 泄漏 560B。修复：桥挂为配对动作的 XObject 子」，实现见 `XMenuBar.c:128-150`（`xmb_bridgeCreate` 内 `Set_Class_IsHeap(bridge, true)` 在 `XMenuBar.c:145`）。
  5. hovered 曾是死信号：`XMenuBar.c:553-557` 注释「悬停桥接（复扫 R-70）：桥槽此前已实现但从未连接，hovered 永不发射」（另见 `XMenuBar.c:656`「与 addMenu 同款连接」），`XGui.md:1784` 亦列「XMenuBar hovered 死信号」入 P1×22 修复批次。
  6. `addAction` 创建的动作不关联子菜单（`m_menus` 不含对应项，`XMenuBar.h:102-111` 明注「与 addSeparator 同一约定」），因此 `insertMenu`/`addMenu` 之外的动作不弹出任何菜单。
  7. `platformMenuBar` 恒返回 NULL（`XMenuBar.h:229-234`），`setNativeMenuBar` 仅存储位（`XMenuBar.h:222-227`）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XMenuBarTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_menus.c` 4 处、`xgui_demo_apitest_containers.c` 2 处。

## XMessageBox

- **定位**：消息对话框，图标/标准按钮位/角色族/exec 收口返回标准按钮位值/静态便捷函数；继承链 `XObject←XWidget←XDialog←XMessageBox`（`XMessageBox.h:113-114`）；头文件 `Src/XGui/Widget/XMessageBox.h`（621 行）。
- **生命周期**：`XMessageBox_init` 构造即应用模态与 Dialog 顶层窗口标志；`XMessageBox_create` 宏 / `XMessageBox_create_ex`，`XClassDelete` 释放；`deinit` 重载（`XMessageBox.c:835`）。
- **API 面**：
  - 生命周期：`XVtable* XMessageBox_class_init(void)` | 单例虚表，运行时注册 `INHERIT_XCLASS(XDialog)` | `QMessageBox`（dialogs/qmessagebox.h:21） | 借用禁释放
  - `void XMessageBox_init(XMessageBox* self, XWidget* parent, XWidgetFlags flags)` | 初始化（模态+Dialog 标志） | `QMessageBox(QWidget*)`（qmessagebox.h:113） | 无
  - `XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)` | 堆对象创建 | 构造对标同上 | 拷贝须释放
  - 文本与图标：`void XMessageBox_setText(XMessageBox* self, const char* utf8)` | 设消息文本并同步标签 | `setText`（qmessagebox.h:146） | utf8 借用
  - `const char* XMessageBox_text(const XMessageBox* self)` | 查询消息文本 | `text`（qmessagebox.h:145） | 借用禁释放
  - `void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon)` | 设消息图标 | `setIcon`（qmessagebox.h:149） | 无
  - `XMessageBoxIcon XMessageBox_icon(const XMessageBox* self)` | 查询消息图标 | `icon`（qmessagebox.h:148） | 借用
  - 按钮管理：`void XMessageBox_setStandardButtons(XMessageBox* self, int buttons)` | 设标准按钮组 | `setStandardButtons`（qmessagebox.h:130） | 无
  - `int XMessageBox_standardButtons(const XMessageBox* self)` | 查询标准按钮组 | `standardButtons`（qmessagebox.h:131） | 借用
  - `XAbstractButton* XMessageBox_button(const XMessageBox* self, XDialogButtonBoxStandardButton which)` | 按标准值取按钮 | `button(StandardButton)`（qmessagebox.h:133） | 借用禁释放
  - `XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self)` | 最近被点按钮 | `clickedButton`（qmessagebox.h:143） | 借用禁释放
  - 模态执行：`XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self)` | 阻塞模态循环，返回被点标准按钮位值 | `exec`（qmessagebox.h:296-299 仅 `Q_QDOC` 块；实际走 QDialog::exec） | 无
  - 非阻塞打开：`void XMessageBox_open_2(XMessageBox* self, XObject* receiver, XSlotFunc1 member, bool buttonPayload)` | 窗口模态显示并连接关闭信号 | `open(QObject*,const char*)`（qmessagebox.h:125） | receiver 借用，关闭时自动断开
  - 静态便捷：`XDialogButtonBoxStandardButton XMessageBox_information(XWidget* parent, const char* title, const char* text, int buttons)` | 信息框 | `information`（qmessagebox.h:168-170） | 无
  - `XDialogButtonBoxStandardButton XMessageBox_information_2(XWidget* parent, const char* title, const char* text, int buttons, XDialogButtonBoxStandardButton defaultButton)` | 信息框（显式默认按钮） | 同上五参形态 | 无
  - `XDialogButtonBoxStandardButton XMessageBox_warning(XWidget* parent, const char* title, const char* text, int buttons)` | 警告框 | `warning`（qmessagebox.h:188-190） | 无
  - `XDialogButtonBoxStandardButton XMessageBox_warning_2(XWidget* parent, const char* title, const char* text, int buttons, XDialogButtonBoxStandardButton defaultButton)` | 警告框（显式默认按钮） | 同上五参形态 | 无
  - `XDialogButtonBoxStandardButton XMessageBox_critical(XWidget* parent, const char* title, const char* text, int buttons)` | 严重错误框 | `critical`（qmessagebox.h:198-200） | 无
  - `XDialogButtonBoxStandardButton XMessageBox_critical_2(XWidget* parent, const char* title, const char* text, int buttons, XDialogButtonBoxStandardButton defaultButton)` | 严重错误框（显式默认按钮） | 同上五参形态 | 无
  - `XDialogButtonBoxStandardButton XMessageBox_question(XWidget* parent, const char* title, const char* text, int buttons)` | 询问框 | `question`（qmessagebox.h:178-180） | 无
  - `XDialogButtonBoxStandardButton XMessageBox_question_2(XWidget* parent, const char* title, const char* text, int buttons, XDialogButtonBoxStandardButton defaultButton)` | 询问框（显式默认按钮） | 同上五参形态 | 无
  - `void XMessageBox_about(XWidget* parent, const char* title, const char* text)` | 关于框（Ok 单按钮） | `about`（qmessagebox.h:208） | 无
  - 补充文本：`void XMessageBox_setDetailedText(XMessageBox* self, const char* utf8)` | 设详细文本（自动追加 Show Details 按钮） | `setDetailedText`（qmessagebox.h:282） | utf8 借用
  - `const char* XMessageBox_detailedText(const XMessageBox* self)` | 查询详细文本 | `detailedText`（qmessagebox.h:281） | 借用禁释放
  - `void XMessageBox_setInformativeText(XMessageBox* self, const char* utf8)` | 设补充文本 | `setInformativeText`（qmessagebox.h:278） | utf8 借用
  - `const char* XMessageBox_informativeText(const XMessageBox* self)` | 查询补充文本 | `informativeText`（qmessagebox.h:277） | 借用禁释放
  - 按钮管理（续）：`void XMessageBox_addButton(XMessageBox* self, XAbstractButton* button, int role)` | 追加自定义按钮+角色 | `addButton(QAbstractButton*,ButtonRole)`（qmessagebox.h:119） | button 借用
  - `XAbstractButton* XMessageBox_addButton_2(XMessageBox* self, const char* text, int role)` | 追加文本按钮 | `addButton(QString,ButtonRole)`（qmessagebox.h:120） | 返回借用（归属按钮盒）
  - `XAbstractButton* XMessageBox_addButton_3(XMessageBox* self, int button)` | 追加标准按钮 | `addButton(StandardButton)`（qmessagebox.h:121） | 返回借用
  - `void XMessageBox_setDefaultButton(XMessageBox* self, XAbstractButton* button)` | 设默认按钮对象 | `setDefaultButton(QPushButton*)`（qmessagebox.h:136） | button 借用
  - `void XMessageBox_setDefaultButton_2(XMessageBox* self, int button)` | 设默认按钮（标准位） | `setDefaultButton(StandardButton)`（qmessagebox.h:137） | 无
  - `XAbstractButton* XMessageBox_defaultButton(const XMessageBox* self)` | 查询默认按钮 | `defaultButton`（qmessagebox.h:135） | 借用
  - `void XMessageBox_setEscapeButton(XMessageBox* self, XAbstractButton* button)` | 设转义按钮对象 | `setEscapeButton(QAbstractButton*)`（qmessagebox.h:140） | button 借用
  - `void XMessageBox_setEscapeButton_2(XMessageBox* self, int button)` | 设转义按钮（标准位） | `setEscapeButton(StandardButton)`（qmessagebox.h:141） | 无
  - `XAbstractButton* XMessageBox_escapeButton(const XMessageBox* self)` | 查询转义按钮 | `escapeButton`（qmessagebox.h:139） | 借用
  - `XVector* XMessageBox_buttons(const XMessageBox* self)` | 全部按钮新建向量副本 | `buttons`（qmessagebox.h:127） | 拷贝须释放（元素仍借用）
  - `int XMessageBox_standardButton(const XMessageBox* self, XAbstractButton* button)` | 按钮对应标准值 | `standardButton`（qmessagebox.h:132） | button 借用
  - `void XMessageBox_setOptions(XMessageBox* self, XMessageBoxOptions options)` | 设选项位集 | `setOptions`（qmessagebox.h:165） | 无
  - `XMessageBoxOptions XMessageBox_options(const XMessageBox* self)` | 查询选项位集 | `options`（qmessagebox.h:166） | 借用
  - `bool XMessageBox_testOption(const XMessageBox* self, XMessageBoxOption option)` | 测选项位 | `testOption`（qmessagebox.h:164） | 借用
  - `void XMessageBox_setOption(XMessageBox* self, XMessageBoxOption option, bool on)` | 置/清单选项位 | `setOption`（qmessagebox.h:163） | 无
  - 信号：`void* XMessageBox_buttonClicked_signal(XMessageBox* self, XAbstractButton* button)` | buttonClicked(button) | `buttonClicked`（qmessagebox.h:294） | 借用禁释放
  - 复选框/图标位图/文本呈现：`void XMessageBox_setCheckBox(XMessageBox* self, XCheckBox* checkBox)` | 设复选框（所有权转移，旧者释放） | `setCheckBox`（qmessagebox.h:160） | 引用转移
  - `XCheckBox* XMessageBox_checkBox(const XMessageBox* self)` | 查询复选框 | `checkBox`（qmessagebox.h:161） | 借用禁释放
  - `void XMessageBox_setIconPixmap(XMessageBox* self, const XImage* pixmap)` | 设自定义图标位图（内部深拷贝） | `setIconPixmap`（qmessagebox.h:152） | pixmap 借用、内部深拷贝
  - `const XImage* XMessageBox_iconPixmap(const XMessageBox* self)` | 查询图标位图 | `iconPixmap`（qmessagebox.h:151） | 借用禁释放
  - `void XMessageBox_setTextFormat(XMessageBox* self, XLabelTextFormat format)` | 设文本呈现格式（转发标签） | `setTextFormat`（qmessagebox.h:155） | 无
  - `XLabelTextFormat XMessageBox_textFormat(const XMessageBox* self)` | 查询文本格式 | `textFormat`（qmessagebox.h:154） | 借用
  - `void XMessageBox_setTextInteractionFlags(XMessageBox* self, XLabelTextInteractionFlags flags)` | 设文本交互标志（转发标签） | `setTextInteractionFlags`（qmessagebox.h:157） | 无
  - `XLabelTextInteractionFlags XMessageBox_textInteractionFlags(const XMessageBox* self)` | 查询交互标志 | `textInteractionFlags`（qmessagebox.h:158） | 借用
  - 按钮角色/移除/文本：`XMessageBoxButtonRole XMessageBox_buttonRole(const XMessageBox* self, XAbstractButton* button)` | 查询按钮角色 | `buttonRole`（qmessagebox.h:128） | button 借用
  - `void XMessageBox_removeButton(XMessageBox* self, XAbstractButton* button)` | 从按钮盒摘除但不释放 | `removeButton`（qmessagebox.h:122） | 所有权归还调用方
  - `XString* XMessageBox_buttonText(const XMessageBox* self, int button)` | 标准按钮文本（Qt 已弃用） | `buttonText`（qmessagebox.h:272，`QT_DEPRECATED_SINCE(6,2)`） | 拷贝须 `XClassDelete`
  - `void XMessageBox_setButtonText(XMessageBox* self, int button, const XString* text)` | 设标准按钮文本（Qt 已弃用） | `setButtonText`（qmessagebox.h:274） | text 借用、内部拷贝
  - `void XMessageBox_setButtonText_2(XMessageBox* self, int button, const char* utf8)` | UTF-8 重载 | 同上 | utf8 借用
  - 静态便捷（续）：`void XMessageBox_aboutQt(XWidget* parent, const XString* title)` | 关于 Qt 框 | `aboutQt`（qmessagebox.h:209） | title 借用
  - `XIcon* XMessageBox_standardIcon(int icon)` | 生成标准图标（Qt 已弃用） | `standardIcon`（qmessagebox.h:290） | 拷贝须 `XClassDelete`
- **所有权与命名契约**：`m_textLabel`/`m_buttonBox`/`m_informativeLabel`/`m_detailsText`/`m_detailsButton`/`m_text`/`m_detailedText`/`m_informativeText`/`m_iconPixmap` 对象拥有（`XMessageBox.h:126-147`）；`m_clicked`/`m_defaultButton`/`m_escapeButton` 借用（`XMessageBox.h:141-143`）；`setCheckBox` 明写「所有权转移给消息框：旧复选框被释放」（`XMessageBox.h:483-484`）；`_2` 重载 `open_2`/`addButton_2`/`setDefaultButton_2`/`setEscapeButton_2`/`setButtonText_2` 与四个静态便捷的 `_2` 五参形态；无 `_utf8`/`_const` 命名族；三态 set* 无（`setOption` 为二态）。
- **可覆写点**：虚表 `XCLASS_DEFINE_BEGING(XMessageBox)` + `XCLASS_DEFINE_EXTEND_END(XMessageBox, XDialog)`（`XMessageBox.h:113-114`）；`XMessageBox.c:827-835` 重载 `EXWidget_ResizeEvent`/`PaintEvent`/`KeyPressEvent`/`ShowEvent`/`CloseEvent` 与 `EXClass_Deinit`。父类为 XDialog（`XDialog.h:28-29` `XCLASS_DEFINE_EXTEND_END(XDialog, XWidget)`）。
- **注意事项与坑点**：
  1. 基类成员必须是 `XDialog m_base` 而非 `XWidget`：头注用整段记录事故「此前误写为 XWidget，x86 下 XDialog 尾部的 m_result/m_modal/m_inExec/m_sizeGripEnabled 八字节会按别名覆盖 m_textLabel 与 m_buttonBox 低字节，accept/done 后按钮盒变野指针」（`XMessageBox.h:118-124`）。
  2. `exec` 返回的是标准按钮位值而非 QDialog 的 Accepted/Rejected：头注「Qt 6.8 中 QMessageBox 无 exec 重写（qmessagebox.h:296-299 仅 Q_QDOC 文档块），实际执行 QDialog::exec；而消息盒按钮点击经 done(execReturnCode) 把 result() 回填为被点标准按钮位值」，自定义按钮返回「Accepted+1+索引 的不透明值，未找到返回 -1」（`XMessageBox.h:216-226`）。
  3. 键盘自递归曾是 P0 缺陷：`XGui.md:237-239`「对话框族键盘自递归（_base 入口虚表再分派回最派生重载；XDialog/XMessageBox 改 XClass_Parent 静态调父类实现，递归链终止于 ignore 默认）」。
  4. 子控件形态 show/hide 曾不标脏：`XGui.md:1780`「XDialog/XMessageBox/XProgressDialog 补 update 中继与自绘面板」，根因是框架层 `XWidget_setVisible` 非 `m_isWindow` 分支缺 `XWidget_update`，已列为修复。
  5. `XMessageBox_buttons` 实现是**新建** XVector 并逐项 push 借用指针副本（`XMessageBox.c:1369-1389`），头注「返回新建 XVector<XAbstractButton*> 借用副本，调用方 delete」（`XMessageBox.h:437`）——向量本身须释放、元素禁释放。
  6. `setIconPixmap` 与 `icon` 的互斥：头注「设置后 icon() 回读为 NoIcon，布局按位图存在性让位」（`XMessageBox.h:500-502`）。
  7. `buttonText`/`setButtonText`/`standardIcon` 三个 API 在 Qt 侧均已弃用（`qmessagebox.h:271-274`、`288-291`），本类仍保留并在头注标注「Qt 中已弃用」。
  8. `setIcon` 头注的参数描述为「icon 图标路径（UTF-8）」（`XMessageBox.h:182-183`），实际类型是 `XMessageBoxIcon` 枚举（`XMessageBox.h:186`）——注释措辞与签名不符。
  9. `XMessageBoxOption_DontUseNativeDialog` 仅存储：头注「XGui 恒自绘无原生消息盒后端，位值仅供 setOption/testOption/setOptions/options 存储与查询」（`XMessageBox.h:100-104`）。
- **测试覆盖**：无专属套件（`Test/XGuiTest/XMessageBoxTest.c` 不存在）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_dialogs.c` 引用 6 处、`Test/XGuiDemo/xgui_demo_page_dialogs.c` 演示页（`XGui.md:442` 记「对话框页（XMessageBox×4 非阻塞 open+结果中继…）」）。
# 控件文档片段 · 批次 06

本批覆盖 `Src/XGui/Widget/` 下 9 个控件/值对象类。所有签名逐字抄自头文件；行号可核对。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/`（存在才写）；`qoffscreensurface.h` 实际在 `gui/kernel/`、`qprogressdialog.h` 实际在 `widgets/dialogs/`，已按真实路径引用。

---

## XOffscreenSurface

**定位**：离屏表面对象（格式/尺寸/屏幕/有效性标志的公共 API 面，无真实离屏渲染）。继承链 `XObject ← XOffscreenSurface`。头文件 `Src/XGui/Widget/XOffscreenSurface.h`（155 行）。编译守卫 `XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON`（XOffscreenSurface.h:42）。

**生命周期**：`XOffscreenSurface_init` 全零后 `XObject_init`，预置 `m_format = XSurfaceFormat_create()`、`m_size = (1,1)`、`m_screen = NULL`、`m_isValid = false`；栈对象须与 `XClassDeinit` 成对，堆对象经 `_create/_create_ex` 后须 `XClassDelete`（XOffscreenSurface.h:74-81；XOffscreenSurface.c:30-53）。无独立 deinit 槽，`m_format` 为值类型（`XSurfaceFormat`，非指针）随对象整体消亡。

**API 面**

枚举（XOffscreenSurface.h:8-9、145-147）：`surfaceType()` 返回 `XWindowSurfaceType`（定义见 XWindow.h:169-178：Raster=0/OpenGL=1/RasterGL/OpenVG/Vulkan/Metal/Direct3D）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XOffscreenSurface_class_init(void);` | 建表（继承 XObject，零槽位重载） | 同名 `class_init` 惯用 | 共享表，禁释放 |
| `void XOffscreenSurface_init(XOffscreenSurface* self);` | 初始化（默认 format/size(1,1)/screen NULL/未创建） | `QOffscreenSurface(QScreen*,QObject*)` qoffscreensurface.h:26 | — |
| `#define XOffscreenSurface_create() XOffscreenSurface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XOffscreenSurface* XOffscreenSurface_create_ex(XMemoryType memory);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XOffscreenSurface_setFormat(XOffscreenSurface* self, XSurfaceFormat format);` | 设表面格式（值拷贝） | `setFormat` qoffscreensurface.h:36 | 值语义，无所有权 |
| `XSurfaceFormat XOffscreenSurface_format(const XOffscreenSurface* self);` | 查表面格式；NULL 返回默认格式 | `format` qoffscreensurface.h:37 | 值返回，无所有权 |
| `void XOffscreenSurface_setSize(XOffscreenSurface* self, XSize size);` | 设表面尺寸 | `size` qoffscreensurface.h:40（Qt 只读，尺寸随平台） | 值语义 |
| `XSize XOffscreenSurface_size(const XOffscreenSurface* self);` | 查尺寸；NULL 返回 (1,1) | 同上 | 值返回 |
| `void XOffscreenSurface_setScreen(XOffscreenSurface* self, XScreen* screen);` | 设屏幕；NULL 清除 | `setScreen` qoffscreensurface.h:43 | 借用禁释放 |
| `XScreen* XOffscreenSurface_screen(const XOffscreenSurface* self);` | 查屏幕；未设返回 NULL | `screen` qoffscreensurface.h:42 | 借用禁释放 |
| `void XOffscreenSurface_createSurface(XOffscreenSurface* self);` | 「创建」离屏表面（仅置 isValid） | `create` qoffscreensurface.h:31 | — |
| `void XOffscreenSurface_destroy(XOffscreenSurface* self);` | 销毁（仅清 isValid） | `destroy` qoffscreensurface.h:32 | — |
| `bool XOffscreenSurface_isValid(const XOffscreenSurface* self);` | 查是否已创建 | `isValid` qoffscreensurface.h:34 | — |
| `XWindowSurfaceType XOffscreenSurface_surfaceType(const XOffscreenSurface* self);` | 查表面类型（恒 OpenGL） | `surfaceType` qoffscreensurface.h:29 | — |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`format`/`size`/`screen`/`isValid`/`surfaceType`）；`_screen` 字段为借用（XOffscreenSurface.h:55）；无 `_utf8`/`_2` 重载；无三态 `set*`（`setFormat`/`setSize`/`setScreen` 均单值）。`setScreen` 只存指针不接管（XOffscreenSurface.c:76-77）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XOffscreenSurface)` + `XCLASS_DEFINE_EXTEND_END(XOffscreenSurface, XObject)`（XOffscreenSurface.h:44-45）。`class_init` 仅 `XVTABLE_INIT_DEFAULT` + `XVTABLE_INHERIT_XCLASS(XObject)`，**零槽位重载**（XOffscreenSurface.c:23-28）。无 `_base` 公开入口。

**注意事项与坑点**

1. **`create()` 名字被构造占用**：Qt 的「创建离屏表面」`create()` 在本类改名为 `XOffscreenSurface_createSurface`（XOffscreenSurface.h:128），因为 `XOffscreenSurface_create()` 宏是建对象入口（XOffscreenSurface.h:75）。误写「`XOffscreenSurface_create()` 之后就 isValid」不成立。
2. **isValid 不代表任何平台资源**：头文件 @note 明写「无真实离屏渲染：create() 仅置 isValid 标志，destroy() 清除；平台离屏资源与 OpenGL 上下文接入为后续扩展」（XOffscreenSurface.h:12-13），实现同为一行标志写入（XOffscreenSurface.c:82-86）。
3. **`surfaceType()` 是常量函数**：头文件称返回值「数值与 QSurface::SurfaceType 一致：Raster=0/OpenGL=1」（XOffscreenSurface.h:8-9），但实现 `(void)self;` 后恒返回 `XWindowSurface_OpenGL`（XOffscreenSurface.c:91-96），与 self 状态无关；`XWindowSurfaceType` 实际有 7 个枚举值（XWindow.h:169-178），头注释只列前两个。
4. **`format()` 的 NULL 分支会新建值**：`self` 为 NULL 时返回 `XSurfaceFormat_create()`（XOffscreenSurface.c:60-63），头文件注释为「无效时返回默认格式」（XOffscreenSurface.h:93）——值类型无泄漏风险，但注意 `XSurfaceFormat` 结构体 ≥64B（XOffscreenSurface.h:56 注释「≥64B，专项设计后再动」），按值传递/返回成本高于指针。
5. **`m_format` 为值成员而非指针**：结构体注释明示「表面格式（值类型；≥64B）」（XOffscreenSurface.h:56），故无 deinit 释放动作，`setFormat` 直接整体赋值（XOffscreenSurface.c:57-58）。

**测试覆盖**：无 `Test/XGuiTest/XOffscreenSurfaceTest.c` 专属套件（`rg -c "\bXOffscreenSurface\b" Test/` 无命中）。回归主体在仓库根 `xgui_regression_test.c:39712-39753`（`#if XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON` 段，8 条 `expect_true`：创建、默认值 1x1/未 valid/OpenGL/NULL screen、size 往返、format 往返（含 depth=24+samples=4）、screen 设置与清除、create 后 isValid、destroy 后无效）。

---

## XPerformanceOverlay

**定位**：可裁剪的性能悬浮层（FPS/帧耗时/网络速率/CPU-GPU 负载/内存用量，九宫格定位 + 拖拽）；不建独立窗口，`draw()` 画进调用方 XPainter 后备缓冲。继承链 `XObject ← XWidget ← XFrame ← XLabel ← XPerformanceOverlay`。头文件 `Src/XGui/Widget/XPerformanceOverlay.h`（336 行）。守卫 `XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON`（XPerformanceOverlay.h:26），另有逐指标开关 `XGUI_PERFORMANCE_OVERLAY_{FPS,FRAME_TIME,NETWORK,SYSSTAT,MEMORY}_ON` 与 `XGUI_PERFORMANCE_OVERLAY_UPDATE_MS`（XGuiConfig.h:806-827；嵌入式 `#undef` 块见 XGuiConfig.h:1346-1355）。

**生命周期**：`XPerformanceOverlay_init` 先 `XLabel_init` 再挂本类虚表，默认全指标可见、`m_memoryDisplay=Both`、`m_memorySource=Auto`、`m_movable=true`、背景 `0xd9000000`、几何 180x60、`XFrameShape_NoFrame`，末尾 `xperformanceoverlay_reset`；对象自有 `m_format`（`XString*`）在 `VXPerformanceOverlay_deinit` 释放（XPerformanceOverlay.c:599-636, 571-578）。栈/堆释放纪律同 XWidget。

**API 面**

枚举（XPerformanceOverlay.h:29-63）：`XPerformanceOverlayPosition`（九宫格 0..8 + Top/Bottom/Left/Right 别名）、`XPerformanceOverlayMemoryDisplay`（AmountOnly/PercentOnly/Both，互斥非位组合）、`XPerformanceOverlayMemorySource`（Auto/System/Library，互斥）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XPerformanceOverlay_init(XPerformanceOverlay* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认全指标可见/可拖拽） | X 扩展（无 Qt 对应类） | — |
| `#define XPerformanceOverlay_create(parent, flags) XPerformanceOverlay_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `XPerformanceOverlay* XPerformanceOverlay_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `#define XPerformanceOverlay_geometry(self) XWidget_geometry((const XWidget*)&((self)->m_base))` | 几何（宏转发，非函数） | `geometry` qwidget.h（XWidget 承载） | 值返回 |
| `#define XPerformanceOverlay_setPosition(self, x, y) XWidget_move(...)` | 移到 (x,y)（宏转发） | `move` qwidget.h | — |
| `#define XPerformanceOverlay_position(self) XWidget_pos(...)` | 取位置（宏转发） | `pos` qwidget.h | 值返回 |
| `#define XPerformanceOverlay_setSize(self, width, height) XWidget_resize(...)` | 改尺寸（宏转发） | `resize` qwidget.h | — |
| `#define XPerformanceOverlay_size(self) XWidget_size(...)` | 取尺寸（宏转发） | `size` qwidget.h | 值返回 |
| `void XPerformanceOverlay_setFontFamily(XPerformanceOverlay* self, const char* family);` | 设字体家族（字符串被复制） | X 扩展 | 入参借用，内部深拷贝 |
| `void XPerformanceOverlay_setTextPixelSize(XPerformanceOverlay* self, int pixelHeight);` | 设文字像素高（重算贴底高度） | X 扩展 | — |
| `void XPerformanceOverlay_setFpsVisible(XPerformanceOverlay* self, bool visible);` | 显示 FPS（裁剪时 no-op） | X 扩展 | — |
| `bool XPerformanceOverlay_isFpsVisible(const XPerformanceOverlay* self);` | 查 FPS 可见（裁剪时恒 false） | X 扩展 | — |
| `void XPerformanceOverlay_setFrameTimeVisible(XPerformanceOverlay* self, bool visible);` | 显示平均/最长帧耗时（裁剪时 no-op） | X 扩展 | — |
| `bool XPerformanceOverlay_isFrameTimeVisible(const XPerformanceOverlay* self);` | 查帧耗时可见 | X 扩展 | — |
| `void XPerformanceOverlay_setNetworkVisible(XPerformanceOverlay* self, bool visible);` | 显示下载/上传速率（裁剪时 no-op） | X 扩展 | — |
| `bool XPerformanceOverlay_isNetworkVisible(const XPerformanceOverlay* self);` | 查网络行可见 | X 扩展 | — |
| `void XPerformanceOverlay_setSysStatVisible(XPerformanceOverlay* self, bool visible);` | 显示 CPU/GPU 行（裁剪时 no-op） | X 扩展 | — |
| `bool XPerformanceOverlay_isSysStatVisible(const XPerformanceOverlay* self);` | 查系统负载行可见（裁剪时恒 false） | X 扩展 | — |
| `void XPerformanceOverlay_setMemoryVisible(XPerformanceOverlay* self, bool visible);` | 显示内存行；打开即采样一次 | X 扩展 | — |
| `bool XPerformanceOverlay_isMemoryVisible(const XPerformanceOverlay* self);` | 查内存行可见（裁剪时恒 false） | X 扩展 | — |
| `void XPerformanceOverlay_setMemoryDisplay(XPerformanceOverlay* self, XPerformanceOverlayMemoryDisplay display);` | 设内存显示种类（越界按 Both） | X 扩展 | — |
| `XPerformanceOverlayMemoryDisplay XPerformanceOverlay_memoryDisplay(const XPerformanceOverlay* self);` | 查内存显示种类（裁剪时恒 Both） | X 扩展 | — |
| `void XPerformanceOverlay_setMemorySource(XPerformanceOverlay* self, XPerformanceOverlayMemorySource source);` | 设内存来源（越界按 Auto）并立即重采样 | X 扩展 | — |
| `XPerformanceOverlayMemorySource XPerformanceOverlay_memorySource(const XPerformanceOverlay* self);` | 查内存来源 | X 扩展 | — |
| `void XPerformanceOverlay_setAutoFitSize(XPerformanceOverlay* self, bool enabled);` | 开关按文本自然尺寸收框（默认关） | X 扩展 | — |
| `bool XPerformanceOverlay_isAutoFitSize(const XPerformanceOverlay* self);` | 查是否自动收框 | X 扩展 | — |
| `void XPerformanceOverlay_setFormat(XPerformanceOverlay* self, const char* format);` | 设输出模板；NULL 恢复默认逐行拼装 | `QDateTime::toString` 模板思想（qdatetime.h，非同名 API） | 模板串内部复制，入参借用 |
| `const char* XPerformanceOverlay_format(const XPerformanceOverlay* self);` | 模板借用指针；未设置返回 NULL | 同上 | **借用禁释放**，生存期至下次 setFormat/deinit |
| `void XPerformanceOverlay_setPresetPosition(XPerformanceOverlay* self, XPerformanceOverlayPosition position, int windowWidth, int windowHeight, int margin);` | 九宫格定位（负 margin 按 0） | X 扩展 | — |
| `void XPerformanceOverlay_setMovable(XPerformanceOverlay* self, bool movable);` | 开关拖拽；关闭即结束会话 | X 扩展 | — |
| `bool XPerformanceOverlay_isMovable(const XPerformanceOverlay* self);` | 查可否拖拽 | X 扩展 | — |
| `void XPerformanceOverlay_setFixed(XPerformanceOverlay* self, bool fixed);` | 锁定/解锁位置 | X 扩展 | — |
| `bool XPerformanceOverlay_isFixed(const XPerformanceOverlay* self);` | 查是否锁定 | X 扩展 | — |
| `bool XPerformanceOverlay_beginDrag(XPerformanceOverlay* self, int x, int y);` | 开始拖拽（命中且未锁返回 true） | X 扩展 | — |
| `bool XPerformanceOverlay_dragTo(XPerformanceOverlay* self, int x, int y, int windowWidth, int windowHeight);` | 拖到新位并钳在客户区内 | X 扩展 | — |
| `void XPerformanceOverlay_endDrag(XPerformanceOverlay* self);` | 结束拖拽会话 | X 扩展 | — |
| `bool XPerformanceOverlay_isDragging(const XPerformanceOverlay* self);` | 查是否拖拽中 | X 扩展 | — |
| `void XPerformanceOverlay_reset(XPerformanceOverlay* self);` | 复位统计窗口/文本（内存行重采样） | X 扩展 | — |
| `void XPerformanceOverlay_updateFrame(XPerformanceOverlay* self, int64_t frameStartUsecs, int64_t nowUsecs);` | 记录一帧并按周期刷新 | X 扩展 | — |
| `void XPerformanceOverlay_updateNetwork(XPerformanceOverlay* self, bool available, uint64_t rxBytes, uint64_t txBytes, int64_t nowUsecs);` | 更新收发计数并算速率 | X 扩展 | — |
| `void XPerformanceOverlay_draw(XPerformanceOverlay* self, XPainter* painter);` | 画到后备缓冲（位置尺寸取当前几何） | X 扩展 | painter 借用 |

**所有权与命名契约**：`_const` 借用族=全部 `is*` 查询与 `format`（`const XPerformanceOverlay*` + `const char*` 借用返回）；`m_format` 是对象自有的 `XString*`（XPerformanceOverlay.h:92 注释「对象拥有」），`setFormat` 先删旧再按需新建（XPerformanceOverlay.c:872-884）；无 `_utf8` 后缀（`setFontFamily`/`setFormat` 入参即 UTF-8）；无 `_2` 重载；无三态 `set*`（`setAutoFitSize`/`setMovable`/`setFixed` 及四个 `set*Visible` 均为二态 bool，`setMemoryDisplay`/`setMemorySource` 为单值枚举）。几何族 5 个是**宏**而非函数（XPerformanceOverlay.h:124-128）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XPerformanceOverlay)` + `XCLASS_DEFINE_EXTEND_END(XPerformanceOverlay, XLabel)`（XPerformanceOverlay.h:65-66）。虚表槽（XPerformanceOverlay.c:583-595）：`EXClass_Copy`/`EXClass_Move`/`EXClass_Deinit`（释放 m_format 后转 XLabel）、`EXWidget_PaintEvent`（`VXPerformanceOverlay_paintEvent`，按控件树自绘保证悬浮层在最上层）、`XGESTURE_ON` 下 `EXWidget_TouchDragEvent`（触摸拖拽）。无 `_base` 公开入口。

**注意事项与坑点**

1. **头文件缺 `XPerformanceOverlay_class_init` 声明**：`XCLASS_DEFINE_BEGING/EXTEND_END` 在头文件（XPerformanceOverlay.h:65-66），但头文件全文无 `class_init` 声明（`grep -n "class_init" XPerformanceOverlay.h` 无命中），实现定义在 XPerformanceOverlay.c:580 且仅 `XClassSetVtable`（宏展开即 `Type##_class_init()`，XClass.h:54-55）内部可见。类外需要该符号的代码无法通过头文件取得原型。
2. **悬空注释**：XPerformanceOverlay.h:121 写着「通过 XClass 入口反初始化/删除/复制/移动性能悬浮层。」，紧随其后的 123 行是几何宏说明，**该注释之后没有任何声明**（同类问题在 api-surface.md 对 XWidget.h:736-737 已登记）。
3. **几何 API 是宏，不能取地址**：`XPerformanceOverlay_geometry/setPosition/position/setSize/size` 全部是 `#define`（XPerformanceOverlay.h:124-128），无法作为函数指针传递，也无法前向声明匹配。
4. **`draw()` 不会重算位置**：头文件 @details 明写「位置和尺寸来自当前控件几何；请先通过 setPosition/setSize 指定显示区域。绘制不会再次计算或覆盖调用方的位置」（XPerformanceOverlay.h:323-325）。
5. **逐指标裁剪是编译期的**：关闭某指标宏后对应 `set*Visible` 为 no-op、`is*Visible` 恒 false（XPerformanceOverlay.c:675-700 等），头文件逐条注明「裁剪…时该调用为 no-op」（XPerformanceOverlay.h:138, 145, 153, 160, 164, 168, 177, 190, 207）。
6. **网络速率单位换算不按 1000**：实现 `rxDelta * 1000000.0 / interval / 1024.0`（XPerformanceOverlay.c:1160-1165），即 KiB/s 口径，而显示层在 MB 量级时输出 "MB/s"（回归断言 `下载 3.0 MB/s`，xgui_regression_test.c:26308）。
7. **`m_cpuPercent`/`m_gpuPercent` 的 `-1` 是「无基线」而非 0**：结构体注释明示「[0,100]，-1=无基线/不支持」（XPerformanceOverlay.h:85-90），`reset()` 即置 -1（XPerformanceOverlay.c:1032-1034）。

**测试覆盖**：无 `Test/XGuiTest/XPerformanceOverlayTest.c` 专属套件。间接覆盖：`rg -c "\bXPerformanceOverlay\b" Test/` → `xgui_demo_page_overlay_settings.c`（17 处）、`xgui_window_demo.c`（3）、`xgui_demo_page_window.c`（3）、`xgui_demo_page_atv61.c`（3）、`xgui_demo_page_dialogs.c`（1）、`titlebar_gate/resize_follow_gate.sh`（1）。仓库根回归有专段 `test_performance_overlay_contract`（xgui_regression_test.c:26209-26447，30 条 `expect_true`，含九宫格 9 位置/负边距/钳位/FPS 与帧耗时数值/中文标签/可见性组合/内存来源与显示种类/模板占位符/拖拽与固定/离屏绘制像素），在 xgui_regression_test.c:42028 注册。

---

## XPlainTextEdit

**定位**：多行纯文本编辑控件壳（视口+双滚动条+内容联动），编辑能力整体委托私有控制器 `XTextControl*`。继承链 `XObject ← XWidget ← XFrame ← XAbstractScrollArea ← XPlainTextEdit`。头文件 `Src/XGui/Widget/XPlainTextEdit.h`（572 行）。守卫 `XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON`（XPlainTextEdit.h:61）；控制器另有总开关 `XTEXTCONTROL_ON`（XPlainTextEdit.h:35-36）。

**生命周期**：`XPlainTextEdit_init` 设默认 `m_wrapMode=WidgetWidth`、`m_undoEnabled=true`、`m_backgroundVisible=true`、`m_tabStopDistance=40`、`m_wordWrapMode=XTextControlWrap_WordWrap`，建 `m_documentTitle`/`m_extraSelCache`/`m_lines`/`m_undoStack`/`m_redoStack`，并 `XTextControl_create_ex` 建控制器（deinit 销毁）；焦点策略 StrongFocus（XPlainTextEdit.c:1328-1390）。栈/堆释放纪律同 XWidget；控制器与全部 `XString*`/`XVector*` 由 `VXPlainTextEdit_deinit` 释放（XPlainTextEdit.c:1324 槽登记）。

**API 面**

枚举/结构（XPlainTextEdit.h:67-89）：`XPlainTextEditMode`（NoWrap=0/WidgetWidth=1，数值对齐 `QPlainTextEdit::LineWrapMode` qplaintextedit.h:51-54）、`XPlainTextEditExtraSelection{line,col,length,color}`。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XPlainTextEdit_class_init(void);` | 建表（继承 XAbstractScrollArea） | 同名惯用 | 共享表，禁释放 |
| `void XPlainTextEdit_init(XPlainTextEdit* self, XWidget* parent, XWidgetFlags flags);` | 初始化（建控制器与镜像容器） | `QPlainTextEdit(QWidget*)` qplaintextedit.h:58 | — |
| `#define XPlainTextEdit_create(parent, flags) XPlainTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XPlainTextEdit* XPlainTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8);` | 整篇替换纯文本 | `setPlainText` qplaintextedit.h:157 | 入参借用 |
| `char* XPlainTextEdit_toPlainText(const XPlainTextEdit* self);` | 导出纯文本（`\n` 连接） | `toPlainText` qplaintextedit.h:116 | **堆拷贝，调用方 `XFree_System`** |
| `void XPlainTextEdit_appendPlainText(XPlainTextEdit* self, const char* utf8);` | 追加一行 | `appendPlainText` qplaintextedit.h:173 | 入参借用 |
| `void XPlainTextEdit_insertPlainText(XPlainTextEdit* self, const char* utf8);` | 光标处插入 | `insertPlainText` qplaintextedit.h:171 | 入参借用 |
| `void XPlainTextEdit_clear(XPlainTextEdit* self);` | 清空内容 | `clear` qplaintextedit.h:168 | — |
| `bool XPlainTextEdit_isReadOnly(const XPlainTextEdit* self);` | 查只读 | `isReadOnly` qplaintextedit.h:70 | — |
| `void XPlainTextEdit_setReadOnly(XPlainTextEdit* self, bool readOnly);` | 设只读（剥 TextEditable） | `setReadOnly` qplaintextedit.h:71 | — |
| `int XPlainTextEdit_lineWrapMode(const XPlainTextEdit* self);` | 查换行模式 | `lineWrapMode` qplaintextedit.h:99 | — |
| `void XPlainTextEdit_setLineWrapMode(XPlainTextEdit* self, int mode);` | 设换行模式（下发控制器） | `setLineWrapMode` qplaintextedit.h:100 | — |
| `int XPlainTextEdit_maximumBlockCount(const XPlainTextEdit* self);` | 查块数上限 | `maximumBlockCount` qplaintextedit.h:95 | — |
| `void XPlainTextEdit_setMaximumBlockCount(XPlainTextEdit* self, int maximum);` | 设块数上限并立即裁剪 | `setMaximumBlockCount` qplaintextedit.h:93 | — |
| `void XPlainTextEdit_setPlaceholderText(XPlainTextEdit* self, const char* utf8);` | 设占位文本 | `setPlaceholderText` qplaintextedit.h:64 | 入参借用，内部复制 |
| `const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self);` | 查占位文本 | `placeholderText` qplaintextedit.h:65 | **借用禁释放** |
| `bool XPlainTextEdit_isUndoRedoEnabled(const XPlainTextEdit* self);` | 查撤销重做开关 | `isUndoRedoEnabled` qplaintextedit.h:88 | — |
| `void XPlainTextEdit_setUndoRedoEnabled(XPlainTextEdit* self, bool enable);` | 设撤销重做开关 | `setUndoRedoEnabled` qplaintextedit.h:90 | — |
| `int XPlainTextEdit_cursorLine(const XPlainTextEdit* self);` | 光标行（0 起） | `QTextCursor::blockNumber` qtextcursor.h:190 | — |
| `int XPlainTextEdit_cursorColumn(const XPlainTextEdit* self);` | 光标列（行内 UTF-8 字节偏移） | `QTextCursor::positionInBlock` qtextcursor.h:54 | — |
| `XRect XPlainTextEdit_cursorRect(const XPlainTextEdit* self);` | 光标竖线矩形（控件局部坐标） | `cursorRect` qplaintextedit.h:129 | 值返回 |
| `XString* XPlainTextEdit_anchorAt(const XPlainTextEdit* self, const XPoint* pos);` | 坐标处超链接 href | `anchorAt` qplaintextedit.h:131 | **堆新建，调用方 `XClassDelete`** |
| `bool XPlainTextEdit_find(XPlainTextEdit* self, const char* text, int flags);` | 从光标处查找（1=FindBackward） | `find` qplaintextedit.h:111 | 入参借用 |
| `void XPlainTextEdit_setTextCursor(XPlainTextEdit* self, int line, int col);` | 设光标（行列简化，无选区） | `setTextCursor` qplaintextedit.h:67 | — |
| `int XPlainTextEdit_textCursorLine(const XPlainTextEdit* self);` | cursorLine 的 Qt 命名别名 | `QTextCursor::blockNumber` qtextcursor.h:190 | — |
| `int XPlainTextEdit_textCursorColumn(const XPlainTextEdit* self);` | cursorColumn 的 Qt 命名别名 | `QTextCursor::positionInBlock` qtextcursor.h:54 | — |
| `void XPlainTextEdit_undo(XPlainTextEdit* self);` | 撤销 | `undo` qplaintextedit.h:165 | — |
| `int XPlainTextEdit_blockCount(const XPlainTextEdit* self);` | 逻辑块数（不含软换行） | `blockCount` qplaintextedit.h:151 | — |
| `int XPlainTextEdit_lineCount(const XPlainTextEdit* self);` | 可视行数（折行后） | 无同名成员；等价物 `QTextDocument::lineCount` qtextdocument.h:242 | — |
| `bool XPlainTextEdit_canPaste(const XPlainTextEdit* self);` | 可粘贴（可编辑且剪贴板有文本） | `canPaste` qplaintextedit.h:147 | — |
| `void XPlainTextEdit_setCursorWidth(XPlainTextEdit* self, int width);` | 设光标宽（-1 取样式度量） | `setCursorWidth` qplaintextedit.h:140 | — |
| `int XPlainTextEdit_cursorWidth(const XPlainTextEdit* self);` | 查光标宽 | `cursorWidth` qplaintextedit.h:139 | — |
| `void XPlainTextEdit_setCenterCursor(XPlainTextEdit* self, bool center);` | 设「光标居中」标志 | **无同名**；Qt `centerCursor()` 是一次滚动动作 qplaintextedit.h:176 | — |
| `bool XPlainTextEdit_centerCursor(const XPlainTextEdit* self);` | 查「光标居中」标志 | 同上（同名不同义） | — |
| `void XPlainTextEdit_setCenterOnScroll(XPlainTextEdit* self, bool on);` | 设滚动跟随光标 | `setCenterOnScroll` qplaintextedit.h:108 | — |
| `bool XPlainTextEdit_centerOnScroll(const XPlainTextEdit* self);` | 查滚动跟随光标 | `centerOnScroll` qplaintextedit.h:109 | — |
| `void XPlainTextEdit_setBackgroundVisible(XPlainTextEdit* self, bool visible);` | 设背景可见 | `setBackgroundVisible` qplaintextedit.h:105 | — |
| `bool XPlainTextEdit_backgroundVisible(const XPlainTextEdit* self);` | 查背景可见 | `backgroundVisible` qplaintextedit.h:106 | — |
| `void XPlainTextEdit_setTabChangesFocus(XPlainTextEdit* self, bool change);` | 设 Tab 切焦点 | `setTabChangesFocus` qplaintextedit.h:81 | — |
| `bool XPlainTextEdit_tabChangesFocus(const XPlainTextEdit* self);` | 查 Tab 切焦点 | `tabChangesFocus` qplaintextedit.h:80 | — |
| `void XPlainTextEdit_setTabStopDistance(XPlainTextEdit* self, int distance);` | 设 Tab 步进（px，仅接受 >0） | `setTabStopDistance` qplaintextedit.h:137（qreal） | — |
| `int XPlainTextEdit_tabStopDistance(const XPlainTextEdit* self);` | 查 Tab 步进 | `tabStopDistance` qplaintextedit.h:136 | — |
| `void XPlainTextEdit_setOverwriteMode(XPlainTextEdit* self, bool overwrite);` | 设改写模式（下发控制器） | `setOverwriteMode` qplaintextedit.h:134 | — |
| `bool XPlainTextEdit_overwriteMode(const XPlainTextEdit* self);` | 查改写模式 | `overwriteMode` qplaintextedit.h:133 | — |
| `void XPlainTextEdit_setWordWrapMode(XPlainTextEdit* self, int mode);` | 设断行规则（XTextControlWrap 0..4） | `setWordWrapMode` qplaintextedit.h:103 | — |
| `int XPlainTextEdit_wordWrapMode(const XPlainTextEdit* self);` | 查断行规则 | `wordWrapMode` qplaintextedit.h:102 | — |
| `void XPlainTextEdit_setTextInteractionFlags(XPlainTextEdit* self, int flags);` | 设交互标志位集 | `setTextInteractionFlags` qplaintextedit.h:73 | — |
| `int XPlainTextEdit_textInteractionFlags(const XPlainTextEdit* self);` | 查交互标志位集 | `textInteractionFlags` qplaintextedit.h:74 | — |
| `void XPlainTextEdit_setDocumentTitle(XPlainTextEdit* self, const XString* title);` | 设文档标题 | `setDocumentTitle` qplaintextedit.h:83 | 入参借用，内部复制 |
| `void XPlainTextEdit_setDocumentTitle_2(XPlainTextEdit* self, const char* utf8);` | 设文档标题（UTF-8 便利版） | 同上 | 入参借用 |
| `XString* XPlainTextEdit_documentTitle(const XPlainTextEdit* self);` | 查文档标题 | `documentTitle` qplaintextedit.h:85 | **堆拷贝，调用方 `XClassDelete`** |
| `void XPlainTextEdit_moveCursor(XPlainTextEdit* self, int operation, int mode);` | 移动光标（operation 对齐 QTextCursor::MoveOperation） | `moveCursor` qplaintextedit.h:145 | — |
| `void XPlainTextEdit_appendHtml(XPlainTextEdit* self, const char* html);` | 追加 HTML 片段 | `appendHtml` qplaintextedit.h:174 | 入参借用 |
| `void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self, bool available);` | undoAvailable 信号（真发射，控制器转发） | `undoAvailable` qplaintextedit.h:183 | 信号标识，禁解引用 |
| `void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self, bool available);` | redoAvailable 信号 | `redoAvailable` qplaintextedit.h:184 | 同上 |
| `void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self, bool available);` | copyAvailable 信号 | `copyAvailable` qplaintextedit.h:185 | 同上 |
| `void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self, bool changed);` | modificationChanged 信号 | `modificationChanged` qplaintextedit.h:191 | 同上 |
| `void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self, int newCount);` | blockCountChanged 信号 | `blockCountChanged` qplaintextedit.h:190 | 同上 |
| `void XPlainTextEdit_redo(XPlainTextEdit* self);` | 重做 | `redo` qplaintextedit.h:166 | — |
| `void XPlainTextEdit_copy(XPlainTextEdit* self);` | 复制选区 | `copy` qplaintextedit.h:161 | — |
| `void XPlainTextEdit_cut(XPlainTextEdit* self);` | 剪切选区 | `cut` qplaintextedit.h:160 | — |
| `void XPlainTextEdit_paste(XPlainTextEdit* self);` | 粘贴 | `paste` qplaintextedit.h:162 | — |
| `void XPlainTextEdit_selectAll(XPlainTextEdit* self);` | 全选 | `selectAll` qplaintextedit.h:169 | — |
| `void XPlainTextEdit_ensureCursorVisible(XPlainTextEdit* self);` | 确保光标可见 | `ensureCursorVisible` qplaintextedit.h:119 | — |
| `XPoint XPlainTextEdit_cursorForPosition(const XPlainTextEdit* self, const XPoint* pos);` | 坐标→(行,列) 承载 | `cursorForPosition` qplaintextedit.h:127 | 值返回 |
| `XMenu* XPlainTextEdit_createStandardContextMenu(XPlainTextEdit* self);` | 标准右键菜单（`XMENU_ON`） | `createStandardContextMenu` qplaintextedit.h:123 | **所有权转移给调用方，`XClassDelete`** |
| `int XPlainTextEdit_currentCharFormat(const XPlainTextEdit* self);` | 当前字符格式（int 位值） | `currentCharFormat` qplaintextedit.h:78 | — |
| `void XPlainTextEdit_setCurrentCharFormat(XPlainTextEdit* self, int format);` | 设当前字符格式 | `setCurrentCharFormat` qplaintextedit.h:77 | — |
| `void XPlainTextEdit_mergeCurrentCharFormat(XPlainTextEdit* self, int format);` | 合并格式（位值按位或） | `mergeCurrentCharFormat` qplaintextedit.h:76 | — |
| `XTextDocument* XPlainTextEdit_document(const XPlainTextEdit* self);` | 内部文档（`XTEXTDOCUMENT_ON`） | `document` qplaintextedit.h:62 | **借用禁释放** |
| `void XPlainTextEdit_setDocument(XPlainTextEdit* self, XTextDocument* doc);` | 换入文档（NULL 建空文档） | `setDocument` qplaintextedit.h:61 | **引用转移给控制器**，调用方不得再释放 |
| `int XPlainTextEdit_extraSelections(const XPlainTextEdit* self, const XPlainTextEditExtraSelection** selections);` | 读额外选择集（返回条目数） | `extraSelections` qplaintextedit.h:143 | 输出数组**借用禁释放** |
| `void XPlainTextEdit_setExtraSelections(XPlainTextEdit* self, const XPlainTextEditExtraSelection* selections, int count);` | 设额外选择集（拷贝语义） | `setExtraSelections` qplaintextedit.h:142 | 入参借用 |
| `XVariant* XPlainTextEdit_loadResource(XPlainTextEdit* self, int type, const char* name);` | 载入资源（恒 NULL） | `loadResource` qplaintextedit.h:121 | 恒 NULL，无所有权 |
| `XPoint XPlainTextEdit_textCursor(const XPlainTextEdit* self);` | 光标位置承载（x=行，y=列） | `textCursor` qplaintextedit.h:68（Qt 返回 QTextCursor） | 值返回 |
| `void XPlainTextEdit_zoomIn(XPlainTextEdit* self, int range);` | 放大字体（点大小+像素字号同步） | `zoomIn` qplaintextedit.h:178 | — |
| `void XPlainTextEdit_zoomOut(XPlainTextEdit* self, int range);` | 缩小字体（等价 zoomIn(-range)） | `zoomOut` qplaintextedit.h:179 | — |
| `bool XPlainTextEdit_hasSelectedText(const XPlainTextEdit* self);` | 是否存在选区（锚点≠位置） | `QTextCursor::hasSelection` qtextcursor.h:114 | — |
| `char* XPlainTextEdit_selectedText(const XPlainTextEdit* self);` | 选中文本 | `QTextCursor::selectedText` qtextcursor.h:121 | **堆拷贝，调用方 `XFree_System`**；无选区返回 NULL |
| `void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self);` | textChanged（控制器转发真发射） | `textChanged` qplaintextedit.h:182 | 信号标识 |
| `void* XPlainTextEdit_cursorPositionChanged_signal(XPlainTextEdit* self);` | cursorPositionChanged | `cursorPositionChanged` qplaintextedit.h:187 | 信号标识 |
| `void* XPlainTextEdit_updateRequest_signal(XPlainTextEdit* self, const XRect* rect, int dy);` | updateRequest（rect+dy 载荷） | `updateRequest` qplaintextedit.h:189 | 信号标识 |
| `void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self);` | selectionChanged | `selectionChanged` qplaintextedit.h:186 | 信号标识 |

**所有权与命名契约**：`_const` 借用族=查询取 const self，其中 `placeholderText`/`updateRequest` 之外的字符串返回分两类——**借用**（`placeholderText` 返回内部 UTF-8 指针，XPlainTextEdit.c:1530-1538）与**堆拷贝**（`toPlainText`/`selectedText` 返回 `XFree_System` 释放的 char*；`anchorAt`/`documentTitle` 返回 `XClassDelete` 释放的 XString*），头文件逐条 @return 注明。`_2` 重载=`setDocumentTitle_2`（`const XString*` → `const char*`，XPlainTextEdit.h:328-329）；按 XGui.md §8.2.1:1813 的 `_2`→`_utf8` 改名立项，该名属待改批次。无三态 `set*`（`setReadOnly`/`setUndoRedoEnabled`/`setCenterCursor`/`setCenterOnScroll`/`setBackgroundVisible`/`setTabChangesFocus`/`setOverwriteMode` 均二态 bool，其余为单值枚举/数值）。`@compat` 镜像字段（`m_lines`/`m_undoStack`/`m_redoStack`）只读，权威在控制器（XPlainTextEdit.h:107-115）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XPlainTextEdit)` + `XCLASS_DEFINE_EXTEND_END(XPlainTextEdit, XAbstractScrollArea)`（XPlainTextEdit.h:91-92）。虚表槽（XPlainTextEdit.c:1303-1325）：`EXWidget_KeyPressEvent`/`PaintEvent`/`MousePressEvent`/`MouseReleaseEvent`/`MouseDoubleClickEvent`/`MouseMoveEvent`/`FocusInEvent`/`FocusOutEvent`/`InputMethodEvent`/`ContextMenuEvent`/`ResizeEvent`/`WheelEvent`（切视口↔内容坐标后交控制器）、`EXWidget_InputMethodQuery`（`XINPUTMETHOD_ON` 下委托控制器真实状态）、`EXObject_TimerEvent`（拖选边缘自动滚动）、`EXAbstractScrollArea_ScrollContentsBy`、`EXClass_Deinit`。无 `_base` 公开入口（本类实现内不使用任何 `X*_base` 转发）。

**注意事项与坑点**

1. **`lineCount` 的对标名不存在**：头文件注释「可视行数（对标 QPlainTextEdit::lineCount，可视行口径）」（XPlainTextEdit.h:304-306），但 Qt 6.8.3 `qplaintextedit.h` 无 `lineCount`（`grep -n "lineCount" qplaintextedit.h` 无命中），Qt 的行数接口在 `QTextDocument::lineCount()`（qtextdocument.h:242）。X 侧语义（折行后可视行）也与 `QTextDocument::lineCount`（逻辑布局行）不完全同源。
2. **`centerCursor` 同名不同义**：X 的 `XPlainTextEdit_setCenterCursor`/`centerCursor` 是一对「设置/查询 bool 属性」（XPlainTextEdit.h:312-313），Qt 的 `QPlainTextEdit::centerCursor()` 是**无参 void 槽**、执行「滚动使光标居中」动作（qplaintextedit.h:176）。把 Qt 用法直译过来会编译失败或语义反转。
3. **`maximumBlockCount`/`tabStopDistance` 被窄字段吞值**：字段为 `uint16_t`（XPlainTextEdit.h:140-141），而 setter 只拦 `maximum < 0`（XPlainTextEdit.c:1515）与 `distance > 0`（XPlainTextEdit.c:1830），超过 65535 的值被静默截断；Qt 的 `tabStopDistance` 是 `qreal`（qplaintextedit.h:136）。两处头文件 @brief（XPlainTextEdit.h:198-204、320-321）均未提上限。
4. **`mergeCurrentCharFormat` 是简化语义**：头文件 @note 明写「Qt 按属性粒度合并 QTextCharFormat（仅覆盖修饰中显式置位的属性），此处简化为位值按位或覆盖」（XPlainTextEdit.h:412-414）。
5. **额外选择集承载与 Qt 不同**：头文件 @note 明写「Qt 条目携带 QTextCursor，可跨块」（XPlainTextEdit.h:79-81），X 用 `{line, col, length, color}` 平铺；`extraSelections` 的输出数组来自内部换算缓存，借用且随下次调用失效（XPlainTextEdit.h:446-453）。
6. **`loadResource` 恒返回 NULL**：头文件注释「平铺模型无资源存储…恒返回 NULL」（XPlainTextEdit.h:469-475），实现亦恒 NULL 转发（XPlainTextEdit.c:2057-2063）。
7. **控制器裁剪时的降级契约**：头文件第 55-59 行明写「控制器模块裁剪时…壳退化为仅滚动/绘制职责，控制器指针恒空，全部编辑 API 无操作」。
8. **增量布局是历史根修项**：XGui.md §8.2:1775 记「XPlainTextEdit 增量布局 ✅ 已修 2026-09-21 §8.0e：单逻辑行编辑局部更新可视行段…2000 次编辑全增量与全量参照逐条一致」。

**测试覆盖**：无 `Test/XGuiTest/XPlainTextEditTest.c` 专属套件。间接覆盖（`rg -c "\bXPlainTextEdit\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_text.c`（71 处）、`Test/XGuiTest/XKeyboardTest.c`（6 处，含 `XPlainTextEdit_clear`/`insertPlainText`/`toPlainText` 全选粘贴替换链路，XKeyboardTest.c:1373-1496）、`xgui_window_demo.c`（3）、`xgui_demo_page_advanced.c`（3）。仓库根回归另有虚表冒烟一条（xgui_regression_test.c:30932）。

---

## XProgressBar

**定位**：水平/垂直进度条（范围/值钳位、`%p`/`%v`/`%m` 文本模板、外观翻转、垂直文本方向、分两段着色的居中文本）。继承链 `XObject ← XWidget ← XProgressBar`。头文件 `Src/XGui/Widget/XProgressBar.h`（390 行）。守卫 `XWIDGET_ON && XPROGRESSBAR_ON`（XProgressBar.h:40；开关定义 XGuiConfig.h:533-535）。

**生命周期**：`XProgressBar_init` 先 `XWidget_init` 再挂虚表，设范围 0..100、值 0、水平、文本可见、`m_format = XString_create_utf8("%p%")`、对齐 HCenter|VCenter，末尾重算尺寸提示；`VXProgressBar_deinit` 释放 `m_format` 后转父类（XProgressBar.c:609-626, 513-521）。栈/堆释放纪律同 XWidget；`m_format` 对象自有（XProgressBar.h:90）。

**API 面**

枚举（XProgressBar.h:45-58）：`XProgressBarOrientation`（Horizontal=1/Vertical=2，数值对齐 `Qt::Orientation`）、`XProgressBarDirection`（TopToBottom=0/BottomToTop=1，数值对齐 `QProgressBar::Direction` qprogressbar.h:32）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XProgressBar_class_init(void);` | 建表（继承 XWidget） | 同名惯用 | 共享表，禁释放 |
| `void XProgressBar_init(XProgressBar* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 0..100/值 0/水平/文本可见/"%p%"） | `QProgressBar(QWidget*)` qprogressbar.h:35 | — |
| `#define XProgressBar_create(parent, flags) XProgressBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XProgressBar* XProgressBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `int XProgressBar_minimum(const XProgressBar* self);` | 查下限；NULL 返回 0 | `minimum` qprogressbar.h:38 | — |
| `int XProgressBar_maximum(const XProgressBar* self);` | 查上限；NULL 返回 100 | `maximum` qprogressbar.h:39 | — |
| `void XProgressBar_setRange(XProgressBar* self, int min, int max);` | 设范围（min>max 收敛；越界值走 reset） | `setRange` qprogressbar.h:66 | — |
| `void XProgressBar_setMinimum(XProgressBar* self, int minimum);` | 单独设下限（上限联动抬升） | `setMinimum` qprogressbar.h:67 | — |
| `void XProgressBar_setMaximum(XProgressBar* self, int maximum);` | 单独设上限（下限联动下压） | `setMaximum` qprogressbar.h:68 | — |
| `int XProgressBar_value(const XProgressBar* self);` | 查当前值（复位态返回 minimum-1） | `value` qprogressbar.h:41 | — |
| `void XProgressBar_setValue(XProgressBar* self, int value);` | 设值（钳位；busy 模式不钳；变化才发信号） | `setValue` qprogressbar.h:69 | — |
| `void XProgressBar_reset(XProgressBar* self);` | 复位到 minimum-1（不发 valueChanged） | `reset` qprogressbar.h:65 | — |
| `int XProgressBar_orientation(const XProgressBar* self);` | 查方向；NULL 返回水平 | `orientation` qprogressbar.h:53 | — |
| `void XProgressBar_setOrientation(XProgressBar* self, int orientation);` | 设方向（非法值忽略）并重算提示 | `setOrientation` qprogressbar.h:70 | — |
| `bool XProgressBar_invertedAppearance(const XProgressBar* self);` | 查外观翻转 | `invertedAppearance` qprogressbar.h:56 | — |
| `void XProgressBar_setInvertedAppearance(XProgressBar* self, bool inverted);` | 设外观翻转 | `setInvertedAppearance` qprogressbar.h:55 | — |
| `bool XProgressBar_isTextVisible(const XProgressBar* self);` | 查文本可见；NULL 返回 true | `isTextVisible` qprogressbar.h:45 | — |
| `void XProgressBar_setTextVisible(XProgressBar* self, bool visible);` | 设文本可见 | `setTextVisible` qprogressbar.h:44 | — |
| `int XProgressBar_textDirection(const XProgressBar* self);` | 查垂直文本方向 | `textDirection` qprogressbar.h:58 | — |
| `void XProgressBar_setTextDirection(XProgressBar* self, int textDirection);` | 设垂直文本方向（非法值忽略） | `setTextDirection` qprogressbar.h:57 | — |
| `int XProgressBar_alignment(const XProgressBar* self);` | 查文本对齐（默认 HCenter\|VCenter） | `alignment` qprogressbar.h:47 | — |
| `void XProgressBar_setAlignment(XProgressBar* self, int alignment);` | 设文本对齐并重绘 | `setAlignment` qprogressbar.h:48 | — |
| `const char* XProgressBar_format(const XProgressBar* self);` | 查格式串（默认 "%p%"） | `format` qprogressbar.h:62 | **借用禁释放**（内部 XString） |
| `void XProgressBar_setFormat(XProgressBar* self, const char* format);` | 设格式串；NULL 恢复 "%p%" | `setFormat` qprogressbar.h:60 | 入参借用，内部复制 |
| `void XProgressBar_resetFormat(XProgressBar* self);` | 恢复默认 "%p%" | `resetFormat` qprogressbar.h:61 | — |
| `void XProgressBar_text(const XProgressBar* self, char* out, int outSize);` | 生成格式化文本（复位/busy 为空串） | `text` qprogressbar.h:43（Qt 返回 QString） | out 由调用方提供 |
| `void* XProgressBar_valueChanged_signal(XProgressBar* self, int value);` | valueChanged(int) 信号 | `valueChanged` qprogressbar.h:73 | 信号标识，禁解引用 |

**所有权与命名契约**：`_const` 借用族=全部查询取 const self，`format` 返回内部 `XString` 的 UTF-8 借用指针（XProgressBar.c:793-799）——按 XGui.md §8.2.1:1811 的「`_const` 改名」立项，`format` 属未加 `_const` 的借用型 getter。无 `_utf8`/`_2` 重载；无三态 `set*`（`setInvertedAppearance`/`setTextVisible` 为二态 bool，`setOrientation`/`setTextDirection`/`setAlignment` 为单值）。父类 API 以 30 条宏转发（`XProgressBar_setEnabled`/`isVisible`/`geometry`/`setFocusPolicy`/`contentsMargins` 等，XProgressBar.h:141-170）；拷贝/移动统一走 `XClassCopy`/`XClassMove`（XProgressBar.h:172-174 注释）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XProgressBar)` + `XCLASS_DEFINE_EXTEND_END(XProgressBar, XWidget)`（XProgressBar.h:61-62）。虚表槽（XProgressBar.c:545-558）：`EXWidget_SizeHint`→`VXProgressBar_sizeHintSlot`（先重算缓存再读 `XWidget_sizeHint_base`）、`EXWidget_MinimumSizeHint`→`VXProgressBar_minimumSizeHintSlot`、`EXObject_Event`→`VXProgressBar_event`（仅转父类）、`EXWidget_PaintEvent`→`VXProgressBar_paintEvent`、`EXWidget_ChangeEvent`→`VXProgressBar_changeEvent`（恒 `XEvent_ignore`）、`EXClass_Copy`/`EXClass_Move`/`EXClass_Deinit`。`_base` 入口：`XWidget_sizeHint_base`（XProgressBar.c:535）、`XWidget_minimumSizeHint_base`（XProgressBar.c:539 起）。

**注意事项与坑点**

1. **格式串「31 字符截断」是过时注释**：头文件 `setFormat` @details 写「超长串截断到内部缓冲上限（31 字符 + NUL）」（XProgressBar.h:347），但实现走 `XString_assign_utf8` 全量保存、无任何长度判断（XProgressBar.c:801-809），且 `XPROGRESSBAR_FORMAT_MAX 32`（XProgressBar.c:48）定义后**全文件零引用**（`rg -n "XPROGRESSBAR_FORMAT_MAX"` 仅命中第 48 行）。
2. **`format` 头注释自相矛盾**：同一段 @brief 既写「`@param self 进度条对象；不可为 NULL`」又写「`self 为 NULL 时返回默认 "%p%"`」（XProgressBar.h:337-343），实现确实对 NULL 返回字面量 `"%p%"`（XProgressBar.c:796）。
3. **`value()` 不钳位与 XGui.md 记载相反**：XGui.md §8.0g28:1890-1892 记「value() getter 补 Qt 哨兵钳位（reset 置 minimum-1 后 getter 显示 minimum）」，而实现注释与代码明确「value() 直接返回内部值，不做任何钳位——reset() 置的 minimum-1 哨兵…必须原样可见」（XProgressBar.c:686-696），测试亦断言 `reset` 后 `== -1`（XProgressBarTest.c:75-76）。以实现与测试为准。
4. **悬空注释两行**：XProgressBar.h:136-137 两条 `/** @brief … */`（「反初始化（宏复用基类 XWidget 的 deinit 入口）」/「释放堆对象（宏复用基类 XWidget 的 delete 入口）」）之后没有任何声明，直接进入宏转发段（XProgressBar.h:139）——与 api-surface.md 对 XWidget.h:736-737 登记的同型问题一致。
5. **`text()` 的 busy 判定依赖 `setRange(0,0)`**：头文件明写「不确定进度（setRange(0, 0)）进入不确定进度（busy）模式」（XProgressBar.h:197），实现以 `m_max==0 && m_min==0` 判 busy（XProgressBar.c 内 `xprogressbar_isBusy`），故 `setRange(0,0)` 下 `setValue` 接受任意值、`text()` 返回空串。
6. **`setRange` 越界走 reset 而非钳位**：头文件 @details 写「当前值落到新范围外（value < min-1 或 value > max）则走 reset()（置 minimum-1、不发 valueChanged）」（XProgressBar.h:194-197），与 Qt 一致（qprogressbar.cpp:320-331）。
7. **`changeEvent` 是空实现且不转父类**：`VXProgressBar_changeEvent` 直接 `XEvent_ignore(event)`（XProgressBar.c:433-437），类注释标为「同 XWidget 默认，无状态重算需求」。

**测试覆盖**：有专属套件 `Test/XGuiTest/XProgressBarTest.c`（138 行）。入口 `bool XProgressBarTest_runAll(void)`（XProgressBarTest.c:18），断言工具为局部 `xp_expect`（XProgressBarTest.c:10-16）；断言调用点 **28 处**（`grep -c "^    xp_expect(" Test/XGuiTest/XProgressBarTest.c` → 28；`grep -o "xp_expect(" | wc -l` → 29，多出的 1 处为函数定义行）。覆盖默认值、setValue 钳位、setRange 收敛与越界 reset、setMinimum/setMaximum 联动、reset 哨兵、方向/文本方向/翻转、对齐、格式串与 `%v/%m/%p` 替换、resetFormat。注册点 `xgui_regression_test.c:36230`（`expect_true(XProgressBarTest_runAll(), "XProgressBar 控件功能")`），文件编入 `XGuiRegression_Test` 目标（Test/CMakeLists.txt:558）；另有虚表冒烟 `xgui_regression_test.c:30964`。

---

## XProgressDialog

**定位**：带取消能力的进度对话框（范围/值、提示与取消按钮文本、autoReset/autoClose/minimumDuration、非阻塞 `open_2`）。继承链 `XObject ← XWidget ← XDialog ← XProgressDialog`。头文件 `Src/XGui/Widget/XProgressDialog.h`（359 行）。守卫 `XWIDGET_ON && XDIALOG_ON`（XProgressDialog.h:41）。

**生命周期**：`XProgressDialog_init` 先给 flags 补 `XWindowType_MSWindowsFixedSizeDialogHint`（XProgressDialog.c:454），再 `XDialog_init` 并设默认 0..100/值 0/`m_minimumDuration=4000`/autoReset=true/autoClose=true，写默认窗口标题「正在执行」，末尾 `updateSize` + 自动显示定时器状态槽登记；`VXProgressDialog_deinit` 停定时器、释放 `m_labelText`/`m_cancelButtonText`、删除自有 `m_label`/`m_cancelButton`（XProgressDialog.c:418-430）。`init_full` 是带 label/cancel 文本+范围的重载（XProgressDialog.h:111-113）。

**API 面**

结构字段（XProgressDialog.h:59-83）：`m_bar` **借用**；`m_label`/`m_cancelButton` **拥有**；`m_openReceiver`/`m_openMember` 记录 open_2 连接。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XProgressDialog_class_init(void);` | 建表（继承 XDialog） | 同名惯用 | 共享表，禁释放 |
| `void XProgressDialog_init(XProgressDialog* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 0..100、4000ms、autoReset/autoClose 真） | `QProgressDialog(QWidget*,WindowFlags)` qprogressdialog.h:35 | — |
| `void XProgressDialog_init_full(XProgressDialog* self, const XString* labelText, const XString* cancelButtonText, int minimum, int maximum, XWidget* parent);` | 带文本+范围的初始化 | `QProgressDialog(labelText,cancelButtonText,min,max,parent)` qprogressdialog.h:36-38 | 文本入参借用，内部复制 |
| `#define XProgressDialog_create(parent, flags) XProgressDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XProgressDialog* XProgressDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `void XProgressDialog_setRange(XProgressDialog* self, int minimum, int maximum);` | 设范围（min>max 自动交换） | `setRange` qprogressdialog.h:70 | — |
| `void XProgressDialog_setMinimum(XProgressDialog* self, int minimum);` | 设最小值 | `setMinimum` qprogressdialog.h:69 | — |
| `void XProgressDialog_setMaximum(XProgressDialog* self, int maximum);` | 设最大值 | `setMaximum` qprogressdialog.h:68 | — |
| `int XProgressDialog_minimum(const XProgressDialog* self);` | 查最小值；NULL 返回 0 | `minimum` qprogressdialog.h:47 | — |
| `int XProgressDialog_maximum(const XProgressDialog* self);` | 查最大值；NULL 返回 100 | `maximum` qprogressdialog.h:48 | — |
| `void XProgressDialog_setValue(XProgressDialog* self, int progress);` | 设值（钳位；达 max 且 autoReset 时 reset） | `setValue` qprogressdialog.h:71 | — |
| `int XProgressDialog_value(const XProgressDialog* self);` | 查当前值 | `value` qprogressdialog.h:50 | — |
| `void XProgressDialog_reset(XProgressDialog* self);` | 复位到 minimum、清 wasCanceled、autoClose 时隐藏 | `reset` qprogressdialog.h:67 | — |
| `void XProgressDialog_setLabelText(XProgressDialog* self, const XString* text);` | 设提示文本（有自定义标签时转发） | `setLabelText` qprogressdialog.h:72 | 入参借用，内部复制 |
| `XString* XProgressDialog_labelText(const XProgressDialog* self);` | 取提示文本 | `labelText` qprogressdialog.h:54 | **堆拷贝，调用方 `XClassDelete`** |
| `void XProgressDialog_setCancelButtonText(XProgressDialog* self, const XString* text);` | 设取消按钮文本（有自定义按钮时转发） | `setCancelButtonText` qprogressdialog.h:73 | 入参借用，内部复制 |
| `void XProgressDialog_setLabel(XProgressDialog* self, XLabel* label);` | 设自定义标签（`XFRAME_ON && XLABEL_ON`） | `setLabel` qprogressdialog.h:41 | **引用转移给对话框**，旧标签被删 |
| `XLabel* XProgressDialog_label(const XProgressDialog* self);` | 取自定义标签 | `label` qprogressdialog.h:41 同段 | 借用禁释放 |
| `void XProgressDialog_setCancelButton(XProgressDialog* self, XPushButton* button);` | 设自定义取消按钮（`XABSTRACTBUTTON_ON && XPUSHBUTTON_ON`） | `setCancelButton` qprogressdialog.h:42 | **引用转移给对话框**，旧按钮被删 |
| `XPushButton* XProgressDialog_cancelButton(const XProgressDialog* self);` | 取自定义取消按钮 | `cancelButton` qprogressdialog.h:42 同段 | 借用禁释放 |
| `void XProgressDialog_setBar(XProgressDialog* self, XProgressBar* bar);` | 设进度条（仅存借用指针） | `setBar` qprogressdialog.h:43 | **借用禁释放**，不接管 |
| `void XProgressDialog_cancel(XProgressDialog* self);` | 请求取消（发 canceled、置 wasCanceled、无条件隐藏） | `cancel` qprogressdialog.h:66 | — |
| `bool XProgressDialog_wasCanceled(const XProgressDialog* self);` | 查是否已取消 | `wasCanceled` qprogressdialog.h:45 | — |
| `void XProgressDialog_setAutoReset(XProgressDialog* self, bool reset);` | 设达最大值自动复位 | `setAutoReset` qprogressdialog.h:57 | — |
| `bool XProgressDialog_autoReset(const XProgressDialog* self);` | 查自动复位；NULL 返回 true | `autoReset` qprogressdialog.h:58 | — |
| `void XProgressDialog_setAutoClose(XProgressDialog* self, bool close);` | 设复位时自动隐藏 | `setAutoClose` qprogressdialog.h:59 | — |
| `bool XProgressDialog_autoClose(const XProgressDialog* self);` | 查自动隐藏；NULL 返回 true | `autoClose` qprogressdialog.h:60 | — |
| `void XProgressDialog_setMinimumDuration(XProgressDialog* self, int ms);` | 设显示最小时长（默认 4000） | `setMinimumDuration` qprogressdialog.h:74 | — |
| `int XProgressDialog_minimumDuration(const XProgressDialog* self);` | 查显示最小时长 | `minimumDuration` qprogressdialog.h:55 | — |
| `void XProgressDialog_forceShow(XProgressDialog* self);` | 强制立即显示（已显示过则不再强显） | `forceShow` qprogressdialog.h:86 | — |
| `void XProgressDialog_open_2(XProgressDialog* self, XObject* receiver, XSlotFunc1 member);` | 窗口模态非阻塞显示 + finished 连接（关闭自动断开） | `open(QObject*,const char*)` qprogressdialog.h:63（经 `using QDialog::open` qprogressdialog.h:62 导出） | receiver 借用，连接自管 |
| `void* XProgressDialog_canceled_signal(XProgressDialog* self);` | canceled() 信号（cancel 时发射） | `canceled` qprogressdialog.h:77 | 信号标识，禁解引用 |

**所有权与命名契约**：`_const` 借用族=全部查询取 const self；`labelText` 是**堆拷贝**（XProgressDialog.c:611-619 经 `xprogressdialog_dupString`），按 §8.2.1:1811「不带 `_const` = 拷贝，调用方必须释放」的拆分依据自洽。`_2` 后缀=`open_2`，头文件明写「open 的重载形态，按数字后缀约定命名 _2，XMessageBox_open_2 同型」（XProgressDialog.h:327-330）——按 §8.2.1:1813 的 `_2`→`_utf8` 立项，本名不在其列（非字符串参数）。三态 `set*`：无（`setAutoReset`/`setAutoClose` 二态）。`setRange` 的双向交换与 `setValue` 的 autoReset 分支均在头文件写明（XProgressDialog.h:129, 164-166）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XProgressDialog)` + `XCLASS_DEFINE_EXTEND_END(XProgressDialog, XDialog)`（XProgressDialog.h:51-52）。虚表槽（XProgressDialog.c:431-442）：`EXClass_Deinit`→释放自有字段、`EXWidget_ResizeEvent`→重排、`EXObject_TimerEvent`→minimumDuration 超时强显 + 显示即停表、`EXWidget_ShowEvent`→按真实装饰态重算尺寸、`EXWidget_CloseEvent`→父类收口后断开 open_2 临时连接。无 `_base` 公开入口。

**注意事项与坑点**

1. **`XPROGRESSDIALOG_ON` 宏不存在**：XGui.md §14.135:3183-3186 明确记「`XPROGRESSDIALOG_ON` 宏不存在（XProgressDialog.h 守卫为 `XDIALOG_ON`）——特性守卫写错宏名该节被预处理**静默裁掉**且编译期零告警」。本头文件守卫确为 `XWIDGET_ON && XDIALOG_ON`（XProgressDialog.h:41），头文件 @note 亦写「模块总开关 XDIALOG_ON（XWIDGET_ON && XDIALOG_ON 有效）」（XProgressDialog.h:19）。
2. **头文件 @note 说「不创建真实子控件布局」，实现已排布**：XProgressDialog.h:20-21 写「本实现不创建真实的子控件布局；setBar 仅存储借用指针，渲染与原生进度面板为后续扩展」，但 `xprogressdialog_relayout` 实打实给标签 `setGeometry(16, 12+applied, lw, 20)`、给取消按钮 `(w-84, h-34-pending, 80, 26)`（XProgressDialog.c:133-158），`updateSize` 还按内容驱动尺寸（XProgressDialog.c:160-175）。仅「进度条面板」一项仍为后续扩展（`setBar` 确只存指针，XProgressDialog.c:698-699）。
3. **`cancel()` 无条件隐藏，不用 autoClose 判断**：实现注释「Qt 经 forceHide 保证取消时无条件隐藏（即使 autoClose=false）」（XProgressDialog.c:704-708），即 `cancel()` 后一定不可见，与 `reset()` 的 autoClose 条件不同（XProgressDialog.c:577-583）。头文件 @details 只写「复位进度（值到 minimum、autoClose 时隐藏）」（XProgressDialog.h:259-262），未点出这个差异。
4. **`open_2` 的断开时机**：头文件 @details 明写「cancel() 仅隐藏不发 finished、不断开」（XProgressDialog.h:333-336），实现由 closeEvent 在事件被接受后调 `xpd_openCleanup`（XProgressDialog.c:405-411）。重复 `open_2` 会先按旧记录断开（XProgressDialog.c:771-775），防叠连。
5. **进度框 `open` 不登记应用模态门**：XGui.md §14.133:3043-3046 记「XDialog_open 对 XProgressDialog vtable 向下识别例外不登记应用模态门（对标 Qt 6.8.3 qprogressdialog.cpp 全文无 setModal——非模态默认）」，`exec` 路径仍无条件模态。
6. **默认标题是库内自定**：XGui.md §14.133:3052 记「账本 #5 进度框窗口标题：库内默认「正在执行」（CSD 形态可辨识性，对标偏差登记）」；实现见 XProgressDialog.c:474-483，且注释自标「Qt 6.8.3 qprogressdialog.cpp 全文无 setWindowTitle」。
7. **`m_bar` 与 `m_label`/`m_cancelButton` 所有权相反**：结构体注释分别写「进度条（借用；setBar 简化存储）」（XProgressDialog.h:65）与「自定义标签控件（拥有）」「自定义取消按钮（拥有）」（XProgressDialog.h:66-67），混用会导致重复释放或泄漏。

**测试覆盖**：无 `Test/XGuiTest/XProgressDialogTest.c` 专属套件。间接覆盖（`rg -c "\bXProgressDialog\b" Test/`）：`Test/XGuiDemo/xgui_demo_page_dialogs.c`（8 处）、`xgui_demo_apitest_dialogs.c`（8 处）、`xgui_dialog_portrait_probe.c`（2 处）。仓库根回归 `test_dialog_task219b_contract` 内有专段（xgui_regression_test.c:39603-39660，10 条 `expect_true`：默认 0/100/0/autoReset/autoClose/4000ms/未取消、setRange、setValue、越界钳位等），另有 XGui.md §14.133:3072 记的 r2#4 六条「进度框 open 后 activeModalWidget==NULL」模态门锁。

---

## XPushButton

**定位**：普通按钮（在 XAbstractButton 之上补 autoDefault 三态/default/flat、菜单关联、sizeHint 与离屏绘制）。继承链 `XObject ← XWidget ← XAbstractButton ← XPushButton`。头文件 `Src/XGui/Widget/XPushButton.h`（324 行）。守卫 `XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON`（XPushButton.h:54）。

**生命周期**：`XPushButton_init` 先 `XAbstractButton_init`（文本/图标/状态位/定时器/调色板角色/强焦点全在基类），再置 `m_flat=false`、`m_defaultButton=false`、`m_autoDefault=Auto`、`m_menu=NULL`，尺寸策略 `Minimum/Fixed + PushButton`，末尾刷新 sizeHint（XPushButton.c:194-212）。`VXPushButton_deinit` 先断开菜单桥连接再转父类（XPushButton.c:285-298）；`m_menu` 借用**不删除**（XPushButton.h:91）。

**API 面**

枚举（XPushButton.h:70-75）：`XPushButtonAutoDefault`（Auto=0/Off=1/On=2，对标 Qt 6.8 `QPushButtonPrivate::AutoDefault`）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XPushButton_class_init(void);` | 建表（继承 XAbstractButton，零新增槽） | 同名惯用 | 共享表，禁释放 |
| `void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags);` | 初始化（autoDefault=Auto/default/flat 假） | `QPushButton(QWidget*)` qpushbutton.h:28 | — |
| `#define XPushButton_create(parent, flags) XPushButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `#define XPushButton_text(self) XAbstractButton_text(...)` | 查文本 | `text` qabstractbutton.h:45 | **借用禁释放** |
| `#define XPushButton_setText(self, text) XAbstractButton_setText(...)` | 设文本（相同则 no-op） | `setText` qabstractbutton.h:44 | 入参借用，内部复制 |
| `#define XPushButton_setText_2(self, utf8) XAbstractButton_setText_2(...)` | 设文本（UTF-8 便利版） | `setText` qabstractbutton.h:44 | 入参借用 |
| `#define XPushButton_icon(self) XAbstractButton_icon(...)` | 查图标（按值返回） | `icon` qabstractbutton.h:48 | 值返回 |
| `#define XPushButton_setIcon(self, icon) XAbstractButton_setIcon(...)` | 设图标（NULL/空图标清空） | `setIcon` qabstractbutton.h:47 | 值拷贝 |
| `#define XPushButton_iconSize(self) XAbstractButton_iconSize(...)` | 查图标渲染尺寸 | `iconSize` qabstractbutton.h:50 | 值返回 |
| `#define XPushButton_setIconSize(self, size) XAbstractButton_setIconSize(...)` | 设图标渲染尺寸（非法恢复默认） | `setIconSize` qabstractbutton.h:82 | — |
| `#define XPushButton_isCheckable(self) XAbstractButton_isCheckable(...)` | 查可选中（默认 false） | `isCheckable` qabstractbutton.h:58 | — |
| `#define XPushButton_setCheckable(self, checkable) XAbstractButton_setCheckable(...)` | 设可选中 | `setCheckable` qabstractbutton.h:57 | — |
| `#define XPushButton_isChecked(self) XAbstractButton_isChecked(...)` | 查选中（默认 false） | `isChecked` qabstractbutton.h:60 | — |
| `#define XPushButton_setChecked(self, checked) XAbstractButton_setChecked(...)` | 设选中 | `setChecked` qabstractbutton.h:86 | — |
| `#define XPushButton_toggle(self) XAbstractButton_toggle(...)` | 切换选中并发 toggled | `toggle` qabstractbutton.h:85 | — |
| `#define XPushButton_isDown(self) XAbstractButton_isDown(...)` | 查按下面板态 | `isDown` qabstractbutton.h:63 | — |
| `#define XPushButton_setDown(self, down) XAbstractButton_setDown(...)` | 设按下面板态 | `setDown` qabstractbutton.h:62 | — |
| `#define XPushButton_autoRepeat(self) XAbstractButton_autoRepeat(...)` | 查自动重复（默认 false） | `autoRepeat` qabstractbutton.h:66 | — |
| `#define XPushButton_setAutoRepeat(self, repeat) XAbstractButton_setAutoRepeat(...)` | 设自动重复 | `setAutoRepeat` qabstractbutton.h:65 | — |
| `#define XPushButton_autoRepeatDelay(self) XAbstractButton_autoRepeatDelay(...)` | 查重复延迟（默认 300） | `autoRepeatDelay` qabstractbutton.h:69 | — |
| `#define XPushButton_setAutoRepeatDelay(self, delay) XAbstractButton_setAutoRepeatDelay(...)` | 设重复延迟 | `setAutoRepeatDelay` qabstractbutton.h:68 | — |
| `#define XPushButton_autoRepeatInterval(self) XAbstractButton_autoRepeatInterval(...)` | 查重复间隔（默认 100） | `autoRepeatInterval` qabstractbutton.h:72 | — |
| `#define XPushButton_setAutoRepeatInterval(self, interval) XAbstractButton_setAutoRepeatInterval(...)` | 设重复间隔 | `setAutoRepeatInterval` qabstractbutton.h:71 | — |
| `#define XPushButton_autoExclusive(self) XAbstractButton_autoExclusive(...)` | 查自动互斥（默认 false） | `autoExclusive` qabstractbutton.h:75 | — |
| `#define XPushButton_setAutoExclusive(self, exclusive) XAbstractButton_setAutoExclusive(...)` | 设自动互斥 | `setAutoExclusive` qabstractbutton.h:74 | — |
| `#define XPushButton_click(self) XAbstractButton_click(...)` | 程序化点击（禁用时直接返回） | `click` qabstractbutton.h:84 | — |
| `#define XPushButton_animateClick(self) XAbstractButton_animateClick(...)` | 动画点击（100ms 后释放/点击） | `animateClick` qabstractbutton.h:83 | — |
| `#define XPushButton_pressed_signal(self) XAbstractButton_pressed_signal(...)` | pressed 信号 | `pressed` qabstractbutton.h:89 | 信号标识 |
| `#define XPushButton_released_signal(self) XAbstractButton_released_signal(...)` | released 信号 | `released` qabstractbutton.h:90 | 信号标识 |
| `#define XPushButton_clicked_signal(self, checked) XAbstractButton_clicked_signal(...)` | clicked(bool) 信号 | `clicked` qabstractbutton.h:91 | 信号标识 |
| `#define XPushButton_toggled_signal(self, checked) XAbstractButton_toggled_signal(...)` | toggled(bool) 信号 | `toggled` qabstractbutton.h:92 | 信号标识 |
| `bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos);` | 局部坐标命中（经基类保护虚表分派） | `hitButton` qpushbutton.h:62（protected 覆写） | — |
| `bool XPushButton_autoDefault(const XPushButton* self);` | 查自动默认是否生效（Auto 时按父对话框判定） | `autoDefault` qpushbutton.h:36 | — |
| `void XPushButton_setAutoDefault(XPushButton* self, bool enable);` | 设自动默认（true=On/false=Off） | `setAutoDefault` qpushbutton.h:37 | — |
| `bool XPushButton_isDefault(const XPushButton* self);` | 查是否默认按钮 | `isDefault` qpushbutton.h:38 | — |
| `void XPushButton_setDefault(XPushButton* self, bool enable);` | 设默认按钮（并清同对话框其它默认位） | `setDefault` qpushbutton.h:39 | — |
| `bool XPushButton_isFlat(const XPushButton* self);` | 查扁平（默认 false） | `isFlat` qpushbutton.h:47 | — |
| `void XPushButton_setFlat(XPushButton* self, bool flat);` | 设扁平 | `setFlat` qpushbutton.h:46 | — |
| `void XPushButton_setMenu(XPushButton* self, XMenu* menu);` | 关联弹出菜单（NULL 解除） | `setMenu` qpushbutton.h:42 | **菜单借用禁释放**，仅建/断连接 |
| `XMenu* XPushButton_menu(const XPushButton* self);` | 查关联菜单 | `menu` qpushbutton.h:43 | 借用禁释放 |
| `void XPushButton_showMenu(XPushButton* self);` | 显示关联菜单（按钮正下方弹出） | `showMenu` qpushbutton.h:51 | — |
| `XSize XPushButton_sizeHint(const XPushButton* self);` | 建议尺寸；NULL 返回 (-1,-1) | `sizeHint` qpushbutton.h:33 | 值返回 |
| `XSize XPushButton_minimumSizeHint(const XPushButton* self);` | 最小建议尺寸（同 sizeHint） | `minimumSizeHint` qpushbutton.h:34 | 值返回 |
| `void XPushButton_drawContents(XPushButton* self, XPainter* painter);` | 离屏绘制内容（背景/边框/图标/文本） | `paintEvent` qpushbutton.h:56 的内容 | painter 借用 |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`hitButton`/`autoDefault`/`isDefault`/`isFlat`/`menu`/`sizeHint`/`minimumSizeHint`）；`_2` 重载=`setText_2`（UTF-8 便利版），按 §8.2.1:1813 属 `_2`→`_utf8` 待改批次。无三态 `set*`（`setAutoDefault`/`setDefault`/`setFlat`/`setCheckable`/`setChecked` 均二态 bool；三态语义由枚举 `XPushButtonAutoDefault` 承载，但 setter 只接受 bool——头文件 @param 明写「true 设为 On，false 设为 Off」（XPushButton.h:238），**无法通过 API 回到 Auto**）。其余继承 API 以 28 条宏别名转发（XPushButton.h:134-218），宏名与基类同名不同前缀。

**可覆写点**：`XCLASS_DEFINE_BEGING(XPushButton)` + `XCLASS_DEFINE_EXTEND_END(XPushButton, XAbstractButton)`（XPushButton.h:64-65）。虚表槽（XPushButton.c:794-808）：`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`→`pushbutton_computeSizeHint`、`EXAbstractButton_ContentChanged`→刷新 sizeHint 存储位、`EXWidget_PaintEvent`→`VXPushButton_paintEvent`、`EXWidget_KeyPressEvent`→`VXPushButton_keyPressEvent`（Return/Enter 在默认或 autoDefault 生效时 click，其余交基类，XPushButton.c:740-762）、`EXClass_Copy`/`EXClass_Move`/`EXClass_Deinit`。`_base` 入口：`XAbstractButton_hitButton_base`（XPushButton.c:302-305）。

**注意事项与坑点**

1. **`setDefault` 的头注释已过时（实现已做互斥）**：XPushButton.h:243-248 写「当前实现仅置默认标志并重绘：不改 autoDefault 三态，也不做对话框内默认按钮唯一性收敛（待办）」，但实现 `XPushButton_setDefault` 在 `enable` 时沿父链找对话框并调 `pushbutton_clearSiblingDefaults` 清掉其它按钮的默认位（XPushButton.c:381-397，辅助函数 XPushButton.c:344-380），XGui.md §8.2:1785 亦记「默认按钮互斥」已修。
2. **头注释的 Qt 行号错位**：`XPushButton.h:257` 写「对标 QPushButton::setMenu，Qt 6.8 qpushbutton.h:43」，但 qpushbutton.h:42 才是 `setMenu`、43 是 `menu()`；`XPushButton.h:270` 给 `menu` 标「qpushbutton.h:44」，实际为 43。`showMenu` 标的 qpushbutton.h:51 正确。
3. **悬空注释两行**：XPushButton.h:128-129（「通过 XClass 虚表释放 XPushButton 资源（栈/外部存储对象使用）」/「删除堆上的 XPushButton 对象」）之后没有声明，紧接宏别名段（XPushButton.h:131）——同 api-surface.md 对 XWidget.h:736-737 登记的同型问题。
4. **`sizeHint` 的边距已改走样式度量，头注释仍写死数值**：头文件 @details 写「关联菜单追加 PM_MenuButtonIndicator=12；最后加 QCommonStyle CT_PushButton 的 buttonMargin(6)+defaultFrameWidth*2(4)」（XPushButton.h:288-290），实现注释则记「[sizeHint 收口 2026-10-07] 边距/指示器改走风格…删除硬编码 +12/+6+4（Fusion DefaultFrameWidth=1 时 Qt 语义应 +8，旧硬编码恒 +10 差 2px）」（XPushButton.c:159-161），实际经 `XStyle_pixelMetric`/`XStyle_sizeFromContents` 取值（XPushButton.c:167-173）。
5. **菜单是借用，按钮不拥有**：结构体注释「关联弹出菜单（借用指针；不拥有）」（XPushButton.h:91），deinit 只 `XObject_disconnect_1` 两条连接后把 `m_menu` 置 NULL，**不删除菜单**（XPushButton.c:285-298）。
6. **`showMenu` 非阻塞**：类注释明写「弹层经 XMenu_popup 非阻塞弹出（Qt showMenu 文档的阻塞语义由调用方事件循环承担）」（XPushButton.c:32-34）；弹层位置用自持父链累加逻辑偏移+顶层逻辑原点，规避旋转下 `mapToGlobal` 的设备全局错位（XPushButton.c:497-508）。
7. **`autoDefault` 的 Auto 态不可由 API 回到**：setter 只在 On/Off 之间切换（XPushButton.c:319-329），构造初值才是 Auto（XPushButton.c:205）。

**测试覆盖**：无 `Test/XGuiTest/XPushButtonTest.c` 专属套件。间接覆盖（`rg -c "\bXPushButton\b" Test/`）：`xgui_window_demo.c`（17 处）、`xgui_demo_page_modbus.c`（15）、`xgui_demo_page_effects.c`（15）、`xgui_demo_page_atv61.c`（12）、`xgui_demo_apitest_dialogs.c`（10）、`xgui_demo_page_window.c`（9），另有 `Test/XGuiTest/XGuiRemoteTest.c`（2）、`xscroll_clip_probe.c`（3）、`hsf_vfd_probe.c`（2）、`XGuiMirrorFidelityTest.c`（1）。仓库根回归中 `XPushButton_` 前缀 API 调用 169 处，另有虚表冒烟一条（xgui_regression_test.c:30944）。

---

## XRadioButton

**定位**：单选按钮（圆形 indicator + 文本/图标，同父控件下经基类 autoExclusive 自动互斥）。继承链 `XObject ← XWidget ← XAbstractButton ← XRadioButton`。头文件 `Src/XGui/Widget/XRadioButton.h`（211 行）。守卫 `XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON`（XRadioButton.h:41）。

**生命周期**：`XRadioButton_init` 先 `XAbstractButton_init` 再挂本类虚表，随后 `setCheckable(true)` + `setAutoExclusive(true)` + 前景角色 WindowText（对标 `QRadioButtonPrivate::init`，qradiobutton.cpp:35-38），末尾刷新 sizeHint（XRadioButton.c:432-445）。结构体**不新增字段**（仅 `XAbstractButton m_base`，XRadioButton.h:61-64），析构遵循基类约定。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XRadioButton_class_init(void);` | 建表（继承 XAbstractButton，零新增槽） | 同名惯用 | 共享表，禁释放 |
| `void XRadioButton_init(XRadioButton* self, XWidget* parent, XWidgetFlags flags);` | 初始化（checkable/autoExclusive 默认开） | `QRadioButton(QWidget*)` qradiobutton.h:23 | — |
| `#define XRadioButton_create(parent, flags) XRadioButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | `QRadioButton(const QString&,QWidget*)` qradiobutton.h:24 | 调用方拥有，须 `XClassDelete` |
| `XRadioButton* XRadioButton_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `#define XRadioButton_text(self) XAbstractButton_text(...)` | 查文本 | `text` qabstractbutton.h:45 | **借用禁释放** |
| `#define XRadioButton_setText(self, text) XAbstractButton_setText(...)` | 设文本 | `setText` qabstractbutton.h:44 | 入参借用 |
| `#define XRadioButton_setText_2(self, utf8) XAbstractButton_setText_2(...)` | 设文本（UTF-8 便利版） | `setText` qabstractbutton.h:44 | 入参借用 |
| `#define XRadioButton_icon(self) XAbstractButton_icon(...)` | 查图标 | `icon` qabstractbutton.h:48 | 值返回 |
| `#define XRadioButton_setIcon(self, icon) XAbstractButton_setIcon(...)` | 设图标 | `setIcon` qabstractbutton.h:47 | 值拷贝 |
| `#define XRadioButton_iconSize(self) XAbstractButton_iconSize(...)` | 查图标渲染尺寸 | `iconSize` qabstractbutton.h:50 | 值返回 |
| `#define XRadioButton_setIconSize(self, size) XAbstractButton_setIconSize(...)` | 设图标渲染尺寸 | `setIconSize` qabstractbutton.h:82 | — |
| `#define XRadioButton_isCheckable(self) XAbstractButton_isCheckable(...)` | 查可选中（**默认 true**） | `isCheckable` qabstractbutton.h:58 | — |
| `#define XRadioButton_setCheckable(self, checkable) XAbstractButton_setCheckable(...)` | 设可选中 | `setCheckable` qabstractbutton.h:57 | — |
| `#define XRadioButton_isChecked(self) XAbstractButton_isChecked(...)` | 查选中 | `isChecked` qabstractbutton.h:60 | — |
| `#define XRadioButton_setChecked(self, checked) XAbstractButton_setChecked(...)` | 设选中 | `setChecked` qabstractbutton.h:86 | — |
| `#define XRadioButton_toggle(self) XAbstractButton_toggle(...)` | 切换选中 | `toggle` qabstractbutton.h:85 | — |
| `#define XRadioButton_isDown(self) XAbstractButton_isDown(...)` | 查按下面板态 | `isDown` qabstractbutton.h:63 | — |
| `#define XRadioButton_setDown(self, down) XAbstractButton_setDown(...)` | 设按下面板态 | `setDown` qabstractbutton.h:62 | — |
| `#define XRadioButton_autoRepeat(self) XAbstractButton_autoRepeat(...)` | 查自动重复 | `autoRepeat` qabstractbutton.h:66 | — |
| `#define XRadioButton_setAutoRepeat(self, repeat) XAbstractButton_setAutoRepeat(...)` | 设自动重复 | `setAutoRepeat` qabstractbutton.h:65 | — |
| `#define XRadioButton_autoRepeatDelay(self) XAbstractButton_autoRepeatDelay(...)` | 查重复延迟 | `autoRepeatDelay` qabstractbutton.h:69 | — |
| `#define XRadioButton_setAutoRepeatDelay(self, delay) XAbstractButton_setAutoRepeatDelay(...)` | 设重复延迟 | `setAutoRepeatDelay` qabstractbutton.h:68 | — |
| `#define XRadioButton_autoRepeatInterval(self) XAbstractButton_autoRepeatInterval(...)` | 查重复间隔 | `autoRepeatInterval` qabstractbutton.h:72 | — |
| `#define XRadioButton_setAutoRepeatInterval(self, interval) XAbstractButton_setAutoRepeatInterval(...)` | 设重复间隔 | `setAutoRepeatInterval` qabstractbutton.h:71 | — |
| `#define XRadioButton_autoExclusive(self) XAbstractButton_autoExclusive(...)` | 查自动互斥（**默认 true**） | `autoExclusive` qabstractbutton.h:75 | — |
| `#define XRadioButton_setAutoExclusive(self, exclusive) XAbstractButton_setAutoExclusive(...)` | 设自动互斥 | `setAutoExclusive` qabstractbutton.h:74 | — |
| `#define XRadioButton_click(self) XAbstractButton_click(...)` | 程序化点击 | `click` qabstractbutton.h:84 | — |
| `#define XRadioButton_animateClick(self) XAbstractButton_animateClick(...)` | 动画点击 | `animateClick` qabstractbutton.h:83 | — |
| `#define XRadioButton_pressed_signal(self) XAbstractButton_pressed_signal(...)` | pressed 信号 | `pressed` qabstractbutton.h:89 | 信号标识 |
| `#define XRadioButton_released_signal(self) XAbstractButton_released_signal(...)` | released 信号 | `released` qabstractbutton.h:90 | 信号标识 |
| `#define XRadioButton_clicked_signal(self, checked) XAbstractButton_clicked_signal(...)` | clicked(bool) 信号 | `clicked` qabstractbutton.h:91 | 信号标识 |
| `#define XRadioButton_toggled_signal(self, checked) XAbstractButton_toggled_signal(...)` | toggled(bool) 信号 | `toggled` qabstractbutton.h:92 | 信号标识 |
| `bool XRadioButton_hitButton(const XRadioButton* self, const XPoint* pos);` | 局部坐标命中（整控件矩形） | `hitButton` qradiobutton.h:32（protected 覆写） | — |
| `XSize XRadioButton_sizeHint(const XRadioButton* self);` | 建议尺寸；NULL 返回 (-1,-1) | `sizeHint` qradiobutton.h:27 | 值返回 |
| `XSize XRadioButton_minimumSizeHint(const XRadioButton* self);` | 最小建议尺寸（同 sizeHint） | `minimumSizeHint` qradiobutton.h:28 | 值返回 |
| `void XRadioButton_drawContents(XRadioButton* self, XPainter* painter);` | 离屏绘制（indicator+图标+文本） | `paintEvent` qradiobutton.h:33 的内容 | painter 借用 |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`hitButton`/`sizeHint`/`minimumSizeHint`）；`_2` 重载=`setText_2`（UTF-8 便利版，属 §8.2.1:1813 的 `_2`→`_utf8` 待改批次）；无三态 `set*`（全部二态 bool）。全部基类 API 以 28 条宏别名转发（XRadioButton.h:101-163）；互斥语义**不在本类实现**，由基类 `autoExclusive` + 派生登记表提供（XRadioButton.h:12-15）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XRadioButton)` + `XCLASS_DEFINE_EXTEND_END(XRadioButton, XAbstractButton)`（XRadioButton.h:51-52）。虚表槽（XRadioButton.c:416-429）：`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`→实时算、`EXAbstractButton_HitButton`→`VXRadioButton_hitButton`、`EXAbstractButton_ContentChanged`→刷新 sizeHint、`EXWidget_PaintEvent`→`VXRadioButton_paintEvent`；末尾 `XAbstractButton_registerClass(XVTABLE_DEFAULT)` 登记派生虚表，供自动互斥/实例识别（XRadioButton.c:427-428）。`_base` 入口：`XAbstractButton_hitButton_base`（XRadioButton.c:152-155）。

**注意事项与坑点**

1. **`hitButton` 头注释与实现矛盾（命中范围）**：头文件 @details 写「Qt 的命中基准是 SE_RadioButtonClickRect（indicator 区域）；本实现…XRadioButton 重载为 indicator 外接矩形包含（无主题时近似）」（XRadioButton.h:169-171），但实现的虚槽用 `XWidget_rect(self)` 做整控件矩形包含（XRadioButton.c:343-353），槽注释也写「indicator + 标签整体命中（对标 SE_RadioButtonClickRect = SE_RadioButtonFocusRect | SE_RadioButtonIndicator）」（XRadioButton.c:334-337）。以实现为准：**点整控件任意处都命中**。
2. **头注释称「悬停效果未实现」，实现已消费 MouseOver**：XRadioButton.h:21 写「快捷键、样式表 bevel、悬停效果未实现」，而样式分支在 `XWidget_underMouse(self) && isEnabled` 时置 `XStyleState_MouseOver`（XRadioButton.c:227-229）。
3. **头注释称 QButtonGroup 登记仍是裁剪项，库内已有 XButtonGroup**：XRadioButton.h:23-24 写「显式 QButtonGroup 登记仍是裁剪项，跨父控件互斥需借助自动互斥组或后续 QButtonGroup」，而库内已存在 `XButtonGroup_addButton`（`Src/XGui/Widget/XButtonGroup.h:81`，注释「加入按钮；id < 0 时自动分配（对标 addButton）」）。头注释未随实现同步。
4. **跨父控件不互斥是设计边界**：互斥只经基类 autoExclusive 在同一父控件下生效（XRadioButton.h:12-15、21-24），跨父控件需另建分组。
5. **indicator 尺寸与间距是硬编码常量**：`RADIOBUTTON_INDICATOR_SIZE 13` + `RADIOBUTTON_LABEL_SPACING 6`（XRadioButton.c:48, 50），sizeHint 与绘制共用（XRadioButton.c:107-108, 298），头文件 @details 亦写「indicator 13x13 加 6 像素标签间距」（XRadioButton.h:182-183）。
6. **选中圆点用方块近似**：`XPAINTER_SHAPE_ON=0` 时 indicator 以矩形描边退化，选中点亦为 5x5 实心方块（XRadioButton.c:273-281 注释「中心方块近似（点阵后端无实心椭圆原语）」）。
7. **悬空注释两行**：XRadioButton.h:95-96（「通过 XClass 虚表释放 XRadioButton 资源…」/「删除堆上的 XRadioButton 对象」）之后无声明，紧接宏别名段（XRadioButton.h:98）。

**测试覆盖**：无 `Test/XGuiTest/XRadioButtonTest.c` 专属套件。间接覆盖（`rg -c "\bXRadioButton\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_buttons.c`（5 处）、`xgui_window_demo.c`（3）、`xgui_demo_theme.h`（2）、`xgui_demo_page_window.c`（2）。仓库根回归另有虚表冒烟一条（xgui_regression_test.c:30945）。

---

## XRubberBand

**定位**：橡皮筋选择框（Line/Rectangle 两形状的边框绘制）。继承链 `XObject ← XWidget ← XRubberBand`。头文件 `Src/XGui/Widget/XRubberBand.h`（63 行）。守卫 `XWIDGET_ON && XRUBBERBAND_ON`（XRubberBand.h:25；开关定义 XGuiConfig.h:581-583，嵌入式裁剪见 XGuiConfig.h:906-907）。

**生命周期**：`XRubberBand_init` 全零后 `XWidget_init(&self->m_base, parent, 0)`（flags 恒 0，无 flags 参数）、挂本类虚表、`m_shape = shape`（XRubberBand.c:104-114）；堆对象 `_create/_create_ex` 后须 `XClassDelete`。无自有堆资源，析构遵循基类约定。

**API 面**

枚举（XRubberBand.h:28-32）：`XRubberBandShape`（Line=0/Rectangle=1，数值对齐 `QRubberBand::Shape` qrubberband.h:22）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XRubberBand_class_init(void);` | 建表（继承 XWidget，仅重载 PaintEvent） | 同名惯用 | 共享表，禁释放 |
| `void XRubberBand_init(XRubberBand* self, XRubberBandShape shape, XWidget* parent);` | 初始化（形状构造期定死） | `QRubberBand(Shape,QWidget*)` qrubberband.h:23 | — |
| `#define XRubberBand_create(shape, parent) XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (shape), (parent))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XRubberBand* XRubberBand_create_ex(XMemoryType memory, XRubberBandShape shape, XWidget* parent);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XRubberBandShape XRubberBand_shape(const XRubberBand* self);` | 查形状；NULL 返回 Line | `shape` qrubberband.h:26 | 值返回 |

几何 API 不自带头声明——头文件 @details 明写「setGeometry/move/resize 沿用 XWidget 基类」（XRubberBand.h:5-6），调用方经 `XWidget_setGeometry`/`XWidget_move`/`XWidget_resize` 作用于 `(XWidget*)rb`。

**所有权与命名契约**：`_const` 借用族=查询取 const self（`shape`）；无 `_utf8`/`_2` 重载；无三态 `set*`（本类无 setter，形状由构造决定）。遵循 XWidget 基类契约，其余 API 一律走基类。

**可覆写点**：`XCLASS_DEFINE_BEGING(XRubberBand)` + `XCLASS_DEFINE_EXTEND_END(XRubberBand, XWidget)`（XRubberBand.h:34-35）。虚表槽仅一个：`EXWidget_PaintEvent`→`VXRubberBand_paintEvent`（XRubberBand.c:96-102）——Rectangle 且默认样式可用时走 `XStyleCE_RubberBand`（半透明填充+边框，XRubberBand.c:57-74），否则手绘四缘 1px 线（Rectangle，XRubberBand.c:75-87）或水平中线（Line，XRubberBand.c:88-92）；描边色取调色板 Highlight，无调色板时回退 `0xFF3080C0`（XRubberBand.c:47-56）。无 `_base` 公开入口。

**注意事项与坑点**

1. **Line 形状只画水平中线，头注释却写「水平/垂直」**：头文件枚举注释「线形（水平/垂直分隔参考线）」（XRubberBand.h:30），实现 Line 分支恒 `XRect_init(&hline, 0, h / 2, w, 1)` 画一条水平线（XRubberBand.c:88-92），无垂直分支；Qt 文档亦为「can represent a vertical or horizontal line」（qrubberband.cpp:122-124）。
2. **头注释「绘制半透明填充+边框」只在样式可用时成立**：XRubberBand.h:5-7 写「绘制半透明填充+边框（第一版绘制为边框线）」；实现的三条路径分别是样式接管（需 `XSTYLE_ON && XStyle_defaultStyle() != NULL && shape == Rectangle`，XRubberBand.c:57-74）、Rectangle 手绘四边线、Line 单线——非样式路径下**没有填充**。
3. **未设 `WA_TransparentForMouseEvents`**：Qt 构造器即置 `setAttribute(Qt::WA_TransparentForMouseEvents)` 与 `WA_NoSystemBackground`（qrubberband.cpp:101-103），`XRubberBand.c` 全文无 `setAttribute`/`TransparentForMouse` 命中（`grep -n "setAttribute\|TransparentForMouse" XRubberBand.c` 无输出），故 X 侧橡皮筋会拦截其覆盖区域的鼠标事件。
4. **无 `changeEvent`/`showEvent`/`resizeEvent`/`moveEvent`/`initStyleOption` 覆写**：头文件宣称「对标 Qt 6.8 QRubberBand 全部公共 API」（XRubberBand.h:3-4），但 Qt 的 QRubberBand 另有这 5 个 protected 钩子（qrubberband.h:40-46，其中 `initStyleOption` 在 qrubberband.cpp:39-50 填 shape/opaque），X 侧只覆写了 PaintEvent（XRubberBand.c:100）。
5. **形状构造期定死后无 setter**：`m_shape` 为 1 位位域（XRubberBand.h:41），仅在 `XRubberBand_init` 里赋值一次（XRubberBand.c:113），`XWidget_move`/`XWidget_resize` 不会改动形状；这与 Qt「The shape can only be set upon construction」（qrubberband.cpp:133-136）一致。

**测试覆盖**：无 `Test/XGuiTest/XRubberBandTest.c` 专属套件。间接覆盖（`rg -c "\bXRubberBand\b" Test/`）：`Test/XGuiDemo/xgui_demo_page_advanced.c`（9 处）、`xgui_demo_apitest_menus.c`（6 处）、`Test/XGuiTest/XGuiDockDragTest.c`（1 处）。仓库根回归有短段（xgui_regression_test.c:32097-32106，3 条 `sml_expect`：创建、形状为 Rectangle、`setGeometry` 后 60x40 生效）。

---

## XScrollArea

**定位**：滚动区域容器（内容控件接管/取回、widgetResizable、对齐、ensureVisible/ensureWidgetVisible）。继承链 `XObject ← XWidget ← XFrame ← XAbstractScrollArea ← XScrollArea`。头文件 `Src/XGui/Widget/XScrollArea.h`（137 行）。守卫 `XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON`（XScrollArea.h:36；开关定义 XGuiConfig.h:669-671，嵌入式裁剪见 XGuiConfig.h:946-947）。

**生命周期**：`XScrollArea_init` 全零后 `XAbstractScrollArea_init`，置 `m_resizable=false`、`m_alignment=0`（XScrollArea.c:405-416）；`VXScrollArea_deinit` **先显式摘掉内容件上的事件过滤器**再转父类（XScrollArea.c:264-275），因为内容件可能先/后于本控件销毁且框架无弱引用。栈/堆释放纪律同 XWidget；`m_widget` 为借用字段（XScrollArea.h:47-48，实际所有权随父子关系转移，见坑点 2）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XScrollArea_class_init(void);` | 建表（继承 XAbstractScrollArea） | 同名惯用 | 共享表，禁释放 |
| `void XScrollArea_init(XScrollArea* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认不 resizable、对齐 0） | `QScrollArea(QWidget*)` qscrollarea.h:23 | — |
| `#define XScrollArea_create(parent, flags) XScrollArea_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XScrollArea* XScrollArea_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `void XScrollArea_setWidget(XScrollArea* self, XWidget* widget);` | 设内容（接管所有权、reparent 到视口、滚动归零） | `setWidget` qscrollarea.h:27 | **所有权转移给滚动区**；旧内容被 `XClassDelete` |
| `XWidget* XScrollArea_takeWidget(XScrollArea* self);` | 取回内容（parent 置 NULL） | `takeWidget` qscrollarea.h:28 | **所有权交还调用方** |
| `XWidget* XScrollArea_widget(const XScrollArea* self);` | 查内容控件 | `widget` qscrollarea.h:26 | 借用禁释放 |
| `bool XScrollArea_widgetResizable(const XScrollArea* self);` | 查内容随视口缩放（默认 false） | `widgetResizable` qscrollarea.h:30 | — |
| `void XScrollArea_setWidgetResizable(XScrollArea* self, bool resizable);` | 设内容随视口缩放并立即重排 | `setWidgetResizable` qscrollarea.h:31 | — |
| `void XScrollArea_updateWidgetGeometry(XScrollArea* self);` | 按当前视口/内容提示重排版内容与滚动范围（幂等） | `QScrollAreaPrivate::updateWidgetPosition`（Qt 私有，无公开对标） | — |
| `int XScrollArea_alignment(const XScrollArea* self);` | 查内容对齐（XAlignment 位掩码） | `alignment` qscrollarea.h:37 | — |
| `void XScrollArea_setAlignment(XScrollArea* self, int alignment);` | 设内容对齐并立即重排 | `setAlignment` qscrollarea.h:38 | — |
| `void XScrollArea_ensureVisible(XScrollArea* self, int x, int y, int xmargin, int ymargin);` | 最小滚动使内容点 (x,y) 连同边距可见 | `ensureVisible` qscrollarea.h:40 | — |
| `void XScrollArea_ensureWidgetVisible(XScrollArea* self, XWidget* childWidget, int xmargin, int ymargin);` | 最小滚动使子控件可见（跨级映射坐标） | `ensureWidgetVisible` qscrollarea.h:41 | childWidget 借用 |
| `XSize XScrollArea_sizeHint(const XScrollArea* self);` | 尺寸提示（基于视口提示） | `sizeHint` qscrollarea.h:33 | 值返回 |

**所有权与命名契约**：`_const` 借用族=查询取 const self（`widget`/`widgetResizable`/`alignment`/`sizeHint`）；无 `_utf8`/`_2` 重载；无三态 `set*`（`setWidgetResizable` 二态 bool，`setAlignment` 单值位掩码）。`m_alignment` 为 int 位掩码原样往返（XScrollArea.h:49）。`m_inGeometryUpdate` 是内部重入挡位，非公开 API（XScrollArea.h:52-55）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XScrollArea)` + `XCLASS_DEFINE_EXTEND_END(XScrollArea, XAbstractScrollArea)`（XScrollArea.h:38-39）。虚表槽（XScrollArea.c:383-403）：`EXWidget_SizeHint`→`XScrollArea_sizeHint`、`EXWidget_MinimumSizeHint`→`XWidget_minimumSizeHint_base`、`EXAbstractScrollArea_ScrollContentsBy`→按滚动条值绝对值定位内容、`EXWidget_ResizeEvent`→父类后重排、`EXWidget_ShowEvent`→按最终几何再断言一次、`EXWidget_WheelEvent`→接管滚轮（主导轴分派 + 20px×3 行/格）、`EXObject_EventFilter`→内容件 Resize 触发重排（恒返回 false）、`EXObject_Event`→LayoutRequest/StyleChange 触发重排后转父类、`EXClass_Deinit`→摘过滤器。`_base` 入口：`XWidget_minimumSizeHint_base`（XScrollArea.c:380）、`XLayoutItem_sizeHint_base`（定义 XLayoutItem.c:745；XScrollArea.c:112 的注释即指此读缓存点）。

**注意事项与坑点**

1. **对齐能力远超头注释所写**：头文件 @details 写「alignment（XAlignment 位掩码，存储；第一版排布仅支持左上/居中水平）」（XScrollArea.h:10-11），实现 `xsa_contentOrigin` 已完整支持 Right/HCenter/Bottom/VCenter 六个方向（XScrollArea.c:48-59），注释与实际能力不符。
2. **`m_widget` 标「借用」但实际会被销毁**：结构体注释写「内容控件（借用；所有权随接管的父子关系，takeWidget 可取回）」（XScrollArea.h:47-48），而 `setWidget` 装入新件前会对旧件执行 `XObject_removeEventFilter` + `XClassDelete`（XScrollArea.c:439-449，注释对标 qscrollarea.cpp:231-252 首行 `delete d->widget`）。同头文件 70-74 行自己也写「所有权移交…已有内容控件在装入新件前被销毁」——两处措辞互相矛盾，**以「接管即拥有」为准**。
3. **重排热点曾引发活锁，修法必须保留**：实现注释记录了板端实测的自我维持活锁——用 `XLayout_invalidate` 会「该事件回到滚动区 ⇒ 再次几何更新 ⇒ 再次失效 ⇒ 再次投递」，实测「act=3535 次/秒、inval=49472 次/秒、CPU 恒 99%」，因此改调 `XLayout_invalidateCacheOnly`（XScrollArea.c:103-115；`XLayout_Internal.h`）。
4. **内容件的事件过滤器必须在析构前显式摘除**：Qt 靠 `QPointer<widget>` 自动置空，本框架无弱引用，故 deinit 与 setWidget/takeWidget 三处都显式 `XObject_removeEventFilter`（XScrollArea.c:267-272, 445-448, 474-476）。
5. **`updateWidgetGeometry` 被公开是刻意的**：头文件注释「对标 QScrollAreaPrivate::updateWidgetPosition，Qt 为私有而本框架 C API 无 friend 语义故公开」（XScrollArea.h:99-104）——这是有意的 API 面扩大，不是遗漏。
6. **widgetResizable 的宽不设首选下界**：实现注释记「首选宽**不**阻止收窄：Qt 的 widgetResizable 语义是「内容随视口宽收缩，只受最小宽约束」…此前把 sizeHint 也当硬下限 → 内容恒保持首选宽（如窄窗 400 时网络页内容仍 564）→ 视口裁切、控件右侧被切（无头截图实测）」（XScrollArea.c:120-127）；高度侧仍取 `max(minHint.height, hint.height)`（XScrollArea.c:128-129）。
7. **内容 Resize 与提示变化是两条互补通知路径**：Resize 环覆盖「内容几何被直接 resize」，LayoutRequest/StyleChange 环覆盖「提示变化而内容几何未动」，实现注释自称「二者合并即 Qt 的两条通知路径全集」（XScrollArea.c:242-249）。
8. **历史修复登记**：XGui.md §10.5:2322 的夜间全量修复清单包含「XScrollArea ShowEvent+WheelEvent」，对应当前 `VXScrollArea_showEvent`（XScrollArea.c:297-303）与 `VXScrollArea_wheelEvent`（XScrollArea.c:321-367）两个槽。

**测试覆盖**：无 `Test/XGuiTest/XScrollAreaTest.c` 专属套件。间接覆盖（`rg -c "\bXScrollArea\b" Test/`）：`Test/XGuiDemo/xgui_demo_apitest_containers.c`（25 处）、`xgui_window_demo.c`（18）、`xgui_demo_page_dialogs.c`（14）、`xgui_demo_page_network.c`（12）、`xgui_demo_page_modbus.c`（11）、`xgui_demo_page_timeddns.c`（8），另有 `Test/XGuiTest/XGuiRemoteInputViewTest.c`（3）、`xscroll_clip_probe.c`（2）、`XScrollTouchProbe.c`（3）、`XTouchMultiPointTest.c`（3）。仓库根回归有专函数 `test_scrollarea_contract`（xgui_regression_test.c:31833-31970，17 条 `sa_expect`，含创建/视口存在/初始无内容/默认不 resizable/setWidget/hint-reflow 双向范围回收），另有虚表冒烟一条（xgui_regression_test.c:30936）。
# 控件文档片段 · 批次 07

本批覆盖 `Src/XGui/Widget/` 下 9 个控件/对象类。签名逐字抄自头文件（含宏原样），行号可核对。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/`；`qshortcut.h` 在 `widgets/kernel/` 只是转发壳（7 行，`#include <QtGui/QShortcut>`），真类在 `gui/kernel/qshortcut.h`，已按真实路径引用。
本批全部行号核对命令：`wc -l`（头文件行数）、`rg -n` / `awk 'NR>=a && NR<=b'`（逐行取文）、`sed -n`（Qt 侧取文）。

---

## XScrollBar

**定位**：滚动条控件（方向 + 页面步进 + 右键标准菜单 + 可选两端步进按钮）。继承链 `XObject ← XWidget ← XAbstractSlider ← XScrollBar`（XWidget.h:517、XAbstractSlider.h:98、XScrollBar.h:39-40）。头文件 `Src/XGui/Widget/XScrollBar.h`（159 行）。守卫 `XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON`（XScrollBar.h:37；开关 XGuiConfig.h:560-561，`XGUI_ON=0` 或 `!XWIDGET_ON||!XGUIAPPLICATION_ON||!XWINDOW_ON` 时被 `#undef` 置 0，XGuiConfig.h:888-889）。

**生命周期**：`XScrollBar_init` 直接转调 `XScrollBar_init_2(..., Vertical, ...)`；`init_2` 先 `XMemset` 再 `XAbstractSlider_init`，随后 `setOrientation`、`setInvertedControls(true)`（对标 `qscrollbar.cpp:348`）、sizeHint/minimumSizeHint 均 15x15、置 `XWidgetAttribute_Hover`（XScrollBar.c:493-529）。无自有堆成员故无 Deinit 槽；栈对象配 `XClassDeinit`、堆对象配 `XClassDelete`。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XScrollBar_class_init(void);` | 建/取类共享虚表 | 同名类初始化惯用 | 共享表，禁释放 |
| `void XScrollBar_init(XScrollBar* self, XWidget* parent, XWidgetFlags flags);` | 初始化（内部按 Vertical） | `QScrollBar(QWidget*)` qscrollbar.h:23（Qt 该构造亦委托 Vertical，qscrollbar.cpp:300-302） | — |
| `void XScrollBar_init_2(XScrollBar* self, int orientation, XWidget* parent, XWidgetFlags flags);` | 按方向初始化（int 取 `XAbstractSliderOrientation` 1/2） | `QScrollBar(Qt::Orientation, QWidget*)` qscrollbar.h:24 | — |
| `#define XScrollBar_create(parent, flags) XScrollBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `XScrollBar* XScrollBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | — | 调用方拥有，须 `XClassDelete` |
| `#define XScrollBar_create_2(orientation, parent, flags) XScrollBar_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))` | 方向重载的默认内存类型创建 | — | 调用方拥有，须 `XClassDelete` |
| `XScrollBar* XScrollBar_create_ex_2(XMemoryType memory, int orientation, XWidget* parent, XWidgetFlags flags);` | 指定内存类型 + 方向创建 | `QScrollBar(Qt::Orientation, QWidget*)` qscrollbar.h:24 | 调用方拥有，须 `XClassDelete` |
| `XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self);` | 构造滚动条标准右键菜单（7 条目 + 3 分隔符，文本随方向切换） | Qt 无 `QScrollBar::createStandardContextMenu`（该方法在 qtextedit.h:147）；仅 `contextMenuEvent` qscrollbar.h:41 | **返回对象所有权移交调用方**（XScrollBar.h:95-97）；自用路径另置 `DeleteOnClose`（XScrollBar.c:469-470） |
| `void XScrollBar_setShowButtons(XScrollBar* self, bool show);` | 是否绘制/命中两端步进按钮（相同值早退，变化后标脏） | X 扩展（Qt 由 `SH_ScrollBar_Transient` 样式语义决定，qscrollbar.cpp:353） | — |
| `bool XScrollBar_showButtons(const XScrollBar* self);` | 查询按钮显隐；`self==NULL` 返回 false | X 扩展 | — |
| 父类转发宏 19 条（XScrollBar.h:128-146） | `XScrollBar_{minimum,maximum,setMinimum,setMaximum,setRange,value,setValue,singleStep,setSingleStep,pageStep,setPageStep,orientation,setOrientation,sizeHint,isSliderDown,setSliderDown,valueChanged_signal,sliderMoved_signal,actionTriggered_signal}`，逐条 `(const X)XAbstractSlider*` 强转后转调 | `minimum` qabstractslider.h:40、`setValue` :82、`setRange` :84、`valueChanged` :87、`sliderMoved` :90、`actionTriggered` :95、`sizeHint` qscrollbar.h:27 | 全部 `—`；`sizeHint` 值返回 |

**所有权与命名契约**：查询族一律 `const XScrollBar*`（`showButtons`）；唯一转移所有权的是 `createStandardContextMenu` 的返回值。无 `_utf8`、无 `_2` 语义重载（`create_2`/`init_2` 是「同功能不同参数」编号，符合 `_2` 契约）。无三态 `set*`（`setShowButtons`/`setSliderDown` 为二态 bool）。父类 19 条转发宏未覆盖 `setTracking`/`hasTracking`/`sliderPosition`/`setSliderPosition`/`invertedAppearance`/`invertedControls`/`triggerAction`/`setRepeatAction`/`repeatAction`/`rangeChanged_signal`/`stepBy_base`/`sliderChange_base`/`stepEnabled_base`（`rg -c "XScrollBar_<name>"` 对 13 个名均返回 0）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XScrollBar)` + `XCLASS_DEFINE_EXTEND_END(XScrollBar, XAbstractSlider)`（XScrollBar.h:39-40）——**不新增槽位**。类初始化重载 5 个 `EXWidget` 事件槽：`PaintEvent`→`VXScrollBar_paintEvent`（样式优先走 `XStyleCC_ScrollBar`，否则自绘槽体/滑块）、`MousePressEvent`→按钮命中/滑块拖动/轨道翻页分支、`MouseMoveEvent`→拖动映射 `setValue`（XScrollBar.c:333-335）、`MouseReleaseEvent`→收闸 `setRepeatAction(NoAction)`（XScrollBar.c:357-359）、`ContextMenuEvent`→popup 标准菜单（XScrollBar.c:481-489）。无 `_base` 入口；映射/绘制内部函数均为 `static`（`xsb_*`、`VXScrollBar_*`）。

**注意事项与坑点**

1. **头注释的 Qt 行号与语义双错**：XScrollBar.h:108-110 称 `QScrollBarPrivate::init` 取 `SH_ScrollBar_Transient` 的**取反**并引 `qscrollbar.cpp:345`。实读 Qt：`init()` 起于 qscrollbar.cpp:345，hint 读取在 qscrollbar.cpp:353，且**无取反**——`transient = q->style()->styleHint(QStyle::SH_ScrollBar_Transient, &opt, q);`；取反只出现在用法处 `!d->transient`（qscrollbar.cpp:199、:294）。
2. **`createStandardContextMenu` 不是 Qt 同名 API**：头文件 XScrollBar.h:99 写「对标 QScrollBar::createStandardContextMenu」，但 `qscrollbar.h` 全文无该方法（只有 :41 `contextMenuEvent`）；`createStandardContextMenu` 属 `QTextEdit`（qtextedit.h:147-148）。应为「X 扩展 + 按 `contextMenuEvent` 条目对齐」。
3. **sizeHint 是固定正方形，Qt 是方向相关的样式度量**：X 侧 `hint.width=15; hint.height=15` 同时写入 sizeHint 与 minimumSizeHint（XScrollBar.c:518-525）；Qt 为 `PM_ScrollBarExtent*2 + PM_ScrollBarSliderMin` 且横竖互换（qscrollbar.cpp:411-425），**不可能是正方形**。
4. **`m_activeIsAdd` 只写不读**：写入点 XScrollBar.c:274（置 0）、:287（置 1），全文件无读取处（读的只有 `m_activeSub`：XScrollBar.c:188、:343）；按钮按下态在 `XStyleOption.m_scrollActiveSub` 上按 `m_activeSub` 表达（XScrollBar.c:188），Add/Sub 区分对绘制无影响。
5. **右键菜单上下文靠模块级单例传递**：`xsb_ctxOwner`/`xsb_ctxPos`（XScrollBar.c:368-369）是文件级静态，`contextMenuEvent` 写、`xsb_actScrollHereSlot` 校验 owner 匹配后读（XScrollBar.c:380-383）。同一时刻只能有一个滚动条的菜单「滚动到此处」有效，第二条滚动条弹菜单会覆盖前者的目标坐标。
6. **文件头「全部公共 API」与实际转发面不符**：XScrollBar.h:3 声称对标 QScrollBar 全部公共 API，但 `QAbstractSlider` 的 tracking/sliderPosition/invertedAppearance/invertedControls/triggerAction/repeatAction 在 XScrollBar 层无同名宏（详见上文清单），只能经 `XAbstractSlider_*` 调用。

**测试覆盖**：有专属套件 `Test/XGuiTest/XScrollBarTest.c`（121 行）+ 头 `XScrollBarTest.h`，入口 `bool XScrollBarTest_runAll(void)`（XScrollBarTest.c:31；模块关闭分支 :116 返回 true）。断言助手 `sb_expect`（:15）共 **15 个调用点**：默认状态（Vertical / 0..99 / step 1/10 / value 0 / sizeHint 15x15，:42-52）、方向重载与切换（:60-65）、标准菜单创建与条目（:74-75）、setValue 同值不重发（:87-89）、PageStepAdd/Sub（:95-98）。按钮显隐不在本套件，改由 `Test/XGuiDemo/xgui_demo_apitest_input.c:3247-3251` 两条 `XAPI_EXPECT` 覆盖 `setShowButtons`/`showButtons` 往返。构建注册：`Test/CMakeLists.txt:563`（并入 `XGuiRegression_Test`，:552）。

---

## XShortcut

**定位**：快捷键值对象（键码/启用/上下文/自动重复/WhatsThis + 全局注册表与按键匹配）。继承链 `XObject ← XShortcut`（XShortcut.h:70-71）。头文件 `Src/XGui/Widget/XShortcut.h`（301 行）。守卫 `XGUI_ON && XWIDGET_ON`（XShortcut.h:42）。

**生命周期**：`XShortcut_init` 零化 → `XObject_init` → 挂 vtable → `m_enabled=true`、`m_autoRepeat=true`、`m_context=WindowShortcut`、`m_parentWidget=parent`（XShortcut.c:78-92）；**`init` 不进注册表**，只有 `*_create_ex` 在末尾调 `XShortcut_register`（XShortcut.c:94-104，register 在 :102）。析构槽 `VX_shortcut_deinit` 先 `XShortcut_unregister` 再释放 `m_whatsThis`，最后交 `XObject`（XShortcut.c:59-68）。

**API 面**

枚举 `XShortcutContext`（XShortcut.h:55-61，数值与 Qt `Qt::ShortcutContext` 逐项一致：qnamespace.h:1294-1297）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XShortcut_class_init(void);` | 建/取类共享虚表（仅重载 Deinit） | 同名惯用 | 共享表，禁释放 |
| `void XShortcut_init(XShortcut* self, XObject* parent);` | 嵌入式初始化；**不注册** | `QShortcut(QObject*)` gui/kernel/qshortcut.h:27 | parent 借用 |
| `#define XShortcut_create(parent) XShortcut_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))` | 默认内存类型创建并自动注册 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XShortcut* XShortcut_create_ex(XMemoryType memory, XObject* parent);` | 指定内存类型创建并自动注册 | 同上 | 调用方拥有，须 `XClassDelete` |
| `#define XShortcut_create_2(key, parent) XShortcut_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE, (key), (parent))` | 带初始键码创建 | `QShortcut(const QKeySequence&, QObject*)` gui/kernel/qshortcut.h:28 | 调用方拥有，须 `XClassDelete` |
| `XShortcut* XShortcut_create_2_ex(XMemoryType memory, int key, XObject* parent);` | 指定内存类型 + 键码创建 | 同上 | 调用方拥有，须 `XClassDelete` |
| `void XShortcut_setKey(XShortcut* self, int key);` | 设键码（0 清除） | `setKey` gui/kernel/qshortcut.h:159 | — |
| `int XShortcut_key(const XShortcut* self);` | 查键码；NULL 返回 0 | `key` gui/kernel/qshortcut.h:160 | 值返回 |
| `void XShortcut_setEnabled(XShortcut* self, bool enabled);` | 设启用（禁用后 match 不命中） | `setEnabled` gui/kernel/qshortcut.h:165 | — |
| `bool XShortcut_isEnabled(const XShortcut* self);` | 查启用；NULL 返回 false | `isEnabled` gui/kernel/qshortcut.h:166 | 值返回 |
| `void XShortcut_setContext(XShortcut* self, XShortcutContext context);` | 设上下文（不校验取值） | `setContext` gui/kernel/qshortcut.h:168 | — |
| `XShortcutContext XShortcut_context(const XShortcut* self);` | 查上下文；NULL 返回 WindowShortcut | `context` gui/kernel/qshortcut.h:169 | 值返回 |
| `void XShortcut_setAutoRepeat(XShortcut* self, bool on);` | 设自动重复（仅存标志，无重复事件路径） | `setAutoRepeat` gui/kernel/qshortcut.h:171 | — |
| `bool XShortcut_autoRepeat(const XShortcut* self);` | 查自动重复；NULL 返回 false | `autoRepeat` gui/kernel/qshortcut.h:172 | 值返回 |
| `void XShortcut_setWhatsThis(XShortcut* self, const XString* text);` | 设帮助文本（内部拷贝；NULL 清空） | `setWhatsThis` gui/kernel/qshortcut.h:178 | 入参借用，内部深拷贝 |
| `void XShortcut_setWhatsThis_2(XShortcut* self, const char* utf8);` | UTF-8 重载（临时 XString 用后释放） | 同上 | 入参借用 |
| `XString* XShortcut_whatsThis(const XShortcut* self);` | 帮助文本**拷贝**；未设/NULL 返回 NULL | `whatsThis` gui/kernel/qshortcut.h:179 | **调用方拥有，须 `XClassDelete`** |
| `void XShortcut_register(XShortcut* self);` | 注册进模块静态注册表（重复忽略） | X 扩展（Qt 由 `QShortcutMap` 内部登记） | 入参借用 |
| `void XShortcut_unregister(XShortcut* self);` | 从注册表注销（未注册为 no-op） | X 扩展 | 入参借用 |
| `XShortcut* XShortcut_match(int key, XShortcutContext context, XWidget* focusWidget);` | 遍历注册表返回首个命中者；`context` 形参当前**不使用** | X 扩展（对标 `QShortcutMap` 派发） | **返回借用指针，禁释放** |
| `void XShortcut_activate(XShortcut* self);` | 无歧义发 `activated`，同键多命中改发 `activatedAmbiguously` | X 扩展（对标 QShortcut 内部激活语义） | — |
| `void* XShortcut_activated_signal(XShortcut* self);` | activated 信号标识（空参，args=NULL） | `activated()` gui/kernel/qshortcut.h:193 | 不透明标识，非对象 |
| `void* XShortcut_activatedAmbiguously_signal(XShortcut* self);` | 歧义激活信号标识 | `activatedAmbiguously()` gui/kernel/qshortcut.h:194 | 不透明标识，非对象 |

**所有权与命名契约**：查询取 `const XShortcut*`；`whatsThis` 是**唯一拷贝型 getter**（返回值须释放），`key`/`isEnabled`/`context`/`autoRepeat` 为值返回。`m_parentWidget` 为借用指针（XShortcut.h:86）。`_2` 用于「同功能不同参数」的 UTF-8 重载（`setWhatsThis_2`）——按 XGui.md:1811-1813 的登记，全库 213 个以 `const char*` 为参的 `_2` 计划统一改 `_utf8`，本类属待改名单。无三态 `set*`（`setEnabled`/`setAutoRepeat` 二态，`setContext` 单值，`setKey` 单值）。信号发射侧 `XSignal(XShortcut_activated_signal)` 才是连接入口（XObject.h:439 宏）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XShortcut)` + `XCLASS_DEFINE_EXTEND_END(XShortcut, XObject)`（XShortcut.h:70-71），**不新增槽位**；`class_init` 只重载 `EXClass_Deinit`→`VX_shortcut_deinit`（注销注册表 + 释放 `m_whatsThis`，XShortcut.c:70-76）。无 `_base` 入口（`match`/`activate` 直接实现，非虚槽）。

**注意事项与坑点**

1. **头注释与实现的匹配语义矛盾（重要）**：XShortcut.h:22-23 写「WidgetShortcut 要求 focusWidget 非 NULL 且等于创建时传入的 parent（**parent 为 NULL 时等价任意焦点控件**）」，但实现 `xshortcut_accepts` 只在 `(const XObject*)focusWidget == sc->m_parentWidget` 时返回 true（XShortcut.c:244-246）——parent 为 NULL 时 `focusWidget` 必为 NULL 才相等，而 :218 已对 `!focusWidget` 早退 false，故**永远不会命中**，与注释相反。WidgetWithChildren 分支同样在无 parent 时直接 false（XShortcut.c:235）。
2. **`WindowShortcut` 比头注释宽松**：头注释只写「要求 focusWidget 非 NULL」，实现另有「无父级/非控件父级 → 直接 true」（XShortcut.c:221-231），即无父窗口的快捷键对任意焦点控件均可命中（注释自称 legacy 宽松口径）。
3. **`match` 的 `context` 形参加入而不生效**：实现首行 `(void)context;`（XShortcut.c:255）；实际过滤全由每条快捷键自己的 `m_context` 完成。`XWidget` 按键路径也固定传 `(XShortcutContext)0`（XWidget.c:3331-3336）。
4. **`init` 与 `create` 的注册语义不同**：栈对象 `XShortcut_init` 不进注册表（XShortcut.c:78-92 无 register），只 `*_create_ex` 注册（XShortcut.c:102）。用 init 构造后必须手动 `XShortcut_register` 才能被 `match` 找到；demo 里正是 `XShortcut_init(&sc, NULL)` 只验属性、另用 `XShortcut_create_2_ex` 才验 `match`（xgui_demo_apitest_menus.c:1718、:1734-1739）。
5. **注册表永不释放**：`s_shortcutRegistry` 为文件级 `XVector*`（XShortcut.c:24），惰性创建于 `xshortcut_ensureRegistry`（:27-31），全文件无销毁调用（`rg -n "XVector_delete|Destroy"` 在 XShortcut.c 无命中）——进程级常驻，属设计取舍但会让「全部对象已删」的内存检查看到残留容器。
6. **信号函数是「ID 函数」而非发射器**：`XShortcut_activated_signal`/`_activatedAmbiguously_signal` 忽略 self 并返回自身函数地址（XShortcut.c:327-337），正是 `XObject_signalIdOf` 以 NULL 调用取真址的约定（XObject.c:751-769 实现 + 注释；宏定义在 XObject.h:439），连接必须写 `XSignal(...)`，不能直接取函数指针。

**测试覆盖**：无 `Test/XGuiTest/XShortcutTest.c` 专属套件（`ls` 报「没有那个文件或目录」）。间接覆盖两处：① `Test/XGuiDemo/xgui_demo_apitest_menus.c` 第 7 节（:1706-1837）共 **23 条 `XAPI_EXPECT`**——默认值、创建即注册、Application/Window/WidgetWithChildren 三种 context 的 match、setEnabled 生效/恢复、unregister/register 往返、setKey 清除与回填、setWhatsThis_2 往返与 NULL 清除、activate 发射 activated、activatedAmbiguously 可连接；② `xgui_regression_test.c:37383-37445`（Task 2.19a 子批）**13 条 `expect_true`**——属性往返、启用位、whatsThis、activate、Window/Application 匹配与禁用后不命中。

---

## XSizeGrip

**定位**：窗口右下角尺寸手柄（拖动改顶层窗口尺寸，绘斜点阵三角）。继承链 `XObject ← XWidget ← XSizeGrip`（XSizeGrip.h:26-27）。头文件 `Src/XGui/Widget/XSizeGrip.h`（71 行）。守卫 `XGUI_ON && XWIDGET_ON && XSIZEGRIP_ON`（XSizeGrip.h:23；开关 XGuiConfig.h:578-579）。

**生命周期**：`XSizeGrip_init` 零化 → `XWidget_init(..., flags=0)` → 挂 vtable → `resize(16,16)` + sizeHint/minimumSizeHint 均 16x16（XSizeGrip.c:229-247）。无自有堆成员、无 Deinit 槽（class_init 只挂 4 个事件槽，XSizeGrip.c:215-227）；**拖拽会话状态不在对象内**，而是文件级静态单例（XSizeGrip.c:29-33：`g_sizeGripDragActive`/`g_sizeGripPressGlobal`/`g_sizeGripStartGeometry`/`g_sizeGripAtBottom`/`g_sizeGripAtLeft`）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSizeGrip_class_init(void);` | 建/取类共享虚表 | 同名惯用 | 共享表，禁释放 |
| `void XSizeGrip_init(XSizeGrip* self, XWidget* parent);` | 初始化（16x16，sizeHint=minimumSizeHint=16x16） | `QSizeGrip(QWidget*)` qsizegrip.h:19 | parent 借用 |
| `#define XSizeGrip_create(parent) XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSizeGrip* XSizeGrip_create_ex(XMemoryType memory, XWidget* parent);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |

**所有权与命名契约**：全类仅 4 个公开符号，无 getter/setter（尺寸/位置/etc. 全走继承的 `XWidget_*`）；无 `_const` 借用族、无 `_utf8`、无 `_2`、无三态 `set*`。父控件的唯一所有权经由 `XWidget_init` 的 parent 借用语义。遵循 XWidget 基类契约。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSizeGrip)` + `XCLASS_DEFINE_EXTEND_END(XSizeGrip, XWidget)`（XSizeGrip.h:26-27），**不新增槽位**。重载 4 槽：`EXWidget_PaintEvent`→样式 `CE_SizeGrip` 优先，否则按 4px 步长画右下斜点阵（XSizeGrip.c:35-100）、`EXWidget_MousePressEvent`→非左键交父类、左键记全局基线 + 判角 + `grabMouse`（:102-140）、`EXWidget_MouseMoveEvent`→按全局位移改顶层窗口几何并钳最小尺寸（:142-193）、`EXWidget_MouseReleaseEvent`→清会话 + `releaseMouse`（:195-213）。无 `_base` 入口。

**注意事项与坑点**

1. **Qt 的 `sizeHint` 不是 16x16**：X 侧固定 16x16（XSizeGrip.c:239-241）；Qt 用 `CT_SizeGrip` + 内容 `QSize(13,13)` 走样式（qsizegrip.cpp:199-204），随样式/主题变化。
2. **Qt 重写了 `setVisible`/`show` 相关事件，X 没有**：`qsizegrip.h:22-34` 声明了 `sizeHint`、`setVisible`、`moveEvent`、`showEvent`、`hideEvent`、`eventFilter`、`event` 七个覆写；X 只覆写 paint 与三个鼠标事件（XSizeGrip.c:215-227）。因此 Qt 的「平台不支持尺寸调整时隐藏手柄」行为在 X 侧无对应（X 未实现该判定）。
3. **拖拽状态是模块级单例，不随对象析构复位**：状态存于 XSizeGrip.c:29-33 的静态变量，本类**无 Deinit 槽**（XSizeGrip.c:215-227），故拖拽中销毁手柄不会清 `g_sizeGripDragActive`/`g_sizeGripStartGeometry`；下一路 `MouseMove` 只要左键按下就会用残留基线改「当前顶层窗口」几何。源码注释已声明前提「同一时刻至多一路拖拽」（XSizeGrip.c:25-28），但未提析构路径。
4. **不要在覆写里调 `XWidget_mousePressEvent_base`**：源码注释明确该助手是 vtable 派发助手，会重入本重载造成无限递归，必须用 `XClass_Parent(XWidget, EXWidget_MousePressEvent, ...)` 直呼父类（XSizeGrip.c:112-118 注释 + 实现）。
5. **`XWidget_init` 的 flags 被硬编码 0**：`XWidget_init(&self->m_base, parent, 0)`（XSizeGrip.c:234），公开 API 无 flags 形参，想构造为 `SubWindow`/`Tool` 等窗口类型只能构造后 `XWidget_setWindowFlags`。

**测试覆盖**：无 `Test/XGuiTest/XSizeGripTest.c` 专属套件。间接覆盖：① `xgui_regression_test.c:32083-32093`（`test_small_widgets_contract`，助手 `sml_expect` 定义于 :32074）**2 条断言**——创建成功、`XWidget_sizeHint` 为 16x16；② `xgui_regression_test.c:30965` 前一行段有虚表创建断言族（`xtw_expect_class`）；③ `Test/XGuiDemo/xgui_demo_apitest_menus.c:2184-2257`（第 12 节）**15 条 `XAPI_EXPECT`**——`class_init` 非空、栈上 init 后 16x16、resize/move/geometry 往返、hidden/visibleTo/enabled 位、create_ex 父子关系与 16x16；④ `Test/XGuiDemo/xgui_demo_page_advanced.c` 有 8 处页面级引用（含截图目验路径）。

---

## XSlider

**定位**：水平/垂直滑块（刻度位置/间隔 + 凹槽/立体 handle 绘制 + 点击定位与拖动，值/范围/步进全部继承）。继承链 `XObject ← XWidget ← XAbstractSlider ← XSlider`（XSlider.h:87-88）。头文件 `Src/XGui/Widget/XSlider.h`（154 行）。守卫 `XGUI_ON && XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON`（XSlider.h:33；开关 XGuiConfig.h:542-543）。

**生命周期**：`XSlider_init` 先 `XAbstractSlider_init`（基类置 min=0/max=99/value=0/pageStep=10/singleStep=1/tracking=true、orientation=Horizontal，XAbstractSlider.c:505-519），再挂 vtable，置 `m_tickPosition=NoTicks`、`m_tickInterval=0`、`m_dragOffset=0`，显式 `setFocusPolicy(StrongFocus)` 与 `XWidgetAttribute_Hover`，末尾 `xslider_updateSizeHint`（XSlider.c:604-624）。无自有堆成员；Deinit 走基类，`Copy`/`Move` 已重载（XSlider.c:482-492、:495-508；槽注册 :598-599）。

**API 面**

枚举 `XSliderTickPosition`（XSlider.h:76-82：NoTicks=0/TicksAbove=1/TicksBelow=2/TicksBothSides=3，与 qslider.h:25-32 数值一致，Qt 另把 `TicksLeft/TicksRight` 定义为别名）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSlider_class_init(void);` | 建/取类共享虚表 | 同名惯用 | 共享表，禁释放 |
| `void XSlider_init(XSlider* self, XWidget* parent, XWidgetFlags flags);` | 初始化（**默认水平**） | `QSlider(QWidget*)` qslider.h:35（Qt 内部按 **Vertical**，qslider.cpp:253-255） | parent 借用 |
| `#define XSlider_create(parent, flags) XSlider_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSlider* XSlider_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `int XSlider_tickPosition(const XSlider* self);` | 查刻度位置；NULL 返回 NoTicks | `tickPosition` qslider.h:44 | 值返回 |
| `void XSlider_setTickPosition(XSlider* self, int position);` | 设刻度位置（越界静默忽略，相同值早退，变化后重绘） | `setTickPosition` qslider.h:43 | — |
| `int XSlider_tickInterval(const XSlider* self);` | 查刻度值间隔（0=自动） | `tickInterval` qslider.h:47 | 值返回 |
| `void XSlider_setTickInterval(XSlider* self, int interval);` | 设刻度间隔（负值按 0） | `setTickInterval` qslider.h:46 | — |
| 父类转发宏 35 条（XSlider.h:39-73） | `XSlider_{orientation,setOrientation,minimum,setMinimum,maximum,setMaximum,setRange,singleStep,setSingleStep,pageStep,setPageStep,hasTracking,setTracking,setSliderDown,isSliderDown,sliderPosition,setSliderPosition,invertedAppearance,setInvertedAppearance,invertedControls,setInvertedControls,value,setValue,triggerAction,setRepeatAction,repeatAction,stepBy_base,sliderChange_base,stepEnabled_base,valueChanged_signal,sliderPressed_signal,sliderMoved_signal,sliderReleased_signal,rangeChanged_signal,actionTriggered_signal}` | `setValue` qabstractslider.h:82、`hasTracking` :52、`sliderPosition` :58、`setRepeatAction` :100、`sliderPressed` :89、`sliderReleased` :91、`rangeChanged` :93 | 全部 `—`（`value` 等为值返回） |

**所有权与命名契约**：全部查询取 `const XSlider*`；无 `_utf8`、无 `_2`（本类无字符串 API）；无三态 `set*`（`setTickPosition`/`setTracking`/`setSliderDown` 均为单值/二态）。父类转发 35 条，**未覆盖** `sizeHint`/`minimumSizeHint`（无 `XSlider_sizeHint` 宏，对比 `XScrollBar_sizeHint` 存在，XScrollBar.h:141）——尺寸只能走 `XWidget_sizeHint`。拷贝/移动统一 `XClassCopy`/`XClassMove`（`VXSlider_copy`/`VXSlider_move` 复制刻度并清 `m_dragOffset`，XSlider.c:485-508）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSlider)` + `XCLASS_DEFINE_EXTEND_END(XSlider, XAbstractSlider)`（XSlider.h:87-88），**不新增槽位**。重载 8 槽（XSlider.c:587-601）：`EXWidget_SizeHint`→`VXSlider_sizeHintSlot`（先重算缓存再读回）、`EXWidget_MinimumSizeHint`→同法，`EXWidget_MousePressEvent`→handle 命中/凹槽绝对定位、`EXWidget_MouseMoveEvent`→`setSliderPosition`、`EXWidget_MouseReleaseEvent`→`setSliderDown(false)` + 释放抓取、`EXWidget_PaintEvent`→凹槽+handle+刻度，`EXClass_Copy`/`EXClass_Move`→带刻度字段的深浅拷贝。`_base` 入口 3 个宏：`XSlider_stepBy_base`/`sliderChange_base`/`stepEnabled_base`（XSlider.h:65-67），默认落到 `XAbstractSlider` 的同名实现。

**注意事项与坑点**

1. **默认方向与 Qt 相反**：头文件写「默认水平方向」（XSlider.h:114），实现确实水平（`XAbstractSlider_init` 置 Horizontal，XAbstractSlider.c:513）；Qt 的 `QSlider(QWidget*)` 直接委托 `QSlider(Qt::Vertical, parent)`（qslider.cpp:253-255）。按 Qt 移植的代码不显式设方向会得到相反的滑块。
2. **`tickInterval` 存 16 位但 setter 不钳位**：字段声明 `uint32_t m_tickInterval : 16`（XSlider.h:105，注释自称「0..65535 收 16 位」），而 `XSlider_setTickInterval` 只把负值抬到 0，其余原样赋值（XSlider.c:658-665）——传 >65535 会被位域静默截断（如 70000 → 4464），`tickInterval()` 读回的不是设进去的值。
3. **有刻度时 sizeHint 不加 `TickSpace`**：X 的 `xslider_updateSizeHint` 只按 `PM_SliderThickness` + 固定 `SliderLength=84` 取内容尺寸（XSlider.c:540-549），Qt 在 `TicksAbove`/`TicksBelow` 各加 `TickSpace=5`（qslider.cpp:428、432-435）。带刻度的滑块 X 侧建议厚度偏小 5~10px。
4. **注释里的 Qt 行号错一行**：XSlider.c:528 注释称 `SliderLength` 常量出自 `qslider.cpp:427`，实读 qslider.cpp:428 才是 `const int SliderLength = 84, TickSpace = 5;`（:427 是 `ensurePolished();`）。
5. **绘制刻度与自动间隔的回落链**：`m_tickInterval<=0` 时按 `pageStep → singleStep → 1` 取间隔（XSlider.c:265-272），与头文件 XSlider.h:5-6 的「interval=0 时自动回退 pageStep/singleStep 间隔」一致；但刻度循环是 `for (v = base->m_min; v <= base->m_max; v += interval)`（XSlider.c:281），`interval` 由 pageStep 提供时若 range 不是 pageStep 的整数倍，末端刻度不会落在 maximum 上。
6. **`setTickPosition` 越界静默丢弃**：`position < 0 || position > TicksBothSides` 直接 return（XSlider.c:646-648），不告警不报错；Qt 的 `setTickPosition` 无取值校验（qslider.cpp:472-479），传非法值会存下去。

**测试覆盖**：有专属套件 `Test/XGuiTest/XSliderTest.c`（536 行）+ `XSliderTest.h`，入口 `bool XSliderTest_runAll(void)`（XSliderTest.c:146），断言助手 `sl_expect`（:30）共 **89 个调用点**。覆盖面（文件头 XSliderTest.c:5-20 自述 + 抽样核对）：默认状态与继承结构（:154-160）、setValue 钳位与 valueChanged、setRange/setMinimum/setMaximum 联动、单步/翻页、hasTracking、setSliderDown 与 pressed/released、sliderPosition 两种 tracking 提交语义、triggerAction 全动作、stepBy/stepEnabled 虚槽、setRepeatAction、键盘（Up/Right/Down/Left/PageUp/PageDown/Home/End + invertedControls）、滚轮、刻度 API（:169-171、:268-277、:506-508）、鼠标交互、Copy/Move。构建注册：`Test/CMakeLists.txt:555`。

---

## XSpinBox

**定位**：整数微调框（内嵌 XLineEdit 编辑 + 自绘上下箭头 + 前缀/后缀/进制/特殊值/千分位）。继承链 `XObject ← XWidget ← XAbstractSpinBox ← XSpinBox`（XSpinBox.h:37-40）。头文件 `Src/XGui/Widget/XSpinBox.h`（216 行）。守卫 `XGUI_ON && XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON`（XSpinBox.h:34；开关 XGuiConfig.h:750-751）。

**生命周期**：`XSpinBox_init` 先 `XAbstractSpinBox_init`（基类建内嵌 `XLineEdit`），置 min=0/max=99/value=0/singleStep=1/stepType=Default（XSpinBox.c:1073）/displayIntegerBase=10，`Hover` 属性，随后刷新文本、给内嵌编辑框设 `XInputMethodHint_DigitsOnly`、装数值 validator 并连 `textChanged`/`editingFinished`，末尾 `spinbox_refreshSizeHint`（XSpinBox.c:1062-1108）。`VXSpinBox_deinit` 释放 `m_prefix`/`m_suffix` 后交基类（XSpinBox.c:946-959）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSpinBox_class_init(void);` | 建/取类共享虚表（新增 2 槽 + 15 处重载） | 同名惯用 | 共享表，禁释放 |
| `void XSpinBox_init(XSpinBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化（0..99/value 0/step 1/base 10） | `QSpinBox(QWidget*)` qspinbox.h:30 | parent 借用 |
| `#define XSpinBox_create(parent, flags) XSpinBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSpinBox* XSpinBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `int XSpinBox_minimum(const XSpinBox* self);` | 查下限（NULL 返回 0） | `minimum` qspinbox.h:46 | 值返回 |
| `void XSpinBox_setMinimum(XSpinBox* self, int min);` | 设下限（超过上限时上限同步抬升，经 setRange） | `setMinimum` qspinbox.h:47 | — |
| `int XSpinBox_maximum(const XSpinBox* self);` | 查上限（NULL 返回 99） | `maximum` qspinbox.h:49 | 值返回 |
| `void XSpinBox_setMaximum(XSpinBox* self, int max);` | 设上限（低于下限时下限同步下调） | `setMaximum` qspinbox.h:50 | — |
| `void XSpinBox_setRange(XSpinBox* self, int min, int max);` | 设范围（min>max 时取 (min,min)）；值重钳位 | `setRange` qspinbox.h:52 | — |
| `int XSpinBox_value(const XSpinBox* self);` | 查当前值 | `value` qspinbox.h:33 | 值返回 |
| `void XSpinBox_setValue(XSpinBox* self, int value);` | 设值（钳位；变化时刷文本并发射 valueChanged） | `setValue` qspinbox.h:69 | — |
| `int XSpinBox_singleStep(const XSpinBox* self);` | 查单步（NULL 返回 1） | `singleStep` qspinbox.h:43 | 值返回 |
| `void XSpinBox_setSingleStep(XSpinBox* self, int step);` | 设单步（<0 忽略） | `setSingleStep` qspinbox.h:44 | — |
| `const char* XSpinBox_prefix(const XSpinBox* self);` | 查前缀 UTF-8 借用串（未设为 ""） | `prefix` qspinbox.h:35 | **借用禁释放**（指向 `m_prefix` 内部缓冲） |
| `void XSpinBox_setPrefix(XSpinBox* self, const char* prefix);` | 设前缀（相同值早退；内部持有） | `setPrefix` qspinbox.h:36 | 入参借用，内部复制 |
| `const char* XSpinBox_suffix(const XSpinBox* self);` | 查后缀 UTF-8 借用串 | `suffix` qspinbox.h:38 | **借用禁释放** |
| `void XSpinBox_setSuffix(XSpinBox* self, const char* suffix);` | 设后缀 | `setSuffix` qspinbox.h:39 | 入参借用，内部复制 |
| `char* XSpinBox_cleanText(const XSpinBox* self);` | 纯净文本（剥前后缀/千分位/空白） | `cleanText` qspinbox.h:41 | **调用方拥有，须 `XFree_System`** |
| `int XSpinBox_stepType(const XSpinBox* self);` | 查步进类型 | `stepType` qspinbox.h:54 | 值返回 |
| `void XSpinBox_setStepType(XSpinBox* self, int stepType);` | 设步进类型（仅接受 Default/Adaptive 两值，其余忽略） | `setStepType` qspinbox.h:55 | — |
| `int XSpinBox_displayIntegerBase(const XSpinBox* self);` | 查显示进制（NULL 返回 10） | `displayIntegerBase` qspinbox.h:57 | 值返回 |
| `void XSpinBox_setDisplayIntegerBase(XSpinBox* self, int base);` | 设显示进制（<2 或 >36 回退 10，变化后刷文本） | `setDisplayIntegerBase` qspinbox.h:58 | — |
| `int XSpinBox_valueFromText_base(const XSpinBox* self, const char* text);` | 虚槽调度入口：文本→值（失败/特殊值文本返回 minimum） | `valueFromText` qspinbox.h:63 | text 借用 |
| `char* XSpinBox_textFromValue_base(const XSpinBox* self, int val);` | 虚槽调度入口：值→文本（前缀+按进制格式化+后缀） | `textFromValue` qspinbox.h:64 | **调用方拥有，须 `XFree_System`** |
| `void* XSpinBox_valueChanged_signal(XSpinBox* self);` | valueChanged(int) 信号标识 | `valueChanged` qspinbox.h:72 | 不透明标识 |
| `void* XSpinBox_textChanged_signal(XSpinBox* self);` | textChanged(const char*) 信号标识（转发编辑框） | `textChanged` qspinbox.h:73 | 不透明标识 |
| 父类转发宏 36 条（XSpinBox.h:98-135） | `XSpinBox_{lineEdit,setLineEdit,buttonSymbols,setButtonSymbols,correctionMode,setCorrectionMode,hasAcceptableInput,text,specialValueText,setSpecialValueText,wrapping,setWrapping,isReadOnly,setReadOnly,keyboardTracking,setKeyboardTracking,alignment,setAlignment,hasFrame,setFrame,setAccelerated,isAccelerated,setGroupSeparatorShown,isGroupSeparatorShown,interpretText,stepUp,stepDown,selectAll,stepBy_base,validate_base,fixup_base,clear_base,stepEnabled_base,interpret_base,updateEdit_base,editingFinished_signal}` | `lineEdit`/`setLineEdit` qabstractspinbox.h:129-130、`setButtonSymbols` :48、`setCorrectionMode` :53、`hasAcceptableInput` :56、`specialValueText` :59、`setWrapping` :63、`setReadOnly` :65、`setKeyboardTracking` :68、`setAlignment` :71、`setFrame` :74、`setAccelerated` :77、`setGroupSeparatorShown` :80、`interpretText` :85、`stepUp`/`stepDown`/`selectAll` :102-104、`editingFinished` :134 | 全部 `—`（`lineEdit` 返回借用指针） |

**所有权与命名契约**：`_const` 借用族 = 全部 getter（`minimum`/`maximum`/`value`/`singleStep`/`stepType`/`displayIntegerBase`/`prefix`/`suffix`/`cleanText`/`valueFromText_base`/`textFromValue_base` 均取 `const XSpinBox*`）；返回值分三档：值返回（int/bool）、借用串（`prefix`/`suffix` 指向对象内部）、**须释放**（`cleanText`、`textFromValue_base`）。字符串设置侧用裸 `const char*` 且函数名**无** `_utf8` 后缀（`setPrefix`/`setSuffix`，XSpinBox.h:160/164）——按 XGui.md:1811-1815 的登记口径属「设置侧 char* 必须 `_utf8`」的待改名项。无 `_2` 重载。三态 `set*`：无（`setMinimum`/`setMaximum`/`setRange`/`setValue`/`setSingleStep`/`setDisplayIntegerBase` 均单值，`setStepType` 为枚举白名单）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSpinBox)` 新增 **2 个类槽** `EXSpinBox_ValueFromText`、`EXSpinBox_TextFromValue`（XSpinBox.h:38-39，起点 `XCLASS_VTABLE_GET_SIZE(XAbstractSpinBox)`）。类初始化另重载 15 处（XSpinBox.c:971-989）：`EXWidget_SizeHint`/`MinimumSizeHint`（按文本宽+按钮区+边距）、`EXAbstractSpinBox_{StepBy,Validate,Fixup,Clear,StepEnabled,Interpret,UpdateEdit}`（分别默认：按 singleStep 步进并钳位/判合法/修正/清空/判可否上下列/重解析/刷新编辑框文本）、`EXSpinBox_ValueFromText`→`VXSpinBox_valueFromText`（剥前后缀后按进制解析，失败返回 minimum）、`EXSpinBox_TextFromValue`→`VXSpinBox_textFromValue`（特殊值整段替换，否则前缀+数值+后缀）、`EXWidget_{PaintEvent,MousePressEvent,MouseReleaseEvent}`、`EXClass_{Deinit,Copy,Move}`。`_base` 入口 2 个：`XSpinBox_valueFromText_base`（XSpinBox.c:1274-1280）、`XSpinBox_textFromValue_base`（XSpinBox.c:1282-1288）——均 `XClassGetVirtualFunc` 派发，默认实现见 XSpinBox.c:829-849。

**注意事项与坑点**

1. **`cleanText` 的 NULL self 分支违反头注释**：头注释写「失败返回 NULL」（XSpinBox.h:167），实现 `if (!self) return XStrdup("")`（XSpinBox.c:1233）——NULL self 拿到的是**空串**而非 NULL，调用方按「NULL 才跳过释放」写会漏 `XFree_System`。非 NULL 路径同样始终返回 `XStrdup` 结果（XSpinBox.c:1238-1239），trim 失败时返回原文拷贝（不返回 NULL）。
2. **`stepType` 是纯存储字段，自适应步长未实现**：结构体注释自述「字段保留，自适应步长行为为后续扩展」（XSpinBox.h:49-51），setter 注释同（XSpinBox.h:172）；实现 `VXSpinBox_stepBy` 只按 `m_singleStep` 乘步数（XSpinBox.c:480-505），全文件无 `m_stepType` 参与步进计算的读取（读取点仅 `XSpinBox_stepType`（:1242-1246）、setter（:1248-1255）与 init/Copy/Move（:1073、:864、:911、:932））。设 `AdaptiveDecimalStepType` 不会改变步进行为。
3. **`setRange` 的「Qt 语义」是缩上限而非交换**：实现 `m_max = (min < max) ? max : min`（XSpinBox.c:1149），与 Qt `QAbstractSpinBoxPrivate::setRange` 一致（qabstractspinbox.cpp:1812 `maximum = (variantCompare(min, max) < 0 ? max : min)`）；头注释 XSpinBox.h:147「按 Qt 语义取 (min,min)」措辞易被读成交换参数，实际是上限收敛到 min。
4. **头文件有两处 Doxygen 文本混排/残缺**：`/** @brief XSpin盒init（对标 Qt 同名接口）。`（XSpinBox.h:79）与 `/** @brief XSpin盒createex（对标 Qt 同名接口）。`（XSpinBox.h:88）——中英混排且 `create ex` 掉了空格，会原样进生成文档。
5. **进制越界不告警**：`setDisplayIntegerBase` 对 `<2`/`>36` 静默回退 10（XSpinBox.c:1262-1270），Qt 会 `qWarning("QSpinBox::setDisplayIntegerBase: Invalid base (%d)", base)`（qspinbox.cpp:449-452）。依赖告警做配置校验的移植代码在 X 侧静默。
6. **前缀/后缀 getter 的返回值会随下次 set 失效**：`XSpinBox_prefix` 返回 `m_prefix` 的 UTF-8 内部缓存（`spinbox_prefixText` → `XString_toUtf8`，XSpinBox.c:73-79；getter 本体 :1187-1190），未设时返回**字面量 ""**；调用方若长期持有该指针，`setPrefix`/`XString_assign_utf8` 后即悬垂。

**测试覆盖**：有专属套件 `Test/XGuiTest/XSpinBoxTest.c`（477 行，无模块裁剪 `#if` 包裹）+ `XSpinBoxTest.h`，入口 `bool XSpinBoxTest_runAll(void)`（XSpinBoxTest.c:77），断言助手 `sb_expect`（:27）共 **117 个调用点**。覆盖（文件头 XSpinBoxTest.c:5-14 自述 + 抽样核对）：默认状态（:91-104）、setValue/setRange/setMinimum/setMaximum 钳位、StepBy/stepUp/stepDown、文本解析与两种 correctionMode 提交修正、按钮符号枚举、基类属性、键盘步进、滚轮、prefix/suffix、displayIntegerBase、特殊值文本、千分位、wrapping、validate、clear/selectAll、valueChanged/textChanged、几何（:459-462）。另有独立探针 `XSpinBoxGeometryProbe`（`Test/CMakeLists.txt:413-426`）。构建注册：`Test/CMakeLists.txt:556`。

---

## XSplashScreen

**定位**：启动画面控件（背景图 + 底部消息文本 + 点击关闭）。继承链 `XObject ← XWidget ← XSplashScreen`（XSplashScreen.h:39-40）。头文件 `Src/XGui/Widget/XSplashScreen.h`（127 行）。守卫 `XGUI_ON && XWIDGET_ON && XSPLASHSCREEN_ON`（XSplashScreen.h:37；开关 XGuiConfig.h:587-588），`setPixmap`/`pixmap` 另受 `XPIXMAP_ON`（XSplashScreen.h:58-69）。

**生命周期**：`XSplashScreen_init` 零化 → `XWidget_init` → 挂 vtable → `m_alignment=Alignment_Left`、`m_color=0xFF000000`、`resize(400,300)`（XSplashScreen.c:129-141）。`VXSplashScreen_deinit` 释放深拷贝的 `m_pixmap` 与 `m_message`（XSplashScreen.c:103-116）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSplashScreen_class_init(void);` | 建/取类共享虚表 | 同名惯用 | 共享表，禁释放 |
| `void XSplashScreen_init(XSplashScreen* self, XWidget* parent, XWidgetFlags flags);` | 初始化（400x300，左对齐黑字） | `QSplashScreen(const QPixmap&, Qt::WindowFlags)` qsplashscreen.h:21 | parent 借用 |
| `#define XSplashScreen_create(parent, flags) XSplashScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSplashScreen* XSplashScreen_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `void XSplashScreen_setPixmap(XSplashScreen* self, const XPixmap* pixmap);` | 设背景图（**深拷贝**，并按图尺寸 resize；NULL 清除） | `setPixmap` qsplashscreen.h:25 | 入参借用，内部深拷贝（XSplashScreen.h:62-64） |
| `const XPixmap* XSplashScreen_pixmap(const XSplashScreen* self);` | 查背景图 | `pixmap` qsplashscreen.h:26 | **借用禁释放** |
| `void XSplashScreen_showMessage(XSplashScreen* self, const char* utf8, int alignment, uint32_t color);` | 显示消息（UTF-8，NULL 按空串；存对齐/颜色并发 messageChanged） | `showMessage` qsplashscreen.h:32 | 入参借用，内部持有 |
| `void XSplashScreen_clearMessage(XSplashScreen* self);` | 清空消息并**无条件**发 messageChanged("") | `clearMessage` qsplashscreen.h:34 | — |
| `const char* XSplashScreen_message(const XSplashScreen* self);` | 查消息 UTF-8 借用串（无消息/NULL 返回 ""） | `message` qsplashscreen.h:29 | **借用禁释放**，下次改消息后失效 |
| `void XSplashScreen_finish(XSplashScreen* self, XWidget* widget);` | 关闭启动画面（**不等待** widget 显示，`widget` 形参未使用） | `finish` qsplashscreen.h:27（Qt 会 `waitForWidgetMapped`，qsplashscreen.cpp:236-244） | widget 借用 |
| `void XSplashScreen_repaint(XSplashScreen* self);` | 立即重绘（update + flushBackingStore） | `repaint` qsplashscreen.h:28 | — |
| `void* XSplashScreen_messageChanged_signal(XSplashScreen* self, const char* message);` | messageChanged(const char*) 信号标识（真发射，载荷 XString*） | `messageChanged` qsplashscreen.h:37 | 不透明标识；message 借用 |

**所有权与命名契约**：查询取 `const XSplashScreen*`（`pixmap`/`message`）；`setPixmap` 是真深拷贝，源对象返回后即可释放（XSplashScreen.h:62-64）；`message` 返回内部 UTF-8 借用（XSplashScreen.h:94-97 明示「禁止释放或修改，下次修改消息后失效」）。`showMessage` 的 `const char* utf8` 按参数名标注 UTF-8 但函数名不带 `_utf8` 后缀（属 XGui.md:1815 登记的全库待改名族）。无 `_2`、无三态 `set*`。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSplashScreen)` + `XCLASS_DEFINE_EXTEND_END(XSplashScreen, XWidget)`（XSplashScreen.h:39-40），**不新增槽位**。重载 3 槽（XSplashScreen.c:118-127）：`EXWidget_PaintEvent`→`VX_splash_paintEvent`（先自填黑底再叠 pixmap 与文本，XSplashScreen.c:72-79）、`EXWidget_MousePressEvent`→`VX_splash_mousePressEvent`（任何鼠标按下即 `XWidget_close`，XSplashScreen.c:95-101）、`EXClass_Deinit`→释放两自有对象。无 `_base` 入口（Qt 的 `drawContents` 钩子在 X 侧无对应，绘制不可注入）。

**注意事项与坑点**

1. **`finish` 不等 widget 显示**：头文件与实现都自述「第一版为直接关闭的简化实现，不等待 widget 显示」（XSplashScreen.h:100-104、XSplashScreen.c:220-226，`(void)widget;`）；Qt 版本会 `createWinId()` + `waitForWidgetMapped` 再 `close()`（qsplashscreen.cpp:236-244）。
2. **点击是 `close()` 而 Qt 是 `hide()`**：X 侧 `XWidget_close(self)` 会走 CLOSE 事件并可被拒绝（XWidget.c:8920-8936：`accepted` 才 `setVisible(false)`）；Qt 的 `mousePressEvent` 直接 `hide()`（qsplashscreen.cpp:132-135）。若启动画面覆写了 closeEvent 并 ignore，X 侧点击将关不掉，Qt 侧不会。
3. **`alignment` 只存不用**：`m_alignment` 仅在 init/`showMessage` 写入（XSplashScreen.c:138、:193），绘制固定 `XPainter_drawText(&painter, 8, h - 12, ...)`（XSplashScreen.c:88-89）——全文件无读取 `m_alignment` 的位置（`rg -n "m_alignment" Src/XGui/Widget/XSplashScreen.c` 仅两条写）。头注释 XSplashScreen.h:71-79 把 alignment 描述为生效参数。
4. **`setPixmap` 不居中、不透明位处理**：X 只 `XWidget_resize` 到图尺寸（XSplashScreen.c:174）；Qt 还会 `setAttribute(WA_TranslucentBackground, pixmap.hasAlpha())` 并按屏幕几何居中（qsplashscreen.cpp:254-259）。
5. **结构体成员名与同族不一致**：基类成员叫 `m_class`（XSplashScreen.h:44）而非其余控件惯用的 `m_base`；按 `XWidget*` 强转的代码不受影响（首成员布局不变），但按字段名写代码/工具扫描会漏。
6. **信号函数签名带参但只用于构造载荷**：`XSplashScreen_messageChanged_signal(self, message)` 在发射路径里构造 `XString` 载荷并以 `xsp2_str_args_del` 由信号系统释放（XSplashScreen.c:237-256、:24-51）；`XObject_signalIdOf` 会用 NULL 调用它取真址，此路径下 `self` 为 NULL 直接跳过构造返回标识（XSplashScreen.c:243）。

**测试覆盖**：无 `Test/XGuiTest/XSplashScreenTest.c` 专属套件。间接覆盖：① `xgui_regression_test.c:30965` 一行虚表创建断言（`xtw_expect_class`）；② `xgui_regression_test.c:32134-32173`（`test_splashscreen_contract`，助手 `sp2_expect` 定义于 :32144）**5 条断言**——创建、初始空消息、showMessage 写入、messageChanged 恰一次、clearMessage 清空；**注意该助手只 `fprintf` 不累加失败计数**（:32144-32148 无 `++s_failures`），同文件 `msg_expect` 在 :32176-32184 已改为计数并注明「只打印不计数时本族失败永不进退出门」，`sp2_expect` 尚未同步；③ `Test/XGuiDemo/xgui_demo_apitest_menus.c:2021-2104`（第 10 节）**15 条 `XAPI_EXPECT`**——class_init、初始消息/pixmap、showMessage 往返（含居中对齐变体与 NULL 文本）、clearMessage、finish 后不可见、repaint 不改消息、resize 往返；④ `Test/XGuiDemo/xgui_demo_page_advanced.c` 有 8 处页面级引用（含 `XWindowType_SplashScreen` 创建与截图目验）。

---

## XSplitter

**定位**：分割器容器（页面管理/方向/尺寸意图/折叠/不透明拖动/把手部件/状态序列化）。继承链 `XObject ← XWidget ← XFrame ← XSplitter`（XSplitter.h:60-62、XFrame.h:102-103）。头文件 `Src/XGui/Widget/XSplitter.h`（313 行）。守卫 `XGUI_ON && XWIDGET_ON && XFRAME_ON && XSPLITTER_ON`（XSplitter.h:54；开关 XGuiConfig.h:575-576）。

**生命周期**：`XSplitter_init` 零化 → `XFrame_init` → 挂 vtable → `m_orientation=1`（水平）、`xsplitter_updateSizeHint`、`m_handleWidth=5`（显式非 -1 哨兵，理由见 XSplitter.c:1052-1054）、`m_childrenCollapsible=true`、`m_opaqueResize=true` 且 `m_opaqueResizeSet=0`（getter 回落样式）、sizePolicy 双向 Expanding（XSplitter.c:1042-1067）。`init_2` 转调后再写方向（XSplitter.c:1069-1075）。`VX_splitter_deinit` 置 `m_blockChildAdd=1`、清 `m_rubberBand` 借用位、释放 6 个堆数组（collapsible/sizes/stretch/collapsed/pages/handles）后交 XFrame；**子页与把手随父子链级联析构**（XSplitter.c:828-866）。

**API 面**

类槽与回调型（XSplitter.h:60-62、:123-124）：`EXSplitter_CreateHandle`（枚举值起点 `XCLASS_VTABLE_GET_SIZE(XFrame)`）；`typedef XSplitterHandle* (*EXSplitter_CreateHandle_t)(XSplitter* self, int index);`。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSplitter_class_init(void);` | 建/取类共享虚表 | 同名惯用 | 共享表，禁释放 |
| `void XSplitter_init(XSplitter* self, XWidget* parent, XWidgetFlags flags);` | 初始化（水平/把手宽 5/可折叠/非透明设置位为假） | `QSplitter(QWidget*)` qsplitter.h:30 | parent 借用 |
| `void XSplitter_init_2(XSplitter* self, XOrientation orientation, XWidget* parent, XWidgetFlags flags);` | 带方向初始化 | `QSplitter(Qt::Orientation, QWidget*)` qsplitter.h:31 | parent 借用 |
| `#define XSplitter_create(parent, flags) XSplitter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSplitter* XSplitter_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `#define XSplitter_create_2(orientation, parent, flags) XSplitter_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))` | 方向重载的默认内存类型创建 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XSplitter* XSplitter_create_ex_2(XMemoryType memory, XOrientation orientation, XWidget* parent, XWidgetFlags flags);` | 指定内存类型 + 方向创建 | 同上 | 调用方拥有，须 `XClassDelete` |
| `void XSplitter_addWidget(XSplitter* self, XWidget* widget);` | 追加页（= insertWidget(count)） | `addWidget` qsplitter.h:34 | **页归分割器**（挂父子链） |
| `void XSplitter_insertWidget(XSplitter* self, int index, XWidget* widget);` | 在 index 插入页（护栏内挂父） | `insertWidget` qsplitter.h:35 | **页归分割器** |
| `XWidget* XSplitter_widget(const XSplitter* self, int index);` | 查第 index 页；越界 NULL | `widget` qsplitter.h:63 | 借用禁释放 |
| `int XSplitter_count(const XSplitter* self);` | 页数（NULL 返回 0） | `count` qsplitter.h:64 | 值返回 |
| `int XSplitter_indexOf(const XSplitter* self, const XWidget* widget);` | 页/把手反查（把手命中返回其分隔点号） | `indexOf` qsplitter.h:62（Qt 同样含把手，qsplitter.cpp:1183-1186） | 值返回 |
| `XWidget* XSplitter_replaceWidget(XSplitter* self, int index, XWidget* widget);` | 替换页并返回被替换旧页（旧页隐藏、不销毁） | `replaceWidget` qsplitter.h:36 | **新页归分割器；旧页交还调用方** |
| `XSplitterHandle* XSplitter_handle(const XSplitter* self, int index);` | 查分隔点把手部件（真实子件，几何经 `XWidget_geometry` 读回） | `handle` qsplitter.h:67 | 借用禁释放（父链所有） |
| `bool XSplitter_getRange(const XSplitter* self, int index, int* min, int* max);` | 查近区间（含把手宽修正）；index 越界/页数<2 返回 false | `getRange` qsplitter.h:66（Qt 返回 void） | 出参写调用方栈 |
| `void XSplitter_setOrientation(XSplitter* self, XOrientation orientation);` | 设方向并遍历把手传播 | `setOrientation` qsplitter.h:38 | — |
| `XOrientation XSplitter_orientation(const XSplitter* self);` | 查方向 | `orientation` qsplitter.h:39 | 值返回 |
| `void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible);` | 设全局可折叠 | `setChildrenCollapsible` qsplitter.h:41 | — |
| `bool XSplitter_childrenCollapsible(const XSplitter* self);` | 查全局可折叠 | `childrenCollapsible` qsplitter.h:42 | 值返回 |
| `void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible);` | 逐页覆写可折叠（越界告警早退） | `setCollapsible` qsplitter.h:44 | — |
| `bool XSplitter_isCollapsible(const XSplitter* self, int index);` | 逐页可折叠（未覆写回落全局） | `isCollapsible` qsplitter.h:45 | 值返回 |
| `void XSplitter_setOpaqueResize(XSplitter* self, bool opaque);` | 设不透明拖动并置「已显式设置」位 | `setOpaqueResize` qsplitter.h:46 | — |
| `bool XSplitter_opaqueResize(const XSplitter* self);` | 查不透明拖动（未显式设置时回落 `SH_Splitter_OpaqueResize`） | `opaqueResize` qsplitter.h:47（Qt 回落到样式，qsplitter.cpp:1472-1484） | 值返回 |
| `int XSplitter_handleWidth(const XSplitter* self);` | 查把手宽（-1 哨兵回落 `PM_SplitterWidth`） | `handleWidth` qsplitter.h:59（qsplitter.cpp:1614-1622） | 值返回 |
| `void XSplitter_setHandleWidth(XSplitter* self, int width);` | 设把手宽（< -1 拒绝；-1=恢复样式回落） | `setHandleWidth` qsplitter.h:60 | — |
| `void XSplitter_moveSplitter(XSplitter* self, int pos, int index);` | 把分隔点 index 移到 pos（迟滞吸附 + 按页分配 + 发 splitterMoved） | `moveSplitter` qsplitter.h:83（protected；X 公开化） | — |
| `void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch);` | 设拉伸因子（重排后按权重分配余量） | `setStretchFactor` qsplitter.h:69 | — |
| `void XSplitter_refresh(XSplitter* self);` | 按当前页尺寸重算布局 | `refresh` qsplitter.h:48 | — |
| `XSize XSplitter_sizeHint(const XSplitter* self);` | 尺寸提示（Σ可见页 sizeHint + 可见把手宽） | `sizeHint` qsplitter.h:50 | 值返回 |
| `XSize XSplitter_minimumSizeHint(const XSplitter* self);` | 最小尺寸提示（Σ smartMin + 把手宽） | `minimumSizeHint` qsplitter.h:51 | 值返回 |
| `void XSplitter_sizes(const XSplitter* self, int* outSizes, int count);` | 写出各页当前尺寸（count 超页数完全不写、不足只写 count 个） | `sizes` qsplitter.h:53（Qt 返回 `QList<int>`） | 出参写调用方数组 |
| `void XSplitter_setSizes(XSplitter* self, const int* sizes, int count);` | 设各页尺寸意图（0=折叠/钳 smartMin 分支） | `setSizes` qsplitter.h:54 | 入参借用 |
| `XByteArray* XSplitter_saveState(const XSplitter* self);` | 序列化方向/页数/把手宽/可折叠/逐页尺寸 | `saveState` qsplitter.h:56 | **调用方拥有**（`XClassDelete`） |
| `bool XSplitter_restoreState(XSplitter* self, const XByteArray* state);` | 恢复布局（按实际页数钳位消费；非法返回 false） | `restoreState` qsplitter.h:57 | state 借用 |
| `void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index);` | splitterMoved(pos,index) 信号标识 | `splitterMoved` qsplitter.h:72 | 不透明标识 |
| `void* XSplitter_handleClicked_signal(XSplitter* self, int index, int doubleClick);` | 把手空闲点击（doubleClick=0）/双击（=1）信号 | **X 扩展**（Qt 把手无双击） | 不透明标识 |

**所有权与命名契约**：查询取 `const XSplitter*`；出参数组由调用方提供（`sizes`/`getRange`）。`addWidget`/`insertWidget`/`replaceWidget` 的**新页归分割器**（Qt 同语义，头注释 XSplitter.h:32-33 明示），`replaceWidget` 的旧页**交还调用方且已隐藏**（XSplitter.h:177-182）。`saveState` 返回堆对象须释放。`m_pages`/`m_handles`/`m_rubberBand` 为分割器自有（deinit 释放 6 个数组，橡皮筋为子件，XSplitter.c:828-866）。`_2` 用于方向重载（`init_2`/`create_2`/`create_ex_2`）；无 `_utf8`（本类无字符串 API）；三态 `set*`：无（`setHandleWidth` 的 -1 是样式哨兵值而非三态语义）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSplitter)` + `XCLASS_DEFINE_ENUM(XSplitter, CreateHandle) = XCLASS_VTABLE_GET_SIZE(XFrame)` + `XCLASS_DEFINE_END(XSplitter)`（XSplitter.h:60-62）——**新增 1 个槽**：`CreateHandle`。默认实现 `VX_splitter_createHandle` = `XSplitterHandle_create_ex(...)` 并回写 `m_index`（XSplitter.c:870-877）。其余重载（XSplitter.c:895-904）：`EXWidget_SizeHint`/`MinimumSizeHint`→转调公开查询函数、`EXObject_Event`→事件总入口（CHILD_ADDED/REMOVED 维护页/把手槽、LAYOUT_REQUEST 重排且 `m_inLayout` 防重入）、`EXWidget_ResizeEvent`→重排、`EXWidget_ChangeEvent`→样式变化时刷新把手尺寸（对标 `qsplitter.cpp:1634-1640`）、`EXClass_Deinit`→释放数组。`_base` 入口：本类无 `*_base` 公开函数（尺寸/事件全走 XWidget 家族 `XWidget_sizeHint_base` 等）。

**注意事项与坑点**

1. **头注释声称「XWidget vtable 暂无 SizeHint 槽」与实现矛盾**：XSplitter.h:261-265 写「XWidget vtable 暂无 SizeHint 槽（missingPlan#4 登记待框架级落地），先行以直接查询函数暴露」；但 `EXWidget_SizeHint`/`EXWidget_MinimumSizeHint` 早已存在（XWidget.h:556、XWidget.h:561-562），且本类自己已挂到这两槽（XSplitter.c:897-898）。另外 `missingPlan#4` 在 XGui.md 中 `rg` 无命中，登记出处无法核对。
2. **`sizes` 的形状与 Qt 不同**：头注释 XSplitter.h:272-273 写「对标 QSplitter::sizes(int*,int)」，但 Qt 6.8.3 只有 `QList<int> sizes() const`（qsplitter.h:53），**不存在（int*,int）重载**。X 侧改成出参 + count 并规定「count 超页数完全不写」（XSplitter.c:1416-1419）。
3. **`saveState` 少写两个字段（本文核实为真偏差）**：X 只写 `"XSP%03d%03d%03d%d"` 头（方向/页数/把手宽/可折叠）+ 每页 5 字符尺寸（XSplitter.c:1505-1519）；Qt 的 `saveState` 还写 `opaqueResize()`（qsplitter.cpp:1675）与 `d->opaqueResizeSet`（qsplitter.cpp:1677）。头注释 XSplitter.h:282-284 已自认「不落 opaqueResize/opaqueResizeSet 两字段（登记偏差）」，但 `restoreState` 也无对应读取（XSplitter.c:1522-1586）——跨版本快照互通会丢这两项。两格式本身不兼容（X 用 ASCII 头，Qt 用 QDataStream + `SplitterMagic=0xff`，qsplitter.cpp:1642、:1663）。
4. **`handleWidth` 默认 5 而非 Qt 的 -1 哨兵**：字段注释写「-1=样式哨兵…构造显式 5」（XSplitter.h:106-107，实现 XSplitter.c:1055），源码给出理由「默认样式 `PM_SplitterWidth` 非 5：Fusion=dpiScaled4/XCommonStyle=0，照 Qt 哨兵默认会丢把手宽」（XSplitter.c:1052-1054）。因此 `XSplitter_handleWidth` 默认返回 5，Qt 同平台默认不是 5。
5. **`setHandleWidth` 拒绝 < -1，但 `restoreState` 把 <-1 钳成 0**：setter 早退（XSplitter.c:1322-1327），restoreState 里 `if (handleWidth < -1) handleWidth = 0;`（XSplitter.c:1553）——从损坏快照恢复会得到 0 宽把手而不是保持原值，两条入口口径不一致。
6. **`orientation` 的 `XOrientation` 取自 `XGridLayout.h`**：头文件为拿 `XOrientation` 直接 `#include "XGridLayout.h"`（XSplitter.h:55-58 注释），该 typedef 在 `XGridLayout.h:82-86` 且位于 `XLAYOUT_GRID` 门内；注释自认「missingPlan#8 登记该 typedef 终将迁 XGuiConfig.h」。在只开分割器不开网格布局的裁剪配置下，此包含链是隐式依赖。

**测试覆盖**：无 `Test/XGuiTest/XSplitterTest.c` 专属套件。间接覆盖：① `xgui_regression_test.c:31453-31525`（`test_splitter_contract`，助手 `sp_expect` 定义于 :31455）**18 条断言**——虚表创建（:31464 前一行）、默认水平/可折叠/不透明/把手宽 5/初始页数 0（:31470-31474）、addWidget 与 widget/indexOf（:31476-31482）、方向切换（:31484-31486）、handleWidth 往返（:31488-31490）、逐页与全局折叠覆写（:31492-31496）、setSizes 比例生效（:31500-31505）、saveState/restoreState 页数钳位语义（:31507-31520）；② `Test/XGuiDemo/xgui_demo_apitest_containers.c` 第 1460-1858 行 XSplitter 段 **57 条 `XAPI_EXPECT`**——refresh 均分（:1560-1563）、handle 几何/越界/负索引（:1566-1581）、getRange 上下限与 smartMin 贡献（:1580-1600）、隐藏页条带保留（:1810-1845）、NULL 口径（:1845-1848）；③ `xgui_regression_test.c:30949` 虚表断言。构建注册同 `XGuiRegression_Test`（`Test/CMakeLists.txt:552`、:563-564）。

---

## XSplitterHandle

**定位**：分割器把手部件（真实子控件，占据分隔点两侧页之间的条带；拖动落位、非透明拖动呈现橡皮筋、空闲点击/双击发信号）。继承链 `XObject ← XWidget ← XSplitterHandle`（XSplitterHandle.h:53-54）。头文件 `Src/XGui/Widget/XSplitterHandle.h`（158 行）。守卫 `XWIDGET_ON && XFRAME_ON && XSPLITTER_ON`（XSplitterHandle.h:51）——**无 `XGUI_ON` 外层包裹**（文件 :38-49 直接从 include guard 进 `extern "C"`），实现文件同样无 `#if XGUI_ON`（XSplitterHandle.c:9 起即为 `#include`）。

**生命周期**：`XSplitterHandle_init` 零化 → `XWidget_init(&m_base, (XWidget*)parent, flags)`（挂入分割器子链，触发父侧 CHILD_ADDED，由分割器的 `m_blockChildAdd` 护栏吞掉）→ 挂 vtable → `m_splitter=parent`、`m_orient=orientation`、`m_index=-1`、应用方向光标（XSplitterHandle.c:313-328）。`m_index` **不在此设置**，由 `EXSplitter_CreateHandle` 工厂回写（XSplitterHandle.h:94-96、XSplitter.c:870-877）。无自有 Deinit 槽；父链级联析构是唯一所有权，外部拆卸走 `XObject_deleteLater`（XSplitterHandle.h:33-35）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XSplitterHandle_class_init(void);` | 建/取类共享虚表 | 同名惯用 | 共享表，禁释放 |
| `void XSplitterHandle_init(XSplitterHandle* self, XOrientation orientation, XSplitter* parent, XWidgetFlags flags);` | 初始化并挂入分割器子链、设方向光标；不置 `m_index` | `QSplitterHandle(Qt::Orientation, QSplitter*)` qsplitter.h:101 | parent 借用（不持有） |
| `XSplitterHandle* XSplitterHandle_create_ex(XMemoryType memory, XOrientation orientation, XSplitter* parent, XWidgetFlags flags);` | 指定内存类型建堆对象（无 `_create` 便捷宏） | `new QSplitterHandle` | 调用方拥有，须 `XClassDelete` |
| `void XSplitterHandle_setOrientation(XSplitterHandle* self, XOrientation orientation);` | 设方向 + 换方向光标 + 重绘（相同值早退） | `setOrientation` qsplitter.h:104 | — |
| `XOrientation XSplitterHandle_orientation(const XSplitterHandle* self);` | 查方向（NULL 返回 Horizontal，原始成员值） | `orientation` qsplitter.h:105 | 值返回 |
| `XSplitter* XSplitterHandle_splitter(const XSplitterHandle* self);` | 查所属分割器（无主 NULL） | `splitter` qsplitter.h:107 | **借用禁释放** |
| `bool XSplitterHandle_opaqueResize(const XSplitterHandle* self);` | 借分割器 getter 查不透明拖动（含样式回落） | `opaqueResize` qsplitter.h:106 | 值返回 |
| `int XSplitterHandle_splitterIndex(const XSplitterHandle* self);` | 本把手分隔点号（O(1) 读 `m_index`；未挂入返回 -1） | Qt 无公开对应（内部 `indexOf(this)`，qsplitter.cpp:166/:183） | 值返回 |
| `void XSplitterHandle_moveSplitter(XSplitterHandle* self, int position, int index);` | 把分隔点移到 position（经分割器迟滞夹取并落位） | `moveSplitter(int)` qsplitter.h:119（protected 槽；X 公开且多一个 index 形参） | — |
| `int XSplitterHandle_closestLegalPosition(const XSplitterHandle* self, int position);` | position 就近合法位置（按 `getRange` 近区间夹取） | `closestLegalPosition(int)` qsplitter.h:120（protected） | 值返回 |
| `XSize XSplitterHandle_sizeHint(const XSplitterHandle* self);` | 把手尺寸提示（`CT_Splitter` + 内容 hw×hw） | `sizeHint` qsplitter.h:109（qsplitter.cpp:191-199） | 值返回 |
| `void xsp_setRubberBand(XSplitter* self, int pos);` | 内部共享入口（分割器实现、把手调用）：pos<0 摘除橡皮筋，否则创建/复用并定位 | `setRubberBand` qsplitter.h:84（protected，qsplitter.cpp:1301-1327） | 橡皮筋归分割器（子件） |

**所有权与命名契约**：查询取 `const XSplitterHandle*`（`orientation`/`splitter`/`opaqueResize`/`splitterIndex`/`closestLegalPosition`/`sizeHint`）；`splitter` 为借用（结构体注释 XSplitterHandle.h:66「借用不持有」）。无 `_utf8`、无 `_2` 重载。三态 `set*`：无（`setOrientation` 单值）。`moveSplitter` 的 `index` 形参**仅为对齐 Qt 槽签名，实现恒用 `m_index`**（XSplitterHandle.h:128、XSplitterHandle.c:399）。`xsp_setRubberBand` 前缀 `xsp_` 标明它是跨文件内部共享而非稳定公共 API（XSplitterHandle.h:143-151）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XSplitterHandle)` + `XCLASS_DEFINE_EXTEND_END(XSplitterHandle, XWidget)`（XSplitterHandle.h:53-54），**不新增槽位**。重载 10 槽（XSplitterHandle.c:291-310）：`EXWidget_SizeHint`→`VXSplitterHandle_sizeHintSlot`（转调公开 `XSplitterHandle_sizeHint`）、`EXWidget_MinimumSizeHint`→`XWidget_minimumSizeHint_base`（**直接回落基类，不按把手宽算**）、`EXWidget_PaintEvent`→`CE_Splitter` + Enabled/MouseOver/Sunken、`EnterEvent`/`LeaveEvent`→刷 `m_hover` 并重绘、`MousePressEvent`→记偏移/按压位/抓取、`MouseMoveEvent`→opaque 走 `moveSplitter`、非 opaque 记 `m_dragTarget` + `xsp_setRubberBand`、`MouseReleaseEvent`→落位/摘橡皮筋/≤2px 判空闲点击发 `handleClicked(index,0)`、`MouseDoubleClickEvent`→发 `handleClicked(index,1)`（X 扩展）。`resizeEvent` 刻意不重载（XSplitterHandle.c:308-309 注释：Qt tiny-mode `setMask` 在 X 无对应物）。无 `_base` 入口。

**注意事项与坑点**

1. **`m_index` 依赖工厂回写，独立 `create_ex` 得到 -1**：`init` 不置 `m_index`（XSplitterHandle.h:94-96；实现 XSplitterHandle.c:326 `m_index = -1`），只有 `VX_splitter_createHandle` 会回写（XSplitter.c:877-879）。自己 `XSplitterHandle_create_ex` 出来的把手，`splitterIndex()` 返回 -1，`moveSplitter`/`closestLegalPosition` 直接早退不动作（XSplitterHandle.c:388-401）。
2. **`minimumSizeHint` 槽与 `sizeHint` 不同源**：`sizeHint` 走 `CT_Splitter` 样式计算（XSplitterHandle.c:406-438），而 `minimumSizeHint` 槽直接 `return XWidget_minimumSizeHint_base(self)`（XSplitterHandle.c:286-289）——即最小提示不含把手宽，二者可能相差一个 handleWidth。
3. **无 `XGUI_ON` 裁剪包裹（与同族 8 个头文件不一致）**：本头从 `#ifndef XSPLITTERHANDLE_H` 直接进 `#ifdef __cplusplus`（XSplitterHandle.h:38-44），`.c` 也从 `#include` 起（XSplitterHandle.c:9），全文件无 `#if XGUI_ON`（`rg -n "XGUI_ON" Src/XGui/Widget/XSplitterHandle.h` 无命中）。其余 8 个本批头文件均有该包裹（如 XSplitter.h:41、XStackedWidget.h:22）。`XGUI_ON=0` 全裁剪构建时本类仍按 `XWIDGET_ON && XFRAME_ON && XSPLITTER_ON` 展开。
4. **无 `_create` 便捷宏**：只有 `XSplitterHandle_create_ex`（XSplitterHandle.h:102-105），`rg` 全头无 `#define XSplitterHandle_create(`。按库内创建惯例（`XType_create`/`XType_create_ex` 成对）本类缺默认内存类型入口，调用方须自己写 `XCLASS_DEFAULT_MEMORY_TYPE`。
5. **把手坐标字段是 `int16_t`**：`m_mouseOffset`/`m_pressPick`/`m_index`/`m_dragTarget` 均为 `int16_t`（XSplitterHandle.h:67-75），`moveSplitter` 的 `target` 由 `XWidget_x/y + pick − m_mouseOffset` 得到后写入 `m_dragTarget`（XSplitterHandle.c:202-208）。条带坐标超出 ±32767 的巨型画布上该字段会截断（类型事实；本批次未构造用例验证实际表现）。
6. **非透明拖动的 3px 内缩无对应物**：头注释自认 Qt `setRubberBand(pos, rBord=3)` 的「3px 内缩在 XRubberBand 自绘路径无对应物，登记」（XSplitterHandle.h:145-150）；X 侧按 `pos` 与有效把手宽直接铺满条带（XSplitter.c:699-708）。
7. **头注释引用的 `XTableWidget.c:206` 指错位置**：XSplitterHandle.h:34 写「外部拆卸走 `XObject_deleteLater`（定式 XTableWidget.c:206）」，但 XTableWidget.c:206 实际是 `XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTableWidgetItem_copy);`；该文件的 `XObject_deleteLater` 调用在 :1130、:1147、:1198、:1213、:1226（`rg -n "XObject_deleteLater" Src/XGui/Widget/XTableWidget.c`）。作为「定式」的出处不可核对。

**测试覆盖**：无 `Test/XGuiTest/XSplitterHandleTest.c` 专属套件。间接覆盖（均以 `XSplitter_handle()` 取回把手后验几何，**未直接调用本类任何公开函数**——`rg -n "XSplitterHandle_(setOrientation|orientation|splitter|opaqueResize|splitterIndex|moveSplitter|closestLegalPosition|sizeHint)" Test/ Src/ --glob '!XSplitterHandle.*'` 仅命中 XSplitter.c:1247 的方向传播调用）：`Test/XGuiDemo/xgui_demo_apitest_containers.c:1566-1581`（handle 位于页 0 几何之后、宽=handleWidth、越界/负索引 NULL）、:1810-1845（隐藏页条带位置）；`Test/XGuiDemo/xgui_demo_page_remote_client.c:961`、`Test/XGuiDemo/xgui_window_demo.c:3423` 为页面级使用。**本类无一条专属断言**。

---

## XStackedWidget

**定位**：堆叠页面容器（内嵌 `XStackedLayout` 承载页面，暴露页管理与当前页槽 + 两个信号）。继承链 `XObject ← XWidget ← XFrame ← XStackedWidget`（XStackedWidget.h:38-39）。头文件 `Src/XGui/Widget/XStackedWidget.h`（124 行）。守卫 `XGUI_ON && XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON`（XStackedWidget.h:36；开关 XGuiConfig.h:563-564），`XStackedLayout.h` 的包含另受 `XLAYOUT_ON && XLAYOUT_STACKED_ON`（:32-34）。

**生命周期**：`XStackedWidget_init` 零化 → `XFrame_init` → 挂 vtable → `XStackedLayout_init(&self->m_layout)`（**值成员，非堆**）→ `XWidget_setLayout((XWidget*)self, (XLayout*)&self->m_layout)`（借用挂接路径，不登记所有权）（XStackedWidget.c:37-53）。`VX_stackedWidget_deinit` 先 `XClassDeinit(&self->m_layout)` 再交 XFrame（XStackedWidget.c:56-61）；`VXLayout_deinit` 会反向清宿主 `m_layout` 槽位（XLayout.c:530-545），故 XWidget 侧的布局接管账无残留。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XStackedWidget_class_init(void);` | 建/取类共享虚表（仅重载 Deinit） | 同名惯用 | 共享表，禁释放 |
| `void XStackedWidget_init(XStackedWidget* self, XWidget* parent, XWidgetFlags flags);` | 初始化并挂内嵌堆叠布局 | `QStackedWidget(QWidget*)` qstackedwidget.h:23 | parent 借用 |
| `#define XStackedWidget_create(parent, flags) XStackedWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XStackedWidget* XStackedWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `int XStackedWidget_addWidget(XStackedWidget* self, XWidget* widget);` | 追加页返回索引（空容器首件发 currentChanged） | `addWidget` qstackedwidget.h:26 | **页归容器**（布局挂父） |
| `int XStackedWidget_insertWidget(XStackedWidget* self, int index, XWidget* widget);` | 在 index 插入并返回实际索引（越界等价追加） | `insertWidget` qstackedwidget.h:27 | **页归容器** |
| `void XStackedWidget_removeWidget(XStackedWidget* self, XWidget* widget);` | 移除页并发 widgetRemoved | `removeWidget` qstackedwidget.h:28 | **页归还调用方**（hide + parent=NULL） |
| `int XStackedWidget_currentIndex(const XStackedWidget* self);` | 当前页索引（无页 -1） | `currentIndex` qstackedwidget.h:31 | 值返回 |
| `XWidget* XStackedWidget_currentWidget(const XStackedWidget* self);` | 当前页控件（无页 NULL） | `currentWidget` qstackedwidget.h:30 | 借用禁释放 |
| `int XStackedWidget_indexOf(const XStackedWidget* self, const XWidget* widget);` | 页反查（未找到 -1） | `indexOf` qstackedwidget.h:33 | 值返回 |
| `XWidget* XStackedWidget_widget(const XStackedWidget* self, int index);` | 按索引取页（越界 NULL） | `widget` qstackedwidget.h:34 | 借用禁释放 |
| `int XStackedWidget_count(const XStackedWidget* self);` | 页数（NULL 返回 0） | `count` qstackedwidget.h:35 | 值返回 |
| `void XStackedWidget_setCurrentIndex(XStackedWidget* self, int index);` | 设当前页（相同索引早退；**越界不切换但仍发信号**） | `setCurrentIndex` qstackedwidget.h:38 | — |
| `void XStackedWidget_setCurrentWidget(XStackedWidget* self, XWidget* widget);` | 设指定控件为当前页（非本容器页忽略） | `setCurrentWidget` qstackedwidget.h:39 | widget 借用 |
| `void* XStackedWidget_currentChanged_signal(XStackedWidget* self, int index);` | currentChanged(int) 信号标识（容器自行发射） | `currentChanged` qstackedwidget.h:42 | 不透明标识 |
| `void* XStackedWidget_widgetRemoved_signal(XStackedWidget* self, int index);` | widgetRemoved(int) 信号标识 | `widgetRemoved` qstackedwidget.h:43 | 不透明标识 |

**所有权与命名契约**：查询取 `const XStackedWidget*`；`widget`/`currentWidget` 返回借用页指针。`addWidget`/`insertWidget` 后页归容器（布局 `XLayout_linkItem` 会把 child 的 parent 设为容器，XLayout.c:225-251），`removeWidget` 后页**归还调用方**（`XWidget_setVisible(page,false)` + `XWidget_setParent_2(page,NULL)`，XStackedLayout.c:383-388）。`m_layout` 为值成员、归容器（deinit 释放，XStackedWidget.c:56-61）。无 `_utf8`、无 `_2`、无三态 `set*`。

**可覆写点**：`XCLASS_DEFINE_BEGING(XStackedWidget)` + `XCLASS_DEFINE_EXTEND_END(XStackedWidget, XFrame)`（XStackedWidget.h:38-39），**不新增槽位**；`class_init` 只重载 `EXClass_Deinit`→`VX_stackedWidget_deinit`（先析构内嵌布局再交 XFrame，XStackedWidget.c:29-35、:56-61）。**未注册 `EXClass_Copy`/`EXClass_Move`**（对比同类 XSpinBox.c:988-989 注册了 Copy/Move）。无 `_base` 入口；两个信号函数既是发射器又是标识（返回自身函数地址，XStackedWidget.c:167-197）。

**注意事项与坑点**

1. **头注释说「转发内部 XStackedLayout 信号」，实现是自行发射**：XStackedWidget.h:9-10 写「信号：currentChanged(int)/widgetRemoved(int)（转发内部 XStackedLayout 信号）」，但 `XStackedLayout` 继承链不含 XObject、无信号槽（XStackedWidget.c:49-52 注释明说「不能作为信号发送方连接」「由本控件在 setCurrentIndex/removeWidget 操作路径自行发射」），XGui.md:1589 亦记「XStackedLayout 不发 currentChanged（信号所有权在 XStackedWidget）」。
2. **`setCurrentIndex` 越界仍发 currentChanged**：头注释写「越界忽略」（XStackedWidget.h:97），实现只在 `old == index` 早退，之后无条件 `XStackedWidget_currentChanged_signal(self, index)`（XStackedWidget.c:146-154）；越界时 `XStackedLayout_setCurrentIndex` 因 `next==NULL` 直接返回（XStackedLayout.c:400），当前页不变，但信号已带一个不存在的索引发出。Qt 的 `QStackedLayout::setCurrentIndex` 在 `!next` 时直接 return、不发射（qstackedlayout.cpp:266-268）。
3. **移除当前页不会补发 currentChanged**：`removeWidget` 只发 `widgetRemoved(index)`（XStackedWidget.c:111-119）；实际切页由 `VXStackedLayout_takeAt` 内部调 `XStackedLayout_setCurrentIndex` 完成，该函数不发任何信号（XStackedLayout.c:63-90、:393-421）。Qt 侧 `takeAt` 在移除当前项时会 `setCurrentIndex(newIndex)`（→ `currentChanged`）或 `emit currentChanged(-1)`（qstackedlayout.cpp:237-244）。依赖「删当前页后收到 currentChanged」的逻辑在 X 侧收不到。
4. **不要再对本容器调用 `XWidget_setLayout`**：`m_layout` 是值成员且已在 init 挂接（XStackedWidget.c:47-48）；`XWidget_setLayout` 对「已挂其他布局」按「告警并保留首个」早退（XWidget.c:10486-10489），换布局不会生效也不改变 `m_layout` 的归属。
5. **`addWidget` 只在「空容器首件」发 currentChanged**：实现比较 `old<0 && cur>=0`（XStackedWidget.c:88-91），追加不会迁移当前页；`insertWidget` 前插导致当前页序号后移时也**不**发信号（XStackedWidget.c:103-107 注释对齐 Qt `d->index++` 直改不发）。
6. **未注册 Copy/Move 槽**：`class_init` 只有 `EXClass_Deinit`（XStackedWidget.c:29-35）。结构体含值成员 `XStackedLayout m_layout`（XStackedWidget.h:47），其内部有堆数组指针；`XClassCopy` 走默认拷贝时该内嵌布局的指针字段会按字节复制（默认拷贝语义见 XClass.c:22 `XClass_copy_base`），是否造成共享/双释放**本批次未构造用例验证**，但缺少 Copy/Move 覆写本身与同族（XSpinBox/XSlider/XSplitterHandle 等）不一致。

**测试覆盖**：有专属套件 `Test/XGuiTest/XStackedWidgetTest.c`（118 行）+ `XStackedWidgetTest.h`，入口 `bool XStackedWidgetTest_runAll(void)`（XStackedWidgetTest.c:38；模块关闭分支 :113）。断言助手 `sw_expect`（:15）共 **20 个调用点**：创建与初始态（:45-50）、addWidget/insertWidget 返回索引与 widget/indexOf（:53-66）、setCurrentIndex + currentWidget + currentChanged 恰一次（:74-77）、setCurrentWidget（:80）、removeWidget 后 count/indexOf/widgetRemoved 与页数（:88-92）。构建注册：`Test/CMakeLists.txt:564`。
**套件文件本身有一处结构异常（未做编译验证）**：第 46 行 `if (sw_failures == 0) {` 之后到文件末尾多出一个 `}`（:111，紧跟函数体 `}` :110），本次用 Python 逐字符扫描（忽略字符串/注释）得花括号净计数为 0，但缩进/嵌套次序可疑；我**没有构建该目标**，故不下「能编译/不能编译」的结论。
# 控件文档片段 · 批次 08

本批覆盖 `Src/XGui/Widget/` 下 9 个控件类。所有签名逐字抄自头文件（行号可核对）；实现行号取自同目录 `.c`。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/`，按真实路径标注：`qtableview.h` / `qtablewidget.h` 在 `widgets/itemviews/`，`qtextdocument.h` 在 `gui/text/`，其余在 `widgets/widgets/`；`QTimeEdit` 无独立头文件，定义在 `widgets/widgets/qdatetimeedit.h:181-192`。
**编译核实**（本批实测）：9 个头文件均以 `gcc -std=c11 -fsyntax-only`（项目 `build/compile_commands.json` 抽出的 `-D`/`-I` 全量旗标）通过、0 error；`g++ -std=c++17 -fsyntax-only` 除 `XTabWidget.h`/`XTextDocument.h` 外均因 `XWidget.h` 未闭合 `extern "C" {` 报 `expected '}' at end of input`，逐类在坑点节记录所需补数。

---

## XStatusBar

**定位**：状态栏控件——临时消息（带超时定时器）+ 普通区/永久区两组常驻控件 + 右下尺寸手柄占位；绘制为底色 + 顶部 1px 分隔线 + 左对齐消息文本。继承链 `XObject ← XWidget ← XStatusBar`（`XCLASS_DEFINE_BEGING(XStatusBar)` XStatusBar.h:38、`XCLASS_DEFINE_EXTEND_END(XStatusBar, XWidget)` XStatusBar.h:39）。头文件 `Src/XGui/Widget/XStatusBar.h`（**151 行**）。守卫 `XWIDGET_ON && XSTATUSBAR_ON`（XStatusBar.h:36）；`XSTATUSBAR_ON` 缺省 1（XGuiConfig.h:569-571），但 `!XWIDGET_ON || !XGUIAPPLICATION_ON || !XWINDOW_ON` 时被强制置 0（XGuiConfig.h:881、894-895）。

**生命周期**：`XStatusBar_init` 全零后 `XWidget_init`、挂本类虚表、建 `m_items`/`m_permanents` 两个 `XVector`、`m_sizeGripEnabled=true`、`m_messageTimer=XTIMER_INVALID_ID`，末尾 `XWidget_resize(self, 200, 24)`（XStatusBar.c:303-318）；`VXStatusBar_deinit` 释放 `m_currentMessage` 与两个条目向量（XStatusBar.c:268-281）后转父类 deinit；栈对象与 `XClassDeinit` 成对、堆对象经 `_create`/`_create_ex` 后 `XClassDelete`，其余遵循基类 XWidget 约定。

**API 面**（按头文件 `/* ==== */` 分区；Qt 行号取 `qstatusbar.h`）

### 生命周期（XStatusBar.h:58-70）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XStatusBar_class_init(void);` | 建/取共享虚表（继承 XWidget，登记 4 槽） | 无公开对应 | 共享表，禁释放 |
| `void XStatusBar_init(XStatusBar* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（默认 200×24、sizeGrip 开、无消息） | `QStatusBar(QWidget*)` qstatusbar.h:23 | parent 借用 |
| `#define XStatusBar_create(parent, flags) XStatusBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |
| `XStatusBar* XStatusBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |

### 常驻控件（XStatusBar.h:72-101）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XStatusBar_addWidget(XStatusBar* self, XWidget* widget, int stretch);` | 追加普通区（消息在场时自动隐藏；加入即 reparent 并排版） | `addWidget` qstatusbar.h:26 | widget 借用禁释放（`removeWidget` 摘父归还） |
| `int XStatusBar_insertWidget(XStatusBar* self, int index, XWidget* widget, int stretch);` | 在 index 处插入普通区，返回实际索引（越界追加） | `insertWidget` qstatusbar.h:27 | 同上 |
| `void XStatusBar_addPermanentWidget(XStatusBar* self, XWidget* widget, int stretch);` | 追加永久区（显式 show，永不被消息遮挡） | `addPermanentWidget` qstatusbar.h:28 | 借用禁释放 |
| `int XStatusBar_insertPermanentWidget(XStatusBar* self, int index, XWidget* widget, int stretch);` | 在 index 处插入永久区，返回实际索引 | `insertPermanentWidget` qstatusbar.h:29 | 借用禁释放 |
| `void XStatusBar_removeWidget(XStatusBar* self, XWidget* widget);` | 两区都查找并移除；`XWidget_setParent_2(widget,NULL)` 摘父归还调用方，不销毁 | `removeWidget` qstatusbar.h:30 | 归还借用方 |

### 尺寸手柄（XStatusBar.h:103-114）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XStatusBar_isSizeGripEnabled(const XStatusBar* self);` | 查手柄开关（NULL 返回 false） | `isSizeGripEnabled` qstatusbar.h:33 | — |
| `void XStatusBar_setSizeGripEnabled(XStatusBar* self, bool on);` | 置开关 → 重排条目（右侧预留 16px）+ 重绘 | `setSizeGripEnabled` qstatusbar.h:32 | — |

### 消息槽（XStatusBar.h:116-134）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XStatusBar_showMessage(XStatusBar* self, const char* utf8, int timeout);` | 显示临时消息；timeout>0 起/重启定时器，<=0 取消定时器；同文早退不重发信号 | `showMessage` qstatusbar.h:38 | utf8 借用 |
| `void XStatusBar_clearMessage(XStatusBar* self);` | 杀定时器、清消息、恢复被消息致隐的普通区控件、**无条件**发 `messageChanged("")` | `clearMessage` qstatusbar.h:39 | — |
| `const char* XStatusBar_currentMessage(const XStatusBar* self);` | 当前消息 UTF-8 借用；无消息返回 `""` | `currentMessage` qstatusbar.h:35 | **借用禁释放**（XStatusBar.c:535-541） |

### 信号（XStatusBar.h:136-142）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XStatusBar_messageChanged_signal(XStatusBar* self, const char* text);` | 消息变化（含清除）时真发射，载荷为堆拷贝 `XString*`（槽内借用） | `messageChanged` qstatusbar.h:43 | 返回值仅为信号标识，不得解引用/释放 |

**所有权与命名契约**：`_const` 借用族＝`currentMessage`（内部缓存借用）、`isSizeGripEnabled`（const self）；`_utf8` 后缀族**不存在**——`showMessage` 直接以 `const char* utf8` 命名参数收 UTF-8（XStatusBar.h:123），属 §8.2.1「全库 `_2` → `_utf8`」批次前的旧口径（XGui.md:1813）；`_2` 重载无；三态 `set*` 无（`setSizeGripEnabled` 单值 bool）。入参所有权：`add*/insert*` 把 widget reparent 到本控件但不拥有（XStatusBar.c:343、365、388、411），`removeWidget` 摘父归还。

**可覆写点**：`class_init` 仅 4 槽重载——`EXWidget_PaintEvent → VX_statusBar_paintEvent`（底色 + 1px 分隔线 + 消息文本，XStatusBar.c:188-238）、`EXWidget_ResizeEvent → VX_statusBar_resizeEvent`（先父类再 `xsb_layoutItems` 重排，XStatusBar.c:283-290）、`EXObject_TimerEvent → VX_statusBar_timerEvent`（超时即 `clearMessage`，XStatusBar.c:240-249）、`EXClass_Deinit → VXStatusBar_deinit`（XStatusBar.c:292-301）。无 `_base` 公开入口（`rg -c "XStatusBar_[A-Za-z]+_base"` = 0）。

**注意事项与坑点**

1. **`stretch` 注释与实现矛盾**：头注释写「追加普通区控件（stretch 语义对标 addWidget，第一版仅存储）」（XStatusBar.h:74），结构体字段注释同样写「拉伸因子（第一版仅存储）」（XStatusBar.c:29）；但实现已按 stretch 分摊弹性宽——`flexible += (*it)->stretch`（XStatusBar.c:105-106）、`iw = avail * (*it)->stretch / flexible`（XStatusBar.c:120-121），并在 `xsb_layoutSection` 头注释里自述该口径（XStatusBar.c:83-94）。注释过时。
2. **`m_tempTimeout` 是只写字段且类型会截断**：声明为 `uint16_t`（XStatusBar.h:52），`showMessage` 的 `timeout` 是 `int` 且原样赋值（XStatusBar.c:506、512）——>65535ms 的超时被静默截断；`rg m_tempTimeout` 全仓仅这 2 处写、**零处读**，实际是死字段。
3. **sizeGrip 只占位不绘制**：头注释称「右下角尺寸手柄占位；手柄绘制为简化实现」（XStatusBar.h:13-14），实现只在排版里预留 `XSTATUSBAR_GRIP_W`=16px 条带（XStatusBar.c:75、152），`VX_statusBar_paintEvent` 全程无手柄图元（XStatusBar.c:188-238）。
4. **同文早退的定时器次序**：`showMessage` 先处理定时器、再比对同文（XStatusBar.c:489-508），注释明写这是缺陷根修——「此前缺 else 分支，`showMessage("a",5000)` 后 `showMessage("b",0)` 仍会在剩余定时内被清空」（XStatusBar.c:489-492）。语义变更点是「timeout<=0 必须取消仍在跑的定时器」。
5. **C++ 包含即编译失败（实测，非注释来源）**：根因在 `XWidget.h:79` 的 `extern "C" {` 无闭合（实测 `#include "Widget/XWidget.h"` 单独编译即报同一错误，提示匹配 `XWidget.h:79`）；本头自己的 `extern "C"`（`:26` 开、`:144-146` 关）是配平的。实测 `g++ -std=c++17 -fsyntax-only $FLAGS t.cpp`（`t.cpp` 仅 `#include "Widget/XStatusBar.h"` + `int main(){return 0;}`，`$FLAGS` 取自 `build/compile_commands.json`）报 `error: expected '}' at end of input`，因两块嵌套，提示指向最内层未闭合的 `XStatusBar.h:26`；补 1 个 `}` 即通过。C11 编译 0 error。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XStatusBarTest.c`）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_menus.c` 33 处 `XStatusBar_*` 调用，含 messageChanged 记录器（同文件 :435-466，槽内以 `XVarList_args_1(args, XString*, str)` 取载荷）与主用例（同文件 :1272-1300：默认 currentMessage 空、sizeGrip 默认开、`showMessage("已保存",0)` 后文本往返）；`Test/XGuiDemo/xgui_demo_apitest_containers.c` 10 处（同文件 :300-310 另一个 messageChanged 槽）；`Test/XGuiDemo/xgui_window_demo.c` 2 处。

---

## XTabBar

**定位**：水平选项卡条——页签增删/文本/启用/可见/图标/提示/帮助/无障碍名/数据/角按钮/文本色、溢出滚动（滚动按钮 + 偏移 + 滚轮 + 按住连发）、拖拽换位、自动隐藏、elide 省略。继承链 `XObject ← XWidget ← XTabBar`（XTabBar.h:29-30）。头文件 `Src/XGui/Widget/XTabBar.h`（**515 行**）。守卫 `XWIDGET_ON && XTABBAR_ON`（XTabBar.h:26），`XTABBAR_ON` 缺省 1（XGuiConfig.h:551-553）。`XTabWidget` 内含一个 `XTabBar` 成员（XTabWidget.h:35）。

**生命周期**：`XTabBar_init` 全零 → `XWidget_init` → 挂虚表 → 各平行数组置 NULL → `m_autoHide=false`、`m_expanding=true`、`m_elideMode=ElideRight`、`m_selectionBehavior=0`、`m_shape=0`、`m_iconSize=0`、`m_usesScrollButtons=true`、`m_scrollOffset=0`（XTabBar.c:1005-1040）；`VXTabBar_deinit` 释放全部平行数组与逐页 `XString` 载荷（XTabBar.c:921 起、`xtabbar_releaseTabs` XTabBar.c:793 起）；栈/堆释放纪律遵循基类 XWidget 约定。`copy`/`move` 均重载（XTabBar.c:766-789、890-915）。

**API 面**（Qt 行号取 `qtabbar.h`）

### 生命周期（XTabBar.h:82-99）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTabBar_class_init(void);` | 建/取共享虚表（登记 13 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTabBar_init(XTabBar* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（0 项、无当前、ElideRight、滚动按钮开） | `QTabBar(QWidget*)` qtabbar.h:39 | parent 借用 |
| `#define XTabBar_create(parent, flags) XTabBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |
| `XTabBar* XTabBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |

### 页签增删与属性（XTabBar.h:101-212）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTabBar_addTab(XTabBar* self, const XString* text);` | 追加页签，返回新索引；首项经 `setCurrentIndex` 走发射路径 | `addTab(const QString&)` qtabbar.h:61 | text 借用 |
| `int XTabBar_addTab_2(XTabBar* self, const char* text);` | UTF-8 重载（NULL 返回 -1） | 同上 | text 借用 |
| `int XTabBar_insertTab(XTabBar* self, int index, const XString* text);` | 插入页签；负/越界一律追加 | `insertTab` qtabbar.h:64 | text 借用 |
| `int XTabBar_insertTab_2(XTabBar* self, int index, const char* text);` | UTF-8 重载（NULL 拒绝，XTabBar.c:1157-1160） | 同上 | text 借用 |
| `void XTabBar_removeTab(XTabBar* self, int index);` | 移除页签；按 `selectionBehaviorOnRemove` 定新当前页；两槽按钮 `XObject_deleteLater` | `removeTab` qtabbar.h:67 | 按钮随本控件父子链级联 |
| `int XTabBar_count(const XTabBar* self);` | 项数（NULL→0） | `count` qtabbar.h:105 | — |
| `int XTabBar_currentIndex(const XTabBar* self);` | 当前项（无→-1） | `currentIndex` qtabbar.h:104 | — |
| `void XTabBar_setCurrentIndex(XTabBar* self, int index);` | 改当前项（同值早退；自动露出 + 发 currentChanged） | `setCurrentIndex` qtabbar.h:149 | — |
| `XString* XTabBar_tabText(const XTabBar* self, int index);` | 读页签文本，**返回新建副本** | `tabText` qtabbar.h:76 | **拷贝须释放**（`XClassDelete`） |
| `const char* XTabBar_tabText_2(const XTabBar* self, int index);` | 读文本 UTF-8 借用；越界返回 `""` | 同上 | 借用禁释放 |
| `void XTabBar_setTabText(XTabBar* self, int index, const XString* text);` | 设文本（NULL 忽略），重绘 + 刷 sizeHint | `setTabText` qtabbar.h:77 | text 借用 |
| `void XTabBar_setTabText_2(XTabBar* self, int index, const char* text);` | UTF-8 重载 | 同上 | text 借用 |
| `bool XTabBar_isTabEnabled(const XTabBar* self, int index);` | 页签启用态 | `isTabEnabled` qtabbar.h:70 | — |
| `void XTabBar_setTabEnabled(XTabBar* self, int index, bool enabled);` | 置启用态 | `setTabEnabled` qtabbar.h:71 | — |
| `bool XTabBar_tabsClosable(const XTabBar* self);` | 可关闭开关 | `tabsClosable` qtabbar.h:119 | — |
| `void XTabBar_setTabsClosable(XTabBar* self, bool closable);` | 置可关闭（命中右侧 12px 关闭区发 tabCloseRequested，XTabBar.c:643-666） | `setTabsClosable` qtabbar.h:120 | — |
| `bool XTabBar_isMovable(const XTabBar* self);` | 可拖动开关 | `isMovable` qtabbar.h:131 | — |
| `void XTabBar_setMovable(XTabBar* self, bool movable);` | 置可拖动（拖拽跨页签即 `moveTab`，XTabBar.c:710-716） | `setMovable` qtabbar.h:132 | — |

### 信号（XTabBar.h:214-227）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTabBar_currentChanged_signal(XTabBar* self, int index);` | currentChanged 标识；`index` 仅占位（实现 `(void)index`，XTabBar.c:1344-1348） | `currentChanged` qtabbar.h:152 | 标识，不得解引用 |
| `void* XTabBar_tabCloseRequested_signal(XTabBar* self);` | 关闭请求标识；真发射点 XTabBar.c:663-666 | `tabCloseRequested` qtabbar.h:153 | 同上 |
| `void* XTabBar_tabBarClicked_signal(XTabBar* self, int index);` | 页签单击；真发射点 XTabBar.c:670 | `tabBarClicked` qtabbar.h:155 | 同上 |
| `void* XTabBar_tabBarDoubleClicked_signal(XTabBar* self, int index);` | 页签双击；真发射点 XTabBar.c:70（双击处理） | `tabBarDoubleClicked` qtabbar.h:156 | 同上 |
| `void* XTabBar_tabMoved_signal(XTabBar* self, int from, int to);` | 页签换位；真发射点 XTabBar.c:1933 | `tabMoved` qtabbar.h:154 | 同上 |

### 外观/几何/项属性（XTabBar.h:229-506，Task 2.2）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTabBar_setDocumentMode(XTabBar* self, bool enable);` / `bool XTabBar_documentMode(const XTabBar* self);` | 文档模式（无边框） | `setDocumentMode`/`documentMode` qtabbar.h:135/134 | — |
| `void XTabBar_setElideMode(XTabBar* self, int mode);` / `int XTabBar_elideMode(const XTabBar* self);` | 省略模式；越界值忽略（XTabBar.c:1429-1438）；NULL 查询返回 ElideRight(1) | `setElideMode`/`elideMode` qtabbar.h:86/85 | — |
| `void XTabBar_setExpanding(XTabBar* self, bool enable);` / `bool XTabBar_expanding(const XTabBar* self);` | 扩展模式（NULL 查询返回 true） | `setExpanding`/`expanding` qtabbar.h:129/128 | — |
| `void XTabBar_setUsesScrollButtons(XTabBar* self, bool enable);` / `bool XTabBar_usesScrollButtons(const XTabBar* self);` | 溢出滚动按钮；关闭时偏移清零回退换行布局 | `setUsesScrollButtons`/`usesScrollButtons` qtabbar.h:117/116 | — |
| `bool XTabBar_isOverflowed(const XTabBar* self);` | 页签总宽是否超条宽（与开关无关） | **X 扩展**（§8.0f，XGui.md:412） | — |
| `int XTabBar_scrollOffset(const XTabBar* self);` | clamp 后滚动偏移 px；非溢出态恒 0 | **X 扩展**（§8.0f） | — |
| `int XTabBar_barHeightHint(const XTabBar* self, int forWidth);` | 供容器按宽预留高度（溢出单行恒 `XTABBAR_TAB_H`，否则按换行行数） | **X 扩展**（§8.0f，替换 XTabWidget 高度漂移副本） | — |
| `void XTabBar_setDrawBase(XTabBar* self, bool enable);` / `bool XTabBar_drawBase(const XTabBar* self);` | 基底绘制 | `setDrawBase`/`drawBase` qtabbar.h:110/111 | — |
| `void XTabBar_setShape(XTabBar* self, int shape);` / `int XTabBar_shape(const XTabBar* self);` | 页签形状；**仅存储**，Rounded/Triangular 同外观（XTabBar.c:1495-1496） | `setShape`/`shape` qtabbar.h:59/58 | — |
| `void XTabBar_setIconSize(XTabBar* self, int size);` / `int XTabBar_iconSize(const XTabBar* self);` | 图标方边 px（0=默认） | `setIconSize(const QSize&)`/`iconSize` qtabbar.h:114/113 | — |
| `void XTabBar_setAutoHide(XTabBar* self, bool enable);` / `bool XTabBar_autoHide(const XTabBar* self);` | 单页自动隐藏页签条（`count>1` 才显示；关闭恢复常显） | `setAutoHide`/`autoHide` qtabbar.h:138/137 | — |
| `void XTabBar_setSelectionBehaviorOnRemove(XTabBar* self, int behavior);` / `int XTabBar_selectionBehaviorOnRemove(const XTabBar* self);` | 移除后选择行为（0 左/1 右/2 上一页；2 实现回退右） | `setSelectionBehaviorOnRemove`/`selectionBehaviorOnRemove` qtabbar.h:126/125 | — |
| `void XTabBar_setChangeCurrentOnDrag(XTabBar* self, bool enable);` / `bool XTabBar_changeCurrentOnDrag(const XTabBar* self);` | 拖拽悬停切换；**仅存储**（XTabBar.c:1528-1529） | `setChangeCurrentOnDrag`/`changeCurrentOnDrag` qtabbar.h:141/140 | — |
| `bool XTabBar_tabRect(const XTabBar* self, int index, XRect* out);` | 页签矩形（out 写回；成功返回 true） | `tabRect` qtabbar.h:101 | out 调用方提供 |
| `int XTabBar_tabAt(const XTabBar* self, const XPoint* pos);` | 局部坐标命中页签（未命中 -1） | `tabAt` qtabbar.h:102 | pos 借用 |
| `int XTabBar_tabWidth(const XTabBar* self);` | 页签宽 | **X 扩展**（`rg tabWidth qtabbar.h` 零命中） | — |
| `int XTabBar_tabHeight(const XTabBar* self);` | 页签高（恒 `XTABBAR_TAB_H`，XTabBar.c:1571-1575） | **X 扩展**（`rg tabHeight qtabbar.h` 零命中） | — |
| `int XTabBar_tabIndexAt(const XTabBar* self, int x, int y);` | (x,y) 命中的页签号（转 `tabAt`） | **X 扩展**（`rg tabIndexAt qtabbar.h` 零命中） | — |
| `bool XTabBar_isEmpty(const XTabBar* self);` | 无页签返回 true | **X 扩展**（`rg isEmpty qtabbar.h` 零命中） | — |
| `void XTabBar_setTabIcon(XTabBar* self, int index, const XString* path);` / `void XTabBar_setTabIcon_2(XTabBar* self, int index, const char* path);` | 设页签图标路径（NULL 清除） | `setTabIcon(const QIcon&)` qtabbar.h:83（Qt 收 QIcon，X 收路径） | path 借用 |
| `const XString* XTabBar_tabIcon(const XTabBar* self, int index);` / `const char* XTabBar_tabIcon_2(const XTabBar* self, int index);` | 读图标路径 | `tabIcon` qtabbar.h:82 | 借用禁释放 |
| `void XTabBar_setTabTextColor(XTabBar* self, int index, uint32_t color);` / `uint32_t XTabBar_tabTextColor(const XTabBar* self, int index);` | 页签文本色 ARGB（0=默认） | `setTabTextColor`/`tabTextColor` qtabbar.h:80/79 | — |
| `void XTabBar_setTabToolTip(XTabBar* self, int index, const XString* tip);` / `void XTabBar_setTabToolTip_2(XTabBar* self, int index, const char* tip);` | 设提示（NULL 清除） | `setTabToolTip` qtabbar.h:89 | tip 借用 |
| `const XString* XTabBar_tabToolTip(const XTabBar* self, int index);` / `const char* XTabBar_tabToolTip_2(const XTabBar* self, int index);` | 读提示 | `tabToolTip` qtabbar.h:90 | 借用禁释放 |
| `void XTabBar_setTabWhatsThis(XTabBar* self, int index, const XString* text);` / `void XTabBar_setTabWhatsThis_2(XTabBar* self, int index, const char* text);` | 设帮助文本（逐项独立槽 `m_tabWhatsThis`） | `setTabWhatsThis` qtabbar.h:94 | text 借用 |
| `const XString* XTabBar_tabWhatsThis(const XTabBar* self, int index);` / `const char* XTabBar_tabWhatsThis_2(const XTabBar* self, int index);` | 读帮助文本 | `tabWhatsThis` qtabbar.h:95 | 借用禁释放 |
| `void XTabBar_setAccessibleTabName(XTabBar* self, int index, const XString* name);` / `void XTabBar_setAccessibleTabName_2(XTabBar* self, int index, const char* name);` | 设无障碍名（仅存储，不派发无障碍事件） | `setAccessibleTabName` qtabbar.h:145 | name 借用 |
| `const XString* XTabBar_accessibleTabName(const XTabBar* self, int index);` / `const char* XTabBar_accessibleTabName_2(const XTabBar* self, int index);` | 读无障碍名 | `accessibleTabName` qtabbar.h:144 | 借用禁释放 |
| `void XTabBar_setTabButtonAt(XTabBar* self, int index, int position, XAbstractButton* button);` | 设左/右角按钮（reparent + show；同槽旧钮隐藏不销毁；removeTab 时 deleteLater） | `setTabButton(index,ButtonPosition,QWidget*)` qtabbar.h:122 | **接管**（父子链级联释放，调用方勿再删） |
| `XAbstractButton* XTabBar_tabButtonAt(const XTabBar* self, int index, int position);` | 读角按钮 | `tabButton` qtabbar.h:123 | 借用禁释放 |
| `void XTabBar_setTabButton(XTabBar* self, int index, XAbstractButton* button);` / `XAbstractButton* XTabBar_tabButton(const XTabBar* self, int index);` | RightSide 槽便捷版 | 同上（默认槽位） | 同上 |
| `void XTabBar_setTabData(XTabBar* self, int index, const XString* data);` / `void XTabBar_setTabData_2(XTabBar* self, int index, const char* data);` | 设页签数据（NULL 清除） | `setTabData(const QVariant&)` qtabbar.h:98（X 以 XString 承载） | data 借用 |
| `const XString* XTabBar_tabData(const XTabBar* self, int index);` / `const char* XTabBar_tabData_2(const XTabBar* self, int index);` | 读页签数据 | `tabData` qtabbar.h:99 | 借用禁释放 |
| `void XTabBar_setTabVisible(XTabBar* self, int index, bool visible);` / `bool XTabBar_isTabVisible(const XTabBar* self, int index);` | 页签可见 | `setTabVisible`/`isTabVisible` qtabbar.h:74/73 | — |
| `void XTabBar_moveTab(XTabBar* self, int from, int to);` | 移动页签并发射 `tabMoved(from,to)` | `moveTab` qtabbar.h:68 | — |
| `#define XTABBAR_BUTTON_LEFTSIDE 0` / `#define XTABBAR_BUTTON_RIGHTSIDE 1` | 角按钮槽位码（对标 `QTabBar::ButtonPosition`） | `ButtonPosition` qtabbar.h:47-51 | — |

**所有权与命名契约**：`_const` 借用族＝`tabIcon`/`tabToolTip`/`tabWhatsThis`/`accessibleTabName`/`tabData`/`tabButtonAt`/`tabButton`（返回 `const XString*` 或借用指针）及其 `_2` UTF-8 版；**例外**：`tabText` 虽无 `_const` 却语义是**新建副本**（XTabBar.h:152-157、XTabBar.c:1285-1289），`tabText_2` 才是借用。`_utf8` 后缀族**不存在**——UTF-8 设置侧一律用 `_2`（`addTab_2`/`insertTab_2`/`setTabText_2`/`setTabIcon_2`/`setTabToolTip_2`/`setTabWhatsThis_2`/`setAccessibleTabName_2`/`setTabData_2` 共 8 组 8 个设置 + 8 个查询），属 §8.2.1「全库 `_2` → `_utf8`」待改批次（XGui.md:1813）。三态 `set*` 无（全部布尔/单值枚举）。

**可覆写点**：`class_init` 12 槽——`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`（`xtabbar_updateSizeHint`，Qt 6.8.3 qtabbar.cpp:1487-1498 可见页签并集口径，XTabBar.c:973-984）、`EXWidget_PaintEvent`、`EXWidget_MousePressEvent`/`MouseMoveEvent`/`MouseReleaseEvent`/`MouseDoubleClickEvent`、`EXWidget_WheelEvent`、`EXWidget_ChangeEvent`（`VXTabBar_changeEvent` 仅 `XEvent_ignore`，XTabBar.c:760-764）、`EXObject_TimerEvent`（滚动连发）、`EXClass_Deinit`、`EXClass_Copy`、`EXClass_Move`（XTabBar.c:951-969）。无 `_base` 公开入口。

**注意事项与坑点**

1. **头注释整体过时**：文件头写「`tabsClosable`/`movable` 为字段保留项」（XTabBar.h:6），但实现两者都已真做——关闭区命中发 `tabCloseRequested`（XTabBar.c:643-666）、按下记 `m_dragActive/m_dragIndex` 且移动跨页即 `XTabBar_moveTab`（XTabBar.c:686-688、710-716）；`setAutoHide` 头注释写「单页自动隐藏行为尚未接入渲染/布局层」（XTabBar.h:296-297），实现已按 `count>1` 真设显隐（XTabBar.c:117-121、1509-1519）。
2. **`selectionBehaviorOnRemove` 的 2 号值降级**：头注释「2=SelectPreviousTab」（XTabBar.h:311-312），实现 switch 只有 `case 0`，其余（含 2）一律 `SelectRightTab`（XTabBar.c:1247-1250）；源码自陈「SelectPreviousTab 需逐页 lastTab 跟踪，本类未存，按 Qt 语义回退 SelectRightTab」（XTabBar.c:1236-1242）。
3. **插入页签不右移当前索引（与 Qt 的登记偏差）**：Qt 在「当前项之前插入」时当前索引右移（qtabbar.cpp:970-971），X 保持既有行为「当前项原地改指新签、选中跳变」，原因写在 XTabBar.c:1140-1150（会击穿 `XTabBarTest.c`「首次添加当前=0」断言，待该断言更新后启用右移）。
4. **`m_elideMode` 编号口径在 XGui.md 已过时**：XGui.md:3713-3716 称「XTabBar.m_elideMode 用另一套编号（XStyleOption.h:985：0=无/1=右/2=左/3=中）」，但 `XStyleOption.h:985-989` 明写「原 m_tabElideMode 字段已删除」；当前实现用 `XAbstractItemViewTextElideMode`（0=Left/1=Right/2=Middle/3=None，XAbstractItemView.h:115-118），缺省 `ElideRight`=1（XTabBar.c:1033-1034），`setElideMode` 越界忽略（XTabBar.c:1434-1437）。
5. **信号函数的载荷参数只是占位**：`currentChanged_signal(self,index)`、`tabBarClicked_signal(self,index)`、`tabBarDoubleClicked_signal(self,index)` 实现全部 `(void)index` 后直接返回自身地址（XTabBar.c:1344-1365），真载荷在发射点用 `xtabbar_emitInt(bar, (size_t)…_signal(bar, idx), idx)` 传入（XTabBar.c:670、70）；`tabCloseRequested_signal(self)` 连参数都没有，同样靠发射点补 idx（XTabBar.c:663-666）。
6. **C++ 包含即编译失败（实测，非注释来源）**：根因在 `XWidget.h:79` 的 `extern "C" {` 无闭合（`XWidget.h` 单独编译同样报错）；本头自己的 `extern "C"`（`:15` 开、`:508-510` 关）配平。`XTabBar.h` 补 1 个 `}` 后 `g++ -std=c++17 -fsyntax-only $FLAGS` 才通过（`$FLAGS` 取自 `build/compile_commands.json`；未补时报 `expected '}' at end of input`，因嵌套提示指向最内层的 `XTabBar.h:15`）。C11 下 0 error。

**测试覆盖**：**有专属套件** `Test/XGuiTest/XTabBarTest.c`（**存在**，148 行；入口 `bool XTabBarTest_runAll(void)` 第 17 行；`tb_expect` 断言调用 **35** 条，失败自带 `[TB-FAIL]` 前缀）。回归挂接 `xgui_regression_test.c:36233`（`expect_true(XTabBarTest_runAll(), "XTabBar/XTabWidget 控件功能")`，`Test/CMakeLists.txt:561` 收源）。覆盖点含 addTab/insertTab/removeTab、setCurrentIndex 发射、currentChanged 连接；另 `Test/XGuiDemo/xgui_demo_apitest_containers.c` 177 处 `XTabBar_*` 调用为间接覆盖。

---

## XTabWidget

**定位**：选项卡容器——内含一个 `XTabBar`（顶部页签）+ 每页一个内部 `XWidget` 页容器（几何由本控件在 resize 时分配）+ 借用记录的页内容控件 + 四角部件。继承链 `XObject ← XWidget ← XTabWidget`（XTabWidget.h:28-29；页码注释 XTabWidget.h:4-7）。头文件 `Src/XGui/Widget/XTabWidget.h`（**336 行**）。守卫 `XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON`（XTabWidget.h:25）；`XTABWIDGET_ON` 缺省 1（XGuiConfig.h:554-556）。

**生命周期**：`XTabWidget_init` 全零 → `XWidget_init` → 挂虚表 → `m_count/m_capacity=0`、`m_currentIndex=-1`、四角 NULL → `XTabBar_init` 内嵌页签条并 `XWidget_show` → 刷 sizeHint → 连接页签条 5 条信号（currentChanged / tabMoved / tabBarClicked / tabBarDoubleClicked / tabCloseRequested，XTabWidget.c:454-496）；`VXTabWidget_deinit` 延迟销毁内部页容器（XTabWidget.c:360 起）；栈/堆释放纪律遵循基类 XWidget 约定。

**API 面**（Qt 行号取 `qtabwidget.h`）

### 生命周期（XTabWidget.h:53-58）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTabWidget_class_init(void);` | 建/取共享虚表（登记 7 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTabWidget_init(XTabWidget* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（0 页、当前 -1、North、内建页签条） | `QTabWidget(QWidget*)` qtabwidget.h:35 | parent 借用 |
| `#define XTabWidget_create(parent, flags) XTabWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |
| `XTabWidget* XTabWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |

### 页签（XTabWidget.h:60-251）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTabWidget_addTab(XTabWidget* self, XWidget* page, const XString* label);` | 追加页签，返回新索引；无效参数 -1 | `addTab(QWidget*,const QString&)` qtabwidget.h:38 | page 借用（内部页容器拥有，page 不销毁） |
| `int XTabWidget_addTab_2(XTabWidget* self, XWidget* page, const char* label);` | UTF-8 重载 | 同上 | 同上 |
| `int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page, const XString* label);` | 插入；负/越界追加（注释引 QStackedLayout::insertWidget） | `insertTab` qtabwidget.h:41 | 同上 |
| `int XTabWidget_insertTab_2(XTabWidget* self, int index, XWidget* page, const char* label);` | UTF-8 重载 | 同上 | 同上 |
| `void XTabWidget_removeTab(XTabWidget* self, int index);` | 移除页签；页内容不销毁（摘父归还） | `removeTab` qtabwidget.h:44 | 归还借用方 |
| `int XTabWidget_count(const XTabWidget* self);` | 页数（NULL→0） | `count` qtabwidget.h:72 | — |
| `int XTabWidget_currentIndex(const XTabWidget* self);` | 当前页（无→-1） | `currentIndex` qtabwidget.h:68 | — |
| `void XTabWidget_setCurrentIndex(XTabWidget* self, int index);` | 切当前页 | `setCurrentIndex` qtabwidget.h:118 | — |
| `XWidget* XTabWidget_currentWidget(const XTabWidget* self);` | 当前页内容控件 | `currentWidget` qtabwidget.h:69 | 借用禁释放 |
| `XWidget* XTabWidget_widget(const XTabWidget* self, int index);` | 指定页内容控件 | `widget(int)` qtabwidget.h:70 | 借用禁释放 |
| `int XTabWidget_indexOf(const XTabWidget* self, const XWidget* page);` | 内容控件所在页号 | `indexOf` qtabwidget.h:71 | page 借用 |
| `XString* XTabWidget_tabText(const XTabWidget* self, int index);` | 读页签文本，**返回新建副本** | `tabText` qtabwidget.h:52 | **拷贝须释放** |
| `const char* XTabWidget_tabText_2(const XTabWidget* self, int index);` | UTF-8 借用；越界 `""` | 同上 | 借用禁释放 |
| `void XTabWidget_setTabText(XTabWidget* self, int index, const XString* text);` / `void XTabWidget_setTabText_2(XTabWidget* self, int index, const char* text);` | 设页签文本（转发页签条） | `setTabText` qtabwidget.h:53 | text 借用 |
| `bool XTabWidget_isTabEnabled(const XTabWidget* self, int index);` / `void XTabWidget_setTabEnabled(XTabWidget* self, int index, bool enabled);` | 页签启用 | `isTabEnabled`/`setTabEnabled` qtabwidget.h:46/47 | — |
| `XTabBar* XTabWidget_tabBar(const XTabWidget* self);` | 取内嵌页签条（**成员地址，非堆对象**） | `tabBar` qtabwidget.h:115 | 借用禁释放 |
| `void XTabWidget_setCurrentWidget(XTabWidget* self, XWidget* page);` | 按控件切当前页 | `setCurrentWidget` qtabwidget.h:119 | page 借用 |
| `void XTabWidget_setWidget(XTabWidget* self, XWidget* widget);` | 替换当前页内容：旧控件摘父转顶层（所有权归还），新控件 reparent 并重排；NULL 清空 | **X 扩展**（`rg setWidget qtabwidget.h` 零命中；Qt 用 `QStackedWidget::insertWidget`） | 旧件归还、新件借用 |
| `void XTabWidget_setTabIcon(XTabWidget* self, int index, const XString* path);` / `void XTabWidget_setTabIcon_2(XTabWidget* self, int index, const char* path);` | 设图标路径（NULL 清除） | `setTabIcon` qtabwidget.h:56 | path 借用 |
| `const XString* XTabWidget_tabIcon(const XTabWidget* self, int index);` / `const char* XTabWidget_tabIcon_2(const XTabWidget* self, int index);` | 读图标路径 | `tabIcon` qtabwidget.h:55 | 借用禁释放 |
| `void XTabWidget_setTabToolTip(XTabWidget* self, int index, const XString* tip);` / `void XTabWidget_setTabToolTip_2(XTabWidget* self, int index, const char* tip);` | 设提示 | `setTabToolTip` qtabwidget.h:59 | tip 借用 |
| `void XTabWidget_setTabWhatsThis(XTabWidget* self, int index, const XString* text);` / `void XTabWidget_setTabWhatsThis_2(XTabWidget* self, int index, const char* text);` | 设帮助文本（**与提示共用存储**，见坑点） | `setTabWhatsThis` qtabwidget.h:64 | text 借用 |
| `const XString* XTabWidget_tabToolTip(const XTabWidget* self, int index);` | 读提示 | `tabToolTip` qtabwidget.h:60 | 借用禁释放 |
| `const XString* XTabWidget_tabWhatsThis(const XTabWidget* self, int index);` | 读帮助文本（实现同 `tabToolTip`） | `tabWhatsThis` qtabwidget.h:65 | 借用禁释放 |
| `void XTabWidget_setTabVisible(XTabWidget* self, int index, bool visible);` / `bool XTabWidget_isTabVisible(const XTabWidget* self, int index);` | 页签可见 | `setTabVisible`/`isTabVisible` qtabwidget.h:50/49 | — |
| `void XTabWidget_setTabBarAutoHide(XTabWidget* self, bool enable);` / `bool XTabWidget_tabBarAutoHide(const XTabWidget* self);` | 页签条自动隐藏（转发 + 容器重排） | `setTabBarAutoHide`/`tabBarAutoHide` qtabwidget.h:111/110 | — |

### 位置/外观转发（XTabWidget.h:195-251）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTabWidget_tabPosition(const XTabWidget* self);` / `void XTabWidget_setTabPosition(XTabWidget* self, int position);` | 页签位置 0..3（North/South/West/East），setter 立即重排 | `tabPosition`/`setTabPosition` qtabwidget.h:76/77 | — |
| `bool XTabWidget_tabsClosable(const XTabWidget* self);` / `void XTabWidget_setTabsClosable(XTabWidget* self, bool closable);` | 可关闭（转发页签条） | `tabsClosable`/`setTabsClosable` qtabwidget.h:79/80 | — |
| `bool XTabWidget_isMovable(const XTabWidget* self);` / `void XTabWidget_setMovable(XTabWidget* self, bool movable);` | 可拖动（转发） | `isMovable`/`setMovable` qtabwidget.h:82/83 | — |
| `void XTabWidget_clear(XTabWidget* self);` | 循环 `removeTab` 清空（对齐 Qt 只摘页不删页） | `clear` qtabwidget.h:113 | 页控件归还 |
| `void XTabWidget_setDocumentMode(XTabWidget* self, bool enable);` / `bool XTabWidget_documentMode(const XTabWidget* self);` | 文档模式（转发） | `setDocumentMode`/`documentMode` qtabwidget.h:108/107 | — |
| `void XTabWidget_setElideMode(XTabWidget* self, int mode);` / `int XTabWidget_elideMode(const XTabWidget* self);` | 省略模式（转发；NULL 查返回 ElideRight） | `setElideMode`/`elideMode` qtabwidget.h:99/98 | — |
| `void XTabWidget_setTabShape(XTabWidget* self, int shape);` / `int XTabWidget_tabShape(const XTabWidget* self);` | 页签形状（转发） | `setTabShape`/`tabShape` qtabwidget.h:88/87 | — |
| `void XTabWidget_setUsesScrollButtons(XTabWidget* self, bool enable);` / `bool XTabWidget_usesScrollButtons(const XTabWidget* self);` | 滚动按钮（转发） | `setUsesScrollButtons`/`usesScrollButtons` qtabwidget.h:105/104 | — |
| `void XTabWidget_setIconSize(XTabWidget* self, int size);` / `int XTabWidget_iconSize(const XTabWidget* self);` | 图标尺寸（单 int 方边承载，Qt 为 QSize） | `setIconSize(const QSize&)`/`iconSize` qtabwidget.h:102/101（**承载差异**） | — |

### 角部件（XTabWidget.h:253-281）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `#define XTABWIDGET_CORNER_TOPLEFT 0` / `_TOPRIGHT 1` / `_BOTTOMLEFT 2` / `_BOTTOMRIGHT 3` | 角位码（对标 `Qt::Corner`；Qt 只用上两角） | `Qt::Corner` | — |
| `XWidget* XTabWidget_cornerWidget(const XTabWidget* self, int corner);` | 读指定角部件（越界 NULL） | `cornerWidget` qtabwidget.h:96 | 借用禁释放 |
| `void XTabWidget_setCornerWidget(XTabWidget* self, XWidget* widget, int corner);` | 设角部件：reparent + 显式 show，旧件隐藏（不销毁）；NULL 清除 | `setCornerWidget` qtabwidget.h:95 | 借用（随本控件父子链级联） |

### 信号（XTabWidget.h:283-332）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTabWidget_currentChanged_signal(XTabWidget* self, int index);` | 页切换（转发页签条 currentChanged），真发射 XTabWidget.c:943-947 | `currentChanged` qtabwidget.h:122 | 标识，不得解引用 |
| `void* XTabWidget_tabCloseRequested_signal(XTabWidget* self);` | 关闭请求（预留：页签侧接通即同步转发） | `tabCloseRequested` qtabwidget.h:123 | 同上 |
| `void* XTabWidget_tabBarClicked_signal(XTabWidget* self);` | 页签单击转发（XTabWidget.c:961-980） | `tabBarClicked` qtabwidget.h:124 | 同上 |
| `void* XTabWidget_tabBarDoubleClicked_signal(XTabWidget* self);` | 页签双击转发（XTabWidget.c:982-1001） | `tabBarDoubleClicked` qtabwidget.h:125 | 同上 |

**所有权与命名契约**：`_const` 借用族＝`tabIcon`/`tabToolTip`/`tabWhatsThis`（返回 `const XString*`）、`tabBar`/`cornerWidget`/`currentWidget`/`widget`（返回借用控件指针）；**例外**：`tabText` 无 `_const` 但返回新建副本（XTabWidget.h:102-107）。`_utf8` 后缀族不存在——UTF-8 侧一律 `_2`（`addTab_2`/`insertTab_2`/`setTabText_2`/`setTabIcon_2`/`setTabToolTip_2`/`setTabWhatsThis_2`），属 §8.2.1 待改批次（XGui.md:1813）。三态 `set*` 无。页内容控件与角部件均为**借用**语义：`removeTab`/`clear` 摘父归还，`setWidget` 替换时旧件摘父转顶层（XTabWidget.h:137-149）。

**可覆写点**：`class_init` 7 槽——`EXWidget_SizeHint`/`EXWidget_MinimumSizeHint`（`xtabwidget_updateSizeHint`：页签条尺寸 + 当前页 sizeHint，对齐 qtabwidget.cpp:837-870，XTabWidget.c:412-440）、`EXWidget_ResizeEvent`（`xtabwidget_layout` 四向摆位 + 页容器/内容/角件几何）、`EXWidget_ChangeEvent`、`EXClass_Deinit`、`EXClass_Copy`、`EXClass_Move`。**无 `EXWidget_PaintEvent`**——XGui.md:2790-2793 记该缺口为「pane 框引擎不可达」的根因之一。无 `_base` 公开入口。

**注意事项与坑点**

1. **`tabWhatsThis` 与 `tabToolTip` 共用存储（项目简化）**：`setTabWhatsThis` 实现直接调 `XTabBar_setTabToolTip`（XTabWidget.c:769-774），`tabWhatsThis` 直接返回 `tabToolTip`（XTabWidget.c:907-911）；而**同族** `XTabBar` 的 `setTabWhatsThis` 有独立槽 `m_tabWhatsThis` 并自称「不与提示共用槽位」（XTabBar.h:396-398）。两层行为不一致，头注释已登记（XTabWidget.h:245-251「项目简化：帮助文本与提示共用存储」）。
2. **头文件 `extern "C"` 收尾错位（实测）**：`XTabWidget.h:300-302` 关了一次 `extern "C"`，`:304-306` 又关了一次（无对应开头），导致 `#endif /* XWIDGET_ON && … */`（:303）之后、`:307` 起的 `tabBarClicked/tabBarDoubleClicked` 两个声明（:320、:332）**既在模块守卫之外、也在 extern "C" 之外**。实测 `nm -C` 显示这两个符号在 C++ 下按 C++ 链接名出现（`U XTabWidget_tabBarClicked_signal(XTabWidget*)`），与其余 298 行 API 的 C 链接混用。
3. **多出的 `}` 恰好抵消了基类缺陷**：`XWidget.h:79` 的 `extern "C" {` 本身无闭合（`tr -cd '{'`/`tr -cd '}'` 得 14/13），`XTabWidget.h` 多出的那个 `}`（:305）正好补上，故本头与本批的 `XTextDocument.h` 是**仅有的两个**能直接 `g++ -std=c++17 -fsyntax-only` 通过者（实测均为 0 层缺失、rc=0）。修基类时须同步删本头的多余 `}`，否则反向破坏（实测：先给 `XWidget.h` 补 `}` 再编本头，会报 `XTabWidget.h:305:1: error: expected declaration before '}' token`）。
4. **West/East 只是几何承载**：页签条四向摆位里 West/East 把条带当竖条占宽（`barW = barH`），源码自陈「XTabBar 当前仅有水平绘制模式，West/East 为几何生效的简化承载，对标 Qt 的东西向页签形状绘制未接入」（XTabWidget.c:68-71）。
5. **角部件无预留逻辑**：上两角按当前尺寸放进页签条行，源码自陈「Qt 以 sizeHint 并在样式中为页签预留空间，此处未接入预留逻辑，页签可能与角部件重叠」（XTabWidget.c:107-111）；下两角仅承载不参与布局（XTabWidget.h:273-275）。
6. **`tabCloseRequested` 是预留接线**：头注释明说「当前 XTabBar 关闭按钮交互尚未实现（tabsClosable 仅存状态），故此句柄为预留」（XTabWidget.h:288-293）——该句同样过时：`XTabBar` 侧关闭区命中已发 `tabCloseRequested`（XTabBar.c:643-666），容器经 init 的 `connect_2` 转发（XTabWidget.c:493-495）。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XTabWidgetTest.c`），与 `XTabBar` 合用 `Test/XGuiTest/XTabBarTest.c`——该文件 14 处 `XTabWidget_*` 调用（含 `XTabWidget_create`、页控件 `XWidget_create` 挂载、套件入口同 `bool XTabBarTest_runAll(void)` :17，35 条 `tb_expect` 断言，`[TB-FAIL]` 前缀）。回归挂接 `xgui_regression_test.c:36233`；另 `Test/XGuiDemo/xgui_demo_apitest_containers.c` 116 处、`xgui_window_demo.c` 12 处为间接覆盖。

---

## XTableView

**定位**：表格视图基类——列宽/行高（统一 + 逐行覆写表）、网格显示/风格、逐行逐列隐藏平行表、跨行跨列合并表、水平/垂直表头对象挂接、排序开关与状态、整行/整列选择、rowAt/columnAt/indexAt/visualRect 几何反查、滚动偏移查询。条目（单元格）模型由派生类 `XTableWidget` 提供。继承链 `XObject ← XWidget ← XAbstractScrollArea ← XAbstractItemView ← XTableView`（XTableView.h:17-18）。头文件 `Src/XGui/Widget/XTableView.h`（**418 行**）。守卫 `XWIDGET_ON && XTABLEWIDGET_ON`（XTableView.h:15）——**无独立 `XTABLEVIEW_ON` 开关**（`rg XTABLEVIEW_ON Src/XGui/XGuiConfig.h` 零命中）。

**生命周期**：`XTableView_init` 全零 → `XAbstractItemView_init` → 挂虚表 → 默认行高 24（`XTV_DEFAULT_ROW_HEIGHT`，XTableView.c:17）、网格 Solid 且可见、`m_wordWrap=false`、`m_cornerButton=true`、`m_sortColumn=-1`，并**内建**两个 `XHeaderView` 子控件（水平/垂直，以本视图为父、显式 hide、只承载段尺寸状态），XTableView.c:186-228；`VXTableView_deinit` 释放 5 张平行表并把两个表头指针置 NULL（**不析构表头**，XTableView.c:241-279）；栈/堆纪律遵循基类 XWidget 约定。`copy`/`move` 重载（XTableView.c:282 起）。

**API 面**（Qt 行号取 `widgets/itemviews/qtableview.h`）

### 生命周期（XTableView.h:82-109）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTableView_class_init(void);` | 建/取共享虚表（登记 6 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTableView_init(XTableView* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（含内建双表头） | `QTableView(QWidget*)` qtableview.h:29 | parent 借用 |
| `XTableView* XTableView_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 堆上创建 | 同上 | 调用方拥有，`XClassDelete` |
| `#define XTableView_create(parent, flags) XTableView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型包装 | 同上 | 同上 |

### 尺寸（XTableView.h:111-128）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setColumnWidth(XTableView* self, int column, int width);` | 设列宽（按需扩容列宽表 + 同步 header 段数/段尺寸 + 差量同步滚动内容宽） | `setColumnWidth` qtableview.h:51 | — |
| `int XTableView_columnWidth(const XTableView* self, int column);` | 查列宽；header 段覆盖时以段尺寸为准，否则回落数组，越界返回 `XTV_DEFAULT_COL_WIDTH`(80) | `columnWidth` qtableview.h:52 | — |
| `void XTableView_setRowHeight(XTableView* self, int row, int height);` | 设**单行**行高（row<0 或 height<=0 无操作；>0x7FFF 钳到 int16 上限） | `setRowHeight` qtableview.h:45 | — |
| `int XTableView_rowHeight(const XTableView* self, int row);` | 查行高；未被逐行覆写回落缺省 | `rowHeight` qtableview.h:46 | — |

### 按内容调整（XTableView.h:130-161）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_resizeColumnToContents(XTableView* self, int column);` | 按可见行文本最大宽 + 左右各 4px 定列宽（不含表头文本/换行/最小钳制） | `resizeColumnToContents` qtableview.h:95 | — |
| `void XTableView_resizeColumnsToContents(XTableView* self);` | 逐列调用 | 同上 qtableview.h:96 | — |
| `void XTableView_resizeRowToContents(XTableView* self, int row);` | 行高 = 文本高 + 上下各 2px，仅该行覆写 | `resizeRowToContents` qtableview.h:93 | — |
| `void XTableView_resizeRowsToContents(XTableView* self);` | 逐行调用 | 同上 qtableview.h:94 | — |

### 网格（XTableView.h:163-175）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setShowGrid(XTableView* self, bool show);` | 显示开关与风格联动（开→无风格回落实线；关→NoGrid） | `setShowGrid` qtableview.h:98 | — |
| `bool XTableView_showGrid(const XTableView* self);` | 等价 `gridStyle != NoGrid` | `showGrid` qtableview.h:63 | — |
| `void XTableView_setGridStyle(XTableView* self, int style);` | 设风格（仅受 `XTableViewGridStyle` 0..2，越界忽略） | `setGridStyle(Qt::PenStyle)` qtableview.h:66 | — |
| `int XTableView_gridStyle(const XTableView* self);` | 查风格 | `gridStyle` qtableview.h:65 | — |

### 文本与角按钮（XTableView.h:177-190）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setWordWrap(XTableView* self, bool wrap);` | 自动换行开关（平铺绘制当前恒单行，仅持久化标志） | `setWordWrap` qtableview.h:68 | — |
| `bool XTableView_wordWrap(const XTableView* self);` | 查开关 | `wordWrap` qtableview.h:69 | — |
| `void XTableView_setCornerButtonEnabled(XTableView* self, bool enable);` | 左上角按钮开关 | `setCornerButtonEnabled` qtableview.h:72 | — |
| `bool XTableView_isCornerButtonEnabled(const XTableView* self);` | 查开关 | 同上 qtableview.h:73 | — |

### 位置反查与视口位置（XTableView.h:192-246）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTableView_rowAt(const XTableView* self, int y);` | 控件坐标 y→行号（先加回滚动偏移、再扣表头区，隐藏行占高 0；无命中 -1） | `rowAt` qtableview.h:43 | — |
| `int XTableView_columnAt(const XTableView* self, int x);` | 控件坐标 x→列号（加回横向滚动偏移，隐藏列占宽 0） | `columnAt` qtableview.h:49 | — |
| `bool XTableView_indexAt(const XTableView* self, int x, int y, int* outRow, int* outCol);` | 视口坐标→单元格；命中所属合并区时吸附到区间原点；未命中两输出写 -1 | `indexAt` qtableview.h:78（Qt 返回 QModelIndex） | out 参数调用方提供 |
| `int XTableView_rowViewportPosition(const XTableView* self, int row);` | 行视口起点 y（**含**顶部表头区高 + 前序可见行高） | `rowViewportPosition` qtableview.h:42 | — |
| `int XTableView_columnViewportPosition(const XTableView* self, int column);` | 列视口起点 x（无左侧表头区，起点 0 + 前序可见列宽） | `columnViewportPosition` qtableview.h:48 | — |

### 滚动与偏移（XTableView.h:248-277）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_scrollTo(XTableView* self, int row, int column, int hint);` | 滚动使单元格可见（转发基类 `XAbstractItemView_scrollTo`） | `scrollTo(const QModelIndex&, ScrollHint)` qtableview.h:77 | — |
| `int XTableView_horizontalOffset(const XTableView* self);` | 水平滚动条当前值（无条/空 → 0） | `horizontalOffset` qtableview.h:121 | — |
| `int XTableView_verticalOffset(const XTableView* self);` | 垂直滚动条当前值（无条/空 → 0） | `verticalOffset` qtableview.h:122 | — |

### 视觉矩形（XTableView.h:279-297）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XRect XTableView_visualRect(const XTableView* self, int row, int column);` | 单元格矩形（x=columnViewportPosition、y=rowViewportPosition、宽=columnWidth、高=rowHeight）；空/越界/隐藏返回 `{0,0,0,0}` | `visualRect` qtableview.h:76 | 值 |

### 行/列隐藏（XTableView.h:299-324）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setRowHidden(XTableView* self, int row, bool hide);` | 设行隐藏（平行表按需扩容，默认不隐藏） | `setRowHidden` qtableview.h:55 | — |
| `bool XTableView_isRowHidden(const XTableView* self, int row);` | 查行隐藏（越界/未建表 false） | `isRowHidden` qtableview.h:54 | — |
| `void XTableView_setColumnHidden(XTableView* self, int column, bool hide);` | 设列隐藏 | `setColumnHidden` qtableview.h:58 | — |
| `bool XTableView_isColumnHidden(const XTableView* self, int column);` | 查列隐藏 | `isColumnHidden` qtableview.h:57 | — |
| `void XTableView_hideRow(XTableView* self, int row);` / `void XTableView_showRow(XTableView* self, int row);` | 便捷 hide/show 行 | `hideRow`/`showRow` qtableview.h:89/91 | — |
| `void XTableView_hideColumn(XTableView* self, int column);` / `void XTableView_showColumn(XTableView* self, int column);` | 便捷 hide/show 列 | `hideColumn`/`showColumn` qtableview.h:90/92 | — |

### 跨行/列合并（XTableView.h:326-356）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setSpan(XTableView* self, int row, int column, int rowSpan, int columnSpan);` | 设合并区间；同原点替换；均 1 移除；**仅负值拒绝**（0 允许） | `setSpan` qtableview.h:80 | — |
| `int XTableView_rowSpan(const XTableView* self, int row, int column);` | 所属区间跨行数（未合并/越界/NULL → 1） | `rowSpan` qtableview.h:81 | — |
| `int XTableView_columnSpan(const XTableView* self, int row, int column);` | 所属区间跨列数 | `columnSpan` qtableview.h:82 | — |
| `void XTableView_clearSpans(XTableView* self);` | 清空区间表（容量保留） | `clearSpans` qtableview.h:83 | — |

### 表头挂接（XTableView.h:358-393）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setHorizontalHeader(XTableView* self, XHeaderView* header);` | 挂水平表头（方向须 0=水平，否则忽略；NULL 不动作） | `setHorizontalHeader` qtableview.h:39 | **借用**（deinit 只置 NULL，XTableView.c:277-279） |
| `XHeaderView* XTableView_horizontalHeader(const XTableView* self);` | 读水平表头 | `horizontalHeader` qtableview.h:37 | 借用禁释放 |
| `void XTableView_setVerticalHeader(XTableView* self, XHeaderView* header);` | 挂垂直表头（方向须 1=垂直） | `setVerticalHeader` qtableview.h:40 | 借用 |
| `XHeaderView* XTableView_verticalHeader(const XTableView* self);` | 读垂直表头 | `verticalHeader` qtableview.h:38 | 借用禁释放 |

### 排序与选择（XTableView.h:395-409）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableView_setSortingEnabled(XTableView* self, bool enable);` | 置排序标志（**仅置位**，无立即排序） | `setSortingEnabled` qtableview.h:60 | — |
| `bool XTableView_isSortingEnabled(const XTableView* self);` | 查标志 | 同上 qtableview.h:61 | — |
| `void XTableView_sortByColumn(XTableView* self, int column, int order);` | 记录排序列/序（0 升/非 0 降），不重排数据 | `sortByColumn` qtableview.h:97 | — |
| `void XTableView_selectRow(XTableView* self, int row);` | 选整行——实现为 `XAbstractItemView_setCurrentIndex(row, m_currentColumn)` | `selectRow` qtableview.h:87（Qt 建整行选区） | — |
| `void XTableView_selectColumn(XTableView* self, int column);` | 选整列——同上以当前行定位 | `selectColumn` qtableview.h:88 | — |

**所有权与命名契约**：无字符串 API，故无 `_const`/`_utf8` 族；两个表头 getter 返回借用指针（禁释放），两个 setter 名义「所有权转移」实为借用（见坑点 2）；`_2` 重载无；三态 `set*` 无（全部布尔/单值整型，`XTableViewGridStyle` 为 0/1/2 枚举）。`out` 型参数（`XTableView_indexAt` 的 outRow/outCol）由调用方提供存储，未命中一律写 -1（XTableView.h:197-215）；同族的 `XTabBar_tabRect(const XTabBar*, int, XRect*)` 亦为 out 参数形态。

**可覆写点**：`class_init` 6 槽——`EXClass_Deinit`、`EXClass_Copy`、`EXClass_Move`、`EXWidget_PaintEvent`、`EXAbstractItemView_IndexAt → XTableView_indexAt`、`EXAbstractItemView_VisualRect → XTableView_visualRect`（XTableView.c:172-185）。无 `_base` 公开入口。

**注意事项与坑点**

1. **`setHorizontalHeader` 头注释自相矛盾**：参数说明写「所有权转移给本视图（对齐 Qt：构造即拥有内建表头，报废时释放）」（XTableView.h:364-366），紧接着同函数的 @note 写「借用承载：不转移所有权、不析构，生命周期归调用方；copy 共享指针、move 转移指针」（XTableView.h:369-372）。实现取后者：只赋指针 + `XWidget_update`（XTableView.c:1043-1051），deinit 仅 `m_horizontalHeader = NULL`（XTableView.c:277-279）。
2. **`rowViewportPosition` 坐标口径文档冲突**：头注释写「= 顶部表头区高（内嵌自绘表头）+ 该行之前各可见行高累计」（XTableView.h:224-226），实现 `yAcc = XTV_HEADER_H`（=20，XTableView.c:29、839）确实含表头区；但 XGui.md:3579-3581 记「`XTableView_rowViewportPosition` 改为**不含表头区**的纯内容坐标（对齐 `QHeaderView::sectionViewportPosition`），此前含 `XTV_HEADER_H` 与 `rowAt` 口径不一致」——该条已被实现反超，文档过时（实现侧注释亦重申「与 rowAt 互逆：顶部内嵌表头区高度计入起点」，XTableView.c:836）。
3. **`setSortingEnabled(true)` 不排序（与 XGui.md 相反）**：XGui.md:3565 记「旧：只置标志 → 新：**立即**按当前指示器排序一次，再置标志（qtableview.cpp:2734-2759）」，实现只有 `self->m_sortingEnabled = enable;`（XTableView.c:679-680）；真正重排只发生在 `XTableWidget_sortItems`（XTableWidget.c 整行冒泡）与 `xtw_sortedInsert`（XTableWidget.c:1978 起，`setItem` 时按指示器插位）。
4. **`selectRow/selectColumn` 名不副实**：头注释「选择整行/整列」（XTableView.h:406-409），实现只是把当前索引设到 `(row, 当前列)`/`(当前行, column)`（XTableView.c:691-703），不建立整行/整列选区，也不碰 `XItemSelectionModel`。
5. **两套表头常量不同源**：视图侧 `XTV_HEADER_H`=20（XTableView.c:29），`XTableWidget` 侧 `XTW_HEADER_H`=24 / `XTW_HEADER_W`=40（XTableWidget.c:34-36）；该差异曾导致 `itemAt` 命中错格，根修注释明确写「`XTV_HEADER_H`/列宽口径与 XTableWidget 的 `m_headerHeight`/`m_colWidths` 不同源，(80,36) 在 cellAt=(0,0) 却被吸附成 (0,1)」（XTableWidget.c:2650-2654）。
6. **头文件残留空注释（无对应声明）**：XTableView.h:107「@brief 析构入口（查表分派父类析构）」与 XTableView.h:109「@brief 删除堆上视图（查表分派析构并释放内存）」两行注释后无任何声明——析构走虚表 `EXClass_Deinit`，无公开 `_deinit`/`_delete` 函数。
7. **C++ 包含即编译失败（实测，非注释来源）**：根因在 `XWidget.h:79` 的 `extern "C" {` 无闭合（`XWidget.h` 单独编译同样报错）；本头自己的 `extern "C"`（`:6` 开、`:412-414` 关）配平。本头补 1 个 `}` 后 `g++ -std=c++17 -fsyntax-only $FLAGS` 通过，未补报 `expected '}' at end of input`（因嵌套，提示指向最内层的 `XTableView.h:6`）。C11 下 0 error。

**测试覆盖**：**无专属套件**。间接覆盖主体是 `Test/XGuiTest/XTableAlignTest.c`（614 行，59 处 `XTableView_*` 调用；入口 `int XTableAlignTest_runAll(void)` :602，内部 8 个子用例 `tbl_testCopyMove`/`tbl_testBoundaries`/`tbl_testSpanMatrix`/`tbl_testHideMatrix`/`tbl_testSortingMatrix`/`tbl_testElideMatrix`/`tbl_testScrollStepMatrix`/`tbl_testSerializeRobustness`，`tbl_expect` 断言调用 **75** 条，失败前缀 `[TBL-FAIL]`）；回归挂接 `xgui_regression_test.c:36242`（`expect_true(XTableAlignTest_runAll() == 0, "XTableView/XTableWidget 对齐专项")`）。另 `Test/XGuiDemo/xgui_demo_apitest_views.c` 107 处、`xgui_demo_page_views.c` / `xgui_demo_page_modbus.c` 为间接覆盖。

---

## XTableWidget

**定位**：行×列条目表格——在 `XTableView` 之上提供 `XTableWidgetItem`（堆对象、role→`XVariant` 映射、`Clone` 虚槽）、内建 `XAbstractItemModel` 桥、表头标签、单元格部件挂载、当前格/选区、排序/查找/坐标反查、持久编辑器与 15 个信号。继承链 `XObject ← XWidget ← XAbstractScrollArea ← XAbstractItemView ← XTableView ← XTableWidget`（XTableWidget.h:33-34；文件头 XTableWidget.h:4-6 同述）。头文件 `Src/XGui/Widget/XTableWidget.h`（**1325 行**，含 `XTableWidgetItem` 与 `XTableWidgetSelectionRange` 两个辅助类型）。守卫 `#if XTABLEWIDGET_ON`（XTableWidget.h:31），缺省 1（XGuiConfig.h:635-637）。

**生命周期**：`XTableWidget_init` 全零 → `XTableView_init` → 建内建 model 并 `XAbstractItemView_setModel` → 连接 `dataChanged → xtw_modelDataChangedSlot` → 挂虚表 → 当前格 -1/-1、行高 24、表头 24/40、网格可见、`m_enteredRow/Column=-2`、`m_selectionRow/Column=-1`、原型 NULL（XTableWidget.c:1605-1635）；`VXTableWidget_deinit` 逐行释放条目与行块、释放表头 `XString`、model、原型、挂载部件（XTableWidget.c:1373 起）。条目侧：`XTableWidgetItem` 是 `XObject` 子类，**必须** `_init`/`_create` 构造、`XClassDeinit`/`XClassDelete` 释放（XTableWidget.h:87-89），`setItem` 后归表格所有。

**API 面**（Qt 行号取 `widgets/itemviews/qtablewidget.h`）

### XTableWidgetItem 构造/克隆（XTableWidget.h:118-231）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTableWidgetItem_class_init(void);` | 建/取条目虚表（含 `Clone` 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTableWidgetItem_init(XTableWidgetItem* self);` | 栈对象初始化（类型 Type、缺省 6 位 flags：Editable\|Selectable\|UserCheckable\|Enabled\|DragEnabled\|DropEnabled） | `QTableWidgetItem(int type = Type)` qtablewidget.h:55 | — |
| `void XTableWidgetItem_init_type(XTableWidgetItem* self, int type);` | 带类型初始化 | 同上 | — |
| `void XTableWidgetItem_init_text(XTableWidgetItem* self, const XString* text, int type);` | 带文本初始化（NULL 按空串） | `QTableWidgetItem(const QString&, int)` qtablewidget.h:56 | text 借用 |
| `void XTableWidgetItem_init_iconText(XTableWidgetItem* self, const XIcon* icon, const XString* text, int type);` | 带图标 + 文本初始化 | `QTableWidgetItem(const QIcon&, const QString&, int)` qtablewidget.h:57 | icon/text 借用 |
| `XTableWidgetItem* XTableWidgetItem_create_ex(XMemoryType memory);` | 堆创建 | 同上 qtablewidget.h:55 | 调用方拥有，`XClassDelete` |
| `XTableWidgetItem* XTableWidgetItem_create(void);` | 堆创建（默认内存类型） | 同上 | 同上 |
| `XTableWidgetItem* XTableWidgetItem_create_text(const XString* text, int type);` | 堆创建 + 文本 | 同上 qtablewidget.h:56 | 同上 |
| `XTableWidgetItem* XTableWidgetItem_create_utf8(const char* utf8, int type);` | 堆创建 + UTF-8 文本 | 同上 | 同上 |
| `XTableWidgetItem* XTableWidgetItem_create_iconText(const XIcon* icon, const XString* text, int type);` | 堆创建 + 图标 + 文本 | 同上 qtablewidget.h:57 | 同上 |
| `XTableWidgetItem* XTableWidgetItem_create_copy(const XTableWidgetItem* other);` | 拷贝创建（深拷贝全部 role + flags，`m_view` 置 NULL） | 拷贝构造 | 同上 |
| `XTableWidgetItem* XTableWidgetItem_clone_base(const XTableWidgetItem* self);` | 多态克隆：走 `EXTableWidgetItem_Clone` 槽，未重载回落 `create_copy` | `clone` qtablewidget.h:61 | 同上 |

### XTableWidgetItem 查询/设置与比较/序列化（XTableWidget.h:233-560）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTableWidgetItem_type(const XTableWidgetItem* self);` | 条目类型（NULL→0） | `type` qtablewidget.h:150 | — |
| `XTableWidget* XTableWidgetItem_tableWidget(const XTableWidgetItem* self);` | 所属表格（游离/NULL→NULL） | `tableWidget` qtablewidget.h:63 | 借用禁释放 |
| `int XTableWidgetItem_row(const XTableWidgetItem* self);` | 所在行（游离→-1，经 `XTableWidget_indexFromItem`） | `row` qtablewidget.h:65 | — |
| `int XTableWidgetItem_column(const XTableWidgetItem* self);` | 所在列（游离→-1） | `column` qtablewidget.h:66 | — |
| `uint32_t XTableWidgetItem_flags(const XTableWidgetItem* self);` | 标志位（`XItemFlag` 组合，NULL→0） | `flags` qtablewidget.h:71 | — |
| `void XTableWidgetItem_setFlags(XTableWidgetItem* self, uint32_t flags);` | 设标志位 | `setFlags` qtablewidget.h:72 | — |
| `bool XTableWidgetItem_isSelected(const XTableWidgetItem* self);` | 选中标记 | `isSelected` qtablewidget.h:69 | — |
| `void XTableWidgetItem_setSelected(XTableWidgetItem* self, bool select);` | 置选中标记 + 请求表格重绘（不驱动选择模型） | `setSelected` qtablewidget.h:68 | — |
| `const XVariant* XTableWidgetItem_data(const XTableWidgetItem* self, int role);` | 读 role 数据（`EditRole` 归一化为 `DisplayRole`） | `data` qtablewidget.h:139 | **借用**（随条目析构或该 role 重设失效） |
| `void XTableWidgetItem_setData(XTableWidgetItem* self, int role, const XVariant* value);` | 写 role 数据（**深拷贝**；NULL 移除该 role；DisplayRole 触发 itemChanged/cellChanged + 模型同步） | `setData` qtablewidget.h:140 | value 借用，调用方仍拥有 |
| `XString* XTableWidgetItem_text(const XTableWidgetItem* self);` | 读文本，**新建副本**（无文本也返回空串对象，仍须释放） | `text` qtablewidget.h:74 | **拷贝须释放** |
| `const XString* XTableWidgetItem_text_const(const XTableWidgetItem* self);` | 读文本借用（禁跨写入持有） | 同上 | 借用禁释放 |
| `const char* XTableWidgetItem_text_toUtf8(const XTableWidgetItem* self);` | 读文本 UTF-8 视图（`""` 非 NULL） | 同上 | 借用（下次写入失效） |
| `void XTableWidgetItem_setText(XTableWidgetItem* self, const XString* text);` | 设文本（NULL 按空串） | `setText` qtablewidget.h:76 | text 借用 |
| `void XTableWidgetItem_setText_utf8(XTableWidgetItem* self, const char* utf8);` | 设 UTF-8 文本 | 同上 | utf8 借用 |
| `const XIcon* XTableWidgetItem_icon(const XTableWidgetItem* self);` | 读图标（DecorationRole） | `icon` qtablewidget.h:78 | 借用禁释放 |
| `void XTableWidgetItem_setIcon(XTableWidgetItem* self, const XIcon* icon);` | 设图标（转 DecorationRole，NULL 清除） | `setIcon` qtablewidget.h:80 | icon 借用 |
| `XString* XTableWidgetItem_statusTip(const XTableWidgetItem* self);` | 读状态栏提示（StatusTipRole），新建副本 | `statusTip` qtablewidget.h:82 | **拷贝须释放** |
| `const char* XTableWidgetItem_statusTip_toUtf8(const XTableWidgetItem* self);` | 读提示 UTF-8 借用 | 同上 | 借用 |
| `void XTableWidgetItem_setStatusTip(XTableWidgetItem* self, const XString* tip);` / `void XTableWidgetItem_setStatusTip_utf8(XTableWidgetItem* self, const char* utf8);` | 设状态栏提示 | `setStatusTip` qtablewidget.h:84 | tip 借用 |
| `XString* XTableWidgetItem_toolTip(const XTableWidgetItem* self);` | 读工具提示（ToolTipRole），新建副本 | `toolTip` qtablewidget.h:87 | **拷贝须释放** |
| `const char* XTableWidgetItem_toolTip_toUtf8(const XTableWidgetItem* self);` | 读工具提示 UTF-8 借用 | 同上 | 借用 |
| `void XTableWidgetItem_setToolTip(XTableWidgetItem* self, const XString* tip);` / `void XTableWidgetItem_setToolTip_utf8(XTableWidgetItem* self, const char* utf8);` | 设工具提示 | `setToolTip` qtablewidget.h:89 | tip 借用 |
| `XString* XTableWidgetItem_whatsThis(const XTableWidgetItem* self);` | 读 What's This（WhatsThisRole），新建副本 | `whatsThis` qtablewidget.h:93 | **拷贝须释放** |
| `const char* XTableWidgetItem_whatsThis_toUtf8(const XTableWidgetItem* self);` | 读 What's This UTF-8 借用 | 同上 | 借用 |
| `void XTableWidgetItem_setWhatsThis(XTableWidgetItem* self, const XString* text);` / `void XTableWidgetItem_setWhatsThis_utf8(XTableWidgetItem* self, const char* utf8);` | 设 What's This | `setWhatsThis` qtablewidget.h:95 | text 借用 |
| `const XFont* XTableWidgetItem_font(const XTableWidgetItem* self);` | 读字体（FontRole） | `font` qtablewidget.h:98 | 借用禁释放 |
| `void XTableWidgetItem_setFont(XTableWidgetItem* self, const XFont* font);` | 设字体（NULL 清除） | `setFont` qtablewidget.h:100 | font 借用 |
| `int XTableWidgetItem_textAlignment(const XTableWidgetItem* self);` | 读文本对齐（TextAlignmentRole，0=委托缺省） | `textAlignment` qtablewidget.h:103/106 | — |
| `void XTableWidgetItem_setTextAlignment(XTableWidgetItem* self, int alignment);` | 设对齐（0 清除） | `setTextAlignment` qtablewidget.h:111-117 | — |
| `bool XTableWidgetItem_background(const XTableWidgetItem* self, XColor* out);` | 读背景色（BackgroundRole，未设置/out NULL 返回 false 且不改 out） | `background` qtablewidget.h:119 | out 调用方提供 |
| `void XTableWidgetItem_setBackground(XTableWidgetItem* self, const XColor* color);` | 设背景色（NULL 清除） | `setBackground` qtablewidget.h:121 | color 借用 |
| `bool XTableWidgetItem_foreground(const XTableWidgetItem* self, XColor* out);` | 读前景色（ForegroundRole） | `foreground` qtablewidget.h:124 | out 调用方提供 |
| `void XTableWidgetItem_setForeground(XTableWidgetItem* self, const XColor* color);` | 设前景色（NULL 清除） | `setForeground` qtablewidget.h:126 | color 借用 |
| `int XTableWidgetItem_checkState(const XTableWidgetItem* self);` | 读勾选态（0 未选/1 半选/2 选中） | `checkState` qtablewidget.h:129 | — |
| `void XTableWidgetItem_setCheckState(XTableWidgetItem* self, int state);` | 设勾选态 | `setCheckState` qtablewidget.h:131 | — |
| `bool XTableWidgetItem_sizeHint(const XTableWidgetItem* self, XSize* out);` | 读尺寸提示（SizeHintRole） | `sizeHint` qtablewidget.h:134 | out 调用方提供 |
| `void XTableWidgetItem_setSizeHint(XTableWidgetItem* self, const XSize* size);` | 设尺寸提示（NULL 或宽高均<=0 清除） | `setSizeHint` qtablewidget.h:136 | size 借用 |
| `bool XTableWidgetItem_lessThan(const XTableWidgetItem* lhs, const XTableWidgetItem* rhs);` | 排序比较：两侧均字符串按 `XStrcmp`，否则 `XVariant_compare`；NULL 按最小 | `operator<` qtablewidget.h:142 | — |
| `bool XTableWidgetItem_write(const XTableWidgetItem* self, XDataStream* stream);` | 写出全部状态（roleCount → role+`XVariant` → type → flags；selected 不写） | `write(QDataStream&)` qtablewidget.h:146（线格式**不互通**） | stream 借用 |
| `bool XTableWidgetItem_read(XTableWidgetItem* self, XDataStream* stream);` | 读回全部状态（先读临时容器，失败不改原条目；`m_view` 保留） | `read(QDataStream&)` qtablewidget.h:145 | stream 借用 |

### XTableWidgetSelectionRange（XTableWidget.h:562-603）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XTableWidgetSelectionRange XTableWidgetSelectionRange_create(int top, int left, int bottom, int right);` | 按值构造选区（缺省 `-1,-1,-2,-2` = 空范围） | `QTableWidgetSelectionRange(int,int,int,int)` qtablewidget.h:16-32 | 值，无堆资源 |
| `int XTableWidgetSelectionRange_topRow(const XTableWidgetSelectionRange* self);` | 起始行（NULL→-1） | `topRow` qtablewidget.h:34 | — |
| `int XTableWidgetSelectionRange_bottomRow(const XTableWidgetSelectionRange* self);` | 结束行（NULL→-1） | `bottomRow` qtablewidget.h:35 | — |
| `int XTableWidgetSelectionRange_leftColumn(const XTableWidgetSelectionRange* self);` | 起始列（NULL→-1） | `leftColumn` qtablewidget.h:36 | — |
| `int XTableWidgetSelectionRange_rightColumn(const XTableWidgetSelectionRange* self);` | 结束列（NULL→-1） | `rightColumn` qtablewidget.h:37 | — |
| `int XTableWidgetSelectionRange_rowCount(const XTableWidgetSelectionRange* self);` | 跨行数（空范围/NULL→0） | `rowCount` qtablewidget.h:38 | — |
| `int XTableWidgetSelectionRange_columnCount(const XTableWidgetSelectionRange* self);` | 跨列数 | `columnCount` qtablewidget.h:39 | — |
| `bool XTableWidgetSelectionRange_equals(const XTableWidgetSelectionRange* lhs, const XTableWidgetSelectionRange* rhs);` | 四边相等；两侧同为 NULL 也视为相等 | `operator==` | — |

### XTableWidget 生命周期与行列（XTableWidget.h:644-776）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTableWidget_class_init(void);` | 建/取共享虚表（登记 12 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTableWidget_init(XTableWidget* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（0×0、行高 24、表头 24/40、网格可见、滚动条 AsNeeded） | `QTableWidget(QWidget*)` qtablewidget.h:200 | parent 借用 |
| `XTableWidget* XTableWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 堆创建 | 同上 | 调用方拥有，`XClassDelete` |
| `XTableWidget* XTableWidget_create_size(XMemoryType memory, int rows, int columns, XWidget* parent, XWidgetFlags flags);` | 建 + 直接设行列（不触发结构变更信号） | `QTableWidget(int rows, int columns, QWidget*)` qtablewidget.h:201 | 同上 |
| `#define XTableWidget_create(parent, flags) XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型包装 | 同上 | 同上 |
| `XAbstractItemModel* XTableWidget_model(const XTableWidget* self);` | 内建模型借用 | `model`（Qt 为 `QAbstractItemView::model`，且 QTableWidget::setModel 私有） | 借用禁释放 |
| `void XTableWidget_setRowCount(XTableWidget* self, int rows);` | 设行数（负值忽略；缩行删尾部条目与行头） | `setRowCount` qtablewidget.h:204 | — |
| `int XTableWidget_rowCount(const XTableWidget* self);` | 行数（NULL→0） | `rowCount` qtablewidget.h:205 | — |
| `void XTableWidget_setColumnCount(XTableWidget* self, int columns);` | 设列数（扩列补空槽；缩列删条目与列头） | `setColumnCount` qtablewidget.h:207 | — |
| `int XTableWidget_columnCount(const XTableWidget* self);` | 列数 | `columnCount` qtablewidget.h:208 | — |
| `void XTableWidget_insertRow(XTableWidget* self, int row);` | 插行（>行数忽略）；原行及其条目/行头/挂载/当前格/选区下移 | `insertRow` qtablewidget.h:270 | — |
| `void XTableWidget_insertColumn(XTableWidget* self, int column);` | 插列（>列数忽略） | `insertColumn` qtablewidget.h:271 | — |
| `void XTableWidget_removeRow(XTableWidget* self, int row);` | 删行（该行条目与行头**被删除**，不返回） | `removeRow` qtablewidget.h:272 | 条目由表格释放 |
| `void XTableWidget_removeColumn(XTableWidget* self, int column);` | 删列（条目与列头被删除） | `removeColumn` qtablewidget.h:273 | 同上 |

### 单元格条目（XTableWidget.h:778-855）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableWidget_setItem(XTableWidget* self, int row, int column, XTableWidgetItem* item);` | 写单元格；item 属别表则忽略；越界仍把 `m_view` 设为 self 但不入表；NULL 等价 take+delete | `setItem` qtablewidget.h:214 | **所有权转移给表格**（调用方勿再释放） |
| `XTableWidgetItem* XTableWidget_item(const XTableWidget* self, int row, int column);` | 读条目 | `item` qtablewidget.h:213 | 借用禁释放 |
| `const XTableWidgetItem* XTableWidget_item_const(const XTableWidget* self, int row, int column);` | 读条目（const 借用） | 同上 | 借用禁释放 |
| `XTableWidgetItem* XTableWidget_takeItem(XTableWidget* self, int row, int column);` | 取出条目（清格、`m_view` 置 NULL、发 itemChanged/cellChanged） | `takeItem` qtablewidget.h:215 | **所有权移交调用方**（`XClassDelete`） |
| `XTableWidgetItem* XTableWidget_itemPrototype(const XTableWidget* self);` | 读原型 | `itemPrototype` qtablewidget.h:265 | 借用禁释放 |
| `void XTableWidget_setItemPrototype(XTableWidget* self, XTableWidgetItem* prototype);` | 设原型（接管所有权，旧原型 delete） | `setItemPrototype` qtablewidget.h:266 | **所有权转移** |

### 单元格部件（XTableWidget.h:857-897）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableWidget_setCellWidget(XTableWidget* self, int row, int column, XWidget* widget);` | 挂载部件（reparent 到 viewport；重复挂载释放旧件；NULL 等价 remove） | `setCellWidget` qtablewidget.h:249 | **所有权转移给表格** |
| `XWidget* XTableWidget_cellWidget(const XTableWidget* self, int row, int column);` | 读挂载部件（越界/未挂 NULL） | `cellWidget` qtablewidget.h:248 | 借用禁释放 |
| `void XTableWidget_removeCellWidget(XTableWidget* self, int row, int column);` | **释放**该格挂载部件（非仅解除关联） | `removeCellWidget` qtablewidget.h:250（Qt 内联为 `setCellWidget(...,nullptr)`） | 表格释放 |

### 表头（XTableWidget.h:899-992）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableWidget_setHorizontalHeaderLabels(XTableWidget* self, const char* const* labels, int count);` | 批量设水平表头（不改列数；超出列数部分仍存储不绘制） | `setHorizontalHeaderLabels` qtablewidget.h:228 | labels 借用 |
| `void XTableWidget_setVerticalHeaderLabels(XTableWidget* self, const char* const* labels, int count);` | 批量设垂直表头 | `setVerticalHeaderLabels` qtablewidget.h:227 | labels 借用 |
| `XString* XTableWidget_horizontalHeaderItem(const XTableWidget* self, int column);` | 读列头文本（**未显式设置时返回 `column+1` 的 1 基序号**） | `horizontalHeaderItem` qtablewidget.h:224（Qt 返回 item 对象） | **新建，须释放** |
| `XString* XTableWidget_verticalHeaderItem(const XTableWidget* self, int row);` | 读行头文本（未设置为 `row+1`） | `verticalHeaderItem` qtablewidget.h:220 | **新建，须释放** |
| `void XTableWidget_setHorizontalHeaderItem(XTableWidget* self, int column, const XString* text);` | 设单列头（负值忽略，NULL 清空，不改列数） | `setHorizontalHeaderItem` qtablewidget.h:225 | text 借用 |
| `void XTableWidget_setVerticalHeaderItem(XTableWidget* self, int row, const XString* text);` | 设单行头 | `setVerticalHeaderItem` qtablewidget.h:221 | text 借用 |
| `XString* XTableWidget_takeHorizontalHeaderItem(XTableWidget* self, int column);` | 取出列头文本（越界/未设置 NULL） | `takeHorizontalHeaderItem` qtablewidget.h:226 | **所有权移交** |
| `XString* XTableWidget_takeVerticalHeaderItem(XTableWidget* self, int row);` | 取出行头文本 | `takeVerticalHeaderItem` qtablewidget.h:222 | **所有权移交** |

### 清空与当前格（XTableWidget.h:994-1071）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableWidget_clear(XTableWidget* self);` | 清全部条目 + 两向表头 + 解除挂载 + 清当前格/选区 + 行列归零；**不清 spans** | `clear` qtablewidget.h:274 | 表格释放 |
| `void XTableWidget_clearContents(XTableWidget* self);` | 只清条目 + 解除挂载 + 清当前格/选区；**保留行列数与表头**；不清 spans | `clearContents` qtablewidget.h:275 | 表格释放 |
| `void XTableWidget_setCurrentCell(XTableWidget* self, int row, int column);` | 设当前格；越界收敛 -1/-1；同格早退；autoScroll；发 currentItemChanged/currentCellChanged/itemSelectionChanged | `setCurrentCell` qtablewidget.h:235 | — |
| `int XTableWidget_currentRow(const XTableWidget* self);` | 当前行（无→-1） | `currentRow` qtablewidget.h:230 | — |
| `int XTableWidget_currentColumn(const XTableWidget* self);` | 当前列（无→-1） | `currentColumn` qtablewidget.h:231 | — |
| `XTableWidgetItem* XTableWidget_currentItem(const XTableWidget* self);` | 当前条目 | `currentItem` qtablewidget.h:232 | 借用禁释放 |
| `const XTableWidgetItem* XTableWidget_currentItem_const(const XTableWidget* self);` | 当前条目（const 借用） | 同上 | 借用禁释放 |
| `void XTableWidget_setCurrentItem(XTableWidget* self, const XTableWidgetItem* item);` | 由条目设当前项；非本表条目 → 清当前项 | `setCurrentItem` qtablewidget.h:233 | item 借用 |

### 索引反查与编辑（XTableWidget.h:1073-1149）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTableWidget_row(const XTableWidget* self, const XTableWidgetItem* item);` | 条目→行（游离/外部/NULL → -1） | `row` qtablewidget.h:210 | — |
| `int XTableWidget_column(const XTableWidget* self, const XTableWidgetItem* item);` | 条目→列 | `column` qtablewidget.h:211 | — |
| `int XTableWidget_indexFromItem(const XTableWidget* self, const XTableWidgetItem* item, int* column);` | 条目→行号，列由 out 写回（未命中写 -1） | `indexFromItem` qtablewidget.h:217（Qt 返回 QModelIndex） | out 调用方提供 |
| `XTableWidgetItem* XTableWidget_itemFromIndex(const XTableWidget* self, int row, int column);` | 格→条目 | `itemFromIndex` qtablewidget.h:218 | 借用禁释放 |
| `void XTableWidget_editItem(XTableWidget* self, const XTableWidgetItem* item);` | 触发编辑（非本表条目忽略） | `editItem` qtablewidget.h:242 | item 借用 |
| `void XTableWidget_openPersistentEditor(XTableWidget* self, const XTableWidgetItem* item);` | 开持久编辑器 | `openPersistentEditor` qtablewidget.h:243 | item 借用 |
| `void XTableWidget_closePersistentEditor(XTableWidget* self, const XTableWidgetItem* item);` | 关持久编辑器 | `closePersistentEditor` qtablewidget.h:244 | item 借用 |
| `bool XTableWidget_isPersistentEditorOpen(const XTableWidget* self, const XTableWidgetItem* item);` | 查编辑器是否打开 | `isPersistentEditorOpen` qtablewidget.h:246 | — |

### 排序/滚动/便捷查找（XTableWidget.h:1151-1282）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTableWidget_sortItems(XTableWidget* self, int column, int order);` | 按列 `DisplayRole` 整行重排（空格沉底；`verticalHeaderItems` 不随之重排）；同步 header sortIndicator | `sortItems` qtablewidget.h:238 | — |
| `void XTableWidget_scrollToItem(XTableWidget* self, const XTableWidgetItem* item, int hint);` | 滚动使条目可见（hint=`EnsureVisible` 为 Qt 缺省） | `scrollToItem` qtablewidget.h:269 | item 借用 |
| `int XTableWidget_findItems(const XTableWidget* self, const char* text, int flags, XTableWidgetItem** out, int maxCount);` | 文本查找，**列优先**遍历；返回命中总数（>maxCount 表示截断） | `findItems` qtablewidget.h:256 | out 数组元素为借用 |
| `XTableWidgetItem* XTableWidget_itemAt(const XTableWidget* self, int x, int y);` | 坐标→条目（表头区/空格/越界 NULL；合并区吸附原点） | `itemAt(QPoint)`/`itemAt(int,int)` qtablewidget.h:261/262 | 借用禁释放 |
| `int XTableWidget_selectedItems(const XTableWidget* self, XTableWidgetItem** out, int maxCount);` | 输出选中条目（空格跳过，行主序） | `selectedItems` qtablewidget.h:255 | out 元素借用 |
| `int XTableWidget_selectedRanges(const XTableWidget* self, XTableWidgetSelectionRange* out, int maxCount);` | 输出选中范围（逐行归并；离散空洞并入 [min,max]） | `selectedRanges` qtablewidget.h:254 | out 按值 |
| `void XTableWidget_setRangeSelected(XTableWidget* self, const XTableWidgetSelectionRange* range, bool select);` | 范围选中/取消；两角须有效否则整体忽略；**不清既有选区** | `setRangeSelected` qtablewidget.h:252 | range 借用 |
| `XRect XTableWidget_visualItemRect(const XTableWidget* self, const XTableWidgetItem* item);` | 条目视口矩形（未找到/NULL → 空矩形） | `visualItemRect` qtablewidget.h:263 | 值 |
| `int XTableWidget_visualRow(const XTableWidget* self, int logicalRow);` | 逻辑行→视觉行（经垂直表头 `visualIndex`；负值/NULL → -1） | `visualRow` qtablewidget.h:258 | — |
| `int XTableWidget_visualColumn(const XTableWidget* self, int logicalColumn);` | 逻辑列→视觉列 | `visualColumn` qtablewidget.h:259 | — |

### 信号（XTableWidget.h:1284-1315，15 个）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTableWidget_itemPressed_signal(XTableWidget* self);` | itemPressed(item*) | qtablewidget.h:278 | 标识，不得解引用 |
| `void* XTableWidget_itemClicked_signal(XTableWidget* self);` | itemClicked(item*) | qtablewidget.h:279 | 同上 |
| `void* XTableWidget_itemDoubleClicked_signal(XTableWidget* self);` | itemDoubleClicked(item*) | qtablewidget.h:280 | 同上 |
| `void* XTableWidget_itemActivated_signal(XTableWidget* self);` | itemActivated(item*) | qtablewidget.h:282 | 同上 |
| `void* XTableWidget_itemEntered_signal(XTableWidget* self);` | itemEntered(item*) | qtablewidget.h:283 | 同上 |
| `void* XTableWidget_itemChanged_signal(XTableWidget* self);` | itemChanged(item*) | qtablewidget.h:284 | 同上 |
| `void* XTableWidget_currentItemChanged_signal(XTableWidget* self);` | currentItemChanged(current, previous) | qtablewidget.h:286 | 同上 |
| `void* XTableWidget_itemSelectionChanged_signal(XTableWidget* self);` | itemSelectionChanged() | qtablewidget.h:287 | 同上 |
| `void* XTableWidget_cellPressed_signal(XTableWidget* self);` | cellPressed(row,col) | qtablewidget.h:289 | 同上 |
| `void* XTableWidget_cellClicked_signal(XTableWidget* self);` | cellClicked(row,col) | qtablewidget.h:290 | 同上 |
| `void* XTableWidget_cellDoubleClicked_signal(XTableWidget* self);` | cellDoubleClicked(row,col) | qtablewidget.h:291 | 同上 |
| `void* XTableWidget_cellActivated_signal(XTableWidget* self);` | cellActivated(row,col) | qtablewidget.h:293 | 同上 |
| `void* XTableWidget_cellEntered_signal(XTableWidget* self);` | cellEntered(row,col) | qtablewidget.h:294 | 同上 |
| `void* XTableWidget_cellChanged_signal(XTableWidget* self);` | cellChanged(row,col) | qtablewidget.h:295 | 同上 |
| `void* XTableWidget_currentCellChanged_signal(XTableWidget* self);` | currentCellChanged(curRow,curCol,prevRow,prevCol) | qtablewidget.h:297 | 同上 |

**所有权与命名契约**：`_const` 借用族＝`item_const`/`currentItem_const`（返回 const 条目指针）、`XTableWidgetItem_text_const`（返回 `const XString*`）与 `_data`/`_icon`/`_font`/`_tableWidget`；**例外**：`XTableWidgetItem_text`/`_statusTip`/`_toolTip`/`_whatsThis`/`XTableWidget_horizontalHeaderItem`/`_verticalHeaderItem`/`_take*` 无 `_const` 而返回**新建对象**（须 `XClassDelete`）。`_utf8` 设置侧＝`XTableWidgetItem_setText_utf8`/`_setStatusTip_utf8`/`_setToolTip_utf8`/`_setWhatsThis_utf8` 与查询侧 `_text_toUtf8`/`_statusTip_toUtf8`/`_toolTip_toUtf8`/`_whatsThis_toUtf8`（合规）；但 `setHorizontalHeaderLabels`/`setVerticalHeaderLabels` 直接收 `const char* const*` 无后缀（旧口径）。`_2` 重载：**无**（`_utf8` 与 `_const` 分别承担两种形态）。三态 `set*`：`XTableWidgetItem_setCheckState(int 0/1/2)` 是唯一三态，其余 set* 均为布尔/单值；`setItem(NULL)` 三态化（清空=take+delete）。

**可覆写点**：`XTableWidgetItem` 只有 `EXTableWidgetItem_Clone` 一槽（XTableWidget.h:41-42），默认回落到 `XTableWidgetItem_create_copy`（XTableWidget.c:308-319）；`XTableWidget` 的 `class_init` 13 槽——`EXClass_Deinit`、`EXClass_Copy`（根修注释：继承的 `VXTableView_copy` 不搬 `m_cells`/表头/模型，XTableWidget.c:1328-1339）、`EXClass_Move`、`EXWidget_PaintEvent`、`EXWidget_MousePressEvent`、`EXWidget_MouseDoubleClickEvent`、`EXAbstractItemView_VisualRect`、`EXAbstractItemView_ShouldEdit`、`EXWidget_KeyPressEvent`、`EXAbstractScrollArea_ScrollContentsBy`、`EXWidget_ResizeEvent`、`EXWidget_MouseMoveEvent`（XTableWidget.c:1328-1355）。无 `_base` 公开入口。

**注意事项与坑点**

1. **`findItems` 的 flags 编号与 Qt 相反**：头注释「1 = 精确相等（对齐 `Qt::MatchExactly`）；0 = 包含子串（对齐 `Qt::MatchContains`）」（XTableWidget.h:1186-1187），实现亦按 `flags == 1` 精确、否则包含（XTableWidget.c:2628-2631）；但 Qt 的真实取值是 `MatchExactly = 0`、`MatchContains = 1`（`qnamespace.h:1531-1532`）——注释保留了 Qt 的枚举**名**却给了**反值**，按 Qt 习惯传 0 会得到「包含」语义。
2. **`read()/write()` 的「未实现」说法已过时**：XGui.md:3613（§15.5 第 4 条）记「`QTableWidgetItem` 的 `read()/write()`（QDataStream）未实现」，但头文件已声明（XTableWidget.h:547、560）且实现存在（XTableWidget.c:809、838），并有 4096 role 上限的损坏防御（XTableWidget.c:847-852）与「失败不改原条目」契约（XTableWidget.h:556-557）。
3. **`setItemPrototype` 的原型实际不参与建条**：头注释称「只在后续按需『创建新条目』的路径（如 `XTableWidgetItem` 工厂）中用作克隆模板」（XTableWidget.h:851-852），实测 `rg m_itemPrototype` 只有 deinit/copy/move/get/set 六类使用点（XTableWidget.c:1407、1500-1503、1567-1592、1634、2082-2091），**无任何 clone 调用点**。
4. **`clear`/`clearContents` 的「不删 span」被两处重复声明**：头注释 XTableWidget.h:1004 与 :1014 都写「不**清除** `setSpan` 合并区间（只有 `clearSpans` 清除）」，与 XGui.md:3570 的「`clear()`/`clearContents()` 不清 spans」一致；实际清 span 的唯一入口是继承的 `XTableView_clearSpans`（`XTableWidget` 侧同名便捷接口已按 §15.2 删除，XGui.md:3531）。
5. **`setItem` 越界行为与「不泄漏」相悖**：头注释明写越界时「仍会把 item 的所属表格设为 self 但**不存入表格**……该条目不会再被表格释放」（XTableWidget.h:793-794），实现一致（`item->m_view = self` 在 `xtw_cellSlot` 判空之前，XTableWidget.c:2030-2035）——调用方在此路径上必须自己回收，否则泄漏。
6. **C++ 包含即编译失败（实测，非注释来源）**：根因在 `XWidget.h:79` 的 `extern "C" {` 无闭合（`XWidget.h` 单独编译同样报错）；本头自己的 `extern "C"`（`:18` 开、`:1319-1321` 关）配平。本头补 1 个 `}` 后 `g++ -std=c++17 -fsyntax-only $FLAGS` 通过，未补报 `expected '}' at end of input`（因嵌套，提示指向最内层的 `XTableWidget.h:18`）。C11 下 0 error。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XTableWidgetTest.c`）。间接覆盖：`Test/XGuiTest/XTableAlignTest.c` 55 处 `XTableWidget_*` 调用（`tbl_testCopyMove` 专测 copy/move 深拷贝与泄漏、`tbl_testSerializeRobustness` 测 `XTableWidgetItem` 读写），入口 `int XTableAlignTest_runAll(void)` :602，75 条 `tbl_expect` 断言；回归挂接 `xgui_regression_test.c:36242`。另 `Test/XGuiTest/XKeyboardTest.c`（36 行起以 helper `xTwSetTextUtf8` 驱动单元格编辑器弹键盘回归）、`Test/XGuiDemo/xgui_demo_apitest_views.c` 141 处、`xgui_window_demo.c` 11 处、`xgui_demo_page_views.c`/`xgui_demo_page_modbus.c` 为间接覆盖。

---

## XTextBrowser

**定位**：富文本浏览控件——继承 `XTextEdit`，恒只读；源导航（`setSource`/`backward`/`forward`/`home`/`reload`）与容量上限 50 的环形历史；`setHtml` 进入只读富文本预览并注入内容（无文件加载通道）；链接点击/悬停经基类信号转发为 `anchorClicked`/`highlighted`。继承链 `XObject ← XWidget ← XAbstractScrollArea ← XTextEdit ← XTextBrowser`（XTextBrowser.h:31-32）。头文件 `Src/XGui/Widget/XTextBrowser.h`（**292 行**）。守卫 `XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON`（XTextBrowser.h:29）；`XTEXTBROWSER_ON` 缺省 1（XGuiConfig.h:599-601），`!XWIDGET_ON || !XGUIAPPLICATION_ON || !XWINDOW_ON` 时强制 0（XGuiConfig.h:881、918-919）。

**生命周期**：`XTextBrowser_init` → `XTextEdit_init` → 挂虚表 → 编辑器 `XPlainTextEdit_setReadOnly(true)` → 关撤销重做 + `TextBrowserInteraction` → **释放并重建** `m_base.m_textDoc`（XTextBrowser.c:350-375，注释自陈「XTextEdit_init 已创建 m_textDoc：覆盖前释放旧对象（否则泄漏）」）→ 历史环 `m_historyCount=0`/`m_historyIndex=-1`/`m_historyCapacity=0` → `m_source=XString_create()` → 编辑器铺满并显式 show → `m_openLinks=true` → 装事件过滤器 + 连基类 `linkActivated`/`linkHovered`（XTextBrowser.c:376-406）；`VXTextBrowser_deinit` 先摘过滤器再释放 `m_hoverAnchor`/`m_source`/历史条目与槽位数组/`m_searchPaths`（XTextBrowser.c:183-217）；其余遵循基类 XWidget 约定。

**API 面**（Qt 行号取 `widgets/widgets/qtextbrowser.h`；`setHtml`/`cursorRect`/`anchorAt` 取 `qtextedit.h`）

### 生命周期（XTextBrowser.h:63-75）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTextBrowser_class_init(void);` | 建/取共享虚表（登记 2 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（恒只读、预览交互标志、空历史、openLinks=true） | `QTextBrowser(QWidget*)` qtextbrowser.h:31 | parent 借用 |
| `#define XTextBrowser_create(parent, flags) XTextBrowser_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |
| `XTextBrowser* XTextBrowser_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 同上 |

### 源与内容（XTextBrowser.h:77-119）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextBrowser_setSource(XTextBrowser* self, const char* url);` | 记录 URL、发 sourceChanged、滚动条归零、压历史（同栈顶 URL 早退不重复压）、发 historyChanged | `setSource(const QUrl&, ResourceType)` qtextbrowser.h:57 | url 借用 |
| `const char* XTextBrowser_source(const XTextBrowser* self);` | 当前源 URL（未设 `""`） | `source` qtextbrowser.h:34 | **借用禁释放** |
| `void XTextBrowser_setHtml(XTextBrowser* self, const char* html);` | 转基类 setHtml 后强制编辑器只读并 `setRichPreview(true)` | `setHtml` qtextedit.h:199 | html 借用 |
| `XTextBrowserSourceType XTextBrowser_sourceType(const XTextBrowser* self);` | 源类型：无历史→Unknown(-1)，否则 Url(0)；不按扩展名推断 | `sourceType` qtextbrowser.h:35（Qt 返回 `QTextDocument::ResourceType`） | — |
| `#define`/`enum XTextBrowserSourceType { _Unknown = -1, _Url = 0 }` | 源类型枚举（简化子集） | `QTextDocument::ResourceType` | — |

### 导航（XTextBrowser.h:120-188）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextBrowser_setOpenLinks(XTextBrowser* self, bool open);` / `bool XTextBrowser_openLinks(const XTextBrowser* self);` | 链接可点击（默认 true） | `setOpenLinks`/`openLinks` qtextbrowser.h:54/53 | — |
| `void XTextBrowser_backward(XTextBrowser* self);` | 后退一条（不压历史） | `backward` qtextbrowser.h:58 | — |
| `void XTextBrowser_forward(XTextBrowser* self);` | 前进一条 | `forward` qtextbrowser.h:59 | — |
| `void XTextBrowser_home(XTextBrowser* self);` | 回首页 | `home` qtextbrowser.h:60 | — |
| `void XTextBrowser_reload(XTextBrowser* self);` | 刷新当前源（同 URL 重走加载、不重复压历史） | `reload` qtextbrowser.h:61 | — |
| `void XTextBrowser_clearHistory(XTextBrowser* self);` | 只保留当前条目（重设环形起点后 count=1），发 historyChanged 并刷导航态 | `clearHistory` qtextbrowser.h:44 | — |
| `int XTextBrowser_backwardHistoryCount(const XTextBrowser* self);` | 可后退条数 | `backwardHistoryCount` qtextbrowser.h:47 | — |
| `int XTextBrowser_forwardHistoryCount(const XTextBrowser* self);` | 可前进条数 | `forwardHistoryCount` qtextbrowser.h:48 | — |
| `const char* XTextBrowser_historyTitle(const XTextBrowser* self, int index);` | 相对偏移取标题（0 当前、负后退、正前进）；**实现直接返回 URL** | `historyTitle` qtextbrowser.h:45 | 借用（条目变化前有效） |
| `const char* XTextBrowser_historyUrl(const XTextBrowser* self, int index);` | 相对偏移取 URL | `historyUrl` qtextbrowser.h:46 | 借用 |
| `bool XTextBrowser_isBackwardAvailable(const XTextBrowser* self);` | 实时按历史栈计算 | `isBackwardAvailable` qtextbrowser.h:42 | — |
| `bool XTextBrowser_isForwardAvailable(const XTextBrowser* self);` | 实时计算 | `isForwardAvailable` qtextbrowser.h:43 | — |

### 外链/搜索路径/度量（XTextBrowser.h:190-240）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextBrowser_setOpenExternalLinks(XTextBrowser* self, bool open);` / `bool XTextBrowser_openExternalLinks(const XTextBrowser* self);` | 外链交桌面打开（默认 false） | `setOpenExternalLinks`/`openExternalLinks` qtextbrowser.h:51/50 | — |
| `void XTextBrowser_setSearchPaths(XTextBrowser* self, const XStringList* paths);` | 深拷贝保存（NULL 清空；拷贝失败保留原表） | `setSearchPaths` qtextbrowser.h:38 | paths 借用，内部深拷贝 |
| `XStringList* XTextBrowser_searchPaths(const XTextBrowser* self);` | **新建深拷贝列表**（内部空也返回新建空表） | `searchPaths` qtextbrowser.h:37 | **拷贝须释放**（`XClassDelete`） |
| `XRect XTextBrowser_cursorRect(const XTextBrowser* self);` | 光标竖线矩形（委托 `XPlainTextEdit_cursorRect`；行高 16px、左留白 2px） | `cursorRect()` qtextedit.h:153 | 值 |
| `XString* XTextBrowser_anchorAt(const XTextBrowser* self, const XPoint* pos);` | 坐标处锚点 href（委托 `XTextEdit_anchorAt`） | `anchorAt` qtextedit.h:155（Qt 返回 QString） | **新建，须释放**；失败 NULL |

### 信号（XTextBrowser.h:242-285，6 个）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url);` | 源变化（真发射，XTextBrowser.c:488-489） | `sourceChanged` qtextbrowser.h:67 | 标识，不得解引用 |
| `void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available);` | 后退可用变化（仅在变化时发） | `backwardAvailable` qtextbrowser.h:64 | 同上 |
| `void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available);` | 前进可用变化 | `forwardAvailable` qtextbrowser.h:65 | 同上 |
| `void* XTextBrowser_historyChanged_signal(XTextBrowser* self);` | 历史变化（setSource 追加 / clearHistory 清空） | `historyChanged` qtextbrowser.h:66 | 同上 |
| `void* XTextBrowser_anchorClicked_signal(XTextBrowser* self, const char* url);` | 锚点点击（eventFilter 命中发；openLinks 决定是否导航） | `anchorClicked` qtextbrowser.h:69 | 同上 |
| `void* XTextBrowser_highlighted_signal(XTextBrowser* self, const char* url);` | 悬停链接（离开载荷空串） | `highlighted` qtextbrowser.h:68 | 同上 |

**所有权与命名契约**：`_const` 借用族＝`source`/`historyTitle`/`historyUrl`（返回 `const char*` 内部借用）、`openLinks`/`openExternalLinks`/`isBackwardAvailable`/`isForwardAvailable`/`sourceType`（const self）；**例外**：`searchPaths`（新建 `XStringList*`）、`anchorAt`（新建 `XString*`）无 `_const` 且须释放。`_utf8` 后缀族不存在（`setSource`/`setHtml` 直接收 `const char*`，旧口径）。`_2` 重载无。三态 `set*` 无（`setOpenLinks`/`setOpenExternalLinks` 单值 bool）。

**可覆写点**：`class_init` 2 槽——`EXClass_Deinit → VXTextBrowser_deinit`、`EXObject_EventFilter → VX_browser_eventFilter`（监视内嵌编辑器：按下命中锚点发 `anchorClicked` + 按开关导航/外开；移动发 `highlighted`，恒返回 false 不过滤，XTextBrowser.c:271-340）。其余行为（绘制/输入/预览）全走 `XTextEdit`。无 `_base` 公开入口。

**注意事项与坑点**

1. **`highlighted` 有两条发射路径，头注释只讲一条**：头注释写「真实发射点：预览态鼠标进入/切换/离开链接（URL 变化）时经基类 linkHovered → xtb_linkHoveredForward 转发发射」（XTextBrowser.h:277-279）；实现另有 eventFilter 内的 MOUSE_MOVE 直发路径（`xtb_emitText(browser, highlighted_signal, …)`，XTextBrowser.c:311-338），而 init 注释称「编辑器隐藏后过滤器路径自然静默，无重复发射」（XTextBrowser.c:397-399）——两条路径并存时需要依赖「预览态编辑器不可见」这一前提，头文件未提示。
2. **`sourceType` 是历史条数函数，不是 URL 推断**：头注释与实现都写明「有导航条目时恒返回 Url，不按扩展名推断（Qt 对 .md/.markdown 推断 MarkdownResource）」（XTextBrowser.h:107-117、XTextBrowser.c:434-437）；判据是 `m_historyCount <= 0 || m_historyIndex < 0` —— `clearHistory` 保留 1 条时仍返回 Url，`setSource` 之后才变 Url。
3. **`historyTitle` 名不副实**：头注释已登记「历史条目未解析文档标题，标题返回对应条目的源 URL 字符串」（XTextBrowser.h:170-171），实现是一行 `return XTextBrowser_historyUrl(self, index);`（XTextBrowser.c:733-736）。
4. **`setHtml` 会覆盖从基类继承的只读状态**：`setHtml` 每次都 `XPlainTextEdit_setReadOnly(..., true)` + `setRichPreview(true)`（XTextBrowser.c:449-460），即浏览器「恒只读」不依赖调用方；但 `init` 里重建 `m_textDoc` 时先 `XClassDelete` 旧对象（XTextBrowser.c:366-370），若外层先取过 `XTextEdit_document` 指针，该指针在 `XTextBrowser_init` 后即悬垂（此路径只在 init 内发生）。
5. **C++ 包含即编译失败（实测，非注释来源）**：本头自身在 `XTextBrowser.h:20` 开 `extern "C" {` 后无闭合（本文件 `{`/`}` 计数 3/2），叠加包含链上 `XWidget.h:79` 与 `XPlainTextEdit.h:44` 两个未闭合块，`g++ -std=c++17 -fsyntax-only` 需补 **3** 个 `}` 才通过（实测 1..3 递补，2 个仍报错）；未补时错误提示分别指向 `XPlainTextEdit.h:44`、`XTextEdit.h:19`、`XTextBrowser.h:20` 三处 `extern "C" {`。C11 下 0 error。

**测试覆盖**：**无专属套件**。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_text.c` 64 处 `XTextBrowser_*` 调用（该文件总 2520 行，同时覆盖 XTextEdit 199 处、XTextDocument 85 处）；`Test/XGuiDemo/xgui_window_demo.c` 1 处。

---

## XTextDocument

**定位**：富文本文档模型（非控件，纯 `XObject`）——块（block，含段落格式：对齐/缩进/列表级别/标题级）+ 片段（fragment，含内联字符格式 `XTDCharFormat`：粗/斜/下划线/删除线/上下标/字色/背景色/字体族/字号/锚点 href/行内图片）；`setHtml`/`appendHtml` 解析渲染子集，`toHtml` 反向生成，`toPlainText`/`toRawText` 导出；含撤销/重做快照栈、块数上限、元信息、按名资源表、查找、字符访问、`mightBeRichText`/`convertFromPlainText` 两个命名空间函数与 9 个信号。继承链 `XObject ← XTextDocument`（XTextDocument.h:169-170）。头文件 `Src/XGui/Widget/XTextDocument.h`（**502 行**）。守卫 `#if XTEXTDOCUMENT_ON`（XTextDocument.h:46），缺省 1（XGuiConfig.h:617-619）。

**生命周期**：`XTextDocument_init` 建空块表与实例级撤销/重做栈、`m_modified=0`、`m_maximumBlockCount=0`、`m_cursorPosition=0`、`m_undoRedoEnabled`/`m_modifiedFlag`/边沿检测位全 0（XTextDocument.c:100-130）；`XTextDocument_create_ex` 堆创建（XTextDocument.c:132）；`VX_td_deinit` 释放每块片段/块格式、块数组、4 个元信息 `XString`、`m_baseUrl`、默认格式与 16 条资源（XTextDocument.c:39-98）；栈对象与 `XClassDeinit` 成对、堆对象 `XClassDelete`。硬上限：`XTD_MAX_FRAGMENTS_PER_BLOCK`=64、`XTD_MAX_BLOCKS`=256、`XTD_MAX_RESOURCES`=16、`XTD_MAX_UNDO_STEPS`=50（XTextDocument.h:48-51）。

**API 面**（Qt 行号取 `gui/text/qtextdocument.h`）

### 生命周期与块操作（XTextDocument.h:216-225）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTextDocument_class_init(void);` | 建/取共享虚表（仅 Deinit 槽） | 无公开对应 | 共享表，禁释放 |
| `void XTextDocument_init(XTextDocument* self);` | 就地初始化（1 个空块 + 容量 16、撤销栈实例持有） | `QTextDocument(QObject*)` qtextdocument.h:78 | — |
| `#define XTextDocument_create() XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)` | 默认内存类型建堆对象（**无参宏**） | 同上 | 调用方拥有，`XClassDelete` |
| `XTextDocument* XTextDocument_create_ex(XMemoryType memory);` | 指定内存类型建堆对象 | 同上 | 同上 |
| `void XTextDocument_clear(XTextDocument* self);` | 清空（块、撤销历史、修订计数复位） | `clear()` qtextdocument.h:85 | — |
| `bool XTextDocument_isEmpty(const XTextDocument* self);` | 无内容 | `isEmpty` qtextdocument.h:84 | — |
| `int XTextDocument_blockCount(const XTextDocument* self);` | 块数 | `blockCount` qtextdocument.h:241 | — |
| `int XTextDocument_characterCount(const XTextDocument* self);` | 字符数（按 UTF-8 字节累加，XTextDocument.c） | `characterCount` qtextdocument.h:243（Qt 含块分隔符） | — |

### 文本导出（XTextDocument.h:227-252）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `char* XTextDocument_toPlainText(const XTextDocument* self);` | 导出纯文本（块间 `\n`） | `toPlainText` qtextdocument.h:134 | **堆分配，`XFree_System`** |
| `void XTextDocument_setPlainText(XTextDocument* self, const char* utf8);` | 设纯文本（先 clear 复位撤销历史，再逐行建块） | `setPlainText` qtextdocument.h:135 | utf8 借用 |
| `char* XTextDocument_toHtml(const XTextDocument* self);` | 导出 HTML（含实体转义，与解析器互逆） | `toHtml` qtextdocument.h:111 | **堆分配，`XFree_System`** |
| `void XTextDocument_setHtml(XTextDocument* self, const char* html);` | 解析渲染子集（NULL 忽略；完成发 contentsChanged） | `setHtml` qtextdocument.h:112 | html 借用 |
| `char* XTextDocument_toRawText(const XTextDocument* self);` | 导出原始纯文本（子集内与 toPlainText 等价，转调之） | `toRawText` qtextdocument.h:133 | **堆分配，`XFree_System`** |
| `XTextDocument* XTextDocument_clone(const XTextDocument* self);` | 深拷贝（块/片段/图片/块格式/title/url/cssMedia/默认格式/资源；**不拷**撤销历史、baseUrl、frontMatter） | `clone(QObject*)` qtextdocument.h:82 | **新文档须释放** |

### 块级格式与片段（XTextDocument.h:254-287）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextDocument_setBlockAlignment(XTextDocument* self, int blockIndex, int alignment);` | 设块对齐（`XTDAlignment` 位组合） | `QTextBlockFormat::setAlignment`（Qt 走 QTextCursor） | — |
| `int XTextDocument_blockAlignment(const XTextDocument* self, int blockIndex);` | 读块对齐 | 同上 | — |
| `void XTextDocument_setBlockHeadingLevel(XTextDocument* self, int blockIndex, int level);` | 设标题级 0..6（h1-h6） | 无直接对应（Qt 用块格式/字符格式） | — |
| `int XTextDocument_addFragment(XTextDocument* self, int blockIndex, const char* text, const XTDCharFormat* fmt);` | 追加片段，返回片段索引（失败 -1） | 无直接对应（Qt 走 QTextCursor::insertText） | text/fmt 借用，内部深拷贝 |
| `int XTextDocument_fragmentCount(const XTextDocument* self, int blockIndex);` | 块内片段数 | 同上 | — |
| `const XTDFragment* XTextDocument_fragment(const XTextDocument* self, int blockIndex, int fragIndex);` | 读片段 | 同上 | **借用**（随文档修改失效） |
| `void XTextDocument_insertText(XTextDocument* self, int blockIndex, int fragIndex, int charOffset, const char* text, const XTDCharFormat* fmt);` | 在片段偏移处插文本 | `QTextCursor::insertText` | text/fmt 借用 |
| `void XTextDocument_appendBlock(XTextDocument* self, const XTDCharFormat* fmt);` | 追加空块 | 无直接对应 | fmt 借用 |
| `void XTextDocument_appendText(XTextDocument* self, const char* text, const XTDCharFormat* fmt);` | 末块尾部追加文本 | 同上 | text/fmt 借用 |
| `void XTextDocument_appendHtml(XTextDocument* self, const char* html);` | 尾部追加 HTML（末块非空则另起段；完成发 contentsChanged） | 无直接对应 | html 借用 |
| `int XTextDocument_insertImage(XTextDocument* self, const XImage* image);` | 末块尾部插行内图片（深拷贝；按原尺寸、底边贴基线），返回片段索引，失败 -1 | `QTextCursor::insertImage` | image 借用，内部深拷贝 |

### 元信息/修改状态/块查询（XTextDocument.h:289-347）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextDocument_setMetaInformation(XTextDocument* self, int info, const char* value);` | 设元信息（0 标题/1 URL/2 CssMedia/3 FrontMatter；NULL 忽略） | `setMetaInformation` qtextdocument.h:107 | value 借用 |
| `const char* XTextDocument_metaInformation(const XTextDocument* self, int info);` | 读元信息（无则 `""`） | `metaInformation` qtextdocument.h:108 | **借用禁释放** |
| `bool XTextDocument_isModified(const XTextDocument* self);` | 修改状态（默认 false） | `isModified` qtextdocument.h:187 | — |
| `void XTextDocument_setModified(XTextDocument* self, bool m);` | 置修改状态（翻转发 modificationChanged） | `setModified` qtextdocument.h:288 | — |
| `int XTextDocument_revision(const XTextDocument* self);` | 修订计数（每次内容变化递增；setModified 不影响；clear 归 0） | `revision` qtextdocument.h:96 | — |
| `int XTextDocument_lineCount(const XTextDocument* self);` | 行数（子集无折行：恒等于 blockCount） | `lineCount` qtextdocument.h:242 | — |
| `const XTDBlock* XTextDocument_findBlock(const XTextDocument* self, int position);` | 按字符位置定位块（负值取首块、越过尾取末块） | `findBlock` qtextdocument.h:161 | **借用**（随修改失效） |
| `const XTDBlock* XTextDocument_findBlockByNumber(const XTextDocument* self, int blockNumber);` | 按块号取块（越界 NULL） | `findBlockByNumber` qtextdocument.h:162 | 借用 |
| `const XTDBlock* XTextDocument_findBlockByLineNumber(const XTextDocument* self, int lineNumber);` | 按行号取块（与块号一一对应） | `findBlockByLineNumber` qtextdocument.h:163 | 借用 |
| `const XTDBlock* XTextDocument_firstBlock(const XTextDocument* self);` | 首块 | `firstBlock` qtextdocument.h:167 | 借用 |
| `const XTDBlock* XTextDocument_lastBlock(const XTextDocument* self);` | 末块 | `lastBlock` qtextdocument.h:168 | 借用 |
| `int XTextDocument_maximumBlockCount(const XTextDocument* self);` | 块数上限（0=不限） | `maximumBlockCount` qtextdocument.h:260 | — |
| `void XTextDocument_setMaximumBlockCount(XTextDocument* self, int maximum);` | 设上限：立即截断开头超出块 + **同时禁用撤销/重做**（Qt 同语义），负值按 0 | `setMaximumBlockCount` qtextdocument.h:261 | — |

### baseUrl / 资源表（XTextDocument.h:349-376）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `const char* XTextDocument_baseUrl(const XTextDocument* self);` | 读 baseUrl（未设 `""`） | `baseUrl` qtextdocument.h:266 | **借用禁释放** |
| `void XTextDocument_setBaseUrl(XTextDocument* self, const char* url);` | 设 baseUrl（变化发 baseUrlChanged） | `setBaseUrl` qtextdocument.h:267 | url 借用 |
| `void XTextDocument_addResource(XTextDocument* self, int type, const char* name, const void* resource);` | 注册按名资源（ImageResource 收 `const XImage*`、文本型收 `const char*`；同 type+name 替换；NULL 移除；表满忽略） | `addResource(int, const QUrl&, const QVariant&)` qtextdocument.h:205 | resource 借用，内部深拷贝 |
| `const void* XTextDocument_resource(const XTextDocument* self, int type, const char* name);` | 查资源（Image→`const XImage*`、文本→`const char*`；未命中 NULL） | `resource` qtextdocument.h:204 | **借用禁释放** |

### 撤销/重做与默认格式（XTextDocument.h:378-401）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextDocument_setUndoRedoEnabled(XTextDocument* self, bool enable);` / `bool XTextDocument_isUndoRedoEnabled(const XTextDocument* self);` | 撤销/重做开关 | `setUndoRedoEnabled`/`isUndoRedoEnabled` qtextdocument.h:87/88 | — |
| `bool XTextDocument_isUndoAvailable(const XTextDocument* self);` / `bool XTextDocument_isRedoAvailable(const XTextDocument* self);` | 可用性查询 | qtextdocument.h:90/91 | — |
| `void XTextDocument_clearUndoRedoStacks(XTextDocument* self, int stacks);` | 清指定栈（`XTDStacks` 位组合；可用性变化发 undoAvailable(false)/redoAvailable(false)） | `clearUndoRedoStacks` qtextdocument.h:258 | — |
| `int XTextDocument_availableUndoSteps(const XTextDocument* self);` / `int XTextDocument_availableRedoSteps(const XTextDocument* self);` | 步数（禁用时 0） | qtextdocument.h:93/94 | — |
| `void XTextDocument_setDefaultFormat(XTextDocument* self, const XTDCharFormat* fmt);` | 设文档默认字符格式（深拷贝字符串字段） | `setDefaultFont` qtextdocument.h:173（承载差异：格式子集） | fmt 借用 |
| `const XTDCharFormat* XTextDocument_defaultFormat(const XTextDocument* self);` | 读默认格式（NULL→NULL） | `defaultFont` qtextdocument.h:174 | **借用禁释放** |

### 信号/段落与片段细节/查找/命名空间函数（XTextDocument.h:403-493）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTextDocument_contentsChanged_signal(XTextDocument* self);` | contentsChanged() | qtextdocument.h:274 | 标识，不得解引用 |
| `void* XTextDocument_contentsChange_signal(XTextDocument* self, int from, int charsRemoved, int charsAdded);` | contentsChange(from,removed,added)，**先于** contentsChanged；`from` 子集恒 0 | qtextdocument.h:273 | 同上 |
| `void* XTextDocument_blockCountChanged_signal(XTextDocument* self, int newCount);` | blockCountChanged(int) | qtextdocument.h:280 | 同上 |
| `void* XTextDocument_modificationChanged_signal(XTextDocument* self, bool modified);` | modificationChanged(bool) | qtextdocument.h:278 | 同上 |
| `void* XTextDocument_baseUrlChanged_signal(XTextDocument* self);` | baseUrlChanged() | qtextdocument.h:281 | 同上 |
| `void* XTextDocument_cursorPositionChanged_signal(XTextDocument* self);` | cursorPositionChanged() | qtextdocument.h:279（Qt 携 QTextCursor） | 同上 |
| `void* XTextDocument_documentLayoutChanged_signal(XTextDocument* self);` | documentLayoutChanged() | qtextdocument.h:282 | 同上 |
| `void* XTextDocument_redoAvailable_signal(XTextDocument* self, bool available);` | redoAvailable(bool) | qtextdocument.h:276 | 同上 |
| `void* XTextDocument_undoAvailable_signal(XTextDocument* self, bool available);` | undoAvailable(bool) | qtextdocument.h:275 | 同上 |
| `void* XTextDocument_undoCommandAdded_signal(XTextDocument* self);` | undoCommandAdded() | qtextdocument.h:277 | 同上 |
| `void XTextDocument_setBlockIndentLevel(XTextDocument* self, int blockIndex, int level);` / `int XTextDocument_blockIndentLevel(const XTextDocument* self, int blockIndex);` | 列表缩进级别（0=非列表） | 无直接对应（Qt 走 QTextList） | — |
| `void XTextDocument_setBlockListItem(XTextDocument* self, int blockIndex, bool isItem, bool ordered);` | 标记块为列表项/有序 | 无直接对应 | — |
| `void XTextDocument_setFragmentBold(XTextDocument* self, int bi, int fi, bool bold);` | 片段粗体 | `QTextCharFormat::setFontWeight` | — |
| `void XTextDocument_setFragmentItalic(XTextDocument* self, int bi, int fi, bool italic);` | 片段斜体 | `setFontItalic` | — |
| `void XTextDocument_setFragmentUnderline(XTextDocument* self, int bi, int fi, bool underline);` | 下划线 | `setFontUnderline` | — |
| `void XTextDocument_setFragmentStrikeOut(XTextDocument* self, int bi, int fi, bool strikeOut);` | 删除线 | `setFontStrikeOut` | — |
| `void XTextDocument_setFragmentFgColor(XTextDocument* self, int bi, int fi, uint32_t color);` | 前景色 ARGB | `setForeground` | — |
| `void XTextDocument_setFragmentBgColor(XTextDocument* self, int bi, int fi, uint32_t color);` | 背景色 ARGB | `setBackground` | — |
| `void XTextDocument_setFragmentFontFamily(XTextDocument* self, int bi, int fi, const char* family);` | 字体族 | `setFontFamily` | family 借用 |
| `void XTextDocument_setFragmentFontSize(XTextDocument* self, int bi, int fi, int size);` | 字号（磅，解析钳制 ≤96） | `setFontPointSize` | — |
| `void XTextDocument_setCursorPosition(XTextDocument* self, int position);` / `int XTextDocument_cursorPosition(const XTextDocument* self);` | 光标字符位置（越界钳位；变化发 cursorPositionChanged） | `QTextCursor::setPosition` | — |
| `int XTextDocument_find(const XTextDocument* self, const char* text);` | 查找（等价 `find_ex(text,0,0)`；默认大小写不敏感），返回字符位置，未命中 -1 | `find(const QString&, int, FindFlags)` qtextdocument.h:147（Qt 返回 QTextCursor） | text 借用 |
| `int XTextDocument_find_ex(const XTextDocument* self, const char* text, int from, int flags);` | 带选项查找（`XTDFindFlag`：Backward/CaseSensitively/WholeWords） | 同上 | text 借用 |
| `char XTextDocument_characterAt(const XTextDocument* self, int position);` | 取指定位置**单字节**（越界 `'\0'`） | `characterAt` qtextdocument.h:137（返回 QChar） | — |
| `void XTextDocument_undo(XTextDocument* self);` / `void XTextDocument_redo(XTextDocument* self);` | 撤销/重做一步（快照栈） | `undo()`/`redo()` qtextdocument.h:285/286 | — |
| `bool XTextDocument_mightBeRichText(const char* text);` | 富文本启发式（跳过行首空白与 `<?xml?>` 前缀，已知元素名表命中即真） | `Qt::mightBeRichText` qtextdocument.h:39 | text 借用 |
| `char* XTextDocument_convertFromPlainText(const char* plain, int mode);` | 纯文本转 HTML（`XTDWhiteSpaceMode`：Pre 为默认；转义 `<>&`、`\n`→`<br>`、Pre 空白用 `&nbsp;`、Tab 展开 8 列） | `Qt::convertFromPlainText` qtextdocument.h:42 | **堆分配，`XFree_System`** |

**所有权与命名契约**：`_const` 借用族＝`metaInformation`/`baseUrl`/`firstBlock`/`lastBlock`/`findBlock*`/`fragment`/`defaultFormat`/`blockAlignment`/`blockIndentLevel`/`characterCount` 等 const self getter，返回内部指针（文档修改后失效）；`resource` 返回 `const void*` 借用（Image 时是 `const XImage*`）。返回 `char*` 的 5 个导出函数（`toPlainText`/`toHtml`/`toRawText`/`convertFromPlainText`，以及 `clone` 返回 `XTextDocument*`）**由调用方释放**（头注释明确写 `XFree_System`：XTextDocument.h:242、479、250）。`_utf8` 后缀族不存在——`setPlainText`/`setHtml`/`setBaseUrl`/`setMetaInformation` 直收 `const char*`（旧口径）。`_2` 重载无；`_ex` 后缀用于「带 options 的同功能变体」（`find_ex`），非 `_2`。三态 `set*`：`XTextDocument_setModified(bool)` 二态，`setMaximumBlockCount(<=0 取消)` 为「值或 0=不限」单值约定。

**可覆写点**：`class_init` 仅 `EXClass_Deinit → VX_td_deinit`（XTextDocument.c:33-39），无其它虚槽。`clone` 是普通函数（非虚）；条目级多态仅存在于 `XTableWidgetItem`。无 `_base` 公开入口。

**注意事项与坑点**

1. **同一头文件内「嵌套上限」自相矛盾**：文件头写「setHtml 解析 HTML 渲染子集（**内联嵌套上限两层：栈深 3**，三层格式如 `<b><i><u>` 同时生效）」（XTextDocument.h:8-9），而 `setHtml` 函数注释写「设置 HTML 内容（渲染子集；**嵌套上限一层**）」（XTextDocument.h:231）。实现为 `#define XTD_INLINE_STACK 3`（XTextDocument.c:580），超限时整标签忽略并计数（XTextDocument.c:976-984）——即文件头口径正确，函数注释错误。XGui.md:107-113 与实现一致（「内联嵌套上限两层」）。
2. **`XTD_MAX_BLOCKS`=256 的静默截断**：头文件只声明常量（XTextDocument.h:49），未写行为；实现 5 处硬钳（XTextDocument.c:520、866、951、1078、1628），`setHtml`/`appendHtml`/`setPlainText` 超过 256 块的部分被**静默丢弃**，且 `appendBlock` 在 `blockIdx+1 >= XTD_MAX_BLOCKS` 时直接 return（XTextDocument.c:951）。
3. **`characterAt` 返回单字节而非字符**：头注释「@return UTF-8 字符（单字符缓冲）；越界返回 `'\0'`」（XTextDocument.h:455-458），实现把整篇 `toPlainText` 导出后按**字节**取 `plain[position]`（XTextDocument.c:2056-2069）——多字节 UTF-8 字符只返回首字节，且每次调用一次 O(n) 堆分配 + 整篇拷贝。
4. **`clone` 的「不拷贝项」头注释漏了 frontMatter**：头注释只写「不拷贝撤销/重做历史与 baseUrl（与 Qt clone 同口径）」（XTextDocument.h:246-248），实现注释写明「baseUrl 与 frontMatter 不拷贝——Qt 6.8 clone 同口径」（XTextDocument.c:2363-2365）——`m_frontMatter`（metaInformation 3）也不会被克隆，头文件未提。
5. **`setMaximumBlockCount` 有副作用且不可逆**：头注释已写「设置同时禁用撤销/重做历史（Qt 同语义）；负值按 0（不限）处理」（XTextDocument.h:341-345），实现 `XTextDocument_setUndoRedoEnabled(self, false)`（XTextDocument.c:2190-2191）——把上限设回 0 不会恢复撤销能力。
6. **本文件不含 `XWidget.h`，故 C++ 编译是本批两个零修补通过者之一（另一个是 `XTabWidget.h`，靠多余的 `}` 抵消，见该节坑点 3）**：`g++ -std=c++17 -fsyntax-only $FLAGS` rc=0，`gcc -std=c11` 亦 0 error——因为 `XTextDocument.h` 只含 `XObject.h`/`XEvent.h`/`XVarList.h`/`XString.h`/`XImage.h`（XTextDocument.h:39-44），不触发 `XWidget.h:79` 的未闭合块。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XTextDocumentTest.c`）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_text.c` 85 处 `XTextDocument_*` 调用（与 XTextEdit/XTextBrowser 同文件），覆盖富文本解析/导出/查找/撤销族。

---

## XTextEdit

**定位**：富文本编辑控件——继承 `XAbstractScrollArea`，内嵌 `XPlainTextEdit` 承载**纯文本**编辑（所见即所存，标签不进缓冲），富文本以只读预览模式承载（`setHtml`/显式 `setRichPreview(true)` 进入：隐藏编辑器、壳逐块绘制 b/i/u/s、font color/size、br、p align、a href，嵌套上限一层）；含逐块字符格式位集、`toHtml`/`toPlainText`/`toMarkdown` 导出、光标几何（`cursorRect`/`cursorForPosition`）、锚点命中（`anchorAt`）、文本光标平铺行列承载、额外选择集、右键菜单。继承链 `XObject ← XWidget ← XAbstractScrollArea ← XTextEdit`（XTextEdit.h:30-31）。头文件 `Src/XGui/Widget/XTextEdit.h`（**918 行**）。守卫 `XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON`（XTextEdit.h:28）；`XTEXTEDIT_ON` 缺省 1（XGuiConfig.h:605-607），条件裁剪时置 0（XGuiConfig.h:921-922）。

**生命周期**：`XTextEdit_init` 全零 → `XAbstractScrollArea_init` → 建内嵌 `XPlainTextEdit`（父为本控件，200×100）→ 自标注 `InputMethodEnabled` + `ImhMultiLine` → 连编辑器信号桥 → 建内部 `XTextDocument`（`m_textDocOwned=true`）→ 挂虚表 → `m_textColor=0xFF000000`、`m_alignment=1`、字体族/标题/markdown 三个 `XString`、`m_fontWeight=400`、`m_fontPointSize=10.0`、`m_tabStopDistance=80.0`、`m_cursorWidth=1`、`m_lineWrapMode=WidgetWidth`、`m_wordWrapMode=WordBoundaryOrAnywhere`、`m_acceptRichText=true`、`m_autoFormatting=0`、`m_centerOnScroll=false`、`m_richPreview=false`（XTextEdit.c:1342-1400）；`VXTextEdit_deinit`（XTextEdit.c:1197）释放自有 `XString`/内部文档（`m_textDocOwned` 才释放）/编辑器；其余遵循基类 Widget 约定，`_create`/`_create_ex` 后 `XClassDelete`。

**API 面**（Qt 行号取 `widgets/widgets/qtextedit.h`）

### 生命周期与字符格式（XTextEdit.h:95-170）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTextEdit_class_init(void);` | 建/取共享虚表（登记 9-10 槽，含 IME 查询） | 无公开对应 | 共享表，禁释放 |
| `void XTextEdit_init(XTextEdit* self, XWidget* parent, XWidgetFlags flags);` | 就地初始化（内嵌编辑器 + 内部文档 + 默认格式） | `QTextEdit(QWidget*)` qtextedit.h:71 | parent 借用 |
| `#define XTextEdit_create(parent, flags) XTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，`XClassDelete` |
| `XTextEdit* XTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 同上 |
| `void XTextEdit_setBold(XTextEdit* self, bool bold);` / `bool XTextEdit_isBold(const XTextEdit* self);` | 当前粗体 | **X 扩展**（QTextEdit 6.8 无 setBold/isBold，`rg` 零命中；Qt 走 `mergeCurrentCharFormat`） | — |
| `void XTextEdit_setItalic(XTextEdit* self, bool italic);` / `bool XTextEdit_isItalic(const XTextEdit* self);` | 当前斜体 | `setFontItalic`/`fontItalic` qtextedit.h:191/94 | — |
| `void XTextEdit_setUnderline(XTextEdit* self, bool underline);` / `bool XTextEdit_isUnderline(const XTextEdit* self);` | 当前下划线 | `setFontUnderline`/`fontUnderline` qtextedit.h:190/93 | — |
| `#define XTextEdit_fontItalic(self)` / `#define XTextEdit_setFontItalic(self, italic)` | 别名宏 → `isItalic`/`setItalic` | qtextedit.h:94/191 | — |
| `#define XTextEdit_fontUnderline(self)` / `#define XTextEdit_setFontUnderline(self, underline)` | 别名宏 → `isUnderline`/`setUnderline` | qtextedit.h:93/190 | — |
| `void XTextEdit_setTextColor(XTextEdit* self, uint32_t color);` / `uint32_t XTextEdit_textColor(const XTextEdit* self);` | 文字颜色 ARGB | `setTextColor`/`textColor` qtextedit.h:192/95 | — |
| `void XTextEdit_setAlignment(XTextEdit* self, int alignment);` / `int XTextEdit_alignment(const XTextEdit* self);` | 当前对齐位组合 | `setAlignment`/`alignment` qtextedit.h:195/98 | — |

### 撤销/重做与 HTML（XTextEdit.h:172-249）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextEdit_undo(XTextEdit* self);` / `void XTextEdit_redo(XTextEdit* self);` | 撤销/重做（委托内嵌编辑器快照栈） | `undo`/`redo` qtextedit.h:212/213 | — |
| `bool XTextEdit_canUndo(const XTextEdit* self);` / `bool XTextEdit_canRedo(const XTextEdit* self);` | 快照栈非空查询 | **X 扩展**（`rg canUndo\|canRedo qtextedit.h` 零命中） | — |
| `void XTextEdit_setHtml(XTextEdit* self, const char* html);` | 解析渲染子写入富文本文档 + 编辑缓冲剥离标签（**不自动进预览**） | `setHtml` qtextedit.h:199 | html 借用 |
| `char* XTextEdit_toHtml(const XTextEdit* self);` | 导出 HTML（预览态以文档为源；编辑态先以编辑器纯文本刷文档再导出） | `toHtml` qtextedit.h:137 | **堆分配，`XFree_System`** |
| `void XTextEdit_setRichPreview(XTextEdit* self, bool on);` | 只读预览开关（true 隐藏编辑器 + 逐块绘制；false 回编辑态） | 无直接对应（Qt 无独立预览开关） | — |
| `bool XTextEdit_isRichPreview(const XTextEdit* self);` | 查预览态（NULL→false） | 无直接对应 | — |
| `XString* XTextEdit_toPlainText(const XTextEdit* self);` | 导出纯文本（去掉全部标记） | `toPlainText` qtextedit.h:135 | **新建 XString，须释放**（NULL self 返回空串对象） |
| `void XTextEdit_setText(XTextEdit* self, const char* text);` | 设文本（`mightBeRichText` 启发式探测：像 HTML 走 setHtml，否则纯文本 + 复位格式） | `setText` qtextedit.h:204 | text 借用 |

### 信号（XTextEdit.h:251-316）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTextEdit_textChanged_signal(XTextEdit* self);` | textChanged()，真发射（编辑器信号桥转接） | qtextedit.h:231 | 标识，不得解引用 |
| `void* XTextEdit_currentCharFormatChanged_signal(XTextEdit* self);` | 光标格式变化（Task 2.3 接线） | qtextedit.h:234 | 同上 |
| `void* XTextEdit_linkHovered_signal(XTextEdit* self, const char* url);` | 悬停链接（离开为空串）；发射点＝预览态壳鼠标移动 | `QLabel::linkHovered`（Qt QTextEdit 无此信号） | 同上 |
| `void* XTextEdit_linkActivated_signal(XTextEdit* self, const char* url);` | 激活链接（按下与释放同链）；发射点＝预览态壳鼠标处理 | `QLabel::linkActivated` | 同上 |
| `void* XTextEdit_copyAvailable_signal(XTextEdit* self);` | copyAvailable(bool) | qtextedit.h:235 | 同上 |
| `void* XTextEdit_cursorPositionChanged_signal(XTextEdit* self);` | cursorPositionChanged() | qtextedit.h:237 | 同上 |
| `void* XTextEdit_modificationChanged_signal(XTextEdit* self);` | modificationChanged(bool) | qtextedit.h（QTextDocument 侧 :278） | 同上 |
| `void* XTextEdit_redoAvailable_signal(XTextEdit* self);` | redoAvailable(bool) | qtextedit.h:233 | 同上 |
| `void* XTextEdit_selectionChanged_signal(XTextEdit* self);` | selectionChanged() | qtextedit.h:236 | 同上 |
| `void* XTextEdit_undoAvailable_signal(XTextEdit* self);` | undoAvailable(bool) | qtextedit.h:232 | 同上 |

### 编辑动作与属性族（XTextEdit.h:317-549）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextEdit_append(XTextEdit* self, const char* text);` | 追加文本（退出预览） | `append` qtextedit.h:223 | text 借用 |
| `void XTextEdit_copy_2(XTextEdit* self);` / `void XTextEdit_cut_2(XTextEdit* self);` / `void XTextEdit_paste_2(XTextEdit* self);` | 复制/剪切/粘贴（委托编辑器） | `copy`/`cut`/`paste` qtextedit.h:208/207/209 | — |
| `void XTextEdit_clear_2(XTextEdit* self);` / `void XTextEdit_selectAll_2(XTextEdit* self);` | 清空/全选 | `clear`/`selectAll` qtextedit.h:215/216 | — |
| `bool XTextEdit_canPaste(XTextEdit* self);` | 剪贴板可粘贴（**非 const self**） | `canPaste` qtextedit.h:179（Qt const） | — |
| `void XTextEdit_setAcceptRichText(XTextEdit* self, bool accept);` / `bool XTextEdit_acceptRichText(const XTextEdit* self);` | 接受富文本（默认 true） | `setAcceptRichText`/`acceptRichText` qtextedit.h:167/166 | — |
| `void XTextEdit_setTextBackgroundColor(XTextEdit* self, uint32_t color);` / `uint32_t XTextEdit_textBackgroundColor(const XTextEdit* self);` | 文本背景色 ARGB | `setTextBackgroundColor`/`textBackgroundColor` qtextedit.h:193/96 | — |
| `void XTextEdit_setFontFamily(XTextEdit* self, const char* family);` / `const char* XTextEdit_fontFamily(const XTextEdit* self);` | 字体族 | `setFontFamily`/`fontFamily` qtextedit.h:188/91 | 借用禁释放 |
| `void XTextEdit_setFontWeight(XTextEdit* self, int weight);` / `int XTextEdit_fontWeight(const XTextEdit* self);` | 字重 | `setFontWeight`/`fontWeight` qtextedit.h:189/92 | — |
| `void XTextEdit_setFontPointSize(XTextEdit* self, double size);` / `double XTextEdit_fontPointSize(const XTextEdit* self);` | 字号（磅） | `setFontPointSize`/`fontPointSize` qtextedit.h:187/90 | — |
| `void XTextEdit_setCurrentFont(XTextEdit* self, const char* family);` | 设当前字体（**收字体族字符串**，Qt 收 QFont） | `setCurrentFont(const QFont&)` qtextedit.h:194（**承载差异**） | family 借用 |
| `XFont XTextEdit_currentFont(const XTextEdit* self);` | 当前字体属性组合（族/字重/字号/斜体/下划线） | `currentFont` qtextedit.h:97 | **值返回，但内部持堆字符串 → 须 `XClassDeinit`** |
| `void XTextEdit_zoomIn(XTextEdit* self, int range);` / `void XTextEdit_zoomOut(XTextEdit* self, int range);` | 放大/缩小字号（缺省步长 1，上限 100） | `zoomIn`/`zoomOut` qtextedit.h:227/228 | — |
| `void XTextEdit_setTabStopDistance(XTextEdit* self, double distance);` / `double XTextEdit_tabStopDistance(const XTextEdit* self);` | 制表位距 | `setTabStopDistance`/`tabStopDistance` qtextedit.h:161/160 | — |
| `void XTextEdit_setAutoFormatting(XTextEdit* self, int features);` / `int XTextEdit_autoFormatting(const XTextEdit* self);` | 自动格式化位集 | `setAutoFormatting`/`autoFormatting` qtextedit.h:106/105 | — |
| `void XTextEdit_setTabChangesFocus(XTextEdit* self, bool b);` / `bool XTextEdit_tabChangesFocus(const XTextEdit* self);` | Tab 改焦点 | `setTabChangesFocus`/`tabChangesFocus` qtextedit.h:109/108 | — |
| `void XTextEdit_setDocumentTitle(XTextEdit* self, const char* title);` / `const char* XTextEdit_documentTitle(const XTextEdit* self);` | 文档标题 | `setDocumentTitle`/`documentTitle` qtextedit.h:111/113 | 借用禁释放 |
| `void XTextEdit_setUndoRedoEnabled_2(XTextEdit* self, bool enable);` / `bool XTextEdit_isUndoRedoEnabled_2(const XTextEdit* self);` | 撤销重做开关（转发编辑器） | `setUndoRedoEnabled`/`isUndoRedoEnabled` qtextedit.h:118/116 | — |
| `void XTextEdit_setLineWrapMode(XTextEdit* self, int mode);` / `int XTextEdit_lineWrapMode(const XTextEdit* self);` | 换行模式 0..1 | `setLineWrapMode`/`lineWrapMode` qtextedit.h:122/121 | — |
| `void XTextEdit_setWordWrapMode(XTextEdit* self, int policy);` / `int XTextEdit_wordWrapMode(const XTextEdit* self);` | 单词换行策略 0..4 | `setWordWrapMode`/`wordWrapMode` qtextedit.h:128/127 | — |
| `void XTextEdit_setReadOnly_2(XTextEdit* self, bool ro);` / `bool XTextEdit_isReadOnly_2(const XTextEdit* self);` | 只读（转发编辑器） | `setReadOnly`/`isReadOnly` qtextedit.h:85/84 | — |
| `void XTextEdit_setPlaceholderText_2(XTextEdit* self, const char* text);` / `const char* XTextEdit_placeholderText_2(const XTextEdit* self);` | 占位文本 | `setPlaceholderText`/`placeholderText` qtextedit.h:78/79 | 借用禁释放 |
| `void XTextEdit_ensureCursorVisible_2(XTextEdit* self);` | 确保光标可见 | `ensureCursorVisible` qtextedit.h:143 | — |
| `void XTextEdit_setCenterOnScroll(XTextEdit* self, bool enabled);` / `bool XTextEdit_centerOnScroll(const XTextEdit* self);` | 滚动居中 | **承载自 `QPlainTextEdit`**（qplaintextedit.h:48/109；`rg centerOnScroll qtextedit.h` 零命中） | — |

### 额外选择集/光标/查找/文档与插入（XTextEdit.h:550-815）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextEdit_setExtraSelections(XTextEdit* self, const XPlainTextEditExtraSelection* selections, int count);` | 设额外高亮（(行,列,长度,颜色) 平铺承载，转发编辑器并请求重绘；NULL 清空） | `setExtraSelections` qtextedit.h:174（Qt 收 `QList<ExtraSelection>`） | selections 借用（拷贝语义） |
| `int XTextEdit_extraSelections(const XTextEdit* self, const XPlainTextEditExtraSelection** selections);` | 读额外高亮（输出借用数组首地址；NULL 只取条数） | `extraSelections` qtextedit.h:175 | 输出借用 |
| `void XTextEdit_setBackgroundVisible(XTextEdit* self, bool visible);` / `bool XTextEdit_backgroundVisible(const XTextEdit* self);` | 背景可见 | **承载自 `QPlainTextEdit`**（qplaintextedit.h:47/106） | — |
| `XPoint XTextEdit_textCursor(const XTextEdit* self);` | 光标平铺承载（x=行号、y=行内 UTF-8 字节列） | `textCursor` qtextedit.h:82（Qt 返回 QTextCursor） | 值 |
| `void XTextEdit_setCursorWidth(XTextEdit* self, int width);` / `int XTextEdit_cursorWidth(const XTextEdit* self);` | 光标宽 px | `setCursorWidth`/`cursorWidth` qtextedit.h:164/163 | — |
| `bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags);` | 查找（flags 仅最低位=FindBackward；命中置光标并重绘） | `find(const QString&, FindFlags)` qtextedit.h:130 | exp 借用 |
| `XRect XTextEdit_cursorRect(const XTextEdit* self);` | 光标竖线矩形（委托编辑器度量：行高 16、左留白 2） | `cursorRect()` qtextedit.h:153 | 值 |
| `XString* XTextEdit_anchorAt(const XTextEdit* self, const XPoint* pos);` | 坐标处锚点 href（非预览态按纯文本口径，恒空串对象） | `anchorAt` qtextedit.h:155 | **新建，须释放**；失败 NULL |
| `void XTextEdit_setTextCursor(XTextEdit* self, int line, int col);` | 设光标（行列越界钳位） | `setTextCursor(const QTextCursor&)` qtextedit.h:81（平铺降级） | — |
| `int XTextEdit_textCursorLine(const XTextEdit* self);` / `int XTextEdit_textCursorColumn(const XTextEdit* self);` | 光标行/列（NULL→0） | 无直接对应（Qt 走 QTextCursor） | — |
| `XPoint XTextEdit_cursorForPosition(const XTextEdit* self, const XPoint* pos);` | 坐标→光标（x=行、y=列，行列钳位） | `cursorForPosition` qtextedit.h:151 | 值 |
| `void XTextEdit_insertPlainText(XTextEdit* self, const char* text);` | 光标处插纯文本（支持 `\n`；**不携带当前字符格式**） | `insertPlainText` qtextedit.h:218 | text 借用 |
| `void XTextEdit_insertHtml(XTextEdit* self, const char* html);` | 光标处插 HTML（剥标签后按纯文本插入） | `insertHtml` qtextedit.h:220（Qt 保留内联格式） | html 借用 |
| `int XTextEdit_lineWrapColumnOrWidth(const XTextEdit* self);` / `void XTextEdit_setLineWrapColumnOrWidth(XTextEdit* self, int w);` | 换行列宽/像素宽（仅存储，不触发重排） | `lineWrapColumnOrWidth` qtextedit.h:124 | — |
| `XVariant* XTextEdit_loadResource(XTextEdit* self, int type, const char* name);` | 加载资源（**恒返回 NULL**，保留签名） | `loadResource` qtextedit.h:145 | 调用方无需释放 |
| `void XTextEdit_mergeCurrentCharFormat(XTextEdit* self, int format);` | 合并字符格式位集（仅置位方向，无法显式取消） | `mergeCurrentCharFormat` qtextedit.h:100 | — |
| `void XTextEdit_setCurrentCharFormat(XTextEdit* self, int format);` | 整体替换字符格式位集（置位开、未置位关） | `setCurrentCharFormat` qtextedit.h:102 | — |
| `void XTextEdit_scrollToAnchor(XTextEdit* self, const char* anchor);` | 滚动到锚点（**无操作**：渲染子集无 name 目标锚） | `scrollToAnchor` qtextedit.h:225 | anchor 借用 |
| `void XTextEdit_setDocument(XTextEdit* self, XTextDocument* doc);` | 外接文档（**不获所有权**；NULL 重置内部默认文档并释放旧的内部文档） | `setDocument` qtextedit.h:75 | doc 借用 |
| `XTextDocument* XTextEdit_document(const XTextEdit* self);` | 当前文档（内部或外接） | `document` qtextedit.h:76 | 借用禁释放 |

### Markdown/纯文本/打印/交互/菜单/移动（XTextEdit.h:817-914）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XTextEdit_setMarkdown(XTextEdit* self, const char* md);` | 设 Markdown（原文完整保存；显示降级为纯文本，标记不转换） | `setMarkdown` qtextedit.h:202 | md 借用 |
| `char* XTextEdit_toMarkdown(const XTextEdit* self);` | 导出 Markdown（setMarkdown 写过则返回原文堆拷贝，否则纯文本导出） | `toMarkdown(MarkdownFeatures)` qtextedit.h:140 | **堆分配，`XFree_System`** |
| `const char* XTextEdit_markdown(const XTextEdit* self);` | Markdown 原文（未写入→`""`） | `markdown` 属性 qtextedit.h:39 | 借用禁释放 |
| `void XTextEdit_setPlainText(XTextEdit* self, const char* text);` | 设纯文本（不做富文本探测；复位格式 + 清 Markdown 原文 + 同步文档） | `setPlainText` qtextedit.h:197 | text 借用 |
| `void XTextEdit_print(XTextEdit* self, void* printer);` | 打印（**空承载**，接口存在性对齐） | `print(QPagedPaintDevice*)` qtextedit.h:181 | printer 借用 |
| `XMenu* XTextEdit_createStandardContextMenu(XTextEdit* self);` | 标准右键菜单（委托编辑器；只读时仅复制/全选） | `createStandardContextMenu()` qtextedit.h:147 | **新建，调用方 `XClassDelete`** |
| `void XTextEdit_setTextInteractionFlags(XTextEdit* self, int flags);` / `int XTextEdit_textInteractionFlags(const XTextEdit* self);` | 文本交互标志（下发编辑器） | `setTextInteractionFlags`/`textInteractionFlags` qtextedit.h:87/88 | — |
| `void XTextEdit_setOverwriteMode(XTextEdit* self, bool overwrite);` / `bool XTextEdit_overwriteMode(const XTextEdit* self);` | 覆盖模式 | `setOverwriteMode`/`overwriteMode` qtextedit.h:158/157 | — |
| `void XTextEdit_moveCursor(XTextEdit* self, int operation, int mode);` | 移动光标（`XTextControlMoveOperation`/`XTextControlMoveMode` 数值对标 QTextCursor 枚举） | `moveCursor` qtextedit.h:177 | — |

**所有权与命名契约**：`_const` 借用族＝`fontFamily`/`documentTitle`/`placeholderText_2`/`markdown`（返回 `const char*` 内部借用）、`toPlainText`（**无 `_const` 但返回新建 `XString*`**，须 `XClassDelete`）与 `anchorAt`（新建）；`_utf8` 后缀族**不存在**。`_2` 重载共 13 个（`copy_2`/`cut_2`/`paste_2`/`clear_2`/`selectAll_2`/`setReadOnly_2`/`isReadOnly_2`/`setPlaceholderText_2`/`placeholderText_2`/`ensureCursorVisible_2`/`setUndoRedoEnabled_2`/`isUndoRedoEnabled_2`/`find_2`），**全部与 Qt 同名同参**——不满足 §8.2.1「`_2` = 同功能不同参数的重载」定义（XGui.md:1813 该行），是「为避让基类/编辑器同名符号」的历史遗留。三态 `set*`：`XTextEdit_setCurrentCharFormat`/`mergeCurrentCharFormat` 的位集语义是「置位=开、未置=不变/关」的二态位组合，无 Qt 式「显式取消」第三态（头注释自陈 XTextEdit.h:757-759）。

**可覆写点**：`class_init` 8 槽 + 条件 1 槽（`EXWidget_InputMethodQuery` 仅在 `XINPUTMETHOD_ON` 时登记，XTextEdit.c:1241-1244，转发内嵌编辑器；注释说明基类兜底只回居中假矩形）——`EXWidget_ResizeEvent`、`EXAbstractScrollArea_ScrollContentsBy`、`EXWidget_WheelEvent`、`EXWidget_PaintEvent`、`EXWidget_MousePressEvent`/`MouseReleaseEvent`/`MouseMoveEvent`、`EXClass_Deinit`。无 `_base` 公开入口。

**注意事项与坑点**

1. **「嵌套上限」三层文档互不一致**：本头文件写「嵌套上限一层」（XTextEdit.h:6-7、:201-202），`XTextDocument.h:8-9` 写「内联嵌套上限两层：栈深 3」，XGui.md:107-113 亦写「内联嵌套上限两层」。实现侧 `XTextDocument.c:580` 的 `XTD_INLINE_STACK 3` 支持两层嵌套 → 本头口径偏保守/过时。
2. **`centerOnScroll`/`backgroundVisible` 的 Qt 归属写错**：两者头注释都只写「对标 Qt 同名接口」（XTextEdit.h:539-549、:573-583），但 `QTextEdit` 6.8 头文件无这两个属性（`rg centerOnScroll\|backgroundVisible widgets/widgets/qtextedit.h` 零命中），它们在 `QPlainTextEdit`（qplaintextedit.h:47-48、106-109）——X 侧实际是「QPlainTextEdit 能力上提」。
3. **3 个 API 是空承载/恒空返回**：`XTextEdit_loadResource` 恒返回 NULL（头注释自陈「平铺模型未建立资源缓存体系」，XTextEdit.h:739-750）、`XTextEdit_scrollToAnchor` 无操作（XTextEdit.h:781-790）、`XTextEdit_print` 空实现（XTextEdit.h:858-866）。接入方不应依赖其副作用。
4. **信号区被切在 `extern "C"` 之外（实测）**：`XTextEdit.h:283-285` 关闭 `extern "C"` 后，`:287` 起 6 个信号函数（`copyAvailable`/`cursorPositionChanged`/`modificationChanged`/`redoAvailable`/`selectionChanged`/`undoAvailable`）与 `append`/`copy_2`/…/`moveCursor` 全部落在 C 链接块之外。实测（补偿基类缺失的 `}` 后）`nm -C` 显示 `XTextEdit_copyAvailable_signal` 以 C++ 修饰名 `XTextEdit_copyAvailable_signal(XTextEdit*)` 导出，而 `textChanged_signal` 等仍在 C 链接内——C++ 调用方混链两套符号。
5. **文件尾守卫标签错序**：`XTextEdit.h:914` 以 `#endif /* XTEXTEDIT_H */` 收第三层守卫（该层实际是 `XWIDGET_ON && … && XTEXTEDIT_ON`，开于 :28），`:916` 收 `XGUI_ON`，`:918` 才以 `#endif /* XTEXTEDIT_ON */` 收尾——标签与层级错位（纯文档性，C 编译无碍）。
6. **C++ 包含需补 2 个 `}`（实测，非注释来源）**：本头自身的 `extern "C"` 是配平的（`:19` 开、`:284` 关），缺的两层来自被包含的 `XWidget.h:79` 与 `XPlainTextEdit.h:44`；`g++ -std=c++17 -fsyntax-only` 补 2 个 `}` 才通过，逐个递补时提示依次匹配 `XPlainTextEdit.h:44`、`XTextEdit.h:19`。C11 下 0 error。
7. **`currentFont()` 是值返回但含堆资源**：头注释要求「返回对象所有权归调用方，使用完毕必须 `XClassDeinit`」（XTextEdit.h:418-420）；结构体 `XFont` 首成员是 `XClass`、`m_family`/`m_styleName` 为对象拥有（`Src/XCore/XData/XFont/XFont.h:153-157`），故**按值 memcpy/浅拷贝会双释放**。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XTextEditTest.c`）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_text.c` 199 处 `XTextEdit_*` 调用（该文件 2520 行，是文本域主要 API 回归面）；`Test/XGuiDemo/xgui_demo_page_advanced.c` 10 处（富文本展示页）。

---

## XTimeEdit

**定位**：时间编辑控件——纯别名层：仅构造语义（parserType=Time、初值 2000-01-01 00:00:00.000、`displayFormat="HH:mm:ss"` 触发基类纯时间段收窄）与 `timeChanged → userTimeChanged` 转发，时间值/范围/分段编辑/绘制/弹层全部继承 `XDateTimeEdit`。继承链 `XObject ← XWidget ← XAbstractScrollArea ← XAbstractSpinBox ← XDateTimeEdit ← XTimeEdit`（XTimeEdit.h:43-44；`XDateTimeEdit` 继承声明见 XDateTimeEdit.h:86-87）。头文件 `Src/XGui/Widget/XTimeEdit.h`（**139 行**，结构体零自有字段）。守卫 `XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON`（XTimeEdit.h:40）；`XTIMEEDIT_ON` 缺省 1（XGuiConfig.h:686-688）。

**生命周期**：`XTimeEdit_init` 与 `XTimeEdit_init_2` 共用 `xte_initCommon`（XTimeEdit.c:68 起）：`XMemset` → `XDateTimeEdit_init` → 挂本类虚表 → `m_parserType = XDateTimeEditParserType_Time` → `XDate_setDate(2000,1,1)` + 初值时间（无效/NULL 回退 00:00:00.000）→ `XDateTimeEdit_setDisplayFormat("HH:mm:ss")`（触发基类收窄：日期范围收窄到 2000-01-01，时间范围 00:00:00.000..23:59:59.999）→ 连接 `timeChanged → xte_userTimeChangedForward`（XTimeEdit.c:90-100）；无自有 `deinit` 槽，析构沿用 `XDateTimeEdit` 的 deinit 链；栈/堆纪律遵循基类 XWidget 约定。

**API 面**（Qt 行号取 `widgets/widgets/qdatetimeedit.h`，`QTimeEdit` 定义在同文件 :181-192）

### 生命周期（XTimeEdit.h:54-117）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTimeEdit_class_init(void);` | 建/取共享虚表（**仅 `XVTABLE_INHERIT_XCLASS(XDateTimeEdit)`，零槽重载**） | 无公开对应 | 共享表，禁释放 |
| `void XTimeEdit_init(XTimeEdit* self, XWidget* parent, XWidgetFlags flags);` | 构造（默认初值 00:00:00.000、`HH:mm:ss`、连接转发） | `QTimeEdit(QWidget*)` qdatetimeedit.h:186 | parent 借用 |
| `void XTimeEdit_init_2(XTimeEdit* self, const XTime* time, XWidget* parent, XWidgetFlags flags);` | 带初值构造（无效/NULL 回退 TIME_MIN） | `QTimeEdit(QTime, QWidget*)` qdatetimeedit.h:187 | time 借用 |
| `#define XTimeEdit_create(parent, flags) XTimeEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 qdatetimeedit.h:186 | 调用方拥有，`XClassDelete` |
| `XTimeEdit* XTimeEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 同上 |
| `#define XTimeEdit_create_2(time, parent, flags) XTimeEdit_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (time), (parent), (flags))` | 带初值建堆对象宏 | 同上 qdatetimeedit.h:187 | 同上 |
| `XTimeEdit* XTimeEdit_create_ex_2(XMemoryType memory, const XTime* time, XWidget* parent, XWidgetFlags flags);` | 带初值建堆对象 | 同上 | 同上 |

### 信号（XTimeEdit.h:119-133）

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void* XTimeEdit_userTimeChanged_signal(XTimeEdit* self, const XTime* time);` | `userTimeChanged(QTime)`：由构造期 `timeChanged` 转发，**时间部分任何变化路径**（步进/键入提交/程序性 setTime/setDateTime/范围钳位）都触发；信号函数本身只返回标识（XTimeEdit.c:139-142） | `userTimeChanged` qdatetimeedit.h:191（属性绑定 :184） | 载荷 `const XTime*` 借用；返回值仅为标识，不得解引用 |

**所有权与命名契约**：无字符串 API（无 `_const`/`_utf8` 族）；`_2` 重载 3 个（`init_2`/`create_2`/`create_ex_2`）均为「带初值参数的同功能变体」，符合 `_2` 契约（§8.2.1 XGui.md:1813）；三态 `set*` 无（时间范围/值全部继承基类；`calendarPopup` 亦继承）；`userTimeChanged_signal` 的 `const XTime*` 是借用（头注释 XTimeEdit.h:128-130「载荷为新时间（借用内部存储）」）。

**可覆写点**：`class_init` **零槽重载**，只做 `XVTABLE_INHERIT_XCLASS(XDateTimeEdit)`（XTimeEdit.c:28-37，注释自陈「子类不重载任何虚槽，仅继承虚表——分段步进/键入提交/绘制/弹层机器全部走 XDateTimeEdit 实现」）；唯一自有行为是构造函数体内 `XObject_connect_1(… XDateTimeEdit_timeChanged_signal(NULL,NULL) …, xte_userTimeChangedForward)`（XTimeEdit.c:97-100）。无 `_base` 公开入口。

**注意事项与坑点**

1. **析构注释承诺的宏不存在**：头文件 `XTimeEdit.h:117` 只有一行注释「@brief 析构调度入口宏（复用基类 XDateTimeEdit 的 deinit 链）」，其后**没有任何声明或宏**；实际析构由继承的 `EXClass_Deinit` 槽承担（`class_init` 未重载 deinit，XTimeEdit.c:28-37）。读者按注释找不到任何专门的析构宏入口（析构只能走 `XClassDeinit`/`XClassDelete`）。
2. **`userTimeChanged` 的触发面比 Qt 注释更宽**：头注释写「时间部分任何变化路径（步进/键入提交/程序性 `setTime`/`setDateTime`/范围钳位）均随之触发；正常应优先使用继承的 `timeChanged`」（XTimeEdit.h:124-128），依据是构造期把 `timeChanged` 直接连到转发槽（XTimeEdit.c:97-100）——`userTimeChanged` 与 `timeChanged` 载荷完全相同，仅在「Qt 文档说 userTimeChanged 只为完整实现 Q_PROPERTY 而存在」这点上保留独立信号（XTimeEdit.h:123-124）。
3. **日期范围被压成单日**：`displayFormat="HH:mm:ss"` 的纯时间段构造会触发基类收窄，结果是 `minimumDate == maximumDate == 2000-01-01`、时间范围 `00:00:00.000..23:59:59.999`（XTimeEdit.h:12-15、:63-67）——任何 `XDateTimeEdit_setDate` 调用都无效；调用方若需要跨日语义必须改用 `XDateTimeEdit`。
4. **C++ 包含需补 4 个 `}`（实测，非注释来源）**：本头在 `XTimeEdit.h:31` 开 `extern "C" {` 且头尾无闭合，叠加包含链上 `XWidget.h`（1 层，`:79`）、`XLineEdit.h`（1 层，经 `XAbstractSpinBox.h:39` 引入）、`XDateTimeEdit.h`（1 层）三处缺失块，`g++ -std=c++17 -fsyntax-only` 需补 **4** 个 `}`（实测逐个递补：补 1~3 仍报 `expected '}' at end of input`，提示匹配 `XWidget.h:79`）。C11 下 0 error。

**测试覆盖**：**无专属套件**（`Test/XGuiTest/` 无 `XTimeEditTest.c`）。间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_labels.c` 4 处（`:1372` `XTimeEdit_init_2(&te,&t,…)`、`:1376` 无效时间回退、`:1380` NULL 时间回退、`:1385` `XTimeEdit_create_2`）；`Test/XGuiDemo/xgui_demo_apitest_input.c` 2 处（`:390` 连接 `XTimeEdit_userTimeChanged_signal(NULL,…)`、`:2059` `XTimeEdit_init`）；`Test/XGuiDemo/xgui_window_demo.c:5902` 1 处（`XTimeEdit_init` 挂到输入页）。
# 控件文档片段 · 批次 09

本批覆盖 `Src/XGui/Widget/` 下 9 个控件类。所有签名逐字抄自头文件（行号可核对）；实现行号取自同目录 `.c`，并逐条 `sed`/`grep` 核实。
Qt 对标行号取自本地 `/home/xinyue/Qt-src/6.8.3/Src/qtbase/src/`。**目录实况与任务书假设不同**（已 `ls`/`find` 核实）：`qtooltip.h` 在 `widgets/kernel/`、`qtreeview.h`/`qtreewidget.h` 在 `widgets/itemviews/`、`qwizard.h` 在 `widgets/dialogs/`、`qstyleoption.h` 在 `widgets/styles/`；`qtoolbar.h`/`qtoolbox.h`/`qtoolbutton.h` 在 `widgets/widgets/`。`QTitleBar` 在 Qt 6.8.3 中**不存在**（`find . -name "qtitlebar*"` 无输出）——`XTitleBar` 的 Qt 对标只能落到 `QStyleOptionTitleBar`（qstyleoption.h:594-611）+ `QStyle::CC_TitleBar`（qstyle.h:337）+ `SC_TitleBar*`（qstyle.h:376-384）。

---

## XTitleBar

**定位**：标题栏控件——把原 `XWindowDecoration` 的 CC_TitleBar 绘制实现迁入为可继承控件，只画不钉（几何钉位与输入拦截归装饰模块）。继承链 `XObject ← XWidget ← XTitleBar`。头文件 `Src/XGui/Widget/XTitleBar.h`（242 行）。守卫 `XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON`（XTitleBar.h:37），**无独立开关**（`grep -c XTITLEBAR_ON Src/XGui/XGuiConfig.h` → 0）。保护接口在 `XTitleBar_Protected.h`（109 行），调用方必须显式 include（XTitleBar.h:19-21）。

**生命周期**：`XTitleBar_init` 全零后 `XWidget_init` 再挂本类虚表，预置 `m_activeSubControls=0`、`m_stripKick=false`、`m_releaseVerifyTimer=XTIMER_INVALID_ID`（XTitleBar.c:279-288）。本类无自有堆资源；`VXTitleBar_deinit` 仅回收挂起的松手校验帧定时器（登记在事件派发器、对象销毁不自动注销），再交 `XClass_Deinit_Parent(XWidget,…)`（XTitleBar.c:207-215）。其余遵循基类 XWidget 约定（栈 `XClassDeinit`／堆 `XClassDelete`／常规异步 `XObject_deleteLater`，XTitleBar.h:146-164）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTitleBar_class_init(void);` | 建表：继承 XWidget + 登记 HitTest 新槽与 PaintEvent/TimerEvent/Deinit/Copy/Move 重载 | X 扩展（Qt 无 QTitleBar；语义对位 `QStyle::CC_TitleBar` qstyle.h:337） | 共享表，禁释放 |
| `void XTitleBar_init(XTitleBar* self, XWidget* parent, XWidgetFlags flags);` | 初始化；`parent` = 宿主顶层（装饰模块挂载），`flags` 应传 0 | X 扩展（对位 `QStyleOptionTitleBar` 的宿主 QWidget，qstyleoption.h:594） | — |
| `#define XTitleBar_create(parent, flags) XTitleBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `XTitleBar* XTitleBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `bool XTitleBar_isBar(const XWidget* candidate);` | 装饰路径动态类型校验（Copy/Move 槽指纹 + HitTest 槽非空） | X 扩展（Qt RTTI/`qobject_cast`） | 只读判定，无所有权 |
| `int XTitleBar_hitTest_base(XTitleBar* self, const XPoint* pos);` | 经虚表分派命中测试；默认组装 CC_TitleBar 选项交 `hitTestComplexControl` | `QStyle::hitTestComplexControl` qstyle.h:408-409 | — |
| `void XTitleBar_armReleaseVerify(XTitleBar* self);` | 武装一次松手校验帧：约 100ms 后对宿主顶层补一帧全窗重绘（幂等；`XWD_RELEASE_VERIFY=0` 禁用） | X 扩展（无 Qt 对位） | — |

保护接口（`XTitleBar_Protected.h`，子类/装饰模块专用）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `int XTitleBar_defaultHeight(void);` | 条高 = max(PM_TitleBarHeight, PM_TitleBarButtonSize+4, 8)；样式缺席返回 0 | `PM_TitleBarHeight` qstyle.h:448 / `PM_TitleBarButtonSize` qstyle.h:534 | — |
| `bool XTitleBar_buildOption(XTitleBar* self, XStyleOptionTitleBar* opt, int* outBarH);` | 组装 CC_TitleBar 选项（绘制/命中共用一次事实源） | `QStyleOptionTitleBar` qstyleoption.h:594-611（text/icon/titleBarState/titleBarFlags） | `opt` 内文本/图标为宿主内部数据借用，禁释放 |
| `void XTitleBar_setActiveSubControls(XTitleBar* self, int subControls);` | 注入活动子控件位（`XStyleSC_TitleBar*` 组合），变化即重绘 | `QStyleOptionComplex::activeSubControls` qstyleoption.h:486 | — |
| `int XTitleBar_activeSubControls(const XTitleBar* self);` | 借用读活动子控件位 | 同上 | 值返回 |

**所有权与命名契约**：查询取 `_const` self（`XTitleBar_activeSubControls`、`XTitleBar_isBar`、`XTitleBar_hitTest_base` 的 `pos` 为借用）；无 `_utf8` 后缀（标题取自宿主 `XWidget_windowTitle`，不设字符串 setter）；无 `_2` 重载；无三态 `set*`——`XTitleBar_setActiveSubControls` 为 int 单值写入。宿主 `parent` 为借用（XTitleBar.h:117-118、137-138）；`XTitleBar_buildOption` 输出的 `m_text`/`m_icon` 均借用宿主内部数据，只在本次绘制/命中期间有效（XTitleBar_Protected.h:65-67）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XTitleBar)` + `XCLASS_DEFINE_ENUM(XTitleBar, HitTest) = XCLASS_VTABLE_GET_SIZE(XWidget)` + `XCLASS_DEFINE_END(XTitleBar)`（XTitleBar.h:51-53）。

- `EXTitleBar_HitTest` → 默认 `VXTitleBar_hitTest`：组装选项后交 `XStyle_hitTestComplexControl`；按钮返回对应 `XStyleSC_TitleBar*` 位，条内空白返回 `XStyleSC_TitleBarLabel`/0，宿主不可装饰返回 0（XTitleBar.c:96-105）。分派入口 `XTitleBar_hitTest_base`（XTitleBar.c:304-311）。
- `EXWidget_PaintEvent` → 默认 `VXTitleBar_paintEvent`：`CC_TitleBar` 条带 + 首帧补拍（`m_stripKick` 首次置位并 `XWidget_update`），宿主缺席零绘制（XTitleBar.c:115-168）。
- `EXObject_TimerEvent` → 默认 `VXTitleBar_timerEvent`：非本校验帧的定时器 `XEvent_ignore` 后交父类槽（XTitleBar.c:178-198）。
- `EXClass_Deinit` → `VXTitleBar_deinit`：回收挂起定时器后交 XWidget（XTitleBar.c:207-215）。
- `EXClass_Copy`/`EXClass_Move` → 两个标量随拷/随移，定时器不随拷移（XTitleBar.c:218-254）。

**注意事项与坑点**

1. **头注释与 Protected 头对「整条矩形高」口径不一致**：XTitleBar.h:11-12 写「高度由 `XTitleBar_defaultHeight` 决定」，而 XTitleBar_Protected.h:61-63 写「整条矩形高取本控件真实几何高……几何零高时退 defaultHeight 兜底」，实现同后者——`rectH = XWidget_height(self); if (rectH <= 0) rectH = barH;`（XTitleBar.c:378-381）。派生自定义条显式定高时，`defaultHeight` 只用于钉位/命中带（`XWindowDecoration.c:353-357` 默认条恒钉 defaultHeight），不决定绘制矩形。误信前者会以为画不满条带是 bug，实为「控件画满自身几何」类不变式。
2. **`isBar` 的识别是槽指纹不是类型**：比对 `EXClass_Copy`/`EXClass_Move` 两槽与 `XTitleBar_class_init` 注册的 static 函数指针，并校验 `HitTest` 槽非空（XTitleBar.c:315-334）。头文件 @warning 明写「派生类若重载 Copy 与 Move 两槽将无法被识别（保守返回 false，装饰路径随之降级为仅摆位/纯子控件放行）」（XTitleBar.h:185-187）——继承定制若顺手覆写 Copy/Move，会被静默降级。
3. **高度上限被钳到按钮尺寸**：`XTitleBar_defaultHeight` 取 `XStylePM_TitleBarHeight`(=26，XStyleOption.h:328) 与 `XStylePM_TitleBarButtonSize`(=92，XStyleOption.h:391)`+4` 及 8px 下限三者取大（XTitleBar.c:338-351）。按当前样式常量该函数实际恒返回 96 而非 26；调试条高时不要拿 `XStylePM_TitleBarHeight` 当期望值。
4. **句柄类文档悬空**：XTitleBar.h:145-164 有两条 `@brief`（反初始化 / 删除堆对象）但**其后无任何声明**（`grep -n "XTitleBar_deinit\|XTitleBar_delete" Src/` → 无命中），释放只能走 `XClassDeinit`/`XClassDelete` 宏（XClass.h:227-231）。同型悬空文档在 XToolButton.h:147-157 亦有（见该节）。
5. **`_base` 入口不得递归**：`XTitleBar_hitTest_base` 经 `XClassGetVirtualFunc(self, EXTitleBar_HitTest, …)` 取**当前**实现（XTitleBar.c:308-310），子类重载内再调它会自递归；需基类行为时按头注用 `XClass_Parent` 访问父类槽位（XTitleBar.h:198-200、XTitleBar_Protected.h:20-22）。
6. **定时器在事件派发器里登记，不随对象销毁注销**：`VXTitleBar_deinit` 显式 `XObject_killTimer` 后才交父类（XTitleBar.c:207-215 注释「析构不回收会在条控件释放后向悬空对象投递定时器事件」）。

**测试覆盖**：无 `Test/XGuiTest/XTitleBarTest.c` 专属套件（`ls Test/XGuiTest/XTitleBarTest.c` → MISSING）。间接覆盖：`Test/XGuiTest/XGuiDialogMoveTest.c` 真机链路测条高与空白锚点（`t_expect(barH > 0, "title bar height positive")` :382；`t_expect(blankX >= 0, "blank title-bar drag anchor found")` :413，全文件 `t_expect(` 共 15 处）；`Test/XGuiTest/XGuiMirrorFidelityTest.c` 用 `XTitleBar_isBar`+`XTitleBar_hitTest_base` 扫空白锚点（:775-790，`mf_expect(blankX >= 0, "R2 blank title-bar anchor")` :790，全文件 `mf_expect(` 共 29 处）。仓库根 `xgui_regression_test.c` 对 `XTitleBar` 零命中（`grep -c XTitleBar xgui_regression_test.c` → 0）。

---

## XToolBar

**定位**：工具栏（动作/控件平行数组 + 自绘面板 + 桥接动作信号转发）。继承链 `XObject ← XWidget ← XToolBar`。头文件 `Src/XGui/Widget/XToolBar.h`（264 行）。守卫 `XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON`（XToolBar.h:46）；开关 `XTOOLBAR_ON` 默认 1（XGuiConfig.h:698-699），裁剪块置 0（XGuiConfig.h:900-901）。

**生命周期**：`XToolBar_init` 建 4 条平行 `XVector`（actions/buttons/bridges/widgets）、`m_movable=true`、`m_floatable=true`、`m_orientation=1`、`m_allowedAreas=0xF`（四区全允许）、`m_iconSize=16`、`m_buttonStyle=XToolButtonStyle_TextOnly`，`resize(200,30)` 并同步 sizeHint/minimumSizeHint（XToolBar.c:361-392）。`VX_toolBar_deinit` 只销毁 `xtb_ownsAction` 判定的自建动作（QObject 父=本栏），外部注入动作只摘除；按钮容器与桥容器整体释放，附加控件仅释放容器（XToolBar.c:272-317）。栈/堆释放纪律遵循基类 XWidget 约定。

**API 面**

枚举：`XToolBarArea`（Left=0x1/Right=0x2/Top=0x4/Bottom=0x8，XToolBar.h:49-55），数值对齐 `Qt::ToolBarArea`（qnamespace.h:1214-1218）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XToolBar_setTitle(XToolBar* self, const char* utf8);` | 设标题（存于 XWidget 窗口标题） | `setWindowTitle` qwidget.h:368 | UTF-8 借用，内部拷贝 |
| `const char* XToolBar_title(const XToolBar* self);` | 读标题；NULL 返回 `""` | `windowTitle` qwidget.h:376 | 借用，禁释放 |
| `XVtable* XToolBar_class_init(void);` | 建表：Resize/Paint/Show/Hide 槽 + Deinit 重载 | X 扩展 | 共享表，禁释放 |
| `void XToolBar_init(XToolBar* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 200×30、四区全允许） | `QToolBar(QWidget*)` qtoolbar.h:39 | — |
| `#define XToolBar_create(parent, flags) XToolBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XToolBar* XToolBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `void XToolBar_setMovable(XToolBar* self, bool movable);` | 设可移动；变化发 movableChanged | `setMovable` qtoolbar.h:42 | — |
| `bool XToolBar_isMovable(const XToolBar* self);` | 读可移动（NULL→false） | `isMovable` qtoolbar.h:43 | — |
| `void XToolBar_setFloatable(XToolBar* self, bool floatable);` | 设可浮动（仅存状态，无信号） | `setFloatable` qtoolbar.h:83 | — |
| `bool XToolBar_isFloatable(const XToolBar* self);` | 读可浮动（NULL→false） | `isFloatable` qtoolbar.h:82 | — |
| `void XToolBar_setOrientation(XToolBar* self, int orientation);` | 设方向（1 水平/2 垂直）；变化重排并发 orientationChanged | `setOrientation` qtoolbar.h:51 | — |
| `int XToolBar_orientation(const XToolBar* self);` | 读方向（NULL→1） | `orientation` qtoolbar.h:52 | — |
| `void XToolBar_setAllowedAreas(XToolBar* self, int areas);` | 设允许停靠区位掩码；变化发 allowedAreasChanged | `setAllowedAreas` qtoolbar.h:45 | — |
| `int XToolBar_allowedAreas(const XToolBar* self);` | 读允许停靠区（NULL→0） | `allowedAreas` qtoolbar.h:46 | — |
| `bool XToolBar_isAreaAllowed(const XToolBar* self, int area);` | 查单个区是否允许 | `isAreaAllowed` qtoolbar.h:48-49 | — |
| `bool XToolBar_isFloating(const XToolBar* self);` | 读浮动位（停靠体系未建，恒默认 false） | `isFloating` qtoolbar.h:84 | — |
| `void XToolBar_setIconSize(XToolBar* self, int size);` | 设图标边长（<=0 忽略）；变化重排并发 iconSizeChanged | `setIconSize` qtoolbar.h:87 | — |
| `int XToolBar_iconSize(const XToolBar* self);` | 读图标边长（NULL→16） | `iconSize` qtoolbar.h:77 | — |
| `void XToolBar_setToolButtonStyle(XToolBar* self, int style);` | 设按钮风格并同步既有按钮 | `setToolButtonStyle` qtoolbar.h:88 | — |
| `int XToolBar_toolButtonStyle(const XToolBar* self);` | 读按钮风格（NULL→0） | `toolButtonStyle` qtoolbar.h:78 | — |
| `void XToolBar_addAction(XToolBar* self, XAction* action);` | 追加外部动作（借用，创建按钮+桥） | `addAction(QAction*)` qwidget.h:559 | 动作借用，不释放 |
| `XAction* XToolBar_addAction_2(XToolBar* self, const char* utf8);` | 以文本建动作并追加（自建，父=本栏） | `addAction(const QString&)` qtoolbar.h:58 | 返回动作归工具栏，禁调用方释放 |
| `XAction* XToolBar_addSeparator(XToolBar* self);` | 追加分隔条 | `addSeparator` qtoolbar.h:65 | 归工具栏 |
| `XAction* XToolBar_insertSeparator(XToolBar* self, XAction* before);` | 在 before 前插入分隔条（before NULL=追加） | `insertSeparator` qtoolbar.h:66 | 归工具栏；`before` 借用 |
| `void XToolBar_addWidget(XToolBar* self, XWidget* widget);` | 追加控件（内部建占位动作） | `addWidget` qtoolbar.h:68 | 控件借用，不删除 |
| `void XToolBar_insertWidget(XToolBar* self, XAction* before, XWidget* widget);` | 在 before 前插入控件 | `insertWidget` qtoolbar.h:69 | 控件借用；占位动作归工具栏 |
| `XWidget* XToolBar_widgetForAction(XToolBar* self, XAction* action);` | 查动作关联的附加控件（动作按钮无关联控件） | `widgetForAction` qtoolbar.h:80 | 借用，禁释放 |
| `void XToolBar_removeAction(XToolBar* self, XAction* action);` | 移除动作（自建动作 deleteLater，外部动作归还调用方） | `removeAction` qwidget.h:563 | 见语义列 |
| `void XToolBar_clear(XToolBar* self);` | 清空全部动作与控件（同上所有权分流） | `clear` qtoolbar.h:54 | 同上 |
| `int XToolBar_actionCount(const XToolBar* self);` | 动作数量（分隔条计入） | `actions()` qwidget.h:564 的长度 | — |
| `XAction* XToolBar_action(const XToolBar* self, int index);` | 按索引取动作；越界 NULL | `actions()` qwidget.h:564 | 借用 |
| `XAction* XToolBar_actionAt(const XToolBar* self, const XPoint* pos);` | 局部坐标命中动作 | `actionAt` qtoolbar.h:72 | 借用；`pos` 借用 |
| `XRect XToolBar_actionGeometry(const XToolBar* self, XAction* action);` | 动作局部矩形；未命中 (0,0,0,0) | `actionGeometry` qtoolbar.h:71 | 值返回 |
| `XAction* XToolBar_toggleViewAction(XToolBar* self);` | 显示/隐藏工具栏的开关动作（懒建，归工具栏） | `toggleViewAction` qtoolbar.h:75 | 归工具栏，禁释放 |
| `void* XToolBar_actionTriggered_signal(XToolBar* self, XAction* action);` | actionTriggered(action) 信号标识（桥真发射） | `actionTriggered` qtoolbar.h:91 | 信号标识，禁解引用 |
| `void* XToolBar_actionHovered_signal(XToolBar* self, XAction* action);` | actionHovered(action) 信号标识（桥真发射） | X 扩展（Qt 无 QToolBar::actionHovered） | 同上 |
| `void* XToolBar_orientationChanged_signal(XToolBar* self, int orientation);` | orientationChanged 信号（setOrientation 真发射） | `orientationChanged` qtoolbar.h:94 | 同上 |
| `void* XToolBar_movableChanged_signal(XToolBar* self, bool movable);` | movableChanged 信号（setMovable 真发射） | `movableChanged` qtoolbar.h:92 | 同上 |
| `void* XToolBar_allowedAreasChanged_signal(XToolBar* self, int areas);` | allowedAreasChanged 信号（setAllowedAreas 真发射） | `allowedAreasChanged` qtoolbar.h:93 | 同上 |
| `void* XToolBar_toolButtonStyleChanged_signal(XToolBar* self, int style);` | toolButtonStyleChanged 信号（setToolButtonStyle 真发射） | `toolButtonStyleChanged` qtoolbar.h:96 | 同上 |
| `void* XToolBar_visibilityChanged_signal(XToolBar* self, bool visible);` | visibilityChanged 信号（show/hide 事件驱动真发射） | `visibilityChanged` qtoolbar.h:98 | 同上 |
| `void* XToolBar_iconSizeChanged_signal(XToolBar* self, int width, int height);` | iconSizeChanged 信号（setIconSize 以 size×size 真发射） | `iconSizeChanged(QSize)` qtoolbar.h:95 | 同上 |
| `void* XToolBar_topLevelChanged_signal(XToolBar* self, bool topLevel);` | topLevelChanged 信号（句柄预留，无内部发射点） | `topLevelChanged` qtoolbar.h:97 | 同上 |

**所有权与命名契约**：`_const` 借用族 = `title`/`isMovable`/`isFloatable`/`orientation`/`allowedAreas`/`isAreaAllowed`/`isFloating`/`iconSize`/`toolButtonStyle`/`actionCount`/`action`/`actionAt`/`actionGeometry`；设置侧 `char*` 无 `_utf8` 后缀之例——`XToolBar_setTitle` 直接收 `const char* utf8`，而 `_2` 后缀用于「同义重载」的只有 `XToolBar_addAction_2`（动作 vs 文本两形态），**不是**字符串类型重载；无三态 `set*`（`setMovable`/`setFloatable`/`setOrientation`/`setAllowedAreas`/`setIconSize`/`setToolButtonStyle` 均单值）。动作所有权分流：`XAction` 的 QObject 父 == 本栏 ⇒ 工具栏所有；否则借用（`xtb_ownsAction`，XToolBar.c:522-526）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XToolBar)` + `XCLASS_DEFINE_EXTEND_END(XToolBar, XWidget)`（XToolBar.h:57-58）——**零新槽位**。

- `EXWidget_ResizeEvent` → `VX_toolBar_resizeEvent`：重排按钮/控件矩形（XToolBar.c:187-191）。
- `EXWidget_PaintEvent` → `VX_toolBar_paintEvent`：有默认样式时走 `XStylePE_PanelToolBar`，否则画底部 1px Mid 线（XToolBar.c:193-251）。
- `EXWidget_ShowEvent`/`EXWidget_HideEvent` → 同步 toggleViewAction 选中态并发 visibilityChanged(bool)（XToolBar.c:322-332 显示 / :337-347 隐藏）。
- `EXClass_Deinit` → `VX_toolBar_deinit`（XToolBar.c:272-317）。

**注意事项与坑点**

1. **头注释的默认按钮风格与实现相反**：XToolBar.h:9-11 写「默认……图标 16x16、IconOnlyWhereText? 对标 Qt 默认 ToolButtonIconOnly」，实现 `self->m_buttonStyle = (int)XToolButtonStyle_TextOnly;`（XToolBar.c:382）；apitest 也按实现口径断言并注明「实现为 TextOnly(1)，Qt 文档默认 ToolButtonIconOnly(0)——实现与 Qt 存在偏差」（Test/XGuiDemo/xgui_demo_apitest_menus.c:1086-1091）。取默认样式请按 TextOnly。
2. **`actions`/`insertAction` 在头文件里被承诺但不存在**：XToolBar.h:7 的功能范围列出 `insertAction`、`actions`，全头文件与实现均无同名 API（`grep -c "XToolBar_insertAction\|XToolBar_actions" Src/XGui/Widget/XToolBar.h Src/XGui/Widget/XToolBar.c` → 0/0）。需要按位置插入动作只能用 `insertSeparator`/`insertWidget` 的 before 参数，或按文本取回动作后用 `XToolBar_action` 枚举。
3. **`setIconSize` 只作用于排布，不下发按钮**：`XToolBar_setIconSize` 仅写 `m_iconSize`、重排并发信号（XToolBar.c:466-480），全文件无 `XAbstractButton_setIconSize` 调用（`grep -n setIconSize Src/XGui/Widget/XToolBar.c` 仅 :466）。虽然 `XAbstractButton_setIconSize` 存在（XAbstractButton.h:224），工具栏不会代为转发——按钮图标实际尺寸仍由按钮自身 `m_iconSize` 决定。
4. **垂直排布与命中测试口径不对称**：`xtb_relayout` 垂直分支把矩形写成 `XRect_init(&r, y, x, bh, bw)`（XToolBar.c:178，x/y 互换），而 `XToolBar_actionAt` 无论方向都只按 `pos->x` 递增命中、`pos->y` 只做范围检查（XToolBar.c:826-873，:843 `if (pos->y < 0 || pos->y >= h) return NULL;` + :854 `pos->x` 判定）；`actionGeometry` 走的 `xtb_actionRectAt` 同样只累加 x（XToolBar.c:793-824）。垂直工具栏上这两个查询与 `xtb_relayout` 的几何不自洽——垂直态命中/几何查询结果不可信。
5. **`topLevelChanged` 是纯句柄**：停靠体系未建，`m_floating` 全文件只在 init 隐式清零、从无写入（`grep -n m_floating Src/XGui/Widget/XToolBar.c` 仅 :463 读、:1007 注释），`isFloating` 恒 false；函数注释明写「当前无内部发射点，供未来停靠体系接入」（XToolBar.h:247-249），只有手动调用才发（实现 XToolBar.c:1016-1021）。
6. **`addWidget` 会 reparent 但头注释只说「控件归调用方」**：`xtb_insertAt` 末尾 `XWidget_setParent(widget, (XWidget*)self, 0)`（XToolBar.c:594-595），头文件 XToolBar.h:152 只写「控件归调用方」——归属确实仍属调用方（容器只存借用、`VX_toolBar_deinit` 只删容器），但父链已被改写；调用方若自行删除必须注意工具栏已不再是其父。
7. **移除路径用 deleteLater 而非同步删**：`removeAction`/`clear` 对按钮/桥/自建动作一律 `XObject_deleteLater`（XToolBar.c:705-708、721-722、744-745、760-762），注释理由「删除可能处于对象自身事件/信号发射帧，同步删 UAF」（XToolBar.c:702-704）。析构路径 `VX_toolBar_deinit` 用同步 `XClassDelete`（XToolBar.c:287、302、313），两条路径不可混用。

**测试覆盖**：无 `Test/XGuiTest/XToolBarTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_menus.c:1049-1268` 块（47 条 `XAPI_EXPECT`：默认值/信号注册/属性往返/addAction/seperator/widget/actionAt/actionGeometry/toggleViewAction/removeAction/clear）；仓库根回归 `xgui_regression_test.c:31412-31452` `test_toolbar_contract()`（14 条 `tb_expect`）。`XToolBar` 在 `Test/` 共被引用 10 处（`rg -c "\bXToolBar\b" Test --glob '*.c'` 求和）。

---

## XToolBox

**定位**：工具箱（每页一个页头条 + 当前页内容区，单页展开模式）。继承链 `XObject ← XWidget ← XFrame ← XToolBox`。头文件 `Src/XGui/Widget/XToolBox.h`（187 行）。守卫 `XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON`（XToolBox.h:35）；开关 `XTOOLBOX_ON` 默认 1（XGuiConfig.h:663-664），裁剪块置 0（XGuiConfig.h:942-943）。

**生命周期**：`XToolBox_init` 先 `XFrame_init` 再挂虚表，建 `m_items`（`XToolBoxItem*` 容器），`m_currentIndex=-1`，`resize(160,160)` 并同步 sizeHint/minimumSizeHint（XToolBox.c:386-406）。`VX_toolBox_deinit` 逐条 `xtb2_itemDestroy`（释放 text/icon/tooltip 三个 `XString`）后删容器，再交 `XClass_Deinit_Parent(XFrame,…)`（XToolBox.c:357-373）。页面控件为借用，不随箱析构（`XToolBoxItem.widget` 注释「借用，归调用方/容器」，XToolBox.c:32）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XToolBox_class_init(void);` | 建表：Paint/Resize/MousePress 槽 + Deinit 重载 | X 扩展 | 共享表，禁释放 |
| `void XToolBox_init(XToolBox* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 160×160、无页 currentIndex=-1） | `QToolBox(QWidget*,Qt::WindowFlags)` qtoolbox.h:24 | — |
| `#define XToolBox_create(parent, flags) XToolBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XToolBox* XToolBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `int XToolBox_addItem(XToolBox* self, XWidget* widget, const char* utf8Text);` | 追加页面并返回索引（失败 -1） | `addItem(QWidget*,const QString&)` qtoolbox.h:27 | 控件 reparent 为子控件，借用 |
| `int XToolBox_insertItem(XToolBox* self, int index, XWidget* widget, const char* utf8Text);` | 在 index 处插入，返回实际索引 | `insertItem(int,QWidget*,const QString&)` qtoolbox.h:29 | 同上 |
| `void XToolBox_removeItem(XToolBox* self, int index);` | 移除条目（控件不销毁，隐藏并摘父链归还调用方） | `removeItem` qtoolbox.h:32 | 控件归还调用方 |
| `int XToolBox_count(const XToolBox* self);` | 页面数量 | `count` qtoolbox.h:52 | — |
| `XWidget* XToolBox_widget(const XToolBox* self, int index);` | 取页面控件；越界 NULL | `widget` qtoolbox.h:50 | 借用 |
| `int XToolBox_indexOf(const XToolBox* self, const XWidget* widget);` | 查页面索引；未找到 -1 | `indexOf` qtoolbox.h:51 | 借用入参 |
| `void XToolBox_setItemText(XToolBox* self, int index, const char* utf8);` | 设页头文本（含 updateGeometry + update） | `setItemText` qtoolbox.h:37 | UTF-8 借用，内部拷贝 |
| `const char* XToolBox_itemText(const XToolBox* self, int index);` | 查页头文本；越界返回 `""` | `itemText` qtoolbox.h:38 | 借用 |
| `void XToolBox_setItemEnabled(XToolBox* self, int index, bool enabled);` | 设条目启用（禁用当前页则切到最近启用页） | `setItemEnabled` qtoolbox.h:34 | — |
| `bool XToolBox_isItemEnabled(const XToolBox* self, int index);` | 查条目启用 | `isItemEnabled` qtoolbox.h:35 | — |
| `void XToolBox_setItemIcon(XToolBox* self, int index, const XString* path);` | 设条目图标路径（XString 主版本）；NULL 清除 | `setItemIcon(QIcon)` qtoolbox.h:40 | `path` 借用，内部拷贝 |
| `void XToolBox_setItemIcon_2(XToolBox* self, int index, const char* utf8);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用，内部拷贝 |
| `const XString* XToolBox_itemIcon(const XToolBox* self, int index);` | 查图标路径 | `itemIcon` qtoolbox.h:41 | 借用，禁释放 |
| `void XToolBox_setItemToolTip(XToolBox* self, int index, const XString* tip);` | 设条目提示（仅存状态） | `setItemToolTip` qtoolbox.h:44 | `tip` 借用，内部拷贝 |
| `void XToolBox_setItemToolTip_2(XToolBox* self, int index, const char* utf8);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用，内部拷贝 |
| `const XString* XToolBox_itemToolTip(const XToolBox* self, int index);` | 查条目提示 | `itemToolTip` qtoolbox.h:45 | 借用，禁释放 |
| `int XToolBox_currentIndex(const XToolBox* self);` | 当前页索引（无页 -1） | `currentIndex` qtoolbox.h:48 | — |
| `XWidget* XToolBox_currentWidget(const XToolBox* self);` | 当前页控件 | `currentWidget` qtoolbox.h:49 | 借用 |
| `void XToolBox_setCurrentIndex(XToolBox* self, int index);` | 切页并发 currentChanged（越界/同页忽略；**不检查 enabled**） | `setCurrentIndex` qtoolbox.h:55 | — |
| `void XToolBox_setCurrentWidget(XToolBox* self, XWidget* widget);` | 按控件切页（内部走 indexOf） | `setCurrentWidget` qtoolbox.h:56 | 借用 |
| `void* XToolBox_currentChanged_signal(XToolBox* self, int index);` | currentChanged(index) 信号（真发射） | `currentChanged` qtoolbox.h:59 | 信号标识，禁解引用 |

**所有权与命名契约**：`_const` 借用族 = `count`/`widget`/`indexOf`/`itemText`/`isItemEnabled`/`itemIcon`/`itemToolTip`/`currentIndex`/`currentWidget`；`_2` 重载两处（`setItemIcon_2`、`setItemToolTip_2`）用于 **XString → UTF-8** 的字符串形态，**未**用 `_utf8` 后缀（与《字符串 API 后缀契约》冲突，且同类的 `setItemText` 直接收 `char*` 反而无 `_2`）；`setItemIcon`/`setItemToolTip` 为「XString 主版本 + NULL 清除」两态，无三态命名。条目内三个 `XString` 全由本对象拥有（XToolBox.c:39-51）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XToolBox)` + `XCLASS_DEFINE_EXTEND_END(XToolBox, XFrame)`（XToolBox.h:37-38）——零新槽位。

- `EXWidget_PaintEvent` → `VX_toolBox_paintEvent`：逐页头条（当前页 Highlight 底色）+ 当前页内容区（XToolBox.c:179-280）。
- `EXWidget_ResizeEvent` → `VX_toolBox_resizeEvent`：`xtb2_layout` 重铺（XToolBox.c:349-353）。
- `EXWidget_MousePressEvent` → `VX_toolBox_mousePressEvent`：页头 y→索引切页；激活页内容区与禁用头都 `XEvent_ignore`（XToolBox.c:293-347）。
- `EXClass_Deinit` → `VX_toolBox_deinit`（XToolBox.c:357-373）。

**注意事项与坑点**

1. **`removeItem` 的头注释与实现相反**：XToolBox.h:15-16 写「removeItem 保持父子关系（对标 Qt）」，实现却 `XWidget_setVisible(widget,false)` + `XWidget_setParent(widget, NULL, 0)`（XToolBox.c:496-499，注释「复扫 R-74：移除当前页后旧页残留可见……先隐藏并摘除父链再出表，控件归还调用方管理」）；Qt 6.8.3 的实现反而是 `w->setParent(this)`（qtoolbox.cpp:458）即**保持父链**。头注与实现、实现与 Qt 三方口径都不同。
2. **`setItemIcon` 只存路径，页头不画图标**：`VX_toolBox_paintEvent` 只有 `XPainter_fillRect` + `XPainter_drawText`（XToolBox.c:256-268；`awk` 该段 `grep -c icon` → 0）。`setItemIcon` 写入的 `XString* icon` 仅在 `itemIcon` 读回时可见，页头不带图标绘制。
3. **`setItemToolTip` 是纯状态**：头文件自己写明「仅存储状态；提示展示层尚未接入」（XToolBox.h:132）；全文件 tooltip 相关行只有分配/释放/赋值（XToolBox.c:35 字段声明、:48 初始置 NULL、:63-65 释放、:634-645 setter 赋值），无 `XToolTip_showText` 调用。
4. **程序化切页不过滤 enabled，点击才过滤**：`setCurrentIndex` 注释明写「只拦截越界与同页，不检查条目 enabled（程序化切换允许落在禁用条目上）；点击路径对禁用条目的拦截在 mousePressEvent」（XToolBox.c:747-750），apitest 也照此断言（`xgui_demo_apitest_containers.c:2522-2523`）。
5. **页高不是「占满内容区」**：`xtb2_pageHeight` 按当前页控件自身现高（0 时取 sizeHint）顶置，只有超过内容区或为 0 才占满（XToolBox.c:91-115，注释「复扫 #55 钉死」）。裸控件页不会垂直居中，与 Qt「页装在 QScrollArea+VBox 首行顶置」的观感一致但几何口径不同。
6. **切页后整箱标脏的理由**：`setCurrentIndex` 末尾 `XWidget_update((XWidget*)self)`，注释「页头带由本控件自绘且纵向位置随 currentIndex 平移：切换后整箱标脏重绘，抹掉旧位置页头残影（rescan_r2 #55）」（XToolBox.c:765-768）——页头残影是历史缺陷，单标脏页矩形不够。

**测试覆盖**：无 `Test/XGuiTest/XToolBoxTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_containers.c:2439-2604` 的 `containers_toolbox()`（29 条 `XAPI_EXPECT`：空箱默认/首条自动选中/信号/越界边界/enabled 切换/图标/提示/insert 前插/remove 摘父链/移除末条回空箱态/移除当前条激活后继）；仓库根回归 `xgui_regression_test.c:31547-31617` `test_toolbox_contract()`（19 条 `tbx_expect`）。

---

## XToolButton

**定位**：工具按钮（默认动作双向联动 + 菜单弹出三模式 + 五种图标/文本排布）。继承链 `XObject ← XWidget ← XAbstractButton ← XToolButton`。头文件 `Src/XGui/Widget/XToolButton.h`（302 行）。守卫 `XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON`（XToolButton.h:40）；开关 `XTOOLBUTTON_ON` 默认 1（XGuiConfig.h:777-778），裁剪块置 0（XGuiConfig.h:968-969），且 `!XMENU_ON` 时连带裁掉（XGuiConfig.h:1046-1051）。

**生命周期**：`XToolButton_init` 全零 → `XAbstractButton_init` → 挂虚表，预置 `IconOnly`/`NoArrow`/`DelayedPopup`，并把 `clicked` 直连到 `toolbutton_clickedSlot`，最后 `toolbutton_refresh`（XToolButton.c:949-964）。`VXToolButton_deinit` 解绑动作连接、断开菜单 `aboutToHide`、清两借用指针后交 `XClass_Deinit_Parent(XAbstractButton,…)`（XToolButton.c:898-911）。默认动作与菜单均为**借用指针**（XToolButton.h:101-102），不释放。栈/堆释放纪律遵循基类 XWidget 约定。

**API 面**

枚举：`XToolButtonStyle`（IconOnly=0/TextOnly=1/TextBesideIcon=2/TextUnderIcon=3/FollowStyle=4，XToolButton.h:47-54，数值对齐 `Qt::ToolButtonStyle` qnamespace.h:1439-1444）；`XToolButtonArrowType`（NoArrow=0/Up=1/Down=2/Left=3/Right=4，XToolButton.h:59-66，对齐 `Qt::ArrowType` qnamespace.h:1083-1088）；`XToolButtonPopupMode`（DelayedPopup=0/MenuButtonPopup=1/InstantPopup=2，XToolButton.h:72-77，对齐 `QToolButton::ToolButtonPopupMode` qtoolbutton.h:31-35）。

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XToolButton_class_init(void);` | 建表：SizeHint/MinimumSizeHint/ContentChanged/Paint/MousePress/MouseRelease/Timer + Copy/Move/Deinit，并登记为 XAbstractButton 派生类 | X 扩展 | 共享表，禁释放 |
| `void XToolButton_init(XToolButton* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 IconOnly/NoArrow/DelayedPopup/autoRaise=false） | `QToolButton(QWidget*)` qtoolbutton.h:38 | — |
| `#define XToolButton_create(parent, flags) XToolButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XToolButton* XToolButton_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `XAction* XToolButton_defaultAction(const XToolButton* self);` | 查默认动作；未设 NULL | `defaultAction` qtoolbutton.h:57 | 借用，禁释放 |
| `void XToolButton_setDefaultAction(XToolButton* self, XAction* action);` | 设默认动作（文本/图标/可选中/选中/启用镜像；旧动作解绑；动作销毁自动解绑） | `setDefaultAction` qtoolbutton.h:67 | `action` 借用，不接管 |
| `XToolButtonStyle XToolButton_toolButtonStyle(const XToolButton* self);` | 查样式；NULL→IconOnly | `toolButtonStyle` qtoolbutton.h:44 | 值 |
| `void XToolButton_setToolButtonStyle(XToolButton* self, XToolButtonStyle style);` | 设样式并刷新尺寸提示/重绘 | `setToolButtonStyle` qtoolbutton.h:66 | — |
| `bool XToolButton_autoRaise(const XToolButton* self);` | 查自动凸起；NULL→false | `autoRaise` qtoolbutton.h:60 | — |
| `void XToolButton_setAutoRaise(XToolButton* self, bool enable);` | 设自动凸起（仅按下/悬停画边框） | `setAutoRaise` qtoolbutton.h:59 | — |
| `XToolButtonArrowType XToolButton_arrowType(const XToolButton* self);` | 查箭头类型；NULL→NoArrow | `arrowType` qtoolbutton.h:46 | 值 |
| `void XToolButton_setArrowType(XToolButton* self, XToolButtonArrowType type);` | 设箭头类型并刷新（宽度 +14） | `setArrowType` qtoolbutton.h:47 | — |
| `XMenu* XToolButton_menu(const XToolButton* self);` | 查关联菜单；未设 NULL | `menu` qtoolbutton.h:51 | 借用，禁释放 |
| `void XToolButton_setMenu(XToolButton* self, XMenu* menu);` | 设关联菜单（换菜单时断开旧菜单 aboutToHide） | `setMenu` qtoolbutton.h:50 | `menu` 借用，不接管 |
| `XToolButtonPopupMode XToolButton_popupMode(const XToolButton* self);` | 查弹出模式；NULL→DelayedPopup | `popupMode` qtoolbutton.h:54 | 值 |
| `void XToolButton_setPopupMode(XToolButton* self, XToolButtonPopupMode mode);` | 设弹出模式（单值写入，不校验越界、不重绘） | `setPopupMode` qtoolbutton.h:53 | — |
| `void XToolButton_showMenu(XToolButton* self);` | 弹出关联菜单（仅启用且有菜单时；进入按下态，菜单隐藏恢复） | `showMenu` qtoolbutton.h:64 | — |
| `XSize XToolButton_sizeHint(const XToolButton* self);` | 按样式/内容算建议尺寸；NULL→(0,0) | `sizeHint` qtoolbutton.h:41 | 值 |
| `XSize XToolButton_minimumSizeHint(const XToolButton* self);` | 同 `sizeHint`（XToolButton.c:523-526） | `minimumSizeHint` qtoolbutton.h:42 | 值 |
| `void* XToolButton_triggered_signal(XToolButton* self, XAction* action);` | triggered(XAction*) 信号（动作 triggered 后真发射） | `triggered(QAction*)` qtoolbutton.h:70 | 信号标识，禁解引用 |

**所有权与命名契约**：`_const` 借用族 = `defaultAction`/`toolButtonStyle`/`autoRaise`/`arrowType`/`menu`/`popupMode`；无 `_utf8` 后缀（文本/图标一律经 `XAbstractButton` 的宏族，本头文件不含字符串 setter）；无 `_2` 重载；无三态 `set*`（`setDefaultAction`/`setMenu` 用 NULL 表示解除关联，属两态）。`m_defaultAction` 与 `m_menu` 均为借用指针（XToolButton.h:101-102、235/243 注释）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XToolButton)` + `XCLASS_DEFINE_EXTEND_END(XToolButton, XAbstractButton)`（XToolButton.h:87-88）——零新槽位。

- `EXWidget_SizeHint`/`EXWidget_MinimumSizeHint` → 转发到实时计算实现（XToolButton.c:917-920）。
- `EXAbstractButton_ContentChanged` → `VXToolButton_contentChanged`：重算并写回 sizeHint/minimumSizeHint（XToolButton.c:836-846）。
- `EXWidget_PaintEvent` → `VXToolButton_paintEvent`（XToolButton.c:559-743）。
- `EXWidget_MousePressEvent` → `VXToolButton_mousePressEvent`：InstantPopup 或 MenuButtonPopup 箭头区立即弹菜单，DelayedPopup 走父类按压后启动 500ms 定时器（XToolButton.c:756-785）。
- `EXWidget_MouseReleaseEvent` → `VXToolButton_mouseReleaseEvent`：菜单仍开则吞掉释放，未到期则停表回落普通点击（XToolButton.c:794-815）。
- `EXObject_TimerEvent` → `VXToolButton_timerEvent`：按住延时到点弹层（XToolButton.c:819-834）。
- `EXClass_Copy`/`EXClass_Move`/`EXClass_Deinit` → 见图（XToolButton.c:848-896）。

**注意事项与坑点**

1. **句柄类文档悬空**：XToolButton.h:147-157 有两条 `@brief`（「通过当前 XClass 虚表释放…」/「…删除其存储空间」）但**其后无声明**（`grep -n "XToolButton_deinit\|XToolButton_delete" Src/` 只命中 `.c` 内 static 槽 `VXToolButton_deinit`）。释放只能走 `XClassDeinit`/`XClassDelete` 宏。
2. **`FollowStyle` 不是「跟随」，是硬编码 TextBesideIcon**：`toolbutton_effectiveStyle` 把 `FollowStyle` 直接映射为 `TextBesideIcon`（XToolButton.c:121-130，注释「有效样式：FollowStyle 按 TextBesideIcon 处理」），头文件同口径（XToolButton.h:53）。Qt 的 FollowStyle 会查样式提示 `SH_ToolButtonStyle`（qtoolbutton.cpp:265-267），并在无图标无箭头且有文本时回落 TextOnly（qtoolbutton.cpp:276-278）——本实现既无 hint 查询也无该回落。
3. **`minimumSizeHint` 恒等于 `sizeHint`**：`XToolButton_minimumSizeHint` 直接 `return XToolButton_sizeHint(self);`（XToolButton.c:523-526）；Qt 的 `minimumSizeHint` 是独立的更小下界（qtoolbutton.h:42）。
4. **`DelayedPopup` 的延时常量不在头文件**：按住延时 `XTOOLBUTTON_POPUP_DELAY_MS 500u`（XToolButton.c:48，注释对标 `QCommonStyle::SH_ToolButton_PopupDelay` 缺省 500ms），且定时器 id 存在**动态属性** `"xgui.toolbutton.popupTimer"` 里（XToolButton.c:44-46），不占结构体字段——调试时按结构体找不到该状态。
5. **弹出位置是自算的逻辑全局，不经 `mapToGlobal`**：`XToolButton_showMenu` 手写父链累加 `m_windowRect` + 顶层原点（XToolButton.c:425-467，坐标计算在 :450-466），注释说明理由「`mapToGlobal` 返回设备全局（旋转正变换后），`XMenu_popup`→`setGeometryRect` 需逻辑全局——90/270 下菜单错位」（XToolButton.c:446-449）。改回 `mapToGlobal` 会在旋转屏复发。
6. **`setPopupMode` 不重绘也不校验**：实现只有 `if (self) self->m_popupMode = mode;`（XToolButton.c:419-423），越界枚举值原样落库、不刷新外观；同族 `setAutoRaise`/`setArrowType` 则会重绘。XGui.md 复扫栏记有「XToolButton popupMode 死存储」（XGui.md:1784）的历史修复，勿再按「存了但不用」理解。
7. **动作镜像覆盖图标为「覆盖语义」，不是「仅在动作有图标时更新」**：`toolbutton_mirrorFromAction` 无论动作有无图标都用 `XIcon_init_file(&mirroredIcon, iconPath)` 设回按钮，NULL 路径即空图标（XToolButton.c:261-271），所以设置一个无图标动作会清掉按钮手工设的图标。

**测试覆盖**：无 `Test/XGuiTest/XToolButtonTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_buttons.c:1088-1211`（24 条 `XAPI_EXPECT`：默认值/外观往返/菜单关联/默认动作镜像与销毁解绑/尺寸提示）；仓库根回归 `xgui_regression_test.c:27872-27948` `test_toolbutton_contract()`（18 条 `expect_true`）。另 `Test/XGuiDemo/xgui_demo_apitest_menus.c` 的工具栏块间接覆盖（`XToolBar` 创建按钮 + `setDefaultAction`）。

---

## XToolTip

**定位**：工具提示静态 API 类（模块级静态状态 + 惰性全局顶层提示控件），无实例、无虚表。继承链：**无**——非 XObject 派生（本头文件不出现任何 `XCLASS_DEFINE_*`，`grep -c XCLASS_DEFINE Src/XGui/Widget/XToolTip.h` → 0）。头文件 `Src/XGui/Widget/XToolTip.h`（142 行）。守卫仅 `XWIDGET_ON`（XToolTip.h:37），无模块开关。

**生命周期**：无创建/析构族。首个非空文本 `XToolTip_showText` 惰性建全局控件（`xtooltip_ensureWidget`：`XLabel` 优先，`XLABEL_ON=0` 时退回普通顶层 `XWidget`，XToolTip.c:56-84），并**在建窗前**设 `XWindowType_ToolTip` 与 `autoFillBackground=true`（XToolTip.c:70-82）；进程退出不提供 teardown 释放 `s_tooltipWidget`/`s_tooltipText`/`s_expireTimer`（全文件无 deinit 入口）。文本由模块静态 `XString* s_tooltipText` 拥有，`hideText`/换文时 `XClassDelete`。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `void XToolTip_showText(int x, int y, const XString* text, XWidget* widget, const XRect* rect, int msecShowTime);` | 显示提示；空文本等价 hideText；同文同控件复用不重摆 | `showText(const QPoint&,const QString&,QWidget*,const QRect&,int)` qtooltip.h:17-18 | `text`/`widget`/`rect` 借用；文本内部深拷贝 |
| `void XToolTip_showText_2(int x, int y, const char* utf8, XWidget* widget, const XRect* rect, int msecShowTime);` | 同上（UTF-8 重载，转发主版本） | 同上 | UTF-8 借用 |
| `void XToolTip_hideText(void);` | 隐藏并清空文本/可见/关联控件；停到期表 | `hideText` qtooltip.h:19（inline 转发 showText 空参） | — |
| `bool XToolTip_isVisible(void);` | 查可见标志 | `isVisible` qtooltip.h:21 | — |
| `XString* XToolTip_text(void);` | 当前文本**新拷贝**；无文本 NULL | `text` qtooltip.h:22 | 调用方拥有，须 `XClassDelete` |
| `XFont XToolTip_font(void);` | 当前字体值副本（含深拷贝资源） | `font` qtooltip.h:26 | 调用方须 `XClassDeinit` |
| `void XToolTip_setFont(const XFont* font);` | 设字体（深拷贝；已建控件同步应用）；NULL 忽略 | `setFont` qtooltip.h:27 | `font` 借用 |
| `XPalette XToolTip_palette(void);` | 当前调色板值副本（`XPALETTE_ON=0` 时返回 `{m_disabled=0}`） | `palette` qtooltip.h:24 | 值，无堆资源 |
| `void XToolTip_setPalette(const XPalette* palette);` | 设调色板（深拷贝；已建控件同步应用）；NULL 忽略 | `setPalette` qtooltip.h:25 | `palette` 借用 |

**所有权与命名契约**：无 self 参数（静态类），`_const` 借用族以入参形态体现：`showText` 的 `text`/`widget`/`rect`、`setFont` 的 `font`、`setPalette` 的 `palette` 全为借用。`_2` 后缀用于 **XString → UTF-8**（`showText_2`），未用 `_utf8`。返回值所有权不对称：`text` 是**新拷贝**（`XString_create_copy`，XToolTip.c:255-260），`font` 是**值副本需 deinit**（XToolTip.h:99-102），`palette` 无堆资源。`setFont`/`setPalette` 用 NULL 表达「忽略」而非「清除」（两态扩展语义，XToolTip.h:109-112、127-130）。

**可覆写点**：无虚表、无 `_base` 入口（静态 API 类，全靠模块级 static 状态）。

**注意事项与坑点**

1. **头文件 @note 说「不提供自动隐藏定时器」，实现已提供**：XToolTip.h:15-16 与函数级 XToolTip.h:56-57 都写「`msecShowTime` 仅存储记录，不启动自动隐藏定时器」；实现 `showText` 末尾调 `xtooltip_restartExpire(msecShowTime)`（XToolTip.c:214），`xtooltip_restartExpire` 建有单发 `XTimer`，按 `msecShowTime>0` 或默认 `10000 + 40×max(0,字符数−100)` 毫秒到期调 `XToolTip_hideText`（XToolTip.c:146-165、135-140）。文档两处均过期。
2. **`rect` 参数完全未使用**：`XToolTip_showText` 首行即 `(void)rect;`（XToolTip.c:172），放置策略恒按 `(x,y)` + sizeHint 处理；同文复用判定因此也不比较 rect（注释明写「rect 本实现未建模，等价空 rect → tipChanged 返 false」，XToolTip.c:180-184；复用判定在 :185-188）。
3. **`s_expireTimer` 永不释放**：`xtooltip_restartExpire` 懒建 `XTimer`（`setAutoDelete(false)`，XToolTip.c:149-153），`hideText` 只 `XTimer_stop_base`（XToolTip.c:236-237），模块无 teardown 入口——按 ASan 口径是进程级常驻单例，不属于对象泄漏。
4. **无窗口环境 `isVisible` 会返回 true**：`xtooltip_ensureWidget` 创建失败时走「仅存储状态」分支，仍置 `s_tooltipVisible = true`（XToolTip.c:190-199）。`isVisible()==true` 不保证屏幕上真有提示——这也是 apitest 能在无头环境断言可见的原因（`xgui_demo_apitest_labels.c:1144`）。
5. **`palette` 在 `XPALETTE_ON=0` 时退化为占位结构体**：`XToolTip_palette` 的 `#else` 分支只写 `out.m_disabled = 0`（XToolTip.c:292-303），而 `XPalette` 此时的定义也只有 `int m_disabled`（XPalette.h:170-173）——两处需同步维护。
6. **悬停提示链已接通在 ENTER，不在 ToolTip 事件**：`XWidget.c` 的 `XEVENT_TYPE_ENTER` 分支直接按 `XWidget_toolTip` + `XWidget_toolTipDuration` 调 `XToolTip_showText`（XWidget.c:5914-5925，判空在 :5915-5916）。`out/xwidget_align/appearance.md:95` 与 `:189` 记的「X 侧提示链未接通」已按当前工作区复核**不再成立**（该矩阵成文时 `XWidget.c` 无该调用）。

**测试覆盖**：无 `Test/XGuiTest/XToolTipTest.c` 专属套件（`ls` → MISSING）。三处间接覆盖：`Test/XGuiDemo/xgui_demo_apitest_labels.c:1136-1210`（7 条：空文本等价 hideText/同文复用/换文重摆/字体 NULL 忽略/调色板往返）；`Test/XGuiDemo/xgui_demo_apitest_menus.c:1845-1895`（10 条）；`Test/XGuiDemo/xgui_demo_page_advanced.c:1613-1640`（4 条 `ADV_EXPECT`）。仓库根 `xgui_regression_test.c:37308-37370` 另有 Task 2.19a 子批（6 条 `expect_true`）。

---

## XTreeView

**定位**：树视图（扁平 model 第一列渲染 + 展开族/列状态/排序状态平行数组承载）。继承链 `XObject ← XWidget ← XFrame ← XAbstractScrollArea ← XAbstractItemView ← XTreeView`（XAbstractScrollArea.h:61 `XFrame m_base`、XAbstractItemView.h:181 `XAbstractScrollArea m_base`、XTreeView.h:30 `EXTEND_END(XTreeView, XAbstractItemView)`）。头文件 `Src/XGui/Widget/XTreeView.h`（539 行）。守卫 `XWIDGET_ON && XTABLEWIDGET_ON`（XTreeView.h:26）——**没有 XTREEVIEW_ON**（`grep -c XTREEVIEW_ON Src/XGui/XGuiConfig.h` → 0）。

**生命周期**：`XTreeView_init` 先 `XAbstractItemView_init` 再挂虚表，四条平行数组全置 NULL/0，`m_indentation=20`、`m_rowHeight=24`、`m_headerHidden=false`、`m_expandsOnDoubleClick/itemsExpandable/rootIsDecorated=true`、`m_sortColumn=-1`、`m_treePosition=0`、`m_autoExpandDelay=-1`、其余开关 false（XTreeView.c:316-346）。`VXTreeView_deinit` 释放 4 条 `XMalloc_System` 数组后交 `XClass_Deinit_Parent(XAbstractItemView,…)`（XTreeView.c:359-382）。`m_header` 为借用，不释放（XTreeView.h:41）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTreeView_class_init(void);` | 建表：Deinit/Copy/Move/Paint/ScrollContentsBy/IndexAt/VisualRect/KeyPress/MousePress/MouseDoubleClick 槽 | X 扩展 | 共享表，禁释放 |
| `void XTreeView_init(XTreeView* self, XWidget* parent, XWidgetFlags flags);` | 初始化（缩进 20、行高 24、表头可见） | `QTreeView(QWidget*)` qtreeview.h:35 | — |
| `XTreeView* XTreeView_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `#define XTreeView_create(parent, flags) XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `void XTreeView_setIndentation(XTreeView* self, int indentation);` | 设缩进宽（<0 忽略） | `setIndentation` qtreeview.h:49 | — |
| `int XTreeView_indentation(const XTreeView* self);` | 查缩进宽（NULL→0） | `indentation` qtreeview.h:48 | — |
| `void XTreeView_resetIndentation(XTreeView* self);` | 缩进复位为固定常量 20（非样式测算） | `resetIndentation` qtreeview.h:50 | — |
| `void XTreeView_setHeaderHidden(XTreeView* self, bool hidden);` | 设表头隐藏，并镜像已挂接表头对象的可见性 | `setHeaderHidden` qtreeview.h:73 | — |
| `bool XTreeView_isHeaderHidden(const XTreeView* self);` | 查表头隐藏 | `isHeaderHidden` qtreeview.h:72 | — |
| `void XTreeView_setRowHeight(XTreeView* self, int height);` | 设统一行高（<=0 忽略）——项目库扩展 | X 扩展（Qt 走 `rowHeight(index)` qtreeview.h:180） | — |
| `int XTreeView_rowHeight(const XTreeView* self);` | 查行高（NULL→24） | 同上 | — |
| `void XTreeView_expand(XTreeView* self, int row);` | 展开行；折叠→展开时发 expanded(row) | `expand` qtreeview.h:121 | — |
| `void XTreeView_collapse(XTreeView* self, int row);` | 收起行；展开→折叠时发 collapsed(row) | `collapse` qtreeview.h:122 | — |
| `bool XTreeView_isExpanded(const XTreeView* self, int row);` | 查展开态；越界 false | `isExpanded` qtreeview.h:81 | — |
| `void XTreeView_setExpanded(XTreeView* self, int row, bool expand);` | 设展开态并差分发信号 | `setExpanded` qtreeview.h:82 | — |
| `void XTreeView_expandAll(XTreeView* self);` | 全展开；**不逐行发** expanded | `expandAll` qtreeview.h:125 | — |
| `void XTreeView_collapseAll(XTreeView* self);` | 全收起；不逐行发 collapsed | `collapseAll` qtreeview.h:127 | — |
| `void XTreeView_expandToDepth(XTreeView* self, int depth);` | 简化：忽略 depth，全展开 | `expandToDepth` qtreeview.h:128 | — |
| `void XTreeView_expandRecursively(XTreeView* self, int row);` | 简化：忽略 row，等价 expandAll | `expandRecursively` qtreeview.h:126 | — |
| `void XTreeView_setExpandsOnDoubleClick(XTreeView* self, bool enable);` | 设双击切换展开 | `setExpandsOnDoubleClick` qtreeview.h:62 | — |
| `bool XTreeView_expandsOnDoubleClick(const XTreeView* self);` | 查（NULL→true） | `expandsOnDoubleClick` qtreeview.h:61 | — |
| `void XTreeView_setItemsExpandable(XTreeView* self, bool enable);` | 设条目可展开（渲染/交互分支判定用） | `setItemsExpandable` qtreeview.h:59 | — |
| `bool XTreeView_itemsExpandable(const XTreeView* self);` | 查（NULL→true） | `itemsExpandable` qtreeview.h:58 | — |
| `void XTreeView_setRootIsDecorated(XTreeView* self, bool show);` | 设绘制 +/- 展开控件列 | `setRootIsDecorated` qtreeview.h:53 | — |
| `bool XTreeView_rootIsDecorated(const XTreeView* self);` | 查（NULL→true） | `rootIsDecorated` qtreeview.h:52 | — |
| `void XTreeView_setSortingEnabled(XTreeView* self, bool enable);` | 设排序使能（状态存取） | `setSortingEnabled` qtreeview.h:84 | — |
| `bool XTreeView_isSortingEnabled(const XTreeView* self);` | 查排序使能 | `isSortingEnabled` qtreeview.h:85 | — |
| `void XTreeView_setUniformRowHeights(XTreeView* self, bool uniform);` | 设等高行（状态存取，渲染恒统一行高） | `setUniformRowHeights` qtreeview.h:56 | — |
| `bool XTreeView_uniformRowHeights(const XTreeView* self);` | 查等高行 | `uniformRowHeights` qtreeview.h:55 | — |
| `int XTreeView_indexAbove(const XTreeView* self, int row);` | 视觉上一行（row-1）；无上行 -1 | `indexAbove` qtreeview.h:104 | — |
| `int XTreeView_indexBelow(const XTreeView* self, int row);` | 视觉下一行（row+1）；末行 -1 | `indexBelow` qtreeview.h:105 | — |
| `void XTreeView_setColumnHidden(XTreeView* self, int column, bool hide);` | 设列隐藏（<0 忽略；按需扩列状态表并发 columnCountChanged） | `setColumnHidden` qtreeview.h:70 | — |
| `bool XTreeView_isColumnHidden(const XTreeView* self, int column);` | 查列隐藏；越界 false | `isColumnHidden` qtreeview.h:69 | — |
| `void XTreeView_setColumnWidth(XTreeView* self, int column, int width);` | 设列宽（0=自动均摊；<0 忽略）；实际变化发 columnResized | `setColumnWidth` qtreeview.h:66 | — |
| `int XTreeView_columnWidth(const XTreeView* self, int column);` | 查列宽；未设/越界 0 | `columnWidth` qtreeview.h:65 | — |
| `void XTreeView_hideColumn(XTreeView* self, int column);` | 转发 setColumnHidden(column,true) | `hideColumn` qtreeview.h:119 | — |
| `void XTreeView_showColumn(XTreeView* self, int column);` | 转发 setColumnHidden(column,false) | `showColumn` qtreeview.h:120 | — |
| `void XTreeView_resizeColumnToContents(XTreeView* self, int column);` | 简化：仅调度一次全量重绘，列宽不变 | `resizeColumnToContents` qtreeview.h:123 | — |
| `void XTreeView_sortByColumn(XTreeView* self, int column, int order);` | 仅记录排序列/方向并重绘，不排序不发信号 | `sortByColumn` qtreeview.h:124 | — |
| `int XTreeView_sortColumn(const XTreeView* self);` | 查排序列；未设/NULL -1 | `sortColumn`（QTreeView 无此 getter；对位 `QHeaderView::sortIndicatorSection` qheaderview.h:110） | — |
| `int XTreeView_sortIndicatorOrder(const XTreeView* self);` | 查排序方向；NULL→0 | `QHeaderView::sortIndicatorOrder` qheaderview.h:111 | — |
| `XRect XTreeView_visualRect(const XTreeView* self, int row, int column);` | 单元格视口矩形；无效入参 {0,0,0,0} | `visualRect` qtreeview.h:101 | 值 |
| `int XTreeView_columnViewportPosition(const XTreeView* self, int column);` | 列水平起始；越界/NULL -1 | `columnViewportPosition` qtreeview.h:64 | — |
| `int XTreeView_rowAt(const XTreeView* self, int y);` | 视口 y→行号（统一行高） | `QTableView::rowAt` qtableview.h:43 | — |
| `int XTreeView_columnAt(const XTreeView* self, int x);` | 视口 x→列号（按可见列累计宽） | `columnAt` qtreeview.h:67（另 `QTableView::columnAt` qtableview.h:49） | — |
| `void XTreeView_setSelectionRectVisible(XTreeView* self, bool visible);` | 橡皮筋选区可见（状态存取） | `QListView::setSelectionRectVisible` qlistview.h:90（QTreeView 无此 API） | — |
| `bool XTreeView_isSelectionRectVisible(const XTreeView* self);` | 查（NULL→false） | `QListView::isSelectionRectVisible` qlistview.h:91 | — |
| `void XTreeView_setAllColumnsShowFocus(XTreeView* self, bool enable);` | 焦点框横贯全部列 | `setAllColumnsShowFocus` qtreeview.h:90 | — |
| `bool XTreeView_allColumnsShowFocus(const XTreeView* self);` | 查 | `allColumnsShowFocus` qtreeview.h:91 | — |
| `void XTreeView_setTreePosition(XTreeView* self, int column);` | 树位置列（状态存取，绘制恒按第 0 列） | `setTreePosition` qtreeview.h:96 | — |
| `int XTreeView_treePosition(const XTreeView* self);` | 查（NULL→0） | `treePosition` qtreeview.h:97 | — |
| `void XTreeView_setAnimated(XTreeView* self, bool enable);` | 展开/收起动画（状态存取，无动画管线） | `setAnimated` qtreeview.h:87 | — |
| `bool XTreeView_isAnimated(const XTreeView* self);` | 查 | `isAnimated` qtreeview.h:88 | — |
| `void XTreeView_setWordWrap(XTreeView* self, bool on);` | 文本自动换行（状态存取，绘制单行省略） | `setWordWrap` qtreeview.h:93 | — |
| `bool XTreeView_wordWrap(const XTreeView* self);` | 查 | `wordWrap` qtreeview.h:94 | — |
| `void XTreeView_setAutoExpandDelay(XTreeView* self, int delay);` | 悬停自动展开延时（状态存取，无拖放路径） | `setAutoExpandDelay` qtreeview.h:46 | — |
| `int XTreeView_autoExpandDelay(const XTreeView* self);` | 查；NULL -1 | `autoExpandDelay` qtreeview.h:45 | — |
| `void XTreeView_setRowHidden(XTreeView* self, int row, bool hide);` | 设行隐藏（<0/越界忽略） | `setRowHidden` qtreeview.h:76 | — |
| `bool XTreeView_isRowHidden(const XTreeView* self, int row);` | 查行隐藏；越界/无表 false | `isRowHidden` qtreeview.h:75 | — |
| `void XTreeView_setFirstColumnSpanned(XTreeView* self, int row, bool span);` | **空操作**（不存储不发信号） | `setFirstColumnSpanned` qtreeview.h:79 | — |
| `bool XTreeView_isFirstColumnSpanned(const XTreeView* self, int row);` | 恒 false | `isFirstColumnSpanned` qtreeview.h:78 | — |
| `void XTreeView_setHeader(XTreeView* self, XHeaderView* header);` | 挂接水平表头（非水平方向忽略；NULL 清除） | `setHeader` qtreeview.h:43 | `header` 借用，不转移所有权 |
| `XHeaderView* XTreeView_header(const XTreeView* self);` | 查挂接表头；未挂 NULL | `header` qtreeview.h:42 | 借用 |
| `void XTreeView_dataChanged(XTreeView* self, int topRow, int leftCol, int bottomRow, int rightCol);` | 区间校验后整体重绘 | `dataChanged` qtreeview.h:110-111 | — |
| `void* XTreeView_expanded_signal(XTreeView* self, int row);` | expanded(int) 信号标识（真发射） | `expanded(const QModelIndex&)` qtreeview.h:115 | 信号标识 |
| `void* XTreeView_collapsed_signal(XTreeView* self, int row);` | collapsed(int) 信号标识（真发射） | `collapsed` qtreeview.h:116 | 信号标识 |
| `void* XTreeView_columnResized_signal(XTreeView* self, int column, int oldSize, int newSize);` | 列宽变化信号（受保护槽对位，C 无访问分区故公开） | `columnResized` qtreeview.h:131（protected Q_SLOT） | 信号标识 |
| `void* XTreeView_columnCountChanged_signal(XTreeView* self, int oldCount, int newCount);` | 列数变化信号（列状态表长度实际变化时） | `columnCountChanged` qtreeview.h:132（protected Q_SLOT） | 信号标识 |

**所有权与命名契约**：`_const` 借用族 = 全部 getter（`indentation`/`isHeaderHidden`/`rowHeight`/`isExpanded`/`expandsOnDoubleClick`/`itemsExpandable`/`rootIsDecorated`/`isSortingEnabled`/`uniformRowHeights`/`indexAbove`/`indexBelow`/`isColumnHidden`/`columnWidth`/`sortColumn`/`sortIndicatorOrder`/`visualRect`/`columnViewportPosition`/`rowAt`/`columnAt`/`isSelectionRectVisible`/`allColumnsShowFocus`/`treePosition`/`isAnimated`/`wordWrap`/`autoExpandDelay`/`isRowHidden`/`isFirstColumnSpanned`/`header`）；无 `_utf8`/`_2` 重载（本类无字符串 setter）；无三态 `set*`。四条平行数组归对象所有（`m_expanded`/`m_columnHidden`/`m_columnWidths`/`m_rowHidden`，XTreeView.h:37-40 均标「对象拥有」）；`m_header` 显式标注「借用指针（不拥有）」（XTreeView.h:41）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XTreeView)` + `XCLASS_DEFINE_EXTEND_END(XTreeView, XAbstractItemView)`（XTreeView.h:29-30）——零新槽位。

- `EXAbstractScrollArea_ScrollContentsBy` → `VXTreeView_scrollContentsBy`：忽略 dx/dy，只 `XWidget_update`（XTreeView.c:29-35）。
- `EXAbstractItemView_IndexAt` → `VXTreeView_indexAt`：先 `xtv_refreshRowStates`，再按可见行带累计反查（隐藏行 `continue`，XTreeView.c:1060-1098）。
- `EXAbstractItemView_VisualRect` → `VXTreeView_visualRect`：转发 `XTreeView_visualRect` 并以正宽高为命中判据（XTreeView.c:1103-1110）。
- `EXWidget_PaintEvent` → `VXTreeView_paintEvent`：调色板 Base 底 + 表头带 + 行循环（隐藏行 `continue`、展开控件 +/-、省略文本）（XTreeView.c:1281-1407）。
- `EXWidget_KeyPressEvent`/`EXWidget_MousePressEvent`/`EXWidget_MouseDoubleClickEvent` → 展开/收拢交互（XTreeView.c:308-313 注册）。
- `EXClass_Deinit`/`EXClass_Copy`/`EXClass_Move` → 四条数组的释放（:359-382）与深拷贝/转移动（:385-465、:467 起），注册见 `XTreeView_class_init`（XTreeView.c:292-318）。

**注意事项与坑点**

1. **头注释说「树渲染按第一列绘制」、「扁平表格」，与实现一致但和 `XTreeWidget` 分工易混**：XTreeView.h:4-7 明写「当前模型为扁平表格，树渲染按第一列绘制（层次缩进预留 indentation）」，`drawText` 固定取 `XAbstractItemModel_data_2(model, row, 0)`（XTreeView.c:1372）。多列文本渲染只在 `XTreeWidget` 侧（XTreeWidget.c:1795-1821），本类不画第 1 列以后。
2. **行隐藏三处口径互相矛盾**：`VXTreeView_paintEvent` 对隐藏行 `continue`（XTreeView.c:1360），但 `y += rh` 在循环末尾无条件执行（XTreeView.c:1403）——隐藏行仍占一条行带，屏幕留空档；`VXTreeView_indexAt`（虚槽）则按「隐藏行占高 0」累计（XTreeView.c:1087-1096，注释「隐藏行占高 0 不参与累计（对标 Qt 隐藏行不在视口命中）」）；而公开的 `XTreeView_rowAt`（XTreeView.c:838-845）与 `XTreeView_visualRect`（XTreeView.c:868-895）又都按行号直乘行高、完全不看隐藏表。三者对同一控件给出三套几何，QA 用 `rowAt` 反查鼠标落点会和实际命中（走 `indexAt`）不一致。头文件 `XTreeView_visualRect` 的 `@note` 还自称「与 indexAt 同口径」（XTreeView.h:339）、`@param row` 自称「不按模型行数裁剪，与 indexAt 同口径」（XTreeView.h:336）——两句都不成立（`indexAt` 既扣隐藏行又校验行数上界，XTreeView.c:1078-1096）。
3. **`itemsExpandable` 参与交互判定但不参与绘制**：`XTreeView.c:1143`（`xtvw_indicatorHit` 的 `!m_rootIsDecorated || !m_itemsExpandable` 门禁）与 `:1269`（双击切换门禁）两处消费，而 `paintEvent` 的 +/- 指示只判 `m_rootIsDecorated`（XTreeView.c:1373）——置 `itemsExpandable=false` 后仍会画 +/-，只是点不动。
4. **`sortByColumn` 完全不排序**：注释明写「排序本体未接：仅记录排序列与方向」（XTreeView.c:798），`XTreeView.c:794-806` 全函数只有 `m_sortColumn/m_sortOrder` 赋值 + `XWidget_update`。真正按列文本排序的是 `XTreeWidget_sortItems`（XTreeWidget.c:1288-1333）。
5. **`setFirstColumnSpanned` 是显式空操作**：实现只 `(void)row; (void)span; (void)self;`（XTreeView.c:992-999），`isFirstColumnSpanned` 恒 `return false`（XTreeView.c:1001-1007）。头注同样登记「空操作保留（同 XTableView setRowSpan 先例），不存储、不发信号」（XTreeView.h:465-467）。
6. **`setHeaderHidden` 与 `setHeader` 是同一份状态的两种镜像**：`setHeaderHidden` 在写 `m_headerHidden` 后调 `XWidget_setHidden(header, hidden)`（XTreeView.c:535-545）；`setHeader` 挂接时以 `m_headerHidden` 反向镜像表头可见性（XTreeView.c:1011-1021）。两边都写同一对状态，但取消挂接（`setHeader(NULL)`）不会恢复表头可见位——表头对象留在外部时保持隐藏。
7. **`columnWidth` 文档称「0=自动铺满」，但列宽表按访问自动扩长**：`setColumnWidth`/`setColumnHidden` 都先 `need = max(模型列数, column+1)` 再 `xtv_syncColumnStates`（XTreeView.c:726-738、747-768），并在长度变化时发 `columnCountChanged(old,count)`（XTreeView.c:203-210）。即访问超出模型列数的列会「扩表」并报列数变化，头文件未提该副作用。

**测试覆盖**：无 `Test/XGuiTest/XTreeViewTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_views.c:2140-2426`（68 条 `XAPI_EXPECT`，含默认值/展开族/信号/列状态/几何反查/表头挂接/排序承载/行隐藏/首列合并恒 false/dataChanged）；仓库根 `xgui_regression_test.c` 对 `XTreeView` 命中 33 处（`grep -c`），散布在 `test_phase31_p1_contract`/`test_phase32_p2_contract` 的空树与列隐藏安全用例（如 `:33813-33822`、`:34062-34065`、`:34694-34697`），无独立契约函数。

---

## XTreeWidget

**定位**：树控件（自持条目树 + 自绘递归渲染 + 内建模型桥 + 平铺行号便捷族）。继承链 `XObject ← XWidget ← XFrame ← XAbstractScrollArea ← XAbstractItemView ← XTreeView ← XTreeWidget`（XTreeWidget.h:68 `EXTEND_END(XTreeWidget, XTreeView)`）。头文件 `Src/XGui/Widget/XTreeWidget.h`（657 行）。守卫 `XWIDGET_ON && XTABLEWIDGET_ON`（XTreeWidget.h:33）。

**生命周期**：`XTreeWidget_init` 先 `XTreeView_init` 再建内建模型桥（`XAbstractItemModel_create` + `XAbstractItemView_setModel`）、`m_sortColumn=-1`、`m_enteredRow=-2`、`m_columnCount=1`、创建 `m_invisibleRoot`（owner=本控件）与 `m_headerItem`（`xtw_createHeaderItem(m_columnCount)`），最后挂本类虚表（XTreeWidget.c:834-860，`XClassSetVtable` 在 :859）。`VXTreeWidget_deinit` 清两个 static 手势/命中态守卫（`xtw_g_curChildOwner`/`xtw_g_resizeOwner`）、逐顶层条目 `XTreeWidgetItem_delete` + 释放部件行表（部件借用不删）、释放顶层数组/根条目/表头条目/展开表/部件表/表头文本表，**显式 `XClassDelete(m_bridgeModel)`**（XTreeWidget.c:934-942，注释「基类视图按 Qt 语义对 setModel 传入的模型只借用、不拥有，故自建桥必须在此显式析构」），再交 `XClass_Deinit_Parent(XTreeView,…)`（XTreeWidget.c:874-945，交父类在 :944）。条目所有权：`XTreeWidgetItem` 由树拥有（`addTopLevelItem`/`insertTopLevelItem` 转移，`takeTopLevelItem` 归还），子条目由父条目拥有。

**API 面**

先列条目类型（`XTreeWidgetItem`，结构体 XTreeWidget.h:40-64）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XTreeWidgetItem* XTreeWidgetItem_create(const XString* text, XTreeWidgetItem* parent);` | 建条目（文本深拷贝；parent NULL=顶层） | `QTreeWidgetItem(QTreeWidgetItem*,int)` qtreewidget.h:36 | 调用方拥有（或交给树/父条目） |
| `XTreeWidgetItem* XTreeWidgetItem_create_2(const char* text, XTreeWidgetItem* parent);` | 同上（UTF-8 重载） | 同上 | 同上 |
| `void XTreeWidgetItem_delete(XTreeWidgetItem* item);` | 释放条目及其整棵子树 | `~QTreeWidgetItem()` qtreewidget.h:40 | 见语义 |
| `const XString* XTreeWidgetItem_text(const XTreeWidgetItem* item);` | 读列 0 文本 | `text(int)` qtreewidget.h:68-69（column=0） | 借用，禁释放 |
| `const char* XTreeWidgetItem_text_2(const XTreeWidgetItem* item);` | 同上（UTF-8 借用） | 同上 | 借用 |
| `void XTreeWidgetItem_setText(XTreeWidgetItem* item, const XString* text);` | 设列 0 文本（NULL=清空）；顶层挂载条目发 itemChanged(row) | `setText` qtreewidget.h:70 | `text` 借用，内部拷贝 |
| `void XTreeWidgetItem_setText_2(XTreeWidgetItem* item, const char* text);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用 |
| `const XString* XTreeWidgetItem_textAt(const XTreeWidgetItem* item, int column);` | 读指定列文本（0=主文本槽，>=1 走逐列表） | `text(int)` qtreewidget.h:68-69 | 借用 |
| `const char* XTreeWidgetItem_textAt_2(const XTreeWidgetItem* item, int column);` | 同上（UTF-8） | 同上 | 借用 |
| `void XTreeWidgetItem_setTextAt(XTreeWidgetItem* item, int column, const XString* text);` | 设指定列文本（<0 忽略；列 0 转发 setText）；发 itemChanged(row) | `setText(column,…)` qtreewidget.h:70 | `text` 借用，内部拷贝 |
| `void XTreeWidgetItem_setTextAt_2(XTreeWidgetItem* item, int column, const char* text);` | 同上（UTF-8） | 同上 | UTF-8 借用 |
| `int XTreeWidgetItem_checkState(const XTreeWidgetItem* item);` | 读勾选态（XItemCheckState）；NULL→Unchecked | `checkState(column)` qtreewidget.h:123-124 | 值 |
| `void XTreeWidgetItem_setCheckState(XTreeWidgetItem* item, int state);` | 设勾选态（越界钳 Unchecked，同态短路）并发 itemChanged(row) | `setCheckState` qtreewidget.h:125-126 | — |
| `bool XTreeWidgetItem_addChild(XTreeWidgetItem* item, XTreeWidgetItem* child);` | 追加子条目（**所有权转移**给 item） | `addChild` qtreewidget.h:155 | 转移 |
| `int XTreeWidgetItem_childCount(const XTreeWidgetItem* item);` | 子条目数 | `childCount` qtreewidget.h:151 | — |
| `XTreeWidgetItem* XTreeWidgetItem_child(const XTreeWidgetItem* item, int index);` | 取子条目；越界 NULL | `child` qtreewidget.h:145-150 | 借用 |

树控件本体：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XTreeWidget_class_init(void);` | 建表：Deinit/Paint/MousePress/MouseMove/MouseRelease/MouseDoubleClick/KeyPress/Wheel 槽 | X 扩展 | 共享表，禁释放 |
| `void XTreeWidget_init(XTreeWidget* self, XWidget* parent, XWidgetFlags flags);` | 初始化（建模型桥/根条目/表头条目，列数 1） | `QTreeWidget(QWidget*)` qtreewidget.h:234 | — |
| `XTreeWidget* XTreeWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `#define XTreeWidget_create(parent, flags) XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `bool XTreeWidget_addTopLevelItem(XTreeWidget* self, XTreeWidgetItem* item);` | 追加顶层条目（所有权转入；失败留在调用方） | `addTopLevelItem` qtreewidget.h:244 | 转移 |
| `bool XTreeWidget_insertTopLevelItem(XTreeWidget* self, int index, XTreeWidgetItem* item);` | 插入顶层条目（index 越界返回 false） | `insertTopLevelItem` qtreewidget.h:243 | 转移 |
| `XTreeWidgetItem* XTreeWidget_topLevelItem(const XTreeWidget* self, int index);` | 取顶层条目；越界 NULL | `topLevelItem` qtreewidget.h:241 | 借用 |
| `int XTreeWidget_topLevelItemCount(const XTreeWidget* self);` | 顶层条目数 | `topLevelItemCount` qtreewidget.h:242 | — |
| `XTreeWidgetItem* XTreeWidget_invisibleRootItem(const XTreeWidget* self);` | 不可见根（children 借用顶层存储；**调用方不得删除**） | `invisibleRootItem` qtreewidget.h:240 | 借用，禁删 |
| `XTreeWidgetItem* XTreeWidget_itemFromIndex(const XTreeWidget* self, int row);` | 行号→条目（恒等简化） | `itemFromIndex` qtreewidget.h:287 | 借用 |
| `XTreeWidgetItem* XTreeWidget_takeTopLevelItem(XTreeWidget* self, int index);` | 移除顶层条目并归还所有权；越界 NULL | `takeTopLevelItem` qtreewidget.h:245 | 转移回调用方 |
| `void XTreeWidget_clear(XTreeWidget* self);` | 清空条目（释放部件表、复位当前索引、发 current/selection 信号；**不清表头文本**） | `clear` qtreewidget.h:296 | — |
| `int XTreeWidget_currentItem(const XTreeWidget* self);` | 当前条目**行号**；越界无当前项 -1 | `currentItem` qtreewidget.h:256（返回条目指针） | 值 |
| `void XTreeWidget_setCurrentItem(XTreeWidget* self, int row);` | 设当前行并滚动可见（越界忽略） | `setCurrentItem` qtreewidget.h:258 | — |
| `XWidget* XTreeWidget_itemWidget(const XTreeWidget* self, int row, int column);` | 取单元格挂载部件；未挂 NULL | `itemWidget` qtreewidget.h:275 | 借用，禁释放 |
| `void XTreeWidget_setItemWidget(XTreeWidget* self, int row, int column, XWidget* widget);` | 挂载单元格部件（不取得所有权、不重设父） | `setItemWidget` qtreewidget.h:276 | 借用 |
| `void XTreeWidget_removeItemWidget(XTreeWidget* self, int row, int column);` | 置空挂载指针（不删除部件） | `removeItemWidget` qtreewidget.h:277 | — |
| `int XTreeWidget_addTopLevelItems(XTreeWidget* self, const char* const* texts, int count);` | 批量追加文本行；返回成功追加数 | `addTopLevelItems` qtreewidget.h:249 | 文本借用 |
| `int XTreeWidget_insertTopLevelItems(XTreeWidget* self, int index, const char* const* texts, int count);` | 批量插入文本行；返回成功插入数 | `insertTopLevelItems` qtreewidget.h:248 | 文本借用 |
| `XRect XTreeWidget_visualItemRect(const XTreeWidget* self, int row);` | 条目可视矩形（行高/展开态累计）；越界全零 | `visualItemRect` qtreewidget.h:264 | 值 |
| `int XTreeWidget_sortColumn(const XTreeWidget* self);` | 最近 sortItems 的列；未排序/NULL -1 | `sortColumn` qtreewidget.h:266 | — |
| `void* XTreeWidget_itemClicked_signal(XTreeWidget* self, int row);` | itemClicked(row) 信号（调用即发射并返回地址） | `itemClicked(QTreeWidgetItem*,int)` qtreewidget.h:300 | 信号标识 |
| `void* XTreeWidget_itemDoubleClicked_signal(XTreeWidget* self, int row);` | itemDoubleClicked(row) | `itemDoubleClicked` qtreewidget.h:301 | 信号标识 |
| `void* XTreeWidget_itemPressed_signal(XTreeWidget* self, int row);` | itemPressed(row) | `itemPressed` qtreewidget.h:299 | 信号标识 |
| `void* XTreeWidget_itemActivated_signal(XTreeWidget* self, int row);` | itemActivated(row)（键盘激活路径未建立） | `itemActivated` qtreewidget.h:302 | 信号标识 |
| `void* XTreeWidget_itemEntered_signal(XTreeWidget* self, int row);` | itemEntered(row)（mouseMove 差分发射） | `itemEntered` qtreewidget.h:303 | 信号标识 |
| `void* XTreeWidget_itemChanged_signal(XTreeWidget* self, int row);` | itemChanged(row)（文本/勾选变化） | `itemChanged(const QTreeWidgetItem*,int)` qtreewidget.h:304 | 信号标识 |
| `void* XTreeWidget_itemExpanded_signal(XTreeWidget* self, int row);` | itemExpanded(row)（独立于基类 expanded） | `itemExpanded` qtreewidget.h:305 | 信号标识 |
| `void* XTreeWidget_itemCollapsed_signal(XTreeWidget* self, int row);` | itemCollapsed(row) | `itemCollapsed` qtreewidget.h:306 | 信号标识 |
| `void* XTreeWidget_currentItemChanged_signal(XTreeWidget* self, int current, int previous);` | currentItemChanged(current,previous)（行号载荷） | `currentItemChanged` qtreewidget.h:307 | 信号标识 |
| `void* XTreeWidget_itemSelectionChanged_signal(XTreeWidget* self);` | itemSelectionChanged()（无载荷） | `itemSelectionChanged` qtreewidget.h:308 | 信号标识 |
| `int XTreeWidget_columnCount(const XTreeWidget* self);` | 列数（状态承载；默认 1） | `columnCount` qtreewidget.h:237 | — |
| `void XTreeWidget_editItem(XTreeWidget* self, int row, int column);` | 句柄预留：校验入参后无操作 | `editItem` qtreewidget.h:269 | — |
| `int XTreeWidget_indexFromItem(const XTreeWidget* self, const XTreeWidgetItem* item);` | 条目→行号（恒等；未挂树/子条目 -1） | `indexFromItem` qtreewidget.h:286 | — |
| `void XTreeWidget_scrollToItem(XTreeWidget* self, int row);` | 滚动使指定行可见（越界忽略） | `scrollToItem` qtreewidget.h:292-293 | — |
| `int XTreeWidget_selectedItems(const XTreeWidget* self, int* outRows, int maxCount);` | 读选中行号数组；返回写出个数 | `selectedItems` qtreewidget.h:279 | 输出缓冲调用方所有 |
| `int XTreeWidget_findItems(const XTreeWidget* self, const char* text, int flags, int* outRows, int maxCount);` | 按文本查条目（前序遍历全树）；返回命中总数 | `findItems` qtreewidget.h:280-281 | 输出缓冲调用方所有 |
| `void XTreeWidget_sortItems(XTreeWidget* self, int column, int order);` | 按列文本冒泡排序（稳定，整行随动） | `sortItems` qtreewidget.h:267 | — |
| `void XTreeWidget_setTextAt_2(XTreeWidget* self, int row, int column, const char* text);` | 行级便捷设文本（列 0 与主文本同槽） | X 扩展（Qt 无行级便捷 setter） | UTF-8 借用 |
| `int XTreeWidget_itemAt(const XTreeWidget* self, int x, int y);` | 位置反查行号（展开态行带口径） | `itemAt` qtreewidget.h:262-263 | — |
| `void XTreeWidget_setHeaderLabels(XTreeWidget* self, const char* const* labels, int count);` | 批量设表头文本（倍增扩容） | `setHeaderLabels` qtreewidget.h:253 | 文本借用，内部拷贝 |
| `void XTreeWidget_setHeaderLabel(XTreeWidget* self, const char* label);` | 单标签（转发批量族） | `setHeaderLabel` qtreewidget.h:254 | 同上 |
| `const char* XTreeWidget_headerLabel(const XTreeWidget* self, int column);` | 读列表头文本；未设/越界空串 | `headerItem()->text(column)` qtreewidget.h:251（Qt 无同名 API） | 借用 |
| `XTreeWidgetItem* XTreeWidget_headerItem(const XTreeWidget* self);` | 表头条目（init 创建；失败 NULL） | `headerItem` qtreewidget.h:251 | 借用 |
| `void XTreeWidget_setHeaderItem(XTreeWidget* self, XTreeWidgetItem* item);` | 设表头条目（**接管所有权**，旧表头条目由控件删） | `setHeaderItem` qtreewidget.h:252 | 转移 |
| `void XTreeWidget_expandItem(XTreeWidget* self, int row);` | 展开顶层行（对接 m_topExpanded；发 itemExpanded） | `expandItem` qtreewidget.h:294 | — |
| `void XTreeWidget_collapseItem(XTreeWidget* self, int row);` | 收起顶层行（发 itemCollapsed） | `collapseItem` qtreewidget.h:295 | — |
| `int XTreeWidget_indexOfTopLevelItem(const XTreeWidget* self, const char* text);` | 按文本反查顶层行号（首个精确匹配） | `indexOfTopLevelItem(QTreeWidgetItem*)` qtreewidget.h:246（Qt 收条目指针） | — |
| `int XTreeWidget_itemAbove(const XTreeWidget* self, int row);` | 上一行（row-1）；无/越界 -1 | `itemAbove` qtreewidget.h:283 | — |
| `int XTreeWidget_itemBelow(const XTreeWidget* self, int row);` | 下一行（row+1）；越界 -1 | `itemBelow` qtreewidget.h:284 | — |
| `void XTreeWidget_setColumnCount(XTreeWidget* self, int count);` | 设列数（<1 忽略；同步扩部件列容量） | `setColumnCount` qtreewidget.h:238 | — |

**所有权与命名契约**：`_const` 借用族 = `topLevelItem`/`invisibleRootItem`/`itemFromIndex`/`itemWidget`/`currentItem`/`sortColumn`/`columnCount`/`indexFromItem`/`selectedItems`/`findItems`/`itemAt`/`headerLabel`/`headerItem`/`indexOfTopLevelItem`/`itemAbove`/`itemBelow`（另条目族的 `text`/`text_2`/`textAt`/`textAt_2`/`checkState`/`childCount`/`child`）。`_2` 后缀**全部**是 XString→UTF-8 的字符串重载（`XTreeWidgetItem_create_2`/`_setText_2`/`_text_2`/`_setTextAt_2`/`_textAt_2`、`XTreeWidget_setTextAt_2`），无一处用 `_utf8`。无三态 `set*`（`setItemWidget` 用 NULL 清、`setTextAt`/`setText` 用 NULL 清空）。所有权三条硬线：条目转移（add/insert/setHeaderItem 接手，take 归还）、部件借用（`setItemWidget` 不删不 reparent）、不可见根/表头条目借用禁删（XTreeWidget.h:244-251、575-591）。

**可覆写点**：`XCLASS_DEFINE_BEGING(XTreeWidget)` + `XCLASS_DEFINE_EXTEND_END(XTreeWidget, XTreeView)`（XTreeWidget.h:67-68）——零新槽位。

- `EXWidget_PaintEvent` → `VXTreeWidget_paintEvent`：Base 底 + 表头带（`xtw_drawHeader`）+ 行带裁剪平移 + `xtw_drawItem` 递归（XTreeWidget.c:1924-2021）。
- `EXWidget_MousePressEvent`/`MouseReleaseEvent`/`MouseDoubleClickEvent` → 命中行走按/点击/双击（含指示器切换与列表宽拖拽）。
- `EXWidget_MouseMoveEvent` → `VXTreeWidget_mouseMoveEvent`：itemEntered 差分（XGui.md:826-839 §8.0g16）。
- `EXWidget_KeyPressEvent`/`EXWidget_WheelEvent` → 键盘导航（XTreeWidget.c:2031 起）与滚轮（XTreeWidget.c:2415 起，:2399 注释对标 `QAbstractScrollArea::wheelEvent`）。
- `EXClass_Deinit` → `VXTreeWidget_deinit`（XTreeWidget.c:874-945）。

**注意事项与坑点**

1. **`columnCount` 的头注释说「自绘当前仍为平铺单文本列渲染」，实现已做多列绘制**：XTreeWidget.h:448-452 写「自绘当前仍为平铺单文本列渲染（每行一次 drawText），多列渲染待列绘制路径建立后接入」，实现 `xtw_drawItem` 在 `columnCount>1` 时逐列 `xtw_columnSpan` + `IntersectClip` + `drawText`（XTreeWidget.c:1795-1821），且 XGui.md §8.0g18 已登记「多列绘制消费」落地（XGui.md:859-868）。头注落后于实现。
2. **`setHeaderLabels` 的「自绘当前无表头带」同样过期**：XTreeWidget.h:555 写「自绘当前无表头带（表头不占行带）」，实现 `paintEvent` 里 `if (headerOffset > 0) xtw_drawHeader(tw, &painter, r.width)`（XTreeWidget.c:1982），`xtw_headerOffset` 按 `XTW_HEADER_H=20` 返回（XTreeWidget.c:22、47-51）。**表头确实占 20px 行带**——apitest 也因此把注入坐标 +20（`xgui_demo_apitest_views.c:2541-2543`），XGui.md:834-835 亦记「注入坐标须 visualItemRect 内容坐标加回 20px 表头带」。
3. **`sortItems` 的 `column` 文档说「仅 0 有效」，实现支持任意列**：XTreeWidget.h:514 写「排序列（本控件恒单文本列，仅 0 有效）」，实现按 `XTreeWidgetItem_textAt_2(item, column)` 取键并接受 `0 <= column < columnCount`（XTreeWidget.c:1288-1333，列键取在 :1299-1301）；XGui.md:848-851 明确「sortItems(column,order) 此前收列参数却恒按列 0 比较——现按 textAt(column) 取键」（§8.0g17）。文档未同步。
4. **`selectedItems` 的「计数查询」与「截断返回值」文档不成立**：XTreeWidget.h:485-486 写「maxCount<=0 表示只统计不写出」「返回值可能大于 maxCount 表示截断」，实现 `if (!self || !outRows || maxCount <= 0) return 0;` 后循环只在 `found < maxCount` 内累加（XTreeWidget.c:1212-1225）——**maxCount<=0 直接返回 0**（不计数），返回值恒 ≤ maxCount（不报告截断）。同文件 `findItems` 的同类说法是成立的（`ctx.hits` 独立累加，XTreeWidget.c:1257-1264），两族口径不同。
5. **`findItems` 的 flags 语义与 Qt `Qt::MatchFlag` 数值相反**：头文件 XTreeWidget.h:499-500 与实现 XTreeWidget.c:1250-1264（分支在 :1253）都以 **1=精确相等、0=包含子串**；而 `Qt::MatchExactly = 0`、`Qt::MatchContains = 1`（qnamespace.h:1531-1532）。照 Qt 常量直传会得到相反结果。
6. **`editItem` 是显式无操作**：实现只做入参校验后直接返回（XTreeWidget.c:1190-1196，注释「句柄预留：无编辑器/委托机制，编辑触发无从落地」）；apitest 有一条 `XAPI_EXPECT(true, "editItem 句柄预留为无操作（编辑路径未建）")`（`xgui_demo_apitest_views.c:2784`）——这条断言恒真，不代表覆盖了编辑能力。
7. **`itemChanged` 只对「顶层且已挂树」条目发射**：`XTreeWidgetItem_setText`/`setTextAt`/`setCheckState` 均按 `item->owner` 定位行号，`owner` 只在 `addTopLevelItem`/`insertTopLevelItem` 时写入、`takeTopLevelItem` 时清空（XTreeWidget.c:957-958、1034）；头文件 XTreeWidget.h:400-404 说明「子条目（平铺行模型无子行号）与未挂树的条目不发射」。改子条目文本不会发信号。
8. **折叠只影响该顶层行的子树，且「展开态」有两套独立承载**：本控件用 `m_topExpanded`（`xtw_isExpanded`，XTreeWidget.c:611-616）驱动绘制/命中，基类 `XTreeView` 的 `m_expanded` 对它无效；头文件 XTreeWidget.h:406-414、595-612 反复说明「基类展开族按模型行承载，本控件条目树未接模型，故在本控件内独立承载与发射」。封装层若混用 `XTreeView_expand` 与 `XTreeWidget_expandItem`，两套状态互不同步。
9. **内建模型桥是自建自删**：`m_bridgeModel` 由 `init` 创建并 `setModel` 给基类，但基类按 Qt 语义只借用，所以 `VXTreeWidget_deinit` 必须显式 `XClassDelete`（XTreeWidget.c:932-937 注释「否则每树泄漏模型本体 + 单元格/表头数组 + setData 文本 + setModel 信号连接（ASan 实测…）」）。派生类覆写 Deinit 不调父类槽即泄漏。
10. **`clear` 的顺序是信号约束**：`clear` 先发 currentItemChanged/itemSelectionChanged，最后才 `xtw_bridgeSync`（XTreeWidget.c:1066-1072），注释「模型桥清空放信号链之后：`setDimension` 发射 `rowsRemoved` 会先重置基类选择/当前状态，抢在信号发射前导致 current/selection 信号丢失（实测 clear 信号断言失败）」（XTreeWidget.c:1069-1071，同见 XGui.md:963-965）。把桥同步提前会静默丢信号。

**测试覆盖**：无 `Test/XGuiTest/XTreeWidgetTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_views.c:2429-2815`（81 条 `XAPI_EXPECT`：条目族/顶层增删/不可见根/表头/前插/批量/信号注入/部件挂载/列数/排序/查找/clear）；`Test/XGuiDemo/xgui_demo_page_views.c` 另有 22 处 API 使用（demo 页）。仓库根 `xgui_regression_test.c` 命中 77 处，主要落在 `test_phase32_p2_contract`（`:33791`、`:34097-34112`、`:34714-34765`）与 `xtw4_*` 夹具（`:33592` 起）。

---

## XWindowDecoration

**定位**：框架级窗口装饰的判定/抑制/承载/输入拦截层（不绘图）——条带绘制已迁入 `XTitleBar`。继承链：**无**——纯静态函数模块，非 XObject 派生（`grep -c XCLASS_DEFINE Src/XGui/Widget/XWindowDecoration.h Src/XGui/Widget/XWindowDecoration.c` → 0/0）。头文件 `Src/XGui/Widget/XWindowDecoration.h`（298 行），另 `XWindowDecoration_Protected.h`（47 行）。守卫 `XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON`（XWindowDecoration.h:57）；无独立开关（`grep -c XWINDOWDECORATION_ON Src/XGui/XGuiConfig.h` → 0）。

**生命周期**：无对象生命周期（模块级静态注册表 `g_xwdStates`，按顶层控件指针懒登记、容量按 2 的幂增长，XWindowDecoration.c:154-211）。注册项在 `XWindowDecoration_notifyWindowDestroyed`（窗口注销）与 `XWindowDecoration_notifyTopDestroyed`（控件先亡，`XWindowDecoration_Protected.h:37` 声明、XWindowDecoration.c:2689）两条钩子中摘除；表清空时 `XMemory_free` 并置 NULL（XWindowDecoration.c:2710-2711）。默认标题条由本模块创建、随装饰失活经 `XObject_deleteLater` 异步释放；自定义条为借用，只解挂不释放（XWindowDecoration.h:20-22、XWindowDecoration.c:2132-2138）。

**API 面**

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `bool XWindowDecoration_activeFor(const XWidget* top);` | 判顶层是否由框架绘制装饰（类型白名单/非 Frameless/策略模式/fbdev Auto） | X 扩展（Qt 交平台 WM） | — |
| `bool XWindowDecoration_activeForFlags(const XWidget* top, XWindowFlags flags);` | 同上，但用调用方给的新 flags（`setWindowFlags` 动态路径消竞态） | X 扩展 | — |
| `XMargins XWindowDecoration_marginsFor(const XWidget* top);` | 装饰保留边距（仅顶部=条高；全屏归零） | X 扩展 | 值 |
| `void XWindowDecoration_syncWindow(XWidget* top);` | 建窗/flags 变化/换条后重算并落盘保留边距、兜底抑制位、承载标题条 | X 扩展 | — |
| `void XWindowDecoration_syncBarGeometry(XWidget* top);` | 几何变化后重钉条区（只钉条，不反向造状态/造条） | X 扩展 | — |
| `void XWindowDecoration_draw(XWidget* top, XPainter* painter, const XRegion* region);` | **兼容空壳**：函数体短路零开销（绘制归 XTitleBar） | X 扩展 | `painter`/`region` 未使用 |
| `bool XWindowDecoration_handlePointer(XWidget* top, XEvent* event);` | 装饰输入拦截（先于控件命中）；已消费返回 true | X 扩展（对标 WM 帧外命中） | `event` 借用 |
| `void XWindowDecoration_handleLeave(XWidget* top);` | 指针离开顶层时清悬停态 | X 扩展 | — |
| `void XWindowDecoration_notifyWindowDestroyed(XWindow* win);` | 窗口注销钩子：丢弃装饰状态 | X 扩展 | `win` 借用 |
| `void XWindowDecoration_notifyActivation(XWindow* win);` | 焦点窗口切换钩子：刷新新旧活动窗标题栏配色 | X 扩展 | `win` 借用 |
| `void XWindowDecoration_notifyAppearanceChanged(XWidget* top);` | 标题/状态等外观属性变化后轻量重绘条带 | X 扩展 | — |
| `bool XWindowDecoration_notifyWindowState(XWidget* top);` | 窗口状态收口：FullScreen 进=条隐藏+边距归零，退=恢复；返回是否应用 | X 扩展 | — |
| `bool XWindowDecoration_gestureActive(void);` | 是否有任一装饰顶层处于拖拽/改尺寸手势 | X 扩展 | — |
| `bool XWindowDecoration_isShaded(const XWidget* top);` | 顶层是否处于最小化卷起态（Shade） | X 扩展（对位 `showMinimized` 语义 qwidget.h:487） | — |
| `int XWindowDecoration_dragSnapshotStepCount(void);` | 拖动快照 blit 落地步累计（恒单调，诊断用） | X 扩展 | — |
| `bool XWindowDecoration_fbdevPanelRect(XRect* outPanel);` | fbdev 面板矩形探测（非 fbdev 恒 false） | X 扩展 | `outPanel` 输出，调用方所有 |
| `void XWindowDecoration_restoreExposeStrips(const XRect* oldG, const XRect* newG, XWindow* selfWindow);` | 让位条带按归属归位还原（fbdev 无 WM 专用） | X 扩展 | 入参借用 |
| `void XWindowDecoration_restoreExposeStripsEx(const XRect* oldG, const XRect* newG, XWindow* selfWindow, bool ownerMayHoldMovedPixels);` | 同上 + 「旧位是否需给归属顶层补标脏」显式闸 | X 扩展 | 入参借用 |
| `void XWindowDecoration_notifyTopDestroyed(XWidget* top);`（XWindowDecoration_Protected.h:37） | 顶层控件销毁摘项钩子（键=控件指针） | X 扩展 | — |

**所有权与命名契约**：`_const` 借用族 = `activeFor`/`activeForFlags`/`marginsFor`/`isShaded`；`notify*` 族入参全为借用（`XWindow*`/`XWidget*`）。无 `_utf8`/`_2` 重载；无三态 `set*`（模块无 setter，状态由 `syncWindow`/`notify*` 驱动）。`_Ex` 后缀是本模块唯一的「增强版」命名（`restoreExposeStripsEx`），语义为「多一个显式布尔闸」，非字符串重载。默认条由模块拥有、自定义条借用（XWindowDecoration.c:2132-2138）。

**可覆写点**：无虚表、无 `_base` 入口（静态模块）。扩展点靠环境/策略而非继承：装饰归属由 `XPlatformThemeDecoration_effectiveMode()`（Framework/System/Auto，环境变量 `XGUI_CSD` 一次性解析）决定（XWindowDecoration.c:2006-2049）。

**注意事项与坑点**

1. **`XWindowDecoration_draw` 是空壳，但入口仍被调用**：函数体三段 `(void)` 后直接返回（XWindowDecoration.c:2154-2166），头文件自称「兼容空壳……函数体短路零开销」（XWindowDecoration.h:128-137）；调用点仍在 `XWidget.c:13007-13012`（`if (!XWindowDecoration_activeFor(top)) return;` 后调 `XWindowDecoration_draw`）。想改条带外观必须改 `XTitleBar` 的 paintEvent，改这里无效。
2. **`handlePointer` 在「别人抓着鼠标」时整段让行**：`if (XWidget_mouseGrabber() != NULL && !st->m_dragging && !st->m_resizing && !st->m_maxRestorePending) return false;`（XWindowDecoration.c:2191-2193），注释记录了真机根因（停靠面板脱靠成浮动态后 CSD 条带吞掉 MOVE/RELEASE，拖动不跟手且落点残留）。自绘标题栏上「按下无反应」时先查全局 mouse grabber。
3. **`activeFor` 与 `activeForFlags` 必须按场景分开用**：`activeFor` 读窗口侧 `XWindow_flags(win)`，若窗口未建则回退控件侧 `m_windowFlags`（XWindowDecoration.c:2051-2065，注释「句柄未建（显示前的布局期）也能预测」）；`activeForFlags` 收调用方新 flags（XWindowDecoration.c:2067-2081），专供 `XWidget_setWindowFlags` 的动态路径——注释记载若用旧 flags 会在 Frameless→正常翻转时误判，导致 Motif hints 按 DECOR_ALL 下发、强制 CSD 下系统标题栏出现且不恢复（「DDE 实测 2026-10-17」）。
4. **`marginsFor` 有独立的全屏闸，早于状态查询**：先判 `xwd_hostFullScreen(top)` 返回全零边距，再取 `xwd_stripHeight(xwd_stateFor(top))`（XWindowDecoration.c:2083-2097），注释「不能只依赖 xwd_stripHeight：状态对象在首次 ensureState 之前为 NULL，此时回退 defaultHeight（28px），全屏保留边距不归零，内容区顶永久留白一条（真机实证）」。注意注释里的 28px 是历史值，当前 `XTitleBar_defaultHeight` 按样式常量算得 96（见 XTitleBar 坑点 3）。
5. **`syncBarGeometry` 只重钉、绝不造状态或造条**：`if (!st || !st->m_bar) return;`（XWindowDecoration.c:2149-2151），注释「承载语义归 syncWindow」；且头文件 @details 明确「只钉条子控件——对顶层自身改几何会经平台同步→ConfigureNotify 回环递归，严禁」（XWindowDecoration.h:122-123）。
6. **`notifyWindowState` 幂等且要求条已承载**：`if (full == st->m_fullBarSuppressed) return false;` 同值短路（XWindowDecoration.c:2752-2753），`if (!st || !st->m_bar) return false;`（:2755）——建窗早期条还没建时通知会被丢弃，等 `syncWindow→ensureBar` 的承载分支自己按全屏态处理（同段注释）。
7. **`isShaded` 不区分「桌面原生窗」以外的语义**：实现只有 `xwd_stateFor(top)` 查表 + 读 `m_shaded`（XWindowDecoration.c:254-258），未装饰/无状态即 false；头注补「桌面原生窗 (iconify 语义, 不做卷起) 一律返回 false」（XWindowDecoration.c:249-253）——即 `isShaded` 与 Qt 的 `isMinimized` 不等价。

**测试覆盖**：无 `Test/XGuiTest/XWindowDecorationTest.c` 专属套件（`ls` → MISSING）。间接覆盖三套：`Test/XGuiTest/XGuiDialogMoveTest.c`（`XWindowDecoration_handlePointer` :269、`t_expect(XWindowDecoration_gestureActive(), …)` :419/:487、`XWindowDecoration_dragSnapshotStepCount` :471/:484/:530，全文件 `t_expect(` 15 处）；`Test/XGuiTest/XGuiMirrorFidelityTest.c`（`handlePointer` :811/:832/:867、`mf_expect(XWindowDecoration_gestureActive(), "R2 drag armed")` :813，全文件 `mf_expect(` 29 处）；`Test/XGuiTest/XGuiDockDragTest.c`（文件头注释声明让位条带还原链，`XWindowDecoration_` 仅在 :7 注释出现，`t_expect(` 22 处，无直接 API 调用）。仓库根 `xgui_regression_test.c` 仅 1 处提及（注释级）。

---

## XWizard

**定位**：向导对话框 + 向导页两个控件（`XWizard` 继承 `XDialog`；`XWizardPage` 继承 `XWidget`）。继承链：`XObject ← XWidget ← XDialog ← XWizard`（XWizard.h:299 `EXTEND_END(XWizard, XDialog)`）、`XObject ← XWidget ← XWizardPage`（XWizard.h:105 `XCLASS_DEFINE_END(XWizardPage)`，基类 `XWidget`）。头文件 `Src/XGui/Widget/XWizard.h`（722 行）。守卫 `XWIDGET_ON && XDIALOG_ON && XWIZARD_ON`（XWizard.h:38）；开关 `XWIZARD_ON` 默认 1（XGuiConfig.h:611-612），裁剪块置 0（XGuiConfig.h:926-927）。

**生命周期**：`XWizardPage_init`：`XWidget_init` → 挂虚表 → `m_complete=true` → 建 `m_title`/`m_subTitle`（XWizard.c:134-145）；`VXWizardPage_deinit` 释放 title/subTitle/pixmap 与 6 个按钮文本后交 XWidget（XWizard.c:30-53）。`XWizard_init`：`XDialog_init` → 挂虚表 → `m_pixmap=XString_create()`、`m_buttonLayout/m_titleFormat/m_subTitleFormat=0`、6 个按钮文本 `XString_create()`、`m_pageCount=0`、`m_currentIndex=0`、`m_startIndex=0`、`m_style=ClassicStyle`、`m_options=0`、`resize(480,320)`，再在 `XPUSHBUTTON_ON` 下创建取消/完成/下一页/上一页四个按钮并接线、统一 `xwiz_layoutButtons`（XWizard.c:864-953）；Help 按钮的创建点在第 894 行但门禁读 `m_options`（见坑点 2）。`VX_wizard_deinit` 释放按钮文本/字段表/横幅图/默认属性名键（value 为借用不释放），并**显式断开并摘下借用自定义按钮**（否则基类级联会把借用按钮当子控件删除），再交 `XClass_Deinit_Parent(XDialog,…)`（XWizard.c:716-773）。

**API 面**

枚举：`XWizardStyle`（Classic=0/Modern=1/Mac=2/Aero=3，XWizard.h:41-47，对齐 `QWizard::WizardStyle` qwizard.h:54-60）；`XWizardOption`（16 个选项枚举，取值 0x1..0x10000，XWizard.h:50-68，数值与 `QWizard::WizardOption` qwizard.h:63-81 一致，但**缺 `ExtendedWatermarkPixmap=0x4`**）；`XWizardButton`（Back=0/Next=1/Commit=2/Finish=3/Cancel=4/Help=5/NStandardButtons=6，XWizard.h:71-80，对齐 qwizard.h:29-44 前 6 项）。

`XWizardPage`（XWizard.h:107-128；四虚槽 XWizard.h:100-105）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XWizardPage_class_init(void);` | 建表：Deinit + 四虚槽默认实现 | X 扩展 | 共享表，禁释放 |
| `void XWizardPage_init(XWizardPage* self, XWidget* parent, XWidgetFlags flags);` | 初始化（默认 complete=true） | `QWizardPage(QWidget*)` qwizard.h:183 | — |
| `#define XWizardPage_create(parent, flags) XWizardPage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XWizardPage* XWizardPage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `void* XWizardPage_completeChanged_signal(XWizardPage* self);` | completeChanged() 信号（setComplete 差分真发射） | `completeChanged` qwizard.h:206 | 信号标识 |
| `void XWizardPage_setTitle(XWizardPage* self, const char* utf8);` | 设页标题（变化联动向导横幅重铺/失效） | `setTitle` qwizard.h:186 | UTF-8 借用，内部拷贝 |
| `const char* XWizardPage_title(const XWizardPage* self);` | 读页标题；NULL/空 `""` | `title` qwizard.h:187 | 借用 |
| `void XWizardPage_setSubTitle(XWizardPage* self, const char* utf8);` | 设子标题（有无决定横幅高 32/56） | `setSubTitle` qwizard.h:188 | UTF-8 借用，内部拷贝 |
| `const char* XWizardPage_subTitle(const XWizardPage* self);` | 读子标题 | `subTitle` qwizard.h:189 | 借用 |
| `void XWizardPage_setComplete(XWizardPage* self, bool complete);` | 设完成态（差分发 completeChanged） | `setComplete`（Qt 无此公开 setter；对位 `isComplete` 虚函数 qwizard.h:202） | — |
| `bool XWizardPage_isComplete(const XWizardPage* self);` | 查完成态；NULL false | `isComplete` qwizard.h:202 | — |
| `void XWizardPage_initializePage(XWizardPage* self);` | 分派 `EXWizardPage_InitializePage`；默认空操作 | `initializePage` qwizard.h:199 | — |
| `void XWizardPage_cleanupPage(XWizardPage* self);` | 分派 `CleanupPage`；默认空操作 | `cleanupPage` qwizard.h:200 | — |
| `bool XWizardPage_validatePage(XWizardPage* self);` | 分派 `ValidatePage`；默认 true | `validatePage` qwizard.h:201 | — |
| `int XWizardPage_nextId(const XWizardPage* self);` | 分派 `NextId`；默认无所属向导 -1，否则下一页索引（末页 -1） | `nextId` qwizard.h:203 | — |
| `struct XWizard* XWizardPage_wizard(const XWizardPage* self);` | 所属向导借用指针；未加入 NULL | `wizard()` qwizard.h:213（protected） | 借用，禁释放 |
| `void XWizardPage_setButtonText(XWizardPage* self, XWizardButton which, const char* utf8);` | 页级按钮文本覆盖（NULL 恢复默认）；仅存状态 | `setButtonText` qwizard.h:196 | UTF-8 借用，内部拷贝 |
| `const char* XWizardPage_buttonText(const XWizardPage* self, XWizardButton which);` | 读页级按钮文本；未设 `""` | `buttonText` qwizard.h:197 | 借用 |
| `void XWizardPage_setCommitPage(XWizardPage* self, bool commitPage);` | 标记提交页（仅存状态） | `setCommitPage` qwizard.h:194 | — |
| `bool XWizardPage_isCommitPage(const XWizardPage* self);` | 查提交页 | `isCommitPage` qwizard.h:195 | — |
| `void XWizardPage_setFinalPage(XWizardPage* self, bool finalPage);` | 标记强制末页（仅存状态） | `setFinalPage` qwizard.h:192 | — |
| `bool XWizardPage_isFinalPage(const XWizardPage* self);` | 查强制末页 | `isFinalPage` qwizard.h:193 | — |
| `void XWizardPage_setPixmap(XWizardPage* self, int which, const XString* path);` | 设页面图片路径（`which` 保留，单图承载） | `setPixmap(WizardPixmap,QPixmap)` qwizard.h:190 | `path` 借用，内部拷贝 |
| `void XWizardPage_setPixmap_2(XWizardPage* self, int which, const char* utf8);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用 |
| `const XString* XWizardPage_pixmap(const XWizardPage* self, int which);` | 查页面图片路径 | `pixmap` qwizard.h:191 | 借用，禁释放 |

`XWizard`（XWizard.h:305-353）：

| 签名 | 语义 | Qt 对标 | 所有权 |
|---|---|---|---|
| `XVtable* XWizard_class_init(void);` | 建表：Deinit/Paint/Resize（+`XWIZ_CSD_QUERY_ON` 时 Show） | X 扩展 | 共享表，禁释放 |
| `void XWizard_init(XWizard* self, XWidget* parent, XWidgetFlags flags);` | 初始化（480×320、四导航按钮、ClassicStyle、无选项） | `QWizard(QWidget*,Qt::WindowFlags)` qwizard.h:87 | — |
| `#define XWizard_create(parent, flags) XWizard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))` | 默认内存类型建堆对象 | 同上 | 调用方拥有，须 `XClassDelete` |
| `XWizard* XWizard_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);` | 指定内存类型建堆对象 | X 扩展 | 调用方拥有，须 `XClassDelete` |
| `int XWizard_addPage(XWizard* self, XWizardPage* page);` | 追加页面返回索引；满 16 或 NULL 返回 -1 | `addPage` qwizard.h:90 | 页 reparent 为子控件，借用 |
| `void XWizard_setPage(XWizard* self, int index, XWizardPage* page);` | 占位设页（越界/空忽略；替换旧页时解绑旧页） | `setPage(int,QWizardPage*)` qwizard.h:91 | 页借用 |
| `void XWizard_removePage(XWizard* self, int index);` | 移除页（解绑归属、复位 initialized、发 pageRemoved） | `removePage(int)` qwizard.h:92 | 页不删除 |
| `XWizardPage* XWizard_page(XWizard* self, int index);` | 按索引取页；越界 NULL | `page(int)` qwizard.h:93 | 借用 |
| `int XWizard_pageCount(const XWizard* self);` | 已加页数 | `pageIds().size()` qwizard.h:96（Qt 无同名） | — |
| `XWizardPage* XWizard_currentPage(const XWizard* self);` | 当前页；无 -1 时 NULL | `currentPage` qwizard.h:99 | 借用 |
| `int XWizard_currentIndex(const XWizard* self);` | 当前页索引；NULL -1 | `currentId` qwizard.h:100 | — |
| `void XWizard_setCurrentIndex(XWizard* self, int index);` | 切页并发 currentIdChanged；越界/同页忽略 | `setCurrentId` qwizard.h:148（槽） | — |
| `#define XWizard_currentId(self) XWizard_currentIndex((self))` | currentId 属性读别名 | `currentId` qwizard.h:100 | — |
| `#define XWizard_setCurrentId(self, id) XWizard_setCurrentIndex((self), (id))` | currentId 属性写别名 | `setCurrentId` qwizard.h:148 | — |
| `#define XWizard_startId(self) XWizard_startIndex((self))` | startId 属性读别名 | `startId` qwizard.h:98 | — |
| `#define XWizard_setStartId(self, id) XWizard_setStartIndex((self), (id))` | startId 属性写别名 | `setStartId` qwizard.h:97 | — |
| `void XWizard_next(XWizard* self);` | 先 `validateCurrentPage()` 再按 `nextId()` 前进 | `next` qwizard.h:147 | — |
| `void XWizard_back(XWizard* self);` | 回上一页（`currentIndex-1`） | `back` qwizard.h:146 | — |
| `void XWizard_restart(XWizard* self);` | 复位全部页 initialized/visited 再进起始页 | `restart` qwizard.h:149 | — |
| `void XWizard_setStartIndex(XWizard* self, int index);` | 设起始页（越界忽略） | `setStartId` qwizard.h:97 | — |
| `int XWizard_startIndex(const XWizard* self);` | 查起始页；NULL 0 | `startId` qwizard.h:98 | — |
| `bool XWizard_hasVisitedPage(const XWizard* self, int index);` | 查访问史 | `hasVisitedPage` qwizard.h:94 | — |
| `int XWizard_visitedIds(const XWizard* self, int* outIds, int maxCount);` | 汇总已访问页 id（升序；NULL/≤0 为计数模式） | `visitedIds` qwizard.h:95 | 输出缓冲调用方所有 |
| `int XWizard_pageIds(const XWizard* self, int* outIds, int maxCount);` | 列出全部页 id（升序；计数模式同上） | `pageIds` qwizard.h:96 | 输出缓冲调用方所有 |
| `#define XWizard_visitedPages(self, outIds, maxCount) XWizard_visitedIds((self), (outIds), (maxCount))` | Qt5 旧名兼容别名 | Qt 6.8.3 已移除 | — |
| `void XWizard_setWizardStyle(XWizard* self, XWizardStyle style);` | 设样式（仅存状态，绘制不消费） | `setWizardStyle` qwizard.h:108 | — |
| `XWizardStyle XWizard_wizardStyle(const XWizard* self);` | 查样式；NULL Classic | `wizardStyle` qwizard.h:109 | 值 |
| `void XWizard_setOption(XWizard* self, XWizardOption option, bool on);` | 开/关单个选项 | `setOption(option,bool=true)` qwizard.h:111 | — |
| `bool XWizard_testOption(const XWizard* self, XWizardOption option);` | 查选项位 | `testOption` qwizard.h:112 | — |
| `void XWizard_setOptions(XWizard* self, int options);` | 整体设选项位 | `setOptions` qwizard.h:113 | — |
| `int XWizard_options(const XWizard* self);` | 查选项位 | `options` qwizard.h:114 | — |
| `void XWizard_setButtonText(XWizard* self, XWizardButton which, const char* utf8);` | 设按钮文本并刷新按钮 | `setButtonText` qwizard.h:116 | UTF-8 借用，内部拷贝 |
| `const char* XWizard_buttonText(const XWizard* self, XWizardButton which);` | 读按钮文本；未设 `""` | `buttonText` qwizard.h:117 | 借用 |
| `struct XAbstractButton* XWizard_button(const XWizard* self, int which);` | 查按钮（登记优先→内建；Commit 恒 NULL，XWizard.c:1253-1254） | `button(WizardButton)` qwizard.h:120 | 借用，禁释放 |
| `void XWizard_setButton(XWizard* self, int which, struct XAbstractButton* btn);` | 登记自定义按钮（借用；NULL 清除登记） | `setButton` qwizard.h:119 | 借用，向导不删不改父（登记时 reparent+show） |
| `void XWizard_setDefaultProperty(XWizard* self, const char* name, void* value);` | 登记默认属性（name 深拷贝，value 不透明借用；同名覆盖；满 16 忽略） | `setDefaultProperty(className,property,changedSignal)` qwizard.h:132-133 | `value` 借用，不深拷贝不释放 |
| `void* XWizard_currentIdChanged_signal(XWizard* self, int index);` | currentIdChanged(index) 信号（切页真发射） | `currentIdChanged` qwizard.h:139 | 信号标识 |
| `void* XWizard_pageAdded_signal(XWizard* self, int index);` | pageAdded(index) 信号（addPage 真发射） | `pageAdded` qwizard.h:142 | 信号标识 |
| `void* XWizard_pageRemoved_signal(XWizard* self, int index);` | pageRemoved(index) 信号（removePage 真发射） | `pageRemoved` qwizard.h:143 | 信号标识 |
| `void* XWizard_helpRequested_signal(XWizard* self);` | helpRequested() 信号（Help 槽真发射 + 可手动调用） | `helpRequested` qwizard.h:140 | 信号标识 |
| `void XWizard_setPixmap(XWizard* self, int which, const XString* path);` | 设横幅图路径（`which` 保留，单图承载） | `setPixmap(WizardPixmap,QPixmap)` qwizard.h:126 | `path` 借用，内部拷贝 |
| `void XWizard_setPixmap_2(XWizard* self, int which, const char* path);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用 |
| `const XString* XWizard_pixmap(const XWizard* self, int which);` | 查横幅图路径 | `pixmap` qwizard.h:127 | 借用，禁释放 |
| `const XString* XWizard_field(const XWizard* self, const XString* name);` | 读字段值；未设 NULL | `field(const QString&)` qwizard.h:106 | 借用 |
| `const char* XWizard_field_2(const XWizard* self, const char* name);` | 同上（UTF-8 重载） | 同上 | 借用 |
| `void XWizard_setField(XWizard* self, const XString* name, const XString* value);` | 设字段值（NULL 清空；表满 32 条忽略新增） | `setField` qwizard.h:105 | `name`/`value` 借用，内部拷贝 |
| `void XWizard_setField_2(XWizard* self, const char* name, const char* value);` | 同上（UTF-8 重载） | 同上 | UTF-8 借用 |
| `void XWizard_setSideWidget(XWizard* self, XWidget* widget);` | 设侧边控件（仅存借用指针，不入布局） | `setSideWidget` qwizard.h:129 | 借用，不 reparent 不删除 |
| `XWidget* XWizard_sideWidget(const XWizard* self);` | 查侧边控件 | `sideWidget` qwizard.h:130 | 借用 |
| `void XWizard_setButtonLayout(XWizard* self, int layout);` | 设按钮布局码（**仅存状态，布局不读**） | `setButtonLayout(QList<WizardButton>)` qwizard.h:118 | — |
| `void XWizard_setTitleFormat(XWizard* self, int format);` | 设标题格式码（仅存状态） | `setTitleFormat(Qt::TextFormat)` qwizard.h:122 | — |
| `int XWizard_titleFormat(const XWizard* self);` | 查标题格式；NULL 0 | `titleFormat` qwizard.h:123 | — |
| `void XWizard_setSubTitleFormat(XWizard* self, int format);` | 设子标题格式码（仅存状态） | `setSubTitleFormat` qwizard.h:124 | — |
| `int XWizard_subTitleFormat(const XWizard* self);` | 查子标题格式；NULL 0 | `subTitleFormat` qwizard.h:125 | — |
| `void XWizard_cleanupPage(XWizard* self);` | 对当前页分派 cleanupPage | `cleanupPage(int)` qwizard.h:160（protected） | — |
| `void XWizard_initializePage(XWizard* self);` | 对当前页分派 initializePage | `initializePage(int)` qwizard.h:159（protected） | — |
| `bool XWizard_validateCurrentPage(const XWizard* self);` | 先判 `isComplete()` 再分派 validatePage；无当前页 true | `validateCurrentPage` qwizard.h:102 | — |
| `int XWizard_nextId(const XWizard* self);` | 分派当前页 nextId；无页 -1 | `nextId` qwizard.h:103 | — |
| `void XWizard_done(XWizard* self, int result);` | Rejected 先 restart 再收口；Accepted 先过 validateCurrentPage | `done(int)` qwizard.h:158（Qt protected；X 公开） | — |
| `void* XWizard_customButtonClicked_signal(XWizard* self, int which);` | customButtonClicked(which) 信号（setButton 登记的按钮点击转发） | `customButtonClicked` qwizard.h:141 | 信号标识 |

**所有权与命名契约**：`_const` 借用族 = `pageCount`/`currentPage`/`currentIndex`/`startIndex`/`hasVisitedPage`/`visitedIds`/`pageIds`/`wizardStyle`/`testOption`/`options`/`buttonText`/`button`/`titleFormat`/`subTitleFormat`/`pixmap`/`field`/`field_2`/`sideWidget`/`nextId`/`validateCurrentPage`（页侧 `title`/`subTitle`/`isComplete`/`validatePage`/`wizard`/`buttonText`/`isCommitPage`/`isFinalPage`/`pixmap`）。`_2` 后缀 5 处全为 **XString → UTF-8**（`XWizardPage_setPixmap_2`、`XWizard_setPixmap_2`、`XWizard_field_2`、`XWizard_setField_2`），**未用 `_utf8`**；另有一处「参数形态」`_2` 不存在于本类（`addPage` 无 `_2`）。三态 `set*`：`XWizardPage_setButtonText`（NULL 恢复默认）、`XWizardPage_setPixmap`（NULL 清除）、`XWizard_setPixmap`/`setField`（NULL 清空）、`XWizard_setButton`（NULL 清除登记）、`XWizard_setSideWidget`（NULL 清除）；不做 NULL 判别的单值 setter：`setComplete`/`setCommitPage`/`setFinalPage`/`setOption`/`setOptions`/`setWizardStyle`/`setButtonLayout`/`setTitleFormat`/`setSubTitleFormat`/`setStartIndex`/`setCurrentIndex`。页面为借用（加入向导只 reparent，不接管生命周期）；自定义按钮为借用且析构时显式摘链（XWizard.c:755-771）。

**可覆写点**：两个虚表。

`XWizardPage`：`XCLASS_DEFINE_BEGING(XWizardPage)` + 四槽 `InitializePage`/`CleanupPage`/`ValidatePage`/`NextId`（自 `XCLASS_VTABLE_GET_SIZE(XWidget)` 起）+ `XCLASS_DEFINE_END`（XWizard.h:100-105）。

- `EXWizardPage_InitializePage` → 默认 `VXWizardPage_initializePage` 空操作（XWizard.c:64）；由 `xwiz_switchTo` 在页**首次**进入时调用一次（XWizard.c:502-506）。
- `EXWizardPage_CleanupPage` → 默认 `VXWizardPage_cleanupPage` 空操作（XWizard.c:76）；仅 Backward 且未开 `IndependentPages` 时对旧页调用（XWizard.c:494-498）。
- `EXWizardPage_ValidatePage` → 默认 `VXWizardPage_validatePage` 返回 true（XWizard.c:88-94，对齐 Qt 6.8.3 `qwizard.cpp:3573-3576`）；`next()` 与 `done(Accepted)` 经 `XWizard_validateCurrentPage` 间接调用（XWizard.c:173-178、1521-1532）。
- `EXWizardPage_NextId` → 默认 `VXWizardPage_nextId`：无所属向导 -1，否则页数组中的下一页索引、末页 -1（XWizard.c:104-116）。
- `EXClass_Deinit` → `VXWizardPage_deinit`（XWizard.c:30-53）。

`XWizard`：`XCLASS_DEFINE_BEGING(XWizard)` + `XCLASS_DEFINE_EXTEND_END(XWizard, XDialog)`（XWizard.h:298-299）——零新槽位。

- `EXClass_Deinit` → `VX_wizard_deinit`（XWizard.c:716-773）。
- `EXWidget_PaintEvent` → `VX_wizard_paintEvent`（XWizard.c:637-714）：背景/横幅/分隔线全部限幅在事件脏区内（:677-680）。
- `EXWidget_ResizeEvent` → `VX_wizard_resizeEvent`：重排按钮行与当前页（XWizard.c:822-830）。
- `EXWidget_ShowEvent`（仅 `XWIZ_CSD_QUERY_ON=1`，XWizard.c:17-19 按窗口开关定义）→ `VX_wizard_showEvent`：按真值装饰条高重排（XWizard.c:839-848）。

**注意事项与坑点**

1. **`XWizard_init` 的 `m_currentIndex` 初值是 0，与结构体注释「-1=无页面」不一致**：结构体注释写「当前页索引（-1=无页面）」（XWizard.h:348），header 内存治理注释还引「XWizard.c:1112 无页面复位」（XWizard.h:345），但 init 置 `self->m_currentIndex = 0;`（XWizard.c:883）。空向导时 `XWizard_currentIndex` 因此返回 **0**（XWizard.c:1066-1069），而 `currentPage` 因 `m_pageCount==0` 返回 NULL（XWizard.c:1059-1064）——两者对「无页面」的表态不一致；注释引用的 `XWizard.c:1112` 实际是 `restart()` 里重置 `m_initialized` 的行，`m_currentIndex = -1` 在 `:1114`。
2. **`HaveHelpButton` 永不生效（Help 按钮创建点被空选项挡死）**：`self->m_options = 0;` 在 :886，而 Help 按钮的创建分支 `if (self->m_options & (int)XWizardOption_HaveHelpButton)` 在 **:894**——同一函数内先归零再判，条件恒假。全文件 `HaveHelpButton` 只出现于 :892（注释）与 :894（该判断），`XWizard_setOption`（XWizard.c:1197-1202）也不会回头补建。`XWizard_button` 的 Help 分支注释「未开 Help 选项为 NULL」（XWizard.c:1260）与此互为印证，但头文件 XWizard.h:544-545 却承诺「HaveHelpButton 选项开启时创建的 Help」。
3. **`XWizard_setButtonLayout` 只存不读**：`m_buttonLayout` 全文件仅三处（init 置 0 :873、setter 赋值 :1495、头文件声明 XWizard.h:335），`xwiz_layoutButtons` 硬编码 Help 最左、Cancel 最右、Finish/Next/Back/Commit 依次向左（XWizard.c:795-816），完全不读 `m_buttonLayout`。头文件注释已自认「setter 往返任意值，保留 int」（XWizard.h:335-336）。
4. **`XWizard_setSideWidget` 只存指针**：实现一行 `if (self) self->m_sideWidget = widget;`（XWizard.c:1489-1490），不 reparent、不入布局、不 show；Qt 的 `setSideWidget` 会挂进布局（qwizard.h:129-130）。`m_sideWidget` 为借用（XWizard.h:325）。
5. **`XWizardPage_isComplete` 与 Qt 的默认实现语义不同**：Qt 6.8.3 的 `QWizardPage::isComplete()` 是**虚函数**，无向导返回 true、有向导则遍历 mandatory 字段比对初值（qwizard.cpp:3595-3625）；本类把它做成**非虚的存储位**（`m_complete` 默认 true，XWizard.h:121、XWizard.c:134-145 置位、:340-342 读取），不随字段自动求值。`XWizard_validateCurrentPage` 先查该位再分派 `validatePage`（XWizard.c:1521-1532）——头文件 XWizard.h:694-698 只提「分派 validatePage 虚槽」，未提这道 isComplete 前置门；Qt 的 `validateCurrentPage` 只有 `page->validatePage()`（qwizard.cpp:3287-3293）。
6. **`addPage` 首页立即显示并发 initializePage，与 Qt 时机不同**：`if (idx == 0)` 分支直接 `setVisible(true)`、`m_visited[0]=true`、`m_currentIndex=0`、`xwiz_layoutCurrentPage`，并在 `!page->m_initialized` 时调 `XWizardPage_initializePage`（XWizard.c:984-1004；`:1005-1006` 为按钮刷新与 pageAdded 发射）。Qt 是 `showEvent → restart()` 才进起始页；本库在 `addPage` 即生效（apitest 断言「首页挂载即进入：currentId==0 且已访问（本库 addPage 首页立即显示并标记访问）」，`xgui_demo_apitest_dialogs.c:2180-2183`）。
7. **`removePage` 不调用 cleanupPage，也不复位可见性/几何**：实现只做数组前移、解绑 `m_wizard`/`m_initialized`、夹紧 `m_currentIndex`、发 pageRemoved（XWizard.c:1026-1046）；Qt 在移除已初始化页时先 `cleanupPage(id)` 并 `removeWidget`（qwizard.cpp:2268-2280）。移除**当前**页后本库不切到任何新页（只夹紧索引，不 `xwiz_switchTo`），当前页控件可能仍可见。
8. **`setPage` 不设可见性、不接 completeChanged**：实现只解绑旧页、写数组、扩 `m_pageCount`、reparent（XWizard.c:1010-1024），**没有** `addPage` 里的 `XObject_connect_1(...completeChanged_signal...)` 与显隐处理（对比 XWizard.c:980-983 的连接、:984-1004 的显隐/初始化）。用 `setPage` 填页的向导，页完备性变化不会刷新按钮使能。
9. **`XWizard_next` / `back` 的越界与末页处理很窄**：`next()` 先过 `validateCurrentPage`（含 isComplete 门），再取 `nextId`，目标 `<0 || >=pageCount` 或等于当前页即直接 return（XWizard.c:1071-1084）；`back()` 只 `currentIndex-1`，`<=0` 即返回（XWizard.c:1086-1091）。`IndependentPages` 之外，头文件 @note 里 `NoBackButtonOnStartPage` 等选项都不参与导航判定（`grep -o "XWizardOption_[A-Za-z0-9]*" Src/XGui/Widget/XWizard.c` 只命中 `HaveHelpButton` 与 `IndependentPages` 两个枚举名）。
10. **`helpRequested_signal` 的文档块与声明被分隔**：XWizard.h:600-608 是 `helpRequested()` 的完整 `@brief/@details/@param/@return`，随后 :609 插入 `/* ==== Task 2.6：字段/侧边/布局/格式/导航 ==== */` 分隔注释，声明 `void* XWizard_helpRequested_signal(XWizard* self);` 在 **:713**（另一段 `customButtonClicked` 文档之后）。按文档工具读到的归属会错位。
11. **`customButtonClicked` 的实际发射点不在信号函数体内**：`XWizard_customButtonClicked_signal` 只有 `(void)self; (void)which; return (void*)(size_t)…;`（XWizard.c:1386-1390）——纯句柄，不发；真发射在 `xwiz_customButtonClickedSlot`（`XObject_sender` 反查槽位，XWizard.c:548-565，发射在 :558-560）。同族的 `currentIdChanged`/`pageAdded`/`pageRemoved` 三个信号函数同样只返地址（XWizard.c:1331-1336），实发点分别在 `xwiz_switchTo`、`addPage`、`removePage` 内的 `xwiz_emitInt`。只有 `helpRequested` 的函数体真发（XWizard.c:1338-1344）。
12. **`XWizardOption` 比 Qt 少一位**：`ExtendedWatermarkPixmap = 0x00000004`（qwizard.h:66）在 XWizard.h:50-68 的枚举中缺失，值 0x4 成为空档；`setOptions` 原样存 int（XWizard.c:1209-1213），传入 0x4 不会报错但无任何行为。

**测试覆盖**：无 `Test/XGuiTest/XWizardTest.c` 专属套件（`ls` → MISSING）。覆盖在 `Test/XGuiDemo/xgui_demo_apitest_dialogs.c:2015-2380`（72 条 `XAPI_EXPECT`：页属性默认值/四虚槽默认/commit/final/pixmap/按钮文本/导航 next-back-restart/访问史/字段/按钮/setButton/自定义按钮/currentIdChanged/pageAdded/pageRemoved/done 语义）；仓库根 `xgui_regression_test.c:35896-35985` `test_wizard_contract()`（30 条 `wiz_expect`），另有 `:28568-28579` helpRequested 探针（4 条 `ws_expect`）与 `:33472` 起导航别名/页面属性段。`XWizard` 在 `Test/` 共引用 16 处（`rg -c` 求和）。
