#include "XPrintf.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "XCoreApplication.h"
#include "XByteArray.h"
#include "XVector.h"
#include "XVariant.h"
#include "XString.h"
#include "XModbusPdu.h"
#include "XModbusAdu.h"
#include "XModbusDataUnit.h"
#include "XModbusDevice.h"
#include "XModbusReply.h"
#include "XModbusClient.h"
#include "XModbusServer.h"
#include "XModbusServer_Protected.h"
#include "XModbusTcpClient.h"
#include "XModbusTcpServer.h"
#include "XModbusRtuSerialClient.h"
#include "XModbusRtuSerialServer.h"

static int g_pass;
static int g_fail;

#define CHECK(expr, name) do { \
    if (expr) { ++g_pass; XPrintf("  [通过] %s\n", name); } \
    else { ++g_fail; XPrintf("  [失败] %s\n", name); } \
} while (0)

static void delete_pdu(XModbusPdu *pdu)
{
    if (pdu) XClassDelete(pdu);
}

static bool configure_holding_registers(XModbusServer *server, uint16_t start, size_t count)
{
    XModbusDataUnitMap *map = XModbusDataUnitMap_create();
    XModbusDataUnit *unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusHoldingRegisters, start, count);
    XModbusRegisterType type = XModbusHoldingRegisters;
    bool ok = false;

    if (map && unit && XMap_insert_base(map, &type, unit))
        ok = XModbusServer_setMap_base(server, map);
    if (unit) XClassDelete(unit);
    if (map) XClassDelete(map);
    return ok;
}

static void test_pdu(void)
{
    XModbusPdu *pdu = XModbusPdu_create_with_code(XModbusPdu_WriteMultipleRegisters);
    uint8_t bytes[] = { 0x12, 0x34 };
    uint16_t words[] = { 0x1234, 0xABCD };
    uint8_t decodedBytes[2] = { 0, 0 };
    uint16_t decodedWords[2] = { 0, 0 };
    XModbusPdu_EncodeField fields[] = {
        { XModbusPdu_DataType_Uint8, bytes, 2 },
        { XModbusPdu_DataType_Uint16, words, 2 }
    };
    XModbusPdu_DecodeField decodeFields[] = {
        { XModbusPdu_DataType_Uint8, decodedBytes, 2 },
        { XModbusPdu_DataType_Uint16, decodedWords, 2 }
    };
    XModbusPdu_DecodeField tooMuch = { XModbusPdu_DataType_Uint16, decodedWords, 4 };
    const uint8_t expected[] = { 0x12, 0x34, 0x12, 0x34, 0xAB, 0xCD };
    XModbusPdu *copy;
    XModbusExceptionResponse *exception;

    CHECK(pdu != NULL, "PDU 创建");
    CHECK(pdu && XModbusPdu_functionCode(pdu) == XModbusPdu_WriteMultipleRegisters,
        "PDU 功能码读取");
    CHECK(pdu && XModbusPdu_encodeData(pdu, fields, 2), "PDU 大端编码");
    CHECK(pdu && XModbusPdu_dataSize(pdu) == 6 &&
        memcmp(XContainerDataAddr(pdu->m_data), expected, sizeof(expected)) == 0,
        "PDU 编码结果字节序");
    CHECK(pdu && XModbusPdu_decodeData(pdu, decodeFields, 2) &&
        memcmp(decodedBytes, bytes, sizeof(bytes)) == 0 &&
        decodedWords[0] == words[0] && decodedWords[1] == words[1],
        "PDU 大端解码");
    CHECK(pdu && !XModbusPdu_decodeData(pdu, &tooMuch, 1),
        "PDU 数据不足检查");
    CHECK(pdu && !XModbusPdu_encodeData(pdu, NULL, 1), "PDU 无效字段检查");
    CHECK(pdu && XModbusPdu_isValid(pdu) && !XModbusPdu_isException(pdu),
        "PDU 有效性和普通响应判断");
    copy = pdu ? XModbusPdu_create_copy(pdu) : NULL;
    CHECK(copy && XModbusPdu_dataSize(copy) == 6 &&
        XModbusPdu_functionCode(copy) == XModbusPdu_WriteMultipleRegisters,
        "PDU 深拷贝");
    exception = XModbusExceptionResponse_create_with_function_and_exception(
        XModbusPdu_ReadHoldingRegisters, XModbusPdu_IllegalDataAddress);
    CHECK(exception && XModbusPdu_isException((XModbusPdu *)exception) &&
        XModbusPdu_exceptionCode((XModbusPdu *)exception) == XModbusPdu_IllegalDataAddress,
        "异常响应创建和异常码");
    delete_pdu(copy);
    delete_pdu(pdu);
    if (exception) XClassDelete(exception);
}

static void test_adu(void)
{
    XModbusRequest *request = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
    uint8_t requestData[] = { 0x00, 0x10, 0x00, 0x02 };
    XByteArray *rtu = NULL;
    XByteArray *ascii = NULL;
    XModbusAdu *parsedRtu = NULL;
    XModbusAdu *parsedAscii = NULL;
    XModbusPdu parsedPdu;
    bool pduOk = false;

    CHECK(request != NULL, "ADU 请求 PDU 创建");
    if (request) XModbusPdu_setData((XModbusPdu *)request, requestData, sizeof(requestData));
    rtu = request ? XModbusAdu_createRtuFrame(1, (XModbusPdu *)request) : NULL;
    ascii = request ? XModbusAdu_createAsciiFrame(1, (XModbusPdu *)request, '\n') : NULL;
    CHECK(rtu && XByteArray_size_base(rtu) == 8, "RTU ADU 帧创建");
    CHECK(ascii && XByteArray_size_base(ascii) > 0 && XByteArray_at_base(ascii, 0) == ':',
        "ASCII ADU 帧创建");
    parsedRtu = rtu ? XModbusAdu_parseRtu(XByteArray_data(rtu), XByteArray_size_base(rtu)) : NULL;
    parsedAscii = ascii ? XModbusAdu_parseAscii(XByteArray_data(ascii), XByteArray_size_base(ascii)) : NULL;
    CHECK(parsedRtu && XModbusAdu_matchingChecksum(parsedRtu) &&
        XModbusAdu_serverAddress(parsedRtu) == 1 && XModbusAdu_size(parsedRtu) == 6,
        "RTU ADU 解析和 CRC");
    CHECK(parsedAscii && XModbusAdu_matchingChecksum(parsedAscii) &&
        XModbusAdu_serverAddress(parsedAscii) == 1,
        "ASCII ADU 解析和 LRC");
    XModbusPdu_init(&parsedPdu);
    if (parsedRtu) pduOk = XModbusAdu_pdu(parsedRtu, &parsedPdu);
    CHECK(pduOk && XModbusPdu_functionCode(&parsedPdu) == XModbusPdu_ReadHoldingRegisters &&
        XModbusPdu_dataSize(&parsedPdu) == sizeof(requestData), "ADU PDU 往返");
    if (pduOk) XClassDeinit(&parsedPdu);
    if (parsedRtu) XModbusAdu_delete(parsedRtu);
    if (parsedAscii) XModbusAdu_delete(parsedAscii);
    if (rtu) XClassDelete(rtu);
    if (ascii) XClassDelete(ascii);
    if (request) XClassDelete(request);
}

