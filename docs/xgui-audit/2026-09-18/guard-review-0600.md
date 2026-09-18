# XGui 头文件特性守卫复核报告(guard-review-0600)

- 日期:2026-09-17
- 范围:`Src/XGui` 全部 175 个 `.h` 头文件(只读复核,未改任何源码)
- 背景:此前审计称约 20 个头"整体无特性守卫",主要位于 Graphics 与 Icon 系。本复核逐头验证。

## 一、总体分类

- 有特性守卫(文件主体被 `#if XXX_ON` 包裹或含成体系子特性守卫):**156 个**
- 无守卫(全文件无任何 `#if XXX_ON`):**16 个**
- 整体未包裹、仅有零散子块守卫(计入先验"~20"):**3 个**(XImage.h、XPixmap.h、XPainter.h)
- 合计复核对象:**19 个**,与先验"约 20"吻合。

## 二、逐头复核表

结论取值:无需守卫(公共类型/基类头)| 建议补守卫(独立可裁剪)| 待讨论

### A. 全文件无任何 `#if XXX_ON` 的 16 个

| 头文件 | 有无守卫 | 对应开关 | 结论 | BOM |
|---|---|---|---|---|
| Src/XGui/XAlignment.h | 无 | 无(XALIGNMENT_ON 不存在) | 无需守卫:纯对齐枚举,被 17 处引用,含 XLabel.h、XHeaderView.h、XLayoutItem.h 等有守卫头,补守卫必断裁剪链 | 有 |
| Src/XGui/Graphics/XColorSpace.h | 无 | 无(XCOLORSPACE_ON 不存在) | 无需守卫:值类型(enum+struct)被 XImage.h(XPAINTDEVICE_ON)、XSurfaceFormat.h(XSURFACEFORMAT_ON) 值嵌入,补守卫断链 | 有 |
| Src/XGui/Graphics/XImageFormat.h | 无 | 无(XIMAGE_ON 不存在) | 无需守卫:像素格式纯枚举/值类型,被 XPixmap.h、XImage.h、XImageIOHandler.h、XBackingStore.c 等广泛引用 | 有 |
| Src/XGui/Graphics/XImageIOHandler.h | 无 | XIMAGEIOPLUGIN_ON 已存在但不可用 | 无需守卫:插件基类;XImageIOHandler.c 全文件无守卫(恒编译),且经 XImageIOPlugin.h→XImagePluginRegistry.h→XImageReader.h 被 XMovie.h(XMOVIE_ON) 引用;插件关闭时 reader/writer 仍靠它直连编解码 | 有 |
| Src/XGui/Graphics/XImageIOPlugin.h | 无 | XIMAGEIOPLUGIN_ON 已存在 | 待讨论:XImageIOPlugin.c 无任何守卫(恒编译),先给头补守卫会在 =0 时编译失败;若要裁剪需头+.c 同批改造,当前实现选择了"registry 层裁剪、类保留"路线 | 有 |
| Src/XGui/Graphics/XImagePluginRegistry.h | 无 | XIMAGEIOPLUGIN_ON 已存在 | 建议补守卫:仅 4 个 .c 消费(XImageReader.c/XImageWriter.c 无条件 include 但使用点全部在 `#if XIMAGEIOPLUGIN_ON` 块内;XMovie.c 自行包裹 include;registry.c 19–1106 行整体包裹,=0 时全为空桩);头整体包裹 `#if XIMAGEIOPLUGIN_ON` 与现状实现完全自洽 | 有 |
| Src/XGui/Graphics/XImageBuiltinPlugin.h | 无 | XIMAGEIOPLUGIN_ON 已存在 | 建议补守卫:仅 registry.c 与自身 .c 消费;XImageBuiltinPlugin.c 19–1144 行整体包裹(=0 时 `#else` 空桩),头包裹 `#if XIMAGEIOPLUGIN_ON` 安全(可选加 `&& XIMAGECODEC_ON` 与注释语义对齐) | 有 |
| Src/XGui/Graphics/XImageReader.h | 无 | XIMAGEIOPLUGIN_ON 已存在但不可用 | 无需守卫:被 XMovie.h(XMOVIE_ON 有守卫头)引用,XIcon.c/XPixmap.c 亦用;插件关闭时它仍是直连读取主 API(reader.c 有完整 `#if !XIMAGEIOPLUGIN_ON` 回退路径) | 有 |
| Src/XGui/Graphics/XImageWriter.h | 无 | XIMAGEIOPLUGIN_ON 已存在但不可用 | 无需守卫:仅自身 .c include,但 writer.c 有 `#if !XIMAGEIOPLUGIN_ON` 直连保存回退,类在 =0 时仍必须完整声明 | 有 |
| Src/XGui/Icon/XIcon.h | 无 | 无(XICON_ON 全仓 0 命中) | 无需守卫:Icon 核心公共类,被 XWidget.h、XWindow.h、XAbstractButton.h、XGuiApplication.h、XPlatformIntegration.h 等核心有守卫头引用;补守卫必断裁剪链,引入 XICON_ON 需改 5 个核心头,成本高收益低 | 有 |
| Src/XGui/Icon/XIconEngine.h | 无 | 无 | 无需守卫:图标引擎基类;XIcon.c(不可裁剪核心)直接使用,且被 XSvgIconEngine.h(XSVGICON_ON)、XIconEnginePlugin.h、XIconThemeEngine.h 继承引用 | 有 |
| Src/XGui/Icon/XIconEnginePlugin.h | 无 | 无 | 待讨论:XIconEnginePlugin.c 无守卫(恒编译),且被 XSvgIconEnginePlugin.h(XSVGICON_ON) 引用;若引入 XICONENGINEPLUGIN_ON 必须与 XSVGICON_ON 建立连带关闭关系(参照 XGuiConfig.h 既有连带裁剪写法),否则两开关组合出非法状态 | 有 |
| Src/XGui/Icon/XIconScaledPixmapCache.h | 无 | XPIXMAPCACHE_ON 已存在(语义=运行时退化) | 无需守卫:XGuiConfig.h 注释明确其裁剪语义是"退化为永久未命中/拒绝插入/空清理";XIconScaledPixmapCache.c 用 `#if/#else` 实现原地退化,类型与 API 必须始终可见,不能空头化(与 XPixmapCache.h 的"空头"路线不同,属有意设计) | 有 |
| Src/XGui/Icon/XIconStyleHelper.h | 无 | 无 | 无需守卫:Icon 内部辅助,仅 XIcon.c/XIconThemeEngine.c/自身 .c 使用;helper.c 已按 XGUI_ON、XGUIAPPLICATION_ON && XPALETTE_ON 做函数级退化,头需保持完整 | 有 |
| Src/XGui/Icon/XIconThemeEngine.h | 无 | 无 | 待讨论:freedesktop 主题引擎是独立的可选功能(与曾获批 XSVGICON_ON 的场景类似),但 XIcon.c 无条件包含并调用它,不像 XSvgIcon 那样"全仓零外部引用";补守卫需新增 XICONTHEME_ON 并同步在 XIcon.c 加引用侧守卫,建议单独立项讨论 | 有 |
| Src/XGui/Icon/XIconThemeInternal.h | 无 | 无 | 无需守卫:名字即内部头,仅 Icon 系 3 个 .c 使用,非裁剪单元;其 .c 内已按 XIMAGECODEC_SVG_ON 等做内部降级 | 有 |

