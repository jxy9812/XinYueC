/* ==================== 神舟号 外扩 SRAM（IS62WV51216，FSMC NE3） ====================
 * 512K×16bit = 1MB，挂 FSMC Bank1 NORSRAM3（NE3=PG10，片选 3）。
 * 地址/数据总线与 TFTLCD 共享（A6 例外：SRAM 侧为地址线 A6，LCD 侧作 RS）。
 * 初始化后经读写图案测试校验，成功方可供 board_mem 建池。
 */
#ifndef BOARD_SRAM_H
#define BOARD_SRAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_SRAM_BASE 0x68000000UL
#define BOARD_SRAM_SIZE (1024UL * 1024UL)

/* FSMC NE3 + 引脚复用配置；返回 true = 总线就绪（未做内容校验）。 */
bool board_sram_init(void);

/* 写读校验（0xAA/0x55/地址噪声三组图案，各 4KB 采样）。
 * 返回可用容量（字节）；失败返回 0。 */
size_t board_sram_verify(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_SRAM_H */
