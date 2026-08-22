#include "XPlatformTheme.h"
#if XPLATFORMINTEGRATION_ON && !defined(__linux__) && !defined(_WIN32)
#include <string.h>
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity)
{
    if (dark) *dark = false;
    if (name && capacity) { strncpy(name, "embedded", capacity - 1); name[capacity - 1] = '\0'; }
    return false;
}
#endif
