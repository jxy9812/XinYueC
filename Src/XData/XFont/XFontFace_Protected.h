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
#include "XByteArray.h"

/* Built-in provider assembly is private to the XFontFace implementation. */
void XFontFace_registerBuiltinProviders(void);

/* 整文件字节读取（XFont.c 实现）：FT/LVGL bin 文件后端共用的唯一
 * 读文件通道，自带 "../" 前缀剥离回退。成功时 *outBytes 归调用方所有
 * （XObject，XClassDelete 释放）。
 * [已移除 2026-10-07] XFontOutlineFace_fileInfo/fileLoadGlyph 声明：
 * XFO1 外挂轮廓文件后端随自研轮廓字实现整体删除，FT 为唯一轮廓字
 * 实现。 */
bool XFont_readFileBytes(const char* filePath, XByteArray** outBytes);

bool XFontBitmapFace_fileInfo(const XFont* self, XFontBitmapInfo* info);
bool XFontBitmapFace_fileLoadGlyph(const XFont* self, uint32_t codepoint,
                                   XFontGlyphDsc* dsc, unsigned char* out,
                                   size_t outSize);

#ifdef __cplusplus
}
#endif

#endif /* XFONTFACE_PROTECTED_H */
