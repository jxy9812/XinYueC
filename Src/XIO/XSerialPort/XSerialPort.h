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
//串口设备
typedef struct XSerialPort
{
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortParity m_parity;//校验
}XSerialPort;//串口
XIODevice* XSerialPort_new(XIODevice_PortFunc* port);
bool XSerialPort_open(XIODevice* io, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
#ifdef __cplusplus
}
#endif
#endif