### B. 整体未包裹、仅含零散子块守卫的 3 个(补齐先验清单)

| 头文件 | 有无守卫 | 对应开关 | 结论 | BOM |
|---|---|---|---|---|
| Src/XGui/Graphics/XImage.h | 部分(27–29、513–522 两个 XPAINTDEVICE_ON 小块) | XPAINTDEVICE_ON(实际开关;XIMAGE_ON 全仓不存在) | 无需守卫:XImage 是全库基类值类型,被 XPixmap.h、XPainter.h、XMimeData.h、XScreen.h、XMovie.h、XImageCodec.h、Drive 平台 .c 等 20+ 处引用;整体包裹必断裁剪链,方法级 XPAINTDEVICE_ON(如 XImage_paintDevice)已是合理粒度 | 有 |
| Src/XGui/Graphics/XPixmap.h | 部分(仅 538–546 一块 XPAINTDEVICE_ON) | XPAINTDEVICE_ON | 无需守卫:同上,被 XLabel.h、XMimeData.h、XIcon.h、XPixmapCache.h、XMovie.h、XSplashScreen.h、XScreen.h、XXYSeries.h 等有守卫头引用,只能维持方法级守卫 | 有 |
| Src/XGui/Graphics/XPainter.h | 部分(仅 XPAINTER_SHAPE_ON 等 15 个子特性块 + 31–34 的 XGPU_ON 块,无整体包裹) | XPAINTER_* 子特性系;无 XPAINTER_ON 整体包裹 | 待讨论:被约 15 个有守卫控件头(XLineEdit.h、XCheckBox.h、XMenu.h、XPushButton.h、XProgressBar.h、XFrame.h 等)引用,整体包裹需全部引用头联动;现有 15 个 XPAINTER_* 子特性守卫已提供细粒度裁剪,整体 XPAINTER_ON 收益有限,建议维持现状 | 有 |

## 三、专项核对结果

