
/**
 * @file       HsfServerIo.c
 * @brief      服务端工业 IO 门面实现：多路 485 总线 + 模拟量站编排。
 * @details    [编排顺序] 每条启用的 485 总线走同一套三步：
 *               1) HsfServerBus_open      —— 建 Hub + RTU 客户端 + 开串口；
 *               2) 各站 mount             —— 往该路映射表登记段、建组/链路/设备；
 *               3) HsfServerBus_start     —— 提交映射 + 放开轮询 + 起轮询。
 *             [拆除顺序] 与装配严格反向：
 *               各站 unmount（先退订 Hub 信号、删设备、删组）
 *               → HsfServerBus_stop（Hub 最后死）。
 *             顺序错了就是悬垂：站链路订阅的是 Hub 信号，Hub 先死则站
 *             析构时退订会碰已释放对象。
 *
 *             [路 B 预留] HSF_BUS_B_ENABLED=0 时整路不建任何对象（连串口都
 *             不打开），因此插不插第二个适配器都不影响启动。硬到货后把
 *             HSF_BUS_B_ENABLED 置 1 并在该路挂上浊度仪/流量计站即可。
 */
#include "HsfServerIo.h"
#include "HsfServerIoSim.h"     /* 模拟设备工厂（--sim / HSF_SIM_MODE）。 */

#include "XModbusHub.h"
#include "XModbusClient.h"        /* XModbusClient 类型（注入客户端的形参）。 */
#include "XModbusVirtualClient.h"
#include "XVfdM512.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>

/* [2026-11-05] 数字量/电磁阀参数**全部在 HsfServerConfig.h 宏里**（不进
 * INI）：它们是现场接线/部署的组成部分，与设备一起版本化。 */

/* ==================== 模拟模式开关 ==================== */

/**
 * @brief 运行期是否走模拟设备（编译期默认 HSF_SIM_MODE，可被 --sim 打开）。
 * @details [为什么是运行期开关而不是纯编译期] 现场排查时经常需要
 *          "同一份程序，一半时间接设备、一半时间脱机演示"；编译期开关
 *          要求两种场景各编一个 exe，且容易误把模拟版发到现场。
 *          运行期开关让同一个 HsfServer.exe 两种形态都能起。
 */
static bool s_ioSimMode = (HSF_SIM_MODE != 0);

void HsfServerIo_setSimMode(bool enable)
{
    s_ioSimMode = enable;
}

bool HsfServerIo_simMode(void)
{
    return s_ioSimMode;
}

/* ==================== 模块状态 ==================== */

/**
 * @brief 一侧的运行时装配（总线 + 该路上的各站）。
 */
typedef struct HsfServerIoSlot {
    HsfServerBus m_bus;             /**< 该侧的 485 总线。 */
    HsfServerIoVfd m_vfd;           /**< 变频器站（m_mounted=0 表示未挂）。 */
    HsfServerIoTempHum m_tempHum;   /**< 温湿度站（m_mounted=0 表示未挂）。 */
    HsfServerIoFlowMeter m_flowMeter; /**< 电磁流量计站（m_mounted=0 表示未挂）。 */
    /** @brief 浊度仪 ×2（同一路 485、不同站号；各自 m_mounted 单独判定）。
     *  [为什么是数组] 现场两台表的站号是手工在面板上设的（1 和 3），与流量计
     *  共线；某台没接线时另一台必须照常工作，故逐台独立装配/判定。 */
    HsfServerIoTurbidity m_turbidity[HSF_TURBIDITY_COUNT];
    bool m_ready;                   /**< 该侧可用（总线已开且至少一站挂上）。 */
} HsfServerIoSlot;

/**
 * @brief 模块单例状态。
 */
typedef struct HsfServerIoState {
    bool m_started;                     /**< 已启动标志（幂等保护）。 */
    HsfServerIoSlot m_sides[HsfSide_Count]; /**< 两路 485 侧。 */
    HsfServerIoAnalog m_analog;         /**< 模拟量站（TCP，与 485 无关）。 */
    HsfServerIoDigital m_digital;       /**< 数字量站（TCP，16DI/16DO）。 */
    HsfServerIoValve m_valves;          /**< 电磁阀组（纯业务层，点位来自配置）。 */
    HsfServerIoPump m_pumps;            /**< 进水泵组（纯业务层，点位来自配置）。 */
} HsfServerIoState;

static HsfServerIoState s_io;           /**< 模块单例（零初始化）。 */

/* ==================== 每路链路参数（改口径只动这里） ==================== */

