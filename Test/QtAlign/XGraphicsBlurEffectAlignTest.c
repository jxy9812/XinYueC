/**
 * @file       XGraphicsBlurEffectAlignTest.c
 * @brief      XGraphicsBlurEffect 与 Qt 6.8 QGraphicsBlurEffect 对齐契约测试。
 * @details    对齐依据：本机 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *             qgraphicseffect.h:118-157、Qt 6.8 源码
 *             （src/widgets/effects/qpixmapfilter.cpp 与
 *             src/gui/painting/qimageeffects.cpp）与 6.8 探针实测。
 *
 *             **包围盒公式 = Qt 源码原文**（qpixmapfilter.cpp:545-555）：
 *                 delta = radiusScale(2.5) * radius + 1
 *                 return rect.adjusted(-delta, -delta, delta, delta);
 *             实测与源码逐点一致（源矩形 (10,20,100,50)）：
 *                 r=0 → 1.000   r=0.5 → 2.250  r=1 → 3.500
 *                 r=2 → 6.000   r=5 → 13.500  r=100 → 251.000
 *                 r=-0.5 → -0.25（收缩）  r=-2 → -4.000  r=-5 → -11.500
 *             blurHints 三档不改变包围盒。
 *
 *             渲染语义（本机 6.8 探针）：
 *             - 缺省半径 5.0（QPixmapBlurFilterPrivate 的 radius(5)，探针实测
 *               QGraphicsBlurEffect().blurRadius()==5.0）；
 *             - r=0/0.5/1 与无效果画面**逐像素相同**（QPixmapBlurFilter::draw
 *               在 radius<=1 时直接绘源）；r=1.25 起可见扩散；
 *             - 负半径**不被钳制**（探针 setBlurRadius(-3) → blurRadius()=-3），
 *               包围盒收缩导致源被裁边；r 极负到包围盒为空时控件整块不画。
 *
 *             实现侧同 Qt 算法的指数滤波（XGraphicsEffect_blurImage），
 *             绘制半径 = 2.5 × blurRadius；QualityHint 走半半径两遍。
 *             旧契约「负值钳 0 / 固定 3x3 核」已按 Qt 语义改写（见
 *             testMigrations：Test/XGuiDemo/xgui_demo_apitest_core.c:1097/
 *             1104 等旧断言需迁移）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsBlurEffect.h"
#include "XObject.h"
#include "XVarList.h"
#include "XWidget.h"
#include "XImage.h"
#include "XColor.h"
#include "XPalette.h"
#include "XClass.h"

#include <math.h>

/** @brief 浮点断言容差（Qt 探针打印三位小数，四舍误差远小于此值）。 */
#define XQ_BLUR_EPS 0.0005f

/* ==================== 探针槽函数 ==================== */

static int s_radiusCount = 0;      /**< blurRadiusChanged 次数。 */
static float s_radiusLast = 0.0f;  /**< 最近一次半径载荷。 */
static int s_hintsCount = 0;       /**< blurHintsChanged 次数。 */
static int s_hintsLast = 0;        /**< 最近一次提示载荷。 */

/** @brief blurRadiusChanged(float) 槽。 */
static void xq_blurRadiusSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, float, radius);
        ++s_radiusCount;
        s_radiusLast = radius;
    }
}

/** @brief blurHintsChanged(BlurHints) 槽（仓库以 int 承载位或组合）。 */
static void xq_blurHintsSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, int, hints);
        ++s_hintsCount;
        s_hintsLast = hints;
    }
}

/** @brief 断言矩形四要素（带容差）。 */
static void xq_blurExpectRect(const XRectF* got, float x, float y, float w,
                              float h, const char* what)
{
    XQ_CHECK_MSG(fabsf(got->x - x) <= XQ_BLUR_EPS, "%s：x 期望 %.3f 实得 %.3f",
                 what, (double)x, (double)got->x);
    XQ_CHECK_MSG(fabsf(got->y - y) <= XQ_BLUR_EPS, "%s：y 期望 %.3f 实得 %.3f",
                 what, (double)y, (double)got->y);
    XQ_CHECK_MSG(fabsf(got->width - w) <= XQ_BLUR_EPS,
                 "%s：width 期望 %.3f 实得 %.3f", what, (double)w,
                 (double)got->width);
    XQ_CHECK_MSG(fabsf(got->height - h) <= XQ_BLUR_EPS,
                 "%s：height 期望 %.3f 实得 %.3f", what, (double)h,
                 (double)got->height);
}

