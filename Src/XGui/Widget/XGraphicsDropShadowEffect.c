/******************************************************************************
 * @file       XGraphicsDropShadowEffect.c
 * @brief      投影效果实现（对标 Qt 6.8 QGraphicsDropShadowEffect，指数模糊）。
 * @details    Draw 实现：按源快照 alpha 形状生成**未着色**投影层（含
 *             offset 位移），经 XGraphicsEffect_blurImage 的 alphaOnly 模式
 *             做指数模糊（半径 = blurRadius 原值，不乘 2.5——Qt 投影
 *             口径），再用投影颜色着色（Qt 的 SourceIn 步），最后按
 *             "先投影后源"次序绘入输出画布。blurRadius 负值照存：滤波半径
 *             按 0 处理（不模糊），包围盒随 Qt 公式收缩、投影被裁边。
 *             boundingRectFor 覆盖为 Qt 原文公式
 *             `rect ∪ (rect.translated(offset).adjusted(-r,-r,r,r))`。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGraphicsDropShadowEffect.h"

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"
#include "XVarList.h"

#include <math.h>

#if XWIDGET_ON

/* ==================== 内部辅助 ==================== */

/** @brief 偏移取整（四舍五入，与像素对齐）。 */
static int xgraphicsdropshadow_roundOffset(float value)
{
    return (int)floorf(value + 0.5f);
}

/** @brief 发射携带 XPointF 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsdropshadow_emitPointF(XGraphicsDropShadowEffect* self,
                                           size_t signal, XPointF value)
{
    XVarList* args = XVarList_Create(XVar(XPointF, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射携带 float 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsdropshadow_emitFloat(XGraphicsDropShadowEffect* self,
                                          size_t signal, float value)
{
    XVarList* args = XVarList_Create(XVar(float, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 发射携带 XColor 参数的信号（无连接时释放参数，防泄漏）。 */
