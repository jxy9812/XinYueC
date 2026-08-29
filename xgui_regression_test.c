/**
 * @file        xgui_regression_test.c
 * @brief       XGui Qt 对齐统一自动回归测试（无平台 API、无菜单依赖）
 * @details     本文件是普通 XGui 功能的唯一自动化回归入口，集中覆盖图像、编解码、
 *              IO、位图与像素图、图标、图片、绘制器、控件、布局和图形后端。
 *              需要真实桌面协议或人工窗口观察的独立端到端测试、演示程序不在此文件内。
 */

#include "XPrintf.h"
#include "XIcon.h"
#include "XIconThemeEngine.h"
#include "XIconThemeInternal.h"
#include "XDir.h"
#include "XBitmap.h"
#include "XGeometry.h"
#include "XImage.h"
#include "XImageCodec.h"
#include "XImageReader.h"
#include "XImageWriter.h"
#if XIMAGEIOPLUGIN_ON

static bool g_mockConsumeCapabilities = false;
static bool g_mockRejectCreate = false;
#include "XImageIOPlugin.h"
#include "XImageBuiltinPlugin.h"
#include "XImagePluginRegistry.h"
#endif /* XIMAGEIOPLUGIN_ON */
#include "XMovie.h"
#include "XPicture.h"
#include "XPainter.h"
#include "XPixmap.h"
#include "XPixmapCache.h"
#include "XFile.h"
#include "XString.h"
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
#include "codec_anim_fixture.h"
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */
#include "codec_assets_fixture.h"
#include "XStringList.h"
#include "XVector.h"
#include <stdlib.h>
#if XSCREEN_ON
#include "XScreen.h"
#endif /* XSCREEN_ON */
#if XSCREEN_ON && XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif /* XSCREEN_ON && XPLATFORMNATIVEWINDOW_ON */
#if XWIDGET_ON
#include "XWidget.h"
#endif /* XWIDGET_ON */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
#if XWIDGET_ON && XFRAME_ON
#include "XFrame.h"
#endif /* XWIDGET_ON && XFRAME_ON */
#if XWIDGET_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XAccessible.h"
#include "XPlatformAccessibility.h"
#endif /* XWINDOW_ON && XACCESSIBLE_ON */
#if XPLATFORMINTEGRATION_ON
#include "XGpu.h"
#include "XPlatformGraphics.h"
#endif /* XPLATFORMINTEGRATION_ON */

#if XLAYOUT_ON
#include "XLayoutItem.h"
#include "XLayout.h"
#endif /* XLAYOUT_ON */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
#include "XBoxLayout.h"
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
#if XLAYOUT_ON && XLAYOUT_GRID_ON
#include "XGridLayout.h"
#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
#if XLAYOUT_ON && XLAYOUT_STACKED_ON
#include "XStackedLayout.h"
#endif /* XLAYOUT_ON && XLAYOUT_STACKED_ON */

#include <stdio.h>
#include <string.h>
#include <math.h>

static int s_failures = 0;

static void expect_true(bool condition, const char* name)
{
    if (!condition) {
        XERROR_PRINTF("FAIL: %s\n", name);
        s_failures++;
    }
}

static void test_picture_put_u32(uint8_t* p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t test_picture_checksum(const uint8_t* data, uint32_t size)
{
    uint32_t hash = 2166136261u;
    uint32_t i;
    for (i = 0; i < size; ++i)
    {
        uint8_t value = (i >= 36u && i < 40u) ? 0u : data[i];
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

static bool test_picture_invalid_u32(const XPicture* source,
                                     uint32_t payloadOffset,
                                     uint32_t value)
{
    XPicture candidate;
    uint8_t* data;
    uint32_t size;
    bool invalid;
    if (!source) return false;
    size = XPicture_size(source);
    if (!XPicture_data(source) || size < XPICTURE_HEADER_SIZE ||
        payloadOffset > size || size - payloadOffset < 4u)
        return false;
    data = (uint8_t*)XMalloc_System(size);
    if (!data) return false;
    memcpy(data, XPicture_data(source), size);
    test_picture_put_u32(data + payloadOffset, value);
    test_picture_put_u32(data + 36u, 0u);
    test_picture_put_u32(data + 36u, test_picture_checksum(data, size));
    XPicture_init(&candidate, -1);
    XPicture_setData(&candidate, (const char*)data, size);
    invalid = !XPicture_isValidStream(&candidate);
    XPicture_deinit_base(&candidate);
    XFree_System(data);
    return invalid;
}

/** @brief 校验 Picture 状态记录的有限值、枚举与启用位防护。 */
static void test_picture_malformed_state_records(void)
{
    XPicture picture;
    XImageTransform matrix;

    memset(&matrix, 0, sizeof(matrix));
    matrix.m11 = 1.0f;
    matrix.m22 = 1.0f;
    matrix.m33 = 1.0f;

    XPicture_init(&picture, -1);
    expect_true(XPicture_recordSetOpacity(&picture, 0.5f),
                "malformed-state fixture records opacity");
    expect_true(test_picture_invalid_u32(&picture, 48u, 0x7fc00000u),
                "picture validator rejects NaN opacity");
    XPicture_clearCommands(&picture);

    expect_true(XPicture_recordSetCompositionMode(&picture,
                        (int)XPainterCompositionMode_SourceOver),
                "malformed-state fixture records composition mode");
    expect_true(test_picture_invalid_u32(&picture, 48u, 0xffffffffu),
                "picture validator rejects out-of-range composition mode");
    XPicture_clearCommands(&picture);

    expect_true(XPicture_recordSetBrushOrigin(&picture, 1.0f, 2.0f),
                "malformed-state fixture records brush origin");
    expect_true(test_picture_invalid_u32(&picture, 52u, 0x7fc00000u),
                "picture validator rejects NaN brush origin");
    XPicture_clearCommands(&picture);

    expect_true(XPicture_recordSetTransform(&picture, &matrix, true),
                "malformed-state fixture records transform");
    expect_true(test_picture_invalid_u32(&picture, 84u, 2u),
                "picture validator rejects invalid transform enable bit");
    XPicture_clearCommands(&picture);

    expect_true(XPicture_recordSetBackgroundMode(&picture, 0),
                "malformed-state fixture records background mode");
    expect_true(test_picture_invalid_u32(&picture, 48u, 2u),
                "picture validator rejects invalid background mode");
    XPicture_deinit_base(&picture);
}

#if XWIDGET_ON && XFRAME_ON
/** @brief XFrame 几何、样式和 short 线宽存储契约测试（对标 Qt 6.8 QFrame）。 */
static void test_frame_contract(void)
{
    XFrame frame;
    XRect wanted;
    XRect got;
    int style;

    memset(&frame, 0, sizeof(frame));
    XFrame_init(&frame, NULL, 0);
    XWidget_setGeometry(&frame.m_base, 0, 0, 100, 80);
    XFrame_setFrameStyle(&frame,
                         (int)XFrameShape_Box | (int)XFrameShadow_Raised);
    XFrame_setLineWidth(&frame, 2);
    XFrame_setMidLineWidth(&frame, 1);
    expect_true(XFrame_frameWidth(&frame) == 5,
                "QFrame Box/Raised frameWidth=2*line+mid");

    style = 0x14000 | (int)XFrameShape_Panel | (int)XFrameShadow_Sunken;
    XFrame_setFrameStyle(&frame, style);
    expect_true(XFrame_frameStyle(&frame) == (int)(short)style &&
                XFrame_frameShape(&frame) == XFrameShape_Panel &&
                XFrame_frameShadow(&frame) == XFrameShadow_Sunken,
                "QFrame setFrameStyle 按 short 截断并按掩码读取形状/阴影");

    wanted.x = 10;
    wanted.y = 8;
    wanted.width = 60;
    wanted.height = 40;
    XFrame_setFrameRect(&frame, &wanted);
    got = XFrame_frameRect(&frame);
    expect_true(got.x == wanted.x && got.y == wanted.y &&
                got.width == wanted.width && got.height == wanted.height,
                "QFrame setFrameRect 非全控件矩形往返保持一致");

    XFrame_setFrameRect(&frame, NULL);
    got = XFrame_frameRect(&frame);
    expect_true(got.x == 0 && got.y == 0 && got.width == 100 && got.height == 80,
                "QFrame null frameRect 回退到控件矩形");

    XFrame_setLineWidth(&frame, -1);
    expect_true(XFrame_lineWidth(&frame) == (int)(short)-1,
                "QFrame setLineWidth 按 short 语义保存负值");
    XFrame_setMidLineWidth(&frame, 65537);
    expect_true(XFrame_midLineWidth(&frame) == (int)(short)65537,
                "QFrame setMidLineWidth 按 short 语义保存溢出值");
    XFrame_deinit_base(&frame);
}
#endif /* XWIDGET_ON && XFRAME_ON */

/** @brief 统计图像中与指定背景色不同的像素数量。 */
static size_t image_count_non_background(const XImage* image,
                                         uint32_t background)
{
    size_t count = 0;
    int x;
    int y;
    if (!image) return 0;
    for (y = 0; y < XImage_height(image); ++y) {
        for (x = 0; x < XImage_width(image); ++x) {
            if (XImage_pixel(image, x, y) != background)
                ++count;
        }
    }
    return count;
}

/** @brief 返回图像中非背景像素的水平包围范围。 */
static bool image_non_background_x_bounds(const XImage* image,
                                          uint32_t background,
                                          int* outMinX, int* outMaxX)
{
    int x;
    int y;
    int minX = image ? XImage_width(image) : 0;
    int maxX = -1;
    if (!image) return false;
    for (y = 0; y < XImage_height(image); ++y) {
        for (x = 0; x < XImage_width(image); ++x) {
            if (XImage_pixel(image, x, y) != background) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
            }
        }
    }
    if (outMinX) *outMinX = minX;
    if (outMaxX) *outMaxX = maxX;
    return maxX >= 0;
}

#if XIMAGECODEC_ON
static void make_file_name(XString** out)
{
    *out = XString_create_utf8("xgui_regression.bmp");
}
#endif /* XIMAGECODEC_ON */

#if XIMAGECODEC_ON
static bool test_write_text_file(const char* path, const char* text)
{
    XString* pathString;
    XFile* file;
    bool ok;

    pathString = XString_create_utf8(path);
    if (!pathString) return false;
    file = XFile_create_2(pathString);
    ok = false;
    if (file && XIODevice_open_base((XIODevice*)file,
                                    XIODevice_WriteOnly | XIODevice_NewOnly)) {
        XIODevice_write_1((XIODevice*)file, text, (int64_t)strlen(text));
        XIODevice_close_base((XIODevice*)file);
        ok = true;
    }
    if (file) XClass_delete_base((XClass*)file);
    XString_delete_base((XClass*)pathString);
    return ok;
}

static bool test_write_binary_file(const char* path, const void* data,
                                   size_t size, bool replace)
{
    XString* pathString;
    XFile* file;
    bool ok;

    pathString = XString_create_utf8(path);
    if (!pathString) return false;
    file = XFile_create_2(pathString);
    ok = file && (!replace || !XFile_exists(file) || XFile_remove(file));
    if (ok && data && XIODevice_open_base((XIODevice*)file,
                                          XIODevice_WriteOnly | XIODevice_NewOnly)) {
        ok = XIODevice_write_1((XIODevice*)file, (const char*)data,
                               (int64_t)size) == (int64_t)size;
        XIODevice_close_base((XIODevice*)file);
    } else {
        ok = false;
    }
    if (file) XClass_delete_base((XClass*)file);
    XString_delete_base((XClass*)pathString);
    return ok;
}

static void test_cache_be16(uint8_t* data, size_t offset, uint16_t value)
{
    data[offset] = (uint8_t)(value >> 8);
    data[offset + 1u] = (uint8_t)value;
}

static void test_cache_be32(uint8_t* data, size_t offset, uint32_t value)
{
    data[offset] = (uint8_t)(value >> 24);
    data[offset + 1u] = (uint8_t)(value >> 16);
    data[offset + 2u] = (uint8_t)(value >> 8);
    data[offset + 3u] = (uint8_t)value;
}

static size_t test_make_icon_theme_cache(uint8_t* data, size_t capacity)
{
    static const char iconName[] = "example-icon";
    static const char appDirectory[] = "48x48/apps";
    static const char statusDirectory[] = "48x48/status";
    const size_t size = 112u;
    if (!data || capacity < size) return 0;
    memset(data, 0, size);
    test_cache_be16(data, 0, 1);       /* VERSION_MAJOR */
    test_cache_be32(data, 4, 16);      /* hash table */
    test_cache_be32(data, 8, 40);      /* directory list */
    test_cache_be32(data, 16, 1);      /* one hash bucket */
    test_cache_be32(data, 20, 24);     /* bucket -> node */
    test_cache_be32(data, 24, 0);      /* node.next */
    test_cache_be32(data, 28, 72);     /* node.name */
    test_cache_be32(data, 32, 52);     /* node.directory list */
    test_cache_be32(data, 40, 2);      /* two directories */
    test_cache_be32(data, 44, 88);
    test_cache_be32(data, 48, 99);
    test_cache_be32(data, 52, 2);      /* two icon entries */
    test_cache_be16(data, 56, 0);
    test_cache_be16(data, 64, 1);
    memcpy(data + 72, iconName, sizeof(iconName));
    memcpy(data + 88, appDirectory, sizeof(appDirectory));
    memcpy(data + 99, statusDirectory, sizeof(statusDirectory));
    return size;
}

static bool test_replace_text_file(const char* path, const char* text)
{
    XString* pathString;
    XFile* file;
    bool ok;

    pathString = XString_create_utf8(path);
    if (!pathString) return false;
    file = XFile_create_2(pathString);
    ok = file && (!XFile_exists(file) || XFile_remove(file));
    if (ok && XIODevice_open_base((XIODevice*)file,
                                  XIODevice_WriteOnly | XIODevice_NewOnly)) {
        XIODevice_write_1((XIODevice*)file, text, (int64_t)strlen(text));
        XIODevice_close_base((XIODevice*)file);
    } else {
        ok = false;
    }
    if (file) XClass_delete_base((XClass*)file);
    XString_delete_base((XClass*)pathString);
    return ok;
}

static bool test_make_theme_dir(const char* root, const char* relative)
{
    XString* rootString;
    XString* relativeString;
    XDir* dir;
    bool ok;

    rootString = XString_create_utf8(root);
    relativeString = XString_create_utf8(relative);
    dir = rootString ? XDir_create_2(rootString) : NULL;
    ok = dir && XDir_mkpath(dir, relativeString);
    if (dir) XDir_delete_base((XClass*)dir);
    if (relativeString) XString_delete_base((XClass*)relativeString);
    if (rootString) XString_delete_base((XClass*)rootString);
    return ok;
}

static void test_icon_theme_index_inherits(void)
{
    const char* tmpRoot = "xgui_icon_theme_tmp";
    XString* rootString = NULL;
    XDir* rootDir = NULL;
    XStringList* oldPaths = NULL;
    XStringList* oldFallbackPaths = NULL;
    XStringList* newPaths = NULL;
    XString* oldTheme = NULL;
    XString* oldFallback = NULL;
    XImage image;
    XPixmap pixmap;

    rootString = XString_create_utf8(tmpRoot);
    rootDir = rootString ? XDir_create_2(rootString) : NULL;
    if (rootDir) XDir_removeRecursively(rootDir);

    expect_true(test_make_theme_dir(tmpRoot, "Base/48x48/apps"),
                "index.theme fixture creates Base directory");
    expect_true(test_make_theme_dir(tmpRoot, "Base/48x48/status"),
                "index.theme fixture creates Base status directory");
    expect_true(test_make_theme_dir(tmpRoot, "Child/48x48/apps"),
                "index.theme fixture creates Child directory");
    expect_true(test_write_text_file(
                    "xgui_icon_theme_tmp/Base/index.theme",
                    "[Icon Theme]\nName=Base\n"
                    "Directories=48x48/apps,48x48/status\n\n"
                    "[48x48/apps]\nSize=48\nType=Fixed\nContext=Applications\n\n"
                    "[48x48/status]\nSize=48\nType=Fixed\nContext=Status\n"),
                "index.theme fixture writes Base metadata");
    expect_true(test_write_text_file(
                    "xgui_icon_theme_tmp/Child/index.theme",
                    "[Icon Theme]\nName=Child\nDirectories=48x48/apps\n"
                    "Inherits=Base\n\n"
                    "[48x48/apps]\nSize=48\nType=Fixed\nContext=Applications\n"),
                "index.theme fixture writes Child metadata");

    XImage_init_ex(&image, 48, 48, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(XImage_save_2(&image,
                    "xgui_icon_theme_tmp/Base/48x48/apps/example-icon.bmp",
                    "BMP", -1),
                "index.theme fixture writes application parent icon");
    XImage_fill(&image, 0xff33cc66u);
    expect_true(XImage_save_2(&image,
                    "xgui_icon_theme_tmp/Base/48x48/status/example-icon.bmp",
                    "BMP", -1),
                "index.theme fixture writes generic fallback icon");
    XImage_deinit_base(&image);

    {
        uint8_t cacheData[112];
        size_t cacheSize = test_make_icon_theme_cache(cacheData,
                                                      sizeof(cacheData));
        expect_true(cacheSize > 0 && test_write_binary_file(
                        "xgui_icon_theme_tmp/Base/icon-theme.cache",
                        cacheData, cacheSize, false),
                    "index.theme fixture writes valid icon-theme.cache");
    }

    oldPaths = XIcon_themeSearchPaths_2();
    oldFallbackPaths = XIcon_fallbackSearchPaths_2();
    oldTheme = XIcon_themeName();
    oldFallback = XIcon_fallbackThemeName();

    newPaths = XStringList_create();
    if (newPaths) XStringList_push_back_utf8(newPaths, tmpRoot);
    XIcon_setThemeSearchPaths(newPaths);
    XIcon_setFallbackSearchPaths(newPaths);
    XIcon_setThemeName_2("Child");
    XIcon_setFallbackThemeName_2("hicolor");

    XPixmap_init(&pixmap);
    expect_true(XIconInternal_resolveThemePixmapSize(
                    "example-icon", 48, &pixmap),
                "index.theme Inherits resolves parent theme icon");
    expect_true(XIcon_hasThemeIcon_2("example-icon"),
                "XIcon_hasThemeIcon uses the same inherited theme lookup");
    if (!XPixmap_isNull(&pixmap)) {
        expect_true(XPixmap_width(&pixmap) == 48 &&
                    XPixmap_height(&pixmap) == 48,
                    "index.theme inherited icon keeps target size");
    }
    XPixmap_deinit_base(&pixmap);

    {
        uint8_t filteredCache[112];
        size_t filteredSize = test_make_icon_theme_cache(
            filteredCache, sizeof(filteredCache));
        expect_true(test_make_theme_dir(tmpRoot, "Base/cache-only"),
                    "icon-theme.cache fixture creates cache-only directory");
        memcpy(filteredCache + 88, "cache-only", 11);
        memcpy(filteredCache + 99, "cache-only", 11);
        expect_true(filteredSize > 0 && test_write_binary_file(
                        "xgui_icon_theme_tmp/Base/icon-theme.cache",
                        filteredCache, filteredSize, true),
                    "icon-theme.cache fixture writes filtered directory list");
        XPixmap_init(&pixmap);
        expect_true(!XIconInternal_resolveThemePixmapSize(
                        "example-icon", 48, &pixmap),
                    "valid icon-theme.cache filters directories like Qt");
        XPixmap_deinit_base(&pixmap);
    }

    {
        static const uint8_t corruptCache[] = {0, 0, 0, 0};
        expect_true(test_write_binary_file(
                        "xgui_icon_theme_tmp/Base/icon-theme.cache",
                        corruptCache, sizeof(corruptCache), true),
                    "corrupt icon-theme.cache fixture writes");
        XPixmap_init(&pixmap);
        expect_true(XIconInternal_resolveThemePixmapSize(
                        "example-icon", 48, &pixmap),
                    "corrupt icon-theme.cache falls back to directory scan");
        XPixmap_deinit_base(&pixmap);
    }

    /* Qt 6.8 treats an existing index.theme as authoritative.  A file in
       Base/ itself is therefore not a theme entry when Base/index.theme does
       not declare that location. */
    XImage_init_ex(&image, 7, 5, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff996633u);
    expect_true(XImage_save_2(&image,
                    "xgui_icon_theme_tmp/Base/indexed-stray-icon.bmp",
                    "BMP", -1),
                "indexed theme fixture writes undeclared root icon");
    XImage_deinit_base(&image);
    XIcon_setThemeName_2("Base");
    XPixmap_init(&pixmap);
    expect_true(!XIconInternal_resolveThemePixmapSize(
                    "indexed-stray-icon", 48, &pixmap),
                "indexed theme rejects undeclared root icon");
    XPixmap_deinit_base(&pixmap);
    XIcon_setThemeName_2("Child");

    XPixmap_init(&pixmap);
    expect_true(XIconInternal_resolveThemePixmapSize(
                    "example-icon-tool", 48, &pixmap),
                "dash generic fallback resolves shorter icon name");
    if (!XPixmap_isNull(&pixmap)) {
        expect_true(XPixmap_width(&pixmap) == 48 &&
                    XPixmap_height(&pixmap) == 48,
                    "dash fallback icon keeps target size");
        XImage_init(&image);
        XPixmap_toImage(&pixmap, &image);
        expect_true((XImage_pixel(&image, 0, 0) & 0x00ffffffu) == 0x33cc66u,
                    "dash fallback skips Application/MimeType contexts");
        XImage_deinit_base(&image);
    }
    XPixmap_deinit_base(&pixmap);

    XIcon_setThemeSearchPaths(oldPaths);
    XIcon_setFallbackSearchPaths(oldFallbackPaths);
    XIcon_setThemeName(oldTheme);
    XIcon_setFallbackThemeName(oldFallback);

    if (rootDir) {
        XDir_removeRecursively(rootDir);
        XDir_delete_base((XClass*)rootDir);
    }
    if (rootString) XString_delete_base((XClass*)rootString);
    if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
    if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
    if (oldTheme) XString_delete_base((XClass*)oldTheme);
    if (oldFallback) XString_delete_base((XClass*)oldFallback);
    if (newPaths) XStringList_delete_base((XClass*)newPaths);
}

/** @brief 验证没有 index.theme 时，fallbackThemeName 仍从主题搜索路径加载传统图标。 */
static void test_icon_theme_fallback_legacy_search_path(void)
{
    const char* themeRoot = "xgui_icon_theme_legacy_tmp";
    const char* fallbackRoot = "xgui_icon_theme_legacy_other_tmp";
    const char* iconPath =
        "xgui_icon_theme_legacy_tmp/Fallback/legacy-fallback-icon.bmp";
    XString* themeRootString = NULL;
    XString* fallbackRootString = NULL;
    XDir* themeRootDir = NULL;
    XDir* fallbackRootDir = NULL;
    XStringList* oldPaths = NULL;
    XStringList* oldFallbackPaths = NULL;
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* oldTheme = NULL;
    XString* oldFallback = NULL;
    XImage image;
    XPixmap pixmap;

    themeRootString = XString_create_utf8(themeRoot);
    fallbackRootString = XString_create_utf8(fallbackRoot);
    themeRootDir = themeRootString ? XDir_create_2(themeRootString) : NULL;
    fallbackRootDir = fallbackRootString ?
        XDir_create_2(fallbackRootString) : NULL;
    if (themeRootDir) XDir_removeRecursively(themeRootDir);
    if (fallbackRootDir) XDir_removeRecursively(fallbackRootDir);

    expect_true(test_make_theme_dir(themeRoot, "Fallback"),
                "传统 fallback 主题创建目录");
    XImage_init_ex(&image, 6, 6, XImageFormat_ARGB32);
    XImage_fill(&image, 0xffcc6633u);
    expect_true(XImage_save_2(&image, iconPath, "BMP", -1),
                "传统 fallback 主题写入图标");
    XImage_deinit_base(&image);

    oldPaths = XIcon_themeSearchPaths_2();
    oldFallbackPaths = XIcon_fallbackSearchPaths_2();
    oldTheme = XIcon_themeName();
    oldFallback = XIcon_fallbackThemeName();

    themePaths = XStringList_create();
    fallbackPaths = XStringList_create();
    if (themePaths) XStringList_push_back_utf8(themePaths, themeRoot);
    if (fallbackPaths) XStringList_push_back_utf8(fallbackPaths, fallbackRoot);
    XIcon_setThemeSearchPaths(themePaths);
    XIcon_setFallbackSearchPaths(fallbackPaths);
    XIcon_setThemeName_2("MissingTheme");
    XIcon_setFallbackThemeName_2("Fallback");

    XPixmap_init(&pixmap);
    expect_true(XIconInternal_resolveThemePixmapSize(
                    "legacy-fallback-icon", 6, &pixmap),
                "fallbackThemeName 从 themeSearchPaths 加载传统图标");
    if (!XPixmap_isNull(&pixmap)) {
        XImage_init(&image);
        XPixmap_toImage(&pixmap, &image);
        expect_true(XPixmap_width(&pixmap) == 6 &&
                    XPixmap_height(&pixmap) == 6 &&
                    (XImage_pixel(&image, 0, 0) & 0x00ffffffu) == 0xcc6633u,
                    "传统 fallback 图标尺寸和像素正确");
        XImage_deinit_base(&image);
    }
    XPixmap_deinit_base(&pixmap);

    XIcon_setThemeSearchPaths(oldPaths);
    XIcon_setFallbackSearchPaths(oldFallbackPaths);
    XIcon_setThemeName(oldTheme);
    XIcon_setFallbackThemeName(oldFallback);

    if (themeRootDir) {
        XDir_removeRecursively(themeRootDir);
        XDir_delete_base((XClass*)themeRootDir);
    }
    if (fallbackRootDir) {
        XDir_removeRecursively(fallbackRootDir);
        XDir_delete_base((XClass*)fallbackRootDir);
    }
    if (themeRootString) XString_delete_base((XClass*)themeRootString);
    if (fallbackRootString) XString_delete_base((XClass*)fallbackRootString);
    if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
    if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
    if (oldTheme) XString_delete_base((XClass*)oldTheme);
    if (oldFallback) XString_delete_base((XClass*)oldFallback);
    if (themePaths) XStringList_delete_base((XClass*)themePaths);
    if (fallbackPaths) XStringList_delete_base((XClass*)fallbackPaths);
}

#if XIMAGECODEC_ON && XIMAGECODEC_PNG_ON
/** @brief 验证独立回退文件只来自 fallbackSearchPaths 且保留原始矩形尺寸。 */
static void test_icon_theme_standalone_fallback_sizes(void)
{
    const char* themeRoot = "xgui_icon_theme_standalone_theme_tmp";
    const char* fallbackRoot = "xgui_icon_theme_standalone_fallback_tmp";
    const char* themeFile =
        "xgui_icon_theme_standalone_theme_tmp/standalone-size-icon.png";
    const char* fallbackFile =
        "xgui_icon_theme_standalone_fallback_tmp/standalone-size-icon.png";
    XString* themeRootString = NULL;
    XString* fallbackRootString = NULL;
    XDir* themeRootDir = NULL;
    XDir* fallbackRootDir = NULL;
    XStringList* oldPaths = NULL;
    XStringList* oldFallbackPaths = NULL;
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* oldTheme = NULL;
    XString* oldFallback = NULL;
    XImage image;
    XPixmap pixmap;
    XVector sizes;
    XSize* size = NULL;

    themeRootString = XString_create_utf8(themeRoot);
    fallbackRootString = XString_create_utf8(fallbackRoot);
    themeRootDir = themeRootString ? XDir_create_2(themeRootString) : NULL;
    fallbackRootDir = fallbackRootString ?
        XDir_create_2(fallbackRootString) : NULL;
    if (themeRootDir) XDir_removeRecursively(themeRootDir);
    if (fallbackRootDir) XDir_removeRecursively(fallbackRootDir);
    expect_true(themeRootDir && fallbackRootDir &&
                    test_make_theme_dir(themeRoot, ".") &&
                    test_make_theme_dir(fallbackRoot, "."),
                "standalone fallback fixture creates search roots");

    XImage_init_ex(&image, 3, 3, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(XImage_save_2(&image, themeFile, "PNG", -1),
                "standalone fallback fixture writes theme-path decoy");
    XImage_deinit_base(&image);
    XImage_init_ex(&image, 7, 5, XImageFormat_ARGB32);
    XImage_fill(&image, 0xffcc6633u);
    expect_true(XImage_save_2(&image, fallbackFile, "PNG", -1),
                "standalone fallback fixture writes fallback PNG");
    XImage_deinit_base(&image);

    oldPaths = XIcon_themeSearchPaths_2();
    oldFallbackPaths = XIcon_fallbackSearchPaths_2();
    oldTheme = XIcon_themeName();
    oldFallback = XIcon_fallbackThemeName();
    themePaths = XStringList_create();
    fallbackPaths = XStringList_create();
    if (themePaths) XStringList_push_back_utf8(themePaths, themeRoot);
    if (fallbackPaths) XStringList_push_back_utf8(fallbackPaths, fallbackRoot);
    XIcon_setThemeSearchPaths(themePaths);
    XIcon_setFallbackSearchPaths(fallbackPaths);
    XIcon_setThemeName_2("MissingTheme");
    XIcon_setFallbackThemeName_2("");

    XPixmap_init(&pixmap);
    expect_true(XIconInternal_resolveThemePixmapSourceSize(
                    "standalone-size-icon", 48, &pixmap),
                "standalone fallback resolves only from fallbackSearchPaths");
    expect_true(!XIcon_hasThemeIcon_2(fallbackFile),
                "hasThemeIcon does not treat a relative standalone file as a theme icon");
    expect_true(!XIcon_hasThemeIcon_2(":/standalone-size-icon"),
                "hasThemeIcon does not treat a Qt resource path as a theme icon");
    if (!XPixmap_isNull(&pixmap)) {
        XImage_init(&image);
        XPixmap_toImage(&pixmap, &image);
        expect_true(XPixmap_width(&pixmap) == 7,
                    "standalone fallback keeps fallback width");
        expect_true(XPixmap_height(&pixmap) == 5,
                    "standalone fallback keeps fallback height");
        expect_true((XImage_pixel(&image, 0, 0) & 0x00ffffffu) == 0xcc6633u,
                    "standalone fallback ignores theme-path decoy");
        XImage_deinit_base(&image);
    }
    XPixmap_deinit_base(&pixmap);

    XVector_init(&sizes, sizeof(XSize), true);
    expect_true(XIconInternal_availableThemeSizes(
                    "standalone-size-icon", &sizes),
                "standalone fallback contributes availableSizes");
    if (XVector_size_base((const XContainer*)&sizes) == 1)
        size = (XSize*)XVector_at_base(&sizes, 0);
    expect_true(size && size->width == 7 && size->height == 5,
                "standalone fallback availableSizes preserves 7x5 source");
    XVector_deinit_base((XClass*)&sizes);

    XIcon_setThemeSearchPaths(oldPaths);
    XIcon_setFallbackSearchPaths(oldFallbackPaths);
    XIcon_setThemeName(oldTheme);
    XIcon_setFallbackThemeName(oldFallback);
    if (themeRootDir) {
        XDir_removeRecursively(themeRootDir);
        XDir_delete_base((XClass*)themeRootDir);
    }
    if (fallbackRootDir) {
        XDir_removeRecursively(fallbackRootDir);
        XDir_delete_base((XClass*)fallbackRootDir);
    }
    if (themeRootString) XString_delete_base((XClass*)themeRootString);
    if (fallbackRootString) XString_delete_base((XClass*)fallbackRootString);
    if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
    if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
    if (oldTheme) XString_delete_base((XClass*)oldTheme);
    if (oldFallback) XString_delete_base((XClass*)oldFallback);
    if (themePaths) XStringList_delete_base((XClass*)themePaths);
    if (fallbackPaths) XStringList_delete_base((XClass*)fallbackPaths);
}
#endif /* XIMAGECODEC_ON && XIMAGECODEC_PNG_ON */

static void test_icon_theme_engine_paint_scales_to_rect(void)
{
    const char* tmpRoot = "xgui_icon_theme_engine_paint_tmp";
    XString* rootString = NULL;
    XDir* rootDir = NULL;
    XStringList* oldPaths = NULL;
    XStringList* oldFallbackPaths = NULL;
    XStringList* newPaths = NULL;
    XString* oldTheme = NULL;
    XString* oldFallback = NULL;
    XImage source;
    XImage target;
    XPainter painter;
    XRect rect;
    XIconThemeEngine* engine;

    rootString = XString_create_utf8(tmpRoot);
    rootDir = rootString ? XDir_create_2(rootString) : NULL;
    if (rootDir) XDir_removeRecursively(rootDir);

    expect_true(test_make_theme_dir(tmpRoot, "Base/48x48/apps"),
                "theme engine paint creates theme directory");
    expect_true(test_replace_text_file(
                    "xgui_icon_theme_engine_paint_tmp/Base/index.theme",
                    "[Icon Theme]\nName=Base\nDirectories=48x48/apps\n\n"
                    "[48x48/apps]\nSize=48\nType=Fixed\nContext=Applications\n"),
                "theme engine paint writes index.theme");

    XImage_init_ex(&source, 48, 48, XImageFormat_ARGB32);
    XImage_fill(&source, 0xff33cc66u);
    expect_true(XImage_save_2(
                    &source,
                    "xgui_icon_theme_engine_paint_tmp/Base/48x48/apps/example-icon.bmp",
                    "BMP", -1),
                "theme engine paint writes theme icon BMP");
    XImage_deinit_base(&source);

    oldPaths = XIcon_themeSearchPaths_2();
    oldFallbackPaths = XIcon_fallbackSearchPaths_2();
    oldTheme = XIcon_themeName();
    oldFallback = XIcon_fallbackThemeName();
    newPaths = XStringList_create();
    if (newPaths) XStringList_push_back_utf8(newPaths, tmpRoot);
    XIcon_setThemeSearchPaths(newPaths);
    XIcon_setFallbackSearchPaths(newPaths);
    XIcon_setThemeName_2("Base");
    XIcon_setFallbackThemeName_2("hicolor");

    engine = XIconThemeEngine_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          "example-icon");
    expect_true(engine != NULL, "theme engine paint creates engine");
    if (!engine) {
        XIcon_setThemeSearchPaths(oldPaths);
        XIcon_setFallbackSearchPaths(oldFallbackPaths);
        XIcon_setThemeName(oldTheme);
        XIcon_setFallbackThemeName(oldFallback);
        if (rootDir) {
            XDir_removeRecursively(rootDir);
            XDir_delete_base((XClass*)rootDir);
        }
        if (rootString) XString_delete_base((XClass*)rootString);
        if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
        if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
        if (oldTheme) XString_delete_base((XClass*)oldTheme);
        if (oldFallback) XString_delete_base((XClass*)oldFallback);
        if (newPaths) XStringList_delete_base((XClass*)newPaths);
        return;
    }
    {
        XPixmap invalidScale;
        XSize requested = {8, 8};
        XPixmap_init(&invalidScale);
        XIconEngine_scaledPixmap_base((const XIconEngine*)engine, &requested,
                                       XIconMode_Normal, XIconState_Off, 0.0f,
                                       &invalidScale);
        expect_true(XPixmap_isNull(&invalidScale),
                    "theme engine rejects non-positive scaledPixmap ratio");
        XPixmap_deinit_base(&invalidScale);
    }
    {
        XPixmap invalidScale;
        XSize requested = {8, 8};
        XPixmap_init(&invalidScale);
        XIconEngine_scaledPixmap_base((const XIconEngine*)engine, &requested,
                                       XIconMode_Normal, XIconState_Off, NAN,
                                       &invalidScale);
        expect_true(XPixmap_isNull(&invalidScale),
                    "theme engine rejects NaN scaledPixmap ratio");
        XPixmap_deinit_base(&invalidScale);
        XPixmap_init(&invalidScale);
        XIconEngine_scaledPixmap_base((const XIconEngine*)engine, &requested,
                                       XIconMode_Normal, XIconState_Off,
                                       INFINITY, &invalidScale);
        expect_true(XPixmap_isNull(&invalidScale),
                    "theme engine rejects infinite scaledPixmap ratio");
        XPixmap_deinit_base(&invalidScale);
    }
    {
        XIcon themedIcon;
        XIconThemeEngine* measureEngine;
        XSize actual;
        measureEngine = XIconThemeEngine_create_2_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, "example-icon");
        XIcon_init_engine(&themedIcon, (XIconEngine*)measureEngine);
        XIcon_actualSize(&themedIcon, 24, 18, XIconMode_Normal,
                         XIconState_Off, &actual);
        expect_true(actual.width == 18 && actual.height == 18,
                    "theme icon actualSize uses the smaller request edge");
        XIcon_actualSize(&themedIcon, 96, 80, XIconMode_Normal,
                         XIconState_Off, &actual);
        expect_true(actual.width == 48 && actual.height == 48,
                    "fixed theme actualSize never exceeds source directory size");
        XIcon_deinit_base(&themedIcon);
    }
    {
        XIcon sizedIcon;
        XIconThemeEngine* sizedEngine;
        XVector sizes;
        XSize* available;
        XPixmap normalPixmap;
        XPixmap hiDpiPixmap;
        sizedEngine = XIconThemeEngine_create_2_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, "example-icon");
        XIcon_init_engine(&sizedIcon, (XIconEngine*)sizedEngine);
        XVector_init(&sizes, sizeof(XSize), true);
        XIcon_availableSizes(&sizedIcon, XIconMode_Normal, XIconState_Off,
                             &sizes);
        available = (XSize*)XVector_at_base(&sizes, 0);
        expect_true(XVector_size_base((const XContainer*)&sizes) == 1 &&
                    available && available->width == 48 &&
                    available->height == 48,
                    "theme icon availableSizes reports index.theme size");
        XPixmap_init(&normalPixmap);
        XIcon_pixmap(&sizedIcon, 24, 18, XIconMode_Normal,
                     XIconState_Off, &normalPixmap);
        expect_true(XPixmap_width(&normalPixmap) == 18 &&
                    XPixmap_height(&normalPixmap) == 18,
                    "theme icon pixmap uses the smaller request edge");
        XPixmap_init(&hiDpiPixmap);
        XIcon_pixmapRatio(&sizedIcon, 24, 18, 2.0f, XIconMode_Normal,
                          XIconState_Off, &hiDpiPixmap);
        expect_true(XPixmap_width(&hiDpiPixmap) == 36 &&
                    XPixmap_height(&hiDpiPixmap) == 36 &&
                    XPixmap_devicePixelRatio(&hiDpiPixmap) == 2.0f,
                    "theme icon scaledPixmap uses physical smaller edge");
        XPixmap_deinit_base(&hiDpiPixmap);
        XPixmap_deinit_base(&normalPixmap);
        XVector_deinit_base((XClass*)&sizes);
        XIcon_deinit_base(&sizedIcon);
    }
    expect_true(test_replace_text_file(
                    "xgui_icon_theme_engine_paint_tmp/Base/index.theme",
                    "[Icon Theme]\nName=Base\nDirectories=48x48/apps\n\n"
                    "[48x48/apps]\nSize=48\nType=Scalable\nMinSize=1\n"
                    "MaxSize=128\nContext=Applications\n"),
                "theme engine paint rewrites scalable metadata");
    {
        XIcon scalableIcon;
        XIconThemeEngine* scalableEngine;
        XSize scalableActual;
        scalableEngine = XIconThemeEngine_create_2_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, "example-icon");
        XIcon_init_engine(&scalableIcon, (XIconEngine*)scalableEngine);
        XIcon_actualSize(&scalableIcon, 24, 18, XIconMode_Normal,
                         XIconState_Off, &scalableActual);
        expect_true(scalableActual.width == 24 &&
                    scalableActual.height == 18,
                    "theme scalable actualSize preserves request rectangle");
        XIcon_deinit_base(&scalableIcon);
    }
    XImage_init_ex(&target, 60, 30, XImageFormat_ARGB32);
    XImage_fill(&target, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target),
                "theme engine paint begins image");
    rect.x = 4;
    rect.y = 2;
    rect.width = 24;
    rect.height = 18;
    XIconEngine_paint_base((const XIconEngine*)engine, &painter, &rect,
                           XIconMode_Normal, XIconState_Off);
    expect_true(XPainter_end(&painter), "theme engine paint ends image");
    XPainter_deinit(&painter);

    expect_true((XImage_pixel(&target, rect.x, rect.y) & 0x00ffffffu) == 0x33cc66u,
                "theme engine paint fills target top-left");
    expect_true((XImage_pixel(&target, rect.x + rect.width - 1,
                              rect.y + rect.height - 1) & 0x00ffffffu) == 0x33cc66u,
                "theme engine paint fills target bottom-right");
    expect_true((XImage_pixel(&target, rect.x - 1, rect.y + rect.height / 2) & 0x00ffffffu) == 0x0u,
                "theme engine paint leaves left background");
    expect_true((XImage_pixel(&target, rect.x + rect.width,
                              rect.y + rect.height / 2) & 0x00ffffffu) == 0x0u,
                "theme engine paint leaves right background");

    XImage_deinit_base(&target);
    XIconThemeEngine_delete_base(engine);

    XIcon_setThemeSearchPaths(oldPaths);
    XIcon_setFallbackSearchPaths(oldFallbackPaths);
    XIcon_setThemeName(oldTheme);
    XIcon_setFallbackThemeName(oldFallback);
    if (rootDir) {
        XDir_removeRecursively(rootDir);
        XDir_delete_base((XClass*)rootDir);
    }
    if (rootString) XString_delete_base((XClass*)rootString);
    if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
    if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
    if (oldTheme) XString_delete_base((XClass*)oldTheme);
    if (oldFallback) XString_delete_base((XClass*)oldFallback);
    if (newPaths) XStringList_delete_base((XClass*)newPaths);
}

static void test_icon_theme_scale_selection(void)
{
    const char* tmpRoot = "xgui_icon_theme_scale_tmp";
    XString* rootString = NULL;
    XDir* rootDir = NULL;
    XStringList* oldPaths = NULL;
    XStringList* oldFallbackPaths = NULL;
    XStringList* newPaths = NULL;
    XString* oldTheme = NULL;
    XString* oldFallback = NULL;
    XImage image;
    XPixmap pixmap;
    XImage decoded;

    rootString = XString_create_utf8(tmpRoot);
    rootDir = rootString ? XDir_create_2(rootString) : NULL;
    if (rootDir) XDir_removeRecursively(rootDir);
    expect_true(test_make_theme_dir(tmpRoot, "Base/36x36/apps"),
                "scaled theme fixture creates 1x directory");
    expect_true(test_make_theme_dir(tmpRoot, "Base/24x24/apps"),
                "scaled theme fixture creates 2x directory");
    expect_true(test_write_text_file(
                    "xgui_icon_theme_scale_tmp/Base/index.theme",
                    "[Icon Theme]\nName=Base\n"
                    "Directories=36x36/apps,24x24/apps\n\n"
                    "[36x36/apps]\nSize=36\nType=Fixed\nScale=1\n"
                    "Context=Applications\n\n"
                    "[24x24/apps]\nSize=24\nType=Fixed\nScale=2\n"
                    "Context=Applications\n"),
                "scaled theme fixture writes Scale metadata");

    XImage_init_ex(&image, 36, 36, XImageFormat_ARGB32);
    XImage_fill(&image, 0xffcc3333u);
    expect_true(XImage_save_2(
                    &image,
                    "xgui_icon_theme_scale_tmp/Base/36x36/apps/example-icon.bmp",
                    "BMP", -1),
                "scaled theme fixture writes 1x icon");
    XImage_fill(&image, 0xff3366ccu);
    expect_true(XImage_save_2(
                    &image,
                    "xgui_icon_theme_scale_tmp/Base/24x24/apps/example-icon.bmp",
                    "BMP", -1),
                "scaled theme fixture writes 2x icon");
    XImage_deinit_base(&image);

    oldPaths = XIcon_themeSearchPaths_2();
    oldFallbackPaths = XIcon_fallbackSearchPaths_2();
    oldTheme = XIcon_themeName();
    oldFallback = XIcon_fallbackThemeName();
    newPaths = XStringList_create();
    if (newPaths) XStringList_push_back_utf8(newPaths, tmpRoot);
    XIcon_setThemeSearchPaths(newPaths);
    XIcon_setFallbackSearchPaths(newPaths);
    XIcon_setThemeName_2("Base");
    XIcon_setFallbackThemeName_2("hicolor");

    XPixmap_init(&pixmap);
    expect_true(XIconInternal_resolveThemePixmapSizeScale(
                    "example-icon", 24, 2, 36, &pixmap),
                "scaled theme fixture resolves requested directory Scale");
    XImage_init(&decoded);
    XPixmap_toImage(&pixmap, &decoded);
    expect_true(XPixmap_width(&pixmap) == 36 && XPixmap_height(&pixmap) == 36 &&
                    (XImage_pixel(&decoded, 0, 0) & 0x00ffffffu) == 0x3366ccu,
                "1.5x request prefers 2x directory over equal physical 1x");
    XImage_deinit_base(&decoded);
    XPixmap_deinit_base(&pixmap);

    XIcon_setThemeSearchPaths(oldPaths);
    XIcon_setFallbackSearchPaths(oldFallbackPaths);
    XIcon_setThemeName(oldTheme);
    XIcon_setFallbackThemeName(oldFallback);
    if (rootDir) {
        XDir_removeRecursively(rootDir);
        XDir_delete_base((XClass*)rootDir);
    }
    if (rootString) XString_delete_base((XClass*)rootString);
    if (oldPaths) XStringList_delete_base((XClass*)oldPaths);
    if (oldFallbackPaths) XStringList_delete_base((XClass*)oldFallbackPaths);
    if (oldTheme) XString_delete_base((XClass*)oldTheme);
    if (oldFallback) XString_delete_base((XClass*)oldFallback);
    if (newPaths) XStringList_delete_base((XClass*)newPaths);
}
#endif /* XIMAGECODEC_ON */

typedef struct PictureProbe
{
    int lineCalls;
    int fillCalls;
    int saveCalls;
    int restoreCalls;
    int imageCalls;
    bool imagePayloadOk;
    int shapeCalls;
    int polylineCalls;
    int polygonCalls;
    int pointsCalls;
    int pathCalls;
    bool shapeOpOk;
    bool polygonOk;
    bool pointsOk;
    bool pathOk;
} PictureProbe;

static bool picture_probe_line(XPainter* p, int x1, int y1, int x2, int y2)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    (void)x1; (void)y1; (void)x2; (void)y2;
    ++probe->lineCalls;
    return true;
}

static bool picture_probe_fill(XPainter* p, const XRect* r, uint32_t color)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    (void)r; (void)color;
    ++probe->fillCalls;
    return true;
}

static bool picture_probe_state(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->saveCalls;
    return true;
}

static bool picture_probe_restore(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->restoreCalls;
    return true;
}

#if XPAINTER_SHAPE_ON
static bool picture_probe_shape(XPainter* p, XPainterShapeOp op,
                                const XRect* r, int startAngle, int spanAngle,
                                bool filled, int xRadius, int yRadius)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->shapeCalls;
    probe->shapeOpOk = op == XPainterShapeOp_RoundedRect &&
                       r && r->x == 10 && r->y == 20 &&
                       r->width == 5 && r->height == 7 &&
                       startAngle == 0 && spanAngle == 0 && filled &&
                       xRadius == 2 && yRadius == 3;
    return probe->shapeOpOk;
}
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
static bool picture_probe_polyline(XPainter* p, const XPoint* points,
                                   int count)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->polylineCalls;
    return points && count == 2 && points[0].x == 1 && points[0].y == 2 &&
           points[1].x == 3 && points[1].y == 4;
}

static bool picture_probe_polygon(XPainter* p, const XPoint* points, int count,
                                  bool filled, XPainterFillRule fillRule)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->polygonCalls;
    probe->polygonOk = points && count == 3 && filled &&
                       fillRule == XPainterFillRule_Winding &&
                       points[0].x == 0 && points[0].y == 0 &&
                       points[1].x == 4 && points[1].y == 0 &&
                       points[2].x == 0 && points[2].y == 3;
    return probe->polygonOk;
}

static bool picture_probe_points(XPainter* p, const XPoint* points, int count)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->pointsCalls;
    probe->pointsOk = points && count == 2 &&
                      points[0].x == 7 && points[0].y == 8 &&
                      points[1].x == 9 && points[1].y == 10;
    return probe->pointsOk;
}
#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
static bool picture_probe_path(XPainter* p, XPainterPathOp op,
                               const struct XPainterPath* path)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->pathCalls;
    probe->pathOk = op == XPainterPathOp_Draw && path &&
                    path->m_elementCount == 5 &&
                    path->m_elements[0].m_type == XPainterPathElement_MoveTo &&
                    path->m_elements[0].m_x1 == 1.0f &&
                    path->m_elements[0].m_y1 == 1.0f &&
                    path->m_elements[4].m_type == XPainterPathElement_LineTo &&
                    path->m_elements[4].m_x1 == 1.0f &&
                    path->m_elements[4].m_y1 == 1.0f;
    return probe->pathOk;
}
#endif /* XPAINTER_PATH_ON */

static bool picture_probe_image(XPainter* p, const XImage* image, int x, int y)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->imageCalls;
    probe->imagePayloadOk = image && x == 8 && y == 9 &&
                            XImage_width(image) == 2 && XImage_height(image) == 1 &&
                            XImage_pixel(image, 0, 0) == 0xff102030u &&
                            XImage_devicePixelRatio(image) == 2.0f;
    return probe->imagePayloadOk;
}

static void test_pixmap_lifecycle(void)
{
    XPixmap source;
    XPixmap empty;
    XPixmap target;
    XPixmap copied;
    XPixmap transformed;
    XImage image;

    XPixmap_init_ex(&source, 2, 3);
    XPixmap_fill(&source, 0xff336699u);
    XPixmap_setDevicePixelRatio(&source, 2.0f);
    {
        XSizeF independent;
        XPixmap_deviceIndependentSize(&source, &independent);
        expect_true(fabsf(independent.width - 1.0f) < 1.0e-5f &&
                    fabsf(independent.height - 1.5f) < 1.0e-5f,
                    "pixmap deviceIndependentSize divides by DPR");
        XPixmap_setDevicePixelRatio(&source, 0.0f);
        expect_true(XPixmap_devicePixelRatio(&source) == 0.0f,
                    "QPixmap-compatible devicePixelRatio accepts zero");
        XPixmap_setDevicePixelRatio(&source, -2.0f);
        expect_true(XPixmap_devicePixelRatio(&source) == -2.0f,
                    "QPixmap-compatible devicePixelRatio accepts negative values");
        XPixmap_setDevicePixelRatio(&source, 2.0f);
    }

    memset(&copied, 0, sizeof(copied));
    XPixmap_copy_base(&copied, &source);
    expect_true(XPixmap_width(&copied) == 2 && XPixmap_height(&copied) == 3,
                "copy_base initializes an uninitialized destination");

    XPixmap_init(&transformed);
    XPixmap_transformed(&source, 0.0f, -1.0f, 3.0f,
                        1.0f, 0.0f, 0.0f, 0, &transformed);
    expect_true(XPixmap_width(&transformed) == 3 && XPixmap_height(&transformed) == 2,
                "transformed rotates dimensions");

    XImage_init(&image);
    XPixmap_toImage(&source, &image);
    expect_true(XImage_width(&image) == 2 && XImage_height(&image) == 3,
                "pixmap converts to image");
    expect_true(XImage_devicePixelRatio(&image) == 2.0f,
                "pixmap to image preserves device pixel ratio");

    XPixmap_init(&empty);
    XPixmap_toImage(&empty, &image);
    expect_true(XImage_isNull(&image),
                "pixmap to image clears the output for an empty source");
    XPixmap_init_ex(&target, 1, 1);
    XPixmap_fromImage(NULL, 0, &target);
    expect_true(XPixmap_isNull(&target),
                "pixmap from image clears the output for a null image");

    XImage_deinit_base(&image);
    XPixmap_deinit_base(&target);
    XPixmap_deinit_base(&empty);
    XPixmap_deinit_base(&transformed);
    XPixmap_deinit_base(&copied);
    XPixmap_deinit_base(&source);
}

static void test_icon_sizes(void)
{
    XPixmap first;
    XPixmap second;
    XIcon icon;
    XVector sizes;
    XSize* size0;
    XSize* size1;

    XPixmap_init_ex(&first, 16, 16);
    XPixmap_init_ex(&second, 32, 24);
    XIcon_init_pixmap(&icon, &first);
    XIcon_addPixmap(&icon, &second, XIconMode_Normal, XIconState_Off);
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);

    expect_true(XVector_size_base((const XContainer*)&sizes) == 2, "icon availableSizes returns unique entries");
    size0 = (XSize*)XVector_at_base(&sizes, 0);
    size1 = (XSize*)XVector_at_base(&sizes, 1);
    expect_true(size0 && size1 &&
                ((size0->width == 16 && size1->width == 32) ||
                 (size0->width == 32 && size1->width == 16)),
                "icon availableSizes preserves dimensions");

    XVector_deinit_base((XClass*)&sizes);
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&second);
    XPixmap_deinit_base(&first);
}

static void test_geometry_contract(void)
{
    XPoint point;
    XPoint otherPoint;
    XPoint difference;
    XSize size;
    XSize bounded;
    XSize expanded;
    XSize transposed;
    XRect first;
    XRect second;
    XRect result;
    XRect adjusted;
    XRect translated;
    XRect normalized;
    XSize rectSize;
    XPoint topLeft;
    XPoint center;
    XRegion region;
    XRegion otherRegion;
    XRegion chained;

    XPoint_init(&point, 3, -4);
    XPoint_init(&otherPoint, -1, 2);
    expect_true(XPoint_manhattanLength(&point) == 7,
                "point manhattan length matches Qt semantics");
    point = XPoint_add(&point, &otherPoint);
    expect_true(point.x == 2 && point.y == -2,
                "point add returns a value type");
    difference = XPoint_subtract(&point, &otherPoint);
    expect_true(difference.x == 3 && difference.y == -4,
                "point subtract returns a value type");

    XSize_init(&size, 640, 480);
    XSize_scale(&size, 100, 100, 1);
    expect_true(size.width == 100 && size.height == 75,
                "size scale keeps aspect ratio");
    bounded = XSize_boundedTo(&size, &(XSize){80, 90});
    expanded = XSize_expandedTo(&size, &(XSize){80, 90});
    transposed = XSize_transposed(&size);
    expect_true(bounded.width == 80 && bounded.height == 75 &&
                expanded.width == 100 && expanded.height == 90 &&
                transposed.width == 75 && transposed.height == 100,
                "size value APIs return lightweight structs");

    XRect_init(&first, 10, 10, 20, 20);
    XRect_init(&second, 20, 0, 20, 20);
    result = XRect_intersected(&first, &second);
    expect_true(result.x == 20 && result.y == 10 &&
                result.width == 10 && result.height == 10,
                "rect intersection uses half-open bounds");
    result = XRect_united(&first, &second);
    expect_true(result.x == 10 && result.y == 0 &&
                result.width == 30 && result.height == 30,
                "rect union returns bounding rectangle");
    normalized = XRect_normalized(&(XRect){30, 20, -10, -5});
    rectSize = XRect_size(&first);
    topLeft = XRect_topLeft(&first);
    center = XRect_center(&first);
    adjusted = XRect_adjusted(&first, 1, 2, -3, -4);
    translated = XRect_translated(&first, 5, -5);
    expect_true(normalized.x == 20 && normalized.y == 15 &&
                normalized.width == 10 && normalized.height == 5 &&
                rectSize.width == 20 && rectSize.height == 20 &&
                topLeft.x == 10 && topLeft.y == 10 &&
                center.x == 19 && center.y == 19 &&
                adjusted.x == 11 && adjusted.y == 12 &&
                adjusted.width == 16 && adjusted.height == 14 &&
                translated.x == 15 && translated.y == 5,
                "rect value APIs return lightweight structs");

    XRegion_init(&region);
    XRegion_init(&otherRegion);
    XRegion_init(&chained);
    XRegion_addRect(&chained, &(XRect){0, 0, 1, 1});
    XRegion_addRect(&chained, &(XRect){0, 1, 1, 1});
    XRegion_addRect(&chained, &(XRect){0, 2, 1, 1});
    expect_true(chained.count == 1 && chained.rects[0].height == 3,
                "region rectangle merging is transitive");
    XRegion_addRect(&region, &first);
    XRegion_addRect(&region, &(XRect){0, 0, 0, 10});
    XRegion_addRect(&otherRegion, &(XRect){15, 15, 10, 10});
    expect_true(XRegion_contains(&region, 12, 12) &&
                !XRegion_contains(&region, 0, 0),
                "region ignores empty rectangles and tests membership");
    XRegion_intersected(&region, &otherRegion, &region);
    expect_true(XRegion_isEmpty(&region) == false && region.count == 1 &&
                region.rects[0].x == 15 && region.rects[0].y == 15,
                "region intersection supports aliased output");
    XRegion_subtracted(&region, &otherRegion, &region);
    expect_true(XRegion_isEmpty(&region),
                "region subtraction supports aliased output");
    XRegion_deinit(&otherRegion);
    XRegion_deinit(&region);
    XRegion_deinit(&chained);
}

static void test_pixmap_scroll_and_bitmap_alias(void)
{
    XImage image;
    XImage movedImage;
    XPixmap pixmap;
    XPixmap rgbPixmap;
    XRegion exposed;
    XBitmap bitmap;
    XBitmap mask;
    XBitmap transformed;

    XImage_init_ex(&image, 4, 2, XImageFormat_ARGB32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            XImage_setPixel(&image, x, y, 0xff000000u | (uint32_t)(x + 1));
    XPixmap_init(&pixmap);
    XPixmap_init_image(&pixmap, &image, 0);
    XImage_deinit_base(&image);
    XRegion_init(&exposed);
    XRegion_addRect(&exposed, &(XRect){99, 99, 1, 1});
    XPixmap_scroll(&pixmap, 1, 0, NULL, &exposed);
    expect_true(exposed.count == 1 && exposed.rects[0].x == 0 &&
                exposed.rects[0].width == 1,
                "pixmap scroll replaces exposed region");
    XImage_init(&movedImage);
    XPixmap_toImage(&pixmap, &movedImage);
    expect_true(XImage_pixel(&movedImage, 1, 0) == 0xff000001u &&
                XImage_pixel(&movedImage, 3, 0) == 0xff000003u,
                "pixmap scroll moves pixels from the pre-scroll image");
    XImage_deinit_base(&movedImage);
    XPixmap_scroll(&pixmap, 0, 0, NULL, &exposed);
    expect_true(XRegion_isEmpty(&exposed),
                "pixmap scroll clears exposed region for a no-op");

    XImage_init_ex(&image, 2, 1, XImageFormat_RGB888);
    XImage_setPixel(&image, 0, 0, 0xffff0000u);
    XImage_setPixel(&image, 1, 0, 0xff00ff00u);
    XPixmap_init(&rgbPixmap);
    XPixmap_init_image(&rgbPixmap, &image, 0);
    XBitmap_init_ex(&mask, 2, 1);
    XImage_init(&movedImage);
    XPixmap_toImage((const XPixmap*)&mask, &movedImage);
    XImage_setPixel(&movedImage, 1, 0, 1);
    XBitmap_fromImage(&movedImage, 0, &mask);
    XPixmap_setMask(&rgbPixmap, (const XPixmap*)&mask);
    XImage_deinit_base(&movedImage);
    XPixmap_toImage(&rgbPixmap, &movedImage);
    expect_true(XPixmap_hasAlphaChannel(&rgbPixmap) &&
                (XImage_pixel(&movedImage, 0, 0) >> 24) == 0 &&
                (XImage_pixel(&movedImage, 1, 0) >> 24) == 0xff,
                "pixmap setMask converts RGB sources and applies mask alpha");
    XImage_deinit_base(&movedImage);
    XBitmap_deinit_base(&mask);
    XPixmap_deinit_base(&rgbPixmap);

    XPixmap swapped;
    XPixmap_init_ex(&swapped, 1, 1);
    XPixmap_swap(&pixmap, &swapped);
    expect_true(XPixmap_width(&pixmap) == 1 && XPixmap_width(&swapped) == 4,
                "pixmap swap exchanges data without changing object identity");
    XPixmap_swap(&pixmap, &swapped);
    expect_true(XPixmap_convertFromImage(&pixmap, &image, 0) &&
                XPixmap_width(&pixmap) == 2 && XPixmap_height(&pixmap) == 1,
                "pixmap convertFromImage replaces data only after success");
    XPixmap_deinit_base(&swapped);
    XImage_deinit_base(&image);

    XBitmap_init_ex(&bitmap, 2, 2);
    XBitmap_init(&transformed);
    XBitmap_transformed_2(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &transformed);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&transformed) &&
                XClassGetVtable((const XClass*)&transformed) ==
                    XClassGetVtable((const XClass*)&bitmap),
                "bitmap transformed output keeps bitmap identity");
    XBitmap_transformed_2(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &bitmap);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&bitmap) &&
                XPixmap_width((const XPixmap*)&bitmap) == 2,
                "bitmap transformed supports aliased output");

    XBitmap_deinit_base(&transformed);
    XBitmap_deinit_base(&bitmap);
    XRegion_deinit(&exposed);
    XPixmap_deinit_base(&pixmap);
}

static void test_pixmap_mask_lifecycle(void)
{
    XPixmap source;
    XPixmap target;
    XPixmap wrongMask;
    XBitmap mask;
    XImage image;
    XImage result;

    XPixmap_init_ex(&source, 3, 2);
    XPixmap_fill(&source, 0xff336699u);
    XPixmap_init(&target);
    XPixmap_copy_base(&target, &source);
    expect_true(!XPixmap_isDetached(&source),
                "pixmap copy shares storage before mask mutation");

    XImage_init_ex(&image, 3, 2, XImageFormat_MonoLSB);
    XImage_fill(&image, 0);
    XImage_setPixel(&image, 1, 0, 1);
    XImage_setPixel(&image, 1, 1, 1);
    XBitmap_init(&mask);
    XBitmap_fromImage(&image, 0, &mask);
    XImage_deinit_base(&image);

    XPixmap_setMask(&source, (const XPixmap*)&mask);
    XImage_init(&result);
    XPixmap_toImage(&source, &result);
    expect_true(XPixmap_isDetached(&source) &&
                (XImage_pixel(&result, 0, 0) >> 24) == 0 &&
                (XImage_pixel(&result, 1, 0) >> 24) == 0xff &&
                (XImage_pixel(&result, 0, 1) >> 24) == 0 &&
                (XImage_pixel(&result, 1, 1) >> 24) == 0xff,
                "pixmap setMask applies mono mask and detaches shared storage");

    XPixmap_toImage(&target, &result);
    expect_true((XImage_pixel(&result, 0, 0) >> 24) == 0xff &&
                (XImage_pixel(&result, 1, 0) >> 24) == 0xff,
                "pixmap target copy is unchanged by source setMask");

    XBitmap_clear(&mask);
    XPixmap_toImage(&source, &result);
    expect_true((XImage_pixel(&result, 0, 0) >> 24) == 0 &&
                (XImage_pixel(&result, 1, 0) >> 24) == 0xff,
                "modifying source mask after setMask does not change target");

    XPixmap_setMask(&source, NULL);
    XPixmap_toImage(&source, &result);
    expect_true(XImage_pixel(&result, 0, 0) == 0xff000000u &&
                XImage_pixel(&result, 1, 0) == 0xff336699u,
                "pixmap null mask clears previous transparent pixels");

    XPixmap_init_ex(&wrongMask, 2, 2);
    XPixmap_setMask(&source, (const XPixmap*)&wrongMask);
    XPixmap_toImage(&source, &result);
    expect_true(XImage_pixel(&result, 0, 0) == 0xff000000u &&
                XImage_pixel(&result, 1, 0) == 0xff336699u,
                "pixmap setMask with mismatched size is a no-op");

    XPixmap_setMask(&source, (const XPixmap*)&source);
    XPixmap_toImage(&source, &result);
    expect_true(XImage_pixel(&result, 0, 0) == 0xff000000u &&
                XImage_pixel(&result, 1, 0) == 0xff336699u,
                "pixmap self mask is a no-op");

    XImage_deinit_base(&result);
    XPixmap_deinit_base(&wrongMask);
    XBitmap_deinit_base(&mask);
    XPixmap_deinit_base(&target);
    XPixmap_deinit_base(&source);
}

static void test_pixmap_cache_contract(void)
{
    int oldLimit = XPixmapCache_cacheLimit();
    XPixmap p10, p20, p32, out;
    XPixmapCacheKey key, copy, third;
    XRect none;

    (void)none;
    XPixmap_init_ex(&p10, 10, 10);
    XPixmap_init_ex(&p20, 20, 20);
    XPixmap_init_ex(&p32, 32, 32);
    XPixmap_init(&out);
    XPixmapCacheKey_init(&key);
    XPixmapCacheKey_init(&copy);
    XPixmapCacheKey_init(&third);

    /* Key 生命周期：insertKey 后有效，副本共享同一有效性 */
    XPixmapCache_setCacheLimit(64);
    expect_true(XPixmapCache_insertKey(&p10, &key) &&
                XPixmapCacheKey_isValid(&key),
                "cache insertKey makes the key valid");
    XPixmapCacheKey_copy(&copy, &key);
    XPixmapCacheKey_copy(&third, &copy);
    expect_true(XPixmapCache_findKey(&copy, &out) &&
                XPixmap_width(&out) == 10,
                "cache findKey via a key copy hits the same entry");

    /* replace：沿用原 Key，键与所有副本保持有效 */
    expect_true(XPixmapCache_replace(&key, &p20),
                "cache replace updates the pixmap");
    expect_true(XPixmapCacheKey_isValid(&key) &&
                XPixmapCacheKey_isValid(&copy) &&
                XPixmapCacheKey_isValid(&third),
                "cache replace keeps the key and all copies valid");
    expect_true(XPixmapCache_findKey(&key, &out) &&
                XPixmap_width(&out) == 20 &&
                XPixmap_height(&out) == 20,
                "cache replace stores the new pixmap");

    /* findKey 未命中：键立即失效（Qt 语义） */
    XPixmapCache_clear();
    expect_true(!XPixmapCache_findKey(&copy, &out) &&
                !XPixmapCacheKey_isValid(&key) &&
                !XPixmapCacheKey_isValid(&third),
                "cache findKey miss invalidates the key and its copies");

    /* 同一 Key 对象重复 insertKey：不产生孤儿节点，语义为覆盖 */
    expect_true(XPixmapCache_insertKey(&p10, &key),
                "cache re-uses an invalid key object after clear");
    expect_true(XPixmapCache_insertKey(&p20, &key),
                "cache re-insert with the same key object overwrites");
    expect_true(XPixmapCache_findKey(&key, &out) &&
                XPixmap_width(&out) == 20,
                "cache second insertKey replaces the cached pixmap");
    expect_true(XPixmapCacheKey_hash(&key) != 0 &&
                !XPixmapCacheKey_equals(&key, &copy),
                "cache key hash is non-zero and new keys differ");

    /* removeKey：调用后键失效（Qt 语义） */
    XPixmapCacheKey_copy(&copy, &key);
    XPixmapCache_removeKey(&copy);
    expect_true(!XPixmapCacheKey_isValid(&key) &&
                !XPixmapCacheKey_isValid(&copy),
                "cache removeKey invalidates every key copy");

    /* 字符串键：覆盖、大小限制、禁用缓存 */
    expect_true(XPixmapCache_insert_2("duplicate", &p10), "cache string insert");
    expect_true(XPixmapCache_insert_2("duplicate", &p20),
                "cache string insert overwrites the old value");
    expect_true(XPixmapCache_find_2("duplicate", &out) &&
                XPixmap_width(&out) == 20,
                "cache string overwrite stores the newest pixmap");
    XPixmapCache_clear();
    /* 32x32 depth32 估计开销为 4 KB：a、b 共 8 KB，恰好命中 8 KB 限制 */
    XPixmapCache_setCacheLimit(8);
    expect_true(XPixmapCache_insert_2("a", &p32), "cache 32x32 fits the 8 KB limit");
    expect_true(XPixmapCache_insert_2("b", &p32) &&
                XPixmapCache_find_2("a", NULL),
                "cache LRU keeps the recently used head entry");
    expect_true(XPixmapCache_insert_2("c", &p32) &&
                !XPixmapCache_find_2("b", NULL) &&
                XPixmapCache_find_2("c", NULL) &&
                XPixmapCache_find_2("a", NULL),
                "cache LRU evicts the least recently used tail");
    XPixmapCache_setCacheLimit(0);
    expect_true(!XPixmapCache_find_2("a", NULL) &&
                !XPixmapCache_insert_2("rejected", &p32),
                "cache limit 0 disables insertion and purges entries");

    /* 回归：全新 Key 的 insertKey 不得误删字符串键条目（m_data 为 NULL 时
     * 字符串条目 m_keyData 同样为 NULL，旧实现会误删第一条字符串条目） */
    XPixmapCache_setCacheLimit(64);
    expect_true(XPixmapCache_insert_2("keepme", &p10) &&
                XPixmapCache_insertKey(&p20, &copy) &&
                XPixmapCache_find_2("keepme", NULL),
                "cache insertKey with a fresh key keeps string entries");
    XPixmapCache_clear();

    /* 超限像素图直接拒绝 */
    XPixmapCache_setCacheLimit(1);
    expect_true(!XPixmapCache_insertKey(&p32, &copy),
                "cache rejects a pixmap larger than the limit");

    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(oldLimit);
    XPixmapCacheKey_deinit(&third);
    XPixmapCacheKey_deinit(&copy);
    XPixmapCacheKey_deinit(&key);
    XPixmap_deinit_base(&out);
    XPixmap_deinit_base(&p32);
    XPixmap_deinit_base(&p20);
    XPixmap_deinit_base(&p10);
}

#if defined(__unix__)
#include <pthread.h>
#include <math.h>
#include <stdio.h>

typedef struct CacheStressCtx
{
    unsigned int m_id;
    unsigned int m_iterations;
    unsigned int m_findMisses;      /**< 插入后立即查找失败的次数（应为 0） */
    unsigned int m_keyLifeErrors;   /**< removeKey 后键仍有效的次数（应为 0） */
} CacheStressCtx;

static void* cache_stress_worker(void* arg)
{
    CacheStressCtx* ctx = (CacheStressCtx*)arg;
    unsigned int i;
    for (i = 0; i < ctx->m_iterations; ++i)
    {
        char key[64];
        XPixmap p;
        XPixmapCacheKey k;
        XPixmap_init_ex(&p, 4, 4);
        XPixmapCacheKey_init(&k);
        snprintf(key, sizeof(key), "stress_t%u_iter%u", ctx->m_id, i % 32u);
        XPixmapCache_insert_2(key, &p);
        XPixmapCache_insert_2(key, &p);   /* 并发覆盖路径 */
        if (!XPixmapCache_find_2(key, NULL)) ctx->m_findMisses++;
        XPixmapCache_remove_2(key);
        if (XPixmapCache_insertKey(&p, &k))
        {
            XPixmap q;
            XPixmap_init(&q);
            if (!XPixmapCache_findKey(&k, &q)) ctx->m_findMisses++;
            XPixmap_deinit_base(&q);
            XPixmapCache_replace(&k, &p);  /* 并发原位替换路径 */
            XPixmapCache_removeKey(&k);
            if (XPixmapCacheKey_isValid(&k)) ctx->m_keyLifeErrors++;
        }
        XPixmapCacheKey_deinit(&k);
        XPixmap_deinit_base(&p);
    }
    return NULL;
}

static void test_pixmap_cache_concurrency(void)
{
    enum { kThreads = 4, kIterations = 600, kLimit = 4096 };
    int oldLimit = XPixmapCache_cacheLimit();
    pthread_t threads[kThreads];
    CacheStressCtx ctxs[kThreads];
    unsigned int i;
    unsigned int totalMisses = 0;
    unsigned int totalKeyErrors = 0;
    XPixmapCache_setCacheLimit(kLimit);
    for (i = 0; i < kThreads; ++i)
    {
        ctxs[i].m_id = i;
        ctxs[i].m_iterations = kIterations;
        ctxs[i].m_findMisses = 0;
        ctxs[i].m_keyLifeErrors = 0;
        expect_true(pthread_create(&threads[i], NULL, cache_stress_worker,
                                   &ctxs[i]) == 0,
                    "cache stress thread creation");
    }
    for (i = 0; i < kThreads; ++i)
    {
        pthread_join(threads[i], NULL);
        totalMisses += ctxs[i].m_findMisses;
        totalKeyErrors += ctxs[i].m_keyLifeErrors;
    }
    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(oldLimit);
    expect_true(totalMisses == 0,
                "cache concurrent stress: insert followed by find never misses");
    expect_true(totalKeyErrors == 0,
                "cache concurrent stress: removeKey always invalidates the key");
}
#endif /* __unix__ */

static void test_picture_play_contract(void)
{
    XPicture picture;
    XPainter painter;
    XRect rect = { 2, 3, 4, 5 };
    PictureProbe probe = { 0, 0, 0, 0, 0, false };
    XImage image;

    XPicture_init(&picture, -1);
    /* Qt returns true for an empty recording without touching the painter. */
    expect_true(XPicture_play(&picture, NULL),
                "empty picture play is a successful no-op");
    XPicture_setData(&picture, "commands", 8);
    expect_true(!XPicture_play(&picture, NULL),
                "non-empty picture play stays unsupported without painter dispatch");
    XPicture_clearCommands(&picture);
    XPainter_init(&painter, &probe);
    painter.m_drawLine = picture_probe_line;
    XPicture_recordDrawLine(&picture, 1, 2, 3, 4);
    painter.m_fillRect = picture_probe_fill;
    XPicture_recordFillRect(&picture, &rect, 0xff112233u);
    painter.m_save = picture_probe_state;
    XPicture_recordSave(&picture);
    painter.m_restore = picture_probe_restore;
    XPicture_recordRestore(&picture);
    painter.m_drawImage = picture_probe_image;
    XImage_init_ex(&image, 2, 1, XImageFormat_ARGB32);
    XImage_setPixel(&image, 0, 0, 0xff102030u);
    XImage_setDevicePixelRatio(&image, 2.0f);
    expect_true(XPicture_recordDrawImage(&picture, &image, 8, 9),
                "recordDrawImage stores a self-contained image payload");
    expect_true(XPicture_isValidStream(&picture),
                "recorded picture has a valid portable stream");
    expect_true(XPicture_play(&picture, &painter),
                "recorded picture dispatches through XPainter");
    expect_true(probe.lineCalls == 1 && probe.fillCalls == 1 &&
                probe.saveCalls == 1 && probe.restoreCalls == 1,
                "picture dispatch calls each command exactly once");
    expect_true(probe.imageCalls == 1 && probe.imagePayloadOk,
                "picture replay reconstructs image metadata and pixels");
#if XPAINTER_SHAPE_ON
    {
        XRect shapeRect = { 10, 20, 5, 7 };
        painter.m_drawShape = picture_probe_shape;
        expect_true(XPicture_recordDrawShape(&picture,
                                             (int)XPainterShapeOp_RoundedRect,
                                             &shapeRect, 0, 0, true, 2, 3),
                    "recordDrawShape stores a portable shape command");
    }
#endif /* XPAINTER_SHAPE_ON */
#if XPAINTER_POLYGON_ON
    {
        XPoint polylinePoints[2] = { { 1, 2 }, { 3, 4 } };
        XPoint polygonPoints[3] = { { 0, 0 }, { 4, 0 }, { 0, 3 } };
        XPoint pointSet[2] = { { 7, 8 }, { 9, 10 } };
        painter.m_drawPolyline = picture_probe_polyline;
        painter.m_drawPolygon = picture_probe_polygon;
        painter.m_drawPoints = picture_probe_points;
        expect_true(XPicture_recordDrawPolyline(&picture, polylinePoints, 2),
                    "recordDrawPolyline stores a portable polyline command");
        expect_true(XPicture_recordDrawPolygon(&picture, polygonPoints, 3,
                                               true,
                                               (int)XPainterFillRule_Winding),
                    "recordDrawPolygon stores a portable polygon command");
        expect_true(XPicture_recordDrawPoints(&picture, pointSet, 2),
                    "recordDrawPoints stores a portable point-set command");
    }
#endif /* XPAINTER_POLYGON_ON */
#if XPAINTER_PATH_ON
    {
        XPainterPath probePath;
        XPainterPath_init(&probePath);
        expect_true(XPainterPath_moveTo(&probePath, 1.0f, 1.0f) &&
                    XPainterPath_lineTo(&probePath, 6.0f, 1.0f) &&
                    XPainterPath_lineTo(&probePath, 6.0f, 6.0f) &&
                    XPainterPath_lineTo(&probePath, 1.0f, 6.0f) &&
                    XPainterPath_closeSubpath(&probePath),
                    "picture probe path construction");
        painter.m_drawPath = picture_probe_path;
        expect_true(XPicture_recordDrawPath(&picture,
                                            (int)XPainterPathOp_Draw,
                                            &probePath),
                    "recordDrawPath stores a portable path command");
        XPainterPath_deinit(&probePath);
    }
#endif /* XPAINTER_PATH_ON */
    expect_true(XPicture_isValidStream(&picture),
                "recorded high-level picture commands remain a valid stream");
    expect_true(XPicture_play(&picture, &painter),
                "recorded high-level picture dispatches through XPainter");
#if XPAINTER_SHAPE_ON
    expect_true(probe.shapeCalls == 1 && probe.shapeOpOk,
                "picture replay dispatches shape payload once");
#endif /* XPAINTER_SHAPE_ON */
#if XPAINTER_POLYGON_ON
    expect_true(probe.polylineCalls == 1 && probe.polygonCalls == 1 &&
                probe.pointsCalls == 1 && probe.polygonOk && probe.pointsOk,
                "picture replay dispatches polyline/polygon/points payloads");
#endif /* XPAINTER_POLYGON_ON */
#if XPAINTER_PATH_ON
    expect_true(probe.pathCalls == 1 && probe.pathOk &&
                probe.lineCalls == 2,
                "picture replay dispatches path as a single command");
#endif /* XPAINTER_PATH_ON */
    {
        XPicture loaded;
        XPicture_init(&loaded, -1);
        expect_true(XPicture_save_2(&picture, "xgui_regression.xpic"),
                    "picture save writes the portable stream");
        expect_true(XPicture_load_2(&loaded, "xgui_regression.xpic") &&
                    XPicture_isValidStream(&loaded) &&
                    XPicture_play(&loaded, &painter),
                    "picture load validates and replays the portable stream");
        remove("xgui_regression.xpic");
        XPicture_deinit_base(&loaded);
    }
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
    XPicture_deinit_base(&picture);
}

#if XPAINTER_SHAPE_ON && XPAINTER_POLYGON_ON
/** @brief 校验 XPainter 录制后端把高层绘制指令原样写入 XPicture。 */
static void test_picture_painter_high_level_record_link(void)
{
    XPicture picture;
    XPainter record;
    XPainter image;
    XImage target;
    XRect ellipseRect = { 2, 2, 16, 8 };
    XPoint polyline[3] = { { 20, 2 }, { 26, 2 }, { 26, 6 } };
    XPoint polygon[4] = { { 2, 10 }, { 8, 10 }, { 8, 15 }, { 2, 15 } };
    XPoint points[3] = { { 20, 10 }, { 24, 12 }, { 22, 14 } };

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "high-level record begins picture");
    XPainter_setPen(&record, 0xffff0000u);
    XPainter_setPenWidth(&record, 1);
    XPainter_setBrush(&record, 0xff0000ffu);
    expect_true(XPainter_drawEllipse(&record, &ellipseRect),
                "recorded drawEllipse uses high-level shape opcode");
    expect_true(XPainter_drawPolyline(&record, polyline, 3),
                "recorded drawPolyline uses high-level polyline opcode");
    expect_true(XPainter_drawPolygon(&record, polygon, 4,
                                     XPainterFillRule_OddEven),
                "recorded drawPolygon uses high-level polygon opcode");
    expect_true(XPainter_drawPoints(&record, points, 3),
                "recorded drawPoints uses high-level points opcode");
    expect_true(XPainter_end(&record),
                "high-level record picture ends");
    expect_true(XPicture_isValidStream(&picture) &&
                XPicture_size(&picture) > XPICTURE_HEADER_SIZE,
                "high-level record produces a valid non-empty stream");

    XImage_init_ex(&target, 32, 18, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&image, NULL);
    expect_true(XPainter_begin_image(&image, &target),
                "high-level replay begins software raster");
    XPainter_setPen(&image, 0xffff0000u);
    XPainter_setPenWidth(&image, 1);
    XPainter_setBrush(&image, 0xff0000ffu);
    expect_true(XPicture_play(&picture, &image),
                "high-level picture replays through software raster");
    expect_true(XImage_pixel(&target, 10, 6) == 0xff0000ffu,
                "replayed ellipse fills recorded brush color");
    expect_true(XImage_pixel(&target, 10, 2) == 0xffff0000u &&
                XImage_pixel(&target, 18, 6) == 0xffff0000u,
                "replayed ellipse outlines with recorded pen color");
    expect_true(XImage_pixel(&target, 20, 2) == 0xffff0000u &&
                XImage_pixel(&target, 26, 6) == 0xffff0000u,
                "replayed polyline paints both segments");
    expect_true(XImage_pixel(&target, 4, 12) == 0xff0000ffu &&
                XImage_pixel(&target, 8, 12) == 0xffff0000u &&
                XImage_pixel(&target, 9, 12) == 0xff000000u,
                "replayed polygon fills, outlines, and stays outside");
    expect_true(XImage_pixel(&target, 20, 10) == 0xffff0000u &&
                XImage_pixel(&target, 24, 12) == 0xffff0000u &&
                XImage_pixel(&target, 22, 14) == 0xffff0000u &&
                XImage_pixel(&target, 21, 11) == 0xff000000u,
                "replayed points plot each recorded point only");

    expect_true(XPainter_end(&image), "high-level replay picture ends");
    XPainter_deinit(&image);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_SHAPE_ON && XPAINTER_POLYGON_ON */

#if XPAINTER_PATH_ON
/** @brief 校验 XPainter 的 Picture 后端把路径命令作为独立 opcode 录制并回放。 */
static void test_picture_painter_path_record_link(void)
{
    XPicture picture;
    XPainter record;
    XPainter image;
    XImage target;
    XPainterPath path;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "path record begins picture");
    XPainter_setPen(&record, 0xffff0000u);
    XPainter_setPenWidth(&record, 1);
    XPainter_setBrush(&record, 0xff0000ffu);
    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 2.0f) &&
                XPainterPath_lineTo(&path, 10.0f, 2.0f) &&
                XPainterPath_lineTo(&path, 10.0f, 10.0f) &&
                XPainterPath_lineTo(&path, 2.0f, 10.0f) &&
                XPainterPath_closeSubpath(&path),
                "path record test path construction");
    expect_true(XPainter_drawPath(&record, &path),
                "drawPath records a portable path draw opcode");
    expect_true(XPainter_fillPath(&record, &path),
                "fillPath records a portable path fill opcode");
    expect_true(XPainter_strokePath(&record, &path),
                "strokePath records a portable path stroke opcode");
    expect_true(XPainter_end(&record),
                "path record picture ends");
    expect_true(XPicture_isValidStream(&picture) &&
                XPicture_size(&picture) > XPICTURE_HEADER_SIZE,
                "path record produces a valid non-empty stream");

    XImage_init_ex(&target, 32, 18, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&image, NULL);
    expect_true(XPainter_begin_image(&image, &target),
                "path replay begins software raster");
    XPainter_setPen(&image, 0xffff0000u);
    XPainter_setPenWidth(&image, 1);
    XPainter_setBrush(&image, 0xff0000ffu);
    expect_true(XPicture_play(&picture, &image),
                "path picture replays through software raster");
    expect_true(XImage_pixel(&target, 3, 3) == 0xff0000ffu,
                "replayed drawPath+fillPath keeps interior brush color");
    expect_true(XImage_pixel(&target, 2, 2) == 0xffff0000u &&
                XImage_pixel(&target, 10, 5) == 0xffff0000u,
                "replayed strokePath keeps outline pen color");
    expect_true(XImage_pixel(&target, 12, 12) == 0xff000000u,
                "replayed path stays outside path bounds");

    expect_true(XPainter_end(&image), "path replay picture ends");
    XPainter_deinit(&image);
    XPainter_deinit(&record);
    XPainterPath_deinit(&path);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_PATH_ON */

static void test_painter_raster_contract(void)
{
    XImage image;
    XPainter painter;
    XRect rect = { 1, 1, 4, 3 };
    XRect outline = { 1, 1, 5, 4 };
#if XPAINTER_CLIP_ON
    XRect clip = { 2, 2, 3, 3 };
#endif
    XRect one = { 0, 0, 1, 1 };
    XRect empty = { 0, 0, 0, 0 };
    XImage tile;
#if XPAINTER_PIXMAP_ON
    XPixmap pixmap;
    XRect pixmapTarget = { 0, 0, 4, 2 };
    XRect pixmapSource = { 0, 0, 2, 2 };
#if XPAINTER_TILED_PIXMAP_ON
    XRect tiledTarget = { 0, 0, 5, 3 };
    XPoint tiledOffset = { 1, 1 };
#endif /* XPAINTER_TILED_PIXMAP_ON */
#endif /* XPAINTER_PIXMAP_ON */
#if XPAINTER_IMAGE_RECT_ON
    XImage strip;
    XRect imageTarget = { 0, 0, 4, 2 };
    XRect imageSource = { 0, 0, 2, 1 };
#endif /* XPAINTER_IMAGE_RECT_ON */

    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
#if XPAINTER_BACKGROUND_ON
    expect_true(XPainter_background(&painter) == 0xff000000u,
                "inactive background color follows Qt fake brush");
    expect_true(XPainter_backgroundMode(&painter) ==
                    XPainterBackgroundMode_Transparent,
                "inactive background mode is Transparent");
#if XPAINTER_BRUSH_ON
    {
        XPainterBrush inactiveBackground;
        XPainter_backgroundBrush(&painter, &inactiveBackground);
        expect_true(inactiveBackground.m_style == XPainterBrushStyle_NoBrush &&
                        inactiveBackground.m_color == 0xff000000u,
                    "inactive background brush is Qt default");
    }
#endif /* XPAINTER_BRUSH_ON */
#endif /* XPAINTER_BACKGROUND_ON */
#if XPAINTER_LAYOUT_DIRECTION_ON
    expect_true(XPainter_layoutDirection(&painter) ==
                    XPainterLayoutDirection_Auto,
                "inactive layout direction is Auto");
    XPainter_setLayoutDirection(&painter,
                                XPainterLayoutDirection_RightToLeft);
    expect_true(XPainter_layoutDirection(&painter) ==
                    XPainterLayoutDirection_Auto,
                "inactive layout direction setter is ignored like Qt");
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
    {
        XRect inactiveFill = { 0, 0, 1, 1 };
        expect_true(!XPainter_fillRect_2(&painter, &inactiveFill),
                    "fillRect_2 rejects an inactive painter");
    }
    XPainter_setPen(&painter, 0xffff00ffu);
    XPainter_setOpacity(&painter, 0.25f);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    expect_true(XPainter_begin_image(&painter, &image),
                "painter begins raster on image");
#if XPAINTER_LAYOUT_DIRECTION_ON
    expect_true(XPainter_layoutDirection(&painter) ==
                    XPainterLayoutDirection_LeftToRight,
                "layout direction begins with application default LTR");
    XPainter_setLayoutDirection(&painter, XPainterLayoutDirection_RightToLeft);
    expect_true(XPainter_layoutDirection(&painter) ==
                    XPainterLayoutDirection_RightToLeft,
                "active layout direction setter stores RTL");
    XPainter_setLayoutDirection(&painter, XPainterLayoutDirection_Auto);
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */
#if XPAINTER_BRUSH_ON
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_NoBrush,
                "raster default brush matches QBrush NoBrush");
#endif /* XPAINTER_BRUSH_ON */
    expect_true(XPainter_penColor(&painter) == 0xff000000u &&
                fabsf(XPainter_opacity(&painter) - 1.0f) < 1.0e-4f &&
                XPainter_compositionMode(&painter) ==
                    XPainterCompositionMode_SourceOver,
                "inactive painter setters do not change initial state");
    expect_true(!XPainter_begin_image(&painter, &image),
                "begin on active painter is rejected");
    expect_true(XPainter_device(&painter) == &image,
                "painter device reports the bound image");

    /* QRect/QPainter 允许单轴退化矩形：零宽或零高仍按一条线描边，
       负尺寸则按两条几何边规范化，而不是将调用当作空操作。 */
    {
        XRect vertical = { 2, 2, 0, 2 };
        XRect negative = { 5, 5, -2, -2 };
        XRect negativeFill = { 5, 5, -2, 2 };
        XImage_fillRect(&image, NULL, 0u);
        XPainter_setPen(&painter, 0xffff0000u);
        expect_true(XPainter_drawRect(&painter, &vertical),
                    "raster drawRect zero-width line");
        expect_true(XImage_pixel(&image, 2, 2) == 0xffff0000u &&
                    XImage_pixel(&image, 2, 3) == 0xffff0000u &&
                    XImage_pixel(&image, 2, 4) == 0xffff0000u,
                    "raster drawRect zero-width keeps height");
        XImage_fillRect(&image, NULL, 0u);
        expect_true(XPainter_drawRect(&painter, &negative),
                    "raster drawRect negative dimensions");
        expect_true(XImage_pixel(&image, 3, 3) == 0xffff0000u &&
                    XImage_pixel(&image, 5, 3) == 0xffff0000u &&
                    XImage_pixel(&image, 3, 5) == 0xffff0000u &&
                    XImage_pixel(&image, 5, 5) == 0xffff0000u &&
                    XImage_pixel(&image, 4, 4) == 0u,
                    "raster drawRect normalizes negative dimensions");
        XImage_fillRect(&image, NULL, 0u);
        expect_true(XPainter_fillRect(&painter, &negativeFill, 0xff00ff00u),
                    "raster fillRect negative width");
        expect_true(XImage_pixel(&image, 3, 5) == 0xff00ff00u &&
                    XImage_pixel(&image, 4, 6) == 0xff00ff00u &&
                    XImage_pixel(&image, 5, 5) == 0u,
                    "raster fillRect normalizes negative width");
    }

    /* 不透明填充走 XImage_fillRect 快速路径 */
    expect_true(XPainter_fillRect(&painter, &rect, 0xff336699u),
                "raster fillRect");
    expect_true(XImage_pixel(&image, 1, 1) == 0xff336699u,
                "raster fill top-left");
    expect_true(XImage_pixel(&image, 4, 3) == 0xff336699u,
                "raster fill bottom-right");
    expect_true(XImage_pixel(&image, 5, 1) == 0, "raster fill right margin");
    expect_true(XImage_pixel(&image, 0, 0) == 0, "raster fill left margin");

    /* 矩形边框（不透明画笔，角点保持精确） */
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawRect(&painter, &outline), "raster drawRect");
    expect_true(XImage_pixel(&image, 1, 1) == 0xffff0000u,
                "raster rect corner tl");
    expect_true(XImage_pixel(&image, 6, 1) == 0xffff0000u,
                "raster rect corner tr");
    expect_true(XImage_pixel(&image, 6, 5) == 0xffff0000u,
                "raster rect corner br");
    expect_true(XImage_pixel(&image, 3, 2) == 0xff336699u,
                "raster rect interior kept");
    expect_true(XImage_pixel(&image, 0, 5) == 0, "raster rect outside");

    /* 画线及笔宽 */
    XPainter_setPen(&painter, 0xff00ff00u);
    expect_true(XPainter_drawLine(&painter, 2, 6, 6, 6), "raster drawLine");
    expect_true(XImage_pixel(&image, 2, 6) == 0xff00ff00u,
                "raster line start");
    expect_true(XImage_pixel(&image, 6, 6) == 0xff00ff00u, "raster line end");
    expect_true(XImage_pixel(&image, 4, 6) == 0xff00ff00u,
                "raster line middle");
    expect_true(XImage_pixel(&image, 0, 5) == 0, "raster line above");
    XPainter_setPen(&painter, 0xffffff00u);
    XPainter_setPenWidth(&painter, 3);
    expect_true(XPainter_drawLine(&painter, 0, 7, 3, 7), "raster thick line");
    expect_true(XImage_pixel(&image, 0, 6) == 0xffffff00u,
                "raster thick line upper");
    expect_true(XImage_pixel(&image, 0, 7) == 0xffffff00u,
                "raster thick line main");
    expect_true(XImage_pixel(&image, 0, 5) == 0, "raster thick line boundary");
    XPainter_setPenWidth(&painter, 1);
#if XPAINTER_PENSTYLE_ON
    /* Qt 光栅器对轴向线的 Flat/Square/Round 端点覆盖不同；宽度 1
       时 FlatCap 右端排他，SquareCap 和 RoundCap 包含终点。 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_FlatCap);
    expect_true(XPainter_drawLine(&painter, 2, 3, 7, 3),
                "raster flat-cap line");
    expect_true(XImage_pixel(&image, 2, 3) == 0xffffff00u &&
                XImage_pixel(&image, 6, 3) == 0xffffff00u &&
                XImage_pixel(&image, 7, 3) == 0u,
                "flat cap excludes geometric endpoint");
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_SquareCap);
    expect_true(XPainter_drawLine(&painter, 2, 3, 7, 3),
                "raster square-cap line");
    expect_true(XImage_pixel(&image, 2, 3) == 0xffffff00u &&
                XImage_pixel(&image, 7, 3) == 0xffffff00u,
                "square cap includes geometric endpoint");
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_RoundCap);
    expect_true(XPainter_drawLine(&painter, 2, 3, 7, 3),
                "raster round-cap line");
    expect_true(XImage_pixel(&image, 2, 3) == 0xffffff00u &&
                XImage_pixel(&image, 7, 3) == 0xffffff00u,
                "round cap includes geometric endpoint");
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_SquareCap);
#endif /* XPAINTER_PENSTYLE_ON */

    /* 后续裁剪断言依赖红色矩形边框，恢复该测试前置状态。 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawRect(&painter, &outline),
                "restore outline before clip contract");

    /* 裁剪矩形使用逻辑坐标并在设置时映射到设备坐标。 */
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
    expect_true(XPainter_hasClipping(&painter), "raster clip active");
    rect.x = 2; rect.y = 2; rect.width = 4; rect.height = 4;
    expect_true(XPainter_fillRect(&painter, &rect, 0xff0000ffu),
                "raster clipped fill");
    expect_true(XImage_pixel(&image, 2, 2) == 0xff0000ffu,
                "raster clip inside");
    expect_true(XImage_pixel(&image, 4, 3) == 0xff0000ffu,
                "raster clip inside 2");
    expect_true(XImage_pixel(&image, 6, 2) == 0xffff0000u,
                "raster clip keeps previous outline");
    expect_true(XImage_pixel(&image, 0, 5) == 0, "raster clip lower margin");
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_NoClip);
    expect_true(!XPainter_hasClipping(&painter), "raster clip cleared");
#endif /* XPAINTER_CLIP_ON */

    /* save/restore 恢复画笔/画刷/变换 */
    XPainter_setPen(&painter, 0xff00ff00u);
    XPainter_setBrush(&painter, 0xff112233u);
    expect_true(XPainter_save(&painter), "raster save");
    XPainter_setPen(&painter, 0xffff00ffu);
    XPainter_setBrush(&painter, 0xffaabbccu);
    XPainter_translate(&painter, 3.0f, 4.0f);
    expect_true(XPainter_restore(&painter), "raster restore");
    expect_true(XPainter_penColor(&painter) == 0xff00ff00u,
                "raster pen restored");
    expect_true(XPainter_brushColor(&painter) == 0xff112233u,
                "raster brush restored");
    {
        XImageTransform t;
        XPainter_transform(&painter, &t);
        expect_true(t.dx == 0.0f && t.dy == 0.0f &&
                    t.m11 == 1.0f && t.m22 == 1.0f,
                    "raster transform restored");
    }

    /* 半透明 SourceOver 合成（整数 Porter-Duff） */
    XImage_setPixel(&image, 0, 0, 0xff204060u);
    XPainter_setOpacity(&painter, 0.5f);
    expect_true(XPainter_fillRect(&painter, &one, 0xff4080ffu),
                "raster translucent fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff3060b0u,
                "raster 50% SourceOver blend exact");

    /* Source 模式整值替换目标 */
    XPainter_setOpacity(&painter, 1.0f);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    expect_true(XPainter_fillRect(&painter, &one, 0x80406080u),
                "raster source mode fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0x80406080u,
                "raster source mode replaces destination");
    XPainter_setCompositionMode(&painter, (XPainterCompositionMode)99);
    expect_true(XPainter_compositionMode(&painter) ==
                XPainterCompositionMode_Source,
                "unsupported composition mode keeps previous mode");
    /* Qt 6.8 的 Porter-Duff 模式在 XImage 后端都可设置并参与像素合成。 */
    {
        int mode;
        for (mode = XPainterCompositionMode_SourceOver;
             mode <= XPainterCompositionMode_Exclusion; ++mode)
        {
            XPainter_setCompositionMode(&painter,
                                        (XPainterCompositionMode)mode);
            expect_true(XPainter_compositionMode(&painter) ==
                        (XPainterCompositionMode)mode,
                        "all Qt composition modes are accepted");
        }
    }
    XImage_setPixel(&image, 0, 0, 0xff102030u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Clear);
    expect_true(XPainter_fillRect(&painter, &one, 0xffffffffu),
                "clear composition fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0,
                "clear composition removes destination");
    XImage_setPixel(&image, 0, 0, 0xff102030u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Destination);
    expect_true(XPainter_fillRect(&painter, &one, 0xffff0000u),
                "destination composition fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff102030u,
                "destination composition preserves destination");
    XImage_setPixel(&image, 0, 0, 0x7f102031u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    expect_true(XPainter_fillRect(&painter, &one, 0x7f405061u),
                "source composition keeps translucent source exactly");
    expect_true(XImage_pixel(&image, 0, 0) == 0x7f405061u,
                "source composition preserves source channels");
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Destination);
    expect_true(XPainter_fillRect(&painter, &one, 0xffffffffu),
                "destination composition keeps translucent destination exactly");
    expect_true(XImage_pixel(&image, 0, 0) == 0x7f405061u,
                "destination composition preserves translucent destination");
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
    XImage_setPixel(&image, 0, 0, 0x7f102031u);
    expect_true(XPainter_fillRect(&painter, &one, 0x00112233u),
                "transparent source over is a no-op");
    expect_true(XImage_pixel(&image, 0, 0) == 0x7f102031u,
                "transparent source preserves destination exactly");
    /* Qt 6.8 的 RasterOp 模式直接按完整 ARGB32 像素逐位计算。 */
    {
        const uint32_t source = 0x5aa55aa5u;
        const uint32_t destination = 0x33cc33ccu;
        const struct RasterOpCase
        {
            XPainterCompositionMode m_mode;
            uint32_t m_expected;
        } cases[] = {
            { XPainterCompositionMode_RasterOp_SourceOrDestination,
              source | destination },
            { XPainterCompositionMode_RasterOp_SourceAndDestination,
              source & destination },
            { XPainterCompositionMode_RasterOp_SourceXorDestination,
              source ^ destination },
            { XPainterCompositionMode_RasterOp_NotSourceAndNotDestination,
              (uint32_t)(~source & ~destination) },
            { XPainterCompositionMode_RasterOp_NotSourceOrNotDestination,
              (uint32_t)(~source | ~destination) },
            { XPainterCompositionMode_RasterOp_NotSourceXorDestination,
              (uint32_t)((~source) ^ destination) },
            { XPainterCompositionMode_RasterOp_NotSource,
              (uint32_t)(~source) },
            { XPainterCompositionMode_RasterOp_NotSourceAndDestination,
              (uint32_t)(~source & destination) },
            { XPainterCompositionMode_RasterOp_SourceAndNotDestination,
              (uint32_t)(source & ~destination) },
            { XPainterCompositionMode_RasterOp_NotSourceOrDestination,
              (uint32_t)(~source | destination) },
            { XPainterCompositionMode_RasterOp_SourceOrNotDestination,
              (uint32_t)(source | ~destination) },
            { XPainterCompositionMode_RasterOp_ClearDestination, 0u },
            { XPainterCompositionMode_RasterOp_SetDestination, 0xffffffffu },
            { XPainterCompositionMode_RasterOp_NotDestination,
              (uint32_t)(~destination) }
        };
        int i;
        for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); ++i)
        {
            XImage_setPixel(&image, 0, 0, destination);
            XPainter_setCompositionMode(&painter, cases[i].m_mode);
            expect_true(XPainter_compositionMode(&painter) == cases[i].m_mode,
                        "RasterOp setter/getter accepts Qt mode");
            expect_true(XPainter_fillRect(&painter, &one, source),
                        "RasterOp fill succeeds");
            expect_true(XImage_pixel(&image, 0, 0) == cases[i].m_expected,
                        "RasterOp computes ARGB32 bitwise result");
        }
    }
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
    XPainter_setCompositionMode(&painter, (XPainterCompositionMode)99);
    expect_true(XPainter_compositionMode(&painter) ==
                    XPainterCompositionMode_SourceOver,
                "unsupported composition mode keeps previous mode");
    XPainter_setOpacity(&painter, NAN);
    expect_true(XPainter_opacity(&painter) == 0.0f,
                "opacity NaN follows Qt bounded result");
    XPainter_setOpacity(&painter, 1.0f);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
#if XPAINTER_BACKGROUND_ON
    expect_true(XPainter_backgroundMode(&painter) ==
                    XPainterBackgroundMode_Transparent,
                "background mode defaults to transparent");
#if XPAINTER_BRUSH_ON
    {
        XPainterBrush backgroundBrush;
        XPainterBrush backgroundOut;
        memset(&backgroundBrush, 0, sizeof(backgroundBrush));
        backgroundBrush.m_style = XPainterBrushStyle_SolidPattern;
        backgroundBrush.m_color = 0xff204060u;
        XPainter_setBackground_2(&painter, &backgroundBrush);
        XPainter_backgroundBrush(&painter, &backgroundOut);
        expect_true(backgroundOut.m_style == XPainterBrushStyle_SolidPattern &&
                        backgroundOut.m_color == 0xff204060u &&
                        XPainter_background(&painter) == 0xff204060u,
                    "background brush setter/getter matches Qt state");
        XImage_fillRect(&image, NULL, 0u);
        XPainter_setBackgroundMode(&painter, XPainterBackgroundMode_Opaque);
        expect_true(XPainter_drawText(&painter, 0, 16, " ", 0xffffffffu),
                    "opaque text accepts blank glyph");
        expect_true(XImage_pixel(&image, 0,
                                 16 - XPainter_textAscent(NULL)) ==
                        0xff204060u,
                    "opaque text fills glyph cell from background brush");
    }
#endif /* XPAINTER_BRUSH_ON */
    XPainter_setBackgroundMode(&painter, XPainterBackgroundMode_Opaque);
    expect_true(XPainter_backgroundMode(&painter) ==
                    XPainterBackgroundMode_Opaque,
                "background mode setter matches Qt state");
    XPainter_setBackgroundMode(&painter, (XPainterBackgroundMode)99);
    expect_true(XPainter_backgroundMode(&painter) ==
                    XPainterBackgroundMode_Opaque,
                "invalid background mode keeps previous state");
    XPainter_setBackgroundMode(&painter, XPainterBackgroundMode_Transparent);
#endif /* XPAINTER_BACKGROUND_ON */

    /* 平移变换 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_resetTransform(&painter);
    XPainter_translate(&painter, 2.0f, 3.0f);
    XPainter_setPen(&painter, 0xffffffffu);
    expect_true(XPainter_drawPoint(&painter, 0, 0),
                "raster translated point");
    expect_true(XImage_pixel(&image, 2, 3) == 0xffffffffu,
                "raster translate draws shifted");
    expect_true(XImage_pixel(&image, 0, 0) == 0,
                "raster translate leaves origin");
    XPainter_resetTransform(&painter);

    /* 旋转变换：顺时针 90° 把 (1,0) 映射到 (0,1) */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_rotate(&painter, 90.0f);
    expect_true(XPainter_drawPoint(&painter, 1, 0), "raster rotated point");
    expect_true(XImage_pixel(&image, 0, 1) == 0xffffffffu,
                "raster rotate maps point clockwise");
    expect_true(XImage_pixel(&image, 1, 0) == 0,
                "raster rotate clears old position");
    XPainter_resetTransform(&painter);

    /* 绘制图像（恒等变换快速路径） */
    XImage_init_ex(&tile, 2, 2, XImageFormat_ARGB32);
    XImage_fillRect(&tile, NULL, 0xff00ff00u);
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawImage(&painter, &tile, 3, 4), "raster drawImage");
    expect_true(XImage_pixel(&image, 3, 4) == 0xff00ff00u,
                "raster image px0");
    expect_true(XImage_pixel(&image, 4, 5) == 0xff00ff00u,
                "raster image px1");
    expect_true(XImage_pixel(&image, 2, 4) == 0, "raster image margin");
    expect_true(XPainter_drawImage(&painter, NULL, 0, 0) == false,
                "raster drawImage null rejected");
#if XPAINTER_PIXMAP_ON
    /* QPainter::drawPixmap：先验证常规像素尺寸，再验证高分辨率像素图
       按 devicePixelRatio 转为逻辑尺寸。 */
    XPixmap_init(&pixmap);
    expect_true(XPixmap_convertFromImage(&pixmap, &tile, 0),
                "raster drawPixmap converts source image");
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawPixmap(&painter, &pixmap, 3, 4),
                "raster drawPixmap point overload");
    expect_true(XImage_pixel(&image, 3, 4) == 0xff00ff00u &&
                XImage_pixel(&image, 4, 5) == 0xff00ff00u,
                "raster drawPixmap preserves 1x physical size");
    XPixmap_setDevicePixelRatio(&pixmap, 2.0f);
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawPixmap_2(&painter, &pixmap,
                                      &(XPoint){ 1, 1 }),
                "raster drawPixmap point-object overload");
#if XPAINTER_IMAGE_RECT_ON
    expect_true(XImage_pixel(&image, 1, 1) == 0xff00ff00u &&
                XImage_pixel(&image, 2, 1) == 0u,
                "raster high-resolution drawPixmap uses logical size");
#else
    expect_true(XImage_pixel(&image, 1, 1) == 0xff00ff00u &&
                XImage_pixel(&image, 2, 1) == 0xff00ff00u,
                "raster cropped drawPixmap uses physical size");
#endif /* XPAINTER_IMAGE_RECT_ON */
#if XPAINTER_IMAGE_RECT_ON
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawPixmapRect(&painter, &pixmapTarget, &pixmap,
                                        &pixmapSource),
                "raster drawPixmap target/source overload");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff00ff00u &&
                XImage_pixel(&image, 3, 1) == 0xff00ff00u,
                "raster drawPixmap scales source rectangle");
#endif /* XPAINTER_IMAGE_RECT_ON */
#if XPAINTER_TILED_PIXMAP_ON
    /* QPainter::drawTiledPixmap：偏移按逻辑 tile 尺寸取模，负值和正值
       都从同一源像素开始；每个边缘 tile 只绘制目标矩形内的部分。 */
    XImage_setPixel(&tile, 0, 0, 0xffff0000u);
    XImage_setPixel(&tile, 1, 0, 0xff00ff00u);
    XImage_setPixel(&tile, 0, 1, 0xff0000ffu);
    XImage_setPixel(&tile, 1, 1, 0xffffff00u);
    expect_true(XPixmap_convertFromImage(&pixmap, &tile, 0),
                "raster tiled pixmap converts pattern");
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawTiledPixmap(&painter, &tiledTarget, &pixmap,
                                         &tiledOffset),
                "raster drawTiledPixmap offset");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffffff00u &&
                XImage_pixel(&image, 1, 0) == 0xff0000ffu &&
                XImage_pixel(&image, 0, 1) == 0xff00ff00u &&
                XImage_pixel(&image, 1, 1) == 0xffff0000u &&
                XImage_pixel(&image, 4, 2) == 0xffffff00u,
                "raster drawTiledPixmap repeats and clips tiles");
    tiledOffset.x = -1;
    tiledOffset.y = -1;
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawTiledPixmap(&painter, &tiledTarget, &pixmap,
                                         &tiledOffset),
                "raster drawTiledPixmap negative offset");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffffff00u &&
                XImage_pixel(&image, 1, 1) == 0xffff0000u,
                "raster drawTiledPixmap negative offset wraps");
#endif /* XPAINTER_TILED_PIXMAP_ON */
    expect_true(XPainter_drawPixmap(&painter, NULL, 0, 0) == false,
                "raster drawPixmap null rejected");
    XPixmap_deinit_base(&pixmap);
#endif /* XPAINTER_PIXMAP_ON */
    XImage_deinit_base(&tile);

#if XPAINTER_IMAGE_RECT_ON
    /* Qt drawImage(target, image, source)：最近邻缩放并保持源像素分区。 */
    XImage_init_ex(&strip, 2, 1, XImageFormat_ARGB32);
    XImage_setPixel(&strip, 0, 0, 0xffff0000u);
    XImage_setPixel(&strip, 1, 0, 0xff0000ffu);
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawImageRect(&painter, &imageTarget, &strip,
                                       &imageSource),
                "raster drawImage target/source rect");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffff0000u &&
                XImage_pixel(&image, 1, 1) == 0xffff0000u &&
                XImage_pixel(&image, 2, 0) == 0xff0000ffu &&
                XImage_pixel(&image, 3, 1) == 0xff0000ffu,
                "raster drawImage target/source nearest scaling");
    expect_true(XPainter_drawImageRect(&painter, &empty, &strip,
                                       &imageSource),
                "raster drawImage empty target no-op");
    /* Qt QRectF 重载：目标负宽度改用源宽度，源零宽度取到图像右边界。 */
    {
        XRect negativeTarget = { 0, 2, -2, 1 };
        XRect sourceToEdge = { 0, 0, 0, 1 };
        XImage_fillRect(&image, NULL, 0u);
        expect_true(XPainter_drawImageRect(&painter, &negativeTarget, &strip,
                                           &imageSource),
                    "raster drawImage negative target uses source extent");
        expect_true(XImage_pixel(&image, 0, 2) == 0xffff0000u &&
                    XImage_pixel(&image, 1, 2) == 0xff0000ffu &&
                    XImage_pixel(&image, 2, 2) == 0u,
                    "negative target width keeps target origin");
        expect_true(XPainter_drawImageRect(&painter, &imageTarget, &strip,
                                           &sourceToEdge),
                    "raster drawImage zero source width uses image edge");
        expect_true(XImage_pixel(&image, 0, 0) == 0xffff0000u &&
                    XImage_pixel(&image, 3, 1) == 0xff0000ffu,
                    "zero source width samples through image edge");
    }
    XImage_fillRect(&image, NULL, 0xff102030u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    {
        XRect outsideSource = { -1, 0, 2, 1 };
        XRect outsideTarget = { 0, 0, 2, 1 };
        expect_true(XPainter_drawImageRect(&painter, &outsideTarget, &strip,
                                           &outsideSource),
                    "raster drawImage clips source bounds");
        expect_true(XImage_pixel(&image, 0, 0) == 0xff102030u &&
                    XImage_pixel(&image, 1, 0) == 0xffff0000u,
                    "source clipping shifts target and keeps untouched area");
    }
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
    XImage_deinit_base(&strip);
#endif /* XPAINTER_IMAGE_RECT_ON */

    /* 空矩形/空图像语义 */
    expect_true(XPainter_drawRect(&painter, NULL), "raster null drawRect no-op");
    expect_true(XPainter_drawRect(&painter, &empty),
                "raster empty drawRect no-op");
    expect_true(XPainter_fillRect(&painter, &empty, 0xff000000u),
                "raster empty fillRect no-op");
    expect_true(XPainter_fillRect(&painter, NULL, 0xff000000u) == false,
                "raster null fillRect rejected");

    /* 结束与解绑 */
    expect_true(XPainter_end(&painter), "raster end");
    expect_true(!XPainter_isActive(&painter), "raster inactive after end");
    expect_true(!XPainter_end(&painter), "ending inactive painter returns false");
    expect_true(XPainter_drawLine(&painter, 0, 0, 1, 1) == false,
                "raster unbound drawLine rejected");
    XPainter_deinit(&painter);
    expect_true(!XPainter_begin_image(&painter, &image),
                "deinitialized painter cannot be reused without init");
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}


static void test_painter_extra_alignment(void)
{
    XImage image;
    XPainter painter;
    XRect r1 = { 1, 1, 2, 2 };
    XRect er = { 2, 4, 2, 2 };
#if XPAINTER_CLIP_ON
    XRect clip = { 2, 2, 3, 3 };
    XRect clipIntersection = { 3, 1, 3, 3 };
    XRect logicalClip = { 1, 1, 2, 2 };
    XRect all = { 0, 0, 8, 8 };
    XRect emptyClip = { 6, 7, 0, 0 };
    XRect clipOut;
    XRect zero = { 0, 0, 0, 0 };
#endif
    XPoint lines[4] = { {1,1}, {3,3}, {5,1}, {5,3} };
#if XPAINTER_POLYGON_ON
    XPoint pts[3] = { {2,2}, {10,2}, {2,9} };
#endif

    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "extra alignment begins raster");

    /* 批量矩形 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenWidth(&painter, 1);
    expect_true(XPainter_drawRects(&painter, &r1, 1), "extra drawRects");
    expect_true(XPainter_drawRects(&painter, NULL, 0),
                "extra drawRects empty no-op");
    expect_true(XImage_pixel(&image, 1, 1) == 0xffff0000u,
                "extra drawRects corner tl");
    expect_true(XImage_pixel(&image, 3, 3) == 0xffff0000u,
                "extra drawRects corner br");

    /* 批量直线 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setPen(&painter, 0xff00ff00u);
    expect_true(XPainter_drawLines(&painter, lines, 2), "extra drawLines");
    expect_true(XPainter_drawLines(&painter, NULL, 0),
                "extra drawLines empty no-op");
    expect_true(XImage_pixel(&image, 1, 1) == 0xff00ff00u,
                "extra drawLines first start");
    expect_true(XImage_pixel(&image, 3, 3) == 0xff00ff00u,
                "extra drawLines first end");
    expect_true(XImage_pixel(&image, 5, 1) == 0xff00ff00u,
                "extra drawLines second start");
    expect_true(XImage_pixel(&image, 5, 3) == 0xff00ff00u,
                "extra drawLines second end");

    /* 背景颜色 + eraseRect */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setBackground(&painter, 0xff4080ffu);
    expect_true(XPainter_background(&painter) == 0xff4080ffu,
                "extra background getter");
    expect_true(XPainter_eraseRect(&painter, &er), "extra eraseRect");
    expect_true(XImage_pixel(&image, 2, 4) == 0xff4080ffu,
                "extra eraseRect top-left");
    expect_true(XImage_pixel(&image, 3, 5) == 0xff4080ffu,
                "extra eraseRect bottom-right");
    expect_true(XImage_pixel(&image, 1, 4) == 0,
                "extra eraseRect stays outside");
#if XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON
    /* eraseRect 使用完整背景画刷，且不能改写当前前景画刷。 */
    {
        XPainterBrush foreground;
        XPainterBrush background;
        XPainterBrush foregroundOut;
        memset(&foreground, 0, sizeof(foreground));
        foreground.m_style = XPainterBrushStyle_SolidPattern;
        foreground.m_color = 0xffff0000u;
        memset(&background, 0, sizeof(background));
        background.m_style = XPainterBrushStyle_SolidPattern;
        background.m_color = 0xff00ff00u;
        XPainter_setBrush(&painter, foreground.m_color);
        XPainter_setBackground_2(&painter, &background);
        XImage_fillRect(&image, NULL, 0u);
        expect_true(XPainter_eraseRect(&painter, &er),
                    "eraseRect uses background brush");
        expect_true(XImage_pixel(&image, 2, 4) == 0xff00ff00u,
                    "eraseRect paints background brush color");
        XPainter_brush(&painter, &foregroundOut);
        expect_true(foregroundOut.m_style == XPainterBrushStyle_SolidPattern &&
                        foregroundOut.m_color == 0xffff0000u,
                    "eraseRect preserves foreground brush");
        XPainter_setBackground_2(&painter, NULL);
        background.m_style = XPainterBrushStyle_NoBrush;
        XPainter_setBackground_2(&painter, &background);
        XImage_fillRect(&image, NULL, 0xff102030u);
        expect_true(XPainter_eraseRect(&painter, &er),
                    "eraseRect accepts NoBrush background");
        expect_true(XImage_pixel(&image, 2, 4) == 0xff102030u,
                    "NoBrush background leaves pixels unchanged");
    }
#endif /* XPAINTER_BACKGROUND_ON && XPAINTER_BRUSH_ON */

    /* ClipOperation：替换、相交、关闭及逻辑坐标映射。 */
#if XPAINTER_CLIP_ON
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
    clipOut = zero;
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 2 && clipOut.y == 2 && clipOut.width == 3 &&
                clipOut.height == 3, "extra clipBoundingRect active");
    XPainter_setClipRect(&painter, &clipIntersection,
                         XPainterClipOperation_IntersectClip);
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 3 && clipOut.y == 2 && clipOut.width == 2 &&
                clipOut.height == 2, "extra IntersectClip bounding rect");
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_fillRect(&painter, &all, 0xff224466u),
                "extra IntersectClip fill");
    expect_true(XImage_pixel(&image, 3, 2) == 0xff224466u &&
                XImage_pixel(&image, 4, 3) == 0xff224466u,
                "extra IntersectClip keeps intersection pixels");
    expect_true(XImage_pixel(&image, 2, 2) == 0u &&
                XImage_pixel(&image, 5, 3) == 0u,
                "extra IntersectClip rejects outside pixels");
    XPainter_setClipping(&painter, false);
    expect_true(!XPainter_hasClipping(&painter), "extra setClipping off");
    clipOut = zero;
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 3 && clipOut.y == 2 && clipOut.width == 2 &&
                clipOut.height == 2,
                "extra clipBoundingRect kept while disabled");
    XPainter_setClipping(&painter, true);
    expect_true(XPainter_hasClipping(&painter), "extra setClipping on");
    XPainter_setClipRect(&painter, NULL, XPainterClipOperation_NoClip);
    expect_true(XPainter_hasClipping(&painter), "extra null clip is no-op");
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_NoClip);
    expect_true(!XPainter_hasClipping(&painter), "extra NoClip clears clipping");
    clipOut = zero;
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 2 && clipOut.y == 2 && clipOut.width == 3 &&
                clipOut.height == 3,
                "extra NoClip keeps the latest clip query record");
#if XPAINTER_CLIP_REGION_ON
    {
        XRegion noClipRegion;
        XRegion_init(&noClipRegion);
        XPainter_clipRegion(&painter, &noClipRegion);
        expect_true(XRegion_contains(&noClipRegion, 2, 2) &&
                    XRegion_contains(&noClipRegion, 4, 4),
                    "extra NoClip keeps the latest clip region record");
        XRegion_deinit(&noClipRegion);
    }
#endif /* XPAINTER_CLIP_REGION_ON */
    XPainter_setClipping(&painter, true);
    expect_true(!XPainter_hasClipping(&painter),
                "extra NoClip cannot be re-enabled without a new clip");
    XPainter_setClipRect(&painter, &emptyClip,
                         XPainterClipOperation_ReplaceClip);
    expect_true(XPainter_hasClipping(&painter), "extra empty ReplaceClip remains active");
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_fillRect(&painter, &all, 0xff6688aau),
                "extra empty ReplaceClip fill command");
    expect_true(XImage_pixel(&image, 0, 0) == 0u,
                "extra empty ReplaceClip rejects all pixels");
    clipOut = zero;
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 6 && clipOut.y == 7 && clipOut.width == 0 &&
                clipOut.height == 0,
                "extra clipBoundingRect preserves empty clip origin");
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_NoClip);

    XImage_fillRect(&image, NULL, 0u);
    XPainter_resetTransform(&painter);
    XPainter_translate(&painter, 2.0f, 1.0f);
    XPainter_setClipRect(&painter, &logicalClip,
                         XPainterClipOperation_ReplaceClip);
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 1 && clipOut.y == 1 && clipOut.width == 2 &&
                clipOut.height == 2, "extra clip uses logical coordinates");
    expect_true(XPainter_fillRect(&painter, &all, 0xffaa6633u),
                "extra transformed clip fill");
    expect_true(XImage_pixel(&image, 3, 2) == 0xffaa6633u &&
                XImage_pixel(&image, 4, 3) == 0xffaa6633u,
                "extra transformed clip maps to device");
    expect_true(XImage_pixel(&image, 2, 2) == 0u,
                "extra transformed clip rejects device exterior");
    XPainter_resetTransform(&painter);
    XPainter_clipBoundingRect(&painter, &clipOut);
    expect_true(clipOut.x == 3 && clipOut.y == 2 && clipOut.width == 2 &&
                clipOut.height == 2,
                "extra clipBoundingRect follows current logical transform");
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_NoClip);

#if XPAINTER_CLIP_REGION_ON
    {
        XRegion region;
        XRegion output;
        XRegion wideRegion;
        XRect regionA = { 1, 1, 2, 2 };
        XRect regionB = { 5, 1, 2, 2 };
        XRect regionC = { 1, 1, 6, 2 };
        XRegion_init(&region);
        XRegion_init(&output);
        XRegion_init(&wideRegion);
        XRegion_addRect(&region, &regionA);
        XRegion_addRect(&region, &regionB);
        XPainter_setClipRegion(&painter, &region,
                               XPainterClipOperation_ReplaceClip);
        expect_true(XPainter_hasClipping(&painter),
                    "extra setClipRegion activates clipping");
        XPainter_clipRegion(&painter, &output);
        expect_true(XRegion_contains(&output, 1, 1) &&
                    XRegion_contains(&output, 5, 1) &&
                    !XRegion_contains(&output, 3, 1),
                    "extra clipRegion keeps disjoint logical rectangles");
        XImage_fillRect(&image, NULL, 0u);
        expect_true(XPainter_fillRect(&painter, &all, 0xff335577u),
                    "extra setClipRegion fill command");
        expect_true(XImage_pixel(&image, 1, 1) == 0xff335577u &&
                    XImage_pixel(&image, 5, 1) == 0xff335577u &&
                    XImage_pixel(&image, 3, 1) == 0u,
                    "extra setClipRegion rejects the region gap");

        XPainter_save(&painter);
        XRegion_addRect(&wideRegion, &regionC);
        XPainter_setClipRegion(&painter, &wideRegion,
                               XPainterClipOperation_ReplaceClip);
        XPainter_restore(&painter);
        XRegion_clear(&output);
        XPainter_clipRegion(&painter, &output);
        expect_true(XRegion_contains(&output, 1, 1) &&
                    XRegion_contains(&output, 5, 1) &&
                    !XRegion_contains(&output, 3, 1),
                    "extra save/restore preserves clipRegion state");
        XRegion_deinit(&output);
        XRegion_deinit(&wideRegion);
        XRegion_deinit(&region);
        XPainter_setClipRect(&painter, &clip, XPainterClipOperation_NoClip);
    }
#endif /* XPAINTER_CLIP_REGION_ON */
#endif /* XPAINTER_CLIP_ON */

#if XPAINTER_POLYGON_ON
    /* 凸多边形：无 filled 参数，始终用当前画刷填充 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_setBrush(&painter, 0xff000080u);
    expect_true(XPainter_drawConvexPolygon(&painter, pts, 3),
                "extra drawConvexPolygon");
    expect_true(XImage_pixel(&image, 3, 4) == 0xff000080u,
                "extra convex polygon fills interior");
    expect_true(XImage_pixel(&image, 5, 2) == 0xff00ff00u,
                "extra convex polygon edge uses current pen");
    expect_true(XImage_pixel(&image, 1, 5) == 0,
                "extra convex polygon stays outside");
#endif

    expect_true(XPainter_end(&painter), "extra alignment end");
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

static void test_painter_draw_picture_align(void)
{
    XPicture picture;
    XImage target;
    XPainter painter;

    /* 录制：红色水平线段 (0,0)-(2,0)。 */
    XPicture_init(&picture, -1);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_picture(&painter, &picture), "drawPicture record begin");
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawLine(&painter, 0, 0, 2, 0), "drawPicture record line");
    expect_true(XPainter_end(&painter), "drawPicture record end");
    expect_true(XPicture_isValidStream(&picture), "drawPicture stream valid");

    /* 回放到新位置：坐标 (4,3) 起绘制。 */
    XImage_init_ex(&target, 8, 8, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff00ff00u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target), "drawPicture target begin");
    /* XPicture 回放沿用调用者当前画笔颜色（与本项目既有录制语义一致）。 */
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawPicture(&painter, &picture, 4, 3),
                "drawPicture at position");
    expect_true(XImage_pixel(&target, 4, 3) == 0xffff0000u,
                "drawPicture start painted");
    expect_true(XImage_pixel(&target, 6, 3) == 0xffff0000u,
                "drawPicture end painted");
    expect_true(XImage_pixel(&target, 0, 3) == 0xff00ff00u &&
                XImage_pixel(&target, 4, 0) == 0xff00ff00u,
                "drawPicture leaves outside green");
    expect_true(!XPainter_drawPicture(&painter, NULL, 0, 0),
                "drawPicture NULL picture returns false");
    expect_true(XPainter_end(&painter), "drawPicture target end");

    XPainter_deinit(&painter);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}

#if XPAINTER_SHAPE_ON
static void test_painter_shape_contract(void)
{
    XImage image;
    XPainter painter;
    XRect ellipseRect  = { 0, 0, 20, 10 };
    XRect fullSweep    = { 0, 0, 20, 10 };
    XRect roundRect    = { 0, 0, 20, 10 };

    XImage_init_ex(&image, 32, 16, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "shape painter begins raster");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, 0xff0000ffu);

    /* 椭圆：内部用画刷填充、轮廓用画笔描出 */
    expect_true(XPainter_drawEllipse(&painter, &ellipseRect),
                "drawEllipse on raster");
    expect_true(XImage_pixel(&image, 10, 5) == 0xff0000ffu,
                "ellipse interior brush color");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "ellipse top outline pen color");
    expect_true(XImage_pixel(&image, 20, 5) == 0xffff0000u,
                "ellipse right outline pen color");
    expect_true(XImage_pixel(&image, 21, 5) == 0xff000000u,
                "ellipse outside remains background");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 圆弧：完整一圈，只描边不填充 */
    expect_true(XPainter_drawArc(&painter, &fullSweep,
                                 0, 16 * 360), "drawArc full sweep");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "arc top pixel");
    expect_true(XImage_pixel(&image, 20, 5) == 0xffff0000u,
                "arc right pixel");
    expect_true(XImage_pixel(&image, 10, 5) == 0xff000000u,
                "arc interior not filled");

    /* Qt 角度约定：0 度在 3 点钟，正 90 度沿逆时针到 12 点钟；
       用四分之一圆弧区分设备 Y 轴方向，避免整圆测试掩盖反向错误。 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawArc(&painter, &fullSweep, 0, 16 * 90),
                "arc quarter sweep");
    expect_true(XImage_pixel(&image, 20, 5) == 0xffff0000u,
                "quarter arc starts at three o'clock");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "quarter arc positive sweep reaches twelve o'clock");
    expect_true(XImage_pixel(&image, 10, 10) == 0xff000000u,
                "quarter arc positive sweep does not reach six o'clock");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 扇形：填充内部 + 轮廓与半径线 */
    expect_true(XPainter_drawPie(&painter, &fullSweep,
                                 0, 16 * 360), "drawPie filled");
    expect_true(XImage_pixel(&image, 10, 5) == 0xffff0000u,
                "pie center covered by radial outline");
    expect_true(XImage_pixel(&image, 12, 8) == 0xff0000ffu,
                "pie interior brush color");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "pie arc outline");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 弦：填充内部 + 闭合弦轮廓 */
    expect_true(XPainter_drawChord(&painter, &fullSweep,
                                   0, 16 * 360), "drawChord filled");
    expect_true(XImage_pixel(&image, 10, 5) == 0xff0000ffu,
                "chord center brush color");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "chord top outline");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 圆角矩形：角被切掉、内部填充、上边中点描边 */
    expect_true(XPainter_drawRoundedRect(&painter, &roundRect, 4, 4),
                "drawRoundedRect");
    expect_true(XImage_pixel(&image, 10, 5) == 0xff0000ffu,
                "rounded rect interior brush");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffff0000u,
                "rounded rect top edge pen");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff000000u,
                "rounded rect corner cut away");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

typedef struct ShapeCapture
{
    int m_count;
    XPainterShapeOp m_ops[5];
    XRect m_rects[5];
    int m_startAngles[5];
    int m_spanAngles[5];
    bool m_filled[5];
    int m_xRadius[5];
    int m_yRadius[5];
} ShapeCapture;

static bool test_shape_capture_proc(XPainter* painter, XPainterShapeOp op,
                                    const XRect* rect, int startAngle,
                                    int spanAngle, bool filled,
                                    int xRadius, int yRadius)
{
    ShapeCapture* capture = (ShapeCapture*)painter->m_userData;
    expect_true(capture != NULL, "shape capture user data");
    if (capture && capture->m_count < 5)
    {
        int i = capture->m_count;
        capture->m_ops[i] = op;
        capture->m_rects[i] = *rect;
        capture->m_startAngles[i] = startAngle;
        capture->m_spanAngles[i] = spanAngle;
        capture->m_filled[i] = filled;
        capture->m_xRadius[i] = xRadius;
        capture->m_yRadius[i] = yRadius;
        ++capture->m_count;
    }
    return true;
}

static void test_painter_shape_callback_contract(void)
{
    XImage image;
    XPainter painter;
    ShapeCapture capture;
    XRect ellipseRect  = { 0, 0, 20, 10 };
    XRect fullSweep    = { 0, 0, 20, 10 };
    XRect roundRect    = { 0, 0, 20, 10 };

    capture.m_count = 0;
    XImage_init_ex(&image, 32, 16, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, &capture);
    expect_true(XPainter_begin_image(&painter, &image),
                "shape callback painter begins raster");
    painter.m_drawShape = test_shape_capture_proc;
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setBrush(&painter, 0xff0000ffu);

    expect_true(XPainter_drawEllipse(&painter, &ellipseRect),
                "shape callback ellipse");
    expect_true(XPainter_drawArc(&painter, &fullSweep, 90, 16 * 120),
                "shape callback arc");
    expect_true(XPainter_drawPie(&painter, &fullSweep, 0, 16 * 90),
                "shape callback pie");
    expect_true(XPainter_drawChord(&painter, &fullSweep, 45, 16 * 60),
                "shape callback chord");
    expect_true(XPainter_drawRoundedRect(&painter, &roundRect, 4, 4),
                "shape callback rounded rect");

    expect_true(capture.m_count == 5, "shape callback captures all five ops");
    if (capture.m_count == 5)
    {
        expect_true(capture.m_ops[0] == XPainterShapeOp_Ellipse &&
                    capture.m_spanAngles[0] == 0 && capture.m_filled[0],
                    "ellipse op params and brush fill");
        expect_true(capture.m_ops[1] == XPainterShapeOp_Arc &&
                    capture.m_startAngles[1] == 90 &&
                    capture.m_spanAngles[1] == 16 * 120 &&
                    !capture.m_filled[1], "arc op params");
        expect_true(capture.m_ops[2] == XPainterShapeOp_Pie &&
                    capture.m_filled[2], "pie op params");
        expect_true(capture.m_ops[3] == XPainterShapeOp_Chord &&
                    capture.m_filled[3], "chord op params");
        expect_true(capture.m_ops[4] == XPainterShapeOp_RoundedRect &&
                    capture.m_xRadius[4] == 4 &&
                    capture.m_yRadius[4] == 4 && capture.m_filled[4],
                    "rounded rect op params and brush fill");
    }

    /* Qt 在半径任一非正时先退化为 drawRect，不分派 RoundedRect 回调。 */
    capture.m_count = 0;
    {
        XRect zeroRadius = { 0, 0, 20, 10 };
        expect_true(XPainter_drawRoundedRect(&painter, &zeroRadius, 0, 4),
                    "rounded rect zero radius falls back to drawRect");
        expect_true(capture.m_count == 0,
                    "zero radius does not dispatch rounded rect callback");
    }

    capture.m_count = 0;
    {
        XRect reversed = { 20, 10, -12, -6 };
#if XPAINTER_BRUSH_ON
        XPainter_setBrushStyle(&painter, XPainterBrushStyle_NoBrush);
#endif /* XPAINTER_BRUSH_ON */
        expect_true(XPainter_drawEllipse(&painter, &reversed),
                    "ellipse normalizes reversed integer rectangle");
#if XPAINTER_BRUSH_ON
        expect_true(capture.m_count == 1 && capture.m_rects[0].x == 8 &&
                    capture.m_rects[0].y == 4 && capture.m_rects[0].width == 12 &&
                    capture.m_rects[0].height == 6 && !capture.m_filled[0],
                    "ellipse callback receives normalized rect and NoBrush");
#else
        expect_true(capture.m_count == 1 && capture.m_rects[0].x == 8 &&
                    capture.m_rects[0].y == 4 && capture.m_rects[0].width == 12 &&
                    capture.m_rects[0].height == 6 && capture.m_filled[0],
                    "ellipse callback receives normalized rect");
#endif /* XPAINTER_BRUSH_ON */
        XPainter_setBrush(&painter, 0xff0000ffu);
    }

    capture.m_count = 0;
    expect_true(XPainter_drawArc(&painter, &fullSweep, 0, 0),
                "zero span arc no-op");
    expect_true(XPainter_drawPie(&painter, &fullSweep, 0, 0),
                "zero span pie no-op");
    expect_true(XPainter_drawChord(&painter, &fullSweep, 0, 0),
                "zero span chord no-op");
    expect_true(capture.m_count == 0, "zero span calls do not dispatch");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_SHAPE_ON */

#if XPAINTER_POLYGON_ON
static void test_painter_polygon_contract(void)
{
    XImage image;
    XPainter painter;
    XPoint tri[3] = { { 2, 2 }, { 10, 2 }, { 2, 9 } };
    XPoint linePts[2] = { { 0, 0 }, { 9, 0 } };
    XPoint pts[3] = { { 0, 0 }, { 3, 3 }, { 6, 0 } };

    XImage_init_ex(&image, 24, 14, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "polygon painter begins raster");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, 0xff0000ffu);

    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "drawPolygon filled triangle");
    expect_true(XImage_pixel(&image, 3, 4) == 0xff0000ffu,
                "polygon interior brush");
    expect_true(XImage_pixel(&image, 8, 4) == 0xffff0000u,
                "polygon edge outline pen color");
    expect_true(XImage_pixel(&image, 9, 5) == 0xff000000u,
                "polygon outside blank");

    /* Qt raster polygons use pixel-center sampling on the half-open
       rectangle [left,right) x [top,bottom).  A square ending at (6,6)
       therefore paints through pixel 5 only, without a one-pixel spill. */
#if XPAINTER_PENSTYLE_ON
    XPainter_setPenStyle(&painter, XPainterPenStyle_NoPen);
#endif
    XImage_fillRect(&image, NULL, 0xff000000u);
    {
        XPoint square[4] = { { 2, 2 }, { 6, 2 }, { 6, 6 }, { 2, 6 } };
        expect_true(XPainter_drawPolygon(&painter, square, 4,
                                         XPainterFillRule_OddEven),
                    "polygon half-open square");
    }
    expect_true(XImage_pixel(&image, 5, 5) == 0xff0000ffu &&
                XImage_pixel(&image, 6, 5) == 0xff000000u &&
                XImage_pixel(&image, 5, 6) == 0xff000000u,
                "polygon does not spill beyond integer edge");
#if XPAINTER_PENSTYLE_ON
    XPainter_setPenStyle(&painter, XPainterPenStyle_SolidLine);
#endif

    /* 同一轮廓沿相同方向绕行两次：Qt 的 OddEvenFill 应形成空洞，
       WindingFill 则因绕组数为 2 继续填充内部。 */
    {
        XPoint doubleLoop[8] = {
            { 2, 2 }, { 14, 2 }, { 14, 14 }, { 2, 14 },
            { 2, 2 }, { 14, 2 }, { 14, 14 }, { 2, 14 }
        };
        XPainter_setPenStyle(&painter, XPainterPenStyle_NoPen);
        XImage_fillRect(&image, NULL, 0xff000000u);
        expect_true(XPainter_drawPolygon(&painter, doubleLoop, 8,
                                         XPainterFillRule_OddEven),
                    "odd-even double loop polygon");
        expect_true(XImage_pixel(&image, 8, 8) == 0xff000000u,
                    "odd-even cancels repeated loop");
        XImage_fillRect(&image, NULL, 0xff000000u);
        expect_true(XPainter_drawPolygon(&painter, doubleLoop, 8,
                                         XPainterFillRule_Winding),
                    "winding double loop polygon");
        expect_true(XImage_pixel(&image, 8, 8) == 0xff0000ffu,
                    "winding retains repeated loop");
        XPainter_setPenStyle(&painter, XPainterPenStyle_SolidLine);
    }

    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPolyline(&painter, linePts, 2),
                "drawPolyline");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffff0000u &&
                XImage_pixel(&image, 5, 0) == 0xffff0000u &&
                XImage_pixel(&image, 9, 0) == 0xffff0000u,
                "polyline drawn end to end");

    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPoints(&painter, pts, 3), "drawPoints");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffff0000u &&
                XImage_pixel(&image, 3, 3) == 0xffff0000u &&
                XImage_pixel(&image, 6, 0) == 0xffff0000u &&
                XImage_pixel(&image, 1, 1) == 0xff000000u,
                "drawPoints plots each point");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

typedef struct PolygonCapture
{
    int m_polylineCalls;
    int m_polygonCalls;
    int m_pointsCalls;
    int m_polylineCount;
    int m_polygonCount;
    int m_pointsCount;
    bool m_polygonFilled;
    XPainterFillRule m_polygonFillRule;
    XPoint m_points[3];
} PolygonCapture;

static bool test_polyline_capture_proc(XPainter* painter,
                                       const XPoint* points, int count)
{
    PolygonCapture* cap = (PolygonCapture*)painter->m_userData;
    if (!cap) return true;
    ++cap->m_polylineCalls;
    cap->m_polylineCount = count;
    if (count <= 0 || count > 3) return true;
    memcpy(cap->m_points, points, (size_t)count * sizeof(cap->m_points[0]));
    return true;
}

static bool test_polygon_capture_proc(XPainter* painter,
                                      const XPoint* points, int count,
                                      bool filled,
                                      XPainterFillRule fillRule)
{
    PolygonCapture* cap = (PolygonCapture*)painter->m_userData;
    if (!cap) return true;
    ++cap->m_polygonCalls;
    cap->m_polygonCount = count;
    cap->m_polygonFilled = filled;
    cap->m_polygonFillRule = fillRule;
    if (count <= 0 || count > 3) return true;
    memcpy(cap->m_points, points, (size_t)count * sizeof(cap->m_points[0]));
    return true;
}

static bool test_points_capture_proc(XPainter* painter,
                                     const XPoint* points, int count)
{
    PolygonCapture* cap = (PolygonCapture*)painter->m_userData;
    if (!cap) return true;
    ++cap->m_pointsCalls;
    cap->m_pointsCount = count;
    if (count <= 0 || count > 3) return true;
    memcpy(cap->m_points, points, (size_t)count * sizeof(cap->m_points[0]));
    return true;
}

static void test_painter_polygon_callback_contract(void)
{
    XImage image;
    XPainter painter;
    PolygonCapture cap;
    XPoint linePts[2] = { { 0, 0 }, { 9, 0 } };
    XPoint tri[3] = { { 2, 2 }, { 10, 2 }, { 2, 9 } };
    XPoint pts[3] = { { 0, 0 }, { 3, 3 }, { 6, 0 } };

    memset(&cap, 0, sizeof(cap));
    XImage_init_ex(&image, 24, 14, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, &cap);
    expect_true(XPainter_begin_image(&painter, &image),
                "polygon callback painter begins raster");
    XPainter_setBrush(&painter, 0xff0000ffu);
    painter.m_drawPolyline = test_polyline_capture_proc;
    painter.m_drawPolygon = test_polygon_capture_proc;
    painter.m_drawPoints = test_points_capture_proc;

    expect_true(XPainter_drawPolyline(&painter, linePts, 2),
                "polygon callback polyline");
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_Winding),
                "polygon callback polygon");
    expect_true(XPainter_drawPoints(&painter, pts, 3),
                "polygon callback points");

    expect_true(cap.m_polylineCalls == 1 && cap.m_polygonCalls == 1 &&
                cap.m_pointsCalls == 1, "polygon callbacks all dispatch once");
    expect_true(cap.m_polylineCount == 2 && cap.m_polygonCount == 3 &&
                cap.m_pointsCount == 3, "polygon callback counts match");
    expect_true(cap.m_polygonFilled, "polygon callback carries filled");
    expect_true(cap.m_polygonFillRule == XPainterFillRule_Winding,
                "polygon callback carries fill rule");
    expect_true(cap.m_points[0].x == 0 && cap.m_points[1].x == 3 &&
                cap.m_points[2].x == 6, "points callback payload copied");

    memset(&cap, 0, sizeof(cap));
    expect_true(XPainter_drawConvexPolygon(&painter, tri, 3),
                "convex polygon callback route");
    expect_true(cap.m_polygonCalls == 1 && cap.m_polygonFilled &&
                cap.m_polygonCount == 3,
                "convex polygon routes through polygon callback");

    memset(&cap, 0, sizeof(cap));
#if XPAINTER_BRUSH_ON
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_NoBrush);
#endif /* XPAINTER_BRUSH_ON */
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
#if XPAINTER_BRUSH_ON
                "NoBrush polygon callback remains drawable");
    expect_true(cap.m_polygonCalls == 1 && !cap.m_polygonFilled,
                "polygon callback reports NoBrush without fill");
#else
                "polygon callback remains drawable");
    expect_true(cap.m_polygonCalls == 1 && cap.m_polygonFilled,
                "polygon callback reports fill");
#endif /* XPAINTER_BRUSH_ON */
    XPainter_setBrush(&painter, 0xff000000u);

    memset(&cap, 0, sizeof(cap));
    expect_true(XPainter_drawPolyline(&painter, NULL, 5),
                "polyline NULL points is no-op");
    expect_true(XPainter_drawPolygon(&painter, NULL, 5,
                                     XPainterFillRule_OddEven),
                "polygon NULL points is no-op");
    expect_true(XPainter_drawPoints(&painter, NULL, 5),
                "points NULL array is no-op");
    expect_true(XPainter_drawPolyline(&painter, linePts, 0),
                "polyline zero count is no-op");
    expect_true(XPainter_drawPolygon(&painter, linePts, 0,
                                     XPainterFillRule_OddEven),
                "polygon zero count is no-op");
    expect_true(XPainter_drawPoints(&painter, linePts, 0),
                "points zero count is no-op");
    expect_true(cap.m_polylineCalls == 0 && cap.m_polygonCalls == 0 &&
                cap.m_pointsCalls == 0,
                "invalid polygon calls do not dispatch callbacks");

    XPainter_end(&painter);
    expect_true(painter.m_drawPolyline == NULL &&
                painter.m_drawPolygon == NULL &&
                painter.m_drawPoints == NULL,
                "polygon callbacks cleared at end");
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_POLYGON_ON */

#if XPAINTER_PENSTYLE_ON
static void test_painter_penstyle_contract(void)
{
    XImage image;
    XPainter painter;

    XImage_init_ex(&image, 12, 2, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "penstyle painter begins raster");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenWidth(&painter, 1);

    expect_true(XPainter_penStyle(&painter) == XPainterPenStyle_SolidLine,
                "default pen style solid");
    expect_true(XPainter_penCapStyle(&painter) == XPainterPenCapStyle_SquareCap &&
                XPainter_penJoinStyle(&painter) == XPainterPenJoinStyle_BevelJoin,
                "default pen cap/join match QPen");
    XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);
    expect_true(XPainter_penStyle(&painter) == XPainterPenStyle_DashLine,
                "pen style set/get");
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_SquareCap);
    expect_true(XPainter_penCapStyle(&painter) == XPainterPenCapStyle_SquareCap,
                "pen cap style set/get");
    XPainter_setPenJoinStyle(&painter, XPainterPenJoinStyle_RoundJoin);
    expect_true(XPainter_penJoinStyle(&painter) == XPainterPenJoinStyle_RoundJoin,
                "pen join style set/get");
    XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);
    XPainter_setPenWidth(&painter, 5);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_RoundCap);
    XPainter_setPenJoinStyle(&painter, XPainterPenJoinStyle_RoundJoin);
    XPainter_setPen(&painter, 0xff00ff00u);
    expect_true(XPainter_penStyle(&painter) == XPainterPenStyle_SolidLine &&
                XPainter_penWidth(&painter) == 1 &&
                XPainter_penCapStyle(&painter) == XPainterPenCapStyle_SquareCap &&
                XPainter_penJoinStyle(&painter) == XPainterPenJoinStyle_BevelJoin,
                "setPen color resets QPen defaults");
    XPainter_setPenWidth(&painter, 0);
    expect_true(XPainter_penWidth(&painter) == 0,
                "zero pen width is preserved as cosmetic QPen");
    XPainter_setPenWidth(&painter, -1);
    expect_true(XPainter_penWidth(&painter) == 0,
                "negative pen width is rejected without changing QPen");
    XPainter_setPenWidth(&painter, (1 << 15));
    expect_true(XPainter_penWidth(&painter) == 0,
                "out-of-range pen width is rejected without changing QPen");
    XPainter_setPenWidth(&painter, 1);
    XPainter_setPenStyle(&painter, XPainterPenStyle_DotLine);
    XPainter_setPenWidth(&painter, 7);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_RoundCap);
    XPainter_setPenJoinStyle(&painter, XPainterPenJoinStyle_RoundJoin);
    XPainter_setPen_2(&painter, XPainterPenStyle_NoPen);
    expect_true(XPainter_penColor(&painter) == 0xff000000u &&
                XPainter_penStyle(&painter) == XPainterPenStyle_NoPen &&
                XPainter_penWidth(&painter) == 1 &&
                XPainter_penCapStyle(&painter) == XPainterPenCapStyle_SquareCap &&
                XPainter_penJoinStyle(&painter) == XPainterPenJoinStyle_BevelJoin,
                "setPen style overload resets black QPen defaults");
    XPainter_setPen_2(&painter, XPainterPenStyle_SolidLine);
    expect_true(XPainter_penColor(&painter) == 0xff000000u,
                "setPen style overload uses black color");
    XPainter_setPenStyle(&painter, (XPainterPenStyle)99);
    expect_true((int)XPainter_penStyle(&painter) == 99,
                "setPenStyle preserves unknown Qt enum values");
    XPainter_setPenStyle(&painter, XPainterPenStyle_SolidLine);
    XPainter_setPenCapStyle(&painter, (XPainterPenCapStyle)0x30);
    expect_true((int)XPainter_penCapStyle(&painter) == 0x30,
                "setPen cap style preserves unknown Qt enum values");
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_SquareCap);
    XPainter_setPenJoinStyle(&painter, (XPainterPenJoinStyle)0x100);
    expect_true((int)XPainter_penJoinStyle(&painter) == 0x100,
                "setPen join style preserves unknown Qt enum values");
    XPainter_setPenJoinStyle(&painter, XPainterPenJoinStyle_BevelJoin);
    XPainter_setPen(&painter, 0xff00ff00u);
    XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);

    expect_true(XPainter_drawLine(&painter, 0, 0, 9, 0),
                "dashed line on raster");
    /* 数据序列 4 画 / 3 空 / 2 收尾：0-3 与 7-9 被画，4-6 留空 */
    expect_true(XImage_pixel(&image, 0, 0) == 0xff00ff00u &&
                XImage_pixel(&image, 4, 0) == 0xff00ff00u &&
                XImage_pixel(&image, 7, 0) == 0xff00ff00u &&
                XImage_pixel(&image, 9, 0) == 0xff00ff00u,
                "dashed line draw segments painted");
    expect_true(XImage_pixel(&image, 5, 0) == 0xff000000u &&
                XImage_pixel(&image, 6, 0) == 0xff000000u,
                "dashed line gaps remain blank");

    XPainter_setPenStyle(&painter, XPainterPenStyle_NoPen);
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawLine(&painter, 0, 0, 9, 0),
                "NoPen line is a no-op that reports success");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff000000u &&
                XImage_pixel(&image, 9, 0) == 0xff000000u,
                "NoPen keeps raster untouched");
    {
        XRect noPenRect = { 1, 0, 5, 2 };
        expect_true(XPainter_drawRect(&painter, &noPenRect),
                    "NoPen rectangle is a no-op that reports success");
        expect_true(XImage_pixel(&image, 1, 0) == 0xff000000u &&
                    XImage_pixel(&image, 5, 1) == 0xff000000u,
                    "NoPen rectangle keeps raster untouched");
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

static void test_painter_picture_penstyle_replay_contract(void)
{
    XPicture picture;
    XImage target;
    XPainter painter;

    XPicture_init(&picture, -1);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_picture(&painter, &picture),
                "penstyle record starts");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);
    expect_true(XPainter_drawLine(&painter, 0, 0, 9, 0),
                "dashed line recorded");
    expect_true(XPainter_end(&painter), "penstyle record ends");
    expect_true(XPicture_isValidStream(&picture),
                "dashed stream valid");

    XImage_init_ex(&target, 12, 2, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target),
                "penstyle replay starts");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenStyle(&painter, XPainterPenStyle_DashLine);
    expect_true(XPicture_play(&picture, &painter),
                "dashed picture replays");
    expect_true(XImage_pixel(&target, 0, 0) == 0xffff0000u &&
                XImage_pixel(&target, 4, 0) == 0xffff0000u &&
                XImage_pixel(&target, 7, 0) == 0xffff0000u &&
                XImage_pixel(&target, 9, 0) == 0xffff0000u,
                "dash replay paints exact dash segments");
    expect_true(XImage_pixel(&target, 5, 0) == 0xff000000u &&
                XImage_pixel(&target, 6, 0) == 0xff000000u,
                "dash replay preserves gaps");
    expect_true(XPainter_penStyle(&painter) == XPainterPenStyle_DashLine,
                "replay restores caller pen style");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XPicture_deinit_base(&picture);
    XImage_deinit_base(&target);
}
#endif /* XPAINTER_PENSTYLE_ON */

#if XPAINTER_BRUSH_ON && XPAINTER_POLYGON_ON
static void test_painter_brush_contract(void)
{
    XImage image;
    XPainter painter;
    XPoint tri[3] = { { 0, 0 }, { 12, 0 }, { 0, 12 } };
    XRect rect = { 0, 0, 12, 12 };
    XPainterGradient grad;
    XPainterBrush brush;

    XImage_init_ex(&image, 16, 16, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "brush painter begins raster");
    XPainter_setPen(&painter, 0xffff0000u);
    XPainter_setPenWidth(&painter, 1);

    /* 纯色画刷：setBrush 后 fillRect_2 / polygon 填充取画刷颜色 */
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_SolidPattern);
    XPainter_setBrush(&painter, 0xff00aa00u);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_SolidPattern,
                "brush style set/get");
    expect_true(XPainter_brushColor(&painter) == 0xff00aa00u,
                "brush color follows setBrush");
#if XPAINTER_BRUSH_ORIGIN_ON
    {
        XPoint origin;
        XPainter_setBrushOrigin(&painter, 1.25f, -2.5f);
        XPainter_brushOrigin(&painter, &origin);
        expect_true(origin.x == 1 && origin.y == -3,
                    "brush origin rounds like QPointF::toPoint");
        expect_true(XPainter_save(&painter), "brush origin save");
        XPainter_setBrushOrigin(&painter, 8.0f, 9.0f);
        expect_true(XPainter_restore(&painter), "brush origin restore");
        XPainter_brushOrigin(&painter, &origin);
        expect_true(origin.x == 1 && origin.y == -3,
                    "brush origin participates in save/restore");
    }
#endif /* XPAINTER_BRUSH_ORIGIN_ON */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_fillRect_2(&painter, &rect),
                "fillRect_2 uses current solid brush");
    expect_true(XImage_pixel(&image, 3, 3) == 0xff00aa00u,
                "fillRect_2 paints current brush color");
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "solid brush triangle fill");
    expect_true(XImage_pixel(&image, 2, 2) == 0xff00aa00u,
                "solid brush polygon interior");
    expect_true(XImage_pixel(&image, 14, 14) == 0xff000000u,
                "solid brush outside blank");

    /* QPainter::drawRect：画刷先填充内部，画笔随后覆盖边框。 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    rect.x = 1; rect.y = 1; rect.width = 6; rect.height = 6;
    expect_true(XPainter_drawRect(&painter, &rect),
                "drawRect fills with current brush and strokes pen");
    expect_true(XImage_pixel(&image, 3, 3) == 0xff00aa00u &&
                XImage_pixel(&image, 1, 1) == 0xffff0000u,
                "drawRect brush interior and pen edge");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 线性渐变色：左白右黑，逐像素渐变 */
    XPainterGradient_initLinear(&grad, 0.0f, 0.0f, 12.0f, 0.0f);
    XPainterGradient_addStop(&grad, 0.0f, 0xffffffffu);
    XPainterGradient_addStop(&grad, 1.0f, 0xff000000u);
    XPainter_setBrushGradient(&painter, &grad);
    expect_true(XPainter_brushStyle(&painter) ==
                XPainterBrushStyle_LinearGradientPattern,
                "gradient brush style set");
    expect_true(XPainter_brushColor(&painter) == 0xff000000u,
                "gradient brush color follows Qt gradient default");
    XPainter_brush(&painter, &brush);
    expect_true(brush.m_style == XPainterBrushStyle_LinearGradientPattern &&
                brush.m_gradient.m_stopCount == 2 &&
                brush.m_gradient.m_stops[0].m_color == 0xffffffffu &&
                brush.m_gradient.m_stops[1].m_color == 0xff000000u,
                "XPainter_brush returns gradient description");

    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_fillRect_2(&painter, &rect),
                "fillRect_2 uses current gradient brush");
    expect_true(((XImage_pixel(&image, 1, 1) >> 16) & 255u) >
                ((XImage_pixel(&image, 6, 1) >> 16) & 255u),
                "fillRect_2 gradient varies across rectangle");
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "gradient brush triangle fill");
    {
        uint32_t left  = XImage_pixel(&image, 1, 1);
        uint32_t right = XImage_pixel(&image, 10, 1);
        uint32_t lr = (left  >> 16) & 255u;
        uint32_t rr = (right >> 16) & 255u;
        expect_true(lr > rr && rr < 250u && rr >= 10u,
                    "linear gradient interpolates along x");
    }

    /* 径向渐变（中心白、边缘黑） */
    XPainterGradient_initRadial(&grad, 6.0f, 6.0f, 12.0f, 6.0f, 6.0f);
    XPainterGradient_addStop(&grad, 0.0f, 0xffffffffu);
    XPainterGradient_addStop(&grad, 1.0f, 0xff000000u);
    XPainter_setBrushGradient(&painter, &grad);
    expect_true(XPainter_brushStyle(&painter) ==
                XPainterBrushStyle_RadialGradientPattern,
                "radial gradient preserves brush style");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "radial gradient triangle fill");
    {
        uint32_t center = XImage_pixel(&image, 6, 1); /* 更靠近中心 */
        uint32_t edge   = XImage_pixel(&image, 1, 1);
        uint32_t ir = (center >> 16) & 255u;
        uint32_t rr = (edge   >> 16) & 255u;
        expect_true(ir > rr,
                    "radial gradient brightens toward center");
    }

    /* 锥形渐变：不同角度颜色不同 */
    XPainterGradient_initConical(&grad, 6.0f, 6.0f, 0.0f);
    XPainterGradient_addStop(&grad, 0.0f, 0xffffffffu);
    XPainterGradient_addStop(&grad, 0.5f, 0xffff0000u);
    XPainterGradient_addStop(&grad, 1.0f, 0xff00ff00u);
    XPainter_setBrushGradient(&painter, &grad);
    expect_true(XPainter_brushStyle(&painter) ==
                XPainterBrushStyle_ConicalGradientPattern,
                "conical gradient preserves brush style");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "conical gradient triangle fill");
    {
        uint32_t a = XImage_pixel(&image, 1, 1);
        uint32_t b = XImage_pixel(&image, 10, 1);
        expect_true(a != b, "conical gradient varies with angle");
    }

    /* QGradient::setColorAt 语义：停止点按位置排序，同位置更新，越界忽略。 */
    XPainterGradient_initLinear(&grad, 0.0f, 0.0f, 1.0f, 0.0f);
    XPainterGradient_addStop(&grad, 0.8f, 0xff0000ffu);
    XPainterGradient_addStop(&grad, -0.1f, 0xffff0000u);
    XPainterGradient_addStop(&grad, 0.2f, 0xff00ff00u);
    XPainterGradient_addStop(&grad, 0.8f, 0xffffffffu);
    expect_true(grad.m_stopCount == 2 &&
                grad.m_stops[0].m_position == 0.2f &&
                grad.m_stops[1].m_position == 0.8f &&
                grad.m_stops[1].m_color == 0xffffffffu,
                "gradient stops are sorted, replaced, and range checked");

    /* 空渐变指针按 QBrush(QColor) 语义恢复纯色画刷，并且不崩溃。 */
    XPainter_setBrushGradient(&painter, NULL);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_SolidPattern,
                "null gradient restores solid brush");

    /* 无画刷：轮廓多边形不填充内部 */
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_NoBrush);
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_fillRect_2(&painter, &rect),
                "fillRect_2 with NoBrush is a successful no-op");
    expect_true(XImage_pixel(&image, 3, 3) == 0xff000000u,
                "NoBrush fillRect_2 leaves interior empty");
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven),
                "NoBrush polygon outline only");
    expect_true(XImage_pixel(&image, 3, 3) == 0xff000000u &&
                XImage_pixel(&image, 2, 0) == 0xffff0000u,
                "NoBrush leaves polygon interior empty");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPolygon(&painter, tri, 3,
                                     XPainterFillRule_OddEven) &&
                XImage_pixel(&image, 3, 3) == 0xff000000u,
                "NoBrush filled request keeps interior empty");

    XPainter_setBrush_2(&painter, XPainterBrushStyle_SolidPattern);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_SolidPattern &&
                XPainter_brushColor(&painter) == 0xff000000u,
                "setBrush style overload uses black solid brush");
    XPainter_setBrush_2(&painter, XPainterBrushStyle_NoBrush);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_NoBrush &&
                XPainter_brushColor(&painter) == 0xff000000u,
                "setBrush NoBrush overload clears fill and keeps black color");
    XPainter_setBrushGradient(&painter, &grad);
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_SolidPattern);
    XPainter_brush(&painter, &brush);
    expect_true(brush.m_style == XPainterBrushStyle_SolidPattern &&
                brush.m_gradient.m_stopCount == 0,
                "setBrushStyle drops gradient payload like QBrush::setStyle");
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_SolidPattern);
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_LinearGradientPattern);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_SolidPattern,
                "setBrushStyle rejects gradient pattern like QBrush::setStyle");
    XPainter_setBrush_2(&painter, XPainterBrushStyle_LinearGradientPattern);
    expect_true(XPainter_brushStyle(&painter) == XPainterBrushStyle_NoBrush,
                "setBrush gradient-style overload creates NoBrush");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_BRUSH_ON && XPAINTER_POLYGON_ON */

#if XPAINTER_TEXTLAYOUT_ON
static void test_painter_text_layout_contract(void)
{
    XImage image;
    XPainter painter;
    XRect lineRect  = { 0, 0, 40, 16 };
    XRect multiRect = { 0, 0, 40, 32 };
    XRect vRect     = { 0, 0, 40, 48 };
    XRect wrapRect  = { 0, 0, 16, 48 };

    XImage_init_ex(&image, 40, 48, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "text layout painter begins raster");
    XPainter_setPen(&painter, 0xffffffffu);
    XPainter_setPenWidth(&painter, 1);

    /* 左/顶对齐单行 */
    expect_true(XPainter_drawTextRect(&painter, &lineRect,
                                      XPAINTER_TEXT_ALIGN_LEFT |
                                      XPAINTER_TEXT_ALIGN_TOP,
                                      "AA", 0xffffffffu),
                "drawTextRect single line top/left");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu,
                "text rect first glyph starts at column 2");
    expect_true(XImage_pixel(&image, 1, 0) == 0xff000000u,
                "text rect left margin blank");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffffffffu,
                "text rect second glyph starts at column 10");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 水平居中 */
    expect_true(XPainter_drawTextRect(&painter, &lineRect,
                                      XPAINTER_TEXT_ALIGN_HCENTER,
                                      "AA", 0xffffffffu),
                "drawTextRect horizontal center");
    expect_true(XImage_pixel(&image, 14, 0) == 0xffffffffu,
                "centered text first glyph near column 14");
    expect_true(XImage_pixel(&image, 10, 0) == 0xff000000u,
                "centered text left margin blank");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 多行：两行按 16px 行高逐行下移 */
    expect_true(XPainter_drawTextRect(&painter, &multiRect,
                                      XPAINTER_TEXT_ALIGN_LEFT |
                                      XPAINTER_TEXT_ALIGN_TOP,
                                      "A\nA", 0xffffffffu),
                "drawTextRect two lines");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu,
                "multiline first line glyph");
    expect_true(XImage_pixel(&image, 2, 16) == 0xffffffffu,
                "multiline second line glyph");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 垂直居中与底对齐 */
    expect_true(XPainter_drawTextRect(&painter, &vRect,
                                      XPAINTER_TEXT_ALIGN_VCENTER,
                                      "A", 0xffffffffu),
                "drawTextRect vertical center");
    expect_true(XImage_pixel(&image, 2, 16) == 0xffffffffu,
                "vertically centered text glyph row 16");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &vRect,
                                      XPAINTER_TEXT_ALIGN_BOTTOM,
                                      "A", 0xffffffffu),
                "drawTextRect bottom align");
    expect_true(XImage_pixel(&image, 2, 32) == 0xffffffffu,
                "bottom aligned text glyph row 32");

    XImage_fillRect(&image, NULL, 0xff000000u);

    /* 按词换行：两行像素都出现 */
    expect_true(XPainter_drawTextRect(&painter, &wrapRect,
                                      XPAINTER_TEXT_WORD_WRAP,
                                      "AB CD", 0xffffffffu),
                "drawTextRect word wrap runs");
    {
        int topHas = 0, bottomHas = 0;
        int row, col;
        for (row = 0; row < 16; ++row)
            for (col = 0; col < 16; ++col)
                if (XImage_pixel(&image, col, row) == 0xffffffffu) topHas = 1;
        for (row = 32; row < 48; ++row)
            for (col = 0; col < 16; ++col)
                if (XImage_pixel(&image, col, row) == 0xffffffffu) bottomHas = 1;
        expect_true(topHas && bottomHas,
                    "word wrap produces two visual lines");
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_TEXTLAYOUT_ON */

#if XPAINTER_PATH_ON
static void test_painter_path_contract(void)
{
    XImage image;
    XPainter painter;
    XPainterPath path;
    XRect small = { 0, 0, 24, 30 };

    XImage_init_ex(&image, 40, 40, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "path painter begins raster");
    XPainter_setPenWidth(&painter, 1);
    XPainter_setPen(&painter, 0xffffffffu);
    XPainter_setBrush(&painter, 0xffff0000u);

    /* 多边形路径：闭合子路径填充 + 描边 */
    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 2.0f),
                "path moveTo");
    expect_true(XPainterPath_lineTo(&path, 10.0f, 2.0f),
                "path lineTo");
    expect_true(XPainterPath_lineTo(&path, 10.0f, 10.0f),
                "path lineTo 2");
    expect_true(XPainterPath_lineTo(&path, 2.0f, 10.0f),
                "path lineTo 3");
    expect_true(XPainterPath_closeSubpath(&path),
                "path closeSubpath");
    expect_true(XPainterPath_elementCount(&path) == 5,
                "path element count");

    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPath(&painter, &path),
                "drawPath polygon fill+stroke");
    expect_true(XImage_pixel(&image, 3, 3) == 0xffff0000u,
                "drawPath interior brush");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffffffffu,
                "drawPath edge pen");
    expect_true(XImage_pixel(&image, 12, 2) == 0xff000000u,
                "drawPath outside empty");

    /* fillPath 只填充内部 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_fillPath(&painter, &path),
                "fillPath fills interior");
    expect_true(XImage_pixel(&image, 3, 3) == 0xffff0000u,
                "fillPath interior brush");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffff0000u,
                "fillPath edge also filled");

    /* strokePath 只描边：NoBrush 下内部保持黑色 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_strokePath(&painter, &path),
                "strokePath draws outline");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffffffffu,
                "strokePath edge pen");
#if XPAINTER_BRUSH_ON
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_NoBrush);
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_strokePath(&painter, &path),
                "strokePath no brush outline");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffffffffu &&
                XImage_pixel(&image, 3, 3) == 0xff000000u,
                "strokePath no interior fill with NoBrush");
    XPainter_setBrushStyle(&painter, XPainterBrushStyle_SolidPattern);
#endif /* XPAINTER_BRUSH_ON */

    /* quadTo：折线展平后控制点附近应出现曲线像素 */
    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 20.0f),
                "quad path moveTo");
    expect_true(XPainterPath_quadTo(&path, 10.0f, 4.0f, 18.0f, 20.0f),
                "quad path quadTo");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_strokePath(&painter, &path),
                "strokePath quad");
    {
        int hit = 0, r, c;
        for (r = 8; r <= 16; ++r)
            for (c = 7; c <= 13; ++c)
                if (XImage_pixel(&image, c, r) == 0xffffffffu)
                    hit = 1;
        expect_true(hit, "quad flatten approaches middle");
    }

    /* cubicTo：三次贝塞尔展平后中间段出现曲线像素 */
    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 30.0f),
                "cubic path moveTo");
    expect_true(XPainterPath_cubicTo(&path, 8.0f, 22.0f, 14.0f, 22.0f,
                                     18.0f, 30.0f),
                "cubic path cubicTo");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_strokePath(&painter, &path),
                "strokePath cubic");
    {
        int hit = 0, r, c;
        for (r = 20; r <= 26; ++r)
            for (c = 9; c <= 13; ++c)
                if (XImage_pixel(&image, c, r) == 0xffffffffu)
                    hit = 1;
        expect_true(hit, "cubic flatten approaches middle");
    }

    /* addRect/addEllipse 便捷构造 */
    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_addRect(&path, &small),
                "path addRect");
    expect_true(XPainterPath_elementCount(&path) == 5,
                "addRect element count");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawPath(&painter, &path),
                "drawPath addRect");
    expect_true(XImage_pixel(&image, 3, 5) == 0xffff0000u,
                "addRect interior filled");

    /* Qt moveTo() 隐式结束前一子路径；两个不相连矩形之间不得出现连接填充。 */
    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 2.0f),
                "multi-subpath first moveTo");
    expect_true(XPainterPath_lineTo(&path, 6.0f, 2.0f),
                "multi-subpath first lineTo");
    expect_true(XPainterPath_lineTo(&path, 6.0f, 6.0f),
                "multi-subpath first lineTo 2");
    expect_true(XPainterPath_lineTo(&path, 2.0f, 6.0f),
                "multi-subpath first lineTo 3");
    expect_true(XPainterPath_closeSubpath(&path),
                "multi-subpath first close");
    expect_true(XPainterPath_moveTo(&path, 14.0f, 2.0f),
                "multi-subpath second moveTo");
    expect_true(XPainterPath_lineTo(&path, 18.0f, 2.0f),
                "multi-subpath second lineTo");
    expect_true(XPainterPath_lineTo(&path, 18.0f, 6.0f),
                "multi-subpath second lineTo 2");
    expect_true(XPainterPath_lineTo(&path, 14.0f, 6.0f),
                "multi-subpath second lineTo 3");
    expect_true(XPainterPath_closeSubpath(&path),
                "multi-subpath second close");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_fillPath(&painter, &path),
                "fillPath keeps subpaths separate");
    expect_true(XImage_pixel(&image, 3, 3) == 0xffff0000u &&
                XImage_pixel(&image, 15, 3) == 0xffff0000u,
                "multi-subpath interiors filled");
    expect_true(XImage_pixel(&image, 10, 3) == 0xff000000u,
                "multi-subpath gap remains empty");

    /* 空路径与重复元素的 QPainterPath 边界行为。 */
    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_closeSubpath(&path),
                "empty closeSubpath is a no-op");
    expect_true(XPainterPath_lineTo(&path, 3.0f, 3.0f),
                "empty lineTo creates origin");
    expect_true(XPainterPath_elementCount(&path) == 2,
                "empty lineTo adds implicit origin MoveTo");
    expect_true(XPainterPath_moveTo(&path, 5.0f, 5.0f) &&
                XPainterPath_moveTo(&path, 6.0f, 6.0f),
                "consecutive moveTo succeeds");
    expect_true(XPainterPath_elementCount(&path) == 3,
                "consecutive moveTo replaces prior MoveTo");
    expect_true(XPainterPath_lineTo(&path, 6.0f, 6.0f),
                "duplicate lineTo is a no-op");
    expect_true(XPainterPath_elementCount(&path) == 3,
                "duplicate lineTo does not append element");
    expect_true(XPainterPath_closeSubpath(&path),
                "closeSubpath marks a new subpath boundary");
    expect_true(XPainterPath_lineTo(&path, 9.0f, 6.0f),
                "lineTo after closeSubpath succeeds");
    expect_true(XPainterPath_elementCount(&path) == 5,
                "lineTo after closeSubpath adds implicit MoveTo");
    {
        float currentX = 0.0f;
        float currentY = 0.0f;
        XPainterPath_currentPosition(&path, &currentX, &currentY);
        expect_true(currentX == 9.0f && currentY == 6.0f,
                    "lineTo after closeSubpath updates current position");
    }

    XPainterPath_deinit(&path);
    XPainterPath_init(&path);
    expect_true(XPainterPath_addEllipse(&path, &small),
                "path addEllipse");
    expect_true(XPainterPath_elementCount(&path) >= 64,
                "addEllipse flattens to many elements");

    {
        XRect degenerateEllipse = { 4, 5, -12, 0 };
        XPainterPath_deinit(&path);
        XPainterPath_init(&path);
        expect_true(XPainterPath_addEllipse(&path, &degenerateEllipse),
                    "addEllipse accepts negative and zero-axis dimensions");
        expect_true(XPainterPath_elementCount(&path) > 1,
                    "degenerate ellipse still creates a subpath");
        degenerateEllipse.width = 0;
        degenerateEllipse.height = 0;
        expect_true(XPainterPath_addEllipse(&path, &degenerateEllipse),
                    "null ellipse is a no-op");
    }

    XPainterPath_deinit(&path);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

typedef struct PathCapture
{
    int m_calls;                 /**< 回调调用次数，用于契约断言。 */
    XPainterPathOp m_lastOp;     /**< 最近一次命令枚举。 */
    int m_elementCount;          /**< 最近一次回调收到的元素数。 */
} PathCapture;

static bool test_path_capture_proc(XPainter* painter, XPainterPathOp op,
                                   const struct XPainterPath* path)
{
    PathCapture* cap = (PathCapture*)painter->m_userData;
    if (!cap) return true;
    ++cap->m_calls;
    cap->m_lastOp = op;
    if (path) cap->m_elementCount = path->m_elementCount;
    return true;
}

static void test_painter_path_callback_contract(void)
{
    XImage image;
    XPainter painter;
    XPainterPath path;
    PathCapture cap;

    XImage_init_ex(&image, 24, 14, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    memset(&cap, 0, sizeof(cap));
    XPainter_init(&painter, &cap);
    expect_true(XPainter_begin_image(&painter, &image),
                "path callback painter begins raster");
    painter.m_drawPath = test_path_capture_proc;

    XPainterPath_init(&path);
    expect_true(XPainterPath_moveTo(&path, 2.0f, 2.0f),
                "callback path moveTo");
    expect_true(XPainterPath_lineTo(&path, 10.0f, 2.0f),
                "callback path lineTo");
    expect_true(XPainterPath_lineTo(&path, 10.0f, 10.0f),
                "callback path lineTo 2");
    expect_true(XPainterPath_lineTo(&path, 2.0f, 10.0f),
                "callback path lineTo 3");
    expect_true(XPainterPath_closeSubpath(&path),
                "callback path closeSubpath");

    expect_true(XPainter_drawPath(&painter, &path),
                "path callback drawPath");
    expect_true(cap.m_calls == 1 && cap.m_lastOp == XPainterPathOp_Draw &&
                cap.m_elementCount == 5,
                "path callback draw op and count");

    memset(&cap, 0, sizeof(cap));
    expect_true(XPainter_fillPath(&painter, &path),
                "path callback fillPath");
    expect_true(cap.m_calls == 1 && cap.m_lastOp == XPainterPathOp_Fill &&
                cap.m_elementCount == 5,
                "path callback fill op and count");

    memset(&cap, 0, sizeof(cap));
    expect_true(XPainter_strokePath(&painter, &path),
                "path callback strokePath");
    expect_true(cap.m_calls == 1 && cap.m_lastOp == XPainterPathOp_Stroke &&
                cap.m_elementCount == 5,
                "path callback stroke op and count");

    memset(&cap, 0, sizeof(cap));
    expect_true(XPainter_drawPath(&painter, NULL),
                "path callback NULL is no-op");
    expect_true(XPainter_fillPath(&painter, NULL),
                "path fill callback NULL is no-op");
    expect_true(XPainter_strokePath(&painter, NULL),
                "path stroke callback NULL is no-op");
    expect_true(cap.m_calls == 0,
                "NULL path does not dispatch callback");

    memset(&cap, 0, sizeof(cap));
    {
        XPainterPath empty;
        XPainterPath_init(&empty);
        expect_true(XPainter_drawPath(&painter, &empty),
                    "empty path callback no-op");
        expect_true(XPainter_fillPath(&painter, &empty),
                    "empty fill path callback no-op");
        expect_true(XPainter_strokePath(&painter, &empty),
                    "empty stroke path callback no-op");
        XPainterPath_deinit(&empty);
    }
    expect_true(cap.m_calls == 0,
                "empty path does not dispatch callback");

    XPainter_end(&painter);
    expect_true(painter.m_drawPath == NULL,
                "path callback cleared at end");
    XPainterPath_deinit(&path);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_PATH_ON */

static void test_painter_transform_contract(void)
{
    XImage image;
    XImage tile;
    XPainter painter;
    XImageTransform m;
    XRect one = { 0, 0, 1, 1 };

    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "transform painter begins raster");

    /* 初始恒等 */
#if XPAINTER_WORLD_MATRIX_ON
    expect_true(!XPainter_worldMatrixEnabled(&painter),
                "initial world matrix is disabled");
#endif /* XPAINTER_WORLD_MATRIX_ON */
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.m11 - 1.0f) < 1.0e-4f &&
                fabsf(m.m22 - 1.0f) < 1.0e-4f &&
                fabsf(m.m33 - 1.0f) < 1.0e-4f &&
                fabsf(m.dx) < 1.0e-4f && fabsf(m.dy) < 1.0e-4f,
                "initial transform identity");

#if XPAINTER_VIEW_TRANSFORM_ON
    {
        XRect window = { 1, 1, 2, 2 };
        XRect viewport = { 3, 3, 4, 4 };
        XRect result;

        XPainter_window(&painter, &result);
        expect_true(result.x == 0 && result.y == 0 &&
                    result.width == 8 && result.height == 8,
                    "image painter defaults window to device rect");
        XPainter_viewport(&painter, &result);
        expect_true(result.x == 0 && result.y == 0 &&
                    result.width == 8 && result.height == 8,
                    "image painter defaults viewport to device rect");
        expect_true(!XPainter_viewTransformEnabled(&painter),
                    "initial view transform is disabled");

        XPainter_setWindow(&painter, &window);
        XPainter_setViewport(&painter, &viewport);
        expect_true(XPainter_viewTransformEnabled(&painter),
                    "setting window and viewport enables view transform");
        XPainter_combinedTransform(&painter, &m);
        expect_true(fabsf(m.m11 - 2.0f) < 1.0e-4f &&
                    fabsf(m.m22 - 2.0f) < 1.0e-4f &&
                    fabsf(m.dx - 1.0f) < 1.0e-4f &&
                    fabsf(m.dy - 1.0f) < 1.0e-4f,
                    "combined transform includes window viewport scale and offset");
        XPainter_deviceTransform(&painter, &m);
        expect_true(fabsf(m.m11 - 2.0f) < 1.0e-4f &&
                    fabsf(m.m22 - 2.0f) < 1.0e-4f &&
                    fabsf(m.dx - 1.0f) < 1.0e-4f &&
                    fabsf(m.dy - 1.0f) < 1.0e-4f,
                    "device transform includes window viewport mapping");
        XPainter_setPen(&painter, 0xffffffffu);
        expect_true(XPainter_drawPoint(&painter, 2, 2),
                    "view transform draws logical point");
        expect_true(XImage_pixel(&image, 5, 5) == 0xffffffffu &&
                    XImage_pixel(&image, 2, 2) == 0xff000000u,
                    "window viewport maps logical point to device point");
        XPainter_transform(&painter, &m);
        expect_true(fabsf(m.m11 - 1.0f) < 1.0e-4f &&
                    fabsf(m.m22 - 1.0f) < 1.0e-4f &&
                    fabsf(m.dx) < 1.0e-4f && fabsf(m.dy) < 1.0e-4f,
                    "world transform excludes view transform");

        XPainter_setViewTransformEnabled(&painter, false);
        expect_true(!XPainter_viewTransformEnabled(&painter),
                    "view transform can be disabled");
        XPainter_window(&painter, &result);
        expect_true(result.x == 1 && result.y == 1 &&
                    result.width == 2 && result.height == 2,
                    "disabled view transform preserves window");
        XPainter_setPen(&painter, 0xffff0000u);
        expect_true(XPainter_drawPoint(&painter, 1, 1) &&
                    XImage_pixel(&image, 1, 1) == 0xffff0000u,
                    "disabled view transform draws in logical device coordinates");
        expect_true(XPainter_save(&painter),
                    "save keeps disabled view transform state");
        XPainter_setViewTransformEnabled(&painter, true);
        expect_true(XPainter_viewTransformEnabled(&painter),
                    "view transform re-enables after save");
        expect_true(XPainter_restore(&painter) &&
                    !XPainter_viewTransformEnabled(&painter),
                    "restore recovers disabled view transform state");
        XPainter_resetTransform(&painter);
        XPainter_window(&painter, &result);
        expect_true(result.x == 0 && result.y == 0 &&
                    result.width == 8 && result.height == 8 &&
                    !XPainter_viewTransformEnabled(&painter),
                    "reset transform restores default window and disables view transform");
        XPainter_viewport(&painter, &result);
        expect_true(result.x == 0 && result.y == 0 &&
                    result.width == 8 && result.height == 8,
                    "reset transform restores default viewport");
    }
#endif /* XPAINTER_VIEW_TRANSFORM_ON */

    /* 平移 */
    XPainter_translate(&painter, 5.0f, 7.0f);
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.dx - 5.0f) < 1.0e-4f &&
                fabsf(m.dy - 7.0f) < 1.0e-4f,
                "translate stores world offset");

#if XPAINTER_WORLD_MATRIX_ON
    expect_true(XPainter_worldMatrixEnabled(&painter),
                "translate enables world matrix");
    XPainter_setWorldMatrixEnabled(&painter, false);
    expect_true(!XPainter_worldMatrixEnabled(&painter),
                "world matrix can be disabled");
    XPainter_worldTransform(&painter, &m);
    expect_true(fabsf(m.dx - 5.0f) < 1.0e-4f &&
                fabsf(m.dy - 7.0f) < 1.0e-4f,
                "disabled world matrix retains transform");
    XPainter_setPen(&painter, 0xffffffffu);
    expect_true(XPainter_drawPoint(&painter, 1, 0),
                "disabled world matrix draws point");
    expect_true(XImage_pixel(&image, 1, 0) == 0xffffffffu &&
                XImage_pixel(&image, 6, 7) == 0xff000000u,
                "disabled world matrix leaves line in logical position");
    expect_true(XPainter_fillRect(&painter, &one, 0xffff0000u),
                "disabled world matrix fills rect");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffff0000u &&
                XImage_pixel(&image, 5, 7) == 0xff000000u,
                "disabled world matrix leaves fill in logical position");
    XImage_init_ex(&tile, 1, 1, XImageFormat_ARGB32);
    XImage_fillRect(&tile, NULL, 0xff0000ffu);
    expect_true(XPainter_drawImage(&painter, &tile, 2, 0),
                "disabled world matrix draws image");
    expect_true(XImage_pixel(&image, 2, 0) == 0xff0000ffu &&
                XImage_pixel(&image, 7, 7) == 0xff000000u,
                "disabled world matrix leaves image in logical position");
    XImage_deinit_base(&tile);
    expect_true(XPainter_save(&painter),
                "save keeps disabled world matrix state");
    XPainter_setWorldMatrixEnabled(&painter, true);
    expect_true(XPainter_worldMatrixEnabled(&painter),
                "world matrix re-enables after save");
    expect_true(XPainter_restore(&painter) &&
                !XPainter_worldMatrixEnabled(&painter),
                "restore recovers disabled world matrix state");
    XPainter_setWorldMatrixEnabled(&painter, true);
#endif /* XPAINTER_WORLD_MATRIX_ON */

    /* 旋转 90 度（顺时针，y 向下） */
    XPainter_resetTransform(&painter);
    XPainter_rotate(&painter, 90.0f);
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.m11) < 1.0e-4f &&
                fabsf(m.m12 - 1.0f) < 1.0e-4f &&
                fabsf(m.m21 + 1.0f) < 1.0e-4f &&
                fabsf(m.m22) < 1.0e-4f,
                "rotate 90 stores clockwise matrix");

    /* 切变 sh=1, sv=0：x'=x+y */
    XPainter_resetTransform(&painter);
    XPainter_shear(&painter, 1.0f, 0.0f);
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.m11 - 1.0f) < 1.0e-4f &&
                fabsf(m.m21 - 1.0f) < 1.0e-4f &&
                fabsf(m.m12) < 1.0e-4f &&
                fabsf(m.m22 - 1.0f) < 1.0e-4f,
                "shear stores x by y matrix");

    /* QTransform::translate/scale 均将新矩阵左乘到当前矩阵。 */
    memset(&m, 0, sizeof(m));
    m.m11 = 2.0f;
    m.m22 = 3.0f;
    m.dx = 1.0f;
    m.dy = 2.0f;
    m.m33 = 1.0f;
    XPainter_resetTransform(&painter);
    XPainter_setWorldTransform(&painter, &m, false);
    XPainter_translate(&painter, 4.0f, 5.0f);
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.m11 - 2.0f) < 1.0e-4f &&
                fabsf(m.m22 - 3.0f) < 1.0e-4f &&
                fabsf(m.dx - 9.0f) < 1.0e-4f &&
                fabsf(m.dy - 17.0f) < 1.0e-4f,
                "translate left-multiplies a scaled world matrix");
    XPainter_resetTransform(&painter);
    XPainter_translate(&painter, 1.0f, 2.0f);
    XPainter_scale(&painter, 2.0f, 3.0f);
    XPainter_transform(&painter, &m);
    expect_true(fabsf(m.m11 - 2.0f) < 1.0e-4f &&
                fabsf(m.m22 - 3.0f) < 1.0e-4f &&
                fabsf(m.dx - 1.0f) < 1.0e-4f &&
                fabsf(m.dy - 2.0f) < 1.0e-4f,
                "scale left-multiplies without scaling translation");

    /* worldTransform/setWorldTransform 替换与组合语义 */
    XPainter_resetTransform(&painter);
    XPainter_worldTransform(&painter, &m);
    expect_true(fabsf(m.m11 - 1.0f) < 1.0e-4f &&
                fabsf(m.m22 - 1.0f) < 1.0e-4f &&
                fabsf(m.m33 - 1.0f) < 1.0e-4f,
                "resetTransform restores identity world transform");
    memset(&m, 0, sizeof(m));
    m.m11 = 2.0f;
    m.m22 = 3.0f;
    m.dx = 1.0f;
    m.dy = 2.0f;
    m.m33 = 1.0f;
    XPainter_setWorldTransform(&painter, &m, false);
    XPainter_worldTransform(&painter, &m);
    expect_true(m.m11 == 2.0f && m.m22 == 3.0f && m.dx == 1.0f,
                "worldTransform reads matrix");
    memset(&m, 0, sizeof(m));
    m.m11 = 1.0f;
    m.m22 = 1.0f;
    m.m33 = 1.0f;
    m.dx = 1.0f;
    XPainter_setWorldTransform(&painter, &m, true);
    XPainter_worldTransform(&painter, &m);
    expect_true(fabsf(m.m11 - 2.0f) < 1.0e-4f &&
                fabsf(m.m22 - 3.0f) < 1.0e-4f &&
                fabsf(m.dx - 3.0f) < 1.0e-4f &&
                fabsf(m.dy - 2.0f) < 1.0e-4f,
                "setWorldTransform combine prepends the new matrix");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

#if XPAINTER_TEXTLAYOUT_ON
static void test_painter_text_flags_contract(void)
{
    XImage image;
    XPainter painter;
    XRect wide = { 0, 0, 80, 24 };
    XRect narrow = { 0, 0, 8, 16 };
    XRect wrapProbe = { 0, 0, 8, 32 };
    XRect skipRect = { 0, 0, 40, 24 };
    XRect directionRect = { 0, 0, 40, 16 };
    int hasPix = 0;

    XImage_init_ex(&image, 80, 48, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff000000u);
    XPainter_init(&painter, NULL);
#if XPAINTER_RENDERHINT_ON
    expect_true(XPainter_renderHints(&painter) == 0u,
                "renderHints is empty while painter is inactive");
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    expect_true(XPainter_renderHints(&painter) == 0u,
                "setRenderHint is ignored while painter is inactive");
#endif /* XPAINTER_RENDERHINT_ON */
    expect_true(XPainter_begin_image(&painter, &image),
                "text flags painter begins raster");
#if XPAINTER_RENDERHINT_ON
    expect_true(XPainter_renderHints(&painter) ==
                XPainterRenderHint_TextAntialiasing,
                "render hints default to TextAntialiasing");
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    expect_true(XPainter_testRenderHint(&painter,
                                        XPainterRenderHint_Antialiasing),
                "setRenderHint enables Antialiasing");
    XPainter_setRenderHints(&painter,
                            XPainterRenderHint_Antialiasing |
                            XPainterRenderHint_TextAntialiasing,
                            false);
    expect_true(!XPainter_testRenderHint(&painter,
                                         XPainterRenderHint_Antialiasing) &&
                !XPainter_testRenderHint(&painter,
                                         XPainterRenderHint_TextAntialiasing),
                "setRenderHints clears requested bits");
    XPainter_setRenderHint(&painter, XPainterRenderHint_TextAntialiasing,
                           true);
#endif /* XPAINTER_RENDERHINT_ON */
    XPainter_setPen(&painter, 0xffffffffu);
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, 0xffffffffu);

    /* DontPrint：不绘制任何像素 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &skipRect,
                                      XPAINTER_TEXT_DONT_PRINT,
                                      "AAA", 0xffffffffu),
                "drawTextRect DontPrint returns true");
    hasPix = 0;
    {
        int r, c;
        for (r = 0; r < 24; ++r)
            for (c = 0; c < 40; ++c)
                if (XImage_pixel(&image, c, r) == 0xffffffffu)
                    hasPix = 1;
    }
    expect_true(!hasPix, "DontPrint draws nothing");

    /* SingleLine：换行视为空格，不拆行 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &skipRect,
                                      XPAINTER_TEXT_SINGLE_LINE,
                                      "A\nBC", 0xffffffffu),
                "drawTextRect SingleLine");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 18, 0) == 0xffffffffu &&
                XImage_pixel(&image, 26, 0) == 0xffffffffu,
                "SingleLine renders one line with newline as space");
    hasPix = 0;
    {
        int r, c;
        for (r = 16; r < 24; ++r)
            for (c = 0; c < 40; ++c)
                if (XImage_pixel(&image, c, r) == 0xffffffffu)
                    hasPix = 1;
    }
    expect_true(!hasPix, "SingleLine keeps second row empty");

    /* ExpandTabs：制表符推进到第 8 个字符位 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &wide,
                                      XPAINTER_TEXT_EXPAND_TABS,
                                      "A\tB", 0xffffffffu),
                "drawTextRect ExpandTabs");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 66, 0) == 0xffffffffu,
                "ExpandTabs places B at tab stop");

    /* Justify：把行内额外宽度分配到空格 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &skipRect,
                                      XPAINTER_TEXT_ALIGN_JUSTIFY |
                                      XPAINTER_TEXT_JUSTIFICATION_FORCED,
                                      "A B", 0xffffffffu),
                "drawTextRect Justify");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 34, 0) == 0xffffffffu,
                "Justify stretches space to right edge");

    /* 默认裁剪到矩形；关闭裁剪能力时两种标志都允许越界绘制。 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &narrow,
                                      XPAINTER_TEXT_SINGLE_LINE,
                                      "AA", 0xffffffffu),
                "drawTextRect default clip");
#if XPAINTER_CLIP_ON
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 10, 0) == 0xff000000u,
                "default clips second glyph to rect");
#else
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 10, 0) == 0xffffffffu,
                "clip-disabled build lets second glyph overflow");
#endif /* XPAINTER_CLIP_ON */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &narrow,
                                      XPAINTER_TEXT_SINGLE_LINE |
                                      XPAINTER_TEXT_DONT_CLIP,
                                      "AA", 0xffffffffu),
                "drawTextRect DontClip");
    expect_true(XImage_pixel(&image, 10, 0) == 0xffffffffu,
                "DontClip lets second glyph overflow");

    /* Qt 默认不换行：窄矩形只裁剪同一行的溢出字形；显式
       TextWrapAnywhere 才把每个字形分到下一行。 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &wrapProbe, 0u,
                                      "AA", 0xffffffffu),
                "drawTextRect keeps one line without wrap flags");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 2, 16) == 0xff000000u,
                "default text layout does not wrap at rectangle width");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &wrapProbe,
                                      XPAINTER_TEXT_WRAP_ANYWHERE,
                                      "AA", 0xffffffffu),
                "drawTextRect wrap-anywhere succeeds");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 2, 16) == 0xffffffffu,
                "wrap-anywhere creates a second visual line");

    /* ShowMnemonic：& 转义下一字符并加下划线 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &skipRect,
                                      XPAINTER_TEXT_SHOW_MNEMONIC,
                                      "&A", 0xffffffffu),
                "drawTextRect ShowMnemonic");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu,
                "ShowMnemonic renders underlined glyph");
    expect_true(XImage_pixel(&image, 2, 16) == 0xffffffffu,
                "ShowMnemonic underline below baseline");

    /* HideMnemonic：& 转义下一字符但不加下划线 */
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &skipRect,
                                      XPAINTER_TEXT_HIDE_MNEMONIC,
                                      "&A", 0xffffffffu),
                "drawTextRect HideMnemonic");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 2, 16) == 0xff000000u,
                "HideMnemonic renders glyph without underline");

#if XPAINTER_LAYOUT_DIRECTION_ON
    /* 布局方向：RTL 时视觉左对齐翻转为右对齐，AlignAbsolute 保持物理左对齐。 */
    expect_true(XPainter_layoutDirection(&painter) ==
                XPainterLayoutDirection_LeftToRight,
                "active layout direction starts at application default LTR");
    XPainter_setLayoutDirection(&painter,
                                XPainterLayoutDirection_RightToLeft);
    expect_true(XPainter_layoutDirection(&painter) ==
                XPainterLayoutDirection_RightToLeft,
                "layout direction getter returns RTL");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &directionRect,
                                      XPAINTER_TEXT_ALIGN_LEFT,
                                      "AA", 0xffffffffu),
                "RTL layout draw succeeds");
    expect_true(XImage_pixel(&image, 26, 0) == 0xffffffffu &&
                XImage_pixel(&image, 2, 0) == 0xff000000u,
                "RTL flips logical left alignment to right");
    XImage_fillRect(&image, NULL, 0xff000000u);
    expect_true(XPainter_drawTextRect(&painter, &directionRect,
                                      XPAINTER_TEXT_ALIGN_LEFT |
                                      XPAINTER_TEXT_ALIGN_ABSOLUTE,
                                      "AA", 0xffffffffu),
                "absolute alignment draw succeeds");
    expect_true(XImage_pixel(&image, 2, 0) == 0xffffffffu &&
                XImage_pixel(&image, 26, 0) == 0xff000000u,
                "AlignAbsolute keeps physical left alignment");
    XPainter_setLayoutDirection(&painter,
                                XPainterLayoutDirection_LeftToRight);
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}
#endif /* XPAINTER_TEXTLAYOUT_ON */

static void test_painter_record_play_contract(void)
{
    XPicture picture;
    XPicture loaded;
    XPainter painter;
    XImage target;
    XImage source;
    XRect rect = { 1, 1, 4, 4 };

    XPicture_init(&picture, -1);
    XPicture_init(&loaded, -1);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_picture(&painter, &picture),
                "painter begins recording");
    expect_true(!XPainter_begin_picture(&painter, &picture),
                "begin picture on active painter is rejected");
    expect_true(XPainter_device(&painter) == &picture,
                "painter device reports the bound picture");

    /* 先填充后描边，回放时描边覆盖在填充上 */
    expect_true(XPainter_fillRect(&painter, &rect, 0xff00ff00u),
                "record fillRect");
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawRect(&painter, &rect), "record drawRect");
    XImage_init_ex(&source, 2, 1, XImageFormat_ARGB32);
    XImage_fillRect(&source, NULL, 0xff0000ffu);
    expect_true(XPainter_drawImage(&painter, &source, 6, 6),
                "record drawImage");
    expect_true(XPainter_save(&painter) && XPainter_restore(&painter),
                "record save/restore");
    expect_true(XPainter_end(&painter), "recording end");
    expect_true(!XPainter_isActive(&painter), "recording ends inactive");
    expect_true(XPicture_isValidStream(&picture), "recorded stream valid");
    expect_true(XPicture_size(&picture) != 0, "recorded stream has commands");

    /* 文件往返 */
    expect_true(XPicture_save_2(&picture, "xgui_painter.xpic"),
                "recorded picture saves to file");
    expect_true(XPicture_load_2(&loaded, "xgui_painter.xpic") &&
                XPicture_isValidStream(&loaded),
                "recorded picture reloads from file");
    remove("xgui_painter.xpic");

    /* 用软件光栅后端回放（先设置与录制时一致的画笔） */
    XImage_init_ex(&target, 10, 10, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target),
                "painter begins image replay");
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPicture_play(&loaded, &painter), "picture replays");
    expect_true(XImage_pixel(&target, 1, 1) == 0xffff0000u,
                "replay outline corner tl");
    expect_true(XImage_pixel(&target, 5, 5) == 0xffff0000u,
                "replay outline corner br");
    expect_true(XImage_pixel(&target, 1, 2) == 0xffff0000u,
                "replay outline left edge");
    expect_true(XImage_pixel(&target, 2, 2) == 0xff00ff00u,
                "replay interior fill");
    expect_true(XImage_pixel(&target, 6, 1) == 0, "replay outside rect");
    expect_true(XImage_pixel(&target, 6, 6) == 0xff0000ffu,
                "replay recorded image px0");
    expect_true(XImage_pixel(&target, 7, 6) == 0xff0000ffu,
                "replay recorded image px1");
    expect_true(XImage_pixel(&target, 5, 6) == 0, "replay image margin");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&target);
    XImage_deinit_base(&source);
    XPicture_deinit_base(&loaded);
    XPicture_deinit_base(&picture);
}

/** @brief 校验 Picture 记录并回放画笔状态 opcode，避免回放沿用调用方画笔。 */
static void test_painter_picture_pen_state_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "pen state record begins picture");
    XPainter_setPen(&record, 0xff20c060u);
#if XPAINTER_PENSTYLE_ON
    XPainter_setPenWidth(&record, 3);
    XPainter_setPenStyle(&record, XPainterPenStyle_SolidLine);
#endif /* XPAINTER_PENSTYLE_ON */
    expect_true(XPainter_drawLine(&record, 1, 1, 4, 1),
                "pen state record draws line");
    expect_true(XPainter_end(&record), "pen state record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "pen state record stream is valid");

    XImage_init_ex(&target, 8, 4, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "pen state replay begins image");
    XPainter_setPen(&replay, 0xffff0000u);
    expect_true(XPicture_play(&picture, &replay),
                "pen state replay applies recorded pen");
    expect_true(XImage_pixel(&target, 1, 1) == 0xff20c060u &&
                XImage_pixel(&target, 4, 1) == 0xff20c060u,
                "pen state replay uses recorded color");
#if XPAINTER_PENSTYLE_ON
    expect_true(XPainter_penWidth(&replay) == 3,
                "pen state replay uses recorded width");
#endif /* XPAINTER_PENSTYLE_ON */
    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}

/** @brief 校验 Picture 记录并回放不透明度与合成模式状态 opcode。 */
static void test_painter_picture_opacity_composition_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;
    XRect rect = { 1, 1, 3, 3 };

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "opacity/composition record begins picture");
    XPainter_setOpacity(&record, 0.5f);
    XPainter_setCompositionMode(&record, XPainterCompositionMode_Clear);
    expect_true(XPainter_fillRect(&record, &rect, 0xffff0000u),
                "opacity/composition record fills rectangle");
    expect_true(XPainter_end(&record),
                "opacity/composition record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "opacity/composition record stream is valid");

    XImage_init_ex(&target, 6, 6, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xffffffffu);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "opacity/composition replay begins image");
    XPainter_setOpacity(&replay, 1.0f);
    XPainter_setCompositionMode(&replay,
                                XPainterCompositionMode_SourceOver);
    expect_true(XPicture_play(&picture, &replay),
                "opacity/composition picture replays");
    expect_true(XPainter_opacity(&replay) > 0.499f &&
                XPainter_opacity(&replay) < 0.501f,
                "picture replay applies recorded opacity");
    expect_true(XPainter_compositionMode(&replay) ==
                    XPainterCompositionMode_Clear,
                "picture replay applies recorded composition mode");
    expect_true(XImage_pixel(&target, 2, 2) == 0u &&
                XImage_pixel(&target, 0, 0) == 0xffffffffu,
                "picture replay applies recorded clear mode");

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}

/** @brief 校验 Picture 记录并回放背景颜色与背景填充模式。 */
static void test_painter_picture_background_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "background record begins picture");
    XPainter_setBackground(&record, 0xff102030u);
#if XPAINTER_BACKGROUND_ON
    XPainter_setBackgroundMode(&record, XPainterBackgroundMode_Opaque);
#endif /* XPAINTER_BACKGROUND_ON */
    expect_true(XPainter_end(&record), "background record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "background record stream is valid");

    XImage_init_ex(&target, 1, 1, XImageFormat_ARGB32);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "background replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "background picture replays");
    expect_true(XPainter_background(&replay) == 0xff102030u,
                "picture replay restores background color");
#if XPAINTER_BACKGROUND_ON
    expect_true(XPainter_backgroundMode(&replay) ==
                    XPainterBackgroundMode_Opaque,
                "picture replay restores background mode");
#endif /* XPAINTER_BACKGROUND_ON */

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}

#if XPAINTER_RENDERHINT_ON
/** @brief 校验 Picture 记录并回放完整渲染提示掩码。 */
static void test_painter_picture_render_hints_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;
    XPainterRenderHints expected;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "render-hints record begins picture");
    XPainter_setRenderHint(&record, XPainterRenderHint_Antialiasing, true);
    XPainter_setRenderHints(&record,
                            XPainterRenderHint_SmoothPixmapTransform |
                            XPainterRenderHint_LosslessImageRendering,
                            true);
    XPainter_setRenderHint(&record, XPainterRenderHint_Antialiasing, false);
    /* Qt keeps DirtyHints on every setter call, including an unchanged mask. */
    XPainter_setRenderHints(&record,
                            XPainterRenderHint_SmoothPixmapTransform |
                            XPainterRenderHint_LosslessImageRendering,
                            true);
    expect_true(XPainter_end(&record), "render-hints record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "render-hints record stream is valid");
    expect_true(XPicture_size(&picture) ==
                    XPICTURE_HEADER_SIZE +
                    4u * (XPICTURE_RECORD_HEADER_SIZE + 4u),
                "render-hints repeated setter is retained in stream");

    XImage_init_ex(&target, 1, 1, XImageFormat_ARGB32);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "render-hints replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "render-hints picture replays");
    expected = XPainterRenderHint_TextAntialiasing |
               XPainterRenderHint_SmoothPixmapTransform |
               XPainterRenderHint_LosslessImageRendering;
    expect_true(XPainter_renderHints(&replay) == expected,
                "picture replay restores complete render-hints mask");
    expect_true(!XPainter_testRenderHint(&replay,
                                         XPainterRenderHint_Antialiasing),
                "picture replay clears disabled render hint");

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_RENDERHINT_ON */

#if XPAINTER_BRUSH_ORIGIN_ON
/** @brief 校验 Picture 记录并回放画刷原点的两个浮点坐标。 */
static void test_painter_picture_brush_origin_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;
    XPoint origin;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "brush-origin record begins picture");
    XPainter_setBrushOrigin(&record, 1.25f, -2.5f);
    /* Qt marks DirtyBrushOrigin for every call, even when coordinates repeat. */
    XPainter_setBrushOrigin(&record, 1.25f, -2.5f);
    expect_true(XPainter_end(&record), "brush-origin record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "brush-origin record stream is valid");
    expect_true(XPicture_size(&picture) ==
                    XPICTURE_HEADER_SIZE +
                    2u * (XPICTURE_RECORD_HEADER_SIZE + 8u),
                "brush-origin repeated setter is retained in stream");

    XImage_init_ex(&target, 1, 1, XImageFormat_ARGB32);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "brush-origin replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "brush-origin picture replays");
    XPainter_brushOrigin(&replay, &origin);
    expect_true(origin.x == 1 && origin.y == -3,
                "picture replay restores brush-origin coordinates");

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_BRUSH_ORIGIN_ON */

#if XPAINTER_BRUSH_ON
/** @brief 校验 Picture 记录并回放基础画刷样式与颜色。 */
static void test_painter_picture_brush_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "brush record begins picture");
    XPainter_setBrush(&record, 0xff224466u);
    XPainter_setBrushStyle(&record, XPainterBrushStyle_Dense4Pattern);
    expect_true(XPainter_end(&record), "brush record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "brush record stream is valid");
    expect_true(XPicture_size(&picture) ==
                    XPICTURE_HEADER_SIZE +
                    2u * (XPICTURE_RECORD_HEADER_SIZE + 8u),
                "brush setters produce fixed-size records");

    XImage_init_ex(&target, 1, 1, XImageFormat_ARGB32);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "brush replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "brush picture replays");
    expect_true(XPainter_brushColor(&replay) == 0xff224466u,
                "picture replay restores brush color");
    expect_true(XPainter_brushStyle(&replay) ==
                    XPainterBrushStyle_Dense4Pattern,
                "picture replay restores brush style");

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_BRUSH_ON */

/** @brief 校验 Picture 记录并回放世界变换矩阵及启用状态。 */
static void test_painter_picture_transform_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;
    XImageTransform matrix = { 1.0f, 0.0f, 0.0f, 1.0f,
                               2.0f, 0.0f, 0.0f, 0.0f, 1.0f };

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "transform record begins picture");
    XPainter_setPen(&record, 0xffff0000u);
    XPainter_setTransform(&record, &matrix, false);
    expect_true(XPainter_drawLine(&record, 0, 0, 0, 0),
                "transform record draws translated point");
#if XPAINTER_WORLD_MATRIX_ON
    XPainter_setWorldMatrixEnabled(&record, false);
    expect_true(XPainter_drawLine(&record, 0, 0, 0, 0),
                "disabled world matrix record draws logical point");
#endif /* XPAINTER_WORLD_MATRIX_ON */
    expect_true(XPainter_end(&record), "transform record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "transform record stream is valid");

    XImage_init_ex(&target, 5, 2, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "transform replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "transform picture replays");
    expect_true(XImage_pixel(&target, 2, 0) == 0xffff0000u,
                "picture replay applies translated matrix");
#if XPAINTER_WORLD_MATRIX_ON
    expect_true(XImage_pixel(&target, 0, 0) == 0xffff0000u &&
                !XPainter_worldMatrixEnabled(&replay),
                "picture replay restores disabled world matrix");
#else
    expect_true(XImage_pixel(&target, 0, 0) == 0xff000000u,
                "crop replay retains translated matrix state");
#endif /* XPAINTER_WORLD_MATRIX_ON */

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}

#if XPAINTER_VIEW_TRANSFORM_ON
/** @brief 校验 Picture 记录并回放 window/viewport 视图变换状态。 */
static void test_painter_picture_view_transform_record(void)
{
    XPicture picture;
    XPainter record;
    XPainter replay;
    XImage target;
    XRect window = { 1, 1, 2, 2 };
    XRect viewport = { 3, 3, 4, 4 };
    XRect result;

    XPicture_init(&picture, -1);
    XPainter_init(&record, NULL);
    expect_true(XPainter_begin_picture(&record, &picture),
                "view transform record begins picture");
    XPainter_setPen(&record, 0xffff0000u);
    XPainter_setWindow(&record, &window);
    XPainter_setViewport(&record, &viewport);
    expect_true(XPainter_drawLine(&record, 1, 1, 1, 1),
                "view transform record draws mapped point");
    XPainter_setViewTransformEnabled(&record, false);
    expect_true(XPainter_drawLine(&record, 1, 1, 1, 1),
                "view transform record draws unmapped point");
    expect_true(XPainter_end(&record), "view transform record ends picture");
    expect_true(XPicture_isValidStream(&picture),
                "view transform record stream is valid");
    expect_true(XPicture_size(&picture) ==
                    XPICTURE_HEADER_SIZE +
                    (XPICTURE_RECORD_HEADER_SIZE + 20u) +
                    4u * (XPICTURE_RECORD_HEADER_SIZE + 16u) +
                    2u * (XPICTURE_RECORD_HEADER_SIZE + 16u) +
                    (XPICTURE_RECORD_HEADER_SIZE + 4u),
                "view transform setters use fixed portable records");

    XImage_init_ex(&target, 8, 8, XImageFormat_ARGB32);
    XImage_fillRect(&target, NULL, 0xff000000u);
    XPainter_init(&replay, NULL);
    expect_true(XPainter_begin_image(&replay, &target),
                "view transform replay begins image");
    expect_true(XPicture_play(&picture, &replay),
                "view transform picture replays");
    expect_true(XImage_pixel(&target, 3, 3) == 0xffff0000u &&
                XImage_pixel(&target, 1, 1) == 0xffff0000u,
                "picture replay applies and disables view mapping");
    XPainter_window(&replay, &result);
    expect_true(result.x == 1 && result.y == 1 &&
                result.width == 2 && result.height == 2,
                "picture replay restores logical window");
    XPainter_viewport(&replay, &result);
    expect_true(result.x == 3 && result.y == 3 &&
                result.width == 4 && result.height == 4,
                "picture replay restores device viewport");
    expect_true(!XPainter_viewTransformEnabled(&replay),
                "picture replay restores disabled view mapping");

    XPainter_end(&replay);
    XPainter_deinit(&replay);
    XPainter_deinit(&record);
    XImage_deinit_base(&target);
    XPicture_deinit_base(&picture);
}
#endif /* XPAINTER_VIEW_TRANSFORM_ON */


static void test_icon_matching(void)
{
    XPixmap normal;
    XPixmap normalLarge;
    XPixmap normalReplacement;
    XPixmap active;
    XPixmap out;
    XIcon fallbackIcon;
    XIcon sizeIcon;

    XPixmap_init_ex(&normal, 16, 16);
    XPixmap_init_ex(&normalLarge, 32, 32);
    XPixmap_init_ex(&active, 20, 20);
    XPixmap_init_ex(&normalReplacement, 16, 16);
    XPixmap_fill(&normalReplacement, 0xff12ab34u);
    XIcon_init_pixmap(&fallbackIcon, &normal);
    XIcon_addPixmap(&fallbackIcon, &normalReplacement,
                    XIconMode_Normal, XIconState_Off);
    XPixmap_init(&out);
    XIcon_pixmap(&fallbackIcon, 16, 16, XIconMode_Normal, XIconState_Off, &out);
    {
        XImage replacementImage;
        XImage_init(&replacementImage);
        XPixmap_toImage(&out, &replacementImage);
        expect_true(XPixmap_width(&out) == 16 && XPixmap_height(&out) == 16 &&
                    (XImage_pixel(&replacementImage, 0, 0) & 0x00ffffffu) == 0x12ab34u,
                    "icon addPixmap replaces identical physical entry");
        XImage_deinit_base(&replacementImage);
    }
    XPixmap_deinit_base(&out);
    XIcon_addPixmap(&fallbackIcon, &active, XIconMode_Active, XIconState_Off);
    XPixmap_init(&out);

    /* Qt's Disabled fallback checks Normal before Active, even when Active
     * has a closer-sized source. */
    XIcon_pixmap(&fallbackIcon, 20, 20, XIconMode_Disabled, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 16 && XPixmap_height(&out) == 16,
                "icon mode fallback prefers Normal over Active");
    XPixmap_deinit_base(&out);

    /* Within one mode/state, Qt chooses the smallest source not below the
     * request and scales it to the requested size. */
    XIcon_init_pixmap(&sizeIcon, &normal);
    XIcon_addPixmap(&sizeIcon, &normalLarge, XIconMode_Normal, XIconState_Off);
    XPixmap_init(&out);
    XIcon_pixmap(&sizeIcon, 24, 24, XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 24 && XPixmap_height(&out) == 24,
                "icon matching uses the smallest sufficient source");

    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&sizeIcon);
    XIcon_deinit_base(&fallbackIcon);
    XPixmap_deinit_base(&active);
    XPixmap_deinit_base(&normalReplacement);
    XPixmap_deinit_base(&normalLarge);
    XPixmap_deinit_base(&normal);
}

static void test_icon_device_pixel_ratio(void)
{
    XPixmap normal;
    XPixmap highResolution;
    XPixmap smallHighResolution;
    XPixmap largeHighResolution;
    XPixmap out;
    XImage selectedImage;
    XIcon icon;
    XIcon highIcon;
    XIcon mixedIcon;
    XVector sizes;
    XSize* size;
    uint32_t selectedPixel;

    XPixmap_init_ex(&normal, 32, 32);
    XIcon_init_pixmap(&icon, &normal);
    XPixmap_init(&out);
    XIcon_pixmapRatio(&icon, 32, 32, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon DPR falls back when only normal-resolution pixels exist");
    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&icon);

    XPixmap_init_ex(&highResolution, 64, 64);
    XPixmap_setDevicePixelRatio(&highResolution, 2.0f);
    XIcon_init_pixmap(&highIcon, &highResolution);
    XPixmap_init(&out);
    XIcon_pixmapRatio(&highIcon, 32, 32, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 64 && XPixmap_height(&out) == 64 &&
                XPixmap_devicePixelRatio(&out) == 2.0f,
                "icon DPR preserves a matching high-resolution source");

    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&highIcon);

    /* 同一 DPR 的候选按物理面积比较。请求 10x10@2x 时，16x16 逻辑
     * 资源已经足够，Qt 不应因为把请求面积乘过 DPR 而误选 32x32。 */
    XPixmap_init_ex(&smallHighResolution, 32, 32);
    XPixmap_setDevicePixelRatio(&smallHighResolution, 2.0f);
    XPixmap_fill(&smallHighResolution, 0xffff0000u);
    XPixmap_init_ex(&largeHighResolution, 64, 64);
    XPixmap_setDevicePixelRatio(&largeHighResolution, 2.0f);
    XPixmap_fill(&largeHighResolution, 0xff0000ffu);
    XIcon_init(&mixedIcon);
    XIcon_addPixmap(&mixedIcon, &smallHighResolution,
                    XIconMode_Normal, XIconState_Off);
    XIcon_addPixmap(&mixedIcon, &largeHighResolution,
                    XIconMode_Normal, XIconState_Off);
    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 10, 10, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    XImage_init(&selectedImage);
    XPixmap_toImage(&out, &selectedImage);
    selectedPixel = XImage_pixel(&selectedImage, 0, 0);
    expect_true(XPixmap_width(&out) == 20 && XPixmap_height(&out) == 20 &&
                (selectedPixel & 0x00ffffffu) == 0x00ff0000u,
                "icon same-DPR matching compares physical candidate area");
    XImage_deinit_base(&selectedImage);
    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&mixedIcon);
    XPixmap_deinit_base(&largeHighResolution);
    XPixmap_deinit_base(&smallHighResolution);

    /* Qt prefers the exact DPR when identical logical sizes exist, then sizes
     * the returned source against the requested device size. */
    XIcon_init(&mixedIcon);
    XIcon_addPixmap(&mixedIcon, &normal, XIconMode_Normal, XIconState_Off);
    XIcon_addPixmap(&mixedIcon, &highResolution, XIconMode_Normal, XIconState_Off);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, 1.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon 1x request chooses the 1x source over the 2x source");
    XPixmap_deinit_base(&out);

    /* QIcon treats zero and sub-normal DPR requests as the ordinary 1x path. */
    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, 0.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon non-positive DPR request uses the ordinary 1x path");
    XPixmap_deinit_base(&out);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, 0.5f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon sub-normal DPR request uses the ordinary 1x path");
    XPixmap_deinit_base(&out);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, NAN,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon NaN DPR request uses the ordinary 1x path");
    XPixmap_deinit_base(&out);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, INFINITY,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_isNull(&out),
                "icon infinite DPR request returns null safely");
    XPixmap_deinit_base(&out);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 32, 32, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 64 && XPixmap_height(&out) == 64 &&
                XPixmap_devicePixelRatio(&out) == 2.0f,
                "icon 2x request chooses the 2x source");
    XPixmap_deinit_base(&out);

    XPixmap_init(&out);
    XIcon_pixmapRatio(&mixedIcon, 20, 20, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 40 && XPixmap_height(&out) == 40 &&
                XPixmap_devicePixelRatio(&out) == 2.0f,
                "icon 2x source scales down to the requested device size");
    XPixmap_deinit_base(&out);

    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&mixedIcon, XIconMode_Normal, XIconState_Off, &sizes);
    size = XVector_size_base((const XContainer*)&sizes) == 1
        ? (XSize*)XVector_at_base(&sizes, 0) : NULL;
    expect_true(size && size->width == 32 && size->height == 32,
                "icon availableSizes deduplicates identical logical sizes");
    XVector_deinit_base((XClass*)&sizes);

    XIcon_deinit_base(&mixedIcon);
    XPixmap_deinit_base(&highResolution);
    XPixmap_deinit_base(&normal);
}

static void test_icon_style_helper(void)
{
    XImage baseImage;
    XImage disabledImage;
    XImage selectedImage;
    XPixmap source;
    XPixmap disabled;
    XPixmap selected;
    XIcon icon;
    uint32_t basePixel;
    uint32_t disabledPixel;
    uint32_t selectedPixel;
    bool alphaPreserved;

    XPixmap_init_ex(&source, 16, 16);
    XImage_init_ex(&baseImage, 16, 16, XImageFormat_ARGB32);
    XImage_fill(&baseImage, 0xff336699u);
    XPixmap_fromImage(&baseImage, 0, &source);

    XIcon_init_pixmap(&icon, &source);

    XPixmap_init(&disabled);
    XIcon_pixmap(&icon, 16, 16, XIconMode_Disabled, XIconState_Off, &disabled);
    expect_true(!XPixmap_isNull(&disabled), "disabled icon style produces non-null pixmap");
    XImage_init(&disabledImage);
    XPixmap_toImage(&disabled, &disabledImage);
    disabledPixel = XImage_pixel(&disabledImage, 0, 0);
    alphaPreserved = ((disabledPixel >> 24) & 0xffu) == 0xffu;
    expect_true(alphaPreserved && disabledPixel != 0xff336699u,
                "disabled icon style keeps opaque alpha and recolorizes pixels");

    XPixmap_init(&selected);
    XIcon_pixmap(&icon, 16, 16, XIconMode_Selected, XIconState_Off, &selected);
    expect_true(!XPixmap_isNull(&selected), "selected icon style produces non-null pixmap");
    XImage_init(&selectedImage);
    XPixmap_toImage(&selected, &selectedImage);
    selectedPixel = XImage_pixel(&selectedImage, 0, 0);
    basePixel = 0xff336699u;
    alphaPreserved = ((selectedPixel >> 24) & 0xffu) == 0xffu;
    expect_true(alphaPreserved && selectedPixel != basePixel &&
                ((selectedPixel >> 16) & 0xffu) >= ((basePixel >> 16) & 0xffu) &&
                ((selectedPixel >> 8) & 0xffu) >= ((basePixel >> 8) & 0xffu) &&
                (selectedPixel & 0xffu) >= (basePixel & 0xffu),
                "selected icon style keeps opaque alpha and pulls pixels toward highlight");

    XImage_deinit_base(&selectedImage);
    XPixmap_deinit_base(&selected);
    XImage_deinit_base(&disabledImage);
    XPixmap_deinit_base(&disabled);
    XIcon_deinit_base(&icon);
    XImage_deinit_base(&baseImage);
    XPixmap_deinit_base(&source);
}

static void test_icon_scaled_pixmap_cache(void)
{
    XPixmap source;
    XPixmap first;
    XPixmap second;
    XPixmap afterClear;
    XIcon icon;
    int64_t firstKey;
    int64_t secondKey;

    XPixmap_init_ex(&source, 32, 32);
    XIcon_init_pixmap(&icon, &source);

    XPixmap_init(&first);
    XIcon_pixmapRatio(&icon, 20, 20, 1.0f,
                      XIconMode_Normal, XIconState_Off, &first);
    firstKey = XPixmap_cacheKey(&first);
    expect_true(firstKey != 0 && XPixmap_width(&first) == 20 &&
                XPixmap_height(&first) == 20,
                "icon scaled pixmap first generation produces a 20x20 pixmap");

    XPixmap_init(&second);
    XIcon_pixmapRatio(&icon, 20, 20, 1.0f,
                      XIconMode_Normal, XIconState_Off, &second);
    secondKey = XPixmap_cacheKey(&second);
    expect_true(secondKey == firstKey && XPixmap_width(&second) == 20 &&
                XPixmap_height(&second) == 20,
                "icon scaled pixmap second request hits the same cache entry");

    XPixmapCache_clear();

    XPixmap_init(&afterClear);
    XIcon_pixmapRatio(&icon, 20, 20, 1.0f,
                      XIconMode_Normal, XIconState_Off, &afterClear);
    expect_true(XPixmap_cacheKey(&afterClear) != firstKey &&
                XPixmap_width(&afterClear) == 20 &&
                XPixmap_height(&afterClear) == 20,
                "icon scaled pixmap cache clear forces a new pixmap generation");

    XPixmap_deinit_base(&afterClear);
    XPixmap_deinit_base(&second);
    XPixmap_deinit_base(&first);
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&source);
}


static void test_icon_theme_engine_contract(void)
{
    XPixmap fallbackPixmap;
    XIcon fallbackIcon;
    XIcon icon;
    XIcon missing;
    XString* key;
    XString* name;
    XIconThemeEngine* engine;
    XIconEngine* clone;

    XPixmap_init_ex(&fallbackPixmap, 4, 4);
    XIcon_init_pixmap(&fallbackIcon, &fallbackPixmap);

    XIcon_fromTheme_2("__xinyuec_no_such_theme_icon__", &fallbackIcon, &icon);
    expect_true(!XIcon_isNull(&icon), "theme icon fallback keeps non-null");

    XIcon_fromTheme_2("__xinyuec_no_such_theme_icon__", NULL, &missing);
    expect_true(XIcon_isNull(&missing), "theme icon without fallback is null");

    engine = XIconThemeEngine_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                          "__xinyuec_no_such_theme_icon__");
    expect_true(engine != NULL, "theme engine create");
    name = XIconEngine_iconName_base((const XIconEngine*)engine);
    expect_true(name != NULL && !XString_isEmpty_base((const XContainer*)name),
                "theme engine iconName");
    key = XIconEngine_key_base((const XIconEngine*)engine);
    expect_true(key != NULL && !XString_isEmpty_base((const XContainer*)key),
                "theme engine key");
    expect_true(key && strcmp(XString_toUtf8(key), "QIconLoaderEngine") == 0,
                "theme engine key matches Qt QIconLoaderEngine");
    clone = XIconEngine_clone_base((const XIconEngine*)engine);
    expect_true(clone != NULL, "theme engine clone");
    if (clone) XIconEngine_delete_base(clone);
    if (key) XString_delete_base((XClass*)key);
    if (name) XString_delete_base((XClass*)name);
    if (engine) XIconThemeEngine_delete_base(engine);

    XIcon_deinit_base(&missing);
    XIcon_deinit_base(&icon);
    XIcon_deinit_base(&fallbackIcon);
    XPixmap_deinit_base(&fallbackPixmap);
}

static void test_icon_engine_hook_contract(void)
{
    XIconEngine* engine;
    XPixmap pixmap;
    XIconEngineScaledPixmapArgument argument;
    bool isNull = true;

    engine = XIconEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    expect_true(engine != NULL, "icon engine base create for hook contract");
    if (!engine) return;

    XIconEngine_virtualHook_base(engine, XIconEngine_IsNullHook, &isNull);
    expect_true(isNull, "icon engine IsNullHook default preserves caller value");
    isNull = false;
    XIconEngine_virtualHook_base(engine, XIconEngine_IsNullHook, &isNull);
    expect_true(!isNull, "icon engine IsNullHook false value remains false");

    XPixmap_init(&pixmap);
    argument.size.width = 0;
    argument.size.height = 16;
    argument.mode = XIconMode_Normal;
    argument.state = XIconState_Off;
    argument.scale = 2.0f;
    argument.pixmap = &pixmap;
    XIconEngine_virtualHook_base(engine, XIconEngine_ScaledPixmapHook, &argument);
    expect_true(XPixmap_isNull(&pixmap),
                "icon engine ScaledPixmapHook rejects non-positive dimensions");

    argument.size.width = 8;
    argument.size.height = 8;
    argument.scale = 0.0f;
    XIconEngine_virtualHook_base(engine, XIconEngine_ScaledPixmapHook, &argument);
    expect_true(XPixmap_isNull(&pixmap),
                "icon engine ScaledPixmapHook rejects non-positive scale");

    argument.scale = NAN;
    XIconEngine_virtualHook_base(engine, XIconEngine_ScaledPixmapHook, &argument);
    expect_true(XPixmap_isNull(&pixmap),
                "icon engine ScaledPixmapHook rejects NaN scale");

    argument.scale = INFINITY;
    XIconEngine_virtualHook_base(engine, XIconEngine_ScaledPixmapHook, &argument);
    expect_true(XPixmap_isNull(&pixmap),
                "icon engine ScaledPixmapHook rejects infinite scale");

    argument.scale = 2.0f;
    XIconEngine_virtualHook_base(engine, XIconEngine_ScaledPixmapHook, &argument);
    expect_true(!XPixmap_isNull(&pixmap) && XPixmap_width(&pixmap) == 16 &&
                    XPixmap_height(&pixmap) == 16 &&
                    XPixmap_devicePixelRatio(&pixmap) == 2.0f,
                "base icon engine scaled hook uses painted physical pixmap");

    XPixmap_deinit_base(&pixmap);
    XIconEngine_delete_base(engine);
}

static void test_icon_paint_visual_alignment(void)
{
    XPixmap source;
    XImage target;
    XIcon icon;
    XPainter painter;
    PictureProbe probe = { 0, 0, 0, 0, 0, false };
    uint32_t background = 0xff000000u;
    uint32_t pixelColor = 0xff336699u;

    XPixmap_init_ex(&source, 4, 2);
    XPixmap_fill(&source, pixelColor);
    XIcon_init_pixmap(&icon, &source);

    XImage_init_ex(&target, 20, 10, XImageFormat_ARGB32);
    XImage_fill(&target, background);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target),
                "icon paint alignment begins image");
    XIcon_paint(&icon, &painter, 0, 0, 20, 10, XAlignment_Left,
                XIconMode_Normal, XIconState_Off);
    expect_true(XPainter_end(&painter), "icon paint alignment ends LTR image");
    expect_true((XImage_pixel(&target, 0, 0) & 0x00ffffffu) == 0x336699u &&
                (XImage_pixel(&target, 4, 0) & 0x00ffffffu) == 0x0u,
                "icon paint Left alignment starts at the visual left edge");

#if XPAINTER_LAYOUT_DIRECTION_ON
    XImage_fill(&target, background);
    expect_true(XPainter_begin_image(&painter, &target),
                "icon paint alignment begins RTL image");
    XPainter_setLayoutDirection(&painter, XPainterLayoutDirection_RightToLeft);
    XIcon_paint(&icon, &painter, 0, 0, 20, 10, XAlignment_Left,
                XIconMode_Normal, XIconState_Off);
    expect_true(XPainter_end(&painter), "icon paint alignment ends RTL image");
    expect_true((XImage_pixel(&target, 16, 0) & 0x00ffffffu) == 0x336699u &&
                (XImage_pixel(&target, 15, 0) & 0x00ffffffu) == 0x0u,
                "icon paint Leading alignment mirrors to the visual right in RTL");
#endif /* XPAINTER_LAYOUT_DIRECTION_ON */

    /* QIcon::paint 的状态保存必须成对发生；没有 save 回调时不能误调用 restore。 */
    XImage_fill(&target, background);
    expect_true(XPainter_begin_image(&painter, &target),
                "icon paint unpaired state begins image");
    painter.m_userData = &probe;
    painter.m_save = NULL;
    painter.m_restore = picture_probe_restore;
    XIcon_paint(&icon, &painter, 0, 0, 20, 10, XAlignment_Left,
                XIconMode_Normal, XIconState_Off);
    expect_true(probe.restoreCalls == 0 &&
                (XImage_pixel(&target, 0, 0) & 0x00ffffffu) == 0x336699u,
                "icon paint does not restore without a successful save");
    expect_true(XPainter_end(&painter), "icon paint unpaired state ends image");

    XPainter_deinit(&painter);
    XImage_deinit_base(&target);
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&source);
}


#if XIMAGECODEC_ON
static void test_icon_add_file_size(void)
{
    XImage image;
    XIcon icon;
    XVector sizes;
    XPixmap loaded;
    XSize* size;

    XImage_init_ex(&image, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(XImage_save_2(&image, "xgui_icon_add_file.bmp", "BMP", -1),
                "writes icon addFile fixture");

    XIcon_init(&icon);
    XIcon_addFile_2(&icon, "xgui_icon_add_file.bmp", 8, 6,
                  XIconMode_Normal, XIconState_Off);
    expect_true(!XIcon_isNull(&icon), "icon addFile entry is present before load");
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);
    size = XVector_size_base((const XContainer*)&sizes) == 1
        ? (XSize*)XVector_at_base(&sizes, 0) : NULL;
    expect_true(size && size->width == 8 && size->height == 6,
                "icon addFile stores the requested raster size");

    XPixmap_init(&loaded);
    XIcon_pixmap(&icon, 16, 16, XIconMode_Normal, XIconState_Off, &loaded);
    expect_true(!XPixmap_isNull(&loaded),
                "icon addFile entry loads on first pixmap request");

    XVector_clear_base((XContainer*)&sizes);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);
    size = XVector_size_base((const XContainer*)&sizes) == 1
        ? (XSize*)XVector_at_base(&sizes, 0) : NULL;
    expect_true(size && size->width == 3 && size->height == 2,
                "icon addFile entry reports native size after load");

    XPixmap_deinit_base(&loaded);
    XVector_deinit_base((XClass*)&sizes);
    XIcon_deinit_base(&icon);
    XImage_deinit_base(&image);
    remove("xgui_icon_add_file.bmp");
}
#endif /* XIMAGECODEC_ON */

#if XIMAGECODEC_ON
static void test_image_device_io(void)
{
    XString* file_name = NULL;
    XFile file;
    XImage source;
    XImage loaded;
    XImageWriter writer;
    XImageWriter file_writer;
    XImageReader reader;
    XImageReader autodetect;
    const char* autodetectedFormat;
    bool ok;

    make_file_name(&file_name);
    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xffff0000u);
    XImage_setPixel(&source, 1, 0, 0xff00ff00u);
    XImage_setPixel(&source, 2, 0, 0xff0000ffu);

    /* 文件名构造器应立即持有可观察的内部设备，并按 Qt 规则保留初始错误文本；
       未指定格式时 canWrite() 通过文件后缀推断 BMP。 */
    XImageWriter_init_file_2(&file_writer, XString_toUtf8(file_name), NULL);
    expect_true(XImageWriter_device(&file_writer) != NULL,
                "file writer exposes its internally-owned device");
    expect_true(strcmp(XImageWriter_errorString_2(&file_writer), "Unknown error") == 0,
                "file writer initializes the unknown error string");
    expect_true(XImageWriter_canWrite(&file_writer),
                "file writer infers BMP format from the filename suffix");
    XImageWriter_deinit_base(&file_writer);

    /* QImageWriter::canWrite() 删除检查期间新建但最终失败的 QFile；未知
       格式不得遗留空目标文件。 */
    {
        XString* failedName = XString_create_utf8(
            "xgui_writer_failed_cleanup.unknown");
        XImageWriter failedWriter;
        XFile_remove_static(failedName);
        XImageWriter_init_file_2(&failedWriter,
                                 XString_toUtf8(failedName), "unsupported");
        expect_true(!XImageWriter_canWrite(&failedWriter) &&
                    !XFile_exists_static(failedName),
                    "failed writer removes newly-created unsupported target");
        XImageWriter_deinit_base(&failedWriter);
        XFile_remove_static(failedName);
        XString_delete_base((XClass*)failedName);
    }

    XFile_init_2(&file, file_name);
    ok = XFile_open_2(&file, XIODevice_WriteOnly, 0);
    expect_true(ok, "opens BMP output device");
    XImageWriter_init_device_2(&writer, (XIODevice*)&file, "BMP");
    expect_true(XImageWriter_canWrite(&writer), "device writer reports BMP support");
    expect_true(XImageWriter_write(&writer, &source), "device writer writes BMP");
    XImageWriter_deinit_base(&writer);
    XIODevice_close_base((XIODevice*)&file);
    XClass_deinit_base((XClass*)&file);

    XFile_init_2(&file, file_name);
    ok = XFile_open_2(&file, XIODevice_ReadOnly, 0);
    expect_true(ok, "opens BMP input device");
    XImageReader_init_device_2(&autodetect, (XIODevice*)&file, NULL);
    autodetectedFormat = XImageReader_format_2(&autodetect);
    expect_true(autodetectedFormat &&
                strcmp(autodetectedFormat, "bmp") == 0,
                "reader format reports detected handler format");
    XImageReader_deinit_base(&autodetect);
    XImageReader_init_device_2(&reader, (XIODevice*)&file, "BMP");
    expect_true(XImageReader_canRead(&reader), "device reader detects BMP");
    XImage_init(&loaded);
    expect_true(XImageReader_read(&reader, &loaded), "device reader reads BMP");
    expect_true(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2,
                "device reader preserves dimensions");
    expect_true(XImage_pixel(&loaded, 0, 0) == 0xffff0000u,
                "device BMP round trip preserves pixels");

    XImage_deinit_base(&loaded);
    XImageReader_deinit_base(&reader);
    XIODevice_close_base((XIODevice*)&file);
    XClass_deinit_base((XClass*)&file);
    XFile_remove_static(file_name);
    XString_delete_base((XClass*)file_name);
    XImage_deinit_base(&source);
}

#if XIMAGEIOPLUGIN_ON

static int g_mockImageWidth;
static int g_mockImageHeight;
static uint32_t g_mockImagePixels[4];
static bool g_mockSuffixOnlyCapabilities;
static bool g_mockRejectSuffixCanRead;
static bool g_mockSupportsAnimation;
static bool g_mockSupportsTransformation;
static XImageIOHandlerTransformation g_mockTransformation;
static XString g_mockExpectedWriterDescription;
static bool g_mockWriterDescriptionCheck;
static bool g_mockWriterDescriptionMatched;

XCLASS_DEFINE_BEGING(TestImageHandler)
XCLASS_DEFINE_EXTEND_END(TestImageHandler, XImageIOHandler)

typedef struct TestImageHandler
{
    XImageIOHandler m_base;
    XString         m_description;
} TestImageHandler;

static bool VTestImageHandler_canRead(const XImageIOHandler* self)
{
    const XString* format = XImageIOHandler_format_const(self);
    if (g_mockRejectSuffixCanRead && format &&
        XString_equals_utf8(format, "bmp", XChar_CaseInsensitive))
        return false;
    return g_mockImageWidth > 0 && g_mockImageHeight > 0;
}

static bool VTestImageHandler_read(XImageIOHandler* self, XImage* image)
{
    int x;
    int y;
    (void)self;
    if (!image || g_mockImageWidth <= 0 || g_mockImageHeight <= 0)
        return false;
    if (g_mockImageWidth * g_mockImageHeight > 4)
        return false;
    if (!XClassIsVtableNull(image))
        XImage_deinit_base(image);
    XImage_init(image);
    XImage_init_ex(image, g_mockImageWidth, g_mockImageHeight, XImageFormat_ARGB32);
    if (XImage_isNull(image))
        return false;
    for (y = 0; y < g_mockImageHeight; ++y)
        for (x = 0; x < g_mockImageWidth; ++x)
            XImage_setPixel(image, x, y, g_mockImagePixels[y * g_mockImageWidth + x]);
    return !XImage_isNull(image);
}

static bool VTestImageHandler_write(XImageIOHandler* self, const XImage* image)
{
    int x;
    int y;
    if (g_mockWriterDescriptionCheck) {
        XImageIOHandlerOptionValue value;
        memset(&value, 0, sizeof(value));
        g_mockWriterDescriptionMatched =
            XImageIOHandler_optionValue(self, XImageIOHandlerOption_Description, &value) &&
            value.string &&
            XString_equals(value.string, &g_mockExpectedWriterDescription,
                           XChar_CaseSensitive);
    }
    if (!image || XImage_isNull(image))
        return false;
    g_mockImageWidth = XImage_width(image);
    g_mockImageHeight = XImage_height(image);
    if (g_mockImageWidth <= 0 || g_mockImageHeight <= 0 ||
        g_mockImageWidth * g_mockImageHeight > 4)
        return false;
    for (y = 0; y < g_mockImageHeight; ++y)
        for (x = 0; x < g_mockImageWidth; ++x)
            g_mockImagePixels[y * g_mockImageWidth + x] = XImage_pixel(image, x, y);
    return true;
}

static bool VTestImageHandler_option(const XImageIOHandler* self,
                                     XImageIOHandlerOption option,
                                     void* out)
{
    TestImageHandler* handler = (TestImageHandler*)self;
    if (option == XImageIOHandlerOption_Size && out) {
        XSize* size = (XSize*)out;
        size->width = g_mockImageWidth;
        size->height = g_mockImageHeight;
        return true;
    }
    if (option == XImageIOHandlerOption_Description && out && handler) {
        XImageIOHandlerOptionValue* value =
            (XImageIOHandlerOptionValue*)out;
        value->string = &handler->m_description;
        return true;
    }
    if (option == XImageIOHandlerOption_Animation && out &&
        g_mockSupportsAnimation) {
        XImageIOHandlerOptionValue* value =
            (XImageIOHandlerOptionValue*)out;
        value->boolean = true;
        return true;
    }
    if (option == XImageIOHandlerOption_ImageTransformation && out &&
        g_mockSupportsTransformation) {
        XImageIOHandlerOptionValue* value =
            (XImageIOHandlerOptionValue*)out;
        value->transformation = g_mockTransformation;
        return true;
    }
    return false;
}

static void VTestImageHandler_setOption(XImageIOHandler* self,
                                        XImageIOHandlerOption option,
                                        const void* value)
{
    if (option == XImageIOHandlerOption_Description)
        XImageIOHandler_storeOptionValue(self, option, value);
}

static bool VTestImageHandler_supportsOption(const XImageIOHandler* self,
                                             XImageIOHandlerOption option)
{
    (void)self;
    return option == XImageIOHandlerOption_Size ||
           option == XImageIOHandlerOption_Description ||
           (option == XImageIOHandlerOption_Animation &&
            g_mockSupportsAnimation) ||
           (option == XImageIOHandlerOption_ImageTransformation &&
            g_mockSupportsTransformation);
}

static void VTestImageHandler_deinit(XImageIOHandler* self)
{
    TestImageHandler* handler = (TestImageHandler*)self;
    if (!handler) return;
    XString_deinit_base((XClass*)&handler->m_description);
    XClass_Deinit_Parent(XImageIOHandler, self);
}

static XVtable* TestImageHandler_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TestImageHandler)
    XVTABLE_INHERIT_XCLASS(XImageIOHandler);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_CanRead, VTestImageHandler_canRead);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Read, VTestImageHandler_read);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Write, VTestImageHandler_write);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_Option, VTestImageHandler_option);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SetOption, VTestImageHandler_setOption);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOHandler_SupportsOption,
                             VTestImageHandler_supportsOption);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VTestImageHandler_deinit);
    return XVTABLE_DEFAULT;
}

static TestImageHandler* TestImageHandler_create(void)
{
    TestImageHandler* self = (TestImageHandler*)XMemory_malloc(
        sizeof(TestImageHandler), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(TestImageHandler));
    XImageIOHandler_init(&self->m_base);
    XString_init(&self->m_description);
    XString_assign_utf8(&self->m_description,
                        "Title:   Sunset  \n\nAuthor:  Alice\n\nDescription:  demo\n\nRawKey\t: raw  \n\nBareText");
    XClassSetVtable(self, TestImageHandler);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

XCLASS_DEFINE_BEGING(TestImagePlugin)
XCLASS_DEFINE_EXTEND_END(TestImagePlugin, XImageIOPlugin)

typedef struct TestImagePlugin
{
    XImageIOPlugin m_base;
    XStringList* m_keys;
    XStringList* m_filters;
    XStringList* m_mimes;
} TestImagePlugin;

static uint32_t VTestImagePlugin_capabilities(const XImageIOPlugin* self,
                                              XIODevice* device,
                                              const XString* format)
{
    char consumed;
    (void)self;
    /* 用于验证注册表遵守 Qt 的设备位置保护契约；真实插件可能读取魔数。 */
    if (g_mockConsumeCapabilities && device)
        (void)XIODevice_read_1(device, &consumed, 1);
    if (g_mockSuffixOnlyCapabilities &&
        (!format || XContainer_isEmpty_base((const XContainer*)format)))
        return 0;
    (void)format;
    return (uint32_t)(XImageIOPlugin_CanRead | XImageIOPlugin_CanWrite);
}

static XImageIOHandler* VTestImagePlugin_create(XImageIOPlugin* self,
                                                XIODevice* device,
                                                const XString* format)
{
    TestImageHandler* handler;
    (void)self;
    (void)format;
    if (g_mockRejectCreate)
        return NULL;
    handler = TestImageHandler_create();
    if (handler)
        XImageIOHandler_setDevice((XImageIOHandler*)handler, device);
    return (XImageIOHandler*)handler;
}

static XStringList* VTestImagePlugin_keys(const XImageIOPlugin* self)
{
    return self ? ((TestImagePlugin*)self)->m_keys : NULL;
}

static XStringList* VTestImagePlugin_nameFilters(const XImageIOPlugin* self)
{
    return self ? ((TestImagePlugin*)self)->m_filters : NULL;
}

static XStringList* VTestImagePlugin_mimeTypes(const XImageIOPlugin* self)
{
    return self ? ((TestImagePlugin*)self)->m_mimes : NULL;
}

static void VTestImagePlugin_deinit(XImageIOPlugin* self)
{
    TestImagePlugin* plugin = (TestImagePlugin*)self;
    if (!plugin) return;
    if (plugin->m_keys) XStringList_delete_base((XClass*)plugin->m_keys);
    if (plugin->m_filters) XStringList_delete_base((XClass*)plugin->m_filters);
    if (plugin->m_mimes) XStringList_delete_base((XClass*)plugin->m_mimes);
    plugin->m_keys = NULL;
    plugin->m_filters = NULL;
    plugin->m_mimes = NULL;
    XClass_Deinit_Parent(XImageIOPlugin, self);
}

static XVtable* TestImagePlugin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TestImagePlugin)
    XVTABLE_INHERIT_XCLASS(XImageIOPlugin);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VTestImagePlugin_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Capabilities, VTestImagePlugin_capabilities);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Create, VTestImagePlugin_create);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_Keys, VTestImagePlugin_keys);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_NameFilters, VTestImagePlugin_nameFilters);
    XVTABLE_OVERLOAD_DEFAULT(EXImageIOPlugin_MimeTypes, VTestImagePlugin_mimeTypes);
    return XVTABLE_DEFAULT;
}

static TestImagePlugin* TestImagePlugin_create(void)
{
    TestImagePlugin* self = (TestImagePlugin*)XMemory_malloc(
        sizeof(TestImagePlugin), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(TestImagePlugin));
    XImageIOPlugin_init(&self->m_base);
    self->m_keys = XStringList_create();
    self->m_filters = XStringList_create();
    self->m_mimes = XStringList_create();
    if (!self->m_keys || !self->m_filters || !self->m_mimes) {
        if (self->m_keys) XStringList_delete_base((XClass*)self->m_keys);
        if (self->m_filters) XStringList_delete_base((XClass*)self->m_filters);
        if (self->m_mimes) XStringList_delete_base((XClass*)self->m_mimes);
        XFree_System(self);
        return NULL;
    }
    XStringList_push_back_utf8(self->m_keys, "mock");
    XStringList_push_back_utf8(self->m_filters, "*.mock");
    XStringList_push_back_utf8(self->m_mimes, "image/x-xinyue-mock");
    XClassSetVtable(self, TestImagePlugin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XIMAGEIOPLUGIN_ON */

static void test_image_handler_registry(void)
{
    XImageIOHandler baseHandler;
    XImageIOHandlerOptionValue baseOption;
    XStringList* readerFormats = XImageReader_supportedImageFormats();
    XStringList* readerMimes = XImageReader_supportedMimeTypes();
    XStringList* readerBmp = XImageReader_imageFormatsForMimeType_2("image/bmp");
    XStringList* readerJpeg = XImageReader_imageFormatsForMimeType_2("image/jpeg");
    XStringList* readerUpperBmp = XImageReader_imageFormatsForMimeType_2("IMAGE/BMP");
    XStringList* writerFormats = XImageWriter_supportedImageFormats();
    XStringList* writerUnknown = XImageWriter_imageFormatsForMimeType_2("image/png");
    XStringList* writerJpeg = XImageWriter_imageFormatsForMimeType_2("image/jpeg");
    XStringList* writerUpperPng = XImageWriter_imageFormatsForMimeType_2("IMAGE/PNG");
    const XString* format;
    int formatCount = 0;

    /* QImageIOHandler 基类 setOption() 为空操作，不能凭调用痕迹报告支持。 */
    XImageIOHandler_init(&baseHandler);
    memset(&baseOption, 0, sizeof(baseOption));
    baseOption.integer = 75;
    XImageIOHandler_setOption_base(&baseHandler,
                                   XImageIOHandlerOption_Quality,
                                   &baseOption);
    expect_true(!XImageIOHandler_supportsOption_base(
                    &baseHandler, XImageIOHandlerOption_Quality),
                "base image handler keeps unsupported options disabled");
    expect_true(!XImageIOHandler_option_base(
                    &baseHandler, XImageIOHandlerOption_Quality,
                    &baseOption),
                "base image handler option remains empty after setOption");
    XImageIOHandler_deinit_base(&baseHandler);

#if XIMAGECODEC_BMP_ON
    ++formatCount;
#endif
#if XIMAGECODEC_PNG_ON
    ++formatCount;
#endif
#if XIMAGECODEC_JPEG_ON
    formatCount += 3; /* Qt JPEG 插件公开 jpg、jpeg 与 jfif 三个键。 */
#endif
#if XIMAGECODEC_GIF_ON
    ++formatCount;
#endif
#if XIMAGECODEC_SVG_ON
    ++formatCount;
#endif
#if XIMAGEIOPLUGIN_ON
    expect_true(XImagePluginRegistry_pluginCount() >= 1,
                "built-in image plugin auto-registers");
#endif
    format = readerFormats ? (const XString*)XStringList_at_base((const XVector*)readerFormats, 0) : NULL;
    expect_true(readerFormats && XStringList_size_base((const XContainer*)readerFormats) == (size_t)formatCount && format &&
                XString_equals_utf8(format, "bmp", XChar_CaseSensitive), "reader registry exposes BMP codec");
#if XIMAGECODEC_JPEG_ON
    expect_true(readerFormats &&
                XStringList_contains_utf8(readerFormats, "jpg", XChar_CaseSensitive) &&
                XStringList_contains_utf8(readerFormats, "jpeg", XChar_CaseSensitive) &&
                XStringList_contains_utf8(readerFormats, "jfif", XChar_CaseSensitive),
                "reader registry exposes all Qt JPEG format keys");
#else
    expect_true(!readerFormats ||
                (!XStringList_contains_utf8(readerFormats, "jpg", XChar_CaseSensitive) &&
                 !XStringList_contains_utf8(readerFormats, "jpeg", XChar_CaseSensitive)),
                "reader registry omits disabled JPEG format keys");
#endif
    expect_true(readerMimes && XStringList_size_base((const XContainer*)readerMimes) ==
                (size_t)(formatCount -
#if XIMAGECODEC_JPEG_ON
                         2 /* jpg/jpeg/jfif 共享 image/jpeg MIME，Qt 列表去重 */
#else
                         0
#endif
                         ),
                "reader registry exposes built-in MIME types");
    expect_true(readerBmp && XStringList_size_base((const XContainer*)readerBmp) == 1,
                "reader MIME lookup resolves exact MIME type");
    expect_true(readerJpeg && XStringList_size_base((const XContainer*)readerJpeg) ==
                (XIMAGECODEC_JPEG_ON ? 3u : 0u),
                "reader JPEG MIME lookup exposes jpg/jpeg/jfif aliases");
    expect_true(readerUpperBmp && XStringList_size_base((const XContainer*)readerUpperBmp) == 0,
                "reader MIME lookup keeps Qt case-sensitive MIME semantics");
    format = writerFormats ? (const XString*)XStringList_at_base((const XVector*)writerFormats, 0) : NULL;
    expect_true(writerFormats && XStringList_size_base((const XContainer*)writerFormats) == (size_t)formatCount && format &&
                XString_equals_utf8(format, "bmp", XChar_CaseSensitive), "writer registry exposes BMP codec");
    expect_true(writerUnknown && XStringList_size_base((const XContainer*)writerUnknown) == 1,
                "writer MIME lookup returns the matching codec format");
    expect_true(writerJpeg && XStringList_size_base((const XContainer*)writerJpeg) ==
                (XIMAGECODEC_JPEG_ON ? 3u : 0u),
                "writer JPEG MIME lookup exposes jpg/jpeg/jfif aliases");
    expect_true(writerUpperPng && XStringList_size_base((const XContainer*)writerUpperPng) == 0,
                "writer MIME lookup keeps Qt case-sensitive MIME semantics");

    if (readerFormats) XStringList_delete_base((XClass*)readerFormats);
    if (readerMimes) XStringList_delete_base((XClass*)readerMimes);
    if (readerBmp) XStringList_delete_base((XClass*)readerBmp);
    if (readerJpeg) XStringList_delete_base((XClass*)readerJpeg);
    if (readerUpperBmp) XStringList_delete_base((XClass*)readerUpperBmp);
    if (writerFormats) XStringList_delete_base((XClass*)writerFormats);
    if (writerUnknown) XStringList_delete_base((XClass*)writerUnknown);
    if (writerJpeg) XStringList_delete_base((XClass*)writerJpeg);
    if (writerUpperPng) XStringList_delete_base((XClass*)writerUpperPng);
}

/* 对齐 Qt QImageReader::setDecideFormatFromContent 的独立状态语义。 */
static void test_image_reader_decide_format_state(void)
{
    XImageReader reader;
    XImageReader empty;
    XImageReader fileReader;
    XImageReader autoReader;
    XImageReader wrongSuffixReader;
    XImageReader strictReader;
    XImageReader contentReader;
    XImageReader extensionReader;
    XImageReader extensionAutoReader;
    XImage autoImage;
    XImage autoFixture;
    XImage wrongSuffixImage;
    XImage contentImage;
    XImage extensionImage;
    XImage extensionAutoImage;
    XImage dprImage;
    XImageReader dprReader;
    XString* autoFileName;
    XImage strictImage;
    XRect emptyRect;

    XImageReader_init(&reader);
    expect_true(XImageReader_autoDetectImageFormat(&reader),
                "image reader enables auto detection by default");
    XImageReader_setAutoDetectImageFormat(&reader, false);
    XImageReader_setFormat_2(&reader, "unsupported-format");
    XImageReader_setDecideFormatFromContent(&reader, true);
    expect_true(XImageReader_decideFormatFromContent(&reader),
                "image reader enables content-based format decision");
    expect_true(!XImageReader_autoDetectImageFormat(&reader),
                "content decision does not mutate auto-detect state");
    XImageReader_setDecideFormatFromContent(&reader, false);
    expect_true(!XImageReader_decideFormatFromContent(&reader),
                "image reader disables content-based format decision");
    XImageReader_deinit_base(&reader);

    XImageReader_init_file_2(&fileReader, "xgui_reader_lifecycle_missing.bmp", NULL);
    expect_true(XImageReader_device(&fileReader) != NULL,
                "file image reader exposes its internally-owned device");
    expect_true(XImageReader_fileName_2(&fileReader) &&
                strcmp(XImageReader_fileName_2(&fileReader),
                       "xgui_reader_lifecycle_missing.bmp") == 0,
                "file image reader preserves the configured file name");
    XImageReader_deinit_base(&fileReader);

    /* 自动探测开启且无显式格式时，Qt 先尝试文件后缀插件，再允许内置
       处理器按内容读取；该路径不能因 format 为空而提前报错。 */
    XImage_init_ex(&autoFixture, 1, 1, XImageFormat_ARGB32);
    XImage_setPixel(&autoFixture, 0, 0, 0xff0a1b2cu);
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_autodetect.bmp", NULL, -1),
                "writes fixture for automatic format detection");
    XImageReader_init_file_2(&autoReader, "xgui_reader_autodetect.bmp", NULL);
    XImage_init(&autoImage);
    expect_true(XImageReader_imageFormatValue(&autoReader) == XImageFormat_ARGB32,
                "reader imageFormat probes BMP V4 alpha header as ARGB32");
    {
        XSize autoSize;
        XImageReader_size(&autoReader, &autoSize);
        expect_true(autoSize.width == 1 && autoSize.height == 1,
                    "reader Size option reports BMP dimensions without decoding");
    }
    expect_true(XImageReader_read(&autoReader, &autoImage) &&
                XImage_width(&autoImage) == 1 &&
                XImage_height(&autoImage) == 1 &&
                XImage_pixel(&autoImage, 0, 0) == 0xff0a1b2cu,
                "reader with autodetect loads a BMP without explicit format");
    XImage_deinit_base(&autoImage);
    XImageReader_deinit_base(&autoReader);
    autoFileName = XString_create_utf8("xgui_reader_autodetect.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }

#if XIMAGECODEC_BMP_ON
    /* Qt 的 decideFormatFromContent 会忽略显式格式和文件扩展名，即使
       autoDetectImageFormat 同时关闭，也应按设备内容选择 BMP 处理器。 */
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_decide_content.bmp", "bmp", -1),
                "writes fixture for content-only format decision");
    XImageReader_init_file_2(&contentReader,
                             "xgui_reader_decide_content.bmp",
                             "unsupported-format");
    XImageReader_setAutoDetectImageFormat(&contentReader, false);
    XImageReader_setDecideFormatFromContent(&contentReader, true);
    XImage_init(&contentImage);
    expect_true(XImageReader_read(&contentReader, &contentImage) &&
                XImage_width(&contentImage) == 1 &&
                XImage_height(&contentImage) == 1 &&
                XImage_pixel(&contentImage, 0, 0) == 0xff0a1b2cu,
                "content decision ignores explicit format and reads by content");
    XImage_deinit_base(&contentImage);
    XImageReader_deinit_base(&contentReader);
    autoFileName = XString_create_utf8("xgui_reader_decide_content.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }
#endif

    /* Qt 读取成功后从文件基名末尾的 @2x..@9x 推导设备像素比。 */
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_autodetect@2x.bmp", NULL, -1),
                "writes fixture for @2x device pixel ratio detection");
    XImageReader_init_file_2(&dprReader,
                             "xgui_reader_autodetect@2x.bmp", NULL);
    XImage_init(&dprImage);
    expect_true(XImageReader_read(&dprReader, &dprImage) &&
                XImage_devicePixelRatio(&dprImage) == 2.0f,
                "reader assigns DPR from @2x file name suffix");
    XImage_deinit_base(&dprImage);
    XImageReader_deinit_base(&dprReader);
    autoFileName = XString_create_utf8("xgui_reader_autodetect@2x.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }

    /* Qt 在后缀处理器不能确认内容时继续按内容回退；错误的 .bmp 后缀
       不应阻止 PNG 内置处理器被选中。 */
#if XIMAGECODEC_ON && XIMAGECODEC_PNG_ON
    XImageReader_init_file_2(&wrongSuffixReader,
                             "xgui_reader_wrong_suffix.bmp", NULL);
    XImage_init(&wrongSuffixImage);
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_wrong_suffix.bmp", "png", -1),
                "writes wrong-suffix content fixture");
    expect_true(XImageReader_read(&wrongSuffixReader, &wrongSuffixImage) &&
                XImage_width(&wrongSuffixImage) == 1 &&
                XImage_height(&wrongSuffixImage) == 1 &&
                XImage_pixel(&wrongSuffixImage, 0, 0) == 0xff0a1b2cu,
                "reader falls back from wrong suffix to content format");
    XImage_deinit_base(&wrongSuffixImage);
    XImageReader_deinit_base(&wrongSuffixReader);
    autoFileName = XString_create_utf8("xgui_reader_wrong_suffix.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }
#endif
#if XIMAGECODEC_ON && XIMAGECODEC_BMP_ON
    /* QImageReader::setFileName() 在原路径不存在时按支持格式追加后缀；
       显式 bmp 应优先于排序后的其它格式尝试。 */
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_default_extension.bmp", "bmp", -1),
                "writes fixture for default extension probing");
    XImageReader_init_file_2(&extensionReader,
                             "xgui_reader_default_extension", "bmp");
    XImage_init(&extensionImage);
    expect_true(XImageReader_read(&extensionReader, &extensionImage) &&
                XImage_width(&extensionImage) == 1 &&
                XImage_height(&extensionImage) == 1 &&
                XImage_pixel(&extensionImage, 0, 0) == 0xff0a1b2cu,
                "reader probes supported extension when base path is missing");
    expect_true(XImageReader_fileName_2(&extensionReader) &&
                strcmp(XImageReader_fileName_2(&extensionReader),
                       "xgui_reader_default_extension.bmp") == 0,
                "reader exposes the successfully probed file name");
    XImage_deinit_base(&extensionImage);
    XImageReader_deinit_base(&extensionReader);
    XImageReader_init_file_2(&extensionAutoReader,
                             "xgui_reader_default_extension", NULL);
    XImage_init(&extensionAutoImage);
    expect_true(XImageReader_read(&extensionAutoReader, &extensionAutoImage) &&
                XImage_width(&extensionAutoImage) == 1 &&
                XImage_height(&extensionAutoImage) == 1 &&
                XImage_pixel(&extensionAutoImage, 0, 0) == 0xff0a1b2cu,
                "reader prioritizes the selected extension during autodetect");
    XImage_deinit_base(&extensionAutoImage);
    XImageReader_deinit_base(&extensionAutoReader);
    autoFileName = XString_create_utf8("xgui_reader_default_extension.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }

    /* 原路径带有未知后缀时，Qt 仍会在该路径后追加候选扩展名；成功后
       后缀探测必须依据实际打开的候选文件，而不能继续使用旧的 .bad。 */
    expect_true(XImage_save_2(&autoFixture,
                              "xgui_reader_default_extension.bad.bmp", "bmp", -1),
                "writes fixture after an unknown source suffix");
    XImageReader_init_file_2(&extensionAutoReader,
                             "xgui_reader_default_extension.bad", NULL);
    XImage_init(&extensionAutoImage);
    expect_true(XImageReader_fileName_2(&extensionAutoReader) &&
                strcmp(XImageReader_fileName_2(&extensionAutoReader),
                       "xgui_reader_default_extension.bad") == 0,
                "reader retains the configured name before probing");
    expect_true(XImageReader_format_2(&extensionAutoReader) &&
                strcmp(XImageReader_format_2(&extensionAutoReader), "bmp") == 0,
                "reader re-evaluates the selected suffix after extension probing");
    expect_true(XImageReader_read(&extensionAutoReader, &extensionAutoImage) &&
                XImage_width(&extensionAutoImage) == 1 &&
                XImage_height(&extensionAutoImage) == 1 &&
                XImage_pixel(&extensionAutoImage, 0, 0) == 0xff0a1b2cu,
                "reader probes an extension after an unknown source suffix");
    expect_true(XImageReader_fileName_2(&extensionAutoReader) &&
                strcmp(XImageReader_fileName_2(&extensionAutoReader),
                       "xgui_reader_default_extension.bad.bmp") == 0,
                "reader exposes the selected extension after probing");
    XImage_deinit_base(&extensionAutoImage);
    XImageReader_deinit_base(&extensionAutoReader);
    autoFileName = XString_create_utf8("xgui_reader_default_extension.bad.bmp");
    if (autoFileName) {
        XFile_remove_static(autoFileName);
        XString_delete_base((XClass*)autoFileName);
    }
#endif

    XImage_deinit_base(&autoFixture);

    /* Qt 在关闭自动探测且未指定格式时不会根据文件名后缀创建处理器，
       即使文件名带有 .bmp 也应直接返回 UnsupportedFormatError。 */
    XImageReader_init_file_2(&strictReader,
                             "xgui_reader_no_auto_missing.bmp", NULL);
    XImageReader_setAutoDetectImageFormat(&strictReader, false);
    XImage_init(&strictImage);
    expect_true(!XImageReader_canRead(&strictReader) &&
                XImageReader_error(&strictReader) ==
                    XImageReaderError_UnsupportedFormatError,
                "reader canRead reports unsupported format when autodetect is disabled");
    expect_true(!XImageReader_read(&strictReader, &strictImage),
                "reader without autodetect or explicit format refuses to read");
    expect_true(XImageReader_error(&strictReader) ==
                    XImageReaderError_UnsupportedFormatError,
                "reader without autodetect reports unsupported format");
    XImage_deinit_base(&strictImage);
    XImageReader_deinit_base(&strictReader);

    XImageReader_init(&empty);
    expect_true(strcmp(XImageReader_errorString_2(&empty), "Unknown error") == 0,
                "image reader returns Qt Unknown error text before any failure");
    {
        XString* initialError = XImageReader_errorString(&empty);
        expect_true(initialError &&
                    XString_equals_utf8(initialError, "Unknown error",
                                        XChar_CaseSensitive),
                    "image reader value errorString returns Unknown error text");
        if (initialError) XString_delete_base((XClass*)initialError);
    }
    expect_true(XImageReader_imageFormatValue(&empty) == XImageFormat_Invalid,
                "reader imageFormat is invalid without an ImageFormat option");
    XImageReader_setBackgroundColor(&empty, 0xff102030u);
    expect_true(XImageReader_backgroundColor(&empty) == 0,
                "image reader backgroundColor is invalid without a BackgroundColor option");
    expect_true(!XImageReader_supportsOption(
                    &empty, XImageIOHandlerOption_BackgroundColor),
                "image reader ignores backgroundColor when the handler does not support it");
    expect_true(!XImageReader_supportsOption(&empty,
                                             XImageIOHandlerOption_Size),
                "image reader supportsOption returns false without a handler");
    expect_true(XImageReader_loopCount(&empty) == -1,
                "image reader loopCount returns -1 when handler initialization fails");
    expect_true(XImageReader_imageCount(&empty) == -1,
                "image reader imageCount returns -1 when handler initialization fails");
    expect_true(XImageReader_nextImageDelay(&empty) == -1,
                "image reader nextImageDelay returns -1 when handler initialization fails");
    expect_true(XImageReader_currentImageNumber(&empty) == -1,
                "image reader currentImageNumber returns -1 when handler initialization fails");
    XImageReader_currentImageRect(&empty, &emptyRect);
    expect_true(emptyRect.x == 0 && emptyRect.y == 0 &&
                emptyRect.width == 0 && emptyRect.height == 0,
                "image reader currentImageRect returns an empty rect without a handler");
    XImageReader_deinit_base(&empty);
}

/* 对齐 Qt QImageReader::setAllocationLimit 的负值忽略与零值禁用语义。 */
static void test_image_reader_allocation_limit(void)
{
    const char* environment = getenv("QT_IMAGEIO_MAXALLOC");
    int original = XImageReader_allocationLimit();
    if (environment && (strcmp(environment, "7") == 0 ||
                        strcmp(environment, "0x7") == 0 ||
                        strcmp(environment, "0b111") == 0 ||
                        strcmp(environment, " 7 ") == 0 ||
                        strcmp(environment, "+7") == 0 ||
                        strcmp(environment, " +7 ") == 0 ||
                        strcmp(environment, " 0x7 ") == 0 ||
                        strcmp(environment, " 0b111 ") == 0))
    {
        expect_true(original == 7,
                    "image reader allocation limit honors QT_IMAGEIO_MAXALLOC");
        XImageReader_setAllocationLimit(8);
        expect_true(XImageReader_allocationLimit() == 7,
                    "environment allocation limit overrides setter values");
        XImageReader_setAllocationLimit(-1);
        expect_true(XImageReader_allocationLimit() == 7,
                    "environment allocation limit overrides negative setter path");
        XImageReader_setAllocationLimit(0);
        expect_true(XImageReader_allocationLimit() == 7,
                    "environment allocation limit keeps zero setter overridden");
        XImageReader_setAllocationLimit(original);
        return;
    }
    XImageReader_setAllocationLimit(8);
    expect_true(XImageReader_allocationLimit() == 8,
                "image reader allocation limit accepts nonnegative values");
    XImageReader_setAllocationLimit(-1);
    expect_true(XImageReader_allocationLimit() == 8,
                "image reader allocation limit ignores negative values");
    XImageReader_setAllocationLimit(0);
    expect_true(XImageReader_allocationLimit() == 0,
                "image reader allocation limit zero disables checking");
    XImageReader_setAllocationLimit(original);
}

static void test_image_plugin_registry_integration(void)
{
#if XIMAGEIOPLUGIN_ON
    TestImagePlugin* plugin;
    XImage source;
    XImage loaded;
    XImageWriter writer;
    XImageReader reader;
    XStringList* formats;
    XStringList* mimes;
    XString* format;
    const char* fileName = "xgui_mock_plugin.bin";
    XString* fileNameObject;
    bool ok;

    plugin = TestImagePlugin_create();
    expect_true(plugin != NULL, "mock plugin is created");
    if (!plugin) return;
    expect_true(XImagePluginRegistry_addPlugin((XImageIOPlugin*)plugin),
                "mock plugin registers into registry");
    fileNameObject = XString_create_utf8(fileName);
    expect_true(fileNameObject != NULL, "mock file name is created");
    if (!fileNameObject) {
        XImagePluginRegistry_removePlugin((XImageIOPlugin*)plugin);
        XImageIOPlugin_delete_base((XImageIOPlugin*)plugin);
        return;
    }
    g_mockImageWidth = 0;
    g_mockImageHeight = 0;

    /* Qt QImageWriter allows format capability queries before a device is
       assigned; the missing device is diagnosed only by canWrite()/write(). */
    XImageWriter_init(&writer);
    XImageWriter_setFormat_2(&writer, "mock");
    expect_true(XImageWriter_supportsOption(&writer,
                                            XImageIOHandlerOption_Description),
                "writer queries format options before device assignment");
    expect_true(!XImageWriter_canWrite(&writer) &&
                XImageWriter_error(&writer) == XImageWriterError_DeviceError,
                "writer still rejects canWrite without a device");
    XImageWriter_deinit_base(&writer);

    formats = XImageReader_supportedImageFormats();
    expect_true(formats != NULL &&
                XStringList_contains_utf8(formats, "mock", XChar_CaseInsensitive),
                "reader formats include registered plugin");
    mimes = XImageWriter_imageFormatsForMimeType_2("image/x-xinyue-mock");
    format = mimes && XStringList_size_base((const XContainer*)mimes) == 1
        ? (XString*)XStringList_at_base((XVector*)mimes, 0) : NULL;
    expect_true(mimes != NULL &&
                XStringList_size_base((const XContainer*)mimes) == 1 && format &&
                XString_equals_utf8(format, "mock", XChar_CaseSensitive),
                "writer MIME lookup resolves plugin format");
    if (formats) XStringList_delete_base((XClass*)formats);
    if (mimes) XStringList_delete_base((XClass*)mimes);

    XImage_init_ex(&source, 2, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xff112233u);
    XImage_setPixel(&source, 1, 0, 0xff445566u);
    XImage_setPixel(&source, 0, 1, 0xff778899u);
    XImage_setPixel(&source, 1, 1, 0xffaabbccu);

    XFile_remove_static(fileNameObject);
    XString_init(&g_mockExpectedWriterDescription);
    XString_assign_utf8(&g_mockExpectedWriterDescription,
                        "Title: Example\n\nAuthor: Alice");
    g_mockWriterDescriptionCheck = true;
    g_mockWriterDescriptionMatched = false;
    XImageWriter_init_file_2(&writer, fileName, "mock");
    expect_true(XImageWriter_canWrite(&writer), "plugin writer canWrite reports mock");
    expect_true(XImageWriter_supportsOption(&writer,
                                            XImageIOHandlerOption_Description),
                "writer exposes plugin Description option");
    XImageWriter_setText_2(&writer, "  Title  ", "  Example\t");
    XImageWriter_setText_2(&writer, " Author ", " Alice ");
    ok = XImageWriter_write(&writer, &source);
    expect_true(ok, "plugin writer receives image through registry");
    expect_true(g_mockWriterDescriptionMatched,
                "writer simplifies and forwards Description metadata");
    XImageWriter_deinit_base(&writer);
    g_mockWriterDescriptionCheck = false;
    XString_deinit_base((XClass*)&g_mockExpectedWriterDescription);

    XImage_init(&loaded);
    g_mockSupportsAnimation = true;
    g_mockSupportsTransformation = true;
    g_mockTransformation = XImageIOHandlerTransformation_Rotate90;
    XImageReader_init_file_2(&reader, fileName, "mock");
    expect_true(XImageReader_canRead(&reader), "plugin reader canRead reports mock");
    expect_true(XImageReader_supportsAnimation(&reader),
                "reader forwards plugin Animation support");
    expect_true(XImageReader_transformation(&reader) ==
                    XImageIOHandlerTransformation_Rotate90,
                "reader forwards plugin image transformation metadata");
    {
        XStringList* textKeys = XImageReader_textKeys(&reader);
        XString* titleKey = XString_create_utf8("Title");
        XString* authorKey = XString_create_utf8("Author");
        XString* descriptionKey = XString_create_utf8("Description");
        XString* rawKey = XString_create_utf8("RawKey\t");
        XString* bareKey = XString_create_utf8("BareText");
        XString* title = XImageReader_text(&reader, titleKey);
        XString* author = XImageReader_text(&reader, authorKey);
        XString* description = XImageReader_text(&reader, descriptionKey);
        XString* bare = XImageReader_text(&reader, bareKey);
        XString* missingKey = XString_create_utf8("Missing");
        XString* missing = XImageReader_text(&reader, missingKey);
        expect_true(textKeys && XStringList_size_base((const XContainer*)textKeys) == 5,
                    "reader parses handler Description keys");
        expect_true(textKeys &&
                    XString_equals_utf8((const XString*)XStringList_at_base(
                        (XVector*)textKeys, 0), "Author", XChar_CaseSensitive) &&
                    XString_equals_utf8((const XString*)XStringList_at_base(
                        (XVector*)textKeys, 1), "BareText", XChar_CaseSensitive) &&
                    XString_equals_utf8((const XString*)XStringList_at_base(
                        (XVector*)textKeys, 2), "Description", XChar_CaseSensitive) &&
                    XString_equals_utf8((const XString*)XStringList_at_base(
                        (XVector*)textKeys, 4), "Title", XChar_CaseSensitive),
                    "reader text keys follow QMap sort order");
        expect_true(textKeys && rawKey && XStringList_contains(textKeys, rawKey,
                                                               XChar_CaseSensitive),
                    "reader preserves ordinary key whitespace like Qt");
        expect_true(title && XString_equals_utf8(title, "Sunset", XChar_CaseSensitive) &&
                    author && XString_equals_utf8(author, "Alice", XChar_CaseSensitive) &&
                    description && XString_equals_utf8(description, "demo", XChar_CaseSensitive) &&
                    bare && XString_equals_utf8(bare, "areText", XChar_CaseSensitive),
                    "reader text values are simplified");
        expect_true(missing && XString_isEmpty_base((const XContainer*)missing),
                    "reader missing text key returns empty string");
        if (textKeys) XStringList_delete_base((XClass*)textKeys);
        if (titleKey) XString_delete_base((XClass*)titleKey);
        if (authorKey) XString_delete_base((XClass*)authorKey);
        if (descriptionKey) XString_delete_base((XClass*)descriptionKey);
        if (rawKey) XString_delete_base((XClass*)rawKey);
        if (bareKey) XString_delete_base((XClass*)bareKey);
        if (missingKey) XString_delete_base((XClass*)missingKey);
        if (title) XString_delete_base((XClass*)title);
        if (author) XString_delete_base((XClass*)author);
        if (description) XString_delete_base((XClass*)description);
        if (bare) XString_delete_base((XClass*)bare);
        if (missing) XString_delete_base((XClass*)missing);
    }
    ok = XImageReader_read(&reader, &loaded);
    expect_true(ok, "plugin reader loads image through registry");
    expect_true(XImage_width(&loaded) == 2 && XImage_height(&loaded) == 2,
                "plugin round trip preserves dimensions");
    expect_true(XImage_pixel(&loaded, 0, 0) == 0xff112233u &&
                XImage_pixel(&loaded, 1, 0) == 0xff445566u &&
                XImage_pixel(&loaded, 0, 1) == 0xff778899u &&
                XImage_pixel(&loaded, 1, 1) == 0xffaabbccu,
                "plugin round trip preserves pixels");

    XImageReader_setAutoTransform(&reader, true);
    expect_true(XImageReader_read(&reader, &loaded) &&
                    XImage_pixel(&loaded, 0, 0) == 0xff778899u &&
                    XImage_pixel(&loaded, 1, 0) == 0xff112233u &&
                    XImage_pixel(&loaded, 0, 1) == 0xffaabbccu &&
                    XImage_pixel(&loaded, 1, 1) == 0xff445566u,
                "reader autoTransform rotates plugin image clockwise");

    XImageReader_deinit_base(&reader);
    g_mockSupportsAnimation = false;
    g_mockSupportsTransformation = false;
    g_mockTransformation = XImageIOHandlerTransformation_None;
    XImage_deinit_base(&loaded);
    XImage_deinit_base(&source);

    expect_true(XImagePluginRegistry_removePlugin((XImageIOPlugin*)plugin),
                "mock plugin is removed from registry");
    XImageIOPlugin_delete_base((XImageIOPlugin*)plugin);
    /* Qt 先尝试外部插件，再回退到内置处理器；同名 bmp 插件应覆盖内置 BMP。 */
    {
        TestImagePlugin* overridePlugin = TestImagePlugin_create();
        XString* overrideKey = NULL;
        XImage overrideLoaded;
        XImageReader overrideReader;
        XString* overrideFileName = XString_create_utf8("xgui_override.bmp");
        XFile* overrideSeed = NULL;
        bool overrideAdded = false;
        if (overridePlugin) {
            overrideKey = (XString*)XStringList_at_base(
                (XVector*)overridePlugin->m_keys, 0);
            if (overrideKey)
                XString_assign_utf8(overrideKey, "bmp");
            overrideAdded = XImagePluginRegistry_addPlugin(
                (XImageIOPlugin*)overridePlugin);
        }
        overrideSeed = overrideFileName ? XFile_create_2(overrideFileName) : NULL;
        if (overrideSeed && XIODevice_open_base((XIODevice*)overrideSeed,
                                                XIODevice_WriteOnly)) {
            char seed = 'x';
            (void)XIODevice_write_1((XIODevice*)overrideSeed, &seed, 1);
            XIODevice_close_base((XIODevice*)overrideSeed);
        }
        expect_true(overrideSeed != NULL,
                    "writes fixture for external plugin precedence");
        if (overrideSeed)
            XClass_delete_base((XClass*)overrideSeed);
        g_mockImageWidth = 1;
        g_mockImageHeight = 1;
        g_mockImagePixels[0] = 0xffa1b2c3u;
        XImage_init(&overrideLoaded);
        XImageReader_init_file_2(&overrideReader, "xgui_override.bmp", "bmp");
        expect_true(overrideAdded && XImageReader_read(&overrideReader, &overrideLoaded) &&
                    XImage_width(&overrideLoaded) == 1 &&
                    XImage_height(&overrideLoaded) == 1 &&
                    XImage_pixel(&overrideLoaded, 0, 0) == 0xffa1b2c3u,
                    "external same-format plugin overrides builtin BMP handler");
        XImageReader_deinit_base(&overrideReader);
        XImage_deinit_base(&overrideLoaded);
        if (overrideAdded)
            XImagePluginRegistry_removePlugin((XImageIOPlugin*)overridePlugin);
        if (overridePlugin)
            XImageIOPlugin_delete_base((XImageIOPlugin*)overridePlugin);
        if (overrideFileName) {
            XFile_remove_static(overrideFileName);
            XString_delete_base((XClass*)overrideFileName);
        }
    }
    /* Qt 对外部后缀处理器的 canRead() 失败继续按内容回退；验证同名
       bmp 插件拒绝内容时不会阻断内置 BMP 处理器。 */
    {
        TestImagePlugin* suffixPlugin = TestImagePlugin_create();
        XString* suffixKey = NULL;
        XImage fallbackSource;
        XImage fallbackLoaded;
        XImageReader fallbackReader;
        const char* suffixFile = "xgui_external_wrong_suffix.bmp";
        bool suffixAdded = false;
        if (suffixPlugin) {
            suffixKey = (XString*)XStringList_at_base(
                (XVector*)suffixPlugin->m_keys, 0);
            if (suffixKey)
                XString_assign_utf8(suffixKey, "bmp");
            suffixAdded = XImagePluginRegistry_addPlugin(
                (XImageIOPlugin*)suffixPlugin);
        }
        XImage_init_ex(&fallbackSource, 1, 1, XImageFormat_ARGB32);
        XImage_setPixel(&fallbackSource, 0, 0, 0xff13579bu);
        expect_true(XImage_save_2(&fallbackSource, suffixFile, "bmp", -1),
                    "writes fixture for external suffix canRead fallback");
        g_mockImageWidth = 1;
        g_mockImageHeight = 1;
        g_mockImagePixels[0] = 0xff2468acu;
        g_mockSuffixOnlyCapabilities = true;
        g_mockRejectSuffixCanRead = true;
        XImage_init(&fallbackLoaded);
        XImageReader_init_file_2(&fallbackReader, suffixFile, NULL);
        expect_true(suffixAdded && XImageReader_read(&fallbackReader, &fallbackLoaded) &&
                    XImage_width(&fallbackLoaded) == 1 &&
                    XImage_height(&fallbackLoaded) == 1 &&
                    XImage_pixel(&fallbackLoaded, 0, 0) == 0xff13579bu,
                    "failed external suffix canRead falls back to content handler");
        XImageReader_deinit_base(&fallbackReader);
        XImage_deinit_base(&fallbackLoaded);
        XImage_deinit_base(&fallbackSource);
        g_mockSuffixOnlyCapabilities = false;
        g_mockRejectSuffixCanRead = false;
        if (suffixAdded)
            XImagePluginRegistry_removePlugin((XImageIOPlugin*)suffixPlugin);
        if (suffixPlugin)
            XImageIOPlugin_delete_base((XImageIOPlugin*)suffixPlugin);
        {
            XString* suffixName = XString_create_utf8(suffixFile);
            if (suffixName) {
                XFile_remove_static(suffixName);
                XString_delete_base((XClass*)suffixName);
            }
        }
    }
    /* Qt 在显式后缀插件 create() 失败后继续尝试其它插件和内置处理器；
       该路径不能因外部插件工厂临时失败而误报 UnsupportedFormatError。 */
    {
        TestImagePlugin* createFailPlugin = TestImagePlugin_create();
        XString* createFailKey = NULL;
        XImage fallbackSource;
        XImage fallbackLoaded;
        XImage strictPluginLoaded;
        XImageReader createFailReader;
        XImageReader strictPluginReader;
        const char* createFailFile = "xgui_external_create_fail.bmp";
        bool createFailAdded = false;
        if (createFailPlugin) {
            createFailKey = (XString*)XStringList_at_base(
                (XVector*)createFailPlugin->m_keys, 0);
            if (createFailKey)
                XString_assign_utf8(createFailKey, "bmp");
            createFailAdded = XImagePluginRegistry_addPlugin(
                (XImageIOPlugin*)createFailPlugin);
        }
        XImage_init_ex(&fallbackSource, 1, 1, XImageFormat_ARGB32);
        XImage_setPixel(&fallbackSource, 0, 0, 0xff0badf0u);
        expect_true(XImage_save_2(&fallbackSource, createFailFile, "bmp", -1),
                    "writes fixture for external plugin create failure");
        g_mockImageWidth = 1;
        g_mockImageHeight = 1;
        g_mockImagePixels[0] = 0xff2468acu;
        g_mockRejectCreate = true;
        XImage_init(&fallbackLoaded);
        XImageReader_init_file_2(&createFailReader,
                                 createFailFile, NULL);
        expect_true(createFailAdded &&
                    XImageReader_read(&createFailReader, &fallbackLoaded) &&
                    XImage_width(&fallbackLoaded) == 1 &&
                    XImage_height(&fallbackLoaded) == 1 &&
                    XImage_pixel(&fallbackLoaded, 0, 0) == 0xff0badf0u,
                    "failed external create falls back to builtin BMP handler");
        XImageReader_init_file_2(&strictPluginReader,
                                 createFailFile, "bmp");
        XImageReader_setAutoDetectImageFormat(&strictPluginReader, false);
        XImage_init(&strictPluginLoaded);
        expect_true(createFailAdded &&
                    XImageReader_read(&strictPluginReader, &strictPluginLoaded) &&
                    XImage_pixel(&strictPluginLoaded, 0, 0) == 0xff0badf0u,
                    "strict explicit format falls back to builtin after first plugin create failure");
        XImage_deinit_base(&strictPluginLoaded);
        XImageReader_deinit_base(&strictPluginReader);
        g_mockRejectCreate = false;
        XImageReader_deinit_base(&createFailReader);
        XImage_deinit_base(&fallbackLoaded);
        XImage_deinit_base(&fallbackSource);
        if (createFailAdded)
            XImagePluginRegistry_removePlugin((XImageIOPlugin*)createFailPlugin);
        if (createFailPlugin)
            XImageIOPlugin_delete_base((XImageIOPlugin*)createFailPlugin);
        {
            XString* createFailName = XString_create_utf8(createFailFile);
            if (createFailName) {
                XFile_remove_static(createFailName);
                XString_delete_base((XClass*)createFailName);
            }
        }
    }
    /* Qt qimagereader.cpp 在 capabilities()/create() 后恢复非顺序设备位置。 */
    {
        XFile* seedFile = XFile_create_2(fileNameObject);
        XFile* probeFile = XFile_create_2(fileNameObject);
        XString* detected;
        int64_t before;
        int64_t after;
        char seed = 'x';
        if (seedFile && XIODevice_open_base((XIODevice*)seedFile, XIODevice_WriteOnly)) {
            (void)XIODevice_write_1((XIODevice*)seedFile, &seed, 1);
            XIODevice_close_base((XIODevice*)seedFile);
        }
        if (seedFile) XClass_delete_base((XClass*)seedFile);
        expect_true(probeFile &&
                    XIODevice_open_base((XIODevice*)probeFile, XIODevice_ReadOnly),
                    "plugin probe file opens for position contract");
        if (probeFile && XIODevice_isOpen((XIODevice*)probeFile)) {
            /* 重新注册测试插件，使其 capabilities() 消费一个字节。 */
            plugin = TestImagePlugin_create();
            expect_true(plugin && XImagePluginRegistry_addPlugin((XImageIOPlugin*)plugin),
                        "consuming probe plugin registers");
            g_mockImageWidth = 1;
            g_mockImageHeight = 1;
            g_mockConsumeCapabilities = true;
            before = XIODevice_pos_base((XIODevice*)probeFile);
            detected = XImagePluginRegistry_detectReadFormat((XIODevice*)probeFile);
            after = XIODevice_pos_base((XIODevice*)probeFile);
            expect_true(detected && XString_equals_utf8(detected, "mock", XChar_CaseInsensitive),
                        "content probe finds consuming plugin");
            expect_true(before == after,
                        "plugin capability probing preserves non-sequential device position");
            if (detected) XString_delete_base((XClass*)detected);
            g_mockConsumeCapabilities = false;

            /* capabilities() 仍声明支持时，Qt 还要求 create()/canRead()
               真正接受内容；测试处理器拒绝空尺寸，探测结果必须为空。 */
            g_mockImageWidth = 0;
            g_mockImageHeight = 0;
            before = XIODevice_pos_base((XIODevice*)probeFile);
            detected = XImagePluginRegistry_detectReadFormat((XIODevice*)probeFile);
            after = XIODevice_pos_base((XIODevice*)probeFile);
            expect_true(detected && XContainer_isEmpty_base((const XContainer*)detected),
                        "content probe rejects plugin whose handler cannot read");
            expect_true(before == after,
                        "failed handler probe preserves non-sequential device position");
            if (detected) XString_delete_base((XClass*)detected);
            XImagePluginRegistry_removePlugin((XImageIOPlugin*)plugin);
            XImageIOPlugin_delete_base((XImageIOPlugin*)plugin);
            XIODevice_close_base((XIODevice*)probeFile);
        }
        if (probeFile) XClass_delete_base((XClass*)probeFile);
    }
    XFile_remove_static(fileNameObject);
    XString_delete_base((XClass*)fileNameObject);

    /* Qt 的内置 imageformats 处理器不属于可卸载插件；清空显式注册项后，
       下次查询应自动恢复内置插件，并且 removePlugin() 不能将其删除。 */
    {
        XImageIOPlugin* builtin = XImageBuiltinPlugin_instance();
        XString* bmpFormat = XString_create_utf8("bmp");
        XImagePluginRegistry_clear();
        expect_true(builtin &&
                    !XImagePluginRegistry_removePlugin(builtin),
                    "built-in image plugin cannot be removed");
        expect_true(XImagePluginRegistry_pluginCount() >= 1 &&
                    bmpFormat &&
                    XImagePluginRegistry_supportsReadFormat(bmpFormat),
                    "cleared registry restores built-in image plugin");
        if (bmpFormat) XString_delete_base((XClass*)bmpFormat);
    }
#else
    expect_true(true, "plugin registry integration is disabled by XIMAGEIOPLUGIN_ON");
#endif /* XIMAGEIOPLUGIN_ON */
}

static void test_image_codec_round_trip(void)
{
    XImage source, decoded;
    XByteArray* encoded;
    const XImageCodecFormat formats[] = {XImageCodecFormat_Bmp, XImageCodecFormat_Png,
#if XIMAGECODEC_JPEG_ON
                                         XImageCodecFormat_Jpeg,
#endif
                                         XImageCodecFormat_Gif, XImageCodecFormat_Svg};
    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xffff0000u); XImage_setPixel(&source, 1, 0, 0xff00ff00u); XImage_setPixel(&source, 2, 0, 0xff0000ffu);
    XImage_setPixel(&source, 0, 1, 0x80402010u); XImage_setPixel(&source, 1, 1, 0xffffffffu); XImage_setPixel(&source, 2, 1, 0xff102030u);
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        encoded = XByteArray_create(); XImage_init(&decoded);
        expect_true(encoded && XImageCodec_canEncode(formats[i]) && XImageCodec_encode(&source, formats[i], -1, encoded), "codec encodes format independently");
        expect_true(encoded && XImageCodec_detect(XByteArray_data(encoded), XByteArray_size_base((const XContainer*)encoded)) == formats[i], "codec detects encoded format");
        {
            int pw = 0, ph = 0;
            expect_true(encoded &&
                        XImageCodec_probeSize(XByteArray_data(encoded),
                                              XByteArray_size_base((const XContainer*)encoded),
                                              XImageCodecFormat_Unknown, &pw, &ph) &&
                        pw == 3 && ph == 2,
                        "codec probeSize reads dimensions without full decode");
        }
        expect_true(encoded && XImageCodec_decode(XByteArray_data(encoded), XByteArray_size_base((const XContainer*)encoded), XImageCodecFormat_Unknown, &decoded), "codec decodes format independently");
        expect_true(XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2, "codec round trip preserves dimensions");
        XImage_deinit_base(&decoded); if (encoded) XByteArray_delete_base((XClass*)encoded);
    }
    XImage_init(&decoded);
    expect_true(XImage_save_2(&source, "xgui_codec.png", "png", -1), "XImage delegates PNG file save to codec");
    expect_true(XImage_load_2(&decoded, "xgui_codec.png", "png") && XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2,
                "XImage delegates PNG file load to codec");
    { XString* codecFile = XString_create_utf8("xgui_codec.png"); XFile_remove_static(codecFile); if (codecFile) XString_delete_base((XClass*)codecFile); }
    XImage_deinit_base(&decoded);
    XImage_deinit_base(&source);
}


/* 逐像素比对：所有像素必须与期望表完全一致 */
static bool pixels_equal_exact(const XImage* image, const uint32_t* expected,
                               int width, int height)
{
    if (!image || XImage_isNull(image) ||
        XImage_width(image) != width || XImage_height(image) != height)
        return false;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (XImage_pixel(image, x, y) != expected[y * width + x])
                return false;
    return true;
}

static void test_codec_pixel_round_trip(void)
{
    /* 各格式独立像素级往返：BMP/PNG/SVG 逐像素一致（含 Alpha），
       GIF 使用量化调色板，仅校验尺寸 */
    static const uint32_t px[6] = {
        0xffff0000u, 0xff00ff01u, 0x800000ffu,
        0x80402010u, 0xffffffffu, 0xff102030u
    };
    XImage source, decoded;
    XByteArray* encoded = NULL;
    const XImageCodecFormat exactFormats[] = {
        XImageCodecFormat_Bmp, XImageCodecFormat_Png, XImageCodecFormat_Svg
    };

    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            XImage_setPixel(&source, x, y, px[y * 3 + x]);

    for (size_t i = 0; i < sizeof(exactFormats) / sizeof(exactFormats[0]); ++i) {
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&source, exactFormats[i], -1, encoded),
                    "codec pixel-encodes format");
        expect_true(encoded &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       exactFormats[i], &decoded),
                    "codec pixel-decodes format with explicit hint");
        expect_true(pixels_equal_exact(&decoded, px, 3, 2),
                    "codec pixel-exact round trip (explicit hint)");
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
    }

    /* GIF：量化解码后仅校验尺寸与非空 */
    encoded = XByteArray_create();
    XImage_init(&decoded);
    expect_true(encoded && XImageCodec_encode(&source, XImageCodecFormat_Gif, -1, encoded) &&
                XImageCodec_decode(XByteArray_data(encoded),
                                   XByteArray_size_base((const XContainer*)encoded),
                                   XImageCodecFormat_Unknown, &decoded),
                "GIF 往返可解");
    expect_true(XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2,
                "GIF 往返保持尺寸");

    XImage_deinit_base(&decoded);
    if (encoded) XByteArray_delete_base((XClass*)encoded);

#if XIMAGECODEC_JPEG_ON
    /* JPEG：有损格式，使用 24x16 平滑渐变做往返，容差校验单通道误差 */
    {
        XImage jpegSrc;
        XImage_init_ex(&jpegSrc, 24, 16, XImageFormat_ARGB32);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 24; ++x) {
                uint32_t r = (uint32_t)(x * 255 / 23);
                uint32_t g = (uint32_t)(y * 255 / 15);
                uint32_t b = (uint32_t)((x + y) * 127 / 38);
                XImage_setPixel(&jpegSrc, x, y, 0xff000000u | (r << 16) | (g << 8) | b);
            }
        }
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&jpegSrc, XImageCodecFormat_Jpeg, 75, encoded) &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       XImageCodecFormat_Unknown, &decoded),
                    "JPEG 有损往返可解");
        expect_true(XImage_width(&decoded) == 24 && XImage_height(&decoded) == 16,
                    "JPEG 有损往返保持尺寸");
        if (!XImage_isNull(&decoded) && XImage_width(&decoded) == 24 &&
            XImage_height(&decoded) == 16) {
            int maxErr = 0;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    uint32_t a = XImage_pixel(&jpegSrc, x, y);
                    uint32_t d = XImage_pixel(&decoded, x, y);
                    int dr = (int)((a >> 16) & 0xffu) - (int)((d >> 16) & 0xffu);
                    int dg = (int)((a >> 8) & 0xffu) - (int)((d >> 8) & 0xffu);
                    int db = (int)(a & 0xffu) - (int)(d & 0xffu);
                    if (dr < 0) dr = -dr;
                    if (dg < 0) dg = -dg;
                    if (db < 0) db = -db;
                    if (dr > maxErr) maxErr = dr;
                    if (dg > maxErr) maxErr = dg;
                    if (db > maxErr) maxErr = db;
                }
            }
            expect_true(maxErr <= 32, "JPEG 有损误差在容差范围内");
            if (maxErr > 32)
                XERROR_PRINTF("JPEG 最大通道误差=%d\n", maxErr);
        }
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
        XImage_deinit_base(&jpegSrc);
    }
#endif /* XIMAGECODEC_JPEG_ON */
    XImage_deinit_base(&source);
}

#if XIMAGECODEC_BMP_ON
static void bmp_le16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void bmp_le32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* 构造 BMP 测试字节，所有未显式填写区域补零。 */
static XByteArray* bmp_make(size_t total, uint32_t offset, uint32_t dib,
                            uint32_t width, int32_t height, uint16_t planes,
                            uint16_t bpp, uint32_t compression)
{
    XByteArray* b;
    uint8_t* d;
    b = XByteArray_create();
    if (!b) return NULL;
    if (!XByteArray_resize_base((XVector*)b, total)) {
        XByteArray_delete_base((XClass*)b);
        return NULL;
    }
    d = XByteArray_data(b);
    memset(d, 0, total);
    d[0] = 'B';
    d[1] = 'M';
    bmp_le32(d + 2, (uint32_t)total);
    bmp_le32(d + 10, offset);
    bmp_le32(d + 14, dib);
    if (dib == 12) {
        if (total >= 26u) {
            bmp_le16(d + 18, (uint16_t)width);
            bmp_le16(d + 20, (uint16_t)(int16_t)height);
            bmp_le16(d + 22, planes);
            bmp_le16(d + 24, bpp);
        }
    } else if (total >= 34u) {
        bmp_le32(d + 18, width);
        bmp_le32(d + 22, (uint32_t)height);
        bmp_le16(d + 26, planes);
        bmp_le16(d + 28, bpp);
        bmp_le32(d + 30, compression);
    }
    return b;
}

static void bmp_expect_reject(XByteArray* b, const char* name)
{
    bool rejected = false;
    if (b) {
        XImage out;
        XImage_init(&out);
        rejected = !XImageCodec_decode(XByteArray_data(b),
                                       XByteArray_size_base((const XContainer*)b),
                                       XImageCodecFormat_Bmp, &out);
        XImage_deinit_base(&out);
    }
    expect_true(rejected, name);
    if (b) XByteArray_delete_base((XClass*)b);
}

static void test_codec_bmp_malformed(void)
{
    XByteArray* b;
    XImage out;

    XImage_init(&out);
    expect_true(!XImageCodec_decode((const uint8_t*)"BM", 2,
                                    XImageCodecFormat_Bmp, &out),
                "BMP header smaller than 26 bytes rejected");
    XImage_deinit_base(&out);

    bmp_expect_reject(bmp_make(20, 0, 40, 1, 1, 1, 24, 0),
                      "truncated DIB header rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 0, 1, 1, 24, 0),
                      "zero width BMP rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 0, 1, 24, 0),
                      "zero height BMP rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 1, 2, 24, 0),
                      "invalid planes count rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 1, 1, 15, 0),
                      "unsupported bit depth rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 1, 1, 2, 0),
                      "Qt BMP reader rejects 2-bit depth");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 1, 1, 24, 99),
                      "unknown compression type rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 1, 1, 1, 32, 6),
                      "Qt BMP reader rejects alpha-bitfields compression");
    bmp_expect_reject(bmp_make(70, 54, 40, 1, 1, 1, 24, 1),
                      "RLE8 mismatch with 24 bit depth rejected");
    bmp_expect_reject(bmp_make(60, 54, 40, 1, 1, 1, 32, 3),
                      "BITFIELDS without mask block rejected");
    bmp_expect_reject(bmp_make(60, 54, 40, 1, 1, 1, 8, 0),
                      "truncated 8-bit palette rejected");
    bmp_expect_reject(bmp_make(56, 54, 40, 2, 1, 1, 24, 0),
                      "truncated pixel data rejected");
    bmp_expect_reject(bmp_make(58, 54, 40, 16385, 1, 1, 24, 0),
                      "oversized BMP dimensions rejected");

    /* OS/2 core header 的宽高是 qint16；负宽度非法，负高度表示顶行序。 */
    bmp_expect_reject(bmp_make(34, 26, 12, 0x8000u, 1, 1, 24, 0),
                      "negative OS/2 core width rejected");
    b = bmp_make(34, 26, 12, 1, -2, 1, 24, 0);
    if (b) {
        uint8_t* d = XByteArray_data(b);
        /* 顶行红色、次行蓝色；每行 24 位像素按 4 字节对齐。 */
        d[26] = 0x00; d[27] = 0x00; d[28] = 0xff;
        d[30] = 0xff; d[31] = 0x00; d[32] = 0x00;
    }
    XImage_init(&out);
    expect_true(b && XImageCodec_decode(XByteArray_data(b),
                                        XByteArray_size_base((const XContainer*)b),
                                        XImageCodecFormat_Bmp, &out) &&
                XImage_width(&out) == 1 && XImage_height(&out) == 2 &&
                XImage_pixel(&out, 0, 0) == 0xffff0000u &&
                XImage_pixel(&out, 0, 1) == 0xff0000ffu,
                "negative OS/2 core height preserves top-down rows");
    XImage_deinit_base(&out);
    if (b) XByteArray_delete_base((XClass*)b);

#if XIMAGECODEC_BMP_INDEXED_ON && XIMAGECODEC_BMP_RLE_ON
    /* Qt 对超出当前行剩余宽度的 RLE 行程按行钳制，而不是解码失败。 */
    b = bmp_make(824, 822, 40, 2, 1, 1, 8, 1);
    if (b) {
        uint8_t* d = XByteArray_data(b);
        d[822] = 3;   /* 计数超过宽度，余下应被钳到当前行尾 */
        d[823] = 1;
    }
    XImage_init(&out);
    expect_true(b && XImageCodec_decode(XByteArray_data(b),
                                        XByteArray_size_base((const XContainer*)b),
                                        XImageCodecFormat_Bmp, &out) &&
                XImage_width(&out) == 2 && XImage_height(&out) == 1 &&
                XImage_pixelIndex(&out, 0, 0) == 1 &&
                XImage_pixelIndex(&out, 1, 0) == 1,
                "RLE8 overrun clamps to current row like Qt");
    XImage_deinit_base(&out);
    if (b) XByteArray_delete_base((XClass*)b);
#endif /* XIMAGECODEC_BMP_INDEXED_ON && XIMAGECODEC_BMP_RLE_ON */

#if XIMAGECODEC_BMP_INDEXED_ON
    /* Qt 使用 biClrUsed 限制调色板读取数量；1 位图输出 Mono，并在
       颜色表亮度顺序相反时翻转存储位与颜色表。 */
    b = bmp_make(64, 60, 40, 8, 1, 1, 1, 0);
    if (b) {
        uint8_t* d = XByteArray_data(b);
        bmp_le32(d + 46, 2);
        d[54] = 0xff; d[55] = 0xff; d[56] = 0xff;
        d[57] = 0; d[58] = 0; d[59] = 0;
        d[60] = 0x80; /* 索引 1：黑色 */
    }
    XImage_init(&out);
    expect_true(b && XImageCodec_decode(XByteArray_data(b),
                                        XByteArray_size_base((const XContainer*)b),
                                        XImageCodecFormat_Bmp, &out) &&
                XImage_format(&out) == XImageFormat_Mono &&
                XImage_colorCount(&out) == 2 &&
                XImage_pixel(&out, 0, 0) == 0xff000000u &&
                XImage_pixel(&out, 1, 0) == 0xffffffffu,
                "1-bit BMP maps to Mono and preserves palette order");
    XImage_deinit_base(&out);
    if (b) XByteArray_delete_base((XClass*)b);

    b = bmp_make(64, 60, 40, 8, 1, 1, 1, 0);
    if (b) {
        uint8_t* d = XByteArray_data(b);
        bmp_le32(d + 46, 2);
        d[54] = 0; d[55] = 0; d[56] = 0;
        d[57] = 0xff; d[58] = 0xff; d[59] = 0xff;
        d[60] = 0x40; /* 索引 0/1：较暗颜色在首像素，Qt 会交换索引与颜色表 */
    }
    XImage_init(&out);
    expect_true(b && XImageCodec_decode(XByteArray_data(b),
                                        XByteArray_size_base((const XContainer*)b),
                                        XImageCodecFormat_Bmp, &out) &&
                XImage_format(&out) == XImageFormat_Mono &&
                XImage_pixel(&out, 0, 0) == 0xff000000u &&
                XImage_pixel(&out, 1, 0) == 0xffffffffu,
                "1-bit BMP swaps reversed palette polarity like Qt");
    XImage_deinit_base(&out);
    if (b) XByteArray_delete_base((XClass*)b);

    b = bmp_make(61, 57, 40, 8, 1, 1, 1, 0);
    if (b) {
        uint8_t* d = XByteArray_data(b);
        bmp_le32(d + 46, 1);
        d[54] = 0; d[55] = 0; d[56] = 0;
        d[57] = 0;
    }
    XImage_init(&out);
    expect_true(b && XImageCodec_decode(XByteArray_data(b),
                                        XByteArray_size_base((const XContainer*)b),
                                        XImageCodecFormat_Bmp, &out) &&
                XImage_format(&out) == XImageFormat_Mono &&
                XImage_colorCount(&out) == 1 &&
                XImage_pixel(&out, 0, 0) == 0xff000000u,
                "1-bit BMP honors a reduced biClrUsed palette");
    XImage_deinit_base(&out);
    if (b) XByteArray_delete_base((XClass*)b);
#endif /* XIMAGECODEC_BMP_INDEXED_ON */
}

static void test_codec_bmp_alpha_semantics(void)
{
    XByteArray* plain;
    XByteArray* v4;
    XImage out;

    /* BI_RGB 32 位：Qt 将最高字节视为未使用，结果必须保持不透明。 */
    plain = bmp_make(58, 54, 40, 1, 1, 1, 32, 0);
    if (plain) {
        uint8_t* data = XByteArray_data(plain);
        data[54] = 0x33;
        data[55] = 0x22;
        data[56] = 0x11;
        data[57] = 0x01;
    }
    XImage_init(&out);
    expect_true(plain && XImageCodec_decode(
                    XByteArray_data(plain),
                    XByteArray_size_base((const XContainer*)plain),
                    XImageCodecFormat_Bmp, &out) &&
                XImage_pixel(&out, 0, 0) == 0xff112233u,
                "plain BI_RGB 32-bit BMP keeps pixels opaque");
    XImage_deinit_base(&out);
    if (plain) XByteArray_delete_base((XClass*)plain);

    /* V4 头显式声明 0xff000000 Alpha 掩码时，Qt 才读取最高字节。 */
    v4 = bmp_make(126, 122, 108, 1, 1, 1, 32, 0);
    if (v4) {
        uint8_t* data = XByteArray_data(v4);
        bmp_le32(data + 14 + 52, 0xff000000u);
        data[122] = 0x33;
        data[123] = 0x22;
        data[124] = 0x11;
        data[125] = 0x01;
    }
    XImage_init(&out);
    expect_true(v4 && XImageCodec_decode(
                    XByteArray_data(v4),
                    XByteArray_size_base((const XContainer*)v4),
                    XImageCodecFormat_Bmp, &out) &&
                XImage_pixel(&out, 0, 0) == 0x01112233u,
                "V4 BI_RGB BMP honors an explicit alpha mask");
    XImage_deinit_base(&out);
    if (v4) XByteArray_delete_base((XClass*)v4);
}

/**
 * @brief 校验 QImageReader 的“轻量 canRead、严格 read”两阶段语义。
 * @details BMP 的文件头足以让 canRead() 返回 true，但截断的像素区必须由
 *          read() 拒绝，并保持输出图像为空，不能泄漏一个部分构造的图像。
 */
static void test_image_reader_malformed_bmp(void)
{
    const char* path = "xgui_reader_truncated.bmp";
    XByteArray* bytes = bmp_make(57, 54, 40, 1, 1, 1, 24, 0);
    XImageReader reader;
    XImage image;
    bool wrote = false;

    if (bytes) {
        wrote = test_write_binary_file(path,
                                       XByteArray_data(bytes),
                                       XByteArray_size_base((const XContainer*)bytes),
                                       true);
    }
    expect_true(wrote, "writes truncated BMP reader fixture");
    XImageReader_init_file_2(&reader, path, "bmp");
    XImage_init(&image);
    expect_true(XImageReader_canRead(&reader),
                "reader canRead accepts a recognizable but potentially corrupt BMP");
    expect_true(!XImageReader_read(&reader, &image) &&
                XImageReader_error(&reader) == XImageReaderError_InvalidDataError,
                "reader read rejects truncated BMP as invalid data");
    expect_true(XImage_isNull(&image),
                "failed reader leaves no partially decoded image");
    XImage_deinit_base(&image);
    XImageReader_deinit_base(&reader);
    if (bytes) XByteArray_delete_base((XClass*)bytes);
    remove(path);
}

/**
 * @brief 校验 QImageReader 软件裁剪对 QRect::isValid() 边界的处理。
 * @details Qt 将零宽或零高的非 null QRect 视为无效矩形；当处理器不支持
 *          ClipRect 时，读取器不应执行 copy()，而应保留完整图像。
 */
static void test_image_reader_invalid_clip_rect(void)
{
    const char* path = "xgui_reader_invalid_clip.bmp";
    XImage source;
    XImage image;
    XImageReader reader;
    XRect clip;

    XImage_init_ex(&source, 2, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xff112233u);
    XImage_setPixel(&source, 1, 0, 0xff445566u);
    XImage_setPixel(&source, 0, 1, 0xff778899u);
    XImage_setPixel(&source, 1, 1, 0xffaabbccu);
    expect_true(XImage_save_2(&source, path, "bmp", -1),
                "writes invalid clip rectangle fixture");

    XImageReader_init_file_2(&reader, path, "bmp");
    clip.x = 0;
    clip.y = 0;
    clip.width = 0;
    clip.height = 2;
    XImageReader_setClipRect(&reader, &clip);
    XImage_init(&image);
    expect_true(XImageReader_read(&reader, &image) &&
                XImage_width(&image) == 2 && XImage_height(&image) == 2,
                "zero-width non-null clip keeps the complete image");
    XImage_deinit_base(&image);
    XImageReader_deinit_base(&reader);

    XImageReader_init_file_2(&reader, path, "bmp");
    clip.width = 2;
    clip.height = 0;
    XImageReader_setClipRect(&reader, &clip);
    XImage_init(&image);
    expect_true(XImageReader_read(&reader, &image) &&
                XImage_width(&image) == 2 && XImage_height(&image) == 2,
                "zero-height non-null clip keeps the complete image");
    XImage_deinit_base(&image);
    XImageReader_deinit_base(&reader);

    XImage_deinit_base(&source);
    remove(path);
}
#endif /* XIMAGECODEC_BMP_ON */

static void test_codec_reject_malformed(void)
{
    static const uint8_t emptyPng[] = {0};
    static const uint8_t truncatedPng[12] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0
    };
    static const uint8_t corruptBmp[20] = {
        'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0
    };
    static const uint8_t garbage[8] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
    XImage out;
    XImage nullImage;
    XByteArray* encoded = NULL;

    XImage_init(&out);
    XImage_init(&nullImage);
    expect_true(!XImageCodec_decode(NULL, 0, XImageCodecFormat_Bmp, &out),
                "null decode rejected");
    expect_true(!XImageCodec_decode(emptyPng, 0, XImageCodecFormat_Png, &out),
                "empty data rejected");
    expect_true(!XImageCodec_decode(truncatedPng, sizeof(truncatedPng),
                                    XImageCodecFormat_Png, &out),
                "truncated PNG rejected");
    expect_true(!XImageCodec_decode(corruptBmp, sizeof(corruptBmp),
                                    XImageCodecFormat_Bmp, &out),
                "corrupt BMP rejected");
    expect_true(!XImageCodec_decode(garbage, sizeof(garbage),
                                    XImageCodecFormat_Unknown, &out),
                "garbage data rejected");
    encoded = XByteArray_create();
    expect_true(encoded && !XImageCodec_encode(NULL, XImageCodecFormat_Png, -1, encoded),
                "null image encode rejected");
    expect_true(encoded && !XImageCodec_encode(&nullImage, XImageCodecFormat_Png, -1, encoded),
                "null (empty) XImage encode rejected");
#if XIMAGECODEC_JPEG_ON
    /* JPEG 已有内置后端：正常图像编码应成功，空图像仍应被拒绝 */
    {
        XImage jpegSrc;
        XImage_init_ex(&jpegSrc, 16, 16, XImageFormat_ARGB32);
        XImage_fill(&jpegSrc, 0xff336699u);
        expect_true(XImageCodec_encode(&jpegSrc, XImageCodecFormat_Jpeg, 75, encoded),
                    "JPEG 编码应成功");
        expect_true(!XImageCodec_encode(&out, XImageCodecFormat_Jpeg, -1, encoded),
                    "JPEG 空图像编码被拒绝");
        XImage_deinit_base(&jpegSrc);
    }
#endif /* XIMAGECODEC_JPEG_ON */
    XImage_deinit_base(&out);
    XImage_deinit_base(&nullImage);
    if (encoded) XByteArray_delete_base((XClass*)encoded);
}

static void test_codec_detect_only(void)
{
#if XIMAGECODEC_JPEG_ON
    static const uint8_t jpegHeader[] = {0xff, 0xd8, 0xff, 0xe0};
#endif
    static const uint8_t gifHeader[] = {'G', 'I', 'F', '8', '9', 'a'};
    static const uint8_t bmpHeader[] = {'B', 'M', 0, 0};
    static const uint8_t svgHeader[] = {'<', 's', 'v', 'g', ' ', 'x'};
    static const uint8_t xmlNotSvg[] = {'<', '?', 'x', 'm', 'l', ' ',
                                        'v', '=', '"', '1', '.', '0', '"', '?', '>'};
    static const uint8_t xmlThenSvg[] = {'<', '?', 'x', 'm', 'l', '?', '>',
                                         '<', 's', 'v', 'g', '>'};
    XImage out;
    XImage_init(&out);
#if XIMAGECODEC_JPEG_ON
    expect_true(XImageCodec_detect(jpegHeader, sizeof(jpegHeader)) == XImageCodecFormat_Jpeg,
                "JPEG 文件头可识别");
    expect_true(XImageCodec_canDecode(XImageCodecFormat_Jpeg) &&
                XImageCodec_canEncode(XImageCodecFormat_Jpeg),
                "JPEG 编解码后端可用");
    expect_true(!XImageCodec_decode(jpegHeader, sizeof(jpegHeader),
                                    XImageCodecFormat_Unknown, &out),
                "JPEG 裸文件头数据解码被拒绝");
#endif
    expect_true(XImageCodec_detect(gifHeader, sizeof(gifHeader)) == XImageCodecFormat_Gif &&
                XImageCodec_detect(bmpHeader, sizeof(bmpHeader)) == XImageCodecFormat_Bmp &&
                XImageCodec_detect(svgHeader, sizeof(svgHeader)) == XImageCodecFormat_Svg,
                "GIF/BMP/SVG 文件头识别");
    expect_true(XImageCodec_detect(xmlNotSvg, sizeof(xmlNotSvg)) == XImageCodecFormat_Unknown &&
                XImageCodec_detect(xmlThenSvg, sizeof(xmlThenSvg)) == XImageCodecFormat_Svg,
                "XML 声明仅在后续出现 SVG 根元素时识别");
    expect_true(XImageCodec_detect(NULL, 0) == XImageCodecFormat_Unknown,
                "空数据识别为 Unknown");
    expect_true(XImageCodec_formatFromName_2("PNG") == XImageCodecFormat_Png &&
                XImageCodec_formatFromName_2("svgz") == XImageCodecFormat_Svg &&
                XImageCodec_formatFromName_2("webp") == XImageCodecFormat_Unknown,
                "格式名解析大小写/别名/未知");
    XImage_deinit_base(&out);
}

static void test_codec_decode_real_assets(void)
{
    struct AssetSpec { const char* file; int width; int height; };
    static const struct AssetSpec assets[] = {
        {"assets/https.png",      670, 304},
        {"assets/运行.png",       794, 411},
        {"assets/配置cmake.png",  638, 920},
        {"assets/分支.png",      1170, 480},
        {"assets/VS克隆储存库.png", 1268, 844},
        {"assets/克隆信息.png",   1268, 844},
    };
    XImage image;
    size_t i;

    for (i = 0; i < sizeof(assets) / sizeof(assets[0]); ++i) {
        char alternate[512];
        bool loaded;
        XImage_init(&image);
        /* 回归程序既可能从仓库根运行，也可能由 bin/ 目录直接启动。 */
        loaded = XImage_load_2(&image, assets[i].file, "png");
        if (!loaded) {
            XImage_deinit_base(&image);
            XImage_init(&image);
            snprintf(alternate, sizeof(alternate), "../%s", assets[i].file);
            loaded = XImage_load_2(&image, alternate, "png");
        }
        expect_true(loaded &&
                    XImage_width(&image) == assets[i].width &&
                    XImage_height(&image) == assets[i].height,
                    "decodes real PNG asset with correct dimensions");
        if (!XImage_isNull(&image)) {
            uint32_t sample = XImage_pixel(&image, 0, 0);
            expect_true((sample >> 24) == 0xffu,
                        "real PNG decodes opaque alpha");
        }
        XImage_deinit_base(&image);
    }
    /* 不存在的文件必须失败 */
    XImage_init(&image);
    expect_true(!XImage_load_2(&image, "assets/not-exist-anyway.png", "png") &&
                (XImage_deinit_base(&image), XImage_init(&image),
                 !XImage_load_2(&image, "../assets/not-exist-anyway.png", "png")),
                "nonexistent asset file rejected");
    XImage_deinit_base(&image);
}


/* ================ PNG 扩展特性测试 ================ */

static void test_codec_png_palette_round_trip(void)
{
    /* 全不透明 Indexed8：编码为调色板 PNG，解码还原 Indexed8 + 索引 + 色表 */
    static const uint32_t opaquePalette[4] = {
        0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffffffu
    };
    static const int idxTable[4][4] = {
        {0, 1, 2, 3}, {3, 1, 2, 0}, {2, 3, 0, 1}, {0, 1, 2, 3}
    };
    XImage src, decoded;
    XByteArray* encoded = NULL;
    uint32_t gotPalette[4];
    int got;

    XImage_init_ex(&src, 4, 4, XImageFormat_Indexed8);
    XImage_setColorTable(&src, opaquePalette, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            XImage_setPixel(&src, x, y, (uint32_t)idxTable[y][x]);

    encoded = XByteArray_create();
    XImage_init(&decoded);
    expect_true(encoded && XImageCodec_encode(&src, XImageCodecFormat_Png, -1, encoded),
                "Indexed8 图像编码为调色板 PNG");
    expect_true(encoded &&
                XImageCodec_decode(XByteArray_data(encoded),
                                   XByteArray_size_base((const XContainer*)encoded),
                                   XImageCodecFormat_Png, &decoded),
                "调色板 PNG 解码成功");
    expect_true(XImage_format(&decoded) == XImageFormat_Indexed8,
                "无透明调色板 PNG 解码输出 Indexed8");
    got = XImage_colorTable(&decoded, gotPalette, 4);
    expect_true(got == 4 && !memcmp(gotPalette, opaquePalette, sizeof(opaquePalette)),
                "调色板 PNG 色表还原一致");
    expect_true(XImage_pixelIndex(&decoded, 0, 1) == 3 &&
                XImage_pixelIndex(&decoded, 3, 3) == 3 &&
                XImage_pixelIndex(&decoded, 2, 0) == 2,
                "调色板 PNG 索引像素还原一致");
    expect_true(XImage_pixel(&decoded, 0, 0) == 0xffff0000u &&
                XImage_pixel(&decoded, 1, 1) == 0xff00ff00u,
                "调色板 PNG 像素颜色还原一致");
    XImage_deinit_base(&decoded);
    if (encoded) XByteArray_delete_base((XClass*)encoded);

    /* 半透明 Indexed8：编码时写 tRNS，解码应展开为 ARGB32 且保留 Alpha */
    {
        static const uint32_t alphaPalette[4] = {
            0x80ff0000u, 0xff00ff00u, 0xff0000ffu, 0x40ffff00u
        };
        XImage srcA;
        XImage_init_ex(&srcA, 3, 2, XImageFormat_Indexed8);
        XImage_setColorTable(&srcA, alphaPalette, 4);
        XImage_setPixel(&srcA, 0, 0, 0); XImage_setPixel(&srcA, 1, 0, 1);
        XImage_setPixel(&srcA, 2, 0, 2); XImage_setPixel(&srcA, 0, 1, 3);
        XImage_setPixel(&srcA, 1, 1, 0); XImage_setPixel(&srcA, 2, 1, 1);
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&srcA, XImageCodecFormat_Png, -1, encoded) &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       XImageCodecFormat_Png, &decoded),
                    "半透明 Indexed8 编码/解码成功");
        expect_true(XImage_format(&decoded) == XImageFormat_ARGB32,
                    "含 tRNS 调色板 PNG 解码展开为 ARGB32");
        expect_true(XImage_pixel(&decoded, 0, 0) == 0x80ff0000u &&
                    XImage_pixel(&decoded, 1, 0) == 0xff00ff00u &&
                    XImage_pixel(&decoded, 0, 1) == 0x40ffff00u,
                    "tRNS Alpha 保留");
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
        XImage_deinit_base(&srcA);
    }
    XImage_deinit_base(&src);
}

static void test_codec_png_extended_assets(void)
{
    /* 调色板/子位深/16 位/Adam7/tRNS 资产解码验证
       （资产字节内嵌于 codec_assets_fixture.h，由 zlib+struct 生成） */
    struct Case {
        const char* file; int w, h;
        XImageFormat fmt;
        uint32_t p0, p1, p2;   /* (0,0) (2,0) (0,2) 等关键点，0 表示按格式校验 */
    };
    static const struct Case cases[] = {
        {"codec_palette8.png", 6, 4, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0x0000ff00u},
        {"codec_palette_trns.png", 6, 4, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0xff00ff00u},
        {"codec_palette_fullalpha.png", 6, 4, XImageFormat_Indexed8,
         0xffff0000u, 0x000000ffu, 0xff00ff00u},
        {"codec_palette1.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xffffffffu, 0xff00ff00u},
        {"codec_palette2.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xff0000ffu, 0xffffffffu},
        {"codec_palette4.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xff00ff00u, 0xffffffffu},
        {"codec_gray1.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xffffffffu, 0xff000000u},
        {"codec_gray2.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xffaa5555u, 0xffffffffu},
        {"codec_gray4.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xff777777u, 0xffffffffu},
        {"codec_gray8.png", 4, 3, XImageFormat_ARGB32,
         0xff000000u, 0xffaaaaaau, 0xff404040u},
        {"codec_gray8_trns.png", 4, 3, XImageFormat_ARGB32,
         0xff000000u, 0xffaaaaaau, 0x00808080u},
        {"codec_gray16.png", 4, 3, XImageFormat_Grayscale16,
         0xff000000u, 0xff808080u, 0xff303030u},
        {"codec_rgb8.png", 4, 3, XImageFormat_ARGB32,
         0xffff0000u, 0xff0000ffu, 0xff010203u},
        {"codec_rgb8_trns.png", 4, 3, XImageFormat_ARGB32,
         0xffff0000u, 0x0000ff00u, 0xff010203u},
        {"codec_rgb16.png", 3, 2, XImageFormat_RGBX64,
         0xff000000u, 0xff00ff00u, 0xffffffffu},
        {"codec_ga8.png", 3, 2, XImageFormat_ARGB32,
         0x00000000u, 0xff000000u, 0xff000000u},
        {"codec_ga16.png", 3, 2, XImageFormat_RGBA64,
         0xff000000u, 0x00808080u, 0xff000000u},
        {"codec_rgba8.png", 3, 2, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0xff000000u},
        {"codec_rgba16.png", 3, 2, XImageFormat_RGBA64,
         0xff000000u, 0x0000ff00u, 0xff000000u},
        {"codec_adam7_rgba8.png", 8, 8, XImageFormat_ARGB32,
         0x80000000u, 0x00000000u, 0x00000000u},
        {"codec_adam7_gray4.png", 8, 8, XImageFormat_ARGB32,
         0xff000000u, 0xff333333u, 0xff444444u},
        {"codec_adam7_palette.png", 8, 8, XImageFormat_Indexed8,
         0xffff0000u, 0xffffffffu, 0xff00ff00u},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "png") &&
                    XImage_width(&image) == c->w && XImage_height(&image) == c->h,
                    "PNG 扩展资产尺寸正确");
        expect_true(XImage_format(&image) == c->fmt,
                    "PNG 扩展资产解码格式正确");
        if (c->fmt == XImageFormat_Indexed8 && !XImage_isNull(&image)) {
            expect_true(XImage_pixelIndex(&image, 0, 0) == 0 &&
                        XImage_pixel(&image, 0, 0) == c->p0,
                        "PNG 索引像素还原一致");
        } else {
            expect_true(!XImage_isNull(&image) && XImage_pixel(&image, 0, 0) == c->p0,
                        "PNG 扩展资产关键像素一致");
        }
        XImage_deinit_base(&image);
    }

    /* 16 位格式逐通道高字节验证 */
    {
        XImage g16, rgb16, rgba16;
        const CodecAssetFixture* a16;
        const uint8_t* line;
        a16 = codec_asset_fixture_find("codec_gray16.png");
        XImage_init(&g16);
        expect_true(a16 != NULL && XImage_loadFromData_2(&g16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&g16) == XImageFormat_Grayscale16,
                    "16 位灰度 PNG 解码为 Grayscale16");
        line = XImage_scanLine(&g16, 0);
        expect_true(line[0] == 0x00 && line[1] == 0x00 && line[2] == 0xff && line[3] == 0xff,
                    "16 位灰度样本双字节值还原");
        XImage_init(&rgb16);
        a16 = codec_asset_fixture_find("codec_rgb16.png");
        expect_true(a16 != NULL && XImage_loadFromData_2(&rgb16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&rgb16) == XImageFormat_RGBX64,
                    "16 位 RGB PNG 解码为 RGBX64");
        line = XImage_scanLine(&rgb16, 1);
        expect_true(line[0] == 0x00 && line[2] == 0x00 && line[4] == 0xff && line[5] == 0xff,
                    "16 位 RGB 样本双字节值还原");
        XImage_init(&rgba16);
        a16 = codec_asset_fixture_find("codec_rgba16.png");
        expect_true(a16 != NULL && XImage_loadFromData_2(&rgba16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&rgba16) == XImageFormat_RGBA64,
                    "16 位 RGBA PNG 解码为 RGBA64");
        line = XImage_scanLine(&rgba16, 0);
        expect_true(line[6] == 0xff && line[7] == 0xff && line[15] == 0x80,
                    "16 位 RGBA Alpha 双字节值还原");
        XImage_deinit_base(&g16);
        XImage_deinit_base(&rgb16);
        XImage_deinit_base(&rgba16);
    }
}


static void test_codec_bmp_extended_assets(void)
{
    /* 索引色/RLE4/RLE8/16 位/BITFIELDS/V4/V5/Core/顶行序 BMP 资产解码验证
       （资产字节内嵌于 codec_assets_fixture.h，由 Python struct 生成） */
    struct Pt { int x; int y; uint32_t c; };
    struct Case {
        const char* file; int w; int h;
        XImageFormat fmt;
        struct Pt p0, p1, p2, p3;
    };
    static const struct Case cases[] = {
        /* 资产规范：逻辑行自上而下；bottom-up 文件按规范倒序存储，
           解码器只翻转一次行序。期望值以解码器实测语义为准。 */
        {"codec_bmp_palette1.bmp", 8, 2, XImageFormat_Mono,
         {0,0,0xff000000u}, {2,0,0xffffffffu}, {0,1,0xff000000u}, {7,1,0xffffffffu}},
        {"codec_bmp_palette2.bmp", 8, 2, XImageFormat_Indexed8,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xff0000ffu}, {3,1,0xff000000u}},
        {"codec_bmp_palette4.bmp", 8, 2, XImageFormat_Indexed8,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xff111111u}, {7,1,0xff222222u}},
        {"codec_bmp_palette8.bmp", 6, 4, XImageFormat_Indexed8,
         {0,0,0xffff0000u}, {2,0,0xff0000ffu}, {0,2,0xff00ff00u}, {5,3,0xff060606u}},
        {"codec_bmp_rle8.bmp", 6, 4, XImageFormat_Indexed8,
         {0,0,0xff0000ffu}, {3,0,0xffffff00u}, {1,3,0xff00ff00u}, {5,0,0xffff0000u}},
        {"codec_bmp_rle4.bmp", 8, 2, XImageFormat_Indexed8,
         {7,0,0xff000000u}, {6,0,0xffff0000u}, {0,1,0xff000000u}, {7,1,0xff888888u}},
        {"codec_bmp_rgb555.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {2,0,0xff00ff00u}, {0,1,0xffffffffu}},
        {"codec_bmp_rgb565.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {3,0,0xff0000ffu}, {3,1,0xffffff00u}},
        {"codec_bmp_bitfields16.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xffffffffu}, {2,1,0xff00ffffu}},
        {"codec_bmp_bitfields32.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0xffff0000u}, {0,1,0xffffffffu}, {2,1,0xff8055aau}, {3,1,0xffff0000u}},
        {"codec_bmp_v4alpha.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0x88ff0000u}, {3,1,0x80102030u}, {2,0,0x880000ffu}, {1,1,0xff000000u}},
        {"codec_bmp_v5rgb.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0x88ff0000u}, {3,1,0x80102030u}, {0,1,0xffffffffu}, {3,0,0x00ffffffu}},
        {"codec_bmp_v5indexed.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0xffff0000u}, {1,0,0x000000ffu}, {0,1,0xffffffffu}, {1,1,0x8800ff00u}},
        {"codec_bmp_core12.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff00ff00u}, {0,1,0xff800000u}, {3,0,0xff00ff00u}, {3,1,0xff800000u}},
        {"codec_bmp_topdown.bmp", 3, 3, XImageFormat_RGB888,
         {0,0,0xffff0000u}, {0,1,0xff00ff00u}, {2,2,0xff0000ffu}, {1,0,0xffff0000u}},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        const struct Pt* pts[4] = {&c->p0, &c->p1, &c->p2, &c->p3};
        if (strcmp(c->file, "codec_bmp_palette2.bmp") == 0) {
            XImage_init(&image);
            expect_true(asset != NULL &&
                        !XImage_loadFromData_2(&image, asset->data,
                                               (int)asset->size, "bmp"),
                        "Qt BMP reader rejects 2-bit palette assets");
            XImage_deinit_base(&image);
            continue;
        }
        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "bmp") &&
                    XImage_width(&image) == c->w && XImage_height(&image) == c->h,
                    "BMP 扩展资产尺寸正确");
        expect_true(!XImage_isNull(&image) && XImage_format(&image) == c->fmt,
                    "BMP 扩展资产解码格式正确");
        for (int k = 0; k < 4; ++k) {
            const struct Pt* pt = pts[k];
            uint32_t got = XImage_pixel(&image, pt->x, pt->y);
            expect_true(got == pt->c, "BMP 扩展资产关键像素一致");
        }
        XImage_deinit_base(&image);
    }
}

#if XIMAGECODEC_JPEG_ON
static void test_codec_jpeg_extended_assets(void)
{
    /* JPEG 扩展资产解码验证：基线(SOF0)/渐进(SOF2)/算术(SOF9)/灰度(单分量)/
       4:2:2 / 4:1:1 抽样/重启间隔(DRI)/CMYK(Adobe APP14)/12 位精度。
       资产字节内嵌于 codec_assets_fixture.h，由 IJG 参考 cjpeg/djpeg 生成，
       期望像素取自参考解码器输出（本实现与 djpeg -nosmooth 同用最近邻上采样），
       8 位逐通道容差 3、12 位容差 4，Alpha 恒为 0xff。 */
    struct Pt { int x; int y; int r; int g; int b; };
    struct Case { const char* file; const struct Pt* pts; int tol; };
    /* YCbCr 3 分量 4:2:0 渐变（基线/渐进/算术/重启间隔共用参考输出） */
    static const struct Pt kYccPts[6] = {
        {0, 0, 135, 247, 0}, {47, 0, 248, 250, 245}, {0, 31, 136, 244, 0},
        {47, 31, 251, 247, 248}, {24, 16, 31, 11, 132}, {16, 10, 180, 77, 96}
    };
    /* 灰度单分量（RGB 三个通道取同一灰度值） */
    static const struct Pt kGrayPts[6] = {
        {0, 0, 185, 185, 185}, {47, 0, 249, 249, 249}, {0, 31, 184, 184, 184},
        {47, 31, 248, 248, 248}, {24, 16, 31, 31, 31}, {16, 10, 110, 110, 110}
    };
    /* 4:2:2（Y 2x1 抽样） */
    static const struct Pt k422Pts[6] = {
        {0, 0, 135, 248, 0}, {47, 0, 250, 249, 244}, {0, 31, 134, 246, 0},
        {47, 31, 249, 248, 244}, {24, 16, 32, 10, 136}, {16, 10, 173, 81, 92}
    };
    /* 4:1:1（Y 4x1 抽样） */
    static const struct Pt k411Pts[6] = {
        {0, 0, 149, 239, 1}, {47, 0, 245, 253, 240}, {0, 31, 149, 238, 0},
        {47, 31, 245, 251, 241}, {24, 16, 23, 13, 144}, {16, 10, 167, 82, 103}
    };
    /* CMYK（Adobe APP14 transform=0，直接 C/M/Y/K -> R/G/B 映射） */
    static const struct Pt kCmykPts[6] = {
        {0, 0, 127, 248, 0}, {47, 0, 247, 248, 249}, {0, 31, 127, 248, 0},
        {47, 31, 247, 248, 249}, {24, 16, 36, 8, 127}, {16, 10, 183, 76, 85}
    };
    /* 12 位算术编码 */
    static const struct Pt k12ArithPts[6] = {
        {0, 0, 134, 246, 0}, {47, 0, 248, 249, 249}, {0, 31, 135, 245, 1},
        {47, 31, 248, 248, 249}, {24, 16, 32, 10, 131}, {16, 10, 184, 76, 96}
    };
    /* 12 位 Huffman（仅两点与算术版不同） */
    static const struct Pt k12HuffPts[6] = {
        {0, 0, 134, 246, 0}, {47, 0, 248, 249, 249}, {0, 31, 135, 245, 0},
        {47, 31, 249, 248, 249}, {24, 16, 32, 10, 131}, {16, 10, 184, 76, 96}
    };
    static const struct Case cases[] = {
        {"codec_jpeg_baseline.jpg", kYccPts, 3},
#if XIMAGECODEC_JPEG_PROGRESSIVE_ON
        {"codec_jpeg_progressive.jpg", kYccPts, 3},
#endif /* XIMAGECODEC_JPEG_PROGRESSIVE_ON */
#if XIMAGECODEC_JPEG_ARITHMETIC_ON
        {"codec_jpeg_arithmetic.jpg", kYccPts, 3},
#endif /* XIMAGECODEC_JPEG_ARITHMETIC_ON */
        {"codec_jpeg_gray.jpg", kGrayPts, 3},
        {"codec_jpeg_422.jpg", k422Pts, 3},
        {"codec_jpeg_411.jpg", k411Pts, 3},
        {"codec_jpeg_restart.jpg", kYccPts, 3},
#if XIMAGECODEC_JPEG_CMYK_ON
        {"codec_jpeg_cmyk.jpg", kCmykPts, 3},
#endif /* XIMAGECODEC_JPEG_CMYK_ON */
#if XIMAGECODEC_JPEG_12BIT_ON && XIMAGECODEC_JPEG_ARITHMETIC_ON
        {"codec_jpeg_12bit_arith.jpg", k12ArithPts, 4},
#endif /* XIMAGECODEC_JPEG_12BIT_ON && XIMAGECODEC_JPEG_ARITHMETIC_ON */
#if XIMAGECODEC_JPEG_12BIT_ON
        {"codec_jpeg_12bit_huff.jpg", k12HuffPts, 4},
#endif /* XIMAGECODEC_JPEG_12BIT_ON */
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        int k;

        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "jpeg") &&
                    XImage_width(&image) == 48 && XImage_height(&image) == 32,
                    "JPEG 扩展资产尺寸正确");
        expect_true(!XImage_isNull(&image), "JPEG 扩展资产解码成功");
        for (k = 0; k < 6; ++k) {
            const struct Pt* pt = &c->pts[k];
            uint32_t got = XImage_pixel(&image, pt->x, pt->y);
            int dr = (int)((got >> 16) & 0xffu) - pt->r;
            int dg = (int)((got >> 8) & 0xffu) - pt->g;
            int db = (int)(got & 0xffu) - pt->b;
            if (dr < 0) dr = -dr;
            if (dg < 0) dg = -dg;
            if (db < 0) db = -db;
            expect_true((got >> 24) == 0xffu &&
                        dr <= c->tol && dg <= c->tol && db <= c->tol,
                        "JPEG 扩展资产采样点逐通道容差内");
        }
        XImage_deinit_base(&image);
    }
}

/* 对齐 Qt 6.8 qjpeghandler.cpp:389-395：JFIF 的 dpi/dpcm 密度写入
   QImage::dotsPerMeterX/Y。这里只改动 APP0 的密度字段，压缩数据保持不变。 */
static void test_codec_jpeg_jfif_density(void)
{
    const CodecAssetFixture* asset =
        codec_asset_fixture_find("codec_jpeg_baseline.jpg");
    XByteArray* bytes;
    XImage image;
    uint8_t* data;

    bytes = XByteArray_create();
    expect_true(asset != NULL && bytes != NULL,
                "JPEG JFIF 密度夹具可用");
    if (!asset || !bytes) {
        if (bytes) XByteArray_delete_base((XClass*)bytes);
        return;
    }
    expect_true(XByteArray_resize_base((XVector*)bytes, asset->size),
                "JPEG JFIF 密度夹具复制成功");
    data = XByteArray_data(bytes);
    if (!data || asset->size < 20) {
        expect_true(false, "JPEG JFIF APP0 头完整");
        XByteArray_delete_base((XClass*)bytes);
        return;
    }
    memcpy(data, asset->data, asset->size);
    expect_true(data[0] == 0xff && data[1] == 0xd8 &&
                data[2] == 0xff && data[3] == 0xe0 &&
                data[6] == 'J' && data[7] == 'F' && data[8] == 'I' &&
                data[9] == 'F' && data[10] == 0,
                "JPEG JFIF APP0 标识正确");

    /* APP0 固定布局：unit@13，X density@14..15，Y density@16..17。 */
    data[13] = 1;   /* dots/inch */
    data[14] = 1; data[15] = 44;   /* 300 dpi */
    data[16] = 0; data[17] = 150;  /* 150 dpi */
    XImage_init(&image);
    expect_true(XImageCodec_decode(XByteArray_data(bytes),
                                   XByteArray_size_base((const XContainer*)bytes),
                                   XImageCodecFormat_Jpeg, &image),
                "JPEG JFIF dpi 解码成功");
    expect_true(XImage_dotsPerMeterX(&image) == 11811 &&
                XImage_dotsPerMeterY(&image) == 5905,
                "JPEG JFIF dpi 转每米点数与 Qt 一致");
    XImage_deinit_base(&image);

    data[13] = 2;   /* dots/cm */
    data[14] = 0; data[15] = 37;
    data[16] = 0; data[17] = 42;
    XImage_init(&image);
    expect_true(XImageCodec_decode(XByteArray_data(bytes),
                                   XByteArray_size_base((const XContainer*)bytes),
                                   XImageCodecFormat_Jpeg, &image),
                "JPEG JFIF dpcm 解码成功");
    expect_true(XImage_dotsPerMeterX(&image) == 3700 &&
                XImage_dotsPerMeterY(&image) == 4200,
                "JPEG JFIF dpcm 转每米点数与 Qt 一致");
    XImage_deinit_base(&image);
    XByteArray_delete_base((XClass*)bytes);
}
#endif /* XIMAGECODEC_JPEG_ON */

#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
static bool codec_svg_vector_decode(const char* svg, XImage* image)
{
    return XImageCodec_decode((const uint8_t*)svg, strlen(svg),
                              XImageCodecFormat_Svg, image);
}

static void test_codec_svg_vector_render(void)
{
    XImage image;
    int filled;
    uint32_t v;

    /* 线性渐变：左红右蓝 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect width=\"4\" height=\"4\" fill=\"url(#g)\"/></svg>", &image),
        "SVG 线性渐变可解码");
    expect_true((XImage_pixel(&image, 0, 2) & 0xff0000u) != 0 &&
                (XImage_pixel(&image, 3, 2) & 0xffu) != 0,
                "SVG 线性渐变左红右蓝");
    XImage_deinit_base(&image);

    /* 径向渐变 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"6\" height=\"6\">"
        "<defs><radialGradient id=\"re\" cx=\"0.5\" cy=\"0.5\" r=\"0.5\">"
        "<stop offset=\"0\" stop-color=\"#ffffff\"/>"
        "<stop offset=\"1\" stop-color=\"#000000\"/>"
        "</radialGradient></defs>"
        "<rect width=\"6\" height=\"6\" fill=\"url(#re)\"/></svg>", &image),
        "SVG 径向渐变可解码");
    expect_true((XImage_pixel(&image, 3, 3) & 0x00ffffffu) > 0x00f0f0f0u &&
                (XImage_pixel(&image, 0, 0) & 0xffu) < 0x20u,
                "SVG 径向渐变中心亮边缘暗");
    XImage_deinit_base(&image);

    /* 圆形 / 椭圆 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<circle cx=\"4\" cy=\"4\" r=\"3.5\" fill=\"#00ff00\"/></svg>", &image),
        "SVG circle 可解码");
    expect_true(XImage_pixel(&image, 4, 4) == 0xff00ff00u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG circle 中心填充外部透明");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<ellipse cx=\"4\" cy=\"2\" rx=\"3\" ry=\"1.5\" fill=\"#00ff00\"/></svg>",
        &image), "SVG ellipse 可解码");
    expect_true(XImage_pixel(&image, 4, 2) == 0xff00ff00u &&
                XImage_pixel(&image, 0, 2) == 0x00000000u,
                "SVG ellipse 填充与外空白");
    XImage_deinit_base(&image);

    /* path：三角形 + 圆弧 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<path d=\"M1 5 L1 1 L7 3 Z\" fill=\"#ff8800\"/></svg>", &image),
        "SVG path 三角形可解码");
    filled = 0;
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x)
            if (XImage_pixel(&image, x, y) == 0xffff8800u) ++filled;
    expect_true(filled > 6, "SVG path 三角形面积填充");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<path d=\"M1 5 A3 3 0 0 1 7 5 L7 7 L1 7 Z\" fill=\"#00aaff\"/></svg>",
        &image), "SVG path 圆弧可解码");
    expect_true(XImage_pixel(&image, 4, 5) == 0xff00aaffu &&
                XImage_pixel(&image, 1, 6) == 0xff00aaffu,
                "SVG 圆弧主体覆盖");
    expect_true(XImage_pixel(&image, 4, 0) == 0x00000000u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG 圆弧外部空白");
    XImage_deinit_base(&image);

    /* 多边形 / 折线 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<polygon points=\"1,5 1,1 7,3\" fill=\"#ff8800\"/></svg>", &image),
        "SVG polygon 可解码");
    expect_true(XImage_pixel(&image, 4, 3) == 0xffff8800u,
                "SVG polygon 宽行覆盖");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<polyline points=\"1,1 5,1 9,9\" fill=\"none\" stroke=\"#ff0000\" "
        "stroke-width=\"2\"/></svg>", &image), "SVG polyline 可解码");
    filled = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            if (XImage_pixel(&image, x, y) == 0xffff0000u) ++filled;
    expect_true(filled > 4, "SVG polyline 描边像素存在");
    XImage_deinit_base(&image);

    /* 文字 / viewBox / group+transform / 描边 / 透明度 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<text x=\"2\" y=\"8\" font-size=\"10\" fill=\"#000000\">A</text></svg>",
        &image), "SVG text 可解码");
    filled = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 20; ++x)
            if (XImage_pixel(&image, x, y) == 0xff000000u) ++filled;
    expect_true(filled > 4, "SVG text 点阵像素存在");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<rect width=\"5\" height=\"10\" fill=\"#123456\"/></svg>", &image),
        "SVG viewBox 可解码");
    expect_true(XImage_width(&image) == 10 && XImage_height(&image) == 10 &&
                XImage_pixel(&image, 0, 9) == 0xff123456u &&
                XImage_pixel(&image, 4, 0) == 0xff123456u,
                "SVG viewBox 尺寸与内容一致");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<g transform=\"translate(2,2)\">"
        "<rect width=\"4\" height=\"4\" fill=\"#123456\"/></g></svg>", &image),
        "SVG group+transform 可解码");
    expect_true(XImage_pixel(&image, 2, 2) == 0xff123456u &&
                XImage_pixel(&image, 5, 5) == 0xff123456u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG group transform 平移生效");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"none\" "
        "stroke=\"#ff0000\" stroke-width=\"3\"/></svg>", &image),
        "SVG 描边矩形可解码");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffff0000u &&
                XImage_pixel(&image, 7, 7) == 0xffff0000u &&
                XImage_pixel(&image, 5, 5) == 0x00000000u,
                "SVG 描边边框与内部空白");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#ff0000\" opacity=\"0.5\"/></svg>",
        &image), "SVG 透明度可解码");
    v = XImage_pixel(&image, 2, 2);
    expect_true((v >> 24) == 0x80u && (v & 0x00ff0000u) != 0,
                "SVG opacity=0.5 得到 0x80 Alpha");
    XImage_deinit_base(&image);
}
#endif /* XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON */

#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
static void test_codec_gif_animation(void)
{
    XImageCodecAnimation* anim;
    XImage image;
    XByteArray* bytes;
    XImageCodecAnimation* noLoopAnim;
    XImageCodecAnimation* shortDelayAnim;
    XFile movieFile;
    XString* movieName;
    XMovie movie;

    anim = XImageCodec_decodeAnimation(kCodecGifAnimFixture,
                                       kCodecGifAnimFixtureSize);
    expect_true(anim != NULL, "GIF 动画解码返回对象");
    if (anim) {
        expect_true(anim->frameCount == 4, "GIF 动画帧数为 4");
        expect_true(anim->loopCount == -1, "GIF Netscape 无限循环");
        expect_true(anim->frames[1].delayMs == 200, "GIF 第 2 帧延迟 200ms");
        expect_true(anim->frames[0].disposal == XImageCodecFrameDisposal_Keep &&
                    anim->frames[2].disposal ==
                        XImageCodecFrameDisposal_RestoreBackground,
                    "GIF 帧处置方式解析正确");
        expect_true(XImage_width(&anim->frames[0].image) == 4 &&
                    XImage_height(&anim->frames[0].image) == 2,
                    "GIF 帧尺寸为逻辑屏幕 4x2");
        expect_true(XImage_pixel(&anim->frames[0].image, 0, 0) == 0xffffffffu &&
                    XImage_pixel(&anim->frames[0].image, 2, 0) == 0xff000000u,
                    "GIF 首帧像素正确");
        expect_true(XImage_pixel(&anim->frames[1].image, 2, 0) == 0xffffffffu &&
                    XImage_pixel(&anim->frames[1].image, 3, 0) == 0xff000000u,
                    "GIF 第 2 帧透明色保留底图");
        expect_true(XImage_pixel(&anim->frames[2].image, 3, 0) == 0xffffffffu,
                    "GIF 第 3 帧绘制白色");
        expect_true(XImage_pixel(&anim->frames[3].image, 3, 0) == 0xff000000u &&
                    XImage_pixel(&anim->frames[3].image, 0, 0) == 0xffffffffu,
                    "GIF 第 4 帧恢复逻辑屏幕背景且旧帧保留");
        XImageCodecAnimation_delete(anim);
    }

    /* 多帧 GIF 的单帧解码仍可用 */
    XImage_init(&image);
    expect_true(XImageCodec_decode(kCodecGifAnimFixture,
                                   kCodecGifAnimFixtureSize,
                                   XImageCodecFormat_Gif, &image),
                "多帧 GIF 单帧解码");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffffffffu,
                "单帧解码取首帧内容");
    XImage_deinit_base(&image);

    /* 动画解码后 GIF 编码往返不受影响 */
    bytes = XByteArray_create();
    XImage_init_ex(&image, 2, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(bytes && XImageCodec_encode(&image, XImageCodecFormat_Gif,
                                            -1, bytes) &&
                XImageCodec_decode(XByteArray_data(bytes),
                                   XByteArray_size_base(
                                       (const XContainer*)bytes),
                                   XImageCodecFormat_Gif, &image),
                "GIF 编码往返在动画 API 之后正常");
    noLoopAnim = bytes
        ? XImageCodec_decodeAnimation(
              XByteArray_data(bytes),
              XByteArray_size_base((const XContainer*)bytes))
        : NULL;
    expect_true(noLoopAnim && noLoopAnim->frameCount == 1 &&
                    noLoopAnim->loopCount == 0,
                "无 Netscape 扩展的 GIF 循环次数为 0");
    if (noLoopAnim) XImageCodecAnimation_delete(noLoopAnim);
    shortDelayAnim = XImageCodec_decodeAnimation(
        kCodecGifSingleGceFixture, kCodecGifSingleGceFixtureSize);
    expect_true(shortDelayAnim && shortDelayAnim->frameCount == 1 &&
                    shortDelayAnim->frames[0].delayMs == 100,
                "GIF 小于 2 个百分之一秒的延迟钳制为 100ms");
    if (shortDelayAnim) XImageCodecAnimation_delete(shortDelayAnim);
    XImage_deinit_base(&image);
    if (bytes) XByteArray_delete_base((XClass*)bytes);

    /* 上层 XMovie 必须真正消费读取器的 GIF 帧，而不是只报告 codec 能力。 */
    movieName = XString_create_utf8("xgui_movie_anim.gif");
    XFile_init_2(&movieFile, movieName);
    if (movieName && XFile_open_2(&movieFile, XIODevice_WriteOnly, 0)) {
        expect_true(XIODevice_write_1((XIODevice*)&movieFile,
                                      (const char*)kCodecGifAnimFixture,
                                      (int64_t)kCodecGifAnimFixtureSize) ==
                        (int64_t)kCodecGifAnimFixtureSize,
                    "XMovie GIF 夹具写入成功");
        XIODevice_close_base((XIODevice*)&movieFile);
        {
            XImageReader reader;
            XImage first;
            XImage second;
            XImageReader_init_file_2(&reader, "xgui_movie_anim.gif", "gif");
            XImage_init(&first);
            XImage_init(&second);
            expect_true(XImageReader_read(&reader, &first) &&
                        XImageReader_currentImageNumber(&reader) == 0 &&
                        XImage_pixel(&first, 2, 0) == 0xff000000u,
                        "XImageReader GIF 首次 read 返回第 0 帧");
            expect_true(XImageReader_read(&reader, &second) &&
                        XImageReader_currentImageNumber(&reader) == 1 &&
                        XImage_pixel(&second, 2, 0) == 0xffffffffu,
                        "XImageReader 连续 read 自动推进到下一帧");
            XImage_deinit_base(&first);
            XImage_deinit_base(&second);
            XImageReader_deinit_base(&reader);
        }
        XMovie_init_file_2(&movie, "xgui_movie_anim.gif", "gif");
        expect_true(XMovie_frameCount(&movie) == 4 &&
                    XMovie_loopCount(&movie) == -1,
                    "XMovie 暴露 GIF 帧数/循环次数");
        XMovie_start(&movie);
        expect_true(XMovie_state(&movie) == XMovieState_Running &&
                    XMovie_currentFrameNumber(&movie) == 0,
                    "XMovie 多帧 start 保持 Running");
        expect_true(XMovie_jumpToNextFrame(&movie) &&
                    XMovie_currentFrameNumber(&movie) == 1 &&
                    XMovie_nextFrameDelay(&movie) == 200,
                    "XMovie jumpToNextFrame 切换真实 GIF 帧");
        XMovie_deinit_base(&movie);
        XFile_remove_static(movieName);
    }
    else
        expect_true(false, "XMovie GIF 夹具文件打开成功");
    XClass_deinit_base((XClass*)&movieFile);
    if (movieName) XString_delete_base((XClass*)movieName);
}
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */

static void test_codec_upper_layer_files(void)
{
    static const char* files[] = {"xgui_rt.bmp", "xgui_rt.png",
#if XIMAGECODEC_JPEG_ON
                                  "xgui_rt.jpg",
#endif
                                  "xgui_rt.gif", "xgui_rt.svg"};
    static const char* fmts[] = {"bmp", "png",
#if XIMAGECODEC_JPEG_ON
                                 "jpeg",
#endif
                                 "gif", "svg"};
    XImage source, loaded;
    size_t i;

    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&source, 0xff336699u);
    XImage_setPixel(&source, 0, 1, 0x80123456u);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        XImage_init(&loaded);
        expect_true(XImage_save_2(&source, files[i], fmts[i], -1) &&
                    XImage_load_2(&loaded, files[i], fmts[i]),
                    "XImage 文件保存/加载走 codec（各格式）");
        expect_true(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2,
                    "XImage 文件往返保持尺寸（各格式）");
        if (fmts[i][0] != 'g' && fmts[i][0] != 'j') {  /* JPEG 有损，跳过逐像素精确比较 */
            expect_true(XImage_pixel(&loaded, 0, 0) == 0xff336699u &&
                        XImage_pixel(&loaded, 2, 0) == 0xff336699u,
                        "XImage 文件往返像素一致（各格式）");
        }
        XImage_deinit_base(&loaded);
        { XString* f = XString_create_utf8(files[i]);
          XFile_remove_static(f); if (f) XString_delete_base((XClass*)f); }
    }
    XImage_deinit_base(&source);
}

static void test_codec_upper_layer_devices(void)
{
    static const char* files[] = {"xgui_dev.bmp", "xgui_dev.png",
#if XIMAGECODEC_JPEG_ON
                                  "xgui_dev.jpg",
#endif
                                  "xgui_dev.gif", "xgui_dev.svg"};
    static const char* fmts[] = {"bmp", "png",
#if XIMAGECODEC_JPEG_ON
                                 "jpeg",
#endif
                                 "gif", "svg"};
    XImage source, loaded;
    XFile file;
    XImageWriter writer;
    XImageReader reader;
    XString* fileName = NULL;
    size_t i;

    XImage_init_ex(&source, 2, 2, XImageFormat_ARGB32);
    XImage_fill(&source, 0xffabcdefu);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        fileName = XString_create_utf8(files[i]);
        XFile_init_2(&file, fileName);
        if (XFile_open_2(&file, XIODevice_WriteOnly, 0)) {
            XImageWriter_init_device_2(&writer, (XIODevice*)&file, fmts[i]);
            expect_true(XImageWriter_canWrite(&writer) &&
                        XImageWriter_write(&writer, &source),
                        "XImageWriter 设备写（各格式）");
            XImageWriter_deinit_base(&writer);
            XIODevice_close_base((XIODevice*)&file);
        }
        XClass_deinit_base((XClass*)&file);

        XFile_init_2(&file, fileName);
        if (XFile_open_2(&file, XIODevice_ReadOnly, 0)) {
            XImageReader_init_device_2(&reader, (XIODevice*)&file, fmts[i]);
            XImage_init(&loaded);
            {
                XSize probedSize;
                XImageReader_size(&reader, &probedSize);
                expect_true(probedSize.width == 2 && probedSize.height == 2,
                            "XImageReader size probes device prefix (各格式)");
            }
            expect_true(XImageReader_canRead(&reader) &&
                        XImageReader_read(&reader, &loaded),
                        "XImageReader 设备读（各格式）");
            expect_true(XImage_width(&loaded) == 2 && XImage_height(&loaded) == 2,
                        "XImageReader 设备往返保持尺寸（各格式）");
            XImage_deinit_base(&loaded);
            XImageReader_deinit_base(&reader);
            XIODevice_close_base((XIODevice*)&file);
        }
        XClass_deinit_base((XClass*)&file);
        XFile_remove_static(fileName);
        XString_delete_base((XClass*)fileName);
    }
    XImage_deinit_base(&source);
}

#endif /* XIMAGECODEC_ON */
static void test_image_pixel_contract(void)
{
    XImage rgba;
    XImage premultiplied;
    XImage converted;
    XImage indexed;
    XImage grayscale;
    XImage cmyk;
    XImage pm64;
    XImage sameSpace;
    XImage grayTransformed;
    XImage reinterpret;
    XImage reinterpretCopy;
    XImage copy;
    const uint32_t customPalette[2] = { 0xff000000u, 0xffffffffu };
    uint32_t largePalette[257];
    XColor indexedColor;

    /* QColorSpace 6.8 的命名空间、模型和自定义原色查询契约。 */
    {
        XColorSpace invalid = XColorSpace_create();
        XColorSpace named;
        XColorSpacePrimariesData primaries;
        XColorSpacePrimariesData customPrimaries = {
            { 0.3127f, 0.3290f }, { 0.640f, 0.330f },
            { 0.300f, 0.600f }, { 0.150f, 0.060f }
        };
        XColorSpace defaultSpace;
        XColorSpace gray;
        expect_true(!XColorSpace_isValid(&invalid) &&
                    !XColorSpace_isValidTarget(&invalid) &&
                    XColorSpace_primaries(&invalid) == XColorSpacePrimaries_Custom &&
                    XColorSpace_transferFunction(&invalid) == XColorSpaceTransfer_Custom &&
                    XColorSpace_transformModel(&invalid) == XColorSpaceTransform_ThreeComponentMatrix &&
                    XColorSpace_colorModel(&invalid) == XColorSpaceModel_Undefined,
                    "QColorSpace default is invalid and has default query values");
        named = XColorSpace_create_named(XColorSpaceNamed_DisplayP3);
        expect_true(XColorSpace_isValid(&named) &&
                    XColorSpace_isValidTarget(&named) &&
                    XColorSpace_primaries(&named) == XColorSpacePrimaries_DciP3D65 &&
                    XColorSpace_transferFunction(&named) == XColorSpaceTransfer_SRgb &&
                    XColorSpace_colorModel(&named) == XColorSpaceModel_Rgb &&
                    strcmp(XColorSpace_description(&named), "Display P3") == 0,
                    "QColorSpace DisplayP3 uses DCI-P3 D65 primaries and sRGB transfer");
        expect_true(XColorSpace_primariesData(&named, &primaries) &&
                    fabsf(primaries.m_redPoint.x - 0.680f) < 1.0e-5f &&
                    fabsf(primaries.m_greenPoint.y - 0.690f) < 1.0e-5f,
                    "QColorSpace returns predefined chromaticities");
        {
            XPointF whitePoint = XColorSpace_whitePoint(&named);
            expect_true(fabsf(whitePoint.x - 0.3127f) < 1.0e-5f &&
                        fabsf(whitePoint.y - 0.3290f) < 1.0e-5f,
                        "QColorSpace returns the predefined D65 white point");
            XColorSpace_setWhitePoint(&named, (XPointF){ 0.3457f, 0.3585f });
            whitePoint = XColorSpace_whitePoint(&named);
            expect_true(XColorSpace_primaries(&named) == XColorSpacePrimaries_Custom &&
                        fabsf(whitePoint.x - 0.3457f) < 1.0e-5f &&
                        fabsf(whitePoint.y - 0.3585f) < 1.0e-5f &&
                        XColorSpace_description(&named)[0] == '\0',
                        "QColorSpace setWhitePoint resets named metadata");
        }
        named = XColorSpace_create_named(XColorSpaceNamed_AdobeRgb);
        expect_true(XColorSpace_isValid(&named) &&
                    XColorSpace_transferFunction(&named) == XColorSpaceTransfer_Gamma &&
                    fabsf(XColorSpace_gamma(&named) - 2.19921875f) < 1.0e-5f,
                    "QColorSpace AdobeRGB uses Qt gamma 2.19921875");
        named = XColorSpace_create_custom(&customPrimaries,
                                          XColorSpaceTransfer_Gamma, 2.2f);
        XColorSpace_setDescription(&named, "custom RGB");
        expect_true(XColorSpace_isValid(&named) &&
                    XColorSpace_primaries(&named) == XColorSpacePrimaries_Custom &&
                    XColorSpace_primariesData(&named, &primaries) &&
                    strcmp(XColorSpace_description(&named), "custom RGB") == 0,
                    "QColorSpace custom RGB retains chromaticities and description");
        XColorSpace_setDescription(&named, "");
        expect_true(strcmp(XColorSpace_description(&named), "") == 0,
                    "QColorSpace custom description clear");
        named = XColorSpace_sRgb();
        XColorSpace_setDescription(&named, "user name");
        expect_true(strcmp(XColorSpace_description(&named), "user name") == 0,
                    "QColorSpace user description");
        XColorSpace_setDescription(&named, "");
        expect_true(strcmp(XColorSpace_description(&named), "sRGB") == 0,
                    "QColorSpace description fallback");
        defaultSpace = XColorSpace_create();
        XColorSpace_setTransferFunction(&defaultSpace,
                                        XColorSpaceTransfer_Linear, 0.0f);
        expect_true(!XColorSpace_isValid(&defaultSpace) &&
                    XColorSpace_colorModel(&defaultSpace) == XColorSpaceModel_Rgb &&
                    XColorSpace_transferFunction(&defaultSpace) == XColorSpaceTransfer_Linear,
                    "QColorSpace default transfer setter keeps RGB model metadata");
        gray = XColorSpace_create_gray((XPointF){ 0.3457f, 0.3585f },
                                       XColorSpaceTransfer_Linear, 0.0f);
        expect_true(XColorSpace_isValid(&gray) &&
                    XColorSpace_colorModel(&gray) == XColorSpaceModel_Gray &&
                    !XColorSpace_primariesData(&gray, &primaries),
                    "QColorSpace supports valid custom grayscale metadata");
        {
            XPointF whitePoint = XColorSpace_whitePoint(&gray);
            expect_true(fabsf(whitePoint.x - 0.3457f) < 1.0e-5f &&
                        fabsf(whitePoint.y - 0.3585f) < 1.0e-5f,
                        "QColorSpace grayscale white point is queryable");
            XColorSpace_setWhitePoint(&invalid, (XPointF){ 0.3457f, 0.3585f });
            expect_true(!XColorSpace_isValid(&invalid) &&
                        XColorSpace_colorModel(&invalid) == XColorSpaceModel_Gray &&
                        fabsf(XColorSpace_whitePoint(&invalid).x - 0.3457f) < 1.0e-5f,
                        "QColorSpace invalid space retains white point without transfer function");
        }
        {
            XColorSpace source = XColorSpace_sRgb();
            XColorSpace copy = XColorSpace_withTransferFunction(
                &source, XColorSpaceTransfer_Linear, 0.0f);
            expect_true(XColorSpace_isValid(&copy) &&
                        XColorSpace_transferFunction(&copy) == XColorSpaceTransfer_Linear &&
                        XColorSpace_transferFunction(&source) == XColorSpaceTransfer_SRgb &&
                        strcmp(XColorSpace_description(&copy), "Linear sRGB") == 0,
                        "QColorSpace withTransferFunction identifies Linear sRGB without mutating source");
            XColorSpace_setPrimaries(&copy, XColorSpacePrimaries_Bt2020);
            expect_true(XColorSpace_primaries(&copy) == XColorSpacePrimaries_Bt2020 &&
                        XColorSpace_colorModel(&copy) == XColorSpaceModel_Rgb &&
                        XColorSpace_description(&copy)[0] == '\0',
                        "QColorSpace setPrimaries resets description and keeps RGB model");
            XColorSpace_setTransferFunction(&copy, XColorSpaceTransfer_Custom, 0.0f);
            expect_true(XColorSpace_transferFunction(&copy) == XColorSpaceTransfer_Linear,
                        "QColorSpace setTransferFunction Custom is ignored");
            XColorSpace described = XColorSpace_sRgb();
            XColorSpace_setTransferFunction(&described,
                                            XColorSpaceTransfer_SRgb, 0.0f);
            expect_true(strcmp(XColorSpace_description(&described), "sRGB") == 0,
                        "QColorSpace raw zero gamma does not clear unchanged description");
        }
        {
            XColorSpace gammaA = XColorSpace_create_ex(
                XColorSpacePrimaries_SRgb, XColorSpaceTransfer_Gamma, 2.2f);
            XColorSpace gammaB = XColorSpace_create_ex(
                XColorSpacePrimaries_SRgb, XColorSpaceTransfer_Gamma,
                2.2f + 1.0f / 1024.0f);
            expect_true(XColorSpace_equals(&gammaA, &gammaB),
                        "QColorSpace Gamma equality uses 1/512 tolerance");
        }
    }

    XImage_init(&copy);
    XImage_init(&pm64);
    XImage_init(&converted);
    XImage_init(&grayscale);
    XImage_init(&cmyk);
    XImage_init(&sameSpace);
    XImage_init(&grayTransformed);
    XImage_init(&reinterpret);
    XImage_init(&reinterpretCopy);
    XImage_init_ex(&premultiplied, 1, 1, XImageFormat_ARGB32_Premultiplied);
    XImage_setText_2(&premultiplied, "foo", "bar");
    XImage_setText_2(&premultiplied, "foo2", "bar2");
    XImage_setColorSpace(&premultiplied, XColorSpace_sRgb());
    {
        const int64_t colorSpaceKey = XImage_cacheKey(&premultiplied);
        XImage_setColorSpace(&premultiplied, XColorSpace_sRgb());
        expect_true(XImage_cacheKey(&premultiplied) == colorSpaceKey,
                    "setColorSpace with the same value is a no-op");
        expect_true(XImage_convertToColorSpace(&premultiplied, XColorSpace_sRgb(), 0) &&
                    XImage_cacheKey(&premultiplied) == colorSpaceKey,
                    "convertToColorSpace with the same value is a no-op");
        XImage_convertedToColorSpace(&premultiplied, XColorSpace_sRgb(), 0, &sameSpace);
        expect_true(!XImage_isNull(&sameSpace) &&
                    XImage_cacheKey(&sameSpace) == colorSpaceKey,
                    "convertedToColorSpace with the same value shares image data");
        XImage_convertedToColorSpace(&premultiplied, XColorSpace_create(), 0, &sameSpace);
        expect_true(XImage_isNull(&sameSpace),
                    "convertedToColorSpace rejects an invalid target");
        {
            XColorSpace elementTarget = XColorSpace_sRgb();
            elementTarget.m_transformModel = XColorSpaceTransform_ElementListProcessing;
            XImage_convertedToColorSpace(&premultiplied, elementTarget, 0, &sameSpace);
            expect_true(XImage_isNull(&sameSpace),
                        "convertedToColorSpace rejects non-matrix target model");
        }
        {
            XColorTransform transform;
            XColorSpace targetGray = XColorSpace_create_gray(
                (XPointF){ 0.3127f, 0.3290f },
                XColorSpaceTransfer_Linear, 0.0f);
            XColorSpace resultSpace;
            transform.m_source = XColorSpace_sRgb();
            transform.m_target = targetGray;
            XImage_applyColorTransform(&premultiplied, &transform,
                                       XImageFormat_Grayscale8, 0,
                                       &grayTransformed);
            resultSpace = XImage_colorSpace(&grayTransformed);
            expect_true(!XImage_isNull(&grayTransformed) &&
                        XImage_format(&grayTransformed) == XImageFormat_Grayscale8 &&
                        XColorSpace_colorModel(&resultSpace) == XColorSpaceModel_Gray,
                        "QImage explicit RGB to grayscale format transform is accepted");
        }
    }
    XImage_setDevicePixelRatio(&premultiplied, 2.0f);
    XImage_setDotsPerMeterX(&premultiplied, 2835);
    {
        XString* allText = XImage_text(&premultiplied, NULL);
        expect_true(allText && XString_equals_utf8(allText, "foo: bar\n\nfoo2: bar2",
                                                    XChar_CaseSensitive),
                    "XImage text(NULL) aggregates all metadata");
        if (allText) XString_delete_base((XClass*)allText);
    }
    XImage_convertToFormat(&premultiplied, XImageFormat_RGB32, 0, &converted);
    expect_true(XImage_textCount(&converted) == 2 &&
                XString_equals_utf8(XImage_textKey_const(&converted, 0), "foo",
                                    XChar_CaseSensitive) &&
                XString_equals_utf8(XImage_textKey_const(&converted, 1), "foo2",
                                    XChar_CaseSensitive),
                "convertToFormat preserves text keys");
    {
        XString* convertedText = XImage_text(&converted, NULL);
        expect_true(convertedText &&
                    XString_equals_utf8(convertedText, "foo: bar\n\nfoo2: bar2",
                                        XChar_CaseSensitive),
                    "convertToFormat preserves aggregated image text");
        if (convertedText) XString_delete_base((XClass*)convertedText);
    }
    {
        XColorSpace convertedSpace = XImage_colorSpace(&converted);
        expect_true(XImage_hasColorSpace(&converted) &&
                    XColorSpace_isSRgb(&convertedSpace) &&
                    XImage_devicePixelRatio(&converted) == 2.0f &&
                    XImage_dotsPerMeterX(&converted) == 2835,
                    "convertToFormat preserves color space and physical metadata");
    }
    {
        const char* convertedText = XImage_text_2(&converted, "foo");
        expect_true(convertedText && strcmp(convertedText, "bar") == 0,
                    "XImage text_2 finds an exact metadata key");
    }
    XImage_setPixel(&premultiplied, 0, 0, 0x80800000u);
    expect_true((XImage_pixel(&premultiplied, 0, 0) & 0x00ff0000u) >= 0x007f0000u &&
                (XImage_pixel(&premultiplied, 0, 0) & 0x00ff0000u) <= 0x00810000u,
                "premultiplied pixel reads back an unpremultiplied channel");
    XImage_convertToFormat(&premultiplied, XImageFormat_ARGB32, 0, &converted);
    expect_true(XImage_pixel(&converted, 0, 0) == 0x80800000u,
                "premultiplied conversion does not unpremultiply twice");

    XImage_init_ex(&pm64, 1, 1, XImageFormat_RGBA64_Premultiplied);
    XImage_fill(&pm64, 0x80402010u);
    {
        uint16_t channel;
        const uint8_t* raw = XImage_bits(&pm64);
        memcpy(&channel, raw, sizeof(channel));
        expect_true(channel == 0x4040u,
                    "fill(uint) keeps unpremultiplied red in RGBA64 storage");
        memcpy(&channel, raw + 2, sizeof(channel));
        expect_true(channel == 0x2020u,
                    "fill(uint) keeps unpremultiplied green in RGBA64 storage");
        memcpy(&channel, raw + 4, sizeof(channel));
        expect_true(channel == 0x1010u,
                    "fill(uint) keeps unpremultiplied blue in RGBA64 storage");
        memcpy(&channel, raw + 6, sizeof(channel));
        expect_true(channel == 0x8080u,
                    "fill(uint) keeps unpremultiplied alpha in RGBA64 storage");
    }
    XImage_deinit_base(&pm64);

    XImage_init_ex(&rgba, 2, 1, XImageFormat_RGBA8888);
    XImage_setPixel(&rgba, 0, 0, 0x80402010u);
    expect_true(XImage_pixel(&rgba, 0, 0) == 0x80402010u,
                "RGBA8888 pixel access uses RGBA byte order");
    XImage_fill(&rgba, 0xff112233u);
    {
        const uint8_t* raw = XImage_bits(&rgba);
#if IS_BIG_ENDIAN
        expect_true(raw && raw[0] == 0xffu && raw[1] == 0x11u &&
                    raw[2] == 0x22u && raw[3] == 0x33u,
                    "fill(uint) writes the host-order RGBA8888 storage value");
#else
        expect_true(raw && raw[0] == 0x33u && raw[1] == 0x22u &&
                    raw[2] == 0x11u && raw[3] == 0xffu,
                    "fill(uint) writes the host-order RGBA8888 storage value");
#endif
    }
    XImage_setPixel(&rgba, 1, 0, 0x80112233u);
    expect_true(XImage_hasAlpha(&rgba), "alpha detection covers RGBA8888");
    XImage_setDevicePixelRatio(&rgba, 2.0f);
    XImage_copy_base(&copy, &rgba);
    expect_true(XImage_devicePixelRatio(&copy) == 2.0f,
                "device pixel ratio survives image copy");
    {
        XSizeF independent;
        XImage_deviceIndependentSize(&rgba, &independent);
        expect_true(fabsf(independent.width - 1.0f) < 1.0e-5f &&
                    fabsf(independent.height - 0.5f) < 1.0e-5f,
                    "deviceIndependentSize divides pixel dimensions by DPR");
        XImage_setDevicePixelRatio(&rgba, 0.0f);
        expect_true(XImage_devicePixelRatio(&rgba) == 0.0f,
                    "QImage-compatible devicePixelRatio accepts zero");
        XImage_setDevicePixelRatio(&rgba, -2.0f);
        expect_true(XImage_devicePixelRatio(&rgba) == -2.0f,
                    "QImage-compatible devicePixelRatio accepts negative values");
        XImage_setDevicePixelRatio(&rgba, 2.0f);
    }
    XImage_invertPixels(&rgba, XImageInvertMode_InvertRgb);
    expect_true(XImage_pixel(&rgba, 0, 0) == 0xffccddeeU,
                "invertPixels preserves alpha and inverts RGB");
    XImage_rgbSwapped(&rgba, &copy);
    expect_true(XImage_pixel(&copy, 0, 0) == 0xffeeddccU,
                "rgbSwapped preserves RGBA alpha and swaps RGB");
    XImage_mirrored(&rgba, true, false, &copy);
    expect_true(XImage_pixel(&copy, 0, 0) == 0x80eeddccu,
                "mirrored uses pixel coordinates for RGBA images");

    XImage_init_ex(&indexed, 1, 1, XImageFormat_Indexed8);
    XImage_setColorSpace(&indexed, XColorSpace_sRgb());
    expect_true(XImage_hasColorSpace(&indexed),
                "indexed image accepts an RGB color space");
    XImage_init_ex(&grayscale, 1, 1, XImageFormat_Grayscale8);
    XImage_setColorSpace(&grayscale, XColorSpace_sRgb());
    expect_true(XImage_hasColorSpace(&grayscale),
                "grayscale image accepts an RGB color space");
    {
        XColorSpace graySpace = XColorSpace_create_gray(
            (XPointF){ 0.3127f, 0.3290f }, XColorSpaceTransfer_Linear, 0.0f);
        XColorSpace storedSpace;
        XImage_setColorSpace(&grayscale, graySpace);
        storedSpace = XImage_colorSpace(&grayscale);
        expect_true(XImage_hasColorSpace(&grayscale) &&
                    XColorSpace_colorModel(&storedSpace) == XColorSpaceModel_Gray,
                    "grayscale image accepts a grayscale color space");
    }
    XImage_init_ex(&cmyk, 1, 1, XImageFormat_CMYK8888);
    XImage_setColorSpace(&cmyk, XColorSpace_sRgb());
    expect_true(!XImage_hasColorSpace(&cmyk),
                "CMYK image rejects an incompatible RGB color space");
    {
        XColorSpace graySpace = XColorSpace_create_gray(
            (XPointF){ 0.3127f, 0.3290f }, XColorSpaceTransfer_Linear, 0.0f);
        /* XImage_init_ex 仅初始化未构造对象；复用已有对象前必须先释放旧数据，
         * 这样共享数据分离产生的克隆也能在引用计数归零时正确回收。 */
        XImage_deinit_base(&rgba);
        XImage_init_ex(&rgba, 1, 1, XImageFormat_ARGB32);
        XImage_setColorSpace(&rgba, graySpace);
    }
    expect_true(!XImage_hasColorSpace(&rgba),
                "RGB image rejects an incompatible grayscale color space");
    for (int i = 0; i < 257; ++i)
        largePalette[i] = 0xff000000u | (uint32_t)i;
    XImage_setColorTable(&indexed, largePalette, 257);
    expect_true(XImage_colorCount(&indexed) == 257 &&
                XImage_color(&indexed, 256) == largePalette[256],
                "setColorTable accepts palettes larger than the pixel index range");
    XImage_setColorCount(&indexed, -1);
    expect_true(XImage_colorCount(&indexed) == 0,
                "setColorCount treats negative values as clearing the palette");
    XImage_setColorTable(&indexed, customPalette, 2);
    XImage_setColorCount(&indexed, 2);
    {
        const int64_t paletteKey = XImage_cacheKey(&indexed);
        XImage_setColorCount(&indexed, 2);
        expect_true(XImage_cacheKey(&indexed) == paletteKey,
                    "setColorCount with the current size is a no-op");
    }
    XImage_setColor(&indexed, 0, 0xff102030u);
    XImage_setColor(&indexed, 1, 0xffa0b0c0u);
    XImage_setColor(&indexed, 4, 0xff405060u);
    expect_true(XImage_colorCount(&indexed) == 5 &&
                XImage_color(&indexed, 0) == 0xff102030u &&
                XImage_color(&indexed, 4) == 0xff405060u,
                "setColor expands a palette for an in-range index");
    XImage_setPixel(&indexed, 0, 0, 1);
    indexedColor = XColor_create_rgb(0xff, 0x00, 0x00, 0xff);
    XImage_setPixelColor(&indexed, 0, 0, &indexedColor);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 1,
                "setPixelColor rejects indexed storage without changing the index");
    XImage_setColorCount(&indexed, 8);
    XImage_fill(&indexed, 0x00000105u);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 5,
                "fill(uint) keeps only the low byte for indexed images");
    {
        XImage mono;
        XImage_init_ex(&mono, 3, 1, XImageFormat_MonoLSB);
        XImage_fill(&mono, 3u);
        expect_true(XImage_pixelIndex(&mono, 0, 0) == 1 &&
                    XImage_pixelIndex(&mono, 2, 0) == 1,
                    "fill(uint) uses the low bit for monochrome images");
        XImage_fill(&mono, 2u);
        expect_true(XImage_pixelIndex(&mono, 0, 0) == 0,
                    "fill(uint) clears monochrome pixels for an even value");
        XImage_deinit_base(&mono);
    }
    XImage_setPixel(&indexed, 0, 0, 1);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 1 &&
                XImage_pixel(&indexed, 0, 0) == 0xffa0b0c0u,
                "indexed pixel access returns index and palette color");
    XImage_invertPixels(&indexed, XImageInvertMode_InvertRgb);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 254 &&
                XImage_pixel(&indexed, 0, 0) == 0u,
                "indexed invertPixels complements indices without changing the palette");
    XImage_convertToFormat_ex(&rgba, XImageFormat_Indexed8, customPalette, 2, 0, &indexed);
    expect_true(XImage_colorCount(&indexed) == 2 &&
                XImage_color(&indexed, 1) == customPalette[1],
                "indexed conversion accepts an explicit color table");

    XImage_init_ex(&reinterpret, 1, 1, XImageFormat_ARGB32);
    XImage_setPixel(&reinterpret, 0, 0, 0xff102030u);
    {
        const int64_t reinterpretKey = XImage_cacheKey(&reinterpret);
        expect_true(XImage_reinterpretAsFormat(&reinterpret, XImageFormat_ARGB32) &&
                    XImage_cacheKey(&reinterpret) == reinterpretKey,
                    "reinterpretAsFormat with the same format is a no-op");
        expect_true(!XImage_reinterpretAsFormat(&reinterpret, XImageFormat_RGB16) &&
                    XImage_format(&reinterpret) == XImageFormat_ARGB32 &&
                    XImage_cacheKey(&reinterpret) == reinterpretKey,
                    "reinterpretAsFormat rejects a different storage depth");
        XImage_copy_base(&reinterpretCopy, &reinterpret);
        expect_true(XImage_reinterpretAsFormat(&reinterpretCopy, XImageFormat_RGB32) &&
                    XImage_format(&reinterpretCopy) == XImageFormat_RGB32 &&
                    XImage_format(&reinterpret) == XImageFormat_ARGB32 &&
                    XImage_cacheKey(&reinterpretCopy) != reinterpretKey,
                    "reinterpretAsFormat detaches shared image data");
    }
    XImage_setColorCount(&indexed, 2);
    expect_true(XImage_reinterpretAsFormat(&indexed, XImageFormat_Grayscale8) &&
                XImage_colorCount(&indexed) == 2,
                "reinterpretAsFormat retains the existing color table");

    XImage_deinit_base(&copy);
    XImage_deinit_base(&converted);
    XImage_deinit_base(&premultiplied);
    XImage_deinit_base(&indexed);
    XImage_deinit_base(&grayscale);
    XImage_deinit_base(&cmyk);
    XImage_deinit_base(&sameSpace);
    XImage_deinit_base(&grayTransformed);
    XImage_deinit_base(&reinterpret);
    XImage_deinit_base(&reinterpretCopy);
    XImage_deinit_base(&rgba);
}

/**
 * @brief 验证 QImage 三种掩码工厂的位序、Alpha 阈值和连通背景语义。
 * @note Qt 6.8 的 createAlphaMask/createHeuristicMask/createMaskFromColor
 *       均返回 MonoLSB；Alpha 掩码使用默认 128 阈值，颜色掩码比较完整
 *       ARGB，启发式掩码只剥离边缘连通背景并保留封闭孔洞。
 */
static void test_image_mask_qt_semantics(void)
{
    XImage image;
    XImage alphaMask;
    XImage rgb32;
    XImage mono;
    XImage rgbMask;
    XImage rgbMaskOut;
    XImage heuristic;
    XImage heuristicLoose;

    XImage_init(&image);
    XImage_init(&alphaMask);
    XImage_init(&rgb32);
    XImage_init(&mono);
    XImage_init(&rgbMask);
    XImage_init(&rgbMaskOut);
    XImage_init(&heuristic);
    XImage_init(&heuristicLoose);

    XImage_init_ex(&image, 3, 1, XImageFormat_ARGB32);
    XImage_setPixel(&image, 0, 0, 0x01336699u);
    XImage_setPixel(&image, 1, 0, 0x80336699u);
    XImage_setPixel(&image, 2, 0, 0xff336699u);
    XImage_createAlphaMask(&image, 0, &alphaMask);
    expect_true(XImage_format(&alphaMask) == XImageFormat_MonoLSB &&
                XImage_pixelIndex(&alphaMask, 0, 0) == 0 &&
                XImage_pixelIndex(&alphaMask, 1, 0) == 1 &&
                XImage_pixelIndex(&alphaMask, 2, 0) == 1 &&
                XImage_pixel(&alphaMask, 0, 0) == 0xffffffffu &&
                XImage_pixel(&alphaMask, 2, 0) == 0xff000000u,
                "QImage createAlphaMask uses MonoLSB and the 128 alpha threshold");

    XImage_init_ex(&rgb32, 1, 1, XImageFormat_RGB32);
    XImage_createAlphaMask(&rgb32, 0, &alphaMask);
    expect_true(XImage_isNull(&alphaMask),
                "QImage createAlphaMask returns a null image for RGB32");

    XImage_init_ex(&mono, 2, 1, XImageFormat_MonoLSB);
    {
        const uint32_t monoColors[2] = {0x00ffffffu, 0xffffffffu};
        XImage_setColorTable(&mono, monoColors, 2);
    }
    XImage_setPixel(&mono, 0, 0, 0);
    XImage_setPixel(&mono, 1, 0, 1);
    XImage_createAlphaMask(&mono, 0, &alphaMask);
    expect_true(XImage_pixelIndex(&alphaMask, 0, 0) == 0 &&
                XImage_pixelIndex(&alphaMask, 1, 0) == 1,
                "QImage createAlphaMask converts depth-one color-table alpha");

    XImage_createMaskFromColor(&image, 0xff336699u, XImageMask_InColor, &rgbMask);
    XImage_createMaskFromColor(&image, 0xff336699u, XImageMask_OutColor, &rgbMaskOut);
    expect_true(XImage_format(&rgbMask) == XImageFormat_MonoLSB &&
                XImage_pixelIndex(&rgbMask, 0, 0) == 0 &&
                XImage_pixelIndex(&rgbMask, 1, 0) == 0 &&
                XImage_pixelIndex(&rgbMask, 2, 0) == 1 &&
                XImage_pixelIndex(&rgbMaskOut, 0, 0) == 1 &&
                XImage_pixelIndex(&rgbMaskOut, 2, 0) == 0 &&
                XImage_constBits(&rgbMaskOut)[0] == 0xfbu,
                "QImage createMaskFromColor compares full ARGB and supports inversion");

    XImage_deinit_base(&image);
    XImage_init_ex(&image, 5, 5, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff000000u);
    for (int y = 1; y <= 3; ++y)
        for (int x = 1; x <= 3; ++x)
            if (x != 2 || y != 2)
                XImage_setPixel(&image, x, y, 0xffffffffu);
    XImage_createHeuristicMask(&image, true, &heuristic);
    expect_true(XImage_format(&heuristic) == XImageFormat_MonoLSB &&
                XImage_pixelIndex(&heuristic, 0, 0) == 0 &&
                XImage_pixelIndex(&heuristic, 1, 1) == 1 &&
                XImage_pixelIndex(&heuristic, 2, 2) == 1,
                "QImage createHeuristicMask preserves an enclosed background hole");
    XImage_createHeuristicMask(&image, false, &heuristicLoose);
    expect_true(XImage_pixelIndex(&heuristicLoose, 0, 2) == 1 &&
                XImage_pixelIndex(&heuristicLoose, 2, 2) == 1,
                "QImage heuristic mask clipTight=false keeps object edge neighbors");

    XImage_deinit_base(&heuristicLoose);
    XImage_deinit_base(&heuristic);
    XImage_deinit_base(&rgbMaskOut);
    XImage_deinit_base(&rgbMask);
    XImage_deinit_base(&mono);
    XImage_deinit_base(&rgb32);
    XImage_deinit_base(&alphaMask);
    XImage_deinit_base(&image);
}

/**
 * @brief 验证 QImage 静态像素格式映射和颜色填充重载。
 * @note Qt 的 QPixelFormat 比较包含通道模型、Alpha 语义、类型解释和
 *       字节序；此测试确保每个公开图像格式都能精确往返，而不是仅按位深
 *       度或通道数量猜测。
 */
static void test_image_format_mapping_and_color_fill(void)
{
    XImage image;
    XImage indexed;
    XImage mono;
    XImage highDepth;
    XPixelFormat pixelFormat;
    XPixelFormat legacyPixelFormat;
    XColor color;
    const uint32_t palette[2] = { 0xff000000u, 0xff336699u };
    int format;

    for (format = 0; format < XImageFormat_NImageFormats; ++format)
    {
        XImageFormat imageFormat = (XImageFormat)format;
        XImageFormat expectedFormat = imageFormat == XImageFormat_MonoLSB
            ? XImageFormat_Mono : imageFormat;
        pixelFormat = XImageFormat_toPixelFormat(imageFormat);
        legacyPixelFormat = XImageFormat_pixelFormat(imageFormat);
        expect_true(XImageFormat_toImageFormat(pixelFormat) == expectedFormat,
                    "QImage format toPixelFormat/toImageFormat round trip");
        expect_true(XPixelFormat_equals(&pixelFormat, &legacyPixelFormat),
                    "QImage pixelFormat() exposes the static mapping");
    }

    pixelFormat = XImageFormat_toPixelFormat(XImageFormat_Mono);
    expect_true(pixelFormat.m_model == XPixelFormatModel_Indexed &&
                pixelFormat.m_redSize == 1 &&
                pixelFormat.m_typeInterpretation == XPixelFormatType_UnsignedByte,
                "QImage Mono uses an indexed one-bit pixel format");
    pixelFormat = XImageFormat_toPixelFormat(XImageFormat_Alpha8);
    expect_true(pixelFormat.m_model == XPixelFormatModel_Alpha &&
                pixelFormat.m_alphaUsage == XPixelFormatAlpha_Uses &&
                pixelFormat.m_alphaSize == 8 &&
                !pixelFormat.m_premultiplied,
                "QImage Alpha8 uses the non-premultiplied alpha-only pixel model");
    pixelFormat = XImageFormat_toPixelFormat(XImageFormat_RGBX8888);
    expect_true(pixelFormat.m_alphaSize == 8 &&
                pixelFormat.m_alphaUsage == XPixelFormatAlpha_Ignores &&
                pixelFormat.m_alphaPosition == XPixelFormatAlpha_AtEnd,
                "QImage RGBX8888 preserves ignored trailing alpha bits");
    {
        XPixelFormat changed = pixelFormat;
        changed.m_channelCount = (uint8_t)(changed.m_channelCount + 1u);
        expect_true(!XPixelFormat_equals(&pixelFormat, &changed),
                    "QPixelFormat equality includes the derived channel count");
    }
#if IS_BIG_ENDIAN
    expect_true(pixelFormat.m_byteOrder == XPixelFormatByteOrder_BigEndian,
                "QPixelFormat resolves CurrentSystemEndian to big endian");
#else
    expect_true(pixelFormat.m_byteOrder == XPixelFormatByteOrder_LittleEndian,
                "QPixelFormat resolves CurrentSystemEndian to little endian");
#endif

    XImage_init_ex(&image, 1, 1, XImageFormat_ARGB32_Premultiplied);
    color = XColor_create_rgb(0x33, 0x66, 0x99, 0x80);
    XImage_fillColor(&image, &color);
    expect_true((XImage_pixel(&image, 0, 0) & 0xff000000u) == 0x80000000u,
                "QImage fill(QColor) preserves alpha on premultiplied storage");
    {
        const uint32_t filled = XImage_pixel(&image, 0, 0);
        const unsigned red = (filled >> 16) & 0xffu;
        const unsigned green = (filled >> 8) & 0xffu;
        const unsigned blue = filled & 0xffu;
        expect_true(red >= 0x30u && red <= 0x36u &&
                    green >= 0x63u && green <= 0x69u &&
                    blue >= 0x96u && blue <= 0x9cu,
                "QImage fill(QColor) stores premultiplied RGB without double conversion");
    }

    XImage_init_ex(&indexed, 1, 1, XImageFormat_Indexed8);
    XImage_setColorTable(&indexed, palette, 2);
    XImage_fillColor(&indexed, &color);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 0,
                "QImage fill(QColor) uses index zero when palette has no exact color");
    color = XColor_create_rgb(0x33, 0x66, 0x99, 0xff);
    XImage_fillColor(&indexed, &color);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 1,
                "QImage fill(QColor) selects an exact indexed palette entry");

    XImage_init_ex(&mono, 1, 1, XImageFormat_Mono);
    color = XColor_create_rgb(0xff, 0xff, 0xff, 0xff);
    XImage_fillColor(&mono, &color);
    expect_true(XImage_pixelIndex(&mono, 0, 0) == 1,
                "QImage fill(QColor) maps opaque white to monochrome color1");
    color = XColor_create_rgb(0, 0, 0, 0xff);
    XImage_fillColor(&mono, &color);
    expect_true(XImage_pixelIndex(&mono, 0, 0) == 0,
                "QImage fill(QColor) maps non-white monochrome colors to color0");

    /* QImage::pixelColor() keeps the native 16-bit value for these formats;
       exercise the same contract instead of accepting an 8-bit truncation. */
    XImage_init_ex(&highDepth, 1, 1, XImageFormat_Grayscale16);
    {
        const uint16_t gray16 = 0x1234u;
        XColor highColor;
        memcpy(XImage_bits(&highDepth), &gray16, sizeof(gray16));
        highColor = XImage_pixelColor(&highDepth, 0, 0);
        expect_true(highColor.m_spec == XColor_Rgb &&
                    highColor.m_comp1 == gray16 &&
                    highColor.m_comp2 == gray16 &&
                    highColor.m_comp3 == gray16 &&
                    highColor.m_alpha == 0xffffu,
                    "QImage pixelColor preserves Grayscale16 channels");
    }
    XImage_deinit_base(&highDepth);
    XImage_init_ex(&highDepth, 1, 1, XImageFormat_RGBA64);
    {
        const uint16_t rgba64[4] = { 0x1234u, 0x5678u, 0x9abcu, 0xdef0u };
        XColor highColor;
        memcpy(XImage_bits(&highDepth), rgba64, sizeof(rgba64));
        highColor = XImage_pixelColor(&highDepth, 0, 0);
        expect_true(highColor.m_spec == XColor_Rgb &&
                    highColor.m_comp1 == rgba64[0] &&
                    highColor.m_comp2 == rgba64[1] &&
                    highColor.m_comp3 == rgba64[2] &&
                    highColor.m_alpha == rgba64[3],
                    "QImage pixelColor preserves RGBA64 channels");
    }
    XImage_deinit_base(&highDepth);

    XImage_mirror(&image, false, false);
    XImage_rgbSwap(&image);
    expect_true(XImage_width(&image) == 1 && XImage_height(&image) == 1,
                "QImage mirror/rgbSwap compatibility aliases preserve the image");
    XImage_deinit_base(&mono);
    XImage_deinit_base(&indexed);
    XImage_deinit_base(&image);
}



/**
 * @brief 验证 QImage 文本元数据使用 QMap 按键排序、允许空 key/value。
 * @note 对齐 Qt 6.8 QImage::text/setText/textKeys：text("") 返回聚合文本，
 *       setText 重复键覆盖原值，textKeys 返回升序键序列。
 */
static void test_image_text_metadata_sorted_map(void)
{
    XImage image;
    XImage copy;
    XImage mirrored;
    XStringList* textKeys;
    XStringList* emptyTextKeys;
    const XString* textKey0;
    const XString* copyTextKey0;
    const char* text1;
    const char* text2;
    const char* text3;
    const char* allText;
    XColorSpace mirroredSpace;

    XImage_init(&copy);
    XImage_init(&mirrored);
    XImage_init_ex(&image, 1, 1, XImageFormat_ARGB32);
    XImage_setColorSpace(&image, XColorSpace_sRgb());
    XImage_setText_2(&image, "zeta", "1");
    XImage_setText_2(&image, "alpha", "2");
    XImage_setText_2(&image, "", "empty");
    XImage_setText_2(&image, "alpha", "22");
    XImage_setText_2(&image, "empty-value", "");

    expect_true(XImage_textCount(&image) == 4,
                "QImage text metadata count after inserts and overwrite");
    textKey0 = XImage_textKey_const(&image, 0);
    text1 = XImage_textKey_2(&image, 1);
    text2 = XImage_textKey_2(&image, 2);
    text3 = XImage_textKey_2(&image, 3);
    expect_true(textKey0 && XString_isEmpty_base((const XContainer*)textKey0) &&
                text1 && strcmp(text1, "alpha") == 0 &&
                text2 && strcmp(text2, "empty-value") == 0 &&
                text3 && strcmp(text3, "zeta") == 0,
                "QImage textKeys returns QMap ascending order");

    textKeys = XImage_textKeys(&image);
    expect_true(textKeys && XStringList_size_base((const XContainer*)textKeys) == 4 &&
                XString_equals_utf8((const XString*)XStringList_at_base(
                    (const XVector*)textKeys, 1), "alpha", XChar_CaseSensitive) &&
                XString_equals_utf8((const XString*)XStringList_at_base(
                    (const XVector*)textKeys, 3), "zeta", XChar_CaseSensitive),
                "QImage textKeys returns a sorted deep-copy list");
    if (textKeys)
    {
        XStringList_push_back_utf8(textKeys, "independent");
        expect_true(XStringList_size_base((const XContainer*)textKeys) == 5 &&
                    XImage_textCount(&image) == 4,
                    "QImage textKeys result detaches on list mutation");
        XStringList_delete_base((XClass*)textKeys);
    }
    emptyTextKeys = XImage_textKeys(NULL);
    expect_true(emptyTextKeys &&
                XStringList_size_base((const XContainer*)emptyTextKeys) == 0,
                "null QImage textKeys returns an empty list");
    if (emptyTextKeys) XStringList_delete_base((XClass*)emptyTextKeys);

    expect_true(XImage_text_2(&image, "alpha") &&
                strcmp(XImage_text_2(&image, "alpha"), "22") == 0,
                "setText overwrites an existing key in place");
    expect_true(XImage_text_2(&image, "empty-value") != NULL &&
                strcmp(XImage_text_2(&image, "empty-value"), "") == 0,
                "existing empty value is distinct from a missing key");
    expect_true(XImage_text_2(&image, "missing") != NULL &&
                strcmp(XImage_text_2(&image, "missing"), "") == 0,
                "missing text key returns an empty string like Qt");
    expect_true(XImage_text_2(NULL, "missing") != NULL &&
                strcmp(XImage_text_2(NULL, "missing"), "") == 0,
                "null QImage text returns an empty string like Qt");

    allText = XImage_text_2(&image, "");
    expect_true(allText &&
                strcmp(allText, ": empty\n\nalpha: 22\n\nempty-value: \n\nzeta: 1") == 0,
                "text(\"\") aggregates all text in sorted key order");

    XImage_mirrored(&image, true, false, &mirrored);
    mirroredSpace = XImage_colorSpace(&mirrored);
    expect_true(XImage_hasColorSpace(&mirrored) &&
                    XColorSpace_isSRgb(&mirroredSpace) &&
                    strcmp(XImage_text_2(&mirrored, "alpha"), "22") == 0,
                "QImage mirrored preserves color space and text metadata");

    XImage_copy_base(&copy, &image);
    copyTextKey0 = XImage_textKey_const(&copy, 0);
    expect_true(XImage_textCount(&copy) == 4 &&
                copyTextKey0 && XString_isEmpty_base((const XContainer*)copyTextKey0) &&
                strcmp(XImage_textKey_2(&copy, 1), "alpha") == 0 &&
                strcmp(XImage_textKey_2(&copy, 2), "empty-value") == 0 &&
                strcmp(XImage_textKey_2(&copy, 3), "zeta") == 0,
                "image copy preserves sorted text metadata order");

    XImage_deinit_base(&copy);
    XImage_deinit_base(&mirrored);
    XImage_deinit_base(&image);
}

/**
 * @brief 验证 QImage 灰度判定和元数据 setter 的无变化语义。
 * @note allGray() 检查完整颜色表而不是只检查已引用像素；isGrayscale()
 *       另外要求索引色表严格等于 i -> qRgb(i,i,i)，空图不是灰度图像。
 */
static void test_image_gray_and_metadata_noop_contract(void)
{
    XImage indexed;
    XImage mono;
    XImage gray;
    XImage nullImage;
    XImage shared;
    XPoint zero = { 0, 0 };
    int64_t key;

    XImage_init(&nullImage);
    expect_true(XImage_allGray(&nullImage) && !XImage_isGrayscale(&nullImage),
                "QImage null allGray/isGrayscale results");

    XImage_init_ex(&indexed, 1, 1, XImageFormat_Indexed8);
    XImage_setColorCount(&indexed, 2);
    XImage_setColor(&indexed, 0, 0xff101010u);
    XImage_setColor(&indexed, 1, 0xffff0000u);
    XImage_setPixel(&indexed, 0, 0, 0);
    expect_true(!XImage_allGray(&indexed) && !XImage_isGrayscale(&indexed),
                "QImage indexed gray checks include an unused palette entry");
    XImage_setColor(&indexed, 0, 0xff000000u);
    XImage_setColor(&indexed, 1, 0xff010101u);
    expect_true(XImage_allGray(&indexed) && XImage_isGrayscale(&indexed),
                "QImage indexed canonical grayscale palette checks");

    XImage_init_ex(&mono, 1, 1, XImageFormat_Mono);
    XImage_setColorCount(&mono, 2);
    XImage_setColor(&mono, 0, 0xff000000u);
    XImage_setColor(&mono, 1, 0xffffffffu);
    expect_true(XImage_allGray(&mono) && !XImage_isGrayscale(&mono),
                "QImage monochrome allGray true but isGrayscale false");

    XImage_init_ex(&gray, 1, 1, XImageFormat_Grayscale8);
    expect_true(XImage_allGray(&gray) && XImage_isGrayscale(&gray),
                "QImage native grayscale formats report both gray predicates");

    key = XImage_cacheKey(&gray);
    XImage_setDotsPerMeterX(&gray, 0);
    XImage_setDotsPerMeterY(&gray, 0);
    XImage_setOffset(&gray, &zero);
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage zero/default metadata writes do not change cache key");
    XImage_setDotsPerMeterX(&gray, 2835);
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage metadata-only DPI writes preserve cache key");
    XImage_setDotsPerMeterX(&gray, 2835);
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage repeated dotsPerMeter write is a no-op");
    XImage_setText_2(&gray, "Description", "gray image");
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage metadata-only text writes preserve cache key");
    XImage_setDevicePixelRatio(&gray, 2.0f);
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage metadata-only DPR writes preserve cache key");
    XImage_setOffset(&gray, &(XPoint){ 1, 2 });
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage metadata-only offset writes preserve cache key");
    XImage_setOffset(&gray, &(XPoint){ 1, 2 });
    expect_true(XImage_cacheKey(&gray) == key,
                "QImage repeated offset write is a no-op");

    XImage_init(&shared);
    XImage_copy_base(&shared, &gray);
    XImage_setText_2(&shared, "Description", "detached gray image");
    expect_true(XImage_cacheKey(&shared) != key &&
                    XImage_cacheKey(&gray) == key &&
                    strcmp(XImage_text_2(&gray, "Description"), "gray image") == 0 &&
                    strcmp(XImage_text_2(&shared, "Description"),
                           "detached gray image") == 0,
                "QImage shared metadata write detaches only the modified image");

    XImage_deinit_base(&shared);
    XImage_deinit_base(&gray);
    XImage_deinit_base(&mono);
    XImage_deinit_base(&indexed);
    XImage_deinit_base(&nullImage);
}



#if XSCREEN_ON

/** @brief XScreen 信号探测数据。 */
typedef struct ScreenProbe
{
    int geometry;                      /**< geometryChanged 计数。 */
    int availableGeometry;             /**< availableGeometryChanged 计数。 */
    int physicalSize;                  /**< physicalSizeChanged 计数。 */
    int physicalDpi;                   /**< physicalDotsPerInchChanged 计数。 */
    int logicalDpi;                    /**< logicalDotsPerInchChanged 计数。 */
    int virtualGeometry;               /**< virtualGeometryChanged 计数。 */
    int primaryOrientation;            /**< primaryOrientationChanged 计数。 */
    int orientation;                   /**< orientationChanged 计数。 */
    int refreshRate;                   /**< refreshRateChanged 计数。 */
    XRect lastRect;                    /**< 最近一次矩形参数。 */
    XSizeF lastSize;                   /**< 最近一次尺寸参数。 */
    float lastDpi;                     /**< 最近一次 DPI/刷新率参数。 */
    XScreenOrientation lastOrientation;/**< 最近一次方向参数。 */
} ScreenProbe;

static ScreenProbe g_screenProbe;

static void screen_probe_geometrySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XRect, rect);
    g_screenProbe.geometry++;
    g_screenProbe.lastRect = rect;
}
static void screen_probe_availableGeometrySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XRect, rect);
    g_screenProbe.availableGeometry++;
    g_screenProbe.lastRect = rect;
}
static void screen_probe_physicalSizeSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XSizeF, size);
    g_screenProbe.physicalSize++;
    g_screenProbe.lastSize = size;
}
static void screen_probe_physicalDpiSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, float, dpi);
    g_screenProbe.physicalDpi++;
    g_screenProbe.lastDpi = dpi;
}
static void screen_probe_logicalDpiSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, float, dpi);
    g_screenProbe.logicalDpi++;
    g_screenProbe.lastDpi = dpi;
}
static void screen_probe_virtualGeometrySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XRect, rect);
    g_screenProbe.virtualGeometry++;
    g_screenProbe.lastRect = rect;
}
static void screen_probe_primaryOrientationSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreenOrientation, orientation);
    g_screenProbe.primaryOrientation++;
    g_screenProbe.lastOrientation = orientation;
}
static void screen_probe_orientationSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreenOrientation, orientation);
    g_screenProbe.orientation++;
    g_screenProbe.lastOrientation = orientation;
}
static void screen_probe_refreshRateSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, float, refreshRate);
    g_screenProbe.refreshRate++;
    g_screenProbe.lastDpi = refreshRate;
}

/** @brief XScreen 全量契约测试：默认值、属性、信号、方向数学、注册表、移动拷贝。 */
static void test_screen_contract(void)
{
    XScreen* a;
    XScreen* b;
    XScreen* copy;
    XScreen* moved;
    XScreen* hit;
    XVector* list;
    XRect g;
    XRect vg;
    XRect avg;
    XSize s;
    XSizeF fs;
    XImageTransform t;
    XString* name;
    XPoint pt;
    XPixmap* grab;
    XScreen* explicitSiblings[1];

    /* ---------- 默认值 ---------- */
    a = XScreen_create();
    expect_true(a != NULL, "XScreen_create");
    expect_true(XScreen_depth(a) == 32, "XScreen 默认深度 32");
    g = XScreen_geometry(a);
    expect_true(g.x == 0 && g.y == 0 && g.width == 0 && g.height == 0,
                "XScreen 默认几何 (0,0,0,0)");
    s = XScreen_size(a);
    expect_true(s.width == 0 && s.height == 0, "XScreen 默认尺寸 (0,0)");
    fs = XScreen_physicalSize(a);
    expect_true(fs.width == 0.0f && fs.height == 0.0f, "XScreen 默认物理尺寸 0");
    expect_true(XScreen_physicalDotsPerInchX(a) == 0.0f &&
                XScreen_physicalDotsPerInchY(a) == 0.0f &&
                XScreen_physicalDotsPerInch(a) == 0.0f, "XScreen 默认物理 DPI 0");
    expect_true(XScreen_logicalDotsPerInchX(a) == 96.0f &&
                XScreen_logicalDotsPerInchY(a) == 96.0f, "XScreen 默认逻辑 DPI 96");
    expect_true(XScreen_devicePixelRatio(a) == 1.0f, "XScreen 默认 DPR 1");
    expect_true(XScreen_orientation(a) == XScreenOrientation_Primary,
                "XScreen 默认当前方向 Primary");
    expect_true(XScreen_nativeOrientation(a) == XScreenOrientation_Primary,
                "XScreen 默认原生方向 Primary");
    expect_true(XScreen_primaryOrientation(a) == XScreenOrientation_Landscape,
                "XScreen 0x0 几何主方向按横屏");
    expect_true(XScreen_refreshRate(a) == 60.0f, "XScreen 默认刷新率 60");
    expect_true(XScreen_handle(a) == NULL, "XScreen 默认句柄 NULL");
    g = XScreen_availableGeometry(a);
    expect_true(g.width == 0 && g.height == 0, "XScreen 可用几何默认跟随几何");
    expect_true(XScreen_primaryScreen() == NULL, "注册表默认无主屏幕");
    list = XScreen_screens();
    expect_true(list != NULL && XVector_size_base((const XContainer*)list) == 0,
                "注册表默认为空");
    if (list) XVector_delete_base((XClass*)list);

    /* ---------- 方向数学（对标 Qt 6.8.3） ---------- */
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Landscape,
                                     XScreenOrientation_Portrait) == 90,
                "angleBetween L->P = 90");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Portrait,
                                     XScreenOrientation_Landscape) == 270,
                "angleBetween P->L = 270");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Landscape,
                                     XScreenOrientation_InvertedLandscape) == 180,
                "angleBetween L->IL = 180");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_InvertedPortrait,
                                     XScreenOrientation_InvertedLandscape) == 270,
                "angleBetween IP->IL = 270");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Landscape,
                                     XScreenOrientation_Landscape) == 0,
                "angleBetween 同方向 = 0");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Primary,
                                     XScreenOrientation_Portrait) == 90,
                "Primary(横屏) -> P = 90");
    expect_true(!XScreen_isPortrait(a, XScreenOrientation_Primary) &&
                XScreen_isLandscape(a, XScreenOrientation_Primary),
                "Primary(横屏) 解析为横屏");
    XScreen_setPrimaryOrientation(a, XScreenOrientation_Portrait);
    expect_true(XScreen_isPortrait(a, XScreenOrientation_Primary) &&
                !XScreen_isLandscape(a, XScreenOrientation_Primary),
                "Primary(竖屏) 解析为竖屏");
    expect_true(XScreen_angleBetween(a, XScreenOrientation_Primary,
                                     XScreenOrientation_Landscape) == 270,
                "Primary(P) -> L = 270");
    expect_true(XScreen_isPortrait(a, XScreenOrientation_Portrait) &&
                XScreen_isPortrait(a, XScreenOrientation_InvertedPortrait) &&
                !XScreen_isPortrait(a, XScreenOrientation_Landscape),
                "isPortrait 各方向判断");
    expect_true(XScreen_isLandscape(a, XScreenOrientation_Landscape) &&
                XScreen_isLandscape(a, XScreenOrientation_InvertedLandscape) &&
                !XScreen_isLandscape(a, XScreenOrientation_Portrait),
                "isLandscape 各方向判断");

    /* transformBetween：先平移再旋转，字段映射 x'=m11*x+m21*y+dx */
    XScreen_setPrimaryOrientation(a, XScreenOrientation_Landscape);
    t = XScreen_transformBetween(a, XScreenOrientation_Landscape,
                                 XScreenOrientation_Portrait,
                                 &(XRect){0, 0, 100, 200});
    expect_true(t.m11 == 0.0f && t.m12 == 1.0f && t.m21 == -1.0f &&
                t.m22 == 0.0f && t.dx == 100.0f && t.dy == 0.0f &&
                t.m13 == 0.0f && t.m23 == 0.0f && t.m33 == 1.0f,
                "transformBetween 90 度矩阵");
    expect_true(0.0f * 0 + 1.0f * 0 + 100.0f == 100.0f &&
                -1.0f * 0 + 0.0f * 0 + 0.0f == 0.0f,
                "transformBetween 90 度 (0,0)->(100,0)");
    expect_true(t.m11 * 200.0f + t.m21 * 100.0f + t.dx == 0.0f &&
                t.m12 * 200.0f + t.m22 * 100.0f + t.dy == 200.0f,
                "transformBetween 90 度 (200,100) 映射");
    t = XScreen_transformBetween(a, XScreenOrientation_Landscape,
                                 XScreenOrientation_InvertedLandscape,
                                 &(XRect){0, 0, 100, 200});
    expect_true(t.m11 == -1.0f && t.m22 == -1.0f && t.dx == 100.0f &&
                t.dy == 200.0f && t.m12 == 0.0f && t.m21 == 0.0f,
                "transformBetween 180 度矩阵");
    t = XScreen_transformBetween(a, XScreenOrientation_Portrait,
                                 XScreenOrientation_Landscape,
                                 &(XRect){0, 0, 100, 200});
    expect_true(t.m11 == 0.0f && t.m12 == -1.0f && t.m21 == 1.0f &&
                t.m22 == 0.0f && t.dx == 0.0f && t.dy == 200.0f,
                "transformBetween 270 度矩阵");
    t = XScreen_transformBetween(a, XScreenOrientation_Portrait,
                                 XScreenOrientation_Portrait,
                                 &(XRect){0, 0, 100, 200});
    expect_true(t.m11 == 1.0f && t.m22 == 1.0f && t.dx == 0.0f && t.dy == 0.0f,
                "transformBetween 同方向为单位矩阵");

    /* mapBetween：竖屏/横屏互换交换 x/y 与宽高 */
    g = XScreen_mapBetween(a, XScreenOrientation_Portrait,
                           XScreenOrientation_Landscape,
                           &(XRect){5, 7, 100, 200});
    expect_true(g.x == 7 && g.y == 5 && g.width == 200 && g.height == 100,
                "mapBetween P->L 交换 x/y 与宽高");
    g = XScreen_mapBetween(a, XScreenOrientation_Landscape,
                           XScreenOrientation_InvertedLandscape,
                           &(XRect){5, 7, 100, 200});
    expect_true(g.x == 5 && g.y == 7 && g.width == 100 && g.height == 200,
                "mapBetween 同面保持原矩形");

    /* ---------- 属性设置与读取 ---------- */
    XScreen_setName_2(a, "HDMI-1");
    expect_true(strcmp(XScreen_name_2(a), "HDMI-1") == 0, "name UTF-8 读取");
    name = XScreen_name(a);
    expect_true(name != NULL && strcmp(XString_toUtf8(name), "HDMI-1") == 0,
                "name 副本");
    if (name) XString_delete_base((XClass*)name);
    XScreen_setName(a, NULL);
    expect_true(XScreen_name_const(a) == NULL, "name 可清空");
    XScreen_setName_2(a, "HDMI-1");
    XScreen_setManufacturer_2(a, "ACME");
    XScreen_setModel_2(a, "M-27");
    XScreen_setSerialNumber_2(a, "SN-123");
    expect_true(strcmp(XScreen_manufacturer_2(a), "ACME") == 0, "manufacturer");
    expect_true(strcmp(XScreen_model_2(a), "M-27") == 0, "model");
    expect_true(strcmp(XScreen_serialNumber_2(a), "SN-123") == 0, "serialNumber");
    XScreen_setDepth(a, 24);
    expect_true(XScreen_depth(a) == 24, "depth 设置");
    XScreen_setHandle(a, (XScreenPlatform*)(uintptr_t)0x1);
    expect_true(XScreen_handle(a) == (XScreenPlatform*)(uintptr_t)0x1, "handle 设置");
    XScreen_setOrientation(a, XScreenOrientation_Landscape);
    expect_true(XScreen_orientation(a) == XScreenOrientation_Landscape,
                "orientation 设置");
    XScreen_setNativeOrientation(a, XScreenOrientation_Portrait);
    expect_true(XScreen_nativeOrientation(a) == XScreenOrientation_Portrait,
                "nativeOrientation 设置");
    XScreen_setRefreshRate(a, 75.0f);
    expect_true(XScreen_refreshRate(a) == 75.0f, "refreshRate 设置");
    XScreen_setLogicalDotsPerInch(a, 110.0f, 115.0f);
    expect_true(XScreen_logicalDotsPerInchX(a) == 110.0f &&
                XScreen_logicalDotsPerInchY(a) == 115.0f, "logicalDpi 设置");
    XScreen_setDevicePixelRatio(a, 2.0f);
    expect_true(XScreen_devicePixelRatio(a) == 2.0f, "devicePixelRatio 设置");

    /* 主方向自动推导（未锁定）：宽>=高为横屏，反之为竖屏 */
    b = XScreen_create();
    XScreen_setGeometry(b, &(XRect){0, 0, 800, 600});
    expect_true(XScreen_primaryOrientation(b) == XScreenOrientation_Landscape,
                "主方向按几何自动推导横屏");
    XScreen_setGeometry(b, &(XRect){0, 0, 600, 800});
    expect_true(XScreen_primaryOrientation(b) == XScreenOrientation_Portrait,
                "主方向按几何自动推导竖屏");
    XScreen_delete_base((XClass*)b);

    /* 几何：物理 DPI = 800/200*25.4 = 101.6 */
    XScreen_setGeometry(a, &(XRect){0, 0, 800, 600});
    g = XScreen_geometry(a);
    expect_true(g.x == 0 && g.y == 0 && g.width == 800 && g.height == 600,
                "geometry 设置");
    s = XScreen_size(a);
    expect_true(s.width == 800 && s.height == 600, "size 跟随几何");
    g = XScreen_availableGeometry(a);
    expect_true(g.width == 800 && g.height == 600,
                "可用几何未设置时跟随几何");
    XScreen_setPhysicalSize(a, &(XSizeF){200.0f, 150.0f});
    expect_true(XScreen_physicalDotsPerInchX(a) > 101.5f &&
                XScreen_physicalDotsPerInchX(a) < 101.7f, "physicalDpiX 101.6");
    expect_true(XScreen_physicalDotsPerInchY(a) > 101.5f &&
                XScreen_physicalDotsPerInchY(a) < 101.7f, "physicalDpiY 101.6");
    expect_true(XScreen_physicalDotsPerInch(a) > 101.5f &&
                XScreen_physicalDotsPerInch(a) < 101.7f, "physicalDpi 均值 101.6");
    XScreen_setAvailableGeometry(a, &(XRect){10, 20, 780, 570});
    g = XScreen_availableGeometry(a);
    expect_true(g.x == 10 && g.y == 20 && g.width == 780 && g.height == 570,
                "availableGeometry 独立设置");
    s = XScreen_availableSize(a);
    expect_true(s.width == 780 && s.height == 570, "availableSize 读取");

    /* ---------- 信号（9 个全量连接） ---------- */
    memset(&g_screenProbe, 0, sizeof(g_screenProbe));
    XObject_connect_2((XObject*)a, XSignal(XScreen_geometryChanged_signal),
                      screen_probe_geometrySlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_availableGeometryChanged_signal),
                      screen_probe_availableGeometrySlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_physicalSizeChanged_signal),
                      screen_probe_physicalSizeSlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_physicalDotsPerInchChanged_signal),
                      screen_probe_physicalDpiSlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_logicalDotsPerInchChanged_signal),
                      screen_probe_logicalDpiSlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_virtualGeometryChanged_signal),
                      screen_probe_virtualGeometrySlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_primaryOrientationChanged_signal),
                      screen_probe_primaryOrientationSlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_orientationChanged_signal),
                      screen_probe_orientationSlot);
    XObject_connect_2((XObject*)a, XSignal(XScreen_refreshRateChanged_signal),
                      screen_probe_refreshRateSlot);

    XScreen_setOrientation(a, XScreenOrientation_Portrait);
    expect_true(g_screenProbe.orientation == 1 &&
                g_screenProbe.lastOrientation == XScreenOrientation_Portrait,
                "orientationChanged 发射一次且参数正确");
    XScreen_setOrientation(a, XScreenOrientation_Portrait);
    expect_true(g_screenProbe.orientation == 1, "orientationChanged 同值不重复发射");

    XScreen_setRefreshRate(a, 120.0f);
    expect_true(g_screenProbe.refreshRate == 1 && g_screenProbe.lastDpi == 120.0f,
                "refreshRateChanged 发射");
    XScreen_setRefreshRate(a, 120.0f);
    expect_true(g_screenProbe.refreshRate == 1, "refreshRateChanged 同值不重复发射");

    XScreen_setPrimaryOrientation(a, XScreenOrientation_Portrait);
    expect_true(g_screenProbe.primaryOrientation == 1 &&
                g_screenProbe.lastOrientation == XScreenOrientation_Portrait,
                "primaryOrientationChanged 发射");
    XScreen_setPrimaryOrientation(a, XScreenOrientation_Portrait);
    expect_true(g_screenProbe.primaryOrientation == 1,
                "primaryOrientationChanged 同值不重复发射");

    XScreen_setPhysicalSize(a, &(XSizeF){160.0f, 120.0f});
    expect_true(g_screenProbe.physicalSize == 1 &&
                g_screenProbe.lastSize.width == 160.0f &&
                g_screenProbe.lastSize.height == 120.0f,
                "physicalSizeChanged 发射");
    expect_true(g_screenProbe.physicalDpi == 1,
                "物理尺寸变化导致 physicalDotsPerInchChanged");

    XScreen_setLogicalDotsPerInch(a, 120.0f, 120.0f);
    expect_true(g_screenProbe.logicalDpi == 1 && g_screenProbe.lastDpi == 120.0f,
                "logicalDotsPerInchChanged 发射");

    XScreen_setDevicePixelRatio(a, 3.0f);
    expect_true(g_screenProbe.physicalDpi == 2 && XScreen_devicePixelRatio(a) == 3.0f,
                "devicePixelRatio NOTIFY physicalDotsPerInchChanged");

    XScreen_setGeometry(a, &(XRect){10, 10, 800, 600});
    expect_true(g_screenProbe.geometry == 1 &&
                g_screenProbe.lastRect.x == 10 && g_screenProbe.lastRect.y == 10,
                "geometryChanged 发射");
    expect_true(g_screenProbe.availableGeometry == 0,
                "显式可用几何不随几何变化触发 availableGeometryChanged");
    expect_true(g_screenProbe.virtualGeometry == 1 &&
                g_screenProbe.lastRect.x == 10 && g_screenProbe.lastRect.width == 800,
                "虚拟几何变化触发 virtualGeometryChanged");

    XScreen_setAvailableGeometry(a, &(XRect){20, 20, 780, 570});
    expect_true(g_screenProbe.availableGeometry == 1 &&
                g_screenProbe.lastRect.x == 20,
                "availableGeometryChanged 发射");
    expect_true(g_screenProbe.virtualGeometry == 1,
                "可用几何变化不改变虚拟几何（不发射）");

    /* ---------- 注册表与虚拟桌面 ---------- */
    XScreen_register(a);
    list = XScreen_screens();
    expect_true(list != NULL && XVector_size_base((const XContainer*)list) == 1, "注册后 1 个屏幕");
    if (list) XVector_delete_base((XClass*)list);
    expect_true(XScreen_primaryScreen() == NULL, "注册不自动设置主屏幕");
    XScreen_setPrimary(a);
    expect_true(XScreen_primaryScreen() == a, "setPrimary 后主屏幕为 a");

    b = XScreen_create();
    XScreen_setGeometry(b, &(XRect){800, 0, 800, 600});
    XScreen_register(b);
    XScreen_register(b); /* 重复注册 no-op */
    list = XScreen_screens();
    expect_true(list != NULL && XVector_size_base((const XContainer*)list) == 2, "注册表去重后 2 个屏幕");
    if (list) XVector_delete_base((XClass*)list);

    /* 默认兄弟语义：自身 + 注册表其它屏幕 */
    list = XScreen_virtualSiblings(a);
    expect_true(list != NULL && XVector_size_base((const XContainer*)list) == 2, "a 的兄弟为 a+b");
    if (list) XVector_delete_base((XClass*)list);

    /* 虚拟几何 = 兄弟几何并集：(10,0,1590,610) */
    vg = XScreen_virtualGeometry(a);
    expect_true(vg.x == 10 && vg.y == 0 && vg.width == 1590 && vg.height == 610,
                "virtualGeometry 并集");
    s = XScreen_virtualSize(a);
    expect_true(s.width == 1590 && s.height == 610, "virtualSize 读取");
    avg = XScreen_availableVirtualGeometry(a);
    expect_true(avg.x == 20 && avg.y == 0 && avg.width == 1580 &&
                avg.height == 600, "availableVirtualGeometry 并集");
    s = XScreen_availableVirtualSize(a);
    expect_true(s.width == 1580 && s.height == 600, "availableVirtualSize 读取");

    pt.x = 100; pt.y = 50;
    hit = XScreen_virtualSiblingAt(a, pt);
    expect_true(hit == a, "virtualSiblingAt 命中 a");
    pt.x = 900; pt.y = 50;
    hit = XScreen_virtualSiblingAt(a, pt);
    expect_true(hit == b, "virtualSiblingAt 命中 b");
    pt.x = 900; pt.y = 900;
    hit = XScreen_virtualSiblingAt(a, pt);
    expect_true(hit == NULL, "virtualSiblingAt 未命中");

    /* 显式兄弟列表（可排除自身） */
    XScreen_setVirtualSiblings(a, NULL, 0); /* 先清除旧显式列表 */
    explicitSiblings[0] = b;
    XScreen_setVirtualSiblings(a, explicitSiblings, 1);
    vg = XScreen_virtualGeometry(a);
    expect_true(vg.x == 800 && vg.width == 800, "显式兄弟列表决定虚拟几何");
    XScreen_setVirtualSiblings(a, NULL, 0);
    vg = XScreen_virtualGeometry(a);
    expect_true(vg.x == 10 && vg.width == 1590, "清除显式列表恢复默认语义");

    /* 两个屏幕都连接虚拟几何信号：几何变化同时通知双方 */
    memset(&g_screenProbe, 0, sizeof(g_screenProbe));
    XObject_connect_2((XObject*)b, XSignal(XScreen_virtualGeometryChanged_signal),
                      screen_probe_virtualGeometrySlot);
    XScreen_setGeometry(b, &(XRect){0, 0, 640, 480});
    expect_true(g_screenProbe.virtualGeometry == 2,
                "几何变化导致双方 virtualGeometryChanged");
    expect_true(g_screenProbe.lastRect.x == 0 && g_screenProbe.lastRect.width == 810,
                "虚拟几何信号参数为并集");

    /* 退表与主屏幕清理 */
    XScreen_unregister(b);
    XScreen_setPrimary(b); /* 主屏幕切换为 b */
    expect_true(XScreen_primaryScreen() == b, "主屏幕切换");
    XScreen_unregister(b);
    expect_true(XScreen_primaryScreen() == NULL, "退表后主屏幕清空");
    XScreen_setPrimary(a);
    XScreen_unregister(a);
    list = XScreen_screens();
    expect_true(list != NULL && XVector_size_base((const XContainer*)list) == 0, "全部退表后注册表为空");
    if (list) XVector_delete_base((XClass*)list);
    XScreen_delete_base((XClass*)b); /* 释放注册表区域创建的 b */

    /* ---------- 拷贝 / 移动 ---------- */
    copy = XScreen_create_copy(a);
    expect_true(copy != NULL && copy != a, "create_copy 新建对象");
    g = XScreen_geometry(copy);
    expect_true(g.x == 10 && g.width == 800, "copy 复制几何");
    expect_true(strcmp(XScreen_name_2(copy), "HDMI-1") == 0, "copy 复制名称");
    expect_true(XScreen_physicalDotsPerInchX(copy) > 126.0f &&
                XScreen_physicalDotsPerInchX(copy) < 128.0f, "copy 复制物理 DPI");
    XScreen_setName_2(copy, "COPY");
    expect_true(strcmp(XScreen_name_2(a), "HDMI-1") == 0 &&
                strcmp(XScreen_name_2(copy), "COPY") == 0, "copy 字符串深拷贝");
    XScreen_delete_base((XClass*)copy);

    moved = XScreen_create_move(a);
    expect_true(moved != NULL && XScreen_geometry(moved).x == 10,
                "move 转移状态");
    expect_true(XScreen_geometry(a).x == 0, "move 后源对象为空");
    XScreen_delete_base((XClass*)moved);
    XScreen_delete_base((XClass*)a);

    /* ---------- 抓屏：真实后端成功时检查尺寸，否则检查 Qt 空图退化 ---------- */
    b = XScreen_create();
    grab = XScreen_grabWindow(b, 0, 0, 0, 100, 100);
#if XPLATFORMNATIVEWINDOW_ON
    if (XPlatformNativeWindow_isAvailable()) {
        expect_true(grab != NULL && !XPixmap_isNull(grab) &&
                    XPixmap_width(grab) == 100 && XPixmap_height(grab) == 100,
                    "grabWindow X11/Win32 真实抓屏");
    } else
 #endif /* XPLATFORMNATIVEWINDOW_ON */
    {
        expect_true(grab != NULL && XPixmap_isNull(grab),
                    "grabWindow 无平台时返回空像素图");
    }
    if (grab) XPixmap_delete_base((XClass*)grab);
    XScreen_delete_base((XClass*)b);
}

#endif /* XSCREEN_ON */

/* ========================================================================== */
/*               XWindow 契约测试（对标 Qt 6.8 QWindow 全部公开 API）          */
/* ========================================================================== */

#if XWINDOW_ON

#include "XCoreApplication.h"
#include "XWindow.h"

/** @brief XWindow 信号探测数据（19 个信号全量连接计数）。 */
typedef struct WindowProbe
{
    int screen;
    int modality;
    int state;
    int title;
    int x;
    int y;
    int w;
    int h;
    int minW;
    int minH;
    int maxW;
    int maxH;
    int visible;
    int visibility;
    int active;
    int contentOrientation;
    int focusObject;
    int opacity;
    int transientParent;
    int lastX;
    int lastY;
    int lastW;
    int lastH;
    int lastMinW;
    int lastMinH;
    int lastMaxW;
    int lastMaxH;
    XWindowState lastState;
    XWindowVisibility lastVisibility;
    float lastOpacity;
    XScreen* lastScreen;
    XWindow* lastTransientParent;
} WindowProbe;

static WindowProbe g_windowProbe;

static void window_probe_screenSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreen*, screen);
    ++g_windowProbe.screen;
    g_windowProbe.lastScreen = screen;
}

static void window_probe_modalitySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XWindowModality, modality);
    ++g_windowProbe.modality;
    (void)modality;
}

static void window_probe_stateSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XWindowState, state);
    ++g_windowProbe.state;
    g_windowProbe.lastState = state;
}

static void window_probe_titleSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XString*, title);
    ++g_windowProbe.title;
    (void)title;
}

static void window_probe_visibleSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, visible);
    ++g_windowProbe.visible;
    (void)visible;
}

static void window_probe_visibilitySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XWindowVisibility, visibility);
    ++g_windowProbe.visibility;
    g_windowProbe.lastVisibility = visibility;
}

static void window_probe_activeSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_windowProbe.active;
}

static void window_probe_orientationSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreenOrientation, orientation);
    ++g_windowProbe.contentOrientation;
    (void)orientation;
}

static void window_probe_focusObjectSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XObject*, object);
    ++g_windowProbe.focusObject;
    (void)object;
}

static void window_probe_opacitySlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, float, opacity);
    ++g_windowProbe.opacity;
    g_windowProbe.lastOpacity = opacity;
}

static void window_probe_transientParentSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XWindow*, transientParent);
    ++g_windowProbe.transientParent;
    g_windowProbe.lastTransientParent = transientParent;
}

#define XW_PROBE_INT_SLOT(SlotName, Field, LastField) \
    static void window_probe_##SlotName(XObject* sender, XVarList* args) \
    { \
        (void)sender; \
        XVarList_args_1(args, int, value); \
        ++g_windowProbe.Field; \
        g_windowProbe.LastField = value; \
    }

XW_PROBE_INT_SLOT(xSlot, x, lastX)
XW_PROBE_INT_SLOT(ySlot, y, lastY)
XW_PROBE_INT_SLOT(wSlot, w, lastW)
XW_PROBE_INT_SLOT(hSlot, h, lastH)
XW_PROBE_INT_SLOT(minWSlot, minW, lastMinW)
XW_PROBE_INT_SLOT(minHSlot, minH, lastMinH)
XW_PROBE_INT_SLOT(maxWSlot, maxW, lastMaxW)
XW_PROBE_INT_SLOT(maxHSlot, maxH, lastMaxH)

#undef XW_PROBE_INT_SLOT

/* ---------------- 窗口事件子类（重载全部事件槽） ---------------- */

XCLASS_DEFINE_BEGING(TestWin)
XCLASS_DEFINE_EXTEND_END(TestWin, XWindow)

typedef struct TestWin
{
    XWindow m_base;                /**< 基类；必须是第一个成员。 */
    int showCount;
    int hideCount;
    int closeCount;
    int resizeCount;
    int paintCount;
    int moveCount;
    int exposeCount;
    int focusInCount;
    int focusOutCount;
    int keyPressCount;
    int keyReleaseCount;
    int mousePressCount;
    int mouseReleaseCount;
    int mouseDoubleClickCount;
    int mouseMoveCount;
    int wheelCount;
    int touchCount;
    int tabletCount;
    int nativeCount;
    int fallbackCount;
} TestWin;

#define XW_TEST_EVENT_SLOT(Name, Field) \
    static void VTestWin_##Name(XWindow* self, XEvent* event) \
    { \
        (void)event; \
        ++((TestWin*)self)->Field; \
    }

XW_TEST_EVENT_SLOT(showEvent, showCount)
XW_TEST_EVENT_SLOT(hideEvent, hideCount)
XW_TEST_EVENT_SLOT(closeEvent, closeCount)
XW_TEST_EVENT_SLOT(resizeEvent, resizeCount)
XW_TEST_EVENT_SLOT(paintEvent, paintCount)
XW_TEST_EVENT_SLOT(moveEvent, moveCount)
XW_TEST_EVENT_SLOT(exposeEvent, exposeCount)
XW_TEST_EVENT_SLOT(focusInEvent, focusInCount)
XW_TEST_EVENT_SLOT(focusOutEvent, focusOutCount)
XW_TEST_EVENT_SLOT(keyPressEvent, keyPressCount)
XW_TEST_EVENT_SLOT(keyReleaseEvent, keyReleaseCount)
XW_TEST_EVENT_SLOT(mousePressEvent, mousePressCount)
XW_TEST_EVENT_SLOT(mouseReleaseEvent, mouseReleaseCount)
XW_TEST_EVENT_SLOT(mouseDoubleClickEvent, mouseDoubleClickCount)
XW_TEST_EVENT_SLOT(mouseMoveEvent, mouseMoveCount)
XW_TEST_EVENT_SLOT(wheelEvent, wheelCount)
XW_TEST_EVENT_SLOT(touchEvent, touchCount)
XW_TEST_EVENT_SLOT(tabletEvent, tabletCount)

#undef XW_TEST_EVENT_SLOT

static bool VTestWin_nativeEvent(XWindow* self, XEvent* event)
{
    (void)event;
    ++((TestWin*)self)->nativeCount;
    return false;
}

/** @brief 事件总入口：先经父类 XWindow 默认分发器，再统计未识别事件。 */
static bool VTestWin_event(XWindow* self, XEvent* event)
{
    bool handled;
    if (event && event->type == XEVENT_TYPE_TIMER)
        ++((TestWin*)self)->fallbackCount;
    handled = XClass_Parent(XWindow, EXObject_Event,
                            bool(*)(XObject*, XEvent*))((XObject*)self, event);
    return handled;
}

static XVtable* TestWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TestWin)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ExposeEvent, VTestWin_exposeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ResizeEvent, VTestWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_PaintEvent, VTestWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MoveEvent, VTestWin_moveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_FocusInEvent, VTestWin_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_FocusOutEvent, VTestWin_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ShowEvent, VTestWin_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_HideEvent, VTestWin_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_CloseEvent, VTestWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyPressEvent, VTestWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyReleaseEvent, VTestWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MousePressEvent, VTestWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseReleaseEvent, VTestWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseDoubleClickEvent, VTestWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseMoveEvent, VTestWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_WheelEvent, VTestWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_TouchEvent, VTestWin_touchEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_TabletEvent, VTestWin_tabletEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_NativeEvent, VTestWin_nativeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VTestWin_event);
    return XVTABLE_DEFAULT;
}

/** @brief 直接向窗口投递一个基础事件并等待处理完成。 */
static void window_send_event(XObject* receiver, XEventType type)
{
    XEvent* event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type);
    if (!event) return;
    XCoreApplication_sendEvent(receiver, event);
    XEvent_delete_base((XEvent*)event);
}

static TestWin* TestWin_create(void)
{
    TestWin* self = (TestWin*)XMemory_malloc(sizeof(TestWin),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(TestWin));
    XWindow_init(&self->m_base);
    XClassSetVtable(self, TestWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief XWindow 全量契约测试。 */
static void test_window_contract(void)
{
    XWindow* w0;
    XWindow* parent;
    XWindow* child;
    XWindow* desktop;
    XWindow* topLevel;
    XWindow* sub;
    XWindow* wMove;
    XWindow* wClose;
    XWindow* wModes;
    XWindow* copy;
    XWindow* moved;
    TestWin* tw;
    XString* title;
    XString* path;
    XIcon icon;
    XIcon* iconOut;
#if XCURSOR_ON
    XCursor cursor;
    XCursor* cursorOut;
#endif /* XCURSOR_ON */
    XRegion mask;
    XRegion maskOut;
    XRect br;
    XPoint pt;
    XPointF ptf;
    XSize sz;
    XSurfaceFormat fmt;
#if XSCREEN_ON
    XScreen* screen;
#endif /* XSCREEN_ON */
    XWindowPlatform* fake = (XWindowPlatform*)(uintptr_t)0x1234;
    XEvent* event;
#if XACCESSIBLE_ON
    XAccessible* accessible;
#endif

    /* ---------- 默认值 ---------- */
    w0 = XWindow_create();
    expect_true(w0 != NULL, "XWindow_create");
    expect_true(XWindow_geometry(w0).x == 0 && XWindow_geometry(w0).y == 0 &&
                XWindow_geometry(w0).width == 0 && XWindow_geometry(w0).height == 0,
                "XWindow 默认几何 (0,0,0,0)");
    expect_true(!XWindow_isVisible(w0), "XWindow 默认隐藏");
#if XACCESSIBLE_ON
    accessible = (XAccessible*)XWindow_accessibleRoot(w0);
    expect_true(accessible != NULL && XAccessible_isValid(accessible) &&
                XAccessible_role(accessible) == XAccessibleRole_Window &&
                !XAccessible_isVisible(accessible),
                "XWindow 提供稳定的辅助功能根节点");
#endif
    expect_true(XWindow_visibility(w0) == XWindowVisibility_Hidden,
                "XWindow 默认 Visibility=Hidden");
    expect_true(XWindow_flags(w0) == XWindowType_Window, "XWindow 默认 flags=Window");
    expect_true(XWindow_type(w0) == XWindowType_Window, "XWindow 默认类型 Window");
    expect_true(XWindow_surfaceType(w0) == XWindowSurface_Raster,
                "XWindow 默认 surfaceType=Raster");
    expect_true(XWindow_surfaceClass(w0) == XWindowSurfaceClass_Window,
                "XWindow 默认 surfaceClass=Window");
    expect_true(!XWindow_supportsOpenGL(w0), "Raster 不支持 OpenGL");
    expect_true(XWindow_opacity(w0) > 0.999f && XWindow_opacity(w0) < 1.001f,
                "XWindow 默认透明度 1.0");
    expect_true(XWindow_devicePixelRatio(w0) > 0.999f &&
                XWindow_devicePixelRatio(w0) < 1.001f, "XWindow 默认 DPR 1.0");
    expect_true(XWindow_minimumWidth(w0) == 0 && XWindow_minimumHeight(w0) == 0,
                "XWindow 默认最小尺寸 (0,0)");
    expect_true(XWindow_maximumWidth(w0) == 16777215 &&
                XWindow_maximumHeight(w0) == 16777215, "XWindow 默认最大尺寸上限");
    expect_true(XWindow_contentOrientation(w0) == XScreenOrientation_Primary,
                "XWindow 默认内容方向 Primary");
    expect_true(XWindow_modality(w0) == XWindowModality_NonModal &&
                !XWindow_isModal(w0), "XWindow 默认非模态");
    expect_true(XWindow_isTopLevel(w0), "XWindow 默认顶层");
    expect_true(!XWindow_isActive(w0), "XWindow 默认不激活");
    expect_true(!XWindow_isExposed(w0), "XWindow 默认未暴露");
    expect_true(XWindow_handle(w0) == NULL, "XWindow 默认句柄 NULL");
    expect_true(XWindow_parent(w0, XWindowAncestor_IncludeTransients) == NULL,
                "XWindow 默认无父窗口");
    expect_true(XWindow_transientParent(w0) == NULL, "XWindow 默认无瞬态父窗口");
    expect_true(XWindow_winId(w0) != 0, "winId 惰性创建平台句柄");
    expect_true(XWindow_fromWinId(XWindow_winId(w0)) == NULL,
                "fromWinId 无平台后端返回 NULL");
    expect_true(XWindow_frameMargins(w0).left == 0 &&
                XWindow_frameMargins(w0).right == 0 &&
                XWindow_frameMargins(w0).top == 0 &&
                XWindow_frameMargins(w0).bottom == 0, "frameMargins 默认 0");
    expect_true(XWindow_frameGeometry(w0).width == 0, "frameGeometry 跟随几何");
    expect_true(XWindow_baseSize(w0).width == 0 && XWindow_sizeIncrement(w0).width == 0,
                "基准尺寸与步进默认 (0,0)");
#if XACCESSIBLE_ON
    expect_true(XWindow_accessibleRoot(w0) == (void*)accessible,
                "accessibleRoot 返回窗口拥有的稳定根节点");
#else
    expect_true(XWindow_accessibleRoot(w0) == NULL,
                "关闭辅助功能后 accessibleRoot 返回 NULL");
#endif
    expect_true(XWindow_focusObject(w0) == (XObject*)w0, "focusObject 默认窗口自身");
    expect_true(!XWindow_setKeyboardGrabEnabled(w0, true) &&
                !XWindow_setMouseGrabEnabled(w0, true),
                "无平台后端抓取返回 false");
    title = XWindow_title(w0);
    expect_true(title != NULL && XString_toUtf8_length(title) == 0,
                "标题默认空字符串");
    if (title) XString_delete_base((XClass*)title);
    expect_true(XWindow_icon(w0) == NULL, "图标默认 NULL");
#if XCURSOR_ON
    expect_true(XWindow_cursor(w0) == NULL, "光标默认 NULL");
#endif /* XCURSOR_ON */
    XWindow_requestUpdate(w0);
    XWindow_alert(w0, 500);
    XWindow_alert(w0, 0);

    /* 表面类型与格式 */
    XWindow_setSurfaceType(w0, XWindowSurface_OpenGL);
    expect_true(XWindow_supportsOpenGL(w0), "OpenGL 表面支持 OpenGL");
    XWindow_setSurfaceType(w0, XWindowSurface_RasterGL);
    expect_true(XWindow_supportsOpenGL(w0), "RasterGL 表面支持 OpenGL");
    XWindow_setSurfaceType(w0, XWindowSurface_Vulkan);
    expect_true(XWindow_surfaceType(w0) == XWindowSurface_Vulkan,
                "surfaceType 设置 Vulkan");
    XWindow_setSurfaceType(w0, XWindowSurface_Raster);

    fmt = XSurfaceFormat_create();
    XSurfaceFormat_setStereo(&fmt, true);
    expect_true(XSurfaceFormat_stereo(&fmt) &&
                XSurfaceFormat_testOption(&fmt, XSurfaceFormat_StereoBuffers),
                "setStereo 同步 StereoBuffers 选项");
    XSurfaceFormat_setOption(&fmt, XSurfaceFormat_StereoBuffers, false);
    expect_true(!XSurfaceFormat_stereo(&fmt),
                "清除 StereoBuffers 同步 stereo 状态");
    fmt.m_redBufferSize = 8;
    XWindow_setFormat(w0, &fmt);
    expect_true(XWindow_requestedFormat(w0).m_redBufferSize == 8,
                "requestedFormat 保留显式字段");
    expect_true(XWindow_format(w0).m_redBufferSize == 8,
                "format 合并后保留显式字段");

    /* 标志位 */
    XWindow_setFlag(w0, XWindowType_FramelessWindowHint, true);
    expect_true((XWindow_flags(w0) & XWindowType_FramelessWindowHint) != 0,
                "setFlag 置位");
    XWindow_setFlag(w0, XWindowType_FramelessWindowHint, false);
    expect_true((XWindow_flags(w0) & XWindowType_FramelessWindowHint) == 0,
                "setFlag 清位");
    XWindow_setFlags(w0, (XWindowFlags)(XWindowType_Dialog |
                                        XWindowType_WindowStaysOnTopHint));
    expect_true(XWindow_type(w0) == XWindowType_Dialog, "flags 组合确定类型");
    XWindow_setFlags(w0, XWindowType_Window);

    /* 标题 / 文件路径 / 透明度 / 遮罩 / 图标 / 光标 */
    XWindow_setTitle_2(w0, "First");
    title = XWindow_title(w0);
    expect_true(title != NULL && strcmp(XString_toUtf8(title), "First") == 0,
                "setTitle_2 标题");
    if (title) XString_delete_base((XClass*)title);
    XWindow_setTitle(w0, NULL);
    title = XWindow_title(w0);
    expect_true(title != NULL && XString_toUtf8_length(title) == 0,
                "setTitle(NULL) 清空标题");
    if (title) XString_delete_base((XClass*)title);

    XWindow_setFilePath_2(w0, "/tmp/demo.qml");
    path = XWindow_filePath(w0);
    expect_true(path != NULL && strcmp(XString_toUtf8(path), "/tmp/demo.qml") == 0,
                "filePath 设置");
    if (path) XString_delete_base((XClass*)path);
    XWindow_setFilePath(w0, NULL);
    path = XWindow_filePath(w0);
    if (path) XString_delete_base((XClass*)path);

    XWindow_setOpacity(w0, 0.5f);
    expect_true(XWindow_opacity(w0) > 0.499f && XWindow_opacity(w0) < 0.501f,
                "opacity 0.5");
    XWindow_setOpacity(w0, 2.0f);
    expect_true(XWindow_opacity(w0) > 0.999f && XWindow_opacity(w0) < 1.001f,
                "opacity 超上限裁剪为 1.0");
    XWindow_setOpacity(w0, -1.0f);
    expect_true(XWindow_opacity(w0) < 0.001f, "opacity 负值裁剪为 0.0");
    XWindow_setOpacity(w0, 1.0f);

    XRegion_init(&mask);
    XRegion_addRect(&mask, &(XRect){5, 5, 10, 10});
    XWindow_setMask(w0, &mask);
    XRegion_init(&maskOut);
    XWindow_mask(w0, &maskOut);
    expect_true(maskOut.count >= 1, "mask 设置后读出非空");
    XRegion_boundingRect(&maskOut, &br);
    expect_true(br.x == 5 && br.y == 5 && br.width == 10 && br.height == 10,
                "mask 矩形参数一致");
    XRegion_clear(&maskOut);
    XWindow_setMask(w0, NULL);
    XWindow_mask(w0, &maskOut);
    expect_true(maskOut.count == 0 && XRegion_isEmpty(&maskOut),
                "mask 可清空");
    XRegion_deinit(&maskOut);
    XRegion_deinit(&mask);

    XIcon_init(&icon);
    XWindow_setIcon(w0, &icon);
    iconOut = XWindow_icon(w0);
    expect_true(iconOut != NULL && iconOut != &icon, "icon 深拷贝");
    if (iconOut) XIcon_delete_base((XClass*)iconOut);
    XWindow_setIcon(w0, NULL);
    expect_true(XWindow_icon(w0) == NULL, "icon 可清空");
    XIcon_deinit_base(&icon);

#if XCURSOR_ON
    XCursor_init(&cursor);
    XWindow_setCursor(w0, &cursor);
    cursorOut = XWindow_cursor(w0);
    expect_true(cursorOut != NULL && cursorOut != &cursor, "cursor 深拷贝");
    if (cursorOut) XCursor_delete_base((XClass*)cursorOut);
    XWindow_unsetCursor(w0);
    expect_true(XWindow_cursor(w0) == NULL, "cursor 可清空");
    XCursor_deinit_base(&cursor);
#endif /* XCURSOR_ON */

    /* ---------- 可见性：overload showEvent/hideEvent 分发 ---------- */
    tw = TestWin_create();
    expect_true(tw != NULL, "TestWin_create");
    XWindow_setVisible(&tw->m_base, true);
    expect_true(tw->showCount == 1 && XWindow_isVisible(&tw->m_base) &&
                XWindow_visibility(&tw->m_base) == XWindowVisibility_Windowed,
                "setVisible(true) 分发 ShowEvent 并更新可见性");
    expect_true(XWindow_isExposed(&tw->m_base), "显示后 isExposed=true");
    XWindow_setVisible(&tw->m_base, false);
    expect_true(tw->hideCount == 1 && !XWindow_isVisible(&tw->m_base) &&
                XWindow_visibility(&tw->m_base) == XWindowVisibility_Hidden,
                "setVisible(false) 分发 HideEvent");
    expect_true(!XWindow_isExposed(&tw->m_base), "隐藏后 isExposed=false");
    XWindow_setVisible(&tw->m_base, true);
    expect_true(tw->showCount == 2, "再次显示再次分发 ShowEvent");
    XWindow_hide(&tw->m_base);
    expect_true(tw->hideCount == 2, "hide() 分发 HideEvent");
    XWindow_show(&tw->m_base);
    expect_true(tw->showCount == 3 && XWindow_isVisible(&tw->m_base),
                "show() 显示窗口");

    /* ---------- 事件路由：逐类直接投递 ---------- */
    window_send_event((XObject*)tw, XEVENT_TYPE_EXPOSE);
    expect_true(tw->exposeCount == 1, "Expose 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_RESIZE);
    expect_true(tw->resizeCount == 1, "Resize 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_PAINT);
    expect_true(tw->paintCount == 1, "Paint 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_MOVE);
    expect_true(tw->moveCount == 1, "Move 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_FOCUS_IN);
    expect_true(tw->focusInCount == 1, "FocusIn 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_FOCUS_OUT);
    expect_true(tw->focusOutCount == 1, "FocusOut 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_SHOW);
    expect_true(tw->showCount == 4, "Show 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_HIDE);
    expect_true(tw->hideCount == 3, "Hide 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_CLOSE);
    expect_true(tw->closeCount == 1, "Close 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_KEY_PRESS);
    expect_true(tw->keyPressCount == 1, "KeyPress 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_KEY_RELEASE);
    expect_true(tw->keyReleaseCount == 1, "KeyRelease 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_MOUSE_BUTTON_PRESS);
    expect_true(tw->mousePressCount == 1, "MousePress 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_MOUSE_BUTTON_RELEASE);
    expect_true(tw->mouseReleaseCount == 1, "MouseRelease 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK);
    expect_true(tw->mouseDoubleClickCount == 1, "MouseDoubleClick 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_MOUSE_MOVE);
    expect_true(tw->mouseMoveCount == 1, "MouseMove 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_WHEEL);
    expect_true(tw->wheelCount == 1, "Wheel 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_TOUCH_BEGIN);
    expect_true(tw->touchCount == 1, "Touch 事件路由");
    window_send_event((XObject*)tw, XEVENT_TYPE_TABLET_MOVE);
    expect_true(tw->tabletCount == 1, "Tablet 事件路由");

    /* 未识别事件回退父类默认 Event 实现（不崩溃、正常返回） */
    event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_TIMER);
    expect_true(event != NULL, "TIMER 事件创建");
    if (event) {
        XWindow_event_base(&tw->m_base, event);
        expect_true(tw->fallbackCount == 1, "未识别事件经 event_base 分发");
        XEvent_delete_base((XEvent*)event);
    }
    /* nativeEvent 默认返回 false（未处理） */
    event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_EXPOSE);
    expect_true(event != NULL && !XWindow_nativeEvent_base(&tw->m_base, event),
                "nativeEvent 默认未处理");
    if (event) XEvent_delete_base((XEvent*)event);
    XWindow_delete_base((XClass*)tw);

    /* ---------- 父窗口 / 坐标映射 / 祖先 / Desktop 父 ---------- */
    parent = XWindow_create();
    XWindow_setGeometry(parent, 100, 0, 400, 300);
    child = XWindow_create();
    XWindow_setGeometry(child, 1, 1, 50, 50);
    XWindow_setParent(child, parent);
    expect_true(!XWindow_isTopLevel(child) &&
                XWindow_parent(child, XWindowAncestor_ExcludeTransients) == parent,
                "setParent 建立父子关系");
    pt = XWindow_mapToGlobal(child, &(XPoint){0, 0});
    expect_true(pt.x == 101 && pt.y == 1, "mapToGlobal 沿父子链累加偏移");
    pt = XWindow_mapToGlobal(child, &(XPoint){49, 49});
    expect_true(pt.x == 150 && pt.y == 50, "mapToGlobal 子坐标");
    pt = XWindow_mapFromGlobal(child, &(XPoint){101, 1});
    expect_true(pt.x == 0 && pt.y == 0, "mapFromGlobal 逆映射");
    ptf = XWindow_mapToGlobal_f(child, &(XPointF){1.0f, 1.0f});
    expect_true(ptf.x > 101.9f && ptf.x < 102.1f && ptf.y > 1.9f && ptf.y < 2.1f,
                "mapToGlobal_f 浮点映射");
    ptf = XWindow_mapFromGlobal_f(child, &(XPointF){150.0f, 50.0f});
    expect_true(ptf.x > 48.9f && ptf.x < 49.1f && ptf.y > 48.9f && ptf.y < 49.1f,
                "mapFromGlobal_f 逆映射");
    expect_true(XWindow_isAncestorOf(parent, child, XWindowAncestor_ExcludeTransients) &&
                !XWindow_isAncestorOf(child, parent, XWindowAncestor_ExcludeTransients),
                "isAncestorOf 方向判断");
    XWindow_setParent(child, NULL);
    expect_true(XWindow_isTopLevel(child), "解除父窗口后恢复顶层");

    desktop = XWindow_create();
    XWindow_setFlags(desktop, XWindowType_Desktop);
    XWindow_setParent(child, desktop);
    expect_true(XWindow_parent(child, XWindowAncestor_ExcludeTransients) == NULL &&
                XWindow_isTopLevel(child), "Desktop 父窗口视为 NULL");

    topLevel = XWindow_create();
    XWindow_setTransientParent(child, topLevel);
    expect_true(XWindow_transientParent(child) == topLevel,
                "setTransientParent 顶层父窗口");
    sub = XWindow_create();
    XWindow_setParent(sub, parent);
    XWindow_setTransientParent(child, sub); /* 非顶层：拒绝 */
    expect_true(XWindow_transientParent(child) == topLevel,
                "拒绝非顶层瞬态父窗口");
    XWindow_setTransientParent(child, child); /* 自身：拒绝 */
    expect_true(XWindow_transientParent(child) == topLevel,
                "拒绝自身作为瞬态父窗口");
    XWindow_setTransientParent(child, NULL);
    expect_true(XWindow_transientParent(child) == NULL, "瞬态父窗口可清空");

    /* ---------- 开始系统缩放 / 移动 ---------- */
    wMove = XWindow_create();
    XWindow_setMinimumSize(wMove, &(XSize){10, 10});
    XWindow_setMaximumSize(wMove, &(XSize){800, 600});
    expect_true(!XWindow_startSystemResize(wMove, XWindowEdge_Top) &&
                !XWindow_startSystemMove(wMove), "不可见时拒绝系统缩放/移动");
    XWindow_setHandle(wMove, fake);
    XWindow_setVisible(wMove, true);
    expect_true(XWindow_startSystemResize(wMove, XWindowEdge_Top),
                "单边缩放合法");
    expect_true(XWindow_startSystemResize(wMove, XWindowEdge_Left),
                "左边缩放合法");
    expect_true(XWindow_startSystemResize(wMove, XWindowEdge_Top | XWindowEdge_Left),
                "直角邻边（左上角）合法");
    expect_true(XWindow_startSystemResize(wMove, XWindowEdge_Top | XWindowEdge_Right),
                "直角邻边（右上角）合法");
    expect_true(!XWindow_startSystemResize(wMove, XWindowEdge_Top | XWindowEdge_Bottom),
                "对边组合非法");
    expect_true(!XWindow_startSystemResize(wMove, XWindowEdge_Left | XWindowEdge_Right),
                "左右对边非法");
    expect_true(!XWindow_startSystemResize(wMove, 0), "空边缘非法");
    expect_true(!XWindow_startSystemResize(wMove,
                XWindowEdge_Top | XWindowEdge_Left | XWindowEdge_Bottom),
                "三边组合非法");
    expect_true(XWindow_startSystemResize(wMove,
                XWindowEdge_Left | XWindowEdge_Bottom),
                "左下角两个直角邻边合法");
    expect_true(XWindow_startSystemMove(wMove), "可见且带句柄支持系统移动");
    XWindow_setMinimumSize(wMove, &(XSize){800, 600});
    XWindow_setMaximumSize(wMove, &(XSize){800, 600});
    expect_true(!XWindow_startSystemResize(wMove, XWindowEdge_Top),
                "最小==最大时禁止缩放");
    XWindow_setVisible(wMove, false);
    expect_true(!XWindow_startSystemMove(wMove), "隐藏后禁止系统移动");
    XWindow_delete_base((XClass*)wMove);

    /* ---------- 关闭语义 ---------- */
    wClose = XWindow_create();
    expect_true(XWindow_close(wClose), "无平台句柄时 close 不派发事件直接返回 true");
    XWindow_setHandle(wClose, fake);
    XWindow_setVisible(wClose, true);
    expect_true(XWindow_close(wClose), "带句柄 close 返回 true");
    expect_true(!XWindow_isVisible(wClose), "close 后窗口隐藏");
    /* 非顶层窗口直接拒绝关闭 */
    XWindow_setHandle(sub, fake);
    XWindow_setVisible(sub, true);
    expect_true(!XWindow_close(sub), "非顶层窗口拒绝关闭");
    XWindow_setVisible(sub, false);
    XWindow_delete_base((XClass*)wClose);

    /* ---------- 显示模式切换与 setVisibility ---------- */
    wModes = XWindow_create();
    XWindow_showMinimized(wModes);
    expect_true(XWindow_windowState(wModes) == XWindowState_Minimized &&
                XWindow_visibility(wModes) == XWindowVisibility_Minimized,
                "showMinimized");
    XWindow_showMaximized(wModes);
    expect_true(XWindow_windowState(wModes) == XWindowState_Maximized &&
                XWindow_visibility(wModes) == XWindowVisibility_Maximized,
                "showMaximized");
    XWindow_showFullScreen(wModes);
    expect_true(XWindow_windowState(wModes) == XWindowState_FullScreen &&
                XWindow_visibility(wModes) == XWindowVisibility_FullScreen &&
                XWindow_isActive(wModes), "showFullScreen 并激活");
    XWindow_showNormal(wModes);
    expect_true(XWindow_windowState(wModes) == XWindowState_NoState &&
                XWindow_visibility(wModes) == XWindowVisibility_Windowed,
                "showNormal");
    XWindow_setVisibility(wModes, XWindowVisibility_FullScreen);
    expect_true(XWindow_visibility(wModes) == XWindowVisibility_FullScreen &&
                XWindow_windowState(wModes) == XWindowState_FullScreen,
                "setVisibility(FullScreen)");
    XWindow_setVisibility(wModes, XWindowVisibility_Maximized);
    expect_true(XWindow_visibility(wModes) == XWindowVisibility_Maximized &&
                XWindow_windowState(wModes) == XWindowState_Maximized,
                "setVisibility(Maximized)");
    XWindow_setVisibility(wModes, XWindowVisibility_Automatic);
    expect_true(XWindow_visibility(wModes) == XWindowVisibility_Windowed,
                "setVisibility(Automatic)");
    XWindow_setVisibility(wModes, XWindowVisibility_Hidden);
    expect_true(XWindow_visibility(wModes) == XWindowVisibility_Hidden,
                "setVisibility(Hidden)");
    XWindow_delete_base((XClass*)wModes);

    /* ---------- 激活 / 置顶置底 ---------- */
    XWindow_requestActivate(w0);
    expect_true(XWindow_isActive(w0), "requestActivate 激活窗口");
    XWindow_raise(w0);
    expect_true(XWindow_isActive(w0), "raise 置顶并激活");
    XWindow_lower(w0);
    expect_true(!XWindow_isActive(w0), "lower 停止激活");
    XWindow_requestActivate(w0);

    /* ---------- 19 个信号全量连接 + 逐项发射验证 ---------- */
    XObject_connect_2((XObject*)w0, XSignal(XWindow_screenChanged_signal),
                      window_probe_screenSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_modalityChanged_signal),
                      window_probe_modalitySlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_windowStateChanged_signal),
                      window_probe_stateSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_windowTitleChanged_signal),
                      window_probe_titleSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_xChanged_signal),
                      window_probe_xSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_yChanged_signal),
                      window_probe_ySlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_widthChanged_signal),
                      window_probe_wSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_heightChanged_signal),
                      window_probe_hSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_minimumWidthChanged_signal),
                      window_probe_minWSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_minimumHeightChanged_signal),
                      window_probe_minHSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_maximumWidthChanged_signal),
                      window_probe_maxWSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_maximumHeightChanged_signal),
                      window_probe_maxHSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_visibleChanged_signal),
                      window_probe_visibleSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_visibilityChanged_signal),
                      window_probe_visibilitySlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_activeChanged_signal),
                      window_probe_activeSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_contentOrientationChanged_signal),
                      window_probe_orientationSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_focusObjectChanged_signal),
                      window_probe_focusObjectSlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_opacityChanged_signal),
                      window_probe_opacitySlot);
    XObject_connect_2((XObject*)w0, XSignal(XWindow_transientParentChanged_signal),
                      window_probe_transientParentSlot);
    memset(&g_windowProbe, 0, sizeof(g_windowProbe));

    /* 几何逐字段信号 */
    XWindow_setGeometry(w0, 10, 20, 100, 80);
    expect_true(g_windowProbe.x == 1 && g_windowProbe.lastX == 10 &&
                g_windowProbe.y == 1 && g_windowProbe.lastY == 20 &&
                g_windowProbe.w == 1 && g_windowProbe.lastW == 100 &&
                g_windowProbe.h == 1 && g_windowProbe.lastH == 80,
                "setGeometry 逐字段发射 x/y/width/heightChanged");
    XWindow_setGeometry(w0, 10, 20, 100, 80);
    expect_true(g_windowProbe.x == 1 && g_windowProbe.w == 1,
                "setGeometry 同值不重复发射");
    XWindow_setWidth(w0, 200);
    expect_true(g_windowProbe.w == 2 && g_windowProbe.lastW == 200 &&
                XWindow_width(w0) == 200, "setWidth 发射 widthChanged");

    /* 尺寸约束信号与自动 resize 夹紧 */
    XWindow_setMinimumSize(w0, &(XSize){5, 300});
    expect_true(g_windowProbe.minW == 1 && g_windowProbe.minH == 1 &&
                g_windowProbe.lastMinW == 5 && g_windowProbe.lastMinH == 300,
                "setMinimumSize 发射最小尺寸信号");
    expect_true(g_windowProbe.h == 2 && XWindow_height(w0) == 300,
                "最小高度夹紧当前几何");
    XWindow_setMaximumSize(w0, &(XSize){150, 700});
    expect_true(g_windowProbe.maxW == 1 && g_windowProbe.maxH == 1 &&
                g_windowProbe.lastMaxW == 150 && g_windowProbe.lastMaxH == 700,
                "setMaximumSize 发射最大尺寸信号");
    expect_true(g_windowProbe.w == 3 && XWindow_width(w0) == 150,
                "最大宽度夹紧当前几何");
    XWindow_setMaximumWidth(w0, 140);
    expect_true(g_windowProbe.maxW == 2 && XWindow_width(w0) == 140,
                "setMaximumWidth 信号与夹紧");
    XWindow_setMinimumSize(w0, &(XSize){-5, -7});
    expect_true(XWindow_minimumSize(w0).width == 0 &&
                XWindow_minimumSize(w0).height == 0 &&
                g_windowProbe.minW == 2 && g_windowProbe.minH == 2,
                "负值最小尺寸裁剪为 0");
    XWindow_setMinimumSize(w0, &(XSize){500, 600});
    expect_true(XWindow_maximumSize(w0).width == 500 &&
                g_windowProbe.maxW == 3, "max<min 时 max 按 min 提升");
    expect_true(XWindow_width(w0) == 500 && XWindow_height(w0) == 600,
                "最小尺寸再次夹紧几何");
    XWindow_setMinimumSize(w0, &(XSize){0, 0});
    XWindow_setMaximumSize(w0, &(XSize){16777215, 16777215});

    /* 窗口状态：优先级 + Active 位清理 */
    XWindow_setWindowStates(w0, XWindowState_Minimized);
    expect_true(g_windowProbe.state == 1 &&
                g_windowProbe.lastState == XWindowState_Minimized,
                "setWindowStates(Minimized) 发射 windowStateChanged");
    XWindow_setWindowStates(w0, XWindowState_Minimized | XWindowState_FullScreen |
                                XWindowState_Maximized);
    expect_true(XWindow_windowState(w0) == XWindowState_Minimized &&
                g_windowProbe.state == 1,
                "状态优先级 Minimized>FullScreen>Maximized 且不重复发射");
    XWindow_setWindowStates(w0, XWindowState_FullScreen | XWindowState_Maximized);
    expect_true(XWindow_windowState(w0) == XWindowState_FullScreen &&
                g_windowProbe.state == 2 &&
                g_windowProbe.lastState == XWindowState_FullScreen,
                "优先级 FullScreen>Maximized");
    XWindow_setWindowStates(w0, XWindowState_Active);
    expect_true(XWindow_windowStates(w0) == 0 && g_windowProbe.state == 3,
                "Active 位不可写且被清理");
    XWindow_setWindowStates(w0, XWindowState_NoState);
    expect_true(XWindow_windowState(w0) == XWindowState_NoState &&
                g_windowProbe.state == 3, "setWindowStates(NoState)");

    /* 可见性信号 */
    XWindow_setVisible(w0, true);
    expect_true(g_windowProbe.visible == 1 &&
                g_windowProbe.visibility == 1 &&
                g_windowProbe.lastVisibility == XWindowVisibility_Windowed,
                "显示发射 visibleChanged/visibilityChanged");
    XWindow_setWindowStates(w0, XWindowState_FullScreen);
    expect_true(XWindow_visibility(w0) == XWindowVisibility_FullScreen &&
                g_windowProbe.visibility == 2 &&
                g_windowProbe.lastVisibility == XWindowVisibility_FullScreen &&
                g_windowProbe.state == 4,
                "全屏状态同步可见性");
    XWindow_setWindowStates(w0, XWindowState_NoState);
    expect_true(XWindow_visibility(w0) == XWindowVisibility_Windowed &&
                g_windowProbe.visibility == 3, "恢复窗口化可见性");
    XWindow_setVisible(w0, false);
    expect_true(g_windowProbe.visible == 2 &&
                g_windowProbe.visibility == 4 &&
                g_windowProbe.lastVisibility == XWindowVisibility_Hidden,
                "隐藏发射 visibleChanged/visibilityChanged");

    /* 其它通知信号 */
    XWindow_setTitle_2(w0, "Title-X");
    expect_true(g_windowProbe.title == 1, "windowTitleChanged 发射");
    XWindow_setTitle_2(w0, "Title-X");
    expect_true(g_windowProbe.title == 1, "windowTitleChanged 同值不重复");
    XWindow_setModality(w0, XWindowModality_ApplicationModal);
    expect_true(g_windowProbe.modality == 1 && XWindow_isModal(w0),
                "modalityChanged 发射");
    XWindow_setModality(w0, XWindowModality_ApplicationModal);
    expect_true(g_windowProbe.modality == 1, "modalityChanged 同值不重复");
    XWindow_setModality(w0, XWindowModality_NonModal);
    XWindow_setOpacity(w0, 0.5f);
    expect_true(g_windowProbe.opacity == 1 && g_windowProbe.lastOpacity > 0.49f &&
                g_windowProbe.lastOpacity < 0.51f, "opacityChanged 发射");
    XWindow_setOpacity(w0, 0.5f);
    expect_true(g_windowProbe.opacity == 1, "opacityChanged 同值不重复");
    XWindow_reportContentOrientationChange(w0, XScreenOrientation_Landscape);
    expect_true(g_windowProbe.contentOrientation == 1 &&
                XWindow_contentOrientation(w0) == XScreenOrientation_Landscape,
                "contentOrientationChanged 发射");
    XWindow_reportContentOrientationChange(w0, XScreenOrientation_Landscape);
    expect_true(g_windowProbe.contentOrientation == 1,
                "contentOrientationChanged 同值不重复");
    XWindow_focusObjectChanged_signal(w0, (XObject*)w0);
    expect_true(g_windowProbe.focusObject == 1, "focusObjectChanged 发射");
    XWindow_activeChanged_signal(w0);
    expect_true(g_windowProbe.active == 1, "activeChanged 发射");
    XWindow_setTransientParent(w0, topLevel);
    expect_true(g_windowProbe.transientParent == 1 &&
                g_windowProbe.lastTransientParent == topLevel &&
                XWindow_transientParent(w0) == topLevel,
                "transientParentChanged 发射");

    /* 屏幕归属信号（XSCREEN_ON 时） */
#if XSCREEN_ON
    screen = XScreen_create();
    XScreen_setGeometry(screen, &(XRect){0, 0, 800, 600});
    XScreen_setDevicePixelRatio(screen, 2.0f);
    XScreen_register(screen);
    XScreen_setPrimary(screen);
    XWindow_setScreen(w0, screen);
    expect_true(g_windowProbe.screen == 1 && XWindow_screen(w0) == screen,
                "screenChanged 发射并绑定屏幕");
    expect_true(XWindow_devicePixelRatio(w0) > 1.99f &&
                XWindow_devicePixelRatio(w0) < 2.01f,
                "屏幕切换后同步设备像素比");
    XWindow_setScreen(w0, screen);
    expect_true(g_windowProbe.screen == 1, "screenChanged 同屏幕不重复");
#endif /* XSCREEN_ON */

    /* ---------- 拷贝 / 移动 / 生命周期 ---------- */
    copy = XWindow_create_copy(w0);
    expect_true(copy != NULL && copy != w0, "create_copy 新建对象");
    title = XWindow_title(copy);
    expect_true(title != NULL && strcmp(XString_toUtf8(title), "Title-X") == 0 &&
                XWindow_geometry(copy).width == 500 &&
                XWindow_geometry(copy).height == 600,
                "copy 复制几何与标题");
    if (title) XString_delete_base((XClass*)title);
    expect_true(XWindow_parent(copy, XWindowAncestor_ExcludeTransients) == NULL,
                "copy 不复制父窗口");
    XWindow_delete_base((XClass*)copy);
    copy = NULL;

    moved = XWindow_create_move(w0);
    expect_true(moved != NULL && moved != w0, "create_move 新建对象");
    title = XWindow_title(moved);
    expect_true(title != NULL && strcmp(XString_toUtf8(title), "Title-X") == 0 &&
                XWindow_geometry(moved).x == 10, "move 转移状态");
    if (title) XString_delete_base((XClass*)title);
    expect_true(XWindow_geometry(w0).x == 0 && XWindow_winId(moved) != 0,
                "move 后源对象为空且目标保留属性");
    XWindow_delete_base((XClass*)moved);
    XWindow_delete_base((XClass*)w0);

    /* ---------- 栈对象生命周期 ---------- */
    {
        XWindow stackWin;
        XWindow_init(&stackWin);
        XWindow_setTitle_2(&stackWin, "stack");
        XWindow_setGeometry(&stackWin, 7, 8, 90, 60);
        sz = XWindow_size(&stackWin);
        title = XWindow_title(&stackWin);
        expect_true(sz.width == 90 && sz.height == 60 &&
                    title != NULL &&
                    strcmp(XString_toUtf8(title), "stack") == 0,
                    "栈对象 XWindow 可用");
        if (title) XString_delete_base((XClass*)title);
        XWindow_deinit_base(&stackWin);
    }

    /* ---------- 清理 ---------- */
    XWindow_delete_base((XClass*)child);
    XWindow_delete_base((XClass*)desktop);
    XWindow_delete_base((XClass*)topLevel);
    XWindow_delete_base((XClass*)sub);
    XWindow_delete_base((XClass*)parent);
#if XSCREEN_ON
    XScreen_unregister(screen);
    XScreen_delete_base((XClass*)screen);
#endif /* XSCREEN_ON */
    (void)ptf;
}
#endif /* XWINDOW_ON */

/* ========================================================================== */
/*           XGuiApplication 契约测试（对标 Qt 6.8 QGuiApplication）           */
/* ========================================================================== */

#if XGUIAPPLICATION_ON

#include "XGuiApplication.h"
#include "XVarList.h"
#include "XEvent.h"
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
#include "XBackingStore.h"
#include "XPlatformBackingStore.h"
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

/** @brief XGuiApplication 信号探测数据（14 个信号全量连接计数）。 */
typedef struct GuiAppProbe
{
    int fontDatabase;                       /**< fontDatabaseChanged 计数。 */
    int screenAdded;                        /**< screenAdded 计数。 */
    int screenRemoved;                      /**< screenRemoved 计数。 */
    int primaryScreenChanged;               /**< primaryScreenChanged 计数。 */
    int lastWindowClosed;                   /**< lastWindowClosed 计数。 */
    int focusObjectChanged;                 /**< focusObjectChanged 计数。 */
    int focusWindowChanged;                 /**< focusWindowChanged 计数。 */
    int applicationStateChanged;            /**< applicationStateChanged 计数。 */
    int layoutDirectionChanged;             /**< layoutDirectionChanged 计数。 */
    int commitDataRequest;                  /**< commitDataRequest 计数。 */
    int saveStateRequest;                   /**< saveStateRequest 计数。 */
    int applicationDisplayNameChanged;      /**< applicationDisplayNameChanged 计数。 */
    int paletteChanged;                     /**< paletteChanged 计数。 */
    int fontChanged;                        /**< fontChanged 计数。 */
    int styleTabFocus;                      /**< styleHints tabFocusBehaviorChanged 计数。 */
    int clipData;                           /**< clipboard dataChanged 计数。 */
    int clipChanged;                        /**< clipboard changed 计数。 */
#if XPLATFORMINTEGRATION_ON
    int platformPropChanged;                /**< nativeInterface windowPropertyChanged 计数。 */
    XPlatformWindow* lastPropWindow;        /**< 最近一次原生属性窗口参数。 */
    int platformPropNameOk;                 /**< 最近一次原生属性名是否匹配预期。 */
#endif /* XPLATFORMINTEGRATION_ON */
    XScreen* lastScreen;                    /**< 最近一次屏幕参数。 */
    XWindow* lastFocusWindow;               /**< 最近一次焦点窗口参数。 */
    XObject* lastFocusObject;               /**< 最近一次焦点对象参数。 */
    XGuiApplicationState lastState;         /**< 最近一次应用状态。 */
    XGuiLayoutDirection lastDirection;      /**< 最近一次布局方向。 */
    XPalette* lastPalette;                  /**< 最近一次调色板参数。 */
    XFont* lastFont;                        /**< 最近一次字体参数。 */
} GuiAppProbe;

static GuiAppProbe g_guiAppProbe;

#define GUI_APP_NOARG_SLOT(SlotName, Field) \
    static void gui_app_probe_##SlotName(XObject* sender, XVarList* args) \
    { \
        (void)sender; (void)args; \
        ++g_guiAppProbe.Field; \
    }

GUI_APP_NOARG_SLOT(fontDatabaseSlot, fontDatabase)
GUI_APP_NOARG_SLOT(lastWindowClosedSlot, lastWindowClosed)
GUI_APP_NOARG_SLOT(commitDataSlot, commitDataRequest)
GUI_APP_NOARG_SLOT(saveStateSlot, saveStateRequest)
GUI_APP_NOARG_SLOT(displayNameSlot, applicationDisplayNameChanged)

#undef GUI_APP_NOARG_SLOT

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
/** @brief 平台后备存储 present 回调探测数据（flush 触发计数）。 */
static int g_backingStorePresentCount = 0;
static void gui_app_probe_backingStorePresent(
        void* userData, XPlatformBackingStore* store,
        const XRegion* flushedRegion, const XPoint* offset)
{
    (void)userData; (void)store; (void)flushedRegion; (void)offset;
    ++g_backingStorePresentCount;
}
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

static void gui_app_probe_screenSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreen*, screen);
    g_guiAppProbe.lastScreen = screen;
}

static void gui_app_probe_screenAddedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreen*, screen);
    ++g_guiAppProbe.screenAdded;
    g_guiAppProbe.lastScreen = screen;
}

static void gui_app_probe_screenRemovedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreen*, screen);
    ++g_guiAppProbe.screenRemoved;
    g_guiAppProbe.lastScreen = screen;
}

static void gui_app_probe_primaryScreenSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XScreen*, screen);
    ++g_guiAppProbe.primaryScreenChanged;
    g_guiAppProbe.lastScreen = screen;
}

static void gui_app_probe_focusWindowSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XWindow*, window);
    ++g_guiAppProbe.focusWindowChanged;
    g_guiAppProbe.lastFocusWindow = window;
}

static void gui_app_probe_focusObjectSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XObject*, object);
    ++g_guiAppProbe.focusObjectChanged;
    g_guiAppProbe.lastFocusObject = object;
}

static void gui_app_probe_stateSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XGuiApplicationState, state);
    ++g_guiAppProbe.applicationStateChanged;
    g_guiAppProbe.lastState = state;
}

static void gui_app_probe_directionSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XGuiLayoutDirection, direction);
    ++g_guiAppProbe.layoutDirectionChanged;
    g_guiAppProbe.lastDirection = direction;
}

static void gui_app_probe_paletteSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XPalette*, palette);
    ++g_guiAppProbe.paletteChanged;
    g_guiAppProbe.lastPalette = palette;
}

static void gui_app_probe_fontSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XFont*, font);
    ++g_guiAppProbe.fontChanged;
    g_guiAppProbe.lastFont = font;
}

static void gui_app_probe_styleTabSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XStyleHintsTabFocusBehavior, behavior);
    ++g_guiAppProbe.styleTabFocus;
    (void)behavior;
}

static void gui_app_probe_clipDataSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_guiAppProbe.clipData;
}

static void gui_app_probe_clipChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XClipboardMode, mode);
    ++g_guiAppProbe.clipChanged;
    (void)mode;
}

#if XPLATFORMINTEGRATION_ON
static void gui_app_probe_platformPropSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_2(args, XPlatformWindow*, window, XString*, name);
    ++g_guiAppProbe.platformPropChanged;
    g_guiAppProbe.lastPropWindow = window;
    g_guiAppProbe.platformPropNameOk =
        (name != NULL && XString_toUtf8(name) != NULL &&
         strcmp(XString_toUtf8(name), "platform-prop") == 0);
}
#endif /* XPLATFORMINTEGRATION_ON */

#if XINPUTMETHOD_ON && XPLATFORMINPUTCTX_ON
/** @brief 输入法查询回调：覆盖启用状态及三类矩形查询。 */
static XVariant* gui_app_inputMethodQuery(XObject* focusObject,
                                          XInputMethodQuery query,
                                          const XVariant* argument,
                                          void* userData)
{
    XRectF rect;
    (void)focusObject;
    (void)userData;
    if (query == XInputMethodQuery_ImEnabled)
        return XVariant_create_bool(true);
    if (query == XInputMethodQuery_ImCursorPosition) {
        XPointF point = { 0.0f, 0.0f };
        if (argument && XVariant_type((XVariant*)argument) == XVariantType_User &&
            XVariant_dataSize((XVariant*)argument) == sizeof(point) &&
            XVariant_data((XVariant*)argument)) {
            memcpy(&point, XVariant_data((XVariant*)argument), sizeof(point));
        }
        return XVariant_create_int((int)(point.x * 100.0f + point.y));
    }
    if (query == XInputMethodQuery_ImCursorRectangle)
        rect = (XRectF){ 2.0f, 3.0f, 4.0f, 5.0f };
    else if (query == XInputMethodQuery_ImAnchorRectangle)
        rect = (XRectF){ 3.0f, 4.0f, 5.0f, 6.0f };
    else if (query == XInputMethodQuery_ImInputItemClipRectangle)
        rect = (XRectF){ 1.0f, 2.0f, 8.0f, 9.0f };
    else
        return NULL;
    return XVariant_create(&rect, sizeof(rect), XVariantType_User);
}
#endif /* XINPUTMETHOD_ON && XPLATFORMINPUTCTX_ON */

static void test_gui_application_contract(void)
{
    char argv0[] = "xgui_test";
    char* argv[] = { argv0, NULL };
    int argc = 1;
    XGuiApplication* app;
    XFont* font;
    XFont* fontOut;
    XString* s;
    XIcon* iconOut;
    XWindow* w1 = NULL;
    XWindow* w2 = NULL;
    XWindow* w3 = NULL;
    XWindow* fw = NULL;
    XVector* list;
    size_t i;
    float dpr;

    /* ---------------- 生命周期与单例 ---------------- */

    memset(&g_guiAppProbe, 0, sizeof(g_guiAppProbe));
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    expect_true(app != NULL, "XGuiApplication_create_ex");
    expect_true(XGuiApplication_instance() == app, "instance 返回单例");
    expect_true(xGuiApp == app, "xGuiApp 宏");
    expect_true(XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv) == app,
                "重复创建返回已有实例");

    /* 默认值（对标 QGuiApplication 默认构造） */
    expect_true(XGuiApplication_quitOnLastWindowClosed(),
                "默认 quitOnLastWindowClosed=true");
    expect_true(XGuiApplication_desktopSettingsAware(),
                "默认 desktopSettingsAware=true");
    expect_true(XGuiApplication_layoutDirection() == XGuiLayoutDirection_LeftToRight,
                "默认布局方向 LTR");
    expect_true(XGuiApplication_isLeftToRight() && !XGuiApplication_isRightToLeft(),
                "isLeftToRight/isRightToLeft 默认");
    expect_true(XGuiApplication_applicationState() == XGuiApplicationState_Inactive,
                "默认应用状态 Inactive");
    expect_true(XGuiApplication_highDpiScaleFactorRoundingPolicy() ==
                XGuiDpiRoundingPolicy_Unset, "默认 DPI 策略 Unset");
    expect_true(XGuiApplication_badgeNumber() == 0, "默认徽标数 0");
    expect_true(!XGuiApplication_isSessionRestored() &&
                !XGuiApplication_isSavingSession() &&
                XGuiApplication_sessionId() == NULL &&
                XGuiApplication_sessionKey() == NULL,
                "会话状态默认值");
    expect_true(XGuiApplication_font() == NULL, "字体未设置时返回 NULL");
    expect_true(XGuiApplication_focusWindow() == NULL &&
                XGuiApplication_focusObject() == NULL &&
                XGuiApplication_modalWindow() == NULL,
                "焦点/模态窗口默认 NULL");
#if XCURSOR_ON
    expect_true(XGuiApplication_overrideCursor() == NULL, "光标覆盖栈默认空");
#endif /* XCURSOR_ON */
    expect_true(XGuiApplication_inputMethod() != NULL &&
                XGuiApplication_platformNativeInterface() != NULL &&
                XGuiApplication_platformFunction("x") == NULL,
                "平台接口：inputMethod/platformNativeInterface 可用、动态函数恒 NULL");
#if XWINDOW_ON && XACCESSIBLE_ON && XPLATFORMINTEGRATION_ON
    {
        XPlatformNativeInterface* native =
            XGuiApplication_platformNativeInterface();
        XPlatformIntegration* integration = native ?
            XPlatformNativeInterface_integration(native) : NULL;
        XPlatformAccessibility* accessibility = integration ?
            (XPlatformAccessibility*)XPlatformIntegration_accessibility(integration) : NULL;
        XAccessible* root = accessibility ?
            XPlatformAccessibility_root(accessibility) : NULL;
        expect_true(accessibility != NULL && root != NULL &&
                    XAccessible_isValid(root) &&
                    XAccessible_role(root) == XAccessibleRole_Application,
                    "平台无障碍桥接提供应用根对象");
    }
#endif /* XWINDOW_ON && XACCESSIBLE_ON && XPLATFORMINTEGRATION_ON */
    {
        const XString* pn = XGuiApplication_platformName();
        expect_true(pn != NULL && strcmp(XString_toUtf8(pn), "xiniyue-embedded") == 0,
                    "平台名");
    }

    /* ---------------- 信号全量连接 ---------------- */

    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_fontDatabaseChanged_signal),
                      gui_app_probe_fontDatabaseSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_screenAdded_signal),
                      gui_app_probe_screenAddedSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_screenRemoved_signal),
                      gui_app_probe_screenRemovedSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_primaryScreenChanged_signal),
                      gui_app_probe_primaryScreenSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_lastWindowClosed_signal),
                      gui_app_probe_lastWindowClosedSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_focusObjectChanged_signal),
                      gui_app_probe_focusObjectSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_focusWindowChanged_signal),
                      gui_app_probe_focusWindowSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_applicationStateChanged_signal),
                      gui_app_probe_stateSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_layoutDirectionChanged_signal),
                      gui_app_probe_directionSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_commitDataRequest_signal),
                      gui_app_probe_commitDataSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_saveStateRequest_signal),
                      gui_app_probe_saveStateSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_applicationDisplayNameChanged_signal),
                      gui_app_probe_displayNameSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_paletteChanged_signal),
                      gui_app_probe_paletteSlot);
    XObject_connect_2((XObject*)app, XSignal(XGuiApplication_fontChanged_signal),
                      gui_app_probe_fontSlot);

    /* 无参信号手动发射（fontDatabaseChanged/displayName/会话请求） */
    XGuiApplication_fontDatabaseChanged_signal(app);
    expect_true(g_guiAppProbe.fontDatabase == 1, "fontDatabaseChanged 发射");
    XGuiApplication_commitDataRequest_signal(app, NULL);
    XGuiApplication_saveStateRequest_signal(app, NULL);
    expect_true(g_guiAppProbe.commitDataRequest == 1 &&
                g_guiAppProbe.saveStateRequest == 1,
                "会话请求信号发射且参数为 NULL");

    /* ---------------- 应用元信息 ---------------- */

    s = XString_create_utf8("MyGuiApp");
    XGuiApplication_setApplicationDisplayName(s);
    XString_delete_base((XClass*)s);
    expect_true(g_guiAppProbe.applicationDisplayNameChanged == 1,
                "applicationDisplayNameChanged 发射");
    s = XGuiApplication_applicationDisplayName();
    expect_true(s != NULL && strcmp(XString_toUtf8(s), "MyGuiApp") == 0,
                "displayName 设置/读取");
    XGuiApplication_setApplicationDisplayName(NULL);
    expect_true(XGuiApplication_applicationDisplayName() == NULL,
                "displayName 可清空");

    s = XString_create_utf8("app.desktop");
    XGuiApplication_setDesktopFileName(s);
    XString_delete_base((XClass*)s);
    s = XGuiApplication_desktopFileName();
    expect_true(s != NULL && strcmp(XString_toUtf8(s), "app.desktop") == 0,
                "desktopFileName 设置/读取");
    XGuiApplication_setDesktopFileName(NULL);
    expect_true(XGuiApplication_desktopFileName() == NULL,
                "desktopFileName 可清空");

    XGuiApplication_setBadgeNumber(7);
    expect_true(XGuiApplication_badgeNumber() == 7, "badgeNumber 设置/读取");

    /* ---------------- 窗口注册表 ---------------- */

#if XWINDOW_ON
    w1 = XWindow_create();
    w2 = XWindow_create();
    w3 = XWindow_create();
    expect_true(w1 && w2 && w3, "窗口创建");
    XWindow_setParent(w3, w1); /* w3 为子窗口，不算顶层。 */
    XWindow_setGeometry(w1, 0, 0, 100, 100);
    XWindow_setGeometry(w2, 200, 200, 100, 100);

    XGuiApplication_addWindow(w1);
    XGuiApplication_addWindow(w1); /* 幂等。 */
    XGuiApplication_addWindow(w2);
    XGuiApplication_addWindow(w3);

    list = XGuiApplication_allWindows();
    expect_true(list != NULL &&
                XVector_size_base((const XContainer*)list) == 3,
                "allWindows 返回全部登记窗口");
    if (list) XVector_delete_base((XClass*)list);

#if XACCESSIBLE_ON && XPLATFORMINTEGRATION_ON
    {
        XPlatformNativeInterface* native =
            XGuiApplication_platformNativeInterface();
        XPlatformIntegration* integration = native ?
            XPlatformNativeInterface_integration(native) : NULL;
        XPlatformAccessibility* accessibility = integration ?
            (XPlatformAccessibility*)XPlatformIntegration_accessibility(integration) : NULL;
        XAccessible* root = accessibility ?
            XPlatformAccessibility_root(accessibility) : NULL;
        expect_true(root && XAccessible_childCount(root) == 3 &&
                    XAccessible_childAtIndex(root, 0) != NULL &&
                    XAccessible_childAtIndex(root, 2) != NULL,
                    "应用辅助功能根枚举自动登记的窗口对象树");
    }
#endif

    list = XGuiApplication_topLevelWindows();
    expect_true(list != NULL &&
                XVector_size_base((const XContainer*)list) == 2,
                "topLevelWindows 只含顶层窗口");
    if (list) XVector_delete_base((XClass*)list);

    expect_true(XGuiApplication_topLevelAt(&(XPoint){50, 50}) == w1,
                "topLevelAt 命中 w1");
    expect_true(XGuiApplication_topLevelAt(&(XPoint){250, 250}) == w2,
                "topLevelAt 命中 w2");
    expect_true(XGuiApplication_topLevelAt(&(XPoint){150, 150}) == NULL,
                "topLevelAt 空点返回 NULL");

    /* 焦点/模态 */
    XGuiApplication_setFocusWindow(w1, NULL);
    expect_true(XGuiApplication_focusWindow() == w1 &&
                XGuiApplication_focusObject() == (XObject*)w1,
                "setFocusWindow 默认焦点对象为窗口自身");
    expect_true(g_guiAppProbe.focusWindowChanged == 1 &&
                g_guiAppProbe.focusObjectChanged == 1 &&
                g_guiAppProbe.lastFocusWindow == w1 &&
                g_guiAppProbe.lastFocusObject == (XObject*)w1,
                "焦点双信号发射（焦点对象）");
    XGuiApplication_setFocusWindow(w1, NULL);
    expect_true(g_guiAppProbe.focusWindowChanged == 1 &&
                g_guiAppProbe.focusObjectChanged == 1,
                "同窗口重复设置不重复发信号");
    XGuiApplication_setFocusWindow(w2, (XObject*)w1);
    expect_true(g_guiAppProbe.focusWindowChanged == 2 &&
                g_guiAppProbe.focusObjectChanged == 1 &&
                g_guiAppProbe.lastFocusWindow == w2 &&
                g_guiAppProbe.lastFocusObject == (XObject*)w1 &&
                XGuiApplication_focusWindow() == w2 &&
                XGuiApplication_focusObject() == (XObject*)w1,
                "焦点窗口与焦点对象独立更新（显式对象保留）");
    XGuiApplication_setFocusWindow(NULL, NULL);
    expect_true(XGuiApplication_focusWindow() == NULL &&
                XGuiApplication_focusObject() == NULL &&
                g_guiAppProbe.focusWindowChanged == 3 &&
                g_guiAppProbe.focusObjectChanged == 2,
                "setFocusWindow(NULL) 清除焦点");

    XGuiApplication_setModalWindow(w1);
    expect_true(XGuiApplication_modalWindow() == w1, "modalWindow 设置");
    XGuiApplication_setModalWindow(NULL);
    expect_true(XGuiApplication_modalWindow() == NULL, "modalWindow 清除");

    /* 移除非顶层窗口不触发 lastWindowClosed */
    XGuiApplication_removeWindow(w3);
    expect_true(g_guiAppProbe.lastWindowClosed == 0,
                "移除子窗口不触发 lastWindowClosed");

    /* 移除 w1（还有 w2 存活）不触发；移除 w2 触发并请求退出 */
    XGuiApplication_removeWindow(w1);
    expect_true(g_guiAppProbe.lastWindowClosed == 0,
                "还有顶层窗口时不移除触发");
    XGuiApplication_removeWindow(w2);
    expect_true(g_guiAppProbe.lastWindowClosed == 1 &&
                XGuiApplication_quitOnLastWindowClosed(),
                "移除最后顶层窗口触发 lastWindowClosed");

    /* quitOnLastWindowClosed=false：仍发信号但不请求退出 */
    XGuiApplication_setQuitOnLastWindowClosed(false);
    expect_true(!XGuiApplication_quitOnLastWindowClosed(),
                "quitOnLastWindowClosed 可关闭");
    XGuiApplication_addWindow(w1);
    XGuiApplication_addWindow(w2);
    XGuiApplication_removeWindow(w2);
    XGuiApplication_removeWindow(w1);
    expect_true(g_guiAppProbe.lastWindowClosed == 2,
                "quitOnLastWindowClosed=false 仍发 lastWindowClosed");
    XGuiApplication_setQuitOnLastWindowClosed(true);

    /* 窗口登记表已清空 */
    list = XGuiApplication_allWindows();
    expect_true(list != NULL &&
                XVector_size_base((const XContainer*)list) == 0,
                "窗口全部移除后注册表为空");
    if (list) XVector_delete_base((XClass*)list);

    XWindow_delete_base((XClass*)w3);
    XWindow_delete_base((XClass*)w1);
    XWindow_delete_base((XClass*)w2);
    w1 = w2 = w3 = NULL;
#endif /* XWINDOW_ON */

    /* ---------------- 屏幕（转发 XScreen 注册表） ---------------- */

#if XSCREEN_ON
    {
        XScreen* s1 = XScreen_create();
        XScreen* s2 = XScreen_create();
        XPoint pt = {10, 10};

        expect_true(s1 && s2, "屏幕创建");
        XScreen_setGeometry(s1, &(XRect){0, 0, 800, 600});
        XScreen_setGeometry(s2, &(XRect){800, 0, 800, 600});
        XScreen_setDevicePixelRatio(s1, 2.0f);
        XScreen_setDevicePixelRatio(s2, 1.5f);

        XGuiApplication_screenAdded(s1);
        expect_true(g_guiAppProbe.screenAdded == 1 &&
                    g_guiAppProbe.lastScreen == s1,
                    "screenAdded 发射并登记");
        XGuiApplication_screenAdded(s2);
        expect_true(g_guiAppProbe.screenAdded == 2, "screenAdded 再次发射");

        expect_true(XGuiApplication_primaryScreen() == NULL,
                    "未设主屏时 primaryScreen 为 NULL");
        XGuiApplication_setPrimaryScreen(s1);
        expect_true(g_guiAppProbe.primaryScreenChanged == 1 &&
                    XGuiApplication_primaryScreen() == s1,
                    "setPrimaryScreen 发射并生效");
        XGuiApplication_setPrimaryScreen(s1);
        expect_true(g_guiAppProbe.primaryScreenChanged == 1,
                    "同主屏重复设置不重复发信号");

        list = XGuiApplication_screens();
        expect_true(list != NULL &&
                    XVector_size_base((const XContainer*)list) == 2,
                    "screens 返回全部屏幕");
        if (list) XVector_delete_base((XClass*)list);

        expect_true(XGuiApplication_screenAt(&pt) == s1, "screenAt 命中 s1");
        expect_true(XGuiApplication_screenAt(&(XPoint){850, 300}) == s2,
                    "screenAt 命中 s2");
        expect_true(XGuiApplication_screenAt(&(XPoint){2000, 2000}) == NULL,
                    "screenAt 空区域返回 NULL");
        dpr = XGuiApplication_devicePixelRatio();
        expect_true(dpr > 1.99f && dpr < 2.01f, "devicePixelRatio 取主屏 2.0");

        XGuiApplication_setPrimaryScreen(s2);
        expect_true(XGuiApplication_primaryScreen() == s2 &&
                    g_guiAppProbe.primaryScreenChanged == 2,
                    "切换主屏再次发射");
        dpr = XGuiApplication_devicePixelRatio();
        expect_true(dpr > 1.49f && dpr < 1.51f, "devicePixelRatio 跟随主屏");

        XGuiApplication_screenRemoved(s1);
        expect_true(g_guiAppProbe.screenRemoved == 1 &&
                    g_guiAppProbe.lastScreen == s1,
                    "screenRemoved 发射并注销");
        XGuiApplication_screenRemoved(s2);
        expect_true(g_guiAppProbe.screenRemoved == 2 &&
                    XGuiApplication_primaryScreen() == NULL,
                    "主屏注销后 primaryScreen 清空");

        XScreen_delete_base((XClass*)s1);
        XScreen_delete_base((XClass*)s2);
        list = XScreen_screens();
        if (list) {
            expect_true(XVector_size_base((const XContainer*)list) == 0,
                        "屏幕注册表清空");
            XVector_delete_base((XClass*)list);
        }
    }
#endif /* XSCREEN_ON */

    /* ---------------- 光标覆盖栈 ---------------- */

#if XCURSOR_ON
    {
        XCursor* c;
        XCursor* top;
        XCursor wc;

        XCursor_init(&wc);
        XCursor_setShape(&wc, XCursor_IBeam);

        XGuiApplication_setOverrideCursor(NULL); /* 入栈默认箭头。 */
        top = XGuiApplication_overrideCursor();
        expect_true(top != NULL && XCursor_shape(top) == XCursor_Arrow,
                    "setOverrideCursor(NULL) 入栈默认箭头");
        XGuiApplication_setOverrideCursor(&wc);
        top = XGuiApplication_overrideCursor();
        expect_true(top != NULL && XCursor_shape(top) == XCursor_IBeam,
                    "setOverrideCursor 入栈 IBeam");

        c = XCursor_create_shape(XCursor_Wait);
        expect_true(c != NULL, "cursor 创建");
        XGuiApplication_changeOverrideCursor(c);
        XCursor_delete_base((XClass*)c);
        top = XGuiApplication_overrideCursor();
        expect_true(top != NULL && XCursor_shape(top) == XCursor_Wait,
                    "changeOverrideCursor 替换栈顶为 Wait");

        XGuiApplication_restoreOverrideCursor();
        top = XGuiApplication_overrideCursor();
        expect_true(top != NULL && XCursor_shape(top) == XCursor_Arrow,
                    "restore 弹出被替换的覆盖光标，回到更早的箭头");
        XGuiApplication_restoreOverrideCursor();
        expect_true(XGuiApplication_overrideCursor() == NULL,
                    "restore 弹空栈");
        XGuiApplication_restoreOverrideCursor(); /* 空栈 no-op。 */

        XCursor_deinit_base(&wc);
    }
#endif /* XCURSOR_ON */

    /* ---------------- 字体 / 调色板 ---------------- */

    font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "Sans", 13, 400, false);
    expect_true(font != NULL, "font 创建");
    XGuiApplication_setFont(font);
    expect_true(g_guiAppProbe.fontChanged == 1 && g_guiAppProbe.lastFont != NULL,
                "setFont 发射 fontChanged");
    fontOut = XGuiApplication_font();
    {
        XFont* fontCopy = XGuiApplication_font();
        expect_true(fontOut != NULL && fontCopy != NULL &&
                    fontCopy != fontOut &&
                    strcmp(XFont_family(fontOut), "Sans") == 0 &&
                    XFont_pointSize(fontOut) == 13,
                    "font 深拷贝返回（每次独立副本）");
        if (fontCopy) XFont_delete_base(fontCopy);
    }
    if (fontOut) XFont_delete_base(fontOut);
    XGuiApplication_setFont(NULL);
    expect_true(g_guiAppProbe.fontChanged == 2 && g_guiAppProbe.lastFont == NULL &&
                XGuiApplication_font() == NULL,
                "setFont(NULL) 清空并发射");
    XFont_delete_base(font);

#if XPALETTE_ON
    {
        XPalette pal;
        XPalette got;
        XColor c1;
        XColor c2;

        expect_true(g_guiAppProbe.paletteChanged == 0, "palette 初始未发射");
        pal = XGuiApplication_palette();
        c1 = XPalette_color(&pal, XPaletteColorGroup_Active,
                            XPaletteColorRole_Mid);
        c2 = XPalette_color(&pal, XPaletteColorGroup_Active,
                            XPaletteColorRole_PlaceholderText);
        expect_true(XColor_red(&c1) == 184 && XColor_green(&c1) == 184 &&
                    XColor_blue(&c1) == 184,
                    "palette Fusion Mid 与 Qt 计算一致");
        expect_true(XColor_red(&c2) == 0 && XColor_alpha(&c2) == 128,
                    "palette Fusion PlaceholderText 透明度一致");
        c1 = XPalette_color(&pal, XPaletteColorGroup_Disabled,
                            XPaletteColorRole_Highlight);
        expect_true(XColor_red(&c1) == 145 && XColor_green(&c1) == 145 &&
                    XColor_blue(&c1) == 145,
                    "palette Fusion Disabled Highlight 一致");
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window,
                          XColor_create_argb(0xff123456u));
        XGuiApplication_setPalette(&pal);
        expect_true(g_guiAppProbe.paletteChanged == 1 &&
                    g_guiAppProbe.lastPalette != NULL,
                    "setPalette 发射 paletteChanged");
        got = XGuiApplication_palette();
        c1 = XPalette_color(&got, XPaletteColorGroup_Active,
                            XPaletteColorRole_Window);
        c2 = XColor_create_argb(0xff123456u);
        expect_true(c1.m_comp1 == c2.m_comp1 && c1.m_comp2 == c2.m_comp2 &&
                    c1.m_comp3 == c2.m_comp3 && c1.m_alpha == c2.m_alpha,
                    "palette 设置/读取颜色一致");
        XGuiApplication_setPalette(NULL);
        expect_true(g_guiAppProbe.paletteChanged == 2,
                    "setPalette(NULL) 重置默认并发射");
    }
#endif /* XPALETTE_ON */

    /* ---------------- 输入状态 / 布局方向 ---------------- */

    expect_true(XGuiApplication_keyboardModifiers() ==
                XKeyboardModifier_NoModifier &&
                XGuiApplication_queryKeyboardModifiers() ==
                XKeyboardModifier_NoModifier,
                "键盘修饰键默认 0");
    XGuiApplication_setKeyboardModifiers(
        (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                             XKeyboardModifier_ShiftModifier));
    expect_true(XGuiApplication_keyboardModifiers() ==
                (XKeyboardModifiers)(XKeyboardModifier_ControlModifier |
                                     XKeyboardModifier_ShiftModifier) &&
                XGuiApplication_queryKeyboardModifiers() ==
                XGuiApplication_keyboardModifiers(),
                "键盘修饰键设置/查询");

    expect_true(XGuiApplication_mouseButtons() == XMouseButton_NoButton,
                "鼠标键默认 0");
    XGuiApplication_setMouseButtons(XMouseButton_LeftButton);
    expect_true(XGuiApplication_mouseButtons() == XMouseButton_LeftButton,
                "鼠标键设置/读取");
    XGuiApplication_setMouseButtons(XMouseButton_NoButton);

    XGuiApplication_setLayoutDirection(XGuiLayoutDirection_RightToLeft);
    expect_true(g_guiAppProbe.layoutDirectionChanged == 1 &&
                g_guiAppProbe.lastDirection == XGuiLayoutDirection_RightToLeft &&
                XGuiApplication_isRightToLeft() &&
                !XGuiApplication_isLeftToRight(),
                "setLayoutDirection(RTL) 发射且查询正确");
    XGuiApplication_setLayoutDirection(XGuiLayoutDirection_RightToLeft);
    expect_true(g_guiAppProbe.layoutDirectionChanged == 1,
                "同方向不重复发信号");
    XGuiApplication_setLayoutDirection(XGuiLayoutDirection_LeftToRight);
    expect_true(g_guiAppProbe.layoutDirectionChanged == 2 &&
                XGuiApplication_isLeftToRight(),
                "setLayoutDirection(LTR) 再次发射");
    XGuiApplication_setLayoutDirection(XGuiLayoutDirection_Auto);
    expect_true(XGuiApplication_layoutDirection() == XGuiLayoutDirection_Auto &&
                g_guiAppProbe.layoutDirectionChanged == 3,
                "Auto 方向保留");

    /* ---------------- styleHints / clipboard 惰性单例 ---------------- */

#if XSTYLEHINTS_ON
    {
        XStyleHints* sh = XGuiApplication_styleHints();
        expect_true(sh != NULL && XGuiApplication_styleHints() == sh,
                    "styleHints 惰性单例");
        XObject_connect_2((XObject*)sh,
                          XSignal(XStyleHints_tabFocusBehaviorChanged_signal),
                          gui_app_probe_styleTabSlot);
        XStyleHints_setCursorFlashTime(sh, 250);
        expect_true(XStyleHints_cursorFlashTime(sh) == 250,
                    "styleHints cursorFlashTime 可写");
        XStyleHints_setTabFocusBehavior(sh,
            XStyleHintsTabFocusBehavior_TabFocusTextControls);
        expect_true(g_guiAppProbe.styleTabFocus == 1 &&
                    XStyleHints_tabFocusBehavior(sh) ==
                    XStyleHintsTabFocusBehavior_TabFocusTextControls,
                    "styleHints tabFocusBehavior 设置+信号");
        XStyleHints_unsetColorScheme(sh);
        expect_true(XStyleHints_colorScheme(sh) == XStyleHintsColorScheme_Unknown,
                    "styleHints colorScheme 可复位");
        /* 恢复平台层默认值依赖的初始状态。 */
        XStyleHints_setCursorFlashTime(sh, 1000);
        expect_true(XStyleHints_cursorFlashTime(sh) == 1000,
                    "styleHints cursorFlashTime 恢复默认");
    }
#endif /* XSTYLEHINTS_ON */

#if XCLIPBOARD_ON
    {
        XClipboard* cb = XGuiApplication_clipboard();
        XString* txt;
        expect_true(cb != NULL && XGuiApplication_clipboard() == cb,
                    "clipboard 惰性单例");
        XObject_connect_2((XObject*)cb, XSignal(XClipboard_dataChanged_signal),
                          gui_app_probe_clipDataSlot);
        XObject_connect_2((XObject*)cb, XSignal(XClipboard_changed_signal),
                          gui_app_probe_clipChangedSlot);
        s = XString_create_utf8("clip hello");
        XClipboard_setText(cb, s, XClipboardMode_Clipboard);
        XString_delete_base((XClass*)s);
        expect_true(g_guiAppProbe.clipData == 1 && g_guiAppProbe.clipChanged == 1,
                    "clipboard 文本写入双信号");
        txt = XClipboard_text(cb, XClipboardMode_Clipboard);
        expect_true(txt != NULL && strcmp(XString_toUtf8(txt), "clip hello") == 0,
                    "clipboard 文本读写");
        if (txt) XString_delete_base((XClass*)txt);
        XClipboard_clear(cb, XClipboardMode_Clipboard);
        expect_true(g_guiAppProbe.clipData == 2, "clipboard clear 再次发信号");
        txt = XClipboard_text(cb, XClipboardMode_Clipboard);
        expect_true(txt == NULL, "clipboard clear 清空文本");
        if (txt) XString_delete_base((XClass*)txt);
    }
#endif /* XCLIPBOARD_ON */

#if XPLATFORMINTEGRATION_ON
    /* ---------------- 平台集成层：XPlatformIntegration / 原生接口 / 输入上下文 ---------------- */

    {
        XPlatformIntegration* gpi;
        XPlatformNativeInterface* gni;
        XPlatformInputContext* gctx;
        XInputMethod* gim;
        XPlatformWindow* gpw = NULL;
        XWindow* gpwin = NULL;
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
        XPlatformBackingStore* gpbs = NULL;
        XBackingStore* gbks = NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
        XVariant* gv = NULL;
        XString* gs = NULL;
        XVector* gnames = NULL;
        XVector* gfams = NULL;
        void* gtheme = NULL;
        void* gservices = NULL;
        void* gdrag = NULL;
        void* goffscreen = NULL;
        void* gdispatcher = NULL;
        const XVariant* gdef = NULL;
        size_t gcount;
        XPlatformIntegrationCapability gcap;

        gni = XGuiApplication_platformNativeInterface();
        gpi = gni ? XPlatformNativeInterface_integration(gni) : NULL;
        expect_true(gpi != NULL && gni != NULL,
                    "平台集成层/原生接口可用且内部关联");

        /* 中高收益平台对象：字体、主题、桌面服务、拖放、事件分发器和离屏表面。 */
        expect_true(XPlatformIntegration_fontDatabase(gpi) != NULL,
                    "平台字体数据库对象已接入");
        gfams = XPlatformFontDatabase_families((XPlatformFontDatabase*)
            XPlatformIntegration_fontDatabase(gpi));
        expect_true(gfams != NULL, "字体数据库家族快照可查询");
        if (gfams) {
            size_t fi;
            size_t fn = XVector_size_base((const XContainer*)gfams);
            for (fi = 0; fi < fn; ++fi) {
                XString** family = (XString**)XVector_at_base(gfams, (int64_t)fi);
                if (family && *family) XString_delete_base((XClass*)*family);
            }
            XVector_delete_base((XClass*)gfams);
            gfams = NULL;
        }
        gtheme = XPlatformIntegration_createPlatformTheme(gpi, NULL);
        expect_true(gtheme != NULL && XPlatformTheme_name(
                        (XPlatformTheme*)gtheme) != NULL,
                    "平台主题快照已接入");
        if (gtheme) XPlatformTheme_destroy((XPlatformTheme*)gtheme);
        gservices = XPlatformIntegration_services(gpi);
        expect_true(gservices != NULL, "桌面服务对象已接入");
        gdrag = XPlatformIntegration_drag(gpi);
#if XPLATFORMNATIVEWINDOW_ON
        expect_true((gdrag != NULL) == XPlatformNativeWindow_isAvailable(),
                    "出站拖放对象按原生窗口能力创建");
#else
        expect_true(gdrag == NULL, "无原生窗口时出站拖放安全退化");
#endif
        gdispatcher = XPlatformIntegration_createEventDispatcher(gpi);
        expect_true(gdispatcher != NULL, "平台事件分发器已接入统一线程调度器");
        goffscreen = XPlatformIntegration_createPlatformOffscreenSurface(gpi, NULL);
        if (goffscreen) {
            expect_true(XPlatformOffscreenSurface_isValid(
                            (XPlatformOffscreenSurface*)goffscreen) &&
                        XPlatformOffscreenSurface_width(
                            (XPlatformOffscreenSurface*)goffscreen) >= 1 &&
                        XPlatformOffscreenSurface_height(
                            (XPlatformOffscreenSurface*)goffscreen) >= 1,
                        "离屏 GPU 表面创建并报告尺寸");
            XPlatformOffscreenSurface_destroy((XPlatformOffscreenSurface*)goffscreen);
        } else {
            expect_true(!XPlatformGraphics_isOpenGLAvailable() ||
                        !XPlatformNativeWindow_isAvailable(),
                        "无可用桌面 GPU/显示时离屏表面安全退化");
        }

        /* 能力位：嵌入式默认开启项（10 项） */
        expect_true(XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_ThreadedPixmaps) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_WindowMasks) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_MultipleWindows) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_ApplicationState) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_NonFullScreenWindows) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_WindowManagement) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_WindowActivation) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_SyncState) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_ApplicationIcon) &&
                    XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_PaintEvents),
                    "能力位：嵌入式默认开启项全为 true");
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
        expect_true(XPlatformIntegration_hasCapability(
                        gpi, XPlatformIntegrationCapability_BackingStoreStaticContents),
                    "能力位：后备存储静态内容已随实现开启");
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
        /* 图形能力按当前构建/运行时驱动探测，其余未实现项保持关闭。 */
        for (gcap = XPlatformIntegrationCapability_OpenGL;
             gcap <= XPlatformIntegrationCapability_BackingStoreStaticContents;
             ++gcap) {
            if (gcap != XPlatformIntegrationCapability_ThreadedPixmaps &&
                gcap != XPlatformIntegrationCapability_WindowMasks &&
                gcap != XPlatformIntegrationCapability_MultipleWindows &&
                gcap != XPlatformIntegrationCapability_ApplicationState &&
                gcap != XPlatformIntegrationCapability_NonFullScreenWindows &&
                gcap != XPlatformIntegrationCapability_WindowManagement &&
                gcap != XPlatformIntegrationCapability_WindowActivation &&
                gcap != XPlatformIntegrationCapability_SyncState &&
                gcap != XPlatformIntegrationCapability_ApplicationIcon &&
                gcap != XPlatformIntegrationCapability_PaintEvents &&
                gcap != XPlatformIntegrationCapability_BackingStoreStaticContents) {
                bool expected = false;
                if (gcap == XPlatformIntegrationCapability_OpenGL ||
                    gcap == XPlatformIntegrationCapability_AllGLFunctionsQueryable)
                    expected = XPlatformGraphics_isOpenGLAvailable();
                else if (gcap == XPlatformIntegrationCapability_RhiBasedRendering)
                    expected = XPlatformGraphics_isVulkanAvailable();
                else if (gcap == XPlatformIntegrationCapability_ForeignWindows)
#if XPLATFORMNATIVEWINDOW_ON
                    expected = XPlatformNativeWindow_isAvailable();
#else
                    expected = false;
#endif
                expect_true(XPlatformIntegration_hasCapability(gpi, gcap) == expected,
                            "能力位：图形能力按驱动探测，其余能力保持关闭");
            }
        }

        /* 原生接口资源表 */
        expect_true(XPlatformNativeInterface_nativeResourceForIntegration(
                        gni, "integration") == (void*)gpi &&
                    XPlatformNativeInterface_nativeResourceForIntegration(
                        gni, "integration-handle") == (void*)gpi &&
                    XPlatformNativeInterface_nativeResourceForIntegration(
                        gni, "x") == NULL,
                    "nativeResourceForIntegration 资源查询");
        expect_true(XPlatformNativeInterface_registerPlatformFunction(
                        gni, "xgui-test-function", (void*)(uintptr_t)0x1234),
                    "platformFunction 注册动态函数");
        expect_true(XPlatformNativeInterface_platformFunction(
                        gni, "xgui-test-function") == (void*)(uintptr_t)0x1234 &&
                    XGuiApplication_platformFunction("xgui-test-function") ==
                        (void*)(uintptr_t)0x1234 &&
                    XPlatformNativeInterface_nativeResourceFunctionForWindow(
                        gni, "xgui-test-function") == (void*)(uintptr_t)0x1234,
                    "platformFunction 及 resourceFunction 查询");
        expect_true(XPlatformNativeInterface_registerPlatformFunction(
                        gni, "xgui-test-function", NULL) &&
                    XPlatformNativeInterface_platformFunction(
                        gni, "xgui-test-function") == NULL,
                    "platformFunction 注销动态函数");

        /* 样式提示默认值（新分配 XVariant，用后释放） */
        gv = XPlatformIntegration_styleHint(
            gpi, XPlatformIntegrationStyleHint_CursorFlashTime);
        expect_true(gv != NULL && XVariant_toInt32(gv) == 1000,
                    "styleHint CursorFlashTime 默认 1000");
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL;
        gv = XPlatformIntegration_styleHint(
            gpi, XPlatformIntegrationStyleHint_ShowIsFullScreen);
        expect_true(gv != NULL && !XVariant_toBool(gv),
                    "styleHint ShowIsFullScreen 默认 false");
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL;
        gv = XPlatformIntegration_styleHint(
            gpi, XPlatformIntegrationStyleHint_FlickStartDistance);
        expect_true(gv != NULL && XVariant_toInt32(gv) == 30,
                    "styleHint FlickStartDistance 默认 30");
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL;
        gv = XPlatformIntegration_styleHint(
            gpi, (XPlatformIntegrationStyleHint)9999);
        expect_true(gv == NULL, "styleHint 非法枚举返回 NULL");
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL;

        /* 平台元信息：主题名列表 / 默认窗口状态 / beep */
        gnames = XPlatformIntegration_themeNames(gpi);
        gcount = gnames ? XVector_size_base((const XContainer*)gnames) : 0;
        expect_true(gcount == 1, "themeNames 返回单元素列表");
        if (gnames) XVector_delete_base((XClass*)gnames); gnames = NULL;
        expect_true(XPlatformIntegration_defaultWindowState(gpi, 0) ==
                        XWindowState_NoState,
                    "defaultWindowState 默认普通状态");
        expect_true(!XPlatformIntegration_beep(gpi), "beep 冒烟恒 false");
        /* quit() 会触发应用退出，测试中不调用；sync() 亦跳过以免刷新副作用。 */

        /* 输入上下文：默认状态 + 输入面板显隐 + 动作转发冒烟 */
        gctx = XPlatformIntegration_inputContext(gpi);
        expect_true(gctx != NULL && XPlatformInputContext_isValid(gctx) &&
                    XPlatformInputContext_hasCapability(
                        gctx, XPlatformInputContextCapability_HiddenTextCapability),
                    "输入上下文有效并支持隐藏文本");
        expect_true(XPlatformInputContext_inputDirection(gctx) ==
                        XInputMethodLayoutDirection_LeftToRight &&
                    !XPlatformInputContext_isInputPanelVisible(gctx),
                    "输入上下文默认 LTR/面板隐藏");
        gs = XPlatformInputContext_locale(gctx);
        expect_true(gs != NULL && strcmp(XString_toUtf8(gs), "C") == 0,
                    "输入上下文默认区域 C");
        if (gs) XString_delete_base((XClass*)gs); gs = NULL;

        XPlatformInputContext_showInputPanel(gctx);
        expect_true(XPlatformInputContext_isInputPanelVisible(gctx),
                    "showInputPanel 记录可见性");
        XPlatformInputContext_hideInputPanel(gctx);
        expect_true(!XPlatformInputContext_isInputPanelVisible(gctx),
                    "hideInputPanel 隐藏面板");
        XPlatformInputContext_reset(gctx);
        XPlatformInputContext_commit(gctx);
        XPlatformInputContext_update(gctx, (XInputMethodQueries)0);
        XPlatformInputContext_invokeAction(gctx, XInputMethodAction_Click, 0);
        expect_true(!XPlatformInputContext_filterEvent(gctx, NULL),
                    "filterEvent 空后端恒 false");

        /* XGuiApplication::inputMethod 与输入上下文双向绑定 */
        gim = XGuiApplication_inputMethod();
        expect_true(gim != NULL && XInputMethod_platformContext(gim) == gctx,
                    "inputMethod 惰性创建并绑定输入上下文");
        expect_true(!XPlatformInputContext_inputMethodAccepted(gctx),
                    "无查询回调时 ImEnabled 默认拒绝");
        XInputMethod_setQueryHandler(gim, gui_app_inputMethodQuery, NULL);
        XInputMethod_update(gim, XInputMethodQuery_ImEnabled);
        expect_true(XPlatformInputContext_inputMethodAccepted(gctx),
                    "ImEnabled 查询回调更新接受状态");
        {
            XInputMethodTransform transform;
            XRectF inputRect = { 100.0f, 100.0f, 10.0f, 10.0f };
            XRectF cursorRect;
            XRectF anchorRect;
            XRectF clipRect;
            XRectF staticItemRect;
            XInputMethod_setInputItemRectangle(gim, &inputRect);
            XInputMethodTransform_identity(&transform);
            transform.xm13 = 10.0f;
            transform.xm23 = 20.0f;
            XInputMethod_setInputItemTransform(gim, &transform);
            cursorRect = XInputMethod_cursorRectangle(gim);
            anchorRect = XInputMethod_anchorRectangle(gim);
            clipRect = XInputMethod_inputItemClipRectangle(gim);
            staticItemRect = XPlatformInputContext_inputItemRectangle();
            expect_true(cursorRect.x == 12.0f && cursorRect.y == 23.0f &&
                        cursorRect.width == 4.0f && cursorRect.height == 5.0f,
                        "输入法光标矩形查询并按变换映射");
            expect_true(anchorRect.x == 13.0f && anchorRect.y == 24.0f &&
                        anchorRect.width == 5.0f && anchorRect.height == 6.0f,
                        "输入法锚点矩形查询并按变换映射");
            expect_true(clipRect.x == 11.0f && clipRect.y == 22.0f &&
                        clipRect.width == 8.0f && clipRect.height == 9.0f,
                        "输入法裁剪矩形查询并按变换映射");
            expect_true(staticItemRect.x == 110.0f && staticItemRect.y == 120.0f &&
                        staticItemRect.width == 10.0f && staticItemRect.height == 10.0f,
                        "输入法静态输入项矩形按变换映射");
            {
                XPointF queryPos = { 12.0f, 23.0f };
                XVariant* queryResult = XPlatformInputContext_queryFocusObject(
                    XInputMethodQuery_ImCursorPosition, queryPos);
                expect_true(queryResult != NULL && XVariant_toInt(queryResult) == 203,
                            "输入法位置查询先逆变换到焦点控件坐标");
                if (queryResult) XVariant_delete_base((XClass*)queryResult);
            }
            XInputMethodTransform_identity(&transform);
            XInputMethod_setInputItemTransform(gim, &transform);
        }
        XInputMethod_setQueryHandler(gim, NULL, NULL);
        XInputMethod_update(gim, XInputMethodQuery_ImEnabled);
        expect_true(!XPlatformInputContext_inputMethodAccepted(gctx),
                    "清除查询回调后恢复拒绝状态");
        expect_true(XInputMethod_cursorRectangle(gim).width == 0.0f &&
                    XInputMethod_anchorRectangle(gim).height == 0.0f,
                    "无矩形查询回调时返回零矩形");
        expect_true(!XInputMethod_isVisible(gim), "输入面板默认隐藏");
        XInputMethod_show(gim);
        expect_true(XInputMethod_isVisible(gim) &&
                    XPlatformInputContext_isInputPanelVisible(gctx),
                    "show 联动输入上下文");
        XInputMethod_hide(gim);
        expect_true(!XInputMethod_isVisible(gim) &&
                    !XPlatformInputContext_isInputPanelVisible(gctx),
                    "hide 联动输入上下文");
        expect_true(XInputMethod_inputDirection(gim) ==
                        XInputMethodLayoutDirection_LeftToRight,
                    "输入法默认方向 LTR");
        gs = XInputMethod_locale(gim);
        expect_true(gs != NULL && strcmp(XString_toUtf8(gs), "C") == 0,
                    "输入法默认区域 C");
        if (gs) XString_delete_base((XClass*)gs); gs = NULL;
        /* 区域可设置（先查完默认值再修改，避免依赖顺序） */
        XPlatformInputContext_setLocale(gctx, "zh_CN");
        gs = XPlatformInputContext_locale(gctx);
        expect_true(gs != NULL && strcmp(XString_toUtf8(gs), "zh_CN") == 0,
                    "输入上下文区域可设置");
        if (gs) XString_delete_base((XClass*)gs); gs = NULL;
        XPlatformInputContext_setLocale(gctx, "ar_SA");
        expect_true(XPlatformInputContext_inputDirection(gctx) ==
                        XInputMethodLayoutDirection_RightToLeft,
                    "输入上下文 RTL 区域方向按 Qt 语义切换");
        XPlatformInputContext_setLocale(gctx, "zh_CN");
        expect_true(XPlatformInputContext_inputDirection(gctx) ==
                        XInputMethodLayoutDirection_LeftToRight,
                    "输入上下文 LTR 区域方向按 Qt 语义恢复");

#if XWINDOW_ON
        /* 平台窗口：createPlatformWindow 幂等 + 句柄挂接 + 原生资源 */
        gpwin = XWindow_create();
        expect_true(gpwin != NULL, "平台窗口测试 XWindow 创建");
        gpw = XPlatformIntegration_createPlatformWindow(gpi, gpwin);
        expect_true(gpw != NULL && XPlatformWindow_window(gpw) == gpwin &&
                    XWindow_handle(gpwin) == (XWindowPlatform*)gpw,
                    "createPlatformWindow 创建并挂接句柄");
        expect_true(XPlatformIntegration_createPlatformWindow(gpi, gpwin) == gpw,
                    "createPlatformWindow 幂等返回同一句柄");
        expect_true(XPlatformIntegration_createForeignWindow(gpi, gpwin, 0) == NULL,
                    "createForeignWindow 对无效句柄返回 NULL");
#if XPLATFORMNATIVEWINDOW_ON
        if (XPlatformNativeWindow_isAvailable()) {
            XWindow* donor = XWindow_create();
            XWindow* foreign = XWindow_create();
            XPlatformWindow* donorPlatform = NULL;
            XPlatformWindow* foreignPlatform = NULL;
            XWindowId donorId = 0;
            if (donor && foreign) {
                donorPlatform = XPlatformIntegration_createPlatformWindow(gpi, donor);
                donorId = XWindow_winId(donor);
                foreignPlatform = XPlatformIntegration_createForeignWindow(
                    gpi, foreign, donorId);
                expect_true(donorPlatform != NULL && donorId != 0 &&
                            foreignPlatform != NULL &&
                            XWindow_type(foreign) == XWindowType_ForeignWindow &&
                            XWindow_winId(foreign) == donorId &&
                            XPlatformIntegration_createForeignWindow(
                                gpi, foreign, donorId) == foreignPlatform,
                            "createForeignWindow 挂接真实句柄并保持幂等");
                XWindow_destroy(foreign);
                expect_true(XWindow_fromWinId(donorId) == donor,
                            "解除外部窗口挂接不销毁 donor 原生句柄");
            }
            if (foreign) XWindow_delete_base((XClass*)foreign);
            if (donor) {
                XWindow_destroy(donor);
                XWindow_delete_base((XClass*)donor);
            }
        }
#endif /* XPLATFORMNATIVEWINDOW_ON */
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
        gpbs = XPlatformIntegration_createPlatformBackingStore(gpi, gpwin);
        expect_true(gpbs != NULL, "createPlatformBackingStore 创建平台后端");
#else
        expect_true(XPlatformIntegration_createPlatformBackingStore(gpi, gpwin) == NULL,
                    "createPlatformBackingStore 开关关闭返回 NULL");
#endif
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
        /* ---- 平台后端句柄直接契约（XPlatformBackingStore） ---- */
        expect_true(XPlatformBackingStore_window(gpbs) == gpwin,
                    "平台后端登记绑定窗口");
        expect_true(XPlatformBackingStore_paintDevice(gpbs) == NULL &&
                    XPlatformNativeInterface_nativeResourceForBackingStore(
                        gni, "paintdevice", gpbs) == NULL,
                    "未 resize 前绘制设备为空");
        expect_true(XPlatformNativeInterface_nativeResourceForBackingStore(
                        gni, "x", gpbs) == NULL,
                    "nativeResourceForBackingStore 未知资源 NULL");
        {
            XSize gppSize;
            XImage* gppDev;
            XSize_init(&gppSize, 3, 4);
            XPlatformBackingStore_resize(gpbs, &gppSize);
            gppDev = XPlatformBackingStore_paintDevice(gpbs);
            expect_true(gppDev != NULL &&
                        XImage_width(gppDev) == 3 &&
                        XImage_height(gppDev) == 4 &&
                        XImage_format(gppDev) == XImageFormat_ARGB32_Premultiplied &&
                        XPlatformNativeInterface_nativeResourceForBackingStore(
                            gni, "paintdevice", gpbs) == (void*)gppDev,
                        "平台后端 resize 后绘制设备有效且原生资源可见");
        }
        gbks = XBackingStore_create(gpwin);
        expect_true(gbks != NULL && XBackingStore_window(gbks) == gpwin,
                    "XBackingStore 创建并绑定窗口");
        {
            XSize gsize;
            XImage gbsImg;
            XRegion gbsRegion;
            XRegion gbsStatic;
            XRect gbsRect = { 0, 0, 3, 4 };
            XRect gbsStaticRect = { 0, 0, 2, 1 };
            XImage* gbsDev = NULL;
            XPainter gbsPainter;
            XPoint gbsOff;

            gsize = XBackingStore_size(gbks);
            expect_true(gsize.width == 0 && gsize.height == 0 &&
                        XBackingStore_paintDevice(gbks) == NULL,
                        "未 resize 默认 0×0 且无绘制设备");
            XSize_init(&gsize, 3, 4);
            XBackingStore_resize(gbks, &gsize);
            gsize = XBackingStore_size(gbks);
            gbsDev = XBackingStore_paintDevice(gbks);
            expect_true(gsize.width == 3 && gsize.height == 4 &&
                        gbsDev != NULL &&
                        XImage_width(gbsDev) == 3 &&
                        XImage_height(gbsDev) == 4 &&
                        XImage_format(gbsDev) == XImageFormat_ARGB32_Premultiplied,
                        "resize 后 size/绘制设备格式正确");

            /* beginPaint/endPaint + XPainter 直接绘制到内部缓冲 */
            XRegion_init(&gbsRegion);
            XRegion_addRect(&gbsRegion, &gbsRect);
            XBackingStore_beginPaint(gbks, &gbsRegion);
            XPainter_init(&gbsPainter, NULL);
            expect_true(XPainter_begin_image(&gbsPainter, gbsDev),
                        "backingStore painter 绑定绘制设备");
            {
                XRect gbsFill = { 1, 1, 1, 1 };
                expect_true(XPainter_fillRect(&gbsPainter, &gbsFill, 0xff00ff00u),
                            "backingStore 绘制绿色像素");
            }
            expect_true(XPainter_end(&gbsPainter), "backingStore painter end");
            XBackingStore_endPaint(gbks);
            expect_true(XImage_pixel(gbsDev, 1, 1) == 0xff00ff00u,
                        "绘制结果写入内部缓冲");

            /* toImage：内容深拷贝一致 */
            XImage_init(&gbsImg);
            expect_true(XBackingStore_toImage(gbks, &gbsImg) &&
                        XImage_width(&gbsImg) == 3 &&
                        XImage_height(&gbsImg) == 4 &&
                        XImage_pixel(&gbsImg, 1, 1) == 0xff00ff00u,
                        "toImage 深拷贝内容一致");

            /* scroll：区域整体下移一行，原区域顶部被清空 */
            expect_true(XBackingStore_scroll(gbks, &gbsRegion, 0, 1),
                        "scroll 区域下移一行");
            expect_true(XImage_pixel(gbsDev, 1, 2) == 0xff00ff00u &&
                        XImage_pixel(gbsDev, 1, 0) == 0,
                        "scroll 内容搬移且 vacated 区域清空");

            /* 静态内容：设置/查询/清除 */
            XRegion_init(&gbsStatic);
            XRegion_addRect(&gbsStatic, &gbsStaticRect);
            XBackingStore_setStaticContents(gbks, &gbsStatic);
            expect_true(XBackingStore_hasStaticContents(gbks),
                        "hasStaticContents 已设置");
            XRegion_deinit(&gbsStatic);
            gbsStatic = XBackingStore_staticContents(gbks); /* 深拷贝，用完释放。 */
            expect_true(gbsStatic.count >= 1 &&
                        gbsStatic.rects[0].width == 2 &&
                        gbsStatic.rects[0].height == 1,
                        "staticContents 返回裁剪后区域");
            XRegion_deinit(&gbsStatic);
            XBackingStore_setStaticContents(gbks, NULL);
            expect_true(!XBackingStore_hasStaticContents(gbks),
                        "清除静态内容后 hasStaticContents false");

            /* flush：公开类不崩溃；平台后端经回调证实提交 */
            XPoint_init(&gbsOff, 0, 0);
            XBackingStore_flush(gbks, &gbsRegion, gpwin, &gbsOff);
            g_backingStorePresentCount = 0;
            XPlatformBackingStore_setPresentCallback(
                gpbs, gui_app_probe_backingStorePresent, NULL);
            XPlatformBackingStore_flush(gpbs, gpwin, NULL, &gbsOff);
            expect_true(g_backingStorePresentCount == 1,
                        "平台后端 flush 触发 present 回调");
            XPlatformBackingStore_setPresentCallback(gpbs, NULL, NULL);
            XPlatformBackingStore_flush(gpbs, gpwin, NULL, &gbsOff);
            expect_true(g_backingStorePresentCount == 1,
                        "取消回调后 present 不再触发");

            XRegion_deinit(&gbsRegion);
            XImage_deinit_base(&gbsImg);
        }
        XBackingStore_delete_base((XClass*)gbks);
        gbks = NULL;
        XPlatformBackingStore_delete(gpbs);
        gpbs = NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
        expect_true(XPlatformNativeInterface_nativeResourceForWindow(
                        gni, "window", gpwin) == (void*)gpwin &&
                    XPlatformNativeInterface_nativeResourceForWindow(
                        gni, "window-handle", gpwin) ==
                        (void*)XWindow_handle(gpwin) &&
                    XPlatformNativeInterface_nativeResourceForWindow(
                        gni, "native-window-id", gpwin) ==
                        (void*)(uintptr_t)XWindow_winId(gpwin) &&
                    XPlatformNativeInterface_nativeResourceForWindow(
                        gni, "x", gpwin) == NULL,
                    "nativeResourceForWindow 资源查询");

        /* 窗口原生属性：写入 + 信号 + 读取 + 默认值 + 移除 */
        XObject_connect_2((XObject*)gni,
                          XSignal(XPlatformNativeInterface_windowPropertyChanged_signal),
                          gui_app_probe_platformPropSlot);
        gv = XVariant_create_int(4321);
        XPlatformNativeInterface_setWindowProperty(gni, gpw, "platform-prop", gv);
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL; /* 表内为深拷贝。 */
        expect_true(g_guiAppProbe.platformPropChanged == 1 &&
                    g_guiAppProbe.lastPropWindow == gpw &&
                    g_guiAppProbe.platformPropNameOk,
                    "setWindowProperty 发射 windowPropertyChanged");
        gv = XPlatformNativeInterface_windowProperty(gni, gpw, "platform-prop");
        expect_true(gv != NULL && XVariant_toInt32(gv) == 4321,
                    "windowProperty 读取回写值");
        gv = NULL; /* windowProperty 返回表内借用值，勿释放。 */
        gdef = XVariant_create_int(1234);
        gv = XPlatformNativeInterface_windowProperty_2(gni, gpw, "no-such", gdef);
        expect_true(gv != NULL && XVariant_toInt32(gv) == 1234,
                    "windowProperty_2 缺失时返回默认值副本");
        if (gv) XVariant_delete_base((XClass*)gv); gv = NULL;
        if (gdef) XVariant_delete_base((XClass*)gdef); gdef = NULL;
        expect_true(XPlatformNativeInterface_windowProperty(
                        gni, gpw, "no-such") == NULL,
                    "windowProperty 缺失返回 NULL");
        expect_true(XPlatformWindow_removeProperty(gpw, "platform-prop"),
                    "XPlatformWindow_removeProperty 移除已有属性");
        expect_true(XPlatformNativeInterface_windowProperty(
                        gni, gpw, "platform-prop") == NULL,
                    "移除后属性查询为 NULL");

        XWindow_delete_base((XClass*)gpwin);
        gpwin = NULL; /* 平台窗口句柄由集成层拥有，随应用销毁统一回收。 */
#endif /* XWINDOW_ON */
    }
#endif /* XPLATFORMINTEGRATION_ON */


    /* ---------------- 应用状态 / DPI 策略 / 桌面设置 / 会话 ---------------- */

    XGuiApplication_setApplicationState(XGuiApplicationState_Active);
    expect_true(g_guiAppProbe.applicationStateChanged == 1 &&
                g_guiAppProbe.lastState == XGuiApplicationState_Active &&
                XGuiApplication_applicationState() == XGuiApplicationState_Active,
                "applicationState 设置+信号");
    XGuiApplication_setApplicationState(XGuiApplicationState_Active);
    expect_true(g_guiAppProbe.applicationStateChanged == 1,
                "同状态不重复发信号");
    XGuiApplication_setApplicationState(XGuiApplicationState_Hidden);

    XGuiApplication_setHighDpiScaleFactorRoundingPolicy(
        XGuiDpiRoundingPolicy_PassThrough);
    expect_true(XGuiApplication_highDpiScaleFactorRoundingPolicy() ==
                XGuiDpiRoundingPolicy_PassThrough,
                "DPI 策略设置/读取");

    XGuiApplication_setDesktopSettingsAware(false);
    expect_true(!XGuiApplication_desktopSettingsAware(), "desktopSettingsAware 关闭");
    XGuiApplication_setDesktopSettingsAware(true);

    XGuiApplication_setSessionState(true, false, "sid-1", "skey-1");
    expect_true(XGuiApplication_isSessionRestored() &&
                !XGuiApplication_isSavingSession() &&
                XGuiApplication_sessionId() != NULL &&
                strcmp(XString_toUtf8(XGuiApplication_sessionId()), "sid-1") == 0 &&
                strcmp(XString_toUtf8(XGuiApplication_sessionKey()), "skey-1") == 0,
                "会话状态设置/读取");

    /* ---------------- notify 冒烟（不崩溃、走 XCoreApplication 分发） ---------------- */

    {
        XEvent* ne = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XEVENT_TYPE_TIMER);
        expect_true(ne != NULL, "notify 冒烟事件创建");
        expect_true(XGuiApplication_notify((XObject*)app, ne),
                    "notify 分发事件（receiver=app）");
        expect_true(!XGuiApplication_notify(NULL, NULL),
                    "notify 空参数安全返回 false");
        if (ne) XEvent_delete_base(ne);
    }

    /* ---------------- 清理：单例销毁 ---------------- */

    XGuiApplication_delete_base(app);
    expect_true(XGuiApplication_instance() == NULL,
                "销毁后全局单例清空");
    (void)fw;
    (void)iconOut;
    (void)i;
}
#endif /* XGUIAPPLICATION_ON */


/* ========================================================================== */
/*     窗口事件类 + 窗口系统接口事件投递闭环测试（对标 Qt 6.8 QWindowSystemInterface） */
/* ========================================================================== */

#if XWINDOWEVENT_ON && XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON

#include "XWindowEvent.h"
#include "XWindowSystemInterface.h"

/** @brief 声明探测窗口类的虚函数枚举：继承 XWindow（无新增槽位）。 */
XCLASS_DEFINE_BEGING(EventLoopWin)
XCLASS_DEFINE_EXTEND_END(EventLoopWin, XWindow)

/** @brief 事件闭环探测窗口：重载窗口事件槽并记录全部负载。 */
typedef struct EventLoopWin
{
    XWindow m_base;                 /**< 基类；必须是第一个成员。 */
    int exposeCount;                /**< expose 槽调用计数。 */
    int paintCount;                 /**< paint 槽调用计数。 */
    int resizeCount;                /**< resize 槽调用计数。 */
    int focusInCount;               /**< focusIn 槽调用计数。 */
    int focusOutCount;              /**< focusOut 槽调用计数。 */
    int showCount;                  /**< show 槽调用计数。 */
    int hideCount;                  /**< hide 槽调用计数。 */
    int closeCount;                 /**< close 槽调用计数。 */
    bool acceptClose;               /**< true 时 close 槽接受关闭。 */
    XSize lastResizeSize;           /**< 最近一次 resize 新尺寸。 */
    XSize lastOldSize;              /**< 最近一次 resize 旧尺寸。 */
    XRect lastExposeBBox;           /**< 最近一次 expose 区域外接矩形。 */
    XRect lastPaintRect;            /**< 最近一次 paint 外接矩形。 */
    XFocusReason lastReason;        /**< 最近一次焦点原因。 */
    int keyPressCount;              /**< keyPress 槽调用计数。 */
    int keyReleaseCount;            /**< keyRelease 槽调用计数。 */
    int mousePressCount;            /**< mousePress 槽调用计数。 */
    int mouseReleaseCount;          /**< mouseRelease 槽调用计数。 */
    int mouseDoubleClickCount;      /**< mouseDoubleClick 槽调用计数。 */
    int mouseMoveCount;             /**< mouseMove 槽调用计数。 */
    int wheelCount;                 /**< wheel 槽调用计数。 */
    int enterCount;                 /**< enter 槽调用计数。 */
    int leaveCount;                 /**< leave 槽调用计数。 */
    int inputMethodCount;           /**< 输入法事件调用计数。 */
    int lastKey;                    /**< 最近一次按键码（XKey 枚举或 ASCII）。 */
    XKeyboardModifiers lastModifiers; /**< 最近一次键盘修饰键位掩码。 */
    bool lastAutoRepeat;            /**< 最近一次自动重复标志。 */
    XMouseButton lastButton;        /**< 最近一次鼠标触发按键。 */
    XMouseButton lastButtons;       /**< 最近一次鼠标按下集合。 */
    XPoint lastPosition;            /**< 最近一次局部坐标。 */
    XPoint lastGlobalPosition;      /**< 最近一次屏幕全局坐标。 */
    XPoint lastAngleDelta;          /**< 最近一次滚轮角度增量。 */
    char lastCommit[64];            /**< 最近一次 IME 提交文本。 */
} EventLoopWin;

static void VEventLoopWin_exposeEvent(XWindow* self, XEvent* event)
{
    XExposeEvent* ee = (XExposeEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    XRegion region;
    ++w->exposeCount;
    region = XExposeEvent_region(ee);
    XRegion_boundingRect(&region, &w->lastExposeBBox);
    XRegion_deinit(&region);
}

static void VEventLoopWin_paintEvent(XWindow* self, XEvent* event)
{
    XPaintEvent* pe = (XPaintEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->paintCount;
    w->lastPaintRect = XPaintEvent_rect(pe);
}

static void VEventLoopWin_resizeEvent(XWindow* self, XEvent* event)
{
    XResizeEvent* re = (XResizeEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->resizeCount;
    w->lastResizeSize = XResizeEvent_size(re);
    w->lastOldSize = XResizeEvent_oldSize(re);
}

static void VEventLoopWin_focusInEvent(XWindow* self, XEvent* event)
{
    XFocusEvent* fe = (XFocusEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->focusInCount;
    w->lastReason = XFocusEvent_reason(fe);
}

static void VEventLoopWin_focusOutEvent(XWindow* self, XEvent* event)
{
    XFocusEvent* fe = (XFocusEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->focusOutCount;
    w->lastReason = XFocusEvent_reason(fe);
}

static void VEventLoopWin_showEvent(XWindow* self, XEvent* event)
{
    (void)event;
    ++((EventLoopWin*)self)->showCount;
}

static void VEventLoopWin_hideEvent(XWindow* self, XEvent* event)
{
    (void)event;
    ++((EventLoopWin*)self)->hideCount;
}

static void VEventLoopWin_closeEvent(XWindow* self, XEvent* event)
{
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->closeCount;
    if (w->acceptClose)
        XEvent_accept(event);
    else
        XEvent_ignore(event);
}

static void VEventLoopWin_keyPressEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke = (XKeyEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->keyPressCount;
    w->lastKey = XKeyEvent_key(ke);
    w->lastModifiers = XKeyEvent_modifiers(ke);
    w->lastAutoRepeat = XKeyEvent_autoRepeat(ke);
}

static void VEventLoopWin_keyReleaseEvent(XWindow* self, XEvent* event)
{
    XKeyEvent* ke = (XKeyEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->keyReleaseCount;
    w->lastKey = XKeyEvent_key(ke);
    w->lastModifiers = XKeyEvent_modifiers(ke);
    w->lastAutoRepeat = XKeyEvent_autoRepeat(ke);
}

static void VEventLoopWin_mousePressEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->mousePressCount;
    w->lastButton = XMouseEvent_button(me);
    w->lastButtons = XMouseEvent_buttons(me);
    w->lastModifiers = XMouseEvent_modifiers(me);
    w->lastPosition = XMouseEvent_position(me);
}

static void VEventLoopWin_mouseReleaseEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->mouseReleaseCount;
    w->lastButton = XMouseEvent_button(me);
    w->lastButtons = XMouseEvent_buttons(me);
    w->lastModifiers = XMouseEvent_modifiers(me);
    w->lastPosition = XMouseEvent_position(me);
}

static void VEventLoopWin_mouseDoubleClickEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->mouseDoubleClickCount;
    w->lastButton = XMouseEvent_button(me);
    w->lastButtons = XMouseEvent_buttons(me);
    w->lastModifiers = XMouseEvent_modifiers(me);
    w->lastPosition = XMouseEvent_position(me);
}

static void VEventLoopWin_mouseMoveEvent(XWindow* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->mouseMoveCount;
    w->lastButton = XMouseEvent_button(me);
    w->lastButtons = XMouseEvent_buttons(me);
    w->lastModifiers = XMouseEvent_modifiers(me);
    w->lastPosition = XMouseEvent_position(me);
}

static void VEventLoopWin_wheelEvent(XWindow* self, XEvent* event)
{
    XWheelEvent* we = (XWheelEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->wheelCount;
    w->lastButtons = XWheelEvent_buttons(we);
    w->lastModifiers = XWheelEvent_modifiers(we);
    w->lastPosition = XWheelEvent_position(we);
    w->lastAngleDelta = XWheelEvent_angleDelta(we);
}

static void VEventLoopWin_enterEvent(XWindow* self, XEvent* event)
{
    XEnterEvent* ee = (XEnterEvent*)event;
    EventLoopWin* w = (EventLoopWin*)self;
    ++w->enterCount;
    w->lastPosition = XEnterEvent_position(ee);
    w->lastGlobalPosition = XEnterEvent_globalPosition(ee);
}

static void VEventLoopWin_leaveEvent(XWindow* self, XEvent* event)
{
    (void)event;
    ++((EventLoopWin*)self)->leaveCount;
}

static void VEventLoopWin_inputMethodEvent(XWindow* self, XEvent* event)
{
    EventLoopWin* w = (EventLoopWin*)self;
    XString* commit = XInputMethodEvent_commitString(
        (XInputMethodEvent*)event);
    ++w->inputMethodCount;
    if (commit) {
        strncpy(w->lastCommit, XString_toUtf8(commit),
                sizeof(w->lastCommit) - 1u);
        w->lastCommit[sizeof(w->lastCommit) - 1u] = '\0';
        XString_delete_base((XClass*)commit);
    }
}

static XVtable* EventLoopWin_class_init(void)
{
    XVTABLE_INIT_DEFAULT(EventLoopWin)
    XVTABLE_INHERIT_XCLASS(XWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ExposeEvent, VEventLoopWin_exposeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_PaintEvent, VEventLoopWin_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ResizeEvent, VEventLoopWin_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_FocusInEvent, VEventLoopWin_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_FocusOutEvent, VEventLoopWin_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_ShowEvent, VEventLoopWin_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_HideEvent, VEventLoopWin_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_CloseEvent, VEventLoopWin_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyPressEvent, VEventLoopWin_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_KeyReleaseEvent, VEventLoopWin_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_InputMethodEvent,
                             VEventLoopWin_inputMethodEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MousePressEvent, VEventLoopWin_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseReleaseEvent, VEventLoopWin_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseDoubleClickEvent,
                             VEventLoopWin_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_MouseMoveEvent, VEventLoopWin_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_WheelEvent, VEventLoopWin_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_EnterEvent, VEventLoopWin_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWindow_LeaveEvent, VEventLoopWin_leaveEvent);
    return XVTABLE_DEFAULT;
}

static EventLoopWin* EventLoopWin_create(void)
{
    EventLoopWin* self = (EventLoopWin*)XMemory_malloc(sizeof(EventLoopWin),
                                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(EventLoopWin));
    XWindow_init(&self->m_base);
    XClassSetVtable(self, EventLoopWin);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

static void test_window_event_payloads(void)
{
    XSize size = { 4, 3 };
    XSize oldSize = { 2, 2 };
    XRect rect = { 0, 0, 5, 4 };
    XRect bigRect = { 7, 7, 7, 7 };
    XRegion reg;
    XRegion got;
    XRegion clr;
    XRegion clrReg;
    XRegion changed;
    XResizeEvent* re;
    XExposeEvent* ee;
    XEvent* cloneBase;
    XExposeEvent* cl;
    XPaintEvent* pe;
    XFocusEvent* feIn;
    XFocusEvent* feOut;
    XCloseEvent* ce;
    XShowEvent* se;
    XHideEvent* he;
    XInputMethodEvent* ime;
    XEvent* imeClone;
    XString* imePreedit;
    XString* imeCommit;
    XDropEvent* drop;
    XString* dropMime;
    XString* dropData;

    /* ---- XResizeEvent 负载 ---- */
    re = XResizeEvent_create(XEVENT_TYPE_RESIZE, &size, &oldSize);
    expect_true(re != NULL &&
                XResizeEvent_size(re).width == 4 &&
                XResizeEvent_size(re).height == 3 &&
                XResizeEvent_oldSize(re).width == 2 &&
                XResizeEvent_oldSize(re).height == 2 &&
                XResizeEvent_normalSize(re).width == 4 &&
                XResizeEvent_normalSize(re).height == 3 &&
                XResizeEvent_normalOldSize(re).width == 2,
                "XResizeEvent size/oldSize/normalSize 负载");
    if (re) XEvent_delete_base((XClass*)re);

    /* ---- XExposeEvent 负载与深拷贝 ---- */
    XRegion_init(&reg);
    XRegion_addRect(&reg, &rect);
    ee = XExposeEvent_create(XEVENT_TYPE_EXPOSE, &reg);
    got = ee ? XExposeEvent_region(ee) : reg;
    expect_true(ee != NULL && got.count == 1 &&
                got.rects[0].width == 5 && got.rects[0].height == 4,
                "XExposeEvent region 深拷贝负载");
    XRegion_deinit(&got);
    cloneBase = ee ? XEvent_clone_base((const XEvent*)ee) : NULL;
    expect_true(cloneBase != NULL && ((XExposeEvent*)cloneBase)->m_class.type ==
                XEVENT_TYPE_EXPOSE, "XExposeEvent 克隆类型");
    cl = (XExposeEvent*)cloneBase;
    clr = cl ? XExposeEvent_region(cl) : reg;
    expect_true(clr.count == 1 && clr.rects[0].width == 5,
                "XExposeEvent 克隆区域一致");
    XRegion_deinit(&clr);
    /* 修改源事件内部区域，克隆不被影响（独立所有权）。 */
    if (ee) {
        XRegion_deinit(&ee->m_region);
        XRegion_init(&changed);
        XRegion_addRect(&changed, &bigRect);
        XRegion_copy(&changed, &ee->m_region);
        XRegion_deinit(&changed);
    }
    clrReg = cl ? XExposeEvent_region(cl) : reg;
    expect_true(clrReg.count == 1 && clrReg.rects[0].width == 5,
                "XExposeEvent 克隆不受源改动影响");
    XRegion_deinit(&clrReg);
    XRegion_deinit(&reg);
    if (cloneBase) XEvent_delete_base(cloneBase);
    if (ee) XEvent_delete_base((XClass*)ee);

    /* ---- XPaintEvent rect/region ---- */
    XRegion_init(&reg);
    XRegion_addRect(&reg, &rect);
    pe = XPaintEvent_create(XEVENT_TYPE_PAINT, &reg);
    expect_true(pe != NULL &&
                XPaintEvent_rect(pe).x == 0 && XPaintEvent_rect(pe).y == 0 &&
                XPaintEvent_rect(pe).width == 5 && XPaintEvent_rect(pe).height == 4,
                "XPaintEvent rect 为区域外接矩形");
    got = pe ? XPaintEvent_region(pe) : reg;
    expect_true(got.count == 1 && got.rects[0].width == 5,
                "XPaintEvent region 负载");
    XRegion_deinit(&got);
    XRegion_deinit(&reg);
    if (pe) XEvent_delete_base((XClass*)pe);

    /* ---- XFocusEvent gotFocus/lostFocus/reason/setReason ---- */
    feIn = XFocusEvent_create(XEVENT_TYPE_FOCUS_IN, XFocusReason_Tab);
    expect_true(feIn != NULL && XFocusEvent_gotFocus(feIn) &&
                !XFocusEvent_lostFocus(feIn) &&
                XFocusEvent_reason(feIn) == XFocusReason_Tab,
                "XFocusEvent FocusIn gotFocus/reason");
    XFocusEvent_setReason(feIn, XFocusReason_Shortcut);
    expect_true(feIn && XFocusEvent_reason(feIn) == XFocusReason_Shortcut,
                "XFocusEvent setReason");
    if (feIn) XEvent_delete_base((XClass*)feIn);
    feOut = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_ActiveWindow);
    expect_true(feOut != NULL && XFocusEvent_lostFocus(feOut) &&
                !XFocusEvent_gotFocus(feOut) &&
                XFocusEvent_reason(feOut) == XFocusReason_ActiveWindow,
                "XFocusEvent FocusOut lostFocus/reason");
    if (feOut) XEvent_delete_base((XClass*)feOut);

    /* ---- XCloseEvent 默认接受 + XShowEvent/XHideEvent 冒烟 ---- */
    ce = XCloseEvent_create(XEVENT_TYPE_CLOSE);
    expect_true(ce != NULL && XEvent_isAccepted((const XEvent*)ce),
                "XCloseEvent 默认 accepted（对齐 QEvent）");
    if (ce) {
        XEvent_ignore((XEvent*)ce);
        expect_true(!XEvent_isAccepted((const XEvent*)ce) && ce->m_class.type ==
                    XEVENT_TYPE_CLOSE, "XCloseEvent ignore 后拒绝");
        XEvent_delete_base((XClass*)ce);
    }
    se = XShowEvent_create(XEVENT_TYPE_SHOW);
    expect_true(se != NULL && se->m_class.type == XEVENT_TYPE_SHOW,
                "XShowEvent 冒烟");
    if (se) XEvent_delete_base((XClass*)se);
    he = XHideEvent_create(XEVENT_TYPE_HIDE);
    expect_true(he != NULL && he->m_class.type == XEVENT_TYPE_HIDE,
                "XHideEvent 冒烟");
    if (he) XEvent_delete_base((XClass*)he);

    /* ---- XInputMethodEvent：组合/提交文本及克隆独立性 ---- */
    imePreedit = XString_create_utf8("zhong");
    imeCommit = XString_create_utf8("\xE4\xB8\xAD");
    ime = XInputMethodEvent_create(imePreedit, imeCommit, -2, 2, 3, 1);
    if (imePreedit) XString_delete_base((XClass*)imePreedit);
    if (imeCommit) XString_delete_base((XClass*)imeCommit);
    imePreedit = ime ? XInputMethodEvent_preeditString(ime) : NULL;
    imeCommit = ime ? XInputMethodEvent_commitString(ime) : NULL;
    expect_true(ime != NULL && ime->m_class.type == XEVENT_TYPE_INPUT_METHOD &&
                imePreedit && imeCommit &&
                strcmp(XString_toUtf8(imePreedit), "zhong") == 0 &&
                strcmp(XString_toUtf8(imeCommit), "\xE4\xB8\xAD") == 0 &&
                XInputMethodEvent_replacementStart(ime) == -2 &&
                XInputMethodEvent_replacementLength(ime) == 2 &&
                XInputMethodEvent_cursorPosition(ime) == 3 &&
                XInputMethodEvent_anchorPosition(ime) == 1,
                "XInputMethodEvent 组合/提交/替换范围负载");
    if (imePreedit) XString_delete_base((XClass*)imePreedit);
    if (imeCommit) XString_delete_base((XClass*)imeCommit);
    imeClone = ime ? XEvent_clone_base((XEvent*)ime) : NULL;
    imeCommit = imeClone ?
        XInputMethodEvent_commitString((XInputMethodEvent*)imeClone) : NULL;
    expect_true(imeClone != NULL && imeCommit != NULL &&
                strcmp(XString_toUtf8(imeCommit), "\xE4\xB8\xAD") == 0,
                "XInputMethodEvent 可深克隆");
    if (imeCommit) XString_delete_base((XClass*)imeCommit);
    if (imeClone) XEvent_delete_base((XClass*)imeClone);
    if (ime) XEvent_delete_base((XClass*)ime);

    /* ---- XDropEvent：MIME 负载、坐标与独立字符串所有权 ---- */
    dropMime = XString_create_utf8("text/uri-list");
    dropData = XString_create_utf8("file:///tmp/demo.txt\r\n");
    drop = XDropEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_DROP,
                                &(XPoint){7, 9}, &(XPoint){107, 109},
                                dropMime, dropData);
    if (dropMime) XString_delete_base((XClass*)dropMime);
    if (dropData) XString_delete_base((XClass*)dropData);
    dropMime = drop ? XDropEvent_mimeType(drop) : NULL;
    dropData = drop ? XDropEvent_data(drop) : NULL;
    expect_true(drop != NULL && XDropEvent_position(drop).x == 7 &&
                XDropEvent_globalPosition(drop).y == 109 &&
                dropMime && dropData &&
                strcmp(XString_toUtf8(dropMime), "text/uri-list") == 0 &&
                strstr(XString_toUtf8(dropData), "demo.txt") != NULL,
                "XDropEvent MIME/数据/坐标负载");
    if (dropMime) XString_delete_base((XClass*)dropMime);
    if (dropData) XString_delete_base((XClass*)dropData);
    if (drop) XEvent_delete_base((XClass*)drop);

    /* ---- XKeyEvent / XMouseEvent 克隆负载（Copy 为唯一复制逻辑） ---- */
    {
        XKeyEvent* ke = XKeyEvent_create(
            XEVENT_TYPE_KEY_PRESS, 65,
            XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier);
        XEvent* keClone = ke ? XEvent_clone_base((const XEvent*)ke) : NULL;
        expect_true(keClone != NULL &&
                    keClone->type == XEVENT_TYPE_KEY_PRESS &&
                    XKeyEvent_key((const XKeyEvent*)keClone) == 65 &&
                    XKeyEvent_modifiers((const XKeyEvent*)keClone) ==
                        (XKeyboardModifier_ControlModifier |
                         XKeyboardModifier_ShiftModifier),
                    "XKeyEvent 克隆保留按键码与修饰键");
        if (keClone) XEvent_delete_base(keClone);
        if (ke) XEvent_delete_base((XClass*)ke);

        XMouseEvent* me = XMouseEvent_create(
            XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_RightButton,
            XKeyboardModifier_ControlModifier, (XPoint){ 3, 7 });
        XEvent* meClone = me ? XEvent_clone_base((const XEvent*)me) : NULL;
        expect_true(meClone != NULL &&
                    meClone->type == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
                    XMouseEvent_button((const XMouseEvent*)meClone) ==
                        XMouseButton_RightButton &&
                    XMouseEvent_modifiers((const XMouseEvent*)meClone) ==
                        XKeyboardModifier_ControlModifier &&
                    XMouseEvent_position((const XMouseEvent*)meClone).x == 3 &&
                    XMouseEvent_position((const XMouseEvent*)meClone).y == 7,
                    "XMouseEvent 克隆保留按键/修饰键/坐标");
        if (meClone) XEvent_delete_base(meClone);
        if (me) XEvent_delete_base((XClass*)me);
    }
}

static void test_window_event_loop(void)
{
    char argv0[] = "event_loop_test";
    char* argv[] = { argv0, NULL };
    int argc = 1;
    XGuiApplication* app;
    EventLoopWin* w;
    XRegion reg;
    XRect rect = { 2, 3, 10, 7 };
    XRect geom1 = { 10, 20, 30, 40 };
    XRect geom2 = { 1, 2, 51, 61 };
    XFocusEvent* fo;

    /* 事件闭环依赖 GUI 应用单例（WSI 注入经 XGuiApplication 分发）。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    expect_true(app != NULL, "事件闭环 XGuiApplication 创建");
    w = EventLoopWin_create();
    expect_true(w != NULL, "事件闭环探测窗口创建");
    if (!app || !w) return;

    XRegion_init(&reg);
    XRegion_addRect(&reg, &rect);

    /* ---- expose 注入 → expose 槽（携带区域） ---- */
    expect_true(XWindowSystemInterface_handleExposeEvent((XWindow*)w, &reg),
                "handleExposeEvent 派发成功");
    expect_true(w->exposeCount == 1 &&
                w->lastExposeBBox.x == 2 && w->lastExposeBBox.y == 3 &&
                w->lastExposeBBox.width == 10 && w->lastExposeBBox.height == 7 &&
                XWindow_isExposed((XWindow*)w),
                "expose 注入后槽收到区域且窗口暴露");

    /* ---- paint 注入 → paint 槽（区域外接矩形） ---- */
    expect_true(XWindowSystemInterface_handlePaintEvent((XWindow*)w, &reg),
                "handlePaintEvent 派发成功");
    expect_true(w->paintCount == 1 &&
                w->lastPaintRect.x == 2 && w->lastPaintRect.y == 3 &&
                w->lastPaintRect.width == 10 && w->lastPaintRect.height == 7,
                "paint 注入后槽收到外接矩形");

    /* ---- 几何变化 → 持久化 + resize 事件（oldSize） ---- */
    XWindowSystemInterface_handleGeometryChange((XWindow*)w, &geom1);
    expect_true(w->resizeCount == 1 &&
                w->lastResizeSize.width == 30 && w->lastResizeSize.height == 40 &&
                w->lastOldSize.width == 0 && w->lastOldSize.height == 0,
                "handleGeometryChange 首次 resize 携带新/旧尺寸");
    expect_true(XWindow_geometry((XWindow*)w).x == 10 &&
                XWindow_width((XWindow*)w) == 30 &&
                XWindow_height((XWindow*)w) == 40,
                "handleGeometryChange 持久化新几何");
    XWindowSystemInterface_handleGeometryChange((XWindow*)w, &geom2);
    expect_true(w->resizeCount == 2 &&
                w->lastResizeSize.width == 51 && w->lastResizeSize.height == 61 &&
                w->lastOldSize.width == 30 && w->lastOldSize.height == 40,
                "handleGeometryChange 再次 resize 旧尺寸跟随");

    /* ---- 焦点变化：FocusIn（WSI）/ FocusOut（直接投递） ---- */
    XWindowSystemInterface_handleFocusWindowChanged((XWindow*)w, XFocusReason_Tab);
    expect_true(w->focusInCount == 1 &&
                w->lastReason == XFocusReason_Tab,
                "handleFocusWindowChanged 派发 FocusIn(reason)");
    fo = XFocusEvent_create(XEVENT_TYPE_FOCUS_OUT, XFocusReason_ActiveWindow);
    expect_true(fo != NULL, "FocusOut 事件创建");
    if (fo) {
        expect_true(XGuiApplication_sendSpontaneousEvent((XObject*)w, (XEvent*)fo),
                    "FocusOut 自发投递");
        expect_true(w->focusOutCount == 1 &&
                    w->lastReason == XFocusReason_ActiveWindow,
                    "FocusOut 槽收到原因");
        XEvent_delete_base((XClass*)fo);
    }

    /* ---- 关闭：默认拒绝（ignore）/ 接受（accept） ---- */
    expect_true(!XWindowSystemInterface_handleCloseEvent((XWindow*)w) &&
                w->closeCount == 1,
                "handleCloseEvent 未接受时拒绝关闭");
    w->acceptClose = true;
    expect_true(XWindowSystemInterface_handleCloseEvent((XWindow*)w) &&
                w->closeCount == 2,
                "handleCloseEvent 接受后允许关闭");

    /* ---- 显示/隐藏扩展注入 ---- */
    expect_true(XWindowSystemInterface_handleShowEvent((XWindow*)w) &&
                w->showCount == 1 &&
                XWindowSystemInterface_handleHideEvent((XWindow*)w) &&
                w->hideCount == 1,
                "handleShowEvent/handleHideEvent 派发");

    /* ---- 键盘注入：按下/自动重复/释放（双后端键码翻译统一入口） ---- */
    expect_true(XWindowSystemInterface_handleKeyEvent(
                    (XWindow*)w, XEVENT_TYPE_KEY_PRESS, 'A',
                    XKeyboardModifier_ShiftModifier, false),
                "handleKeyEvent 按下派发");
    expect_true(w->keyPressCount == 1 && w->lastKey == 'A' &&
                w->lastModifiers == XKeyboardModifier_ShiftModifier &&
                !w->lastAutoRepeat,
                "keyPress 槽收到键码/修饰键/非重复");
    expect_true(XWindowSystemInterface_handleKeyEvent(
                    (XWindow*)w, XEVENT_TYPE_KEY_PRESS, 'A',
                    XKeyboardModifier_ShiftModifier, true),
                "handleKeyEvent 自动重复派发");
    expect_true(w->keyPressCount == 2 && w->lastAutoRepeat,
                "keyPress 槽识别自动重复");
    expect_true(XWindowSystemInterface_handleKeyEvent(
                    (XWindow*)w, XEVENT_TYPE_KEY_RELEASE, XKey_Escape,
                    XKeyboardModifier_NoModifier, false),
                "handleKeyEvent 释放派发");
    expect_true(w->keyReleaseCount == 1 && w->lastKey == XKey_Escape &&
                !w->lastAutoRepeat,
                "keyRelease 槽收到释放键码");

    /* ---- 输入法注入：与键盘事件独立，提交文本经专用槽到达窗口 ---- */
    expect_true(XWindowSystemInterface_handleInputMethodEvent(
                    (XWindow*)w, "zhong", "\xE4\xB8\xAD", -1, 1, 2, 2) &&
                w->inputMethodCount == 1 &&
                strcmp(w->lastCommit, "\xE4\xB8\xAD") == 0,
                "handleInputMethodEvent 注入真实文本事件通道");

    /* ---- 鼠标注入：按下/双击/释放/移动（含 buttons 集合与坐标） ---- */
    expect_true(XWindowSystemInterface_handleMouseEvent(
                    (XWindow*)w, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                    XMouseButton_LeftButton,
                    XMouseButton_LeftButton | XMouseButton_RightButton,
                    XKeyboardModifier_ControlModifier, (XPoint){ 9, 11 }),
                "handleMouseEvent 按下派发");
    expect_true(w->mousePressCount == 1 &&
                w->lastButton == XMouseButton_LeftButton &&
                w->lastButtons == (XMouseButton_LeftButton |
                                   XMouseButton_RightButton) &&
                w->lastPosition.x == 9 && w->lastPosition.y == 11,
                "mousePress 槽收到触发键/按下集合/坐标");
    expect_true(XWindowSystemInterface_handleMouseEvent(
                    (XWindow*)w, XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK,
                    XMouseButton_RightButton, XMouseButton_RightButton,
                    XKeyboardModifier_NoModifier, (XPoint){ 5, 6 }),
                "handleMouseEvent 双击派发");
    expect_true(w->mouseDoubleClickCount == 1 &&
                w->lastButton == XMouseButton_RightButton,
                "mouseDoubleClick 槽收到双击");
    expect_true(XWindowSystemInterface_handleMouseEvent(
                    (XWindow*)w, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                    XMouseButton_LeftButton, XMouseButton_NoButton,
                    XKeyboardModifier_NoModifier, (XPoint){ 9, 11 }),
                "handleMouseEvent 释放派发");
    expect_true(w->mouseReleaseCount == 1 &&
                w->lastButton == XMouseButton_LeftButton &&
                w->lastButtons == XMouseButton_NoButton,
                "mouseRelease 槽收到释放");
    expect_true(XWindowSystemInterface_handleMouseEvent(
                    (XWindow*)w, XEVENT_TYPE_MOUSE_MOVE,
                    XMouseButton_NoButton, XMouseButton_LeftButton,
                    XKeyboardModifier_NoModifier, (XPoint){ 12, 13 }),
                "handleMouseEvent 移动派发");
    expect_true(w->mouseMoveCount == 1 &&
                w->lastPosition.x == 12 && w->lastPosition.y == 13,
                "mouseMove 槽收到局部坐标");

    /* ---- 滚轮注入：角度增量（Qt 约定 ±120/格） ---- */
    {
        XPoint angle = { 0, 120 };
        expect_true(XWindowSystemInterface_handleWheelEvent(
                        (XWindow*)w, XMouseButton_NoButton,
                        XKeyboardModifier_NoModifier, (XPoint){ 3, 4 },
                        &angle),
                    "handleWheelEvent 派发");
        expect_true(w->wheelCount == 1 &&
                    w->lastAngleDelta.x == 0 && w->lastAngleDelta.y == 120 &&
                    w->lastPosition.x == 3 && w->lastPosition.y == 4,
                    "wheel 槽收到角度增量与坐标");
    }

    /* ---- 进入/离开注入：局部与全局坐标 + 空负载离开 ---- */
    {
        XPoint global = { 400, 300 };
        XWindowSystemInterface_handleEnterEvent((XWindow*)w,
                                                (XPoint){ 1, 2 }, &global);
        expect_true(w->enterCount == 1 &&
                    w->lastPosition.x == 1 && w->lastPosition.y == 2 &&
                    w->lastGlobalPosition.x == 400 &&
                    w->lastGlobalPosition.y == 300,
                    "enter 槽收到局部/全局坐标");
    }
    XWindowSystemInterface_handleLeaveEvent((XWindow*)w);
    expect_true(w->leaveCount == 1, "leave 槽收到离开事件");

    /* ---- post + flush 队列闭环 ---- */
    {
        XPaintEvent* postPaint = XPaintEvent_create(XEVENT_TYPE_PAINT, &reg);
        expect_true(postPaint != NULL, "post 绘制事件创建");
        if (postPaint) {
            XGuiApplication_postEvent((XObject*)w, (XEvent*)postPaint, 0);
            XWindowSystemInterface_flushWindowSystemEvents(XEventLoop_AllEvents);
            expect_true(w->paintCount == 2,
                        "postEvent + flushWindowSystemEvents 闭环派发");
        }
    }

    XRegion_deinit(&reg);
    XWindow_delete_base((XClass*)w);
    XGuiApplication_delete_base(app);
    expect_true(XGuiApplication_instance() == NULL, "事件闭环测试后单例清空");
}

#endif /* XWINDOWEVENT_ON && XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON */

#if XWIDGET_ON

/* ---------------- XWidget 契约测试子类（重载事件槽并计数） ---------------- */

XCLASS_DEFINE_BEGING(TestWidget)
XCLASS_DEFINE_EXTEND_END(TestWidget, XWidget)

typedef struct TestWidget
{
    XWidget m_base;                 /**< 基类；必须是第一个成员。 */
    int paintCount;                 /**< paintEvent 次数。 */
    int resizeCount;                /**< resizeEvent 次数。 */
    int moveCount;                  /**< moveEvent 次数。 */
    int closeCount;                 /**< closeEvent 次数。 */
    int showCount;                  /**< showEvent 次数。 */
    int hideCount;                  /**< hideEvent 次数。 */
    int focusInCount;               /**< focusInEvent 次数。 */
    int focusOutCount;              /**< focusOutEvent 次数。 */
    bool rejectClose;               /**< closeEvent 拒绝关闭。 */
} TestWidget;

#define XT_WIDGET_SLOT(Name, Field) \
    static void VTestWidget_##Name(XWidget* self, XEvent* event) \
    { \
        (void)event; \
        ++((TestWidget*)self)->Field; \
    }

XT_WIDGET_SLOT(paintEvent, paintCount)
XT_WIDGET_SLOT(resizeEvent, resizeCount)
XT_WIDGET_SLOT(moveEvent, moveCount)
XT_WIDGET_SLOT(showEvent, showCount)
XT_WIDGET_SLOT(hideEvent, hideCount)
XT_WIDGET_SLOT(focusInEvent, focusInCount)
XT_WIDGET_SLOT(focusOutEvent, focusOutCount)

#undef XT_WIDGET_SLOT

/** @brief closeEvent：默认接受；rejectClose 置 1 时忽略（返回 false）。 */
static void VTestWidget_closeEvent(XWidget* self, XEvent* event)
{
    TestWidget* tw = (TestWidget*)self;
    ++tw->closeCount;
    if (tw->rejectClose)
        XEvent_ignore(event);
    else
        XEvent_accept(event);
}

/** @brief TestWidget 虚函数表：仅重载事件槽，其余继承 XWidget 默认实现。 */
static XVtable* TestWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TestWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VTestWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VTestWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MoveEvent, VTestWidget_moveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VTestWidget_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VTestWidget_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VTestWidget_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VTestWidget_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VTestWidget_focusOutEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建 TestWidget；parent 可为 NULL（此时为顶层控件）。 */
static TestWidget* TestWidget_create(XWidget* parent)
{
    TestWidget* self = (TestWidget*)XMemory_malloc(sizeof(TestWidget),
                                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(TestWidget));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, TestWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief XWidget 全量契约测试（对标 Qt 6.8 QWidget 语义）。 */
static void test_widget_contract(void)
{
    TestWidget* geo;
    XString* title;
    const XString* title2;
    XRegion region;
    XRect rect;
    XSize size;
    XWidgetSizePolicy policy;

    /* XWidgetSizePolicy 返回结构体需按值存取。 */

    /* ---------- 默认值与属性 ---------- */
    geo = TestWidget_create(NULL);
    expect_true(geo != NULL, "XWidget_create(NULL) 顶层控件");
    expect_true(XWidget_isWindow(&geo->m_base), "无父控件默认顶层");
    expect_true(XWidget_x(&geo->m_base) == 0 && XWidget_y(&geo->m_base) == 0 &&
                XWidget_width(&geo->m_base) == 640 &&
                XWidget_height(&geo->m_base) == 480,
                "XWidget 顶层默认几何 (0,0,640,480)");
    expect_true(XWidget_isEnabled(&geo->m_base), "XWidget 默认启用");
    expect_true(!XWidget_isVisible(&geo->m_base), "XWidget 默认不可见");
    expect_true(XWidget_isHidden(&geo->m_base), "XWidget 默认隐藏");
    expect_true(XWidget_focusPolicy(&geo->m_base) == XWidgetFocusPolicy_NoFocus,
                "XWidget 默认 NoFocus");
    expect_true(XWidget_minimumSize(&geo->m_base).width == 0 &&
                XWidget_minimumSize(&geo->m_base).height == 0,
                "XWidget 默认最小尺寸 (0,0)");
    expect_true(XWidget_maximumSize(&geo->m_base).width == 16777215 &&
                XWidget_maximumSize(&geo->m_base).height == 16777215,
                "XWidget 默认最大尺寸上限");
    policy = XWidget_sizePolicy(&geo->m_base);
    expect_true(XWidgetSizePolicy_horizontalPolicy(&policy) ==
                    XWidgetSizePolicy_Preferred &&
                XWidgetSizePolicy_verticalPolicy(&policy) ==
                    XWidgetSizePolicy_Preferred,
                "XWidget 默认尺寸策略 Preferred/Preferred");
    expect_true(XWidget_windowTitle(&geo->m_base) == NULL,
                "XWidget 默认无窗口标题");
    {
        TestWidget* childDefault = TestWidget_create(&geo->m_base);
        expect_true(childDefault != NULL, "XWidget 子控件默认几何创建");
        if (childDefault) {
            expect_true(XWidget_x(&childDefault->m_base) == 0 &&
                        XWidget_y(&childDefault->m_base) == 0 &&
                        XWidget_width(&childDefault->m_base) == 100 &&
                        XWidget_height(&childDefault->m_base) == 30,
                        "XWidget 子控件默认几何 (0,0,100,30)");
            XWidget_delete_base((XClass*)childDefault);
        }
    }
    XWidget_setAttribute(&geo->m_base, XWidgetAttribute_StaticContents, true);
    expect_true(XWidget_testAttribute(&geo->m_base,
                                      XWidgetAttribute_StaticContents),
                "setAttribute(true)/testAttribute");
    XWidget_setAttribute(&geo->m_base, XWidgetAttribute_StaticContents, false);
    expect_true(!XWidget_testAttribute(&geo->m_base,
                                       XWidgetAttribute_StaticContents),
                "setAttribute(false)/testAttribute");

    /* ---------- 几何/尺寸约束钳位（对标 QWidget） ---------- */
    XWidget_setGeometry(&geo->m_base, 10, 20, 800, 600);
    rect = XWidget_geometry(&geo->m_base);
    expect_true(rect.x == 10 && rect.y == 20 &&
                rect.width == 800 && rect.height == 600,
                "setGeometry(10,20,800,600)");
    rect = XWidget_rect(&geo->m_base);
    expect_true(rect.x == 0 && rect.y == 0 &&
                rect.width == 800 && rect.height == 600,
                "rect() 客户区 0,0,w,h");
    expect_true(XWidget_pos(&geo->m_base).x == 10 &&
                XWidget_pos(&geo->m_base).y == 20, "pos()=(10,20)");
    expect_true(XWidget_size(&geo->m_base).width == 800 &&
                XWidget_size(&geo->m_base).height == 600, "size()=(800,600)");
    expect_true(geo->moveCount == 1 && geo->resizeCount == 1,
                "setGeometry 派发一次 move/resize 事件");
    XWidget_setMinimumSize(&geo->m_base, 100, 100);
    XWidget_resize(&geo->m_base, 50, 50);
    size = XWidget_size(&geo->m_base);
    expect_true(size.width == 100 && size.height == 100,
                "最小尺寸钳位 50x50 -> 100x100");
    XWidget_setMaximumSize(&geo->m_base, 500, 400);
    XWidget_resize(&geo->m_base, 900, 900);
    size = XWidget_size(&geo->m_base);
    expect_true(size.width == 500 && size.height == 400,
                "最大尺寸钳位 900x900 -> 500x400");
    XWidget_setFixedSize(&geo->m_base, 120, 80);
    expect_true(XWidget_minimumWidth(&geo->m_base) == 120 &&
                XWidget_minimumHeight(&geo->m_base) == 80,
                "setFixedSize 最小尺寸=120x80");
    expect_true(XWidget_maximumWidth(&geo->m_base) == 120 &&
                XWidget_maximumHeight(&geo->m_base) == 80,
                "setFixedSize 最大尺寸=120x80");
    XWidget_resize(&geo->m_base, 9999, 9999);
    size = XWidget_size(&geo->m_base);
    expect_true(size.width == 120 && size.height == 80,
                "固定尺寸后 resize(9999) 不越界");
    XWidget_move(&geo->m_base, 3, 4);
    expect_true(XWidget_x(&geo->m_base) == 3 && XWidget_y(&geo->m_base) == 4,
                "move(3,4) 更新位置");
    size = XWidget_sizeHint(&geo->m_base);
    expect_true(size.width == -1 && size.height == -1,
                "默认 sizeHint (-1,-1) 无效");
    XSize_init(&size, 64, 48);
    XWidget_setSizeHint(&geo->m_base, &size);
    size = XWidget_sizeHint(&geo->m_base);
    expect_true(size.width == 64 && size.height == 48, "setSizeHint 生效");

    /* ---------- 可用性 ---------- */
    XWidget_setEnabled(&geo->m_base, false);
    expect_true(!XWidget_isEnabled(&geo->m_base), "setEnabled(false)");
    XWidget_setEnabled(&geo->m_base, true);
    expect_true(XWidget_isEnabled(&geo->m_base), "setEnabled(true)");
    {
        TestWidget* child = TestWidget_create(&geo->m_base);
        expect_true(child != NULL, "父子启用传播创建子控件");
        if (child) {
            XWidget_setEnabled(&geo->m_base, false);
            expect_true(!XWidget_isEnabled(&child->m_base) &&
                        XWidget_testAttribute(&child->m_base,
                                              XWidgetAttribute_Disabled),
                        "父控件禁用同步子控件 Disabled");
            XWidget_setEnabled(&child->m_base, false);
            XWidget_setEnabled(&geo->m_base, true);
            expect_true(!XWidget_isEnabled(&child->m_base),
                        "父控件恢复时保留子控件显式禁用");
            XWidget_setEnabled(&child->m_base, true);
            expect_true(XWidget_isEnabled(&child->m_base),
                        "子控件显式启用后恢复可用");
        }
        XWidget_setEnabled(&geo->m_base, true);
    }

    /* ---------- 标题/透明度 ---------- */
    title = XString_create_utf8("XWidget 契约测试");
    XWidget_setWindowTitle(&geo->m_base, title);
    title2 = XWidget_windowTitle(&geo->m_base);
    expect_true(title2 != NULL &&
                XString_equals(title2, title, XChar_CaseSensitive),
                "setWindowTitle/windowTitle");
    XWidget_setWindowOpacity(&geo->m_base, 0.5);
    expect_true(XWidget_windowOpacity(&geo->m_base) > 0.499 &&
                XWidget_windowOpacity(&geo->m_base) < 0.501,
                "setWindowOpacity(0.5)");
    XString_delete_base((XClass*)title);
    XWidget_delete_base((XClass*)geo);

    /* ---------- 焦点流转（未显示窗口，不进平台激活路径） ---------- */
    {
        XWidget* fRoot = XWidget_create(NULL, XWindowType_Window);
        TestWidget* fa = TestWidget_create(fRoot);
        TestWidget* fb = TestWidget_create(fRoot);
        expect_true(!XWidget_hasFocus(&fa->m_base), "焦点初始无主");
        XWidget_setFocusReason(&fa->m_base, XFocusReason_Mouse);
        expect_true(XWidget_hasFocus(&fa->m_base), "setFocusReason 获得焦点");
        expect_true(XWidget_focusWidget(fRoot) == &fa->m_base,
                    "focusWidget 返回焦点控件");
        expect_true(fa->focusInCount == 1, "focusInEvent 计数");
        XWidget_setFocusReason(&fb->m_base, XFocusReason_Tab);
        expect_true(XWidget_hasFocus(&fb->m_base), "焦点转移");
        expect_true(!XWidget_hasFocus(&fa->m_base), "旧焦点释放");
        expect_true(fa->focusOutCount == 1 && fb->focusInCount == 1,
                    "focusOut/focusIn 顺序事件");
        XWidget_clearFocus(&fb->m_base);
        expect_true(!XWidget_hasFocus(&fb->m_base), "clearFocus 释放焦点");
        expect_true(fb->focusOutCount == 1, "clearFocus 派发 focusOut");
        XWidget_delete_base((XClass*)fa);
        XWidget_delete_base((XClass*)fb);
        XWidget_delete_base((XClass*)fRoot);
    }

    /* ---------- close 接受/拒绝（对标 QWidget::close） ---------- */
    {
        TestWidget* accept = TestWidget_create(NULL);
        TestWidget* reject = TestWidget_create(NULL);
        XWidget_setVisible(&accept->m_base, true);
        expect_true(XWidget_isVisible(&accept->m_base), "close 前已显示");
        expect_true(XWidget_close(&accept->m_base), "close 默认接受返回 true");
        expect_true(!XWidget_isVisible(&accept->m_base) &&
                    XWidget_isHidden(&accept->m_base),
                    "close 接受后隐藏");
        expect_true(accept->closeCount == 1, "closeEvent 派发一次");
        XWidget_setVisible(&reject->m_base, true);
        expect_true(XWidget_isVisible(&reject->m_base), "拒绝 close 前已显示");
        reject->rejectClose = true;
        expect_true(!XWidget_close(&reject->m_base),
                    "closeEvent 忽略返回 false");
        expect_true(XWidget_isVisible(&reject->m_base),
                    "拒绝关闭保持可见不隐藏");
        expect_true(reject->closeCount == 1, "拒绝 closeEvent 仍派发");
        XWidget_delete_base((XClass*)accept);
        XWidget_delete_base((XClass*)reject);
    }

    /* ---------- copy/move（对标 QWidget 拷贝/移动语义子集） ---------- */
    {
        XWidget* src = XWidget_create(NULL, XWindowType_Window);
        XWidget dst;
        XWidget mover;
        title = XString_create_utf8("复制标题");
        XWidget_setGeometry(src, 7, 8, 99, 66);
        XWidget_setMinimumSize(src, 10, 20);
        XWidget_setWindowTitle(src, title);
        XString_delete_base((XClass*)title);
        memset(&dst, 0, sizeof(dst));
        XWidget_copy_base(&dst, src);
        rect = XWidget_geometry(&dst);
        expect_true(rect.x == 7 && rect.y == 8 &&
                    rect.width == 99 && rect.height == 66,
                    "copy_base 复制几何");
        expect_true(XWidget_minimumSize(&dst).width == 10 &&
                    XWidget_minimumSize(&dst).height == 20,
                    "copy_base 复制尺寸约束");
        title = XString_create_utf8("复制标题");
        expect_true(XWidget_windowTitle(&dst) != NULL &&
                    XString_equals(XWidget_windowTitle(&dst), title,
                                   XChar_CaseSensitive),
                    "copy_base 复制标题");
        XString_delete_base((XClass*)title);
        memset(&mover, 0, sizeof(mover));
        XWidget_move_base(&mover, src);
        rect = XWidget_geometry(&mover);
        expect_true(rect.x == 7 && rect.y == 8 &&
                    rect.width == 99 && rect.height == 66,
                    "move_base 转移几何");
        expect_true(XWidget_x(src) == 0 && XWidget_y(src) == 0 &&
                    XWidget_width(src) == 0 && XWidget_height(src) == 0,
                    "move_base 源对象归零");
        XWidget_deinit_base(&dst);
        XWidget_deinit_base(&mover);
        XWidget_delete_base((XClass*)src);
    }

    /* ---------- 控件树 + 可见性传播 + 绘制 (对标 QWidget) ---------- */
    {
        TestWidget* visRoot = TestWidget_create(NULL);
        TestWidget* visA = TestWidget_create(&visRoot->m_base);
        TestWidget* visB = TestWidget_create(&visA->m_base);
        XWidget* hit;

        XWidget_setGeometry(&visA->m_base, 0, 0, 100, 80);
        XWidget_setGeometry(&visB->m_base, 10, 10, 40, 30);
        expect_true(XWidget_parentWidget(&visB->m_base) == &visA->m_base,
                    "parentWidget 指向直接父控件");
        expect_true(XWidget_isAncestorOf(&visRoot->m_base, &visB->m_base),
                    "isAncestorOf 祖先判定");
        expect_true(!XWidget_isAncestorOf(&visB->m_base, &visRoot->m_base),
                    "isAncestorOf 反向为假");

        /* 父隐藏时显式 show 只置显式位（Qt 语义：恢复父显示后自动可见） */
        XWidget_setVisible(&visA->m_base, true);
        XWidget_setVisible(&visB->m_base, true);
        expect_true(!XWidget_isHidden(&visA->m_base) &&
                    !XWidget_isVisible(&visA->m_base),
                    "父隐藏时显式 show 不生效可见");
        expect_true(!XWidget_isHidden(&visB->m_base) &&
                    !XWidget_isVisible(&visB->m_base),
                    "孙控件同理");

        XWidget_show(&visRoot->m_base);
        expect_true(XWidget_isVisible(&visRoot->m_base), "顶层 show 可见");
        expect_true(XWidget_isVisible(&visA->m_base), "子控件随父传播可见");
        expect_true(XWidget_isVisible(&visB->m_base), "孙控件随父传播可见");
        expect_true(visA->showCount == 1 && visB->showCount == 1,
                    "可见性传播仅发一次 showEvent");

        /* isVisibleTo() 只判断 self 到 ancestor 之前的显式隐藏状态：
         * ancestor 本身隐藏不影响结果；无关 ancestor 继续检查到顶层，
         * ancestor=NULL 才等价于 isVisible()。使用独立树避免改变下方
         * 可见性传播计数。 */
        {
            TestWidget* probeRoot = TestWidget_create(NULL);
            TestWidget* probeParent = probeRoot
                ? TestWidget_create(&probeRoot->m_base) : NULL;
            TestWidget* probeChild = probeParent
                ? TestWidget_create(&probeParent->m_base) : NULL;
            TestWidget* unrelated = TestWidget_create(NULL);
            if (probeRoot && probeParent && probeChild && unrelated) {
                XWidget_show(&probeRoot->m_base);
                XWidget_show(&probeParent->m_base);
                XWidget_show(&probeChild->m_base);
                expect_true(XWidget_isVisibleTo(&probeChild->m_base,
                                                &unrelated->m_base),
                            "isVisibleTo 无关 ancestor 仍检查到顶层");
                XWidget_hide(&probeParent->m_base);
                expect_true(XWidget_isVisibleTo(&probeChild->m_base,
                                                &probeParent->m_base),
                            "isVisibleTo 不检查 ancestor 自身隐藏状态");
                expect_true(!XWidget_isVisibleTo(&probeChild->m_base, NULL),
                            "isVisibleTo(NULL) 等价于有效可见状态");
            }
            if (probeChild) XWidget_delete_base((XClass*)probeChild);
            if (probeParent) XWidget_delete_base((XClass*)probeParent);
            if (probeRoot) XWidget_delete_base((XClass*)probeRoot);
            if (unrelated) XWidget_delete_base((XClass*)unrelated);
        }

        /* 命中测试（子控件必须可见才可命中；逆序 Z 序） */
        hit = XWidget_childAt_2(&visA->m_base, 20, 20);
        expect_true(hit == &visB->m_base, "childAt 命中孙控件");
        hit = XWidget_childAt_2(&visRoot->m_base, 5, 5);
        expect_true(hit == &visA->m_base, "childAt 命中子控件");
        hit = XWidget_childAt_2(&visRoot->m_base, 200, 200);
        expect_true(hit == NULL, "childAt 越界返回 NULL");

        /* childrenRect/childrenRegion 只统计可见子控件 */
        rect = XWidget_childrenRect(&visRoot->m_base);
        expect_true(rect.x == 0 && rect.y == 0 &&
                    rect.width == 100 && rect.height == 80,
                    "childrenRect(顶层)=(0,0,100,80)");
        rect = XWidget_childrenRect(&visA->m_base);
        expect_true(rect.x == 10 && rect.y == 10 &&
                    rect.width == 40 && rect.height == 30,
                    "childrenRect(子)=(10,10,40,30)");
        region = XWidget_childrenRegion(&visRoot->m_base);
        expect_true(region.count >= 1, "childrenRegion 非空");
        XRegion_deinit(&region);

        /* update 并入顶层脏区；repaint 同步派发 paintEvent */
        XWidget_update(&visA->m_base);
        expect_true(visRoot->m_base.m_dirty.count >= 1,
                    "update 并入顶层脏区");
        XWidget_repaint(&visA->m_base);
        expect_true(visA->paintCount >= 1, "repaint 同步派发 paintEvent");
        expect_true(visA->hideCount == 0, "尚未隐藏");
        visA->paintCount = 0;

        /* 父可见时显式 hide：立即生效并传播给子树（修复点回归） */
        XWidget_hide(&visA->m_base);
        expect_true(XWidget_isHidden(&visA->m_base), "显式 hide hidden");
        expect_true(!XWidget_isVisible(&visA->m_base), "显式 hide 不可见");
        expect_true(!XWidget_isVisible(&visB->m_base), "孙控件随父隐藏");
        expect_true(visA->hideCount == 1 && visB->hideCount == 1,
                    "hide 传播精确一次 hideEvent");
        expect_true(XWidget_childAt_2(&visRoot->m_base, 5, 5) == NULL,
                    "隐藏子控件不再命中");

        XWidget_show(&visA->m_base);
        expect_true(XWidget_isVisible(&visA->m_base) &&
                    XWidget_isVisible(&visB->m_base),
                    "重新 show 恢复可见传播");

        XWidget_hide(&visRoot->m_base);
        expect_true(!XWidget_isVisible(&visRoot->m_base) &&
                    !XWidget_isVisible(&visA->m_base) &&
                    !XWidget_isVisible(&visB->m_base),
                    "顶层 hide 全树不可见");
        expect_true(!XWidget_isHidden(&visA->m_base) &&
                    !XWidget_isHidden(&visB->m_base),
                    "isHidden 反映显式状态（Qt 语义）");

        /* 清理：先子后父（堆对象）；父已隐藏，无平台残留 */
        XWidget_delete_base((XClass*)visB);
        XWidget_delete_base((XClass*)visA);
        XWidget_delete_base((XClass*)visRoot);
    }
    {
        TestWidget* reparentRoot = TestWidget_create(NULL);
        TestWidget* reparentA = TestWidget_create(&reparentRoot->m_base);
        TestWidget* reparentB = TestWidget_create(&reparentRoot->m_base);
        TestWidget* reparentChild = TestWidget_create(&reparentA->m_base);
        XWidget_setGeometry(&reparentA->m_base, 0, 0, 120, 80);
        XWidget_setGeometry(&reparentB->m_base, 140, 0, 120, 80);
        XWidget_setGeometry(&reparentChild->m_base, 12, 14, 40, 20);
        XWidget_show(&reparentRoot->m_base);
        XWidget_show(&reparentA->m_base);
        XWidget_show(&reparentB->m_base);
        XWidget_show(&reparentChild->m_base);
        expect_true(XWidget_isVisible(&reparentChild->m_base),
                    "setParent 前子控件可见");
        XWidget_setParentPlain(&reparentChild->m_base, &reparentB->m_base);
        expect_true(XWidget_parentWidget(&reparentChild->m_base) ==
                        &reparentB->m_base,
                    "setParentPlain 更新父控件");
        expect_true(!XWidget_isVisible(&reparentChild->m_base) &&
                        XWidget_isHidden(&reparentChild->m_base),
                    "setParentPlain 后控件隐藏且需显式 show");
        expect_true(XWidget_x(&reparentChild->m_base) == 0 &&
                        XWidget_y(&reparentChild->m_base) == 0 &&
                        XWidget_width(&reparentChild->m_base) == 40 &&
                        XWidget_height(&reparentChild->m_base) == 20,
                    "setParentPlain 将子控件位置归零并保持尺寸");
        XWidget_show(&reparentChild->m_base);
        expect_true(XWidget_isVisible(&reparentChild->m_base),
                    "setParentPlain 后显式 show 恢复可见");
        XWidget_delete_base((XClass*)reparentChild);
        XWidget_delete_base((XClass*)reparentB);
        XWidget_delete_base((XClass*)reparentA);
        XWidget_delete_base((XClass*)reparentRoot);
    }
#if XWINDOW_ON && XACCESSIBLE_ON
    {
        XWidget* a = XWidget_create(NULL, XWindowType_Window);
        XWidget* b = XWidget_create(a, XWindowType_Widget);
        XString* objectName = XString_create_utf8("accessible-child");
        XAccessible* aa = a ? a->m_accessible : NULL;
        XAccessible* ab = b ? b->m_accessible : NULL;
        XRect rect;
        if (b && objectName) XObject_setObjectName((XObject*)b, objectName);
        if (a) XWidget_setGeometry(a, 30, 40, 200, 100);
        if (b) XWidget_setGeometry(b, 7, 9, 80, 20);
        expect_true(aa && ab && XAccessible_role(aa) == XAccessibleRole_Window &&
                    XAccessible_role(ab) == XAccessibleRole_Client,
                    "控件辅助功能节点角色");
        expect_true(aa && XAccessible_childCount(aa) == 1 &&
                    XAccessible_childAtIndex(aa, 0) == ab &&
                    XAccessible_parent(ab) == aa &&
                    XAccessible_childAtIndex(aa, 1) == NULL,
                    "控件辅助功能父子索引");
        {
            XString* name = XAccessible_name(ab);
            expect_true(name && strcmp(XString_toUtf8(name), "accessible-child") == 0,
                        "控件辅助功能名称跟随 objectName");
            if (name) XString_delete_base((XClass*)name);
        }
        rect = XAccessible_rect(ab);
        expect_true(rect.x == 37 && rect.y == 49 && rect.width == 80 && rect.height == 20,
                    "控件辅助功能全局几何");
        if (objectName) XString_delete_base((XClass*)objectName);
        if (b) XWidget_delete_base((XClass*)b);
        if (a) XWidget_delete_base((XClass*)a);
    }
#endif /* XWINDOW_ON && XACCESSIBLE_ON */
}

#if XLAYOUT_ON

/* ---------------- X布局系统契约测试（对标 Qt 6.8 QLayout 家族） ---------------- */

/** @brief 布局条目的隐藏宿主控件。
 * @details 布局控件都挂在此宿主下并显式 setVisible(true)：
 *          显式可见使条目 isHidden()=false（不空、参与排布）；
 *          宿主保持隐藏使子控件 isVisible()=false（不真正上屏），
 *          完全符合 Qt「不隐藏也不可见」的布局条目语义。 */
static TestWidget* g_layoutTestHost = NULL;

/** @brief 创建固定尺寸测试控件（sizeHint=最小=最大=size）。 */
static TestWidget* layout_make_fixed_widget(int w, int h)
{
    TestWidget* tw;
    XSize size;
    if (!g_layoutTestHost)
        g_layoutTestHost = TestWidget_create(NULL);
    if (!g_layoutTestHost) return NULL;
    tw = TestWidget_create(&g_layoutTestHost->m_base);
    if (!tw) return NULL;
    XSize_init(&size, w, h);
    XWidget_setSizeHint(&tw->m_base, &size);
    XWidget_setMinimumSize(&tw->m_base, w, h);
    XWidget_setMaximumSize(&tw->m_base, w, h);
    XWidget_setVisible(&tw->m_base, true);
    return tw;
}

/** @brief 创建可伸缩测试控件（sizeHint=最小=size，最大不设限）。 */
static TestWidget* layout_make_growable_widget(int w, int h)
{
    TestWidget* tw;
    XSize size;
    if (!g_layoutTestHost)
        g_layoutTestHost = TestWidget_create(NULL);
    if (!g_layoutTestHost) return NULL;
    tw = TestWidget_create(&g_layoutTestHost->m_base);
    if (!tw) return NULL;
    XSize_init(&size, w, h);
    XWidget_setSizeHint(&tw->m_base, &size);
    XWidget_setMinimumSize(&tw->m_base, w, h);
    XWidget_setVisible(&tw->m_base, true);
    return tw;
}

/** @brief 辅助断言：控件几何精确匹配。 */
static void layout_expect_rect(TestWidget* tw, int x, int y, int w, int h,
                               const char* name)
{
    XRect r;
    if (!tw) {
        expect_true(false, name);
        return;
    }
    r = XWidget_geometry(&tw->m_base);
    if (!(r.x == x && r.y == y && r.width == w && r.height == h))
        printf("[layout] %s: actual=(%d,%d,%d,%d) expect=(%d,%d,%d,%d)\n",
               name, r.x, r.y, r.width, r.height, x, y, w, h);
    expect_true(r.x == x && r.y == y && r.width == w && r.height == h, name);
}

static int layout_hfw_probe(XWidget* widget, int width, void* userData)
{
    (void)widget;
    (void)userData;
    return width / 2 + 3;
}

/** @brief XLayoutItem 基类契约：几何/对齐/条目查询/尺寸协商。 */
static void test_layout_item_contract(void)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout box;
    XLayoutItem* item;
    TestWidget* w;
    XRect rect;
    XRect got;
    XSize size;

    w = layout_make_growable_widget(30, 40);
    expect_true(w != NULL, "layout item 测试控件创建");
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&box, &w->m_base);
    expect_true(XLayout_count_base((XLayout*)&box) == 1, "box 条目计数=1");
    item = XLayout_itemAt_base((XLayout*)&box, 0);
    expect_true(item != NULL, "itemAt(0) 非空");
    expect_true(XLayoutItem_widget_base(item) == &w->m_base,
                "widget() 返回原控件");
    expect_true(XLayoutItem_layout_base(item) == NULL,
                "控件条目 layout() 返回 NULL");
    expect_true(!XLayoutItem_isEmpty_base(item), "可见控件条目非空");

    {
        XWidgetSizePolicy policy = XWidget_sizePolicy(&w->m_base);
        XWidgetSizePolicy_setHeightForWidth(&policy, true);
        XWidget_setSizePolicyFull(&w->m_base, &policy);
        XWidget_setHeightForWidthHandler(&w->m_base, layout_hfw_probe, NULL);
        expect_true(XLayoutItem_hasHeightForWidth_base(item) &&
                    XLayoutItem_heightForWidth_base(item, 100) == 53,
                    "QWidgetItem heightForWidth 回调按宽度计算");
    }

    /* 对齐读写与几何收拢（对标 Qt QWidgetItem::setGeometry）。 */
    XLayoutItem_setAlignment(item, XLayoutAlignment_Right |
                                          XLayoutAlignment_VCenter);
    expect_true(XLayoutItem_alignment(item) ==
                    (XLayoutAlignment_Right | XLayoutAlignment_VCenter),
                "alignment 读写一致");
    XRect_init(&rect, 0, 0, 300, 100);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    got = XWidget_geometry(&w->m_base);
    expect_true(got.x == 270 && got.y == 30 && got.width == 30 &&
                got.height == 40,
                "Right|VCenter 收拢到首选并右/中摆放");
    /* 清空对齐后条目填满分配单元格。 */
    XLayoutItem_setAlignment(item, 0);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    got = XWidget_geometry(&w->m_base);
    expect_true(got.x == 0 && got.y == 0 &&
                got.width == 300 && got.height == 100,
                "清除对齐后条目填满单元格");
    got = XLayoutItem_geometry_base(item);
    expect_true(got.x == 0 && got.y == 0 &&
                got.width == 300 && got.height == 100,
                "Item::geometry() 保存分配快照");
    size = XLayoutItem_sizeHint_base(item);
    expect_true(size.width == 30 && size.height == 40, "Item::sizeHint()");
    size = XLayoutItem_minimumSize_base(item);
    expect_true(size.width == 30 && size.height == 40, "Item::minimumSize()");

    XLayoutItem_invalidate_base((XLayoutItem*)&box);
    XLayoutItem_deinit_base((XLayoutItem*)&box);
    XWidget_delete_base((XClass*)w);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
}

/** @brief XBoxLayout 契约：边距/间距/固定尺寸/伸展/镜像/takeAt/垂直 BTT。 */
static void test_box_layout_contract(void)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout box;
    XBoxLayout box2;
    XBoxLayout vbox;
    TestWidget* w0;
    TestWidget* w1;
    TestWidget* w2;
    TestWidget* ga;
    TestWidget* gb;
    TestWidget* wv;
    XLayoutItem* item;
    XRect rect;
    int idx;

    /* ---------- 固定尺寸 + 边距 + 间距（精确分配） ---------- */
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    w0 = layout_make_fixed_widget(40, 20);
    w1 = layout_make_fixed_widget(30, 20);
    w2 = layout_make_fixed_widget(50, 20);
    expect_true(w0 && w1 && w2, "box 测试控件创建");
    XLayout_setContentsMargins((XLayout*)&box, 4, 5, 6, 7);
    XLayout_setSpacing((XLayout*)&box, 6);
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addWidget(&box, &w1->m_base);
    XBoxLayout_addWidget(&box, &w2->m_base);
    XRect_init(&rect, 0, 0, 142, 32);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 4, 5, 40, 20, "HBox 边距/间距 w0=(4,5,40,20)");
    layout_expect_rect(w1, 50, 5, 30, 20, "HBox 边距/间距 w1=(50,5,30,20)");
    layout_expect_rect(w2, 86, 5, 50, 20, "HBox 边距/间距 w2=(86,5,50,20)");

    /* ---------- RTL 镜像（对标 QBoxLayout 布局方向） ---------- */
    XBoxLayout_setDirection(&box, XBoxLayoutDirection_RightToLeft);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 96, 5, 40, 20, "RTL w0=(96,5,40,20)");
    layout_expect_rect(w1, 60, 5, 30, 20, "RTL w1=(60,5,30,20)");
    layout_expect_rect(w2, 4, 5, 50, 20, "RTL w2=(4,5,50,20)");
    XBoxLayout_setDirection(&box, XBoxLayoutDirection_LeftToRight);

    /* ---------- takeAt 同步移除并返回条目 ---------- */
    expect_true(XLayout_count_base((XLayout*)&box) == 3, "box takeAt 前 count=3");
    item = XLayout_takeAt_base((XLayout*)&box, 1);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &w1->m_base,
                "takeAt(1) 返回 w1 条目");
    expect_true(XLayout_count_base((XLayout*)&box) == 2, "takeAt 后 count=2");
    idx = XLayout_indexOf((XLayout*)&box, &w2->m_base);
    expect_true(idx == 1, "takeAt 后 w2 索引前移为 1");
    XLayoutItem_delete_base(item);
    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w0);
    XWidget_delete_base((XClass*)w1);
    XWidget_delete_base((XClass*)w2);

    /* ---------- 伸展因子 1:2 分配多余空间 ---------- */
    XBoxLayout_init(&box2, XBoxLayoutDirection_LeftToRight);
    ga = layout_make_growable_widget(30, 20);
    gb = layout_make_growable_widget(30, 20);
    expect_true(ga && gb, "box stretch 测试控件创建");
    XBoxLayout_addWidget(&box2, &ga->m_base);
    XBoxLayout_addWidget(&box2, &gb->m_base);
    expect_true(XBoxLayout_setStretchFactorWidget(&box2, &ga->m_base, 1),
                "setStretchFactorWidget(ga,1)");
    expect_true(XBoxLayout_setStretchFactorWidget(&box2, &gb->m_base, 2),
                "setStretchFactorWidget(gb,2)");
    XRect_init(&rect, 0, 0, 90, 30);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box2, &rect);
    layout_expect_rect(ga, 0, 0, 30, 30, "stretch 1:2 ga=(0,0,30,30)");
    layout_expect_rect(gb, 30, 0, 60, 30, "stretch 1:2 gb=(30,0,60,30)");
    XLayoutItem_deinit_base((XLayout*)&box2);
    XWidget_delete_base((XClass*)ga);
    XWidget_delete_base((XClass*)gb);

    /* ---------- 垂直盒 BottomToTop / TopToBottom ---------- */
    XBoxLayout_init(&vbox, XBoxLayoutDirection_BottomToTop);
    wv = layout_make_fixed_widget(40, 20);
    expect_true(wv != NULL, "vbox 测试控件创建");
    XBoxLayout_addWidget(&vbox, &wv->m_base);
    XRect_init(&rect, 0, 0, 100, 50);
    XLayoutItem_setGeometry_base((XLayoutItem*)&vbox, &rect);
    layout_expect_rect(wv, 0, 15, 40, 20, "BTT wv=(0,15,40,20)");
    XBoxLayout_setDirection(&vbox, XBoxLayoutDirection_TopToBottom);
    XLayoutItem_setGeometry_base((XLayoutItem*)&vbox, &rect);
    layout_expect_rect(wv, 0, 15, 40, 20, "TTB wv=(0,15,40,20)");
    XLayoutItem_deinit_base((XLayout*)&vbox);
    XWidget_delete_base((XClass*)wv);

    /* ---------- 低于最小总宽：不压到最小以下（对标 Qt，回归修复） ----------
     * 早期盒式分配在 space<=间距和时调用 shrinkToMin 把条目压缩到最小以下；
     * 与 Qt 实机行为（激活后窗口放大到布局最小尺寸、条目恒保有最小尺寸）
     * 不符，且该 helper 已删除导致编译失败。修复后空间从大到小（含小于单
     * 条间距）都应逐项给满最小尺寸，多余部分自然溢出。 */
    {
        XBoxLayout tight;
        TestWidget* ta;
        TestWidget* tb;
        XBoxLayout_init(&tight, XBoxLayoutDirection_LeftToRight);
        ta = layout_make_growable_widget(100, 20);
        tb = layout_make_growable_widget(100, 20);
        expect_true(ta && tb, "box 低于最小 测试控件创建");
        XLayout_setSpacing((XLayout*)&tight, 6);
        XBoxLayout_addWidget(&tight, &ta->m_base);
        XBoxLayout_addWidget(&tight, &tb->m_base);
        /* 正好最小总宽 100+6+100=206。 */
        XRect_init(&rect, 0, 0, 206, 20);
        XLayoutItem_setGeometry_base((XLayoutItem*)&tight, &rect);
        layout_expect_rect(ta, 0, 0, 100, 20, "below-min 基准 ta=(0,0,100,20)");
        layout_expect_rect(tb, 106, 0, 100, 20, "below-min 基准 tb=(106,0,100,20)");
        /* 150 / 60 / 3：均低于最小总宽，条目保持最小、位置按间距推算溢出。 */
        XRect_init(&rect, 0, 0, 150, 20);
        XLayoutItem_setGeometry_base((XLayoutItem*)&tight, &rect);
        layout_expect_rect(ta, 0, 0, 100, 20, "below-min-150 ta=(0,0,100,20)");
        layout_expect_rect(tb, 106, 0, 100, 20, "below-min-150 tb=(106,0,100,20)");
        XRect_init(&rect, 0, 0, 60, 20);
        XLayoutItem_setGeometry_base((XLayoutItem*)&tight, &rect);
        layout_expect_rect(ta, 0, 0, 100, 20, "below-min-60 ta=(0,0,100,20)");
        layout_expect_rect(tb, 106, 0, 100, 20, "below-min-60 tb=(106,0,100,20)");
        XRect_init(&rect, 0, 0, 3, 20);
        XLayoutItem_setGeometry_base((XLayoutItem*)&tight, &rect);
        layout_expect_rect(ta, 0, 0, 100, 20, "below-spacing-3 ta=(0,0,100,20)");
        layout_expect_rect(tb, 106, 0, 100, 20, "below-spacing-3 tb=(106,0,100,20)");
        XLayoutItem_deinit_base((XLayout*)&tight);
        XWidget_delete_base((XClass*)ta);
        XWidget_delete_base((XClass*)tb);
    }
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
}

/** @brief XBoxLayout 扩展契约：addStrut/公开空白条目与 insertSpacerItem/
 *  setDirection 翻转（对标 Qt 6.8 QBoxLayout 其余公开 API）。 */
static void test_box_layout_extended(void)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout box;
    XRect rect;
    XSize size;
    TestWidget* w0;
    TestWidget* w1;
    XRect got;
    int g;
    int h;
    int i;
    int j;
    int k;
    XLayoutItem* item;
    XWidgetSizePolicy pol;
#if XLAYOUT_SPACER_ON
    XSpacerItem* sp;
#endif /* XLAYOUT_SPACER_ON */

    /* ---------- addStrut：交叉轴最小尺寸（对标 QBoxLayout::addStrut） ---------- */
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    w0 = layout_make_fixed_widget(40, 20);
    expect_true(w0 != NULL, "box ext 测试控件创建");
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addStrut(&box, 50);
    expect_true(XLayout_count_base((XLayout*)&box) == 2, "addStrut 追加空白条目");
    size = XLayoutItem_sizeHint_base((XLayoutItem*)&box);
    expect_true(size.width == 40 && size.height == 50, "HBox+strut sizeHint=(40,50)");
    XRect_init(&rect, 0, 0, 40, 50);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 0, 15, 40, 20, "HBox+strut w0=(0,15,40,20)");
    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w0);

    XBoxLayout_init(&box, XBoxLayoutDirection_TopToBottom);
    w0 = layout_make_fixed_widget(40, 20);
    expect_true(w0 != NULL, "VBox ext 测试控件创建");
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addStrut(&box, 80);
    size = XLayoutItem_sizeHint_base((XLayoutItem*)&box);
    expect_true(size.width == 80 && size.height == 20, "VBox+strut sizeHint=(80,20)");
    XRect_init(&rect, 0, 0, 80, 20);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 0, 0, 40, 20, "VBox+strut w0=(0,0,40,20)");
    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w0);

#if XLAYOUT_SPACER_ON
    /* ---------- XSpacerItem 公开条目：策略/尺寸读写（对标 QSpacerItem） ---------- */
    sp = XSpacerItem_create(20, 10, XWidgetSizePolicy_Minimum,
                            XWidgetSizePolicy_Fixed);
    expect_true(sp != NULL, "XSpacerItem_create 成功");
    pol = XSpacerItem_sizePolicy(sp);
    expect_true(pol.m_horizontalPolicy == XWidgetSizePolicy_Minimum &&
                pol.m_verticalPolicy == XWidgetSizePolicy_Fixed,
                "spacer 初始尺寸策略读写");
    item = (XLayoutItem*)sp;
    size = XLayoutItem_sizeHint_base(item);
    expect_true(size.width == 20 && size.height == 10, "spacer sizeHint=(20,10)");
    size = XLayoutItem_minimumSize_base(item);
    expect_true(size.width == 20 && size.height == 10,
                "spacer minimumSize=(20,10)");
    XSpacerItem_changeSize(sp, 40, 5, XWidgetSizePolicy_Expanding,
                           XWidgetSizePolicy_Minimum);
    size = XLayoutItem_sizeHint_base(item);
    expect_true(size.width == 40 && size.height == 5,
                "changeSize 后 sizeHint=(40,5)");
    size = XLayoutItem_minimumSize_base(item);
    expect_true(size.width == 0 && size.height == 5,
                "changeSize 后 minimumSize=(0,5)（Expanding 收缩语义）");

    /* insertSpacerItem：插入到索引 0，空白条目所有权转移给布局。 */
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    w0 = layout_make_growable_widget(30, 20);
    w1 = layout_make_growable_widget(30, 20);
    expect_true(w0 && w1, "insertSpacerItem 测试控件创建");
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addWidget(&box, &w1->m_base);
    XBoxLayout_insertSpacerItem(&box, 0, sp);
    expect_true(XLayout_count_base((XLayout*)&box) == 3,
                "insertSpacerItem 插入到索引 0");
    XRect_init(&rect, 0, 0, 80, 20);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 20, 0, 30, 20, "spacer w0=(20,0,30,20)");
    layout_expect_rect(w1, 50, 0, 30, 20, "spacer w1=(50,0,30,20)");
    XLayoutItem_deinit_base((XLayout*)&box);  /* 释放 spacer 与两个控件条目 */
    XWidget_delete_base((XClass*)w0);
    XWidget_delete_base((XClass*)w1);
#endif /* XLAYOUT_SPACER_ON */

    /* ---------- setDirection：水平↔垂直翻转 magic 空白（对标 QBoxLayout） ---------- */
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    w0 = layout_make_growable_widget(30, 20);
    expect_true(w0 != NULL, "setDirection H→TTB 测试控件创建");
    XBoxLayout_addSpacing(&box, 20);
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addStretch(&box, 1);
    XBoxLayout_setDirection(&box, XBoxLayoutDirection_TopToBottom);
    XRect_init(&rect, 0, 0, 60, 50);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 0, 20, 60, 20, "H→TTB w0=(0,20,60,20)");
    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w0);

    XBoxLayout_init(&box, XBoxLayoutDirection_TopToBottom);
    w0 = layout_make_growable_widget(30, 20);
    expect_true(w0 != NULL, "setDirection V→LTR 测试控件创建");
    XBoxLayout_addSpacing(&box, 20);
    XBoxLayout_addWidget(&box, &w0->m_base);
    XBoxLayout_addStretch(&box, 1);
    XBoxLayout_setDirection(&box, XBoxLayoutDirection_LeftToRight);
    XRect_init(&rect, 0, 0, 120, 30);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    layout_expect_rect(w0, 20, 0, 30, 30, "V→LTR w0=(20,0,30,30)");
    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w0);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
}

/** @brief XGridLayout 契约：定位/伸展/跨度/查询/原点角/takeAt/拷贝移动。 */
static void test_grid_layout_contract(void)
{
#if XLAYOUT_ON && XLAYOUT_GRID_ON
    XGridLayout grid;
    XGridLayout span;
    XGridLayout grid2;
    TestWidget* w;
    TestWidget* wa;
    TestWidget* wb;
    XLayoutItem* taken;
    XSize size;
    TestWidget* cells[2][3];
    XLayoutItem* item;
    XRect rect;
    XRect got;
    int row;
    int column;
    int rowSpan;
    int columnSpan;

    /* ---------- 基本定位与查询（精确贴合首选尺寸） ---------- */
    XGridLayout_init(&grid);
    w = layout_make_fixed_widget(30, 20);   /* (0,0) */
    wa = layout_make_fixed_widget(50, 20);  /* (0,2) */
    wb = layout_make_fixed_widget(40, 20);  /* (1,1) */
    expect_true(w && wa && wb, "grid 测试控件创建");
    XGridLayout_addWidget(&grid, &w->m_base, 0, 0, 0);
    XGridLayout_addWidget(&grid, &wa->m_base, 0, 2, 0);
    XGridLayout_addWidget(&grid, &wb->m_base, 1, 1, 0);
    expect_true(XGridLayout_rowCount(&grid) == 2, "grid rowCount=2");
    expect_true(XGridLayout_columnCount(&grid) == 3, "grid columnCount=3");
    size = XLayoutItem_sizeHint_base((XLayoutItem*)&grid);
    expect_true(size.width == 120 && size.height == 40,
                "grid sizeHint=(120,40)");
    /* 回归：分配 80x40（低于最小总宽 120x40），仍按最小逐列给满、
     * 不压缩到最小以下（对标 Qt 语义），超出部分自然溢出。 */
    XRect_init(&rect, 0, 0, 80, 40);
    XLayoutItem_setGeometry_base((XLayoutItem*)&grid, &rect);
    layout_expect_rect(w, 0, 0, 30, 20, "grid (0,0) w=(0,0,30,20)");
    layout_expect_rect(wb, 30, 20, 40, 20, "grid (1,1) wb=(30,20,40,20)");
    layout_expect_rect(wa, 70, 0, 50, 20, "grid (0,2) wa=(70,0,50,20)");
    got = XGridLayout_cellRect(&grid, 0, 0);
    expect_true(got.x == 0 && got.y == 0 && got.width == 30 &&
                got.height == 20, "cellRect(0,0)=(0,0,30,20)");
    got = XGridLayout_cellRect(&grid, 1, 1);
    expect_true(got.x == 30 && got.y == 20 && got.width == 40 &&
                got.height == 20, "cellRect(1,1)=(30,20,40,20)");
    got = XGridLayout_cellRect(&grid, 0, 1);
    expect_true(got.x == 30 && got.y == 0 && got.width == 40 &&
                got.height == 20, "cellRect(0,1)=(30,0,40,20)");
    item = XGridLayout_itemAtPosition(&grid, 0, 0);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &w->m_base,
                "itemAtPosition(0,0)==w");
    item = XGridLayout_itemAtPosition(&grid, 1, 1);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &wb->m_base,
                "itemAtPosition(1,1)==wb");
    expect_true(XGridLayout_itemAtPosition(&grid, 0, 1) == NULL,
                "itemAtPosition(0,1)==NULL(空列)");
    expect_true(XGridLayout_getItemPosition(&grid, 0, &row, &column,
                                            &rowSpan, &columnSpan) &&
                row == 0 && column == 0 && rowSpan == 1 && columnSpan == 1,
                "getItemPosition(0)=(0,0,1,1)");
    expect_true(XGridLayout_getItemPosition(&grid, 1, &row, &column,
                                            &rowSpan, &columnSpan) &&
                row == 0 && column == 2 && rowSpan == 1 && columnSpan == 1,
                "getItemPosition(1)=(0,2,1,1)");
    expect_true(!XGridLayout_getItemPosition(&grid, 99, NULL, NULL,
                                             NULL, NULL),
                "getItemPosition(越界)=false");

    /* ---------- 原点角镜像 ---------- */
    XGridLayout_setOriginCorner(&grid, XGridLayoutOriginCorner_BottomLeft);
    expect_true(XGridLayout_originCorner(&grid) ==
                    XGridLayoutOriginCorner_BottomLeft,
                "originCorner 读写一致");
    XLayoutItem_setGeometry_base((XLayoutItem*)&grid, &rect);
    layout_expect_rect(w, 0, 20, 30, 20, "BottomLeft w 沉底");
    layout_expect_rect(wb, 30, 0, 40, 20, "BottomLeft wb 浮顶");
    {
        /* Qt 实机下窗口会自动放大到布局最小尺寸（120x40），
         * 因此原点角镜像按 120x40 矩形校准（与 Qt 6.8.3 实测一致）。 */
        XRect cornerRect;
        XRect_init(&cornerRect, 0, 0, 120, 40);
        XGridLayout_setOriginCorner(&grid, XGridLayoutOriginCorner_TopRight);
        XLayoutItem_setGeometry_base((XLayoutItem*)&grid, &cornerRect);
        layout_expect_rect(w, 90, 0, 30, 20, "TopRight w 右移(90,0,30,20)");
        layout_expect_rect(wa, 0, 0, 50, 20, "TopRight wa 居左(0,0,50,20)");
        layout_expect_rect(wb, 50, 20, 40, 20, "TopRight wb 右移(50,20,40,20)");
        XGridLayout_setOriginCorner(&grid, XGridLayoutOriginCorner_TopLeft);
    }

    /* ---------- takeAt 与单元格数组同步 ---------- */
    taken = XLayout_takeAt_base((XLayout*)&grid, 0);
    expect_true(taken != NULL &&
                XLayoutItem_widget_base(taken) == &w->m_base,
                "grid takeAt(0) 返回 w 条目");
    expect_true(XLayout_count_base((XLayout*)&grid) == 2, "grid takeAt 后 count=2");
    expect_true(XGridLayout_itemAtPosition(&grid, 0, 0) == NULL,
                "takeAt 后 (0,0) 不再被占用");
    item = XGridLayout_itemAtPosition(&grid, 0, 2);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &wa->m_base,
                "takeAt 后 (0,2) 仍为 wa");
    item = XGridLayout_itemAtPosition(&grid, 1, 1);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &wb->m_base,
                "takeAt 后 (1,1) 仍为 wb");
    expect_true(XGridLayout_rowCount(&grid) == 2 &&
                XGridLayout_columnCount(&grid) == 3,
                "takeAt 后行列数保持");
    /* 被取出条目的对象由调用方释放；阵内剩余条目由 deinit 统一释放。 */
    XLayoutItem_delete_base(taken);
    XLayoutItem_deinit_base((XLayout*)&grid);
    XWidget_delete_base((XClass*)w);
    XWidget_delete_base((XClass*)wa);
    XWidget_delete_base((XClass*)wb);

    /* ---------- 跨格合并（span） ---------- */
    XGridLayout_init(&span);
    w = layout_make_fixed_widget(10, 10);
    wa = layout_make_fixed_widget(20, 20);
    expect_true(w && wa, "grid span 测试控件创建");
    XGridLayout_addWidget(&span, &w->m_base, 0, 0, 0);
    XGridLayout_addWidgetSpan(&span, &wa->m_base, 1, 0, 1, 2, 0);
    XRect_init(&rect, 0, 0, 20, 30); /* 网格最小 20x30，对标 Qt 校准点 */
    XLayoutItem_setGeometry_base((XLayoutItem*)&span, &rect);
    layout_expect_rect(w, 0, 0, 10, 10, "span w=(0,0,10,10)");
    layout_expect_rect(wa, 0, 10, 20, 20, "span wa=(0,10,20,20)跨两列");
    got = XGridLayout_cellRect(&span, 1, 1);
    expect_true(got.x == 10 && got.y == 10 && got.width == 10 &&
                got.height == 20, "span cellRect(1,1)=(10,10,10,20)");
    item = XGridLayout_itemAtPosition(&span, 1, 1);
    expect_true(item != NULL && XLayoutItem_widget_base(item) == &wa->m_base,
                "span itemAtPosition(1,1) 覆盖跨格条目");
    XLayoutItem_deinit_base((XLayout*)&span);
    XWidget_delete_base((XClass*)w);
    XWidget_delete_base((XClass*)wa);

    /* ---------- 列伸展 + 行列间距 ---------- */
    XGridLayout_init(&grid2);
    for (row = 0; row < 2; ++row) {
        for (column = 0; column < 3; ++column) {
            int rw = 30 + column * 10;
            cells[row][column] = layout_make_growable_widget(rw, 20);
            expect_true(cells[row][column] != NULL, "grid2 控件创建");
            XGridLayout_addWidget(&grid2, &cells[row][column]->m_base,
                                  row, column, 0);
        }
    }
    XGridLayout_setHorizontalSpacing(&grid2, 5);
    XGridLayout_setVerticalSpacing(&grid2, 5);
    XGridLayout_setColumnStretch(&grid2, 0, 1);
    XGridLayout_setColumnStretch(&grid2, 1, 2);
    XRect_init(&rect, 0, 0, 300, 100);
    XLayoutItem_setGeometry_base((XLayoutItem*)&grid2, &rect);
    /* 三列 x 坐标：0 + 87+5=92 + 153+5=250；行距 5+2=7。 */
    layout_expect_rect(cells[0][0], 0, 0, 87, 48, "grid2 (0,0)=(0,0,87,48)");
    layout_expect_rect(cells[0][1], 92, 0, 153, 48, "grid2 (0,1)=(92,0,153,48)");
    layout_expect_rect(cells[0][2], 250, 0, 50, 48, "grid2 (0,2)=(250,0,50,48)");
    layout_expect_rect(cells[1][0], 0, 53, 87, 47, "grid2 (1,0)=(0,53,87,47)");
    layout_expect_rect(cells[1][1], 92, 53, 153, 47, "grid2 (1,1)=(92,53,153,47)");
    layout_expect_rect(cells[1][2], 250, 53, 50, 47, "grid2 (1,2)=(250,53,50,47)");
    XLayoutItem_deinit_base((XLayout*)&grid2);
    for (row = 0; row < 2; ++row)
        for (column = 0; column < 3; ++column)
            XWidget_delete_base((XClass*)cells[row][column]);

    /* ---------- 拷贝/移动语义 ---------- */
    {
        XGridLayout src;
        XGridLayout copy;
        XGridLayout moved;
        TestWidget* wd = layout_make_fixed_widget(15, 15);
        expect_true(wd != NULL, "grid move 测试控件创建");
        XGridLayout_init(&src);
        XGridLayout_addWidget(&src, &wd->m_base, 0, 0, 0);
        expect_true(XGridLayout_rowCount(&src) == 1 &&
                    XGridLayout_columnCount(&src) == 1, "src 网格 1x1");
        XGridLayout_init(&copy);
        XClass_copy_base((XClass*)&copy, (const XClass*)&src);
        expect_true(XLayout_count_base((XLayout*)&copy) == 0 &&
                    XGridLayout_columnCount(&copy) == 0 &&
                    XLayout_count_base((XLayout*)&src) == 1,
                    "copy 不复制条目树");
        XGridLayout_init(&moved);
        XClass_move_base((XClass*)&moved, (XClass*)&src);
        expect_true(XLayout_count_base((XLayout*)&moved) == 1 &&
                    XGridLayout_rowCount(&moved) == 1 &&
                    XGridLayout_columnCount(&moved) == 1 &&
                    XLayout_count_base((XLayout*)&src) == 0,
                    "move 转移条目与网格状态");
        XRect_init(&rect, 0, 0, 15, 15);
        XLayoutItem_setGeometry_base((XLayoutItem*)&moved, &rect);
        layout_expect_rect(wd, 0, 0, 15, 15, "move 后网格几何分配正常");
        XLayoutItem_deinit_base((XLayout*)&moved);
        XLayoutItem_deinit_base((XLayout*)&copy);
        XLayoutItem_deinit_base((XLayout*)&src);
        XWidget_delete_base((XClass*)wd);
    }
#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
}

/** @brief XGridLayout 默认定位：setDefaultPositioning/addWidgetAuto 游标
 *  推进、绕行与 addItemAt 显式坐标（对标 Qt 6.8 QGridLayout 无坐标添加）。 */
static void test_grid_layout_default_positioning(void)
{
#if XLAYOUT_ON && XLAYOUT_GRID_ON
    XGridLayout grid;
    XSpacerItem* sp0;
    XSpacerItem* sp1;
    XSpacerItem* sp2;
    TestWidget* ws[4];
    int row;
    int column;
    int rowSpan;
    int columnSpan;
    int i;
    bool ok;

    /* ---------- Horizontal：先横后竖，满列绕行走位 ---------- */
    XGridLayout_init(&grid);
    XGridLayout_setDefaultPositioning(&grid, 3, XOrientation_Horizontal);
    expect_true(XGridLayout_rowCount(&grid) == 1 &&
                XGridLayout_columnCount(&grid) == 3,
                "setDefaultPositioning(3,H) → 1x3");
    for (i = 0; i < 4; ++i) {
        ws[i] = layout_make_fixed_widget(10, 10);
        expect_true(ws[i] != NULL, "grid 默认定位控件创建");
        XGridLayout_addWidgetAuto(&grid, &ws[i]->m_base);
    }
    ok = XGridLayout_getItemPosition(&grid, 0, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 0 && column == 0, "默认定位 #0=(0,0)");
    ok = XGridLayout_getItemPosition(&grid, 1, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 0 && column == 1, "默认定位 #1=(0,1)");
    ok = XGridLayout_getItemPosition(&grid, 2, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 0 && column == 2, "默认定位 #2=(0,2)");
    ok = XGridLayout_getItemPosition(&grid, 3, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 0, "默认定位 #3=(1,0)（满列绕行）");
    expect_true(XGridLayout_rowCount(&grid) == 2 &&
                XGridLayout_columnCount(&grid) == 3, "默认定位后网格 2x3");

#if XLAYOUT_SPACER_ON
    /* 重置规模（只扩不减）后，游戏中游标从当前 (1,1) 继续推进。 */
    XGridLayout_setDefaultPositioning(&grid, 4, XOrientation_Horizontal);
    expect_true(XGridLayout_rowCount(&grid) == 2 &&
                XGridLayout_columnCount(&grid) == 4,
                "setDefaultPositioning(4,H) 只扩不减 → 2x4");
    sp0 = XSpacerItem_create(5, 5, XWidgetSizePolicy_Fixed,
                             XWidgetSizePolicy_Fixed);
    expect_true(sp0 != NULL, "grid 默认定位 spacer 创建");
    XGridLayout_addItem(&grid, (XLayoutItem*)sp0);   /* 借用：调用方释放 */
    ok = XGridLayout_getItemPosition(&grid, 4, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 1, "addItem 沿当前游标 (1,1)");
    sp1 = XSpacerItem_create(5, 5, XWidgetSizePolicy_Fixed,
                             XWidgetSizePolicy_Fixed);
    expect_true(sp1 != NULL, "grid 显式坐标 spacer 创建");
    XGridLayout_addItemAt(&grid, (XLayoutItem*)sp1, 3, 3, 1, 1, 0);
    ok = XGridLayout_getItemPosition(&grid, 5, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 3 && column == 3, "addItemAt 显式 (3,3)");
    sp2 = XSpacerItem_create(5, 5, XWidgetSizePolicy_Fixed,
                             XWidgetSizePolicy_Fixed);
    expect_true(sp2 != NULL, "grid 绕行 spacer 创建");
    XGridLayout_addItem(&grid, (XLayoutItem*)sp2);
    ok = XGridLayout_getItemPosition(&grid, 6, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 4 && column == 0, "游标绕行后 addItem → (4,0)");
    expect_true(XGridLayout_rowCount(&grid) == 5, "绕行后网格行数=5");
#endif /* XLAYOUT_SPACER_ON */
    XLayoutItem_deinit_base((XLayout*)&grid);
    for (i = 0; i < 4; ++i)
        XWidget_delete_base((XClass*)ws[i]);
#if XLAYOUT_SPACER_ON
    XLayoutItem_delete_base((XLayoutItem*)sp0);
    XLayoutItem_delete_base((XLayoutItem*)sp1);
    XLayoutItem_delete_base((XLayoutItem*)sp2);
#endif /* XLAYOUT_SPACER_ON */

    /* ---------- addWidgetSpan 推游标：跨格结束位 + 环绕 ---------- */
    XGridLayout_init(&grid);
    ws[0] = layout_make_fixed_widget(20, 10);
    expect_true(ws[0] != NULL, "grid span 推游标控件创建");
    XGridLayout_addWidgetSpan(&grid, &ws[0]->m_base, 0, 0, 1, 2, 0);
    expect_true(XGridLayout_rowCount(&grid) == 1 &&
                XGridLayout_columnCount(&grid) == 2, "span 后网格 1x2");
    ws[1] = layout_make_fixed_widget(10, 10);
    expect_true(ws[1] != NULL, "grid span 环绕控件创建");
    XGridLayout_addWidgetAuto(&grid, &ws[1]->m_base);
    ok = XGridLayout_getItemPosition(&grid, 1, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 0,
                "跨格结束后 addWidgetAuto → (1,0)");
    XLayoutItem_deinit_base((XLayout*)&grid);
    XWidget_delete_base((XClass*)ws[0]);
    XWidget_delete_base((XClass*)ws[1]);

    /* ---------- Vertical：先竖后横（对标 setDefaultPositioning Vertical） ---------- */
    XGridLayout_init(&grid);
    XGridLayout_setDefaultPositioning(&grid, 3, XOrientation_Vertical);
    expect_true(XGridLayout_rowCount(&grid) == 3 &&
                XGridLayout_columnCount(&grid) == 1,
                "setDefaultPositioning(3,V) → 3x1");
    for (i = 0; i < 4; ++i) {
        ws[i] = layout_make_fixed_widget(10, 10);
        expect_true(ws[i] != NULL, "grid 垂直默认定位控件创建");
        XGridLayout_addWidgetAuto(&grid, &ws[i]->m_base);
    }
    ok = XGridLayout_getItemPosition(&grid, 0, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 0 && column == 0, "垂直默认定位 #0=(0,0)");
    ok = XGridLayout_getItemPosition(&grid, 1, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 0, "垂直默认定位 #1=(1,0)");
    ok = XGridLayout_getItemPosition(&grid, 2, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 2 && column == 0, "垂直默认定位 #2=(2,0)");
    ok = XGridLayout_getItemPosition(&grid, 3, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 0 && column == 1, "垂直默认定位 #3=(0,1)（满行绕列）");
    expect_true(XGridLayout_rowCount(&grid) == 3 &&
                XGridLayout_columnCount(&grid) == 2, "垂直默认定位后网格 3x2");
    XLayoutItem_deinit_base((XLayout*)&grid);
    for (i = 0; i < 4; ++i)
        XWidget_delete_base((XClass*)ws[i]);
#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
}

/** @brief XGridLayout 原位替换：replaceWidget/ReplaceItemAt 保留单元格
 *  位置（对标 QGridLayoutPrivate::replaceAt / QLayout::replaceWidget）。 */
static void test_grid_layout_replace_item(void)
{
#if XLAYOUT_ON && XLAYOUT_GRID_ON
    XGridLayout grid;
    TestWidget* ws[2];
    XLayoutItem* old;
    XRect rect;
    XRect got;
    XSize size;
    int row;
    int column;
    int rowSpan;
    int columnSpan;
    bool ok;

    /* 跨格条目：替换后位置/跨度原样保留。 */
    XGridLayout_init(&grid);
    ws[0] = layout_make_fixed_widget(30, 20);
    ws[1] = layout_make_fixed_widget(40, 30);
    expect_true(ws[0] && ws[1], "grid 原位替换 测试控件创建");
    XGridLayout_addWidgetSpan(&grid, &ws[0]->m_base, 1, 2, 2, 1, 0);
    expect_true(XGridLayout_rowCount(&grid) == 3 &&
                XGridLayout_columnCount(&grid) == 3, "原位替换前网格 3x3");
    old = XLayout_replaceWidget((XLayout*)&grid, &ws[0]->m_base,
                                &ws[1]->m_base, false);
    expect_true(old != NULL, "replaceWidget 返回旧条目");
    ok = XGridLayout_getItemPosition(&grid, 0, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 2 && rowSpan == 2 &&
                columnSpan == 1, "原位替换保留 (1,2,2,1) 单元格");
    expect_true(XLayout_count_base((XLayout*)&grid) == 1,
                "原位替换后条目数不变");
    XLayoutItem_delete_base(old);   /* 旧条目所有权转移给调用方 */
    XLayoutItem_deinit_base((XLayout*)&grid);
    XWidget_delete_base((XClass*)ws[0]);
    XWidget_delete_base((XClass*)ws[1]);

    /* 单格条目：替换后新控件按首选尺寸填满原单元格。 */
    XGridLayout_init(&grid);
    ws[0] = layout_make_fixed_widget(30, 20);
    ws[1] = layout_make_fixed_widget(40, 30);
    expect_true(ws[0] && ws[1], "grid 原位替换(单格) 测试控件创建");
    XGridLayout_addWidget(&grid, &ws[0]->m_base, 1, 1, 0);
    old = XLayout_replaceWidget((XLayout*)&grid, &ws[0]->m_base,
                                &ws[1]->m_base, false);
    expect_true(old != NULL, "replaceWidget(单格) 返回旧条目");
    ok = XGridLayout_getItemPosition(&grid, 0, &row, &column,
                                     &rowSpan, &columnSpan);
    expect_true(ok && row == 1 && column == 1, "单格替换保留 (1,1)");
    XLayoutItem_delete_base(old);
    size = XLayoutItem_sizeHint_base((XLayoutItem*)&grid);
    expect_true(size.width == 40 && size.height == 30,
                "单格替换后 sizeHint=(40,30)");
    XRect_init(&rect, 0, 0, 40, 30);
    XLayoutItem_setGeometry_base((XLayoutItem*)&grid, &rect);
    layout_expect_rect(ws[1], 0, 0, 40, 30, "单格替换后新控件填满 (40,30)");
    got = XGridLayout_cellRect(&grid, 1, 1);
    expect_true(got.x == 0 && got.y == 0 && got.width == 40 &&
                got.height == 30, "cellRect(1,1)=(0,0,40,30)");
    XLayoutItem_deinit_base((XLayout*)&grid);
    XWidget_delete_base((XClass*)ws[0]);
    XWidget_delete_base((XClass*)ws[1]);
#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
}

/** @brief 布局挂控控件集成：setLayout/自动 reparent/show 激活/隐藏收缩。 */
static void test_layout_widget_integration(void)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    TestWidget* parent;
    TestWidget* c0;
    TestWidget* c1;
    XBoxLayout* laid;
    XLayoutItem* item;

    parent = TestWidget_create(NULL);
    c0 = layout_make_fixed_widget(40, 20);
    c1 = layout_make_fixed_widget(30, 20);
    expect_true(parent && c0 && c1, "layout 集成测试控件创建");
    XWidget_setGeometry(&parent->m_base, 50, 60, 300, 120);
    laid = XBoxLayout_create(XBoxLayoutDirection_LeftToRight,
                             &parent->m_base);
    expect_true(laid != NULL, "XBoxLayout_create(parent) 成功");
    expect_true(XWidget_layout(&parent->m_base) == (XLayout*)laid,
                "create(parent) 自动挂接 layout()");
    expect_true(XLayout_parentWidget((XLayout*)laid) == &parent->m_base,
                "parentWidget() 回读");
    XBoxLayout_addWidget(laid, &c0->m_base);
    XBoxLayout_addWidget(laid, &c1->m_base);
    expect_true(XWidget_parentWidget(&c0->m_base) == &parent->m_base &&
                XWidget_parentWidget(&c1->m_base) == &parent->m_base,
                "addWidget 自动 reparent 到宿主控件");
    XWidget_show(&parent->m_base);
    layout_expect_rect(c0, 76, 50, 40, 20, "集成 c0=(76,50,40,20)");
    layout_expect_rect(c1, 192, 50, 30, 20, "集成 c1=(192,50,30,20)");

    /* 隐藏子控件：条目变空，布局收缩重排。 */
    XWidget_hide(&c0->m_base);
    item = XLayout_itemAt_base((XLayout*)laid, 0);
    expect_true(item != NULL && XLayoutItem_isEmpty_base(item),
                "隐藏控件条目 isEmpty=true");
    expect_true(XLayout_activate((XLayout*)laid), "隐藏后 activate 成功");
    layout_expect_rect(c1, 135, 50, 30, 20, "隐藏后 c1=(135,50,30,20)");
    XWidget_show(&c0->m_base);
    expect_true(XLayout_activate((XLayout*)laid), "恢复后 activate 成功");
    layout_expect_rect(c0, 76, 50, 40, 20, "恢复后 c0=(76,50,40,20)");
    layout_expect_rect(c1, 192, 50, 30, 20, "恢复后 c1=(192,50,30,20)");

    XWidget_hide(&parent->m_base);
    XWidget_delete_base((XClass*)c0);
    XWidget_delete_base((XClass*)c1);
    XWidget_delete_base((XClass*)parent);
    XLayout_delete_base((XLayout*)laid);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
}

/** @brief XLayout 顶层合计契约：内容边距/启用开关/菜单栏/
 *  closestAcceptableSize（对标 Qt 6.8 QLayout 其余公开 API）。 */
static void test_layout_total_extended(void)
{
#if XLAYOUT_ON && XLAYOUT_TOTAL_ON && XLAYOUT_BOX_ON
    XBoxLayout box;
    XRect rect;
    XRect got;
    XSize size;
    XSize total;
    TestWidget* menu;
    TestWidget* w;
    int l;
    int t;
    int r;
    int b;

    /* ---------- contentsMargins 读写 / unset / contentsRect ---------- */
    XBoxLayout_init(&box, XBoxLayoutDirection_LeftToRight);
    XLayout_setContentsMargins((XLayout*)&box, 4, 5, 6, 7);
    l = t = r = b = 0;
    XLayout_getContentsMargins((XLayout*)&box, &l, &t, &r, &b);
    expect_true(l == 4 && t == 5 && r == 6 && b == 7,
                "setContentsMargins/getContentsMargins 读写一致");
    XRect_init(&rect, 10, 20, 100, 60);
    XLayoutItem_setGeometry_base((XLayoutItem*)&box, &rect);
    got = XLayout_contentsRect((XLayout*)&box);
    expect_true(got.x == 14 && got.y == 25 && got.width == 90 &&
                got.height == 48, "contentsRect=(14,25,90,48)");
    XLayout_unsetContentsMargins((XLayout*)&box);
    l = t = r = b = -99;
    XLayout_getContentsMargins((XLayout*)&box, &l, &t, &r, &b);
    expect_true(l == 0 && t == 0 && r == 0 && b == 0,
                "unsetContentsMargins 后全部解析为 0");
    got = XLayout_contentsRect((XLayout*)&box);
    expect_true(got.x == 10 && got.y == 20 && got.width == 100 &&
                got.height == 60, "unset 后 contentsRect=(10,20,100,60)");

    /* ---------- setEnabled / isEnabled ---------- */
    XLayout_setEnabled((XLayout*)&box, false);
    expect_true(!XLayout_isEnabled((XLayout*)&box), "setEnabled(false) 生效");
    XLayout_setEnabled((XLayout*)&box, true);
    expect_true(XLayout_isEnabled((XLayout*)&box), "setEnabled(true) 生效");

    /* ---------- 菜单栏：total* 把菜单栏高度附加到顶部 ---------- */
    menu = layout_make_fixed_widget(100, 25);
    expect_true(menu != NULL, "total 菜单栏测试控件创建");
    w = layout_make_fixed_widget(40, 30);
    expect_true(w != NULL, "total 布局子控件创建");
    XBoxLayout_addWidget(&box, &w->m_base);
    XLayout_setMenuBar((XLayout*)&box, &menu->m_base);
    expect_true(XLayout_menuBar((XLayout*)&box) == &menu->m_base,
                "setMenuBar/menuBar 回读");
    size = XLayoutItem_sizeHint_base((XLayoutItem*)&box);
    total = XLayout_totalSizeHint((XLayout*)&box);
    expect_true(total.height == size.height + 25,
                "totalSizeHint.height=sizeHint+25");
    size = XLayoutItem_minimumSize_base((XLayoutItem*)&box);
    total = XLayout_totalMinimumSize((XLayout*)&box);
    expect_true(total.height == size.height + 25,
                "totalMinimumSize.height=min+25");
    expect_true(XLayout_totalHeightForWidth((XLayout*)&box, 123) == -1 + 25,
                "totalHeightForWidth=(-1)+25（无 hfw 子条目按 Qt 返回 -1）");
    expect_true(XLayout_totalMinimumHeightForWidth((XLayout*)&box, 123) ==
                    -1 + 25,
                "totalMinimumHeightForWidth=(-1)+25");

    XLayoutItem_deinit_base((XLayout*)&box);
    XWidget_delete_base((XClass*)w);
    XWidget_delete_base((XClass*)menu);

    /* ---------- closestAcceptableSize（对标 QLayout 静态接口） ---------- */
    w = layout_make_growable_widget(40, 20);
    expect_true(w != NULL, "closest 可伸缩控件创建");
    XSize_init(&size, 100, 50);
    size = XLayout_closestAcceptableSize(&w->m_base, size);
    expect_true(size.width == 100 && size.height == 50,
                "closest(100,50) 可伸缩 → (100,50)");
    XSize_init(&size, 10, 10);
    size = XLayout_closestAcceptableSize(&w->m_base, size);
    expect_true(size.width == 40 && size.height == 20,
                "closest(10,10) 可伸缩 → 钳到最小 (40,20)");
    XWidget_delete_base((XClass*)w);
    w = layout_make_fixed_widget(30, 30);
    expect_true(w != NULL, "closest 固定尺寸控件创建");
    XSize_init(&size, 200, 200);
    size = XLayout_closestAcceptableSize(&w->m_base, size);
    expect_true(size.width == 30 && size.height == 30,
                "closest(200,200) 固定 → (30,30)");
    XWidget_delete_base((XClass*)w);
#endif /* XLAYOUT_ON && XLAYOUT_TOTAL_ON && XLAYOUT_BOX_ON */
}

#if XLAYOUT_STACKED_ON
#if XWIDGET_ON && XPUSHBUTTON_ON && XFRAME_ON && XLABEL_ON
/** @brief 堆叠布局按钮联动探针：把 clicked 槽转换为下一页索引。 */
typedef struct StackedButtonProbe
{
    XLabel receiver; /**< 作为信号槽接收者的 XLabel 基类对象。 */
    XStackedLayout* stack; /**< 被槽函数切换的堆叠布局借用指针。 */
    int clicked; /**< 已接收 clicked 信号次数。 */
} StackedButtonProbe;

static void stacked_button_next_clickedSlot(XObject* receiver,
                                            XVarList* args)
{
    StackedButtonProbe* probe = (StackedButtonProbe*)receiver;
    int index;
    int count;
    (void)args;
    if (!probe || !probe->stack) return;
    index = XStackedLayout_currentIndex(probe->stack);
    count = XStackedLayout_count(probe->stack);
    if (count <= 0) return;
    XStackedLayout_setCurrentIndex(probe->stack, (index + 1) % count);
    ++probe->clicked;
}
#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XFRAME_ON && XLABEL_ON */

/** @brief XStackedLayout 契约：页面索引、可见性、几何分配与移除。 */
static void test_stacked_layout_contract(void)
{
    XStackedLayout stack;
    TestWidget* first;
    TestWidget* second;
    XRect rect;
    XLayoutItem* taken;

    /* 页面允许扩展，几何断言才能覆盖 QStackedLayout 的整矩形分配。 */
    first = layout_make_growable_widget(40, 24);
    second = layout_make_growable_widget(64, 32);
    expect_true(first != NULL && second != NULL,
                "stacked layout 页面控件创建");
    if (!first || !second) return;

    XWidget_setGeometry((XWidget*)g_layoutTestHost, 0, 0, 240, 120);
    XWidget_show((XWidget*)g_layoutTestHost);
    XStackedLayout_init(&stack);
    expect_true(XStackedLayout_currentIndex(&stack) == -1,
                "stacked layout 初始 currentIndex=-1");
    expect_true(XStackedLayout_addWidget(&stack, &first->m_base) == 0,
                "stacked layout addWidget 返回 0");
    expect_true(XStackedLayout_addWidget(&stack, &second->m_base) == 1,
                "stacked layout addWidget 返回 1");
    expect_true(XStackedLayout_count(&stack) == 2 &&
                XStackedLayout_currentWidget(&stack) == &first->m_base,
                "stacked layout 当前页为首项");
    expect_true(XWidget_isVisible(&first->m_base) &&
                !XWidget_isVisible(&second->m_base),
                "StackOne 仅首项可见");

    XRect_init(&rect, 5, 7, 180, 90);
    XLayoutItem_setGeometry_base((XLayoutItem*)&stack, &rect);
    expect_true(XWidget_geometry(&first->m_base).x == 5 &&
                XWidget_geometry(&first->m_base).y == 7 &&
                XWidget_geometry(&first->m_base).width == 180 &&
                XWidget_geometry(&first->m_base).height == 90,
                "StackOne 几何分配给当前页");
    expect_true(XWidget_geometry(&second->m_base).width != 180 ||
                XWidget_geometry(&second->m_base).height != 90,
                "StackOne 不改非当前页几何");

#if XWIDGET_ON && XPUSHBUTTON_ON && XFRAME_ON && XLABEL_ON
    {
        XPushButton nextButton;
        StackedButtonProbe probe;
        memset(&probe, 0, sizeof(probe));
        XPushButton_init(&nextButton, NULL, 0);
        XLabel_init(&probe.receiver, NULL, 0);
        probe.stack = &stack;
        XObject_connect_1((XObject*)&nextButton,
                          (size_t)XPushButton_clicked_signal(NULL, false),
                          (XObject*)&probe.receiver,
                          stacked_button_next_clickedSlot,
                          XConnectionType_Direct);
        XPushButton_click(&nextButton);
        expect_true(probe.clicked == 1 &&
                    XStackedLayout_currentIndex(&stack) == 1 &&
                    XStackedLayout_currentWidget(&stack) == &second->m_base,
                    "XPushButton clicked 槽联动 XStackedLayout 切到下一页");
        XLabel_deinit_base(&probe.receiver);
        XPushButton_deinit_base(&nextButton);
    }
#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XFRAME_ON && XLABEL_ON */

    XStackedLayout_setCurrentIndex(&stack, 1);
    expect_true(XStackedLayout_currentIndex(&stack) == 1 &&
                XStackedLayout_currentWidget(&stack) == &second->m_base,
                "setCurrentIndex 切换当前页");
    expect_true(!XWidget_isVisible(&first->m_base) &&
                XWidget_isVisible(&second->m_base),
                "切换后 StackOne 可见性更新");

    /* 让当前页拥有非空几何，同时故意改变非当前页，验证 StackAll
     * 以 currentWidget()->geometry() 覆盖全部页面。 */
    XWidget_setGeometry(&first->m_base, 1, 2, 11, 12);
    XWidget_setGeometry(&second->m_base, 5, 7, 180, 90);
    XStackedLayout_setStackingMode(&stack, XStackedLayoutStackAll);
    expect_true(XStackedLayout_stackingMode(&stack) ==
                    XStackedLayoutStackAll,
                "setStackingMode(StackAll) 生效");
    expect_true(XWidget_isVisible(&first->m_base) &&
                XWidget_isVisible(&second->m_base),
                "StackAll 所有页面可见");
    expect_true(XWidget_geometry(&first->m_base).x == 5 &&
                XWidget_geometry(&first->m_base).y == 7 &&
                XWidget_geometry(&first->m_base).width == 180 &&
                XWidget_geometry(&first->m_base).height == 90,
                "StackAll 同步当前页几何到其他页面");

    taken = XLayout_takeAt_base((XLayout*)&stack, 1);
    expect_true(taken != NULL && XStackedLayout_count(&stack) == 1 &&
                XStackedLayout_currentIndex(&stack) == 0 &&
                XStackedLayout_currentWidget(&stack) == &first->m_base,
                "移除当前页后索引回退到剩余页面");
    if (taken) XLayoutItem_delete_base(taken);
    XStackedLayout_deinit_base(&stack);
}
#endif /* XLAYOUT_STACKED_ON */


#endif /* XLAYOUT_ON */

#endif /* XWIDGET_ON */

#if XPLATFORMINTEGRATION_ON
static void test_gpu_contract(void)
{
    XGpu* gpu;
    XGpuAdapterInfo info;
    if (!XPlatformGraphics_isVulkanAvailable()) {
        gpu = XGpu_create(XGpuBackend_Vulkan, NULL);
        expect_true(gpu == NULL, "无 Vulkan 驱动时统一 XGpu 创建安全失败");
        return;
    }
    gpu = XGpu_create(XGpuBackend_Vulkan, NULL);
    info = XGpu_adapterInfo(gpu);
    expect_true(gpu != NULL && XGpu_isValid(gpu) &&
                XGpu_backend(gpu) == XGpuBackend_Vulkan &&
                info.m_backend == XGpuBackend_Vulkan &&
                info.m_supportsExplicitCommands,
                "XGpu 统一入口创建真实 Vulkan 实例");
    XGpu_destroy(gpu);
}
#endif /* XPLATFORMINTEGRATION_ON */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
typedef struct LabelProbe
{
    int activated;
    int hovered;
    const char* lastLink;
    char linkBuffer[96];
} LabelProbe;

static LabelProbe g_labelProbe;

static void label_probe_activatedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XString*, link);
    ++g_labelProbe.activated;
    if (link && XString_toUtf8(link)) {
        size_t n = XString_toUtf8_length(link);
        if (n >= sizeof(g_labelProbe.linkBuffer))
            n = sizeof(g_labelProbe.linkBuffer) - 1;
        memcpy(g_labelProbe.linkBuffer, XString_toUtf8(link), n);
        g_labelProbe.linkBuffer[n] = '\0';
        g_labelProbe.lastLink = g_labelProbe.linkBuffer;
    } else {
        g_labelProbe.lastLink = NULL;
    }
}

static void label_probe_hoveredSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XString*, link);
    ++g_labelProbe.hovered;
    if (link && XString_toUtf8(link)) {
        size_t n = XString_toUtf8_length(link);
        if (n >= sizeof(g_labelProbe.linkBuffer))
            n = sizeof(g_labelProbe.linkBuffer) - 1;
        memcpy(g_labelProbe.linkBuffer, XString_toUtf8(link), n);
        g_labelProbe.linkBuffer[n] = '\0';
        g_labelProbe.lastLink = g_labelProbe.linkBuffer;
    } else {
        g_labelProbe.lastLink = NULL;
    }
}

/** @brief XLabel 控件的 Qt 对齐契约测试。 */
static void test_label_contract(void)
{
    XLabel label;
    XLabel* zero;
    const XString* text;
    XString* sel;
    XPixmap pm;
    XPixmap got;
    XPixmap empty;
    XPicture* pic;
    XPicture* gotPic;
    XMovie movie;
    XSize size;
    XImage image;
    XImage scaledImage;
    XPainter painter;
    XRect drawRect;
    XSize normalTextSize;
    XSize scaledTextSize;
    size_t normalTextPixels;
    size_t scaledTextPixels;

    memset(&label, 0, sizeof(label));
    XLabel_init(&label, NULL, 0);
    expect_true(XLabel_text(&label) == NULL ||
                XString_toUtf8_length(XLabel_text(&label)) == 0,
                "XLabel 默认文本为空");
    expect_true(XLabel_textFormat(&label) == XLabelTextFormat_AutoText,
                "XLabel 默认 AutoText");
    expect_true(!XLabel_wordWrap(&label), "XLabel 默认不换行");
    expect_true(XLabel_indent(&label) == -1, "XLabel 默认 indent=-1");
    expect_true(XLabel_margin(&label) == 0, "XLabel 默认 margin=0");
    expect_true(!XLabel_hasScaledContents(&label),
                "XLabel 默认不缩放内容");
    expect_true(XLabel_buddy(&label) == NULL, "XLabel 默认无伙伴");
    expect_true(!XLabel_openExternalLinks(&label),
                "XLabel 默认不打开外链");
    expect_true((XLabel_textInteractionFlags(&label) &
                 XLabelTextInteraction_LinksAccessibleByMouse) != 0,
                "XLabel 默认鼠标可访问链接");
    expect_true(!XLabel_hasSelectedText(&label),
                "XLabel 默认无选择文本");
    expect_true(XLabel_selectionStart(&label) == -1,
                "XLabel 默认选择起点 -1");
    size = XLabel_sizeHint(&label);
    expect_true(size.width >= 0 && size.height >= 0,
                "XLabel 空文本 sizeHint 有效");
    size = XLabel_minimumSizeHint(&label);
    expect_true(size.width >= 0 && size.height >= 0,
                "XLabel 空文本 minimumSizeHint 有效");

    XLabel_setText_2(&label, "Hello");
    text = XLabel_text(&label);
    expect_true(text != NULL &&
                strcmp(XString_toUtf8(text), "Hello") == 0,
                "XLabel_setText_2 文本写入");
    expect_true(XLabel_heightForWidth(&label, 64) > 0,
                "XLabel 文本 heightForWidth>0");
    XLabel_setNum(&label, 42);
    text = XLabel_text(&label);
    expect_true(text != NULL &&
                strcmp(XString_toUtf8(text), "42") == 0,
                "XLabel_setNum(int) 十进制文本");
    XLabel_setNum_2(&label, 1.5);
    text = XLabel_text(&label);
    expect_true(text != NULL &&
                strcmp(XString_toUtf8(text), "1.5") == 0,
                "XLabel_setNum(double) %g 文本");

    XLabel_setTextFormat(&label, XLabelTextFormat_PlainText);
    expect_true(XLabel_textFormat(&label) == XLabelTextFormat_PlainText,
                "XLabel setTextFormat 读写");
    XLabel_setWordWrap(&label, true);
    expect_true(XLabel_wordWrap(&label), "XLabel setWordWrap true");
    XLabel_setAlignment(&label, 0);
    expect_true(XLabel_alignment(&label) == 0, "XLabel setAlignment 0");
    XLabel_setAlignment(&label, XAlignment_Right | XAlignment_Bottom |
                                   XAlignment_Absolute);
    expect_true(XLabel_alignment(&label) ==
                    (XAlignment_Right | XAlignment_Bottom |
                     XAlignment_Absolute),
                "XLabel alignment 保留 Qt AlignAbsolute 标志");
    XLabel_setAlignment(&label, XAlignment_Left | XAlignment_VCenter);
    XLabel_setIndent(&label, 2);
    expect_true(XLabel_indent(&label) == 2, "XLabel setIndent 2");
    XLabel_setMargin(&label, 3);
    expect_true(XLabel_margin(&label) == 3, "XLabel setMargin 3");
    XLabel_setScaledContents(&label, true);
    expect_true(XLabel_hasScaledContents(&label),
                "XLabel setScaledContents true");
    XLabel_setOpenExternalLinks(&label, true);
    expect_true(XLabel_openExternalLinks(&label),
                "XLabel setOpenExternalLinks true");
    XLabel_setTextInteractionFlags(&label,
        XLabelTextInteraction_TextSelectableByMouse);
    expect_true((XLabel_textInteractionFlags(&label) &
                 XLabelTextInteraction_TextSelectableByMouse) != 0,
                "XLabel 文本可选交互标志");

    XLabel_setSelection(&label, 1, 3);
    expect_true(XLabel_hasSelectedText(&label),
                "XLabel setSelection 产生选择");
    expect_true(XLabel_selectionStart(&label) == 1,
                "XLabel selectionStart 读取");
    sel = XLabel_selectedText(&label);
    expect_true(sel != NULL, "XLabel selectedText 非空");
    if (sel) {
        expect_true(strcmp(XString_toUtf8(sel), ".5") == 0,
                    "XLabel selectedText 按 UTF-16 区间截取");
        XString_delete_base((XClass*)sel);
    }
    XLabel_setTextInteractionFlags(&label,
        XLabelTextInteraction_NoTextInteraction);
    expect_true(!XLabel_hasSelectedText(&label) &&
                XLabel_selectionStart(&label) == -1,
                "XLabel 关闭文本交互后清除选择");
    XLabel_setSelection(&label, 0, 1);
    expect_true(!XLabel_hasSelectedText(&label),
                "XLabel 无文本选择交互时 setSelection 无操作");
    XLabel_setText_2(&label, "AB");
    XLabel_setTextInteractionFlags(&label,
        XLabelTextInteraction_LinksAccessibleByKeyboard);
    expect_true(XWidget_focusPolicy((XWidget*)&label) ==
                    XWidgetFocusPolicy_StrongFocus,
                "XLabel 键盘链接交互设置 StrongFocus");
    XLabel_setSelection(&label, 0, 1);
    expect_true(XLabel_hasSelectedText(&label) &&
                    XLabel_selectionStart(&label) == 0,
                "XLabel 非可选标志但非 NoFocus 时允许程序化选择");
    XLabel_setTextInteractionFlags(&label,
        XLabelTextInteraction_TextSelectableByMouse);
    XLabel_setSelection(&label, 1, 1);
    expect_true(XLabel_hasSelectedText(&label),
                "XLabel 恢复可选交互后可设置选择");
    XLabel_setSelection(&label, -1, -1);
    expect_true(!XLabel_hasSelectedText(&label),
                "XLabel setSelection(-1,-1) 清除选择");

    /* Rich text creates QLabel's text control independently of selection
       flags, so programmatic selection remains available in NoInteraction. */
    XLabel_setTextFormat(&label, XLabelTextFormat_RichText);
    XLabel_setText_2(&label, "<b>AB</b>");
    XLabel_setTextInteractionFlags(&label,
        XLabelTextInteraction_NoTextInteraction);
    XLabel_setSelection(&label, 0, 1);
    expect_true(XLabel_hasSelectedText(&label),
                "XLabel 富文本无交互标志仍可程序化选择");
    XLabel_setSelection(&label, -1, -1);
    XLabel_setTextFormat(&label, XLabelTextFormat_PlainText);

    /* 链接命中测试必须随点阵字号缩放：Qt 文本控件按实际行高和字宽
       命中，不能固定使用默认 16/8 像素。 */
    {
        XLabel scaledLink;
        XMouseEvent press;
        XMouseEvent release;
        XPoint click = { 16, 16 };
        memset(&scaledLink, 0, sizeof(scaledLink));
        XLabel_init(&scaledLink, NULL, 0);
        XLabel_setTextFormat(&scaledLink, XLabelTextFormat_RichText);
        XLabel_setText_2(&scaledLink,
                         "<a href=\"https://scaled.example\">AB</a>");
        XLabel_setAlignment(&scaledLink, XAlignment_Left | XAlignment_VCenter);
        XLabel_setMargin(&scaledLink, 0);
        XLabel_setIndent(&scaledLink, 0);
        XWidget_resize((XWidget*)&scaledLink, 40, 40);
        XLabel_setTextPixelSize(&scaledLink, 32);
        memset(&g_labelProbe, 0, sizeof(g_labelProbe));
        XObject_connect_2((XObject*)&scaledLink,
                          (size_t)XLabel_linkActivated_signal(&scaledLink, NULL),
                          label_probe_activatedSlot);
        XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton,
                         XKeyboardModifier_NoModifier, click);
        XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton,
                         XKeyboardModifier_NoModifier, click);
        XWidget_event_base((XWidget*)&scaledLink, (XEvent*)&press);
        XWidget_event_base((XWidget*)&scaledLink, (XEvent*)&release);
        expect_true(g_labelProbe.activated == 1 &&
                    g_labelProbe.lastLink != NULL &&
                    strcmp(g_labelProbe.lastLink, "https://scaled.example") == 0,
                    "XLabel 缩放字号链接命中按实际行高/字宽计算");
        XMouseEvent_deinit_base((XClass*)&press);
        XMouseEvent_deinit_base((XClass*)&release);
        XLabel_deinit_base(&scaledLink);
    }

    XPixmap_init_ex(&pm, 5, 4);
    XLabel_setPixmap(&label, &pm);
    got = XLabel_pixmap(&label);
    expect_true(XPixmap_width(&got) == 5 && XPixmap_height(&got) == 4,
                "XLabel setPixmap/pixmap 尺寸往返");
    XPixmap_deinit_base(&got);
    XLabel_clear(&label);
    empty = XLabel_pixmap(&label);
    expect_true(XPixmap_isNull(&empty), "XLabel clear 清空像素图");
    XPixmap_deinit_base(&empty);
    XPixmap_deinit_base(&pm);

    pic = XPicture_create();
    expect_true(pic != NULL, "XLabel 测试绘图记录创建");
    if (pic) {
        XRect picRect = { 0, 0, 1, 1 };
        XPicture_recordFillRect(pic, &picRect, 0xff3070ffu);
    }
    XLabel_setPicture(&label, pic);
    gotPic = XLabel_picture(&label);
    expect_true(gotPic != NULL, "XLabel setPicture/picture 往返");
    XLabel_clear(&label);
    gotPic = XLabel_picture(&label);
    expect_true(gotPic == NULL, "XLabel clear 清空绘图记录");
    XPicture_delete_base((XClass*)pic);

    /* Qt clears label contents on every setMovie call, including reusing the
       same borrowed movie pointer. */
    XMovie_init(&movie);
    XLabel_setText_2(&label, "movie-old-text");
    XLabel_setMovie(&label, &movie);
    expect_true(XLabel_movie(&label) == &movie &&
                XLabel_text(&label) != NULL &&
                XString_toUtf8_length(XLabel_text(&label)) == 0,
                "XLabel setMovie 清空旧文本");
    XLabel_setText_2(&label, "movie-new-text");
    XLabel_setMovie(&label, &movie);
    expect_true(XLabel_movie(&label) == &movie &&
                XString_toUtf8_length(XLabel_text(&label)) == 0,
                "XLabel 重设同一 movie 仍清空内容");
    XMovie_deinit_base(&movie);

    XLabel_setText_2(&label, "A");
    XWidget_resize((XWidget*)&label, 24, 16);
    XImage_init_ex(&image, 24, 16, XImageFormat_ARGB32);
    XImage_fill(&image, 0xFFFFFFFFu);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "XLabel 绘制测试绑定图像");
    XLabel_drawContents(&label, &painter);
    expect_true(XPainter_end(&painter), "XLabel 绘制测试结束");
    XPainter_deinit(&painter);
    drawRect.x = 1; drawRect.y = 1; drawRect.width = 22; drawRect.height = 14;
    expect_true(image_count_non_background(&image, 0xFFFFFFFFu) > 0,
                "XLabel 离屏绘制产生非背景像素");
    XImage_deinit_base(&image);

    /* 文字模式也必须使用 XFont 的整数倍缩放，而非只缩放像素图内容。 */
    XLabel_setText_2(&label, "Scale");
    XLabel_setAlignment(&label, XAlignment_Left | XAlignment_Top);
    XLabel_setMargin(&label, 0);
    XLabel_setIndent(&label, 0);
    XLabel_setWordWrap(&label, false);
    XWidget_resize((XWidget*)&label, 96, 64);
    XLabel_setTextPixelSize(&label, 16);
    normalTextSize = XLabel_sizeHint(&label);
    XImage_init_ex(&image, 96, 64, XImageFormat_ARGB32);
    XImage_fill(&image, 0xFFFFFFFFu);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "XLabel 默认字号绘制绑定图像");
    XLabel_drawContents(&label, &painter);
    expect_true(XPainter_end(&painter), "XLabel 默认字号绘制结束");
    XPainter_deinit(&painter);
    normalTextPixels = image_count_non_background(&image, 0xFFFFFFFFu);
    expect_true(normalTextPixels > 0,
                "XLabel 默认字号实际显示文字");

    XLabel_setTextPixelSize(&label, 32);
    expect_true(XLabel_textPixelSize(&label) == 32,
                "XLabel 文字缩放字号读回");
    scaledTextSize = XLabel_sizeHint(&label);
    expect_true(scaledTextSize.width > normalTextSize.width &&
                scaledTextSize.height > normalTextSize.height,
                "XLabel 放大文字尺寸提示同步增长");
    XImage_init_ex(&scaledImage, 96, 64, XImageFormat_ARGB32);
    XImage_fill(&scaledImage, 0xFFFFFFFFu);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &scaledImage),
                "XLabel 放大字号绘制绑定图像");
    XLabel_drawContents(&label, &painter);
    expect_true(XPainter_end(&painter), "XLabel 放大字号绘制结束");
    XPainter_deinit(&painter);
    scaledTextPixels = image_count_non_background(&scaledImage, 0xFFFFFFFFu);
    expect_true(scaledTextPixels > normalTextPixels,
                "XLabel 放大字号实际文字像素增长");
    XImage_deinit_base(&scaledImage);
    XImage_deinit_base(&image);

    /* Qt::AlignLeft/Right are logical flags: in RTL they are converted by
       QStyle::visualAlignment unless AlignAbsolute is present. */
    {
        XLabel directionLabel;
        XImage directionImage;
        int ltrMinX = -1;
        int rtlMinX = -1;
        memset(&directionLabel, 0, sizeof(directionLabel));
        XLabel_init(&directionLabel, NULL, 0);
        XLabel_setText_2(&directionLabel, "A");
        XLabel_setAlignment(&directionLabel, XAlignment_Left | XAlignment_Top);
        XLabel_setMargin(&directionLabel, 0);
        XLabel_setIndent(&directionLabel, 0);
        XWidget_resize((XWidget*)&directionLabel, 64, 32);
        XLabel_setTextPixelSize(&directionLabel, 16);

        XImage_init_ex(&directionImage, 64, 32, XImageFormat_ARGB32);
        XImage_fill(&directionImage, 0xFFFFFFFFu);
        XPainter_init(&painter, NULL);
        expect_true(XPainter_begin_image(&painter, &directionImage),
                    "XLabel LTR 方向绘制绑定");
        XLabel_drawContents(&directionLabel, &painter);
        expect_true(XPainter_end(&painter), "XLabel LTR 方向绘制结束");
        XPainter_deinit(&painter);
        image_non_background_x_bounds(&directionImage, 0xFFFFFFFFu,
                                      &ltrMinX, NULL);
        XImage_deinit_base(&directionImage);

        XWidget_setLayoutDirection((XWidget*)&directionLabel,
                                    XWidgetLayoutDirection_RightToLeft);
        XImage_init_ex(&directionImage, 64, 32, XImageFormat_ARGB32);
        XImage_fill(&directionImage, 0xFFFFFFFFu);
        XPainter_init(&painter, NULL);
        expect_true(XPainter_begin_image(&painter, &directionImage),
                    "XLabel RTL 方向绘制绑定");
        XLabel_drawContents(&directionLabel, &painter);
        expect_true(XPainter_end(&painter), "XLabel RTL 方向绘制结束");
        XPainter_deinit(&painter);
        image_non_background_x_bounds(&directionImage, 0xFFFFFFFFu,
                                      &rtlMinX, NULL);
        expect_true(ltrMinX >= 0 && rtlMinX > ltrMinX,
                    "XLabel RTL 布局方向转换 AlignLeft");
        XImage_deinit_base(&directionImage);
        XLabel_deinit_base(&directionLabel);
    }

    memset(&g_labelProbe, 0, sizeof(g_labelProbe));
    zero = XLabel_create(NULL, 0);
    expect_true(zero != NULL, "XLabel_create 创建信号探针对象");
    XObject_connect_2((XObject*)zero,
                      (size_t)XLabel_linkActivated_signal(zero, NULL),
                      label_probe_activatedSlot);
    XObject_connect_2((XObject*)zero,
                      (size_t)XLabel_linkHovered_signal(zero, NULL),
                      label_probe_hoveredSlot);
    {
        XString* linkText = XString_create_utf8("https://example.test");
        XLabel_linkActivated_signal(zero, linkText);
        expect_true(g_labelProbe.activated == 1 &&
                    g_labelProbe.lastLink != NULL &&
                    strcmp(g_labelProbe.lastLink, "https://example.test") == 0,
                    "XLabel linkActivated 信号参数");
        XString_delete_base((XClass*)linkText);
    }
    XLabel_delete_base(zero);

    XLabel_deinit_base(&label);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#if XWIDGET_ON && XPUSHBUTTON_ON
typedef struct ButtonProbe
{
    int pressed;
    int released;
    int clicked;
    int toggled;
    bool lastClicked;
    bool lastToggled;
} ButtonProbe;

static ButtonProbe g_buttonProbe;

static void button_probe_pressedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_buttonProbe.pressed;
}

static void button_probe_releasedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    ++g_buttonProbe.released;
}

static void button_probe_clickedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, checked);
    ++g_buttonProbe.clicked;
    g_buttonProbe.lastClicked = checked;
}

static void button_probe_toggledSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, bool, checked);
    ++g_buttonProbe.toggled;
    g_buttonProbe.lastToggled = checked;
}

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
typedef struct ButtonLinkProbe
{
    XLabel label;
    int pressed;
    int released;
} ButtonLinkProbe;

static void button_link_pressedSlot(XObject* receiver, XVarList* args)
{
    ButtonLinkProbe* probe = (ButtonLinkProbe*)receiver;
    (void)args;
    if (!probe) return;
    ++probe->pressed;
    XLabel_setText_2(&probe->label, "Pressed");
}

static void button_link_releasedSlot(XObject* receiver, XVarList* args)
{
    ButtonLinkProbe* probe = (ButtonLinkProbe*)receiver;
    (void)args;
    if (!probe) return;
    ++probe->released;
    XLabel_setText_2(&probe->label, "Released");
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/** @brief XPushButton 按钮的 Qt 对齐契约测试。 */
static void test_pushbutton_contract(void)
{
    XPushButton button;
    XPushButton* zero;
    XPixmap pm;
    XIcon icon;
    XIcon gotIcon;
    XSize size;
    XPoint pos;
    XImage image;
    XPainter painter;
    XMenu* menu;

    memset(&button, 0, sizeof(button));
    XPushButton_init(&button, NULL, 0);
    expect_true(XPushButton_text(&button) == NULL ||
                XString_toUtf8_length(XPushButton_text(&button)) == 0,
                "XPushButton 默认文本为空");
    gotIcon = XPushButton_icon(&button);
    expect_true(XIcon_isNull(&gotIcon), "XPushButton 默认图标为空");
    XIcon_deinit_base(&gotIcon);
    expect_true(!XPushButton_isCheckable(&button),
                "XPushButton 默认不可选中");
    expect_true(!XPushButton_isChecked(&button),
                "XPushButton 默认未选中");
    expect_true(!XPushButton_isDown(&button), "XPushButton 默认未按下");
    expect_true(!XPushButton_autoRepeat(&button),
                "XPushButton 默认无自动重复");
    expect_true(XPushButton_autoRepeatDelay(&button) == 300,
                "XPushButton 自动重复延迟默认 300");
    expect_true(XPushButton_autoRepeatInterval(&button) == 100,
                "XPushButton 自动重复间隔默认 100");
    expect_true(!XPushButton_autoExclusive(&button),
                "XPushButton 默认非自动互斥");
    expect_true(!XPushButton_isDefault(&button),
                "XPushButton 默认非默认按钮");
    expect_true(!XPushButton_isFlat(&button), "XPushButton 默认非扁平");
    expect_true(XPushButton_menu(&button) == NULL,
                "XPushButton 默认无菜单");
    expect_true(XWidget_focusPolicy((XWidget*)&button) ==
                    XWidgetFocusPolicy_StrongFocus,
                "XPushButton 默认 StrongFocus");
    size = XPushButton_sizeHint(&button);
    expect_true(size.width >= 0 && size.height >= 0,
                "XPushButton sizeHint 有效");
    size = XPushButton_minimumSizeHint(&button);
    expect_true(size.width >= 0 && size.height >= 0,
                "XPushButton minimumSizeHint 有效");

    XPushButton_setText_2(&button, "OK");
    expect_true(strcmp(XString_toUtf8(XPushButton_text(&button)), "OK") == 0,
                "XPushButton setText_2 写入");
    XPushButton_setAutoRepeat(&button, true);
    expect_true(XPushButton_autoRepeat(&button),
                "XPushButton setAutoRepeat true");
    XPushButton_setAutoRepeatDelay(&button, 250);
    XPushButton_setAutoRepeatInterval(&button, 80);
    expect_true(XPushButton_autoRepeatDelay(&button) == 250 &&
                XPushButton_autoRepeatInterval(&button) == 80,
                "XPushButton 自动重复参数");
    XPushButton_setAutoExclusive(&button, true);
    expect_true(XPushButton_autoExclusive(&button),
                "XPushButton setAutoExclusive true");

    XPixmap_init_ex(&pm, 3, 2);
    XIcon_init_pixmap(&icon, &pm);
    XPushButton_setIcon(&button, &icon);
    gotIcon = XPushButton_icon(&button);
    expect_true(!XIcon_isNull(&gotIcon), "XPushButton 设置图标非空");
    XIcon_deinit_base(&gotIcon);
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&pm);

    zero = XPushButton_create(NULL, 0);
    expect_true(zero != NULL, "XPushButton_create 创建");
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XObject_connect_2((XObject*)zero,
                      (size_t)XPushButton_pressed_signal(zero),
                      button_probe_pressedSlot);
    XObject_connect_2((XObject*)zero,
                      (size_t)XPushButton_released_signal(zero),
                      button_probe_releasedSlot);
    XObject_connect_2((XObject*)zero,
                      (size_t)XPushButton_clicked_signal(zero, false),
                      button_probe_clickedSlot);
    XObject_connect_2((XObject*)zero,
                      (size_t)XPushButton_toggled_signal(zero, false),
                      button_probe_toggledSlot);

    XPushButton_setChecked(zero, true);
    expect_true(!XPushButton_isChecked(zero),
                "非可选中按钮 setChecked(true) 不生效");
    XPushButton_toggle(zero);
    expect_true(!XPushButton_isChecked(zero),
                "非可选中按钮 toggle 不生效");
    XPushButton_setCheckable(zero, true);
    XPushButton_toggle(zero);
    expect_true(XPushButton_isChecked(zero) &&
                g_buttonProbe.toggled == 1 &&
                g_buttonProbe.lastToggled,
                "XPushButton checkable+toggle 发 toggled");
    XPushButton_toggle(zero);
    expect_true(!XPushButton_isChecked(zero) &&
                g_buttonProbe.toggled == 2 &&
                !g_buttonProbe.lastToggled,
                "XPushButton toggle 反向切换");

    XPushButton_setCheckable(zero, false);
    XPushButton_click(zero);
    expect_true(g_buttonProbe.pressed == 1 &&
                g_buttonProbe.released == 1 &&
                g_buttonProbe.clicked == 1 &&
                !g_buttonProbe.lastClicked &&
                g_buttonProbe.toggled == 2,
                "XPushButton 非可选中 click 只发基本信号");
    XPushButton_setCheckable(zero, true);
    XPushButton_click(zero);
    expect_true(XPushButton_isChecked(zero) &&
                g_buttonProbe.clicked == 2 &&
                g_buttonProbe.lastClicked &&
                g_buttonProbe.toggled == 3 &&
                g_buttonProbe.lastToggled,
                "XPushButton 可选中 click 换选中并发 full 信号");

    /* Qt setCheckable(false) 会静默清除 checked，不额外发 toggled。 */
    {
        int toggledBefore = g_buttonProbe.toggled;
        XPushButton_setCheckable(zero, false);
        expect_true(!XPushButton_isChecked(zero) &&
                    g_buttonProbe.toggled == toggledBefore,
                    "XPushButton 关闭 checkable 静默清除 checked");
    }
    XPushButton_setAutoRepeatDelay(zero, -5);
    XPushButton_setAutoRepeatInterval(zero, -7);
    expect_true(XPushButton_autoRepeatDelay(zero) == -5 &&
                XPushButton_autoRepeatInterval(zero) == -7,
                "XPushButton 自动重复参数保留负值");

    XWidget_resize((XWidget*)zero, 80, 30);
    XPoint_init(&pos, 40, 15);
    expect_true(XPushButton_hitButton(zero, &pos),
                "XPushButton hitButton 矩形内命中");
    XPoint_init(&pos, -1, -1);
    expect_true(!XPushButton_hitButton(zero, &pos),
                "XPushButton hitButton 矩形外不命中");
    expect_true(g_buttonProbe.released == 2,
                "XPushButton click 累计 released");
    XPushButton_setDown(zero, true);
    expect_true(XPushButton_isDown(zero),
                "XPushButton setDown true");
    expect_true(g_buttonProbe.released == 2,
                "XPushButton setDown 不发射 released");
    XPushButton_setDown(zero, false);
    expect_true(g_buttonProbe.released == 2,
                "XPushButton 取消按下不发射 released");
    XWidget_setEnabled((XWidget*)zero, false);
    XPushButton_click(zero);
    expect_true(g_buttonProbe.pressed == 2,
                "XPushButton 禁用时 click 直接返回");
    XWidget_setEnabled((XWidget*)zero, true);

    XPushButton_setDefault(zero, true);
    expect_true(XPushButton_isDefault(zero),
                "XPushButton setDefault 生效");
    expect_true(!XPushButton_autoDefault(zero),
                "XPushButton setDefault 不修改 autoDefault");
    XPushButton_setDefault(zero, false);
    expect_true(!XPushButton_isDefault(zero),
                "XPushButton 取消默认状态");
    XPushButton_setAutoDefault(zero, true);
    expect_true(XPushButton_autoDefault(zero),
                "XPushButton setAutoDefault true");
    XPushButton_setFlat(zero, true);
    expect_true(XPushButton_isFlat(zero),
                "XPushButton setFlat true");

    menu = XMenu_create("Options");
    expect_true(menu != NULL, "按钮菜单测试对象创建");
    if (menu) {
        XPushButton_setMenu(zero, menu);
        expect_true(XPushButton_menu(zero) == menu,
                    "XPushButton setMenu/menu 往返");
        XPushButton_setMenu(zero, NULL);
        expect_true(XPushButton_menu(zero) == NULL,
                    "XPushButton setMenu(NULL) 清空");
        XMenu_delete(menu);
    }

    XPushButton_setText_2(zero, "B");
    XWidget_resize((XWidget*)zero, 24, 16);
    XImage_init_ex(&image, 24, 16, XImageFormat_ARGB32);
    XImage_fill(&image, 0xFF000000u);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "按钮绘制测试绑定图像");
    XPushButton_drawContents(zero, &painter);
    expect_true(XPainter_end(&painter), "按钮绘制测试结束");
    XPainter_deinit(&painter);
    expect_true(XImage_pixel(&image, 12, 8) != 0xFF000000u,
                "按钮离屏绘制产生非背景像素");
    XImage_deinit_base(&image);

    XPushButton_delete_base((XClass*)zero);
    XPushButton_deinit_base(&button);
}

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
/** @brief XPushButton 与 XLabel 的信号槽联动测试（对标窗口演示中的按压文字切换）。 */
static void test_pushbutton_label_signal_slot_link(void)
{
    XPushButton button;
    ButtonLinkProbe link;
    const char* text;

    memset(&button, 0, sizeof(button));
    memset(&link, 0, sizeof(link));
    XPushButton_init(&button, NULL, 0);
    XLabel_init(&link.label, NULL, 0);
    XLabel_setText_2(&link.label, "Released");

    XObject_connect_1((XObject*)&button,
                      (size_t)XPushButton_pressed_signal(NULL),
                      (XObject*)&link, button_link_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&button,
                      (size_t)XPushButton_released_signal(NULL),
                      (XObject*)&link, button_link_releasedSlot,
                      XConnectionType_Direct);

    XPushButton_setDown(&button, true);
    XPushButton_pressed_signal(&button);
    text = XString_toUtf8(XLabel_text(&link.label));
    expect_true(link.pressed == 1 && link.released == 0 &&
                text != NULL && strcmp(text, "Pressed") == 0,
                "XPushButton pressed 信号槽更新标签为 Pressed");

    XPushButton_setDown(&button, false);
    XPushButton_released_signal(&button);
    text = XString_toUtf8(XLabel_text(&link.label));
    expect_true(link.pressed == 1 && link.released == 1 &&
                text != NULL && strcmp(text, "Released") == 0,
                "XPushButton released 信号槽更新标签为 Released");

    XLabel_deinit_base(&link.label);
    XPushButton_deinit_base(&button);
}
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/** @brief XPushButton 鼠标/键盘输入事件契约测试（对标 Qt 6.8 QAbstractButton 事件语义）。 */
static void test_pushbutton_input_event_contract(void)
{
    XPushButton button;
    XPushButton kb;
    XPushButton def;
    XMouseEvent press;
    XMouseEvent release;
    XMouseEvent pressOut;
    XMouseEvent releaseOut;
    XMouseEvent moveOut;
    XMouseEvent moveIn;
    XKeyEvent keyPress;
    XKeyEvent keyRelease;
    XPoint inside = { 10, 10 };
    XPoint outside = { 200, 200 };

    memset(&button, 0, sizeof(button));
    XPushButton_init(&button, NULL, 0);
    XWidget_resize((XWidget*)&button, 80, 40);
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XObject_connect_2((XObject*)&button,
                      (size_t)XPushButton_pressed_signal(&button),
                      button_probe_pressedSlot);
    XObject_connect_2((XObject*)&button,
                      (size_t)XPushButton_released_signal(&button),
                      button_probe_releasedSlot);
    XObject_connect_2((XObject*)&button,
                      (size_t)XPushButton_clicked_signal(&button, false),
                      button_probe_clickedSlot);
    XObject_connect_2((XObject*)&button,
                      (size_t)XPushButton_toggled_signal(&button, false),
                      button_probe_toggledSlot);

    /* 命中控件内部按下：accept，置 down 并发 pressed。 */
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, inside);
    XWidget_event_base((XWidget*)&button, (XEvent*)&press);
    expect_true(XEvent_isAccepted((XEvent*)&press),
                "XPushButton 命中按下 accept");
    expect_true(XPushButton_isDown(&button),
                "XPushButton 命中按下置 down");
    expect_true(g_buttonProbe.pressed == 1,
                "XPushButton 命中按下发 pressed");

    /* 控件内部释放：走 click 路径，发 released/clicked。 */
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, inside);
    XWidget_event_base((XWidget*)&button, (XEvent*)&release);
    expect_true(XEvent_isAccepted((XEvent*)&release),
                "XPushButton 命中释放 accept");
    expect_true(g_buttonProbe.pressed == 0 &&
                g_buttonProbe.released == 1 &&
                g_buttonProbe.clicked == 1,
                "XPushButton 命中释放触发点击信号");
    expect_true(!XPushButton_isDown(&button),
                "XPushButton 点击完成后恢复非按下");
    expect_true(!XPushButton_isChecked(&button),
                "XPushButton 非可选中点击不改 checked");

    /* 外部按下不命中：ignore，不发 pressed。 */
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XMouseEvent_init(&pressOut, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, outside);
    XWidget_event_base((XWidget*)&button, (XEvent*)&pressOut);
    expect_true(!XEvent_isAccepted((XEvent*)&pressOut),
                "XPushButton 外部按下 ignore");
    expect_true(!XPushButton_isDown(&button) &&
                g_buttonProbe.pressed == 0,
                "XPushButton 外部按下不置按下");

    /* 未按下时外部/内部释放都不触发 click。 */
    XMouseEvent_init(&releaseOut, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, outside);
    XWidget_event_base((XWidget*)&button, (XEvent*)&releaseOut);
    expect_true(!XEvent_isAccepted((XEvent*)&releaseOut) &&
                g_buttonProbe.clicked == 0,
                "XPushButton 未按下时释放不点击");

    /* 按住状态移出：down 取消并发 released；移回：恢复 down 并发 pressed。 */
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, inside);
    XWidget_event_base((XWidget*)&button, (XEvent*)&press);
    expect_true(XPushButton_isDown(&button),
                "XPushButton 拖拽测试先按下");

    XMouseEvent_init(&moveOut, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_NoButton,
                     XKeyboardModifier_NoModifier, outside);
    XMouseEvent_setButtons(&moveOut, XMouseButton_LeftButton);
    XWidget_event_base((XWidget*)&button, (XEvent*)&moveOut);
    expect_true(XEvent_isAccepted((XEvent*)&moveOut),
                "XPushButton 按住移出 accept");
    expect_true(!XPushButton_isDown(&button) &&
                g_buttonProbe.released == 1,
                "XPushButton 按住移出取消 down 并发 released");

    XMouseEvent_init(&moveIn, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_NoButton,
                     XKeyboardModifier_NoModifier, inside);
    XMouseEvent_setButtons(&moveIn, XMouseButton_LeftButton);
    XWidget_event_base((XWidget*)&button, (XEvent*)&moveIn);
    expect_true(XEvent_isAccepted((XEvent*)&moveIn) &&
                XPushButton_isDown(&button) &&
                g_buttonProbe.pressed == 2,
                "XPushButton 按住移回恢复 down 并发 pressed");

    XPushButton_setDown(&button, false);

    /* Space 首按/释放按 Qt 语义模拟完整点击。 */
    memset(&kb, 0, sizeof(kb));
    XPushButton_init(&kb, NULL, 0);
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XObject_connect_2((XObject*)&kb,
                      (size_t)XPushButton_pressed_signal(&kb),
                      button_probe_pressedSlot);
    XObject_connect_2((XObject*)&kb,
                      (size_t)XPushButton_released_signal(&kb),
                      button_probe_releasedSlot);
    XObject_connect_2((XObject*)&kb,
                      (size_t)XPushButton_clicked_signal(&kb, false),
                      button_probe_clickedSlot);
    XKeyEvent_init(&keyPress, XEVENT_TYPE_KEY_PRESS, XKey_Space,
                   XKeyboardModifier_NoModifier);
    XKeyEvent_setAutoRepeat(&keyPress, false);
    XWidget_event_base((XWidget*)&kb, (XEvent*)&keyPress);
    expect_true(XEvent_isAccepted((XEvent*)&keyPress) &&
                XPushButton_isDown(&kb) &&
                g_buttonProbe.pressed == 1,
                "XPushButton Space 按下 accept 并置 down");

    XKeyEvent_init(&keyRelease, XEVENT_TYPE_KEY_RELEASE, XKey_Space,
                   XKeyboardModifier_NoModifier);
    XKeyEvent_setAutoRepeat(&keyRelease, false);
    XWidget_event_base((XWidget*)&kb, (XEvent*)&keyRelease);
    expect_true(XEvent_isAccepted((XEvent*)&keyRelease) &&
                !XPushButton_isDown(&kb) &&
                g_buttonProbe.pressed == 1 &&
                g_buttonProbe.released == 1 &&
                g_buttonProbe.clicked == 1,
                "XPushButton Space 释放触发完整点击");

    /* Return/Enter 在 default 按钮上触发 click（对标 QPushButton）。 */
    memset(&def, 0, sizeof(def));
    XPushButton_init(&def, NULL, 0);
    XPushButton_setDefault(&def, true);
    memset(&g_buttonProbe, 0, sizeof(g_buttonProbe));
    XObject_connect_2((XObject*)&def,
                      (size_t)XPushButton_clicked_signal(&def, false),
                      button_probe_clickedSlot);
    XKeyEvent_init(&keyPress, XEVENT_TYPE_KEY_PRESS, XKey_Return,
                   XKeyboardModifier_NoModifier);
    XWidget_event_base((XWidget*)&def, (XEvent*)&keyPress);
    expect_true(XEvent_isAccepted((XEvent*)&keyPress) &&
                g_buttonProbe.clicked == 1,
                "XPushButton default 按钮 Return 触发 click");

    XKeyEvent_deinit_base((XClass*)&keyRelease);
    XKeyEvent_deinit_base((XClass*)&keyPress);
    XMouseEvent_deinit_base((XClass*)&moveIn);
    XMouseEvent_deinit_base((XClass*)&moveOut);
    XMouseEvent_deinit_base((XClass*)&releaseOut);
    XMouseEvent_deinit_base((XClass*)&pressOut);
    XMouseEvent_deinit_base((XClass*)&release);
    XMouseEvent_deinit_base((XClass*)&press);
    XPushButton_deinit_base(&def);
    XPushButton_deinit_base(&kb);
    XPushButton_deinit_base(&button);
}

/** @brief XPushButton autoDefault 的父对话框链契约测试（对标 Qt 6.8 QPushButtonPrivate::dialogParent）。 */
static void test_pushbutton_auto_default_dialog_parent(void)
{
    XWidget dialog;
    XWidget window;
    XWidget container;
    XPushButton inDialog;
    XPushButton inWindow;
    XPushButton inNested;

    memset(&dialog, 0, sizeof(dialog));
    XWidget_init(&dialog, NULL, (XWidgetFlags)XWindowType_Dialog);
    expect_true(XWidget_isWindow(&dialog) &&
                XWidget_windowType(&dialog) == XWindowType_Dialog,
                "对话框父对象为 Dialog 顶层窗口");

    memset(&inDialog, 0, sizeof(inDialog));
    XPushButton_init(&inDialog, &dialog, 0);
    expect_true(XPushButton_autoDefault(&inDialog),
                "Auto 三态在直接父对话框下返回 true");
    XPushButton_setAutoDefault(&inDialog, false);
    expect_true(!XPushButton_autoDefault(&inDialog),
                "对话框父 + setAutoDefault(false) 返回 false");
    XPushButton_setAutoDefault(&inDialog, true);
    expect_true(XPushButton_autoDefault(&inDialog),
                "对话框父 + setAutoDefault(true) 返回 true");

    memset(&container, 0, sizeof(container));
    XWidget_init(&container, &dialog, 0);
    memset(&inNested, 0, sizeof(inNested));
    XPushButton_init(&inNested, &container, 0);
    expect_true(!XWidget_isWindow(&container) &&
                XPushButton_autoDefault(&inNested),
                "Auto 三态经中间子控件仍解析到对话框父返回 true");

    memset(&window, 0, sizeof(window));
    XWidget_init(&window, NULL, (XWidgetFlags)XWindowType_Widget);
    expect_true(XWidget_isWindow(&window) &&
                XWidget_windowType(&window) == XWindowType_Window,
                "普通顶层父对象为 Window 类型");
    memset(&inWindow, 0, sizeof(inWindow));
    XPushButton_init(&inWindow, &window, 0);
    expect_true(!XPushButton_autoDefault(&inWindow),
                "Auto 三态在普通窗口父下返回 false");
    XPushButton_setAutoDefault(&inWindow, true);
    expect_true(XPushButton_autoDefault(&inWindow),
                "普通窗口父 + setAutoDefault(true) 返回 true");

    XPushButton_deinit_base(&inNested);
    XWidget_deinit_base(&container);
    XPushButton_deinit_base(&inWindow);
    XWidget_deinit_base(&window);
    XPushButton_deinit_base(&inDialog);
    XWidget_deinit_base(&dialog);
}
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */

int main(void)
{
    test_geometry_contract();
    test_pixmap_scroll_and_bitmap_alias();
    test_pixmap_mask_lifecycle();
    test_pixmap_lifecycle();
    test_icon_sizes();
    test_icon_matching();
    test_icon_device_pixel_ratio();
    test_icon_scaled_pixmap_cache();
    test_icon_style_helper();
    test_icon_theme_engine_contract();
    test_icon_engine_hook_contract();
    test_icon_paint_visual_alignment();
#if XIMAGECODEC_ON
    test_icon_add_file_size();
    test_icon_theme_index_inherits();
    test_icon_theme_fallback_legacy_search_path();
#if XIMAGECODEC_PNG_ON
    test_icon_theme_standalone_fallback_sizes();
#endif /* XIMAGECODEC_PNG_ON */
    test_icon_theme_engine_paint_scales_to_rect();
    test_icon_theme_scale_selection();
#endif /* XIMAGECODEC_ON */
    test_pixmap_cache_contract();
#if defined(__unix__)
    test_pixmap_cache_concurrency();
#endif
    test_picture_play_contract();
    test_picture_malformed_state_records();
#if XPAINTER_SHAPE_ON && XPAINTER_POLYGON_ON
    test_picture_painter_high_level_record_link();
#endif /* XPAINTER_SHAPE_ON && XPAINTER_POLYGON_ON */
#if XPAINTER_PATH_ON
    test_picture_painter_path_record_link();
#endif /* XPAINTER_PATH_ON */
    test_painter_raster_contract();
    test_painter_extra_alignment();
    test_painter_draw_picture_align();
    test_painter_transform_contract();
#if XPAINTER_PATH_ON
    test_painter_path_contract();
    test_painter_path_callback_contract();
#endif /* XPAINTER_PATH_ON */
#if XPAINTER_SHAPE_ON
    test_painter_shape_contract();
    test_painter_shape_callback_contract();
#endif /* XPAINTER_SHAPE_ON */
#if XPAINTER_POLYGON_ON
    test_painter_polygon_contract();
    test_painter_polygon_callback_contract();
#endif /* XPAINTER_POLYGON_ON */
#if XPAINTER_PENSTYLE_ON
    test_painter_penstyle_contract();
    test_painter_picture_penstyle_replay_contract();
#endif /* XPAINTER_PENSTYLE_ON */
#if XPAINTER_BRUSH_ON && XPAINTER_POLYGON_ON
    test_painter_brush_contract();
#endif /* XPAINTER_BRUSH_ON && XPAINTER_POLYGON_ON */
#if XPAINTER_TEXTLAYOUT_ON
    test_painter_text_layout_contract();
    test_painter_text_flags_contract();
#endif /* XPAINTER_TEXTLAYOUT_ON */
    test_painter_record_play_contract();
    test_painter_picture_pen_state_record();
    test_painter_picture_opacity_composition_record();
    test_painter_picture_background_record();
#if XPAINTER_RENDERHINT_ON
    test_painter_picture_render_hints_record();
#endif /* XPAINTER_RENDERHINT_ON */
#if XPAINTER_BRUSH_ORIGIN_ON
    test_painter_picture_brush_origin_record();
#endif /* XPAINTER_BRUSH_ORIGIN_ON */
#if XPAINTER_BRUSH_ON
    test_painter_picture_brush_record();
#endif /* XPAINTER_BRUSH_ON */
    test_painter_picture_transform_record();
#if XPAINTER_VIEW_TRANSFORM_ON
    test_painter_picture_view_transform_record();
#endif /* XPAINTER_VIEW_TRANSFORM_ON */
    test_image_reader_decide_format_state();
    test_image_reader_allocation_limit();
#if XIMAGECODEC_ON
    test_image_device_io();
    test_image_handler_registry();
    test_image_plugin_registry_integration();
    test_image_codec_round_trip();
    test_codec_pixel_round_trip();
    test_codec_reject_malformed();
#if XIMAGECODEC_BMP_ON
    test_codec_bmp_malformed();
    test_codec_bmp_alpha_semantics();
    test_image_reader_malformed_bmp();
    test_image_reader_invalid_clip_rect();
#endif /* XIMAGECODEC_BMP_ON */
    test_codec_detect_only();
    test_codec_decode_real_assets();
    test_codec_png_palette_round_trip();
    test_codec_png_extended_assets();
    test_codec_bmp_extended_assets();
#if XIMAGECODEC_JPEG_ON
    test_codec_jpeg_extended_assets();
#endif /* XIMAGECODEC_JPEG_ON */
#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
    test_codec_svg_vector_render();
#endif
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    test_codec_gif_animation();
#endif
    test_codec_upper_layer_files();
    test_codec_upper_layer_devices();
#endif /* XIMAGECODEC_ON */
    test_image_pixel_contract();
    test_image_mask_qt_semantics();
    test_image_format_mapping_and_color_fill();
    test_image_text_metadata_sorted_map();
    test_image_gray_and_metadata_noop_contract();
#if XSCREEN_ON
    test_screen_contract();
#endif /* XSCREEN_ON */
#if XWINDOW_ON
    test_window_contract();
#endif /* XWINDOW_ON */
#if XGUIAPPLICATION_ON
    test_gui_application_contract();
#endif /* XGUIAPPLICATION_ON */
#if XWIDGET_ON
    test_widget_contract();
#endif /* XWIDGET_ON */
#if XWIDGET_ON && XFRAME_ON
    test_frame_contract();
#endif /* XWIDGET_ON && XFRAME_ON */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    test_label_contract();
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
#if XWIDGET_ON && XPUSHBUTTON_ON
    test_pushbutton_contract();
    test_pushbutton_input_event_contract();
    test_pushbutton_auto_default_dialog_parent();
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    test_pushbutton_label_signal_slot_link();
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
#endif /* XWIDGET_ON && XPUSHBUTTON_ON */
#if XLAYOUT_ON
    test_layout_item_contract();
    test_layout_widget_integration();
    test_layout_total_extended();
#if XLAYOUT_STACKED_ON
    test_stacked_layout_contract();
#endif /* XLAYOUT_STACKED_ON */
#endif /* XLAYOUT_ON */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    test_box_layout_contract();
    test_box_layout_extended();
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
#if XLAYOUT_ON && XLAYOUT_GRID_ON
    test_grid_layout_contract();
    test_grid_layout_default_positioning();
    test_grid_layout_replace_item();
#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */
#if XLAYOUT_ON
    /* 清理布局测试宿主（其余布局控件已由各自测试删除）。 */
    if (g_layoutTestHost) {
        XWidget_delete_base((XClass*)g_layoutTestHost);
        g_layoutTestHost = NULL;
    }
#endif /* XLAYOUT_ON */
#if XWINDOWEVENT_ON && XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON
    test_window_event_payloads();
    test_window_event_loop();
#endif /* XWINDOWEVENT_ON && XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON */
#if XPLATFORMINTEGRATION_ON
    test_gpu_contract();
#endif /* XPLATFORMINTEGRATION_ON */
    if (s_failures != 0) {
        XERROR_PRINTF("%d XGui regression test(s) failed\n", s_failures);
        return 1;
    }
    puts("XGui regression tests passed");
    return 0;
}
