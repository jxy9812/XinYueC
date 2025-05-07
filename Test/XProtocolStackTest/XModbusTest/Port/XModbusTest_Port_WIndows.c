#ifdef WIN32
//Windows接口文件
#include"XModbusTest_Port.h"
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
static  HANDLE hEvent;
static OVERLAPPED ov;
// 打开串口
bool XModbusTest_SerialInit(XModbus* modbus, uint8_t port, uint32_t baudRate, XModbusParity parity)
{
    char portName[10] = { 0 };
    sprintf(portName, "COM%d", port);
    // 打开串口
    hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, 1024, 1024)) {
        printf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 获取当前串口配置
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 设置串口参数
    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = parity;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        printf("无法设置串口状态！\n");
        CloseHandle(hSerial);
        return false;
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
    ov.hEvent = CreateEvent(NULL, true, false, NULL);

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
    return true;
}
bool XModbusTest_GetByte(XModbus* modbus, uint8_t* byte)
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
        if (!GetOverlappedResult(hSerial, &ov, &bytesRead, true))
        {
            printf("异步读取失败\n");
            return false;
        }
    }
    // printf("接收到字符%c\n", *byte);
    return true;
}
bool XModbusTest_PutByte(XModbus* modbus, uint8_t Byte)
{
    DWORD bytesWritten;
    if (!WriteFile(hSerial, &Byte, 1, &bytesWritten, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("写入失败");
            return false;
        }
        // 等待异步操作完成
        if (!GetOverlappedResult(hSerial, &ov, &bytesWritten, true))
        {
            printf("异步写入失败");
            return false;
        }
    }
    return true;
}

// 定时器回调函数
VOID CALLBACK TimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    XTimer* timer = ((XTimer*)dwUser);
    XTimer_out(timer);
}
void XModbusTest_XTimer_Start(XTimer* timer)
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
void XModbusTest_XTimer_Stop(XTimer* timer)
{
    if (timer->timerId)
    {
        timeKillEvent(timer->timerId);
        timer->timerId = 0;
        //printf("停止定时器\n");
    }
}
void XModbusTest_XTimerCreat(XTimer* timer)
{
    // 设置定时器分辨率
    //timeBeginPeriod(1);
    //timer->data = XMemory_malloc(sizeof(UINT));
    timer->start = XModbusTest_XTimer_Start;
    timer->stop = XModbusTest_XTimer_Stop;
}

void XModbusTest_SerialPoll(XModbus* modbus)
{
    COMSTAT comStat;
    DWORD errors;

    // 清除通信错误并获取串口状态
    if (!ClearCommError(hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return false;
    }
    if (comStat.cbInQue)
    {
        //接收缓冲区空时调用
        modbus->pxMBFrameCBByteReceived(modbus);
    }
    else
    {
        //写入缓冲区空时调用
        modbus->pxMBFrameCBTransmitterEmpty(modbus);
    }
    // 重置事件
    ResetEvent(ov.hEvent);
}
#endif // WIN32