/**
 * @brief 一路 485 的链路参数（每路一套，互不影响）。
 */
typedef struct HsfServerIoBusParam {
    const char* m_name;     /**< 显示名（静态常量）。 */
    const char* m_port;     /**< 串口名（静态常量）。 */
    int m_baud;             /**< 波特率（bps）。 */
    int m_timeoutMs;        /**< 单帧请求超时（毫秒）。 */
    int m_retries;          /**< 重试次数。 */
    int m_scanMs;           /**< 扫描节拍（毫秒/读组；从站节拍另配）。 */
    bool m_hasVfd;          /**< 是否挂变频器站。 */
    bool m_hasTempHum;      /**< 是否挂温湿度站。 */
    bool m_hasFlowMeter;    /**< 是否挂电磁流量计站。 */
    bool m_hasTurbidity;    /**< 是否挂浊度仪站（两台一起）。 */
} HsfServerIoBusParam;

/** @brief 路 A 参数（现役：变频器 + 温湿度共用同一条 485）。 */
static const HsfServerIoBusParam s_busParamA = {
    "485-A", HSF_BUS_A_PORT, HSF_BUS_A_BAUDRATE, HSF_BUS_A_TIMEOUT_MS,
    HSF_BUS_A_RETRIES, HSF_BUS_A_SCAN_MS,
    HSF_BUS_A_HAS_VFD != 0, HSF_BUS_A_HAS_TEMPHUM != 0,
    HSF_BUS_A_HAS_FLOWMETER != 0, HSF_BUS_A_HAS_TURBIDITY != 0
};

/** @brief 路 B 参数（电磁流量计 + 浊度仪 ×2；COM18）。
 *  [现场接法] 浊度仪两台与流量计**同一条 485**，靠站号区分（1/3/5）。
 *  这里把三个站挂在同一个 Hub 上：各自独立的映射段与从站约束，互不干扰。 */
static const HsfServerIoBusParam s_busParamB = {
    "485-B", HSF_BUS_B_PORT, HSF_BUS_B_BAUDRATE, HSF_BUS_B_TIMEOUT_MS,
    HSF_BUS_B_RETRIES, HSF_BUS_B_SCAN_MS,
    HSF_BUS_B_HAS_VFD != 0, HSF_BUS_B_HAS_TEMPHUM != 0,
    HSF_BUS_B_HAS_FLOWMETER != 0, HSF_BUS_B_HAS_TURBIDITY != 0
};

/* ==================== 单路装配 ==================== */

/**
 * @brief 装配一条 485 侧（总线 + 按参数决定挂哪些站）。
 * @param side 侧号；slot 该侧槽位；p 该路链路参数（不能让）。
 * @return 总线打开且至少一站挂成功返回 true。
 */
