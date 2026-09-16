# XGui ↔ Qt 6.8.3 Icon 模块对齐审计报告

- 审计日期：2026-09-15
- 审计性质：只读审计（未修改 `Src/`、`Test/` 任何源码，未 commit/push）
- 审计范围：`Src/XGui/Icon/` 全部 7 个头文件 + 7 个实现文件
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image/`
  （`qicon.h`、`qiconengine.h`、`qiconengineplugin.h`、私有 `qicon_p.h`、`qiconloader_p.h`，
  行为对照 `qicon.cpp`、`qiconengine.cpp`、`qiconloader.cpp`、`qcommonstyle.cpp`）

---

## 1. 模块概览

| X 文件 | 对标 Qt 类/模块 | 状态 | 继承（X） | 继承（Qt） | 1:1 |
|---|---|---|---|---|---|
| XIcon.h/.c | QIcon | 已实现，主体对齐 | XIcon→XClass | QIcon（无基类值类型，内部 QIconPrivate 引用计数） | ✅（项目惯例映射） |
| XIconEngine.h/.c | QIconEngine | 已实现，14 虚函数全覆盖 | XIconEngine→XClass | QIconEngine（抽象类） | ✅（惯例映射；纯虚差异见 3.2） |
| XIconEnginePlugin.h/.c | QIconEnginePlugin | 空壳，未接入任何加载流程 | XIconEnginePlugin→XObject | QIconEnginePlugin→QObject | ✅ |
| XIconThemeEngine.h/.c | QIconLoaderEngine / QThemeIconEngine（合并） | 已实现，行为主体对齐 | XIconThemeEngine→XIconEngine | QThemeIconEngine→QProxyIconEngine→QIconEngine；QIconLoaderEngine→QIconEngine | ❌（缺 QProxyIconEngine 中间层，两层合并为一类） |
| XIconThemeInternal.h/.c | QIconLoader / QIconTheme / QIconCacheGtkReader / 条目类型（内部） | 已实现 | 内部函数模块（ThemeContext/ThemeDirInfo 等价内部结构） | 内部类 | ✅（函数级等价） |
| XIconScaledPixmapCache.h/.c | QPixmapIconEngine::scaledPixmap / PixmapEntry::pixmap 的 QPixmapCache 键逻辑（Qt 6.8 无同名类） | 已实现 | 内部辅助模块 | — | ✅（内部） |
| XIconStyleHelper.h/.c | QCommonStyle::generatedIconPixmap / QApplicationPrivate::applyQIconStyleHelper | 已实现，逐像素等价 | 内部辅助模块 | — | ✅（内部） |

总体评价：**Icon 模块是 XGui 中完成度较高的模块**。XIcon 公共 API 覆盖度高（150 项
`ThemeIcon` 枚举与 Qt 6.8 逐项一致，经脚本核对 150/150 无差异）；主题解析实现了
`index.theme` 两遍解析、Inherits/dash 回退、Context 过滤、Fixed/Scalable/Threshold/
Fallback 目录语义、GTK `icon-theme.cache` 版本 1 读取、DPR 条目选择与缩放缓存、禁用/
选中样式态生成。主要问题集中在：主题 `isNull()` 的尺寸过滤偏差、文件图标 `name()`
与 Qt 语义不一致、插件工厂（XIconEnginePlugin）完全未接入、以及 3 个头文件丢失
UTF-8 BOM。

---

## 2. 逐类对比

### 2.1 XIcon ↔ QIcon（qtbase/src/gui/image/qicon.h）

#### 继承
- X：`XIcon → XClass`（结构体第一成员 `XClass m_class`，`XCLASS_DEFINE_BEGING/EXTEND_END(XIcon, XClass)`，`XIcon_class_init` 中 `XVTABLE_INHERIT_XCLASS(XClass)`）。
- Qt：`QIcon` 为无基类值类型，内部 `QIconPrivate`（`qicon_p.h`）持有 `QIconEngine*` + 原子引用计数。
- 判定：✅ 匹配（XClass 是项目对所有对象的统一基座，XIconPrivate 对应 QIconPrivate）。

#### API 覆盖
| Qt 公共 API | X 对应 | 结论 |
|---|---|---|
| QIcon() / QIcon(QPixmap) / QIcon(QString) / QIcon(QIconEngine*) | XIcon_init / XIcon_init_pixmap / XIcon_init_file(+_2) / XIcon_init_engine | ✅ |
| 拷贝/移动构造、operator=、析构 | vtable Copy/Move/Deinit（XCopy/XMove 走虚表） | ✅（无独立 XIcon_copy/move 公开函数，符合项目惯例） |
| swap() | ❌ 无 XIcon_swap | ⚠️ API 缺口 |
| pixmap(QSize) / pixmap(w,h) / pixmap(extent) | XIcon_pixmap / XIcon_pixmapExtent | ✅ |
| pixmap(QSize, qreal dpr) | XIcon_pixmapRatio | ✅（float dpr） |
| actualSize(QSize) / actualSize(QSize, dpr) | XIcon_actualSize / XIcon_actualSizeRatio | ✅ |
| name() | XIcon_name / XIcon_name_const / XIcon_name_2 | ✅ 接口齐；⚠️ 语义偏差见下 |
| paint(QPainter*, QRect, alignment, mode, state) | XIcon_paint(void* painter, x,y,w,h, alignment, mode, state) | ✅ |
| isNull / isDetached / detach / cacheKey | 同名 | ✅ |
| addPixmap / addFile | XIcon_addPixmap / XIcon_addFile(+_2) | ✅ |
| availableSizes | XIcon_availableSizes | ✅ |
| setIsMask / isMask | 同名 | ✅ |
| fromTheme(name) / (name, fallback) / hasThemeIcon(name) | XIcon_fromTheme(+_2) / XIcon_fromThemeIcon / XIcon_hasThemeIcon(+_2/Type) | ✅ |
| themeSearchPaths/setThemeSearchPaths、fallbackSearchPaths/setFallbackSearchPaths | 同名（含 `_2`） | ✅ 接口齐；⚠️ `_2` 违规见下 |
| themeName/setThemeName、fallbackThemeName/setFallbackThemeName | 同名（含 `_2`） | ✅ |
| ThemeIcon 枚举（150 项） | XIconThemeIcon（150 项逐一一致 + 22 项 Legacy 扩展） | ✅ |
| operator QVariant | 无 | ⚠️ 项目无 QVariant，豁免 |
| QDataStream << / >>（icon 级序列化） | ❌ 无 | ⚠️ API 缺口 |
| pixmap(QWindow*) / actualSize(QWindow*)（Qt 6.0 弃用） | 无 | ✅ 弃用 API 不实现合理 |

**API 缺口（2 项）**：
1. `void swap(QIcon &other) noexcept`（qicon.h:193）——无 XIcon_swap。
2. icon 级 `QDataStream &operator<< / operator>>`（qicon.h:272-273，QT_NO_DATASTREAM 之外）——XIcon 无序列化入口（XIconThemeEngine 有引擎级 read/write，但默认像素图引擎没有）。

#### 功能缺口
1. **`name()` 语义偏差（P1）**：`XIcon_init_file`（XIcon.c:560）把文件名存入 `m_name`，`XIcon_name()` 对文件图标返回文件名；Qt 6.8 `QIcon::name()` 返回 `engine->iconName()`，`QPixmapIconEngine` 未重载 `iconName()`，**文件图标返回空串**（qicon.cpp:1203-1216）。X 头文件注释自称“对标 Qt”，实际是 X 扩展行为。
2. **`pixmap()`/`actualSize()` 不使用应用全局 DPR（P2）**：Qt 无参 `pixmap()` 内部用 `qApp->devicePixelRatio()`（qicon.cpp:868-945），X 的 `XIcon_pixmap`/`XIcon_actualSize` 固定 DPR=1，仅显式 `XIcon_pixmapRatio`/`XIcon_actualSizeRatio` 提供 DPR 路径；应用 DPR>1 时默认重载与 Qt 行为不一致。
3. **`qtIconCache` 未实现（P2）**：Qt `fromTheme()` 按名称缓存 QIcon（qicon.cpp:1376-1390）；X 每次 `XIcon_fromTheme` 新建 `XIconThemeEngine`，无对象级缓存（XGui.md 10.38 已注明）。
4. **`detach()` 缺少 Qt 的 isNull 分支（P3）**：Qt 在引擎 isNull 时把 `d` 置 nullptr（qicon.cpp:1073-1087）；X 的 `XIcon_detach` 保留空私有数据，且 `detach_no` 未参与 cacheKey（X 用 serial 常量替代）。
5. **ICO 深度优先未实现（P3）**：Qt `QPixmapIconEngine::addFile` 对 ICO 按 `origIcoDepth` 同尺寸择优（qicon.cpp:443-490）；X 走通用多帧枚举，无深度优选。
6. **addFile 无插件引擎选择（P1）**：Qt `QIcon::addFile` 先按后缀/MIME 经 `iconEngineFromSuffix` 找 `QIconEnginePlugin`（qicon.cpp:1109-1181）；X 只走内置 XImageReader，XIconEnginePlugin 无调用方。

#### 违规
- ⚠️ **`_2` 后缀滥用（P1，风格文档 779-783 条）**：`XIcon_themeSearchPaths_2`、`XIcon_fallbackSearchPaths_2`、`XIcon_setThemeSearchPaths_2`、`XIcon_setFallbackSearchPaths_2`（XIcon.h:576/588/599/611）与主版本**完全相同签名**（`XStringList*`），不是 `const char*` UTF-8 重载；风格文档规定 `_2` 只表示 UTF-8 `const char*` 兼容重载，且只负责转发。
- ⚠️ **空 Doxygen 注释块（P2）**：XIcon.h:292-317 有 5 处“复制构造函数/移动构造函数/释放资源/虚函数调度”注释块，其后无任何函数声明（复制/移动走 vtable，公开头无入口说明）。
- ⚠️ **`static char name[2][128]` 长期持有平台主题名（P2，规则 1 边界）**：XIcon.c:133 为进程级静态缓冲；持有内容实际由 `g_iconThemeName`（XString*）保存，缓冲仅为平台名暂存/借用返回，但 `XIcon_themeName_2`/`XIcon_fallbackThemeName_2` 直接返回其指针，属于规则 1 例外清单之外的长期 char[N]。

---

### 2.2 XIconEngine ↔ QIconEngine（qtbase/src/gui/image/qiconengine.h）

#### 继承
- X：`XIconEngine → XClass`；Qt：`QIconEngine` 无基类抽象类。
- 判定：✅（惯例映射）。

#### API 覆盖
| Qt 虚函数 | X 槽位 | 结论 |
|---|---|---|
| paint()（纯虚） | EXIconEngine_Paint（默认空实现） | ⚠️ 见功能缺口 |
| actualSize() | EXIconEngine_ActualSize（默认返回请求尺寸） | ✅ |
| pixmap() | EXIconEngine_Pixmap（默认建图调 paint） | ✅ |
| addPixmap() / addFile() | 默认空实现 | ✅（Qt 默认亦空） |
| key() | 默认空 XString | ✅ |
| clone()（纯虚） | 默认 NULL | ⚠️ 见功能缺口 |
| read() / write() | 默认 false | ✅ |
| availableSizes() | 默认清空 out | ✅ |
| iconName() | 默认空 | ✅ |
| isNull() | 经 IsNullHook | ✅ |
| scaledPixmap() | 经 ScaledPixmapHook | ✅ |
| virtual_hook() | 默认处理 ScaledPixmapHook | ✅ |
| IconEngineHook（IsNullHook=3, ScaledPixmapHook=4） | 数值一致 | ✅ |
| ScaledPixmapArgument | XIconEngineScaledPixmapArgument（pixmap 为 XPixmap* 输出指针） | ✅ |

虚表槽位顺序与 Qt 声明顺序一致（14 槽），`XCLASS_DEFINE_ENUM(XIconEngine, Paint) = XCLASS_VTABLE_GET_SIZE(XClass)` 正确。

#### 功能缺口
1. **`paint` 默认空实现且基类可实例化（P2）**：Qt `paint` 为纯虚、`QIconEngine` 不可实例化；X 的 `VXIconEngine_paint` 是空函数体，`XIconEngine_create_ex` 可创建“画了等于没画”的空引擎，调用方无编译期/运行期提示。
2. **`clone` 默认返回 NULL（P2）**：Qt 为纯虚；X 默认 NULL 可视为“抽象”近似，但同样允许实例化。

#### 违规
- 无（基类抽象性差异归入功能缺口，建议在头文件注明“默认空实现，派生类必须重载 Paint”）。

---

### 2.3 XIconEnginePlugin ↔ QIconEnginePlugin（qtbase/src/gui/image/qiconengineplugin.h）

#### 继承
- X：`XIconEnginePlugin → XObject`；Qt：`QIconEnginePlugin → QObject`。
- 判定：✅。

#### API 覆盖
| Qt | X | 结论 |
|---|---|---|
| `create(const QString& = {})`（纯虚） | EXIconEnginePlugin_Create（默认 NULL）、XIconEnginePlugin_createEngine_base / _2 / create_base / _2 | ✅ 虚函数有；⚠️ 入口冗余 |
| IID `org.qt-project.Qt.QIconEngineFactoryInterface` | `XICONENGINEPLUGIN_IID` 同串 | ✅ |
| QObject 构造 parent 参数 | XObject_init 无 parent | ✅ 项目惯例豁免 |

#### 功能缺口
1. **默认 create 返回 NULL（纯虚占位）（P2）**：无任何派生插件实现。
2. **模块内零调用（P1）**：全仓 grep 除 Icon 目录自身外无任何 `XIconEnginePlugin` 引用；Qt 的 `QIconLoader::iconEngine()`（qiconloader.cpp:652-684）与 `QIcon::addFile` 的 `iconEngineFromSuffix` 都会先走插件工厂，X 的插件机制是死代码。

#### 违规
- ⚠️ **四重冗余入口（P2，风格文档子类 API 复用规范）**：`createEngine_base`、`createEngine_2_base`、`create_base`、`create_2_base` 均为同一虚函数别名（XIconEnginePlugin.h:66-94），建议只保留 `create_base` + `create_2_base`。

---

### 2.4 XIconThemeEngine ↔ QIconLoaderEngine / QThemeIconEngine（qtbase/src/gui/image/qiconloader_p.h）

#### 继承
- X：`XIconThemeEngine → XIconEngine`（m_base 嵌 XIconEngine 为第一成员）。
- Qt：`QThemeIconEngine → QProxyIconEngine → QIconEngine`；内部代理 `QIconLoaderEngine → QIconEngine`。
- 判定：❌ **非 1:1**。缺 `QProxyIconEngine` 中间基类，且 `QThemeIconEngine`（对外壳）与 `QIconLoaderEngine`（实际查找引擎）两层合并为 X 单类。X 的 key 返回 `"QThemeIconEngine"`、read/write 只序列化图标名（对应 QThemeIconEngine），而 paint/pixmap/actualSize/isNull 等行为对应 QIconLoaderEngine——是“两引擎合一”设计。

#### API 覆盖
14 个 QIconEngine 虚函数全部重载（Paint/ActualSize/Pixmap/AddPixmap/AddFile/Key/Clone/Read/Write/AvailableSizes/IconName/IsNull/ScaledPixmap/VirtualHook），并重载 Copy/Move/Deinit；`iconName()` 走 `XIconInternal_resolveThemeIconName` 返回实际命中名称（对标 `m_info.iconName`），`key()` 返回 `"QThemeIconEngine"`（与 xgui_regression_test.c:7739 断言一致；**XGui.md 10.187/10.192 记录“key 固定为 QIconLoaderEngine”已过时**）。

#### 功能缺口
1. **`isNull()`/`themeHasIcon()` 按 48px 目录匹配过滤（P1）**：Qt `QIconLoaderEngine::isNull()` 只判 `m_info.entries.empty()`（qiconloader.cpp:964-967），条目收集不按请求尺寸过滤；X 的 `theme_searchThemeExists`（XIconThemeInternal.c:1708-1714）要求目录 `directoryMatches(48,1)`，因此**仅存在于 16x16/512x512 等目录的图标会被 X 误报为 null**，`XIcon_hasThemeIcon` 同样受影响。
2. **无 themeKey 失效重建代理（P2）**：Qt `QThemeIconEngine::proxiedEngine()` 在 `QIconLoader::themeKey()` 变化时重建底层引擎（qiconloader.cpp:740-762）；X 每次取图重新解析，靠 `XIconScaledPixmapCache_clear()` 兜底缓存（XGui.md 已注明，属设计简化）。
3. **`state` 参数被忽略（P3）**：X 的 paint/scaledPixmap 忽略 `XIconState`；Qt `ScalableEntry::pixmap` 会把 state 传给 svg `QIcon`（qiconloader.cpp:940-945）。
4. **`VirtualHook` 重载为空（P3）**：直接调用 `XIconEngine_virtualHook_base(themeEngine, ScaledPixmapHook, ...)` 无效果（Qt 走代理到基类默认实现）。

#### 违规
- ⚠️ **继承非 1:1（规则 2）**：QProxyIconEngine 中间层缺失、QThemeIconEngine/QIconLoaderEngine 合并（设计取舍，需在 XGui.md 明确记录为有意简化；若严格 1:1 需拆类）。

---

### 2.5 XIconThemeInternal（内部，对应 QIconLoader / QIconTheme / QIconCacheGtkReader / 条目类型）

非类模块，提供 10 个 `XIconInternal_*` 内部入口。对照 Qt 实现情况：

| 功能 | Qt 依据 | X 状态 |
|---|---|---|
| index.theme 两遍解析（Directories/Inherits + 目录元数据） | qiconloader.cpp:347-420 | ✅ |
| 父主题顺序 Inherits→fallback→hicolor、防循环 | qiconloader.cpp:422-436, 446-570 | ✅ |
| dash 通用回退（逐级截断） | qiconloader.cpp:557-566 | ✅ |
| Context=Applications/MimeTypes 通用回退跳过 | qiconloader.cpp:517-535 | ✅ |
| Fixed/Scalable/Threshold/Fallback + Scale 精确匹配 + 距离计算 | qiconloader.cpp:794-847 | ✅ |
| entryForSize 先精确后最小距离 | qiconloader.cpp:849-880 | ✅（含格式优先级 png>svg>xpm>bmp） |
| lookupFallbackIcon 独立文件 png/xpm/svg | qiconloader.cpp:572-609 | ✅ |
| icon-theme.cache（GTK 缓存版本 1，大端哈希桶） | qiconloader.cpp:213-345 | ✅（XIconThemeInternal.c:174-436） |
| availableSizes 登记语义（不去重，Fallback 委托文件尺寸） | qiconloader.cpp:976-995 | ✅ |

#### 功能缺口
1. **主题内格式受限 png/svg/xpm/bmp（P2）**：Qt 经 QImageReader 支持全部已注册格式（含 gif/jpg 等）；X 硬编码 4 种 + 空扩展名（裁剪回退可接受，需注明）。
2. **isNull 入口 48px 过滤（P1）**：同上 2.4-1（`XIconInternal_themeHasIcon` 经 `theme_searchThemeExists`）。
3. **GTK 缓存时间戳秒级粒度（P3）**：`theme_fileModified` 用秒；Qt 高精度 QDateTime，同一秒内修改不立即失效（XGui.md 10.168 已注明）。
4. **死代码（P3）**：`XIconInternal_themeHasScalable` 与 `XIconInternal_resolveThemePixmap`（无任何调用方）。

#### 违规
- ⚠️ **头文件无 UTF-8 BOM（规则 4）**：`XIconThemeInternal.h` 首字节为 `#if`，非 `\xEF\xBB\xBF`。
- ⚠️ **头文件注释以英文为主、无文件级中文 Doxygen（内部头，低优先）**。

