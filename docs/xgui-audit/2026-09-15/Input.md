# XGui ↔ Qt 6.8.3 对齐审计报告：Input 模块

- 审计日期：2026-09-15
- 审计范围：`Src/XGui/Input/` 全部公开头文件与实现（只读审计，未修改任何 Src/Test 源码，未 commit/push）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/accessible/qaccessible.h` + `qtbase/src/gui/kernel`（qclipboard.h qcursor.h qinputmethod.h）+ `qtbase/src/corelib/kernel/qmimedata.h`
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、《XGui.md》相关章节（10.389、10.477 等）

---

## 一、模块概览表

| X 类 | Qt 对应类 | Qt 头文件 | X 继承链 | Qt 继承链 | 继承匹配 | API 缺口 | 功能缺口 | 完成度 |
|---|---|---|---|---|---|---|---|---|
| XAccessible | QAccessibleInterface | gui/accessible/qaccessible.h | XAccessible→XObject | QAccessibleInterface（无基类纯抽象接口） | 是*（X 多一层 XObject，合理扩展） | 12 | 4 | 55 |
| XClipboard | QClipboard | gui/kernel/qclipboard.h | XClipboard→XObject | QClipboard→QObject | 是 | 0 | 3 | 90 |
| XCursor | QCursor | gui/kernel/qcursor.h | XCursor→XObject | QCursor（无基类，隐式共享值类型） | 是*（X 多一层 XObject） | 5 | 2 | 85 |
| XInputMethod | QInputMethod | gui/kernel/qinputmethod.h | XInputMethod→XObject | QInputMethod→QObject | 是 | 0 | 3 | 80 |
| XMimeData | QMimeData | corelib/kernel/qmimedata.h | XMimeData→XObject | QMimeData→QObject | 是 | 2 | 4 | 78 |

> \* 与 Graphics 审计中 XBackingStore→XObject 判例一致：Qt 侧无基类时，X 侧增加 XObject 对象机制层判为“合理扩展”，不构成缺失中间基类。本模块不存在“缺中间基类”类问题（区别于 Graphics 模块缺 XPaintDevice）。

---

## 二、逐类对比

### 2.1 XAccessible ↔ QAccessibleInterface（qtbase/src/gui/accessible/qaccessible.h）

**继承**
- X：`XAccessible { XObject m_class; ... }`，`XCLASS_DEFINE_EXTEND_END(XAccessible, XObject)`（XAccessible.h:39-44）
- Qt：`class QAccessibleInterface`（纯抽象接口，无基类）
- 判定：匹配（X 增加 XObject 层，C 对象模型需要）。X 是“可访问树节点数据模型”，Qt 是“可访问接口适配层”，语义定位不同但节点树遍历职责对应。

**API 缺口（Qt 原型 → X 现状）**

| Qt 6.8 API | X 现状 | 说明 |
|---|---|---|
| `virtual QObject *object() const = 0` | 无 XAccessible_object | 缺失（有 window()/widget() 替代） |
| `relations(QAccessible::Relation) const` | 无 | 缺失 |
| `virtual QAccessibleInterface *focusChild() const` | 无 | 缺失 |
| `virtual QAccessibleInterface *childAt(int x, int y) const = 0` | 无 | 缺失（只有 childAtIndex） |
| `virtual int indexOfChild(const QAccessibleInterface*) const = 0` | 无 | 缺失 |
| `virtual QString text(QAccessible::Text) const = 0` / `setText(Text, QString)` | 仅 name/description 两个固定槽 | 缺 Qt::Text 参数化（Value/Help/Accelerator/DebugDescription/Identifier/UserText） |
| `virtual QAccessible::State state() const = 0` | 仅 bool isVisible | 缺完整 State 位集合（Qt 34 位） |
| `virtual QColor foregroundColor()/backgroundColor() const` | 无 | 缺失 |
| `virtual void *interface_cast(QAccessible::InterfaceType)` + 10 个子接口（Text/EditableText/Value/Action/Image/Table/TableCell/Hyperlink/Selection/Attributes） | 无 | 全部缺失 |
| `virtual void virtual_hook(int id, void *data)` | 无 | 缺失（Qt 内部扩展点） |
| QAccessibleEvent 家族（type/object/uniqueId/setChild/child/accessibleInterface + 9 个子类事件） | 无对象化事件类 | 缺失（Platform 模块仅有 XPlatformAccessibility 的 6 值 XAccessibleEvent 枚举） |
| `qAccessibleRoleString/qAccessibleEventString/qAccessibleLocalizedActionDescription` | 无 | 缺失（Drive 侧自行映射） |
| QAccessible::Role/State/Text/Relation/Event/Attribute 完整枚举 | 仅 XAccessibleRole 8 项子集 | 数值亦与 Qt 不一致（见违规） |

**功能缺口**
1. **XWidget 可访问元数据未接入**：XAccessible_name() 的控件路径只用 `XWidget_windowTitle → XObject_objectName → XWidget_toolTip`（XAccessible.c:132-140），完全没有读取 `XWidget_accessibleName`；XAccessible_description() 只读自身 m_description，不读 `XWidget_accessibleDescription`。而 Qt 的 QAccessibleWidget 优先取 accessibleName/accessibleDescription。XGui.md 10.479 已实现 XWidget_accessibleName/setAccessibleName（XWidget.h:1331-1337），却未与 XAccessible 打通 → 控件设置无障碍名/描述后 XAccessible_name/description 不反映。
2. **角色信息失真**：角色仅在创建时固定（窗口=Window、控件=Window/Client、应用=Application，XAccessible.c:52/69-70/88），无 `setRole`、无按控件类型推导；枚举里的 Button/Text/List/Table 无任何赋值路径 → 树节点角色基本无效。
3. **窗口节点 parent() 恒 NULL**（XAccessible.c:171-183 只处理控件节点）：应用根→窗口、窗口→应用根不双向，AT-SPI/UIA 向上导航在窗口节点断开。
4. **状态只暴露 isVisible**：Qt State 的 disabled/selected/focusable/focused/pressed/checked/readOnly/expanded 等常用位全部缺失，Drive 桥接无法上报真实状态。

**违规**
- 约束 4（Doxygen）：XAccessible.h 有 9 个公共函数完全无注释（isValid/role/rect/isVisible/name/setName/description/setDescription/window，第 66-74 行）+ 5 个仅单行 @brief（createApplication_ex/widget/parent/childCount/childAtIndex），缺 @param/@return。
- XAccessibleRole 数值与 Qt QAccessible::Role 不一致（X：Application=1、Window=2…；Qt：Application=0x0E、Window=0x09、Button=0x2B…），且仅 8 项真子集。头文件未声明“取值一致”，但建议在文档中明示子集与映射表（Drive 的 xpa_atspiRole 依赖此映射）。

### 2.2 XClipboard ↔ QClipboard（qtbase/src/gui/kernel/qclipboard.h）

**继承**：X：XClipboard→XObject；Qt：QClipboard→QObject。匹配。

**API 覆盖**：clear / supportsSelection / supportsFindBuffer / ownsClipboard / ownsSelection / ownsFindBuffer / text()×2 / setText / mimeData / setMimeData / image / pixmap / setImage / setPixmap 及 4 个信号（changed/selectionChanged/findBufferChanged/dataChanged）**全部存在**。Mode 枚举值（0/1/2）与 Qt 完全一致。C 无默认实参，mode 显式传参，可接受。**API 缺口 = 0**。

**信号（约束 5 通过）**：changed(mode) 带参经 XVarList 传递；selectionChanged/findBufferChanged/dataChanged 空参 args=NULL（XClipboard.c:308-335）。发射顺序与 Qt 一致：先模式专用信号、后 changed(mode)（qclipboard.cpp emitChanged 同序）。

**功能缺口**
1. **不支持模式的存储语义与 Qt 不一致**：supportsSelection()/supportsFindBuffer() 恒 false，但 `setText/setMimeData(Selection/FindBuffer)` 仍会存储数据并发信号（XClipboard.c:231-242）。Qt 对不支持模式的处理是 `setMimeData` 直接删除数据（`deleteLater`）且不存储、不发信号（qclipboard.cpp setMimeData）。X 头文件已文档化“仅做存储”，但仍属 Qt 行为偏差。
2. **进程内剪贴板不接系统**（XClipboard.h:7-12 明示）：无系统剪贴板同步、无外部内容变化通知、无延迟销毁。Qt 的剪贴板是系统背书的。属于文档化设计边界，但 XGui.md 10.389 亦承认“不模拟平台剪贴板的外部内容变化”。
3. **text() 无数据返回 NULL**（Qt 返回空 QString，XClipboard.c:154-169）：XGui.md 10.389 已文档化为边界，头文件用“空文本返回非空 XString”区分，语义可辩护。

**违规**
- 约束 1 / 命名规范：`XClipboard_text_2`（XClipboard.h:147）的 `_2` 后缀误用——项目规范中 `_2` 专指 UTF-8 `const char*` 兼容重载，此处实为 Qt `text(QString&, Mode)` 的子类型输出重载，应改名为如 `XClipboard_text_subtype` 之类。
- 裁剪配置失效：XClipboard.h 在 `XMIMEDATA_ON=0` 时仍无条件声明 `XMimeData* mimeData / setMimeData / XImage image / setImage / XPixmap pixmap / setPixmap`，且只 `#if XMIMEDATA_ON` 包含 XMimeData.h（XClipboard.h:30-32）→ `XImage/XPixmap/XMimeData` 类型未声明，头文件**不自包含**（已用 `gcc -fsyntax-only -DXMIMEDATA_ON=0 -DXCLIPBOARD_ON=1` 实测报 unknown type）。XClipboard.c 的 `XClipboard_image` 亦在无 `#if XMIMEDATA_ON` 保护下调用 `XMimeData_imageData`（XClipboard.c:244-251），`clipboard_clearModeData` 无条件调用 `XMimeData_delete_base`（XClipboard.c:36）。即文档承诺的“MIME 关闭时退化为仅文本模式”在 `XCLIPBOARD_ON=1` 时无法编译/链接。
- 约束 4（Doxygen）：supportsSelection/supportsFindBuffer 缺 @param；ownsClipboard/ownsSelection/ownsFindBuffer 与 3 个信号为单行 @brief，无 @param/@return（XClipboard.h:100-116、225-232）。

