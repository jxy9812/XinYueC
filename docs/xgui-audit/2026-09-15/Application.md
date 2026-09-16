# XGui Application 模块 Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计模块：`Application`（`Src/XGui/Application/`）
- 审计对象：`XApplication.h/.c`、`XGuiApplication.h/.c`（只读审计，未改动 Src/、Test/ 任何源码，未 commit/push）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/kernel/qapplication.h`、
  `qtbase/src/gui/kernel/qguiapplication.h`、`qtbase/src/corelib/kernel/qcoreapplication.h`
  （行为对照另参考同目录 `qapplication.cpp`、`qguiapplication.cpp`）
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`
  （3.2 节、10.474/10.475/10.476/10.477 等 Application 相关章节）
- 结论摘要：`XGuiApplication` 公开 API 对齐度很高（Qt 6.8 公开 API 与 14 个信号全部有对应）；
  `XApplication` 仅完成核心（注册表/焦点登记/交互参数/事件循环），Qt 控件级 API 缺失约 26 项
  （style/样式类静态重载、allWidgets/topLevelAt、beep/alert、效果开关、QSS、closeAllWindows/aboutQt、
  focusChanged 信号）。存在 1 项 P0 硬约束违规（`XGuiApplication.h` 缺 UTF-8 BOM）与 2 项信号/Doxygen 违规。

---

## 一、模块概览

| X 类 | 文件 | 对标 Qt 类 | X 继承链 | Qt 继承链 | 继承一比一 | 对齐度 |
|---|---|---|---|---|---|---|
| XGuiApplication | XGuiApplication.h/.c | QGuiApplication | XGuiApplication→XCoreApplication→XObject | QGuiApplication→QCoreApplication→QObject | ✓ | 高（约 92%） |
| XApplication | XApplication.h/.c | QApplication | XApplication→XGuiApplication→XCoreApplication→XObject | QApplication→QGuiApplication→QCoreApplication→QObject | ✓ | 中（约 45%） |
| 基类（模块外参考） | XCode/XCoreApplication/XCoreApplication.h/.c | QCoreApplication | XCoreApplication→XObject | QCoreApplication→QObject | ✓ | 参考 |

- 模块开关：`XGUIAPPLICATION_ON`（XGuiConfig.h:38，默认 1）、`XAPPLICATION_ON`（XGuiConfig.h:234，默认 1；
  且 `!XGUIAPPLICATION_ON` 时强制裁剪，XGuiConfig.h:568-570）。
- 平台依赖：两个类均不直接调用平台 API；平台能力经 `XPlatformIntegration`（`XPLATFORMINTEGRATION_ON` 守卫）
  与平台注入钩子（`addWindow`/`screenAdded`/`setFocusWindow` 等）接入。
- 测试覆盖：`Test/XGuiTest/` 下**无** `XApplicationTest.c`/`XGuiApplicationTest.c` 专用测试；
  仅 `XScrollBarTest.c` 间接创建 `XGuiApplication`（create_ex/delete_base），其余覆盖散落在
  `XGuiWindowDemo_Test` 等演示程序中。模块级信号/注册表/退出策略缺少直接回归。

---

## 二、逐类对比

### 2.1 XGuiApplication ↔ QGuiApplication（qtbase/src/gui/kernel/qguiapplication.h）

#### 2.1.1 继承关系

- Qt：`QGuiApplication : QCoreApplication`（QCoreApplication : QObject）。
- X：`typedef struct XGuiApplication { XGuiApplication m_class; ... }`，第一成员为 `XGuiApplication`，
  虚表 `XCLASS_DEFINE_BEGING(XGuiApplication) / XCLASS_DEFINE_EXTEND_END(XGuiApplication, XCoreApplication)`，
  `class_init` 中 `XVTABLE_INHERIT_XCLASS(XCoreApplication)` + 仅重载 `EXClass_Deinit`。
- 判定：**一比一 ✓**，无缺失中间基类（XCoreApplication↔QCoreApplication、XObject↔QObject 均对应）。
- Qt 中 QGuiApplication 重写保护虚函数 `event/compressEvent/notify`；X 侧 Notify/Event 沿用
  XCoreApplication 父类槽位（`XGuiApplication_notify → XCoreApplication_notify_base` 宏），
  属于裁剪/简化但行为入口一致，可接受。

