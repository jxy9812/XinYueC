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
// 跨平台停止位枚举
typedef enum
{
    SP_ST_One,        // 1位停止位
    SP_ST_OnePointFive,   // 1.5位停止位
    SP_ST_Two,             // 2位停止位
    SP_ST_ZeroPointFive//0.5位停止位
}XSerialPortBaseStopBits;
// 跨平台数据位枚举
typedef enum 
{
    SP_DB_Five = 5,       // 5位数据位
    SP_DB_Six = 6,        // 6位数据位
    SP_DB_Seven = 7,      // 7位数据位
    SP_DB_Eight = 8,       // 8位数据位
    SP_DB_Nine = 9         // 9位数据位
}XSerialPortBaseDataBits;
/*! \brief 串口传输校验位类型 */
typedef enum
{
    SP_PAR_NONE = 0,                /*!< 无校验 */
    SP_PAR_ODD,                 /*!< 奇校验 */
    SP_PAR_EVEN,                 /*!< 偶校验 */
    SP_PAR_Mark,                // 标记校验（始终为1）
    SP_PAR_Space                // 空格校验（始终为0）
} XSerialPortBaseParity;

// 流控制类型枚举
typedef enum 
{
    SP_FC_None,           // 无流控制（默认）
    SP_FC_Hardware,       // 硬件流控制（RTS/CTS）
    SP_FC_Software,       // 软件流控制（XON/XOFF）
    SP_FC_Both            // 同时使用硬件和软件流控制
}XSerialPortBaseFlowControl;
//串口设备抽象类
typedef struct XSerialPortBase
{
    XIODeviceBase m_parent;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortBaseDataBits m_dataBits;//数据位
    XSerialPortBaseStopBits m_stopBits;//停止位
    XSerialPortBaseParity m_parity;//校验
    XSerialPortBaseFlowControl m_flowControl;//流控制
}XSerialPortBase;//串口
//初始化类
XVtable* XSerialPortBase_class_init();
XSerialPortBase* XSerialPortBase_create(XVtable* vtable);
void XSerialPortBase_init(XSerialPortBase* serial, XVtable* vtable);
bool XSerialPortBase_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity);
#define XSerialPortBase_delete_base                                   XIODeviceBase_delete_base
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
#elif defined(USE_STDPERIPH_DRIVER) 
#include"XSerialPortSTM32.h"
#endif

#ifdef __cplusplus
}
#endif
#endif