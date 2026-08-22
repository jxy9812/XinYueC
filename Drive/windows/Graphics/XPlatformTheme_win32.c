#include "XPlatformTheme.h"
#if XPLATFORMINTEGRATION_ON && defined(_WIN32)
#include <windows.h>
#include <string.h>
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity)
{
    DWORD value = 1, size = sizeof(value);
    HKEY key = NULL;
    bool found = RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key) == ERROR_SUCCESS;
    if (found && RegQueryValueExA(key, "AppsUseLightTheme", NULL, NULL,
                                  (LPBYTE)&value, &size) != ERROR_SUCCESS) value = 1;
    if (key) RegCloseKey(key);
    if (dark) *dark = found && value == 0;
    if (name && capacity) { strncpy(name, "windows", capacity - 1); name[capacity - 1] = '\0'; }
    return found;
}
#endif
