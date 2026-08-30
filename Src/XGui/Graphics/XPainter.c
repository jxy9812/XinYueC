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
#if XPAINTER_PIXMAP_ON
#include "XPixmap.h"
#endif /* XPAINTER_PIXMAP_ON */
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#include <string.h>
#include <math.h>
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
    state->m_penCap = XPainterPenCapStyle_SquareCap;
    state->m_penJoin = XPainterPenJoinStyle_BevelJoin;
#endif
    state->m_penColor = 0xff000000u;   /* 默认黑色不透明画笔 */
    state->m_penWidth = 1;
    state->m_brushColor = 0xff000000u; /* 画刷颜色；默认样式为 NoBrush */
#if XPAINTER_BRUSH_ON
    /* QPainter/QBrush 默认构造结果是 NoBrush；颜色仍保留黑色，供
       setBrushStyle(SolidPattern) 后直接使用。 */
    state->m_brush.m_style = XPainterBrushStyle_NoBrush;
    state->m_brush.m_color = 0xff000000u;
#endif
#if XPAINTER_BRUSH_ORIGIN_ON
    state->m_brushOriginX = 0.0f;
    state->m_brushOriginY = 0.0f;
#endif
    state->m_backgroundColor = 0xffffffffu; /* 默认背景不透明白 */
#if XPAINTER_BACKGROUND_ON
    state->m_backgroundMode = XPainterBackgroundMode_Transparent;
#if XPAINTER_BRUSH_ON
    /* QPainter::QPainterState::init() 默认背景画刷为不透明白色实心。 */
    state->m_backgroundBrush.m_style = XPainterBrushStyle_SolidPattern;
    state->m_backgroundBrush.m_color = 0xffffffffu;
#endif /* XPAINTER_BRUSH_ON */
#endif /* XPAINTER_BACKGROUND_ON */
#if XPAINTER_CLIP_ON
    state->m_hasClip = false;
    state->m_hasClipRect = false;
    state->m_clipRect = (XRect){ 0, 0, 0, 0 };
    state->m_clipOperation = XPainterClipOperation_NoClip;
#if XPAINTER_CLIP_REGION_ON
    XRegion_init(&state->m_clipRegion);
#endif /* XPAINTER_CLIP_REGION_ON */
#endif /* XPAINTER_CLIP_ON */
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
    /* QPainterState::init() takes the application's effective direction.  In
       Qt 6.8 the static default is LeftToRight even before a GUI application
       is constructed; map the project application state when available. */
#if XGUIAPPLICATION_ON
    state->m_layoutDirection =
        XGuiApplication_layoutDirection() == XGuiLayoutDirection_RightToLeft
            ? XPainterLayoutDirection_RightToLeft
            : XPainterLayoutDirection_LeftToRight;
#else
    state->m_layoutDirection = XPainterLayoutDirection_LeftToRight;
#endif /* XGUIAPPLICATION_ON */
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
 * @brief      矩阵级联（result = a * b，先应用 a、再应用 b）。
 * @param a 先作用于逻辑坐标的矩阵。
 * @param b 后作用于坐标的矩阵。
 * @return 级联结果；矩阵存储和映射顺序与 Qt QTransform 一致。
 */
static XImageTransform painterMatrixMultiply(const XImageTransform* a,
                                             const XImageTransform* b)
{
    XImageTransform r;
    /*
     * XImageTransform uses Qt's row-vector field layout:
     * [m11 m12 m13] [m21 m22 m23] [dx dy m33].
     * Keep this explicit instead of converting through a temporary matrix;
     * it is used by all embedded transform paths and must preserve the
     * exact QTransform::operator* ordering.
     */
    r.m11 = a->m11 * b->m11 + a->m12 * b->m21 + a->m13 * b->dx;
    r.m12 = a->m11 * b->m12 + a->m12 * b->m22 + a->m13 * b->dy;
    r.m13 = a->m11 * b->m13 + a->m12 * b->m23 + a->m13 * b->m33;
    r.m21 = a->m21 * b->m11 + a->m22 * b->m21 + a->m23 * b->dx;
    r.m22 = a->m21 * b->m12 + a->m22 * b->m22 + a->m23 * b->dy;
    r.m23 = a->m21 * b->m13 + a->m22 * b->m23 + a->m23 * b->m33;
    r.dx  = a->dx  * b->m11 + a->dy  * b->m21 + a->m33 * b->dx;
    r.dy  = a->dx  * b->m12 + a->dy  * b->m22 + a->m33 * b->dy;
    r.m33 = a->dx  * b->m13 + a->dy  * b->m23 + a->m33 * b->m33;
    return r;
}

#if XPAINTER_VIEW_TRANSFORM_ON
/**
 * @brief      构造保存状态中的 window/viewport 视图矩阵。
 * @details    逻辑坐标先经过世界矩阵，再经过此处返回的视图矩阵，等价于
 *             Qt 的 window/viewport 转换。
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
 * @details    世界矩阵关闭时先替换为单位矩阵；随后在视图变换启用时右乘
 *             window/viewport 矩阵，使逻辑坐标按“世界坐标后视图坐标”的
 *             顺序映射到设备坐标，等价于 Qt 的 worldMatrix * viewTransform。
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
    *out = painterMatrixMultiply(out, &view);
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
    *out = painterMatrixMultiply(out, &view);
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

/**
 * @brief 把浮点像素边界转换为首尾像素下标。
 * @details Qt 光栅填充按像素中心采样，边界交点落在像素中心时按覆盖像素
 *          处理；因此首个像素为 ceil(left - 0.5)，末个像素为
 *          floor(right - 0.5)。轴对齐整数矩形自然保持右侧排他。
 *          该辅助函数统一处理负坐标、非有限值和 int 边界，避免扫描线
 *          在整数右边界多填充一个像素。
 * @param left  覆盖区间左边界。
 * @param right 覆盖区间右边界（排他）。
 * @param outFirst 输出首个像素下标。
 * @param outLast 输出末个像素下标。
 * @return 区间有效且至少覆盖一个像素时返回 true。
 */
