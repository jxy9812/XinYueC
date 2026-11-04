/**
 * @file       hsf_server_io_flowmeter_test.c
 * @brief      服务端电磁流量计站（HsfServerIoFlowMeter）隔离单测。
 * @details    [为什么要这个测试] 站模块此前只随 add_executable(HsfServer)
 *             编译、端到端手测才覆盖，Test/ 侧零单测。本文件把它拉进自动化。
 *
 *             [为什么用真 TCP 从站而不是"往 Hub 缓存塞值"]
 *             站模块读的是 **InputRegisters**（LD 的变量区就是 FC04），而
 *             XModbusHub_writeValue 明确只接受 Coils/HoldingRegisters
 *             （"if (type != XModbusCoils && type != XModbusHoldingRegisters)
 *             return false;"）——读区只读，塞不进去。首版测试踩的就是这个坑
 *             （write-hit=1 但读回恒 0，所有拼装断言全红）。
 *             所以这里起一个进程内 TCP 从站，让 Hub 真轮询 FC04 回填。
 *
 *             [环回停滞不影响本测试] 本机 Windows 环回在突发后会有秒级完成
 *             停滞（verify/windows_loopback_stall_repro.c 可复现），但本用例
 *             全程只几十毫秒、帧量极小，实测稳定通过。
 *
 *             [可测/不可测边界]
 *               - 装配契约（mount/幂等/空指针/总线未就绪）→ 可测；
 *               - 采集链路（FC04 回填 → 22 role → 设备层拼装）→ 可测；
 *               - 长稳/断线重连/多客户端并发 → 不在本文件（属端到端，
 *                 见 verify/verify_hsf_flowmeter.py）。
 */
#include "HsfServerIoFlowMeter.h"
#include "HsfServerBus.h"
#include "HsfServerConfig.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusTcpClient.h"
#include "XModbusTcpServer.h"
#include "XModbusServer.h"
#include "XModbusDevice.h"
#include "XModbusDevice_Protected.h"
#include "XModbusDataUnit.h"
#include "XTcpServer.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XFlowMeter.h"
#include "XObject.h"
#include "XVariant.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>

#if XINDUSTRY_ON && XFLOW_METER_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/** @brief 事件泵小睡（Hub 轮询/信号派发驱动）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/**
 * @brief 测试夹具：进程内 TCP 从站（IR 区）+ Hub + 站模块。
 */
typedef struct FmFixture {
    XModbusTcpServer* m_slave;
    XModbusTcpClient* m_client;
    XModbusHub* m_hub;
    HsfServerBus m_bus;             /**< 借用包装（只填 hub/devId 两字段）。 */
    HsfServerIoFlowMeter m_station;
    /* 不再缓存 DataUnit 指针：XMap_insert_base 是**拷贝**语义（参考
     * xindustry_modbus_test.c 也是 insert 后立刻 delete 源对象），缓存
     * 原指针会悬垂、写进去无声无息。改为每次从从站 m_dataMap 取现用那份。 */
    uint16_t m_port;
    int m_devId;
} FmFixture;

/**
 * @brief 建夹具：起从站（IR 0..31 共 32 字）、连 Hub。
 * @return 全部建成返回 true。
 */
