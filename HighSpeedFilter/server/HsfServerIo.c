
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
#include "HsfServerSettings.h"  /* collect/* 采集节拍（INI 覆盖编译期宏） */

#include "XModbusHub.h"
#include "XModbusClient.h"        /* XModbusClient 类型（注入客户端的形参）。 */
#include "XModbusVirtualClient.h"
#include "XFlowMeter.h"           /* XFlowMeter_setStaleMs（陈旧阈值随周期）。 */
#include "XTurbidityMeter.h"      /* XTurbidityMeter_setStaleMs（同上）。 */
#include "XTempHumTransmitter.h"  /* XTempHumTransmitter_setStaleMs（同上）。 */
#include "XVfdM512.h"
#include "XPrintf.h"
#include <string.h>
#include <stdio.h>

/* [2026-11-05] 数字量/电磁阀参数**全部在 HsfServerConfig.h 宏里**（不进
 * INI）：它们是现场接线/部署的组成部分，与设备一起版本化。 */

/* [停用从站的最小发送间隔 2026-11-10] Hub 的 XModbusHubUnitLimits 没有
 * enabled 字段，hubUnitDue 只按 `now - last >= m_minPollIntervalMs` 判到期；
 * 把间隔顶到 ~24.8 天（0x7FFFFFF0ms）即"永不轮询"，而且随时能写回正常
 * 周期恢复（不必重建组/链路）。仅用于逐台停采的浊度仪。 */
#define HSF_IO_UNIT_DISABLED_MS 0x7FFFFFF0

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

