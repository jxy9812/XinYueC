/******************************************************************************
 * @file       XFontOutline_Xfo.h
 * @brief      XFO1 轮廓数据的内存解析 helper。
 ******************************************************************************/
#ifndef XFONT_OUTLINE_XFO_H
#define XFONT_OUTLINE_XFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XFontFace.h"

bool XFontOutline_Xfo_info(const unsigned char* data, size_t size,
                           XFontOutlineInfo* info);

bool XFontOutline_Xfo_loadGlyph(const unsigned char* data, size_t size,
                                uint32_t codepoint,
                                XFontOutlineGlyphMetrics* metrics,
                                const XFontOutlineSink* sink);

#ifdef __cplusplus
}
#endif

#endif /* XFONT_OUTLINE_XFO_H */
