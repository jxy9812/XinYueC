/**
 * @file       XSvgIconEngine.c
 * @brief      SVG 图标引擎实现（对标 Qt 6.8 QSvgIconEngine 最小语义）。
 * @author     XinYueC 团队
 */

#include "XSvgIconEngine.h"

/* XSVGICON_ON=0 时整体裁剪：引擎无自动注册、全仓无外部引用，
 * 关闭后仅损失 SVG 图标加载能力，不影响 XIcon 其它引擎路径。 */
#if XSVGICON_ON

#include "XAlgorithm.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XMemory.h"
#include "XImageCodecInternal.h"  /* decodeSvg_ex：目标尺寸矢量直渲。 */
#include "XFile.h"
#include "XIODevice.h"
#include "XByteArray.h"


/** @brief Pixmap 虚槽：解码 SVG 文件为像素图（按请求尺寸光栅化）。 */
static void VSvgEngine_pixmap(const XIconEngine* self, const XSize* size,
                              XIconMode mode, XIconState state, XPixmap* out)
{
    XSvgIconEngine* se = (XSvgIconEngine*)self;
    XImage image;
    XImage scaled;
    XImage* renderImage;
    bool scaledInited = false;
    (void)mode;
    (void)state;
    if (!out || !se || !se->m_fileName) return;
    XImage_init(&image);
    /* 矢量直渲优先（R-108 矢量级锐度）：有效请求尺寸时读文件字节走
       decodeSvg_ex 按目标尺寸出图，viewBox 几何按目标比例映射，消除
       「固有尺寸光栅化+平滑放大」的插值模糊（对标 QSvgIconEngine 经
       QSvgRenderer::render(targetRect) 的行为）。失败回退下方既有
       XImage_load 缓存路径（不空手）。注意直渲路径绕过 XImageCache
       （键为 fileName+format 不分尺寸，避免错尺寸命中）；重复请求的
       成本由上层 XIconScaledPixmapCache 的最终位图缓存吸收。 */
    if (size && size->width > 0 && size->height > 0) {
        XString* path = XString_create_utf8(XString_toUtf8(se->m_fileName));
        XFile* file = path ? XFile_create_2(path) : NULL;
        XByteArray* bytes = NULL;
        if (file && XIODevice_open_base((XIODevice*)file,
                                        XIODevice_ReadOnly)) {
            bytes = XIODevice_readAll_3((XIODevice*)file);
            XIODevice_close_base((XIODevice*)file);
        }
        if (file) XClass_delete_base((XClass*)file);
        if (path) XString_delete_base((XClass*)path);
        if (bytes && XByteArray_size_base((const XContainer*)bytes) > 0 &&
            XImageCodecInternal_decodeSvg_ex(
                (const uint8_t*)XByteArray_data(bytes),
                XByteArray_size_base((const XContainer*)bytes),
                size->width, size->height, &image) &&
            !XImage_isNull(&image)) {
            XByteArray_delete_base((XClass*)bytes);
            XPixmap_fromImage(&image, 0, out);
            XImage_deinit_base(&image);
            return;
        }
        if (bytes) XByteArray_delete_base((XClass*)bytes);
        XImage_deinit_base(&image);
        XImage_init(&image);
    }
    /* 对标 QSvgIconEngine 的 pixmap 缓存诉求：每次请求都完整「读文件 +
       解析 SVG + 光栅化」代价毫秒级，hover/状态切换/窗口重绘高频触发。
       XImageCache（默认开）在此路径命中后跳过全部 IO 与解析，是本缓存
       的首个受益者。 */
    if (!XImage_load(&image, se->m_fileName, NULL) || XImage_isNull(&image)) {
        XImage_deinit_base(&image);
        return;
    }
    /* 根因（R-108）：此前 (void)size 显式忽略请求尺寸，按 SVG 固有尺寸
     * 出图且引擎路径无事后缩放。对标 QSvgIconEngine::pixmap 按请求尺寸
     * 渲染（QSvgRenderer::render 默认保持宽高比装入目标矩形）：有效请求
     * 尺寸且与固有尺寸不同时缩放出图，缩放失败回退固有尺寸（不空手）。 */
    renderImage = &image;
    if (size && size->width > 0 && size->height > 0 &&
        (size->width != XImage_width(&image) ||
         size->height != XImage_height(&image))) {
        XImage_init(&scaled);
        scaledInited = true;
        /* aspectMode=1 KeepAspectRatio；mode=1 平滑插值（矢量观感）。 */
        XImage_scaled(&image, size->width, size->height, 1u, 1u, &scaled);
        if (!XImage_isNull(&scaled)) renderImage = &scaled;
    }
    XPixmap_fromImage(renderImage, 0, out);
    if (scaledInited) XImage_deinit_base(&scaled);
    XImage_deinit_base(&image);
}

/** @brief Key 虚槽：返回 "svg"。 */
static XString* VSvgEngine_key(const XIconEngine* self)
{
    (void)self;
    return XString_create_utf8("svg");
}

/** @brief IconName 虚槽：Qt SVG 引擎返回空串。 */
static XString* VSvgEngine_iconName(const XIconEngine* self)
{
    (void)self;
    return XString_create();
}

/** @brief IsNull 虚槽：文件名为空判定。 */
static bool VSvgEngine_isNull(const XIconEngine* self)
{
    XSvgIconEngine* se = (XSvgIconEngine*)self;
    return !se || !se->m_fileName ||
           XString_length_base(se->m_fileName) == 0;
}

static void VSvgEngine_deinit(XSvgIconEngine* self)
{
    if (!self) return;
    if (self->m_fileName) {
        XString_delete_base(self->m_fileName);
        self->m_fileName = NULL;
    }
    XClass_Deinit_Parent(XIconEngine, (XIconEngine*)self);
}

XVtable* XSvgIconEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSvgIconEngine)
    XVTABLE_INHERIT_XCLASS(XIconEngine);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Pixmap, VSvgEngine_pixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Key, VSvgEngine_key);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IconName, VSvgEngine_iconName);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IsNull, VSvgEngine_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VSvgEngine_deinit);
    return XVTABLE_DEFAULT;
}

static void VSvgEngine_init(XSvgIconEngine* self, const XString* fileName)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XIconEngine_init((XIconEngine*)self);
    XClassSetVtable(self, XSvgIconEngine);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    if (fileName)
        self->m_fileName = XString_create_copy(fileName);
}

XSvgIconEngine* XSvgIconEngine_create(const XString* fileName)
{
    XSvgIconEngine* self =
        (XSvgIconEngine*)XMemory_malloc(sizeof(*self),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    VSvgEngine_init(self, fileName);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

XSvgIconEngine* XSvgIconEngine_create_2(const char* utf8FileName)
{
    XSvgIconEngine* engine;
    XString* name;
    if (!utf8FileName) return XSvgIconEngine_create(NULL);
    name = XString_create_utf8(utf8FileName);
    if (!name) return NULL;
    engine = XSvgIconEngine_create(name);
    XString_delete_base(name);
    return engine;
}
#endif /* XSVGICON_ON */

