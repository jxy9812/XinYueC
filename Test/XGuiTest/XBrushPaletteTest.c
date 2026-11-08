/******************************************************************************
 * @file       XBrushPaletteTest.c
 * @brief      XBrush（对标 QBrush）/ XPalette 笔刷族 / 本轮改名与样式入口测试。
 * @details    覆盖：
 *             ① XBrush 样式、颜色、不透明、相等、拷贝、swap；
 *             ② 渐变拥有语义（源清空后池内副本仍完整）；
 *             ③ 纹理拥有语义（源释放后池内副本仍有效）；
 *             ④ XPalette brush/setBrush/isBrushSet/resolveMask/resolve/
 *                swap/cacheKey/isCopyOf 与纯色默认值；
 *             ⑤ XWidget_move_2 与 style()/setStyle()；
 *             ⑥ XBoxLayout_setStretchFactor / _2 重载命名。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBrushPaletteTest.h"

#include "XBrush.h"
#include "XPalette.h"
#include "XLabel.h"
#include "XBoxLayout.h"
#include "XLinearGradient.h"
#include "XPixmap.h"
#include "XWidget.h"
#include "XStyle.h"
#include "XMemory.h"
#include "XClass.h"

#include <stdio.h>

static int bp_failures = 0;

static void bp_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[BP-FAIL] %s\n", what ? what : "");
        ++bp_failures;
    }
}

static uint32_t bp_argb(XColor color)
{
    return XColor_rgba(&color);
}

bool XBrushPaletteTest_runAll(void)
{
    bp_failures = 0;

    /* ---------- ① XBrush 基本属性 ---------- */
    {
        XBrush b = XBrush_create();
        XColor red = XColor_create_rgb(255, 0, 0, 255);
        XBrush c;
        bp_expect(XBrush_style(&b) == XBrushStyle_NoBrush, "create 默认 NoBrush");
        XBrush_setColor(&b, red);
        XBrush_setStyle(&b, XBrushStyle_SolidPattern);
        bp_expect(XBrush_style(&b) == XBrushStyle_SolidPattern, "style 往返");
        bp_expect(bp_argb(XBrush_color(&b)) == 0xFFFF0000u, "color 往返");
        bp_expect(XBrush_isOpaque(&b), "纯色不透明");
        bp_expect(!XBrush_isDetached(&b), "isDetached 恒 false");
        XBrush_copy(&c, &b);
        bp_expect(XBrush_isEqual(&b, &c), "拷贝后相等");
        XBrush_setColor(&c, XColor_create_rgb(0, 0, 255, 255));
        bp_expect(!XBrush_isEqual(&b, &c), "改色后不等");
        XBrush_swap(&b, &c);
        bp_expect(bp_argb(XBrush_color(&b)) == 0xFF0000FFu, "swap 生效");
    }

    /* ---------- ② 渐变拥有语义 ---------- */
    {
        XLinearGradient lin;
        XBrush b = XBrush_create();
        const XGradient* owned;
        XLinearGradient_init_ex(&lin, 0.0, 0.0, 0.0, 8.0);
        XGradient_setColorAt((XGradient*)&lin, 0.0, 0xFFFF0000u);
        XGradient_setColorAt((XGradient*)&lin, 1.0, 0xFF0000FFu);
        XBrush_setGradient(&b, (const XGradient*)&lin);
        XGradient_clearStops((XGradient*)&lin);
        owned = XBrush_gradient(&b);
        bp_expect(owned != NULL, "渐变副本存在");
        bp_expect(owned && XGradient_stopCount(owned) == 2,
                  "源清空后副本仍有 2 个停止点");
        bp_expect(XBrush_style(&b) == XBrushStyle_LinearGradientPattern,
                  "设置渐变自动切渐变样式");
    }

    /* ---------- ③ 纹理拥有语义 ---------- */
    {
        XPixmap* pm = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XBrush b = XBrush_create();
        if (pm) {
            XPixmap_init_ex(pm, 4, 4);
            XBrush_setTexture(&b, pm);
            XClassDelete((XClass*)pm);
            bp_expect(XBrush_texture(&b) != NULL, "纹理副本存在");
            bp_expect(XBrush_texture(&b) && !XPixmap_isNull(XBrush_texture(&b)),
                      "源释放后纹理仍有效");
            bp_expect(XBrush_style(&b) == XBrushStyle_TexturePattern,
                      "设置纹理自动切纹理样式");
        } else {
            bp_expect(false, "测试纹理创建失败");
        }
    }

    /* ---------- ④ 调色板笔刷与解析族 ---------- */
    {
        XPalette p = XPalette_create();
        XColor windowColor;
        bp_expect(!XPalette_isBrushSet(&p, XPaletteColorGroup_Active,
                                       XPaletteColorRole_Button),
                  "默认未显式设置 Button");
        windowColor = XPalette_color(&p, XPaletteColorGroup_Active,
                                     XPaletteColorRole_Window);
        bp_expect(XColor_rgba(&windowColor) == 0xFFEFEFEFu,
                  "默认 Window=#efefef");
        {
            XBrush red = XBrush_create();
            XBrush readBack;
            XBrush_setColor(&red, XColor_create_rgb(255, 0, 0, 255));
            XBrush_setStyle(&red, XBrushStyle_SolidPattern);
            XPalette_setBrush(&p, XPaletteColorGroup_Active,
                              XPaletteColorRole_Button, &red);
            bp_expect(XPalette_isBrushSet(&p, XPaletteColorGroup_Active,
                                          XPaletteColorRole_Button),
                      "setBrush 后 isBrushSet 为真");
            bp_expect((XPalette_resolveMask(&p) &
                       ((uint64_t)1 << XPaletteColorRole_Button)) != 0,
                      "setBrush 置 resolveMask 位");
            readBack = XPalette_brush(&p, XPaletteColorGroup_Active,
                                      XPaletteColorRole_Button);
            bp_expect(bp_argb(XBrush_color(&readBack)) == 0xFFFF0000u,
                      "brush 读回红色");
        }
        {
            XPalette q = XPalette_create();
            XPalette merged;
            XBrush green = XBrush_create();
            XBrush textBrush;
            XBrush buttonBrush;
            XBrush_setColor(&green, XColor_create_rgb(0, 255, 0, 255));
            XBrush_setStyle(&green, XBrushStyle_SolidPattern);
            XPalette_setBrush(&q, XPaletteColorGroup_Active,
                              XPaletteColorRole_Text, &green);
            merged = XPalette_resolve(&p, &q);
            textBrush = XPalette_brush(&merged, XPaletteColorGroup_Active,
                                       XPaletteColorRole_Text);
            buttonBrush = XPalette_brush(&merged, XPaletteColorGroup_Active,
                                         XPaletteColorRole_Button);
            bp_expect(bp_argb(XBrush_color(&textBrush)) == 0xFF00FF00u,
                      "resolve 覆盖已解析角色 Text");
            bp_expect(bp_argb(XBrush_color(&buttonBrush)) == 0xFFFF0000u,
                      "resolve 保留未覆盖角色 Button");
        }
        {
            XPalette a = XPalette_create();
            XPalette b2;
            uint64_t k1, k2;
            XPalette_copy(&b2, &a);
            k1 = XPalette_cacheKey(&a);
            k2 = XPalette_cacheKey(&b2);
            bp_expect(k1 == k2, "内容相同 cacheKey 相同");
            bp_expect(XPalette_isCopyOf(&a, &b2), "isCopyOf 内容相同");
            XPalette_setColor(&b2, XPaletteColorGroup_Active,
                              XPaletteColorRole_Window,
                              XColor_create_rgb(1, 2, 3, 255));
            bp_expect(XPalette_cacheKey(&a) != XPalette_cacheKey(&b2),
                      "改色后 cacheKey 不同");
            XPalette_swap(&a, &b2);
            bp_expect(bp_argb(XPalette_color(&a, XPaletteColorGroup_Active,
                                             XPaletteColorRole_Window)) ==
                      0xFF010203u,
                      "swap 后内容互换");
        }
    }

    /* ---------- ⑤ move_2 与 style ---------- */
    {
        XLabel* label = XLabel_create(NULL, 0);
        if (label) {
            XPoint pos = { 12, 34 };
            XWidget_move_2((XWidget*)label, &pos);
            bp_expect(XWidget_pos((XWidget*)label).x == 12 &&
                      XWidget_pos((XWidget*)label).y == 34,
                      "move_2 生效");
            bp_expect(XWidget_style((XWidget*)label) == XStyle_defaultStyle(),
                      "未设控件样式时回落默认");
            XWidget_setStyle((XWidget*)label, XStyle_defaultStyle());
            bp_expect(XWidget_style((XWidget*)label) == XStyle_defaultStyle(),
                      "setStyle 生效（同一对象）");
            XWidget_setStyle((XWidget*)label, NULL);
            bp_expect(XWidget_style((XWidget*)label) == XStyle_defaultStyle(),
                      "setStyle(NULL) 回落默认");
            XClassDelete((XClass*)label);
        } else {
            bp_expect(false, "测试控件创建失败");
        }
    }

    /* ---------- ⑥ 布局伸展因子重载命名 ---------- */
    {
        XBoxLayout* box = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        XLabel* child = XLabel_create(NULL, 0);
        if (box && child) {
            XBoxLayout* inner = XBoxLayout_create(XBoxLayoutDirection_TopToBottom,
                                                  NULL);
            XBoxLayout_addWidget(box, (XWidget*)child);
            bp_expect(XBoxLayout_setStretchFactor(box, (XWidget*)child, 2),
                      "setStretchFactor(控件) 命中");
            bp_expect(XBoxLayout_stretch(box, 0) == 2, "stretch 读回 2");
            if (inner) {
                XBoxLayout_addLayout(box, (XLayout*)inner);
                bp_expect(XBoxLayout_setStretchFactor_2(box, (XLayout*)inner, 3),
                          "setStretchFactor_2(布局) 命中");
            }
        } else {
            bp_expect(false, "布局或子控件创建失败");
        }
        if (child) XClassDelete((XClass*)child);
        if (box) XClassDelete((XClass*)box);
    }

    if (bp_failures)
        fprintf(stderr, "XBrushPalette test: %d assertion(s) failed\n", bp_failures);
    return bp_failures == 0;
}
