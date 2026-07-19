#include"XProtocolStackTest.h"
#include"XModbusRtuSerialClient.h"
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

void XMenu_XModbusTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XModbus(modbus)");
    {
        XAction* action = XMenu_addAction(menu, "CommEvent单元测试");
        XAction_setAction(action, XModbusCommEventTest);
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