---

### 2.6 XIconScaledPixmapCache（内部，对应 QPixmapCache 键逻辑；Qt 6.8 无同名类）

- 任务背景中的 “QIconPrivate::ScaledPixmapCache” 在 Qt 6.8.3 中不存在（grep qicon_p.h/qicon.cpp 无此类）；实际对应 `QPixmapIconEngine::scaledPixmap`（qicon.cpp:332-383）与 `PixmapEntry::pixmap`（qiconloader.cpp:903-985）内联的 QPixmapCache 键逻辑。
- X 键格式：`前缀 + sourceKey + paletteKey + mode + 宽 + 高 + dprThousand`；默认引擎前缀 `qt_icon_scale/`，主题前缀 `qt_icon_theme/`（Qt 分别为 `"qt_" + hex(...)` 与 `"$qt_theme_" + hex(basePixmap.cacheKey)`，键结构等价、格式不同，属内部实现自由）。

#### 功能缺口
1. **键缓冲 320B（P3）**：超长主题图标名会导致 `cacheKeyBuild` 失败，静默退化为不缓存。
2. **主题键用图标名而非源 pixmap cacheKey（P2）**：Qt 用 `basePixmap.cacheKey()`（文件身份）；X 用图标名，主题文件内容变化（不换主题名）时旧缓存可被复用，只能靠 `clear()` 兜底。
3. **`clear()` 全局清空 XPixmapCache（P3）**：Qt 主题变更只 `invalidateKey()` 不全局清缓存；X 更激进但安全。

