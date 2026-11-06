/**
 * @file       HsfServerIoDigital.c
 * @brief      服务端数字量站实现（MT3A-IO1632，16DI/16DO，Modbus-TCP）。
 * @details    装配顺序：TCP 客户端 + 连接参数 + 超时 → 挂 Hub → 建 DI/DO
 *             两个映射段 → 组 + 链路 → 16 个 DI 点 + 16 个 DO 点 →
 *             提交轮询（FC02 离散输入 + FC01 线圈两区）。
 *
 *             [role 分段] DI 通道 n → role=n（0..15）；DO 通道 n → role=16+n。
 *             段基址只在本文件内翻译，对上层只暴露"通道号 + 方向"。
 *
 *             [写路径] 单点写经 XIoChannelGroup_writeDigitalOutput → 链路
 *             WriteRole → Hub。核实结论（2026-11-05）：
 *               `XModbusClient.c:buildWriteRequest` 在 valueCount==1 且
 *               type==Coils 时选 **FC05 WriteSingleCoil**，只有 count>1 才
 *               选 FC0F。故**一次只写一个点**即可天然避开 FC0F——这正是
 *               本站（以及电磁阀组）的写口径，绝不能改成整组批量写。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#include "HsfServerIoDigital.h"
#include "HsfServerConfig.h"   /* HSF_DIG_CHANNELS / HSF_DIG_HOST_EXT 等 */

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusLink.h"
#include "XModbusTcpClient.h"
#include "XModbusVirtualClient.h" /* 模拟模型访问器（虚表判类型）。 */
#include "XModbusVirtualModel.h"
#include "XModbusDevice.h"
#include "XModbusClient.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XVariant.h"
#include "XPrintf.h"
#include "XAbstractSocket.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XThread.h"
#include <stdio.h>
#include <string.h>

/** @brief DI 点 role 段基址（DI 通道 n → role = 0 + n，n=0..31）。 */
#define HSF_DIG_ROLE_DI_BASE   0
/**
 * @brief DO 点 role 段基址（DO 通道 n → role = 32 + n，n=0..31）。
 * @details [2026-11-05 32 点段冲突根修] 原为 16。16 点时代 DI(0..15) 与
 *          DO(16..31) 刚好错开；扩到 32 点后 DI 自己就占满 0..31，DO 基址 16
 *          与 DI 通道 16..31 **role 相撞**——同一 role 在组内只能有一个点，
 *          后建的点会把先建的顶掉（症状：DI 上半区或 DO 前半区整片无数据，
 *          且没有报错）。32 点必须让两段各占 32 个槽：DI 0..31、DO 32..63。
 */
#define HSF_DIG_ROLE_DO_BASE   32

/** @brief 区下标（XModbusLink_areaOfType 口径：0=DI 1=COIL 2=HR 3=IR）。 */
#define HSF_DIG_AREA_DI        0
#define HSF_DIG_AREA_DO        1

/* ==================== 内部辅助 ==================== */

/**
 * @brief 建连接参数变体并写入设备连接参数表。
 * @return 写入成功返回 true。
 * @note  拷贝语义：value 为借用，设备内深拷贝后不取得所有权，故设完由本
 *        函数释放自建对象（与 HsfServerIoAnalog 同款）。
 */
static bool digitalSetConnParam(XModbusDevice* dev, int param, bool isStr,
                                int i, const char* str)
{
    XVariant* v = isStr ? XVariant_create_utf8_str(str)
                        : XVariant_create_int(i);
    if (!v) return false;
    XModbusDevice_setConnectionParameter(dev,
                                         (XModbusDevice_ConnectionParameter)param, v);
    XClassDelete((XClass*)v);
    return true;
}

