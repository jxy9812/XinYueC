#include "XGuiTest.h"
#if DEMOTEST
#include "XImageFormat.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"

/* ==================== XImageFormat 测试 ==================== */

/**
 * @brief      测试 XImageFormat 枚举值
 */
static void XImageFormatEnumTest(void)
{
    XPrintf("===== 像素格式枚举值测试 =====\n");
    XPrintf("XImageFormat_Invalid = %d (期望:0)\n", XImageFormat_Invalid);
    XPrintf("XImageFormat_Mono = %d (期望:1)\n", XImageFormat_Mono);
    XPrintf("XImageFormat_RGB32 = %d (期望:4)\n", XImageFormat_RGB32);
    XPrintf("XImageFormat_ARGB32 = %d (期望:5)\n", XImageFormat_ARGB32);
    XPrintf("XImageFormat_ARGB32_Premultiplied = %d (期望:6)\n", XImageFormat_ARGB32_Premultiplied);
    XPrintf("XImageFormat_RGB888 = %d (期望:13)\n", XImageFormat_RGB888);
    XPrintf("XImageFormat_RGBA8888 = %d (期望:17)\n", XImageFormat_RGBA8888);
    XPrintf("XImageFormat_Grayscale8 = %d (期望:24)\n", XImageFormat_Grayscale8);
    XPrintf("\n");
}

/**
 * @brief      测试 XImageFormat_bitDepth
 */
static void XImageFormatBitDepthTest(void)
{
    XPrintf("===== 像素格式位深度测试 =====\n");
    XPrintf("Invalid bitDepth = %d (期望:0)\n", XImageFormat_bitDepth(XImageFormat_Invalid));
    XPrintf("Mono bitDepth = %d (期望:1)\n", XImageFormat_bitDepth(XImageFormat_Mono));
    XPrintf("Indexed8 bitDepth = %d (期望:8)\n", XImageFormat_bitDepth(XImageFormat_Indexed8));
    XPrintf("RGB32 bitDepth = %d (期望:32)\n", XImageFormat_bitDepth(XImageFormat_RGB32));
    XPrintf("ARGB32 bitDepth = %d (期望:32)\n", XImageFormat_bitDepth(XImageFormat_ARGB32));
    XPrintf("RGB888 bitDepth = %d (期望:24)\n", XImageFormat_bitDepth(XImageFormat_RGB888));
    XPrintf("RGBA8888 bitDepth = %d (期望:32)\n", XImageFormat_bitDepth(XImageFormat_RGBA8888));
    XPrintf("Grayscale8 bitDepth = %d (期望:8)\n", XImageFormat_bitDepth(XImageFormat_Grayscale8));
    XPrintf("Grayscale16 bitDepth = %d (期望:16)\n", XImageFormat_bitDepth(XImageFormat_Grayscale16));
    XPrintf("Alpha8 bitDepth = %d (期望:8)\n", XImageFormat_bitDepth(XImageFormat_Alpha8));
    XPrintf("\n");
}

/**
 * @brief      测试 XImageFormat_hasAlpha
 */
static void XImageFormatHasAlphaTest(void)
{
    XPrintf("===== 像素格式 Alpha 通道测试 =====\n");
    XPrintf("Invalid hasAlpha = %s (期望:否)\n", XImageFormat_hasAlpha(XImageFormat_Invalid) ? "是" : "否");
    XPrintf("Mono hasAlpha = %s (期望:否)\n", XImageFormat_hasAlpha(XImageFormat_Mono) ? "是" : "否");
    XPrintf("RGB32 hasAlpha = %s (期望:否)\n", XImageFormat_hasAlpha(XImageFormat_RGB32) ? "是" : "否");
    XPrintf("ARGB32 hasAlpha = %s (期望:是)\n", XImageFormat_hasAlpha(XImageFormat_ARGB32) ? "是" : "否");
    XPrintf("ARGB32_Premultiplied hasAlpha = %s (期望:是)\n", XImageFormat_hasAlpha(XImageFormat_ARGB32_Premultiplied) ? "是" : "否");
    XPrintf("RGBA8888 hasAlpha = %s (期望:是)\n", XImageFormat_hasAlpha(XImageFormat_RGBA8888) ? "是" : "否");
    XPrintf("RGBX8888 hasAlpha = %s (期望:否)\n", XImageFormat_hasAlpha(XImageFormat_RGBX8888) ? "是" : "否");
    XPrintf("Alpha8 hasAlpha = %s (期望:是)\n", XImageFormat_hasAlpha(XImageFormat_Alpha8) ? "是" : "否");
    XPrintf("Grayscale8 hasAlpha = %s (期望:否)\n", XImageFormat_hasAlpha(XImageFormat_Grayscale8) ? "是" : "否");
    XPrintf("\n");
}

