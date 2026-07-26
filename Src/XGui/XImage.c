/******************************************************************************
 * @file       XImage.c
 * @brief      XImage 图像类实现（对标 Qt 6.8 QImage）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XImageFormat.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>

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

static uint32_t g_imageSerialCounter = 0;  /**< 全局序列号计数器 */

static int64_t XImageData_nextCacheKey(void)
{
    uint32_t serial = ++g_imageSerialCounter;
    if (serial == 0)
        serial = ++g_imageSerialCounter;
    return (int64_t)((uint64_t)serial << 32);
}

static void XImageData_markDirty(XImageData* d)
{
    if (d)
        d->m_cacheKey = XImageData_nextCacheKey();
}

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
    if (width <= 0 || height <= 0 || format <= XImageFormat_Invalid ||
        format >= XImageFormat_NImageFormats)
        return NULL;

    const int depth = XImageFormat_bitDepth(format);
    const int defaultBytesPerLine = XImageFormat_bytesPerLine(width, format);
    const int64_t minimumBytesPerLine64 = ((int64_t)width * depth + 7) / 8;
    if (depth <= 0 || defaultBytesPerLine <= 0 || minimumBytesPerLine64 > INT_MAX ||
        bytesPerLine < 0 || bytesPerLine > INT_MAX)
        return NULL;

    const int actualBytesPerLine = bytesPerLine <= 0 ? defaultBytesPerLine : (int)bytesPerLine;
    if (actualBytesPerLine < (int)minimumBytesPerLine64 ||
        (size_t)actualBytesPerLine > SIZE_MAX / (size_t)height ||
        (size_t)actualBytesPerLine * (size_t)height > INT_MAX)
        return NULL;

    XImageData* d = (XImageData*)XMalloc_System(sizeof(XImageData));
    if (!d) return NULL;
    memset(d, 0, sizeof(XImageData));

    XAtomic_init(d->m_refCount, 1);
    d->m_width = width;
    d->m_height = height;
    d->m_format = format;
    d->m_depth = depth;
    d->m_cleanupFunc = cleanupFunc;
    d->m_cleanupInfo = cleanupInfo;

    // 计算每行字节数
    d->m_bytesPerLine = actualBytesPerLine;

    // 分配或引用像素数据
    if (data)
    {
        d->m_data = data;
        d->m_ownsData = false;
    }
    else
    {
        const size_t totalSize = (size_t)d->m_bytesPerLine * (size_t)height;
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
    d->m_serialNumber = (int)(g_imageSerialCounter + 1);
    d->m_cacheKey = XImageData_nextCacheKey();

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

static XImageData* XImageData_clone(const XImageData* source)
{
    if (!source || !source->m_data)
        return NULL;
    XImageData* copy = XImageData_create(source->m_width, source->m_height,
                                         source->m_format, source->m_bytesPerLine,
                                         NULL, NULL, NULL);
    if (!copy)
        return NULL;
    memcpy(copy->m_data, source->m_data,
           (size_t)source->m_bytesPerLine * (size_t)source->m_height);
    copy->m_dpmX = source->m_dpmX;
    copy->m_dpmY = source->m_dpmY;
    copy->m_offsetX = source->m_offsetX;
    copy->m_offsetY = source->m_offsetY;
    if (source->m_colorCount > 0 && source->m_colorTable)
    {
        if ((size_t)source->m_colorCount > SIZE_MAX / sizeof(uint32_t))
        {
            XImageData_unref(copy);
            return NULL;
        }
        copy->m_colorTable = (uint32_t*)XMalloc_System((size_t)source->m_colorCount * sizeof(uint32_t));
        if (!copy->m_colorTable)
        {
            XImageData_unref(copy);
            return NULL;
        }
        memcpy(copy->m_colorTable, source->m_colorTable,
               (size_t)source->m_colorCount * sizeof(uint32_t));
        copy->m_colorCount = source->m_colorCount;
    }
    return copy;
}

/* ========== XImage 类实现 ========== */

/**
 * @brief      虚函数：拷贝
 */
static void VXImage_copy(XImage* dest, const XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    if (dest == src) return;
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
    if (dest == src) return;
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
    if (self == other) return;
    if (!XClassIsVtableNull(self))
        XImage_deinit_base(self);
    XImage_init(self);
    XImage_copy_base(self, other);
}

void XImage_move(XImage* self, XImage* other)
{
    if (ISNULL(self, "XImage") || ISNULL(other, "XImage")) return;
    if (self == other) return;
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
    if (!self || !self->m_data || count < 0 ||
        (size_t)count > SIZE_MAX / sizeof(uint32_t)) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    uint32_t* replacement = NULL;
    if (count > 0)
    {
        replacement = (uint32_t*)XMalloc_System((size_t)count * sizeof(uint32_t));
        if (!replacement) return;
        memset(replacement, 0, (size_t)count * sizeof(uint32_t));
        int retained = count < self->m_data->m_colorCount ? count : self->m_data->m_colorCount;
        if (retained > 0 && self->m_data->m_colorTable)
            memcpy(replacement, self->m_data->m_colorTable, (size_t)retained * sizeof(uint32_t));
    }
    XFree_System(self->m_data->m_colorTable);
    self->m_data->m_colorTable = replacement;
    self->m_data->m_colorCount = count;
    XImageData_markDirty(self->m_data);
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
    if (!XImage_isDetached(self)) return;
    self->m_data->m_colorTable[index] = color;
    XImageData_markDirty(self->m_data);
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
    if (!self || !self->m_data) return NULL;
    XImage_detach(self);
    return XImage_isDetached(self) ? self->m_data->m_data : NULL;
}

const uint8_t* XImage_constBits(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_data : NULL;
}

uint8_t* XImage_scanLine(XImage* self, int scanLine)
{
    if (!self || !self->m_data || !self->m_data->m_data) return NULL;
    if (scanLine < 0 || scanLine >= self->m_data->m_height) return NULL;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return NULL;
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
    return (self && self->m_data) ? self->m_data->m_bytesPerLine * self->m_data->m_height : 0;
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

static bool XImageFormat_isBasicConvertible(XImageFormat format)
{
    switch (format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        case XImageFormat_RGB32:
        case XImageFormat_ARGB32:
        case XImageFormat_ARGB32_Premultiplied:
        case XImageFormat_RGB888:
        case XImageFormat_BGR888:
        case XImageFormat_Grayscale8:
            return true;
        default:
            return false;
    }
}

static uint32_t XImage_readBasicPixel(const XImageData* d, int x, int y)
{
    const uint8_t* line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    switch (d->m_format)
    {
        case XImageFormat_RGB32:
            return ((const uint32_t*)line)[x] | 0xff000000u;
        case XImageFormat_ARGB32:
            return ((const uint32_t*)line)[x];
        case XImageFormat_ARGB32_Premultiplied:
        {
            uint32_t c = ((const uint32_t*)line)[x];
            unsigned a = c >> 24;
            if (a == 0 || a == 255) return c;
            unsigned r = (((c >> 16) & 255u) * 255u + a / 2u) / a;
            unsigned g = (((c >> 8) & 255u) * 255u + a / 2u) / a;
            unsigned b = ((c & 255u) * 255u + a / 2u) / a;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }
        case XImageFormat_RGB888:
            return 0xff000000u | ((uint32_t)line[x * 3] << 16) |
                   ((uint32_t)line[x * 3 + 1] << 8) | line[x * 3 + 2];
        case XImageFormat_BGR888:
            return 0xff000000u | ((uint32_t)line[x * 3 + 2] << 16) |
                   ((uint32_t)line[x * 3 + 1] << 8) | line[x * 3];
        case XImageFormat_Grayscale8:
        {
            uint32_t gray = line[x];
            return 0xff000000u | (gray << 16) | (gray << 8) | gray;
        }
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        {
            unsigned shift = d->m_format == XImageFormat_Mono ? 7u - ((unsigned)x & 7u) : ((unsigned)x & 7u);
            unsigned index = (line[x >> 3] >> shift) & 1u;
            if (d->m_colorTable && (int)index < d->m_colorCount)
                return d->m_colorTable[index];
            return index ? 0xffffffffu : 0xff000000u;
        }
        default:
            return 0;
    }
}

static void XImage_writeBasicPixel(XImageData* d, int x, int y, uint32_t c)
{
    uint8_t* line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    const unsigned a = c >> 24;
    const unsigned r = (c >> 16) & 255u;
    const unsigned g = (c >> 8) & 255u;
    const unsigned b = c & 255u;
    switch (d->m_format)
    {
        case XImageFormat_RGB32:
            ((uint32_t*)line)[x] = c | 0xff000000u;
            break;
        case XImageFormat_ARGB32:
            ((uint32_t*)line)[x] = c;
            break;
        case XImageFormat_ARGB32_Premultiplied:
            ((uint32_t*)line)[x] = (a << 24) |
                ((((r * a + 127u) / 255u) & 255u) << 16) |
                ((((g * a + 127u) / 255u) & 255u) << 8) |
                (((b * a + 127u) / 255u) & 255u);
            break;
        case XImageFormat_RGB888:
            line[x * 3] = (uint8_t)r; line[x * 3 + 1] = (uint8_t)g; line[x * 3 + 2] = (uint8_t)b;
            break;
        case XImageFormat_BGR888:
            line[x * 3] = (uint8_t)b; line[x * 3 + 1] = (uint8_t)g; line[x * 3 + 2] = (uint8_t)r;
            break;
        case XImageFormat_Grayscale8:
            line[x] = (uint8_t)((11u * r + 16u * g + 5u * b) / 32u);
            break;
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        {
            unsigned shift = d->m_format == XImageFormat_Mono ? 7u - ((unsigned)x & 7u) : ((unsigned)x & 7u);
            uint8_t mask = (uint8_t)(1u << shift);
            if ((11u * r + 16u * g + 5u * b) / 32u >= 128u)
                line[x >> 3] |= mask;
            else
                line[x >> 3] &= (uint8_t)~mask;
            break;
        }
        default:
            break;
    }
}

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
    (void)flags;
    if (!out) return;
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data ||
        format <= XImageFormat_Invalid || format >= XImageFormat_NImageFormats) return;
    if (self->m_data->m_format == format)
    {
        out->m_data = XImageData_clone(self->m_data);
        return;
    }
    if (!XImageFormat_isBasicConvertible(self->m_data->m_format) ||
        !XImageFormat_isBasicConvertible(format))
        return;

    out->m_data = XImageData_create(self->m_data->m_width, self->m_data->m_height,
                                     format, 0, NULL, NULL, NULL);
    if (!out->m_data)
        return;
    out->m_data->m_dpmX = self->m_data->m_dpmX;
    out->m_data->m_dpmY = self->m_data->m_dpmY;
    out->m_data->m_offsetX = self->m_data->m_offsetX;
    out->m_data->m_offsetY = self->m_data->m_offsetY;
    if (format == XImageFormat_Mono || format == XImageFormat_MonoLSB)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System(2 * sizeof(uint32_t));
        if (!out->m_data->m_colorTable)
        {
            XImageData_unref(out->m_data);
            out->m_data = NULL;
            return;
        }
        out->m_data->m_colorTable[0] = 0xff000000u;
        out->m_data->m_colorTable[1] = 0xffffffffu;
        out->m_data->m_colorCount = 2;
    }
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
            XImage_writeBasicPixel(out->m_data, x, y, XImage_readBasicPixel(self->m_data, x, y));
}