static bool painterSpanPixelRange(float left, float right,
                                  int* outFirst, int* outLast)
{
    float first;
    float last;
    if (!outFirst || !outLast || !isfinite(left) || !isfinite(right) ||
        !(right > left))
        return false;
    first = ceilf(left - 0.5f);
    last = floorf(right - 0.5f);
    if (!isfinite(first) || !isfinite(last) || first > last)
        return false;
    if (first > (float)INT_MAX) *outFirst = INT_MAX;
    else if (first < (float)INT_MIN) *outFirst = INT_MIN;
    else *outFirst = (int)first;
    if (last > (float)INT_MAX) *outLast = INT_MAX;
    else if (last < (float)INT_MIN) *outLast = INT_MIN;
    else *outLast = (int)last;
    return *outFirst <= *outLast;
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
    float xs[4];
    float ys[4];
    bool first = true;
    int i;
    if (!matrix || !rect) return false;
    xs[0] = (float)rect->x;
    xs[1] = (float)rect->x + (float)rect->width;
    xs[2] = (float)rect->x;
    xs[3] = (float)rect->x + (float)rect->width;
    ys[0] = (float)rect->y;
    ys[1] = (float)rect->y;
    ys[2] = (float)rect->y + (float)rect->height;
    ys[3] = (float)rect->y + (float)rect->height;
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
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
        XRegion_init(&saved->m_clipRegion);
        XRegion_copy(&self->m_state.m_clipRegion, &saved->m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
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
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
        XRegion regionCopy;
        XRegion_init(&regionCopy);
        XRegion_copy(&self->m_stateStack[self->m_stateCount].m_clipRegion,
                     &regionCopy);
        XRegion_deinit(&self->m_state.m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
        XFont_copy(&fontCopy, &self->m_stateStack[self->m_stateCount].m_font);
        self->m_state = self->m_stateStack[self->m_stateCount];
        self->m_state.m_font = fontCopy;
        XFont_deinit(&self->m_stateStack[self->m_stateCount].m_font);
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
        self->m_state.m_clipRegion = regionCopy;
        XRegion_deinit(&self->m_stateStack[self->m_stateCount].m_clipRegion);
        XRegion_init(&self->m_stateStack[self->m_stateCount].m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
    }
}

/* ========== 软件光栅后端 ========== */

/** @brief 8 位分量相乘并按 255 四舍五入。 */
static unsigned painterMul255(unsigned a, unsigned b)
{
    return (a * b + 127u) / 255u;
}

/** @brief 把预乘分量还原为普通 8 位颜色分量。 */
static unsigned painterUnpremultiply(unsigned value, unsigned alpha)
{
    unsigned result;
    if (alpha == 0u) return 0u;
    result = (value * 255u + alpha / 2u) / alpha;
    return result > 255u ? 255u : result;
}

/** @brief 计算 SVG 1.2 混合模式的单通道 B(Cs,Cd) 值。 */
static unsigned painterBlendChannel(unsigned source, unsigned destination,
                                    XPainterCompositionMode mode)
{
    unsigned value;
    switch (mode)
    {
        case XPainterCompositionMode_Multiply:
            return painterMul255(source, destination);
        case XPainterCompositionMode_Screen:
            return 255u - painterMul255(255u - source, 255u - destination);
        case XPainterCompositionMode_Overlay:
            return destination < 128u
                ? painterMul255(source, destination * 2u)
                : 255u - painterMul255(255u - source, (255u - destination) * 2u);
        case XPainterCompositionMode_Darken:
            return source < destination ? source : destination;
        case XPainterCompositionMode_Lighten:
            return source > destination ? source : destination;
        case XPainterCompositionMode_ColorDodge:
            if (source >= 255u) return 255u;
            value = destination * 255u / (255u - source);
            return value > 255u ? 255u : value;
        case XPainterCompositionMode_ColorBurn:
            if (source == 0u) return 0u;
            value = (255u - destination) * 255u / source;
            return value >= 255u ? 0u : 255u - value;
        case XPainterCompositionMode_HardLight:
            return source < 128u
                ? painterMul255(source * 2u, destination)
                : 255u - painterMul255((255u - source) * 2u,
                                       255u - destination);
        case XPainterCompositionMode_SoftLight:
        {
            float s = (float)source / 255.0f;
            float d = (float)destination / 255.0f;
            float result;
            if (s <= 0.5f)
                result = d - (1.0f - 2.0f * s) * d * (1.0f - d);
            else
            {
                float g = d <= 0.25f
                    ? ((16.0f * d - 12.0f) * d + 4.0f) * d
                    : (float)sqrt(d);
                result = d + (2.0f * s - 1.0f) * (g - d);
            }
            value = (unsigned)(result * 255.0f + 0.5f);
            return value > 255u ? 255u : value;
        }
        case XPainterCompositionMode_Difference:
            return source > destination ? source - destination : destination - source;
        case XPainterCompositionMode_Exclusion:
            value = source + destination - 2u * painterMul255(source, destination);
            return value > 255u ? 255u : value;
        default:
            return source;
    }
}

/**
 * @brief 按 Qt QPainter::CompositionMode 合成一个 ARGB32 像素。
 * @details 输入是 XImage 的非预乘 ARGB；内部转为预乘分量，按
 *          Porter-Duff 因子或 SVG 混合函数计算，再还原为普通颜色。
 */
static uint32_t painterComposeColor(uint32_t source, uint32_t destination,
                                    XPainterCompositionMode mode)
{
    unsigned sa = (source >> 24) & 255u;
    unsigned sr = (source >> 16) & 255u;
    unsigned sg = (source >> 8) & 255u;
    unsigned sb = source & 255u;
    unsigned da = (destination >> 24) & 255u;
    unsigned dr = (destination >> 16) & 255u;
    unsigned dg = (destination >> 8) & 255u;
    unsigned db = destination & 255u;
    unsigned outA;
    unsigned outR;
    unsigned outG;
    unsigned outB;
    unsigned spR = painterMul255(sr, sa);
    unsigned spG = painterMul255(sg, sa);
    unsigned spB = painterMul255(sb, sa);
    unsigned dpR = painterMul255(dr, da);
    unsigned dpG = painterMul255(dg, da);
    unsigned dpB = painterMul255(db, da);
    unsigned factorSource;
    unsigned factorDestination;

    /* Qt RasterOp 模式直接对完整 ARGB32 位模式运算，不经过
       Porter-Duff 的预乘/反预乘流程。 */
    switch (mode)
    {
        case XPainterCompositionMode_RasterOp_SourceOrDestination:
            return source | destination;
        case XPainterCompositionMode_RasterOp_SourceAndDestination:
            return source & destination;
        case XPainterCompositionMode_RasterOp_SourceXorDestination:
            return source ^ destination;
        case XPainterCompositionMode_RasterOp_NotSourceAndNotDestination:
            return (uint32_t)(~source & ~destination);
        case XPainterCompositionMode_RasterOp_NotSourceOrNotDestination:
            return (uint32_t)(~source | ~destination);
        case XPainterCompositionMode_RasterOp_NotSourceXorDestination:
            /* QPainter 文档定义为 ((NOT source) XOR destination)。 */
            return (uint32_t)((~source) ^ destination);
        case XPainterCompositionMode_RasterOp_NotSource:
            return (uint32_t)(~source);
        case XPainterCompositionMode_RasterOp_NotSourceAndDestination:
            return (uint32_t)(~source & destination);
        case XPainterCompositionMode_RasterOp_SourceAndNotDestination:
            return (uint32_t)(source & ~destination);
        case XPainterCompositionMode_RasterOp_NotSourceOrDestination:
            return (uint32_t)(~source | destination);
        case XPainterCompositionMode_RasterOp_SourceOrNotDestination:
            return (uint32_t)(source | ~destination);
        case XPainterCompositionMode_RasterOp_ClearDestination:
            return 0u;
        case XPainterCompositionMode_RasterOp_SetDestination:
            return 0xffffffffu;
        case XPainterCompositionMode_RasterOp_NotDestination:
            return (uint32_t)(~destination);
        default:
            break;
    }

    /* 这三个模式在 Qt 光栅引擎中是逐像素的直接结果，不应因预乘与
       反预乘往返而改变半透明颜色。 */
    if (mode == XPainterCompositionMode_Clear)
        return 0u;
    if (mode == XPainterCompositionMode_Source)
        return source;
    if (mode == XPainterCompositionMode_Destination)
        return destination;
    if (mode == XPainterCompositionMode_SourceOver && sa == 0u)
        return destination;
    if (mode == XPainterCompositionMode_SourceOver && da == 0u)
        return source;

    if (mode >= XPainterCompositionMode_Multiply &&
        mode <= XPainterCompositionMode_Exclusion)
    {
        unsigned common = painterMul255(sa, da);
        unsigned blendR = painterBlendChannel(sr, dr, mode);
        unsigned blendG = painterBlendChannel(sg, dg, mode);
        unsigned blendB = painterBlendChannel(sb, db, mode);
        unsigned outPR = painterMul255(spR, 255u - da) +
                         painterMul255(dpR, 255u - sa) +
                         painterMul255(common, blendR);
        unsigned outPG = painterMul255(spG, 255u - da) +
                         painterMul255(dpG, 255u - sa) +
                         painterMul255(common, blendG);
        unsigned outPB = painterMul255(spB, 255u - da) +
                         painterMul255(dpB, 255u - sa) +
                         painterMul255(common, blendB);
        outA = sa + painterMul255(da, 255u - sa);
        outR = painterUnpremultiply(outPR, outA);
        outG = painterUnpremultiply(outPG, outA);
        outB = painterUnpremultiply(outPB, outA);
        return (outA << 24) | (outR << 16) | (outG << 8) | outB;
    }

    if (mode == XPainterCompositionMode_Plus)
    {
        outA = sa + da;
        outA = outA > 255u ? 255u : outA;
        outR = spR + dpR; outR = outR > 255u ? 255u : outR;
        outG = spG + dpG; outG = outG > 255u ? 255u : outG;
        outB = spB + dpB; outB = outB > 255u ? 255u : outB;
        outR = painterUnpremultiply(outR, outA);
        outG = painterUnpremultiply(outG, outA);
        outB = painterUnpremultiply(outB, outA);
        return (outA << 24) | (outR << 16) | (outG << 8) | outB;
    }

    switch (mode)
    {
        case XPainterCompositionMode_DestinationOver:
            factorSource = 255u - da; factorDestination = 255u; break;
        case XPainterCompositionMode_Clear:
            factorSource = 0u; factorDestination = 0u; break;
        case XPainterCompositionMode_Source:
            factorSource = 255u; factorDestination = 0u; break;
        case XPainterCompositionMode_Destination:
            factorSource = 0u; factorDestination = 255u; break;
        case XPainterCompositionMode_SourceIn:
            factorSource = da; factorDestination = 0u; break;
        case XPainterCompositionMode_DestinationIn:
            factorSource = 0u; factorDestination = sa; break;
        case XPainterCompositionMode_SourceOut:
            factorSource = 255u - da; factorDestination = 0u; break;
        case XPainterCompositionMode_DestinationOut:
            factorSource = 0u; factorDestination = 255u - sa; break;
        case XPainterCompositionMode_SourceAtop:
            factorSource = da; factorDestination = 255u - sa; break;
        case XPainterCompositionMode_DestinationAtop:
            factorSource = 255u - da; factorDestination = sa; break;
        case XPainterCompositionMode_Xor:
            factorSource = 255u - da; factorDestination = 255u - sa; break;
        case XPainterCompositionMode_SourceOver:
        default:
            factorSource = 255u; factorDestination = 255u - sa; break;
    }
    outA = painterMul255(sa, factorSource) + painterMul255(da, factorDestination);
    outR = painterMul255(spR, factorSource) + painterMul255(dpR, factorDestination);
    outG = painterMul255(spG, factorSource) + painterMul255(dpG, factorDestination);
    outB = painterMul255(spB, factorSource) + painterMul255(dpB, factorDestination);
    outR = painterUnpremultiply(outR, outA);
    outG = painterUnpremultiply(outG, outA);
    outB = painterUnpremultiply(outB, outA);
    return (outA << 24) | (outR << 16) | (outG << 8) | outB;
}

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
    if (!self || !self->m_image) return;
    state = &self->m_state;
#if XPAINTER_CLIP_ON
    if (state->m_hasClip)
    {
#if XPAINTER_CLIP_REGION_ON
        if (!XRegion_contains(&state->m_clipRegion, x, y))
            return;
#else
        if (state->m_clipRect.width <= 0 || state->m_clipRect.height <= 0 ||
            x < state->m_clipRect.x || y < state->m_clipRect.y ||
            x >= state->m_clipRect.x + state->m_clipRect.width ||
            y >= state->m_clipRect.y + state->m_clipRect.height)
            return;
#endif /* XPAINTER_CLIP_REGION_ON */
    }
#endif /* XPAINTER_CLIP_ON */
    if (!XImage_valid(self->m_image, x, y)) return;
    dst = XImage_pixel(self->m_image, x, y);
    XImage_setPixel(self->m_image, x, y,
                    painterComposeColor(color, dst, state->m_compositionMode));
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
 * @brief      绘制水平或垂直整数线的端点覆盖。
 * @details    Qt 光栅后端对整数轴向线按像素中心采样：FlatCap 不包含
 *             几何终点外侧像素，SquareCap 向两端延伸半个线宽，RoundCap
 *             在端点添加圆形覆盖。斜线继续使用通用 Bresenham 近似，轴向
 *             线在此单独处理可保证常用控件边框与 Qt 一致。
 * @param self 绘制器指针。
 * @param x1/y1 线段起点（设备坐标）。
 * @param x2/y2 线段终点（设备坐标）。
 * @param width 线宽（至少为 1）。
 * @param color ARGB32 颜色。
 * @return 始终返回 true；目标像素由 painterRaster_putPixel 做边界裁剪。
 */
static bool painterRaster_drawAxisLine(XPainter* self, int x1, int y1,
                                       int x2, int y2, int width,
                                       uint32_t color)
{
    int half;
    int start;
    int end;
    int a;
    int b;
    int crossStart;
    int crossEnd;
    int p;
    int q;
    if (!self) return false;
    if (width < 1) width = 1;
    half = width / 2;
    if (y1 == y2)
    {
        start = x1 < x2 ? x1 : x2;
        end = x1 > x2 ? x1 : x2;
#if XPAINTER_PENSTYLE_ON
        if (self->m_state.m_penCap == XPainterPenCapStyle_FlatCap)
            --end;
        else if (self->m_state.m_penCap == XPainterPenCapStyle_SquareCap)
        {
            start -= half;
            end += half;
        }
#endif /* XPAINTER_PENSTYLE_ON */
        crossStart = y1 - half;
        crossEnd = crossStart + width - 1;
#if XPAINTER_PENSTYLE_ON
        if (self->m_state.m_penCap == XPainterPenCapStyle_RoundCap)
        {
            /* 圆头分支单独栅格化有限线段，避免先画方形主体再重复
               写入同一像素；这对半透明 SourceOver 尤其重要。 */
            float radius = (float)width * 0.5f;
            int minX = (x1 < x2 ? x1 : x2) - half;
            int maxX = (x1 > x2 ? x1 : x2) + half;
            int minY = y1 - half;
            int maxY = y1 + half;
            for (p = minY; p <= maxY; ++p)
            {
                for (q = minX; q <= maxX; ++q)
                {
                    float px = (float)q + 0.5f;
                    float py = (float)p + 0.5f;
                    float nearest = px;
                    float dx;
                    float dy;
                    float distance;
                    if (nearest < (float)(x1 < x2 ? x1 : x2))
                        nearest = (float)(x1 < x2 ? x1 : x2);
                    if (nearest > (float)(x1 > x2 ? x1 : x2))
                        nearest = (float)(x1 > x2 ? x1 : x2);
                    dx = px - nearest;
                    dy = py - (float)y1;
                    distance = dx * dx + dy * dy;
                    if ((width == 1 && distance <= radius * radius + 1.0e-6f) ||
                        (width > 1 && distance < radius * radius))
                        painterRaster_putPixel(self, q, p, color);
                }
            }
            if (width == 1)
            {
                painterRaster_putPixel(self, x1, y1, color);
                painterRaster_putPixel(self, x2, y2, color);
            }
            return true;
        }
#endif /* XPAINTER_PENSTYLE_ON */
        for (p = crossStart; p <= crossEnd; ++p)
            for (q = start; q <= end; ++q)
                painterRaster_putPixel(self, q, p, color);
        return true;
    }
    /* 垂直线：交换 x/y 后复用完全相同的覆盖规则。 */
    if (x1 == x2)
    {
        start = y1 < y2 ? y1 : y2;
        end = y1 > y2 ? y1 : y2;
#if XPAINTER_PENSTYLE_ON
        if (self->m_state.m_penCap == XPainterPenCapStyle_FlatCap)
            --end;
        else if (self->m_state.m_penCap == XPainterPenCapStyle_SquareCap)
        {
            start -= half;
            end += half;
        }
#endif /* XPAINTER_PENSTYLE_ON */
        crossStart = x1 - half;
        crossEnd = crossStart + width - 1;
#if XPAINTER_PENSTYLE_ON
        if (self->m_state.m_penCap == XPainterPenCapStyle_RoundCap)
        {
            float radius = (float)width * 0.5f;
            int minY = (y1 < y2 ? y1 : y2) - half;
            int maxY = (y1 > y2 ? y1 : y2) + half;
            int minX = x1 - half;
            int maxX = x1 + half;
            for (p = minX; p <= maxX; ++p)
            {
                for (q = minY; q <= maxY; ++q)
                {
                    float py = (float)q + 0.5f;
                    float px = (float)p + 0.5f;
                    float nearest = py;
                    float dy;
                    float dx;
                    float distance;
                    if (nearest < (float)(y1 < y2 ? y1 : y2))
                        nearest = (float)(y1 < y2 ? y1 : y2);
                    if (nearest > (float)(y1 > y2 ? y1 : y2))
                        nearest = (float)(y1 > y2 ? y1 : y2);
                    dy = py - nearest;
                    dx = px - (float)x1;
                    distance = dx * dx + dy * dy;
                    if ((width == 1 && distance <= radius * radius + 1.0e-6f) ||
                        (width > 1 && distance < radius * radius))
                        painterRaster_putPixel(self, p, q, color);
                }
            }
            if (width == 1)
            {
                painterRaster_putPixel(self, x1, y1, color);
                painterRaster_putPixel(self, x2, y2, color);
            }
            return true;
        }
#endif /* XPAINTER_PENSTYLE_ON */
        for (p = crossStart; p <= crossEnd; ++p)
            for (q = start; q <= end; ++q)
                painterRaster_putPixel(self, p, q, color);
        return true;
    }
    return false;
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
    int64_t adx, ady;
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
        /* Qt 对 drawPoint 的 Flat/SquareCap 使用宽度方块，RoundCap
           使用圆形覆盖；宽度为 1 时三种样式都至少覆盖中心像素。 */
#if XPAINTER_PENSTYLE_ON
        if (self->m_state.m_penCap == XPainterPenCapStyle_RoundCap && width > 1)
        {
            float radius = (float)width * 0.5f;
            int j;
            int min = -(width / 2) - 1;
            int max = (width / 2) + 1;
            for (k = min; k <= max; ++k)
            {
                for (j = min; j <= max; ++j)
                {
                    float dxp = (float)j + 0.5f;
                    float dyp = (float)k + 0.5f;
                    if (dxp * dxp + dyp * dyp < radius * radius)
                        painterRaster_putPixel(self, ix1 + j, iy1 + k, color);
                }
            }
        }
        else
#endif /* XPAINTER_PENSTYLE_ON */
        {
            for (k = start; k < end; ++k)
            {
                int j;
                for (j = start; j < end; ++j)
                    painterRaster_putPixel(self, ix1 + j, iy1 + k, color);
            }
        }
        return true;
    }
    if (painterRaster_drawAxisLine(self, ix1, iy1, ix2, iy2, width, color))
        return true;
    adx = dx >= 0 ? (int64_t)dx : -(int64_t)dx;
    ady = dy >= 0 ? (int64_t)dy : -(int64_t)dy;
    if (adx >= ady)
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
            (state->m_compositionMode == XPainterCompositionMode_SourceOver &&
             ((effective >> 24) == 255u)))
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

#if XPAINTER_IMAGE_RECT_ON
/**
 * @brief      drawImage 矩形重载经过 Qt 规则处理后的浮点参数。
 * @note       QRect 重载先转换为 QRectF；目标负宽高表示按源尺寸绘制，
 *             源非正宽高表示取到图像边缘，源越界则按比例裁剪目标区域。
 */
typedef struct XPainterImageRectParams
{
    float m_targetX;
    float m_targetY;
    float m_targetWidth;
    float m_targetHeight;
    float m_sourceX;
    float m_sourceY;
    float m_sourceWidth;
    float m_sourceHeight;
} XPainterImageRectParams;

/**
 * @brief      按 QPainter::drawImage(QRectF, QImage, QRectF) 规则整理参数。
 * @param targetRect 目标矩形。
 * @param image 源图像。
 * @param sourceRect 源矩形。
 * @param out 输出有效目标/源参数。
 * @return 有可绘制区域返回 true；整理后为空返回 false。
 */
static bool painterPrepareImageRect(const XRect* targetRect,
                                    const XImage* image,
                                    const XRect* sourceRect,
                                    XPainterImageRectParams* out)
{
    float imageWidth;
    float imageHeight;
    float imageScale;
    if (!targetRect || !image || !sourceRect || !out) return false;
    imageWidth = (float)XImage_width(image);
    imageHeight = (float)XImage_height(image);
    imageScale = XImage_devicePixelRatio(image);
    if (!(imageScale > 0.0f) || !isfinite(imageScale)) imageScale = 1.0f;
    out->m_targetX = (float)targetRect->x;
    out->m_targetY = (float)targetRect->y;
    out->m_targetWidth = (float)targetRect->width;
    out->m_targetHeight = (float)targetRect->height;
    out->m_sourceX = (float)sourceRect->x;
    out->m_sourceY = (float)sourceRect->y;
    out->m_sourceWidth = (float)sourceRect->width;
    out->m_sourceHeight = (float)sourceRect->height;

    /* Qt treats a non-positive source size as "to the image edge". */
    if (out->m_sourceWidth <= 0.0f)
        out->m_sourceWidth = imageWidth - out->m_sourceX;
    if (out->m_sourceHeight <= 0.0f)
        out->m_sourceHeight = imageHeight - out->m_sourceY;

    /* A negative target size requests the source extent at image scale. */
    if (out->m_targetWidth < 0.0f)
        out->m_targetWidth = out->m_sourceWidth / imageScale;
    if (out->m_targetHeight < 0.0f)
        out->m_targetHeight = out->m_sourceHeight / imageScale;

    /* Clip source on the left/top while preserving the source-to-target ratio. */
    if (out->m_sourceX < 0.0f)
    {
        float ratio = out->m_sourceX * out->m_targetWidth /
                      out->m_sourceWidth;
        out->m_targetX -= ratio;
        out->m_targetWidth += ratio;
        out->m_sourceWidth += out->m_sourceX;
        out->m_sourceX = 0.0f;
    }
    if (out->m_sourceY < 0.0f)
    {
        float ratio = out->m_sourceY * out->m_targetHeight /
                      out->m_sourceHeight;
        out->m_targetY -= ratio;
        out->m_targetHeight += ratio;
        out->m_sourceHeight += out->m_sourceY;
        out->m_sourceY = 0.0f;
    }

    /* Clip source on the right/bottom with the same proportional adjustment. */
    if (out->m_sourceX + out->m_sourceWidth > imageWidth)
    {
        float delta = out->m_sourceWidth - (imageWidth - out->m_sourceX);
        float ratio = delta * out->m_targetWidth / out->m_sourceWidth;
        out->m_sourceWidth -= delta;
        out->m_targetWidth -= ratio;
    }
    if (out->m_sourceY + out->m_sourceHeight > imageHeight)
    {
        float delta = out->m_sourceHeight - (imageHeight - out->m_sourceY);
        float ratio = delta * out->m_targetHeight / out->m_sourceHeight;
        out->m_sourceHeight -= delta;
        out->m_targetHeight -= ratio;
    }
    if (out->m_targetWidth == 0.0f || out->m_targetHeight == 0.0f ||
        out->m_sourceWidth <= 0.0f || out->m_sourceHeight <= 0.0f)
        return false;
    return isfinite(out->m_targetX) && isfinite(out->m_targetY) &&
           isfinite(out->m_targetWidth) && isfinite(out->m_targetHeight) &&
           isfinite(out->m_sourceX) && isfinite(out->m_sourceY) &&
           isfinite(out->m_sourceWidth) && isfinite(out->m_sourceHeight);
}

/** @brief 把浮点目标矩形四个角映射到设备空间并求包围盒。 */
static bool painterMapImageRectCorners(const XImageTransform* matrix,
                                       const XPainterImageRectParams* params,
                                       float* minX, float* minY,
                                       float* maxX, float* maxY)
{
    float xs[4];
    float ys[4];
    bool first = true;
    int i;
    if (!matrix || !params) return false;
    xs[0] = params->m_targetX;
    xs[1] = params->m_targetX + params->m_targetWidth;
    xs[2] = params->m_targetX;
    xs[3] = params->m_targetX + params->m_targetWidth;
    ys[0] = params->m_targetY;
    ys[1] = params->m_targetY;
    ys[2] = params->m_targetY + params->m_targetHeight;
    ys[3] = params->m_targetY + params->m_targetHeight;
    for (i = 0; i < 4; ++i)
    {
        float fx;
        float fy;
        if (!painterMapPoint(matrix, xs[i], ys[i], &fx, &fy)) return false;
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

/**
 * @brief 软件光栅绘制图像目标/源矩形重载。
 * @details 目标矩形先经过当前有效变换映射到设备空间，再对每个目标
 *          像素逆变换回用户坐标并在源矩形内做最近邻采样。这样与
 *          painterRaster_drawImage 共用裁剪、透明度和合成路径，也能处理
 *          旋转、切变及透视矩阵；源矩形越界部分已在调用前按 Qt
 *          drawImage 比例裁剪，不会写入被裁掉的目标区域。
 */
static bool painterRaster_drawImageRect(XPainter* self,
                                        const XPainterImageRectParams* params,
                                        const XImage* image)
{
    XImageTransform transform;
    XImageTransform inverse;
    float minX, minY, maxX, maxY;
    int px0, py0, px1, py1;
    uint8_t opacity;
    if (!self || !self->m_image || !params || !image)
        return false;
    if (!painterEffectiveTransform(&self->m_state, &transform) ||
        !painterMapImageRectCorners(&transform, params,
                               &minX, &minY, &maxX, &maxY) ||
        !painterMatrixInvert(&transform, &inverse))
        return false;
    px0 = painterFloorClamp(minX, XImage_width(self->m_image) - 1);
    py0 = painterFloorClamp(minY, XImage_height(self->m_image) - 1);
    px1 = painterCeilClamp(maxX, XImage_width(self->m_image));
    py1 = painterCeilClamp(maxY, XImage_height(self->m_image));
    opacity = painterOpacityByte(self->m_state.m_opacity);
    {
        int py;
        for (py = py0; py < py1; ++py)
        {
            int px;
            for (px = px0; px < px1; ++px)
            {
                float ux, uy;
                float tx, ty;
                int sx, sy;
                if (!painterMapPoint(&inverse, (float)px + 0.5f,
                                     (float)py + 0.5f, &ux, &uy))
                    continue;
                tx = (ux - params->m_targetX) / params->m_targetWidth;
                ty = (uy - params->m_targetY) / params->m_targetHeight;
                if (tx < 0.0f || tx >= 1.0f || ty < 0.0f || ty >= 1.0f)
                    continue;
                /* Qt raster sampling floors source coordinates.  A C cast
                   would truncate -0.5 toward zero and incorrectly sample
                   pixel 0 for a source rectangle extending left/up. */
                sx = (int)floorf(params->m_sourceX +
                                 tx * params->m_sourceWidth);
                sy = (int)floorf(params->m_sourceY +
                                 ty * params->m_sourceHeight);
                {
                    uint32_t sourcePixel = 0u;
                    if (sx >= 0 && sy >= 0 &&
                        sx < XImage_width(image) &&
                        sy < XImage_height(image))
                        sourcePixel = XImage_pixel(image, sx, sy);
                painterRaster_putPixel(self, px, py,
                    painterApplyOpacityByte(sourcePixel,
                                            opacity));
                }
            }
        }
    }
    return true;
}
#endif /* XPAINTER_IMAGE_RECT_ON */

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

#if XPAINTER_SHAPE_ON
static bool painterRecord_drawShape(XPainter* self, XPainterShapeOp op,
                                    const XRect* rect, int startAngle,
                                    int spanAngle, bool filled,
                                    int xRadius, int yRadius)
{
    return self && self->m_picture && rect &&
           XPicture_recordDrawShape(self->m_picture, (int)op, rect,
                                    startAngle, spanAngle, filled,
                                    xRadius, yRadius);
}
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
static bool painterRecord_drawPolyline(XPainter* self,
                                       const XPoint* points, int count)
{
    return self && self->m_picture && points &&
           XPicture_recordDrawPolyline(self->m_picture, points, count);
}

static bool painterRecord_drawPolygon(XPainter* self,
                                      const XPoint* points, int count,
                                      bool filled,
                                      XPainterFillRule fillRule)
{
    return self && self->m_picture && points &&
           XPicture_recordDrawPolygon(self->m_picture, points, count,
                                      filled, (int)fillRule);
}

static bool painterRecord_drawPoints(XPainter* self,
                                     const XPoint* points, int count)
{
    return self && self->m_picture && points &&
           XPicture_recordDrawPoints(self->m_picture, points, count);
}
#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
static bool painterRecord_drawPath(XPainter* self, XPainterPathOp op,
                                   const XPainterPath* path)
{
    return self && self->m_picture && path &&
           XPicture_recordDrawPath(self->m_picture, (int)op, path);
}
#endif /* XPAINTER_PATH_ON */

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

/* Qt 的 QPicturePaintEngine 在画笔状态脏标志变化时会写入 PdcSetPen。
   XPainter 的便携流采用固定宽度快照，故即使可选的画笔样式能力被裁剪，
   基础颜色仍可跨配置回放。调用方已经先更新本地状态；记录失败不回滚
   setter 的 Qt 一致无返回值语义。 */
static void painterRecord_penState(XPainter* self)
{
    int style = 1;
    int width;
    int cap = 0x10;
    int join = 0x40;
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    width = self->m_state.m_penWidth;
#if XPAINTER_PENSTYLE_ON
    style = (int)self->m_state.m_penStyle;
    cap = (int)self->m_state.m_penCap;
    join = (int)self->m_state.m_penJoin;
#endif /* XPAINTER_PENSTYLE_ON */
    (void)XPicture_recordSetPen(self->m_picture, self->m_state.m_penColor,
                                style, width, cap, join);
}

/* Qt 的 QPicturePaintEngine 在 DirtyOpacity 时写入 PdcSetOpacity。
   XPainter 采用有限的 float 状态，因此使用与当前状态相同的四字节
   便携表示；回放直接更新状态，避免在 Picture 后端再次追加命令。 */
static void painterRecord_opacity(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetOpacity(self->m_picture, self->m_state.m_opacity);
}

/* Qt 的 QPicturePaintEngine 在 DirtyCompositionMode 时写入
   PdcSetCompositionMode；有效枚举范围由 XPainter setter 先行校验。 */
static void painterRecord_compositionMode(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetCompositionMode(
        self->m_picture, (int)self->m_state.m_compositionMode);
}

#if XPAINTER_RENDERHINT_ON
/* Qt QPicturePaintEngine::updateRenderHints() writes the complete hint mask
   as one PdcSetRenderHint record (qpaintengine_pic.cpp:274-281).  Recording
   the post-update mask, rather than an individual bit, also preserves the
   semantics of QPainter::setRenderHints(mask, on) when several bits change. */
static void painterRecord_renderHints(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetRenderHints(self->m_picture,
                                        self->m_state.m_renderHints);
}
#endif /* XPAINTER_RENDERHINT_ON */

#if XPAINTER_BRUSH_ORIGIN_ON
/* Qt QPicturePaintEngine::updateBrushOrigin() writes one QPointF after a
   DirtyBrushOrigin update (qpaintengine_pic.cpp:194-202, 483-496).  The
   portable stream stores the same two coordinates as fixed-width floats,
   matching XPainterState and avoiding host-dependent QPointF padding. */
static void painterRecord_brushOrigin(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetBrushOrigin(self->m_picture,
                                        self->m_state.m_brushOriginX,
                                        self->m_state.m_brushOriginY);
}
#endif /* XPAINTER_BRUSH_ORIGIN_ON */

/* Qt QPicturePaintEngine::updateMatrix() serializes the complete matrix
   after every DirtyTransform update (qpaintengine_pic.cpp:235-243 and
   :491).  Keep
   the world-matrix enable bit beside the matrix because XPainter stores the
   disabled matrix for worldTransform() queries while omitting it from the
   effective raster transform. */
static void painterRecord_transform(XPainter* self)
{
    bool enabled = true;
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
#if XPAINTER_WORLD_MATRIX_ON
    enabled = self->m_state.m_worldMatrixEnabled;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    (void)XPicture_recordSetTransform(self->m_picture,
                                      &self->m_state.m_transform,
                                      enabled);
}

#if XPAINTER_VIEW_TRANSFORM_ON
/* Qt QPainter serializes window/viewport changes through DirtyTransform.
   Keep the two rectangles as separate fixed records because either setter
   may be called independently and the portable stream must preserve the
   corresponding query state as well as the effective mapping. */
static void painterRecord_window(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetWindow(self->m_picture, &self->m_state.m_window);
}

static void painterRecord_viewport(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetViewport(self->m_picture,
                                     &self->m_state.m_viewport);
}

static void painterRecord_viewTransformEnabled(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetViewTransformEnabled(
        self->m_picture, self->m_state.m_viewTransformEnabled);
}
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

#if XPAINTER_CLIP_ON
/* Qt QPicturePaintEngine 序列化裁剪状态时分别写入启用标志和裁剪区域
   （qpaintengine_pic.cpp:152-159、247-269）。回放期间直接调用同一
   XPainter setter，但 m_replaying 会抑制再次向 Picture 追加记录。 */
static void painterRecord_clipEnabled(XPainter* self)
{
    if (!self || self->m_replaying ||
        self->m_deviceKind != XPainterDevice_Picture || !self->m_picture)
        return;
    (void)XPicture_recordSetClipEnabled(self->m_picture,
                                        self->m_state.m_hasClip);
}

static void painterRecord_clipRect(XPainter* self, const XRect* rect,
                                   XPainterClipOperation operation)
{
    if (!self || self->m_replaying ||
        self->m_deviceKind != XPainterDevice_Picture || !self->m_picture)
        return;
    (void)XPicture_recordSetClipRect(self->m_picture, rect, (int)operation);
}

#if XPAINTER_CLIP_REGION_ON
static void painterRecord_clipRegion(XPainter* self, const XRegion* region,
                                     XPainterClipOperation operation)
{
    if (!self || self->m_replaying ||
        self->m_deviceKind != XPainterDevice_Picture || !self->m_picture)
        return;
    (void)XPicture_recordSetClipRegion(self->m_picture, region,
                                       (int)operation);
}
#endif /* XPAINTER_CLIP_REGION_ON */
#endif /* XPAINTER_CLIP_ON */

/* Qt QPicturePaintEngine::updateBackground() records PdcSetBkColor as a
   separate fixed-color command (qpaintengine_pic.cpp:219-231).  Keep this
   record independent of the optional background-brush feature so cropped
   builds still preserve QPainter::background(). */
static void painterRecord_backgroundColor(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetBackgroundColor(self->m_picture,
                                            self->m_state.m_backgroundColor);
}

#if XPAINTER_BACKGROUND_ON
/* Qt writes PdcSetBkMode beside PdcSetBkColor.  The setter validates the
   two Qt modes before this recorder is reached, so the payload is fixed and
   replay validation remains deterministic. */
static void painterRecord_backgroundMode(XPainter* self)
{
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
    (void)XPicture_recordSetBackgroundMode(
        self->m_picture, (int)self->m_state.m_backgroundMode);
}
#endif /* XPAINTER_BACKGROUND_ON */

/* Qt updateBrush() writes PdcSetBrush whenever DirtyBrush changes.  The
   embedded record deliberately covers only the fixed style/color subset;
   variable gradient and texture payloads remain outside this opcode. */
static void painterRecord_brush(XPainter* self)
{
    int style = 0;
    if (!self || self->m_deviceKind != XPainterDevice_Picture ||
        !self->m_picture)
        return;
#if XPAINTER_BRUSH_ON
    style = (int)self->m_state.m_brush.m_style;
#endif /* XPAINTER_BRUSH_ON */
    (void)XPicture_recordSetBrush(self->m_picture, style,
                                  self->m_state.m_brushColor);
}


/* ========== 画笔样式（虚线/点线） ========== */

void XPainter_setBackground(XPainter* self, uint32_t color)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    self->m_state.m_backgroundColor = color;
#if XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON
    self->m_state.m_backgroundBrush.m_style = XPainterBrushStyle_SolidPattern;
    self->m_state.m_backgroundBrush.m_color = color;
    memset(&self->m_state.m_backgroundBrush.m_gradient, 0,
           sizeof(self->m_state.m_backgroundBrush.m_gradient));
#endif /* XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON */
    painterRecord_backgroundColor(self);
}

uint32_t XPainter_background(const XPainter* self)
{
    if (!self)
        return 0u;
    /* QPainter::background() on an inactive painter reads fakeState()->brush,
       whose default is NoBrush with opaque black color. */
    if (self->m_deviceKind == XPainterDevice_None)
        return 0xff000000u;
    return self->m_state.m_backgroundColor;
}
#if XPAINTER_BACKGROUND_ON
#if XPAINTER_BRUSH_ON
void XPainter_setBackground_2(XPainter* self, const XPainterBrush* brush)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !brush)
        return;
    self->m_state.m_backgroundBrush = *brush;
    self->m_state.m_backgroundColor = brush->m_color;
    painterRecord_backgroundColor(self);
}

