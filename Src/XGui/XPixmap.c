/******************************************************************************
 * @file       XPixmap.c
 * @brief      XPixmap 像素图类实现（对标 Qt 6.8 QPixmap）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPixmap.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

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

static XPlatformPixmap* XPlatformPixmap_createFromImage(const XImage* image)
{
    if (!image) return NULL;
    XPlatformPixmap* d = (XPlatformPixmap*)XMalloc_System(sizeof(XPlatformPixmap));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPlatformPixmap));
    XAtomic_init(d->m_refCount, 1);
    XImage_copy(&d->m_image, image);
    d->m_devicePixelRatio = 1.0f;
    d->m_serialNumber = g_pixmapSerialCounter;
    d->m_cacheKey = XPlatformPixmap_nextCacheKey();
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
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest == src) return;
    if (dest->m_data)
        XPlatformPixmap_unref(dest->m_data);
    dest->m_data = src->m_data;
    XPlatformPixmap_ref(dest->m_data);
}

static void VXPixmap_move(XPixmap* dest, XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest == src) return;
    if (dest->m_data)
        XPlatformPixmap_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXPixmap_deinit(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    if (self->m_data)
    {
        XPlatformPixmap_unref(self->m_data);
        self->m_data = NULL;
    }
}

/* ========== 虚函数表初始化 ========== */

XVtable* XPixmap_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XPixmap));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPixmap_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPixmap_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPixmap_deinit);
    return XVTABLE_DEFAULT;
}

XPixmap* XPixmap_create()
{
    XPixmap* self = (XPixmap*)XMalloc_System(sizeof(XPixmap));
    if (!self) return NULL;
    XPixmap_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XPixmap_init(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    memset(self, 0, sizeof(XPixmap));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPixmap);
}

void XPixmap_init_ex(XPixmap* self, int width, int height)
{
    if (ISNULL(self, "XPixmap")) return;
    XPixmap_init(self);
    if (width > 0 && height > 0)
        self->m_data = XPlatformPixmap_create(width, height);
}

void XPixmap_init_size(XPixmap* self, const XSize* size)
{
    if (size)
        XPixmap_init_ex(self, size->width, size->height);
    else
        XPixmap_init(self);
}

void XPixmap_init_file(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    XPixmap_init(self);
    XPixmap_load(self, fileName, format, flags);
}

void XPixmap_init_image(XPixmap* self, const XImage* image, uint32_t flags)
{
    if (ISNULL(self, "XPixmap") || ISNULL(image, "XImage")) return;
    XPixmap_init(self);
    self->m_data = XPlatformPixmap_createFromImage(image);
}

void XPixmap_init_bitmap_image(XPixmap* self, const XImage* image, uint32_t flags)
{
    XPixmap_init_image(self, image, flags);
    if (self && self->m_data)
        self->m_data->m_isQBitmap = true;
}

void XPixmap_copy(XPixmap* self, const XPixmap* other)
{
    if (ISNULL(self, "XPixmap") || ISNULL(other, "XPixmap")) return;
    if (self == other) return;
    if (!XClassIsVtableNull(self))
        XPixmap_deinit_base(self);
    XPixmap_init(self);
    XPixmap_copy_base(self, other);
}

void XPixmap_move(XPixmap* self, XPixmap* other)
{
    if (ISNULL(self, "XPixmap") || ISNULL(other, "XPixmap")) return;
    if (self == other) return;
    if (!XClassIsVtableNull(self))
        XPixmap_deinit_base(self);
    XPixmap_init(self);
    XPixmap_move_base(self, other);
}

void XPixmap_deinit(XPixmap* self)
{
    XPixmap_deinit_base(self);
}

void XPixmap_copy_base(XPixmap* dest, const XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    VXPixmap_copy(dest, src);
}
void XPixmap_move_base(XPixmap* dest, XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    VXPixmap_move(dest, src);
}

void XPixmap_deinit_base(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    VXPixmap_deinit(self);
}



void XPixmap_delete_base(XPixmap* self)
{
    XClass_delete_base((XClass*)self);
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
    XImage_fill(&self->m_data->m_image, color);
    XPlatformPixmap_touch(self->m_data);
}