**合规亮点**：无 malloc/free/strdup/memcpy（全模块 grep=0）；XClipboard 与 Qt 一样禁拷贝（无 copy/move 虚函数）；setText 走“构造 XMimeData→setMimeData 转移所有权”与 Qt 完全同构（XClipboard.c:196-221）；clear 经 setMimeData 等价路径并保持“空剪贴板也通知”与 Qt 一致。

### 2.3 XCursor ↔ QCursor（qtbase/src/gui/kernel/qcursor.h）

**继承**：X：XCursor→XObject；Qt：QCursor（无基类，隐式共享值类型）。X 多一层 XObject，判匹配（合理扩展）。

**枚举**：XCursorShape 与 Qt::CursorShape **完全一致**（ArrowCursor=0…DragLinkCursor=22、LastCursor=22、BitmapCursor=24、CustomCursor=25，X 与 Qt 均无 23 号值）。头注释“全部 24 个标准形状”计数不准（标准形状为 23 个，24/25 为位图/自定义光标），仅文档瑕疵。

**API 缺口（Qt 原型）**

| Qt 6.8 API | X 现状 | 说明 |
|---|---|---|
| `static QPoint pos(const QScreen *screen)` | 无 | 缺失（XScreen 已存在，可映射） |
| `static void setPos(QScreen *screen, int x, int y)` | 无 | 缺失 |
| `void swap(QCursor &other)` | 无 | 可补 XCursor_swap |
| `friend bool operator==/!=(...)` | 无 | 可补 XCursor_equals |
| `operator QVariant() const` | 无 | Graphics 中 XImage/XBitmap 已有 toVariant 先例 |
| DataStream/Debug `operator<<` | 无 | C 裁剪，不计 |

