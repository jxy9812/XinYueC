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
#include "XModbusRtuSerialClient.h"
#include "XModbusClient.h"
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
#include <stdlib.h>
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
            do { XVariant* vParam = XVariant_create_utf8_str("127.0.0.1"); XModbusDevice_setConnectionParameter((XModbusDevice*)server,
                XModbusDevice_NetworkAddressParameter, vParam); XClassDelete((XClass*)vParam); } while (0);
            do { XVariant* vParam = XVariant_create_int(0); XModbusDevice_setConnectionParameter((XModbusDevice*)server,
                XModbusDevice_NetworkPortParameter, vParam); XClassDelete((XClass*)vParam); } while (0);
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
        do { XVariant* vParam = XVariant_create_utf8_str("127.0.0.1"); XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkAddressParameter, vParam); XClassDelete((XClass*)vParam); } while (0);
        do { XVariant* vParam = XVariant_create_int(actualPort); XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkPortParameter, vParam); XClassDelete((XClass*)vParam); } while (0);
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

/* ==================== RTU 组合用例（2026-10-31，hub 从站拓扑） ====================
 * 依赖环境变量 INDUSTRY_RTU_PTY 指向从站 pty（mbtest_industry_run.sh 起
 * mbtest_hub_slaves.py 后传入）。初值口径：RTU 总线侧 DI0=1、IR0=7
 * （与 TCP 面板的 IR0=2530 寄存器独立）；HR10/12/13 可写。覆盖：RTU 四适配器全链、AI den=0 视 1（同址
 * 参照比对）、AO 负值/越界钳位读回、DO 广播受理即完成、AO 广播效果读回、
 * DO 单飞 Busy 拒绝、大死区首拍恒发且静态不重复推。 */

static int g_rdiVal = 0;  static int g_rdiN = 0;
static int g_raiVal = 0;  static int g_raiN = 0;
static int g_rdnVal = 0;  static int g_rdnN = 0;   /* den=0 视 1 */
static int g_rdrVal = 0;  static int g_rdrN = 0;   /* den=1 同址参照 */
static int g_ra10Val = 0; static int g_ra10N = 0;  /* HR10 读回 */
static int g_ra12Val = 0; static int g_ra12N = 0;  /* HR12 读回（钳位） */
static int g_ra13Val = 0; static int g_ra13N = 0;  /* HR13 读回（广播） */
static int g_rai0Val = 0; static int g_rai0N = 0;  /* IR0 标度 1/1 对照 */
static int g_ra12Base = 0;                         /* HR12 钳位轮询基线 */
static int g_ra13Base = 0;                         /* HR13 广播轮询基线 */
static int g_raoOk = 0;   static int g_raoN = 0;
static int g_rdoOk = 0;   static int g_rdoN = 0;
static int g_rbyOk = 0;   static int g_rbyN = 0;

static void rOnDi2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rdiVal = XVarList_arg(args, bool); g_rdiN++; }
static void rOnAi2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_raiVal = XVarList_arg(args, int32_t); g_raiN++; }
static void rOnDn2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rdnVal = XVarList_arg(args, int32_t); g_rdnN++; }
static void rOnDr2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rdrVal = XVarList_arg(args, int32_t); g_rdrN++; }
static int g_rdbVal = 0;  static int g_rdbN = 0;   /* 大死区首拍语义 */
static void rOnDb2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rdbVal = XVarList_arg(args, int32_t); g_rdbN++; }
static void rOnA12(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_ra12Val = XVarList_arg(args, int32_t); g_ra12N++; }
static void rOnA13(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_ra13Val = XVarList_arg(args, int32_t); g_ra13N++; }
static void rOnAi0(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rai0Val = XVarList_arg(args, int32_t); g_rai0N++; }
static void rOnA10(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_ra10Val = XVarList_arg(args, int32_t); g_ra10N++; }
static void rOnAo2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_raoOk = XVarList_arg(args, bool) ? 1 : 0; g_raoN++; }
static void rOnDo2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rdoOk = XVarList_arg(args, bool) ? 1 : 0; g_rdoN++; }
static void rOnBy2(XObject* sender, XVarList* args)
{ (void)sender; XVarList_start(args); g_rbyOk = XVarList_arg(args, bool) ? 1 : 0; g_rbyN++; }