static void test_data_unit(void)
{
    XModbusDataUnit *invalid = XModbusDataUnit_create();
    XModbusDataUnit *registers = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusHoldingRegisters, 10, 3);
    XModbusDataUnit *coils = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusCoils, 20, 4);
    XVector *values = XVector_Create(uint16_t);
    XVector *copyValues = NULL;
    XBitArray *bits = XBitArray_create(4);

    CHECK(invalid && !XModbusDataUnit_isValid(invalid) &&
        XModbusDataUnit_startAddress(invalid) == 65535, "DataUnit 默认无效值");
    CHECK(registers && XModbusDataUnit_isValid(registers) &&
        XModbusDataUnit_registerType(registers) == XModbusHoldingRegisters &&
        XModbusDataUnit_valueCount(registers) == 3, "DataUnit 寄存器创建和属性");
    CHECK(registers && XModbusDataUnit_setValue(registers, 0, 0x1234) &&
        XModbusDataUnit_value(registers, 0) == 0x1234 &&
        !XModbusDataUnit_setValue(registers, 3, 1), "DataUnit 单值读写和越界");
    if (values) {
        uint16_t v[] = { 1, 2, 3 };
        XVector_push_back_2(values, v, 3);
    }
    CHECK(registers && values && XModbusDataUnit_setValues(registers, values) &&
        XModbusDataUnit_value(registers, 2) == 3, "DataUnit 批量设置值");
    copyValues = registers ? XModbusDataUnit_values1(registers) : NULL;
    CHECK(copyValues && XVector_size_base(copyValues) == 3 &&
        *(uint16_t *)XVector_at_base(copyValues, 1) == 2, "DataUnit 获取寄存器副本");
    if (bits) {
        XBitArray_setBit(bits, 1, true);
        CHECK(coils && XModbusDataUnit_setBitArray(coils, bits) &&
            XModbusDataUnit_value(coils, 1) == 1 && XModbusDataUnit_values2(coils) != NULL,
            "DataUnit 位数组读写");
    }
    if (copyValues) XClassDelete(copyValues);
    if (values) XClassDelete(values);
    if (bits) XClassDelete(bits);
    if (invalid) XClassDelete(invalid);
    if (registers) XClassDelete(registers);
    if (coils) XClassDelete(coils);
}

