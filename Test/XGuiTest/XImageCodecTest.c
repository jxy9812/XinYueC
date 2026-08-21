#include "XGuiTest.h"
#if DEMOTEST && XIMAGECODEC_ON
#include "XImageCodec.h"
#include "XImage.h"
#include "XImageReader.h"
#include "XImageWriter.h"
#include "XByteArray.h"
#include "XMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XFile.h"
#include <string.h>
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
#include "codec_anim_fixture.h"
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */

/* ==================== XImageCodec 全量测试 ==================== */

static int s_codecFailures = 0;

static void codec_expect(bool condition, const char* name)
{
    if (!condition) {
        XPrintf("[FAIL] %s\n", name);
        s_codecFailures++;
    }
}

/**
 * @brief      测试格式名解析、格式名输出与能力查询
 */
static void XImageCodecRegistryTest(void)
{
    XPrintf("===== 格式注册与能力查询测试 =====\n");
    codec_expect(XImageCodec_formatFromName_2("bmp") == XImageCodecFormat_Bmp,
                 "formatFromName('bmp')");
    codec_expect(XImageCodec_formatFromName_2("PNG") == XImageCodecFormat_Png,
                 "formatFromName('PNG') 不区分大小写");
    codec_expect(XImageCodec_formatFromName_2("jpg") == XImageCodecFormat_Jpeg &&
                 XImageCodec_formatFromName_2("jpeg") == XImageCodecFormat_Jpeg,
                 "formatFromName('jpg'/'jpeg')");
    codec_expect(XImageCodec_formatFromName_2("gif") == XImageCodecFormat_Gif,
                 "formatFromName('gif')");
    codec_expect(XImageCodec_formatFromName_2("svg") == XImageCodecFormat_Svg &&
                 XImageCodec_formatFromName_2("svgz") == XImageCodecFormat_Svg,
                 "formatFromName('svg'/'svgz')");
    codec_expect(XImageCodec_formatFromName_2("webp") == XImageCodecFormat_Unknown &&
                 XImageCodec_formatFromName_2(NULL) == XImageCodecFormat_Unknown,
                 "未知/空格式返回 Unknown");
    codec_expect(strcmp(XImageCodec_formatName_2(XImageCodecFormat_Bmp), "bmp") == 0 &&
                 strcmp(XImageCodec_formatName_2(XImageCodecFormat_Png), "png") == 0 &&
                 strcmp(XImageCodec_formatName_2(XImageCodecFormat_Jpeg), "jpeg") == 0 &&
                 strcmp(XImageCodec_formatName_2(XImageCodecFormat_Gif), "gif") == 0 &&
                 strcmp(XImageCodec_formatName_2(XImageCodecFormat_Svg), "svg") == 0 &&
                 XImageCodec_formatName_2(XImageCodecFormat_Unknown) == NULL,
                 "formatName_2 与枚举一一对应");
    codec_expect(XImageCodec_canDecode(XImageCodecFormat_Bmp) &&
                 XImageCodec_canDecode(XImageCodecFormat_Png) &&
                 XImageCodec_canDecode(XImageCodecFormat_Jpeg) &&
                 XImageCodec_canDecode(XImageCodecFormat_Gif) &&
                 XImageCodec_canDecode(XImageCodecFormat_Svg),
                 "BMP/PNG/JPEG/GIF/SVG 可解码");
    codec_expect(XImageCodec_canEncode(XImageCodecFormat_Bmp) &&
                 XImageCodec_canEncode(XImageCodecFormat_Png) &&
                 XImageCodec_canEncode(XImageCodecFormat_Jpeg) &&
                 XImageCodec_canEncode(XImageCodecFormat_Gif) &&
                 XImageCodec_canEncode(XImageCodecFormat_Svg),
                 "BMP/PNG/JPEG/GIF/SVG 可编码");
    codec_expect(!XImageCodec_canDecode(XImageCodecFormat_Unknown) &&
                 !XImageCodec_canEncode(XImageCodecFormat_Unknown),
                 "Unknown 无编解码后端");
    XPrintf("\n");
}

