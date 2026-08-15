#ifdef USE_STDPERIPH_DRIVER
#ifndef XSERIALPORTSTM32_H
#define XSERIALPORTSTM32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XSerialPort.h"
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
typedef struct XSerialPort
{
    XSerialPortBase m_class;//父对象
#ifdef USE_STDPERIPH_DRIVER
    uint32_t  USARTX;
    XUsartGPIO TX;
    XUsartGPIO RX;
#endif
}XSerialPort;//串口
    XVtable* XSerialPort_class_init();
#ifdef USE_STDPERIPH_DRIVER
    XSerialPort* XSerialPort_create_ex(XMemoryType memory,  XUsartGPIO* TX, XUsartGPIO* RX);
#endif
    void XSerialPort_init(XSerialPort* serial);
    XSerialPort* XSerialPort_global(uint8_t port);
#ifdef __cplusplus
}
#endif
#endif

/* XClass create API default-memory wrappers. */
#undef XSerialPort_create
#define XSerialPort_create(...) XSerialPort_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // Win32