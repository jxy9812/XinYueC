# XGui ↔ Qt 6.8.3 gui 域缺口三分处置报告（首扫 129 MISS）

> 处置日期：2026-09-18　依据：`docs/xgui-audit/2026-09-16/xgui-api-gaps-gui-v1.txt`
> （映射类 45、缺口 129 MISS、涉及 15 类）
> Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui`（下文 Qt 头文件路径均
> 相对该目录，如 `image/qimage.h:142`）；XGui 现状相对 `Src/XGui/`。
> 性质：只读审计，未改动任何源码/脚本；本文件为唯一新增产物。

## 一、处置规则

| 标签 | 含义 | 后续动作 |
|---|---|---|
| A 豁免 | 设计边界不做：QVariant 转换运算符、内部钩子/数据指针、无障碍注册表、平台专属桥接、Qt 已废弃 API、C 无法承载的 C++ 机制 | 登记脚本 `tools/xgui_api_scan.py` SKIP 表（理由见第六节） |
| A′ 已覆盖（误报） | **非缺口**：XGui 侧已有实现，仅因命名归一（`copyRect`↔`copy`、`virtualHook`↔`virtual_hook`、`toVariant`↔`operator QVariant`）或扫描器 LIFECYCLE 过滤（虚工厂 `create_base`）被误报 | 登记 RENAMED 改名表或修扫描器，**不进 SKIP** |
| B 候选 | 有应用价值的真实缺失 API | 标注优先级（高/中/低）与预估工作量，按批次实现（第四节） |
| C 平台补齐 | 功能面已由 XPlatformIntegration/XPlatformAccessibility 承载，缺口属类归属/模型差异 | 注明现状；其中改名承载者登记 RENAMED，所有权模型差异者登记 SKIP（第六节） |

## 二、总表（15 类逐类三分占比）

| Qt 类 → XGui 类 | MISS | A 豁免 | A′ 已覆盖 | B 候选 | C 平台 | 备注 |
|---|---:|---:|---:|---:|---:|---|
| QAccessible → XAccessible | 18 | 11 | 0 | 0 | 7 | 注册表/工厂族豁免；事件分发族归平台桥 |
| QActionGroup → XActionGroup | 5 | 0 | 0 | 5 | 0 | 全部为真实候选（互斥策略/可见性/别名） |
| QBitmap → XBitmap | 1 | 0 | 1 | 0 | 0 | `QVariant` 已由 XBitmap_toVariant 承载 |
| QIcon → XIcon | 2 | 1 | 0 | 1 | 0 | data_ptr 豁免；toVariant 小候选 |
| QIconEngine → XIconEngine | 1 | 0 | 1 | 0 | 0 | virtualHook 已实现，命名归一问题 |
| QImage → XImage | 15 | 8 | 5 | 2 | 0 | 平台桥 6 条豁免；copy/convert 族已覆盖 |
| QImageIOPlugin → XImageIOPlugin | 1 | 0 | 1 | 0 | 0 | 虚工厂 create_base 已实现，扫描器误滤 |
| QMovie → XMovie | 2 | 2 | 0 | 0 | 0 | QBindable 绑定属性体系不做 |
| QPicture → XPicture | 1 | 1 | 0 | 0 | 0 | data_ptr 豁免（data()/setData() 已承载） |
| QPixmap → XPixmap | 4 | 2 | 1 | 1 | 0 | copy 已覆盖；handle/句柄族豁免 |
| QPlatformAccessibility → XPlatformAccessibility | 2 | 0 | 0 | 0 | 2 | notify 改名承载；根内部拥有 |
| QPlatformIntegration → XPlatformIntegration | 1 | 1 | 0 | 0 | 0 | C++ 模板原生接口，C 不做 |
| QShortcut → XShortcut | 4 | 2 | 0 | 2 | 0 | 废弃 API 豁免；键序列族候选 |
| QTextDocument → XTextDocument | 71 | 45 | 0 | 26 | 0 | 排版引擎/块对象体系豁免为大头 |
| QWheelEvent → XWheelEvent | 1 | 1 | 0 | 0 | 0 | QPointingDevice 设备对象体系未建 |
| **合计** | **129** | **74（57.4%）** | **9（7.0%）** | **37（28.7%）** | **9（7.0%）** | 真实待实现 = 37 候选；9 平台项多为现状声明 |

> 摘要：129 条中 83 条（74 豁免 + 9 已覆盖）经核实**无需实现**——其中 9 条
> 纯属扫描器命名/过滤盲区，XGui 已有实现；37 条为真实候选（高 2 / 中 7 /
> 低 28）；9 条为平台接口现状声明。收口全部 37 条候选后 gui 域首扫缺口
> 即可清零（豁免/平台项经第六节联动登记后由扫描器消账）。

## 三、逐类明细

### 3.1 QAccessible → XAccessible（18 条，Qt：`accessible/qaccessible_base.h`）

XGui 现状：XAccessible 为纯数据节点（`Input/XAccessible.h`：role/rect/
name/parent/childCount/childAtIndex），由 `createForWindow/createForWidget/
createApplication` 直接构造；进程级桥接由 `Platform/XPlatformAccessibility.h`
的 Drive 契约承载（Driver_start/notify/processEvents）。

| MISS | 处置 | 理由（Qt 佐证 / XGui 现状） |
|---|---|---|
| installFactory | A 豁免 | 接口工厂注册表（`qaccessible_base.h:387` InterfaceFactory）；插件工厂体系嵌入式不做；XAccessible 由 createForWindow/createForWidget 直接构造 |
| removeFactory | A 豁免 | 同 installFactory（`:388`） |
| queryAccessibleInterface | A 豁免 | QObject→接口查询注册表（`:401`）；XGui 无 QObject 元对象体系，节点由 widget/window 直接挂接 |
| uniqueId | A 豁免 | 接口句柄唯一 id 注册表（`:402`）；无障碍注册表管理不做，树遍历经 parent/childAtIndex |
| accessibleInterface | A 豁免 | 同 uniqueId 反查（`:403`） |
| registerAccessibleInterface | A 豁免 | 同 uniqueId 注册（`:404`） |
| deleteAccessibleInterface | A 豁免 | 同 uniqueId 注销（`:405`） |
| installActivationObserver | A 豁免 | 激活观察者注册表（`:398` ActivationObserver）；启停状态经 XPlatformAccessibility_setActive 单点承载，不建观察者列表 |
| removeActivationObserver | A 豁免 | 同 installActivationObserver（`:399`） |
| qAccessibleTextBoundaryHelper | A 豁免 | 文本边界计算内部辅助（`:415`，供 AT-SPI 桥实现用），非应用层 API |
| void | A 豁免 | **扫描器解析伪影**：`typedef void(*UpdateHandler)(...)`（`:382`）被提取为方法名 `void`；建议修扫描器（第六节），非真实 API |
| isActive | C 平台 | 静态启停（`:409`）；现状：`XPlatformAccessibility_isActive`（XPlatformAccessibility.h:54）已承载，仅类归属不同 |
| setActive | C 平台 | 同 isActive（`:410`；XPlatformAccessibility.h:56） |
| cleanup | C 平台 | 静态清理（`:413`）；现状：`XPlatformAccessibility_cleanup`（h:61，幂等）已承载 |
| updateAccessibility | C 平台 | 全局事件分发（`:407`）；现状：`XPlatformAccessibility_notify/notifyWindow/notifyWidget`（h:62/68/70）+ Driver_notify 契约承载；不做回调注册模型 |
| installUpdateHandler | C 平台 | 全局更新回调钩子（`:389`）；事件分发已由 Drive 契约承载，回调注册模型不做 |
| installRootObjectHandler | C 平台 | 根对象回调钩子（`:390`）；同上 |
| setRootObject | C 平台 | 静态根设置（`:411`）；现状：应用根由 XPlatformAccessibility 内部创建拥有（h:60 注释、m_root 字段），无 setter，root() 读取 |

### 3.2 QActionGroup → XActionGroup（5 条，Qt：`kernel/qactiongroup.h`）

XGui 现状：`Widget/XActionGroup.h` 已有 exclusive（bool）、enabled、
checkedAction、triggered/hovered 信号；无可见性状态、无策略枚举。

| MISS | 处置 | 理由 |
|---|---|---|
| exclusionPolicy | B 候选·低 | `qactiongroup.h:21,46` ExclusionPolicy 枚举（None/Exclusive）；XGui 以 bool m_exclusive 承载（h:52），补枚举访问器与 bool 双向映射即可 |
| setExclusionPolicy | B 候选·低 | 同上（`:54`） |
| isVisible | B 候选·中 | `:45` visible 属性——组整体从菜单/工具栏隐藏；需新增 m_visible + 成员转发 + 菜单挂接消费路径 |
| setVisible | B 候选·中 | 同上（`:52`） |
| setDisabled | B 候选·低 | `:51` 内联 `setEnabled(!b)` 反义别名；C 侧以别名宏实现，近乎零成本 |

### 3.3 QBitmap → XBitmap（1 条，Qt：`image/qbitmap.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| QVariant | A′ 已覆盖 | `qbitmap.h:31` `operator QVariant()`；XGui 以 `XBitmap_toVariant/fromVariant` 承载（Graphics/XBitmap.h:163/170，含 XVariant 兼容适配注释）；登记 RENAMED `QBitmap.QVariant → toVariant` |

