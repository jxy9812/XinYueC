# Window 模块 XGui ↔ Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计范围：`Src/XGui/Window/`（XWindow、XScreen、XWindowSystemInterface、XWindowEvent 事件类合集；`XWindow_Protected.h` 仅参考）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/qwindow.h / qscreen.h / qwindowsysteminterface.h / qevent.h / qsurfaceformat.h`，行为对照 `qwindow.cpp / qplatformwindow.cpp / qscreen.cpp / qplatformscreen.cpp`
- 审计方式：只读；未修改 `Src/`、`Test/` 任何源码；未 commit / push
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`（§10.287 窗口默认格式、2026-09-04 保护头文件分离等章节）

## 一、模块概览

| X 类 | Qt 对标 | X 头文件 | X 继承链 | Qt 继承链 | 继承一比一 | API 覆盖 | 功能完整性 | 综合 |
|---|---|---|---|---|---|---|---|---|
| XWindow | QWindow(+QSurface) | XWindow.h | XWindow→XObject | QWindow→QObject + QSurface(接口) | ✗（缺 QSurface 层） | 高（缺 2 个 vulkan API） | 中（多处占位/偏差） | ~78% |
| XScreen | QScreen | XScreen.h | XScreen→XObject | QScreen→QObject | ✓ | 全 | 高 | ~95% |
| XWindowSystemInterface | QWindowSystemInterface | XWindowSystemInterface.h | 静态工具类 | 静态工具类 | ✓ | 低（约 11/47 入口） | 中（关键入口签名简化） | ~35% |
| XInputMethodEvent | QInputMethodEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 中 | 中 | ~70% |
| XDropEvent | QDropEvent 家族 | XWindowEvent.h | →XEvent | QDropEvent→QEvent | ✓（家族未拆分） | 低 | 低 | ~45% |
| XResizeEvent | QResizeEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XExposeEvent | QExposeEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XPaintEvent | QPaintEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XCloseEvent | QCloseEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XShowEvent | QShowEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XHideEvent | QHideEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XFocusEvent | QFocusEvent | XWindowEvent.h | →XEvent | →QEvent | ✓ | 全 | 高 | ~95% |
| XWheelEvent | QWheelEvent | XWindowEvent.h | →XEvent | QWheelEvent→QSinglePointEvent→QPointerEvent→QInputEvent→QEvent | ✗（缺 3 层中间基类） | 中 | 中 | ~60% |
| XEnterEvent | QEnterEvent | XWindowEvent.h | →XEvent | QEnterEvent→QSinglePointEvent→…→QEvent | ✗（缺中间基类） | 中 | 中 | ~85% |
| XContextMenuEvent | QContextMenuEvent | XWindowEvent.h | →XEvent | QContextMenuEvent→QInputEvent→QEvent | ✗（缺 XInputEvent） | 中 | 中 | ~85% |

## 二、硬约束合规核查（Window 模块内逐条）

| # | 硬约束 | 结论 | 说明 |
|---|---|---|---|
| 1 | 拥有型字符串一律 XString*，禁止 char[N]/char* 长期持有 | ✅ 合规 | 标题/文件路径（XWindow.c:70-71）、屏幕名称/厂商/型号/序列号（XScreen.c:37-40）、IME 文本、拖放 MIME/数据均为 XString*；`_2` 后缀仅作临时 UTF-8 转发（如 XWindow.c:1090-1096）。 |
| 2 | 继承一比一含中间基类层 | ❌ 违规（部分） | XWindow 缺 QSurface 接口层（合并进 XWindow）；XWheelEvent/XEnterEvent 缺 QInputEvent→QPointerEvent→QSinglePointEvent 三层；XContextMenuEvent 缺 XInputEvent；QDropEvent 家族（DragEnter/DragMove/DragLeave）未拆分。 |
| 3 | 样式/绘制不得精简近似 | ❌ 违规（行为近似） | 本模块无绘制代码，但行为占位/简化较多：`XWindow_show` 恒 showNormal（XWindow.c:1845）、frameMargins 恒 0（XWindow.c:1363-1367）、startSystemResize/startSystemMove 只校验不调用平台（XWindow.c:1937-1954）、requestUpdate 不投递 UpdateRequest（XWindow.c:1962-1966）、XWheelEvent 不承载 pixelDelta/phase/inverted、XDropEvent 无动作集。 |
| 4 | 公共头中文 Doxygen + UTF-8 BOM | ❌ 违规（BOM） | 4 个公共头与全部 .c 均有 BOM（efbbbf）；`XWindow_Protected.h` 无 BOM（违反“所有 .c/.h 必须 UTF-8 BOM”）。XWindowEvent.h 部分单行访问器缺 `@param/@return`（P2 级）。 |
| 5 | 信号：空参 args=NULL；Qt 6.8 每信号有 *_signal 宏 | ✅ 合规（含 1 功能缺口） | XWindow 19 个信号、XScreen 9 个信号齐备；空参 `activeChanged` 用 `XVarList` NULL（XWindow.c:2104）。但 `activeChanged` 从未被内部代码发射（见功能缺口 F7）。 |
| 6 | 生命周期：init/deinit 成对；copy/move 安全；禁 memcpy；禁裸 malloc/free/strdup | ❌ 违规（memcpy） | init/deinit 成对 ✓；VXWindow_copy/move 事务式安全 ✓；未发现裸 malloc/free/strdup ✓；但 XWindowEvent.c 7 处克隆用 `XMemcpy` 整体复制对象（第 30/49/70/83/94/105/116 行），违反“禁止使用 memcpy 复制对象”（Expose/Paint 虽随后重建 XRegion 深拷贝，字面上仍违规）。 |
| 7 | 旧 API 不保留，与 Qt 冲突者改名 | ✅ 合规 | 未发现新旧双轨；`QWindow::create` 因与 `XWindow_create()` 对象构造宏冲突改名 `XWindow_createHandle`（XWindow.h:406-411，已注明）。 |
| 8 | 新代码 C99，无 C++/C11 语法 | ✅ 合规 | 全部 C99（混合声明、复合字面量合法），无 C++/C11 特性。 |

## 三、逐类对比

### 3.1 XWindow ↔ QWindow（+QSurface / QSurfaceFormat）

**继承链**
- X：`XWindow→XObject`（结构体首成员 `XObject m_class`，XWindow.h:275-279；虚表 `XCLASS_DEFINE_BEGING(XWindow)` 从 `XCLASS_VTABLE_GET_SIZE(XObject)` 追加 26 个事件槽，XWindow.h:110-137）
- Qt：`QWindow : public QObject, public QSurface`（qwindow.h:62）
- 判定：**不一致**。QSurface 是抽象接口层（surfaceType/surfaceClass/supportsOpenGL/format/size/surfaceHandle），XGui 未建 `XSurface` 类，而是把该 5 个查询合并进 XWindow（XWindow.h:346-375）。功能等价但继承链非一比一（同 XAbstractAxis 类问题）。

**API 缺口（Qt 公开 API 中 X 缺失）**

| Qt 原型（qwindow.h） | X 状态 | 说明 |
|---|---|---|
| `void setVulkanInstance(QVulkanInstance *instance); QVulkanInstance *vulkanInstance() const;` | ❌ 缺失 | 仅 `QT_CONFIG(vulkan)` 下的 2 个 API；XWindow.h 头注“实现全部公开 API”与事实不符（XWindow.h:3） |

**命名/签名差异（映射合规性）**

| Qt | X | 判定 |
|---|---|---|
| `QString title() / setTitle(const QString&)` | `XString* XWindow_title`（深拷贝）/ `XWindow_setTitle(XString*)` + `_2(const char*)` | ✅ 合规（XString 主版本 + UTF-8 `_2` 重载） |
| `void create()` | `XWindow_createHandle()` | ✅ 改名规避 `XWindow_create()` 宏冲突（XWindow.h:406-411） |
| `QWindow *parent(AncestorMode = ExcludeTransients)` | `XWindow_parent(self, mode)`（无默认参） | ✅ C 语言合理（需显式传参） |
| `bool isAncestorOf(child, mode = IncludeTransients)` | `XWindow_isAncestorOf(self, child, mode)`（无默认参） | ✅ C 语言合理 |
| `qreal devicePixelRatio/opacity` | `float` | ✅ 精度映射（qreal=double→float，嵌入式取舍） |
| `QPointF mapToGlobal/mapFromGlobal` | `XWindow_mapToGlobal_f/_f` + 整型版本 | ✅ 双精度变体齐全（XWindow.h:1010-1044） |
| `setFlag(Qt::WindowType, bool on=true)` | `XWindow_setFlag(self, flag, bool on)`（无默认参） | ✅ C 语言合理 |
| `Qt::WindowType/WindowFlags/WindowState(s)/Modality/Visibility/AncestorMode/Edges` 枚举 | XWindowType/XWindowFlags/XWindowState(s)/XWindowModality/XWindowVisibility/XWindowAncestorMode/XWindowEdges | ✅ 数值逐一核对与 Qt 一致（XWindow.h:142-255） |
| `QSurfaceFormat setFormat/format/requestedFormat` | `XSurfaceFormat`（值类型，Src/XGui/Style/XSurfaceFormat.h） | ✅ 值语义；QSurfaceFormat 全部 API 均有对应 |

**功能缺口（对照 qwindow.cpp 行为）**

| # | 缺口 | 位置 | 说明 |
|---|---|---|---|
| F1 | `isTopLevel()` 语义不符 | XWindow.c:968-973 | Qt：`isTopLevel() = (parentWindow == nullptr)`，**瞬态父不影响顶层**（qwindow.cpp:843-846）。X 额外要求 `m_transientParent == NULL`，导致：带瞬态父的窗口 `close()` 直接返回 false（XWindow.c:1889）；`setTransientParent` 校验过严（父带瞬态父时被拒）。 |
| F2 | `close()` 行为近似 | XWindow.c:1881-1905 | Qt：无平台窗口返回 true；有平台窗口时 `QPlatformWindow::close()` → `QWindowSystemInterface::handleCloseEvent`（qwindow.cpp:2369-2390）。X 自行派发事件并**无条件 hide+destroy**，且返回恒 true（拒绝关闭时也返回 true）——Qt 返回平台 close() 结果。 |
| F3 | `setParent()` 缺事件链/屏幕重连 | XWindow.c:932-957 | Qt 发送 ParentWindowAboutToChange / ChildWindowRemoved / ChildWindowAdded / ParentWindowChange 事件并处理屏幕连接切换（qwindow.cpp:785-840）；X 只同步 XObject 父指针并重应用可见性。 |
| F4 | `show()` 简化 | XWindow.c:1842-1847 | Qt：子窗口→showNormal，顶层按平台默认状态（FullScreen/Maximized/Normal）（qwindow.cpp:2269-2288）；X 恒 showNormal（注释自认“简化”）。 |
| F5 | frameMargins/frameGeometry 恒 0/恒等于 geometry | XWindow.c:1363-1370 | Qt 由平台窗口返回；X 未接 XPlatformNativeWindow 边框信息。 |
| F6 | startSystemResize/startSystemMove 占位 | XWindow.c:1937-1954 | Qt 校验后**调用平台后端**（qwindow.cpp:1170-1222）；X 校验（visible/platform/边组合/min≠max）后直接返回 true/false，不调用任何后端，是“伪成功”。 |
| F7 | `activeChanged` 信号从未发射 | XWindow.c:1786-1800, 1907-1919 | requestActivate/raise/lower 直接改 `m_active` 不发射信号；全仓无任何调用者。 |
| F8 | `requestUpdate()` 不投递事件 | XWindow.c:1962-1966 | 仅置 `m_updateRequested` 标志；XEventType 已有 `XEVENT_TYPE_UPDATE_REQUEST=77`（XEventType.h:70）但未使用。 |
| F9 | `alert()` 仅记录 | XWindow.c:1956-1960 | Qt 有 `_q_clearAlert` 定时器/平台提示；X 只存 `m_alertMsec`。 |
| F10 | `winId()` 虚拟占位 | XWindow.c:850-898 | Qt 平台创建失败返回 0；X 无平台后端时分配自增虚拟 WId（已注明嵌入式设计，但与 Qt 语义有差异）。 |
| F11 | `setOpacity()` 裁剪越界值 | XWindow.c:1100-1109 | Qt 不裁剪，`level>=1 视为不透明、<=0 视为全透明`由平台解释（qwindow.cpp:1231-1241）；X 先夹到 [0,1] 再存，信号值与 Qt 不同。 |
| F12 | `VXWindow_event` 多数事件不置 accepted | XWindow.c:2144-2248 | XEvent 基类默认 `accepted=false`（XEvent.c:67 memset 清零），而 Qt QEvent 默认 accepted=true；分发器只对 expose/focusIn/focusOut/enter/leave 显式 accept，resize/paint/show/hide/key/mouse/wheel/drag 派发后 `XEvent_isAccepted` 仍为 false，与 Qt 相反（XCloseEvent 已在 init 中单独补偿，XWindowEvent.c:388）。 |

**违规项**
1. XWindow.h:3 头注“实现全部公开 API”不实（缺 vulkan 2 API）。
2. `XWindow_Protected.h` 无 UTF-8 BOM（编码硬约束）。
3. 继承链缺 QSurface 中间层（约束 2）。
4. F1/F6/F12 属行为偏差（约束 3 近似）。

### 3.2 XScreen ↔ QScreen

**继承链**：X `XScreen→XObject`（XScreen.h:35-36） vs Qt `QScreen→QObject`（qscreen.h:31）→ **一致 ✓**。

**API 缺口**：无。Qt QScreen 全部公开 API 均有对应：
handle/name/manufacturer/model/serialNumber/depth/size/geometry/physicalSize/physicalDotsPerInchX|Y|avg/logicalDotsPerInchX|Y|avg/devicePixelRatio/availableSize/availableGeometry/virtualSiblings/virtualSiblingAt/virtualSize/virtualGeometry/availableVirtualSize/availableVirtualGeometry/primaryOrientation/orientation/nativeOrientation/angleBetween/transformBetween/mapBetween/isPortrait/isLandscape/grabWindow/refreshRate + 9 个信号（qscreen.h:122-131 ↔ XScreen.h:647-664）。

**差异（合规）**
- Qt QScreen 属性全部只读（由 QPA 填充）；X 提供程序化 setter 与屏幕注册表（XScreen_register/screens/primaryScreen，对标 QGuiApplication::screens 语义）——属扩展，不冲突。
- `grabWindow` Qt 有默认参数 `(WId=0,0,0,-1,-1)`；X 需显式传参（C 语言合理）。
- `angleBetween/transformBetween/mapBetween` 算法与 QPlatformScreen 一致（XScreen.c:841-912 vs qplatformscreen.cpp:365-431，含 Primary 解析、log2 差值查表、translate+rotate 矩阵、矩形换轴）；`XImageTransform` 承载 3×3 仿射矩阵，透视项恒 0/0/1。

**功能缺口**：1 项——无平台后端时 `grabWindow` 返回空 XPixmap（XScreen.c:936-947），与 Qt 无显示服务器时空 QPixmap 退化一致，非问题；真实抓屏依赖 `XPLATFORMNATIVEWINDOW_ON`。虚拟几何/可用几何/DPI 联动与信号发射顺序（geometry→availableGeometry→virtual→physicalDpi→primaryOrientation）已按 Qt 语义实现（XScreen.c:524-547）。

**违规**：无。

### 3.3 XWindowSystemInterface ↔ QWindowSystemInterface

**继承链**：均为静态工具类 → **一致 ✓**（XWindowSystemInterface.h:36-40）。

**API 缺口（Qt qwindowsysteminterface.h 公开入口 vs X）**

X 已实现（11 个）：handleGeometryChange / handleExposeEvent / handlePaintEvent / handleCloseEvent / handleFocusWindowChanged / handleEnterEvent / handleLeaveEvent / handleKeyEvent / handleWheelEvent / handleMouseEvent / flushWindowSystemEvents（另加 2 个项目扩展 handleShowEvent/handleHideEvent、handleInputMethodEvent、handleDropEvent）。

X 缺失（约 36 个入口，分组）：

| Qt 原型（组） | 建议 |
|---|---|
| `handleShortcutEvent`、`handleExtendedKeyEvent`（3 重载） | 键盘扩展，待 XKeyEvent 补 native 字段后实现 |
| `handleTouchEvent`、`handleTouchCancelEvent`（各 2 重载） | 需先建 XTouchEvent 与触点模型 |
| `handleEnterLeaveEvent(enter, leave, …)` | 简单，可立即实现 |
| `handleWindowStateChanged` / `handleWindowScreenChanged` / `handleWindowDevicePixelRatioChanged` / `handleSafeAreaMarginsChanged` | 窗口状态/屏幕/DPR 注入，建议实现 |
| `handleApplicationStateChanged` / `handleApplicationTermination` | 建议实现（事件类型可复用/新增） |
| `handleDrag` / `handleDrop`（QMimeData/响应对象） | 需 XMimeData 与响应对象；X 现有 handleDropEvent 签名不同 |
| `handleNativeEvent` | 建议实现（XWindow_nativeEvent_base 已存在） |
| `handleScreenAdded/Removed/PrimaryScreenChanged/OrientationChange/GeometryChange/LogicalDotsPerInchChange/RefreshRateChange` | 与 XScreen 注册表/信号对接，建议实现 |
| `handleThemeChange` / `handleFileOpenEvent`（2 重载） | 可选/建议 |
| `handleTabletEvent`（4 重载）/ `handleTabletEnterLeaveProximityEvent`（2 重载） | 需先建 XTabletEvent |
| `handleGestureEvent` 家族（3 重载）/ `handlePlatformPanelEvent` / `handleEnterWhatsThisEvent` | 可选，暂缓 |
| `handleContextMenuEvent` | Qt 6.8 仍保留模板版（qwindowsysteminterface.h:258-262）；X 建议实现 |
| `registerInputDevice` | 需输入设备抽象，暂缓 |
| `sendWindowSystemEvents` / `setSynchronousWindowSystemEvents` / `deferredFlushWindowSystemEvents` / `windowSystemEventsQueued` / `nonUserInputEventsQueued` | 事件循环管理入口，建议实现 |

**签名差异（已实现入口）**

| Qt 6.8 原型 | X | 判定 |
|---|---|---|
| `handleKeyEvent(window, type, key, mods, text="", autorep=false, count=1)` | `handleKeyEvent(window, type, key, modifiers, autoRepeat)` | ❌ 缺 text/count；XKeyEvent 也无 text/native 字段（XEvent.h:327-332），键盘文本无法送达 |
| `handleMouseEvent(window, local(QPointF), global(QPointF), state, button, type, mods, source)` 多模板 | `handleMouseEvent(window, type, button, buttons, modifiers, position(XPoint))` | ❌ 缺全局坐标、QPointF 精度、source、timestamp、device |
| `handleWheelEvent(window, local, global, pixelDelta, angleDelta, mods, phase, source, inverted)` | `handleWheelEvent(window, buttons, modifiers, position, angleDelta)` | ❌ 缺 pixelDelta/phase/inverted/globalPosition |
| `handleCloseEvent → bool` | `→ bool` | ✅ |
| `static bool flushWindowSystemEvents(flags=AllEvents)` | `void XWindowSystemInterface_flushWindowSystemEvents(flags)` | ❌ 返回类型/默认参数不一致 |
| `handleEnterEvent(window, local=QPointF(), global=QPointF())` | `handleEnterEvent(window, position, globalPosition)` | ✅ 等价 |
| `handleExposeEvent(window, const QRegion&)` | `handleExposeEvent(window, const XRegion*)` | ✅ 等价（内部深拷贝） |

**功能缺口**
1. 键盘事件无文本/计数字段（输入法以外的可打印字符键依赖 XKey 码位，应用需自行转换）。
2. 鼠标/滚轮无全局坐标（XMouseEvent 无 globalPosition 字段，XEvent.h:383-389）。
3. 滚轮无像素增量/滚动相位/方向反转，触摸板平滑滚动语义丢失。
4. 拖放 handleDropEvent 无 actions/buttons/modifiers，无法回复 proposedAction（XWindowSystemInterface.h:157-160）。
5. 同步注入系列一律 `sendSpontaneousEvent`，无异步投递选项（Qt 有 DefaultDelivery/AsynchronousDelivery 模板）。

**违规**：无硬约束违规；`handleShowEvent/handleHideEvent` 为 Qt WSI 不存在的项目扩展（头注已声明，XWindowSystemInterface.h:101-114），与约束 7“不保留旧 API”不冲突但需在移植 Qt QPA 时注意（qt-qpa-port 技能）。

### 3.4 XWindowEvent 事件类合集 ↔ qevent.h

公共基类差异：Qt 事件默认 `accepted=true`（QEvent 构造），XEvent 默认 `accepted=false`（XEvent.c:64-71 清零）——影响下述所有事件，XCloseEvent 已单独补偿（XWindowEvent.c:388）。

#### 3.4.1 XResizeEvent ↔ QResizeEvent（qevent.h:548-559）
- 继承 ✓；API：size/oldSize 齐全 ✅；X 额外提供 normalSize/normalOldSize（Qt 6.8 头文件已无，属扩展）。
- 缺口：无。功能：clone 用 XMemcpy（XWindowEvent.c:30）→ 违规项（见 3.4.13）。

#### 3.4.2 XExposeEvent ↔ QExposeEvent（qevent.h:515-529）
- 继承 ✓；API：region() 齐全 ✅（Qt 6.8 已弃用但保留）。
- 功能：region 深拷贝 + deinit/clone 虚槽正确（XWindowEvent.c:37-57）；clone 先 XMemcpy 复制含指针的 XRegion 再重建深拷贝——**字面违反“禁止 memcpy 复制对象”**（XWindowEvent.c:49）。

#### 3.4.3 XPaintEvent ↔ QPaintEvent（qevent.h:486-500）
- 继承 ✓；API：rect()/region() 齐全 ✅；rect 由 region 外接矩形计算（XWindowEvent.c:351）。
- 功能：deinit/clone 正确；clone 同样 XMemcpy 整体复制（XWindowEvent.c:70）→ 违规项。

#### 3.4.4 XCloseEvent ↔ QCloseEvent（qevent.h:562-567）
- 继承 ✓；API 齐全 ✅；init 置 accepted=true 对齐 Qt 默认（XWindowEvent.c:388）。
- clone 用 XMemcpy（XWindowEvent.c:83）→ 违规项。

#### 3.4.5 XShowEvent / XHideEvent ↔ QShowEvent / QHideEvent（qevent.h:578-591）
- 继承 ✓；API 齐全 ✅；clone 用 XMemcpy（XWindowEvent.c:94/105）→ 违规项。

#### 3.4.6 XFocusEvent ↔ QFocusEvent（qevent.h:470-483）
- 继承 ✓；API：gotFocus/lostFocus/reason 齐全 ✅；`setReason` 为 X 扩展（Qt 无公开 setter，扩展无害）。
- XFocusReason 数值 0-8 与 Qt::FocusReason 完全一致（XWindowEvent.h:65-76，含 Popup/Other/NoReason）。
- clone 用 XMemcpy（XWindowEvent.c:116）→ 违规项。

#### 3.4.7 XWheelEvent ↔ QWheelEvent（qevent.h:280-316）
- 继承 ✗：Qt 链 `QWheelEvent→QSinglePointEvent→QPointerEvent→QInputEvent→QEvent`；X 直接 `→XEvent`，缺 XInputEvent/XPointerEvent/XSinglePointEvent 中间基类。
- API 缺口：`pixelDelta()`、`phase()`、`inverted()/isInverted()`、`hasPixelDelta()`、`source()`、`DefaultDeltasPerStep=120` 常量（Qt 6.8）；X 仅 position/globalPosition/angleDelta/buttons/modifiers。
- 签名差异：Qt 坐标为 QPointF，X 用整型 XPoint（亚像素丢失）。
- 功能：头注自认“本简化实现不单独承载 pixelDelta”（XWindowEvent.h:451）→ 违反约束 3 的精神（行为近似）。

#### 3.4.8 XEnterEvent ↔ QEnterEvent（qevent.h:165-194）
- 继承 ✗（缺 QSinglePointEvent 等中间层）；API 缺口：`scenePosition()`（Qt 6.8 三坐标体系）；X 仅 position/globalPosition。
- Qt 6.8 已弃用 pos/globalPos/localPos/windowPos/screenPos 等整型/别名访问器，X 不提供可接受。

#### 3.4.9 XContextMenuEvent ↔ QContextMenuEvent（qevent.h:593-622）
- 继承 ✗（Qt 经 QInputEvent，X 缺 XInputEvent 层）。
- API 缺口：`Reason::Other`（Qt 有 Mouse/Keyboard/**Other**，X 只有 Mouse/Keyboard，XWindowEvent.h:530-535）。
- X 的 position/globalPosition/modifiers 等价 Qt 的 pos/globalPos + QInputEvent::modifiers；x/y/globalX/globalY 为 Qt 已弃用接口，可不提供。

#### 3.4.10 XInputMethodEvent ↔ QInputMethodEvent（qevent.h:624-677）
- 继承 ✓；API 缺口：`attributes()`/`Attribute{type,start,length,value}`（TextFormat/Cursor/Language/Ruby/Selection）与 `setCommitString(text, replaceFrom=0, replaceLength=0)`。
- X 扩展：cursorPosition/anchorPosition（Qt 中属 QInputMethodQueryEvent 语义）；构造参数直接携带 commitString 与 replacement（等价 Qt 的 setCommitString 结果）。
- 功能：深拷贝/析构/克隆正确（XWindowEvent.c:123-147），无 memcpy。

#### 3.4.11 XDropEvent ↔ QDropEvent 家族（qevent.h:702-785）
- 继承：单类 ✓（`→XEvent`），但 Qt 家族为 `QDropEvent→(QDragMoveEvent)→QDragEnterEvent` + `QDragLeaveEvent` 3 个派生类，X 用 type 字段（DRAG_ENTER/MOVE/LEAVE/DROP）合并——**家族未拆分**。
- API 缺口：`possibleActions()`、`proposedAction()`、`dropAction()/setDropAction()`、`acceptProposedAction()`、`source()`、`mimeData()`、`buttons()/modifiers()`（Qt QDropEvent 携带按键/修饰键，X 结构体无此字段，XWindowEvent.h:140-147）；QDragMoveEvent::`answerRect()` 与 `accept(rect)/ignore(rect)`。
- 功能：仅承载 position/globalPosition/mimeType/data 字符串（适配 text/uri-list 等），拖放协议无法回复动作——显著简化。

#### 3.4.12 缺失的事件负载类（见第五节清单）
XMoveEvent、XTouchEvent、XTabletEvent、QInputMethodQueryEvent 等价类完全不存在；`XWindow_moveEvent_base/touchEvent_base/tabletEvent_base` 槽收到的是无负载 XEvent。

#### 3.4.13 XWindowEvent 合集违规汇总
1. **XMemcpy 克隆对象**：XWindowEvent.c:30/49/70/83/94/105/116 共 7 处（约束 6“禁止 memcpy 复制对象”硬违规；Expose/Paint 虽已做指针重建+深拷贝，字面仍违规，建议改用逐字段赋值或 create+init）。
2. 事件默认 accepted=false 与 Qt 相反（基类 XEvent 问题，本模块仅 XCloseEvent 补偿）。
3. XWheelEvent/XDropEvent 行为简化（约束 3）。
4. 部分单行访问器缺 `@param/@return`（如 XWindowEvent.h:117-124、415-425、473-485），Doxygen 完整性 P2。

## 四、缺失 Qt 类清单（Window 对应范围）

| Qt 类 | Qt 头文件（相对 qtbase/src/gui/kernel/） | 建议 |
|---|---|---|
| QSurface | qsurface.h | **不实现**（接口方法已合并进 XWindow：surfaceType/surfaceClass/supportsOpenGL/format/size；继承链偏差已在文档标注），或建 XSurface 接口层 |
| QOffscreenSurface | qoffscreensurface.h | **建议实现**（XWindowSurfaceClass_Offscreen 枚举已预留，XWindow.h:160-164） |
| QMoveEvent | qevent.h | **建议实现** XMoveEvent（moveEvent 槽现收无负载 XEvent，丢失 pos/oldPos；XEventType 已有 MOVE=13） |
| QTouchEvent | qevent.h | **建议实现**最小 XTouchEvent（触点 id/位置/状态/压力；XEVENT_TYPE_TOUCH_* 已存在） |
| QTabletEvent | qevent.h | **建议实现**最小 XTabletEvent（压力/倾斜/旋转/z；XEVENT_TYPE_TABLET_* 已存在） |
| QDragEnterEvent | qevent.h | **建议拆分/扩展**：从 XDropEvent 分出 DragEnter 类型语义与 accept 回复 |
| QDragMoveEvent | qevent.h | **建议拆分/扩展**：answerRect/accept(rect)/ignore(rect) |
| QDragLeaveEvent | qevent.h | **建议拆分/扩展**：无负载拖离事件 |
| QInputMethodQueryEvent | qevent.h | **建议实现**（XEVENT_TYPE_INPUT_METHOD_QUERY=207 已定义，XEventType.h:216） |
| QPlatformSurfaceEvent | qevent.h | **建议实现**（createHandle/destroy 时通知 SurfaceCreated/AboutToBeDestroyed） |
| QWindowStateChangeEvent | qevent.h | **可选**（windowStateChanged 信号已覆盖新状态，缺 oldState/isOverride） |
| QChildWindowEvent | qevent.h | **建议实现**（setParent 事件链缺失，配合 3.1-F3） |
| QScreenOrientationChangeEvent | qevent.h | **可选**（XScreen::orientationChanged 已覆盖） |
| QApplicationStateChangeEvent | qevent.h | **建议实现**（配合 WSI handleApplicationStateChanged） |
| QNativeGestureEvent | qevent.h | **暂不实现**（无手势输入层） |
| QIconDragEvent | qevent.h | **暂不实现**（拖拽图标展示，属平台/Widget 边界） |
| QInputMethodEvent::Attribute | qevent.h | **建议实现**（XInputMethodEvent 缺 attributes 列表） |

注：XKeyEvent/XMouseEvent 定义于 `Src/XCode/XEvent/XEvent.h`（非 Window 模块），但其缺口（无 text/native 键码、无 globalPosition、无 QInputEvent 中间基类）直接影响本模块 WSI 与窗口事件语义，已列入 3.3。

## 五、优先任务建议（按优先级）

1. **P0 修复 `XWindow_isTopLevel`**：改为只检查 `m_parentWindow == NULL`（对齐 qwindow.cpp:843-846），同步修正 `close()`/`setTransientParent`/`isAncestorOf` 判定，补回归夹具（带瞬态父窗口可 close）。
2. **P0 去除 XWindowEvent.c 7 处 XMemcpy 克隆**：改用 `create_ex + init` 或逐字段赋值深拷贝，消除“禁止 memcpy 复制对象”硬违规；顺带统一 `VX*_clone` 风格。
3. **P0 对齐事件 accepted 默认值**：让 XEvent 基类默认 accepted=true（或窗口事件 init 统一补偿），使 resize/paint/show/hide/key/mouse/wheel 派发后状态与 Qt 一致。
4. **P1 补 XWindow 缺口与头注**：实现或明确裁剪 `setVulkanInstance/vulkanInstance`，修正 XWindow.h:3“实现全部公开 API”表述。
5. **P1 发射缺失信号/事件**：requestActivate/raise/lower 后发射 `activeChanged`；`requestUpdate` 投递 `XEVENT_TYPE_UPDATE_REQUEST`（XEventType.h:70 已定义）。
6. **P1 事件负载补全**：实现 XMoveEvent/XTouchEvent/XTabletEvent 最小负载并接入 VXWindow_event；XWheelEvent 补 pixelDelta/phase/inverted、坐标改 QPointF；XEnterEvent 补 scenePosition。
7. **P1 XDropEvent 动作集**：补 possibleActions/proposedAction/dropAction/acceptProposedAction/mimeData/source/buttons/modifiers，或拆分 QDragEnterEvent/QDragMoveEvent/QDragLeaveEvent；WSI handleDropEvent 同步扩展。
8. **P2 WSI 分批补齐**：先实现窗口状态/屏幕/应用状态/触摸/tablet 注入与 `sendWindowSystemEvents` 等队列管理入口；键盘补 text/count、鼠标补全局坐标/source。
9. **P2 输入法补全**：XInputMethodEvent 补 attributes()/setCommitString；实现 XInputMethodQueryEvent（类型已预留）。
10. **P2 工程性修复**：`XWindow_Protected.h` 补 UTF-8 BOM；XWindowEvent.h 单行访问器补 `@param/@return`；startSystemResize/Move 接入平台后端或改为显式“未支持”返回。
