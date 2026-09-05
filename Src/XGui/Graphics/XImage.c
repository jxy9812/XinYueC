/******************************************************************************
 * @file       XImage.c
 * @brief      XImage 图像类实现（对标 Qt 6.8 QImage）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XImageCodec.h"
#include "XImageCodecInternal.h"
#include "XImageFormat.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XStringList.h"
#include "XVariant.h"
#include "XFile.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

/* ========== 私有数据结构 ========== */

/*
 * ICC/LUT 数据不能放进按值复制的 XColorSpace。这个不可变侧车只由
 * XImageData 持有，所有权由引用计数管理；外部接口只接受输入副本或
 * 写出副本，避免把资源生命周期暴露为裸指针。
 */
#define XIMAGE_PROFILE_MAX_ICC_BYTES (16u * 1024u * 1024u)
#define XIMAGE_PROFILE_MAX_LUT_ELEMENTS 65536u
/* 小型临时工作区直接放在调用栈；大图像再回退到 Hybrid。 */
#define XIMAGE_ALPHA_DITHER_STACK_WIDTH 64
#define XIMAGE_HEURISTIC_QUEUE_STACK_COUNT 256

typedef struct XImageColorProfileResource
{
    XAtomic_int32_t m_refCount;
    XByteArray* m_iccData;
    uint8_t* m_lutData[3];
    uint32_t m_lutElements[3];
    uint8_t m_lutBits[3];
    bool m_lutTwoWay[3];
} XImageColorProfileResource;

static void XImageColorProfileResource_unref(XImageColorProfileResource* resource);

static void XImageColorProfileResource_ref(XImageColorProfileResource* resource)
{
    if (resource)
        XAtomic_fetch_add_int32(&resource->m_refCount, 1,
                                XAtomic_MemoryOrder_SeqCst);
}

static bool XImageColorProfileResource_validateLut(const void* data,
                                                   uint32_t elements,
                                                   uint8_t bits,
                                                   bool twoWay)
{
    size_t bytesPerElement;
    uint32_t i;
    uint32_t previous = 0;
    if (elements == 0)
        return data == NULL && bits == 0;
    if (!data || elements < 2u || elements > XIMAGE_PROFILE_MAX_LUT_ELEMENTS)
        return false;
    if (bits != 8u && bits != 16u)
        return false;
    bytesPerElement = bits / 8u;
    if ((size_t)elements > SIZE_MAX / bytesPerElement ||
        (size_t)elements * bytesPerElement > XIMAGE_PROFILE_MAX_ICC_BYTES)
        return false;
    if (!twoWay)
        return true;
    /* Two-way tables must be monotonic so that inverse lookup is defined. */
    for (i = 0; i < elements; ++i)
    {
        uint32_t value;
        if (bits == 8u)
            value = ((const uint8_t*)data)[i];
        else
        {
            uint16_t sample;
            memcpy(&sample, (const uint8_t*)data + (size_t)i * 2u,
                   sizeof(sample));
            value = sample;
        }
        if (i != 0 && value < previous)
            return false;
        previous = value;
    }
    return true;
}

static XImageColorProfileResource* XImageColorProfileResource_create(
    const XImageColorProfileSpec* spec)
{
    XImageColorProfileResource* resource;
    int channel;
    bool hasLut = false;

    if (!spec || spec->m_iccSize > XIMAGE_PROFILE_MAX_ICC_BYTES ||
        (spec->m_iccSize != 0 && !spec->m_iccData))
        return NULL;
    for (channel = 0; channel < 3; ++channel)
    {
        if (!XImageColorProfileResource_validateLut(
                spec->m_lutData[channel], spec->m_lutElements[channel],
                spec->m_lutBits[channel], spec->m_lutTwoWay[channel]))
            return NULL;
        if (spec->m_lutElements[channel] != 0)
            hasLut = true;
    }
    if (spec->m_iccSize == 0 && !hasLut)
        return NULL;

    resource = (XImageColorProfileResource*)XMalloc_System(sizeof(*resource));
    if (!resource)
        return NULL;
    memset(resource, 0, sizeof(*resource));
    XAtomic_init(resource->m_refCount, 1);
    if (spec->m_iccSize != 0)
    {
        resource->m_iccData = XByteArray_create_with_data(
            (const char*)spec->m_iccData, spec->m_iccSize);
        if (!resource->m_iccData ||
            XByteArray_size_base((const XContainer*)resource->m_iccData) !=
                spec->m_iccSize)
        {
            XImageColorProfileResource_unref(resource);
            return NULL;
        }
    }
    for (channel = 0; channel < 3; ++channel)
    {
        size_t bytes;
        if (spec->m_lutElements[channel] == 0)
            continue;
        bytes = (size_t)spec->m_lutElements[channel] *
                (size_t)(spec->m_lutBits[channel] / 8u);
        resource->m_lutData[channel] = (uint8_t*)XMalloc_System(bytes);
        if (!resource->m_lutData[channel])
        {
            XImageColorProfileResource_unref(resource);
            return NULL;
        }
        memcpy(resource->m_lutData[channel], spec->m_lutData[channel], bytes);
        resource->m_lutElements[channel] = spec->m_lutElements[channel];
        resource->m_lutBits[channel] = spec->m_lutBits[channel];
        resource->m_lutTwoWay[channel] = spec->m_lutTwoWay[channel];
    }
    return resource;
}

static void XImageColorProfileResource_unref(XImageColorProfileResource* resource)
{
    int channel;
    if (!resource)
        return;
    if (XAtomic_fetch_add_int32(&resource->m_refCount, -1,
                                XAtomic_MemoryOrder_SeqCst) != 1)
        return;
    if (resource->m_iccData)
        XByteArray_delete_base((XClass*)resource->m_iccData);
    for (channel = 0; channel < 3; ++channel)
        XFree_System(resource->m_lutData[channel]);
    XFree_System(resource);
}

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
    XColorSpace      m_colorSpace;       /**< 色彩空间描述 */
    XImageColorProfileResource* m_colorProfile; /**< ICC/LUT 不可变资源侧车 */
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
    XStringList      m_textKeys;         /**< 文本元数据键列表 */
    XStringList      m_textValues;       /**< 文本元数据值列表 */
    XString          m_textAll;          /**< 空键聚合文本缓存（对标 text("") 的稳定返回） */
}XImageData;

static XAtomic_uint32_t g_imageSerialCounter;  /**< 全局序列号计数器 */

static uint8_t XImage_luma(uint32_t color);
static uint32_t XImage_readPixelValue(const XImageData* d, int x, int y);
static void XImage_writePixelValue(XImageData* d, int x, int y, uint32_t color);
static void XImage_writePixelIndex(XImageData* d, int x, int y, uint32_t indexOrRgb);
static uint16_t XImage_load16(const uint8_t* p);
static void XImage_store16(uint8_t* p, uint16_t value);
static uint32_t XImage_load32(const uint8_t* p);
static void XImage_store32(uint8_t* p, uint32_t value);
static uint16_t XImage_floatToHalf(float value);
static float XImage_halfToFloat(uint16_t value);
static uint16_t XImage_unpremultiply16(uint16_t value, uint16_t alpha);
static uint16_t XImage_premultiply16(uint16_t value, uint16_t alpha);
static void XImage_writePixelColor16(XImageData* d, int x, int y,
                                     uint16_t red, uint16_t green,
                                     uint16_t blue, uint16_t alpha);
static XColor XImage_colorFrom16(uint16_t red, uint16_t green,
                                 uint16_t blue, uint16_t alpha);
static void XImage_detachMetadata(XImage* self);
bool XImage_allGray(const XImage* self);

/**
 * @brief 获取嵌入式图像对象使用的默认水平/垂直分辨率。
 * @return 按 Qt 6.8 默认 96 DPI 换算得到的每米点数。
 * @note Qt 在 QImageData 构造时以 qt_defaultDpiX/Y() * 100 / 2.54
 *       初始化 dpmx/dpmy；XScreen 的默认逻辑 DPI 同样为 96，因此在
 *       不暴露平台 DPI 查询的嵌入式实现中使用对应的固定换算值。
 */
static int XImage_defaultDotsPerMeter(void)
{
    return (int)(96.0f * 100.0f / 2.54f + 0.5f);
}

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

static void XImageData_clearText(XImageData* d)
{
    if (!d) return;
    XStringList_clear_base((XContainer*)&d->m_textKeys);
    XStringList_clear_base((XContainer*)&d->m_textValues);
    XString_clear_base((XContainer*)&d->m_textAll);
}

static void XImageData_clearTextAll(XImageData* d)
{
    if (d)
        XString_clear_base((XContainer*)&d->m_textAll);
}

/**
 * @brief 清除图像数据关联的 ICC/LUT 资源。
 * @param d 图像私有数据；允许为空。
 * @note 资源使用独立引用计数管理，清除只影响当前数据对象。
 */
static void XImageData_clearColorProfile(XImageData* d)
{
    if (!d || !d->m_colorProfile)
        return;
    XImageColorProfileResource_unref(d->m_colorProfile);
    d->m_colorProfile = NULL;
}

/**
 * @brief 反初始化图像文本元数据。
 * @param d 待反初始化的图像数据对象。
 * @note XStringList 的 clear 仅改变元素数量；最终释放前必须逐项反初始化
 *       嵌入的 XString，才能释放每个字符串的共享字符缓冲和 UTF-8 缓存。
 */
static void XImageData_deinitText(XImageData* d)
{
    size_t count;
    size_t i;
    if (!d)
        return;

    /* 文本列表采用隐式共享；先分离，避免销毁当前图像时修改其他副本。 */
    XVector_detach((XVector*)&d->m_textKeys);
    XVector_detach((XVector*)&d->m_textValues);
    count = XStringList_size_base((const XContainer*)&d->m_textKeys);
    for (i = 0; i < count; ++i)
    {
        XString* item = (XString*)XStringList_at_base(
            (XVector*)&d->m_textKeys, (int64_t)i);
        if (item)
            XString_deinit_base((XClass*)item);
    }
    XContainerSize((XContainer*)&d->m_textKeys) = 0;
    count = XStringList_size_base((const XContainer*)&d->m_textValues);
    for (i = 0; i < count; ++i)
    {
        XString* item = (XString*)XStringList_at_base(
            (XVector*)&d->m_textValues, (int64_t)i);
        if (item)
            XString_deinit_base((XClass*)item);
    }
    XContainerSize((XContainer*)&d->m_textValues) = 0;

    XStringList_deinit_base((XClass*)&d->m_textKeys);
    XStringList_deinit_base((XClass*)&d->m_textValues);
    XString_deinit_base((XClass*)&d->m_textAll);
}

/**
 * @brief 复制不随像素格式转换丢失的图像元数据。
 * @note 对标 Qt 6.8 的 copyMetadata 语义：变换不复制颜色表，
 *       但保留物理分辨率、设备像素比、偏移、文本元数据和色彩空间。
 */
static void XImageData_copyMetadata(XImageData* dst, const XImageData* src)
{
    int count;
    if (!dst || !src) return;
    dst->m_dpmX = src->m_dpmX;
    dst->m_dpmY = src->m_dpmY;
    dst->m_devicePixelRatio = src->m_devicePixelRatio;
    dst->m_offsetX = src->m_offsetX;
    dst->m_offsetY = src->m_offsetY;
    dst->m_colorSpace = src->m_colorSpace;
    if (dst != src && dst->m_colorProfile)
        XImageColorProfileResource_unref(dst->m_colorProfile);
    if (dst != src)
        dst->m_colorProfile = src->m_colorProfile;
    if (dst != src && dst->m_colorProfile)
        XImageColorProfileResource_ref(dst->m_colorProfile);
    count = (int)XStringList_size_base((const XContainer*)&src->m_textKeys);
    for (int i = 0; i < count; ++i)
    {
        const XString* item = (const XString*)XStringList_at_base(
            (const XVector*)&src->m_textKeys, i);
        if (!item)
            continue;
        XStringList_push_back_base((XVector*)&dst->m_textKeys, (void*)item);
        item = (const XString*)XStringList_at_base(
            (const XVector*)&src->m_textValues, i);
        if (item)
            XStringList_push_back_base((XVector*)&dst->m_textValues, (void*)item);
    }
}

/**
 * @brief 聚合全部文本元数据（对标 QImage::text("")）。
 * @note 按键排序（等价 QMap<QString, QString>）；每个条目格式为 "key: value\n\n"，
 *       结尾多余的 "\n\n" 会被去掉，value 按 Qt 语义经过 simplified。
 */
static XString* XImageData_buildAllText(const XImageData* d)
{
    XString* result;
    int count;
    const char* emptyUtf8 = "";
    if (!d) return XString_create_utf8("");
    result = XString_create_utf8("");
    if (!result) return NULL;
    count = (int)XStringList_size_base((const XContainer*)&d->m_textKeys);
    for (int i = 0; i < count; ++i)
    {
        const XString* key = (const XString*)XStringList_at_base(
            (const XVector*)&d->m_textKeys, i);
        const XString* value = (const XString*)XStringList_at_base(
            (const XVector*)&d->m_textValues, i);
        const char* keyUtf8;
        XString* simplified;
        const char* valueUtf8;
        if (!key || !value) continue;
        keyUtf8 = XString_isEmpty_base((const XContainer*)key)
                  ? emptyUtf8
                  : XString_toUtf8(key);
        simplified = XString_simplified(value);
        if (!simplified) continue;
        valueUtf8 = XString_isEmpty_base((const XContainer*)simplified)
                    ? emptyUtf8
                    : XString_toUtf8(simplified);
        if (XString_isEmpty_base((const XContainer*)key) == false && !keyUtf8)
        {
            XString_delete_base((XClass*)simplified);
            XString_delete_base((XClass*)result);
            return NULL;
        }
        if (XString_isEmpty_base((const XContainer*)simplified) == false && !valueUtf8)
        {
            XString_delete_base((XClass*)simplified);
            XString_delete_base((XClass*)result);
            return NULL;
        }
        if ((XString_isEmpty_base((const XContainer*)key) ||
             XString_append_utf8(result, keyUtf8)) &&
            XString_append_utf8(result, ": ") &&
            (XString_isEmpty_base((const XContainer*)simplified) ||
             XString_append_utf8(result, valueUtf8)) &&
            XString_append_utf8(result, "\n\n"))
        {
            /* 每个条目成功追加。 */
        }
        else
        {
            XString_delete_base((XClass*)simplified);
            XString_delete_base((XClass*)result);
            return NULL;
        }
        XString_delete_base((XClass*)simplified);
    }
    {
        size_t length = XString_length_base((const XContainer*)result);
        const uint16_t* data = XString_utf16(result);
        if (length >= 2 && data &&
            data[length - 1] == (uint16_t)0x000a &&
            data[length - 2] == (uint16_t)0x000a)
            XString_truncate(result, length - 2);
    }
    return result;
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
    XStringList_init(&d->m_textKeys);
    XStringList_init(&d->m_textValues);
    XString_init(&d->m_textAll);

    XAtomic_init(d->m_refCount, 1);
    d->m_width = width;
    d->m_height = height;
    d->m_format = format;
    d->m_colorSpace = XColorSpace_create();
    d->m_depth = depth;
    /* 对齐 Qt qimage.cpp:92-100 的 QImageData 默认 dpmx/dpmy。 */
    d->m_dpmX = XImage_defaultDotsPerMeter();
    d->m_dpmY = XImage_defaultDotsPerMeter();
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
            XImageData_deinitText(d);
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
        XImageColorProfileResource_unref(d->m_colorProfile);
        XImageData_deinitText(d);
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
    XImageData_copyMetadata(copy, source);
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
    XVTABLE_INIT_DEFAULT(XImage)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXImage_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXImage_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXImage_deinit);
    return XVTABLE_DEFAULT;
}

