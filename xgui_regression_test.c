/**
 * @file        xgui_regression_test.c
 * @brief       XGui Qt 对齐回归测试（无平台 API、无菜单依赖）
 */

#include "XPrintf.h"
#include "XIcon.h"
#include "XBitmap.h"
#include "XData/XGeometry.h"
#include "XImage.h"
#include "XImageCodec.h"
#include "XImageReader.h"
#include "XImageWriter.h"
#include "XPicture.h"
#include "XPainter.h"
#include "XPixmap.h"
#include "XPixmapCache.h"
#include "XFile.h"
#include "XString.h"
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
#include "codec_anim_fixture.h"
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */
#include "codec_assets_fixture.h"
#include "XStringList.h"
#include "XVector.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;

static void expect_true(bool condition, const char* name)
{
    if (!condition) {
        XERROR_PRINTF("FAIL: %s\n", name);
        s_failures++;
    }
}

#if XIMAGECODEC_ON
static void make_file_name(XString** out)
{
    *out = XString_create_utf8("xgui_regression.bmp");
}
#endif /* XIMAGECODEC_ON */

typedef struct PictureProbe
{
    int lineCalls;
    int fillCalls;
    int saveCalls;
    int restoreCalls;
    int imageCalls;
    bool imagePayloadOk;
} PictureProbe;

static bool picture_probe_line(XPainter* p, int x1, int y1, int x2, int y2)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    (void)x1; (void)y1; (void)x2; (void)y2;
    ++probe->lineCalls;
    return true;
}

static bool picture_probe_fill(XPainter* p, const XRect* r, uint32_t color)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    (void)r; (void)color;
    ++probe->fillCalls;
    return true;
}

static bool picture_probe_state(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->saveCalls;
    return true;
}

static bool picture_probe_restore(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->restoreCalls;
    return true;
}

static bool picture_probe_image(XPainter* p, const XImage* image, int x, int y)
{
    PictureProbe* probe = (PictureProbe*)p->m_userData;
    ++probe->imageCalls;
    probe->imagePayloadOk = image && x == 8 && y == 9 &&
                            XImage_width(image) == 2 && XImage_height(image) == 1 &&
                            XImage_pixel(image, 0, 0) == 0xff102030u &&
                            XImage_devicePixelRatio(image) == 2.0f;
    return probe->imagePayloadOk;
}

static void test_pixmap_lifecycle(void)
{
    XPixmap source;
    XPixmap empty;
    XPixmap target;
    XPixmap copied;
    XPixmap transformed;
    XImage image;

    XPixmap_init_ex(&source, 2, 3);
    XPixmap_fill(&source, 0xff336699u);
    XPixmap_setDevicePixelRatio(&source, 2.0f);

    memset(&copied, 0, sizeof(copied));
    XPixmap_copy_base(&copied, &source);
    expect_true(XPixmap_width(&copied) == 2 && XPixmap_height(&copied) == 3,
                "copy_base initializes an uninitialized destination");

    XPixmap_init(&transformed);
    XPixmap_transformed(&source, 0.0f, -1.0f, 3.0f,
                        1.0f, 0.0f, 0.0f, 0, &transformed);
    expect_true(XPixmap_width(&transformed) == 3 && XPixmap_height(&transformed) == 2,
                "transformed rotates dimensions");

    XImage_init(&image);
    XPixmap_toImage(&source, &image);
    expect_true(XImage_width(&image) == 2 && XImage_height(&image) == 3,
                "pixmap converts to image");
    expect_true(XImage_devicePixelRatio(&image) == 2.0f,
                "pixmap to image preserves device pixel ratio");

    XPixmap_init(&empty);
    XPixmap_toImage(&empty, &image);
    expect_true(XImage_isNull(&image),
                "pixmap to image clears the output for an empty source");
    XPixmap_init_ex(&target, 1, 1);
    XPixmap_fromImage(NULL, 0, &target);
    expect_true(XPixmap_isNull(&target),
                "pixmap from image clears the output for a null image");

    XImage_deinit_base(&image);
    XPixmap_deinit_base(&target);
    XPixmap_deinit_base(&empty);
    XPixmap_deinit_base(&transformed);
    XPixmap_deinit_base(&copied);
    XPixmap_deinit_base(&source);
}

static void test_icon_sizes(void)
{
    XPixmap first;
    XPixmap second;
    XIcon icon;
    XVector sizes;
    XSize* size0;
    XSize* size1;

    XPixmap_init_ex(&first, 16, 16);
    XPixmap_init_ex(&second, 32, 24);
    XIcon_init_pixmap(&icon, &first);
    XIcon_addPixmap(&icon, &second, XIconMode_Normal, XIconState_Off);
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);

    expect_true(XVector_size_base((const XContainer*)&sizes) == 2, "icon availableSizes returns unique entries");
    size0 = (XSize*)XVector_at_base(&sizes, 0);
    size1 = (XSize*)XVector_at_base(&sizes, 1);
    expect_true(size0 && size1 &&
                ((size0->width == 16 && size1->width == 32) ||
                 (size0->width == 32 && size1->width == 16)),
                "icon availableSizes preserves dimensions");

    XVector_deinit_base((XClass*)&sizes);
    XIcon_deinit_base(&icon);
    XPixmap_deinit_base(&second);
    XPixmap_deinit_base(&first);
}

static void test_geometry_contract(void)
{
    XPoint point;
    XPoint otherPoint;
    XPoint difference;
    XSize size;
    XSize bounded;
    XSize expanded;
    XSize transposed;
    XRect first;
    XRect second;
    XRect result;
    XRect adjusted;
    XRect translated;
    XRect normalized;
    XSize rectSize;
    XPoint topLeft;
    XPoint center;
    XRegion region;
    XRegion otherRegion;
    XRegion chained;

    XPoint_init(&point, 3, -4);
    XPoint_init(&otherPoint, -1, 2);
    expect_true(XPoint_manhattanLength(&point) == 7,
                "point manhattan length matches Qt semantics");
    point = XPoint_add(&point, &otherPoint);
    expect_true(point.x == 2 && point.y == -2,
                "point add returns a value type");
    difference = XPoint_subtract(&point, &otherPoint);
    expect_true(difference.x == 3 && difference.y == -4,
                "point subtract returns a value type");

    XSize_init(&size, 640, 480);
    XSize_scale(&size, 100, 100, 1);
    expect_true(size.width == 100 && size.height == 75,
                "size scale keeps aspect ratio");
    bounded = XSize_boundedTo(&size, &(XSize){80, 90});
    expanded = XSize_expandedTo(&size, &(XSize){80, 90});
    transposed = XSize_transposed(&size);
    expect_true(bounded.width == 80 && bounded.height == 75 &&
                expanded.width == 100 && expanded.height == 90 &&
                transposed.width == 75 && transposed.height == 100,
                "size value APIs return lightweight structs");

    XRect_init(&first, 10, 10, 20, 20);
    XRect_init(&second, 20, 0, 20, 20);
    result = XRect_intersected(&first, &second);
    expect_true(result.x == 20 && result.y == 10 &&
                result.width == 10 && result.height == 10,
                "rect intersection uses half-open bounds");
    result = XRect_united(&first, &second);
    expect_true(result.x == 10 && result.y == 0 &&
                result.width == 30 && result.height == 30,
                "rect union returns bounding rectangle");
    normalized = XRect_normalized(&(XRect){30, 20, -10, -5});
    rectSize = XRect_size(&first);
    topLeft = XRect_topLeft(&first);
    center = XRect_center(&first);
    adjusted = XRect_adjusted(&first, 1, 2, -3, -4);
    translated = XRect_translated(&first, 5, -5);
    expect_true(normalized.x == 20 && normalized.y == 15 &&
                normalized.width == 10 && normalized.height == 5 &&
                rectSize.width == 20 && rectSize.height == 20 &&
                topLeft.x == 10 && topLeft.y == 10 &&
                center.x == 19 && center.y == 19 &&
                adjusted.x == 11 && adjusted.y == 12 &&
                adjusted.width == 16 && adjusted.height == 14 &&
                translated.x == 15 && translated.y == 5,
                "rect value APIs return lightweight structs");

    XRegion_init(&region);
    XRegion_init(&otherRegion);
    XRegion_init(&chained);
    XRegion_addRect(&chained, &(XRect){0, 0, 1, 1});
    XRegion_addRect(&chained, &(XRect){0, 1, 1, 1});
    XRegion_addRect(&chained, &(XRect){0, 2, 1, 1});
    expect_true(chained.count == 1 && chained.rects[0].height == 3,
                "region rectangle merging is transitive");
    XRegion_addRect(&region, &first);
    XRegion_addRect(&region, &(XRect){0, 0, 0, 10});
    XRegion_addRect(&otherRegion, &(XRect){15, 15, 10, 10});
    expect_true(XRegion_contains(&region, 12, 12) &&
                !XRegion_contains(&region, 0, 0),
                "region ignores empty rectangles and tests membership");
    XRegion_intersected(&region, &otherRegion, &region);
    expect_true(XRegion_isEmpty(&region) == false && region.count == 1 &&
                region.rects[0].x == 15 && region.rects[0].y == 15,
                "region intersection supports aliased output");
    XRegion_subtracted(&region, &otherRegion, &region);
    expect_true(XRegion_isEmpty(&region),
                "region subtraction supports aliased output");
    XRegion_deinit(&otherRegion);
    XRegion_deinit(&region);
    XRegion_deinit(&chained);
}

static void test_pixmap_scroll_and_bitmap_alias(void)
{
    XImage image;
    XImage movedImage;
    XPixmap pixmap;
    XPixmap rgbPixmap;
    XRegion exposed;
    XBitmap bitmap;
    XBitmap mask;
    XBitmap transformed;

    XImage_init_ex(&image, 4, 2, XImageFormat_ARGB32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            XImage_setPixel(&image, x, y, 0xff000000u | (uint32_t)(x + 1));
    XPixmap_init_image(&pixmap, &image, 0);
    XImage_deinit_base(&image);
    XRegion_init(&exposed);
    XRegion_addRect(&exposed, &(XRect){99, 99, 1, 1});
    XPixmap_scroll(&pixmap, 1, 0, NULL, &exposed);
    expect_true(exposed.count == 1 && exposed.rects[0].x == 0 &&
                exposed.rects[0].width == 1,
                "pixmap scroll replaces exposed region");
    XImage_init(&movedImage);
    XPixmap_toImage(&pixmap, &movedImage);
    expect_true(XImage_pixel(&movedImage, 1, 0) == 0xff000001u &&
                XImage_pixel(&movedImage, 3, 0) == 0xff000003u,
                "pixmap scroll moves pixels from the pre-scroll image");
    XImage_deinit_base(&movedImage);
    XPixmap_scroll(&pixmap, 0, 0, NULL, &exposed);
    expect_true(XRegion_isEmpty(&exposed),
                "pixmap scroll clears exposed region for a no-op");

    XImage_init_ex(&image, 2, 1, XImageFormat_RGB888);
    XImage_setPixel(&image, 0, 0, 0xffff0000u);
    XImage_setPixel(&image, 1, 0, 0xff00ff00u);
    XPixmap_init_image(&rgbPixmap, &image, 0);
    XBitmap_init_ex(&mask, 2, 1);
    XImage_init(&movedImage);
    XPixmap_toImage((const XPixmap*)&mask, &movedImage);
    XImage_setPixel(&movedImage, 1, 0, 1);
    XBitmap_fromImage(&movedImage, 0, &mask);
    XPixmap_setMask(&rgbPixmap, (const XPixmap*)&mask);
    XImage_deinit_base(&movedImage);
    XPixmap_toImage(&rgbPixmap, &movedImage);
    expect_true(XPixmap_hasAlphaChannel(&rgbPixmap) &&
                (XImage_pixel(&movedImage, 0, 0) >> 24) == 0 &&
                (XImage_pixel(&movedImage, 1, 0) >> 24) == 0xff,
                "pixmap setMask converts RGB sources and applies mask alpha");
    XImage_deinit_base(&movedImage);
    XBitmap_deinit_base(&mask);
    XPixmap_deinit_base(&rgbPixmap);

    XPixmap swapped;
    XPixmap_init_ex(&swapped, 1, 1);
    XPixmap_swap(&pixmap, &swapped);
    expect_true(XPixmap_width(&pixmap) == 1 && XPixmap_width(&swapped) == 4,
                "pixmap swap exchanges data without changing object identity");
    XPixmap_swap(&pixmap, &swapped);
    expect_true(XPixmap_convertFromImage(&pixmap, &image, 0) &&
                XPixmap_width(&pixmap) == 2 && XPixmap_height(&pixmap) == 1,
                "pixmap convertFromImage replaces data only after success");
    XPixmap_deinit_base(&swapped);
    XImage_deinit_base(&image);

    XBitmap_init_ex(&bitmap, 2, 2);
    XBitmap_init(&transformed);
    XBitmap_transformed_2(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &transformed);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&transformed) &&
                XClassGetVtable((const XClass*)&transformed) ==
                    XClassGetVtable((const XClass*)&bitmap),
                "bitmap transformed output keeps bitmap identity");
    XBitmap_transformed_2(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &bitmap);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&bitmap) &&
                XPixmap_width((const XPixmap*)&bitmap) == 2,
                "bitmap transformed supports aliased output");

    XBitmap_deinit_base(&transformed);
    XBitmap_deinit_base(&bitmap);
    XRegion_deinit(&exposed);
    XPixmap_deinit_base(&pixmap);
}

