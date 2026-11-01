/**
 * @file       xindustry_modbus_test.c
 * @brief      Industry 工业设备层通道组（XIoChannelGroup + XIoPoint）端到端
 *             测试（同进程 TCP 环回）。
 * @details    [2026-11-01 通道组重构] 原 XModbus* 四适配器（每通道自带
 *             轮询/单飞的 XObject）已由 XIoChannelGroup + XIoPoint 轻量点
 *             模型取代：XModbusTcpServer（自动端口）作从站、Hub 聚合轮询
 *             DI/Coils/HR 三区、XIoChannelGroup 承载四种点（XModbusPoint
 *             数据源点，组内全部 XIoPoint* 直传，无 id）。覆盖：
 *               - 上线对齐/播种：登记即从 Hub 缓存取初值（默认值结果）；
 *               - DI：server 翻转 → inputChanged(pt,1) + read；
 *               - AI：server 改 HR → requestRefresh 回填 → inputChanged；
 *               - DO/AO：write 受理 → writeDone(pt,true) + 从站侧读回；
 *               - remove(autoDelete 点)：摘除即销毁 + contains/recordCount。
 *             [同泵粘性命中] HR 区段推送按地址分发会连带喂到 aoPt（如
 *             段内其它地址的回灌值），同一次泵内多信号相继到达——判定
 *             目标 (pt,value) 对在槽内逐帧比对（g_hit*），泵间隙只看
 *             "最近一帧"会漏。
 *             骨架对齐 Test/XProtocolTest/XModbusPublicApiTest 的
 *             test_tcp_loopback 先例；RTU 总线场景由 Hub 层测试覆盖
 *             （Test/XProtocolTest/xmodbus_hub_test.c）。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XModbusPoint.h"
#include "XModbusTcpServer.h"
#include "XModbusTcpClient.h"
#include "XModbusHub.h"
#include "XModbusServer.h"
#include "XModbusDataUnit.h"
#include "XTcpServer.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XDeviceNetwork.h"
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

/* 组级信号记录（XIoPoint* 直传） */
static int  g_inChanged = 0;          /* inputChanged 次数 */
static XIoPoint* g_inLastPt = NULL;   /* 最近一次 inputChanged 的点 */
static int32_t g_inLastValue = -1;
static XIoPoint* g_doneLastPt = NULL; /* 最近一次 writeDone(ok=true) 的点 */
static int  g_doneCount = 0;

/* 同泵粘性命中目标（见文件头 [同泵粘性命中] 注记） */
static XIoPoint* g_hitTargetPt = NULL;
static int32_t g_hitTargetVal = 0;
static int g_hitFlag = 0;

static void onGroupInputChanged(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, XIoPoint*, point, int32_t, value);
    (void)sender;
    ++g_inChanged;
    g_inLastPt = point;
    g_inLastValue = value;
    if (g_hitTargetPt && point == g_hitTargetPt && value == g_hitTargetVal)
        g_hitFlag = 1;
}

static void onGroupWriteDone(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, XIoPoint*, point, bool, ok);
    (void)sender;
    if (ok) {
        g_doneLastPt = point;
        ++g_doneCount;
    }
}

/**
 * @brief 泵事件循环直至条件成立或超时。
 * @return 条件成立 true；超时 false。
 */
static int waitCond(int* flag, int expect, int timeoutMs)
{
    int waited = 0;
    while (*flag != expect && waited < timeoutMs) {
        XCoreApplication_processEventsWithMaxTime(0, 10);
        waited += 10;
    }
    return *flag == expect;
}

/* ==================== 主用例：通道组 × Hub 端到端 ==================== */