void XPainter_backgroundBrush(const XPainter* self, XPainterBrush* out)
{
    if (!out)
        return;
    if (!self || self->m_deviceKind == XPainterDevice_None)
    {
        memset(out, 0, sizeof(*out));
        out->m_style = XPainterBrushStyle_NoBrush;
        /* Qt 的 fakeState() 使用 QBrush 默认值：NoBrush + 黑色。 */
        out->m_color = 0xff000000u;
        return;
    }
    *out = self->m_state.m_backgroundBrush;
}
#endif /* XPAINTER_BRUSH_ON */

void XPainter_setBackgroundMode(XPainter* self, XPainterBackgroundMode mode)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    if (mode != XPainterBackgroundMode_Transparent &&
        mode != XPainterBackgroundMode_Opaque)
        return;
    if (self->m_state.m_backgroundMode == mode)
        return;
    self->m_state.m_backgroundMode = mode;
    painterRecord_backgroundMode(self);
}

XPainterBackgroundMode XPainter_backgroundMode(const XPainter* self)
{
    return (self && self->m_deviceKind != XPainterDevice_None)
                ? self->m_state.m_backgroundMode
                : XPainterBackgroundMode_Transparent;
}
#endif /* XPAINTER_BACKGROUND_ON */
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
        case XPainterPenStyle_CustomDashLine:
            /* 未提供 QPen::setDashPattern 的动态数组时，使用 Qt 默认
               DashLine 的节距作为可裁剪后端的确定性近似。 */
            *outPattern = kDash; *outCount = 2; return true;
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
            if (!self->m_drawLine(self, aX, aY, bX, bY))
                return false;
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
    if (!g) return 0xff000000u;
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
    if (!isfinite(t)) t = 0.0f;
    if (g->m_stopCount <= 0)
    {
        /* QGradient::stops() supplies implicit black-at-0/white-at-1
           stops when the caller has not configured any explicit stop. */
        return painterLerpColor(0xff000000u, 0xffffffffu,
                                t <= 0.0f ? 0.0f :
                                (t >= 1.0f ? 1.0f : t));
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

#if XPAINTER_POLYGON_ON || XPAINTER_SHAPE_ON || XPAINTER_PATH_ON
/** @brief 内部多边形顶点上限（椭圆采样与扫描填充共用）。 */
#define XPAINTER_POLY_MAX_POINTS 128

typedef struct XPainterFillCrossing
{
    float m_x;
    int m_direction;
} XPainterFillCrossing;

/** @brief 按填充规则把排序后的交点转换为成对填充区间。 */
static int painterBuildFillSpans(XPainterFillCrossing* crossings, int count,
                                 XPainterFillRule rule, float* spans)
{
    int i;
    int spanCount = 0;
    if (!crossings || !spans || count <= 0) return 0;
    if (rule != XPainterFillRule_Winding)
    {
        for (i = 0; i + 1 < count; i += 2)
        {
            spans[spanCount++] = crossings[i].m_x;
            spans[spanCount++] = crossings[i + 1].m_x;
        }
        return spanCount;
    }
    {
        int winding = 0;
        float start = 0.0f;
        for (i = 0; i < count; ++i)
        {
            float x = crossings[i].m_x;
            int previous = winding;
            winding += crossings[i].m_direction;
            if (previous == 0 && winding != 0)
                start = x;
            else if (previous != 0 && winding == 0)
            {
                spans[spanCount++] = start;
                spans[spanCount++] = x;
            }
        }
    }
    return spanCount;
}

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
static bool painterScanFillUser(XPainter* self, int n,
                                const float* uxs, const float* uys,
                                uint32_t color, bool gradient,
                                XPainterFillRule fillRule)
{
    XPainterFillCrossing crossings[XPAINTER_POLY_MAX_POINTS];
    float spans[XPAINTER_POLY_MAX_POINTS];
    float minY, maxY;
    int y0, y1, py;
    int i;
    if (!self || !uxs || !uys || n < 3) return self != NULL;
    minY = maxY = uys[0];
    for (i = 1; i < n; ++i)
    {
        if (uys[i] < minY) minY = uys[i];
        if (uys[i] > maxY) maxY = uys[i];
    }
    if (!painterSpanPixelRange(minY, maxY, &y0, &y1))
        return true;
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
                crossings[xc].m_x = uxs[j] + t * (uxs[k] - uxs[j]);
                crossings[xc].m_direction = by > ay ? 1 : -1;
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
                    XPainterFillCrossing key = crossings[a];
                    b = a - 1;
                    while (b >= 0 && crossings[b].m_x > key.m_x)
                    { crossings[b + 1] = crossings[b]; --b; }
                    crossings[b + 1] = key;
                }
            }
            {
                int spanCount = painterBuildFillSpans(crossings, xc,
                                                       fillRule, spans);
                for (j = 0; j + 1 < spanCount; j += 2)
                {
                    float left = spans[j];
                    float right = spans[j + 1];
                    int xl;
                    int xr;
                if (painterSpanPixelRange(left, right, &xl, &xr))
                {
                    XRect r;
                    uint32_t fc = color;
                    if (gradient)
                    {
#if XPAINTER_BRUSH_ON
                        const XPainterGradient* g = &self->m_state.m_brush.m_gradient;
                        fc = painterGradientColor(
                                g,
                                (float)(xl + xr) * 0.5f -
#if XPAINTER_BRUSH_ORIGIN_ON
                                    self->m_state.m_brushOriginX,
#else
                                    0.0f,
#endif
                                (float)py -
#if XPAINTER_BRUSH_ORIGIN_ON
                                    self->m_state.m_brushOriginY);
#else
                                    0.0f);
#endif
#endif /* XPAINTER_BRUSH_ON */
                    }
                    r.x = xl; r.y = py; r.width = xr - xl + 1; r.height = 1;
                    if (!XPainter_fillRect(self, &r, fc))
                        return false;
                }
                }
            }
        }
    }
    return true;
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
static bool painterScanFillDevice(XPainter* self, int n,
                                  const float* uxs, const float* uys,
                                  uint32_t color, bool gradient,
                                  XPainterFillRule fillRule)
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
    if (!self || !self->m_image || !uxs || !uys || n < 3)
        return self != NULL;
    if (!painterEffectiveTransform(&self->m_state, &transform))
        return false;
    for (i = 0; i < n; ++i)
    {
        float a, b;
        if (!painterMapPoint(&transform, uxs[i], uys[i], &a, &b))
            return false;
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
    if (!painterSpanPixelRange(minX, maxX, &px0, &px1) ||
        !painterSpanPixelRange(minY, maxY, &py0, &py1))
        return true;
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 >= XImage_width(self->m_image)) px1 = XImage_width(self->m_image) - 1;
    if (py1 >= XImage_height(self->m_image)) py1 = XImage_height(self->m_image) - 1;
    if (gradient)
        haveInverse = painterMatrixInvert(&transform, &inverse);
    for (py = py0; py <= py1; ++py)
    {
        float yc = (float)py + 0.5f;
        XPainterFillCrossing crossings[XPAINTER_POLY_MAX_POINTS];
        float spans[XPAINTER_POLY_MAX_POINTS];
        int xc = 0;
        int j;
        for (j = 0; j < n; ++j)
        {
            int k = (j + 1) % n;
            float ay = dty[j], by = dty[k];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc))
            {
                float t = (yc - ay) / (by - ay);
                crossings[xc].m_x = dtx[j] + t * (dtx[k] - dtx[j]);
                crossings[xc].m_direction = by > ay ? 1 : -1;
                ++xc;
                if (xc == XPAINTER_POLY_MAX_POINTS) break;
            }
        }
        if (xc < 2) continue;
        {
            int a, b;
            for (a = 1; a < xc; ++a)
            {
                XPainterFillCrossing key = crossings[a];
                b = a - 1;
                while (b >= 0 && crossings[b].m_x > key.m_x)
                { crossings[b + 1] = crossings[b]; --b; }
                crossings[b + 1] = key;
            }
        }
        {
            int spanCount = painterBuildFillSpans(crossings, xc,
                                                   fillRule, spans);
            for (j = 0; j + 1 < spanCount; j += 2)
        {
            int xl;
            int xr;
            int px;
            if (!painterSpanPixelRange(spans[j], spans[j + 1], &xl, &xr))
                continue;
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
                                &self->m_state.m_brush.m_gradient,
                                ux -
#if XPAINTER_BRUSH_ORIGIN_ON
                                    self->m_state.m_brushOriginX,
#else
                                    0.0f,
#endif
                                uy -
#if XPAINTER_BRUSH_ORIGIN_ON
                                    self->m_state.m_brushOriginY);
#else
                                    0.0f);