static void test_modbus_adapters_rtu(const char* pty)
{
    XModbusRtuSerialClient* client;
    XModbusDigitalInput* di;
    XModbusAnalogInput* ai;
    XModbusAnalogInput* aiDen;
    XModbusAnalogInput* aiRef;
    XModbusAnalogInput* aiDb;
    XModbusAnalogInput* aiRef0;
    XModbusAnalogInput* aiHold10;
    XModbusAnalogInput* aiHold12;
    XModbusAnalogInput* aiHold13;
    XModbusAnalogOutput* ao10;
    XModbusAnalogOutput* ao12;
    XModbusAnalogOutput* aoBc;
    XModbusDigitalOutput* dop;
    XModbusDigitalOutput* dob;
    XModbusDigitalOutput* dbusy;
    int flag = 0;
    int i;

    printf("== RTU 组合：四适配器/广播/Busy/钳位/死区首拍 ==\n"); fflush(stdout);
    client = XModbusRtuSerialClient_create();
    CHECK("RTU client 创建", client != NULL);
    if (!client) return;
    XModbusClient_setTimeout((XModbusClient*)client, 1000);
    XModbusClient_setNumberOfRetries((XModbusClient*)client, 0);
    XModbusRtuSerialClient_setTurnaroundDelay(client, 60);
    {
        XVariant* v1 = XVariant_create_utf8_str(pty);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_SerialPortNameParameter, v1);
        XClassDelete((XClass*)v1);
    }
    {
        XVariant* v2 = XVariant_create_int(9600);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_SerialBaudRateParameter, v2);
        XClassDelete((XClass*)v2);
    }
    CHECK("RTU 连接发起", XModbusDevice_connectDevice((XModbusDevice*)client));
    for (i = 0; i < 1500 && XModbusDevice_state((XModbusDevice*)client) !=
         XModbusDevice_ConnectedState; ++i) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
    CHECK("RTU 连接 pty 就绪",
          XModbusDevice_state((XModbusDevice*)client) == XModbusDevice_ConnectedState);

    di = XModbusDigitalInput_create_ex((XModbusClient*)client, 1, 0, 120);
    ai = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 0, false, 10, 1, 0, 0, 120);
    aiRef0 = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 0, false, 1, 1, 0, 0, 120);
    aiDen = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 1, false, 1, 0, 0, 0, 150);
    aiRef = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 1, false, 1, 1, 0, 0, 150);
    aiDb = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 2, false, 1, 1, 0, 60000, 150);
    aiHold10 = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 10, true, 1, 1, 0, 0, 150);
    aiHold12 = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 12, true, 1, 1, 0, 0, 150);
    aiHold13 = XModbusAnalogInput_create_ex((XModbusClient*)client, 1, 13, true, 1, 1, 0, 0, 150);
    ao10 = XModbusAnalogOutput_create_ex((XModbusClient*)client, 1, 10, 1, 1, 0);
    ao12 = XModbusAnalogOutput_create_ex((XModbusClient*)client, 1, 12, 1, 1, 0);
    aoBc = XModbusAnalogOutput_create_ex((XModbusClient*)client, 0, 13, 1, 1, 0);
    dop = XModbusDigitalOutput_create_ex((XModbusClient*)client, 1, 5);
    dob = XModbusDigitalOutput_create_ex((XModbusClient*)client, 0, 6);
    dbusy = XModbusDigitalOutput_create_ex((XModbusClient*)client, 1, 7);
    CHECK("RTU 十四适配器创建",
          di && ai && aiRef0 && aiDen && aiRef && aiDb && aiHold10 && aiHold12 && aiHold13 &&
          ao10 && ao12 && aoBc && dop && dob && dbusy);
    if (!di || !ai || !aiRef0 || !aiDen || !aiRef || !aiDb || !aiHold10 || !aiHold12 ||
        !aiHold13 || !ao10 || !ao12 || !aoBc || !dop || !dob || !dbusy) {
        XModbusDevice_disconnectDevice((XModbusDevice*)client);
        XObject_deleteLater((XObject*)client);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return;
    }
    XObject_connect_2((XObject*)di, XSignal(XDigitalInput_inputChanged_signal), rOnDi2);
    XObject_connect_2((XObject*)ai, XSignal(XAnalogInput_inputChanged_signal), rOnAi2);
    XObject_connect_2((XObject*)aiRef0, XSignal(XAnalogInput_inputChanged_signal), rOnAi0);
    XObject_connect_2((XObject*)aiDen, XSignal(XAnalogInput_inputChanged_signal), rOnDn2);
    XObject_connect_2((XObject*)aiRef, XSignal(XAnalogInput_inputChanged_signal), rOnDr2);
    XObject_connect_2((XObject*)aiDb, XSignal(XAnalogInput_inputChanged_signal), rOnDb2);
    XObject_connect_2((XObject*)aiHold10, XSignal(XAnalogInput_inputChanged_signal), rOnA10);
    XObject_connect_2((XObject*)aiHold12, XSignal(XAnalogInput_inputChanged_signal), rOnA12);
    XObject_connect_2((XObject*)aiHold13, XSignal(XAnalogInput_inputChanged_signal), rOnA13);
    XObject_connect_2((XObject*)ao10, XSignal(XAnalogOutput_writeDone_signal), rOnAo2);
    XObject_connect_2((XObject*)ao12, XSignal(XAnalogOutput_writeDone_signal), rOnAo2);
    XObject_connect_2((XObject*)aoBc, XSignal(XAnalogOutput_writeDone_signal), rOnAo2);
    XObject_connect_2((XObject*)dop, XSignal(XDigitalOutput_writeDone_signal), rOnDo2);
    XObject_connect_2((XObject*)dob, XSignal(XDigitalOutput_writeDone_signal), rOnDo2);
    XObject_connect_2((XObject*)dbusy, XSignal(XDigitalOutput_writeDone_signal), rOnBy2);

    /* 1) DI 初值推送（hub 从站 DI0=1）。 */
    flag = (g_rdiN > 0);
    waitUntil(&flag, 2000);
    CHECK("RTU DI 初值推送 true", g_rdiN >= 1 && g_rdiVal == true);

    /* 2) AI 标度初值：RTU 总线寄存器与 TCP 面板不同源——hub 从站 RTU 侧
     * IR0=7（已知初值），×10 → 70；同址 1/1 对照应为原始码 7。 */
    flag = (g_raiN > 0 && g_rai0N > 0);
    waitUntil(&flag, 2500);
    CHECK("RTU AI 标度初值 70（RTU 侧 IR0=7 × 10）",
          g_raiN >= 1 && g_raiVal == 70 && g_rai0Val == 7);

    /* 3) AI den=0 视为 1：同址 den=0 与 den=1 两路推送值恒等。 */
    flag = (g_rdnN > 0 && g_rdrN > 0);
    waitUntil(&flag, 2500);
    CHECK("AI den=0 与 den=1 同址同值（视 1）",
          g_rdnN >= 1 && g_rdrN >= 1 && g_rdnVal == g_rdrVal);

    /* 4) 大死区首拍恒发 + 静态不重复推。 */
    XThread_msleep(400);
    XCoreApplication_processEvents(0);
    CHECK("AI 大死区仅首拍推送一次", g_rdbN == 1);
    XThread_msleep(300);
    XCoreApplication_processEvents(0);
    CHECK("AI 大死区静态不重复推送", g_rdbN == 1);

    /* 5) AO 写 HR10=777 → writeDone → aiHold 全链读回。 */
    g_raoN = 0;
    CHECK("RTU AO 写 777 受理", XAnalogOutput_write_base((XAnalogOutput*)ao10, 777));
    flag = (g_raoN > 0);
    waitUntil(&flag, 3000);
    CHECK("RTU AO writeDone(true) 恰一次", g_raoN == 1 && g_raoOk == 1);
    flag = (g_ra10N > 0 && g_ra10Val == 777);
    waitUntil(&flag, 3000);
    CHECK("RTU AO→aiHold 全链读回 777", g_ra10Val == 777);

    /* 6) AO 钳位：-5→0、70000→65535（HR12）。等待口径=「写落定后的下一次
     * 轮询推送」（基线计数），防 HR12 初值 0 抢跑 + 单飞 Busy。 */
    g_ra12Base = g_ra12N;
    CHECK("AO 写 500 建基线", XAnalogOutput_write_base((XAnalogOutput*)ao12, 500));
    flag = (g_ra12N > g_ra12Base && g_ra12Val == 500);
    waitUntil(&flag, 4000);
    CHECK("AO 基线 500 读回", g_ra12N > g_ra12Base && g_ra12Val == 500);
    g_ra12Base = g_ra12N;
    CHECK("AO 写 -5 受理", XAnalogOutput_write_base((XAnalogOutput*)ao12, -5));
    flag = (g_ra12N > g_ra12Base && g_ra12Val == 0);
    waitUntil(&flag, 4000);
    CHECK("AO 负值钳 0 读回", g_ra12N > g_ra12Base && g_ra12Val == 0);
    g_ra12Base = g_ra12N;
    CHECK("AO 写 70000 受理", XAnalogOutput_write_base((XAnalogOutput*)ao12, 70000));
    flag = (g_ra12N > g_ra12Base && g_ra12Val == 65535);
    waitUntil(&flag, 4000);
    CHECK("AO 越界钳 65535 读回", g_ra12N > g_ra12Base && g_ra12Val == 65535);

    /* 7) DO 写线圈 writeDone（RTU 应答）。 */
    g_rdoN = 0;
    CHECK("RTU DO 写受理", XDigitalOutput_write_base((XDigitalOutput*)dop, true));
    flag = (g_rdoN > 0);
    waitUntil(&flag, 3000);
    CHECK("RTU DO writeDone(true) 恰一次", g_rdoN == 1 && g_rdoOk == 1);

    /* 8) DO 单飞 Busy：第一写在途时第二写立即拒绝；随后第一写正常落定。 */
    g_rbyN = 0;
    CHECK("Busy 写①受理", XDigitalOutput_write_base((XDigitalOutput*)dbusy, true));
    CHECK("Busy 写②在途被拒",
          !XDigitalOutput_write_base((XDigitalOutput*)dbusy, false));
    flag = (g_rbyN > 0);
    waitUntil(&flag, 3000);
    CHECK("Busy 写①正常落定恰一次（仅一笔上总线）", g_rbyN == 1 && g_rbyOk == 1);

    /* 9) DO/AO 广播：受理即完成（writeDone(true) 同步发出）。 */
    g_rdoN = 0;
    CHECK("DO 广播受理", XDigitalOutput_write_base((XDigitalOutput*)dob, true));
    CHECK("DO 广播同步 writeDone(true)", g_rdoN == 1 && g_rdoOk == 1);
    g_ra13Base = g_ra13N;
    CHECK("AO 广播受理", XAnalogOutput_write_base((XAnalogOutput*)aoBc, 999));
    flag = (g_ra13N > g_ra13Base && g_ra13Val == 999);
    waitUntil(&flag, 4000);
    CHECK("AO 广播效果读回 HR13=999（unit1 命中）",
          g_ra13N > g_ra13Base && g_ra13Val == 999);

    XClassDelete(di);
    XClassDelete(ai);
    XClassDelete(aiDen);
    XClassDelete(aiRef);
    XClassDelete(aiDb);
    XClassDelete(aiHold10);
    XClassDelete(aiHold12);
    XClassDelete(aiHold13);
    XClassDelete(ao10);
    XClassDelete(ao12);
    XClassDelete(aoBc);
    XClassDelete(dop);
    XClassDelete(dob);
    XClassDelete(dbusy);
    fprintf(stderr,
            "[rtu-diag] di=%d/%d ai=%d(%d) rai0=%d den=%d db=%d a10=%d/%d "
            "a12=%d/%d a13=%d/%d ao=%d/%d do=%d/%d\n",
            g_rdiVal, g_rdiN, g_raiVal, g_raiN, g_rai0Val, g_rdnVal, g_rdbVal,
            g_ra10Val, g_ra10N, g_ra12Val, g_ra12N, g_ra13Val, g_ra13N,
            g_raoOk, g_raoN, g_rdoOk, g_rdoN);
    XModbusDevice_disconnectDevice((XModbusDevice*)client);
    XObject_deleteLater((XObject*)client);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
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

