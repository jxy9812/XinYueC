/**
 * @file xgui_demo_fontscale.c
 * @brief demo 全局字号联动：把「行高/行宽/标签字号」按全局字号缩放。
 *
 * @details 背景（2026-11-16 用户口径「B 方案」）：demo 各页大量控件高度是
 *          固定像素（22/24/26…），全局字号上调后文字装不下 → 裁切。这里
 *          不分 351 个调用点逐个改，而是**整树一次扫描**：
 *            - 固定高（min==max 且 ≤DEMO_SCALE_MAX_H）视为「行高」，按
 *              字号比例放大并记基线（动态属性 demoBaseH）；
 *            - 固定宽（min==max 且 ≤DEMO_SCALE_MAX_W）视为「文本驱动宽」，
 *              同比例放大（demoBaseW）；
 *            - XLabel 自有 textPixelSize（标题类）按比例放大（demoBasePx）。
 *          基线只记一次：此后字号再变只按基线重算，不累积漂移；字号回落
 *          到缺省（≤0）时按基线原样还原。
 *
 *          比例 = px / DEMO_SCALE_BASE_PX（16px ≈ 12pt@96dpi），钳在
 *          [1.0, 2.0]，避免 32px 时把行宽推到爆。
 *
 * @note   只在 demo 层（Test/XGuiDemo）使用，不进框架；框架的 setFixedHeight
 *         语义不动。整树扫描成本 ≈ 控件数 × 两次 sizeHint 读取，字号变更
 *         是用户手点频率，可接受。
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XVariant.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XLayout.h"

#define DEMO_SCALE_BASE_PX 16   /**< 比例基准：16px（12pt@96dpi 等效）。 */
#define DEMO_SCALE_MAX_H   96   /**< 缩放高度上限：行高(22~30)、卡片/按钮
                                     (40~65)、复合控件(65~90) 都随字号长；
                                     >96 视为面板定尺（表格/页面容器）不缩放。 */
#define DEMO_SCALE_MAX_W   420  /**< 缩放宽度上限：文本驱动宽（按钮/标题行）
                                     随字号长；>420 视为面板/页面定尺。 */
#define DEMO_SCALE_MAX_F   1.6f /**< 比例上限（32/16=2，钳 1.6 防行宽爆）。 */

static int s_demoGlobalFontPx = 0; /**< 0=未设置（用框架缺省）。 */

int demo_global_font_px(void)
{
    static int envRead = 0;
    if (!envRead) {
        const char* env;
        envRead = 1;
        env = getenv("XGUI_DEMO_FONT_PX");
        if (env && env[0] && s_demoGlobalFontPx == 0) {
            int v = atoi(env);
            if (v >= 10 && v <= 32) s_demoGlobalFontPx = v;
        }
    }
    return s_demoGlobalFontPx;
}

void demo_set_global_font_px(int px)
{
    if (px < 10) px = 10;
    if (px > 32) px = 32;
    s_demoGlobalFontPx = px;
}

/** @brief 当前缩放比例（未设字号/≤基准 ⇒ 1.0）。 */
static float demo_scaleFactor(void)
{
    int px = s_demoGlobalFontPx;
    float f;
    if (px <= 0) return 1.0f;
    f = (float)px / (float)DEMO_SCALE_BASE_PX;
    if (f < 1.0f) f = 1.0f;
    if (f > DEMO_SCALE_MAX_F) f = DEMO_SCALE_MAX_F;
    return f;
}

/** @brief 读/写对象上的整型动态属性（基线存储；0=缺失）。 */
static int demo_propInt(const XObject* obj, const char* key, int dflt)
{
    XString* name;
    XVariant* v;
    int out = dflt;
    if (!obj) return dflt;
    name = XString_create_utf8(key);
    if (!name) return dflt;
    v = XObject_property_const(obj, name);
    if (v) out = (int)XVariant_toInt32(v);
    XClassDelete((XClass*)name);
    return out;
}

