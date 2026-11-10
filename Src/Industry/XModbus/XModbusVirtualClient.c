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
#include "XThread.h"              /* XThread_msleep：虚拟从站应答时延。 */
#include "XDateTime.h"            /* 恢复期窗口的时刻戳（与 Hub 同一计时源）。 */
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
    memset(client->m_latencyLoMs, 0, sizeof(client->m_latencyLoMs));
    memset(client->m_latencyHiMs, 0, sizeof(client->m_latencyHiMs));
    client->m_longFrameUnit = -1;
    client->m_blockedUnit = -1;
    client->m_recoveryWindowMs = 0;
    client->m_longFrameEndMs = 0;
    client->m_recoveryDropCount = 0;
    /* [诊断 2026-11-10] 打印本类的 sendRawRequest 实现地址：与 Hub/客户端侧
     * 解析到的槽位指针对账——两者不一致 ⇒ 虚表槽位被别的实现占了（派发
     * 到野函数 ⇒ 请求"发出去就没动静"）。env 门控：XMODBUS_VIRTUAL_TRACE=1 */
    {
        const char* env = getenv("XMODBUS_VIRTUAL_TRACE");
        if (env && env[0] && env[0] != '0') {
            static int s_addr;
            if (s_addr < 2) {
                ++s_addr;
                XPrintf("[vsim] VXModbusVirtualClient_sendRawRequest=%p client=%p\n",
                        (void*)VXModbusVirtualClient_sendRawRequest, (void*)client);
            }
        }
    }
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

/* ==================== ttyS4 保真：应答时延 + 长帧后恢复期 ==================== */

void XModbusVirtualClient_setUnitReplyLatency(
    struct XModbusVirtualClient* client, int unitId, int loMs, int hiMs)
{
    if (!client) return;
    if (unitId < 0 || unitId >= XMODBUSVIRTUAL_UNIT_CAP) return;
    if (loMs < 0) loMs = 0;
    if (hiMs < loMs) hiMs = loMs;
    client->m_latencyLoMs[unitId] = loMs;
    client->m_latencyHiMs[unitId] = hiMs;
}

bool XModbusVirtualClient_unitReplyLatency(
    const struct XModbusVirtualClient* client, int unitId,
    int* outLoMs, int* outHiMs)
{
    if (!client || unitId < 0 || unitId >= XMODBUSVIRTUAL_UNIT_CAP)
        return false;
    if (outLoMs) *outLoMs = client->m_latencyLoMs[unitId];
    if (outHiMs) *outHiMs = client->m_latencyHiMs[unitId];
    return true;
}

void XModbusVirtualClient_setRecoveryGate(struct XModbusVirtualClient* client,
                                          int longFrameUnit, int blockedUnit,
                                          int windowMs)
{
    if (!client) return;
    if (windowMs < 0) windowMs = 0;
    client->m_longFrameUnit = longFrameUnit;
    client->m_blockedUnit = blockedUnit;
    client->m_recoveryWindowMs = windowMs;
    if (windowMs == 0 || longFrameUnit < 0) {
        /* 显式关闭：清收口戳，避免残留时刻影响下一次开启后的判定。 */
        client->m_longFrameEndMs = 0;
    }
}

int XModbusVirtualClient_recoveryWindowMs(
    const struct XModbusVirtualClient* client)
{
    return client ? client->m_recoveryWindowMs : 0;
}

uint64_t XModbusVirtualClient_recoveryDropCount(
    const struct XModbusVirtualClient* client)
{
    return client ? client->m_recoveryDropCount : 0;
}

/**
 * @brief 应答路径内的「恢复期闸 + 站时延」。
 * @return false = 本次请求按「无应答」丢弃（调用方直接返回 NULL）。
 * @details [顺序 = 真实时序] 真实总线上，站 1 在恢复期里根本不应答（主站
 *          等到超时）；出了恢复期，仪表按自己的节奏延迟后回帧。虚拟侧：
 *          先判恢复期（命中 ⇒ 丢请求计零应答），再按站号休眠时延区间内
 *          随机值——两段都发生在**构造应答之前**，主站的等待语义与真串口
 *          一致（约束：时延不放主站 sleep）。
 */
