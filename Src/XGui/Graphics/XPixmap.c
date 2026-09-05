/******************************************************************************
 * @file       XPixmap.c
 * @brief      XPixmap 像素图类实现（对标 Qt 6.8 QPixmap）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPixmap.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XImageReader.h"
#include "XBitmap.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

/* ========== 私有数据结构 ========== */

/**
 * @brief      XPlatformPixmap 平台像素图数据
 * @note       内部包装 XImage，提供平台相关优化
 */
typedef struct XPlatformPixmap
{
    XAtomic_int32_t  m_refCount;        /**< 引用计数 */
    XImage           m_image;            /**< 内部图像数据 */
    float            m_devicePixelRatio; /**< 设备像素比 */
    bool             m_isQBitmap;        /**< 是否为位图 */
    int64_t          m_cacheKey;         /**< 缓存键值 */
    int              m_serialNumber;     /**< 序列号 */
}XPlatformPixmap;

static int g_pixmapSerialCounter = 1;

static void XPlatformPixmap_unref(XPlatformPixmap* d);

/*
 * 输出对象的初始化状态由对象自身的 vtable 表示。比较完整的 vtable
 * 指针对象表示，避免把随机内容当作可调用的函数表；命中 XPixmap 或
 * XBitmap 时才允许释放旧的平台数据。未初始化输出由 copy/move/init
 * 直接建立标准 XClass 状态，不依赖全局地址登记或额外分配。
 */
static bool XPixmap_vtableIs(const XPixmap* self, XVtable* expected)
{
    unsigned char actualBytes[sizeof(void*)];
    unsigned char expectedBytes[sizeof(void*)];
    if (!self || !expected) return false;
    memcpy(actualBytes, &self->m_class.m_vtable, sizeof(actualBytes));
    memcpy(expectedBytes, &expected, sizeof(expectedBytes));
    return memcmp(actualBytes, expectedBytes, sizeof(actualBytes)) == 0;
}

static bool XPixmap_isInitializedObject(const XPixmap* self)
{
    return XPixmap_vtableIs(self, XPixmap_class_init()) ||
           XPixmap_vtableIs(self, XBitmap_class_init());
}

static void XPixmap_setData(XPixmap* self, XPlatformPixmap* data)
{
    if (self) self->m_data = data;
}

static XPlatformPixmap* XPixmap_takeData(XPixmap* self)
{
    XPlatformPixmap* data;
    if (!self) return NULL;
    data = self->m_data;
    self->m_data = NULL;
    return data;
}

static void XPixmap_replaceData(XPixmap* self, XPlatformPixmap* data)
{
    XPlatformPixmap* oldData = XPixmap_takeData(self);
    XPixmap_setData(self, data);
    if (oldData && oldData != data)
        XPlatformPixmap_unref(oldData);
}

static void XPixmap_releaseData(XPixmap* self)
{
    XPlatformPixmap_unref(XPixmap_takeData(self));
}

static void XPixmap_resetOutput(XPixmap* out)
{
    if (!out) return;
    XPixmap_init(out);
}

static int64_t XPlatformPixmap_nextCacheKey(void)
{
    return (int64_t)(uint32_t)g_pixmapSerialCounter++ << 32;
}

static void XPlatformPixmap_touch(XPlatformPixmap* d)
{
    if (d)
        d->m_cacheKey = XPlatformPixmap_nextCacheKey();
}

static XPlatformPixmap* XPlatformPixmap_create(int width, int height)
{
    XPlatformPixmap* d = (XPlatformPixmap*)XMalloc_System(sizeof(XPlatformPixmap));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPlatformPixmap));
    XAtomic_init(d->m_refCount, 1);
    XImage_init_ex(&d->m_image, width, height, XImageFormat_ARGB32_Premultiplied);
    d->m_devicePixelRatio = 1.0f;
    d->m_serialNumber = g_pixmapSerialCounter;
    d->m_cacheKey = XPlatformPixmap_nextCacheKey();
    return d;
}

