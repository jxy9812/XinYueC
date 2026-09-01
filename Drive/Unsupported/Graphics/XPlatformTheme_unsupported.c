#include "XPlatformTheme.h"
#if XPLATFORMINTEGRATION_ON && !defined(__linux__) && !defined(_WIN32)
#include <string.h>
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity)
{
    if (dark) *dark = false;
    if (name && capacity) { strncpy(name, "embedded", capacity - 1); name[capacity - 1] = '\0'; }
    return false;
}
bool XPlatformThemeDriver_iconSearchPaths(bool fallback, XStringList* out)
{
    (void)fallback;
    (void)out;
    return false;
}
bool XPlatformThemeDriver_iconThemeName(bool fallback, char* name, size_t capacity)
{
    (void)fallback;
    if (name && capacity) name[0] = '\0';
    return false;
}
#endif
