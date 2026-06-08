#include"XProtocolStackTest.h"
#include"XModbusRtuSerialClient.h"
#include"XSerialPort.h"
#include"XMemory.h"
#include"XCrc.h"
//#include"XModbusFrame.h"
#include"XTimerGroupBase.h"
//#include"XModbusRegister.h"
//#include"XModbusDigitalSwitch.h"
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
//static void StateChangeCallback2(XSwitchDeviceBase* sw)
//{
//    printf("in2:%s\n", sw->m_state ? "true" : "false");
//}
void XModbusTest()
{
    XModbusRtuSerialClient* serial = XModbusRtuSerialClient_create();
    XModbusDevice_setConnectionParameter_ref(serial, XModbusDevice_SerialPortNameParameter,XVariant_create_utf8_str("COM26"));
    XModbusDevice_setConnectionParameter_ref(serial, XModbusDevice_SerialBaudRateParameter, XVariant_create_int(9600));
    if (!XModbusDevice_connectDevice(serial))
    {
        XPrintf_utf8("失败了\n");
        XModbusRtuSerialClient_deleteLater(serial);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    XModbusDataUnit* read = XModbusDataUnit_create_ex(XModbusCoils,0,1);
    XModbusDataUnit_setValue(read,0,true);
    XModbusReply* reply= XModbusClient_sendWriteRequest(serial, read,1);
     reply = XModbusClient_sendWriteRequest(serial, read, 1);
    XCoreApplication_exec();
}


void XMenu_XModbusTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XModbus(modbus)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "主测试");
        XAction_setAction(action, XModbusTest);
    }
}