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
#include "XFont8x16.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>

/* ========== 文本绘制工具前向声明（drawTextRect 使用，定义在文件末尾文本段） ========== */
typedef struct PainterBitmapFontTable PainterBitmapFontTable;
static const PainterBitmapFontTable* painterBitmapFont(const XFont* font);
static int painterBitmapScaleForFont(const XFont* font);
static uint32_t painter8x16DecodeNext(const char** p);
static void painter8x16DrawGlyphScaled(XPainter* painter, int x,
                                       int baselineY, uint32_t color,
                                       const unsigned char* glyph, int scale);
static uint32_t painter8x16DecodeNextBounded(const char** p, const char* end);

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
#if XPAINTER_PENSTYLE_ON
    state->m_penStyle = XPainterPenStyle_SolidLine;
    state->m_penCap = XPainterPenCapStyle_FlatCap;
    state->m_penJoin = XPainterPenJoinStyle_MiterJoin;
#endif
    state->m_penColor = 0xff000000u;   /* 默认黑色不透明画笔 */
    state->m_penWidth = 1;
    state->m_brushColor = 0xff000000u; /* 默认黑色不透明画刷 */
#if XPAINTER_BRUSH_ON
    state->m_brush.m_style = XPainterBrushStyle_SolidPattern;
    state->m_brush.m_color = 0xff000000u;
#endif
    state->m_backgroundColor = 0xffffffffu; /* 默认背景不透明白 */
    state->m_transform.m11 = 1.0f;     /* 默认单位矩阵 */
    state->m_transform.m22 = 1.0f;
    state->m_transform.m33 = 1.0f;
#if XPAINTER_WORLD_MATRIX_ON
    state->m_worldMatrixEnabled = false;
#endif /* XPAINTER_WORLD_MATRIX_ON */
#if XPAINTER_VIEW_TRANSFORM_ON
    state->m_viewTransformEnabled = false;
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    state->m_opacity = 1.0f;
    state->m_compositionMode = XPainterCompositionMode_SourceOver;
#if XPAINTER_RENDERHINT_ON
    state->m_renderHints = XPainterRenderHint_TextAntialiasing;
#endif /* XPAINTER_RENDERHINT_ON */
#if XPAINTER_LAYOUT_DIRECTION_ON
    state->m_layoutDirection = XPainterLayoutDirection_Auto;
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
    XFont_init(&state->m_font);
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

/** @brief 单位变换；用于关闭世界或视图变换时的矩阵构造。 */
static const XImageTransform g_painterIdentityTransform = {
    1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
};

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
    float a, b, c, d, e, f, g, h, i;
    float det;
    float inv;
    if (!m || !out) return false;
    a = m->m11; b = m->m21; c = m->dx;
    d = m->m12; e = m->m22; f = m->dy;
    g = m->m13; h = m->m23; i = m->m33;
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

#if XPAINTER_VIEW_TRANSFORM_ON
/**
 * @brief      构造保存状态中的 window/viewport 视图矩阵。
 * @details    本项目矩阵采用列向量映射。故逻辑坐标先经过世界矩阵，再经过
 *             此处返回的视图矩阵，等价于 Qt 的 window/viewport 转换。
 * @param state 绘制状态。
 * @param out 输出视图矩阵。
 * @return 状态有效且窗口尺寸非零时返回 true。
 */
static bool painterViewTransform(const XPainterState* state,
                                 XImageTransform* out)
{
    float scaleX;
    float scaleY;
    if (!state || !out)
        return false;
    *out = g_painterIdentityTransform;
    if (!state->m_viewTransformEnabled)
        return true;
    if (state->m_window.width == 0 || state->m_window.height == 0)
        return false;
    scaleX = (float)state->m_viewport.width / (float)state->m_window.width;
    scaleY = (float)state->m_viewport.height / (float)state->m_window.height;
    if (!isfinite(scaleX) || !isfinite(scaleY))
        return false;
    out->m11 = scaleX;
    out->m22 = scaleY;
    out->dx = (float)state->m_viewport.x -
              (float)state->m_window.x * scaleX;
    out->dy = (float)state->m_viewport.y -
              (float)state->m_window.y * scaleY;
    return isfinite(out->dx) && isfinite(out->dy);
}

/**
 * @brief      恢复当前绘制设备的默认 window/viewport 状态。
 * @param self 绘制器指针，必须已绑定设备。
 * @note       图像设备的默认矩形等于图像边界；图片录制设备没有固定边界，
 *             保持零矩形。这与 QPainter 按 QPaintDevice metric 初始化的方式
 *             对齐。
 */
static void painterResetViewTransform(XPainter* self)
{
    int width = 0;
    int height = 0;
    if (!self)
        return;
    if (self->m_deviceKind == XPainterDevice_Image && self->m_image)
    {
        width = XImage_width(self->m_image);
        height = XImage_height(self->m_image);
    }
    self->m_state.m_window.x = 0;
    self->m_state.m_window.y = 0;
    self->m_state.m_window.width = width;
    self->m_state.m_window.height = height;
    self->m_state.m_viewport = self->m_state.m_window;
    self->m_state.m_viewTransformEnabled = false;
}
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

/**
 * @brief      取得当前实际参与绘制的组合变换。
 * @details    世界矩阵关闭时先替换为单位矩阵；随后在视图变换启用时左乘
 *             window/viewport 矩阵，使逻辑坐标按“世界坐标后视图坐标”的
 *             顺序映射到设备坐标。
 * @param state 绘制状态。
 * @param out 输出组合矩阵。
 * @return 可构造有效矩阵返回 true；退化 window 返回 false。
 */
static bool painterEffectiveTransform(const XPainterState* state,
                                      XImageTransform* out)
{
#if XPAINTER_VIEW_TRANSFORM_ON
    XImageTransform view;
#endif
    if (!state || !out)
        return false;
#if XPAINTER_WORLD_MATRIX_ON
    if (!state->m_worldMatrixEnabled)
        *out = g_painterIdentityTransform;
    else
#endif /* XPAINTER_WORLD_MATRIX_ON */
        *out = state->m_transform;
#if XPAINTER_VIEW_TRANSFORM_ON
    if (!painterViewTransform(state, &view))
        return false;
    *out = painterMatrixMultiply(&view, out);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    return true;
}

/**
 * @brief      取得查询用的世界/视图组合矩阵。
 * @details    对标 QPainter::combinedTransform：保留的世界矩阵始终参与
 *             查询，不受 worldMatrixEnabled 状态影响。
 */
