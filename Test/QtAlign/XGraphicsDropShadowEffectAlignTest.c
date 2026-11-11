/**
 * @file       XGraphicsDropShadowEffectAlignTest.c
 * @brief      XGraphicsDropShadowEffect 与 Qt 6.8 同名类对齐契约测试。
 * @details    对齐依据：本机 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *             qgraphicseffect.h:159-213 与 Qt 6.8 探针实测。
 *
 *             **包围盒公式来自实测**（源 (10,20,100,50)）：
 *                 rect ∪ (rect 外扩 blurRadius 后平移 offset)
 *             采样证据：
 *                 r=1 off=(8,8)     → (10,20,109,59)     右/下 +9，左上不动
 *                 r=0 off=(8,8)     → (10,20,108,58)     外扩 0，仅平移
 *                 r=0 off=(-8,-8)   → (2,12,108,58)
 *                 r=1 off=(-3.5,12) → (5.5,20,104.5,63)
 *                 r=4 off=(-3.5,12) → (2.5,20,108,66)
 *                 r=2.5 off=(8,8)   → (10,20,110.5,60.5)
 *                 r=0 off=(0,0)     → (10,20,100,50)     恒等
 *             即"并集"语义，**不是**四周对称外扩（对称外扩会把正偏移方向的
 *             左侧也撑大，与 Qt 实测不符）。
 *             负半径同样按 Qt 源码公式
 *             （qpixmapfilter.cpp:965-970 的 `rect.united(rect.translated
 *             (offset).adjusted(-r,-r,r,r))`）参与并集：
 *                 r=-1 off=(8,8) → (10,20,107,57)
 *                 r=-2 off=(8,8) → (10,20,106,56)
 *                 r=-5 off=(8,8) → (10,20,103,53)
 *
 *             渲染语义（Qt 6.8 源码 + 探针）：
 *             - blurRadius <= 0 且 offset 为零 ⇒ 直接绘源
 *               （qgraphicseffect.cpp:1017）；
 *             - 指数模糊只滤 alpha、半径用 blurRadius 原值（不乘 2.5），
 *               随后以投影颜色着色（SourceIn），再叠绘源；
 *             - blurRadius 负值照存（Qt 不钳制），滤波按 0 处理：投影保持
 *               硬边、包围盒按上式收缩（探针 r=-1/-2/-4 投影内部恒为实色）。
 *
 *             缺省值（Qt 6.8 构造）：offset=(8,8)、blurRadius=1.0、
 *             color=QColor(63,63,63,180)。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsDropShadowEffect.h"
#include "XObject.h"
#include "XVarList.h"
#include "XWidget.h"
#include "XImage.h"
#include "XColor.h"
#include "XClass.h"

#include <math.h>

/** @brief 浮点断言容差。 */
#define XQ_SHADOW_EPS 0.0005f

/* ==================== 探针槽函数 ==================== */

static int s_offsetCount = 0;   /**< offsetChanged 次数。 */
static XPointF s_offsetLast;    /**< 最近一次偏移载荷。 */
static int s_radiusCount = 0;   /**< blurRadiusChanged 次数。 */
static int s_colorCount = 0;    /**< colorChanged 次数。 */

/** @brief offsetChanged(XPointF) 槽。 */
static void xq_shadowOffsetSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, XPointF, offset);
        ++s_offsetCount;
        s_offsetLast = offset;
    }
}

/** @brief blurRadiusChanged(float) 槽。 */
static void xq_shadowRadiusSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_radiusCount;
}

/** @brief colorChanged(XColor) 槽。 */
static void xq_shadowColorSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_colorCount;
}

/** @brief 断言矩形四要素（带容差）。 */
static void xq_shadowExpectRect(const XRectF* got, float x, float y, float w,
                                float h, const char* what)
{
    XQ_CHECK_MSG(fabsf(got->x - x) <= XQ_SHADOW_EPS,
                 "%s：x 期望 %.3f 实得 %.3f", what, (double)x,
                 (double)got->x);
    XQ_CHECK_MSG(fabsf(got->y - y) <= XQ_SHADOW_EPS,
                 "%s：y 期望 %.3f 实得 %.3f", what, (double)y,
                 (double)got->y);
    XQ_CHECK_MSG(fabsf(got->width - w) <= XQ_SHADOW_EPS,
                 "%s：width 期望 %.3f 实得 %.3f", what, (double)w,
                 (double)got->width);
    XQ_CHECK_MSG(fabsf(got->height - h) <= XQ_SHADOW_EPS,
                 "%s：height 期望 %.3f 实得 %.3f", what, (double)h,
                 (double)got->height);
}

