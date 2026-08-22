#ifndef XSERIALPORTSTM32_H
#define XSERIALPORTSTM32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief STM32 标准外设库 GPIO 引脚描述。 */
typedef struct XUsartGPIO
{
    uint8_t m_pinSource;
    uint16_t m_pin;
    uint32_t m_gpio;
    uint32_t m_gpioClock;
} XUsartGPIO;

/**
 * @brief STM32 串口平台配置，作为 XDeviceSerialPortOpenOptions.m_platformData 传入。
 * @details m_usart、m_gpio 使用目标芯片头文件提供的寄存器地址宏转换为整数；
 *          m_portNumber 用于选择 USART1/2/3/6 的复用功能和总线时钟。
 */
typedef struct XDeviceSerialPortStm32Config
{
    uint32_t m_usart;
    uint8_t m_portNumber;
    XUsartGPIO m_tx;
    XUsartGPIO m_rx;
} XDeviceSerialPortStm32Config;

#ifdef __cplusplus
}
#endif

#endif
