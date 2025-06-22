#include"XProtocolStackTest.h"
#include"XModbus.h"
#include"XSerialPortBase.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrame.h"
#include"XTimerGroupBase.h"
#include"XModbusRegister.h"
#include"XModbusDigitalSwitch.h"
#include"XSwitchDeviceModbus.h"
static XSwitchDeviceModbus* SW;
static void StateChangeCallback0(XSwitchDeviceBase* sw)
{
    printf("in0:%s\n",sw->m_state?"true":"false");
    XSwitchDeviceBase_setState_base(SW, sw->m_state);
}
static void StateChangeCallback1(XSwitchDeviceBase* sw)
{
    printf("in1:%s\n", sw->m_state ? "true" : "false");
}
static void StateChangeCallback2(XSwitchDeviceBase* sw)
{
    printf("in2:%s\n", sw->m_state ? "true" : "false");
}
void XModbusTest()
{
    XSerialPortBase* serial = XSerialPortWin32_create();
    serial->m_baudRate = 38400;
    serial->m_portNum = 2;
    XModbus* modbus = XModbus_create_RTU_SerialPort(serial,NULL,NULL);
    XModbus_setAddress(modbus,2);
    XModbus_setMode(modbus, XMB_RTU_MASTER);
    //XModbus_setRecvHandMode(modbus, XMB_RecvHand_CodeOnly);
 
    XModbusDigitalSwitch* ds = XModbusDigitalSwitch_create(modbus,0x01,8,8);
    //XModbusDigitalSwitch_bindModbus_RTU(ds,modbus);
    XModbusDigitalSwitch_setScanningPeriod_RTU(ds,50);

    {
        XSwitchDeviceModbus* sw0 = XSwitchDeviceModbus_create(ds, 0);
        XSwitchDeviceBase_open_base(sw0, XIODeviceBase_WriteOnly);
        XSwitchDeviceBase_setState_base(sw0, true);
        SW = sw0;

        XSwitchDeviceModbus* sw1 = XSwitchDeviceModbus_create(ds, 1);
        XSwitchDeviceBase_open_base(sw1, XIODeviceBase_WriteOnly);
        XSwitchDeviceBase_setState_base(sw1, true);

        XSwitchDeviceModbus* sw2 = XSwitchDeviceModbus_create(ds, 2);
        XSwitchDeviceBase_open_base(sw2, XIODeviceBase_WriteOnly);
        XSwitchDeviceBase_setState_base(sw2, true);
    }
    {
        XSwitchDeviceModbus* sw0 = XSwitchDeviceModbus_create(ds, 0);
        XSwitchDeviceBase_open_base(sw0, XIODeviceBase_ReadOnly);
        XSwitchDeviceBase_setStateChangeCallback(sw0, StateChangeCallback0);

        XSwitchDeviceModbus* sw1 = XSwitchDeviceModbus_create(ds, 1);
        XSwitchDeviceBase_open_base(sw1, XIODeviceBase_ReadOnly);
        XSwitchDeviceBase_setStateChangeCallback(sw1, StateChangeCallback1);

        XSwitchDeviceModbus* sw2 = XSwitchDeviceModbus_create(ds, 2);
        XSwitchDeviceBase_open_base(sw2, XIODeviceBase_ReadOnly);
        XSwitchDeviceBase_setStateChangeCallback(sw2, StateChangeCallback2);
    }

    XModbusRegister* Register=XModbusRegister_create(16);
    //设置从站的功能码回调函数
    {
        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_READ_HOLDING_REGISTER, XModbusRegister_0x03_RTU_slaveRecvHandCb, Register);
    }
    {
        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_WRITE_REGISTER, XModbusRegister_0x06_RTU_slaveRecvHandCb, Register);
    }
    {//发送一帧数据
        XVector* frame = XVector_Create(uint8_t);
        char buff[] = {0x00,0x01};
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x01, buff);
        //XModbus_sendData_base(modbus, frame);
        //XModbus_sendDataPeriodicMaster_base(modbus, frame,50);
    }
    {//发送一帧数据
   
       // printf("%d\n",frame->recvHandle->timeout);
        char buff[] = { 0x00,0x01 };
        //XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x00, buff);
        //XModbusBase_sendFrame(modbus, frame);
        //printf("发送\n");
        //XModbusBase_sendFrameRegularlyMaster(modbus, frame,50);
        XVector* frame = XVector_Create(uint8_t);
        uint8_t State =0;
        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
        XMODBUS_UINT8_SET_BITS(&State,3,1);
        XMODBUS_UINT8_SET_BITS(&State, 7, 1);

        XModbusFrameRTU_setFrameData_0x0F_request(frame, 0x01,0x0, 8, &State);
        //XModbus_sendData_base(modbus, frame);
    }
    {
        XVector* frame = XVector_Create(uint8_t);
        XModbusFrameRTU_setFrameData_0x01_request(frame, 0x01, 0x0, 8);
        //XModbus_sendDataPeriodicMaster_base(modbus, frame,500);
    }

    //使能打开Modbus
   // XModbus_enable(modbus);
    XModbus_connect_base(modbus);
   // XModbusTest_threadReceiveCreate(modbus);
    //开始轮询
    while (true)
    {
        XModbus_poll_base(modbus);
        XTimerGroupBase_global_poll();
       // XModbus_poll(modbus);
       // XModbusTest_SerialPoll(modbus);
    }
}