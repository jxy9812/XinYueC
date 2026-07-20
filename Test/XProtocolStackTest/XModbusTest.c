#include"XProtocolStackTest.h"
#include"XModbusRtuSerialClient.h"
#include"XModbusAdu.h"
#include"XModbusTcpClient.h"
#include"XSerialPort.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XTimerGroupBase.h"
#include"XSwitchDeviceModbus.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XTimer.h"
//static void deinit_slot(XObject* receiver, void* argList, XObject* sender)
//{
//    XPrintf("sender:%p receiver:%p 串口释放\n",sender,receiver);
//}
//
//static void connected_slot(XObject* receiver, void* argList, XObject* sender)
//{
//    XPrintf("sender:%p receiver:%p 网络已连接\n", sender, receiver);
//}
//static void stateChanged_slot(XObject* receiver, XSocketState state, XObject* sender)
//{
//    XPrintf("sender:%p receiver:%p 状态改变:%d\n", sender, receiver,state);
//}
//static XSwitchDeviceModbus* SW;
//static void StateChangeCallback0(XSwitchDeviceBase* sw)
//{
//    printf("in0:%s\n",sw->m_state?"true":"false");
//    XSwitchDeviceBase_setState_base(SW, sw->m_state);
//}
//static void StateChangeCallback1(XSwitchDeviceBase* sw)
//{
//    printf("in1:%s\n", sw->m_state ? "true" : "false");
//}
static void rtuFinished(XObject* sender, XVarList* args)
{
    //XPrintf("结束了\n");
    XModbusClient* client = XObject_parent(sender);
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);
    XModbusDataUnit_setValue(read, 0, true);
    XModbusReply* reply = XModbusClient_sendWriteRequest(client, read, 1);
    XObject_setParent(reply, client);
    XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), rtuFinished);
    XObject_connect_1(reply, XSignal(XModbusReply_finished_signal), reply, XObject_deleteLater, XConnectionType_Auto);
    //XObject_deleteLater(sender);
    XModbusDataUnit_delete_base(read);
}

void XModbusRtuSerialClientTest()
{
    XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
    XModbusClient_setAutoReconnect(client, true);
    XModbusDevice_setConnectionParameter_ref(client, XModbusDevice_SerialPortNameParameter,XVariant_create_utf8_str("COM26"));
    XModbusDevice_setConnectionParameter_ref(client, XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
    if (!XModbusDevice_connectDevice(client))
    {
        XPrintf_3("失败了\n");
        XModbusRtuSerialClient_deleteLater(client);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return;
    }
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils,0,1);
    XModbusDataUnit_setValue(read,0,true);

    XModbusReply* reply= XModbusClient_sendWriteRequest(client, read,1);
    XObject_setParent(reply, client);
    XObject_connect_2(reply,XSignal(XModbusReply_finished_signal), rtuFinished);


    //XModbusClient_pollWriteRequest(client, read, 1,10);

    XCoreApplication_exec();
}
static void tcpFinished(XObject* receiver, XVarList* args)
{
    XObject* sender = receiver;
    XCoreApplication_processEvents(0);
    //XPrintf("结束了，准备重启\n");
    XModbusRtuSerialClient* rtu = XObject_parent(sender);
    XModbusTcpClient* client = XObject_parent(sender);
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);
    XModbusDataUnit_setValue(read, 0, true);
    XModbusReply* reply = XModbusClient_sendWriteRequest(client, read, 1);
    if (!read)return;
    XObject_setParent(reply, client);
    XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), tcpFinished);
    //XObject_connect_1(reply, XSignal(XModbusReply_finished_signal), reply, tcpFinished, XConnectionType_Queued);
   //XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), XObject_deleteLater);
    XObject_deleteLater(sender);
    //XPrintf("请求释放:%p\n", sender);
    //XClass_delete_base(sender);
    XModbusDataUnit_delete_base(read);
}