XImage* XImage_create_ex(XMemoryType memory)
{
    XImage* self = (XImage*)XMemory_malloc(sizeof(XImage), memory);
    if (!self) return NULL;
    XImage_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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

bool XImage_reinit_ex(XImage* self, int width, int height, XImageFormat format)
{
    XImage replacement;
    if (!self) return false;
    /* XImage_init_ex 只用于首次初始化这个栈上临时对象，绝不覆盖
       已存在目标的 XClass 元数据或 m_data 所有权。 */
    XImage_init_ex(&replacement, width, height, format);
    if (XImage_isNull(&replacement)) {
        XImage_deinit_base(&replacement);
        return false;
    }
    XMove(self, &replacement);
    XImage_deinit_base(&replacement);
    return true;
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

void XImage_init_file_2(XImage* self, const char* fileName, const char* format)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImage_init_file(self, fileNameString, formatString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImage_init_file(XImage* self, const XString* fileName, const XString* format)
{
    XImage_init(self);
    XImage_load(self, fileName, format);
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

bool XImage_equals(const XImage* left, const XImage* right)
{
    int width;
    int height;
    int depth;
    int y;
    /* Qt first accepts the same object or the same implicitly-shared data. */
    if (left == right || (left && right && left->m_data == right->m_data))
        return true;
    if (!left || !right)
        return false;
    /* XImage_init leaves m_data null; two independent empty images therefore
       have the same null contents, matching Qt's shared null QImageData. */
    if (!left->m_data || !right->m_data)
        return left->m_data == right->m_data;
    if (!left->m_data->m_data || !right->m_data->m_data)
        return false;
    if (left->m_data->m_width != right->m_data->m_width ||
        left->m_data->m_height != right->m_data->m_height ||
        left->m_data->m_format != right->m_data->m_format ||
        !XColorSpace_equals(&left->m_data->m_colorSpace,
                            &right->m_data->m_colorSpace))
        return false;

    width = left->m_data->m_width;
    height = left->m_data->m_height;
    /* Qt qimage.cpp:4013-4030 compares indexed/monochrome images after
       resolving each index through its color table, so different index
       numbers can still represent equal image contents. */
    if (left->m_data->m_format < XImageFormat_ARGB32)
    {
        int x;
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
                if (XImage_readPixelValue(left->m_data, x, y) !=
                    XImage_readPixelValue(right->m_data, x, y))
                    return false;
        return true;
    }

    /* QImage::operator== has a dedicated RGB32 path (qimage.cpp:4041-4051)
       because its alpha byte is explicitly undefined. */
    if (left->m_data->m_format == XImageFormat_RGB32)
    {
        int x;
        for (y = 0; y < height; ++y)
        {
            const uint8_t* leftLine = left->m_data->m_data +
                (size_t)y * (size_t)left->m_data->m_bytesPerLine;
            const uint8_t* rightLine = right->m_data->m_data +
                (size_t)y * (size_t)right->m_data->m_bytesPerLine;
            for (x = 0; x < width; ++x)
                if ((XImage_load32(leftLine + (size_t)x * 4u) & 0x00ffffffu) !=
                    (XImage_load32(rightLine + (size_t)x * 4u) & 0x00ffffffu))
                    return false;
        }
        return true;
    }

    /* For formats with fully-defined storage, Qt compares only the bytes
       belonging to each pixel and ignores row padding when strides differ
       (qimage.cpp:4013-4040). */
    depth = XImageFormat_bitDepth(left->m_data->m_format);
    if (depth <= 0)
        return false;
    {
        const size_t rowBytes = ((size_t)width * (size_t)depth) / 8u;
        for (y = 0; y < height; ++y)
        {
            const uint8_t* leftLine = left->m_data->m_data +
                (size_t)y * (size_t)left->m_data->m_bytesPerLine;
            const uint8_t* rightLine = right->m_data->m_data +
                (size_t)y * (size_t)right->m_data->m_bytesPerLine;
            if (memcmp(leftLine, rightLine, rowBytes) != 0)
                return false;
        }
    }
    return true;
}

bool XImage_notEquals(const XImage* left, const XImage* right)
{
    return !XImage_equals(left, right);
}

/**
 * @brief 交换两幅图像的数据指针。
 * @details Qt 的 QImage::swap 只交换隐式共享数据指针；不能调用移动操作，
 *          因为移动会把右侧对象清空而不是完成双向交换。保留各自 XClass
 *          元数据也使堆对象和栈对象可以安全混用。
 */
void XImage_swap(XImage* left, XImage* right)
{
    XImageData* data;
    if (!left || !right || left == right)
        return;
    data = left->m_data;
    left->m_data = right->m_data;
    right->m_data = data;
}

int XImage_depth(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_depth : 0;
}

int XImage_bitPlaneCount(const XImage* self)
{
    XImageFormat format = XImage_format(self);
    switch (format)
    {
        case XImageFormat_RGB32:
        case XImageFormat_RGBX8888:
            return 24;
        case XImageFormat_RGBX64:
        case XImageFormat_RGBX16FPx4:
            return 48;
        case XImageFormat_RGBX32FPx4:
            return 96;
        case XImageFormat_BGR30:
        case XImageFormat_RGB30:
            return 30;
        default:
            return XImage_depth(self);
    }
}

XImageFormat XImage_format(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_format : XImageFormat_Invalid;
}

XPixelFormat XImage_pixelFormat(const XImage* self)
{
    return XImageFormat_pixelFormat(XImage_format(self));
}

XColorSpace XImage_colorSpace(const XImage* self)
{
    if (self && self->m_data)
        return self->m_data->m_colorSpace;
    return XColorSpace_create();
}

/*
 * QImage 只允许给当前像素模型可表达的图像附加有效色彩空间。
 * 本实现支持的 XColorSpace 均为 RGB 原色模型，因此 RGB/BGR、索引、
 * 单色和灰度格式都可使用；Alpha8 不含颜色，按 Qt 规则对任意空间兼容；
 * CMYK 与未知模型必须拒绝。目标色彩空间无效时由调用者直接放行。
 */
static XColorSpaceColorModel XImage_pixelColorModel(const XImage* self)
{
    const XImageFormat format = XImage_format(self);
    const XPixelFormat pixel = XImageFormat_pixelFormat(format);
    if (format == XImageFormat_Alpha8)
        return XColorSpaceModel_Undefined;
    switch (pixel.m_model)
    {
        case XPixelFormatModel_Gray:
            return XColorSpaceModel_Gray;
        case XPixelFormatModel_Indexed:
        case XPixelFormatModel_RGB:
        case XPixelFormatModel_BGR:
            return XColorSpaceModel_Rgb;
        case XPixelFormatModel_CMYK:
            return XColorSpaceModel_Cmyk;
        default:
            return XColorSpaceModel_Undefined;
    }
}

static bool XImage_colorSpaceSourceCompatibleModel(
    XColorSpaceColorModel dataModel, XColorSpaceColorModel targetModel)
{
    if (dataModel == XColorSpaceModel_Undefined)
        return true; /* Alpha 数据不含颜色，可附加任意有效空间。 */
    if (targetModel == XColorSpaceModel_Undefined)
        return false;
    if (targetModel == dataModel)
        return true;
    return dataModel == XColorSpaceModel_Gray &&
           targetModel == XColorSpaceModel_Rgb;
}

static bool XImage_colorSpaceTargetCompatibleModel(
    XColorSpaceColorModel dataModel, const XColorSpace* colorSpace)
{
    const XColorSpaceColorModel targetModel = XColorSpace_colorModel(colorSpace);
    if (dataModel == XColorSpaceModel_Undefined)
        return true;
    if (targetModel == XColorSpaceModel_Undefined)
        return false;
    if (targetModel == dataModel)
        return true;
    return dataModel == XColorSpaceModel_Gray &&
           XColorSpace_transformModel(colorSpace) ==
               XColorSpaceTransform_ThreeComponentMatrix;
}

static bool XImage_colorSpaceCompatible(const XImage* self,
                                         const XColorSpace* colorSpace)
{
    const XColorSpaceColorModel dataModel = XImage_pixelColorModel(self);
    const XColorSpaceColorModel targetModel = XColorSpace_colorModel(colorSpace);
    return XImage_colorSpaceSourceCompatibleModel(dataModel, targetModel);
}

/* 对齐 Qt qt_compatibleColorModelTarget：灰度数据可通过三分量矩阵
   转换到任意带明确颜色模型的目标空间，不限于 RGB。 */
static bool XImage_colorSpaceTargetCompatible(const XImage* self,
                                               const XColorSpace* colorSpace)
{
    const XColorSpaceColorModel dataModel = XImage_pixelColorModel(self);
    return XImage_colorSpaceTargetCompatibleModel(dataModel, colorSpace);
}

/* 对标 Qt 6.8 toPixelFormat(format).colorModel() 的目标格式检查。 */
static bool XImage_colorSpaceTargetCompatibleFormat(XImageFormat format,
                                                     const XColorSpace* colorSpace)
{
    XPixelFormat pixel;
    XColorSpaceColorModel model;
    if (format <= XImageFormat_Invalid ||
        format >= XImageFormat_NImageFormats || !colorSpace)
        return false;
    /* 先验证枚举范围，再查格式表；Qt 的 Invalid/越界格式必须直接
       返回失败，不能让不可信调用者触发格式表越界读取。 */
    pixel = XImageFormat_toPixelFormat(format);
    switch (pixel.m_model)
    {
        case XPixelFormatModel_Gray: model = XColorSpaceModel_Gray; break;
        case XPixelFormatModel_CMYK: model = XColorSpaceModel_Cmyk; break;
        case XPixelFormatModel_Alpha: model = XColorSpaceModel_Undefined; break;
        case XPixelFormatModel_Indexed:
        case XPixelFormatModel_RGB:
        case XPixelFormatModel_BGR: model = XColorSpaceModel_Rgb; break;
        default: model = XColorSpaceModel_Undefined; break;
    }
    return XImage_colorSpaceTargetCompatibleModel(model, colorSpace);
}

void XImage_setColorSpace(XImage* self, XColorSpace colorSpace)
{
    if (!self || !self->m_data) return;
    if (XColorSpace_equals(&self->m_data->m_colorSpace, &colorSpace)) return;
    if (XColorSpace_isValid(&colorSpace) &&
        !XImage_colorSpaceCompatible(self, &colorSpace)) return;
    /* Qt 的 setColorSpace() 只分离元数据；唯一数据的 cacheKey 不变。 */
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
    XImageData_clearColorProfile(self->m_data);
    self->m_data->m_colorSpace = colorSpace;
}

bool XImage_hasColorSpace(const XImage* self)
{
    return self && self->m_data && XColorSpace_isValid(&self->m_data->m_colorSpace);
}

bool XImageCodecInternal_setColorProfile(XImage* self,
                                         const XImageColorProfileSpec* spec)
{
    XImageColorProfileResource* replacement = NULL;
    bool hasData = false;
    bool malformedEmpty = false;
    int channel;
    if (!self || !self->m_data)
        return false;
    if (spec)
    {
        if (spec->m_iccSize != 0)
            hasData = true;
        else if (spec->m_iccData)
            malformedEmpty = true;
        for (channel = 0; channel < 3; ++channel)
        {
            hasData = hasData || spec->m_lutElements[channel] != 0;
            if (spec->m_lutElements[channel] == 0 &&
                (spec->m_lutData[channel] || spec->m_lutBits[channel] != 0 ||
                 spec->m_lutTwoWay[channel]))
                malformedEmpty = true;
        }
        if (malformedEmpty)
            return false;
    }
    if (hasData)
    {
        replacement = XImageColorProfileResource_create(spec);
        if (!replacement)
            return false;
    }
    /* Detach before replacing metadata so aliases retain their resource. */
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self))
    {
        XImageColorProfileResource_unref(replacement);
        return false;
    }
    XImageData_clearColorProfile(self->m_data);
    self->m_data->m_colorProfile = replacement;
    return true;
}

bool XImageCodecInternal_copyIccProfile(const XImage* self, XByteArray* out)
{
    size_t size = 0;
    if (!self || !self->m_data || !out)
        return false;
    if (self->m_data->m_colorProfile && self->m_data->m_colorProfile->m_iccData)
        size = XByteArray_size_base((const XContainer*)
                                    self->m_data->m_colorProfile->m_iccData);
    if (!XByteArray_resize_base((XVector*)out, size))
        return false;
    if (size)
        memcpy(XByteArray_data(out),
               XByteArray_data(self->m_data->m_colorProfile->m_iccData),
               size);
    return true;
}

bool XImageCodecInternal_copyLut(const XImage* self, int channel,
                                 void* out, size_t outBytes,
                                 uint32_t* elements, uint8_t* bits,
                                 bool* twoWay)
{
    const XImageColorProfileResource* resource;
    size_t bytes;
    if (!self || !self->m_data || channel < 0 || channel >= 3)
        return false;
    resource = self->m_data->m_colorProfile;
    if (!resource || resource->m_lutElements[channel] == 0)
    {
        if (elements) *elements = 0;
        if (bits) *bits = 0;
        if (twoWay) *twoWay = false;
        return true;
    }
    if (elements) *elements = resource->m_lutElements[channel];
    if (bits) *bits = resource->m_lutBits[channel];
    if (twoWay) *twoWay = resource->m_lutTwoWay[channel];
    bytes = (size_t)resource->m_lutElements[channel] *
            (size_t)(resource->m_lutBits[channel] / 8u);
    if (!out || outBytes < bytes)
        return false;
    memcpy(out, resource->m_lutData[channel], bytes);
    return true;
}

static float XImage_decodeTransfer(float value, XColorSpaceTransferFunction transfer,
                                   float gamma)
{
    /* QColorTransferFunction keeps the extended-range result of HDR curves;
       in particular PQ(1) is 64 and HLG(1) is 12.  Clamping the input at
       one would silently turn those transfers into identity at the white
       point.  Zero is handled explicitly because the PQ expression has a
       negative denominator at its mathematical endpoint. */
    if (value <= 0.0f) return 0.0f;
    switch (transfer)
    {
        case XColorSpaceTransfer_Linear: return value;
        case XColorSpaceTransfer_Gamma:
            return powf(value, gamma > 0.0f ? gamma : 1.0f);
        case XColorSpaceTransfer_SRgb:
            return value < 0.04045f ? value / 12.92f : powf((value + 0.055f) / 1.055f, 2.4f);
        case XColorSpaceTransfer_ProPhotoRgb:
            return value < (16.0f / 512.0f) ? value / 16.0f : powf(value, 1.8f);
        case XColorSpaceTransfer_Bt2020:
            return value < 0.08145f ? value / 4.5f :
                powf((value + 0.0993f) / 1.0993f, 2.2f);
        case XColorSpaceTransfer_St2084:
        {
            const float c1 = 107.0f / 128.0f;
            const float c2 = 2413.0f / 128.0f;
            const float c3 = 2392.0f / 128.0f;
            const float m1 = 1305.0f / 8192.0f;
            const float m2 = 2523.0f / 32.0f;
            const float powered = powf(value, 1.0f / m2);
            /* This is the Qt 6.8 qcolortransfergeneric_p.h expression.
               It intentionally follows Qt's endpoint behavior. */
            return powf((c1 - powered) / (c3 * powered - c2),
                        1.0f / m1) * 64.0f;
        }
        case XColorSpaceTransfer_Hlg:
        {
            const float a = 0.17883277f;
            const float b = 1.0f - 4.0f * a;
            const float c = 0.55991073f;
            return value < 0.5f ? value * value * 4.0f :
                expf((value - c) / a) + b;
        }
        case XColorSpaceTransfer_Gamma22:
        case XColorSpaceTransfer_Gamma28:
            return powf(value, gamma > 0.0f ? gamma :
                        (transfer == XColorSpaceTransfer_Gamma28 ? 2.8f : 2.2f));
        default:
            return value;
    }
}

static float XImage_encodeTransfer(float value, XColorSpaceTransferFunction transfer,
                                   float gamma)
{
    /* Keep extended linear luminance for HDR destinations.  The final pixel
       writer performs the format-specific clamp/quantization where needed. */
    if (value <= 0.0f) return 0.0f;
    switch (transfer)
    {
        case XColorSpaceTransfer_Linear: return value;
        case XColorSpaceTransfer_Gamma:
            return powf(value, 1.0f / (gamma > 0.0f ? gamma : 1.0f));
        case XColorSpaceTransfer_SRgb:
            return value < 0.0031308f ? value * 12.92f : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
        case XColorSpaceTransfer_ProPhotoRgb:
            return value < (1.0f / 512.0f) ? value * 16.0f : powf(value, 1.0f / 1.8f);
        case XColorSpaceTransfer_Bt2020:
            return value < 0.0181f ? value * 4.5f :
                1.0993f * powf(value, 1.0f / 2.2f) - 0.0993f;
        case XColorSpaceTransfer_St2084:
        {
            const float c1 = 107.0f / 128.0f;
            const float c2 = 2413.0f / 128.0f;
            const float c3 = 2392.0f / 128.0f;
            const float m1 = 1305.0f / 8192.0f;
            const float m2 = 2523.0f / 32.0f;
            const float powered = powf(value * (1.0f / 64.0f), m1);
            return powf((c1 + c2 * powered) / (1.0f + c3 * powered), m2);
        }
        case XColorSpaceTransfer_Hlg:
        {
            const float a = 0.17883277f;
            const float b = 1.0f - 4.0f * a;
            const float c = 0.55991073f;
            return value > 1.0f ? a * logf(value - b) + c :
                sqrtf(value * 0.25f);
        }
        case XColorSpaceTransfer_Gamma22:
        case XColorSpaceTransfer_Gamma28:
            return powf(value, 1.0f / (gamma > 0.0f ? gamma :
                        (transfer == XColorSpaceTransfer_Gamma28 ? 2.8f : 2.2f)));
        default:
            return value;
    }
}

static uint8_t XImage_colorSpaceChannel(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

/*
 * Qt QColorSpace 将每个 RGB 原色的 xy 色度转换为 XYZ 列向量，再用白点
 * 反解出三个缩放因子（qcolorspace.cpp:87-103）。XColorSpace 没有引入
 * Qt 的私有 QColorMatrix，而是在这里按相同的行主序矩阵临时计算，避免
 * 把 GUI 私有实现细节暴露到公共 C99 结构体中。
 */
static void XImage_matrixMap(const float matrix[9], const float input[3],
                             float output[3]);

static void XImage_matrixMultiply(const float left[9], const float right[9],
                                  float output[9])
{
    int row, column;
    for (row = 0; row < 3; ++row)
        for (column = 0; column < 3; ++column)
        {
            float value = 0.0f;
            int index;
            for (index = 0; index < 3; ++index)
                value += left[row * 3 + index] * right[index * 3 + column];
            output[row * 3 + column] = value;
        }
}

static bool XImage_colorSpacePrimariesMatrix(const XColorSpace* colorSpace,
                                             float matrix[9])
{
    XColorSpacePrimariesData primaries;
    float red[3], green[3], blue[3], white[3], inverse[9];
    float scale[3];
    if (!colorSpace || XColorSpace_colorModel(colorSpace) != XColorSpaceModel_Rgb ||
        !XColorSpace_primariesData(colorSpace, &primaries))
        return false;
    if (!(primaries.m_redPoint.y > 0.0f) ||
        !(primaries.m_greenPoint.y > 0.0f) ||
        !(primaries.m_bluePoint.y > 0.0f) ||
        !(primaries.m_whitePoint.y > 0.0f))
        return false;
    red[0] = primaries.m_redPoint.x / primaries.m_redPoint.y;
    red[1] = 1.0f;
    red[2] = (1.0f - primaries.m_redPoint.x - primaries.m_redPoint.y) /
             primaries.m_redPoint.y;
    green[0] = primaries.m_greenPoint.x / primaries.m_greenPoint.y;
    green[1] = 1.0f;
    green[2] = (1.0f - primaries.m_greenPoint.x - primaries.m_greenPoint.y) /
              primaries.m_greenPoint.y;
    blue[0] = primaries.m_bluePoint.x / primaries.m_bluePoint.y;
    blue[1] = 1.0f;
    blue[2] = (1.0f - primaries.m_bluePoint.x - primaries.m_bluePoint.y) /
             primaries.m_bluePoint.y;
    white[0] = primaries.m_whitePoint.x / primaries.m_whitePoint.y;
    white[1] = 1.0f;
    white[2] = (1.0f - primaries.m_whitePoint.x - primaries.m_whitePoint.y) /
               primaries.m_whitePoint.y;
    matrix[0] = red[0]; matrix[1] = green[0]; matrix[2] = blue[0];
    matrix[3] = red[1]; matrix[4] = green[1]; matrix[5] = blue[1];
    matrix[6] = red[2]; matrix[7] = green[2]; matrix[8] = blue[2];
    {
        const float determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
                                  matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
                                  matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
        if (!isfinite((double)determinant) || fabsf(determinant) < 1.0e-8f)
            return false;
        inverse[0] = (matrix[4] * matrix[8] - matrix[5] * matrix[7]) / determinant;
        inverse[1] = (matrix[2] * matrix[7] - matrix[1] * matrix[8]) / determinant;
        inverse[2] = (matrix[1] * matrix[5] - matrix[2] * matrix[4]) / determinant;
        inverse[3] = (matrix[5] * matrix[6] - matrix[3] * matrix[8]) / determinant;
        inverse[4] = (matrix[0] * matrix[8] - matrix[2] * matrix[6]) / determinant;
        inverse[5] = (matrix[2] * matrix[3] - matrix[0] * matrix[5]) / determinant;
        inverse[6] = (matrix[3] * matrix[7] - matrix[4] * matrix[6]) / determinant;
        inverse[7] = (matrix[1] * matrix[6] - matrix[0] * matrix[7]) / determinant;
        inverse[8] = (matrix[0] * matrix[4] - matrix[1] * matrix[3]) / determinant;
    }
    scale[0] = inverse[0] * white[0] + inverse[1] * white[1] + inverse[2] * white[2];
    scale[1] = inverse[3] * white[0] + inverse[4] * white[1] + inverse[5] * white[2];
    scale[2] = inverse[6] * white[0] + inverse[7] * white[1] + inverse[8] * white[2];
    if (!isfinite((double)scale[0]) || !isfinite((double)scale[1]) ||
        !isfinite((double)scale[2]))
        return false;
    matrix[0] *= scale[0]; matrix[1] *= scale[1]; matrix[2] *= scale[2];
    matrix[3] *= scale[0]; matrix[4] *= scale[1]; matrix[5] *= scale[2];
    matrix[6] *= scale[0]; matrix[7] *= scale[1]; matrix[8] *= scale[2];
    return true;
}

static bool XImage_matrixInverse(const float matrix[9], float inverse[9])
{
    const float determinant = matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
                              matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
                              matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    if (!isfinite((double)determinant) || fabsf(determinant) < 1.0e-8f)
        return false;
    inverse[0] = (matrix[4] * matrix[8] - matrix[5] * matrix[7]) / determinant;
    inverse[1] = (matrix[2] * matrix[7] - matrix[1] * matrix[8]) / determinant;
    inverse[2] = (matrix[1] * matrix[5] - matrix[2] * matrix[4]) / determinant;
    inverse[3] = (matrix[5] * matrix[6] - matrix[3] * matrix[8]) / determinant;
    inverse[4] = (matrix[0] * matrix[8] - matrix[2] * matrix[6]) / determinant;
    inverse[5] = (matrix[2] * matrix[3] - matrix[0] * matrix[5]) / determinant;
    inverse[6] = (matrix[3] * matrix[7] - matrix[4] * matrix[6]) / determinant;
    inverse[7] = (matrix[1] * matrix[6] - matrix[0] * matrix[7]) / determinant;
    inverse[8] = (matrix[0] * matrix[4] - matrix[1] * matrix[3]) / determinant;
    return true;
}

static void XImage_matrixMap(const float matrix[9], const float input[3],
                             float output[3])
{
    output[0] = matrix[0] * input[0] + matrix[1] * input[1] + matrix[2] * input[2];
    output[1] = matrix[3] * input[0] + matrix[4] * input[1] + matrix[5] * input[2];
    output[2] = matrix[6] * input[0] + matrix[7] * input[1] + matrix[8] * input[2];
}

/* 对齐 QColorMatrix::chromaticAdaptation()：Bradford 将任意白点映射到
 * D50，供 source/target 矩阵在同一个 PCS 中组合。 */
static bool XImage_chromaticAdaptationToD50(XPointF whitePoint, float matrix[9])
{
    static const float bradford[9] = {
        0.8951f, -0.7502f, 0.0389f, 0.2664f, 1.7135f, -0.0685f,
        -0.1614f, 0.0367f, 1.0296f
    };
    static const float bradfordInverse[9] = {
        0.9869929f, 0.4323053f, -0.0085287f, -0.1470543f, 0.5183603f,
        0.0400428f, 0.1599627f, 0.0492912f, 0.9684867f
    };
    const float sourceWhite[3] = {
        whitePoint.x / whitePoint.y, 1.0f,
        (1.0f - whitePoint.x - whitePoint.y) / whitePoint.y
    };
    const float destinationWhite[3] = {
        0.34567f / 0.35850f, 1.0f,
        (1.0f - 0.34567f - 0.35850f) / 0.35850f
    };
    float sourceCone[3], destinationCone[3];
    if (!(whitePoint.y > 0.0f) || !isfinite((double)whitePoint.x) ||
        !isfinite((double)whitePoint.y))
        return false;
    XImage_matrixMap(bradford, sourceWhite, sourceCone);
    XImage_matrixMap(bradford, destinationWhite, destinationCone);
    if (fabsf(sourceCone[0]) < 1.0e-8f || fabsf(sourceCone[1]) < 1.0e-8f ||
        fabsf(sourceCone[2]) < 1.0e-8f)
        return false;
    {
        float diagonal[9] = {
            destinationCone[0] / sourceCone[0], 0.0f, 0.0f,
            0.0f, destinationCone[1] / sourceCone[1], 0.0f,
            0.0f, 0.0f, destinationCone[2] / sourceCone[2]
        };
        float temporary[9];
        XImage_matrixMultiply(diagonal, bradford, temporary);
        XImage_matrixMultiply(bradfordInverse, temporary, matrix);
    }
    return true;
}

/* 返回 Qt QColorSpacePrivate::toXyz 的等效矩阵。RGB 空间先由原色和
 * 白点得到 RGB->XYZ 矩阵，再将白点适配到 D50；灰度空间的三个输入轴
 * 都指向它的白点向量，等价于 Qt loadGray() 的 source whitePoint 路径。 */
static bool XImage_colorSpaceToXyz(const XColorSpace* colorSpace,
                                   float matrix[9])
{
    XColorSpaceColorModel model;
    float adaptation[9];
    if (!colorSpace || !XColorSpace_isValid(colorSpace)) return false;
    model = XColorSpace_colorModel(colorSpace);
    if (model == XColorSpaceModel_Rgb)
    {
        if (!XImage_colorSpacePrimariesMatrix(colorSpace, matrix)) return false;
        {
            XColorSpacePrimariesData primaries;
            float adapted[9];
            if (!XColorSpace_primariesData(colorSpace, &primaries) ||
                !XImage_chromaticAdaptationToD50(primaries.m_whitePoint,
                                                 adaptation))
                return false;
            XImage_matrixMultiply(adaptation, matrix, adapted);
            memcpy(matrix, adapted, sizeof(adapted));
        }
        return true;
    }
    if (model == XColorSpaceModel_Gray)
    {
        const XPointF whitePoint = XColorSpace_whitePoint(colorSpace);
        const float white[3] = {
            whitePoint.x / whitePoint.y, 1.0f,
            (1.0f - whitePoint.x - whitePoint.y) / whitePoint.y
        };
        float adaptedWhite[3];
        if (!XImage_chromaticAdaptationToD50(whitePoint, adaptation)) return false;
        XImage_matrixMap(adaptation, white, adaptedWhite);
        matrix[0] = adaptedWhite[0]; matrix[1] = adaptedWhite[0]; matrix[2] = adaptedWhite[0];
        matrix[3] = adaptedWhite[1]; matrix[4] = adaptedWhite[1]; matrix[5] = adaptedWhite[1];
        matrix[6] = adaptedWhite[2]; matrix[7] = adaptedWhite[2]; matrix[8] = adaptedWhite[2];
        return true;
    }
    return false;
}

/**
 * @brief 将一个 32 位 ARGB 颜色通过已经准备好的矩阵完成色彩转换。
 * @param argb 源颜色，通道顺序为 0xAARRGGBB。
 * @param sourceSpace 源色彩空间。
 * @param targetSpace 目标色彩空间。
 * @param sourceMatrix 源色彩空间到 D50 XYZ 的矩阵。
 * @param targetInverse 目标矩阵的逆矩阵，灰度目标时为目标适配矩阵的逆。
 * @param sourceIsRgb 源色彩空间是否为 RGB 模型。
 * @param targetIsRgb 目标色彩空间是否为 RGB 模型。
 * @return 转换后的 32 位 ARGB 颜色，Alpha 通道保持不变。
 * @note 该辅助函数使用与逐像素路径相同的 8 位量化规则，供
 *       Indexed/Mono 调色板逐项转换复用。Qt 6.8 在 qimage.cpp:5211-5217
 *       对 Indexed 数据变换的是颜色表，而不是图像中保存的索引字节。
 */
static uint32_t XImage_convertColorSpaceColor(
    uint32_t argb, const XColorSpace* sourceSpace,
    const XColorSpace* targetSpace, const float sourceMatrix[9],
    const float targetInverse[9], bool sourceIsRgb, bool targetIsRgb)
{
    float sourceLinear[3], xyz[3], targetLinear[3];
    const float r = XImage_decodeTransfer(
        (float)((argb >> 16) & 0xffu) / 255.0f,
        sourceSpace->m_transferFunction, sourceSpace->m_gamma);
    const float g = XImage_decodeTransfer(
        (float)((argb >> 8) & 0xffu) / 255.0f,
        sourceSpace->m_transferFunction, sourceSpace->m_gamma);
    const float b = XImage_decodeTransfer(
        (float)(argb & 0xffu) / 255.0f,
        sourceSpace->m_transferFunction, sourceSpace->m_gamma);
    sourceLinear[0] = r;
    sourceLinear[1] = g;
    sourceLinear[2] = b;
    if (!sourceIsRgb)
    {
        /* Qt QColorTransform::loadGray() maps one scalar through the
           colorspace white point.  The gray matrix stores that white point
           in its first column, so duplicating the scalar into all three
           columns would multiply luminance by three. */
        sourceLinear[1] = sourceLinear[2] = 0.0f;
    }
    XImage_matrixMap(sourceMatrix, sourceLinear, xyz);
    if (targetIsRgb)
        XImage_matrixMap(targetInverse, xyz, targetLinear);
    else
    {
        /* Qt 的灰度输出使用目标白点适配后的 Y 分量。 */
        XImage_matrixMap(targetInverse, xyz, targetLinear);
        targetLinear[0] = targetLinear[1];
        targetLinear[2] = targetLinear[1];
    }
    return (argb & 0xff000000u) |
           ((uint32_t)XImage_colorSpaceChannel(XImage_encodeTransfer(
               targetLinear[0], targetSpace->m_transferFunction,
               targetSpace->m_gamma)) << 16) |
           ((uint32_t)XImage_colorSpaceChannel(XImage_encodeTransfer(
               targetLinear[1], targetSpace->m_transferFunction,
               targetSpace->m_gamma)) << 8) |
           (uint32_t)XImage_colorSpaceChannel(XImage_encodeTransfer(
               targetLinear[2], targetSpace->m_transferFunction,
               targetSpace->m_gamma));
}

/**
 * @brief 通过色彩空间矩阵转换三个浮点颜色分量。
 * @param source 源颜色分量，范围通常为 0~1；HDR 输入可暂时超过 1。
 * @param sourceSpace 源色彩空间描述。
 * @param targetSpace 目标色彩空间描述。
 * @param sourceMatrix 源空间到 PCS D50 XYZ 的矩阵。
 * @param targetInverse 目标空间矩阵的逆矩阵。
 * @param sourceIsRgb 源空间是否为 RGB 模型。
 * @param targetIsRgb 目标空间是否为 RGB 模型。
 * @param target 输出目标颜色分量。
 * @note 与 8 位兼容路径使用相同矩阵和传递函数，但保留原始通道精度，
 *       用于 Qt QRgba64/Grayscale16 对应的存储格式。
 */
static void XImage_convertColorSpaceComponents(
    const float source[3], const XColorSpace* sourceSpace,
    const XColorSpace* targetSpace, const float sourceMatrix[9],
    const float targetInverse[9], bool sourceIsRgb, bool targetIsRgb,
    bool extended, float target[3])
{
    float sourceLinear[3], xyz[3], targetLinear[3];
    if (extended)
    {
        const float sourceSign[3] = {
            source[0] < 0.0f ? -1.0f : 1.0f,
            source[1] < 0.0f ? -1.0f : 1.0f,
            source[2] < 0.0f ? -1.0f : 1.0f
        };
        sourceLinear[0] = sourceSign[0] * XImage_decodeTransfer(
            fabsf(source[0]), sourceSpace->m_transferFunction,
            sourceSpace->m_gamma);
        sourceLinear[1] = sourceSign[1] * XImage_decodeTransfer(
            fabsf(source[1]), sourceSpace->m_transferFunction,
            sourceSpace->m_gamma);
        sourceLinear[2] = sourceSign[2] * XImage_decodeTransfer(
            fabsf(source[2]), sourceSpace->m_transferFunction,
            sourceSpace->m_gamma);
    }
    else
    {
        sourceLinear[0] = XImage_decodeTransfer(
            source[0], sourceSpace->m_transferFunction, sourceSpace->m_gamma);
        sourceLinear[1] = XImage_decodeTransfer(
            source[1], sourceSpace->m_transferFunction, sourceSpace->m_gamma);
        sourceLinear[2] = XImage_decodeTransfer(
            source[2], sourceSpace->m_transferFunction, sourceSpace->m_gamma);
    }
    if (!sourceIsRgb)
    {
        /* 灰度矩阵的三列均为白点向量，只需保留一个亮度标量。 */
        sourceLinear[1] = sourceLinear[2] = 0.0f;
    }
    XImage_matrixMap(sourceMatrix, sourceLinear, xyz);
    if (targetIsRgb)
        XImage_matrixMap(targetInverse, xyz, targetLinear);
    else
    {
        XImage_matrixMap(targetInverse, xyz, targetLinear);
        targetLinear[0] = targetLinear[1];
        targetLinear[2] = targetLinear[1];
    }
    if (extended)
    {
        const float targetSign[3] = {
            targetLinear[0] < 0.0f ? -1.0f : 1.0f,
            targetLinear[1] < 0.0f ? -1.0f : 1.0f,
            targetLinear[2] < 0.0f ? -1.0f : 1.0f
        };
        target[0] = targetSign[0] * XImage_encodeTransfer(
            fabsf(targetLinear[0]), targetSpace->m_transferFunction,
            targetSpace->m_gamma);
        target[1] = targetSign[1] * XImage_encodeTransfer(
            fabsf(targetLinear[1]), targetSpace->m_transferFunction,
            targetSpace->m_gamma);
        target[2] = targetSign[2] * XImage_encodeTransfer(
            fabsf(targetLinear[2]), targetSpace->m_transferFunction,
            targetSpace->m_gamma);
    }
    else
    {
        target[0] = XImage_encodeTransfer(targetLinear[0],
                                          targetSpace->m_transferFunction,
                                          targetSpace->m_gamma);
        target[1] = XImage_encodeTransfer(targetLinear[1],
                                          targetSpace->m_transferFunction,
                                          targetSpace->m_gamma);
        target[2] = XImage_encodeTransfer(targetLinear[2],
                                          targetSpace->m_transferFunction,
                                          targetSpace->m_gamma);
    }
}

/** @brief 判断图像是否使用可直接保留 16 位颜色通道的格式。 */
static bool XImage_isNative16ColorFormat(XImageFormat format)
{
    return format == XImageFormat_Grayscale16 ||
           format == XImageFormat_RGBX64 ||
           format == XImageFormat_RGBA64 ||
           format == XImageFormat_RGBA64_Premultiplied;
}

/**
 * @brief 读取图像的原生 16 位 RGB/灰度通道。
 * @param d 图像私有数据。
 * @param x 像素 X 坐标。
 * @param y 像素 Y 坐标。
 * @param red 输出红色或灰度通道。
 * @param green 输出绿色通道。
 * @param blue 输出蓝色通道。
 * @param alpha 输出 Alpha 通道。
 * @return 格式受支持且坐标有效返回 true，否则返回 false。
 */
static bool XImage_readNative16(const XImageData* d, int x, int y,
                                uint16_t* red, uint16_t* green,
                                uint16_t* blue, uint16_t* alpha)
{
    const uint8_t* line;
    const uint8_t* pixel;
    if (!d || !d->m_data || !red || !green || !blue || !alpha ||
        x < 0 || y < 0 || x >= d->m_width || y >= d->m_height ||
        !XImage_isNative16ColorFormat(d->m_format))
        return false;
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    if (d->m_format == XImageFormat_Grayscale16)
    {
        *red = *green = *blue = XImage_load16(line + (size_t)x * 2u);
        *alpha = 65535u;
        return true;
    }
    pixel = line + (size_t)x * 8u;
    *red = XImage_load16(pixel);
    *green = XImage_load16(pixel + 2u);
    *blue = XImage_load16(pixel + 4u);
    *alpha = d->m_format == XImageFormat_RGBX64
        ? 65535u : XImage_load16(pixel + 6u);
    if (d->m_format == XImageFormat_RGBA64_Premultiplied)
    {
        *red = XImage_unpremultiply16(*red, *alpha);
        *green = XImage_unpremultiply16(*green, *alpha);
        *blue = XImage_unpremultiply16(*blue, *alpha);
    }
    return true;
}

/** @brief 将 0~1 的浮点颜色通道量化为 Qt QRgba64 使用的 16 位值。 */
static uint16_t XImage_colorSpaceChannel16(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 65535u;
    return (uint16_t)(value * 65535.0f + 0.5f);
}

/**
 * @brief 将原生 16 位颜色通道写入目标格式。
 * @param d 目标图像私有数据。
 * @param x 像素 X 坐标。
 * @param y 像素 Y 坐标。
 * @param red 未预乘红色通道。
 * @param green 未预乘绿色通道。
 * @param blue 未预乘蓝色通道。
 * @param alpha 未预乘 Alpha 通道。
 * @return 目标为原生 16 位颜色格式且写入成功返回 true。
 */
static bool XImage_writeNative16(XImageData* d, int x, int y,
                                 uint16_t red, uint16_t green,
                                 uint16_t blue, uint16_t alpha)
{
    if (!d || !XImage_isNative16ColorFormat(d->m_format)) return false;
    XImage_writePixelColor16(d, x, y, red, green, blue, alpha);
    return true;
}

/** @brief 判断像素格式是否使用原生半精度或单精度浮点通道。 */
static bool XImage_isNativeFloatColorFormat(XImageFormat format)
{
    return format == XImageFormat_RGBX16FPx4 ||
           format == XImageFormat_RGBA16FPx4 ||
           format == XImageFormat_RGBA16FPx4_Premultiplied ||
           format == XImageFormat_RGBX32FPx4 ||
           format == XImageFormat_RGBA32FPx4 ||
           format == XImageFormat_RGBA32FPx4_Premultiplied;
}

/**
 * @brief 读取可参与色彩变换的原生高精度浮点通道。
 * @param d 图像私有数据。
 * @param x 像素 X 坐标。
 * @param y 像素 Y 坐标。
 * @param red 输出未预乘红色分量。
 * @param green 输出未预乘绿色分量。
 * @param blue 输出未预乘蓝色分量。
 * @param alpha 输出 Alpha 分量；RGBX 格式固定为 1。
 * @return 读取成功返回 true，否则返回 false。
 * @note 同时接受 16 位整数源，便于 RGBA64 到 FP32 的转换保留源精度。
 */
static bool XImage_readNativeFloat(const XImageData* d, int x, int y,
                                   float* red, float* green,
                                   float* blue, float* alpha)
{
    const uint8_t* line;
    const uint8_t* pixel;
    if (!d || !d->m_data || !red || !green || !blue || !alpha ||
        x < 0 || y < 0 || x >= d->m_width || y >= d->m_height)
        return false;
    if (XImage_isNative16ColorFormat(d->m_format))
    {
        uint16_t channel[4];
        if (!XImage_readNative16(d, x, y, &channel[0], &channel[1],
                                 &channel[2], &channel[3]))
            return false;
        *red = (float)channel[0] / 65535.0f;
        *green = (float)channel[1] / 65535.0f;
        *blue = (float)channel[2] / 65535.0f;
        *alpha = (float)channel[3] / 65535.0f;
        return true;
    }
    if (!XImage_isNativeFloatColorFormat(d->m_format))
    {
        /* Qt QColorTransform promotes ordinary 8-bit/packed sources to a
           floating intermediate when the destination is a floating format.
           Reuse the public ARGB interpretation here so Indexed/Mono and
           grayscale sources retain their existing palette semantics. */
        const uint32_t argb = XImage_readPixelValue(d, x, y);
        *red = (float)((argb >> 16) & 0xffu) / 255.0f;
        *green = (float)((argb >> 8) & 0xffu) / 255.0f;
        *blue = (float)(argb & 0xffu) / 255.0f;
        *alpha = (float)(argb >> 24) / 255.0f;
        return true;
    }
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    if (d->m_format <= XImageFormat_RGBA16FPx4_Premultiplied)
    {
        pixel = line + (size_t)x * 8u;
        *red = XImage_halfToFloat(XImage_load16(pixel));
        *green = XImage_halfToFloat(XImage_load16(pixel + 2u));
        *blue = XImage_halfToFloat(XImage_load16(pixel + 4u));
        *alpha = d->m_format == XImageFormat_RGBX16FPx4
            ? 1.0f : XImage_halfToFloat(XImage_load16(pixel + 6u));
    }
    else
    {
        pixel = line + (size_t)x * 16u;
        memcpy(red, pixel, sizeof(float));
        memcpy(green, pixel + 4u, sizeof(float));
        memcpy(blue, pixel + 8u, sizeof(float));
        *alpha = d->m_format == XImageFormat_RGBX32FPx4
            ? 1.0f : 0.0f;
        if (d->m_format != XImageFormat_RGBX32FPx4)
            memcpy(alpha, pixel + 12u, sizeof(float));
    }
    if (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ||
        d->m_format == XImageFormat_RGBA32FPx4_Premultiplied)
    {
        if (*alpha > 0.0f)
        {
            *red /= *alpha;
            *green /= *alpha;
            *blue /= *alpha;
        }
        else
            *red = *green = *blue = 0.0f;
    }
    return true;
}

/**
 * @brief 将浮点颜色通道写入原生半精度或单精度格式。
 * @param d 目标图像私有数据。
 * @param x 像素 X 坐标。
 * @param y 像素 Y 坐标。
 * @param red 未预乘红色分量。
 * @param green 未预乘绿色分量。
 * @param blue 未预乘蓝色分量。
 * @param alpha 未预乘 Alpha 分量。
 * @return 目标格式受支持且写入成功返回 true，否则返回 false。
 * @note 半精度目标按 Qt qfloat16 规则量化；单精度目标保留完整 C99 float。
 */
static bool XImage_writeNativeFloat(XImageData* d, int x, int y,
                                    float red, float green,
                                    float blue, float alpha)
{
    uint8_t* line;
    uint8_t* pixel;
    float storageAlpha;
    if (!d || !d->m_data || x < 0 || y < 0 || x >= d->m_width ||
        y >= d->m_height || !XImage_isNativeFloatColorFormat(d->m_format))
        return false;
    storageAlpha = (d->m_format == XImageFormat_RGBX16FPx4 ||
                    d->m_format == XImageFormat_RGBX32FPx4) ? 1.0f : alpha;
    if (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ||
        d->m_format == XImageFormat_RGBA32FPx4_Premultiplied)
    {
        red *= storageAlpha;
        green *= storageAlpha;
        blue *= storageAlpha;
    }
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    if (d->m_format <= XImageFormat_RGBA16FPx4_Premultiplied)
    {
        pixel = line + (size_t)x * 8u;
        XImage_store16(pixel, XImage_floatToHalf(red));
        XImage_store16(pixel + 2u, XImage_floatToHalf(green));
        XImage_store16(pixel + 4u, XImage_floatToHalf(blue));
        XImage_store16(pixel + 6u, XImage_floatToHalf(storageAlpha));
    }
    else
    {
        pixel = line + (size_t)x * 16u;
        memcpy(pixel, &red, sizeof(float));
        memcpy(pixel + 4u, &green, sizeof(float));
        memcpy(pixel + 8u, &blue, sizeof(float));
        memcpy(pixel + 12u, &storageAlpha, sizeof(float));
    }
    return true;
}

/**
 * @brief 判断像素格式是否通过颜色表保存颜色索引。
 * @param format 待判断的 XImage 像素格式。
 * @return Indexed8、Mono 或 MonoLSB 返回 true，否则返回 false。
 */
static bool XImage_isIndexedColorFormat(XImageFormat format)
{
    return format == XImageFormat_Indexed8 ||
           format == XImageFormat_Mono || format == XImageFormat_MonoLSB;
}

static void XImage_convertColorSpacePixels(const XImage* source,
                                           XImage* target,
                                           XColorSpace sourceSpace,
                                           XColorSpace targetSpace)
{
    const bool sourceKnown = XColorSpace_isValid(&sourceSpace);
    const bool targetKnown = XColorSpace_isValid(&targetSpace);
    const bool sourceIsRgb = XColorSpace_colorModel(&sourceSpace) == XColorSpaceModel_Rgb;
    const bool targetIsRgb = XColorSpace_colorModel(&targetSpace) == XColorSpaceModel_Rgb;
    float sourceMatrix[9], targetMatrix[9], targetInverse[9];
    if (!source || !target || !sourceKnown || !targetKnown) return;
    if (XColorSpace_equals(&sourceSpace, &targetSpace)) return;
    if (!XImage_colorSpaceToXyz(&sourceSpace, sourceMatrix) ||
        !XImage_colorSpaceToXyz(&targetSpace, targetMatrix))
        return;
    if (targetIsRgb && !XImage_matrixInverse(targetMatrix, targetInverse))
        return;
    if (!targetIsRgb)
    {
        const XPointF targetWhitePoint = XColorSpace_whitePoint(&targetSpace);
        float targetAdaptation[9];
        if (!XImage_chromaticAdaptationToD50(targetWhitePoint,
                                             targetAdaptation) ||
            !XImage_matrixInverse(targetAdaptation, targetInverse))
            return;
    }

    /* Qt 6.8 qimage.cpp:5211-5217 对 Indexed color model 只变换调色板，
     * 保存的索引位不能当作 RGB 值写回。XImage_setPixel() 对索引格式的
     * 参数含义本来就是“索引”，若把变换后的 ARGB 直接传入会只保留低
     * 字节，既破坏索引又无法改变颜色。目标可能是另一种索引格式，先
     * 保持源格式的调色板语义，调用者再按 Qt 的中间格式规则转换。 */
    if (source->m_data && target->m_data &&
        XImage_isIndexedColorFormat(source->m_data->m_format) &&
        XImage_isIndexedColorFormat(target->m_data->m_format))
    {
        const int sourceCount = source->m_data->m_colorCount;
        const int targetCount = target->m_data->m_colorCount;
        const int count = sourceCount < targetCount ? sourceCount : targetCount;
        XImage_detach(target);
        if (!XImage_isDetached(target)) return;
        if (source->m_data->m_colorTable && target->m_data->m_colorTable &&
            count > 0)
        {
            for (int i = 0; i < count; ++i)
                target->m_data->m_colorTable[i] =
                    XImage_convertColorSpaceColor(
                        source->m_data->m_colorTable[i], &sourceSpace,
                        &targetSpace, sourceMatrix, targetInverse,
                        sourceIsRgb, targetIsRgb);
        }
        return;
    }

    /* Qt 6.8 qimage.cpp:5219-5289 对 QRgba64/Grayscale16 直接使用原生
     * 16 位通道变换；若先调用 XImage_pixel()，低 8 位会被永久丢失。
     * 浮点源也先提升到同一中间值，再由整数目标在最后一步量化，避免
     * FP -> RGBA64 转换额外经过 8 位窄化。 */
    if (source->m_data && target->m_data &&
        XImage_isNative16ColorFormat(target->m_data->m_format) &&
        (XImage_isNative16ColorFormat(source->m_data->m_format) ||
         XImage_isNativeFloatColorFormat(source->m_data->m_format)))
    {
        const int width = XImage_width(source), height = XImage_height(source);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float sourceFloat[4], targetFloat[3];
                if (!XImage_readNativeFloat(source->m_data, x, y,
                                            &sourceFloat[0], &sourceFloat[1],
                                            &sourceFloat[2], &sourceFloat[3]))
                    continue;
                XImage_convertColorSpaceComponents(
                    sourceFloat, &sourceSpace, &targetSpace,
                    sourceMatrix, targetInverse, sourceIsRgb, targetIsRgb,
                    false, targetFloat);
                XImage_writeNative16(
                    target->m_data, x, y,
                    XImage_colorSpaceChannel16(targetFloat[0]),
                    XImage_colorSpaceChannel16(targetFloat[1]),
                    XImage_colorSpaceChannel16(targetFloat[2]),
                    XImage_colorSpaceChannel16(sourceFloat[3]));
            }
        }
        return;
    }

    /* Qt 6.8 qimage.cpp:5268-5289 对浮点精度图像直接应用
     * QColorTransform，不能先经 XImage_pixel() 量化到 8 位。目标为浮点
     * 格式时，16 位整数源也可无损提升为浮点中间值；目标半精度的最后一步
     * 由 qfloat16 等价转换完成。这样 FP32/HDR 通道不会被兼容路径截断。 */
    if (source->m_data && target->m_data &&
        XImage_isNativeFloatColorFormat(target->m_data->m_format))
    {
        const int width = XImage_width(source), height = XImage_height(source);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                float sourceFloat[4], sourceRgb[3], targetRgb[3];
                if (!XImage_readNativeFloat(source->m_data, x, y,
                                            &sourceFloat[0], &sourceFloat[1],
                                            &sourceFloat[2], &sourceFloat[3]))
                    continue;
                sourceRgb[0] = sourceFloat[0];
                sourceRgb[1] = sourceFloat[1];
                sourceRgb[2] = sourceFloat[2];
                XImage_convertColorSpaceComponents(
                    sourceRgb, &sourceSpace, &targetSpace,
                    sourceMatrix, targetInverse, sourceIsRgb, targetIsRgb,
                    true, targetRgb);
                XImage_writeNativeFloat(target->m_data, x, y,
                                        targetRgb[0], targetRgb[1], targetRgb[2],
                                        sourceFloat[3]);
            }
        }
        return;
    }

    const int width = XImage_width(source), height = XImage_height(source);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const uint32_t argb = XImage_pixel(source, x, y);
            const uint32_t converted = XImage_convertColorSpaceColor(
                argb, &sourceSpace, &targetSpace, sourceMatrix, targetInverse,
                sourceIsRgb, targetIsRgb);
            if (!targetIsRgb &&
                (XImage_format(target) == XImageFormat_Grayscale8 ||
                 XImage_format(target) == XImageFormat_Grayscale16))
            {
                XImage_writePixelValue(target->m_data, x, y,
                                       converted);
                continue;
            }
            XImage_setPixel(target, x, y, converted);
        }
    }
}