#endif
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
    return true;
}

/**
 * @brief      用一个多边形闭合填充当前画刷的颜色/渐变色。
 * @param self 绘制器指针。
 * @param n    顶点数。
 * @param uxs  用户坐标 X。
 * @param uys  用户坐标 Y。
 */
static bool painterFillPolygonShape(XPainter* self, int n,
                                    const float* uxs, const float* uys,
                                    XPainterFillRule fillRule)
{
    uint32_t brushColor;
    bool gradient = false;
    if (!self || n < 3) return self != NULL;
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
    /* Qt 光栅引擎始终在设备空间按像素中心扫描多边形。图像后端即使
       使用纯色画刷也走同一路径；否则在缩放/旋转时会把用户空间整数
       扫描线误当成设备像素，导致边缘漏画或多画。Picture 后端仍在
       用户空间分解为 fillRect 命令，录制格式保持可移植。 */
    if (self->m_deviceKind == XPainterDevice_Image)
    {
        return painterScanFillDevice(self, n, uxs, uys, brushColor, gradient,
                                     fillRule);
    }
    return painterScanFillUser(self, n, uxs, uys, brushColor, gradient,
                               fillRule);
}

/**
 * @brief      计算圆弧上若干采样点。
 * @param cx/cy/rx/ry 椭圆中心与半径。
 * @param startA 起始角（弧度，0 度位于 3 点钟方向，正角度逆时针）。
 * @param spanA  跨度角（弧度，可负；在设备坐标中需反转 Y 轴）。
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
        /* Qt 的角度约定以数学坐标为准，设备 Y 轴向下时反转正弦项，
           使正跨度从 3 点钟方向沿逆时针方向经过 12 点钟方向。 */
        ys[i] = cy - ry * sinf(a);
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

/**
 * @brief      将圆弧跨度限制为 Qt 路径引擎支持的一整圆范围。
 * @param spanAngle 输入的跨度，单位为十六分之一度。
 * @return       限制到 [-5760, 5760] 后的跨度。
 *
 * Qt 6.8 的 qt_curves_for_arc() 在生成贝塞尔曲线前会把超过一整圆的
 * sweepLength 截断为正负 360 度。XPainter 的图像后端使用折线近似路径，
 * 因此也必须先截断，否则大跨度会重复描边，尤其会改变虚线画笔的相位。
 * 便携 shape 回调仍接收原始 opcode 参数，由回调自行决定其协议语义。
 */
static int painterClampArcSpan(int spanAngle)
{
    const int fullCircle = 360 * 16;
    if (spanAngle > fullCircle) return fullCircle;
    if (spanAngle < -fullCircle) return -fullCircle;
    return spanAngle;
}
#endif /* XPAINTER_POLYGON_ON || XPAINTER_SHAPE_ON || XPAINTER_PATH_ON */

/* ========== XPainter 公开 API ========== */

void XPainter_init(XPainter* self, void* userData)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    self->m_userData = userData;
    painterDefaultState(&self->m_state);
    self->m_replaying = false;
    self->m_initialized = true;
}