static bool hsfIoMountSide(HsfSide side, HsfServerIoSlot* slot,
                           const HsfServerIoBusParam* p)
{
    XModbusMapping* map = NULL;
    bool anyStation = false;

    if (!slot || !p) return false;
    HsfServerIoTempHum_init(&slot->m_tempHum);
    HsfServerIoVfd_init(&slot->m_vfd);
    HsfServerIoFlowMeter_init(&slot->m_flowMeter);
    {
        int t;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t)
            HsfServerIoTurbidity_init(&slot->m_turbidity[t]);
    }
    slot->m_ready = false;

    /* ---- 传输层：真实 RTU 串口 或 进程内模拟客户端 ---- */
    if (s_ioSimMode) {
        XModbusVirtualClient* vc = HsfServerIoSim_createBusClient(
            p->m_hasVfd, p->m_hasTempHum, p->m_hasFlowMeter,
            p->m_hasTurbidity);
        if (!vc) {
            XPrintf("HsfServerIo: [%s] 模拟客户端创建失败\n", p->m_name);
            return false;
        }
        if (!HsfServerBus_openWithClient(&slot->m_bus, p->m_name,
                                         (XModbusClient*)vc,
                                         p->m_timeoutMs, p->m_retries)) {
            return false;   /* openWithClient 失败已释放客户端。 */
        }
    } else if (!HsfServerBus_open(&slot->m_bus, p->m_name, p->m_port, p->m_baud,
                                  p->m_timeoutMs, p->m_retries)) {
        return false;
    }
    /* 映射表：本路一张，各站往里登记段，最后由 bus_start 一并提交。 */
    map = HsfServerBus_createMapping(&slot->m_bus);
    if (!map) {
        XPrintf("HsfServerIo: [%s] 映射表创建失败\n", p->m_name);
        goto fail;
    }
    if (p->m_hasVfd) {
        if (!HsfServerIoVfd_mount(&slot->m_vfd, &slot->m_bus, map,
                                  HSF_VFD_UNIT_ID, HSF_VFD_ACTION_MS,
                                  HSF_VFD_STATUS_MS)) {
            XPrintf("HsfServerIo: [%s] 变频器站装配失败（继续其它站）\n", p->m_name);
        } else {
            anyStation = true;
        }
    }
    if (p->m_hasTempHum) {
        if (!HsfServerIoTempHum_mount(&slot->m_tempHum, &slot->m_bus, map,
                                      HSF_TEMPHUM_UNIT_ID, HSF_TEMPHUM_VADDR,
                                      HSF_TEMPHUM_STALE_MS)) {
            XPrintf("HsfServerIo: [%s] 温湿度站装配失败（继续其它站）\n", p->m_name);
        } else {
            anyStation = true;
        }
    }
    if (p->m_hasFlowMeter) {
        if (!HsfServerIoFlowMeter_mount(&slot->m_flowMeter, &slot->m_bus, map,
                                        HSF_FLOWMETER_UNIT_ID,
                                        HSF_FLOWMETER_VADDR,
                                        HSF_FLOWMETER_STALE_MS)) {
            XPrintf("HsfServerIo: [%s] 流量计站装配失败（继续其它站）\n", p->m_name);
        } else {
            anyStation = true;
        }
    }
    if (p->m_hasTurbidity) {
        /* 两台各占 2 个寄存器（32 位量）。第一台 vbase，第二台 vbase+2。
         * 某台失败**不影响另一台**——现场两台表是独立接线，一台没接不该
         * 让另一台也不显示。 */
        static const int s_turbIds[HSF_TURBIDITY_COUNT] = {
            HSF_TURBIDITY1_UNIT_ID, HSF_TURBIDITY2_UNIT_ID
        };
        int t;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
            uint16_t vbase = (uint16_t)(HSF_TURBIDITY_VADDR +
                                        (uint16_t)(t * HSF_TURBIDITY_REG_COUNT));
            if (!HsfServerIoTurbidity_mount(&slot->m_turbidity[t],
                                            &slot->m_bus, map, s_turbIds[t], t,
                                            vbase, HSF_TURBIDITY_STALE_MS)) {
                XPrintf("HsfServerIo: [%s] 浊度仪%d 装配失败（站 %d，继续其它站）\n",
                        p->m_name, t + 1, s_turbIds[t]);
            } else {
                anyStation = true;
            }
        }
    }
    if (!anyStation) {
        XPrintf("HsfServerIo: [%s] 无一站装配成功，放弃本路\n", p->m_name);
        goto fail;
    }
    if (!HsfServerBus_start(&slot->m_bus, map, p->m_scanMs)) {
        map = NULL;                 /* start 已接管所有权（内部已释放）。 */
        goto fail;
    }
    map = NULL;                      /* 所有权已移交 Hub。 */
    slot->m_ready = true;
    XPrintf("HsfServerIo: [%s] 侧 %c 就绪（变频器=%s 温湿度=%s 流量计=%s "
            "浊度仪=%s/%s 节拍=%dms/读组）\n",
            p->m_name, side == HsfSide_A ? 'A' : 'B',
            HsfServerIoVfd_isMounted(&slot->m_vfd) ? "OK" : "无",
            HsfServerIoTempHum_isMounted(&slot->m_tempHum) ? "OK" : "无",
            HsfServerIoFlowMeter_isMounted(&slot->m_flowMeter) ? "OK" : "无",
            HsfServerIoTurbidity_isMounted(&slot->m_turbidity[0]) ? "OK" : "无",
            HsfServerIoTurbidity_isMounted(&slot->m_turbidity[1]) ? "OK" : "无",
            p->m_scanMs);
    return true;

fail:
    /* 站先拆（退订 Hub 信号），总线后拆——顺序见文件头。 */
    {
        int t;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t)
            HsfServerIoTurbidity_unmount(&slot->m_turbidity[t]);
    }
    HsfServerIoFlowMeter_unmount(&slot->m_flowMeter);
    HsfServerIoTempHum_unmount(&slot->m_tempHum);
    HsfServerIoVfd_unmount(&slot->m_vfd);
    if (map) XClassDelete((XClass*)map);
    HsfServerBus_stop(&slot->m_bus);
    slot->m_ready = false;
    return false;
}

/**
 * @brief 拆一条 485 侧（严格反向：站 → 总线）。
 */
