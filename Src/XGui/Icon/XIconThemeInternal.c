/******************************************************************************
 * @file       XIconThemeInternal.c
 * @brief      XIcon 主题图标资源解析实现（对标 Qt 6.8 QIconLoader 简化版）。
 * @note       本文件只提供内部主题文件发现与尺寸匹配，不读取 index.theme
 *             的 Inherits/Context 元数据；大小目录按常用 XDG 目录名匹配，
 *             再按“当前主题、后备主题”的顺序回退，供主题引擎和 hasThemeIcon
 *             等入口共用。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIconThemeInternal.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include "XContainer.h"
#include "XIcon.h"
#include "XImageCodec_config.h"
#include "XGeometry.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
#define XICON_THEME_SVG_AVAILABLE 1
#else
#define XICON_THEME_SVG_AVAILABLE 0
#endif

static const char* theme_size_dirs[] = {
    "scalable", "512x512", "256x256", "128x128", "96x96", "64x64",
    "48x48", "32x32", "24x24", "22x22", "16x16"
};

static const char* theme_context_dirs[] = {
    "actions", "apps", "devices", "emblems", "mimetypes",
    "places", "status", "ui"
};

static const char* theme_import_exts[] = {
    ".png", ".svg", ".xpm", ".bmp", ""
};

/* XDG 目录名中的尺寸为十进制，例如 48x48。 */
static int theme_parseSize(const char* s)
{
    int value = 0;
    if (!s) return 0;
    s = strchr(s, 'x');
    if (!s || !s[1]) return 0;
    ++s;
    while (*s >= '0' && *s <= '9') {
        int d = *s - '0';
        if (value > 65535) return 0;
        value = value * 10 + d;
        ++s;
    }
    return value > 0 ? value : 0;
}

static void theme_buildDirPath(char* out, size_t outSize, const char* root,
                               const char* theme, const char* sizeDir,
                               const char* context, const char* name,
                               const char* ext)
{
    snprintf(out, outSize, "%s/%s/%s/%s/%s%s", root ? root : "",
             theme ? theme : "", sizeDir ? sizeDir : "",
             context ? context : "", name ? name : "", ext ? ext : "");
}

static void theme_buildThemePath(char* out, size_t outSize, const char* root,
                                 const char* theme, const char* name,
                                 const char* ext)
{
    snprintf(out, outSize, "%s/%s/%s%s", root ? root : "",
             theme ? theme : "", name ? name : "", ext ? ext : "");
}

static void theme_buildRootPath(char* out, size_t outSize, const char* root,
                                const char* name, const char* ext)
{
    snprintf(out, outSize, "%s/%s%s", root ? root : "", name ? name : "",
             ext ? ext : "");
}

static bool theme_loadFile(const char* path, XPixmap* out)
{
    XPixmap candidate;
    bool ok;
    if (!path || !path[0]) return false;
    XPixmap_init(&candidate);
    ok = XPixmap_load_2(&candidate, path, NULL, 0) &&
         !XPixmap_isNull(&candidate);
    if (ok && out) {
        XPixmap_move_base(out, &candidate);
        XPixmap_deinit_base(&candidate);
    } else {
        XPixmap_deinit_base(&candidate);
    }
    return ok;
}

static bool theme_extAllowed(const char* ext)
{
#if XICON_THEME_SVG_AVAILABLE
    return true;
#else
    return !(ext[0] == '.' && ext[1] == 's' && ext[2] == 'v' && ext[3] == 'g');
#endif
}

static int theme_sizeDistance(int requested, int dirSize)
{
    if (requested <= 0 || dirSize <= 0) return INT_MAX;
    if (requested == dirSize) return 0;
    if (requested < dirSize) return dirSize - requested;
    return (requested - dirSize) * 4;
}

static bool theme_tryDir(const char* root, const char* theme, const char* name,
                         int target, const char* sizeDir, const char* context,
                         XPixmap* out, int* distance)
{
    char path[1024];
    size_t extIndex;
    int dirSize;
    bool loaded = false;
    if (!root || !root[0] || !theme || !theme[0] || !name || !name[0])
        return false;
    dirSize = theme_parseSize(sizeDir);
    for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
             sizeof(theme_import_exts[0]); ++extIndex) {
        const char* ext = theme_import_exts[extIndex];
        if (!theme_extAllowed(ext)) continue;
        theme_buildDirPath(path, sizeof(path), root, theme, sizeDir, context,
                           name, ext);
        if (theme_loadFile(path, out)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) return false;
    if (distance)
        *distance = (dirSize > 0) ? (sizeDir[0] == 's' ? 0 :
                                     theme_sizeDistance(target, dirSize)) : 0;
    return true;
}

static bool theme_tryTheme(const char* root, const char* theme,
                           const char* name, int target, XPixmap* out,
                           int* distance)
{
    size_t di;
    size_t ci;
    for (di = 0; di < sizeof(theme_size_dirs) / sizeof(theme_size_dirs[0]); ++di) {
        for (ci = 0; ci < sizeof(theme_context_dirs) / sizeof(theme_context_dirs[0]); ++ci) {
            if (theme_tryDir(root, theme, name, target, theme_size_dirs[di],
                             theme_context_dirs[ci], out, distance))
                return true;
        }
    }
    return false;
}