**功能缺口**
1. **pos()/setPos() 未接平台真实光标**：X 为进程内静态变量（XCursor.c:25/233-247），初始 (0,0)，仅由 setPos 维护。Qt 的 pos() 查询平台光标（QPlatformCursor），在已有 X11/Win32 输入后端的背景下，真实指针位置无法读取、setPos 无法作用于系统。头文件已注明“未来由输入后端同步”，属文档化但影响实际可用性。
2. **setShape(Bitmap/Custom) 状态不一致**：X 直接保存任意 shape（XCursor.c:190-195）；Qt 对 `shape > LastCursor` 回退到箭头（qcursor.cpp setShape 用 `qt_cursorTable[0]`），不会出现“shape=Bitmap 但无位图”的中间态。

**违规**：无硬约束违规。深拷贝（XCursor.c:148-159）vs Qt 隐式共享为文档化性能语义差异，不违规。

### 2.4 XInputMethod ↔ QInputMethod（qtbase/src/gui/kernel/qinputmethod.h）

**继承**：X：XInputMethod→XObject；Qt：QInputMethod→QObject。匹配。

**API 覆盖**：inputItemTransform/set、inputItemRectangle/set、cursorRectangle、anchorRectangle、keyboardRectangle、inputItemClipRectangle、isVisible、setVisible、isAnimating、locale、inputDirection、queryFocusObject、show、hide、update、reset、commit、invokeAction 及 8 个信号**全部存在**。Action 枚举（0/1）、LayoutDirection 枚举（0/1/2）、InputMethodQuery 单值位（ImEnabled..ImPlatformData、ImQueryAll）与 Qt 一致。8 个 Q_PROPERTY 在 C 中无属性机制，getter+NOTIFY 信号已对应，豁免。**API 缺口 = 0**。