#### 2.1.2 API 对照总表（Qt 6.8 公开 API）

| Qt API（原型） | X API | 状态 |
|---|---|---|
| `QGuiApplication(int&, char**, int = ApplicationFlags)` | `XGuiApplication_create_ex(XMemoryType, int, char**)` / `XGuiApplication_init` | ✓（缺 flags 参数，P2） |
| `static void setApplicationDisplayName(const QString&)` / `static QString applicationDisplayName()` | `XGuiApplication_setApplicationDisplayName(const XString*)` / `XGuiApplication_applicationDisplayName(void)` | ✓（缺 `_2` 重载，见 V2；displayName 回退 applicationName 与 Qt 一致） |
| `Q_SLOT void setBadgeNumber(qint64)` | `XGuiApplication_setBadgeNumber(int64_t)` | ✓（额外提供 badgeNumber() getter，Qt 6.8 无 getter，属扩展不冲突） |
| `static void setDesktopFileName(const QString&)` / `static QString desktopFileName()` | `XGuiApplication_setDesktopFileName(const XString*)` / `desktopFileName(void)` | ✓（缺 `_2` 重载，见 V2） |
| `static QWindowList allWindows()` | `XGuiApplication_allWindows(void)`（新建 XVector 借用指针） | ✓ |
| `static QWindowList topLevelWindows()` | `XGuiApplication_topLevelWindows(void)` | ✓（按无父窗口过滤，ExcludeTransients） |
| `static QWindow *topLevelAt(const QPoint&)` | `XGuiApplication_topLevelAt(const XPoint*)` | ✓（逆序+可见优先+不可见回退，与 Qt 一致） |
| `static void setWindowIcon(const QIcon&)` / `static QIcon windowIcon()` | `setWindowIcon(const XIcon*)` / `windowIcon(void)`（深拷贝返回） | ✓ |
| `static QString platformName()` | `platformName(void)`（恒 "xiniyue-embedded"） | ✓ |
| `static QWindow *modalWindow()` / `focusWindow()` / `QObject *focusObject()` | 同名 X API | ✓ |
| `static QScreen *primaryScreen()` / `QList<QScreen*> screens()` / `screenAt(const QPoint&)` | 同名 X API（转发 XScreen 注册表） | ✓ |
| `qreal devicePixelRatio() const` | `float XGuiApplication_devicePixelRatio(void)`（全屏 DPR 最大值，默认 1.0） | ✓（10.477 已按 Qt 修正） |
| `static QCursor *overrideCursor()` / `setOverrideCursor` / `changeOverrideCursor` / `restoreOverrideCursor` | 同名 X API（深拷贝入栈） | ✓ |
| `static QFont font()` / `static void setFont(const QFont&)` | `XGuiApplication_font(void)`（堆拷贝）/ `setFont(const XFont*)` | ✓（见 F1） |
| `static QClipboard *clipboard()` | `XGuiApplication_clipboard(void)`（惰性单例） | ✓ |
| `static QPalette palette()` / `setPalette(const QPalette&)` | `XGuiApplication_palette(void)`（值拷贝）/ `setPalette(const XPalette*)`（XPALETTE_ON 守卫） | ✓ |
| `static Qt::KeyboardModifiers keyboardModifiers()` / `queryKeyboardModifiers()` / `Qt::MouseButtons mouseButtons()` | 同名 X API | ✓ |
| `static void setLayoutDirection(Qt::LayoutDirection)` / `layoutDirection()` / `isRightToLeft()` / `isLeftToRight()` | 同名 X API（请求方向/有效方向分离） | ✓ |
| `static QStyleHints *styleHints()` | `XGuiApplication_styleHints(void)`（惰性单例） | ✓ |
| `static void setDesktopSettingsAware(bool)` / `desktopSettingsAware()` | 同名 X API | ✓ |
| `static QInputMethod *inputMethod()` | `XGuiApplication_inputMethod(void)`（惰性单例） | ✓ |
| `static QPlatformNativeInterface *platformNativeInterface()` | `XGuiApplication_platformNativeInterface(void)` | ✓ |
| `static QFunctionPointer platformFunction(const QByteArray&)` | `void* XGuiApplication_platformFunction(const char*)` | ✓（参数为 const char* 查找键，见 2.1.5 V2 说明） |
| `static void setQuitOnLastWindowClosed(bool)` / `quitOnLastWindowClosed()` | 同名 X API | ✓ |
| `static Qt::ApplicationState applicationState()` | `XGuiApplication_applicationState(void)`（另提供平台注入 setter） | ✓ |
| `static void setHighDpiScaleFactorRoundingPolicy(...)` / `highDpiScaleFactorRoundingPolicy()` | 同名 X API | ✓ |
| `static int exec()` | `#define XGuiApplication_exec XCoreApplication_exec`（返回 int） | ✓ |
| `bool notify(QObject*, QEvent*) override` | `#define XGuiApplication_notify XCoreApplication_notify_base` | ✓ |
| `static void processEvents(QEventLoop::ProcessEventsFlags)` | `XGuiApplication_processEvents(XEventLoopProcessEventsFlags)` | ✓ |
| 会话：`isSessionRestored()/sessionId()/sessionKey()/isSavingSession()` | 同名 X API + `XGuiApplication_setSessionState` 平台注入 | ✓ |
| `static void sync()` | `XGuiApplication_sync(void)`（顺序与 Qt 一致：processEvents→平台 sync→processEvents→flushWindowSystemEvents） | ✓ |