#### 违规
- ⚠️ **头文件无 UTF-8 BOM（规则 4）**：`XIconScaledPixmapCache.h` 首字节为 `/*`。

---

### 2.7 XIconStyleHelper（内部，对应 QCommonStyle::generatedIconPixmap）

逐像素核对 qcommonstyle.cpp:6159-6225：

| 步骤 | Qt | X | 结论 |
|---|---|---|---|
| Disabled 颜色表 | `(red*(i<<1))>>8`，后段 `qMin(red+(i<<1),255)` | 相同 | ✅ |
| 背景强度 | `qt_intensity = (77r+150g+28b)/255` | 相同 | ✅ |
| 亮度调整 | factor=191 高亮 +91、低亮 -51 | 相同 | ✅ |
| 灰度索引 | `qGray(pixel)/3 + (130 - intensity/3)`，qGray=(11r+16g+5b)>>5 | 相同 | ✅ |
| alpha 保留 | qRgba(..., qAlpha(pixel)) | 相同 | ✅ |
| Selected | Highlight alpha 0.3、SourceAtop 预乘合成、保留目标 alpha | 逐像素等价 | ✅ |

#### 功能缺口
- 无（调色板不可用时回退原样，属合理裁剪行为）。

#### 违规
- ⚠️ **头文件无 UTF-8 BOM（规则 4）**：`XIconStyleHelper.h` 首字节为 `/*`。