static void tcpStart(XObject* sender, XVarList* args)
{
    //XPrintf("结束了\n");
    XModbusTcpClient* client = XObject_parent(sender);
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);
    XModbusDataUnit_setValue(read, 0, true);
    XModbusReply* reply = XModbusClient_sendWriteRequest(client, read, 1);
    if (!read)return;
    XObject_setParent(reply, client);
    XObject_connect_1(reply, XSignal(XModbusReply_finished_signal), reply,tcpFinished,XConnectionType_Queued);

    XModbusDataUnit_delete_base(read);

}
void XModbusTcpClientTest()
{
     // 创建TCP客户端
    XModbusTcpClient* client = XModbusTcpClient_create();
    XModbusClient_setAutoReconnect(client,true);
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
    XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("192.168.1.117"));
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
    XModbusDevice_NetworkPortParameter, XVariant_create_int(502));
  
    if (!XModbusDevice_connectDevice(client))
    {
        XPrintf_3("失败了\n");
        XModbusTcpClient_deleteLater(client);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return;
    }
    XObject_connect_2((XObject*)XModbusDevice_device(client),XSignal(XTcpSocket_connected_signal), tcpStart);

    //{
    //    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);
    //    XModbusDataUnit_setValue(read, 0, true);
    //    XModbusClient_pollWriteRequest(client, read, 1, 2);
    //}

    XCoreApplication_exec();
}

#include "XModbusCommEvent.h"

/**
 * @brief XModbusCommEvent 单元测试
 * @details 测试所有 API 函数，验证枚举值、位操作和事件判断
 */
