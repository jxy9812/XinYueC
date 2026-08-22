#include "XPlatformServices.h"
#if XPLATFORMINTEGRATION_ON && !defined(__linux__) && !defined(_WIN32)
bool XPlatformServicesDriver_isAvailable(void) { return false; }
bool XPlatformServicesDriver_openUrl(const char* url) { (void)url; return false; }
#endif
