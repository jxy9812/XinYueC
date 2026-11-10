
/**
 * @file       HsfServerBus.c
 * @brief      服务端 485 总线容器实现。
 * @details    建链顺序对齐 XModbusRtuSerialClient 的约束：串口对象在
 *             connectDevice()（open）里才物化，**参数只能经连接参数表在
 *             open 之前设置**；open 之后再改 XSerialPort_* 不生效。
 *             因此本实现在 open 之前把串口名/波特率/数据位/校验/停止位
 *             全部写进连接参数表。
 */
#include "HsfServerBus.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XModbusClient.h"
#include "XSerialPort.h"
#include "XVariant.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include <string.h>

/* ==================== 内部辅助 ==================== */

/* ---- 注入客户端自愈看门狗的阈值与节奏（实现在文件末同名段落） ---- */
/** @brief 停滞判定下限（毫秒）：低于它就误报（正常抖动/GC 级别的停顿）。 */
#define HSF_HEAL_STALE_MIN_MS   2000u
/** @brief 停滞判定上限（毫秒）：必须 ≤ 上层 5000ms 陈旧阈值，否则救不回来。 */
#define HSF_HEAL_STALE_MAX_MS   5000u
/** @brief 停滞阈值 = 轮询节拍 × 该倍数（20ms 节拍 ⇒ 400ms，夹到下限 2000）。 */
#define HSF_HEAL_STALE_MULT     20u
/** @brief 首次重试间隔（毫秒）：失联第一拍就动手，不等。 */
#define HSF_HEAL_BACKOFF_MIN_MS 500u
/** @brief 退避上限（毫秒）：再久也要继续试（模拟链路重连是零成本）。 */
#define HSF_HEAL_BACKOFF_MAX_MS 10000u
/** @brief "重试中"日志限频窗口（毫秒）：30s 一条，避免刷屏。 */
#define HSF_HEAL_LOG_GAP_MS     30000u
/** @brief 连续失败达到该次数后做一次断链重连（阶梯④）。 */
#define HSF_HEAL_HARD_CYCLE_AT  4u

/* [自愈看门狗内部辅助] 实现在文件末「注入客户端自愈看门狗」段；
 * 这里前向声明是因为 HsfServerBus_start 起看门狗时要打印停滞阈值。 */
static uint64_t healStaleMs(int pollMs);

/**
 * @brief 建连接参数变体并写入设备连接参数表。
 * @param dev 目标设备；param 参数键；isStr 值类型（true=utf8，false=int）。
 * @return 写入成功返回 true。
 */
static bool busSetConnParam(XModbusDevice* dev, int param, bool isStr,
                            int i, const char* str)
{
    XVariant* v = isStr ? XVariant_create_utf8_str(str)
                        : XVariant_create_int(i);
    if (!v) return false;
    /* 拷贝语义：value 为借用，设备内深拷贝后不取得所有权，故设完由本函数
       释放自建对象；该 API 无返回值，参数非法时设备保持不变。 */
    XModbusDevice_setConnectionParameter(dev, (XModbusDevice_ConnectionParameter)param, v);
    XClassDelete((XClass*)v);
    return true;
}

/* ==================== 公开 API ==================== */