static bool fxInit(FmFixture* fx)
{
    static XModbusRegisterType keyIR = XModbusInputRegisters;
    XVariant* a;
    XVariant* p;
    XModbusDataUnitMap* smap;
    XModbusDataUnit* unit;

    memset(fx, 0, sizeof(*fx));

    /* ---- 从站：只开输入寄存器区（LD 变量区走 FC04） ---- */
    fx->m_slave = XModbusTcpServer_create();
    if (!fx->m_slave) return false;
    a = XVariant_create_utf8_str("127.0.0.1");
    p = XVariant_create_int(0);
    XModbusDevice_setConnectionParameter((XModbusDevice*)fx->m_slave,
        XModbusDevice_NetworkAddressParameter, a);
    XModbusDevice_setConnectionParameter((XModbusDevice*)fx->m_slave,
        XModbusDevice_NetworkPortParameter, p);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)p);

    smap = XModbusDataUnitMap_create();
    /* IR 区起点必须与站模块映射的**远端起点**一致（0x1010）；
     * 从 0 起只开 32 字会让 Hub 读 0x1010 越界→从站回异常码→永不回填
     * （首版实锅：装配全绿但采集全红）。 */
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, keyIR,
                                     0x1010, 22);
    if (unit) {
        XMap_insert_base((XMapBase*)smap, &keyIR, unit);
        XClassDelete(unit);         /* 拷贝语义：从站持有副本 */
    }
    XModbusServer_setMap_ref_base((XModbusServer*)fx->m_slave, smap);
    XModbusDevice_connectDevice((XModbusDevice*)fx->m_slave);
    if (!fx->m_slave->m_tcpServer) return false;
    fx->m_port = XTcpServer_serverPort(fx->m_slave->m_tcpServer);
    if (fx->m_port == 0) return false;

    /* ---- Hub + 客户端 ---- */
    fx->m_client = XModbusTcpClient_create();
    fx->m_hub = XModbusHub_create();
    if (!fx->m_client || !fx->m_hub) return false;
    {
        XVariant* aa = XVariant_create_utf8_str("127.0.0.1");
        XVariant* pp = XVariant_create_int((int)fx->m_port);
        XModbusDevice_setConnectionParameter((XModbusDevice*)fx->m_client,
            XModbusDevice_NetworkAddressParameter, aa);
        XModbusDevice_setConnectionParameter((XModbusDevice*)fx->m_client,
            XModbusDevice_NetworkPortParameter, pp);
        XClassDelete((XClass*)aa);
        XClassDelete((XClass*)pp);
    }
    XModbusClient_setTimeout((XModbusClient*)fx->m_client, 500);
    fx->m_devId = XModbusHub_addDevice_2(fx->m_hub,
                                         (XModbusClient*)fx->m_client, "slave");
    if (fx->m_devId <= 0) return false;
    XModbusHub_setUnitBackoff(fx->m_hub, 1000, 1000);

    memset(&fx->m_bus, 0, sizeof(fx->m_bus));
    fx->m_bus.m_hub = fx->m_hub;    /* 站模块只经 HsfServerBus_hub 取 hub */
    fx->m_bus.m_devId = fx->m_devId;

    HsfServerIoFlowMeter_init(&fx->m_station);
    return true;
}

/**
 * @brief 拆夹具（顺序：站 → Hub → 从站）。
 * @note 客户端所有权在 Hub（addDevice 转移），只清指针不再释放
 *       —— 首版显式再删 client 会双重释放（实测在 fxDeinit 里崩）。
 */
static void fxDeinit(FmFixture* fx)
{
    if (!fx) return;
    XClassDeinit((XClass*)&fx->m_station);   /* deinit 内部已 unmount */
    if (fx->m_hub) { XClassDelete((XClass*)fx->m_hub); fx->m_hub = NULL; }
    fx->m_client = NULL;
    if (fx->m_slave) { XClassDelete((XClass*)fx->m_slave); fx->m_slave = NULL; }
}

/**
 * @brief 通过从站写一段输入寄存器（从站侧自持 DataUnit，Hub 轮询读回）。
 * @param remoteStart 远端起始地址（须等于 unit 的 m_startAddress，0x1010）。
 * @param regs/count 数据。
 * @note  XModbusDataUnit_setValue 的 index 是**相对偏移**（0..size-1），
 *        不是绝对协议地址 —— 传 0x1010 会越界静默失败（首版实锅：
 *        devState=2 已连上、缓存命中但值恒 0）。
 */
static void fxSetRegs(FmFixture* fx, uint16_t remoteStart,
                      const uint16_t* regs, int count)
{
    XModbusDataUnit* unit;
    XModbusRegisterType key = XModbusInputRegisters;
    XModbusServer* srv;
    int base;
    int i;

    if (!fx || !fx->m_slave) return;
    srv = (XModbusServer*)fx->m_slave;      /* m_dataMap 在基类 */
    if (!srv->m_dataMap) return;
    /* 取从站 m_dataMap 里现用的那份 IR unit（insert 是拷贝语义）。 */
    unit = (XModbusDataUnit*)XMapBase_value_base(
        (XMapBase*)srv->m_dataMap, (const void*)&key);
    if (!unit) return;
    base = (int)remoteStart - XModbusDataUnit_startAddress(unit);
    if (base < 0) base = 0;
    for (i = 0; i < count; ++i)
        XModbusDataUnit_setValue(unit, (size_t)(base + i), regs[i]);
}

/* ==================== 用例 ==================== */

/**
 * @brief 装配契约：mount/幂等/查询/空指针/总线未就绪。
 */