/**
 * @brief      测试 XImageFormat_isPremultiplied
 */
static void XImageFormatPremultipliedTest(void)
{
    XPrintf("===== 像素格式预乘 Alpha 测试 =====\n");
    XPrintf("ARGB32_Premultiplied isPremultiplied = %s (期望:是)\n", XImageFormat_isPremultiplied(XImageFormat_ARGB32_Premultiplied) ? "是" : "否");
    XPrintf("ARGB32 isPremultiplied = %s (期望:否)\n", XImageFormat_isPremultiplied(XImageFormat_ARGB32) ? "是" : "否");
    XPrintf("RGBA8888_Premultiplied isPremultiplied = %s (期望:是)\n", XImageFormat_isPremultiplied(XImageFormat_RGBA8888_Premultiplied) ? "是" : "否");
    XPrintf("RGBA8888 isPremultiplied = %s (期望:否)\n", XImageFormat_isPremultiplied(XImageFormat_RGBA8888) ? "是" : "否");
    XPrintf("RGB32 isPremultiplied = %s (期望:否)\n", XImageFormat_isPremultiplied(XImageFormat_RGB32) ? "是" : "否");
    XPrintf("\n");
}

/**
 * @brief      测试 XImageFormat_bytesPerLine
 */
static void XImageFormatBytesPerLineTest(void)
{
    XPrintf("===== 每行字节数计算测试 =====\n");
    XPrintf("RGB32 bytesPerLine(100) = %d (期望:400)\n", XImageFormat_bytesPerLine(100, XImageFormat_RGB32));
    XPrintf("ARGB32 bytesPerLine(100) = %d (期望:400)\n", XImageFormat_bytesPerLine(100, XImageFormat_ARGB32));
    XPrintf("RGB888 bytesPerLine(100) = %d (期望:304)\n", XImageFormat_bytesPerLine(100, XImageFormat_RGB888));
    XPrintf("Mono bytesPerLine(100) = %d (期望:16)\n", XImageFormat_bytesPerLine(100, XImageFormat_Mono));
    XPrintf("MonoLSB bytesPerLine(100) = %d (期望:16)\n", XImageFormat_bytesPerLine(100, XImageFormat_MonoLSB));
    XPrintf("Indexed8 bytesPerLine(100) = %d (期望:100)\n", XImageFormat_bytesPerLine(100, XImageFormat_Indexed8));
    XPrintf("Grayscale8 bytesPerLine(100) = %d (期望:100)\n", XImageFormat_bytesPerLine(100, XImageFormat_Grayscale8));
    XPrintf("RGBA8888 bytesPerLine(100) = %d (期望:400)\n", XImageFormat_bytesPerLine(100, XImageFormat_RGBA8888));
    XPrintf("RGB16 bytesPerLine(100) = %d (期望:200)\n", XImageFormat_bytesPerLine(100, XImageFormat_RGB16));
    XPrintf("Invalid bytesPerLine(100) = %d (期望:0)\n", XImageFormat_bytesPerLine(100, XImageFormat_Invalid));
    XPrintf("\n");
}

/**
 * @brief      测试 XImageFormat_bytesPerLineAlignment
 */
static void XImageFormatAlignmentTest(void)
{
    XPrintf("===== 行对齐字节数测试 =====\n");
    XPrintf("RGB32 alignment = %d (期望:4)\n", XImageFormat_bytesPerLineAlignment(XImageFormat_RGB32));
    XPrintf("Mono alignment = %d (期望:4)\n", XImageFormat_bytesPerLineAlignment(XImageFormat_Mono));
    XPrintf("RGB16 alignment = %d (期望:2)\n", XImageFormat_bytesPerLineAlignment(XImageFormat_RGB16));
    XPrintf("RGBA64 alignment = %d (期望:8)\n", XImageFormat_bytesPerLineAlignment(XImageFormat_RGBA64));
    XPrintf("\n");
}

/**
 * @brief      XImageFormat 综合测试入口
 */
void XMenu_XImageFormatTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImageFormat 格式枚举");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "枚举值测试");
    XAction_setAction(action, XImageFormatEnumTest);

    action = XMenu_addAction(menu, "位深度测试");
    XAction_setAction(action, XImageFormatBitDepthTest);

    action = XMenu_addAction(menu, "Alpha通道测试");
    XAction_setAction(action, XImageFormatHasAlphaTest);

    action = XMenu_addAction(menu, "预乘Alpha测试");
    XAction_setAction(action, XImageFormatPremultipliedTest);

    action = XMenu_addAction(menu, "每行字节数测试");
    XAction_setAction(action, XImageFormatBytesPerLineTest);

    action = XMenu_addAction(menu, "行对齐测试");
    XAction_setAction(action, XImageFormatAlignmentTest);
}
#endif // DEMOTEST


