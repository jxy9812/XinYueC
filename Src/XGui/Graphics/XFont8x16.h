/******************************************************************************
 * @file       XFont8x16.h
 * @brief      内置 8x16 单色点阵字体数据模块声明（纯数据，不依赖平台）。
 * @details    本模块只提供字体度量常量与按 Unicode 码点读取字形数据的
 *             XFont8x16_loadGlyph；解码、绘制、测宽、基线等高阶算法由
 *             XPainter 与 XFont 位图小工具负责，嵌入式可直接使用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFONT8X16_H
#define XFONT8X16_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 8x16 点阵字体的字形宽度（像素）。 */
#define XFONT8X16_WIDTH 8

/** @brief 8x16 点阵字体的字形高度（像素）。 */
#define XFONT8X16_HEIGHT 16

/** @brief 8x16 点阵字体的基线到字形顶部的参考高度（像素）。 */
#define XFONT8X16_ASCENT 13

/** @brief 8x16 点阵字体的基线到字形底部的参考高度（像素）。 */
#define XFONT8X16_DESCENT 3

/**
 * @brief      读取指定 Unicode 码点在 8x16 点阵字体中的行位图。
 * @param cp   Unicode 码点；非 ASCII 或无有效字形时返回空心方框替换字形。
 * @param out  输出缓冲区，长度至少 XFONT8X16_HEIGHT 字节；每字节对应一行，
 *             位 0 为最左像素，置 1 表示该像素点亮。
 */
void XFont8x16_loadGlyph(uint32_t cp, unsigned char out[XFONT8X16_HEIGHT]);

#ifdef __cplusplus
}
#endif
#endif /* XFONT8X16_H */
