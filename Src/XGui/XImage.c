/******************************************************************************
 * @file       XImage.c
 * @brief      XImage 图像类实现（对标 Qt 6.8 QImage）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XImageFormat.h"
#include "XCode/XAtomic/XAtomic.h"
#include "XClass/XClass.h"
#include "XClass/XVtable/XVtable.h"
#include "XMemory/XMemory.h"
#include <string.h>
#include <stdlib.h>

/* ========== 私有数据结构 ========== */

/**
 * @brief      XImage 图像数据私有结构体
 * @note       使用引用计数管理，支持写时复制（COW）
 */
typedef struct XImageData
{
    XAtomic_int32_t  m_refCount;        /**< 引用计数 */
    int              m_width;            /**< 图像宽度 */
    int              m_height;           /**< 图像高度 */
    int              m_depth;            /**< 位深度 */
    int              m_bytesPerLine;     /**< 每行字节数 */
    XImageFormat     m_format;           /**< 像素格式 */
    int              m_colorCount;       /**< 颜色表颜色数量 */
    uint32_t*        m_colorTable;       /**< 颜色表指针 */
    int              m_dpmX;             /**< X 方向分辨率（点/米） */
    int              m_dpmY;             /**< Y 方向分辨率（点/米） */
    int              m_offsetX;          /**< X 偏移 */
    int              m_offsetY;          /**< Y 偏移 */
    uint8_t*         m_data;             /**< 像素数据指针 */
    bool             m_ownsData;         /**< 是否拥有数据所有权 */
    void (*m_cleanupFunc)(void*);        /**< 清理回调函数 */
    void*            m_cleanupInfo;      /**< 清理回调参数 */
    int64_t          m_cacheKey;         /**< 缓存键值 */
    int              m_serialNumber;     /**< 序列号（用于生成缓存键） */
}XImageData;

static int g_imageSerialCounter = 0;  /**< 全局序列号计数器 */

/**
 * @brief      创建图像数据对象
 * @param width         图像宽度
 * @param height        图像高度
 * @param format        像素格式
 * @param bytesPerLine  每行字节数（0 表示自动计算）
 * @param data          像素数据指针（NULL 表示内部分配）
 * @param cleanupFunc   清理回调函数
 * @param cleanupInfo   清理回调参数
 * @return 图像数据指针，失败返回 NULL
 */
static XImageData* XImageData_create(int width, int height, XImageFormat format,
                                     int64_t bytesPerLine, uint8_t* data,
                                     void (*cleanupFunc)(void*), void* cleanupInfo)
{
    // 参数验证
    if (width <= 0 || height <= 0 || format < 0 || format >= XImageFormat_NImageFormats)
        return NULL;

    XImageData* d = (XImageData*)XMalloc_System(sizeof(XImageData));
    if (!d) return NULL;
    memset(d, 0, sizeof(XImageData));

    XAtomic_init(d->m_refCount, 1);
    d->m_width = width;
    d->m_height = height;
    d->m_format = format;
    d->m_depth = XImageFormat_bitDepth(format);
    d->m_cleanupFunc = cleanupFunc;
    d->m_cleanupInfo = cleanupInfo;

    // 计算每行字节数
    if (bytesPerLine <= 0)
        d->m_bytesPerLine = XImageFormat_bytesPerLine(width, format);
    else
        d->m_bytesPerLine = (int)bytesPerLine;

    // 分配或引用像素数据
    if (data)
    {
        d->m_data = data;
        d->m_ownsData = false;
    }
    else
    {
        int totalSize = d->m_bytesPerLine * height;
        d->m_data = (uint8_t*)XMalloc_System(totalSize);
        if (!d->m_data)
        {
            XFree_System(d);
            return NULL;
        }
        memset(d->m_data, 0, totalSize);
        d->m_ownsData = true;
    }

    // 生成缓存键
    d->m_serialNumber = (g_imageSerialCounter++);
    d->m_cacheKey = ((int64_t)d->m_serialNumber << 32) | (int64_t)(uintptr_t)d;

    return d;
}

/**
 * @brief      增加引用计数
 * @param d 图像数据指针
 */
static void XImageData_ref(XImageData* d)
{
    if (d)
        XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst);
}

/**
 * @brief      减少引用计数，减到 0 时释放资源
 * @param d 图像数据指针
 */