static void test_mount(void)
{
    FmFixture fx;
    XModbusMapping* map;
    const int vbase = HSF_FLOWMETER_VADDR;

    XPrintf("---- 装配契约 ----\n");
    if (!fxInit(&fx)) { CHECK("夹具创建", false); return; }

    map = XModbusMapping_create();
    CHECK("未挂接时 isMounted=false", !HsfServerIoFlowMeter_isMounted(&fx.m_station));
    CHECK("mount 成功",
          HsfServerIoFlowMeter_mount(&fx.m_station, &fx.m_bus, map,
                                     HSF_FLOWMETER_UNIT_ID, (uint16_t)vbase,
                                     HSF_FLOWMETER_STALE_MS));
    CHECK("mount 后 isMounted=true", HsfServerIoFlowMeter_isMounted(&fx.m_station));
    CHECK("设备对象可查", HsfServerIoFlowMeter_device(&fx.m_station) != NULL);
    CHECK("重复 mount 被拒",
          !HsfServerIoFlowMeter_mount(&fx.m_station, &fx.m_bus, map,
                                      HSF_FLOWMETER_UNIT_ID, (uint16_t)vbase,
                                      HSF_FLOWMETER_STALE_MS));
    HsfServerIoFlowMeter_unmount(&fx.m_station);   /* void：幂等 */
    CHECK("unmount 后 isMounted=false", !HsfServerIoFlowMeter_isMounted(&fx.m_station));
    HsfServerIoFlowMeter_unmount(&fx.m_station);   /* 二次调用不崩即幂等 */
    CHECK("unmount 幂等（二次调用不崩）", !HsfServerIoFlowMeter_isMounted(&fx.m_station));

    XClassDelete((XClass*)map);
    fxDeinit(&fx);
}

/**
 * @brief 空指针与参数校验。
 */
static void test_nullSafety(void)
{
    XPrintf("---- 空指针/参数校验 ----\n");
    CHECK("values(NULL) 返回 false",
          !HsfServerIoFlowMeter_values(NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL));
    CHECK("isMounted(NULL) 返回 false", !HsfServerIoFlowMeter_isMounted(NULL));
    CHECK("device(NULL) 返回 NULL", HsfServerIoFlowMeter_device(NULL) == NULL);
    CHECK("unmount(NULL) 不崩", (HsfServerIoFlowMeter_unmount(NULL), true));
    CHECK("sampleNow(NULL) 返回 false", !HsfServerIoFlowMeter_sampleNow(NULL));
    CHECK("mount(NULL,...) 返回 false",
          !HsfServerIoFlowMeter_mount(NULL, NULL, NULL, 1, 0, 0));

    {
        HsfServerIoFlowMeter st;
        HsfServerBus dead;
        HsfServerIoFlowMeter_init(&st);
        memset(&dead, 0, sizeof(dead));
        dead.m_devId = -1;
        CHECK("总线未就绪时 mount 失败",
              !HsfServerIoFlowMeter_mount(&st, &dead, NULL,
                                          HSF_FLOWMETER_UNIT_ID,
                                          HSF_FLOWMETER_VADDR,
                                          HSF_FLOWMETER_STALE_MS));
        XClassDeinit((XClass*)&st);
    }
}

/**
 * @brief 采集链路：FC04 轮询回填 → 22 role → 设备层 32 位拼装。
 * @details 用协议文档的真实示例帧做输入，验证整条链没把字或顺序搞错。
 */