static bool painterCombinedTransform(const XPainterState* state,
                                     XImageTransform* out)
{
#if XPAINTER_VIEW_TRANSFORM_ON
    XImageTransform view;
#endif
    if (!state || !out)
        return false;
    *out = state->m_transform;
#if XPAINTER_VIEW_TRANSFORM_ON
    if (!painterViewTransform(state, &view))
        return false;
    *out = painterMatrixMultiply(&view, out);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    return true;
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
 * @brief      构造切变矩阵。
 * @param sh 水平剪切系数。
 * @param sv 垂直剪切系数。
 * @return 切变矩阵。
 */
static XImageTransform painterShear(float sh, float sv)
{
    XImageTransform t;
    memset(&t, 0, sizeof(t));
    t.m11 = 1.0f;
    t.m21 = sh;
    t.m12 = sv;
    t.m22 = 1.0f;
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

#if XPAINTER_CLIP_ON
/** @brief 向下取整并饱和到 int 范围。 */
static int painterFloorInt(float value)
{
    float result;
    if (!isfinite(value)) return 0;
    result = floorf(value);
    if (result > (float)INT_MAX) return INT_MAX;
    if (result < (float)INT_MIN) return INT_MIN;
    return (int)result;
}

/** @brief 向上取整并饱和到 int 范围。 */
static int painterCeilInt(float value)
{
    float result;
    if (!isfinite(value)) return 0;
    result = ceilf(value);
    if (result > (float)INT_MAX) return INT_MAX;
    if (result < (float)INT_MIN) return INT_MIN;
    return (int)result;
}
#endif /* XPAINTER_CLIP_ON */

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
    {
        XFont fontCopy;
        XPainterState* saved = &self->m_stateStack[self->m_stateCount];
        XFont_copy(&fontCopy, &saved->m_font);
        saved->m_font = fontCopy;
    }
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
    XFont_deinit(&self->m_state.m_font);
    {
        XFont fontCopy;
        XFont_copy(&fontCopy, &self->m_stateStack[self->m_stateCount].m_font);
        self->m_state = self->m_stateStack[self->m_stateCount];
        self->m_state.m_font = fontCopy;
        XFont_deinit(&self->m_stateStack[self->m_stateCount].m_font);
    }
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
#if XPAINTER_CLIP_ON
    if (state->m_hasClip)
    {
        if (state->m_clipRect.width <= 0 || state->m_clipRect.height <= 0 ||
            x < state->m_clipRect.x || y < state->m_clipRect.y ||
            x >= state->m_clipRect.x + state->m_clipRect.width ||
            y >= state->m_clipRect.y + state->m_clipRect.height)
            return;
    }
#endif /* XPAINTER_CLIP_ON */
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
    XImageTransform transform;
    float fx1, fy1, fx2, fy2;
    int ix1, iy1, ix2, iy2;
    int dx, dy;
    int width, start, end, k;
    uint32_t color;
    if (!self || !self->m_image) return false;
    if (!painterEffectiveTransform(&self->m_state, &transform))
        return false;
    if (!painterMapPoint(&transform,
                         (float)x1, (float)y1, &fx1, &fy1) ||
        !painterMapPoint(&transform,
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
    XImageTransform transform;
    XImageTransform inverse;
    uint32_t effective;
    float minX, minY, maxX, maxY;
    int px0, py0, px1, py1;
    if (!self || !self->m_image || !rect) return false;
    state = &self->m_state;
    if (!painterEffectiveTransform(state, &transform))
        return false;
    effective = painterApplyOpacity(color, state->m_opacity);
    if (painterMatrixIsIdentity(&transform)
#if XPAINTER_CLIP_ON
        && !state->m_hasClip
#endif /* XPAINTER_CLIP_ON */
       )
    {
        if (state->m_compositionMode == XPainterCompositionMode_Source ||
            ((effective >> 24) == 255u))
        {
            XImage_fillRect(self->m_image, rect, effective);
            return true;
        }
    }
    if (!painterMapRectCorners(&transform, rect,
                               &minX, &minY, &maxX, &maxY))
        return false;
    if (!painterMatrixInvert(&transform, &inverse))
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
    XImageTransform transform;
    int width;
    int height;
    uint8_t opacity;
    if (!self || !self->m_image || !image) return false;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0) return true;
    state = &self->m_state;
    if (!painterEffectiveTransform(state, &transform))
        return false;
    opacity = painterOpacityByte(state->m_opacity);
    if (painterMatrixIsIdentity(&transform))
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
        if (!painterMapRectCorners(&transform, &sourceRect,
                                   &minX, &minY, &maxX, &maxY))
            return false;
        if (!painterMatrixInvert(&transform, &inverse))
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


/* ========== 画笔样式（虚线/点线） ========== */

void XPainter_setBackground(XPainter* self, uint32_t color)
{
    if (!self) return;
    self->m_state.m_backgroundColor = color;
}

uint32_t XPainter_background(const XPainter* self)
{
    return self ? self->m_state.m_backgroundColor : 0u;
}
#if XPAINTER_PENSTYLE_ON
/**
 * @brief      取某画笔样式的“画/空”二元相位模式。
 * @param style 画笔样式。
 * @param outPattern 输出模式表（画/空交替，段长按像素计）。
 * @param outCount 输出模式段数。
 * @return 实线样式返回 false（无需拆分）；NoPen 或无效样式也返回 false。
 */
static bool painterDashPattern(XPainterPenStyle style,
                               const float** outPattern, int* outCount)
{
    static const float kDash[2]      = { 4.0f, 3.0f };
    static const float kDot[2]       = { 1.0f, 2.0f };
    static const float kDashDot[4]   = { 4.0f, 2.0f, 1.0f, 2.0f };
    static const float kDashDotDot[6]= { 4.0f, 2.0f, 1.0f, 2.0f, 1.0f, 2.0f };
    if (!outPattern || !outCount) return false;
    switch (style)
    {
        case XPainterPenStyle_DashLine:      *outPattern = kDash;      *outCount = 2; return true;
        case XPainterPenStyle_DotLine:       *outPattern = kDot;       *outCount = 2; return true;
        case XPainterPenStyle_DashDotLine:   *outPattern = kDashDot;   *outCount = 4; return true;
        case XPainterPenStyle_DashDotDotLine:*outPattern = kDashDotDot;*outCount = 6; return true;
        default:
            return false;
    }
}

/**
 * @brief      按当前画笔样式绘制一条线段。虚线/点线线段按二元模式拆成
 *             若干实线段后交由当前后端绘制；录制后端因此记录为实线段，
 *             XPicture_play 回放时强制实线，避免二次拆线。
 * @param self 绘制器指针。
 * @param x1/y1/x2/y2 线段端点。
 * @return 绘制成功返回 true。
 */
static bool painterDrawLineStyled(XPainter* self, int x1, int y1,
                                  int x2, int y2)
{
    const float* pat;
    int patCount;
    float dxf, dyf;
    float total;
    float pos;
    int idx;
    if (!self || !self->m_drawLine)
        return false;
    if (self->m_state.m_penStyle == XPainterPenStyle_NoPen)
        return true;
    if (!painterDashPattern(self->m_state.m_penStyle, &pat, &patCount))
        return self->m_drawLine(self, x1, y1, x2, y2);
    dxf = (float)(x2 - x1);
    dyf = (float)(y2 - y1);
    total = sqrtf(dxf * dxf + dyf * dyf);
    if (!(total > 0.0f) || !isfinite(total))
        return self->m_drawLine(self, x1, y1, x2, y2);
    pos = 0.0f;
    idx = 0;
    while (pos < total)
    {
        float segLen = pat[idx];
        bool draw = (idx & 1) == 0;
        float t0, t1;
        if (segLen <= 0.0f) segLen = 1.0f;
        if (pos + segLen > total) segLen = total - pos;
        t0 = pos / total;
        t1 = (pos + segLen) / total;
        if (draw)
        {
            int aX = x1 + painterRound(dxf * t0);
            int aY = y1 + painterRound(dyf * t0);
            int bX = x1 + painterRound(dxf * t1);
            int bY = y1 + painterRound(dyf * t1);
            self->m_drawLine(self, aX, aY, bX, bY);
        }
        pos += segLen;
        idx = (idx + 1) % patCount;
    }
    return true;
}
#endif /* XPAINTER_PENSTYLE_ON */


/* ========== 画刷渐变色工具 ========== */

#if XPAINTER_BRUSH_ON
/**
 * @brief      对 ARGB32 颜色做分量线性插值。
 * @param c0 起始颜色。
 * @param c1 结束颜色。
 * @param t  0.0~1.0 插值位置（越界自动钳位）。
 * @return 插值后的 ARGB32 颜色。
 */
static uint32_t painterLerpColor(uint32_t c0, uint32_t c1, float t)
{
    int r0, g0, b0, a0;
    int r1, g1, b1, a1;
    int r, g, b, a;
    if (t <= 0.0f) return c0;
    if (t >= 1.0f) return c1;
    r0 = (c0 >> 16) & 255; g0 = (c0 >> 8) & 255; b0 = c0 & 255; a0 = (c0 >> 24) & 255;
    r1 = (c1 >> 16) & 255; g1 = (c1 >> 8) & 255; b1 = c1 & 255; a1 = (c1 >> 24) & 255;
    r = r0 + (int)((r1 - r0) * t);
    g = g0 + (int)((g1 - g0) * t);
    b = b0 + (int)((b1 - b0) * t);
    a = a0 + (int)((a1 - a0) * t);
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    if (a < 0) a = 0; else if (a > 255) a = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

/**
 * @brief      在用户坐标 x,y 处求渐变色颜色。
 * @param g 渐变色描述。
 * @param x 用户坐标 X。
 * @param y 用户坐标 Y。
 * @return ARGB32 颜色；无停止点时返回纯黑。
 */
static uint32_t painterGradientColor(const XPainterGradient* g, float x, float y)
{
    float t;
    int i;
    if (!g || g->m_stopCount <= 0) return 0xff000000u;
    switch (g->m_type)
    {
        default:
        case XPainterGradientType_Linear:
        {
            float dx = g->m_endX - g->m_startX;
            float dy = g->m_endY - g->m_startY;
            float denom = dx * dx + dy * dy;
            if (denom > 1.0e-9f)
                t = ((x - g->m_startX) * dx + (y - g->m_startY) * dy) / denom;
            else
                t = 0.0f;
            break;
        }
        case XPainterGradientType_Radial:
        {
            float r = g->m_radius > 0.0f ? g->m_radius : 1.0f;
            float dx = x - g->m_centerX;
            float dy = y - g->m_centerY;
            t = sqrtf(dx * dx + dy * dy) / r;
            break;
        }
        case XPainterGradientType_Conical:
        {
            float dx = x - g->m_centerX;
            float dy = y - g->m_centerY;
            float a = atan2f(dy, dx) * 57.29577951308232f; /* 弧度转度 */
            a -= g->m_angleDeg;
            while (a < 0.0f) a += 360.0f;
            while (a >= 360.0f) a -= 360.0f;
            t = a / 360.0f;
            break;
        }
    }
    if (t <= 0.0f) return g->m_stops[0].m_color;
    if (t >= 1.0f) return g->m_stops[g->m_stopCount - 1].m_color;
    for (i = 0; i + 1 < g->m_stopCount; ++i)
    {
        const XPainterGradientStop* a = &g->m_stops[i];
        const XPainterGradientStop* b = &g->m_stops[i + 1];
        if (t <= b->m_position)
        {
            float span = b->m_position - a->m_position;
            float tt = span > 1.0e-6f ? (t - a->m_position) / span : 0.0f;
            return painterLerpColor(a->m_color, b->m_color, tt);
        }
    }
    return g->m_stops[g->m_stopCount - 1].m_color;
}
#endif /* XPAINTER_BRUSH_ON */

/* ========== 形状/多边形填充工具 ========== */

#if XPAINTER_POLYGON_ON || XPAINTER_SHAPE_ON
/** @brief 内部多边形顶点上限（椭圆采样与扫描填充共用）。 */
#define XPAINTER_POLY_MAX_POINTS 128

/**
 * @brief      对多边形做用户坐标扫描线填充（支持实心与渐变色近似）。
 *             - 实心：逐行调用公开 fillRect，编码器/播放器都能正确变换；
 *             - 渐变色：录制/回放路径按每行左端点取色近似（文档注明）。
 * @param self 绘制器指针。
 * @param n    顶点数。
 * @param uxs  顶点 X 数组。
 * @param uys  顶点 Y 数组。
 * @param color 实心填充色（渐变色路径忽略到 GHOST）。
 * @param gradient 是否按渐变色逐点取色（仅对录制路径近似）。
 */
static void painterScanFillUser(XPainter* self, int n,
                                const float* uxs, const float* uys,
                                uint32_t color, bool gradient)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float minY, maxY;
    int y0, y1, py;
    int i;
    if (!self || n < 3) return;
    minY = maxY = uys[0];
    for (i = 1; i < n; ++i)
    {
        if (uys[i] < minY) minY = uys[i];
        if (uys[i] > maxY) maxY = uys[i];
    }
    y0 = (int)ceilf(minY);
    y1 = (int)floorf(maxY);
    for (py = y0; py <= y1; ++py)
    {
        float yc = (float)py + 0.5f;
        int xc = 0;
        int j;
        for (j = 0; j < n; ++j)
        {
            int k = (j + 1) % n;
            float ay = uys[j], by = uys[k];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc))
            {
                float t = (yc - ay) / (by - ay);
                xs[xc] = uxs[j] + t * (uxs[k] - uxs[j]);
                ++xc;
                if (xc == XPAINTER_POLY_MAX_POINTS) break;
            }
        }
        if (xc >= 2)
        {
            /* 简单插入排序 */
            {
                int a, b;
                for (a = 1; a < xc; ++a)
                {
                    float key = xs[a];
                    b = a - 1;
                    while (b >= 0 && xs[b] > key) { xs[b + 1] = xs[b]; --b; }
                    xs[b + 1] = key;
                }
            }
            for (j = 0; j + 1 < xc; j += 2)
            {
                int xl = (int)ceilf(xs[j]);
                int xr = (int)floorf(xs[j + 1]);
                if (xl <= xr)
                {
                    XRect r;
                    uint32_t fc = color;
                    if (gradient)
                    {
#if XPAINTER_BRUSH_ON
                        const XPainterGradient* g = &self->m_state.m_brush.m_gradient;
                        fc = painterGradientColor(
                                g, (float)(xl + xr) * 0.5f, (float)py);
#endif /* XPAINTER_BRUSH_ON */
                    }
                    r.x = xl; r.y = py; r.width = xr - xl + 1; r.height = 1;
                    XPainter_fillRect(self, &r, fc);
                }
            }
        }
    }
}

/**
 * @brief      设备空间扫描填充（软件光栅后端；逐像素支持渐变色）。
 * @param self 绘制器指针（必须绑定图像）。
 * @param n    顶点数。
 * @param uxs  用户坐标顶点 X。
 * @param uys  用户坐标顶点 Y。
 * @param color 实心填充色。
 * @param gradient 是否按渐变色逐点取色。
 */
static void painterScanFillDevice(XPainter* self, int n,
                                  const float* uxs, const float* uys,
                                  uint32_t color, bool gradient)
{
    XImageTransform transform;
    float dtx[XPAINTER_POLY_MAX_POINTS];
    float dty[XPAINTER_POLY_MAX_POINTS];
    float minX, maxX, minY, maxY;
    int px0, px1, py0, py1;
    int py;
    int i;
    XImageTransform inverse;
    bool haveInverse = false;
    if (!self || !self->m_image || n < 3)
        return;
    if (!painterEffectiveTransform(&self->m_state, &transform))
        return;
    for (i = 0; i < n; ++i)
    {
        float a, b;
        if (!painterMapPoint(&transform, uxs[i], uys[i], &a, &b))
            return;
        dtx[i] = a; dty[i] = b;
    }
    minX = maxX = dtx[0];
    minY = maxY = dty[0];
    for (i = 1; i < n; ++i)
    {
        if (dtx[i] < minX) minX = dtx[i];
        if (dtx[i] > maxX) maxX = dtx[i];
        if (dty[i] < minY) minY = dty[i];
        if (dty[i] > maxY) maxY = dty[i];
    }
    px0 = (int)floorf(minX);
    px1 = (int)ceilf(maxX);
    py0 = (int)floorf(minY);
    py1 = (int)ceilf(maxY);
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 >= XImage_width(self->m_image)) px1 = XImage_width(self->m_image) - 1;
    if (py1 >= XImage_height(self->m_image)) py1 = XImage_height(self->m_image) - 1;
    if (gradient)
        haveInverse = painterMatrixInvert(&transform, &inverse);
    for (py = py0; py <= py1; ++py)
    {
        float yc = (float)py + 0.5f;
        float cross[XPAINTER_POLY_MAX_POINTS];
        int xc = 0;
        int j;
        for (j = 0; j < n; ++j)
        {
            int k = (j + 1) % n;
            float ay = dty[j], by = dty[k];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc))
            {
                float t = (yc - ay) / (by - ay);
                cross[xc] = dtx[j] + t * (dtx[k] - dtx[j]);
                ++xc;
                if (xc == XPAINTER_POLY_MAX_POINTS) break;
            }
        }
        if (xc < 2) continue;
        {
            int a, b;
            for (a = 1; a < xc; ++a)
            {
                float key = cross[a];
                b = a - 1;
                while (b >= 0 && cross[b] > key) { cross[b + 1] = cross[b]; --b; }
                cross[b + 1] = key;
            }
        }
        for (j = 0; j + 1 < xc; j += 2)
        {
            int xl = (int)ceilf(cross[j]);
            int xr = (int)floorf(cross[j + 1]);
            int px;
            if (xl < px0) xl = px0;
            if (xr > px1) xr = px1;
            for (px = xl; px <= xr; ++px)
            {
                uint32_t fc;
                if (gradient && haveInverse)
                {
#if XPAINTER_BRUSH_ON
                    float ux, uy;
                    if (painterMapPoint(&inverse, (float)px + 0.5f,
                                        (float)py + 0.5f, &ux, &uy))
                        fc = painterGradientColor(
                                &self->m_state.m_brush.m_gradient, ux, uy);
                    else
                        fc = color;
                    fc = painterApplyOpacity(fc, self->m_state.m_opacity);
#endif /* XPAINTER_BRUSH_ON */
                }
                else
                {
                    fc = painterApplyOpacity(color, self->m_state.m_opacity);
                }
                painterRaster_putPixel(self, px, py, fc);
            }
        }
    }
}

