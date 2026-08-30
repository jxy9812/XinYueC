/******************************************************************************
 * @file       XFont8x16.h
 * @brief      内置 8x16 单色点阵字体数据模块声明（纯数据，不依赖平台）。
 * @details    本模块拥有 8x16 字形数据及其 XFontBitmapProvider 描述；
 *             provider 直接使用 LVGL fmt_txt 的 glyph_bitmap、glyph_dsc
 *             和 cmaps 数据布局。XFont8x16_register() 负责把 provider
 *             注册到 XFont；启用时包含常用汉字 Unicode 字形。
 *             解码、绘制、测宽、基线等高阶算法由 XPainter 与
 *             XFont 位图小工具负责，嵌入式可直接使用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFONT8X16_H
#define XFONT8X16_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "XFont.h"

/** @brief 8x16 点阵字体的字形宽度（像素）。 */
#define XFONT8X16_WIDTH 8

/** @brief 8x16 点阵字体的字形高度（像素）。 */
#define XFONT8X16_HEIGHT 16
#define XFONT8X16_ROW_BYTES 1

/** @brief 8x16 点阵字体的基线到字形顶部的参考高度（像素）。 */
#define XFONT8X16_ASCENT 13

/** @brief 8x16 点阵字体的基线到字形底部的参考高度（像素）。 */
#define XFONT8X16_DESCENT 3

/** @brief 把 8x16 provider 注册到 XFont。 */
#if XFONT_BUILTIN_8X16_ON
bool XFont8x16_register(void);
#endif /* XFONT_BUILTIN_8X16_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFONT8X16_H */
