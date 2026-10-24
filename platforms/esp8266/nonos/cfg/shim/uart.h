/* ============================================================================
 * uart.h —— 裸机 UART 类型定义补充头（**不是**完整 SDK uart.h）
 * ============================================================================
 * 背景：`Drive/Espressif/ESP8266/Device/XSerialPortESP8266.c` 参考完整的
 * NONOS SDK（driver_lib/include/uart.h）编写，需要下面这些枚举/类型。
 * 但本机迁移的 Arduino SDK 与 nonos_v305 包里**都没有** uart.h
 * （`C:/esp/arduino8266/tools/sdk/include/uart.h` 不存在），
 * 于是用本文件补上类型声明。
 *
 * 注意：驱动里 UART_SetBaudrate 等函数是**寄存器直写自实现**的（注释说明
 * SDK 预编译库里 UART_* 符号为 0，链接会 undefined），所以这里只需要类型，
 * 不需要函数原型；UART0 常量供 REG_UART_BASE(UART0) 使用。
 *
 * ⚠ 如果将来换回带完整 uart.h 的 SDK，本文件需要删除，否则重复定义。
 * ============================================================================ */
#ifndef XESP8266_BAREMETAL_UART_SHIM_H
#define XESP8266_BAREMETAL_UART_SHIM_H

/* UART 端口号（SDK uart.h 的原值） */
typedef enum {
    UART0 = 0,
    UART1 = 1
} UART_Port;

/* 数据位宽（SDK uart.h 的原始枚举名） */
typedef enum {
    UART_WordLength_5b = 0,
    UART_WordLength_6b = 1,
    UART_WordLength_7b = 2,
    UART_WordLength_8b = 3
} UART_WordLength;

/* 停止位（SDK uart.h 的原始枚举名；值对应 CONF0[14:13] 编码：
 * 1 位 -> 0、1.5 位 -> 1、2 位 -> 2，与驱动 UART_SetStopBits 的注释一致） */
typedef enum {
    USART_StopBits_1    = 0,
    USART_StopBits_1_5  = 1,
    USART_StopBits_2    = 2
} UART_StopBits;

/* 校验模式 */
typedef enum {
    USART_Parity_None = 0,
    USART_Parity_Even = 1,
    USART_Parity_Odd  = 2
} UART_ParityMode;

#endif /* XESP8266_BAREMETAL_UART_SHIM_H */
