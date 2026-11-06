/**
 * @file       hsf_sim_mode_test.c
 * @brief      服务端「模拟设备模式」全链路回归（HsfServerIo --sim 的锁）。
 *
 * @details 回归的是 2026-11-05 模拟设备接入战役里四个真实缺陷，它们全都
 *          "模拟联调看着在跑、数据却是错的"，靠人工看日志发现不了：
 *
 *          ① **多从站必须按站号分派模型**。路 A 是变频器(站2) + 温湿度(站4)
 *             共用一条 485。进程内应答没有物理总线做仲裁，若虚拟客户端只持
 *             一个模型，两个站会读到同一份寄存器（变频器读到温度字），且
 *             不报任何错误。本用例断言两站各自拿到自己的数据。
 *
 *          ② **VFD 模型必须应答整段内的未建模地址**。主站按通信组整块读
 *             （0x1000 段读 9 个），模型若对段内未显式建模的地址回 EXC02，
 *             整个读组失败 ⇒ 段内本来正常的点（输出频率/母线电压/电流）
 *             全部拿不到，遥测有效位只剩 5 个。
 *
 *          ③ **静止量的心跳拍必须用 feedRoleRefresh**。房间温湿度、管道流量
 *             长期恒定，feedRole 因"值未变"静默 ⇒ 设备层「最近一拍」停住
 *             ⇒ 超过 staleMs 被判陈旧。实测流量计 973/1031 次取值为 0
 *             （Hub 里原始字完全正确）。
 *
 *          ④ **32 位量的低字不能是 0**。12.5f = 0x41480000，低字恰为 0，
 *             与点初值相同 ⇒ 该字永远不被喂 ⇒ 设备层整拍重算时缺低字。
 *             模型缺省值改用 12.53f（0x41487AE1）。
 *
 *          另有阀控端到端：写 DO 经虚拟数字量模型联动行程开关，阀状态
 *          由"已关"变"已开"再变回"已关"——这是"模拟联调等于真联调"的
 *          最后一块拼图（否则只能证明数据能读，证明不了命令能到）。
 *
 * @note 全部走**生产装配路径**（HsfServerBus_openWithClient /
 *       HsfServerIoDigital_mountWithClients / HsfServerIoFlowMeter_mount …），
 *       不另写一套测试专用装配——否则"测试通过、现场装配不同"就是必然。
 *
 * 用法：--test hsf-sim-mode。退出码 = 失败断言数。
 */
#include <stdio.h>
#include <string.h>

#include "CXinYueConfig.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"

#include "HsfServerBus.h"
#include "HsfServerConfig.h"
#include "HsfServerIoValve.h"
#include "HsfServerIoDigital.h"
#include "HsfServerIoFlowMeter.h"
#include "HsfServerIoTempHum.h"
#include "HsfServerIoVfd.h"
#include "HsfServerIoSim.h"

#include "XModbusHub.h"
#include "XModbusVirtualClient.h"
#include "XModbusVirtualModel.h"
#include "XVfdDevice.h"

static int s_failures = 0;

static void check(const char* what, int ok, const char* detail)
{
    if (ok) {
        XPrintf("[PASS] %s\n", what);
    } else {
        XPrintf("[FAIL] %s%s%s\n", what, detail ? " — " : "",
                detail ? detail : "");
        ++s_failures;
    }
}

/** @brief 驱动事件循环 ms 毫秒（让 Hub 轮询把缓存填起来）。 */
static void spinMs(int ms)
{
    const int64_t until = XDateTime_currentMSecsSinceEpoch() + ms;
    while (XDateTime_currentMSecsSinceEpoch() < until) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
}

/* ============================================================================
 * ① 多从站按站号分派：变频器(2) 与温湿度(4) 共用一条虚拟 485
 * ============================================================================ */
