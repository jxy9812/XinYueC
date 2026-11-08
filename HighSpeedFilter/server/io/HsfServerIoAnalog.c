
/**
 * @file       HsfServerIoAnalog.c
 * @brief      服务端模拟量站实现（8 路 4-20mA，Modbus-TCP）。
 * @details    装配顺序：TCP 客户端 + 连接参数 → 挂 Hub → 建 IR 映射段 →
 *             组 + 链路 → 8 个 XIoAnalogPoint（μA 标度）→ 提交轮询。
 *             TCP 连不上不算装配失败（Hub 重连自愈）。
 */
#include "HsfServerIoAnalog.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusLink.h"
#include "XModbusTcpClient.h"
#include "XModbusDevice.h"
#include "XModbusClient.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "XVariant.h"
#include "XPrintf.h"
#include "XDateTime.h"
#include <stdio.h>
#include <string.h>

/** @brief 站内通道上限（模块物理路数）。 */
#define HSF_ANALOG_MAX_CHANNELS  8

/* ==================== 内部辅助 ==================== */

/**
 * @brief 建连接参数变体并写入设备连接参数表。
 * @return 写入成功返回 true。
 */
static bool analogSetConnParam(XModbusDevice* dev, int param, bool isStr,
                               int i, const char* str)
{
    XVariant* v = isStr ? XVariant_create_utf8_str(str)
                        : XVariant_create_int(i);
    if (!v) return false;
    /* 拷贝语义：value 为借用，设备内深拷贝后不取得所有权，故设完由本函数
       释放自建对象；该 API 无返回值，参数非法时设备保持不变。 */
    XModbusDevice_setConnectionParameter(dev,
                                         (XModbusDevice_ConnectionParameter)param, v);
    XClassDelete((XClass*)v);
    return true;
}

/* ==================== 生命周期 ==================== */

static void VHsfServerIoAnalog_deinit(XObject* obj)
{
    HsfServerIoAnalog* st = (HsfServerIoAnalog*)obj;
    if (!st) return;
    HsfServerIoAnalog_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* HsfServerIoAnalog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoAnalog)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoAnalog_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoAnalog);
    return XVTABLE_DEFAULT;
}

void HsfServerIoAnalog_init(HsfServerIoAnalog* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoAnalog_class_init();
    st->m_devId = -1;
    st->m_mounted = false;
}

/* ==================== 装配 ==================== */

/**
 * @brief 站装配核心：客户端已建好 → 挂 Hub → 登记段 → 建点/链路 → 起轮询。
 * @param st 目标站。
 * @param client 客户端（**所有权转移给 Hub**）。
 * @param unitId 从站 Unit ID。
 * @param channels 通道数（已夹到 1..8）。
 * @param shunt 采样电阻整数欧姆（已夹到 >=1）。
 * @param pollMs 轮询周期。
 * @return 成功 true；失败时已完成内部清理。
 */
