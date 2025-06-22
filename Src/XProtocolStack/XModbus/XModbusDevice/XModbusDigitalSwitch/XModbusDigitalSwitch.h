#ifndef XMODBUSDIGITALSWITCH_H
#define XMODBUSDIGITALSWITCH_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XModbusCoilsDisc.h"
#include"XIODeviceBase.h"
typedef enum 
{
	XMB_DS_OutTimerMode_Continuous_Run,//输出采集定时器连续运行，不一致自动重发
	XMB_DS_OutTimerMode_Write_Run,//输出采集定时器写入后开始运行,远程和本地相同后退出采集，不一致自动重发
}XMB_DS_OutTimerMode;//输出状态定时采集模式
//modbus数字开关
typedef struct XModbusDigitalSwitch
{
	uint8_t m_address;
	uint16_t m_inAddressOffset;//输入Modbus地址偏移
	uint16_t m_outAddressOffset;//输出Modbus地址偏移
	XModbusCoilsDisc* m_in;//当前输入
	XByteArray* m_cmpIn;//比较输入输入
	XModbusCoilsDisc* m_out;//读取的输出
	XModbusCoilsDisc* m_cmpOut;//比较输出
	XHandle m_outHandle;
	XHandle m_inHandle;
	XMB_DS_OutTimerMode m_outTimerMode;
	XModbus* m_modbus;
	XVector* m_ioInList;//
	XVector* m_ioOutList;//
}XModbusDigitalSwitch;
//创建线圈或离散功能类 要创建几个离散或线圈
XModbusDigitalSwitch* XModbusDigitalSwitch_create(XModbus* modbus,uint8_t address, uint16_t inCount, uint16_t outCount);
//释放除modbus以外的成员包括自身
void XModbusDigitalSwitch_setAddress(XModbusDigitalSwitch*ds, uint8_t address);
void XModbusDigitalSwitch_delete(XModbusDigitalSwitch* ds);
bool XModbusDigitalSwitch_setScanningPeriod_RTU(XModbusDigitalSwitch* ds,uint32_t time);
void XModbusDigitalSwitch_setOutTimerMode(XModbusDigitalSwitch* ds, XMB_DS_OutTimerMode mode);
/*XSwitchDeviceModbus 调用的API*/
bool XModbusDigitalSwitch_XSwitchDeviceModbusOpen(XModbusDigitalSwitch* ds, XSwitchDeviceModbus* sw, XIODeviceBaseMode mode, uint16_t portNum);
void XModbusDigitalSwitch_XSwitchDeviceModbusClose(XModbusDigitalSwitch* ds, XSwitchDeviceModbus* sw, XIODeviceBaseMode mode, uint16_t portNum);
bool XModbusDigitalSwitch_readIn(XModbusDigitalSwitch* ds, uint16_t portNum, bool* state);
bool XModbusDigitalSwitch_writeOut(XModbusDigitalSwitch* ds, uint16_t portNum, bool state);
#ifdef __cplusplus
}
#endif
#endif // !XModbusFuncCode_H