static void test_pixmap_cache_contract(void)
{
    int oldLimit = XPixmapCache_cacheLimit();
    XPixmap p10, p20, p32, out;
    XPixmapCacheKey key, copy, third;
    XRect none;

    (void)none;
    XPixmap_init_ex(&p10, 10, 10);
    XPixmap_init_ex(&p20, 20, 20);
    XPixmap_init_ex(&p32, 32, 32);
    XPixmap_init(&out);
    XPixmapCacheKey_init(&key);
    XPixmapCacheKey_init(&copy);
    XPixmapCacheKey_init(&third);

    /* Key 生命周期：insertKey 后有效，副本共享同一有效性 */
    XPixmapCache_setCacheLimit(64);
    expect_true(XPixmapCache_insertKey(&p10, &key) &&
                XPixmapCacheKey_isValid(&key),
                "cache insertKey makes the key valid");
    XPixmapCacheKey_copy(&copy, &key);
    XPixmapCacheKey_copy(&third, &copy);
    expect_true(XPixmapCache_findKey(&copy, &out) &&
                XPixmap_width(&out) == 10,
                "cache findKey via a key copy hits the same entry");

    /* replace：沿用原 Key，键与所有副本保持有效 */
    expect_true(XPixmapCache_replace(&key, &p20),
                "cache replace updates the pixmap");
    expect_true(XPixmapCacheKey_isValid(&key) &&
                XPixmapCacheKey_isValid(&copy) &&
                XPixmapCacheKey_isValid(&third),
                "cache replace keeps the key and all copies valid");
    expect_true(XPixmapCache_findKey(&key, &out) &&
                XPixmap_width(&out) == 20 &&
                XPixmap_height(&out) == 20,
                "cache replace stores the new pixmap");

    /* findKey 未命中：键立即失效（Qt 语义） */
    XPixmapCache_clear();
    expect_true(!XPixmapCache_findKey(&copy, &out) &&
                !XPixmapCacheKey_isValid(&key) &&
                !XPixmapCacheKey_isValid(&third),
                "cache findKey miss invalidates the key and its copies");

    /* 同一 Key 对象重复 insertKey：不产生孤儿节点，语义为覆盖 */
    expect_true(XPixmapCache_insertKey(&p10, &key),
                "cache re-uses an invalid key object after clear");
    expect_true(XPixmapCache_insertKey(&p20, &key),
                "cache re-insert with the same key object overwrites");
    expect_true(XPixmapCache_findKey(&key, &out) &&
                XPixmap_width(&out) == 20,
                "cache second insertKey replaces the cached pixmap");
    expect_true(XPixmapCacheKey_hash(&key) != 0 &&
                !XPixmapCacheKey_equals(&key, &copy),
                "cache key hash is non-zero and new keys differ");

    /* removeKey：调用后键失效（Qt 语义） */
    XPixmapCacheKey_copy(&copy, &key);
    XPixmapCache_removeKey(&copy);
    expect_true(!XPixmapCacheKey_isValid(&key) &&
                !XPixmapCacheKey_isValid(&copy),
                "cache removeKey invalidates every key copy");

    /* 字符串键：覆盖、大小限制、禁用缓存 */
    expect_true(XPixmapCache_insert_2("duplicate", &p10), "cache string insert");
    expect_true(XPixmapCache_insert_2("duplicate", &p20),
                "cache string insert overwrites the old value");
    expect_true(XPixmapCache_find_2("duplicate", &out) &&
                XPixmap_width(&out) == 20,
                "cache string overwrite stores the newest pixmap");
    XPixmapCache_clear();
    /* 32x32 depth32 估计开销为 4 KB：a、b 共 8 KB，恰好命中 8 KB 限制 */
    XPixmapCache_setCacheLimit(8);
    expect_true(XPixmapCache_insert_2("a", &p32), "cache 32x32 fits the 8 KB limit");
    expect_true(XPixmapCache_insert_2("b", &p32) &&
                XPixmapCache_find_2("a", NULL),
                "cache LRU keeps the recently used head entry");
    expect_true(XPixmapCache_insert_2("c", &p32) &&
                !XPixmapCache_find_2("b", NULL) &&
                XPixmapCache_find_2("c", NULL) &&
                XPixmapCache_find_2("a", NULL),
                "cache LRU evicts the least recently used tail");
    XPixmapCache_setCacheLimit(0);
    expect_true(!XPixmapCache_find_2("a", NULL) &&
                !XPixmapCache_insert_2("rejected", &p32),
                "cache limit 0 disables insertion and purges entries");

    /* 回归：全新 Key 的 insertKey 不得误删字符串键条目（m_data 为 NULL 时
     * 字符串条目 m_keyData 同样为 NULL，旧实现会误删第一条字符串条目） */
    XPixmapCache_setCacheLimit(64);
    expect_true(XPixmapCache_insert_2("keepme", &p10) &&
                XPixmapCache_insertKey(&p20, &copy) &&
                XPixmapCache_find_2("keepme", NULL),
                "cache insertKey with a fresh key keeps string entries");
    XPixmapCache_clear();

    /* 超限像素图直接拒绝 */
    XPixmapCache_setCacheLimit(1);
    expect_true(!XPixmapCache_insertKey(&p32, &copy),
                "cache rejects a pixmap larger than the limit");

    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(oldLimit);
    XPixmapCacheKey_deinit(&third);
    XPixmapCacheKey_deinit(&copy);
    XPixmapCacheKey_deinit(&key);
    XPixmap_deinit_base(&out);
    XPixmap_deinit_base(&p32);
    XPixmap_deinit_base(&p20);
    XPixmap_deinit_base(&p10);
}

#if defined(__unix__)
#include <pthread.h>
#include <stdio.h>

typedef struct CacheStressCtx
{
    unsigned int m_id;
    unsigned int m_iterations;
    unsigned int m_findMisses;      /**< 插入后立即查找失败的次数（应为 0） */
    unsigned int m_keyLifeErrors;   /**< removeKey 后键仍有效的次数（应为 0） */
} CacheStressCtx;

static void* cache_stress_worker(void* arg)
{
    CacheStressCtx* ctx = (CacheStressCtx*)arg;
    unsigned int i;
    for (i = 0; i < ctx->m_iterations; ++i)
    {
        char key[64];
        XPixmap p;
        XPixmapCacheKey k;
        XPixmap_init_ex(&p, 4, 4);
        XPixmapCacheKey_init(&k);
        snprintf(key, sizeof(key), "stress_t%u_iter%u", ctx->m_id, i % 32u);
        XPixmapCache_insert_2(key, &p);
        XPixmapCache_insert_2(key, &p);   /* 并发覆盖路径 */
        if (!XPixmapCache_find_2(key, NULL)) ctx->m_findMisses++;
        XPixmapCache_remove_2(key);
        if (XPixmapCache_insertKey(&p, &k))
        {
            XPixmap q;
            XPixmap_init(&q);
            if (!XPixmapCache_findKey(&k, &q)) ctx->m_findMisses++;
            XPixmap_deinit_base(&q);
            XPixmapCache_replace(&k, &p);  /* 并发原位替换路径 */
            XPixmapCache_removeKey(&k);
            if (XPixmapCacheKey_isValid(&k)) ctx->m_keyLifeErrors++;
        }
        XPixmapCacheKey_deinit(&k);
        XPixmap_deinit_base(&p);
    }
    return NULL;
}

static void test_pixmap_cache_concurrency(void)
{
    enum { kThreads = 4, kIterations = 600, kLimit = 4096 };
    int oldLimit = XPixmapCache_cacheLimit();
    pthread_t threads[kThreads];
    CacheStressCtx ctxs[kThreads];
    unsigned int i;
    unsigned int totalMisses = 0;
    unsigned int totalKeyErrors = 0;
    XPixmapCache_setCacheLimit(kLimit);
    for (i = 0; i < kThreads; ++i)
    {
        ctxs[i].m_id = i;
        ctxs[i].m_iterations = kIterations;
        ctxs[i].m_findMisses = 0;
        ctxs[i].m_keyLifeErrors = 0;
        expect_true(pthread_create(&threads[i], NULL, cache_stress_worker,
                                   &ctxs[i]) == 0,
                    "cache stress thread creation");
    }
    for (i = 0; i < kThreads; ++i)
    {
        pthread_join(threads[i], NULL);
        totalMisses += ctxs[i].m_findMisses;
        totalKeyErrors += ctxs[i].m_keyLifeErrors;
    }
    XPixmapCache_clear();
    XPixmapCache_setCacheLimit(oldLimit);
    expect_true(totalMisses == 0,
                "cache concurrent stress: insert followed by find never misses");
    expect_true(totalKeyErrors == 0,
                "cache concurrent stress: removeKey always invalidates the key");
}
#endif /* __unix__ */

static void test_picture_play_contract(void)
{
    XPicture picture;
    XPainter painter;
    XRect rect = { 2, 3, 4, 5 };
    PictureProbe probe = { 0, 0, 0, 0, 0, false };
    XImage image;

    XPicture_init(&picture, -1);
    /* Qt returns true for an empty recording without touching the painter. */
    expect_true(XPicture_play(&picture, NULL),
                "empty picture play is a successful no-op");
    XPicture_setData(&picture, "commands", 8);
    expect_true(!XPicture_play(&picture, NULL),
                "non-empty picture play stays unsupported without painter dispatch");
    XPicture_clearCommands(&picture);
    XPainter_init(&painter, &probe);
    painter.m_drawLine = picture_probe_line;
    XPicture_recordDrawLine(&picture, 1, 2, 3, 4);
    painter.m_fillRect = picture_probe_fill;
    XPicture_recordFillRect(&picture, &rect, 0xff112233u);
    painter.m_save = picture_probe_state;
    XPicture_recordSave(&picture);
    painter.m_restore = picture_probe_restore;
    XPicture_recordRestore(&picture);
    painter.m_drawImage = picture_probe_image;
    XImage_init_ex(&image, 2, 1, XImageFormat_ARGB32);
    XImage_setPixel(&image, 0, 0, 0xff102030u);
    XImage_setDevicePixelRatio(&image, 2.0f);
    expect_true(XPicture_recordDrawImage(&picture, &image, 8, 9),
                "recordDrawImage stores a self-contained image payload");
    expect_true(XPicture_isValidStream(&picture),
                "recorded picture has a valid portable stream");
    expect_true(XPicture_play(&picture, &painter),
                "recorded picture dispatches through XPainter");
    expect_true(probe.lineCalls == 1 && probe.fillCalls == 1 &&
                probe.saveCalls == 1 && probe.restoreCalls == 1,
                "picture dispatch calls each command exactly once");
    expect_true(probe.imageCalls == 1 && probe.imagePayloadOk,
                "picture replay reconstructs image metadata and pixels");
    {
        XPicture loaded;
        XPicture_init(&loaded, -1);
        expect_true(XPicture_save_2(&picture, "xgui_regression.xpic"),
                    "picture save writes the portable stream");
        expect_true(XPicture_load_2(&loaded, "xgui_regression.xpic") &&
                    XPicture_isValidStream(&loaded) &&
                    XPicture_play(&loaded, &painter),
                    "picture load validates and replays the portable stream");
        remove("xgui_regression.xpic");
        XPicture_deinit_base(&loaded);
    }
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
    XPicture_deinit_base(&picture);
}

static void test_painter_raster_contract(void)
{
    XImage image;
    XPainter painter;
    XRect rect = { 1, 1, 4, 3 };
    XRect outline = { 1, 1, 5, 4 };
    XRect clip = { 2, 2, 3, 3 };
    XRect one = { 0, 0, 1, 1 };
    XRect empty = { 0, 0, 0, 0 };
    XImage tile;

    XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &image),
                "painter begins raster on image");
    expect_true(XPainter_device(&painter) == &image,
                "painter device reports the bound image");

    /* 不透明填充走 XImage_fillRect 快速路径 */
    expect_true(XPainter_fillRect(&painter, &rect, 0xff336699u),
                "raster fillRect");
    expect_true(XImage_pixel(&image, 1, 1) == 0xff336699u,
                "raster fill top-left");
    expect_true(XImage_pixel(&image, 4, 3) == 0xff336699u,
                "raster fill bottom-right");
    expect_true(XImage_pixel(&image, 5, 1) == 0, "raster fill right margin");
    expect_true(XImage_pixel(&image, 0, 0) == 0, "raster fill left margin");

    /* 矩形边框（不透明画笔，角点保持精确） */
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawRect(&painter, &outline), "raster drawRect");
    expect_true(XImage_pixel(&image, 1, 1) == 0xffff0000u,
                "raster rect corner tl");
    expect_true(XImage_pixel(&image, 5, 1) == 0xffff0000u,
                "raster rect corner tr");
    expect_true(XImage_pixel(&image, 5, 4) == 0xffff0000u,
                "raster rect corner br");
    expect_true(XImage_pixel(&image, 3, 2) == 0xff336699u,
                "raster rect interior kept");
    expect_true(XImage_pixel(&image, 2, 5) == 0, "raster rect outside");

    /* 画线及笔宽 */
    XPainter_setPen(&painter, 0xff00ff00u);
    expect_true(XPainter_drawLine(&painter, 2, 6, 6, 6), "raster drawLine");
    expect_true(XImage_pixel(&image, 2, 6) == 0xff00ff00u,
                "raster line start");
    expect_true(XImage_pixel(&image, 6, 6) == 0xff00ff00u, "raster line end");
    expect_true(XImage_pixel(&image, 4, 6) == 0xff00ff00u,
                "raster line middle");
    expect_true(XImage_pixel(&image, 2, 5) == 0, "raster line above");
    XPainter_setPenWidth(&painter, 3);
    XPainter_setPen(&painter, 0xffffff00u);
    expect_true(XPainter_drawLine(&painter, 0, 7, 3, 7), "raster thick line");
    expect_true(XImage_pixel(&image, 0, 6) == 0xffffff00u,
                "raster thick line upper");
    expect_true(XImage_pixel(&image, 0, 7) == 0xffffff00u,
                "raster thick line main");
    expect_true(XImage_pixel(&image, 0, 5) == 0, "raster thick line boundary");
    XPainter_setPenWidth(&painter, 1);

    /* 裁剪矩形（设备坐标，在变换之后生效） */
    XPainter_setClipRect(&painter, &clip);
    expect_true(XPainter_hasClipping(&painter), "raster clip active");
    rect.x = 2; rect.y = 2; rect.width = 4; rect.height = 4;
    expect_true(XPainter_fillRect(&painter, &rect, 0xff0000ffu),
                "raster clipped fill");
    expect_true(XImage_pixel(&image, 2, 2) == 0xff0000ffu,
                "raster clip inside");
    expect_true(XImage_pixel(&image, 4, 3) == 0xff0000ffu,
                "raster clip inside 2");
    expect_true(XImage_pixel(&image, 5, 2) == 0xffff0000u,
                "raster clip keeps previous outline");
    expect_true(XImage_pixel(&image, 5, 5) == 0, "raster clip lower margin");
    XPainter_setClipRect(&painter, NULL);
    expect_true(!XPainter_hasClipping(&painter), "raster clip cleared");

    /* save/restore 恢复画笔/画刷/变换 */
    XPainter_setPen(&painter, 0xff00ff00u);
    XPainter_setBrush(&painter, 0xff112233u);
    expect_true(XPainter_save(&painter), "raster save");
    XPainter_setPen(&painter, 0xffff00ffu);
    XPainter_setBrush(&painter, 0xffaabbccu);
    XPainter_translate(&painter, 3.0f, 4.0f);
    expect_true(XPainter_restore(&painter), "raster restore");
    expect_true(XPainter_penColor(&painter) == 0xff00ff00u,
                "raster pen restored");
    expect_true(XPainter_brushColor(&painter) == 0xff112233u,
                "raster brush restored");
    {
        XImageTransform t;
        XPainter_transform(&painter, &t);
        expect_true(t.dx == 0.0f && t.dy == 0.0f &&
                    t.m11 == 1.0f && t.m22 == 1.0f,
                    "raster transform restored");
    }

    /* 半透明 SourceOver 合成（整数 Porter-Duff） */
    XImage_setPixel(&image, 0, 0, 0xff204060u);
    XPainter_setOpacity(&painter, 0.5f);
    expect_true(XPainter_fillRect(&painter, &one, 0xff4080ffu),
                "raster translucent fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0xff3060b0u,
                "raster 50% SourceOver blend exact");

    /* Source 模式整值替换目标 */
    XPainter_setOpacity(&painter, 1.0f);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    expect_true(XPainter_fillRect(&painter, &one, 0x80406080u),
                "raster source mode fill");
    expect_true(XImage_pixel(&image, 0, 0) == 0x80406080u,
                "raster source mode replaces destination");
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);

    /* 平移变换 */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_resetTransform(&painter);
    XPainter_translate(&painter, 2.0f, 3.0f);
    XPainter_setPen(&painter, 0xffffffffu);
    expect_true(XPainter_drawPoint(&painter, 0, 0),
                "raster translated point");
    expect_true(XImage_pixel(&image, 2, 3) == 0xffffffffu,
                "raster translate draws shifted");
    expect_true(XImage_pixel(&image, 0, 0) == 0,
                "raster translate leaves origin");
    XPainter_resetTransform(&painter);

    /* 旋转变换：顺时针 90° 把 (1,0) 映射到 (0,1) */
    XImage_fillRect(&image, NULL, 0u);
    XPainter_rotate(&painter, 90.0f);
    expect_true(XPainter_drawPoint(&painter, 1, 0), "raster rotated point");
    expect_true(XImage_pixel(&image, 0, 1) == 0xffffffffu,
                "raster rotate maps point clockwise");
    expect_true(XImage_pixel(&image, 1, 0) == 0,
                "raster rotate clears old position");
    XPainter_resetTransform(&painter);

    /* 绘制图像（恒等变换快速路径） */
    XImage_init_ex(&tile, 2, 2, XImageFormat_ARGB32);
    XImage_fillRect(&tile, NULL, 0xff00ff00u);
    XImage_fillRect(&image, NULL, 0u);
    expect_true(XPainter_drawImage(&painter, &tile, 3, 4), "raster drawImage");
    expect_true(XImage_pixel(&image, 3, 4) == 0xff00ff00u,
                "raster image px0");
    expect_true(XImage_pixel(&image, 4, 5) == 0xff00ff00u,
                "raster image px1");
    expect_true(XImage_pixel(&image, 2, 4) == 0, "raster image margin");
    expect_true(XPainter_drawImage(&painter, NULL, 0, 0) == false,
                "raster drawImage null rejected");
    XImage_deinit_base(&tile);

    /* 空矩形/空图像语义 */
    expect_true(XPainter_drawRect(&painter, NULL), "raster null drawRect no-op");
    expect_true(XPainter_drawRect(&painter, &empty),
                "raster empty drawRect no-op");
    expect_true(XPainter_fillRect(&painter, &empty, 0xff000000u),
                "raster empty fillRect no-op");
    expect_true(XPainter_fillRect(&painter, NULL, 0xff000000u) == false,
                "raster null fillRect rejected");

    /* 结束与解绑 */
    expect_true(XPainter_end(&painter), "raster end");
    expect_true(!XPainter_isActive(&painter), "raster inactive after end");
    expect_true(XPainter_drawLine(&painter, 0, 0, 1, 1) == false,
                "raster unbound drawLine rejected");
    XPainter_deinit(&painter);
    XImage_deinit_base(&image);
}

static void test_painter_record_play_contract(void)
{
    XPicture picture;
    XPicture loaded;
    XPainter painter;
    XImage target;
    XImage source;
    XRect rect = { 1, 1, 4, 4 };

    XPicture_init(&picture, -1);
    XPicture_init(&loaded, -1);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_picture(&painter, &picture),
                "painter begins recording");
    expect_true(XPainter_device(&painter) == &picture,
                "painter device reports the bound picture");

    /* 先填充后描边，回放时描边覆盖在填充上 */
    expect_true(XPainter_fillRect(&painter, &rect, 0xff00ff00u),
                "record fillRect");
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPainter_drawRect(&painter, &rect), "record drawRect");
    XImage_init_ex(&source, 2, 1, XImageFormat_ARGB32);
    XImage_fillRect(&source, NULL, 0xff0000ffu);
    expect_true(XPainter_drawImage(&painter, &source, 6, 6),
                "record drawImage");
    expect_true(XPainter_save(&painter) && XPainter_restore(&painter),
                "record save/restore");
    expect_true(XPainter_end(&painter), "recording end");
    expect_true(!XPainter_isActive(&painter), "recording ends inactive");
    expect_true(XPicture_isValidStream(&picture), "recorded stream valid");
    expect_true(XPicture_size(&picture) != 0, "recorded stream has commands");

    /* 文件往返 */
    expect_true(XPicture_save_2(&picture, "xgui_painter.xpic"),
                "recorded picture saves to file");
    expect_true(XPicture_load_2(&loaded, "xgui_painter.xpic") &&
                XPicture_isValidStream(&loaded),
                "recorded picture reloads from file");
    remove("xgui_painter.xpic");

    /* 用软件光栅后端回放（先设置与录制时一致的画笔） */
    XImage_init_ex(&target, 10, 10, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    expect_true(XPainter_begin_image(&painter, &target),
                "painter begins image replay");
    XPainter_setPen(&painter, 0xffff0000u);
    expect_true(XPicture_play(&loaded, &painter), "picture replays");
    expect_true(XImage_pixel(&target, 1, 1) == 0xffff0000u,
                "replay outline corner tl");
    expect_true(XImage_pixel(&target, 4, 4) == 0xffff0000u,
                "replay outline corner br");
    expect_true(XImage_pixel(&target, 1, 2) == 0xffff0000u,
                "replay outline left edge");
    expect_true(XImage_pixel(&target, 2, 2) == 0xff00ff00u,
                "replay interior fill");
    expect_true(XImage_pixel(&target, 5, 1) == 0, "replay outside rect");
    expect_true(XImage_pixel(&target, 6, 6) == 0xff0000ffu,
                "replay recorded image px0");
    expect_true(XImage_pixel(&target, 7, 6) == 0xff0000ffu,
                "replay recorded image px1");
    expect_true(XImage_pixel(&target, 5, 5) == 0, "replay image margin");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XImage_deinit_base(&target);
    XImage_deinit_base(&source);
    XPicture_deinit_base(&loaded);
    XPicture_deinit_base(&picture);
}