static XPlatformPixmap* XPlatformPixmap_createFromImage(const XImage* image,
                                                        uint32_t flags,
                                                        bool bitmap)
{
    XImage converted;
    const XImage* source = image;
    XImageFormat targetFormat;
    bool convertedActive = false;
    XPlatformPixmap* d;
    if (!image || XImage_isNull(image)) return NULL;

    /* Qt 6.8 qpixmap_raster.cpp:269-308 selects the preferred raster format:
       opaque images use RGB32, alpha images use premultiplied ARGB32, and a
       depth-one source is expanded to one of those 32-bit formats.  The
       explicit NoFormatConversion and NoOpaqueDetection flags are preserved
       here so callers can request the source format or retain an alpha plane. */
    if (!(flags & XPixmapImageConversion_NoFormatConversion))
    {
        if (bitmap)
            targetFormat = XImageFormat_MonoLSB;
        else if (XImage_depth(image) == 1)
            targetFormat = XImage_hasAlphaChannel(image)
                ? XImageFormat_ARGB32_Premultiplied : XImageFormat_RGB32;
        else
            targetFormat = (XImage_hasAlphaChannel(image) &&
                            ((flags & XPixmapImageConversion_NoOpaqueDetection) ||
                             XImage_hasAlpha(image)))
                ? XImageFormat_ARGB32_Premultiplied : XImageFormat_RGB32;
        if (targetFormat != XImage_format(image))
        {
            XImage_init(&converted);
            XImage_convertToFormat(image, targetFormat, flags, &converted);
            if (XImage_isNull(&converted))
            {
                XImage_deinit_base(&converted);
                return NULL;
            }
            source = &converted;
            convertedActive = true;
        }
    }

    d = (XPlatformPixmap*)XMalloc_System(sizeof(XPlatformPixmap));
    if (!d)
    {
        if (convertedActive)
            XImage_deinit_base(&converted);
        return NULL;
    }
    memset(d, 0, sizeof(XPlatformPixmap));
    XAtomic_init(d->m_refCount, 1);
    XCopy(&d->m_image, source);
    d->m_devicePixelRatio = XImage_devicePixelRatio(source);
    d->m_serialNumber = g_pixmapSerialCounter;
    d->m_cacheKey = XPlatformPixmap_nextCacheKey();
    if (convertedActive)
        XImage_deinit_base(&converted);
    return d;
}

static void XPlatformPixmap_ref(XPlatformPixmap* d)
{
    if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst);
}

static void XPlatformPixmap_unref(XPlatformPixmap* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        XImage_deinit_base(&d->m_image);
        XFree_System(d);
    }
}

/* ========== 虚函数实现 ========== */

static void VXPixmap_copy(XPixmap* dest, const XPixmap* src)
{
    XPlatformPixmap* data;
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest == src) return;
    if (!XPixmap_isInitializedObject(dest)) XPixmap_init(dest);
    XPixmap_releaseData(dest);
    data = src->m_data;
    XPlatformPixmap_ref(data);
    XPixmap_setData(dest, data);
}

static void VXPixmap_move(XPixmap* dest, XPixmap* src)
{
    XPlatformPixmap* data;
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest == src) return;
    if (!XPixmap_isInitializedObject(dest)) XPixmap_init(dest);
    XPixmap_releaseData(dest);
    data = src->m_data;
    XPixmap_setData(dest, data);
    XPixmap_setData(src, NULL);
}

static void VXPixmap_deinit(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    XPixmap_releaseData(self);
}

/* ========== 虚函数表初始化 ========== */

XVtable* XPixmap_class_init()
{
    XVTABLE_INIT_DEFAULT(XPixmap)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPixmap_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPixmap_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPixmap_deinit);
    return XVTABLE_DEFAULT;
}

XPixmap* XPixmap_create_ex(XMemoryType memory)
{
    XPixmap* self = (XPixmap*)XMemory_malloc(sizeof(XPixmap), memory);
    if (!self) return NULL;
    XPixmap_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XPixmap_init(XPixmap* self)
{
    XMemory* memory = NULL;
    bool isHeap = false;
    bool wasInitialized;
    if (ISNULL(self, "XPixmap")) return;
    wasInitialized = XPixmap_isInitializedObject(self);
    if (wasInitialized)
    {
        memory = Class_Memory(self);
        isHeap = Class_IsHeap(self) != 0;
        XPixmap_releaseData(self);
    }
    memset(self, 0, sizeof(XPixmap));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPixmap);
    if (wasInitialized)
    {
        if (memory) Class_Memory(self) = memory;
        Class_IsHeap(self) = isHeap;
    }
}

void XPixmap_init_ex(XPixmap* self, int width, int height)
{
    if (ISNULL(self, "XPixmap")) return;
    XPixmap_init(self);
    if (width > 0 && height > 0)
        XPixmap_setData(self, XPlatformPixmap_create(width, height));
}

void XPixmap_init_size(XPixmap* self, const XSize* size)
{
    if (size)
        XPixmap_init_ex(self, size->width, size->height);
    else
        XPixmap_init(self);
}