void HsfServerIo_setSimValveOverride(uint32_t moveMs, bool noFeedback)
{
    HsfServerIoSim_setValveOverride(moveMs, noFeedback);
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

/** @brief 采集节拍读 INI（定义见本文件下方；提前声明供装配期取周期）。 */
static void hsfIoCollectFromIni(HsfCollectConfig* out);

/**
 * @brief 有效陈旧阈值：不得短于该设备的采集周期。
 * @param baseStaleMs 设备基线阈值（各设备的 STALE_MS 宏）。
 * @param pollMs 该设备**当前生效**的采集周期（毫秒；<=0 视作不限）。
 * @return 有效阈值（毫秒）。
 * @details [为什么不能写死] 2026-11-10 现场实锅：用户把温湿度采集周期
 *          改成 1 分钟，几秒后主界面温湿度变 "--" —— 固定 stale=5000ms
 *          短于周期 60000ms，值在下一拍到达前必然被判陈旧。
 *          口径：有效阈值 = max(基线, 周期 × HSF_STALE_FROM_POLL_MULT)。
 *          与 HsfServerBus.c 的 healStaleMs（周期 × 20，夹 2~5s）同思路：
 *          阈值跟着周期走。**装配期与运行期（改周期）都要算**，
 *          否则只改周期不改阈值 ⇒ 同样的 "--" 依旧。
 */
static uint32_t hsfIoStaleMs(uint32_t baseStaleMs, int pollMs)
{
    uint32_t v = (pollMs > 0)
                     ? (uint32_t)pollMs * HSF_STALE_FROM_POLL_MULT : 0u;
    if (v < baseStaleMs) v = baseStaleMs;
    return v;
}

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
    /* [陈旧阈值随周期走 2026-11-10] 装配期就要按**实际生效的采集周期**
     * 算陈旧阈值：周期来自 INI（用户可能已改大），阈值若还写死 5s ⇒
     * 周期 1 分钟时值在两拍之间必被判陈旧 ⇒ 界面恒 "--"。 */
    HsfCollectConfig cfg;
    hsfIoCollectFromIni(&cfg);

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
                                      hsfIoStaleMs(HSF_TEMPHUM_STALE_MS,
                                                   cfg.temphumPollMs))) {
            XPrintf("HsfServerIo: [%s] 温湿度站装配失败（继续其它站）\n", p->m_name);
        } else {
            anyStation = true;
        }
    }
    /* [站 1 恢复期 ≥200ms 的保障机制] 2026-11-09 板端实测（ttyS4 总线，
     * docs/HSF板端ttyS4三设备取数实测总表_20261109.md §8）：站 1 浊度仪在
     * 站 5 流量计的 49 字节长帧之后需要 ≥100~125ms 才恢复应答（200ms 稳定
     * 10/10），站 2 完全免疫。Hub 的读组序是**区优先**（IR 先于 HR，与登记
     * 顺序无关）⇒ 流量计的组恒排在两台浊度仪之前，「流量计帧尾 → 站 1
     * 帧头」恒 ≈ 一个扫描节拍（20ms），恰落在站 1 的恢复期死区。这道间隙
     * **不靠装配顺序或节拍错相**（改周期只会让相位漂移周期性扫过坏相位），
     * 而是给站 1 配发帧前静默闸 HSF_TURBIDITY1_TX_QUIET_MS=200ms（经
     * XModbusHubUnitLimits.m_preTxQuietMs 生效，见 mount 实参）。 */
    if (p->m_hasTurbidity) {
        /* 逐台参数表（站号 / 虚拟地址 / 寄存器数 / 字序 / 静默期）全部来自
         * HsfServerConfig.h——换表/改站号只改那个头文件，不动本文件。
         * [2026-11-09 实测] 站 1=CDAB ×2 + 静默 200ms；站 2=DCBA ×4
         * （浊度+温度同刻）+ 无静默（免疫）。
         * 某台失败**不影响另一台**——现场两台表是独立接线，一台没接不该
         * 让另一台也不显示。 */
        static const int s_turbIds[HSF_TURBIDITY_COUNT] = {
            HSF_TURBIDITY1_UNIT_ID, HSF_TURBIDITY2_UNIT_ID
        };
        static const uint16_t s_turbVbase[HSF_TURBIDITY_COUNT] = {
            (uint16_t)HSF_TURBIDITY_VADDR, (uint16_t)HSF_TURBIDITY2_VADDR
        };
        static const int s_turbCounts[HSF_TURBIDITY_COUNT] = {
            HSF_TURBIDITY1_REG_COUNT, HSF_TURBIDITY2_REG_COUNT
        };
        static const int s_turbOrders[HSF_TURBIDITY_COUNT] = {
            HSF_TURBIDITY1_WORD_ORDER, HSF_TURBIDITY2_WORD_ORDER
        };
        static const uint32_t s_turbQuiet[HSF_TURBIDITY_COUNT] = {
            HSF_TURBIDITY1_TX_QUIET_MS, HSF_TURBIDITY2_TX_QUIET_MS
        };
        int t;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
            if (!HsfServerIoTurbidity_mount(&slot->m_turbidity[t],
                                            &slot->m_bus, map, s_turbIds[t], t,
                                            s_turbVbase[t],
                                            hsfIoStaleMs(HSF_TURBIDITY_STALE_MS,
                                                         (t == 0)
                                                             ? cfg.turbidity1PollMs
                                                             : cfg.turbidity2PollMs),
                                            s_turbCounts[t], s_turbOrders[t],
                                            s_turbQuiet[t])) {
                XPrintf("HsfServerIo: [%s] 浊度仪%d 装配失败（站 %d，继续其它站）\n",
                        p->m_name, t + 1, s_turbIds[t]);
            } else {
                anyStation = true;
            }
        }
    }
    if (p->m_hasFlowMeter) {
        if (!HsfServerIoFlowMeter_mount(&slot->m_flowMeter, &slot->m_bus, map,
                                        HSF_FLOWMETER_UNIT_ID,
                                        HSF_FLOWMETER_VADDR,
                                        hsfIoStaleMs(HSF_FLOWMETER_STALE_MS,
                                                     cfg.flowmeterPollMs))) {
            XPrintf("HsfServerIo: [%s] 流量计站装配失败（继续其它站）\n", p->m_name);
        } else {
            anyStation = true;
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
        /* 模拟模式：与**真实装配同一条口径**——
         *   · HSF_DIG_HOST_EXT 非空 ⇒ 主 + 扩展两台，合成 32 点；
         *   · 为空（当前现场：.12 拆除后只剩 .13）⇒ 只建主模块，16 点。
         * [为什么要跟着宏走] 模拟器是给联调/回归用的，点数必须和现场一致，
         * 否则模拟显示 32 点、实机 16 点，测出来的东西不是现场的。 */
        XModbusVirtualClient* vmain = HsfServerIoSim_createDigitalMain();
        XModbusVirtualClient* vext = (HSF_DIG_HOST_EXT[0] != '\0')
                                     ? HsfServerIoSim_createDigitalExt() : NULL;
        if (!vmain) {
            XPrintf("HsfServerIo: 数字量模拟客户端创建失败（继续其它站）\n");
            if (vext) XClassDelete((XClass*)vext);
        } else if (!HsfServerIoDigital_mountWithClients(
                       &io->m_digital, (XModbusClient*)vmain,
                       (XModbusClient*)vext,
                       /* [点数跟着装配走] 单台 = HSF_DIG_CHANNELS(16)；
                        * 挂上扩展模块才是 32 点聚合。模拟器给 32 点、
                        * 实机 16 点的话，测出来的东西不是现场的。 */
                       vext ? HSF_DIG_CHANNELS_MAX : HSF_DIG_CHANNELS,
                       HSF_DIG_START_ADDR,
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
    /* [采集节拍 2026-11-10] 装配完成后把 INI 的 collect/* 应用一次（此前
     * 这些键只写不读 ⇒ 远端设置里改采集时间永远无效，连重启都不生效）。 */
    (void)HsfServerIo_applyCollectConfig(NULL);
    XPrintf("HsfServerIo: IO 装配完成（A=%s B=%s 模拟量=%s 数字量=%s 电磁阀=%d路 泵=%d台）\n",
            io->m_sides[HsfSide_A].m_ready ? "OK" : "跳过",
            io->m_sides[HsfSide_B].m_ready ? "OK" : "跳过",
            HsfServerIoAnalog_isMounted(&io->m_analog) ? "OK" : "跳过",
            HsfServerIoDigital_isMounted(&io->m_digital) ? "OK" : "跳过",
            HsfServerIoValve_count(&io->m_valves),
            HsfServerIoPump_count(&io->m_pumps));
    return true;}

/**
 * @brief 从 INI 读采集节拍（缺键回落编译期宏）。
 * @param out 输出。
 * @note  键名与协议 CFGK_* 同字面（collect/vfdPollMs …）：远端设置写的是
 *        同一批键，两端口径天然一致。
 */
static void hsfIoCollectFromIni(HsfCollectConfig* out)
{
    if (!out) return;
    /* [缺键必须回落宏 2026-11-10] HsfServerSettings_getInt 对**缺失键返回 0**
     * 而非调用方缺省（INI 后端既有行为："缺键吞默认值"），直接用会把所有
     * 节拍读成 0（=0ms）。故先 contains 再读。 */
    out->vfdPollMs = HsfServerSettings_contains("collect/vfdPollMs")
                         ? HsfServerSettings_getInt("collect/vfdPollMs",
                                                    HSF_VFD_MIN_POLL_MS)
                         : (int)HSF_VFD_MIN_POLL_MS;
    out->temphumPollMs = HsfServerSettings_contains("collect/temphumPollMs")
                             ? HsfServerSettings_getInt("collect/temphumPollMs",
                                                        HSF_TEMPHUM_MIN_POLL_MS)
                             : (int)HSF_TEMPHUM_MIN_POLL_MS;
    out->turbidityPollMs = HsfServerSettings_contains("collect/turbidityPollMs")
                               ? HsfServerSettings_getInt("collect/turbidityPollMs",
                                                          HSF_TURBIDITY_MIN_POLL_MS)
                               : (int)HSF_TURBIDITY_MIN_POLL_MS;
    /* [逐台 2026-11-10] 缺键回落"两台共同缺省"，再回落宏。 */
    out->turbidity1PollMs =
        HsfServerSettings_contains("collect/turbidity1PollMs")
            ? HsfServerSettings_getInt("collect/turbidity1PollMs",
                                       out->turbidityPollMs)
            : out->turbidityPollMs;
    out->turbidity2PollMs =
        HsfServerSettings_contains("collect/turbidity2PollMs")
            ? HsfServerSettings_getInt("collect/turbidity2PollMs",
                                       out->turbidityPollMs)
            : out->turbidityPollMs;
    /* [逐台开关 2026-11-10] 两台浊度是两台独立设备：缺逐台键回落共同开关
     * （旧配置/旧客户端只写 turbidityEnabled），再回落「路 B 是否装配」。
     * [模拟模式跟随装配 2026-11-10] 路 B 在模拟模式下被强制装配（见
     * HsfServerIo_start 路 B 注释）：从站对象都建出来了，开关缺省若仍按
     * 编译期宏（桌面构建=0）走，两台浊度缺省恒「停」⇒ 健康快照恒
     * 「数据过期·无有效数据」（2026-11-10 桌面 --sim 实锅：同一条
     * 485-B 上流量计无运行时开关照采「正常」、两台浊度停采「过期」，
     * 同屏三种口径）。缺省改跟装配事实（宏或模拟模式），与 Python
     * 模拟器全采口径对齐；INI 显式覆盖仍然最高。 */
    {
        const bool bMounted = (HSF_BUS_B_ENABLED || s_ioSimMode) != 0;
        const bool allOn =
            HsfServerSettings_contains("collect/turbidityEnabled")
                ? (HsfServerSettings_getInt("collect/turbidityEnabled",
                                            bMounted ? 1 : 0) != 0)
                : bMounted;
        out->turbidity1Enabled =
            HsfServerSettings_contains("collect/turbidity1Enabled")
                ? (HsfServerSettings_getInt("collect/turbidity1Enabled",
                                            allOn ? 1 : 0) != 0)
                : allOn;
        out->turbidity2Enabled =
            HsfServerSettings_contains("collect/turbidity2Enabled")
                ? (HsfServerSettings_getInt("collect/turbidity2Enabled",
                                            allOn ? 1 : 0) != 0)
                : allOn;
    }
    out->flowmeterPollMs =
        HsfServerSettings_contains("collect/flowmeterPollMs")
            ? HsfServerSettings_getInt("collect/flowmeterPollMs",
                                       HSF_FLOWMETER_MIN_POLL_MS)
            : (int)HSF_FLOWMETER_MIN_POLL_MS;
    out->analogPollMs = HsfServerSettings_contains("collect/analogPollMs")
                            ? HsfServerSettings_getInt("collect/analogPollMs",
                                                       HSF_AI_POLL_MS)
                            : (int)HSF_AI_POLL_MS;
    out->digitalPollMs = HsfServerSettings_contains("collect/digitalPollMs")
                             ? HsfServerSettings_getInt("collect/digitalPollMs",
                                                        HSF_DIG_POLL_MS)
                             : (int)HSF_DIG_POLL_MS;
}

void HsfServerIo_collectConfig(HsfCollectConfig* out)
{
    if (out) hsfIoCollectFromIni(out);
}

/**
 * @brief 把某个从站的最小发送间隔写进 Hub 的从站约束（保留其它约束字段）。
 * @param hub Hub。
 * @param devId 设备号。
 * @param unitId 从站号。
 * @param ms 最小发送间隔（<=0 不设限）。
 * @return 该从站有约束条目且写入成功返回 true。
 */
static bool hsfIoApplyUnitMinPoll(XModbusHub* hub, int devId, int unitId, int ms)
{
    XModbusHubUnitLimits lim;
    if (!hub || unitId < 0) return false;
    if (!XModbusHub_getUnitLimits(hub, devId, unitId, &lim)) return false;
    lim.m_minPollIntervalMs = (ms > 0) ? (uint32_t)ms : 0u;
    return XModbusHub_setUnitLimits(hub, devId, unitId, &lim);
}

bool HsfServerIo_applyCollectConfig(const HsfCollectConfig* cfg)
{
    HsfCollectConfig want;
    bool any = false;
    int si;

    if (!s_io.m_started) return false;
    if (cfg) want = *cfg;
    else hsfIoCollectFromIni(&want);
    /* 非法值兜底：<=0 视为「不设限/缺省」，绝不把 0 当"0ms 全速"写下去。 */
    if (want.vfdPollMs < 0) want.vfdPollMs = HSF_VFD_MIN_POLL_MS;
    if (want.temphumPollMs <= 0) want.temphumPollMs = HSF_TEMPHUM_MIN_POLL_MS;
    if (want.turbidityPollMs <= 0) want.turbidityPollMs = HSF_TURBIDITY_MIN_POLL_MS;
    if (want.turbidity1PollMs <= 0) want.turbidity1PollMs = want.turbidityPollMs;
    if (want.turbidity2PollMs <= 0) want.turbidity2PollMs = want.turbidityPollMs;
    if (want.flowmeterPollMs <= 0) want.flowmeterPollMs = HSF_FLOWMETER_MIN_POLL_MS;

    for (si = 0; si < (int)HsfSide_Count; ++si) {
        HsfSide side = (HsfSide)si;
        HsfServerIoSlot* slot = &s_io.m_sides[si];
        XModbusHub* hub;
        int devId;
        int fastest = 0;
        if (!slot->m_ready) continue;
        hub = HsfServerBus_hub(&slot->m_bus);
        devId = HsfServerBus_deviceId(&slot->m_bus);
        if (!hub || devId < 0) continue;

        /* 逐从站下发最小发送间隔；同时记录本侧最紧的一档作为设备节拍
         * （Hub 每设备一个定时器、每拍只发一个读组）。 */
        if (HsfServerIo_vfdDevice(side)) {
            if (hsfIoApplyUnitMinPoll(hub, devId, HSF_VFD_UNIT_ID,
                                      want.vfdPollMs))
                any = true;
            if (want.vfdPollMs > 0 &&
                (fastest == 0 || want.vfdPollMs < fastest))
                fastest = want.vfdPollMs;
        }
        if (HsfServerIo_tempHum(side)) {
            if (hsfIoApplyUnitMinPoll(hub, devId, HSF_TEMPHUM_UNIT_ID,
                                      want.temphumPollMs))
                any = true;
            if (fastest == 0 || want.temphumPollMs < fastest)
                fastest = want.temphumPollMs;
        }
        {
            int t;
            for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
                const HsfServerIoTurbidity* tb = HsfServerIo_turbidity(side, t);
                int uid;
                if (!tb) continue;
                uid = HsfServerIoTurbidity_unitId(tb);
                /* [逐台周期 2026-11-10] 站1 有 200ms 发帧前静默期、站3 没有，
                 * 共用一个周期会互相拖累。t==0 是浊度仪1（站1）。
                 * [逐台开关] 停用的台把最小发送间隔顶到天文数字 ⇒
                 * hubUnitDue 永不到期（静默不读），且不参与设备节拍取最紧
                 * ——另一台照常按自己的周期采，不被拖慢。Hub 的
                 * XModbusHubUnitLimits 没有 enabled 字段，这是不扩框架的
                 * 等价实现（重新启用时同一入口写回正常周期即可）。 */
                {
                    const bool on = (t == 0) ? want.turbidity1Enabled
                                             : want.turbidity2Enabled;
                    const int ms = (t == 0) ? want.turbidity1PollMs
                                            : want.turbidity2PollMs;
                    const int applyMs = on ? ms : HSF_IO_UNIT_DISABLED_MS;
                    if (hsfIoApplyUnitMinPoll(hub, devId, uid, applyMs))
                        any = true;
                    if (on && ms > 0 && (fastest == 0 || ms < fastest))
                        fastest = ms;
                }
            }
        }
        if (HsfServerIo_flowMeter(side)) {
            /* [流量计独立键 2026-11-10] 原先跟随温湿度周期；现场要求单列可调
             * （49 字节长帧最慢，常需要比温湿度更大的间隔）。 */
            if (hsfIoApplyUnitMinPoll(hub, devId, HSF_FLOWMETER_UNIT_ID,
                                      want.flowmeterPollMs))
                any = true;
            if (want.flowmeterPollMs > 0 &&
                (fastest == 0 || want.flowmeterPollMs < fastest))
                fastest = want.flowmeterPollMs;
        }
        /* 设备定时器节拍：取本侧最紧的一档（各从站的 m_minPollIntervalMs
         * 仍各自生效，节拍只是"扫描周期"）。 */
        if (fastest > 0) {
            if (XModbusHub_setPollInterval(hub, devId, (size_t)fastest))
                any = true;
        }
    }

    /* [陈旧阈值随周期走 · 运行期 2026-11-10] 改了采集周期就必改陈旧阈值：
     * 阈值短于周期 ⇒ 两拍之间值被判陈旧 ⇒ 界面恒 "--"（现场实锅：温湿度
     * 周期改 1 分钟后几秒就 "--"）。装配期已按 INI 算过一次，这里再按
     * **本次生效的周期**推到设备层，两处口径一致。
     * 设备层已有 setStaleMs（框架现成能力，不新造）：温湿度/流量计各一，
     * 两台浊度仪各用各的周期（逐台可调）。 */
    {
        int si2;
        for (si2 = 0; si2 < (int)HsfSide_Count; ++si2) {
            HsfSide sd = (HsfSide)si2;
            const HsfServerIoTempHum* th = HsfServerIo_tempHum(sd);
            const HsfServerIoFlowMeter* fm = HsfServerIo_flowMeter(sd);
            /* [直取槽位 2026-11-10] 站点层缓存了阈值（m_staleMs，质量判定
             * 用它而不是编译期宏）；访问器返回 const，故这里从本模块自己的
             * 槽位取可写指针（对象归本模块所有）。 */
            HsfServerIoSlot* slot = &s_io.m_sides[si2];
            int t;
            if (th) {
                XTempHumTransmitter* dev =
                    HsfServerIoTempHum_device(th);
                const uint32_t ms = hsfIoStaleMs(HSF_TEMPHUM_STALE_MS,
                                                 want.temphumPollMs);
                if (dev) XTempHumTransmitter_setStaleMs(dev, ms);
                /* 站点层同步（质量判定口径），漏了就会「设备层不陈旧、
                 * 站点层判陈旧」⇒ 界面照抖。 */
                if (slot->m_tempHum.m_mounted)
                    slot->m_tempHum.m_staleMs = ms;
            }
            if (fm) {
                XFlowMeter* dev = HsfServerIoFlowMeter_device(fm);
                const uint32_t ms = hsfIoStaleMs(HSF_FLOWMETER_STALE_MS,
                                                 want.flowmeterPollMs);
                if (dev) XFlowMeter_setStaleMs(dev, ms);
                if (slot->m_flowMeter.m_mounted)
                    slot->m_flowMeter.m_staleMs = ms;
            }
            for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
                const HsfServerIoTurbidity* tb = HsfServerIo_turbidity(sd, t);
                XTurbidityMeter* dev;
                const int pollMs = (t == 0) ? want.turbidity1PollMs
                                            : want.turbidity2PollMs;
                const uint32_t ms = hsfIoStaleMs(HSF_TURBIDITY_STALE_MS, pollMs);
                if (!tb) continue;
                dev = HsfServerIoTurbidity_device(tb);
                if (dev) XTurbidityMeter_setStaleMs(dev, ms);
                if (slot->m_turbidity[t].m_mounted)
                    slot->m_turbidity[t].m_staleMs = ms;
            }
            /* [变频器陈旧阈值随**轮转周期** 2026-11-10] 变频器与上面几个
             * 设备不同：它的数据刷新节拍不是「周期」，而是 Hub 把该总线
             * 的读组轮完一圈的时间（Hub 每拍只发一个读组 ⇒ 一圈 = 组数 ×
             * 拍长；现场 14 组 × 200ms = 2.8s）。设备层缺省阈值「发布周期 ×
             * 5」=1s 盖不住一圈 ⇒ 每隔两拍判陈旧、掩码清零 ⇒ 客户端变频器
             * 在「---」与真值之间来回闪（用户报"有时候显示 --- 然后又恢复
             * 循环"）。⇒ 阈值由站模块按组数算好（轮转 × 2）。 */
            if (slot->m_vfd.m_mounted) {
                (void)HsfServerIoVfd_applyStaleMs(&slot->m_vfd,
                                                  want.vfdPollMs);
            }
        }
    }

    /* 模拟量/数字量：各自独占一个 Hub（TCP，无总线共享问题）⇒ 直接改该
     * Hub 的设备节拍即可，不需要站模块再开新 API。 */    if (HsfServerIoAnalog_isMounted(&s_io.m_analog) && want.analogPollMs > 0) {
        XModbusHub* ahub = HsfServerIoAnalog_hub(&s_io.m_analog);
        if (ahub) {
            XModbusHub_setPollIntervalDefault(ahub, (size_t)want.analogPollMs);
            if (XModbusHub_setPollInterval(ahub, 1, (size_t)want.analogPollMs))
                any = true;
        }
    }
    if (HsfServerIoDigital_isMounted(&s_io.m_digital) && want.digitalPollMs > 0) {
        XModbusHub* dhub = HsfServerIoDigital_hub(&s_io.m_digital);
        if (dhub) {
            XModbusHub_setPollIntervalDefault(dhub, (size_t)want.digitalPollMs);
            if (XModbusHub_setPollInterval(dhub, 1, (size_t)want.digitalPollMs))
                any = true;
        }
    }

    XPrintf("HsfServerIo: 采集节拍已应用（变频器=%d 温湿度=%d 浊度1=%d(%s) "
            "浊度2=%d(%s) 流量计=%d 模拟量=%d 数字量=%d ms）\n",
            want.vfdPollMs, want.temphumPollMs, want.turbidity1PollMs,
            want.turbidity1Enabled ? "开" : "停",
            want.turbidity2PollMs,
            want.turbidity2Enabled ? "开" : "停",
            want.flowmeterPollMs,
            want.analogPollMs, want.digitalPollMs);
    /* [陈旧阈值随周期走 2026-11-10] 一并打印有效阈值：阈值 < 周期 ⇒
     * 两拍之间值必被判陈旧 ⇒ 界面恒 "--"（现场实锅：温湿度周期改 1 分钟
     * 后几秒就 "--"）。这行是排查该类问题的第一手证据。 */
    XPrintf("HsfServerIo: 陈旧阈值已随周期联动（温湿度=%u 浊度1=%u 浊度2=%u "
            "流量计=%u 变频器=%u ms）\n",
            hsfIoStaleMs(HSF_TEMPHUM_STALE_MS, want.temphumPollMs),
            hsfIoStaleMs(HSF_TURBIDITY_STALE_MS, want.turbidity1PollMs),
            hsfIoStaleMs(HSF_TURBIDITY_STALE_MS, want.turbidity2PollMs),
            hsfIoStaleMs(HSF_FLOWMETER_STALE_MS, want.flowmeterPollMs),
            /* 变频器按「轮转周期」（组数 × 拍长）算，见站模块的打印行。 */
            s_io.m_sides[0].m_vfd.m_staleMs);
    return any;
}

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

/* ==================== 注入链路自愈 ==================== */

/**
 * @brief 跑一拍注入链路自愈看门狗（485 两路 + 模拟量站 + 数字量站）。
 * @details [接线位置] 由 hsfAppTick 每拍调用（与阀/泵轮询同一事件循环，
 *          不存在并发）；顺序放在推数据之前，让同一拍里的编码拿到的是
 *          自愈后的状态。
 *
 *          [为什么必须在装配层统一驱动] 四个 Hub 各自独立、互不知道对方；
 *          看门狗是"服务端对链路的兜底"，天然属于编排层——放在任何单站
 *          模块里都会漏掉其它站。各站自己只提供"我这条链路怎么重连"的
 *          描述（hub/devId/client/节拍），策略实现全在 HsfServerBusHeal。
 *
 *          [不碰物理链路] 每个 *Heal* 状态只在注入装配入口 enable，
 *          物理串口/真机 TCP 装配下 m_on=false，本函数是纯空转。
 */
bool HsfServerIo_watchdog(uint64_t nowMs)
{
    HsfServerIoState* io = &s_io;
    bool acted = false;
    int s;

    if (!io->m_started) return false;

    for (s = 0; s < (int)HsfSide_Count; ++s) {
        if (HsfServerBus_watchdog(&io->m_sides[s].m_bus, nowMs))
            acted = true;
    }
    if (HsfServerIoAnalog_watchdog(&io->m_analog, nowMs)) acted = true;
    if (HsfServerIoDigital_watchdog(&io->m_digital, nowMs)) acted = true;
    return acted;
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

HsfSide HsfServerIo_turbiditySide(HsfSide requested)
{
    int i;
    int t;
    /* 请求侧装了就用它（客户端偶尔会显式点名某侧）。 */
    for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
        if (HsfServerIo_turbidity(requested, t)) return requested;
    }
    /* 否则回落到实际装配的另一侧。 */
    for (i = 0; i < (int)HsfSide_Count; ++i) {
        HsfSide s = (HsfSide)i;
        if (s == requested) continue;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
            if (HsfServerIo_turbidity(s, t)) return s;
        }
    }
    return requested;
}