void XPixmap_mask(const XPixmap* self, XPixmap* out)
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
    const XImageFormat format = XImage_format(&self->m_data->m_image);
    for (int y = 0; maskBits && y < XPixmap_height(self); ++y)
    {
        uint8_t* dst = maskBits + y * maskStride;
        const uint8_t* src = XImage_constScanLine(&self->m_data->m_image, y);
        for (int x = 0; src && x < XPixmap_width(self); ++x)
        {
            uint8_t alpha = 0xff;
            if (format == XImageFormat_ARGB32 || format == XImageFormat_ARGB32_Premultiplied)
                alpha = (uint8_t)(((const uint32_t*)src)[x] >> 24);
            else if (format == XImageFormat_Alpha8)
                alpha = src[x];
            if (alpha != 0)
                dst[x >> 3] |= (uint8_t)(1u << (x & 7));
        }
    }
    XPixmap_init_bitmap_image(out, &maskImage, 0);
    XImage_deinit_base(&maskImage);
}

void XPixmap_setMask(XPixmap* self, const XPixmap* mask)
{
    if (!self || !self->m_data || XPixmap_isNull(self)) return;
    if (mask && !XPixmap_isNull(mask) &&
        (XPixmap_width(mask) != XPixmap_width(self) || XPixmap_height(mask) != XPixmap_height(self)))
        return;

    XPixmap_detach(self);
    XImage_detach(&self->m_data->m_image);
    XImage* image = &self->m_data->m_image;
    XImageFormat format = XImage_format(image);
    if (format != XImageFormat_ARGB32 && format != XImageFormat_ARGB32_Premultiplied)
        return;

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
        XImage_copy_base(&tight, &maskImage);
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
    if (!self || !self->m_data || !out) return;
    XImage scaled;
    XImage_init(&scaled);
    XImage_scaled(&self->m_data->m_image, width, height, aspectMode, mode, &scaled);
    XPixmap_init_image(out, &scaled, 0);
    if (out->m_data)
    {
        out->m_data->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
        out->m_data->m_isQBitmap = self->m_data->m_isQBitmap;
    }
    XImage_deinit_base(&scaled);
}

void XPixmap_scaledToWidth(const XPixmap* self, int width, uint32_t mode, XPixmap* out)
{
    if (!out) return;
    if (!self || !self->m_data || width <= 0 || XPixmap_width(self) <= 0)
    {
        XPixmap_init(out);
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
        XPixmap_init(out);
        return;
    }
    int width = XPixmap_width(self) * height / XPixmap_height(self);
    XPixmap_scaled(self, width, height, 0, mode, out);
}

void XPixmap_transformed(const XPixmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, uint32_t mode, XPixmap* out)
{
    (void)self;
    (void)m00;
    (void)m01;
    (void)m02;
    (void)m10;
    (void)m11;
    (void)m12;
    (void)mode;
    (void)out;
}

/* ========== 转换方法 ========== */

void XPixmap_toImage(const XPixmap* self, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    XImage_init(out);
    XImage_copy_base(out, &self->m_data->m_image);
}

void XPixmap_fromImage(const XImage* image, uint32_t flags, XPixmap* out)
{
    if (!image || !out) return;
    XPixmap_init_image(out, image, flags);
}

void XPixmap_fromImageReader(void* reader, uint32_t flags, XPixmap* out)
{
    (void)reader;
    (void)flags;
    (void)out;
}

/* ========== 文件操作 ========== */

bool XPixmap_load(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    (void)self;
    (void)fileName;
    (void)format;
    (void)flags;
    return false;
}

bool XPixmap_loadFromData(XPixmap* self, const uint8_t* buf, uint32_t len, const char* format, uint32_t flags)
{
    (void)self;
    (void)buf;
    (void)len;
    (void)format;
    (void)flags;
    return false;
}

bool XPixmap_save(const XPixmap* self, const char* fileName, const char* format, int quality)
{
    if (!self || !self->m_data || XPixmap_isNull(self) || !fileName)
        return false;
    return XImage_save(&self->m_data->m_image, fileName, format, quality);
}

/* ========== 其他 ========== */

void XPixmap_copyRect(const XPixmap* self, const XRect* rect, XPixmap* out)
{
    if (!self || !self->m_data || !out) return;
    XImage copied;
    XImage_init(&copied);
    XImage_copyRect(&self->m_data->m_image, rect, &copied);
    XPixmap_init_image(out, &copied, 0);
    XImage_deinit_base(&copied);
}