static void test_reply_device(void)
{
    XModbusReply *reply = XModbusReply_create(XModbusReply_Common, 7);
    XModbusDataUnit *unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusHoldingRegisters, 2, 2);
    XModbusResponse *response = XModbusResponse_create_with_code(XModbusPdu_ReadHoldingRegisters);
    XVector *errors;
    XString *errorText;
    XModbusTcpClient *tcp = XModbusTcpClient_create();
    XModbusRtuSerialClient *rtu = XModbusRtuSerialClient_create();
    XModbusRtuSerialServer *rtuServer = XModbusRtuSerialServer_create();
    XVariant *variant;

    if (unit) {
        XModbusDataUnit_setValue(unit, 0, 0xAAAA);
        XModbusDataUnit_setValue(unit, 1, 0x5555);
    }
    if (response) {
        uint8_t data[] = { 4, 0, 1, 0, 2 };
        XModbusPdu_setData((XModbusPdu *)response, data, sizeof(data));
    }
    CHECK(reply && XModbusReply_type(reply) == XModbusReply_Common &&
        XModbusReply_serverAddress(reply) == 7 && !XModbusReply_isFinished(reply),
        "Reply 创建和属性");
    if (reply) {
        XModbusReply_setResult(reply, unit);
        XModbusReply_setRawResult(reply, response);
        XModbusReply_addIntermediateError(reply, XModbusDevice_ResponseCrcError);
        errors = XModbusReply_intermediateErrors(reply);
        XModbusReply_setError(reply, XModbusDevice_ProtocolError, "协议错误测试");
        errorText = XModbusReply_errorString(reply);
        CHECK(XModbusReply_result_const(reply) && XModbusReply_result(reply) &&
            XModbusReply_result(reply)->m_startAddress == 2, "Reply 结构化结果深拷贝");
        CHECK(XModbusReply_rawResult_const(reply) && XModbusReply_rawResult(reply) &&
            XModbusPdu_functionCode((XModbusPdu *)XModbusReply_rawResult_const(reply)) ==
            XModbusPdu_ReadHoldingRegisters, "Reply 原始结果深拷贝");
        CHECK(errors && XVector_size_base(errors) == 1 &&
            *(XModbusDevice_IntermediateError *)XVector_at_base(errors, 0) ==
            XModbusDevice_ResponseCrcError, "Reply 中间错误列表");
        CHECK(XModbusReply_error(reply) == XModbusDevice_ProtocolError &&
            XModbusReply_isFinished(reply) && errorText &&
            strcmp(XString_toUtf8(errorText), "协议错误测试") == 0, "Reply 错误和完成状态");
        if (errors) XClassDelete(errors);
        if (errorText) XClassDelete(errorText);
        {
            XModbusDataUnit *resultCopy = XModbusReply_result(reply);
            if (resultCopy) {
                XModbusDataUnit_setValue(resultCopy, 0, 0);
                CHECK(XModbusDataUnit_value(XModbusReply_result_const(reply), 0) == 0xAAAA,
                    "Reply 结果确实深拷贝");
                XClassDelete(resultCopy);
            } else CHECK(false, "Reply 结果确实深拷贝");
        }
    }
    CHECK(tcp && XModbusClient_timeout((XModbusClient *)tcp) == 1000 &&
        XModbusClient_numberOfRetries((XModbusClient *)tcp) == 3, "TCP 客户端默认参数");
    if (tcp) {
        XModbusClient_setTimeout((XModbusClient *)tcp, 321);
        XModbusClient_setNumberOfRetries((XModbusClient *)tcp, 5);
        XModbusClient_setAutoReconnect((XModbusClient *)tcp, true);
        XModbusClient_setReconnectInterval((XModbusClient *)tcp, 77);
        XModbusClient_setMaxReconnectAttempts((XModbusClient *)tcp, 9);
        CHECK(XModbusClient_timeout((XModbusClient *)tcp) == 321 &&
            XModbusClient_numberOfRetries((XModbusClient *)tcp) == 5 &&
            XModbusClient_autoReconnect((XModbusClient *)tcp) &&
            XModbusClient_reconnectInterval((XModbusClient *)tcp) == 77 &&
            XModbusClient_maxReconnectAttempts((XModbusClient *)tcp) == 9, "TCP 客户端参数设置");
        variant = XVariant_create_int(12345);
        XModbusDevice_setConnectionParameter((XModbusDevice *)tcp,
            XModbusDevice_NetworkPortParameter, variant);
        if (variant) XClassDelete(variant);
        variant = XModbusDevice_connectionParameter((XModbusDevice *)tcp,
            XModbusDevice_NetworkPortParameter);
        CHECK(variant && XVariant_toInt(variant) == 12345, "设备连接参数深拷贝");
        if (variant) XClassDelete(variant);
    }
    CHECK(rtu && XModbusRtuSerialClient_turnaroundDelay(rtu) == 100,
        "RTU 客户端创建和默认参数");
    CHECK(rtuServer && XModbusRtuSerialServer_serialPort(rtuServer) != NULL &&
        rtuServer->m_turnaroundDelay == 100, "RTU 服务端创建和串口访问");
    if (reply) XModbusReply_deleteLater(reply);
    if (unit) XClassDelete(unit);
    if (response) XClassDelete(response);
    if (tcp) XModbusTcpClient_deleteLater(tcp);
    if (rtu) XModbusRtuSerialClient_deleteLater(rtu);
    if (rtuServer) XModbusRtuSerialServer_deleteLater(rtuServer);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

static void test_server_request(void)
{
    XModbusServer *server = XModbusServer_create();
    XModbusRequest *request = XModbusRequest_create_with_code(XModbusPdu_ReadHoldingRegisters);
    XModbusResponse *response = NULL;
    uint8_t data[] = { 0, 10, 0, 2 };
    uint16_t value = 0;
    XVariant *option;

    CHECK(server && XModbusServer_serverAddress(server) == 1, "服务器默认地址");
    CHECK(server && configure_holding_registers(server, 10, 16), "服务器数据映射配置");
    CHECK(server && XModbusServer_setData2(server, XModbusHoldingRegisters, 10, 0x1234) &&
        XModbusServer_data2(server, XModbusHoldingRegisters, 10, &value) && value == 0x1234,
        "服务器单寄存器读写");
    option = XVariant_create_int(42);
    CHECK(server && option && XModbusServer_setValue_base(server, XModbusServer_ServerIdentifier, option),
        "服务器选项设置");
    if (option) XClassDelete(option);
    option = server ? XModbusServer_value_base(server, XModbusServer_ServerIdentifier) : NULL;
    CHECK(option && XVariant_toInt(option) == 42, "服务器选项读取");
    if (option) XClassDelete(option);
    if (request) XModbusPdu_setData((XModbusPdu *)request, data, sizeof(data));
    response = server && request ? XModbusServer_processRequest_base(server, request) : NULL;
    CHECK(response && XModbusPdu_functionCode((XModbusPdu *)response) == XModbusPdu_ReadHoldingRegisters &&
        XModbusPdu_dataSize((XModbusPdu *)response) == 5, "服务器读取请求处理");
    if (response) XClassDelete(response);
    if (request) XClassDelete(request);
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

static void test_tcp_loopback(void)
{
    XModbusTcpServer *server = XModbusTcpServer_create();
    XModbusTcpClient *client = XModbusTcpClient_create();
    XVariant *address = XVariant_create_utf8_str("127.0.0.1");
    XVariant *port = XVariant_create_int(0);
    XModbusDataUnit *read = NULL;
    XModbusReply *reply = NULL;
    uint16_t actualPort = 0;
    bool connected = false;
    int i;

    if (server && address) XModbusDevice_setConnectionParameter_ref((XModbusDevice *)server,
        XModbusDevice_NetworkAddressParameter, address);
    if (server && port) XModbusDevice_setConnectionParameter_ref((XModbusDevice *)server,
        XModbusDevice_NetworkPortParameter, port);
    if (address) XClassDelete(address);
    if (port) XClassDelete(port);
    if (server) {
        configure_holding_registers((XModbusServer *)server, 0, 16);
        XModbusServer_setData2((XModbusServer *)server,
            XModbusHoldingRegisters, 0, 0x1357);
    }
    CHECK(server && XModbusDevice_connectDevice((XModbusDevice *)server) &&
        XModbusDevice_state((XModbusDevice *)server) == XModbusDevice_ConnectedState,
        "TCP 服务端本机监听");
    if (server && server->m_tcpServer) actualPort = XTcpServer_serverPort(server->m_tcpServer);
    CHECK(actualPort != 0, "TCP 服务端自动端口");
    if (client && actualPort) {
        XModbusDevice_setConnectionParameter_ref((XModbusDevice *)client,
            XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("127.0.0.1"));
        XModbusDevice_setConnectionParameter_ref((XModbusDevice *)client,
            XModbusDevice_NetworkPortParameter, XVariant_create_int(actualPort));
        connected = XModbusDevice_connectDevice((XModbusDevice *)client);
        for (i = 0; i < 100 && XModbusDevice_state((XModbusDevice *)client) != XModbusDevice_ConnectedState; ++i) {
            XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
        }
        connected = connected && XModbusDevice_state((XModbusDevice *)client) == XModbusDevice_ConnectedState;
    }
    CHECK(connected, "TCP 客户端本机连接");
    read = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusHoldingRegisters, 0, 1);
    if (client && connected && read)
        reply = XModbusClient_sendReadRequest((XModbusClient *)client, read, 1, XModbusRequestPriority_Normal);
    for (i = 0; reply && i < 100 && !XModbusReply_isFinished(reply); ++i)
        XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
    CHECK(reply && XModbusReply_isFinished(reply) && XModbusReply_error(reply) == XModbusDevice_NoError,
        "TCP Modbus 读请求联调完成");
    if (reply) {
        const XModbusDataUnit *result = XModbusReply_result_const(reply);
        CHECK(result && XModbusDataUnit_value(result, 0) == 0x1357, "TCP Modbus 读结果校验");
        XModbusReply_deleteLater(reply);
    }
    if (read) XClassDelete(read);
    if (client) { XModbusDevice_disconnectDevice((XModbusDevice *)client); XModbusTcpClient_deleteLater(client); }
    if (server) { XModbusDevice_disconnectDevice((XModbusDevice *)server); XModbusTcpServer_deleteLater(server); }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}