void XImage_convertToFormat_ex(const XImage* self, XImageFormat format,
                               const uint32_t* colorTable, int colorCount,
                               uint32_t flags, XImage* out)
{
    XImage_convertToFormat(self, format, flags, out);
    if (!out || !out->m_data || !colorTable || colorCount <= 0 ||
        (format != XImageFormat_Mono && format != XImageFormat_MonoLSB))
        return;
    XImage_setColorCount(out, colorCount);
    if (!out->m_data->m_colorTable || out->m_data->m_colorCount != colorCount)
        return;
    memcpy(out->m_data->m_colorTable, colorTable, (size_t)colorCount * sizeof(uint32_t));
    XImageData_markDirty(out->m_data);
}

bool XImage_convertToFormatInPlace(XImage* self, XImageFormat format, uint32_t flags)
{
    if (!self || !self->m_data) return false;
    if (self->m_data->m_format == format) return true;
    XImage temp;
    XImage_init(&temp);
    XImage_convertToFormat(self, format, flags, &temp);
    if (XImage_isNull(&temp))
    {
        XImage_deinit_base(&temp);
        return false;
    }
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
    if (!out) return;
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data) return;
    out->m_data = XImageData_clone(self->m_data);
    if (!out->m_data) return;
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    switch (self->m_data->m_format)
    {
        case XImageFormat_RGB32:
        case XImageFormat_ARGB32:
        case XImageFormat_ARGB32_Premultiplied:
        case XImageFormat_RGBX8888:
        case XImageFormat_RGBA8888:
        case XImageFormat_RGBA8888_Premultiplied:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint32_t* p = (uint32_t*)(out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine) + x;
                    uint32_t c = *p;
                    *p = (c & 0xff00ff00u) | ((c & 0x00ff0000u) >> 16) | ((c & 0x000000ffu) << 16);
                }
            break;
        case XImageFormat_RGB888:
        case XImageFormat_BGR888:
        case XImageFormat_RGB666:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint8_t* p = out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine + x * 3;
                    uint8_t t = p[0]; p[0] = p[2]; p[2] = t;
                }
            break;
        case XImageFormat_RGB16:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    uint16_t* p = (uint16_t*)(out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine) + x;
                    uint16_t c = *p;
                    *p = (uint16_t)(((c << 11) & 0xf800u) | ((c >> 11) & 0x001fu) | (c & 0x07e0u));
                }
            break;
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        case XImageFormat_Indexed8:
            for (int i = 0; i < out->m_data->m_colorCount; ++i)
            {
                uint32_t c = out->m_data->m_colorTable[i];
                out->m_data->m_colorTable[i] = (c & 0xff00ff00u) |
                    ((c & 0x00ff0000u) >> 16) | ((c & 0x000000ffu) << 16);
            }
            break;
        default:
            break;
    }
    XImageData_markDirty(out->m_data);
}