void XPixmap_init_file_2(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XPixmap_init_file(self, fileNameString, formatString, flags);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XPixmap_init_file(XPixmap* self, const XString* fileName, const XString* format, uint32_t flags)
{
    if (!self) return;
    if (!XPixmap_load(self, fileName, format, flags))
        XPixmap_resetOutput(self);
}

void XPixmap_init_image(XPixmap* self, const XImage* image, uint32_t flags)
{
    if (ISNULL(self, "XPixmap") || ISNULL(image, "XImage")) return;
    XPixmap_init(self);
    XPixmap_setData(self, XPlatformPixmap_createFromImage(image, flags, false));
}

void XPixmap_init_bitmap_image(XPixmap* self, const XImage* image, uint32_t flags)
{
    if (ISNULL(self, "XPixmap") || ISNULL(image, "XImage")) return;
    XPixmap_init(self);
    XPixmap_setData(self, XPlatformPixmap_createFromImage(image, flags, true));
    if (self && self->m_data)
        self->m_data->m_isQBitmap = true;
}

/* ========== 查询方法 ========== */

bool XPixmap_isNull(const XPixmap* self)
{
    return !self || !self->m_data || XImage_isNull(&self->m_data->m_image);
}

int XPixmap_width(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_width(&self->m_data->m_image) : 0;
}

int XPixmap_height(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_height(&self->m_data->m_image) : 0;
}

void XPixmap_size(const XPixmap* self, XSize* out)
{
    if (out)
    {
        out->width = XPixmap_width(self);
        out->height = XPixmap_height(self);
    }
}

void XPixmap_rect(const XPixmap* self, XRect* out)
{
    if (out)
    {
        out->x = 0;
        out->y = 0;
        out->width = XPixmap_width(self);
        out->height = XPixmap_height(self);
    }
}

int XPixmap_depth(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_depth(&self->m_data->m_image) : 0;
}

int XPixmap_defaultDepth() { return 32; }

/* ========== 填充与掩码 ========== */

void XPixmap_fill(XPixmap* self, uint32_t color)
{
    if (!self || !self->m_data) return;
    XPixmap_detach(self);
    /* QPixmap::fill() takes a color; XImage::fill() mirrors QImage's raw
       pixel overload, so route through the color-aware rectangle helper. */
    XImage_fillRect(&self->m_data->m_image, NULL, color);
    XPlatformPixmap_touch(self->m_data);
}

void XPixmap_mask_2(const XPixmap* self, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || XPixmap_isNull(self))
    {
        XPixmap_init(out);
        return;
    }

    XImage maskImage;
    XImage_init_ex(&maskImage, XPixmap_width(self), XPixmap_height(self), XImageFormat_MonoLSB);
    XImage_fill(&maskImage, 0);
    uint8_t* maskBits = XImage_bits(&maskImage);
    const int maskStride = XImage_bytesPerLine(&maskImage);
    for (int y = 0; maskBits && y < XPixmap_height(self); ++y)
    {
        uint8_t* dst = maskBits + y * maskStride;
        const uint8_t* src = XImage_constScanLine(&self->m_data->m_image, y);
        for (int x = 0; src && x < XPixmap_width(self); ++x)
        {
            uint8_t alpha = XImage_hasAlphaChannel(&self->m_data->m_image)
                ? (uint8_t)(XImage_pixel(&self->m_data->m_image, x, y) >> 24) : 0xffu;
            if (alpha != 0)
                dst[x >> 3] |= (uint8_t)(1u << (x & 7));
        }
    }
    XPixmap_init_bitmap_image(out, &maskImage, 0);
    XImage_deinit_base(&maskImage);
}

void XPixmap_maskBitmap(const XPixmap* self, XBitmap* out)
{
    XPixmap_mask(self, out);
}

void XPixmap_mask(const XPixmap* self, XBitmap* out)
{
    XPixmap mask;
    if (!out) return;
    XPixmap_init(&mask);
    XPixmap_mask_2(self, &mask);
    XBitmap_init_pixmap(out, &mask);
    XPixmap_deinit_base(&mask);
}

void XPixmap_setMask(XPixmap* self, const XPixmap* mask)
{
    if (!self || !self->m_data || XPixmap_isNull(self)) return;
    if (mask && !XPixmap_isNull(mask) &&
        (XPixmap_width(mask) != XPixmap_width(self) || XPixmap_height(mask) != XPixmap_height(self)))
        return;
    if (mask && mask->m_data == self->m_data)
        return;

    XPixmap_detach(self);
    XImage* image = &self->m_data->m_image;
    XImageFormat format = XImage_format(image);
    if (format != XImageFormat_ARGB32 && format != XImageFormat_ARGB32_Premultiplied)
    {
        if (!XImage_convertToFormatInPlace(image, XImageFormat_ARGB32_Premultiplied, 0))
            return;
    }
    XImage_detach(image);

    for (int y = 0; y < XPixmap_height(self); ++y)
    {
        uint32_t* dst = (uint32_t*)XImage_scanLine(image, y);
        const uint8_t* maskLine = (mask && !XPixmap_isNull(mask))
            ? XImage_constScanLine(&mask->m_data->m_image, y) : NULL;
        XImageFormat maskFormat = maskLine ? XImage_format(&mask->m_data->m_image) : XImageFormat_Invalid;
        for (int x = 0; dst && x < XPixmap_width(self); ++x)
        {
            bool keep = true;
            if (maskLine && maskFormat == XImageFormat_Mono)
                keep = (maskLine[x >> 3] & (0x80u >> (x & 7))) != 0;
            else if (maskLine && maskFormat == XImageFormat_MonoLSB)
                keep = (maskLine[x >> 3] & (1u << (x & 7))) != 0;
            else if (maskLine)
                keep = XImage_pixel(&mask->m_data->m_image, x, y) != 0;
            if (!keep)
                dst[x] = 0;
            else if (!maskLine)
                dst[x] |= 0xff000000u;
        }
    }
    XPlatformPixmap_touch(self->m_data);
}

bool XPixmap_hasAlpha(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_hasAlpha(&self->m_data->m_image) : false;
}

bool XPixmap_hasAlphaChannel(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_hasAlphaChannel(&self->m_data->m_image) : false;
}

