/**
 * @file       xindustry_modbus_test.c
 * @brief      Industry 工业设备层 Modbus 通道适配器端到端测试（同进程 TCP 环回）。
 * @details    XModbusTcpServer（自动端口）作从站、XModbusTcpClient 挂四个
 *             适配器（DI/DO/AI/AO），走真实 Modbus/TCP 帧回路：DI 离散量
 *             轮询初值与翻转推送、AI 输入寄存器标度换算、AO 写保持寄存器
 *             writeDone 确认 + 服务端读回、DO 写线圈 writeDone 确认 +
 *             服务端读回、AI(useHolding) 对 AO 写入的全链读回闭环、通道
 *             valid 标志。骨架对齐 Test/XProtocolTest/XModbusPublicApiTest
 *             的 test_tcp_loopback 先例。
 */
#include "XIoChannel.h"
#include "XDigitalInput.h"
#include "XDigitalOutput.h"
#include "XChannel/XModbusDigitalInput.h"
#include "XChannel/XModbusDigitalOutput.h"
#include "XChannel/XModbusAnalogInput.h"
#include "XChannel/XModbusAnalogOutput.h"
#include "XModbusTcpServer.h"
#include "XModbusTcpClient.h"
#include "XModbusServer.h"
#include "XModbusDataUnit.h"
#include "XTcpServer.h"
#include "XHostAddress.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDeviceNetwork.h"
#include "XString.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* 通道信号记录（connect_2：第 1 参即 sender） */
static bool g_diValue = false;
static int  g_diChanges = 0;
static int32_t g_aiValue = 0;
static int  g_aiChanges = 0;
static int32_t g_aiHoldValue = 0;
static bool g_aoWriteOk = false;
static bool g_doWriteOk = false;

static void resetFlags(void)
{
    g_diValue = false; g_diChanges = 0;
    g_aiValue = 0;     g_aiChanges = 0;
    g_aiHoldValue = 0;
    g_aoWriteOk = false;
    g_doWriteOk = false;
}

static void onDiChanged2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_diValue = XVarList_arg(args, bool);
    g_diChanges++;
}

static void onAiChanged2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_aiValue = XVarList_arg(args, int32_t);
    g_aiChanges++;
}

static void onAiHoldChanged2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_aiHoldValue = XVarList_arg(args, int32_t);
}

static void onAoWriteDone2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_aoWriteOk = XVarList_arg(args, bool);
}

static void onDoWriteDone2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_doWriteOk = XVarList_arg(args, bool);
}

/* 非阻塞泵直到条件成立或超时。
 * @note 必须用 processEvents(0)+msleep 泵法：processEventsWithMaxTime
 *       的短超时模式不派发 XObject 对象定时器事件（实测适配器轮询全哑）。 */
static int waitUntil(int* flag, int timeoutMs)
{
    int waited = 0;
    while (!(*flag) && waited < timeoutMs) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
        waited += 2;
    }
    return *flag;
}

/* 服务端数据映射：四张表一次配齐（先例 configure_holding_registers 同型） */
static bool configureServerMaps(XModbusServer* server)
{
    XModbusDataUnitMap* map = XModbusDataUnitMap_create();
    XModbusDataUnit* di = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusDiscreteInputs, 0, 8);
    XModbusDataUnit* coils = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusCoils, 0, 8);
    XModbusDataUnit* ir = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusInputRegisters, 0, 8);
    XModbusDataUnit* hr = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusHoldingRegisters, 0, 16);
    XModbusRegisterType tDi = XModbusDiscreteInputs;
    XModbusRegisterType tCoils = XModbusCoils;
    XModbusRegisterType tIr = XModbusInputRegisters;
    XModbusRegisterType tHr = XModbusHoldingRegisters;
    bool ok = false;

    if (map && di && coils && ir && hr &&
        XMap_insert_base(map, &tDi, di) &&
        XMap_insert_base(map, &tCoils, coils) &&
        XMap_insert_base(map, &tIr, ir) &&
        XMap_insert_base(map, &tHr, hr))
        ok = XModbusServer_setMap_base(server, map);
    if (di) XClassDelete(di);
    if (coils) XClassDelete(coils);
    if (ir) XClassDelete(ir);
    if (hr) XClassDelete(hr);
    if (map) XClassDelete(map);
    return ok;
}

/* ==================== 用例 ==================== */