---

## 3. 缺失 Qt 类清单

| Qt 类 | Qt 头文件（qtbase/src/gui/image/） | 建议 |
|---|---|---|
| QProxyIconEngine | qiconengine_p.h | 暂不实现（已并入 XIconThemeEngine）；继承链标注非 1:1，需确认是否接受合并设计 |
| QPixmapIconEngine | qicon_p.h | 暂不实现（功能内嵌 XIconPrivate 条目引擎）；如需插件序列化再抽出为类 |
| QPixmapIconEngineEntry | qicon_p.h | 不实现（XIconEntry 等价） |
| QIconPrivate | qicon_p.h | 不实现（XIconPrivate 等价） |
| QIconLoader | qiconloader_p.h | 不实现（XIconThemeInternal 函数集等价） |
| QIconLoaderEngine | qiconloader_p.h | 已由 XIconThemeEngine 覆盖（合并） |
| QThemeIconEngine | qiconloader_p.h | 已由 XIconThemeEngine 覆盖（合并） |
| QIconTheme | qiconloader_p.h | 不实现（ThemeContext 等价） |
| QIconDirInfo | qiconloader_p.h | 不实现（ThemeDirInfo 等价） |
| QIconLoaderEngineEntry / ScalableEntry / PixmapEntry | qiconloader_p.h | 不实现类；但“按任意尺寸登记条目”的语义需修正（见 2.4-1） |
| QIconCacheGtkReader | qiconloader_p.h | 不实现（theme_cacheDirState 等价） |
| QAbstractFileIconEngine | qabstractfileiconengine_p.h | 暂不实现（QFileIconProvider 场景未启用） |
| QAbstractFileIconProvider | qabstractfileiconprovider.h | 后续按需实现或明确标注不实现 |

