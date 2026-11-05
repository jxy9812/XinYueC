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
#if XINDUSTRY_ON && (XTEMP_HUM_ON || XPRESS_DIFF_ON)
	/* 温湿度变送器/压差计（mock 通道，进程内自跑）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustrySensor 温湿度/压差计（mock）"),
		XIndustrySensorTest);
#endif
#if XINDUSTRY_ON && XFLOW_METER_ON
	/* 电磁流量计（mock 通道，进程内自跑；含 32 位拼装与编解码）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustryFlowMeter 电磁流量计（mock）"),
		XIndustryFlowMeterTest);
	/* 服务端流量计站（Hub 本地缓存驱动，不起串口/TCP）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XHsfServerIoFlowMeter 服务端流量计站"),
		XHsfServerIoFlowMeterTest);
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XHsfClientFlowMeter 客户端流量计 provider"),
		XHsfClientFlowMeterProviderTest);
#endif
#if XINDUSTRY_ON && XBUTTON_STATION_ON
	/* 按钮组/操作柱（mock 触点，进程内自跑；含急停安全侧与指示灯）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustryButtonStation 按钮组（mock）"),
		XIndustryButtonStationTest);
#endif
#if XINDUSTRY_ON && XTURBIDITY_ON
	/* 在线浊度仪（mock 通道，进程内自跑；含真机帧回归与编解码）。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XIndustryTurbidity 在线浊度仪（mock）"),
		XIndustryTurbidityTest);
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON
	/* 电磁阀组反馈新鲜度语义：模块离线必须报错，不得伪装成"无到位反馈"。 */
	XTestMenu_setActionFunction(
		XTestMenu_addAction(menu, "XHsfServerIoValveFresh 电磁阀反馈新鲜度"),
		XHsfServerIoValveFreshTest);
#endif
}