static void test_icon_matching(void)
{
    XPixmap normal;
    XPixmap normalLarge;
    XPixmap active;
    XPixmap out;
    XIcon fallbackIcon;
    XIcon sizeIcon;

    XPixmap_init_ex(&normal, 16, 16);
    XPixmap_init_ex(&normalLarge, 32, 32);
    XPixmap_init_ex(&active, 20, 20);
    XIcon_init_pixmap(&fallbackIcon, &normal);
    XIcon_addPixmap(&fallbackIcon, &active, XIconMode_Active, XIconState_Off);
    XPixmap_init(&out);

    /* Qt's Disabled fallback checks Normal before Active, even when Active
     * has a closer-sized source. */
    XIcon_pixmap(&fallbackIcon, 20, 20, XIconMode_Disabled, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 16 && XPixmap_height(&out) == 16,
                "icon mode fallback prefers Normal over Active");
    XPixmap_deinit_base(&out);

    /* Within one mode/state, Qt chooses the smallest source not below the
     * request and scales it to the requested size. */
    XIcon_init_pixmap(&sizeIcon, &normal);
    XIcon_addPixmap(&sizeIcon, &normalLarge, XIconMode_Normal, XIconState_Off);
    XPixmap_init(&out);
    XIcon_pixmap(&sizeIcon, 24, 24, XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 24 && XPixmap_height(&out) == 24,
                "icon matching uses the smallest sufficient source");

    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&sizeIcon);
    XIcon_deinit_base(&fallbackIcon);
    XPixmap_deinit_base(&active);
    XPixmap_deinit_base(&normalLarge);
    XPixmap_deinit_base(&normal);
}

static void test_icon_device_pixel_ratio(void)
{
    XPixmap normal;
    XPixmap highResolution;
    XPixmap out;
    XIcon icon;
    XIcon highIcon;

    XPixmap_init_ex(&normal, 32, 32);
    XIcon_init_pixmap(&icon, &normal);
    XPixmap_init(&out);
    XIcon_pixmapRatio(&icon, 32, 32, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 32 && XPixmap_height(&out) == 32 &&
                XPixmap_devicePixelRatio(&out) == 1.0f,
                "icon DPR falls back when only normal-resolution pixels exist");
    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&icon);

    XPixmap_init_ex(&highResolution, 64, 64);
    XPixmap_setDevicePixelRatio(&highResolution, 2.0f);
    XIcon_init_pixmap(&highIcon, &highResolution);
    XPixmap_init(&out);
    XIcon_pixmapRatio(&highIcon, 32, 32, 2.0f,
                      XIconMode_Normal, XIconState_Off, &out);
    expect_true(XPixmap_width(&out) == 64 && XPixmap_height(&out) == 64 &&
                XPixmap_devicePixelRatio(&out) == 2.0f,
                "icon DPR preserves a matching high-resolution source");

    XPixmap_deinit_base(&out);
    XIcon_deinit_base(&highIcon);
    XPixmap_deinit_base(&highResolution);
    XPixmap_deinit_base(&normal);
}

#if XIMAGECODEC_ON
static void test_icon_add_file_size(void)
{
    XImage image;
    XIcon icon;
    XVector sizes;
    XSize* size;

    XImage_init_ex(&image, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(XImage_save_2(&image, "xgui_icon_add_file.bmp", "BMP", -1),
                "writes icon addFile fixture");

    XIcon_init(&icon);
    XIcon_addFile_2(&icon, "xgui_icon_add_file.bmp", 8, 6,
                  XIconMode_Normal, XIconState_Off);
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);
    size = XVector_size_base((const XContainer*)&sizes) == 1
        ? (XSize*)XVector_at_base(&sizes, 0) : NULL;
    expect_true(size && size->width == 8 && size->height == 6,
                "icon addFile stores the requested raster size");

    XVector_deinit_base((XClass*)&sizes);
    XIcon_deinit_base(&icon);
    XImage_deinit_base(&image);
    remove("xgui_icon_add_file.bmp");
}
#endif /* XIMAGECODEC_ON */

#if XIMAGECODEC_ON
static void test_image_device_io(void)
{
    XString* file_name = NULL;
    XFile file;
    XImage source;
    XImage loaded;
    XImageWriter writer;
    XImageReader reader;
    bool ok;

    make_file_name(&file_name);
    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xffff0000u);
    XImage_setPixel(&source, 1, 0, 0xff00ff00u);
    XImage_setPixel(&source, 2, 0, 0xff0000ffu);

    XFile_init_2(&file, file_name);
    ok = XFile_open_2(&file, XIODevice_WriteOnly, 0);
    expect_true(ok, "opens BMP output device");
    XImageWriter_init_device_2(&writer, (XIODevice*)&file, "BMP");
    expect_true(XImageWriter_canWrite(&writer), "device writer reports BMP support");
    expect_true(XImageWriter_write(&writer, &source), "device writer writes BMP");
    XImageWriter_deinit_base(&writer);
    XIODevice_close_base((XIODevice*)&file);
    XClass_deinit_base((XClass*)&file);

    XFile_init_2(&file, file_name);
    ok = XFile_open_2(&file, XIODevice_ReadOnly, 0);
    expect_true(ok, "opens BMP input device");
    XImageReader_init_device_2(&reader, (XIODevice*)&file, "BMP");
    expect_true(XImageReader_canRead(&reader), "device reader detects BMP");
    XImage_init(&loaded);
    expect_true(XImageReader_read(&reader, &loaded), "device reader reads BMP");
    expect_true(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2,
                "device reader preserves dimensions");
    expect_true(XImage_pixel(&loaded, 0, 0) == 0xffff0000u,
                "device BMP round trip preserves pixels");

    XImage_deinit_base(&loaded);
    XImageReader_deinit_base(&reader);
    XIODevice_close_base((XIODevice*)&file);
    XClass_deinit_base((XClass*)&file);
    XFile_remove_static(file_name);
    XString_delete_base((XClass*)file_name);
    XImage_deinit_base(&source);
}

static void test_image_handler_registry(void)
{
    XStringList* readerFormats = XImageReader_supportedImageFormats();
    XStringList* readerMimes = XImageReader_supportedMimeTypes();
    XStringList* readerBmp = XImageReader_imageFormatsForMimeType_2("IMAGE/BMP");
    XStringList* writerFormats = XImageWriter_supportedImageFormats();
    XStringList* writerUnknown = XImageWriter_imageFormatsForMimeType_2("image/png");
    const XString* format;

    format = readerFormats ? (const XString*)XStringList_at_base((const XVector*)readerFormats, 0) : NULL;
    expect_true(readerFormats && XStringList_size_base((const XContainer*)readerFormats) == 5 && format &&
                XString_equals_utf8(format, "bmp", XChar_CaseSensitive), "reader registry exposes BMP codec");
    expect_true(readerMimes && XStringList_size_base((const XContainer*)readerMimes) == 5,
                "reader registry exposes built-in MIME types");
    expect_true(readerBmp && XStringList_size_base((const XContainer*)readerBmp) == 1,
                "reader MIME lookup is case insensitive");
    format = writerFormats ? (const XString*)XStringList_at_base((const XVector*)writerFormats, 0) : NULL;
    expect_true(writerFormats && XStringList_size_base((const XContainer*)writerFormats) == 5 && format &&
                XString_equals_utf8(format, "bmp", XChar_CaseSensitive), "writer registry exposes BMP codec");
    expect_true(writerUnknown && XStringList_size_base((const XContainer*)writerUnknown) == 1,
                "writer MIME lookup returns the matching codec format");

    if (readerFormats) XStringList_delete_base((XClass*)readerFormats);
    if (readerMimes) XStringList_delete_base((XClass*)readerMimes);
    if (readerBmp) XStringList_delete_base((XClass*)readerBmp);
    if (writerFormats) XStringList_delete_base((XClass*)writerFormats);
    if (writerUnknown) XStringList_delete_base((XClass*)writerUnknown);
}

static void test_image_codec_round_trip(void)
{
    XImage source, decoded;
    XByteArray* encoded;
    const XImageCodecFormat formats[] = {XImageCodecFormat_Bmp, XImageCodecFormat_Png,
                                         XImageCodecFormat_Jpeg, XImageCodecFormat_Gif,
                                         XImageCodecFormat_Svg};
    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_setPixel(&source, 0, 0, 0xffff0000u); XImage_setPixel(&source, 1, 0, 0xff00ff00u); XImage_setPixel(&source, 2, 0, 0xff0000ffu);
    XImage_setPixel(&source, 0, 1, 0x80402010u); XImage_setPixel(&source, 1, 1, 0xffffffffu); XImage_setPixel(&source, 2, 1, 0xff102030u);
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        encoded = XByteArray_create(); XImage_init(&decoded);
        expect_true(encoded && XImageCodec_canEncode(formats[i]) && XImageCodec_encode(&source, formats[i], -1, encoded), "codec encodes format independently");
        expect_true(encoded && XImageCodec_detect(XByteArray_data(encoded), XByteArray_size_base((const XContainer*)encoded)) == formats[i], "codec detects encoded format");
        expect_true(encoded && XImageCodec_decode(XByteArray_data(encoded), XByteArray_size_base((const XContainer*)encoded), XImageCodecFormat_Unknown, &decoded), "codec decodes format independently");
        expect_true(XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2, "codec round trip preserves dimensions");
        XImage_deinit_base(&decoded); if (encoded) XByteArray_delete_base((XClass*)encoded);
    }
    XImage_init(&decoded);
    expect_true(XImage_save_2(&source, "xgui_codec.png", "png", -1), "XImage delegates PNG file save to codec");
    expect_true(XImage_load_2(&decoded, "xgui_codec.png", "png") && XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2,
                "XImage delegates PNG file load to codec");
    { XString* codecFile = XString_create_utf8("xgui_codec.png"); XFile_remove_static(codecFile); if (codecFile) XString_delete_base((XClass*)codecFile); }
    XImage_deinit_base(&decoded);
    XImage_deinit_base(&source);
}


/* 逐像素比对：所有像素必须与期望表完全一致 */
static bool pixels_equal_exact(const XImage* image, const uint32_t* expected,
                               int width, int height)
{
    if (!image || XImage_isNull(image) ||
        XImage_width(image) != width || XImage_height(image) != height)
        return false;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (XImage_pixel(image, x, y) != expected[y * width + x])
                return false;
    return true;
}