### 3.4 QIcon → XIcon（2 条，Qt：`image/qicon.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| QVariant | B 候选·低 | `qicon.h:198` `operator QVariant()`；XImage/XBitmap 已有 toVariant/fromVariant 先例（XImage.h:741、XBitmap.h:163），XIcon 补一对保持一致性，极小工作量 |
| data_ptr | A 豁免 | `qicon.h:266` `inline DataPtr &data_ptr()` 内部数据指针访问器，非应用层 API（对标 SKIP 表 `*.qt_findObjChild` 措辞） |

### 3.5 QIconEngine → XIconEngine（1 条，Qt：`image/qiconengine.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| virtual_hook | A′ 已覆盖 | `qiconengine.h:50` `virtual void virtual_hook(int, void*)`；XGui 已实现扩展点：`XIconEngine_virtualHook_base`（Icon/XIconEngine.h:229，XIconEngine.c:234，vtable 槽 VXIconEngine_virtualHook，含 IsNullHook/ScaledPixmapHook）。缺口为 Qt snake_case 与 XGui camelCase 命名归一问题；登记 RENAMED `virtual_hook → virtualHook` |

### 3.6 QImage → XImage（15 条，Qt：`image/qimage.h`）

XGui 现状：`Graphics/XImage.h` 已有 copyRect(:615)、convertToFormat(:624)、
convertToFormatInPlace(:646)、applyColorTransform(:348)、convertedToColorSpace
(:300)、toVariant(:741)、paintDevice(:521)。