**信号（约束 5 通过）**：8/8，空参信号 args=NULL，inputDirectionChanged 带参经 XVarList（XInputMethod.c:415-471）。

**行为对照（与 qinputmethod.cpp 逐条比对）**：setInputItemTransform 相同则返回、变化时先发 cursorRectangleChanged 再发 anchorRectangleChanged（XInputMethod.c:207-219）✓；update 的 ImEnabled→context setInputMethodAccepted、随后 context->update、再按位发 3 个信号（XInputMethod.c:357-380）✓；setVisible→show/hide ✓；locale 无上下文返回 "C"（Qt QLocale::c()）✓；inputDirection 无上下文 LeftToRight ✓；输入项矩形映射按四角外接轴对齐矩形与 QTransform::mapRect 语义一致（XInputMethod.c:62-87）✓。

**功能缺口**
1. **焦点对象查询回调无生产注册点（最严重）**：`XInputMethod_setQueryHandler` 全仓库（Src/Drive/Test）只有 xgui_regression_test.c 测试代码调用（xgui_regression_test.c:19017），`XGuiApplication` 生产代码从不注册。后果：`queryFocusObject()` 恒返回 NULL，`cursorRectangle()/anchorRectangle()/inputItemClipRectangle()` 恒为零矩形，`update(ImEnabled)` 恒置 accepted=false——即 Qt 开箱即用的输入法矩形查询/启用判定在真实应用中全部失效。Qt 通过元对象 `inputMethodQuery()` 或 `QInputMethodQueryEvent` 直接查询焦点对象，无需手工接线。
2. **ImQueryInput 组值与 Qt 6.8 不一致**：X 定义 `ImQueryInput = 0x2|0x8|0x10|0x20|0x40|0x80 = 0xFA`（XInputMethod.h:85-87），Qt 6.8 为 `ImCursorRectangle|ImCursorPosition|ImSurroundingText|ImCurrentSelection|ImAnchorRectangle|ImAnchorPosition = 0x40BA`。X 多含 ImMaximumTextLength(0x40)、漏掉 ImAnchorRectangle(0x4000)。头文件声明“取值一致”但该组合值不一致（当前代码未使用该组合，属潜伏 API 契约缺陷）。
3. **keyboardRectangle/isVisible/isAnimating 依赖空后端**：无平台上下文时恒零/恒 false（XInputMethod.c:251-290），嵌入式下依赖 XPlatformInputContext 的 setter+emit 驱动，属文档化设计。