static void XImageData_unref(XImageData* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        // 引用计数归零，释放资源
        if (d->m_ownsData && d->m_data)
        {
            XFree_System(d->m_data);
        }
        else if (d->m_cleanupFunc && d->m_data)
        {
            d->m_cleanupFunc(d->m_cleanupInfo);
        }
        if (d->m_colorTable)
            XFree_System(d->m_colorTable);
        XFree_System(d);
    }
}

/* ========== XImage 类实现 ========== */

/**
 * @brief      虚函数：拷贝
 */
static void VXImage_copy(XImage* dest, const XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    // 释放旧数据
    if (dest->m_data)
        XImageData_unref(dest->m_data);
    // 共享新数据
    dest->m_data = src->m_data;
    XImageData_ref(dest->m_data);
}

/**
 * @brief      虚函数：移动
 */
static void VXImage_move(XImage* dest, XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    if (dest->m_data)
        XImageData_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

/**
 * @brief      虚函数：释放
 */
static void VXImage_deinit(XImage* self)
{
    if (ISNULL(self, "XImage")) return;
    if (self->m_data)
    {
        XImageData_unref(self->m_data);
        self->m_data = NULL;
    }
}

/* ========== 虚函数表初始化 ========== */

XVtable* XImage_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XImage));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXImage_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXImage_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImage_deinit);
    return XVTABLE_DEFAULT;
}

XImage* XImage_create()
{
    XImage* self = (XImage*)XMalloc_System(sizeof(XImage));
    if (!self) return NULL;
    XImage_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XImage_init(XImage* self)
{
    if (ISNULL(self, "XImage")) return;
    memset(self, 0, sizeof(XImage));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XImage);
}

void XImage_init_ex(XImage* self, int width, int height, XImageFormat format)
{
    if (ISNULL(self, "XImage")) return;
    XImage_init(self);
    self->m_data = XImageData_create(width, height, format, 0, NULL, NULL, NULL);
}

void XImage_init_ex_2(XImage* self, int width, int height, XImageFormat format,
                      int64_t bytesPerLine, uint8_t* data,
                      void (*cleanupFunction)(void*), void* cleanupInfo)
{
    if (ISNULL(self, "XImage")) return;
    XImage_init(self);
    self->m_data = XImageData_create(width, height, format, bytesPerLine, data, cleanupFunction, cleanupInfo);
}

void XImage_init_file(XImage* self, const char* fileName, const char* format)
{
    XImage_init(self);
    XImage_load(self, fileName, format);
}

void XImage_copy(XImage* self, const XImage* other)
{
    if (ISNULL(self, "XImage") || ISNULL(other, "XImage")) return;
    if (!XClassIsVtableNull(self))
        XImage_deinit_base(self);
    XImage_init(self);
    XImage_copy_base(self, other);
}

void XImage_move(XImage* self, XImage* other)
{
    if (ISNULL(self, "XImage") || ISNULL(other, "XImage")) return;
    if (!XClassIsVtableNull(self))
        XImage_deinit_base(self);
    XImage_init(self);
    XImage_move_base(self, other);
}

void XImage_deinit(XImage* self)
{
    XImage_deinit_base(self);
}

void XImage_copy_base(XImage* dest, const XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    VXImage_copy(dest, src);
}

void XImage_move_base(XImage* dest, XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    VXImage_move(dest, src);
}

void XImage_deinit_base(XImage* self)
{
    if (ISNULL(self, "XImage")) return;
    VXImage_deinit(self);
}



void XImage_delete_base(XImage* self)
{
    XClass_delete_base((XClass*)self);
}
/* ========== 查询方法 ========== */

bool XImage_isNull(const XImage* self)
{
    return !self || !self->m_data || !self->m_data->m_data;
}

int XImage_width(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_width : 0;
}

int XImage_height(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_height : 0;
}

void XImage_size(const XImage* self, XSize* out)
{
    if (out)
    {
        out->width = XImage_width(self);
        out->height = XImage_height(self);
    }
}

void XImage_rect(const XImage* self, XRect* out)
{
    if (out)
    {
        out->x = 0;
        out->y = 0;
        out->width = XImage_width(self);
        out->height = XImage_height(self);
    }
}

int XImage_depth(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_depth : 0;
}

XImageFormat XImage_format(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_format : XImageFormat_Invalid;
}

int XImage_colorCount(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_colorCount : 0;
}