static void hsfIoUnmountSide(HsfServerIoSlot* slot)
{
    int t;
    if (!slot) return;
    /* 浊度仪先拆：它们与流量计共用同一条线，但各自订阅 Hub 信号，
     * 拆序只需保证"都在 Bus_stop 之前"完成退订。 */
    for (t = 0; t < HSF_TURBIDITY_COUNT; ++t)
        HsfServerIoTurbidity_unmount(&slot->m_turbidity[t]);
    HsfServerIoFlowMeter_unmount(&slot->m_flowMeter);
    HsfServerIoTempHum_unmount(&slot->m_tempHum);
    HsfServerIoVfd_unmount(&slot->m_vfd);
    HsfServerBus_stop(&slot->m_bus);
    slot->m_ready = false;
}

/* ==================== 公开 API ==================== */

bool HsfServerIo_start(void)
{
    HsfServerIoState* io = &s_io;

    if (io->m_started) return true;
    memset(io, 0, sizeof(*io));
    HsfServerIoAnalog_init(&io->m_analog);

    /* ---- 路 A：现役（变频器 + 温湿度共用同一条 485） ---- */
    if (HSF_BUS_A_ENABLED) {
        (void)hsfIoMountSide(HsfSide_A, &io->m_sides[HsfSide_A], &s_busParamA);
    } else {
        XPrintf("HsfServerIo: 路 A 按配置禁用\n");
    }

    /* ---- 路 B：预留（浊度仪 ×2 + 流量计；宏关时不建任何对象） ----
     * [模拟模式下一律启用] 路 B 的宏默认是 0（现场只有一根适配器时别去抢
     * COM18）。但模拟模式不需要任何物理端口，而流量计站挂在路 B——
     * 若跟着宏关掉，"全 C 端联调"就缺了流量计这一路（实测：flowmeter 主题
     * 恒 valid=false，页面永远是空）。故模拟模式忽略该宏。 */
    if (HSF_BUS_B_ENABLED || s_ioSimMode) {
        if (!HSF_BUS_B_ENABLED)
            XPrintf("HsfServerIo: 路 B 因模拟模式启用（%s 不实际打开）\n",
                    HSF_BUS_B_PORT);
        (void)hsfIoMountSide(HsfSide_B, &io->m_sides[HsfSide_B], &s_busParamB);
    } else {
        XPrintf("HsfServerIo: 路 B 预留未启用（%s）\n", HSF_BUS_B_PORT);
    }

    /* ---- 模拟量站（TCP，独立 Hub） ---- */
    if (s_ioSimMode) {
        XModbusVirtualClient* vc = HsfServerIoSim_createAnalog();
        if (!vc) {
            XPrintf("HsfServerIo: 模拟量站模拟客户端创建失败（继续其它站）\n");
        } else if (!HsfServerIoAnalog_mountWithClient(
                       &io->m_analog, (XModbusClient*)vc, HSF_AI_UNIT_ID,
                       HSF_AI_CHANNELS, HSF_AI_SHUNT_OHM, HSF_AI_POLL_MS)) {
            XPrintf("HsfServerIo: 模拟量站装配失败（继续其它站）\n");
        }
    } else if (!HsfServerIoAnalog_mount(&io->m_analog, HSF_AI_HOST,
                                        HSF_AI_TCP_PORT, HSF_AI_UNIT_ID,
                                        HSF_AI_CHANNELS, HSF_AI_SHUNT_OHM,
                                        HSF_AI_POLL_MS)) {
        XPrintf("HsfServerIo: 模拟量站装配失败（继续其它站）\n");
    }

    /* ---- 数字量站（TCP，16DI/16DO；参数来自 HsfServerConfig.h 宏） ---- */
    HsfServerIoDigital_init(&io->m_digital);
    if (!HSF_DIG_ENABLED) {
        XPrintf("HsfServerIo: 数字量站按 HSF_DIG_ENABLED=0 禁用\n");
    } else if (s_ioSimMode) {
        /* 模拟模式：.12 与 .13 各一台模型，合成 32 点。
         * [为什么要 .13] 电磁阀六路点位全在上半区（逻辑点 16..27），
         * 只装 16 点时那六路一律"未配置"——模拟联调等于没覆盖阀。 */
        XModbusVirtualClient* vmain = HsfServerIoSim_createDigitalMain();
        XModbusVirtualClient* vext = HsfServerIoSim_createDigitalExt();
        if (!vmain) {
            XPrintf("HsfServerIo: 数字量模拟客户端创建失败（继续其它站）\n");
            if (vext) XClassDelete((XClass*)vext);
        } else if (!HsfServerIoDigital_mountWithClients(
                       &io->m_digital, (XModbusClient*)vmain,
                       (XModbusClient*)vext,
                       HSF_DIG_CHANNELS_MAX, HSF_DIG_START_ADDR,
                       HSF_DIG_POLL_MS, HSF_DIG_TIMEOUT_MS)) {
            XPrintf("HsfServerIo: 数字量站装配失败（继续其它站）\n");
        }
    } else if (!HsfServerIoDigital_mount(&io->m_digital, HSF_DIG_HOST,
                                         HSF_DIG_TCP_PORT, HSF_DIG_UNIT_ID,
                                         HSF_DIG_CHANNELS,
                                         HSF_DIG_START_ADDR,
                                         HSF_DIG_POLL_MS,
                                         HSF_DIG_TIMEOUT_MS)) {
        XPrintf("HsfServerIo: 数字量站装配失败（继续其它站）\n");
    }

    /* ---- 电磁阀组（点位来自 HsfServerConfig.h 宏；依赖数字量站） ---- */
    {
        HsfServerIoValveCfg valveItems[HSF_VALVE_COUNT];
        bool enabled = (HSF_VALVE_ENABLED != 0);
        int count = HSF_VALVE_ACTIVE_COUNT;
        int i;

        if (count < 0) count = 0;
        if (count > HSF_VALVE_COUNT) count = HSF_VALVE_COUNT;
        for (i = 0; i < HSF_VALVE_COUNT; ++i) {
            valveItems[i].m_name = NULL;      /* 阀组内部给缺省名。 */
            valveItems[i].m_do = HSF_VALVE_DO(i);
            valveItems[i].m_openDi = HSF_VALVE_OPEN_DI(i);
            valveItems[i].m_closeDi = HSF_VALVE_CLOSE_DI(i);
        }
        if (!HsfServerIoDigital_isMounted(&io->m_digital)) {
            /* 没有数字量站 ⇒ 阀的 DO/DI 无处可落。仍装配阀组对象
             * （界面能看到各路"未配置/离线"），但把 enabled 关掉让控制
             * 被拒，避免"点了没反应"。 */
            if (enabled)
                XPrintf("HsfServerIo: 数字量站未装配，电磁阀组控制关闭\n");
            enabled = false;
        }
        HsfServerIoValve_configure(&io->m_valves, valveItems, count,
                                   HSF_VALVE_MOVE_TIMEOUT_MS, enabled);
    }

    /* ---- 进水泵组（点位来自 HsfServerConfig.h 宏；依赖数字量站） ---- */
    {
        HsfServerIoPumpCfg pumpItems[HSF_PUMP_COUNT];
        bool penabled = (HSF_PUMP_ENABLED != 0);
        int pcount = HSF_PUMP_COUNT;
        int i;
        for (i = 0; i < HSF_PUMP_COUNT; ++i) {
            pumpItems[i].m_name = HSF_PUMP_NAME(i);
            pumpItems[i].m_do = HSF_PUMP_DO(i);
        }
        if (!HsfServerIoDigital_isMounted(&io->m_digital)) {
            if (penabled)
                XPrintf("HsfServerIo: 数字量站未装配，进水泵控制关闭\n");
            penabled = false;
        }
        HsfServerIoPump_configure(&io->m_pumps, pumpItems, pcount, penabled);
    }

    io->m_started = true;    /* 先置位：失败也走统一拆除路径，不留半成品。 */
    if (!io->m_sides[HsfSide_A].m_ready && !io->m_sides[HsfSide_B].m_ready &&
        !HsfServerIoAnalog_isMounted(&io->m_analog) &&
        !HsfServerIoDigital_isMounted(&io->m_digital)) {
        XPrintf("HsfServerIo: 无任何从站装配成功\n");
        return false;
    }
    XPrintf("HsfServerIo: IO 装配完成（A=%s B=%s 模拟量=%s 数字量=%s 电磁阀=%d路 泵=%d台）\n",
            io->m_sides[HsfSide_A].m_ready ? "OK" : "跳过",
            io->m_sides[HsfSide_B].m_ready ? "OK" : "跳过",
            HsfServerIoAnalog_isMounted(&io->m_analog) ? "OK" : "跳过",
            HsfServerIoDigital_isMounted(&io->m_digital) ? "OK" : "跳过",
            HsfServerIoValve_count(&io->m_valves),
            HsfServerIoPump_count(&io->m_pumps));
    return true;}