/**
 * @brief      用一个多边形闭合填充当前画刷的颜色/渐变色。
 * @param self 绘制器指针。
 * @param n    顶点数。
 * @param uxs  用户坐标 X。
 * @param uys  用户坐标 Y。
 */
static void painterFillPolygonShape(XPainter* self, int n,
                                    const float* uxs, const float* uys)
{
    uint32_t brushColor;
    bool gradient = false;
    if (!self || n < 3) return;
#if XPAINTER_BRUSH_ON
    brushColor = self->m_state.m_brush.m_color;
    gradient = self->m_state.m_brush.m_style ==
                 XPainterBrushStyle_LinearGradientPattern ||
               self->m_state.m_brush.m_style ==
                 XPainterBrushStyle_RadialGradientPattern ||
               self->m_state.m_brush.m_style ==
                 XPainterBrushStyle_ConicalGradientPattern;
#else
    brushColor = self->m_state.m_brushColor;
#endif
    if (self->m_deviceKind == XPainterDevice_Image && gradient)
    {
        painterScanFillDevice(self, n, uxs, uys, brushColor, true);
        return;
    }
    painterScanFillUser(self, n, uxs, uys, brushColor, gradient);
}

/**
 * @brief      计算圆弧上若干采样点。
 * @param cx/cy/rx/ry 椭圆中心与半径。
 * @param startA 起始角（弧度）。
 * @param spanA  跨度角（弧度，可负）。
 * @param maxPts 最多采样点数。
 * @param xs/ys  输出坐标数组。
 * @param outN   输出实际点数。
 */
static void painterArcPoints(float cx, float cy, float rx, float ry,
                             float startA, float spanA, int maxPts,
                             float* xs, float* ys, int* outN)
{
    int count;
    int i;
    if (maxPts < 2) maxPts = 2;
    count = maxPts;
    for (i = 0; i < count + 1; ++i)
    {
        float t = (float)i / (float)count;
        float a = startA + spanA * t;
        xs[i] = cx + rx * cosf(a);
        ys[i] = cy + ry * sinf(a);
    }
    if (outN) *outN = count + 1;
}

/** @brief 椭圆外接矩形 → 中心/半径。 */
static void painterEllipseParams(const XRect* rect, float* cx, float* cy,
                                 float* rx, float* ry)
{
    *cx = (float)rect->x + (float)rect->width * 0.5f;
    *cy = (float)rect->y + (float)rect->height * 0.5f;
    *rx = (float)rect->width * 0.5f;
    *ry = (float)rect->height * 0.5f;
}
#endif /* XPAINTER_POLYGON_ON || XPAINTER_SHAPE_ON */

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
#if XPAINTER_SHAPE_ON
    self->m_drawShape = NULL;
#endif
#if XPAINTER_POLYGON_ON
    self->m_drawPolyline = NULL;
    self->m_drawPolygon = NULL;
    self->m_drawPoints = NULL;
#endif
#if XPAINTER_PATH_ON
    self->m_drawPath = NULL;