#### 2.1.3 信号对照（14/14）

| Qt 6.8 信号 | X 信号 | 空参 args | 状态 |
|---|---|---|---|
| `fontDatabaseChanged()` | `XGuiApplication_fontDatabaseChanged_signal(app)` | NULL | ✓ |
| `screenAdded(QScreen*)` | `XGuiApplication_screenAdded_signal(app, XScreen*)` | - | ✓ |
| `screenRemoved(QScreen*)` | `XGuiApplication_screenRemoved_signal(app, XScreen*)` | - | ✓ |
| `primaryScreenChanged(QScreen*)` | `XGuiApplication_primaryScreenChanged_signal(app, XScreen*)` | - | ✓ |
| `lastWindowClosed()` | `XGuiApplication_lastWindowClosed_signal(app)` | NULL | ✓ |
| `focusObjectChanged(QObject*)` | `XGuiApplication_focusObjectChanged_signal(app, XObject*)` | - | ✓ |
| `focusWindowChanged(QWindow*)` | `XGuiApplication_focusWindowChanged_signal(app, XWindow*)` | - | ✓ |
| `applicationStateChanged(Qt::ApplicationState)` | `XGuiApplication_applicationStateChanged_signal(app, XGuiApplicationState)` | - | ✓ |
| `layoutDirectionChanged(Qt::LayoutDirection)` | `XGuiApplication_layoutDirectionChanged_signal(app, XGuiLayoutDirection)` | - | ✓ |
| `commitDataRequest(QSessionManager&)` | `XGuiApplication_commitDataRequest_signal(app, XSessionManager*)`（恒传 NULL） | - | ✓ |
| `saveStateRequest(QSessionManager&)` | `XGuiApplication_saveStateRequest_signal(app, XSessionManager*)`（恒传 NULL） | - | ✓ |
| `applicationDisplayNameChanged()` | `XGuiApplication_applicationDisplayNameChanged_signal(app)` | NULL | ✓ |
| `paletteChanged(const QPalette&)`（6.0 弃用） | `XGuiApplication_paletteChanged_signal(app, XPalette*)` | - | ✓ |
| `fontChanged(const QFont&)`（6.0 弃用） | `XGuiApplication_fontChanged_signal(app, XFont*)` | - | ✓ |

#### 2.1.4 枚举对照

- `XGuiLayoutDirection`（LeftToRight=0/RightToLeft/Auto）↔ `Qt::LayoutDirection` ✓。
- `XGuiApplicationState`（Suspended=0x0/Hidden=0x1/Inactive=0x2/Active=0x4）↔ `Qt::ApplicationState` ✓。
- `XGuiDpiRoundingPolicy`（Unset=0/Round/Ceil/Floor/RoundPreferFloor/PassThrough）↔
  `Qt::HighDpiScaleFactorRoundingPolicy` ✓。

#### 2.1.5 功能缺口（F）与违规（V）

