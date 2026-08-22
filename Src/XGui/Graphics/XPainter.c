/******************************************************************************
 * @file       XPainter.c
 * @brief      XPainter 绘图器类实现（对标 Qt 6.8 QPainter）
 * @author     XinYueC 团队
 * @note       本文件实现两种内置绘制后端：
 *             - 软件光栅后端（XPainter_begin_image 绑定）：所有绘制命令按
 *               当前变换/裁剪/透明度/合成模式逐像素输出到 XImage，采用
 *               Bresenham 画线、像素中心采样与 Alpha 合成，不依赖任何平台
 *               API，可直接用于嵌入式环境；
 *             - 指令录制后端（XPainter_begin_picture 绑定）：绘制命令原样
 *               转录为 XPicture 的可移植指令流（画线/填充矩形/绘制图像/
 *               保存/恢复），保存后可由任意 XPainter 回放。
 *             XPainter 自身是值样式上下文（不持有图像/图片所有权），
 *             save()/restore() 通过内部动态状态栈保存与恢复 XPainterState。
 ******************************************************************************/
#include "XPainter.h"
#include "XMemory.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>

/* ========== 内部常量 ========== */

/** @brief 状态栈初始容量。 */
enum { XPAINTER_STATE_INITIAL_CAPACITY = 8 };

/** @brief 角度转弧度换算系数。 */
#define XPAINTER_DEG_TO_RAD (3.14159265358979323846f / 180.0f)

/* ========== 内部工具函数 ========== */

/**
 * @brief      恢复 XPainterState 的默认值。
 * @param state 待初始化的状态指针。
 */
static void painterDefaultState(XPainterState* state)
{
    memset(state, 0, sizeof(*state));
    state->m_penColor = 0xff000000u;   /* 默认黑色不透明画笔 */
    state->m_penWidth = 1;
    state->m_brushColor = 0xff000000u; /* 默认黑色不透明画刷 */
    state->m_transform.m11 = 1.0f;     /* 默认单位矩阵 */
    state->m_transform.m22 = 1.0f;
    state->m_transform.m33 = 1.0f;
    state->m_opacity = 1.0f;
    state->m_compositionMode = XPainterCompositionMode_SourceOver;
}

/**
 * @brief      判断变换矩阵是否为单位矩阵。
 * @param matrix 变换矩阵指针（非 NULL）。
 * @return 是单位矩阵返回 true。
 */
static bool painterMatrixIsIdentity(const XImageTransform* matrix)
{
    return matrix &&
           fabsf(matrix->m11 - 1.0f) < 1.0e-6f &&
           fabsf(matrix->m12) < 1.0e-6f &&
           fabsf(matrix->m21) < 1.0e-6f &&
           fabsf(matrix->m22 - 1.0f) < 1.0e-6f &&
           fabsf(matrix->dx) < 1.0e-6f &&
           fabsf(matrix->dy) < 1.0e-6f &&
           fabsf(matrix->m13) < 1.0e-6f &&
           fabsf(matrix->m23) < 1.0e-6f &&
           fabsf(matrix->m33 - 1.0f) < 1.0e-6f;
}

/**
 * @brief      用齐次变换矩阵映射一个点。
 * @param matrix 变换矩阵指针。
 * @param x 源 X 坐标。
 * @param y 源 Y 坐标。
 * @param outX 输出 X 坐标。
 * @param outY 输出 Y 坐标。
 * @return 映射成功返回 true；透视分母过小或结果非有限值返回 false。
 */
static bool painterMapPoint(const XImageTransform* matrix, float x, float y,
                            float* outX, float* outY)
{
    float denominator;
    float px;
    float py;
    if (!matrix || !outX || !outY) return false;
    denominator = matrix->m13 * x + matrix->m23 * y + matrix->m33;
    if (fabsf(denominator) < 1.0e-8f) return false;
    px = (matrix->m11 * x + matrix->m21 * y + matrix->dx) / denominator;
    py = (matrix->m12 * x + matrix->m22 * y + matrix->dy) / denominator;
    if (!isfinite(px) || !isfinite(py)) return false;
    *outX = px;
    *outY = py;
    return true;
}