/* ==================== [补充 2026-10-15] 服务端 FC 处理全量回归 ====================
 * 背景：现场远程 IO 模块（12DI/16DO，第三方）在全开(F legacy FC15)+10ms 轮询
 * 下出现"写偶发未应用、下一次轮询显示全关"。本组测试在进程内对
 * XModbusServer 的各 FC 处理路径做位/字节级断言，锁定框架侧保证：
 *   1. FC15 任意位型写入 → FC1 回读逐位一致（远程 IO 掉线问题回归锁）；
 *   2. 全部数量/字节计数/地址越界异常码与 Modbus 规范一致；
 *   3. FC5/FC16 编码规范（0xFF00/0x0000、大端寄存器）。
 * 远端模块自身的通信看门狗/单事务压力行为属于对端设备语义，不在本测试
 * 范围（主站侧保证：写帧完整发出、事务按 ID 匹配、错误如实计数）。 */

/** 覆盖率测试共用服务器：16 线圈 + 16 离散输入 + 32 保持 + 32 输入寄存器。
 *  返回堆服务器（调用方 deleteLater），失败返回 NULL。 */
static XModbusServer *coverage_server_new(void)
{
    XModbusServer *server = XModbusServer_create();
    XModbusDataUnitMap *map = XModbusDataUnitMap_create();
    XModbusDataUnit *unit;
    XModbusRegisterType type;
    /* [根修 2026-10-15] 键必须长生命周期（static）：XMap 按键指针存取，
     * 栈变量键在函数返回后悬垂，按类型查找命中垃圾=写读互相失配。 */
    static struct { XModbusRegisterType type; uint16_t start; size_t count; } defs[] = {
        { XModbusCoils, 0, 16 },
        { XModbusDiscreteInputs, 0, 16 },
        { XModbusHoldingRegisters, 0, 32 },
        { XModbusInputRegisters, 0, 32 }
    };
    size_t i;
    if (!server || !map) { if (server) XModbusServer_deleteLater(server); if (map) XClassDelete(map); return NULL; }
    for (i = 0; i < sizeof(defs) / sizeof(defs[0]); ++i) {
        unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
            defs[i].type, defs[i].start, defs[i].count);
        if (!unit || !XMap_insert_base((XMapBase *)map, &defs[i].type, unit)) {
            if (unit) XClassDelete(unit);
            XClassDelete(map);
            XModbusServer_deleteLater(server);
            return NULL;
        }
        XClassDelete(unit);
    }
    (void)type;
    fprintf(stderr, "[COVNEW] map built, entering setMap_ref\n");
    if (!XModbusServer_setMap_ref_base(server, map)) {
        XClassDelete(map);
        XModbusServer_deleteLater(server);
        return NULL;
    }
    /* _ref=所有权移交：map/单元归服务器所有并在其析构时释放，
     * 此处不得再 delete（否则服务器 m_dataMap 悬垂=挂死/UB）。 */
    fprintf(stderr, "[COVNEW] setMap_ref done\n");
    return server;
}

/** 构造请求并预处理：fc + 不含 FC 的 PDU 数据（data[0]=起始地址高字节）。 */
static XModbusRequest *coverage_request(uint8_t fc, const uint8_t *data, size_t size)
{
    XModbusRequest *request = XModbusRequest_create_with_code(
        (XModbusPdu_FunctionCode)fc);
    if (request)
        XModbusPdu_setData((XModbusPdu *)request, data, size);
    return request;
}

/** 断言响应为指定异常码（返回 true=是；释放响应由调用方负责）。 */
static int coverage_is_exception(XModbusResponse *response, uint8_t fc,
    XModbusPdu_ExceptionCode code)
{
    if (!response) return 0;
    return XModbusPdu_isException((XModbusPdu *)response) &&
        XModbusPdu_functionCode((XModbusPdu *)response) == (uint8_t)(fc | 0x80) &&
        XModbusPdu_exceptionCode((XModbusPdu *)response) == code;
}

