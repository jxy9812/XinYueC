/******************************************************************************
 * @file       XFontRegistration.c
 * @brief      XFont 字库 provider 注册装配层实现。
 * @details    [已移除 2026-10-07] XFontOutlineCommon 内置轮廓字库注册
 *             （GB2312 全集 XFO1 数据）随自研轮廓字实现整体删除；轮廓
 *             字形供货由 FT 文件后端（XFontFt）承担，此处仅装配点阵。
 ******************************************************************************/
#include "XFont8x16.h"
#include "XFont16x16.h"
#include "XFont32x32.h"
void XFontFace_registerBuiltinProviders(void)
{
#if XFONT_BUILTIN_8X16_ON
    (void)XFont8x16_register();
#endif /* XFONT_BUILTIN_8X16_ON */
#if XFONT_BUILTIN_16X16_ON
    (void)XFont16x16_register();
#endif /* XFONT_BUILTIN_16X16_ON */
#if XFONT_BUILTIN_32X32_ON
    (void)XFont32x32_register();
#endif /* XFONT_BUILTIN_32X32_ON */
}
