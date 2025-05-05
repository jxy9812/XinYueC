#include"XProtocolStackTest.h"
#include"XModbusRtu.h"
#include"XMemory.h"
#include"XCrc.h"
#include"XModbusFrameData.h"
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
static  HANDLE hEvent;
static OVERLAPPED ov;
// 打开串口
static bool SerialInit(XModbus* modbus, uint8_t port, uint32_t baudRate, XModbusParity parity)
{
    char portName[10] = {0};
    sprintf(portName,"COM%d",port);
   // 打开串口
    hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return FALSE;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, 1024, 1024)) {
        printf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 获取当前串口配置
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 设置串口参数
    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = parity;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        printf("无法设置串口状态！\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 3. 设置超时
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;  // 完全异步模式
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(hSerial, &timeouts))
        printf("设置超时失败");

    // 4. 创建异步操作结构
    //OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (ov.hEvent == NULL)
        printf("创建事件对象失败");

    // 5. 设置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR))
        printf("设置事件掩码失败");

    // 关闭 DTR 和 RTS
    if (!EscapeCommFunction(hSerial, CLRDTR)) {
        DWORD errorCode = GetLastError();
        printf("无法关闭 DTR！错误代码: %lu\n", errorCode);
        CloseHandle(hSerial);
        return 1;
    }
    if (!EscapeCommFunction(hSerial, CLRRTS)) {
        DWORD errorCode = GetLastError();
        printf("无法关闭 RTS！错误代码: %lu\n", errorCode);
        CloseHandle(hSerial);
        return 1;
    }
    return TRUE;
}
static bool XModbus_GetByte(XModbus* modbus, uint8_t* byte)
{
	DWORD bytesRead;
    if (!ReadFile(hSerial, byte, 1, &bytesRead, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("读取失败\n");
            return false;
        }

        // 等待异步操作完成
        if (!GetOverlappedResult(hSerial, &ov, &bytesRead, TRUE))
        {
            printf("异步读取失败\n");
            return false;
        }
	}
   // printf("接收到字符%c\n", *byte);
	return TRUE;
}
static bool XModbus_PutByte(XModbus* modbus, uint8_t Byte)
{
	DWORD bytesWritten;
	if (!WriteFile(hSerial, &Byte,1, &bytesWritten, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("写入失败");
            return FALSE;
        }
        // 等待异步操作完成
        if (!GetOverlappedResult(hSerial, &ov, &bytesWritten, TRUE))
        {
            printf("异步写入失败");
            return FALSE;
        }
	}
	return TRUE;
}

// 定时器回调函数
VOID CALLBACK TimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) 
{
    XTimer* timer = ((XTimer*)dwUser);
    XTimer_out(timer);
}
static  void XTimer_Start(XTimer* timer)
{
    XTimer_stop(timer);
    // 创建定时器，间隔为 2 毫秒
    UINT timerId = timeSetEvent(timer->interval, 1, TimerCallback, timer, TIME_PERIODIC);
    if (timerId == 0) {
        timeEndPeriod(1);
        printf("定时器创建失败\n");
    }
    timer->timerId = timerId;
}
static  void XTimer_Stop(XTimer* timer)
{
    if(timer->timerId)
    {
        timeKillEvent(timer->timerId);
        timer->timerId = 0;
        //printf("停止定时器\n");
    }
}
static  void XTimerCreat(XTimer* timer)
{
    // 设置定时器分辨率
    //timeBeginPeriod(1);
    //timer->data = XMemory_malloc(sizeof(UINT));
    timer->start = XTimer_Start;
    timer->stop = XTimer_Stop;
}

static void SerialPoll(XModbus* modbus)
{
    COMSTAT comStat;
    DWORD errors;

    // 清除通信错误并获取串口状态
    if (!ClearCommError(hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return FALSE;
    }
    if (comStat.cbInQue)
    {
        modbus->pxMBFrameCBByteReceived(modbus);
    }
    else
    {
        modbus->pxMBFrameCBTransmitterEmpty(modbus);
    }
    // 重置事件
    ResetEvent(ov.hEvent);
}
#endif // WIN32
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
    InitFunction.xGetByte = XModbus_GetByte;
    InitFunction.xPutByte = XModbus_PutByte;
    InitFunction.SerialInit = SerialInit;
    InitFunction.TimerCreate = XTimerCreat;
    InitFunction.TimerStart = XTimer_Start;
    InitFunction.TimerStop = XTimer_Stop;
    XModbus* modbus = XMemory_malloc(sizeof(XModbus));
    XModbus_init(modbus, &InitFunction, MB_RTU_MASTER, 0x01, 2, 38400, MB_PAR_NONE);
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
        XModbus_sendData(modbus, frame);
    }
    {
        XModbusFrameData* frame = XModbusFrameData_newRecvHandle();
        //frame->recvHandle->pRecvHandCallFunc = RtuDataFrame_0x06_reply;
        char buff[] = { 0x00,0x01 };
        XModbusFrameDataRTU_setDataFrame_0x06_request(frame, 0x01,  0x00, buff);
        XModbus_sendData(modbus, frame);
    }
    XModbus_enable(modbus);
    while (true)
    {
        XModbus_poll(modbus);
        SerialPoll(modbus);
    }
}