**功能缺口**

- F1 `font()`：X 在从未 setFont 时返回 NULL；Qt 6.8 `QGuiApplication::font()` 返回平台/主题默认应用字体
  （qguiapplication.cpp），不会返回空。头文件已注明该差异，但与 Qt 行为不一致。
- F2 `lastWindowClosed` 触发语义：X 在 `removeWindow` 时注册表变空即发射（且 exec 外也发射）；
  Qt 仅在 `in_exec` 时发射（qguiapplication.cpp `maybeLastWindowClosed`），且判定依据是
  `lastWindowClosed()`——即“不存在参与关闭的**可见**顶层窗口”（`participatesInLastWindowClosed` +
  `treatAsVisible`），隐藏但未销毁的窗口不阻止发射；退出还受 `canQuitAutomatically()`（QEventLoopLocker）
  约束。X 的实现是“注册表清空”的近似，隐藏窗口会阻止信号，且绕过 quit lock。
- F3 `setFont`/`setPalette`：X 仅发射弃用信号 `fontChanged/paletteChanged`；Qt 6.8 主路径还会向应用发送
  `QEvent::ApplicationFontChange` / `QEvent::ApplicationPaletteChange`（事件体系侧无对应分发）。
- F4 `setWindowIcon`/`setDesktopFileName`/`setBadgeNumber`：X 只更新字段；Qt 会同步到平台集成
  （QPA `setWindowIcon`/`setDesktopFileName`/`setBadgeNumber`）。嵌入式无原生需求时可接受，但属行为简化。
- F5 `setApplicationDisplayName` 仅比较/替换并发射信号；Qt 还会通知平台（`QPlatformIntegration`）并触发
  `applicationDisplayNameChanged` 的跨线程通知。X 未接平台，可接受（与 F4 同类）。

**违规（硬约束逐条详见第三节）**

- V1（P0，约束 4）：`XGuiApplication.h` **缺少 UTF-8 BOM**（文件首字节为 `2f 2a 2a` 即 `/**`；
  同目录 XGuiApplication.c、XApplication.h/.c 均为 `ef bb bf`）。
- V2（P1，约束 1 字符串规则）：
  - `XGuiApplication_setSessionState(bool, bool, const char* id, const char* key)` 主版本直接用
    `const char*`，违反“API 主版本用 XString、UTF-8 用 `_2` 后缀重载”（XGuiApplication.h:716、
    XGuiApplication.c:1124）。
  - `setApplicationDisplayName(const XString*)`、`setDesktopFileName(const XString*)` 缺少
    `_2`（UTF-8 const char*）重载；同仓库 XWidget 的字符串 API 均为 `XString` 主版本 + `_2` 模式
    （如 `XWidget_setWindowTitle/_2`）。
  - `platformFunction(const char*)` 参数为 const char* 查找键（对应 Qt QByteArray），未提供 XString 主版本，
    按库内惯例建议补 `_2` 或改 XString 主版本。
- V3（P1，约束 4 Doxygen）：14 个信号声明（XGuiApplication.h:730-770）、`isRightToLeft/isLeftToRight`
  （550-554）、会话 4 个 getter（697-707）以及 `exec/quit/notify/sendEvent/postEvent/sendPostedEvents/
  removePostedEvents/sendSpontaneousEvent` 8 个宏（655-668）缺 `@param/@return`（信号连 `@param` 也没有）。
- V4（P2）：缺 `XGuiApplication_create()` 默认宏（仅 create_ex），与全库 `XType_create()` 规范不一致
  （XApplication 有 `XApplication_create()`）。

### 2.2 XApplication ↔ QApplication（qtbase/src/widgets/kernel/qapplication.h）

#### 2.2.1 继承关系

- Qt：`QApplication : QGuiApplication`（→ QCoreApplication → QObject），无中间基类。
- X：`typedef struct XApplication { XGuiApplication m_class; ... }`，第一成员 XGuiApplication；
  虚表 `XCLASS_DEFINE_BEGING(XApplication) / XCLASS_DEFINE_EXTEND_END(XApplication, XGuiApplication)`，
  `class_init` 继承 XGuiApplication 虚表并仅重载析构。