void XPixmap_scroll(XPixmap* self, int dx, int dy, const XRect* rect, XRegion* exposed)
{
    if (!self || !self->m_data || XPixmap_isNull(self) || (dx == 0 && dy == 0)) return;
    XRect dest = rect ? *rect : (XRect){0, 0, XPixmap_width(self), XPixmap_height(self)};
    int x1 = dest.x < 0 ? 0 : dest.x;
    int y1 = dest.y < 0 ? 0 : dest.y;
    int x2 = dest.x + dest.width;
    int y2 = dest.y + dest.height;
    if (x2 > XPixmap_width(self)) x2 = XPixmap_width(self);
    if (y2 > XPixmap_height(self)) y2 = XPixmap_height(self);
    if (x2 <= x1 || y2 <= y1) return;
    dest = (XRect){x1, y1, x2 - x1, y2 - y1};

    XRect src = {dest.x - dx, dest.y - dy, dest.width, dest.height};
    if (src.x < dest.x) { src.width -= dest.x - src.x; src.x = dest.x; }
    if (src.y < dest.y) { src.height -= dest.y - src.y; src.y = dest.y; }
    if (src.x + src.width > dest.x + dest.width) src.width = dest.x + dest.width - src.x;
    if (src.y + src.height > dest.y + dest.height) src.height = dest.y + dest.height - src.y;
    if (src.width <= 0 || src.height <= 0)
    {
        if (exposed)
            XRegion_addRect(exposed, &dest);
        return;
    }
    XRect moved = {src.x + dx, src.y + dy, src.width, src.height};
    if (src.width > 0 && src.height > 0)
    {
        XImage oldImage;
        XImage_init(&oldImage);
        XImage_copy_base(&oldImage, &self->m_data->m_image);
        XPixmap_detach(self);
        XImage_detach(&self->m_data->m_image);
        const int bpp = XImage_depth(&oldImage);
        if ((bpp % 8) == 0)
        {
            const int bytes = bpp / 8;
            for (int y = 0; y < src.height; ++y)
            {
                const uint8_t* s = XImage_constScanLine(&oldImage, src.y + y) + src.x * bytes;
                uint8_t* d = XImage_scanLine(&self->m_data->m_image, moved.y + y) + moved.x * bytes;
                memcpy(d, s, (size_t)src.width * bytes);
            }
        }
        else
        {
            XImageFormat fmt = XImage_format(&oldImage);
            for (int y = 0; y < src.height; ++y)
                for (int x = 0; x < src.width; ++x)
                {
                    const uint8_t* s = XImage_constScanLine(&oldImage, src.y + y);
                    uint8_t* d = XImage_scanLine(&self->m_data->m_image, moved.y + y);
                    unsigned sm = fmt == XImageFormat_Mono ? (0x80u >> ((src.x + x) & 7)) : (1u << ((src.x + x) & 7));
                    unsigned dm = fmt == XImageFormat_Mono ? (0x80u >> ((moved.x + x) & 7)) : (1u << ((moved.x + x) & 7));
                    if (s[(src.x + x) >> 3] & sm) d[(moved.x + x) >> 3] |= (uint8_t)dm;
                    else d[(moved.x + x) >> 3] &= (uint8_t)~dm;
                }
        }
        XImage_deinit_base(&oldImage);
        XPlatformPixmap_touch(self->m_data);
    }
    if (exposed)
    {
        if (moved.y > dest.y) { XRect r = {dest.x, dest.y, dest.width, moved.y - dest.y}; XRegion_addRect(exposed, &r); }
        if (moved.y + moved.height < dest.y + dest.height) { XRect r = {dest.x, moved.y + moved.height, dest.width, dest.y + dest.height - moved.y - moved.height}; XRegion_addRect(exposed, &r); }
        if (moved.x > dest.x) { XRect r = {dest.x, moved.y, moved.x - dest.x, moved.height}; XRegion_addRect(exposed, &r); }
        if (moved.x + moved.width < dest.x + dest.width) { XRect r = {moved.x + moved.width, moved.y, dest.x + dest.width - moved.x - moved.width, moved.height}; XRegion_addRect(exposed, &r); }
    }
}

int64_t XPixmap_cacheKey(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_cacheKey : 0;
}

bool XPixmap_isDetached(const XPixmap* self)
{
    return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}

void XPixmap_detach(XPixmap* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XPlatformPixmap* newData = XPlatformPixmap_createFromImage(&self->m_data->m_image);
        if (newData)
        {
            newData->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
            newData->m_isQBitmap = self->m_data->m_isQBitmap;
            XPlatformPixmap_unref(self->m_data);
            self->m_data = newData;
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
    if (!self || !self->m_data || scaleFactor <= 0.0f || scaleFactor == self->m_data->m_devicePixelRatio) return;
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
    if (!image || !out) return;
    XPixmap_init(out);
    out->m_data = XPlatformPixmap_createFromImage(image);
}



