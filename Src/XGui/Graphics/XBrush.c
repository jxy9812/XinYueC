/******************************************************************************
 * @file       XBrush.c
 * @brief      画刷值类型实现（对标 Qt 6.8 QBrush）。
 * @details    采用进程级驻留池：相同描述只存一份，XBrush 仅持 8 字节只读
 *             句柄。这样 XPalette / XStyleOption 等按值调用方无需引用计数
 *             与析构钩子，语义等价 QBrush 的隐式共享（代价是池条目常驻，
 *             上限 XBRUSH_POOL_MAX；超出后回落到最后一条，避免运行期增长）。
 *             Qt 侧对应实现：QBrushData 引用计数 + QPalettePrivate::Data
 *             的 QBrush 数组（qbrush.h:112、qpalette_p.h）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBrush.h"

#if XGUI_ON

#include <string.h>
#include "XMemory.h"
#include "XPainter.h"
#include "XGradient.h"
#include "XPixmap.h"

/** @brief 驻留池分块容量（按需增长，不再有硬上限）。 */
#define XBRUSH_CHUNK_SIZE 64
#define XBRUSH_MAX_CHUNKS 64

/** @brief 画刷共享数据块（只读；池拥有）。 */
typedef struct XBrushData
{
    uint8_t m_style;              /**< XBrushStyle。 */
    uint8_t m_reserved[3];        /**< 对齐填充。 */
    uint32_t m_argb;              /**< 颜色 ARGB32 快照（比较/不透明判定用）。 */
    XColor m_color;               /**< 原始颜色（保留色域/精度）。 */
    const XGradient* m_gradient;  /**< 渐变（借用；NULL 表示无）。 */
    const XPixmap* m_texture;     /**< 纹理（借用；NULL 表示无）。 */
    const XImage* m_textureImage; /**< 纹理图像（借用；NULL 表示无）。 */
    XBrushTransform m_transform;  /**< 变换（默认单位阵）。 */
} XBrushData;

static XBrushData* g_brushChunks[XBRUSH_MAX_CHUNKS];
static int g_brushChunkCount = 0;
static int g_brushPoolCount = 0;

/** @brief 渐变拷贝池（块式增长；池拥有，调用方可释放源对象）。 */
#define XBRUSH_GRAD_CHUNK 16
#define XBRUSH_MAX_GRAD_CHUNKS 32
static XGradient* g_gradChunks[XBRUSH_MAX_GRAD_CHUNKS];
static int g_gradChunkCount = 0;
static int g_gradCount = 0;

static XGradient* xbrush_grad_slot(int index)
{
    int ci = index / XBRUSH_GRAD_CHUNK;
    int off = index % XBRUSH_GRAD_CHUNK;
    if (ci >= XBRUSH_MAX_GRAD_CHUNKS) return NULL;
    if (!g_gradChunks[ci]) {
        XGradient* chunk = (XGradient*)XMalloc_System(sizeof(XGradient) * XBRUSH_GRAD_CHUNK);
        if (!chunk) return NULL;
        memset(chunk, 0, sizeof(XGradient) * XBRUSH_GRAD_CHUNK);
        g_gradChunks[ci] = chunk;
        g_gradChunkCount = ci + 1;
    }
    return &g_gradChunks[ci][off];
}

/** @brief 把纹理按类拷贝归池（XPixmap 内部 m_data 引用计数，拷贝即 COW）。 */
static const XPixmap* xbrush_own_texture(const XPixmap* src)
{
    XPixmap* copy;
    if (!src) return NULL;
    copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XClass_copy_base((XClass*)copy, (const XClass*)src);
    return copy;
}

/** @brief 把渐变按值拷入池（去重）；返回池内只读副本。 */
static const XGradient* xbrush_own_gradient(const XGradient* src)
{
    int i;
    if (!src) return NULL;
    for (i = 0; i < g_gradCount; ++i) {
        const XGradient* it = xbrush_grad_slot(i);
        if (it && memcmp(it, src, sizeof(XGradient)) == 0) return it;
    }
    {
        XGradient* copy = xbrush_grad_slot(g_gradCount);
        if (!copy) return NULL;
        memcpy(copy, src, sizeof(XGradient));
        ++g_gradCount;
        return copy;
    }
}

/** @brief 取第 index 条驻留数据（按需分配块）。 */
static XBrushData* xbrush_slot(int index)
{
    int ci = index / XBRUSH_CHUNK_SIZE;
    int off = index % XBRUSH_CHUNK_SIZE;
    if (ci >= XBRUSH_MAX_CHUNKS) return NULL;
    if (!g_brushChunks[ci]) {
        XBrushData* chunk = (XBrushData*)XMalloc_System(sizeof(XBrushData) * XBRUSH_CHUNK_SIZE);
        if (!chunk) return NULL;
        memset(chunk, 0, sizeof(XBrushData) * XBRUSH_CHUNK_SIZE);
        g_brushChunks[ci] = chunk;
        g_brushChunkCount = ci + 1;
    }
    return &g_brushChunks[ci][off];
}