/* ==================== ① 包围盒 ==================== */

/**
 * @brief boundingRectFor = 源矩形 ∪（外扩 blurRadius 后平移 offset）。
 * @details 逐条对齐 Qt 6.8 实测采样（见文件头）。
 */
XQ_TEST(XGraphicsDropShadowEffect, boundingRectFormula)
{
    XGraphicsDropShadowEffect* shadow = XGraphicsDropShadowEffect_create();
    XRectF src;
    XRectF got;
    XPointF offset;
    XQ_REQUIRE(shadow != NULL);

    src.x = 10.0f;
    src.y = 20.0f;
    src.width = 100.0f;
    src.height = 50.0f;

    /* 缺省：offset(8,8)、blurRadius 1.0 → (10,20,109,59)。 */
    {
        XPointF def = XGraphicsDropShadowEffect_offset(shadow);
        XQ_CHECK(def.x == 8.0f && def.y == 8.0f);
        XQ_CHECK(XGraphicsDropShadowEffect_blurRadius(shadow) == 1.0f);
    }
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 109.0f, 59.0f, "缺省 (8,8) r=1");

    /* r=0 + 正偏移：只有平移，外扩为 0。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 0.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 108.0f, 58.0f, "r=0 off=(8,8)");

    /* r=0 + 负偏移：向左上撑开。 */
    XGraphicsDropShadowEffect_setOffset_2(shadow, -8.0f, -8.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 2.0f, 12.0f, 108.0f, 58.0f, "r=0 off=(-8,-8)");

    /* r=1 + 非对称偏移：左侧 -4.5、下侧 +13（Qt 采样值）。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 1.0f);
    XGraphicsDropShadowEffect_setOffset_2(shadow, -3.5f, 12.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 5.5f, 20.0f, 104.5f, 63.0f, "r=1 off=(-3.5,12)");

    /* r=4：外扩 4 后再平移。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 4.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 2.5f, 20.0f, 108.0f, 66.0f, "r=4 off=(-3.5,12)");

    /* r=0 且偏移为零：恒等返回（Qt 采样值）。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 0.0f);
    XGraphicsDropShadowEffect_setOffset_3(shadow, 0.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 100.0f, 50.0f, "r=0 off=(0,0)");

    /* 小数半径：r=2.5 off=(8,8) → (10,20,110.5,60.5)。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 2.5f);
    XGraphicsDropShadowEffect_setOffset_2(shadow, 8.0f, 8.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 110.5f, 60.5f, "r=2.5 off=(8,8)");

    /* 反方向：r=2.5 off=(-8,-8) → (-0.5,9.5,110.5,60.5)。 */
    XGraphicsDropShadowEffect_setOffset_2(shadow, -8.0f, -8.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, -0.5f, 9.5f, 110.5f, 60.5f, "r=2.5 off=(-8,-8)");

    /* 负半径：外扩量变负 ⇒ 并集里"平移后的矩形"收缩（Qt 探针实测
       r=-1 → (10,20,107,57)、r=-2 → (10,20,106,56)、
       r=-5 → (10,20,103,53)）。 */
    XGraphicsDropShadowEffect_setOffset_2(shadow, 8.0f, 8.0f);
    XGraphicsDropShadowEffect_setBlurRadius(shadow, -1.0f);
    XQ_CHECK_MSG(XGraphicsDropShadowEffect_blurRadius(shadow) == -1.0f,
                 "投影 setBlurRadius 负值照存（Qt 不钳制）");
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 107.0f, 57.0f, "r=-1 off=(8,8)");
    XGraphicsDropShadowEffect_setBlurRadius(shadow, -2.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 106.0f, 56.0f, "r=-2 off=(8,8)");
    XGraphicsDropShadowEffect_setBlurRadius(shadow, -5.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, &src);
    xq_shadowExpectRect(&got, 10.0f, 20.0f, 103.0f, 53.0f, "r=-5 off=(8,8)");

    /* NULL 源矩形返回空矩形。 */
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)shadow, NULL);
    XQ_CHECK(got.width == 0.0f && got.height == 0.0f);

    XClassDelete((XClass*)shadow);
}

/* ==================== ② setOffset 重载族与信号 ==================== */