void HsfServerIo_stop(void)
{
    HsfServerIoState* io = &s_io;
    if (!io->m_started) return;
    HsfServerIoValve_clear(&io->m_valves);
    HsfServerIoPump_clear(&io->m_pumps);
    HsfServerIoDigital_unmount(&io->m_digital);
    HsfServerIoAnalog_unmount(&io->m_analog);
    hsfIoUnmountSide(&io->m_sides[HsfSide_B]);
    hsfIoUnmountSide(&io->m_sides[HsfSide_A]);
    /* [顺序] 模拟模型被虚拟客户端**借用**，必须等所有 Hub（即全部客户端）
     * 释放之后再销毁模型，否则是悬垂。上面四步已把客户端全部拆掉。 */
    HsfServerIoSim_destroy();
    io->m_started = false;
    XPrintf("HsfServerIo: IO 已全部拆除\n");
}

/* ==================== 按侧查询 ==================== */

bool HsfServerIo_sideReady(HsfSide side)
{
    if (!s_io.m_started) return false;
    if ((int)side < 0 || side >= (int)HsfSide_Count) return false;
    return s_io.m_sides[side].m_ready;
}

XVfdDevice* HsfServerIo_vfdDevice(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    return (XVfdDevice*)HsfServerIoVfd_device(&s_io.m_sides[side].m_vfd);
}