void XImage_convertedToColorSpace(const XImage* self, XColorSpace colorSpace,
                                  uint32_t flags, XImage* out)
{
    (void)flags;
    if (!out) return;
    if (!self || !self->m_data ||
        !XColorSpace_isValid(&self->m_data->m_colorSpace) ||
        !XColorSpace_isValidTarget(&colorSpace))
    {
        XImage_deinit_base(out);
        return;
    }
    if (out == self)
    {
        XImage_convertToColorSpace(out, colorSpace, flags);
        return;
    }
    XCopy(out, self);
    /* Qt changes to a color-capable format when the source model cannot
     * represent the target color space (for example CMYK -> RGB).  The
     * current XColorSpace value type describes RGB spaces only, so ARGB32 is
     * the canonical target for that conversion. */
    if (!XImage_colorSpaceTargetCompatible(self, &colorSpace))
    {
        XImage converted;
        XImage_init(&converted);
        XImage_convertToFormat(self, XImageFormat_ARGB32, flags, &converted);
        if (XImage_isNull(&converted))
        {
            XImage_deinit_base(out);
            XImage_deinit_base(&converted);
            return;
        }
        XMove(out, &converted);
    }
    XImage_convertColorSpacePixels(self, out, self->m_data->m_colorSpace, colorSpace);
    XImage_setColorSpace(out, colorSpace);
}