/**
 * @brief 探测某台数字量模块是否在线（能否建立 Modbus-TCP 会话）。
 * @param host 模块 IPv4 文本；NULL/空返回 false。
 * @param port TCP 端口。
 * @param timeoutMs 连接超时（毫秒）。
 * @return 在线 true。
 * @details 用一个**临时** XModbusTcpClient 建连接，拿到 Connected 就断开。
 *          为什么不复用主客户端的连接：探测发生在装配前，主客户端此刻还没
 *          建；也不该为了探测先建主连接（会打乱后面的映射/轮询时序）。
 *          为什么不用"读 16 点"当探针：该模块会话槽有限（每建一次连接占
 *          一个，耗尽后连真轮询都会被拒），读点会在模块刚上电时留下半个
 *          会话，代价比一次 connect 大得多。
 *          连接失败的各种原因（不可达/超时/被拒）一律判"不在线"：本函数只
 *          只用于决定装 16 点还是 32 点，网络细节不抛给调用方。
 *          [当前现场] HSF_DIG_HOST_EXT 为空 ⇒ 根本不会调用它（也就不会
 *          为探测多占一个会话槽）。
 */
static bool digitalProbeOnline(const char* host, int port, int timeoutMs)
{
    XModbusTcpClient* probe;
    bool ok = false;
    if (!host || !host[0]) return false;
    probe = XModbusTcpClient_create();
    if (!probe) return false;
    digitalSetConnParam((XModbusDevice*)probe,
                        XModbusDevice_NetworkAddressParameter, true, 0, host);
    digitalSetConnParam((XModbusDevice*)probe,
                        XModbusDevice_NetworkPortParameter, false, port, NULL);
    if (!XModbusDevice_connectDevice((XModbusDevice*)probe)) {
        XClassDelete((XClass*)probe);
        return false;
    }
    {
        const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() +
                                 (uint64_t)((timeoutMs > 0) ? timeoutMs : 400);
        while (XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (XModbusDevice_state((XModbusDevice*)probe) ==
                XModbusDevice_ConnectedState) {
                ok = true;
                break;
            }
            XThread_msleep(10);
        }
    }
    XModbusDevice_disconnectDevice((XModbusDevice*)probe);
    XClassDelete((XClass*)probe);
    return ok;
}

/* ==================== 生命周期 ==================== */

static void VHsfServerIoDigital_deinit(XObject* obj)
{
    HsfServerIoDigital* st = (HsfServerIoDigital*)obj;
    if (!st) return;
    HsfServerIoDigital_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* HsfServerIoDigital_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoDigital)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoDigital_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoDigital);
    return XVTABLE_DEFAULT;
}

void HsfServerIoDigital_init(HsfServerIoDigital* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoDigital_class_init();
    st->m_devId = -1;
    st->m_devIdExt = -1;
    st->m_channelsPerDev = HSF_DIG_CHANNELS;
    st->m_mounted = false;
}

/* ==================== 装配 ==================== */

/**
 * @brief 站装配核心：挂设备 → 登记**全部**映射段 → 建点/链路 → 起轮询。
 * @param st 目标站。
 * @param client 主模块客户端（所有权转移给 Hub）。
 * @param extClient 扩展模块客户端（所有权转移给 Hub；NULL=不聚合）。
 * @param channels 对外点数（含扩展；调用方已夹到合法范围）。
 * @param startAddr 远端起始地址（已夹）。
 * @param pollMs 轮询周期（已夹）。
 * @return 成功 true；失败时已完成内部清理。
 *
 * @details [顺序是硬约束] `XModbusHub_setMapping` 会**立即重建**全部缓存块与
 *          视图（hubRebuildAll）。因此顺序必须是：
 *            ① 挂设备（拿 devId）→ ② 用 devId 登记全部映射段 → ③ setMapping
 *          若先 setMapping 再追加段，新段不进缓存视图——读到的永远是旧段缓存，
 *          表现为"扩展模块的点恒为 0 且不报错"。这正是 32 点聚合第一版的实锅。
 *          [当前现场单台 16 点，extClient 恒为 NULL，但这条顺序约束照旧
 *          适用——将来挂上扩展模块时它仍是前提。]
 */