/** @brief 校验句柄是否落在驻留池有效区（未初始化/脏句柄一律回 NoBrush）。 */
static const XBrushData* xbrush_valid(const XBrushData* d)
{
    int i;
    if (!d) return NULL;
    for (i = 0; i < g_brushChunkCount; ++i) {
        if (d >= g_brushChunks[i] && d < g_brushChunks[i] + XBRUSH_CHUNK_SIZE)
            return d;
    }
    return NULL;
}

static const XBrushTransform k_identityTransform = { 1.0f, 0.0f, 0.0f, 1.0f,
                                                     0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

static bool xbrush_transform_equal(const XBrushTransform* a,
                                   const XBrushTransform* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static const XBrushData* xbrush_intern(const XBrushData* src)
{
    int i;
    for (i = 0; i < g_brushPoolCount; ++i) {
        const XBrushData* it = xbrush_slot(i);
        if (!it) break;
        if (it->m_style == src->m_style && it->m_argb == src->m_argb &&
            it->m_gradient == src->m_gradient && it->m_texture == src->m_texture &&
            it->m_textureImage == src->m_textureImage &&
            xbrush_transform_equal(&it->m_transform, &src->m_transform))
            return it;
    }
    {
        XBrushData* copy = xbrush_slot(g_brushPoolCount);
        if (!copy) return g_brushPoolCount > 0 ? xbrush_slot(g_brushPoolCount - 1) : NULL;
        memcpy(copy, src, sizeof(XBrushData));
        copy->m_reserved[0] = copy->m_reserved[1] = copy->m_reserved[2] = 0;
        ++g_brushPoolCount;
        return copy;
    }
}

static XBrushData xbrush_current(const XBrush* self)
{
    XBrushData d;
    const XBrushData* valid = self ? xbrush_valid(self->m_data) : NULL;
    memset(&d, 0, sizeof(d));
    if (valid) {
        memcpy(&d, valid, sizeof(d));
    } else {
        d.m_style = (uint8_t)XBrushStyle_NoBrush;
        d.m_color = XColor_create_argb(0xFF000000u);
        d.m_argb = 0xFF000000u;
        d.m_transform = k_identityTransform;
    }
    return d;
}

XBrush XBrush_create(void)
{
    XBrush b;
    b.m_data = NULL;
    return b;
}

XBrush XBrush_create_style(XBrushStyle style)
{
    XBrush b = XBrush_create();
    XBrush_setStyle(&b, style);
    return b;
}

XBrush XBrush_create_color(XColor color, XBrushStyle style)
{
    XBrush b = XBrush_create();
    XBrush_setColor(&b, color);
    XBrush_setStyle(&b, style);
    return b;
}

void XBrush_init(XBrush* self)
{
    if (!self) return;
    self->m_data = NULL;
}

void XBrush_deinit(XBrush* self)
{
    /* 常驻池：无所有权，无需释放；保留入口以对称值类型生命周期。 */
    if (!self) return;
    self->m_data = NULL;
}

void XBrush_copy(XBrush* dest, const XBrush* src)
{
    if (!dest) return;
    dest->m_data = src ? src->m_data : NULL;
}

void XBrush_swap(XBrush* a, XBrush* b)
{
    const struct XBrushData* tmp;
    if (!a || !b) return;
    tmp = a->m_data;
    a->m_data = b->m_data;
    b->m_data = tmp;
}

XBrushStyle XBrush_style(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    if (!d) return XBrushStyle_NoBrush;
    return (XBrushStyle)d->m_style;
}

void XBrush_setStyle(XBrush* self, XBrushStyle style)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_style = (uint8_t)style;
    self->m_data = xbrush_intern(&d);
}

XColor XBrush_color(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    if (!d) {
        return XColor_create_argb(0xFF000000u);
    }
    return d->m_color;
}

void XBrush_setColor(XBrush* self, XColor color)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_color = color;
    d.m_argb = XColor_rgba(&color);
    self->m_data = xbrush_intern(&d);
}

const XGradient* XBrush_gradient(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    return d ? d->m_gradient : NULL;
}

void XBrush_setGradient(XBrush* self, const XGradient* gradient)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_gradient = xbrush_own_gradient(gradient);
    if (gradient)
        d.m_style = (uint8_t)XBrushStyle_LinearGradientPattern;
    self->m_data = xbrush_intern(&d);
}

const XPixmap* XBrush_texture(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    return d ? d->m_texture : NULL;
}

void XBrush_setTexture(XBrush* self, const XPixmap* texture)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_texture = xbrush_own_texture(texture);
    d.m_textureImage = NULL;
    if (texture)
        d.m_style = (uint8_t)XBrushStyle_TexturePattern;
    self->m_data = xbrush_intern(&d);
}

const XImage* XBrush_textureImage(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    return d ? d->m_textureImage : NULL;
}

void XBrush_setTextureImage(XBrush* self, const XImage* image)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_textureImage = image;
    d.m_texture = NULL;
    if (image)
        d.m_style = (uint8_t)XBrushStyle_TexturePattern;
    self->m_data = xbrush_intern(&d);
}

void XBrush_detach(XBrush* self)
{
    (void)self; /* 常驻池：数据本就只读共享，无需分离。 */
}

XBrushTransform XBrush_transform(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    if (!d) return k_identityTransform;
    return d->m_transform;
}

void XBrush_setTransform(XBrush* self, const XBrushTransform* transform)
{
    XBrushData d;
    if (!self) return;
    d = xbrush_current(self);
    d.m_transform = transform ? *transform : k_identityTransform;
    self->m_data = xbrush_intern(&d);
}

bool XBrush_isOpaque(const XBrush* self)
{
    const XBrushData* d = self ? xbrush_valid(self->m_data) : NULL;
    if (!d) return false;
    if (d->m_texture || d->m_textureImage) return false;
    if (d->m_style != (uint8_t)XBrushStyle_SolidPattern) return false;
    return ((d->m_argb >> 24) & 0xFFu) == 0xFFu;
}

bool XBrush_isEqual(const XBrush* a, const XBrush* b)
{
    const XBrushData* da = a ? xbrush_valid(a->m_data) : NULL;
    const XBrushData* db = b ? xbrush_valid(b->m_data) : NULL;
    if (a == b) return true;
    if (!da || !db) return da == db;
    return da->m_style == db->m_style &&
           da->m_argb == db->m_argb &&
           da->m_gradient == db->m_gradient &&
           da->m_texture == db->m_texture &&
           da->m_textureImage == db->m_textureImage &&
           xbrush_transform_equal(&da->m_transform, &db->m_transform);
}

bool XBrush_isDetached(const XBrush* self)
{
    (void)self;
    return false; /* 常驻池：永不独占，对齐 isDetached 的只读语义。 */
}

void XBrush_applyToPainter(const XBrush* self, XPainter* painter, const XRect* rect)
{
    XBrushStyle style;
    const XGradient* grad;
    XRect r;
    if (!painter) return;
    style = XBrush_style(self);
    grad = XBrush_gradient(self);
    if (grad && style != XBrushStyle_NoBrush && style != XBrushStyle_SolidPattern) {
        XPainterGradient pg;
        int i, n;
        memset(&pg, 0, sizeof(pg));
        pg.m_type = (XPainterGradientType)XGradient_type(grad);
        if (rect) { r = *rect; } else { r.x = 0; r.y = 0; r.width = 1; r.height = 1; }
        if (pg.m_type == XPainterGradientType_Linear) {
            pg.m_startX = (float)r.x;
            pg.m_startY = (float)r.y;
            pg.m_endX = (float)r.x;
            pg.m_endY = (float)(r.y + r.height);
        } else {
            pg.m_centerX = (float)(r.x + r.width / 2);
            pg.m_centerY = (float)(r.y + r.height / 2);
            pg.m_focalX = pg.m_centerX;
            pg.m_focalY = pg.m_centerY;
            pg.m_radius = (float)((r.width < r.height ? r.width : r.height) / 2);
            pg.m_angleDeg = 0.0f;
        }
        n = XGradient_stopCount(grad);
        if (n > XPAINTER_GRADIENT_MAX_STOPS) n = XPAINTER_GRADIENT_MAX_STOPS;
        for (i = 0; i < n; ++i) {
            XGradientStop s;
            if (!XGradient_stopAt(grad, i, &s)) break;
            pg.m_stops[i].m_position = (float)s.m_position;
            pg.m_stops[i].m_color = s.m_color;
            pg.m_stopCount = i + 1;
        }
        XPainter_setBrushGradient(painter, &pg);
        XPainter_setBrushStyle(painter, (XPainterBrushStyle)style);
        return;
    }
    {
        XColor color = XBrush_color(self);
        XPainter_setBrush(painter, XColor_rgba(&color));
        XPainter_setBrushStyle(painter, (XPainterBrushStyle)style);
    }
}

#endif /* XGUI_ON */
