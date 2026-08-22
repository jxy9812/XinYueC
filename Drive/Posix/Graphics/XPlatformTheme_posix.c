#include "XPlatformTheme.h"
#if XPLATFORMINTEGRATION_ON && defined(__linux__)
#include <stdlib.h>
#include <string.h>
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
#endif