#endif
#if XPAINTER_VIEW_TRANSFORM_ON
    painterResetViewTransform(self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
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
#if XPAINTER_SHAPE_ON
    self->m_drawShape = NULL;
#endif
#if XPAINTER_POLYGON_ON
    self->m_drawPolyline = NULL;
    self->m_drawPolygon = NULL;
    self->m_drawPoints = NULL;
#endif
#if XPAINTER_PATH_ON
    self->m_drawPath = NULL;
#endif
#if XPAINTER_VIEW_TRANSFORM_ON
    painterResetViewTransform(self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    return true;
}

bool XPainter_end(XPainter* self)
{
    if (!self) return false;
    {
        int i;
        for (i = 0; i < self->m_stateCount; ++i)
            XFont_deinit(&self->m_stateStack[i].m_font);
    }
    XFont_deinit(&self->m_state.m_font);
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
#if XPAINTER_SHAPE_ON
    self->m_drawShape = NULL;
#endif
#if XPAINTER_POLYGON_ON
    self->m_drawPolyline = NULL;
    self->m_drawPolygon = NULL;
    self->m_drawPoints = NULL;
#endif
#if XPAINTER_PATH_ON
    self->m_drawPath = NULL;
#endif
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
#if XPAINTER_PENSTYLE_ON
    if (self->m_state.m_penStyle != XPainterPenStyle_SolidLine)
        return painterDrawLineStyled(self, x1, y1, x2, y2);
#endif /* XPAINTER_PENSTYLE_ON */
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

bool XPainter_drawRects(XPainter* self, const XRect* rects, int rectCount)
{
    int i;
    if (!self) return false;
    if (!rects || rectCount <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_drawLine)
        return false;
    for (i = 0; i < rectCount; ++i)
        if (!XPainter_drawRect(self, &rects[i]))
            return false;
    return true;
}

bool XPainter_drawLines(XPainter* self, const XPoint* pointPairs, int pairCount)
{
    int i;
    if (!self) return false;
    if (!pointPairs || pairCount <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_drawLine)
        return false;
    for (i = 0; i < pairCount; ++i)
    {
        const XPoint* a = &pointPairs[i * 2];
        const XPoint* b = &pointPairs[i * 2 + 1];
        if (!XPainter_drawLine(self, a->x, a->y, b->x, b->y))
            return false;
    }
    return true;
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

bool XPainter_eraseRect(XPainter* self, const XRect* rect)
{
    if (!self) return false;
    return XPainter_fillRect(self, rect, self->m_state.m_backgroundColor);
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

bool XPainter_drawPicture(XPainter* self, XPicture* picture, int x, int y)
{
    bool ok;
    if (!self || !picture) return false;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (!XPainter_save(self)) return false;
    XPainter_translate(self, (float)x, (float)y);
    ok = XPicture_play(picture, self);
    XPainter_restore(self);
    return ok;
}

void XPainter_setFont(XPainter* self, const XFont* font)
{
    if (!self) return;
    XFont_deinit(&self->m_state.m_font);
    if (font)
        XFont_copy(&self->m_state.m_font, font);
    else
        XFont_init(&self->m_state.m_font);
}

const XFont* XPainter_font(const XPainter* self)
{
    return self ? &self->m_state.m_font : NULL;
}

/* ========== 内置 8x16 点阵文本（数据来自 XFont8x16，算法在本文件实现） ========== */

/* ---------- 内部工具：UTF-8 解码 / 缩放 / 逐字形绘制 ---------- */

/**
 * @brief      解码下一个 UTF-8 码点并前进指针。
 * @param p 输入指针（指向 const char*，调用后前进到该字符之后）。
 * @return 解码得到的 Unicode 码点；无效序列返回 0xFFFD 并只前进 1 字节。
 * @note       只支持合法 1~4 字节 UTF-8；C 字符串以 NUL 结尾，越界/
 *             悬垂续字节会撞上 NUL 而被判定为无效，不会越界读取。
 */
static uint32_t painter8x16DecodeNext(const char** p)
{
    const unsigned char* s = (const unsigned char*)*p;
    unsigned char c0 = s[0];
    uint32_t cp;
    int extra;
    int i;
    if (c0 < 0x80u)
    {
        *p += 1;
        return c0;
    }
    if (c0 < 0xC2u || c0 >= 0xF5u)
        goto invalid; /* 悬垂续字节、残缺 2 字节首字节或超范围 4 字节头 */
    if (c0 < 0xE0u)
    {
        extra = 1; cp = c0 & 0x1Fu;
    }
    else if (c0 < 0xF0u)
    {
        extra = 2; cp = c0 & 0x0Fu;
    }
    else
    {
        extra = 3; cp = c0 & 0x07u;
    }
    for (i = 0; i < extra; ++i)
    {
        unsigned char cc = s[1 + i];
        if ((cc & 0xC0u) != 0x80u)
            goto invalid;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if ((extra == 1 && cp < 0x80u) ||
        (extra == 2 && cp < 0x800u) ||
        (cp >= 0xD800u && cp <= 0xDFFFu) ||
        cp > 0x10FFFFu)
        goto invalid;
    *p += 1 + extra;
    return cp;
invalid:
    *p += 1;
    return 0xFFFDu;
}

/**
 * @brief      按字节区间边界解码下一个 UTF-8 码点（有界版本）。
 * @param p   输入指针（指向 const char*，调用后前进到该字符之后）。
 * @param end 字节区间结束位置（排他）。
 * @return 解码得到的 Unicode 码点；到达 end 时返回 0（哨兵，正常文本
 *         不含 NUL，0 不会被当作有效码点）；无效序列返回 0xFFFD 并只
 *         前进 1 字节。
 * @note       区间边界必须落在完整码点边界上；实现绝不越界读取。
 */
static uint32_t painter8x16DecodeNextBounded(const char** p, const char* end)
{
    const unsigned char* s;
    const unsigned char* e;
    unsigned char c0;
    uint32_t cp;
    int extra;
    int i;
    if (!p || !*p || *p >= end)
        return 0;
    s = (const unsigned char*)*p;
    e = (const unsigned char*)end;
    c0 = s[0];
    if (c0 < 0x80u)
    {
        *p += 1;
        return c0;
    }
    if (c0 < 0xC2u || c0 >= 0xF5u)
        goto invalid;
    if (c0 < 0xE0u)
    {
        extra = 1; cp = c0 & 0x1Fu;
    }
    else if (c0 < 0xF0u)
    {
        extra = 2; cp = c0 & 0x0Fu;
    }
    else
    {
        extra = 3; cp = c0 & 0x07u;
    }
    for (i = 0; i < extra; ++i)
    {
        if (s + 1 + i >= e)
            goto invalid;
        else
        {
            unsigned char cc = s[1 + i];
            if ((cc & 0xC0u) != 0x80u)
                goto invalid;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
    }
    if ((extra == 1 && cp < 0x80u) ||
        (extra == 2 && cp < 0x800u) ||
        (cp >= 0xD800u && cp <= 0xDFFFu) ||
        cp > 0x10FFFFu)
        goto invalid;
    *p += 1 + extra;
    return cp;
invalid:
    *p += 1;
    return 0xFFFDu;
}

/** @brief 规范化整倍缩放系数（<1 按 1 处理；统一复用 XFont 位图缩放算法）。 */
static int painter8x16Scale(int scale)
{
    return XFont_bitmapScaledSize(1, scale);
}

/**
 * @brief      逐运行段绘制一个 8x16 字形（最近邻整倍缩放）。
 * @param painter 绘制器指针（非空，文本 API 入口已校验设备）。
 * @param x 缩放后字形左上角 X。
 * @param baselineY 缩放后基线 Y（字形顶部 = baselineY - ASCENT*scale）。
 * @param color ARGB32 颜色。
 * @param glyph 16 字节字形行数据。
 * @param scale 整倍缩放系数（<1 按 1 处理）。
 */
static void painter8x16DrawGlyphScaled(XPainter* painter, int x,
                                       int baselineY, uint32_t color,
                                       const unsigned char* glyph, int scale)
{
    int row;
    int s;
    if (!painter || !glyph)
        return;
    s = painter8x16Scale(scale);
    for (row = 0; row < XFONT8X16_HEIGHT; ++row)
    {
        unsigned bits = glyph[row];
        int col = 0;
        if (bits == 0u)
            continue;
        while (col < XFONT8X16_WIDTH)
        {
            int runStart = -1;
            int runLen = 0;
            /* 合并连续置位像素为一个填充矩形（位 0 为最左像素）。 */
            while (col < XFONT8X16_WIDTH &&
                   (bits & (1u << col)) != 0u)
            {
                if (runStart < 0) runStart = col;
                ++runLen;
                ++col;
            }
            if (runStart >= 0)
            {
                XRect r;
                r.x = x + runStart * s;
                r.y = baselineY - XFONT8X16_ASCENT * s + row * s;
                r.width = runLen * s;
                r.height = s;
                XPainter_fillRect(painter, &r, color);
            }
            if (col < XFONT8X16_WIDTH)
                ++col; /* 跳过 0 位 */
        }
    }
}

/* ---------- 内置点阵字库表（字体由 XFont 的 family 选择） ---------- */

/** @brief 内置点阵字库数据描述（后续新增字库只需在表中追加）。 */
typedef struct PainterBitmapFontTable
{
    const char* m_family;   /**< 家族名；空串表示默认回退字库。 */
    int m_width;            /**< 原始字宽（像素）。 */
    int m_height;           /**< 原始行高（像素）。 */
    int m_ascent;           /**< 原始基线上高度（像素）。 */
    int m_descent;          /**< 原始基线下高度（像素）。 */
    void (*m_loadGlyph)(uint32_t cp, unsigned char* out); /**< 字形数据读取。 */
} PainterBitmapFontTable;

/** @brief 当前内置字库表（数据来自 XFont8x16；算法统一在 XFont/本文件）。 */
static const PainterBitmapFontTable kPainterBitmapFonts[] =
{
    { "",           XFONT8X16_WIDTH, XFONT8X16_HEIGHT,
      XFONT8X16_ASCENT, XFONT8X16_DESCENT, XFont8x16_loadGlyph },
    { "8x16",       XFONT8X16_WIDTH, XFONT8X16_HEIGHT,
      XFONT8X16_ASCENT, XFONT8X16_DESCENT, XFont8x16_loadGlyph },
    { "Monospace",  XFONT8X16_WIDTH, XFONT8X16_HEIGHT,
      XFONT8X16_ASCENT, XFONT8X16_DESCENT, XFont8x16_loadGlyph }
};

/** @brief 由 XFont 选择内置点阵字库（未知 family 回退默认字库）。 */
static const PainterBitmapFontTable* painterBitmapFont(const XFont* font)
{
    const char* family = font ? XFont_family(font) : "";
    size_t i;
    if (family && family[0] != '\0')
    {
        for (i = 1; i < sizeof(kPainterBitmapFonts) /
                            sizeof(kPainterBitmapFonts[0]); ++i)
        {
            if (strcmp(family, kPainterBitmapFonts[i].m_family) == 0)
                return &kPainterBitmapFonts[i];
        }
    }
    return &kPainterBitmapFonts[0];
}

/** @brief 由 XFont 像素字号计算整倍缩放系数（NULL 按默认字库 = 1 倍）。 */
static int painterBitmapScaleForFont(const XFont* font)
{
    return XFont_bitmapScaleForFont(font, XFONT8X16_HEIGHT);
}

/* ---------- 公开文本 API（字体由 XFont 选择，无字库专用命名） ---------- */

bool XPainter_drawText(XPainter* self, int x, int baselineY,
                       const char* utf8, uint32_t color)
{
    const PainterBitmapFontTable* table;
    int scale;
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return false;
    if (!utf8 || utf8[0] == '\0')
        return true; /* 空文本视为无操作成功 */
    table = painterBitmapFont(&self->m_state.m_font);
    scale = painterBitmapScaleForFont(&self->m_state.m_font);
    {
        const char* p = utf8;
        while (*p != '\0')
        {
            uint32_t cp;
            if (*p == '\n')
                break; /* 单行绘制：换行符前停止 */
            cp = painter8x16DecodeNext(&p);
            if (cp < 0x20u)
            {
                x += table->m_width * scale; /* 控制字符：占位不画 */
                continue;
            }
            {
                unsigned char glyphData[XFONT8X16_HEIGHT];
                table->m_loadGlyph(cp, glyphData);
                painter8x16DrawGlyphScaled(self, x, baselineY, color,
                                           glyphData, scale);
            }
            x += table->m_width * scale;
        }
    }
    return true;
}

int XPainter_textWidth(const XFont* font, const char* utf8)
{
    const PainterBitmapFontTable* table = painterBitmapFont(font);
    const char* p;
    int scale;
    int count = 0;
    if (!utf8)
        return 0;
    scale = painterBitmapScaleForFont(font);
    p = utf8;
    while (*p != '\0' && *p != '\n')
    {
        (void)painter8x16DecodeNext(&p);
        ++count;
    }
    return count * table->m_width * scale;
}

int XPainter_textHeight(const XFont* font)
{
    return XFont_bitmapScaledSize(painterBitmapFont(font)->m_height,
                                  painterBitmapScaleForFont(font));
}

int XPainter_textAscent(const XFont* font)
{
    return XFont_bitmapScaledSize(painterBitmapFont(font)->m_ascent,
                                  painterBitmapScaleForFont(font));
}

int XPainter_textDescent(const XFont* font)
{
    return XFont_bitmapScaledSize(painterBitmapFont(font)->m_descent,
                                  painterBitmapScaleForFont(font));
}

int XPainter_drawGlyph(XPainter* self, int x, int baselineY,
                       const char* utf8, uint32_t color)
{
    int scale;
    const char* p;
    const char* start;
    uint32_t cp;
    int adv;
    bool drawable;
    if (!utf8 || utf8[0] == '\0')
        return 0;
    start = utf8;
    p = utf8;
    cp = painter8x16DecodeNext(&p);
    adv = (int)(p - start);
    if (cp < 0x20u)
        return adv; /* 控制字符：占位不画 */
    drawable = (self != NULL) && (self->m_deviceKind != XPainterDevice_None);
    if (!drawable)
        return adv;
    scale = painterBitmapScaleForFont(&self->m_state.m_font);
    {
        unsigned char glyphData[XFONT8X16_HEIGHT];
        painterBitmapFont(&self->m_state.m_font)->m_loadGlyph(cp, glyphData);
        painter8x16DrawGlyphScaled(self, x, baselineY, color,
                                   glyphData, scale);
    }
    return adv;
}

int XPainter_textWidthRange(const XFont* font, const char* utf8,
                            int startByte, int endByte)
{
    const PainterBitmapFontTable* table = painterBitmapFont(font);
    const char* p;
    const char* end;
    int scale;
    int count = 0;
    if (!utf8 || startByte < 0)
        return 0;
    scale = painterBitmapScaleForFont(font);
    p = utf8 + startByte;
    end = utf8 + endByte;
    if (endByte >= 0 && end < p)
        return 0;
    while (p < end)
    {
        uint32_t cp = painter8x16DecodeNextBounded(&p, end);
        if (cp == 0u)
            break;
        ++count;
    }
    return count * table->m_width * scale;
}

#if (XPAINTER_SHAPE_ON) || (XPAINTER_POLYGON_ON)
/** @brief 判断当前画刷是否要填充形状内部。 */
static bool painterBrushShouldFill(XPainter* self)
{
    if (!self) return false;
#if XPAINTER_BRUSH_ON
    return self->m_state.m_brush.m_style != XPainterBrushStyle_NoBrush;
#else
    return true;
#endif
}

/** @brief 用画线方式连接一组用户空间浮点顶点（画刷样式不参与）。 */
static void painterDrawPolyLineFloat(XPainter* self,
                                     const float* uxs, const float* uys,
                                     int n, bool close)
{
    int i;
    if (!self || n < 2) return;
    for (i = 0; i + 1 < n; ++i)
    {
        if (!XPainter_drawLine(self, painterRound(uxs[i]), painterRound(uys[i]),
                               painterRound(uxs[i + 1]),
                               painterRound(uys[i + 1])))
            return;
    }
    if (close && n > 2)
        XPainter_drawLine(self, painterRound(uxs[n - 1]), painterRound(uys[n - 1]),
                          painterRound(uxs[0]), painterRound(uys[0]));
}
#endif /* XPAINTER_SHAPE_ON || XPAINTER_POLYGON_ON */

#if XPAINTER_SHAPE_ON
bool XPainter_drawEllipse(XPainter* self, const XRect* rect)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    int n;
    if (!self) return false;
    if (!rect || rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Ellipse, rect,
                                 0, 0, false, 0, 0);
    painterEllipseParams(rect, &cx, &cy, &rx, &ry);
    if (painterBrushShouldFill(self))
    {
        painterArcPoints(cx, cy, rx, ry, 0.0f, 6.283185307179586f,
                         64, xs, ys, &n);
        painterFillPolygonShape(self, n, xs, ys);
    }
    painterArcPoints(cx, cy, rx, ry, 0.0f, 6.283185307179586f,
                     32, xs, ys, &n);
    painterDrawPolyLineFloat(self, xs, ys, n, true);
    return true;
}

bool XPainter_drawArc(XPainter* self, const XRect* rect,
                      int startAngle, int spanAngle)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    float deg0, degS;
    int n;
    if (!self) return false;
    if (!rect || rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (spanAngle == 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Arc, rect,
                                 startAngle, spanAngle, false, 0, 0);
    painterEllipseParams(rect, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)spanAngle / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    painterDrawPolyLineFloat(self, xs, ys, n, false);
    return true;
}

bool XPainter_drawPie(XPainter* self, const XRect* rect,
                      int startAngle, int spanAngle, bool filled)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float polyX[XPAINTER_POLY_MAX_POINTS];
    float polyY[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    float deg0, degS;
    int n;
    int i;
    if (!self) return false;
    if (!rect || rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (spanAngle == 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Pie, rect,
                                 startAngle, spanAngle, filled, 0, 0);
    painterEllipseParams(rect, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)spanAngle / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    if (filled && painterBrushShouldFill(self))
    {
        polyX[0] = cx; polyY[0] = cy;
        for (i = 0; i < n; ++i) { polyX[1 + i] = xs[i]; polyY[1 + i] = ys[i]; }
        painterFillPolygonShape(self, n + 1, polyX, polyY);
    }
    /* 轮廓：圆心 → 起始点 → 弧 → 终点 → 圆心 */
    if (n > 0)
        XPainter_drawLine(self, painterRound(cx), painterRound(cy),
                          painterRound(xs[0]), painterRound(ys[0]));
    painterDrawPolyLineFloat(self, xs, ys, n, false);
    if (n > 0)
        XPainter_drawLine(self, painterRound(xs[n - 1]), painterRound(ys[n - 1]),
                          painterRound(cx), painterRound(cy));
    return true;
}

bool XPainter_drawChord(XPainter* self, const XRect* rect,
                        int startAngle, int spanAngle, bool filled)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    float deg0, degS;
    int n;
    if (!self) return false;
    if (!rect || rect->width <= 0 || rect->height <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (spanAngle == 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Chord, rect,
                                 startAngle, spanAngle, filled, 0, 0);
    painterEllipseParams(rect, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)spanAngle / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    if (filled && n >= 3 && painterBrushShouldFill(self))
        painterFillPolygonShape(self, n, xs, ys);
    painterDrawPolyLineFloat(self, xs, ys, n, true); /* 闭合弦 */
    return true;
}

bool XPainter_drawRoundedRect(XPainter* self, const XRect* rect,
                              int xRadius, int yRadius)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float xr = (float)xRadius, yr = (float)yRadius;
    int count = 0;
    int perCorner;
    int x = rect->x, y = rect->y, w = rect->width, h = rect->height;
    if (!self) return false;
    if (!rect || w <= 0 || h <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_RoundedRect, rect,
                                 0, 0, false, xRadius, yRadius);
    if (xr <= 0.0f || yr <= 0.0f)
        return XPainter_drawRect(self, rect);
    if (xr > (float)w * 0.5f) xr = (float)w * 0.5f;
    if (yr > (float)h * 0.5f) yr = (float)h * 0.5f;
    perCorner = 6;
    /* 顶边 */
    {
        int i;
        float cx0 = (float)x + xr, cy0 = (float)y + yr;
        float cx1 = (float)x + (float)w - xr, cy1 = (float)y + yr;
        float cx2 = (float)x + (float)w - xr, cy2 = (float)y + (float)h - yr;
        float cx3 = (float)x + xr, cy3 = (float)y + (float)h - yr;
        xs[count] = cx1; ys[count] = (float)y; ++count; /* 顶边右端 */
        /* 右上角：从 -90° 到 0° */
        for (i = 0; i <= perCorner; ++i)
        {
            float a = -90.0f + 90.0f * (float)i / (float)perCorner;
            float r = a * XPAINTER_DEG_TO_RAD;
            xs[count] = cx1 + xr * cosf(r); ys[count] = cy1 + yr * sinf(r);
            ++count;
        }
        /* 右边 */
        xs[count] = (float)x + (float)w; ys[count] = cy2; ++count;
        /* 右下角：0°→90° */
        for (i = 1; i <= perCorner; ++i)
        {
            float a = 90.0f * (float)i / (float)perCorner;
            float r = a * XPAINTER_DEG_TO_RAD;
            xs[count] = cx2 + xr * cosf(r); ys[count] = cy2 + yr * sinf(r);
            ++count;
        }
        /* 底边 */
        xs[count] = cx3; ys[count] = (float)y + (float)h; ++count;
        /* 左下角：90°→180° */
        for (i = 1; i <= perCorner; ++i)
        {
            float a = 90.0f + 90.0f * (float)i / (float)perCorner;
            float r = a * XPAINTER_DEG_TO_RAD;
            xs[count] = cx3 + xr * cosf(r); ys[count] = cy3 + yr * sinf(r);
            ++count;
        }
        /* 左边 */
        xs[count] = (float)x; ys[count] = cy0; ++count;
        /* 左上角：180°→270° */
        for (i = 1; i <= perCorner; ++i)
        {
            float a = 180.0f + 90.0f * (float)i / (float)perCorner;
            float r = a * XPAINTER_DEG_TO_RAD;
            xs[count] = cx0 + xr * cosf(r); ys[count] = cy0 + yr * sinf(r);
            ++count;
        }
        xs[count] = cx1; ys[count] = (float)y; ++count;
    }
    if (painterBrushShouldFill(self) && count >= 3)
        painterFillPolygonShape(self, count, xs, ys);
    painterDrawPolyLineFloat(self, xs, ys, count, true);
    return true;
}
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
bool XPainter_drawPolyline(XPainter* self, const XPoint* points, int count)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    int i;
    if (!self) return false;
    if (!points || count <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (count > XPAINTER_POLY_MAX_POINTS) count = XPAINTER_POLY_MAX_POINTS;
    if (self->m_drawPolyline)
        return self->m_drawPolyline(self, points, count);
    for (i = 0; i < count; ++i) { xs[i] = (float)points[i].x; ys[i] = (float)points[i].y; }
    painterDrawPolyLineFloat(self, xs, ys, count, false);
    return true;
}

bool XPainter_drawPolygon(XPainter* self, const XPoint* points, int count,
                          bool filled)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    int i;
    if (!self) return false;
    if (!points || count <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (count > XPAINTER_POLY_MAX_POINTS) count = XPAINTER_POLY_MAX_POINTS;
    if (self->m_drawPolygon)
        return self->m_drawPolygon(self, points, count, filled);
    for (i = 0; i < count; ++i) { xs[i] = (float)points[i].x; ys[i] = (float)points[i].y; }
    if (filled && count >= 3 && painterBrushShouldFill(self))
        painterFillPolygonShape(self, count, xs, ys);
    painterDrawPolyLineFloat(self, xs, ys, count, count >= 3);
    return true;
}

bool XPainter_drawConvexPolygon(XPainter* self, const XPoint* points,
                                int count)
{
    /* Qt 的 drawConvexPolygon 始终用当前画刷填充、当前画笔描边；
     * 当前与 drawPolygon 共用扫描线实现，不区分凸凹性。 */
    return XPainter_drawPolygon(self, points, count, true);
}
bool XPainter_drawPoints(XPainter* self, const XPoint* points, int count)
{
    int i;
    if (!self) return false;
    if (!points || count <= 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (self->m_drawPoints)
        return self->m_drawPoints(self, points, count);
    for (i = 0; i < count; ++i)
        if (!XPainter_drawPoint(self, points[i].x, points[i].y))
            return false;
    return true;
}
#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
/* ========== QPainterPath 风格路径实现（对标 Qt 6.8 QPainterPath） ========== */

static bool painterPathEnsure(XPainterPath* self, int extra)
{
    int newCapacity;
    XPainterPathElement* p;
    if (!self) return false;
    if (extra <= 0) return true;
    if (self->m_elementCount + extra <= self->m_elementCapacity)
        return true;
    newCapacity = self->m_elementCapacity > 0 ? self->m_elementCapacity : 16;
    while (newCapacity < self->m_elementCount + extra)
        newCapacity *= 2;
    p = (XPainterPathElement*)XRealloc_System(
            self->m_elements, (size_t)newCapacity * sizeof(XPainterPathElement));
    if (!p) return false;
    self->m_elements = p;
    self->m_elementCapacity = newCapacity;
    return true;
}

static bool painterPathAppendElt(XPainterPath* self, XPainterPathElementType type,
                                 float x1, float y1, float x2, float y2,
                                 float x3, float y3)
{
    XPainterPathElement* e;
    if (!self) return false;
    if (!painterPathEnsure(self, 1)) return false;
    e = &self->m_elements[self->m_elementCount++];
    e->m_type = type;
    e->m_x1 = x1; e->m_y1 = y1;
    e->m_x2 = x2; e->m_y2 = y2;
    e->m_x3 = x3; e->m_y3 = y3;
    return true;
}

void XPainterPath_init(XPainterPath* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
}

void XPainterPath_deinit(XPainterPath* self)
{
    if (!self) return;
    XFree_System(self->m_elements);
    memset(self, 0, sizeof(*self));
}

bool XPainterPath_moveTo(XPainterPath* self, float x, float y)
{
    if (!self) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    self->m_subpathStartX = x;
    self->m_subpathStartY = y;
    return painterPathAppendElt(self, XPainterPathElement_MoveTo,
                                x, y, 0.0f, 0.0f, 0.0f, 0.0f);
}

bool XPainterPath_lineTo(XPainterPath* self, float x, float y)
{
    if (!self || self->m_elementCount == 0) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return painterPathAppendElt(self, XPainterPathElement_LineTo,
                                x, y, 0.0f, 0.0f, 0.0f, 0.0f);
}

bool XPainterPath_quadTo(XPainterPath* self, float cx, float cy,
                         float x, float y)
{
    if (!self || self->m_elementCount == 0) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return painterPathAppendElt(self, XPainterPathElement_QuadTo,
                                cx, cy, x, y, 0.0f, 0.0f);
}

bool XPainterPath_cubicTo(XPainterPath* self, float c1x, float c1y,
                          float c2x, float c2y, float x, float y)
{
    if (!self || self->m_elementCount == 0) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return painterPathAppendElt(self, XPainterPathElement_CubicTo,
                                c1x, c1y, c2x, c2y, x, y);
}

bool XPainterPath_closeSubpath(XPainterPath* self)
{
    if (!self || self->m_elementCount == 0) return false;
    if (!painterPathAppendElt(self, XPainterPathElement_CloseSubpath,
                              0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;
    self->m_currentX = self->m_subpathStartX;
    self->m_currentY = self->m_subpathStartY;
    return true;
}

bool XPainterPath_addRect(XPainterPath* self, const XRect* rect)
{
    float x1, y1, x2, y2;
    bool ok;
    if (!self || !rect || rect->width <= 0 || rect->height <= 0)
        return false;
    x1 = (float)rect->x;
    y1 = (float)rect->y;
    x2 = x1 + (float)rect->width;
    y2 = y1 + (float)rect->height;
    ok = XPainterPath_moveTo(self, x1, y1);
    ok = ok && XPainterPath_lineTo(self, x2, y1);
    ok = ok && XPainterPath_lineTo(self, x2, y2);
    ok = ok && XPainterPath_lineTo(self, x1, y2);
    ok = ok && XPainterPath_closeSubpath(self);
    return ok;
}

bool XPainterPath_addEllipse(XPainterPath* self, const XRect* rect)
{
    int i;
    float cx, cy, rx, ry;
    bool ok;
    const int segments = 64;
    if (!self || !rect || rect->width <= 0 || rect->height <= 0)
        return false;
    cx = (float)rect->x + (float)rect->width * 0.5f;
    cy = (float)rect->y + (float)rect->height * 0.5f;
    rx = (float)rect->width * 0.5f;
    ry = (float)rect->height * 0.5f;
    ok = XPainterPath_moveTo(self, cx + rx, cy);
    for (i = 1; i <= segments; ++i)
    {
        float a = 6.283185307179586f * (float)i / (float)segments;
        ok = ok && XPainterPath_lineTo(self, cx + rx * cosf(a),
                                       cy + ry * sinf(a));
    }
    ok = ok && XPainterPath_closeSubpath(self);
    return ok;
}

int XPainterPath_elementCount(const XPainterPath* self)
{
    return self ? self->m_elementCount : 0;
}

void XPainterPath_currentPosition(const XPainterPath* self,
                                  float* x, float* y)
{
    if (!self) return;
    if (x) *x = self->m_currentX;
    if (y) *y = self->m_currentY;
}

typedef struct PainterPathVertices
{
    float* xs;
    float* ys;
    int m_count;
    int m_capacity;
} PainterPathVertices;

static bool painterPathVerticesPush(PainterPathVertices* v, float x, float y)
{
    int newCapacity;
    float* nx;
    float* ny;
    if (!v) return false;
    if (v->m_count >= v->m_capacity)
    {
        newCapacity = v->m_capacity > 0 ? v->m_capacity * 2 : 64;
        nx = (float*)XRealloc_System(v->xs, (size_t)newCapacity * sizeof(float));
        if (!nx) return false;
        ny = (float*)XRealloc_System(v->ys, (size_t)newCapacity * sizeof(float));
        if (!ny)
        {
            XFree_System(nx);
            return false;
        }
        v->xs = nx;
        v->ys = ny;
        v->m_capacity = newCapacity;
    }
    v->xs[v->m_count] = x;
    v->ys[v->m_count] = y;
    ++v->m_count;
    return true;
}

static void painterPathVerticesReset(PainterPathVertices* v)
{
    if (!v) return;
    v->m_count = 0;
}

static void painterPathVerticesFree(PainterPathVertices* v)
{
    if (!v) return;
    XFree_System(v->xs);
    XFree_System(v->ys);
    memset(v, 0, sizeof(*v));
}

static void painterPathFlattenQuad(PainterPathVertices* v,
                                   float sx, float sy,
                                   float cx, float cy,
                                   float ex, float ey)
{
    int i;
    const int segments = 16;
    for (i = 1; i <= segments; ++i)
    {
        float t = (float)i / (float)segments;
        float inv = 1.0f - t;
        float x = inv * inv * sx + 2.0f * inv * t * cx + t * t * ex;
        float y = inv * inv * sy + 2.0f * inv * t * cy + t * t * ey;
        if (!painterPathVerticesPush(v, x, y)) return;
    }
}

static void painterPathFlattenCubic(PainterPathVertices* v,
                                    float sx, float sy,
                                    float c1x, float c1y,
                                    float c2x, float c2y,
                                    float ex, float ey)
{
    int i;
    const int segments = 24;
    for (i = 1; i <= segments; ++i)
    {
        float t = (float)i / (float)segments;
        float inv = 1.0f - t;
        float x = inv*inv*inv*sx + 3.0f*inv*inv*t*c1x
                  + 3.0f*inv*t*t*c2x + t*t*t*ex;
        float y = inv*inv*inv*sy + 3.0f*inv*inv*t*c1y
                  + 3.0f*inv*t*t*c2y + t*t*t*ey;
        if (!painterPathVerticesPush(v, x, y)) return;
    }
}

static bool painterPathDraw(XPainter* self, const XPainterPath* path,
                            bool fill, bool stroke)
{
    PainterPathVertices v;
    int i;
    bool closed = false;
    float px;
    float py;
    if (!self) return false;
    if (!path || path->m_elementCount == 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    memset(&v, 0, sizeof(v));
    painterPathVerticesReset(&v);
    for (i = 0; i < path->m_elementCount; ++i)
    {
        const XPainterPathElement* e = &path->m_elements[i];
        switch (e->m_type)
        {
            case XPainterPathElement_MoveTo:
                if (fill && v.m_count >= 3)
                    painterFillPolygonShape(self, v.m_count, v.xs, v.ys);
                if (stroke && v.m_count >= 2)
                    painterDrawPolyLineFloat(self, v.xs, v.ys, v.m_count, closed);
                if (!painterPathVerticesPush(&v, e->m_x1, e->m_y1)) return false;
                closed = false;
                break;
            case XPainterPathElement_LineTo:
                if (!painterPathVerticesPush(&v, e->m_x1, e->m_y1)) return false;
                break;
            case XPainterPathElement_QuadTo:
                if (v.m_count < 1) goto fail;
                px = v.xs[v.m_count - 1];
                py = v.ys[v.m_count - 1];
                painterPathFlattenQuad(&v, px, py, e->m_x1, e->m_y1,
                                       e->m_x2, e->m_y2);
                break;
            case XPainterPathElement_CubicTo:
                if (v.m_count < 1) goto fail;
                px = v.xs[v.m_count - 1];
                py = v.ys[v.m_count - 1];
                painterPathFlattenCubic(&v, px, py, e->m_x1, e->m_y1,
                                        e->m_x2, e->m_y2, e->m_x3, e->m_y3);
                break;
            case XPainterPathElement_CloseSubpath:
                closed = true;
                break;
            default:
                break;
        }
    }
    if (fill && v.m_count >= 3)
        painterFillPolygonShape(self, v.m_count, v.xs, v.ys);
    if (stroke && v.m_count >= 2)
        painterDrawPolyLineFloat(self, v.xs, v.ys, v.m_count, closed);
    painterPathVerticesFree(&v);
    return true;
fail:
    painterPathVerticesFree(&v);
    return false;
}

/**
 * @brief      路径绘制统一入口：先做参数/设备校验，再优先派发高层回调。
 * @param self 绘制器指针。
 * @param path 路径对象。
 * @param op 高层回调使用的命令枚举。
 * @param fill 是否填充内部。
 * @param stroke 是否描边轮廓。
 * @return 绘制成功返回 true。
 */
static bool painterPathDrawDispatch(XPainter* self, const XPainterPath* path,
                                    XPainterPathOp op, bool fill, bool stroke)
{
    if (!self) return false;
    if (!path || path->m_elementCount == 0) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (self->m_drawPath)
        return self->m_drawPath(self, op, path);
    return painterPathDraw(self, path, fill, stroke);
}

bool XPainter_drawPath(XPainter* self, const XPainterPath* path)
{
    return painterPathDrawDispatch(self, path, XPainterPathOp_Draw, true, true);
}

bool XPainter_fillPath(XPainter* self, const XPainterPath* path)
{
    return painterPathDrawDispatch(self, path, XPainterPathOp_Fill, true, false);
}

bool XPainter_strokePath(XPainter* self, const XPainterPath* path)
{
    return painterPathDrawDispatch(self, path, XPainterPathOp_Stroke, false, true);
}
#endif /* XPAINTER_PATH_ON */



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
    if (!self) return;
    self->m_state.m_brushColor = color;
#if XPAINTER_BRUSH_ON
    self->m_state.m_brush.m_color = color;
    self->m_state.m_brush.m_style = XPainterBrushStyle_SolidPattern;
#endif /* XPAINTER_BRUSH_ON */
}

uint32_t XPainter_brushColor(const XPainter* self)
{
    return self ? self->m_state.m_brushColor : 0u;
}
#if XPAINTER_PENSTYLE_ON
void XPainter_setPenStyle(XPainter* self, XPainterPenStyle style)
{
    if (!self) return;
    if (style < XPainterPenStyle_NoPen || style > XPainterPenStyle_DashDotDotLine)
        style = XPainterPenStyle_SolidLine;
    self->m_state.m_penStyle = style;
}

XPainterPenStyle XPainter_penStyle(const XPainter* self)
{
    return self ? self->m_state.m_penStyle : XPainterPenStyle_SolidLine;
}

void XPainter_setPenCapStyle(XPainter* self, XPainterPenCapStyle cap)
{
    if (!self) return;
    if (cap < XPainterPenCapStyle_FlatCap || cap > XPainterPenCapStyle_RoundCap)
        cap = XPainterPenCapStyle_FlatCap;
    self->m_state.m_penCap = cap;
}

XPainterPenCapStyle XPainter_penCapStyle(const XPainter* self)
{
    return self ? self->m_state.m_penCap : XPainterPenCapStyle_FlatCap;
}

void XPainter_setPenJoinStyle(XPainter* self, XPainterPenJoinStyle join)
{
    if (!self) return;
    if (join < XPainterPenJoinStyle_MiterJoin || join > XPainterPenJoinStyle_RoundJoin)
        join = XPainterPenJoinStyle_MiterJoin;
    self->m_state.m_penJoin = join;
}

XPainterPenJoinStyle XPainter_penJoinStyle(const XPainter* self)
{
    return self ? self->m_state.m_penJoin : XPainterPenJoinStyle_MiterJoin;
}
#endif /* XPAINTER_PENSTYLE_ON */

#if XPAINTER_BRUSH_ON
void XPainterGradient_initLinear(XPainterGradient* g, float x1, float y1,
                                 float x2, float y2)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->m_type = XPainterGradientType_Linear;
    g->m_startX = x1; g->m_startY = y1;
    g->m_endX = x2;   g->m_endY = y2;
}

void XPainterGradient_initRadial(XPainterGradient* g, float cx, float cy,
                                 float radius, float focalX, float focalY)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->m_type = XPainterGradientType_Radial;
    g->m_centerX = cx; g->m_centerY = cy;
    g->m_focalX = focalX; g->m_focalY = focalY;
    g->m_radius = radius;
}

void XPainterGradient_initConical(XPainterGradient* g, float cx, float cy,
                                  float angleDeg)
{
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->m_type = XPainterGradientType_Conical;
    g->m_centerX = cx; g->m_centerY = cy;
    g->m_angleDeg = angleDeg;
}

void XPainterGradient_addStop(XPainterGradient* g, float position, uint32_t color)
{
    if (!g || g->m_stopCount >= XPAINTER_GRADIENT_MAX_STOPS)
        return;
    if (position < 0.0f) position = 0.0f;
    if (position > 1.0f) position = 1.0f;
    g->m_stops[g->m_stopCount].m_position = position;
    g->m_stops[g->m_stopCount].m_color = color;
    ++g->m_stopCount;
}

void XPainter_setBrushStyle(XPainter* self, XPainterBrushStyle style)
{
    if (!self) return;
    if (style < XPainterBrushStyle_NoBrush ||
        style > XPainterBrushStyle_ConicalGradientPattern)
        style = XPainterBrushStyle_SolidPattern;
    self->m_state.m_brush.m_style = style;
}

XPainterBrushStyle XPainter_brushStyle(const XPainter* self)
{
    return self ? self->m_state.m_brush.m_style : XPainterBrushStyle_NoBrush;
}

void XPainter_setBrushGradient(XPainter* self, const XPainterGradient* gradient)
{
    if (!self) return;
    self->m_state.m_brush.m_gradient = *gradient;
    self->m_state.m_brush.m_style = XPainterBrushStyle_LinearGradientPattern;
}

void XPainter_brush(const XPainter* self, XPainterBrush* out)
{
    if (!out) return;
    if (!self)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = self->m_state.m_brush;
}
#endif /* XPAINTER_BRUSH_ON */



#if XPAINTER_CLIP_ON
/** @brief 把逻辑矩形按当前组合变换映射为设备坐标包围矩形。 */
static bool painterMapClipRect(const XPainterState* state, const XRect* rect,
                               XRect* out)
{
    XImageTransform transform;
    float minX;
    float minY;
    float maxX;
    float maxY;
    int left;
    int top;
    int right;
    int bottom;
    int64_t width;
    int64_t height;
    if (!state || !rect || !out) return false;
    memset(out, 0, sizeof(*out));
    if (rect->width <= 0 || rect->height <= 0) return true;
    if (!painterEffectiveTransform(state, &transform) ||
        !painterMapRectCorners(&transform, rect, &minX, &minY, &maxX, &maxY))
        return false;
    left = painterFloorInt(minX);
    top = painterFloorInt(minY);
    right = painterCeilInt(maxX);
    bottom = painterCeilInt(maxY);
    width = (int64_t)right - (int64_t)left;
    height = (int64_t)bottom - (int64_t)top;
    out->x = left;
    out->y = top;
    out->width = width > INT_MAX ? INT_MAX : (width > 0 ? (int)width : 0);
    out->height = height > INT_MAX ? INT_MAX : (height > 0 ? (int)height : 0);
    return true;
}

void XPainter_setClipRect(XPainter* self, const XRect* rect,
                          XPainterClipOperation operation)
{
    XRect mapped;
    if (!self || self->m_deviceKind == XPainterDevice_None || !rect) return;
    if (operation != XPainterClipOperation_NoClip &&
        operation != XPainterClipOperation_ReplaceClip &&
        operation != XPainterClipOperation_IntersectClip)
        operation = XPainterClipOperation_ReplaceClip;
    if (!painterMapClipRect(&self->m_state, rect, &mapped)) return;

    /* Qt 的非 Picture 引擎在裁剪未启用时把 IntersectClip 简化为 ReplaceClip。 */
    if (!self->m_state.m_hasClip &&
        operation == XPainterClipOperation_IntersectClip)
        operation = XPainterClipOperation_ReplaceClip;

    if (operation == XPainterClipOperation_IntersectClip)
        self->m_state.m_clipRect =
            XRect_intersected(&self->m_state.m_clipRect, &mapped);
    else
        self->m_state.m_clipRect = mapped;

    self->m_state.m_hasClipRect = true;
    self->m_state.m_clipOperation = operation;
    self->m_state.m_hasClip = operation != XPainterClipOperation_NoClip;
}

bool XPainter_hasClipping(const XPainter* self)
{
    return self && self->m_deviceKind != XPainterDevice_None &&
           self->m_state.m_hasClip &&
           self->m_state.m_clipOperation != XPainterClipOperation_NoClip;
}

void XPainter_setClipping(XPainter* self, bool enable)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    if (XPainter_hasClipping(self) == enable) return;
    if (enable && (!self->m_state.m_hasClipRect ||
                   self->m_state.m_clipOperation ==
                       XPainterClipOperation_NoClip))
        return;
    self->m_state.m_hasClip = enable;
}

void XPainter_clipBoundingRect(const XPainter* self, XRect* out)
{
    XImageTransform transform;
    XImageTransform inverse;
    float minX;
    float minY;
    float maxX;
    float maxY;
    int left;
    int top;
    int right;
    int bottom;
    int64_t width;
    int64_t height;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!self || self->m_deviceKind == XPainterDevice_None ||
        !self->m_state.m_hasClipRect || self->m_state.m_clipRect.width <= 0 ||
        self->m_state.m_clipRect.height <= 0)
        return;
    if (!painterEffectiveTransform(&self->m_state, &transform) ||
        !painterMatrixInvert(&transform, &inverse) ||
        !painterMapRectCorners(&inverse, &self->m_state.m_clipRect,
                               &minX, &minY, &maxX, &maxY))
        return;
    left = painterFloorInt(minX);
    top = painterFloorInt(minY);
    right = painterCeilInt(maxX);
    bottom = painterCeilInt(maxY);
    width = (int64_t)right - (int64_t)left;
    height = (int64_t)bottom - (int64_t)top;
    out->x = left;
    out->y = top;
    out->width = width > INT_MAX ? INT_MAX : (width > 0 ? (int)width : 0);
    out->height = height > INT_MAX ? INT_MAX : (height > 0 ? (int)height : 0);
}
#endif /* XPAINTER_CLIP_ON */
void XPainter_setTransform(XPainter* self, const XImageTransform* matrix,
                           bool combine)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !matrix)
        return;
    if (combine)
        self->m_state.m_transform =
            painterMatrixMultiply(&self->m_state.m_transform, matrix);
    else
        self->m_state.m_transform = *matrix;
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
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
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    memset(&self->m_state.m_transform, 0, sizeof(XImageTransform));
    self->m_state.m_transform.m11 = 1.0f;
    self->m_state.m_transform.m22 = 1.0f;
    self->m_state.m_transform.m33 = 1.0f;
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = false;
#endif /* XPAINTER_WORLD_MATRIX_ON */
#if XPAINTER_VIEW_TRANSFORM_ON
    painterResetViewTransform(self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
}

void XPainter_translate(XPainter* self, float dx, float dy)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterTranslation(dx, dy);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
}

void XPainter_scale(XPainter* self, float sx, float sy)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterScale(sx, sy);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
}

void XPainter_rotate(XPainter* self, float degrees)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterRotation(degrees);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
}

