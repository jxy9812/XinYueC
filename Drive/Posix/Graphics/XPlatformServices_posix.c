#include "XPlatformServices.h"
#if XPLATFORMINTEGRATION_ON && defined(__linux__)
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