/**
 * @brief      测试文件头自动识别
 */
static void XImageCodecDetectTest(void)
{
    static const uint8_t bmpHeader[] = {'B', 'M', 0, 0};
    static const uint8_t pngHeader[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    static const uint8_t jpegHeader[] = {0xff, 0xd8, 0xff, 0xe0};
    static const uint8_t gifHeader[] = {'G', 'I', 'F', '8', '9', 'a'};
    static const uint8_t svgHeader[] = {'<', 's', 'v', 'g', ' ', 'x'};
    static const uint8_t bad[] = {'x', 'y', 'z'};
    XPrintf("===== 文件头识别测试 =====\n");
    codec_expect(XImageCodec_detect(bmpHeader, sizeof(bmpHeader)) == XImageCodecFormat_Bmp, "识别 BMP");
    codec_expect(XImageCodec_detect(pngHeader, sizeof(pngHeader)) == XImageCodecFormat_Png, "识别 PNG");
    codec_expect(XImageCodec_detect(jpegHeader, sizeof(jpegHeader)) == XImageCodecFormat_Jpeg, "识别 JPEG");
    codec_expect(XImageCodec_detect(gifHeader, sizeof(gifHeader)) == XImageCodecFormat_Gif, "识别 GIF");
    codec_expect(XImageCodec_detect(svgHeader, sizeof(svgHeader)) == XImageCodecFormat_Svg, "识别 SVG");
    codec_expect(XImageCodec_detect(bad, sizeof(bad)) == XImageCodecFormat_Unknown, "未知数据返回 Unknown");
    codec_expect(XImageCodec_detect(NULL, 0) == XImageCodecFormat_Unknown, "空数据返回 Unknown");
    XPrintf("\n");
}

/**
 * @brief      单格式往返测试：编码 -> 识别 -> 解码 -> 像素校验
 */
static void codecRoundTripOne(XImageCodecFormat format, bool checkPixel,
                              const char* name)
{
    static const uint32_t sourcePixels[6] = {
        0xffff0000u, 0xff00ff00u, 0xff0000ffu,
        0x80402010u, 0xffffffffu, 0xff102030u
    };
    XImage source, decoded;
    XByteArray* bytes = XByteArray_create();
    bool ok;
    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            XImage_setPixel(&source, x, y, sourcePixels[y * 3 + x]);
    XImage_init(&decoded);
    ok = bytes && XImageCodec_encode(&source, format, -1, bytes) &&
         XImageCodec_detect(XByteArray_data(bytes),
                            XByteArray_size_base((const XContainer*)bytes)) == format &&
         XImageCodec_decode(XByteArray_data(bytes),
                            XByteArray_size_base((const XContainer*)bytes),
                            XImageCodecFormat_Unknown, &decoded);
    codec_expect(ok, name);
    if (ok) {
        codec_expect(XImage_width(&decoded) == 3 && XImage_height(&decoded) == 2,
                     "编解码往返保持尺寸");
        if (checkPixel) {
            codec_expect(XImage_pixel(&decoded, 0, 0) == sourcePixels[0] &&
                         XImage_pixel(&decoded, 1, 0) == sourcePixels[1] &&
                         XImage_pixel(&decoded, 2, 0) == sourcePixels[2],
                         "第一行像素完全一致");
            codec_expect(XImage_pixel(&decoded, 0, 1) == sourcePixels[3] &&
                         XImage_pixel(&decoded, 1, 1) == sourcePixels[4] &&
                         XImage_pixel(&decoded, 2, 1) == sourcePixels[5],
                         "第二行像素完全一致");
        }
    }
    XImage_deinit_base(&decoded);
    XImage_deinit_base(&source);
    if (bytes) XByteArray_delete_base((XClass*)bytes);
}

/**
 * @brief      JPEG 有损往返测试：编码 -> 识别 -> 解码 -> 容差像素校验。
 * @param quality     质量参数 1..100（<=0 由 codec 取默认 75）。
 * @param name        测试名称。
 * @param tolerance   单通道最大允许误差（JPEG 为有损格式，不能逐像素精确比对）。
 */
static void codecRoundTripJpegOne(int quality, const char* name, int tolerance)
{
    XImage source, decoded;
    XByteArray* bytes = XByteArray_create();
    bool ok;
    const int w = 24, h = 16;   /* 24x16：多个 MCU，覆盖 4:2:0 色度下采样 */
    XImage_init_ex(&source, w, h, XImageFormat_ARGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int r = (x * 255) / (w - 1);
            int g = (y * 255) / (h - 1);
            int b = ((x + y) * 127) / (w + h - 2);
            XImage_setPixel(&source, x, y,
                            0xff000000u | ((uint32_t)r << 16) |
                            ((uint32_t)g << 8) | (uint32_t)b);
        }
    }
    XImage_init(&decoded);
    ok = bytes && XImageCodec_encode(&source, XImageCodecFormat_Jpeg, quality, bytes) &&
         XImageCodec_detect(XByteArray_data(bytes),
                            XByteArray_size_base((const XContainer*)bytes)) == XImageCodecFormat_Jpeg &&
         XImageCodec_decode(XByteArray_data(bytes),
                            XByteArray_size_base((const XContainer*)bytes),
                            XImageCodecFormat_Unknown, &decoded);
    codec_expect(ok, name);
    if (ok) {
        int maxErr = 0;
        codec_expect(XImage_width(&decoded) == w && XImage_height(&decoded) == h,
                     "JPEG 编解码往返保持尺寸");
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t a = XImage_pixel(&source, x, y);
                uint32_t b2 = XImage_pixel(&decoded, x, y);
                int er = (int)((a >> 16) & 0xffu) - (int)((b2 >> 16) & 0xffu);
                int eg = (int)((a >> 8) & 0xffu) - (int)((b2 >> 8) & 0xffu);
                int eb = (int)(a & 0xffu) - (int)(b2 & 0xffu);
                if (er < 0) er = -er;
                if (eg < 0) eg = -eg;
                if (eb < 0) eb = -eb;
                if (er > maxErr) maxErr = er;
                if (eg > maxErr) maxErr = eg;
                if (eb > maxErr) maxErr = eb;
            }
        }
        codec_expect(maxErr <= tolerance, "JPEG 有损误差在容差范围内");
        if (maxErr > tolerance)
            XPrintf("      质量=%d 最大通道误差=%d（容差=%d）\n",
                    quality, maxErr, tolerance);
    }
    XImage_deinit_base(&decoded);
    XImage_deinit_base(&source);
    if (bytes) XByteArray_delete_base((XClass*)bytes);
}