---

## 4. 硬约束核查结论

| # | 约束 | 结论 |
|---|---|---|
| 1 | 拥有型字符串一律 XString* | ✅ 模块内长期持有均为 XString*；唯一边界为 XIcon.c:133 `static char name[2][128]`（平台主题名暂存，见 2.1 违规） |
| 2 | 继承一比一含中间基类 | ❌ XIconThemeEngine 缺 QProxyIconEngine 中间层，QThemeIconEngine/QIconLoaderEngine 合并（见 2.4 违规） |
| 3 | 样式/绘制完整复刻 | ✅ 样式助手逐像素一致；主题绘制/条目选择主体复刻；偏差见 2.4-1（isNull 尺寸过滤） |
| 4 | 公共头中文 Doxygen + UTF-8 BOM | ❌ **3 个头文件丢失 BOM**：`XIconScaledPixmapCache.h`、`XIconStyleHelper.h`、`XIconThemeInternal.h`（风格文档 1071 行：“所有 .c/.h 必须使用 UTF-8 BOM”）；XIcon.h 有空注释块 |
| 5 | 信号空参 args=NULL | ✅ N/A（QIcon/QIconEngine/QIconEnginePlugin 在 Qt 中均无信号） |
| 6 | init/deinit 成对、copy/move 安全、禁 memcpy/malloc | ✅ XIcon/XIconEngine/XIconEnginePlugin/XIconThemeEngine 均成对；Copy/Move 走 XCopy/XMove 虚表；无 memcpy 复制对象；无裸 malloc/free/strdup（grep 全模块 0 命中） |
| 7 | 旧 API 不保留 | ⚠️ 22 项 Legacy XIconThemeIcon 枚举为旧名兼容扩展（不参与 Qt 序号，与 Qt 不冲突）；`_2` 同签名重复 4 个入口违反命名规范（见 2.1 违规） |
| 8 | C99 | ✅ 仅 C99 语法（`for (int ...)`、复合字面量），无 C11/C++ 特性 |