static void testMultiUnitDispatch(void)
{
    HsfServerBus bus;
    HsfServerIoVfd vfd;
    HsfServerIoTempHum th;
    XModbusMapping* map;
    const XVfdDevice* dev;
    const XVfdStatus* st;
    int32_t temp = 0;
    int32_t hum = 0;
    bool tv = false;
    bool hv = false;

    XPrintf("---- ① 多从站按站号分派（变频器站2 + 温湿度站4 共用一条线）----\n");

    HsfServerIoVfd_init(&vfd);
    HsfServerIoTempHum_init(&th);

    /* 生产同款：注入虚拟客户端，两站各绑一个模型。 */
    {
        XModbusVirtualClient* c = HsfServerIoSim_createBusClient(true, true, false,
                                                                 false);
        if (!c) {
            check("路 A 虚拟客户端创建", 0, "createBusClient 返回 NULL");
            return;
        }
        if (!HsfServerBus_openWithClient(&bus, "sim-485-A", (XModbusClient*)c,
                                        HSF_BUS_A_TIMEOUT_MS,
                                        HSF_BUS_A_RETRIES)) {
            check("路 A 注入客户端装配", 0, "openWithClient 失败");
            return;
        }
    }
    check("路 A 注入客户端装配", 1, NULL);

    map = HsfServerBus_createMapping(&bus);
    if (!map) {
        check("映射表创建", 0, NULL);
        HsfServerBus_stop(&bus);
        return;
    }
    check("变频器站装配",
          HsfServerIoVfd_mount(&vfd, &bus, map, HSF_VFD_UNIT_ID,
                               HSF_VFD_ACTION_MS, HSF_VFD_STATUS_MS), NULL);
    check("温湿度站装配",
          HsfServerIoTempHum_mount(&th, &bus, map, HSF_TEMPHUM_UNIT_ID,
                                   HSF_TEMPHUM_VADDR, HSF_TEMPHUM_STALE_MS),
          NULL);
    check("总线启动（提交映射 + 轮询）",
          HsfServerBus_start(&bus, map, HSF_BUS_A_SCAN_MS), NULL);

    /* 轮询若干拍：Hub 填缓存 → 各站订阅的 valueChanged 得到触发。 */
    spinMs(1200);

    /* 变频器：状态快照必须有效（模型给 5532 = 553.2V 母线电压，Python 口径）。 */
    dev = (const XVfdDevice*)HsfServerIoVfd_device(&vfd);
    st = dev ? XVfdDevice_status_const(dev) : NULL;
    check("变频器遥测有效（按站号 2 读到自己的模型）",
          st && st->m_valid, NULL);
    /* 母线电压是最能区分"读到变频器 vs 读到温湿度"的量：
     * 温湿度模型在 0x1002 上没有分配（地址空间只到 0x0001）。 */
    check("变频器母线电压 ≈ 553.2V（未串到温湿度模型）",
          st && st->m_valid && st->m_busVoltageV >= 548.0 &&
          st->m_busVoltageV <= 558.0, NULL);

    /* 温湿度：模型缺省 265/550（26.5℃ / 55.0%）。 */
    check("温湿度取值",
          HsfServerIoTempHum_values(&th, &temp, &hum, &tv, &hv) && tv && hv,
          NULL);
    check("温度 = 26.5℃（按站号 4 读到自己的模型）",
          tv && temp == 265, NULL);
    check("湿度 = 55.0%", hv && hum == 550, NULL);

    /* 关键反证：两站的数据必须不同（同一个模型时这两条会相等）。 */
    check("两站数据不串（母线电压 ≠ 温度）",
          !(st && st->m_valid && tv &&
            (int)st->m_busVoltageV == temp), NULL);

    HsfServerIoVfd_unmount(&vfd);
    HsfServerIoTempHum_unmount(&th);
    HsfServerBus_stop(&bus);
}

/* ============================================================================
 * ② VFD 模型整段应答：0x1000 段读 9 个必须整体成功
 * ============================================================================ */