bool HsfServerIo_vfdOnline(HsfSide side)
{
    HsfServerIoSlot* slot;
    const XVfdDevice* dev;
    const XVfdStatus* st;
    if (!HsfServerIo_sideReady(side)) return false;
    slot = &s_io.m_sides[side];
    if (!HsfServerIoVfd_isMounted(&slot->m_vfd)) return false;
    /* [2026-11-05 复合总线在线误判根修] 不能拿"总线设备状态"当从站在线：
     * 一条 485 上挂多个从站（变频器 + 温湿度），Hub 的设备句柄是**RTU
     * 客户端本身**，它只有一个 Connected 状态。温湿度通了就整条总线
     * Connected ⇒ 变频器被误判为在线（实测：健康列表报"变频器-A 正常"，
     * 但它的遥测快照是空的——自相矛盾，现场会据此误以为变频器没问题）。
     *
     * 正确判据：**该从站自己的数据是否新鲜有效**。XVfdDevice_status_const
     * 已按发布周期做陈旧判定（陈旧即清 valid），所以这里直接看它。
     * 这也与客户端"页面能显示什么"的口径一致。 */
    dev = HsfServerIoVfd_device(&slot->m_vfd);
    if (!dev) return false;
    st = XVfdDevice_status_const(dev);
    return st && st->m_valid;
}

const HsfServerIoTempHum* HsfServerIo_tempHum(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    if (!HsfServerIoTempHum_isMounted(&s_io.m_sides[side].m_tempHum)) return NULL;
    return &s_io.m_sides[side].m_tempHum;
}

const HsfServerIoFlowMeter* HsfServerIo_flowMeter(HsfSide side)
{
    if (!s_io.m_started) return NULL;
    if (!HsfServerIoFlowMeter_isMounted(&s_io.m_sides[side].m_flowMeter)) return NULL;
    return &s_io.m_sides[side].m_flowMeter;
}

const HsfServerIoTurbidity* HsfServerIo_turbidity(HsfSide side, int index)
{
    if (!s_io.m_started) return NULL;
    if (index < 0 || index >= HSF_TURBIDITY_COUNT) return NULL;
    if (!HsfServerIoTurbidity_isMounted(&s_io.m_sides[side].m_turbidity[index]))
        return NULL;
    return &s_io.m_sides[side].m_turbidity[index];
}

const char* HsfServerIo_vfdFaultText(uint16_t code)
{
    return XVfdM512_faultText(code);
}

/* ==================== 模拟量站 ==================== */

int HsfServerIo_aiValues(double* outMA, bool* outValid, int maxCount)
{
    return HsfServerIoAnalog_values(&s_io.m_analog, outMA, outValid, maxCount);
}

int HsfServerIo_aiRawValues(int32_t* outRaw, bool* outValid, int maxCount)
{
    return HsfServerIoAnalog_rawValues(&s_io.m_analog, outRaw, outValid, maxCount);
}

/* ==================== 数字量模块 ==================== */

bool HsfServerIo_digitalReady(void)
{
    if (!s_io.m_started) return false;
    return HsfServerIoDigital_isMounted(&s_io.m_digital);
}

int HsfServerIo_digitalCount(void)
{
    return HsfServerIoDigital_channelCount(&s_io.m_digital);
}