| MISS | 处置 | 理由 |
|---|---|---|
| QVariant | A′ 已覆盖 | `qimage.h:115` `operator QVariant()`；`XImage_toVariant/fromVariant`（XImage.h:741/748）已承载；RENAMED `QImage.QVariant → toVariant` |
| colorTransformed | A′ 已覆盖 | `qimage.h:243` 应用 QColorTransform 返回新图；`XImage_applyColorTransform`（XImage.h:348，XColorTransform 建模于 XColorSpace.h:128）语义等价；RENAMED `colorTransformed → applyColorTransform` |
| convertTo | A′ 已覆盖 | `qimage.h:142` 原地格式转换；`XImage_convertToFormatInPlace`（XImage.h:646）等价；RENAMED |
| convertedTo | A′ 已覆盖 | `qimage.h:138` 命名返回版 convertToFormat；`XImage_convertToFormat`（XImage.h:624）等价；RENAMED |
| copy | A′ 已覆盖 | `qimage.h:119` 区域拷贝；`XImage_copyRect`（XImage.h:615）等价（与 C 生命周期 create 命名冲突同一规避模式）；RENAMED `copy → copyRect` |
| data_ptr | A 豁免 | `qimage.h:318` 内部 DataPtr 访问器，非应用层 API |
| devType | B 候选·低 | `qimage.h:111` QPaintDevice 虚访问器；XImage 已是绘制目标（XPainter_begin_image，XPainter.h:602）且有 XImage_paintDevice（XImage.h:521）、XPaintDevice 含 m_devType（XPaintDevice.h:133）、XPicture_devType 先例（XPicture.h:168），补转发即可 |
| paintEngine | B 候选·低 | `qimage.h:267` 同上；XPaintDevice 含 m_engine（XPaintDevice.h:136），XPicture_paintEngine 先例（XPicture.h:171），补转发即可 |
| fromHBITMAP | A 豁免 | `qimage.h:292` Windows GDI 桥接（HBITMAP）；XGui 面向 Linux/X11，设计上不做（对齐 toNSMenu 豁免措辞） |
| fromHICON | A 豁免 | `qimage.h:293` Windows GDI 桥接（HICON），同上 |
| toHBITMAP | A 豁免 | `qimage.h:290` Windows GDI 桥接，同上 |
| toHICON | A 豁免 | `qimage.h:291` Windows GDI 桥接，同上 |
| toCGImage | A 豁免 | `qimage.h:287` macOS CoreGraphics 桥接（CGImageRef），同上不做 |
| toPixelFormat | A 豁免 | `qimage.h:282` QPixelFormat 低层像素描述体系未建，非应用层 API |
| toImageFormat | A 豁免 | `qimage.h:283` 同上（QPixelFormat→Format 反查） |

### 3.7 QImageIOPlugin → XImageIOPlugin（1 条，Qt：`image/qimageiohandler.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| create | A′ 已覆盖 | `qimageiohandler.h:116` 纯虚工厂 `create(device, format)`；XGui 已实现虚工厂 `XImageIOPlugin_create_base`（Graphics/XImageIOPlugin.h:90，XImageIOPlugin.c:61）+ capabilities_base（h:80）+ 内建插件注册（XImageBuiltinPlugin/XImagePluginRegistry）。MISS 为扫描器 LIFECYCLE_NAMES 将 `create` 当生命周期名过滤所致；**修扫描器**（`X<cls>_create_base` 计入满足集），不进 SKIP |

### 3.8 QMovie → XMovie（2 条，Qt：`image/qmovie.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| bindableSpeed | A 豁免 | `qmovie.h:82` QBindable 绑定属性体系（QProperty 响应式绑定）；C API 以成对访问器承载，XMovie 已有 speed/setSpeed（XMovie.h:346） |
| bindableCacheMode | A 豁免 | `qmovie.h:89` 同上；cacheMode/setCacheMode 已有（XMovie.h:371/377） |

### 3.9 QPicture → XPicture（1 条，Qt：`image/qpicture.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| data_ptr | A 豁免 | `qpicture.h:70` 内部 DataPtr 访问器；序列化数据面已由 XPicture_data/setData/size 承载（XPicture.h:185/193/178） |

