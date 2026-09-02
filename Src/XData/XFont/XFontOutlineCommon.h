/******************************************************************************
 * @file       XFontOutlineCommon.h
 * @brief      内置常用汉字轮廓字库 provider 声明。
 * @details    字形使用 XFO1 设计单位轮廓，绘制时由 XFont 的字号和颜色
 *             决定缩放与填充。整套数据通过 XFONT_BUILTIN_OUTLINE_ON 裁剪。
 ******************************************************************************/
#ifndef XFONT_OUTLINE_COMMON_H
#define XFONT_OUTLINE_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XFontOutlineFace.h"

#if XFONT_BUILTIN_OUTLINE_ON
/** @brief 注册 ASCII/标点和 GB2312 一级常用汉字轮廓字库。 */
bool XFontOutlineCommon_register(void);
#endif /* XFONT_BUILTIN_OUTLINE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XFONT_OUTLINE_COMMON_H */