static void xgraphicsdropshadow_emitColor(XGraphicsDropShadowEffect* self,
                                          size_t signal, XColor value)
{
    XVarList* args = XVarList_Create(XVar(XColor, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 浮点矩形并集（就地写 out）。 */
static XRectF xgraphicsdropshadow_unionRect(const XRectF* a, const XRectF* b)
{
    XRectF out;
    float ax2;
    float ay2;
    float bx2;
    float by2;
    ax2 = a->x + a->width;
    ay2 = a->y + a->height;
    bx2 = b->x + b->width;
    by2 = b->y + b->height;
    out.x = a->x < b->x ? a->x : b->x;
    out.y = a->y < b->y ? a->y : b->y;
    out.width = (ax2 > bx2 ? ax2 : bx2) - out.x;
    out.height = (ay2 > by2 ? ay2 : by2) - out.y;
    return out;
}

/** @brief BoundingRectFor 槽位实现：源矩形与「外扩后平移」矩形的并集。 */
static XRectF VXGraphicsDropShadowEffect_boundingRectFor(
    const XGraphicsEffect* base, const XRectF* sourceRect)
{
    XGraphicsDropShadowEffect* self = (XGraphicsDropShadowEffect*)base;
    XRectF grown;
    XRectF shifted;
    if (!sourceRect) {
        grown.x = 0.0f;
        grown.y = 0.0f;
        grown.width = 0.0f;
        grown.height = 0.0f;
        return grown;
    }
    if (!self) return *sourceRect;
    /* 对标 QGraphicsDropShadowEffect::boundingRectFor → QPixmapDropShadowFilter
     * （qpixmapfilter.cpp:965-970 原文）：
     *   return rect.united(rect.translated(offset)
     *                          .adjusted(-radius, -radius, radius, radius));
     * 即「源矩形」与「源矩形按半径外扩后平移到投影位置」的并集，
     * 不是"四周对称外扩"——正偏移只把右侧/下侧撑大，负偏移只撑左侧/上侧。
     * 本机 Qt 6.8 采样（源 (10,20,100,50)）：
     *   r=1 off=(8,8)   → (10,20,109,59)     右侧/下侧 +9，左上不动
     *   r=1 off=(-3.5,12)→ (5.5,20,104.5,63) 左侧 -4.5、下侧 +13
     *   r=4 off=(-3.5,12)→ (2.5,20,108,66)
     *   r=0 off=(0,0)   → 原样返回
     *   r=-2 off=(8,8)  → (10,20,106,56)     外扩 -2（收缩）
     * 外扩量用 blurRadius 本身（不是模糊效果的 1+2.5r），负值照算。 */
    grown = *sourceRect;
    grown.x -= self->m_blurRadius;
    grown.y -= self->m_blurRadius;
    grown.width += 2.0f * self->m_blurRadius;
    grown.height += 2.0f * self->m_blurRadius;
    shifted = grown;
    shifted.x += self->m_offset.x;
    shifted.y += self->m_offset.y;
    return xgraphicsdropshadow_unionRect(sourceRect, &shifted);
}

/** @brief 生成与源 alpha 形状一致的**未着色**投影层（画布尺寸，含偏移）。
 * @details 只写 alpha 通道（rgb 置 0），对标 Qt QPixmapDropShadowFilter::draw
 *          第一步 `tmpPainter.drawPixmap(d->offset, px)`——投影形状即源
 *          alpha，随后 `qt_blurImage(..., alphaOnly=true)` 只模糊 alpha。 */
static XImage* xgraphicsdropshadow_makeShadow(
    XGraphicsDropShadowEffect* self, const XGraphicsEffectDrawContext* ctx)
{
    XImage* shadow;
    int srcW;
    int srcH;
    int stridePixels;
    uint32_t* srcBits;
    uint32_t* dstBits;
    int dstStride;
    int offX;
    int offY;
    int x;
    int y;
    shadow = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!shadow) return NULL;
    if (!XImage_reinit_ex(shadow, ctx->m_destRect.width,
                          ctx->m_destRect.height,
                          XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(shadow);
        return NULL;
    }
    XImage_fillRect(shadow, NULL, 0u);
    srcW = XImage_width(ctx->m_source);
    srcH = XImage_height(ctx->m_source);
    stridePixels = XImage_bytesPerLine(ctx->m_source) / 4;
    srcBits = (uint32_t*)(void*)XImage_bits(ctx->m_source);
    dstBits = (uint32_t*)(void*)XImage_bits(shadow);
    if (!srcBits || !dstBits || stridePixels <= 0) {
        XClassDelete(shadow);
        return NULL;
    }
    dstStride = XImage_bytesPerLine(shadow) / 4;
    if (dstStride < ctx->m_destRect.width) {
        XClassDelete(shadow);
        return NULL;
    }
    /* 画布像素 (x,y) 对应源像素 (x + destRect.x - offX, y + destRect.y - offY)；
     * 预乘格式下 alpha 即源 alpha、rgb 留 0（着色在模糊之后做）。 */
    offX = xgraphicsdropshadow_roundOffset(self->m_offset.x);
    offY = xgraphicsdropshadow_roundOffset(self->m_offset.y);
    for (y = 0; y < ctx->m_destRect.height; ++y) {
        uint32_t* outRow = dstBits + (size_t)y * (size_t)dstStride;
        int sy = y + ctx->m_destRect.y - offY;
        if (sy < 0 || sy >= srcH) continue;
        for (x = 0; x < ctx->m_destRect.width; ++x) {
            int sx = x + ctx->m_destRect.x - offX;
            uint32_t alpha;
            if (sx < 0 || sx >= srcW) continue;
            alpha = (srcBits[(size_t)sy * (size_t)stridePixels + sx] >> 24) &
                    0xFFu;
            if (alpha == 0u) continue;
            outRow[x] = alpha << 24;
        }
    }
    return shadow;
}

/** @brief 用投影颜色着色（对标 Qt QPixmapDropShadowFilter::draw 的
 *         `setCompositionMode(SourceIn); fillRect(tmp.rect(), d->color)`）。
 * @details 预乘结果 = color 的直色 × (blurredAlpha × colorAlpha)：
 *          alpha 通道同样乘 colorAlpha；rgb 不被源颜色污染（Qt 用 SourceIn
 *          把整张图换成纯色，只保留 alpha 形状）。
 */
static void xgraphicsdropshadow_colorize(XGraphicsDropShadowEffect* self,
                                         XImage* shadow)
{
    uint32_t* bits;
    int stridePixels;
    int width;
    int height;
    float alphaScale;
    float red;
    float green;
    float blue;
    int x;
    int y;
    if (!self || !shadow || XImage_isNull(shadow)) return;
    bits = (uint32_t*)(void*)XImage_bits(shadow);
    if (!bits) return;
    stridePixels = XImage_bytesPerLine(shadow) / 4;
    width = XImage_width(shadow);
    height = XImage_height(shadow);
    alphaScale = (float)XColor_alpha(&self->m_color) / 255.0f;
    red = XColor_redF(&self->m_color);
    green = XColor_greenF(&self->m_color);
    blue = XColor_blueF(&self->m_color);
    for (y = 0; y < height; ++y) {
        uint32_t* row = bits + (size_t)y * (size_t)stridePixels;
        for (x = 0; x < width; ++x) {
            uint32_t alpha = (row[x] >> 24) & 0xFFu;
            float outA;
            uint32_t out;
            if (alpha == 0u) continue;
            outA = (float)alpha * alphaScale;
            out = (uint32_t)(outA + 0.5f) << 24;
            out |= (uint32_t)(red * outA + 0.5f) << 16;
            out |= (uint32_t)(green * outA + 0.5f) << 8;
            out |= (uint32_t)(blue * outA + 0.5f);
            row[x] = out;
        }
    }
}

/** @brief Draw 槽位实现：模糊投影按偏移绘入画布，再叠绘源快照。 */
static void VXGraphicsDropShadowEffect_draw(XGraphicsEffect* base,
                                            XGraphicsEffectDrawContext* ctx)
{
    XGraphicsDropShadowEffect* self = (XGraphicsDropShadowEffect*)base;
    XImage* shadow;
    XPainter painter;
    float blurRadius;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* [零投影短路 2026-10-07] 对标 Qt qgraphicseffect.cpp:1017：
     * `if (blurRadius <= 0 && offset().isNull()) { drawSource; return; }`。 */
    if (self->m_blurRadius <= 0.0f &&
        self->m_offset.x == 0.0f && self->m_offset.y == 0.0f) {
        XPainter_init(&painter, NULL);
        if (XPainter_begin_image(&painter, ctx->m_dest)) {
            XGraphicsEffect_drawSource(base, &painter);
            XPainter_end(&painter);
        }
        XPainter_deinit(&painter);
        return;
    }
    /* 1. 未着色投影层（源 alpha 形状 + 偏移，画布坐标对齐）。 */
    shadow = xgraphicsdropshadow_makeShadow(self, ctx);
    if (!shadow) return;
    /* 2. 指数模糊只滤 alpha（对标 Qt qt_blurImage(..., alphaOnly=true)），
          半径 = blurRadius 原值（投影不乘 2.5）。负半径按 0 处理：Qt 的
          定点式在负 alpha 下可观测结果就是"不模糊 + 包围盒裁边"（本机
          实测 r=-1/-2/-4 投影内部恒为实色硬边），钳 0 避免发散且结果一致。 */
    blurRadius = self->m_blurRadius > 0.0f ? self->m_blurRadius : 0.0f;
    XGraphicsEffect_blurImage(shadow, blurRadius, false, true);
    /* 3. 着色（SourceIn 语义：保留 alpha 形状、整张换成投影色）。 */
    xgraphicsdropshadow_colorize(self, shadow);
    /* 4. 投影先画、源后叠（对标 Qt draw 的绘制次序）；投影层已含偏移，
          与画布原点对齐，源按控件局部坐标对齐覆盖。投影层经 Source 合成
          写入：此刻画布为全透明空画布，Source 与 SourceOver 结果语义等
          价，但预乘 SourceOver 紧致混合循环对透明目标会把写回 alpha 恒
          置 0xFF（像素探针实测：180/255 半透明投影被渲染成不透明深灰
          硬块）；Source 路径按行直拷预乘值，保住投影的半透明 alpha。 */
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, ctx->m_dest)) {
        XPainter_setCompositionMode(&painter,
                                    XPainterCompositionMode_Source);
        XPainter_drawImage(&painter, shadow, 0, 0);
        XPainter_setCompositionMode(&painter,
                                    XPainterCompositionMode_SourceOver);
        XGraphicsEffect_drawSource(base, &painter);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XClassDelete(shadow);
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XGraphicsDropShadowEffect_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsDropShadowEffect)
    XVTABLE_INHERIT_XCLASS(XGraphicsEffect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_BoundingRectFor,
                             VXGraphicsDropShadowEffect_boundingRectFor);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_Draw,
                             VXGraphicsDropShadowEffect_draw);
    return XVTABLE_DEFAULT;
}

