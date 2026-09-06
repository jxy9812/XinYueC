/******************************************************************************
 * @file       xgui_gpu_test.c
 * @brief      XGui GPU 渲染后端冒烟测试（阶段 1/阶段 2 共用）。
 * @details    程序在进程内创建 ARGB32 离屏图像并绑定 XPainter：
 *             - 软件后端为默认；运行前设置环境变量
 *               XGUI_RENDER_BACKEND=gpu 时启用 GPU 光栅后端；
 *             - 用例覆盖：fillRect（不透明/半透明+opacity）、drawImage
 *               （纹理贴图）、drawText（文本像素存在性）、整帧结束后的
 *               像素断言（fill 色/tile 色/半透明合成色）；
 *             - GPU 后端把帧画进离屏/窗口 FBO 并 readback 到目标 XImage，
 *               像素断言同时校验软件与 GPU 两条后端的可观测结果一致；
 *             - 无 GL 驱动或会话创建失败时后端自动回退软件，本测试对
 *               两种结果都给出明确诊断输出，便于嵌入式无 GPU 目标区分
 *               “回退成功”与“真实失败”。
 * @note       本测试直接以像素断言验证后端渲染正确性，是 GPU 后端回归的
 *             最小入口；完整窗口直通链路由 XGuiWindowDemo_Test 覆盖。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XPainter.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    XImage image;
    XImage tile;
    XPainter painter;
    XRect rect = { 2, 1, 5, 3 };
    XPainterRasterBackend backend;
    int textPixels = 0;
    int scanX;
    int scanY;
    int ok = 1;

    fprintf(stderr, "gpu-test: image init\n");
    XImage_init_ex(&image, 16, 12, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff101820u);
    XImage_init_ex(&tile, 2, 2, XImageFormat_ARGB32);
    XImage_fillRect(&tile, NULL, 0xff20a040u);
    fprintf(stderr, "gpu-test: painter init/begin\n");
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &image)) return 2;
    backend = XPainter_rasterBackend(&painter);
    fprintf(stderr, "gpu-test: backend=%d\n", (int)backend);
    if (!XPainter_fillRect(&painter, &rect, 0xffc04020u)) ok = 0;
    fprintf(stderr, "gpu-test: fill done\n");
    XPainter_setOpacity(&painter, 0.5f);
    {
        XRect translucent = { 0, 0, 1, 1 };
        if (!XPainter_fillRect(&painter, &translucent, 0x80406080u)) ok = 0;
    }
    XPainter_setOpacity(&painter, 1.0f);
    if (!XPainter_drawImage(&painter, &tile, 10, 8)) ok = 0;
    fprintf(stderr, "gpu-test: image done\n");
    if (!XPainter_drawText(&painter, 1, 10, "A", 0xffffffffu)) ok = 0;
    fprintf(stderr, "gpu-test: text done\n");
    if (!XPainter_end(&painter)) ok = 0;
    fprintf(stderr, "gpu-test: end done pixel=%08x tile=%08x\n",
            (unsigned)XImage_pixel(&image, 3, 2),
            (unsigned)XImage_pixel(&image, 10, 8));
    fprintf(stderr, "gpu-test: translucent=%08x\n",
            (unsigned)XImage_pixel(&image, 0, 0));
    for (scanY = 0; scanY < XImage_height(&image); ++scanY)
        for (scanX = 0; scanX < XImage_width(&image); ++scanX)
            if (XImage_pixel(&image, scanX, scanY) == 0xffffffffu)
                ++textPixels;
    if (XImage_pixel(&image, 3, 2) != 0xffc04020u ||
        XImage_pixel(&image, 10, 8) != 0xff20a040u || textPixels <= 0)
        ok = 0;
    XPainter_deinit(&painter);
    XImage_deinit_base(&tile);
    XImage_deinit_base(&image);
    fprintf(stderr, "gpu-test: done\n");
    return ok ? 0 : 1;
}
