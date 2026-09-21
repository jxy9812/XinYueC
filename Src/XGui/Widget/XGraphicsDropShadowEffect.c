/******************************************************************************
 * @file       XGraphicsDropShadowEffect.c
 * @brief      投影效果实现（对标 Qt 6.8 QGraphicsDropShadowEffect，简化）。
 * @details    Draw 实现：按源快照 alpha 形状生成投影着色层（颜色预乘源
 *             alpha），经 XGraphicsEffect_blurBox3 固定盒式模糊后按
 *             offset 偏移绘入输出画布，再把源快照原样叠绘其上（对标 Qt
 *             draw() 的"先画模糊着色投影、再画源"次序）。与 Qt 的差异：
 *             Qt 6 按 blurRadius 用与半径相关的高斯/指数近似核；本实现
 *             为固定 3x3 盒式核，blurRadius 仅作 API 对齐保留。
 *             boundingRectFor 覆盖为源矩形按"模糊扩散 2px + 偏移外沿"
 *             生长。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"

#if XWIDGET_ON

#include "XGraphicsDropShadowEffect.h"
#include "XWidget.h"
#include "XImage.h"
#include "XPainter.h"

#include <math.h>

/** @brief 投影模糊扩散外扩量（固定 3x3 两趟盒式核各扩散 1px）。 */
#define XGRAPHICSDROPSHADOW_PAD 2.0f

/* ==================== 内部辅助 ==================== */

/** @brief 偏移取整（四舍五入，与像素对齐）。 */
static int xgraphicsdropshadow_roundOffset(float value)
{
    return (int)floorf(value + 0.5f);
}

/** @brief BoundingRectFor 槽位实现：按模糊扩散与偏移外沿生长源矩形。 */
static XRectF VXGraphicsDropShadowEffect_boundingRectFor(
    const XGraphicsEffect* base, const XRectF* sourceRect)
{
    XGraphicsDropShadowEffect* self = (XGraphicsDropShadowEffect*)base;
    XRectF out;
    float padL;
    float padT;
    float padR;
    float padB;
    if (!sourceRect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    if (!self) return *sourceRect;
    /* 对标 Qt boundingRectFor 覆盖语义（Qt 按 blurRadius+|offset| 四周
       对称外扩）：包围盒 = 源矩形按"模糊半径外扩 + 偏移外沿"调整。投影
       位于源形状 + offset 处：offset 为正时投影越过源右/下边缘，外沿量
       必须加在 padR/padB 一侧，否则右/下方向投影被画布截断（像素探针
       实测：源贴控件右缘时投影 8px 外沿整段丢失）。 */
    padL = XGRAPHICSDROPSHADOW_PAD;
    padT = XGRAPHICSDROPSHADOW_PAD;
    padR = XGRAPHICSDROPSHADOW_PAD;
    padB = XGRAPHICSDROPSHADOW_PAD;
    if (self->m_offset.x > 0.0f) padR += self->m_offset.x;
    else padL -= self->m_offset.x;
    if (self->m_offset.y > 0.0f) padB += self->m_offset.y;
    else padT -= self->m_offset.y;
    out.x = sourceRect->x - padL;
    out.y = sourceRect->y - padT;
    out.width = sourceRect->width + padL + padR;
    out.height = sourceRect->height + padT + padB;
    return out;
}

/** @brief 生成与源 alpha 形状一致的着色投影层（画布尺寸，含偏移）。 */
static XImage* xgraphicsdropshadow_makeShadow(
    XGraphicsDropShadowEffect* self, const XGraphicsEffectDrawContext* ctx)
{
    XImage* shadow;
    int srcW;
    int srcH;
    int stridePixels;
    uint32_t* srcBits;
    uint32_t* dstBits;
    float alphaScale;
    float red;
    float green;
    float blue;
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
        XImage_delete_base(shadow);
        return NULL;
    }
    XImage_fillRect(shadow, NULL, 0u);
    srcW = XImage_width(ctx->m_source);
    srcH = XImage_height(ctx->m_source);
    stridePixels = XImage_bytesPerLine(ctx->m_source) / 4;
    srcBits = (uint32_t*)(void*)XImage_bits(ctx->m_source);
    dstBits = (uint32_t*)(void*)XImage_bits(shadow);
    if (!srcBits || !dstBits || stridePixels <= 0) {
        XImage_delete_base(shadow);
        return NULL;
    }
    dstStride = XImage_bytesPerLine(shadow) / 4;
    if (dstStride < ctx->m_destRect.width) {
        XImage_delete_base(shadow);
        return NULL;
    }
    /* 投影像素 = 源 alpha 形状 × 预乘投影颜色（对标 Qt 用源 alpha 以
       color 着色生成投影；0xAARRGGBB 本机端序）。画布像素 (x,y) 对应
       源像素 (x + destRect.x - offX, y + destRect.y - offY)。 */
    alphaScale = (float)XColor_alpha(&self->m_color) / 255.0f;
    red = XColor_redF(&self->m_color);
    green = XColor_greenF(&self->m_color);
    blue = XColor_blueF(&self->m_color);
    offX = xgraphicsdropshadow_roundOffset(self->m_offset.x);
    offY = xgraphicsdropshadow_roundOffset(self->m_offset.y);
    for (y = 0; y < ctx->m_destRect.height; ++y) {
        uint32_t* outRow = dstBits + (size_t)y * (size_t)dstStride;
        int sy = y + ctx->m_destRect.y - offY;
        if (sy < 0 || sy >= srcH) continue;
        for (x = 0; x < ctx->m_destRect.width; ++x) {
            int sx = x + ctx->m_destRect.x - offX;
            uint32_t alpha;
            uint32_t out;
            if (sx < 0 || sx >= srcW) continue;
            alpha = (srcBits[(size_t)sy * (size_t)stridePixels + sx] >> 24) &
                    0xFFu;
            if (alpha == 0u) continue;
            out = (uint32_t)((float)alpha * alphaScale + 0.5f) << 24;
            out |= (uint32_t)(red * (float)alpha * alphaScale + 0.5f) << 16;
            out |= (uint32_t)(green * (float)alpha * alphaScale + 0.5f) << 8;
            out |= (uint32_t)(blue * (float)alpha * alphaScale + 0.5f);
            outRow[x] = out;
        }
    }
    return shadow;
}