/** @brief 取图像某点的 alpha。 */
static int xq_blurAlphaAt(const XImage* image, int x, int y)
{
    XColor c = XImage_pixelColor(image, x, y);
    return (int)XColor_alpha(&c);
}

/** @brief 统计两幅同尺寸图像中不同的像素数。 */
static long xq_blurDiffPixels(const XImage* a, const XImage* b)
{
    long n = 0;
    int x;
    int y;
    for (y = 0; y < XImage_height(a); ++y)
        for (x = 0; x < XImage_width(a); ++x)
            if (XImage_pixel(a, x, y) != XImage_pixel(b, x, y)) ++n;
    return n;
}

/** @brief 构造 WxH 的 ARGB32_Premultiplied 图像并填 0（全透明）。 */
static XImage* xq_blurMakeImage(int w, int h)
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

/* ==================== ① 缺省值与包围盒公式 ==================== */

/**
 * @brief 缺省 blurRadius=5.0、blurHints=PerformanceHint（Qt 构造）。
 * @details 本机 Qt 6.8 探针：QGraphicsBlurEffect().blurRadius()=5.000、
 *          blurHints()=0(PerformanceHint)；源码见 QPixmapBlurFilterPrivate
 *          的 `radius(5), hints(PerformanceHint)`（qpixmapfilter.cpp:465）。
 */
XQ_TEST(XGraphicsBlurEffect, defaults)
{
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XQ_REQUIRE(blur != NULL);
    XQ_CHECK_MSG(XGraphicsBlurEffect_blurRadius(blur) == 5.0f,
                 "缺省半径 5.0（Qt QPixmapBlurFilterPrivate radius(5)）");
    XQ_CHECK_MSG(XGraphicsBlurEffect_blurHints(blur) ==
                     XGraphicsBlurEffectHint_Performance,
                 "缺省 blurHints=PerformanceHint");
    /* NULL 安全：查询返回 Qt 缺省值。 */
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(NULL) == 5.0f);
    XQ_CHECK(XGraphicsBlurEffect_blurHints(NULL) ==
             XGraphicsBlurEffectHint_Performance);
    XClassDelete((XClass*)blur);
}

/**
 * @brief boundingRectFor = 源矩形四周外扩 (1 + 2.5r)，负半径收缩。
 * @details 源矩形 (10,20,100,50)；期望值逐条对标本机 Qt 6.8 探针。
 */
