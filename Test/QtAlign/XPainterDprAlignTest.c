/**
 * @file       XPainterDprAlignTest.c
 * @brief      XPainter DPR 渲染链 × Qt 6.8 对齐契约测试。
 * @details    对齐依据（docs/Qt68对齐_远端a4d9af05合规扫描_20261112.md
 *             「合并后整改清单」P0-1/P0-2 与 ASan 实锤项）：
 *             - Qt 6.8.3 源码 qtbase/src/gui/painting/qpaintengine_raster.cpp
 *               drawTextItem（:3084-3091）：逻辑笔点经
 *               matrix.translate(p) 过**整矩阵一次**得到设备笔点——
 *               DPR 纯缩放分支的设备笔点恒为 x*devScale + dx，不得把
 *               已是设备域的 transform.dx 再乘 devScale（同文件 8x16
 *               点阵 DPR 分支 devOriginX 同式）；
 *             - Qt fillRect_normalized（:1383-1395）：混合前与设备矩形
 *               求交——本侧等价物为 painterRaster_blendFillRect 入口的
 *               图像边界钳位（透明快路径 XImage_fillRect 内部自钳位、
 *               逐像素回退路径 painterFloorClamp/painterCeilClamp 限幅，
 *               混合路径是唯一无边界防御的点，2026-11-12 ASan/LSan 在
 *               XSlider.handleGeometryFromPositionAndUpsideDown 抓到
 *               heap-buffer-overflow READ 实锤）。
 *             覆盖：
 *             - glyphPenDevicePositionDpr2WorldTranslate：dpr=2 + 世界
 *               translate(100,50) 的轮廓字形设备笔点 = 逻辑
 *               x*devScale + dx（逻辑 (10,20) → 设备 (220,140)）。
 *               断法：与 dpr=2 无平移、逻辑 (110,70) 的渲染逐字节相等
 *               （dx=0 参照位对旧式 (x+dx)*devScale 恒正确，A==B 即锁
 *               死正确式），并断言墨迹非空 + 落点窗；旧式下 A 渲染笔点
 *               420 越出图像，无墨 + 不等两断言同时爆红。探针删除
 *               （dpr>1 硬 return false）后 DPR SW-AA 纯缩放链经本
 *               用例保证可达；
 *             - fillRectBlendClampedAtImageEdge：半透明 fillRect 的
 *               贴边精确（右/下边==图像宽高）、右/下越界、负原点三种
 *               几何在 ARGB32_Premultiplied 直写分支下钳位于图像边界；
 *             - fillRectKernelBlendClampedAtImageEdge：同三几何走
 *               ARGB32 内核 fillSpanBlend 分支（ASan 实锤路径）。
 *             期望值均为手算可复核（背景 0 + 源 0x28FF0000）：
 *             预乘分支 sa=40、spR=(255*40+127)/255=40 → 0x28280000；
 *             内核分支 da=0 存非预乘 → 0x28FF0000。
 *             断言取样一律走**原始存储字节**（xpd_rawPixel：
 *             XImage_constBits + 原生 32 位载入），因 Qt 6.8.3
 *             QImage::pixel 对 Format_ARGB32_Premultiplied 返回
 *             **原始存储 QRgb**（qimage.cpp:2523-2525 "Keep old
 *             behaviour"），预乘期望值 0x28280000 即真实存储字节；
 *             本侧 XImage_pixel 现对预乘格式按「逻辑色」口径反预乘
 *             读回（XImage.c readPixelValue），读 0x28280000 会返回
 *             0x28FF0000（unpremultiply8(0x28,0x28)=0xFF）。该读取
 *             语义是共享框架契约（painterRaster_putPixel 的合成读
 *             回、图像编解码、其余取样型用例均依赖），属共享根因
 *             另行交底，本用例不断言它——按原始字节取样对 ARGB32
 *             两种格式与 Qt 逐位一致，覆盖只增不减。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XPAINTER_ON

#include "XPainter.h"
#include "XImage.h"
#include "XFont.h"
#include "XFontFace.h"
#include "XFontFt.h"
#include <string.h>

/* ---- 半透明填充期望值（背景 0 + 源 0x28FF0000，手算可复核） ---- */
#define XPD_FILL_SRC 0x28FF0000u
#define XPD_FILL_EXPECT_PREMUL 0x28280000u
#define XPD_FILL_EXPECT_ARGB32 0x28FF0000u

