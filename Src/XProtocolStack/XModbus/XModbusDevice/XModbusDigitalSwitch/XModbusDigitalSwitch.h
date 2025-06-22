#ifndef XMODBUSDIGITALSWITCH_H
#define XMODBUSDIGITALSWITCH_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusCoilsDisc.h"
//modbus数字开关
typedef struct XModbusDigitalSwitch
{
	uint8_t m_address;
	XModbusCoilsDisc* m_in;//当前输入
	XByteArray* m_cmpIn;//比较输入输入
	XModbusCoilsDisc* m_out;//读取的输出
	XByteArray* m_cmpOut;//比较输出
	XHandle m_outHandle;
	XHandle m_inHandle;
	XModbus* m_modbus;
}XModbusDigitalSwitch;
//创建线圈或离散功能类 要创建几个离散或线圈
XModbusDigitalSwitch* XModbusDigitalSwitch_create(XModbus* modbus,uint8_t address, uint16_t inCount, uint16_t outCount);
//释放除modbus以外的成员包括自身
void XModbusDigitalSwitch_setAddress(XModbusDigitalSwitch*ds, uint8_t address);
void XModbusDigitalSwitch_delete(XModbusDigitalSwitch* ds);
bool XModbusDigitalSwitch_setScanningPeriod_RTU(XModbusDigitalSwitch* ds,uint32_t time);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