### 3.10 QPixmap → XPixmap（4 条，Qt：`image/qpixmap.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| QVariant | B 候选·低 | `qpixmap.h:48` `operator QVariant()`；XImage/XBitmap 已有 toVariant 先例，XPixmap 补一对保持一致性 |
| copy | A′ 已覆盖 | `qpixmap.h:104-105` 区域拷贝；`XPixmap_copyRect`（Graphics/XPixmap.h:469）等价；RENAMED `copy → copyRect` |
| data_ptr | A 豁免 | `qpixmap.h:148` 内部 DataPtr 访问器，非应用层 API |
| handle | A 豁免 | `qpixmap.h:144` 返回 QPlatformPixmap*（平台像素后端对象）；XPixmap 自含光栅实现，平台后端指针不暴露（对标 winId「原生句柄体系不做」豁免措辞） |

### 3.11 QPlatformAccessibility → XPlatformAccessibility（2 条，Qt：`accessible/qplatformaccessibility.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| notifyAccessibilityUpdate | C 平台 | `qplatformaccessibility.h:31` 虚通知；现状：`XPlatformAccessibility_notify(event, accessible)`（XPlatformAccessibility.h:62）改名承载（事件以 XAccessibleEvent 枚举承载）；RENAMED `notifyAccessibilityUpdate → notify` |
| setRootObject | C 平台 | `:32` 虚根设置；现状：应用根由桥内部创建拥有（m_root，h:41；createApplication 注释 h:60），不设 setter，公开面为 root()（h:53）；登记 SKIP（所有权模型差异） |

### 3.12 QPlatformIntegration → XPlatformIntegration（1 条，Qt：`kernel/qplatformintegration.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| call | A 豁免 | `qplatformintegration.h:207-212` 为 C++ 模板 `template <auto func, typename... Args> call(...)`（QNativeInterface 原生接口访问器）；C 语言无法承载模板机制；XPlatformIntegration 已按 C 契约直接暴露全部平台入口（XPlatformIntegration.h:265-624，约 40 个入口函数） |

### 3.13 QShortcut → XShortcut（4 条，Qt：`kernel/qshortcut.h`）

XGui 现状：`Widget/XShortcut.h` 单键 int 承载（m_key，h:83；文件头 @note
已声明组合键序列不在本子批范围），含 register/unregister/match 全局匹配。

| MISS | 处置 | 理由 |
|---|---|---|
| id | A 豁免 | `qshortcut.h:175` `QT_DEPRECATED_VERSION_6_0`——Qt 6.0 已废弃的快捷键 id 注册表访问器；XGui 亦不建 id 注册表（SKIP 表 grabShortcut 族同一边界、navigationMode 废弃 API 同一先例） |
| parentWidget | A 豁免 | `qshortcut.h:181-187` `QT_DEPRECATED_VERSION_X_6_0("Use parent() and qobject_cast instead")`；XGui 以 XObject parent() 承载，创建时父对象存于 m_parentWidget（h:88）内部用于匹配 |
| keys | B 候选·中 | `qshortcut.h:163` 返回 QKeySequence 列表；依赖 XKeySequence 序列建模（当前单键 int），序列体系落地后补列表访问器 |
| setKeys | B 候选·中 | `qshortcut.h:161-162`（StandardKey/列表两个重载）；同上，依赖序列建模 |

### 3.14 QTextDocument → XTextDocument（71 条，Qt：`text/qtextdocument.h`）

XGui 现状：`Widget/XTextDocument.h` 为块+片段二级简化模型（扁平块数组、
XTD_MAX_BLOCKS=256 硬上限、模块级快照撤销栈 XTD_MAX_UNDO=50、m_modified
计数 + modificationChanged 信号、m_url 元信息 + baseUrlChanged 信号）；
**无排版引擎、无 QTextBlock/QTextFrame/QTextObject 对象体系、无 QTextOption/
QCSS**（此前报告已声明「XTextDocument 为纯 C 子集，11b」）。

#### 3.14.1 A 豁免（45 条）

