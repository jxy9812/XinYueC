/**
 * @file       XGraphicsOpacityEffectAlignTest.c
 * @brief      XGraphicsOpacityEffect 与 Qt 6.8 同名类对齐契约测试。
 * @details    对齐依据：本机 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *             qgraphicseffect.h:215-242 与 Qt 6.8 探针实测：
 *             - 构造缺省 opacity = 0.7（不是 1.0）；
 *             - setOpacity 钳制到 [0,1]（实测 -1 → 0、2 → 1），载荷为
 *               钳位后的值；
 *             - boundingRectFor 原样返回（不透明度不扩大包围盒），
 *               安装到控件后 boundingRect() 仍是源本地 (0,0,w,h)。
 *
 *             opacityMask（QBrush）按 Qt 口径实现并有像素级断言：
 *             - 缺省 NoBrush（探针 opacityMask().style()==0）；
 *             - setOpacityMask 按**画刷值**比较早退（探针：同值不同实例不
 *               发射），变化时 update + opacityMaskChanged；
 *             - 只取掩码 **alpha** 参与合成（探针：不透明绿色掩码对红色源
 *               无影响），结果 alpha = 源 alpha × opacity × maskAlpha/255
 *               （探针：mask a=128 + opacity 1 → a=128；再叠 opacity 0.5 →
 *               a=64）；
 *             - NoBrush 清除掩码后恢复完全不透明。
 *             画刷光栅化复用 XBrush_applyToPainter + XPainter_fillRect_2
 *             （纯色/渐变完整支持；Dense/Hor/Ver/Diag 图案在软件后端按纯色
 *             近似——XPainter_setBrushStyle 既有声明，属该后端能力边界）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsOpacityEffect.h"
#include "XObject.h"
#include "XVarList.h"
#include "XWidget.h"
#include "XImage.h"
#include "XColor.h"
#include "XBrush.h"
#include "XClass.h"

#include <math.h>

/** @brief 浮点断言容差。 */
#define XQ_OPACITY_EPS 0.0005f

/* ==================== 探针槽函数 ==================== */

static int s_opacityCount = 0;    /**< opacityChanged 次数。 */
static float s_opacityLast = -1.0f; /**< 最近一次不透明度载荷。 */
static int s_destroyedCount = 0;  /**< destroyed 次数（所有权取证）。 */

/** @brief opacityChanged(float) 槽。 */
static void xq_opacitySlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, float, opacity);
        ++s_opacityCount;
        s_opacityLast = opacity;
    }
}

/** @brief destroyed() 槽。 */
static void xq_opacityDestroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_destroyedCount;
}

static int s_maskCount = 0;       /**< opacityMaskChanged 次数。 */
static XBrush s_maskLast;         /**< 最近一次掩码载荷。 */

/** @brief opacityMaskChanged(XBrush) 槽。 */
static void xq_opacityMaskSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    {
        XVarList_args_1(args, XBrush, mask);
        ++s_maskCount;
        s_maskLast = mask;
    }
}

/* ==================== ① 不透明度属性与信号 ==================== */

/**
 * @brief 缺省 0.7、区间钳制、同值早退、信号载荷为钳位后值。
 * @details Qt 6.8 实测序列：default 0.7 / setOpacity(-1) → 0 /
 *          setOpacity(2) → 1 / setOpacity(0.3) → 0.3。
 */
XQ_TEST(XGraphicsOpacityEffect, opacityClamp)
{
    XObject* probe = XObject_create();
    XGraphicsOpacityEffect* effect = XGraphicsOpacityEffect_create();
    XQ_REQUIRE(probe != NULL && effect != NULL);

    s_opacityCount = 0;
    s_opacityLast = -1.0f;

    XQ_CHECK_MSG(fabsf(XGraphicsOpacityEffect_opacity(effect) - 0.7f) <=
                     XQ_OPACITY_EPS,
                 "构造缺省 opacity=0.7（Qt 6.8 qgraphicseffect_p.h 实证）");

    XObject_connect_1((XObject*)effect,
                      XSignal(XGraphicsOpacityEffect_opacityChanged_signal),
                      probe, xq_opacitySlot, XConnectionType_Direct);

    XGraphicsOpacityEffect_setOpacity(effect, 0.3f);
    XQ_CHECK(fabsf(XGraphicsOpacityEffect_opacity(effect) - 0.3f) <=
             XQ_OPACITY_EPS);
    XQ_EQ_I64(s_opacityCount, 1);
    XQ_CHECK(fabsf(s_opacityLast - 0.3f) <= XQ_OPACITY_EPS);

    /* 同值早退（Qt `if (qFuzzyCompare(...)) return;`）。 */
    XGraphicsOpacityEffect_setOpacity(effect, 0.3f);
    XQ_EQ_I64(s_opacityCount, 1);

    /* 下越界钳 0：Qt 实测 -1 → 0；载荷是钳位后的值。 */
    XGraphicsOpacityEffect_setOpacity(effect, -1.0f);
    XQ_CHECK(XGraphicsOpacityEffect_opacity(effect) == 0.0f);
    XQ_EQ_I64(s_opacityCount, 2);
    XQ_CHECK(s_opacityLast == 0.0f);

    /* 上越界钳 1。 */
    XGraphicsOpacityEffect_setOpacity(effect, 2.0f);
    XQ_CHECK(XGraphicsOpacityEffect_opacity(effect) == 1.0f);
    XQ_EQ_I64(s_opacityCount, 3);
    XQ_CHECK(s_opacityLast == 1.0f);

    /* NULL 安全：缺省返回 1.0（"不改变画面"的中性值）。 */
    XQ_CHECK(XGraphicsOpacityEffect_opacity(NULL) == 1.0f);
    XGraphicsOpacityEffect_setOpacity(NULL, 0.2f);

    XClassDelete((XClass*)effect);
    XClassDelete((XClass*)probe);
}