void XImage_convertedToColorSpace_ex(const XImage* self, XColorSpace colorSpace,
                                     XImageFormat format, uint32_t flags,
                                     XImage* out)
{
    XColorTransform transform;
    if (!out) return;
    if (!self || !self->m_data ||
        !XColorSpace_isValid(&self->m_data->m_colorSpace) ||
        !XColorSpace_isValidTarget(&colorSpace) ||
        !XImage_colorSpaceTargetCompatibleFormat(format, &colorSpace))
    {
        XImage_deinit_base(out);
        return;
    }
    if (XColorSpace_equals(&self->m_data->m_colorSpace, &colorSpace))
    {
        XImage_convertToFormat(self, format, flags, out);
        return;
    }
    transform.m_source = self->m_data->m_colorSpace;
    transform.m_target = colorSpace;
    XImage_applyColorTransform(self, &transform, format, flags, out);
}

bool XImage_convertToColorSpace(XImage* self, XColorSpace colorSpace,
                                uint32_t flags)
{
    (void)flags;
    if (!self || !self->m_data ||
        !XColorSpace_isValid(&self->m_data->m_colorSpace) ||
        !XColorSpace_isValidTarget(&colorSpace)) return false;
    if (XColorSpace_equals(&self->m_data->m_colorSpace, &colorSpace)) return true;
    if (!XImage_colorSpaceTargetCompatible(self, &colorSpace))
    {
        if (!XImage_convertToFormatInPlace(self, XImageFormat_ARGB32, flags))
            return false;
    }
    const XColorSpace sourceSpace = self->m_data->m_colorSpace;
    if (!XColorSpace_isValid(&sourceSpace)) return false;
    XImage_detach(self);
    XImage_convertColorSpacePixels(self, self, sourceSpace, colorSpace);
    self->m_data->m_colorSpace = colorSpace;
    XImageData_markDirty(self->m_data);
    return true;
}

bool XImage_convertToColorSpace_ex(XImage* self, XColorSpace colorSpace,
                                   XImageFormat format, uint32_t flags)
{
    XImage converted;
    if (!self || !self->m_data ||
        !XColorSpace_isValid(&self->m_data->m_colorSpace) ||
        !XColorSpace_isValidTarget(&colorSpace) ||
        !XImage_colorSpaceTargetCompatibleFormat(format, &colorSpace))
        return false;
    if (XColorSpace_equals(&self->m_data->m_colorSpace, &colorSpace))
        return XImage_convertToFormatInPlace(self, format, flags);
    XImage_init(&converted);
    XImage_convertedToColorSpace_ex(self, colorSpace, format, flags, &converted);
    if (XImage_isNull(&converted))
    {
        XImage_deinit_base(&converted);
        return false;
    }
    XMove(self, &converted);
    return true;
}

void XImage_applyColorTransform(const XImage* self, const XColorTransform* transform,
                                XImageFormat format, uint32_t flags, XImage* out)
{
    XColorSpace source;
    XImageFormat outputFormat = format;
    XImageFormat transformFormat = format;
    if (!self || !out || !transform ||
        !XColorSpace_isValidTarget(&transform->m_target))
    {
        XImage_deinit_base(out);
        return;
    }
    source = transform->m_source;
    if (!XColorSpace_isValid(&source)) source = XImage_colorSpace(self);
    if (!XColorSpace_isValid(&source) ||
        !XImage_colorSpaceCompatible(self, &source))
    {
        if (out != self)
            XImage_deinit_base(out);
        return;
    }
    /* Qt QColorTransform::isIdentity() returns without touching pixels.  The
       portable transform carries only source/target spaces, so equal spaces
       are the representable identity case.  Preserve implicit sharing when
       no output format conversion is requested; an explicit format still
       follows Qt's identity path and performs that conversion. */
    if (XColorSpace_equals(&source, &transform->m_target))
    {
        if (format == XImageFormat_Invalid || format == XImage_format(self))
        {
            if (out != self)
                XCopy(out, self);
            return;
        }
        if (out == self)
        {
            (void)XImage_convertToFormatInPlace(out, format, flags);
            return;
        }
        XImage_convertToFormat(self, format, flags, out);
        return;
    }
    if (outputFormat == XImageFormat_Invalid &&
        !XImage_colorSpaceTargetCompatible(self, &transform->m_target))
    {
        switch (XColorSpace_colorModel(&transform->m_target))
        {
            case XColorSpaceModel_Rgb: outputFormat = XImageFormat_ARGB32; break;
            case XColorSpaceModel_Gray: outputFormat = XImageFormat_Grayscale8; break;
            case XColorSpaceModel_Cmyk: outputFormat = XImageFormat_CMYK8888; break;
            default: outputFormat = XImageFormat_Invalid; break;
        }
        if (outputFormat == XImageFormat_Invalid)
        {
            if (out != self)
                XImage_deinit_base(out);
            return;
        }
    }
    if (outputFormat != XImageFormat_Invalid)
    {
        /* Qt permits a model switch when the caller explicitly chooses a
           compatible output format (for example RGB -> Grayscale8).  Only
           the selected format's own model is constrained here; checking the
           source model first would incorrectly reject that valid path. */
        const XPixelFormat outputPixel = XImageFormat_toPixelFormat(outputFormat);
        XColorSpaceColorModel outputModel;
        switch (outputPixel.m_model)
        {
            case XPixelFormatModel_Gray: outputModel = XColorSpaceModel_Gray; break;
            case XPixelFormatModel_CMYK: outputModel = XColorSpaceModel_Cmyk; break;
            case XPixelFormatModel_Alpha: outputModel = XColorSpaceModel_Undefined; break;
            case XPixelFormatModel_Indexed:
            case XPixelFormatModel_RGB:
            case XPixelFormatModel_BGR: outputModel = XColorSpaceModel_Rgb; break;
            default: outputModel = XColorSpaceModel_Undefined; break;
        }
        if (!XImage_colorSpaceTargetCompatibleModel(outputModel,
                                                     &transform->m_target))
        {
            if (out != self)
                XImage_deinit_base(out);
            return;
        }
    }

    /* Qt 6.8 qimage.cpp:5467-5527 先选择可直接执行颜色变换的中间格式，
     * 最后再转换为调用者要求的格式。Indexed/Mono 的颜色变换尤其需要
     * 先保留索引格式以变换颜色表；非索引图像则先转到 ARGB32，避免把
     * 变换后的 ARGB 值误当作 Indexed8 的索引写入。 */
    if (outputFormat != XImageFormat_Invalid &&
        XImage_isIndexedColorFormat(outputFormat))
    {
        transformFormat = XImage_isIndexedColorFormat(XImage_format(self))
            ? XImage_format(self) : XImageFormat_ARGB32;
    }
    XCopy(out, self);
    if (transformFormat != XImageFormat_Invalid &&
        transformFormat != XImage_format(out))
    {
        XImage converted;
        XImage_init(&converted);
        XImage_convertToFormat(out, transformFormat, flags, &converted);
        XMove(out, &converted);
    }
    /* XCopy() above intentionally shares the source data.  A
       color transform writes every destination pixel, so detach the output
       first whenever it is a distinct image; otherwise converting a copied
       RGBA32FPx4 image would mutate the caller's source in place. */
    if (out != self)
        XImage_detach(out);
    XImage_convertColorSpacePixels(self, out, source, transform->m_target);

    if (outputFormat != XImageFormat_Invalid &&
        outputFormat != XImage_format(out))
    {
        XImage converted;
        XImage_init(&converted);
        XImage_convertToFormat(out, outputFormat, flags, &converted);
        XMove(out, &converted);
    }
    XImage_setColorSpace(out, transform->m_target);
}

int XImage_colorCount(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_colorCount : 0;
}

void XImage_setColorCount(XImage* self, int count)
{
    if (!self || !self->m_data) return;
    if (count == self->m_data->m_colorCount ||
        (count <= 0 && self->m_data->m_colorCount == 0)) return;
    /* Qt treats every non-positive count as a request to clear the table. */
    if (count <= 0)
    {
        XImage_detach(self);
        if (!XImage_isDetached(self)) return;
        XFree_System(self->m_data->m_colorTable);
        self->m_data->m_colorTable = NULL;
        self->m_data->m_colorCount = 0;
        XImageData_markDirty(self->m_data);
        return;
    }
    if ((size_t)count > SIZE_MAX / sizeof(uint32_t)) return;
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
    int maxIndex;
    if (!self || !self->m_data) return;
    /* QImage permits palette entries up to the storage depth even when the
     * current table is shorter, growing the table to index + 1 first. */
    if (index < 0 || self->m_data->m_depth > 8) return;
    maxIndex = 1 << self->m_data->m_depth;
    if (index >= maxIndex) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    if (index >= self->m_data->m_colorCount)
    {
        XImage_setColorCount(self, index + 1);
        if (!self->m_data->m_colorTable || index >= self->m_data->m_colorCount)
            return;
    }
    self->m_data->m_colorTable[index] = color;
    XImageData_markDirty(self->m_data);
}

int XImage_colorTable(const XImage* self, uint32_t* out, int maxCount)
{
    int count;
    if (!self || !self->m_data) return 0;
    count = self->m_data->m_colorCount;
    if (out && maxCount > 0 && self->m_data->m_colorTable)
    {
        int copyCount = count < maxCount ? count : maxCount;
        memcpy(out, self->m_data->m_colorTable, (size_t)copyCount * sizeof(uint32_t));
    }
    return count;
}

void XImage_setColorTable(XImage* self, const uint32_t* colors, int count)
{
    if (!self || !self->m_data || count < 0 || (count > 0 && !colors)) return;
    if ((size_t)count > SIZE_MAX / sizeof(uint32_t)) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    uint32_t* replacement = NULL;
    if (count > 0)
    {
        replacement = (uint32_t*)XMalloc_System((size_t)count * sizeof(uint32_t));
        if (!replacement) return;
        memcpy(replacement, colors, (size_t)count * sizeof(uint32_t));
    }
    XFree_System(self->m_data->m_colorTable);
    self->m_data->m_colorTable = replacement;
    self->m_data->m_colorCount = count;
    XImageData_markDirty(self->m_data);
}

void XImage_fill(XImage* self, uint32_t pixel)
{
    XImageData* d;
    int x, y;
    if (!self || !self->m_data || !self->m_data->m_data) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    d = self->m_data;

    /* QImage::fill(uint) writes a storage pixel, rather than converting the
       value through QColor.  Keep the same depth-specific truncation and
       reserved-bit rules while retaining XinYueC's portable byte layouts. */
    if (d->m_depth == 1)
    {
        const size_t bytes = ((size_t)d->m_width + 7u) / 8u;
        const int value = (pixel & 1u) ? 0xff : 0;
        for (y = 0; y < d->m_height; ++y)
            memset(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine,
                   value, bytes);
    }
    else if (d->m_depth == 8)
    {
        const uint8_t value = (uint8_t)pixel;
        for (y = 0; y < d->m_height; ++y)
            memset(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine,
                   value, (size_t)d->m_width);
    }
    else if (d->m_depth == 16)
    {
        uint16_t value = (uint16_t)pixel;
        if (d->m_format == XImageFormat_RGB444) value = (uint16_t)(value | 0xf000u);
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
                XImage_store16(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 2u, value);
    }
    else if (d->m_depth == 24)
    {
        uint32_t value = pixel & 0x00ffffffu;
        uint8_t bytes[3];
        if (d->m_format == XImageFormat_RGB666) value |= 0x00fc0000u;
        bytes[0] = (uint8_t)(value >> 16);
        bytes[1] = (uint8_t)(value >> 8);
        bytes[2] = (uint8_t)value;
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
                memcpy(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 3u,
                       bytes, sizeof(bytes));
    }
    else if (d->m_format >= XImageFormat_RGBX16FPx4 &&
             d->m_format <= XImageFormat_RGBA16FPx4_Premultiplied)
    {
        const float r = ((float)((pixel >> 16) & 0xffu)) / 255.0f;
        const float g = ((float)((pixel >> 8) & 0xffu)) / 255.0f;
        const float b = ((float)(pixel & 0xffu)) / 255.0f;
        /* QImage::fill(uint) uses QRgbaFloat16::fromArgb32(pixel) for
         * RGBX16FPx4 too; unlike fill(QColor), it does not force X alpha to
         * opaque. Keep the storage value supplied by the caller. */
        const float a = ((float)((pixel >> 24) & 0xffu)) / 255.0f;
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
            {
                uint8_t* p = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 8u;
                XImage_store16(p, XImage_floatToHalf(r));
                XImage_store16(p + 2, XImage_floatToHalf(g));
                XImage_store16(p + 4, XImage_floatToHalf(b));
                XImage_store16(p + 6, XImage_floatToHalf(a));
            }
    }
    else if (d->m_depth == 64)
    {
        const uint16_t r = (uint16_t)(((pixel >> 16) & 0xffu) * 257u);
        const uint16_t g = (uint16_t)(((pixel >> 8) & 0xffu) * 257u);
        const uint16_t b = (uint16_t)((pixel & 0xffu) * 257u);
        /* Qt qimage.cpp:1792-1795 constructs RGBX64 fromArgb32(pixel)
         * without the opaque OR used by setPixel(). */
        const uint16_t a = (uint16_t)(((pixel >> 24) & 0xffu) * 257u);
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
            {
                uint8_t* p = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 8u;
                XImage_store16(p, r); XImage_store16(p + 2, g);
                XImage_store16(p + 4, b); XImage_store16(p + 6, a);
            }
    }
    else if (d->m_depth == 128)
    {
        const float r = ((float)((pixel >> 16) & 0xffu)) / 255.0f;
        const float g = ((float)((pixel >> 8) & 0xffu)) / 255.0f;
        const float b = ((float)(pixel & 0xffu)) / 255.0f;
        /* As with the 16-bit float layout, fill(uint) preserves the caller's
         * ARGB alpha in the X component (qimage.cpp:1802-1806). */
        const float a = ((float)((pixel >> 24) & 0xffu)) / 255.0f;
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
            {
                uint8_t* p = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 16u;
                memcpy(p, &r, sizeof(float)); memcpy(p + 4, &g, sizeof(float));
                memcpy(p + 8, &b, sizeof(float)); memcpy(p + 12, &a, sizeof(float));
            }
    }
    else
    {
        uint32_t value = pixel;
        if (d->m_format == XImageFormat_RGB32) value |= 0xff000000u;
        if (d->m_format == XImageFormat_RGBX8888)
        {
#if IS_BIG_ENDIAN
            value = (value & 0xffffff00u) | 0x000000ffu;
#else
            value = (value & 0x00ffffffu) | 0xff000000u;
#endif
        }
        if (d->m_format == XImageFormat_BGR30 || d->m_format == XImageFormat_RGB30)
            value |= 0xc0000000u;
        if (d->m_format == XImageFormat_RGB32 ||
            d->m_format == XImageFormat_ARGB32 ||
            d->m_format == XImageFormat_ARGB32_Premultiplied ||
            d->m_format == XImageFormat_RGBX8888 ||
            d->m_format == XImageFormat_RGBA8888 ||
            d->m_format == XImageFormat_RGBA8888_Premultiplied ||
            d->m_format == XImageFormat_BGR30 || d->m_format == XImageFormat_RGB30 ||
            d->m_format == XImageFormat_A2BGR30_Premultiplied ||
            d->m_format == XImageFormat_A2RGB30_Premultiplied ||
            d->m_format == XImageFormat_CMYK8888)
            for (y = 0; y < d->m_height; ++y)
                for (x = 0; x < d->m_width; ++x)
                    XImage_store32(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine + (size_t)x * 4u, value);
        else
            for (y = 0; y < d->m_height; ++y)
                for (x = 0; x < d->m_width; ++x)
                    XImage_writePixelValue(d, x, y, pixel);
    }
    XImageData_markDirty(d);
}

void XImage_fillColor(XImage* self, const XColor* color)
{
    XImageData* d;
    XColor rgb;
    uint32_t argb;
    int x, y;
    if (!self || !self->m_data || !color || !XColor_isValid(color)) return;
    XImage_detach(self);
    if (!XImage_isDetached(self)) return;
    d = self->m_data;
    XColor_toRgb(color, &rgb);
    argb = ((uint32_t)XColor_alpha(color) << 24) |
           ((uint32_t)XColor_red(color) << 16) |
           ((uint32_t)XColor_green(color) << 8) |
           (uint32_t)XColor_blue(color);

    /* QColor::fill handles indexed and monochrome images through their
       palette/index semantics rather than converting the value as a color. */
    if (d->m_format == XImageFormat_Mono || d->m_format == XImageFormat_MonoLSB)
    {
        const size_t bytes = ((size_t)d->m_width + 7u) / 8u;
        const int value = argb == 0xffffffffu ? 0xff : 0;
        for (y = 0; y < d->m_height; ++y)
            memset(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine,
                   value, bytes);
    }
    else if (d->m_format == XImageFormat_Indexed8)
    {
        uint32_t index = 0;
        if (d->m_colorTable)
        {
            for (int i = 0; i < d->m_colorCount; ++i)
            {
                if (d->m_colorTable[i] == argb)
                {
                    index = (uint32_t)i;
                    break;
                }
            }
        }
        for (y = 0; y < d->m_height; ++y)
            memset(d->m_data + (size_t)y * (size_t)d->m_bytesPerLine,
                   (int)(uint8_t)index, (size_t)d->m_width);
    }
    else if (d->m_format == XImageFormat_BGR30 ||
             d->m_format == XImageFormat_RGB30 ||
             d->m_format == XImageFormat_A2BGR30_Premultiplied ||
             d->m_format == XImageFormat_A2RGB30_Premultiplied ||
             d->m_format == XImageFormat_RGBX64 ||
             d->m_format == XImageFormat_RGBA64 ||
             d->m_format == XImageFormat_RGBA64_Premultiplied ||
             d->m_format == XImageFormat_RGBX16FPx4 ||
             d->m_format == XImageFormat_RGBA16FPx4 ||
             d->m_format == XImageFormat_RGBA16FPx4_Premultiplied ||
             d->m_format == XImageFormat_RGBX32FPx4 ||
             d->m_format == XImageFormat_RGBA32FPx4 ||
             d->m_format == XImageFormat_RGBA32FPx4_Premultiplied ||
             d->m_format == XImageFormat_Grayscale16)
    {
        /* Qt qimage.cpp:1847-1960 uses QRgba64/getRgbF for high precision
         * formats.  Keep the native 16-bit components instead of narrowing
         * through rgba(), which would discard the low eight bits. */
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
                XImage_writePixelColor16(d, x, y, rgb.m_comp1,
                                         rgb.m_comp2, rgb.m_comp3,
                                         rgb.m_alpha);
    }
    else
    {
        for (y = 0; y < d->m_height; ++y)
            for (x = 0; x < d->m_width; ++x)
                XImage_writePixelValue(d, x, y, argb);
    }
    XImageData_markDirty(d);
}

static bool XImage_colorTableHasAlpha(const XImageData* data)
{
    if (!data || (data->m_format != XImageFormat_Mono &&
                  data->m_format != XImageFormat_MonoLSB &&
                  data->m_format != XImageFormat_Indexed8) ||
        !data->m_colorTable || data->m_colorCount <= 0)
        return false;
    for (int i = 0; i < data->m_colorCount; ++i)
        if ((data->m_colorTable[i] >> 24) != 0xffu)
            return true;
    return false;
}

bool XImage_hasAlphaChannel(const XImage* self)
{
    if (!self || !self->m_data) return false;
    /* Qt qimage.cpp:4591-4600 treats indexed formats specially: an alpha
       entry anywhere in the color table makes hasAlphaChannel() true, even
       when no pixel currently references that entry. */
    return XImageFormat_hasAlpha(self->m_data->m_format) ||
           XImage_colorTableHasAlpha(self->m_data);
}

