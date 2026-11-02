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
#include "XChannel/XModbusLink.h"
#include "XChannel/XChannelAssembly.h"
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
static uint16_t g_inLastRole = 0xFFFFu; /* 最近一次 inputChanged 的 role */
static int32_t g_inLastValue = -1;
static uint16_t g_doneLastRole = 0xFFFFu; /* 最近一次 writeDone(ok=true) 的 role */
static int  g_doneCount = 0;

/* 同泵粘性命中目标（见文件头 [同泵粘性命中] 注记） */
static uint16_t g_hitTargetRole = 0xFFFFu;  /* 目标 role（信号已改为携带 role） */
static int32_t g_hitTargetVal = 0;
static int g_hitFlag = 0;

static void onGroupInputChanged(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, uint16_t, role, int32_t, value);
    (void)sender;
    ++g_inChanged;
    g_inLastRole = role;
    g_inLastValue = value;
    if (role == g_hitTargetRole && value == g_hitTargetVal)
        g_hitFlag = 1;
}

static void onGroupWriteDone(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, uint16_t, role, bool, ok);
    (void)sender;
    if (ok) {
        g_doneLastRole = role;
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
    XIoPoint* diPt = NULL;
    XIoPoint* doPt = NULL;
    XIoPoint* aiPt = NULL;
    XIoPoint* aoPt = NULL;
    XIoPoint* keepPt = NULL;
    XModbusTcpClient* client = NULL;
    XModbusHub* hub = NULL;
    XIoChannelGroup* group = NULL;
    XModbusLink* link = NULL;
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
    /* [单例所有权] 本函数只借用事件循环，**不在此删除**：XCoreApplication
     * 是进程单例，create 在已有实例时原样返回 g_app
     * （XCoreApplication.c:115-116），释放统一由 _run 按 owned 判定收口。 */

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
    /* [2026-11-02 通道层重写] 点退化为纯数据容器，链路能力在 XModbusLink：
     *   role 分配 DI3=0 / DO0=1 / AI0=2 / AO5=3（本用例自定，组内作下标）。 */
    link = XModbusLink_create(hub);
    CHECK("链路创建", link != NULL);
    if (!link) goto cleanup;
    XIoChannelGroup_setLink(group, (XIoLink*)link);
    diPt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    doPt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    aiPt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    aoPt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (diPt) { XIoPoint_setRole(diPt, 0); XIoPoint_setType(diPt, XIoPoint_DigitalInput); XIoPoint_setTag(diPt, "DI3"); }
    if (doPt) { XIoPoint_setRole(doPt, 1); XIoPoint_setType(doPt, XIoPoint_DigitalOutput); XIoPoint_setTag(doPt, "DO0"); }
    if (aiPt) { XIoPoint_setRole(aiPt, 2); XIoPoint_setType(aiPt, XIoPoint_AnalogInput); XIoPoint_setTag(aiPt, "AI0"); XIoPoint_setScale(aiPt, 1, 1, 0, 0); XIoPoint_setUseHolding(aiPt, true); }
    if (aoPt) { XIoPoint_setRole(aoPt, 3); XIoPoint_setType(aoPt, XIoPoint_AnalogOutput); XIoPoint_setTag(aoPt, "AO5"); XIoPoint_setScale(aoPt, 1, 1, 0, 0); }
    CHECK("四点创建", diPt && doPt && aiPt && aoPt);
    CHECK("四点登记",
          XIoChannelGroup_addPoint(group, diPt, true) &&
          XIoChannelGroup_addPoint(group, doPt, true) &&
          XIoChannelGroup_addPoint(group, aiPt, true) &&
          XIoChannelGroup_addPoint(group, aoPt, true));
    /* 链路登记：role → 虚拟地址（区由类型决定；本用例虚拟地址=原生地址）。 */
    CHECK("链路绑定",
          XIoLink_bindRole_base((XIoLink*)link, 0, 3, 0) &&   /* DI3 → DI 区 */
          XIoLink_bindRole_base((XIoLink*)link, 1, 0, 1) &&   /* DO0 → Coils */
          XIoLink_bindRole_base((XIoLink*)link, 2, 0, 2) &&   /* AI0 → HR */
          XIoLink_bindRole_base((XIoLink*)link, 3, 5, 2));    /* AO5 → HR */
    XObject_connect_2((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                      onGroupInputChanged);
    XObject_connect_2((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
                      onGroupWriteDone);

    /* ---- ① 联动在线：上线对齐写两段落定 + 首轮回填推送初值 ---- */
    /* [2026-11-02] 链路必须先 Start：它才负责订阅 Hub 的 valueChanged/
     * writeCompleted（旧实现是每个点各自订阅，点类删除后订阅由链路统一承担）。 */
    CHECK("链路启动", XIoLink_start_base((XIoLink*)link));
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);
    {
        int writeOk = 0;
        /* 从站 Coil/HR 初值 0 → 对齐写落定后从站侧即默认值，无独立可观测
         * 沿——以首轮 AI 回填（0x00AA）作为"对齐+轮询已活"的综合判据。 */
        g_hitTargetRole = XIoPoint_role(aiPt);
        g_hitTargetVal = 0x00AA;
        g_hitFlag = 0;
        (void)writeOk;
    }
    for (i = 0; i < 500 && !g_hitFlag; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AI 回填推送（默认值 0x00AA）", g_hitFlag);
    CHECK("AI read=0x00AA",
          XIoChannelGroup_readAnalogInput(group, XIoPoint_role(aiPt), &eng) && eng == 0x00AA);

    /* ---- ③ DI 翻转：server DI3=1 → inputChanged(diPt, 1) + read ---- */
    XModbusServer_setData2((XModbusServer*)slave, XModbusDiscreteInputs, 3, 1);
    g_inChanged = 0;
    g_hitTargetRole = XIoPoint_role(diPt);
    g_hitTargetVal = 1;
    g_hitFlag = 0;
    CHECK("DI 翻转推送", waitCond(&g_hitFlag, 1, 5000) &&
                         g_inLastRole == XIoPoint_role(diPt) && g_inLastValue == 1);
    CHECK("DI read=1", XIoChannelGroup_readDigitalInput(group, XIoPoint_role(diPt), &b) && b);

    /* ---- ④ AI：server HR0=0x0777 → requestRefresh 回填推送 ---- */
    XModbusServer_setData2((XModbusServer*)slave, XModbusHoldingRegisters, 0, 0x0777);
    XModbusHub_requestRefresh(hub, XModbusHoldingRegisters, 0, 1);
    g_hitTargetRole = XIoPoint_role(aiPt);
    g_hitTargetVal = 0x0777;
    g_hitFlag = 0;
    for (i = 0; i < 500 && !g_hitFlag; ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AI 刷新推送 0x0777", g_hitFlag);
    CHECK("AI read=0x0777",
          XIoChannelGroup_readAnalogInput(group, XIoPoint_role(aiPt), &eng) && eng == 0x0777);
    g_hitTargetRole = 0xFFFFu;

    /* ---- ⑤ DO：write → writeDone(doPt,true) + 从站侧读回 ---- */
    CHECK("DO write 受理", XIoChannelGroup_writeDigitalOutput(group, XIoPoint_role(doPt), true));
    g_doneLastRole = 0xFFFFu;
    for (i = 0; i < 500 && g_doneLastRole != XIoPoint_role(doPt); ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("DO writeDone(doPt,true)", g_doneLastRole == XIoPoint_role(doPt) && g_doneCount >= 1);
    CHECK("从站侧 Coil0=1",
          XModbusServer_data2((XModbusServer*)slave, XModbusCoils, 0, &v) && v == 1);

    /* ---- ⑥ AO：write(1000) → writeDone(aoPt,true) + 从站侧读回 ---- */
    CHECK("AO write 受理", XIoChannelGroup_writeAnalogOutput(group, XIoPoint_role(aoPt), 1000));
    g_doneLastRole = 0xFFFFu;
    for (i = 0; i < 500 && g_doneLastRole != XIoPoint_role(aoPt); ++i)
        XCoreApplication_processEventsWithMaxTime(0, 10);
    CHECK("AO writeDone(aoPt,true)", g_doneLastRole == XIoPoint_role(aoPt) && g_doneCount >= 2);
    CHECK("从站侧 HR5=1000",
          XModbusServer_data2((XModbusServer*)slave, XModbusHoldingRegisters, 5, &v) && v == 1000);

    /* ---- ⑦ remove：autoDelete（摘除即销毁）与非托管（摘除后组不再承认） ---- */
    /* [ASan 根修] autoDelete 点被摘除时即销毁（XIoChannelGroup.c:114 的
       XClassDelete），**摘除后不能再拿它的指针做任何查询**——旧写法在
       remove 之后接着用 doPt 调 contains/readDigitalInput，ASan 报
       heap-use-after-free（XIoChannelGroup.c:29 groupOwns 读已释放内存）。
       「摘除后组不再承认该点」这条语义必须用**仍然存活的指针**验证，
       故另建一个非托管点做身份断言。 */
    CHECK("remove(doPt)", XIoChannelGroup_removeRole(group, XIoPoint_role(doPt)));
    doPt = NULL;                       /* autoDelete：点已随摘除销毁，不可再引用 */
    CHECK("摘除后记录数=3", XIoChannelGroup_recordCount(group) == 3);
    keepPt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (keepPt) { XIoPoint_setRole(keepPt, 4); XIoPoint_setType(keepPt, XIoPoint_DigitalOutput); XIoPoint_setTag(keepPt, "DO1"); }
    CHECK("建非托管点", keepPt != NULL);
    if (keepPt) {
        CHECK("加入组(非托管)", XIoChannelGroup_addPoint(group, keepPt, false));
        /* takePoint：摘出但**不销毁**，点仍存活 → 下面的身份断言安全。 */
        CHECK("takePoint 取出", XIoChannelGroup_takePoint(group, keepPt) == keepPt);
        CHECK("取出后 contains=false", !XIoChannelGroup_contains(group, keepPt));
        CHECK("取出后 read 拒绝", !XIoChannelGroup_readDigitalInput(group, XIoPoint_role(keepPt), &b));
        XClassDelete((XClass*)keepPt);  /* 非托管：自己释放 */
        keepPt = NULL;
    }

cleanup:
    g_hitTargetRole = 0xFFFFu;
    if (group) XClassDelete((XClass*)group);       /* autoDelete 点随组释放 */
    if (hub) XModbusHub_stopPolling(hub);
    if (hub) XModbusHub_deleteLater((XModbusHub*)hub);
    if (slave) XModbusDevice_disconnectDevice((XModbusDevice*)slave);
    if (slave) XModbusTcpServer_deleteLater(slave);
    XCoreApplication_processEvents(0);
    (void)app;   /* 单例不在此释放（见上方所有权说明）。 */
}

/* ==================== 测试入口（收编签名） ==================== */

int XIndustryModbusTest_run(void)
{
    XCoreApplication* app;
    bool owned;
    XDeviceNetwork_ensureInit();

    /* [单例所有权] create 在已有实例时原样返回 g_app（XCoreApplication.c:115-116）。
     * --test 路径下 main 已建实例并负责释放（main.c:376），此处若无条件删除
     * 会二次释放同一对象——实测退出段错误，栈为
     * XClass_deinit_base ← XClass_delete_base ← main+0x4ac。
     * 故只在本次真正创建了实例时才释放。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    test_channel_group_hub();

    XPrintf("\n===== XIndustryModbusTest: %d passed, %d failed =====\n",
            g_pass, g_fail);
    XCoreApplication_processEvents(0);
    if (owned) XClassDelete(app);
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
