/**
 * @file xdevice_oled.c
 * @brief SSD1306 OLED 外设封装（XGui 绘制 + 增量上屏）
 *
 * @details 职责边界：
 *          - 本文件：XGui 绘制内容（标题栏/框线）、上屏节拍、错误计数导出；
 *          - `XDeviceDisplay_ssd1306_i2c.c`：I2C 位操作时序、init 序列、
 *            帧缓冲、页寻址 flush（oled3 真机验证版，**一行时序都不要改**）。
 *
 *          绘制走「直绘 + blit565」而不是 EXPOSE→PAINT→present 回调链：
 *          本形态下 PARTIAL present 回调 presents=0（脏区为空），直推是
 *          唯一确定性上屏路径（见 PROGRESS §10.2）。
 *
 *          上屏必须由事件循环调用 xdevice_oled_pump() 每拍发一页；
 *          xdevice_oled_draw() 只写帧缓冲并置脏页，**不会**自己发 I2C。
 *          （历史事故：漏调 pump 时屏幕永远停在开机图样，见 PROGRESS §0-Y。）
 */
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XImage.h"
#include "XFont.h"
#include "XPainter.h"

#include "xdevice_oled.h"
#include "xdevice_uart.h"

/* ---- 底层驱动接口（XDeviceDisplay_ssd1306_i2c.c） ---- */
extern int  XDeviceDisplay_ssd1306_i2c_init(void);
extern int  XDeviceDisplay_ssd1306_i2c_probe(void);
extern void XDeviceDisplay_ssd1306_i2c_clear(void);
extern void XDeviceDisplay_ssd1306_i2c_blit565(int x, int y, int w, int h,
                                               const unsigned char* rgb565,
                                               int strideBytes);
extern void XDeviceDisplay_ssd1306_i2c_flush(void);
extern int  XDeviceDisplay_ssd1306_i2c_flush_step(void);
extern unsigned XDeviceDisplay_ssd1306_i2c_ackErrors(void);
extern int  XDeviceDisplay_ssd1306_i2c_earlyInit(void);
extern void XDeviceDisplay_ssd1306_i2c_testPattern(void);

#define GUI_W 128
#define GUI_H 64
/* tile = 整行 128 像素宽（与 10-18 验证画法一致）。
 * 曾为省 1.5KB 改成 32 像素分块 + painter -chunkX 平移，实测字形错位，
 * 已回退：整行 tile 多占 1.5KB .bss，换来确定正确的对齐。 */
#define TILE_W 128
#define TILE_BYTES (TILE_W * 8 * 2)      /* 128*8*2 = 2048B（一个 SSD1306 页高） */

/* 静态 tile 缓冲：draw 期间不碰堆。 */
static unsigned char s_tile[TILE_BYTES] __attribute__((aligned(4)));

/* 标题栏文字与几何（用户裁定：黄字黑底） */
#define TITLE_TEXT      "XinYueC"
#define TITLE_X         36
#define TITLE_BASELINE  13
#define TITLE_BAND_H    16

bool xdevice_oled_early_init(void)
{
    int r = XDeviceDisplay_ssd1306_i2c_earlyInit();
    /* 约定：0 = 成功（nack ? -1 : 0），不要按 true/false 判。 */
    uart0_puts(r == 0 ? "[oled] early init OK\r\n"
                      : "[oled] early init FAIL\r\n");
    XDeviceDisplay_ssd1306_i2c_testPattern();
    uart0_puts("[oled] boot pattern flushed\r\n");
    return r == 0;
}

bool xdevice_oled_init(void)
{
    /* 开机最前端已成功 init 并刷过图样时【不再重发 init 序列】：
     * 序列首字节是 0xAE（关屏），万一这轮 NACK 就把已点亮的屏弄成全黑。 */
    if (XDeviceDisplay_ssd1306_i2c_probe()) {
        uart0_puts("[i2c] already init@boot, skip re-init\r\n");
        return true;
    }
    if (XDeviceDisplay_ssd1306_i2c_init() != 0)
        return false;
    XDeviceDisplay_ssd1306_i2c_clear();
    uart0_puts("[i2c] pg0..pg7 128B clear done\r\n");
    return true;
}

bool xdevice_oled_draw(void)
{
    XImage tile;
    XPainter painter;
    XFont font;
    XRect rect;
    int band;
    bool textOk = true;
    bool rectOk = true;

    XImage_init(&tile);
    XImage_init_ex_2(&tile, TILE_W, 8, XImageFormat_RGB16,
                     TILE_W * 2, s_tile, NULL, NULL);
    if (XImage_isNull(&tile)) {
        uart0_puts("[gui] tile image init FAIL\r\n");
        return false;
    }
    XFont_init(&font);
    XFont_setPixelSize(&font, 16);         /* 内置 8x16 点阵，原生 1:1 */

    for (band = 0; band < GUI_H / 8; ++band) {
        int bandY = band * 8;
        XPainter_init(&painter, NULL);
        if (!XPainter_begin_image(&painter, &tile)) {
            uart0_puts("[gui] begin_image FAIL\r\n");
            XPainter_deinit(&painter);
            textOk = rectOk = false;
            break;
        }
        /* 只按行平移（-bandY）：tile 是整行宽，绘制坐标直接用全屏坐标，
         * 越界部分由 painter 裁剪。 */
        XPainter_translate(&painter, 0.0f, (float)(-bandY));
        XPainter_setFont(&painter, &font);
        /* 整屏黑底：点亮像素在黄区发黄光、蓝区发蓝光。 */
        XRect_init(&rect, 0, bandY, GUI_W, 8);
        XPainter_fillRect(&painter, &rect, 0xff000000u);
        /* 黄区（行 0-15）点亮标题文字 = 黄字黑底。 */
        if (!XPainter_drawText(&painter, TITLE_X, TITLE_BASELINE,
                               TITLE_TEXT, 0xffffffffu))
            textOk = false;
        /* 蓝区：点亮矩形框 + 对角线 = 蓝线（默认画笔是黑，会隐入黑底，
         * 必须显式设成点亮色）。 */
        XPainter_setPen(&painter, 0xffffffffu);
        XRect_init(&rect, 2, 22, 124, 20);
        if (!XPainter_drawRect(&painter, &rect)) rectOk = false;
        if (!XPainter_drawLine(&painter, 3, 23, 124, 40)) rectOk = false;
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        XDeviceDisplay_ssd1306_i2c_blit565(0, bandY, GUI_W, 8, s_tile, GUI_W * 2);
    }
    XClassDeinit(&font);
    XClassDeinit(&tile);
    uart0_puts(textOk ? "[gui] draw text ok\r\n" : "[gui] draw text FAIL\r\n");
    uart0_puts(rectOk ? "[gui] draw rect ok\r\n" : "[gui] draw rect FAIL\r\n");
    /* 不在此处整屏 flush：blit565 已写进 s_fb 并标脏页，
     * 实际发送交给 xdevice_oled_pump() 每拍一页。 */
    uart0_puts("[gui] rendered, pending incremental flush\r\n");
    return textOk && rectOk;
}

int xdevice_oled_pump(void)
{
    return XDeviceDisplay_ssd1306_i2c_flush_step();
}

unsigned xdevice_oled_ack_errors(void)
{
    return XDeviceDisplay_ssd1306_i2c_ackErrors();
}
