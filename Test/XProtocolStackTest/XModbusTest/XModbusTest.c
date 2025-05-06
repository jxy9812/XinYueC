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
    XModbus_InitFunction InitFunction = {0};
    InitFunction.xGetByte = XModbusTest_GetByte;
    InitFunction.xPutByte = XModbusTest_PutByte;
    InitFunction.SerialInit = XModbusTest_SerialInit;
    InitFunction.TimerCreate = XModbusTest_XTimerCreat;
    InitFunction.TimerStart = XModbusTest_XTimer_Start;
    InitFunction.TimerStop = XModbusTest_XTimer_Stop;
    XModbus* modbus = XMemory_malloc(sizeof(XModbus));
    //初始化Modbus
    XModbus_init(modbus, &InitFunction, MB_RTU_MASTER, 0x02, 2, 38400, MB_PAR_NONE);
    XModbusRegisterFunc* Register=XModbusRegisterFunc_new(16);
    //设置从站的功能码回调函数
    {
        XModbusFunctionHandler Handler = { MB_FUNC_READ_HOLDING_REGISTER,XModbusRegisterFunc_0x03_RTU_slaveRecvHandCallFunc,Register };
        XModbus_setFunctionHandler(modbus, &Handler);
    }

    {
        XModbusFunctionHandler Handler = { MB_FUNC_WRITE_REGISTER,XModbusRegisterFunc_0x06_RTU_slaveRecvHandCallFunc,Register };
        XModbus_setFunctionHandler(modbus, &Handler);
    }
    {//发送一帧数据
        XModbusFrame* frame = XModbusFrame_newRecvHandle();
        frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = {0x00,0x01};
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x01, buff);
        XModbus_sendData(modbus, frame);
    }
    {//发送一帧数据
        XModbusFrame* frame = XModbusFrame_newRecvHandle();
        //frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = { 0x00,0x01 };
        XModbusFrameRTU_setFrameData_0x06_request(frame, 0x01,  0x00, buff);
        XModbus_sendData(modbus, frame);
       /* uint8_t State =0;
        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
        XMODBUS_UINT8_SET_BITS(&State,3,1);
        XMODBUS_UINT8_SET_BITS(&State, 7, 1);

        XModbusFrameRTU_setFrameData_0x0F_request(frame, 0x01,0x0, 8, &State);
        XModbus_sendData(modbus, frame);*/
    }
    //使能打开Modbus
    XModbus_enable(modbus);
    //开始轮询
    while (true)
    {
        XModbus_poll(modbus);
        XModbusTest_SerialPoll(modbus);
    }
}