void XPainter_deinit(XPainter* self)
{
    if (!self) return;
    if (!self->m_initialized) return;
    XPainter_end(self);
    XFont_deinit(&self->m_state.m_font);
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
    XRegion_deinit(&self->m_state.m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
    memset(self, 0, sizeof(*self));
    self->m_userData = NULL;
}

bool XPainter_begin_image(XPainter* self, XImage* image)
{
    if (!self || !self->m_initialized || !image || XImage_isNull(image)) return false;
    /* QPainter::begin() 拒绝在同一绘制器已激活时再次绑定设备，并保持
       原有设备与状态不变；调用方应先显式 end()。 */
    if (self->m_deviceKind != XPainterDevice_None) return false;
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
    if (!self || !self->m_initialized || !picture) return false;
    /* 与 begin_image() 一致：活动绘制器不能被隐式切换到另一设备。 */
    if (self->m_deviceKind != XPainterDevice_None) return false;
    self->m_picture = picture;
    self->m_deviceKind = XPainterDevice_Picture;
    self->m_drawLine = painterRecord_drawLine;
    self->m_fillRect = painterRecord_fillRect;
    self->m_drawImage = painterRecord_drawImage;
    self->m_save = painterRecord_save;
    self->m_restore = painterRecord_restore;
#if XPAINTER_SHAPE_ON
    self->m_drawShape = painterRecord_drawShape;
#endif
#if XPAINTER_POLYGON_ON
    self->m_drawPolyline = painterRecord_drawPolyline;
    self->m_drawPolygon = painterRecord_drawPolygon;
    self->m_drawPoints = painterRecord_drawPoints;
#endif
#if XPAINTER_PATH_ON
    self->m_drawPath = painterRecord_drawPath;
#endif
#if XPAINTER_VIEW_TRANSFORM_ON
    painterResetViewTransform(self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    return true;
}

bool XPainter_end(XPainter* self)
{
    bool wasActive;
    if (!self || !self->m_initialized) return false;
    wasActive = self->m_deviceKind != XPainterDevice_None;
    {
        int i;
        for (i = 0; i < self->m_stateCount; ++i)
        {
            XFont_deinit(&self->m_stateStack[i].m_font);
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
            XRegion_deinit(&self->m_stateStack[i].m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
        }
    }
    XFont_deinit(&self->m_state.m_font);
#if XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON
    XRegion_deinit(&self->m_state.m_clipRegion);
#endif /* XPAINTER_CLIP_ON && XPAINTER_CLIP_REGION_ON */
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
    self->m_replaying = false;
    painterDefaultState(&self->m_state);
    return wasActive;
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

/*
 * 按 Qt QRect 的几何边规范化填充输入。QPainter::fillRect(const QRect&)
 * 对负宽/高仍填充交换后的正区域，但任一轴为零时保持空操作；该辅助
 * 函数保留零轴信息并在 int64 中计算边界，避免 x + width 溢出。
 */
static bool painterNormalizeFillRect(const XRect* rect, XRect* out)
{
    int64_t left;
    int64_t top;
    int64_t right;
    int64_t bottom;
    int64_t width;
    int64_t height;
    if (!rect || !out) return false;
    left = rect->x;
    top = rect->y;
    right = left + (int64_t)rect->width;
    bottom = top + (int64_t)rect->height;
    if (right < left)
    {
        int64_t t = left;
        left = right;
        right = t;
    }
    if (bottom < top)
    {
        int64_t t = top;
        top = bottom;
        bottom = t;
    }
    width = right - left;
    height = bottom - top;
    out->x = left > INT_MAX ? INT_MAX :
             (left < INT_MIN ? INT_MIN : (int)left);
    out->y = top > INT_MAX ? INT_MAX :
             (top < INT_MIN ? INT_MIN : (int)top);
    out->width = width > INT_MAX ? INT_MAX : (width > 0 ? (int)width : 0);
    out->height = height > INT_MAX ? INT_MAX : (height > 0 ? (int)height : 0);
    return width > 0 && height > 0;
}

bool XPainter_drawRect(XPainter* self, const XRect* rect)
{
    bool ok;
    XRect normalized;
    int64_t left64;
    int64_t top64;
    int64_t x2_64;
    int64_t y2_64;
    int64_t right64;
    int64_t bottom64;
    int right;
    int bottom;
    if (!self) return false;
    if (!rect) return true;
    /* QRect/QPainter 允许单轴退化矩形绘制一条线；仅双零尺寸才是
       真正的空矩形。负尺寸先按 QRect::normalized() 规则处理。 */
    if (rect->width == 0 && rect->height == 0) return true;
    if (self->m_deviceKind == XPainterDevice_None)
        return false;
    /* QRect 的边界是 x 与 x + width（不是 width - 1）；单轴为零时
       仍须保留另一轴长度，故不能直接使用 XRect_normalized()，它会将
       退化矩形压缩为零尺寸。 */
    left64 = rect->x;
    top64 = rect->y;
    x2_64 = left64 + (int64_t)rect->width;
    y2_64 = top64 + (int64_t)rect->height;
    if (x2_64 < left64)
    {
        int64_t t = left64;
        left64 = x2_64;
        x2_64 = t;
    }
    if (y2_64 < top64)
    {
        int64_t t = top64;
        top64 = y2_64;
        y2_64 = t;
    }
    normalized.x = left64 > INT_MAX ? INT_MAX :
                   (left64 < INT_MIN ? INT_MIN : (int)left64);
    normalized.y = top64 > INT_MAX ? INT_MAX :
                   (top64 < INT_MIN ? INT_MIN : (int)top64);
    normalized.width = (x2_64 - left64) > INT_MAX ? INT_MAX :
                       (int)(x2_64 - left64);
    normalized.height = (y2_64 - top64) > INT_MAX ? INT_MAX :
                        (int)(y2_64 - top64);
    right64 = x2_64;
    bottom64 = y2_64;
    right = right64 > INT_MAX ? INT_MAX :
            (right64 < INT_MIN ? INT_MIN : (int)right64);
    bottom = bottom64 > INT_MAX ? INT_MAX :
             (bottom64 < INT_MIN ? INT_MIN : (int)bottom64);
    /* QPainter::drawRect 先用当前画刷填充，再用当前画笔描边。默认
       QBrush 是 NoBrush，因此普通边框调用仍只产生轮廓。 */
#if XPAINTER_BRUSH_ON
    if (self->m_state.m_brush.m_style != XPainterBrushStyle_NoBrush)
    {
        if (self->m_state.m_brush.m_style ==
            XPainterBrushStyle_SolidPattern)
        {
            if (!XPainter_fillRect(self, &normalized,
                                   self->m_state.m_brush.m_color))
                return false;
        }
        else
        {
#if XPAINTER_SHAPE_ON || XPAINTER_POLYGON_ON || XPAINTER_PATH_ON
            float xs[4];
            float ys[4];
            xs[0] = (float)normalized.x;                         ys[0] = (float)normalized.y;
            xs[1] = (float)normalized.x + (float)normalized.width;    ys[1] = ys[0];
            xs[2] = xs[1];                                  ys[2] = (float)normalized.y + (float)normalized.height;
            xs[3] = xs[0];                                  ys[3] = ys[2];
            if (!painterFillPolygonShape(self, 4, xs, ys,
                                         XPainterFillRule_OddEven))
                return false;
#else
            /* 形状、多边形和路径能力同时裁剪时，渐变只能退化为其当前颜色。 */
            if (!XPainter_fillRect(self, &normalized, self->m_state.m_brush.m_color))
                return false;
#endif /* XPAINTER_SHAPE_ON || XPAINTER_POLYGON_ON || XPAINTER_PATH_ON */
        }
    }
#else
    /* 画刷能力被裁剪时，drawRect 退化为仅使用当前画笔描边；不能再
       依赖历史 m_brushColor 强制填充，否则关闭特性后会产生隐藏状态。 */
#endif /* XPAINTER_BRUSH_ON */
    if (!self->m_drawLine)
        return true;
    /* 按四条边各画一条线：上、左、右、下。统一经过 drawLine()，
       这样 NoPen、虚线和画笔宽度等状态与单独画线保持一致。 */
    ok = XPainter_drawLine(self, normalized.x, normalized.y,
                           right, normalized.y);
    if (!ok) return false;
    ok = XPainter_drawLine(self, normalized.x, normalized.y,
                           normalized.x, bottom);
    if (!ok) return false;
    ok = XPainter_drawLine(self, right, normalized.y,
                           right, bottom);
    if (!ok) return false;
    ok = XPainter_drawLine(self, normalized.x, bottom,
                           right, bottom);
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
    XRect normalized;
    if (!self) return false;
    if (!rect) return false;
    if (!painterNormalizeFillRect(rect, &normalized)) return true;
    if (self->m_deviceKind == XPainterDevice_None || !self->m_fillRect)
        return false;
    return self->m_fillRect(self, &normalized, color);
}

bool XPainter_fillRect_2(XPainter* self, const XRect* rect)
{
    XRect normalized;
    if (!self) return false;
    if (!rect) return false;
    if (!painterNormalizeFillRect(rect, &normalized)) return true;
    if (self->m_deviceKind == XPainterDevice_None)
        return false;
#if XPAINTER_BRUSH_ON
    if (self->m_state.m_brush.m_style == XPainterBrushStyle_NoBrush)
        return true;
    if (self->m_state.m_brush.m_style == XPainterBrushStyle_SolidPattern)
        return XPainter_fillRect(self, &normalized, self->m_state.m_brush.m_color);
#if XPAINTER_SHAPE_ON || XPAINTER_POLYGON_ON || XPAINTER_PATH_ON
    {
        float xs[4];
        float ys[4];
        xs[0] = (float)normalized.x;                         ys[0] = (float)normalized.y;
        xs[1] = (float)normalized.x + (float)normalized.width;    ys[1] = ys[0];
        xs[2] = xs[1];                                  ys[2] = (float)normalized.y + (float)normalized.height;
        xs[3] = xs[0];                                  ys[3] = ys[2];
        return painterFillPolygonShape(self, 4, xs, ys,
                                       XPainterFillRule_OddEven);
    }
#else
    return XPainter_fillRect(self, &normalized, self->m_state.m_brush.m_color);
#endif
#else
    return XPainter_fillRect(self, &normalized, self->m_state.m_brushColor);
#endif
}

bool XPainter_eraseRect(XPainter* self, const XRect* rect)
{
    if (!self) return false;
#if XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON
    /* Qt::eraseRect 直接把 state->bgBrush 传给 fillRect；临时切换当前
       画刷可以复用现有的纯色、渐变、NoBrush 和合成逻辑，同时不改动
       对外可观察的当前画刷状态。 */
    if (self->m_deviceKind == XPainterDevice_None)
        return XPainter_fillRect(self, rect, self->m_state.m_backgroundColor);
    {
        XPainterBrush savedBrush = self->m_state.m_brush;
        bool ok;
        self->m_state.m_brush = self->m_state.m_backgroundBrush;
        ok = XPainter_fillRect_2(self, rect);
        self->m_state.m_brush = savedBrush;
        return ok;
    }
#else
    return XPainter_fillRect(self, rect, self->m_state.m_backgroundColor);
#endif /* XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON */
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

#if XPAINTER_IMAGE_RECT_ON
bool XPainter_drawImageRect(XPainter* self, const XRect* targetRect,
                            const XImage* image,
                            const XRect* sourceRect)
{
    XPainterImageRectParams params;
    int sourceX;
    int sourceY;
    int sourceWidth;
    int sourceHeight;
    int targetX;
    int targetY;
    int targetWidth;
    int targetHeight;
    if (!self || !targetRect || !image || !sourceRect)
        return false;
    if (XImage_isNull(image)) return true;
    if (!painterPrepareImageRect(targetRect, image, sourceRect, &params))
        return true;
    if (self->m_deviceKind == XPainterDevice_Image)
        return painterRaster_drawImageRect(self, &params, image);
    if (self->m_deviceKind == XPainterDevice_Picture)
    {
        XImage cropped;
        XImage scaled;
        const XImage* drawImage = &cropped;
        bool ok;
        sourceX = painterRound(params.m_sourceX);
        sourceY = painterRound(params.m_sourceY);
        sourceWidth = painterRound(params.m_sourceWidth);
        sourceHeight = painterRound(params.m_sourceHeight);
        targetX = painterRound(params.m_targetX);
        targetY = painterRound(params.m_targetY);
        targetWidth = painterRound(params.m_targetWidth);
        targetHeight = painterRound(params.m_targetHeight);
        if (sourceWidth <= 0 || sourceHeight <= 0 ||
            targetWidth <= 0 || targetHeight <= 0)
            return true;
        XImage_init(&cropped);
        {
            XRect source;
            source.x = sourceX;
            source.y = sourceY;
            source.width = sourceWidth;
            source.height = sourceHeight;
            XImage_copyRect(image, &source, &cropped);
        }
        if (XImage_isNull(&cropped))
        {
            XImage_deinit_base(&cropped);
            return false;
        }
        XImage_init(&scaled);
        if (XImage_width(&cropped) != targetWidth ||
            XImage_height(&cropped) != targetHeight)
        {
            XImage_scaled(&cropped, targetWidth, targetHeight,
                          0u, 0u, &scaled);
            if (XImage_isNull(&scaled))
            {
                XImage_deinit_base(&scaled);
                XImage_deinit_base(&cropped);
                return false;
            }
            drawImage = &scaled;
        }
        ok = XPainter_drawImage(self, drawImage, targetX, targetY);
        XImage_deinit_base(&scaled);
        XImage_deinit_base(&cropped);
        return ok;
    }
    return false;
}
#endif /* XPAINTER_IMAGE_RECT_ON */

#if XPAINTER_PIXMAP_ON
bool XPainter_drawPixmap(XPainter* self, const XPixmap* pixmap, int x, int y)
{
    XImage image;
    bool ok;
    int width;
    int height;
#if XPAINTER_IMAGE_RECT_ON
    float scale;
#endif /* XPAINTER_IMAGE_RECT_ON */
    if (!self || !pixmap) return false;
    if (XPixmap_isNull(pixmap)) return true;

    width = XPixmap_width(pixmap);
    height = XPixmap_height(pixmap);
    if (width <= 0 || height <= 0) return true;

    XImage_init(&image);
    XPixmap_toImage(pixmap, &image);
    if (XImage_isNull(&image))
    {
        XImage_deinit_base(&image);
        return true;
    }

#if XPAINTER_IMAGE_RECT_ON
    scale = XPixmap_devicePixelRatio(pixmap);
    /* QPainter::drawPixmap(QPointF, QPixmap) sends a physical pixmap to a
       logical rectangle whose dimensions are divided by devicePixelRatio. */
    if (scale > 0.0f && isfinite(scale) && scale != 1.0f)
    {
        XRect target;
        XRect source;
        target.x = x;
        target.y = y;
        target.width = painterRound((float)width / scale);
        target.height = painterRound((float)height / scale);
        source.x = 0;
        source.y = 0;
        source.width = width;
        source.height = height;
        if (target.width <= 0 || target.height <= 0)
        {
            XImage_deinit_base(&image);
            return true;
        }
        ok = XPainter_drawImageRect(self, &target, &image, &source);
    }
    else
#endif /* XPAINTER_IMAGE_RECT_ON */
    {
        ok = XPainter_drawImage(self, &image, x, y);
    }
    XImage_deinit_base(&image);
    return ok;
}

bool XPainter_drawPixmap_2(XPainter* self, const XPixmap* pixmap,
                           const XPoint* pos)
{
    if (!pos) return false;
    return XPainter_drawPixmap(self, pixmap, pos->x, pos->y);
}

#if XPAINTER_IMAGE_RECT_ON
bool XPainter_drawPixmapRect(XPainter* self, const XRect* targetRect,
                             const XPixmap* pixmap,
                             const XRect* sourceRect)
{
    XImage image;
    bool ok;
    if (!self || !targetRect || !pixmap || !sourceRect)
        return false;
    if (XPixmap_isNull(pixmap)) return true;
    XImage_init(&image);
    XPixmap_toImage(pixmap, &image);
    if (XImage_isNull(&image))
    {
        XImage_deinit_base(&image);
        return true;
    }
    ok = XPainter_drawImageRect(self, targetRect, &image, sourceRect);
    XImage_deinit_base(&image);
    return ok;
}
#endif /* XPAINTER_IMAGE_RECT_ON */

#if XPAINTER_TILED_PIXMAP_ON
/** @brief 计算非负取模，确保负平铺偏移按 Qt 规则向后环绕。 */
static int painterPixmapPositiveModulo(int value, int modulus)
{
    int result;
    if (modulus <= 0) return 0;
    result = value % modulus;
    return result < 0 ? result + modulus : result;
}

bool XPainter_drawTiledPixmap(XPainter* self, const XRect* rect,
                              const XPixmap* pixmap,
                              const XPoint* offset)
{
    XImage image;
    float scale;
    int physicalWidth;
    int physicalHeight;
    int tileWidth;
    int tileHeight;
    int offsetX;
    int offsetY;
    int y;
    if (!self || !rect || !pixmap) return false;
    if (XPixmap_isNull(pixmap) || rect->width <= 0 || rect->height <= 0)
        return true;

    physicalWidth = XPixmap_width(pixmap);
    physicalHeight = XPixmap_height(pixmap);
    if (physicalWidth <= 0 || physicalHeight <= 0) return true;
    scale = XPixmap_devicePixelRatio(pixmap);
    if (!(scale > 0.0f) || !isfinite(scale)) scale = 1.0f;
    tileWidth = painterRound((float)physicalWidth / scale);
    tileHeight = painterRound((float)physicalHeight / scale);
    if (tileWidth <= 0 || tileHeight <= 0) return true;
    offsetX = offset ? offset->x : 0;
    offsetY = offset ? offset->y : 0;
    offsetX = painterPixmapPositiveModulo(offsetX, tileWidth);
    offsetY = painterPixmapPositiveModulo(offsetY, tileHeight);

    XImage_init(&image);
    XPixmap_toImage(pixmap, &image);
    if (XImage_isNull(&image))
    {
        XImage_deinit_base(&image);
        return true;
    }
    for (y = 0; y < rect->height; )
    {
        int sourceYLogical = offsetY;
        int remainingY = rect->height - y;
        int targetHeight = tileHeight - sourceYLogical;
        int x;
        if (targetHeight > remainingY) targetHeight = remainingY;
        if (targetHeight <= 0) break;
        for (x = 0; x < rect->width; )
        {
            int sourceXLogical = offsetX;
            int remainingX = rect->width - x;
            int targetWidth = tileWidth - sourceXLogical;
            XRect target;
            XRect source;
            if (targetWidth > remainingX) targetWidth = remainingX;
            if (targetWidth <= 0) break;
            target.x = rect->x + x;
            target.y = rect->y + y;
            target.width = targetWidth;
            target.height = targetHeight;
            source.x = painterRound((float)sourceXLogical * scale);
            source.y = painterRound((float)sourceYLogical * scale);
            source.width = painterRound((float)targetWidth * scale);
            source.height = painterRound((float)targetHeight * scale);
            if (source.width > 0 && source.height > 0 &&
                !XPainter_drawImageRect(self, &target, &image, &source))
            {
                XImage_deinit_base(&image);
                return false;
            }
            x += targetWidth;
            offsetX = 0;
        }
        y += targetHeight;
        offsetY = 0;
        offsetX = offset ? painterPixmapPositiveModulo(offset->x, tileWidth) : 0;
    }
    XImage_deinit_base(&image);
    return true;
}
#endif /* XPAINTER_TILED_PIXMAP_ON */
#endif /* XPAINTER_PIXMAP_ON */

bool XPainter_drawPicture(XPainter* self, const XPicture* picture, int x, int y)
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
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
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
#if XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON
    /* Qt 的 OpaqueMode 会先以 background() 画刷覆盖字形单元，再绘制
       字形前景。嵌入式点阵字体没有复杂 glyph bounds，因此按完整 8x16
       单元填充；渐变背景使用其基色，保持固定成本。 */
    if (painter->m_state.m_backgroundMode == XPainterBackgroundMode_Opaque &&
        painter->m_state.m_backgroundBrush.m_style != XPainterBrushStyle_NoBrush)
    {
        XRect backgroundRect;
        backgroundRect.x = x;
        backgroundRect.y = baselineY - XFONT8X16_ASCENT * s;
        backgroundRect.width = XFONT8X16_WIDTH * s;
        backgroundRect.height = XFONT8X16_HEIGHT * s;
        XPainter_fillRect(painter, &backgroundRect,
                          painter->m_state.m_backgroundBrush.m_color);
    }
#endif /* XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON */
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

#if (XPAINTER_SHAPE_ON) || (XPAINTER_POLYGON_ON) || (XPAINTER_PATH_ON)
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
static bool painterDrawPolyLineFloat(XPainter* self,
                                     const float* uxs, const float* uys,
                                     int n, bool close)
{
    int i;
    if (!self || n < 2) return self != NULL;
    for (i = 0; i + 1 < n; ++i)
    {
        if (!XPainter_drawLine(self, painterRound(uxs[i]), painterRound(uys[i]),
                               painterRound(uxs[i + 1]),
                               painterRound(uys[i + 1])))
            return false;
    }
    /* 闭合请求对两点多边形同样有效：Qt 会把末点重新连接到首点，
       因而产生与正向边重合的反向边；开放折线传 close=false，不受影响。 */
    if (close && n >= 2)
        return XPainter_drawLine(self, painterRound(uxs[n - 1]),
                                 painterRound(uys[n - 1]),
                                 painterRound(uxs[0]),
                                 painterRound(uys[0]));
    return true;
}
#endif /* XPAINTER_SHAPE_ON || XPAINTER_POLYGON_ON || XPAINTER_PATH_ON */

#if XPAINTER_SHAPE_ON
bool XPainter_drawEllipse(XPainter* self, const XRect* rect)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    int n;
    XRect normalized;
    if (!self) return false;
    if (!rect) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    normalized = XRect_normalized(rect);
    if (normalized.width <= 0 || normalized.height <= 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Ellipse, &normalized,
                                 0, 0, painterBrushShouldFill(self), 0, 0);
    painterEllipseParams(&normalized, &cx, &cy, &rx, &ry);
    if (painterBrushShouldFill(self))
    {
        painterArcPoints(cx, cy, rx, ry, 0.0f, 6.283185307179586f,
                         64, xs, ys, &n);
        if (!painterFillPolygonShape(self, n, xs, ys,
                                     XPainterFillRule_OddEven))
            return false;
    }
    painterArcPoints(cx, cy, rx, ry, 0.0f, 6.283185307179586f,
                     32, xs, ys, &n);
        if (!painterDrawPolyLineFloat(self, xs, ys, n, true))
            return false;
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
    XRect normalized;
    if (!self) return false;
    if (!rect) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    normalized = XRect_normalized(rect);
    if (normalized.width <= 0 || normalized.height <= 0) return true;
    if (spanAngle == 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Arc, &normalized,
                                 startAngle, spanAngle, false, 0, 0);
    painterEllipseParams(&normalized, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)painterClampArcSpan(spanAngle) / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    return painterDrawPolyLineFloat(self, xs, ys, n, false);
}

bool XPainter_drawPie(XPainter* self, const XRect* rect,
                      int startAngle, int spanAngle)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float polyX[XPAINTER_POLY_MAX_POINTS];
    float polyY[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    float deg0, degS;
    int n;
    int i;
    XRect normalized;
    if (!self) return false;
    if (!rect) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    normalized = XRect_normalized(rect);
    if (normalized.width <= 0 || normalized.height <= 0) return true;
    if (spanAngle == 0) return true;
    if (startAngle > 360 * 16 || startAngle < 0)
    {
        startAngle %= 360 * 16;
        if (startAngle < 0) startAngle += 360 * 16;
    }
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Pie, &normalized,
                                 startAngle, spanAngle,
                                 painterBrushShouldFill(self), 0, 0);
    painterEllipseParams(&normalized, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)painterClampArcSpan(spanAngle) / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    if (painterBrushShouldFill(self))
    {
        polyX[0] = cx; polyY[0] = cy;
        for (i = 0; i < n; ++i) { polyX[1 + i] = xs[i]; polyY[1 + i] = ys[i]; }
        if (!painterFillPolygonShape(self, n + 1, polyX, polyY,
                                     XPainterFillRule_OddEven))
            return false;
    }
    /* 轮廓：圆心 → 起始点 → 弧 → 终点 → 圆心 */
    if (n > 0 && !XPainter_drawLine(self, painterRound(cx), painterRound(cy),
                                    painterRound(xs[0]), painterRound(ys[0])))
        return false;
    if (!painterDrawPolyLineFloat(self, xs, ys, n, false))
        return false;
    if (n > 0 && !XPainter_drawLine(self, painterRound(xs[n - 1]),
                                    painterRound(ys[n - 1]),
                                    painterRound(cx), painterRound(cy)))
        return false;
    return true;
}

bool XPainter_drawChord(XPainter* self, const XRect* rect,
                        int startAngle, int spanAngle)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float cx, cy, rx, ry;
    float deg0, degS;
    int n;
    XRect normalized;
    if (!self) return false;
    if (!rect) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    normalized = XRect_normalized(rect);
    if (normalized.width <= 0 || normalized.height <= 0) return true;
    if (spanAngle == 0) return true;
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_Chord, &normalized,
                                 startAngle, spanAngle,
                                 painterBrushShouldFill(self), 0, 0);
    painterEllipseParams(&normalized, &cx, &cy, &rx, &ry);
    deg0 = (float)startAngle / 16.0f;
    degS = (float)painterClampArcSpan(spanAngle) / 16.0f;
    painterArcPoints(cx, cy, rx, ry,
                     deg0 * XPAINTER_DEG_TO_RAD, degS * XPAINTER_DEG_TO_RAD,
                     32, xs, ys, &n);
    if (n >= 3 && painterBrushShouldFill(self))
        if (!painterFillPolygonShape(self, n, xs, ys,
                                     XPainterFillRule_OddEven))
            return false;
    return painterDrawPolyLineFloat(self, xs, ys, n, true); /* 闭合弦 */
}

bool XPainter_drawRoundedRect(XPainter* self, const XRect* rect,
                              int xRadius, int yRadius)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    float xr = (float)xRadius, yr = (float)yRadius;
    int count = 0;
    int perCorner;
    int x, y, w, h;
    XRect normalized;
    if (!self) return false;
    if (!rect) return false;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    normalized = XRect_normalized(rect);
    x = normalized.x; y = normalized.y; w = normalized.width; h = normalized.height;
    if (w <= 0 || h <= 0) return true;
    /* Qt 在扩展引擎分派之前处理非正半径：任一半径小于等于零时
       直接退化为普通矩形，因此不能把该输入交给圆角专用回调。 */
    if (xr <= 0.0f || yr <= 0.0f)
        return XPainter_drawRect(self, rect);
    if (self->m_drawShape)
        return self->m_drawShape(self, XPainterShapeOp_RoundedRect, &normalized,
                                 0, 0, painterBrushShouldFill(self),
                                 xRadius, yRadius);
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
        if (!painterFillPolygonShape(self, count, xs, ys,
                                     XPainterFillRule_OddEven))
            return false;
    return painterDrawPolyLineFloat(self, xs, ys, count, true);
}
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
bool XPainter_drawPolyline(XPainter* self, const XPoint* points, int count)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    int i;
    if (!self) return false;
    if (!points || count < 2) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (count > XPAINTER_POLY_MAX_POINTS) count = XPAINTER_POLY_MAX_POINTS;
    if (self->m_drawPolyline)
        return self->m_drawPolyline(self, points, count);
    for (i = 0; i < count; ++i) { xs[i] = (float)points[i].x; ys[i] = (float)points[i].y; }
    return painterDrawPolyLineFloat(self, xs, ys, count, false);
}

