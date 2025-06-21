#include"XProtocolStackTest.h"
#include"XModbusBase.h"
#include"XSerialPortBase.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrame.h"
#include"XTimerGroupBase.h"
#include"XModbusRegisterHandler.h"
//0x6 功能码响应
static void RtuDataFrame_0x06_reply(XModbus* modbus, XModbusFrame* frame)
{
    if(frame==NULL)
        printf("超时了\n");
    printf("执行自定义回调%p\n",frame);
   //exit(0);
}

void XModbusTest()
{
    XSerialPortBase* serial = XSerialPortWin32_create();
    serial->m_baudRate = 38400;
    serial->m_portNum = 2;
    XModbus* modbus = XModbus_create_RTU_SerialPort(serial,NULL,NULL);
    XModbus_setAddress(modbus,2);
    XModbus_setMode(modbus, MB_RTU_MASTER);
    //初始化Modbus
   // XModbus_init(modbus, &InitFunction, MB_RTU_MASTER, 0x02, 2, 38400, MB_PAR_NONE);
    XModbusRegisterHandler* Register=XModbusRegisterHandler_create(16);
    printf("设置回调\n");
    //设置从站的功能码回调函数
    {
        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_READ_HOLDING_REGISTER, XModbusRegisterHandler_0x03_RTU_slaveRecvHandCallFunc, Register);
    }
    printf("设置回调\n");
    {
        XModbus_addRecvHand_CodeOnly(modbus, MB_FUNC_WRITE_REGISTER, XModbusRegisterHandler_0x06_RTU_slaveRecvHandCallFunc, Register);
    }
    {//发送一帧数据
        XVector* frame = XVector_Create(uint8_t);
        char buff[] = {0x00,0x01};
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x01, buff);
        XModbus_sendFrame_base(modbus, frame);
        //XModbus_sendFrameRegularlyMaster(modbus, frame,50);
    }
    {//发送一帧数据
   
       // printf("%d\n",frame->recvHandle->timeout);
        char buff[] = { 0x00,0x01 };
        //XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x00, buff);
        //XModbusBase_sendFrame(modbus, frame);
        //printf("发送\n");
        //XModbusBase_sendFrameRegularlyMaster(modbus, frame,50);
       /* uint8_t State =0;
        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
        XMODBUS_UINT8_SET_BITS(&State,3,1);
        XMODBUS_UINT8_SET_BITS(&State, 7, 1);

        XModbusFrameRTU_setFrameData_0x0F_request(frame, 0x01,0x0, 8, &State);
        XModbus_sendFrame(modbus, frame);*/
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