void XGraphicsDropShadowEffect_init(XGraphicsDropShadowEffect* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGraphicsEffect_init(&self->m_base);
    XClassSetVtable(self, XGraphicsDropShadowEffect);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt 6.8 默认值：offset=(8,8)、blurRadius=1.0、
       color=QColor(63,63,63,180)（半透明深灰）。 */
    self->m_offset.x = 8.0f;
    self->m_offset.y = 8.0f;
    self->m_blurRadius = 1.0f;
    XColor_init_rgb(&self->m_color, 63, 63, 63, 180);
}

XGraphicsDropShadowEffect* XGraphicsDropShadowEffect_create_ex(
    XMemoryType memory)
{
    XGraphicsDropShadowEffect* self =
        (XGraphicsDropShadowEffect*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGraphicsDropShadowEffect_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

XPointF XGraphicsDropShadowEffect_offset(
    const XGraphicsDropShadowEffect* self)
{
    XPointF offset;
    offset.x = 0.0f;
    offset.y = 0.0f;
    if (self) return self->m_offset;
    return offset;
}

void XGraphicsDropShadowEffect_setOffset(XGraphicsDropShadowEffect* self,
                                         XPointF offset)
{
    if (!self) return;
    if (self->m_offset.x == offset.x && self->m_offset.y == offset.y) return;
    self->m_offset = offset;
    /* 对标 Qt：offset 实际变化即触发源重绘并发射 offsetChanged(offset)
     * （qgraphicseffect.cpp:896-905，updateBoundingRect + emit；偏移同时
     * 影响效果包围盒，updateBoundingRect 的并集口径一并覆盖收缩情形）。 */
    XGraphicsEffect_updateBoundingRect(&self->m_base);
    xgraphicsdropshadow_emitPointF(
        self, (size_t)XGraphicsDropShadowEffect_offsetChanged_signal,
        offset);
}

void XGraphicsDropShadowEffect_setOffset_2(XGraphicsDropShadowEffect* self,
                                           float dx, float dy)
{
    XPointF offset;
    offset.x = dx;
    offset.y = dy;
    XGraphicsDropShadowEffect_setOffset(self, offset);
}

void XGraphicsDropShadowEffect_setOffset_3(XGraphicsDropShadowEffect* self,
                                           float d)
{
    XGraphicsDropShadowEffect_setOffset_2(self, d, d);
}

void XGraphicsDropShadowEffect_setXOffset(XGraphicsDropShadowEffect* self,
                                          float dx)
{
    XPointF offset;
    if (!self) return;
    offset = self->m_offset;
    offset.x = dx;
    XGraphicsDropShadowEffect_setOffset(self, offset);
}

void XGraphicsDropShadowEffect_setYOffset(XGraphicsDropShadowEffect* self,
                                          float dy)
{
    XPointF offset;
    if (!self) return;
    offset = self->m_offset;
    offset.y = dy;
    XGraphicsDropShadowEffect_setOffset(self, offset);
}

float XGraphicsDropShadowEffect_xOffset(
    const XGraphicsDropShadowEffect* self)
{
    return self ? self->m_offset.x : 0.0f;
}

float XGraphicsDropShadowEffect_yOffset(
    const XGraphicsDropShadowEffect* self)
{
    return self ? self->m_offset.y : 0.0f;
}

float XGraphicsDropShadowEffect_blurRadius(
    const XGraphicsDropShadowEffect* self)
{ return self ? self->m_blurRadius : 1.0f; }

void XGraphicsDropShadowEffect_setBlurRadius(
    XGraphicsDropShadowEffect* self, float radius)
{
    if (!self) return;
    /* 不钳制（对标 Qt setBlurRadius 只做早退 + 存值：qgraphicseffect.cpp
       未做下限裁剪，本机 6.8 实测负值照存；渲染侧按 max(0, r) 不模糊）。
       [P0-61 2026-11-11] 早退判据由精确 == 改为 qFuzzyCompare 口径
       （XGraphicsEffect_fuzzyCompare）：Qt qgraphicseffect.cpp:954 用
       `qFuzzyCompare(d->filter->blurRadius(), blurRadius)`。 */
    if (XGraphicsEffect_fuzzyCompare(self->m_blurRadius, radius)) return;
    self->m_blurRadius = radius;
    /* 对标 Qt：setBlurRadius 实际变化即 updateBoundingRect() 并发射
     * blurRadiusChanged(radius)（qgraphicseffect.cpp:951-960）。 */
    XGraphicsEffect_updateBoundingRect(&self->m_base);
    xgraphicsdropshadow_emitFloat(
        self, (size_t)XGraphicsDropShadowEffect_blurRadiusChanged_signal,
        radius);
}

XColor XGraphicsDropShadowEffect_color(
    const XGraphicsDropShadowEffect* self)
{
    XColor color;
    XColor_init(&color);
    if (self) return self->m_color;
    return color;
}

void XGraphicsDropShadowEffect_setColor(XGraphicsDropShadowEffect* self,
                                        XColor color)
{
    if (!self) return;
    if (XColor_rgba(&self->m_color) == XColor_rgba(&color)) return;
    self->m_color = color;
    /* 对标 Qt：setColor 实际变化即触发源重绘并发射 colorChanged(color)
     * （qgraphicseffect.cpp:984-993，update + emit；颜色不改变包围盒）。 */
    XGraphicsEffect_update(&self->m_base);
    xgraphicsdropshadow_emitColor(
        self, (size_t)XGraphicsDropShadowEffect_colorChanged_signal, color);
}

/* ==================== 信号 ==================== */

void* XGraphicsDropShadowEffect_offsetChanged_signal(
    XGraphicsDropShadowEffect* self, XPointF offset)
{
    (void)self; (void)offset;
    return (void*)(size_t)XGraphicsDropShadowEffect_offsetChanged_signal;
}

void* XGraphicsDropShadowEffect_blurRadiusChanged_signal(
    XGraphicsDropShadowEffect* self, float radius)
{
    (void)self; (void)radius;
    return (void*)(size_t)XGraphicsDropShadowEffect_blurRadiusChanged_signal;
}

void* XGraphicsDropShadowEffect_colorChanged_signal(
    XGraphicsDropShadowEffect* self, XColor color)
{
    (void)self; (void)color;
    return (void*)(size_t)XGraphicsDropShadowEffect_colorChanged_signal;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
