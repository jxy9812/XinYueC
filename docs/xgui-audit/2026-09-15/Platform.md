# Platform 模块 XGui ↔ Qt 6.8.3 对齐审计报告

- 审计日期：2026-09-15
- 审计范围：`Src/XGui/Platform/`（12 个头文件 + 11 个 .c 实现，全量读取；`*_Protected.h`/`*_Internal.h` 本模块无）
- Qt 基准：Qt 6.8.3 QPA 头文件（注意：任务书写的 `src/gui/platform/*.h` 实际位于
  `/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/kernel/` 与 `gui/painting/`、`gui/accessible/`、`gui/text/`，
  本报告按实际路径引用），行为对照 `qplatformintegration.cpp / qplatformwindow.cpp / qplatformbackingstore.cpp / qplatforminputcontext.cpp / qplatformnativeinterface.cpp / qplatformaccessibility.cpp`
- 审计方式：只读；未修改 `Src/`、`Test/` 任何源码；未 commit / push
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、`XGui.md`（§3.3 目录、§10.31/§10.313/§10.392/§10.393 相关章节）
- 总评：**API 命名对齐度中等偏上（核心 QPA 角色齐全），但存在 4 个公共头无 UTF-8 BOM、字符串约束系统性违规（const char* 主版本 / char[64] / char* 长期持有）、QPlatformScreen/GraphicsBuffer 等 16+ 个 Qt 类无对应，XPlatformInputContext RTL 判定与 Qt 行为近似**。

## 一、模块概览

| X 类 | Qt 对标 | X 头文件 | X 继承链 | Qt 继承链 | 继承一比一 | API 覆盖 | 功能完整性 | 综合 |
|---|---|---|---|---|---|---|---|---|
| XPlatformIntegration | QPlatformIntegration | XPlatformIntegration.h | →XObject | 无基类（纯接口类） | ✗ | 全（34/34，多处恒 NULL/恒 false 占位） | 中 | ~82% |
| XPlatformWindow | QPlatformWindow | XPlatformWindow.h | →XObject | →QPlatformSurface（无基类） | ✗（缺 surface 层、多 XObject 层） | 低（轻量子集，约 14/50） | 中 | ~55% |
| XPlatformNativeWindow | QPA 内部实现（QXcbWindow 等） | XPlatformNativeWindow.h | 纯函数契约 | 无公共头 | —（N/A） | —（契约自定） | 中（真实实现在 Drive） | ~65% |
| XPlatformBackingStore | QPlatformBackingStore | XPlatformBackingStore.h | 不透明句柄 | 无基类（虚函数类） | ✗（形式不同） | 中（核心 10/18，缺 RHI 系 8 个） | 高（软件核心完整） | ~75% |
| XPlatformTheme | QPlatformTheme | XPlatformTheme.h | 不透明快照 | Q_GADGET 类 | ✗（形式不同） | 低（name/isDark vs ~22 虚函数+8 枚举） | 低 | ~30% |
| XPlatformServices | QPlatformServices | XPlatformServices.h | 不透明对象 | 无基类 | ✗（形式不同） | 低（2/6） | 低 | ~40% |
| XPlatformFontDatabase | QPlatformFontDatabase | XPlatformFontDatabase.h | 不透明对象 | 无基类 | ✗（形式不同） | 低（3/~21） | 低 | ~25% |
| XPlatformInputContext | QPlatformInputContext | XPlatformInputContext.h | →XObject | →QObject | ✓ | 全（0 缺） | 中高（空后端 + RTL 近似表） | ~85% |
| XPlatformNativeInterface | QPlatformNativeInterface | XPlatformNativeInterface.h | →XObject | →QObject | ✓ | 高（缺 context 2 个） | 高 | ~90% |
| XPlatformAccessibility | QPlatformAccessibility | XPlatformAccessibility.h | →XObject | 无基类 | ✗（X 多 XObject 层） | 中（映射型差异 5 个） | 中 | ~65% |
| XPlatformGraphics（三对象） | QPlatformOpenGLContext / QPlatformOffscreenSurface / QPlatformVulkanInstance | XPlatformGraphics.h | 不透明句柄×3 | 三个独立类 | ✗（合并+形式不同） | 中（核心 5/13） | 中高 | ~70% |
| XPlatformDrag | QPlatformDrag | XPlatformDrag.h | 不透明句柄 | 无基类 | ✗（形式不同） | 低（2/8） | 中（XDND/OLE 在 Drive） | ~55% |

## 二、硬约束合规核查（Platform 模块内逐条）

