/******************************************************************************
 * @file       XPicture.c
 * @brief      XPicture 绘图指令记录与回放类实现（对标 Qt 6.8 QPicture）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPicture.h"
#include "XPainter.h"
#include "XImage.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XFile.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>

static const uint8_t g_xpictureMagic[XPICTURE_MAGIC_SIZE] =
    { 'X', 'P', 'I', 'C', 'T', 'U', 'R', 'E' };

enum { XPICTURE_IMAGE_FIXED_SIZE = 52 };
enum
{
    XPICTURE_SHAPE_PAYLOAD_SIZE = 40,
    XPICTURE_TRANSFORM_PAYLOAD_SIZE = 40,
    XPICTURE_POINT_HEADER_SIZE = 4,
    XPICTURE_POLYGON_HEADER_SIZE = 12,
    XPICTURE_MAX_POINTS = 65535,
    XPICTURE_PATH_HEADER_SIZE = 8,
    XPICTURE_PATH_ELEMENT_SIZE = 12,
    XPICTURE_MAX_PATH_ELEMENTS = 65535
};

static uint16_t XPicture_getU16(const uint8_t* p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t XPicture_getU32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t XPicture_getI32(const uint8_t* p)
{
    return (int32_t)XPicture_getU32(p);
}

static float XPicture_getF32(const uint8_t* p)
{
    uint32_t bits = XPicture_getU32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void XPicture_putU16(uint8_t* p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void XPicture_putU32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void XPicture_putI32(uint8_t* p, int32_t value)
{
    XPicture_putU32(p, (uint32_t)value);
}

static void XPicture_putF32(uint8_t* p, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    XPicture_putU32(p, bits);
}

static bool XPicture_transformIsFinite(const XImageTransform* matrix)
{
    return matrix && isfinite(matrix->m11) && isfinite(matrix->m12) &&
           isfinite(matrix->m21) && isfinite(matrix->m22) &&
           isfinite(matrix->dx) && isfinite(matrix->dy) &&
           isfinite(matrix->m13) && isfinite(matrix->m23) &&
           isfinite(matrix->m33);
}

static bool XPicture_payloadFloatsAreFinite(const uint8_t* payload,
                                            uint32_t count)
{
    uint32_t i;
    if (!payload) return false;
    for (i = 0; i < count; ++i)
    {
        if (!isfinite(XPicture_getF32(payload + i * 4u)))
            return false;
    }
    return true;
}

static uint32_t XPicture_checksum(const uint8_t* data, uint32_t size)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    if (!data) return 0;
    for (i = 0; i < size; ++i)
    {
        uint8_t value = (i >= 36u && i < 40u) ? 0u : data[i];
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

static bool XPicture_magicMatches(const uint8_t* data, uint32_t size)
{
    return data && size >= XPICTURE_MAGIC_SIZE &&
           memcmp(data, g_xpictureMagic, XPICTURE_MAGIC_SIZE) == 0;
}

static bool XPicture_validateImagePayload(const uint8_t* payload, uint32_t length)
{
    uint32_t width, height, bytesPerLine, imageSize, colorCount;
    uint64_t required;
    float dpr;
    uint32_t dprBits;
    if (!payload || length < XPICTURE_IMAGE_FIXED_SIZE) return false;
    width = XPicture_getU32(payload + 8);
    height = XPicture_getU32(payload + 12);
    bytesPerLine = XPicture_getU32(payload + 20);
    imageSize = XPicture_getU32(payload + 24);
    dprBits = XPicture_getU32(payload + 28);
    colorCount = XPicture_getU32(payload + 48);
    memcpy(&dpr, &dprBits, sizeof(dpr));
    if (width == 0 || height == 0 || width > (uint32_t)INT_MAX ||
        height > (uint32_t)INT_MAX || bytesPerLine == 0 ||
        imageSize == 0 || colorCount > 65536u ||
        XPicture_getU32(payload + 16) >= (uint32_t)XImageFormat_NImageFormats ||
        !(dpr > 0.0f) || !isfinite(dpr))
        return false;
    required = (uint64_t)XPICTURE_IMAGE_FIXED_SIZE +
               (uint64_t)colorCount * 4u + imageSize;
    if (required != length) return false;
    if ((uint64_t)bytesPerLine * height != imageSize || imageSize > (uint32_t)INT_MAX)
        return false;
    return true;
}

static bool XPicture_validatePointsPayload(const uint8_t* payload,
                                              uint32_t length,
                                              uint32_t minCount,
                                              uint32_t headerSize)
{
    uint32_t count;
    uint64_t required;
    if (!payload || length < headerSize) return false;
    count = XPicture_getU32(payload);
    if (count < minCount || count > XPICTURE_MAX_POINTS) return false;
    required = (uint64_t)headerSize + (uint64_t)count * 8u;
    return required == length;
}

static bool XPicture_validateShapePayload(const uint8_t* payload,
                                          uint32_t length)
{
    if (!payload || length != XPICTURE_SHAPE_PAYLOAD_SIZE) return false;
    if (XPicture_getI32(payload) < 1 || XPicture_getI32(payload) > 5)
        return false;
    return XPicture_getU32(payload + 28u) <= 1u;
}

static bool XPicture_validatePolygonPayload(const uint8_t* payload,
                                            uint32_t length)
{
    if (!XPicture_validatePointsPayload(payload, length, 2u,
                                        XPICTURE_POLYGON_HEADER_SIZE))
        return false;
    return XPicture_getU32(payload + 4u) <= 1u &&
           XPicture_getU32(payload + 8u) <= 1u;
}

#if XPAINTER_PATH_ON
static bool XPicture_validatePathPayload(const uint8_t* payload,
                                         uint32_t length)
{
    uint32_t op, count, i, pendingCurveData = 0u;
    uint64_t required;
    if (!payload || length < XPICTURE_PATH_HEADER_SIZE) return false;
    op = XPicture_getU32(payload);
    count = XPicture_getU32(payload + 4);
    if (op < (uint32_t)XPainterPathOp_Draw ||
        op > (uint32_t)XPainterPathOp_Stroke ||
        count == 0u || count > XPICTURE_MAX_PATH_ELEMENTS)
        return false;
    required = (uint64_t)XPICTURE_PATH_HEADER_SIZE +
               (uint64_t)count * XPICTURE_PATH_ELEMENT_SIZE;
    if (required != length) return false;
    for (i = 0; i < count; ++i)
    {
        const uint8_t* element = payload + XPICTURE_PATH_HEADER_SIZE +
                                 i * XPICTURE_PATH_ELEMENT_SIZE;
        uint32_t type = XPicture_getU32(element);
        if (type > (uint32_t)XPainterPathElement_CurveToData)
            return false;
        if (!isfinite(XPicture_getF32(element + 4)) ||
            !isfinite(XPicture_getF32(element + 8)))
            return false;
        if (type == (uint32_t)XPainterPathElement_CurveTo)
            pendingCurveData = 2u;
        else if (type == (uint32_t)XPainterPathElement_CurveToData)
        {
            if (pendingCurveData == 0u)
                return false;
            --pendingCurveData;
        }
        else
            pendingCurveData = 0u;
    }
    return pendingCurveData == 0u;
}
#endif /* XPAINTER_PATH_ON */