static void testVfdSegmentRead(void)
{
    struct XVirtualModelVfd* m = XVirtualModelVfd_create(50.0);
    uint16_t out[16];
    uint16_t n = 0;
    XModbusVirtualResult r;

    XPrintf("---- ② VFD 模型整段读（0x1000 × 9）----\n");
    if (!m) {
        check("VFD 模型创建", 0, NULL);
        return;
    }
    /* 0x1000~0x1008 是主站的一个通信组（见 HsfServerIoVfd 装配日志）。
     * 其中 0x1007/0x1008（DI/DO 位图）模型未显式建模，但它落在段内——
     * 回 EXC02 会让整个读组失败，段内其它点一起拿不到。 */
    r = XModbusVirtualModel_read(XVirtualModel_base(m), 0x03, 0x1000, 9, out, 16, &n);
    check("0x1000 段整块读被受理（段内未建模地址不报异常）",
          r == XModbusVirtual_Ok && n == 9, NULL);
    check("段内输出频率字可读（0x1001 有效）", n >= 2, NULL);
    /* [口径对齐 Python] 母线电压真机稳定 553.2V（0.1V ⇒ 5532），不再是
     * 旧模型的 540V 发明值。 */
    check("母线电压 = 5532（0.1V，553.2V 真机口径）",
          n >= 3 && out[2] == 5532, NULL);

    /* 段落之外仍必须是 EXC02（真从站对未分配地址就是异常应答）。 */
    r = XModbusVirtualModel_read(XVirtualModel_base(m), 0x03, 0x9000, 1, out, 16, &n);
    check("段外地址回非法地址（EXC02）",
          r == XModbusVirtual_IllegalAddress, NULL);

    XFree_System(m);
}

/* ============================================================================
 * ③ 静止量心跳：流量计必须持续有效（feedRoleRefresh）
 * ============================================================================ */
static void testFlowMeterHeartbeat(void)
{
    HsfServerBus bus;
    HsfServerIoFlowMeter fm;
    XModbusMapping* map;
    double flow = 0.0;
    int valid0 = 0;
    int validN = 0;
    int i;

    XPrintf("---- ③ 静止量心跳（流量计持续有效）----\n");

    HsfServerIoFlowMeter_init(&fm);
    {
        XModbusVirtualClient* c = HsfServerIoSim_createBusClient(false, false, true,
                                                                 false);
        if (!c || !HsfServerBus_openWithClient(&bus, "sim-485-B",
                                              (XModbusClient*)c,
                                              HSF_BUS_B_TIMEOUT_MS,
                                              HSF_BUS_B_RETRIES)) {
            check("路 B 注入客户端装配", 0, NULL);
            return;
        }
    }
    check("路 B 注入客户端装配", 1, NULL);

    map = HsfServerBus_createMapping(&bus);
    check("流量计站装配",
          map && HsfServerIoFlowMeter_mount(&fm, &bus, map,
                                            HSF_FLOWMETER_UNIT_ID,
                                            HSF_FLOWMETER_VADDR,
                                            HSF_FLOWMETER_STALE_MS), NULL);
    check("总线启动", HsfServerBus_start(&bus, map, HSF_BUS_B_SCAN_MS), NULL);

    spinMs(800);
    /* 首拍：模型缺省 36.5 m3/h ±2% 正弦波动（Python VirtualFlowMeter 口径）。
     * [不能再用恒 12.53] 恒值是旧口径；波动使 32 位量的两字每拍都被喂，
     * "低字恰为 0 不被喂"的旧坑随波动消失。 */
    {
        bool ok = HsfServerIoFlowMeter_values(&fm, &flow, NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL);
        check("首拍流量有效", ok, NULL);
        check("瞬时流量 = 36.5±2%（波动基线，Python 同口径）",
              ok && flow > 35.7 && flow < 37.3, NULL);
        valid0 = ok ? 1 : 0;
    }

    /* [核心断言] 跨过 staleMs 后仍必须有效：静止量的"拍"靠采样定时器刷新。
     * [窗口必须 > staleMs] 缺陷（用 feedRole 而非 feedRoleRefresh）的表现是
     * "最后一次值变化超过 staleMs 之后才判陈旧"——窗口短于 staleMs 时缺陷
     * 也会通过（实测 2.4s 窗口 A/B 不红）。这里连采 14 次 × 500ms ≈ 7s，
     * 必然跨过 HSF_FLOWMETER_STALE_MS(5000) 阈值。 */
    for (i = 0; i < 14; ++i) {
        bool ok;
        spinMs(500);
        ok = HsfServerIoFlowMeter_values(&fm, &flow, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL);
        if (ok && flow > 35.7 && flow < 37.3) ++validN;
    }
    {
        char detail[96];
        snprintf(detail, sizeof(detail),
                 "14 次采样中 %d 次有效（窗口 7s > staleMs 5s）", validN);
        check("静止量跨过 staleMs 仍持续有效（心跳拍未被静默吞掉）",
              validN == 14, detail);
    }
    (void)valid0;

    HsfServerIoFlowMeter_unmount(&fm);
    HsfServerBus_stop(&bus);
}

