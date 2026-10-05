/* ==================== 神舟号 内存布局与 XMemory 接线 ====================
 * 按库作者口径（2026-10-04）：
 * - FreeRTOS 堆（heap_4 的 ucHeap）放外扩 1MB SRAM（.extheap 段 → FSMC），
 *   经 XMemory 的 SYSTEM API（pvPortMalloc/vPortFree）暴露——大缓冲
 *   （GUI tile、事件负载等）落外部 RAM；
 * - 芯片内置 RAM 剩余空间交给 XMultiPool（.intpool 段，分级定长子池），
 *   注册到 XMEMORY_TYPE_MULTIPOOL；XCLASS_DEFAULT_MEMORY_TYPE=该类型，
 *   控件树/字符串等热小对象走片内高速 RAM；
 * - MULTIPOOL 分配耗尽时回落 pvPortMalloc（外部），free 按地址区间分派。
 */
#ifndef BOARD_MEM_H
#define BOARD_MEM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 构建片内 XMultiPool 并注册进 XMemory（须在首个 XClass 分配前调用）。 */
void board_mem_init(void);

/* 片内池自检统计（供串口打印）。 */
size_t board_mem_intpool_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_MEM_H */
