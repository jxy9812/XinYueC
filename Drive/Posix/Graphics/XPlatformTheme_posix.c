#include "XPlatformTheme.h"
#if XPLATFORMINTEGRATION_ON && defined(__linux__)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "XStringList.h"

/*
 * Qt obtains these values from QPlatformTheme.  The portable POSIX driver
 * has no desktop-shell object, so it follows the XDG variables that desktop
 * launchers already export.  Paths are copied into the caller-owned list;
 * no platform allocation crosses the Src/ boundary.
 */
static void xplatform_icon_append_path(XStringList* out, const char* base,
                                       const char* suffix)
{
    char path[1024];
    int written;
    size_t baseLength;
    if (!out || !base || !base[0]) return;
    baseLength = strlen(base);
    if (baseLength > 0 && base[baseLength - 1] == '/')
        written = snprintf(path, sizeof(path), "%s%s", base, suffix);
    else
        written = snprintf(path, sizeof(path), "%s/%s", base, suffix);
    if (written >= 0 && (size_t)written < sizeof(path))
        XStringList_push_back_utf8(out, path);
}

static void xplatform_icon_append_colon_paths(XStringList* out,
                                              const char* value,
                                              const char* suffix)
{
    const char* begin;
    const char* end;
    char base[768];
    size_t length;
    if (!out || !value || !value[0]) return;
    begin = value;
    while (*begin) {
        end = strchr(begin, ':');
        if (!end) end = begin + strlen(begin);
        length = (size_t)(end - begin);
        if (length > 0 && length < sizeof(base)) {
            memcpy(base, begin, length);
            base[length] = '\0';
            xplatform_icon_append_path(out, base, suffix);
        }
        if (!*end) break;
        begin = end + 1;
    }
}

bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity)
{
    const char* value = getenv("XDG_CURRENT_DESKTOP");
    const char* color = getenv("COLOR_SCHEME");
    if (dark) *dark = color && (strcmp(color, "dark") == 0 || strcmp(color, "prefer-dark") == 0);
    if (name && capacity) {
        strncpy(name, value && value[0] ? value : "linux", capacity - 1);
        name[capacity - 1] = '\0';
    }
    return value != NULL || color != NULL;
}

bool XPlatformThemeDriver_iconSearchPaths(bool fallback, XStringList* out)
{
    const char* home = getenv("HOME");
    const char* dataHome = getenv("XDG_DATA_HOME");
    const char* dataDirs = getenv("XDG_DATA_DIRS");
    int before;
    if (!out) return false;
    before = XStringList_size_base((const XContainer*)out);

    /* ~/.icons is the legacy location used by Qt's fallback lookup. */
    if (home) xplatform_icon_append_path(out, home, ".icons");
    if (fallback) {
        if (dataHome && dataHome[0])
            xplatform_icon_append_path(out, dataHome, "pixmaps");
        xplatform_icon_append_colon_paths(out, dataDirs, "pixmaps");
        xplatform_icon_append_path(out, "/usr/local/share", "pixmaps");
        xplatform_icon_append_path(out, "/usr/share", "pixmaps");
    } else {
        if (dataHome && dataHome[0])
            xplatform_icon_append_path(out, dataHome, "icons");
        xplatform_icon_append_colon_paths(out, dataDirs, "icons");
        if (!dataHome || !dataHome[0]) {
            if (home) xplatform_icon_append_path(out, home, ".local/share/icons");
        }
    }
    return XStringList_size_base((const XContainer*)out) > before;
}

bool XPlatformThemeDriver_iconThemeName(bool fallback, char* name,
                                        size_t capacity)
{
    const char* value;
    if (!name || capacity == 0) return false;
    value = fallback ? getenv("XDG_ICON_FALLBACK_THEME") :
        getenv("QT_QPA_SYSTEM_ICON_THEME");
    if (!value || !value[0])
        value = fallback ? getenv("XDG_FALLBACK_ICON_THEME") :
            getenv("XDG_ICON_THEME");
    if (!value || !value[0]) {
        name[0] = '\0';
        return false;
    }
    strncpy(name, value, capacity - 1);
    name[capacity - 1] = '\0';
    return name[0] != '\0';
}
#endif
