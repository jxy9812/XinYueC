/*****************************************************************************
 * @file       XFontFace_Protected.h
 * @brief      XFontFace 文件后端使用的内部适配入口。
 * @details    这些函数只供内置 face 的文件解析实现调用；对外公开的
 *             字库访问必须使用 XFontFace_*_base()。
 ******************************************************************************/
#ifndef XFONTFACE_PROTECTED_H
#define XFONTFACE_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XFontFace.h"

/* Built-in provider assembly is private to the XFontFace implementation. */
void XFontFace_registerBuiltinProviders(void);

bool XFontBitmapFace_fileInfo(const XFont* self, XFontBitmapInfo* info);
bool XFontBitmapFace_fileLoadGlyph(const XFont* self, uint32_t codepoint,
                                   XFontGlyphDsc* dsc, unsigned char* out,
                                   size_t outSize);
bool XFontOutlineFace_fileInfo(const XFont* self, XFontOutlineInfo* info);
bool XFontOutlineFace_fileLoadGlyph(const XFont* self, uint32_t codepoint,
                                    XFontOutlineGlyphMetrics* metrics,
                                    const XFontOutlineSink* sink);

#ifdef __cplusplus
}
#endif

#endif /* XFONTFACE_PROTECTED_H */