/**
 * @brief      测试各格式往返
 */
static void XImageCodecRoundTripTest(void)
{
    XPrintf("===== 各格式编解码往返测试 =====\n");
    s_codecFailures = 0;
    codecRoundTripOne(XImageCodecFormat_Bmp, true, "BMP 往返");
    codecRoundTripOne(XImageCodecFormat_Png, true, "PNG 往返");
    /* GIF 使用 3-3-2 量化调色板，逐像素精确比较不适用，仅校验可解码且尺寸正确 */
    codecRoundTripOne(XImageCodecFormat_Gif, false, "GIF 往返");
    codecRoundTripOne(XImageCodecFormat_Svg, true, "SVG 往返");
    /* JPEG 为有损格式（YCbCr 4:2:0 + 量化），分别验证默认/高/低质量往返 */
    codecRoundTripJpegOne(-1, "JPEG 往返（默认质量 75）", 32);
    codecRoundTripJpegOne(100, "JPEG 往返（高质量 100）", 24);
    codecRoundTripJpegOne(1, "JPEG 往返（低质量 1）", 96);
    XPrintf("本轮失败数: %d\n", s_codecFailures);
    XPrintf("\n");
}

/**
 * @brief      测试 SVG 纯色矩形解码
 */
static void XImageCodecSvgSolidTest(void)
{
    XImage image;
    const char* svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"2\">"
                      "<rect width=\"4\" height=\"2\" fill=\"#336699\"/></svg>";
    XPrintf("===== SVG 纯色矩形解码测试 =====\n");
    XImage_init(&image);
    codec_expect(XImageCodec_decode((const uint8_t*)svg, strlen(svg),
                                    XImageCodecFormat_Svg, &image),
                 "纯色 SVG 解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_width(&image) == 4 && XImage_height(&image) == 2,
                     "纯色 SVG 尺寸正确");
        codec_expect(XImage_pixel(&image, 0, 0) == 0xff336699u &&
                     XImage_pixel(&image, 3, 1) == 0xff336699u,
                     "纯色 SVG 颜色正确");
    }
    XImage_deinit_base(&image);
    XPrintf("\n");
}