void XPainter_shear(XPainter* self, float sh, float sv)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterShear(sh, sv);
    self->m_state.m_transform =
        painterMatrixMultiply(&self->m_state.m_transform, &t);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
}

void XPainter_setWorldTransform(XPainter* self, const XImageTransform* matrix,
                                bool combine)
{
    XPainter_setTransform(self, matrix, combine);
}

void XPainter_worldTransform(const XPainter* self, XImageTransform* out)
{
    XPainter_transform(self, out);
}

#if XPAINTER_WORLD_MATRIX_ON
void XPainter_setWorldMatrixEnabled(XPainter* self, bool enabled)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    self->m_state.m_worldMatrixEnabled = enabled;
}

bool XPainter_worldMatrixEnabled(const XPainter* self)
{
    return self && self->m_deviceKind != XPainterDevice_None &&
           self->m_state.m_worldMatrixEnabled;
}
#endif /* XPAINTER_WORLD_MATRIX_ON */

void XPainter_combinedTransform(const XPainter* self, XImageTransform* out)
{
    if (!out)
        return;
    if (!self || self->m_deviceKind == XPainterDevice_None ||
        !painterCombinedTransform(&self->m_state, out))
        *out = g_painterIdentityTransform;
}

#if XPAINTER_VIEW_TRANSFORM_ON
void XPainter_setWindow(XPainter* self, const XRect* window)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !window)
        return;
    self->m_state.m_window = *window;
    self->m_state.m_viewTransformEnabled = true;
}