/* ============================================================================
 * ④ 16 点数字量（单模块 .13）+ 阀控端到端
 * ============================================================================ */
static void testValveEndToEnd(void)
{
    HsfServerIoDigital dig;
    HsfServerIoValve bank;
    HsfServerIoValveCfg cfg[HSF_VALVE_COUNT];
    const HsfServerIoValveItem* it;
    uint32_t diMask = 0;
    bool diValid = false;
    int i;

    XPrintf("---- ④ 16 点数字量（单模块 .13）+ 阀控端到端 ----\n");

    HsfServerIoDigital_init(&dig);
    {
        /* [2026-11-06 现场变更] .12 拆除 ⇒ 只剩 .13 一台 16 点模块，
         * 阀在 DO0..5 / 关到位 DI0 / 开到位 DI1（HSF_VALVE_* 宏）。 */
        XModbusVirtualClient* vmain = HsfServerIoSim_createDigitalMain();
        if (!vmain ||
            !HsfServerIoDigital_mountWithClients(
                &dig, (XModbusClient*)vmain, NULL,
                HSF_DIG_CHANNELS, HSF_DIG_START_ADDR,
                HSF_DIG_POLL_MS, HSF_DIG_TIMEOUT_MS)) {
            check("数字量模拟客户端创建", 0, NULL);
            return;
        }
    }
    check("数字量站按 16 点装配（单模块 .13）",
          HsfServerIoDigital_channelCount(&dig) == HSF_DIG_CHANNELS, NULL);

    /* 阀 1：DO=0（模块本地点 0），开到位 DI=1、关到位 DI=0。 */
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        cfg[i].m_name = NULL;
        cfg[i].m_do = (i == 0) ? HSF_VALVE_DO(0) : -1;
        cfg[i].m_openDi = (i == 0) ? HSF_VALVE_OPEN_DI(0) : -1;
        cfg[i].m_closeDi = (i == 0) ? HSF_VALVE_CLOSE_DI(0) : -1;
    }
    check("阀组点位与配置宏一致（DO0/开DI1/关DI0）",
          cfg[0].m_do == HSF_VALVE_DO(0) &&
          cfg[0].m_openDi == HSF_VALVE_OPEN_DI(0) &&
          cfg[0].m_closeDi == HSF_VALVE_CLOSE_DI(0),
          NULL);
    HsfServerIoValve_init(&bank);
    check("阀组装配", HsfServerIoValve_configure(&bank, cfg, HSF_VALVE_COUNT,
                                                2000, true), NULL);

    spinMs(500);

    /* 首拍判读：模型开机稳态为六路阀全部"已关"（行程推导 ⇒ 关到位 DI
     * 吸合，Python 同口径），DI0（阀 1 关到位）有效 ⇒ 已关。 */
    HsfServerIoDigital_readDi(&dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < HSF_DIG_CHANNELS; ++i)
            if (HsfServerIoDigital_isDiFresh(&dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    check("初始为已关（关到位吸合）", it && it->m_state == HsfValveState_Closed,
          NULL);
    check("阀反馈模块在线（16 点装配后本区新鲜）", it && it->m_online, NULL);

    /* 开阀：写 DO0=1 ⇒ 模型进入行程（缺省 10s）⇒ 先是「动作中」，
     * 10s 后到位才变「已开」。
     *
     * [这两条断言是模拟设备的关键价值] 立即联动的模型下"命令"与"到位"
     * 是同一瞬间，「动作中」与「超时告警」两条分支永远不会被跑到，
     * 模拟联调会给人"超时逻辑已验证"的错觉。 */
    check("开阀受理", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0), NULL);
    check("写 DO0 被受理",
          HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), true), NULL);
    spinMs(600);      /* 远小于 10s 行程 */
    diMask = 0;
    HsfServerIoDigital_readDi(&dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < HSF_DIG_CHANNELS; ++i)
            if (HsfServerIoDigital_isDiFresh(&dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    check("行程中状态 = 动作中（两个到位都不吸）",
          it && it->m_state == HsfValveState_Moving, NULL);
    check("行程中两个到位反馈都是 0", it && !it->m_openFb && !it->m_closeFb,
          NULL);

    /* 等行程走完（缺省 10s）⇒ 到位变「已开」。 */
    spinMs(10200);
    diMask = 0;
    HsfServerIoDigital_readDi(&dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < HSF_DIG_CHANNELS; ++i)
            if (HsfServerIoDigital_isDiFresh(&dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    check("行程走完后状态 = 已开", it && it->m_state == HsfValveState_Open, NULL);
    check("开到位反馈有效", it && it->m_openFb && !it->m_closeFb, NULL);

    /* 关回去（安全：模拟模式只写内存模型，绝不驱动真实继电器）。
     * 为了不让本用例再等 10s，把行程调短。 */
    XVirtualModelDigital_setMoveMs(
        (struct XVirtualModelDigital*)HsfServerIoDigital_simModel(&dig), 300u);
    check("关阀受理", HsfServerIoValve_setOutput(&bank, 0, false, NULL, 0), NULL);
    check("写 DO0=0 被受理",
          HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), false), NULL);
    spinMs(700);
    diMask = 0;
    HsfServerIoDigital_readDi(&dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < HSF_DIG_CHANNELS; ++i)
            if (HsfServerIoDigital_isDiFresh(&dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    check("关到位后状态回到已关",
          it && it->m_state == HsfValveState_Closed, NULL);

    HsfServerIoValve_clear(&bank);
    HsfServerIoDigital_unmount(&dig);
}

/* ============================================================================
 * ④b 超时告警路径：行程过长（> 动作超时）必须报"超时未到位"
 * ============================================================================ */
static void testValveTimeoutAlarm(void)
{
    HsfServerIoDigital dig;
    HsfServerIoValve bank;
    HsfServerIoValveCfg cfg[HSF_VALVE_COUNT];
    const HsfServerIoValveItem* it;
    uint32_t diMask = 0;
    bool diValid = false;
    int kind = 0;
    char text[128];
    int i;

    XPrintf("---- ④b 阀动作超时告警（行程 > 动作超时）----\n");

    HsfServerIoDigital_init(&dig);
    {
        XModbusVirtualClient* vmain = HsfServerIoSim_createDigitalMain();
        if (!vmain ||
            !HsfServerIoDigital_mountWithClients(
                &dig, (XModbusClient*)vmain, NULL,
                HSF_DIG_CHANNELS, HSF_DIG_START_ADDR,
                HSF_DIG_POLL_MS, HSF_DIG_TIMEOUT_MS)) {
            check("超时用例夹具装配", 0, NULL);
            return;
        }
    }
    /* 行程 3s，但阀组动作超时设 1s ⇒ 必然判超时。 */
    XVirtualModelDigital_setMoveMs(
        (struct XVirtualModelDigital*)HsfServerIoDigital_simModel(&dig), 3000u);

    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        cfg[i].m_name = NULL;
        cfg[i].m_do = (i == 0) ? HSF_VALVE_DO(0) : -1;
        cfg[i].m_openDi = (i == 0) ? HSF_VALVE_OPEN_DI(0) : -1;
        cfg[i].m_closeDi = (i == 0) ? HSF_VALVE_CLOSE_DI(0) : -1;
    }
    HsfServerIoValve_init(&bank);
    HsfServerIoValve_configure(&bank, cfg, HSF_VALVE_COUNT, 1000, true);

    spinMs(400);
    /* 首拍判读：先让它看到"已关"，这样 everFeedback 置位 ⇒ 超时判"故障"
     * 而不是"无到位反馈"。 */
    {
        uint32_t fresh = 0;
        HsfServerIoDigital_readDi(&dig, &diMask, &diValid);
        for (i = 0; i < HSF_DIG_CHANNELS; ++i)
            if (HsfServerIoDigital_isDiFresh(&dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    check("开场为已关（关到位吸合）",
          HsfServerIoValve_item(&bank, 0) &&
          HsfServerIoValve_item(&bank, 0)->m_state == HsfValveState_Closed,
          NULL);

    /* 开阀后喂"两个到位都不吸"的位图（模拟阀在动、行程开关还没到位），
     * 这样阀组才会进「动作中」并最终判超时。
     * [不能冻结在"关到位"] 那样 closeFb=true ⇒ 阀组每拍都判"已关"，
     * 永远走不到超时分支（首版就这么写，三条断言全红）。 */
    check("开阀受理", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0), NULL);
    HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), true);

    {
        /* 阀 1 的两个到位位（DI0 / DI1）都清掉，其余位保持。 */
        const uint32_t frozenDi = diMask &
            ~((1u << HSF_VALVE_CLOSE_DI(0)) | (1u << HSF_VALVE_OPEN_DI(0)));
        for (i = 0; i < 20; ++i) {
            spinMs(100);
            HsfServerIoValve_poll(&bank, frozenDi, true, 0xFFFFFFFFu, 0, false,
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch());
        }
    }
    it = HsfServerIoValve_item(&bank, 0);
    check("超时后状态 = 故障", it && it->m_state == HsfValveState_Fault, NULL);
    {
        bool got = HsfServerIoValve_takeAlarm(&bank, 0, &kind, text, (int)sizeof(text));
        check("产生超时告警", got && kind == HsfValveAlarmKind_Timeout, NULL);
        check("告警文字含\"超时\"", got && strstr(text, "超时") != NULL, text);
    }
    {
        bool again = HsfServerIoValve_takeAlarm(&bank, 0, &kind, text,
                                                (int)sizeof(text));
        check("同一动作不重复告警（取走即消费 + 一次动作一条）", !again, NULL);
    }

    HsfServerIoValve_clear(&bank);
    HsfServerIoDigital_unmount(&dig);
}

/* ============================================================================
 * ⑥ 阀控制三分支幂等 + 外部接管（对齐 Python valves.set_valve）
 * ============================================================================
 * [为什么锁这三条] 连点同侧会把 20s 超时无限推后（卡阀被掩盖）、已到位
 * 重复下发白写 DO、外部改动（面板/现场按钮）无人解释——Python 侧三个
 * 实锅的回归锁，C 侧同口径落地后必须同等锁死。 */
static void testValveCommandSemantics(void)
{
    HsfServerIoDigital dig;
    HsfServerIoValve bank;
    HsfServerIoValveCfg cfg[HSF_VALVE_COUNT];
    const HsfServerIoValveItem* it;
    uint32_t diMask = 0;
    bool diValid = false;
    int kind = 0;
    char text[128];
    int i;

    XPrintf("---- ⑥ 阀控制三分支幂等 + 外部接管 ----\n");

    HsfServerIoDigital_init(&dig);
    {
        XModbusVirtualClient* vmain = HsfServerIoSim_createDigitalMain();
        if (!vmain ||
            !HsfServerIoDigital_mountWithClients(
                &dig, (XModbusClient*)vmain, NULL,
                HSF_DIG_CHANNELS, HSF_DIG_START_ADDR,
                HSF_DIG_POLL_MS, HSF_DIG_TIMEOUT_MS)) {
            check("幂等用例夹具装配", 0, NULL);
            return;
        }
    }
    XVirtualModelDigital_setMoveMs(
        (struct XVirtualModelDigital*)HsfServerIoDigital_simModel(&dig),
        300u);   /* 行程 300ms：到位类断言不必等真实 10s。 */
    /* [夹具复位] 模拟模型是**全局单例**：前一用例（④b 卡死用例）留下的
     * DO0=1 与在途行程会泄进本用例（开场就是"已开"）。先写回关位并等
     * 行程完成，保证下面的"开场已关"断言测的是本用例自己的语义。 */
    HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), false);
    spinMs(500);

    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        cfg[i].m_name = NULL;
        cfg[i].m_do = (i == 0) ? HSF_VALVE_DO(0) : -1;
        cfg[i].m_openDi = (i == 0) ? HSF_VALVE_OPEN_DI(0) : -1;
        cfg[i].m_closeDi = (i == 0) ? HSF_VALVE_CLOSE_DI(0) : -1;
    }
    HsfServerIoValve_init(&bank);
    HsfServerIoValve_configure(&bank, cfg, HSF_VALVE_COUNT, 1000, true);

