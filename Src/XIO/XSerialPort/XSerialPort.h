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
    XIODeviceBase m_parent;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortParity m_parity;//校验
}XSerialPort;//串口
//初始化类
void XSerialPort_class_init();
XSerialPort* XSerialPort_new(XSerialPort_PortFuncInit* port);
void XSerialPort_init(XSerialPort* serial, XSerialPort_PortFuncInit* port);
bool XSerialPort_open_base(XSerialPort* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
#define XSerialPort_free_base XIODevice_free_base
#define XSerialPort_setWriteBuffer_base XIODevice_setWriteBuffer_base
#define XSerialPort_setReadBuffer_base XIODevice_setReadBuffer_base
#define XSerialPort_setDevice_base XIODevice_setDevice_base
#define XSerialPort_write_base XIODevice_write_base
#define XSerialPort_read_base XIODevice_read_base
#define XSerialPort_receive_base XIODevice_receive_base
#define XSerialPort_isOpen_base XIODevice_isOpen_base
#define XSerialPort_close_base XIODevice_close_base
#define XSerialPort_poll_base XIODevice_poll_base
#define XSerialPort_writeFull_base XIODevice_writeFull_base
#ifdef __cplusplus
}
#endif
#endif