void XImage_setColorCount(XImage* self, int count)
{
    if (!self || !self->m_data) return;
    XImage_detach(self);
    if (self->m_data->m_colorTable)
        XFree_System(self->m_data->m_colorTable);
    self->m_data->m_colorCount = count;
    if (count > 0)
        self->m_data->m_colorTable = (uint32_t*)XMalloc_System(count * sizeof(uint32_t));
    else
        self->m_data->m_colorTable = NULL;
}

uint32_t XImage_color(const XImage* self, int index)
{
    if (!self || !self->m_data || !self->m_data->m_colorTable)
        return 0;
    if (index < 0 || index >= self->m_data->m_colorCount)
        return 0;
    return self->m_data->m_colorTable[index];
}

void XImage_setColor(XImage* self, int index, uint32_t color)
{
    if (!self || !self->m_data || !self->m_data->m_colorTable) return;
    if (index < 0 || index >= self->m_data->m_colorCount) return;
    XImage_detach(self);
    self->m_data->m_colorTable[index] = color;
}

void XImage_fill(XImage* self, uint32_t color)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    XImage_detach(self);
    int totalBytes = self->m_data->m_bytesPerLine * self->m_data->m_height;
    // 根据格式填充
    switch (self->m_data->m_format)
    {
        case XImageFormat_ARGB32:
        case XImageFormat_RGB32:
        {
            uint32_t* p = (uint32_t*)self->m_data->m_data;
            int count = totalBytes / 4;
            for (int i = 0; i < count; i++)
                p[i] = color;
            break;
        }
        case XImageFormat_RGB888:
        case XImageFormat_BGR888:
        {
            // 简单填充
            memset(self->m_data->m_data, color & 0xFF, totalBytes);
            break;
        }
        default:
            memset(self->m_data->m_data, 0, totalBytes);
            break;
    }
}

bool XImage_hasAlphaChannel(const XImage* self)
{
    return (self && self->m_data) ? XImageFormat_hasAlpha(self->m_data->m_format) : false;
}

bool XImage_hasAlpha(const XImage* self)
{
    if (!self || !self->m_data) return false;
    if (!XImageFormat_hasAlpha(self->m_data->m_format))
        return false;
    // 检查实际像素是否使用了 Alpha
    if (self->m_data->m_format == XImageFormat_ARGB32)
    {
        uint32_t* p = (uint32_t*)self->m_data->m_data;
        int count = (self->m_data->m_bytesPerLine * self->m_data->m_height) / 4;
        for (int i = 0; i < count; i++)
        {
            if ((p[i] >> 24) != 0xFF)
                return true;
        }
    }
    return false;
}

/* ========== 像素数据访问 ========== */

uint8_t* XImage_bits(XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_data : NULL;
}

const uint8_t* XImage_constBits(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_data : NULL;
}

uint8_t* XImage_scanLine(XImage* self, int scanLine)
{
    if (!self || !self->m_data || !self->m_data->m_data) return NULL;
    if (scanLine < 0 || scanLine >= self->m_data->m_height) return NULL;
    return self->m_data->m_data + scanLine * self->m_data->m_bytesPerLine;
}

const uint8_t* XImage_constScanLine(const XImage* self, int scanLine)
{
    if (!self || !self->m_data || !self->m_data->m_data) return NULL;
    if (scanLine < 0 || scanLine >= self->m_data->m_height) return NULL;
    return self->m_data->m_data + scanLine * self->m_data->m_bytesPerLine;
}

int XImage_bytesPerLine(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_bytesPerLine : 0;
}

int XImage_sizeInBytes(const XImage* self)
{
    return (self && self->m_data) ? (self->m_data->m_bytesPerLine * self->m_data->m_height) : 0;
}

int XImage_pixelIndex(const XImage* self, int x, int y)
{
    if (!self || !self->m_data || !self->m_data->m_data) return -1;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return -1;
    const uint8_t* line = XImage_constScanLine(self, y);
    if (!line) return -1;
    if (self->m_data->m_format == XImageFormat_Indexed8)
        return line[x];
    return 0;
}

uint32_t XImage_pixel(const XImage* self, int x, int y)
{
    if (!self || !self->m_data || !self->m_data->m_data) return 0;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return 0;
    const uint8_t* line = XImage_constScanLine(self, y);
    if (!line) return 0;
    switch (self->m_data->m_format)
    {
        case XImageFormat_ARGB32:
        case XImageFormat_RGB32:
            return ((const uint32_t*)line)[x];
        case XImageFormat_RGB888:
            return 0xFF000000 | ((uint32_t)line[x * 3]) << 16 |
                   ((uint32_t)line[x * 3 + 1]) << 8 | line[x * 3 + 2];
        default:
            return 0;
    }
}