static bool digitalMountCore(HsfServerIoDigital* st, XModbusClient* client,
                             XModbusClient* extClient, int channels,
                             int startAddr, int pollMs)
{
    XModbusMapping* map = NULL;
    int ch;
    int perDev = st->m_channelsPerDev;
    int mainChannels = (extClient && channels > perDev) ? perDev : channels;

    st->m_hub = XModbusHub_create();
    st->m_client = client;      /* 仅用于诊断查询；所有权在 Hub。 */
    if (!st->m_hub || !client) {
        XPrintf("HsfServerIoDigital: Hub/客户端创建失败\n");
        goto fail;
    }

    /* ---- ① 挂设备 ---- */
    st->m_devId = XModbusHub_addDevice_2(st->m_hub, client, "di-do-12");
    if (st->m_devId < 0) {
        XPrintf("HsfServerIoDigital: 主模块挂 Hub 失败\n");
        goto fail;
    }
    if (extClient) {
        st->m_clientExt = extClient;
        st->m_devIdExt = XModbusHub_addDevice_2(st->m_hub, extClient, "di-do-13");
        if (st->m_devIdExt < 0) {
            XPrintf("HsfServerIoDigital: 扩展模块挂 Hub 失败（降级为 16 点）\n");
            st->m_clientExt = NULL;
            XClassDelete((XClass*)extClient);   /* addDevice 失败不接管。 */
            channels = mainChannels;
        }
    }

    /* ---- ② 登记全部映射段 ---- */
    map = XModbusMapping_create();
    if (!map) goto fail;
    /* 两个独立映射段：离散输入（FC02）与线圈（FC01/FC05）。
     * 虚拟地址 = 逻辑点号（0..15 主模块 / 16..31 扩展模块）。 */
    if (!XModbusMapping_addRange(map, XModbusDiscreteInputs,
                                 (uint16_t)startAddr, (uint16_t)mainChannels,
                                 st->m_devId, HSF_DIG_UNIT_ID, 0, 0) ||
        !XModbusMapping_addRange(map, XModbusCoils,
                                 (uint16_t)startAddr, (uint16_t)mainChannels,
                                 st->m_devId, HSF_DIG_UNIT_ID, 0, 0)) {
        XPrintf("HsfServerIoDigital: 主模块映射段登记失败\n");
        goto fail;
    }
    if (st->m_devIdExt >= 0) {
        const int extStart = startAddr + perDev;
        const int extCount = channels - perDev;
        if (extCount > 0 &&
            (!XModbusMapping_addRange(map, XModbusDiscreteInputs,
                                      (uint16_t)extStart, (uint16_t)extCount,
                                      st->m_devIdExt, HSF_DIG_UNIT_ID, 0, 0) ||
             !XModbusMapping_addRange(map, XModbusCoils,
                                      (uint16_t)extStart, (uint16_t)extCount,
                                      st->m_devIdExt, HSF_DIG_UNIT_ID, 0, 0))) {
            XPrintf("HsfServerIoDigital: 扩展模块映射段登记失败（降级 16 点）\n");
            XModbusHub_removeDevice(st->m_hub, st->m_devIdExt);
            st->m_devIdExt = -1;
            st->m_clientExt = NULL;         /* 所有权随 removeDevice 释放。 */
            channels = mainChannels;
        } else if (extCount > 0) {
            XPrintf("HsfServerIoDigital: 扩展模块已接入（逻辑点 %d..%d）\n",
                    extStart, channels - 1);
        }
    }

    st->m_group = XIoChannelGroup_create();
    st->m_link = XModbusLink_create(st->m_hub);
    if (!st->m_group || !st->m_link ||
        !XIoChannelGroup_setLink(st->m_group, (XIoLink*)st->m_link)) {
        XPrintf("HsfServerIoDigital: 站组/链路装配失败\n");
        goto fail;
    }

    /* ---- DI 点（只读）+ DO 点（可写） ---- */
    for (ch = 0; ch < channels; ++ch) {
        XIoPoint* pt;
        char tag[24];

        /* DI */
        pt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pt) goto fail;
        snprintf(tag, sizeof(tag), "DI%d", ch + 1);
        XIoPoint_setRole(pt, (uint16_t)(HSF_DIG_ROLE_DI_BASE + ch));
        XIoPoint_setType(pt, XIoPoint_DigitalInput);
        XIoPoint_setTag(pt, tag);
        if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
            XClassDelete((XClass*)pt);
            goto fail;
        }
        if (!XIoLink_bindRole_base((XIoLink*)st->m_link,
                                   (uint16_t)(HSF_DIG_ROLE_DI_BASE + ch),
                                   (uint32_t)(startAddr + ch),
                                   HSF_DIG_AREA_DI)) {
            goto fail;
        }

        /* DO */
        pt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pt) goto fail;
        snprintf(tag, sizeof(tag), "DO%d", ch + 1);
        XIoPoint_setRole(pt, (uint16_t)(HSF_DIG_ROLE_DO_BASE + ch));
        XIoPoint_setType(pt, XIoPoint_DigitalOutput);
        XIoPoint_setTag(pt, tag);
        if (!XIoChannelGroup_addPoint(st->m_group, pt, true)) {
            XClassDelete((XClass*)pt);
            goto fail;
        }
        if (!XIoLink_bindRole_base((XIoLink*)st->m_link,
                                   (uint16_t)(HSF_DIG_ROLE_DO_BASE + ch),
                                   (uint32_t)(startAddr + ch),
                                   HSF_DIG_AREA_DO)) {
            goto fail;
        }
    }

    /* 提交映射 + 启动轮询（DI/DO 两区都轮询，DO 读回用于回显）。 */
    XModbusHub_setMapping(st->m_hub, map);
    /* [写合并窗口=0] 单点写立即出帧，不等窗口攒批——阀/DO 是即时动作，
     * 攒批会让"点下去没反应"（现场观感不可接受）。 */
    XModbusHub_setWriteCoalesceMs(st->m_hub, 0);
    XModbusHub_setPollAreas(st->m_hub,
                            (1u << XModbusDiscreteInputs) |
                            (1u << XModbusCoils));
    XModbusHub_setPollIntervalDefault(st->m_hub, (size_t)pollMs);
    XModbusHub_setPollInterval(st->m_hub, st->m_devId, (size_t)pollMs);
    if (st->m_devIdExt >= 0)
        XModbusHub_setPollInterval(st->m_hub, st->m_devIdExt, (size_t)pollMs);
    if (!XIoLink_start_base((XIoLink*)st->m_link))
        XPrintf("HsfServerIoDigital: 链路启动失败\n");
    XModbusHub_connectAll(st->m_hub);
    XModbusHub_startPolling(st->m_hub);

    st->m_channelCount = channels;
    st->m_channelsPerDev = perDev;
    st->m_startAddr = startAddr;
    st->m_mounted = true;
    return true;

