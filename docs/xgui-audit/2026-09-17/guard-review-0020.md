# XGui 头文件特性守卫复核报告(guard-review-0020)

- 日期:2026-09-17
- 范围:`Src/XGui` 全部 168 个头文件(只读复核,未改动任何源码)
- 背景:前次审计发现约 20 个头"整体无特性守卫",集中在 Graphics 与 Icon 系。本次逐个复核是否需要补守卫。

## 一、总览

| 分类 | 数量 | 说明 |
| --- | --- | --- |
| 有特性守卫(`#if XXX_ON`) | 148 | 含 GPU 系 2 头(`XPLATFORMINTEGRATION_ON && XGPU_ON`) |
| 无特性守卫 | 20 | 即本次复核对象,见明细表 |
| 缺 UTF-8 BOM(全量扫描) | 12 | 20 个复核对象全部有 BOM;缺 BOM 的另有 12 头,见第四节 |

## 二、20 个无守卫头明细

结论口径:无需守卫(公共类型/基类头,补守卫会破坏裁剪链)| 建议补守卫(独立可裁剪,链路安全)| 待讨论(需整体方案,单补会破坏链路)。

| 头文件 | 有无守卫 | 对应开关 | 结论 | BOM |
| --- | --- | --- | --- | --- |
| Graphics/XColorSpace.h | 无 | 无(无需) | 无需守卫:公共色彩空间值类型;被 XSurfaceFormat.h(XSURFACEFORMAT_ON)与 XImage.h 在各自守卫区之外无条件 include,补守卫必破坏裁剪链 | 有 |
| Graphics/XImageFormat.h | 无 | 无(无需) | 无需守卫:公共图像格式枚举;被 XPixmap.h/XImage.h/XImageReader.h/XImageIOHandler.h 无条件引用 | 有 |
| Graphics/XImageIOHandler.h | 无 | XIMAGEIOPLUGIN_ON(已存在,.c 实现层生效) | 无需守卫:XGuiConfig.h:56 已定义 XIMAGEIOPLUGIN_ON,且 XImageReader.c/XImageWriter.c/XMovie.c/XImagePluginRegistry.c/XImageBuiltinPlugin.c 全部已在实现层按它降级(返回 NULL/走内置单帧路径);头保持 API 可见是既定模式,XIcon.c、XPixmap.c、XMovie.h 无条件使用这些 API,补头守卫反而破坏链路 | 有 |
| Graphics/XImageIOPlugin.h | 无 | XIMAGEIOPLUGIN_ON(同上) | 无需守卫:同上,实现层已按开关降级 | 有 |
| Graphics/XImagePluginRegistry.h | 无 | XIMAGEIOPLUGIN_ON(同上) | 无需守卫:仅 .c 引用,实现层已有开关;API 需对 XImageReader.c 等恒可见 | 有 |
| Graphics/XImageReader.h | 无 | XIMAGEIOPLUGIN_ON(不适用) | 无需守卫:XImageReader 有独立于插件开关的内置单帧读取路径(`#if !XIMAGEIOPLUGIN_ON` 分支),XIcon.c/XPixmap.c/XMovie 无条件使用;绝不能用 XIMAGEIOPLUGIN_ON 守卫 | 有 |
| Graphics/XImageWriter.h | 无 | XIMAGEIOPLUGIN_ON(同上) | 无需守卫:仅自身 .c include,实现层已有开关降级,保持与 Reader 对称 | 有 |
| Graphics/XImageBuiltinPlugin.h | 无 | XIMAGEIOPLUGIN_ON(同上) | 无需守卫:头注释明确 instance() 在开关关闭时返回 NULL,属"API 恒可见、实现降级"设计的一部分 | 有 |
| Graphics/XMovie.h | 无 | 无(建议新增 XMOVIE_ON) | 建议补守卫:仅 XLabel.c 与自身 .c 引用,无任何有守卫头 include;XLabel.h 已有自备前向声明(`typedef struct XMovie XMovie;`),补守卫不破坏头链;需联动 XLabel.h 的 movie()/setMovie() 与 XLabel.c 约 4 处使用点(XBackingStore 回退模式为先例) | 有 |
| Graphics/XPixmapCache.h | 无 | 无(建议新增 XPIXMAPCACHE_ON) | 建议补守卫:仅 XPixmapCache.c 与 XIconScaledPixmapCache.c 引用,头链零风险;需同步包裹 XIconScaledPixmapCache.c 的 3 个使用点(find/insert/clear) | 有 |
| Icon/XIcon.h | 无 | 无(XICON_ON 全仓不存在) | 待讨论:核心公共类型,被 5 个有守卫头在各自主守卫之外无条件 include(见第三节);windowIcon 类 API 遍布 Widget/Window,单独补守卫必破坏裁剪链,裁剪须整体方案 | 有 |
| Icon/XIconEngine.h | 无 | 无(随 XICON_ON) | 待讨论:XIconEnginePlugin.h/XIconThemeEngine.h/XSvgIconEngine.h 与 XIcon.c 无条件引用,XIcon.c 直接使用引擎路径 | 有 |
| Icon/XIconEnginePlugin.h | 无 | 无(随 XICON_ON) | 待讨论:被 XSvgIconEnginePlugin.h 引用,属引擎插件基类 | 有 |
| Icon/XIconScaledPixmapCache.h | 无 | 无(随 XICON_ON) | 待讨论:XIcon.c/XIconThemeEngine.c 无条件引用 | 有 |
| Icon/XIconStyleHelper.h | 无 | 无(随 XICON_ON) | 待讨论:XIcon.c/XIconThemeEngine.c 无条件引用 | 有 |
| Icon/XIconThemeEngine.h | 无 | 无(随 XICON_ON) | 待讨论:XIcon.c 的 fromTheme 路径无条件使用 | 有 |
| Icon/XIconThemeInternal.h | 无 | 无(随 XICON_ON) | 待讨论:XIcon.c/XIconThemeEngine.c 无条件引用 | 有 |
| Icon/XSvgIconEngine.h | 无 | 无(建议新增 XSVGICON_ON) | 建议补守卫:全仓零外部引用(仅自身 2 个 .c),无自动注册,独立可裁剪,链路零风险 | 有 |
| Icon/XSvgIconEnginePlugin.h | 无 | 无(建议新增 XSVGICON_ON) | 建议补守卫:仅 XSvgIconEnginePlugin.c 引用,同上 | 有 |
| XAlignment.h | 无 | 无(无需) | 无需守卫:纯枚举值头(对齐标志),被 XLabel.h/XProgressBar.h/XLineEdit.h/XGroupBox.h/XLayoutItem.h 等 16 处跨 Widget/Style/Layout 引用,属公共值类型 | 有 |

