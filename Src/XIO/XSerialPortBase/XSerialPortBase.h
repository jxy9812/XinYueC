#ifndef XSERIALPORTBASE_H
#define XSERIALPORTBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODeviceBase.h"
//XSerialPortDevice虚函数表
extern XVtable* XSerialPortVtable;
#define XSERIALPORT_VTABLE_SIZE (XIODEVICEBASE_VTABLE_SIZE)       //XSerialPortDevice容器虚函数表大小
/*! \brief 串口传输校验位类型 */
typedef enum
{
    SP_PAR_NONE,                /*!< 无校验 */
    SP_PAR_ODD,                 /*!< 奇校验 */
    SP_PAR_EVEN                 /*!< 偶校验 */
} XSerialPortBaseParity;
//串口设备抽象类
typedef struct XSerialPortBase
{
    XIODeviceBase m_parent;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortBaseParity m_parity;//校验
}XSerialPortBase;//串口
//初始化类
XVtable* XSerialPortBase_class_init();
XSerialPortBase* XSerialPortBase_new(XVtable* vtable);
void XSerialPortBase_init(XSerialPortBase* serial, XVtable* vtable);
bool XSerialPortBase_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity);
#define XSerialPortBase_free_base                                   XIODeviceBase_free_base
#define XSerialPortBase_setWriteBuffer_base                         XIODeviceBase_setWriteBuffer_base
#define XSerialPortBase_setReadBuffer_base                          XIODeviceBase_setReadBuffer_base
#define XSerialPortBase_setDevice_base                              XIODeviceBase_setDevice_base
#define XSerialPortBase_write_base                                  XIODeviceBase_write_base
#define XSerialPortBase_read_base                                   XIODeviceBase_read_base
#define XSerialPortBase_getBytesAvailable_base                      XIODeviceBase_getBytesAvailable_base
#define XSerialPortBase_getBytesToWrite_base                        XIODeviceBase_getBytesToWrite_base
#define XSerialPortBase_isOpen                                      XIODeviceBase_isOpen
#define XSerialPortBase_close_base                                  XIODeviceBase_close_base
#define XSerialPortBase_poll_base                                   XIODeviceBase_poll_base
#define XSerialPortBase_writeFull_base                              XIODeviceBase_writeFull_base

//以下是平台的具体实现
#ifdef WIN32
#include"XSerialPortWin32.h"
#endif
#ifdef __cplusplus
}
#endif
#endif