static bool analogMountCore(HsfServerIoAnalog* st, XModbusClient* client,
                            int unitId, int channels, int shunt, int pollMs)
{
    XModbusMapping* map = NULL;
    int ch;

    st->m_hub = XModbusHub_create();
    map = XModbusMapping_create();
    st->m_client = client;              /* 仅用于诊断查询；所有权在 Hub。 */
    if (!st->m_hub || !map || !client) {
        XPrintf("HsfServerIoAnalog: Hub/映射/客户端创建失败\n");
        goto fail;
    }
    st->m_devId = XModbusHub_addDevice_2(st->m_hub, client, "ai-8ch");
    if (st->m_devId < 0) {
        XPrintf("HsfServerIoAnalog: 客户端挂 Hub 失败\n");
        XClassDelete((XClass*)map);
        return false;                   /* addDevice 失败不接管客户端。 */
    }
    /* 输入寄存器段：虚拟 0x0000~ ↔ 远端 0x0000~（本站独占 Hub，无冲突）。 */
    if (!XModbusMapping_addRange(map, XModbusInputRegisters, 0,
                                 (uint16_t)channels, st->m_devId, unitId, 0, 0)) {
        XPrintf("HsfServerIoAnalog: 映射段登记失败\n");
        goto fail;
    }
    st->m_group = XIoChannelGroup_create();
    st->m_link = XModbusLink_create(st->m_hub);
    if (!st->m_group || !st->m_link ||
        !XIoChannelGroup_setLink(st->m_group, (XIoLink*)st->m_link)) {
        XPrintf("HsfServerIoAnalog: 站组/链路装配失败\n");
        goto fail;
    }
    for (ch = 0; ch < channels; ++ch) {
        XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoPoint* pt;
        char tag[24];
        if (!apt) goto fail;
        pt = &apt->m_base;
        snprintf(tag, sizeof(tag), "AI%d", ch + 1);
        XIoPoint_setRole(pt, (uint16_t)ch);
        XIoPoint_setType(pt, XIoPoint_AnalogInput);
        XIoPoint_setTag(pt, tag);
        XIoPoint_setUseHolding(pt, false);       /* 输入寄存器（FC04）。 */
        /* 寄存器值=采样电压毫伏 → μA 定点：eng = raw × 1000 / 249。 */
        XIoAnalogPoint_setScale(apt, 1000, shunt, 0, 0);
        if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
            XClassDelete((XClass*)apt);
            goto fail;
        }
        if (!XIoLink_bindRole_base((XIoLink*)st->m_link, (uint16_t)ch,
                                   (uint32_t)ch, 3)) {   /* 区 3 = IR。 */
            goto fail;
        }
    }
    /* 提交映射 + 启动轮询。 */
    XModbusHub_setMapping(st->m_hub, map);
    XModbusHub_setWriteCoalesceMs(st->m_hub, 0);
    XModbusHub_setPollAreas(st->m_hub, (1u << XModbusInputRegisters));
    XModbusHub_setPollIntervalDefault(st->m_hub, (size_t)pollMs);
    XModbusHub_setPollInterval(st->m_hub, st->m_devId, (size_t)pollMs);
    if (!XIoLink_start_base((XIoLink*)st->m_link))
        XPrintf("HsfServerIoAnalog: 链路启动失败\n");
    XModbusHub_connectAll(st->m_hub);
    XModbusHub_startPolling(st->m_hub);
    st->m_channelCount = channels;
    st->m_shuntOhm = shunt;
    st->m_mounted = true;
    /* [注入链路：挂自愈看门狗] 真机装配（mount 建 TCP 客户端）不置
     * m_injected，行为与历史一致。 */
    if (st->m_injected) {
        HsfServerBusHeal_arm(&st->m_heal, client, pollMs,
                             (uint64_t)XDateTime_currentMSecsSinceEpoch());
        XPrintf("HsfServerIoAnalog: 注入链路自愈已启用（失联重连 + 数据停滞"
                "重挂 %dms 定时器）\n", pollMs);
    }
    return true;

fail:
    if (map) XClassDelete((XClass*)map);
    HsfServerIoAnalog_unmount(st);
    return false;
}

bool HsfServerIoAnalog_mount(HsfServerIoAnalog* st, const char* host, int port,
                             int unitId, int channels, double shuntOhm,
                             int pollMs)
{
    XModbusTcpClient* client;
    int shunt = (int)(shuntOhm + 0.5);

    if (!st || st->m_mounted) return false;
    if (!host || !host[0]) return false;
    if (channels <= 0) return false;
    if (channels > HSF_ANALOG_MAX_CHANNELS) channels = HSF_ANALOG_MAX_CHANNELS;
    if (shunt <= 0) shunt = 1;

    client = XModbusTcpClient_create();
    if (!client) {
        XPrintf("HsfServerIoAnalog: 客户端创建失败\n");
        return false;
    }
    analogSetConnParam((XModbusDevice*)client,
                       XModbusDevice_NetworkAddressParameter, true, 0, host);
    analogSetConnParam((XModbusDevice*)client,
                       XModbusDevice_NetworkPortParameter, false, port, NULL);
    if (!analogMountCore(st, (XModbusClient*)client, unitId, channels, shunt,
                         pollMs)) {
        return false;
    }
    XPrintf("HsfServerIoAnalog: %s:%d 装配完成（%d 路，轮询 %dms）\n",
            host, port, channels, pollMs);
    return true;
}