- 判定：**一比一 ✓**（含中间基类层 XGuiApplication↔QGuiApplication、XCoreApplication↔QCoreApplication、
  XObject↔QObject）。Qt QApplication 相对 QGuiApplication 无新增公开虚函数（仅重写保护虚函数
  event/compressEvent/notify），X 不追加槽位 ✓。

#### 2.2.2 API 缺口表（Qt 原型 → X 状态）

X 已覆盖：`topLevelWidgets`、`activePopupWidget`、`activeModalWidget`、`focusWidget`、`activeWindow`、
`setActiveWindow`、`widgetAt(QPoint)`、`cursorFlashTime/doubleClickInterval/keyboardInputInterval/
wheelScrollLines/startDragTime/startDragDistance` 六组 get/set、`exec`（宏）、`notify`（宏）、
`XApplication_instance`（对标 `QCoreApplication::instance`/`qApp`）。

缺失（Qt 原型，按 qapplication.h 顺序）：

| # | Qt 原型 | X 状态 |
|---|---|---|
| 1 | `static QStyle *style()` | 缺失（XStyle 已存在于 Src/XGui/Style/XStyle.h） |
| 2 | `static void setStyle(QStyle*)` | 缺失 |
| 3 | `static QStyle *setStyle(const QString&)` | 缺失 |
| 4 | `static QPalette palette(const QWidget*)` | 缺失 |
| 5 | `static QPalette palette(const char *className)` | 缺失 |
| 6 | `static void setPalette(const QPalette&, const char* className = nullptr)` | 缺失 |
| 7 | `static QFont font(const QWidget*)` | 缺失 |
| 8 | `static QFont font(const char *className)` | 缺失 |
| 9 | `static void setFont(const QFont&, const char* className = nullptr)` | 缺失 |
| 10 | `static QFontMetrics fontMetrics()`（6.0 弃用） | 缺失（建议不实现） |
| 11 | `static QWidgetList allWidgets()` | 缺失 |
| 12 | `static QWidget *topLevelAt(const QPoint&)` | 缺失 |
| 13 | `static inline QWidget *topLevelAt(int x, int y)` | 缺失 |
| 14 | `static inline QWidget *widgetAt(int x, int y)` | 缺失（仅有 XPoint 版本） |
| 15 | `static void beep()` | 缺失 |
| 16 | `static void alert(QWidget *widget, int duration = 0)` | 缺失 |
| 17 | `static bool isEffectEnabled(Qt::UIEffect)` | 缺失 |
| 18 | `static void setEffectEnabled(Qt::UIEffect, bool enable = true)` | 缺失 |
| 19 | `QString styleSheet() const` | 缺失（XWidget_setStyleSheet 仅控件级“只存储不解释”；XCssStyleSheet/XStyleSheetStyle 已存在可支撑应用级 QSS） |
| 20 | `public Q_SLOT void setStyleSheet(const QString&)` | 缺失 |
| 21 | `bool autoSipEnabled() const` | 缺失 |
| 22 | `public Q_SLOT void setAutoSipEnabled(const bool)` | 缺失 |
| 23 | `static void closeAllWindows()` | 缺失 |
| 24 | `static void aboutQt()` | 缺失 |
| 25 | `Q_SIGNALS: void focusChanged(QWidget *old, QWidget *now)` | **缺失**（XApplication 全文件 0 个 `*_signal`） |
| 26 | 构造 `QApplication(int&, char**, int = ApplicationFlags)` | `XApplication_create_ex(XMemoryType, int, char**)` 缺 flags 参数（P2） |

另有 Qt 属性 8 项：cursorFlashTime/doubleClickInterval/keyboardInputInterval/wheelScrollLines/
startDragTime/startDragDistance（X 已覆盖 6 项）、styleSheet、autoSipEnabled（缺失）。

#### 2.2.3 功能缺口

- F1 `setActiveWindow`（XApplication.c:180-196）：X 仅设置 `m_activeWindow` 并转发
  `XGuiApplication_setFocusWindow(nativeWindow, NULL)`。Qt（qapplication.cpp `QApplicationPrivate::setActiveWindow`）
  会：相同窗口时直接返回；对旧窗口发 `ActivationChange/WindowDeactivate`（并按 styleHint
  SH_Widget_ShareActivation 扩散）、对新窗口发 `WindowActivate`；对旧焦点控件发
  `FocusAboutToChange` + `FocusOut`（ActiveWindowFocusReason）、提交输入法；最终 `emit focusChanged(prev, now)`。
  X 无任何事件与信号。