| # | 硬约束 | 结论 | 说明 |
|---|---|---|---|
| 1 | 拥有型字符串一律 XString*，禁止 char[N]/char* 长期持有；API 主版本用 XString，UTF-8 用 `_2` 后缀 | ❌ **系统性违规** | ① `XPlatformTheme.c:12/22` 用 `char m_name[64]` 长期持有主题名（例外不适用），`XPlatformTheme_name()` 返回借用 `const char*`；② `XPlatformNativeInterface.c:29-41/254-257` 平台函数注册表用 `char* m_name`（XMemory_strdup 分配、XFree_System 释放）长期持有；③ 以下公共 API 以 `const char*` 为主版本、无 XString 主版本、无 `_2` 后缀：`XPlatformInputContext_setLocale`（h:249）、`XPlatformServices_openUrl`（h:22）、`XPlatformFontDatabase_hasFamily`（h:28）、`XPlatformTheme_create_ex`（h:19）、`XPlatformNativeInterface_*` 资源名/函数名/属性名（h:133-286）、`XPlatformWindow_property/setProperty/removeProperty`（h:182-200）。 |
| 2 | 继承一比一含中间基类层 | ❌ 违规（部分） | 仅 XPlatformInputContext / XPlatformNativeInterface 一比一（XObject↔QObject）。XPlatformIntegration / XPlatformAccessibility 多出 XObject 层（Qt 两基类无基类）；XPlatformWindow 缺 QPlatformSurface 中间层；XPlatformBackingStore / Theme / Services / FontDatabase / Drag / Graphics 用不透明句柄替代 Qt 虚函数类（形式不 1:1）。 |
| 3 | 样式/绘制不得精简近似 | ❌ 违规（行为近似） | `XPlatformInputContext.c:32-51` RTL 判定为硬编码 11 语言近似表，缺 ICU/QLocale 全表（ckb/az-Arab/pa-Arab/mzn/nqo 等 40+ RTL 变体）；`setSelectionOnFocusObject` no-op（Qt 实际发送 Selection 属性输入法事件）；`XPlatformIntegration_openGLModuleType` 头注"恒 0"与实现（可用时返回 1）矛盾；`ScreenWindowGrabbing` 能力位未随 XScreen_grabWindow 置位。 |
| 4 | 公共头中文 Doxygen @brief/@param/@return；UTF-8 带 BOM | ❌ 违规 | BOM：`XPlatformBackingStore.h`、`XPlatformDrag.h`、`XPlatformGraphics.h`、`XPlatformIntegration.h`、`XPlatformGraphics.c` 共 4 头 + 1 实现无 UTF-8 BOM（od 实测）。Doxygen：`XPlatformDrag.h` 全部函数、`XPlatformServices.h` 全部函数、`XPlatformFontDatabase.h` 多数函数、`XPlatformAccessibility.h` 多数函数无 `@brief/@param/@return`。 |
| 5 | 信号：空参 args=NULL；Qt 6.8 每信号有 *_signal 宏/回调 | ✅ 合规 | XPlatformNativeInterface 的 `windowPropertyChanged_signal` 齐备（2 参）；XPlatformInputContext 无自有信号，emit 系列（keyboardRectChanged/animatingChanged/visibleChanged/localeChanged/inputDirectionChanged）转发到 XInputMethod 同名 `*_signal`（Input 模块已审），空参转发符合约定。 |
| 6 | 生命周期：init/deinit_base 成对；copy/move 安全；禁 memcpy；禁裸 malloc/free/strdup | ✅ 合规 | 4 个 XObject 类 init/deinit_base 成对；不透明对象 create/destroy 成对；未发现裸 malloc/free/strdup/memcpy 复制对象（XPlatformBackingStore.c:138 仅像素行拷贝，合法）；XPlatformIntegration 图标深拷贝用 XCopy（c:96）。 |
| 7 | 旧 API 不保留，与 Qt 冲突者改名 | ✅ 合规（1 项注意） | 未发现新旧双轨；`XPlatformIntegration_themeName()` 为 Qt 6.8 已删除 API 的便捷扩展（头注已注明"对标 Qt 6.8 之前"），不冲突但需知悉。 |
| 8 | 新代码 C99，无 C++/C11 语法 | ✅ 合规 | 全部 C99（混合声明/复合字面量），无 `_Generic/_Static_assert/_Atomic` 等。 |

## 三、逐类对比

### 3.1 XPlatformIntegration ↔ QPlatformIntegration（gui/kernel/qplatformintegration.h）

**继承链**：X `XPlatformIntegration→XObject`（h:217-221，XCLASS_DEFINE_EXTEND_END(XPlatformIntegration, XObject)）vs Qt `QPlatformIntegration` 无基类（qplatformintegration.h:73）。判定：**不一致**（X 多出 XObject 层；功能上作为可析构对象更安全，但非 1:1）。

**API 缺口**：无。Qt 34 个公开 API（hasCapability / createPlatformPixmap / createPlatformWindow / createForeignWindow / createPlatformBackingStore / createPlatformOpenGLContext / createPlatformSharedGraphicsCache / createImagePaintEngine / createEventDispatcher / initialize / destroy / fontDatabase / clipboard / drag / inputContext / accessibility / nativeInterface / services / styleHint / defaultWindowState / queryKeyboardModifiers / possibleKeys / keyMapper / themeNames / createPlatformTheme / createPlatformOffscreenSurface / createPlatformSessionManager / sync / openGLModuleType / setApplicationIcon / setApplicationBadge / beep / quit / createPlatformVulkanInstance）全部有对应入口。Capability 24 项、StyleHint 27 项枚举值与 Qt 顺序逐一一致（h:146-210）。

**命名/签名差异**：

| Qt 原型 | X | 判定 |
|---|---|---|
| `virtual void beep() const` | `bool XPlatformIntegration_beep(...)` 恒 false | ❌ 返回类型不一致（Qt 无返回值；X 恒 false 语义=失败） |
| `QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context)` | `void* ...(void* context)`，解释为 `XWindow*` | ⚠️ 参数语义不同（Qt 传 QOpenGLContext，X 传 XWindow；头注自认"兼容既有 void* 工厂签名"） |
| `QStringList themeNames() const` | `XVector*`（元素为借用 XString*） | ⚠️ 值语义→借用语义 |
| `QPlatformPixmap *createPlatformPixmap(PixelType)` | `void*` 恒 NULL | ⚠️ Qt 默认创建栅格 pixmap（qplatformintegration.cpp），X 由 XPixmap 进程内承载（等效但工厂恒 NULL） |
| `QList<int> possibleKeys(const QKeyEvent*)` | `void*` 恒 NULL | ⚠️ Qt 默认返回空列表，X 返回 NULL |
| `QOpenGLContext::OpenGLModuleType openGLModuleType()` | `int`，可用时返回 1 | ❌ 头注（h:567-572）称"恒返回 0"，实现（c:752-756）返回 `isOpenGLAvailable()?1:0`；Qt 枚举 LibGL=0/LibGLES=1，X 的 1 语义错误且文档自相矛盾 |

