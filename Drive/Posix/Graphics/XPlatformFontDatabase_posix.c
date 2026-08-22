#include "XPlatformFontDatabase.h"
#if XPLATFORMINTEGRATION_ON && defined(__linux__) && defined(XINYUE_C_HAS_FONTCONFIG)
#include "XString.h"
#include <fontconfig/fontconfig.h>
#include <string.h>
static bool xpfont_hasFamily(const XVector* families, const char* family)
{
    size_t i;
    size_t n = XVector_size_base((const XContainer*)families);
    for (i = 0; i < n; ++i) {
        XString* const* item = (XString* const*)XVector_at_base(
            families, (int64_t)i);
        if (item && *item && strcmp(XString_toUtf8(*item), family) == 0)
            return true;
    }
    return false;
}
bool XPlatformFontDatabaseDriver_collect(XVector* families)
{
    FcPattern* pattern;
    FcObjectSet* objects;
    FcFontSet* set;
    int i;
    if (!families || !FcInit()) return false;
    pattern = FcPatternBuild(NULL, FC_SCALABLE, FcTypeBool, FcTrue, NULL);
    objects = FcObjectSetBuild(FC_FAMILY, NULL);
    set = pattern && objects ? FcFontList(NULL, pattern, objects) : NULL;
    if (pattern) FcPatternDestroy(pattern);
    if (objects) FcObjectSetDestroy(objects);
    if (!set) return false;
    for (i = 0; i < set->nfont; ++i) {
        FcChar8* family = NULL;
        if (FcPatternGetString(set->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family) {
            if (!xpfont_hasFamily(families, (const char*)family)) {
                XString* value = XString_create_utf8((const char*)family);
                if (value) XVector_Push_Back_Base(families, XString*, value);
            }
        }
    }
    FcFontSetDestroy(set);
    return true;
}
#endif
