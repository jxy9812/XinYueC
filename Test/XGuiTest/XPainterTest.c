#include "XGuiTest.h"
#if DEMOTEST
#include "XPainter.h"
#include "XPicture.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ==================== XPainter 测试 ==================== */

/**
 * @brief      软件光栅后端测试：填充/描边/画线/裁剪/变换/合成
 */
static void XPainterRasterTest(void)
{
    XPrintf("===== XPainter 软件光栅后端测试 =====\n");
    {
        XImage image;
        XPainter painter;
        XRect rect = {1, 1, 4, 3};

        XImage_init_ex(&image, 8, 8, XImageFormat_ARGB32);
        XPainter_init(&painter, NULL);
        assert(XPainter_begin_image(&painter, &image));

        assert(XPainter_fillRect(&painter, &rect, 0xff336699u));
        assert(XImage_pixel(&image, 1, 1) == 0xff336699u);
        assert(XImage_pixel(&image, 4, 3) == 0xff336699u);
        assert(XImage_pixel(&image, 0, 0) == 0u);

        XPainter_setPen(&painter, 0xffff0000u);
        XRect outline = {1, 1, 5, 4};
        assert(XPainter_drawRect(&painter, &outline));
        assert(XImage_pixel(&image, 1, 1) == 0xffff0000u);
        assert(XImage_pixel(&image, 3, 2) == 0xff336699u);

        XPainter_setPen(&painter, 0xff00ff00u);
        assert(XPainter_drawLine(&painter, 2, 6, 6, 6));
        assert(XImage_pixel(&image, 2, 6) == 0xff00ff00u);
        assert(XImage_pixel(&image, 6, 6) == 0xff00ff00u);

        /* 透明度与 Source 合成 */
        XImage_setPixel(&image, 0, 0, 0xff204060u);
        XPainter_setOpacity(&painter, 0.5f);
        XRect one = {0, 0, 1, 1};
        assert(XPainter_fillRect(&painter, &one, 0xff4080ffu));
        assert(XImage_pixel(&image, 0, 0) == 0xff3060b0u);
        XPainter_setOpacity(&painter, 1.0f);
        XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
        assert(XPainter_fillRect(&painter, &one, 0x80406080u));
        assert(XImage_pixel(&image, 0, 0) == 0x80406080u);

        /* 变换 */
        XImage_fillRect(&image, NULL, 0u);
        XPainter_resetTransform(&painter);
        XPainter_translate(&painter, 2.0f, 3.0f);
        assert(XPainter_drawPoint(&painter, 4, 4));
        assert(XImage_pixel(&image, 6, 7) == 0xff00ff00u);
        assert(XImage_pixel(&image, 0, 0) == 0u);
        XPainter_resetTransform(&painter);

        XPainter_deinit(&painter);
        XImage_deinit_base(&image);
        XPrintf("PASS\n");
    }
    XPrintf("\n");
}

/**
 * @brief      录制/回放测试：先录制到 XPicture，再在图像上回放
 */
static void XPainterRecordPlayTest(void)
{
    XPrintf("===== XPainter 录制/回放测试 =====\n");
    {
        XPicture picture;
        XPicture loaded;
        XPainter painter;
        XImage target;
        XRect rect = {1, 1, 4, 4};

        XPicture_init(&picture, -1);
        XPicture_init(&loaded, -1);
        XPainter_init(&painter, NULL);

        assert(XPainter_begin_picture(&painter, &picture));
        assert(XPainter_fillRect(&painter, &rect, 0xff00ff00u));
        XPainter_setPen(&painter, 0xffff0000u);
        assert(XPainter_drawRect(&painter, &rect));
        assert(XPainter_save(&painter) && XPainter_restore(&painter));
        assert(XPainter_end(&painter));

        assert(XPicture_isValidStream(&picture));
        assert(XPicture_save_2(&picture, "xpainter_demo.bin"));
        assert(XPicture_load_2(&loaded, "xpainter_demo.bin"));
        remove("xpainter_demo.bin");

        XImage_init_ex(&target, 10, 10, XImageFormat_ARGB32);
        XPainter_init(&painter, NULL);
        assert(XPainter_begin_image(&painter, &target));
        XPainter_setPen(&painter, 0xffff0000u);
        assert(XPicture_play(&loaded, &painter));
        assert(XImage_pixel(&target, 1, 1) == 0xffff0000u);
        assert(XImage_pixel(&target, 2, 2) == 0xff00ff00u);
        assert(XImage_pixel(&target, 5, 1) == 0u);

        XPainter_end(&painter);
        XPainter_deinit(&painter);
        XImage_deinit_base(&target);
        XPicture_deinit_base(&loaded);
        XPicture_deinit_base(&picture);
        XPrintf("PASS\n");
    }
    XPrintf("\n");
}

/**
 * @brief      XPainter 综合测试入口
 */
void XMenu_XPainterTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XPainter 绘图器类");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "软件光栅后端测试");
    XAction_setAction(action, XPainterRasterTest);

    action = XMenu_addAction(menu, "录制/回放测试");
    XAction_setAction(action, XPainterRecordPlayTest);
}
#endif // DEMOTEST