/**
 * @brief      3x3 齐次矩阵求逆。
 * @param m 源矩阵指针。
 * @param out 输出逆矩阵指针。
 * @return 可逆（行列式非零）返回 true。
 * @note       矩阵按 XImageTransform 布局存储：
 *             [ m11 m21 dx ]
 *             [ m12 m22 dy ]
 *             [ m13 m23 m33 ]
 */
static bool painterMatrixInvert(const XImageTransform* m, XImageTransform* out)
{
    float a = m->m11, b = m->m21, c = m->dx;
    float d = m->m12, e = m->m22, f = m->dy;
    float g = m->m13, h = m->m23, i = m->m33;
    float det;
    float inv;
    if (!m || !out) return false;
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1.0e-8f) return false;
    inv = 1.0f / det;
    out->m11 = (e * i - f * h) * inv;
    out->m12 = (f * g - d * i) * inv;
    out->m21 = (c * h - b * i) * inv;
    out->m22 = (a * i - c * g) * inv;
    out->dx  = (b * f - c * e) * inv;
    out->dy  = (c * d - a * f) * inv;
    out->m13 = (d * h - e * g) * inv;
    out->m23 = (b * g - a * h) * inv;
    out->m33 = (a * e - b * d) * inv;
    return true;
}

/**
 * @brief      矩阵级联（result = a * b，b 先生效，符合 Qt 叠加顺序）。
 * @param a 第一个矩阵（已有变换）。
 * @param b 第二个矩阵（新叠加的变换）。
 * @return 级联结果。
 */
static XImageTransform painterMatrixMultiply(const XImageTransform* a,
                                             const XImageTransform* b)
{
    XImageTransform r;
    r.m11 = a->m11 * b->m11 + a->m21 * b->m12 + a->dx * b->m13;
    r.m21 = a->m11 * b->m21 + a->m21 * b->m22 + a->dx * b->m23;
    r.dx  = a->m11 * b->dx  + a->m21 * b->dy  + a->dx * b->m33;
    r.m12 = a->m12 * b->m11 + a->m22 * b->m12 + a->dy * b->m13;
    r.m22 = a->m12 * b->m21 + a->m22 * b->m22 + a->dy * b->m23;
    r.dy  = a->m12 * b->dx  + a->m22 * b->dy  + a->dy * b->m33;
    r.m13 = a->m13 * b->m11 + a->m23 * b->m12 + a->m33 * b->m13;
    r.m23 = a->m13 * b->m21 + a->m23 * b->m22 + a->m33 * b->m23;
    r.m33 = a->m13 * b->dx  + a->m23 * b->dy  + a->m33 * b->m33;
    return r;
}

/**
 * @brief      构造平移矩阵。
 * @param dx X 方向平移量。
 * @param dy Y 方向平移量。
 * @return 平移矩阵。
 */
static XImageTransform painterTranslation(float dx, float dy)
{
    XImageTransform t;
    memset(&t, 0, sizeof(t));
    t.m11 = 1.0f;
    t.m22 = 1.0f;
    t.m33 = 1.0f;
    t.dx = dx;
    t.dy = dy;
    return t;
}

/**
 * @brief      构造缩放矩阵。
 * @param sx X 方向缩放系数。
 * @param sy Y 方向缩放系数。
 * @return 缩放矩阵。
 */
static XImageTransform painterScale(float sx, float sy)
{
    XImageTransform t;
    memset(&t, 0, sizeof(t));
    t.m11 = sx;
    t.m22 = sy;
    t.m33 = 1.0f;
    return t;
}

/**
 * @brief      构造旋转矩阵（角度制，顺时针为正）。
 * @param degrees 旋转角度。
 * @return 旋转矩阵。
 */
static XImageTransform painterRotation(float degrees)
{
    float radians = degrees * XPAINTER_DEG_TO_RAD;
    float cosine = cosf(radians);
    float sine = sinf(radians);
    XImageTransform t;
    memset(&t, 0, sizeof(t));
    t.m11 = cosine;
    t.m21 = -sine;
    t.m12 = sine;
    t.m22 = cosine;
    t.m33 = 1.0f;
    return t;
}

/**
 * @brief      把浮点数四舍五入为 int（越界夹到 int 极值）。
 * @param value 浮点数值。
 * @return 四舍五入后的整数。
 */
