/******************************************************************************
 * @file       XIconScaledPixmapCache.c
 * @brief      XIcon 缩放像素图缓存内部辅助实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XIconScaledPixmapCache.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XString.h"
#if XPIXMAPCACHE_ON
#include "XPixmapCache.h"
#endif /* XPIXMAPCACHE_ON */
#include <stdio.h>

#if XPIXMAPCACHE_ON
static bool cacheKeyBuild(char* out, size_t outSize, const char* prefix,
                          const char* sourceKey, uint64_t paletteKey,
                          XIconMode mode,
                          int width, int height, int dprThousand)
{
    int written;
    if (!out || outSize == 0 || !prefix || !sourceKey || width <= 0 ||
        height <= 0 || dprThousand < 0)
        return false;
    written = XSnprintf(out, outSize, "%s%s/%llu/%d/%d/%d/%d",
                       prefix, sourceKey,
                       (unsigned long long)paletteKey, (int)mode,
                       width, height, dprThousand);
    return written >= 0 && (size_t)written < outSize;
}
#endif /* XPIXMAPCACHE_ON */

bool XIconScaledPixmapCache_find(const char* prefix, const char* sourceKey,
                                 uint64_t paletteKey, XIconMode mode,
                                 int width, int height, int dprThousand,
                                 XPixmap* out)
{
#if XPIXMAPCACHE_ON
    char keyBuffer[320];
    XString* key;
    XPixmap cached;
    XMemset(&cached, 0, sizeof(cached)); /* 裸栈清零：防 init 的 vtable 探测把前序帧残留误判为已初始化而释放陈旧 m_data */
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
    XCopy(out, &cached);
    XPixmap_deinit_base(&cached);
    return true;
#else /* !XPIXMAPCACHE_ON */
    /* 像素图缓存裁剪（XPIXMAPCACHE_ON=0）时的回退路径：全局缓存不存在，
     * 查找语义退化为永久未命中；调用方（XIcon/XIconThemeEngine）会按
     * 未命中继续走正常的渲染与回退分支，功能不受损。 */
    (void)prefix; (void)sourceKey; (void)paletteKey; (void)mode;
    (void)width; (void)height; (void)dprThousand; (void)out;
    return false;
#endif /* XPIXMAPCACHE_ON */
}

bool XIconScaledPixmapCache_insert(const char* prefix, const char* sourceKey,
                                   uint64_t paletteKey, XIconMode mode,
                                   int width, int height, int dprThousand,
                                   const XPixmap* pixmap)
{
#if XPIXMAPCACHE_ON
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
#else /* !XPIXMAPCACHE_ON */
    /* 像素图缓存裁剪时的回退路径：无处缓存，插入语义退化为拒绝并返回
     * false；像素图本身仍由 XIcon 缓存外的正常流程返回给调用方。 */
    (void)prefix; (void)sourceKey; (void)paletteKey; (void)mode;
    (void)width; (void)height; (void)dprThousand; (void)pixmap;
    return false;
#endif /* XPIXMAPCACHE_ON */
}

void XIconScaledPixmapCache_clear(void)
{
#if XPIXMAPCACHE_ON
    XPixmapCache_clear();
#else /* !XPIXMAPCACHE_ON */
    /* 像素图缓存裁剪时的回退路径：无缓存可清，退化为空操作。 */
#endif /* XPIXMAPCACHE_ON */
}