static bool virtualSlaveAccepts(XModbusVirtualClient* client,
                                int serverAddress)
{
    if (client->m_recoveryWindowMs > 0 &&
        serverAddress == client->m_blockedUnit &&
        client->m_longFrameEndMs > 0) {
        const int64_t since = XDateTime_currentMSecsSinceEpoch() -
                              client->m_longFrameEndMs;
        if (since >= 0 && since < client->m_recoveryWindowMs) {
            /* [站1 恢复期] 站5 长帧收口后窗口内：任何请求零应答（上层按
             * 超时处理，与真机「无应答（0 字节）」同口径）。 */
            client->m_recoveryDropCount++;
            return false;
        }
    }
    if (serverAddress >= 0 && serverAddress < XMODBUSVIRTUAL_UNIT_CAP) {
        const int lo = client->m_latencyLoMs[serverAddress];
        const int hi = client->m_latencyHiMs[serverAddress];
        if (hi > 0) {
            int ms = lo + (int)(((double)rand() / ((double)RAND_MAX + 1.0)) *
                                (double)(hi - lo + 1));
            if (ms > hi) ms = hi;
            if (ms > 0) XThread_msleep((uint32_t)ms);
        }
    }
    return true;
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
    uint8_t fc = 0;
    uint16_t addr = 0;
    uint16_t second = 0;
    XModbusResponse* response;
    struct XModbusVirtualModel* model;

    /* [诊断 2026-11-10] 函数入口即打（env 门控）：确认派发真的进了本函数，
     * 以及收到的实参（站号/请求指针/所有权模式）是否合理。
     * 用法：XMODBUS_VIRTUAL_TRACE=1 */
    {
        static int s_entry;
        const char* env = getenv("XMODBUS_VIRTUAL_TRACE");
        if (env && env[0] && env[0] != '0' && s_entry < 8) {
            ++s_entry;
            XPrintf("[vsim] enter sendRawRequest client=%p req=%p unit=%d type=%d\n",
                    (void*)baseClient, (const void*)request, serverAddress,
                    (int)type);
        }
    }
    if (!baseClient || !request || serverAddress < 0 || serverAddress > 247)
        return NULL;
    client = (XModbusVirtualClient*)baseClient;
    /* [诊断 2026-11-10] 入口即打（此前打在模型选择之后，"没打"说明请求
     * 压根没进这个函数 ⇒ 是 Hub 侧没发出来）。每站前 3 次。
     *
     * [2026-11-10 根修：不再打印未初始化的 fc]
     * fc 在本函数**后面**才由请求 PDU 赋值，而本诊断块位于赋值之前。
     * Debug 构建带 /RTC1（-RTCu：局部变量初始化检查）——读未初始化局部量
     * 会触发运行期检查失败 ⇒ 进程停在 DebugBreak（等 JIT 调试器）⇒
     * 现场表现"服务端启动后第一条 Modbus 请求就把进程冻住，客户端读不到
     * 任何快照"。这正是本次读组发不出去的**直接触发点**（诊断代码自己
     * 踩了未初始化缓冲这条红线）。此处只打印已赋值的实参。 */
    {
        static int s_traceN[XMODBUSVIRTUAL_UNIT_CAP];
        const char* env = getenv("XMODBUS_VIRTUAL_TRACE");
        if (env && env[0] && env[0] != '0' &&
            serverAddress >= 0 && serverAddress < XMODBUSVIRTUAL_UNIT_CAP &&
            s_traceN[serverAddress] < 3) {
            ++s_traceN[serverAddress];
            XPrintf("[vsim] unit=%d state=%d\n", serverAddress,
                    (int)XModbusDevice_state((XModbusDevice*)client));
        }
    }

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
     * 这是复现"单次空应答不该判设备失联"的唯一可控手段。
     * [所有权账 2026-11-10] Reply 的 m_request 是 createReply_**自己拷贝**
     * 出来的（Copy/Move/Ref 三态下都归 Reply 所有，deinit 统一删）——
     * 丢 Reply 时**不许**把 m_request 摘成 NULL：那会孤儿化这份拷贝，
     * 每次丢弃漏一个 Request（旧实现的注释写"摘所有权避免再删一次"，
     * 实际上根本没有第二处会删它——本函数返回 NULL，Hub 拿不到 Reply）。
     * [为什么直删不是 deleteLater] 丢弃的 Reply 还没进任何信号/队列，
     * 生命周期只在本函数内；deleteLater 要靠事件循环投递，顶层（无嵌套
     * 派发）post 的延迟删除按 shouldDeliver 判据永不投递 ⇒ 测试等非派发
     * 场景整只 Reply 泄漏（K4 实测）。直删与上下文无关，事件派发期内
     * 同样安全（deinit 自己清 posted 事件）。 */
    if (client->m_dropRate > 0.0) {
        double r = (double)rand() / (double)RAND_MAX;
        if (r < client->m_dropRate) {
            client->m_dropCount++;
            XClassDelete((XClass*)reply);
            return NULL;
        }
    }
    client->m_requestCount++;

    /* [ttyS4 保真] 恢复期闸 + 站应答时延——都在构造应答之前（虚拟从站的
     * 应答路径，模拟真实仪表的响应节奏；命中恢复期 ⇒ 无应答，上层按超时
     * 处理，与 dropRate 的"空应答"同一条返回路径）。 */
    if (!virtualSlaveAccepts(client, serverAddress)) {
        /* [计数已在 virtualSlaveAccepts 内记过] 这里只丢 Reply。 */
        XClassDelete((XClass*)reply);
        return NULL;
    }

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

    /* [诊断 2026-11-10] env 门控的取数诊断：模拟侧"某站全哑"时唯一能看清
     * 的一行——请求到了没有？模型绑上没有？读成功没有？
     * 用法：XMODBUS_VIRTUAL_TRACE=1（每站只打前 3 次，避免刷屏）。
     * [2026-11-10 根修] 移到 fc/addr/second **赋值之后**再打：这三个量在
     * 赋值前是未初始化局部量，Debug 构建的 /RTC1（-RTCu）会判"使用未初始化
     * 变量"而让进程停在 DebugBreak（等 JIT 调试器）——诊断代码自己踩了
     * 未初始化缓冲这条红线，现场表现为"服务端第一条读请求就把进程冻住"。 */
    {
        static int s_traceN[XMODBUSVIRTUAL_UNIT_CAP];
        const char* env = getenv("XMODBUS_VIRTUAL_TRACE");
        if (env && env[0] && env[0] != '0' &&
            serverAddress >= 0 && serverAddress < XMODBUSVIRTUAL_UNIT_CAP &&
            s_traceN[serverAddress] < 3) {
            ++s_traceN[serverAddress];
            XPrintf("[vsim] unit=%d fc=0x%02X addr=0x%04X count=%u model=%p\n",
                    serverAddress, (unsigned)fc, (unsigned)addr,
                    (unsigned)second, (void*)model);
        }
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
        /* [应答功能码可被模型覆盖] 站 2 ZHEYI 的怪癖：FC04 请求也受理，
         * 但应答功能码恒回 FC03（2026-11-09 实测文档 §5.5）。缺省=原码。 */
        const uint8_t respFc = XModbusVirtualModel_responseFunction(model, fc);
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
        virtualBuildReadBody(response, respFc, second, isBit, vals, outLen);
        /* [长帧收口戳] 长帧站的寄存器读成功且数据够长 ⇒ 记应答收口时刻，
         * 供恢复期闸判定（站 5 的 49 字节帧 → 站 1 的 125ms 死区）。 */
        if (!isBit && serverAddress == client->m_longFrameUnit &&
            (uint32_t)outLen * 2u >= XMODBUSVIRTUAL_LONG_FRAME_MIN_BYTES) {
            client->m_longFrameEndMs = XDateTime_currentMSecsSinceEpoch();
        }
        /* 结构化结果也填上（Hub 走 processResponse 取数据；按**应答**功能码
         * 分派——站 2 的 FC04 请求应答是 03，结构化语义随之）。 */
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