---

## 5. 优先任务建议

按优先级排序：

1. **【P0·格式】补齐 3 个头文件 UTF-8 BOM**：`XIconScaledPixmapCache.h`、`XIconStyleHelper.h`、`XIconThemeInternal.h` 文件头加 `\xEF\xBB\xBF`。
2. **【P1·功能】修正主题 isNull/hasThemeIcon 尺寸过滤**：`theme_searchThemeExists` 移除 `directoryMatches(48,1)` 过滤，改为“文件存在即登记”（对标 `entries.empty()`），或为 isNull 单独走无尺寸过滤的登记查询。
3. **【P1·功能】对齐 `name()` 语义**：文件图标 `XIcon_name` 返回空串（与 Qt 一致）；如需文件名查询另设 `XIcon_fileName_*` 接口。
4. **【P1·功能】接入 XIconEnginePlugin**：`XIcon_addFile` 按后缀/MIME 选插件引擎、`XIcon_fromTheme` 走插件工厂（对标 `iconEngineFromSuffix`/`QIconLoader::iconEngine`），并给出至少一个真实插件实现（如 SVG）。
5. **【P1·规范】清理 `_2` 同签名重复 API**：删除或改造 `XIcon_themeSearchPaths_2`、`XIcon_fallbackSearchPaths_2`、`XIcon_setThemeSearchPaths_2`、`XIcon_setFallbackSearchPaths_2` 四个非 UTF-8 重载。
6. **【P2·API】补齐 `XIcon_swap` 与 icon 级序列化**：或明确降级不实现并在文档记录。
7. **【P2·功能】`XIcon_pixmap`/`XIcon_actualSize` 接入应用全局 DPR**（`XGuiApplication_devicePixelRatio`），避免高 DPI 屏上默认重载与 Qt 行为不一致。
8. **【P2·设计】明确并文档化 XIconThemeEngine 合并 QProxyIconEngine/QThemeIconEngine/QIconLoaderEngine 的取舍**；如需严格 1:1 再拆类。
9. **【P2·功能】评估 qtIconCache 对象缓存**：`fromTheme` 名称级缓存（可复刻 Qt 的 QIconCache 语义或明确不做）。
10. **【P3·清理】删除死代码与过时文档**：`XIconInternal_themeHasScalable`、`XIconInternal_resolveThemePixmap`；更新 XGui.md 10.187/10.192（key 已为 QThemeIconEngine）与 10.259（read/write 已实现）等过时记录。

---

*报告生成：只读审计，未改动任何 Src/Test 文件。*
