/******************************************************************************
 * @file       XIconScaledPixmapCache.c
 * @brief      XIcon 缩放像素图缓存内部辅助实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIconScaledPixmapCache.h"
#include "XString.h"
#include "XPixmapCache.h"
#include <stdio.h>
#include <string.h>

static bool cacheKeyBuild(char* out, size_t outSize, const char* prefix,
                          const char* sourceKey, uint64_t paletteKey,
                          XIconMode mode,
                          int width, int height, int dprThousand)
{
    int written;
    if (!out || outSize == 0 || !prefix || !sourceKey || width <= 0 ||
        height <= 0 || dprThousand < 0)
        return false;
    written = snprintf(out, outSize, "%s%s/%llu/%d/%d/%d/%d",
                       prefix, sourceKey,
                       (unsigned long long)paletteKey, (int)mode,
                       width, height, dprThousand);
    return written >= 0 && (size_t)written < outSize;
}

bool XIconScaledPixmapCache_find(const char* prefix, const char* sourceKey,
                                 uint64_t paletteKey, XIconMode mode,
                                 int width, int height, int dprThousand,
                                 XPixmap* out)
{
    char keyBuffer[320];
    XString* key;
    XPixmap cached;
    bool found;
    if (!out || !cacheKeyBuild(keyBuffer, sizeof(keyBuffer), prefix, sourceKey,
                               paletteKey, mode, width, height, dprThousand))
        return false;
    key = XString_create_utf8(keyBuffer);
    if (!key) return false;
    XPixmap_init(&cached);
    found = XPixmapCache_find(key, &cached);
    XString_delete_base((XClass*)key);
    if (!found) {
        XPixmap_deinit_base(&cached);
        return false;
    }
    XPixmap_copy_base(out, &cached);
    XPixmap_deinit_base(&cached);
    return true;
}

bool XIconScaledPixmapCache_insert(const char* prefix, const char* sourceKey,
                                   uint64_t paletteKey, XIconMode mode,
                                   int width, int height, int dprThousand,
                                   const XPixmap* pixmap)
{
    char keyBuffer[320];
    XString* key;
    bool inserted;
    if (!pixmap || XPixmap_isNull(pixmap) ||
        !cacheKeyBuild(keyBuffer, sizeof(keyBuffer), prefix, sourceKey,
                       paletteKey, mode, width, height, dprThousand))
        return false;
    key = XString_create_utf8(keyBuffer);
    if (!key) return false;
    inserted = XPixmapCache_insert(key, pixmap);
    XString_delete_base((XClass*)key);
    return inserted;
}

void XIconScaledPixmapCache_clear(void)
{
    XPixmapCache_clear();
}