/* ==================== 收编入口（2026-11-01 去 main 收编，XModbusHubTest
 * 同款：_run 供 --test/Test 命令分派并回失败数，菜单动作签名同
 * XModbusPublicApiTest。TCP 环回进程内自跑；RTU 组合经 INDUSTRY_RTU_PTY
 * 环境变量由驱动脚本 mbtest_industry_run.sh 起从站后驱动） ==================== */

int XIndustryModbusTest_run(void)
{
    XCoreApplication* app;

    app = XCoreApplication_create(0, NULL);
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

    /* RTU 组合用例：由驱动脚本起好从站后经环境变量传入 pty 路径。 */
    {
        const char* rtuPty = getenv("INDUSTRY_RTU_PTY");
        fprintf(stderr, "[main] rtuPty=%p val=%s\n",
                (void*)rtuPty, rtuPty ? rtuPty : "(null)");
        if (rtuPty && rtuPty[0]) test_modbus_adapters_rtu(rtuPty);
        else printf("[skip] INDUSTRY_RTU_PTY 未设置，RTU 组合用例跳过\n");
    }

    XPrintf("\n===== XIndustryModbusTest: %d passed, %d failed =====\n",
            g_pass, g_fail);
    XDeviceNetwork_cleanup();
    XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XIndustryModbusTest(XVariant* data)
{
    const int fails = XIndustryModbusTest_run();
    (void)data;
    XPrintf("=== XIndustryModbusTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* 开关未启用 */

#include "XPrintf.h"
int XIndustryModbusTest_run(void)
{
    XPrintf("XIndustryModbusTest: XINDUSTRY/XCHANNEL/XINDUSTRY_MODBUS 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */
