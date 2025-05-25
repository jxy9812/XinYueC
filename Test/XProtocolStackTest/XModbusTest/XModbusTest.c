#include"XProtocolStackTest.h"
#include"XModbusTest_Port.h"
#include"XModbusRtu.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrame.h"
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
    //初始化信息
    XIODevice_PortFunc io_port = { 0 };//串口设备接口设置
    io_port.open_funcPointer = XModbusTest_SerialOpen;
    io_port.readData_funcPointer = XModbusTest_readByte;
    io_port.writeData_funcPointer = XModbusTest_writeByte;
    XTimer_PortFunc  timePort = XTimer_PortFunc_Win32TimeSetEvent();//定时器接口设置
    XModbus_PortFunc InitFunction = {0};//modbus接口设置
    InitFunction.IO_Port = io_port;
    InitFunction.timePort = timePort;
    XModbus* modbus = XMemory_malloc(sizeof(XModbus));
    //初始化Modbus
    XModbus_init(modbus, &InitFunction, MB_RTU_MASTER, 0x02, 2, 38400, MB_PAR_NONE);
    XModbusRegisterHandler* Register=XModbusRegisterHandler_new(16);
    //设置从站的功能码回调函数
    {
        XModbusFunctionHandler Handler = { MB_FUNC_READ_HOLDING_REGISTER,XModbusRegisterHandler_0x03_RTU_slaveRecvHandCallFunc,Register };
        XModbus_setFunctionHandler(modbus, &Handler);
    }

    {
        XModbusFunctionHandler Handler = { MB_FUNC_WRITE_REGISTER,XModbusRegisterHandler_0x06_RTU_slaveRecvHandCallFunc,Register };
       //XModbus_setFunctionHandler(modbus, &Handler);
    }
    {//发送一帧数据
        XModbusFrame* frame = XModbusFrame_newRecvHandle();
        frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = {0x00,0x01};
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x01, buff);
       // XModbus_sendFrame(modbus, frame);
        //XModbus_sendFrameRegularlyMaster(modbus, frame,50);
    }
    {//发送一帧数据
        XModbusFrame* frame = XModbusFrame_newRecvHandle();
        //frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = { 0x00,0x01 };
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x00, buff);
        //XModbus_sendFrame(modbus, frame);
        XModbus_sendFrameRegularlyMaster(modbus, frame,100);
       /* uint8_t State =0;
        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
        XMODBUS_UINT8_SET_BITS(&State,3,1);
        XMODBUS_UINT8_SET_BITS(&State, 7, 1);

        XModbusFrameRTU_setFrameData_0x0F_request(frame, 0x01,0x0, 8, &State);
        XModbus_sendFrame(modbus, frame);*/
    }
    //使能打开Modbus
    XModbus_enable(modbus);
    XModbusTest_threadReceiveCreate(modbus);
    //开始轮询
    while (true)
    {
        XModbus_poll(modbus);
       // XModbusTest_SerialPoll(modbus);
    }
}