/* ================== SVG 矢量渲染（XIMAGECODEC_SVG_VECTOR_ON） ================== */

#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
/**
 * @brief 以纯色/透明背景解码一段 SVG 文本。
 */
static bool svgVectorDecode(const char* svg, XImage* image)
{
    return XImageCodec_decode((const uint8_t*)svg, strlen(svg),
                              XImageCodecFormat_Svg, image);
}

/**
 * @brief      测试 SVG 矢量渲染：渐变/图形/路径/文字/viewBox/变换/描边/透明度
 */
static void XImageCodecSvgVectorTest(void)
{
    XImage image;
    int filled;
    uint32_t v;
    XPrintf("===== SVG 矢量渲染测试 =====\n");

    /* 线性渐变：左红右蓝 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect width=\"4\" height=\"4\" fill=\"url(#g)\"/></svg>", &image),
        "SVG 线性渐变解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect((XImage_pixel(&image, 0, 2) & 0xff0000u) != 0,
                     "渐变左端为红色系");
        codec_expect((XImage_pixel(&image, 3, 2) & 0xffu) != 0,
                     "渐变右端为蓝色系");
    }
    XImage_deinit_base(&image);

    /* 径向渐变：中心亮、边缘暗 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"6\" height=\"6\">"
        "<defs><radialGradient id=\"re\" cx=\"0.5\" cy=\"0.5\" r=\"0.5\">"
        "<stop offset=\"0\" stop-color=\"#ffffff\"/>"
        "<stop offset=\"1\" stop-color=\"#000000\"/>"
        "</radialGradient></defs>"
        "<rect width=\"6\" height=\"6\" fill=\"url(#re)\"/></svg>", &image),
        "SVG 径向渐变解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect((XImage_pixel(&image, 3, 3) & 0x00ffffffu) > 0x00f0f0f0u,
                     "径向渐变中心近白");
        codec_expect((XImage_pixel(&image, 0, 0) & 0xffu) < 0x20u,
                     "径向渐变四角近黑");
    }
    XImage_deinit_base(&image);

    /* 圆形 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<circle cx=\"4\" cy=\"4\" r=\"3.5\" fill=\"#00ff00\"/></svg>", &image),
        "SVG 圆形解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 4, 4) == 0xff00ff00u,
                     "圆形中心覆盖");
        codec_expect(XImage_pixel(&image, 0, 0) == 0x00000000u,
                     "圆形外部透明");
    }
    XImage_deinit_base(&image);

    /* 椭圆 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<ellipse cx=\"4\" cy=\"2\" rx=\"3\" ry=\"1.5\" fill=\"#00ff00\"/></svg>",
        &image), "SVG 椭圆解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 4, 2) == 0xff00ff00u &&
                     XImage_pixel(&image, 0, 2) == 0x00000000u,
                     "椭圆填充与外部空白");
    }
    XImage_deinit_base(&image);

    /* path 三角形（由 M/L/Z 组成） */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<path d=\"M1 5 L1 1 L7 3 Z\" fill=\"#ff8800\"/></svg>", &image),
        "SVG path 三角形解码成功");
    if (!XImage_isNull(&image)) {
        filled = 0;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 8; ++x)
                if (XImage_pixel(&image, x, y) == 0xffff8800u) ++filled;
        codec_expect(filled > 6, "path 三角形面积填充");
    }
    XImage_deinit_base(&image);

    /* path 圆弧（A 命令） */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<path d=\"M1 5 A3 3 0 0 1 7 5 L7 7 L1 7 Z\" fill=\"#00aaff\"/></svg>",
        &image), "SVG path 圆弧解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 4, 5) == 0xff00aaffu &&
                     XImage_pixel(&image, 1, 6) == 0xff00aaffu,
                     "圆弧形状主体覆盖");
        codec_expect(XImage_pixel(&image, 4, 0) == 0x00000000u &&
                     XImage_pixel(&image, 0, 0) == 0x00000000u,
                     "圆弧外部空白");
    }
    XImage_deinit_base(&image);

    /* 多边形 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
        "<polygon points=\"1,5 1,1 7,3\" fill=\"#ff8800\"/></svg>", &image),
        "SVG 多边形解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 4, 3) == 0xffff8800u,
                     "多边形宽行覆盖");
    }
    XImage_deinit_base(&image);

    /* 折线 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<polyline points=\"1,1 5,1 9,9\" fill=\"none\" stroke=\"#ff0000\" "
        "stroke-width=\"2\"/></svg>", &image), "SVG 折线解码成功");
    if (!XImage_isNull(&image)) {
        int red = 0;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                if (XImage_pixel(&image, x, y) == 0xffff0000u) ++red;
        codec_expect(red > 4, "折线描边像素存在");
    }
    XImage_deinit_base(&image);

    /* 文字（内置 5x7 点阵） */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<text x=\"2\" y=\"8\" font-size=\"10\" fill=\"#000000\">A</text></svg>",
        &image), "SVG 文本解码成功");
    if (!XImage_isNull(&image)) {
        int black = 0;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 20; ++x)
                if (XImage_pixel(&image, x, y) == 0xff000000u) ++black;
        codec_expect(black > 4, "文本点阵像素存在");
    }
    XImage_deinit_base(&image);

    /* viewBox 坐标系 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
        "<rect width=\"5\" height=\"10\" fill=\"#123456\"/></svg>", &image),
        "SVG viewBox 解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_width(&image) == 10 && XImage_height(&image) == 10 &&
                     XImage_pixel(&image, 0, 9) == 0xff123456u &&
                     XImage_pixel(&image, 4, 0) == 0xff123456u,
                     "viewBox 尺寸与内容");
    }
    XImage_deinit_base(&image);

    /* group + transform */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
        "<g transform=\"translate(2,2)\">"
        "<rect width=\"4\" height=\"4\" fill=\"#123456\"/></g></svg>", &image),
        "SVG group+transform 解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 2, 2) == 0xff123456u &&
                     XImage_pixel(&image, 5, 5) == 0xff123456u &&
                     XImage_pixel(&image, 0, 0) == 0x00000000u,
                     "group transform 平移生效");
    }
    XImage_deinit_base(&image);

    /* 描边：不填充矩形边框 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">"
        "<rect x=\"1\" y=\"1\" width=\"8\" height=\"8\" fill=\"none\" "
        "stroke=\"#ff0000\" stroke-width=\"3\"/></svg>", &image),
        "SVG 描边矩形解码成功");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 2, 2) == 0xffff0000u &&
                     XImage_pixel(&image, 7, 7) == 0xffff0000u &&
                     XImage_pixel(&image, 5, 5) == 0x00000000u,
                     "描边边框颜色与内部空白");
    }
    XImage_deinit_base(&image);

    /* 透明度 */
    XImage_init(&image);
    codec_expect(svgVectorDecode(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"4\" height=\"4\">"
        "<rect width=\"4\" height=\"4\" fill=\"#ff0000\" opacity=\"0.5\"/></svg>",
        &image), "SVG 透明度解码成功");
    if (!XImage_isNull(&image)) {
        v = XImage_pixel(&image, 2, 2);
        codec_expect((v >> 24) == 0x80u && (v & 0x00ff0000u) != 0,
                     "opacity=0.5 得到 0x80 Alpha");
    }
    XImage_deinit_base(&image);

    XPrintf("\n");
}
#endif /* XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON */

/* ================== GIF 动画（XIMAGECODEC_GIF_ANIM_ON） ================== */

#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
/**
 * @brief      测试 GIF 多帧动画解码：帧数/循环/延迟/处置方式/透明色
 */
static void XImageCodecGifAnimationTest(void)
{
    XImageCodecAnimation* anim;
    XImage image;
    XByteArray* bytes;
    XPrintf("===== GIF 动画解码测试 =====\n");

    anim = XImageCodec_decodeAnimation(kCodecGifAnimFixture,
                                       kCodecGifAnimFixtureSize);
    codec_expect(anim != NULL, "GIF 动画解码返回对象");
    if (anim) {
        codec_expect(anim->frameCount == 4, "动画帧数为 4");
        codec_expect(anim->loopCount == -1, "Netscape 无限循环");
        codec_expect(anim->frames[1].delayMs == 200, "第 2 帧延迟 200ms");
        codec_expect(anim->frames[0].disposal == XImageCodecFrameDisposal_Keep &&
                     anim->frames[2].disposal ==
                         XImageCodecFrameDisposal_RestoreBackground,
                     "帧处置方式解析正确");
        codec_expect(XImage_width(&anim->frames[0].image) == 4 &&
                     XImage_height(&anim->frames[0].image) == 2,
                     "帧尺寸为逻辑屏幕 4x2");
        codec_expect(XImage_pixel(&anim->frames[0].image, 0, 0) == 0xffffffffu &&
                     XImage_pixel(&anim->frames[0].image, 2, 0) == 0xff000000u,
                     "首帧像素");
        codec_expect(XImage_pixel(&anim->frames[1].image, 2, 0) == 0xffffffffu &&
                     XImage_pixel(&anim->frames[1].image, 3, 0) == 0xff000000u,
                     "第 2 帧透明色保留底图");
        codec_expect(XImage_pixel(&anim->frames[2].image, 3, 0) == 0xffffffffu,
                     "第 3 帧绘制白色");
        codec_expect(XImage_pixel(&anim->frames[3].image, 3, 0) == 0x00000000u &&
                     XImage_pixel(&anim->frames[3].image, 0, 0) == 0xffffffffu,
                     "第 4 帧恢复背景且旧帧保留");
        XImageCodecAnimation_delete(anim);
    }

    /* 多帧 GIF 的单帧解码仍可用 */
    XImage_init(&image);
    codec_expect(XImageCodec_decode(kCodecGifAnimFixture,
                                    kCodecGifAnimFixtureSize,
                                    XImageCodecFormat_Gif, &image),
                 "多帧 GIF 单帧解码");
    if (!XImage_isNull(&image)) {
        codec_expect(XImage_pixel(&image, 0, 0) == 0xffffffffu,
                     "单帧解码取首帧内容");
    }
    XImage_deinit_base(&image);

    /* 动画解码后 GIF 编码往返不受影响 */
    bytes = XByteArray_create();
    XImage_init_ex(&image, 2, 2, XImageFormat_ARGB32);
    XImage_fill(&image, 0xff336699u);
    codec_expect(bytes && XImageCodec_encode(&image, XImageCodecFormat_Gif,
                                             -1, bytes) &&
                 XImageCodec_decode(XByteArray_data(bytes),
                                    XByteArray_size_base(
                                        (const XContainer*)bytes),
                                    XImageCodecFormat_Gif, &image),
                 "GIF 编码往返在动画 API 之后正常");
    XImage_deinit_base(&image);
    if (bytes) XByteArray_delete_base((XClass*)bytes);

    XPrintf("\n");
}
#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */


/**
 * @brief      测试非法/截断数据被拒绝
 */
static void XImageCodecMalformedTest(void)
{
    static const uint8_t empty[] = {0};
    static const uint8_t truncatedPng[16] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    static const uint8_t badBmp[54] = {'B', 'M'};
    static const uint8_t truncatedJpeg[4] = {0xff, 0xd8, 0xff, 0xe0};
    XImage image;
    XPrintf("===== 非法数据拒绝测试 =====\n");
    XImage_init(&image);
    codec_expect(!XImageCodec_decode(NULL, 0, XImageCodecFormat_Bmp, &image),
                 "空指针解码被拒绝");
    codec_expect(!XImageCodec_decode(empty, 0, XImageCodecFormat_Png, &image),
                 "空数据解码被拒绝");
    codec_expect(!XImageCodec_decode(truncatedPng, sizeof(truncatedPng),
                                     XImageCodecFormat_Png, &image),
                 "截断 PNG 被拒绝");
    codec_expect(!XImageCodec_decode(badBmp, sizeof(badBmp),
                                     XImageCodecFormat_Bmp, &image),
                 "损坏 BMP 被拒绝");
    codec_expect(!XImageCodec_decode(truncatedJpeg, sizeof(truncatedJpeg),
                                     XImageCodecFormat_Jpeg, &image),
                 "截断 JPEG 被拒绝");
    codec_expect(!XImageCodec_encode(NULL, XImageCodecFormat_Png, -1, NULL),
                 "空参数编码被拒绝");
    XImage_deinit_base(&image);
    XPrintf("\n");
}

