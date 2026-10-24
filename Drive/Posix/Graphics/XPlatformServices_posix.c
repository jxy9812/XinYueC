#include "XPlatformServices.h"
/* [2026-10-24 平台补扫] __ANDROID__ 亦定义 __linux__：xdg-open/fork 通道
 * 与安卓沙箱无关，平台实现由 Drive/Android/Graphics/XPlatformServices_android.c
 * 提供（JNI ACTION_VIEW），此处必须排除，否则两端重复定义 Driver 钩子。 */
#if XPLATFORMINTEGRATION_ON && defined(__linux__) && !defined(__ANDROID__)
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
bool XPlatformServicesDriver_isAvailable(void)
{ return getenv("DISPLAY") != NULL || getenv("WAYLAND_DISPLAY") != NULL; }
bool XPlatformServicesDriver_openUrl(const char* url)
{
    pid_t pid;
    if (!url || !XPlatformServicesDriver_isAvailable()) return false;
    pid = fork();
    if (pid < 0) return false;
    if (pid == 0) { execlp("xdg-open", "xdg-open", url, (char*)NULL); _exit(127); }
    return true;
}
#endif
