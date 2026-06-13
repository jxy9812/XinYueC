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
    XModbusRtuSerialClient* rtu = XModbusRtuSerialClient_create();
    XModbusDevice_setConnectionParameter_ref(rtu, XModbusDevice_SerialPortNameParameter,XVariant_create_utf8_str("COM26"));
    XModbusDevice_setConnectionParameter_ref(rtu, XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
    if (!XModbusDevice_connectDevice(rtu))
    {
        XPrintf_utf8("失败了\n");
        XModbusRtuSerialClient_deleteLater(rtu);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return;
    }
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils,0,1);
    XModbusDataUnit_setValue(read,0,true);

    XModbusReply* reply= XModbusClient_sendWriteRequest(rtu, read,1);
    XObject_setParent(reply, rtu);
    XObject_connect_2(reply,XSignal(XModbusReply_finished_signal), rtuFinished);

     //reply = XModbusClient_sendWriteRequest(client, read, 1);

    //XModbusClient_pollWriteRequest(client, read, 1,10);

    XCoreApplication_exec();
}
static void tcpFinished(XObject* sender, XVarList* args)
{
    //XPrintf("结束了，准备重启\n");
    XModbusRtuSerialClient* rtu = XObject_parent(sender);
    XModbusTcpClient* client = XObject_parent(sender);
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);
    XModbusDataUnit_setValue(read, 0, true);
    XModbusReply* reply = XModbusClient_sendWriteRequest(client, read, 1);
    if (!read)return;
    XObject_setParent(reply, client);
    XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), tcpFinished);
    //XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), XObject_deleteLater);
    XObject_deleteLater(sender);

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
    XObject_connect_2(reply, XSignal(XModbusReply_finished_signal), tcpFinished);

    XModbusDataUnit_delete_base(read);

}
void XModbusTcpClientTest()
{
     // 创建TCP客户端
    XModbusTcpClient* client = XModbusTcpClient_create();
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
    XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("192.168.1.117"));
    XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
    XModbusDevice_NetworkPortParameter, XVariant_create_int(502));
  
    if (!XModbusDevice_connectDevice(client))
    {
        XPrintf_utf8("失败了\n");
        XModbusTcpClient_deleteLater(client);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return;
    }
    XObject_connect_2((XObject*)XModbusDevice_device(client),XSignal(XTcpSocket_connected_signal), tcpStart);

    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils, 0, 1);

    XModbusDataUnit_setValue(read, 0, true);
  
    
    //XModbusClient_pollWriteRequest(client, read, 1, 2);
    XCoreApplication_exec();
}

void XMenu_XModbusTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XModbus(modbus)");
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