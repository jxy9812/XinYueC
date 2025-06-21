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
#define XMODBUS_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm)+4)       //XCommunicatorBase虚函数表大小
enum XModbusVtableEnum
{
    EXModbus_SendFrame = XCLASS_VTABLE_GET_SIZE(XCommunicatorBase),
    EXModbus_RecvFrame,
};

typedef struct XModbus
{
    XDataFrameComm m_parent;
    uint8_t    m_address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode m_mode;//模式
    XModbusRecvHandMode m_recvHandMode;//接收处理模式
}XModbus;
XVtable* XModbus_class_init();
//基础创建
XModbus* XModbus_create(XIODeviceBase* io);
//用串口创建RTU 
XModbus* XModbus_create_RTU_SerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired);
void XModbus_init(XModbus* modbus, XIODeviceBase* io);
void XModbus_setAddress(XModbus* modbus, uint8_t address);
void XModbus_setMode(XModbus* modbus, XModbusMode mode);
//当前是主站吗
bool XModbus_isMaster(XModbus* modbus);
//设置接收处理模式
void XModbus_setRecvHandMode(XModbus* modbus, XModbusRecvHandMode mode);
void XModbus_addRecvHand_CodeOnly(XModbus* modbus,uint8_t modbusCode, XModbusRecvHandCb cb, void* userData);
void XModbus_addRecvHand_AddressOnly(XModbus* modbus, uint8_t modbusAddress, XModbusRecvHandCb cb, void* userData);
void XModbus_addRecvHand_AddressCode(XModbus* modbus, uint8_t modbusAddress, uint8_t modbusCode, XModbusRecvHandCb cb, void* userData);
#define XModbus_connect_base                        XCommunicatorBase_connect_base
#define XModbus_disconnect_base                     XCommunicatorBase_disconnect_base
#define XModbus_isConnected_base                    XCommunicatorBase_isConnected_base
//发送一帧数据
#define XModbus_sendFrame_base                      XDataFrameComm_sendData_base
//主站定期发送
#define XModbus_sendFramePeriodicMaster             XDataFrameComm_addPeriodicSendData_base
//轮询处理
#define XModbus_poll_base XCommunicatorBase_poll_base

#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
