#include"XProtocolStackTest.h"
#include"XModbusTest_Port.h"
#include"XModbusRtu.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrameData.h"
//0x6 功能码响应
static void RtuDataFrame_0x06_reply(XModbus* modbus, XModbusFrameData* frame)
{
    if(frame==NULL)
        printf("超时了\n");
    printf("执行自定义回调%p\n",frame);
   //exit(0);
}

void XModbusTest()
{
    //UCHAR buffer[] = {0x01,0x06,0x00,0x00,0x00,0x01,0x48,0x0A };
    //UCHAR buffer[] = { 0x01,0x06,0x00,0x00,0x00,0x02,0x08,0x0B };
    //printf("CRC校验为:%d\n", modbus_crc16_table(buffer, sizeof(buffer)));
    //printf("CRC校验为:%d\n", XCRC16(buffer, sizeof(buffer)-2));
    XModbus_InitFunction InitFunction = {0};
    InitFunction.xGetByte = XModbusTest_GetByte;
    InitFunction.xPutByte = XModbusTest_PutByte;
    InitFunction.SerialInit = XModbusTest_SerialInit;
    InitFunction.TimerCreate = XModbusTest_XTimerCreat;
    InitFunction.TimerStart = XModbusTest_XTimer_Start;
    InitFunction.TimerStop = XModbusTest_XTimer_Stop;
    XModbus* modbus = XMemory_malloc(sizeof(XModbus));
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
    {
        XModbusFrameData* frame = XModbusFrameData_newRecvHandle();
        frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = {0x00,0x01};
        XModbusFrameDataRTU_setDataFrame_0x06_request(frame, 0x01,  0x01, buff);
        //XModbus_sendData(modbus, frame);
    }
    {
        XModbusFrameData* frame = XModbusFrameData_newRecvHandle();
        //frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = { 0x00,0x01 };
        //XModbusFrameDataRTU_setDataFrame_0x06_request(frame, 0x01,  0x00, buff);
        uint8_t State =0;
        
        XMODBUS_UINT8_SET_BITS(&State, 0, 1);
        XMODBUS_UINT8_SET_BITS(&State, 2, 1); 
        XMODBUS_UINT8_SET_BITS(&State,3,1);
        XMODBUS_UINT8_SET_BITS(&State, 7, 1);

        XModbusFrameDataRTU_setDataFrame_0x0F_request(frame, 0x01,0x0, 8, &State);
        XModbus_sendData(modbus, frame);
    }
    XModbus_enable(modbus);
    while (true)
    {
        XModbus_poll(modbus);
        XModbusTest_SerialPoll(modbus);
    }
}