bool XImage_hasAlpha(const XImage* self)
{
    if (!self || !self->m_data) return false;
    if (!XImage_hasAlphaChannel(self))
        return false;
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
            if ((XImage_pixel(self, x, y) >> 24) != 0xFFu)
                return true;
    return false;
}

bool XImage_isGrayscale(const XImage* self)
{
    XImageFormat format;
    int depth;

    /* QImage distinguishes an image made only of grayscale pixels from an
     * image whose indexed palette is the canonical i -> (i, i, i) table.
     * In particular, a null image is not a grayscale image, and a
     * monochrome image has depth one and therefore falls through to false. */
    if (!self || !self->m_data) return false;
    format = self->m_data->m_format;
    if (format == XImageFormat_Alpha8) return false;
    if (format == XImageFormat_Grayscale8 || format == XImageFormat_Grayscale16)
        return true;

    depth = self->m_data->m_depth;
    if (depth == 32 || depth == 24 || depth == 16)
        return XImage_allGray(self);
    if (depth == 8 && format == XImageFormat_Indexed8)
    {
        int i;
        for (i = 0; i < self->m_data->m_colorCount; ++i)
        {
            uint32_t expected = 0xff000000u | ((uint32_t)i * 0x010101u);
            if (!self->m_data->m_colorTable ||
                self->m_data->m_colorTable[i] != expected)
                return false;
        }
        return true;
    }
    return false;
}

XColor XImage_pixelColor(const XImage* self, int x, int y)
{
    const XImageData* data;
    const uint8_t* line;
    const uint8_t* pixel;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
    uint16_t alpha;
    if (!XImage_valid(self, x, y)) return XColor_create();

    /* Qt 6.8 keeps the native QRgba64 value for these formats.  The normal
       XImage_pixel() API intentionally returns an 8-bit ARGB compatibility
       value, so using it here would make QColor::pixelColor lose precision. */
    data = self->m_data;
    line = data->m_data + (size_t)y * (size_t)data->m_bytesPerLine;
    if (data->m_format == XImageFormat_Grayscale16)
    {
        red = XImage_load16(line + (size_t)x * 2u);
        return XImage_colorFrom16(red, red, red, 65535u);
    }
    if (data->m_format == XImageFormat_RGBX64 ||
        data->m_format == XImageFormat_RGBA64 ||
        data->m_format == XImageFormat_RGBA64_Premultiplied)
    {
        pixel = line + (size_t)x * 8u;
        red = XImage_load16(pixel);
        green = XImage_load16(pixel + 2u);
        blue = XImage_load16(pixel + 4u);
        alpha = data->m_format == XImageFormat_RGBX64
            ? 65535u : XImage_load16(pixel + 6u);
        if (data->m_format == XImageFormat_RGBA64_Premultiplied)
        {
            /* QRgba64::unpremultiplied() returns the transparent value as-is. */
            red = XImage_unpremultiply16(red, alpha);
            green = XImage_unpremultiply16(green, alpha);
            blue = XImage_unpremultiply16(blue, alpha);
        }
        return XImage_colorFrom16(red, green, blue, alpha);
    }
    return XColor_create_argb(XImage_pixel(self, x, y));
}

void XImage_setPixelColor(XImage* self, int x, int y, const XColor* color)
{
    XColor rgb;
    if (!color || !XColor_isValid(color)) return;
    if (!self || !self->m_data) return;
    /* QImage refuses QColor writes to monochrome and paletted storage;
     * callers must use setPixel() with an explicit palette index instead. */
    if (self->m_data->m_format == XImageFormat_Mono ||
        self->m_data->m_format == XImageFormat_MonoLSB ||
        self->m_data->m_format == XImageFormat_Indexed8)
        return;
    if (!XImage_valid(self, x, y)) return;
    XColor_toRgb(color, &rgb);
    if (self->m_data->m_format == XImageFormat_BGR30 ||
        self->m_data->m_format == XImageFormat_RGB30 ||
        self->m_data->m_format == XImageFormat_A2BGR30_Premultiplied ||
        self->m_data->m_format == XImageFormat_A2RGB30_Premultiplied ||
        self->m_data->m_format == XImageFormat_RGBX64 ||
        self->m_data->m_format == XImageFormat_RGBA64 ||
        self->m_data->m_format == XImageFormat_RGBA64_Premultiplied ||
        self->m_data->m_format == XImageFormat_RGBX16FPx4 ||
        self->m_data->m_format == XImageFormat_RGBA16FPx4 ||
        self->m_data->m_format == XImageFormat_RGBA16FPx4_Premultiplied ||
        self->m_data->m_format == XImageFormat_RGBX32FPx4 ||
        self->m_data->m_format == XImageFormat_RGBA32FPx4 ||
        self->m_data->m_format == XImageFormat_RGBA32FPx4_Premultiplied)
    {
        /* Qt uses QColor::rgba64()/getRgbF() for these layouts.  Keep the
         * native components instead of narrowing through an ARGB32 value. */
        XImage_detach(self);
        if (!XImage_isDetached(self)) return;
        XImage_writePixelColor16(self->m_data, x, y, rgb.m_comp1,
                                 rgb.m_comp2, rgb.m_comp3, rgb.m_alpha);
        XImageData_markDirty(self->m_data);
        return;
    }
    XImage_setPixel(self, x, y, XColor_rgba(&rgb));
}

/*
 * 对应 Qt 6.8 qimage_p.h:265-310 的 qt_alphaVersion()。
 * QImage::setAlphaChannel() 不是简单地把无 Alpha 图像升级为 ARGB32，
 * 而是尽量保留原来的通道深度和布局。例如 RGB16 要升级为
 * ARGB8565_Premultiplied，RGBX64 要升级为 RGBA64_Premultiplied。这样
 * DestinationIn 合成不会在调用接口时无故丢失源图像的格式精度。
 */
static XImageFormat XImage_alphaVersionForPainting(XImageFormat format)
{
    switch (format)
    {
        case XImageFormat_RGB32:
        case XImageFormat_ARGB32:
            return XImageFormat_ARGB32_Premultiplied;
        case XImageFormat_RGB16:
            return XImageFormat_ARGB8565_Premultiplied;
        case XImageFormat_RGB555:
            return XImageFormat_ARGB8555_Premultiplied;
        case XImageFormat_RGB666:
            return XImageFormat_ARGB6666_Premultiplied;
        case XImageFormat_RGB444:
            return XImageFormat_ARGB4444_Premultiplied;
        case XImageFormat_RGBX8888:
        case XImageFormat_RGBA8888:
            return XImageFormat_RGBA8888_Premultiplied;
        case XImageFormat_BGR30:
            return XImageFormat_A2BGR30_Premultiplied;
        case XImageFormat_RGB30:
            return XImageFormat_A2RGB30_Premultiplied;
        case XImageFormat_RGBX64:
        case XImageFormat_RGBA64:
        case XImageFormat_Grayscale16:
            return XImageFormat_RGBA64_Premultiplied;
        case XImageFormat_RGBX16FPx4:
        case XImageFormat_RGBA16FPx4:
            return XImageFormat_RGBA16FPx4_Premultiplied;
        case XImageFormat_RGBX32FPx4:
        case XImageFormat_RGBA32FPx4:
            return XImageFormat_RGBA32FPx4_Premultiplied;
        case XImageFormat_ARGB32_Premultiplied:
        case XImageFormat_ARGB8565_Premultiplied:
        case XImageFormat_ARGB8555_Premultiplied:
        case XImageFormat_ARGB6666_Premultiplied:
        case XImageFormat_ARGB4444_Premultiplied:
        case XImageFormat_RGBA8888_Premultiplied:
        case XImageFormat_A2BGR30_Premultiplied:
        case XImageFormat_A2RGB30_Premultiplied:
        case XImageFormat_RGBA64_Premultiplied:
        case XImageFormat_RGBA16FPx4_Premultiplied:
        case XImageFormat_RGBA32FPx4_Premultiplied:
            return format;
        default:
            /* Mono、MonoLSB、Indexed8、Alpha8、RGB888、BGR888、Grayscale8 和 CMYK8888
               没有同深度的可绘制 Alpha 版本，Qt 统一选择 ARGB32PM。 */
            return XImageFormat_ARGB32_Premultiplied;
    }
}

bool XImage_setAlphaChannel(XImage* self, const XImage* alphaChannel)
{
    XImage sourceCopy;
    XImage sourceImage;
    const XImage* source;
    XImageFormat targetFormat;
    int targetWidth;
    int targetHeight;
    bool sourceImageInitialized = false;

    if (!self || !self->m_data || !self->m_data->m_data ||
        !alphaChannel || !alphaChannel->m_data || !alphaChannel->m_data->m_data)
        return false;

    /* Qt 先以值语义保存 alphaChannel。特别是 self == alphaChannel 时，
       后续的格式升级和写入不能改变我们随后要读取的源 Alpha。 */
    XImage_init(&sourceCopy);
    if ((const XImage*)self == alphaChannel)
    {
        XCopy(&sourceCopy, alphaChannel);
        if (!sourceCopy.m_data)
            return false;
        source = &sourceCopy;
    }
    else
    {
        source = alphaChannel;
    }

    targetFormat = XImage_alphaVersionForPainting(self->m_data->m_format);
    if (targetFormat != self->m_data->m_format)
    {
        XImage converted;
        XImage_init(&converted);
        XImage_convertToFormat(self, targetFormat, 0, &converted);
        if (XImage_isNull(&converted))
        {
            XImage_deinit_base(&sourceCopy);
            return false;
        }
        XMove(self, &converted);
    }
    XImage_detach(self);
    if (!XImage_isDetached(self))
    {
        XImage_deinit_base(&sourceCopy);
        return false;
    }

    /* qimage.cpp:4569-4576 使用 Alpha8 原图，或把其它来源先转换成
       Grayscale8，再 reinterpretAsFormat(Alpha8)。重解释只改变格式，
       不修改字节，因此 8 位灰度索引图的原始索引语义也得到保留。 */
    XImage_init(&sourceImage);
    if (XImage_format(source) == XImageFormat_Alpha8)
    {
        XCopy(&sourceImage, source);
        sourceImageInitialized = sourceImage.m_data != NULL;
    }
    else if (XImage_depth(source) == 8 && XImage_isGrayscale(source))
    {
        XCopy(&sourceImage, source);
        sourceImageInitialized = sourceImage.m_data != NULL &&
                                 XImage_reinterpretAsFormat(&sourceImage,
                                                            XImageFormat_Alpha8);
    }
    else
    {
        XImage_convertToFormat(source, XImageFormat_Grayscale8, 0, &sourceImage);
        sourceImageInitialized = sourceImage.m_data != NULL &&
                                 XImage_reinterpretAsFormat(&sourceImage,
                                                            XImageFormat_Alpha8);
    }
    if (!sourceImageInitialized)
    {
        XImage_deinit_base(&sourceImage);
        XImage_deinit_base(&sourceCopy);
        return false;
    }

    targetWidth = self->m_data->m_width;
    targetHeight = self->m_data->m_height;
    for (int y = 0; y < targetHeight; ++y)
        for (int x = 0; x < targetWidth; ++x)
        {
            const int sourceWidth = XImage_width(&sourceImage);
            const int sourceHeight = XImage_height(&sourceImage);
            /* QPainter 的非同尺寸路径启用 SmoothPixmapTransform。XGui
               没有 QPainter 光栅管线，这里使用中心附近的最近邻采样，
               对常见 1x1 alpha 图像和同尺寸路径保持精确；复杂缩放的
               插值仍是嵌入式实现边界。 */
            const int sourceX = sourceWidth == targetWidth ? x :
                                (int)(((int64_t)x * sourceWidth) / targetWidth);
            const int sourceY = sourceHeight == targetHeight ? y :
                                (int)(((int64_t)y * sourceHeight) / targetHeight);
            const uint32_t color = XImage_pixel(self, x, y);
            const uint32_t sourcePixel = XImage_pixel(&sourceImage,
                                                      sourceX, sourceY);
            const uint8_t oldAlpha = (uint8_t)(color >> 24);
            const uint8_t newAlpha = (uint8_t)(sourcePixel >> 24);
            /* DestinationIn 的源 alpha 与目标 alpha 相乘；旧实现直接
               覆盖 alpha，在连续调用 setAlphaChannel() 时会错误地把
               第一次设置完全丢掉。Qt 的 8 位饱和乘法等价于下式。 */
            const uint8_t composedAlpha = (uint8_t)(((unsigned)oldAlpha *
                                                     newAlpha + 127u) / 255u);
            XImage_writePixelValue(self->m_data, x, y,
                                   (color & 0x00ffffffu) |
                                   ((uint32_t)composedAlpha << 24));
        }
    XImageData_markDirty(self->m_data);
    XImage_deinit_base(&sourceImage);
    XImage_deinit_base(&sourceCopy);
    return true;
}

static void XImage_initMask(const XImage* source, XImage* out,
                            bool withColorTable)
{
    if (!out) return;
    if (!source || !source->m_data || source->m_data->m_width <= 0 ||
        source->m_data->m_height <= 0 || (const XImage*)out == source)
    {
        XImage_deinit_base(out);
        return;
    }
    /* Qt 的所有 QImage 掩码工厂均返回小端位序 MonoLSB。 */
    XImage_deinit_base(out);
    XImage_init_ex(out, source->m_data->m_width, source->m_data->m_height,
                   XImageFormat_MonoLSB);
    if (!out->m_data) return;
    if (withColorTable)
    {
        /* dither_to_Mono() 使用 color0=白、color1=黑；Alpha 掩码和启发式
           掩码因此与 Qt pixel() 的颜色表语义一致。 */
        const uint32_t qtColors[2] = {0xffffffffu, 0xff000000u};
        XImage_setColorTable(out, qtColors, 2);
    }
    /* 掩码只继承物理元数据；Qt qimage.cpp:3123、3242、3289 调用的
       copyPhysicalMetadata() 只复制 dpmX/dpmY/devicePixelRatio，不传播
       offset、文本、色彩空间或原图颜色表。offset 属于 copyMetadata()，
       不应从源图像泄漏到掩码。 */
    out->m_data->m_dpmX = source->m_data->m_dpmX;
    out->m_data->m_dpmY = source->m_data->m_dpmY;
    out->m_data->m_devicePixelRatio = source->m_data->m_devicePixelRatio;
}

/* Qt's alpha dither flags are part of Qt::ImageConversionFlag.  XImage keeps
 * the public flag type as uint32_t, so retain the Qt bit values locally. */
#define XIMAGE_ALPHA_DITHER_MASK    0x0000000cu
#define XIMAGE_ORDERED_ALPHA_DITHER 0x00000004u
#define XIMAGE_DIFFUSE_ALPHA_DITHER 0x00000008u

static uint8_t XImage_alphaMaskAlpha(const XImage* self, int x, int y)
{
    const XImageData* data;
    const uint8_t* line;
    if (!self || !self->m_data || !self->m_data->m_data ||
        x < 0 || y < 0 || x >= self->m_data->m_width ||
        y >= self->m_data->m_height)
        return 0;
    data = self->m_data;
    line = data->m_data + (size_t)y * (size_t)data->m_bytesPerLine;
    if (data->m_depth == 32)
        /* qimage_conversions.cpp reads *(const uint *)p >> 24 for d == 32. */
        return (uint8_t)(XImage_load32(line + (size_t)x * 4u) >> 24);
    if (data->m_depth == 8)
    {
        if (data->m_format == XImageFormat_Alpha8)
            return line[x];
        if (data->m_format == XImageFormat_Indexed8 && data->m_colorTable &&
            line[x] < (unsigned)data->m_colorCount)
            return (uint8_t)(data->m_colorTable[line[x]] >> 24);
    }
    return (uint8_t)(XImage_pixel(self, x, y) >> 24);
}

static uint32_t XImage_heuristicRgb(const XImage* self, int x, int y)
{
    const XImageData* data;
    const uint8_t* line;
    if (!self || !self->m_data || !self->m_data->m_data ||
        x < 0 || y < 0 || x >= self->m_data->m_width ||
        y >= self->m_data->m_height)
        return 0;
    data = self->m_data;
    if (data->m_depth == 32)
    {
        line = data->m_data + (size_t)y * (size_t)data->m_bytesPerLine;
        /* qimage.cpp's PIX macro intentionally does not un-premultiply. */
        return XImage_load32(line + (size_t)x * 4u) & 0x00ffffffu;
    }
    return XImage_pixel(self, x, y) & 0x00ffffffu;
}

static const uint8_t XImage_bayerMatrix[16][16] = {
    { 0x01, 0xc0, 0x30, 0xf0, 0x0c, 0xcc, 0x3c, 0xfc,
      0x03, 0xc3, 0x33, 0xf3, 0x0f, 0xcf, 0x3f, 0xff },
    { 0x80, 0x40, 0xb0, 0x70, 0x8c, 0x4c, 0xbc, 0x7c,
      0x83, 0x43, 0xb3, 0x73, 0x8f, 0x4f, 0xbf, 0x7f },
    { 0x20, 0xe0, 0x10, 0xd0, 0x2c, 0xec, 0x1c, 0xdc,
      0x23, 0xe3, 0x13, 0xd3, 0x2f, 0xef, 0x1f, 0xdf },
    { 0xa0, 0x60, 0x90, 0x50, 0xac, 0x6c, 0x9c, 0x5c,
      0xa3, 0x63, 0x93, 0x53, 0xaf, 0x6f, 0x9f, 0x5f },
    { 0x08, 0xc8, 0x38, 0xf8, 0x04, 0xc4, 0x34, 0xf4,
      0x0b, 0xcb, 0x3b, 0xfb, 0x07, 0xc7, 0x37, 0xf7 },
    { 0x88, 0x48, 0xb8, 0x78, 0x84, 0x44, 0xb4, 0x74,
      0x8b, 0x4b, 0xbb, 0x7b, 0x87, 0x47, 0xb7, 0x77 },
    { 0x28, 0xe8, 0x18, 0xd8, 0x24, 0xe4, 0x14, 0xd4,
      0x2b, 0xeb, 0x1b, 0xdb, 0x27, 0xe7, 0x17, 0xd7 },
    { 0xa8, 0x68, 0x98, 0x58, 0xa4, 0x64, 0x94, 0x54,
      0xab, 0x6b, 0x9b, 0x7b, 0xa7, 0x67, 0x97, 0x57 },
    { 0x02, 0xc2, 0x32, 0xf2, 0x0e, 0xce, 0x3e, 0xfe,
      0x01, 0xc1, 0x31, 0xf1, 0x0d, 0xcd, 0x3d, 0xfd },
    { 0x82, 0x42, 0xb2, 0x72, 0x8e, 0x4e, 0xbe, 0x7e,
      0x81, 0x41, 0xb1, 0x71, 0x8d, 0x4d, 0xbd, 0x7d },
    { 0x22, 0xe2, 0x12, 0xd2, 0x2e, 0xee, 0x1e, 0xde,
      0x21, 0xe1, 0x11, 0xd1, 0x2d, 0xed, 0x1d, 0xdd },
    { 0xa2, 0x62, 0x92, 0x52, 0xae, 0x6e, 0x9e, 0x5e,
      0xa1, 0x61, 0x91, 0x51, 0xad, 0x6d, 0x9d, 0x5d },
    { 0x0a, 0xca, 0x3a, 0xfa, 0x06, 0xc6, 0x36, 0xf6,
      0x09, 0xc9, 0x39, 0xf9, 0x05, 0xc5, 0x35, 0xf5 },
    { 0x8a, 0x4a, 0xba, 0x7a, 0x86, 0x46, 0xb6, 0x76,
      0x89, 0x49, 0xb9, 0x79, 0x85, 0x45, 0xb5, 0x75 },
    { 0x2a, 0xea, 0x1a, 0xda, 0x26, 0xe6, 0x16, 0xd6,
      0x29, 0xe9, 0x19, 0xd9, 0x25, 0xe5, 0x15, 0xd5 },
    { 0xaa, 0x6a, 0x9a, 0x5a, 0xa6, 0x66, 0x96, 0x56,
      0xa9, 0x69, 0x99, 0x59, 0xa5, 0x65, 0x95, 0x55 }
};

static bool XImage_alphaMaskDitherPixel(const XImage* self, int x, int y,
                                         uint32_t flags)
{
    const uint8_t alpha = XImage_alphaMaskAlpha(self, x, y);
    if ((flags & XIMAGE_ALPHA_DITHER_MASK) == XIMAGE_ORDERED_ALPHA_DITHER)
    {
        /* dither_to_Mono() uses 255 - alpha for 8-bit source data. */
        if (self && self->m_data && self->m_data->m_depth == 8)
            return (uint8_t)(255u - alpha) <
                   XImage_bayerMatrix[x & 15][y & 15];
        return alpha >= XImage_bayerMatrix[x & 15][y & 15];
    }
    return alpha >= 128u;
}

static bool XImage_alphaMaskDither(const XImage* self, uint32_t flags,
                                   XImage* out)
{
    const int width = self->m_data->m_width;
    const int height = self->m_data->m_height;
    const uint32_t alphaMode = flags & XIMAGE_ALPHA_DITHER_MASK;
    if (alphaMode != XIMAGE_DIFFUSE_ALPHA_DITHER)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                XImage_writePixelIndex(out->m_data, x, y,
                                       XImage_alphaMaskDitherPixel(self, x, y,
                                                                    flags) ? 1u : 0u);
        return true;
    }

    if ((size_t)width > SIZE_MAX / (2u * sizeof(int)))
        return false;
    int lineStack[XIMAGE_ALPHA_DITHER_STACK_WIDTH * 2];
    int* line1;
    bool heapLines = false;
    if (width <= XIMAGE_ALPHA_DITHER_STACK_WIDTH)
        line1 = lineStack;
    else
    {
        line1 = (int*)XMalloc_Hybrid((size_t)width * 2u * sizeof(int));
        heapLines = true;
        if (!line1)
            return false;
    }
    int* line2 = line1 + width;
    for (int x = 0; x < width; ++x)
        line2[x] = 255 - XImage_alphaMaskAlpha(self, x, 0);
    for (int y = 0; y < height; ++y)
    {
        int* swap = line1;
        line1 = line2;
        line2 = swap;
        if (y + 1 < height)
            for (int x = 0; x < width; ++x)
                line2[x] = 255 - XImage_alphaMaskAlpha(self, x, y + 1);
        for (int x = 0; x < width; ++x)
        {
            const int value = line1[x];
            const int error = value < 128 ? value : value - 255;
            if (value < 128)
                XImage_writePixelIndex(out->m_data, x, y, 1u);
            if (x + 1 < width)
                line1[x + 1] += (error * 7 + 8) >> 4;
            if (y + 1 < height)
            {
                line2[x] += (error * 5 + 8) >> 4;
                if (x > 0) line2[x - 1] += (error * 3 + 8) >> 4;
                if (x + 1 < width) line2[x + 1] += (error + 0) -
                                                        (((error * 7 + 8) >> 4) +
                                                         ((error * 5 + 8) >> 4) +
                                                         ((error * 3 + 8) >> 4));
            }
        }
    }
    if (heapLines)
        XFree_Hybrid(line1 < line2 ? line1 : line2);
    return true;
}

