/******************************************************************************
 * @file       XFontRegistration.c
 * @brief      XFont 字库 provider 注册装配层实现。
 ******************************************************************************/
#include "XFont8x16.h"
#include "XFont16x16.h"
#include "XFont32x32.h"
#include "XFontOutlineCommon.h"
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
#if XFONT_BUILTIN_OUTLINE_ON
    (void)XFontOutlineCommon_register();
#endif /* XFONT_BUILTIN_OUTLINE_ON */
}