**功能缺口**：

| # | 缺口 | 位置 | 说明 |
|---|---|---|---|
| F1 | ScreenWindowGrabbing 能力位未置位 | c:188-220 | XScreen_grabWindow 在 X11/Win32 真实可用（Window 模块审计），但能力位默认集不含 ScreenWindowGrabbing，与 Qt 6.8 默认 hasCapability 返回 true 不符 |
| F2 | openGLModuleType 实现/文档矛盾 | h:567-572 vs c:752-756 | 头注"恒 0"、实现返回 1（应返回 Qt LibGL=0）；文档与代码不一致 |
| F3 | 默认能力位集合与 Qt 6.8 基类不同 | c:188-205 | Qt 6.8 默认 true：NonFullScreen/WindowManagement/WindowActivation/NativeWidgets/TopStackedNativeChildWindows/RhiBasedRendering/ScreenWindowGrabbing；X 另开 ThreadedPixmaps/WindowMasks/MultipleWindows/ApplicationState/SyncState/ApplicationIcon/PaintEvents，未开 NativeWidgets/TopStackedNativeChildWindows（嵌入式设计，需文档化） |
| F4 | beep 恒 false | c:778-783 | 嵌入式无提示音（头注声明），但 Qt beep 为 void 默认空实现，签名不一致 |
| F5 | clipboard 需注入 | c:462-473 | Qt 默认懒建静态 QPlatformClipboard；X 依赖 XGuiApplication 注入，未注入时返回 NULL |
| F6 | createPlatformOffscreenSurface 恒尝试创建 1×1 | c:730-735 | Qt 默认返回 nullptr；X 恒走 XPlatformOffscreenSurface_create（无驱动时回落 NULL，行为尚可但语义不同） |

**违规**：① 头无 BOM（约束 4）；② `openGLModuleType` 头注与实现矛盾（约束 3 近似）；③ 继承多 XObject 层（约束 2）；④ 字符串 API 无 XString 主版本问题见模块级（themeNames 借用属映射差异）。

### 3.2 XPlatformWindow ↔ QPlatformWindow（gui/kernel/qplatformwindow.h）

**继承链**：X `XPlatformWindow→XObject`（h:60-64）vs Qt `QPlatformWindow : public QPlatformSurface`（qplatformwindow.h:36，QPlatformSurface 为无基类接口类，qplatformsurface.h:29，持 QSurface*）。判定：**不一致**（X 缺 QPlatformSurface 中间层且多出 XObject 层；format()/screen() 未实现）。

**API 缺口（Qt 公开 API 中 X 缺失；X 头注自称"轻量子集"）**：

| Qt 原型（qplatformwindow.h） | X 状态 | 说明 |
|---|---|---|
| `QPlatformWindow *parent() const` / `QPlatformScreen *screen() const` / `QSurfaceFormat format() const` | ❌ | 由 XWindow/XScreen 公共层承担，平台句柄层无对应 |
| `QRect normalGeometry() / QMargins frameMargins() / QMargins safeAreaMargins() const` | ❌ | 嵌入式恒缺（XWindow.frameMargins 恒 0，Window 审计 F5） |
| `void setWindowFlags(Qt::WindowFlags)` / `setWindowState(Qt::WindowStates)` / `setParent(const QPlatformWindow*)` | ❌ | XWindow 承担（flags/state 在公共层） |
| `void setWindowTitle / setWindowFilePath / setWindowIcon` | ❌ | XPlatformNativeWindow_setTitle 承担标题；文件路径/图标缺 |
| `bool close() / void raise() / void lower()` | ❌ | XWindow 承担（close 语义偏差见 Window 审计 F2） |
| `bool isExposed() / isActive() / isAncestorOf() / isEmbedded() const` | ❌ | 平台句柄层未实现（XWindow 有 isVisible/部分状态） |
| `QPoint mapToGlobal / mapFromGlobal（+F 变体）` | ❌ | XWindow 有整型/浮点变体，平台句柄层未转发 |
| `void propagateSizeHints() / setOpacity(qreal) / setMask(const QRegion&) / devicePixelRatio()` | ❌ | XWindow 承担（opacity/mask 见 Window 审计） |
| `void handleContentOrientationChange / setWindowModified / windowEvent / invalidateSurface` | ❌ | 未实现 |
| `bool startSystemResize(Qt::Edges) / startSystemMove()` | ❌ | XWindow 有占位实现（Window 审计 F6） |
| `setFrameStrutEventsEnabled / frameStrutEventsEnabled / setAlertState / isAlertState` | ❌ | 未实现 |
| `static QRect initialGeometry(...)` / `requestUpdate / hasPendingUpdateRequest / deliverUpdateRequest` | ❌ | XWindow requestUpdate 仅置标志（Window 审计 F8） |
| `QSize windowMinimumSize/MaximumSize/BaseSize/SizeIncrement / windowGeometry / windowFrameGeometry / windowClosestAcceptableGeometry` | ❌ | 未实现 |

**签名差异（已实现部分）**：
- `requestActivateWindow()` → `XPlatformWindow_requestActivate`：X 转发 `XGuiApplication_setFocusWindow`；Qt 默认发 `QWindowSystemInterface::handleFocusWindowChanged`（qplatformwindow.cpp:382-384）——等效但链路不同。
- `winId()` → `XPlatformWindow_winId`：X 无平台时回退自增 ID（嵌入式声明，Qt 返回 1）。
- `isForeignWindow()` → `isForeign` + `setForeign`：✓ 等价。
- `handle()`（64 位自增 ID）：X 扩展（对标 NativeHandle，Qt 6.8 无公开 API）。
- `properties/property/setProperty/removeProperty`：X 扩展（对标 QPlatformNativeInterface::windowProperties 系列，见 3.9）。

**功能缺口**：① isExposed/isActive/mapToGlobal(F)/mapFromGlobal(F) 无平台句柄转发（部分 XWindow 承担）；② normalGeometry/frameMargins/safeAreaMargins 恒缺；③ requestActivate 焦点系统链路与 Qt WSI 不同。

**违规**：继承缺 QPlatformSurface 层（约束 2）；"轻量子集"范围未在头注列出完整缺口清单（文档 P2）。

### 3.3 XPlatformNativeWindow ↔ QPA 内部实现（无直接 Qt 头）

**说明**：XPlatformNativeWindow 是 XWindow 与 Drive 原生后端之间的纯函数契约（h:1-38），对应 Qt QPA 插件内部实现类（QXcbWindow/QWindowsWindow 等）的职责，Qt 侧无公共头可比对。按任务要求简述：

- 对齐的角色/关键虚函数名：`setVisible↔setVisible`、`setGeometry↔setGeometry`、`setTitle↔setWindowTitle`、`winId↔winId`、`requestActivate↔requestActivateWindow`、`setKeyboardGrabEnabled/setMouseGrabEnabled↔同名`、`grabWindow↔QPlatformScreen::grabWindow`（Qt 在屏对象而非窗口对象）。
- X 扩展：`attachForeign/createForeign`（对标 QPlatformIntegration::createForeignWindow 职责）、`windowForWinId` 反查表、`processPendingEvents/waitForEvents` 事件泵（Qt 走 QAbstractEventDispatcher + QWindowSystemInterface）、`present()` 上屏（对标 linuxfb/mir 平台后端 flush 职责）、`nativeConnection`（display/HINSTANCE 资源查询）、`queryKeyboardModifiers`（Qt 在 QPlatformKeyMapper 层）。
- 功能：真实 X11/Win32 实现在 `Drive/Posix/Graphics/XPlatformNativeWindow_posix.c`、`Drive/windows/Graphics/XPlatformNativeWindow_win32.c`（含 XDND、XGetImage 抓屏、DIB/BitBlt 上屏），本审计未逐行审 Drive；公共契约无占位。
- 约束：头文件 BOM ✓；Doxygen 完整 ✓；无字符串违规（setTitle 用 XString* ✓）。

### 3.4 XPlatformBackingStore ↔ QPlatformBackingStore（gui/painting/qplatformbackingstore.h）

**继承链**：X 为不透明句柄（h:52，具体结构只在 .c），Qt 为无基类虚函数类。判定：**形式不一致**（纯函数契约 vs 虚函数接口；软件核心在公共层、平台差异收敛为 6 个 Driver 钩子，属 X 特有架构）。

**API 缺口**：

| Qt 原型 | X 状态 | 说明 |
|---|---|---|
| `QBackingStore *backingStore() const` | ❌ | X 无 QBackingStore 等价句柄（XBackingStore 在 Graphics 模块，未回链） |
| `FlushResult rhiFlush(QWindow*, qreal, const QRegion&, const QPoint&, QPlatformTextureList*, bool)` | ❌ | 无 RHI 层 |
| `QRhiTexture *toTexture(QRhiResourceUpdateBatch*, const QRegion&, TextureFlags*) const` | ❌ | 无 RHI 层 |
| `QPlatformGraphicsBuffer *graphicsBuffer() const` | ❌ | QPlatformGraphicsBuffer 完全缺失（见四） |
| `void resize(const QSize &size, const QRegion &staticContents)` | ⚠️ 拆分 | X：`resize(size)` + `setStaticContents(region)`（h:168/208，头注注明收敛语义） |
| `void createRhi(QWindow*, QPlatformBackingStoreRhiConfig)` / `QRhi *rhi(QWindow*) const` / `surfaceAboutToBeDestroyed()` / `graphicsDeviceReportedLost(QWindow*)` | ❌ | 无 RHI 层 |
| `FlushResult` / `TextureFlag` 枚举、QPlatformTextureList、QPlatformBackingStoreRhiConfig | ❌ | 无 RHI 层 |

**已实现（对齐）**：window ✓、paintDevice ✓（返回内部 XImage）、flush(window, region, offset) ✓、toImage ✓（深拷贝）、scroll(area, dx, dy) ✓、beginPaint/endPaint ✓、静态内容三件套（Qt 在 QBackingStore 层，X 上移）。

**功能缺口**：
1. scroll 用整幅快照回拷（c:669-687）——结果正确但性能近似；Qt 基类默认返回 false、由后端自实现，无基准冲突。
2. FULL 模式恒整屏提交（c:444-451）——裁剪宏回退，属声明行为。
3. `setBuffers` 外部缓冲登记后 `resize` 不能超过缓冲容量（c:534-536），Qt 无此限制（嵌入式裁剪约束，头注已注明）。

**违规**：头无 BOM（约束 4）；继承形式不 1:1（约束 2，架构声明）。

### 3.5 XPlatformTheme ↔ QPlatformTheme（gui/kernel/qplatformtheme.h）

**继承链**：X 不透明快照（c:12 `char m_name[64]; bool m_dark`），Qt 为 Q_GADGET 类。判定：**形式不一致**。

**API 缺口（Qt 公开 API 中 X 缺失）**：

| Qt 原型 | 说明 |
|---|---|
| `QPlatformMenuItem *createPlatformMenuItem() / QPlatformMenu *createPlatformMenu() / QPlatformMenuBar *createPlatformMenuBar() / void showPlatformMenuBar()` | 无原生菜单体系 |
| `bool usePlatformNativeDialog(DialogType) / QPlatformDialogHelper *createPlatformDialogHelper(DialogType)` | 无原生对话框 |
| `QPlatformSystemTrayIcon *createPlatformSystemTrayIcon()` | 无托盘 |
| `Qt::ColorScheme colorScheme() const` | 缺（X 仅有 isDark 布尔） |
| `const QPalette *palette(Palette = SystemPalette) const` | 缺（XPalette 在 Style 模块，未挂钩） |
| `const QFont *font(Font = SystemFont) const` | 缺 |
| `QVariant themeHint(ThemeHint) const` | 缺（XPlatformIntegration_styleHint 只覆盖 StyleHint 子集） |
| `QPixmap standardPixmap(StandardPixmap, const QSizeF&) / QIcon fileIcon(...) / QIconEngine *createIconEngine(...)` | 缺（XIcon 在 Icon 模块） |
| `QList<QKeySequence> keyBindings(...) / standardButtonText / standardButtonShortcut / requestColorScheme` | 缺 |
| 静态 `defaultThemeHint / defaultStandardButtonText / removeMnemonics` | 缺 |
| 8 个枚举（ThemeHint 47 项 / Palette 18 / Font 25 / StandardPixmap / DialogType / KeyboardSchemes / UiEffect / IconOption） | 缺 |

**功能缺口**：① 仅 name/isDark 快照，Driver_detect 探测深色偏好（c:21）；② `XPlatformThemeDriver_iconSearchPaths / iconThemeName` 已接 XDG 平台路径（h:26-40，XGui.md §10.392），但公共层无对应查询 API（调用方在 XIcon）；③ 无 colorScheme/palette/font/themeHint 快照，未来样式层（XGui.md §12450 建议）依赖此扩展。

**违规**：① `char m_name[64]` 长期持有拥有型字符串（约束 1 硬违规，XPlatformTheme.c:12/22）；② `create_ex` 主版本为 `const char*`（约束 1）；③ 头文件部分函数缺逐参 Doxygen（约束 4）。

### 3.6 XPlatformServices ↔ QPlatformServices（gui/kernel/qplatformservices.h）

**继承链**：X 不透明对象 vs Qt 无基类。判定：**形式不一致**。

**API 缺口**：

| Qt 原型 | X 状态 |
|---|---|
| `bool openUrl(const QUrl &url)` | ⚠️ 有（`const char*` 主版本，无 XString 主版/`_2`） |
| `bool openDocument(const QUrl &url)` | ❌ |
| `QByteArray desktopEnvironment() const` | ❌ |
| `bool hasCapability(Capability capability) const`（ColorPicking） | ❌ |
| `QPlatformServiceColorPicker *colorPicker(QWindow *parent = nullptr)` | ❌（含 QPlatformServiceColorPicker 类缺失） |

**功能缺口**：① 嵌入式无桌面服务时 `isAvailable=false`、openUrl 恒 false（声明，c:16/23）；② 真实实现（xdg-open/ShellExecute）在 Drive，本模块未审。**违规**：openUrl 字符串主版本（约束 1）；全部函数无 Doxygen（约束 4）。

### 3.7 XPlatformFontDatabase ↔ QPlatformFontDatabase（gui/text/qplatformfontdatabase.h）

**继承链**：X 不透明对象 vs Qt 无基类。判定：**形式不一致**。

**API 缺口（Qt 公开 API 中 X 缺失）**：

| Qt 原型 | 说明 |
|---|---|
| `void populateFontDatabase() / populateFamilyAliases(const QString&) / populateFamily(const QString&) / invalidate()` | 家族填充/失效机制 |
| `QStringList fallbacksForFamily(...) const` | 字体回退 |
| `QStringList addApplicationFont(const QByteArray&, const QString&, ...)` | 应用字体 |
| `QFontEngine *fontEngine(const QFontDef&, void*) / fontEngine(const QByteArray&, qreal, HintingPreference) / fontEngineMulti(...)` | 字体引擎（XPainter 自有装载路径，未走 QPA） |
| `QString fontDir() / QFont defaultFont() / isPrivateFontFamily / resolveFontFamilyAlias / fontsAlwaysScalable / standardSizes / supportsVariableApplicationFonts` | 全部缺失 |
| 静态 `writingSystemsFromTrueTypeBits / writingSystemsFromOS2Table / registerFont / registerFontFamily / registerAliasToFontFamily / repopulateFontDatabase / isFamilyPopulated` | 全部缺失 |
| `QSupportedWritingSystems` 类 | 缺失 |

**功能缺口**：① 仅保存家族快照（Driver_collect），无引擎/回退/别名解析——嵌入式最小设计（头注声明）；② `families()` 返回深拷贝列表（h:27），Qt 无直接对应（Qt 通过 populate 回调注册）。**违规**：`hasFamily` 主版本 const char*（约束 1）；多数函数缺逐参 Doxygen（约束 4）。

### 3.8 XPlatformInputContext ↔ QPlatformInputContext（gui/kernel/qplatforminputcontext.h）

**继承链**：X `XPlatformInputContext→XObject`（h:60-64）vs Qt `QPlatformInputContext : public QObject`（h:23）→ **一致 ✓**（虚表无新增槽位，继承 XObject 无新增虚函数）。

**API 缺口**：无。Qt 全部公开 API（isValid/hasCapability/reset/commit/update/invokeAction/filterEvent/keyboardRect/emitKeyboardRectChanged/isAnimating/emitAnimatingChanged/showInputPanel/hideInputPanel/isInputPanelVisible/emitInputPanelVisibleChanged/locale/emitLocaleChanged/inputDirection/emitInputDirectionChanged/setFocusObject/inputMethodAccepted + 6 个静态辅助）均有对应，能力位 HiddenTextCapability=0x1 一致（h:53）。

**签名差异**：