void XImage_createAlphaMask(const XImage* self, uint32_t flags, XImage* out)
{
    if (!out) return;
    if (!self || !self->m_data || self->m_data->m_format == XImageFormat_RGB32)
    {
        XImage_deinit_base(out);
        return;
    }
    if (self->m_data->m_depth == 1)
    {
        /* Qt routes monochrome images through Indexed8 so the alpha values
           in their two-entry color table participate in dithering. */
        XImage indexed;
        XImage_init(&indexed);
        XImage_convertToFormat(self, XImageFormat_Indexed8, flags, &indexed);
        if (!XImage_isNull(&indexed))
            XImage_createAlphaMask(&indexed, flags, out);
        else
        {
            XImage_deinit_base(out);
        }
        XImage_deinit_base(&indexed);
        return;
    }
    XImage_initMask(self, out, true);
    if (!out || !out->m_data || !self || !self->m_data) return;
    /* Qt qimage_conversions.cpp:1611-1634 根据 AlphaDither_Mask 选择
       Threshold、Ordered 或 Diffuse；默认 flags=0 仍落到 Threshold，
       但显式标志不能被入口静默丢弃。辅助函数同时处理 Alpha8、Indexed8
       调色板 Alpha 以及深度 32 的原始 Alpha 字节。 */
    if (!XImage_alphaMaskDither(self, flags, out))
    {
        /* QImage's failed allocation produces a null result rather than a
           partially initialized mask. */
        XImage_deinit_base(out);
        return;
    }
    XImageData_markDirty(out->m_data);
}

static bool XImage_maskBit(const XImageData* data, int x, int y)
{
    const uint8_t* line;
    uint8_t mask;
    if (!data || !data->m_data || x < 0 || y < 0 ||
        x >= data->m_width || y >= data->m_height)
        return false;
    line = data->m_data + (size_t)y * (size_t)data->m_bytesPerLine;
    mask = (uint8_t)(1u << ((unsigned)x & 7u));
    return (line[(unsigned)x >> 3] & mask) != 0;
}

static void XImage_maskSetBit(XImageData* data, int x, int y, bool value)
{
    uint8_t* line;
    uint8_t mask;
    if (!data || !data->m_data || x < 0 || y < 0 ||
        x >= data->m_width || y >= data->m_height)
        return;
    line = data->m_data + (size_t)y * (size_t)data->m_bytesPerLine;
    mask = (uint8_t)(1u << ((unsigned)x & 7u));
    if (value) line[(unsigned)x >> 3] |= mask;
    else line[(unsigned)x >> 3] &= (uint8_t)~mask;
}

void XImage_createHeuristicMask(const XImage* self, bool clipTight, XImage* out)
{
    uint32_t background;
    int width;
    int height;
    size_t count;
    size_t head;
    size_t tail;
    size_t* queue;
    size_t queueStack[XIMAGE_HEURISTIC_QUEUE_STACK_COUNT];
    bool heapQueue = false;
    if (self && self->m_data && self->m_data->m_depth != 32)
    {
        /* Qt 6.8 首先把非 32 位源图像转换为 RGB32，再运行启发式算法；
           这样索引表、灰度和低位深格式都沿用统一的 RGB 像素解释。 */
        XImage image32;
        XImage_init(&image32);
        XImage_convertToFormat(self, XImageFormat_RGB32, 0, &image32);
        if (!XImage_isNull(&image32))
            XImage_createHeuristicMask(&image32, clipTight, out);
        else
        {
            /* Qt qimage.cpp:3158-3161 returns the result of the temporary
               RGB32 image directly; if that conversion fails, the temporary
               image is null and the mask factory must also return null. */
            XImage_deinit_base(out);
        }
        XImage_deinit_base(&image32);
        return;
    }
    XImage_initMask(self, out, true);
    if (!out || !out->m_data || !self || !self->m_data) return;
    width = self->m_data->m_width;
    height = self->m_data->m_height;
    if (width <= 0 || height <= 0) return;

    /* 与 Qt PIX 宏一致：启发式掩码忽略 Alpha，仅比较 RGB。四角投票
       选择背景色，再从四条边向内剥离连通背景。 */
    background = XImage_heuristicRgb(self, 0, 0);
    if (background != XImage_heuristicRgb(self, width - 1, 0) &&
        background != XImage_heuristicRgb(self, 0, height - 1) &&
        background != XImage_heuristicRgb(self, width - 1, height - 1))
    {
        background = XImage_heuristicRgb(self, width - 1, 0);
        if (background != XImage_heuristicRgb(self, width - 1, height - 1) &&
            background != XImage_heuristicRgb(self, 0, height - 1) &&
            XImage_heuristicRgb(self, 0, height - 1) ==
                XImage_heuristicRgb(self, width - 1, height - 1))
            background = XImage_heuristicRgb(self, width - 1, height - 1);
    }

    /* 输出初始为全不透明，队列中的位清零即表示已剥离的透明背景。 */
    memset(out->m_data->m_data, 0xff,
           (size_t)out->m_data->m_bytesPerLine * (size_t)height);
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return;
    count = (size_t)width * (size_t)height;
    if (count > SIZE_MAX / sizeof(size_t))
    {
        XImage_deinit_base(out);
        return;
    }
    if (count <= XIMAGE_HEURISTIC_QUEUE_STACK_COUNT)
        queue = queueStack;
    else
    {
        queue = (size_t*)XMalloc_Hybrid(count * sizeof(size_t));
        heapQueue = true;
    }
    if (!queue)
    {
        /* QImage returns a null image when the mask allocation fails; do not
           expose the provisional all-opaque buffer as a successful result. */
        XImage_deinit_base(out);
        return;
    }
    head = 0;
    tail = 0;
#define XIMAGE_QUEUE_BACKGROUND(px, py) \
    do { \
        int _qx = (px); int _qy = (py); \
        if (_qx >= 0 && _qy >= 0 && _qx < width && _qy < height && \
            XImage_maskBit(out->m_data, _qx, _qy) && \
            (XImage_heuristicRgb(self, _qx, _qy) == background)) { \
            XImage_maskSetBit(out->m_data, _qx, _qy, false); \
            queue[tail++] = (size_t)_qy * (size_t)width + (size_t)_qx; \
        } \
    } while (0)
    for (int x = 0; x < width; ++x)
    {
        XIMAGE_QUEUE_BACKGROUND(x, 0);
        if (height > 1) XIMAGE_QUEUE_BACKGROUND(x, height - 1);
    }
    for (int y = 1; y + 1 < height; ++y)
    {
        XIMAGE_QUEUE_BACKGROUND(0, y);
        if (width > 1) XIMAGE_QUEUE_BACKGROUND(width - 1, y);
    }
    while (head < tail)
    {
        const size_t index = queue[head++];
        const int x = (int)(index % (size_t)width);
        const int y = (int)(index / (size_t)width);
        XIMAGE_QUEUE_BACKGROUND(x - 1, y);
        XIMAGE_QUEUE_BACKGROUND(x + 1, y);
        XIMAGE_QUEUE_BACKGROUND(x, y - 1);
        XIMAGE_QUEUE_BACKGROUND(x, y + 1);
    }
#undef XIMAGE_QUEUE_BACKGROUND
    if (heapQueue)
        XFree_Hybrid(queue);

    if (!clipTight)
    {
        /* Qt 在非紧致模式下把非背景像素的四邻域也标为不透明。 */
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                if (XImage_heuristicRgb(self, x, y) != background)
                {
                    XImage_maskSetBit(out->m_data, x - 1, y, true);
                    XImage_maskSetBit(out->m_data, x + 1, y, true);
                    XImage_maskSetBit(out->m_data, x, y - 1, true);
                    XImage_maskSetBit(out->m_data, x, y + 1, true);
                }
    }
    XImageData_markDirty(out->m_data);
}

void XImage_createMaskFromColor(const XImage* self, uint32_t color,
                                XImageMaskMode mode, XImage* out)
{
    static const uint32_t qtMaskColors[2] = {0xff000000u, 0xffffffffu};
    const bool raw32 = self && self->m_data && self->m_data->m_depth == 32;
    XImage_initMask(self, out, false);
    if (!out || !out->m_data || !self || !self->m_data) return;
    /* Qt QImageData::create() initializes a MonoLSB table as color0=black,
       color1=white (qimage.cpp:126-135); createMaskFromColor retains that
       default table, unlike the alpha/heuristic factories which replace it
       with their white/black mask convention. */
    XImage_setColorTable(out, qtMaskColors, 2);
    memset(out->m_data->m_data, 0,
           (size_t)out->m_data->m_bytesPerLine *
           (size_t)out->m_data->m_height);
    for (int y = 0; y < self->m_data->m_height; ++y)
        for (int x = 0; x < self->m_data->m_width; ++x)
        {
            bool equal;
            if (raw32)
            {
                /* Qt qimage.cpp:3270-3285 对 depth=32 直接比较扫描行的
                   存储值。这个细节对 ARGB32_Premultiplied 很重要：传入
                   的 QRgb 不会先被反预乘，必须按实际四字节布局比较。 */
                const uint8_t* line = XImage_constScanLine(self, y);
                equal = line && XImage_load32(line + (size_t)x * 4u) == color;
            }
            else
            {
                /* 非 32 位格式使用 Qt 的 pixel() 路径比较完整 ARGB。 */
                equal = XImage_pixel(self, x, y) == color;
            }
            XImage_writePixelIndex(out->m_data, x, y, equal ? 1u : 0u);
        }
    if (mode == XImageMask_OutColor)
    {
        /* QImage::invertPixels() complements the packed bytes, therefore
           trailing padding bits are complemented as well. */
        for (int y = 0; y < out->m_data->m_height; ++y)
        {
            uint8_t* line = out->m_data->m_data +
                (size_t)y * (size_t)out->m_data->m_bytesPerLine;
            for (int byte = 0; byte < out->m_data->m_bytesPerLine; ++byte)
                line[byte] = (uint8_t)~line[byte];
        }
    }
    XImageData_markDirty(out->m_data);
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
    /* Qt qimage.cpp:2448-2460 returns the diagnostic sentinel -12345 for
       a null image or an out-of-range coordinate.  Keep the public C API's
       invalid-coordinate result observable instead of collapsing it to -1. */
    if (!self || !self->m_data || !self->m_data->m_data) return -12345;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return -12345;
    const uint8_t* line = XImage_constScanLine(self, y);
    if (!line) return -12345;
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

static uint16_t XImage_unpremultiply16(uint16_t value, uint16_t alpha)
{
    uint64_t result;
    if (alpha == 0 || alpha == 65535) return value;
    result = ((uint64_t)value * 65535u + alpha / 2u) / alpha;
    return (uint16_t)(result > 65535u ? 65535u : result);
}

static uint16_t XImage_repremultiply30(uint16_t value, uint16_t alpha)
{
    uint16_t unpremultiplied;
    uint16_t reducedAlpha;
    if (alpha == 0 || alpha == 65535)
        return value;
    unpremultiplied = XImage_unpremultiply16(value, alpha);
    reducedAlpha = (uint16_t)(21845u * ((unsigned)alpha >> 14));
    return XImage_premultiply16(unpremultiplied, reducedAlpha);
}

static void XImage_writePixelColor16(XImageData* d, int x, int y,
                                     uint16_t red, uint16_t green,
                                     uint16_t blue, uint16_t alpha)
{
    uint8_t* line;
    uint8_t* pixel;
    float redF, greenF, blueF, alphaF;
    if (!d || !d->m_data || x < 0 || y < 0 || x >= d->m_width || y >= d->m_height)
        return;
    line = d->m_data + (size_t)y * (size_t)d->m_bytesPerLine;
    switch (d->m_format)
    {
        case XImageFormat_BGR30:
        case XImageFormat_RGB30:
        {
            uint32_t value = ((uint32_t)red >> 6) |
                             (((uint32_t)green >> 6) << 10) |
                             (((uint32_t)blue >> 6) << 20) | 0xc0000000u;
            if (d->m_format == XImageFormat_RGB30)
                value = ((uint32_t)red >> 6) << 20 |
                        ((uint32_t)green >> 6) << 10 |
                        ((uint32_t)blue >> 6) | 0xc0000000u;
            XImage_store32(line + (size_t)x * 4u, value);
            return;
        }
        case XImageFormat_A2BGR30_Premultiplied:
        case XImageFormat_A2RGB30_Premultiplied:
        {
            uint16_t premultRed = XImage_premultiply16(red, alpha);
            uint16_t premultGreen = XImage_premultiply16(green, alpha);
            uint16_t premultBlue = XImage_premultiply16(blue, alpha);
            uint32_t packedRed = (uint32_t)(XImage_repremultiply30(premultRed, alpha) >> 6);
            uint32_t packedGreen = (uint32_t)(XImage_repremultiply30(premultGreen, alpha) >> 6);
            uint32_t packedBlue = (uint32_t)(XImage_repremultiply30(premultBlue, alpha) >> 6);
            uint32_t packedAlpha = (uint32_t)(alpha >> 14);
            uint32_t value;
            if (d->m_format == XImageFormat_A2BGR30_Premultiplied)
                value = (packedAlpha << 30) | (packedBlue << 20) |
                        (packedGreen << 10) | packedRed;
            else
                value = (packedAlpha << 30) | (packedRed << 20) |
                        (packedGreen << 10) | packedBlue;
            XImage_store32(line + (size_t)x * 4u, value);
            return;
        }
        case XImageFormat_RGBX64:
        case XImageFormat_RGBA64:
        case XImageFormat_RGBA64_Premultiplied:
            pixel = line + (size_t)x * 8u;
            if (d->m_format == XImageFormat_RGBA64_Premultiplied)
            {
                XImage_store16(pixel, XImage_premultiply16(red, alpha));
                XImage_store16(pixel + 2u, XImage_premultiply16(green, alpha));
                XImage_store16(pixel + 4u, XImage_premultiply16(blue, alpha));
            }
            else
            {
                XImage_store16(pixel, red);
                XImage_store16(pixel + 2u, green);
                XImage_store16(pixel + 4u, blue);
            }
            XImage_store16(pixel + 6u, d->m_format == XImageFormat_RGBX64 ? 65535u : alpha);
            return;
        case XImageFormat_Grayscale16:
        {
            uint16_t gray;
            /* Qt's destStore64Gray16 keeps an already-gray QRgba64 channel
             * verbatim. Preserve that branch before the 8-bit fallback so
             * values such as 0x1234 do not become 0x1212. For a colored
             * value retain the compact implementation's sRGB luma model,
             * widened directly to the native 16-bit range. */
            if (red == green && red == blue)
                gray = red;
            else
            {
                uint64_t weighted = 299u * (uint64_t)red +
                                    587u * (uint64_t)green +
                                    114u * (uint64_t)blue + 500u;
                gray = (uint16_t)(weighted / 1000u);
            }
            XImage_store16(line + (size_t)x * 2u, gray);
            (void)alpha;
            return;
        }
        case XImageFormat_RGBX16FPx4:
        case XImageFormat_RGBA16FPx4:
        case XImageFormat_RGBA16FPx4_Premultiplied:
            redF = (float)red / 65535.0f;
            greenF = (float)green / 65535.0f;
            blueF = (float)blue / 65535.0f;
            alphaF = d->m_format == XImageFormat_RGBX16FPx4
                ? 1.0f : (float)alpha / 65535.0f;
            if (d->m_format == XImageFormat_RGBA16FPx4_Premultiplied)
            {
                redF *= alphaF;
                greenF *= alphaF;
                blueF *= alphaF;
            }
            pixel = line + (size_t)x * 8u;
            XImage_store16(pixel, XImage_floatToHalf(redF));
            XImage_store16(pixel + 2u, XImage_floatToHalf(greenF));
            XImage_store16(pixel + 4u, XImage_floatToHalf(blueF));
            XImage_store16(pixel + 6u, XImage_floatToHalf(alphaF));
            return;
        case XImageFormat_RGBX32FPx4:
        case XImageFormat_RGBA32FPx4:
        case XImageFormat_RGBA32FPx4_Premultiplied:
            redF = (float)red / 65535.0f;
            greenF = (float)green / 65535.0f;
            blueF = (float)blue / 65535.0f;
            alphaF = d->m_format == XImageFormat_RGBX32FPx4
                ? 1.0f : (float)alpha / 65535.0f;
            if (d->m_format == XImageFormat_RGBA32FPx4_Premultiplied)
            {
                redF *= alphaF;
                greenF *= alphaF;
                blueF *= alphaF;
            }
            pixel = line + (size_t)x * 16u;
            memcpy(pixel, &redF, sizeof(float));
            memcpy(pixel + 4u, &greenF, sizeof(float));
            memcpy(pixel + 8u, &blueF, sizeof(float));
            memcpy(pixel + 12u, &alphaF, sizeof(float));
            return;
        default:
            return;
    }
}

/**
 * @brief 从 16 位无预乘分量构造 XColor。
 * @param red 红色分量，范围为 0~65535。
 * @param green 绿色分量，范围为 0~65535。
 * @param blue 蓝色分量，范围为 0~65535。
 * @param alpha Alpha 分量，范围为 0~65535。
 * @return 保留完整 16 位精度的 RGB 颜色。
 * @note QColor::pixelColor() 对高位深图像直接返回 QRgba64；不能先经过
 *       8 位 ARGB 中间值，否则低 8 位会在读取时永久丢失。
 */
static XColor XImage_colorFrom16(uint16_t red, uint16_t green,
                                 uint16_t blue, uint16_t alpha)
{
    XColor color = XColor_create();
    color.m_spec = XColor_Rgb;
    color.m_alpha = alpha;
    color.m_comp1 = red;
    color.m_comp2 = green;
    color.m_comp3 = blue;
    color.m_comp4 = 0;
    return color;
}

static uint16_t XImage_premultiply16(uint16_t value, uint16_t alpha)
{
    uint64_t product = (uint64_t)value * alpha;
    return (uint16_t)((product + (product >> 16) + 0x8000u) >> 16);
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
    /* Qt qimage.cpp:2489-2495 returns 12345 when the coordinate is invalid.
       Internal callers validate coordinates before sampling, so preserving
       this sentinel only changes the documented public boundary behavior. */
    if (!self || !self->m_data || !XImage_valid(self, x, y))
        return 12345u;
    return XImage_readPixelValue(self->m_data, x, y);
}

void XImage_setPixel(XImage* self, int x, int y, uint32_t indexOrRgb)
{
    if (!self || !self->m_data || !self->m_data->m_data) return;
    if (x < 0 || x >= self->m_data->m_width || y < 0 || y >= self->m_data->m_height)
        return;
    /* Qt qimage.cpp:2593-2614 rejects out-of-range palette indices instead
       of truncating them to the storage width.  Keep the existing pixel
       untouched for invalid Mono/MonoLSB and Indexed8 requests. */
    if ((self->m_data->m_format == XImageFormat_Mono ||
         self->m_data->m_format == XImageFormat_MonoLSB) && indexOrRgb > 1u)
        return;
    if (self->m_data->m_format == XImageFormat_Indexed8 &&
        indexOrRgb >= (uint32_t)(self->m_data->m_colorCount < 0
                                 ? 0 : self->m_data->m_colorCount))
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
        XMove(out, &temp);
        return;
    }
    int w = self->m_data->m_width, h = self->m_data->m_height;
    int rx = 0, ry = 0, rw = w, rh = h;
    /* QRect::isNull() is distinct from isEmpty(): a rectangle with both
       dimensions equal to zero means "copy the whole image", regardless of
       its origin.  This is the overload's documented Qt behavior
       (qimage.cpp:1218-1240), while a rectangle with only one zero or a
       negative dimension remains an empty request below. */
    if (rect && !(rect->width == 0 && rect->height == 0))
    {
        rx = rect->x;
        ry = rect->y;
        rw = rect->width;
        rh = rect->height;
    }
    if (rw <= 0 || rh <= 0)
    {
        XImage_deinit_base(out);
        return;
    }
    XImage_deinit_base(out);
    XImage_init_ex(out, rw, rh, self->m_data->m_format);
    if (!out->m_data) return;
    XImageData_copyMetadata(out->m_data, self->m_data);
    if (self->m_data->m_colorCount > 0 && self->m_data->m_colorTable)
    {
        out->m_data->m_colorTable = (uint32_t*)XMalloc_System((size_t)self->m_data->m_colorCount * sizeof(uint32_t));
        if (!out->m_data->m_colorTable) { XImage_deinit_base(out); return; }
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
        XMove(out, &temp);
        return;
    }
    XImage_deinit_base(out);
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
    XImageData_copyMetadata(out->m_data, self->m_data);
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
            /* Keep a high-precision intermediate whenever the destination
             * can represent one.  This covers mixed-format conversions such
             * as ARGB32 -> RGBA32FPx4 and RGBA32FPx4 -> RGBA64; routing those
             * through XImage_readColorValue() would quantize the source to
             * eight bits before the destination writer sees it. */
            if (XImage_isNativeFloatColorFormat(format) ||
                XImage_isNative16ColorFormat(format))
            {
                float sourceFloat[4];
                if (XImage_readNativeFloat(self->m_data, x, y,
                                           &sourceFloat[0], &sourceFloat[1],
                                           &sourceFloat[2], &sourceFloat[3]))
                {
                    if (XImage_isNativeFloatColorFormat(format))
                        XImage_writeNativeFloat(out->m_data, x, y,
                                                sourceFloat[0], sourceFloat[1],
                                                sourceFloat[2], sourceFloat[3]);
                    else
                        XImage_writeNative16(
                            out->m_data, x, y,
                            XImage_colorSpaceChannel16(sourceFloat[0]),
                            XImage_colorSpaceChannel16(sourceFloat[1]),
                            XImage_colorSpaceChannel16(sourceFloat[2]),
                            XImage_colorSpaceChannel16(sourceFloat[3]));
                }
                continue;
            }
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
         format != XImageFormat_Indexed8))
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
    XMove(self, &temp);
    return true;
}