/* ==================== ② 包围盒与安装所有权 ==================== */

/**
 * @brief 不透明度不改包围盒；安装后仍按源本地坐标取包围盒。
 * @details Qt：QGraphicsOpacityEffect 不覆盖 boundingRectFor，故
 *          boundingRect() == sourceBoundingRect() == (0,0,w,h)。
 *          同时验证控件换装时旧效果被删除（destroyed 信号取证）。
 */
XQ_TEST(XGraphicsOpacityEffect, boundingRectAndOwnership)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XObject* probe = XObject_create();
    XGraphicsOpacityEffect* effect = XGraphicsOpacityEffect_create();
    XGraphicsEffect* replacement = XGraphicsEffect_create();
    XRectF src;
    XRectF got;
    XQ_REQUIRE(widget != NULL && probe != NULL && effect != NULL &&
               replacement != NULL);

    /* 纯函数口径：入参原样返回。 */
    src.x = 10.0f;
    src.y = 20.0f;
    src.width = 100.0f;
    src.height = 50.0f;
    got = XGraphicsEffect_boundingRectFor((XGraphicsEffect*)effect, &src);
    XQ_CHECK(got.x == 10.0f && got.y == 20.0f);
    XQ_CHECK(got.width == 100.0f && got.height == 50.0f);

    /* 安装到子控件（位于 (12,7)，尺寸 50x30）：包围盒仍是源本地坐标。 */
    {
        XWidget* parent = XWidget_create(NULL, 0);
        XQ_REQUIRE(parent != NULL);
        XWidget_resize(parent, 200, 200);
        XWidget_setParent(widget, parent, 0);
        XWidget_move(widget, 12, 7);
        XWidget_resize(widget, 50, 30);

        XGraphicsOpacityEffect_setOpacity(effect, 0.5f);
        XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)effect);
        XQ_CHECK(XWidget_graphicsEffect(widget) == (XGraphicsEffect*)effect);
        XQ_CHECK(XGraphicsEffect_source((XGraphicsEffect*)effect) == widget);

        got = XGraphicsEffect_boundingRect((XGraphicsEffect*)effect);
        XQ_CHECK_MSG(got.x == 0.0f && got.y == 0.0f,
                     "效果包围盒原点恒为源本地 (0,0)，与控件位置无关");
        XQ_CHECK(got.width == 50.0f && got.height == 30.0f);

        /* 换装：旧（不透明度）效果由控件删除。 */
        s_destroyedCount = 0;
        XObject_connect_1((XObject*)effect, XSignal(XObject_destroyed_signal),
                          probe, xq_opacityDestroyedSlot,
                          XConnectionType_Direct);
        XWidget_setGraphicsEffect(widget, replacement);
        XQ_EQ_I64(s_destroyedCount, 1);
        XQ_CHECK(XWidget_graphicsEffect(widget) == replacement);
        XQ_CHECK(XGraphicsEffect_source(replacement) == widget);

        XWidget_setParent(widget, NULL, 0);
        XClassDelete((XClass*)parent);
    }

    XClassDelete((XClass*)widget); /* 带走 replacement */
    XClassDelete((XClass*)probe);
}

/* ==================== ③ opacityMask 属性与像素合成 ==================== */

/** @brief 构造 WxH 的 ARGB32_Premultiplied 图像并填 0。 */
static XImage* xq_opacityMakeImage(int w, int h)
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