void XPixmap_createHeuristicMask(const XPixmap* self, bool clipTight, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || XPixmap_isNull(self))
    {
        XPixmap_init(out);
        return;
    }
    const int width = XPixmap_width(self);
    const int height = XPixmap_height(self);
    const XImage* source = &self->m_data->m_image;
    XImage maskImage;
    XImage_init_ex(&maskImage, width, height, XImageFormat_MonoLSB);
    uint8_t* maskBits = XImage_bits(&maskImage);
    const int stride = XImage_bytesPerLine(&maskImage);
    if (!maskBits)
    {
        XPixmap_init(out);
        XImage_deinit_base(&maskImage);
        return;
    }
    memset(maskBits, 0xff, (size_t)XImage_sizeInBytes(&maskImage));

    uint32_t corners[4];
    corners[0] = XImage_pixel(source, 0, 0) & 0x00ffffffu;
    corners[1] = XImage_pixel(source, width - 1, 0) & 0x00ffffffu;
    corners[2] = XImage_pixel(source, 0, height - 1) & 0x00ffffffu;
    corners[3] = XImage_pixel(source, width - 1, height - 1) & 0x00ffffffu;
    if (XImage_format(source) == XImageFormat_ARGB32_Premultiplied)
    {
        corners[0] = ((const uint32_t*)XImage_constScanLine(source, 0))[0] & 0x00ffffffu;
        corners[1] = ((const uint32_t*)XImage_constScanLine(source, 0))[width - 1] & 0x00ffffffu;
        corners[2] = ((const uint32_t*)XImage_constScanLine(source, height - 1))[0] & 0x00ffffffu;
        corners[3] = ((const uint32_t*)XImage_constScanLine(source, height - 1))[width - 1] & 0x00ffffffu;
    }
    uint32_t background = corners[0];
    int bestVotes = 0;
    for (int i = 0; i < 4; ++i)
    {
        int votes = 0;
        for (int j = 0; j < 4; ++j) if (corners[i] == corners[j]) ++votes;
        if (votes > bestVotes) { bestVotes = votes; background = corners[i]; }
    }

    bool changed;
    do
    {
        changed = false;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                uint8_t* line = maskBits + y * stride;
                unsigned bit = 1u << (x & 7);
                if (!(line[x >> 3] & bit)) continue;
                bool touchesBackground = x == 0 || y == 0 || x == width - 1 || y == height - 1;
                if (!touchesBackground && !(line[(x - 1) >> 3] & (1u << ((x - 1) & 7)))) touchesBackground = true;
                if (!touchesBackground && !(line[(x + 1) >> 3] & (1u << ((x + 1) & 7)))) touchesBackground = true;
                if (!touchesBackground && !(maskBits[(y - 1) * stride + (x >> 3)] & bit)) touchesBackground = true;
                if (!touchesBackground && !(maskBits[(y + 1) * stride + (x >> 3)] & bit)) touchesBackground = true;
                uint32_t pixel = XImage_pixel(source, x, y) & 0x00ffffffu;
                if (XImage_format(source) == XImageFormat_ARGB32_Premultiplied)
                    pixel = ((const uint32_t*)XImage_constScanLine(source, y))[x] & 0x00ffffffu;
                if (touchesBackground && pixel == background)
                {
                    line[x >> 3] &= (uint8_t)~bit;
                    changed = true;
                }
            }
    } while (changed);

    if (!clipTight)
    {
        XImage tight;
        XImage_init(&tight);
        XCopy(&tight, &maskImage);
        XImage_detach(&maskImage);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const uint8_t* sourceLine = XImage_constScanLine(&tight, y);
                if (!(sourceLine[x >> 3] & (1u << (x & 7)))) continue;
                if (x > 0) XImage_scanLine(&maskImage, y)[(x - 1) >> 3] |= (uint8_t)(1u << ((x - 1) & 7));
                if (x + 1 < width) XImage_scanLine(&maskImage, y)[(x + 1) >> 3] |= (uint8_t)(1u << ((x + 1) & 7));
                if (y > 0) XImage_scanLine(&maskImage, y - 1)[x >> 3] |= (uint8_t)(1u << (x & 7));
                if (y + 1 < height) XImage_scanLine(&maskImage, y + 1)[x >> 3] |= (uint8_t)(1u << (x & 7));
            }
        XImage_deinit_base(&tight);
    }
    XPixmap_init_bitmap_image(out, &maskImage, 0);
    XImage_deinit_base(&maskImage);
}

void XPixmap_createMaskFromColor(const XPixmap* self, uint32_t maskColor, uint32_t mode, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || XPixmap_isNull(self))
    {
        XPixmap_init(out);
        return;
    }
    XImage maskImage;
    XImage_init_ex(&maskImage, XPixmap_width(self), XPixmap_height(self), XImageFormat_MonoLSB);
    XImage_fill(&maskImage, 0);
    uint8_t* bits = XImage_bits(&maskImage);
    int stride = XImage_bytesPerLine(&maskImage);
    for (int y = 0; bits && y < XPixmap_height(self); ++y)
    {
        uint8_t* dst = bits + y * stride;
        for (int x = 0; x < XPixmap_width(self); ++x)
        {
            uint32_t pixel = XImage_pixel(&self->m_data->m_image, x, y);
            if (XImage_format(&self->m_data->m_image) == XImageFormat_ARGB32_Premultiplied)
                pixel = ((const uint32_t*)XImage_constScanLine(&self->m_data->m_image, y))[x];
            bool set = pixel == maskColor;
            if (mode != 0)
                set = !set;
            if (set)
                dst[x >> 3] |= (uint8_t)(1u << (x & 7));
        }
    }
    XPixmap_init_bitmap_image(out, &maskImage, 0);
    XImage_deinit_base(&maskImage);
}

