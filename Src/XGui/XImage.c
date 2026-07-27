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
    float            m_devicePixelRatio; /**< 设备像素比 */
    int              m_offsetX;          /**< X 偏移 */
    int              m_offsetY;          /**< Y 偏移 */
    uint8_t*         m_data;             /**< 像素数据指针 */
    bool             m_ownsData;         /**< 是否拥有数据所有权 */
    void (*m_cleanupFunc)(void*);        /**< 清理回调函数 */
    void*            m_cleanupInfo;      /**< 清理回调参数 */
    int64_t          m_cacheKey;         /**< 缓存键值 */
    int              m_serialNumber;     /**< 序列号（用于生成缓存键） */
}XImageData;

static XAtomic_uint32_t g_imageSerialCounter;  /**< 全局序列号计数器 */

static int64_t XImageData_nextCacheKey(void)
{
    uint32_t serial;
    do {
        serial = XAtomic_fetch_add_uint32(&g_imageSerialCounter, 1u, XAtomic_MemoryOrder_SeqCst) + 1u;
    } while (serial == 0);
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
    d->m_devicePixelRatio = 1.0f;
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
    d->m_serialNumber = (int)(XImageData_nextCacheKey() >> 32);
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
    copy->m_devicePixelRatio = source->m_devicePixelRatio;
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
    if (XClassIsVtableNull(dest)) XImage_init(dest);
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
    if (XClassIsVtableNull(dest)) XImage_init(dest);
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
    if (self->m_data) self->m_data->m_devicePixelRatio = 1.0f;
}

void XImage_init_ex_2(XImage* self, int width, int height, XImageFormat format,
                      int64_t bytesPerLine, uint8_t* data,
                      void (*cleanupFunction)(void*), void* cleanupInfo)
{
    if (ISNULL(self, "XImage")) return;
    XImage_init(self);
    self->m_data = XImageData_create(width, height, format, bytesPerLine, data, cleanupFunction, cleanupInfo);
    if (self->m_data) self->m_data->m_devicePixelRatio = 1.0f;
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
    if (ISNULL(XClassGetVtable(src), "Vtable")) return;
    XClassGetVirtualFunc(src, EXClass_Copy, void(*)(XImage*, const XImage*))(dest, src);
}

void XImage_move_base(XImage* dest, XImage* src)
{
    if (ISNULL(dest, "XImage") || ISNULL(src, "XImage")) return;
    if (ISNULL(XClassGetVtable(src), "Vtable")) return;
    XClassGetVirtualFunc(src, EXClass_Move, void(*)(XImage*, XImage*))(dest, src);
}

void XImage_deinit_base(XImage* self)
{
    if (ISNULL(self, "XImage")) return;
    if (ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XImage*))(self);
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
    XImage_fillRect(self, NULL, color);
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
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
            if ((XImage_pixel(self, x, y) >> 24) != 0xFFu)
                return true;
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
    if (self->m_data->m_format == XImageFormat_Mono ||
        self->m_data->m_format == XImageFormat_MonoLSB)
    {
        unsigned mask = self->m_data->m_format == XImageFormat_Mono
            ? (0x80u >> ((unsigned)x & 7u)) : (1u << ((unsigned)x & 7u));
        return (line[(unsigned)x >> 3] & mask) ? 1 : 0;
    }
    return 0;
}