void XModbusCommEventTest()
{
    int pass = 0, fail = 0;
    XPrintf_3("========== XModbusCommEvent 单元测试开始 ==========\n");

    // 1. 测试 create 和默认值
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_InitiatedCommunicationRestart);
        uint8_t byte = XModbusCommEvent_toUint8(&e);
        if (byte == 0x00) {
            XPrintf("  [PASS] create(InitiatedCommunicationRestart) = 0x%02X\n", byte);
            pass++;
        } else {
            XPrintf("  [FAIL] create(InitiatedCommunicationRestart) = 0x%02X, expected 0x00\n", byte);
            fail++;
        }
    }

    // 2. 测试 fromUint8 和 toUint8
    {
        uint8_t testByte = 0xA5;
        XModbusCommEvent e = XModbusCommEvent_fromUint8(testByte);
        uint8_t result = XModbusCommEvent_toUint8(&e);
        if (result == testByte) {
            XPrintf("  [PASS] fromUint8/toUint8 roundtrip: 0x%02X\n", result);
            pass++;
        } else {
            XPrintf("  [FAIL] fromUint8/toUint8: 0x%02X, expected 0x%02X\n", result, testByte);
            fail++;
        }
    }

    // 3. 测试 NULL 指针保护
    {
        bool ok = true;
        if (XModbusCommEvent_toUint8(NULL) != 0) ok = false;
        if (XModbusCommEvent_toEventByte(NULL) != XModbusCommEvent_InitiatedCommunicationRestart) ok = false;
        if (XModbusCommEvent_testSendFlag(NULL, XModbusCommEvent_ReadExceptionSent) != false) ok = false;
        if (XModbusCommEvent_testReceiveFlag(NULL, XModbusCommEvent_CommunicationError) != false) ok = false;
        if (XModbusCommEvent_isSentEvent(NULL) != false) ok = false;
        if (XModbusCommEvent_isReceiveEvent(NULL) != false) ok = false;
        if (XModbusCommEvent_isListenOnlyMode(NULL) != false) ok = false;
        if (XModbusCommEvent_isRestartEvent(NULL) != false) ok = false;
        if (ok) {
            XPrintf_3("  [PASS] NULL 指针保护全部正确\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] NULL 指针保护异常\n");
            fail++;
        }
    }

    // 4. 测试 setEventByte
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_InitiatedCommunicationRestart);
        XModbusCommEvent_setEventByte(&e, XModbusCommEvent_SentEvent);
        if (XModbusCommEvent_toUint8(&e) == 0x40) {
            XPrintf("  [PASS] setEventByte(SentEvent) = 0x%02X\n", XModbusCommEvent_toUint8(&e));
            pass++;
        } else {
            XPrintf("  [FAIL] setEventByte(SentEvent) = 0x%02X, expected 0x40\n", XModbusCommEvent_toUint8(&e));
            fail++;
        }
    }

    // 5. 测试 orWithSendFlag 和 testSendFlag
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_SentEvent);
        XModbusCommEvent_orWithSendFlag(&e, XModbusCommEvent_ReadExceptionSent);
        // SentEvent(0x40) | ReadExceptionSent(0x01) = 0x41
        uint8_t byte = XModbusCommEvent_toUint8(&e);
        if (byte == 0x41 &&
            XModbusCommEvent_testSendFlag(&e, XModbusCommEvent_ReadExceptionSent) &&
            !XModbusCommEvent_testSendFlag(&e, XModbusCommEvent_ServerAbortExceptionSent) &&
            XModbusCommEvent_isSentEvent(&e)) {
            XPrintf("  [PASS] orWithSendFlag(ReadException) = 0x%02X, testSendFlag=true, isSent=true\n", byte);
            pass++;
        } else {
            XPrintf("  [FAIL] orWithSendFlag(ReadException) = 0x%02X, expected 0x41\n", byte);
            fail++;
        }
    }

    // 6. 测试 orWithReceiveFlag 和 testReceiveFlag
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_ReceiveEvent);
        XModbusCommEvent_orWithReceiveFlag(&e, XModbusCommEvent_BroadcastReceived);
        // ReceiveEvent(0x80) | BroadcastReceived(0x40) = 0xC0
        uint8_t byte = XModbusCommEvent_toUint8(&e);
        if (byte == 0xC0 &&
            XModbusCommEvent_testReceiveFlag(&e, XModbusCommEvent_BroadcastReceived) &&
            !XModbusCommEvent_testReceiveFlag(&e, XModbusCommEvent_CommunicationError) &&
            XModbusCommEvent_isReceiveEvent(&e)) {
            XPrintf("  [PASS] orWithReceiveFlag(Broadcast) = 0x%02X, testReceiveFlag=true, isReceive=true\n", byte);
            pass++;
        } else {
            XPrintf("  [FAIL] orWithReceiveFlag(Broadcast) = 0x%02X, expected 0xC0\n", byte);
            fail++;
        }
    }

    // 7. 测试 isListenOnlyMode
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_EnteredListenOnlyMode);
        if (XModbusCommEvent_isListenOnlyMode(&e)) {
            XPrintf_3("  [PASS] isListenOnlyMode(EnteredListenOnlyMode) = true\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] isListenOnlyMode(EnteredListenOnlyMode) = false\n");
            fail++;
        }
    }

    // 8. 测试 isRestartEvent
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_InitiatedCommunicationRestart);
        if (XModbusCommEvent_isRestartEvent(&e)) {
            XPrintf_3("  [PASS] isRestartEvent(InitiatedCommunicationRestart) = true\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] isRestartEvent(InitiatedCommunicationRestart) = false\n");
            fail++;
        }
    }

    // 9. 测试 combineEventByte
    {
        XModbusCommEvent_EventByte result = XModbusCommEvent_combineEventByte(
            XModbusCommEvent_SentEvent, XModbusCommEvent_ServerAbortExceptionSent);
        // SentEvent(0x40) | ServerAbortExceptionSent(0x02) = 0x42
        if ((uint8_t)result == 0x42) {
            XPrintf("  [PASS] combineEventByte(SentEvent, ServerAbort) = 0x%02X\n", (uint8_t)result);
            pass++;
        } else {
            XPrintf("  [FAIL] combineEventByte(SentEvent, ServerAbort) = 0x%02X, expected 0x42\n", (uint8_t)result);
            fail++;
        }
    }

    // 10. 测试 combineEventByteWithReceive
    {
        XModbusCommEvent_EventByte result = XModbusCommEvent_combineEventByteWithReceive(
            XModbusCommEvent_ReceiveEvent, XModbusCommEvent_CommunicationError);
        // ReceiveEvent(0x80) | CommunicationError(0x02) = 0x82
        if ((uint8_t)result == 0x82) {
            XPrintf("  [PASS] combineEventByteWithReceive(ReceiveEvent, CommError) = 0x%02X\n", (uint8_t)result);
            pass++;
        } else {
            XPrintf("  [FAIL] combineEventByteWithReceive(ReceiveEvent, CommError) = 0x%02X, expected 0x82\n", (uint8_t)result);
            fail++;
        }
    }

    // 11. 测试多标志组合
    {
        XModbusCommEvent e = XModbusCommEvent_create(XModbusCommEvent_ReceiveEvent);
        XModbusCommEvent_orWithReceiveFlag(&e, XModbusCommEvent_CharacterOverrun);
        XModbusCommEvent_orWithReceiveFlag(&e, XModbusCommEvent_CommunicationError);
        // ReceiveEvent(0x80) | CharacterOverrun(0x10) | CommunicationError(0x02) = 0x92
        uint8_t byte = XModbusCommEvent_toUint8(&e);
        if (byte == 0x92 &&
            XModbusCommEvent_testReceiveFlag(&e, XModbusCommEvent_CharacterOverrun) &&
            XModbusCommEvent_testReceiveFlag(&e, XModbusCommEvent_CommunicationError) &&
            !XModbusCommEvent_testReceiveFlag(&e, XModbusCommEvent_BroadcastReceived) &&
            XModbusCommEvent_isReceiveEvent(&e)) {
            XPrintf("  [PASS] 多标志组合: 0x%02X, 所有位测试正确\n", byte);
            pass++;
        } else {
            XPrintf("  [FAIL] 多标志组合: 0x%02X, expected 0x92\n", byte);
            fail++;
        }
    }

    // 12. 测试 setEventByte 重置事件
    {
        XModbusCommEvent e = XModbusCommEvent_fromUint8(0xFF);
        XModbusCommEvent_setEventByte(&e, XModbusCommEvent_InitiatedCommunicationRestart);
        if (XModbusCommEvent_isRestartEvent(&e) && XModbusCommEvent_toUint8(&e) == 0x00) {
            XPrintf_3("  [PASS] setEventByte 重置为 InitiatedCommunicationRestart = 0x00\n");
            pass++;
        } else {
            XPrintf("  [FAIL] setEventByte 重置后 = 0x%02X, expected 0x00\n", XModbusCommEvent_toUint8(&e));
            fail++;
        }
    }

    // 13. 测试 setEventByte 对 NULL 的保护
    {
        XModbusCommEvent_setEventByte(NULL, XModbusCommEvent_ReceiveEvent);
        XPrintf_3("  [PASS] setEventByte(NULL) 不崩溃\n");
        pass++;
    }

    // 14. 测试 orWithSendFlag 对 NULL 的保护
    {
        XModbusCommEvent* result = XModbusCommEvent_orWithSendFlag(NULL, XModbusCommEvent_ReadExceptionSent);
        if (result == NULL) {
            XPrintf_3("  [PASS] orWithSendFlag(NULL) 返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] orWithSendFlag(NULL) 返回非 NULL\n");
            fail++;
        }
    }

    XPrintf("========== XModbusCommEvent 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}


// =============== XModbusAdu 单元测试 ===============

void XModbusAduTest(void)
{
    int pass = 0, fail = 0;
    XPrintf_3("========== XModbusAdu 单元测试开始 ==========\n");

    // ========== 1. 测试 init/deinit ==========
    {
        XModbusAdu adu;
        XModbusAdu_init(&adu);
        if (adu.m_rawData == NULL && adu.m_data == NULL && adu.m_serverAddress == 0xFF) {
            XPrintf_3("  [PASS] init 初始化正确\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] init 初始化失败\n");
            fail++;
        }
        XModbusAdu_deinit(&adu);
        if (adu.m_rawData == NULL && adu.m_data == NULL) {
            XPrintf_3("  [PASS] deinit 释放正确\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] deinit 释放失败\n");
            fail++;
        }
    }

    // ========== 2. 测试 delete ==========
    {
        XModbusAdu* adu = (XModbusAdu*)XMalloc_System(sizeof(XModbusAdu));
        XModbusAdu_init(adu);
        XModbusAdu_delete(adu);
        XPrintf_3("  [PASS] delete 删除正确\n");
        pass++;
    }

    // ========== 3. NULL 指针测试 ==========
    {
        bool ok = true;
        XModbusAdu_init(NULL);
        XModbusAdu_deinit(NULL);
        XModbusAdu_delete(NULL);
        if (XModbusAdu_size(NULL) != -1) ok = false;
        if (XModbusAdu_data(NULL) != NULL) ok = false;
        if (XModbusAdu_rawSize(NULL) != -1) ok = false;
        if (XModbusAdu_rawData(NULL) != NULL) ok = false;
        if (XModbusAdu_serverAddress(NULL) != -1) ok = false;
        if (XModbusAdu_matchingChecksum(NULL) != false) ok = false;
        {
            XModbusPdu pdu;
            if (XModbusAdu_pdu(NULL, &pdu) != false) ok = false;
        }
        if (ok) {
            XPrintf_3("  [PASS] NULL 指针保护正确\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] NULL 指针保护异常\n");
            fail++;
        }
    }

    // ========== 4. 测试 RTU 帧格式 ==========
    {
        XModbusPdu pdu;
        XModbusPdu_init_with_code(&pdu, XModbusPdu_ReadHoldingRegisters);
        uint8_t pduData[] = {0x00, 0x00, 0x00, 0x0A};
        XModbusPdu_setData(&pdu, pduData, 4);

        XByteArray* frame = XModbusAdu_createRtuFrame(1, &pdu);
        if (!frame) {
            XPrintf_3("  [FAIL] createRtuFrame 返回 NULL\n");
            fail++;
        } else {
            size_t frameSize = XByteArray_size_base(frame);
            uint8_t* raw = XByteArray_data(frame);

            if (frameSize == 8 && raw[0] == 0x01 && raw[1] == 0x03 &&
                raw[2] == 0x00 && raw[3] == 0x00 && raw[4] == 0x00 && raw[5] == 0x0A) {
                XPrintf_3("  [PASS] createRtuFrame 格式正确\n");
                pass++;
            } else {
                XPrintf("  [FAIL] createRtuFrame 格式错误: size=%zu, [0]=0x%02X, [1]=0x%02X\n",
                        frameSize, raw[0], raw[1]);
                fail++;
            }

            XModbusAdu* adu = XModbusAdu_parseRtu(raw, frameSize);
            if (!adu) {
                XPrintf_3("  [FAIL] parseRtu 返回 NULL\n");
                fail++;
            } else {
                bool ok = true;
                if (XModbusAdu_serverAddress(adu) != 1) ok = false;
                if (adu->m_type != XModbusAdu_Rtu) ok = false;
                if (XModbusAdu_size(adu) != 6) ok = false;

                XByteArray* d = XModbusAdu_data(adu);
                if (!d || XByteArray_size_base(d) != 6) ok = false;
                if (d) {
                    uint8_t* dRaw = XByteArray_data(d);
                    if (dRaw[0] != 0x01 || dRaw[1] != 0x03 ||
                        dRaw[2] != 0x00 || dRaw[3] != 0x00 ||
                        dRaw[4] != 0x00 || dRaw[5] != 0x0A) ok = false;
                    XByteArray_delete_base(d);
                }

                if (XModbusAdu_rawSize(adu) != (int)frameSize) ok = false;

                XByteArray* rd = XModbusAdu_rawData(adu);
                if (!rd || XByteArray_size_base(rd) != frameSize) ok = false;
                if (rd) XByteArray_delete_base(rd);

                if (!XModbusAdu_matchingChecksum(adu)) ok = false;

                XModbusPdu outPdu;
                if (!XModbusAdu_pdu(adu, &outPdu)) ok = false;
                else {
                    if (XModbusPdu_functionCodeRaw(&outPdu) != XModbusPdu_ReadHoldingRegisters) ok = false;
                    XByteArray* outData = XModbusPdu_data(&outPdu);
                    if (!outData || XByteArray_size_base(outData) != 4) ok = false;
                    if (outData) {
                        uint8_t* oRaw = XByteArray_data(outData);
                        if (oRaw[0] != 0x00 || oRaw[1] != 0x00 ||
                            oRaw[2] != 0x00 || oRaw[3] != 0x0A) ok = false;
                        XByteArray_delete_base(outData);
                    }
                    XModbusPdu_deinit_base(&outPdu);
                }

                if (ok) {
                    XPrintf_3("  [PASS] createRtuFrame + parseRtu 完整正确\n");
                    pass++;
                } else {
                    XPrintf_3("  [FAIL] createRtuFrame + parseRtu 结果异常\n");
                    fail++;
                }
                XModbusAdu_delete(adu);
            }
            XByteArray_delete_base(frame);
        }
        XModbusPdu_deinit_base(&pdu);
    }

    // ========== 5. 测试 ASCII 帧格式 ==========
    {
        XModbusPdu pdu;
        XModbusPdu_init_with_code(&pdu, XModbusPdu_ReadHoldingRegisters);
        uint8_t pduData[] = {0x00, 0x00, 0x00, 0x0A};
        XModbusPdu_setData(&pdu, pduData, 4);

        XByteArray* frame = XModbusAdu_createAsciiFrame(1, &pdu, '\n');
        if (!frame) {
            XPrintf_3("  [FAIL] createAsciiFrame 返回 NULL\n");
            fail++;
        } else {
            size_t frameSize = XByteArray_size_base(frame);
            uint8_t* raw = XByteArray_data(frame);

            if (frameSize >= 5 && raw[0] == ':' && raw[frameSize - 2] == '\r' && raw[frameSize - 1] == '\n') {
                XPrintf_3("  [PASS] createAsciiFrame 格式正确\n");
                pass++;
            } else {
                XPrintf("  [FAIL] createAsciiFrame 格式错误: size=%zu, [0]=0x%02X\n", frameSize, raw[0]);
                fail++;
            }

            XModbusAdu* adu = XModbusAdu_parseAscii(raw, frameSize);
            if (!adu) {
                XPrintf_3("  [FAIL] parseAscii 返回 NULL\n");
                fail++;
            } else {
                bool ok = true;
                if (XModbusAdu_serverAddress(adu) != 1) ok = false;
                if (adu->m_type != XModbusAdu_Ascii) ok = false;
                if (XModbusAdu_size(adu) != 6) ok = false;

                XByteArray* d = XModbusAdu_data(adu);
                if (!d || XByteArray_size_base(d) != 6) ok = false;
                if (d) {
                    uint8_t* dRaw = XByteArray_data(d);
                    if (dRaw[0] != 0x01 || dRaw[1] != 0x03 ||
                        dRaw[2] != 0x00 || dRaw[3] != 0x00 ||
                        dRaw[4] != 0x00 || dRaw[5] != 0x0A) ok = false;
                    XByteArray_delete_base(d);
                }

                if (!XModbusAdu_matchingChecksum(adu)) ok = false;

                if (ok) {
                    XPrintf_3("  [PASS] createAsciiFrame + parseAscii 完整正确\n");
                    pass++;
                } else {
                    XPrintf_3("  [FAIL] createAsciiFrame + parseAscii 结果异常\n");
                    fail++;
                }
                XModbusAdu_delete(adu);
            }
            XByteArray_delete_base(frame);
        }
        XModbusPdu_deinit_base(&pdu);
    }

    // ========== 6. parseRtu 边界测试 ==========
    {
        uint8_t shortData[] = {0x01, 0x03, 0x00};
        XModbusAdu* adu = XModbusAdu_parseRtu(shortData, 3);
        if (adu == NULL) {
            XPrintf_3("  [PASS] parseRtu 短数据返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] parseRtu 短数据未返回 NULL\n");
            fail++;
            XModbusAdu_delete(adu);
        }

        adu = XModbusAdu_parseRtu(NULL, 10);
        if (adu == NULL) {
            XPrintf_3("  [PASS] parseRtu NULL 输入返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] parseRtu NULL 输入未返回 NULL\n");
            fail++;
            XModbusAdu_delete(adu);
        }
    }

    // ========== 7. parseAscii 边界测试 ==========
    {
        uint8_t badStart[] = "01030000000A";
        XModbusAdu* adu = XModbusAdu_parseAscii(badStart, sizeof(badStart) - 1);
        if (adu == NULL) {
            XPrintf_3("  [PASS] parseAscii 无':'前缀返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] parseAscii 无':'前缀未返回 NULL\n");
            fail++;
            XModbusAdu_delete(adu);
        }

        uint8_t shortData2[] = ":0103";
        adu = XModbusAdu_parseAscii(shortData2, 5);
        if (adu == NULL) {
            XPrintf_3("  [PASS] parseAscii 短数据返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] parseAscii 短数据未返回 NULL\n");
            fail++;
            XModbusAdu_delete(adu);
        }

        adu = XModbusAdu_parseAscii(NULL, 10);
        if (adu == NULL) {
            XPrintf_3("  [PASS] parseAscii NULL 输入返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] parseAscii NULL 输入未返回 NULL\n");
            fail++;
            XModbusAdu_delete(adu);
        }
    }

    // ========== 8. 校验和测试 ==========
    {
        uint8_t testData[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
        uint8_t lrc = XModbusAdu_calculateLRC(testData, 6);
        if (lrc == 0xF2) {
            XPrintf_3("  [PASS] calculateLRC 正确\n");
            pass++;
        } else {
            XPrintf("  [FAIL] calculateLRC = 0x%02X, expected 0xF2\n", lrc);
            fail++;
        }

        uint16_t crc = XModbusAdu_calculateCRC(testData, 6);
        uint16_t expectedCrc = XCrc_get16(testData, 6);
        if (crc == expectedCrc) {
            XPrintf_3("  [PASS] calculateCRC 正确\n");
            pass++;
        } else {
            XPrintf("  [FAIL] calculateCRC = 0x%04X, expected 0x%04X\n", crc, expectedCrc);
            fail++;
        }
    }

    // ========== 9. 错误校验测试 ==========
    {
        uint8_t badFrame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00};
        XModbusAdu* adu = XModbusAdu_parseRtu(badFrame, 8);
        if (adu && !XModbusAdu_matchingChecksum(adu)) {
            XPrintf_3("  [PASS] 错误CRC校验正确检测\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] 错误CRC校验未检测\n");
            fail++;
        }
        if (adu) XModbusAdu_delete(adu);
    }

    // ========== 10. pdu() 提取测试 ==========
    {
        XModbusPdu pdu;
        XModbusPdu_init_with_code(&pdu, XModbusPdu_ReadCoils);
        uint8_t pduData2[] = {0x00, 0x00, 0x00, 0x01};
        XModbusPdu_setData(&pdu, pduData2, 4);

        XByteArray* frame2 = XModbusAdu_createRtuFrame(1, &pdu);
        if (frame2) {
            uint8_t* raw2 = XByteArray_data(frame2);
            XModbusAdu* adu2 = XModbusAdu_parseRtu(raw2, XByteArray_size_base(frame2));
            if (adu2) {
                XModbusPdu outPdu;
                XModbusAdu_pdu(adu2, &outPdu);
                if (!XModbusPdu_isException(&outPdu) &&
                    XModbusPdu_functionCodeRaw(&outPdu) == XModbusPdu_ReadCoils) {
                    XPrintf_3("  [PASS] pdu() 提取 XModbusPdu 正确\n");
                    pass++;
                } else {
                    XPrintf_3("  [FAIL] pdu() 提取失败\n");
                    fail++;
                }
                XModbusPdu_deinit_base(&outPdu);
                XModbusAdu_delete(adu2);
            }
            XByteArray_delete_base(frame2);
        }
        XModbusPdu_deinit_base(&pdu);
    }

    // ========== 11. createRtuFrame NULL PDU 测试 ==========
    {
        XByteArray* frame3 = XModbusAdu_createRtuFrame(1, NULL);
        if (frame3 == NULL) {
            XPrintf_3("  [PASS] createRtuFrame(NULL PDU) 返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] createRtuFrame(NULL PDU) 未返回 NULL\n");
            fail++;
            XByteArray_delete_base(frame3);
        }
    }

    // ========== 12. createAsciiFrame NULL PDU 测试 ==========
    {
        XByteArray* frame4 = XModbusAdu_createAsciiFrame(1, NULL, '\n');
        if (frame4 == NULL) {
            XPrintf_3("  [PASS] createAsciiFrame(NULL PDU) 返回 NULL\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] createAsciiFrame(NULL PDU) 未返回 NULL\n");
            fail++;
            XByteArray_delete_base(frame4);
        }
    }

    // ========== 13. 多地址循环测试 ==========
    {
        XModbusPdu pdu3;
        XModbusPdu_init_with_code(&pdu3, XModbusPdu_WriteSingleRegister);
        uint8_t pduData3[] = {0x00, 0x01, 0x00, 0x0A};
        XModbusPdu_setData(&pdu3, pduData3, 4);

        bool ok = true;
        for (int i = 0; i < 100; i++) {
            XByteArray* rtuFrame = XModbusAdu_createRtuFrame(i & 0xFF, &pdu3);
            XByteArray* asciiFrame = XModbusAdu_createAsciiFrame(i & 0xFF, &pdu3, '\n');
            if (!rtuFrame || !asciiFrame) ok = false;
            if (rtuFrame) XByteArray_delete_base(rtuFrame);
            if (asciiFrame) XByteArray_delete_base(asciiFrame);
        }

        if (ok) {
            XPrintf_3("  [PASS] 100次循环RTU/ASCII均成功\n");
            pass++;
        } else {
            XPrintf_3("  [FAIL] 循环RTU/ASCII失败\n");
            fail++;
        }
        XModbusPdu_deinit_base(&pdu3);
    }

    XPrintf("========== XModbusAdu 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

void XMenu_XModbusTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XModbus(modbus)");
    {
        XAction* action = XMenu_addAction(menu, "CommEvent单元测试");
        XAction_setAction(action, XModbusCommEventTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "Adu单元测试");
        XAction_setAction(action, XModbusAduTest);
    }
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "RtuSerialClient测试");
        XAction_setAction(action, XModbusRtuSerialClientTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "TcpClient测试");
        XAction_setAction(action, XModbusTcpClientTest);
    }
}