/* ========== 缩放与变换 ========== */

void XPixmap_scaled(const XPixmap* self, int width, int height, uint32_t aspectMode, uint32_t mode, XPixmap* out)
{
    float sourceDevicePixelRatio;
    bool sourceIsQBitmap;
    if (!out) return;
    if (!self || !self->m_data || width <= 0 || height <= 0) {
        XPixmap_resetOutput(out);
        return;
    }
    sourceDevicePixelRatio = self->m_data->m_devicePixelRatio;
    sourceIsQBitmap = self->m_data->m_isQBitmap;
    XImage scaled;
    XImage_init(&scaled);
    XImage_scaled(&self->m_data->m_image, width, height, aspectMode, mode, &scaled);
    XPixmap_init_image(out, &scaled, 0);
    if (out->m_data)
    {
        out->m_data->m_devicePixelRatio = sourceDevicePixelRatio;
        out->m_data->m_isQBitmap = sourceIsQBitmap;
    }
    XImage_deinit_base(&scaled);
}

void XPixmap_scaledToWidth(const XPixmap* self, int width, uint32_t mode, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || width <= 0 || XPixmap_width(self) <= 0)
    {
        XPixmap_resetOutput(out);
        return;
    }
    int height = XPixmap_height(self) * width / XPixmap_width(self);
    XPixmap_scaled(self, width, height, 0, mode, out);
}

void XPixmap_scaledToHeight(const XPixmap* self, int height, uint32_t mode, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || height <= 0 || XPixmap_height(self) <= 0)
    {
        XPixmap_resetOutput(out);
        return;
    }
    int width = XPixmap_width(self) * height / XPixmap_height(self);
    XPixmap_scaled(self, width, height, 0, mode, out);
}

void XPixmap_transformed(const XPixmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, uint32_t mode, XPixmap* out)
{
    float sourceDevicePixelRatio;
    bool sourceIsQBitmap;
    (void)mode;
    if (!out) return;
    if (!self || !self->m_data || XPixmap_isNull(self)) {
        XPixmap_resetOutput(out);
        return;
    }

    const int sourceWidth = XPixmap_width(self);
    const int sourceHeight = XPixmap_height(self);
    sourceDevicePixelRatio = self->m_data->m_devicePixelRatio;
    sourceIsQBitmap = self->m_data->m_isQBitmap;
    const float determinant = m00 * m11 - m01 * m10;
    if (sourceWidth <= 0 || sourceHeight <= 0 || fabsf(determinant) < 1.0e-7f) {
        XPixmap_resetOutput(out);
        return;
    }

    const float cornersX[4] = {0.0f, (float)sourceWidth, 0.0f, (float)sourceWidth};
    const float cornersY[4] = {0.0f, 0.0f, (float)sourceHeight, (float)sourceHeight};
    float minX = m00 * cornersX[0] + m01 * cornersY[0] + m02;
    float maxX = minX;
    float minY = m10 * cornersX[0] + m11 * cornersY[0] + m12;
    float maxY = minY;
    for (int i = 1; i < 4; ++i) {
        float x = m00 * cornersX[i] + m01 * cornersY[i] + m02;
        float y = m10 * cornersX[i] + m11 * cornersY[i] + m12;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }

    float widthFloat = ceilf(maxX) - floorf(minX);
    float heightFloat = ceilf(maxY) - floorf(minY);
    if (widthFloat <= 0.0f || heightFloat <= 0.0f ||
        widthFloat > (float)INT_MAX || heightFloat > (float)INT_MAX) {
        XPixmap_resetOutput(out);
        return;
    }
    int width = (int)widthFloat;
    int height = (int)heightFloat;
    float originX = floorf(minX);
    float originY = floorf(minY);

    XImage transformed;
    XImage_init_ex(&transformed, width, height,
                   self->m_data->m_isQBitmap ? XImageFormat_MonoLSB : XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(&transformed)) {
        XPixmap_resetOutput(out);
        XImage_deinit_base(&transformed);
        return;
    }
    XImage_fill(&transformed, 0);

    /* Sample the inverse affine transform. The image layer currently exposes
     * nearest-neighbour sampling as its portable primitive; callers still get
     * the same transformed bounds and pixel placement for every backend. */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float targetX = originX + (float)x + 0.5f;
            float targetY = originY + (float)y + 0.5f;
            float sourceX = (m11 * (targetX - m02) - m01 * (targetY - m12)) / determinant;
            float sourceY = (-m10 * (targetX - m02) + m00 * (targetY - m12)) / determinant;
            int sampleX = (int)floorf(sourceX);
            int sampleY = (int)floorf(sourceY);
            if (sampleX >= 0 && sampleX < sourceWidth && sampleY >= 0 && sampleY < sourceHeight)
                XImage_setPixel(&transformed, x, y, XImage_pixel(&self->m_data->m_image, sampleX, sampleY));
        }
    }

    XPixmap_init_image(out, &transformed, 0);
    if (out->m_data) {
        out->m_data->m_devicePixelRatio = sourceDevicePixelRatio;
        out->m_data->m_isQBitmap = sourceIsQBitmap;
    }
    XImage_deinit_base(&transformed);
}

