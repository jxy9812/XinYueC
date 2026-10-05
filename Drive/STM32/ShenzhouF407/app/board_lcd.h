/* ==================== 神舟号 3.5 寸 TFT LCD（FSMC 16 位 8080） ====================
 * 面板：NT35310 / ILI9488 双变体（模块内 FPC 二选一，运行时读 ID 自适应），
 * 横屏 480x320 RGB565；NE4 片选（0x6C00_0000 段）、A6 命令/数据选择
 * （CMD=0x6C00_007E / DATA=0x6C00_0080）、背光 PB15。
 * 初始化序列提取自神舟出厂测试例程 HARDWARE/LCD/lcd.c（见 lcd_init_tables.inc）。
 */
#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 面板几何（横屏定版；与 XINYUE_PANEL_WIDTH/HEIGHT 全局定义保持一致）。 */
#define BOARD_LCD_WIDTH  320
#define BOARD_LCD_HEIGHT 480

/* 屏控制器 ID（0 = 未识别）。 */
uint16_t board_lcd_id(void);

/* 上电时序 + FSMC 配置 + ID 探测 + 驱动初始化 + 横屏窗口钳位。
 * 背光保持熄灭，由 board_lcd_backlight 打开。 */
void board_lcd_init(void);

void board_lcd_backlight(int on);

/* 整块填充（面板坐标系）。 */
void board_lcd_fill(int x, int y, int w, int h, uint16_t color);

/* RGB565 块搬运：src 每行 srcStrideBytes 字节，逐行写入面板 (x,y,w,h)。
 * 行内顺序写 GRAM，无读回。present 回调的热路径。 */
void board_lcd_blit(int x, int y, int w, int h,
                    const uint8_t* src, int srcStrideBytes);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LCD_H */
