/**
 * @file       XModbusVirtualClient.c
 * @brief      虚拟 Modbus 客户端实现（进程内同步应答，无真实传输）。
 * @details    实现要点：
 *               · Open/Close 只改设备状态，不建套接字/不打开串口；
 *               · SendRawRequest **当场**构造 Reply 并置 Finished，故不占用
 *                 超时定时器、不进发送队列；
 *               · 功能码语义在这里翻译成模型的读写调用，异常按 Modbus 口径
 *                 回"功能码|0x80 + 异常码"。
 */
#include "XModbusVirtualClient.h"

#if XMODBUS_ON && XMODBUS_CLIENT_ON

#include "XModbusVirtualModel.h"
#include "XModbusClient_Protected.h"   /* createReply* / processResponse_base：
                                       * [必须包含] 不包含时 C 会按"隐式返回 int"
                                       * 处理这些函数，x64 下指针高 32 位被截成
                                       * 0xFFFFFFFF，随后 setState 直接 AV。 */
#include "XModbusReply_Protected.h"
#include "XModbusPdu.h"
#include "XModbusDataUnit.h"
#include "XModbusDevice_Protected.h"   /* setState / setError_2 在本头，非公开头。 */
#include "XByteArray.h"           /* push_back_1。 */
#include "XPrintf.h"
#include <string.h>
#include <stdlib.h>

/* ==================== 前向声明 ==================== */

static bool VXModbusVirtualClient_open(XModbusDevice* device);
static void VXModbusVirtualClient_close(XModbusDevice* device);
static void VXModbusVirtualClient_deinit(XModbusVirtualClient* client);
static XModbusReply* VXModbusVirtualClient_sendRawRequest(
    XModbusClient* client, const XModbusRequest* request, int serverAddress,
    XFuncParamType type);

/* ==================== 异常码表 ==================== */

static uint8_t virtualExceptionCode(XModbusVirtualResult r)
{
    switch (r) {
    case XModbusVirtual_IllegalFunction: return (uint8_t)XModbusPdu_IllegalFunction;
    case XModbusVirtual_IllegalValue:    return (uint8_t)XModbusPdu_IllegalDataValue;
    case XModbusVirtual_IllegalAddress:
    default:                             return (uint8_t)XModbusPdu_IllegalDataAddress;
    }
}

/* ==================== 类初始化 ==================== */

XVtable* XModbusVirtualClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusVirtualClient)
    XVTABLE_INHERIT_XCLASS(XModbusClient);

    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXModbusVirtualClient_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXModbusVirtualClient_close);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusVirtualClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest,
                             VXModbusVirtualClient_sendRawRequest);

    XCLASS_SHOW_SIZE_DEFAULT(XModbusVirtualClient);
    return XVTABLE_DEFAULT;
}

void XModbusVirtualClient_init(XModbusVirtualClient* client,
                               struct XModbusVirtualModel* model)
{
    if (!client) return;
    XModbusClient_init(&client->m_base);
    XClassGetVtable(client) = XModbusVirtualClient_class_init();
    client->m_model = model;
    memset(client->m_units, 0, sizeof(client->m_units));
    client->m_dropRate = 0.0;
    client->m_forceOffline = false;
    client->m_requestCount = 0;
    client->m_dropCount = 0;
}

XModbusVirtualClient* XModbusVirtualClient_create_ex(XMemoryType memory,
                                                    struct XModbusVirtualModel* model)
{
    XModbusVirtualClient* c = (XModbusVirtualClient*)XMemory_malloc(
        sizeof(XModbusVirtualClient), memory);
    if (!c) return NULL;
    XModbusVirtualClient_init(c, model);
    Set_Class_Memory(c, memory);
    Set_Class_IsHeap(c, true);
    return c;
}

static void VXModbusVirtualClient_deinit(XModbusVirtualClient* client)
{
    if (!client) return;
    /* 模型是借用，不释放（见头文件所有权说明）。 */
    client->m_model = NULL;
    XClass_Deinit_Parent(XModbusClient, (XModbusClient*)client);
}