static bool XPicture_validateStreamData(const char* data, uint32_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t commandCount, commandBytes, offset, index;
    if (!XPicture_magicMatches(bytes, size) || size < XPICTURE_HEADER_SIZE)
        return false;
    if (XPicture_getU16(bytes + 8) != XPICTURE_STREAM_VERSION ||
        XPicture_getU16(bytes + 10) != XPICTURE_HEADER_SIZE ||
        XPicture_getU32(bytes + 16) != size - XPICTURE_HEADER_SIZE ||
        XPicture_getU32(bytes + 36) != XPicture_checksum(bytes, size))
        return false;
    commandCount = XPicture_getU32(bytes + 12);
    commandBytes = XPicture_getU32(bytes + 16);
    offset = XPICTURE_HEADER_SIZE;
    for (index = 0; index < commandCount; ++index)
    {
        uint8_t opcode;
        uint32_t length;
        const uint8_t* payload;
        if (offset > size || size - offset < XPICTURE_RECORD_HEADER_SIZE)
            return false;
        opcode = bytes[offset];
        length = XPicture_getU32(bytes + offset + 4);
        if (bytes[offset + 1u] != 0u || XPicture_getU16(bytes + offset + 2u) != 0u)
            return false;
        offset += XPICTURE_RECORD_HEADER_SIZE;
        if (length > size - offset) return false;
        payload = bytes + offset;
        if (opcode < XPictureOpcode_DrawLine ||
            opcode > XPictureOpcode_SetViewTransformEnabled)
            return false;
        if ((opcode == XPictureOpcode_DrawLine && length != 16u) ||
            (opcode == XPictureOpcode_SetPen && length != 20u) ||
            (opcode == XPictureOpcode_SetOpacity && length != 4u) ||
            (opcode == XPictureOpcode_SetCompositionMode && length != 4u) ||
            (opcode == XPictureOpcode_SetRenderHints && length != 4u) ||
            (opcode == XPictureOpcode_SetBrushOrigin && length != 8u) ||
            (opcode == XPictureOpcode_SetTransform &&
             length != XPICTURE_TRANSFORM_PAYLOAD_SIZE) ||
            (opcode == XPictureOpcode_SetWindow && length != 16u) ||
            (opcode == XPictureOpcode_SetViewport && length != 16u) ||
            (opcode == XPictureOpcode_SetViewTransformEnabled &&
             length != 4u) ||
            (opcode == XPictureOpcode_SetBackgroundColor && length != 4u) ||
            (opcode == XPictureOpcode_SetBackgroundMode && length != 4u) ||
            (opcode == XPictureOpcode_SetBrush && length != 8u) ||
            (opcode == XPictureOpcode_FillRect && length != 20u) ||
            (opcode == XPictureOpcode_DrawImage &&
             !XPicture_validateImagePayload(payload, length)) ||
            ((opcode == XPictureOpcode_Save || opcode == XPictureOpcode_Restore) && length != 0u))
            return false;
        if (opcode == XPictureOpcode_SetOpacity)
        {
            float opacity = XPicture_getF32(payload);
            if (!isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
                return false;
        }
        if (opcode == XPictureOpcode_SetCompositionMode &&
            (XPicture_getI32(payload) <
                 (int32_t)XPainterCompositionMode_SourceOver ||
             XPicture_getI32(payload) >
                 (int32_t)XPainterCompositionMode_RasterOp_NotDestination))
            return false;
        if (opcode == XPictureOpcode_SetBrushOrigin &&
            !XPicture_payloadFloatsAreFinite(payload, 2u))
            return false;
        if (opcode == XPictureOpcode_SetTransform &&
            (!XPicture_payloadFloatsAreFinite(payload, 9u) ||
             XPicture_getU32(payload + 36u) > 1u))
            return false;
        if (opcode == XPictureOpcode_SetBackgroundMode &&
            XPicture_getU32(payload) > 1u)
            return false;
#if XPAINTER_SHAPE_ON
        if (opcode == XPictureOpcode_DrawShape &&
            !XPicture_validateShapePayload(payload, length))
            return false;
#else
        if (opcode == XPictureOpcode_DrawShape)
            return false;
#endif
#if XPAINTER_POLYGON_ON
        if ((opcode == XPictureOpcode_DrawPolyline &&
             !XPicture_validatePointsPayload(payload, length, 2u,
                                             XPICTURE_POINT_HEADER_SIZE)) ||
            (opcode == XPictureOpcode_DrawPolygon &&
             !XPicture_validatePolygonPayload(payload, length)) ||
            (opcode == XPictureOpcode_DrawPoints &&
             !XPicture_validatePointsPayload(payload, length, 1u,
                                             XPICTURE_POINT_HEADER_SIZE)))
            return false;
#else
        if (opcode == XPictureOpcode_DrawPolyline ||
            opcode == XPictureOpcode_DrawPolygon ||
            opcode == XPictureOpcode_DrawPoints)
            return false;
#endif
#if XPAINTER_PATH_ON
        if (opcode == XPictureOpcode_DrawPath &&
            !XPicture_validatePathPayload(payload, length))
            return false;
#else
        if (opcode == XPictureOpcode_DrawPath)
            return false;
#endif
        if (opcode == XPictureOpcode_SetViewTransformEnabled &&
            XPicture_getU32(payload) > 1u)
            return false;
        offset += length;
    }
    return offset == size && commandBytes == offset - XPICTURE_HEADER_SIZE;
}

/**
 * @brief      XPicture 私有数据结构
 */
typedef struct XPicturePrivate
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数 */
    char*            m_data;        /**< 绘图指令数据 */
    uint32_t         m_dataSize;    /**< 数据大小 */
    uint32_t         m_dataCapacity;/**< 数据容量 */
    bool             m_isNull;      /**< 是否为 null */
    int              m_formatVersion; /**< 格式版本号 */
    int              m_boundingX;   /**< 边界矩形 X */
    int              m_boundingY;   /**< 边界矩形 Y */
    int              m_boundingW;   /**< 边界矩形宽度 */
    int              m_boundingH;   /**< 边界矩形高度 */
}XPicturePrivate;

static XPicturePrivate* XPicturePrivate_create(int formatVersion)
{
    XPicturePrivate* d = (XPicturePrivate*)XMalloc_System(sizeof(XPicturePrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPicturePrivate));
    XAtomic_init(d->m_refCount, 1);
    d->m_isNull = true;
    d->m_formatVersion = formatVersion;
    return d;
}

static void XPicturePrivate_ref(XPicturePrivate* d) { if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst); }

static void XPicturePrivate_unref(XPicturePrivate* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        if (d->m_data) XFree_System(d->m_data);
        XFree_System(d);
    }
}

static XPicturePrivate* XPicturePrivate_clone(const XPicturePrivate* source)
{
    XPicturePrivate* copy;
    if (!source) return NULL;
    copy = XPicturePrivate_create(source->m_formatVersion);
    if (!copy) return NULL;
    copy->m_isNull = source->m_isNull;
    copy->m_boundingX = source->m_boundingX;
    copy->m_boundingY = source->m_boundingY;
    copy->m_boundingW = source->m_boundingW;
    copy->m_boundingH = source->m_boundingH;
    if (source->m_dataSize != 0)
    {
        copy->m_data = (char*)XMalloc_System(source->m_dataSize);
        if (!copy->m_data)
        {
            XPicturePrivate_unref(copy);
            return NULL;
        }
        memcpy(copy->m_data, source->m_data, source->m_dataSize);
        copy->m_dataSize = source->m_dataSize;
        copy->m_dataCapacity = source->m_dataSize;
    }
    return copy;
}

static void XPicture_reset(XPicture* self)
{
    XPicturePrivate* empty;
    int version;
    if (!self) return;
    version = self->m_data ? self->m_data->m_formatVersion : -1;
    empty = XPicturePrivate_create(version);
    if (!empty) return;
    XPicturePrivate_unref(self->m_data);
    self->m_data = empty;
}

static void VXPicture_copy(XPicture* dest, const XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest)) XPicture_init(dest, -1);
    if (dest->m_data)
        XPicturePrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    XPicturePrivate_ref(dest->m_data);
}

static void VXPicture_move(XPicture* dest, XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest)) XPicture_init(dest, -1);
    if (dest->m_data)
        XPicturePrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXPicture_deinit(XPicture* self)
{
    if (ISNULL(self, "XPicture")) return;
    if (self->m_data)
    {
        XPicturePrivate_unref(self->m_data);
        self->m_data = NULL;
    }
}

XVtable* XPicture_class_init()
{
    XVTABLE_INIT_DEFAULT(XPicture)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPicture_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPicture_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPicture_deinit);
    return XVTABLE_DEFAULT;
}

XPicture* XPicture_create_ex(XMemoryType memory)
{
    XPicture* self = (XPicture*)XMemory_malloc(sizeof(XPicture), memory);
    if (!self) return NULL;
    XPicture_init(self, -1);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XPicture* XPicture_create_copy(const XPicture* other, XMemoryType memory)
{
    XPicture* self;
    if (!other) return NULL;
    self = XPicture_create_ex(memory);
    if (!self) return NULL;
    XPicture_copy_base(self, other);
    return self;
}

XPicture* XPicture_create_move(XPicture* other, XMemoryType memory)
{
    XPicture* self;
    if (!other) return NULL;
    self = XPicture_create_ex(memory);
    if (!self) return NULL;
    XPicture_move_base(self, other);
    return self;
}

void XPicture_init(XPicture* self, int formatVersion)
{
    if (ISNULL(self, "XPicture")) return;
    memset(self, 0, sizeof(XPicture));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPicture);
    self->m_data = XPicturePrivate_create(formatVersion);
}