bool HsfServerBus_open(HsfServerBus* bus, const char* name, const char* port,
                       int baud, int timeoutMs, int retries)
{
    XModbusRtuSerialClient* client;

    if (!bus) return false;
    memset(bus, 0, sizeof(*bus));
    bus->m_devId = -1;
    if (!port || !port[0]) return false;
    bus->m_name = name ? name : "485";
    bus->m_port = port;

    bus->m_hub = XModbusHub_create();
    if (!bus->m_hub) {
        XPrintf("HsfServerBus: [%s] Hub 创建失败\n", bus->m_name);
        goto fail;
    }
    XModbusHub_setWriteCoalesceMs(bus->m_hub, 0);   /* 命令字受理即下发。 */

    client = XModbusRtuSerialClient_create();
    if (!client) {
        XPrintf("HsfServerBus: [%s] RTU 客户端创建失败\n", bus->m_name);
        goto fail;
    }
    /* 串口参数必须在 connectDevice 之前经连接参数表设置（open 时物化）。 */
    busSetConnParam((XModbusDevice*)client, XModbusDevice_SerialPortNameParameter,
                    true, 0, port);
    busSetConnParam((XModbusDevice*)client, XModbusDevice_SerialBaudRateParameter,
                    false, baud, NULL);
    busSetConnParam((XModbusDevice*)client, XModbusDevice_SerialDataBitsParameter,
                    false, (int)XSerialPort_Data8, NULL);
    busSetConnParam((XModbusDevice*)client, XModbusDevice_SerialParityParameter,
                    false, (int)XSerialPort_NoParity, NULL);
    busSetConnParam((XModbusDevice*)client, XModbusDevice_SerialStopBitsParameter,
                    false, (int)XSerialPort_OneStop, NULL);
    XModbusClient_setTimeout((XModbusClient*)client, timeoutMs);
    XModbusClient_setNumberOfRetries((XModbusClient*)client, retries);
    /* [口径声明] 物理串口路径**不自建自愈看门狗**：线松了由 XModbusRtuSerialClient
     * 自己的 attemptReconnect 处理（Hub::addDevice 会把 autoReconnect 打开，
     * 所以这里的 false 只是"本模块不额外干预"的声明，实际值以 Hub 装配为准）。 */
    XModbusClient_setAutoReconnect((XModbusClient*)client, false);
    if (!XModbusDevice_connectDevice((XModbusDevice*)client)) {
        XPrintf("HsfServerBus: [%s] 串口 %s 打开失败（错误码=%d）——整路跳过，"
                "插回后重启服务端\n", bus->m_name, port,
                (int)XModbusDevice_error((XModbusDevice*)client));
        XClassDelete((XClass*)client);      /* 未入 Hub，自持所有权。 */
        goto fail;
    }
    bus->m_devId = XModbusHub_addDevice_2(bus->m_hub, (XModbusClient*)client,
                                          bus->m_name);
    if (bus->m_devId < 0) {
        XPrintf("HsfServerBus: [%s] 客户端挂 Hub 失败\n", bus->m_name);
        XClassDelete((XClass*)client);      /* addDevice 失败不接管。 */
        goto fail;
    }
    bus->m_client = client;
    XPrintf("HsfServerBus: [%s] %s 已打开（%d 8N1，超时 %dms，重试 %d）\n",
            bus->m_name, port, baud, timeoutMs, retries);
    return true;

fail:
    /* 不留半成品：Hub 已建的释放（此时还没有客户端入编）。 */
    if (bus->m_hub) {
        XClassDelete((XClass*)bus->m_hub);
        bus->m_hub = NULL;
    }
    bus->m_client = NULL;
    bus->m_started = false;
    return false;
}

bool HsfServerBus_openWithClient(HsfServerBus* bus, const char* name,
                                 XModbusClient* client, int timeoutMs,
                                 int retries)
{
    if (!bus || !client) return false;
    memset(bus, 0, sizeof(*bus));
    bus->m_devId = -1;
    bus->m_name = name ? name : "485";
    bus->m_port = NULL;             /* 无物理串口（模拟/注入）。 */

    bus->m_hub = XModbusHub_create();
    if (!bus->m_hub) {
        XPrintf("HsfServerBus: [%s] Hub 创建失败\n", bus->m_name);
        XClassDelete((XClass*)client);   /* 未被接管，按所有权约定释放。 */
        return false;
    }
    XModbusHub_setWriteCoalesceMs(bus->m_hub, 0);   /* 命令字受理即下发。 */

    XModbusClient_setTimeout(client, (size_t)timeoutMs);
    XModbusClient_setNumberOfRetries(client, retries);
    /* [注入链路必须自愈] 现场实测：注入的客户端一旦被判失联，Hub 的泵被
     * `state != Connected` 门槛永久挡住，温湿度/流量计/阀反馈全部变"无数据"，
     * 只有重启服务端能恢复。autoReconnect 对虚拟客户端是空操作（它没有
     * 重连实现），真正的兜底是 HsfServerBus_start 里挂上的看门狗；
     * 对注入的 RTU/TCP 客户端则让底层先自己重试。 */
    XModbusClient_setAutoReconnect(client, true);
    XModbusClient_setReconnectInterval(client, (size_t)HSF_HEAL_BACKOFF_MIN_MS);
    if (!XModbusDevice_connectDevice((XModbusDevice*)client)) {
        /* 注入的客户端 Open 失败：虚拟客户端的 forced-offline 会走到这里，
         * 正是"模拟模块不在线"的预期表现，如实上报。 */
        XPrintf("HsfServerBus: [%s] 注入客户端连接失败（错误码=%d）\n",
                bus->m_name, (int)XModbusDevice_error((XModbusDevice*)client));
        XClassDelete((XClass*)client);
        XClassDelete((XClass*)bus->m_hub);
        bus->m_hub = NULL;
        return false;
    }
    bus->m_devId = XModbusHub_addDevice_2(bus->m_hub, client, bus->m_name);
    if (bus->m_devId < 0) {
        XPrintf("HsfServerBus: [%s] 客户端挂 Hub 失败\n", bus->m_name);
        XClassDelete((XClass*)client);      /* addDevice 失败不接管。 */
        XClassDelete((XClass*)bus->m_hub);
        bus->m_hub = NULL;
        return false;
    }
    bus->m_client = client;
    bus->m_injected = true;         /* 标记注入链路：start 时挂自愈看门狗。 */
    XPrintf("HsfServerBus: [%s] 已接入注入客户端（超时 %dms，重试 %d）\n",
            bus->m_name, timeoutMs, retries);
    return true;
}