/**
 * @brief      测试上层图像类统一走 XImageCodec 公共 API
 */
static void XImageCodecUpperLayerTest(void)
{
    XImage source, loaded;
    XByteArray* bytes = NULL;
    XImageCodecFormat format;
    XImageReader reader;
    XImageWriter writer;
    XStringList* formats = NULL;
    XFile file;
    XString* fileName = NULL;
    XPrintf("===== 上层图像类集成测试 =====\n");

    XImage_init_ex(&source, 3, 2, XImageFormat_ARGB32);
    XImage_fill(&source, 0xff224466u);
    bytes = XByteArray_create();
    codec_expect(bytes && XImageCodec_encode(&source, XImageCodecFormat_Png,
                                             -1, bytes),
                 "XImageCodec 编码 PNG 供上层使用");
    format = XImageCodec_detect(XByteArray_data(bytes),
                                XByteArray_size_base((const XContainer*)bytes));
    codec_expect(format == XImageCodecFormat_Png, "上层识别格式");

    /* XImage 直接内存加载/保存走 codec */
    XImage_init(&loaded);
    codec_expect(XImage_loadFromData_2(&loaded, XByteArray_data(bytes),
                    (int)XByteArray_size_base((const XContainer*)bytes), "png"),
                 "XImage_loadFromData 走 XImageCodec");
    codec_expect(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2 &&
                 XImage_pixel(&loaded, 1, 1) == 0xff224466u,
                 "XImage 加载结果正确");
    XImage_deinit_base(&loaded);

    /* XImageReader 能力查询与解码走 codec（设备工作流） */
    formats = XImageReader_supportedImageFormats();
    codec_expect(formats &&
                 XStringList_contains_utf8(formats, "png", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "bmp", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "gif", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "svg", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "jpeg", XChar_CaseSensitive),
                 "XImageReader 能力查询与 XImageCodec 对齐");
    if (formats) XStringList_delete_base((XClass*)formats);

    fileName = XString_create_utf8("xgui_codec_menu.png");
    XFile_init_2(&file, fileName);
    if (bytes && XFile_open_2(&file, XIODevice_WriteOnly, 0)) {
        XImageWriter_init_device_2(&writer, (XIODevice*)&file, "png");
        codec_expect(XImageWriter_canWrite(&writer),
                     "XImageWriter 报告 PNG 可写");
        codec_expect(XImageWriter_write(&writer, &source),
                     "XImageWriter 写 PNG 走 XImageCodec");
        XImageWriter_deinit_base(&writer);
        XIODevice_close_base((XIODevice*)&file);
    }
    XClass_deinit_base((XClass*)&file);
    XFile_remove_static(fileName);
    XString_delete_base((XClass*)fileName);

    fileName = XString_create_utf8("xgui_codec_menu.png");
    XFile_init_2(&file, fileName);
    if (XFile_open_2(&file, XIODevice_ReadOnly, 0)) {
        XImageReader_init_device_2(&reader, (XIODevice*)&file, "png");
        codec_expect(XImageReader_canRead(&reader),
                     "XImageReader 识别 PNG 文件");
        XImage_init(&loaded);
        codec_expect(XImageReader_read(&reader, &loaded),
                     "XImageReader 读 PNG 走 XImageCodec");
        codec_expect(XImage_width(&loaded) == 3 && XImage_height(&loaded) == 2 &&
                     XImage_pixel(&loaded, 1, 1) == 0xff224466u,
                     "XImageReader 读取结果正确");
        XImage_deinit_base(&loaded);
        XImageReader_deinit_base(&reader);
        XIODevice_close_base((XIODevice*)&file);
    }
    XClass_deinit_base((XClass*)&file);
    XFile_remove_static(fileName);
    XString_delete_base((XClass*)fileName);

    /* XImageWriter 静态能力查询与 XImageCodec 对齐（JPEG 已有后端应可写） */
    formats = XImageWriter_supportedImageFormats();
    codec_expect(formats &&
                 XStringList_contains_utf8(formats, "png", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "svg", XChar_CaseSensitive) &&
                 XStringList_contains_utf8(formats, "jpeg", XChar_CaseSensitive),
                 "XImageWriter 能力查询与 XImageCodec 对齐");
    if (formats) XStringList_delete_base((XClass*)formats);

    if (bytes) XByteArray_delete_base((XClass*)bytes);
    XImage_deinit_base(&source);
    XPrintf("\n");
}