void XImage_setPixel(XImage* self, int x, int y, uint32_t indexOrRgb)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return;
    XImage_detach(self);
    uint8_t* line = XImage_scanLine(self, y);
    if (!line) return;
    switch (self->m_data->m_format)
    {
        case XImageFormat_ARGB32:
        case XImageFormat_RGB32:
            ((uint32_t*)line)[x] = indexOrRgb;
            break;
        case XImageFormat_RGB888:
            line[x * 3]     = (uint8_t)((indexOrRgb >> 16) & 0xFF);
            line[x * 3 + 1] = (uint8_t)((indexOrRgb >> 8) & 0xFF);
            line[x * 3 + 2] = (uint8_t)(indexOrRgb & 0xFF);
            break;
        default:
            break;
    }
}

bool XImage_valid(const XImage* self, int x, int y)
{
    if (!self || !self->m_data) return false;
    return x >= 0 && x < self->m_data->m_width && y >= 0 && y < self->m_data->m_height;
}

/* ========== 图像复制与转换 ========== */

void XImage_copyRect(const XImage* self, const XRect* rect, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    XImage_init(out);
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    int rx = 0, ry = 0, rw = w, rh = h;
    if (rect)
    {
        rx = rect->x;
        ry = rect->y;
        rw = rect->width;
        rh = rect->height;
    }
    // 裁剪到图像边界
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > w) rw = w - rx;
    if (ry + rh > h) rh = h - ry;
    if (rw <= 0 || rh <= 0) return;
    XImage_init_ex(out, rw, rh, self->m_data->m_format);
    // 逐行复制
    for (int y = 0; y < rh; y++)
    {
        const uint8_t* src = XImage_constScanLine(self, ry + y) + rx * (self->m_data->m_depth / 8);
        uint8_t* dst = XImage_scanLine(out, y);
        if (src && dst)
            memcpy(dst, src, out->m_data->m_bytesPerLine < self->m_data->m_bytesPerLine ?
                   out->m_data->m_bytesPerLine : self->m_data->m_bytesPerLine);
    }
}

void XImage_convertToFormat(const XImage* self, XImageFormat format, uint32_t flags, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    if (self->m_data->m_format == format)
    {
        XImage_copyRect(self, NULL, out);
        return;
    }
    // 简单转换：对于 RGB32/ARGB32 互转
    if ((self->m_data->m_format == XImageFormat_RGB32 || self->m_data->m_format == XImageFormat_ARGB32) &&
        (format == XImageFormat_RGB32 || format == XImageFormat_ARGB32))
    {
        XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, format);
        memcpy(out->m_data->m_data, self->m_data->m_data, XImage_sizeInBytes(self));
        return;
    }
    // 默认：直接复制数据
    XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, format);
}

void XImage_convertToFormat_ex(const XImage* self, XImageFormat format,
                               const uint32_t* colorTable, int colorCount,
                               uint32_t flags, XImage* out)
{
    XImage_convertToFormat(self, format, flags, out);
}

bool XImage_convertToFormatInPlace(XImage* self, XImageFormat format, uint32_t flags)
{
    if (!self || !self->m_data) return false;
    if (self->m_data->m_format == format) return true;
    XImage temp;
    XImage_init(&temp);
    XImage_convertToFormat(self, format, flags, &temp);
    XImage_deinit_base(self);
    self->m_data = temp.m_data;
    temp.m_data = NULL;
    return true;
}

bool XImage_reinterpretAsFormat(XImage* self, XImageFormat format)
{
    if (!self || !self->m_data) return false;
    // 检查格式兼容性（相同位深度）
    if (XImageFormat_bitDepth(self->m_data->m_format) != XImageFormat_bitDepth(format))
        return false;
    XImage_detach(self);
    self->m_data->m_format = format;
    self->m_data->m_depth = XImageFormat_bitDepth(format);
    return true;
}

void XImage_mirrored(const XImage* self, bool horizontal, bool vertical, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, self->m_data->m_format);
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    int bpp = self->m_data->m_depth / 8;
    if (bpp < 1) bpp = 1;
    for (int y = 0; y < h; y++)
    {
        int srcY = vertical ? (h - 1 - y) : y;
        const uint8_t* src = XImage_constScanLine(self, srcY);
        uint8_t* dst = XImage_scanLine(out, y);
        if (!src || !dst) continue;
        if (horizontal)
        {
            for (int x = 0; x < w; x++)
                memcpy(dst + x * bpp, src + (w - 1 - x) * bpp, bpp);
        }
        else
        {
            memcpy(dst, src, self->m_data->m_bytesPerLine);
        }
    }
}

