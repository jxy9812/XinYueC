/******************************************************************************
 * @file       XFont32x32.h
 * @brief      内置 LVGL fmt_txt 32px 2bpp 字体注册声明。
 ******************************************************************************/
#ifndef XFONT32X32_H
#define XFONT32X32_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XFont.h"

#if XFONT_BUILTIN_32X32_ON
bool XFont32x32_register(void);
#endif /* XFONT_BUILTIN_32X32_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFONT32X32_H */
