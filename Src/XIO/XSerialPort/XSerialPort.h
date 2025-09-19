#ifndef XSERIALPORT_H
#define XSERIALPORT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XIODeviceBase.h"
//XSerialPortDevice虚函数表
#define XSERIALPORT_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XIODeviceBase))       //XSerialPort容器虚函数表大小
// 跨平台停止位枚举
typedef enum
{
    SP_ST_One,        // 1位停止位
    SP_ST_OnePointFive,   // 1.5位停止位
    SP_ST_Two,             // 2位停止位
    SP_ST_ZeroPointFive//0.5位停止位
}XSerialPortStopBits;
// 跨平台数据位枚举
typedef enum 
{
    SP_DB_Five = 5,       // 5位数据位
    SP_DB_Six = 6,        // 6位数据位
    SP_DB_Seven = 7,      // 7位数据位
    SP_DB_Eight = 8,       // 8位数据位
    SP_DB_Nine = 9         // 9位数据位
}XSerialPortDataBits;
/*! \brief 串口传输校验位类型 */
typedef enum
{
    SP_PAR_NONE = 0,                /*!< 无校验 */
    SP_PAR_ODD,                 /*!< 奇校验 */
    SP_PAR_EVEN,                 /*!< 偶校验 */
    SP_PAR_Mark,                // 标记校验（始终为1）
    SP_PAR_Space                // 空格校验（始终为0）
} XSerialPortParity;

// 流控制类型枚举
typedef enum 
{
    SP_FC_None,           // 无流控制（默认）
    SP_FC_Hardware,       // 硬件流控制（RTS/CTS）
    SP_FC_Software,       // 软件流控制（XON/XOFF）
    SP_FC_Both            // 同时使用硬件和软件流控制
}XSerialPortFlowControl;
//串口设备抽象类
typedef struct XSerialPortBase
{
    XIODeviceBase m_class;//父对象
    uint8_t m_portNum;//端口号
    uint32_t m_baudRate;//波特率
    XSerialPortDataBits m_dataBits;//数据位
    XSerialPortStopBits m_stopBits;//停止位
    XSerialPortParity m_parity;//校验
    XSerialPortFlowControl m_flowControl;//流控制
}XSerialPortBase;//串口
void XSerialPortBase_init(XSerialPortBase* serial);

/*以下是API*/
bool XSerialPort_open_base(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
#define XSerialPort_delete_base                                   XIODeviceBase_delete_base
#define XSerialPort_setWriteBuffer_base                           XIODeviceBase_setWriteBuffer_base
#define XSerialPort_setReadBuffer_base                            XIODeviceBase_setReadBuffer_base
#define XSerialPort_setDevice_base                                XIODeviceBase_setDevice_base
#define XSerialPort_write_base                                    XIODeviceBase_write_base
#define XSerialPort_read_base                                     XIODeviceBase_read_base
#define XSerialPort_getBytesAvailable_base                        XIODeviceBase_getBytesAvailable_base
#define XSerialPort_getBytesToWrite_base                          XIODeviceBase_getBytesToWrite_base
#define XSerialPort_isOpen_base                                   XIODeviceBase_isOpen_base
#define XSerialPort_close_base                                    XIODeviceBase_close_base
#define XSerialPort_poll_base                                     XIODeviceBase_poll_base
#define XSerialPort_writeFull_base                                XIODeviceBase_writeFull_base

//以下是平台的具体实现
#ifdef WIN32
#include"XSerialPortWin32.h"
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include "XSerialPortPosix.h"  // Linux/BSD平台套接字实现
#elif defined(USE_STDPERIPH_DRIVER) 
#include"XSerialPortSTM32.h"
#endif

#ifdef __cplusplus
}
#endif
#endif