| Qt | X | 判定 |
|---|---|---|
| `QLocale locale() const` | `XString* XPlatformInputContext_locale()`（IETF 标签） | ⚠️ Qt 返回完整 QLocale 对象，X 用标签串 + 方向枚举拆分（合理裁剪，但字符串主版本问题见违规） |
| `void update(Qt::InputMethodQueries)` | `update(XInputMethodQueries)` | ✓ 等价（uint32_t 位集） |
| `bool filterEvent(const QEvent *event)` | `filterEvent(const XEvent*)` | ✓ 等价（XEvent 为事件基类） |
| `static QRectF keyboardRectangle()` | `XPlatformInputContext_keyboardRectangle_static()` | ✓ 改名避免与成员 keyboardRect 冲突 |
| `emitInputDirectionChanged(Qt::LayoutDirection)` | `emitInputDirectionChanged(XInputMethodLayoutDirection)` | ✓ 等价 |

**功能缺口**：

| # | 缺口 | 位置 | 说明 |
|---|---|---|---|
| F1 | RTL 判定为硬编码近似表 | c:32-51 | 仅 ar/fa/he/iw/ur/ps/dv/yi/sd/ug/ku-Arab 11 项；Qt `QLocale::textDirection()` 基于 ICU 全表（ckb/az-Arab/pa-Arab/mzn/nqo/sd 等 40+ 变体缺失）→ 约束 3 违规 |
| F2 | isValid 恒 true、locale 默认 "C" | c:188-193 / c:171 | Qt 默认 isValid=false、locale=QLocale::system()；X 为嵌入式内置后端声明（头注已注明），但非 1:1 |
| F3 | setSelectionOnFocusObject no-op | c:437-442 | Qt 实际查询焦点对象光标位置并发送含 Selection 属性的 QInputMethodEvent（qplatforminputcontext.cpp:254-278） |
| F4 | showInputPanel/hideInputPanel 自动发射 visibleChanged | c:300-314 | Qt 基类为 no-op（信号由真实后端在状态变化时发）；X 空后端自发射属行为偏差（已声明） |
| F5 | inputMethodAccepted 实例级存储 | c:63/415-426 | Qt 为进程级静态 `s_inputMethodAccepted`；X 每实例独立（更合理但语义不同） |
| F6 | emitInputDirectionChanged 不防重 | c:390-397 | Qt 同值直接返回（qplatforminputcontext.cpp:216-226）；X 的调用路径 setInputDirection 已防重，公开 emit 无保护（次要） |

**违规**：① `setLocale` 主版本 const char*、无 `_2` 后缀（约束 1）；② F1 RTL 近似表（约束 3）。其余为嵌入式声明差异。

### 3.9 XPlatformNativeInterface ↔ QPlatformNativeInterface（gui/kernel/qplatformnativeinterface.h）

**继承链**：X `→XObject` vs Qt `: public QObject` → **一致 ✓**。

**API 缺口**：

| Qt 原型 | X 状态 |
|---|---|
| `void *nativeResourceForContext(const QByteArray &resource, QOpenGLContext *context)` | ❌ |
| `NativeResourceForContextFunction nativeResourceFunctionForContext(const QByteArray &resource)` | ❌ |

**签名/语义差异**：
- 资源名：Qt `QByteArray` vs X `const char*`（UTF-8 精确匹配，h:134/143）——⚠️ 字符串主版本问题（约束 1）。
- `windowProperties(QPlatformWindow*)`：Qt 值返回 `QVariantMap`；X 返回内部 `XVariantHashMap*` 借用指针（c:263-269）——语义差异（借用 vs 拷贝）。
- `setWindowProperty`：X 写入后**自发射** windowPropertyChanged（c:303-314）；Qt 基类 setWindowProperty 为 no-op 且 qtbase 内无任何发射者（qplatformnativeinterface.cpp:106-111，信号由平台插件在系统属性变化时发）——行为偏差。
- `registerPlatformFunction`（注册/覆盖/注销）：X 扩展（Qt 无公开注册入口）。
- `nativeResourceFunctionForCursor`：X 扩展（Qt 6.8 无此接口，只有 nativeResourceForCursor）。
- `integration()` getter：X 扩展。

**功能缺口**：① context 资源查询缺失（QOpenGLContext 原生资源无法获取）；② 平台函数注册表固定 32 项上限（c:27/235-259），且键为 `char*` 长期持有（约束 1 违规，XPlatformNativeInterface.c:29-41/254-257）。**违规**：① 注册表 char*（约束 1）；② 资源名/属性名 const char* 主版本（约束 1）；③ 头注"对标全部公共 API"不实（缺 2 个 context API，h:4）。

### 3.10 XPlatformAccessibility ↔ QPlatformAccessibility（gui/accessible/qplatformaccessibility.h）

**继承链**：X `XPlatformAccessibility→XObject`（h:35-36）vs Qt `QPlatformAccessibility` **无基类**（h:25）→ **不一致**（X 多 XObject 层）。

**API 缺口（Qt 公开 API vs X 映射）**：

| Qt 原型 | X 状态 |
|---|---|
| `virtual void notifyAccessibilityUpdate(QAccessibleEvent *event)` | ⚠️ 映射为 `notify(event, accessible)`（拆分事件+对象，XAccessibleEvent 为 6 项自定枚举） |
| `virtual void setRootObject(QObject *o)` | ⚠️ 语义不同：X 在 create_ex 自建并拥有 `m_root`（c:45），Qt 接收外部 QObject 根 |
| `virtual void initialize()` | ⚠️ 由 `XPlatformAccessibilityDriver_start` 承担（c:46-47） |
| `virtual void cleanup()` | ⚠️ 由 `XPlatformAccessibilityDriver_stop` 承担（c:17-20） |
| `inline bool isActive() const` | ✓ `isActive()`（c:55-57） |
| `void setActive(bool active)` | ❌ 无公开 setActive（m_active 仅内部+Driver 反馈更新） |