void XPixmap_trueMatrix(const XImageTransform* matrix, int width, int height,
                        XImageTransform* out)
{
    if (!out) return;
    XImage_trueMatrix(matrix, width, height, out, NULL);
}

void XPixmap_trueMatrix_2(float m00, float m01, float m02, float m10, float m11,
                          float m12, int width, int height, XImageTransform* out)
{
    XImageTransform matrix = {0};
    matrix.m11 = m00;
    matrix.m12 = m10;
    matrix.m21 = m01;
    matrix.m22 = m11;
    matrix.dx = m02;
    matrix.dy = m12;
    XPixmap_trueMatrix(&matrix, width, height, out);
}

/* ========== 转换方法 ========== */

void XPixmap_toImage(const XPixmap* self, XImage* out)
{
    if (!out) return;
    XImage_init(out);
    if (!self || !self->m_data) return;
    XCopy(out, &self->m_data->m_image);
    if (out->m_data)
        XImage_setDevicePixelRatio(out, self->m_data->m_devicePixelRatio);
}

void XPixmap_fromImage(const XImage* image, uint32_t flags, XPixmap* out)
{
    if (!out) return;
    if (!image || XImage_isNull(image)) {
        XPixmap_resetOutput(out);
        return;
    }
    XPixmap_init_image(out, image, flags);
}

void XPixmap_fromImageReader(XImageReader* reader, uint32_t flags, XPixmap* out)
{
    (void)flags;
    if (!reader || !out) return;
    XImage image;
    XImage_init(&image);
    if (XImageReader_read((XImageReader*)reader, &image)) {
        XPixmap_init_image(out, &image, flags);
    } else {
        XPixmap_resetOutput(out);
    }
    XImage_deinit_base(&image);
}

bool XPixmap_loadDevice_2(XPixmap* self, XIODevice* device, const char* format, uint32_t flags)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    bool result = XPixmap_loadDevice(self, device, value, flags);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPixmap_loadDevice(XPixmap* self, XIODevice* device, const XString* format, uint32_t flags)
{
    XImage image;
    bool result;
    if (!self || !device) return false;
    XImage_init(&image);
    result = XImage_loadDevice(&image, device, format);
    if (result) {
        XPixmap_init_image(self, &image, flags);
    }
    XImage_deinit_base(&image);
    return result;
}