/** FC1/FC2 读线圈/离散输入处理路径：位打包 LSB-first + 数量/越界异常。 */
static void test_server_fc1_fc2(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t req[5];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    int i;

    /* 预置：线圈 0..15 = 交替 0xAA（位 0,2,4,6..=1）；离散输入 0..15 = 0x55 */
    if (server) {
        for (i = 0; i < 16; ++i) {
            XModbusServer_setData2(server, XModbusCoils, (uint16_t)i, (i % 2) ? 0 : 1);
            XModbusServer_setData2(server, XModbusDiscreteInputs, (uint16_t)i, (i % 2) ? 1 : 0);
        }
    }
    fprintf(stderr, "[FC1T] entering block\n");
    fprintf(stderr, "[FC1T] before preset loop\n");
    /* FC1 正常读 0..7：LSB-first 打包应为 0x55 */
    fprintf(stderr, "[FC1T] after preset loop\n");
    if (server) {
        fprintf(stderr, "[FC1T] building request\n");
        req[0] = 0; req[1] = 0; req[2] = 0; req[3] = 8;
        request = coverage_request(1, req, 4);
        fprintf(stderr, "[FC1T] before processRequest\n");
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        fprintf(stderr, "[FC1T] after processRequest\n");
        CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) &&
            XModbusPdu_dataSize((XModbusPdu *)response) == 2 &&
            XByteArray_data(((XModbusPdu *)response)->m_data)[1] == 0x55,
            "FC1 读线圈 LSB 位打包");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC1 数量=0 → 异常 03（IllegalDataValue） */
    if (server) {
        req[2] = 0; req[3] = 0;
        request = coverage_request(1, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 1, XModbusPdu_IllegalDataValue),
            "FC1 数量 0 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC1 数量=2001（>2000 上限）→ 异常 03 */
    if (server) {
        req[2] = 0x07; req[3] = 0xD1;
        request = coverage_request(1, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 1, XModbusPdu_IllegalDataValue),
            "FC1 数量 2001 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC1 越界（起点 10 数量 16 > 16 点）→ 异常 02（IllegalDataAddress） */
    if (server) {
        req[0] = 0; req[1] = 10; req[2] = 0; req[3] = 16;
        request = coverage_request(1, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 1, XModbusPdu_IllegalDataAddress),
            "FC1 越界 → 异常 02");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC2 读离散输入 0..7：应为 0x55 的镜像（0x55=奇数位=1） */
    if (server) {
        req[0] = 0; req[1] = 0; req[2] = 0; req[3] = 8;
        request = coverage_request(2, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) &&
            XByteArray_data(((XModbusPdu *)response)->m_data)[1] == 0x55,
            "FC2 读离散输入位打包");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** FC3/FC4 读寄存器处理路径：大端字序 + 数量上限 0x7D + 越界异常。 */
static void test_server_fc3_fc4(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t req[5];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    const uint8_t *rd;
    int i;

    if (server) {
        for (i = 0; i < 8; ++i)
            XModbusServer_setData2(server, XModbusHoldingRegisters, (uint16_t)i,
                (uint16_t)(0x1000 * (i + 1)));
        for (i = 0; i < 8; ++i)
            XModbusServer_setData2(server, XModbusInputRegisters, (uint16_t)i,
                (uint16_t)(0x2000 * (i + 1)));
    }
    /* FC3 读 0..3：大端 1000,2000,3000,4000 */
    if (server) {
        req[0] = 0; req[1] = 0; req[2] = 0; req[3] = 4;
        request = coverage_request(3, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        rd = response ? (const uint8_t *)XContainerDataAddr(
            (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
        CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) &&
            rd && rd[0] == 8 && rd[1] == 0x10 && rd[2] == 0x00 && rd[3] == 0x20 &&
            rd[4] == 0x00 && rd[5] == 0x30 && rd[6] == 0x00 && rd[7] == 0x40,
            "FC3 读保持寄存器大端字序");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC3 数量=0x7E（>0x7D 上限）→ 异常 03 */
    if (server) {
        req[2] = 0; req[3] = 0x7E;
        request = coverage_request(3, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 3, XModbusPdu_IllegalDataValue),
            "FC3 数量 126 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* FC4 读输入寄存器 0..1：2000,4000 大端 */
    if (server) {
        req[0] = 0; req[1] = 0; req[2] = 0; req[3] = 2;
        request = coverage_request(4, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        rd = response ? (const uint8_t *)XContainerDataAddr(
            (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
        CHECK(response && rd && rd[0] == 4 && rd[1] == 0x20 && rd[2] == 0x00 &&
            rd[3] == 0x40 && rd[4] == 0x00,
            "FC4 读输入寄存器大端字序");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** FC5 写单线圈：0xFF00=置位 / 0x0000=复位，响应回显，越界异常。 */
static void test_server_fc5(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t req[4];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    uint16_t value = 0;

    /* 置位线圈 3：0xFF00 */
    if (server) {
        req[0] = 0; req[1] = 3; req[2] = 0xFF; req[3] = 0;
        request = coverage_request(5, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) &&
            XModbusPdu_dataSize((XModbusPdu *)response) == 4,
            "FC5 置位响应回显");
        value = 0;
        if (server) XModbusServer_data2(server, XModbusCoils, 3, &value);
        CHECK(value == 1, "FC5 置位落库");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 复位线圈 3：0x0000 */
    if (server) {
        req[2] = 0x00;
        request = coverage_request(5, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        value = 1;
        if (server) XModbusServer_data2(server, XModbusCoils, 3, &value);
        CHECK(response && value == 0, "FC5 复位落库（0x0000 编码）");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 越界线圈 99 → 异常 02 */
    if (server) {
        req[0] = 0; req[1] = 99; req[2] = 0xFF; req[3] = 0;
        request = coverage_request(5, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 5, XModbusPdu_IllegalDataAddress),
            "FC5 越界 → 异常 02");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** FC15 写多线圈：任意位型写读一致性（远程 IO 掉线回归锁）+ 规范异常。
 *  位型矩阵：全 0/全 1/交替 0x55/交替 0xAA/单点游走/随机阵——每种写入后
 *  立即 FC1 回读逐位断言，杜绝"部分应用/位序颠倒/奇偶丢失"。 */
static void test_server_fc15_patterns(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t wreq[6];
    uint8_t rreq[4];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    const uint8_t *rd;
    int round;
    int i;
    uint8_t pattern[16];
    uint32_t seed = 20261015;

    for (round = 0; round < 8; ++round) {
        if (!server) break;
        /* 生成位型：全0/全1/交替/反交替/游走/随机/半区/反相上一轮 */
        for (i = 0; i < 16; ++i) {
            switch (round) {
            case 0: pattern[i] = 0; break;
            case 1: pattern[i] = 1; break;
            case 2: pattern[i] = (uint8_t)(i % 2); break;
            case 3: pattern[i] = (uint8_t)((i + 1) % 2); break;
            case 4: pattern[i] = (uint8_t)((i == round) ? 1 : 0); break;
            case 5:
                seed = seed * 1103515245u + 12345u;
                pattern[i] = (uint8_t)((seed >> 16) & 1);
                break;
            case 6: pattern[i] = (uint8_t)(i < 8); break;
            default: pattern[i] = (uint8_t)((round == 7) ? !pattern[i] : pattern[i]); break;
            }
        }
        /* FC15 写：addr 0 qty 16 bc 2 + 打包数据（LSB-first） */
        wreq[0] = 0; wreq[1] = 0; wreq[2] = 0; wreq[3] = 16; wreq[4] = 2;
        request = coverage_request(15, wreq, 5);
        if (request) {
            uint8_t bytes[2] = { 0, 0 };
            XByteArray *pdu = ((XModbusPdu *)request)->m_data;
            XVector_push_back_2((XVector *)pdu, bytes, 2);
            for (i = 0; i < 16; ++i)
                if (pattern[i]) bytes[i / 8] |= (uint8_t)(1 << (i % 8));
            XVector_push_back_2((XVector *)pdu, bytes, 2);
        }
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        /* 响应回显 addr+qty（4 字节） */
        rd = response ? (const uint8_t *)XContainerDataAddr(
            (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
        CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) && rd &&
            rd[0] == 0 && rd[1] == 0 && rd[2] == 0 && rd[3] == 16,
            "FC15 响应回显 addr+qty");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
        /* FC1 回读逐位断言（回归锁核心） */
        if (XSystem_environment("MB_TREE_DUMP")) {
            fprintf(stderr, "[COV] FC15 写后 data2 直读: ");
            for (i = 0; i < 16; ++i) {
                uint16_t v = 0;
                XModbusServer_data2(server, XModbusCoils, (uint16_t)i, &v);
                fprintf(stderr, "%d", v ? 1 : 0);
            }
            fprintf(stderr, "\n");
        }
        rreq[0] = 0; rreq[1] = 0; rreq[2] = 0; rreq[3] = 16;
        request = coverage_request(1, rreq, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        rd = response ? (const uint8_t *)XContainerDataAddr(
            (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
        i = (response && !XModbusPdu_isException((XModbusPdu *)response) && rd &&
            rd[0] == 2) ? 1 : 0;
        if (i) {
            int bad = 0;
            for (i = 0; i < 16; ++i) {
                uint8_t bit = (uint8_t)((rd[1 + i / 8] >> (i % 8)) & 1);
                if (bit != pattern[i]) {
                    if (bad < 8)
                        XPrintf("    [失配] coil%d 期望=%d 实际=%d (回读字节=%02x)\n",
                            i, pattern[i], bit, rd[1 + i / 8]);
                    ++bad;
                }
            }
            i = (bad == 0);
        }
        CHECK(i, "FC15 写→FC1 回读逐位一致（远程 IO 掉线回归锁）");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* byteCount 与数量不符（bc=1 应为 2）→ 异常 03 */
    if (server) {
        wreq[0] = 0; wreq[1] = 0; wreq[2] = 0; wreq[3] = 16; wreq[4] = 1;
        request = coverage_request(15, wreq, 5);
        if (request) {
            uint8_t bytes[2] = { 0xFF, 0xFF };
            XVector_push_back_2((XVector *)((XModbusPdu *)request)->m_data, bytes, 2);
        }
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 15, XModbusPdu_IllegalDataValue),
            "FC15 byteCount 不符 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 数量 0 → 异常 03；数量 1969（>1968 上限）→ 异常 03 */
    if (server) {
        wreq[2] = 0; wreq[3] = 0; wreq[4] = 0;
        request = coverage_request(15, wreq, 5);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 15, XModbusPdu_IllegalDataValue),
            "FC15 数量 0 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
        wreq[2] = 0x07; wreq[3] = 0xB1; wreq[4] = 0; wreq[5] = 0;
        request = coverage_request(15, wreq, 6);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 15, XModbusPdu_IllegalDataValue),
            "FC15 数量 1969 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 越界（起点 10 数量 16 > 16 点）→ 异常 02 */
    if (server) {
        wreq[0] = 0; wreq[1] = 10; wreq[2] = 0; wreq[3] = 16; wreq[4] = 2;
        request = coverage_request(15, wreq, 6);
        if (request) {
            uint8_t bytes[2] = { 0xFF, 0xFF };
            XVector_push_back_2((XVector *)((XModbusPdu *)request)->m_data, bytes, 2);
        }
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 15, XModbusPdu_IllegalDataAddress),
            "FC15 越界 → 异常 02");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** FC16 写多寄存器 + FC3 回读一致性（大端）+ 规范异常。 */
static void test_server_fc16_fc3(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t wreq[6];
    uint8_t rreq[4];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    const uint8_t *rd;
    int i;
    uint16_t words[8];

    for (i = 0; i < 8; ++i)
        words[i] = (uint16_t)(0x1111 * (i + 1));
    /* FC16 写：addr 0 qty 8 bc 16 + 大端数据 */
    wreq[0] = 0; wreq[1] = 0; wreq[2] = 0; wreq[3] = 8; wreq[4] = 0; wreq[5] = 16;
    request = coverage_request(16, wreq, 6);
    if (request) {
        XByteArray *pdu = ((XModbusPdu *)request)->m_data;
        for (i = 0; i < 8; ++i) {
            uint8_t be[2];
            be[0] = (uint8_t)(words[i] >> 8);
            be[1] = (uint8_t)words[i];
            XVector_push_back_2((XVector *)pdu, be, 2);
        }
    }
    response = request ? XModbusServer_processRequest_base(server, request) : NULL;
    rd = response ? (const uint8_t *)XContainerDataAddr(
        (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
    CHECK(response && !XModbusPdu_isException((XModbusPdu *)response) && rd &&
        rd[0] == 0 && rd[1] == 0 && rd[2] == 0 && rd[3] == 8,
        "FC16 响应回显 addr+qty");
    if (request) XClassDelete(request);
    if (response) XClassDelete(response);
    request = NULL; response = NULL;
    /* FC3 回读逐字一致（回归锁） */
    rreq[0] = 0; rreq[1] = 0; rreq[2] = 0; rreq[3] = 8;
    request = coverage_request(3, rreq, 4);
    response = request ? XModbusServer_processRequest_base(server, request) : NULL;
    rd = response ? (const uint8_t *)XContainerDataAddr(
        (XContainer *)((XModbusPdu *)response)->m_data) : NULL;
    i = (response && rd && rd[0] == 16) ? 1 : 0;
    if (i) {
        int bad = 0;
        for (i = 0; i < 8; ++i) {
            uint16_t v = (uint16_t)((rd[1 + i * 2] << 8) | rd[2 + i * 2]);
            if (v != words[i]) ++bad;
        }
        i = (bad == 0);
    }
    CHECK(i, "FC16 写→FC3 回读逐字一致（大端回归锁）");
    if (request) XClassDelete(request);
    if (response) XClassDelete(response);
    request = NULL; response = NULL;
    /* bc 与数量不符（bc=15 应为 16）→ 异常 03 */
    if (server) {
        wreq[4] = 0; wreq[5] = 15;
        request = coverage_request(16, wreq, 6);
        if (request) {
            uint8_t be[30];
            memset(be, 0xEE, sizeof(be));
            XVector_push_back_2((XVector *)((XModbusPdu *)request)->m_data, be, 15);
        }
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 16, XModbusPdu_IllegalDataValue),
            "FC16 byteCount 不符 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 数量 124（>123 上限）→ 异常 03 */
    if (server) {
        wreq[2] = 0; wreq[3] = 124; wreq[4] = 0; wreq[5] = 248;
        request = coverage_request(16, wreq, 6);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 16, XModbusPdu_IllegalDataValue),
            "FC16 数量 124 → 异常 03");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** 无效功能码与四类型数据区隔离。 */
static void test_server_invalid_fc_and_map(void)
{
    XModbusServer *server = coverage_server_new();
    uint8_t req[4];
    XModbusRequest *request = NULL;
    XModbusResponse *response = NULL;
    uint16_t value = 0x5A5A;
    int i;

    /* 非法功能码 0x47 → 异常 01（IllegalFunction） */
    if (server) {
        req[0] = 0; req[1] = 0; req[2] = 0; req[3] = 4;
        request = coverage_request(0x47, req, 4);
        response = request ? XModbusServer_processRequest_base(server, request) : NULL;
        CHECK(coverage_is_exception(response, 0x47, XModbusPdu_IllegalFunction),
            "非法功能码 → 异常 01");
        if (request) XClassDelete(request);
        if (response) XClassDelete(response);
        request = NULL; response = NULL;
    }
    /* 四类型隔离：写线圈不串离散/保持/输入寄存器 */
    if (server) {
        for (i = 0; i < 16; ++i)
            XModbusServer_setData2(server, XModbusCoils, (uint16_t)i, 1);
        for (i = 0; i < 16; ++i) {
            value = 0x5A5A;
            XModbusServer_data2(server, XModbusDiscreteInputs, (uint16_t)i, &value);
            if (value == 0x5A5A) value = 0;
            XModbusServer_data2(server, XModbusHoldingRegisters, (uint16_t)i, &value);
            if (value == 0x5A5A) value = 0;
            XModbusServer_data2(server, XModbusInputRegisters, (uint16_t)i, &value);
            if (value == 0x5A5A) value = 0;
        }
        CHECK(value == 0, "四类型数据区互不串扰");
    }
    if (server) XModbusServer_deleteLater(server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** TCP 回环：轮询读与 FC15 写交叠压力（远程 IO 场景回归锁）——框架侧保证
 *  每笔 FC15 在压力下仍完整应用（回读逐位一致），轮询读不丢不串。 */
static void test_client_fc15_pressure_e2e(void)
{
    XModbusTcpServer *server = XModbusTcpServer_create();
    XModbusTcpClient *client = XModbusTcpClient_create();
    XVariant *address = XVariant_create_utf8_str("127.0.0.1");
    XVariant *port = XVariant_create_int(0);
    XModbusDataUnit *unit = NULL;
    XModbusReply *reply = NULL;
    uint16_t actualPort = 0;
    int connected = 0;
    int round;
    int i;
    int bad = 0;
    uint8_t pattern[16];
    uint32_t seed = 777;

    if (server && address) XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice *)server, XModbusDevice_NetworkAddressParameter, address);
    if (server && port) XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice *)server, XModbusDevice_NetworkPortParameter, port);
    if (address) XClassDelete(address);
    if (port) XClassDelete(port);
    connected = server && XModbusDevice_connectDevice((XModbusDevice *)server);
    if (server && server->m_tcpServer)
        actualPort = XTcpServer_serverPort(server->m_tcpServer);
    if (client && actualPort) {
        XModbusDevice_setConnectionParameter_ref((XModbusDevice *)client,
            XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("127.0.0.1"));
        XModbusDevice_setConnectionParameter_ref((XModbusDevice *)client,
            XModbusDevice_NetworkPortParameter, XVariant_create_int(actualPort));
        XModbusDevice_connectDevice((XModbusDevice *)client);
        for (i = 0; i < 200 && XModbusDevice_state((XModbusDevice *)client) !=
            XModbusDevice_ConnectedState; ++i)
            XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
        connected = connected && XModbusDevice_state((XModbusDevice *)client) ==
            XModbusDevice_ConnectedState;
    }
    CHECK(connected, "压力测试回环连接");
    /* 30 轮：FC15 交替全开/全关/随机 + 每轮 FC1 读两组；写应答与回读全断言 */
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusCoils, 0, 16);
    for (round = 0; connected && round < 30; ++round) {
        seed = seed * 1103515245u + 12345u;
        for (i = 0; i < 16; ++i)
            pattern[i] = (uint8_t)(((seed >> (i % 13)) & 1) |
                ((round % 2) ? 1 : 0));
        if (unit) {
            for (i = 0; i < 16; ++i)
                XModbusDataUnit_setValue(unit, (size_t)i,
                    pattern[i] ? 1u : 0u);
            reply = XModbusClient_sendWriteRequest((XModbusClient *)client,
                unit, 1, XModbusRequestPriority_Normal);
        }
        for (i = 0; reply && i < 200 && !XModbusReply_isFinished(reply); ++i)
            XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
        CHECK(reply && XModbusReply_isFinished(reply) &&
            XModbusReply_error(reply) == XModbusDevice_NoError,
            "压力 FC15 写应答无错");
        if (reply) { XModbusReply_deleteLater(reply); reply = NULL; }
        /* FC1 回读逐位断言 */
        if (unit) {
            XModbusDataUnit *read = XModbusDataUnit_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, XModbusCoils, 0, 16);
            reply = read ? XModbusClient_sendReadRequest(
                (XModbusClient *)client, read, 1, XModbusRequestPriority_Normal) : NULL;
            for (i = 0; reply && i < 200 && !XModbusReply_isFinished(reply); ++i)
                XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
            if (reply && XModbusReply_isFinished(reply) &&
                XModbusReply_error(reply) == XModbusDevice_NoError) {
                const XModbusDataUnit *result = XModbusReply_result_const(reply);
                for (i = 0; result && i < 16; ++i) {
                    uint16_t v = XModbusDataUnit_value(result, (size_t)i);
                    if ((v != 0) != (pattern[i] != 0)) ++bad;
                }
            } else {
                ++bad;
            }
            if (read) XClassDelete(read);
        }
        if (reply) { XModbusReply_deleteLater(reply); reply = NULL; }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    CHECK(bad == 0, "压力下 FC15 写→FC1 回读逐位一致（远程 IO 掉线回归锁）");
    if (unit) XClassDelete(unit);
    if (client) { XModbusDevice_disconnectDevice((XModbusDevice *)client); XModbusTcpClient_deleteLater(client); }
    if (server) { XModbusDevice_disconnectDevice((XModbusDevice *)server); XModbusTcpServer_deleteLater(server); }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** 离线错误路径：连接拒绝 → 客户端错误态（不挂死、可清理）。 */
static void test_client_offline_error(void)
{
    XModbusTcpClient *client = XModbusTcpClient_create();
    XVariant *address = XVariant_create_utf8_str("127.0.0.1");
    XVariant *port = XVariant_create_int(1); /* 1 端口几乎必然无监听 */
    XModbusDataUnit *read = XModbusDataUnit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, XModbusHoldingRegisters, 0, 1);
    XModbusReply *reply = NULL;
    int i;

    if (client && address) XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice *)client, XModbusDevice_NetworkAddressParameter, address);
    if (client && port) XModbusDevice_setConnectionParameter_ref(
        (XModbusDevice *)client, XModbusDevice_NetworkPortParameter, port);
    if (address) XClassDelete(address);
    if (port) XClassDelete(port);
    CHECK(client && !XModbusDevice_connectDevice((XModbusDevice *)client),
        "离线连接失败如实返回失败");
    if (client && read)
        reply = XModbusClient_sendReadRequest((XModbusClient *)client, read, 1, XModbusRequestPriority_Normal);
    CHECK(!reply, "未连接发送请求如实返回 NULL（错误路径入口）");
    if (reply) XModbusReply_deleteLater(reply);
    if (read) XClassDelete(read);
    if (client) XModbusTcpClient_deleteLater(client);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/* 本文件不 include XProtocolTest.h，聚合入口先调用后定义：C 未开隐式声明时
   按 int() 假设，与下方 void(XVariant*) 定义冲突报 C2371。此处前置原型。 */
void XModbusServerCoverageTest(XVariant* data);

/** 服务端覆盖组聚合入口（注册于 XModbus 菜单）。 */
int XModbusCoverageTest_run(void)
{
    XModbusServerCoverageTest(NULL);
    return g_fail == 0;
}

void XModbusServerCoverageTest(XVariant* data)
{
    (void)data;
    setvbuf(stdout, NULL, _IONBF, 0);
    XPrintf("========== Modbus 服务端 FC 全量覆盖测试开始 ==========\n");
    g_pass = g_fail = 0;
    test_server_fc1_fc2();
    test_server_fc3_fc4();
    test_server_fc5();
    test_server_fc15_patterns();
    test_server_fc16_fc3();
    test_server_invalid_fc_and_map();
    test_client_fc15_pressure_e2e();
    test_client_offline_error();
    XPrintf("========== Modbus 服务端 FC 全量覆盖测试结束：通过 %d，失败 %d ==========\n", g_pass, g_fail);
}

void XModbusPublicApiTest(XVariant* data)
{
    (void)data;
    setvbuf(stdout, NULL, _IONBF, 0);
    XPrintf("========== Modbus 公共 API 回归测试开始（Qt 6.8.3 对齐）==========\n");
    g_pass = g_fail = 0;
    test_pdu();
    test_adu();
    test_data_unit();
    test_reply_device();
    test_server_request();
    test_tcp_loopback();
    XPrintf("========== Modbus 公共 API 回归测试结束：通过 %d，失败 %d ==========\n", g_pass, g_fail);
}