fail:
    if (map) XClassDelete((XClass*)map);
    HsfServerIoDigital_unmount(st);
    return false;
}

bool HsfServerIoDigital_mountWithClient(HsfServerIoDigital* st,
                                        struct XModbusClient* client,
                                        int channels, int startAddr,
                                        int pollMs, int timeoutMs)
{
    return HsfServerIoDigital_mountWithClients(st, client, NULL, channels,
                                               startAddr, pollMs, timeoutMs);
}

bool HsfServerIoDigital_mountWithClients(HsfServerIoDigital* st,
                                        struct XModbusClient* client,
                                        struct XModbusClient* extClient,
                                        int channels, int startAddr,
                                        int pollMs, int timeoutMs)
{
    int perDev;
    int maxTotal;

    if (!st || st->m_mounted || !client) return false;
    if (channels <= 0) return false;

    perDev = HSF_DIG_CHANNELS;
    if (perDev > HSF_DIGITAL_CHANNELS) perDev = HSF_DIGITAL_CHANNELS;
    if (startAddr < 0) startAddr = 0;
    /* 扩展模块最多再给一台，合计不超过对外点数上限。 */
    maxTotal = perDev * (extClient ? 2 : 1);
    if (maxTotal > HSF_DIGITAL_CHANNELS) maxTotal = HSF_DIGITAL_CHANNELS;
    if (channels > maxTotal) channels = maxTotal;
    /* 点数没超过单台能力 ⇒ 用不上扩展模块（与真实装配的降级口径一致）。 */
    if (extClient && channels <= perDev) extClient = NULL;
    if (startAddr >= perDev) startAddr = 0;
    if (channels > maxTotal - startAddr) channels = maxTotal - startAddr;
    if (pollMs < 20) pollMs = 20;
    if (timeoutMs < 200) timeoutMs = 200;

    st->m_channelsPerDev = perDev;
    XModbusClient_setTimeout((XModbusClient*)client, (size_t)timeoutMs);
    if (extClient)
        XModbusClient_setTimeout((XModbusClient*)extClient, (size_t)timeoutMs);
    if (!digitalMountCore(st, (XModbusClient*)client,
                          (XModbusClient*)extClient,
                          channels, startAddr, pollMs)) {
        return false;
    }
    XPrintf("HsfServerIoDigital: 装配完成（%d 点 DI+DO，外部客户端%s，"
            "起址 %d，轮询 %dms）\n", st->m_channelCount,
            extClient ? "（双模块）" : "", startAddr, pollMs);
    return true;
}

