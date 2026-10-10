/** @brief 粗体真字体 A/B 离屏验证（2026-10-08）。
 *  Normal vs Bold 字重同句渲染 PPM，验证 XFontFt 字重变体槽
 *  （XFontFt_ensureFamilyWeighted）命中 <family>-Bold 文件。仅桌面。
 *  输出 /tmp/hint12.ppm（上=Normal 下=Bold）。 */
#include "XGuiConfig.h"
#include "XObject.h"
#include "XPainter.h"
#include "XFont.h"
#include "XImage.h"
#include <stdio.h>

/** @brief 与巡检工具 scanRender 同链：fillRect→begin→setFont→drawText
 *  →end（begin/end 每次重建绑定，两档字体各走一遍）。 */
static int scanLikeRender(XPainter* painter, XImage* img, XFont* font,
                          int x, int y, const char* utf8)
{
    XImage_fillRect(img, NULL, 0xFFFFFFFFu);
    if (!XPainter_begin_image(painter, img)) return 0;
    XPainter_setFont(painter, font);
    if (!XPainter_drawText(painter, x, y, utf8, 0xFF000000u)) return 0;
    if (!XPainter_end(painter)) return 0;
    return 1;
}

int main(void)
{
    XImage img;
    XPainter painter;
    XFont normal;
    XFont bold;
    FILE* f;
    int x;
    int y;
    XImage_init_ex(&img, 1100, 200, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);
    XFont_init(&normal);
    XFont_setFamily(&normal, "XFontOutlineCommon");
    XFont_setPixelSize(&normal, 12);
    XFont_init(&bold);
    XFont_setFamily(&bold, "XFontOutlineCommon");
    XFont_setPixelSize(&bold, 12);
    XFont_setWeight(&bold, XFont_Bold);
    /* 一次 begin 画两行（fillRect 仅开头一次，Normal/Bold 同画布）：
       两行分别 setFont+drawText，避免第二次 fillRect 洗掉第一行。 */
    if (!XPainter_begin_image(&painter, &img))
        return 1;
    XPainter_setFont(&painter, &normal);
    XPainter_drawText(&painter, 20, 60, "改动即时生效；选预设位置后窗口缩放不再自动贴右下角 即", 0xFF000000u);
    XPainter_setFont(&painter, &bold);
    XPainter_drawText(&painter, 20, 150, "改动即时生效；选预设位置后窗口缩放不再自动贴右下角 即", 0xFF000000u);
    if (!XPainter_end(&painter))
        return 1;
    f = fopen("/tmp/hint12.ppm", "wb");
    if (!f) return 2;
    fprintf(f, "P6\n1100 200\n255\n");
    for (y = 0; y < 200; ++y)
        for (x = 0; x < 720; ++x)
        {
            const unsigned char* px =
                (const unsigned char*)XImage_bits(&img) + (y * 1100 + x) * 4;
            fwrite(px + 1, 1, 3, f);
        }
    fclose(f);
    printf("bold_ab: written /tmp/hint12.ppm\n");
    return 0;
}