/**
 * @brief offset 三个重载 + X/Y 分量存取 + 三类属性信号。
 * @details Qt 6.8 实测：
 *          setOffset(3)      → (3,3)
 *          setXOffset(-2)    → (-2,3)   只改 X、Y 保持
 *          setYOffset(9)     → (-2,9)
 *          xOffset()/yOffset() 与 offset() 的分量一致；
 *          同值 setOffset 不发射 offsetChanged。
 */
XQ_TEST(XGraphicsDropShadowEffect, offsetOverloads)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XObject* probe = XObject_create();
    XGraphicsDropShadowEffect* shadow = XGraphicsDropShadowEffect_create();
    XPointF offset;
    XColor color;
    XRectF bounds;
    XQ_REQUIRE(widget != NULL && probe != NULL && shadow != NULL);

    s_offsetCount = 0;
    s_radiusCount = 0;
    s_colorCount = 0;

    /* 缺省颜色：QColor(63,63,63,180)。 */
    color = XGraphicsDropShadowEffect_color(shadow);
    XQ_CHECK(XColor_red(&color) == 63 && XColor_green(&color) == 63);
    XQ_CHECK(XColor_blue(&color) == 63 && XColor_alpha(&color) == 180);

    XObject_connect_1((XObject*)shadow,
                      XSignal(XGraphicsDropShadowEffect_offsetChanged_signal),
                      probe, xq_shadowOffsetSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)shadow,
                      XSignal(XGraphicsDropShadowEffect_blurRadiusChanged_signal),
                      probe, xq_shadowRadiusSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)shadow,
                      XSignal(XGraphicsDropShadowEffect_colorChanged_signal),
                      probe, xq_shadowColorSlot, XConnectionType_Direct);

    /* setOffset(qreal d) 重载：两个方向同值。 */
    XGraphicsDropShadowEffect_setOffset_3(shadow, 3.0f);
    XQ_EQ_I64(s_offsetCount, 1);
    XQ_CHECK(s_offsetLast.x == 3.0f && s_offsetLast.y == 3.0f);
    offset = XGraphicsDropShadowEffect_offset(shadow);
    XQ_CHECK(offset.x == 3.0f && offset.y == 3.0f);

    /* setXOffset：只改 X，Y 保持 3。 */
    XGraphicsDropShadowEffect_setXOffset(shadow, -2.0f);
    XQ_EQ_I64(s_offsetCount, 2);
    offset = XGraphicsDropShadowEffect_offset(shadow);
    XQ_CHECK(offset.x == -2.0f && offset.y == 3.0f);
    XQ_CHECK(XGraphicsDropShadowEffect_xOffset(shadow) == -2.0f);

    /* setYOffset：只改 Y。 */
    XGraphicsDropShadowEffect_setYOffset(shadow, 9.0f);
    offset = XGraphicsDropShadowEffect_offset(shadow);
    XQ_CHECK(offset.x == -2.0f && offset.y == 9.0f);
    XQ_CHECK(XGraphicsDropShadowEffect_yOffset(shadow) == 9.0f);
    XQ_EQ_I64(s_offsetCount, 3);

    /* setOffset(qreal dx, qreal dy) 重载。 */
    XGraphicsDropShadowEffect_setOffset_2(shadow, 4.0f, -5.0f);
    offset = XGraphicsDropShadowEffect_offset(shadow);
    XQ_CHECK(offset.x == 4.0f && offset.y == -5.0f);
    XQ_EQ_I64(s_offsetCount, 4);

    /* 同值早退：不发射。 */
    XGraphicsDropShadowEffect_setOffset_2(shadow, 4.0f, -5.0f);
    XQ_EQ_I64(s_offsetCount, 4);

    /* 半径与颜色：变化发射，同值早退。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 4.0f);
    XQ_CHECK(XGraphicsDropShadowEffect_blurRadius(shadow) == 4.0f);
    XQ_EQ_I64(s_radiusCount, 1);
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 4.0f);
    XQ_EQ_I64(s_radiusCount, 1);

    XColor_init_rgb(&color, 255, 0, 0, 200);
    XGraphicsDropShadowEffect_setColor(shadow, color);
    XQ_EQ_I64(s_colorCount, 1);
    color = XGraphicsDropShadowEffect_color(shadow);
    XQ_CHECK(XColor_red(&color) == 255 && XColor_alpha(&color) == 200);
    XGraphicsDropShadowEffect_setColor(shadow, color);
    XQ_EQ_I64(s_colorCount, 1);

    /* 挂接后包围盒以控件尺寸为源：widget 40x30、offset(4,-5)、r=4。
       源 (0,0,40,30)；外扩 4 后 (-4,-4,48,38)；平移 (4,-5) 后 (0,-9,48,38)；
       并集 → (0,-9,48,39)。 */
    XWidget_resize(widget, 40, 30);
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)shadow);
    bounds = XGraphicsEffect_boundingRect((XGraphicsEffect*)shadow);
    xq_shadowExpectRect(&bounds, 0.0f, -9.0f, 48.0f, 39.0f,
                        "控件 40x30 off=(4,-5) r=4");

    /* NULL 安全。 */
    XQ_CHECK(XGraphicsDropShadowEffect_blurRadius(NULL) == 1.0f);
    XQ_CHECK(XGraphicsDropShadowEffect_xOffset(NULL) == 0.0f);
    XQ_CHECK(XGraphicsDropShadowEffect_yOffset(NULL) == 0.0f);
    XGraphicsDropShadowEffect_setOffset_2(NULL, 1.0f, 1.0f);
    XGraphicsDropShadowEffect_setXOffset(NULL, 1.0f);
    XGraphicsDropShadowEffect_setYOffset(NULL, 1.0f);

    XClassDelete((XClass*)widget); /* 带走 shadow */
    XClassDelete((XClass*)probe);
}