/**
 * @brief      XImageCodec 综合测试入口
 */
void XMenu_XImageCodecTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XImageCodec 编解码器");
    XMenu_addMenu(root, menu);
    XAction* action;
    action = XMenu_addAction(menu, "格式注册与能力查询");
    XAction_setAction(action, XImageCodecRegistryTest);

    action = XMenu_addAction(menu, "文件头识别");
    XAction_setAction(action, XImageCodecDetectTest);

    action = XMenu_addAction(menu, "各格式编解码往返");
    XAction_setAction(action, XImageCodecRoundTripTest);

    action = XMenu_addAction(menu, "SVG 纯色矩形解码");
    XAction_setAction(action, XImageCodecSvgSolidTest);

#if XIMAGECODEC_SVG_ON && XIMAGECODEC_SVG_VECTOR_ON
    action = XMenu_addAction(menu, "SVG 矢量渲染");
    XAction_setAction(action, XImageCodecSvgVectorTest);
#endif

#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON
    action = XMenu_addAction(menu, "GIF 动画解码");
    XAction_setAction(action, XImageCodecGifAnimationTest);
#endif

    action = XMenu_addAction(menu, "非法数据拒绝");
    XAction_setAction(action, XImageCodecMalformedTest);

    action = XMenu_addAction(menu, "上层图像类集成");
    XAction_setAction(action, XImageCodecUpperLayerTest);
}
#endif // DEMOTEST