**违规**
- XInputMethod.h:85 声明“对标 Qt 6.8 Qt::InputMethodQuery，取值一致”但 ImQueryInput 不一致 → 枚举契约违规（P1）。
- 约束 4（Doxygen）：setQueryHandler 单行 @brief（第 199 行）、8 个信号单行 @brief，缺 @param/@return。

### 2.5 XMimeData ↔ QMimeData（qtbase/src/corelib/kernel/qmimedata.h）

**继承**：X：XMimeData→XObject；Qt：QMimeData→QObject。匹配。

**API 缺口（Qt 原型）**

| Qt 6.8 API | X 现状 | 说明 |
|---|---|---|
| `QList<QUrl> urls() const` / `void setUrls(...)` / `bool hasUrls() const` | 无 | 缺失（XUrl 已存在于 Src/XData/XUrl/XUrl.h，可映射） |
| `void removeFormat(const QString &mimetype)` | 无 | 缺失（无法单独移除自定义格式） |
| `virtual QVariant retrieveData(...)` | 无 | C 无虚函数，X 内部直读字段，豁免 |

**功能缺口**
1. **URL（text/uri-list）支持整体缺失**：Qt 的 setData 对 text/uri-list 会转换为 QList<QUrl>（qmimedata.cpp setData），hasText()/text() 有“从 URL 列表回退生成文本”的语义（retrieveTypedData 回退）。X 既无 urls 系列也无该回退。拖放（XGui.md 已实现 text/uri-list 接收）无法经 XMimeData 表达 URL 语义。
2. **hasFormat 大小写敏感性与 Qt 相反**：X 用 ASCII 大小写不敏感比较（XMimeData.c:177-193），Qt 的 hasFormat 实现为 `formats().contains(mimeType)`（QString 大小写敏感）。X 头文件还特别声明“大小写不敏感”——与 Qt 6.8 行为不一致。
3. **formats() 顺序与 Qt 不同**：X 为“内置格式优先、自定义在后”（XMimeData.c:229-256）；Qt 为 dataList 的插入顺序（qmimedata.cpp formats）。对依赖格式顺序的拖放/剪贴板协商有可观察差异。
4. **setData/data 的格式路由不完整**：Qt data() 对 application/x-color、application/x-qt-image、text/html（含编码探测）均有转换路径（retrieveTypedData）；X 的 data() 只路由 text/plain 与 text/html（XMimeData.c:398-417），application/x-color 返回 NULL；X 的 setData 只路由 text/plain、text/html（XMimeData.c:360-368），对 text/uri-list 按普通字节存（Qt 会转 URL）。

**违规**
- 约束 1（字符串 API 主次）：`XMimeData_hasFormat(self, const char*)`、`XMimeData_setData(self, const char*, ...)`、`XMimeData_data(self, const char*)` 主版本直接取 UTF-8 `const char*`，无 XString 主版本 + `_2` UTF-8 重载。风格文档明确将“MIME 类型”列入适用规则（“格式名…MIME 类型均遵循此规则”）。
- 约束 4（Doxygen）：hasText/hasHtml/hasColor/hasImage 单行 @brief（XMimeData.h:107/126/144/162），无 @param/@return。

**合规亮点**：copy/move 虚函数安全（深拷贝自定义表、move 置空源，XMimeData.c:67-135）；setText(NULL) 仍登记 text/plain 与 Qt 空串语义一致；无裸内存操作。

---

## 三、约束合规核查（Input 模块内）