bool HsfServerIoDigital_mount(HsfServerIoDigital* st, const char* host,
                              int port, int unitId, int channels,
                              int startAddr, int pollMs, int timeoutMs)
{
    int perDev;
    bool extOnline = false;

    if (!st || st->m_mounted) return false;
    if (!host || !host[0]) return false;
    if (channels <= 0) return false;
    /* 单台模块的物理点数上限（实测 16 点；读 32 点回 EXC03）。 */
    perDev = HSF_DIG_CHANNELS;
    if (perDev > HSF_DIGITAL_CHANNELS) perDev = HSF_DIGITAL_CHANNELS;
    if (channels > perDev) channels = perDev;
    if (startAddr < 0) startAddr = 0;
    /* [实测硬约束] 起始 + 点数跨出单台 16 点 ⇒ 模块回 EXC03/EXC02。
     * 夹进合法范围，宁可少读也不发非法请求（非法请求会拖慢整条链路：
     * 模块要等超时才释放通道）。 */
    if (startAddr >= perDev) startAddr = 0;
    if (channels > perDev - startAddr) channels = perDev - startAddr;
    if (pollMs < 20) pollMs = 20;
    if (timeoutMs < 200) timeoutMs = 200;

    st->m_channelsPerDev = perDev;

    /* [32 点自动装配] HSF_DIG_HOST_EXT 非空时先探它：在线就两台合成 32 点，
     * 不在线（拆机/断网/上电中）就只读主模块自己的 16 点。
     * 降级必须自动——现场拔掉一台不该让整块 IO 跟着失联。
     * [当前现场] 该宏为空 = 本来就没有扩展模块，直接单台 16 点，
     * 不是"降级"。 */
    if (HSF_DIG_HOST_EXT[0]) {
        extOnline = digitalProbeOnline(HSF_DIG_HOST_EXT, port, 400);
        XPrintf("HsfServerIoDigital: 扩展模块 %s %s\n", HSF_DIG_HOST_EXT,
                extOnline ? "在线（按 32 点装配）" : "不在线（降级为 16 点）");
    } else {
        XPrintf("HsfServerIoDigital: 未配置扩展模块（单台 16 点）\n");
    }

    /* ---- 主模块客户端（HSF_DIG_HOST，当前现场 .13） ---- */
    st->m_client = XModbusTcpClient_create();
    if (!st->m_client) {
        XPrintf("HsfServerIoDigital: 客户端创建失败\n");
        return false;
    }
    digitalSetConnParam((XModbusDevice*)st->m_client,
                        XModbusDevice_NetworkAddressParameter, true, 0, host);
    digitalSetConnParam((XModbusDevice*)st->m_client,
                        XModbusDevice_NetworkPortParameter, false, port, NULL);
    /* [超时给足] 该模块应答实测 0.4~0.8s，比其它设备慢一个数量级；
     * 用缺省 1000ms 会偶发假超时（超时后 Hub 会重发，帧序容易乱）。 */
    XModbusClient_setTimeout((XModbusClient*)st->m_client, (size_t)timeoutMs);

    /* ---- 扩展模块（.13）：在线才建客户端（装配由 core 统一完成） ---- */
    {
        XModbusClient* extClient = NULL;
        if (extOnline && channels == perDev) {
            st->m_clientExt = XModbusTcpClient_create();
            if (!st->m_clientExt) {
                XPrintf("HsfServerIoDigital: 扩展模块客户端创建失败（降级 16 点）\n");
            } else {
                digitalSetConnParam((XModbusDevice*)st->m_clientExt,
                                    XModbusDevice_NetworkAddressParameter, true, 0,
                                    HSF_DIG_HOST_EXT);
                digitalSetConnParam((XModbusDevice*)st->m_clientExt,
                                    XModbusDevice_NetworkPortParameter, false,
                                    port, NULL);
                XModbusClient_setTimeout((XModbusClient*)st->m_clientExt,
                                         (size_t)timeoutMs);
                extClient = (XModbusClient*)st->m_clientExt;
                channels += perDev;          /* 对外点数变成 32 */
            }
        }

        /* 核心装配：挂设备 → 登记全部映射段 → 建点/链路 → 起轮询。
         * [unitId 用参数] core 内部按 HSF_DIG_UNIT_ID 登记；本函数的 unitId
         * 参数保留为兼容既有调用（现场实测恒 1，与宏一致）。 */
        (void)unitId;
        if (!digitalMountCore(st, (XModbusClient*)st->m_client, extClient,
                              channels, startAddr, pollMs)) {
            return false;
        }
    }

    XPrintf("HsfServerIoDigital: 装配完成（%d 点 DI+DO，%s，起址 %d，轮询 %dms，超时 %dms）\n",
            st->m_channelCount,
            (st->m_devIdExt >= 0) ? "双模块聚合" : "单模块",
            startAddr, pollMs, timeoutMs);
    return true;
}

