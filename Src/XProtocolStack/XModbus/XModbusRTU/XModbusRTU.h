#ifndef XMODBUSRTU_H
#define XMODBUSRTU_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XModbusBase.h"
#include "XTimerBase.h"
typedef struct XModbusFrame XModbusFrame;
    /* ----------------------- 常量定义 -----------------------------------*/
#define MB_SER_PDU_SIZE_MIN     4       // Modbus RTU帧最小长度（地址+功能码+数据+CRC=4字节）
#define MB_SER_PDU_SIZE_MAX     256     // Modbus RTU帧最大长度（256字节，含CRC）
#define MB_SER_PDU_SIZE_CRC     2       // CRC校验字段长度（2字节）
#define MB_SER_PDU_ADDR_OFF     0       // 从机地址在帧中的偏移（第0字节）
#define MB_SER_PDU_PDU_OFF      1       // Modbus PDU在帧中的偏移（地址后第1字节开始）

#define XMODBUSRTU_VTABLE_SIZE		(XMODBUSBASE_VTABLE_SIZE+10)       //XModbusRTU虚函数表大小
enum XModbusRTUVtableEnum
{
    EXModbusRTU_Send = XMODBUSBASE_VTABLE_SIZE,

};
typedef struct XModbusRTU
{
    XModbusBase m_parent;
}XModbusRTU;
XVtable* XModbusRTU_class_init();
void XModbusRTU_init(XModbusRTU* modbus);
#ifdef __cplusplus
}
#endif
#endif // !