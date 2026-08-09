/**
 * @file        xgui_regression_test.c
 * @brief       XGui Qt 对齐回归测试（无平台 API、无菜单依赖）
 */

#include "XIcon.h"
#include "XBitmap.h"
#include "XGuiTypes.h"
#include "XImage.h"
#include "XImageReader.h"
#include "XImageWriter.h"
#include "XPicture.h"
#include "XPixmap.h"
#include "XFile.h"
#include "XString.h"
#include "XVector.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;

static void expect_true(bool condition, const char* name)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        s_failures++;
    }
}

static void make_file_name(XString** out)
{
    *out = XString_create_utf8("xgui_regression.bmp");
}

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
    PictureProbe* probe = (PictureProbe*)p->userData;
    (void)x1; (void)y1; (void)x2; (void)y2;
    ++probe->lineCalls;
    return true;
}

static bool picture_probe_fill(XPainter* p, const XRect* r, uint32_t color)
{
    PictureProbe* probe = (PictureProbe*)p->userData;
    (void)r; (void)color;
    ++probe->fillCalls;
    return true;
}

static bool picture_probe_state(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->userData;
    ++probe->saveCalls;
    return true;
}

static bool picture_probe_restore(XPainter* p)
{
    PictureProbe* probe = (PictureProbe*)p->userData;
    ++probe->restoreCalls;
    return true;
}

static bool picture_probe_image(XPainter* p, const XImage* image, int x, int y)
{
    PictureProbe* probe = (PictureProbe*)p->userData;
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

    expect_true(XVector_size_base(&sizes) == 2, "icon availableSizes returns unique entries");
    size0 = (XSize*)XVector_at_base(&sizes, 0);
    size1 = (XSize*)XVector_at_base(&sizes, 1);
    expect_true(size0 && size1 &&
                ((size0->width == 16 && size1->width == 32) ||
                 (size0->width == 32 && size1->width == 16)),
                "icon availableSizes preserves dimensions");

    XVector_deinit_base(&sizes);
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
    XBitmap_transformed(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &transformed);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&transformed) &&
                XClassGetVtable((const XClass*)&transformed) ==
                    XClassGetVtable((const XClass*)&bitmap),
                "bitmap transformed output keeps bitmap identity");
    XBitmap_transformed(&bitmap, 1.0f, 0.0f, 0.0f,
                        0.0f, 1.0f, 0.0f, &bitmap);
    expect_true(XPixmap_isQBitmap((const XPixmap*)&bitmap) &&
                XPixmap_width((const XPixmap*)&bitmap) == 2,
                "bitmap transformed supports aliased output");

    XBitmap_deinit_base(&transformed);
    XBitmap_deinit_base(&bitmap);
    XRegion_deinit(&exposed);
    XPixmap_deinit_base(&pixmap);
}

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
    painter.drawLine = picture_probe_line;
    XPicture_recordDrawLine(&picture, 1, 2, 3, 4);
    painter.fillRect = picture_probe_fill;
    XPicture_recordFillRect(&picture, &rect, 0xff112233u);
    painter.save = picture_probe_state;
    XPicture_recordSave(&picture);
    painter.restore = picture_probe_restore;
    XPicture_recordRestore(&picture);
    painter.drawImage = picture_probe_image;
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
        expect_true(XPicture_save(&picture, "xgui_regression.xpic"),
                    "picture save writes the portable stream");
        expect_true(XPicture_load(&loaded, "xgui_regression.xpic") &&
                    XPicture_isValidStream(&loaded) &&
                    XPicture_play(&loaded, &painter),
                    "picture load validates and replays the portable stream");
        remove("xgui_regression.xpic");
        XPicture_deinit_base(&loaded);
    }
    XImage_deinit_base(&image);
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

static void test_icon_add_file_size(void)
{
    XImage image;
    XIcon icon;
    XVector sizes;
    XSize* size;

    XImage_init_ex(&image, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    expect_true(XImage_save(&image, "xgui_icon_add_file.bmp", "BMP", -1),
                "writes icon addFile fixture");

    XIcon_init(&icon);
    XIcon_addFile(&icon, "xgui_icon_add_file.bmp", 8, 6,
                  XIconMode_Normal, XIconState_Off);
    XVector_init(&sizes, sizeof(XSize), true);
    XIcon_availableSizes(&icon, XIconMode_Normal, XIconState_Off, &sizes);
    size = XVector_size_base(&sizes) == 1
        ? (XSize*)XVector_at_base(&sizes, 0) : NULL;
    expect_true(size && size->width == 8 && size->height == 6,
                "icon addFile stores the requested raster size");

    XVector_deinit_base(&sizes);
    XIcon_deinit_base(&icon);
    XImage_deinit_base(&image);
    remove("xgui_icon_add_file.bmp");
}

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
    XImageWriter_init_device(&writer, (XIODevice*)&file, "BMP");
    expect_true(XImageWriter_canWrite(&writer), "device writer reports BMP support");
    expect_true(XImageWriter_write(&writer, &source), "device writer writes BMP");
    XImageWriter_deinit_base(&writer);
    XIODevice_close_base((XIODevice*)&file);
    XClass_deinit_base((XClass*)&file);

    XFile_init_2(&file, file_name);
    ok = XFile_open_2(&file, XIODevice_ReadOnly, 0);
    expect_true(ok, "opens BMP input device");
    XImageReader_init_device(&reader, (XIODevice*)&file, "BMP");
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
    XString_delete_base(file_name);
    XImage_deinit_base(&source);
}

static void test_image_handler_registry(void)
{
    XVector* readerFormats = (XVector*)XImageReader_supportedImageFormats();
    XVector* readerMimes = (XVector*)XImageReader_supportedMimeTypes();
    XVector* readerBmp = (XVector*)XImageReader_imageFormatsForMimeType("IMAGE/BMP");
    XVector* writerFormats = (XVector*)XImageWriter_supportedImageFormats();
    XVector* writerUnknown = (XVector*)XImageWriter_imageFormatsForMimeType("image/png");
    const char** format;

    format = readerFormats ? (const char**)XVector_at_base(readerFormats, 0) : NULL;
    expect_true(readerFormats && XVector_size_base(readerFormats) == 1 && format &&
                strcmp(*format, "bmp") == 0, "reader registry exposes BMP codec");
    expect_true(readerMimes && XVector_size_base(readerMimes) == 1,
                "reader registry exposes BMP MIME type");
    expect_true(readerBmp && XVector_size_base(readerBmp) == 1,
                "reader MIME lookup is case insensitive");
    format = writerFormats ? (const char**)XVector_at_base(writerFormats, 0) : NULL;
    expect_true(writerFormats && XVector_size_base(writerFormats) == 1 && format &&
                strcmp(*format, "bmp") == 0, "writer registry exposes BMP codec");
    expect_true(writerUnknown && XVector_size_base(writerUnknown) == 0,
                "writer MIME lookup returns empty list for unsupported format");

    if (readerFormats) XVector_delete_base(readerFormats);
    if (readerMimes) XVector_delete_base(readerMimes);
    if (readerBmp) XVector_delete_base(readerBmp);
    if (writerFormats) XVector_delete_base(writerFormats);
    if (writerUnknown) XVector_delete_base(writerUnknown);
}

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
    XImage_copy(&copy, &rgba);
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
    test_icon_add_file_size();
    test_picture_play_contract();
    test_image_device_io();
    test_image_handler_registry();
    test_image_pixel_contract();
    if (s_failures != 0) {
        fprintf(stderr, "%d XGui regression test(s) failed\n", s_failures);
        return 1;
    }
    puts("XGui regression tests passed");
    return 0;
}