**功能缺口**：① XAccessibleEvent 仅 6 种（ObjectCreated/ObjectDestroyed/NameChanged/DescriptionChanged/LocationChanged/StateChanged），Qt QAccessibleEvent::Type 数十种（ValueChanged/Focus/Help/Text/ChildAdded 等）——桥接信息量大幅裁剪；② notifyWindow/notifyWidget 全局入口依赖 XWindow/XWidget 生命周期调用（Window/Widget 模块已接），Qt 由 QAccessible 事件源驱动。**违规**：继承多 XObject 层（约束 2）；多数函数缺逐参 Doxygen（约束 4）。

### 3.11 XPlatformGraphics（三对象）↔ QPlatformOpenGLContext / QPlatformOffscreenSurface / QPlatformVulkanInstance（gui/kernel/qplatformopenglcontext.h、gui/kernel/qplatformoffscreensurface.h、gui/vulkan/qplatformvulkaninstance.h）

**说明**：X 把 OpenGL 上下文、Vulkan 实例、离屏表面三个不透明句柄合并到一个头（h:26-31），Qt 为三个独立类。任务基准中的 `qplatformgraphicsbuffer.h` 无 X 对应（见四）。

**API 对齐（以 QPlatformOpenGLContext 为例）**：X 覆盖 create/destroy/makeCurrent/doneCurrent/swapBuffers/getProcAddress/isValid；缺 `format() / isSharing() / isOpenGLES() / defaultFramebufferObject() / window() / shareContext() / initialized()` 等。QPlatformVulkanInstance（gui/vulkan/qplatformvulkaninstance.h：supportedLayers/supportedExtensions/supportedApiVersion/createOrAdoptInstance/vkInstance/getInstanceProcAddr/supportsPresent 等）X 仅提供 physicalDeviceCount/apiVersion 快照（部分对标 supportedApiVersion），缺 vkInstance/getInstanceProcAddr/supportsPresent 等。离屏表面 X 提供 width/height/isValid/makeCurrent/doneCurrent/getProcAddress（X 扩展；Qt QPlatformOffscreenSurface 仅 offscreenSurface/screen/format/isValid，无 makeCurrent——那是 QPlatformOpenGLContext 职责，X 把 GL 会话职责合并进离屏对象）。

**功能缺口**：① 离屏 getProcAddress 复用全局 GLX/WGL 查询（c:206-213），非严格上下文绑定（头注声明）；② 无 QPlatformGraphicsBuffer（锁/纹理绑定/数据访问链路）。**违规**：无硬约束违规（无字符串/继承/BOM——BOM ✓，Doxygen ✓；`XPlatformGraphics.c` 无 BOM 属约束 4 违规）。

### 3.12 XPlatformDrag ↔ QPlatformDrag（gui/kernel/qplatformdrag.h）

**继承链**：X 不透明句柄 vs Qt 无基类。判定：**形式不一致**。

**API 缺口**：

| Qt 原型 | X 状态 |
|---|---|
| `QDrag *currentDrag() const` | ❌ |
| `Qt::DropAction drag(QDrag *m_drag) = 0` | ⚠️ 映射为 `XPlatformDrag_exec(self, source, data, actions)`（展开 QDrag 载荷，签名结构不同） |
| `void cancelDrag()` | ❌ |
| `void updateAction(Qt::DropAction action)` | ❌ |
| `Qt::DropAction defaultAction(Qt::DropActions, Qt::KeyboardModifiers) const` | ❌ |
| `static QPixmap defaultPixmap()` | ❌（无拖拽图标） |
| `bool ownsDragObject() const` | ❌ |
| `QPlatformDropQtResponse / QPlatformDragQtResponse` | ❌（accept 回复链对象缺失） |

**命名/数值不一致**：`XPlatformDragAction`（Copy=1/Move=2/Link=4）与 Qt::DropAction 一致；但 `XPlatformDragResult`（h:32-39）数值与 Qt::DropAction **冲突**：X `Cancelled=1` == Qt `CopyAction=1`、`Copied=2` == Qt `MoveAction=2`、`Moved=3` 无 Qt 对应值、`Linked=4` == Qt `LinkAction=4`（Qt 另有无对应 0 值 IgnoreAction）。任何按 Qt 数值解释结果的上层代码都会把 Copy/Move 语义读反。

**功能缺口**：① Result 枚举数值冲突（上）；② 无 defaultPixmap/拖拽图标；③ 真实 XDND/OLE 实现位于 Drive（Posix X11 同步 XDND 会话，c:2013+），公共契约无占位。**违规**：全部函数无 Doxygen（约束 4）；头无 BOM（约束 4）；Result 数值与 Qt 冲突（约束 3 近似）。

## 四、缺失 Qt 类清单（Platform 对应范围）