/** @brief 按存储布局读原始 32 位像素（Qt QImage::pixel 口径）。
 *  @details Qt 6.8.3 qimage.cpp:2523-2525 对 Format_ARGB32 与
 *           Format_ARGB32_Premultiplied 同式返回原始存储 QRgb
 *           （reinterpret_cast 原生 32 位载入）；XImage_load32 与之
 *           同为原生载入（XImage.c:3445-3450 memcpy），故两种格式
 *           （均 4 字节/像素）按原始字节取样与 Qt 逐位一致。
 *           不走 XImage_pixel：其对预乘存储按逻辑色口径反预乘读回
 *           （XImage_readPixelValue，XImage.c:3772-3779），读
 *           0x28280000 返回 0x28FF0000——该共享契约不在本用例
 *           断言范围（见文件头注），这里只核对真实存储字节。 */
static uint32_t xpd_rawPixel(const XImage* img, int x, int y)
{
    const uint8_t* base;
    size_t bpl;
    uint32_t value = 0u;
    if (!img || x < 0 || y < 0 || x >= XImage_width(img) ||
        y >= XImage_height(img))
        return 0u;
    /* 只读取样走 constBits（不触发 detach/换版本，与 Qt QImage::pixel
       的 const 读路径同口径）；bits() 可写取用会 detach，取样不需要。 */
    base = XImage_constBits(img);
    bpl = (size_t)XImage_bytesPerLine(img);
    if (!base || bpl < (size_t)XImage_width(img) * 4u)
        return 0u;
    memcpy(&value, base + (size_t)y * bpl + (size_t)x * 4u, sizeof(value));
    return value;
}

/** @brief 断言 [x1,x2)×[y1,y2) 内每像素等于 expected，不符打印首个
 *         反例坐标（整图均匀填充的期望断言）。 */
static void xpd_checkRectUniform(XImage* img, int x1, int y1, int x2,
                                 int y2, uint32_t expected,
                                 const char* tag)
{
    int x;
    int y;
    for (y = y1; y < y2; ++y)
    {
        for (x = x1; x < x2; ++x)
        {
            uint32_t px = xpd_rawPixel(img, x, y);
            if (px != expected)
            {
                xq_check_impl(false, "pixel == expected", __FILE__,
                              __LINE__,
                              "%s: pixel(%d,%d)=0x%08x expected=0x%08x",
                              tag, x, y, px, expected);
                return;
            }
        }
    }
    xq_check_impl(true, "rect uniform", __FILE__, __LINE__, "%s", tag);
}

/** @brief 单像素期望断言（原始存储字节口径，见 xpd_rawPixel）。 */
static void xpd_checkPixel(XImage* img, int x, int y, uint32_t expected,
                           const char* tag)
{
    uint32_t px = xpd_rawPixel(img, x, y);
    xq_check_impl(px == expected, "pixel == expected", __FILE__, __LINE__,
                  "%s: pixel(%d,%d)=0x%08x expected=0x%08x", tag, x, y, px,
                  expected);
}

/* ==================== 用例 1：dpr=2 轮廓字形设备笔点 ==================== */

#if XFONT_FREETYPE_ON && XFONT_OUTLINE_ON && XPAINTER_PATH_ON

/** @brief 渲染一帧 dpr=2 的 "A"（图像 256×160 预乘，失败也保证已
 *         init 的图像可被调用方 XClassDeinit 安全回收）。
 *  @param translated true=世界 translate(100,50) + drawText(10,20)
 *                    （设备笔点应为 (220,140)）；
 *                    false=无平移 + drawText(110,70)（dx=0 参照位，
 *                    两种算式下设备笔点同为 (220,140)）。 */
