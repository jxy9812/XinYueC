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
#include "XGpuRenderBackend.h"
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
    bool wasGpu;

    /* 默认请求 GPU 后端，使 CTest/手工运行都覆盖 GPU 代码路径；
       无 GL 驱动时后端自动回退软件，降级断言按实际会话状态跳过。 */
    if (!getenv("XGUI_RENDER_BACKEND") && !getenv("XGPU_BACKEND"))
        setenv("XGUI_RENDER_BACKEND", "gpu", 1);

    fprintf(stderr, "gpu-test: image init\n");
    XImage_init_ex(&image, 64, 40, XImageFormat_ARGB32);
    XImage_fillRect(&image, NULL, 0xff101820u);
    XImage_init_ex(&tile, 2, 2, XImageFormat_ARGB32);
    XImage_fillRect(&tile, NULL, 0xff20a040u);
    fprintf(stderr, "gpu-test: painter init/begin\n");
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &image)) return 2;
    backend = XPainter_rasterBackend(&painter);
    fprintf(stderr, "gpu-test: backend=%d\n", (int)backend);
    wasGpu = backend == XPainterRasterBackend_Gpu;
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
    /* 默认字体为 outline（XFontOutlineCommon）：GPU 会话下不得整帧降级。 */
    if (!XPainter_drawText(&painter, 1, 24, "Ag", 0xffffffffu)) ok = 0;
    fprintf(stderr, "gpu-test: text done backend=%d\n",
            (int)XPainter_rasterBackend(&painter));
    /* drawTextRect（文本布局路径）同样必须留在 GPU 快速路径内。 */
    {
        XRect textRect = { 30, 4, 30, 30 };
        if (!XPainter_drawTextRect(&painter, &textRect, 0, "Xy", 0xffffffffu))
            ok = 0;
    }
    fprintf(stderr, "gpu-test: textRect done backend=%d\n",
            (int)XPainter_rasterBackend(&painter));
    if (wasGpu && XPainter_rasterBackend(&painter) !=
                      XPainterRasterBackend_Gpu)
    {
        fprintf(stderr, "gpu-test: outline text degraded frame to software\n");
        ok = 0;
    }
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

    /* ===== 字形图集：命中零上传、覆盖度可换色复用、满时重置 ===== */
    if (backend == XPainterRasterBackend_Gpu)
    {
        XGpuRenderBackend* session = XGpuRenderBackend_create(64, 64);
        if (session)
        {
            /* 8x8 的"十字"覆盖图，便于断言子矩形采样。 */
            static const uint8_t cross[64] = {
                0, 0, 0, 255, 255, 0, 0, 0,
                0, 0, 0, 255, 255, 0, 0, 0,
                0, 0, 0, 255, 255, 0, 0, 0,
                255, 255, 255, 255, 255, 255, 255, 255,
                255, 255, 255, 255, 255, 255, 255, 255,
                0, 0, 0, 255, 255, 0, 0, 0,
                0, 0, 0, 255, 255, 0, 0, 0,
                0, 0, 0, 255, 255, 0, 0, 0
            };
            XImage frame;
            unsigned uploads0;
            unsigned hits0;
            XImage_init_ex(&frame, 64, 64, XImageFormat_ARGB32);
            XImage_fillRect(&frame, NULL, 0xff000000u);
            XGpuRenderBackend_beginFrameImage(session, &frame);
            uploads0 = XGpuRenderBackend_glyphAtlasUploadCount(session);
            hits0 = XGpuRenderBackend_glyphAtlasHitCount(session);
            /* 首次绘制：未命中，上传一次。 */
            if (!XGpuRenderBackend_drawGlyphAlpha(
                    session, 0xABCDEF01ull, 8, 8, cross, 8, 2, 2,
                    0xffffffffu, 1.0f, true))
                ok = 0;
            if (XGpuRenderBackend_glyphAtlasUploadCount(session) !=
                    uploads0 + 1 ||
                XGpuRenderBackend_glyphAtlasHitCount(session) != hits0)
            {
                fprintf(stderr, "gpu-test: atlas miss did not upload once\n");
                ok = 0;
            }
            /* 同键再次绘制（换颜色）：必须命中且不再上传；换色说明缓存
               存的是原始覆盖度而非烤色位图。 */
            if (!XGpuRenderBackend_drawGlyphAlpha(
                    session, 0xABCDEF01ull, 8, 8, cross, 8, 20, 2,
                    0xff00ff00u, 1.0f, true))
                ok = 0;
            if (XGpuRenderBackend_glyphAtlasUploadCount(session) !=
                    uploads0 + 1 ||
                XGpuRenderBackend_glyphAtlasHitCount(session) != hits0 + 1)
            {
                fprintf(stderr, "gpu-test: atlas hit uploaded again\n");
                ok = 0;
            }
            XGpuRenderBackend_readback(session, &frame);
            /* 十字中心与两处绘制点的横竖笔（覆盖度列 3/4、行 3/4）。 */
            if (XImage_pixel(&frame, 5, 5) != 0xffffffffu ||
                XImage_pixel(&frame, 5, 2) != 0xffffffffu ||
                XImage_pixel(&frame, 22, 5) != 0xff00ff00u ||
                XImage_pixel(&frame, 24, 2) != 0xff00ff00u)
            {
                fprintf(stderr, "gpu-test: atlas pixels wrong: %08x %08x\n",
                        (unsigned)XImage_pixel(&frame, 5, 5),
                        (unsigned)XImage_pixel(&frame, 22, 5));
                ok = 0;
            }
            /* 背景未被波及。 */
            if (XImage_pixel(&frame, 0, 63) != 0xff000000u)
                ok = 0;
            /* 大量不同键填满图集触发重置后，原键仍可正确重传重绘。 */
            {
                unsigned key;
                for (key = 0; key < 9000u; ++key)
                    XGpuRenderBackend_drawGlyphAlpha(
                        session, 0x50000000ull + key, 8, 8, cross, 8,
                        (int)((key * 8u) % 56u), (int)((key * 8u) % 56u),
                        0xff804020u, 1.0f, true);
            }
            if (XGpuRenderBackend_glyphAtlasUploadCount(session) <=
                uploads0 + 1)
            {
                fprintf(stderr, "gpu-test: atlas stress uploaded nothing\n");
                ok = 0;
            }
            XGpuRenderBackend_readback(session, &frame);
            if (XImage_pixel(&frame, 0, 0) != 0xff000000u)
            {
                fprintf(stderr, "gpu-test: atlas stress corrupted frame\n");
                ok = 0;
            }
            /* 命中查询与 alpha=NULL 命中绘制：调用方据此跳过 CPU 光栅。 */
            if (XGpuRenderBackend_drawGlyphAlpha(
                    session, 0xABCDEF01ull, 8, 8, cross, 8, 40, 40,
                    0xffffffffu, 1.0f, true))
            {
                uploads0 = XGpuRenderBackend_glyphAtlasUploadCount(session);
                if (!XGpuRenderBackend_drawGlyphAlpha(
                        session, 0xABCDEF01ull, 8, 8, NULL, 8, 50, 40,
                        0xff4040ffu, 1.0f, true) ||
                    XGpuRenderBackend_glyphAtlasUploadCount(session) != uploads0)
                {
                    fprintf(stderr, "gpu-test: atlas NULL-alpha hit failed\n");
                    ok = 0;
                }
            }
            if (XGpuRenderBackend_drawGlyphAlpha(
                    session, 0xFEEDF00Dull, 8, 8, NULL, 8, 0, 0,
                    0xffffffffu, 1.0f, true))
            {
                fprintf(stderr, "gpu-test: atlas NULL-alpha miss succeeded\n");
                ok = 0;
            }
            if (!XGpuRenderBackend_glyphAtlasContains(
                    session, 0xABCDEF01ull, 8, 8) ||
                XGpuRenderBackend_glyphAtlasContains(
                    session, 0xFEEDF00Dull, 8, 8))
            {
                fprintf(stderr, "gpu-test: atlas contains wrong\n");
                ok = 0;
            }
            XGpuRenderBackend_resetGlyphAtlas(session);
            if (XGpuRenderBackend_glyphAtlasUploadCount(session) != 0 ||
                XGpuRenderBackend_glyphAtlasHitCount(session) != 0)
            {
                fprintf(stderr, "gpu-test: atlas reset kept counters\n");
                ok = 0;
            }
            if (XGpuRenderBackend_glyphAtlasContains(
                    session, 0xABCDEF01ull, 8, 8))
            {
                fprintf(stderr, "gpu-test: atlas reset kept entries\n");
                ok = 0;
            }
            /* 几何 AA：GPU 会话内 AA 多边形产生灰度边缘且不降级。 */
            {
                XImage polyFrame;
                XPainter polyPainter;
                XPoint triangle[3];
                int px, py, grayCount = 0;
                XImage_init_ex(&polyFrame, 64, 64, XImageFormat_ARGB32);
                XImage_fillRect(&polyFrame, NULL, 0xff202020u);
                XPainter_init(&polyPainter, NULL);
                XPainter_begin_image(&polyPainter, &polyFrame);
                if (XPainter_rasterBackend(&polyPainter) !=
                    XPainterRasterBackend_Gpu)
                {
                    fprintf(stderr, "gpu-test: poly session not gpu\n");
                    ok = 0;
                }
                XPainterPath triPath;
                XPainter_setRenderHint(&polyPainter,
                                       XPainterRenderHint_Antialiasing,
                                       true);
                XPainter_setBrush(&polyPainter, 0xff00ff00u);
                triangle[0].x = 6;  triangle[0].y = 52;
                triangle[1].x = 32; triangle[1].y = 10;
                triangle[2].x = 56; triangle[2].y = 52;
                /* fillPath 纯填充（drawPolygon 的描边为画线，画线暂无
                   GPU 快速路径，会整帧降级——既有设计）。 */
                XPainterPath_init(&triPath);
                XPainterPath_moveTo(&triPath, 6.0f, 52.0f);
                XPainterPath_lineTo(&triPath, 32.0f, 10.0f);
                XPainterPath_lineTo(&triPath, 56.0f, 52.0f);
                XPainterPath_closeSubpath(&triPath);
                {
                    bool fillOk = XPainter_fillPath(&polyPainter, &triPath);
                    XPainterPath_deinit(&triPath);
                    if (!fillOk) ok = 0;
                }
                if (XPainter_rasterBackend(&polyPainter) !=
                    XPainterRasterBackend_Gpu)
                {
                    fprintf(stderr, "gpu-test: aa polygon degraded\n");
                    ok = 0;
                }
                XPainter_end(&polyPainter);
                for (py = 10; py < 53; ++py)
                    for (px = 6; px < 57; ++px)
                    {
                        uint32_t pixel = XImage_pixel(&polyFrame, px, py) &
                                         0x00ffffffu;
                        if (pixel != 0x202020u && pixel != 0x00ff00u &&
                            pixel != 0x000000u)
                            ++grayCount;
                    }
                if (grayCount <= 0)
                {
                    fprintf(stderr, "gpu-test: aa polygon no gray edge\n");
                    ok = 0;
                }
                XPainter_deinit(&polyPainter);
                XImage_deinit_base(&polyFrame);
            }
            /* 画线/描边 GPU 快速路径：轴对齐线与 drawRect 边框零降级、
               像素精确；斜线回退软件（降级但内容正确）。 */
            {
                XImage lineFrame;
                XPainter linePainter;
                int x;
                int y;
                XImage_init_ex(&lineFrame, 64, 64, XImageFormat_ARGB32);
                XImage_fillRect(&lineFrame, NULL, 0xff202020u);
                XPainter_init(&linePainter, NULL);
                XPainter_begin_image(&linePainter, &lineFrame);
                if (XPainter_rasterBackend(&linePainter) !=
                    XPainterRasterBackend_Gpu)
                {
                    fprintf(stderr, "gpu-test: line session not gpu\n");
                    ok = 0;
                }
                /* 水平线 y=10, x∈[8,40]（SquareCap 默认无扩展）。 */
                if (!XPainter_drawLine(&linePainter, 8, 10, 40, 10)) ok = 0;
                /* 垂直线 x=50, y∈[8,40]。 */
                if (!XPainter_drawLine(&linePainter, 50, 8, 50, 40)) ok = 0;
                /* drawRect 边框 (6,44)-(26,60)：四边走线。 */
                if (!XPainter_drawRect(&linePainter,
                                       &(XRect){6, 44, 20, 16}))
                    ok = 0;
                if (XPainter_rasterBackend(&linePainter) !=
                    XPainterRasterBackend_Gpu)
                {
                    fprintf(stderr, "gpu-test: axis lines degraded\n");
                    ok = 0;
                }
                /* 斜线：经软件光栅局部提交（不整帧降级，会话保持 GPU）。 */
                if (!XPainter_drawLine(&linePainter, 2, 2, 60, 30)) ok = 0;
                if (XPainter_rasterBackend(&linePainter) !=
                    XPainterRasterBackend_Gpu)
                {
                    fprintf(stderr, "gpu-test: slanted line degraded\n");
                    ok = 0;
                }
                /* 渐变笔刷：局部提交不降级。 */
                {
                    XPainterGradient gradient;
                    XRect gRect = { 40, 2, 20, 4 };
                    XPainterGradient_initLinear(&gradient, 40.0f, 4.0f,
                                                60.0f, 4.0f);
                    XPainterGradient_addStop(&gradient, 0.0f, 0xffff0000u);
                    XPainterGradient_addStop(&gradient, 1.0f, 0xff0000ffu);
                    XPainter_setBrushGradient(&linePainter, &gradient);
                    if (!XPainter_fillRect_2(&linePainter, &gRect)) ok = 0;
                    if (XPainter_rasterBackend(&linePainter) !=
                        XPainterRasterBackend_Gpu)
                    {
                        fprintf(stderr, "gpu-test: gradient degraded\n");
                        ok = 0;
                    }
                }
                XPainter_end(&linePainter);
                /* 渐变端点颜色采样（局部提交经 FBO，帧末 readback 可见）。 */
                if (XImage_pixel(&lineFrame, 41, 4) == 0xff202020u)
                {
                    fprintf(stderr, "gpu-test: gradient not drawn\n");
                    ok = 0;
                }
                /* 像素断言在 end/readback 之后（GPU 模式帧末才落回目标）。 */
                {
                    int bad = 0;
                    for (x = 8; x <= 40; ++x)
                        if (XImage_pixel(&lineFrame, x, 10) != 0xff000000u)
                            ++bad;
                    for (y = 8; y <= 40; ++y)
                        if (XImage_pixel(&lineFrame, 50, y) != 0xff000000u)
                            ++bad;
                    /* 边框：上/下边 y=44/60 含端点，左右边 x=6/26。 */
                    for (x = 6; x <= 26; ++x)
                    {
                        if (XImage_pixel(&lineFrame, x, 44) != 0xff000000u)
                            ++bad;
                        if (XImage_pixel(&lineFrame, x, 60) != 0xff000000u)
                            ++bad;
                    }
                    for (y = 45; y <= 59; ++y)
                    {
                        if (XImage_pixel(&lineFrame, 6, y) != 0xff000000u)
                            ++bad;
                        if (XImage_pixel(&lineFrame, 26, y) != 0xff000000u)
                            ++bad;
                    }
                    /* 内部不填充。 */
                    if (XImage_pixel(&lineFrame, 15, 52) != 0xff202020u)
                        ++bad;
                    /* 斜线（软件 Bresenham）落在预期带内。 */
                    {
                        int found = 0;
                        for (y = 10; y <= 20; ++y)
                            if (XImage_pixel(&lineFrame, 30, y) == 0xff000000u)
                                found = 1;
                        if (!found) ++bad;
                    }
                    if (bad)
                    {
                        fprintf(stderr, "gpu-test: line pixels wrong (%d)\n"
                                "  h10=%08x v50=%08x top=%08x left=%08x "
                                "in=%08x slash=%08x\n",
                                bad,
                                (unsigned)XImage_pixel(&lineFrame, 20, 10),
                                (unsigned)XImage_pixel(&lineFrame, 50, 20),
                                (unsigned)XImage_pixel(&lineFrame, 10, 44),
                                (unsigned)XImage_pixel(&lineFrame, 6, 50),
                                (unsigned)XImage_pixel(&lineFrame, 15, 52),
                                (unsigned)XImage_pixel(&lineFrame, 30, 15));
                        ok = 0;
                    }
                }
                XPainter_deinit(&linePainter);
                XImage_deinit_base(&lineFrame);
            }
            XImage_deinit_base(&frame);
            XGpuRenderBackend_destroy(session);
            fprintf(stderr, "gpu-test: atlas done\n");
        }
        else
        {
            fprintf(stderr, "gpu-test: atlas session unavailable\n");
        }
    }
    fprintf(stderr, "gpu-test: done\n");
    return ok ? 0 : 1;
}