static void test_codec_pixel_round_trip(void)
{
    /* 各格式独立像素级往返：BMP/PNG/SVG 逐像素一致（含 Alpha），
       GIF 使用量化调色板，仅校验尺寸 */
    static const uint32_t px[6] = {
        0xffff0000u, 0xff00ff01u, 0x800000ffu,
        0x80402010u, 0xffffffffu, 0xff102030u
    };
    XImage source, decoded;
    XByteArray* encoded = NULL;
    const XImageCodecFormat exactFormats[] = {
        XImageCodecFormat_Bmp, XImageCodecFormat_Png, XImageCodecFormat_Svg
    };

    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            XImage_setPixel(&source, x, y, px[y * 3 + x]);

    for (size_t i = 0; i < sizeof(exactFormats) / sizeof(exactFormats[0]); ++i) {
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&source, exactFormats[i], -1, encoded),
                    "codec pixel-encodes format");
        expect_true(encoded &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       exactFormats[i], &decoded),
                    "codec pixel-decodes format with explicit hint");
        expect_true(pixels_equal_exact(&decoded, px, 3, 2),
                    "codec pixel-exact round trip (explicit hint)");
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
    }

    /* GIF：量化解码后仅校验尺寸与非空 */
    encoded = XByteArray_create();
    XImage_init(&decoded);
    expect_true(encoded && XImageCodec_encode(&source, XImageCodecFormat_Gif, -1, encoded) &&
                XImageCodec_decode(XByteArray_data(encoded),
                                   XByteArray_size_base((const XContainer*)encoded),
                                   XImageCodecFormat_Unknown, &decoded),
                "GIF 往返可解");
    expect_true(XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2,
                "GIF 往返保持尺寸");

    XImage_deinit_base(&decoded);
    if (encoded) XByteArray_delete_base((XClass*)encoded);

    /* JPEG：有损格式，使用 24x16 平滑渐变做往返，容差校验单通道误差 */
    {
        XImage jpegSrc;
        XImage_init_ex(&jpegSrc, 24, 16, XImageFormat_ARGB32);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 24; ++x) {
                uint32_t r = (uint32_t)(x * 255 / 23);
                uint32_t g = (uint32_t)(y * 255 / 15);
                uint32_t b = (uint32_t)((x + y) * 127 / 38);
                XImage_setPixel(&jpegSrc, x, y, 0xff000000u | (r << 16) | (g << 8) | b);
            }
        }
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&jpegSrc, XImageCodecFormat_Jpeg, 75, encoded) &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       XImageCodecFormat_Unknown, &decoded),
                    "JPEG 有损往返可解");
        expect_true(XImage_width(&decoded) == 24 && XImage_height(&decoded) == 16,
                    "JPEG 有损往返保持尺寸");
        if (!XImage_isNull(&decoded) && XImage_width(&decoded) == 24 &&
            XImage_height(&decoded) == 16) {
            int maxErr = 0;
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    uint32_t a = XImage_pixel(&jpegSrc, x, y);
                    uint32_t d = XImage_pixel(&decoded, x, y);
                    int dr = (int)((a >> 16) & 0xffu) - (int)((d >> 16) & 0xffu);
                    int dg = (int)((a >> 8) & 0xffu) - (int)((d >> 8) & 0xffu);
                    int db = (int)(a & 0xffu) - (int)(d & 0xffu);
                    if (dr < 0) dr = -dr;
                    if (dg < 0) dg = -dg;
                    if (db < 0) db = -db;
                    if (dr > maxErr) maxErr = dr;
                    if (dg > maxErr) maxErr = dg;
                    if (db > maxErr) maxErr = db;
                }
            }
            expect_true(maxErr <= 32, "JPEG 有损误差在容差范围内");
            if (maxErr > 32)
                XERROR_PRINTF("JPEG 最大通道误差=%d\n", maxErr);
        }
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
        XImage_deinit_base(&jpegSrc);
    }
    XImage_deinit_base(&source);
}

static void test_codec_reject_malformed(void)
{
    static const uint8_t emptyPng[] = {0};
    static const uint8_t truncatedPng[12] = {
        0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0, 0
    };
    static const uint8_t corruptBmp[20] = {
        'B', 'M', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0
    };
    static const uint8_t garbage[8] = {0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4};
    XImage out;
    XImage nullImage;
    XByteArray* encoded = NULL;

    XImage_init(&out);
    XImage_init(&nullImage);
    expect_true(!XImageCodec_decode(NULL, 0, XImageCodecFormat_Bmp, &out),
                "null decode rejected");
    expect_true(!XImageCodec_decode(emptyPng, 0, XImageCodecFormat_Png, &out),
                "empty data rejected");
    expect_true(!XImageCodec_decode(truncatedPng, sizeof(truncatedPng),
                                    XImageCodecFormat_Png, &out),
                "truncated PNG rejected");
    expect_true(!XImageCodec_decode(corruptBmp, sizeof(corruptBmp),
                                    XImageCodecFormat_Bmp, &out),
                "corrupt BMP rejected");
    expect_true(!XImageCodec_decode(garbage, sizeof(garbage),
                                    XImageCodecFormat_Unknown, &out),
                "garbage data rejected");
    encoded = XByteArray_create();
    expect_true(encoded && !XImageCodec_encode(NULL, XImageCodecFormat_Png, -1, encoded),
                "null image encode rejected");
    expect_true(encoded && !XImageCodec_encode(&nullImage, XImageCodecFormat_Png, -1, encoded),
                "null (empty) XImage encode rejected");
    /* JPEG 已有内置后端：正常图像编码应成功，空图像仍应被拒绝 */
    {
        XImage jpegSrc;
        XImage_init_ex(&jpegSrc, 16, 16, XImageFormat_ARGB32);
        XImage_fill(&jpegSrc, 0xff336699u);
        expect_true(XImageCodec_encode(&jpegSrc, XImageCodecFormat_Jpeg, 75, encoded),
                    "JPEG 编码应成功");
        expect_true(!XImageCodec_encode(&out, XImageCodecFormat_Jpeg, -1, encoded),
                    "JPEG 空图像编码被拒绝");
        XImage_deinit_base(&jpegSrc);
    }
    XImage_deinit_base(&out);
    XImage_deinit_base(&nullImage);
    if (encoded) XByteArray_delete_base((XClass*)encoded);
}

static void test_codec_detect_only(void)
{
    static const uint8_t jpegHeader[] = {0xff, 0xd8, 0xff, 0xe0};
    static const uint8_t gifHeader[] = {'G', 'I', 'F', '8', '9', 'a'};
    static const uint8_t bmpHeader[] = {'B', 'M', 0, 0};
    static const uint8_t svgHeader[] = {'<', 's', 'v', 'g', ' ', 'x'};
    XImage out;
    XImage_init(&out);
    expect_true(XImageCodec_detect(jpegHeader, sizeof(jpegHeader)) == XImageCodecFormat_Jpeg,
                "JPEG 文件头可识别");
    expect_true(XImageCodec_canDecode(XImageCodecFormat_Jpeg) &&
                XImageCodec_canEncode(XImageCodecFormat_Jpeg),
                "JPEG 编解码后端可用");
    expect_true(!XImageCodec_decode(jpegHeader, sizeof(jpegHeader),
                                    XImageCodecFormat_Unknown, &out),
                "JPEG 裸文件头数据解码被拒绝");
    expect_true(XImageCodec_detect(gifHeader, sizeof(gifHeader)) == XImageCodecFormat_Gif &&
                XImageCodec_detect(bmpHeader, sizeof(bmpHeader)) == XImageCodecFormat_Bmp &&
                XImageCodec_detect(svgHeader, sizeof(svgHeader)) == XImageCodecFormat_Svg,
                "GIF/BMP/SVG 文件头识别");
    expect_true(XImageCodec_detect(NULL, 0) == XImageCodecFormat_Unknown,
                "空数据识别为 Unknown");
    expect_true(XImageCodec_formatFromName_2("PNG") == XImageCodecFormat_Png &&
                XImageCodec_formatFromName_2("svgz") == XImageCodecFormat_Svg &&
                XImageCodec_formatFromName_2("webp") == XImageCodecFormat_Unknown,
                "格式名解析大小写/别名/未知");
    XImage_deinit_base(&out);
}

static void test_codec_decode_real_assets(void)
{
    struct AssetSpec { const char* file; int width; int height; };
    static const struct AssetSpec assets[] = {
        {"assets/https.png",      670, 304},
        {"assets/运行.png",       794, 411},
        {"assets/配置cmake.png",  638, 920},
        {"assets/分支.png",      1170, 480},
        {"assets/VS克隆储存库.png", 1268, 844},
        {"assets/克隆信息.png",   1268, 844},
    };
    XImage image;
    size_t i;

    for (i = 0; i < sizeof(assets) / sizeof(assets[0]); ++i) {
        XImage_init(&image);
        expect_true(XImage_load_2(&image, assets[i].file, "png") &&
                    XImage_width(&image) == assets[i].width &&
                    XImage_height(&image) == assets[i].height,
                    "decodes real PNG asset with correct dimensions");
        if (!XImage_isNull(&image)) {
            uint32_t sample = XImage_pixel(&image, 0, 0);
            expect_true((sample >> 24) == 0xffu,
                        "real PNG decodes opaque alpha");
        }
        XImage_deinit_base(&image);
    }
    /* 不存在的文件必须失败 */
    XImage_init(&image);
    expect_true(!XImage_load_2(&image, "assets/not-exist-anyway.png", "png"),
                "nonexistent asset file rejected");
    XImage_deinit_base(&image);
}


/* ================ PNG 扩展特性测试 ================ */