void XImage_mirroredInPlace(XImage* self, bool horizontal, bool vertical)
{
    if (!self || !self->m_data) return;
    XImage temp;
    XImage_init(&temp);
    XImage_mirrored(self, horizontal, vertical, &temp);
    XImage_deinit_base(self);
    self->m_data = temp.m_data;
    temp.m_data = NULL;
}

void XImage_rgbSwapped(const XImage* self, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, self->m_data->m_format);
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    for (int y = 0; y < h; y++)
    {
        const uint8_t* src = XImage_constScanLine(self, y);
        uint8_t* dst = XImage_scanLine(out, y);
        if (!src || !dst) continue;
        for (int x = 0; x < w; x++)
        {
            uint32_t pixel = ((const uint32_t*)src)[x];
            ((uint32_t*)dst)[x] = (pixel & 0xFF00FF00) | ((pixel & 0x00FF0000) >> 16) | ((pixel & 0x000000FF) << 16);
        }
    }
}

void XImage_rgbSwappedInPlace(XImage* self)
{
    if (!self || !self->m_data) return;
    XImage temp;
    XImage_init(&temp);
    XImage_rgbSwapped(self, &temp);
    XImage_deinit_base(self);
    self->m_data = temp.m_data;
    temp.m_data = NULL;
}

void XImage_scaled(const XImage* self, int width, int height, uint32_t aspectMode, uint32_t mode, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    if (width <= 0 || height <= 0) return;
    XImage_init_ex(out, width, height, self->m_data->m_format);
    // 简单最近邻缩放
    int sw = self->m_data->m_width;
    int sh = self->m_data->m_height;
    int bpp = self->m_data->m_depth / 8;
    if (bpp < 1) bpp = 1;
    for (int y = 0; y < height; y++)
    {
        int srcY = y * sh / height;
        const uint8_t* src = XImage_constScanLine(self, srcY);
        uint8_t* dst = XImage_scanLine(out, y);
        if (!src || !dst) continue;
        for (int x = 0; x < width; x++)
        {
            int srcX = x * sw / width;
            memcpy(dst + x * bpp, src + srcX * bpp, bpp);
        }
    }
}

void XImage_scaledToWidth(const XImage* self, int width, uint32_t mode, XImage* out)
{
    if (!self || !self->m_data) return;
    int height = self->m_data->m_height * width / self->m_data->m_width;
    XImage_scaled(self, width, height, 0, mode, out);
}

void XImage_scaledToHeight(const XImage* self, int height, uint32_t mode, XImage* out)
{
    if (!self || !self->m_data) return;
    int width = self->m_data->m_width * height / self->m_data->m_height;
    XImage_scaled(self, width, height, 0, mode, out);
}

/* ========== 文件操作 ========== */

bool XImage_load(XImage* self, const char* fileName, const char* format)
{
    // 暂未实现实际文件解码，预留接口
    (void)self;
    (void)fileName;
    (void)format;
    return false;
}

bool XImage_loadFromData(XImage* self, const uint8_t* data, int len, const char* format)
{
    (void)self;
    (void)data;
    (void)len;
    (void)format;
    return false;
}

bool XImage_save(const XImage* self, const char* fileName, const char* format, int quality)
{
    (void)self;
    (void)fileName;
    (void)format;
    (void)quality;
    return false;
}

/* ========== 辅助数据 ========== */

int XImage_dotsPerMeterX(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_dpmX : 0;
}

void XImage_setDotsPerMeterX(XImage* self, int val)
{
    if (!self || !self->m_data) return;
    XImage_detach(self);
    self->m_data->m_dpmX = val;
}

int XImage_dotsPerMeterY(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_dpmY : 0;
}

void XImage_setDotsPerMeterY(XImage* self, int val)
{
    if (!self || !self->m_data) return;
    XImage_detach(self);
    self->m_data->m_dpmY = val;
}

void XImage_offset(const XImage* self, XPoint* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_offsetX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_offsetY : 0;
    }
}

void XImage_setOffset(XImage* self, const XPoint* pos)
{
    if (!self || !self->m_data || !pos) return;
    XImage_detach(self);
    self->m_data->m_offsetX = pos->x;
    self->m_data->m_offsetY = pos->y;
}