/* ==================== ③ Draw 槽位：alpha 模糊 + 着色 + 硬边阈值 ==================== */

/** @brief 取图像某点的 alpha。 */
static int xq_shadowAlphaAt(const XImage* image, int x, int y)
{
    XColor c = XImage_pixelColor(image, x, y);
    return (int)XColor_alpha(&c);
}

/** @brief 构造 WxH 的 ARGB32_Premultiplied 图像并填 0。 */
static XImage* xq_shadowMakeImage(int w, int h)
{
    XImage* image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image) return NULL;
    if (!XImage_reinit_ex(image, w, h, XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(image);
        return NULL;
    }
    XImage_fillRect(image, NULL, 0u);
    return image;
}

/**
 * @brief 投影渲染：默认半径下投影半透明、源覆盖其上；零半径零偏移直绘源；
 *        负半径按不模糊处理且包围盒收缩。
 * @details 对齐 Qt QPixmapDropShadowFilter::draw（qpixmapfilter.cpp:975-1010）：
 *          ① 以源 alpha 在 offset 处生成形状 → qt_blurImage(radius,
 *             alphaOnly=true) → SourceIn 填投影色 → 先投影后源；
 *          ② qgraphicseffect.cpp:1017：`blurRadius <= 0 && offset.isNull()`
 *             直接 drawSource（不建投影层）；
 *          ③ 负半径：Qt 定点式在负 alpha 下的可观测结果是"不模糊 + 包围盒
 *             收缩"（本机探针 r=-1/-2/-4 投影内部恒为实色硬边），本实现
 *             以 max(0,r) 作滤波半径复现该结果。
 *          画布由调用方按 boundingRectFor 给出（与管线一致）：
 *             源 (0,0,20,14)、r=1/off(8,8) → (0,0,29,23)；
 *             源 (0,0,20,14)、r=0/off(0,0)  → (0,0,20,14)；
 *             源 (0,0,20,14)、r=-2/off(8,8) → (0,0,26,20)。
 */