| MISS（组） | 处置 | 理由（Qt 佐证） |
|---|---|---|
| begin / end | A 豁免 | `:164-165` QTextBlock 迭代器；QTextBlock 对象体系未建，XGui 以 blockIndex int 直接寻块 |
| findBlock / findBlockByNumber / findBlockByLineNumber | A 豁免 | `:161-163` 返回 QTextBlock；且 XGui 扁平模型块号==数组索引，无独立块号/行号映射，遍历即可获得等价信息 |
| firstBlock / lastBlock | A 豁免 | `:167-168` 返回 QTextBlock，同上 |
| frameAt / rootFrame | A 豁免 | `:155-156` QTextFrame 框架层级体系（表格/嵌套框架）未建；XGui 扁平块模型 |
| object / objectForFormat | A 豁免 | `:158-159` QTextObject 定制富对象体系（列表对象/图片对象等）未建 |
| allFormats | A 豁免 | `:215` 返回 QList<QTextFormat>；QTextFormat 格式对象体系未建（对齐 SKIP 表 QTextCharFormat 未建模措辞） |
| defaultTextOption / setDefaultTextOption | A 豁免 | `:263-264` QTextOption 对象体系（换行模式/制表位/方向）未建模 |
| defaultStyleSheet / setDefaultStyleSheet | A 豁免 | `:70,246-247` QCSS 级联样式表解析器不做；XGui HTML 子集以内联 style 属性承载（富文本格式子集边界） |
| documentLayout / setDocumentLayout | A 豁免 | `:98-99` QAbstractTextDocumentLayout 布局引擎对象体系，架构级不做；documentLayoutChanged 信号标记已保留（XTextDocument.h:196） |
| drawContents | A 豁免 | `:225` 经 documentLayout->draw 绘制；无布局引擎；渲染由 XTextEdit 自绘路径承载 |
| adjustSize | A 豁免 | `:238` 依赖布局计算文档尺寸 |
| idealWidth | A 豁免 | `:230` 依赖换行布局 |
| textWidth / setTextWidth | A 豁免 | `:66,227-228` 换行宽度驱动折行布局；无折行模型 |
| lineCount | A 豁免 | `:242` 行数依赖折行布局（块≠行） |
| isLayoutEnabled / setLayoutEnabled | A 豁免 | `:64,222-223` 布局引擎开关；无引擎 |
| useDesignMetrics / setUseDesignMetrics | A 豁免 | `:63,219-220` 排版度量精度开关；依赖布局引擎 |
| revision | A 豁免 | `:96` 布局增量重绘内部修订号（配合布局引擎 delta 机制），内部机制 |
| markContentsDirty | A 豁免 | `:217` 布局脏区内部通知钩子，非应用层 API |
| pageCount / pageSize / setPageSize | A 豁免 | `:61,170-171,185` 分页体系（配合打印）；打印子系统不做（SKIP 表 QPlainTextEdit.print 同一边界） |
| print | A 豁免 | `:190` QPagedPaintDevice 打印输出；打印子系统不在 XGui 范围 |
| baselineOffset / setBaselineOffset | A 豁免 | `:182-183` 基线微调（Qt 6.8 新 API），依赖排版引擎 |
| superScriptBaseline / setSuperScriptBaseline | A 豁免 | `:176-177` 基线微调；上下标已由 XTDCharFormat.superScript/subScript 布尔位承载（XTextDocument.h:47-48） |
| subScriptBaseline / setSubScriptBaseline | A 豁免 | `:179-180` 同上 |
| defaultCursorMoveStyle / setDefaultCursorMoveStyle | A 豁免 | `:269-270` Qt::CursorMoveStyle 光标移动风格（逻辑/视觉序双向文本导航）；光标导航引擎子集不做 |
| QVariant | A 豁免 | **扫描器解析伪影**：`using ResourceProvider = std::function<QVariant(const QUrl&)>;`（`:207`）被提取为方法名 QVariant；修扫描器（第六节），非真实 API |
| appendUndoItem | A 豁免 | `:287` QAbstractUndoItem 自定义撤销命令扩展钩子（protected 内部扩展点）；C 无多态命令对象，撤销栈内部自管 |
| size | A 豁免 | `Q_PROPERTY size`，= documentLayout->documentSize()；依赖布局引擎 |

（以上合计 45 条：迭代器/块对象 7、框架/富对象 4、格式/样式 5、布局族 15、
分页打印 4、基线 6、光标移动风格 2、撤销钩子 1、解析伪影 1——逐行计数
见上表。）

#### 3.14.2 B 候选（26 条）

| MISS | 优先级 | 理由与工作量 |
|---|---|---|
| isModified | **高** | `:60,187` modified Q_PROPERTY READ；m_modified 字段与 modificationChanged 信号均已存在（XTextDocument.h:96、c 撤销联动 c:195），仅缺访问器。约 0.5 人时 |
| setModified | **高** | `:288` WRITE；语义对齐 Qt：setModified(false) 复位修改基线并发射 modificationChanged。约 0.5 人时 |
| clone | 中 | `:82` 深拷贝出新文档；仓库已有 create_copy 先例（XPicture_create_copy，XPicture.h:107），复制块/片段/字符串即可。约 0.5-1 人日 |
| addResource | 中 | `:205` 资源表（type+url 键→QVariant）；供富文本图片等资源解析。以 XVariant 承载值（XData/XVariant 已有）。约 1 人日 |
| resource | 中 | `:204` 资源查询；与 addResource 同批。约 0.5 人日 |
| availableUndoSteps | 低 | `:93` 撤销步数；撤销栈已有（快照栈 c:663-688），返回栈深即可；注意当前栈为模块级静态（非 per-document），建议随本项改为文档成员。约 0.5 人日 |
| availableRedoSteps | 低 | `:94` 同上 |
| clearUndoRedoStacks | 低 | `:258` 清栈便利；小 |
| baseUrl | 低 | `:266` baseUrl Q_PROPERTY READ；m_url 字段（h:95）与 baseUrlChanged 信号（h:192）已有，别名 getter。极小 |
| setBaseUrl | 低 | `:267` WRITE；同上，发射 baseUrlChanged |
| contentsChange | 低 | `:273` 带参信号 contentsChange(from, charsRemoved, charsAdded)；与既有无参 contentsChanged 并存，插入/删除路径补参数发射。约 0.5 人日 |
| defaultFont / setDefaultFont | 低 | `:62,173-174` QFont 默认字体；XGui 有 XFont 体系（Src/XData/XFont），与 defaultFormat（h:149-150）字体字段建立别名/转发。约 0.5 人日 |
| documentMargin / setDocumentMargin | 低 | `:73,235-236` qreal 文档边距；纯存储 + 渲染消费路径（XTextEdit 绘制边距）。约 0.5 人日 |
| indentWidth / setIndentWidth | 低 | `:68,232-233` 列表缩进宽度（像素）；与既有 indentLevel（h:77）配合换算。约 0.5 人日 |
| maximumBlockCount / setMaximumBlockCount | 低 | `:72,260-261` 动态块上限；现硬编码 XTD_MAX_BLOCKS=256（h:34），动态化为字段+追加钳制。约 0.5 人日 |
| toRawText | 低 | `:133` 与 toPlainText 差异仅保留 U+2028/U+2029 等控制符；近别名实现。极小 |