static void demo_propSetInt(XObject* obj, const char* key, int value)
{
    XString* name;
    XVariant* v;
    if (!obj) return;
    name = XString_create_utf8(key);
    v = XVariant_create_int32(value);
    if (!name || !v) {
        if (name) XClassDelete((XClass*)name);
        if (v) XClassDelete((XClass*)v);
        return;
    }
    /* XObject_setProperty **调用即接管** value（成功/失败都不归还；
     * Qt 6.8 setProperty 返回 false 只表示「未命中静态属性」，动态属性
     * 写入本身成功，见 XObject.h「所有权与返回值解耦 2026-11-11 口径
     * 翻转」）。旧写法按返回值释放 ⇒ 属性表里留下悬垂 value，之后
     * 任意堆复用都会把 vtable 踩坏（2026-11-17 大字号下状态栏退出段
     * XClass_deinit_base 段错误实锅）。 */
    (void)XObject_setProperty(obj, name, v);
    XClassDelete((XClass*)name);
}

/** @brief 单控件按基线缩放（首次登记基线；之后只按基线重算）。 */
static void demo_scaleOne(XObject* obj, float f)
{
    int hMin;
    int hMax;
    int wMin;
    int wMax;
    int base;
    int pxNow = s_demoGlobalFontPx;
    if (!XObject_inherits(obj, "XWidget")) return;
    /* 字号：显式设置像素值（0=未设置时不动，保持框架缺省口径）。 */
    if (pxNow > 0) {
        XFont font = XWidget_font((XWidget*)obj); /* 拷贝：堆串需释放 */
        XFont_setPixelSize(&font, pxNow);
        XWidget_setFont((XWidget*)obj, &font);
        XClassDeinit(&font);
    }
    hMin = XWidget_minimumHeight((XWidget*)obj);
    hMax = XWidget_maximumHeight((XWidget*)obj);
    if (hMin > 0 && hMin == hMax) {
        base = demo_propInt(obj, "demoBaseH", 0);
        if (base <= 0 && hMin <= DEMO_SCALE_MAX_H) {
            base = hMin;
            demo_propSetInt(obj, "demoBaseH", base);
        }
        if (base > 0) {
            int h = (int)((float)base * f + 0.5f);
            if (h < base) h = base;
            if (h != hMin)
                XWidget_setFixedHeight((XWidget*)obj, h);
        }
    }
    wMin = XWidget_minimumWidth((XWidget*)obj);
    wMax = XWidget_maximumWidth((XWidget*)obj);
    if (wMin > 0 && wMin == wMax) {
        base = demo_propInt(obj, "demoBaseW", 0);
        if (base <= 0 && wMin <= DEMO_SCALE_MAX_W) {
            base = wMin;
            demo_propSetInt(obj, "demoBaseW", base);
        }
        if (base > 0) {
            int w = (int)((float)base * f + 0.5f);
            if (w < base) w = base;
            if (w != wMin)
                XWidget_setFixedWidth((XWidget*)obj, w);
        }
    }
    if (XObject_inherits(obj, "XLabel")) {
        int px = XLabel_textPixelSize((const XLabel*)obj);
        base = demo_propInt(obj, "demoBasePx", 0);
        if (base <= 0 && px > 0) {
            base = px;
            demo_propSetInt(obj, "demoBasePx", base);
        }
        if (base > 0) {
            int np = (int)((float)base * f + 0.5f);
            if (np < base) np = base;
            if (np != px) XLabel_setTextPixelSize((XLabel*)obj, np);
        }
    }
}

static void demo_scaleWalk(XObject* obj, float f)
{
    const XVector* children;
    size_t i;
    size_t n;
    if (!obj) return;
    demo_scaleOne(obj, f);
    children = XObject_children(obj);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        demo_scaleWalk(child, f);
    }
}

void demo_scale_apply(XWidget* root)
{
    XLayout* layout;
    if (!root) return;
    demo_scaleWalk((XObject*)root, demo_scaleFactor());
    layout = XWidget_layout(root);
    if (layout)
        XLayout_invalidate(layout);
    XWidget_update(root);
}
