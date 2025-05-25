#ifndef XSERIALPORT_H
#define XSERIALPORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODevice.h"
//XSerialPortDevice虚函数表
extern XVtable* XSerialPortVtable;
#define XSERIALPORT_VTABLE_SIZE (XIODEVICE_VTABLE_SIZE)       //XSerialPortDevice容器虚函数表大小
/*! \brief 串口传输校验位类型 */
typedef enum
{
    SP_PAR_NONE,                /*!< 无校验 */
    SP_PAR_ODD,                 /*!< 奇校验 */
    SP_PAR_EVEN                 /*!< 偶校验 */
} XSerialPortParity;
//串口设备初始化接口
typedef struct XSerialPort_PortFuncInit
{
    XIODevice_PortFuncInit parentPort;//父对象接口
}XSerialPort_PortFuncInit;
//串口设备
typedef struct XSerialPort
{
    XIODevice m_parent;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortParity m_parity;//校验
}XSerialPort;//串口
//初始化类
void XSerialPort_class_init();
XSerialPort* XSerialPort_new(XSerialPort_PortFuncInit* port);
void XSerialPort_init(XSerialPort* serial, XSerialPort_PortFuncInit* port);
bool XSerialPort_open(XSerialPort* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
#define XSerialPort_free XIODevice_free;
#define XSerialPort_setWriteBuffer XIODevice_setWriteBuffer;
#define XSerialPort_setReadBuffer XIODevice_setReadBuffer;
#define XSerialPort_setDevice XIODevice_setDevice;
#define XSerialPort_write XIODevice_write;
#define XSerialPort_read XIODevice_read;
#define XSerialPort_receive XIODevice_receive;
#define XSerialPort_isOpen XIODevice_isOpen;
#define XSerialPort_close XIODevice_close;
#define XSerialPort_poll XIODevice_poll;
#define XSerialPort_writeFull XIODevice_writeFull;
#ifdef __cplusplus
}
#endif
#endif