XQ_TEST(XGraphicsBlurEffect, boundingRectFormula)
{
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XRectF src;
    XRectF got;
    XQ_REQUIRE(blur != NULL);

    src.x = 10.0f;
    src.y = 20.0f;
    src.width = 100.0f;
    src.height = 50.0f;

    /* 缺省半径 5.0 → pad 13.5（Qt 探针 r=5 → (‑3.5,6.5,127,77)）。 */
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, -3.5f, 6.5f, 127.0f, 77.0f, "r=5 缺省");

    /* r=0 仍外扩 1px（Qt 实测，不是"零半径零外扩"）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 0.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 9.0f, 19.0f, 102.0f, 52.0f, "r=0");

    /* r=0.5 → pad 2.25（实测 7.75,17.75,104.5,54.5）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 0.5f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 7.75f, 17.75f, 104.5f, 54.5f, "r=0.5");

    /* r=1 → pad 3.5。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 1.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 6.5f, 16.5f, 107.0f, 57.0f, "r=1");

    XGraphicsBlurEffect_setBlurRadius(blur, 2.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 4.0f, 14.0f, 112.0f, 62.0f, "r=2");

    XGraphicsBlurEffect_setBlurRadius(blur, 3.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 1.5f, 11.5f, 117.0f, 67.0f, "r=3");

    /* 线性外推无上限钳制。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 100.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, -241.0f, -231.0f, 602.0f, 552.0f, "r=100");

    /* 负半径：pad = 1 + 2.5r 变负 ⇒ 包围盒收缩（Qt 6.8 探针实测
       r=-0.5 → (10.25,20.25,99.5,49.5)、r=-2 → (14,24,92,42)、
       r=-5 → (21.5,31.5,77,27)）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, -0.5f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 10.25f, 20.25f, 99.5f, 49.5f, "r=-0.5");
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(blur) == -0.5f);

    XGraphicsBlurEffect_setBlurRadius(blur, -2.0f);
    XQ_CHECK_MSG(XGraphicsBlurEffect_blurRadius(blur) == -2.0f,
                 "负半径照存（Qt 探针 setBlurRadius(-3) → -3，不钳制）");
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 14.0f, 24.0f, 92.0f, 42.0f, "r=-2");

    XGraphicsBlurEffect_setBlurRadius(blur, -5.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 21.5f, 31.5f, 77.0f, 27.0f, "r=-5");

    /* 半径极负：pad=-49，包围盒整体收缩（源 (10,20,100,50) →
       (59,69,2,-48)：宽剩 2、高已为负）。空包围盒要看源尺寸：30x20 源
       在 r=-20 时得 (49,49,-68,-78)（Qt 探针实测），绘制管线据此整块
       裁剪（见下方 drawThresholdAndCrop 的像素断言）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, -20.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 59.0f, 69.0f, 2.0f, -48.0f, "r=-20 大源");
    {
        XRectF small;
        small.x = 0.0f;
        small.y = 0.0f;
        small.width = 30.0f;
        small.height = 20.0f;
        got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &small);
        xq_blurExpectRect(&got, 49.0f, 49.0f, -68.0f, -78.0f,
                          "r=-20 30x20 源（空包围盒）");
        XQ_CHECK(got.width <= 0.0f && got.height <= 0.0f);
    }

    /* hints 三档不改变包围盒（Qt 6.8 实测结果完全一致）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 3.0f);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 1.5f, 11.5f, 117.0f, 67.0f, "r=3 perf");
    XGraphicsBlurEffect_setBlurHints(blur, XGraphicsBlurEffectHint_Quality);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 1.5f, 11.5f, 117.0f, 67.0f, "r=3 quality");
    XGraphicsBlurEffect_setBlurHints(blur, XGraphicsBlurEffectHint_Animation);
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &src);
    xq_blurExpectRect(&got, 1.5f, 11.5f, 117.0f, 67.0f, "r=3 anim");

    /* NULL 源矩形返回空矩形。 */
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, NULL);
    XQ_CHECK(got.width == 0.0f && got.height == 0.0f);

    XClassDelete((XClass*)blur);
}

/* ==================== ② 属性、信号与安装 ==================== */

/**
 * @brief blurRadius/blurHints 往返、同值早退、信号载荷、挂接后包围盒。
 * @details Qt 语义：setBlurRadius 变化 → updateBoundingRect + emit
 *          blurRadiusChanged；setBlurHints 变化 → **只** emit（不重绘）；
 *          两者同值均早退；hints 缺省 PerformanceHint(=0)。
 */