bool XPixmap_saveDevice_2(const XPixmap* self, XIODevice* device, const char* format, int quality)
{
    XString* value = format ? XString_create_utf8(format) : NULL;
    bool result = XPixmap_saveDevice(self, device, value, quality);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPixmap_saveDevice(const XPixmap* self, XIODevice* device, const XString* format, int quality)
{
    if (!self || !self->m_data || !device) return false;
    return XImage_saveDevice(&self->m_data->m_image, device, format, quality);
}

int XPixmap_devType(const XPixmap* self)
{
    return (self && self->m_data) ? 1 : 0;
}

void* XPixmap_paintEngine(const XPixmap* self)
{
    (void)self;
    return NULL;
}

bool XPixmap_convertFromImage(XPixmap* self, const XImage* image, uint32_t flags)
{
    XPlatformPixmap* replacement;
    if (!self) return false;
    /* Qt 6.8 qpixmap.cpp:969-976 first detaches and then assigns the result
       of fromImage(); a null QImage therefore clears the old pixmap and
       returns false instead of leaving stale contents behind. */
    if (!image || XImage_isNull(image))
    {
        XPixmap_releaseData(self);
        return false;
    }
    replacement = XPlatformPixmap_createFromImage(image, flags, false);
    if (!replacement) return false;
    XPixmap_replaceData(self, replacement);
    return true;
}

void XPixmap_swap(XPixmap* self, XPixmap* other)
{
    XPlatformPixmap* data;
    XPlatformPixmap* otherData;
    if (!self || !other || self == other) return;
    if (!XPixmap_isInitializedObject(self)) XPixmap_init(self);
    if (!XPixmap_isInitializedObject(other)) XPixmap_init(other);
    data = self->m_data;
    otherData = other->m_data;
    XPixmap_setData(self, otherData);
    XPixmap_setData(other, data);
}

/* ========== 文件操作 ========== */

bool XPixmap_load_2(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    (void)flags;
    if (!self || !fileName) return false;
    XImage image;
    XImage_init(&image);
    bool ok = XImage_load_2(&image, fileName, format);
    if (ok) {
        XPixmap_init_image(self, &image, flags);
    }
    XImage_deinit_base(&image);
    return ok;
}

bool XPixmap_load(XPixmap* self, const XString* fileName, const XString* format, uint32_t flags)
{
    return XPixmap_load_2(self, XString_toUtf8(fileName), XString_toUtf8(format), flags);
}

bool XPixmap_loadFromData_2(XPixmap* self, const uint8_t* buf, uint32_t len, const char* format, uint32_t flags)
{
    (void)flags;
    if (!self || !buf || len > (uint32_t)INT_MAX) return false;
    XImage image;
    XImage_init(&image);
    bool ok = XImage_loadFromData_2(&image, buf, (int)len, format);
    if (ok) {
        XPixmap_init_image(self, &image, flags);
    }
    XImage_deinit_base(&image);
    return ok;
}

bool XPixmap_loadFromData(XPixmap* self, const uint8_t* buf, uint32_t len,
                           const XString* format, uint32_t flags)
{
    return XPixmap_loadFromData_2(self, buf, len, XString_toUtf8(format), flags);
}

bool XPixmap_save_2(const XPixmap* self, const char* fileName, const char* format, int quality)
{
    if (!self || !self->m_data || XPixmap_isNull(self) || !fileName)
        return false;
    return XImage_save_2(&self->m_data->m_image, fileName, format, quality);
}

bool XPixmap_save(const XPixmap* self, const XString* fileName,
                  const XString* format, int quality)
{
    return XPixmap_save_2(self, XString_toUtf8(fileName), XString_toUtf8(format), quality);
}

/* ========== 其他 ========== */

void XPixmap_copyRect(const XPixmap* self, const XRect* rect, XPixmap* out)
{
    float sourceDevicePixelRatio;
    bool sourceIsQBitmap;
    if (!out) return;
    if (!self || !self->m_data) {
        XPixmap_resetOutput(out);
        return;
    }
    sourceDevicePixelRatio = self->m_data->m_devicePixelRatio;
    sourceIsQBitmap = self->m_data->m_isQBitmap;
    XImage copied;
    XImage_init(&copied);
    XImage_copyRect(&self->m_data->m_image, rect, &copied);
    XPixmap_init_image(out, &copied, 0);
    if (out->m_data) {
        out->m_data->m_devicePixelRatio = sourceDevicePixelRatio;
        out->m_data->m_isQBitmap = sourceIsQBitmap;
    }
    XImage_deinit_base(&copied);
}

void XPixmap_scroll(XPixmap* self, int dx, int dy, const XRect* rect, XRegion* exposed)
{
    const int imageWidth = XPixmap_width(self);
    const int imageHeight = XPixmap_height(self);
    int64_t destLeft;
    int64_t destTop;
    int64_t destRight;
    int64_t destBottom;
    int64_t srcLeft;
    int64_t srcTop;
    int64_t srcRight;
    int64_t srcBottom;
    int64_t movedLeft;
    int64_t movedTop;
    int64_t movedRight;
    int64_t movedBottom;
    XRect exposedRect;
    if (exposed) XRegion_clear(exposed);
    if (!self || !self->m_data || XPixmap_isNull(self) || imageWidth <= 0 || imageHeight <= 0)
        return;
    if (dx == 0 && dy == 0)
        return;

    destLeft = rect ? rect->x : 0;
    destTop = rect ? rect->y : 0;
    destRight = destLeft + (rect ? (int64_t)rect->width : imageWidth);
    destBottom = destTop + (rect ? (int64_t)rect->height : imageHeight);
    if (destLeft < 0) destLeft = 0;
    if (destTop < 0) destTop = 0;
    if (destRight > imageWidth) destRight = imageWidth;
    if (destBottom > imageHeight) destBottom = imageHeight;
    if (destRight <= destLeft || destBottom <= destTop)
        return;

    /* The source is the destination translated backwards, clipped back to
     * the destination. All arithmetic stays in int64_t to avoid overflow for
     * hostile rectangles or offsets. */
    srcLeft = destLeft > destLeft - (int64_t)dx ? destLeft : destLeft - (int64_t)dx;
    srcTop = destTop > destTop - (int64_t)dy ? destTop : destTop - (int64_t)dy;
    srcRight = destRight < destRight - (int64_t)dx ? destRight : destRight - (int64_t)dx;
    srcBottom = destBottom < destBottom - (int64_t)dy ? destBottom : destBottom - (int64_t)dy;
    if (srcRight <= srcLeft || srcBottom <= srcTop)
    {
        if (exposed) {
            exposedRect.x = (int)destLeft;
            exposedRect.y = (int)destTop;
            exposedRect.width = (int)(destRight - destLeft);
            exposedRect.height = (int)(destBottom - destTop);
            XRegion_addRect(exposed, &exposedRect);
        }
        return;
    }
    movedLeft = srcLeft + dx;
    movedTop = srcTop + dy;
    movedRight = srcRight + dx;
    movedBottom = srcBottom + dy;

    {
        XImage oldImage;
        XImage_init(&oldImage);
        XCopy(&oldImage, &self->m_data->m_image);
        XPixmap_detach(self);
        XImage_detach(&self->m_data->m_image);
        const int bpp = XImage_depth(&oldImage);
        if ((bpp % 8) == 0)
        {
            const int bytes = bpp / 8;
            for (int y = 0; y < (int)(srcBottom - srcTop); ++y)
            {
                const uint8_t* s = XImage_constScanLine(&oldImage, (int)srcTop + y) + (size_t)srcLeft * bytes;
                uint8_t* d = XImage_scanLine(&self->m_data->m_image, (int)movedTop + y) + (size_t)movedLeft * bytes;
                memcpy(d, s, (size_t)(srcRight - srcLeft) * bytes);
            }
        }
        else
        {
            XImageFormat fmt = XImage_format(&oldImage);
            for (int y = 0; y < (int)(srcBottom - srcTop); ++y)
                for (int x = 0; x < (int)(srcRight - srcLeft); ++x)
                {
                    const uint8_t* s = XImage_constScanLine(&oldImage, (int)srcTop + y);
                    uint8_t* d = XImage_scanLine(&self->m_data->m_image, (int)movedTop + y);
                    unsigned sm = fmt == XImageFormat_Mono ? (0x80u >> (((int)srcLeft + x) & 7)) : (1u << (((int)srcLeft + x) & 7));
                    unsigned dm = fmt == XImageFormat_Mono ? (0x80u >> (((int)movedLeft + x) & 7)) : (1u << (((int)movedLeft + x) & 7));
                    if (s[((int)srcLeft + x) >> 3] & sm) d[((int)movedLeft + x) >> 3] |= (uint8_t)dm;
                    else d[((int)movedLeft + x) >> 3] &= (uint8_t)~dm;
                }
        }
        XImage_deinit_base(&oldImage);
        XPlatformPixmap_touch(self->m_data);
    }

    if (exposed)
    {
        const int64_t dWidth = destRight - destLeft;
        if (movedTop > destTop) {
            exposedRect.x = (int)destLeft;
            exposedRect.y = (int)destTop;
            exposedRect.width = (int)dWidth;
            exposedRect.height = (int)(movedTop - destTop);
            XRegion_addRect(exposed, &exposedRect);
        }
        if (movedBottom < destBottom) {
            exposedRect.x = (int)destLeft;
            exposedRect.y = (int)movedBottom;
            exposedRect.width = (int)dWidth;
            exposedRect.height = (int)(destBottom - movedBottom);
            XRegion_addRect(exposed, &exposedRect);
        }
        if (movedLeft > destLeft) {
            exposedRect.x = (int)destLeft;
            exposedRect.y = (int)movedTop;
            exposedRect.width = (int)(movedLeft - destLeft);
            exposedRect.height = (int)(movedBottom - movedTop);
            XRegion_addRect(exposed, &exposedRect);
        }
        if (movedRight < destRight) {
            exposedRect.x = (int)movedRight;
            exposedRect.y = (int)movedTop;
            exposedRect.width = (int)(destRight - movedRight);
            exposedRect.height = (int)(movedBottom - movedTop);
            XRegion_addRect(exposed, &exposedRect);
        }
    }
}

int64_t XPixmap_cacheKey(const XPixmap* self)
{
    /* Qt 6.8 qpixmap.cpp:882-888 returns zero for a null pixmap, including
       the (invalid) state where a platform wrapper has no image contents. */
    return XPixmap_isNull(self) ? 0 : self->m_data->m_cacheKey;
}

bool XPixmap_isDetached(const XPixmap* self)
{
    /* Qt 6.8 qpixmap.cpp:955-957 reports false for a default/null pixmap;
       only an existing platform object with one owner is detached. */
    return self && self->m_data &&
           XAtomic_load_int32(&self->m_data->m_refCount,
                              XAtomic_MemoryOrder_Relaxed) == 1;
}

void XPixmap_detach(XPixmap* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XPlatformPixmap* newData = XPlatformPixmap_createFromImage(
            &self->m_data->m_image, XPixmapImageConversion_NoFormatConversion,
            self->m_data->m_isQBitmap);
        if (newData)
        {
            newData->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
            newData->m_isQBitmap = self->m_data->m_isQBitmap;
            XPixmap_replaceData(self, newData);
        }
    }
}

bool XPixmap_isQBitmap(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_isQBitmap : false;
}

float XPixmap_devicePixelRatio(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_devicePixelRatio : 1.0f;
}

void XPixmap_setDevicePixelRatio(XPixmap* self, float scaleFactor)
{
    if (XPixmap_isNull(self) || scaleFactor == self->m_data->m_devicePixelRatio) return;
    XPixmap_detach(self);
    self->m_data->m_devicePixelRatio = scaleFactor;
    XPlatformPixmap_touch(self->m_data);
}

void XPixmap_deviceIndependentSize(const XPixmap* self, XSizeF* out)
{
    if (out)
    {
        float ratio = XPixmap_devicePixelRatio(self);
        out->width = XPixmap_width(self) / ratio;
        out->height = XPixmap_height(self) / ratio;
    }
}

void XPixmap_fromImageInPlace(XImage* image, uint32_t flags, XPixmap* out)
{
    if (!out) return;
    XPixmap_resetOutput(out);
    if (!image || XImage_isNull(image)) return;
    XPixmap_setData(out, XPlatformPixmap_createFromImage(image, flags, false));
}