static int painterRound(float value)
{
    float rounded;
    if (!isfinite(value)) return 0;
    rounded = floorf(value + 0.5f);
    if (rounded > (float)INT_MAX) return INT_MAX;
    if (rounded < (float)INT_MIN) return INT_MIN;
    return (int)rounded;
}

/** @brief floor 进位不成式，并夹到 [0, maxVal]。 */
static int painterFloorClamp(float value, int maxVal)
{
    float f = floorf(value);
    if (f < 0.0f) return 0;
    if (maxVal < 0 || f > (float)maxVal) return maxVal < 0 ? 0 : maxVal;
    return (int)f;
}

/** @brief ceil 进位成式，并夹到 [0, maxVal]。 */
static int painterCeilClamp(float value, int maxVal)
{
    float c = ceilf(value);
    if (c < 0.0f) return 0;
    if (maxVal < 0 || c > (float)maxVal) return maxVal < 0 ? 0 : maxVal;
    return (int)c;
}

/** @brief 把 [0,1] 不透明度换算成 0~255 字节值。 */
static uint8_t painterOpacityByte(float opacity)
{
    if (opacity >= 1.0f) return 255;
    if (opacity <= 0.0f) return 0;
    return (uint8_t)(opacity * 255.0f + 0.5f);
}

/**
 * @brief      把指定颜色按 8 位不透明度缩放 Alpha 分量。
 * @param color ARGB32 颜色。
 * @param opacity 0~255 不透明度字节。
 * @return 缩放后的 ARGB32 颜色。
 */
static uint32_t painterApplyOpacityByte(uint32_t color, uint8_t opacity)
{
    unsigned alpha = (color >> 24) & 255u;
    unsigned scaled;
    if (opacity == 255u) return color;
    scaled = (alpha * (unsigned)opacity + 127u) / 255u;
    if (scaled > 255u) scaled = 255u;
    return (color & 0x00ffffffu) | ((uint32_t)scaled << 24);
}

/**
 * @brief      把指定颜色按浮点不透明度缩放 Alpha 分量。
 * @param color ARGB32 颜色。
 * @param opacity 0.0~1.0 不透明度。
 * @return 缩放后的 ARGB32 颜色。
 */
static uint32_t painterApplyOpacity(uint32_t color, float opacity)
{
    return painterApplyOpacityByte(color, painterOpacityByte(opacity));
}

/**
 * @brief      变换矩形四个角点并求设备空间包围盒。
 * @param matrix 变换矩阵指针。
 * @param rect 源矩形指针。
 * @param minX/minY/maxX/maxY 输出包围盒（可空）。
 * @return 全部角点可映射返回 true。
 */
static bool painterMapRectCorners(const XImageTransform* matrix, const XRect* rect,
                                  float* minX, float* minY,
                                  float* maxX, float* maxY)
{
    const int xs[4] = { rect->x, rect->x + rect->width,
                        rect->x, rect->x + rect->width };
    const int ys[4] = { rect->y, rect->y,
                        rect->y + rect->height, rect->y + rect->height };
    bool first = true;
    int i;
    if (!matrix || !rect) return false;
    for (i = 0; i < 4; ++i)
    {
        float fx;
        float fy;
        if (!painterMapPoint(matrix, (float)xs[i], (float)ys[i], &fx, &fy))
            return false;
        if (first)
        {
            if (minX) *minX = fx;
            if (minY) *minY = fy;
            if (maxX) *maxX = fx;
            if (maxY) *maxY = fy;
            first = false;
        }
        else
        {
            if (minX && fx < *minX) *minX = fx;
            if (maxX && fx > *maxX) *maxX = fx;
            if (minY && fy < *minY) *minY = fy;
            if (maxY && fy > *maxY) *maxY = fy;
        }
    }
    return true;
}

/* ========== 状态栈（save/restore） ========== */

/**
 * @brief      压入当前状态到状态栈（内部维护动态数组）。
 * @param self 绘制器指针。
 * @return 压栈成功返回 true；内存不足返回 false。
 */