bool HsfServerIo_digitalMasks(uint32_t* outDi, uint32_t* outDo, bool* outValid)
{
    uint32_t di = 0;
    uint32_t doMask = 0;
    bool diValid = false;
    bool doValid = false;

    if (outDi) *outDi = 0;
    if (outDo) *outDo = 0;
    if (outValid) *outValid = false;
    if (!HsfServerIo_digitalReady()) return false;

    HsfServerIoDigital_readDi(&s_io.m_digital, &di, &diValid);
    HsfServerIoDigital_readDo(&s_io.m_digital, &doMask, &doValid);
    if (outDi) *outDi = di;
    if (outDo) *outDo = doMask;
    if (outValid) *outValid = diValid || doValid;
    return diValid || doValid;
}

bool HsfServerIo_digitalWrite(int channel, bool on)
{
    if (!HsfServerIo_digitalReady()) return false;
    return HsfServerIoDigital_writeDo(&s_io.m_digital, channel, on);
}

/* ==================== 电磁阀组 ==================== */

const HsfServerIoValve* HsfServerIo_valves(void)
{
    if (!s_io.m_started) return NULL;
    return &s_io.m_valves;
}

void HsfServerIo_valvePoll(uint64_t nowMs)
{
    uint32_t di = 0;
    uint32_t fresh = 0;
    bool valid = false;
    int i;
    int count;
    int channels;

    if (!s_io.m_started) return;
    count = HsfServerIoValve_count(&s_io.m_valves);
    if (count <= 0) return;
    if (!HsfServerIo_digitalReady()) {
        /* 数字量站没了：把反馈标成"全部不新鲜"（保留状态，界面按"模块离线"
         * 呈现），绝不在这里伪造 0 反馈——那会把六个阀全判成"动作中/未知"。 */
        HsfServerIoValve_poll(&s_io.m_valves, 0, false, 0u, nowMs);
        return;
    }
    HsfServerIoDigital_readDi(&s_io.m_digital, &di, &valid);

    /* [新鲜度必须按通道给，不能是整站一个 bool] 32 点 DI 是两台模块聚合
     * 出来的：.12 提供逻辑点 0..15、.13 提供 16..31，两台在线状态互相独立。
     * 电磁阀可能一部分挂 .12 那半区、一部分挂 .13 那半区。
     * 若整站给一个 bool，.13 掉线时 .12 仍正常应答 ⇒ 整站恒 true ⇒
     * 挂在 .13 上那几路读到恒 0 却仍算有效，界面显示"无到位反馈（检查行程
     * 开关接线）"——而真相是模块掉线，提示把人引向错误方向。
     * 按通道给位图后，每路用自己的两个 DI 通道各自判定。 */
    channels = HsfServerIo_digitalCount();
    if (channels > 32) channels = 32;
    for (i = 0; i < channels; ++i) {
        if (HsfServerIoDigital_isDiFresh(&s_io.m_digital, i))
            fresh |= (1u << i);
    }
    HsfServerIoValve_poll(&s_io.m_valves, di, valid, fresh, nowMs);
}

bool HsfServerIo_valveSet(int index, bool wantOpen, char* outErr, int cap)
{
    const HsfServerIoValveItem* it;
    bool ok;

    if (outErr && cap > 0) outErr[0] = '\0';
    if (!s_io.m_started) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "IO 未启动");
        return false;
    }
    /* 先让阀组做校验 + 意图记账（未配置/未启用/越界在这里被拒）。 */
    ok = HsfServerIoValve_setOutput(&s_io.m_valves, index, wantOpen, outErr, cap);
    if (!ok) return false;

    it = HsfServerIoValve_item(&s_io.m_valves, index);
    if (!it) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "阀条目缺失");
        return false;
    }
    if (!HsfServerIo_digitalReady()) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "数字量站未装配，无法动作");
        return false;
    }
    /* [扩展模块离线 ⇒ 明确报错，不要伪装成"阀卡住"] 电磁阀挂在上半区
     * （逻辑点 16..31）时，这些点由扩展模块 .13 提供。.13 掉线后主模块
     * .12 仍正常应答，DO 写会落到一个不存在的通道上——若放行，界面先显示
     * "动作中"、超时后显示"动作超时未到位"，**编造出"阀卡住"的假象**。
     * 故这里按"该路自己的 DO/DI 通道是否新鲜"判，离线直接给出结论。
     * 拒绝点：DO 通道不新鲜即拒（写不进去）；反馈通道不新鲜也拒——
     * 写了也验证不了是否到位，等于盲操作。 */
    if (!HsfServerIoDigital_isDiFresh(&s_io.m_digital, it->m_openDi) ||
        !HsfServerIoDigital_isDiFresh(&s_io.m_digital, it->m_closeDi)) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "反馈模块离线，无法控制（检查扩展模块在线与接线）");
        return false;
    }
    /* [动作出口] 单点写对应 DO：开=写 1、关=写 0（弹簧复位/双电控在软件层
     * 表现一致，差别只在现场继电器接线——见 Python valves.py 头注释）。 */
    return HsfServerIoDigital_writeDo(&s_io.m_digital, it->m_do, wantOpen);
}