void XPainter_window(const XPainter* self, XRect* out)
{
    if (!out)
        return;
    if (!self || self->m_deviceKind == XPainterDevice_None)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = self->m_state.m_window;
}

void XPainter_setViewport(XPainter* self, const XRect* viewport)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !viewport)
        return;
    self->m_state.m_viewport = *viewport;
    self->m_state.m_viewTransformEnabled = true;
}

void XPainter_viewport(const XPainter* self, XRect* out)
{
    if (!out)
        return;
    if (!self || self->m_deviceKind == XPainterDevice_None)
    {
        memset(out, 0, sizeof(*out));
        return;
    }
    *out = self->m_state.m_viewport;
}

void XPainter_setViewTransformEnabled(XPainter* self, bool enabled)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    self->m_state.m_viewTransformEnabled = enabled;
}

bool XPainter_viewTransformEnabled(const XPainter* self)
{
    return self && self->m_deviceKind != XPainterDevice_None &&
           self->m_state.m_viewTransformEnabled;
}
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

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

#if XPAINTER_RENDERHINT_ON
void XPainter_setRenderHint(XPainter* self, XPainterRenderHint hint,
                            bool enabled)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    if (enabled)
        self->m_state.m_renderHints |= (XPainterRenderHints)hint;
    else
        self->m_state.m_renderHints &= ~((XPainterRenderHints)hint);
}