static bool painterStatePush(XPainter* self)
{
    int newCapacity;
    XPainterState* newStack;
    if (!self) return false;
    if (!self->m_stateStack)
    {
        newStack = (XPainterState*)XMalloc_System(
            sizeof(XPainterState) * (size_t)XPAINTER_STATE_INITIAL_CAPACITY);
        if (!newStack) return false;
        self->m_stateStack = newStack;
        self->m_stateCapacity = XPAINTER_STATE_INITIAL_CAPACITY;
    }
    else if (self->m_stateCount >= self->m_stateCapacity)
    {
        newCapacity = self->m_stateCapacity * 2;
        newStack = (XPainterState*)XRealloc_System(
            self->m_stateStack, sizeof(XPainterState) * (size_t)newCapacity);
        if (!newStack) return false;
        self->m_stateStack = newStack;
        self->m_stateCapacity = newCapacity;
    }
    self->m_stateStack[self->m_stateCount] = self->m_state;
    ++self->m_stateCount;
    return true;
}

/**
 * @brief      从状态栈弹出最近一次保存的状态并恢复。
 * @param self 绘制器指针。
 */
static void painterStatePop(XPainter* self)
{
    if (!self || self->m_stateCount <= 0) return;
    --self->m_stateCount;
    self->m_state = self->m_stateStack[self->m_stateCount];
}

/* ========== 软件光栅后端 ========== */

/**
 * @brief      把单个像素输出到目标图像（含裁剪、边界和 Alpha 合成）。
 * @param self 绘制器指针。
 * @param x 目标 X 坐标（设备坐标）。
 * @param y 目标 Y 坐标（设备坐标）。
 * @param color ARGB32 源颜色（已含整体透明度缩放）。
 */
static void painterRaster_putPixel(XPainter* self, int x, int y, uint32_t color)
{
    XPainterState* state;
    uint32_t dst;
    unsigned sa, sr, sg, sb;
    unsigned da, dr, dg, db;
    unsigned outA;
    unsigned outR, outG, outB;
    if (!self || !self->m_image) return;
    state = &self->m_state;
    if (state->m_hasClip)
    {
        if (x < state->m_clipRect.x || y < state->m_clipRect.y ||
            x >= state->m_clipRect.x + state->m_clipRect.width ||
            y >= state->m_clipRect.y + state->m_clipRect.height)
            return;
    }
    if (!XImage_valid(self->m_image, x, y)) return;
    if (state->m_compositionMode == XPainterCompositionMode_Source)
    {
        XImage_setPixel(self->m_image, x, y, color);
        return;
    }
    /* SourceOver：先读出目标（XImage_pixel 返回非预乘 ARGB），再做
     * Porter-Duff 源覆盖合成，全部用整数运算避免浮点和溢出。 */
    sa = (color >> 24) & 255u;
    sr = (color >> 16) & 255u;
    sg = (color >> 8) & 255u;
    sb = color & 255u;
    dst = XImage_pixel(self->m_image, x, y);
    da = (dst >> 24) & 255u;
    dr = (dst >> 16) & 255u;
    dg = (dst >> 8) & 255u;
    db = dst & 255u;
    outA = sa + ((da * (255u - sa) + 127u) / 255u);
    if (outA == 0u)
    {
        outR = outG = outB = 0u;
    }
    else
    {
        unsigned value;
        value = (sr * sa + ((dr * da * (255u - sa) + 127u) / 255u) +
                 outA / 2u) / outA;
        outR = value > 255u ? 255u : value;
        value = (sg * sa + ((dg * da * (255u - sa) + 127u) / 255u) +
                 outA / 2u) / outA;
        outG = value > 255u ? 255u : value;
        value = (sb * sa + ((db * da * (255u - sa) + 127u) / 255u) +
                 outA / 2u) / outA;
        outB = value > 255u ? 255u : value;
    }
    XImage_setPixel(self->m_image, x, y,
                    (outA << 24) | (outR << 16) | (outG << 8) | outB);
}

/**
 * @brief      Bresenham 逐像素画线。
 * @param self 绘制器指针。
 * @param x0/y0 起点（设备坐标）。
 * @param x1/y1 终点（设备坐标）。
 * @param color ARGB32 颜色。
 */