## 三、重点核对

### 1. GPU 系与 XGPU_ON — 已合规
- `Graphics/XGpuRenderBackend.h`、`Graphics/XGpuRenderDriver.h` 均有守卫 `#if XPLATFORMINTEGRATION_ON && XGPU_ON`。
- `XGuiConfig.h:68` 定义 `XGPU_ON`(默认 1),`XGUI_ON=0` 时连带置 0(XGuiConfig.h:646)。无需动作。

### 2. Icon 系与 XICON_ON — 开关不存在
- 全仓(含 XGuiConfig.h、CXinYueConfig.h)搜不到 `XICON_ON`,审计提到的 XICON_ON 从未定义、从未被引用。
- `#include "XIcon.h"` 位于 5 个有守卫头的主守卫之外(无条件):
  - `Widget/XWidget.h:99`(主守卫在其后)
  - `Window/XWindow.h:34`(`#if XWINDOW_ON` 在其后)
  - `Application/XGuiApplication.h:45`(`#if XGUIAPPLICATION_ON` 在 69 行)
  - `Platform/XPlatformIntegration.h:59`(`#if XPLATFORMINTEGRATION_ON` 在 128 行)
  - `Widget/XAbstractButton.h:38`(守卫在 43 行)
- 另有 XIcon.c 无条件使用 XIconEngine/XIconThemeEngine/XIconThemeInternal/XIconScaledPixmapCache/XIconStyleHelper(fromTheme 与文件加载)。
- 结论:Icon 核心 7 头为事实核心;若要支持裁剪,须一次性引入 XICON_ON(7 头内容守卫 + typedef 回退 + 5 核心头回退声明 + Icon 及相关 Widget .c 条件编译),改动面大 → 待讨论。