static uint8_t XImage_expand5(unsigned value)
{
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t XImage_expand6(unsigned value)
{
    return (uint8_t)((value << 2) | (value >> 4));
}

static uint8_t XImage_luma(uint32_t color)
{
    unsigned r = (color >> 16) & 0xffu;
    unsigned g = (color >> 8) & 0xffu;
    unsigned b = color & 0xffu;
    return (uint8_t)((299u * r + 587u * g + 114u * b + 500u) / 1000u);
}

static uint16_t XImage_load16(const uint8_t* p)
{
    uint16_t value = 0;
    if (p) memcpy(&value, p, sizeof(value));
    return value;
}

static void XImage_store16(uint8_t* p, uint16_t value)
{
    if (p) memcpy(p, &value, sizeof(value));
}

static uint32_t XImage_load32(const uint8_t* p)
{
    uint32_t value = 0;
    if (p) memcpy(&value, p, sizeof(value));
    return value;
}

static void XImage_store32(uint8_t* p, uint32_t value)
{
    if (p) memcpy(p, &value, sizeof(value));
}

static uint8_t XImage_expand4(unsigned value)
{
    return (uint8_t)((value << 4) | value);
}

static uint8_t XImage_expand2(unsigned value)
{
    return (uint8_t)(value * 85u);
}

static uint8_t XImage_expand10(unsigned value)
{
    return (uint8_t)((value + 2u) >> 2);
}

static unsigned XImage_compress16(uint8_t value)
{
    return ((unsigned)value << 8) | value;
}

static unsigned XImage_compress6(uint8_t value)
{
    return ((unsigned)value * 63u + 127u) / 255u;
}

static unsigned XImage_compress5(uint8_t value)
{
    return ((unsigned)value * 31u + 127u) / 255u;
}

static unsigned XImage_compress4(uint8_t value)
{
    return ((unsigned)value * 15u + 127u) / 255u;
}

static unsigned XImage_compress2(uint8_t value)
{
    return ((unsigned)value * 3u + 127u) / 255u;
}

static unsigned XImage_compress10(uint8_t value)
{
    return ((unsigned)value * 1023u + 127u) / 255u;
}

static uint8_t XImage_unpremultiply8(uint8_t value, uint8_t alpha)
{
    if (alpha == 0 || alpha == 255) return value;
    unsigned result = ((unsigned)value * 255u + alpha / 2u) / alpha;
    return (uint8_t)(result > 255u ? 255u : result);
}

static uint16_t XImage_floatToHalf(float value)
{
    union { float f; uint32_t u; } bits;
    uint32_t sign, exponent, fraction;
    bits.f = value;
    sign = (bits.u >> 16) & 0x8000u;
    exponent = (bits.u >> 23) & 0xffu;
    fraction = bits.u & 0x7fffffu;
    if (exponent == 0xffu)
        return (uint16_t)(sign | (fraction ? 0x7e00u : 0x7c00u));
    if (exponent > 142u)
        return (uint16_t)(sign | 0x7c00u);
    if (exponent < 113u)
    {
        if (exponent < 103u) return (uint16_t)sign;
        fraction |= 0x800000u;
        return (uint16_t)(sign | (fraction >> (114u - exponent)));
    }
    return (uint16_t)(sign | ((exponent - 112u) << 10) |
                      ((fraction + 0x1000u) >> 13));
}

static float XImage_halfToFloat(uint16_t value)
{
    union { float f; uint32_t u; } bits;
    uint32_t sign = ((uint32_t)value & 0x8000u) << 16;
    int exponent = (int)(((uint32_t)value >> 10) & 0x1fu);
    uint32_t fraction = value & 0x3ffu;
    if (exponent == 0)
    {
        if (fraction == 0) bits.u = sign;
        else
        {
            exponent = 1;
            while ((fraction & 0x400u) == 0) { fraction <<= 1; --exponent; }
            fraction &= 0x3ffu;
            bits.u = sign | ((exponent + 112u) << 23) | (fraction << 13);
        }
    }
    else if (exponent == 0x1fu)
        bits.u = sign | 0x7f800000u | (fraction << 13);
    else
        bits.u = sign | ((exponent + 112u) << 23) | (fraction << 13);
    return bits.f;
}

static uint8_t XImage_floatChannel(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

static uint32_t XImage_readPixelValue(const XImageData* d, int x, int y)
{
    const uint8_t* line;
    uint32_t value;
    uint8_t a, r, g, b;
    if (!d || !d->m_data || x < 0 || y < 0 || x >= d->m_width || y >= d->m_height)
        return 0;
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    switch (d->m_format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        case XImageFormat_Indexed8:
        {
            unsigned index = d->m_format == XImageFormat_Indexed8
                ? line[x]
                : ((line[(unsigned)x >> 3] & (d->m_format == XImageFormat_Mono
                    ? (0x80u >> ((unsigned)x & 7u)) : (1u << ((unsigned)x & 7u)))) ? 1u : 0u);
            /* QImage::pixel() returns zero when an indexed pixel references
             * an entry outside the color table; it does not synthesize a
             * black/white fallback. */
            return d->m_colorTable && index < (unsigned)d->m_colorCount
                ? d->m_colorTable[index] : 0u;
        }
        case XImageFormat_RGB32:
            return XImage_load32(line + x * 4) | 0xff000000u;
        case XImageFormat_ARGB32:
            return XImage_load32(line + x * 4);
        case XImageFormat_ARGB32_Premultiplied:
            value = XImage_load32(line + x * 4);
            a = (uint8_t)(value >> 24);
            r = XImage_unpremultiply8((uint8_t)(value >> 16), a);
            g = XImage_unpremultiply8((uint8_t)(value >> 8), a);
            b = XImage_unpremultiply8((uint8_t)value, a);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                   ((uint32_t)g << 8) | b;
        case XImageFormat_RGB16:
            value = XImage_load16(line + x * 2);
            return 0xff000000u | ((uint32_t)XImage_expand5((value >> 11) & 0x1fu) << 16) |
                   ((uint32_t)XImage_expand6((value >> 5) & 0x3fu) << 8) | XImage_expand5(value & 0x1fu);
        case XImageFormat_RGB555:
            value = XImage_load16(line + x * 2);
            return 0xff000000u | ((uint32_t)XImage_expand5((value >> 10) & 0x1fu) << 16) |
                   ((uint32_t)XImage_expand5((value >> 5) & 0x1fu) << 8) | XImage_expand5(value & 0x1fu);
        case XImageFormat_RGB444:
            value = XImage_load16(line + x * 2);
            return 0xff000000u | ((uint32_t)XImage_expand4((value >> 8) & 0xfu) << 16) |
                   ((uint32_t)XImage_expand4((value >> 4) & 0xfu) << 8) | XImage_expand4(value & 0xfu);
        case XImageFormat_ARGB4444_Premultiplied:
            value = XImage_load16(line + x * 2);
            a = XImage_expand4((value >> 12) & 0xfu);
            r = XImage_unpremultiply8(XImage_expand4((value >> 8) & 0xfu), a);
            g = XImage_unpremultiply8(XImage_expand4((value >> 4) & 0xfu), a);
            b = XImage_unpremultiply8(XImage_expand4(value & 0xfu), a);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        case XImageFormat_ARGB8565_Premultiplied:
            value = XImage_load16(line + x * 3 + 1);
            a = line[x * 3];
            r = XImage_unpremultiply8(XImage_expand5((value >> 11) & 0x1fu), a);
            g = XImage_unpremultiply8(XImage_expand6((value >> 5) & 0x3fu), a);
            b = XImage_unpremultiply8(XImage_expand5(value & 0x1fu), a);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        case XImageFormat_ARGB8555_Premultiplied:
            value = XImage_load16(line + x * 3 + 1);
            a = line[x * 3];
            r = XImage_unpremultiply8(XImage_expand5((value >> 10) & 0x1fu), a);
            g = XImage_unpremultiply8(XImage_expand5((value >> 5) & 0x1fu), a);
            b = XImage_unpremultiply8(XImage_expand5(value & 0x1fu), a);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        case XImageFormat_RGB666:
        case XImageFormat_ARGB6666_Premultiplied:
        {
            uint32_t packed = ((uint32_t)line[x * 3] << 16) | ((uint32_t)line[x * 3 + 1] << 8) | line[x * 3 + 2];
            if (d->m_format == XImageFormat_RGB666)
            {
                a = 255; r = XImage_expand6((packed >> 18) & 0x3fu);
                g = XImage_expand6((packed >> 12) & 0x3fu); b = XImage_expand6((packed >> 6) & 0x3fu);
            }
            else
            {
                a = XImage_expand6((packed >> 18) & 0x3fu);
                r = XImage_unpremultiply8(XImage_expand6((packed >> 12) & 0x3fu), a);
                g = XImage_unpremultiply8(XImage_expand6((packed >> 6) & 0x3fu), a);
                b = XImage_unpremultiply8(XImage_expand6(packed & 0x3fu), a);
            }
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case XImageFormat_RGB888:
            return 0xff000000u | ((uint32_t)line[x * 3] << 16) | ((uint32_t)line[x * 3 + 1] << 8) | line[x * 3 + 2];
        case XImageFormat_BGR888:
            return 0xff000000u | ((uint32_t)line[x * 3 + 2] << 16) | ((uint32_t)line[x * 3 + 1] << 8) | line[x * 3];
        case XImageFormat_RGBX8888:
            return 0xff000000u | ((uint32_t)line[x * 4] << 16) | ((uint32_t)line[x * 4 + 1] << 8) | line[x * 4 + 2];
        case XImageFormat_RGBA8888:
            return ((uint32_t)line[x * 4 + 3] << 24) | ((uint32_t)line[x * 4] << 16) |
                   ((uint32_t)line[x * 4 + 1] << 8) | line[x * 4 + 2];
        case XImageFormat_RGBA8888_Premultiplied:
            a = line[x * 4 + 3];
            r = XImage_unpremultiply8(line[x * 4], a);
            g = XImage_unpremultiply8(line[x * 4 + 1], a);
            b = XImage_unpremultiply8(line[x * 4 + 2], a);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                   ((uint32_t)g << 8) | b;
        case XImageFormat_BGR30:
        case XImageFormat_RGB30:
        case XImageFormat_A2BGR30_Premultiplied:
        case XImageFormat_A2RGB30_Premultiplied:
        {
            value = XImage_load32(line + x * 4);
            bool alpha = d->m_format == XImageFormat_A2BGR30_Premultiplied || d->m_format == XImageFormat_A2RGB30_Premultiplied;
            a = alpha ? XImage_expand2((value >> 30) & 3u) : 255;
            if (d->m_format == XImageFormat_BGR30 || d->m_format == XImageFormat_A2BGR30_Premultiplied)
            {
                r = XImage_expand10(value & 0x3ffu); g = XImage_expand10((value >> 10) & 0x3ffu); b = XImage_expand10((value >> 20) & 0x3ffu);
            }
            else
            {
                r = XImage_expand10((value >> 20) & 0x3ffu); g = XImage_expand10((value >> 10) & 0x3ffu); b = XImage_expand10(value & 0x3ffu);
            }
            if (alpha) { r = XImage_unpremultiply8(r, a); g = XImage_unpremultiply8(g, a); b = XImage_unpremultiply8(b, a); }
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case XImageFormat_Alpha8:
            return (uint32_t)line[x] << 24;
        case XImageFormat_Grayscale8:
            return 0xff000000u | (uint32_t)line[x] * 0x010101u;
        case XImageFormat_Grayscale16:
            value = XImage_load16(line + x * 2);
            a = (uint8_t)(value >> 8);
            return 0xff000000u | (uint32_t)a * 0x010101u;
        case XImageFormat_RGBX64:
        case XImageFormat_RGBA64:
        case XImageFormat_RGBA64_Premultiplied:
        {
            const uint8_t* p = line + x * 8;
            uint16_t rv = XImage_load16(p), gv = XImage_load16(p + 2), bv = XImage_load16(p + 4), av = XImage_load16(p + 6);
            a = d->m_format == XImageFormat_RGBX64 ? 255 : (uint8_t)(av >> 8);
            r = d->m_format == XImageFormat_RGBA64_Premultiplied ? XImage_unpremultiply8((uint8_t)(rv >> 8), a) : (uint8_t)(rv >> 8);
            g = d->m_format == XImageFormat_RGBA64_Premultiplied ? XImage_unpremultiply8((uint8_t)(gv >> 8), a) : (uint8_t)(gv >> 8);
            b = d->m_format == XImageFormat_RGBA64_Premultiplied ? XImage_unpremultiply8((uint8_t)(bv >> 8), a) : (uint8_t)(bv >> 8);
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case XImageFormat_RGBX16FPx4:
        case XImageFormat_RGBA16FPx4:
        case XImageFormat_RGBA16FPx4_Premultiplied:
        {
            const uint8_t* p = line + x * 8;
            float rf = XImage_halfToFloat(XImage_load16(p)), gf = XImage_halfToFloat(XImage_load16(p + 2));
            float bf = XImage_halfToFloat(XImage_load16(p + 4)), af = XImage_halfToFloat(XImage_load16(p + 6));
            a = d->m_format == XImageFormat_RGBX16FPx4 ? 255 : XImage_floatChannel(af);
            r = XImage_floatChannel(rf); g = XImage_floatChannel(gf); b = XImage_floatChannel(bf);
            if (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied) { r = XImage_unpremultiply8(r, a); g = XImage_unpremultiply8(g, a); b = XImage_unpremultiply8(b, a); }
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case XImageFormat_RGBX32FPx4:
        case XImageFormat_RGBA32FPx4:
        case XImageFormat_RGBA32FPx4_Premultiplied:
        {
            float rf, gf, bf, af;
            const uint8_t* p = line + x * 16;
            memcpy(&rf, p, sizeof(float)); memcpy(&gf, p + 4, sizeof(float)); memcpy(&bf, p + 8, sizeof(float)); memcpy(&af, p + 12, sizeof(float));
            a = d->m_format == XImageFormat_RGBX32FPx4 ? 255 : XImage_floatChannel(af);
            r = XImage_floatChannel(rf); g = XImage_floatChannel(gf); b = XImage_floatChannel(bf);
            if (d->m_format == XImageFormat_RGBA32FPx4_Premultiplied) { r = XImage_unpremultiply8(r, a); g = XImage_unpremultiply8(g, a); b = XImage_unpremultiply8(b, a); }
            return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case XImageFormat_CMYK8888:
        {
            unsigned c = line[x * 4], m = line[x * 4 + 1], yy = line[x * 4 + 2], k = line[x * 4 + 3];
            r = (uint8_t)(255u - (c + k > 255u ? 255u : c + k)); g = (uint8_t)(255u - (m + k > 255u ? 255u : m + k)); b = (uint8_t)(255u - (yy + k > 255u ? 255u : yy));
            return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        default:
            return 0;
    }
}

static uint32_t XImage_readColorValue(const XImageData* d, int x, int y)
{
    /* XImage_readPixelValue already exposes the public, un-premultiplied
     * ARGB value.  Conversion and filtering must consume that value exactly
     * once; un-premultiplying here again would brighten semi-transparent
     * pixels. */
    return XImage_readPixelValue(d, x, y);
}

static void XImage_writePixelValue(XImageData* d, int x, int y, uint32_t color)
{
    uint8_t* line;
    uint8_t a, r, g, b;
    uint32_t value;
    if (!d || !d->m_data || x < 0 || y < 0 || x >= d->m_width || y >= d->m_height)
        return;
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    a = (uint8_t)(color >> 24); r = (uint8_t)(color >> 16); g = (uint8_t)(color >> 8); b = (uint8_t)color;
    switch (d->m_format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        {
            unsigned mask = d->m_format == XImageFormat_Mono ? (0x80u >> ((unsigned)x & 7u)) : (1u << ((unsigned)x & 7u));
            if (color & 0x00ffffffu) line[(unsigned)x >> 3] |= (uint8_t)mask; else line[(unsigned)x >> 3] &= (uint8_t)~mask;
            break;
        }
        case XImageFormat_Indexed8:
        {
            unsigned index = 0, best = UINT_MAX, distance = UINT_MAX;
            if (d->m_colorTable && d->m_colorCount > 0)
            {
                for (int i = 0; i < d->m_colorCount; ++i)
                {
                    uint32_t pc = d->m_colorTable[i];
                    unsigned dr = (unsigned)((pc >> 16) & 255u) > r ? ((pc >> 16) & 255u) - r : r - ((pc >> 16) & 255u);
                    unsigned dg = (unsigned)((pc >> 8) & 255u) > g ? ((pc >> 8) & 255u) - g : g - ((pc >> 8) & 255u);
                    unsigned db = (unsigned)(pc & 255u) > b ? (pc & 255u) - b : b - (pc & 255u);
                    unsigned da = (unsigned)(pc >> 24) > a ? (pc >> 24) - a : a - (pc >> 24);
                    unsigned score = dr * dr + dg * dg + db * db + da * da;
                    if (score < distance) { distance = score; best = (unsigned)i; }
                }
                if (best != UINT_MAX) index = best;
            }
            line[x] = (uint8_t)index;
            break;
        }
        case XImageFormat_RGB32:
            XImage_store32(line + x * 4, color | 0xff000000u); break;
        case XImageFormat_ARGB32:
            XImage_store32(line + x * 4, color); break;
        case XImageFormat_ARGB32_Premultiplied:
            value = ((uint32_t)a << 24) | ((((uint32_t)r * a + 127u) / 255u) << 16) |
                    ((((uint32_t)g * a + 127u) / 255u) << 8) | (((uint32_t)b * a + 127u) / 255u);
            XImage_store32(line + x * 4, value); break;
        case XImageFormat_RGB16:
            value = (XImage_compress5(r) << 11) | (XImage_compress6(g) << 5) | XImage_compress5(b);
            XImage_store16(line + x * 2, (uint16_t)value); break;
        case XImageFormat_RGB555:
            value = (XImage_compress5(r) << 10) | (XImage_compress5(g) << 5) | XImage_compress5(b);
            XImage_store16(line + x * 2, (uint16_t)value); break;
        case XImageFormat_RGB444:
            value = (XImage_compress4(r) << 8) | (XImage_compress4(g) << 4) | XImage_compress4(b);
            XImage_store16(line + x * 2, (uint16_t)value); break;
        case XImageFormat_ARGB4444_Premultiplied:
            value = (XImage_compress4(a) << 12) | ((XImage_compress4((uint8_t)((r * a + 127u) / 255u)) << 8)) |
                    (XImage_compress4((uint8_t)((g * a + 127u) / 255u)) << 4) | XImage_compress4((uint8_t)((b * a + 127u) / 255u));
            XImage_store16(line + x * 2, (uint16_t)value); break;
        case XImageFormat_ARGB8565_Premultiplied:
            line[x * 3] = a; value = (XImage_compress5((uint8_t)((r * a + 127u) / 255u)) << 11) |
                (XImage_compress6((uint8_t)((g * a + 127u) / 255u)) << 5) | XImage_compress5((uint8_t)((b * a + 127u) / 255u));
            XImage_store16(line + x * 3 + 1, (uint16_t)value); break;
        case XImageFormat_ARGB8555_Premultiplied:
            line[x * 3] = a; value = (XImage_compress5((uint8_t)((r * a + 127u) / 255u)) << 10) |
                (XImage_compress5((uint8_t)((g * a + 127u) / 255u)) << 5) | XImage_compress5((uint8_t)((b * a + 127u) / 255u));
            XImage_store16(line + x * 3 + 1, (uint16_t)value); break;
        case XImageFormat_RGB666:
        case XImageFormat_ARGB6666_Premultiplied:
        {
            unsigned aa = d->m_format == XImageFormat_RGB666 ? 63u : XImage_compress6(a);
            unsigned rr = XImage_compress6(d->m_format == XImageFormat_RGB666 ? r : (uint8_t)((r * a + 127u) / 255u));
            unsigned gg = XImage_compress6(d->m_format == XImageFormat_RGB666 ? g : (uint8_t)((g * a + 127u) / 255u));
            unsigned bb = XImage_compress6(d->m_format == XImageFormat_RGB666 ? b : (uint8_t)((b * a + 127u) / 255u));
            value = d->m_format == XImageFormat_RGB666
                ? ((rr << 18) | (gg << 12) | (bb << 6))
                : ((aa << 18) | (rr << 12) | (gg << 6) | bb);
            line[x * 3] = (uint8_t)(value >> 16); line[x * 3 + 1] = (uint8_t)(value >> 8); line[x * 3 + 2] = (uint8_t)value;
            break;
        }
        case XImageFormat_RGB888:
            line[x * 3] = r; line[x * 3 + 1] = g; line[x * 3 + 2] = b; break;
        case XImageFormat_BGR888:
            line[x * 3] = b; line[x * 3 + 1] = g; line[x * 3 + 2] = r; break;
        case XImageFormat_RGBX8888:
            line[x * 4] = r; line[x * 4 + 1] = g; line[x * 4 + 2] = b; line[x * 4 + 3] = 0xff; break;
        case XImageFormat_RGBA8888:
            line[x * 4] = r; line[x * 4 + 1] = g; line[x * 4 + 2] = b; line[x * 4 + 3] = a; break;
        case XImageFormat_RGBA8888_Premultiplied:
            line[x * 4] = (uint8_t)((r * a + 127u) / 255u); line[x * 4 + 1] = (uint8_t)((g * a + 127u) / 255u);
            line[x * 4 + 2] = (uint8_t)((b * a + 127u) / 255u); line[x * 4 + 3] = a; break;
        case XImageFormat_BGR30:
        case XImageFormat_RGB30:
        case XImageFormat_A2BGR30_Premultiplied:
        case XImageFormat_A2RGB30_Premultiplied:
        {
            bool alpha = d->m_format == XImageFormat_A2BGR30_Premultiplied || d->m_format == XImageFormat_A2RGB30_Premultiplied;
            unsigned aa = alpha ? XImage_compress2(a) : 3u;
            unsigned rr = XImage_compress10(alpha ? (uint8_t)((r * a + 127u) / 255u) : r);
            unsigned gg = XImage_compress10(alpha ? (uint8_t)((g * a + 127u) / 255u) : g);
            unsigned bb = XImage_compress10(alpha ? (uint8_t)((b * a + 127u) / 255u) : b);
            if (d->m_format == XImageFormat_BGR30 || d->m_format == XImageFormat_A2BGR30_Premultiplied) value = (aa << 30) | (bb << 20) | (gg << 10) | rr;
            else value = (aa << 30) | (rr << 20) | (gg << 10) | bb;
            XImage_store32(line + x * 4, value); break;
        }
        case XImageFormat_Alpha8:
            line[x] = a; break;
        case XImageFormat_Grayscale8:
            line[x] = XImage_luma(color); break;
        case XImageFormat_Grayscale16:
            XImage_store16(line + x * 2, (uint16_t)XImage_luma(color) * 0x0101u); break;
        case XImageFormat_RGBX64:
        case XImageFormat_RGBA64:
        case XImageFormat_RGBA64_Premultiplied:
        {
            uint16_t rv = (uint16_t)(XImage_compress16(d->m_format == XImageFormat_RGBA64_Premultiplied ? (uint8_t)((r * a + 127u) / 255u) : r));
            uint16_t gv = (uint16_t)(XImage_compress16(d->m_format == XImageFormat_RGBA64_Premultiplied ? (uint8_t)((g * a + 127u) / 255u) : g));
            uint16_t bv = (uint16_t)(XImage_compress16(d->m_format == XImageFormat_RGBA64_Premultiplied ? (uint8_t)((b * a + 127u) / 255u) : b));
            uint16_t av = d->m_format == XImageFormat_RGBX64 ? 0xffffu : (uint16_t)XImage_compress16(a);
            uint8_t* p = line + x * 8; XImage_store16(p, rv); XImage_store16(p + 2, gv); XImage_store16(p + 4, bv); XImage_store16(p + 6, av); break;
        }
        case XImageFormat_RGBX16FPx4:
        case XImageFormat_RGBA16FPx4:
        case XImageFormat_RGBA16FPx4_Premultiplied:
        {
            uint8_t* p = line + x * 8; float af = a / 255.0f;
            float rf = (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ? (r / 255.0f) * af : r / 255.0f);
            float gf = (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ? (g / 255.0f) * af : g / 255.0f);
            float bf = (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ? (b / 255.0f) * af : b / 255.0f);
            XImage_store16(p, XImage_floatToHalf(rf)); XImage_store16(p + 2, XImage_floatToHalf(gf)); XImage_store16(p + 4, XImage_floatToHalf(bf)); XImage_store16(p + 6, XImage_floatToHalf(d->m_format == XImageFormat_RGBX16FPx4 ? 1.0f : af)); break;
        }
        case XImageFormat_RGBX32FPx4:
        case XImageFormat_RGBA32FPx4:
        case XImageFormat_RGBA32FPx4_Premultiplied:
        {
            uint8_t* p = line + x * 16; float af = a / 255.0f;
            float rf = d->m_format == XImageFormat_RGBA32FPx4_Premultiplied ? (r / 255.0f) * af : r / 255.0f;
            float gf = d->m_format == XImageFormat_RGBA32FPx4_Premultiplied ? (g / 255.0f) * af : g / 255.0f;
            float bf = d->m_format == XImageFormat_RGBA32FPx4_Premultiplied ? (b / 255.0f) * af : b / 255.0f;
            float xf = d->m_format == XImageFormat_RGBX32FPx4 ? 1.0f : af;
            memcpy(p, &rf, sizeof(float)); memcpy(p + 4, &gf, sizeof(float)); memcpy(p + 8, &bf, sizeof(float)); memcpy(p + 12, &xf, sizeof(float)); break;
        }
        case XImageFormat_CMYK8888:
        {
            unsigned k = 255u - (r > g ? (r > b ? r : b) : (g > b ? g : b));
            line[x * 4] = (uint8_t)(r + k > 255u ? 0u : 255u - r - k);
            line[x * 4 + 1] = (uint8_t)(g + k > 255u ? 0u : 255u - g - k);
            line[x * 4 + 2] = (uint8_t)(b + k > 255u ? 0u : 255u - b - k);
            line[x * 4 + 3] = (uint8_t)k; break;
        }
        default:
            break;
    }
}

static void XImage_writePixelIndex(XImageData* d, int x, int y, uint32_t indexOrRgb)
{
    if (!d || !d->m_data || x < 0 || y < 0 || x >= d->m_width || y >= d->m_height) return;
    uint8_t* line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    if (d->m_format == XImageFormat_Indexed8)
        line[x] = (uint8_t)indexOrRgb;
    else if (d->m_format == XImageFormat_Mono || d->m_format == XImageFormat_MonoLSB)
    {
        unsigned mask = d->m_format == XImageFormat_Mono ? (0x80u >> ((unsigned)x & 7u)) : (1u << ((unsigned)x & 7u));
        if (indexOrRgb & 1u) line[(unsigned)x >> 3] |= (uint8_t)mask;
        else line[(unsigned)x >> 3] &= (uint8_t)~mask;
    }
    else
        XImage_writePixelValue(d, x, y, indexOrRgb);
}

uint32_t XImage_pixel(const XImage* self, int x, int y)
{
    return (self && self->m_data) ? XImage_readPixelValue(self->m_data, x, y) : 0;
}

void XImage_setPixel(XImage* self, int x, int y, uint32_t indexOrRgb)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    XImage_writePixelIndex(self->m_data, x, y, indexOrRgb);
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
    if (out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_copyRect(self, rect, &temp);
        XImage_deinit_base(out);
        out->m_data = temp.m_data;
        temp.m_data = NULL;
        XImage_deinit_base(&temp);
        return;
    }
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
    int w = self->m_data->m_width, h = self->m_data->m_height;
    int rx = 0, ry = 0, rw = w, rh = h;
    if (rect) { rx = rect->x; ry = rect->y; rw = rect->width; rh = rect->height; }
    if (rw <= 0 || rh <= 0) return;
    XImage_init_ex(out, rw, rh, self->m_data->m_format);
    if (!out->m_data) return;
    out->m_data->m_dpmX = self->m_data->m_dpmX;
    out->m_data->m_dpmY = self->m_data->m_dpmY;
    out->m_data->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
    out->m_data->m_offsetX = self->m_data->m_offsetX;
    out->m_data->m_offsetY = self->m_data->m_offsetY;
    if (self->m_data->m_colorCount > 0 && self->m_data->m_colorTable)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System((size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        if (!out->m_data->m_colorTable) { XImage_deinit_base(out); XImage_init(out); return; }
        memcpy(out->m_data->m_colorTable, self->m_data->m_colorTable, (size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        out->m_data->m_colorCount = self->m_data->m_colorCount;
    }
    for (int y = 0; y < rh; ++y)
        for (int x = 0; x < rw; ++x)
        {
            int64_t sourceX64 = (int64_t)rx + x, sourceY64 = (int64_t)ry + y;
            if (sourceX64 < 0 || sourceY64 < 0 || sourceX64 >= w || sourceY64 >= h) continue;
            int sourceX = (int)sourceX64, sourceY = (int)sourceY64;
            if (self->m_data->m_format == XImageFormat_Indexed8 || self->m_data->m_format == XImageFormat_Mono || self->m_data->m_format == XImageFormat_MonoLSB)
                XImage_writePixelIndex(out->m_data, x, y, (uint32_t)XImage_pixelIndex(self, sourceX, sourceY));
            else
                XImage_writePixelValue(out->m_data, x, y, XImage_readColorValue(self->m_data, sourceX, sourceY));
        }
}

void XImage_convertToFormat(const XImage* self, XImageFormat format, uint32_t flags, XImage* out)
{
    (void)flags;
    if (!out) return;
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_convertToFormat(self, format, flags, &temp);
        XImage_deinit_base(out);
        out->m_data = temp.m_data;
        temp.m_data = NULL;
        XImage_deinit_base(&temp);
        return;
    }
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data ||
        format <= XImageFormat_Invalid || format >= XImageFormat_NImageFormats) return;
    if (self->m_data->m_format == format)
    {
        out->m_data = XImageData_clone(self->m_data);
        return;
    }
    out->m_data = XImageData_create(self->m_data->m_width, self->m_data->m_height,
                                     format, 0, NULL, NULL, NULL);
    if (!out->m_data)
        return;
    out->m_data->m_dpmX = self->m_data->m_dpmX;
    out->m_data->m_dpmY = self->m_data->m_dpmY;
    out->m_data->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
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
    else if (format == XImageFormat_Indexed8)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System(256 * sizeof(uint32_t));
        if (!out->m_data->m_colorTable) { XImageData_unref(out->m_data); out->m_data = NULL; return; }
        out->m_data->m_colorCount = 256;
        for (int i = 0; i < 256; ++i) out->m_data->m_colorTable[i] = 0xff000000u | (uint32_t)i * 0x010101u;
        if ((self->m_data->m_format == XImageFormat_Indexed8 || self->m_data->m_format == XImageFormat_Mono || self->m_data->m_format == XImageFormat_MonoLSB) &&
            self->m_data->m_colorTable && self->m_data->m_colorCount > 0)
        {
            int count = self->m_data->m_colorCount < 256 ? self->m_data->m_colorCount : 256;
            memcpy(out->m_data->m_colorTable, self->m_data->m_colorTable, (size_t)count * sizeof(uint32_t));
        }
    }
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
        {
            bool sourceMono = self->m_data->m_format == XImageFormat_Mono ||
                              self->m_data->m_format == XImageFormat_MonoLSB;
            bool targetMono = format == XImageFormat_Mono || format == XImageFormat_MonoLSB;
            /* A monochrome image may legitimately have no color table.  Keep
             * its stored bit when converting between packed formats instead
             * of routing through pixel(), which returns zero for that case. */
            if (sourceMono && (targetMono || format == XImageFormat_Indexed8))
                XImage_writePixelIndex(out->m_data, x, y,
                                       (uint32_t)XImage_pixelIndex(self, x, y));
            else
                XImage_writePixelValue(out->m_data, x, y,
                                       XImage_readColorValue(self->m_data, x, y));
        }
}

void XImage_convertToFormat_ex(const XImage* self, XImageFormat format,
                               const uint32_t* colorTable, int colorCount,
                               uint32_t flags, XImage* out)
{
    XImage_convertToFormat(self, format, flags, out);
    if (!out || !out->m_data || !colorTable || colorCount <= 0 ||
        (format != XImageFormat_Mono && format != XImageFormat_MonoLSB &&
         format != XImageFormat_Indexed8) ||
        (format == XImageFormat_Indexed8 && colorCount > 256))
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
    if (!self || !self->m_data || format <= XImageFormat_Invalid || format >= XImageFormat_NImageFormats) return false;
    // 检查格式兼容性（相同位深度）
    if (XImageFormat_bitDepth(self->m_data->m_format) != XImageFormat_bitDepth(format))
        return false;
    XImage_detach(self);
    self->m_data->m_format = format;
    self->m_data->m_depth = XImageFormat_bitDepth(format);
    if (format != XImageFormat_Indexed8 && format != XImageFormat_Mono && format != XImageFormat_MonoLSB)
    {
        XFree_System(self->m_data->m_colorTable);
        self->m_data->m_colorTable = NULL;
        self->m_data->m_colorCount = 0;
    }
    XImageData_markDirty(self->m_data);
    return true;
}

void XImage_mirrored(const XImage* self, bool horizontal, bool vertical, XImage* out)
{
    if (self && out && (!horizontal && !vertical ||
                        (self->m_data && self->m_data->m_width <= 1 && self->m_data->m_height <= 1)))
    {
        if ((const XImage*)out != self) XImage_copy(out, self);
        return;
    }
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_mirrored(self, horizontal, vertical, &temp);
        if (temp.m_data)
        {
            XImage_deinit_base(out);
            out->m_data = temp.m_data;
            temp.m_data = NULL;
        }
        XImage_deinit_base(&temp);
        return;
    }
    if (!out) return;
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data) return;
    XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, self->m_data->m_format);
    if (!out->m_data) return;
    out->m_data->m_dpmX = self->m_data->m_dpmX;
    out->m_data->m_dpmY = self->m_data->m_dpmY;
    out->m_data->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
    out->m_data->m_offsetX = self->m_data->m_offsetX;
    out->m_data->m_offsetY = self->m_data->m_offsetY;
    if (self->m_data->m_colorCount > 0 && self->m_data->m_colorTable)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System(
            (size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        if (!out->m_data->m_colorTable)
        {
            XImageData_unref(out->m_data);
            out->m_data = NULL;
            return;
        }
        memcpy(out->m_data->m_colorTable, self->m_data->m_colorTable,
               (size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        out->m_data->m_colorCount = self->m_data->m_colorCount;
    }
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    for (int y = 0; y < h; y++)
    {
        int srcY = vertical ? (h - 1 - y) : y;
        for (int x = 0; x < w; x++)
        {
            int srcX = horizontal ? (w - 1 - x) : x;
            if (self->m_data->m_format == XImageFormat_Indexed8 || self->m_data->m_format == XImageFormat_Mono || self->m_data->m_format == XImageFormat_MonoLSB)
                XImage_writePixelIndex(out->m_data, x, y, (uint32_t)XImage_pixelIndex(self, srcX, srcY));
            else
                XImage_writePixelValue(out->m_data, x, y, XImage_readColorValue(self->m_data, srcX, srcY));
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
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_rgbSwapped(self, &temp);
        if (temp.m_data)
        {
            XImage_deinit_base(out);
            out->m_data = temp.m_data;
            temp.m_data = NULL;
        }
        XImage_deinit_base(&temp);
        return;
    }
    if (!out) return;
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
    XImage_init(out);
    if (!self || !self->m_data || !self->m_data->m_data) return;
    out->m_data = XImageData_clone(self->m_data);
    if (!out->m_data) return;
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    if (self->m_data->m_format == XImageFormat_Indexed8 && out->m_data->m_colorTable)
    {
        for (int i = 0; i < out->m_data->m_colorCount; ++i)
        {
            uint32_t c = out->m_data->m_colorTable[i];
            out->m_data->m_colorTable[i] = (c & 0xff00ff00u) |
                ((c & 0x00ff0000u) >> 16) | ((c & 0x000000ffu) << 16);
        }
    }
    else if (self->m_data->m_format != XImageFormat_Mono &&
             self->m_data->m_format != XImageFormat_MonoLSB &&
             self->m_data->m_format != XImageFormat_Alpha8)
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                uint32_t c;
                if (self->m_data->m_format == XImageFormat_ARGB32_Premultiplied)
                {
                    c = XImage_load32(self->m_data->m_data + (size_t)y * self->m_data->m_bytesPerLine + x * 4);
                    c = (c & 0xff00ff00u) | ((c & 0x00ff0000u) >> 16) | ((c & 0x000000ffu) << 16);
                    XImage_store32(out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine + x * 4, c);
                }
                else if (self->m_data->m_format == XImageFormat_RGBA8888_Premultiplied)
                {
                    uint8_t* src = self->m_data->m_data + (size_t)y * self->m_data->m_bytesPerLine + x * 4;
                    uint8_t* dst = out->m_data->m_data + (size_t)y * out->m_data->m_bytesPerLine + x * 4;
                    dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
                }
                else
                {
                    c = XImage_readColorValue(self->m_data, x, y);
                    c = (c & 0xff00ff00u) | ((c & 0x00ff0000u) >> 16) | ((c & 0x000000ffu) << 16);
                    XImage_writePixelValue(out->m_data, x, y, c);
                }
            }
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
    if (!out) return;
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_scaled(self, width, height, aspectMode, mode, &temp);
        XImage_deinit_base(out);
        out->m_data = temp.m_data;
        temp.m_data = NULL;
        XImage_deinit_base(&temp);
        return;
    }
    if (!XClassIsVtableNull(out)) XImage_deinit_base(out);
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
    out->m_data->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
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
    const bool smooth = mode != 0;
    for (int y = 0; y < targetHeight; y++)
    {
        for (int x = 0; x < targetWidth; x++)
        {
            int srcX = (int)((int64_t)x * sw / targetWidth), srcY = (int)((int64_t)y * sh / targetHeight);
            uint32_t color = XImage_readColorValue(self->m_data, srcX, srcY);
            if (smooth && !(srcX == sw - 1 && srcY == sh - 1))
            {
                double fx = ((double)x + 0.5) * sw / targetWidth - 0.5;
                double fy = ((double)y + 0.5) * sh / targetHeight - 0.5;
                int x0 = fx < 0.0 ? 0 : (int)fx, y0 = fy < 0.0 ? 0 : (int)fy;
                int x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
                double tx = fx < 0.0 ? 0.0 : fx - x0, ty = fy < 0.0 ? 0.0 : fy - y0;
                uint32_t c00 = XImage_readColorValue(self->m_data, x0, y0), c10 = XImage_readColorValue(self->m_data, x1, y0);
                uint32_t c01 = XImage_readColorValue(self->m_data, x0, y1), c11 = XImage_readColorValue(self->m_data, x1, y1);
                unsigned aa = (unsigned)(((((c00 >> 24) & 255u) * (1.0 - tx) + ((c10 >> 24) & 255u) * tx) * (1.0 - ty) + (((c01 >> 24) & 255u) * (1.0 - tx) + ((c11 >> 24) & 255u) * tx) * ty) + 0.5);
                unsigned rr = (unsigned)(((((c00 >> 16) & 255u) * (1.0 - tx) + ((c10 >> 16) & 255u) * tx) * (1.0 - ty) + (((c01 >> 16) & 255u) * (1.0 - tx) + ((c11 >> 16) & 255u) * tx) * ty) + 0.5);
                unsigned gg = (unsigned)(((((c00 >> 8) & 255u) * (1.0 - tx) + ((c10 >> 8) & 255u) * tx) * (1.0 - ty) + (((c01 >> 8) & 255u) * (1.0 - tx) + ((c11 >> 8) & 255u) * tx) * ty) + 0.5);
                unsigned bb = (unsigned)((((c00 & 255u) * (1.0 - tx) + (c10 & 255u) * tx) * (1.0 - ty) + ((c01 & 255u) * (1.0 - tx) + (c11 & 255u) * tx) * ty) + 0.5);
                color = (aa << 24) | (rr << 16) | (gg << 8) | bb;
            }
            if (!smooth && (self->m_data->m_format == XImageFormat_Indexed8 || self->m_data->m_format == XImageFormat_Mono || self->m_data->m_format == XImageFormat_MonoLSB))
                XImage_writePixelIndex(out->m_data, x, y, (uint32_t)XImage_pixelIndex(self, srcX, srcY));
            else
                XImage_writePixelValue(out->m_data, x, y, color);
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
    XImageData_markDirty(self->m_data);
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
    XImageData_markDirty(self->m_data);
}

float XImage_devicePixelRatio(const XImage* self)
{
    return (self && self->m_data && self->m_data->m_devicePixelRatio > 0.0f)
        ? self->m_data->m_devicePixelRatio : 1.0f;
}

void XImage_setDevicePixelRatio(XImage* self, float scaleFactor)
{
    if (!self || !self->m_data || scaleFactor <= 0.0f || scaleFactor == XImage_devicePixelRatio(self))
        return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    self->m_data->m_devicePixelRatio = scaleFactor;
    XImageData_markDirty(self->m_data);
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
    XImageData_markDirty(self->m_data);
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
    return self && self->m_data && XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
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
        case XImageFormat_RGB16: return "RGB16";
        case XImageFormat_ARGB8565_Premultiplied: return "ARGB8565_Premultiplied";
        case XImageFormat_RGB666: return "RGB666";
        case XImageFormat_ARGB6666_Premultiplied: return "ARGB6666_Premultiplied";
        case XImageFormat_RGB555: return "RGB555";
        case XImageFormat_ARGB8555_Premultiplied: return "ARGB8555_Premultiplied";
        case XImageFormat_RGB888: return "RGB888";
        case XImageFormat_RGB444: return "RGB444";
        case XImageFormat_ARGB4444_Premultiplied: return "ARGB4444_Premultiplied";
        case XImageFormat_RGBX8888: return "RGBX8888";
        case XImageFormat_RGBA8888: return "RGBA8888";
        case XImageFormat_RGBA8888_Premultiplied: return "RGBA8888_Premultiplied";
        case XImageFormat_BGR30: return "BGR30";
        case XImageFormat_A2BGR30_Premultiplied: return "A2BGR30_Premultiplied";
        case XImageFormat_RGB30: return "RGB30";
        case XImageFormat_A2RGB30_Premultiplied: return "A2RGB30_Premultiplied";
        case XImageFormat_Alpha8: return "Alpha8";
        case XImageFormat_Grayscale8: return "Grayscale8";
        case XImageFormat_RGBX64: return "RGBX64";
        case XImageFormat_RGBA64: return "RGBA64";
        case XImageFormat_RGBA64_Premultiplied: return "RGBA64_Premultiplied";
        case XImageFormat_Grayscale16: return "Grayscale16";
        case XImageFormat_BGR888: return "BGR888";
        case XImageFormat_RGBX16FPx4: return "RGBX16FPx4";
        case XImageFormat_RGBA16FPx4: return "RGBA16FPx4";
        case XImageFormat_RGBA16FPx4_Premultiplied: return "RGBA16FPx4_Premultiplied";
        case XImageFormat_RGBX32FPx4: return "RGBX32FPx4";
        case XImageFormat_RGBA32FPx4: return "RGBA32FPx4";
        case XImageFormat_RGBA32FPx4_Premultiplied: return "RGBA32FPx4_Premultiplied";
        case XImageFormat_CMYK8888: return "CMYK8888";
        default: return "Unknown";
    }
}




/* ========== 像素查询方法 ========== */

bool XImage_allGray(const XImage* self)
{
    if (!self || !self->m_data || !self->m_data->m_data)
        return false;
    if (self->m_data->m_width <= 0 || self->m_data->m_height <= 0) return false;
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
        {
            uint32_t c = XImage_pixel(self, x, y);
            if (((c >> 16) & 255u) != ((c >> 8) & 255u) || ((c >> 8) & 255u) != (c & 255u)) return false;
        }
    return true;
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
    int64_t left64 = rx, top64 = ry, right64 = (int64_t)rx + rw, bottom64 = (int64_t)ry + rh;
    if (left64 < 0) left64 = 0;
    if (top64 < 0) top64 = 0;
    if (right64 > w) right64 = w;
    if (bottom64 > h) bottom64 = h;
    if (right64 <= left64 || bottom64 <= top64) return;
    rx = (int)left64; ry = (int)top64; rw = (int)(right64 - left64); rh = (int)(bottom64 - top64);
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    for (int y = ry; y < ry + rh; y++)
        for (int x = rx; x < rx + rw; x++)
            XImage_writePixelValue(self->m_data, x, y, color);
}

void XImage_clear(XImage* self, const XRect* rect, uint32_t color)
{
    XImage_fillRect(self, rect, color);
}

void XImage_invertPixels(XImage* self, XImageInvertMode mode)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    const int depth = self->m_data->m_depth;
    if (XImageFormat_isPremultiplied(self->m_data->m_format))
    {
        XImageFormat original = self->m_data->m_format;
        XImageFormat intermediate = depth > 128 ? XImageFormat_RGBA32FPx4 :
            (depth > 64 ? XImageFormat_RGBA32FPx4 :
             (depth > 32 ? XImageFormat_RGBA64 : XImageFormat_ARGB32));
        if (original == XImageFormat_RGBA16FPx4_Premultiplied) intermediate = XImageFormat_RGBA16FPx4;
        else if (original == XImageFormat_RGBA32FPx4_Premultiplied) intermediate = XImageFormat_RGBA32FPx4;
        else if (original == XImageFormat_ARGB32_Premultiplied || original == XImageFormat_A2RGB30_Premultiplied || original == XImageFormat_A2BGR30_Premultiplied ||
                 original == XImageFormat_ARGB8565_Premultiplied || original == XImageFormat_ARGB6666_Premultiplied || original == XImageFormat_ARGB8555_Premultiplied || original == XImageFormat_ARGB4444_Premultiplied)
            intermediate = XImageFormat_ARGB32;
        XImage temp;
        XImage_init(&temp);
        XImage_convertToFormat(self, intermediate, 0, &temp);
        if (!XImage_isNull(&temp))
        {
            XImage_invertPixels(&temp, mode);
            XImage_convertToFormat(&temp, original, 0, self);
            XImage_deinit_base(&temp);
            return;
        }
        XImage_deinit_base(&temp);
    }
    if (depth < 32)
    {
        int bytes = (depth + 7) / 8;
        if (depth == 1) bytes = (self->m_data->m_width + 7) / 8;
        else if (bytes <= 0) bytes = 1;
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            int count = depth == 1 ? bytes : self->m_data->m_width * bytes;
            for (int i = 0; i < count; ++i) line[i] ^= 0xffu;
        }
    }
    else if (depth == 64)
    {
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            for (int x = 0; x < self->m_data->m_width; ++x)
            {
                for (int c = 0; c < 3; ++c) XImage_store16(line + x * 8 + c * 2, (uint16_t)~XImage_load16(line + x * 8 + c * 2));
                if (mode == XImageInvertMode_InvertRgba) XImage_store16(line + x * 8 + 6, (uint16_t)~XImage_load16(line + x * 8 + 6));
            }
        }
    }
    else if (depth == 128)
    {
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            for (int x = 0; x < self->m_data->m_width; ++x)
            {
                float value;
                for (int c = 0; c < 3; ++c) { memcpy(&value, line + x * 16 + c * 4, sizeof(value)); value = 1.0f - value; memcpy(line + x * 16 + c * 4, &value, sizeof(value)); }
                if (mode == XImageInvertMode_InvertRgba) { memcpy(&value, line + x * 16 + 12, sizeof(value)); value = 1.0f - value; memcpy(line + x * 16 + 12, &value, sizeof(value)); }
            }
        }
    }
    else
    {
        uint32_t xorbits = mode == XImageInvertMode_InvertRgba ? 0xffffffffu : 0x00ffffffu;
        if (self->m_data->m_format == XImageFormat_RGBX8888) xorbits = 0x00ffffffu;
        if (self->m_data->m_format == XImageFormat_RGBA8888 && mode == XImageInvertMode_InvertRgb) xorbits = 0x00ffffffu;
        if (self->m_data->m_format == XImageFormat_RGB30 || self->m_data->m_format == XImageFormat_BGR30) xorbits = 0x3fffffffu;
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            for (int x = 0; x < self->m_data->m_width; ++x)
            {
                uint32_t value = XImage_load32(line + x * 4);
                XImage_store32(line + x * 4, value ^ xorbits);
            }
        }
    }
    XImageData_markDirty(self->m_data);
}
