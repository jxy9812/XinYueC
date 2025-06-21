#ifndef XMODBUSBASE_H
#define XMODBUSBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include"XModbusEnum.h"
typedef struct XQueueBase XQueueBase; 
typedef struct XModbusFunctionHandler XModbusFunctionHandler; 
typedef struct XModbusFrame XModbusFrame;
typedef struct XVector XVector;
typedef struct XListBase XListBase;
typedef struct XTimerBase XTimerBase;
#include"XDataFrameComm.h"
//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0
#define XMODBUSBASE_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm)+4)       //XCommunicatorBase虚函数表大小
enum XModbusBaseVtableEnum
{
    EXModbusBase_SendFrame = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
    EXModbusBase_RecvFrame,
};
typedef struct XModbusBase
{
    XDataFrameComm m_parent;
    uint8_t    m_address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode m_mode;//模式
}XModbusBase;
XVtable* XModbusBase_class_init();
void XModbusBase_init(XModbusBase* modbus, XIODeviceBase* io);
void XModbusBase_setAddress(XModbusBase* modbus, uint8_t address);
void XModbusBase_setMode(XModbusBase* modbus, XModbusMode mode);
//当前是主站吗
bool XModbusBase_isMaster(XModbusBase* modbus);
#define XModbusBase_connect_base                        XCommunicatorBase_connect_base
#define XModbusBase_disconnect_base                     XCommunicatorBase_disconnect_base
#define XModbusBase_isConnected_base                    XCommunicatorBase_isConnected_base
//发送一帧数据
#define XModbusBase_sendFrame_base                      XDataFrameComm_sendData_base
//主站定期发送
#define XModbusBase_sendFramePeriodicMaster             XDataFrameComm_addPeriodicSendData_base
//轮询处理
#define XModbusBase_poll_base XCommunicatorBase_poll_base

#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