| # | 硬约束 | 核查结果 |
|---|---|---|
| 1 | 拥有型字符串一律 XString*；API 主版本 XString，UTF-8 用 `_2` | **部分违规**：字符串成员均为 XString*（通过）；但 XMimeData_hasFormat/setData/data 主版本直接取 `const char*`（缺 XString 主版本+`_2` 重载）；XClipboard_text_2 的 `_2` 后缀语义误用（非 UTF-8 重载）。 |
| 2 | 继承一比一含中间基类 | 通过。5 个类均直接对应（X→XObject vs Qt→QObject 或 Qt 无基类+XObject 合理扩展），无缺失中间基类。 |
| 3 | 样式/绘制不得精简近似 | 不适用（本模块无绘制逻辑）。行为偏差集中在“进程内剪贴板”“空后端输入法”“未接平台光标”等平台集成边界，均已文档化。 |
| 4 | 公共头中文 Doxygen + UTF-8 BOM | **部分违规（Doxygen）**：5 个头文件全部带 UTF-8 BOM（通过）；但 XAccessible.h 9 个函数完全无注释+5 个仅 @brief、XClipboard.h 约 8 处、XInputMethod.h 约 9 处、XMimeData.h 约 4 处公共函数缺 @param/@return（共约 35 处）。 |
| 5 | 信号：空参 args=NULL；每信号有 `*_signal` | 通过。XClipboard 4/4、XInputMethod 8/8；空参信号 args=NULL，带参信号经 XVarList 传递；发射顺序与 Qt 一致。 |
| 6 | init/deinit 成对；copy/move 虚函数安全；禁 memcpy；禁裸 malloc/free/strdup | 通过。全模块无 malloc/free/strdup/memcpy；XCursor/XMimeData 实现 copy/move 虚函数；XClipboard/XInputMethod 与 Qt 一致禁拷贝。备注：XAccessible 无 XAccessible_init（create 专用）；XClipboard_init/XInputMethod_init 在 m_data 分配失败时对象半初始化但 create 仍返回非 NULL（健壮性瑕疵，非违规）。 |
| 7 | 旧 API 不保留 | 通过。未发现新旧双轨 API。 |
| 8 | 新代码 C99，无 C++/C11 | 通过。复合字面量等均为 C99 合法。 |

**额外发现（裁剪/头文件工程）**
- XClipboard.h 在 `XMIMEDATA_ON=0` 时引用未声明类型 XMimeData/XImage/XPixmap，且 XClipboard.c 对 XMimeData_imageData/XMimeData_delete_base 无裁剪保护 → `XCLIPBOARD_ON=1 + XMIMEDATA_ON=0` 配置无法编译（实测语法检查报 unknown type），与头文件“退化为仅文本模式”承诺矛盾。
- XInputMethod_setQueryHandler 无生产调用者（仅回归测试注册）→ 查询管线实际失效。

---

## 四、缺失 Qt 类清单（审计范围内）

| Qt 类 | Qt 头文件 | 建议 |
|---|---|---|
| QAccessibleTextInterface | qtbase/src/gui/accessible/qaccessible.h | 暂不实现；Drive AT-SPI/UIA 桥接按需扩展文本接口 |
| QAccessibleEditableTextInterface | 同上 | 不实现（同前） |
| QAccessibleValueInterface | 同上 | 不实现 |
| QAccessibleActionInterface | 同上 | 不实现（或按控件能力表实现 actionNames/doAction 最小子集） |
| QAccessibleImageInterface | 同上 | 不实现 |
| QAccessibleTableInterface / QAccessibleTableCellInterface | 同上 | 不实现（表格可访问能力暂缺） |
| QAccessibleHyperlinkInterface | 同上 | 不实现 |
| QAccessibleSelectionInterface | 同上 | 不实现 |
| QAccessibleAttributesInterface | 同上 | 不实现 |
| QAccessibleEvent（+ StateChange/TextCursor/TextSelection/TextInsert/TextRemove/TextUpdate/ValueChange/TableModelChange/Announcement 9 个子类） | 同上 | 建议轻量实现（或延续 Platform 模块 XAccessibleEvent 枚举方案），至少补 StateChanged/ValueChanged/TextCaretMoved 关键事件供 Drive 上报 |
| QAccessible::State（34 位状态结构） | 同上（qaccessible_base.h） | 建议补 XAccessibleState 位集合常用子集（disabled/selected/focusable/focused/pressed/checked/readOnly/expanded 等） |
| QAccessible::Role 完整枚举 | 同上（qaccessible_base.h） | 建议扩展 XAccessibleRole 到 Qt 数值或正式文档化“8 项子集+映射表” |
| QInputMethodQueryEvent | qtbase/src/gui/kernel/qevent.h | 不实现（X 用 XInputMethodQueryHandler 回调替代事件投递，已文档化） |

