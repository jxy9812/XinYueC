/******************************************************************************
 * @file       XFont16x16.h
 * @brief      内置 LVGL fmt_txt 16px 2bpp 字体注册声明。
 * @details    字库数据直接按 LVGL 的 glyph_bitmap、glyph_dsc 和 cmaps
 *             布局提供给 XFont；字库注册由 XFont16x16_register() 完成。
 ******************************************************************************/
#ifndef XFONT16X16_H
#define XFONT16X16_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XFontBitmapFace.h"

#define XFONT16X16_WIDTH 16
#define XFONT16X16_HEIGHT 16
#define XFONT16X16_ASCENT 15
#define XFONT16X16_DESCENT 4
#define XFONT16X16_BPP 2
#define XFONT16X16_ROW_BYTES 4

#if XFONT_BUILTIN_16X16_ON
bool XFont16x16_register(void);
#endif /* XFONT_BUILTIN_16X16_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFONT16X16_H */