void HsfServerIoDigital_unmount(HsfServerIoDigital* st)
{
    if (!st) return;
    if (st->m_hub) {
        XModbusHub_stopPolling(st->m_hub);
        XModbusHub_disconnectAll(st->m_hub);
    }
    if (st->m_group) { XClassDelete((XClass*)st->m_group); st->m_group = NULL; }
    if (st->m_link)  { XClassDelete((XClass*)st->m_link);  st->m_link = NULL; }
    if (st->m_hub)   { XClassDelete((XClass*)st->m_hub);   st->m_hub = NULL; }
    st->m_client = NULL;      /* 所有权在 Hub，随 Hub 释放。 */
    st->m_clientExt = NULL;   /* 同上。 */
    st->m_devId = -1;
    st->m_devIdExt = -1;
    st->m_channelCount = 0;
    st->m_startAddr = 0;
    st->m_mounted = false;
}

/* ==================== 读写 ==================== */

bool HsfServerIoDigital_readDi(const HsfServerIoDigital* st, uint32_t* outMask,
                               bool* outValid)
{
    int ch;

    if (outValid) *outValid = false;
    if (outMask) *outMask = 0;
    if (!st || !st->m_mounted || !st->m_group || !outMask) return false;

    for (ch = 0; ch < st->m_channelCount; ++ch) {
        bool on = false;
        if (!XIoChannelGroup_readDigitalInput(st->m_group,
                                              (uint16_t)(HSF_DIG_ROLE_DI_BASE + ch),
                                              &on))
            continue;
        if (on) *outMask |= (1u << ch);
        if (outValid && XIoChannelGroup_isValid(
                st->m_group, (uint16_t)(HSF_DIG_ROLE_DI_BASE + ch)))
            *outValid = true;
    }
    return outValid ? *outValid : true;
}

bool HsfServerIoDigital_readDo(const HsfServerIoDigital* st, uint32_t* outMask,
                               bool* outValid)
{
    int ch;

    if (outValid) *outValid = false;
    if (outMask) *outMask = 0;
    if (!st || !st->m_mounted || !st->m_group || !outMask) return false;

    for (ch = 0; ch < st->m_channelCount; ++ch) {
        bool on = false;
        /* [必须用 readDigitalOutput] DO 点是输出型，readDigitalInput 会因
         * 类型不符恒返回 false ⇒ DO 位图永远是 0（2026-11-05 实测踩过：
         * 写入其实已生效，只是回读被静默拒绝）。 */
        if (!XIoChannelGroup_readDigitalOutput(st->m_group,
                                               (uint16_t)(HSF_DIG_ROLE_DO_BASE + ch),
                                               &on))
            continue;
        if (on) *outMask |= (1u << ch);
        if (outValid && XIoChannelGroup_isValid(
                st->m_group, (uint16_t)(HSF_DIG_ROLE_DO_BASE + ch)))
            *outValid = true;
    }
    return outValid ? *outValid : true;
}

bool HsfServerIoDigital_writeDo(HsfServerIoDigital* st, int channel, bool on)
{
    if (!st || !st->m_mounted || !st->m_group) return false;
    if (channel < 0 || channel >= st->m_channelCount) return false;
    /* 单点写 ⇒ 客户端选 FC05（见文件头核实结论）。绝不改成批量写。 */
    return XIoChannelGroup_writeDigitalOutput(
        st->m_group, (uint16_t)(HSF_DIG_ROLE_DO_BASE + channel), on);
}

bool HsfServerIoDigital_isDiFresh(const HsfServerIoDigital* st, int channel)
{
    if (!st || !st->m_mounted || !st->m_group) return false;
    if (channel < 0 || channel >= st->m_channelCount) return false;
    return XIoChannelGroup_isFresh(st->m_group,
                                   (uint16_t)(HSF_DIG_ROLE_DI_BASE + channel));
}

bool HsfServerIoDigital_readDiBit(const HsfServerIoDigital* st, int channel,
                                  bool* outOn)
{
    if (outOn) *outOn = false;
    if (!st || !st->m_mounted || !st->m_group || !outOn) return false;
    if (channel < 0 || channel >= st->m_channelCount) return false;
    return XIoChannelGroup_readDigitalInput(
        st->m_group, (uint16_t)(HSF_DIG_ROLE_DI_BASE + channel), outOn);
}

/* ==================== 查询 ==================== */

bool HsfServerIoDigital_isMounted(const HsfServerIoDigital* st)
{
    return st && st->m_mounted;
}

int HsfServerIoDigital_channelCount(const HsfServerIoDigital* st)
{
    return st ? st->m_channelCount : 0;
}

XModbusHub* HsfServerIoDigital_hub(const HsfServerIoDigital* st)
{
    return st ? st->m_hub : NULL;
}

struct XModbusVirtualModel* HsfServerIoDigital_simModel(
    const HsfServerIoDigital* st)
{
    const XModbusVirtualClient* c;
    if (!st || !st->m_client) return NULL;
    /* [必须验虚表] m_client 是 XModbusClient*，真实设备也走这条指针；
     * 直接强转成 XModbusVirtualClient 再读 m_model 是**未定义行为**
     * （读到 TCP 客户端的别的字段当模型指针）。按虚表判断类型。 */
    c = (const XModbusVirtualClient*)st->m_client;
    if (XClassGetVtable(c) != XModbusVirtualClient_class_init()) return NULL;
    return XModbusVirtualClient_model(c);
}

struct XModbusVirtualModel* HsfServerIoDigital_simModelExt(
    const HsfServerIoDigital* st)
{
    const XModbusVirtualClient* c;
    if (!st || !st->m_clientExt) return NULL;
    c = (const XModbusVirtualClient*)st->m_clientExt;
    if (XClassGetVtable(c) != XModbusVirtualClient_class_init()) return NULL;
    return XModbusVirtualClient_model(c);
}