> 说明：QClipboard/QCursor/QInputMethod/QMimeData 四个基准头内的主类均有 X 对应实现，无“完全缺失”主类；缺失集中在 QAccessible 的接口/事件体系。

---

## 五、优先任务建议（按优先级）

1. **修复 XInputMethod 查询管线**（P0 功能）：在 XGuiApplication 初始化 XInputMethod 时默认注册焦点对象查询回调（或由 XLineEdit 等焦点控件实现 inputMethodQuery 等价查询），否则 queryFocusObject/cursorRectangle/anchorRectangle/inputItemClipRectangle 与 ImEnabled 在真实应用中恒失效。
2. **修正 XInputMethodQuery_ImQueryInput 取值**：改为 Qt 6.8 的 `ImCursorRectangle|ImCursorPosition|ImSurroundingText|ImCurrentSelection|ImAnchorRectangle|ImAnchorPosition`（0x40BA），删掉误含的 ImMaximumTextLength。
3. **修复 XClipboard 裁剪配置**：XClipboard.h 无条件 include XImage.h/XPixmap.h，且把 XMimeData/XImage/XPixmap 相关接口用 `#if XMIMEDATA_ON` 裁剪（或在 XMIMEDATA_ON=0 时给 XClipboard.c 补空实现），兑现“退化为仅文本模式”承诺。
4. **XAccessible 打通控件元数据**：name/description 优先读取 XWidget_accessibleName/accessibleDescription；补 setRole 或按控件类型推导角色；窗口节点 parent 指向应用根；补常用 State 位集合。
5. **XMimeData 补 URL 与 removeFormat**：用 XUrl/XStringList 实现 urls/setUrls/hasUrls，setData 对 text/uri-list 做 URL 转换；hasFormat 改大小写敏感对齐 Qt；formats 顺序对齐插入序。
6. **XMimeData 字符串 API 合规化**（约束 1）：hasFormat/setData/data 补 XString 主版本 + `_2` UTF-8 重载；XClipboard_text_2 改名（如 text_subtype）。
7. **XClipboard 不支持模式对齐 Qt**：Selection/FindBuffer 的 set 操作按 Qt 语义释放数据、不存储、不发信号（或正式文档化豁免）。
8. **XCursor 接入真实平台光标**：X11/Win32 后端为 XCursor_pos/setPos 提供数据源；补 pos(XScreen)/setPos(XScreen,...)、swap、equals。
9. **补齐 Doxygen**（约束 4）：XAccessible（14 处）、XClipboard（约 8 处）、XInputMethod（约 9 处）、XMimeData（约 4 处）公共函数补全 @param/@return。
10. **补测试**：在 xgui_regression_test.c 增加 XMIMEDATA_ON=0 裁剪编译用例、ImQueryInput 取值断言、XMimeData 大小写敏感性与 formats 顺序断言，锁定本轮发现的行为差异。

---

## 附：审计方法说明

- 仅只读检查：glob 列出 Src/XGui/Input/*.h（无 *_Protected.h/*_Internal.h），read 全部 5 个头文件与 5 个 .c；Qt 侧 read qaccessible.h/qclipboard.h/qcursor.h/qinputmethod.h/qmimedata.h，并对照 qclipboard.cpp/qmimedata.cpp/qcursor.cpp/qinputmethod.cpp 验证行为。
- 裁剪配置验证：用 `gcc -std=gnu99 -fsyntax-only -DXMIMEDATA_ON=0 -DXCLIPBOARD_ON=1`（复用 build-crop-min 的 include 集）实测 XClipboard.h 编译失败，证据：unknown type name 'XMimeData'/'XImage'/'XPixmap'。
- 生产接线核查：`grep -rn XInputMethod_setQueryHandler Src Test Drive` 仅命中 XInputMethod.c/h 定义与 xgui_regression_test.c 测试调用。
- 未修改 Src/、Test/ 任何文件，未 commit/push（git status 仅 docs/xgui-audit/ 未跟踪）。