#### 3.14.3 B 候选（依赖/评估，8 条）

| MISS | 优先级 | 理由与工作量 |
|---|---|---|
| defaultResourceProvider | 低 | `:212` 静态资源回调（std::function）；C 侧为「函数指针+userdata」对；依赖 resource 体系，同批评估。约 0.5 人日 |
| resourceProvider | 低 | `:209` 实例资源回调；同上 |
| setResourceProvider | 低 | `:210` 同上 |
| setDefaultResourceProvider | 低 | `:213` 同上 |
| toMarkdown | 低·评估 | `:126` Markdown 方言导出；XGui HTML 子集已承载交换格式，Markdown 属可选转换器，按需求评估（此前 D 类规划已列「markdown(评估)」） |
| setMarkdown | 低·评估 | `:130` Markdown 解析导入；同上 |

### 3.15 QWheelEvent → XWheelEvent（1 条，Qt：`kernel/qevent.h`）

| MISS | 处置 | 理由 |
|---|---|---|
| pointingDevice | A 豁免 | `qevent.h:80` 返回 const QPointingDevice*（输入设备对象注册表体系 QInputDevice/QPointingDevice）；XGui 无设备对象体系且嵌入式单指针设备（对标 SKIP 表 QWidget.screen「屏幕对象体系未建（嵌入式单屏）」措辞）；XWheelEvent 已覆盖 position/angleDelta/pixelDelta/phase/inverted/source 全部交互面（Window/XWindowEvent.h:498-570，无设备字段为有意简化） |

## 四、真实候选汇总表（按优先级与建议批次）

| 批次 | 优先级 | 类 | 条目 | 条数 | 预估工作量 |
|---|---|---|---|---:|---|
| G1 | 高 | QTextDocument | isModified、setModified | 2 | 0.5 人日（含回归） |
| G2 | 中 | QTextDocument | clone | 1 | 0.5-1 人日 |
| G2 | 中 | QTextDocument | addResource、resource（资源表，XVariant 承载） | 2 | 1.5 人日 |
| G2 | 中 | QActionGroup | isVisible、setVisible（m_visible+成员转发+菜单消费） | 2 | 1 人日 |
| G3 | 中 | QShortcut | keys、setKeys（前置：XKeySequence 序列建模，另列专项） | 2 | 1-2 人日（不含序列建模） |
| G4 | 低 | QActionGroup | setDisabled（别名宏）、exclusionPolicy、setExclusionPolicy（枚举+bool 映射） | 3 | 0.5 人日 |
| G4 | 低 | QIcon / XPixmap | toVariant/fromVariant 各一对（对齐 XImage/XBitmap 模式） | 2 | 0.5 人日 |
| G4 | 低 | QImage | devType、paintEngine（转发 XPaintDevice m_devType/m_engine） | 2 | 0.5 人日 |
| G4 | 低 | QTextDocument | availableUndoSteps、availableRedoSteps（建议顺势把快照栈改为文档成员）、clearUndoRedoStacks、baseUrl、setBaseUrl、contentsChange、documentMargin、setDocumentMargin、indentWidth、setIndentWidth、maximumBlockCount、setMaximumBlockCount、toRawText | 13 | 2 人日 |
| G5 | 低 | QTextDocument | defaultFont、setDefaultFont、resourceProvider、setResourceProvider、defaultResourceProvider、setDefaultResourceProvider | 6 | 1.5 人日 |
| G5 | 低·评估 | QTextDocument | toMarkdown、setMarkdown（按需求评估，可长期挂起） | 2 | 评估后 1.5-2 人日 |
| **合计** | | | | **37** | 约 **8-10 人日**（不含 Markdown 评估项） |

排序说明：G1 为文档编辑核心状态且近零成本，最先收口；G2 为独立功能面
（快照/资源/可见性），可并行；G3 受键序列建模前置约束；G4 为别名与微项，
适合单批打包；G5 依赖资源回调体系与需求确认，最后处理。

## 五、平台接口补齐现状小结（C 类 9 条）