bool XImage_reinterpretAsFormat(XImage* self, XImageFormat format)
{
    if (!self || !self->m_data || format <= XImageFormat_Invalid || format >= XImageFormat_NImageFormats) return false;
    if (self->m_data->m_format == format)
        return true;
    /* QImage only permits a reinterpretation when the storage depth is
     * unchanged.  Detach shared data, but keep a unique image's cache key
     * because the bytes themselves are not modified. */
    if (XImageFormat_bitDepth(self->m_data->m_format) != XImageFormat_bitDepth(format))
        return false;
    if (!XImage_isDetached(self))
        XImage_detach(self);
    if (!XImage_isDetached(self))
        return false;
    self->m_data->m_format = format;
    self->m_data->m_depth = XImageFormat_bitDepth(format);
    return true;
}

void XImage_mirrored(const XImage* self, bool horizontal, bool vertical, XImage* out)
{
    if (self && out && (!horizontal && !vertical ||
                        (self->m_data && self->m_data->m_width <= 1 && self->m_data->m_height <= 1)))
    {
        if ((const XImage*)out != self) XCopy(out, self);
        return;
    }
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_mirrored(self, horizontal, vertical, &temp);
        if (temp.m_data)
            XMove(out, &temp);
        return;
    }
    if (!out) return;
    if (!self || !self->m_data || !self->m_data->m_data)
    {
        XImage_deinit_base(out);
        return;
    }
    XImage_deinit_base(out);
    XImage_init_ex(out, self->m_data->m_width, self->m_data->m_height, self->m_data->m_format);
    if (!out->m_data) return;
    XImageData_copyMetadata(out->m_data, self->m_data);
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
    /* Qt qimage.cpp:3504-3507 在没有变换或图像只有一个像素时直接返回。
     * 该快路径必须保留当前共享数据和 cacheKey，不能为了“原地”接口额外
     * 克隆一份相同图像。 */
    if ((!horizontal && !vertical) ||
        (self->m_data->m_width <= 1 && self->m_data->m_height <= 1))
        return;
    XImage temp;
    XImage_init(&temp);
    XImage_mirrored(self, horizontal, vertical, &temp);
    if (temp.m_data)
        XMove(self, &temp);
}

void XImage_mirror(XImage* self, bool horizontal, bool vertical)
{
    XImage_mirroredInPlace(self, horizontal, vertical);
}

void XImage_rgbSwapped(const XImage* self, XImage* out)
{
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_rgbSwapped(self, &temp);
        if (temp.m_data)
            XMove(out, &temp);
        return;
    }
    if (!out) return;
    if (self && self->m_data && self->m_data->m_data &&
        (self->m_data->m_format == XImageFormat_Alpha8 ||
         self->m_data->m_format == XImageFormat_Grayscale8 ||
         self->m_data->m_format == XImageFormat_Grayscale16))
    {
        /* Qt qimage.cpp:3584-3588 返回 *this；这些格式没有可交换的
         * 红蓝通道，必须共享原数据而不是制造新的 cacheKey。 */
        XCopy(out, self);
        return;
    }
    XImage_deinit_base(out);
    if (!self || !self->m_data || !self->m_data->m_data) return;
    out->m_data = XImageData_clone(self->m_data);
    if (!out->m_data) return;
    int w = self->m_data->m_width;
    int h = self->m_data->m_height;
    /* Qt qimage.cpp:3595-3601 对 Mono、MonoLSB 与 Indexed8 统一交换调色表；
     * 位图索引本身不变，只有颜色表中的红蓝分量互换。 */
    if ((self->m_data->m_format == XImageFormat_Indexed8 ||
         self->m_data->m_format == XImageFormat_Mono ||
         self->m_data->m_format == XImageFormat_MonoLSB) &&
        out->m_data->m_colorTable)
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
             self->m_data->m_format != XImageFormat_Alpha8 &&
             self->m_data->m_format != XImageFormat_Grayscale8 &&
             self->m_data->m_format != XImageFormat_Grayscale16)
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
    XMove(self, &temp);
}

void XImage_rgbSwap(XImage* self)
{
    XImage_rgbSwappedInPlace(self);
}

void XImage_scaled(const XImage* self, int width, int height, uint32_t aspectMode, uint32_t mode, XImage* out)
{
    if (!out) return;
    if (self && (const XImage*)out == self)
    {
        XImage temp;
        XImage_init(&temp);
        XImage_scaled(self, width, height, aspectMode, mode, &temp);
        XMove(out, &temp);
        return;
    }
    XImage_deinit_base(out);
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
    /* Qt QImage::scaled() returns *this when the requested size resolves to
     * the current size (qimage.cpp:3009-3031).  Preserve implicit sharing and
     * cacheKey in that case instead of allocating an identical pixel buffer. */
    if (targetWidth == sw && targetHeight == sh)
    {
        XCopy(out, self);
        return;
    }
    out->m_data = XImageData_create(targetWidth, targetHeight, self->m_data->m_format,
                                     0, NULL, NULL, NULL);
    if (!out->m_data) return;
    /* Qt QImage::scaled() delegates to transformed(), whose output receives
     * the complete copyMetadata() set.  Keep color space and text metadata
     * alongside physical resolution, DPR and offset; copying only the latter
     * silently changes the meaning of a scaled image. */
    XImageData_copyMetadata(out->m_data, self->m_data);
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
    /* Qt QImage::scaledToWidth() returns a null image for a null source or
       non-positive width.  Replace an existing destination safely instead
       of reinitializing it over a live shared-data reference. */
    if (!self || !self->m_data || width <= 0) {
        XImage_deinit_base(out);
        return;
    }
    int64_t scaledHeight = ((int64_t)self->m_data->m_height * width + self->m_data->m_width / 2) / self->m_data->m_width;
    if (scaledHeight < 1) scaledHeight = 1;
    if (scaledHeight > INT_MAX) {
        XImage_deinit_base(out);
        return;
    }
    int height = (int)scaledHeight;
    XImage_scaled(self, width, height, 0, mode, out);
}

void XImage_scaledToHeight(const XImage* self, int height, uint32_t mode, XImage* out)
{
    if (!out) return;
    /* 与 scaledToWidth() 相同，非法高度必须安全地把已存在的目标
       替换为空图像，不能覆盖其 m_data 指针造成泄漏。 */
    if (!self || !self->m_data || height <= 0) {
        XImage_deinit_base(out);
        return;
    }
    int64_t scaledWidth = ((int64_t)self->m_data->m_width * height + self->m_data->m_height / 2) / self->m_data->m_height;
    if (scaledWidth < 1) scaledWidth = 1;
    if (scaledWidth > INT_MAX) {
        XImage_deinit_base(out);
        return;
    }
    int width = (int)scaledWidth;
    XImage_scaled(self, width, height, 0, mode, out);
}

/* 将旧六参数仿射结构归一化为 Qt QTransform 同布局的 3x3 矩阵。 */
static void XImage_transformMatrix(const XImageTransform* matrix, float out[9])
{
    static const float identity[9] = {1.0f, 0.0f, 0.0f,
                                      0.0f, 1.0f, 0.0f,
                                      0.0f, 0.0f, 1.0f};
    if (!matrix) {
        memcpy(out, identity, sizeof(identity));
        return;
    }
    out[0] = matrix->m11;
    out[1] = matrix->m21;
    out[2] = matrix->dx;
    out[3] = matrix->m12;
    out[4] = matrix->m22;
    out[5] = matrix->dy;
    out[6] = matrix->m13;
    out[7] = matrix->m23;
    out[8] = matrix->m33;
    /* C aggregate initializers from the old API leave the new fields zero. */
    if (fabsf(out[6]) < 1.0e-8f && fabsf(out[7]) < 1.0e-8f &&
        fabsf(out[8]) < 1.0e-8f)
        out[8] = 1.0f;
}

static bool XImage_transformInverse(const float matrix[9], float inverse[9])
{
    float c00 = matrix[4] * matrix[8] - matrix[5] * matrix[7];
    float c01 = matrix[2] * matrix[7] - matrix[1] * matrix[8];
    float c02 = matrix[1] * matrix[5] - matrix[2] * matrix[4];
    float c10 = matrix[5] * matrix[6] - matrix[3] * matrix[8];
    float c11 = matrix[0] * matrix[8] - matrix[2] * matrix[6];
    float c12 = matrix[2] * matrix[3] - matrix[0] * matrix[5];
    float c20 = matrix[3] * matrix[7] - matrix[4] * matrix[6];
    float c21 = matrix[1] * matrix[6] - matrix[0] * matrix[7];
    float c22 = matrix[0] * matrix[4] - matrix[1] * matrix[3];
    float determinant = matrix[0] * c00 + matrix[1] * c10 + matrix[2] * c20;
    if (fabsf(determinant) < 1.0e-8f || !isfinite(determinant))
        return false;
    inverse[0] = c00 / determinant; inverse[1] = c01 / determinant; inverse[2] = c02 / determinant;
    inverse[3] = c10 / determinant; inverse[4] = c11 / determinant; inverse[5] = c12 / determinant;
    inverse[6] = c20 / determinant; inverse[7] = c21 / determinant; inverse[8] = c22 / determinant;
    return true;
}

void XImage_trueMatrix(const XImageTransform* matrix, int width, int height,
                       XImageTransform* out, XSize* transformedSize)
{
    float m[9];
    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
    bool valid = true;
    XImage_transformMatrix(matrix, m);
    for (int i = 0; i < 4; ++i)
    {
        float x = (i & 1) ? (float)width : 0.0f;
        float y = (i & 2) ? (float)height : 0.0f;
        float denominator = m[6] * x + m[7] * y + m[8];
        float transformedX;
        float transformedY;
        if (fabsf(denominator) < 1.0e-8f || !isfinite(denominator)) {
            valid = false;
            break;
        }
        transformedX = (m[0] * x + m[1] * y + m[2]) / denominator;
        transformedY = (m[3] * x + m[4] * y + m[5]) / denominator;
        if (!isfinite(transformedX) || !isfinite(transformedY)) {
            valid = false;
            break;
        }
        if (i == 0 || transformedX < minX) minX = transformedX;
        if (i == 0 || transformedY < minY) minY = transformedY;
        if (i == 0 || transformedX > maxX) maxX = transformedX;
        if (i == 0 || transformedY > maxY) maxY = transformedY;
    }
    if (!valid) {
        if (out) memset(out, 0, sizeof(*out));
        if (transformedSize) { transformedSize->width = 0; transformedSize->height = 0; }
        return;
    }
    int resultWidth = (int)ceilf(maxX - floorf(minX));
    int resultHeight = (int)ceilf(maxY - floorf(minY));
    if (resultWidth < 0) resultWidth = 0;
    if (resultHeight < 0) resultHeight = 0;
    if (out)
    {
        out->m11 = m[0] - floorf(minX) * m[6];
        out->m21 = m[1] - floorf(minX) * m[7];
        out->dx  = m[2] - floorf(minX) * m[8];
        out->m12 = m[3] - floorf(minY) * m[6];
        out->m22 = m[4] - floorf(minY) * m[7];
        out->dy  = m[5] - floorf(minY) * m[8];
        out->m13 = m[6]; out->m23 = m[7]; out->m33 = m[8];
    }
    if (transformedSize) {
        transformedSize->width = resultWidth;
        transformedSize->height = resultHeight;
    }
}

void XImage_transformed(const XImage* self, const XImageTransform* matrix,
                        uint32_t mode, XImage* out)
{
    XImageTransform adjusted;
    XSize size;
    float transform[9];
    float inverse[9];
    (void)mode;
    if (!out) return;
    /* QImage::transformed returns a value; preserve the source when the C
     * output parameter aliases it. */
    if (self == out && self)
    {
        XImage sourceCopy;
        XImage_init(&sourceCopy);
        XCopy(&sourceCopy, self);
        XImage_transformed(&sourceCopy, matrix, mode, out);
        XImage_deinit_base(&sourceCopy);
        return;
    }
    XImage_trueMatrix(matrix, self ? XImage_width(self) : 0,
                      self ? XImage_height(self) : 0, &adjusted, &size);
    if (!self || !self->m_data || size.width <= 0 || size.height <= 0)
    {
        XImage_deinit_base(out);
        return;
    }
    XImage_transformMatrix(&adjusted, transform);
    if (!XImage_transformInverse(transform, inverse))
    {
        XImage_deinit_base(out);
        return;
    }
    XImage_deinit_base(out);
    XImage_init_ex(out, size.width, size.height, self->m_data->m_format);
    if (!out->m_data) return;
    XImageData_copyMetadata(out->m_data, self->m_data);
    for (int y = 0; y < size.height; ++y)
        for (int x = 0; x < size.width; ++x)
        {
            float tx = (float)x + 0.5f;
            float ty = (float)y + 0.5f;
            float sourceW = inverse[6] * tx + inverse[7] * ty + inverse[8];
            float sourceX;
            float sourceY;
            if (fabsf(sourceW) < 1.0e-8f || !isfinite(sourceW)) continue;
            sourceX = (inverse[0] * tx + inverse[1] * ty + inverse[2]) / sourceW - 0.5f;
            sourceY = (inverse[3] * tx + inverse[4] * ty + inverse[5]) / sourceW - 0.5f;
            int sx = (int)floorf(sourceX + 0.5f);
            int sy = (int)floorf(sourceY + 0.5f);
            if (sx >= 0 && sy >= 0 && sx < self->m_data->m_width && sy < self->m_data->m_height)
            {
                if (self->m_data->m_format == XImageFormat_Indexed8 || self->m_data->m_format == XImageFormat_Mono || self->m_data->m_format == XImageFormat_MonoLSB)
                    XImage_writePixelIndex(out->m_data, x, y, (uint32_t)XImage_pixelIndex(self, sx, sy));
                else
                    XImage_writePixelValue(out->m_data, x, y, XImage_pixel(self, sx, sy));
            }
        }
}

/* ========== 文件操作 ========== */

bool XImage_load_2(XImage* self, const char* fileName, const char* format)
{
    XString* path;
    XFile* file;
    XByteArray* bytes;
    XImage decoded;
    bool result = false;
    bool hasExplicitFormat;
    if (!self || !fileName) return false;
    path = XString_create_utf8(fileName);
    file = path ? XFile_create_2(path) : NULL;
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        if (path) XString_delete_base((XClass*)path);
        return false;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!bytes || XByteArray_size_base((const XContainer*)bytes) > INT_MAX) {
        if (bytes) XByteArray_delete_base((XClass*)bytes);
        if (path) XString_delete_base((XClass*)path);
        return false;
    }

    /* QImage::load(fileName, nullptr) 先让读取器依据最终路径后缀选取
       处理器，后缀处理器拒绝数据时再回退到文件头探测。直接把文件
       内容交给 XImage_loadFromData() 会跳过这一步，XPM 等无可靠二进制
       魔数的格式因此无法获得 Qt 的后缀优先行为。 */
    XImage_init(&decoded);
    hasExplicitFormat = format && format[0];
    if (hasExplicitFormat) {
        result = XImage_loadFromData_2(
            &decoded, XByteArray_data(bytes),
            (int)XByteArray_size_base((const XContainer*)bytes), format);
    } else {
        const char* base = strrchr(fileName, '/');
        const char* backslash = strrchr(fileName, '\\');
        const char* dot;
        const char* suffix = NULL;
        if (backslash && (!base || backslash > base))
            base = backslash;
        base = base ? base + 1 : fileName;
        dot = strrchr(base, '.');
        if (dot && dot[1])
            suffix = dot + 1;
        if (suffix && XImageCodec_formatFromName_2(suffix) !=
                          XImageCodecFormat_Unknown) {
            result = XImage_loadFromData_2(
                &decoded, XByteArray_data(bytes),
                (int)XByteArray_size_base((const XContainer*)bytes), suffix);
        }
        if (!result) {
            /* 每次失败都将临时图像恢复为空，随后按内容探测；这也
               保持 XImage_loadFromData_2() 的失败失效契约。 */
            XImage_deinit_base(&decoded);
            result = XImage_loadFromData_2(
                &decoded, XByteArray_data(bytes),
                (int)XByteArray_size_base((const XContainer*)bytes), NULL);
        }
    }
    if (result) {
        XMove(self, &decoded);
    } else {
        /* 与 QImage::load() 一致，失败结果替换为 null 图像。 */
        XImage_deinit_base(self);
        XImage_deinit_base(&decoded);
    }
    XByteArray_delete_base((XClass*)bytes);
    if (path) XString_delete_base((XClass*)path);
    return result;
}

bool XImage_load(XImage* self, const XString* fileName, const XString* format)
{
    return XImage_load_2(self, XString_toUtf8(fileName), XString_toUtf8(format));
}

bool XImage_loadFromData_2(XImage* self, const uint8_t* data, int len, const char* format)
{
    XImage decoded;
    bool result = false;
    if (!self) return false;
    /* QImage::loadFromData() assigns the temporary result back to *this;
     * failed decoding therefore invalidates the destination instead of
     * leaving stale pixels.  Decode into a temporary first so a partially
     * written codec can never leak data into the caller's image. */
    XImage_init(&decoded);
    if (!data || len <= 0) goto failed;
#if XIMAGECODEC_ON
    {
        XImageCodecFormat codecFormat = XImageCodec_formatFromName_2(format);
        /* An explicit format is authoritative in Qt.  Unknown names fail
         * directly; content probing is reserved for an omitted/empty name. */
        if (format && format[0] && codecFormat == XImageCodecFormat_Unknown)
            goto failed;
        if (codecFormat == XImageCodecFormat_Unknown)
            codecFormat = XImageCodec_detect(data, (size_t)len);
        result = XImageCodec_decode(data, (size_t)len, codecFormat, &decoded);
    }
#endif
    if (!result) goto failed;
    XMove(self, &decoded);
    return true;

failed:
    XImage_deinit_base(&decoded);
    XImage_deinit_base(self);
    return false;
}

bool XImage_loadFromData(XImage* self, const uint8_t* data, int len, const XString* format)
{
    return XImage_loadFromData_2(self, data, len, XString_toUtf8(format));
}

bool XImage_save_2(const XImage* self, const char* fileName, const char* format, int quality)
{
    XString* path; XString* type; XString* extension = NULL; XFile* file = NULL; XByteArray* bytes = NULL; bool ok = false;
    if (!self || !fileName) return false;
    path = XString_create_utf8(fileName);
    /* QImageWriter treats a null or empty QByteArray format identically:
       when writing a file, both select the final filename suffix. */
    type = (format && format[0]) ? XString_create_utf8(format) : NULL;
    if (!type) {
        const char* base = strrchr(fileName, '/');
        const char* backslash = strrchr(fileName, '\\');
        const char* dot;
        if (backslash && (!base || backslash > base))
            base = backslash;
        base = base ? base + 1 : fileName;
        dot = strrchr(base, '.');
        extension = dot && dot[1] ? XString_create_utf8(dot + 1) : NULL;
    }
#if XIMAGECODEC_ON
    {
        const XString* selectedType = type ? type : extension;
        XImageCodecFormat codecFormat = XImageCodec_formatFromName(selectedType);
        bool encoded = false;
        file = path ? XFile_create_2(path) : NULL; bytes = XByteArray_create();
#if XIMAGECODEC_PPM_ON
        if (codecFormat == XImageCodecFormat_Ppm)
            encoded = bytes && XImageCodecInternal_encodePpmSubtype(
                self, XString_toUtf8(selectedType), bytes);
        else
#endif
#if XIMAGECODEC_XBM_ON
        if (codecFormat == XImageCodecFormat_Xbm)
            encoded = bytes && XImageCodecInternal_encodeXbmNamed(
                self, fileName, bytes);
        else
#endif
#if XIMAGECODEC_XPM_ON
        if (codecFormat == XImageCodecFormat_Xpm)
            encoded = bytes && XImageCodecInternal_encodeXpmNamed(
                self, fileName, bytes);
        else
#endif
            encoded = bytes && XImageCodec_encode(self, codecFormat, quality, bytes);
        ok = file && encoded && codecFormat != XImageCodecFormat_Unknown &&
             XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly |
                                 XIODevice_Truncate | XIODevice_Create) &&
             XIODevice_write_1((XIODevice*)file,
                 (const char*)XByteArray_data(bytes),
                 (int64_t)XByteArray_size_base((const XContainer*)bytes)) ==
                 (int64_t)XByteArray_size_base((const XContainer*)bytes);
    }
#endif
    if (file) { XIODevice_close_base((XIODevice*)file); XClass_delete_base((XClass*)file); } if (bytes) XByteArray_delete_base((XClass*)bytes); if (path) XString_delete_base((XClass*)path); if (type) XString_delete_base((XClass*)type); if (extension) XString_delete_base((XClass*)extension); return ok;
}

bool XImage_save(const XImage* self, const XString* fileName, const XString* format, int quality)
{
    return XImage_save_2(self, XString_toUtf8(fileName), XString_toUtf8(format), quality);
}

bool XImage_loadDevice_2(XImage* self, XIODevice* device, const char* format)
{
    XByteArray* bytes;
    bool result;
    if (!self || !device || !XIODevice_isOpen(device) || !XIODevice_isReadable(device)) return false;
    bytes = XIODevice_readAll_3(device);
    if (!bytes) return false;
    result = XImage_loadFromData_2(self, XByteArray_data(bytes),
                                 (int)XByteArray_size_base((const XContainer*)bytes), format);
    XByteArray_delete_base((XClass*)bytes);
    return result;
}

bool XImage_loadDevice(XImage* self, XIODevice* device, const XString* format)
{
    return XImage_loadDevice_2(self, device, XString_toUtf8(format));
}

bool XImage_saveDevice_2(const XImage* self, XIODevice* device, const char* format, int quality)
{
    XString* type = format ? XString_create_utf8(format) : NULL; XByteArray* bytes = XByteArray_create(); bool openedHere = false, ok = false;
#if XIMAGECODEC_ON
    { XImageCodecFormat codecFormat = XImageCodec_formatFromName(type);
      bool encoded = false;
#if XIMAGECODEC_PPM_ON
      if (codecFormat == XImageCodecFormat_Ppm)
          encoded = bytes && XImageCodecInternal_encodePpmSubtype(
              self, XString_toUtf8(type), bytes);
      else
#endif
#if XIMAGECODEC_XPM_ON
      if (codecFormat == XImageCodecFormat_Xpm)
          encoded = bytes && XImageCodecInternal_encodeXpmNamed(
              self, XString_toUtf8(type), bytes);
      else
#endif
          encoded = bytes && XImageCodec_encode(self, codecFormat, quality, bytes);
      if (self && device && codecFormat != XImageCodecFormat_Unknown && encoded) {
          if (!XIODevice_isOpen(device))
              openedHere = XIODevice_open_base(device,
                  XIODevice_WriteOnly | XIODevice_Truncate);
          ok = XIODevice_isWritable(device) &&
               XIODevice_write_1(device, (const char*)XByteArray_data(bytes),
                   (int64_t)XByteArray_size_base((const XContainer*)bytes)) ==
                   (int64_t)XByteArray_size_base((const XContainer*)bytes);
          if (openedHere) XIODevice_close_base(device);
      }
    }
#endif
    if (bytes) XByteArray_delete_base((XClass*)bytes); if (type) XString_delete_base((XClass*)type); return ok;
}

bool XImage_saveDevice(const XImage* self, XIODevice* device, const XString* format, int quality)
{
    return XImage_saveDevice_2(self, device, XString_toUtf8(format), quality);
}

int XImage_textCount(const XImage* self)
{
    return self && self->m_data ? (int)XStringList_size_base((const XContainer*)&self->m_data->m_textKeys) : 0;
}

XStringList* XImage_textKeys(const XImage* self)
{
    if (!self || !self->m_data)
        return XStringList_create();
    return XStringList_create_copy(&self->m_data->m_textKeys);
}

const char* XImage_textKey_2(const XImage* self, int index)
{
    return XString_toUtf8(XImage_textKey_const(self, index));
}

const XString* XImage_textKey_const(const XImage* self, int index)
{
    if (!self || !self->m_data || index < 0 || index >= XImage_textCount(self)) return NULL;
    return (const XString*)XStringList_at_base((const XVector*)&self->m_data->m_textKeys, index);
}

XString* XImage_textKey(const XImage* self, int index)
{
    const XString* value = XImage_textKey_const(self, index);
    return value ? XString_create_copy(value) : XString_create();
}

const char* XImage_text_2(const XImage* self, const char* key)
{
    XString* keyString;
    const XString* value;
    XString* aggregated;
    static const char emptyUtf8[] = "";
    /* QImage::text() 返回值类型是 QString：空图像、空键和缺失键均为
       空字符串，而不是可区分的空指针。兼容重载中的 NULL 键等价于
       默认构造的空 QString，因此也必须走全部文本聚合分支。 */
    if (!self || !self->m_data) return emptyUtf8;
    keyString = XString_create_utf8(key ? key : "");
    if (!keyString) return NULL;
    if (XString_isEmpty_base((const XContainer*)keyString))
    {
        if (XString_isEmpty_base((const XContainer*)&self->m_data->m_textAll))
        {
            aggregated = XImageData_buildAllText(self->m_data);
            if (!aggregated)
            {
                XString_delete_base((XClass*)keyString);
                return NULL;
            }
            XString_assign(&self->m_data->m_textAll, aggregated);
            XString_delete_base((XClass*)aggregated);
        }
        XString_delete_base((XClass*)keyString);
        if (XString_isEmpty_base((const XContainer*)&self->m_data->m_textAll))
            return emptyUtf8;
        return XString_toUtf8(&self->m_data->m_textAll);
    }
    value = XImage_text_const(self, keyString);
    if (keyString) XString_delete_base((XClass*)keyString);
    if (!value) return emptyUtf8;
    return XString_isEmpty_base((const XContainer*)value) ? emptyUtf8
                                                          : XString_toUtf8(value);
}

void XImage_setText_2(XImage* self, const char* key, const char* value)
{
    XString* keyString;
    XString* valueString;
    if (!key || !value) return;
    keyString = XString_create_utf8(key);
    valueString = XString_create_utf8(value);
    if (keyString && valueString) XImage_setText(self, keyString, valueString);
    if (keyString) XString_delete_base((XClass*)keyString);
    if (valueString) XString_delete_base((XClass*)valueString);
}

XString* XImage_text(const XImage* self, const XString* key)
{
    if (!self || !self->m_data) return XString_create_utf8("");
    if (!key || XString_isEmpty_base((const XContainer*)key))
    {
        XString* aggregated = XImageData_buildAllText(self->m_data);
        return aggregated ? aggregated : XString_create_utf8("");
    }
    {
        const XString* value = XImage_text_const(self, key);
        return value ? XString_create_copy(value) : XString_create_utf8("");
    }
}

const XString* XImage_text_const(const XImage* self, const XString* key)
{
    int count;
    if (!self || !self->m_data || !key || XString_isEmpty_base((const XContainer*)key)) return NULL;
    count = XImage_textCount(self);
    for (int i = 0; i < count; ++i)
    {
        const XString* item = (const XString*)XStringList_at_base((const XVector*)&self->m_data->m_textKeys, i);
        if (item && XString_equals(item, key, XChar_CaseSensitive))
            return (const XString*)XStringList_at_base((const XVector*)&self->m_data->m_textValues, i);
    }
    return NULL;
}

void XImage_setText(XImage* self, const XString* key, const XString* value)
{
    int count;
    int insertIndex = 0;
    if (!self || !self->m_data || !key || !value) return;
    /* 文本属于 QImage 的元数据，不应使唯一图像的 cacheKey 递增。 */
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
    count = XImage_textCount(self);
    for (int i = 0; i < count; ++i)
    {
        XString* item = (XString*)XStringList_at_base((XVector*)&self->m_data->m_textKeys, i);
        if (!item) continue;
        if (XString_equals(item, key, XChar_CaseSensitive))
        {
            XString* target = (XString*)XStringList_at_base((XVector*)&self->m_data->m_textValues, i);
            if (!target) return;
            XCopy((XClass*)target, (const XClass*)value);
            XImageData_clearTextAll(self->m_data);
            return;
        }
        if (XString_compare(item, key) > 0)
        {
            insertIndex = i;
            break;
        }
        insertIndex = i + 1;
    }
    if (!XStringList_insert_base((XVector*)&self->m_data->m_textKeys, insertIndex, (void*)key) ||
        !XStringList_insert_base((XVector*)&self->m_data->m_textValues, insertIndex, (void*)value))
    {
        if (XImage_textCount(self) > count)
            XStringList_remove_base((XVector*)&self->m_data->m_textKeys, insertIndex, 1);
        return;
    }
    XImageData_clearTextAll(self->m_data);
}

bool XImage_applyTextDescription(XImage* self, const XString* description)
{
    XStringList* pairs;
    int64_t count;
    int64_t i;
    if (!self || !self->m_data || !description)
        return false;
    if (XString_isEmpty_base((const XContainer*)description))
        return true;
    /* QImage's helper tokenizes by two newlines and ignores empty records
       after trimming.  The portable split helper has the same effective
       result for this separator while preserving UTF-16 code-unit indexes. */
    pairs = XString_split_utf8(description, "\n\n", XChar_CaseSensitive);
    if (!pairs)
        return false;
    count = (int64_t)XStringList_size_base((const XContainer*)pairs);
    for (i = 0; i < count; ++i)
    {
        XString* pair = (XString*)XStringList_at_base((XVector*)pairs, i);
        XString* simplified;
        XString* key = NULL;
        XString* value = NULL;
        int64_t colonIndex;
        int64_t spaceIndex;
        if (!pair)
            continue;
        simplified = XString_simplified(pair);
        if (!simplified || XString_isEmpty_base((const XContainer*)simplified))
        {
            if (simplified) XString_delete_base((XClass*)simplified);
            continue;
        }
        colonIndex = XString_indexOf_char(pair, (XChar)':', 0,
                                          XChar_CaseSensitive);
        spaceIndex = XString_indexOf_char(pair, (XChar)' ', 0,
                                          XChar_CaseSensitive);
        if (colonIndex >= 0 && spaceIndex < colonIndex)
        {
            key = XString_create_utf8("Description");
            value = simplified;
            simplified = NULL;
        }
        else
        {
            size_t valuePos;
            size_t pairLength;
            key = XString_left(pair, colonIndex >= 0
                                      ? (size_t)colonIndex : SIZE_MAX);
            if (key)
            {
                XString* keyCheck = XString_simplified(key);
                if (!keyCheck || XString_isEmpty_base((const XContainer*)keyCheck))
                {
                    if (keyCheck) XString_delete_base((XClass*)keyCheck);
                    XString_delete_base((XClass*)key);
                    key = NULL;
                }
                else
                    XString_delete_base((XClass*)keyCheck);
            }
            valuePos = colonIndex >= 0 ? (size_t)colonIndex + 2u : 1u;
            pairLength = XString_length_base((const XContainer*)pair);
            value = XString_mid(pair, valuePos,
                                valuePos < pairLength ? pairLength - valuePos : 0u);
            if (value)
            {
                XString* normalized = XString_simplified(value);
                XString_delete_base((XClass*)value);
                value = normalized;
            }
        }
        if (key && value)
            XImage_setText(self, key, value);
        if (key) XString_delete_base((XClass*)key);
        if (value) XString_delete_base((XClass*)value);
        if (simplified) XString_delete_base((XClass*)simplified);
    }
    XStringList_delete_base((XClass*)pairs);
    return true;
}

/* ========== 辅助数据 ========== */

int XImage_dotsPerMeterX(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_dpmX : 0;
}

void XImage_setDotsPerMeterX(XImage* self, int val)
{
    /* Qt treats zero as the default/unset value and does not detach or
     * mutate metadata for it; repeated values are also strict no-ops. */
    if (!self || !self->m_data || val == 0 || self->m_data->m_dpmX == val) return;
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
    self->m_data->m_dpmX = val;
}

int XImage_dotsPerMeterY(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_dpmY : 0;
}

void XImage_setDotsPerMeterY(XImage* self, int val)
{
    /* Qt treats zero as the default/unset value and does not detach or
     * mutate metadata for it; repeated values are also strict no-ops. */
    if (!self || !self->m_data || val == 0 || self->m_data->m_dpmY == val) return;
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
    self->m_data->m_dpmY = val;
}

float XImage_devicePixelRatio(const XImage* self)
{
    return (self && self->m_data) ? self->m_data->m_devicePixelRatio : 1.0f;
}

void XImage_deviceIndependentSize(const XImage* self, XSizeF* out)
{
    float ratio;
    if (!out) return;
    if (!self || !self->m_data) {
        out->width = 0.0f;
        out->height = 0.0f;
        return;
    }
    ratio = self->m_data->m_devicePixelRatio;
    out->width = (float)self->m_data->m_width / ratio;
    out->height = (float)self->m_data->m_height / ratio;
}

void XImage_setDevicePixelRatio(XImage* self, float scaleFactor)
{
    if (!self || !self->m_data || scaleFactor == XImage_devicePixelRatio(self))
        return;
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
    self->m_data->m_devicePixelRatio = scaleFactor;
}

void XImage_offset(const XImage* self, XPoint* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_offsetX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_offsetY : 0;
    }
}

XVariant* XImage_toVariant(const XImage* self)
{
    return XVariant_create_ptr((void*)self);
}

XImage* XImage_fromVariant(const XVariant* variant)
{
    if (!variant || XVariant_type((XVariant*)variant) != XVariantType_Ptr)
        return NULL;
    return (XImage*)XVariant_toPtr(variant);
}

void XImage_setOffset(XImage* self, const XPoint* pos)
{
    if (!self || !self->m_data || !pos) return;
    if (self->m_data->m_offsetX == pos->x && self->m_data->m_offsetY == pos->y)
        return;
    XImage_detachMetadata(self);
    if (!XImage_isDetached(self)) return;
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

/*
 * 对齐 QImage::detachMetadata(false)：只有共享数据需要复制，唯一数据
 * 直接复用；无论哪条路径都不递增 cacheKey 的 detach 序号。这样色彩空间、
 * 文本、DPI、DPR 和 offset 等元数据修改不会伪装成像素数据修改。
 */
static void XImage_detachMetadata(XImage* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount,
                           XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XImageData* newData = XImageData_clone(self->m_data);
        if (newData)
        {
            XImageData_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

bool XImage_isDetached(const XImage* self)
{
    return self && self->m_data && XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}

/* ========== 静态工具方法 ========== */

void XImage_fromData_2(const uint8_t* data, int size, const char* format, XImage* out)
{
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XImage_fromData(data, size, formatString, out);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XImage_fromData(const uint8_t* data, int size, const XString* format, XImage* out)
{
    XImage_loadFromData(out, data, size, format);
}

const char* XImage_formatToStr_2(XImageFormat format)
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

XString* XImage_formatToStr(XImageFormat format)
{
    return XString_create_utf8(XImage_formatToStr_2(format));
}




/* ========== 像素查询方法 ========== */

bool XImage_allGray(const XImage* self)
{
    int i;
    if (!self || !self->m_data) return true;
    switch (self->m_data->m_format)
    {
        case XImageFormat_Mono:
        case XImageFormat_MonoLSB:
        case XImageFormat_Indexed8:
            /* QImage checks the whole color table, including entries not
             * referenced by a pixel. An empty table is vacuously gray. */
            for (i = 0; i < self->m_data->m_colorCount; ++i)
            {
                uint32_t color;
                if (!self->m_data->m_colorTable) return false;
                color = self->m_data->m_colorTable[i];
                if (((color >> 16) & 255u) != ((color >> 8) & 255u) ||
                    ((color >> 8) & 255u) != (color & 255u))
                    return false;
            }
            return true;
        case XImageFormat_Alpha8:
            return false;
        case XImageFormat_Grayscale8:
        case XImageFormat_Grayscale16:
            return true;
        default:
            break;
    }
    if (!self->m_data->m_data) return false;
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

    /* The raster backend uses premultiplied ARGB32 for its framebuffers.
     * Keep the general format conversion below for uncommon image formats,
     * but write the common 32-bit packed formats in one tight row loop.  The
     * old path dispatched through XImage_writePixelValue for every pixel,
     * which made clearing a full window needlessly expensive. */
    if (self->m_data->m_format == XImageFormat_ARGB32_Premultiplied ||
        self->m_data->m_format == XImageFormat_ARGB32 ||
        self->m_data->m_format == XImageFormat_RGB32)
    {
        uint8_t a = (uint8_t)(color >> 24);
        uint8_t r = (uint8_t)(color >> 16);
        uint8_t g = (uint8_t)(color >> 8);
        uint8_t b = (uint8_t)color;
        uint32_t value = color;
        int y;
        if (self->m_data->m_format == XImageFormat_ARGB32_Premultiplied)
            value = ((uint32_t)a << 24) |
                    ((((uint32_t)r * a + 127u) / 255u) << 16) |
                    ((((uint32_t)g * a + 127u) / 255u) << 8) |
                    (((uint32_t)b * a + 127u) / 255u);
        else if (self->m_data->m_format == XImageFormat_RGB32)
            value |= 0xff000000u;
        for (y = ry; y < ry + rh; ++y)
        {
            uint8_t* line = self->m_data->m_data +
                            (size_t)y * (size_t)self->m_data->m_bytesPerLine +
                            (size_t)rx * 4u;
            int x;
            if ((((uintptr_t)line) & (sizeof(uint32_t) - 1u)) == 0u)
            {
                uint32_t* pixels = (uint32_t*)line;
                for (x = 0; x < rw; ++x)
                    pixels[x] = value;
            }
            else
            {
                for (x = 0; x < rw; ++x)
                    XImage_store32(line + (size_t)x * 4u, value);
            }
        }
        return;
    }
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
    if (XImageFormat_isPremultiplied(self->m_data->m_format) &&
        self->m_data->m_format != XImageFormat_RGBA64_Premultiplied &&
        self->m_data->m_format != XImageFormat_RGBA16FPx4_Premultiplied &&
        self->m_data->m_format != XImageFormat_RGBA32FPx4_Premultiplied)
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
    else if (self->m_data->m_format >= XImageFormat_RGBX16FPx4 &&
             self->m_data->m_format <= XImageFormat_RGBA16FPx4_Premultiplied)
    {
        const bool premultiplied = self->m_data->m_format == XImageFormat_RGBA16FPx4_Premultiplied;
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            size_t count = (size_t)self->m_data->m_bytesPerLine / 8u;
            for (size_t x = 0; x < count; ++x)
            {
                uint8_t* p = line + x * 8u;
                float r = XImage_halfToFloat(XImage_load16(p));
                float g = XImage_halfToFloat(XImage_load16(p + 2));
                float b = XImage_halfToFloat(XImage_load16(p + 4));
                float a = XImage_halfToFloat(XImage_load16(p + 6));
                if (premultiplied)
                {
                    if (a <= 0.0f) r = g = b = a = 0.0f;
                    else if (a < 1.0f) { r /= a; g /= a; b /= a; }
                }
                r = 1.0f - r; g = 1.0f - g; b = 1.0f - b;
                if (mode == XImageInvertMode_InvertRgba) a = 1.0f - a;
                if (premultiplied) { r *= a; g *= a; b *= a; }
                XImage_store16(p, XImage_floatToHalf(r));
                XImage_store16(p + 2, XImage_floatToHalf(g));
                XImage_store16(p + 4, XImage_floatToHalf(b));
                if (mode == XImageInvertMode_InvertRgba || premultiplied)
                    XImage_store16(p + 6, XImage_floatToHalf(a));
            }
        }
    }
    else if (self->m_data->m_format == XImageFormat_RGBA64_Premultiplied)
    {
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            size_t count = (size_t)self->m_data->m_bytesPerLine / 8u;
            for (size_t x = 0; x < count; ++x)
            {
                uint8_t* p = line + x * 8u;
                uint16_t a = XImage_load16(p + 6);
                uint16_t r = XImage_unpremultiply16(XImage_load16(p), a);
                uint16_t g = XImage_unpremultiply16(XImage_load16(p + 2), a);
                uint16_t b = XImage_unpremultiply16(XImage_load16(p + 4), a);
                r = (uint16_t)~r; g = (uint16_t)~g; b = (uint16_t)~b;
                if (mode == XImageInvertMode_InvertRgba) a = (uint16_t)~a;
                XImage_store16(p, XImage_premultiply16(r, a));
                XImage_store16(p + 2, XImage_premultiply16(g, a));
                XImage_store16(p + 4, XImage_premultiply16(b, a));
                if (mode == XImageInvertMode_InvertRgba)
                    XImage_store16(p + 6, a);
            }
        }
    }
    else if (self->m_data->m_format >= XImageFormat_RGBX32FPx4 &&
             self->m_data->m_format <= XImageFormat_RGBA32FPx4_Premultiplied)
    {
        const bool premultiplied = self->m_data->m_format == XImageFormat_RGBA32FPx4_Premultiplied;
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            for (int x = 0; x < self->m_data->m_width; ++x)
            {
                uint8_t* p = line + (size_t)x * 16u;
                float r, g, b, a;
                memcpy(&r, p, sizeof(float)); memcpy(&g, p + 4, sizeof(float));
                memcpy(&b, p + 8, sizeof(float)); memcpy(&a, p + 12, sizeof(float));
                if (premultiplied)
                {
                    if (a <= 0.0f) r = g = b = a = 0.0f;
                    else if (a < 1.0f) { r /= a; g /= a; b /= a; }
                }
                r = 1.0f - r; g = 1.0f - g; b = 1.0f - b;
                if (mode == XImageInvertMode_InvertRgba) a = 1.0f - a;
                if (premultiplied) { r *= a; g *= a; b *= a; }
                memcpy(p, &r, sizeof(float)); memcpy(p + 4, &g, sizeof(float));
                memcpy(p + 8, &b, sizeof(float));
                if (mode == XImageInvertMode_InvertRgba || premultiplied)
                    memcpy(p + 12, &a, sizeof(float));
            }
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
    else
    {
        uint32_t xorbits;
        switch (self->m_data->m_format)
        {
            case XImageFormat_RGB32:
                /* RGB32 has an opaque storage byte which Qt never inverts. */
                xorbits = 0x00ffffffu;
                break;
            case XImageFormat_ARGB32:
                xorbits = mode == XImageInvertMode_InvertRgba ? 0xffffffffu : 0x00ffffffu;
                break;
            case XImageFormat_RGBX8888:
#if IS_BIG_ENDIAN
                xorbits = 0xffffff00u;
#else
                xorbits = 0x00ffffffu;
#endif
                break;
            case XImageFormat_RGBA8888:
                if (mode == XImageInvertMode_InvertRgba)
                    xorbits = 0xffffffffu;
                else
                {
#if IS_BIG_ENDIAN
                    xorbits = 0xffffff00u;
#else
                    xorbits = 0x00ffffffu;
#endif
                }
                break;
            case XImageFormat_BGR30:
            case XImageFormat_RGB30:
                xorbits = 0x3fffffffu;
                break;
            case XImageFormat_CMYK8888:
                /* Qt marks invertPixels(CMYK8888) unreachable; leave storage unchanged. */
                return;
            default:
                return;
        }
        for (int y = 0; y < self->m_data->m_height; ++y)
        {
            uint8_t* line = self->m_data->m_data + (size_t)y * (size_t)self->m_data->m_bytesPerLine;
            size_t count = (size_t)self->m_data->m_bytesPerLine / 4u;
            for (size_t x = 0; x < count; ++x)
            {
                uint32_t value = XImage_load32(line + x * 4u);
                XImage_store32(line + x * 4u, value ^ xorbits);
            }
        }
    }
    XImageData_markDirty(self->m_data);
}
