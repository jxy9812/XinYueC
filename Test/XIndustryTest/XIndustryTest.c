#include "XIndustryTest.h"
#include "XTestMenu.h"
#include "XAction.h"

void XTestMenu_XIndustryTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("工业");
	XTestMenu_addMenu(root, menu);
#if XINDUSTRY_ON && XSOLENOID_VALVE_ON
	/* 电磁阀状态机（mock 通道，进程内自跑）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustryValve 电磁阀状态机（mock）"),
		XIndustryValveTest);
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON
	/* Modbus 适配器闭环（TCP 环回进程内自跑；RTU 组合走驱动脚本）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustryModbus 适配器闭环（TCP 环回）"),
		XIndustryModbusTest);
#endif
}