static void test_codec_png_palette_round_trip(void)
{
    /* 全不透明 Indexed8：编码为调色板 PNG，解码还原 Indexed8 + 索引 + 色表 */
    static const uint32_t opaquePalette[4] = {
        0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffffffu
    };
    static const int idxTable[4][4] = {
        {0, 1, 2, 3}, {3, 1, 2, 0}, {2, 3, 0, 1}, {0, 1, 2, 3}
    };
    XImage src, decoded;
    XByteArray* encoded = NULL;
    uint32_t gotPalette[4];
    int got;

    XImage_init_ex(&src, 4, 4, XImageFormat_Indexed8);
    XImage_setColorTable(&src, opaquePalette, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            XImage_setPixel(&src, x, y, (uint32_t)idxTable[y][x]);

    encoded = XByteArray_create();
    XImage_init(&decoded);
    expect_true(encoded && XImageCodec_encode(&src, XImageCodecFormat_Png, -1, encoded),
                "Indexed8 图像编码为调色板 PNG");
    expect_true(encoded &&
                XImageCodec_decode(XByteArray_data(encoded),
                                   XByteArray_size_base((const XContainer*)encoded),
                                   XImageCodecFormat_Png, &decoded),
                "调色板 PNG 解码成功");
    expect_true(XImage_format(&decoded) == XImageFormat_Indexed8,
                "无透明调色板 PNG 解码输出 Indexed8");
    got = XImage_colorTable(&decoded, gotPalette, 4);
    expect_true(got == 4 && !memcmp(gotPalette, opaquePalette, sizeof(opaquePalette)),
                "调色板 PNG 色表还原一致");
    expect_true(XImage_pixelIndex(&decoded, 0, 1) == 3 &&
                XImage_pixelIndex(&decoded, 3, 3) == 3 &&
                XImage_pixelIndex(&decoded, 2, 0) == 2,
                "调色板 PNG 索引像素还原一致");
    expect_true(XImage_pixel(&decoded, 0, 0) == 0xffff0000u &&
                XImage_pixel(&decoded, 1, 1) == 0xff00ff00u,
                "调色板 PNG 像素颜色还原一致");
    XImage_deinit_base(&decoded);
    if (encoded) XByteArray_delete_base((XClass*)encoded);

    /* 半透明 Indexed8：编码时写 tRNS，解码应展开为 ARGB32 且保留 Alpha */
    {
        static const uint32_t alphaPalette[4] = {
            0x80ff0000u, 0xff00ff00u, 0xff0000ffu, 0x40ffff00u
        };
        XImage srcA;
        XImage_init_ex(&srcA, 3, 2, XImageFormat_Indexed8);
        XImage_setColorTable(&srcA, alphaPalette, 4);
        XImage_setPixel(&srcA, 0, 0, 0); XImage_setPixel(&srcA, 1, 0, 1);
        XImage_setPixel(&srcA, 2, 0, 2); XImage_setPixel(&srcA, 0, 1, 3);
        XImage_setPixel(&srcA, 1, 1, 0); XImage_setPixel(&srcA, 2, 1, 1);
        encoded = XByteArray_create();
        XImage_init(&decoded);
        expect_true(encoded && XImageCodec_encode(&srcA, XImageCodecFormat_Png, -1, encoded) &&
                    XImageCodec_decode(XByteArray_data(encoded),
                                       XByteArray_size_base((const XContainer*)encoded),
                                       XImageCodecFormat_Png, &decoded),
                    "半透明 Indexed8 编码/解码成功");
        expect_true(XImage_format(&decoded) == XImageFormat_ARGB32,
                    "含 tRNS 调色板 PNG 解码展开为 ARGB32");
        expect_true(XImage_pixel(&decoded, 0, 0) == 0x80ff0000u &&
                    XImage_pixel(&decoded, 1, 0) == 0xff00ff00u &&
                    XImage_pixel(&decoded, 0, 1) == 0x40ffff00u,
                    "tRNS Alpha 保留");
        XImage_deinit_base(&decoded);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
        XImage_deinit_base(&srcA);
    }
    XImage_deinit_base(&src);
}

static void test_codec_png_extended_assets(void)
{
    /* 调色板/子位深/16 位/Adam7/tRNS 资产解码验证
       （资产字节内嵌于 codec_assets_fixture.h，由 zlib+struct 生成） */
    struct Case {
        const char* file; int w, h;
        XImageFormat fmt;
        uint32_t p0, p1, p2;   /* (0,0) (2,0) (0,2) 等关键点，0 表示按格式校验 */
    };
    static const struct Case cases[] = {
        {"codec_palette8.png", 6, 4, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0x0000ff00u},
        {"codec_palette_trns.png", 6, 4, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0xff00ff00u},
        {"codec_palette_fullalpha.png", 6, 4, XImageFormat_Indexed8,
         0xffff0000u, 0x000000ffu, 0xff00ff00u},
        {"codec_palette1.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xffffffffu, 0xff00ff00u},
        {"codec_palette2.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xff0000ffu, 0xffffffffu},
        {"codec_palette4.png", 8, 2, XImageFormat_Indexed8,
         0xffff0000u, 0xff00ff00u, 0xffffffffu},
        {"codec_gray1.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xffffffffu, 0xff000000u},
        {"codec_gray2.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xffaa5555u, 0xffffffffu},
        {"codec_gray4.png", 8, 2, XImageFormat_ARGB32,
         0xff000000u, 0xff777777u, 0xffffffffu},
        {"codec_gray8.png", 4, 3, XImageFormat_ARGB32,
         0xff000000u, 0xffaaaaaau, 0xff404040u},
        {"codec_gray8_trns.png", 4, 3, XImageFormat_ARGB32,
         0xff000000u, 0xffaaaaaau, 0x00808080u},
        {"codec_gray16.png", 4, 3, XImageFormat_Grayscale16,
         0xff000000u, 0xff808080u, 0xff303030u},
        {"codec_rgb8.png", 4, 3, XImageFormat_ARGB32,
         0xffff0000u, 0xff0000ffu, 0xff010203u},
        {"codec_rgb8_trns.png", 4, 3, XImageFormat_ARGB32,
         0xffff0000u, 0x0000ff00u, 0xff010203u},
        {"codec_rgb16.png", 3, 2, XImageFormat_RGBX64,
         0xff000000u, 0xff00ff00u, 0xffffffffu},
        {"codec_ga8.png", 3, 2, XImageFormat_ARGB32,
         0x00000000u, 0xff000000u, 0xff000000u},
        {"codec_ga16.png", 3, 2, XImageFormat_RGBA64,
         0xff000000u, 0x00808080u, 0xff000000u},
        {"codec_rgba8.png", 3, 2, XImageFormat_ARGB32,
         0xffff0000u, 0x000000ffu, 0xff000000u},
        {"codec_rgba16.png", 3, 2, XImageFormat_RGBA64,
         0xff000000u, 0x0000ff00u, 0xff000000u},
        {"codec_adam7_rgba8.png", 8, 8, XImageFormat_ARGB32,
         0x80000000u, 0x00000000u, 0x00000000u},
        {"codec_adam7_gray4.png", 8, 8, XImageFormat_ARGB32,
         0xff000000u, 0xff333333u, 0xff444444u},
        {"codec_adam7_palette.png", 8, 8, XImageFormat_Indexed8,
         0xffff0000u, 0xffffffffu, 0xff00ff00u},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "png") &&
                    XImage_width(&image) == c->w && XImage_height(&image) == c->h,
                    "PNG 扩展资产尺寸正确");
        expect_true(XImage_format(&image) == c->fmt,
                    "PNG 扩展资产解码格式正确");
        if (c->fmt == XImageFormat_Indexed8 && !XImage_isNull(&image)) {
            expect_true(XImage_pixelIndex(&image, 0, 0) == 0 &&
                        XImage_pixel(&image, 0, 0) == c->p0,
                        "PNG 索引像素还原一致");
        } else {
            expect_true(!XImage_isNull(&image) && XImage_pixel(&image, 0, 0) == c->p0,
                        "PNG 扩展资产关键像素一致");
        }
        XImage_deinit_base(&image);
    }

    /* 16 位格式逐通道高字节验证 */
    {
        XImage g16, rgb16, rgba16;
        const CodecAssetFixture* a16;
        const uint8_t* line;
        a16 = codec_asset_fixture_find("codec_gray16.png");
        XImage_init(&g16);
        expect_true(a16 != NULL && XImage_loadFromData_2(&g16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&g16) == XImageFormat_Grayscale16,
                    "16 位灰度 PNG 解码为 Grayscale16");
        line = XImage_scanLine(&g16, 0);
        expect_true(line[0] == 0x00 && line[1] == 0x00 && line[2] == 0xff && line[3] == 0xff,
                    "16 位灰度样本双字节值还原");
        XImage_init(&rgb16);
        a16 = codec_asset_fixture_find("codec_rgb16.png");
        expect_true(a16 != NULL && XImage_loadFromData_2(&rgb16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&rgb16) == XImageFormat_RGBX64,
                    "16 位 RGB PNG 解码为 RGBX64");
        line = XImage_scanLine(&rgb16, 1);
        expect_true(line[0] == 0x00 && line[2] == 0x00 && line[4] == 0xff && line[5] == 0xff,
                    "16 位 RGB 样本双字节值还原");
        XImage_init(&rgba16);
        a16 = codec_asset_fixture_find("codec_rgba16.png");
        expect_true(a16 != NULL && XImage_loadFromData_2(&rgba16, a16->data, (int)a16->size, "png") &&
                    XImage_format(&rgba16) == XImageFormat_RGBA64,
                    "16 位 RGBA PNG 解码为 RGBA64");
        line = XImage_scanLine(&rgba16, 0);
        expect_true(line[6] == 0xff && line[7] == 0xff && line[15] == 0x80,
                    "16 位 RGBA Alpha 双字节值还原");
        XImage_deinit_base(&g16);
        XImage_deinit_base(&rgb16);
        XImage_deinit_base(&rgba16);
    }
}


static void test_codec_bmp_extended_assets(void)
{
    /* 索引色/RLE4/RLE8/16 位/BITFIELDS/V4/V5/Core/顶行序 BMP 资产解码验证
       （资产字节内嵌于 codec_assets_fixture.h，由 Python struct 生成） */
    struct Pt { int x; int y; uint32_t c; };
    struct Case {
        const char* file; int w; int h;
        XImageFormat fmt;
        struct Pt p0, p1, p2, p3;
    };
    static const struct Case cases[] = {
        /* 资产规范：逻辑行自上而下；bottom-up 文件按规范倒序存储，
           解码器只翻转一次行序。期望值以解码器实测语义为准。 */
        {"codec_bmp_palette1.bmp", 8, 2, XImageFormat_Indexed8,
         {0,0,0xff000000u}, {2,0,0xffffffffu}, {0,1,0xff000000u}, {7,1,0xffffffffu}},
        {"codec_bmp_palette2.bmp", 8, 2, XImageFormat_Indexed8,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xff0000ffu}, {3,1,0xff000000u}},
        {"codec_bmp_palette4.bmp", 8, 2, XImageFormat_Indexed8,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xff111111u}, {7,1,0xff222222u}},
        {"codec_bmp_palette8.bmp", 6, 4, XImageFormat_Indexed8,
         {0,0,0xffff0000u}, {2,0,0xff0000ffu}, {0,2,0xff00ff00u}, {5,3,0xff060606u}},
        {"codec_bmp_rle8.bmp", 6, 4, XImageFormat_Indexed8,
         {0,0,0xff0000ffu}, {3,0,0xffffff00u}, {1,3,0xff00ff00u}, {5,0,0xffff0000u}},
        {"codec_bmp_rle4.bmp", 8, 2, XImageFormat_Indexed8,
         {7,0,0xff000000u}, {6,0,0xffff0000u}, {0,1,0xff000000u}, {7,1,0xff888888u}},
        {"codec_bmp_rgb555.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {2,0,0xff00ff00u}, {0,1,0xffffffffu}},
        {"codec_bmp_rgb565.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {3,0,0xff0000ffu}, {3,1,0xffffff00u}},
        {"codec_bmp_bitfields16.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff000000u}, {1,0,0xffff0000u}, {0,1,0xffffffffu}, {2,1,0xff00ffffu}},
        {"codec_bmp_bitfields32.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0xffff0000u}, {0,1,0xffffffffu}, {2,1,0xff8055aau}, {3,1,0xffff0000u}},
        {"codec_bmp_v4alpha.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0x88ff0000u}, {3,1,0x80102030u}, {2,0,0x880000ffu}, {1,1,0xff000000u}},
        {"codec_bmp_v5rgb.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0x88ff0000u}, {3,1,0x80102030u}, {0,1,0xffffffffu}, {3,0,0x00ffffffu}},
        {"codec_bmp_v5indexed.bmp", 4, 2, XImageFormat_ARGB32,
         {0,0,0xffff0000u}, {1,0,0x000000ffu}, {0,1,0xffffffffu}, {1,1,0x8800ff00u}},
        {"codec_bmp_core12.bmp", 4, 2, XImageFormat_RGB888,
         {0,0,0xff00ff00u}, {0,1,0xff800000u}, {3,0,0xff00ff00u}, {3,1,0xff800000u}},
        {"codec_bmp_topdown.bmp", 3, 3, XImageFormat_RGB888,
         {0,0,0xffff0000u}, {0,1,0xff00ff00u}, {2,2,0xff0000ffu}, {1,0,0xffff0000u}},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        const struct Pt* pts[4] = {&c->p0, &c->p1, &c->p2, &c->p3};
        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "bmp") &&
                    XImage_width(&image) == c->w && XImage_height(&image) == c->h,
                    "BMP 扩展资产尺寸正确");
        expect_true(!XImage_isNull(&image) && XImage_format(&image) == c->fmt,
                    "BMP 扩展资产解码格式正确");
        for (int k = 0; k < 4; ++k) {
            const struct Pt* pt = pts[k];
            uint32_t got = XImage_pixel(&image, pt->x, pt->y);
            expect_true(got == pt->c, "BMP 扩展资产关键像素一致");
        }
        XImage_deinit_base(&image);
    }
}

#if XIMAGECODEC_JPEG_ON
static void test_codec_jpeg_extended_assets(void)
{
    /* JPEG 扩展资产解码验证：基线(SOF0)/渐进(SOF2)/算术(SOF9)/灰度(单分量)/
       4:2:2 / 4:1:1 抽样/重启间隔(DRI)/CMYK(Adobe APP14)/12 位精度。
       资产字节内嵌于 codec_assets_fixture.h，由 IJG 参考 cjpeg/djpeg 生成，
       期望像素取自参考解码器输出（本实现与 djpeg -nosmooth 同用最近邻上采样），
       8 位逐通道容差 3、12 位容差 4，Alpha 恒为 0xff。 */
    struct Pt { int x; int y; int r; int g; int b; };
    struct Case { const char* file; const struct Pt* pts; int tol; };
    /* YCbCr 3 分量 4:2:0 渐变（基线/渐进/算术/重启间隔共用参考输出） */
    static const struct Pt kYccPts[6] = {
        {0, 0, 135, 247, 0}, {47, 0, 248, 250, 245}, {0, 31, 136, 244, 0},
        {47, 31, 251, 247, 248}, {24, 16, 31, 11, 132}, {16, 10, 180, 77, 96}
    };
    /* 灰度单分量（RGB 三个通道取同一灰度值） */
    static const struct Pt kGrayPts[6] = {
        {0, 0, 185, 185, 185}, {47, 0, 249, 249, 249}, {0, 31, 184, 184, 184},
        {47, 31, 248, 248, 248}, {24, 16, 31, 31, 31}, {16, 10, 110, 110, 110}
    };
    /* 4:2:2（Y 2x1 抽样） */
    static const struct Pt k422Pts[6] = {
        {0, 0, 135, 248, 0}, {47, 0, 250, 249, 244}, {0, 31, 134, 246, 0},
        {47, 31, 249, 248, 244}, {24, 16, 32, 10, 136}, {16, 10, 173, 81, 92}
    };
    /* 4:1:1（Y 4x1 抽样） */
    static const struct Pt k411Pts[6] = {
        {0, 0, 149, 239, 1}, {47, 0, 245, 253, 240}, {0, 31, 149, 238, 0},
        {47, 31, 245, 251, 241}, {24, 16, 23, 13, 144}, {16, 10, 167, 82, 103}
    };
    /* CMYK（Adobe APP14 transform=0，直接 C/M/Y/K -> R/G/B 映射） */
    static const struct Pt kCmykPts[6] = {
        {0, 0, 127, 248, 0}, {47, 0, 247, 248, 249}, {0, 31, 127, 248, 0},
        {47, 31, 247, 248, 249}, {24, 16, 36, 8, 127}, {16, 10, 183, 76, 85}
    };
    /* 12 位算术编码 */
    static const struct Pt k12ArithPts[6] = {
        {0, 0, 134, 246, 0}, {47, 0, 248, 249, 249}, {0, 31, 135, 245, 1},
        {47, 31, 248, 248, 249}, {24, 16, 32, 10, 131}, {16, 10, 184, 76, 96}
    };
    /* 12 位 Huffman（仅两点与算术版不同） */
    static const struct Pt k12HuffPts[6] = {
        {0, 0, 134, 246, 0}, {47, 0, 248, 249, 249}, {0, 31, 135, 245, 0},
        {47, 31, 249, 248, 249}, {24, 16, 32, 10, 131}, {16, 10, 184, 76, 96}
    };
    static const struct Case cases[] = {
        {"codec_jpeg_baseline.jpg", kYccPts, 3},
#if XIMAGECODEC_JPEG_PROGRESSIVE_ON
        {"codec_jpeg_progressive.jpg", kYccPts, 3},
#endif /* XIMAGECODEC_JPEG_PROGRESSIVE_ON */
#if XIMAGECODEC_JPEG_ARITHMETIC_ON
        {"codec_jpeg_arithmetic.jpg", kYccPts, 3},
#endif /* XIMAGECODEC_JPEG_ARITHMETIC_ON */
        {"codec_jpeg_gray.jpg", kGrayPts, 3},
        {"codec_jpeg_422.jpg", k422Pts, 3},
        {"codec_jpeg_411.jpg", k411Pts, 3},
        {"codec_jpeg_restart.jpg", kYccPts, 3},
#if XIMAGECODEC_JPEG_CMYK_ON
        {"codec_jpeg_cmyk.jpg", kCmykPts, 3},
#endif /* XIMAGECODEC_JPEG_CMYK_ON */
#if XIMAGECODEC_JPEG_12BIT_ON && XIMAGECODEC_JPEG_ARITHMETIC_ON
        {"codec_jpeg_12bit_arith.jpg", k12ArithPts, 4},
#endif /* XIMAGECODEC_JPEG_12BIT_ON && XIMAGECODEC_JPEG_ARITHMETIC_ON */
#if XIMAGECODEC_JPEG_12BIT_ON
        {"codec_jpeg_12bit_huff.jpg", k12HuffPts, 4},
#endif /* XIMAGECODEC_JPEG_12BIT_ON */
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case* c = &cases[i];
        const CodecAssetFixture* asset = codec_asset_fixture_find(c->file);
        XImage image;
        int k;

        XImage_init(&image);
        expect_true(asset != NULL && XImage_loadFromData_2(&image, asset->data, (int)asset->size, "jpeg") &&
                    XImage_width(&image) == 48 && XImage_height(&image) == 32,
                    "JPEG 扩展资产尺寸正确");
        expect_true(!XImage_isNull(&image), "JPEG 扩展资产解码成功");
        for (k = 0; k < 6; ++k) {
            const struct Pt* pt = &c->pts[k];
            uint32_t got = XImage_pixel(&image, pt->x, pt->y);
            int dr = (int)((got >> 16) & 0xffu) - pt->r;
            int dg = (int)((got >> 8) & 0xffu) - pt->g;
            int db = (int)(got & 0xffu) - pt->b;
            if (dr < 0) dr = -dr;
            if (dg < 0) dg = -dg;
            if (db < 0) db = -db;
            expect_true((got >> 24) == 0xffu &&
                        dr <= c->tol && dg <= c->tol && db <= c->tol,
                        "JPEG 扩展资产采样点逐通道容差内");
        }
        XImage_deinit_base(&image);
    }
}
#endif /* XIMAGECODEC_JPEG_ON */

#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
static bool codec_svg_vector_decode(const char* svg, XImage* image)
{
    return XImageCodec_decode((const uint8_t*)svg, strlen(svg),
                              XImageCodecFormat_Svg, image);
}

static void test_codec_svg_vector_render(void)
{
    XImage image;
    int filled;
    uint32_t v;

    /* 线性渐变：左红右蓝 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect width=\"4\" height=\"4\" fill=\"url(#g)\"/></svg>", &image),
        "SVG 线性渐变可解码");
    expect_true((XImage_pixel(&image, 0, 2) & 0xff0000u) != 0 &&
                (XImage_pixel(&image, 3, 2) & 0xffu) != 0,
                "SVG 线性渐变左红右蓝");
    XImage_deinit_base(&image);

    /* 径向渐变 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"6\" height=\"6\">"
        "<defs><radialGradient id=\"re\" cx=\"0.5\" cy=\"0.5\" r=\"0.5\">"
        "<stop offset=\"0\" stop-color=\"#ffffff\"/>"
        "<stop offset=\"1\" stop-color=\"#000000\"/>"
        "</radialGradient></defs>"
        "<rect width=\"6\" height=\"6\" fill=\"url(#re)\"/></svg>", &image),
        "SVG 径向渐变可解码");
    expect_true((XImage_pixel(&image, 3, 3) & 0x00ffffffu) > 0x00f0f0f0u &&
                (XImage_pixel(&image, 0, 0) & 0xffu) < 0x20u,
                "SVG 径向渐变中心亮边缘暗");
    XImage_deinit_base(&image);

    /* 圆形 / 椭圆 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<circle cx=\"4\" cy=\"4\" r=\"3.5\" fill=\"#00ff00\"/></svg>", &image),
        "SVG circle 可解码");
    expect_true(XImage_pixel(&image, 4, 4) == 0xff00ff00u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG circle 中心填充外部透明");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<ellipse cx=\"4\" cy=\"2\" rx=\"3\" ry=\"1.5\" fill=\"#00ff00\"/></svg>",
        &image), "SVG ellipse 可解码");
    expect_true(XImage_pixel(&image, 4, 2) == 0xff00ff00u &&
                XImage_pixel(&image, 0, 2) == 0x00000000u,
                "SVG ellipse 填充与外空白");
    XImage_deinit_base(&image);

    /* path：三角形 + 圆弧 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<path d=\"M1 5 L1 1 L7 3 Z\" fill=\"#ff8800\"/></svg>", &image),
        "SVG path 三角形可解码");
    filled = 0;
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x)
            if (XImage_pixel(&image, x, y) == 0xffff8800u) ++filled;
    expect_true(filled > 6, "SVG path 三角形面积填充");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<path d=\"M1 5 A3 3 0 0 1 7 5 L7 7 L1 7 Z\" fill=\"#00aaff\"/></svg>",
        &image), "SVG path 圆弧可解码");
    expect_true(XImage_pixel(&image, 4, 5) == 0xff00aaffu &&
                XImage_pixel(&image, 1, 6) == 0xff00aaffu,
                "SVG 圆弧主体覆盖");
    expect_true(XImage_pixel(&image, 4, 0) == 0x00000000u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG 圆弧外部空白");
    XImage_deinit_base(&image);

    /* 多边形 / 折线 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<polygon points=\"1,5 1,1 7,3\" fill=\"#ff8800\"/></svg>", &image),
        "SVG polygon 可解码");
    expect_true(XImage_pixel(&image, 4, 3) == 0xffff8800u,
                "SVG polygon 宽行覆盖");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<polyline points=\"1,1 5,1 9,9\" fill=\"none\" stroke=\"#ff0000\" "
        "stroke-width=\"2\"/></svg>", &image), "SVG polyline 可解码");
    filled = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            if (XImage_pixel(&image, x, y) == 0xffff0000u) ++filled;
    expect_true(filled > 4, "SVG polyline 描边像素存在");
    XImage_deinit_base(&image);

    /* 文字 / viewBox / group+transform / 描边 / 透明度 */
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<text x=\"2\" y=\"8\" font-size=\"10\" fill=\"#000000\">A</text></svg>",
        &image), "SVG text 可解码");
    filled = 0;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 20; ++x)
            if (XImage_pixel(&image, x, y) == 0xff000000u) ++filled;
    expect_true(filled > 4, "SVG text 点阵像素存在");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<rect width=\"5\" height=\"10\" fill=\"#123456\"/></svg>", &image),
        "SVG viewBox 可解码");
    expect_true(XImage_width(&image) == 10 && XImage_height(&image) == 10 &&
                XImage_pixel(&image, 0, 9) == 0xff123456u &&
                XImage_pixel(&image, 4, 0) == 0xff123456u,
                "SVG viewBox 尺寸与内容一致");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<g transform=\"translate(2,2)\">"
        "<rect width=\"4\" height=\"4\" fill=\"#123456\"/></g></svg>", &image),
        "SVG group+transform 可解码");
    expect_true(XImage_pixel(&image, 2, 2) == 0xff123456u &&
                XImage_pixel(&image, 5, 5) == 0xff123456u &&
                XImage_pixel(&image, 0, 0) == 0x00000000u,
                "SVG group transform 平移生效");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"none\" "
        "stroke=\"#ff0000\" stroke-width=\"3\"/></svg>", &image),
        "SVG 描边矩形可解码");
    expect_true(XImage_pixel(&image, 2, 2) == 0xffff0000u &&
                XImage_pixel(&image, 7, 7) == 0xffff0000u &&
                XImage_pixel(&image, 5, 5) == 0x00000000u,
                "SVG 描边边框与内部空白");
    XImage_deinit_base(&image);
    XImage_init(&image);
    expect_true(codec_svg_vector_decode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#ff0000\" opacity=\"0.5\"/></svg>",
        &image), "SVG 透明度可解码");
    v = XImage_pixel(&image, 2, 2);
    expect_true((v >> 24) == 0x80u && (v & 0x00ff0000u) != 0,
                "SVG opacity=0.5 得到 0x80 Alpha");
    XImage_deinit_base(&image);
}
#endif /* XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON */

#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
static void test_codec_gif_animation(void)
{
    XImageCodecAnimation* anim;
    XImage image;
    XByteArray* bytes;

    anim = XImageCodec_decodeAnimation(kCodecGifAnimFixture,
                                       kCodecGifAnimFixtureSize);
    expect_true(anim != NULL, "GIF 动画解码返回对象");
    if (anim) {
        expect_true(anim->frameCount == 4, "GIF 动画帧数为 4");
        expect_true(anim->loopCount == -1, "GIF Netscape 无限循环");
        expect_true(anim->frames[1].delayMs == 200, "GIF 第 2 帧延迟 200ms");
        expect_true(anim->frames[0].disposal == XImageCodecFrameDisposal_Keep &&
                    anim->frames[2].disposal ==
                        XImageCodecFrameDisposal_RestoreBackground,
                    "GIF 帧处置方式解析正确");
        expect_true(XImage_width(&anim->frames[0].image) == 4 &&
                    XImage_height(&anim->frames[0].image) == 2,
                    "GIF 帧尺寸为逻辑屏幕 4x2");
        expect_true(XImage_pixel(&anim->frames[0].image, 0, 0) == 0xffffffffu &&
                    XImage_pixel(&anim->frames[0].image, 2, 0) == 0xff000000u,
                    "GIF 首帧像素正确");
        expect_true(XImage_pixel(&anim->frames[1].image, 2, 0) == 0xffffffffu &&
                    XImage_pixel(&anim->frames[1].image, 3, 0) == 0xff000000u,
                    "GIF 第 2 帧透明色保留底图");
        expect_true(XImage_pixel(&anim->frames[2].image, 3, 0) == 0xffffffffu,
                    "GIF 第 3 帧绘制白色");
        expect_true(XImage_pixel(&anim->frames[3].image, 3, 0) == 0x00000000u &&
                    XImage_pixel(&anim->frames[3].image, 0, 0) == 0xffffffffu,
                    "GIF 第 4 帧恢复背景且旧帧保留");
        XImageCodecAnimation_delete(anim);
    }

    /* 多帧 GIF 的单帧解码仍可用 */
    XImage_init(&image);
    expect_true(XImageCodec_decode(kCodecGifAnimFixture,
                                   kCodecGifAnimFixtureSize,
                                   XImageCodecFormat_Gif, &image),
                "多帧 GIF 单帧解码");
    expect_true(XImage_pixel(&image, 0, 0) == 0xffffffffu,
                "单帧解码取首帧内容");
    XImage_deinit_base(&image);

    /* 动画解码后 GIF 编码往返不受影响 */
    bytes = XByteArray_create();
    XImage_init_ex(&image, 2, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(bytes && XImageCodec_encode(&image, XImageCodecFormat_Gif,
                                            -1, bytes) &&
                XImageCodec_decode(XByteArray_data(bytes),
                                   XByteArray_size_base(
                                       (const XContainer*)bytes),
                                   XImageCodecFormat_Gif, &image),
                "GIF 编码往返在动画 API 之后正常");
    XImage_deinit_base(&image);
    if (bytes) XByteArray_delete_base((XClass*)bytes);
}
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */

static void test_codec_upper_layer_files(void)
{
    static const char* files[] = {"xgui_rt.bmp", "xgui_rt.png", "xgui_rt.jpg",
                                  "xgui_rt.gif", "xgui_rt.svg"};
    static const char* fmts[] = {"bmp", "png", "jpeg", "gif", "svg"};
    XImage source, loaded;
    size_t i;

    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&source, 0xff336699u);
    XImage_setPixel(&source, 0, 1, 0x80123456u);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        XImage_init(&loaded);
        expect_true(XImage_save_2(&source, files[i], fmts[i], -1) &&
                    XImage_load_2(&loaded, files[i], fmts[i]),
                    "XImage 文件保存/加载走 codec（各格式）");
        expect_true(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2,
                    "XImage 文件往返保持尺寸（各格式）");
        if (fmts[i][0] != 'g' && fmts[i][0] != 'j') {  /* JPEG 有损，跳过逐像素精确比较 */
            expect_true(XImage_pixel(&loaded, 0, 0) == 0xff336699u &&
                        XImage_pixel(&loaded, 2, 0) == 0xff336699u,
                        "XImage 文件往返像素一致（各格式）");
        }
        XImage_deinit_base(&loaded);
        { XString* f = XString_create_utf8(files[i]);
          XFile_remove_static(f); if (f) XString_delete_base((XClass*)f); }
    }
    XImage_deinit_base(&source);
}