bool XPainter_drawPolygon(XPainter* self, const XPoint* points, int count,
                          XPainterFillRule fillRule)
{
    float xs[XPAINTER_POLY_MAX_POINTS];
    float ys[XPAINTER_POLY_MAX_POINTS];
    int i;
    if (!self) return false;
    if (!points || count < 2) return true;
    if (self->m_deviceKind == XPainterDevice_None) return false;
    if (fillRule != XPainterFillRule_Winding)
        fillRule = XPainterFillRule_OddEven;
    if (count > XPAINTER_POLY_MAX_POINTS) count = XPAINTER_POLY_MAX_POINTS;
    if (self->m_drawPolygon)
        return self->m_drawPolygon(self, points, count,
                                   painterBrushShouldFill(self), fillRule);
    for (i = 0; i < count; ++i) { xs[i] = (float)points[i].x; ys[i] = (float)points[i].y; }
    if (count >= 3 && painterBrushShouldFill(self))
        if (!painterFillPolygonShape(self, count, xs, ys, fillRule))
            return false;
    /* Qt QPainter::drawPolygon() 将首点隐式连接到末点，即使仅给出
       两个顶点也会形成闭合轮廓（第二条边与第一条边重合）。保持
       count==2 的闭合语义，避免与 QPaintEngineEx::drawPolygon()
       （qpainter.cpp:4558-4588、qpaintengineex.cpp:903-912）不一致；
       三点及以上仍按通常多边形闭合。 */
    return painterDrawPolyLineFloat(self, xs, ys, count, count >= 2);
}

bool XPainter_drawConvexPolygon(XPainter* self, const XPoint* points,
                                int count)
{
    /* Qt 的 drawConvexPolygon 始终用当前画刷填充、当前画笔描边；
     * 当前与 drawPolygon 共用扫描线实现，不区分凸凹性。 */
    return XPainter_drawPolygon(self, points, count,
                                XPainterFillRule_OddEven);
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

static bool painterPathAppendCubic(XPainterPath* self,
                                   float c1x, float c1y,
                                   float c2x, float c2y,
                                   float x, float y)
{
    /* 一次性预留三个元素，避免内存不足时留下半条曲线。 */
    if (!self || !painterPathEnsure(self, 3)) return false;
    if (!painterPathAppendElt(self, XPainterPathElement_CurveTo,
                              c1x, c1y, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;
    if (!painterPathAppendElt(self, XPainterPathElement_CurveToData,
                              c2x, c2y, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;
    return painterPathAppendElt(self, XPainterPathElement_CurveToData,
                                x, y, 0.0f, 0.0f, 0.0f, 0.0f);
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
    bool ok;
    if (!self) return false;
    if (!isfinite(x) || !isfinite(y)) return true;
    if (self->m_elementCount > 0 &&
        self->m_elements[self->m_elementCount - 1].m_type ==
            XPainterPathElement_MoveTo)
    {
        /* QPainterPath::moveTo() 合并连续 MoveTo，而不是追加无效元素。 */
        self->m_elements[self->m_elementCount - 1].m_x1 = x;
        self->m_elements[self->m_elementCount - 1].m_y1 = y;
        self->m_currentX = x;
        self->m_currentY = y;
        self->m_subpathStartX = x;
        self->m_subpathStartY = y;
        self->m_requireMoveTo = false;
        return true;
    }
    ok = painterPathAppendElt(self, XPainterPathElement_MoveTo,
                              x, y, 0.0f, 0.0f, 0.0f, 0.0f);
    if (!ok) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    self->m_subpathStartX = x;
    self->m_subpathStartY = y;
    self->m_requireMoveTo = false;
    return true;
}

bool XPainterPath_lineTo(XPainterPath* self, float x, float y)
{
    bool ok;
    if (!self || !isfinite(x) || !isfinite(y)) return self != NULL;
    if (self->m_elementCount == 0 && !XPainterPath_moveTo(self, 0.0f, 0.0f))
        return false;
    if (self->m_requireMoveTo)
    {
        if (!painterPathAppendElt(self, XPainterPathElement_MoveTo,
                                  self->m_currentX, self->m_currentY,
                                  0.0f, 0.0f, 0.0f, 0.0f))
            return false;
        self->m_subpathStartX = self->m_currentX;
        self->m_subpathStartY = self->m_currentY;
        self->m_requireMoveTo = false;
    }
    if (self->m_currentX == x && self->m_currentY == y)
        return true;
    ok = painterPathAppendElt(self, XPainterPathElement_LineTo,
                              x, y, 0.0f, 0.0f, 0.0f, 0.0f);
    if (!ok) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return true;
}

bool XPainterPath_quadTo(XPainterPath* self, float cx, float cy,
                         float x, float y)
{
    float c1x;
    float c1y;
    float c2x;
    float c2y;
    bool ok;
    if (!self || !isfinite(cx) || !isfinite(cy) ||
        !isfinite(x) || !isfinite(y))
        return self != NULL;
    if (self->m_elementCount == 0 && !XPainterPath_moveTo(self, 0.0f, 0.0f))
        return false;
    if (self->m_currentX == cx && self->m_currentY == cy &&
        cx == x && cy == y)
        return true;
    /* Qt 将二次曲线转换为三次曲线后再交给 cubicTo()。 */
    c1x = (self->m_currentX + 2.0f * cx) / 3.0f;
    c1y = (self->m_currentY + 2.0f * cy) / 3.0f;
    c2x = (x + 2.0f * cx) / 3.0f;
    c2y = (y + 2.0f * cy) / 3.0f;
    if (self->m_requireMoveTo)
    {
        if (!painterPathAppendElt(self, XPainterPathElement_MoveTo,
                                  self->m_currentX, self->m_currentY,
                                  0.0f, 0.0f, 0.0f, 0.0f))
            return false;
        self->m_subpathStartX = self->m_currentX;
        self->m_subpathStartY = self->m_currentY;
        self->m_requireMoveTo = false;
    }
    ok = painterPathAppendCubic(self, c1x, c1y, c2x, c2y, x, y);
    if (!ok) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return true;
}

bool XPainterPath_cubicTo(XPainterPath* self, float c1x, float c1y,
                          float c2x, float c2y, float x, float y)
{
    bool ok;
    if (!self || !isfinite(c1x) || !isfinite(c1y) ||
        !isfinite(c2x) || !isfinite(c2y) ||
        !isfinite(x) || !isfinite(y))
        return self != NULL;
    if (self->m_elementCount == 0 && !XPainterPath_moveTo(self, 0.0f, 0.0f))
        return false;
    if (self->m_currentX == c1x && self->m_currentY == c1y &&
        c1x == c2x && c1y == c2y && c2x == x && c2y == y)
        return true;
    if (self->m_requireMoveTo)
    {
        if (!painterPathAppendElt(self, XPainterPathElement_MoveTo,
                                  self->m_currentX, self->m_currentY,
                                  0.0f, 0.0f, 0.0f, 0.0f))
            return false;
        self->m_subpathStartX = self->m_currentX;
        self->m_subpathStartY = self->m_currentY;
        self->m_requireMoveTo = false;
    }
    ok = painterPathAppendCubic(self, c1x, c1y, c2x, c2y, x, y);
    if (!ok) return false;
    self->m_currentX = x;
    self->m_currentY = y;
    return true;
}

bool XPainterPath_closeSubpath(XPainterPath* self)
{
    if (!self) return false;
    /* Qt 对空路径 closeSubpath() 规定为无操作。 */
    if (self->m_elementCount == 0) return true;
    /* QPainterPathPrivate::close() 只在末点尚未回到子路径起点时补一条
       LineTo；Qt 不把 CloseSubpath 作为公开 ElementType 存入元素数组。 */
    if (fabsf(self->m_currentX - self->m_subpathStartX) > 1.0e-5f ||
        fabsf(self->m_currentY - self->m_subpathStartY) > 1.0e-5f)
    {
        if (!painterPathAppendElt(self, XPainterPathElement_LineTo,
                                  self->m_subpathStartX, self->m_subpathStartY,
                                  0.0f, 0.0f, 0.0f, 0.0f))
            return false;
    }
    self->m_currentX = self->m_subpathStartX;
    self->m_currentY = self->m_subpathStartY;
    self->m_requireMoveTo = true;
    return true;
}

bool XPainterPath_addRect(XPainterPath* self, const XRect* rect)
{
    float x1, y1, x2, y2;
    bool ok;
    if (!self || !rect) return false;
    /* QRectF::addRect 对 Null（宽高均为零）矩形是无操作；单轴为零或
       负尺寸仍会生成对应的退化/反向子路径。 */
    if (rect->width == 0 && rect->height == 0)
        return true;
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
    if (!self || !rect)
        return false;
    /* QRectF::isNull() 仅在宽高同时为零时成立；Qt 对负尺寸和单轴
       零尺寸仍建立退化椭圆子路径。 */
    if (rect->width == 0 && rect->height == 0)
        return true;
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
        /* 先提交 xs：realloc 可能已经释放旧地址；即使 ys 扩容
           失败，调用方也能沿着新 xs 指针统一释放现场。 */
        v->xs = nx;
        ny = (float*)XRealloc_System(v->ys, (size_t)newCapacity * sizeof(float));
        if (!ny) return false;
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

static bool painterPathFlattenCubic(PainterPathVertices* v,
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
        if (!painterPathVerticesPush(v, x, y)) return false;
    }
    return true;
}

static bool painterPathPointsEqual(float x1, float y1, float x2, float y2)
{
    return fabsf(x1 - x2) <= 1.0e-5f && fabsf(y1 - y2) <= 1.0e-5f;
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
                    if (!painterFillPolygonShape(self, v.m_count, v.xs, v.ys,
                                                 XPainterFillRule_OddEven))
                        goto fail;
                if (stroke && v.m_count >= 2 &&
                    !painterDrawPolyLineFloat(self, v.xs, v.ys, v.m_count, closed))
                    goto fail;
                /* Qt moveTo() 隐式结束前一子路径；新子路径不能复用旧顶点，
                   否则填充/描边会在两个几何体之间产生虚假连接。 */
                painterPathVerticesReset(&v);
                if (!painterPathVerticesPush(&v, e->m_x1, e->m_y1)) goto fail;
                closed = false;
                break;
            case XPainterPathElement_LineTo:
                if (!painterPathVerticesPush(&v, e->m_x1, e->m_y1)) goto fail;
                if (v.m_count >= 2 && painterPathPointsEqual(
                        v.xs[v.m_count - 1], v.ys[v.m_count - 1],
                        v.xs[0], v.ys[0]))
                    closed = true;
                break;
            case XPainterPathElement_CurveTo:
                if (v.m_count < 1 || i + 2 >= path->m_elementCount ||
                    path->m_elements[i + 1].m_type !=
                        XPainterPathElement_CurveToData ||
                    path->m_elements[i + 2].m_type !=
                        XPainterPathElement_CurveToData)
                    goto fail;
                px = v.xs[v.m_count - 1];
                py = v.ys[v.m_count - 1];
                if (!painterPathFlattenCubic(
                        &v, px, py, e->m_x1, e->m_y1,
                        path->m_elements[i + 1].m_x1,
                        path->m_elements[i + 1].m_y1,
                        path->m_elements[i + 2].m_x1,
                        path->m_elements[i + 2].m_y1))
                    goto fail;
                if (v.m_count >= 2 && painterPathPointsEqual(
                        v.xs[v.m_count - 1], v.ys[v.m_count - 1],
                        v.xs[0], v.ys[0]))
                    closed = true;
                i += 2;
                break;
            case XPainterPathElement_CurveToData:
                /* CurveToData 只能作为前一 CurveTo 的第二、第三元素出现。 */
                goto fail;
            default:
                break;
        }
    }
    if (fill && v.m_count >= 3)
        if (!painterFillPolygonShape(self, v.m_count, v.xs, v.ys,
                                     XPainterFillRule_OddEven))
            goto fail;
    if (stroke && v.m_count >= 2 &&
        !painterDrawPolyLineFloat(self, v.xs, v.ys, v.m_count, closed))
        goto fail;
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
    if (self && self->m_deviceKind != XPainterDevice_None)
    {
        self->m_state.m_penColor = color;
#if XPAINTER_PENSTYLE_ON
        /* QPainter::setPen(const QColor&) constructs a fresh QPen with
           SolidLine, width 1, SquareCap and BevelJoin defaults. */
        self->m_state.m_penStyle = XPainterPenStyle_SolidLine;
        self->m_state.m_penWidth = 1;
        self->m_state.m_penCap = XPainterPenCapStyle_SquareCap;
        self->m_state.m_penJoin = XPainterPenJoinStyle_BevelJoin;
#endif /* XPAINTER_PENSTYLE_ON */
        painterRecord_penState(self);
    }
}

#if XPAINTER_PENSTYLE_ON
void XPainter_setPen_2(XPainter* self, XPainterPenStyle style)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* QPen(Qt::PenStyle) 直接保存传入枚举，不对超出公开范围的数值做
       额外钳位；未知样式绘制时由后端按普通实线退化。 */
    self->m_state.m_penColor = 0xff000000u;
    self->m_state.m_penWidth = 1;
    self->m_state.m_penStyle = style;
    self->m_state.m_penCap = XPainterPenCapStyle_SquareCap;
    self->m_state.m_penJoin = XPainterPenJoinStyle_BevelJoin;
    painterRecord_penState(self);
}
#endif /* XPAINTER_PENSTYLE_ON */

uint32_t XPainter_penColor(const XPainter* self)
{
    return self ? self->m_state.m_penColor : 0u;
}

void XPainter_setPenWidth(XPainter* self, int width)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    /* 对齐 QPen::setWidth：0 表示 cosmetic pen，负值和 15 位以上宽度
       超出 Qt 允许范围时保持旧值不变；软件光栅绘制时再将 0 解释为
       一像素宽，保证状态值与实际显示宽度分别符合 Qt 语义。 */
    if (width < 0 || width >= (1 << 15))
        return;
    self->m_state.m_penWidth = width;
    painterRecord_penState(self);
}

int XPainter_penWidth(const XPainter* self)
{
    return self ? self->m_state.m_penWidth : 0;
}

void XPainter_setBrush(XPainter* self, uint32_t color)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    self->m_state.m_brushColor = color;
#if XPAINTER_BRUSH_ON
    self->m_state.m_brush.m_color = color;
    self->m_state.m_brush.m_style = XPainterBrushStyle_SolidPattern;
    /* QPainter::setBrush(const QColor&) replaces the brush object.  Do not
       leave an old gradient payload observable through XPainter_brush(). */
    memset(&self->m_state.m_brush.m_gradient, 0,
           sizeof(self->m_state.m_brush.m_gradient));
#endif /* XPAINTER_BRUSH_ON */
    painterRecord_brush(self);
}

#if XPAINTER_BRUSH_ON
void XPainter_setBrush_2(XPainter* self, XPainterBrushStyle style)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* QBrush(Qt::BrushStyle) 通过 qbrush_check_type 拒绝渐变和纹理
       类型；其他位值由 Qt 原样放入 brush style。 */
    if (style == XPainterBrushStyle_LinearGradientPattern ||
        style == XPainterBrushStyle_RadialGradientPattern ||
        style == XPainterBrushStyle_ConicalGradientPattern ||
        style == XPainterBrushStyle_TexturePattern)
        style = XPainterBrushStyle_NoBrush;
    self->m_state.m_brushColor = 0xff000000u;
    self->m_state.m_brush.m_color = 0xff000000u;
    self->m_state.m_brush.m_style = style;
    memset(&self->m_state.m_brush.m_gradient, 0,
           sizeof(self->m_state.m_brush.m_gradient));
    painterRecord_brush(self);
}
#endif /* XPAINTER_BRUSH_ON */

uint32_t XPainter_brushColor(const XPainter* self)
{
    return self ? self->m_state.m_brushColor : 0u;
}
#if XPAINTER_PENSTYLE_ON
void XPainter_setPenStyle(XPainter* self, XPainterPenStyle style)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* 对标 QPen::setStyle：Qt 不会替调用者校验枚举范围。 */
    self->m_state.m_penStyle = style;
    painterRecord_penState(self);
}

XPainterPenStyle XPainter_penStyle(const XPainter* self)
{
    return self ? self->m_state.m_penStyle : XPainterPenStyle_SolidLine;
}

void XPainter_setPenCapStyle(XPainter* self, XPainterPenCapStyle cap)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* 对标 QPen::setCapStyle：未知位值原样保留。 */
    self->m_state.m_penCap = cap;
    painterRecord_penState(self);
}

XPainterPenCapStyle XPainter_penCapStyle(const XPainter* self)
{
    return self ? self->m_state.m_penCap : XPainterPenCapStyle_SquareCap;
}

void XPainter_setPenJoinStyle(XPainter* self, XPainterPenJoinStyle join)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* 对标 QPen::setJoinStyle：未知位值原样保留。 */
    self->m_state.m_penJoin = join;
    painterRecord_penState(self);
}

XPainterPenJoinStyle XPainter_penJoinStyle(const XPainter* self)
{
    return self ? self->m_state.m_penJoin : XPainterPenJoinStyle_BevelJoin;
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
    int index;
    if (!g || !isfinite(position) || position < 0.0f || position > 1.0f)
        return;
    index = 0;
    while (index < g->m_stopCount &&
           g->m_stops[index].m_position < position)
        ++index;
    if (index < g->m_stopCount &&
        g->m_stops[index].m_position == position)
    {
        /* QGradient::setColorAt() replaces an existing stop at the same
           position instead of appending a duplicate. */
        g->m_stops[index].m_color = color;
        return;
    }
    if (g->m_stopCount >= XPAINTER_GRADIENT_MAX_STOPS)
        return;
    memmove(&g->m_stops[index + 1], &g->m_stops[index],
            (size_t)(g->m_stopCount - index) * sizeof(g->m_stops[0]));
    g->m_stops[index].m_position = position;
    g->m_stops[index].m_color = color;
    ++g->m_stopCount;
}

void XPainter_setBrushStyle(XPainter* self, XPainterBrushStyle style)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* QBrush::setStyle 使用同一个类型检查，渐变/纹理调用会被忽略；
       其余枚举值（包括未知值）原样保存。 */
    if (style == XPainterBrushStyle_LinearGradientPattern ||
        style == XPainterBrushStyle_RadialGradientPattern ||
        style == XPainterBrushStyle_ConicalGradientPattern ||
        style == XPainterBrushStyle_TexturePattern)
        return;
    /* QBrush::setStyle() 从渐变样式切换到普通样式时会分离为新的普通
       画刷数据。新数据不再包含渐变载荷，因此 XPainter_brush() 不能继续
       暴露旧的停止点和几何参数。 */
    if (self->m_state.m_brush.m_style == XPainterBrushStyle_LinearGradientPattern ||
        self->m_state.m_brush.m_style == XPainterBrushStyle_RadialGradientPattern ||
        self->m_state.m_brush.m_style == XPainterBrushStyle_ConicalGradientPattern)
    {
        memset(&self->m_state.m_brush.m_gradient, 0,
               sizeof(self->m_state.m_brush.m_gradient));
    }
    self->m_state.m_brush.m_style = style;
    painterRecord_brush(self);
}

XPainterBrushStyle XPainter_brushStyle(const XPainter* self)
{
    return self ? self->m_state.m_brush.m_style : XPainterBrushStyle_NoBrush;
}

void XPainter_setBrushGradient(XPainter* self, const XPainterGradient* gradient)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    if (!gradient)
    {
        self->m_state.m_brush.m_style = XPainterBrushStyle_SolidPattern;
        /* 无渐变时保持现有 QBrush 颜色；仅清空渐变载荷。 */
        memset(&self->m_state.m_brush.m_gradient, 0,
               sizeof(self->m_state.m_brush.m_gradient));
        /* Qt QPainter::setBrush() 会标记 DirtyBrush。渐变载荷不能由
           当前固定长度 Picture opcode 表达，但切回纯色样式可以安全
           记录；否则回放到已有渐变状态时会错误保留旧渐变。 */
        painterRecord_brush(self);
        return;
    }
    /* QBrush(const QGradient&) 以 QColor() 构造画刷，color().rgb() 按
       Qt 6.8 返回 opaque black (0xff000000)，而不是沿用旧画刷颜色。 */
    self->m_state.m_brushColor = 0xff000000u;
    self->m_state.m_brush.m_color = 0xff000000u;
    self->m_state.m_brush.m_gradient = *gradient;
    switch (gradient->m_type)
    {
        case XPainterGradientType_Radial:
            self->m_state.m_brush.m_style =
                XPainterBrushStyle_RadialGradientPattern;
            break;
        case XPainterGradientType_Conical:
            self->m_state.m_brush.m_style =
                XPainterBrushStyle_ConicalGradientPattern;
            break;
        case XPainterGradientType_Linear:
        default:
            self->m_state.m_brush.m_style =
                XPainterBrushStyle_LinearGradientPattern;
            break;
    }
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