bool HsfServerIo_valveTakeAlarm(int index, int* outKind, char* outText, int cap)
{
    if (outText && cap > 0) outText[0] = '\0';
    if (!s_io.m_started) return false;
    return HsfServerIoValve_takeAlarm(&s_io.m_valves, index, outKind,
                                      outText, cap);
}

/* ==================== 进水泵组 ==================== */

const HsfServerIoPump* HsfServerIo_pumps(void)
{
    if (!s_io.m_started) return NULL;
    return &s_io.m_pumps;
}

void HsfServerIo_pumpPoll(void)
{
    uint32_t doMask = 0;
    bool doValid = false;

    if (!s_io.m_started) return;
    if (HsfServerIoPump_count(&s_io.m_pumps) <= 0) return;
    if (!HsfServerIo_digitalReady()) {
        /* 数字量站没了：回显标"全部不新鲜"（命令状态保留，界面按"模块
         * 离线"呈现），绝不在这里伪造 0 回显。 */
        HsfServerIoPump_poll(&s_io.m_pumps, 0, false, false);
        return;
    }
    HsfServerIoDigital_readDo(&s_io.m_digital, &doMask, &doValid);
    /* 新鲜度：readDo 的 doValid 已含"采过一拍"口径（与数字量页推送同一
     * 数据源，自洽）；链路在线由 Hub 缓存超时兜底。 */
    HsfServerIoPump_poll(&s_io.m_pumps, doMask, doValid, true);
}

bool HsfServerIo_pumpSet(int index, bool on, char* outErr, int cap)
{
    const HsfServerIoPumpItem* it;
    bool ok;

    if (outErr && cap > 0) outErr[0] = '\0';
    if (!s_io.m_started) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "IO 未启动");
        return false;
    }
    /* 先让泵组做校验 + 命令状态记账（未配置/未启用/越界/离线在这里被拒；
     * 幂等"已处于该状态"返回 true 且不改状态）。 */
    ok = HsfServerIoPump_setOutput(&s_io.m_pumps, index, on, outErr, cap);
    if (!ok) return false;

    it = HsfServerIoPump_item(&s_io.m_pumps, index);
    if (!it) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "泵条目缺失");
        return false;
    }
    if (!HsfServerIo_digitalReady()) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s", "数字量站未装配，无法动作");
        return false;
    }
    /* [动作出口] 单点写对应 DO（FC05）：1=启动（接触器吸合）、0=停止。 */
    return HsfServerIoDigital_writeDo(&s_io.m_digital, it->m_do, on);
}

/* ==================== 诊断 ==================== */

XModbusHub* HsfServerIo_busHub(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    return HsfServerBus_hub(&s_io.m_sides[side].m_bus);
}

int HsfServerIo_busDeviceId(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return -1;
    return HsfServerBus_deviceId(&s_io.m_sides[side].m_bus);
}

bool HsfServerIo_busOnline(HsfSide side)
{
    const HsfServerBus* bus;
    XModbusHub* hub;
    int devId;
    if (side < 0 || side >= HsfSide_Count) return false;
    if (!HsfServerIo_sideReady(side)) return false;
    bus = &s_io.m_sides[side].m_bus;
    hub = HsfServerIo_busHub(side);
    if (!hub) return false;
    devId = HsfServerBus_deviceId(bus);
    if (devId < 0) return false;
    return XModbusHub_deviceState(hub, devId) == XModbusDevice_ConnectedState;
}

XModbusHub* HsfServerIo_hub(void)
{
    return HsfServerIoAnalog_hub(&s_io.m_analog);
}

int HsfServerIo_debugHubHr(HsfSide side, uint16_t vaddr)
{
    XModbusHub* hub = HsfServerIo_busHub(side);
    uint16_t v = 0;
    if (!hub) return -1;
    if (!XModbusHub_value(hub, XModbusHoldingRegisters, vaddr, &v))
        return -1;
    return (int)v;
}