| 缺口 | 现状承载 | 建议 |
|---|---|---|
| QAccessible.isActive / setActive / cleanup | XPlatformAccessibility_isActive/setActive/initialize/cleanup（XPlatformAccessibility.h:54-61），静态函数→平台桥成员，类归属差异 | 登记 SKIP（理由注明承载点） |
| QAccessible.updateAccessibility / installUpdateHandler / installRootObjectHandler | XPlatformAccessibility_notify/notifyWindow/notifyWidget（h:62-70）+ XPlatformAccessibilityDriver_notify 契约（h:73-80）；回调注册模型不做 | 登记 SKIP |
| QAccessible.setRootObject | 应用根由桥内部 createApplication 拥有（h:60），root()（h:53）读取，无 setter | 登记 SKIP |
| QPlatformAccessibility.notifyAccessibilityUpdate | XPlatformAccessibility_notify(event, accessible)（h:62）改名承载 | 登记 RENAMED `notifyAccessibilityUpdate → notify` |
| QPlatformAccessibility.setRootObject | 同 QAccessible.setRootObject（m_root 内部拥有） | 登记 SKIP |

XPlatformIntegration 侧无 C 类项：既有 API 面已覆盖 Qt 对应虚函数族
（XPlatformIntegration.h:265-624），唯一 MISS `call` 为 C++ 模板机制（A 豁免）。

## 六、与 tools/xgui_api_scan.py 的联动建议

### 6.1 建议登记为正式 SKIP（35 条，措辞对齐既有 SKIP 表风格）

```
# --- gui 域首扫三分处置（2026-09-18）：无障碍注册表/钩子族 ---
"QAccessible.installFactory": "接口工厂注册表(InterfaceFactory)不做；XAccessible 由 createForWindow/createForWidget 直接构造",
"QAccessible.removeFactory": "同 installFactory",
"QAccessible.queryAccessibleInterface": "QObject→接口查询注册表不做；节点由 widget/window 直接挂接",
"QAccessible.uniqueId": "接口句柄唯一 id 注册表不做；可访问树经 parent/childAtIndex 遍历",
"QAccessible.accessibleInterface": "同 uniqueId",
"QAccessible.registerAccessibleInterface": "同 uniqueId",
"QAccessible.deleteAccessibleInterface": "同 uniqueId",
"QAccessible.installActivationObserver": "激活观察者注册表不做；启停经 XPlatformAccessibility_setActive 单点承载",
"QAccessible.removeActivationObserver": "同 installActivationObserver",
"QAccessible.qAccessibleTextBoundaryHelper": "文本边界计算内部辅助(供 AT-SPI 桥实现用)，非应用层 API",
"QAccessible.isActive": "静态启停由 XPlatformAccessibility_isActive/setActive 承载(平台桥)，节点类不设",
"QAccessible.setActive": "同 isActive",
"QAccessible.cleanup": "静态清理由 XPlatformAccessibility_cleanup 承载(平台桥)",
"QAccessible.updateAccessibility": "事件分发经 XPlatformAccessibility_notify 与 Drive 契约承载；全局钩子不做",
"QAccessible.installUpdateHandler": "全局回调钩子注册不做；事件分发经 Drive 契约承载",
"QAccessible.installRootObjectHandler": "同 installUpdateHandler",
"QAccessible.setRootObject": "应用根由 XPlatformAccessibility 内部创建拥有(createApplication)，不设 setter",
"QPlatformAccessibility.setRootObject": "应用根由桥内部拥有(m_root)，公开面为 root()",
# --- gui 域首扫三分处置（2026-09-18）：内部指针/绑定属性/低层描述族 ---
"*.data_ptr": "内部 DataPtr 数据指针访问器，非应用层 API（qicon/qimage/qpixmap/qpicture）",
"QPixmap.handle": "QPlatformPixmap 平台像素后端指针不暴露（XPixmap 自含光栅实现；对标 winId 豁免）",
"QMovie.bindableSpeed": "QProperty/QBindable 绑定属性体系不做（C API 以成对访问器承载）",
"QMovie.bindableCacheMode": "同 bindableSpeed",
"QImage.toPixelFormat": "QPixelFormat 低层像素描述体系未建，非应用层 API",
"QImage.toImageFormat": "同 toPixelFormat",
# --- gui 域首扫三分处置（2026-09-18）：平台专属桥接 ---
"QImage.fromHBITMAP": "Windows GDI 专属桥接(HBITMAP)，XGui 面向 Linux/X11，设计上不做",
"QImage.fromHICON": "Windows GDI 专属桥接(HICON)，同上",
"QImage.toHBITMAP": "Windows GDI 专属桥接，同上",
"QImage.toHICON": "Windows GDI 专属桥接，同上",
"QImage.toCGImage": "macOS CoreGraphics 专属桥接(CGImageRef)，同上不做",
# --- gui 域首扫三分处置（2026-09-18）：废弃 API / 设备对象体系 ---
"QShortcut.id": "Qt 6.0 已废弃(QT_DEPRECATED_VERSION_6_0)的快捷键 id 注册表访问器",
"QShortcut.parentWidget": "Qt 6.0 已废弃(建议用 parent())；XGui 以 XObject parent 承载",
"QWheelEvent.pointingDevice": "QPointingDevice 输入设备对象注册表未建(嵌入式单指针设备)；对标 QWidget.screen 豁免",
# --- gui 域首扫三分处置（2026-09-18）：文本引擎子集（45 条 QTextDocument 豁免按子体系登记，
#     此处列组键，逐条理由见本报告 3.14.1）---
"QTextDocument.begin / end / findBlock / findBlockByNumber / findBlockByLineNumber / firstBlock / lastBlock":
    "QTextBlock 迭代器/块对象体系未建；XGui 扁平块模型以 blockIndex 承载（块号==索引）",
"QTextDocument.frameAt / rootFrame": "QTextFrame 框架层级体系未建（扁平块模型）",
"QTextDocument.object / objectForFormat": "QTextObject 定制富对象体系未建",
"QTextDocument.allFormats": "QTextFormat 格式对象体系未建（富文本格式子集边界）",
"QTextDocument.defaultTextOption / setDefaultTextOption": "QTextOption 对象体系（换行模式/制表位）未建模",
"QTextDocument.defaultStyleSheet / setDefaultStyleSheet": "QCSS 级联样式表不做；内联 style 属性已承载（富文本子集边界）",
"QTextDocument.documentLayout / setDocumentLayout / drawContents / adjustSize / idealWidth / textWidth / setTextWidth / lineCount / isLayoutEnabled / setLayoutEnabled / useDesignMetrics / setUseDesignMetrics / revision / markContentsDirty / size":
    "排版引擎(QAbstractTextDocumentLayout)体系未建；渲染由 XTextEdit 自绘承载",
"QTextDocument.pageCount / pageSize / setPageSize / print": "分页/打印子系统不做（对齐 QPlainTextEdit.print）",
"QTextDocument.baselineOffset / setBaselineOffset / superScriptBaseline / setSuperScriptBaseline / subScriptBaseline / setSubScriptBaseline":
    "基线微调排版属性依赖排版引擎；上下标由 XTDCharFormat 布尔位承载",
"QTextDocument.defaultCursorMoveStyle / setDefaultCursorMoveStyle": "双向文本光标移动风格导航子集不做",
"QTextDocument.appendUndoItem": "QAbstractUndoItem 自定义撤销命令内部扩展钩子，非应用层 API",
```