#define POLL_VALVE()                                                          \
    do {                                                                      \
        uint32_t fresh6 = 0;                                                  \
        uint32_t doMask6 = 0;                                                 \
        bool doValid6 = false;                                                \
        int k6;                                                               \
        HsfServerIoDigital_readDi(&dig, &diMask, &diValid);                   \
        HsfServerIoDigital_readDo(&dig, &doMask6, &doValid6);                 \
        for (k6 = 0; k6 < HSF_DIG_CHANNELS; ++k6)                           \
            if (HsfServerIoDigital_isDiFresh(&dig, k6)) fresh6 |= (1u << k6); \
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh6, doMask6,        \
                              doValid6,                                       \
                              (uint64_t)XDateTime_currentMSecsSinceEpoch());  \
    } while (0)

    spinMs(400);
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("⑥ 开场已关", it && it->m_state == HsfValveState_Closed, NULL);

    /* ① 已到位同目标 → ignored：受理但不出帧。 */
    check("① 已关再关：受理", HsfServerIoValve_setOutput(&bank, 0, false, NULL, 0),
          NULL);
    it = HsfServerIoValve_item(&bank, 0);
    check("① 处置 = ignored（不写 DO）",
          it && strcmp(it->m_cmdResult, "ignored") == 0,
          it ? it->m_cmdResult : "?");

    /* ② 新动作 → issued；到位后同目标再点仍 ignored。 */
    check("② 开阀受理", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0), NULL);
    it = HsfServerIoValve_item(&bank, 0);
    check("② 处置 = issued", it && strcmp(it->m_cmdResult, "issued") == 0,
          it ? it->m_cmdResult : "?");
    check("② 写 DO 受理",
          HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), true), NULL);
    /* [自有写入登记] 真实链路里 HsfServerIo_valveSet 写完会调
     * noteOwnWrite；测试直写 DO 必须同样登记，否则下一拍被阀组判成
     * "外部改动"接管（cmdResult=external），后续处置断言全部错位。 */
    HsfServerIoValve_noteOwnWrite(&bank, 0, true);
    spinMs(120);
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("② 动作中", it && it->m_state == HsfValveState_Moving, NULL);
    spinMs(500);
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("② 行程走完已开", it && it->m_state == HsfValveState_Open, NULL);
    check("② 已开再开：ignored", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0) &&
          strcmp(HsfServerIoValve_item(&bank, 0)->m_cmdResult, "ignored") == 0,
          NULL);

    /* ③ 卡死口径下同目标在途连点 → repeated 且**不重置计时器**。 */
    XVirtualModelDigital_setMoveMs(
        (struct XVirtualModelDigital*)HsfServerIoDigital_simModel(&dig),
        UINT32_MAX);   /* 卡死：永远不到位，超时分支可达。 */
    check("③ 关阀（新动作）", HsfServerIoValve_setOutput(&bank, 0, false, NULL, 0),
          NULL);
    check("③ 写 DO=0",
          HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), false), NULL);
    HsfServerIoValve_noteOwnWrite(&bank, 0, false);   /* 同②：自有写入登记。 */
    spinMs(300);   /* ≥ 数字量站轮询周期：等 Hub 缓存翻到写后的新 DI。 */
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("③ 在途（卡死，Moving）", it && it->m_state == HsfValveState_Moving,
          NULL);
    {
        uint64_t cmdAt = it ? it->m_cmdAtMs : 0;
        check("③ 在途重发受理",
              HsfServerIoValve_setOutput(&bank, 0, false, NULL, 0), NULL);
        it = HsfServerIoValve_item(&bank, 0);
        check("③ 处置 = repeated",
              it && strcmp(it->m_cmdResult, "repeated") == 0,
              it ? it->m_cmdResult : "?");
        check("③ 计时器未被重置（超时从首次命令起算）",
              it && it->m_cmdAtMs == cmdAt, NULL);
    }
    spinMs(1300);   /* 越过 1s 超时判据。 */
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("③ 连点后超时仍触发（卡阀无法被掩盖）",
          it && it->m_state == HsfValveState_Fault, NULL);
    check("③ 超时告警一条",
          HsfServerIoValve_takeAlarm(&bank, 0, &kind, text, (int)sizeof(text)) &&
          kind == HsfValveAlarmKind_Timeout, NULL);
    check("③ 不重复告警",
          !HsfServerIoValve_takeAlarm(&bank, 0, &kind, text, (int)sizeof(text)),
          NULL);

    /* ④ 在途反向 → reversed。 */
    check("④ 反向受理", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0), NULL);
    it = HsfServerIoValve_item(&bank, 0);
    check("④ 处置 = reversed", it && strcmp(it->m_cmdResult, "reversed") == 0,
          it ? it->m_cmdResult : "?");

    /* ⑤ 外部接管：绕过服务端直接写 DO（现场按钮口径）→ external。 */
    XVirtualModelDigital_setMoveMs(
        (struct XVirtualModelDigital*)HsfServerIoDigital_simModel(&dig),
        300u);
    spinMs(400);
    POLL_VALVE();
    HsfServerIoDigital_writeDo(&dig, HSF_VALVE_DO(0), true);   /* 外部开阀（非我方命令）。 */
    spinMs(350);   /* ≥ 站点轮询周期：等 Hub 缓存翻到"动作中"（两 DI 断开）。 */
    POLL_VALVE();
    it = HsfServerIoValve_item(&bank, 0);
    check("⑤ 外部改动被接管（cmdResult=external）",
          it && strcmp(it->m_cmdResult, "external") == 0,
          it ? it->m_cmdResult : "?");
    check("⑤ 接管起算超时（cmdAtMs>0）", it && it->m_cmdAtMs > 0, NULL);