XQ_TEST(XGraphicsBlurEffect, propsAndSignals)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XObject* probe = XObject_create();
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XRectF src;
    XRectF got;
    XQ_REQUIRE(widget != NULL && probe != NULL && blur != NULL);

    s_radiusCount = 0;
    s_hintsCount = 0;

    XObject_connect((XObject*)blur,
                      XSignal(XGraphicsBlurEffect_blurRadiusChanged_signal),
                      probe, xq_blurRadiusSlot, XConnectionType_Direct);
    XObject_connect((XObject*)blur,
                      XSignal(XGraphicsBlurEffect_blurHintsChanged_signal),
                      probe, xq_blurHintsSlot, XConnectionType_Direct);

    /* 缺省 5.0：setBlurRadius(5) 是同值 → 不发射。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 5.0f);
    XQ_EQ_I64(s_radiusCount, 0);

    XGraphicsBlurEffect_setBlurRadius(blur, 3.0f);
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(blur) == 3.0f);
    XQ_EQ_I64(s_radiusCount, 1);
    XQ_CHECK(s_radiusLast == 3.0f);

    XGraphicsBlurEffect_setBlurRadius(blur, 3.0f); /* 同值早退 */
    XQ_EQ_I64(s_radiusCount, 1);

    /* 负值照存（Qt 不钳制；旧契约「钳 0」已迁移）。 */
    XGraphicsBlurEffect_setBlurRadius(blur, -2.0f);
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(blur) == -2.0f);
    XQ_EQ_I64(s_radiusCount, 2);
    XQ_CHECK(s_radiusLast == -2.0f);

    /* [P0-60 2026-11-11] **近等值**早退：Qt 用 qFuzzyCompare
       （qgraphicseffect.cpp:763-764：
        `if (qFuzzyCompare(d->filter->radius(), radius)) return;`，
        等价式 corelib/global/qnumeric.h:393-396
        `|a-b| * 100000.f <= min(|a|,|b|)`）。精确 == 会让 2.0 与
       nextafterf(2,3)（相对差 1.2e-7）多发一次 blurRadiusChanged 并白跑
       一次 updateBoundingRect()。早退同时**不改存值**。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 2.0f);
    XQ_EQ_I64(s_radiusCount, 3);
    XGraphicsBlurEffect_setBlurRadius(blur, nextafterf(2.0f, 3.0f));
    XQ_EQ_I64(s_radiusCount, 3);
    XQ_CHECK_MSG(XGraphicsBlurEffect_blurRadius(blur) == 2.0f,
                 "近等值早退不得改写存储值");
    /* 真差别（2.01 vs 2.0）必须照常发射。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 2.01f);
    XQ_EQ_I64(s_radiusCount, 4);
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(blur) == 2.01f);

    XGraphicsBlurEffect_setBlurHints(blur, XGraphicsBlurEffectHint_Quality);
    XQ_CHECK(XGraphicsBlurEffect_blurHints(blur) ==
             XGraphicsBlurEffectHint_Quality);
    XQ_EQ_I64(s_hintsCount, 1);
    XQ_EQ_I64(s_hintsLast, (long long)XGraphicsBlurEffectHint_Quality);
    XGraphicsBlurEffect_setBlurHints(blur, XGraphicsBlurEffectHint_Quality);
    XQ_EQ_I64(s_hintsCount, 1);

    /* 挂接：效果包围盒以控件尺寸为源（源本地坐标，原点 (0,0)）。 */
    XWidget_resize(widget, 60, 40);
    XGraphicsBlurEffect_setBlurRadius(blur, 2.0f);
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)blur); /* 所有权转移 */
    src = XGraphicsEffect_boundingRect((XGraphicsEffect*)blur);
    xq_blurExpectRect(&src, -6.0f, -6.0f, 72.0f, 52.0f, "控件 60x40 r=2");

    /* 半径收缩要能覆盖"旧外扩环"：只在有源时标脏，不得崩溃。 */
    XGraphicsBlurEffect_setBlurRadius(blur, 0.0f);
    got = XGraphicsEffect_boundingRect((XGraphicsEffect*)blur);
    xq_blurExpectRect(&got, -1.0f, -1.0f, 62.0f, 42.0f, "控件 60x40 r=0");

    /* NULL 安全。 */
    XQ_CHECK(XGraphicsBlurEffect_blurRadius(NULL) == 5.0f);
    XGraphicsBlurEffect_setBlurRadius(NULL, 3.0f);
    XGraphicsBlurEffect_setBlurHints(NULL, 0);

    XClassDelete((XClass*)widget); /* 带走 blur */
    XClassDelete((XClass*)probe);
}

/* ==================== ③ 指数模糊核随半径展宽 ==================== */

/**
 * @brief XGraphicsEffect_blurImage 的核展宽随半径变化（不再是固定 3x3）。
 * @details 对齐 Qt qt_blurImage/expblur（qimageeffects.cpp:27-160）：
 *          alpha = round(4096·(1 − (2/255)^(1/radius)))，双侧指数冲激响应。
 *          可观测性质（用合成图像逐像素取证）：
 *            ① radius=0 → 逐像素不变（Qt radius ≤ 1e-5 时 alpha 取 4095，
 *               近似恒等）；
 *            ② radius=3 → 有像素被改变，且改变只发生在源块外扩约
 *               radius 像素以内（核支撑与包围盒 pad=1+2.5r 同量级）；
 *            ③ radius=10 的改变像素数 > radius=3（半径越大摊得越开）；
 *            ④ alphaOnly=true 只动 alpha，rgb 原样保留（Qt 投影用
 *               expblur<...,true>）。
 *          注意：本用例直接给"绘制半径"；QGraphicsBlurEffect::draw 传的是
 *          2.5 × blurRadius（见下方 drawThresholdAndCrop）。
 */
