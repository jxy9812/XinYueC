#ifndef XINDUSTRY_TEST_H
#define XINDUSTRY_TEST_H
/* ==================== Industry 工业设备层测试域（2026-11-01 收编） ====================
 * 原 Src/Industry/test/ 两个自带 main 的独立可执行（XIndustryValve_Test /
 * XIndustryModbus_Test）去 main 收编进主程序，XModbusHubTest 同款：
 *   int XxxTest_run(void)        --test/Test 命令分派，返回失败数
 *   void XxxTest(XVariant*)      测试菜单动作（签名同 XModbusPublicApiTest）
 * 跑法：
 *   电磁阀  --test xindustry-valve            mock 通道进程内自跑
 *   Modbus  --test xindustry-modbus           TCP 环回进程内自跑
 *            （RTU 组合经 INDUSTRY_RTU_PTY 环境变量，由
 *              Test/XProtocolTest/mbtest_industry_run.sh 起从站后驱动）
 * 菜单：「测试代码 → 工业」。开关未启用时仅存 _run 跳过桩，菜单动作与
 * 分派项随特性宏一同裁掉（守卫与实现文件同款）。
 */
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"

void XTestMenu_XIndustryTest(XTestMenu* root);

#if XINDUSTRY_ON && XSOLENOID_VALVE_ON
	int XIndustryValveTest_run(void);
	void XIndustryValveTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON
	int XIndustryModbusTest_run(void);
	void XIndustryModbusTest(XVariant* data);
#endif
#if XINDUSTRY_ON && (XTEMP_HUM_ON || XPRESS_DIFF_ON)
	int XIndustrySensorTest_run(void);
	void XIndustrySensorTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XFLOW_METER_ON
	int XIndustryFlowMeterTest_run(void);
	void XIndustryFlowMeterTest(XVariant* data);
	/* 服务端流量计站隔离单测（Hub 本地缓存驱动；需链接 XHsfServerIoLib）。 */
	int XHsfServerIoFlowMeterTest_run(void);
	void XHsfServerIoFlowMeterTest(XVariant* data);
	/* 客户端流量计 provider 隔离单测（注入 BSON 帧，不走网络）。 */
	int XHsfClientFlowMeterProviderTest_run(void);
	void XHsfClientFlowMeterProviderTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XTURBIDITY_ON
	/* 在线浊度仪（通道组喂点进程内自跑；含真机帧回归 + 三种口径旋钮 +
	 * Analog 二级标度 + 编解码往返）。 */
	int XIndustryTurbidityTest_run(void);
	void XIndustryTurbidityTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XBUTTON_STATION_ON
	int XIndustryButtonStationTest_run(void);
	void XIndustryButtonStationTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XAUTO_CONTROL_ON
	/* 自动运行控制器（纯框架件；状态机/步进闸门/报警队列/序列化，无设备）。 */
	int XAutoControlTest_run(void);
	void XAutoControlTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XACCOUNT_ON && XACCESS_CONTROL_ON
	/* 访问控制三件（账户管理/权限策略/登录策略 + 编解码往返）。 */
	int XAccessControlTest_run(void);
	void XAccessControlTest(XVariant* data);
#endif
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON
	/* 电磁阀组「反馈模块离线」语义单测（纯业务层，不需要 TCP 从站）。
	 * 锁定 XIoLink::RoleFresh → XIoChannelGroup_isFresh → 阀组按通道新鲜度
	 * 这条链路的对外行为：离线必须报错，不得退化成"无到位反馈"。 */
	int XHsfServerIoValveFreshTest_run(void);
	void XHsfServerIoValveFreshTest(XVariant* data);
	/* 服务端「模拟设备模式」全链路回归（HsfServer --sim）。
	 * 锁定四类缺陷：多从站按站号分派、VFD 整段应答、静止量心跳拍、
	 * 32 位量低字非 0；外加阀控端到端（写 DO → 行程开关联动 → 状态翻转）。 */
	int XHsfServerIoSimModeTest_run(void);
	void XHsfServerIoSimModeTest(XVariant* data);
#endif

#ifdef __cplusplus
}
#endif
#endif /* XINDUSTRY_TEST_H */
