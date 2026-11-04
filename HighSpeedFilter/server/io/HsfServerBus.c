
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
#include <string.h>

/* ==================== 内部辅助 ==================== */

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
    bool ok = false;
    if (!v) return false;
    ok = XModbusDevice_setConnectionParameter(dev, (XModbusDevice_ConnectionParameter)param, v);
    XClassDelete((XClass*)v);
    return ok;
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
    XPrintf("HsfServerBus: [%s] 轮询已启动（节拍 %dms）\n", bus->m_name, pollMs);
    return true;
}

void HsfServerBus_stop(HsfServerBus* bus)
{
    if (!bus) return;
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