/** @brief 取图像某点的 alpha。 */
static int xq_opacityAlphaAt(const XImage* image, int x, int y)
{
    XColor c = XImage_pixelColor(image, x, y);
    return (int)XColor_alpha(&c);
}

/**
 * @brief opacityMask 缺省 NoBrush、值比较早退、信号载荷、清除恢复。
 * @details 对齐 Qt 6.8（qgraphicseffect.cpp:1124-1141）：
 *              QBrush opacityMask() const
 *              setOpacityMask: if (d->opacityMask == mask) return;
 *                              d->opacityMask = mask;
 *                              d->hasOpacityMask = (mask.style() != NoBrush);
 *                              update(); emit opacityMaskChanged(mask);
 *          本机探针实测：缺省 style=0(NoBrush)；setMask 发 1 次；
 *          同值再设 0 次；**等值不同实例**（QBrush(QColor(0,0,255,128)) 新建）
 *          同样 0 次（按值比较）。
 */
XQ_TEST(XGraphicsOpacityEffect, maskPropertyAndSignals)
{
    XObject* probe = XObject_create();
    XGraphicsOpacityEffect* effect = XGraphicsOpacityEffect_create();
    XBrush mask;
    XBrush same;
    XColor maskColor;
    XQ_REQUIRE(probe != NULL && effect != NULL);

    s_maskCount = 0;

    /* 缺省 NoBrush（Qt「There is no opacity mask by default」）。 */
    mask = XGraphicsOpacityEffect_opacityMask(effect);
    XQ_CHECK_MSG(XBrush_style(&mask) == XBrushStyle_NoBrush,
                 "缺省掩码 = NoBrush（Qt 探针 style==0）");
    /* NULL 安全：NoBrush。 */
    mask = XGraphicsOpacityEffect_opacityMask(NULL);
    XQ_CHECK(XBrush_style(&mask) == XBrushStyle_NoBrush);

    XObject_connect_1((XObject*)effect,
                      XSignal(XGraphicsOpacityEffect_opacityMaskChanged_signal),
                      probe, xq_opacityMaskSlot, XConnectionType_Direct);

    XColor_init_rgb(&maskColor, 0, 0, 255, 128);
    mask = XBrush_create_color(maskColor, XBrushStyle_SolidPattern);
    XGraphicsOpacityEffect_setOpacityMask(effect, mask);
    XQ_EQ_I64(s_maskCount, 1);
    XQ_CHECK_MSG(XBrush_style(&s_maskLast) == XBrushStyle_SolidPattern,
                 "信号载荷必须是新掩码（样式）");
    {
        XColor got = XBrush_color(&s_maskLast);
        XQ_CHECK(XColor_alpha(&got) == 128);
    }
    /* 同值（同一句柄）早退。 */
    XGraphicsOpacityEffect_setOpacityMask(effect, mask);
    XQ_EQ_I64(s_maskCount, 1);
    /* 等值不同实例同样早退（XBrush_isEqual 按值比较，对齐 QBrush::operator==）。 */
    same = XBrush_create_color(maskColor, XBrushStyle_SolidPattern);
    XGraphicsOpacityEffect_setOpacityMask(effect, same);
    XQ_CHECK_MSG(s_maskCount == 1,
                 "等值不同实例不得发射（Qt 探针实测 0 次，实得 %d）",
                 s_maskCount);

    /* NoBrush 清除掩码：与实际变化一样发信号。 */
    XGraphicsOpacityEffect_setOpacityMask(effect, XBrush_create());
    XQ_EQ_I64(s_maskCount, 2);
    XQ_CHECK(XBrush_style(&s_maskLast) == XBrushStyle_NoBrush);

    /* NULL 安全。 */
    XGraphicsOpacityEffect_setOpacityMask(NULL, mask);
    mask = XGraphicsOpacityEffect_opacityMask(effect);
    XQ_CHECK(XBrush_style(&mask) == XBrushStyle_NoBrush);
    XClassDelete((XClass*)effect);
    /* 探针对象必须释放：此前漏删 ⇒ 每遍 +352 字节，泄漏门在复核遍仍
     * 增长而判 [LEAK]（2026-11-11 实测）。 */
    XClassDelete((XClass*)probe);
}

/**
 * @brief 掩码按 alpha 相乘：alpha = 源 alpha × opacity × maskAlpha/255，
 *        掩码 RGB 不参与。
 * @details 本机 Qt 6.8 探针（60x40 不透明红控件）：
 *            mask a=128 + opacity 1   → 输出 a=128, r=128（预乘直红）
 *            mask a=128 + opacity 0.5 → 输出 a=64
 *            NoBrush                  → a=255
 *            不透明绿掩码              → a=255, r=255（RGB 被忽略）
 *          本用例直接派发 Draw 槽位（合成绘制上下文，画布=源矩形，
 *          opacity 效果不扩包围盒），逐像素断言 alpha。
 */