void XPicture_swap(XPicture* self, XPicture* other)
{
    XPicturePrivate* data;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPicture_init(self, -1);
    if (XClassIsVtableNull(other)) XPicture_init(other, -1);
    data = self->m_data;
    self->m_data = other->m_data;
    other->m_data = data;
}
bool XPicture_isNull(const XPicture* self) { return !self || !self->m_data || self->m_data->m_isNull; }
int XPicture_devType(const XPicture* self) { return XPicture_isNull(self) ? 0 : 1; }
void* XPicture_paintEngine(const XPicture* self) { (void)self; return NULL; }
uint32_t XPicture_size(const XPicture* self) { return (self && self->m_data) ? self->m_data->m_dataSize : 0; }
const char* XPicture_data(const XPicture* self) { return (self && self->m_data) ? self->m_data->m_data : NULL; }

void XPicture_setData(XPicture* self, const char* data, uint32_t size)
{
    char* newData = NULL;
    if (!self || !self->m_data || (size != 0 && !data)) return;
    if (size != 0)
    {
        newData = (char*)XMalloc_System(size);
        if (!newData) return;
        memcpy(newData, data, size);
    }
    XPicture_detach(self);
    if (!XPicture_isDetached(self))
    {
        XFree_System(newData);
        return;
    }
    XFree_System(self->m_data->m_data);
    self->m_data->m_data = newData;
    self->m_data->m_dataSize = size;
    self->m_data->m_dataCapacity = size;
    self->m_data->m_isNull = (size == 0);
    if (size >= XPICTURE_HEADER_SIZE &&
        XPicture_validateStreamData(self->m_data->m_data, size))
    {
        const uint8_t* bytes = (const uint8_t*)self->m_data->m_data;
        self->m_data->m_boundingX = (int)XPicture_getI32(bytes + 20);
        self->m_data->m_boundingY = (int)XPicture_getI32(bytes + 24);
        self->m_data->m_boundingW = (int)XPicture_getI32(bytes + 28);
        self->m_data->m_boundingH = (int)XPicture_getI32(bytes + 32);
    }
}

void XPicture_boundingRect(const XPicture* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_boundingX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_boundingY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_boundingW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_boundingH : 0;
    }
}

void XPicture_setBoundingRect(XPicture* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    XPicture_detach(self);
    if (!XPicture_isDetached(self)) return;
    self->m_data->m_boundingX = rect->x;
    self->m_data->m_boundingY = rect->y;
    self->m_data->m_boundingW = rect->width;
    self->m_data->m_boundingH = rect->height;
    if (XPicture_validateStreamData(self->m_data->m_data, self->m_data->m_dataSize))
    {
        uint8_t* bytes = (uint8_t*)self->m_data->m_data;
        XPicture_putI32(bytes + 20, rect->x);
        XPicture_putI32(bytes + 24, rect->y);
        XPicture_putI32(bytes + 28, rect->width);
        XPicture_putI32(bytes + 32, rect->height);
        XPicture_putU32(bytes + 36,
                        XPicture_checksum(bytes, self->m_data->m_dataSize));
    }
}

static bool XPicture_prepareStream(XPicture* self)
{
    uint8_t* bytes;
    if (!self || !self->m_data) return false;
    XPicture_detach(self);
    if (!XPicture_isDetached(self)) return false;
    if (self->m_data->m_dataSize == 0)
    {
        bytes = (uint8_t*)XMalloc_System(XPICTURE_HEADER_SIZE);
        if (!bytes) return false;
        memset(bytes, 0, XPICTURE_HEADER_SIZE);
        memcpy(bytes, g_xpictureMagic, XPICTURE_MAGIC_SIZE);
        XPicture_putU16(bytes + 8, XPICTURE_STREAM_VERSION);
        XPicture_putU16(bytes + 10, XPICTURE_HEADER_SIZE);
        XPicture_putU32(bytes + 12, 0);
        XPicture_putU32(bytes + 16, 0);
        XPicture_putU32(bytes + 36, XPicture_checksum(bytes, XPICTURE_HEADER_SIZE));
        XFree_System(self->m_data->m_data);
        self->m_data->m_data = (char*)bytes;
        self->m_data->m_dataSize = XPICTURE_HEADER_SIZE;
        self->m_data->m_dataCapacity = XPICTURE_HEADER_SIZE;
        self->m_data->m_isNull = false;
        return true;
    }
    return XPicture_validateStreamData(self->m_data->m_data,
                                       self->m_data->m_dataSize);
}

static bool XPicture_appendRecord(XPicture* self, uint8_t opcode,
                                  const uint8_t* payload, uint32_t payloadSize)
{
    uint64_t required;
    uint32_t oldSize, newSize, capacity, count;
    char* replacement;
    uint8_t* bytes;
    if (!XPicture_prepareStream(self)) return false;
    if (payloadSize != 0 && !payload) return false;
    oldSize = self->m_data->m_dataSize;
    required = (uint64_t)oldSize + XPICTURE_RECORD_HEADER_SIZE + payloadSize;
    if (required > UINT32_MAX) return false;
    newSize = (uint32_t)required;
    if (newSize > self->m_data->m_dataCapacity)
    {
        capacity = self->m_data->m_dataCapacity ? self->m_data->m_dataCapacity : XPICTURE_HEADER_SIZE;
        while (capacity < newSize)
        {
            if (capacity > UINT32_MAX / 2u) { capacity = newSize; break; }
            capacity *= 2u;
        }
        replacement = (char*)XRealloc_System(self->m_data->m_data, capacity);
        if (!replacement) return false;
        self->m_data->m_data = replacement;
        self->m_data->m_dataCapacity = capacity;
    }
    bytes = (uint8_t*)self->m_data->m_data;
    bytes[oldSize] = opcode;
    bytes[oldSize + 1u] = 0;
    XPicture_putU16(bytes + oldSize + 2u, 0);
    XPicture_putU32(bytes + oldSize + 4u, payloadSize);
    if (payloadSize != 0)
        memcpy(bytes + oldSize + XPICTURE_RECORD_HEADER_SIZE, payload, payloadSize);
    count = XPicture_getU32(bytes + 12);
    if (count == UINT32_MAX) return false;
    XPicture_putU32(bytes + 12, count + 1u);
    XPicture_putU32(bytes + 16, newSize - XPICTURE_HEADER_SIZE);
    self->m_data->m_dataSize = newSize;
    XPicture_putU32(bytes + 36, XPicture_checksum(bytes, newSize));
    self->m_data->m_isNull = false;
    return true;
}

static void XPicture_updateBounds(XPicture* self, const XRect* rect)
{
    int64_t left, top, right, bottom, oldRight, oldBottom;
    XRect current;
    if (!self || !self->m_data || !rect || XRect_isEmpty(rect)) return;
    XPicture_boundingRect(self, &current);
    if (XRect_isEmpty(&current))
    {
        XPicture_setBoundingRect(self, rect);
        return;
    }
    left = current.x < rect->x ? current.x : rect->x;
    top = current.y < rect->y ? current.y : rect->y;
    oldRight = (int64_t)current.x + current.width;
    oldBottom = (int64_t)current.y + current.height;
    right = (int64_t)rect->x + rect->width;
    bottom = (int64_t)rect->y + rect->height;
    if (oldRight > right) right = oldRight;
    if (oldBottom > bottom) bottom = oldBottom;
    if (right - left > INT_MAX || bottom - top > INT_MAX ||
        left < INT_MIN || top < INT_MIN || left > INT_MAX || top > INT_MAX)
        return;
    current.x = (int)left;
    current.y = (int)top;
    current.width = (int)(right - left);
    current.height = (int)(bottom - top);
    XPicture_setBoundingRect(self, &current);
}

bool XPicture_recordDrawLine(XPicture* self, int x1, int y1, int x2, int y2)
{
    uint8_t payload[16];
    XPicture_putI32(payload + 0, x1);
    XPicture_putI32(payload + 4, y1);
    XPicture_putI32(payload + 8, x2);
    XPicture_putI32(payload + 12, y2);
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawLine, payload, sizeof(payload)))
        return false;
    {
        XRect bounds;
        int64_t left = x1 < x2 ? x1 : x2;
        int64_t top = y1 < y2 ? y1 : y2;
        int64_t right = x1 > x2 ? x1 : x2;
        int64_t bottom = y1 > y2 ? y1 : y2;
        bounds.x = x1 < x2 ? x1 : x2;
        bounds.y = y1 < y2 ? y1 : y2;
        if (right - left >= INT_MAX || bottom - top >= INT_MAX)
            return true;
        bounds.x = (int)left;
        bounds.y = (int)top;
        bounds.width = (int)(right - left + 1);
        bounds.height = (int)(bottom - top + 1);
        XPicture_updateBounds(self, &bounds);
    }
    return true;
}