void XPainter_setRenderHints(XPainter* self, XPainterRenderHints hints,
                             bool enabled)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    if (enabled)
        self->m_state.m_renderHints |= hints;
    else
        self->m_state.m_renderHints &= ~hints;
}

XPainterRenderHints XPainter_renderHints(const XPainter* self)
{
    return (self && self->m_deviceKind != XPainterDevice_None)
               ? self->m_state.m_renderHints
               : 0u;
}

bool XPainter_testRenderHint(const XPainter* self, XPainterRenderHint hint)
{
    XPainterRenderHints mask = (XPainterRenderHints)hint;
    return self && self->m_deviceKind != XPainterDevice_None && mask != 0u &&
           (self->m_state.m_renderHints & mask) == mask;
}
#endif /* XPAINTER_RENDERHINT_ON */

#if XPAINTER_LAYOUT_DIRECTION_ON
void XPainter_setLayoutDirection(XPainter* self,
                                 XPainterLayoutDirection direction)
{
    if (!self)
        return;
    if (direction < XPainterLayoutDirection_LeftToRight ||
        direction > XPainterLayoutDirection_Auto)
        direction = XPainterLayoutDirection_Auto;
    self->m_state.m_layoutDirection = direction;
}

XPainterLayoutDirection XPainter_layoutDirection(const XPainter* self)
{
    return self ? self->m_state.m_layoutDirection
                : XPainterLayoutDirection_Auto;
}
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

#if XPAINTER_TEXTLAYOUT_ON
/** @brief 单行文本布局范围（drawTextRect 内部使用）。 */
typedef struct PainterTextLine
{
    const char* m_start;   /**< 行首字节。 */
    const char* m_end;     /**< 行尾字节（排他）。 */
    int m_charCount;       /**< 该行字形数。 */
} PainterTextLine;

/** @brief 统计 [start,end) 内 UTF-8 字形数量。 */
static int painterCountChars(const char* start, const char* end)
{
    int count = 0;
    const char* p = start;
    while (p < end)
    {
        uint32_t cp = painter8x16DecodeNext(&p);
        if (cp == 0u)
            break;
        ++count;
    }
    return count;
}

/** @brief 取助记键处理模式（0 不处理，1 显示下划线，2 仅隐藏）。 */
static int painterMnemonicMode(uint32_t flags)
{
    if ((flags & XPAINTER_TEXT_SHOW_MNEMONIC) != 0u)
        return 1;
    if ((flags & XPAINTER_TEXT_HIDE_MNEMONIC) != 0u)
        return 2;
    return 0;
}

#if XPAINTER_LAYOUT_DIRECTION_ON
/** @brief 判断码点是否属于常见强 RTL 文字区间。 */
static bool painterCodepointIsRTL(uint32_t cp)
{
    return (cp >= 0x0590u && cp <= 0x08ffu) ||
           (cp >= 0xfb1du && cp <= 0xfdffu) ||
           (cp >= 0xfe70u && cp <= 0xfeffu) ||
           (cp >= 0x10800u && cp <= 0x10fffu);
}

/** @brief 在 UTF-8 文本中寻找首个强 RTL 码点。 */
static bool painterTextHasRTL(const char* utf8)
{
    const char* p = utf8;
    if (!p)
        return false;
    while (*p != '\0')
    {
        uint32_t cp = painter8x16DecodeNext(&p);
        if (cp == 0u)
            break;
        if (painterCodepointIsRTL(cp))
            return true;
    }
    return false;
}
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

/**
 * @brief 计算一行点阵文本的像素宽度（含制表符展开与助记键配对）。
 * @note  mnemonicMode 为 0 时按字面字符计算；1/2 时 '&x'/'&&' 仅占一个字形位。
 */