XQ_TEST(XGraphicsOpacityEffect, maskAlphaComposite)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XGraphicsOpacityEffect* effect = XGraphicsOpacityEffect_create();
    XGraphicsEffect_DrawSlot slot;
    XImage* src = xq_opacityMakeImage(30, 20);
    XImage* dest;
    XGraphicsEffectDrawContext ctx;
    XBrush mask;
    XColor color;
    XQ_REQUIRE(widget != NULL && effect != NULL && src != NULL);
    XImage_fillRect(src, NULL, 0xFFFF0000u); /* 源 = 不透明红 */
    XWidget_resize(widget, 30, 20);
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)effect);
    XQ_REQUIRE(XClassGetVtable((XClass*)effect) != NULL);
    slot = XClassGetVirtualFunc((XGraphicsEffect*)effect,
                                EXGraphicsEffect_Draw,
                                XGraphicsEffect_DrawSlot);
    XQ_REQUIRE(slot != NULL);

    ctx.m_source = src;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 30;
    ctx.m_sourceRect.height = 20;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 30;
    ctx.m_destRect.height = 20;

    /* 基线：opacity=1、无掩码 → 全不透明。 */
    XGraphicsOpacityEffect_setOpacity(effect, 1.0f);
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    XQ_CHECK(xq_opacityAlphaAt(dest, 15, 10) == 255);
    XClassDelete((XClass*)dest);

    /* 掩码 a=128 + opacity 1 → a=128。 */
    XColor_init_rgb(&color, 0, 0, 255, 128);
    mask = XBrush_create_color(color, XBrushStyle_SolidPattern);
    XGraphicsOpacityEffect_setOpacityMask(effect, mask);
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    XQ_CHECK_MSG(xq_opacityAlphaAt(dest, 15, 10) == 128,
                 "mask a=128 应把不透明源压到 a=128（实得 %d）",
                 xq_opacityAlphaAt(dest, 15, 10));
    XClassDelete((XClass*)dest);

    /* 再叠 opacity 0.5 → a≈64。 */
    XGraphicsOpacityEffect_setOpacity(effect, 0.5f);
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    {
        int a = xq_opacityAlphaAt(dest, 15, 10);
        XQ_CHECK_MSG(a >= 63 && a <= 65,
                     "opacity 0.5 × mask 128 → a≈64（实得 %d）", a);
    }
    XClassDelete((XClass*)dest);

    /* 不透明掩码（绿）→ RGB 不参与，a 仍 255。 */
    XColor_init_rgb(&color, 0, 255, 0, 255);
    mask = XBrush_create_color(color, XBrushStyle_SolidPattern);
    XGraphicsOpacityEffect_setOpacityMask(effect, mask);
    XGraphicsOpacityEffect_setOpacity(effect, 1.0f);
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    {
        XColor px = XImage_pixelColor(dest, 15, 10);
        XQ_CHECK_MSG(XColor_alpha(&px) == 255,
                     "不透明掩码不改变 alpha（实得 %d）",
                     (int)XColor_alpha(&px));
        XQ_CHECK_MSG(XColor_red(&px) == 255,
                     "掩码 RGB 被忽略：源红保留（实得 r=%d）",
                     (int)XColor_red(&px));
    }
    XClassDelete((XClass*)dest);

    /* 全透明掩码 → 全透明。 */
    XColor_init_rgb(&color, 0, 0, 0, 0);
    mask = XBrush_create_color(color, XBrushStyle_SolidPattern);
    XGraphicsOpacityEffect_setOpacityMask(effect, mask);
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    XQ_CHECK_MSG(xq_opacityAlphaAt(dest, 15, 10) == 0,
                 "mask a=0 → 输出全透明（DestinationIn 语义）");
    XClassDelete((XClass*)dest);

    /* 清除掩码 → 恢复完全不透明。 */
    XGraphicsOpacityEffect_setOpacityMask(effect, XBrush_create());
    dest = xq_opacityMakeImage(30, 20);
    XQ_REQUIRE(dest != NULL);
    ctx.m_dest = dest;
    ((XGraphicsEffect*)effect)->m_drawCtx = &ctx;
    slot((XGraphicsEffect*)effect, &ctx);
    ((XGraphicsEffect*)effect)->m_drawCtx = NULL;
    XQ_CHECK(xq_opacityAlphaAt(dest, 15, 10) == 255);
    XClassDelete((XClass*)dest);

    XClassDelete((XClass*)src);
    XClassDelete((XClass*)widget); /* 带走 effect */
}
