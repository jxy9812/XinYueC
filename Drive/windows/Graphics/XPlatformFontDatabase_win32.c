#include "XPlatformFontDatabase.h"
#if XPLATFORMINTEGRATION_ON && defined(_WIN32)
#include "XString.h"
#include <windows.h>

/** @brief 枚举回调：W 版 UTF-16 族名转 XString 入表，滤空并按名去重。
 *  @note  A 版 EnumFontFamiliesExA 在 zh-CN 下对无法用 ANSI 代码页表示的
 *         本地化族名（如「微软雅黑」）返回空 lfFaceName，空串经
 *         XString_toUtf8 变 NULL 会在下游 XStrcmp 解引用崩溃；故改用
 *         W 版并显式过滤空名。DEFAULT_CHARSET 按字符集逐次回调，同一
 *         族名会重复出现，需去重。 */
static int CALLBACK xpfont_enum(const LOGFONTW* lf, const TEXTMETRICW* tm,
                                DWORD type, LPARAM data)
{
    XVector* families = (XVector*)(void*)data;
    XString* value;
    size_t i, n;
    (void)tm; (void)type;
    if (!families || !lf) return 0;
    if (!lf->lfFaceName[0]) return 1;            /* 滤空：空族名不入表。 */
    value = XString_create_utf16((const uint16_t*)lf->lfFaceName);
    if (!value) return 1;
    n = XVector_size_base((const XContainer*)families);
    for (i = 0; i < n; ++i) {                    /* 去重：同族名只留首个。 */
        XString** exist = (XString**)XVector_at_base(families, (int64_t)i);
        if (exist && *exist && XString_equals(*exist, value,
                                               XChar_CaseSensitive)) {
            XString_delete_base((XClass*)value);
            return 1;
        }
    }
    XVector_Push_Back_Base(families, XString*, value);
    return 1;
}
bool XPlatformFontDatabaseDriver_collect(XVector* families)
{
    HDC dc;
    LOGFONTW lf;
    if (!families) return false;
    dc = GetDC(NULL);
    if (!dc) return false;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, xpfont_enum, (LPARAM)(void*)families, 0);
    ReleaseDC(NULL, dc);
    return true;
}
#endif