static bool xpd_renderGlyphDpr2(XImage* img, bool translated)
{
    XFont font;
    XPainter painter;
    bool ok;
    XFont_init_ex(&font, "XFontOutlineCommon", 16, -1, false);
    XImage_init_ex(img, 256, 160, XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(img))
    {
        XClassDeinit((XClass*)&font);
        return false;
    }
    XImage_fill(img, 0u);
    XImage_setDevicePixelRatio(img, 2.0f);
    XPainter_init(&painter, NULL);
    ok = XPainter_begin_image(&painter, img);
    if (ok)
    {
        if (translated) XPainter_translate(&painter, 100.0f, 50.0f);
        XPainter_setFont(&painter, &font);
        ok = XPainter_drawText(&painter, translated ? 10 : 110,
                               translated ? 20 : 70, "A", 0xFF000000u);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XClassDeinit((XClass*)&font);
    return ok;
}

XQ_TEST(XPainterDpr, glyphPenDevicePositionDpr2WorldTranslate)
{
    XFont probe;
    XFontOutlineInfo info;
    XImage imgA;
    XImage imgB;
    const uint8_t* bitsA;
    const uint8_t* bitsB;
    int minX = 0x7FFFFFFF;
    int maxX = -1;
    int minY = 0x7FFFFFFF;
    int maxY = -1;
    int x;
    int y;
    /* 前提：缺省家族 "XFontOutlineCommon" 经 FT 外挂链命中
       （Library/XFont/XFontOutlineCommon.otf，XFONT_EXTERNAL_FT_FONT_DIR
       缺省 ../Library/XFont + exe 目录前缀变体）——否则 drawText 退化
       到点阵回退链、锁不到轮廓 DPR 纯缩放分支，必须响失败。 */
    XFont_init_ex(&probe, "XFontOutlineCommon", 16, -1, false);
    memset(&info, 0, sizeof(info));
    if (!(XFontFt_fileInfo(&probe, &info) && info.unitsPerEm > 0))
    {
        XClassDeinit((XClass*)&probe);
        xq_fail_at(__FILE__, __LINE__,
                   "XFontOutlineCommon 未命中 FT 轮廓字库（缺 "
                   "Library/XFont/XFontOutlineCommon.otf），轮廓 DPR 链"
                   "不可测");
        return;
    }
    XClassDeinit((XClass*)&probe);

    if (!xpd_renderGlyphDpr2(&imgA, true))
    {
        XClassDeinit(&imgA);
        xq_fail_at(__FILE__, __LINE__, "dpr=2 + translate 渲染失败");
        return;
    }
    if (!xpd_renderGlyphDpr2(&imgB, false))
    {
        XClassDeinit(&imgA);
        XClassDeinit(&imgB);
        xq_fail_at(__FILE__, __LINE__, "dpr=2 无平移参照渲染失败");
        return;
    }

    /* 正确式：penX = x*devScale + dx → A: 10*2+200 = 220 = B: 110*2+0。
       逐字节相等（含 bytesPerLine 裙边，两图同尺寸同格式同背景）。 */
    bitsA = (const uint8_t*)XImage_bits(&imgA);
    bitsB = (const uint8_t*)XImage_bits(&imgB);
    if (!bitsA || !bitsB)
    {
        XClassDeinit(&imgA);
        XClassDeinit(&imgB);
        xq_fail_at(__FILE__, __LINE__, "图像 bits 不可得");
        return;
    }
    {
        size_t plane = (size_t)XImage_bytesPerLine(&imgA) *
                       (size_t)XImage_height(&imgA);
        XQ_CHECK_MSG(XImage_bytesPerLine(&imgA) ==
                         XImage_bytesPerLine(&imgB),
                     "A/B bytesPerLine 一致");
        XQ_CHECK_MSG(memcmp(bitsA, bitsB, plane) == 0,
                     "dpr=2+translate(100,50) 的字形渲染与 dx=0 参照位"
                     "逐字节相等（设备笔点=x*devScale+dx）");
    }

    /* 墨迹非空 + 落点窗：16px 'A' 在 dpr=2 的墨迹应落在设备
       (220,140) 笔点附近——x ∈ [205,300]、y ∈ [90,155]（含左右
       bearing 与 ascender 全余量）。旧式 (x+dx)*devScale 下 A 笔点
       420 越出 256 宽图像 → 无墨，本断言先爆。 */
    for (y = 0; y < XImage_height(&imgA); ++y)
    {
        for (x = 0; x < XImage_width(&imgA); ++x)
        {
            if (XImage_pixel(&imgA, x, y) != 0u)
            {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    XQ_CHECK_MSG(maxX >= 0, "字形墨迹非空");
    if (maxX >= 0)
    {
        XQ_CHECK_MSG(minX >= 205 && maxX <= 300,
                     "墨迹 X 落点窗 [205,300]（左=%d 右=%d，期望锚 220）",
                     minX, maxX);
        XQ_CHECK_MSG(minY >= 90 && maxY <= 155,
                     "墨迹 Y 落点窗 [90,155]（顶=%d 底=%d，期望锚 140）",
                     minY, maxY);
    }

    XClassDeinit(&imgA);
    XClassDeinit(&imgB);
}

#endif /* XFONT_FREETYPE_ON && XFONT_OUTLINE_ON && XPAINTER_PATH_ON */

/* ==================== 用例 2/3：半透明 fillRect 图像边界钳位 ==================== */

XQ_TEST(XPainterDpr, fillRectBlendClampedAtImageEdge)
{
    XImage img;
    XPainter painter;
    XImage_init_ex(&img, 64, 32, XImageFormat_ARGB32_Premultiplied);
    if (XImage_isNull(&img))
    {
        XClassDeinit(&img);
        xq_fail_at(__FILE__, __LINE__, "图像分配失败");
        return;
    }
    XImage_setDevicePixelRatio(&img, 2.0f); /* 逻辑 32×16 */
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &img))
    {
        XPainter_deinit(&painter);
        XClassDeinit(&img);
        xq_fail_at(__FILE__, __LINE__, "begin 失败");
        return;
    }

    /* 相 1：贴边精确——逻辑 (0,0,32,16) → 设备 (0,0,64,32)，右/下边
       == 图像宽高，恰好铺满；混合直写不得越界。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){0, 0, 32, 16},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 64, 32, XPD_FILL_EXPECT_PREMUL,
                         "premul 贴边精确");

    /* 相 2：右/下越界——逻辑 (0,0,40,20) → 设备 (0,0,80,40)，超出
       图像 16×8px；DPR 纯缩放分支映射后钳位回 (0,0,64,32)。无钳位时
       行循环越出缓冲区 8 行（2026-11-12 ASan heap-buffer-overflow
       READ 回归锁）。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){0, 0, 40, 20},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 64, 32, XPD_FILL_EXPECT_PREMUL,
                         "premul 右/下越界钳位");

    /* 相 3：负原点——逻辑 (-4,-4,20,20) → 设备 (-8,-8,40,40)，负 x/y
       夹 0 后钳位为 (0,0,32,32)：左下 32×32 混合、右下半保持背景。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){-4, -4, 20, 20},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 32, 32, XPD_FILL_EXPECT_PREMUL,
                         "premul 负原点钳位段");
    xpd_checkPixel(&img, 63, 31, 0u, "premul 负原点段外保持背景");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XClassDeinit(&img);
}

XQ_TEST(XPainterDpr, fillRectKernelBlendClampedAtImageEdge)
{
    XImage img;
    XPainter painter;
    /* ARGB32（非预乘）：走 XRenderKernel fillSpanBlend 内核分支
       （ASan 实锤路径 argb32_fillSpanBlend）。 */
    XImage_init_ex(&img, 64, 32, XImageFormat_ARGB32);
    if (XImage_isNull(&img))
    {
        XClassDeinit(&img);
        xq_fail_at(__FILE__, __LINE__, "图像分配失败");
        return;
    }
    XImage_setDevicePixelRatio(&img, 2.0f);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, &img))
    {
        XPainter_deinit(&painter);
        XClassDeinit(&img);
        xq_fail_at(__FILE__, __LINE__, "begin 失败");
        return;
    }

    /* 相 1：贴边精确（右/下边==图像宽高）。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){0, 0, 32, 16},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 64, 32, XPD_FILL_EXPECT_ARGB32,
                         "argb32 内核 贴边精确");

    /* 相 2：右/下越界——内核分支行循环同样钳位（本用例即 2026-11-12
       ASan 栈 argb32_fillSpanBlend 越界读的回归锁）。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){0, 0, 40, 20},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 64, 32, XPD_FILL_EXPECT_ARGB32,
                         "argb32 内核 右/下越界钳位");

    /* 相 3：负原点。 */
    XImage_fill(&img, 0u);
    XQ_CHECK(XPainter_fillRect(&painter, &(XRect){-4, -4, 20, 20},
                               XPD_FILL_SRC));
    xpd_checkRectUniform(&img, 0, 0, 32, 32, XPD_FILL_EXPECT_ARGB32,
                         "argb32 内核 负原点钳位段");
    xpd_checkPixel(&img, 63, 31, 0u, "argb32 内核 负原点段外保持背景");

    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XClassDeinit(&img);
}

#endif /* XGUI_ON && XPAINTER_ON */