HsfSide HsfServerIo_flowMeterSide(HsfSide requested)
{
    int i;
    if (HsfServerIo_flowMeter(requested)) return requested;
    for (i = 0; i < (int)HsfSide_Count; ++i) {
        HsfSide s = (HsfSide)i;
        if (s == requested) continue;
        if (HsfServerIo_flowMeter(s)) return s;
    }
    return requested;
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
    uint32_t doMask = 0;
    bool valid = false;
    bool doValid = false;
    int i;
    int count;
    int channels;

    if (!s_io.m_started) return;
    count = HsfServerIoValve_count(&s_io.m_valves);
    if (count <= 0) return;
    if (!HsfServerIo_digitalReady()) {
        /* 数字量站没了：把反馈标成"全部不新鲜"（保留状态，界面按"模块离线"
         * 呈现），绝不在这里伪造 0 反馈——那会把六个阀全判成"动作中/未知"。 */
        HsfServerIoValve_poll(&s_io.m_valves, 0, false, 0u, 0, false, nowMs);
        return;
    }
    HsfServerIoDigital_readDi(&s_io.m_digital, &di, &valid);
    /* DO 回显：外部改动接管用（面板/现场按钮绕过本服务动线圈时，状态机
     * 要能解释"没有对应命令的动作中"）。 */
    HsfServerIoDigital_readDo(&s_io.m_digital, &doMask, &doValid);

    /* [新鲜度必须按通道给，不能是整站一个 bool] 聚合架構下 DI 是**多台模块**
     * 拼出来的（主模块提供 0..15、扩展模块提供 16..31），两台在线状态互相
     * 独立，电磁阀也可能一部分挂这一半区、一部分挂那一半。
     * 若整站给一个 bool，扩展模块掉线时主模块仍正常应答 ⇒ 整站恒 true ⇒
     * 挂在扩展那半区的几路读到恒 0 却仍算有效，界面显示"无到位反馈（检查
     * 行程开关接线）"——而真相是模块掉线，提示把人引向错误方向。
     * 按通道给位图后，每路用自己的两个 DI 通道各自判定。
     *
     * [当前现场] 只剩 .13 一台 16 点（HSF_DIG_HOST_EXT 为空），位图恒为
     * 0..15 全 fresh；按通道的逻辑保留，将来再挂扩展模块即自动生效。 */
    channels = HsfServerIo_digitalCount();
    if (channels > 32) channels = 32;
    for (i = 0; i < channels; ++i) {
        if (HsfServerIoDigital_isDiFresh(&s_io.m_digital, i))
            fresh |= (1u << i);
    }
    HsfServerIoValve_poll(&s_io.m_valves, di, valid, fresh, doMask, doValid,
                          nowMs);
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
    /* [模块离线 ⇒ 明确报错，不要伪装成"阀卡住"] 聚合架构下若某路阀挂在
     * 扩展模块那半区（逻辑点 16..31），通信由扩展模块提供。它掉线后主模块
     * 仍正常应答，DO 写会落到一个不存在的通道上——若放行，界面先显示
     * "动作中"、超时后显示"动作超时未到位"，**编造出"阀卡住"的假象**。
     * 故这里按"该路自己的 DO/DI 通道是否新鲜"判，离线直接给出结论。
     * 拒绝点：DO 通道不新鲜即拒（写不进去）；反馈通道不新鲜也拒——
     * 写了也验证不了是否到位，等于盲操作。
     * [当前现场] 单台 .13 全部 16 点同源，这条退化为"整站在线与否"；
     * 逻辑保留，将来加扩展模块时无需再改。 */
    if (!HsfServerIoDigital_isDiFresh(&s_io.m_digital, it->m_openDi) ||
        !HsfServerIoDigital_isDiFresh(&s_io.m_digital, it->m_closeDi)) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "反馈模块离线，无法控制（检查扩展模块在线与接线）");
        return false;
    }
    /* [ignored 处置不出帧] 已到位同目标的重复命令在 setOutput 里已判
     * 幂等忽略——这里必须跳过 DO 写，否则白写一次还会重置计时/重启行程
     * （Python 侧同分支"不写 DO、不重置计时器"）。 */
    if (it->m_cmdResult[0] != '\0' &&
        strcmp(it->m_cmdResult, "ignored") == 0) {
        return true;
    }
    /* [动作出口] 单点写对应 DO：开=写 1、关=写 0（弹簧复位/双电控在软件层
     * 表现一致，差别只在现场继电器接线——见 Python valves.py 头注释）。 */
    ok = HsfServerIoDigital_writeDo(&s_io.m_digital, it->m_do, wantOpen);
    if (ok) {
        /* [同步自有写入基准] 否则下一拍 poll 把刚写的 DO 当外部改动，
         * 凭空多一条 external 接管。 */
        HsfServerIoValve_noteOwnWrite(&s_io.m_valves, index, wantOpen);
    }
    return ok;
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