bool XPicture_recordSetPen(XPicture* self, uint32_t color, int style,
                           int width, int cap, int join)
{
    uint8_t payload[20];
    if (!self) return false;
    XPicture_putU32(payload + 0, color);
    XPicture_putI32(payload + 4, style);
    XPicture_putI32(payload + 8, width);
    XPicture_putI32(payload + 12, cap);
    XPicture_putI32(payload + 16, join);
    return XPicture_appendRecord(self, XPictureOpcode_SetPen, payload,
                                 sizeof(payload));
}

bool XPicture_recordSetOpacity(XPicture* self, float opacity)
{
    uint8_t payload[4];
    if (!self || !isfinite(opacity)) return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    XPicture_putF32(payload, opacity);
    return XPicture_appendRecord(self, XPictureOpcode_SetOpacity, payload,
                                 sizeof(payload));
}

bool XPicture_recordSetCompositionMode(XPicture* self, int mode)
{
    uint8_t payload[4];
    if (!self || mode < (int)XPainterCompositionMode_SourceOver ||
        mode > (int)XPainterCompositionMode_RasterOp_NotDestination)
        return false;
    XPicture_putI32(payload, mode);
    return XPicture_appendRecord(self, XPictureOpcode_SetCompositionMode,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetRenderHints(XPicture* self, uint32_t hints)
{
    uint8_t payload[4];
    if (!self) return false;
    /* Qt QPicturePaintEngine::updateRenderHints serializes the complete
       RenderHints bit field as one quint32 (qpaintengine_pic.cpp:274-281).
       Keep the bit field opaque so future hint bits remain stream-compatible. */
    XPicture_putU32(payload, hints);
    return XPicture_appendRecord(self, XPictureOpcode_SetRenderHints,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetBrushOrigin(XPicture* self, float x, float y)
{
    uint8_t payload[8];
    if (!self || !isfinite(x) || !isfinite(y)) return false;
    /* Qt QPicturePaintEngine::updateBrushOrigin() serializes one QPointF
       (qpaintengine_pic.cpp:194-202).  The portable stream stores the two
       coordinates as IEEE-754 single precision values, matching XPainter's
       existing brush-origin state and avoiding host ABI padding. */
    XPicture_putF32(payload + 0u, x);
    XPicture_putF32(payload + 4u, y);
    return XPicture_appendRecord(self, XPictureOpcode_SetBrushOrigin,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetTransform(XPicture* self,
                                 const XImageTransform* matrix,
                                 bool enabled)
{
    uint8_t payload[XPICTURE_TRANSFORM_PAYLOAD_SIZE];
    if (!self || !XPicture_transformIsFinite(matrix)) return false;
    /* Qt QPicturePaintEngine::updateMatrix() writes the complete QTransform
       followed by a combine flag (qpaintengine_pic.cpp:235-243).  The
       embedded state keeps the world matrix and its enable bit separately,
       so this fixed record carries nine floats followed by a four-byte
       enable flag.  No host alignment or bool representation is exposed. */
    XPicture_putF32(payload + 0u, matrix->m11);
    XPicture_putF32(payload + 4u, matrix->m12);
    XPicture_putF32(payload + 8u, matrix->m21);
    XPicture_putF32(payload + 12u, matrix->m22);
    XPicture_putF32(payload + 16u, matrix->dx);
    XPicture_putF32(payload + 20u, matrix->dy);
    XPicture_putF32(payload + 24u, matrix->m13);
    XPicture_putF32(payload + 28u, matrix->m23);
    XPicture_putF32(payload + 32u, matrix->m33);
    XPicture_putU32(payload + 36u, enabled ? 1u : 0u);
    return XPicture_appendRecord(self, XPictureOpcode_SetTransform,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetWindow(XPicture* self, const XRect* window)
{
    uint8_t payload[16];
    if (!self || !window) return false;
    /* QPainter::setWindow() stores all four QRect integers and then marks the
       transform dirty.  The portable record keeps the rectangle independent
       from the host ABI; zero or negative extents remain representable and
       are rejected only when the effective transform is used. */
    XPicture_putI32(payload + 0u, window->x);
    XPicture_putI32(payload + 4u, window->y);
    XPicture_putI32(payload + 8u, window->width);
    XPicture_putI32(payload + 12u, window->height);
    return XPicture_appendRecord(self, XPictureOpcode_SetWindow,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetViewport(XPicture* self, const XRect* viewport)
{
    uint8_t payload[16];
    if (!self || !viewport) return false;
    /* QPainter::setViewport() follows the same fixed QRect state update as
       setWindow().  Preserve every integer exactly for later queries. */
    XPicture_putI32(payload + 0u, viewport->x);
    XPicture_putI32(payload + 4u, viewport->y);
    XPicture_putI32(payload + 8u, viewport->width);
    XPicture_putI32(payload + 12u, viewport->height);
    return XPicture_appendRecord(self, XPictureOpcode_SetViewport,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetViewTransformEnabled(XPicture* self, bool enabled)
{
    uint8_t payload[4];
    if (!self) return false;
    /* Keep bool representation explicit: the validator accepts only 0/1 so
       malformed streams cannot introduce an ambiguous view state. */
    XPicture_putU32(payload, enabled ? 1u : 0u);
    return XPicture_appendRecord(self, XPictureOpcode_SetViewTransformEnabled,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetBackgroundColor(XPicture* self, uint32_t color)
{
    uint8_t payload[4];
    if (!self) return false;
    /* Qt QPicturePaintEngine::updateBackground() first writes the background
       color as PdcSetBkColor (qpaintengine_pic.cpp:219-231).  Qt playback
       restores this as a solid brush color; the portable stream stores only
       the fixed ARGB32 color and applies that same solid-brush rule. */
    XPicture_putU32(payload, color);
    return XPicture_appendRecord(self, XPictureOpcode_SetBackgroundColor,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetBackgroundMode(XPicture* self, int mode)
{
    uint8_t payload[4];
    if (!self || (mode != 0 && mode != 1)) return false;
    /* Qt updateBackground() emits PdcSetBkMode separately from the color.
       The embedded representation deliberately accepts only the two Qt
       modes: 0 transparent and 1 opaque (qpaintengine_pic.cpp:219-231). */
    XPicture_putI32(payload, mode);
    return XPicture_appendRecord(self, XPictureOpcode_SetBackgroundMode,
                                 payload, sizeof(payload));
}

bool XPicture_recordSetBrush(XPicture* self, int style, uint32_t color)
{
    uint8_t payload[8];
    if (!self) return false;
    /* Qt QPicturePaintEngine::updateBrush() serializes a QBrush object
       (qpaintengine_pic.cpp:176-184).  This embedded opcode intentionally
       carries only the fixed basic style/color subset, matching the
       XPainter portable brush state without host-dependent QBrush data. */
    XPicture_putI32(payload + 0u, style);
    XPicture_putU32(payload + 4u, color);
    return XPicture_appendRecord(self, XPictureOpcode_SetBrush,
                                 payload, sizeof(payload));
}

bool XPicture_recordFillRect(XPicture* self, const XRect* rect, uint32_t color)
{
    uint8_t payload[20];
    if (!rect || XRect_isEmpty(rect)) return false;
    XPicture_putI32(payload + 0, rect->x);
    XPicture_putI32(payload + 4, rect->y);
    XPicture_putI32(payload + 8, rect->width);
    XPicture_putI32(payload + 12, rect->height);
    XPicture_putU32(payload + 16, color);
    if (!XPicture_appendRecord(self, XPictureOpcode_FillRect, payload, sizeof(payload)))
        return false;
    XPicture_updateBounds(self, rect);
    return true;
}

static void XPicture_updatePointsBounds(XPicture* self,
                                            const XPoint* points, int count)
{
    XRect bounds;
    int64_t left, top, right, bottom;
    int i;
    if (!points || count <= 0) return;
    left = right = points[0].x;
    top = bottom = points[0].y;
    for (i = 1; i < count; ++i)
    {
        if (points[i].x < left) left = points[i].x;
        if (points[i].x > right) right = points[i].x;
        if (points[i].y < top) top = points[i].y;
        if (points[i].y > bottom) bottom = points[i].y;
    }
    if (right - left >= INT_MAX || bottom - top >= INT_MAX) return;
    bounds.x = (int)left;
    bounds.y = (int)top;
    bounds.width = (int)(right - left + 1);
    bounds.height = (int)(bottom - top + 1);
    XPicture_updateBounds(self, &bounds);
}

bool XPicture_recordDrawShape(XPicture* self, int shapeOp, const XRect* rect,
                              int startAngle, int spanAngle, bool filled,
                              int xRadius, int yRadius)
{
    uint8_t payload[XPICTURE_SHAPE_PAYLOAD_SIZE];
    XRect normalized;
    if (!self || !rect) return false;
    normalized = XRect_normalized(rect);
    if (normalized.width <= 0 || normalized.height <= 0 || shapeOp < 1 ||
        shapeOp > 5)
        return true;
    memset(payload, 0, sizeof(payload));
    XPicture_putI32(payload + 0, shapeOp);
    XPicture_putI32(payload + 4, normalized.x);
    XPicture_putI32(payload + 8, normalized.y);
    XPicture_putI32(payload + 12, normalized.width);
    XPicture_putI32(payload + 16, normalized.height);
    XPicture_putI32(payload + 20, startAngle);
    XPicture_putI32(payload + 24, spanAngle);
    XPicture_putU32(payload + 28, filled ? 1u : 0u);
    XPicture_putI32(payload + 32, xRadius);
    XPicture_putI32(payload + 36, yRadius);
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawShape, payload,
                               sizeof(payload)))
        return false;
    XPicture_updateBounds(self, &normalized);
    return true;
}

bool XPicture_recordDrawPolyline(XPicture* self, const XPoint* points,
                                 int count)
{
    uint8_t* payload;
    uint32_t countU;
    uint32_t payloadSize;
    int i;
    if (!self || !points || count < 2) return true;
    if (count > XPICTURE_MAX_POINTS) return false;
    countU = (uint32_t)count;
    payloadSize = XPICTURE_POINT_HEADER_SIZE + countU * 8u;
    payload = (uint8_t*)XMalloc_System(payloadSize);
    if (!payload) return false;
    XPicture_putU32(payload, countU);
    for (i = 0; i < count; ++i)
    {
        XPicture_putI32(payload + 4 + (uint32_t)i * 8u, points[i].x);
        XPicture_putI32(payload + 8 + (uint32_t)i * 8u, points[i].y);
    }
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawPolyline, payload,
                               payloadSize))
    {
        XFree_System(payload);
        return false;
    }
    XFree_System(payload);
    XPicture_updatePointsBounds(self, points, count);
    return true;
}

bool XPicture_recordDrawPolygon(XPicture* self, const XPoint* points, int count,
                                bool filled, int fillRule)
{
    uint8_t* payload;
    uint32_t countU;
    uint32_t payloadSize;
    int i;
    if (!self || !points || count < 2) return true;
    if (count > XPICTURE_MAX_POINTS || fillRule < 0 || fillRule > 1)
        return false;
    countU = (uint32_t)count;
    payloadSize = XPICTURE_POLYGON_HEADER_SIZE + countU * 8u;
    payload = (uint8_t*)XMalloc_System(payloadSize);
    if (!payload) return false;
    XPicture_putU32(payload, countU);
    XPicture_putU32(payload + 4, filled ? 1u : 0u);
    XPicture_putU32(payload + 8, (uint32_t)fillRule);
    for (i = 0; i < count; ++i)
    {
        XPicture_putI32(payload + 12 + (uint32_t)i * 8u, points[i].x);
        XPicture_putI32(payload + 16 + (uint32_t)i * 8u, points[i].y);
    }
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawPolygon, payload,
                               payloadSize))
    {
        XFree_System(payload);
        return false;
    }
    XFree_System(payload);
    XPicture_updatePointsBounds(self, points, count);
    return true;
}

bool XPicture_recordDrawPoints(XPicture* self, const XPoint* points, int count)
{
    uint8_t* payload;
    uint32_t countU;
    uint32_t payloadSize;
    int i;
    if (!self || !points || count < 1) return true;
    if (count > XPICTURE_MAX_POINTS) return false;
    countU = (uint32_t)count;
    payloadSize = XPICTURE_POINT_HEADER_SIZE + countU * 8u;
    payload = (uint8_t*)XMalloc_System(payloadSize);
    if (!payload) return false;
    XPicture_putU32(payload, countU);
    for (i = 0; i < count; ++i)
    {
        XPicture_putI32(payload + 4 + (uint32_t)i * 8u, points[i].x);
        XPicture_putI32(payload + 8 + (uint32_t)i * 8u, points[i].y);
    }
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawPoints, payload,
                               payloadSize))
    {
        XFree_System(payload);
        return false;
    }
    XFree_System(payload);
    XPicture_updatePointsBounds(self, points, count);
    return true;
}

#if XPAINTER_PATH_ON
static void XPicture_updatePathBounds(XPicture* self,
                                      const XPainterPath* path)
{
    XRect bounds;
    float minX, minY, maxX, maxY;
    int i, left, top, right, bottom;
    if (!self || !path || path->m_elementCount <= 0) return;
    minX = maxX = path->m_elements[0].m_x1;
    minY = maxY = path->m_elements[0].m_y1;
    for (i = 1; i < path->m_elementCount; ++i)
    {
        float x = path->m_elements[i].m_x1;
        float y = path->m_elements[i].m_y1;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }
    left = (int)floorf(minX);
    top = (int)floorf(minY);
    right = (int)ceilf(maxX);
    bottom = (int)ceilf(maxY);
    if (right - left >= INT_MAX || bottom - top >= INT_MAX ||
        left < INT_MIN || top < INT_MIN || left > INT_MAX || top > INT_MAX)
        return;
    bounds.x = left;
    bounds.y = top;
    bounds.width = right - left + 1;
    bounds.height = bottom - top + 1;
    XPicture_updateBounds(self, &bounds);
}

bool XPicture_recordDrawPath(XPicture* self, int pathOp,
                             const struct XPainterPath* path)
{
    uint8_t* payload;
    uint32_t countU, payloadSize;
    uint32_t i;
    if (!self || !path || path->m_elementCount <= 0) return true;
    if (pathOp < (int)XPainterPathOp_Draw || pathOp > (int)XPainterPathOp_Stroke)
        return false;
    if (path->m_elementCount > XPICTURE_MAX_PATH_ELEMENTS) return false;
    countU = (uint32_t)path->m_elementCount;
    payloadSize = XPICTURE_PATH_HEADER_SIZE +
                  countU * XPICTURE_PATH_ELEMENT_SIZE;
    payload = (uint8_t*)XMalloc_System(payloadSize);
    if (!payload) return false;
    XPicture_putU32(payload, (uint32_t)pathOp);
    XPicture_putU32(payload + 4, countU);
    for (i = 0; i < countU; ++i)
    {
        uint8_t* dest = payload + XPICTURE_PATH_HEADER_SIZE +
                        i * XPICTURE_PATH_ELEMENT_SIZE;
        const XPainterPathElement* element = &path->m_elements[i];
        XPicture_putU32(dest, (uint32_t)element->m_type);
        XPicture_putF32(dest + 4, element->m_x1);
        XPicture_putF32(dest + 8, element->m_y1);
    }
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawPath, payload,
                               payloadSize))
    {
        XFree_System(payload);
        return false;
    }
    XFree_System(payload);
    XPicture_updatePathBounds(self, path);
    return true;
}
#endif /* XPAINTER_PATH_ON */

bool XPicture_recordSave(XPicture* self)
{
    return XPicture_appendRecord(self, XPictureOpcode_Save, NULL, 0);
}

bool XPicture_recordRestore(XPicture* self)
{
    return XPicture_appendRecord(self, XPictureOpcode_Restore, NULL, 0);
}

bool XPicture_recordDrawImage(XPicture* self, const XImage* image, int x, int y)
{
    XSize size;
    XPoint offset;
    int bytesPerLine, imageSize, colorCount, i;
    uint64_t payloadSize64;
    uint8_t* payload;
    uint32_t dprBits;
    float dpr;
    const uint8_t* bits;
    if (!image || XImage_isNull(image)) return false;
    XImage_size(image, &size);
    bytesPerLine = XImage_bytesPerLine(image);
    imageSize = XImage_sizeInBytes(image);
    colorCount = XImage_colorCount(image);
    bits = XImage_constBits(image);
    dpr = XImage_devicePixelRatio(image);
    if (size.width <= 0 || size.height <= 0 || bytesPerLine <= 0 || imageSize <= 0 ||
        !bits || colorCount < 0 || colorCount > 65536 || !(dpr > 0.0f) || !isfinite(dpr))
        return false;
    payloadSize64 = (uint64_t)XPICTURE_IMAGE_FIXED_SIZE +
                    (uint64_t)colorCount * 4u + (uint32_t)imageSize;
    if (payloadSize64 > UINT32_MAX) return false;
    payload = (uint8_t*)XMalloc_System((size_t)payloadSize64);
    if (!payload) return false;
    memset(payload, 0, (size_t)payloadSize64);
    XPicture_putI32(payload + 0, x);
    XPicture_putI32(payload + 4, y);
    XPicture_putU32(payload + 8, (uint32_t)size.width);
    XPicture_putU32(payload + 12, (uint32_t)size.height);
    XPicture_putU32(payload + 16, (uint32_t)XImage_format(image));
    XPicture_putU32(payload + 20, (uint32_t)bytesPerLine);
    XPicture_putU32(payload + 24, (uint32_t)imageSize);
    memcpy(&dprBits, &dpr, sizeof(dprBits));
    XPicture_putU32(payload + 28, dprBits);
    XPicture_putI32(payload + 32, XImage_dotsPerMeterX(image));
    XPicture_putI32(payload + 36, XImage_dotsPerMeterY(image));
    XImage_offset(image, &offset);
    XPicture_putI32(payload + 40, offset.x);
    XPicture_putI32(payload + 44, offset.y);
    XPicture_putU32(payload + 48, (uint32_t)colorCount);
    for (i = 0; i < colorCount; ++i)
        XPicture_putU32(payload + XPICTURE_IMAGE_FIXED_SIZE + (uint32_t)i * 4u,
                        XImage_color(image, i));
    memcpy(payload + XPICTURE_IMAGE_FIXED_SIZE + (uint32_t)colorCount * 4u,
           bits, (size_t)imageSize);
    if (!XPicture_appendRecord(self, XPictureOpcode_DrawImage, payload,
                               (uint32_t)payloadSize64))
    {
        XFree_System(payload);
        return false;
    }
    XFree_System(payload);
    {
        XRect bounds = { x, y, size.width, size.height };
        XPicture_updateBounds(self, &bounds);
    }
    return true;
}

void XPicture_clearCommands(XPicture* self)
{
    if (!self || !self->m_data) return;
    XPicture_detach(self);
    if (!XPicture_isDetached(self)) return;
    XFree_System(self->m_data->m_data);
    self->m_data->m_data = NULL;
    self->m_data->m_dataSize = 0;
    self->m_data->m_dataCapacity = 0;
    self->m_data->m_isNull = true;
    self->m_data->m_boundingX = self->m_data->m_boundingY = 0;
    self->m_data->m_boundingW = self->m_data->m_boundingH = 0;
}

bool XPicture_isValidStream(const XPicture* self)
{
    return self && self->m_data &&
           XPicture_validateStreamData(self->m_data->m_data,
                                        self->m_data->m_dataSize);
}

static void XPicture_imageDataCleanup(void* info)
{
    XFree_System(info);
}

#if XPAINTER_PATH_ON
static bool XPicture_rebuildPath(const uint8_t* payload, XPainterPath* path)
{
    uint32_t count, i;
    bool ok;
    count = XPicture_getU32(payload + 4);
    XPainterPath_init(path);
    for (i = 0; i < count; ++i)
    {
        const uint8_t* element = payload + XPICTURE_PATH_HEADER_SIZE +
                                 i * XPICTURE_PATH_ELEMENT_SIZE;
        uint32_t type = XPicture_getU32(element);
        float x = XPicture_getF32(element + 4);
        float y = XPicture_getF32(element + 8);
        if (type == (uint32_t)XPainterPathElement_MoveTo)
            ok = XPainterPath_moveTo(path, x, y);
        else if (type == (uint32_t)XPainterPathElement_LineTo)
            ok = XPainterPath_lineTo(path, x, y);
        else if (type == (uint32_t)XPainterPathElement_CurveTo)
        {
            const uint8_t* e1;
            const uint8_t* e2;
            if (i + 2u >= count)
            {
                XPainterPath_deinit(path);
                return false;
            }
            e1 = element + XPICTURE_PATH_ELEMENT_SIZE;
            e2 = e1 + XPICTURE_PATH_ELEMENT_SIZE;
            if (XPicture_getU32(e1) != (uint32_t)XPainterPathElement_CurveToData ||
                XPicture_getU32(e2) != (uint32_t)XPainterPathElement_CurveToData)
            {
                XPainterPath_deinit(path);
                return false;
            }
            ok = XPainterPath_cubicTo(path, x, y,
                                      XPicture_getF32(e1 + 4),
                                      XPicture_getF32(e1 + 8),
                                      XPicture_getF32(e2 + 4),
                                      XPicture_getF32(e2 + 8));
            i += 2u;
        }
        else
            ok = false;
        if (!ok)
        {
            XPainterPath_deinit(path);
            return false;
        }
    }
    return true;
}
#endif /* XPAINTER_PATH_ON */

/*
 * 录制时虚线/点线已被拆分为若干实线段；回放时若画笔仍为虚线样式会被
 * 二次拆分，因此回放期间强制改用实线画笔，结束后恢复调用方原样式。
 */
static bool XPicture_play_inner(const XPicture* self, XPainter* painter)
{
    const uint8_t* bytes;
    uint32_t offset, commandCount, index;
    if (!self || !self->m_data) return false;
    if (self->m_data->m_dataSize == 0) return true;
    if (!painter || !XPicture_isValidStream(self)) return false;
    bytes = (const uint8_t*)self->m_data->m_data;
    commandCount = XPicture_getU32(bytes + 12);
    offset = XPICTURE_HEADER_SIZE;
    for (index = 0; index < commandCount; ++index)
    {
        uint8_t opcode = bytes[offset];
        uint32_t length = XPicture_getU32(bytes + offset + 4);
        const uint8_t* payload = bytes + offset + XPICTURE_RECORD_HEADER_SIZE;
        bool ok = false;
        offset += XPICTURE_RECORD_HEADER_SIZE;
        if (opcode == XPictureOpcode_SetPen)
        {
            /* QPicture 的 updatePen 记录的是完整 QPen。这里保留同一
               个便携快照；未编译的可选样式字段使用流中的值而不会
               影响基础颜色 ABI。回放不调用公开 setter，避免在 Picture
               后端上再次追加 SetPen 命令。 */
            painter->m_state.m_penColor = XPicture_getU32(payload + 0);
#if XPAINTER_PENSTYLE_ON
            painter->m_state.m_penStyle =
                (XPainterPenStyle)XPicture_getI32(payload + 4);
            painter->m_state.m_penWidth = XPicture_getI32(payload + 8);
            painter->m_state.m_penCap =
                (XPainterPenCapStyle)XPicture_getI32(payload + 12);
            painter->m_state.m_penJoin =
                (XPainterPenJoinStyle)XPicture_getI32(payload + 16);
#endif /* XPAINTER_PENSTYLE_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetOpacity)
        {
            painter->m_state.m_opacity = XPicture_getF32(payload);
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetCompositionMode)
        {
            painter->m_state.m_compositionMode =
                (XPainterCompositionMode)XPicture_getI32(payload);
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetRenderHints)
        {
#if XPAINTER_RENDERHINT_ON
            /* Replay updates the painter state directly, just like the
               existing pen/opacity/composition state opcodes.  Calling the
               public setter would append a second command on a Picture
               backend and would reject an inactive callback-only painter. */
            painter->m_state.m_renderHints = XPicture_getU32(payload);
#endif /* XPAINTER_RENDERHINT_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetBrushOrigin)
        {
#if XPAINTER_BRUSH_ORIGIN_ON
            /* Restore the exact float state.  Calling setBrushOrigin() would
               append a new record when replaying into a Picture backend. */
            painter->m_state.m_brushOriginX = XPicture_getF32(payload + 0u);
            painter->m_state.m_brushOriginY = XPicture_getF32(payload + 4u);
#endif /* XPAINTER_BRUSH_ORIGIN_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetTransform)
        {
            /* Restore matrix state directly.  Calling public setters while
               replaying into a Picture backend would append duplicate
               records, and would also lose the disabled-world-matrix bit. */
            painter->m_state.m_transform.m11 =
                XPicture_getF32(payload + 0u);
            painter->m_state.m_transform.m12 =
                XPicture_getF32(payload + 4u);
            painter->m_state.m_transform.m21 =
                XPicture_getF32(payload + 8u);
            painter->m_state.m_transform.m22 =
                XPicture_getF32(payload + 12u);
            painter->m_state.m_transform.dx =
                XPicture_getF32(payload + 16u);
            painter->m_state.m_transform.dy =
                XPicture_getF32(payload + 20u);
            painter->m_state.m_transform.m13 =
                XPicture_getF32(payload + 24u);
            painter->m_state.m_transform.m23 =
                XPicture_getF32(payload + 28u);
            painter->m_state.m_transform.m33 =
                XPicture_getF32(payload + 32u);
#if XPAINTER_WORLD_MATRIX_ON
            painter->m_state.m_worldMatrixEnabled =
                XPicture_getU32(payload + 36u) != 0u;
#endif /* XPAINTER_WORLD_MATRIX_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetWindow)
        {
#if XPAINTER_VIEW_TRANSFORM_ON
            /* Restore the complete logical window without calling the public
               setter, which would append a duplicate record on a Picture
               backend.  Qt enables view mapping after setWindow(). */
            painter->m_state.m_window.x =
                (int)XPicture_getI32(payload + 0u);
            painter->m_state.m_window.y =
                (int)XPicture_getI32(payload + 4u);
            painter->m_state.m_window.width =
                (int)XPicture_getI32(payload + 8u);
            painter->m_state.m_window.height =
                (int)XPicture_getI32(payload + 12u);
            painter->m_state.m_viewTransformEnabled = true;
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetViewport)
        {
#if XPAINTER_VIEW_TRANSFORM_ON
            /* Restore the complete device viewport and mirror
               QPainter::setViewport()'s immediate enable semantics. */
            painter->m_state.m_viewport.x =
                (int)XPicture_getI32(payload + 0u);
            painter->m_state.m_viewport.y =
                (int)XPicture_getI32(payload + 4u);
            painter->m_state.m_viewport.width =
                (int)XPicture_getI32(payload + 8u);
            painter->m_state.m_viewport.height =
                (int)XPicture_getI32(payload + 12u);
            painter->m_state.m_viewTransformEnabled = true;
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetViewTransformEnabled)
        {
#if XPAINTER_VIEW_TRANSFORM_ON
            /* The fixed bool payload has already been range-checked by the
               stream validator; replay only restores state. */
            painter->m_state.m_viewTransformEnabled =
                XPicture_getU32(payload) != 0u;
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetBackgroundColor)
        {
            /* QPicture playback restores PdcSetBkColor as a solid brush.
               Update the always-present color state first; the optional
               background brush mirrors it only when both features exist. */
            painter->m_state.m_backgroundColor = XPicture_getU32(payload);
#if XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON
            painter->m_state.m_backgroundBrush.m_style =
                XPainterBrushStyle_SolidPattern;
            painter->m_state.m_backgroundBrush.m_color =
                painter->m_state.m_backgroundColor;
            memset(&painter->m_state.m_backgroundBrush.m_gradient, 0,
                   sizeof(painter->m_state.m_backgroundBrush.m_gradient));
#endif /* XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetBackgroundMode)
        {
#if XPAINTER_BACKGROUND_ON
            painter->m_state.m_backgroundMode =
                (XPainterBackgroundMode)XPicture_getI32(payload);
#endif /* XPAINTER_BACKGROUND_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_SetBrush)
        {
            painter->m_state.m_brushColor = XPicture_getU32(payload + 4u);
#if XPAINTER_BRUSH_ON
            painter->m_state.m_brush.m_style =
                (XPainterBrushStyle)XPicture_getI32(payload + 0u);
            painter->m_state.m_brush.m_color =
                painter->m_state.m_brushColor;
            memset(&painter->m_state.m_brush.m_gradient, 0,
                   sizeof(painter->m_state.m_brush.m_gradient));
#endif /* XPAINTER_BRUSH_ON */
            ok = true;
        }
        else if (opcode == XPictureOpcode_DrawLine)
        {
            if (!painter->m_drawLine) return false;
            ok = painter->m_drawLine(painter, (int)XPicture_getI32(payload + 0),
                                   (int)XPicture_getI32(payload + 4),
                                   (int)XPicture_getI32(payload + 8),
                                   (int)XPicture_getI32(payload + 12));
        }
        else if (opcode == XPictureOpcode_FillRect)
        {
            XRect rect;
            if (!painter->m_fillRect) return false;
            rect.x = (int)XPicture_getI32(payload + 0);
            rect.y = (int)XPicture_getI32(payload + 4);
            rect.width = (int)XPicture_getI32(payload + 8);
            rect.height = (int)XPicture_getI32(payload + 12);
            ok = painter->m_fillRect(painter, &rect, XPicture_getU32(payload + 16));
        }
#if XPAINTER_SHAPE_ON
        else if (opcode == XPictureOpcode_DrawShape)
        {
            XRect rect;
            XPainterShapeOp shapeOp = (XPainterShapeOp)XPicture_getI32(payload);
            int startAngle = (int)XPicture_getI32(payload + 20);
            int spanAngle = (int)XPicture_getI32(payload + 24);
            bool filled = XPicture_getU32(payload + 28) != 0u;
            int xRadius = (int)XPicture_getI32(payload + 32);
            int yRadius = (int)XPicture_getI32(payload + 36);
            rect.x = (int)XPicture_getI32(payload + 4);
            rect.y = (int)XPicture_getI32(payload + 8);
            rect.width = (int)XPicture_getI32(payload + 12);
            rect.height = (int)XPicture_getI32(payload + 16);
            if (painter->m_drawShape)
            {
                ok = painter->m_drawShape(painter, shapeOp, &rect,
                                         startAngle, spanAngle, filled,
                                         xRadius, yRadius);
            }
            else
            {
                if (painter->m_deviceKind == XPainterDevice_None)
                    return false;
                switch (shapeOp)
                {
                case XPainterShapeOp_Ellipse:
                    ok = XPainter_drawEllipse(painter, &rect);
                    break;
                case XPainterShapeOp_Arc:
                    ok = XPainter_drawArc(painter, &rect, startAngle, spanAngle);
                    break;
                case XPainterShapeOp_Pie:
                    ok = XPainter_drawPie(painter, &rect, startAngle, spanAngle);
                    break;
                case XPainterShapeOp_Chord:
                    ok = XPainter_drawChord(painter, &rect, startAngle, spanAngle);
                    break;
                case XPainterShapeOp_RoundedRect:
                    ok = XPainter_drawRoundedRect(painter, &rect, xRadius, yRadius);
                    break;
                default:
                    return false;
                }
            }
        }
#endif /* XPAINTER_SHAPE_ON */
#if XPAINTER_POLYGON_ON
        else if (opcode == XPictureOpcode_DrawPolyline ||
                 opcode == XPictureOpcode_DrawPolygon ||
                 opcode == XPictureOpcode_DrawPoints)
        {
            uint32_t count = XPicture_getU32(payload);
            XPoint* pts;
            uint32_t base = (opcode == XPictureOpcode_DrawPolygon)
                                ? 12u : 4u;
            uint32_t i;
            if (count == 0u) return false;
            pts = (XPoint*)XMalloc_System((size_t)count * sizeof(XPoint));
            if (!pts) return false;
            for (i = 0; i < count; ++i)
            {
                pts[i].x = (int)XPicture_getI32(payload + base + i * 8u);
                pts[i].y = (int)XPicture_getI32(payload + base + i * 8u + 4u);
            }
            if (opcode == XPictureOpcode_DrawPolyline)
            {
                if (painter->m_drawPolyline)
                    ok = painter->m_drawPolyline(painter, pts, (int)count);
                else
                {
                    if (painter->m_deviceKind == XPainterDevice_None)
                    {
                        XFree_System(pts);
                        return false;
                    }
                    ok = XPainter_drawPolyline(painter, pts, (int)count);
                }
            }
            else if (opcode == XPictureOpcode_DrawPolygon)
            {
                bool filled = XPicture_getU32(payload + 4) != 0u;
                XPainterFillRule fillRule =
                    (XPainterFillRule)XPicture_getU32(payload + 8);
                if (painter->m_drawPolygon)
                    ok = painter->m_drawPolygon(painter, pts, (int)count,
                                                filled, fillRule);
                else
                {
                    if (painter->m_deviceKind == XPainterDevice_None)
                    {
                        XFree_System(pts);
                        return false;
                    }
                    (void)filled;
                    ok = XPainter_drawPolygon(painter, pts, (int)count,
                                              fillRule);
                }
            }
            else
            {
                if (painter->m_drawPoints)
                    ok = painter->m_drawPoints(painter, pts, (int)count);
                else
                {
                    if (painter->m_deviceKind == XPainterDevice_None)
                    {
                        XFree_System(pts);
                        return false;
                    }
                    ok = XPainter_drawPoints(painter, pts, (int)count);
                }
            }
            XFree_System(pts);
        }
#endif /* XPAINTER_POLYGON_ON */
#if XPAINTER_PATH_ON
        else if (opcode == XPictureOpcode_DrawPath)
        {
            XPainterPath path;
            XPainterPathOp pathOp = (XPainterPathOp)XPicture_getU32(payload);
            if (!XPicture_rebuildPath(payload, &path)) return false;
            if (painter->m_drawPath)
                ok = painter->m_drawPath(painter, pathOp, &path);
            else
            {
                if (painter->m_deviceKind == XPainterDevice_None)
                {
                    XPainterPath_deinit(&path);
                    return false;
                }
                if (pathOp == XPainterPathOp_Draw)
                    ok = XPainter_drawPath(painter, &path);
                else if (pathOp == XPainterPathOp_Fill)
                    ok = XPainter_fillPath(painter, &path);
                else if (pathOp == XPainterPathOp_Stroke)
                    ok = XPainter_strokePath(painter, &path);
                else
                    ok = false;
            }
            XPainterPath_deinit(&path);
        }
#endif /* XPAINTER_PATH_ON */
        else if (opcode == XPictureOpcode_Save || opcode == XPictureOpcode_Restore)
        {
            if (opcode == XPictureOpcode_Save)
            {
                if (!painter->m_save) return false;
                ok = painter->m_save(painter);
            }
            else
            {
                if (!painter->m_restore) return false;
                ok = painter->m_restore(painter);
            }
        }
        else if (opcode == XPictureOpcode_DrawImage)
        {
            uint32_t width = XPicture_getU32(payload + 8);
            uint32_t height = XPicture_getU32(payload + 12);
            XImageFormat format = (XImageFormat)XPicture_getU32(payload + 16);
            uint32_t bytesPerLine = XPicture_getU32(payload + 20);
            uint32_t imageSize = XPicture_getU32(payload + 24);
            uint32_t colorCount = XPicture_getU32(payload + 48);
            uint32_t dprBits = XPicture_getU32(payload + 28);
            float dpr;
            uint8_t* imageBytes;
            const uint8_t* imageData;
            XImage image;
            uint32_t i;
            if (!painter->m_drawImage) return false;
            imageData = payload + XPICTURE_IMAGE_FIXED_SIZE + colorCount * 4u;
            imageBytes = (uint8_t*)XMalloc_System(imageSize);
            if (!imageBytes) return false;
            memcpy(imageBytes, imageData, imageSize);
            XImage_init_ex_2(&image, (int)width, (int)height, format,
                             (int64_t)bytesPerLine, imageBytes,
                             XPicture_imageDataCleanup, imageBytes);
            if (XImage_isNull(&image))
            {
                XFree_System(imageBytes);
                return false;
            }
            memcpy(&dpr, &dprBits, sizeof(dpr));
            XImage_setDevicePixelRatio(&image, dpr);
            XImage_setDotsPerMeterX(&image, (int)XPicture_getI32(payload + 32));
            XImage_setDotsPerMeterY(&image, (int)XPicture_getI32(payload + 36));
            {
                XPoint imageOffset;
                imageOffset.x = (int)XPicture_getI32(payload + 40);
                imageOffset.y = (int)XPicture_getI32(payload + 44);
                XImage_setOffset(&image, &imageOffset);
            }
            if (colorCount != 0)
            {
                XImage_setColorCount(&image, (int)colorCount);
                for (i = 0; i < colorCount; ++i)
                    XImage_setColor(&image, (int)i,
                                    XPicture_getU32(payload + XPICTURE_IMAGE_FIXED_SIZE + i * 4u));
            }
            ok = painter->m_drawImage(painter, &image,
                                    (int)XPicture_getI32(payload + 0),
                                    (int)XPicture_getI32(payload + 4));
            XImage_deinit_base(&image);
        }
        if (!ok) return false;
        offset += length;
    }
    return true;
}

bool XPicture_play(const XPicture* self, XPainter* painter)
{
    bool ok;
#if XPAINTER_PENSTYLE_ON
    XPainterPenStyle savedStyle = painter
        ? painter->m_state.m_penStyle : XPainterPenStyle_SolidLine;
    if (painter)
        painter->m_state.m_penStyle = XPainterPenStyle_SolidLine;
#endif /* XPAINTER_PENSTYLE_ON */
    ok = XPicture_play_inner(self, painter);
#if XPAINTER_PENSTYLE_ON
    if (painter)
        painter->m_state.m_penStyle = savedStyle;
#endif /* XPAINTER_PENSTYLE_ON */
    return ok;
}

bool XPicture_load(XPicture* self, const XString* fileName)
{
    XFile* file; XByteArray* bytes; size_t size; bool success = false;
    if (!self || !self->m_data || !fileName || XContainer_isEmpty_base((const XContainer*)fileName)) return false;
    file = XFile_create_2(fileName); if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) { if (file) XClass_delete_base((XClass*)file); XPicture_reset(self); return false; }
    bytes = XIODevice_readAll_3((XIODevice*)file); XIODevice_close_base((XIODevice*)file); XClass_delete_base((XClass*)file); size = bytes ? XByteArray_size_base((const XContainer*)bytes) : 0;
    if (bytes && size > 0 && size <= UINT32_MAX && ((!XPicture_magicMatches(XByteArray_data(bytes), (uint32_t)size)) || XPicture_validateStreamData((const char*)XByteArray_data(bytes), (uint32_t)size))) { XPicture_setData(self, (const char*)XByteArray_data(bytes), (uint32_t)size); success = XPicture_size(self) == (uint32_t)size; }
    if (bytes) XByteArray_delete_base((XClass*)bytes); if (!success) XPicture_reset(self); return success;
}

bool XPicture_load_2(XPicture* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    bool result = XPicture_load(self, value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPicture_load_device(XPicture* self, XIODevice* device)
{
    XByteArray* bytes;
    size_t size;
    bool success = false;
    if (!self || !self->m_data || !device) return false;
    bytes = XIODevice_readAll_3(device);
    size = bytes ? XByteArray_size_base((const XContainer*)bytes) : 0;
    if (bytes && size <= UINT32_MAX && size != 0)
    {
        const char* data = XByteArray_constData(bytes);
        if ((!XPicture_magicMatches((const uint8_t*)data, (uint32_t)size) ||
             XPicture_validateStreamData(data, (uint32_t)size)))
        {
            XPicture_setData(self, data, (uint32_t)size);
            success = XPicture_size(self) == (uint32_t)size;
        }
    }
    if (bytes) XByteArray_delete_base((XClass*)bytes);
    if (!success) XPicture_reset(self);
    return success;
}

bool XPicture_save(const XPicture* self, const XString* fileName)
{
    XFile* file; size_t size; bool success;
    if (!self || !self->m_data || !fileName || XContainer_isEmpty_base((const XContainer*)fileName)) return false;
    if (XPicture_magicMatches((const uint8_t*)self->m_data->m_data,
                              self->m_data->m_dataSize) &&
        !XPicture_validateStreamData(self->m_data->m_data,
                                     self->m_data->m_dataSize))
        return false;
    file = XFile_create_2(fileName); if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create)) { if (file) XClass_delete_base((XClass*)file); return false; }
    size = self->m_data->m_dataSize;
    success = size == 0 || XIODevice_write_1((XIODevice*)file, self->m_data->m_data, (int64_t)size) == (int64_t)size;
    XIODevice_close_base((XIODevice*)file); XClass_delete_base((XClass*)file);
    return success;
}

bool XPicture_save_2(const XPicture* self, const char* fileName)
{
    XString* value = fileName ? XString_create_utf8(fileName) : NULL;
    bool result = XPicture_save(self, value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

bool XPicture_save_device(const XPicture* self, XIODevice* device)
{
    int64_t written;
    uint32_t size;
    if (!self || !self->m_data || !device) return false;
    if (XPicture_magicMatches((const uint8_t*)self->m_data->m_data,
                              self->m_data->m_dataSize) &&
        !XPicture_validateStreamData(self->m_data->m_data,
                                     self->m_data->m_dataSize))
        return false;
    size = self->m_data->m_dataSize;
    if (size == 0) return XIODevice_flush(device);
    written = XIODevice_write_1(device, self->m_data->m_data, size);
    return written == (int64_t)size && XIODevice_flush(device);
}

void XPicture_detach(XPicture* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XPicturePrivate* oldData = self->m_data;
        XPicturePrivate* newData = XPicturePrivate_clone(oldData);
        if (newData)
        {
            self->m_data = newData;
            XPicturePrivate_unref(oldData);
        }
    }
}

bool XPicture_isDetached(const XPicture* self)
{
    return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}
