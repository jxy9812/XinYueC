/**
 * @file        xgui_regression_test.c
 * @brief       XGui Qt 对齐回归测试（无平台 API、无菜单依赖）
 */

#include "XIcon.h"
#include "XImage.h"
#include "XImageReader.h"
#include "XImageWriter.h"
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

static void test_pixmap_lifecycle(void)
{
    XPixmap source;
    XPixmap copied;
    XPixmap transformed;
    XImage image;

    XPixmap_init_ex(&source, 2, 3);
    XPixmap_fill(&source, 0xff336699u);

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

    XImage_deinit_base(&image);
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

int main(void)
{
    test_pixmap_lifecycle();
    test_icon_sizes();
    test_image_device_io();
    if (s_failures != 0) {
        fprintf(stderr, "%d XGui regression test(s) failed\n", s_failures);
        return 1;
    }
    puts("XGui regression tests passed");
    return 0;
}