XModbusHub* HsfServerBus_hub(const HsfServerBus* bus)
{
    return bus ? bus->m_hub : NULL;
}

XModbusMapping* HsfServerBus_createMapping(const HsfServerBus* bus)
{
    if (!bus || !bus->m_hub) return NULL;
    return XModbusMapping_create();
}

int HsfServerBus_deviceId(const HsfServerBus* bus)
{
    return bus ? bus->m_devId : -1;
}

bool HsfServerBus_start(HsfServerBus* bus, XModbusMapping* map, int pollMs)
{
    if (!bus || !bus->m_hub) {
        if (map) XClassDelete((XClass*)map);
        return false;
    }
    if (!map) {
        XPrintf("HsfServerBus: [%s] 映射表为空\n", bus->m_name);
        return false;
    }
    /* 提交映射（所有权移交 Hub；Hub 内部据此重建缓存块与读组）。 */
    XModbusHub_setMapping(bus->m_hub, map);
    bus->m_map = NULL;
    XModbusHub_setPollAreas(bus->m_hub,
                            (1u << XModbusDiscreteInputs) |
                            (1u << XModbusCoils) |
                            (1u << XModbusInputRegisters) |
                            (1u << XModbusHoldingRegisters));
    XModbusHub_setPollIntervalDefault(bus->m_hub, (size_t)pollMs);
    XModbusHub_setPollInterval(bus->m_hub, bus->m_devId, (size_t)pollMs);
    XModbusHub_connectAll(bus->m_hub);
    XModbusHub_startPolling(bus->m_hub);
    bus->m_started = true;
    /* [注入链路：挂自愈看门狗] 节拍此刻才确定（重挂定时器要用它），故在
     * start 而不是 openWithClient 里启用。物理串口（m_injected=false）不挂。 */
    if (bus->m_injected) {
        HsfServerBusHeal_arm(&bus->m_heal, (XModbusClient*)bus->m_client,
                             pollMs,
                             (uint64_t)XDateTime_currentMSecsSinceEpoch());
        XPrintf("HsfServerBus: [%s] 注入链路自愈已启用（失联重连 + 数据停滞"
                "重挂定时器；停滞阈值 %llu ms，退避 %u~%u ms）\n",
                bus->m_name,
                (unsigned long long)healStaleMs(pollMs),
                (unsigned)HSF_HEAL_BACKOFF_MIN_MS,
                (unsigned)HSF_HEAL_BACKOFF_MAX_MS);
    }
    XPrintf("HsfServerBus: [%s] 轮询已启动（节拍 %dms）\n", bus->m_name, pollMs);
    return true;
}

void HsfServerBus_stop(HsfServerBus* bus)
{
    if (!bus) return;
    HsfServerBusHeal_disarm(&bus->m_heal);
    bus->m_injected = false;
    if (bus->m_hub) {
        XModbusHub_stopPolling(bus->m_hub);
        XModbusHub_disconnectAll(bus->m_hub);
        XClassDelete((XClass*)bus->m_hub);   /* 客户端与映射随 Hub 释放。 */
        bus->m_hub = NULL;
    }
    bus->m_client = NULL;
    bus->m_map = NULL;
    bus->m_devId = -1;
    bus->m_started = false;
}

bool HsfServerBus_isOpen(const HsfServerBus* bus)
{
    return bus && bus->m_hub != NULL && bus->m_devId >= 0;
}

/* ==================== 注入客户端自愈看门狗 ==================== */

