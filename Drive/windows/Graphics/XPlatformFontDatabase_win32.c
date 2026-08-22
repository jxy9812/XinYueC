#include "XPlatformFontDatabase.h"
#if XPLATFORMINTEGRATION_ON && defined(_WIN32)
#include "XString.h"
#include <windows.h>
static int CALLBACK xpfont_enum(const LOGFONTA* lf, const TEXTMETRICA* tm,
                                DWORD type, LPARAM data)
{
    XVector* families = (XVector*)(void*)data;
    XString* value;
    (void)tm; (void)type;
    if (!families || !lf) return 0;
    value = XString_create_utf8(lf->lfFaceName);
    if (value) XVector_Push_Back_Base(families, XString*, value);
    return 1;
}
bool XPlatformFontDatabaseDriver_collect(XVector* families)
{
    HDC dc;
    LOGFONTA lf;
    if (!families) return false;
    dc = GetDC(NULL);
    if (!dc) return false;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExA(dc, &lf, xpfont_enum, (LPARAM)(void*)families, 0);
    ReleaseDC(NULL, dc);
    return true;
}
#endif
