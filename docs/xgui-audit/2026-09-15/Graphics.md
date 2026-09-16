# XGui ↔ Qt 6.8.3 对齐审计报告：Graphics 模块

- 审计日期：2026-09-15
- 审计范围：`Src/XGui/Graphics/` 全部公开头文件与实现（只读审计，未修改任何 Src/Test 源码，未 commit/push）
- Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/gui/image`（qimage.h qpixmap.h qbitmap.h qpicture.h qimageiohandler.h qimagereader.h qimagewriter.h qmovie.h qpixmapcache.h）+ `qtbase/src/gui/painting`（qpainter.h qbackingstore.h qcolorspace.h qcolortransform.h）
- 背景文档：《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》、《XGui.md》相关章节（10.31~10.38）

---

## 一、模块概览表

| X 类 | Qt 对应类 | Qt 头文件 | X 继承链 | Qt 继承链 | 继承匹配 | API 缺口 | 功能缺口 | 完成度 |
|---|---|---|---|---|---|---|---|---|
| XImage | QImage | image/qimage.h | XImage→XClass | QImage→QPaintDevice | 否（缺 XPaintDevice） | 6 | 3 | 92 |
| XPixmap | QPixmap | image/qpixmap.h | XPixmap→XClass | QPixmap→QPaintDevice | 否（缺 XPaintDevice） | 4 | 1 | 94 |
| XBitmap | QBitmap | image/qbitmap.h | XBitmap→XPixmap→XClass | QBitmap→QPixmap→QPaintDevice | 否（缺最底层 QPaintDevice） | 1 | 0 | 97 |
| XPicture | QPicture | image/qpicture.h | XPicture→XClass | QPicture→QPaintDevice | 否（缺 XPaintDevice） | 1 | 1 | 90 |
| XImageIOHandler | QImageIOHandler | image/qimageiohandler.h | XImageIOHandler→XClass | QImageIOHandler（无基类） | 是 | 0 | 0 | 97 |
| XImageIOPlugin | QImageIOPlugin | image/qimageiohandler.h | XImageIOPlugin→XObject | QImageIOPlugin→QObject | 是 | 0 | 0 | 95 |
| XImagePluginRegistry | （Qt 无对应公开类，内部实现） | — | 静态注册表 | — | — | — | — | 90 |
| XImageFormat/XPixelFormat | QImage::Format/QPixelFormat | image/qimage.h + qpixelformat.h | 枚举/值类型 | 枚举/值类型 | 是 | 1 | 0 | 95 |
| XImageReader | QImageReader | image/qimagereader.h | XImageReader→XClass | QImageReader（无基类） | 是 | 2 | 1 | 95 |
| XImageWriter | QImageWriter | image/qimagewriter.h | XImageWriter→XClass | QImageWriter（无基类） | 是 | 0 | 0 | 96 |
| XMovie | QMovie | image/qmovie.h | XMovie→XObject | QMovie→QObject | 是 | 1 | 2 | 78 |
| XPixmapCache | QPixmapCache | image/qpixmapcache.h | 静态类 | 静态类 | 是 | 0 | 0 | 96 |
| XPainter | QPainter | painting/qpainter.h | 普通结构体 | 普通类 | 是 | 10 | 5 | 85 |
| XBackingStore | QBackingStore | painting/qbackingstore.h | XBackingStore→XObject | QBackingStore（无基类） | 是（X 多一层 XObject） | 0 | 1 | 95 |
| XColorSpace | QColorSpace | painting/qcolorspace.h | C99 值类型（char[64] 描述，规则允许例外） | Qt 值类型（隐式共享） | 是 | 5 | 3 | 72 |
| XColorTransform | QColorTransform | painting/qcolortransform.h | 描述结构体（source+target） | Qt 值类型（隐式共享） | 是（但仅描述） | 5 | 2 | 45 |
| XGpuRenderBackend/XGpuRenderDriver_* | （Qt 无直接对应公开头，内部实现） | — | 内部对象/驱动表 | — | — | — | — | 90 |
| XImageCodec/* | （Qt 图像插件私有实现，无公开头） | — | 内部编解码器 | — | — | — | — | 88 |

> 注：XImage/Xpixmap/XPicture 与 Qt 的继承差异统一为“缺少 XPaintDevice 中间基类”，详见约束 2 判定。

---

## 二、逐类对比

### 2.1 XImage ↔ QImage（qtbase/src/gui/image/qimage.h）

**继承**
- X：`XImage { XClass m_class; XImageData* m_data; }`，`XCLASS_DEFINE_EXTEND_END(XImage, XClass)`
- Qt：`class QImage : public QPaintDevice`
- 判定：不匹配。缺少 `XPaintDevice` 中间基类（仓库中不存在该类，`XWidget_paintDevice()` 直接返回 XImage*）。

**API 缺口（Qt 原型 → X 现状）**
| Qt 6.8 API | X 现状 | 说明 |
|---|---|---|
| `int devType() const override` | 无 XImage_devType | 缺少 |
| `QPaintEngine *paintEngine() const override` | 无 XImage_paintEngine | 缺少（XPixmap/XPicture 有） |
| `QImage convertedTo(Format, flags)` / `void convertTo(Format, flags)` | 无 convertedTo/convertTo 命名 | 仅有 convertToFormat/convertToFormatInPlace |
| `void fill(Qt::GlobalColor color)` | 无 GlobalColor 重载 | 有 fill(uint)/fillColor(XColor*) 可覆盖 |
| `valid(QPoint)`、`pixelIndex(QPoint)`、`pixel(QPoint)`、`setPixel(QPoint,uint)`、`pixelColor(QPoint)`、`setPixelColor(QPoint,QColor)` | 仅 x,y 版本 | C 下可接受，记 1 项 |
| `QImage(const QSize&, Format)` | 无 size 版本 | 有 init_ex(w,h,f)，记 1 项 |

- 命名差异合规项：`XImage_loadFromData_2/loadDevice_2/saveDevice_2` 等 `_2` UTF-8 重载符合项目命名规范；`text_2` 返回 const char* 由内部缓存持有，符合规范。

**功能缺口**
1. `XImage_transformed()`：`(void)mode;` 完全忽略 Qt::TransformationMode，SmoothTransformation 也走最近邻（XImage.c:4598-4656）。Qt 在 Smooth 时用平滑变换。→ 约束 3 违规。
2. `XImage_scaled()` smooth 路径为自写双线性插值（XImage.c:4437-4451），不是 Qt qimage smoothScaled 的算法，像素输出不能与 Qt 1:1。→ 约束 3（近似）。
3. `XImage_setAlphaChannel()` 异尺寸路径用最近邻采样（XImage.c:2756-2763），Qt 走 QPainter 平滑缩放。→ 约束 3（近似）。

**其他**
- 颜色表/像素格式/色彩空间/文本元数据/DPI/DPR/offset/cacheKey/detach/swap/equals 等均有实现，未发现 TODO/未实现占位。
- 无 raw malloc/free/strdup/memcpy；copy/move 走 XCopy/XMove。

### 2.2 XPixmap ↔ QPixmap（image/qpixmap.h）

**继承**：X：XPixmap→XClass；Qt：QPixmap→QPaintDevice。不匹配（缺 XPaintDevice）。

**API 缺口**
| Qt 6.8 API | X 现状 |
|---|---|
| `QPlatformPixmap *handle() const` | 无公开 handle（有 paintEngine 返回 void*） |
| `operator QVariant() const` | 无（XImage/XBitmap 有 toVariant，XPixmap 没有） |
| `QPixmap copy(int x,int y,int w,int h)` | 无 int 重载（有 copyRect(rect/NULL)） |
| `void scroll(int dx,int dy,int x,int y,int w,int h,QRegion*)` | 无 6 参数重载（有 rect 版） |

**功能缺口**：无实质占位；toImage/fromImage/fromImageReader/load/save/convertFromImage/掩码/缩放/变换均实现。
- 违规：`XPixmap_mask_2()`（旧 XPixmap 兼容版本）与 Qt 对齐的 `XPixmap_mask/maskBitmap` 并存，属旧 API 双轨（约束 7，轻微）。

### 2.3 XBitmap ↔ QBitmap（image/qbitmap.h）

**继承**：X：XBitmap→XPixmap→XClass；Qt：QBitmap→QPixmap→QPaintDevice。中间 XPixmap 对应上了，最底层缺 QPaintDevice。

**API 缺口**：无实质缺口（QVariant 转换、fromImage/fromData/fromPixmap/transformed/clear/swap 齐全；`XBitmap_init_pixmap` 对应 Qt 已弃用的 `QBitmap(const QPixmap&)`，均带弃用提示，与 Qt 行为一致）。
**功能缺口**：无。单色存储 MonoLSB、颜色表 color0/color1 约定与 Qt 一致。

### 2.4 XPicture ↔ QPicture（image/qpicture.h）

**继承**：X：XPicture→XClass；Qt：QPicture→QPaintDevice。不匹配（缺 XPaintDevice）。

**API 缺口**：`operator=(const QPicture&)`/move 赋值无（有 XPicture_create_copy/create_move/swap 覆盖生命周期语义），记 1 项。
**功能缺口**
1. 便携记录流为 XPictureOpcode 自定义子集（31 个 opcode），覆盖 Qt QPicturePaintEngine 的主要 Pdc 指令，但渐变画刷只保存线性/径向/锥形几何与停止点，不保存 spread、坐标模式、插值模式、纹理画刷（XPicture.h:311-323 明确标注）；`recordDrawText` 仅保存 UTF-8 文本+颜色+点阵字体，不含 Qt 复杂字体属性。→ 约束 3（记录能力子集，回放近似）。
2. 回放强制实线描边语义、录制路径渐变色按行左端点取色近似（XPainter.c:3071-3079、XGui.md 10.31）。→ 约束 3（近似）。

### 2.5 XImageIOHandler ↔ QImageIOHandler（image/qimageiohandler.h）

**继承**：X：XImageIOHandler→XClass；Qt：无基类（普通类）。X 增加 XClass 对象机制，可接受，判匹配。

**API/行为对比**：ImageOption 19 项枚举、Transformation 8 项枚举数值与 Qt 完全一致；13 个虚槽（canRead/read/write/option/setOption/supportsOption/jumpToNextImage/jumpToImage/loopCount/imageCount/nextImageDelay/currentImageNumber/currentImageRect）与 Qt 纯虚/虚函数一一对应；默认实现与 Qt 一致（write=false、option=false、supportsOption=false、jumpToNext=false、loopCount=0、imageCount=canRead()?1:0、currentImageNumber=0、currentImageRect=0，见 XImageIOHandler.c:43-125）。allocateImage/checkAllocation 按 qimageiohandler.cpp:532-557 的 32 位有效深度上限规则实现。`setFormat const` 变体（XImageIOHandler_setFormat_const）与 Qt 6.8 一致。
**缺口**：无。**功能**：无占位。

### 2.6 XImageIOPlugin ↔ QImageIOPlugin（image/qimageiohandler.h:101-117）

**继承**：X：XImageIOPlugin→XObject；Qt：QImageIOPlugin→QObject。匹配。
**对比**：Capability 位值 0x1/0x2/0x4 一致；capabilities/create 虚槽对应 Qt 纯虚函数；额外提供 keys/nameFilters/mimeTypes（对应 Qt QFactoryInterface/插件元数据），合理扩展。
**缺口/功能**：无。

### 2.7 XImagePluginRegistry（Qt 无公开对应类）

内部源码级注册表（容量 32 静态存储），对应 Qt 的 QImageReaderPrivate/QPluginLoader 插件发现机制。功能：add/remove/pluginAt/clear、读写 handler 创建、suffix 优先/内容回退探测、格式/MIME 查询、动态发现回调。Qt 动态插件目录加载与工厂发现未实现（XGui.md 10.36 已文档化）。`XImagePluginRegistry.h` 无 BOM → 约束 4 违规。

### 2.8 XImageFormat / XPixelFormat ↔ QImage::Format / QPixelFormat（image/qimage.h:41-82）

- XImageFormat 36 个格式值 + NImageFormats 与 Qt 6.8 完全同序同值。
- XPixelFormat 覆盖 QPixelFormat 的 ColorModel/AlphaUsage/AlphaPosition/AlphaPremultiplied/TypeInterpretation/ByteOrder 字段；XPixelFormatModel 含旧名兼容宏（Mono→Indexed、Grayscale→Gray），不改变 Qt 语义。
- API：bitDepth/hasAlpha/isPremultiplied/bytesPerLine/bytesPerLineAlignment/pixelFormat/toPixelFormat/toImageFormat 齐全。
- 缺口：无实质缺口（记 1：QPixelFormat 的 `premultiplied` 等构造器语义未 1:1 建模，X 用结构体字段代替，可接受）。

### 2.9 XImageReader ↔ QImageReader（image/qimagereader.h）

**继承**：X：XImageReader→XClass；Qt：无基类。匹配。
**API 缺口**
| Qt 6.8 API | X 现状 |
|---|---|
| `QImage read()`（返回值重载） | 仅 `read(QImage*)`（C 语义可接受，记 1） |
| 成员 `QImage::Format imageFormat() const` | 命名为 `XImageReader_imageFormatValue`（避免与静态 imageFormat 冲突，命名差异合规，记 1） |

**功能缺口**：动画仅 GIF 多帧（XIMAGECODEC_GIF_ANIM_ON 时）；`@2x~@9x` 高 DPI 文件名推导、QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING 已实现（XImageReader.h:387-389）。未发现占位。
**无违规**。

### 2.10 XImageWriter ↔ QImageWriter（image/qimagewriter.h）

API/行为全覆盖：错误枚举 4 项一致；format/device/fileName/quality/compression/subType/supportedSubTypes/optimizedWrite/progressiveScanWrite/transformation/setText/canWrite/write/error/errorString/supportsOption/静态格式列表齐全。`setText` 的 Qt simplified 语义（去首尾空白、合并内部空白、多次拼接 Description）已实现。
**缺口/功能**：无。

### 2.11 XMovie ↔ QMovie（image/qmovie.h）

**继承**：X：XMovie→XObject；Qt：QMovie→QObject。匹配。
**API 缺口**
| Qt 6.8 API | X 现状 |
|---|---|
| `QBindable<int> bindableSpeed()` / `bindableCacheMode()` | 无（C 无 QBindable，记 1） |
| 构造 parent 参数 | 无（C 生命周期由 XObject 管理，可接受） |

**信号**：7 个信号全部有 `*_signal` 函数：started/resized/updated/stateChanged/error/finished/frameChanged；空参信号 `XMovie_started_signal`/`XMovie_finished_signal` 的 args 传 NULL，符合约束 5（XMovie.c:764-809）。
**功能缺口**
1. 无内部定时器/事件循环：Qt 由 QTimer 驱动 `_q_loadNextFrame()` 自动逐帧播放；XMovie 的 start() 只读第一帧并发 started，后续帧必须由调用方手动 `jumpToNextFrame()` 驱动（XMovie.c:665-676 明确注释）。speed 只参与 nextFrameDelay() 计算，不驱动播放。→ 功能缺口（QMovie 播放模型核心差异）。
2. CacheMode 两种模式效果相同（单帧后端不缓存额外帧，XMovie.h:42-46 注明）；CacheAll 无实际帧缓存。→ 功能缺口（轻微）。

### 2.12 XPixmapCache ↔ QPixmapCache（image/qpixmapcache.h）

全部静态 API 对应：cacheLimit/setCacheLimit/find(QString)/find(Key)/insert(QString)/insert(pixmap)→Key/replace(Key，Qt 6.6 起弃用，X 保留并按 Qt 内联实现语义)/remove(QString)/remove(Key)/clear；Key 的 init/copy/deinit/isValid/equals/hash/swap 对应 Qt Key 的 ctor/==/isValid/swap/hash。主线程限制、LRU、replace 先失效后插入、负值限制保留等语义与 qpixmapcache.cpp 注释逐点对应。原子自旋锁保护内部结构。
**缺口/功能**：无。`XPixmapCache.h` 无 BOM → 约束 4 违规。

### 2.13 XPainter ↔ QPainter（painting/qpainter.h）

**继承**：X：普通结构体（非 XClass）；Qt：普通类。匹配（均非 QObject）。

**API 缺口（Qt 原型）**
| Qt 6.8 API | X 现状 |
|---|---|
| `void drawPixmapFragments(const PixmapFragment*, int, const QPixmap&, PixmapFragmentHints)` + `class PixmapFragment` | 完全缺失 |
| `void drawGlyphRun(const QPointF&, const QGlyphRun&)` | 缺失（有逐字形 drawGlyph） |
| `void drawStaticText(...)` | 缺失 |
| `void drawTextItem(const QPointF&, const QTextItem&)` | 缺失 |
| `QRectF/QRect boundingRect(...)`（4 个重载） | 缺失（有 textWidth/textHeight 等辅助） |
| `QFontMetrics fontMetrics()` / `QFontInfo fontInfo()` | 缺失（无 XFontMetrics/XFontInfo） |
| `QPainterPath clipPath()` / `void setClipPath(...)` | 缺失（仅矩形/区域裁剪） |
| `void setClipRect(const QRectF&, ...)` | 缺失（仅整型 XRect） |
| `drawEllipse(center, rx, ry)`（QPointF/QPoint 重载） | 缺失（仅外接矩形） |
| `drawText(QRectF, QString, QTextOption)` / `drawText(QPointF, QString, int tf, int justificationPadding)` | 缺失 |
| `beginNativePainting()`/`endNativePainting()` | 缺失（纯 C 无原生引擎，可豁免，计 0） |

**功能缺口（约束 3 相关，均有代码注释/文档佐证）**
1. 画刷 Dense1~DiagCross 图案样式只保存不绘制，填充时按纯色处理（XPainter.c painterFillPolygonShape 仅区分 Solid 与渐变，XPainter.h:1319 注明“按当前纯色近似”）。Qt 绘制 8x8 位图案。→ 约束 3 违规。
2. 纹理画刷（TexturePattern=24）整体不支持：setBrushStyle 按 Qt setStyle 语义拒绝，但没有 QBrush(QPixmap) 纹理画刷构造路径。→ 约束 3 违规。
3. 路径/椭圆/圆弧/扇形/弦/圆角矩形按折线采样展平（XGui.md 10.31 注明二次贝塞尔 16 段、三次 24 段），与 Qt 精确曲线光栅化输出不一致；圆弧按折线近似（XPainter.c:4268）。→ 约束 3（近似）。
4. 多边形渐变填充在录制/回放路径按“每行左端点取色”近似（XPainter.c:3071-3079）。→ 约束 3（近似）。
5. RTL 文本仅按右对齐处理，无真正 RTL 排版；TextLongestVariant 无多变体（XGui.md 10.31）；CustomDashLine 用默认虚线近似（XPainter.c:2817）。→ 约束 3（近似）。
6. drawImageRect/drawImage 软件后端恒最近邻采样，SmoothPixmapTransform 提示不改变采样（XPainter.c:1928/2309）。→ 约束 3（近似；Qt 默认也为最近邻，仅当 Smooth 提示时差异）。

**合规亮点**：38 个 CompositionMode 与 Qt 完全同序；RenderHint 位值一致；save/restore 状态栈、window/viewport 视图变换、world matrix、combinedTransform/deviceTransform、setClipRegion、背景画刷、渐变画刷、文本 flags 数值全集（对齐 Qt::AlignmentFlag/TextFlag）均实现；XPAINTER_* 裁剪宏回退符合“裁剪宏回退除外”。

### 2.14 XBackingStore ↔ QBackingStore（painting/qbackingstore.h）

**继承**：X：XBackingStore→XObject；Qt：普通类。X 多一层 XObject，判匹配（合理扩展）。
**API 对比**：window/paintDevice/flush(region,window,offset)/resize/size/scroll/beginPaint/endPaint/setStaticContents/staticContents/hasStaticContents/handle 全覆盖；`paintDevice()` 返回 XImage*（Qt 返回 QPaintDevice*，因无 XPaintDevice 而收窄，记 1 项结构差异）。额外提供 nextTile/paintOrigin/setBuffers/requiredBufferSize/flushTile/toImage（Qt 在 QPlatformBackingStore 提供 toImage），属合理扩展。
**功能缺口**：平台后端不可用时安全退化（文档化）；scroll/静态内容区域不被后端裁剪（对齐 qbackingstore.cpp:271-281）。未发现占位。`XBackingStore.h` 无 BOM → 约束 4 违规。

### 2.15 XColorSpace ↔ QColorSpace（painting/qcolorspace.h）

**继承**：均为值类型。X 为 C99 值结构（char[64] 描述为规则明确允许的例外）。

**API 缺口（Qt 原型）**
| Qt 6.8 API | X 现状 |
|---|---|
| `static QColorSpace fromIccProfile(const QByteArray&)` / `QByteArray iccProfile() const` | 缺失（无 ICC 承载） |
| `setTransferFunction(const QList<uint16_t>&)` / `setTransferFunctions(...)` / `withTransferFunction(table)` / `withTransferFunctions(...)` | 缺失（无 LUT 承载，XColorSpace.h:103-104 注明） |
| `QColorTransform transformationToColorSpace(const QColorSpace&) const` | 缺失（XColorTransform 仅为描述结构） |
| `void detach()` | 无（值类型不需要，豁免） |
| `operator QVariant()` | 无（可豁免） |

**功能缺口**
1. 仅支持 ThreeComponentMatrix 变换模型与 RGB/Gray 元数据；ICC 原始字节与逐通道 LUT 未实现（XColorSpace.h:103-104 明确为“暂不放入值类型”）。
2. setWhitePoint 只更新可见白点元数据，不保存 Qt 的色彩适应矩阵（XColorSpace.h:263 注明）。
3. Primaries/TransferFunction 枚举值含 Gamma22/Gamma28 旧兼容值，与 Qt 8 项 TransferFunction 不完全一一对应（兼容别名，不改变新代码语义）。
- `XColorSpace.h` 无 BOM → 约束 4 违规。

### 2.16 XColorTransform ↔ QColorTransform（painting/qcolortransform.h）

**现状**：XColorTransform 仅 `{XColorSpace source; XColorSpace target;}` 描述结构，无任何 Qt API：
- `bool isIdentity() const` 缺失
- `QRgb/QRgba64/QRgbaFloat16/QRgbaFloat32/QColor map(...)` 5 个 map 重载全部缺失
- `swap`/`operator==`/`operator!=` 缺失（XColorSpace_equals 只比空间本身）

**功能缺口**：真正的颜色变换由 XImage_applyColorTransform 内部按 3x3 矩阵临时计算，XColorTransform 对象本身不可映射颜色，也不可独立复用/比较。完成度最低的类。

### 2.17 XGpuRenderBackend / XGpuRenderDriver_*（内部实现，简述）

Qt 无直接公开头（对应 Qt 的 QRhi/图形后端私有体系）。软件/OpenGL/Vulkan 三后端，GPU 会话、FBO 快照、scissor 同步、字形图集均有实现；不支持的 GPU 原语（复杂变换、多矩形裁剪、RasterOp 等）整帧降级软件路径（XPainter.c:1798-1869），Vulkan 简化说明见 XGpuRenderDriver_vulkan.c:50/1690/2013。`XGpuRenderBackend.h`、`XGpuRenderDriver.h`、`XGpuRenderDriver_vulkan_shaders.h` 无 BOM → 约束 4 违规。

### 2.18 XImageCodec/*（内部实现，简述）

九类格式 codec（BMP/PNG/JPEG/GIF/PPM/XBM/XPM/ICO/SVG），通过 XImageBuiltinPlugin 接入注册表。裁剪开关齐全；已知近似：JPEG 渐进/算术/12 位/CMYK 为可裁剪扩展（XIMAGECODEC_JPEG_* 开关）、SVG 数字列表“简化：直接使用逗号分隔解析”（XImageCodecSvg.c:1034）、SVG 圆头连接用“顶点圆盘近似”（XImageCodecSvg.c:2399）、GIF 单帧解码在动画关闭时裁剪帧数组（XImageCodecGif.c:605）。均为裁剪/文档化边界。

---

## 三、约束合规核查（Graphics 模块内）

| # | 硬约束 | 核查结果 |
|---|---|---|
| 1 | 拥有型字符串一律 XString*，禁止 char[N]/char* 长期持有；API 主版本 XString，UTF-8 用 `_2` 重载 | 通过。字符串成员均为 XString*；`_2` 重载均为转发包装（XString_toUtf8 临时转换），未见直接维护第二份字符串状态；唯一 char[64] 为 XColorSpace 描述字段（规则明确例外）。raw malloc/free/strdup/memcpy 全模块 grep 为 0（仅 XMalloc_System/XFree_System/XMemcpy/XMemory_strdup）。 |
| 2 | 继承一比一含中间基类 | **违规**：XImage/XPixmap/XPicture 缺 `XPaintDevice`（对应 Qt QPaintDevice）中间基类；XBitmap 链末端同样缺。其余 XImageIOPlugin→XObject、XMovie→XObject 与 Qt QObject 对应正确。 |
| 3 | 样式/绘制不得精简近似（裁剪宏回退除外） | **违规（已文档化近似）**：① Dense1~DiagCross 图案画刷按纯色填充；② 纹理画刷不支持；③ transformed() 忽略 Smooth 模式恒最近邻；④ setAlphaChannel 异尺寸最近邻；⑤ XImage_scaled smooth 为自写双线性而非 Qt smoothScaled；⑥ 路径/圆弧折线采样展平；⑦ 多边形渐变按行左端点取色；⑧ RTL 仅右对齐；⑨ CustomDashLine 默认虚线；⑩ XPicture 记录流为子集（无 spread/纹理/复杂字体属性）。 |
| 4 | 公共头中文 Doxygen + UTF-8 BOM | **违规（BOM）**：以下 9 个 .h 缺 BOM：XBackingStore.h、XColorSpace.h、XGpuRenderBackend.h、XGpuRenderDriver.h、XGpuRenderDriver_vulkan_shaders.h、XImageBuiltinPlugin.h、XImagePluginRegistry.h、XPainter.h、XPixmapCache.h。Doxygen 覆盖率总体达标（@brief/@param/@return），XPainter/XPixmapCache 少量单行文档压缩但参数齐全。 |
| 5 | 信号：空参 args=NULL；Qt 6.8 每信号有 *_signal | 通过。XMovie 7 信号齐全，started/finished 空参传 NULL；其余 Graphics 类无 Qt 信号。 |
| 6 | init/deinit 成对；copy/move 虚函数安全；禁 memcpy；禁裸 malloc/free/strdup | 通过。XCopy/XMove 贯穿 XImage/XPixmap/XBitmap/XPicture/XMovie/XPixmapCache；XImageIOHandler 虚表继承 XClass 并重载 deinit；未发现 memcpy 复制对象。 |
| 7 | 旧 API 不保留（与 Qt 冲突改名不并存） | **轻微违规**：XPixmap_mask_2（旧版返回 XPixmap 的掩码接口）与 Qt 对齐的 XPixmap_mask/maskBitmap 并存；XBitmap_init_pixmap 与 fromPixmap 并存（Qt 自身也保留弃用版，可辩护）。XImage_mirror/rgbSwap 为 Qt 同名（非旧 API）。 |
| 8 | 新代码 C99，无 C++/C11 | 通过。未发现 C++/C11 语法；复合字面量 `&(XSize){...}` 为 C99 合法。 |

---

## 四、缺失 Qt 类清单（审计范围内）

| Qt 类 | Qt 头文件 | 建议 |
|---|---|---|
| QPaintDevice | qtbase/src/gui/painting/qpaintdevice.h | 建议实现 `XPaintDevice` 抽象基类（devType/metric/paintEngine 虚槽），让 XImage/XPixmap/XPicture/XBitmap 继承链补齐中间层；若坚持 C 精简设计，需在文档中正式豁免并说明影响 |
| QPaintEngine | qtbase/src/gui/painting/qpaintengine.h | 建议不实现：X 以 XPainter 回调函数表（m_drawLine 等）代替引擎体系，paintEngine() 返回 NULL/void* 已文档化 |
| QColorTransform（完整语义） | qtbase/src/gui/painting/qcolortransform.h | 建议补 `XColorTransform_map(ARGB32)` 与 `isIdentity`/`equals`，或明确限定为描述结构并移除 XImage_applyColorTransform 对它的隐式依赖 |
| QPainter::PixmapFragment | qtbase/src/gui/painting/qpainter.h:64-86 | 建议不实现（drawPixmapFragments 为高性能专用接口，嵌入式裁剪合理） |
| QFontMetrics / QFontInfo | qtbase/src/gui/painting/qfontmetrics.h / qfontinfo.h | 建议不实现为独立类；现有 XPainter_textWidth/Ascent/Descent/Height 覆盖常用子集，如需对齐再建值类型 |
| QStaticText / QGlyphRun / QTextItem / QTextOption | qpainter.h 引用类 | 建议不实现（点阵文本引擎无对应物），在文档中列为裁剪项 |

> 说明：QImageIOPlugin/QImageReader 等 14 个基准头内的主类均有 X 对应实现，无“完全缺失”的主类。

---

## 五、优先任务建议（按优先级）

1. **补齐 XPaintDevice 中间基类**（或正式豁免）：一次性解决 XImage/XPixmap/XPicture/XBitmap 继承链与 QPaintDevice 的一比一问题（约束 2），并让 XBackingStore_paintDevice 返回类型可泛化。影响面大，需先评审。
2. **实现画刷图案样式**：Dense1~DiagCross 按 Qt 8x8 位图案光栅化（约束 3 最高频可见差异），至少覆盖软件后端；纹理画刷补 QBrush(pixmap) 等价路径。
3. **XImage_transformed 支持 SmoothTransformation**：把 mode 参数接入平滑插值（可复用 XImage_scaled 的双线性路径），消除 `(void)mode` 硬伤。
4. **XMovie 增加定时驱动层**：接入 XGui 事件循环/定时器后自动按 nextFrameDelay 推进帧并触发更新信号，使 start()/setSpeed() 语义与 QMovie 对齐；否则在文档中明确“手动驱动”为正式裁剪项。
5. **XPainter 补 boundingRect()/clipPath()/setClipPath()**：文本布局与路径裁剪是 QPainter 常用能力，优先于 drawStaticText/drawGlyphRun 等高性能接口。
6. **XColorSpace 补 ICC/LUT 或明确裁剪**：fromIccProfile/iccProfile 与 LUT 传递函数是 Qt 色彩管理核心，至少提供“读取/写入 ICC 字节”的透明承载（存字节，不解析），并补 transformationToColorSpace 的矩阵生成。
7. **清理 9 个无 BOM 公共头**（约束 4）：XBackingStore.h、XColorSpace.h、XPainter.h、XPixmapCache.h、XImagePluginRegistry.h、XImageBuiltinPlugin.h、XGpuRenderBackend.h、XGpuRenderDriver.h、XGpuRenderDriver_vulkan_shaders.h 加 UTF-8 BOM。
8. **XPixmap 补 handle() 与 QVariant 适配**（toVariant），并评估移除 XPixmap_mask_2 旧双轨 API（约束 7）。
9. **XPainter 文本与路径近似收敛**：RTL 至少补齐镜像布局或标记为正式不支持；CustomDashLine 支持用户 dash 数组；渐变多边形按像素取色替代行左端点。
10. **XImageReader/Writer 补 Qt fixture 回归**（畸形 BMP/掩码、Picture 序列化、QImage 色彩空间/文本元数据边界），把 10.36 遗留的“Qt fixture 回归补全”落实为 Test 用例。