- F2 `setFocusWidget`（XApplication.c:204-208）：X 仅存指针。Qt 焦点切换（qapplication.cpp 焦点分发路径）
  对旧/新控件发送 FocusOut/FocusIn（含 style() 事件）并 `emit qApp->focusChanged(prev, focus_widget)`。
- F3 `widgetAt`：X 为“可见顶层逆序 + XWidget_childAtGlobal 命中”，Qt 还叠加
  `tryModalHelper`（模态拦截）、活动弹窗优先级与禁用控件处理；X 为可接受简化，但行为不完全一致。
- F4 `activeModalWidget/activePopupWidget`：X 仅提供 setter 钩子与 getter，无 Qt 的
  “激活窗口时自动关闭弹窗/模态 helper”联动逻辑（嵌入式场景可接受，建议在头文件注明简化边界）。
- F5 无 `style()`/`setStyle()` 入口：控件绘制依赖的样式引擎全局单例在 Application 层缺失
  （XCommonStyle/XFusionStyle/XStyleSheetStyle 已实现，属接线缺口）。

#### 2.2.4 违规

- V1（P0，约束 5 信号对齐）：`focusChanged(QWidget *old, QWidget *now)` 无对应
  `XApplication_focusChanged_signal`，且 setFocusWidget/setActiveWindow 无任何信号触发点。
- V2（P1，约束 4 Doxygen）：XApplication.h 中 6 组交互参数 getter/setter（213-230）、
  `XApplication_create()` 宏（96）、`XApplication_exec/quit` 宏（125-126）均只有一行 `@brief` 或无注释，
  缺 `@param/@return`。
- V3（P2）：`XApplication_setActiveWindow` 头文件注释未声明“Qt 6.5 起弃用”与 X 实现的简化差异。

### 2.3 基类链核对（模块外参考：XCoreApplication ↔ QCoreApplication）

- `XCoreApplication→XObject` 对应 `QCoreApplication→QObject`，一比一 ✓。
- QCoreApplication 的 `installTranslator/removeTranslator/translate` 与 `QTranslator` 在 X 侧完全缺失
  （见第四节缺失类）；`QEventLoopLocker` 已有 `XEventLoopLocker`（Src/XCode/XEvent）、
  `QPermission` 已有 `XPermission`、`QAbstractEventDispatcher/QAbstractNativeEventFilter` 均有对应。

---

## 三、模块内硬约束逐条核查

| # | 约束 | 核查结果 |
|---|---|---|
| 1 | 拥有型字符串一律 XString*；API 主版本 XString，UTF-8 用 `_2` 重载 | 成员全部 `XString*` ✓；违规：`setSessionState` 主版本 const char*、displayName/desktopFileName 缺 `_2`（见 2.1.5 V2） |
| 2 | 继承一比一含中间基类 | ✓ 两条链均一比一（见 2.1.1/2.2.1），无缺失中间基类 |
| 3 | 样式/绘制不得精简近似 | 本模块无绘制代码，N/A；仅 `widgetAt/焦点` 属事件语义简化（F3/F4，非绘制） |
| 4 | 公共头中文 Doxygen（@brief/@param/@return）+ UTF-8 BOM | ✗ `XGuiApplication.h` 缺 BOM（V1）；两文件多处公开 API 缺 @param/@return（V3） |
| 5 | 信号：空参 args=NULL；Qt 6.8 每个信号有对应 *_signal | XGuiApplication 14/14 ✓ 且空参信号传 NULL ✓；XApplication 的 `focusChanged` 缺失（V1） |
| 6 | init/deinit 成对；copy/move 安全；禁 memcpy/malloc/free/strdup | ✓ init/create_ex 与 deinit/delete_base 成对；全模块无 memcpy/malloc/free/strdup（仅 XMemory_malloc/XMemset/XString_create_utf8）；单例类与 Qt `Q_DISABLE_COPY` 一致，未提供 copy/move，无违规 |
| 7 | 旧 API 不保留 | ✓ 未见旧 API 并存 |
| 8 | 新代码 C99 | ✓ 无 C11/C++ 语法（for 循环内声明、stdbool/stdint 均为 C99） |

---

## 四、缺失 Qt 类清单（Application 对应范围）

| Qt 类 | Qt 头文件（相对 qtbase） | X 现状 | 建议 |
|---|---|---|---|
| QSessionManager | `src/widgets/kernel/qsessionmanager.h` | 仅有不透明占位 `typedef struct XSessionManager XSessionManager;`，commitDataRequest/saveStateRequest 参数恒传 NULL | **不实现**（嵌入式无会话管理需求；信号签名占位已满足对齐）。若未来接入，再补最小实现 |
| QTranslator | `src/corelib/kernel/qtranslator.h` | 完全缺失；XCoreApplication 亦无 installTranslator/removeTranslator/translate | **暂不实现**（QCoreApplication 翻译体系整体未做；有 i18n 需求时随 XCoreApplication 一并补齐） |

已确认**不缺失**：QStyle（XStyle）、QEventLoopLocker（XEventLoopLocker）、QPermission（XPermission）、
QAbstractEventDispatcher（XAbstractEventDispatcher）、QAbstractNativeEventFilter（XAbstractNativeEventFilter）、
QPlatformNativeInterface（XPlatformNativeInterface）、QInputMethod/QClipboard/QStyleHints/QPalette/QScreen/
QFont/QWindow/QWidget 等均有对应类。

---

## 五、优先任务建议（按优先级）

1. **P0 修复 BOM**：`XGuiApplication.h` 补 UTF-8 BOM（其余 3 个文件已有），避免格式回归。
2. **P0 补 focusChanged**：新增 `XApplication_focusChanged_signal(XApplication*, XWidget* old, XWidget* now)`
   （空参规则不适用，带两参数），在 `XApplication_setFocusWidget`/`setActiveWindow` 中触发；同步评估
   XWidget 焦点路径（FocusOut/FocusIn 事件）是否已有或需要补。
3. **P1 补 style()/setStyle()**：对接已存在的 XStyle/XCommonStyle/XFusionStyle，先实现
   `XApplication_style()` 与 `XApplication_setStyle(XStyle*)`（`setStyle(const XString*)` 样式名版本可随后补）。
4. **P1 补控件级静态重载**：`allWidgets()`、`topLevelAt()`、`font/palette` 的 className 重载
   （先补 `XApplication_font()` 无参与 `XApplication_palette()` 无参——注意 XGuiApplication 已有 font/palette，
   控件级版本需按 Qt 语义加 className 维度）。
5. **P1 补交互 API**：`beep()`、`alert(XWidget*, int duration)`、`isEffectEnabled/setEffectEnabled`、
   `closeAllWindows()`、`aboutQt()`（可裁剪开关）。
6. **P2 补 QSS/autoSip**：`styleSheet()/setStyleSheet()`（复用 XCssStyleSheet/XStyleSheetStyle，明确应用级
   语义）、`autoSipEnabled()/setAutoSipEnabled()`（嵌入式可返回固定值）。
7. **P2 修字符串 API 规则**：`setSessionState` 改为 `XString*` 主版本 + `_2`；补
   `setApplicationDisplayName_2/setDesktopFileName_2` 转发版本。
8. **P2 修行为偏差**：`XGuiApplication_font()` 未设置时返回默认字体（或至少文档化）；`lastWindowClosed`
   按“无可见顶层窗口 + in_exec + quit lock”修正；`setFont/setPalette` 补
   ApplicationFontChange/PaletteChange 事件分发（如事件体系支持）。
9. **P2 补 Doxygen**：14 个信号 + 交互参数 getter/setter + 宏别名统一补 `@brief/@param/@return`。
10. **P2 补测试**：新增 `Test/XGuiTest/XGuiApplicationTest.c`（信号 14 项、注册表、lastWindowClosed、
    退出策略、BOM 无需测）与 `XApplicationTest.c`（focusChanged、widgetAt/topLevelAt、交互参数），
    并补充 `XGuiApplication_create()` 默认宏与全库风格一致。

---

*本报告为只读审计产物；除本文件外未修改 Src/、Test/ 任何内容，未执行 git commit/push。*