| Qt 类 | Qt 头文件（相对 qtbase/src/gui/） | 建议 |
|---|---|---|
| QPlatformScreen | kernel/qplatformscreen.h | **暂不实现**：XScreen 同时承担 QScreen 与 QPlatformScreen 双重角色（Window 模块审计）；做真 QPA 插件移植时需拆出 XPlatformScreen（geometry/availableGeometry/depth/format/physicalSize/logicalDpi/refreshRate/virtualSiblings/grabWindow） |
| QPlatformCursor | kernel/qplatformcursor.h | **暂不实现**：XCursor 公共层已存在（Input 模块），无平台光标句柄需求 |
| QPlatformClipboard | kernel/qplatformclipboard.h | **暂不实现**：XClipboard 公共层已存在（Input 模块），XPlatformIntegration_clipboard 以注入方式对接 |
| QPlatformKeyMapper | kernel/qplatformkeymapper.h | **建议实现最小版**：`XPlatformIntegration_keyMapper()` 恒 NULL、possibleKeys 恒 NULL，键盘码表/按键候选缺失 |
| QPlatformGraphicsBuffer | kernel/qplatformgraphicsbuffer.h | **建议暂不实现**：软件渲染无纹理访问需求；引入 RHI/GPU 合成时再补（data/bytesPerLine/origin/lock/bindToTexture） |
| QPlatformTextureList | painting/qplatformbackingstore.h | **不实现**：无 RHI 合成路径 |
| QPlatformBackingStoreRhiConfig | painting/qplatformbackingstore.h | **不实现**：无 RHI |
| QPlatformMenu | kernel/qplatformmenu.h | **暂不实现**：无原生菜单体系（Widget 层有 XMenu 时再补） |
| QPlatformMenuItem | kernel/qplatformmenu.h | 同上 |
| QPlatformMenuBar | kernel/qplatformmenu.h | 同上 |
| QPlatformDialogHelper | kernel/qplatformdialoghelper.h | **暂不实现**：无原生对话框 |
| QPlatformSystemTrayIcon | kernel/qplatformsystemtrayicon.h | **暂不实现**：嵌入式无托盘 |
| QPlatformSessionManager | kernel/qplatformsessionmanager.h | **暂不实现**：createPlatformSessionManager 恒 NULL 已声明 |
| QPlatformSharedGraphicsCache | kernel/qplatformsharedgraphicscache.h | **暂不实现**：createPlatformSharedGraphicsCache 恒 NULL 已声明 |
| QPlatformSurface | kernel/qplatformsurface.h | **不实现**：职责已并入 XWindow/XPlatformWindow（继承偏差已在 3.2 标注） |
| QSupportedWritingSystems | text/qplatformfontdatabase.h | **暂不实现**：字体快照足够；全字体引擎（QFontEngine 等价）时再补 |
| QPlatformIntegrationPlugin | kernel/qplatformintegrationplugin.h | **不实现**：XinYueC 为单内嵌后端，无插件工厂体系 |
| QPlatformThemePlugin | kernel/qplatformthemeplugin.h | 同上 |
| QPlatformInputContextPlugin | kernel/qplatforminputcontextplugin.h | 同上 |
| QPlatformServiceColorPicker | kernel/qplatformservices.h | **暂不实现**：无取色器（XPlatformServices 缺 colorPicker 入口） |

## 五、优先任务建议（按优先级）

1. **P0 修复 Platform 模块系统性字符串约束违规**：`XPlatformInputContext_setLocale / XPlatformServices_openUrl / XPlatformFontDatabase_hasFamily / XPlatformTheme_create_ex` 补 XString* 主版本 + `_2(const char*)` 重载；`XPlatformNativeInterface_*` 与 `XPlatformWindow_property/setProperty/removeProperty` 的资源名/属性名同样处理；`XPlatformTheme.m_name char[64]` 改 XString*；`XPlatformNativeInterface` 注册表 `char* m_name` 改 XString*（XPlatformTheme.c:12/22、XPlatformNativeInterface.c:29-41/254-257）。
2. **P0 补 UTF-8 BOM**：`XPlatformBackingStore.h`、`XPlatformDrag.h`、`XPlatformGraphics.h`、`XPlatformIntegration.h`、`XPlatformGraphics.c`（约束 4 硬违规）。
3. **P0 补公共头 Doxygen**：`XPlatformDrag.h`、`XPlatformServices.h` 全部函数、`XPlatformFontDatabase.h`、`XPlatformAccessibility.h` 多数函数补 `@brief/@param/@return`（约束 4）。
4. **P1 修正 XPlatformInputContext RTL 判定**：替换硬编码 11 语言表为完整文本方向表（或复用 XLocale 的 textDirection 语义），消除与 QLocale::textDirection 的行为差距（约束 3）；`setSelectionOnFocusObject` 按 Qt 实现 Selection 属性事件而非 no-op。
5. **P1 修正 XPlatformIntegration_openGLModuleType**：头注与实现统一（Qt 语义 LibGL=0 时返回 0，可用 GLX 时返回 0；现实现返回 1=LibGLES 语义错误）；`ScreenWindowGrabbing` 能力位随 XScreen_grabWindow 可用性置位；beep 返回值按 Qt void 调整或文档化 bool 扩展。
6. **P1 修正 XPlatformDragResult 数值冲突**：`Cancelled=1` 与 Qt CopyAction=1 冲突——改为与 Qt::DropAction 数值一致（IgnoreAction=0/Copy=1/Move=2/Link=4）或新增独立结果码并文档化，避免互操作误读。
7. **P1 补齐 XPlatformNativeInterface context 资源查询**：`nativeResourceForContext / nativeResourceFunctionForContext`（QOpenGLContext 原生句柄/函数解析），并修正头注"全部公共 API"表述。
8. **P2 XPlatformWindow 缺口清单化**：以头注/文档形式列出"轻量子集"与 XWindow/XPlatformNativeWindow 的职责边界（isExposed/isActive/mapToGlobal(F)/frameMargins 等由谁承担），避免下游误用。
9. **P2 XPlatformTheme 扩展**：补 colorScheme/palette/font/themeHint 最小快照（支撑 XGui.md §12450 建议的样式层）；XPlatformFontDatabase 补 defaultFont/standardSizes 最小集。
10. **P2 XPlatformAccessibility 对齐**：补 `setActive(bool)`（含 QAccessible::setActive 等价全局态）与 initialize/cleanup 显式入口，或文档化 Driver_start/stop 映射；XAccessibleEvent 枚举按需扩充（ValueChanged/Focus）。

（报告完）