int64_t XImage_cacheKey(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_cacheKey : 0;
}

void XImage_detach(XImage* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XImageData* newData = XImageData_create(self->m_data->m_width, self->m_data->m_height,
                                                 self->m_data->m_format, self->m_data->m_bytesPerLine,
                                                 NULL, NULL, NULL);
        if (newData)
        {
            // 复制像素数据
            memcpy(newData->m_data, self->m_data->m_data,
                   self->m_data->m_bytesPerLine * self->m_data->m_height);
            // 复制元数据
            newData->m_dpmX = self->m_data->m_dpmX;
            newData->m_dpmY = self->m_data->m_dpmY;
            newData->m_offsetX = self->m_data->m_offsetX;
            newData->m_offsetY = self->m_data->m_offsetY;
            XImageData_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

bool XImage_isDetached(const XImage* self)
{
    return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}

/* ========== 静态工具方法 ========== */

void XImage_fromData(const uint8_t* data, int size, const char* format, XImage* out)
{
    XImage_init(out);
    XImage_loadFromData(out, data, size, format);
}

const char* XImage_formatToStr(XImageFormat format)
{
    switch (format)
    {
        case XImageFormat_Invalid: return "Invalid";
        case XImageFormat_Mono: return "Mono";
        case XImageFormat_MonoLSB: return "MonoLSB";
        case XImageFormat_Indexed8: return "Indexed8";
        case XImageFormat_RGB32: return "RGB32";
        case XImageFormat_ARGB32: return "ARGB32";
        case XImageFormat_ARGB32_Premultiplied: return "ARGB32_Premultiplied";
        case XImageFormat_RGB888: return "RGB888";
        case XImageFormat_RGBA8888: return "RGBA8888";
        default: return "Unknown";
    }
}




/* ========== 像素查询方法 ========== */

bool XImage_allGray(const XImage* self)
{
    if (!self || !self->m_data || !self->m_data->m_data)
        return false;
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    if (w <= 0 || h <= 0)
        return false;
    switch (self->m_data->m_format)
    {
        case XImageFormat_ARGB32:
        case XImageFormat_RGB32:
        {
            const uint32_t* p = (const uint32_t*)self->m_data->m_data;
            int count = (self->m_data->m_bytesPerLine * h) / 4;
            for (int i = 0; i < count; i++)
            {
                uint32_t c = p[i];
                uint8_t r = (uint8_t)((c >> 16) & 0xFF);
                uint8_t g = (uint8_t)((c >> 8) & 0xFF);
                uint8_t b = (uint8_t)(c & 0xFF);
                if (r != g || g != b)
                    return false;
            }
            return true;
        }
        case XImageFormat_RGB888:
        {
            for (int y = 0; y < h; y++)
            {
                const uint8_t* line = XImage_constScanLine(self, y);
                if (!line) continue;
                for (int x = 0; x < w; x++)
                {
                    if (line[x * 3] != line[x * 3 + 1] ||
                        line[x * 3 + 1] != line[x * 3 + 2])
                        return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

void XImage_fillRect(XImage* self, const XRect* rect, uint32_t color)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    int rx = 0, ry = 0, rw = w, rh = h;
    if (rect)
    {
        rx = rect->x;
        ry = rect->y;
        rw = rect->width;
        rh = rect->height;
    }
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > w) rw = w - rx;
    if (ry + rh > h) rh = h - ry;
    if (rw <= 0 || rh <= 0) return;
    XImage_detach(self);
    for (int y = ry; y < ry + rh; y++)
    {
        uint8_t* line = XImage_scanLine(self, y);
        if (!line) continue;
        switch (self->m_data->m_format)
        {
            case XImageFormat_ARGB32:
            case XImageFormat_RGB32:
            {
                uint32_t* p = (uint32_t*)line + rx;
                for (int x = 0; x < rw; x++)
                    p[x] = color;
                break;
            }
            case XImageFormat_RGB888:
            {
                for (int x = rx; x < rx + rw; x++)
                {
                    line[x * 3]     = (uint8_t)((color >> 16) & 0xFF);
                    line[x * 3 + 1] = (uint8_t)((color >> 8) & 0xFF);
                    line[x * 3 + 2] = (uint8_t)(color & 0xFF);
                }
                break;
            }
            default:
                memset(line + rx, 0, rw);
                break;
        }
    }
}

void XImage_clear(XImage* self, const XRect* rect, uint32_t color)
{
    XImage_fillRect(self, rect, color);
}