/* ==================== 模拟开关 ==================== */

void XModbusVirtualClient_setDropRate(XModbusVirtualClient* client, double rate)
{
    if (!client) return;
    if (rate < 0.0) rate = 0.0;
    if (rate > 1.0) rate = 1.0;
    client->m_dropRate = rate;
}

void XModbusVirtualClient_setOffline(XModbusVirtualClient* client, bool offline)
{
    if (!client) return;
    client->m_forceOffline = offline;
}

/* ==================== 多从站分派 ==================== */

bool XModbusVirtualClient_bindUnit(struct XModbusVirtualClient* client,
                                   int unitId,
                                   struct XModbusVirtualModel* model)
{
    if (!client) return false;
    if (unitId < 0 || unitId >= XMODBUSVIRTUAL_UNIT_CAP) return false;
    client->m_units[unitId] = model;
    return true;
}

struct XModbusVirtualModel* XModbusVirtualClient_unitModel(
    const struct XModbusVirtualClient* client, int unitId)
{
    if (!client) return NULL;
    if (unitId < 0 || unitId >= XMODBUSVIRTUAL_UNIT_CAP) return NULL;
    return client->m_units[unitId];
}

/**
 * @brief 按从站号选模型：绑定表优先，未命中回落缺省模型。
 * @details [为什么需要] 一条 485 挂多个从站是常态（现场路 A = 变频器站 2 +
 *          温湿度站 4）。真实 RTU 靠帧内从站地址在**总线**上仲裁；进程内
 *          应答没有总线，必须显式按 serverAddress 查表，否则两个站会读到
 *          同一个模型（变频器读到温湿度的温度字，且不报错——最难查的那类 bug）。
 */
static struct XModbusVirtualModel* virtualModelFor(
    const XModbusVirtualClient* client, int serverAddress)
{
    struct XModbusVirtualModel* m;
    if (serverAddress >= 0 && serverAddress < XMODBUSVIRTUAL_UNIT_CAP) {
        m = client->m_units[serverAddress];
        if (m) return m;
    }
    return client->m_model;
}

uint64_t XModbusVirtualClient_requestCount(const XModbusVirtualClient* client)
{
    return client ? client->m_requestCount : 0;
}

uint64_t XModbusVirtualClient_dropCount(const XModbusVirtualClient* client)
{
    return client ? client->m_dropCount : 0;
}

struct XModbusVirtualModel* XModbusVirtualClient_model(
    const XModbusVirtualClient* client)
{
    return client ? client->m_model : NULL;
}

/* ==================== Open / Close ==================== */

static bool VXModbusVirtualClient_open(XModbusDevice* device)
{
    XModbusVirtualClient* client;

    if (!device) return false;
    client = (XModbusVirtualClient*)device;
    /* [强制离线] 模拟"模块不在线"：Open 直接失败，设备停在 Unconnected。
     * 这是验证降级/报错路径的唯一可控手段（真设备只能拔网线）。 */
    if (client->m_forceOffline) {
        XModbusDevice_setError_2(device, XModbusDevice_ConnectionError,
                                 "virtual: forced offline");
        XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
        return false;
    }
    /* 无模型 ⇒ 装配错误，也要暴露（不能装作连上了）。 */
    if (!client->m_model) {
        int i;
        bool anyUnit = false;
        for (i = 0; i < XMODBUSVIRTUAL_UNIT_CAP; ++i) {
            if (client->m_units[i]) { anyUnit = true; break; }
        }
        /* [多从站] 只靠 bindUnit 装配（不设缺省模型）是合法用法——
         * 此时"有模型"的判据是绑定表非空。 */
        if (!anyUnit) {
            XModbusDevice_setError_2(device, XModbusDevice_ConfigurationError,
                                     "virtual: no model");
            XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
            return false;
        }
    }
    /* 同步连上：不建套接字、不开串口、不起定时器。 */
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    return true;
}