void XImage_rgbSwappedInPlace(XImage* self)
{
    if (!self || !self->m_data) return;
    XImage temp;
    XImage_init(&temp);
    XImage_rgbSwapped(self, &temp);
    if (XImage_isNull(&temp))
    {
        XImage_deinit_base(&temp);
        return;
    }
    XImage_deinit_base(self);
    self->m_data = temp.m_data;
    temp.m_data = NULL;
}

void XImage_scaled(const XImage* self, int width, int height, uint32_t aspectMode, uint32_t mode, XImage* out)
{
    (void)mode;
    if (!out) return;
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data || width <= 0 || height <= 0) return;
    int sw = self->m_data->m_width;
    int sh = self->m_data->m_height;
    int targetWidth = width;
    int targetHeight = height;
    if (aspectMode == 1 || aspectMode == 2)
    {
        int64_t widthProduct = (int64_t)width * sh;
        int64_t heightProduct = (int64_t)height * sw;
        bool useWidth = aspectMode == 1 ? widthProduct <= heightProduct : widthProduct >= heightProduct;
        if (useWidth)
            targetHeight = (int)(((int64_t)sh * width + sw / 2) / sw);
        else
            targetWidth = (int)(((int64_t)sw * height + sh / 2) / sh);
        if (targetWidth < 1) targetWidth = 1;
        if (targetHeight < 1) targetHeight = 1;
    }
    out->m_data = XImageData_create(targetWidth, targetHeight, self->m_data->m_format,
                                     0, NULL, NULL, NULL);
    if (!out->m_data) return;
    out->m_data->m_dpmX = self->m_data->m_dpmX;
    out->m_data->m_dpmY = self->m_data->m_dpmY;
    out->m_data->m_offsetX = self->m_data->m_offsetX;
    out->m_data->m_offsetY = self->m_data->m_offsetY;
    if (self->m_data->m_colorCount > 0 && self->m_data->m_colorTable)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System((size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        if (!out->m_data->m_colorTable)
        {
            XImageData_unref(out->m_data); out->m_data = NULL; return;
        }
        memcpy(out->m_data->m_colorTable, self->m_data->m_colorTable,
               (size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        out->m_data->m_colorCount = self->m_data->m_colorCount;
    }
    if (XImageFormat_isBasicConvertible(self->m_data->m_format))
    {
        for (int y = 0; y < targetHeight; ++y)
            for (int x = 0; x < targetWidth; ++x)
                XImage_writeBasicPixel(out->m_data, x, y,
                    XImage_readBasicPixel(self->m_data, (int)((int64_t)x * sw / targetWidth),
                                          (int)((int64_t)y * sh / targetHeight)));
        return;
    }
    int bpp = self->m_data->m_depth / 8;
    if (bpp < 1 || self->m_data->m_depth % 8 != 0)
    {
        XImageData_unref(out->m_data); out->m_data = NULL; return;
    }
    for (int y = 0; y < targetHeight; y++)
    {
        const uint8_t* src = self->m_data->m_data + (size_t)((int64_t)y * sh / targetHeight) * self->m_data->m_bytesPerLine;
        uint8_t* dst = out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine;
        for (int x = 0; x < targetWidth; x++)
        {
            int srcX = (int)((int64_t)x * sw / targetWidth);
            memcpy(dst + x * bpp, src + srcX * bpp, bpp);
        }
    }
}

void XImage_scaledToWidth(const XImage* self, int width, uint32_t mode, XImage* out)
{
    if (!out) return;
    if (!self || !self->m_data || width <= 0) { XImage_init(out); return; }
    int64_t scaledHeight = ((int64_t)self->m_data->m_height * width + self->m_data->m_width / 2) / self->m_data->m_width;
    if (scaledHeight < 1) scaledHeight = 1;
    if (scaledHeight > INT_MAX) { XImage_init(out); return; }
    int height = (int)scaledHeight;
    XImage_scaled(self, width, height, 0, mode, out);
}

void XImage_scaledToHeight(const XImage* self, int height, uint32_t mode, XImage* out)
{
    if (!out) return;
    if (!self || !self->m_data || height <= 0) { XImage_init(out); return; }
    int64_t scaledWidth = ((int64_t)self->m_data->m_width * height + self->m_data->m_height / 2) / self->m_data->m_height;
    if (scaledWidth < 1) scaledWidth = 1;
    if (scaledWidth > INT_MAX) { XImage_init(out); return; }
    int width = (int)scaledWidth;
    XImage_scaled(self, width, height, 0, mode, out);
}

/* ========== 文件操作 ========== */

static uint16_t XImage_readLe16(const uint8_t* p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t XImage_readLe32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void XImage_writeLe16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}

static void XImage_writeLe32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static bool XImage_isBmpFormat(const char* format)
{
    return !format ||
        (tolower((unsigned char)format[0]) == 'b' &&
         tolower((unsigned char)format[1]) == 'm' &&
         tolower((unsigned char)format[2]) == 'p' && format[3] == '\0');
}

bool XImage_load(XImage* self, const char* fileName, const char* format)
{
    if (!self || !fileName || !XImage_isBmpFormat(format)) return false;
    FILE* file = fopen(fileName, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length <= 0 || length > INT_MAX || fseek(file, 0, SEEK_SET) != 0)
    { fclose(file); return false; }
    uint8_t* bytes = (uint8_t*)XMalloc_System((size_t)length);
    if (!bytes) { fclose(file); return false; }
    bool readOk = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    bool result = readOk && XImage_loadFromData(self, bytes, (int)length, format);
    XFree_System(bytes);
    return result;
}

bool XImage_loadFromData(XImage* self, const uint8_t* data, int len, const char* format)
{
    if (!self || !data || len < 54 || !XImage_isBmpFormat(format) ||
        data[0] != 'B' || data[1] != 'M') return false;
    uint32_t pixelOffset = XImage_readLe32(data + 10);
    uint32_t dibSize = XImage_readLe32(data + 14);
    int32_t width = (int32_t)XImage_readLe32(data + 18);
    int32_t signedHeight = (int32_t)XImage_readLe32(data + 22);
    uint16_t planes = XImage_readLe16(data + 26);
    uint16_t bitsPerPixel = XImage_readLe16(data + 28);
    uint32_t compression = XImage_readLe32(data + 30);
    if (dibSize < 40 || (uint64_t)14 + dibSize > (uint32_t)len || width <= 0 ||
        signedHeight == 0 || signedHeight == INT32_MIN || planes != 1 ||
        (bitsPerPixel != 24 && bitsPerPixel != 32) || compression != 0)
        return false;
    int height = signedHeight < 0 ? -signedHeight : signedHeight;
    uint64_t rowBytes = ((uint64_t)(uint32_t)width * bitsPerPixel + 31u) / 32u * 4u;
    uint64_t imageBytes = rowBytes * (uint32_t)height;
    if (rowBytes > INT_MAX || pixelOffset < 14u + dibSize ||
        (uint64_t)pixelOffset + imageBytes > (uint32_t)len)
        return false;

    XImage temp;
    XImage_init_ex(&temp, width, height,
                   bitsPerPixel == 24 ? XImageFormat_RGB888 : XImageFormat_ARGB32);
    if (XImage_isNull(&temp)) return false;
    for (int y = 0; y < height; ++y)
    {
        int sourceY = signedHeight < 0 ? y : height - 1 - y;
        const uint8_t* source = data + pixelOffset + (size_t)sourceY * (size_t)rowBytes;
        uint8_t* target = temp.m_data->m_data + (size_t)y * temp.m_data->m_bytesPerLine;
        for (int x = 0; x < width; ++x)
        {
            if (bitsPerPixel == 32)
                memcpy(target + x * 4, source + x * 4, 4);
            else
            {
                target[x * 3] = source[x * 3 + 2];
                target[x * 3 + 1] = source[x * 3 + 1];
                target[x * 3 + 2] = source[x * 3];
            }
        }
    }
    XImage_deinit_base(self);
    self->m_data = temp.m_data;
    temp.m_data = NULL;
    return true;
}

bool XImage_save(const XImage* self, const char* fileName, const char* format, int quality)
{
    (void)quality;
    if (!self || XImage_isNull(self) || !fileName || !XImage_isBmpFormat(format)) return false;
    bool withAlpha = XImage_hasAlphaChannel(self);
    XImage converted;
    XImage_init(&converted);
    XImage_convertToFormat(self, withAlpha ? XImageFormat_ARGB32 : XImageFormat_RGB888, 0, &converted);
    if (XImage_isNull(&converted)) return false;
    uint16_t bpp = withAlpha ? 32 : 24;
    uint64_t rowBytes64 = ((uint64_t)(uint32_t)XImage_width(&converted) * bpp + 31u) / 32u * 4u;
    uint64_t imageBytes64 = rowBytes64 * (uint32_t)XImage_height(&converted);
    if (imageBytes64 > UINT32_MAX - 54u)
    { XImage_deinit_base(&converted); return false; }
    uint32_t rowBytes = (uint32_t)rowBytes64;
    uint32_t imageBytes = (uint32_t)imageBytes64;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    XImage_writeLe32(header + 2, 54u + imageBytes);
    XImage_writeLe32(header + 10, 54u);
    XImage_writeLe32(header + 14, 40u);
    XImage_writeLe32(header + 18, (uint32_t)XImage_width(&converted));
    XImage_writeLe32(header + 22, (uint32_t)XImage_height(&converted));
    XImage_writeLe16(header + 26, 1u);
    XImage_writeLe16(header + 28, bpp);
    XImage_writeLe32(header + 34, imageBytes);
    FILE* file = fopen(fileName, "wb");
    if (!file) { XImage_deinit_base(&converted); return false; }
    bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    uint8_t padding[3] = {0};
    int pixelBytes = bpp / 8;
    for (int y = XImage_height(&converted) - 1; ok && y >= 0; --y)
    {
        const uint8_t* source = XImage_constScanLine(&converted, y);
        for (int x = 0; ok && x < XImage_width(&converted); ++x)
        {
            if (withAlpha)
                ok = fwrite(source + x * 4, 1, 4, file) == 4;
            else
            {
                uint8_t bgr[3] = {source[x * 3 + 2], source[x * 3 + 1], source[x * 3]};
                ok = fwrite(bgr, 1, 3, file) == 3;
            }
        }
        uint32_t paddingBytes = rowBytes - (uint32_t)XImage_width(&converted) * pixelBytes;
        if (ok && paddingBytes)
            ok = fwrite(padding, 1, paddingBytes, file) == paddingBytes;
    }
    if (fclose(file) != 0) ok = false;
    XImage_deinit_base(&converted);
    return ok;
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
        XImageData* newData = XImageData_clone(self->m_data);
        if (newData)
        {
            XImageData_unref(self->m_data);
            self->m_data = newData;
        }
    }
    if (XImage_isDetached(self))
        XImageData_markDirty(self->m_data);
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
