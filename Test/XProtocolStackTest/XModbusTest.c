//#include"XProtocolStackTest.h"
//#include"XModbus.h"
//#include"XSerialPort.h"
//#include"XMemory.h"
//#include"XCrc.h"
//#include"XModbusFrame.h"
//#include"XTimerGroupBase.h"
//#include"XModbusRegister.h"
//#include"XModbusDigitalSwitch.h"
//#include"XSwitchDeviceModbus.h"
//#include"XSocket.h"
//#include"XMenu.h"
//#include"XAction.h"
//#include"XCoreApplication.h"
//#include"XPrintf.h"
//#include"XTimerBase.h"
//static void deinit_slot(XObject* receiver, void* args, XObject* sender)
//{
//    XPrintf("sender:%p receiver:%p 串口释放\n",sender,receiver);
//}
//
//static void connected_slot(XObject* receiver, void* args, XObject* sender)
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
//void XModbusTest()
//{
//    XSerialPortBase* serial = XSerialPort_create();
//    XObject_connect(serial, XSignal(XObject_deinit_signal), serial, deinit_slot, XConnectionType_Auto);
//    XObject_connect(serial, XSignal(XObject_deinit_signal), serial, deinit_slot, XConnectionType_Auto| XConnectionType_Unique);
//    serial->m_baudRate = 38400;
//    serial->m_portNum = 2;
//    XSocket* socket = XSocket_create();
//    //XIODevice_setWriteBuffer_base(socket,512);
//    XObject_connect(socket, XSignal(XSocket_connected_signal), socket, connected_slot, XConnectionType_Auto);
//    XObject_connect(socket, XSignal(XSocket_stateChanged_signal), socket, stateChanged_slot, XConnectionType_Auto);
//    XSocket_connectToHost_base(socket, "192.168.1.117", 500, XIODevice_ReadWrite);
//    //XObject_delete_event(serial);
//    XSerialPort_delete_base(serial);
//    
//    XModbus* modbus = XModbus_create_RTU(socket,NULL,NULL);
//    //XDataFrameComm_setSendMod();
//    XModbus_setAddress(modbus,2);
//    XModbus_setMode(modbus, XMB_RTU_MASTER);
//    //XModbus_setRecvHandMode(modbus, XMB_RecvHand_CodeOnly);
//   // return;
//    XModbusDigitalSwitch* ds = XModbusDigitalSwitch_create(modbus,0x01,8,8);
//    //XModbusDigitalSwitch_bindModbus_RTU(ds,modbus);
//    //XDataFrameComm_setCommMode_base(modbus,XDFC_COMM_MODE_FULL_DUPLEX);
//
//    XModbusDigitalSwitch_setScanningPeriod(ds,30);
//    XModbusDigitalSwitch_setScanningPeriod(ds, 200);
//    {
//        XSwitchDeviceModbus* sw0 = XSwitchDeviceModbus_create(ds, 0);
//        XSwitchDeviceBase_open_base(sw0, XIODevice_WriteOnly);
//        XSwitchDeviceBase_setState_base(sw0, true);
//        SW = sw0;
//
//        XSwitchDeviceModbus* sw1 = XSwitchDeviceModbus_create(ds, 1);
//        XSwitchDeviceBase_open_base(sw1, XIODevice_WriteOnly);
//        XSwitchDeviceBase_setState_base(sw1, true);
//
//        XSwitchDeviceModbus* sw2 = XSwitchDeviceModbus_create(ds, 2);
//        XSwitchDeviceBase_open_base(sw2, XIODevice_WriteOnly);
//        XSwitchDeviceBase_setState_base(sw2, true);
//    }
//    {
//        XSwitchDeviceModbus* sw0 = XSwitchDeviceModbus_create(ds, 0);
//        XSwitchDeviceBase_open_base(sw0, XIODevice_ReadOnly);
//        XSwitchDeviceBase_setStateChangeCallback(sw0, StateChangeCallback0);
//
//        XSwitchDeviceModbus* sw1 = XSwitchDeviceModbus_create(ds, 1);
//        XSwitchDeviceBase_open_base(sw1, XIODevice_ReadOnly);
//        XSwitchDeviceBase_setStateChangeCallback(sw1, StateChangeCallback1);
//
//        XSwitchDeviceModbus* sw2 = XSwitchDeviceModbus_create(ds, 2);
//        XSwitchDeviceBase_open_base(sw2, XIODevice_ReadOnly);
//        XSwitchDeviceBase_setStateChangeCallback(sw2, StateChangeCallback2);
//    }
//
//    XModbusRegister* Register=XModbusRegister_create(16);
//    //设置从站的功能码回调函数
//    {
//        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_READ_HOLDING_REGISTER, XModbusRegister_0x03_RTU_slaveRecvHandCb, Register);
//    }
//    {
//        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_WRITE_REGISTER, XModbusRegister_0x06_RTU_slaveRecvHandCb, Register);
//    }
//    {//发送一帧数据
//        XVector* frame = XVector_Create(uint8_t);
//        char buff[] = {0x00,0x01};
//        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x01, buff);
//        //XModbus_sendData_base(modbus, frame);
//        //XModbus_sendDataPeriodicMaster_base(modbus, frame,50);
//    }
//    {//发送一帧数据
//   
//       // printf("%d\n",frame->recvHandle->timeout);
//        char buff[] = { 0x00,0x01 };
//        //XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x00, buff);
//        //XModbusBase_sendFrame(modbus, frame);
//        //printf("发送\n");
//        //XModbusBase_sendFrameRegularlyMaster(modbus, frame,50);
//        XVector* frame = XVector_Create(uint8_t);
//        uint8_t State =0;
//        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
//        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
//        XMODBUS_UINT8_SET_BITS(&State,3,1);
//        XMODBUS_UINT8_SET_BITS(&State, 7, 1);
//
//        XModbusFrameRTU_setFrameData_0x0F_request(frame, 0x01,0x0, 8, &State);
//        XModbus_sendData_base(modbus, frame);
//    }
//    {
//        XVector* frame = XVector_Create(uint8_t);
//        XModbusFrameRTU_setFrameData_0x01_request(frame, 0x01, 0x0, 8);
//        //XModbus_sendDataPeriodicMaster_base(modbus, frame,500);
//    }
//
//    //使能打开Modbus
//   // XModbus_enable(modbus);
//    if (!XModbus_connect_base(modbus))
//    {
//        //XSerialPort_delete_base(USART);//内存管理已经被XModbus接管
//        XModbus_delete_base(modbus);
//        XCoreApplication_quit();
//        return;
//    }
//    //XTimerBase_delay_ms(100);
//}
//
//
//void XMenu_XModbusTest(XMenu* root)
//{
//    XMenu* menu = XMenu_create("XModbus(modbus)");
//    XMenu_addMenu(root, menu);
//    {
//        XAction* action = XMenu_addAction(menu, "主测试");
//        XAction_setAction(action, XModbusTest);
//    }
//}