/*
 * [为什么需要这套东西] 现场实测（HsfServer.exe --sim 长时间运行 + 万次
 * 连接/断开压测）：首页「电箱温湿度」变 `--℃ / --%RH`、「流量计 无数据」、
 * 「电磁阀 · 6 路」间歇陈旧；重启服务端即恢复。查证结论：
 *   · Hub 的泵（hubPumpDevice）对 `state != Connected` 的设备**安静跳过**；
 *   · 注入的 XModbusVirtualClient 只把 Open/Close 翻译成状态字，
 *     **没有任何重连实现**（RTU/TCP 客户端的 attemptReconnect 它没有），
 *     XModbusClient_setAutoReconnect 对它是纯空操作；
 *   · 于是设备一旦被判失联/轮询定时器一旦停摆，读组再也不提交，
 *     上层（pointValid，阈值 5000ms）只能显示"无数据"，直到重启进程。
 * 看门狗就是补这条缺失的恢复路径，且只用可观测量（状态 + 最后成功应答时刻）
 * 作判据，不依赖具体客户端类型。
 */

/**
 * @brief 由轮询节拍推停滞阈值（夹在 [2000, 5000] 毫秒）。
 */
static uint64_t healStaleMs(int pollMs)
{
    uint64_t ms = (pollMs > 0) ? (uint64_t)pollMs : 0;
    ms *= (uint64_t)HSF_HEAL_STALE_MULT;
    if (ms < (uint64_t)HSF_HEAL_STALE_MIN_MS) ms = HSF_HEAL_STALE_MIN_MS;
    if (ms > (uint64_t)HSF_HEAL_STALE_MAX_MS) ms = HSF_HEAL_STALE_MAX_MS;
    return ms;
}

/**
 * @brief 指数退避：500,1000,2000,4000,8000,10000,10000...
 * @param attempts 连续失败次数（>=1）。
 */
static uint64_t healBackoffMs(uint32_t attempts)
{
    uint64_t ms = (uint64_t)HSF_HEAL_BACKOFF_MIN_MS;
    uint32_t i;
    for (i = 1; i < attempts && ms < (uint64_t)HSF_HEAL_BACKOFF_MAX_MS; ++i)
        ms <<= 1;
    if (ms > (uint64_t)HSF_HEAL_BACKOFF_MAX_MS) ms = (uint64_t)HSF_HEAL_BACKOFF_MAX_MS;
    return ms;
}

/**
 * @brief 限频判定：允许打印时返回 true 并刷新窗口，否则把次数折叠计数。
 * @note  限频只作用于"还在失败"的重复日志；失联/恢复的**跃迁沿**不受限
 *        （那才是现场需要看到的结论）。
 */
static bool healLogAllowed(HsfServerBusHeal* heal, uint64_t nowMs)
{
    if (heal->m_lastLogMs != 0 &&
        nowMs >= heal->m_lastLogMs &&
        nowMs - heal->m_lastLogMs < (uint64_t)HSF_HEAL_LOG_GAP_MS) {
        ++heal->m_suppressed;
        return false;
    }
    heal->m_lastLogMs = nowMs;
    return true;
}

/**
 * @brief 清空一次异常的所有记账（恢复/重新启用时复用）。
 * @note  保留 m_on / m_pollMs / m_armedMs（配置，不随恢复清零）。
 */
static void healResetEpisode(HsfServerBusHeal* heal)
{
    heal->m_lost = false;
    heal->m_stalled = false;
    heal->m_attempts = 0;
    heal->m_sinceMs = 0;
    heal->m_nextTryMs = 0;
    heal->m_lastLogMs = 0;
    heal->m_suppressed = 0;
}

void HsfServerBusHeal_arm(HsfServerBusHeal* heal, XModbusClient* client,
                          int pollMs, uint64_t nowMs)
{
    if (!heal) return;
    memset(heal, 0, sizeof(*heal));
    heal->m_on = true;
    heal->m_pollMs = (pollMs > 0) ? pollMs : 200;
    heal->m_armedMs = nowMs;
    if (client) {
        /* 底层自带重连实现的客户端（注入的 RTU/TCP）先自己试；
         * 虚拟客户端忽略该开关（无重连实现），由本看门狗兜底。 */
        XModbusClient_setAutoReconnect(client, true);
        XModbusClient_setReconnectInterval(client, (size_t)HSF_HEAL_BACKOFF_MIN_MS);
    }
}