static void test_modbus_adapters_e2e(void)
{
    XModbusTcpServer* server = XModbusTcpServer_create();
    XModbusTcpClient* client = XModbusTcpClient_create();
    XModbusDigitalInput* di = NULL;
    XModbusAnalogInput* ai = NULL;
    XModbusAnalogInput* aiHold = NULL;
    XModbusAnalogOutput* ao = NULL;
    XModbusDigitalOutput* dop = NULL;
    uint16_t actualPort = 0;
    uint16_t regValue = 0;
    int i;

    resetFlags();

    /* --- 服务端：数据映射 + 初值 + 本机监听（自动端口） --- */
    {
        bool mapOk = false, listenOk = false;
        if (server) {
            mapOk = configureServerMaps((XModbusServer*)server);
            XModbusServer_setData2((XModbusServer*)server, XModbusDiscreteInputs, 0, 1);
            XModbusServer_setData2((XModbusServer*)server, XModbusInputRegisters, 0, 2530);
            XModbusDevice_setConnectionParameter((XModbusDevice*)server,
                XModbusDevice_NetworkAddressParameter,
                XVariant_create_utf8_str("127.0.0.1"));
            XModbusDevice_setConnectionParameter((XModbusDevice*)server,
                XModbusDevice_NetworkPortParameter, XVariant_create_int(0));
            listenOk = XModbusDevice_connectDevice((XModbusDevice*)server);
        }
        CHECK("服务端数据映射+监听", mapOk && listenOk && server &&
            XModbusDevice_state((XModbusDevice*)server) == XModbusDevice_ConnectedState);
    }
    if (server && server->m_tcpServer)
        actualPort = XTcpServer_serverPort(server->m_tcpServer);
    CHECK("服务端自动端口", actualPort != 0);

    /* --- 客户端连接 --- */
    if (client && actualPort) {
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkAddressParameter,
            XVariant_create_utf8_str("127.0.0.1"));
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkPortParameter, XVariant_create_int(actualPort));
        XModbusDevice_connectDevice((XModbusDevice*)client);
    }
    for (i = 0; client && i < 500 &&
         XModbusDevice_state((XModbusDevice*)client) != XModbusDevice_ConnectedState; ++i) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
    CHECK("客户端本机连接", client &&
        XModbusDevice_state((XModbusDevice*)client) == XModbusDevice_ConnectedState);
    if (!client || actualPort == 0 ||
        XModbusDevice_state((XModbusDevice*)client) != XModbusDevice_ConnectedState) {
        if (server) XModbusTcpServer_deleteLater(server);
        if (client) XModbusTcpClient_deleteLater(client);
        return;
    }

    /* --- 挂四个适配器（轮询 120ms，AI 标度 2530 raw -> 25 工程） --- */
    di = XModbusDigitalInput_create_ex((XModbusClient*)client, 1, 0, 120);
    ai = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 0, false,
                                      1, 100, 0, 0, 120);
    aiHold = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 10, true,
                                          1, 1, 0, 0, 120);
    ao = XModbusAnalogOutput_create_ex((XModbusClient*)client, 1, 10, 1, 1, 0);
    dop = XModbusDigitalOutput_create_ex((XModbusClient*)client, 1, 5);
    CHECK("四适配器创建", di && ai && aiHold && ao && dop);
    if (!di || !ai || !aiHold || !ao || !dop) {
        if (di) XClassDelete(di);
        if (ai) XClassDelete(ai);
        if (aiHold) XClassDelete(aiHold);
        if (ao) XClassDelete(ao);
        if (dop) XClassDelete(dop);
        XModbusTcpServer_deleteLater(server);
        XModbusTcpClient_deleteLater(client);
        return;
    }
    XObject_connect_2((XObject*)di, XSignal(XDigitalInput_inputChanged_signal), onDiChanged2);
    XObject_connect_2((XObject*)ai, XSignal(XAnalogInput_inputChanged_signal), onAiChanged2);
    XObject_connect_2((XObject*)aiHold, XSignal(XAnalogInput_inputChanged_signal), onAiHoldChanged2);
    XObject_connect_2((XObject*)ao, XSignal(XAnalogOutput_writeDone_signal), onAoWriteDone2);
    XObject_connect_2((XObject*)dop, XSignal(XDigitalOutput_writeDone_signal), onDoWriteDone2);

    /* --- DI：初值推送（服务端离散量 0 = 1） --- */
    {
        int flag = (g_diChanges > 0);
        waitUntil(&flag, 2000);
        CHECK("DI 初值轮询推送", g_diChanges >= 1 && g_diValue == true);
        CHECK("DI 通道 valid", XIoChannel_isValid((XIoChannel*)di));
    }

    /* --- AI：初值 2530 raw * 1/100 = 25 工程 --- */
    {
        int flag = (g_aiChanges > 0);
        waitUntil(&flag, 2000);
        CHECK("AI 标度换算初值", g_aiChanges >= 1 && g_aiValue == 25);
        CHECK("AI 通道 valid", XIoChannel_isValid((XIoChannel*)ai));
    }

    /* --- DI 翻转：服务端改值 -> 下一拍推送 false --- */
    XModbusServer_setData2((XModbusServer*)server, XModbusDiscreteInputs, 0, 0);
    {
        int flag = (g_diValue == false && g_diChanges >= 2);
        waitUntil(&flag, 2000);
        CHECK("DI 翻转推送", g_diValue == false && g_diChanges >= 2);
    }

    /* --- AO：写保持寄存器 10 = 777 -> writeDone + 服务端读回 --- */
    CHECK("AO 写受理", XAnalogOutput_write_base((XAnalogOutput*)ao, 777));
    {
        int flag = g_aoWriteOk;
        waitUntil(&flag, 3000);
        CHECK("AO writeDone(true)", g_aoWriteOk);
    }
    regValue = 0;
    CHECK("AO 服务端读回 777",
        XModbusServer_data2((XModbusServer*)server, XModbusHoldingRegisters, 10, &regValue) &&
        regValue == 777);

    /* --- AI(useHolding) 读回 AO 写入值：全链闭环 --- */
    {
        int flag = (g_aiHoldValue == 777);
        waitUntil(&flag, 3000);
        CHECK("AI(holding) 全链读回 777", g_aiHoldValue == 777);
    }

    /* --- DO：写线圈 5 = true -> writeDone + 服务端读回 --- */
    CHECK("DO 写受理", XDigitalOutput_write_base((XDigitalOutput*)dop, true));
    {
        int flag = g_doWriteOk;
        waitUntil(&flag, 3000);
        CHECK("DO writeDone(true)", g_doWriteOk);
    }
    regValue = 0;
    CHECK("DO 服务端读回 1",
        XModbusServer_data2((XModbusServer*)server, XModbusCoils, 5, &regValue) &&
        regValue == 1);

    XClassDelete(di);
    XClassDelete(ai);
    XClassDelete(aiHold);
    XClassDelete(ao);
    XClassDelete(dop);
    XModbusTcpServer_deleteLater(server);
    XModbusTcpClient_deleteLater(client);
}