static void test_collectAndAssemble(void)
{
    FmFixture fx;
    XModbusMapping* map;
    XFlowMeter* dev;
    const int vbase = HSF_FLOWMETER_VADDR;
    int waited;
    double v = 0.0;

    XPrintf("---- 采集链路（FC04 -> 22 role -> 拼装）----\n");
    if (!fxInit(&fx)) { CHECK("夹具创建", false); return; }

    /* 从站侧写入 22 个寄存器（远端 0x1010 起）。 */
    {
        uint16_t regs[22] = {
            0xC41C, 0x6000,   /* 瞬时流量     -625.5    */
            0xC1B0, 0x8000,   /* 瞬时流速     -22.0625  */
            0x4264, 0x0000,   /* 百分比        57.0     */
            0x0000, 0x0000,   /* 电导比        0        */
            0x0123, 0x4567,   /* 正向累积整数  19088743 */
            0x0000, 0x0000,   /* 正向累积小数  0        */
            0x0000, 0x0000,   /* 反向累积整数  0        */
            0x0000, 0x0000,   /* 反向累积小数  0        */
            0x0005,           /* 流量单位 M3/H */
            0x0001,           /* 累积单位 M3   */
            0x0000, 0x0000,   /* 上限/下限     */
            0x0001, 0x0000    /* 空管 / 系统   */
        };
        fxSetRegs(&fx, 0x1010, regs, 22);
    }

    map = XModbusMapping_create();
    CHECK("挂接成功",
          HsfServerIoFlowMeter_mount(&fx.m_station, &fx.m_bus, map,
                                     HSF_FLOWMETER_UNIT_ID, (uint16_t)vbase,
                                     HSF_FLOWMETER_STALE_MS));
    XModbusHub_setMapping(fx.m_hub, map);   /* void：所有权移交 Hub */
    CHECK("轮询周期设置", XModbusHub_setPollInterval(fx.m_hub, fx.m_devId, 30));
    XModbusHub_setPollAreas(fx.m_hub, 1u << 3);   /* 只轮询 IR 区 */
    /* [关键] 必须先 connectAll 再 startPolling —— 服务端是在
     * HsfServerBus_start 里做这两步的（connectAll → startPolling）。
     * 漏掉 connectAll 时设备停在 UnconnectedState，轮询不发帧，
     * 缓存永远是映射默认值 0（首版实锅：devState=0 且 reg0 恒 0）。 */
    CHECK("connectAll 成功", XModbusHub_connectAll(fx.m_hub));
    XModbusHub_startPolling(fx.m_hub);

    /* 等首帧回填。 */
    for (waited = 0; waited < 3000; waited += 20) {
        uint16_t raw = 0;
        if (XModbusHub_value(fx.m_hub, XModbusInputRegisters,
                             (uint16_t)vbase, &raw) && raw == 0xC41C)
            break;
        spinMs(20);
    }
    CHECK("Hub 已回填首帧（reg0 == 0xC41C）", waited < 3000);
    CHECK("sampleNow 成功", HsfServerIoFlowMeter_sampleNow(&fx.m_station));
    dev = HsfServerIoFlowMeter_device(&fx.m_station);
    CHECK("设备对象非空", dev != NULL);
    if (!dev) {
        XModbusHub_stopPolling(fx.m_hub);
        HsfServerIoFlowMeter_unmount(&fx.m_station);
        fxDeinit(&fx);
        return;
    }

    CHECK("瞬时流量 = -625.5（C41C6000）",
          XFlowMeter_value(dev, XFlowMeterQuantity_InstantFlow, &v) &&
          v < -625.49 && v > -625.51);
    CHECK("瞬时流速 = -22.0625（C1B08000）",
          XFlowMeter_value(dev, XFlowMeterQuantity_InstantSpeed, &v) &&
          v < -22.06 && v > -22.07);
    CHECK("流量百分比 = 57.0（42640000）",
          XFlowMeter_value(dev, XFlowMeterQuantity_FlowPercent, &v) &&
          v > 56.99 && v < 57.01);
    CHECK("正向累积 = 19088743（01234567）",
          XFlowMeter_value(dev, XFlowMeterQuantity_ForwardTotal, &v) &&
          v > 19088742.9 && v < 19088743.1);

    CHECK("瞬时流量单位 = M3/H",
          XFlowMeter_flowUnit(dev) == XFlowMeterFlowUnit_M3H);
    CHECK("累积总量单位 = M3",
          XFlowMeter_totalUnit(dev) == XFlowMeterTotalUnit_M3);
    CHECK("空管报警位已置", (XFlowMeter_alarms(dev) & 0x04) != 0);
    CHECK("仅空管位", XFlowMeter_alarms(dev) == 0x04);

    /* 站层质量：真读回后应 valid=true（端到端手测覆盖不到这个口径）。 */
    {
        double flow = 0.0;
        bool valid = false;
        bool has = HsfServerIoFlowMeter_values(&fx.m_station, &flow, NULL,
                                               NULL, NULL, NULL, NULL,
                                               NULL, &valid);
        CHECK("真读回后站层 valid=true", valid);
        CHECK("站层取到的流量 = -625.5（与设备层一致）",
              flow < -625.49 && flow > -625.51);
        (void)has;
    }

    /* 改一帧：验证能更新而非粘死。 */
    {
        uint16_t zero[2] = { 0x0000, 0x0000 };
        fxSetRegs(&fx, 0x1010, zero, 2);
        for (waited = 0; waited < 2000; waited += 20) {
            uint16_t raw = 0;
            if (XModbusHub_value(fx.m_hub, XModbusInputRegisters,
                                 (uint16_t)vbase, &raw) && raw == 0x0000)
                break;
            spinMs(20);
        }
        HsfServerIoFlowMeter_sampleNow(&fx.m_station);
        CHECK("改一帧后瞬时流量 = 0",
              XFlowMeter_value(dev, XFlowMeterQuantity_InstantFlow, &v) && v == 0.0);
    }

    XModbusHub_stopPolling(fx.m_hub);
    HsfServerIoFlowMeter_unmount(&fx.m_station);
    fxDeinit(&fx);
}

/* ==================== 运行器 ==================== */

static void runAll(void)
{
    test_mount();
    test_nullSafety();
    test_collectAndAssemble();
}

int XHsfServerIoFlowMeterTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    runAll();
    XPrintf("\n===== XHsfServerIoFlowMeterTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete((XClass*)app);
    return g_fail == 0 ? 0 : 1;
}

void XHsfServerIoFlowMeterTest(XVariant* data)
{
    const int fails = XHsfServerIoFlowMeterTest_run();
    (void)data;
    XPrintf("=== XHsfServerIoFlowMeterTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else

#include "XPrintf.h"
int XHsfServerIoFlowMeterTest_run(void)
{
    XPrintf("XHsfServerIoFlowMeterTest: XINDUSTRY/XFLOW_METER 未启用，跳过\n");
    return 0;
}

#endif