### 1. GPU 系 vs XGPU_ON —— 已正确守卫,无需动作
- `Graphics/XGpuRenderBackend.h`:整体包裹于 `#if XPLATFORMINTEGRATION_ON && XGPU_ON`(26 行起,265 行 `#endif` 收口)。✓
- `Graphics/XGpuRenderDriver.h`:同样整体包裹(27 行起)。✓
- `Application/XGuiApplication.h` 共享 GPU 入口:196–197 行同一条件。✓
- `Graphics/XPainter.h` 31–34 行的 GPU 声明块亦受 XGPU_ON 保护。✓
- 结论:GPU 系与 XGPU_ON 关系完整、一致,不需要补守卫。

### 2. Icon 系 vs XICON_ON —— 开关不存在,属设计现状
- 全仓(含 Src/Drive/Library/CMake/Config)**不存在 XICON_ON**。Icon 核心为常开组件:XIcon/XIconEngine/XIconEnginePlugin 均被 XWidget、XWindow、XGuiApplication 等核心有守卫头或其 .c 直接引用。
- Icon 系现存的守卫只有 XSVGICON_ON(包 XSvgIconEngine.h/XSvgIconEnginePlugin.h,先验批次产物)与 XPIXMAPCACHE_ON(运行时退化路线,包 XPixmapCache.h、退化 XIconScaledPixmapCache.c)。
- 结论:Icon 核心补守卫不可行(断链);可裁剪面(XSVGICON_ON、XPIXMAPCACHE_ON)已覆盖;唯一遗留决策点是 XIconThemeEngine(待讨论)。

### 3. XImage/XPixmap/XPainter vs XIMAGE_ON —— 开关名不存在
- **XIMAGE_ON 全仓 0 命中**;先验记录中的"XIMAGE_ON"应修正为 **XPAINTDEVICE_ON**(XImage.h、XPixmap.h、XPaintDevice.h、XBackingStore.h、XBitmap.h、XPicture.h 的方法级守卫)与 **XPAINTER_ON/XPAINTER_* 系**(XPainter_config.h、XPainter.h 子特性)。
- 三者均为被广泛引用的基类/公共类型,整体包裹会破坏裁剪链;维持现状即可。

## 四、BOM 复核

- 全部 175 个头中,**仅 1 个缺 UTF-8 BOM(ef bb bf)**:
  - `Src/XGui/Widget/XWizard.h`(首 3 字节 `2f 2a 2a`,即 `/**`)——该头属有守卫(XWIZARD_ON)文件,顺带列出。
- 其余 174 个头(含本次复核的 19 个无/部分守卫头)均带 BOM。

## 五、总结论与建议批次

1. **无需守卫(13 个,维持现状)**:XAlignment.h、XColorSpace.h、XImageFormat.h、XImageIOHandler.h、XImageReader.h、XImageWriter.h、XIcon.h、XIconEngine.h、XIconScaledPixmapCache.h、XIconStyleHelper.h、XIconThemeInternal.h,以及部分守卫的 XImage.h、XPixmap.h。理由一致:公共类型/基类/值嵌入,被有守卫头引用,补守卫即断裁剪链;或开关语义本就是 .c 内退化而非空头。
2. **建议补守卫(2 个,低风险,可与 guard-review 后续批次合并)**:
   - 批次建议(guard-review-0600 批次):`Graphics/XImagePluginRegistry.h`、`Graphics/XImageBuiltinPlugin.h` 整体包裹 `#if XIMAGEIOPLUGIN_ON`(如需更严可写 `#if XIMAGEIOPLUGIN_ON && XIMAGECODEC_ON`,与 XImageBuiltinPlugin.h 注释语义一致)。依据:开关已存在,全部消费方 .c 的使用点已在 `#if XIMAGEIOPLUGIN_ON` 内,=0 时两 .c 本就产出空桩,头空头化后全仓编译自洽。**需同步验证:两层 include 为无条件 include 的 XImageReader.c/XImageWriter.c 在 =0 下仍可编译(空头文件合法,仅声明缺失,而其使用点均已包裹,预期通过)。**
3. **待讨论(3 个)**:
   - XImageIOPlugin.h:自身 .c 无守卫恒编译,补守卫需头+.c 同批改造,当前"registry 裁剪、类保留"路线已是自洽设计;
   - XIconEnginePlugin.h:如引入开关必须与 XSVGICON_ON 连带(参照 XGuiConfig.h 连带裁剪段写法);
   - XIconThemeEngine.h(以及整体 XPAINTER_ON):独立可裁剪但存在核心侧引用,需先解耦或同步加引用侧守卫,建议单独立项。
4. **BOM 修正(1 个,低风险)**:为 `Widget/XWizard.h` 补 UTF-8 BOM,与其余 174 头保持一致。
5. 先验审计的"约 20 个"经复核收敛为 **19 个**(16 个全无守卫 + 3 个仅零散子块守卫),其中真正建议补守卫的仅 2 个;其余均为断链保护下的合理无守卫状态。