static bool theme_scaledToSize(XPixmap* pixmap, int target)
{
    int w;
    int h;
    XPixmap scaled;
    if (!pixmap || XPixmap_isNull(pixmap) || target <= 1) return false;
    w = XPixmap_width(pixmap);
    h = XPixmap_height(pixmap);
    if (w == target && h == target) return true;
    XPixmap_init(&scaled);
    XPixmap_scaled(pixmap, target, target, 0, 0, &scaled);
    if (!XPixmap_isNull(&scaled)) {
        XPixmap_deinit_base(pixmap);
        XPixmap_move_base(pixmap, &scaled);
        XPixmap_deinit_base(&scaled);
        return true;
    }
    XPixmap_deinit_base(&scaled);
    return false;
}

static bool theme_loadBestInSet(const XStringList* paths, const char* theme,
                                const char* name, int target, XPixmap* best,
                                int* bestDistance)
{
    size_t pathIndex;
    for (pathIndex = 0; paths && pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        XPixmap candidate;
        const XString* path = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = path ? XString_toUtf8(path) : NULL;
        int distance = INT_MAX;
        if (!root || !root[0]) continue;
        XPixmap_init(&candidate);
        if (theme_tryTheme(root, theme, name, target, &candidate, &distance) &&
            distance < *bestDistance) {
            if (!XPixmap_isNull(best)) XPixmap_deinit_base(best);
            XPixmap_move_base(best, &candidate);
            XPixmap_deinit_base(&candidate);
            *bestDistance = distance;
            if (distance == 0) return true;
        } else {
            XPixmap_deinit_base(&candidate);
        }
    }
    return false;
}

static bool theme_loadLegacy(const XStringList* paths, const char* theme,
                             const char* name, XPixmap* out)
{
    size_t pathIndex;
    if (!paths) return false;
    for (pathIndex = 0; pathIndex < XStringList_size_base(
             (XContainer*)paths); ++pathIndex) {
        const XString* path = (const XString*)XStringList_at_base(
            (const XVector*)paths, (int64_t)pathIndex);
        const char* root = path ? XString_toUtf8(path) : NULL;
        size_t extIndex;
        char filePath[1024];
        if (!root || !root[0]) continue;
        for (extIndex = 0; extIndex < sizeof(theme_import_exts) /
                 sizeof(theme_import_exts[0]); ++extIndex) {
            const char* ext = theme_import_exts[extIndex];
            if (!theme_extAllowed(ext)) continue;
            if (theme && theme[0]) {
                theme_buildThemePath(filePath, sizeof(filePath), root, theme,
                                     name, ext);
            } else {
                theme_buildRootPath(filePath, sizeof(filePath), root, name, ext);
            }
            if (theme_loadFile(filePath, out)) return true;
        }
    }
    return false;
}

bool XIconInternal_resolveThemePixmapSize(const char* name, int size, XPixmap* out)
{
    XStringList* themePaths = NULL;
    XStringList* fallbackPaths = NULL;
    XString* themeName = NULL;
    XString* fallbackThemeName = NULL;
    const char* themeUtf8 = NULL;
    const char* fallbackUtf8 = NULL;
    XPixmap best;
    XPixmap candidate;
    int bestDistance = INT_MAX;
    bool any = false;
    bool found;
    if (!name || !name[0] || !out) return false;
    if (size <= 0) size = 48;
    XPixmap_init(out);

    if (name[0] == '/' || name[0] == '.' || strchr(name, '/')) {
        if (theme_loadFile(name, out)) {
            theme_scaledToSize(out, size);
            return true;
        }
        return false;
    }

    themePaths = XIcon_themeSearchPaths_2();
    fallbackPaths = XIcon_fallbackSearchPaths_2();
    themeName = XIcon_themeName();
    fallbackThemeName = XIcon_fallbackThemeName();
    if (themeName && !XString_isEmpty_base((const XContainer*)themeName))
        themeUtf8 = XString_toUtf8(themeName);
    if (fallbackThemeName && !XString_isEmpty_base(
            (const XContainer*)fallbackThemeName))
        fallbackUtf8 = XString_toUtf8(fallbackThemeName);

    XPixmap_init(&best);
    XPixmap_init(&candidate);

    if (themeUtf8) {
        any = theme_loadBestInSet(themePaths, themeUtf8, name, size, &best,
                                  &bestDistance) || any;
    }
    if (!any && fallbackUtf8) {
        any = theme_loadBestInSet(fallbackPaths, fallbackUtf8, name, size,
                                  &best, &bestDistance) || any;
    }
    if (!any && themeUtf8) {
        any = theme_loadLegacy(themePaths, themeUtf8, name, &best);
    }
    if (!any && fallbackUtf8) {
        any = theme_loadLegacy(fallbackPaths, fallbackUtf8, name, &best);
    }
    if (!any) {
        any = theme_loadLegacy(themePaths, NULL, name, &best);
    }
    if (!any) {
        any = theme_loadLegacy(fallbackPaths, NULL, name, &best);
    }

    found = any && !XPixmap_isNull(&best);
    if (found) {
        theme_scaledToSize(&best, size);
        if (out) {
            XPixmap_move_base(out, &best);
            XPixmap_deinit_base(&best);
        }
    } else {
        XPixmap_deinit_base(&best);
    }
    XPixmap_deinit_base(&candidate);
    if (themeName) XString_delete_base((XClass*)themeName);
    if (fallbackThemeName) XString_delete_base((XClass*)fallbackThemeName);
    if (themePaths) XStringList_delete_base((XClass*)themePaths);
    if (fallbackPaths) XStringList_delete_base((XClass*)fallbackPaths);
    return found;
}

bool XIconInternal_resolveThemePixmap(const char* name, XPixmap* out)
{
    return XIconInternal_resolveThemePixmapSize(name, 48, out);
}