XQ_TEST(XGraphicsDropShadowEffect, drawAlphaBlurAndColor)
{
    XGraphicsDropShadowEffect* shadow = XGraphicsDropShadowEffect_create();
    XGraphicsEffect_DrawSlot slot;
    XImage* src = xq_shadowMakeImage(20, 14);
    XImage* dest;
    XGraphicsEffectDrawContext ctx;
    XQ_REQUIRE(shadow != NULL && src != NULL);
    XImage_fillRect(src, NULL, 0xFFFF0000u); /* 源 = 全幅不透明红 */
    XQ_REQUIRE(XClassGetVtable((XClass*)shadow) != NULL);
    slot = XClassGetVirtualFunc((XGraphicsEffect*)shadow,
                                EXGraphicsEffect_Draw,
                                XGraphicsEffect_DrawSlot);
    XQ_REQUIRE(slot != NULL);

    ctx.m_source = src;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 20;
    ctx.m_sourceRect.height = 14;

    /* ① 缺省 r=1、off=(8,8)：画布 29x23。 */
    dest = xq_shadowMakeImage(29, 23);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 29;
    ctx.m_destRect.height = 23;
    ((XGraphicsEffect*)shadow)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)shadow, &ctx);
    ((XGraphicsEffect*)shadow)->m_drawCtx = NULL;
    {
        XColor srcPx = XImage_pixelColor(dest, 10, 7);
        XColor shPx = XImage_pixelColor(dest, 24, 18);
        int shA = xq_shadowAlphaAt(dest, 24, 18);
        XQ_CHECK_MSG(XColor_alpha(&srcPx) == 255 && XColor_red(&srcPx) == 255,
                     "源必须叠绘在投影之上且保持不透明");
        XQ_CHECK_MSG(shA >= 170 && shA <= 190,
                     "投影内部 alpha≈180（color alpha 180，实得 %d）", shA);
        /* 画布是 ARGB32_Premultiplied：存储值为 63×180/255 ≈ 44。
         * XImage_pixelColor 与 Qt QImage::pixelColor 同口径——**反预乘**
         * 后返回 ≈62（本机 Qt 6.8.0 探针：QImage(ARGB32_Premultiplied)
         * 填 QColor(63,63,63,180) → pixelColor=(62,62,62,180)、
         * pixel()=0xb42c2c2c）。修复前注释误称 pixelColor 直读预乘值，
         * 才把正确的 62 判成偏差。故这里双层断言：
         *   ① 原始存储通道 = 预乘值 44（真正验证着色预乘正确）；
         *   ② pixelColor 反预乘读回 ≈62（与 Qt 实测一致）。 */
        {
            const uint32_t* bits = (const uint32_t*)(const void*)XImage_bits(dest);
            int stride = XImage_bytesPerLine(dest) / 4;
            uint32_t raw = bits && stride > 0 ? bits[18 * stride + 24] : 0u;
            int rawR = (int)((raw >> 16) & 0xFFu);
            XQ_CHECK_MSG(rawR >= 40 && rawR <= 50,
                         "投影存储通道应为预乘值≈44（实得 %d）", rawR);
        }
        XQ_CHECK_MSG(XColor_red(&shPx) >= 58 && XColor_red(&shPx) <= 66,
                     "pixelColor 反预乘后 ≈62（Qt 探针实测 62，实得 r=%d）",
                     (int)XColor_red(&shPx));
        XQ_CHECK_MSG(xq_shadowAlphaAt(dest, 25, 0) == 0,
                     "投影上沿之外必须全透明");
    }
    XClassDelete((XClass*)dest);

    /* ② r=0、off=(0,0)：直接绘源（画布 = 源矩形，全不透明）。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, 0.0f);
    XGraphicsDropShadowEffect_setOffset_3(shadow, 0.0f);
    dest = xq_shadowMakeImage(20, 14);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 20;
    ctx.m_destRect.height = 14;
    ((XGraphicsEffect*)shadow)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)shadow, &ctx);
    ((XGraphicsEffect*)shadow)->m_drawCtx = NULL;
    XQ_CHECK_MSG(xq_shadowAlphaAt(dest, 0, 0) == 255 &&
                     xq_shadowAlphaAt(dest, 19, 13) == 255,
                 "零半径零偏移 = 直接绘源（qgraphicseffect.cpp:1017）");
    XClassDelete((XClass*)dest);

    /* ③ r=-2、off=(8,8)：不模糊（硬边）、画布按并集收缩为 26x20。 */
    XGraphicsDropShadowEffect_setBlurRadius(shadow, -2.0f);
    XGraphicsDropShadowEffect_setOffset_2(shadow, 8.0f, 8.0f);
    dest = xq_shadowMakeImage(26, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 26;
    ctx.m_destRect.height = 20;
    ((XGraphicsEffect*)shadow)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)shadow, &ctx);
    ((XGraphicsEffect*)shadow)->m_drawCtx = NULL;
    XQ_CHECK_MSG(xq_shadowAlphaAt(dest, 24, 18) == 180 &&
                     xq_shadowAlphaAt(dest, 25, 19) == 180,
                 "负半径不模糊：投影内部/边缘 alpha 恒为 180（硬边）");
    XQ_CHECK_MSG(xq_shadowAlphaAt(dest, 25, 0) == 0,
                 "负半径画布之外（投影上沿）必须透明");
    XClassDelete((XClass*)dest);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)shadow);
}