#undef POLL_VALVE
    HsfServerIoValve_clear(&bank);
    HsfServerIoDigital_unmount(&dig);
}

/* ============================================================================
 * ⑤ 模拟单例生命周期：模型必须在客户端全部释放后才销毁
 * ============================================================================ */
static void testSimLifecycle(void)
{
    const HsfServerIoSimModels* m;
    XModbusVirtualClient* c;

    XPrintf("---- ⑤ 模拟单例生命周期 ----\n");
    m = HsfServerIoSim_ensure();
    check("模型集合建立", m && m->m_vfd && m->m_tempHum && m->m_flowMeter &&
          m->m_digMain && m->m_digExt && m->m_analog, NULL);
    check("重复 ensure 幂等", HsfServerIoSim_ensure() == m, NULL);

    /* 客户端借用模型：客户端先释放，模型仍可用（顺序正确时无悬垂）。 */
    c = HsfServerIoSim_createAnalog();
    check("模拟量客户端创建", c != NULL, NULL);
    if (c) XClassDelete((XClass*)c);

    HsfServerIoSim_destroy();
    check("destroy 后重 ensure 能重建",
          HsfServerIoSim_ensure() != NULL, NULL);
    HsfServerIoSim_destroy();
}

int XHsfServerIoSimModeTest_run(void)
{
    XCoreApplication* app;
    bool owned;
    int argc = 0;
    char** argv = NULL;

    s_failures = 0;
    XPrintf("========== HSF 服务端模拟设备模式回归 ==========\n");

    /* [单例所有权] XCoreApplication 是进程单例：create 在已有实例时**原样
     * 返回 g_app**（XCoreApplication.c 的 create_ex 首行）。--test 路径下
     * main 已在入口建好实例并对它负责（main.c 收尾的 XClassDelete(app)），
     * 本函数若无条件 XClassDelete 就是删同一个对象两次——实测退出时段错误，
     * 栈为 XClass_deinit_base ← XClass_delete_base ← main+0x4b1（堆已填
     * 0xdd 的已释放对象）。故只删自己建的（创建前 instance() 为空）。
     * 同 xindustry_valve_test.c 的口径。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("[FAIL] XCoreApplication 创建失败\n");
        return 1;
    }

    testMultiUnitDispatch();
    testVfdSegmentRead();
    testFlowMeterHeartbeat();
    testValveEndToEnd();
    testValveTimeoutAlarm();
    testValveCommandSemantics();
    testSimLifecycle();

    if (owned) XClassDelete((XClass*)app);
    XPrintf("========== 结果：%s（%d 项失败）==========\n",
            s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures;
}

void XHsfServerIoSimModeTest(XVariant* data)
{
    (void)data;
    (void)XHsfServerIoSimModeTest_run();
}

#else /* !(XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON) */

int XHsfServerIoSimModeTest_run(void) { return 0; }
void XHsfServerIoSimModeTest(XVariant* data) { (void)data; }

#endif