### 3. XImage/XPixmap/XPainter 与 XIMAGE_ON — XIMAGE_ON 不存在
- 全仓不存在 `XIMAGE_ON`。实际开关是 `XPAINTDEVICE_ON`(XGuiConfig.h:181):XImage.h/XPixmap.h/XPaintDevice.h/XBitmap.h/XPicture.h/XBackingStore.h 均用它;但注意 XImage.h/XPixmap.h 的 `#if XPAINTDEVICE_ON` 只包裹 paintDevice 访问器等少量条目,类型定义本体恒可见(公共类型头模式),与本次 9 个"无需守卫"判定同构。
- XPainter.h 挂 `XPAINTER_ON` 族(XGuiConfig.h:184 起),GPU 相关段落另有 `XPLATFORMINTEGRATION_ON && XGPU_ON` 守卫,链路自洽。

## 四、UTF-8 BOM 核查

20 个复核对象**全部**有 BOM(前 3 字节 ef bb bf)。全量 168 头扫描另发现 **12 个缺 BOM**(均以 `/*` 或 `#if` 开头):

| 文件 | 前 3 字节 |
| --- | --- |
| Src/XGui/Style/XStyle.h | `#if` |
| Src/XGui/Widget/XAbstractScrollArea.h | `/*` |
| Src/XGui/Widget/XCompleter.h | `/*` |
| Src/XGui/Widget/XGraphicsEffect.h | `/*` |
| Src/XGui/Widget/XKeySequenceEdit.h | `/*` |
| Src/XGui/Widget/XLineEdit.h | `/*` |
| Src/XGui/Widget/XMenuBar.h | `/*` |
| Src/XGui/Widget/XMenu.h | `/*` |
| Src/XGui/Widget/XProgressDialog.h | `/*` |
| Src/XGui/Widget/XTextBrowser.h | `/*` |
| Src/XGui/Widget/XWidget.h | `/*` |
| Src/XGui/Widget/XWizard.h | `/*` |

## 五、总结论与建议批次

**总结论**:20 个无守卫头中,9 个属于"公共类型/API 恒可见 + 实现层已由既有开关(XIMAGEIOPLUGIN_ON 等)降级"的既定设计,补守卫反而破坏裁剪链,维持现状;7 个 Icon 核心头与 XICON_ON 缺位问题需整体方案;真正可立即补守卫的是 4 个独立可裁剪头。

| 批次 | 内容 | 风险 |
| --- | --- | --- |
| 批次 1(零风险) | 新增 `XSVGICON_ON`(XGuiConfig.h 定义 + `#if` 包裹 XSvgIconEngine.h/XSvgIconEnginePlugin.h + 构建条件剔除 2 个 .c) | 低:零外部引用 |
| 批次 2(低风险,需少量联动) | 新增 `XPIXMAPCACHE_ON`(联动 XIconScaledPixmapCache.c 3 处);新增 `XMOVIE_ON`(联动 XLabel.h movie API + XLabel.c 约 4 处,参照 XBackingStore 回退模式) | 中低:仅 .c/.h 内部封装 |
| 待讨论(不建议本轮) | Icon 核心 7 头(XIcon/XIconEngine/XIconEnginePlugin/XIconScaledPixmapCache/XIconStyleHelper/XIconThemeEngine/XIconThemeInternal)是否引入 XICON_ON 整体裁剪;若产品定位 Icon 为核心能力,则维持现状即为正确结论 | 高:5 个核心头 + 多 .c 联动 |
| 格式批次(可并行) | 12 个缺 BOM 头补 ef bb bf(上表清单) | 低:纯格式,注意不要与守卫改动冲突 |
| 无需动作 | GPU 系 2 头已合规;9 个 Graphics 头维持"API 恒可见"设计 | 无 |