static void test_codec_upper_layer_devices(void)
{
    static const char* files[] = {"xgui_dev.bmp", "xgui_dev.png", "xgui_dev.jpg",
                                  "xgui_dev.gif", "xgui_dev.svg"};
    static const char* fmts[] = {"bmp", "png", "jpeg", "gif", "svg"};
    XImage source, loaded;
    XFile file;
    XImageWriter writer;
    XImageReader reader;
    XString* fileName = NULL;
    size_t i;

    XImage_init_ex(&source, 2, 2, XImageFormat_ARGB32);
    XImage_fill(&source, 0xffabcdefu);
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        fileName = XString_create_utf8(files[i]);
        XFile_init_2(&file, fileName);
        if (XFile_open_2(&file, XIODevice_WriteOnly, 0)) {
            XImageWriter_init_device_2(&writer, (XIODevice*)&file, fmts[i]);
            expect_true(XImageWriter_canWrite(&writer) &&
                        XImageWriter_write(&writer, &source),
                        "XImageWriter 设备写（各格式）");
            XImageWriter_deinit_base(&writer);
            XIODevice_close_base((XIODevice*)&file);
        }
        XClass_deinit_base((XClass*)&file);

        XFile_init_2(&file, fileName);
        if (XFile_open_2(&file, XIODevice_ReadOnly, 0)) {
            XImageReader_init_device_2(&reader, (XIODevice*)&file, fmts[i]);
            XImage_init(&loaded);
            expect_true(XImageReader_canRead(&reader) &&
                        XImageReader_read(&reader, &loaded),
                        "XImageReader 设备读（各格式）");
            expect_true(XImage_width(&loaded) == 2 && XImage_height(&loaded) == 2,
                        "XImageReader 设备往返保持尺寸（各格式）");
            XImage_deinit_base(&loaded);
            XImageReader_deinit_base(&reader);
            XIODevice_close_base((XIODevice*)&file);
        }
        XClass_deinit_base((XClass*)&file);
        XFile_remove_static(fileName);
        XString_delete_base((XClass*)fileName);
    }
    XImage_deinit_base(&source);
}

#endif /* XIMAGECODEC_ON */
static void test_image_pixel_contract(void)
{
    XImage rgba;
    XImage premultiplied;
    XImage converted;
    XImage indexed;
    XImage copy;
    const uint32_t customPalette[2] = { 0xff000000u, 0xffffffffu };

    XImage_init(&copy);
    XImage_init(&converted);
    XImage_init_ex(&premultiplied, 1, 1, XImageFormat_ARGB32_Premultiplied);
    XImage_setPixel(&premultiplied, 0, 0, 0x80800000u);
    expect_true((XImage_pixel(&premultiplied, 0, 0) & 0x00ff0000u) >= 0x007f0000u &&
                (XImage_pixel(&premultiplied, 0, 0) & 0x00ff0000u) <= 0x00810000u,
                "premultiplied pixel reads back an unpremultiplied channel");
    XImage_convertToFormat(&premultiplied, XImageFormat_ARGB32, 0, &converted);
    expect_true(XImage_pixel(&converted, 0, 0) == 0x80800000u,
                "premultiplied conversion does not unpremultiply twice");

    XImage_init_ex(&rgba, 2, 1, XImageFormat_RGBA8888);
    XImage_setPixel(&rgba, 0, 0, 0x80402010u);
    expect_true(XImage_pixel(&rgba, 0, 0) == 0x80402010u,
                "RGBA8888 pixel access uses RGBA byte order");
    XImage_fill(&rgba, 0xff112233u);
    expect_true(XImage_pixel(&rgba, 1, 0) == 0xff112233u &&
                !XImage_hasAlpha(&rgba), "fill resets RGBA alpha to opaque");
    XImage_setPixel(&rgba, 1, 0, 0x80112233u);
    expect_true(XImage_hasAlpha(&rgba), "alpha detection covers RGBA8888");
    XImage_setDevicePixelRatio(&rgba, 2.0f);
    XImage_copy_base(&copy, &rgba);
    expect_true(XImage_devicePixelRatio(&copy) == 2.0f,
                "device pixel ratio survives image copy");
    XImage_invertPixels(&rgba, XImageInvertMode_InvertRgb);
    expect_true(XImage_pixel(&rgba, 0, 0) == 0xffeeddccu,
                "invertPixels preserves alpha and inverts RGB");
    XImage_rgbSwapped(&rgba, &copy);
    expect_true(XImage_pixel(&copy, 0, 0) == 0xffccddeeU,
                "rgbSwapped preserves RGBA alpha and swaps RGB");
    XImage_mirrored(&rgba, true, false, &copy);
    expect_true(XImage_pixel(&copy, 0, 0) == 0x80eeddccu,
                "mirrored uses pixel coordinates for RGBA images");

    XImage_init_ex(&indexed, 1, 1, XImageFormat_Indexed8);
    XImage_setColorCount(&indexed, 2);
    XImage_setColor(&indexed, 0, 0xff102030u);
    XImage_setColor(&indexed, 1, 0xffa0b0c0u);
    XImage_setPixel(&indexed, 0, 0, 1);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 1 &&
                XImage_pixel(&indexed, 0, 0) == 0xffa0b0c0u,
                "indexed pixel access returns index and palette color");
    XImage_invertPixels(&indexed, XImageInvertMode_InvertRgb);
    expect_true(XImage_pixelIndex(&indexed, 0, 0) == 254 &&
                XImage_pixel(&indexed, 0, 0) == 0u,
                "indexed invertPixels complements indices without changing the palette");
    XImage_convertToFormat_ex(&rgba, XImageFormat_Indexed8, customPalette, 2, 0, &indexed);
    expect_true(XImage_colorCount(&indexed) == 2 &&
                XImage_color(&indexed, 1) == customPalette[1],
                "indexed conversion accepts an explicit color table");

    XImage_deinit_base(&copy);
    XImage_deinit_base(&converted);
    XImage_deinit_base(&premultiplied);
    XImage_deinit_base(&indexed);
    XImage_deinit_base(&rgba);
}

int main(void)
{
    test_geometry_contract();
    test_pixmap_scroll_and_bitmap_alias();
    test_pixmap_lifecycle();
    test_icon_sizes();
    test_icon_matching();
    test_icon_device_pixel_ratio();
#if XIMAGECODEC_ON
    test_icon_add_file_size();
#endif /* XIMAGECODEC_ON */
    test_pixmap_cache_contract();
#if defined(__unix__)
    test_pixmap_cache_concurrency();
#endif
    test_picture_play_contract();
    test_painter_raster_contract();
    test_painter_record_play_contract();
#if XIMAGECODEC_ON
    test_image_device_io();
    test_image_handler_registry();
    test_image_codec_round_trip();
    test_codec_pixel_round_trip();
    test_codec_reject_malformed();
    test_codec_detect_only();
    test_codec_decode_real_assets();
    test_codec_png_palette_round_trip();
    test_codec_png_extended_assets();
    test_codec_bmp_extended_assets();
#if XIMAGECODEC_JPEG_ON
    test_codec_jpeg_extended_assets();
#endif /* XIMAGECODEC_JPEG_ON */
#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
    test_codec_svg_vector_render();
#endif
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    test_codec_gif_animation();
#endif
    test_codec_upper_layer_files();
    test_codec_upper_layer_devices();
#endif /* XIMAGECODEC_ON */
    test_image_pixel_contract();
    if (s_failures != 0) {
        XERROR_PRINTF("%d XGui regression test(s) failed\n", s_failures);
        return 1;
    }
    puts("XGui regression tests passed");
    return 0;
}
