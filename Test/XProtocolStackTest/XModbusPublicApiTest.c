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
    if (expr) { ++g_pass; printf("  [通过] %s\n", name); } \
    else { ++g_fail; printf("  [失败] %s\n", name); } \
} while (0)

static void delete_pdu(XModbusPdu *pdu)
{
    if (pdu) XModbusPdu_delete_base(pdu);
}

static bool configure_holding_registers(XModbusServer *server, uint16_t start, size_t count)
{
    XModbusDataUnitMap *map = XModbusDataUnitMap_create();
    XModbusDataUnit *unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, start, count);
    XModbusRegisterType type = XModbusHoldingRegisters;
    bool ok = false;

    if (map && unit && XMap_insert_base(map, &type, unit))
        ok = XModbusServer_setMap_base(server, map);
    if (unit) XModbusDataUnit_delete_base(unit);
    if (map) XModbusDataUnitMap_delete_base(map);
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
    if (exception) XModbusExceptionResponse_delete_base(exception);
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
    if (pduOk) XModbusPdu_deinit_base(&parsedPdu);
    if (parsedRtu) XModbusAdu_delete(parsedRtu);
    if (parsedAscii) XModbusAdu_delete(parsedAscii);
    if (rtu) XByteArray_delete_base(rtu);
    if (ascii) XByteArray_delete_base(ascii);
    if (request) XModbusRequest_delete_base(request);
}

static void test_data_unit(void)
{
    XModbusDataUnit *invalid = XModbusDataUnit_create();
    XModbusDataUnit *registers = XModbusDataUnit_create_ex(XModbusHoldingRegisters, 10, 3);
    XModbusDataUnit *coils = XModbusDataUnit_create_ex(XModbusCoils, 20, 4);
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
    if (copyValues) XVector_delete_base(copyValues);
    if (values) XVector_delete_base(values);
    if (bits) XBitArray_delete_base(bits);
    if (invalid) XModbusDataUnit_delete_base(invalid);
    if (registers) XModbusDataUnit_delete_base(registers);
    if (coils) XModbusDataUnit_delete_base(coils);
}

static void test_reply_device(void)
{
    XModbusReply *reply = XModbusReply_create(XModbusReply_Common, 7);
    XModbusDataUnit *unit = XModbusDataUnit_create_ex(XModbusHoldingRegisters, 2, 2);
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
        if (errors) XVector_delete_base(errors);
        if (errorText) XString_delete_base(errorText);
        {
            XModbusDataUnit *resultCopy = XModbusReply_result(reply);
            if (resultCopy) {
                XModbusDataUnit_setValue(resultCopy, 0, 0);
                CHECK(XModbusDataUnit_value(XModbusReply_result_const(reply), 0) == 0xAAAA,
                    "Reply 结果确实深拷贝");
                XModbusDataUnit_delete_base(resultCopy);
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
        if (variant) XVariant_delete_base(variant);
        variant = XModbusDevice_connectionParameter((XModbusDevice *)tcp,
            XModbusDevice_NetworkPortParameter);
        CHECK(variant && XVariant_toInt(variant) == 12345, "设备连接参数深拷贝");
        if (variant) XVariant_delete_base(variant);
    }
    CHECK(rtu && XModbusRtuSerialClient_turnaroundDelay(rtu) == 100,
        "RTU 客户端创建和默认参数");
    CHECK(rtuServer && XModbusRtuSerialServer_serialPort(rtuServer) != NULL &&
        rtuServer->m_turnaroundDelay == 100, "RTU 服务端创建和串口访问");
    if (reply) XModbusReply_deleteLater(reply);
    if (unit) XModbusDataUnit_delete_base(unit);
    if (response) XModbusResponse_delete_base(response);
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
    if (option) XVariant_delete_base(option);
    option = server ? XModbusServer_value_base(server, XModbusServer_ServerIdentifier) : NULL;
    CHECK(option && XVariant_toInt(option) == 42, "服务器选项读取");
    if (option) XVariant_delete_base(option);
    if (request) XModbusPdu_setData((XModbusPdu *)request, data, sizeof(data));
    response = server && request ? XModbusServer_processRequest_base(server, request) : NULL;
    CHECK(response && XModbusPdu_functionCode((XModbusPdu *)response) == XModbusPdu_ReadHoldingRegisters &&
        XModbusPdu_dataSize((XModbusPdu *)response) == 5, "服务器读取请求处理");
    if (response) XModbusResponse_delete_base(response);
    if (request) XModbusRequest_delete_base(request);
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
    if (address) XVariant_delete_base(address);
    if (port) XVariant_delete_base(port);
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
    read = XModbusDataUnit_create_ex(XModbusHoldingRegisters, 0, 1);
    if (client && connected && read)
        reply = XModbusClient_sendReadRequest((XModbusClient *)client, read, 1);
    for (i = 0; reply && i < 100 && !XModbusReply_isFinished(reply); ++i)
        XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents, 10);
    CHECK(reply && XModbusReply_isFinished(reply) && XModbusReply_error(reply) == XModbusDevice_NoError,
        "TCP Modbus 读请求联调完成");
    if (reply) {
        const XModbusDataUnit *result = XModbusReply_result_const(reply);
        CHECK(result && XModbusDataUnit_value(result, 0) == 0x1357, "TCP Modbus 读结果校验");
        XModbusReply_deleteLater(reply);
    }
    if (read) XModbusDataUnit_delete_base(read);
    if (client) { XModbusDevice_disconnectDevice((XModbusDevice *)client); XModbusTcpClient_deleteLater(client); }
    if (server) { XModbusDevice_disconnectDevice((XModbusDevice *)server); XModbusTcpServer_deleteLater(server); }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

void XModbusPublicApiTest(XVariant* data)
{
    (void)data;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("========== Modbus 公共 API 回归测试开始（Qt 6.8.3 对齐）==========\n");
    g_pass = g_fail = 0;
    test_pdu();
    test_adu();
    test_data_unit();
    test_reply_device();
    test_server_request();
    test_tcp_loopback();
    printf("========== Modbus 公共 API 回归测试结束：通过 %d，失败 %d ==========\n", g_pass, g_fail);
}