#if XPAINTER_BRUSH_ORIGIN_ON
void XPainter_setBrushOrigin(XPainter* self, float x, float y)
{
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    self->m_state.m_brushOriginX = x;
    self->m_state.m_brushOriginY = y;
    /* Qt QPainter::setBrushOrigin() 每次调用都会标记 DirtyBrushOrigin；
       Picture 后端因此也要保留重复设置，不能按值去重。 */
    painterRecord_brushOrigin(self);
}

void XPainter_brushOrigin(const XPainter* self, XPoint* out)
{
    XPointF origin;
    if (!out)
        return;
    out->x = 0;
    out->y = 0;
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    origin.x = self->m_state.m_brushOriginX;
    origin.y = self->m_state.m_brushOriginY;
    /* XPointF_toPoint 与 Qt QPointF::toPoint 一样，对负数也采用对称
       四舍五入；painterRound() 的正数优化不适用于这里。 */
    *out = XPointF_toPoint(&origin);
}
#endif /* XPAINTER_BRUSH_ORIGIN_ON */



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
    /* 即使裁剪区域为空，Qt 仍保留其逻辑原点；继续映射退化矩形，
       让 clipBoundingRect() 能返回该原点和零宽高。 */
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
    if (operation == XPainterClipOperation_NoClip)
    {
        /* Qt replaces the clip history with this NoClip record.  The
           record remains visible to clipBoundingRect()/clipRegion(), while
           the operation marker prevents setClipping(true) from re-enabling
           it. */
        if (!painterMapClipRect(&self->m_state, rect, &mapped)) return;
        self->m_state.m_clipRect = mapped;
#if XPAINTER_CLIP_REGION_ON
        XRegion_clear(&self->m_state.m_clipRegion);
        XRegion_addRect(&self->m_state.m_clipRegion, &mapped);
#endif /* XPAINTER_CLIP_REGION_ON */
        self->m_state.m_hasClipRect = true;
        self->m_state.m_clipOperation = XPainterClipOperation_NoClip;
        self->m_state.m_hasClip = false;
        painterRecord_clipRect(self, rect, operation);
        return;
    }
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

#if XPAINTER_CLIP_REGION_ON
    if (operation == XPainterClipOperation_IntersectClip)
    {
        XRegion mappedRegion;
        XRegion_init(&mappedRegion);
        XRegion_addRect(&mappedRegion, &mapped);
        XRegion_intersected(&self->m_state.m_clipRegion, &mappedRegion,
                           &self->m_state.m_clipRegion);
        XRegion_deinit(&mappedRegion);
    }
    else
    {
        XRegion_clear(&self->m_state.m_clipRegion);
        if (operation != XPainterClipOperation_NoClip)
            XRegion_addRect(&self->m_state.m_clipRegion, &mapped);
    }
#endif /* XPAINTER_CLIP_REGION_ON */

    self->m_state.m_hasClipRect = true;
    self->m_state.m_clipOperation = operation;
    self->m_state.m_hasClip = operation != XPainterClipOperation_NoClip;
    painterRecord_clipRect(self, rect, operation);
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
    painterRecord_clipEnabled(self);
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
        !self->m_state.m_hasClipRect)
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

#if XPAINTER_CLIP_REGION_ON
void XPainter_setClipRegion(XPainter* self, const XRegion* region,
                            XPainterClipOperation operation)
{
    XRegion mapped;
    XRect mappedBounds;
    XImageTransform transform;
    if (!self || self->m_deviceKind == XPainterDevice_None || !region)
        return;
    if (operation != XPainterClipOperation_NoClip &&
        operation != XPainterClipOperation_ReplaceClip &&
        operation != XPainterClipOperation_IntersectClip)
        operation = XPainterClipOperation_ReplaceClip;
    if (!painterEffectiveTransform(&self->m_state, &transform))
        return;

    XRegion_init(&mapped);
    for (int i = 0; i < region->count; ++i)
    {
        XRect mappedRect;
        if (painterMapClipRect(&self->m_state, &region->rects[i],
                               &mappedRect))
            XRegion_addRect(&mapped, &mappedRect);
    }
    XRegion_boundingRect(&mapped, &mappedBounds);

    if (operation == XPainterClipOperation_NoClip)
    {
        /* Preserve the NoClip record for Qt-compatible clip queries while
           marking the operation as disabled for hasClipping(). */
        XRegion_clear(&self->m_state.m_clipRegion);
        XRegion_copy(&mapped, &self->m_state.m_clipRegion);
        self->m_state.m_clipRect = mappedBounds;
        self->m_state.m_hasClipRect = true;
        self->m_state.m_clipOperation = XPainterClipOperation_NoClip;
        self->m_state.m_hasClip = false;
        painterRecord_clipRegion(self, region, operation);
        XRegion_deinit(&mapped);
        return;
    }

    /* 与 Qt 非 Picture 引擎一致：未启用旧裁剪时，相交操作从替换开始。 */
    if (!self->m_state.m_hasClip &&
        operation == XPainterClipOperation_IntersectClip)
        operation = XPainterClipOperation_ReplaceClip;
    if (operation == XPainterClipOperation_IntersectClip)
        XRegion_intersected(&self->m_state.m_clipRegion, &mapped,
                            &self->m_state.m_clipRegion);
    else
    {
        XRegion_clear(&self->m_state.m_clipRegion);
        if (operation != XPainterClipOperation_NoClip)
            XRegion_copy(&mapped, &self->m_state.m_clipRegion);
    }
    if (operation == XPainterClipOperation_IntersectClip)
        XRegion_boundingRect(&self->m_state.m_clipRegion, &mappedBounds);
    self->m_state.m_clipRect = mappedBounds;
    self->m_state.m_hasClipRect = true;
    self->m_state.m_clipOperation = operation;
    self->m_state.m_hasClip = operation != XPainterClipOperation_NoClip;
    painterRecord_clipRegion(self, region, operation);
    XRegion_deinit(&mapped);
}

void XPainter_clipRegion(const XPainter* self, XRegion* out)
{
    XImageTransform transform;
    XImageTransform inverse;
    if (!out) return;
    XRegion_clear(out);
    if (!self || self->m_deviceKind == XPainterDevice_None ||
        !self->m_state.m_hasClipRect ||
        !painterEffectiveTransform(&self->m_state, &transform) ||
        !painterMatrixInvert(&transform, &inverse))
        return;
    for (int i = 0; i < self->m_state.m_clipRegion.count; ++i)
    {
        float minX;
        float minY;
        float maxX;
        float maxY;
        XRect logical;
        if (!painterMapRectCorners(&inverse,
                                   &self->m_state.m_clipRegion.rects[i],
                                   &minX, &minY, &maxX, &maxY))
            continue;
        logical.x = painterFloorInt(minX);
        logical.y = painterFloorInt(minY);
        {
            int right = painterCeilInt(maxX);
            int bottom = painterCeilInt(maxY);
            int64_t width = (int64_t)right - logical.x;
            int64_t height = (int64_t)bottom - logical.y;
            logical.width = width > INT_MAX ? INT_MAX : (width > 0 ? (int)width : 0);
            logical.height = height > INT_MAX ? INT_MAX : (height > 0 ? (int)height : 0);
        }
        XRegion_addRect(out, &logical);
    }
}
#endif /* XPAINTER_CLIP_REGION_ON */
#endif /* XPAINTER_CLIP_ON */
void XPainter_setTransform(XPainter* self, const XImageTransform* matrix,
                           bool combine)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !matrix)
        return;
    if (combine)
        /* QPainter::setWorldTransform(matrix, true) 使用 matrix * current；
           translate/scale 等便捷操作同样将新变换左乘到当前矩阵。 */
        self->m_state.m_transform =
            painterMatrixMultiply(matrix, &self->m_state.m_transform);
    else
        self->m_state.m_transform = *matrix;
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    painterRecord_transform(self);
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
    painterRecord_transform(self);
#if XPAINTER_VIEW_TRANSFORM_ON
    /* resetTransform() also resets the embedded window/viewport contract;
       emit those fixed records after the matrix record so Picture replay
       reaches the same final state. */
    painterRecord_window(self);
    painterRecord_viewport(self);
    painterRecord_viewTransformEnabled(self);
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
}

void XPainter_translate(XPainter* self, float dx, float dy)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterTranslation(dx, dy);
    self->m_state.m_transform =
        painterMatrixMultiply(&t, &self->m_state.m_transform);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    painterRecord_transform(self);
}

void XPainter_scale(XPainter* self, float sx, float sy)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterScale(sx, sy);
    self->m_state.m_transform =
        painterMatrixMultiply(&t, &self->m_state.m_transform);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    painterRecord_transform(self);
}

void XPainter_rotate(XPainter* self, float degrees)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterRotation(degrees);
    self->m_state.m_transform =
        painterMatrixMultiply(&t, &self->m_state.m_transform);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    painterRecord_transform(self);
}

void XPainter_shear(XPainter* self, float sh, float sv)
{
    XImageTransform t;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    t = painterShear(sh, sv);
    self->m_state.m_transform =
        painterMatrixMultiply(&t, &self->m_state.m_transform);
#if XPAINTER_WORLD_MATRIX_ON
    self->m_state.m_worldMatrixEnabled = true;
#endif /* XPAINTER_WORLD_MATRIX_ON */
    painterRecord_transform(self);
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
    if (self->m_state.m_worldMatrixEnabled == enabled)
        return;
    self->m_state.m_worldMatrixEnabled = enabled;
    painterRecord_transform(self);
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

void XPainter_deviceTransform(const XPainter* self, XImageTransform* out)
{
    /* 内置设备均以 (0,0) 为原点，没有平台窗口附加偏移。 */
    XPainter_combinedTransform(self, out);
}

#if XPAINTER_VIEW_TRANSFORM_ON
void XPainter_setWindow(XPainter* self, const XRect* window)
{
    if (!self || self->m_deviceKind == XPainterDevice_None || !window)
        return;
    self->m_state.m_window = *window;
    self->m_state.m_viewTransformEnabled = true;
    painterRecord_window(self);
    /* Picture 录制器的默认 viewport 可能是零矩形，而回放到 XImage
       时目标设备会有自己的默认 viewport；同步另一侧状态，避免单独
       setWindow() 时错误继承回放目标的默认值。 */
    painterRecord_viewport(self);
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
    painterRecord_viewport(self);
    /* 同理，保留录制时的逻辑 window，确保单独 setViewport() 的 Picture
       在不同设备尺寸上仍得到同一组合变换。 */
    painterRecord_window(self);
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
    if (self->m_state.m_viewTransformEnabled == enabled)
        return;
    self->m_state.m_viewTransformEnabled = enabled;
    painterRecord_viewTransformEnabled(self);
}

bool XPainter_viewTransformEnabled(const XPainter* self)
{
    return self && self->m_deviceKind != XPainterDevice_None &&
           self->m_state.m_viewTransformEnabled;
}
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

void XPainter_setOpacity(XPainter* self, float opacity)
{
    float oldOpacity;
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* qBound(0, qMin(1, NaN)) 的 Qt 6.8 结果为 0。 */
    if (isnan(opacity)) opacity = 0.0f;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    oldOpacity = self->m_state.m_opacity;
    if (opacity == oldOpacity)
        return;
    self->m_state.m_opacity = opacity;
    painterRecord_opacity(self);
}

float XPainter_opacity(const XPainter* self)
{
    /* QPainter::opacity() 在未激活时返回默认不透明度 1.0；空指针
       保护也采用该默认值，避免把无效对象误报为完全透明。 */
    return self ? self->m_state.m_opacity : 1.0f;
}

void XPainter_setCompositionMode(XPainter* self, XPainterCompositionMode mode)
{
    if (!self || self->m_deviceKind == XPainterDevice_None) return;
    /* XImage 软件后端覆盖 Qt 6.8 的全部 38 个合成模式；枚举范围
       之外的值保持原状态。RasterOp 对完整 ARGB32 像素逐位运算。 */
    if (mode < XPainterCompositionMode_SourceOver ||
        mode > XPainterCompositionMode_RasterOp_NotDestination)
        return;
    if (mode == self->m_state.m_compositionMode)
        return;
    self->m_state.m_compositionMode = mode;
    painterRecord_compositionMode(self);
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
    painterRecord_renderHints(self);
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
    painterRecord_renderHints(self);
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
    /* Qt 6.8 的 QPainter 在 begin() 前没有 QPainterState；未激活调用
       会被忽略，end() 后同样回到 fakeState 的 Auto。 */
    if (!self || self->m_deviceKind == XPainterDevice_None)
        return;
    if (direction < XPainterLayoutDirection_LeftToRight ||
        direction > XPainterLayoutDirection_Auto)
        direction = XPainterLayoutDirection_Auto;
    self->m_state.m_layoutDirection = direction;
}

XPainterLayoutDirection XPainter_layoutDirection(const XPainter* self)
{
    /* Qt returns Auto when no QPainterState exists; XPainter's inactive
       device marker is the equivalent state check. */
    return (self && self->m_deviceKind != XPainterDevice_None)
               ? self->m_state.m_layoutDirection
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
    bool wrapAnywhere;
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
    lineCap = (int)(sizeof(lines) / sizeof(lines[0]));
    wrap = (flags & XPAINTER_TEXT_WORD_WRAP) != 0u;
    wrapAnywhere = (flags & XPAINTER_TEXT_WRAP_ANYWHERE) != 0u;
    /* Qt 只有显式启用 WordWrap/WrapAnywhere（或强制两端对齐）时才
       将布局宽度传给 QTextLayout；普通 drawTextRect 保持单行并交给
       默认裁剪处理溢出文本。 */
    if (wrap || wrapAnywhere ||
        (flags & XPAINTER_TEXT_JUSTIFICATION_FORCED) != 0u)
    {
        maxChars = rect->width / charW;
        if (maxChars < 1) maxChars = 1;
    }
    else
    {
        maxChars = INT_MAX;
    }
    singleLine = (flags & XPAINTER_TEXT_SINGLE_LINE) != 0u;
    isRTL = false;
    if ((flags & XPAINTER_TEXT_FORCE_RIGHT_TO_LEFT) != 0u)
        isRTL = true;
    else if ((flags & XPAINTER_TEXT_FORCE_LEFT_TO_RIGHT) == 0u)
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
        if (curCount >= maxChars && !singleLine &&
            (wrap || wrapAnywhere ||
             (flags & XPAINTER_TEXT_JUSTIFICATION_FORCED) != 0u))
        {
            if (wrap && !wrapAnywhere)
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