/** @brief Draw 槽位实现：模糊投影按偏移绘入画布，再叠绘源快照。 */
static void VXGraphicsDropShadowEffect_draw(XGraphicsEffect* base,
                                            XGraphicsEffectDrawContext* ctx)
{
    XGraphicsDropShadowEffect* self = (XGraphicsDropShadowEffect*)base;
    XImage* shadow;
    XPainter painter;
    if (!self || !ctx || !ctx->m_source || !ctx->m_dest) return;
    if (XImage_isNull(ctx->m_dest)) return;
    /* 1. 着色投影层（含偏移，画布坐标对齐）。 */
    shadow = xgraphicsdropshadow_makeShadow(self, ctx);
    if (!shadow) return;
    /* 2. 固定盒式模糊（简化项：Qt 按 blurRadius 高斯/指数近似，本实现
          固定 3x3 两趟盒式核，画布边缘外按透明参与均值）。 */
    XGraphicsEffect_blurBox3(shadow, NULL);
    /* 3. 投影先画、源后叠（对标 Qt draw 的绘制次序）；投影层已含偏移，
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
        XPainter_drawImage(&painter, ctx->m_source,
                           -ctx->m_destRect.x, -ctx->m_destRect.y);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XImage_delete_base(shadow);
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
    /* 对标 Qt：offset 实际变化即触发源重绘。 */
    XGraphicsEffect_update(&self->m_base);
}

float XGraphicsDropShadowEffect_blurRadius(
    const XGraphicsDropShadowEffect* self)
{ return self ? self->m_blurRadius : 1.0f; }

void XGraphicsDropShadowEffect_setBlurRadius(
    XGraphicsDropShadowEffect* self, float radius)
{
    if (!self) return;
    if (radius < 0.0f) radius = 0.0f;
    if (self->m_blurRadius == radius) return;
    self->m_blurRadius = radius;
    /* 对标 Qt：setBlurRadius 实际变化即触发源重绘（半径当前不改变
       固定核尺寸，见文件头"与 Qt 的差异"）。 */
    XGraphicsEffect_update(&self->m_base);
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
    /* 对标 Qt：setColor 实际变化即触发源重绘。 */
    XGraphicsEffect_update(&self->m_base);
}

#endif /* XWIDGET_ON */
