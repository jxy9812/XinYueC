#ifdef USE_STDPERIPH_DRIVER
#ifndef XSerialPortSTM32_H
#define XSerialPortSTM32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XSerialPortBase.h"
#ifdef USE_STDPERIPH_DRIVER
typedef struct XUsartGPIO
{
    uint8_t  GPIO_PinSourceX;
    uint16_t GPIO_Pin_X;
    uint32_t GPIOX;
    uint32_t GPIO_Clock;//时钟
}XUsartGPIO;
#endif

//stm32串口设备
typedef struct XSerialPortSTM32
{
    XSerialPortBase m_parent;//父对象
#ifdef USE_STDPERIPH_DRIVER
    uint32_t  USARTX;
    XUsartGPIO TX;
    XUsartGPIO RX;
#endif
}XSerialPortSTM32;//串口
    XVtable* XSerialPortSTM32_class_init();
#ifdef USE_STDPERIPH_DRIVER
    XSerialPortSTM32* XSerialPortSTM32StdPeriph_create(XUsartGPIO* TX, XUsartGPIO* RX);
#endif
    void XSerialPortSTM32_init(XSerialPortSTM32* serial);
    XSerialPortSTM32* XSerialPortSTM32_global(uint8_t port);
#ifdef __cplusplus
}
#endif
#endif
#endif // Win32