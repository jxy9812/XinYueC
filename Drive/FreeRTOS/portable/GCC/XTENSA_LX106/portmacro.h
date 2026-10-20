/*
 * FreeRTOS V9.0.0 移植层类型/宏 —— ESP8266 LX106 (call0 ABI, NONOS3 底座)
 *
 * 本工具链默认 call0 ABI（abi_probe 实测：全程 call0，无窗口旋转），
 * 上下文切换走自研 L1 向量 + CCOMPARE0 tick，见 portasm.S/port.c。
 * 沿用原 V7.5.2 xtensa 端口的 V9 typedef 补丁（TickType_t 等）。
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "c_types.h"
/* 注意：不要在这里 include ets_sys.h —— NONOS3 的它无条件声明 3 参
 * pvPortMalloc/vPortFree，会与 FreeRTOS portable.h 的 1 参声明冲突。
 * 需要 SDK 头的文件（如 app.c）自行用重命名宏隔离。 */

#include <xtensa/xtruntime.h>
#include <xtensa/tie/xt_interrupt.h>
#include "xtensa_rtos.h"

/* ---------------------------------------------------------------------
 * V9 内核所需基础类型（V9 把它们放在 portmacro.h，V7 时在 FreeRTOS.h）
 * TickType_t 32 位：80MHz + 1000Hz tick，必须 32 位。
 * ------------------------------------------------------------------- */
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
typedef unsigned long StackType_t;

/* 旧代码兼容别名 */
#define portCHAR		char
#define portFLOAT		float
#define portDOUBLE		double
#define portLONG		long
#define portSHORT		short
#define portSTACK_TYPE	unsigned portLONG
#define portBASE_TYPE	long

typedef unsigned portLONG portTickType;
typedef unsigned int INT32U;
#define portMAX_DELAY ( portTickType ) 0xffffffff
/*-----------------------------------------------------------*/

/* Architecture specifics. */
#define portSTACK_GROWTH			( -1 )
/* portTICK_RATE_MS 由 FreeRTOS.h 提供默认定义，此处不再重复 */
#define portBYTE_ALIGNMENT			8

/* 单核；无 MPU。
 * 注意：绝不能在这里定义 portUSING_MPU_WRAPPERS（哪怕置 0）——
 * V9 的 mpu_wrappers.h 用 #ifdef（存在性）判断，一旦定义就把全部 API
 * 映射到 MPU_ 前缀版本。不定义时由 mpu_wrappers.h 自行置 0。 */
#define portNUM_CONFIGURABLE_REGIONS		1
#define portHAS_STACK_OVERFLOW_CHECKING		0

/* 32 位 tick 天然原子 */
#define portTICK_TYPE_IS_ATOMIC		1
/*-----------------------------------------------------------*/

/* Yield：置 soft 中断（inum 7）位，L1 向量接管做完整上下文切换 */
extern void vPortYield( void );
#define portYIELD()				vPortYield()
#define portYIELD_WITHIN_API()			vPortYIELD()
/* 仓库定制 tasks.c 直接调用 vPortYIELD()（大写）——映射到本端口实现 */
#define vPortYIELD()				vPortYield()

/* Tick 用 rsil 关中断（level 3 = EXCM_LEVEL，NMI 不可屏蔽除外） */
#define portDISABLE_INTERRUPTS() \
	__asm__ volatile ("rsil a2, 3" ::: "a2", "memory")
#define portENABLE_INTERRUPTS()  \
	__asm__ volatile ("wsr a2, ps" ::: "memory")  /* 仅 vPortExitCritical 内部用 */

/* ISR 内临界区 */
#define portSET_INTERRUPT_MASK_FROM_ISR()	uxPortSetInterruptMaskFromISR()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)	vPortClearInterruptMaskFromISR(x)
UBaseType_t uxPortSetInterruptMaskFromISR( void );
void vPortClearInterruptMaskFromISR( UBaseType_t uxSaved );

/* 任务级临界区 */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
#define portENTER_CRITICAL()                vPortEnterCritical()
#define portEXIT_CRITICAL()                 vPortExitCritical()

#define portEND_SWITCHING_ISR( xSwitchRequired ) \
	if( xSwitchRequired ) vTaskSwitchContext()

/*-----------------------------------------------------------*/

/* Task function macros */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) void vFunction( void *pvParameters )
/*-----------------------------------------------------------*/

/* 本移植层提供的少量 ISR 表（见 port.c） */
typedef void (* _xt_isr)(void *arg);
void _xt_isr_attach( unsigned i, _xt_isr func, void *arg );

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
