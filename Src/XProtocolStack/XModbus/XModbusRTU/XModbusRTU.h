#ifndef XMODBUSRTU_H
#define XMODBUSRTU_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XModbusBase.h"
#include "XTimerBase.h"
#include "XSerialPortBase.h"
typedef struct XModbusFrame XModbusFrame;
    /* ----------------------- 常量定义 -----------------------------------*/
#define MB_SER_PDU_SIZE_MIN     4       // Modbus RTU帧最小长度（地址+功能码+数据+CRC=4字节）
#define MB_SER_PDU_SIZE_MAX     256     // Modbus RTU帧最大长度（256字节，含CRC）
#define MB_SER_PDU_SIZE_CRC     2       // CRC校验字段长度（2字节）
#define MB_SER_PDU_ADDR_OFF     0       // 从机地址在帧中的偏移（第0字节）
#define MB_SER_PDU_PDU_OFF      1       // Modbus PDU在帧中的偏移（地址后第1字节开始）

#define XMODBUSRTU_VTABLE_SIZE		(XMODBUSBASE_VTABLE_SIZE)       //XModbusRTU虚函数表大小
enum XModbusRTUVtableEnum
{
    EXModbusRTU_Send = XMODBUSBASE_VTABLE_SIZE,

};
typedef struct XModbusRTU
{
    XModbusBase m_parent;
    XTimerBase* m_timerT35Expired;//检测帧间隔超时
    XTimerBase* m_timerSendExpired;//检测发送帧到期
}XModbusRTU;
XVtable* XModbusRTU_class_init();
void XModbusRTU_init(XModbusRTU* modbus, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired);
//创建串口 
XModbusRTU* XModbusRTU_newSerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired);

#define XModbusRTU_connect_base             XModbusBase_connect_base                
#define XModbusRTU_disconnect_base          XModbusBase_disconnect_base             
#define XModbusRTU_isConnected_base         XModbusBase_isConnected_base            
#ifdef __cplusplus
}
#endif
#endif // !