bool HsfServerIoAnalog_mountWithClient(HsfServerIoAnalog* st,
                                       struct XModbusClient* client,
                                       int unitId, int channels,
                                       double shuntOhm, int pollMs)
{
    int shunt = (int)(shuntOhm + 0.5);

    if (!st || st->m_mounted || !client) return false;
    if (channels <= 0) return false;
    if (channels > HSF_ANALOG_MAX_CHANNELS) channels = HSF_ANALOG_MAX_CHANNELS;
    if (shunt <= 0) shunt = 1;

    /* [注入链路标记] 客户端由调用方给（模拟设备/单测），底层可能是没有
     * 任何重连实现的虚拟客户端 ⇒ 装配尾部要挂服务端自愈看门狗。 */
    st->m_injected = true;
    if (!analogMountCore(st, (XModbusClient*)client, unitId, channels, shunt,
                         pollMs)) {
        return false;
    }
    XPrintf("HsfServerIoAnalog: 装配完成（%d 路，注入客户端，轮询 %dms）\n",
            channels, pollMs);
    return true;
}

void HsfServerIoAnalog_unmount(HsfServerIoAnalog* st)
{
    if (!st) return;
    /* 看门狗先撤：下面会把 Hub/客户端整体释放。 */
    HsfServerBusHeal_disarm(&st->m_heal);
    st->m_injected = false;
    if (st->m_hub) {
        XModbusHub_stopPolling(st->m_hub);
        XModbusHub_disconnectAll(st->m_hub);
    }
    if (st->m_group) { XClassDelete((XClass*)st->m_group); st->m_group = NULL; }
    if (st->m_link)  { XClassDelete((XClass*)st->m_link);  st->m_link = NULL; }
    if (st->m_hub)   { XClassDelete((XClass*)st->m_hub);   st->m_hub = NULL; }
    st->m_client = NULL;      /* 所有权在 Hub，随 Hub 释放。 */
    st->m_devId = -1;
    st->m_channelCount = 0;
    st->m_mounted = false;
}

/* ==================== 查询 ==================== */

int HsfServerIoAnalog_values(const HsfServerIoAnalog* st, double* outMA,
                             bool* outValid, int maxCount)
{
    int ch;
    int n;
    if (!st || !st->m_mounted || !st->m_group || !outMA) return 0;
    n = maxCount < st->m_channelCount ? maxCount : st->m_channelCount;
    for (ch = 0; ch < n; ++ch) {
        int32_t eng = 0;
        bool valid = XIoChannelGroup_isValid(st->m_group, (uint16_t)ch) &&
                     XIoChannelGroup_readAnalogInput(st->m_group,
                                                     (uint16_t)ch, &eng);
        outMA[ch] = valid ? ((double)eng / 1000.0) : 0.0;   /* μA → mA。 */
        if (outValid) outValid[ch] = valid;
    }
    return n;
}

int HsfServerIoAnalog_rawValues(const HsfServerIoAnalog* st, int32_t* outRaw,
                                bool* outValid, int maxCount)
{
    int ch;
    int n;
    if (!st || !st->m_mounted || !st->m_group || !outRaw) return 0;
    n = maxCount < st->m_channelCount ? maxCount : st->m_channelCount;
    for (ch = 0; ch < n; ++ch) {
        int32_t eng = 0;
        bool valid = XIoChannelGroup_isValid(st->m_group, (uint16_t)ch) &&
                     XIoChannelGroup_readAnalogInput(st->m_group,
                                                     (uint16_t)ch, &eng);
        /* 原始毫伏值反推：raw = eng × shunt / 1000。 */
        outRaw[ch] = valid ? (int32_t)((double)eng * 249.0 / 1000.0) : 0;
        if (outValid) outValid[ch] = valid;
    }
    return n;
}

XModbusHub* HsfServerIoAnalog_hub(const HsfServerIoAnalog* st)
{
    return st ? st->m_hub : NULL;
}

bool HsfServerIoAnalog_isMounted(const HsfServerIoAnalog* st)
{
    return st && st->m_mounted;
}

/* ==================== 注入链路自愈 ==================== */

/**
 * @brief 跑一拍自愈检查（与 485 两路/数字量站同一套策略）。
 * @details [为什么需要] 模拟链路注入的 XModbusVirtualClient 没有重连实现，
 *          设备一旦被判失联或轮询定时器被误杀，8 路模拟量就永久陈旧
 *          （现场同款故障已见于温湿度/流量计/阀反馈）。真机装配不启用。
 */
bool HsfServerIoAnalog_watchdog(HsfServerIoAnalog* st, uint64_t nowMs)
{
    if (!st || !st->m_mounted || !st->m_hub || !st->m_injected) return false;
    return HsfServerBusHeal_step(&st->m_heal, st->m_hub, st->m_devId,
                                 st->m_client, "模拟量站", nowMs);
}
