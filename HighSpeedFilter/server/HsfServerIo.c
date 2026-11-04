
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

#include "XModbusHub.h"
#include "XVfdM512.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 模块状态 ==================== */

/**
 * @brief 一侧的运行时装配（总线 + 该路上的各站）。
 */
typedef struct HsfServerIoSlot {
    HsfServerBus m_bus;             /**< 该侧的 485 总线。 */
    HsfServerIoVfd m_vfd;           /**< 变频器站（m_mounted=0 表示未挂）。 */
    HsfServerIoTempHum m_tempHum;   /**< 温湿度站（m_mounted=0 表示未挂）。 */
    HsfServerIoFlowMeter m_flowMeter; /**< 电磁流量计站（m_mounted=0 表示未挂）。 */
    bool m_ready;                   /**< 该侧可用（总线已开且至少一站挂上）。 */
} HsfServerIoSlot;

/**
 * @brief 模块单例状态。
 */
typedef struct HsfServerIoState {
    bool m_started;                     /**< 已启动标志（幂等保护）。 */
    HsfServerIoSlot m_sides[HsfSide_Count]; /**< 两路 485 侧。 */
    HsfServerIoAnalog m_analog;         /**< 模拟量站（TCP，与 485 无关）。 */
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
} HsfServerIoBusParam;

/** @brief 路 A 参数（现役：变频器 + 温湿度共用同一条 485）。 */
static const HsfServerIoBusParam s_busParamA = {
    "485-A", HSF_BUS_A_PORT, HSF_BUS_A_BAUDRATE, HSF_BUS_A_TIMEOUT_MS,
    HSF_BUS_A_RETRIES, HSF_BUS_A_SCAN_MS,
    HSF_BUS_A_HAS_VFD != 0, HSF_BUS_A_HAS_TEMPHUM != 0,
    HSF_BUS_A_HAS_FLOWMETER != 0
};

/** @brief 路 B 参数（电磁流量计；COM18）。 */
static const HsfServerIoBusParam s_busParamB = {
    "485-B", HSF_BUS_B_PORT, HSF_BUS_B_BAUDRATE, HSF_BUS_B_TIMEOUT_MS,
    HSF_BUS_B_RETRIES, HSF_BUS_B_SCAN_MS,
    HSF_BUS_B_HAS_VFD != 0, HSF_BUS_B_HAS_TEMPHUM != 0,
    HSF_BUS_B_HAS_FLOWMETER != 0
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
    slot->m_ready = false;

    if (!HsfServerBus_open(&slot->m_bus, p->m_name, p->m_port, p->m_baud,
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
            "节拍=%dms/读组）\n",
            p->m_name, side == HsfSide_A ? 'A' : 'B',
            HsfServerIoVfd_isMounted(&slot->m_vfd) ? "OK" : "无",
            HsfServerIoTempHum_isMounted(&slot->m_tempHum) ? "OK" : "无",
            HsfServerIoFlowMeter_isMounted(&slot->m_flowMeter) ? "OK" : "无",
            p->m_scanMs);
    return true;

fail:
    /* 站先拆（退订 Hub 信号），总线后拆——顺序见文件头。 */
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
    if (!slot) return;
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

    /* ---- 路 B：预留（浊度仪 ×2 + 流量计；宏关时不建任何对象） ---- */
    if (HSF_BUS_B_ENABLED) {
        (void)hsfIoMountSide(HsfSide_B, &io->m_sides[HsfSide_B], &s_busParamB);
    } else {
        XPrintf("HsfServerIo: 路 B 预留未启用（%s）\n", HSF_BUS_B_PORT);
    }

    /* ---- 模拟量站（TCP，独立 Hub） ---- */
    if (!HsfServerIoAnalog_mount(&io->m_analog, HSF_AI_HOST, HSF_AI_TCP_PORT,
                                 HSF_AI_UNIT_ID, HSF_AI_CHANNELS,
                                 HSF_AI_SHUNT_OHM, HSF_AI_POLL_MS)) {
        XPrintf("HsfServerIo: 模拟量站装配失败（继续其它站）\n");
    }

    io->m_started = true;    /* 先置位：失败也走统一拆除路径，不留半成品。 */
    if (!io->m_sides[HsfSide_A].m_ready && !io->m_sides[HsfSide_B].m_ready &&
        !HsfServerIoAnalog_isMounted(&io->m_analog)) {
        XPrintf("HsfServerIo: 无任何从站装配成功\n");
        return false;
    }
    XPrintf("HsfServerIo: IO 装配完成（A=%s B=%s 模拟量=%s）\n",
            io->m_sides[HsfSide_A].m_ready ? "OK" : "跳过",
            io->m_sides[HsfSide_B].m_ready ? "OK" : "跳过",
            HsfServerIoAnalog_isMounted(&io->m_analog) ? "OK" : "跳过");
    return true;}

void HsfServerIo_stop(void)
{
    HsfServerIoState* io = &s_io;
    if (!io->m_started) return;
    HsfServerIoAnalog_unmount(&io->m_analog);
    hsfIoUnmountSide(&io->m_sides[HsfSide_B]);
    hsfIoUnmountSide(&io->m_sides[HsfSide_A]);
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
    if (!HsfServerIo_sideReady(side)) return false;
    slot = &s_io.m_sides[side];
    if (!HsfServerIoVfd_isMounted(&slot->m_vfd)) return false;
    return XModbusHub_deviceState(HsfServerBus_hub(&slot->m_bus),
                                  HsfServerBus_deviceId(&slot->m_bus)) ==
           XModbusDevice_ConnectedState;
}

const HsfServerIoTempHum* HsfServerIo_tempHum(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    if (!HsfServerIoTempHum_isMounted(&s_io.m_sides[side].m_tempHum)) return NULL;
    return &s_io.m_sides[side].m_tempHum;
}

const HsfServerIoFlowMeter* HsfServerIo_flowMeter(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    if (!HsfServerIoFlowMeter_isMounted(&s_io.m_sides[side].m_flowMeter)) return NULL;
    return &s_io.m_sides[side].m_flowMeter;
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

/* ==================== 诊断 ==================== */

XModbusHub* HsfServerIo_busHub(HsfSide side)
{
    if (!HsfServerIo_sideReady(side)) return NULL;
    return HsfServerBus_hub(&s_io.m_sides[side].m_bus);
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