/* ==================== 定时器环境探针 ==================== */

typedef struct XProbeTimer {
    XObject m_class;          /**< 第一个成员，基类 XObject。 */
    XTimerId m_timerId;       /**< 本探针的周期定时器 id。 */
    int m_ticks;              /**< 收到的 timerEvent 次数。 */
} XProbeTimer;

XCLASS_DEFINE_BEGING(XProbeTimer)
XCLASS_DEFINE_EXTEND_END(XProbeTimer, XObject)

static void VXProbeTimer_timerEvent(XObject* obj, XTimerEvent* event)
{
    XProbeTimer* p = (XProbeTimer*)obj;
    if (!p || !event) return;
    if (XTimerEvent_timerId(event) == p->m_timerId) {
        p->m_ticks++;
    }
    XEvent_accept(event);
}

XVtable* XProbeTimer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProbeTimer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXProbeTimer_timerEvent);
    return XVTABLE_DEFAULT;
}

static void XProbeTimer_init(XProbeTimer* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XProbeTimer));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XProbeTimer);
    obj->m_timerId = XTIMER_INVALID_ID;
    obj->m_ticks = 0;
}

/* ==================== main ==================== */

int main(int argc, char** argv)
{
    XCoreApplication* app;

    (void)argc;
    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }
    /* Windows 依赖 winsock：TCP 环回前必须显式初始化平台网络子系统
     * （主程序由启动链隐式完成，独立可执行需自管；引用计数式，可多调）。 */
    XDeviceNetwork_ensureInit();

    {   /* 定时器环境探针：本 exe 中 XObject 周期定时器是否 tick */
        XProbeTimer probe;
        int waited = 0;
        XProbeTimer_init(&probe);
        probe.m_timerId = XObject_startTimer_ms((XObject*)&probe, 50, XTimerType_CoarseTimer);
        while (waited < 500 && probe.m_ticks < 3) {
            XCoreApplication_processEvents(0);
            XThread_msleep(2);
            waited += 2;
        }
        XPrintf("[probe] object timer ticks=%d (id valid=%d)\n",
                probe.m_ticks, (int)(probe.m_timerId != XTIMER_INVALID_ID));
        if (probe.m_timerId != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)&probe, probe.m_timerId);
    }

    test_modbus_adapters_e2e();

    XPrintf("\n===== XIndustryModbus_Test: %d passed, %d failed =====\n",
            g_pass, g_fail);
    XDeviceNetwork_cleanup();
    XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

#else /* 开关未启用 */

#include "XPrintf.h"
int main(void)
{
    XPrintf("XIndustryModbus_Test: XINDUSTRY/XCHANNEL/XINDUSTRY_MODBUS 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */
