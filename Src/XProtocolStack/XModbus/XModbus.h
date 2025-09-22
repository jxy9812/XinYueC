#ifndef XMODBUS_H
#define XMODBUS_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include"XModbusEnum.h"
#include"XDataFrameComm.h"
//使用默认的Modbus TCP端口（502）
#define MB_TCP_PORT_USE_DEFAULT 0
#define XMODBUS_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))       //XModbus虚函数表大小
XCLASS_DEFINE_BEGING(XModbus)
//EXModbus_SendFrame = XCLASS_VTABLE_GET_SIZE(XDataFrameComm),
XCLASS_DEFINE_END(XModbus)

typedef struct XModbus
{
    XDataFrameComm m_class;
    uint8_t    m_address;         // Modbus 主机地址（1-247，0 为广播地址，255 保留）
    XModbusMode m_mode;//模式
}XModbus;
XVtable* XModbus_class_init();
//基础创建
XModbus* XModbus_create(XIODeviceBase* io);
//创建RTU协议的Modbus
XModbus* XModbus_create_RTU(XIODeviceBase* io, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired);
//用串口创建RTU 
XModbus* XModbus_create_RTU_SerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired);
void XModbus_init(XModbus* modbus, XIODeviceBase* io);
void XModbus_setAddress(XModbus* modbus, uint8_t address);
void XModbus_setMode(XModbus* modbus, XModbusMode mode);
//当前是主站吗
bool XModbus_isMaster(XModbus* modbus);
//只匹配modbus的功能码
void XModbus_addRecvHand_CodeOnly(XModbus* modbus,uint8_t modbusCode, XModbusRecvHandCb cb, void* userData);
//只匹配modbus的地址
void XModbus_addRecvHand_AddressOnly(XModbus* modbus, uint8_t modbusAddress, XModbusRecvHandCb cb, void* userData);
//同时匹配modbus的地址和功能码
void XModbus_addRecvHand_AddressCode(XModbus* modbus, uint8_t modbusAddress, uint8_t modbusCode, XModbusRecvHandCb cb, void* userData);
void XModbus_removeRecvHand_CodeOnly(XModbus* modbus, uint8_t modbusCode);
void XModbus_removeRecvHand_AddressOnly(XModbus* modbus, uint8_t modbusAddress);
void XModbus_removeRecvHand_AddressCode(XModbus* modbus, uint8_t modbusAddress, uint8_t modbusCode);

void XModbus_sendFrame(XModbus* modbus, XModbusFrame* frame);
//主站模式下设置帧轮询
void XModbus_sendFramePeriodicMaster(XModbus* modbus, XModbusFrame* frame, uint32_t time);
#define XModbus_connect_base                        XCommunicatorBase_connect_base
#define XModbus_disconnect_base                     XCommunicatorBase_disconnect_base
#define XModbus_isConnected_base                    XCommunicatorBase_isConnected_base
//发送一帧数据
#define XModbus_sendData_base                       XDataFrameComm_sendData_base
//主站定期发送
#define XModbus_sendDataPeriodicMaster_base         XDataFrameComm_addPeriodicSendData_base
//轮询处理
#define XModbus_poll_base                           XCommunicatorBase_poll_base
#define XModbus_delete_base                         XDataFrameComm_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XModbus_H