static void VXModbusVirtualClient_close(XModbusDevice* device)
{
    if (!device) return;
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

/* ==================== 请求翻译 ==================== */

/**
 * @brief 把请求 PDU 的载荷解成 (addr, count / addr, value)。
 * @details Modbus 请求载荷一律大端：地址 2 字节 + 数量/取值 2 字节。
 */
static bool virtualParseRequest(const XModbusRequest* request, uint16_t* addr,
                                uint16_t* second)
{
    const XModbusPdu* pdu = (const XModbusPdu*)request;
    const uint8_t* raw;
    int size;

    if (!request || !addr || !second) return false;
    /* [不调 XModbusPdu_data()] 它返回**拷贝**（XByteArray_create_copy），
     * 每次调用都要调用方释放；在数据路径上用它等于每帧一次分配+泄漏。
     * 这里只需要读，直接取 pdu->m_data（脚本层结构体在公开头里有定义）。 */
    if (!pdu->m_data) return false;
    size = XModbusPdu_dataSize(pdu);
    if (size < 4) return false;
    raw = XContainerDataAddr(pdu->m_data);
    *addr = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    *second = (uint16_t)(((uint16_t)raw[2] << 8) | raw[3]);
    return true;
}

/**
 * @brief 构造异常应答 PDU（功能码|0x80 + 异常码）。
 * @note [2026-10-30 实锅] TCP 拼帧曾经漏掉 0x80，导致客户端把异常帧当正常
 *       帧解析。此处一律用 functionCodeRaw（保留异常位口径）。
 */
static void virtualBuildException(XModbusResponse* response, uint8_t fc,
                                  XModbusVirtualResult r)
{
    uint8_t body[2];
    body[0] = (uint8_t)(fc | 0x80u);
    body[1] = virtualExceptionCode(r);
    XModbusPdu_setFunctionCode((XModbusPdu*)response,
                               (XModbusPdu_FunctionCode)body[0]);
    XModbusPdu_setData((XModbusPdu*)response, &body[1], 1);
}

/**
 * @brief 构造读类应答的数据部分（位区压位图 / 寄存器区逐字大端）。
 * @details [必须用 setData 一次性写入] `XModbusPdu_data()` 返回的是内部
 *          XByteArray 的**拷贝**（XByteArray_create_copy），对它 push_back
 *          不会写回 PDU——首版就踩了这个坑：应答帧功能码正确、数据区恒空，
 *          上层读到 valid=true 但值全 0，且没有任何报错。
 */
static void virtualBuildReadBody(XModbusResponse* response, uint8_t fc,
                                 uint16_t count, bool isBitArea,
                                 const uint16_t* vals, uint16_t valCount)
{
    uint8_t buf[520];
    size_t n = 0;

    XModbusPdu_setFunctionCode((XModbusPdu*)response,
                               (XModbusPdu_FunctionCode)fc);

    if (isBitArea) {
        uint16_t nbytes = (uint16_t)((count + 7u) / 8u);
        uint16_t b;
        uint8_t cur = 0;
        if (nbytes > 250) nbytes = 250;
        buf[n++] = (uint8_t)nbytes;
        for (b = 0; b < count && b < valCount && n + 1 < sizeof(buf); ++b) {
            if (vals[b]) cur |= (uint8_t)(1u << (b % 8u));
            if ((b % 8u) == 7u) {
                buf[n++] = cur;
                cur = 0;
            }
        }
        if ((count % 8u) != 0u && n + 1 < sizeof(buf)) buf[n++] = cur;
        XModbusPdu_setData((XModbusPdu*)response, buf, n);
        return;
    }

    if (valCount > 0 && valCount < 256) {
        uint16_t i;
        buf[n++] = (uint8_t)(valCount * 2u);
        for (i = 0; i < valCount && n + 2 < sizeof(buf); ++i) {
            buf[n++] = (uint8_t)(vals[i] >> 8);
            buf[n++] = (uint8_t)(vals[i] & 0xFFu);
        }
    }
    XModbusPdu_setData((XModbusPdu*)response, buf, n);
}

/**
 * @brief SendRawRequest 实现：同步应答。
 * @param client 客户端。
 * @param request 请求（借用；按 type 决定是否拷进 Reply）。
 * @param serverAddress 从站地址。
 * @param type 所有权类型（Copy/Move/Ref，与真实客户端一致）。
 * @return Reply（已置 Finished）；失败返回 NULL。
 *
 * @note [为什么同步] 进程内应答没有"等应答"这回事。同步完成让上层代码路径
 *       （Hub 的写合并、失败重发、值回填）**完全复用**，且不引入定时器——
 *       测试里不会因为"等一会儿"而不稳定。
 */
static XModbusReply* VXModbusVirtualClient_sendRawRequest(
    XModbusClient* baseClient, const XModbusRequest* request, int serverAddress,
    XFuncParamType type)
{
    XModbusVirtualClient* client;
    XModbusReply* reply = NULL;
    uint8_t fc;
    uint16_t addr = 0;
    uint16_t second = 0;
    XModbusResponse* response;
    struct XModbusVirtualModel* model;

    if (!baseClient || !request || serverAddress < 0 || serverAddress > 247)
        return NULL;
    client = (XModbusVirtualClient*)baseClient;

    if (XModbusDevice_state((XModbusDevice*)client) !=
        XModbusDevice_ConnectedState) {
        XModbusDevice_setError_2((XModbusDevice*)client,
                                 XModbusDevice_ConnectionError, "Not connected");
        return NULL;
    }

    /* Reply 创建：与真实客户端同口径（Copy/Move/Ref 三种所有权）。 */
    if (type == XFuncParamType_Copy)
        reply = XModbusClient_createReply(baseClient, request, serverAddress);
    else if (type == XFuncParamType_Move)
        reply = XModbusClient_createReply_move(baseClient, request, serverAddress);
    else
        reply = XModbusClient_createReply_ref(baseClient,
                                              (XModbusRequest*)request,
                                              serverAddress);
    if (!reply) return NULL;

    /* [模拟空应答] 按概率"收不到应答"：直接丢 Reply，上层按超时处理。
     * 这是复现"单次空应答不该判设备失联"的唯一可控手段。 */
    if (client->m_dropRate > 0.0) {
        double r = (double)rand() / (double)RAND_MAX;
        if (r < client->m_dropRate) {
            client->m_dropCount++;
            reply->m_request = NULL;     /* 摘所有权，避免延迟删除再删一次。 */
            XModbusReply_deleteLater(reply);
            return NULL;
        }
    }
    client->m_requestCount++;

    /* [按从站号选模型] 见 virtualModelFor 注记。 */
    model = virtualModelFor(client, serverAddress);

    XModbusReply_setState(reply, XModbusReply_State_Requesting);

    fc = (uint8_t)XModbusPdu_functionCodeRaw((const XModbusPdu*)request);
    if (!reply->m_rawResult)
        reply->m_rawResult = XModbusResponse_create();
    response = reply->m_rawResult;
    if (!response) {
        reply->m_request = NULL;
        XModbusReply_deleteLater(reply);
        return NULL;
    }

    if (!virtualParseRequest(request, &addr, &second)) {
        virtualBuildException(response, fc, XModbusVirtual_IllegalValue);
        XModbusReply_setState(reply, XModbusReply_State_Finished);
        return reply;
    }

    switch (fc) {
    /* ---- 读类 ---- */
    case (uint8_t)XModbusPdu_ReadCoils:
    case (uint8_t)XModbusPdu_ReadDiscreteInputs:
    case (uint8_t)XModbusPdu_ReadHoldingRegisters:
    case (uint8_t)XModbusPdu_ReadInputRegisters:
    {
        const bool isBit = (fc == (uint8_t)XModbusPdu_ReadCoils ||
                            fc == (uint8_t)XModbusPdu_ReadDiscreteInputs);
        uint16_t vals[256];
        uint16_t outLen = 0;
        XModbusVirtualResult r;
        if (second == 0 || second > 256) {
            virtualBuildException(response, fc, XModbusVirtual_IllegalValue);
            break;
        }
        r = XModbusVirtualModel_read(model, fc, addr, second,
                                     vals, (uint16_t)256, &outLen);
        if (r != XModbusVirtual_Ok) {
            virtualBuildException(response, fc, r);
            break;
        }
        virtualBuildReadBody(response, fc, second, isBit, vals, outLen);
        /* 结构化结果也填上（Hub 走 processResponse 取数据）。 */
        if (!reply->m_result) reply->m_result = XModbusDataUnit_create();
        if (reply->m_result) {
            XModbusClient_processResponse_base(baseClient, response,
                                               reply->m_result);
        }
        break;
    }
    /* ---- 写单点 ---- */
    case (uint8_t)XModbusPdu_WriteSingleCoil:
    case (uint8_t)XModbusPdu_WriteSingleRegister:
    {
        uint16_t vals[1];
        XModbusVirtualResult r;
        vals[0] = second;
        r = XModbusVirtualModel_write(model, fc, addr, 1, vals);
        if (r != XModbusVirtual_Ok) {
            virtualBuildException(response, fc, r);
            break;
        }
        /* 回声应答：FC + addr + value（与真从站一致）。 */
        {
            uint8_t body[4];
            body[0] = (uint8_t)(addr >> 8);
            body[1] = (uint8_t)(addr & 0xFFu);
            body[2] = (uint8_t)(second >> 8);
            body[3] = (uint8_t)(second & 0xFFu);
            XModbusPdu_setFunctionCode((XModbusPdu*)response,
                                       (XModbusPdu_FunctionCode)fc);
            XModbusPdu_setData((XModbusPdu*)response, body, 4);
        }
        break;
    }
    /* ---- 写多点 ---- */
    case (uint8_t)XModbusPdu_WriteMultipleCoils:
    case (uint8_t)XModbusPdu_WriteMultipleRegisters:
    {
        const bool isBit = (fc == (uint8_t)XModbusPdu_WriteMultipleCoils);
        const XModbusPdu* pdu = (const XModbusPdu*)request;
        const uint8_t* raw;
        int size = XModbusPdu_dataSize(pdu);
        uint16_t vals[256];
        uint16_t i;
        XModbusVirtualResult r;

        if (!pdu->m_data || second == 0 || second > 256 || size < 5) {
            virtualBuildException(response, fc, XModbusVirtual_IllegalValue);
            break;
        }
        raw = XContainerDataAddr(pdu->m_data);
        if (isBit) {
            for (i = 0; i < second; ++i)
                vals[i] = (uint16_t)((raw[4 + i / 8u] >> (i % 8u)) & 1u);
        } else {
            for (i = 0; i < second; ++i)
                vals[i] = (uint16_t)(((uint16_t)raw[4 + i * 2u] << 8) |
                                     raw[5 + i * 2u]);
        }
        r = XModbusVirtualModel_write(model, fc, addr, second, vals);
        if (r != XModbusVirtual_Ok) {
            virtualBuildException(response, fc, r);
            break;
        }
        {
            uint8_t body[4];
            body[0] = (uint8_t)(addr >> 8);
            body[1] = (uint8_t)(addr & 0xFFu);
            body[2] = (uint8_t)(second >> 8);
            body[3] = (uint8_t)(second & 0xFFu);
            XModbusPdu_setFunctionCode((XModbusPdu*)response,
                                       (XModbusPdu_FunctionCode)fc);
            XModbusPdu_setData((XModbusPdu*)response, body, 4);
        }
        break;
    }
    default:
        virtualBuildException(response, fc, XModbusVirtual_IllegalFunction);
        break;
    }

    XModbusReply_setState(reply, XModbusReply_State_Finished);
    return reply;
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