static int painterTextLinePixelWidth(const char* start, const char* end,
                                     int charW, bool expandTabs,
                                     int mnemonicMode)
{
    int col = 0;
    int width = 0;
    const char* p = start;
    while (p < end)
    {
        const char* next = p;
        uint32_t cp = painter8x16DecodeNext(&next);
        if (cp == 0u)
            break;
        if (mnemonicMode != 0 && cp == '&')
        {
            const char* following = next;
            uint32_t fc = (following < end)
                              ? painter8x16DecodeNext(&following)
                              : 0u;
            ++col;
            width += charW;
            if (fc != 0u)
                p = following;   /* 转义对：&x 或 && 只占一个字形位 */
            else
                p = next;        /* 行尾孤立 & 按字面一个字形 */
            continue;
        }
        if (expandTabs && cp == '\t')
        {
            int adv = 8 - (col % 8);
            col += adv;
            width += adv * charW;
        }
        else
        {
            ++col;
            width += charW;
        }
        p = next;
    }
    return width;
}

/** @brief 绘制一个字形（control 仅占位不画），underline 时画下划线。 */
static void painterDrawCodepoint(XPainter* self,
                                 const PainterBitmapFontTable* table,
                                 int scale, int x, int baselineY,
                                 uint32_t cp, uint32_t color,
                                 bool underline)
{
    unsigned char glyphData[XFONT8X16_HEIGHT];
    if (cp < 0x20u)
        return;
    table->m_loadGlyph(cp, glyphData);
    painter8x16DrawGlyphScaled(self, x, baselineY, color, glyphData, scale);
    if (underline)
    {
        XRect r;
        r.x = x;
        r.y = baselineY + XFONT8X16_DESCENT * scale;
        r.width = table->m_width * scale;
        r.height = scale;
        XPainter_fillRect(self, &r, color);
    }
}

/** @brief 用当前字体绘制一段 UTF-8 运行段（仅单行，不做换行）。 */
static void painterDrawTextRun(XPainter* self,
                               const PainterBitmapFontTable* table, int scale,
                               int x, int baselineY,
                               const char* start, const char* end,
                               uint32_t color,
                               bool expandTabs, int justifyExtra,
                               int mnemonicMode)
{
    const char* p = start;
    int col = 0;
    int spaces = 0;
    int spaceBonus = 0;
    int spaceRem = 0;
    int spaceDrawn = 0;

    if (justifyExtra > 0)
    {
        const char* q = start;
        while (q < end)
        {
            const char* nq = q;
            uint32_t cq = painter8x16DecodeNext(&nq);
            if (cq == 0u)
                break;
            if (cq == ' ')
                ++spaces;
            q = nq;
        }
        if (spaces > 0)
        {
            spaceBonus = justifyExtra / spaces;
            spaceRem = justifyExtra % spaces;
        }
    }

    while (p < end)
    {
        const char* next = p;
        uint32_t cp = painter8x16DecodeNext(&next);
        if (cp == 0u)
            break;

        if (mnemonicMode != 0 && cp == '&')
        {
            const char* following = next;
            uint32_t fc = (following < end)
                              ? painter8x16DecodeNext(&following)
                              : 0u;
            if (fc != 0u)
            {
                if (fc == '&')
                {
                    painterDrawCodepoint(self, table, scale, x, baselineY,
                                         '&', color, false);
                    x += table->m_width * scale;
                    ++col;
                    p = following;
                    continue;
                }
                painterDrawCodepoint(self, table, scale, x, baselineY,
                                     fc, color, mnemonicMode == 1);
                x += table->m_width * scale;
                ++col;
                p = following;
                continue;
            }
        }

        if (expandTabs && cp == '\t')
        {
            int adv = 8 - (col % 8);
            x += adv * table->m_width * scale;
            col += adv;
            p = next;
            continue;
        }
        if (cp == ' ' && justifyExtra > 0 && spaces > 0)
        {
            x += spaceBonus + (spaceDrawn < spaceRem ? 1 : 0);
            ++spaceDrawn;
        }
        painterDrawCodepoint(self, table, scale, x, baselineY,
                             cp, color, false);
        x += table->m_width * scale;
        ++col;
        p = next;
    }
}

bool XPainter_drawTextRect(XPainter* self, const XRect* rect, uint32_t flags,
                           const char* utf8, uint32_t color)
{
    const PainterBitmapFontTable* table;
    int scale;
    int charW;
    int maxChars;
    int lineCap;
    PainterTextLine lines[64];
    int lineCount = 0;
    const char* lineStart;
    const char* p;
    int curCount;
    int i;
    int lineH;
    int ascent;
    int totalH;
    int yTop;
    bool wrap;
    bool singleLine;
    bool expandTabs;
#if XPAINTER_CLIP_ON
    bool doClip;
    bool restoreClip;
#endif /* XPAINTER_CLIP_ON */
    bool isRTL;
    bool visualLeft;
    bool visualRight;
    int mnemonicMode;

    if (!self || self->m_deviceKind == XPainterDevice_None)
        return false;
    if (!rect || rect->width <= 0 || rect->height <= 0)
        return true;
    if (!utf8 || utf8[0] == '\0')
        return true;
    if ((flags & XPAINTER_TEXT_DONT_PRINT) != 0u)
        return true;

    table = painterBitmapFont(&self->m_state.m_font);
    scale = painterBitmapScaleForFont(&self->m_state.m_font);
    charW = table->m_width * scale;
    maxChars = rect->width / charW;
    if (maxChars < 1) maxChars = 1;
    lineCap = (int)(sizeof(lines) / sizeof(lines[0]));
    wrap = (flags & XPAINTER_TEXT_WORD_WRAP) != 0u;
    singleLine = (flags & XPAINTER_TEXT_SINGLE_LINE) != 0u;
    isRTL = false;
    if ((flags & XPAINTER_TEXT_FORCE_RTL) != 0u)
        isRTL = true;
    else if ((flags & XPAINTER_TEXT_FORCE_LTR) == 0u)
    {
#if XPAINTER_LAYOUT_DIRECTION_ON
        if (self->m_state.m_layoutDirection ==
            XPainterLayoutDirection_RightToLeft)
            isRTL = true;
        else if (self->m_state.m_layoutDirection ==
                 XPainterLayoutDirection_Auto)
            isRTL = painterTextHasRTL(utf8);
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
    }
    visualLeft = (flags & XPAINTER_TEXT_ALIGN_LEFT) != 0u;
    visualRight = (flags & XPAINTER_TEXT_ALIGN_RIGHT) != 0u;
    if (!visualLeft && !visualRight &&
        (flags & XPAINTER_TEXT_ALIGN_HCENTER) == 0u)
        visualLeft = true; /* Qt visualAlignment 默认补 AlignLeft。 */
    if (isRTL && (flags & XPAINTER_TEXT_ALIGN_ABSOLUTE) == 0u)
    {
        bool alignSwap = visualLeft;
        visualLeft = visualRight;
        visualRight = alignSwap;
    }
    expandTabs = (flags & XPAINTER_TEXT_EXPAND_TABS) != 0u;
    expandTabs = expandTabs &&
                 ((visualLeft && !isRTL) || (visualRight && isRTL));
    mnemonicMode = painterMnemonicMode(flags);

    /* 默认裁剪到布局矩形（TextDontClip 除外）；内部用 save/restore 恢复状态。 */
#if XPAINTER_CLIP_ON
    doClip = (flags & XPAINTER_TEXT_DONT_CLIP) == 0u;
    restoreClip = false;
    if (doClip && XPainter_save(self))
    {
        XPainter_setClipRect(self, rect, XPainterClipOperation_IntersectClip);
        restoreClip = true;
    }
#endif /* XPAINTER_CLIP_ON */

    lineStart = utf8;
    p = utf8;
    curCount = 0;
    while (*p != '\0' && lineCount < lineCap)
    {
        const char* charStart = p;
        uint32_t cp = painter8x16DecodeNext(&p);
        if (cp == 0u)
            break;
        if (cp == '\n')
        {
            if (singleLine)
            {
                /* TextSingleLine：换行当作空格处理，不拆分。 */
                ++curCount;
                continue;
            }
            lines[lineCount].m_start = lineStart;
            lines[lineCount].m_end = charStart;
            lines[lineCount].m_charCount = curCount;
            ++lineCount;
            lineStart = p;
            curCount = 0;
            continue;
        }
        ++curCount;
        if (curCount >= maxChars && !singleLine)
        {
            if (wrap)
            {
                const char* q = lineStart;
                const char* spaceEnd = NULL;
                while (q < p)
                {
                    const char* cs = q;
                    uint32_t cc = painter8x16DecodeNext(&q);
                    if (cc == ' ') spaceEnd = q;
                }
                if (spaceEnd)
                {
                    lines[lineCount].m_start = lineStart;
                    lines[lineCount].m_end = spaceEnd;
                    lines[lineCount].m_charCount =
                        painterCountChars(lineStart, spaceEnd);
                    ++lineCount;
                    lineStart = spaceEnd;
                    curCount = painterCountChars(lineStart, p);
                    continue;
                }
            }
            lines[lineCount].m_start = lineStart;
            lines[lineCount].m_end = p;
            lines[lineCount].m_charCount = curCount;
            ++lineCount;
            lineStart = p;
            curCount = 0;
        }
    }
    if (lineStart < p && lineCount < lineCap)
    {
        lines[lineCount].m_start = lineStart;
        lines[lineCount].m_end = p;
        lines[lineCount].m_charCount = curCount;
        ++lineCount;
    }
    if (lineCount == 0)
    {
#if XPAINTER_CLIP_ON
        if (restoreClip)
            XPainter_restore(self);
#endif /* XPAINTER_CLIP_ON */
        return true;
    }

    lineH = table->m_height * scale;
    ascent = table->m_ascent * scale;
    totalH = lineCount * lineH;

    if ((flags & XPAINTER_TEXT_ALIGN_VCENTER) != 0u)
        yTop = rect->y + (rect->height - totalH) / 2;
    else if ((flags & XPAINTER_TEXT_ALIGN_BOTTOM) != 0u)
        yTop = rect->y + rect->height - totalH;
    else
        yTop = rect->y; /* 默认顶对齐 */

    for (i = 0; i < lineCount; ++i)
    {
        int lineW = painterTextLinePixelWidth(lines[i].m_start,
                                              lines[i].m_end, charW,
                                              expandTabs, mnemonicMode);
        int justifyExtra = 0;
        int x = rect->x;
        int baselineY = yTop + ascent;

        if ((flags & XPAINTER_TEXT_ALIGN_JUSTIFY) != 0u &&
            (i < lineCount - 1 ||
             (flags & XPAINTER_TEXT_JUSTIFICATION_FORCED) != 0u) &&
            lineW < rect->width)
            justifyExtra = rect->width - lineW;

        if (visualRight)
            x = rect->x + rect->width - lineW;
        else if ((flags & XPAINTER_TEXT_ALIGN_HCENTER) != 0u)
            x = rect->x + (rect->width - lineW) / 2;

        painterDrawTextRun(self, table, scale, x, baselineY,
                           lines[i].m_start, lines[i].m_end, color,
                           expandTabs, justifyExtra, mnemonicMode);
        yTop += lineH;
    }

#if XPAINTER_CLIP_ON
    if (restoreClip)
        XPainter_restore(self);
#endif /* XPAINTER_CLIP_ON */
    return true;
}
#endif /* XPAINTER_TEXTLAYOUT_ON */
