/* ==================== 神舟号 W25Q128 SPI Flash（上电识别） ====================
 * SPI1：PB3=SCK / PB4=MISO / PB5=MOSI（AF5），片选 PB14（F_CS）。
 * 本阶段只做初始化 + JEDEC ID 读取上报；文件系统（XDeviceFile/fatfs）
 * 挂载与字体/字库外置属后续接入项。
 */
#ifndef BOARD_W25Q_H
#define BOARD_W25Q_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool board_w25q_init(void);
uint32_t board_w25q_jedec_id(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_W25Q_H */