（实际登记时按脚本 `Qt类.方法名` 单键展开；上表为分组示意。）

### 6.2 建议登记 RENAMED（7 键，消除 9 条 A′ 误报）

```python
RENAMED = {
    "QImage.QVariant":    {"to": "toVariant", "why": "operator QVariant 以 XImage_toVariant/fromVariant 承载(XImage.h:741)"},
    "QBitmap.QVariant":   {"to": "toVariant", "why": "同上(XBitmap.h:163)"},
    "QImage.colorTransformed": {"to": "applyColorTransform", "why": "XImage_applyColorTransform 语义等价(XImage.h:348)"},
    "QImage.convertTo":   {"to": "convertToFormatInPlace", "why": "原地格式转换(XImage.h:646)"},
    "QImage.convertedTo": {"to": "convertToFormat", "why": "命名返回版转换(XImage.h:624)"},
    "QImage.copy":        {"to": "copyRect", "why": "区域拷贝，规避 C 生命周期命名冲突(XImage.h:615)"},
    "QPixmap.copy":       {"to": "copyRect", "why": "同上(XPixmap.h:469)"},
    # QIconEngine.virtual_hook 与 QImageIOPlugin.create 建议走扫描器规则修复而非 RENAMED：
    # - virtual_hook：可加 snake_case→camelCase 名称归一（virtual_hook→virtualHook）；
    # - create：LIFECYCLE_NAMES 过滤应豁免 `X<cls>_create_base` 形态（虚工厂调度入口）。
}
```

### 6.3 扫描器改进（消除 2 条解析伪影 + 1 条过滤误报）

1. `parse_class_body.record()`：跳过以 `typedef`/`using` 开头的语句——
   可同时消除 `QAccessible.void`（来自 `typedef void(*UpdateHandler)(...)`，
   qaccessible_base.h:382）与 `QTextDocument.QVariant`（来自
   `using ResourceProvider = std::function<QVariant(...)>`，qtextdocument.h:207）
   两条伪影，且不必为 `void` 扩充 NON_API_NAMES。
2. `normalize_xgui_names`/LIFECYCLE_NAMES：`X<cls>_create_base` 为 Qt 纯虚
   工厂的调度入口（如 XImageIOPlugin_create_base），不应因名字含 `create`
   被过滤——建议在归一化后若名字为 `create_base`（或归一为 create 且来自
   `_base` 后缀）则保留计入满足集。
3. 可选：方法名匹配增加 snake_case 归一（`virtual_hook`→`virtualHook`），
   覆盖 Qt 少量下划线命名（qiconengine.h:50），避免逐条 RENAMED。

## 七、验证与边界说明

- 本报告为只读审计：仅核读缺口清单、扫描脚本、Qt 6.8.3 头文件与
  Src/XGui 头/源文件；未改动任何代码与脚本，未执行扫描器重跑。
- 每条 MISS 的 Qt 佐证行号均经 grep 实测（2026-09-18，Qt 6.8.3 Src 树）；
  XGui 现状引用均给出头文件与行号。
- 三分占比按条数计：129 = A 豁免 74 + A′ 已覆盖 9 + B 候选 37 + C 平台 9。
  A′ 9 条收口后，gui 域「真实待实现缺口」即第四节 37 条候选。
