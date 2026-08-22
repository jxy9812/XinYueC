#include "XPlatformFontDatabase.h"
#if XPLATFORMINTEGRATION_ON && \
    !(defined(__linux__) && defined(XINYUE_C_HAS_FONTCONFIG)) && !defined(_WIN32)
bool XPlatformFontDatabaseDriver_collect(XVector* families)
{ (void)families; return false; }
#endif