void HsfServerBusHeal_disarm(HsfServerBusHeal* heal)
{
    if (!heal) return;
    memset(heal, 0, sizeof(*heal));
}

bool HsfServerBusHeal_step(HsfServerBusHeal* heal, XModbusHub* hub,
                           int devId, XModbusClient* client, const char* name,
                           uint64_t nowMs)
{
    XModbusDevice_State state;
    uint64_t lastOk;
    uint64_t refMs;
    uint64_t staleMs;
    uint64_t elapsed;
    uint64_t backoffMs;
    const char* devName;

    if (!heal || !heal->m_on || !hub || !client || devId < 0) return false;
    devName = name ? name : "io";

    staleMs = healStaleMs(heal->m_pollMs);
    state = XModbusHub_deviceState(hub, devId);
    lastOk = XModbusHub_deviceLastSuccessMs(hub, devId);
    refMs = (lastOk != 0) ? lastOk : heal->m_armedMs;
    elapsed = (nowMs > refMs) ? (nowMs - refMs) : 0;
    /* [按从站粒度判停滞 2026-11-09 根修] 设备级"最后成功应答"会被**同一
     * 设备上其它从站的成功掩盖**：一条 485 线上挂变频器+温湿度两台，变频器
     * 一成功，设备级计时器就一直新鲜 ⇒ 温湿度那台从站停止刷新也看不出来
     * （现场实证：温湿度恒"无数据"、看门狗全程无告警、只有重启服务端
     * 才恢复）。改用"最陈旧从站"的块级新鲜度（块 lastOkMs = 这个点最后
     * 一次真正被回填的时刻）作为停滞判据：从站从未回填（UINT64_MAX）
     * 时仍退回设备级口径，避免刚装配完就误判停滞。 */
    {
        int worstUnit = -1;
        uint64_t worstAge = 0;
        if (XModbusHub_deviceWorstUnitAge(hub, devId, &worstUnit, &worstAge) &&
            worstAge != UINT64_MAX && worstAge > elapsed) {
            elapsed = worstAge;
        }
    }

    /* ---- 健康：状态在线且数据在阈值内刷新过 ⇒ 只补跃迁沿日志 ---- */
    if (state == XModbusDevice_ConnectedState && elapsed <= staleMs) {
        const uint64_t lasted =
            (heal->m_sinceMs != 0 && nowMs > heal->m_sinceMs)
                ? (nowMs - heal->m_sinceMs) : 0;
        /* [毛刺过滤] 事件循环偶发阻塞（落盘/大帧编码）也可能让"距上次成功"
         * 越界一拍；这类一跳即恢复的抖动不当作一次自愈事件——否则日志成对
         * 刷屏，真正的故障反而被埋掉。只有持续超过一个停滞阈值才留恢复痕。 */
        if (heal->m_lost || (heal->m_stalled && lasted >= staleMs)) {
            XPrintf("HsfServerBus: [%s] 自愈恢复：链路在线且数据已刷新"
                    "（中断 %llu ms，共尝试 %u 次）\n",
                    devName, (unsigned long long)lasted, heal->m_attempts);
        }
        healResetEpisode(heal);
        return false;
    }

    if (heal->m_sinceMs == 0) heal->m_sinceMs = nowMs;

    /* ---- ① 轮询总闸不在 ⇒ 重开（最便宜且无副作用；正常不会走到） ---- */
    if (!XModbusHub_isPolling(hub)) {
        XModbusHub_startPolling(hub);
        XPrintf("HsfServerBus: [%s] 轮询总闸已关（异常），已重新启动轮询\n",
                devName);
    }

    /* ---- 退避闸：未到点不做重动作（仍保留上面的总闸兜底） ---- */
    if (nowMs < heal->m_nextTryMs) return false;

    /* ---- 底层自己正在拨号（仅注入的 TCP 客户端会出现）：不抢它的节奏 ---- */
    if (state == XModbusDevice_ConnectingState) {
        if (healLogAllowed(heal, nowMs)) {
            XPrintf("HsfServerBus: [%s] 底层仍在连接中（已 %llu ms 无成功应答），"
                    "等它自己完成；若长期停在此态请查适配器/网线\n",
                    devName,
                    (unsigned long long)((nowMs > heal->m_sinceMs)
                                             ? (nowMs - heal->m_sinceMs) : 0));
        }
        return false;
    }

    ++heal->m_attempts;

    if (state != XModbusDevice_ConnectedState) {
        uint32_t suppressed = heal->m_suppressed;
        /* ---- ② 失联：直接重连（虚拟客户端 Open 即 Connected，零成本） ---- */
        if (XModbusDevice_connectDevice((XModbusDevice*)client)) {
            XPrintf("HsfServerBus: [%s] 重连成功（第 %u 次尝试，中断 %llu ms）\n",
                    devName, heal->m_attempts,
                    (unsigned long long)(nowMs - heal->m_sinceMs));
            healResetEpisode(heal);
            /* [首帧宽限] 刚连上还没轮询到第一帧，数据必然"陈旧"；
             * 给一个停滞阈值的窗口，避免误判成"在线但停滞"再折腾一次。 */
            heal->m_nextTryMs = nowMs + staleMs;
            return true;
        }
        backoffMs = healBackoffMs(heal->m_attempts);
        heal->m_nextTryMs = nowMs + backoffMs;
        if (!heal->m_lost) {
            heal->m_lost = true;
            XPrintf("HsfServerBus: [%s] 注入链路失联（状态=%d，错误码=%d），"
                    "启用自愈重连（退避 %llu ms，上限 %u ms）\n",
                    devName, (int)state,
                    (int)XModbusDevice_error((XModbusDevice*)client),
                    (unsigned long long)backoffMs,
                    (unsigned)HSF_HEAL_BACKOFF_MAX_MS);
        } else if (healLogAllowed(heal, nowMs)) {
            XPrintf("HsfServerBus: [%s] 重连仍未成功（错误码=%d，累计 %u 次），"
                    "%llu ms 后重试%s\n",
                    devName,
                    (int)XModbusDevice_error((XModbusDevice*)client),
                    heal->m_attempts,
                    (unsigned long long)backoffMs,
                    suppressed ? "（此前若干次被限频折叠）" : "");
        }
        return true;
    }

    /* ---- ③ 在线但数据停滞：重挂设备轮询定时器 ---- */
    /* [为什么用 setPollInterval 而不是 stopPolling/startPolling]
     * stop/start 会 hubRebuildAll：块表被重建，而在途应答是按**块下标**
     * 回填的，重建与在途事务并发时会写错块（UAF/串值）。setPollInterval
     * 只 kill+start 该设备的周期定时器，块表、在途事务、脏区间全不动，
     * 足以恢复"定时器被误杀/丢失"这一类静默停摆。 */
    heal->m_stalled = true;
    (void)XModbusHub_setPollInterval(hub, devId, (size_t)heal->m_pollMs);

    /* ---- ④ 连续失败：断链再连，强制一次状态跃迁 ---- */
    if (heal->m_attempts >= (uint32_t)HSF_HEAL_HARD_CYCLE_AT) {
        /* 跃迁会经 Hub 的 stateChanged 槽触发"上线续泵/补写"路径；
         * 对虚拟客户端瞬时完成，对注入的 TCP/RTU 客户端即重拨。 */
        XModbusDevice_disconnectDevice((XModbusDevice*)client);
        (void)XModbusDevice_connectDevice((XModbusDevice*)client);
    }

    backoffMs = healBackoffMs(heal->m_attempts);
    heal->m_nextTryMs = nowMs + backoffMs;
    if (healLogAllowed(heal, nowMs)) {
        XPrintf("HsfServerBus: [%s] 数据停滞 %llu ms（设备报在线，%s %llu ms；"
                "%s），重挂 %d ms 轮询定时器，第 %u 次，%llu ms 后再查\n",
                devName,
                (unsigned long long)(nowMs - heal->m_sinceMs),
                (lastOk != 0) ? "距上次成功应答" : "自看门狗启用",
                (unsigned long long)elapsed,
                (heal->m_attempts >= (uint32_t)HSF_HEAL_HARD_CYCLE_AT)
                    ? "已做一次断链重连" : "暂不动链路",
                heal->m_pollMs, heal->m_attempts,
                (unsigned long long)backoffMs);
    }
    return true;
}

bool HsfServerBus_watchdog(HsfServerBus* bus, uint64_t nowMs)
{
    if (!bus || !bus->m_injected || !bus->m_heal.m_on) return false;
    return HsfServerBusHeal_step(&bus->m_heal, bus->m_hub, bus->m_devId,
                                 (XModbusClient*)bus->m_client, bus->m_name,
                                 nowMs);
}
