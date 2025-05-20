#ifndef XSERIALPORT_H
#define XSERIALPORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODevice.h"
/*! \brief 串口传输校验位类型 */
typedef enum
{
    SP_PAR_NONE,                /*!< 无校验 */
    SP_PAR_ODD,                 /*!< 奇校验 */
    SP_PAR_EVEN                 /*!< 偶校验 */
} XSerialPortParity;
//pwm设备初始化接口
typedef struct XSerialPortDevice_PortFuncInit
{
    XIODevice_PortFuncInit parentPort;//父对象接口
}XSerialPortDevice_PortFuncInit;
//串口设备
typedef struct XSerialPortDevice
{
    XIODevice m_parent;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortParity m_parity;//校验
}XSerialPortDevice;//串口
XSerialPortDevice* XSerialPort_new(XSerialPortDevice_PortFuncInit* port);
void XSerialPort_init(XSerialPortDevice* serial, XSerialPortDevice_PortFuncInit* port);
bool XSerialPort_open(XSerialPortDevice* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
#ifdef __cplusplus
}
#endif
#endif