XQ_TEST(XGraphicsBlurEffect, kernelRadiusScaling)
{
    const int W = 41;
    const int H = 21;
    XImage* base = xq_blurMakeImage(W, H);
    XImage* r0;
    XImage* r3;
    XImage* r10;
    XImage* alphaOnly;
    long d3;
    long d10;
    int x;
    int y;
    int outsideChanged = 0;
    XQ_REQUIRE(base != NULL);

    /* 源：中间 9 像素宽的不透明竖条（x∈[16,24]，alpha=255、rgb=0），
       模拟锐利边缘；rgb 为 0 是为了让 alphaOnly 的"只动 alpha"可断言
       （投影层正是这种形态）。 */
    for (y = 0; y < H; ++y)
        for (x = 16; x <= 24; ++x)
            XImage_setPixel(base, x, y, 0xFF000000u);

    r0 = xq_blurMakeImage(W, H);
    r3 = xq_blurMakeImage(W, H);
    r10 = xq_blurMakeImage(W, H);
    alphaOnly = xq_blurMakeImage(W, H);
    XQ_REQUIRE(r0 != NULL && r3 != NULL && r10 != NULL && alphaOnly != NULL);
    /* 复制基准到三个试验图像。 */
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint32_t p = XImage_pixel(base, x, y);
            XImage_setPixel(r0, x, y, p);
            XImage_setPixel(r3, x, y, p);
            XImage_setPixel(r10, x, y, p);
            XImage_setPixel(alphaOnly, x, y, p);
        }

    /* ① 半径 0 = 恒等。 */
    XGraphicsEffect_blurImage(r0, 0.0f, false, false);
    XQ_EQ_I64(xq_blurDiffPixels(base, r0), 0);

    /* ② 半径 3：有扩散，且不超出源块 ± (2.5r+1)。 */
    XGraphicsEffect_blurImage(r3, 3.0f, false, false);
    d3 = xq_blurDiffPixels(base, r3);
    XQ_CHECK_MSG(d3 > 0, "radius=3 必须产生扩散（核随半径展宽）");
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            if (XImage_pixel(base, x, y) == XImage_pixel(r3, x, y)) continue;
            if (x < 16 - 9 || x > 24 + 9) ++outsideChanged;
        }
    XQ_CHECK_MSG(outsideChanged == 0,
                 "radius=3 的扩散必须落在包围盒 pad(1+2.5r≈8.5→9) 内");

    /* ③ 半径越大改变面越广。 */
    XGraphicsEffect_blurImage(r10, 10.0f, false, false);
    d10 = xq_blurDiffPixels(base, r10);
    XQ_CHECK_MSG(d10 > d3, "radius=10 的改变像素数必须多于 radius=3");

    /* ④ alphaOnly 只动 alpha（源块 rgb=0 处保持 0）。 */
    XGraphicsEffect_blurImage(alphaOnly, 3.0f, false, true);
    {
        long rgbBad = 0;
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x)
                if ((XImage_pixel(alphaOnly, x, y) & 0x00FFFFFFu) != 0)
                    ++rgbBad;
        XQ_CHECK_MSG(rgbBad == 0, "alphaOnly 必须保留 rgb（预乘 0）");
    }
    /* alphaOnly 确实扩散了 alpha（源块外的点 alpha > 0）。 */
    XQ_CHECK(xq_blurAlphaAt(alphaOnly, 15, 10) > 0);

    XClassDelete((XClass*)base);
    XClassDelete((XClass*)r0);
    XClassDelete((XClass*)r3);
    XClassDelete((XClass*)r10);
    XClassDelete((XClass*)alphaOnly);
}

/* ==================== ④ 绘制阈值、裁剪与空包围盒 ==================== */

/**
 * @brief Draw 槽位的半径阈值（≤1 不模糊）、负半径裁边与空包围盒整块不画。
 * @details ① radius=1：Qt QPixmapBlurFilter::draw 直接绘源 ⇒ 边缘像素
 *              alpha 仍为全值（255）；
 *          ② radius=6：绘制半径 2.5×6=15，边缘像素 alpha 被指数模糊衰减
 *              （0<alpha<255），远离边缘的中心仍为 255；
 *          ③ radius=-2：boundingRectFor 收缩为 (4,4,22,12)，Draw 的画布
 *              就是该尺寸 ⇒ 源被裁掉四周 4px（画布内全为源内容）；
 *          ④ 半径极负（-20）：boundingRectFor 面积为负；整条管线
 *              （XGraphicsEffect_drawWidget）此时**不落任何像素**——控件
 *              被整块裁剪，与 Qt 实测（父级全白、无任何子控件像素）一致。
 *          ⑤ XPALETTE_ON 下再经 XWidget_grab 走完整 paintTree 管线：
 *              r=1 时控件边缘 alpha=255（不模糊）、r=6 时 <255、r=-2 时
 *              四周 4px 透明（裁边）、r=-20 时整幅透明（整块裁剪）。
 */
XQ_TEST(XGraphicsBlurEffect, drawThresholdAndCrop)
{
    XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
    XGraphicsEffect_DrawSlot slot;
    XImage* src = xq_blurMakeImage(60, 40);
    XImage* dest;
    XGraphicsEffectDrawContext ctx;
    XRectF bounds;
    XRectF srcRectF;
    XQ_REQUIRE(blur != NULL && src != NULL);
    XImage_fillRect(src, NULL, 0xFFFF0000u); /* 全幅不透明红 */

    XQ_REQUIRE(XClassGetVtable((XClass*)blur) != NULL);
    slot = XClassGetVirtualFunc((XGraphicsEffect*)blur,
                                EXGraphicsEffect_Draw,
                                XGraphicsEffect_DrawSlot);
    XQ_REQUIRE(slot != NULL);

    /* ① radius=1：不模糊，边缘 alpha 保持 255。 */
    dest = xq_blurMakeImage(92, 72);
    XQ_REQUIRE(dest != NULL);
    ctx.m_source = src;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 60;
    ctx.m_sourceRect.height = 40;
    ctx.m_dest = dest;
    ctx.m_destRect.x = -16;
    ctx.m_destRect.y = -16;
    ctx.m_destRect.width = 92;
    ctx.m_destRect.height = 72;
    XGraphicsBlurEffect_setBlurRadius(blur, 1.0f);
    ((XGraphicsEffect*)blur)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)blur, &ctx);
    ((XGraphicsEffect*)blur)->m_drawCtx = NULL;
    /* 源左上角落在画布 (16,16)；该点是源的边缘像素。 */
    XQ_CHECK_MSG(xq_blurAlphaAt(dest, 16, 36) == 255,
                 "radius=1 不模糊（Qt QPixmapBlurFilter::draw 短路）");
    XClassDelete((XClass*)dest);

    /* ② radius=6：绘制半径 15，源边缘被衰减，中心保持不透明。 */
    dest = xq_blurMakeImage(92, 72);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    XGraphicsBlurEffect_setBlurRadius(blur, 6.0f);
    ((XGraphicsEffect*)blur)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)blur, &ctx);
    ((XGraphicsEffect*)blur)->m_drawCtx = NULL;
    {
        int edgeAlpha = xq_blurAlphaAt(dest, 16, 36);
        int centerAlpha = xq_blurAlphaAt(dest, 46, 36);
        XQ_CHECK_MSG(edgeAlpha > 0 && edgeAlpha < 255,
                     "radius=6 必须把源边缘模糊成半透明（实得 %d）",
                     edgeAlpha);
        /* 60x40 源在绘制半径 15 下，中心距最近边缘仍约 19px，指数尾迹
           使 alpha 略低于 255（本机离线核验证 ≈252），但必须显著高于边缘。 */
        XQ_CHECK_MSG(centerAlpha >= 245,
                     "远离边缘的中心必须基本保持不透明（实得 %d）",
                     centerAlpha);
        XQ_CHECK_MSG(centerAlpha > edgeAlpha + 60,
                     "中心必须明显比边缘不透明（center=%d edge=%d）",
                     centerAlpha, edgeAlpha);
    }
    XClassDelete((XClass*)dest);

    /* ③ radius=-2：包围盒收缩成 (4,4,22,12)，画布内全为源内容。 */
    XRectF in;
    in.x = 0.0f;
    in.y = 0.0f;
    in.width = 60.0f;
    in.height = 40.0f;
    XGraphicsBlurEffect_setBlurRadius(blur, -2.0f);
    bounds = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &in);
    xq_blurExpectRect(&bounds, 4.0f, 4.0f, 52.0f, 32.0f, "60x40 r=-2");
    dest = xq_blurMakeImage((int)bounds.width, (int)bounds.height);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ctx.m_destRect.x = (int)bounds.x;
    ctx.m_destRect.y = (int)bounds.y;
    ctx.m_destRect.width = (int)bounds.width;
    ctx.m_destRect.height = (int)bounds.height;
    ((XGraphicsEffect*)blur)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)blur, &ctx);
    ((XGraphicsEffect*)blur)->m_drawCtx = NULL;
    XQ_CHECK_MSG(xq_blurAlphaAt(dest, 0, 0) == 255 &&
                     xq_blurAlphaAt(dest, 51, 31) == 255,
                 "负半径画布=收缩后的包围盒，源被裁边后仍铺满画布");
    XClassDelete((XClass*)dest);

    /* ④ radius=-20：包围盒面积为负（管线整块裁剪；像素级验证见 ⑤）。 */
    srcRectF.x = 0.0f;
    srcRectF.y = 0.0f;
    srcRectF.width = 60.0f;
    srcRectF.height = 40.0f;
    XGraphicsBlurEffect_setBlurRadius(blur, -20.0f);
    bounds = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)blur, &srcRectF);
    XQ_CHECK(bounds.width <= 0.0f && bounds.height <= 0.0f);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)blur);

#if XPALETTE_ON
    /* ⑤ 完整 paintTree 管线（XWidget_grab → 效果挂接渲染段）。 */
    {
        XWidget* widget = XWidget_create(NULL, 0);
        XPalette pal;
        XColor red;
        XImage* shot;
        XQ_REQUIRE(widget != NULL);
        XWidget_resize(widget, 30, 20);
        XColor_init_rgb(&red, 255, 0, 0, 255);
        pal = XWidget_palette(widget);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, red);
        XWidget_setPalette(widget, &pal);
        XWidget_setAutoFillBackground(widget, true);

        /* 基线（无效果）：整幅不透明。 */
        shot = XWidget_grab(widget);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK(xq_blurAlphaAt(shot, 10, 10) == 255);
        XClassDelete((XClass*)shot);

        /* r=1：阈值内不模糊，边缘像素仍不透明。 */
        XGraphicsBlurEffect* fx = XGraphicsBlurEffect_create();
        XQ_REQUIRE(fx != NULL);
        XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)fx);
        XGraphicsBlurEffect_setBlurRadius(fx, 1.0f);
        shot = XWidget_grab(widget);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK_MSG(xq_blurAlphaAt(shot, 0, 10) == 255,
                     "r=1 直接绘源：控件边缘 alpha 应为 255（实得 %d）",
                     xq_blurAlphaAt(shot, 0, 10));
        XClassDelete((XClass*)shot);

        /* r=6：边缘被模糊成半透明。 */
        XGraphicsBlurEffect_setBlurRadius(fx, 6.0f);
        shot = XWidget_grab(widget);
        XQ_REQUIRE(shot != NULL);
        {
            int edge = xq_blurAlphaAt(shot, 0, 10);
            XQ_CHECK_MSG(edge > 0 && edge < 255,
                         "r=6 控件边缘 alpha 应被衰减（实得 %d）", edge);
        }
        XClassDelete((XClass*)shot);

        /* r=-2：包围盒收缩 ⇒ 四周 4px 被裁掉（透明），内部保留。 */
        XGraphicsBlurEffect_setBlurRadius(fx, -2.0f);
        shot = XWidget_grab(widget);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK_MSG(xq_blurAlphaAt(shot, 0, 0) == 0 &&
                         xq_blurAlphaAt(shot, 29, 19) == 0,
                     "r=-2 包围盒收缩：四角必须被裁成透明");
        XQ_CHECK_MSG(xq_blurAlphaAt(shot, 10, 10) == 255,
                     "r=-2 画布内部仍是源内容（实得 %d）",
                     xq_blurAlphaAt(shot, 10, 10));
        XClassDelete((XClass*)shot);

        /* r=-20：空包围盒 ⇒ 整块裁剪，一个像素都不落。 */
        XGraphicsBlurEffect_setBlurRadius(fx, -20.0f);
        shot = XWidget_grab(widget);
        XQ_REQUIRE(shot != NULL);
        {
            long opaque = 0;
            int x;
            int y;
            for (y = 0; y < XImage_height(shot); ++y)
                for (x = 0; x < XImage_width(shot); ++x)
                    if (xq_blurAlphaAt(shot, x, y) != 0) ++opaque;
            XQ_CHECK_MSG(opaque == 0,
                         "r=-20 空包围盒：整块裁剪，控件不得落像素（实得 %ld）",
                         opaque);
        }
        XClassDelete((XClass*)shot);
        XClassDelete((XClass*)widget); /* 带走 fx */
    }
#endif /* XPALETTE_ON */
}
