#include "XPlatformServices.h"
#if XPLATFORMINTEGRATION_ON && defined(_WIN32)
#include <windows.h>
bool XPlatformServicesDriver_isAvailable(void) { return true; }
bool XPlatformServicesDriver_openUrl(const char* url)
{ return url && (INT_PTR)ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL) > 32; }
#endif
