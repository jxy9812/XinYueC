/******************************************************************************
 * @file       XFontOutlineCommon.c
 * @brief      内置常用汉字轮廓字库 provider 实现。
 ******************************************************************************/
#include "XFontOutlineCommon.h"
#include "XFontOutline_Xfo.h"

#if XFONT_BUILTIN_OUTLINE_ON

static const unsigned char g_xfontOutlineCommonLatin[] = {
#include "../../../Library/XFont/XFontOutlineCommonLatin.inc"
};

static const unsigned char g_xfontOutlineCommonCjk[] = {
#include "../../../Library/XFont/XFontOutlineCommonCjk.inc"
};

static bool XFontOutlineCommon_loadGlyph(
    uint32_t codepoint, XFontOutlineGlyphMetrics* metrics,
    const XFontOutlineSink* sink, void* userData)
{
    (void)userData;
    if (!metrics)
        return false;
    if (codepoint <= 0x7Fu &&
        XFontOutline_Xfo_loadGlyph(g_xfontOutlineCommonLatin,
                                   sizeof(g_xfontOutlineCommonLatin),
                                   codepoint, metrics, sink))
        return true;
    if (XFontOutline_Xfo_loadGlyph(g_xfontOutlineCommonCjk,
                                   sizeof(g_xfontOutlineCommonCjk), codepoint,
                                   metrics, sink))
        return true;
    return false;
}

static const XFontOutlineProvider g_xfontOutlineCommonProvider = {
    "XFontOutlineCommon", { 1000, 1160, 288, 0 },
    XFontOutlineCommon_loadGlyph, NULL
};

bool XFontOutlineCommon_register(void)
{
    return XFontOutlineFace_registerProvider(&g_xfontOutlineCommonProvider);
}

#endif /* XFONT_BUILTIN_OUTLINE_ON */