static void test_channel_group_hub(void)
{
    XCoreApplication* app = NULL;
    XModbusTcpServer* slave = NULL;
    XModbusPoint* diPt = NULL;
    XModbusPoint* doPt = NULL;
    XModbusPoint* aiPt = NULL;
    XModbusPoint* aoPt = NULL;
    XModbusTcpClient* client = NULL;
    XModbusHub* hub = NULL;
    XIoChannelGroup* group = NULL;
    XModbusMapping* map = NULL;
    XModbusDataUnitMap* smap = NULL;
    XModbusDataUnit* unit = NULL;
    XModbusRegisterType area = XModbusDiscreteInputs;
    uint16_t port = 0;
    int devId = -1;
    uint16_t v = 0;
    int i = 0;
    bool b = false;
    int32_t eng = -1;

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return;
    }

    /* ---- 从站：DI 0..11 + Coils 0..15 + HR 0..15（XMap 键 static 区） ---- */
    slave = XModbusTcpServer_create();
    {
        static XModbusRegisterType key0 = XModbusDiscreteInputs;
        static XModbusRegisterType key1 = XModbusCoils;
        static XModbusRegisterType key2 = XModbusHoldingRegisters;
        XVariant* a = XVariant_create_utf8_str("127.0.0.1");
        XVariant* p = XVariant_create_int(0);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)slave,
            XModbusDevice_NetworkAddressParameter, a);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)slave,
            XModbusDevice_NetworkPortParameter, p);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)p);
        smap = XModbusDataUnitMap_create();
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         key0, 0, 12);
        if (unit) { XMap_insert_base((XMapBase*)smap, &key0, unit); XClassDelete(unit); }
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         key1, 0, 16);
        if (unit) { XMap_insert_base((XMapBase*)smap, &key1, unit); XClassDelete(unit); }
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         key2, 0, 16);
        if (unit) { XMap_insert_base((XMapBase*)smap, &key2, unit); XClassDelete(unit); }
        XModbusServer_setMap_ref_base((XModbusServer*)slave, smap); /* 所有权移交。 */
        XModbusDevice_connectDevice((XModbusDevice*)slave);
        if (slave->m_tcpServer) port = XTcpServer_serverPort(slave->m_tcpServer);
        CHECK("从站监听", port != 0);
    }

    /* ---- Hub：DI/Coils/HR 三区映射（默认值 0/0x00AA 供播种断言） ---- */
    client = XModbusTcpClient_create();
    hub = XModbusHub_create();
    CHECK("Hub/客户端创建", client != NULL && hub != NULL);
    if (!client || !hub) goto cleanup;
    {
        XVariant* a = XVariant_create_utf8_str("127.0.0.1");
        XVariant* p = XVariant_create_int((int)port);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkAddressParameter, a);
        XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
            XModbusDevice_NetworkPortParameter, p);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)p);
    }
    XModbusClient_setTimeout((XModbusClient*)client, 500);
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)client, "slave1");
    map = XModbusMapping_create();
    CHECK("设备注入/映射创建", devId > 0 && map != NULL);
    if (devId <= 0 || !map) goto cleanup;
    CHECK("映射登记 DI/Coils/HR",
          XModbusMapping_addRange(map, XModbusDiscreteInputs, 0, 12, devId, 1, 0, 0) &&
          XModbusMapping_addRange(map, XModbusCoils, 0, 16, devId, 1, 0, 0x0000) &&
          XModbusMapping_addRange(map, XModbusHoldingRegisters, 0, 16, devId, 1, 0, 0x00AA));
    XModbusHub_setMapping(hub, map);
    XModbusHub_setPollInterval(hub, devId, 50);
    XModbusHub_setUnitBackoff(hub, 1000, 1000); /* 测试免退避干扰。 */

    /* ---- 通道组：XModbusPoint 数据源点 + 组登记（组无定时器，推送制；
     *      四点 autoDelete=true 交组托管，清理时先删组（点析构断开 Hub
     *      订阅，彼时 Hub 仍在）再收 Hub ---- */
    group = XIoChannelGroup_create();
    CHECK("通道组创建", group != NULL);
    if (!group) goto cleanup;
    diPt = XModbusPoint_createDigitalInput(hub, 3, "DI3");
    doPt = XModbusPoint_createDigitalOutput(hub, 0, "DO0");
    aiPt = XModbusPoint_createAnalogInput(hub, 0, true, 1, 1, 0, 0, "AI0");
    aoPt = XModbusPoint_createAnalogOutput(hub, 5, 1, 1, 0, "AO5");
    CHECK("四点创建", diPt && doPt && aiPt && aoPt);
    CHECK("四点登记",
          XIoChannelGroup_addPoint(group, (XIoPoint*)diPt, true) &&
          XIoChannelGroup_addPoint(group, (XIoPoint*)doPt, true) &&
          XIoChannelGroup_addPoint(group, (XIoPoint*)aiPt, true) &&
          XIoChannelGroup_addPoint(group, (XIoPoint*)aoPt, true));
    XObject_connect_2((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                      onGroupInputChanged);
    XObject_connect_2((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
                      onGroupWriteDone);

    /* ---- ① 联动在线：上线对齐写两段落定 + 首轮回填推送初值 ---- */
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);
    {
        int writeOk = 0;
        /* 从站 Coil/HR 初值 0 → 对齐写落定后从站侧即默认值，无独立可观测
         * 沿——以首轮 AI 回填（0x00AA）作为"对齐+轮询已活"的综合判据。 */
        g_hitTargetPt = (XIoPoint*)aiPt;
        g_hitTargetVal = 0x00AA;
        g_hitFlag = 0;
        (void)writeOk;
    }
    for (i = 0; i < 500 && !g_hitFlag; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AI 回填推送（默认值 0x00AA）", g_hitFlag);
    CHECK("AI read=0x00AA",
          XIoChannelGroup_readAnalogInput(group, (XIoPoint*)aiPt, &eng) && eng == 0x00AA);

    /* ---- ③ DI 翻转：server DI3=1 → inputChanged(diPt, 1) + read ---- */
    XModbusServer_setData2((XModbusServer*)slave, XModbusDiscreteInputs, 3, 1);
    g_inChanged = 0;
    g_hitTargetPt = (XIoPoint*)diPt;
    g_hitTargetVal = 1;
    g_hitFlag = 0;
    CHECK("DI 翻转推送", waitCond(&g_hitFlag, 1, 5000) &&
                         g_inLastPt == (XIoPoint*)diPt && g_inLastValue == 1);
    CHECK("DI read=1", XIoChannelGroup_readDigitalInput(group, (XIoPoint*)diPt, &b) && b);

    /* ---- ④ AI：server HR0=0x0777 → requestRefresh 回填推送 ---- */
    XModbusServer_setData2((XModbusServer*)slave, XModbusHoldingRegisters, 0, 0x0777);
    XModbusHub_requestRefresh(hub, XModbusHoldingRegisters, 0, 1);
    g_hitTargetPt = (XIoPoint*)aiPt;
    g_hitTargetVal = 0x0777;
    g_hitFlag = 0;
    for (i = 0; i < 500 && !g_hitFlag; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AI 刷新推送 0x0777", g_hitFlag);
    CHECK("AI read=0x0777",
          XIoChannelGroup_readAnalogInput(group, (XIoPoint*)aiPt, &eng) && eng == 0x0777);
    g_hitTargetPt = NULL;

    /* ---- ⑤ DO：write → writeDone(doPt,true) + 从站侧读回 ---- */
    CHECK("DO write 受理", XIoChannelGroup_writeDigitalOutput(group, (XIoPoint*)doPt, true));
    g_doneLastPt = NULL;
    for (i = 0; i < 500 && g_doneLastPt != (XIoPoint*)doPt; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("DO writeDone(doPt,true)", g_doneLastPt == (XIoPoint*)doPt && g_doneCount >= 1);
    CHECK("从站侧 Coil0=1",
          XModbusServer_data2((XModbusServer*)slave, XModbusCoils, 0, &v) && v == 1);

    /* ---- ⑥ AO：write(1000) → writeDone(aoPt,true) + 从站侧读回 ---- */
    CHECK("AO write 受理", XIoChannelGroup_writeAnalogOutput(group, (XIoPoint*)aoPt, 1000));
    g_doneLastPt = NULL;
    for (i = 0; i < 500 && g_doneLastPt != (XIoPoint*)aoPt; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AO writeDone(aoPt,true)", g_doneLastPt == (XIoPoint*)aoPt && g_doneCount >= 2);
    CHECK("从站侧 HR5=1000",
          XModbusServer_data2((XModbusServer*)slave, XModbusHoldingRegisters, 5, &v) && v == 1000);

    /* ---- ⑦ remove(autoDelete)：摘除即销毁、contains/recordCount、读写拒绝 ---- */
    CHECK("remove(doPt)", XIoChannelGroup_remove(group, (XIoPoint*)doPt));
    CHECK("摘除后 contains=false", !XIoChannelGroup_contains(group, (XIoPoint*)doPt));
    CHECK("摘除后记录数=3", XIoChannelGroup_recordCount(group) == 3);
    CHECK("摘除后 read 拒绝", !XIoChannelGroup_readDigitalInput(group, (XIoPoint*)doPt, &b));

cleanup:
    g_hitTargetPt = NULL;
    if (group) XClassDelete((XClass*)group);       /* autoDelete 点随组释放 */
    if (hub) XModbusHub_stopPolling(hub);
    if (hub) XModbusHub_deleteLater((XModbusHub*)hub);
    if (slave) XModbusDevice_disconnectDevice((XModbusDevice*)slave);
    if (slave) XModbusTcpServer_deleteLater(slave);
    XCoreApplication_processEvents(0);
    if (app) XClassDelete(app);
}

/* ==================== 测试入口（收编签名） ==================== */

int XIndustryModbusTest_run(void)
{
    XCoreApplication* app;
    XDeviceNetwork_ensureInit();

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    test_channel_group_hub();

    XPrintf("\n===== XIndustryModbusTest: %d passed, %d failed =====\n",
            g_pass, g_fail);
    XCoreApplication_processEvents(0);
    XClassDelete(app);
    XDeviceNetwork_cleanup();
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