static void painterRaster_bresenham(XPainter* self, int x0, int y0,
                                    int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;)
    {
        painterRaster_putPixel(self, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

/**
 * @brief      软件光栅画线：变换端点后沿主轴方向偏移绘制粗线。
 */
static bool painterRaster_drawLine(XPainter* self, int x1, int y1,
                                   int x2, int y2)
{
    float fx1, fy1, fx2, fy2;
    int ix1, iy1, ix2, iy2;
    int dx, dy;
    int width, start, end, k;
    uint32_t color;
    if (!self || !self->m_image) return false;
    if (!painterMapPoint(&self->m_state.m_transform,
                         (float)x1, (float)y1, &fx1, &fy1) ||
        !painterMapPoint(&self->m_state.m_transform,
                         (float)x2, (float)y2, &fx2, &fy2))
        return false;
    ix1 = painterRound(fx1);
    iy1 = painterRound(fy1);
    ix2 = painterRound(fx2);
    iy2 = painterRound(fy2);
    color = painterApplyOpacity(self->m_state.m_penColor, self->m_state.m_opacity);
    width = self->m_state.m_penWidth;
    if (width < 1) width = 1;
    dx = ix2 - ix1;
    dy = iy2 - iy1;
    start = -(width / 2);
    end = start + width;
    if (dx == 0 && dy == 0)
    {
        /* 退化点：按画笔宽度画一个实心方块 */
        for (k = start; k < end; ++k)
        {
            int j;
            for (j = start; j < end; ++j)
                painterRaster_putPixel(self, ix1 + j, iy1 + k, color);
        }
        return true;
    }
    if (abs(dx) >= abs(dy))
    {
        /* 主方向为 X：沿 Y 轴方向平行偏移形成粗线 */
        for (k = start; k < end; ++k)
            painterRaster_bresenham(self, ix1, iy1 + k, ix2, iy2 + k, color);
    }
    else
    {
        /* 主方向为 Y：沿 X 轴方向平行偏移形成粗线 */
        for (k = start; k < end; ++k)
            painterRaster_bresenham(self, ix1 + k, iy1, ix2 + k, iy2, color);
    }
    return true;
}

/**
 * @brief      软件光栅填充矩形。
 * @note       恒等变换且不透明/源替换时可走 XImage_fillRect 快速路径；
 *             否则按变换后包围盒逐像素反算用户坐标并做像素中心采样。
 */
static bool painterRaster_fillRect(XPainter* self, const XRect* rect,
                                   uint32_t color)
{
    XPainterState* state;
    XImageTransform inverse;
    uint32_t effective;
    float minX, minY, maxX, maxY;
    int px0, py0, px1, py1;
    if (!self || !self->m_image || !rect) return false;
    state = &self->m_state;
    effective = painterApplyOpacity(color, state->m_opacity);
    if (painterMatrixIsIdentity(&state->m_transform) && !state->m_hasClip)
    {
        if (state->m_compositionMode == XPainterCompositionMode_Source ||
            ((effective >> 24) == 255u))
        {
            XImage_fillRect(self->m_image, rect, effective);
            return true;
        }
    }
    if (!painterMapRectCorners(&state->m_transform, rect,
                               &minX, &minY, &maxX, &maxY))
        return false;
    if (!painterMatrixInvert(&state->m_transform, &inverse))
        return false;
    px0 = painterFloorClamp(minX, XImage_width(self->m_image) - 1);
    py0 = painterFloorClamp(minY, XImage_height(self->m_image) - 1);
    px1 = painterCeilClamp(maxX, XImage_width(self->m_image));
    py1 = painterCeilClamp(maxY, XImage_height(self->m_image));
    {
        int py;
        for (py = py0; py < py1; ++py)
        {
            int px;
            for (px = px0; px < px1; ++px)
            {
                float ux, uy;
                if (!painterMapPoint(&inverse, px + 0.5f, py + 0.5f, &ux, &uy))
                    continue;
                if (ux >= (float)rect->x &&
                    ux < (float)rect->x + rect->width &&
                    uy >= (float)rect->y &&
                    uy < (float)rect->y + rect->height)
                    painterRaster_putPixel(self, px, py, effective);
            }
        }
    }
    return true;
}

/**
 * @brief      软件光栅绘制图像（最近邻采样，支持变换）。
 */
static bool painterRaster_drawImage(XPainter* self, const XImage* image,
                                    int x, int y)
{
    XPainterState* state;
    int width;
    int height;
    uint8_t opacity;
    if (!self || !self->m_image || !image) return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0) return true;
    state = &self->m_state;
    opacity = painterOpacityByte(state->m_opacity);
    if (painterMatrixIsIdentity(&state->m_transform))
    {
        int sy;
        for (sy = 0; sy < height; ++sy)
        {
            int sx;
            for (sx = 0; sx < width; ++sx)
                painterRaster_putPixel(self, x + sx, y + sy,
                    painterApplyOpacityByte(XImage_pixel(image, sx, sy), opacity));
        }
        return true;
    }
    {
        XRect sourceRect;
        XImageTransform inverse;
        float minX, minY, maxX, maxY;
        int px0, py0, px1, py1;
        sourceRect.x = x;
        sourceRect.y = y;
        sourceRect.width = width;
        sourceRect.height = height;
        if (!painterMapRectCorners(&state->m_transform, &sourceRect,
                                   &minX, &minY, &maxX, &maxY))
            return false;
        if (!painterMatrixInvert(&state->m_transform, &inverse))
            return false;
        px0 = painterFloorClamp(minX, XImage_width(self->m_image) - 1);
        py0 = painterFloorClamp(minY, XImage_height(self->m_image) - 1);
        px1 = painterCeilClamp(maxX, XImage_width(self->m_image));
        py1 = painterCeilClamp(maxY, XImage_height(self->m_image));
        {
            int py;
            for (py = py0; py < py1; ++py)
            {
                int px;
                for (px = px0; px < px1; ++px)
                {
                    float ux, uy, sx, sy;
                    if (!painterMapPoint(&inverse, px + 0.5f, py + 0.5f,
                                         &ux, &uy))
                        continue;
                    sx = ux - (float)x;
                    sy = uy - (float)y;
                    if (sx < 0.0f || sy < 0.0f ||
                        sx >= (float)width || sy >= (float)height)
                        continue;
                    painterRaster_putPixel(self, px, py,
                        painterApplyOpacityByte(
                            XImage_pixel(image, (int)sx, (int)sy), opacity));
                }
            }
        }
    }
    return true;
}

/** @brief 软件光栅保存状态。 */
static bool painterRaster_save(XPainter* self)
{
    return painterStatePush(self);
}

/** @brief 软件光栅恢复状态。 */
static bool painterRaster_restore(XPainter* self)
{
    if (!self || self->m_stateCount <= 0) return false;
    painterStatePop(self);
    return true;
}

/* ========== 指令录制后端 ========== */

static bool painterRecord_drawLine(XPainter* self, int x1, int y1,
                                   int x2, int y2)
{
    return self && self->m_picture &&
           XPicture_recordDrawLine(self->m_picture, x1, y1, x2, y2);
}

static bool painterRecord_fillRect(XPainter* self, const XRect* rect,
                                   uint32_t color)
{
    return self && self->m_picture && rect &&
           XPicture_recordFillRect(self->m_picture, rect, color);
}

static bool painterRecord_drawImage(XPainter* self, const XImage* image,
                                    int x, int y)
{
    return self && self->m_picture && image &&
           XPicture_recordDrawImage(self->m_picture, image, x, y);
}

/**
 * @brief      录制保存：先把当前状态压栈，录制成功才保留。
 */
static bool painterRecord_save(XPainter* self)
{
    if (!self || !self->m_picture) return false;
    if (!painterStatePush(self)) return false;
    if (!XPicture_recordSave(self->m_picture))
    {
        painterStatePop(self);
        return false;
    }
    return true;
}

/**
 * @brief      录制恢复：校验栈非空后录制恢复指令，成功才弹栈。
 */
static bool painterRecord_restore(XPainter* self)
{
    if (!self || !self->m_picture) return false;
    if (self->m_stateCount <= 0) return false;
    if (!XPicture_recordRestore(self->m_picture)) return false;
    painterStatePop(self);
    return true;
}

/* ========== XPainter 公开 API ========== */

void XPainter_init(XPainter* self, void* userData)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    self->m_userData = userData;
    painterDefaultState(&self->m_state);
}

void XPainter_deinit(XPainter* self)
{
    if (!self) return;
    XPainter_end(self);
    self->m_userData = NULL;
}

bool XPainter_begin_image(XPainter* self, XImage* image)
{
    if (!self || !image || XImage_isNull(image)) return false;
    XPainter_end(self);
    self->m_image = image;
    self->m_deviceKind = XPainterDevice_Image;
    self->m_drawLine = painterRaster_drawLine;
    self->m_fillRect = painterRaster_fillRect;
    self->m_drawImage = painterRaster_drawImage;
    self->m_save = painterRaster_save;
    self->m_restore = painterRaster_restore;
    return true;
}

bool XPainter_begin_picture(XPainter* self, XPicture* picture)
{
    if (!self || !picture) return false;
    XPainter_end(self);
    self->m_picture = picture;
    self->m_deviceKind = XPainterDevice_Picture;
    self->m_drawLine = painterRecord_drawLine;
    self->m_fillRect = painterRecord_fillRect;
    self->m_drawImage = painterRecord_drawImage;
    self->m_save = painterRecord_save;
    self->m_restore = painterRecord_restore;
    return true;
}

bool XPainter_end(XPainter* self)
{
    if (!self) return false;
    if (self->m_stateStack)
    {
        XFree_System(self->m_stateStack);
        self->m_stateStack = NULL;
    }
    self->m_stateCount = 0;
    self->m_stateCapacity = 0;
    self->m_deviceKind = XPainterDevice_None;
    self->m_image = NULL;
    self->m_picture = NULL;
    self->m_drawLine = NULL;
    self->m_fillRect = NULL;
    self->m_drawImage = NULL;
    self->m_save = NULL;
    self->m_restore = NULL;
    painterDefaultState(&self->m_state);
    return true;
}

bool XPainter_isActive(const XPainter* self)
{
    return self && self->m_deviceKind != XPainterDevice_None;
}

void* XPainter_device(const XPainter* self)
{
    if (!self) return NULL;
    if (self->m_deviceKind == XPainterDevice_Image) return self->m_image;
    if (self->m_deviceKind == XPainterDevice_Picture) return self->m_picture;
    return NULL;
}

bool XPainter_drawLine(XPainter* self, int x1, int y1, int x2, int y2)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !self->m_drawLine)
        return false;
    return self->m_drawLine(self, x1, y1, x2, y2);
}

bool XPainter_drawLine_2(XPainter* self, const XPoint* p1, const XPoint* p2)
{
    if (!p1 || !p2) return false;
    return XPainter_drawLine(self, p1->x, p1->y, p2->x, p2->y);
}

bool XPainter_drawPoint(XPainter* self, int x, int y)
{
    return XPainter_drawLine(self, x, y, x, y);
}

bool XPainter_drawPoint_2(XPainter* self, const XPoint* point)
{
    if (!point) return false;
    return XPainter_drawPoint(self, point->x, point->y);
}

bool XPainter_drawRect(XPainter* self, const XRect* rect)
{
    bool ok;
    if (!self) return false;
    if (!rect || rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_drawLine)
        return false;
    /* 按四条边各画一条线：上、左、右、下。 */
    ok = self->m_drawLine(self, rect->x, rect->y,
                          rect->x + rect->width - 1, rect->y);
    if (!ok) return false;
    ok = self->m_drawLine(self, rect->x, rect->y,
                          rect->x, rect->y + rect->height - 1);
    if (!ok) return false;
    ok = self->m_drawLine(self, rect->x + rect->width - 1, rect->y,
                          rect->x + rect->width - 1, rect->y + rect->height - 1);
    if (!ok) return false;
    ok = self->m_drawLine(self, rect->x, rect->y + rect->height - 1,
                          rect->x + rect->width - 1, rect->y + rect->height - 1);
    return ok;
}

bool XPainter_fillRect(XPainter* self, const XRect* rect, uint32_t color)
{
    if (!self) return false;
    if (!rect) return false;
    if (rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_fillRect)
        return false;
    return self->m_fillRect(self, rect, color);
}

bool XPainter_fillRect_2(XPainter* self, const XRect* rect)
{
    if (!self) return false;
    return XPainter_fillRect(self, rect, self->m_state.m_brushColor);
}

bool XPainter_drawImage(XPainter* self, const XImage* image, int x, int y)
{
    if (!self) return false;
    if (!image) return false;
    if (XImage_isNull(image) || XImage_width(image) <= 0 ||
        XImage_height(image) <= 0)
        return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_drawImage)
        return false;
    return self->m_drawImage(self, image, x, y);
}

bool XPainter_drawImage_2(XPainter* self, const XImage* image, const XPoint* pos)
{
    if (!pos) return false;
    return XPainter_drawImage(self, image, pos->x, pos->y);
}

bool XPainter_save(XPainter* self)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !self->m_save)
        return false;
    return self->m_save(self);
}

bool XPainter_restore(XPainter* self)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !self->m_restore)
        return false;
    return self->m_restore(self);
}

void XPainter_setPen(XPainter* self, uint32_t color)
{
    if (self) self->m_state.m_penColor = color;
}

uint32_t XPainter_penColor(const XPainter* self)
{
    return self ? self->m_state.m_penColor : 0u;
}

void XPainter_setPenWidth(XPainter* self, int width)
{
    if (self) self->m_state.m_penWidth = width < 1 ? 1 : width;
}

int XPainter_penWidth(const XPainter* self)
{
    return self ? self->m_state.m_penWidth : 0;
}

void XPainter_setBrush(XPainter* self, uint32_t color)
{
    if (self) self->m_state.m_brushColor = color;
}

uint32_t XPainter_brushColor(const XPainter* self)
{
    return self ? self->m_state.m_brushColor : 0u;
}

void XPainter_setClipRect(XPainter* self, const XRect* rect)
{
    if (!self) return;
    if (!rect || rect->width <= 0 || rect->height <= 0)
    {
        self->m_state.m_hasClip = false;
        memset(&self->m_state.m_clipRect, 0, sizeof(XRect));
        return;
    }
    self->m_state.m_hasClip = true;
    self->m_state.m_clipRect = *rect;
}

void XPainter_clipRect(const XPainter* self, XRect* out)
{
    if (!out) return;
    if (!self || !self->m_state.m_hasClip)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = self->m_state.m_clipRect;
}

bool XPainter_hasClipping(const XPainter* self)
{
    return self && self->m_state.m_hasClip;
}

void XPainter_setTransform(XPainter* self, const XImageTransform* matrix)
{
    if (!self) return;
    if (!matrix)
    {
        XPainter_resetTransform(self);
        return;
    }
    self->m_state.m_transform = *matrix;
}

void XPainter_transform(const XPainter* self, XImageTransform* out)
{
    if (!out) return;
    if (!self)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = self->m_state.m_transform;
}

void XPainter_resetTransform(XPainter* self)
{
    if (!self) return;
    memset(&self->m_state.m_transform, 0, sizeof(XImageTransform));
    self->m_state.m_transform.m11 = 1.0f;
    self->m_state.m_transform.m22 = 1.0f;
    self->m_state.m_transform.m33 = 1.0f;
}

void XPainter_translate(XPainter* self, float dx, float dy)
{
    XImageTransform t;
    if (!self) return;
    t = painterTranslation(dx, dy);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
}

void XPainter_scale(XPainter* self, float sx, float sy)
{
    XImageTransform t;
    if (!self) return;
    t = painterScale(sx, sy);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
}

void XPainter_rotate(XPainter* self, float degrees)
{
    XImageTransform t;
    if (!self) return;
    t = painterRotation(degrees);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
}

void XPainter_setOpacity(XPainter* self, float opacity)
{
    if (!self) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    self->m_state.m_opacity = opacity;
}

float XPainter_opacity(const XPainter* self)
{
    return self ? self->m_state.m_opacity : 0.0f;
}

void XPainter_setCompositionMode(XPainter* self, XPainterCompositionMode mode)
{
    if (self) self->m_state.m_compositionMode = mode;
}

XPainterCompositionMode XPainter_compositionMode(const XPainter* self)
{
    return self ? self->m_state.m_compositionMode
                : XPainterCompositionMode_SourceOver;
}
